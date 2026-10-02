#!/usr/bin/env python3
# (c) Copyright 2026 Huw Rogers
# This code is licensed by the MIT license (see LICENSE for details)

"""Disposable zumd Vault-old/environment-new rekey fixture."""

import base64
import os
from pathlib import Path
import secrets
import sqlite3

from cryptography.hazmat.primitives.ciphers.aead import AESGCM

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
            "client_id": "rekey-client", "client_secret": secrets.token_urlsafe(32),
            "scopes": ["openid"], "role_claim": "roles",
            "claim_source": "IDToken"}, token=token,
            headers={"Idempotency-Key": secrets.token_hex(16)}, status=201)
        fixture.admin_cli()
        app = fixture.admin_secret("appEnroll", {
            "name": "rekey-ssf", "audience": "https://rekey.example/api"}, idempotence=secrets.token_hex(16))["item"]
        basic = base64.b64encode((app["client_id"] + ":" + app["client_secret"]).encode()).decode()
        workload = fixture.request("POST", fixture.oauth(app["app_id"], "token"), {
            "grant_type": "client_credentials", "scope": "zum.catalog"}, form=True,
            headers={"Authorization": "Basic " + basic})[0]["access_token"]
        callback = "Bearer " + secrets.token_urlsafe(24)
        fixture.request("POST", f"/admin/apps/{app['app_id']}/ssf", {
            "receiver_id": "rekey", "delivery_url": "https://rekey.example/ssf",
            "callback_auth": callback, "expires_in": 300}, token=workload)
        fixture.stop()

        def credential():
            with sqlite3.connect(fixture.env["ZDB_CONNECT"]) as db:
                return db.execute('SELECT receiver_i_d, callback_auth '
                                  'FROM "a_zum.ssf_rx"').fetchone()

        identity, before = credential()
        old_key = fixture.env["ZUM_DB_KEY"]
        fixture.rekey()
        after_id, after = credential()
        assert identity == after_id and before != after
        aad = (fixture.origin + "\0zum.ssf_rx\0" + identity + "\0callbackAuth").encode()
        for envelope, key in ((before, old_key), (after, fixture.env["ZUM_DB_KEY"])):
            assert AESGCM(base64.b64decode(key)).decrypt(
                envelope[6:18], envelope[18:], aad) == callback.encode()
        success = True
    finally:
        fixture.stop()
        residue.finish(success)


if __name__ == "__main__":
    main()
    print("1..1\nok 1 - zumd Vault rekey and boundary retry")
