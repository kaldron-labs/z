#!/usr/bin/env python3
"""Verify checkpoint restart and explicit reset across fresh independent clients."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import platform
import selectors
import subprocess
import sys
import tempfile
import time
import traceback


def load_pair():
    path = Path(__file__).resolve().parents[1] / "util" / "pair-test.py"
    spec = importlib.util.spec_from_file_location("zfix_pair", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class Feed:
    """Preserve all JSON records, including events sharing one pipe read."""
    def __init__(self, command, directory, name):
        self.path = directory / f"{name}.jsonl"
        self.error = (directory / f"{name}.log").open("wb")
        self.output = self.path.open("wb")
        self.process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=self.error,
                                        start_new_session=(os.name == "posix"))
        self.error.close()
        self.pending = b""
        self.rows = []
        self.used = set()

    def append(self, chunk):
        self.output.write(chunk)
        self.output.flush()
        self.pending += chunk
        while b"\n" in self.pending:
            line, self.pending = self.pending.split(b"\n", 1)
            if line.strip():
                self.rows.append(json.loads(line))

    def wait(self, event, timeout):
        deadline = time.monotonic() + timeout
        with selectors.DefaultSelector() as selector:
            selector.register(self.process.stdout, selectors.EVENT_READ)
            while True:
                for index, row in enumerate(self.rows):
                    if index not in self.used and row.get("event") == event:
                        self.used.add(index)
                        return row
                remaining = deadline - time.monotonic()
                assert remaining > 0 and selector.select(remaining), f"{event} deadline"
                chunk = os.read(self.process.stdout.fileno(), 65536)
                assert chunk, f"closed output before {event}: {self.path}"
                self.append(chunk)

    def finish(self, timeout):
        chunk, _ = self.process.communicate(timeout=timeout)
        self.append(chunk)
        assert not self.pending.strip(), f"incomplete JSON: {self.path}"
        assert self.process.returncode == 0, (self.path, self.process.returncode)
        return self.rows

    def close(self, pair):
        pair.stop(self.process)
        if not self.process.stdout.closed:
            chunk, _ = self.process.communicate(timeout=3)
            self.append(chunk)
        self.output.close()


def completed(rows, count, client, connections, recover=False):
    results = [row for row in rows if row.get("event") == "result"]
    assert len(results) == 1, rows
    row = results[0]
    assert row["session"] == 1 and row["errors"] == 0, row
    p = row["progress"]
    assert p["admitted"] == p["complete"] == count, row
    assert p["reports"] >= count * 2 if recover else p["reports"] == count * 2, row
    assert p["unknown"] == p["invalid"] == p["refused"] == 0, row
    assert row["connections"] == connections, row
    if client:
        assert p["offered"] == p["ack"] == count and p["inFlight"] == 0, row
        assert row["ackNs"]["count"] == row["terminalNs"]["count"] == count, row
    return row


def wire(pair, path):
    rows = pair.records(path)
    results = [r for r in rows if r["event"] == "result"]
    assert len(results) == 1 and results[0]["errors"] == 0, results
    return [r for r in rows if r["event"] == "frame"]


def continuity(first, second, reset):
    evidence = {}
    for direction in ("up", "down"):
        previous = [r for r in first if r["direction"] == direction and r["possDup"] != "Y"]
        current = [r for r in second if r["direction"] == direction]
        assert previous and current and current[0]["kind"] == "A", current
        logon = current[0]
        prior = max(r["seq"] for r in previous)
        if reset:
            assert logon["seq"] == 1 and logon["admin"].get("141") == "Y", logon
        else:
            assert logon["seq"] == prior + 1 and logon["admin"].get("141") != "Y", (prior, logon)
        evidence[direction] = {"previous_last": prior, "next_logon": logon}
    return evidence


def recovered(pair, first, second):
    original = [r for r in first if r["direction"] == "up" and r["kind"] == "D"
                and r["possDup"] != "Y"]
    assert original, "saved business history was not recorded"
    by_seq = {r["seq"]: r for r in original}
    persisted = set(by_seq)
    replay = []
    # An open-ended request can include new orders sent before the peer's
    # request reaches the SDK. Validate those replays too, against an original
    # already observed on this connection; every persisted order remains required.
    for r in second:
        if r["direction"] != "up" or r["kind"] != "D":
            continue
        seq = r["seq"]
        if r["possDup"] != "Y":
            assert seq not in by_seq, ("ordinary sequence reused", r)
            by_seq[seq] = r
            continue
        assert seq in by_seq, ("replay has no preceding original", r)
        old = by_seq[seq]
        assert r["body_sha256"] == old["body_sha256"], (old, r)
        assert pair.stamp(r["original"]) == pair.stamp(old["time"]), (old, r)
        assert pair.stamp(r["time"]) >= pair.stamp(r["original"]), r
        replay.append(r)
    assert persisted <= {r["seq"] for r in replay}, (original, replay)
    requests = [r for r in second if r["direction"] == "down" and r["kind"] == "2"]
    assert any(int(r["admin"]["7"]) <= min(persisted) and
               (int(r["admin"]["16"]) == 0 or int(r["admin"]["16"]) >= max(persisted))
               for r in requests), requests
    fills = [r for r in second if r["direction"] == "up" and r["kind"] == "4"
             and r["admin"].get("123") == "Y"]
    assert fills, "unavailable standard administrative history was not gap-filled"
    return {"replayed": replay, "requests": requests, "gap_fills": fills}


def run_case(pair, base, root, label, client, server, transport, profile):
    reset, recover = profile == "reset", profile == "recover"
    directory = root / f"{label}-{transport}-{profile}"
    directory.mkdir()
    common = ["--warmup", "0", "--sessions", "1", "--inflight", "4", "--deadline", "25",
              "--heartbeat", "30", "--version", "FIX.4.4", "--mode", "4"]
    tls = (["--tcp"] if transport == "tcp" else
           ["--cert", str(root / "cert.pem"), "--key", str(root / "key.pem"),
            "--ca", str(root / "cert.pem")])
    commands = {}
    peers = []
    case = {"pair": label, "transport": transport, "profile": profile, "reset": reset,
            "commands": commands, "status": "running", "events": []}

    def event(action, **values):
        case["events"].append({"event": action, "ns": time.monotonic_ns(), **values})

    def launch(name, command):
        commands[name] = command
        feed = Feed(command, directory, name)
        peers.append(feed)
        event("started", name=name, pid=feed.process.pid)
        return feed

    try:
        server_cmd = [server, "--port", "0", "--count", "8", "--connections", "2", *common, *tls]
        if reset:
            server_cmd += ["--reset-logon"]
        if recover:
            server_cmd += ["--recover-rx", "1"]
        acceptor = launch("server", server_cmd)
        opened = acceptor.wait("ready", 10)
        assert len(opened["ports"]) == 1 and opened["ports"][0] > 0, opened
        checkpoint = directory / "checkpoint.bin"
        traces = []
        results = []
        for phase in (1, 2):
            relay = launch(f"relay{phase}", [sys.executable,
                str(base / "interop" / "fault-proxy.py"),
                "--ports", str(opened["ports"][0]),
                "--trace", str(directory / f"wire{phase}.jsonl"),
                "--deadline", "35", *tls])
            endpoint = relay.wait("ready", 10)
            command = [client, "--host", "localhost", "--ports", str(endpoint["ports"][0]),
                       "--count", "4", *common, *tls]
            if phase == 1:
                command += ["--state-out", str(checkpoint)]
            else:
                command += ["--key-offset", "4"]
                command += ["--reset-logon"] if reset else ["--state-in", str(checkpoint)]
            initiator = launch(f"client{phase}", command)
            ready = initiator.wait("ready", 10)
            event("client-ready", phase=phase, record=ready)
            results.append(completed(initiator.finish(35), 4, True, 1))
            event("client-exited", phase=phase, exit=initiator.process.returncode)
            relay.finish(10)
            traces.append(wire(pair, directory / f"wire{phase}.jsonl"))
            if phase == 1:
                assert checkpoint.is_file() and checkpoint.stat().st_size > 0
                case["checkpoint"] = {"bytes": checkpoint.stat().st_size,
                                      "sha256": pair.digest(checkpoint)}
                barrier = acceptor.wait("reconnect_ready", 10)
                assert barrier["connection"] == 2 and barrier["ports"] == opened["ports"], barrier
                assert barrier["barrier"] in ("owner-drained", "sdk-drop-before-accept"), barrier
                event("reconnect-barrier", record=barrier)
        served = completed(acceptor.finish(35), 8, False, 2, recover)
        case["continuity"] = continuity(*traces, reset)
        if recover:
            case["recovery"] = recovered(pair, *traces)
            assert served["cached"] > 0 and results[1]["oldReports"] > 0, (served, results[1])
        case["results"] = {"clients": results, "server": served}
        case["status"] = "passed"
    except Exception as error:
        case.update(status="failed", error=str(error), traceback=traceback.format_exc())
        raise
    finally:
        for feed in reversed(peers):
            feed.close(pair)
        case["exits"] = {name: feed.process.returncode for name, feed in zip(commands, peers)}
        (directory / "case.json").write_text(json.dumps(case, indent=2) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    base = Path(__file__).resolve().parents[1]
    parser.add_argument("--dictionary", type=Path, required=True)
    for name, default in (("native-client", base / "util" / "zfix"),
                          ("native-server", base / "util" / "zfixd"),
                          ("quickfix-client", base / "interop" / "quickfix"),
                          ("quickfix-server", base / "interop" / "quickfixd")):
        parser.add_argument(f"--{name}", type=Path, default=default)
    parser.add_argument("--artifacts", type=Path)
    parser.add_argument("--tcp-only", action="store_true")
    parser.add_argument("--profile", choices=("resume", "reset", "recover"), action="append")
    parser.add_argument("--pair", choices=("native-native", "native-quickfix",
                                          "quickfix-native", "quickfix-quickfix"))
    options = parser.parse_args()
    pair = load_pair()
    root = (options.artifacts.resolve() if options.artifacts else
            Path(tempfile.mkdtemp(prefix="zfix-lifecycle-")))
    root.mkdir(parents=True, exist_ok=True)
    assert not any(root.iterdir()), f"artifact directory must be empty: {root}"
    lock = (base / "interop" / "quickfix.lock").read_text()
    expected = next(l.split("=", 1)[1] for l in lock.splitlines()
                    if l.startswith("QUICKFIX_FIX44_SHA256="))
    assert pair.digest(options.dictionary) == expected, "canonical dictionary mismatch"
    programs = {"native": (str(options.native_client.resolve()),
                           str(options.native_server.resolve())),
                "quickfix": (str(options.quickfix_client.resolve()),
                             str(options.quickfix_server.resolve()))}
    pair.certificate(root)
    manifest = {"platform": platform.platform(), "dictionary_sha256": expected,
                "quickfix_lock": lock, "harness_sha256": pair.digest(Path(__file__).resolve()),
                "programs": {p: {"wrapper_sha256": pair.digest(Path(p)),
                                  "binary_sha256": pair.digest(pair.executable(Path(p)))}
                             for values in programs.values() for p in values},
                "verification": "TLS relay validates upstream CA and localhost; "
                                "client validates relay",
                "scope": "fresh client checkpoint restart; same acceptor; "
                         "explicit reset and persisted replay separately"}
    (root / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    cases = [(f"{c}-{s}", programs[c][0], programs[s][1], t, profile)
             for c in programs for s in programs
             for t in (("tcp",) if options.tcp_only else ("tcp", "tls"))
             for profile in options.profile or ("resume", "reset", "recover")
             if not options.pair or options.pair == f"{c}-{s}"]
    failed = 0
    def tap(line):
        print(line, flush=True)
        with (root / "lifecycle.tap").open("a") as output:
            output.write(line + "\n")
    tap(f"# artifacts: {root}")
    tap(f"1..{len(cases)}")
    for index, (label, client, server, transport, profile) in enumerate(cases, 1):
        try:
            run_case(pair, base, root, label, client, server, transport, profile)
            tap(f"ok {index} - {label} {transport} {profile}")
        except Exception as error:
            failed += 1
            tap(f"not ok {index} - {label} {transport} {profile}: {error}")
    return bool(failed)


if __name__ == "__main__":
    sys.exit(main())
