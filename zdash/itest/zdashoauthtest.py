#!/usr/bin/env python3
# (c) Copyright 2026 Huw Rogers
# This code is licensed by the MIT license (see LICENSE for details)

"""Exercise dashboard refresh-token reuse without GTK or a browser."""

from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import subprocess
from threading import Thread
from urllib.parse import parse_qs
from zi_test_residue import Residue


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *_args):
        pass

    def reply(self, status, body):
        payload = json.dumps(body).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def do_GET(self):
        if self.path != "/.well-known/oauth-authorization-server/oauth2/1":
            return self.reply(404, {})
        origin = self.server.origin
        self.reply(200, {
            "issuer": origin + "/oauth2/1",
            "authorization_endpoint": origin + "/authorize",
            "token_endpoint": origin + "/token",
            "jwks_uri": origin + "/keys",
            "response_types_supported": ["code"],
            "grant_types_supported": ["authorization_code", "refresh_token"],
            "code_challenge_methods_supported": ["S256"],
        })

    def do_POST(self):
        size = int(self.headers.get("Content-Length", "0"))
        form = parse_qs(self.rfile.read(size).decode())
        expected = "refresh-" + str(len(self.server.seen))
        if (self.path != "/token" or
                form.get("grant_type") != ["refresh_token"] or
                form.get("client_id") != ["client"] or
                form.get("refresh_token") != [expected]):
            return self.reply(400, {"error": "invalid_grant"})
        self.server.seen.append(expected)
        number = len(self.server.seen)
        self.reply(200, {"access_token": "access-" + str(number),
                         "refresh_token": "refresh-" + str(number),
                         "token_type": "Bearer",
                         "scope": "Client offline_access"})


def main():
    binary = Path(os.environ["ZDASH_OAUTH_BIN"])
    with Residue("zdash-oauth") as residue:
        directory = residue.directory
        with ThreadingHTTPServer(("127.0.0.1", 0), Handler) as server:
            server.origin = "http://127.0.0.1:" + str(server.server_port)
            server.seen = []
            thread = Thread(target=server.serve_forever, daemon=True)
            thread.start()
            try:
                issuer = server.origin + "/oauth2/1"
                home = Path(directory) / "vault-home"
                env = dict(os.environ, ZDASH_HOME=str(home),
                           DBUS_SESSION_BUS_ADDRESS="unsupported:address")
                for args in (("seed", issuer), ("run", issuer, "access-1"),
                             ("run", issuer, "access-2")):
                    result = subprocess.run([str(binary), *args], env=env,
                                            stdout=subprocess.PIPE,
                                            stderr=subprocess.PIPE, timeout=30)
                    assert result.returncode == 0, "dashboard OAuth fixture failed"
                    assert b"Open this URL" not in result.stdout
                assert server.seen == ["refresh-0", "refresh-1"]
                assert (home / "vault" / "secrets.json").stat().st_mode & 0o777 == 0o600
            finally:
                server.shutdown()
                thread.join()
        residue.success()
if __name__ == "__main__":
    main()
