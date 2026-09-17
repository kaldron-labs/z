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
import signal
import socket
import ssl
import subprocess
import tempfile
import time
from urllib.parse import urlencode

from zumhttp import Fixture


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
                raise AssertionError("wire fixture output timed out: " + prefix)
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


def stop_process(process, timeout=20):
    if process is None:
        return
    if process.poll() is None:
        process.send_signal(signal.SIGTERM)
    try:
        _, diagnostic = process.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        process.kill()
        _, diagnostic = process.communicate()
    process._diagnostic = diagnostic


def run_wire(root, mode, issuer, device, wss, ca, token=None, cookie=None,
             origin=None, telemetry=1, expect=1, stall_ms=0, payload=0,
             control_burst=1):
    executable = root / "ztc" / "itest" / "ztchubwiretest"
    environment = dict(os.environ)
    if token is None:
        environment.pop("ZTC_ACCESS_TOKEN", None)
    else:
        environment["ZTC_ACCESS_TOKEN"] = token
    args = [str(executable), "--mode=" + mode, "--issuer=" + issuer,
            "--device-id=" + device, "--wss=" + wss, "--ca=" + str(ca),
            "--telemetry=" + str(telemetry), "--expect=" + str(expect)]
    if stall_ms:
        args.append("--stall-ms=" + str(stall_ms))
    if payload:
        args.append("--payload=" + str(payload))
    if control_burst != 1:
        args.append("--control-burst=" + str(control_burst))
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


def app_client(fixture, admin, app_id, prefix, role_id, label, grants, kind, redirect_port=49152):
    item = fixture.admin_secret("clientAdd", {
        "appID": app_id, "label": label, "type": kind,
        "redirectURIs": ([f"http://127.0.0.1:{redirect_port}/callback"]
                          if kind == "native" else []),
        "grants": grants, "refreshAllowed": grants >= 5,
        "$idempotencyKey": secrets.token_hex(16)})["item"]
    fixture.request("PUT", prefix + "/client-access/" + item["id"], {
        "roleIDs": [role_id]},
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
            "appID": app_id, "label": "load device " + str(index),
            "type": "confidential", "redirectURIs": [], "grants": 2},
            token=admin, headers={"Idempotency-Key": secrets.token_hex(16)},
            status=201)[0]["item"]
        fixture.request("PUT", prefix + "/client-access/" + client["id"],
                        {"roleIDs": [role_id]}, token=admin,
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
    directory = Path(tempfile.mkdtemp(prefix="ztchub-"))
    fixture = Fixture(directory / "zum")
    fixture.directory.mkdir()
    hub = None
    hubs = []
    agents = []
    fronts = []
    success = False
    shm_names = []
    registries = []
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
            "name": "ztchub-itest", "integration": "catalogClient",
            "audience": service_audience,
            "$idempotencyKey": secrets.token_hex(16)})["item"]
        app_id = app["appID"]
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
        callback_ref = "ZUM_SSF_ZTCHUB_AUTH"
        fixture.env[callback_ref] = callback_auth
        node_source = (Path(__file__).resolve().parents[2] /
                       "zum" / "itest" / "zumd.cf").read_text()
        fixture.node_config = directory / "zumd-ztchub.cf"
        fixture.node_config.write_text(
            node_source.rstrip() + ",\n" +
            "oidc: {caPath: " + json.dumps(str(cert)) + "},\n" +
            "ssf: {issuer: " + json.dumps(fixture.issuer(app_id)) +
            ", receivers: [{receiverID: \"ztchub\", appID: " + str(app_id) +
            ", audience: " + json.dumps(service_audience) +
            ", deliveryURL: " +
            json.dumps("https://127.0.0.1:" + str(ssf_port) + "/ssf") +
            ", secretRef: \"" + callback_ref + "\", revision: 1}]}\n")
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
            "actions: [\"Request\", \"Telemetry\"], "
            "roles: [\"Client\", \"Agent\"], maxFrame: 65536, "
            + capacity + ("idleTimeout: 900, " if load else "") +
            "minRefreshMS: 1000, fanoutSLOMS: 200, "
            "schedulerTurnWork: 64\n")
        config.write_text(config_text)
        env = dict(os.environ, ZUM_CLIENT_SECRET=app["client_secret"],
                   ZUM_SSF_CALLBACK_AUTH=callback_auth)
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
            "POST", prefix + "/memberships", {"userID": user["id"]},
            token=admin, headers={"Idempotency-Key": secrets.token_hex(16)},
            status=201)[0]["item"]
        fixture.request("PUT", prefix + "/memberships/" + user["id"] + "/roles",
                        {"roleIDs": [role["Client"]["id"]]}, token=admin,
                        headers={"If-Match": member["etag"]})
        front_client = app_client(
            fixture, admin, app_id, prefix, role["Client"]["id"],
            "ztchub native front end", 5, "native")
        agent1 = app_client(
            fixture, admin, app_id, prefix, role["Agent"]["id"],
            "ztchub device one", 2, "confidential")
        agent2 = app_client(
            fixture, admin, app_id, prefix, role["Agent"]["id"],
            "ztchub device two", 2, "confidential")
        assert agent1["id"] != agent2["id"]
        token1 = service_token(fixture, app_id, agent1)
        token2 = service_token(fixture, app_id, agent2)
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
        with (directory / "example.log").open("ab") as example_log:
            example = subprocess.Popen([
                str(root / "ztc" / "example" / "ztchub_client"),
                "--config=" + str(example_cf), "--no-browser",
                "--device-id=" + agent2["id"], "--wss=" + wss2,
                "--ca=" + str(cert)], stdout=subprocess.PIPE,
                stderr=example_log)
            fronts.append(example)
            fixture.cookies = SimpleCookie()
            fixture.cli_callback(example, example_port, app_id=app_id)
            output, _ = example.communicate(timeout=30)
            assert example.returncode == 0, "native example failed"
            assert output.count(b"telemetry device=") == 2, output
            assert b"refresh token rotated" in output, output

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
        wait_line(eos_front, "front eos", timeout=15)
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
            hub = subprocess.Popen(hub_command(root, config, directory),
                env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
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
        agent_cf.write_text("loopbackTest: true, maxFrame: 65536\n")
        real_agents = []
        for index, device in enumerate((agent1, agent2)):
            ring = "ztc-real-" + secrets.token_hex(6)
            registry = "ztc-pubs-" + secrets.token_hex(6)
            publisher_id = "ztc-pub-" + secrets.token_hex(6)
            shm_names.extend((ring, publisher_id))
            registries.append(Path(tempfile.gettempdir()) / registry)
            agent_env = dict(os.environ, ZTC_ISSUER=fixture.issuer(app_id),
                             ZTC_CLIENT_ID=device["id"], ZTC_DEVICE_ID=device["id"],
                             ZTC_CREDENTIAL_STORE=str(directory), ZTC_WSS_URL=wss1,
                             ZTC_ACCESS_TOKEN=service_token(fixture, app_id, device),
                             ZTC_CA_PATH=str(cert), ZTC_RING=ring, ZTC_DIR=registry)
            publisher = subprocess.Popen([
                str(root / "ztc" / "itest" / "ztchubwiretest"),
                "--mode=publisher", "--device-id=" + publisher_id],
                cwd=directory, env=agent_env, stdout=subprocess.PIPE,
                stderr=subprocess.PIPE)
            agents.append(publisher)
            wait_line(publisher, "publisher ready")
            collector = subprocess.Popen([
                str(root / "ztc" / "src" / "ztcagent"), "--config=" + str(agent_cf)],
                cwd=directory, env=agent_env, stdout=subprocess.PIPE,
                stderr=subprocess.PIPE)
            agents.append(collector)
            real_agents.append(device["id"])
            wait_line(hub, "agent accepted " + device["id"])
        for device_id in real_agents:
            front = run_wire(root, "front", fixture.issuer(app_id), device_id,
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
        for name in shm_names:
            for suffix in (".ctrl", ".data"):
                (Path("/dev/shm") / (name + suffix)).unlink(missing_ok=True)
        import shutil
        for registry in registries:
            shutil.rmtree(registry, ignore_errors=True)
        try:
            fixture.stop()
        except Exception:
            pass
        if os.environ.get("ZTC_VALGRIND"):
            for diagnostic in sorted(directory.glob("valgrind-*.log")):
                print(diagnostic.read_text(), flush=True)
        if success and directory.exists():
            for child in directory.rglob("*"):
                if child.is_file():
                    child.chmod(0o600)
            import shutil
            shutil.rmtree(directory)
        else:
            print("# failed fixture diagnostics retained in " + str(directory),
                  flush=True)
        assert shutdown_ok, "hub shutdown failed: " + ", ".join(
            str(process.returncode) for process in hubs)


if __name__ == "__main__":
    main()
    label = ("ztchub WSS workload and latency" if os.environ.get("ZTC_LOAD") else
             "ztchub OAuth, WSS routing, overflow, and restart")
    print("1..1\nok 1 - " + label)
