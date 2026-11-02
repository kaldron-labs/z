#!/usr/bin/env python3
# (c) Copyright 2026 Huw Rogers
# This code is licensed by the MIT license (see LICENSE for details)
"""Exercise the built zi examples with real peer traffic, without sleep-based startup."""
import contextlib
import pathlib
import selectors
import socket
import subprocess
import tempfile
import unittest

EXAMPLE = pathlib.Path(__file__).resolve().parents[1] / "example"
TIMEOUT = 15


def port(kind):
    with socket.socket(socket.AF_INET, kind) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


@contextlib.contextmanager
def server(name, *args):
    with tempfile.TemporaryFile() as output:
        p = subprocess.Popen([str(EXAMPLE / name), *map(str, args)],
                             stdout=output, stderr=subprocess.PIPE)
        try:
            with selectors.DefaultSelector() as ready:
                ready.register(p.stderr, selectors.EVENT_READ)
                if not ready.select(TIMEOUT):
                    raise AssertionError(f"{name}: no readiness notification")
                line = p.stderr.readline()
                if line != b"listening\n":
                    raise AssertionError(f"{name}: {line!r}")
            yield p
            _, err = p.communicate(timeout=TIMEOUT)
            if p.returncode:
                raise AssertionError(f"{name}: exit {p.returncode}: {err!r}")
        finally:
            if p.poll() is None:
                p.kill()
                p.wait()
            p.stderr.close()


def client(name, *args):
    p = subprocess.run([str(EXAMPLE / name), *map(str, args)],
                       capture_output=True, timeout=TIMEOUT)
    if p.returncode:
        raise AssertionError(f"{name}: exit {p.returncode}: {p.stderr!r}")
    return p


class Examples(unittest.TestCase):
    def test_tcp(self):
        n = port(socket.SOCK_STREAM)
        with server("zimxserver", "-c", 4, "-l", 4, "127.0.0.1", n):
            client("zimxclient", "-c", 3, "-r", 3, "127.0.0.1", n)
            with socket.create_connection(("127.0.0.1", n), TIMEOUT) as s:
                for chunk in (b"GET / HTTP/1.1\r", b"\nHost: local\r\n", b"\r", b"\n"):
                    s.sendall(chunk)
                response = bytearray()
                while chunk := s.recv(65536):
                    response.extend(chunk)
            header, body = response.split(b"\r\n\r\n", 1)
            self.assertTrue(header.startswith(b"HTTP/1.1 200 OK"))
            size = int(next(line.split(b":", 1)[1] for line in header.split(b"\r\n")
                            if line.startswith(b"Content-Length:")))
            self.assertEqual(len(body), size)

    def test_tcp_early_close(self):
        n = port(socket.SOCK_STREAM)
        with server("zimxserver", "127.0.0.1", n):
            with socket.create_connection(("127.0.0.1", n), TIMEOUT) as s:
                s.sendall(b"GET / HTTP/1.1\r\nHost: local\r\n\r\n")
                s.shutdown(socket.SHUT_RDWR)

    def test_udp_client(self):
        n = port(socket.SOCK_DGRAM)
        with server("zimxudpserver", "-n", 10, "-b", f"127.0.0.1:{n}"):
            client("zimxudpclient", "-n", 10, "-d", f"127.0.0.1:{n}")

    def test_udp_pending(self):
        n = port(socket.SOCK_DGRAM)
        count = 40
        with server("zimxudpserver", "-n", count * 2, "-b", f"127.0.0.1:{n}"):
            with contextlib.ExitStack() as stack:
                peers = [stack.enter_context(socket.socket(socket.AF_INET, socket.SOCK_DGRAM))
                         for _ in range(2)]
                for s in peers:
                    s.settimeout(TIMEOUT)
                    s.bind(("127.0.0.1", 0))
                expected = [[], []]
                for i in range(count):
                    for peer, s in enumerate(peers):
                        msg = f"{peer}:{i}".encode() if i else b""
                        expected[peer].append(msg)
                        s.sendto(msg, ("127.0.0.1", n))
                for peer, s in enumerate(peers):
                    received = [s.recvfrom(65536)[0] for _ in range(count)]
                    self.assertEqual(received, expected[peer])


if __name__ == "__main__":
    unittest.main()
