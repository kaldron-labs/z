"""SQLite two-node Zum admission and active-service failover fixture."""

import base64
import http.client
from http.cookies import SimpleCookie
import os
from pathlib import Path
import secrets
import selectors
import shutil
import signal
import socket
import subprocess
import sys
import time

from zumhttp import Authenticator, Fixture
from zi_test_residue import Residue


def wait_replication(process, port):
    deadline = time.monotonic() + 30
    while True:
        assert process.poll() is None, "standby exited before replication startup"
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=1):
                return
        except ConnectionRefusedError:
            assert time.monotonic() < deadline, "replication listener did not start"
            time.sleep(0.05)


def require_closed(port):
    try:
        with socket.create_connection(("127.0.0.1", port), timeout=1):
            raise AssertionError("standby HTTP listener accepted a connection")
    except ConnectionRefusedError:
        pass


def exercise(directory):
    module = os.environ["ZDB_MODULE"]
    connections = [os.environ["ZDB_CONNECT"], os.environ["ZUM_CLUSTER_PEER_CONNECT"]]
    assert connections[0] != connections[1], "cluster nodes need separate disposable stores"
    reservations = [socket.socket() for _ in range(4)]
    for listener in reservations:
        listener.bind(("127.0.0.1", 0))
    ports = [listener.getsockname()[1] for listener in reservations]
    key = base64.b64encode(secrets.token_bytes(32)).decode()
    issuer = "http://localhost:" + str(ports[0])
    server = Path(__file__).resolve().parent.parent / "src" / "zumd"
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
                "--vault-store=file", "--vault-test-store",
                "--issuer=" + issuer, "--admin=cluster-admin", "--rp-id=localhost",
                "--port=" + str(ports[index]),
                "--bootstrap-output=" + str(directory / ("enrollment" + str(index)))],
                env=dict(os.environ, ZDB_MODULE=module, ZDB_CONNECT=connection,
                         ZUM_DB_KEY=key,
                         ZUMD_HOME=str(directory / ("vault-node" + str(index)))),
                stdout=subprocess.PIPE, stderr=log))
        # Consume startup events, not a timed guess about election completion.
        pending = [b"", b""]
        listening = set()
        active = set()
        deadline = time.monotonic() + 30
        with selectors.DefaultSelector() as selector:
            for index, process in enumerate(processes):
                selector.register(process.stdout, selectors.EVENT_READ, index)
            while not active or not active <= listening:
                remaining = deadline - time.monotonic()
                events = selector.select(max(0, remaining))
                assert remaining > 0 and events, "active node did not start its HTTP listener"
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
        leader = next(iter(active))
        follower = 1 - leader
        assert listening == active, "only the active node may bind HTTP"
        # Multiplexer/DB runtime starts on both processes. A standby's native
        # replication listener, rather than an application HTTP health route,
        # establishes that it has started independently of role activation.
        wait_replication(processes[follower], ports[2 + follower])
        require_closed(ports[follower])
        assert processes[follower].poll() is None
        connection = http.client.HTTPConnection("127.0.0.1", ports[leader], timeout=10)
        connection.request("GET", "/health/live")
        response = connection.getresponse()
        assert response.status == 200
        response.read()
        connection.request("GET", "/health/ready")
        response = connection.getresponse()
        assert response.status == 503  # active, but initial admin is not enrolled
        response.read()
        connection.close()

        # Use the production WebAuthn driver against the active node.  The
        # issuer remains on the leader's stable public address when requests
        # later move to the promoted listener.
        fixture = Fixture(directory)
        fixture.port = ports[leader]
        fixture.origin = issuer
        fixture.authenticator = Authenticator(issuer)
        enrollment = directory / "enrollment"
        shutil.copyfile(directory / ("enrollment" + str(leader)), enrollment)
        enrollment.chmod(0o600)
        fixture.enroll()
        admin = fixture.login(offline=False)
        enrolled, _ = fixture.request(
            "GET", "/admin/users?name=cluster-admin&source=Local", token=admin)
        assert len(enrolled["items"]) == 1
        credentials, _ = fixture.request(
            "GET", "/admin/credentials?limit=1000", token=admin)
        assert len(credentials["items"]) == 1

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
        # guarantee from a timed pause. Activation starts the survivor's listener.
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
        fixture.port = ports[follower]
        fixture.request("GET", "/health/live")
        fixture.request("GET", "/health/ready")
        # A fresh passkey login proves the promoted daemon loaded the
        # replicated user, credential and signing key rather than merely
        # opening its listener.  It also executes an authenticated request
        # with the pre-promotion token.
        fixture.cookies = SimpleCookie()
        promoted = fixture.login(offline=False)
        fixture.verify_jwt(promoted)
        replicated, _ = fixture.request(
            "GET", "/admin/users?name=cluster-admin&source=Local", token=admin)
        assert replicated == enrolled
        replicated, _ = fixture.request(
            "GET", "/admin/credentials?limit=1000", token=admin)
        assert replicated == credentials
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
    residue = Residue("zum-cluster")
    directory = residue.directory
    try:
        exercise(directory)
    except BaseException:
        print("# cluster diagnostics retained in " + str(directory), flush=True)
        raise
    else:
        residue.finish(True)


if __name__ == "__main__":
    main()
