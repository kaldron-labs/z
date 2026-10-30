#!/usr/bin/env python3
"""Check the patched independent SDK with real TCP/TLS sessions and JSON output."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "zi" / "itest"))
from zi_test_residue import Residue


def credentials(root):
    def openssl(*args):
        result = subprocess.run(["openssl", *map(str, args)], check=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30)
        with (root / "openssl.log").open("ab") as log:
            log.write(result.stdout)

    openssl("req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1",
            "-subj", "/CN=Zfix dependency fixture CA",
            "-addext", "basicConstraints=critical,CA:TRUE",
            "-addext", "keyUsage=critical,keyCertSign,cRLSign",
            "-keyout", root / "ca.key", "-out", root / "ca.pem")
    (root / "ca-dir").mkdir()
    shutil.copyfile(root / "ca.pem", root / "ca-dir" / "ca.pem")
    (root / "ca-dupe.pem").write_bytes((root / "ca.pem").read_bytes() * 2)
    openssl("req", "-new", "-newkey", "rsa:2048", "-nodes", "-subj", "/CN=localhost",
            "-keyout", root / "server.key", "-out", root / "server.csr")
    for serial, (name, san) in enumerate((
            ("valid", "DNS:localhost,IP:127.0.0.1"),
            ("name", "DNS:wrong.invalid,IP:127.0.0.1"),
            ("ip", "DNS:localhost,IP:127.0.0.2")), 1):
        ext = root / f"{name}.ext"
        ext.write_text("basicConstraints=CA:FALSE\nkeyUsage=digitalSignature,keyEncipherment\n"
                       f"extendedKeyUsage=serverAuth\nsubjectAltName={san}\n")
        openssl("x509", "-req", "-in", root / "server.csr", "-CA", root / "ca.pem",
                "-CAkey", root / "ca.key", "-set_serial", str(serial), "-days", "1",
                "-extfile", ext, "-out", root / f"{name}.pem")
    openssl("req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1",
            "-subj", "/CN=Other dependency fixture CA",
            "-addext", "basicConstraints=critical,CA:TRUE",
            "-keyout", root / "other.key", "-out", root / "other.pem")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("probe")
    parser.add_argument("dictionary")
    parser.add_argument("--artifacts", type=Path)
    args = parser.parse_args()
    probe, dictionary = str(Path(args.probe).resolve()), str(Path(args.dictionary).resolve())
    with Residue("zfix-qf-dependency") as residue:
        root = residue.directory
        credentials(root)
        result = subprocess.run([probe, dictionary, str(root)], capture_output=True,
                                text=True, timeout=180)
        (root / "probe.tap").write_text(result.stdout)
        (root / "probe.log").write_text(result.stderr)
        print(result.stdout, end="", flush=True)
        if result.returncode:
            raise AssertionError(f"TAP probe exited {result.returncode}; see {root / 'probe.log'}")
        checked = subprocess.run(["prove", "--exec", "cat", str(root / "probe.tap")],
                                 capture_output=True, text=True, timeout=20)
        (root / "tap-check.log").write_text(checked.stdout + checked.stderr)
        assert checked.returncode == 0, "invalid or failing TAP plan"
        for case in ("tcp", "tls-dns", "tls-ip", "wrong-name", "wrong-ip", "untrusted"):
            result = subprocess.run([probe, "--json", case, dictionary, str(root)],
                                    capture_output=True, text=True, timeout=45)
            (root / f"{case}.jsonl").write_text(result.stdout)
            (root / f"{case}.log").write_text(result.stderr)
            assert result.returncode == 0, (case, result.returncode)
            ready, completed = [json.loads(line) for line in result.stdout.splitlines()]
            assert ready["event"] == "ready" and ready["scenario"] == case
            assert completed["event"] == "result" and completed["scenario"] == case
            assert completed["errors"] == 0
            ports = ready["ports"]
            assert len(ports) == completed["sessions"] and len(set(ports)) == len(ports)
            assert all(0 < port < 65536 for port in ports)
            if completed["expectedFailure"]:
                assert completed["client"] == completed["server"] == 0
                assert completed["failures"] > 0
            else:
                assert completed["sessions"] == 2
                assert completed["client"] == completed["server"] == 3
                assert completed["failures"] == 0
            if case != "tcp":
                assert "Certificate Verification:" in result.stderr, case
            print(f"# independent SDK JSON/diagnostic separation: {case}", flush=True)
        if args.artifacts:
            shutil.copytree(root, args.artifacts)
        residue.success()


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(f"Bail out! independent SDK dependency check: {error}", flush=True)
        raise SystemExit(1)
