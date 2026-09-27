#!/usr/bin/env python3
# (c) Copyright 2026 Huw Rogers
# This code is licensed by the MIT license (see LICENSE for details)

"""Disposable zumd Vault-old/environment-new rekey fixture."""

import os
from pathlib import Path
import secrets

from zumhttp import Fixture
from zi_test_residue import Residue


def main():
    for name in ("ZDB_MODULE", "ZDB_CONNECT"):
        if not os.environ.get(name):
            raise AssertionError("set " + name + " for a fresh rekey fixture")
    residue = Residue("zumd-rekey")
    directory = residue.directory
    fixture = Fixture(directory)
    success = False
    try:
        fixture.start()
        fixture.enroll()
        token = fixture.login()
        fixture.request("POST", "/admin/providers", {
            "name": "rekey-provider", "issuer": "https://rekey.example",
            "clientID": "rekey-client", "clientSecret": secrets.token_urlsafe(32),
            "scopes": ["openid"], "roleClaim": "roles",
            "claimSource": "IDToken"}, token=token,
            headers={"Idempotency-Key": secrets.token_hex(16)}, status=201)
        fixture.stop()
        fixture.rekey()
        success = True
    finally:
        fixture.stop()
        residue.finish(success)


if __name__ == "__main__":
    main()
    print("1..1\nok 1 - zumd Vault rekey and boundary retry")
