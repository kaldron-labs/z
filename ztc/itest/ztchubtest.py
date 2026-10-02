#!/usr/bin/env python3
# (c) Copyright 2026 Huw Rogers
# This code is licensed by the MIT license (see LICENSE for details)

"""Multi-process ztchub fixture.

The WebSocket peers are the C++ ``ztchubwiretest`` fixture, so all frames are
constructed and verified with the production FlatBuffers schema and Zws
client.  Python is used only to provision the disposable Zum instance and to
drive process and TLS session boundaries.
"""

import base64
import json
import os
from pathlib import Path
import resource
import secrets
from http.cookies import SimpleCookie
import http.client
import http.server
import signal
import socket
import sqlite3
import ssl
import subprocess
import threading
import time
from urllib.parse import urlencode

from zumhttp import Fixture
from zi_test_residue import Residue


def free_port():
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        return listener.getsockname()[1]


def check_load_limits(counts):
    """Reject a scale run that cannot fit the host's descriptor/port limits."""
    required = counts["agents"] + counts["clients"] + 64
    soft, _ = resource.getrlimit(resource.RLIMIT_NOFILE)
    if soft != resource.RLIM_INFINITY and required * 2 >= soft:
        raise AssertionError(
            f"load needs about {required * 2} descriptors, RLIMIT_NOFILE is {soft}")
    try:
        first, last = (int(value) for value in
                       Path("/proc/sys/net/ipv4/ip_local_port_range").read_text().split())
    except (FileNotFoundError, ValueError):
        return
    if required >= last - first + 1:
        raise AssertionError(
            f"load needs about {required} ephemeral ports, range is {first}-{last}")


def wait_line(process, prefix, timeout=None):
    import selectors

    if timeout is None:
        timeout = 120 if os.environ.get("ZTC_VALGRIND") else 20
    pending = getattr(process, "_wire_pending", b"")
    seen = []
    deadline = time.monotonic() + timeout
    with selectors.DefaultSelector() as selector:
        selector.register(process.stdout, selectors.EVENT_READ)
        while True:
            while b"\n" in pending:
                line, pending = pending.split(b"\n", 1)
                seen.append(line.decode(errors="replace"))
                seen = seen[-8:]
                if line.startswith(prefix.encode()):
                    process._wire_pending = pending
                    return line.decode(errors="replace")
            remaining = deadline - time.monotonic()
            if remaining <= 0 or not selector.select(remaining):
                raise AssertionError("wire fixture output timed out: " + prefix +
                                     "; stdout: " + repr(seen))
            data = os.read(process.stdout.fileno(), 4096)
            if not data:
                diagnostic = getattr(process, "_diagnostic", b"")
                if process.stderr is not None:
                    diagnostic += process.stderr.read()
                diagnostic = diagnostic.decode(errors="replace")
                raise AssertionError(
                    "wire fixture exited before output: " + prefix +
                    "; stdout: " + repr(seen) +
                    ("; stderr: " + diagnostic if diagnostic else ""))
            pending += data


def wait_agents(process, devices):
    pending = set(devices)
    deadline = time.monotonic() + 30
    while pending:
        line = wait_line(process, "agent accepted ",
                         timeout=max(0.1, deadline - time.monotonic()))
        pending.discard(line.removeprefix("agent accepted "))


def stop_process(process, timeout=20):
    if process is None:
        return
    if process.poll() is None:
        if getattr(process, "_process_group", False):
            os.killpg(process.pid, signal.SIGTERM)
        else:
            process.send_signal(signal.SIGTERM)
    try:
        _, diagnostic = process.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        if getattr(process, "_process_group", False):
            os.killpg(process.pid, signal.SIGKILL)
        else:
            process.kill()
        _, diagnostic = process.communicate()
    process._diagnostic = diagnostic


def run_wire(root, mode, issuer, device, wss, ca, token=None, cookie=None,
             origin=None, telemetry=1, expect=1, stall_ms=0, payload=0,
             control_burst=1, inventory=False, one_shot=False,
             wait_shutdown=False, group="App", snapshots=1, changes=False,
             expect_publishers=0, source_errors=False):
    executable = root / "ztc" / "itest" / "ztchubwiretest"
    environment = dict(os.environ)
    if token is None:
        environment.pop("ZTC_ACCESS_TOKEN", None)
    else:
        environment["ZTC_ACCESS_TOKEN"] = token
    args = [str(executable), "--mode=" + mode, "--issuer=" + issuer,
            "--device-id=" + device, "--wss=" + wss, "--ca=" + str(ca),
            "--telemetry=" + str(telemetry), "--expect=" + str(expect),
            "--group=" + group, "--snapshots=" + str(snapshots)]
    if stall_ms:
        args.append("--stall-ms=" + str(stall_ms))
    if payload:
        args.append("--payload=" + str(payload))
    if control_burst != 1:
        args.append("--control-burst=" + str(control_burst))
    if inventory:
        args.append("--inventory")
    if one_shot:
        args.append("--one-shot")
    if wait_shutdown:
        args.append("--wait-shutdown")
    if changes:
        args.append("--changes")
    if expect_publishers:
        args.append("--expect-publishers=" + str(expect_publishers))
    if source_errors:
        args.append("--source-errors")
    if cookie:
        args.append("--cookie=" + cookie)
    if origin:
        args.append("--origin=" + origin)
    return subprocess.Popen(args, env=environment, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE)


def session_cookie(port, token, origin):
    context = ssl._create_unverified_context()
    context.set_alpn_protocols(["http/1.1"])
    connection = http.client.HTTPSConnection(
        "127.0.0.1", port, context=context, timeout=15)
    try:
        connection.request("POST", "/session", headers={
            "Authorization": "Bearer " + token,
            "Origin": origin})
        response = connection.getresponse()
        response.read(0)
        assert response.status == 201, response.status
        header = response.getheader("Set-Cookie")
        assert header
        cookies = SimpleCookie()
        cookies.load(header)
        value = cookies["__Host-ztc_session"].value
        assert value and "=" not in value
        return "__Host-ztc_session=" + value
    finally:
        connection.close()


def bad_subprotocol(port, token):
    context = ssl._create_unverified_context()
    context.set_alpn_protocols(["http/1.1"])
    sock = socket.create_connection(("127.0.0.1", port), 5)
    sock = context.wrap_socket(sock, server_hostname="127.0.0.1")
    key = base64.b64encode(secrets.token_bytes(16)).decode()
    request = (
        "GET /ztc HTTP/1.1\r\nHost: 127.0.0.1:%d\r\n"
        "Upgrade: websocket\r\nConnection: Upgrade\r\n"
        "Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n"
        "Sec-WebSocket-Protocol: invalid.v1\r\n"
        "Authorization: Bearer %s\r\n\r\n" % (port, key, token))
    try:
        sock.sendall(request.encode())
        response = sock.recv(256)
        assert response and not response.startswith(b"HTTP/1.1 101")
    finally:
        sock.close()


def app_client(fixture, admin, app_id, prefix, role_id, label, grants, profile, redirect_port=49152):
    item = fixture.admin_secret("clientAdd", {
        "app_id": app_id, "label": label, "profile": profile,
        "redirect_uris": ([f"http://127.0.0.1:{redirect_port}/callback"]
                          if profile == "native" else []),
        "grants": ",".join(name for bit, name in (
            (1, "AuthCode"), (2, "ClientCredentials"), (4, "Refresh")) if grants & bit), "refresh_allowed": grants >= 5}, idempotence=secrets.token_hex(16))["item"]
    fixture.request("PUT", prefix + "/client-access/" + item["id"], {
        "role_ids": [role_id]},
        token=admin, headers={"If-None-Match": "*"}, status=201)
    return item


def service_token(fixture, app_id, client):
    basic = base64.b64encode((client["id"] + ":" +
                              client["client_secret"]).encode()).decode()
    token = fixture.request("POST", fixture.oauth(app_id, "token"), {
        "grant_type": "client_credentials", "scope": "Agent"}, form=True,
        headers={"Authorization": "Basic " + basic})[0]
    claims = fixture.verify_jwt(token["access_token"])
    assert claims["iss"] == fixture.issuer(app_id)
    assert claims["actions"] == ["Telemetry"]
    assert claims["sub"] == client["id"]
    return token["access_token"]


def hub_command(root, config, directory):
    command = [str(root / "ztc" / "src" / "ztchub"), "--config", str(config)]
    if os.environ.get("ZTC_VALGRIND"):
        command = [str(root / "libtool"), "exec", "valgrind",
                   "--leak-check=full", "--errors-for-leak-kinds=definite",
                   "--error-exitcode=97",
                   "--log-file=" + str(directory / "valgrind-%p.log")] + command
    return command


def run_load(root, fixture, directory, hub, app_id, prefix, role_id,
             front_client, devices, wss, cert, processes, counts):
    admin = fixture.login(offline=False)
    renewed = time.monotonic()
    for index in range(len(devices), counts["agents"]):
        if time.monotonic() - renewed > 120:
            admin = fixture.login(offline=False)
            renewed = time.monotonic()
        client = fixture.request("POST", "/admin/clients", {
            "app_id": app_id, "label": "load device " + str(index),
            "profile": "server", "redirect_uris": [], "grants": "ClientCredentials"},
            token=admin, headers={"Idempotency-Key": secrets.token_hex(16)},
            status=201)[0]["item"]
        fixture.request("PUT", prefix + "/client-access/" + client["id"],
                        {"role_ids": [role_id]}, token=admin,
                        headers={"If-None-Match": "*"}, status=201)
        devices.append(client)
    values = []
    tokens = []
    for device in devices:
        token = service_token(fixture, app_id, device)
        values.append("{id: " + json.dumps(device["id"]) + "}")
        tokens.append(token)
    token = fixture.login(front_client["id"], "Client", app_id, offline=False)
    config = directory / "load.cf"
    token_file = directory / "load.tokens"
    token_file.write_text("\n".join(tokens) + "\n")
    token_file.chmod(0o600)
    config.write_text("devices: [" + ",".join(values) + "], tokenFile: " +
                      json.dumps(str(token_file)) + ", token: " +
                      json.dumps(token) + ", wss: " + json.dumps(wss) +
                      ", ca: " + json.dumps(str(cert)) + ", " +
                      ", ".join(key + ": " + str(counts[key]) for key in
                                ("clients", "subs", "publishers", "rounds")) + "\n")
    config.chmod(0o600)
    peer = subprocess.Popen([str(root / "ztc/itest/ztchubloadtest"), str(config)],
                            stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE)
    processes.append(peer)
    # Drain the hub concurrently with handshakes; its readiness output is bounded.
    pending = {device["id"] for device in devices}
    while pending:
        if peer.poll() is not None:
            output, diagnostic = peer.communicate()
            (directory / "load.log").write_bytes(output +
                                                  getattr(peer, "_wire_pending", b"") +
                                                  diagnostic)
            raise AssertionError("load peer exited before all agents were accepted")
        line = wait_line(hub, "agent accepted ", timeout=300)
        device = line.removeprefix("agent accepted ").strip()
        assert device in pending, "duplicate or unexpected admitted load agent"
        pending.remove(device)
    wait_line(peer, "load agents connected", timeout=300)
    peer.stdin.write(b"\n")
    peer.stdin.flush()
    try:
        output, diagnostic = peer.communicate(timeout=300)
    except subprocess.TimeoutExpired as error:
        output = error.stdout or b""
        diagnostic = error.stderr or b""
        peer.kill()
        tailout, tailerr = peer.communicate()
        output += tailout
        diagnostic += tailerr
        pending = getattr(peer, "_wire_pending", b"")
        (directory / "load.log").write_bytes(output + pending + diagnostic)
        raise
    pending = getattr(peer, "_wire_pending", b"")
    (directory / "load.log").write_bytes(output + pending + diagnostic +
                                          ("\n# peer returncode " +
                                           str(peer.returncode)).encode())
    for line in output.decode(errors="replace").splitlines():
        if line.startswith("# load "):
            print(line, flush=True)
    assert peer.returncode == 0, "WSS workload/SLO failed; see load.log"


def main():
    for key in ("ZDB_MODULE", "ZDB_CONNECT"):
        if not os.environ.get(key):
            raise AssertionError("set " + key + " for a fresh ztchub fixture")
    root = Path(__file__).resolve().parents[2]
    residue = Residue("ztchubtest")
    directory = residue.directory
    fixture = Fixture(directory / "zum")
    fixture.directory.mkdir()
    hub = None
    hubs = []
    agents = []
    fronts = []
    success = False
    load = bool(os.environ.get("ZTC_LOAD"))
    cluster = bool(os.environ.get("ZTC_CLUSTER"))
    counts = {key: int(os.environ.get("ZTC_LOAD_" + key.upper(), default))
              for key, default in (("agents", 2048), ("clients", 32),
                                   ("subs", 32), ("publishers", 256), ("rounds", 3))}
    if load:
        assert counts["agents"] >= 2 and all(value > 0 for value in counts.values())
        check_load_limits(counts)
    try:
        fixture.start()
        fixture.enroll()
        admin = fixture.login(offline=False)
        fixture.admin_cli()
        front_port = free_port()
        second_port = free_port()
        ssf_port = free_port()
        second_ssf_port = free_port()
        audience_uri = "https://127.0.0.1:" + str(front_port)
        service_audience = fixture.origin + "/admin"
        app = fixture.admin_secret("appEnroll", {
            "name": "ztchub-itest",
            "audience": service_audience}, idempotence=secrets.token_hex(16))["item"]
        app_id = app["app_id"]
        stored_app = fixture.admin_command("appQuery", {"id": str(app_id)})["items"]
        assert len(stored_app) == 1 and stored_app[0]["audience"] == service_audience
        cert = directory / "hub-cert.pem"
        key = directory / "hub-key.pem"
        subprocess.run([
            "openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes",
            "-days", "1", "-subj", "/CN=127.0.0.1",
            "-addext", "subjectAltName=IP:127.0.0.1",
            "-keyout", str(key), "-out", str(cert)],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
        callback_auth = "Bearer " + secrets.token_urlsafe(24)
        node_source = (Path(__file__).resolve().parents[2] /
                       "zum" / "itest" / "zumd.cf").read_text()
        zumd_ring = residue.shm("ztc-zumd-" + secrets.token_hex(6))
        zumd_regdir = residue.tmp_dir("zumd-registry")
        zumd_registry = zumd_regdir.name
        zumd_publisher = residue.shm("zumd-" + secrets.token_hex(6))
        fixture.env.update(ZUMD_ZTC_PUBLISH="1", ZTC_RING=zumd_ring,
                           ZTC_DIR=zumd_registry)
        fixture.node_config = directory / "zumd-ztchub.cf"
        fixture.node_config.write_text(
            node_source.rstrip() + ",\n" +
            "oidc: {caPath: " + json.dumps(str(cert)) + "},\n" +
            "ztc: {id: " + json.dumps(zumd_publisher) +
            ", alertPrefix: " +
            json.dumps(str(directory / "zumd-alerts")) + "}\n")
        fixture.stop()
        fixture.start()
        fixture.request("GET", "/health/ready")
        admin = fixture.login(offline=False)

        config = directory / "ztchub.cf"
        origin1 = "https://127.0.0.1:" + str(front_port)
        origin2 = "https://127.0.0.1:" + str(second_port)
        capacity = (
            f'controlFrames: 256, telemetryFrames: {counts["subs"] * counts["publishers"] * 2}, '
            "controlBytes: 1048576, telemetryBytes: 16777216, queueMem: 34359738368, "
            f'expectedAgents: {counts["agents"]}, publishersPerAgent: {counts["publishers"]}, '
            f'activeFrontEnds: {counts["clients"] + len(fronts)}, subscriptionsPerFrontEnd: {counts["subs"]}, '
            if load else
            "controlFrames: 4, telemetryFrames: 4, controlBytes: 131072, "
            "telemetryBytes: 65536, queueMem: 1073741824, "
            "expectedAgents: 2, publishersPerAgent: 64, activeFrontEnds: 2, "
            "subscriptionsPerFrontEnd: 2, ")
        config_text = (
            "listeners: [" +
            "{bind: \"127.0.0.1\", path: \"/ztc\", cert: " +
            json.dumps(str(cert)) + ", key: " + json.dumps(str(key)) +
            ", browserPath: \"/session\", origins: [" +
            json.dumps(origin1) + "], port: " + str(front_port) +
            ", ssfPort: " + str(ssf_port) + "}," +
            "{bind: \"127.0.0.1\", path: \"/ztc\", cert: " +
            json.dumps(str(cert)) + ", key: " + json.dumps(str(key)) +
            ", browserPath: \"/session\", origins: [" +
            json.dumps(origin2) + "], port: " + str(second_port) +
            ", ssfPort: " + str(second_ssf_port) + "}],\n" +
            "issuer: " + json.dumps(fixture.issuer(app_id)) +
            ", audience: " + json.dumps(service_audience) +
            ", managementIssuer: " + json.dumps(fixture.issuer(fixture.core_app_id)) +
            ", managementURL: " + json.dumps(fixture.origin) +
            ", managementClientID: " + json.dumps(app["client_id"]) +
            ", caPath: \"\", ssfCallbackPath: \"/ssf\", "
            "ssfDeliveryURL: " + json.dumps("https://127.0.0.1:" + str(ssf_port) + "/ssf") + ", "
            "ssfLease: 8, actions: [\"Request\", \"Telemetry\"], "
            "roles: [\"Client\", \"Agent\"], maxFrame: 65536, "
            + capacity + ("idleTimeout: 900, " if load else "") +
            "minRefreshMS: 1000, fanoutSLOMS: 200, "
            "schedulerTurnWork: 64\n")
        config.write_text(config_text)
        env = dict(os.environ, ZUM_CLIENT_SECRET=app["client_secret"],
                   ZUM_SSF_AUTH=callback_auth,
                   ZTCHUB_HOME=str(directory / "ztchub-vault"),
                   DBUS_SESSION_BUS_ADDRESS="unsupported:address")
        for var in ("ZUM_DB_KEY", "ZDB_MODULE", "ZDB_CONNECT"):
            env.pop(var, None)
        for var in ("ZTCHUB_HOSTS", "ZTCHUB_HOSTID"):
            env.pop(var, None)
        if cluster:
            standby_port = free_port()
            standby_ssf_port = free_port()
            db_port = free_port()
            standby_db_port = free_port()
            hosts = json.dumps({
                "hot": {"priority": 100, "ip": "127.0.0.1", "port": db_port},
                "warm": {"priority": 50, "ip": "127.0.0.1", "port": standby_db_port}},
                separators=(",", ":"))
            env.update(ZTCHUB_HOSTS=hosts, ZTCHUB_HOSTID="hot")
        hub = subprocess.Popen(hub_command(root, config, directory),
            env=env, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE)
        hubs.append(hub)
        wait_line(hub, "ztchub ready")
        # Observe the hub's own registration and its timer-driven renewal.
        def ssf_expiry():
            with sqlite3.connect(fixture.env["ZDB_CONNECT"]) as db:
                rows = db.execute('SELECT receiver_i_d, expires FROM "a_zum.ssf_rx"')
                for receiver_id, expiry in rows:
                    if receiver_id.endswith("https://127.0.0.1:" + str(ssf_port) + "/ssf"):
                        return int.from_bytes(expiry, "big") if isinstance(expiry, bytes) else expiry
            return 0

        initial_expiry = ssf_expiry()
        assert initial_expiry, "hub did not register its SSF receiver"
        deadline = time.monotonic() + 15
        while ssf_expiry() <= initial_expiry:
            assert time.monotonic() < deadline, "hub did not renew its SSF receiver"
            time.sleep(0.02)
        if cluster:
            standby_config = directory / "ztchub-standby.cf"
            standby_config.write_text(
                config_text.replace(str(front_port), str(standby_port))
                .replace(str(second_port), str(standby_port + 1))
                .replace(str(ssf_port), str(standby_ssf_port))
                .replace(str(second_ssf_port), str(standby_ssf_port + 1)))
            standby_env = dict(env, ZTCHUB_HOSTS=hosts, ZTCHUB_HOSTID="warm")
            standby = subprocess.Popen(
                hub_command(root, standby_config, directory), env=standby_env,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            hubs.append(standby)
            wait_line(standby, "ztchub ready")

        prefix = "/admin/apps/" + str(app_id)
        roles = fixture.request("GET", prefix + "/roles?limit=1000",
                                token=admin)[0]["items"]
        role = {item["name"]: item for item in roles}
        assert set(role) == {"Client", "Agent"}
        actions = fixture.request("GET", prefix + "/actions?limit=1000",
                                  token=admin)[0]["items"]
        assert {item["name"] for item in actions} == {"Request", "Telemetry"}
        action_name = {str(item["id"]): item["name"] for item in actions}
        assert [action_name[str(item)] for item in role["Client"]["actions"]] == ["Request"]
        assert [action_name[str(item)] for item in role["Agent"]["actions"]] == ["Telemetry"]

        user = fixture.request(
            "GET", "/admin/users?name=http-admin&source=Local", token=admin)[0]["items"][0]
        member = fixture.request(
            "POST", prefix + "/assignments", {"user_id": user["id"]},
            token=admin, headers={"Idempotency-Key": secrets.token_hex(16)},
            status=201)[0]["item"]
        fixture.request("PUT", prefix + "/assignments/" + user["id"] + "/roles",
                        {"role_ids": [role["Client"]["id"]]}, token=admin,
                        headers={"If-Match": member["etag"]})
        front_client = app_client(
            fixture, admin, app_id, prefix, role["Client"]["id"],
            "ztchub native front end", 5, "native")
        agent1 = app_client(
            fixture, admin, app_id, prefix, role["Agent"]["id"],
            "ztchub device one", 2, "server")
        agent2 = app_client(
            fixture, admin, app_id, prefix, role["Agent"]["id"],
            "ztchub device two", 2, "server")
        assert agent1["id"] != agent2["id"]
        token1 = service_token(fixture, app_id, agent1)
        token2 = service_token(fixture, app_id, agent2)
        agent_cf = directory / "agent.cf"
        agent_cf.write_text('loopbackTest: true, maxFrame: 65536, '
                            'vaultStore: "file", vaultTestStore: true\n')
        vault_home = directory / "agent-vault"
        agent_env = dict(os.environ, ZTC_ISSUER=fixture.issuer(app_id),
                         ZTC_CLIENT_ID=agent1["id"], ZTC_DEVICE_ID=agent1["id"],
                         ZTC_CLIENT_SECRET=agent1["client_secret"],
                         ZTC_WSS_URL="wss://127.0.0.1:" + str(front_port) + "/ztc",
                         ZTC_CA_PATH=str(cert),
                         ZTCAGENT_HOME=str(vault_home),
                         ZTC_RING=residue.shm("ztc-early-" + secrets.token_hex(6)),
                         ZTC_DIR=residue.tmp_dir("early-registry").name)
        for key in ("ZTC_ACCESS_TOKEN", "ZTC_CREDENTIAL_STORE"):
            agent_env.pop(key, None)
        collector = subprocess.Popen([
            str(root / "ztc" / "src" / "ztcagent"), "--config=" + str(agent_cf)],
            cwd=directory, env=agent_env, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE)
        agents.append(collector)
        wait_line(hub, "agent accepted " + agent1["id"])
        stop_process(collector)
        assert collector.returncode == 0, (
            "collector shutdown returned " + str(collector.returncode) + ": " +
            (getattr(collector, "_diagnostic", b"") or b"").decode(errors="replace"))
        agents.remove(collector)
        agent_env.pop("ZTC_CLIENT_SECRET")
        collector = subprocess.Popen([
            str(root / "ztc" / "src" / "ztcagent"), "--config=" + str(agent_cf)],
            cwd=directory, env=agent_env, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE)
        agents.append(collector)
        wait_line(hub, "agent accepted " + agent1["id"])
        stop_process(collector)
        assert collector.returncode == 0
        agents.remove(collector)
        vault_file = vault_home / "vault" / "secrets.json"
        stored_secret = vault_file.read_bytes()
        bad_env = dict(agent_env, ZTC_CLIENT_SECRET="invalid-device-secret")
        bad = subprocess.Popen([
            str(root / "ztc" / "src" / "ztcagent"), "--config=" + str(agent_cf)],
            cwd=directory, env=bad_env, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE)
        agents.append(bad)
        # A rejected exchange leaves the agent in its bounded reconnect path.
        time.sleep(2)
        assert bad.poll() is None
        stop_process(bad)
        assert bad.returncode == 0 and vault_file.read_bytes() == stored_secret
        agents.remove(bad)
        collector = subprocess.Popen([
            str(root / "ztc" / "src" / "ztcagent"), "--config=" + str(agent_cf)],
            cwd=directory, env=agent_env, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE)
        agents.append(collector)
        wait_line(hub, "agent accepted " + agent1["id"])
        stop_process(collector)
        assert collector.returncode == 0
        agents.remove(collector)
        secure_cf = directory / "agent-secure.cf"
        secure_cf.write_text('loopbackTest: true, vaultStore: "keyring"\n')
        failed = subprocess.run([
            str(root / "ztc" / "src" / "ztcagent"),
            "--config=" + str(secure_cf)], cwd=directory,
            env=dict(agent_env,
                     DBUS_SESSION_BUS_ADDRESS="unsupported:address"),
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=20)
        assert failed.returncode != 0
        assert vault_file.read_bytes() == stored_secret
        bad_issuer = subprocess.run([
            str(root / "ztc" / "src" / "ztcagent"),
            "--config=" + str(agent_cf)], cwd=directory,
            env=dict(agent_env,
                     ZTC_ISSUER="http://example.test/oauth2/7",
                     ZTC_CLIENT_SECRET="invalid-device-secret"),
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=20)
        assert bad_issuer.returncode != 0
        assert vault_file.read_bytes() == stored_secret
        other_cert = directory / "other-cert.pem"
        subprocess.run([
            "openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes",
            "-days", "1", "-subj", "/CN=127.0.0.1",
            "-addext", "subjectAltName=IP:127.0.0.1",
            "-keyout", str(directory / "other-key.pem"),
            "-out", str(other_cert)], stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL, check=True)
        untrusted = subprocess.Popen([
            str(root / "ztc" / "src" / "ztcagent"),
            "--config=" + str(agent_cf)], cwd=directory,
            env=dict(agent_env, ZTC_CA_PATH=str(other_cert)),
            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        agents.append(untrusted)
        try:
            wait_line(hub, "agent accepted " + agent1["id"], timeout=5)
            raise AssertionError("agent accepted an untrusted hub certificate")
        except AssertionError as error:
            if "wire fixture output timed out" not in str(error):
                raise
        stop_process(untrusted)
        assert untrusted.returncode == 0
        agents.remove(untrusted)
        if os.environ.get("ZTC_AGENT_ONLY"):
            class OAuthFault(http.server.BaseHTTPRequestHandler):
                def log_message(self, *_):
                    pass

                def respond(self, status, value):
                    body = json.dumps(value).encode()
                    self.send_response(status)
                    self.send_header("Content-Type", "application/json")
                    self.send_header("Content-Length", str(len(body)))
                    self.end_headers()
                    try:
                        self.wfile.write(body)
                    except BrokenPipeError:
                        pass

                def do_GET(self):
                    self.respond(200, {"issuer": self.server.issuer,
                        "token_endpoint": self.server.issuer + "/v1/token"})

                def do_POST(self):
                    self.rfile.read(int(self.headers.get("Content-Length", "0")))
                    if self.server.hang:
                        self.server.inflight.set()
                        self.server.release.wait(20)
                        self.respond(503, {"error": "unavailable"})
                        return
                    if self.server.tokens:
                        self.server.tokenPosts += 1
                        token = self.server.tokens[min(
                            self.server.tokenPosts - 1, 1)]
                        self.respond(200, {"token_type": "Bearer",
                            "access_token": token, "expires_in": 3})
                        return
                    self.server.posts += 1
                    if self.server.posts == 2:
                        self.server.retried.set()
                    if self.server.posts == 3:
                        self.server.invalidRetried.set()
                    if self.server.posts == 1:
                        self.respond(503, {"error": "unavailable"})
                    else:
                        self.respond(200, {"token_type": "Bearer",
                                           "access_token": "invalid"})

            oauth = http.server.HTTPServer(("127.0.0.1", 0), OAuthFault)
            oauth.issuer = ("http://127.0.0.1:" +
                            str(oauth.server_address[1]) + "/oauth2/7")
            oauth.posts = 0
            oauth.hang = False
            oauth.tokens = []
            oauth.tokenPosts = 0
            oauth.retried = threading.Event()
            oauth.invalidRetried = threading.Event()
            oauth.inflight = threading.Event()
            oauth.release = threading.Event()
            serving = threading.Thread(target=oauth.serve_forever, daemon=True)
            serving.start()
            fault_env = dict(agent_env, ZTC_ISSUER=oauth.issuer,
                             ZTC_CLIENT_SECRET="fault-test-secret",
                             ZTCAGENT_HOME=str(directory / "fault-vault"))
            try:
                fault = subprocess.Popen([
                    str(root / "ztc" / "src" / "ztcagent"),
                    "--config=" + str(agent_cf)], cwd=directory,
                    env=fault_env, stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE)
                agents.append(fault)
                assert oauth.retried.wait(15), "token outage was not retried"
                assert oauth.invalidRetried.wait(15), "invalid token was not retried"
                stop_process(fault)
                assert fault.returncode == 0
                agents.remove(fault)
                assert not (directory / "fault-vault" / "vault" /
                            "secrets.json").exists()

                oauth.hang = True
                inflight = subprocess.Popen([
                    str(root / "ztc" / "src" / "ztcagent"),
                    "--config=" + str(agent_cf)], cwd=directory,
                    env=fault_env, stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE)
                agents.append(inflight)
                assert oauth.inflight.wait(10), "token request did not start"
                stop_process(inflight)
                assert inflight.returncode == 0
                agents.remove(inflight)
                oauth.release.set()
                oauth.hang = False
                oauth.tokens = [service_token(fixture, app_id, agent1),
                                service_token(fixture, app_id, agent1)]
                assert oauth.tokens[0] != oauth.tokens[1]
                short_cf = directory / "agent-short.cf"
                short_cf.write_text('loopbackTest: true, maxFrame: 65536, '
                    'upgradeTimeout: 1, vaultStore: "file", vaultTestStore: true\n')
                reconnect_env = dict(fault_env,
                    ZTC_CLIENT_SECRET=agent1["client_secret"])
                reconnecting = subprocess.Popen([
                    str(root / "ztc" / "src" / "ztcagent"),
                    "--config=" + str(short_cf)], cwd=directory,
                    env=reconnect_env, stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE)
                agents.append(reconnecting)
                wait_line(hub, "agent accepted " + agent1["id"])
                time.sleep(4)
                stop_process(hub)
                assert hub.returncode == 0
                hub = subprocess.Popen(hub_command(root, config, directory),
                    env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                hubs.append(hub)
                wait_line(hub, "ztchub ready")
                wait_line(hub, "agent accepted " + agent1["id"])
                assert oauth.tokenPosts >= 2
                stop_process(reconnecting)
                assert reconnecting.returncode == 0
                agents.remove(reconnecting)
            finally:
                oauth.release.set()
                oauth.shutdown()
                oauth.server_close()
                serving.join()
            success = True
            return
        if cluster:
            probe = run_wire(
                root, "agent", fixture.issuer(app_id), agent1["id"],
                "wss://127.0.0.1:" + str(standby_port) + "/ztc", cert,
                token=token1)
            try:
                wait_line(standby, "agent accepted " + agent1["id"], timeout=10)
            except AssertionError:
                pass
            else:
                stop_process(probe)
                raise AssertionError("standby admitted an agent")
            stop_process(probe)
        fixture.cookies = SimpleCookie()
        front_tokens = fixture.login(
            front_client["id"], "Client", app_id, return_tokens=True,
            offline=True, replay_refresh=True)
        front_token = front_tokens["access_token"]
        assert fixture.verify_jwt(front_token)["actions"] == ["Request"]
        wait_line(hub, "refresh family revocation received")

        if load:
            run_load(root, fixture, directory, hub, app_id, prefix,
                     role["Agent"]["id"], front_client, [agent1, agent2],
                     "wss://127.0.0.1:" + str(front_port) + "/ztc", cert,
                     fronts, counts)
            success = True
            return

        bad_subprotocol(front_port, front_token)
        invalid = run_wire(root, "front", fixture.issuer(app_id), agent1["id"],
                           "wss://127.0.0.1:" + str(front_port) + "/ztc",
                           cert, token=fixture.login(offline=False))
        invalid.wait(timeout=15)
        assert invalid.returncode != 0

        wss1 = "wss://127.0.0.1:" + str(front_port) + "/ztc"
        wss2 = "wss://127.0.0.1:" + str(second_port) + "/ztc"
        agent_process1 = run_wire(
            root, "agent", fixture.issuer(app_id), agent1["id"], wss2, cert,
            token=token1, telemetry=1, control_burst=512)
        agents.append(agent_process1)
        wait_line(agent_process1, "agent ready")
        wait_line(hub, "agent accepted " + agent1["id"])
        agent_process2 = run_wire(
            root, "agent", fixture.issuer(app_id), agent2["id"], wss1, cert,
            token=token2, telemetry=1)
        agents.append(agent_process2)
        wait_line(agent_process2, "agent ready")
        wait_line(hub, "agent accepted " + agent2["id"])

        native = run_wire(
            root, "front", fixture.issuer(app_id), agent1["id"], wss1, cert,
            token=front_token, expect=1)
        fronts.append(native)
        wait_line(agent_process1, "agent request ")
        wait_line(native, "front telemetry 1")
        native.wait(timeout=15)
        assert native.returncode == 0

        cookie = session_cookie(ssf_port, front_token, origin1)
        browser = run_wire(
            root, "front", fixture.issuer(app_id), agent2["id"], wss2, cert,
            cookie=cookie, origin=origin2, expect=1)
        fronts.append(browser)
        wait_line(agent_process2, "agent request ")
        wait_line(browser, "front telemetry 1")
        browser.wait(timeout=15)
        assert browser.returncode == 0

        example_port = free_port()
        example_client = app_client(
            fixture, admin, app_id, prefix, role["Client"]["id"],
            "ztchub example", 5, "native", redirect_port=example_port)
        example_cf = directory / "example.cf"
        example_cf.write_text(
            f'issuerURL: {json.dumps(fixture.issuer(app_id))}, '
            f'clientID: {json.dumps(example_client["id"])}, '
            f'scope: "Client offline_access", callbackPort: {example_port}, '
            'loginTimeout: 30, loopbackTest: true\n')
        example_env = {**os.environ,
                       "ZTCHUB_CLIENT_HOME": str(directory / "example-vault"),
                       "DBUS_SESSION_BUS_ADDRESS": "unsupported:address"}
        with (directory / "example.log").open("ab") as example_log:
            example = subprocess.Popen([
                str(root / "ztc" / "example" / "ztchub_client"),
                "--config=" + str(example_cf), "--no-browser",
                "--device-id=" + agent2["id"], "--wss=" + wss2,
                "--ca=" + str(cert)], stdout=subprocess.PIPE,
                stderr=example_log, env=example_env)
            fronts.append(example)
            fixture.cookies = SimpleCookie()
            fixture.cli_callback(example, example_port, app_id=app_id)
            output, _ = example.communicate(timeout=30)
            assert example.returncode == 0, "native example failed"
            assert output.count(b"telemetry device=") == 2, output
            assert b"refresh token rotated" in output, output
            resumed = subprocess.run([
                str(root / "ztc" / "example" / "ztchub_client"),
                "--config=" + str(example_cf), "--no-browser",
                "--device-id=" + agent2["id"], "--wss=" + wss2,
                "--ca=" + str(cert)], stdout=subprocess.PIPE,
                stderr=example_log, env=example_env, timeout=30)
            assert resumed.returncode == 0, "stored-token example failed"
            assert resumed.stdout.count(b"telemetry device=") == 1
            assert b"Open this URL" not in resumed.stdout

        control_front = run_wire(
            root, "front", fixture.issuer(app_id), agent1["id"], wss1, cert,
            token=front_token, expect=1000, stall_ms=2000)
        fronts.append(control_front)
        wait_line(control_front, "front ack")
        control_front.wait(timeout=15)
        assert control_front.returncode != 0

        duplicate = run_wire(
            root, "agent", fixture.issuer(app_id), agent1["id"], wss1, cert,
            token=token1, telemetry=1)
        agents.append(duplicate)
        try:
            duplicate.wait(timeout=10)
        except subprocess.TimeoutExpired:
            duplicate.terminate()
            duplicate.wait(timeout=10)
        assert b"agent request" not in (duplicate.stdout.read() or b"")

        eos_front = run_wire(
            root, "front", fixture.issuer(app_id), agent1["id"], wss1, cert,
            token=front_token, expect=100)
        fronts.append(eos_front)
        wait_line(eos_front, "front telemetry 1")
        agent_process1.terminate()
        wait_line(eos_front, "front error", timeout=15)
        agent_process1.wait(timeout=10)
        eos_front.wait(timeout=15)
        assert eos_front.returncode == 0
        agents.remove(agent_process1)

        overflow_agent = run_wire(
            root, "agent", fixture.issuer(app_id), agent1["id"], wss2, cert,
            token=token1, telemetry=128, payload=60000)
        agents.append(overflow_agent)
        wait_line(overflow_agent, "agent ready")
        wait_line(hub, "agent accepted " + agent1["id"])
        overflow_front = run_wire(
            root, "front", fixture.issuer(app_id), agent1["id"], wss2, cert,
            token=front_token, expect=1000, stall_ms=10000)
        fronts.append(overflow_front)
        wait_line(overflow_agent, "agent request ")
        error = wait_line(overflow_front, "front error", timeout=60)
        assert error.endswith(" 5")
        overflow_front.wait(timeout=15)

        before_roles = fixture.request(
            "GET", prefix + "/roles?limit=1000", token=admin)[0]["items"]
        stop_process(hub)
        assert hub.returncode == 0, "hub shutdown failed: " + str(hub.returncode)
        with socket.socket() as listener:
            listener.settimeout(1)
            try:
                listener.connect(("127.0.0.1", front_port))
                raise AssertionError("ztchub accepted after deactivation")
            except OSError:
                pass
        if cluster:
            hub = hubs[1]
            front_port = standby_port
            second_port = standby_port + 1
            wss1 = "wss://127.0.0.1:" + str(front_port) + "/ztc"
            wss2 = "wss://127.0.0.1:" + str(second_port) + "/ztc"
            restarted_agent = run_wire(
                root, "agent", fixture.issuer(app_id), agent1["id"], wss1,
                cert, token=token1, telemetry=1)
            agents.append(restarted_agent)
            wait_line(restarted_agent, "agent ready")
            wait_line(hub, "agent accepted " + agent1["id"])
        else:
            hub = None
            # The earlier overflow phase deliberately uses four-frame queues.
            # Size the restarted hub for complete real heap snapshots.
            config.write_text(config_text.replace(capacity,
                "controlFrames: 64, telemetryFrames: 1024, "
                "controlBytes: 1048576, telemetryBytes: 4194304, "
                "queueMem: 1073741824, expectedAgents: 2, "
                "publishersPerAgent: 64, activeFrontEnds: 2, "
                "subscriptionsPerFrontEnd: 16, "))
            resumed_env = dict(env)
            resumed_env.pop("ZUM_CLIENT_SECRET")
            resumed_env.pop("ZUM_SSF_AUTH")
            hub = subprocess.Popen(hub_command(root, config, directory),
                env=resumed_env, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            hubs.append(hub)
            wait_line(hub, "ztchub ready")
            restarted_agent = run_wire(
                root, "agent", fixture.issuer(app_id), agent1["id"], wss1,
                cert, token=token1, telemetry=1)
            agents.append(restarted_agent)
            wait_line(restarted_agent, "agent ready")
        restarted_front = run_wire(
            root, "front", fixture.issuer(app_id), agent1["id"], wss1,
            cert, token=front_token, expect=1)
        fronts.append(restarted_front)
        wait_line(restarted_agent, "agent request ")
        wait_line(restarted_front, "front telemetry 1")
        restarted_front.wait(timeout=15)
        assert restarted_front.returncode == 0
        after_roles = fixture.request(
            "GET", prefix + "/roles?limit=1000", token=admin)[0]["items"]
        assert after_roles == before_roles
        # Exercise the real collector and publisher path on two independent devices.
        for process in agents:
            stop_process(process)
        agents.clear()
        agent_cf = directory / "agent.cf"
        agent_cf.write_text('loopbackTest: true, maxFrame: 65536, '
                            'vaultStore: "file", vaultTestStore: true\n')
        real_agents = []
        real_publishers = []
        real_collectors = []
        real_envs = []
        real_pub_ids = []
        for index, device in enumerate((agent1, agent2)):
            ring = residue.shm("ztc-real-" + secrets.token_hex(6))
            registry = residue.tmp_dir("publisher-registry").name
            publisher_id = residue.shm("ztc-pub-" + secrets.token_hex(6))
            agent_env = dict(os.environ, ZTC_ISSUER=fixture.issuer(app_id),
                             ZTC_CLIENT_ID=device["id"], ZTC_DEVICE_ID=device["id"],
                             ZTC_CLIENT_SECRET=device["client_secret"], ZTC_WSS_URL=wss1,
                             ZTCAGENT_HOME=str(directory / "agent-vault"),
                             ZTC_CA_PATH=str(cert), ZTC_RING=ring, ZTC_DIR=registry)
            publisher = subprocess.Popen([
                str(root / "ztc" / "itest" / "ztchubwiretest"),
                "--mode=publisher", "--device-id=" + publisher_id,
                "--controlled"],
                cwd=directory, env=agent_env, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                stderr=subprocess.PIPE)
            agents.append(publisher)
            real_publishers.append(publisher)
            real_envs.append(agent_env)
            real_pub_ids.append(publisher_id)
            wait_line(publisher, "publisher ready")
            collector = subprocess.Popen([
                str(root / "ztc" / "src" / "ztcagent"), "--config=" + str(agent_cf)],
                cwd=directory, env=agent_env, stdout=subprocess.PIPE,
                stderr=subprocess.PIPE)
            agents.append(collector)
            real_collectors.append(collector)
            real_agents.append(device["id"])
            wait_line(hub, "agent accepted " + device["id"])
        live_dashboard = bool(os.environ.get("ZDASH_LIVE_TEST"))
        if live_dashboard:
            dashboard_cf = directory / "dashboard.cf"
            dashboard_cf.write_text(
                "wssURL: " + json.dumps(wss1) + ", caPath: " + json.dumps(str(cert)) +
                ", gtkGlade: " + json.dumps(str(root / "zdash/src/zdash.glade")) +
                ", deviceID: " + json.dumps(real_agents[0]) +
                ', interval: 1000, maxSubscriptions: 16, '
                'telRing: {name: "zdash-live-test", size: 1048576}\n')
            dashboard_env = dict(os.environ,
                ZDASH_TEST=str(root / "zdash/test/.libs/zdash_test.so"),
                ZDASH_TEST_TOKEN=front_token, ZDASH_TEST_LIVE="1",
                ZDASH_TEST_DEVICE=real_agents[0], ZDASH_TEST_PUBLISHER=real_pub_ids[0],
                ZDASH_TEST_TIMEOUT="120", G_DEBUG="fatal-warnings", GDK_BACKEND="x11",
                NO_AT_BRIDGE="1")
            dashboard_env.pop("WAYLAND_DISPLAY", None)
            dashboard_env.pop("DBUS_SESSION_BUS_ADDRESS", None)
            dashboard = subprocess.Popen([
                "xvfb-run", "-a", str(root / "zdash/src/zdash"),
                "--config=" + str(dashboard_cf)], env=dashboard_env,
                start_new_session=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            dashboard._process_group = True
            fronts.append(dashboard)
            wait_line(dashboard, "# dashboard initial", timeout=30)
            real_publishers[0].stdin.write(b"e\n")
            real_publishers[0].stdin.flush()
            wait_line(real_publishers[0], "publisher snapshot boundary")
            real_publishers[0].stdin.write(b"u\n")
            real_publishers[0].stdin.flush()
            wait_line(dashboard, "# dashboard live update", timeout=30)
            late_id = residue.shm("ztc-late-" + secrets.token_hex(6))
            late = subprocess.Popen([
                str(root / "ztc/itest/ztchubwiretest"), "--mode=publisher",
                "--device-id=" + late_id, "--controlled"],
                cwd=directory, env=real_envs[0], stdin=subprocess.PIPE,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            agents.append(late)
            wait_line(late, "publisher ready")
            wait_line(dashboard, "# dashboard late publisher", timeout=30)
            stop_process(real_collectors[0])
            agents.remove(real_collectors[0])
            collector = subprocess.Popen([
                str(root / "ztc/src/ztcagent"), "--config=" + str(agent_cf)],
                cwd=directory, env=real_envs[0], stdout=subprocess.PIPE,
                stderr=subprocess.PIPE)
            agents.append(collector)
            real_collectors[0] = collector
            wait_line(hub, "agent accepted " + real_agents[0])
            wait_line(dashboard, "# dashboard transport reconnection", timeout=30)
            # One aggregate snapshot EOS must follow both real publishers.
            fanout = run_wire(root, "front", fixture.issuer(app_id),
                real_agents[0], wss1, cert, token=front_token, group="Heap",
                one_shot=True, expect_publishers=2)
            fronts.append(fanout)
            wait_line(fanout, "front eos", timeout=30)
            fanout.wait(timeout=15)
            assert fanout.returncode == 0
            # Retiring one wildcard leg reports the failure while the other
            # publisher keeps delivering snapshots under the same request.
            survivor = run_wire(root, "front", fixture.issuer(app_id),
                real_agents[0], wss1, cert, token=front_token, group="Heap",
                snapshots=5, source_errors=True)
            fronts.append(survivor)
            wait_line(survivor, "front snapshot 2", timeout=30)
            late.stdin.write(b"q\n")
            late.stdin.flush()
            late.wait(timeout=15)
            assert late.returncode == 0
            wait_line(survivor, "front error", timeout=30)
            wait_line(survivor, "front snapshot 5", timeout=30)
            survivor.wait(timeout=15)
            assert survivor.returncode == 0
            agents.remove(late)
            # Confirmed publisher shutdown removes its rows; transport EOS did not.
            publisher = real_publishers[0]
            publisher.stdin.write(b"q\n")
            publisher.stdin.flush()
            publisher.wait(timeout=15)
            assert publisher.returncode == 0
            agents.remove(publisher)
            wait_line(dashboard, "# dashboard publisher shutdown", timeout=30)
            _, dashboard._diagnostic = dashboard.communicate(timeout=15)
            assert dashboard.returncode == 0, dashboard._diagnostic.decode(
                errors="replace")
            # With no publishers, a requested snapshot fails instead of emitting EOS.
            empty = run_wire(root, "front", fixture.issuer(app_id),
                real_agents[0], wss1, cert, token=front_token, group="Heap",
                one_shot=True, expect=0)
            fronts.append(empty)
            assert wait_line(empty, "front error", timeout=30) == "front error 7"
            empty.wait(timeout=15)
            assert empty.returncode == 0
            # Restore this producer for the remaining independent wire checks.
            publisher = subprocess.Popen([
                str(root / "ztc/itest/ztchubwiretest"), "--mode=publisher",
                "--device-id=" + real_pub_ids[0], "--controlled"],
                cwd=directory, env=real_envs[0], stdin=subprocess.PIPE,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            agents.append(publisher)
            real_publishers[0] = publisher
            wait_line(publisher, "publisher ready")
            publisher.stdin.write(b"u\n")
            publisher.stdin.flush()
            wait_line(publisher, "publisher changed")
        for index, (device_id, publisher) in enumerate(zip(real_agents, real_publishers)):
            front = run_wire(root, "front", fixture.issuer(app_id), device_id,
                             wss1, cert, token=front_token, expect=1)
            fronts.append(front)
            wait_line(front, "front telemetry 1")
            front.wait(timeout=15)
            assert front.returncode == 0
            # Successful snapshot ACK must retain the agent route until EOS:
            # the real publisher queues its heap capture asynchronously.
            snapshot = run_wire(
                root, "front", fixture.issuer(app_id), device_id, wss1, cert,
                token=front_token, expect=1, one_shot=True, group="Heap")
            fronts.append(snapshot)
            wait_line(snapshot, "front telemetry 1")
            wait_line(snapshot, "front eos")
            snapshot.wait(timeout=15)
            assert snapshot.returncode == 0
            continuing = run_wire(
                root, "front", fixture.issuer(app_id), device_id, wss1, cert,
                token=front_token, expect=1, group="Heap", snapshots=3,
                changes=not live_dashboard or index > 0)
            fronts.append(continuing)
            if not live_dashboard or index > 0:
                wait_line(continuing, "front snapshot 1")
                publisher.stdin.write(b"u\n")
                publisher.stdin.flush()
                wait_line(publisher, "publisher changed")
            wait_line(continuing, "front snapshot 3")
            if not live_dashboard or index > 0:
                wait_line(continuing, "front new heap and updated heap")
            continuing.wait(timeout=15)
            assert continuing.returncode == 0
        # Independent consumers of the same group/filter must retain replies
        # after another consumer finishes and unsubscribes.
        short = run_wire(root, "front", fixture.issuer(app_id), real_agents[0],
                         wss1, cert, token=front_token, snapshots=2)
        long = run_wire(root, "front", fixture.issuer(app_id), real_agents[0],
                        wss1, cert, token=front_token, snapshots=4)
        fronts.extend((short, long))
        wait_line(short, "front snapshot 2")
        short.wait(timeout=15)
        assert short.returncode == 0
        wait_line(long, "front snapshot 4")
        long.wait(timeout=15)
        assert long.returncode == 0
        if live_dashboard:
            # Inventory owns a separate registry from ordinary routes. A
            # one-frame queue overflows while its two cached publishers are
            # enumerated, without cancelling another frontend's App stream.
            normal_config = config.read_text()
            small_config = normal_config.replace("telemetryFrames: 1024",
                                                  "telemetryFrames: 1")
            assert small_config != normal_config
            stop_process(hub)
            assert hub.returncode == 0
            config.write_text(small_config)
            hub = subprocess.Popen(hub_command(root, config, directory),
                env=resumed_env, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            hubs.append(hub)
            wait_line(hub, "ztchub ready")
            wait_agents(hub, real_agents)
            # Collector discovery samples every second. Allow two sampling
            # periods plus a scheduling margin to refill both cached sources.
            time.sleep(3)
            survivor = run_wire(root, "front", fixture.issuer(app_id),
                real_agents[0], wss1, cert, token=front_token, snapshots=4)
            fronts.append(survivor)
            wait_line(survivor, "front snapshot 1", timeout=30)
            overflow_inventory = run_wire(root, "front", fixture.issuer(app_id),
                "", wss1, cert, token=front_token, inventory=True, expect=100)
            fronts.append(overflow_inventory)
            error = wait_line(overflow_inventory, "front error", timeout=30)
            assert error.endswith(" 5")
            overflow_inventory.wait(timeout=15)
            assert overflow_inventory.returncode == 0
            wait_line(survivor, "front snapshot 4", timeout=30)
            survivor.wait(timeout=15)
            assert survivor.returncode == 0
            stop_process(hub)
            assert hub.returncode == 0
            config.write_text(normal_config)
            hub = subprocess.Popen(hub_command(root, config, directory),
                env=resumed_env, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            hubs.append(hub)
            wait_line(hub, "ztchub ready")
            wait_agents(hub, real_agents)
        inventory = run_wire(
            root, "front", fixture.issuer(app_id), "", wss1, cert,
            token=front_token, expect=len(real_agents), inventory=True,
            wait_shutdown=True)
        fronts.append(inventory)
        wait_line(inventory, "front telemetry " + str(len(real_agents)))
        stop_process(real_publishers[0])
        wait_line(inventory, "front shutdown")
        inventory.wait(timeout=15)
        assert inventory.returncode == 0
        inventory = run_wire(
            root, "front", fixture.issuer(app_id), "", wss1, cert,
            token=front_token, expect=len(real_agents) - 1, inventory=True,
            one_shot=True)
        fronts.append(inventory)
        wait_line(inventory, "front telemetry " + str(len(real_agents) - 1))
        inventory.wait(timeout=15)
        assert inventory.returncode == 0
        for process in agents:
            stop_process(process)
        agents.clear()
        assert (zumd_regdir / (zumd_publisher + ".pid")).exists()
        zumd_agent_env = dict(os.environ, ZTC_ISSUER=fixture.issuer(app_id),
                              ZTC_CLIENT_ID=agent1["id"],
                              ZTC_DEVICE_ID=agent1["id"],
                              ZTC_CLIENT_SECRET=agent1["client_secret"],
                              ZTC_WSS_URL=wss1,
                              ZTCAGENT_HOME=str(directory / "zumd-agent-vault"),
                              ZTC_CA_PATH=str(cert), ZTC_RING=zumd_ring,
                              ZTC_DIR=zumd_registry)
        collector = subprocess.Popen([
            str(root / "ztc" / "src" / "ztcagent"), "--config=" + str(agent_cf)],
            cwd=directory, env=zumd_agent_env, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE)
        agents.append(collector)
        wait_line(hub, "agent accepted " + agent1["id"])
        front = run_wire(root, "front", fixture.issuer(app_id), agent1["id"],
                         wss1, cert, token=front_token, expect=1)
        fronts.append(front)
        wait_line(front, "front telemetry 1")
        front.wait(timeout=15)
        assert front.returncode == 0
        success = True
    finally:
        for index, process in enumerate(fronts + agents):
            stop_process(process)
            if not success and process._diagnostic:
                (directory / ("peer-" + str(index) + ".log")).write_bytes(
                    process._diagnostic)
        for process in hubs:
            stop_process(process)
            if process._diagnostic:
                (directory / ("hub-" + str(hubs.index(process)) + ".log")).write_bytes(
                    process._diagnostic)
        shutdown_ok = (not hubs or not success or
                       all(process.returncode == 0 for process in hubs))
        success = success and shutdown_ok
        try:
            fixture.stop()
        except Exception:
            pass
        if os.environ.get("ZTC_VALGRIND"):
            for diagnostic in sorted(directory.glob("valgrind-*.log")):
                print(diagnostic.read_text(), flush=True)
        residue.finish(success)
        assert shutdown_ok, "hub shutdown failed: " + ", ".join(
            str(process.returncode) for process in hubs)


if __name__ == "__main__":
    main()
    label = ("ztcagent Vault provisioning and restart"
             if os.environ.get("ZTC_AGENT_ONLY") else
             "ztchub WSS workload and latency" if os.environ.get("ZTC_LOAD") else
             "ztchub OAuth, WSS routing, overflow, and restart")
    print("1..1\nok 1 - " + label)
