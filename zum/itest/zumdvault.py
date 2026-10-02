#!/usr/bin/env python3
# (c) Copyright 2026 Huw Rogers
# This code is licensed by the MIT license (see LICENSE for details)

"""Disposable zumd Vault provisioning and startup fixture."""

import base64
import errno
from http.cookies import SimpleCookie
import json
import os
from pathlib import Path
import secrets
import sqlite3
import subprocess

from urllib.parse import parse_qs, urlsplit

from zumhttp import Authenticator, Fixture, unb64
from zi_test_residue import Residue


def main():
    for name in ("ZDB_MODULE", "ZDB_CONNECT"):
        if not os.environ.get(name):
            raise AssertionError("set " + name + " for a fresh Vault fixture")
    residue = Residue("zumd-vault")
    directory = residue.directory
    fixture = Fixture(directory)
    server = Path(__file__).resolve().parent.parent / "src" / "zumd"
    config = directory / "zumd-vault.cf"
    fixture.node_config = config
    source = Path(__file__).with_name("zumd.cf").read_text().rstrip()
    config.write_text(source + ",\nvault: {store: \"file\"}\n")

    def stored():
        path = directory / "vault-home" / "vault" / "secrets.json"
        return json.loads(path.read_text())["accounts"][fixture.origin]

    def run_once(store=None, success=True, test_store=True, reissue=False,
                 output=None, log=None, repair_admin=False, bootstrap=True):
        command = [
            str(server), "--config=" + str(config),
            "--issuer=" + fixture.origin, "--once"]
        if bootstrap:
            command.append("--admin=http-admin")
        if bootstrap or output:
            command.append("--bootstrap-output=" + str(output or directory / "enrollment"))
        if reissue:
            command.append("--bootstrap-reissue")
        if repair_admin:
            command.append("--repair-admin-client")
        if log:
            command.append("--log=" + str(log))
        if store:
            command.append("--vault-store=" + store)
        if test_store:
            command.append("--vault-test-store")
        result = subprocess.run(command,
            env={**fixture.env, "DBUS_SESSION_BUS_ADDRESS": "unsupported:address"},
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30)
        assert (result.returncode == 0) == success, result.stderr.decode(
            errors="replace")
        assert (b"zumd: active" in result.stdout) == success
        return result

    success = False
    try:
        run_once(success=False, test_store=False)
        missing = run_once(bootstrap=False, success=False)
        assert b"initial bootstrap requires --admin and --bootstrap-output" in missing.stderr
        with sqlite3.connect(fixture.env["ZDB_CONNECT"]) as db:
            assert db.execute('SELECT count(*) FROM "a_zum.issuer"').fetchone() == (0,)
        run_once(repair_admin=True, success=False)
        run_once(reissue=True)
        enrollment = directory / "enrollment"
        with sqlite3.connect(fixture.env["ZDB_CONNECT"]) as db:
            grants = db.execute('SELECT digest FROM "a_zum.grant"').fetchall()
        missing = run_once(reissue=True, bootstrap=False, success=False)
        assert b"bootstrap reissue requires --bootstrap-output" in missing.stderr
        with sqlite3.connect(fixture.env["ZDB_CONNECT"]) as db:
            assert db.execute('SELECT digest FROM "a_zum.grant"').fetchall() == grants
        run_once(reissue=True, bootstrap=False, output=enrollment)
        run_once(bootstrap=False)
        original = enrollment.read_bytes()
        enrollment.write_bytes(original + b"stale trailing bytes")
        enrollment.chmod(0o644)
        run_once(reissue=True)
        renewed = enrollment.read_bytes()
        assert renewed != original
        assert len(renewed) == len(original)
        assert enrollment.stat().st_mode & 0o777 == 0o600

        # File-operation details and the fatal exception use the configured
        # ZiLog sink; a symlink must not overwrite its target.
        link = directory / "enrollment-link"
        link.symlink_to(enrollment)
        missing = directory / "missing" / "enrollment"
        for output, error in ((link, errno.ELOOP), (missing, errno.ENOENT)):
            log = directory / (output.parent.name + "-" + output.name + ".log")
            result = run_once(reissue=True, output=output, log=log, success=False)
            message = log.read_text()
            assert "bootstrap output open(" + str(output) + ") failed: " in message
            assert os.strerror(error) in message
            assert "bootstrap/signing preparation failed" in message
            assert ' FATAL "zumd.cc":' in message
            assert "[zumd] main()" in message
            assert " ERROR [Zum]" in message
            assert b"bootstrap/signing preparation failed" not in result.stderr
            assert enrollment.read_bytes() == renewed
        run_once(reissue=True)
        records = stored()
        assert base64.b64decode(records["global/dbKey"]) == base64.b64decode(
            fixture.env["ZUM_DB_KEY"])

        fixture.env.pop("ZUM_DB_KEY")
        fixture.start(bootstrap=False)
        assert not (directory / "vault-home" / "vault" / "vault.pid").exists()
        fixture.enroll()
        fixture.login(redirect="http://127.0.0.1:8081/callback")
        fixture.stop()
        fixture.start(bootstrap=False)
        fixture.login(redirect="http://127.0.0.1:8081/callback")
        fixture.stop()
        # Reissue after enrollment adds a credential to the persisted user.
        # Neither a consumed capability nor an interrupted ceremony can replay.
        consumed = parse_qs(urlsplit(enrollment.read_text()).query)["capability"][0]
        with sqlite3.connect(fixture.env["ZDB_CONNECT"]) as db:
            users = db.execute('SELECT * FROM "a_zum.user"').fetchall()
            assignments = db.execute('SELECT * FROM "a_zum.assignment"').fetchall()
            grants = db.execute('SELECT * FROM "a_zum.grant"').fetchall()
        run_once(reissue=True, bootstrap=False, success=False)
        with sqlite3.connect(fixture.env["ZDB_CONNECT"]) as db:
            assert db.execute('SELECT * FROM "a_zum.grant"').fetchall() == grants
        run_once(reissue=True, bootstrap=False, output=enrollment)
        first = parse_qs(urlsplit(enrollment.read_text()).query)["capability"][0]
        assert first != consumed and enrollment.stat().st_mode & 0o777 == 0o600
        fixture.start(bootstrap=False)
        fixture.request("GET", "/health/ready")
        begin_path = fixture.oauth(fixture.core_app_id, "passkey/begin")
        finish_path = fixture.oauth(fixture.core_app_id, "passkey/finish")
        fixture.request("POST", begin_path,
                        {"purpose": "bootstrap", "capability": consumed}, status=400)
        begin, _ = fixture.request("POST", begin_path,
                                   {"purpose": "bootstrap", "capability": first})
        abandoned = Authenticator(fixture.origin)
        registration = abandoned.register(begin["options"]["publicKey"])
        assert abandoned.handle == fixture.authenticator.handle
        fixture.stop()
        run_once(reissue=True, bootstrap=False, output=enrollment)
        second = parse_qs(urlsplit(enrollment.read_text()).query)["capability"][0]
        assert second != first
        fixture.start(bootstrap=False)
        fixture.request("POST", finish_path + "?id=" + begin["ceremony"],
                        registration, status=400)
        fixture.request("POST", begin_path,
                        {"purpose": "bootstrap", "capability": first}, status=400)
        begin, _ = fixture.request("POST", begin_path,
                                   {"purpose": "bootstrap", "capability": second})
        added = Authenticator(fixture.origin)
        registration = added.register(begin["options"]["publicKey"])
        assert added.handle == fixture.authenticator.handle
        fixture.request("POST", finish_path + "?id=" + begin["ceremony"], registration)
        fixture.request("GET", "/health/ready")
        fixture.request("POST", begin_path,
                        {"purpose": "bootstrap", "capability": second}, status=400)
        with sqlite3.connect(fixture.env["ZDB_CONNECT"]) as db:
            assert db.execute('SELECT * FROM "a_zum.user"').fetchall() == users
            assert db.execute('SELECT * FROM "a_zum.assignment"').fetchall() == assignments
            credentials = db.execute('SELECT id, state FROM "a_zum.cred"').fetchall()
            assert {row[0] for row in credentials} == {
                fixture.authenticator.credential, added.credential}
        fixture.cookies = SimpleCookie()
        fixture.login(redirect="http://127.0.0.1:8081/callback")
        original_authenticator = fixture.authenticator
        fixture.authenticator = added
        fixture.cookies = SimpleCookie()
        fixture.login(redirect="http://127.0.0.1:8081/callback")
        fixture.authenticator = original_authenticator
        fixture.stop()
        # Even an already-enrolled administrator's reissue capability expires.
        run_once(reissue=True, bootstrap=False, output=enrollment)
        expired = parse_qs(urlsplit(enrollment.read_text()).query)["capability"][0]
        with sqlite3.connect(fixture.env["ZDB_CONNECT"]) as db:
            db.execute('UPDATE "a_zum.grant" SET expires=? WHERE id=?',
                       (0, unb64(expired.split(".")[0])))
        fixture.start(bootstrap=False)
        fixture.request("POST", begin_path,
                        {"purpose": "bootstrap", "capability": expired}, status=400)
        fixture.login(redirect="http://127.0.0.1:8081/callback")
        fixture.stop()
        # Reissue can be repeated after an added credential is also consumed.
        run_once(reissue=True, bootstrap=False, output=enrollment)
        repeated = parse_qs(urlsplit(enrollment.read_text()).query)["capability"][0]
        fixture.start(bootstrap=False)
        fixture.request("GET", "/health/ready")
        begin, _ = fixture.request("POST", begin_path,
                                   {"purpose": "bootstrap", "capability": repeated})
        another = Authenticator(fixture.origin)
        registration = another.register(begin["options"]["publicKey"])
        assert another.handle == original_authenticator.handle
        fixture.request("POST", finish_path + "?id=" + begin["ceremony"], registration)
        fixture.authenticator = another
        fixture.cookies = SimpleCookie()
        fixture.login(redirect="http://127.0.0.1:8081/callback")
        fixture.authenticator = original_authenticator
        fixture.stop()
        # Model the previously dropped string vectors on a stopped database.
        with sqlite3.connect(fixture.env["ZDB_CONNECT"]) as db:
            version = db.execute('SELECT version FROM "a_zum.client" '
                                 'WHERE id=?', ("zum-admin",)).fetchone()[0]
            db.execute('UPDATE "a_zum.client" SET redirects=?, identity_scopes=? '
                       'WHERE id=?', (bytes(4), bytes(4), "zum-admin"))
        run_once(repair_admin=True, bootstrap=False)
        with sqlite3.connect(fixture.env["ZDB_CONNECT"]) as db:
            repaired = db.execute('SELECT version, _un, _sn, _vn '
                                  'FROM "a_zum.client" WHERE id=?',
                                  ("zum-admin",)).fetchone()
            assert int.from_bytes(repaired[0], "big") == \
                int.from_bytes(version, "big") + 1
        run_once(repair_admin=True, bootstrap=False)
        with sqlite3.connect(fixture.env["ZDB_CONNECT"]) as db:
            assert db.execute('SELECT version, _un, _sn, _vn '
                              'FROM "a_zum.client" WHERE id=?',
                              ("zum-admin",)).fetchone() == repaired
        fixture.start(bootstrap=False)
        fixture.login(redirect="http://127.0.0.1:8081/callback",
                      scope="openid profile email zum.admin offline_access")
        fixture.stop()
        run_once()
        fixture.wrong_key()
        assert stored()["global/dbKey"] == records["global/dbKey"]
        fixture.env["ZUM_DB_KEY"] = "bad"
        run_once(success=False)
        fixture.env["ZUM_DB_KEY"] = ""
        run_once(success=False)
        fixture.env.pop("ZUM_DB_KEY")
        fixture.env["ZUMD_HOME"] = str(directory / "missing-home")
        run_once(success=False)
        fixture.env["ZUMD_HOME"] = str(directory / "vault-home")

        # A valid key reaches bootstrap, but failed publication must not
        # report successful activation.
        blocked_home = directory / "blocked-home"
        blocked_home.write_text("not a directory")
        fixture.env["ZUM_DB_KEY"] = records["global/dbKey"]
        fixture.env["ZUMD_HOME"] = str(blocked_home)
        run_once(success=False)
        fixture.env["ZUMD_HOME"] = str(directory / "vault-home")
        fixture.env.pop("ZUM_DB_KEY")

        run_once(store="keyring", success=False)
        success = True
    finally:
        fixture.stop()
        residue.finish(success)


if __name__ == "__main__":
    main()
    print("1..1\nok 1 - zumd Vault provisioning and startup")
