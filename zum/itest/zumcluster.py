"""SQLite two-node Zum activation/admission fixture, not a failover SLA test."""

import base64
import http.client
import os
import re
from pathlib import Path
import secrets
import selectors
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import time


def exercise(directory):
    module = os.environ["ZUM_TEST_MODULE"]
    connections = [os.environ["ZUM_CLUSTER_CONNECT_A"], os.environ["ZUM_CLUSTER_CONNECT_B"]]
    assert connections[0] != connections[1], "cluster nodes need separate disposable stores"
    reservations = [socket.socket() for _ in range(4)]
    for listener in reservations:
        listener.bind(("127.0.0.1", 0))
    ports = [listener.getsockname()[1] for listener in reservations]
    key = base64.b64encode(secrets.token_bytes(32)).decode()
    issuer = "http://localhost:" + str(ports[0])
    server = Path(__file__).resolve().parent.parent / "src" / "zumd"
    # Source-tree fixture: enumerate the authoritative route declarations rather
    # than maintain a second endpoint list. No authenticated catalog is available
    # before initial-admin enrollment in this activation scenario.
    catalog = server.with_name("ZumMgmt.cc").read_text()
    routes = re.findall(r'ZUM_ROUTE\(\s*(\w+),\s*(\w+),\s*"([^"]+)"\s*\)', catalog)
    assert len(routes) == 68 and len({name for name, _, _ in routes}) == len(routes)
    base = Path(__file__).with_name("zumd.cf").read_text()
    processes, logs = [], []
    for listener in reservations:
        listener.close()
    try:
        for index, connection in enumerate(connections):
            hosts = ("hostID: " + str(index) + ", hosts: {"
                     "0: {priority: 100, ip: 127.0.0.1, port: " + str(ports[2]) + "},"
                     "1: {priority: 80, ip: 127.0.0.1, port: " + str(ports[3]) + "}}")
            config = directory / ("node" + str(index) + ".cf")
            config.write_text(base.replace("hostID: self, hosts: {self: {standalone: true}}", hosts))
            log = (directory / ("node" + str(index) + ".log")).open("ab")
            logs.append(log)
            processes.append(subprocess.Popen([str(server), "--config=" + str(config),
                "--issuer=" + issuer, "--admin=cluster-admin", "--rp-id=localhost",
                "--port=" + str(ports[index]),
                "--bootstrap-output=" + str(directory / ("enrollment" + str(index)))],
                env=dict(os.environ, ZDB_MODULE=module, ZDB_CONNECT=connection, ZUM_DB_KEY=key),
                stdout=subprocess.PIPE, stderr=log))
        # Consume startup events, not a timed guess about election completion.
        pending = [b"", b""]
        listening = set()
        active = set()
        deadline = time.monotonic() + 30
        with selectors.DefaultSelector() as selector:
            for index, process in enumerate(processes):
                selector.register(process.stdout, selectors.EVENT_READ, index)
            while len(listening) != len(processes) or not active:
                remaining = deadline - time.monotonic()
                events = selector.select(max(0, remaining))
                assert remaining > 0 and events, "both active and standby must expose HTTP health"
                for event, _ in events:
                    index = event.data
                    data = os.read(event.fileobj.fileno(), 4096)
                    assert data, "cluster node exited during startup"
                    pending[index] += data
                    lines = pending[index].split(b"\n")
                    pending[index] = lines.pop()
                    if b"zumd: listening" in lines:
                        listening.add(index)
                    if b"zumd: active" in lines:
                        active.add(index)
        assert len(active) == 1, "only the activated node may finish preparation"
        for index, port in enumerate(ports[:2]):
            connection = http.client.HTTPConnection("127.0.0.1", port, timeout=10)
            connection.request("GET", "/health/live")
            response = connection.getresponse()
            assert response.status == 200
            response.read()
            connection.request("GET", "/health/ready")
            response = connection.getresponse()
            # No administrator has enrolled, and a standby is never ready.
            assert response.status == 503
            response.read()
            if index not in active:
                for name, method, path in routes:
                    path = path.replace("{actorKind}", "user").replace("{valueKey}", "eA")
                    path = re.sub(r"\{[^}]+\}", "1", path)
                    body = None if method in ("GET", "DELETE") else "{}"
                    connection.request(method, path, body=body,
                                       headers={"Content-Type": "application/json"})
                    response = connection.getresponse()
                    assert response.status == 503, "standby admitted " + name
                    response.read()
                print("# standby administrative HTTP503 coverage: " + str(len(routes)), flush=True)
            connection.close()

        leader = next(iter(active))
        follower = 1 - leader

        def bootstrap_ids(index):
            result = subprocess.run(["sqlite3", "-batch", "-noheader",
                connections[index],
                'SELECT id, schema_version, core_app_i_d, initial_user_i_d, initial_client_i_d '
                'FROM "a_zum.issuer" ORDER BY id'],
                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            assert result.returncode == 0, "cluster bootstrap snapshot query failed"
            assert result.stdout.strip(), "cluster bootstrap issuer is missing"
            return result.stdout

        original_ids = bootstrap_ids(leader)
        processes[leader].send_signal(signal.SIGTERM)
        processes[leader].communicate(timeout=30)
        assert processes[leader].returncode == 0, "active node did not stop cleanly"
        # Observe Zdb's activation; do not direct election or infer a durability
        # guarantee from a timed pause. The surviving listener stays available.
        deadline = time.monotonic() + 30
        with selectors.DefaultSelector() as selector:
            selector.register(processes[follower].stdout, selectors.EVENT_READ)
            while True:
                remaining = deadline - time.monotonic()
                events = selector.select(max(0, remaining))
                assert remaining > 0 and events, "survivor did not prepare after Zdb activation"
                data = os.read(processes[follower].stdout.fileno(), 4096)
                assert data, "survivor exited before active preparation"
                pending[follower] += data
                lines = pending[follower].split(b"\n")
                pending[follower] = lines.pop()
                if b"zumd: active" in lines:
                    break
        assert bootstrap_ids(follower) == original_ids, "activation must preserve bootstrap identities"
        connection = http.client.HTTPConnection("127.0.0.1", ports[follower], timeout=10)
        connection.request("GET", "/health/live")
        response = connection.getresponse()
        assert response.status == 200
        response.read()
        connection.request("GET", "/health/ready")
        response = connection.getresponse()
        assert response.status == 503, "activation must not bypass initial administrator enrollment"
        response.read()
        connection.close()
    finally:
        original_failure = sys.exc_info()[0] is not None
        for process in processes:
            if process.poll() is None:
                process.send_signal(signal.SIGTERM)
        failed = False
        for process in processes:
            try:
                process.communicate(timeout=30)
            except subprocess.TimeoutExpired:
                process.kill()
                process.communicate()
                failed = True
            if process.returncode:
                failed = True
        for log in logs:
            log.close()
        if failed:
            if original_failure:
                print("# cluster shutdown also failed to drain", flush=True)
            else:
                raise AssertionError("cluster shutdown did not drain cleanly")


def main():
    directory = Path(tempfile.mkdtemp(prefix="zum-cluster-"))
    try:
        exercise(directory)
    except BaseException:
        print("# cluster diagnostics retained in " + str(directory), flush=True)
        raise
    else:
        shutil.rmtree(directory)


if __name__ == "__main__":
    main()
