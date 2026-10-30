#!/usr/bin/env python3
"""Shared FIX process supervision and result checks for external harnesses."""
import hashlib
import json
import os
import platform
from pathlib import Path
import selectors
import signal
import subprocess
import sys
import time


def records(path):
    text = path.read_text()
    if text and not text.endswith("\n"):
        raise AssertionError(f"incomplete JSON output: {path}")
    return [json.loads(line) for line in text.splitlines() if line.strip()]


def digest(path):
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def executable(path):
    candidate = path.parent / ".libs" / path.name
    if candidate.is_file():
        with candidate.open("rb") as source:
            if source.read(4) == b"\x7fELF":
                return candidate
    return path


def stop(process):
    if process is None or process.poll() is not None:
        return
    try:
        if os.name == "posix":
            os.killpg(process.pid, signal.SIGTERM)
        else:
            process.terminate()
    except ProcessLookupError:
        pass
    try:
        process.wait(timeout=3)
    except subprocess.TimeoutExpired:
        try:
            if os.name == "posix":
                os.killpg(process.pid, signal.SIGKILL)
            else:
                process.kill()
        except ProcessLookupError:
            pass
        process.wait(timeout=3)


def ready(process, path, timeout):
    """Wait for a readiness record on the child's pipe; preserve every byte."""
    pending = b""
    deadline = time.monotonic() + timeout
    with selectors.DefaultSelector() as events, path.open("wb") as output:
        events.register(process.stdout, selectors.EVENT_READ)
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0 or not events.select(remaining):
                raise AssertionError("acceptor readiness deadline expired")
            chunk = os.read(process.stdout.fileno(), 65536)
            if not chunk:
                raise AssertionError("acceptor closed output before readiness")
            output.write(chunk)
            pending += chunk
            while b"\n" in pending:
                line, pending = pending.split(b"\n", 1)
                if not line.strip():
                    continue
                value = json.loads(line)
                if value.get("event") == "ready":
                    return value


def certificate(root):
    subprocess.run([
        "openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1",
        "-subj", "/CN=localhost", "-addext", "subjectAltName=DNS:localhost,IP:127.0.0.1",
        "-keyout", str(root / "key.pem"), "-out", str(root / "cert.pem")],
        check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=30)


def stamp(value):
    second, _, fraction = value.partition(".")
    assert len(fraction) <= 9
    return second, int((fraction + "000000000")[:9])


def relay_result(directory, action, direction, negative):
    wire = records(directory / "wire.jsonl")
    outcome = [entry for entry in wire if entry["event"] == "result"]
    assert len(outcome) == 1 and outcome[0]["errors"] == 0
    faults = [(i, entry) for i, entry in enumerate(wire) if entry["event"] == "fault"]
    if action != "pass":
        assert faults, "the declared wire fault was not applied"
    if action == "fragment":
        assert outcome[0]["chunks"] > outcome[0]["frames"]
    if action in ("drop", "checksum", "length") and not negative:
        for index, lost in faults:
            retries = [entry for entry in wire[index + 1:] if entry["event"] == "frame"
                       and entry["direction"] == direction and entry["kind"] == lost["kind"]
                       and entry["session"] == lost["session"] and entry["seq"] == lost["seq"]]
            assert retries, f"no replay for dropped sequence {lost['seq']}"
            replay = retries[0]
            assert replay["possDup"] == "Y" and stamp(replay["original"]) == stamp(lost["time"])
            assert stamp(replay["time"]) >= stamp(replay["original"])
            requests = [entry for entry in wire[index + 1:] if entry["event"] == "frame"
                        and entry["direction"] != direction and entry["kind"] == "2"
                        and entry["session"] == lost["session"]]
            assert any(int(entry["admin"]["7"]) <= lost["seq"] and
                       (int(entry["admin"]["16"]) == 0 or
                        lost["seq"] <= int(entry["admin"]["16"])) for entry in requests)
    if action in ("qty", "unknown") and negative:
        tag = "38" if action == "qty" else "9999"
        assert any(entry["event"] == "frame" and entry["direction"] != direction and
                   entry["kind"] == "3" and entry.get("admin", {}).get("371") == tag
                   for entry in wire), f"no Session::Reject naming the injected tag {tag}"
    return outcome[0]


def run_case(client, server, root, transport, name, args, count, warmup, sessions=1,
             negative=False, pair="native-native", ready_timeout=10, process_timeout=40,
             relay=None, client_args=(), server_args=(), client_ca=None,
             server_cert=None, server_key=None):
    directory = root / f"{transport}-{name}"
    directory.mkdir()
    common = ["--count", str(count), "--warmup", str(warmup), "--sessions", str(sessions),
              "--deadline", "4" if negative else "15", "--inflight", "4", *args]
    client_common = common[:]
    server_common = common[:]
    if transport == "tcp":
        client_common += ["--tcp"]
        server_common += ["--tcp"]
    else:
        client_common += ["--cert", str(root / "cert.pem"), "--key", str(root / "key.pem"),
                          "--ca", str(client_ca or root / "cert.pem")]
        server_common += ["--cert", str(server_cert or root / "cert.pem"),
                          "--key", str(server_key or root / "key.pem"),
                          "--ca", str(root / "cert.pem")]
    so, se, co, ce = [directory / entry for entry in (
        "server.jsonl", "server.log", "client.jsonl", "client.log")]
    server_cmd = [server, "--port", "0", *server_common, *server_args]
    case = {"pair": pair, "transport": transport, "case": name, "sessions": sessions,
            "expected_failure": negative, "server_command": server_cmd,
            "ready_timeout": ready_timeout, "process_timeout": process_timeout,
            "status": "running"}
    sp = cp = proxy = None
    try:
        with se.open("wb") as errors:
            sp = subprocess.Popen(server_cmd, stdout=subprocess.PIPE, stderr=errors,
                                  start_new_session=True)
        opened = ready(sp, so, ready_timeout)
        ports = opened["ports"]
        assert (len(ports) == sessions and len(set(ports)) == sessions
                and all(0 < port < 65536 for port in ports)), ports
        if relay:
            script = Path(__file__).resolve().parent.parent / "interop" / "fault-proxy.py"
            command = [sys.executable, str(script), "--ports", ",".join(map(str, ports)),
                       "--trace", str(directory / "wire.jsonl"), "--deadline",
                       str(process_timeout), *relay]
            if transport == "tcp":
                command += ["--tcp"]
            else:
                command += ["--cert", str(root / "cert.pem"), "--key", str(root / "key.pem"),
                            "--ca", str(root / "cert.pem")]
            case["relay_command"] = command
            case["relay_sha256"] = digest(script)
            with (directory / "relay.log").open("wb") as errors:
                proxy = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=errors,
                                         start_new_session=True)
            relayed = ready(proxy, directory / "relay.jsonl", ready_timeout)
            ports = relayed["ports"]
        client_cmd = [client, "--ports", ",".join(map(str, ports)), *client_common, *client_args]
        case["client_command"] = client_cmd
        with co.open("wb") as output, ce.open("wb") as errors:
            cp = subprocess.Popen(client_cmd, stdout=output, stderr=errors,
                                  start_new_session=True)
        # Drain the pipe while waiting: even a large session result set cannot
        # fill the pipe and prevent the acceptor from completing shutdown.
        remainder, _ = sp.communicate(timeout=process_timeout)
        with so.open("ab") as output:
            output.write(remainder)
        cr = cp.wait(timeout=process_timeout)
        sr = sp.returncode
        case.update(client_exit=cr, server_exit=sr)
        if proxy:
            remainder, _ = proxy.communicate(timeout=process_timeout)
            with (directory / "relay.jsonl").open("ab") as output:
                output.write(remainder)
            case["relay_exit"] = proxy.returncode
            assert proxy.returncode == 0
            action = relay[relay.index("--action") + 1]
            direction = relay[relay.index("--direction") + 1]
            case["wire"] = relay_result(directory, action, direction, negative)
        clients = sorted((r for r in records(co) if r.get("event") == "result"),
                         key=lambda r: r["session"])
        servers = sorted((r for r in records(so) if r.get("event") == "result"),
                         key=lambda r: r["session"])
        assert len(clients) == sessions and len(servers) == sessions, (cr, sr)
        if negative:
            assert cr == 1 and sr == 1 and any(r["errors"] for r in clients + servers), (cr, sr)
        else:
            assert cr == 0 and sr == 0, (cr, sr)
            scenario = common[common.index("--scenario") + 1] if "--scenario" in common else "orders"
            reports = 3 if scenario == "replace" else 2
            for c, s in zip(clients, servers):
                progress = c["progress"]
                assert c["session"] == s["session"]
                assert c["errors"] == s["errors"] == 0
                assert progress["offered"] == progress["admitted"] == progress["complete"]
                if "--duration" not in common:
                    assert progress["complete"] == count
                else:
                    assert 0 < progress["complete"] <= count
                assert progress["ack"] == progress["complete"]
                assert progress["reports"] == reports * progress["complete"]
                assert (progress["inFlight"] == progress["invalid"]
                        == progress["unknown"] == progress["refused"] == 0)
                assert 0 < progress["peak"] <= 4 and c["warmup"] == warmup
                assert (c["ackNs"]["count"] == c["terminalNs"]["count"]
                        == c["scheduleNs"]["count"] == progress["complete"])
                assert s["progress"]["admitted"] == warmup + progress["complete"]
                assert s["progress"]["complete"] == s["progress"]["admitted"]
                assert c["rxFrames"] and c["txFrames"] and c["rxBytes"] and c["txBytes"]
        case.update(status="passed", measured=sum(r["progress"]["complete"] for r in clients))
        print(json.dumps({key: value for key, value in case.items()
                          if key not in ("client_command", "server_command", "relay_command")}),
              flush=True)
        return clients, servers
    except Exception as error:
        case.update(status="failed", error=str(error))
        for path in (so, se, co, ce):
            if path.exists():
                print(f"{path}:\n{path.read_text()}", file=os.sys.stderr)
        raise
    finally:
        stop(cp)
        stop(sp)
        stop(proxy)
        if case["status"] == "running":
            case["status"] = "aborted"
        (directory / "case.json").write_text(json.dumps(case, indent=2) + "\n")


def pair_cases(smoke=False, canonical=False, workflows=False, names=None):
    """Shared workload parameters, independent of a test runner or peer engine."""
    cases = [("scalar", [], 37, 5, 1, False)]
    if not smoke:
        cases += [("sessions", [], 17, 3, 3, False),
                  ("retention", ["--retain", "2"], 37, 5, 1, False),
                  ("groups", ["--scenario", "groups", "--text", "24", "--binary", "4096"],
                   9, 2, 2, False),
                  ("paced", ["--rate", "20"], 6, 2, 2, False),
                  ("duration", ["--rate", "50", "--duration", "1"], 1000, 2, 1, False)]
        if workflows:
            cases += [("cancel", ["--scenario", "cancel"], 13, 3, 2, False),
                      ("replace", ["--scenario", "replace"], 13, 3, 2, False)]
        if not canonical:
            cases += [("44A", ["--mode", "44A"], 11, 2, 1, False),
                      ("T11", ["--mode", "T11", "--version", "FIXT.1.1"], 11, 2, 1, False)]
        cases += [("quota", ["--cache-bytes", "1"], 1, 0, 1, True)]
    if names:
        unknown = set(names) - {case[0] for case in cases}
        if unknown:
            raise ValueError(f"cases unavailable in selected profile: {sorted(unknown)}")
        cases = [case for case in cases if case[0] in names]
    return cases


def run_pair(client, server, root, cases, *, pair="native-native", tcp_only=False,
             canonical=False, workflows=False, arguments=None, relay=None,
             ready_timeout=10, process_timeout=40, timeout=None):
    """Run a reusable workload profile and retain its complete evidence."""
    client, server = str(Path(client).resolve()), str(Path(server).resolve())
    root = root.resolve()
    root.mkdir(parents=True, exist_ok=True)
    if any(root.iterdir()):
        raise ValueError(f"artifact directory must be empty: {root}")
    deadline = time.monotonic() + timeout if timeout is not None else None
    manifest = {"pair": pair, "platform": platform.platform(),
                "client": client, "server": server, "client_sha256": digest(Path(client)),
                "server_sha256": digest(Path(server)), "canonical": canonical,
                "client_binary": str(executable(Path(client))),
                "server_binary": str(executable(Path(server))),
                "client_binary_sha256": digest(executable(Path(client))),
                "server_binary_sha256": digest(executable(Path(server))),
                "workflows": workflows, "arguments": arguments or {}}
    if not tcp_only:
        certificate(root)
        manifest["certificate_sha256"] = digest(root / "cert.pem")
    lock = Path(__file__).resolve().parent.parent / "interop" / "quickfix.lock"
    if lock.exists():
        manifest["quickfix_lock"] = lock.read_text()
    (root / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(json.dumps({"event": "artifacts", "pair": pair, "path": str(root)}), flush=True)
    completed = 0
    for transport in (["tcp"] if tcp_only else ["tcp", "tls"]):
        for name, args, count, warmup, sessions, negative in cases:
            ready_limit, process_limit = ready_timeout, process_timeout
            if deadline is not None:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise TimeoutError("pair profile deadline expired")
                ready_limit, process_limit = min(ready_limit, remaining), min(process_limit, remaining)
            run_case(client, server, root, transport, name, args, count, warmup,
                     sessions, negative, pair, ready_limit, process_limit, relay)
            completed += 1
    return completed
