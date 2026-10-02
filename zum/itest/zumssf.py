#!/usr/bin/env python3
# (c) Copyright 2026 Huw Rogers
# This code is licensed under the MIT license (see LICENSE for details)

"""Real zumd/libZum SSF refresh-family delivery and restart fixture."""

import base64
import json
import os
from pathlib import Path
import re
import secrets
import signal
import socket
import subprocess
import sqlite3
import time

from zumhttp import Fixture
from zumidp import Provider, TLSProxy
from zi_test_residue import Residue


def free_port():
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        return listener.getsockname()[1]


def launch_service(fixture, config, env):
    executable = Path(__file__).resolve().parent.parent / "example" / "zumpingd"
    log = (fixture.directory / "zumpingd-ssf.log").open("ab")
    process = subprocess.Popen([str(executable), "--config", str(config)],
                               env=env, stdout=subprocess.PIPE, stderr=log)
    fixture.wait_output(process, b"zumpingd listening on port ")
    return process, log


def stop_service(process, log):
    if process.poll() is None:
        process.send_signal(signal.SIGTERM)
    process.communicate(timeout=30)
    log.close()
    assert process.returncode == 0, "zumpingd shutdown failed"


def main():
    for key in ("ZDB_MODULE", "ZDB_CONNECT"):
        if not os.environ.get(key):
            raise AssertionError("set " + key + " for a fresh SSF fixture")
    residue = Residue("zum-ssf")
    directory = residue.directory
    fixture = Fixture(directory)
    # Fixture.admin_cli launches the real CLI with the process environment;
    # keep it on the same disposable database key as the daemon.
    os.environ["ZUM_DB_KEY"] = fixture.env["ZUM_DB_KEY"]
    provider = None
    proxy = None
    service = None
    service_log = None
    success = False
    try:
        fixture.start()
        fixture.enroll()
        admin = fixture.login()
        fixture.admin_cli()
        audience_uri = "https://ssf.example/api"
        app = fixture.admin_secret("appEnroll", {
            "name": "ssf-service",
            "audience": audience_uri}, idempotence=secrets.token_hex(16))["item"]
        app_id = app["app_id"]
        service_port = free_port()
        (directory / "tls").mkdir()
        provider = Provider(directory / "tls")
        proxy = TLSProxy(service_port, provider)
        node_source = Path(__file__).with_name("zumd.cf").read_text()
        fixture.node_config = directory / "zumd-ssf.cf"
        fixture.node_config.write_text(
            node_source.rstrip() + f',\noidc: {{caPath: {json.dumps(str(provider.ca_path))}}},\n'
            'ssf: {leaseMax: 30, receiverMax: 3, errorMax: 3}\n')
        fixture.stop()
        fixture.start()
        fixture.request("GET", "/health/ready")
        service_config = directory / "zumpingd-ssf.cf"
        service_config.write_text(
            f'zum: {{issuerURL: {json.dumps(fixture.issuer(app_id))}, '
            f'managementIssuerURL: {json.dumps(fixture.issuer(fixture.core_app_id))}, '
            f'managementURL: {json.dumps(fixture.origin)}, '
            f'clientID: {json.dumps(app["client_id"])} }}, '
            f'caPath: "", audience: {json.dumps(audience_uri)}, '
            f'ssfDeliveryURL: {json.dumps(proxy.origin + "/ssf")}, '
            f'ssfLease: 8, port: {service_port}\n')
        callback_auth = "Bearer " + secrets.token_urlsafe(24)
        service_env = dict(os.environ, ZUM_CLIENT_SECRET=app["client_secret"],
                           ZUM_SSF_AUTH=callback_auth)
        for key in ("ZUM_DB_KEY", "ZDB_MODULE", "ZDB_CONNECT"):
            service_env.pop(key, None)
        service, service_log = launch_service(fixture, service_config, service_env)

        # The real resource server owns its catalog.  Publish it before
        # assigning the fixture user and native client to the advertised role.
        catalog = {}
        for operation in ("actionQuery", "roleQuery"):
            catalog[operation] = fixture.admin_command(
                operation, {"app_id": app_id})["items"]
        assert len(catalog["actionQuery"]) == 1
        assert len(catalog["roleQuery"]) == 1
        user_config, _, authenticator, _ = fixture.ping_user(
            app, catalog, service_port)
        client_match = re.search(r'clientID: "([^"]+)"', user_config.read_text())
        assert client_match
        client_id = client_match[1]

        # Observe actual persisted registration and timer renewal. SQLite is
        # read independently of the daemon, so this also checks store delivery.
        def receivers():
            with sqlite3.connect(fixture.env["ZDB_CONNECT"]) as db:
                columns = [item[1] for item in db.execute(
                    'PRAGMA table_info("a_zum.ssf_rx")')]
                return [dict(zip(columns, row)) for row in db.execute(
                    'SELECT * FROM "a_zum.ssf_rx"')]

        def number(value):
            return int.from_bytes(value, "big", signed=False) if isinstance(value, bytes) else value

        def wait_for(check):
            deadline = time.monotonic() + 15
            while True:
                value = check()
                if value:
                    return value
                assert time.monotonic() < deadline, "SSF lifecycle deadline exceeded"
                time.sleep(0.02)

        def receiver(suffix):
            return next((row for row in receivers() if row["receiver_i_d"].endswith(suffix)), None)

        first = wait_for(lambda: receiver(proxy.origin + "/ssf"))
        assert callback_auth.encode() not in first["callback_auth"], "callback stored in plaintext"
        initial_expiry = number(first["expires"])
        wait_for(lambda: (row := receiver(proxy.origin + "/ssf")) and
                 number(row["expires"]) > initial_expiry)

        basic = base64.b64encode((app["client_id"] + ":" + app["client_secret"]).encode()).decode()
        workload = fixture.request("POST", fixture.oauth(app_id, "token"), {
            "grant_type": "client_credentials", "scope": "zum.catalog"}, form=True,
            headers={"Authorization": "Basic " + basic})[0]["access_token"]
        path = f"/admin/apps/{app_id}/ssf"

        def register(name, ttl=300, url=None, status=200, token=workload):
            return fixture.request("POST", path, {
                "receiver_id": name, "delivery_url": url or proxy.origin + "/ssf",
                "callback_auth": callback_auth, "expires_in": ttl}, token=token, status=status)[0]

        register("expired", ttl=2, token=None, status=401)
        register("expired", ttl=2, token=admin, status=403)
        register("bad-url", url="http://example.test/ssf", status=400)
        lease = register("expired", ttl=2)
        assert lease["expires_in"] == 2
        wait_for(lambda: receiver("expired"))
        wait_for(lambda: not receiver("expired"))
        # Capacity is global, while receiver identity is client/application scoped.
        lease = register("dead", url="http://127.0.0.1:" + str(free_port()) + "/ssf")
        assert lease["expires_in"] == 30
        register("capacity", ttl=2)
        register("overflow", status=429)
        wait_for(lambda: not receiver("capacity"))

        fixture.authenticator = authenticator
        tokens = fixture.login(
            client_id=client_id, app_id=app_id, scope="ping",
            return_tokens=True, replay_refresh=True, login="user")
        fixture.wait_output(service, b"zumpingd refresh family revoked")
        wait_for(lambda: not receiver("dead"))
        def acknowledged():
            with sqlite3.connect(fixture.env["ZDB_CONNECT"]) as db:
                return not db.execute(
                    'SELECT 1 FROM "a_zum.ssf_delivery" WHERE receiver_i_d = ?',
                    (first["receiver_i_d"],)).fetchone()
        wait_for(acknowledged)

        import http.client
        connection = http.client.HTTPConnection("127.0.0.1", service_port, timeout=15)
        try:
            connection.request("GET", "/ping", headers={
                "Authorization": "Bearer " + tokens["access_token"]})
            response = connection.getresponse()
            response.read()
            assert response.status == 200, "refresh revocation rejected access token"
        finally:
            connection.close()

        # Start a separate refresh family for the outage/restart delivery.
        fixture.cookies.clear()
        pending = fixture.login(
            client_id=client_id, app_id=app_id, scope="ping",
            return_tokens=True, replay_refresh=False, login="user")

        # Leave one durable delivery unacknowledged, restart zumd, then let the
        # retry deliver it to a restarted Receiver.
        stop_service(service, service_log)
        service = service_log = None
        proxy.backend_port = free_port()
        with sqlite3.connect(fixture.env["ZDB_CONNECT"]) as db:
            previous_events = {row[0] for row in db.execute(
                'SELECT event_i_d FROM "a_zum.ssf_delivery"')}
        fixture.request("POST", fixture.oauth(app_id, "token"), {
            "grant_type": "refresh_token", "client_id": client_id,
            "refresh_token": pending["refresh_token"]}, form=True)
        fixture.request("POST", fixture.oauth(app_id, "token"), {
            "grant_type": "refresh_token", "client_id": client_id,
            "refresh_token": pending["refresh_token"]}, form=True, status=400)
        # Wait for the asynchronous outbox insert to reach the store before
        # restarting. An HTTP readiness response is not a persistence barrier.
        def queued():
            with sqlite3.connect(fixture.env["ZDB_CONNECT"]) as db:
                return any(row[0] not in previous_events for row in db.execute(
                    'SELECT event_i_d FROM "a_zum.ssf_delivery"'))
        wait_for(queued)
        service, service_log = launch_service(fixture, service_config, service_env)
        fixture.stop()
        fixture.start()
        fixture.request("GET", "/health/ready")
        proxy.backend_port = service_port
        fixture.wait_output(service, b"zumpingd refresh family revoked")
        # Stop renewing: cleanup must reclaim the receiver without another
        # revocation or registration request.
        stop_service(service, service_log)
        service = service_log = None
        wait_for(lambda: not receivers())
        success = True
    finally:
        if service is not None:
            try:
                stop_service(service, service_log)
            except Exception:
                service.kill()
                service.communicate()
                if service_log:
                    service_log.close()
        if proxy is not None:
            proxy.close()
        if provider is not None:
            provider.close()
        try:
            fixture.stop()
        except Exception:
            pass
        residue.finish(success)


if __name__ == "__main__":
    main()
    print("1..1\nok 1 - SSF refresh-family delivery and restart")
