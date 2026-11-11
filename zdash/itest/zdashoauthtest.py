#!/usr/bin/env python3
# (c) Copyright 2026 Huw Rogers
# This code is licensed by the MIT license (see LICENSE for details)

"""Exercise renewal, rotation, failure and Vault reuse without a browser."""

from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
from queue import Queue
import subprocess
import time
from threading import Thread
from urllib.parse import parse_qs, urlencode, urlsplit
from urllib.request import urlopen
from urllib.error import URLError
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
            "token_endpoint": self.server.token_origin + "/token",
            "jwks_uri": origin + "/keys",
            "response_types_supported": ["code"],
            "grant_types_supported": ["authorization_code", "refresh_token"],
            "code_challenge_methods_supported": ["S256"],
        })

    def do_POST(self):
        size = int(self.headers.get("Content-Length", "0"))
        form = parse_qs(self.rfile.read(size).decode())
        if (self.path == "/token" and
                form.get("grant_type") == ["authorization_code"]):
            assert form.get("code") == ["fixture-code"]
            assert form.get("client_id") == ["client"]
            assert len(form.get("code_verifier", [""])[0]) >= 43
            self.server.code_seen = True
            return self.reply(200, {"access_token": "access-fresh",
                                   "refresh_token": "refresh-fresh",
                                   "token_type": "Bearer", "expires_in": 2,
                                   "scope": "Client offline_access"})
        expected = "refresh-" + str(len(self.server.seen))
        if (self.path != "/token" or
                form.get("grant_type") != ["refresh_token"] or
                form.get("client_id") != ["client"] or
                form.get("refresh_token") != [expected]):
            return self.reply(400, {"error": "invalid_grant"})
        if len(self.server.seen) == 6 and not self.server.rejected:
            self.server.rejected = True
            return self.reply(400, {"error": "invalid_grant"})
        self.server.seen.append(expected)
        number = len(self.server.seen)
        self.reply(200, {"access_token": "access-" + str(number),
                         "refresh_token": "refresh-" + str(number),
                         "token_type": "Bearer", "expires_in": 2,
                         "scope": "Client offline_access"})


def main():
    binary = Path(os.environ["ZDASH_OAUTH_BIN"])
    with Residue("zdash-oauth") as residue:
        directory = residue.directory
        with ThreadingHTTPServer(("127.0.0.1", 0), Handler) as server, \
                ThreadingHTTPServer(("127.0.0.1", 0), Handler) as token_server:
            server.origin = "http://127.0.0.1:" + str(server.server_port)
            server.token_origin = "http://127.0.0.1:" + str(token_server.server_port)
            token_server.seen = []
            token_server.rejected = False
            token_server.code_seen = False
            threads = [Thread(target=s.serve_forever, daemon=True)
                       for s in (server, token_server)]
            for thread in threads:
                thread.start()
            try:
                issuer = server.origin + "/oauth2/1"
                home = Path(directory) / "vault-home"
                env = dict(os.environ, ZDASH_HOME=str(home),
                           DBUS_SESSION_BUS_ADDRESS="unsupported:address")
                for args in (("seed", issuer),
                             ("run", issuer, "access-1", "2"),
                             ("run", issuer, "access-4", "1"),
                             ("run", issuer, "access-6", "-1"),
                             ("run", issuer, "access-7", "0")):
                    result = subprocess.run([str(binary), *args], env=env,
                                            stdout=subprocess.PIPE,
                                            stderr=subprocess.PIPE, timeout=30)
                    assert result.returncode == 0, "dashboard OAuth fixture failed"
                    assert b"Open this URL" not in result.stdout
                    if args[-1] == "-1":
                        assert b"credential renewal failed" in result.stderr
                assert token_server.seen == ["refresh-" + str(i) for i in range(7)]
                assert token_server.rejected
                assert (home / "vault" / "secrets.json").stat().st_mode & 0o777 == 0o600
                fresh_env = dict(env, ZDASH_HOME=str(Path(directory) / "fresh-home"))
                with subprocess.Popen([str(binary), "run", issuer, "access-fresh", "0"],
                                      env=fresh_env, stdout=subprocess.PIPE,
                                      stderr=subprocess.PIPE) as child:
                    lines = Queue()
                    reader = Thread(target=lambda: [lines.put(line)
                        for line in iter(child.stdout.readline, b"")], daemon=True)
                    reader.start()
                    try:
                        assert lines.get(timeout=10).startswith(b"Open this URL")
                        authorize = urlsplit(lines.get(timeout=10).decode().strip())
                        query = parse_qs(authorize.query)
                        callback = query["redirect_uri"][0] + "?" + urlencode({
                            "code": "fixture-code", "state": query["state"][0]})
                        deadline = time.monotonic() + 10
                        while True:
                            try:
                                with urlopen(callback, timeout=10) as response:
                                    assert response.status == 200
                                break
                            except URLError:
                                if time.monotonic() >= deadline:
                                    raise
                                time.sleep(0.02)
                        assert child.wait(timeout=15) == 0, child.stderr.read().decode()
                        assert token_server.code_seen
                    finally:
                        if child.poll() is None:
                            child.kill()
                            child.wait()
                        reader.join(timeout=5)
            finally:
                server.shutdown()
                token_server.shutdown()
                for thread in threads:
                    thread.join()
        residue.success()
if __name__ == "__main__":
    main()
