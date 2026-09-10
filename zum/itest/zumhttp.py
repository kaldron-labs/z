#!/usr/bin/env python3
# (c) Copyright 2026 Huw Rogers
# This code is licensed by the MIT license (see LICENSE for details)

"""PostgreSQL-backed HTTP fixture with a virtual WebAuthn authenticator.

Not a browser test: credentials are generated and signed independently, then
sent through the production ceremony/OAuth/admin endpoints. Never print tokens,
capabilities, process environments, or secret-bearing response bodies.
"""

import base64
import hashlib
import http.client
from http.cookies import SimpleCookie
import json
import os
from pathlib import Path
import re
import secrets
import selectors
import signal
import socket
import subprocess
import tempfile
import time
from urllib.parse import parse_qs, urlencode, urlsplit

from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import ec, utils


def b64(value):
    return base64.urlsafe_b64encode(value).rstrip(b"=").decode()


def unb64(value):
    return base64.urlsafe_b64decode(value + "=" * (-len(value) % 4))


def compact(value):
    return json.dumps(value, separators=(",", ":")).encode()


def cbor(value):
    # Only the definite-length COSE/none-attestation fixture types are needed.
    def head(major, size):
        if size < 24:
            return bytes([major * 32 + size])
        if size < 256:
            return bytes([major * 32 + 24, size])
        return bytes([major * 32 + 25]) + size.to_bytes(2, "big")

    if isinstance(value, int):
        return head(0, value) if value >= 0 else head(1, -1 - value)
    if isinstance(value, bytes):
        return head(2, len(value)) + value
    if isinstance(value, str):
        raw = value.encode()
        return head(3, len(raw)) + raw
    if isinstance(value, dict):
        return head(5, len(value)) + b"".join(
            cbor(k) + cbor(v) for k, v in value.items())
    raise TypeError("unsupported fixture CBOR type")


class Authenticator:
    def __init__(self, origin):
        self.origin = origin
        self.key = ec.generate_private_key(ec.SECP256R1())
        self.credential = secrets.token_bytes(32)
        self.handle = None
        self.counter = 0

    def client_data(self, operation, challenge):
        return compact({"type": operation, "challenge": challenge,
                        "origin": self.origin, "crossOrigin": False})

    def wire(self, response):
        return {"id": b64(self.credential), "rawId": b64(self.credential),
                "type": "public-key", "response": response}

    def register(self, options):
        key = self.key.public_key().public_numbers()
        self.handle = options["user"]["id"]
        cose = cbor({1: 2, 3: -7, -1: 1,
                     -2: key.x.to_bytes(32, "big"),
                     -3: key.y.to_bytes(32, "big")})
        auth = (hashlib.sha256(options["rp"]["id"].encode()).digest() +
                b"\x45" + bytes(4) + bytes(16) +
                len(self.credential).to_bytes(2, "big") + self.credential + cose)
        return self.wire({
            "clientDataJSON": b64(self.client_data(
                "webauthn.create", options["challenge"])),
            "attestationObject": b64(cbor({
                "fmt": "none", "authData": auth, "attStmt": {}}))})

    def assert_(self, options):
        self.counter += 1
        client = self.client_data("webauthn.get", options["challenge"])
        auth = (hashlib.sha256(options["rpId"].encode()).digest() + b"\x05" +
                self.counter.to_bytes(4, "big"))
        signature = self.key.sign(auth + hashlib.sha256(client).digest(),
                                  ec.ECDSA(hashes.SHA256()))
        return self.wire({"clientDataJSON": b64(client),
                          "authenticatorData": b64(auth),
                          "signature": b64(signature), "userHandle": self.handle})


class Fixture:
    def __init__(self, directory):
        self.directory = Path(directory)
        with socket.socket() as listener:
            listener.bind(("127.0.0.1", 0))
            self.port = listener.getsockname()[1]
        self.origin = f"http://localhost:{self.port}"
        self.env = dict(os.environ, ZUM_DB_KEY=base64.b64encode(
            secrets.token_bytes(32)).decode(),
            ZDB_MODULE=os.environ["ZUM_TEST_MODULE"],
            ZDB_CONNECT=os.environ["ZUM_HTTP_CONNECT"])
        self.cookies = SimpleCookie()
        self.process = None
        self.log = None
        self.authenticator = Authenticator(self.origin)

    def start(self):
        self.cookies = SimpleCookie()
        self.log = (self.directory / "server.log").open("ab")
        server = Path(__file__).resolve().parent.parent / "src" / "zumd"
        self.process = subprocess.Popen([
            str(server), "--issuer=" + self.origin, "--admin=http-admin",
            "--bootstrap-output=" + str(self.directory / "enrollment"),
            "--port=" + str(self.port), "--rp-id=localhost"],
            env=self.env, stdout=subprocess.PIPE, stderr=self.log)
        # Read unbuffered pipe events so an adjacent 'active' line cannot hide
        # 'listening' in a userspace read-ahead buffer. No readiness polling.
        deadline = time.monotonic() + 30
        pending = b""
        with selectors.DefaultSelector() as selector:
            selector.register(self.process.stdout, selectors.EVENT_READ)
            while True:
                remaining = deadline - time.monotonic()
                if remaining <= 0 or not selector.select(remaining):
                    raise AssertionError("server listener startup timed out")
                data = os.read(self.process.stdout.fileno(), 4096)
                if not data:
                    raise AssertionError("server exited before listening")
                pending += data
                lines = pending.split(b"\n")
                pending = lines.pop()
                if b"zumd: listening" in lines:
                    return

    def stop(self):
        if self.process is not None:
            process, self.process = self.process, None
            if process.poll() is None:
                process.send_signal(signal.SIGTERM)
            try:
                process.communicate(timeout=30)
            except subprocess.TimeoutExpired:
                process.kill()
                process.communicate()
                raise AssertionError("server did not drain and stop")
            finally:
                self.log.close()
            if process.returncode:
                raise AssertionError("server shutdown failed")

    def request(self, method, path, value=None, *, form=False, token=None,
                headers=None, status=200):
        fields = dict(headers or {})
        if token:
            fields["Authorization"] = "Bearer " + token
        if self.cookies:
            fields["Cookie"] = "; ".join(
                f"{key}={item.value}" for key, item in self.cookies.items())
        body = None
        if value is not None:
            body = urlencode(value).encode() if form else compact(value)
            fields["Content-Type"] = ("application/x-www-form-urlencoded"
                                      if form else "application/json")
        connection = http.client.HTTPConnection("127.0.0.1", self.port, timeout=15)
        try:
            connection.request(method, path, body=body, headers=fields)
            response = connection.getresponse()
            raw = response.read()
            for key, value in response.getheaders():
                if key.lower() == "set-cookie":
                    self.cookies.load(value)
            if response.status != status:
                # Do not include URL query, body, cookies, or credentials.
                error = ""
                try:
                    code = json.loads(raw).get("error")
                    if code in ("invalid_request", "access_denied", "server_error",
                                "invalid_client", "invalid_grant", "invalid_scope"):
                        error = " (" + code + ")"
                except (ValueError, AttributeError):
                    pass
                raise AssertionError(f"{method} {path.split('?')[0]}: "
                                     f"expected {status}, got {response.status}{error}")
            content = response.getheader("Content-Type", "")
            result = json.loads(raw) if content.startswith("application/json") else raw.decode()
            return result, dict((k.lower(), v) for k, v in response.getheaders())
        finally:
            connection.close()

    def state_cycle(self, query, path, token, denied):
        before, _ = self.request("GET", query, token=token)
        before = before["items"][0]
        self.request("PUT", path, {"state": "Suspended"}, token=token, status=428)
        changed, _ = self.request("PUT", path, {"state": "Suspended"}, token=token,
                                  headers={"If-Match": before["etag"]})
        suspended, _ = self.request("GET", query, token=token)
        suspended = suspended["items"][0]
        assert suspended["state"] == "Suspended"
        assert int(suspended["version"]) == int(before["version"]) + 1
        assert int(suspended["authVersion"]) == int(before["authVersion"]) + 1
        assert changed["item"]["etag"] == suspended["etag"]
        denied()
        unchanged, _ = self.request("PUT", path, {"state": "Suspended"}, token=token,
                                    headers={"If-Match": suspended["etag"]})
        assert unchanged["item"]["etag"] == suspended["etag"]
        self.request("PUT", path, {"state": "Active"}, token=token,
                     headers={"If-Match": before["etag"]}, status=412)
        self.request("PUT", path, {"state": "Active"}, token=token,
                     headers={"If-Match": suspended["etag"]})
        active, _ = self.request("GET", query, token=token)
        active = active["items"][0]
        assert active["state"] == "Active"
        assert int(active["version"]) == int(before["version"]) + 2
        assert int(active["authVersion"]) == int(before["authVersion"]) + 2
        return active["authVersion"]

    def enroll(self):
        self.request("GET", "/health/live")
        self.request("GET", "/health/ready", status=503)
        capability_file = self.directory / "enrollment"
        assert capability_file.stat().st_mode & 0o777 == 0o600
        capability = parse_qs(urlsplit(capability_file.read_text().strip()).query)["capability"][0]
        begin, _ = self.request("POST", "/passkey/begin",
                                {"purpose": "bootstrap", "capability": capability})
        registration = self.authenticator.register(begin["options"]["publicKey"])
        self.request("POST", "/passkey/finish?id=" + begin["ceremony"], registration)
        self.request("GET", "/health/ready")

    def login(self, client_id="zum-admin", scope="zum.admin", resource=None,
              return_tokens=False, client_secret=None,
              redirect="http://127.0.0.1:49152/callback"):
        verifier = b64(secrets.token_bytes(32))
        state = b64(secrets.token_bytes(24))
        query = {"response_type": "code", "client_id": client_id,
                 "redirect_uri": redirect, "scope": scope,
                 "resource": resource or self.origin + "/admin", "state": state,
                 "code_challenge": b64(hashlib.sha256(verifier.encode()).digest()),
                 "code_challenge_method": "S256"}
        nonce = b64(secrets.token_bytes(24)) if "openid" in scope.split() else None
        if nonce:
            query["nonce"] = nonce
        page, _ = self.request("GET", "/authorize?" + urlencode(query))
        match = re.search(r"const id='([^']+)',o=(.*?);const d=", page)
        assert match, "authorization did not return a passkey page"
        ceremony = match[1]
        self.request("POST", "/login", {"id": ceremony, "login": "http-admin"}, form=True)
        assertion = self.authenticator.assert_(json.loads(match[2])["publicKey"])
        finish, _ = self.request("POST", "/passkey/finish?id=" + ceremony, assertion)
        if isinstance(finish, str):
            consent = re.search(r'name=id value="([^"]+)"', finish)
            assert consent, "unexpected passkey completion page"
            _, headers = self.request("POST", "/consent",
                                      {"id": consent[1], "decision": "approve"},
                                      form=True, status=302)
            location = headers["location"]
        else:
            location = finish["redirectURI"]
        result = parse_qs(urlsplit(location).query)
        assert result["state"] == [state] and "code" in result
        assert location.startswith(redirect + "?")
        request = {"grant_type": "authorization_code", "client_id": client_id,
                   "redirect_uri": redirect, "code": result["code"][0],
                   "code_verifier": verifier}
        def token_request(value):
            headers = {}
            if client_secret is not None:
                for secret in (None, "incorrect-secret"):
                    invalid_headers = {} if secret is None else {
                        "Authorization": "Basic " + base64.b64encode(
                            (client_id + ":" + secret).encode()).decode()}
                    denied, _ = self.request("POST", "/token", value, form=True,
                                              headers=invalid_headers, status=401)
                    assert denied["error"] == "invalid_client"
                headers["Authorization"] = "Basic " + base64.b64encode(
                    (client_id + ":" + client_secret).encode()).decode()
                if value["grant_type"] == "authorization_code":
                    denied, _ = self.request("POST", "/token",
                                              dict(value, code_verifier=b64(secrets.token_bytes(32))),
                                              form=True, headers=headers, status=400)
                    assert denied["error"] == "invalid_grant"
            return self.request("POST", "/token", value, form=True, headers=headers)

        tokens, _ = token_request(request)
        assert tokens["token_type"].lower() == "bearer" and tokens["access_token"]
        if nonce:
            identity = self.verify_jwt(tokens["id_token"])
            assert identity["iss"] == self.origin and identity["aud"] == client_id
            assert identity["nonce"] == nonce and identity["sub"]
            assert identity["iat"] <= time.time() < identity["exp"]
        replay_headers = {} if client_secret is None else {
            "Authorization": "Basic " + base64.b64encode(
                (client_id + ":" + client_secret).encode()).decode()}
        self.request("POST", "/token", request, form=True, headers=replay_headers, status=400)
        refreshed, _ = token_request({
            "grant_type": "refresh_token", "client_id": client_id,
            "refresh_token": tokens["refresh_token"]})
        assert refreshed["token_type"].lower() == "bearer" and refreshed["access_token"]
        assert refreshed["refresh_token"] != tokens["refresh_token"]
        if nonce:
            identity = self.verify_jwt(refreshed["id_token"])
            assert identity["iss"] == self.origin and identity["aud"] == client_id
            assert identity["nonce"] == nonce and identity["sub"]
        return refreshed if return_tokens else refreshed["access_token"]

    def app_login(self, token, app_id, action_id, audience_uri):
        def create(path, value):
            result, _ = self.request("POST", path, value, token=token,
                                     headers={"Idempotency-Key": secrets.token_hex(16)},
                                     status=201)
            return result["item"]

        prefix = "/admin/apps/" + app_id
        users, _ = self.request("GET", "/admin/users?name=http-admin&source=Local", token=token)
        assert len(users["items"]) == 1
        user_id = users["items"][0]["id"]
        audiences, _ = self.request("GET", "/admin/audiences?" + urlencode({"uri": audience_uri}),
                                    token=token)
        assert len(audiences["items"]) == 1
        audience_id = audiences["items"][0]["id"]
        role = create(prefix + "/roles", {"name": "ping", "label": "Ping"})
        self.request("PUT", prefix + "/roles/" + role["id"] + "/actions",
                     {"actionIDs": [action_id]}, token=token, headers={"If-Match": role["etag"]})
        scope = create(prefix + "/scopes", {"name": "ping", "audienceID": audience_id})
        self.request("PUT", prefix + "/scopes/" + scope["id"] + "/roles",
                     {"roleIDs": [role["id"]]}, token=token, headers={"If-Match": scope["etag"]})
        membership = create(prefix + "/memberships", {"userID": user_id})
        self.request("PUT", prefix + "/memberships/" + user_id + "/roles",
                     {"roleIDs": [role["id"]]}, token=token,
                     headers={"If-Match": membership["etag"]})
        client = create("/admin/clients", {
            "appID": app_id, "label": "Independent native client", "type": "native",
            "redirectURIs": ["http://127.0.0.1:49152/callback"],
            "grants": 5, "refreshAllowed": True, "identityScopes": ["openid"]})
        self.request("PUT", prefix + "/client-access/" + client["id"], {
            "audienceIDs": [audience_id], "scopeIDs": [scope["id"]], "roleIDs": []},
            token=token, headers={"If-None-Match": "*"}, status=201)
        app, _ = self.request("GET", "/admin/apps?id=" + app_id, token=token)
        # The app's earlier mutations made its authority version independent.
        assert int(app["items"][0]["authVersion"]) > 1
        self.cookies = SimpleCookie()
        tokens = self.login(client["id"], "openid ping", audience_uri, return_tokens=True)
        self.verify_access(tokens["access_token"], client["id"], app_id, audience_uri, ["ping"])
        web = create("/admin/clients", {
            "appID": app_id, "label": "Independent web client", "type": "confidential",
            "redirectURIs": ["https://orders.example/callback"],
            "grants": 5, "refreshAllowed": True, "identityScopes": ["openid"]})
        registered, _ = self.request("GET", "/admin/clients?id=" + web["id"], token=token)
        assert len(registered["items"]) == 1
        assert "client_secret" not in registered["items"][0]
        assert "secretDigest" not in registered["items"][0]
        self.request("PUT", prefix + "/client-access/" + web["id"], {
            "audienceIDs": [audience_id], "scopeIDs": [scope["id"]], "roleIDs": []},
            token=token, headers={"If-None-Match": "*"}, status=201)
        self.cookies = SimpleCookie()
        web_tokens = self.login(web["id"], "openid ping", audience_uri,
                                return_tokens=True, client_secret=web["client_secret"],
                                redirect="https://orders.example/callback")
        self.verify_access(web_tokens["access_token"], web["id"], app_id, audience_uri, ["ping"])
        membership_path = prefix + "/memberships/" + user_id
        members, _ = self.request("GET", prefix + "/memberships?userID=" + user_id, token=token)
        removed, _ = self.request("PUT", membership_path + "/roles", {"roleIDs": []},
                                  token=token, headers={"If-Match": members["items"][0]["etag"]})

        def refresh(current, status=200):
            result, _ = self.request("POST", "/token", {
                "grant_type": "refresh_token", "client_id": client["id"],
                "refresh_token": current["refresh_token"]}, form=True, status=status)
            return result

        narrowed = refresh(tokens)
        self.verify_access(narrowed["access_token"], client["id"], app_id, audience_uri, [])
        restored, _ = self.request("PUT", membership_path + "/roles",
                                   {"roleIDs": [role["id"]]}, token=token,
                                   headers={"If-Match": removed["item"]["etag"]})
        still_narrowed = refresh(narrowed)
        self.verify_access(still_narrowed["access_token"], client["id"], app_id, audience_uri, [])
        self.request("PUT", membership_path + "/state", {"state": "Suspended"},
                     token=token, headers={"If-Match": restored["item"]["etag"]})
        denied = refresh(still_narrowed, 400)
        assert denied["error"] == "invalid_grant"
        self.stop()
        self.start()
        self.request("GET", "/health/ready")
        denied = refresh(still_narrowed, 400)
        assert denied["error"] == "invalid_grant"

    def verify_access(self, access_token, client_id, app_id, audience_uri, actions):
        claims = self.verify_jwt(access_token)
        assert claims["iss"] == self.origin and claims["aud"] == audience_uri
        assert claims["zum_app_id"] == app_id and claims["client_id"] == client_id
        assert claims["scope"] == "openid ping" and claims["actions"] == actions
        assert claims["iat"] <= time.time() < claims["exp"]

    def verify_jwt(self, access_token):
        encoded_header, encoded_claims, encoded_signature = access_token.split(".")
        header = json.loads(unb64(encoded_header))
        claims = json.loads(unb64(encoded_claims))
        jwks, _ = self.request("GET", "/jwks")
        keys = [key for key in jwks["keys"] if key["kid"] == header["kid"]]
        assert header["alg"] == "ES256" and len(keys) == 1
        key = keys[0]
        assert key["kty"] == "EC" and key["crv"] == "P-256"
        public_key = ec.EllipticCurvePublicNumbers(
            int.from_bytes(unb64(key["x"])), int.from_bytes(unb64(key["y"])),
            ec.SECP256R1()).public_key()
        signature = unb64(encoded_signature)
        assert len(signature) == 64
        public_key.verify(utils.encode_dss_signature(
            int.from_bytes(signature[:32]), int.from_bytes(signature[32:])),
            (encoded_header + "." + encoded_claims).encode(), ec.ECDSA(hashes.SHA256()))
        return claims


def main():
    for key in ("ZUM_TEST_MODULE", "ZUM_HTTP_CONNECT"):
        if not os.environ.get(key):
            raise AssertionError("set " + key + " for a fresh PostgreSQL HTTP fixture")
    with tempfile.TemporaryDirectory(prefix="zum-http-") as directory:
        fixture = Fixture(directory)
        try:
            fixture.start()
            fixture.enroll()
            token = fixture.login()
            fixture.request("GET", "/admin/apps", status=401)
            initial, _ = fixture.request("GET", "/admin/apps", token=token)
            assert len(initial["items"]) == 1
            app_input = {
                "name": "http-orders", "label": "HTTP Orders",
                "integration": "nativeService", "audienceURI": "https://orders.example/api"}
            app_headers = {"Idempotency-Key": secrets.token_hex(16)}
            app, _ = fixture.request("POST", "/admin/apps", app_input,
                                     token=token, headers=app_headers, status=201)
            assert app["item"]["client_secret"] and app["item"]["client_id"]
            basic = base64.b64encode((app["item"]["client_id"] + ":" +
                                      app["item"]["client_secret"]).encode()).decode()
            workload, _ = fixture.request("POST", "/token", {
                "grant_type": "client_credentials", "scope": "zum.service"},
                form=True, headers={"Authorization": "Basic " + basic})
            assert workload["access_token"] and "refresh_token" not in workload
            fixture.request("GET", "/admin/apps", token=workload["access_token"], status=403)
            app_id = app["item"]["appID"]
            replay, _ = fixture.request("POST", "/admin/apps", app_input,
                                        token=token, headers=app_headers)
            assert replay["status"] == "complete" and app_id in replay["resultIDs"]
            assert "client_secret" not in replay and "item" not in replay
            fixture.request("POST", "/admin/apps", dict(app_input, label="Changed"),
                            token=token, headers=app_headers, status=409)
            current, _ = fixture.request("GET", "/admin/apps?id=" + app_id, token=token)
            assert len(current["items"]) == 1 and current["items"][0]["id"] == app_id
            etag = current["items"][0]["etag"]
            update_path = "/admin/apps/" + app_id
            update = {"label": "Updated HTTP Orders"}
            fixture.request("PATCH", update_path, update, token=token, status=428)
            updated, _ = fixture.request("PATCH", update_path, update, token=token,
                                         headers={"If-Match": etag})
            assert updated["item"]["etag"] != etag
            fixture.request("PATCH", update_path, {"label": "Stale overwrite"},
                            token=token, headers={"If-Match": etag}, status=412)
            fixture.request("GET", "/admin/apps?unsupported=x", token=token, status=400)
            path = "/admin/apps/" + app_id + "/actions"
            action, _ = fixture.request("POST", path, {"name": "ping", "label": "Ping"},
                                        token=token, headers={"Idempotency-Key": secrets.token_hex(16)},
                                        status=201)
            actions, _ = fixture.request("GET", path, token=token)
            assert len(actions["items"]) == 1 and actions["items"][0]["name"] == "ping"
            action_id = action["item"]["id"]
            assert type(action_id) is int and actions["items"][0]["id"] == action_id
            def workload_token(status=200):
                return fixture.request("POST", "/token", {
                    "grant_type": "client_credentials", "scope": "zum.service"},
                    form=True, headers={"Authorization": "Basic " + basic}, status=status)

            app_query = "/admin/apps?id=" + app_id
            app_version = fixture.state_cycle(app_query, update_path + "/state", token,
                                              lambda: workload_token(401))
            workload_token()
            core_id = initial["items"][0]["id"]
            access_path = "/admin/apps/" + core_id + "/client-access"
            access_query = access_path + "?" + urlencode({"clientID": app["item"]["client_id"]})
            access, _ = fixture.request("GET", access_query, token=token)
            access = access["items"][0]
            access_row_path = access_path + "/" + app["item"]["client_id"]
            fixture.request("PUT", access_row_path,
                            {field: access[field] for field in
                             ("audienceIDs", "scopeIDs", "roleIDs")}, token=token,
                            headers={"If-Match": access["etag"]})
            workload_token()
            client_query = "/admin/clients?" + urlencode({"id": app["item"]["client_id"]})
            client, _ = fixture.request("GET", client_query, token=token)
            fixture.request("PATCH", "/admin/clients/" + app["item"]["client_id"],
                            {"label": "Updated service client"}, token=token,
                            headers={"If-Match": client["items"][0]["etag"]})
            access_version = fixture.state_cycle(
                access_query, access_row_path + "/state",
                token, lambda: workload_token(400))
            workload_token()
            fixture.stop()
            fixture.start()
            fixture.request("GET", "/health/ready")
            token = fixture.login()
            actions, _ = fixture.request("GET", path, token=token)
            assert len(actions["items"]) == 1 and actions["items"][0]["id"] == action_id
            apps, _ = fixture.request("GET", "/admin/apps", token=token)
            assert len(apps["items"]) == 2
            current, _ = fixture.request("GET", "/admin/apps?id=" + app_id, token=token)
            assert current["items"][0]["label"] == update["label"]
            assert current["items"][0]["authVersion"] == app_version
            access, _ = fixture.request("GET", access_query, token=token)
            assert access["items"][0]["authVersion"] == access_version
            client, _ = fixture.request("GET", client_query, token=token)
            assert client["items"][0]["label"] == "Updated service client"
            workload_token()
            replay, _ = fixture.request("POST", "/admin/apps", app_input,
                                        token=token, headers=app_headers)
            assert replay["status"] == "complete" and app_id in replay["resultIDs"]
            assert "client_secret" not in replay and "item" not in replay
            fixture.app_login(token, app_id, action_id, app_input["audienceURI"])
        finally:
            fixture.stop()


if __name__ == "__main__":
    main()
