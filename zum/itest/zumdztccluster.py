#!/usr/bin/env python3
# (c) Copyright 2026 Huw Rogers
# This code is licensed by the MIT license (see LICENSE for details)

"""Publisher lifetime across passive and active Zdb states."""

import base64
import os
import secrets
import selectors
import shutil
import signal
import socket
import subprocess
import time

from zumdztctest import HERE, PROBE, SERVER, probe, wait_line
from zumhttp import Authenticator, Fixture
from zi_test_residue import Residue


def events(processes, required, timeout=30):
    pending = [b"" for _ in processes]
    seen = [set() for _ in processes]
    deadline = time.monotonic() + timeout
    with selectors.DefaultSelector() as selector:
        for index, process in enumerate(processes):
            selector.register(process.stdout, selectors.EVENT_READ, index)
        while not required(seen):
            remaining = deadline - time.monotonic()
            selected = selector.select(max(0, remaining))
            if remaining <= 0 or not selected:
                raise AssertionError("cluster startup events timed out")
            for key, _ in selected:
                index = key.data
                data = os.read(key.fileobj.fileno(), 4096)
                if not data:
                    raise AssertionError("cluster node exited during startup")
                pending[index] += data
                lines = pending[index].split(b"\n")
                pending[index] = lines.pop()
                seen[index].update(lines)
    return seen


def wait_probe(name, env, group, process, timeout=10):
    deadline = time.monotonic() + timeout
    while True:
        try:
            probe(name, env, group)
            return
        except AssertionError:
            if process.poll() is not None or time.monotonic() >= deadline:
                raise
            time.sleep(0.05)


def main():
    residue = Residue("zumdztccluster")
    directory = residue.directory
    registry = "zumd-ztc-" + secrets.token_hex(8)
    ring = registry + "-ring"
    ids = [registry + "-0", registry + "-1", registry + "-2"]
    regdir = residue.tmp_dir("registry")
    registry = regdir.name
    for name in (ring, *ids):
        residue.shm(name)
    reservations = [socket.socket() for _ in range(6)]
    for sock in reservations:
        sock.bind(("127.0.0.1", 0))
    ports = [sock.getsockname()[1] for sock in reservations]
    for sock in reservations:
        sock.close()
    key = base64.b64encode(secrets.token_bytes(32)).decode()
    issuer = "http://localhost:" + str(ports[0])
    base = (HERE / "zumd.cf").read_text()
    standalone = directory / "standalone.cf"
    standalone.write_text(base)
    configurations = []
    environments = []
    for index in range(3):
        hosts = ("hostID: " + str(index) + ", hosts: {"
                 "0: {priority: 100, ip: 127.0.0.1, port: " + str(ports[3]) + "},"
                 "1: {priority: 80, ip: 127.0.0.1, port: " + str(ports[4]) + "},"
                 "2: {priority: 120, ip: 127.0.0.1, port: " + str(ports[5]) + "}}")
        config = directory / ("node" + str(index) + ".cf")
        config.write_text(base.replace(
            "hostID: self, hosts: {self: {standalone: true}}", hosts).rstrip() +
            ',\nztcPublish: true,\nztc: {id: "' + ids[index] +
            '", alertPrefix: "' + str(directory / ("alerts" + str(index))) +
            '"}\n')
        configurations.append(config)
        environments.append(dict(os.environ,
            ZDB_MODULE=os.environ["ZDB_MODULE"],
            ZDB_CONNECT=str(directory / ("node" + str(index) + ".db")),
            ZUM_DB_KEY=key, ZUMD_HOME=str(directory / ("home" + str(index))),
            ZTC_RING=ring, ZTC_DIR=registry))
    setup = None
    processes = [None, None, None]
    logs = []
    passed = False
    leader = 0

    def start(index, config=None):
        log = (directory / ("node" + str(index) + ".log")).open("ab")
        logs.append(log)
        processes[index] = subprocess.Popen([
            str(SERVER), "--config=" + str(config or configurations[index]),
            "--vault-store=file", "--vault-test-store",
            "--issuer=" + issuer, "--admin=cluster-admin",
            "--rp-id=localhost", "--port=" + str(ports[index]),
            "--bootstrap-output=" + str(directory / ("enrollment" + str(index)))],
            env=environments[index], stdout=subprocess.PIPE, stderr=log)

    def stop(index):
        process = processes[index]
        if process.poll() is None:
            process.send_signal(signal.SIGTERM)
        process.communicate(timeout=30)
        assert process.returncode == 0, "cluster node did not drain"

    try:
        setup = subprocess.Popen([str(PROBE), ids[0]],
                                 env=environments[0], stdin=subprocess.PIPE,
                                 stdout=subprocess.PIPE,
                                 stderr=subprocess.PIPE)
        wait_line(setup, b"ready")
        start(0)
        start(1)
        seen = events(processes[:2], lambda lines:
            all(b"zumd: listening" in item for item in lines) and
            any(b"zumd: active" in item for item in lines))
        leaders = [index for index, lines in enumerate(seen)
                   if b"zumd: active" in lines]
        assert len(leaders) == 1, "cluster must elect one active node"
        leader = leaders[0]
        follower = 1 - leader
        assert leader == 0, "higher-priority node was not elected"
        registration = regdir / (ids[follower] + ".pid")
        assert registration.exists(), "passive publisher not registered"
        original_pid = registration.read_bytes()
        probe(ids[follower], environments[follower], "app")
        probe(ids[follower], environments[follower], "mx")
        probe(ids[follower], environments[follower], "db-passive")

        fixture = Fixture(directory)
        fixture.port = ports[leader]
        fixture.origin = issuer
        fixture.authenticator = Authenticator(issuer)
        enrollment = directory / "enrollment"
        shutil.copyfile(directory / ("enrollment" + str(leader)), enrollment)
        enrollment.chmod(0o600)
        fixture.enroll()

        stop(leader)
        wait_line(processes[follower], b"zumd: active")
        probe(ids[follower], environments[follower], "db-active")
        assert registration.read_bytes() == original_pid
        probe(ids[follower], environments[follower], "app")

        backup = subprocess.run(["sqlite3", "-batch",
            environments[follower]["ZDB_CONNECT"],
            ".backup '" + environments[2]["ZDB_CONNECT"] + "'"],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30)
        assert backup.returncode == 0, "active-node SQLite snapshot failed"
        start(2, standalone)
        wait_line(processes[2], b"zumd: active")
        fixture.port = ports[2]
        token = fixture.login(offline=False)
        fixture.request("POST", "/admin/providers", {
            "name": "ahead-peer", "issuer": "https://ahead.example",
            "client_id": "ahead-client", "client_secret": secrets.token_urlsafe(32),
            "scopes": ["openid"], "role_claim": "roles",
            "claim_source": "IDToken"}, token=token,
            headers={"Idempotency-Key": secrets.token_hex(16)}, status=201)
        stop(2)
        start(2)
        wait_line(processes[2], b"zumd: active")
        wait_probe(ids[follower], environments[follower],
                   "db-passive", processes[follower])
        probe(ids[follower], environments[follower], "app")
        assert registration.read_bytes() == original_pid

        stop(2)
        wait_line(processes[follower], b"zumd: active")
        probe(ids[follower], environments[follower], "db-active")
        assert registration.read_bytes() == original_pid
        stop(follower)
        assert not registration.exists(), "publisher registration retained"
        passed = True
        print("1..1\nok 1 - publisher survives passive and active Zdb cycles")
    finally:
        for process in processes:
            if process is not None and process.poll() is None:
                process.send_signal(signal.SIGTERM)
        for process in processes:
            if process is not None:
                try:
                    process.communicate(timeout=30)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.communicate()
        for log in logs:
            log.close()
        if setup is not None:
            setup.stdin.write(b"q\n")
            setup.stdin.flush()
            setup.communicate(timeout=5)
        residue.finish(passed)


if __name__ == "__main__":
    main()
