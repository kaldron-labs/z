#!/usr/bin/env python3
# (c) Copyright 2026 Huw Rogers
# This code is licensed under the MIT license (see LICENSE for details)

"""Real zumd/libZum SSF refresh-family delivery and restart fixture."""

import json
import os
from pathlib import Path
import re
import secrets
import shutil
import signal
import socket
import subprocess
import tempfile

from zumhttp import Fixture
from zumidp import Provider, TLSProxy


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
    directory = Path(tempfile.mkdtemp(prefix="zum-ssf-"))
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
            "audience": audience_uri,
            "$idempotencyKey": secrets.token_hex(16)})["item"]
        app_id = app["appID"]
        service_port = free_port()
        service_config = directory / "zumpingd-ssf.cf"
        service_config.write_text(
            f'zum: {{issuerURL: {json.dumps(fixture.issuer(app_id))}, '
            f'managementIssuerURL: {json.dumps(fixture.issuer(fixture.core_app_id))}, '
            f'managementURL: {json.dumps(fixture.origin)}, '
            f'clientID: {json.dumps(app["client_id"])} }}, '
            f'caPath: "", audience: {json.dumps(audience_uri)}, '
            f'port: {service_port}\n')
        callback_auth = "Bearer " + secrets.token_urlsafe(24)
        callback_ref = "ZUM_SSF_SSF_AUTH"
        service_env = dict(os.environ, ZUM_CLIENT_SECRET=app["client_secret"],
                           ZUM_SSF_CALLBACK_AUTH=callback_auth)
        for key in ("ZUM_DB_KEY", "ZDB_MODULE", "ZDB_CONNECT"):
            service_env.pop(key, None)
        service, service_log = launch_service(fixture, service_config, service_env)

        # The real resource server owns its catalog.  Publish it before
        # assigning the fixture user and native client to the advertised role.
        catalog = {}
        for operation in ("actionQuery", "roleQuery"):
            catalog[operation] = fixture.admin_command(
                operation, {"appID": app_id})["items"]
        assert len(catalog["actionQuery"]) == 1
        assert len(catalog["roleQuery"]) == 1
        user_config, _, authenticator, _ = fixture.ping_user(
            app, catalog, service_port)
        client_match = re.search(r'clientID: "([^"]+)"', user_config.read_text())
        assert client_match
        client_id = client_match[1]

        (directory / "tls").mkdir()
        provider = Provider(directory / "tls")
        proxy = TLSProxy(service_port, provider)
        fixture.env[callback_ref] = callback_auth
        node_source = Path(__file__).with_name("zumd.cf").read_text()
        fixture.node_config = directory / "zumd-ssf.cf"
        fixture.node_config.write_text(
            node_source.rstrip() + f',\noidc: {{caPath: {json.dumps(str(provider.ca_path))}}},\n'
            f'ssf: {{issuer: {json.dumps(fixture.issuer(fixture.core_app_id))}, '
            f'receivers: [{{receiverID: "ssf-service", appID: {app_id}, '
            f'audience: {json.dumps(audience_uri)}, '
            f'deliveryURL: {json.dumps(proxy.origin + "/ssf")}, '
            f'secretRef: "{callback_ref}", revision: 1}}]}}\n')
        fixture.stop()
        fixture.start()
        fixture.request("GET", "/health/ready")

        fixture.authenticator = authenticator
        tokens = fixture.login(
            client_id=client_id, app_id=app_id, scope="ping",
            return_tokens=True, replay_refresh=True, login="user")
        fixture.wait_output(service, b"zumpingd refresh family revoked")

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
        fixture.request("POST", fixture.oauth(app_id, "token"), {
            "grant_type": "refresh_token", "client_id": client_id,
            "refresh_token": pending["refresh_token"]}, form=True)
        fixture.request("POST", fixture.oauth(app_id, "token"), {
            "grant_type": "refresh_token", "client_id": client_id,
            "refresh_token": pending["refresh_token"]}, form=True, status=400)
        # Let the daemon's request scheduler finish the asynchronous SSF
        # outbox insert before the restart; this is a single readiness request,
        # not a delivery poll.
        fixture.request("GET", "/health/ready")
        service, service_log = launch_service(fixture, service_config, service_env)
        fixture.stop()
        fixture.start()
        fixture.request("GET", "/health/ready")
        proxy.backend_port = service_port
        fixture.wait_output(service, b"zumpingd refresh family revoked")
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
        if success and directory.exists():
            shutil.rmtree(directory)


if __name__ == "__main__":
    main()
    print("1..1\nok 1 - SSF refresh-family delivery and restart")
