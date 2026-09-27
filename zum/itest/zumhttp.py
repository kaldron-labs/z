#!/usr/bin/env python3
# (c) Copyright 2026 Huw Rogers
# This code is licensed by the MIT license (see LICENSE for details)

"""SQLite-backed HTTP fixture with a virtual WebAuthn authenticator.

Not a browser test: credentials are generated and signed independently, then
sent through the production ceremony/OAuth/admin endpoints. Never print tokens,
capabilities, process environments, or secret-bearing response bodies.
"""

import base64
import copy
import hashlib
import http.client
from http.cookies import SimpleCookie
import json
import os
from pathlib import Path
import re
import secrets
import selectors
import shutil
import signal
import socket
import subprocess
import tempfile
import time
from urllib.parse import parse_qs, urlencode, urlsplit

from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import ec, utils
from cryptography.hazmat.primitives.ciphers.aead import AESGCM


def b64(value):
    return base64.urlsafe_b64encode(value).rstrip(b"=").decode()


def sign_catalog(contents, revision):
    digest = b64(hashlib.sha256(json.dumps(
        contents, sort_keys=True, ensure_ascii=True,
        separators=(",", ":")).encode()).digest())
    return {"catalog": contents, "revision": str(revision), "digest": digest}


def catalog_manifest(revision, empty=False):
    contents = {
        "actions": [{"name": "catalog.probe", "label": "Catalog é probe / \"\\\n\u0001 🎵"}],
        "roles": [{"name": "catalog.probe", "actions": ["catalog.probe"]}]}
    if empty:
        contents = {key: [] for key in contents}
    return sign_catalog(contents, revision)


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
        self.env = dict(os.environ, ZUM_DB_KEY=os.environ.get(
            "ZUM_HTTP_DB_KEY", base64.b64encode(secrets.token_bytes(32)).decode()),
            ZUMD_HOME=str(self.directory / "vault-home"))
        self.cookies = SimpleCookie()
        self.process = None
        self.log = None
        self.authenticator = Authenticator(self.origin)
        self.operations = []
        self.calls = {}
        self.starts = 0
        self.node_config = None
        self.idp = None
        self.ca_path = None
        self.core_app_id = None

    def issuer(self, app_id=None):
        app_id = app_id or self.core_app_id
        assert app_id
        return self.origin + "/oauth2/" + str(app_id)

    def oauth(self, app_id, endpoint):
        return "/oauth2/" + str(app_id) + "/v1/" + endpoint

    def start(self):
        self.cookies = SimpleCookie()
        self.log = (self.directory / "server.log").open("ab")
        server = Path(__file__).resolve().parent.parent / "src" / "zumd"
        node_config = (["--config=" + str(self.node_config or Path(__file__).with_name("zumd.cf"))]
                       if self.starts or self.node_config else [])
        self.process = subprocess.Popen([
            str(server), "--issuer=" + self.origin, "--admin=http-admin",
            "--vault-store=file",
            "--bootstrap-output=" + str(self.directory / "enrollment"),
            "--port=" + str(self.port), "--rp-id=localhost", *node_config],
            env=self.env, stdout=subprocess.PIPE, stderr=self.log)
        self.starts += 1
        # Read unbuffered pipe events so an adjacent 'active' line cannot hide
        # 'listening' in a userspace read-ahead buffer. No readiness polling.
        deadline = time.monotonic() + 30
        pending = b""
        started = set()
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
                started.update(lines)
                if {b"zumd: listening", b"zumd: active"} <= started:
                    return

    def stop(self):
        if self.process is not None:
            process, self.process = self.process, None
            if process.poll() is None:
                process.send_signal(signal.SIGTERM)
            try:
                process.communicate(timeout=30)
            except subprocess.TimeoutExpired:
                # ZmTrap emits a native symbol-only backtrace on SIGABRT. Retain
                # it in the private log before killing an already failed drain.
                process.send_signal(signal.SIGABRT)
                try:
                    process.communicate(timeout=2)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.communicate()
                raise AssertionError("server did not drain and stop")
            finally:
                self.log.close()
            if process.returncode:
                raise AssertionError("server shutdown failed")

    @staticmethod
    def wait_output(process, prefix, timeout=30):
        pending = b""
        deadline = time.monotonic() + timeout
        with selectors.DefaultSelector() as selector:
            selector.register(process.stdout, selectors.EVENT_READ)
            while True:
                remaining = deadline - time.monotonic()
                if remaining <= 0 or not selector.select(remaining):
                    raise AssertionError("process output event timed out")
                data = os.read(process.stdout.fileno(), 4096)
                if not data:
                    raise AssertionError("process exited before output event")
                pending += data
                lines = pending.split(b"\n")
                pending = lines.pop()
                if any(line.startswith(prefix) for line in lines):
                    return

    def wrong_key(self, key=None):
        assert self.process is None
        server = Path(__file__).resolve().parent.parent / "src" / "zumd"
        with (self.directory / "wrong-key.log").open("ab") as log:
            process = subprocess.Popen([
                str(server), "--issuer=" + self.origin, "--admin=http-admin",
                "--vault-store=file",
                "--bootstrap-output=" + str(self.directory / "enrollment"),
                "--port=" + str(self.port), "--rp-id=localhost"],
                env=dict(self.env, ZUM_DB_KEY=key or base64.b64encode(
                    secrets.token_bytes(32)).decode()),
                stdout=subprocess.PIPE, stderr=log)
            try:
                output, _ = process.communicate(timeout=30)
            except subprocess.TimeoutExpired:
                process.kill()
                process.communicate()
                raise AssertionError("wrong-key startup did not fail promptly")
            assert process.returncode != 0
            assert b"zumd: active" not in output

    def rekey(self):
        assert self.process is None
        server = Path(__file__).resolve().parent.parent / "src" / "zumd"
        old_key = self.env["ZUM_DB_KEY"]
        new_key = base64.b64encode(secrets.token_bytes(32)).decode()
        env = dict(self.env, ZUM_DB_KEY=new_key)

        def sql(statement):
            result = subprocess.run([
                "sqlite3", "-batch", "-noheader", self.env["ZDB_CONNECT"],
                statement],
                env=self.env, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                timeout=30)
            assert result.returncode == 0
            return result.stdout

        def ciphertext():
            return sql('SELECT id, lower(hex(private_material)) '
                       'FROM "a_zum.sign_key" ORDER BY id')

        def rotate(environment, success):
            with (self.directory / "rekey.log").open("ab") as log:
                result = subprocess.run([
                    str(server), "--rekey", "--issuer=" + self.origin,
                    "--vault-store=file"],
                    env=environment, stdout=subprocess.PIPE, stderr=log, timeout=30)
            assert (result.returncode == 0) == success
            assert (b"secret-key rotation complete" in result.stdout) == success
            assert b"zumd: listening" not in result.stdout

        def fingerprints(snapshot, key, table=b"zum.sign_key",
                         field=b"privateMaterial", hex_ids=False):
            cipher = AESGCM(base64.b64decode(key))
            result = {}
            issuers = {}
            if table == b"zum.sign_key":
                issuers = dict(line.split(b"|", 1) for line in sql(
                    'SELECT id, issuer FROM "a_zum.sign_key"').splitlines())
            for line in snapshot.splitlines():
                key_id, value = line.split(b"|", 1)
                if not value:
                    continue
                record_id = key_id
                if hex_ids:
                    record_id = str(int.from_bytes(
                        bytes.fromhex(key_id.decode()), "big")).encode()
                envelope = bytes.fromhex(value.decode())
                assert envelope[:6] == b"\x01\x01\x00\x00\x00\x01"
                issuer = issuers.get(key_id, self.origin.encode())
                aad = (issuer + b"\0" + table + b"\0" + record_id +
                       b"\0" + field)
                result[key_id] = hashlib.sha256(cipher.decrypt(
                    envelope[6:18], envelope[18:], aad)).digest()
            return result

        before = ciphertext()
        original_fingerprints = fingerprints(before, old_key)
        provider_query = ('SELECT lower(hex(id)), lower(hex(client_secret)) '
                          'FROM "a_zum.provider" ORDER BY id')
        provider_before = sql(provider_query)
        provider_fingerprints = fingerprints(
            provider_before, old_key, b"provider", b"clientSecret", True)
        assert provider_fingerprints
        backup = self.directory / "before-rekey.dump"
        result = subprocess.run([
            "sqlite3", "-batch", self.env["ZDB_CONNECT"],
            ".backup '" + str(backup) + "'"], env=self.env,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30)
        assert result.returncode == 0, "pre-rotation SQLite backup failed"
        binding_query = 'SELECT lower(hex(key_check)) FROM "a_zum.issuer"'
        old_binding = sql(binding_query)
        # Corrupt a late signing envelope in the stopped throwaway DB. Earlier
        # fields can rotate, but the bad tag must prevent the key-binding switch.
        rows = [line.split(b"|", 1) for line in before.splitlines()]
        rows = [(key.decode(), value.decode()) for key, value in rows if value]
        assert len(rows) >= 2
        key_id, saved = rows[-1]
        quoted_id = "'" + key_id.replace("'", "''") + "'"

        def replace(value):
            sql('UPDATE "a_zum.sign_key" SET private_material = X\'' +
                value + "' WHERE id = " + quoted_id)

        corrupt = saved[:-2] + f"{int(saved[-2:], 16) ^ 1:02x}"
        replace(corrupt)
        try:
            rotate(env, False)
            assert sql('SELECT length(pending_key_check) '
                       'FROM "a_zum.issuer"').strip() == b"32"
            assert sql(binding_query) == old_binding
            partial = ciphertext()
            # At least one undamaged signer was rewrapped before the failure.
            partial_rows = dict(line.split(b"|", 1) for line in partial.splitlines())
            assert any(key != key_id and partial_rows[key.encode()] != value.encode()
                       for key, value in rows)
            self.wrong_key(old_key)
            other = dict(env, ZUM_DB_KEY=base64.b64encode(
                secrets.token_bytes(32)).decode())
            rotate(other, False)
            assert ciphertext() == partial
            assert sql(binding_query) == old_binding
        finally:
            replace(saved)
        vault_file = Path(self.env["ZUMD_HOME"]) / "vault" / "secrets.json"
        old_vault = vault_file.read_bytes()
        rotate(env, True)
        after = ciphertext()
        assert after != before
        # Model interruption after durable DB commit but before Vault publish.
        # This is a disposable fixture aggregate, never a deployment store.
        vault_file.write_bytes(old_vault)
        rotate(env, True)
        assert ciphertext() == after
        # After recovery the Vault holds the new key; repeating it fails.
        rotate(env, False)
        assert ciphertext() == after
        assert sql('SELECT coalesce(length(pending_key_check), 0) '
                   'FROM "a_zum.issuer"').strip() == b"0"
        assert sql(binding_query) != old_binding
        assert fingerprints(after, new_key) == original_fingerprints
        provider_after = sql(provider_query)
        assert provider_after != provider_before
        assert fingerprints(provider_after, new_key, b"provider", b"clientSecret", True) == \
            provider_fingerprints
        # The retained pre-rotation ciphertext still needs its matching old key.
        assert fingerprints(before, old_key) == original_fingerprints
        self.env["ZUM_DB_KEY"] = new_key
        self.wrong_key(old_key)
        try:
            self.start()
            self.request("GET", "/health/ready")
            token = self.login()
            self.request("GET", "/admin/apps", token=token)
        finally:
            self.stop()

        # Restore the actual pre-rotation SQLite backup into a different file,
        # never over the source. Keep it for diagnosis if the fixture fails.
        backup_db = self.directory / "before-rekey-restored.db"
        shutil.copyfile(backup, backup_db)
        source_env = self.env
        self.env = dict(source_env,
                        ZDB_CONNECT=str(backup_db),
                        ZUM_DB_KEY=old_key,
                        ZUMD_HOME=str(self.directory / "backup-vault-home"))
        restored = False
        try:
            assert ciphertext() == before
            assert sql(provider_query) == provider_before
            self.wrong_key(new_key)
            self.start()
            self.request("GET", "/health/ready")
            token = self.login()
            self.request("GET", "/admin/apps", token=token)
            restored = True
        finally:
            try:
                self.stop()
            finally:
                self.env = source_env
        if restored:
            backup_db.unlink()

    def connection(self):
        return http.client.HTTPConnection("127.0.0.1", self.port, timeout=15)

    def wrong_schema(self):
        assert self.process is None

        def sql(statement):
            result = subprocess.run(["sqlite3", "-batch", "-noheader",
                                     self.env["ZDB_CONNECT"], statement],
                env=self.env, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            assert result.returncode == 0, "SQLite schema-test query failed"
            return result.stdout.decode().strip()

        # Deliberately corrupt only the version marker in this stopped,
        # disposable store. This is a rejection test, not a migration.
        version = int(sql('SELECT schema_version FROM "a_zum.issuer"'))
        counts = ('SELECT (SELECT count(*) FROM "a_zum.user"), '
                  '(SELECT count(*) FROM "a_zum.app")')
        before = sql(counts)
        server = Path(__file__).resolve().parent.parent / "src" / "zumd"
        log_path = self.directory / "wrong-schema.log"
        sql('UPDATE "a_zum.issuer" SET schema_version = 0')
        try:
            with log_path.open("ab") as log:
                process = subprocess.Popen([str(server), "--issuer=" + self.origin,
                    "--vault-store=file",
                    "--admin=http-admin", "--bootstrap-output=" + str(self.directory / "enrollment"),
                    "--port=" + str(self.port), "--rp-id=localhost"],
                    env=self.env, stdout=subprocess.PIPE, stderr=log)
                try:
                    output, _ = process.communicate(timeout=30)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.communicate()
                    raise AssertionError("unsupported-schema startup did not fail promptly")
            assert process.returncode != 0 and b"zumd: active" not in output
            assert "unsupported database schema" in log_path.read_text()
            assert sql('SELECT schema_version FROM "a_zum.issuer"') == "0"
            assert sql(counts) == before
        finally:
            sql('UPDATE "a_zum.issuer" SET schema_version = ' + str(version))

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
        connection = self.connection()
        try:
            connection.request(method, path, body=body, headers=fields)
            response = connection.getresponse()
            raw = response.read()
            # Record only method, path and status, never query strings or bodies.
            self.calls.setdefault((method, path.split("?")[0]), set()).add(response.status)
            for key, value in response.getheaders():
                if key.lower() == "set-cookie":
                    self.cookies.load(value)
            if response.status not in ((status,) if isinstance(status, int) else status):
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

    def app_disable(self, token, app_id):
        query = "/admin/apps?id=" + app_id
        before = self.request("GET", query, token=token)[0]["items"][0]
        changed, _ = self.request(
            "PUT", "/admin/apps/" + app_id + "/state",
            {"state": "Disabled"}, token=token,
            headers={"If-Match": before["etag"]})
        assert changed["item"]["etag"] != before["etag"]
        disabled = self.request("GET", query, token=token)[0]["items"][0]
        assert disabled["state"] == "Disabled"
        self.request("GET", "/.well-known/"
                     "oauth-authorization-server/oauth2/" + app_id,
                     status=500)
        self.request("GET", self.oauth(app_id, "keys"), status=500)

    def management_matrix(self, token, workload):
        catalog, _ = self.request("GET", "/admin/operations?limit=1000", token=token)
        operations = catalog["items"]
        self.operations = operations
        assert len(operations) == 60 and "nextCursor" not in catalog
        assert len({item["id"] for item in operations}) == len(operations)
        assert len({item["name"] for item in operations}) == len(operations)
        issuer, _ = self.request("GET", "/admin/issuer", token=token)
        read_key = secrets.token_hex(16)
        for _ in range(2):
            replay, _ = self.request("GET", "/admin/issuer", token=token,
                                     headers={"Idempotency-Key": read_key})
            assert replay["items"] == issuer["items"]
        read_request, _ = self.request("GET", "/admin/operations?" + urlencode({
            "operation": "issuerQuery", "idempotencyKey": read_key}), token=token)
        assert read_request["items"] == []
        core_id = issuer["items"][0]["coreAppID"]
        actions, _ = self.request("GET", "/admin/apps/" + core_id + "/actions?limit=1000",
                                  token=token)
        by_id = {item["id"]: item for item in actions["items"]}
        workload_actions = self.verify_jwt(workload)["actions"]
        routes = {}
        for operation in operations:
            assert operation["action"] == "Zum." + operation["name"]
            assert by_id[operation["id"]]["name"] == operation["action"]
            path = re.sub(r"\{[^}]+\}", "1", operation["path"])
            method = operation["method"]
            routes.setdefault(path, set()).add(method)
            body = None if method in ("GET", "DELETE") else {}
            self.request(method, path, body, status=401)
            self.request(method, path, body, token="invalid", status=401)
            # The enrolled service client receives operationQuery through its
            # AdminAccess row, so this management capability is not emitted
            # in the resource-catalog actions on its workload token.
            if (operation["action"] not in workload_actions and
                    operation["name"] != "operationQuery"):
                self.request(method, path, body, token=workload, status=403)
        for path, allowed in routes.items():
            method = next(item for item in ("GET", "POST", "PUT", "PATCH", "DELETE")
                          if item not in allowed)
            body = None if method in ("GET", "DELETE") else {}
            _, headers = self.request(method, path, body, status=405)
            assert {item.strip() for item in headers["allow"].split(",")} == allowed

    def report_coverage(self):
        observed = {}
        for operation in self.operations:
            pattern = "".join("[^/]+" if part.startswith("{") else re.escape(part)
                              for part in re.split(r"(\{[^}]+\})", operation["path"]))
            observed[operation["name"]] = set().union(*(
                statuses for (method, path), statuses in self.calls.items()
                if method == operation["method"] and re.fullmatch(pattern, path)))
        missing = sorted(name for name, statuses in observed.items()
                         if not any(200 <= status < 300 for status in statuses))
        print(f"# administrative operations with observed success: "
              f"{len(observed) - len(missing)}/{len(observed)}")
        print("# administrative operations without success coverage: " + ", ".join(missing))
        assert not missing, "administrative success coverage is incomplete: " + \
            ", ".join(missing)
        for status in (401, 403, 400, 412, 503):
            absent = sorted(name for name, statuses in observed.items() if status not in statuses)
            print(f"# administrative operations with observed HTTP{status}: "
                  f"{len(observed) - len(absent)}/{len(observed)}")
            if status in (401, 403):
                assert not absent, f"administrative HTTP{status} coverage missing: " + ", ".join(absent)
        # These counts locate missing scenarios; they do not prove input,
        # cross-app authority, durability or redaction invariants for a route.

    def administrative_queries(self, token, app_id):
        hidden = {"owner", "privateMaterial", "providerRef", "publicKey",
                  "digest", "spent", "challenge", "bindingDigest", "pkceChallenge",
                  "oauthState", "refreshToken", "secret", "secretDigest"}
        for collection in ("credentials", "auth-policies", "identities", "evidence",
                           "sessions", "consents", "grants", "signing-keys"):
            path = "/admin/" + collection
            all_rows = self.request("GET", path + "?limit=1000", token=token)[0]
            assert "nextCursor" not in all_rows
            if collection in ("identities", "evidence"):
                assert all_rows["items"] == []  # This fixture uses local identities only.
            else:
                assert all_rows["items"], collection
            assert all(not hidden.intersection(row) for row in all_rows["items"]), collection
            assert token.encode() not in compact(all_rows)
            self.request("GET", path + "?unsupported=x", token=token, status=400)
            pages, cursor = [], None
            while True:
                query = {"limit": 1}
                if cursor:
                    query["cursor"] = cursor
                page = self.request("GET", path + "?" + urlencode(query), token=token)[0]
                pages.extend(page["items"])
                assert len(pages) <= len(all_rows["items"]), collection
                cursor = page.get("nextCursor")
                if not cursor:
                    break
            assert pages == all_rows["items"], collection
        policy = self.request("GET", "/admin/auth-policies?appID=" + app_id, token=token)[0]
        assert len(policy["items"]) == 1 and policy["items"][0]["appID"] == app_id
        self.request("GET", "/admin/auth-policies?appID=invalid", token=token, status=400)

    def definition_lifecycle(self, token, app_id):
        def create(path, value):
            result, _ = self.request("POST", path, value, token=token,
                                     headers={"Idempotency-Key": secrets.token_hex(16)}, status=201)
            return result["item"]

        def edit(query, path, method, value):
            before = self.request("GET", query, token=token)[0]["items"][0]
            self.request(method, path, value, token=token, status=428)
            self.request(method, path, value, token=token,
                         headers={"If-Match": '"stale"'}, status=412)
            changed, _ = self.request(method, path, value, token=token,
                                      headers={"If-Match": before["etag"]})
            after = self.request("GET", query, token=token)[0]["items"][0]
            assert after["etag"] == changed["item"]["etag"] and after["etag"] != before["etag"]
            assert int(after["version"]) == int(before["version"]) + 1
            for key, expected in value.items():
                assert after[key] == expected
            self.request(method, path, value, token=token,
                         headers={"If-Match": before["etag"]}, status=412)
            assert self.request("GET", query, token=token)[0]["items"][0] == after

        prefix = "/admin/apps/" + app_id
        policy_query = "/admin/auth-policies?appID=" + app_id
        policy = self.request("GET", policy_query, token=token)[0]["items"][0]
        policy_fields = ("providerID", "localFirst", "eligibilityMode", "eligibilityClaim",
                         "eligibilityValues", "assignmentMaxAge", "sessionIdle",
                         "sessionAbsolute", "tokenLifetime", "consentPolicy", "state")
        replacement = {field: policy[field] for field in policy_fields}
        replacement["tokenLifetime"] = policy["tokenLifetime"] - 1
        edit(policy_query, prefix + "/auth-policy", "PUT", replacement)
        mappings_query = prefix + "/role-mappings?limit=1000"
        mappings = self.request("GET", mappings_query, token=token)[0]["items"]
        assert mappings
        mapping = mappings[0]
        mapping_path = (prefix + "/role-mappings/" + mapping["providerID"] + "/" +
                        b64(mapping["value"].encode()))
        self.request("DELETE", mapping_path, token=token, status=428)
        self.request("DELETE", mapping_path, token=token,
                     headers={"If-Match": '"stale"'}, status=412)
        self.request("DELETE", mapping_path, token=token,
                     headers={"If-Match": mapping["etag"]})
        assert self.request("GET", mappings_query, token=token)[0]["items"] == mappings[1:]
        role = create(prefix + "/roles", {"name": "lifecycle", "label": "Lifecycle"})
        role_query = prefix + "/roles?id=" + role["id"]
        role_path = prefix + "/roles/" + role["id"]
        edit(role_query, role_path, "PATCH", {"label": "Updated lifecycle"})
        edit(role_query, role_path + "/state", "PUT", {"state": "Disabled"})
        client = create("/admin/clients", {
            "appID": app_id, "profile": "native", "label": "Lifecycle",
            "redirectURIs": ["http://127.0.0.1:49152/callback"], "grants": 1})
        edit("/admin/clients?id=" + client["id"],
             "/admin/clients/" + client["id"] + "/state", "PUT", {"state": "Disabled"})
        provider_secret = secrets.token_urlsafe(32)
        provider = create("/admin/providers", {
            "name": "lifecycle", "issuer": "https://lifecycle.example", "clientID": "before",
            "clientSecret": provider_secret,
            "scopes": ["openid", "roles"], "roleClaim": "roles", "claimSource": "IDToken"})
        provider_query = "/admin/providers?id=" + provider["id"]
        provider_path = "/admin/providers/" + provider["id"]
        assert provider_secret not in json.dumps(provider)
        assert provider_secret not in json.dumps(self.request(
            "GET", provider_query, token=token)[0])
        edit(provider_query, provider_path, "PATCH", {"clientID": "after"})
        edit(provider_query, provider_path + "/state", "PUT", {"state": "Disabled"})
        invited = create("/admin/users", {"name": "lifecycle-user"})
        assert invited["enrollmentURL"]
        user = self.request("GET", "/admin/users?name=user&source=Local", token=token)[0]["items"][0]
        user_query = "/admin/users?id=" + user["id"]
        user_path = "/admin/users/" + user["id"]
        edit(user_query, user_path, "PATCH", {"profile": "Ping user", "email": "user@example.com"})
        credentials = self.request("GET", "/admin/credentials?userID=" + user["id"], token=token)[0]["items"]
        assert len(credentials) == 1
        credential_id = credentials[0]["id"]
        credential_query = "/admin/credentials?id=" + credential_id
        credential_path = "/admin/credentials/" + credential_id
        assert self.request("GET", credential_query, token=token)[0]["items"] == credentials
        limited = self.request("GET", "/admin/credentials?" + urlencode({
            "userID": user["id"], "limit": 1}), token=token)[0]
        assert limited["items"] == credentials and "nextCursor" not in limited
        for query in ({"userID": "invalid"}, {"id": "!"},
                      {"id": credential_id, "userID": user["id"]}):
            self.request("GET", "/admin/credentials?" + urlencode(query), token=token, status=400)
        assert self.request("GET", "/admin/credentials?userID=" + invited["id"], token=token)[0]["items"] == []
        edit(credential_query, credential_path, "PATCH", {"label": "Updated passkey"})
        edit(credential_query, credential_path + "/state", "PUT", {"state": "Disabled"})
        before = self.request("GET", user_query, token=token)[0]["items"][0]
        recovery_headers = {"Idempotency-Key": secrets.token_hex(16)}
        self.request("POST", user_path + "/recover", {}, token=token,
                     headers=recovery_headers, status=428)
        self.request("POST", user_path + "/recover", {}, token=token,
                     headers=dict(recovery_headers, **{"If-Match": '"stale"'}), status=412)
        recovery_headers["If-Match"] = before["etag"]
        recovery, _ = self.request("POST", user_path + "/recover", {}, token=token,
                                    headers=recovery_headers)
        assert recovery["item"]["id"] == user["id"] and recovery["item"]["recoveryURL"]
        after = self.request("GET", user_query, token=token)[0]["items"][0]
        assert after["state"] == "Suspended"
        assert int(after["version"]) == int(before["version"]) + 1
        assert int(after["authVersion"]) == int(before["authVersion"]) + 1
        replay, _ = self.request("POST", user_path + "/recover", {}, token=token,
                                 headers=recovery_headers)
        assert replay["status"] == "complete" and "recoveryURL" not in json.dumps(replay)
        assert self.request("GET", user_query, token=token)[0]["items"][0] == after
        grants_query = "/admin/grants?limit=1000"
        grants = self.request("GET", grants_query, token=token)[0]["items"]
        selected = next(row for row in grants if row["userID"] == user["id"]
                        and row["state"] == "Active")
        assert b64(unb64(selected["id"])) == selected["id"]
        exact = {"id": selected["id"], "limit": 1}
        for expected in (1, 0):
            result, _ = self.request("POST", "/admin/grants/revoke", exact, token=token,
                                     headers={"Idempotency-Key": secrets.token_hex(16)})
            assert result["item"]["revoked"] == expected
            rows = self.request("GET", grants_query, token=token)[0]["items"]
            assert next(row for row in rows if row["id"] == selected["id"])["state"] == "Revoked"
            assert [row for row in rows if row["id"] != selected["id"]] == [
                row for row in grants if row["id"] != selected["id"]]
        for collection in ("sessions", "consents", "grants"):
            query = "/admin/" + collection + "?limit=1000"
            path = "/admin/" + collection + "/revoke"
            before = self.request("GET", query, token=token)[0]["items"]
            unrelated = [row for row in before if row["userID"] != user["id"]]
            eligible = [row for row in before if row["userID"] == user["id"]
                        and row["state"] not in ("Revoked", "Consumed")]
            body = {"userID": user["id"], "limit": 1}
            self.request("POST", path, body, token=token, status=400)
            self.request("POST", path, dict(body, limit=0), token=token,
                         headers={"Idempotency-Key": secrets.token_hex(16)}, status=400)
            if not eligible:
                # The exact grant revocation above may consume the last live
                # grant; the filtered operation must then remain successful
                # and report that it changed nothing.
                assert collection == "grants"
                result, _ = self.request("POST", path, body, token=token,
                                         headers={"Idempotency-Key": secrets.token_hex(16)})
                assert result["item"]["revoked"] == 0
                continue
            result, _ = self.request("POST", path, body, token=token,
                                     headers={"Idempotency-Key": secrets.token_hex(16)})
            assert result["item"]["revoked"] == 1, collection + " did not revoke one eligible record"
            after = self.request("GET", query, token=token)[0]["items"]
            assert [row for row in after if row["userID"] != user["id"]] == unrelated
            remaining = [row for row in after if row["userID"] == user["id"]
                         and row["state"] not in ("Revoked", "Consumed")]
            assert len(remaining) == len(eligible) - 1
            result, _ = self.request("POST", path, dict(body, limit=1000), token=token,
                                     headers={"Idempotency-Key": secrets.token_hex(16)})
            assert result["item"]["revoked"] == len(remaining)
            result, _ = self.request("POST", path, body, token=token,
                                     headers={"Idempotency-Key": secrets.token_hex(16)})
            assert result["item"]["revoked"] == 0

        grants_query = "/admin/grants?limit=1000"
        cleanup_path = "/admin/grants/cleanup"
        grants = self.request("GET", grants_query, token=token)[0]["items"]
        self.request("POST", cleanup_path, {"limit": 1}, token=token, status=400)
        self.request("POST", cleanup_path, {"limit": 0}, token=token,
                     headers={"Idempotency-Key": secrets.token_hex(16)}, status=400)
        self.request("POST", cleanup_path, {"limit": 1, "before": int(time.time()) + 86400},
                     token=token, headers={"Idempotency-Key": secrets.token_hex(16)}, status=400)
        cleaned, _ = self.request("POST", cleanup_path, {"limit": 1}, token=token,
                                  headers={"Idempotency-Key": secrets.token_hex(16)})
        cutoff = int(time.time())
        after = self.request("GET", grants_query, token=token)[0]["items"]
        remaining_ids = {row["id"] for row in after}
        removed = [row for row in grants if row["id"] not in remaining_ids]
        assert len(removed) == cleaned["item"]["removed"] <= 1
        assert all(row["expires"] <= cutoff for row in removed)
        assert after == [row for row in grants if row["id"] in remaining_ids]

    def numeric_filters(self, token, app_id, action_id):
        prefix = "/admin/apps/" + app_id
        filters = [("/admin/" + collection, "id", {})
                   for collection in ("apps", "users", "providers")]
        filters += [("/admin/auth-policies", "appID", {}),
                    (prefix + "/memberships", "userID", {}),
                    (prefix + "/actions", "id", {}),
                    (prefix + "/roles", "id", {})]
        for path, field, extra in filters:
            for invalid in ("invalid", "1tail", "-1", "18446744073709551615", ""):
                self.request("GET", path + "?" + urlencode(dict(extra, **{field: invalid})),
                             token=token, status=400)
        self.request("GET", prefix + "/actions?id=4294967296", token=token, status=400)
        for invalid in ("invalid", "0", "18446744073709551615", app_id + "tail"):
            self.request("GET", "/admin/apps/" + invalid + "/roles", token=token, status=400)
        padded = self.request("GET", "/admin/apps?id=000" + app_id, token=token)[0]
        assert len(padded["items"]) == 1 and padded["items"][0]["id"] == app_id
        maximum = self.request("GET", "/admin/apps?id=18446744073709551614", token=token)[0]
        assert maximum["items"] == []
        # Action positions are zero-based; rejecting /actions/0/state would make
        # the application's first action impossible to administer.
        assert action_id == 0
        query = prefix + "/actions?id=" + str(action_id)
        path = prefix + "/actions/" + str(action_id) + "/state"
        before = self.request("GET", query, token=token)[0]["items"][0]
        self.request("PUT", path, {"state": "Suspended"}, token=token, status=428)
        self.request("PUT", path, {"state": "Suspended"}, token=token,
                     headers={"If-Match": '"stale"'}, status=412)
        current = before
        for state in ("Suspended", "Active"):
            self.request("PUT", path, {"state": state}, token=token,
                         headers={"If-Match": current["etag"]})
            changed = self.request("GET", query, token=token)[0]["items"][0]
            assert changed["id"] == action_id and changed["state"] == state
            assert int(changed["version"]) == int(current["version"]) + 1
            self.request("PUT", path, {"state": state}, token=token,
                         headers={"If-Match": changed["etag"]})
            assert self.request("GET", query, token=token)[0]["items"][0] == changed
            current = changed
        self.stop()
        self.start()
        assert self.request("GET", query, token=token)[0]["items"][0] == current

    def rotate_client(self, token, client_id, original, app_id):
        query = "/admin/clients?" + urlencode({"id": client_id})
        path = "/admin/clients/" + client_id + "/rotate-secret"

        def current():
            return self.request("GET", query, token=token)[0]["items"][0]

        def authenticate(basic, status=200):
            return self.request("POST", self.oauth(app_id, "token"), {
                "grant_type": "client_credentials", "scope": "zum.catalog"},
                form=True, headers={"Authorization": "Basic " + basic}, status=status)

        before = current()
        for etag, status in ((None, 428), ('"stale"', 412)):
            headers = {"Idempotency-Key": secrets.token_hex(16)}
            if etag:
                headers["If-Match"] = etag
            self.request("POST", path, {"overlapSeconds": 60},
                         token=token, headers=headers, status=status)
            assert current() == before
        previous = original
        for overlap in (60, 0):
            headers = {"Idempotency-Key": secrets.token_hex(16),
                       "If-Match": before["etag"]}
            body = {"overlapSeconds": overlap}
            result = self.request("POST", path, body, token=token, headers=headers)[0]
            item = result["item"]
            assert item["id"] == client_id and item["client_secret"]
            basic = base64.b64encode(
                (client_id + ":" + item["client_secret"]).encode()).decode()
            after = current()
            assert int(after["version"]) == int(before["version"]) + 1
            assert int(after["secretVersion"]) == int(before["secretVersion"]) + 1
            assert not {"client_secret", "secretDigest", "previousSecretDigest"} & after.keys()
            replay = self.request("POST", path, body, token=token, headers=headers)[0]
            assert replay["status"] == "complete" and client_id in replay["resultIDs"]
            assert "item" not in replay and "client_secret" not in replay
            assert current() == after
            authenticate(basic)
            authenticate(previous, 200 if overlap else 401)
            self.stop()
            self.start()
            assert current() == after
            authenticate(basic)
            authenticate(previous, 200 if overlap else 401)
            if not overlap:
                authenticate(original, 401)
            before, previous = after, basic

    def pending_user(self, token):
        created = self.admin_secret("userInvite", {
            "name": "pending-state", "$idempotencyKey": secrets.token_hex(16)})
        user_id = created["item"]["id"]
        assert created["item"]["enrollmentURL"]
        query = "/admin/users?id=" + user_id
        path = "/admin/users/" + user_id + "/state"
        result, _ = self.request("GET", query, token=token)
        current = result["items"][0]
        assert current["state"] == "Pending"
        self.request("PUT", path, {"state": "Active"}, token=token, status=428)
        self.request("PUT", path, {"state": "Active"}, token=token,
                     headers={"If-Match": '"stale"'}, status=412)
        rejected, _ = self.request("PUT", path, {"state": "Active"}, token=token,
                                   headers={"If-Match": current["etag"]}, status=409)
        failures = [rejected["correlationID"]]
        for state in ("Suspended", "Disabled"):
            self.request("PUT", path, {"state": state}, token=token,
                         headers={"If-Match": current["etag"]})
            result, _ = self.request("GET", query, token=token)
            changed = result["items"][0]
            assert changed["state"] == state
            assert int(changed["version"]) == int(current["version"]) + 1
            assert int(changed["authVersion"]) == int(current["authVersion"]) + 1
            rejected, _ = self.request("PUT", path, {"state": "Active"}, token=token,
                                       headers={"If-Match": changed["etag"]}, status=409)
            failures.append(rejected["correlationID"])
            result, _ = self.request("GET", query, token=token)
            assert result["items"][0] == changed
            current = changed
        self.request("GET", "/admin/audit", token=token, status=404)
        self.request("POST", "/admin/audit/cleanup", {}, token=token, status=404)
        capability = parse_qs(urlsplit(created["item"]["enrollmentURL"]).query)["capability"][0]
        return query, path, current, {
            "actor": self.verify_jwt(token)["sub"], "path": path,
            "failures": failures, "secrets": [token, capability,
                                            created["item"]["enrollmentURL"]],
        }

    def enroll(self):
        self.request("GET", "/health/live")
        self.request("GET", "/health/ready", status=503)
        capability_file = self.directory / "enrollment"
        assert capability_file.stat().st_mode & 0o777 == 0o600
        enrollment = urlsplit(capability_file.read_text().strip())
        capability = parse_qs(enrollment.query)["capability"][0]
        page, _ = self.request("GET", enrollment.path + "?" + enrollment.query)
        match = re.search(r"/oauth2/([0-9]+)/v1/passkey/begin", page)
        assert match, "bootstrap page did not identify its application issuer"
        self.core_app_id = match[1]
        begin, _ = self.request("POST", self.oauth(
                                self.core_app_id, "passkey/begin"),
                                {"purpose": "bootstrap", "capability": capability})
        registration = self.authenticator.register(begin["options"]["publicKey"])
        self.request("POST", self.oauth(self.core_app_id, "passkey/finish") +
                     "?id=" + begin["ceremony"], registration)
        self.request("GET", "/health/ready")
        metadata, _ = self.request("GET", "/.well-known/"
                                   "oauth-authorization-server/oauth2/" +
                                   self.core_app_id)
        assert metadata["issuer"] == self.issuer()
        assert metadata["authorization_endpoint"] == \
            self.issuer() + "/v1/authorize"
        assert metadata["token_endpoint"] == self.issuer() + "/v1/token"
        assert metadata["jwks_uri"] == self.issuer() + "/v1/keys"
        oidc, _ = self.request("GET", "/oauth2/" + self.core_app_id +
                               "/.well-known/openid-configuration")
        assert oidc == metadata

    def login(self, client_id="zum-admin", scope="zum.admin", app_id=None,
              return_tokens=False, client_secret=None, replay_refresh=False,
              redirect="http://127.0.0.1:49152/callback", login="http-admin",
              wrong_app_id=None, offline=True):
        app_id = app_id or self.core_app_id
        if offline and "offline_access" not in scope.split():
            scope += " offline_access"
        verifier = b64(secrets.token_bytes(32))
        state = b64(secrets.token_bytes(24))
        query = {"response_type": "code", "client_id": client_id,
                 "redirect_uri": redirect, "scope": scope, "state": state,
                 "code_challenge": b64(hashlib.sha256(verifier.encode()).digest()),
                 "code_challenge_method": "S256"}
        nonce = b64(secrets.token_bytes(24)) if "openid" in scope.split() else None
        if nonce:
            query["nonce"] = nonce
        location = self.authorize(self.oauth(app_id, "authorize") + "?" +
                                  urlencode(query), login=login)
        result = parse_qs(urlsplit(location).query)
        assert result["state"] == [state] and "code" in result, result
        assert location.startswith(redirect + "?")
        request = {"grant_type": "authorization_code", "client_id": client_id,
                   "redirect_uri": redirect, "code": result["code"][0],
                   "code_verifier": verifier}
        if wrong_app_id is not None:
            denied, _ = self.request("POST", self.oauth(wrong_app_id, "token"),
                                     request, form=True, status=401)
            assert denied["error"] == "invalid_client"
        def token_request(value):
            headers = {}
            if client_secret is not None:
                for secret in (None, "incorrect-secret"):
                    invalid_headers = {} if secret is None else {
                        "Authorization": "Basic " + base64.b64encode(
                            (client_id + ":" + secret).encode()).decode()}
                    denied, _ = self.request("POST", self.oauth(app_id, "token"),
                                              value, form=True,
                                              headers=invalid_headers, status=401)
                    assert denied["error"] == "invalid_client"
                headers["Authorization"] = "Basic " + base64.b64encode(
                    (client_id + ":" + client_secret).encode()).decode()
                if value["grant_type"] == "authorization_code":
                    denied, _ = self.request("POST", self.oauth(app_id, "token"),
                                              dict(value, code_verifier=b64(secrets.token_bytes(32))),
                                              form=True, headers=headers, status=400)
                    assert denied["error"] == "invalid_grant"
            return self.request("POST", self.oauth(app_id, "token"), value,
                                form=True, headers=headers)

        tokens, _ = token_request(request)
        assert tokens["token_type"].lower() == "bearer" and tokens["access_token"]
        if nonce:
            identity = self.verify_jwt(tokens["id_token"], app_id)
            assert identity["iss"] == self.issuer(app_id) and identity["aud"] == client_id
            assert identity["nonce"] == nonce and identity["sub"]
            assert identity["iat"] <= time.time() < identity["exp"]
        if not offline:
            assert "refresh_token" not in tokens
            return tokens if return_tokens else tokens["access_token"]
        replay_headers = {} if client_secret is None else {
            "Authorization": "Basic " + base64.b64encode(
                (client_id + ":" + client_secret).encode()).decode()}
        self.request("POST", self.oauth(app_id, "token"), request, form=True,
                     headers=replay_headers, status=400)
        refreshed, _ = token_request({
            "grant_type": "refresh_token", "client_id": client_id,
            "refresh_token": tokens["refresh_token"]})
        assert refreshed["token_type"].lower() == "bearer" and refreshed["access_token"]
        assert refreshed["refresh_token"] != tokens["refresh_token"]
        if replay_refresh:
            denied, _ = self.request("POST", self.oauth(app_id, "token"), {
                "grant_type": "refresh_token", "client_id": client_id,
                "refresh_token": tokens["refresh_token"]}, form=True,
                headers=replay_headers, status=400)
            assert denied["error"] == "invalid_grant"
        if nonce:
            identity = self.verify_jwt(refreshed["id_token"], app_id)
            assert identity["iss"] == self.issuer(app_id) and identity["aud"] == client_id
            assert identity["nonce"] == nonce and identity["sub"]
        return refreshed if return_tokens else refreshed["access_token"]

    def session_lifecycle(self, client_id, app_id):
        cookies = copy.deepcopy(self.cookies)
        self.cookies = SimpleCookie()
        native = self.login(client_id, "ping", app_id, return_tokens=True)
        self.cookies = cookies

        def silent(client, scope, issuer_app, error=None, max_age=None):
            verifier = b64(secrets.token_bytes(32))
            state = b64(secrets.token_bytes(24))
            redirect = "http://127.0.0.1:49152/callback"
            query = {"response_type": "code", "client_id": client,
                     "redirect_uri": redirect, "scope": scope,
                     "state": state, "prompt": "none",
                     "code_challenge": b64(hashlib.sha256(verifier.encode()).digest()),
                     "code_challenge_method": "S256"}
            if max_age is not None:
                query["max_age"] = max_age
            _, headers = self.request("GET", self.oauth(issuer_app, "authorize") +
                                      "?" + urlencode(query), status=302)
            location = headers["location"]
            assert location.startswith(redirect + "?")
            result = parse_qs(urlsplit(location).query)
            assert result["state"] == [state]
            if error:
                assert result.get("error") == [error] and "code" not in result
                return
            assert "code" in result and "error" not in result, result.get("error")
            return self.request("POST", self.oauth(issuer_app, "token"), {
                "grant_type": "authorization_code", "client_id": client,
                "redirect_uri": redirect, "code": result["code"][0],
                "code_verifier": verifier}, form=True)[0]

        # The current session was authenticated through the confidential app
        # client. Reuse it for another client of the same application, but not
        # across application-scoped issuer boundaries.
        ordinary = silent(client_id, "ping", app_id)
        assert "refresh_token" not in ordinary
        session_cookies = SimpleCookie(self.cookies.output(header="", sep=";"))
        silent(client_id, "offline_access ping", app_id, "consent_required")
        self.cookies = copy.deepcopy(session_cookies)
        silent("zum-admin", "zum.admin", self.core_app_id, "login_required")
        self.cookies = SimpleCookie()
        admin = self.login(return_tokens=True)
        self.cookies = SimpleCookie(session_cookies.output(header="", sep=";"))
        silent(client_id, "ping", app_id, max_age=3600)
        # Cross a real protocol-second boundary: max_age=0 must reject this
        # existing authentication even though its session is otherwise usable.
        time.sleep(int(time.time()) + 1.05 - time.time())
        silent(client_id, "ping", app_id, "login_required", max_age=0)
        self.cookies = SimpleCookie(session_cookies.output(header="", sep=";"))
        silent(client_id, "ping", app_id)
        page, _ = self.request("GET", self.oauth(app_id, "authorize") + "?" + urlencode({
            "response_type": "code", "client_id": client_id,
            "redirect_uri": "http://127.0.0.1:49152/callback",
            "scope": "ping", "prompt": "login",
            "state": b64(secrets.token_bytes(24)), "code_challenge_method": "S256",
            "code_challenge": b64(hashlib.sha256(secrets.token_bytes(32)).digest())}))
        assert "navigator.credentials" in page, "prompt=login must require authentication"
        # Abandon the reauthentication transaction. Its new binding cookie is
        # not the authenticated handle whose logout boundary is tested below.
        self.cookies = session_cookies
        page, _ = self.request("GET", self.oauth(app_id, "login"))
        csrf = re.search(r'name=csrf value="([^"]+)"', page)
        assert csrf, "provider session must offer CSRF-bound logout"
        self.request("POST", self.oauth(app_id, "logout"),
                     {"csrf": "incorrect"}, form=True, status=400)
        silent(client_id, "ping", app_id)
        old_cookies = SimpleCookie(self.cookies.output(header="", sep=";"))
        page, headers = self.request("POST", self.oauth(app_id, "logout"),
                                     {"csrf": csrf[1]}, form=True)
        assert "Signed out of Zum" in page and "location" not in headers
        silent(client_id, "ping", app_id, "login_required")
        self.cookies = old_cookies
        silent(client_id, "ping", app_id, "login_required")
        self.cookies = SimpleCookie()
        # Ending provider SSO does not revoke independent application grants.
        for client, issuer_app, tokens in (
                (client_id, app_id, native),
                ("zum-admin", self.core_app_id, admin)):
            refreshed, _ = self.request("POST", self.oauth(issuer_app, "token"), {
                "grant_type": "refresh_token", "client_id": client,
                "refresh_token": tokens["refresh_token"]}, form=True)
            assert refreshed["access_token"]
            assert refreshed["refresh_token"] != tokens["refresh_token"]
            # Reusing a consumed token revokes that family, including the
            # newest token, rather than just rejecting the stale credential.
            for consumed in (tokens["refresh_token"], refreshed["refresh_token"]):
                denied, _ = self.request("POST", self.oauth(issuer_app, "token"), {
                    "grant_type": "refresh_token", "client_id": client,
                    "refresh_token": consumed}, form=True, status=400)
                assert denied["error"] == "invalid_grant"

    def authorize(self, path, login="http-admin", authenticator=None):
        route = re.match(r"^(/oauth2/[^/]+)/v1/", path)
        assert route, "authorization request lacks application issuer path"
        prefix = route[1] + "/v1/"
        page, headers = self.request("GET", path, status=(200, 302))
        if "location" in headers:
            return headers["location"]
        finish = page
        if not re.search(r'name=id value="([^"]+)"', page):
            match = re.search(r"const id='([^']+)',o=(.*?),loginPath='", page)
            assert match, "authorization did not return a passkey page"
            ceremony = match[1]
            _, headers = self.request("POST", prefix + "login",
                                      {"id": ceremony, "login": login},
                                      form=True, status=(200, 302))
            if "location" in headers:
                assert self.idp is not None, "no upstream browser fixture configured"
                callback = urlsplit(self.idp.authorize(headers["location"]))
                issuer_app = route[1].rsplit("/", 1)[1]
                if self.core_app_id and issuer_app != self.core_app_id:
                    cookies = copy.deepcopy(self.cookies)
                    denied, _ = self.request(
                        "GET", self.oauth(self.core_app_id, "oidc/callback") +
                        "?" + callback.query, status=400)
                    assert denied["error"] == "access_denied"
                    self.cookies = cookies
                finish, headers = self.request("GET", callback.path + "?" + callback.query,
                                               status=(200, 302))
                if "location" in headers:
                    return headers["location"]
            else:
                assertion = (authenticator or self.authenticator).assert_(json.loads(match[2])["publicKey"])
                finish, _ = self.request("POST", prefix +
                                         "passkey/finish?id=" + ceremony, assertion)
        if isinstance(finish, str):
            consent = re.search(r'name=id value="([^"]+)"', finish)
            assert consent, "unexpected passkey completion page"
            issuer_app = route[1].rsplit("/", 1)[1]
            if self.core_app_id and issuer_app != self.core_app_id:
                cookies = copy.deepcopy(self.cookies)
                denied, _ = self.request(
                    "POST", self.oauth(self.core_app_id, "consent"),
                    {"id": consent[1], "decision": "approve"},
                    form=True, status=400)
                assert denied["error"] == "access_denied"
                self.cookies = cookies
            _, headers = self.request("POST", prefix + "consent",
                                      {"id": consent[1], "decision": "approve"},
                                      form=True, status=302)
            location = headers["location"]
        else:
            location = finish["redirectURI"]
        return location

    def cli_callback(self, process, port, login="http-admin", authenticator=None,
                     app_id=None, before_callback=None):
        pending = b""
        deadline = time.monotonic() + 30
        with selectors.DefaultSelector() as selector:
            selector.register(process.stdout, selectors.EVENT_READ)
            while True:
                remaining = deadline - time.monotonic()
                if remaining <= 0 or not selector.select(remaining):
                    raise AssertionError("CLI authorization URL timed out")
                data = os.read(process.stdout.fileno(), 4096)
                assert data, "CLI exited before authorization"
                pending += data
                lines = pending.split(b"\n")
                pending = lines.pop()
                urls = [line for line in lines if line.startswith((b"http://", b"https://"))]
                if urls:
                    url = urlsplit(urls[0].decode())
                    break
        assert f"{url.scheme}://{url.netloc}" == self.origin
        assert url.path == self.oauth(app_id or self.core_app_id, "authorize")
        location = urlsplit(self.authorize(url.path + "?" + url.query, login, authenticator))
        assert location.hostname == "127.0.0.1" and location.port == port
        assert location.path == "/callback"
        if before_callback is not None:
            before_callback()
        connection = http.client.HTTPConnection("127.0.0.1", port, timeout=15)
        try:
            connection.request("GET", location.path + "?" + location.query)
            response = connection.getresponse()
            response.read()
            assert response.status == 200
        finally:
            connection.close()

    def admin_cli(self):
        self.cookies = SimpleCookie()
        executable = Path(__file__).resolve().parent.parent / "src" / "zum"
        config = self.directory / "admin.cf"
        home = self.directory / "admin-vault"
        vault_env = {**os.environ, "ZUM_HOME": str(home),
                     "DBUS_SESSION_BUS_ADDRESS": "unsupported:address"}
        with socket.socket() as listener:
            listener.bind(("127.0.0.1", 0))
            port = listener.getsockname()[1]
        config.write_text(f'issuerURL: {json.dumps(self.issuer())}, '
                          f'managementURL: {json.dumps(self.origin)}, '
                          f'scope: "zum.admin offline_access", '
                          f'caPath: {json.dumps(str(self.ca_path) if self.ca_path else "")}, '
                          f'callbackPort: {port}, loginTimeout: 30, '
                          f'loopbackTest: true\n')
        with (self.directory / "admin.log").open("ab") as log:
            process = subprocess.Popen([str(executable), "--config", str(config),
                                        "login", "--no-browser"],
                                       stdout=subprocess.PIPE, stderr=log,
                                       env=vault_env)
            try:
                self.cli_callback(process, port)
                output, _ = process.communicate(timeout=30)
                assert process.returncode == 0, \
                    "admin CLI login failed rc=" + str(process.returncode) + \
                    " stdout=" + output.decode(errors="replace") + \
                    " stderr=" + (self.directory / "admin.log").read_text(
                        errors="replace")
            finally:
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.communicate(timeout=10)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.communicate()
                process.stdout.close()
            assert (home / "vault" / "secrets.json").stat().st_mode & 0o777 == 0o600
        apps = self.admin_command("appQuery", {})
        assert len(apps["items"]) == 1 and apps["items"][0]["name"] == "zum"

    def admin_command(self, operation, value):
        executable = Path(__file__).resolve().parent.parent / "src" / "zum"
        request = self.directory / "admin-request.json"
        request.write_bytes(compact(value))
        with (self.directory / "admin.log").open("ab") as log:
            result = subprocess.run([str(executable), "--config",
                                     str(self.directory / "admin.cf"), operation,
                                     "--json", str(request), "--no-browser"],
                                    stdout=subprocess.PIPE, stderr=log, timeout=30,
                                    env={**os.environ,
                                         "ZUM_HOME": str(self.directory / "admin-vault"),
                                         "DBUS_SESSION_BUS_ADDRESS": "unsupported:address"})
        assert result.returncode == 0, "admin CLI " + operation + " failed"
        try:
            return json.loads(result.stdout)
        except ValueError as error:
            raise AssertionError(f"admin CLI {operation} returned invalid JSON: "
                                 f"{result.stdout!r}") from error

    def admin_secret(self, operation, value):
        output = self.directory / (operation + "-secret.json")
        receipt = self.admin_command(operation, dict(value, **{"$secretOutput": str(output)}))
        assert receipt == {"secretOutput": str(output)}
        assert output.stat().st_mode & 0o777 == 0o600
        return json.loads(output.read_bytes())

    def ping_service(self, after_login=None, foreign_tokens=()):
        audience = "https://ping.example/api"
        app = self.admin_secret("appEnroll", {
            "name": "zumpingd", "audience": audience,
            "$idempotencyKey": secrets.token_hex(16)})["item"]
        stored = self.admin_command("appQuery", {"id": app["appID"]})["items"]
        assert len(stored) == 1 and stored[0]["audience"] == audience
        assert self.admin_command("clientQuery", {"id": "zumping"})["items"] == []
        with socket.socket() as listener:
            listener.bind(("127.0.0.1", 0))
            port = listener.getsockname()[1]
        config = self.directory / "zumpingd.cf"
        config.write_text(f'zum: {{issuerURL: {json.dumps(self.issuer(app["appID"]))}, '
                          f'managementIssuerURL: {json.dumps(self.issuer(self.core_app_id))}, '
                          f'managementURL: {json.dumps(self.origin)}, '
                          f'clientID: {json.dumps(app["client_id"])} }}, '
                          f'caPath: {json.dumps(str(self.ca_path) if self.ca_path else "")}, '
                          f'audience: {json.dumps(audience)}, '
                          f'port: {port}\n')
        env = dict(os.environ, ZUM_CLIENT_SECRET=app["client_secret"],
                   ZUM_SSF_AUTH="Bearer " + secrets.token_urlsafe(24),
                   ZUMPINGD_HOME=str(self.directory / "zumpingd-vault"),
                   DBUS_SESSION_BUS_ADDRESS="unsupported:address")
        for key in ("ZUM_DB_KEY", "ZDB_MODULE", "ZDB_CONNECT"):
            env.pop(key, None)
        executable = Path(__file__).resolve().parent.parent / "example" / "zumpingd"
        missing_env = dict(env)
        missing_env.pop("ZUM_CLIENT_SECRET")
        invalid_secret = "invalid-" + secrets.token_urlsafe(32)
        with (self.directory / "zumpingd.log").open("ab") as log:
            missing = subprocess.run([str(executable), "--config", str(config)],
                                     env=missing_env, stdout=subprocess.PIPE,
                                     stderr=log, timeout=10)
            invalid = subprocess.run([str(executable), "--config", str(config)],
                                     env=dict(env, ZUM_CLIENT_SECRET=invalid_secret,
                                              ZUMPINGD_HOME=str(self.directory / "invalid-zumpingd-vault")),
                                     stdout=subprocess.PIPE, stderr=log, timeout=30)
        assert missing.returncode == 1 and invalid.returncode == 1
        service_log = (self.directory / "zumpingd.log").read_text()
        assert app["client_secret"] not in service_log and invalid_secret not in service_log
        previous = None
        user = None
        for restart in range(2):
            startup_env = dict(env)
            if restart:
                startup_env.pop("ZUM_CLIENT_SECRET")
                startup_env.pop("ZUM_SSF_AUTH")
            with (self.directory / "zumpingd.log").open("ab") as log:
                process = subprocess.Popen([str(executable), "--config", str(config)],
                                           env=startup_env, stdout=subprocess.PIPE, stderr=log)
                try:
                    pending = b""
                    deadline = time.monotonic() + 30
                    with selectors.DefaultSelector() as selector:
                        selector.register(process.stdout, selectors.EVENT_READ)
                        while True:
                            remaining = deadline - time.monotonic()
                            if remaining <= 0 or not selector.select(remaining):
                                raise AssertionError("ping service startup timed out")
                            data = os.read(process.stdout.fileno(), 4096)
                            assert data, "ping service exited before listening"
                            pending += data
                            lines = pending.split(b"\n")
                            pending = lines.pop()
                            if any(line.startswith(b"zumpingd listening on port ") for line in lines):
                                break
                    def request_service(method, path, headers={}):
                        connection = http.client.HTTPConnection(
                            "127.0.0.1", port, timeout=15)
                        try:
                            connection.request(method, path, headers=headers)
                            response = connection.getresponse()
                            response.read()
                            return response.status
                        finally:
                            connection.close()
                    assert request_service("GET", "/ping") == 401
                    for method, path in (("GET", "/authorize"),
                                         ("POST", "/token"),
                                         ("POST", "/revoke")):
                        assert request_service(method, path) == 404, \
                            "resource server exposes an OAuth facade"
                    for foreign in foreign_tokens:
                        claims = self.verify_jwt(foreign)
                        assert claims["iss"] == self.issuer(claims["zum_app_id"])
                        assert claims["aud"] != audience
                        assert claims["iat"] <= time.time() < claims["exp"]
                        assert request_service("GET", "/ping", {
                            "Authorization": "Bearer " + foreign}) == 401, \
                            "ping accepted a different application's token"
                    catalog = {}
                    for operation in ("actionQuery", "roleQuery"):
                        records = self.admin_command(operation, {"appID": app["appID"]})["items"]
                        assert len(records) == 1 and records[0]["name"] == "ping"
                        catalog[operation] = records
                    clients = self.admin_command("clientQuery", {"id": "zumping"})["items"]
                    assert len(clients) == 1 and clients[0]["appID"] == str(app["appID"])
                    access = self.admin_command("clientAccessQuery", {
                        "appID": app["appID"], "clientID": "zumping"})["items"]
                    assert len(access) == 1 and access[0]["roleIDs"] == [
                        catalog["roleQuery"][0]["id"]]
                    access_query = "/admin/apps/" + app["appID"] + \
                        "/client-access?clientID=zumping"
                    self.state_cycle(access_query,
                                     "/admin/apps/" + app["appID"] +
                                     "/client-access/zumping/state",
                                     self.login(), lambda: None)
                    if previous is not None:
                        assert catalog == previous, "ping restart changed its catalog"
                    else:
                        user = self.ping_user(app, catalog, port)
                    previous = catalog
                    vault_home = self.ping_client(port, *user)
                    self.ping_client(port, *user, vault_home=vault_home,
                                     cached=True)
                    if after_login:
                        after_login(app, catalog, port, user)
                    if restart:
                        user_id = self.admin_command("userQuery", {
                            "name": "user", "source": "Local"})["items"][0]["id"]
                        def assign(roles):
                            member = self.admin_command("membershipQuery", {
                                "appID": app["appID"], "userID": user_id})["items"][0]
                            self.admin_command("membershipRoles", {
                                "appID": app["appID"], "userID": user_id,
                                "roleIDs": roles, "$ifMatch": member["etag"]})
                        try:
                            self.ping_client(port, *user,
                                             before_callback=lambda: assign([]))
                        finally:
                            assign([catalog["roleQuery"][0]["id"]])
                        self.ping_client(port, *user)
                        self.stop()
                        try:
                            self.ping_unavailable(user[0], "zumd")
                        finally:
                            self.start()
                        self.request("GET", "/health/ready")
                        self.ping_client(port, *user)
                    process.terminate()
                    process.communicate(timeout=30)
                    assert process.returncode == 0, "ping service shutdown failed"
                finally:
                    if process.poll() is None:
                        process.terminate()
                        try:
                            process.communicate(timeout=10)
                        except subprocess.TimeoutExpired:
                            process.kill()
                            process.communicate()
                    process.stdout.close()
            if restart:
                self.ping_unavailable(user[0], "zumpingd")

    def ping_user(self, app, catalog, service_port):
        app_id = app["appID"]
        invited = self.admin_secret("userInvite", {
            "name": "user", "$idempotencyKey": secrets.token_hex(16)})["item"]
        authenticator = Authenticator(self.origin)
        self.cookies = SimpleCookie()
        capability = parse_qs(urlsplit(invited["enrollmentURL"]).query)["capability"][0]
        begin, _ = self.request("POST", self.oauth(
                                self.core_app_id, "passkey/begin"),
                                {"purpose": "bootstrap", "capability": capability})
        registration = authenticator.register(begin["options"]["publicKey"])
        self.request("POST", self.oauth(self.core_app_id, "passkey/finish") +
                     "?id=" + begin["ceremony"], registration)
        member = self.admin_command("membershipAdd", {
            "appID": app_id, "userID": invited["id"],
            "$idempotencyKey": secrets.token_hex(16)})["item"]
        self.admin_command("membershipRoles", {
            "appID": app_id, "userID": invited["id"],
            "roleIDs": [catalog["roleQuery"][0]["id"]], "$ifMatch": member["etag"]})
        assigned = self.admin_command("membershipQuery", {
            "appID": app_id, "userID": invited["id"]})["items"]
        assert len(assigned) == 1
        assert assigned[0]["roleIDs"] == [catalog["roleQuery"][0]["id"]]
        core = self.admin_command("appQuery", {"name": "zum"})["items"][0]["id"]
        assert self.admin_command("membershipQuery", {
            "appID": core, "userID": invited["id"]})["items"] == []
        config, port = self.ping_registration(app, catalog, service_port)
        return config, port, authenticator, app_id

    def ping_registration(self, app, catalog, service_port):
        app_id = app["appID"]
        client = self.admin_command("clientQuery", {"id": "zumping"})["items"][0]
        port = 8081
        assert client["redirectURIs"] == [f"http://127.0.0.1:{port}/callback"]
        config = self.directory / "zumping.cf"
        config.write_text(f'issuerURL: {json.dumps(self.issuer(app_id))}, '
                          f'serviceURL: "http://127.0.0.1:{service_port}", '
                          f'clientID: {json.dumps(client["id"])}, scope: "ping offline_access", '
                          f'caPath: {json.dumps(str(self.ca_path) if self.ca_path else "")}, '
                          f'callbackPort: {port}, loginTimeout: 30, loopbackTest: true\n')
        return config, port

    def ping_client(self, service_port, config, port, authenticator, app_id, *, before_callback=None,
                    login="user", vault_home=None, cached=False):
        self.cookies = SimpleCookie()
        executable = Path(__file__).resolve().parent.parent / "example" / "zumping"
        env = dict(os.environ)
        for key in ("ZUM_CLIENT_SECRET", "ZUM_DB_KEY", "ZDB_MODULE", "ZDB_CONNECT"):
            env.pop(key, None)
        if vault_home is None:
            vault_home = self.directory / ("zumping-vault-" + secrets.token_hex(8))
        env["ZUMPING_HOME"] = str(vault_home)
        env["DBUS_SESSION_BUS_ADDRESS"] = "unsupported:address"
        with (self.directory / "zumping.log").open("ab") as log:
            process = subprocess.Popen([str(executable), "--config", str(config), "--no-browser"],
                                       env=env, stdout=subprocess.PIPE, stderr=log)
            try:
                if not cached:
                    self.cli_callback(process, port, login, authenticator,
                                      app_id, before_callback)
                output, _ = process.communicate(timeout=30)
                replies = [json.loads(line) for line in output.splitlines() if line.startswith(b"{")]
                if before_callback is not None:
                    assert process.returncode == 1, "zumping did not reject removed authority"
                    assert not replies, "zumping received pong after role removal"
                    return
                assert process.returncode == 0, "zumping failed"
                assert replies == ([{"reply": "pong"}] if cached else
                                   [{"reply": "pong"}, {"reply": "pong"}])
                if not cached:
                    assert b"refresh token rotated" in output
            finally:
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.communicate(timeout=10)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.communicate()
                process.stdout.close()
        return vault_home

    def ping_unavailable(self, config, server):
        executable = Path(__file__).resolve().parent.parent / "example" / "zumping"
        unavailable = self.directory / "zumping-unavailable.cf"
        source = config.read_text()
        shortened = source.replace("loginTimeout: 30", "loginTimeout: 3")
        assert shortened != source
        unavailable.write_text(shortened)
        env = dict(os.environ)
        for key in ("ZUM_CLIENT_SECRET", "ZUM_DB_KEY", "ZDB_MODULE", "ZDB_CONNECT"):
            env.pop(key, None)
        env["ZUMPING_HOME"] = str(self.directory / ("zumping-unavailable-" +
                                                   secrets.token_hex(8)))
        env["DBUS_SESSION_BUS_ADDRESS"] = "unsupported:address"
        with (self.directory / "zumping.log").open("ab") as log:
            result = subprocess.run([str(executable), "--config", str(unavailable),
                                     "--no-browser"], env=env, stdout=subprocess.PIPE,
                                    stderr=log, timeout=10)
        assert result.returncode != 0, "zumping accepted unavailable " + server
        assert not any(line.startswith(b"{") for line in result.stdout.splitlines()), \
            "zumping received pong with unavailable " + server

    def membership_roles(self, token, prefix, user_id, roles):
        query = prefix + "/memberships?" + urlencode({"userID": user_id})
        path = prefix + "/memberships/" + user_id + "/roles"
        before, _ = self.request("GET", query, token=token)
        before = before["items"][0]
        app_query = "/admin/apps?" + urlencode({"id": before["appID"]})
        app, _ = self.request("GET", app_query, token=token)
        app = app["items"][0]
        body = {"roleIDs": roles}
        self.request("PUT", path, body, token=token, status=428)
        self.request("PUT", path, body, token=token,
                     headers={"If-Match": '"stale"'}, status=412)
        invalid = [["0"], ["18446744073709551615"]]
        if roles:
            invalid.append([roles[0], roles[0]])
        for ids in invalid:
            self.request("PUT", path, {"roleIDs": ids}, token=token,
                         headers={"If-Match": before["etag"]}, status=400)
        unchanged, _ = self.request("GET", query, token=token)
        assert unchanged["items"][0] == before
        unchanged, _ = self.request("GET", app_query, token=token)
        assert unchanged["items"][0] == app
        changed = self.admin_command("membershipRoles", dict(body, **{
            "appID": before["appID"], "userID": user_id,
            "$ifMatch": before["etag"]}))
        after, _ = self.request("GET", query, token=token)
        after = after["items"][0]
        assert after["roleIDs"] == roles
        assert after["etag"] == changed["item"]["etag"]
        delta = int(before["roleIDs"] != roles)
        for field in ("version", "authVersion"):
            assert int(after[field]) == int(before[field]) + delta
        app_after, _ = self.request("GET", app_query, token=token)
        app_after = app_after["items"][0]
        for field in ("version", "authVersion"):
            assert int(app_after[field]) == int(app[field]) + delta
        retry, _ = self.request("PUT", path, body, token=token,
                                headers={"If-Match": after["etag"]})
        assert retry == changed
        unchanged, _ = self.request("GET", query, token=token)
        assert unchanged["items"][0] == after
        unchanged, _ = self.request("GET", app_query, token=token)
        assert unchanged["items"][0] == app_after
        return changed

    def grouped_queries(self, token, app_id, role_id, clients):
        prefix = "/admin/apps/" + app_id
        issuer, _ = self.request("GET", "/admin/issuer", token=token)
        core = "/admin/apps/" + issuer["items"][0]["coreAppID"]
        roles, _ = self.request("GET", core + "/roles", token=token)
        expected = set()
        for index in range(2):
            provider, _ = self.request("POST", "/admin/providers", {
                "name": "query-provider-" + str(index),
                "issuer": "https://query-provider-" + str(index) + ".example",
                "clientID": "query-client", "scopes": ["openid", "roles"],
                "roleClaim": "roles", "claimSource": "IDToken"}, token=token,
                headers={"Idempotency-Key": secrets.token_hex(16)}, status=201)
            provider_id = provider["item"]["id"]
            for value in ("reader", "operator"):
                suffix = "/role-mappings/" + provider_id + "/" + b64(value.encode())
                self.request("PUT", prefix + suffix, {"roleID": role_id}, token=token,
                             headers={"If-None-Match": "*"}, status=201)
                expected.add((provider_id, value))
                self.request("PUT", core + suffix, {"roleID": roles["items"][0]["id"]},
                             token=token, headers={"If-None-Match": "*"}, status=201)
        snapshots = []
        for collection in ("client-access", "role-mappings"):
            path = prefix + "/" + collection
            result, _ = self.request("GET", path + "?limit=1000", token=token)
            items = result["items"]
            assert items and all(item["appID"] == app_id for item in items)
            if collection == "client-access":
                assert clients <= {item["clientID"] for item in items}
            else:
                assert {(item["providerID"], item["value"]) for item in items} == expected
            page, _ = self.request("GET", path + "?limit=1", token=token)
            pages = page["items"]
            cursor = page["nextCursor"]
            self.request("GET", core + "/" + collection + "?" +
                         urlencode({"cursor": cursor}), token=token, status=400)
            self.request("GET", path + "?" + urlencode({"cursor": cursor + "!"}),
                         token=token, status=400)
            altered = bytearray(unb64(cursor))
            altered[-1] ^= 1
            self.request("GET", path + "?" + urlencode({"cursor": b64(altered)}),
                         token=token, status=400)
            other = "role-mappings" if collection == "client-access" else "client-access"
            self.request("GET", prefix + "/" + other + "?" +
                         urlencode({"cursor": cursor}), token=token, status=400)
            while cursor:
                page, _ = self.request("GET", path + "?" +
                                       urlencode({"cursor": cursor, "limit": 1}), token=token)
                pages.extend(page["items"])
                assert len(pages) <= len(items)
                cursor = page.get("nextCursor")
            assert pages == items, (
                collection, len(pages), len(items),
                [(offset, key) for offset, (left, right) in enumerate(zip(pages, items))
                 for key in left if left[key] != right.get(key)])
            snapshots.append((path, items))
        return snapshots

    def cross_app_refs(self, token, app_id, user_id, client_id):
        def query(path):
            return self.request("GET", path, token=token)[0]["items"]

        core_id = query("/admin/issuer")[0]["coreAppID"]
        prefix = "/admin/apps/" + app_id
        foreign_roles = query("/admin/apps/" + core_id + "/roles?limit=1000")
        foreign = next(row for row in foreign_roles if row["name"] == "zum.admin")
        assert all(row["id"] != foreign["id"] for row in query(prefix + "/roles?limit=1000"))
        app_query = "/admin/apps?id=" + app_id
        before_app = query(app_query)
        cases = (
            (prefix + "/memberships?userID=" + user_id,
             prefix + "/memberships/" + user_id + "/roles", None),
            (prefix + "/client-access?clientID=" + client_id,
             prefix + "/client-access/" + client_id, ("roleIDs",)))
        for lookup, path, fields in cases:
            before = query(lookup)
            assert len(before) == 1
            body = {field: before[0][field] for field in fields or ()}
            body["roleIDs"] = [foreign["id"]]
            self.request("PUT", path, body, token=token,
                         headers={"If-Match": before[0]["etag"]}, status=400)
            assert query(lookup) == before, "cross-app rejection changed target records"
            assert query(app_query) == before_app, "cross-app rejection changed app authority"
        lookup = prefix + "/admin-access?limit=1000"
        before = query(lookup)
        assert not any(row["actorKind"] == "User" and row["actorID"] == user_id for row in before)
        self.request("PUT", prefix + "/admin-access/user/" + user_id,
                     {"operationIDs": [], "roleIDs": [foreign["id"]]}, token=token,
                     headers={"If-None-Match": "*"}, status=400)
        assert query(lookup) == before and query(app_query) == before_app
        assert query("/admin/apps/" + core_id + "/roles?limit=1000") == foreign_roles

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
        role = create(prefix + "/roles", {"name": "ping", "label": "Ping"})
        self.request("PUT", prefix + "/roles/" + role["id"] + "/actions",
                     {"actionIDs": [action_id]}, token=token, headers={"If-Match": role["etag"]})
        member_body = {"userID": user_id}
        member_headers = {"Idempotency-Key": secrets.token_hex(16)}
        membership = self.admin_command("membershipAdd", dict(member_body, **{
            "appID": app_id, "$idempotencyKey": member_headers["Idempotency-Key"]}))
        assert membership["item"]["appID"] == app_id
        assert membership["item"]["userID"] == user_id
        for restart in (False, True):
            if restart:
                self.stop()
                self.start()
            replay, _ = self.request("POST", prefix + "/memberships", member_body,
                                     token=token, headers=member_headers)
            assert replay["status"] == "complete"
            assert replay["resultIDs"] == [app_id + ":" + user_id]
        duplicate_headers = {"Idempotency-Key": secrets.token_hex(16)}
        for _ in range(2):
            self.request("POST", prefix + "/memberships", member_body,
                         token=token, headers=duplicate_headers, status=409)
            failed, _ = self.request("GET", "/admin/operations?" + urlencode({
                "operation": "membershipAdd",
                "idempotencyKey": duplicate_headers["Idempotency-Key"]}), token=token)
            assert failed["items"] == []
        self.membership_roles(token, prefix, user_id, [role["id"]])
        client = create("/admin/clients", {
            "appID": app_id, "label": "Independent native client", "profile": "native",
            "redirectURIs": ["http://127.0.0.1:49152/callback"],
            "grants": 5, "refreshAllowed": True, "identityScopes": ["openid"]})
        self.request("PUT", prefix + "/client-access/" + client["id"], {
            "roleIDs": [role["id"]]},
            token=token, headers={"If-None-Match": "*"}, status=201)
        self.cross_app_refs(token, app_id, user_id, client["id"])
        app, _ = self.request("GET", "/admin/apps?id=" + app_id, token=token)
        # The app's earlier mutations made its authority version independent.
        assert int(app["items"][0]["authVersion"]) > 1
        self.cookies = SimpleCookie()
        tokens = self.login(client["id"], "openid ping", app_id,
                           return_tokens=True, wrong_app_id=self.core_app_id)
        ordinary = self.login(client["id"], "openid ping", app_id,
                              return_tokens=True, offline=False)
        assert "refresh_token" not in ordinary
        self.verify_access(tokens["access_token"], client["id"], app_id, audience_uri, ["ping"])
        identity = self.verify_jwt(tokens["id_token"], app_id)
        for method in ("GET", "POST"):
            info, _ = self.request(method, self.oauth(app_id, "userinfo"),
                                   token=tokens["access_token"])
            assert info == {"sub": identity["sub"]}, "ungranted identity claims leaked"
            for invalid in (None, "invalid", tokens["id_token"], token):
                denied, _ = self.request(method, self.oauth(app_id, "userinfo"),
                                         token=invalid, status=401)
                assert denied["error"] == "invalid_token"
        client_query = "/admin/clients?id=" + client["id"]
        registered, _ = self.request("GET", client_query, token=token)
        suspended, _ = self.request("PUT", "/admin/clients/" + client["id"] + "/state",
                                     {"state": "Suspended"}, token=token,
                                     headers={"If-Match": registered["items"][0]["etag"]})
        for method in ("GET", "POST"):
            denied, _ = self.request(method, self.oauth(app_id, "userinfo"),
                                     token=tokens["access_token"], status=401)
            assert denied["error"] == "invalid_token"
        self.request("PUT", "/admin/clients/" + client["id"] + "/state",
                     {"state": "Active"}, token=token,
                     headers={"If-Match": suspended["item"]["etag"]})
        for method in ("GET", "POST"):
            info, _ = self.request(method, self.oauth(app_id, "userinfo"),
                                   token=tokens["access_token"])
            assert info == {"sub": identity["sub"]}
        web = create("/admin/clients", {
            "appID": app_id, "label": "Independent web client", "profile": "server",
            "redirectURIs": ["https://orders.example/callback"],
            "grants": 5, "refreshAllowed": True, "identityScopes": ["openid"]})
        registered, _ = self.request("GET", "/admin/clients?id=" + web["id"], token=token)
        assert len(registered["items"]) == 1
        assert "client_secret" not in registered["items"][0]
        assert "secretDigest" not in registered["items"][0]
        self.request("PUT", prefix + "/client-access/" + web["id"], {
            "roleIDs": [role["id"]]},
            token=token, headers={"If-None-Match": "*"}, status=201)
        collections = self.grouped_queries(token, app_id, role["id"],
                                           {client["id"], web["id"]})
        self.cookies = SimpleCookie()
        web_tokens = self.login(web["id"], "openid ping", app_id,
                                return_tokens=True, client_secret=web["client_secret"],
                                redirect="https://orders.example/callback")
        self.verify_access(web_tokens["access_token"], web["id"], app_id, audience_uri, ["ping"])
        self.session_lifecycle(client["id"], app_id)
        membership_path = prefix + "/memberships/" + user_id
        self.membership_roles(token, prefix, user_id, [])

        def refresh(current, status=200):
            result, _ = self.request("POST", self.oauth(app_id, "token"), {
                "grant_type": "refresh_token", "client_id": client["id"],
                "refresh_token": current["refresh_token"]}, form=True, status=status)
            return result

        narrowed = refresh(tokens)
        self.verify_access(narrowed["access_token"], client["id"], app_id, audience_uri, [])
        restored = self.membership_roles(token, prefix, user_id, [role["id"]])
        still_narrowed = refresh(narrowed)
        self.verify_access(still_narrowed["access_token"], client["id"], app_id, audience_uri, [])
        before, _ = self.request("GET", prefix + "/memberships?userID=" + user_id,
                                 token=token)
        before = before["items"][0]
        app_before, _ = self.request("GET", "/admin/apps?id=" + app_id, token=token)
        suspended, _ = self.request("PUT", membership_path + "/state",
                                    {"state": "Suspended"}, token=token,
                                    headers={"If-Match": restored["item"]["etag"]})
        self.request("PUT", membership_path + "/roles", {"roleIDs": []},
                     token=token, headers={"If-Match": before["etag"]}, status=412)
        retry, _ = self.request("PUT", membership_path + "/state",
                                {"state": "Suspended"}, token=token,
                                headers={"If-Match": suspended["item"]["etag"]})
        assert retry == suspended
        after, _ = self.request("GET", prefix + "/memberships?userID=" + user_id,
                                token=token)
        after = after["items"][0]
        assert after["state"] == "Suspended" and after["roleIDs"] == before["roleIDs"]
        app_after, _ = self.request("GET", "/admin/apps?id=" + app_id, token=token)
        for field in ("version", "authVersion"):
            assert int(after[field]) == int(before[field]) + 1
            assert int(app_after["items"][0][field]) == int(app_before["items"][0][field]) + 1
        denied = refresh(still_narrowed, 400)
        assert denied["error"] == "invalid_grant"

        self.stop()
        self.start()
        self.request("GET", "/health/ready")
        denied = refresh(still_narrowed, 400)
        assert denied["error"] == "invalid_grant"
        for path, items in collections:
            result, _ = self.request("GET", path + "?limit=1000", token=token)
            assert result["items"] == items
        self.role_removal(token, app_id, user_id, client["id"],
                          role["id"])
        return tokens["access_token"]

    def role_removal(self, token, app_id, user_id, client_id,
                     retained_role):
        prefix = "/admin/apps/" + app_id

        def query(path):
            return self.request("GET", path, token=token)[0]["items"]

        roles = query(prefix + "/roles?name=catalog.probe")
        assert len(roles) == 1
        role = roles[0]
        role_id = role["id"]
        member_query = prefix + "/memberships?userID=" + user_id
        member = query(member_query)[0]
        self.request("PUT", prefix + "/memberships/" + user_id + "/roles",
                     {"roleIDs": [retained_role, role_id]}, token=token,
                     headers={"If-Match": member["etag"]})
        client_query = prefix + "/client-access?clientID=" + client_id
        access = query(client_query)[0]
        self.request("PUT", prefix + "/client-access/" + client_id,
                     {"roleIDs": [role_id]}, token=token,
                     headers={"If-Match": access["etag"]})
        access_path = prefix + "/admin-access/user/"
        for invalid_id in ("0", "18446744073709551615", user_id + "junk"):
            self.request("PUT", access_path + invalid_id,
                         {"operationIDs": [], "roleIDs": [role_id]}, token=token,
                         headers={"If-None-Match": "*"}, status=400)
        self.request("PUT", access_path + "00" + user_id,
                     {"operationIDs": [], "roleIDs": [role_id]}, token=token,
                     headers={"If-None-Match": "*"}, status=201)
        delegations = query(prefix + "/admin-access?limit=1000")
        delegation = [row for row in delegations
                      if row["actorKind"] == "User" and row["actorID"] == user_id]
        assert len(delegation) == 1
        assert not any(row["actorID"] == "00" + user_id for row in delegations)
        self.request("PUT", access_path + "000" + user_id + "/state",
                     {"state": "Suspended"}, token=token,
                     headers={"If-Match": delegation[0]["etag"]})
        delegation = [row for row in query(prefix + "/admin-access?limit=1000")
                      if row["actorKind"] == "User" and row["actorID"] == user_id][0]
        assert delegation["state"] == "Suspended"
        self.request("PUT", access_path + user_id + "/state",
                     {"state": "Active"}, token=token,
                     headers={"If-Match": delegation["etag"]})
        provider_id = query("/admin/providers?limit=1000")[0]["id"]
        self.request("PUT", prefix + "/role-mappings/" + provider_id + "/" +
                     b64(b"catalog-probe"), {"roleID": role_id}, token=token,
                     headers={"If-None-Match": "*"}, status=201)
        queries = [member_query, client_query,
                   prefix + "/admin-access?limit=1000", prefix + "/role-mappings?limit=1000"]
        before = [query(path) for path in queries]
        path = prefix + "/roles/" + role_id
        self.request("DELETE", path, token=token, status=400)
        missing, _ = self.request("DELETE", path, token=token,
                                  headers={"Idempotency-Key": secrets.token_hex(16)},
                                  status=428)
        stale, _ = self.request("DELETE", path, token=token,
                                headers={"If-Match": '"stale"',
                                         "Idempotency-Key": secrets.token_hex(16)}, status=412)
        for response, error in ((missing, "precondition_required"),
                                (stale, "precondition_failed")):
            assert response["error"] == error and response["message"]
            assert response["correlationID"]
        assert [query(path) for path in queries] == before
        headers = {"If-Match": role["etag"], "Idempotency-Key": secrets.token_hex(16)}
        result, _ = self.request("DELETE", path, token=token,
                                 headers=headers)
        deleted = query(prefix + "/roles?id=" + role_id)[0]
        assert deleted["tombstone"] and deleted["state"] == "Revoked"
        assert deleted["etag"] == result["item"]["etag"]
        assert int(deleted["version"]) == int(role["version"]) + 1
        after = [query(path) for path in queries]
        for items in after:
            assert all(role_id not in item.get("roleIDs", []) and
                       item.get("roleID") != role_id for item in items)
        assert after[0][0]["roleIDs"] == [retained_role]
        assert after[1][0]["roleIDs"] == []
        delegation = next(row for row in after[2]
                          if row["actorKind"] == "User" and row["actorID"] == user_id)
        assert delegation["roleIDs"] == []
        self.stop()
        self.start()
        assert [query(path) for path in queries] == after
        replay, _ = self.request("DELETE", path, token=token, headers=headers)
        assert replay["status"] == "complete"
        assert [query(path) for path in queries] == after

    def catalog(self, token, workload, app_id, core_id):
        def publish(target, value, *, token, headers=None, status=200):
            fields = {"Idempotency-Key": secrets.token_hex(16)}
            fields.update(headers or {})
            return self.request("PUT", target, value, token=token,
                                headers=fields, status=status)

        prefix = "/admin/apps/" + app_id
        path = prefix + "/catalog"
        manifest = catalog_manifest(1)
        headers = {"If-Match": '"catalog-0"'}
        publish(path, manifest, token=token, headers=headers, status=403)
        publish("/admin/apps/" + core_id + "/catalog", manifest,
                     token=workload, headers=headers, status=403)
        publish(path, manifest, token=workload, status=428)
        publish(path, manifest, token=workload,
                     headers={"If-Match": '"catalog-stale"'}, status=412)
        published, _ = publish(path, manifest, token=workload, headers=headers)
        assert published["item"]["revision"] == "1"
        paths = ["/admin/apps?id=" + app_id,
                 prefix + "/actions?name=catalog.probe",
                 prefix + "/roles?name=catalog.probe"]
        snapshots = [self.request("GET", query, token=token)[0] for query in paths]
        assert all(len(result["items"]) == 1 for result in snapshots)
        for result in snapshots[1:]:
            assert result["items"][0]["origin"] == "Standard"
            assert result["items"][0]["catalogRevision"] == "1"
        retry, _ = publish(path, manifest, token=workload)
        assert retry == published
        publish(path, dict(manifest, digest=b64(bytes(32))),
                     token=workload, headers=headers, status=409)
        publish(path, dict(manifest, revision="2"),
                     token=workload, status=428)
        forged = dict(manifest)
        forged["catalog"] = dict(manifest["catalog"], actions=[
            {"name": "catalog.probe", "label": "forged"}])
        publish(path, forged, token=workload, status=400)
        for query, snapshot in zip(paths, snapshots):
            assert self.request("GET", query, token=token)[0] == snapshot

    def signing_rotation(self, token):
        path = "/admin/signing-keys"
        key = ec.generate_private_key(ec.SECP256R1())
        public = key.public_key().public_numbers()
        kid = "zz-http-rotation-" + secrets.token_hex(8)
        def private_material(key):
            # The backend imports a big-endian EC scalar, not a DER envelope.
            return b64(key.private_numbers().private_value.to_bytes(32, "big"))
        material = private_material(key)
        jwk = {"kty": "EC", "crv": "P-256", "alg": "ES256", "use": "sig",
               "kid": kid, "x": b64(public.x.to_bytes(32, "big")),
               "y": b64(public.y.to_bytes(32, "big"))}
        value = {"appID": self.core_app_id, "id": kid,
                 "algorithm": "ES256", "publicJwk": compact(jwk).decode(),
                 "privateMaterial": material, "notBefore": int(time.time())}
        before, _ = self.request("GET", path, token=token)
        self.request("POST", path,
            {key: item for key, item in dict(value,
                providerRef="test-provider").items() if key != "privateMaterial"},
            token=token, headers={"Idempotency-Key": secrets.token_hex(16)}, status=400)
        assert self.request("GET", path, token=token)[0] == before
        wrong = ec.generate_private_key(ec.SECP256R1())
        self.request("POST", path, dict(value, privateMaterial=private_material(wrong)),
            token=token, headers={"Idempotency-Key": secrets.token_hex(16)}, status=400)
        assert self.request("GET", path, token=token)[0] == before
        self.request("POST", path,
            dict(value, appID="18446744073709551614"), token=token,
            headers={"Idempotency-Key": secrets.token_hex(16)}, status=404)
        assert self.request("GET", path, token=token)[0] == before
        added, _ = self.request("POST", path, value, token=token,
            headers={"Idempotency-Key": secrets.token_hex(16)}, status=201)
        listed, _ = self.request("GET", path, token=token)
        assert len(listed["items"]) == len(before["items"]) + 1
        for result in (added, listed):
            assert material not in json.dumps(result)
            assert "privateMaterial" not in json.dumps(result)
        for restarted in (False, True):
            if restarted:
                self.stop()
                self.wrong_key()
                self.start()
            self.request("GET", "/health/ready")
            rotated = self.login()
            assert json.loads(unb64(rotated.split(".")[0]))["kid"] == kid
            self.verify_jwt(rotated)
            # Rotation must retain verification of tokens signed by the old key.
            self.request("GET", path, token=token)
        old_kid = json.loads(unb64(token.split(".")[0]))["kid"]
        old = next(item for item in self.request("GET", path, token=token)[0]["items"]
                   if item["id"] == old_kid)
        retire_path = path + "/" + old_kid + "/retire"
        retirement = {"retireAfter": int(time.time()) + 86400}
        self.request("POST", retire_path, retirement, token=token, status=400)
        self.request("POST", retire_path, retirement, token=token,
                     headers={"Idempotency-Key": secrets.token_hex(16)}, status=428)
        self.request("POST", retire_path, retirement, token=token,
                     headers={"Idempotency-Key": secrets.token_hex(16),
                              "If-Match": '"stale"'}, status=412)
        self.request("POST", retire_path, retirement, token=token,
                     headers={"Idempotency-Key": secrets.token_hex(16),
                              "If-Match": old["etag"]})
        retired = next(item for item in self.request("GET", path, token=token)[0]["items"]
                       if item["id"] == old_kid)
        assert retired["state"] == "Suspended"
        assert retired["retireAfter"] == retirement["retireAfter"]
        assert int(retired["version"]) == int(old["version"]) + 1
        self.verify_jwt(token)
        self.request("POST", retire_path, retirement, token=token,
                     headers={"Idempotency-Key": secrets.token_hex(16),
                              "If-Match": old["etag"]}, status=412)

    def verify_access(self, access_token, client_id, app_id, audience_uri, actions):
        claims = self.verify_jwt(access_token)
        assert claims["iss"] == self.issuer(app_id) and claims["aud"] == audience_uri
        assert claims["zum_app_id"] == app_id and claims["client_id"] == client_id
        assert claims["scope"] == "openid ping offline_access" and claims["actions"] == actions, (
            claims["scope"], claims["actions"], actions)
        assert claims["iat"] <= time.time() < claims["exp"]

    def verify_jwt(self, token, app_id=None):
        encoded_header, encoded_claims, encoded_signature = token.split(".")
        header = json.loads(unb64(encoded_header))
        claims = json.loads(unb64(encoded_claims))
        key_app_id = app_id or claims["zum_app_id"]
        jwks, _ = self.request("GET", self.oauth(key_app_id, "keys"))
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
    for key in ("ZDB_MODULE", "ZDB_CONNECT"):
        if not os.environ.get(key):
            raise AssertionError("set " + key + " for a fresh SQLite HTTP fixture")
    directory = tempfile.mkdtemp(prefix="zum-http-")
    try:
        fixture = Fixture(directory)
        try:
            fixture.start()
            fixture.enroll()
            token = fixture.login()
            fixture.admin_cli()
            pending_query, pending_path, pending_user, user_audit = fixture.pending_user(token)
            fixture.request("GET", "/admin/apps", status=401)
            initial, _ = fixture.request("GET", "/admin/apps", token=token)
            assert len(initial["items"]) == 1
            app_input = {
                "name": "http-orders", "label": "HTTP Orders",
                "audience": "https://orders.example/api"}
            app_headers = {"Idempotency-Key": secrets.token_hex(16)}
            app = fixture.admin_secret("appEnroll", dict(app_input, **{
                "$idempotencyKey": app_headers["Idempotency-Key"]}))
            assert app["item"]["client_secret"] and app["item"]["client_id"]
            basic = base64.b64encode((app["item"]["client_id"] + ":" +
                                      app["item"]["client_secret"]).encode()).decode()
            app_id = app["item"]["appID"]
            assert app["item"]["client_id"] == app_input["name"]
            workload, _ = fixture.request("POST", fixture.oauth(app_id, "token"), {
                "grant_type": "client_credentials", "scope": "zum.catalog"},
                form=True, headers={"Authorization": "Basic " + basic})
            assert workload["access_token"] and "refresh_token" not in workload
            fixture.management_matrix(token, workload["access_token"])
            delegation_path = "/admin/apps/" + app["item"]["appID"] + "/admin-access"
            delegations, _ = fixture.request("GET", delegation_path, token=token)
            delegation = next(row for row in delegations["items"]
                              if row["actorKind"] == "Client" and
                              row["actorID"] == app["item"]["client_id"])
            state_path = delegation_path + "/client/" + delegation["actorID"] + "/state"
            suspended, _ = fixture.request("PUT", state_path, {"state": "Suspended"},
                                            token=token, headers={"If-Match": delegation["etag"]})
            fixture.request("GET", "/admin/operations", token=workload["access_token"], status=403)
            fixture.request("PUT", state_path, {"state": "Active"}, token=token,
                            headers={"If-Match": suspended["item"]["etag"]})
            fixture.request("GET", "/admin/operations", token=workload["access_token"])
            fixture.request("GET", "/admin/apps", token=workload["access_token"], status=403)
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
            action_body = {"name": "ping", "label": "Ping"}
            action_headers = {"Idempotency-Key": secrets.token_hex(16)}
            action, _ = fixture.request("POST", path, action_body,
                                        token=token, headers=action_headers,
                                        status=201)
            actions, _ = fixture.request("GET", path, token=token)
            assert len(actions["items"]) == 1 and actions["items"][0]["name"] == "ping"
            action_id = action["item"]["id"]
            assert type(action_id) is int and actions["items"][0]["id"] == action_id
            replay, _ = fixture.request("POST", path, action_body,
                                        token=token, headers=action_headers)
            assert replay["status"] == "complete" and replay["resultIDs"] == [str(action_id)]
            failed_headers = {"Idempotency-Key": secrets.token_hex(16)}
            for _ in range(2):
                fixture.request("POST", path, action_body, token=token,
                                headers=failed_headers, status=409)
                failed, _ = fixture.request("GET", "/admin/operations?" + urlencode({
                    "operation": "actionAdd",
                    "idempotencyKey": failed_headers["Idempotency-Key"]}), token=token)
                assert failed["items"] == []
            def workload_token(status=200):
                return fixture.request("POST", fixture.oauth(app_id, "token"), {
                    "grant_type": "client_credentials", "scope": "zum.catalog"},
                    form=True, headers={"Authorization": "Basic " + basic}, status=status)

            def issuer_unavailable():
                # The workload issuer is the enrolled app itself, so its
                # token endpoint rejects requests while the app is suspended.
                workload_token(status=400)
                fixture.request("GET", "/.well-known/"
                                "oauth-authorization-server/oauth2/" + app_id,
                                status=500)

            app_query = "/admin/apps?id=" + app_id
            app_version = fixture.state_cycle(app_query, update_path + "/state", token,
                                              issuer_unavailable)
            workload_token()
            active = fixture.request("GET", app_query, token=token)[0]["items"][0]
            disabled = fixture.request("PUT", update_path + "/state",
                                       {"state": "Disabled"}, token=token,
                                       headers={"If-Match": active["etag"]})[0]["item"]
            issuer_unavailable()
            changed = fixture.request("PUT", update_path + "/state",
                                      {"state": "Active"}, token=token,
                                      headers={"If-Match": disabled["etag"]})[0]["item"]
            active = fixture.request("GET", app_query, token=token)[0]["items"][0]
            assert active["etag"] == changed["etag"] and active["state"] == "Active"
            app_version = active["authVersion"]
            workload_token()
            core_id = initial["items"][0]["id"]
            access_path = "/admin/apps/" + core_id + "/client-access"
            access_query = access_path + "?" + urlencode({"clientID": app["item"]["client_id"]})
            access, _ = fixture.request("GET", access_query, token=token)
            assert access["items"] == []
            workload_token()
            client_query = "/admin/clients?" + urlencode({"id": app["item"]["client_id"]})
            client, _ = fixture.request("GET", client_query, token=token)
            fixture.request("PATCH", "/admin/clients/" + app["item"]["client_id"],
                            {"label": "Updated service client"}, token=token,
                            headers={"If-Match": client["items"][0]["etag"]})
            workload_token()
            fixture.stop()
            provisioned_key = fixture.env.pop("ZUM_DB_KEY")
            fixture.start()
            fixture.env["ZUM_DB_KEY"] = provisioned_key
            fixture.request("GET", "/health/ready")
            token = fixture.login()
            persisted, _ = fixture.request("GET", pending_query, token=token)
            assert persisted["items"][0] == pending_user
            enrollment_replay, _ = fixture.request("POST", "/admin/apps", app_input,
                                                   token=token, headers=app_headers)
            assert enrollment_replay["status"] == "complete"
            assert app_id in enrollment_replay["resultIDs"]
            assert "client_secret" not in enrollment_replay and "item" not in enrollment_replay
            assert app["item"]["client_secret"] not in json.dumps(enrollment_replay)
            # The preceding graceful stop drained ZiLog. Check those events,
            # not a second audit database or an exactly-once replay contract.
            log = (fixture.directory / "server.log").read_text()
            changes = [line for line in log.splitlines()
                       if " target=" + user_audit["path"] + " " in line
                       and " detail=userState" in line]
            assert changes
            assert all(" actor=" + user_audit["actor"] + " " in line
                       and " event=8 " in line for line in changes)
            for correlation in user_audit["failures"]:
                assert any(" correlation=" + correlation + " " in line
                           and " outcome=1 " in line for line in changes)
            assert any(" outcome=0 " in line for line in changes)
            assert all(secret not in log for secret in user_audit["secrets"])
            fixture.request("PUT", pending_path, {"state": "Active"}, token=token,
                            headers={"If-Match": pending_user["etag"]}, status=409)
            actions, _ = fixture.request("GET", path, token=token)
            assert len(actions["items"]) == 1 and actions["items"][0]["id"] == action_id
            replay, _ = fixture.request("POST", path, action_body,
                                        token=token, headers=action_headers)
            assert replay["status"] == "complete" and replay["resultIDs"] == [str(action_id)]
            apps, _ = fixture.request("GET", "/admin/apps", token=token)
            assert len(apps["items"]) == 2
            current, _ = fixture.request("GET", "/admin/apps?id=" + app_id, token=token)
            assert current["items"][0]["label"] == update["label"]
            assert current["items"][0]["authVersion"] == app_version
            access, _ = fixture.request("GET", access_query, token=token)
            assert access["items"] == []
            client, _ = fixture.request("GET", client_query, token=token)
            assert client["items"][0]["label"] == "Updated service client"
            workload_token()
            replay, _ = fixture.request("POST", "/admin/apps", app_input,
                                        token=token, headers=app_headers)
            assert replay["status"] == "complete" and app_id in replay["resultIDs"]
            assert "client_secret" not in replay and "item" not in replay
            publisher, _ = workload_token()
            fixture.catalog(token, publisher["access_token"], app_id, core_id)
            foreign_access = fixture.app_login(token, app_id, action_id, app_input["audience"])
            fixture.administrative_queries(token, app_id)
            fixture.numeric_filters(token, app_id, action_id)
            fixture.rotate_client(token, app["item"]["client_id"], basic, app_id)
            fixture.signing_rotation(token)
            assert fixture.verify_jwt(foreign_access)["actions"] == ["ping"]
            fixture.ping_service(foreign_tokens=(token, foreign_access))
            fixture.definition_lifecycle(token, app_id)
            fixture.app_disable(token, app_id)
            fixture.report_coverage()
        except BaseException:
            try:
                fixture.stop()
            except Exception:
                pass  # Preserve the original failure; diagnostics remain on disk.
            raise
        else:
            fixture.stop()
            fixture.wrong_schema()
            fixture.rekey()
    except BaseException:
        print("# failed fixture diagnostics retained in " + directory, flush=True)
        raise
    else:
        shutil.rmtree(directory)


if __name__ == "__main__":
    main()
