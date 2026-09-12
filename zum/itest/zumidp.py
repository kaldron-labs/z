"""Independent local OIDC/TLS fixture; not an external-provider conformance test.

Only test credentials are held here. Never log requests, tokens or exceptions
containing protocol payloads. The enclosing fixture owns the private directory.
"""

import base64
import datetime
import hashlib
import http.client
import http.server
import ipaddress
import json
import os
import secrets
import ssl
import threading
import time
from pathlib import Path
from urllib.parse import parse_qs, urlencode, urlsplit

from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec, utils
from cryptography.x509.oid import NameOID


def b64(value):
    return base64.urlsafe_b64encode(value).rstrip(b"=").decode()


class Provider:
    def __init__(self, directory):
        self.directory = Path(directory)
        self.client_id = "zum-upstream"
        self.client_secret = secrets.token_urlsafe(32)
        self.redirect = None
        self.roles = ["operators", "readers", "unmapped"]
        self.userinfo_roles = None
        self.userinfo_subject = None
        self.id_overrides = {}
        self.subject = "external-user"
        self.outage = False
        self.calls = {}
        self.codes = {}
        self.tokens = {}
        self.key = ec.generate_private_key(ec.SECP256R1())
        numbers = self.key.public_key().public_numbers()
        self.jwk = {"kty": "EC", "crv": "P-256", "alg": "ES256",
                    "use": "sig", "kid": "fixture-key",
                    "x": b64(numbers.x.to_bytes(32, "big")),
                    "y": b64(numbers.y.to_bytes(32, "big"))}
        self.ca_path, self.cert_path, self.key_path = self.certificates()
        owner = self

        class Handler(http.server.BaseHTTPRequestHandler):
            protocol_version = "HTTP/1.1"

            def log_message(self, *_):
                pass

            def do_GET(self):
                owner.handle(self)

            def do_POST(self):
                owner.handle(self)

        self.server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(self.cert_path, self.key_path)
        context.set_alpn_protocols(["http/1.1"])
        self.server.socket = context.wrap_socket(self.server.socket, server_side=True)
        self.issuer = "https://localhost:" + str(self.server.server_port)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()

    def certificates(self):
        now = datetime.datetime.now(datetime.timezone.utc)
        ca_key = ec.generate_private_key(ec.SECP256R1())
        name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, "Zum test CA")])
        ca = (x509.CertificateBuilder().subject_name(name).issuer_name(name)
              .public_key(ca_key.public_key()).serial_number(x509.random_serial_number())
              .not_valid_before(now - datetime.timedelta(minutes=1))
              .not_valid_after(now + datetime.timedelta(days=1))
              .add_extension(x509.BasicConstraints(ca=True, path_length=0), critical=True)
              .add_extension(x509.SubjectKeyIdentifier.from_public_key(ca_key.public_key()),
                             critical=False)
              .add_extension(x509.AuthorityKeyIdentifier.from_issuer_public_key(ca_key.public_key()),
                             critical=False)
              .add_extension(x509.KeyUsage(digital_signature=True, content_commitment=False,
                  key_encipherment=False, data_encipherment=False, key_agreement=False,
                  key_cert_sign=True, crl_sign=True, encipher_only=None, decipher_only=None),
                  critical=True)
              .sign(ca_key, hashes.SHA256()))
        key = ec.generate_private_key(ec.SECP256R1())
        cert = (x509.CertificateBuilder()
                .subject_name(x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, "localhost")]))
                .issuer_name(name).public_key(key.public_key())
                .serial_number(x509.random_serial_number())
                .not_valid_before(now - datetime.timedelta(minutes=1))
                .not_valid_after(now + datetime.timedelta(days=1))
                .add_extension(x509.BasicConstraints(ca=False, path_length=None), critical=True)
                .add_extension(x509.AuthorityKeyIdentifier.from_issuer_public_key(ca_key.public_key()),
                               critical=False)
                .add_extension(x509.SubjectAlternativeName([
                    x509.DNSName("localhost"), x509.IPAddress(ipaddress.ip_address("127.0.0.1"))]),
                    critical=False)
                .sign(ca_key, hashes.SHA256()))
        ca_path = self.directory / "upstream-ca.pem"
        cert_path = self.directory / "upstream-cert.pem"
        key_path = self.directory / "upstream-key.pem"
        for path, data in (
                (ca_path, ca.public_bytes(serialization.Encoding.PEM)),
                (cert_path, cert.public_bytes(serialization.Encoding.PEM)),
                (key_path, key.private_bytes(serialization.Encoding.PEM,
                    serialization.PrivateFormat.PKCS8, serialization.NoEncryption()))):
            with os.fdopen(os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600), "wb") as out:
                out.write(data)
        return ca_path, cert_path, key_path

    def close(self):
        self.server.shutdown()
        self.thread.join()
        self.server.server_close()
        self.codes.clear()
        self.tokens.clear()

    def signed(self, claims):
        head = b64(json.dumps({"alg": "ES256", "kid": "fixture-key"}).encode())
        body = b64(json.dumps(claims).encode())
        value = (head + "." + body).encode()
        r, s = utils.decode_dss_signature(self.key.sign(value, ec.ECDSA(hashes.SHA256())))
        return value.decode() + "." + b64(r.to_bytes(32, "big") + s.to_bytes(32, "big"))

    def reply(self, request, status, value=None, location=None):
        body = json.dumps(value or {}).encode()
        request.send_response(status)
        request.send_header("Content-Type", "application/json")
        request.send_header("Content-Length", str(len(body)))
        request.send_header("Connection", "close")
        if location:
            request.send_header("Location", location)
        request.end_headers()
        request.wfile.write(body)
        request.close_connection = True

    def handle(self, request):
        path = urlsplit(request.path).path
        self.calls[path] = self.calls.get(path, 0) + 1
        if self.outage:
            self.reply(request, 503, {"error": "temporarily_unavailable"})
            return
        if path == "/.well-known/openid-configuration":
            self.reply(request, 200, {
                "issuer": self.issuer, "authorization_endpoint": self.issuer + "/authorize",
                "token_endpoint": self.issuer + "/token", "jwks_uri": self.issuer + "/jwks",
                "userinfo_endpoint": self.issuer + "/userinfo",
                "response_types_supported": ["code"], "subject_types_supported": ["public"],
                "id_token_signing_alg_values_supported": ["ES256"],
                "token_endpoint_auth_methods_supported": ["client_secret_basic"],
                "code_challenge_methods_supported": ["S256"],
                "scopes_supported": ["openid", "roles"]})
        elif path == "/jwks":
            self.reply(request, 200, {"keys": [self.jwk]})
        elif path == "/authorize":
            params = parse_qs(urlsplit(request.path).query)
            valid = (params.get("client_id") == [self.client_id] and
                     params.get("redirect_uri") == [self.redirect] and
                     params.get("response_type") == ["code"] and
                     params.get("code_challenge_method") == ["S256"] and
                     all(len(params.get(key, [])) == 1 for key in
                         ("state", "nonce", "code_challenge", "scope")))
            if not valid:
                self.reply(request, 400, {"error": "invalid_request"})
                return
            code = secrets.token_urlsafe(32)
            self.codes[code] = {key: values[0] for key, values in params.items()}
            self.reply(request, 302, location=self.redirect + "?" + urlencode({
                "code": code, "state": params["state"][0]}))
        elif path == "/token":
            basic = "Basic " + base64.b64encode(
                (self.client_id + ":" + self.client_secret).encode()).decode()
            if request.command != "POST" or request.headers.get("Authorization") != basic:
                self.reply(request, 401, {"error": "invalid_client"})
                return
            length = int(request.headers.get("Content-Length", "0"))
            if not 0 < length <= 8192:
                self.reply(request, 400, {"error": "invalid_request"})
                return
            params = parse_qs(request.rfile.read(length).decode())
            code = self.codes.pop(params.get("code", [""])[0], None)
            verifier = params.get("code_verifier", [""])[0]
            if (not code or params.get("grant_type") != ["authorization_code"] or
                    params.get("redirect_uri") != [self.redirect] or
                    b64(hashlib.sha256(verifier.encode()).digest()) != code["code_challenge"]):
                self.reply(request, 400, {"error": "invalid_grant"})
                return
            now = int(time.time())
            claims = {"iss": self.issuer, "sub": self.subject, "aud": self.client_id,
                      "iat": now, "auth_time": now, "exp": now + 300,
                      "nonce": code["nonce"], "roles": list(self.roles)}
            claims.update(self.id_overrides)
            access = secrets.token_urlsafe(32)
            self.tokens[access] = {"sub": self.subject, "roles": list(self.roles)}
            self.reply(request, 200, {"access_token": access, "token_type": "Bearer",
                                     "expires_in": 300, "scope": code["scope"],
                                     "id_token": self.signed(claims)})
        elif path == "/userinfo":
            token = request.headers.get("Authorization", "").removeprefix("Bearer ")
            claims = self.tokens.get(token)
            if claims and self.userinfo_roles is not None:
                claims = dict(claims, roles=list(self.userinfo_roles))
            if claims and self.userinfo_subject is not None:
                claims = dict(claims, sub=self.userinfo_subject)
            self.reply(request, 200 if claims else 401, claims or {"error": "invalid_token"})
        else:
            self.reply(request, 404)

    def authorize(self, location):
        parsed = urlsplit(location)
        assert parsed.scheme + "://" + parsed.netloc == self.issuer
        connection = http.client.HTTPSConnection(parsed.hostname, parsed.port, timeout=20,
            context=ssl.create_default_context(cafile=self.ca_path))
        try:
            connection.request("GET", parsed.path + "?" + parsed.query)
            response = connection.getresponse()
            response.read()
            assert response.status == 302, "upstream fixture rejected authorization"
            callback = response.getheader("Location")
            assert callback.startswith(self.redirect + "?")
            return callback
        finally:
            connection.close()


class TLSProxy:
    """Fixture-only TLS termination; Zum retains its canonical HTTPS issuer."""

    def __init__(self, backend_port, provider):
        class Handler(http.server.BaseHTTPRequestHandler):
            protocol_version = "HTTP/1.1"

            def log_message(self, *_):
                pass

            def relay(self):
                length = int(self.headers.get("Content-Length", "0"))
                body = self.rfile.read(length) if length else None
                headers = {key: value for key, value in self.headers.items()
                           if key.lower() not in ("connection", "transfer-encoding")}
                connection = http.client.HTTPConnection("127.0.0.1", backend_port, timeout=30)
                try:
                    connection.request(self.command, self.path, body, headers)
                    response = connection.getresponse()
                    data = response.read()
                    self.send_response(response.status)
                    for key, value in response.getheaders():
                        if key.lower() not in ("connection", "transfer-encoding", "content-length"):
                            self.send_header(key, value)
                    self.send_header("Content-Length", str(len(data)))
                    self.send_header("Connection", "close")
                    self.end_headers()
                    self.wfile.write(data)
                    self.close_connection = True
                finally:
                    connection.close()

            do_GET = do_POST = do_PUT = do_PATCH = do_DELETE = relay

        self.server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(provider.cert_path, provider.key_path)
        context.set_alpn_protocols(["http/1.1"])
        self.server.socket = context.wrap_socket(self.server.socket, server_side=True)
        self.port = self.server.server_port
        self.origin = "https://localhost:" + str(self.port)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()

    def close(self):
        self.server.shutdown()
        self.thread.join()
        self.server.server_close()
