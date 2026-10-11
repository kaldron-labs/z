#!/usr/bin/env python3
# (c) Copyright 2026 Huw Rogers
# This code is licensed by the MIT license (see LICENSE for details)

"""Separate-process WSS echo control for the telemetry load peer.

Use the same ZTC_LOAD_{CLIENTS,SUBS,PUBLISHERS,ROUNDS,MESSAGES_PS,TURN}
settings as ztchubtest.py. Reports round-trip, not one-way, latency. No Zum,
routing, subscriptions, or authentication are performed by the echo endpoint.
"""

import json
import os
from pathlib import Path
import socket
import subprocess
import sys

root = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(root / "zi/itest"), str(root / "zum/itest")]
from zi_test_residue import Residue
from ztchubtest import wait_line


def main():
    residue = Residue("ztcdirecttest")
    directory = residue.directory
    peer = root / "ztc/itest/ztchubloadtest"
    cert, key = directory / "cert.pem", directory / "key.pem"
    subprocess.run([
        "openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes",
        "-days", "1", "-subj", "/CN=127.0.0.1",
        "-addext", "subjectAltName=IP:127.0.0.1",
        "-keyout", str(key), "-out", str(cert)], check=True,
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        port = sock.getsockname()[1]
    values = dict(devices=[], tokenFile="", token="", direct=True,
                  wss=f"wss://127.0.0.1:{port}/ztc", ca=str(cert), key=str(key))
    for name, env, default in (
            ("clients", "CLIENTS", 32), ("subs", "SUBS", 32),
            ("publishers", "PUBLISHERS", 256), ("rounds", "ROUNDS", 3),
            ("messagesPS", "MESSAGES_PS", 262144), ("turn", "TURN", 64)):
        values[name] = int(os.environ.get("ZTC_LOAD_" + env, default))
    for mode in ("echo", "client"):
        config = dict(values, echo=mode == "echo")
        (directory / (mode + ".cf")).write_text(
            ", ".join(k + ": " + json.dumps(v) for k, v in config.items()) + "\n")
    server = None
    success = False
    try:
        with (directory / "echo.err").open("wb") as error:
            server = subprocess.Popen([str(peer), str(directory / "echo.cf")],
                                      stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                      stderr=error)
            wait_line(server, "echo listening")
            run = subprocess.run([str(peer), str(directory / "client.cf")],
                                 capture_output=True, timeout=300)
            (directory / "client.log").write_bytes(run.stdout + run.stderr)
            print(run.stdout.decode(errors="replace"), end="", flush=True)
            print(run.stderr.decode(errors="replace"), end="", file=sys.stderr)
            output, _ = server.communicate(b"\n", timeout=30)
            (directory / "echo.log").write_bytes(output)
            success = run.returncode == 0 and server.returncode == 0
            return 0 if success else 1
    finally:
        if server is not None and server.poll() is None:
            server.kill()
            server.wait()
        residue.finish(success)


if __name__ == "__main__":
    sys.exit(main())
