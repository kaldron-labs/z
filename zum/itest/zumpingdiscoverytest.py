#!/usr/bin/env python3
# (c) Copyright 2026 Huw Rogers
# This code is licensed by the MIT license (see LICENSE for details)

"""Executable-level negative discovery tests for the zumping client."""

import http.server
import json
from pathlib import Path
import socket
import subprocess
import tempfile
import threading
from urllib.parse import parse_qs, urlsplit


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    body = b""

    def do_GET(self):
        if self.path != "/.well-known/oauth-authorization-server/oauth2/42":
            self.send_error(404)
            return
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(self.body)))
        self.end_headers()
        self.wfile.write(self.body)

    def log_message(self, *_):
        pass


def free_port():
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        return listener.getsockname()[1]


def main():
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    origin = "http://127.0.0.1:" + str(server.server_port)
    issuer = origin + "/oauth2/42"
    executable = Path(__file__).resolve().parents[1] / "example" / "zumping"
    tests = 0

    def run(body, configured_issuer=issuer):
        Handler.body = body if isinstance(body, bytes) else json.dumps(
            body, separators=(",", ":")).encode()
        with tempfile.TemporaryDirectory(prefix="zumping-discovery-") as directory:
            config = Path(directory) / "zumping.cf"
            config.write_text(
                "issuerURL: " + json.dumps(configured_issuer) + ",\n" +
                "serviceURL: " + json.dumps(origin) + ",\n" +
                "clientID: \"zumping-test\",\n"
                "scope: \"ping\",\n"
                "callbackPort: " + str(free_port()) + ",\n"
                "loginTimeout: 1,\n"
                "loopbackTest: true\n")
            return subprocess.run(
                [str(executable), "--config", str(config), "--no-browser"],
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=8)

    def check(name, test):
        nonlocal tests
        tests += 1
        try:
            test()
            print("ok " + str(tests) + " - " + name)
        except BaseException:
            print("not ok " + str(tests) + " - " + name)
            raise

    def metadata(**changes):
        value = {
            "issuer": issuer,
            "authorization_endpoint": origin + "/custom/authorize?tenant=42",
            "token_endpoint": origin + "/custom/token",
            "jwks_uri": origin + "/custom/keys",
            "revocation_endpoint": origin + "/custom/revoke",
            "response_types_supported": ["code"],
            "grant_types_supported": ["authorization_code", "refresh_token"],
            "code_challenge_methods_supported": ["S256"],
        }
        value.update(changes)
        return value

    def rejected(value):
        result = run(value)
        assert result.returncode != 0
        assert b"OAuth discovery failed" in result.stderr

    try:
        def custom_endpoint():
            result = run(metadata())
            assert result.returncode == 1, (
                result.returncode, result.stdout, result.stderr)
            urls = [line.decode() for line in result.stdout.splitlines()
                    if line.startswith(b"http://")]
            assert len(urls) == 1
            parsed = urlsplit(urls[0])
            assert parsed.path == "/custom/authorize"
            query = parse_qs(parsed.query)
            assert query["tenant"] == ["42"]
            assert query["client_id"] == ["zumping-test"]

        check("advertised non-default endpoint", custom_endpoint)
        check("exact issuer", lambda: rejected(
            metadata(issuer=origin + "/oauth2/43")))
        check("absolute endpoint", lambda: rejected(
            metadata(authorization_endpoint="/authorize")))
        check("secure endpoint", lambda: rejected(
            metadata(token_endpoint="http://example.com/token")))
        check("fragment-free endpoint", lambda: rejected(
            metadata(revocation_endpoint=origin + "/revoke#secret")))
        duplicate = json.dumps(metadata(), separators=(",", ":"))[:-1] + \
            ',"issuer":"' + issuer + '"}'
        check("unique metadata fields", lambda: rejected(duplicate.encode()))
        check("bounded metadata", lambda: rejected(
            (json.dumps(metadata()) + " " * (70 << 10)).encode()))
        check("refresh grant advertised", lambda: rejected(
            metadata(grant_types_supported=["authorization_code"])))
        check("PKCE S256 advertised", lambda: rejected(
            metadata(code_challenge_methods_supported=["plain"])))
    finally:
        server.shutdown()
        server.server_close()
        thread.join()
    print("1.." + str(tests))


if __name__ == "__main__":
    main()
