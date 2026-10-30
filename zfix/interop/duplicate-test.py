#!/usr/bin/env python3
"""Check QuickFIX's duplicate-session rejection policy without replacing the original."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import platform
import signal
import subprocess
import sys
import tempfile
import time
import traceback


def load_pair():
    path = Path(__file__).resolve().parents[1] / "util" / "pair-test.py"
    spec = importlib.util.spec_from_file_location("zfix_pair", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def connection(pid, port):
    """Read this owned process's socket, including kernel state while it is stopped."""
    process = Path("/proc") / str(pid)
    sockets = {}
    for fd in (process / "fd").iterdir():
        try:
            target = os.readlink(fd)
        except FileNotFoundError:
            continue
        if target.startswith("socket:["):
            sockets[target[8:-1]] = fd.name
    matches = []
    for family in ("tcp", "tcp6"):
        for line in (process / "net" / family).read_text().splitlines()[1:]:
            values = line.split()
            if values[9] in sockets and int(values[2].split(":")[1], 16) == port:
                matches.append({"fd": sockets[values[9]], "inode": values[9],
                                "family": family, "local": values[1], "peer": values[2],
                                "state": values[3]})
    assert len(matches) == 1, (pid, port, matches)
    assert matches[0]["state"] == "01", matches[0]
    return matches[0]


def result(pair, path):
    rows = pair.records(path)
    results = [row for row in rows if row.get("event") == "result"]
    assert len(results) == 1, rows
    return rows, results[0]


def completed(row, client):
    assert row["session"] == 1 and row["errors"] == 0, row
    assert row["connections"] == 1, row
    progress = row["progress"]
    assert progress["admitted"] == progress["complete"] == 4, row
    assert progress["reports"] == 8, row
    assert progress["invalid"] == progress["unknown"] == progress["refused"] == 0, row
    if client:
        assert progress["offered"] == progress["ack"] == 4, row
        assert progress["inFlight"] == 0 and 0 < progress["peak"] <= 1, row
        for name in ("ackNs", "terminalNs", "scheduleNs"):
            assert row[name]["count"] == 4, row


def run_case(pair, root, client, server, label, transport, ready_timeout, process_timeout):
    directory = root / f"{label}-{transport}"
    directory.mkdir()
    paths = {name: directory / name for name in (
        "server.jsonl", "server.log", "original.jsonl", "original.log",
        "duplicate.jsonl", "duplicate.log")}
    common = ["--warmup", "0", "--sessions", "1", "--inflight", "1",
              "--heartbeat", "30", "--mode", "4", "--version", "FIX.4.4",
              "--sender", "ZFIX-C", "--target", "ZFIX-S"]
    if transport == "tcp":
        common += ["--tcp"]
    else:
        common += ["--cert", str(root / "cert.pem"), "--key", str(root / "key.pem"),
                   "--ca", str(root / "cert.pem")]
    server_cmd = [server, "--port", "0", "--count", "4", "--deadline", "30", *common]
    case = {"pair": label, "transport": transport, "server_command": server_cmd,
            "events": [], "status": "running", "ready_timeout": ready_timeout,
            "process_timeout": process_timeout}

    def event(action, **values):
        case["events"].append({"action": action, "ns": time.monotonic_ns(), **values})

    sp = original = duplicate = None
    paused = False
    try:
        with paths["server.log"].open("wb") as errors:
            sp = subprocess.Popen(server_cmd, stdout=subprocess.PIPE, stderr=errors,
                                  start_new_session=True)
        event("acceptor-started", pid=sp.pid)
        opened = pair.ready(sp, paths["server.jsonl"], ready_timeout)
        assert opened.get("role") == "acceptor", opened
        ports = opened["ports"]
        assert len(ports) == 1 and 0 < ports[0] < 65536, opened
        event("acceptor-ready", pid=sp.pid, ports=ports)
        endpoint = ["--host", "localhost", "--ports", str(ports[0])]
        original_cmd = [client, *endpoint, "--count", "4", "--rate", "1",
                        "--deadline", "30", *common]
        case["original_command"] = original_cmd
        with paths["original.log"].open("wb") as errors:
            original = subprocess.Popen(original_cmd, stdout=subprocess.PIPE, stderr=errors,
                                        start_new_session=True)
        event("original-started", pid=original.pid)
        logged_on = pair.ready(original, paths["original.jsonl"], ready_timeout)
        assert logged_on.get("role") == "initiator", logged_on
        event("original-ready", pid=original.pid)
        os.kill(original.pid, signal.SIGSTOP)
        paused = True
        pid, status = os.waitpid(original.pid, os.WUNTRACED)
        if os.WIFEXITED(status) or os.WIFSIGNALED(status):
            original.returncode = os.waitstatus_to_exitcode(status)
        assert pid == original.pid and os.WIFSTOPPED(status), (pid, status)
        assert os.WSTOPSIG(status) == signal.SIGSTOP, status
        held = connection(original.pid, ports[0])
        event("original-stopped", pid=pid, wait_status=status, socket=held)
        assert sp.poll() is None, "acceptor exited before duplicate attempt"
        duplicate_cmd = [client, *endpoint, "--count", "1", "--deadline", "4", *common]
        case["duplicate_command"] = duplicate_cmd
        with paths["duplicate.jsonl"].open("wb") as output:
            with paths["duplicate.log"].open("wb") as errors:
                duplicate = subprocess.Popen(duplicate_cmd, stdout=output, stderr=errors,
                                             start_new_session=True)
        event("duplicate-started", pid=duplicate.pid)
        code = duplicate.wait(timeout=process_timeout)
        case["duplicate_exit"] = code
        assert code == 1, code
        rows, refused = result(pair, paths["duplicate.jsonl"])
        assert not any(row.get("event") == "ready" for row in rows), rows
        assert refused["errors"] > 0 and refused["connections"] == 0, refused
        progress = refused["progress"]
        assert progress["admitted"] == progress["complete"] == progress["reports"] == 0, refused
        assert sp.poll() is None, "duplicate attempt terminated the acceptor"
        retained = connection(original.pid, ports[0])
        assert retained == held, (held, retained)
        event("duplicate-refused-original-retained", socket=retained)
        os.kill(original.pid, signal.SIGCONT)
        paused = False
        event("original-continued", pid=original.pid)
        remainder, _ = original.communicate(timeout=process_timeout)
        with paths["original.jsonl"].open("ab") as output:
            output.write(remainder)
        case["original_exit"] = original.returncode
        assert original.returncode == 0, original.returncode
        remainder, _ = sp.communicate(timeout=process_timeout)
        with paths["server.jsonl"].open("ab") as output:
            output.write(remainder)
        case["server_exit"] = sp.returncode
        assert sp.returncode == 0, sp.returncode
        original_rows, worked = result(pair, paths["original.jsonl"])
        assert sum(row.get("event") == "ready" for row in original_rows) == 1, original_rows
        _, served = result(pair, paths["server.jsonl"])
        completed(worked, True)
        completed(served, False)
        event("original-workload-complete", original_exit=0, server_exit=0, count=4)
        case["status"] = "passed"
    except Exception as error:
        case.update(status="failed", error=str(error), traceback=traceback.format_exc())
        raise
    finally:
        # A stopped process cannot run termination cleanup until continued.
        if paused and original is not None:
            try:
                os.kill(original.pid, signal.SIGCONT)
                event("cleanup-continued-original", pid=original.pid)
            except ProcessLookupError:
                pass
        for process in (duplicate, original, sp):
            pair.stop(process)
        for name, process in (("server", sp), ("original", original), ("duplicate", duplicate)):
            if process is not None:
                case[f"{name}_exit"] = process.returncode
                if process.stdout is not None and not process.stdout.closed:
                    remainder, _ = process.communicate(timeout=3)
                    with paths[f"{name}.jsonl"].open("ab") as output:
                        output.write(remainder)
        (directory / "case.json").write_text(json.dumps(case, indent=2) + "\n")


def main():
    if sys.platform != "linux":
        print("1..0 # SKIP ordered process stop/socket-state gate requires Linux")
        return 0
    pair = load_pair()
    base = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    for name, default in (("native-client", base / "util" / "zfix"),
                          ("quickfix-client", base / "interop" / "quickfix"),
                          ("quickfix-server", base / "interop" / "quickfixd")):
        parser.add_argument(f"--{name}", type=Path, default=default)
    parser.add_argument("--dictionary", type=Path, required=True)
    parser.add_argument("--artifacts", type=Path)
    parser.add_argument("--ready-timeout", type=float, default=10)
    parser.add_argument("--process-timeout", type=float, default=40)
    options = parser.parse_args()
    root = (options.artifacts.resolve() if options.artifacts else
            Path(tempfile.mkdtemp(prefix="zfix-duplicate-")))
    root.mkdir(parents=True, exist_ok=True)
    if any(root.iterdir()):
        parser.error(f"artifact directory must be empty: {root}")
    programs = {name: getattr(options, name).resolve() for name in (
        "native_client", "quickfix_client", "quickfix_server")}
    lock = (base / "interop" / "quickfix.lock").read_text()
    expected = next(line.split("=", 1)[1] for line in lock.splitlines()
                    if line.startswith("QUICKFIX_FIX44_SHA256="))
    assert pair.digest(options.dictionary) == expected, "canonical FIX44 dictionary mismatch"
    pair.certificate(root)
    manifest = {"platform": platform.platform(), "dictionary": str(options.dictionary.resolve()),
                "dictionary_sha256": expected, "quickfix_lock": lock,
                "programs": {name: {"path": str(path), "sha256": pair.digest(path),
                                    "binary_sha256": pair.digest(pair.executable(path))}
                             for name, path in programs.items()},
                "certificate_sha256": pair.digest(root / "cert.pem"),
                "harness_sha256": pair.digest(Path(__file__).resolve()),
                "bind": {"quickfix_acceptor": "0.0.0.0"},
                "scope": "QuickFIX acceptor policy; no native-server displacement coverage",
                "verification": "CA and localhost identity enabled; direct TLS",
                "ordering": "initiator Ready -> SIGSTOP -> waitpid WUNTRACED -> duplicate "
                            "refusal -> same ESTABLISHED socket -> SIGCONT -> four orders"}
    (root / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")

    def tap(line):
        with (root / "duplicate.tap").open("a") as output:
            output.write(line + "\n")
        print(line, flush=True)

    tap(f"# retained QuickFIX duplicate-session policy artifacts: {root}")
    tap("1..4")
    number = failed = 0
    for client_kind in ("native", "quickfix"):
        label = f"{client_kind}-quickfix"
        for transport in ("tcp", "tls"):
            number += 1
            try:
                run_case(pair, root, str(programs[f"{client_kind}_client"]),
                         str(programs["quickfix_server"]), label, transport,
                         options.ready_timeout, options.process_timeout)
                tap(f"ok {number} - {label} {transport} SDK duplicate policy")
            except Exception:
                failed += 1
                tap(f"not ok {number} - {label} {transport} SDK duplicate policy")
                tap(f"# details: {root / f'{label}-{transport}' / 'case.json'}")
    (root / "summary.json").write_text(json.dumps({"cases": number, "failed": failed,
                                                  "passed": number - failed}, indent=2) + "\n")
    return bool(failed)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:
        print(f"Bail out! duplicate attachment fixture: {error}", flush=True)
        sys.exit(1)
