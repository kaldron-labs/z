#!/usr/bin/env python3
"""Prove Native FIX probe expiry using independent TCP/TLS peer processes."""
import argparse
import importlib.util
import json
from pathlib import Path
import platform
import subprocess
import sys
import tempfile
import time
import traceback

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("zfix_pair", HERE.parent / "util" / "pair-test.py")
pair = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pair)


def run_case(options, root, programs, label, transport):
    client_kind, server_kind = label.split("-")
    # Logon crosses normally and ordinary readiness requires no probe exchange.
    # Silence subsequent receive traffic until Native's receive-idle timer
    # publishes a liveness probe, including the peer's actual matching echo.
    direction = "down" if client_kind == "native" else "up"
    target = "client" if direction == "down" else "server"
    directory = root / f"{label}-{transport}"
    directory.mkdir()
    common = ["--count", "4", "--warmup", "0", "--inflight", "4", "--heartbeat", "1",
              "--deadline", str(options.deadline)]
    if transport == "tcp":
        common += ["--tcp"]
    else:
        common += ["--cert", str(root / "cert.pem"), "--key", str(root / "key.pem"),
                   "--ca", str(root / "cert.pem")]
    server_cmd = [str(programs[f"{server_kind}_server"]), "--port", "0", *common]
    sp = cp = relay = None
    case = {"pair": label, "transport": transport, "native_target": target,
            "silenced_direction": direction, "server_command": server_cmd, "status": "running"}
    start = time.monotonic()
    try:
        with (directory / "server.log").open("wb") as errors:
            sp = subprocess.Popen(server_cmd, stdout=subprocess.PIPE, stderr=errors,
                                  start_new_session=True)
        opened = pair.ready(sp, directory / "server.jsonl", options.process_timeout)
        assert len(opened["ports"]) == 1 and 0 < opened["ports"][0] < 65536
        relay_cmd = [sys.executable, str(HERE / "fault-proxy.py"), "--ports",
                     str(opened["ports"][0]), "--trace", str(directory / "wire.jsonl"),
                     "--action", "drop", "--kind", "*", "--repeat", "--direction", direction,
                     "--deadline", str(options.process_timeout)]
        if transport == "tcp":
            relay_cmd += ["--tcp"]
        else:
            relay_cmd += ["--cert", str(root / "cert.pem"), "--key", str(root / "key.pem"),
                          "--ca", str(root / "cert.pem")]
        case["relay_command"] = relay_cmd
        with (directory / "relay.log").open("wb") as errors:
            relay = subprocess.Popen(relay_cmd, stdout=subprocess.PIPE, stderr=errors,
                                     start_new_session=True)
        forwarded = pair.ready(relay, directory / "relay.jsonl", options.process_timeout)
        client_cmd = [str(programs[f"{client_kind}_client"]), "--port",
                      str(forwarded["ports"][0]), *common]
        case["client_command"] = client_cmd
        with (directory / "client.jsonl").open("wb") as output, \
                (directory / "client.log").open("wb") as errors:
            cp = subprocess.Popen(client_cmd, stdout=output, stderr=errors, start_new_session=True)
        remainder, _ = sp.communicate(timeout=options.process_timeout)
        with (directory / "server.jsonl").open("ab") as output:
            output.write(remainder)
        client_exit = cp.wait(timeout=options.process_timeout)
        remainder, _ = relay.communicate(timeout=options.process_timeout)
        with (directory / "relay.jsonl").open("ab") as output:
            output.write(remainder)
        elapsed = time.monotonic() - start
        case.update(client_exit=client_exit, server_exit=sp.returncode,
                    relay_exit=relay.returncode, elapsed=elapsed)
        assert relay.returncode == 0
        assert client_exit in (0, 1) and sp.returncode in (0, 1)
        assert (client_exit if target == "client" else sp.returncode) == 1
        # Process deadlines supervise the fixture; they are never completion
        # evidence. Native must diagnose its own session timer before that limit.
        assert elapsed < options.deadline, (elapsed, options.deadline)
        diagnostic = (directory / f"{target}.log").read_text()
        assert "FIX session timeout\n" in diagnostic, diagnostic
        client_rows = pair.records(directory / "client.jsonl")
        ready = any(row.get("event") == "ready" for row in client_rows)
        assert ready, client_rows  # both initiators can offer business after Logon/recovery
        results = {}
        for role in ("client", "server"):
            rows = [row for row in pair.records(directory / f"{role}.jsonl")
                    if row.get("event") == "result"]
            assert len(rows) == 1, rows
            results[role] = rows[0]
        assert results[target]["errors"] > 0
        assert results["client"]["progress"]["admitted"] > 0
        assert results["client"]["progress"]["complete"] == 0
        wire = pair.records(directory / "wire.jsonl")
        outcome = [row for row in wire if row["event"] == "result"]
        assert len(outcome) == 1 and outcome[0]["errors"] == 0
        assert outcome[0]["opened"] == outcome[0]["closed"] == 1
        requests = [(i, row) for i, row in enumerate(wire) if row["event"] == "frame"
                    and row["direction"] != direction and row["kind"] == "1"
                    and row.get("admin", {}).get("112")]
        assert requests, "no actual Native TestRequest112 on the wire"
        matched = []
        for index, request in requests:
            echoes = [row for row in wire[index + 1:] if row["event"] == "fault"
                      and row["action"] == "drop" and row["direction"] == direction
                      and row["kind"] == "0" and row.get("admin", {}).get("112") ==
                      request["admin"]["112"]]
            if echoes:
                matched.append({"id": request["admin"]["112"], "request_seq": request["seq"],
                                "echo_seq": echoes[0]["seq"]})
        assert matched, "no dropped Heartbeat carrying the actual TestRequest112"
        case.update(status="passed", probes=matched, target_result=results[target],
                    phase="receive-idle liveness after Logon/recovery", client_ready=ready,
                    expected="Native session Timeout after published liveness-probe echo "
                             "is dropped")
    except Exception as error:
        case.update(status="failed", error=str(error))
        raise
    finally:
        pair.stop(cp)
        pair.stop(sp)
        pair.stop(relay)
        (directory / "case.json").write_text(json.dumps(case, indent=2) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name, default in (("native-client", HERE.parent / "util" / "zfix"),
                          ("native-server", HERE.parent / "util" / "zfixd"),
                          ("quickfix-client", HERE / "quickfix"),
                          ("quickfix-server", HERE / "quickfixd")):
        parser.add_argument(f"--{name}", type=Path, default=default)
    parser.add_argument("--dictionary", type=Path, required=True)
    parser.add_argument("--artifacts", type=Path)
    parser.add_argument("--deadline", type=int, default=15)
    parser.add_argument("--process-timeout", type=float, default=30)
    options = parser.parse_args()
    if options.deadline < 8 or options.process_timeout <= options.deadline:
        parser.error("deadline >=8 and larger process timeout required")
    root = options.artifacts.resolve() if options.artifacts else Path(
        tempfile.mkdtemp(prefix="zfix-probe-"))
    root.mkdir(parents=True, exist_ok=True)
    if any(root.iterdir()):
        parser.error(f"artifact directory must be empty: {root}")
    programs = {name: getattr(options, name).resolve() for name in (
        "native_client", "native_server", "quickfix_client", "quickfix_server")}
    lock = (HERE / "quickfix.lock").read_text()
    expected = next(line.split("=", 1)[1] for line in lock.splitlines()
                    if line.startswith("QUICKFIX_FIX44_SHA256="))
    assert pair.digest(options.dictionary) == expected, "canonical dictionary mismatch"
    manifest = {"platform": platform.platform(), "quickfix_lock": lock,
                "dictionary_sha256": expected, "relay_sha256": pair.digest(HERE / "fault-proxy.py"),
                "programs": {name: {"path": str(path), "sha256": pair.digest(path),
                                    "binary_sha256": pair.digest(pair.executable(path))}
                             for name, path in programs.items()},
                "phase": "receive-idle liveness; ordinary readiness requires no probe echo",
                "quickfix-quickfix": "excluded: this gate requires a Native session timeout",
                "tls": "verified fixture termination and verified upstream reference identity"}
    (root / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    pair.certificate(root)
    failed = number = 0
    def tap(line):
        print(line, flush=True)
        with (root / "probe.tap").open("a") as output:
            output.write(line + "\n")
    tap(f"# retained probe artifacts: {root}")
    tap("1..6")
    for label in ("native-native", "native-quickfix", "quickfix-native"):
        for transport in ("tcp", "tls"):
            number += 1
            try:
                run_case(options, root, programs, label, transport)
                tap(f"ok {number} - {label} {transport} Native probe timeout")
            except Exception:
                failed += 1
                path = root / f"failure-{number}.log"
                path.write_text(traceback.format_exc())
                tap(f"not ok {number} - {label} {transport} Native probe timeout")
                tap(f"# details: {path}")
    (root / "summary.json").write_text(json.dumps({"cases": number, "failed": failed,
                                                  "passed": number - failed}, indent=2) + "\n")
    return bool(failed)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:
        print(f"Bail out! probe fixture: {error}", flush=True)
        sys.exit(1)
