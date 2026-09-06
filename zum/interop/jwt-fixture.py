#!/usr/bin/env python3

import base64
import json
import pathlib
import sys


def b64(data):
    return base64.urlsafe_b64encode(data).rstrip(b"=")


def integer(data, offset):
    if data[offset] != 2:
        raise ValueError("invalid ECDSA signature")
    length = data[offset + 1]
    start = offset + 2
    value = data[start:start + length].lstrip(b"\0")
    if not value or len(value) > 32:
        raise ValueError("invalid ECDSA integer")
    return value.rjust(32, b"\0"), start + length


def main():
    directory = pathlib.Path(sys.argv[1])
    header = {"typ": "at+jwt", "alg": "ES256", "kid": "interop"}
    claims = {
        "iss": "https://issuer.example",
        "sub": "workload",
        "aud": "orders",
        "client_id": "workload",
        "iat": 100,
        "nbf": 100,
        "exp": 200,
        "jti": "fixture",
        "scope": "read",
        "actions": ["orders.read"],
    }
    compact = b".".join((
        b64(json.dumps(header, separators=(",", ":")).encode()),
        b64(json.dumps(claims, separators=(",", ":")).encode()),
    ))
    (directory / "input").write_bytes(compact)

    if len(sys.argv) == 2:
        return
    signature = (directory / "signature.der").read_bytes()
    if len(signature) < 8 or signature[0] != 0x30 or signature[1] != len(signature) - 2:
        raise ValueError("invalid ECDSA signature")
    r, offset = integer(signature, 2)
    s, offset = integer(signature, offset)
    if offset != len(signature):
        raise ValueError("trailing ECDSA data")
    public = (directory / "public.der").read_bytes()
    if len(public) < 65 or public[-65] != 4:
        raise ValueError("invalid P-256 public key")
    token = compact + b"." + b64(r + s)
    (directory / "token").write_bytes(token)
    (directory / "public.hex").write_text(public[-65:].hex().upper())


if __name__ == "__main__":
    main()
