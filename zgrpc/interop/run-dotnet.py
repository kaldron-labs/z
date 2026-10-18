#!/usr/bin/env python3
# (c) Copyright 2026 Huw Rogers
# This code is licensed by the MIT license (see LICENSE for details)

"""Qualify native H3 client and server roles against canonical grpc-dotnet."""

import argparse
import os
from pathlib import Path
import selectors
import socket
import subprocess
import tempfile


def ready(process):
    with selectors.DefaultSelector() as selector:
        selector.register(process.stdout, selectors.EVENT_READ)
        if not selector.select(15):
            raise RuntimeError("peer did not become ready")
        line = process.stdout.readline().strip()
    if not line.startswith("READY "):
        raise RuntimeError("peer startup failed: " + line)
    return line.rsplit(":", 1)[1]


def report(result, number, name, extra=0):
    print("# Subtest: " + name)
    for line in result.stdout.splitlines():
        print("    " + line)
    if extra:
        print(f"    # native server exit {extra}")
    print(("not ok" if result.returncode or extra else "ok") + f" {number} - {name}", flush=True)
    return result.returncode or extra


def native(command, valgrind):
    if not valgrind:
        return [str(command)]
    return [str(command.parents[2] / "libtool"), "exec", "valgrind", "--vgdb=no",
            "--error-exitcode=99", "--leak-check=full", "--show-leak-kinds=definite,indirect,possible",
            "--errors-for-leak-kinds=definite,indirect,possible", str(command)]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--peer", required=True)
    parser.add_argument("--client")
    parser.add_argument("--server")
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--valgrind", action="store_true")
    args = parser.parse_args()
    if not args.self_test and (not args.client or not args.server):
        parser.error("native qualification requires --client and --server")
    peer = Path(args.peer).resolve()
    print("TAP version 13\n1.." + ("1" if args.self_test else "2"), flush=True)
    with tempfile.TemporaryDirectory(prefix="zgrpc-dotnet-") as directory:
        root = Path(directory)
        cert, key = root / "cert.pem", root / "key.pem"
        subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes",
                        "-days", "1", "-subj", "/CN=localhost",
                        "-addext", "subjectAltName=DNS:localhost,IP:127.0.0.1",
                        "-keyout", str(key), "-out", str(cert)], check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if args.self_test:
            result = subprocess.run([str(peer), "--mode=self-test", "--cert=" + str(cert),
                                     "--key=" + str(key)], timeout=180,
                                    stdout=subprocess.PIPE, text=True)
            return report(result, 1, "grpc-dotnet H3 peer self-test")
        client, server = Path(args.client).resolve(), Path(args.server).resolve()
        env = dict(os.environ, ZGRPC_GO_H3="1", ZGRPC_GO_CA=str(cert),
                   ZGRPC_GO_CERT=str(cert), ZGRPC_GO_KEY=str(key), ZI_LOGDIR=str(root / "logs"))
        if args.valgrind:
            env["OPENSSL_ia32cap"] = "0"
            env["ZGRPC_PEER_TIMEOUT"] = "60"
        dotnet = subprocess.Popen([str(peer), "--mode=server", "--cert=" + str(cert),
                                   "--key=" + str(key)], stdout=subprocess.PIPE, text=True)
        try:
            env["ZGRPC_GO_PORT"] = ready(dotnet)
            result = subprocess.run(native(client, args.valgrind), env=env,
                                    timeout=180, stdout=subprocess.PIPE, text=True)
            failed = report(result, 1, "native H3 client against grpc-dotnet server")
        finally:
            dotnet.terminate()
            try:
                dotnet.wait(timeout=15)
            except subprocess.TimeoutExpired:
                dotnet.kill()
                dotnet.wait()
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as reservation:
            reservation.bind(("127.0.0.1", 0))
            env["ZGRPC_GO_PORT"] = str(reservation.getsockname()[1])
        srv = subprocess.Popen(native(server, args.valgrind), env=env,
                               stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
        try:
            port = ready(srv)
            result = subprocess.run([str(peer), "--mode=client", "--target=https://localhost:" + port,
                                     "--ca=" + str(cert)], timeout=180,
                                    stdout=subprocess.PIPE, text=True)
        finally:
            try:
                srv.communicate("\n", timeout=20)
            except (BrokenPipeError, subprocess.TimeoutExpired):
                srv.kill()
                srv.wait()
        failed |= report(result, 2, "grpc-dotnet H3 client against native server", srv.returncode)
        return failed


if __name__ == "__main__":
    raise SystemExit(main())
