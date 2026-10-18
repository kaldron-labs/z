#!/usr/bin/env python3
# (c) Copyright 2026 Huw Rogers
# This code is licensed by the MIT license (see LICENSE for details)

"""Run both native endpoint roles against the independent Go H2/H3 peers."""

import argparse
import os
from pathlib import Path
import selectors
import socket
import subprocess
import tempfile


def report(result, number, name, extra=0):
    print("# Subtest: " + name)
    for line in result.stdout.splitlines():
        print("    " + line)
    if extra:
        print(f"    # native server exit {extra}")
    print(("not ok" if result.returncode or extra else "ok") + f" {number} - " + name,
          flush=True)
    return result.returncode or extra


def check(command, env, number, name):
    result = subprocess.run(command, env=env, timeout=120,
                            stdout=subprocess.PIPE, text=True)
    return report(result, number, name)


def main():
    args = argparse.ArgumentParser()
    args.add_argument("--peer", required=True)
    args.add_argument("--client", required=True)
    args.add_argument("--server")
    args.add_argument("--valgrind", action="store_true")
    args.add_argument("--h3", action="store_true")
    opts = args.parse_args()
    print("TAP version 13\n1.." + ("2" if opts.server else "1"), flush=True)
    with tempfile.TemporaryDirectory(prefix="zgrpc-go-") as directory:
        root = Path(directory)
        cert, key = root / "cert.pem", root / "key.pem"
        subprocess.run([
            "openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes",
            "-days", "1", "-subj", "/CN=localhost",
            "-addext", "subjectAltName=DNS:localhost,IP:127.0.0.1",
            "-keyout", str(key), "-out", str(cert),
        ], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        peer = subprocess.Popen([
            str(Path(opts.peer).resolve()), "--mode=server",
            "--cert=" + str(cert), "--key=" + str(key),
        ], stdout=subprocess.PIPE, text=True)
        try:
            with selectors.DefaultSelector() as ready:
                ready.register(peer.stdout, selectors.EVENT_READ)
                if not ready.select(15):
                    raise RuntimeError("Go server did not become ready")
                line = peer.stdout.readline().strip()
            if not line.startswith("READY "):
                raise RuntimeError("Go server startup failed: " + line)
            env = dict(os.environ, ZGRPC_GO_PORT=line.rsplit(":", 1)[1],
                       ZGRPC_GO_CA=str(cert), ZI_LOGDIR=str(root / "logs"))
            if opts.h3:
                env["ZGRPC_GO_H3"] = "1"
            client = Path(opts.client).resolve()
            command = [str(client)]
            if opts.valgrind:
                env["OPENSSL_ia32cap"] = "0"
                command = [str(client.parents[2] / "libtool"), "exec", "valgrind",
                           "--vgdb=no", "--error-exitcode=99", "--leak-check=full",
                           "--show-leak-kinds=definite,indirect,possible",
                           "--errors-for-leak-kinds=definite,indirect,possible"] + command
            result = check(command, env, 1, "native client against Go server")
        finally:
            peer.terminate()
            try:
                peer.wait(timeout=10)
            except subprocess.TimeoutExpired:
                peer.kill()
                peer.wait()
        if result or not opts.server:
            return result
        with socket.socket(type=socket.SOCK_DGRAM if opts.h3 else socket.SOCK_STREAM) as listener:
            listener.bind(("127.0.0.1", 0))
            port = listener.getsockname()[1]
        env = dict(os.environ, ZGRPC_GO_PORT=str(port), ZGRPC_GO_CERT=str(cert),
                   ZGRPC_GO_KEY=str(key), ZI_LOGDIR=str(root / "logs"))
        if opts.h3:
            env["ZGRPC_GO_H3"] = "1"
        server = Path(opts.server).resolve()
        command = [str(server)]
        if opts.valgrind:
            env["OPENSSL_ia32cap"] = "0"
            command = [str(server.parents[2] / "libtool"), "exec", "valgrind",
                       "--vgdb=no", "--error-exitcode=99", "--leak-check=full",
                       "--show-leak-kinds=definite,indirect,possible",
                       "--errors-for-leak-kinds=definite,indirect,possible"] + command
        native = subprocess.Popen(command, env=env, stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE, text=True)
        try:
            with selectors.DefaultSelector() as ready:
                ready.register(native.stdout, selectors.EVENT_READ)
                if not ready.select(30):
                    raise RuntimeError("native server did not become ready")
                line = native.stdout.readline().strip()
            if not line.startswith("READY "):
                raise RuntimeError("native server startup failed: " + line)
            result = subprocess.run([
                str(Path(opts.peer).resolve()), "--mode=client", "--native",
                "--target=" + line.removeprefix("READY "), "--ca=" + str(cert),
            ], env=env, timeout=120, stdout=subprocess.PIPE, text=True)
        finally:
            if native.poll() is None:
                try:
                    native.communicate("\n", timeout=30)
                except subprocess.TimeoutExpired:
                    native.kill()
                    native.communicate()
        return report(result, 2, "Go client against native server", native.returncode)


if __name__ == "__main__":
    raise SystemExit(main())
