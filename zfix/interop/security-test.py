#!/usr/bin/env python3
"""Check FIX identities and verified TLS with all four independent peer pairings."""
import argparse
import contextlib
import importlib.util
import json
from pathlib import Path
import platform
import subprocess
import sys
import tempfile
import traceback


def load_pair():
    path = Path(__file__).resolve().parents[1] / "util" / "pair-test.py"
    spec = importlib.util.spec_from_file_location("zfix_pair", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def credentials(root, pair):
    pair.certificate(root)

    def openssl(*args, expected=0):
        command = ["openssl", *map(str, args)]
        result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                timeout=30)
        with (root / "openssl.jsonl").open("a") as output:
            output.write(json.dumps({"command": command, "exit": result.returncode,
                                     "output": result.stdout.decode()}) + "\n")
        assert result.returncode == expected, (command, result.returncode, result.stdout)

    openssl("req", "-new", "-newkey", "rsa:2048", "-nodes", "-subj", "/CN=localhost",
            "-keyout", root / "server.key", "-out", root / "server.csr")
    for serial, (name, san) in enumerate((
            ("valid", "DNS:localhost,IP:127.0.0.1"),
            ("wrong-dns", "DNS:wrong.invalid,IP:127.0.0.1"),
            ("wrong-ip", "DNS:localhost,IP:127.0.0.2")), 1):
        ext = root / f"{name}.ext"
        ext.write_text("basicConstraints=critical,CA:FALSE\n"
                       "keyUsage=critical,digitalSignature,keyEncipherment\n"
                       "extendedKeyUsage=serverAuth\n"
                       f"subjectAltName={san}\n")
        openssl("x509", "-req", "-in", root / "server.csr", "-CA", root / "cert.pem",
                "-CAkey", root / "key.pem", "-set_serial", str(serial), "-days", "1",
                "-extfile", ext, "-out", root / f"{name}.pem")
    openssl("req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1",
            "-subj", "/CN=Independent untrusted fixture CA",
            "-addext", "basicConstraints=critical,CA:TRUE",
            "-keyout", root / "other.key", "-out", root / "other.pem")
    for kind, identity in (("hostname", "localhost"), ("ip", "127.0.0.1")):
        openssl("verify", "-CAfile", root / "cert.pem", f"-verify_{kind}", identity,
                root / "valid.pem")
        name = "wrong-dns" if kind == "hostname" else "wrong-ip"
        openssl("verify", "-CAfile", root / "cert.pem", f"-verify_{kind}", identity,
                root / f"{name}.pem", expected=2)
        openssl("verify", "-CAfile", root / "cert.pem", root / f"{name}.pem")
    openssl("verify", "-CAfile", root / "cert.pem", "-verify_ip", "127.0.0.1",
            root / "wrong-dns.pem")
    openssl("verify", "-CAfile", root / "cert.pem", "-verify_hostname", "localhost",
            root / "wrong-ip.pem")
    openssl("verify", "-CAfile", root / "other.pem", "-verify_hostname", "localhost",
            root / "valid.pem", expected=2)


def rejected(pair, directory, clients, servers):
    # A diagnostic deadline alone does not establish successful rejection:
    # require no initiator Session::Logon readiness and no admitted/completed workload.
    output = pair.records(directory / "client.jsonl")
    assert not any(row.get("event") == "ready" for row in output), output
    for result in clients + servers:
        assert result["errors"] > 0, result
        progress = result["progress"]
        assert progress["admitted"] == progress["complete"] == progress["reports"] == 0, result


def tls_refused(directory, name, client_kind, clients, servers):
    for result in clients + servers:
        assert result["rxFrames"] == result["txFrames"] == 0, result
    if client_kind == "quickfix":
        code = {"wrong-dns": 62, "wrong-ip": 64, "untrusted": 20}[name]
        diagnostic = (directory / "client.log").read_text()
        assert f"Certificate Verification: Error ({code}):" in diagnostic, diagnostic


def main():
    pair = load_pair()
    base = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--native-client", type=Path, default=base / "util" / "zfix")
    parser.add_argument("--native-server", type=Path, default=base / "util" / "zfixd")
    parser.add_argument("--quickfix-client", type=Path, default=base / "interop" / "quickfix")
    parser.add_argument("--quickfix-server", type=Path, default=base / "interop" / "quickfixd")
    parser.add_argument("--dictionary", type=Path, required=True)
    parser.add_argument("--artifacts", type=Path)
    parser.add_argument("--ready-timeout", type=float, default=10)
    parser.add_argument("--process-timeout", type=float, default=40)
    options = parser.parse_args()
    root = (options.artifacts.resolve() if options.artifacts else
            Path(tempfile.mkdtemp(prefix="zfix-security-")))
    root.mkdir(parents=True, exist_ok=True)
    if any(root.iterdir()):
        parser.error(f"artifact directory must be empty: {root}")
    programs = {name: getattr(options, name).resolve() for name in (
        "native_client", "native_server", "quickfix_client", "quickfix_server")}
    lock = (base / "interop" / "quickfix.lock").read_text()
    expected = next(line.split("=", 1)[1] for line in lock.splitlines()
                    if line.startswith("QUICKFIX_FIX44_SHA256="))
    assert pair.digest(options.dictionary) == expected, "canonical FIX44 dictionary mismatch"
    manifest = {"platform": platform.platform(), "dictionary": str(options.dictionary.resolve()),
                "dictionary_sha256": expected, "quickfix_lock": lock,
                "programs": {name: {"path": str(path), "sha256": pair.digest(path),
                                    "binary_sha256": pair.digest(pair.executable(path))}
                             for name, path in programs.items()},
                "bind": {"native": "127.0.0.1", "quickfix": "0.0.0.0"},
                "verification": "CA and DNS/IP reference identity enabled; no TLS proxy",
                "negative_contract": "both exits 1, no initiator Session::Logon readiness, "
                                     "no business admission",
                "ready_timeout": options.ready_timeout,
                "process_timeout": options.process_timeout}
    (root / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    credentials(root, pair)
    profiles = [("tcp", "valid", "localhost", (), None, False),
                ("tcp", "wrong-sender", "localhost", ("--sender", "WRONG"), None, True),
                ("tcp", "wrong-target", "localhost", ("--target", "WRONG"), None, True),
                ("tls", "valid-dns", "localhost", (), None, False),
                ("tls", "valid-ip", "127.0.0.1", (), None, False),
                ("tls", "wrong-sender", "localhost", ("--sender", "WRONG"), None, True),
                ("tls", "wrong-target", "localhost", ("--target", "WRONG"), None, True),
                ("tls", "wrong-dns", "localhost", (), None, True),
                ("tls", "wrong-ip", "127.0.0.1", (), None, True),
                ("tls", "untrusted", "localhost", (), root / "other.pem", True)]
    def tap(line):
        with (root / "security.tap").open("a") as output:
            output.write(line + "\n")
        print(line, flush=True)

    tap(f"# retained security artifacts: {root}")
    tap(f"1..{4 * len(profiles)}")
    number = failed = 0
    for client_kind in ("native", "quickfix"):
        for server_kind in ("native", "quickfix"):
            label = f"{client_kind}-{server_kind}"
            for transport, name, host, extra, ca, negative in profiles:
                number += 1
                case_name = f"{label}-{name}"
                directory = root / f"{transport}-{case_name}"
                try:
                    cert = name if name in ("wrong-dns", "wrong-ip") else "valid"
                    with (root / "events.jsonl").open("a") as events:
                        with contextlib.redirect_stdout(events):
                            clients, servers = pair.run_case(
                                str(programs[f"{client_kind}_client"]),
                                str(programs[f"{server_kind}_server"]), root, transport,
                                case_name, [], 3, 0, negative=negative, pair=label,
                                ready_timeout=options.ready_timeout,
                                process_timeout=options.process_timeout,
                                client_args=("--host", host, *extra), client_ca=ca,
                                server_cert=root / f"{cert}.pem", server_key=root / "server.key")
                    if negative:
                        rejected(pair, directory, clients, servers)
                    if transport == "tls" and name in ("wrong-dns", "wrong-ip", "untrusted"):
                        tls_refused(directory, name, client_kind, clients, servers)
                    (directory / "security.json").write_text(json.dumps({
                        "expected": "reject before Session::Logon/business admission" if negative else
                                    "verified Session::Logon, complete workload and Session::Logout",
                        "reference_identity": host, "certificate": cert,
                        "client_ca": str(ca or root / "cert.pem"), "passed": True},
                        indent=2) + "\n")
                    tap(f"ok {number} - {label} {transport} {name}")
                except Exception:
                    failed += 1
                    (root / f"failure-{number}.log").write_text(traceback.format_exc())
                    tap(f"not ok {number} - {label} {transport} {name}")
                    tap(f"# details: {root / f'failure-{number}.log'}")
    (root / "summary.json").write_text(json.dumps({"cases": number, "failed": failed,
                                                  "passed": number - failed}, indent=2) + "\n")
    return bool(failed)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:
        print(f"Bail out! security fixture: {error}", flush=True)
        sys.exit(1)
