#!/usr/bin/env python3
# (c) Copyright 2026 Huw Rogers
# This code is licensed by the MIT license (see LICENSE for details)

"""Disposable zumd Vault provisioning and startup fixture."""

import base64
import json
import os
from pathlib import Path
import secrets
import subprocess

from zumhttp import Fixture
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
    names = ("ZUM_SSF_ALPHA", "ZUM_SSF_BETA")
    values = {name: "Bearer " + secrets.token_urlsafe(24) for name in names}
    fixture.env.update(values)
    source = Path(__file__).with_name("zumd.cf").read_text().rstrip()

    def write_config(receiver_names):
        receivers = [{"receiverID": name, "appID": 42 + index,
                      "audience": "https://example.test/api",
                      "deliveryURL": "https://example.test/ssf",
                      "secretName": name, "revision": 1}
                     for index, name in enumerate(receiver_names)]
        config.write_text(source + ",\nvault: {store: \"file\"},\nssf: {receivers: " +
                          json.dumps(receivers) + "}\n")

    def stored():
        path = directory / "vault-home" / "vault" / "secrets.json"
        return json.loads(path.read_text())["accounts"][fixture.origin]

    def run_once(store=None, success=True, test_store=True):
        command = [
            str(server), "--config=" + str(config),
            "--issuer=" + fixture.origin, "--admin=http-admin",
            "--bootstrap-output=" + str(directory / "enrollment"),
            "--once"]
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

    success = False
    try:
        write_config(names)
        run_once(success=False, test_store=False)
        run_once()
        records = stored()
        assert base64.b64decode(records["global/dbKey"]) == base64.b64decode(
            fixture.env["ZUM_DB_KEY"])
        for name, value in values.items():
            assert base64.b64decode(records["env/ssf/" + name]) == value.encode()

        fixture.env.pop("ZUM_DB_KEY")
        for name in names:
            fixture.env.pop(name)
        fixture.start()
        assert not (directory / "vault-home" / "vault" / "vault.pid").exists()
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
        fixture.env.update(values)
        fixture.env["ZUMD_HOME"] = str(blocked_home)
        run_once(success=False)
        fixture.env["ZUMD_HOME"] = str(directory / "vault-home")
        fixture.env.pop("ZUM_DB_KEY")
        for name in names:
            fixture.env.pop(name)

        replacement = "Bearer " + secrets.token_urlsafe(24)
        fixture.env[names[0]] = replacement
        run_once()
        fixture.env.pop(names[0])
        records = stored()
        assert base64.b64decode(records["env/ssf/" + names[0]]) == \
            replacement.encode()
        assert base64.b64decode(records["env/ssf/" + names[1]]) == \
            values[names[1]].encode()
        run_once()

        fixture.env[names[0]] = "Bearer uncommitted"
        write_config((*names, "ZUM_SSF_MISSING"))
        run_once(success=False)
        fixture.env.pop(names[0])
        assert stored()["env/ssf/" + names[0]] == records["env/ssf/" + names[0]]
        write_config(names)
        run_once(store="keyring", success=False)
        success = True
    finally:
        fixture.stop()
        residue.finish(success)


if __name__ == "__main__":
    main()
    print("1..1\nok 1 - zumd Vault provisioning and startup")
