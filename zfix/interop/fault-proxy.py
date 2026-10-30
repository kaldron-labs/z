#!/usr/bin/env python3
"""Verified TCP/TLS FIX fixture relay with deterministic frame faults."""
import argparse
import asyncio
import hashlib
import json
from pathlib import Path
import ssl
import sys
import time

SOH = b"\x01"
HEADER = {35, 49, 56, 34, 50, 57, 142, 143, 43, 97, 52, 122, 347, 369, 1128}


def fields(frame):
    first = frame.index(SOH)
    second = frame.index(SOH, first + 1)
    body = frame[second + 1:-7]
    header = []
    offset = 0
    while offset < len(body):
        end = body.index(SOH, offset)
        tag, value = body[offset:end].split(b"=", 1)
        if int(tag) not in HEADER:
            break
        header.append((int(tag), value))
        offset = end + 1
    return frame[:first], header, body[offset:]


def build(begin, header, body):
    content = b"".join(str(tag).encode() + b"=" + value + SOH for tag, value in header) + body
    wire = begin + SOH + b"9=" + str(len(content)).encode() + SOH + content
    return wire + b"10=" + f"{sum(wire) % 256:03d}".encode() + SOH


def duplicate(frame):
    begin, header, body = fields(frame)
    old = dict(header)
    if 52 not in old:
        raise ValueError("source frame lacks SendingTime")
    result = []
    for tag, value in header:
        if tag == 43 or tag == 122:
            continue
        if tag == 52:
            result.append((43, b"Y"))
        result.append((tag, value))
        if tag == 52:
            result.append((122, old.get(122, old[52])))
    return build(begin, result, body)


def mutate(frame, action):
    begin, header, body = fields(frame)
    if action == "unknown":
        return build(begin, header, body + b"9999=fixture" + SOH)
    if action == "qty":
        # Only the declared scalar-order fault uses this body edit; binary data
        # is otherwise opaque to the relay and never split into token strings.
        parts = body.split(SOH)
        parts = [b"38=invalid" if part.startswith(b"38=") else part for part in parts]
        return build(begin, header, SOH.join(parts))
    if action == "checksum":
        value = (int(frame[-4:-1]) + 1) % 256
        return frame[:-4] + f"{value:03d}".encode() + SOH
    if action == "length":
        first = frame.index(SOH)
        second = frame.index(SOH, first + 1)
        length = int(frame[first + 3:second]) + 1
        before = frame[:first + 1] + b"9=" + str(length).encode() + frame[second:-7]
        return before + b"10=" + f"{sum(before) % 256:03d}".encode() + SOH
    raise ValueError(f"unknown mutation {action}")


async def frame(reader, limit):
    try:
        begin = await reader.readuntil(SOH)
    except asyncio.IncompleteReadError as error:
        if not error.partial:
            return None
        raise
    length = await reader.readuntil(SOH)
    if not begin.startswith(b"8=FIX") or not length.startswith(b"9="):
        raise ValueError("source is not a FIX frame")
    count = int(length[2:-1])
    if count <= 0 or count > limit:
        raise ValueError("source frame exceeds fixture working-storage bound")
    content = await reader.readexactly(count + 7)
    if content[-7:-4] != b"10=" or content[-1:] != SOH:
        raise ValueError("invalid source checksum field boundary")
    return begin + length + content


class Relay:
    def __init__(self, options):
        self.options = options
        self.trace = options.trace.open("w")
        self.done = asyncio.Event()
        self.closed = 0
        self.opened = 0
        self.errors = 0
        self.tasks = set()
        self.start = time.monotonic_ns()
        self.frames = 0
        self.chunks = 0
        self.faults = 0
        self.bytes = 0
        self.client_tls = None
        if not options.tcp:
            self.client_tls = ssl.create_default_context(cafile=str(options.ca))

    def record(self, event, **values):
        if self.options.summary and event in ("frame", "fault"):
            return
        self.trace.write(json.dumps({"event": event, "ns": time.monotonic_ns() - self.start,
                                     **values}) + "\n")
        self.trace.flush()

    def describe(self, wire):
        _, header, body = fields(wire)
        header = dict(header)
        values = {"kind": header[35].decode(), "seq": int(header[34]),
                  "possDup": header.get(43, b"N").decode(),
                  "time": header.get(52, b"").decode(),
                  "original": header.get(122, b"").decode()}
        if not self.options.summary:
            values["body_sha256"] = hashlib.sha256(body).hexdigest()
        if header[35] in (b"0", b"1", b"2", b"3", b"4", b"A"):
            admin = {}
            for token in body.split(SOH):
                if b"=" in token:
                    tag, value = token.split(b"=", 1)
                    if tag in (b"7", b"16", b"36", b"45", b"371", b"373", b"112", b"123", b"141", b"789"):
                        admin[tag.decode()] = value.decode()
            values["admin"] = admin
        return values

    async def pump(self, reader, writer, index, direction):
        matched = 0
        while True:
            wire = await frame(reader, self.options.frame_bytes)
            if wire is None:
                return
            info = self.describe(wire)
            self.frames += 1
            self.record("frame", session=index, direction=direction, **info)
            chosen = direction == self.options.direction and (
                info["kind"] == self.options.kind or
                self.options.kind == "*" and info["kind"] != "A")
            if chosen:
                matched += 1
            fault = (chosen and self.options.action != "pass"
                     and (self.options.action == "fragment" or self.options.repeat
                          or matched == self.options.at))
            output = [wire]
            if fault:
                self.faults += 1
                self.record("fault", session=index, direction=direction,
                            action=self.options.action, **info)
                if self.options.action == "drop":
                    output = []
                elif self.options.action == "duplicate":
                    output.append(duplicate(wire))
                elif self.options.action != "fragment":
                    output = [mutate(wire, self.options.action)]
            for value in output:
                if fault and self.options.action == "fragment":
                    step = self.options.fragment
                    for offset in range(0, len(value), step):
                        part = value[offset:offset + step]
                        writer.write(part)
                        await writer.drain()
                        self.chunks += 1
                        self.bytes += len(part)
                else:
                    writer.write(value)
                    await writer.drain()
                    self.chunks += 1
                    self.bytes += len(value)

    async def accept(self, reader, writer, index, port):
        current = asyncio.current_task()
        self.tasks.add(current)
        self.opened += 1
        upstream = None
        pumps = []
        try:
            kwargs = {"ssl": self.client_tls}
            if self.client_tls:
                kwargs.update(server_hostname=self.options.peer,
                              ssl_handshake_timeout=self.options.deadline)
            source, upstream = await asyncio.open_connection(
                self.options.host, port, limit=self.options.frame_bytes, **kwargs)
            self.record("connected", session=index, tls=not self.options.tcp)
            pumps = [asyncio.create_task(self.pump(reader, upstream, index, "up")),
                     asyncio.create_task(self.pump(source, writer, index, "down"))]
            done, pending = await asyncio.wait(pumps, return_when=asyncio.FIRST_COMPLETED)
            for task in done:
                task.result()
            for task in pending:
                task.cancel()
            await asyncio.gather(*pending, return_exceptions=True)
        except (ConnectionError, asyncio.IncompleteReadError) as error:
            # Peer disconnects are normal outcomes in negative protocol cases.
            self.record("closed", session=index, detail=str(error))
        except Exception as error:
            self.errors += 1
            self.record("error", session=index, detail=str(error))
            print(f"fault relay: {error}", file=sys.stderr, flush=True)
        finally:
            for task in pumps:
                if not task.done():
                    task.cancel()
            if pumps:
                await asyncio.gather(*pumps, return_exceptions=True)
            for output in (writer, upstream):
                if output:
                    output.close()
                    try:
                        await asyncio.wait_for(output.wait_closed(),
                                               timeout=self.options.close_timeout)
                    except asyncio.TimeoutError:
                        self.record("forced-close", session=index)
                        output.transport.abort()
                    except (ConnectionError, ssl.SSLError):
                        pass
            self.closed += 1
            self.tasks.discard(current)
            if self.closed == len(self.options.ports):
                self.done.set()

    async def run(self):
        server_tls = None
        if not self.options.tcp:
            server_tls = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            server_tls.load_cert_chain(self.options.cert, self.options.key)
        listeners = []
        try:
            ports = []
            for index, port in enumerate(self.options.ports):
                async def accepted(reader, writer, index=index, port=port):
                    await self.accept(reader, writer, index, port)
                kwargs = {"ssl": server_tls}
                if server_tls:
                    kwargs["ssl_handshake_timeout"] = self.options.deadline
                listener = await asyncio.start_server(
                    accepted, "127.0.0.1", 0, limit=self.options.frame_bytes, **kwargs)
                listeners.append(listener)
                ports.append(listener.sockets[0].getsockname()[1])
            print(json.dumps({"event": "ready", "engine": "fixture-relay", "ports": ports}),
                  flush=True)
            await asyncio.wait_for(self.done.wait(), timeout=self.options.deadline)
        finally:
            for listener in listeners:
                listener.close()
            for listener in listeners:
                await listener.wait_closed()
            tasks = list(self.tasks)
            for task in tasks:
                task.cancel()
            if tasks:
                await asyncio.gather(*tasks, return_exceptions=True)
            self.record("result", errors=self.errors, opened=self.opened, closed=self.closed,
                        frames=self.frames, chunks=self.chunks, bytes=self.bytes, faults=self.faults)
            self.trace.close()
        return 1 if self.errors else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ports", type=lambda s: [int(p) for p in s.split(",")], required=True)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--peer", default="localhost")
    parser.add_argument("--tcp", action="store_true")
    parser.add_argument("--cert", type=Path)
    parser.add_argument("--key", type=Path)
    parser.add_argument("--ca", type=Path)
    parser.add_argument("--trace", type=Path, required=True)
    parser.add_argument("--action", choices=("pass", "drop", "duplicate", "fragment",
                                              "unknown", "qty", "checksum", "length"), default="pass")
    parser.add_argument("--kind", default="D",
                        help="message type; * selects all post-Session::Logon frames")
    parser.add_argument("--direction", choices=("up", "down"), default="up")
    parser.add_argument("--at", type=int, default=1)
    parser.add_argument("--repeat", action="store_true")
    parser.add_argument("--fragment", type=int, default=1)
    parser.add_argument("--frame-bytes", type=int, default=1 << 20)
    parser.add_argument("--deadline", type=float, default=30)
    parser.add_argument("--close-timeout", type=float, default=3)
    parser.add_argument("--summary", action="store_true")
    options = parser.parse_args()
    if (not options.ports or any(p <= 0 or p >= 65536 for p in options.ports)
            or options.at <= 0 or options.fragment <= 0 or options.frame_bytes <= 0
            or options.deadline <= 0 or options.close_timeout <= 0):
        parser.error("positive ports, ordinal, fragment, storage bound and deadline required")
    if not options.tcp and not all((options.cert, options.key, options.ca)):
        parser.error("TLS requires fixture certificate, key and trusted upstream CA")
    try:
        return asyncio.run(Relay(options).run())
    except (asyncio.TimeoutError, OSError) as error:
        print(f"fault relay failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
