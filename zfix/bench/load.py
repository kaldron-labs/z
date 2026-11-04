#!/usr/bin/env python3
"""Measure independent FIX peers in separate processes and retain raw evidence."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import platform
import random
import shlex
import shutil
import socket
import subprocess
import sys
import tempfile
import time
import traceback


HERE = Path(__file__).resolve().parent
BASE = HERE.parent
PROFILES = ("scalar", "groups", "text", "fragmentation", "replay", "overload")
PAIRS = ("native-native", "native-quickfix", "quickfix-native", "quickfix-quickfix")


def load_pair():
    path = BASE / "util" / "pair-test.py"
    spec = importlib.util.spec_from_file_location("zfix_pair", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class Process(subprocess.Popen):
    """Let Popen retain its pipe/deadline handling, but reap resources with wait4."""
    def __init__(self, *args, **kwargs):
        self.usage = None
        super().__init__(*args, **kwargs)

    def _try_wait(self, flags):
        try:
            pid, status, usage = os.wait4(self.pid, flags)
        except ChildProcessError:
            return self.pid, 0
        if pid == self.pid:
            self.usage = {"userSeconds": usage.ru_utime, "systemSeconds": usage.ru_stime,
                          "maxRssBytes": usage.ru_maxrss *
                                         (1 if sys.platform == "darwin" else 1024),
                          "minorFaults": usage.ru_minflt, "majorFaults": usage.ru_majflt,
                          "voluntarySwitches": usage.ru_nvcsw,
                          "involuntarySwitches": usage.ru_nivcsw}
        return pid, status

    def _internal_poll(self, _deadstate=None):
        if self.returncode is not None or not self._waitpid_lock.acquire(False):
            return self.returncode
        try:
            pid, status = self._try_wait(os.WNOHANG)
            if pid == self.pid:
                self._handle_exitstatus(status)
        finally:
            self._waitpid_lock.release()
        return self.returncode


def command_output(command):
    try:
        result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                text=True, timeout=20)
    except (OSError, subprocess.TimeoutExpired) as error:
        return {"command": command, "exit": None, "output": str(error)}
    return {"command": command, "exit": result.returncode, "output": result.stdout}


def metadata(options, pair, programs):
    variables = {}
    makefile = HERE / "Makefile"
    if makefile.exists():
        for line in makefile.read_text().splitlines():
            name, separator, value = line.partition(" = ")
            if separator and name in ("CXX", "CPPFLAGS", "Z_CPPFLAGS",
                                       "CXXFLAGS", "Z_CXXFLAGS", "Z_LDFLAGS",
                                       "QUICKFIX_CFLAGS", "QUICKFIX_LIBS", "QUICKFIX_DICT"):
                variables[name] = value
    cpu = Path("/proc/cpuinfo")
    hardware = sorted({line for line in cpu.read_text().splitlines()
                       if line.startswith(("model name", "Hardware", "CPU implementer",
                                           "CPU part"))}) if cpu.exists() else []
    extensions = {".cc", ".hh", ".py", ".am", ".patch", ".lock", ".sh"}
    excluded = {".libs", ".deps", "zlib", "__pycache__", "build", "generated"}
    sources = [BASE / "Makefile.am"]
    for role in ("src", "test", "util", "example", "itest", "interop", "bench"):
        sources += [path for path in (BASE / role).rglob("*")
                    if path.is_file() and path.suffix in extensions and
                    not excluded.intersection(path.relative_to(BASE).parts)]
    sources += [BASE.parent / name for name in (
        "configure.ac", "m4/zi_uring.m4", "zi/src/ZiConfig.hh",
        "zi/src/ZiIOBuf.hh", "zi/src/ZiIOBuf.cc", "zi/src/ZiIOContext.hh",
        "zi/src/ZiMultiplex.hh", "zi/src/ZiMultiplex.cc", "zi/src/ZiPlatform.cc",
        "zi/src/ZiRx.hh", "zi/src/ZiRxStream.hh", "zi/src/ZiTx.hh", "zi/src/ZiTxStream.hh",
        "zm/src/ZmPQueue.hh", "zm/src/ZmList.hh", "zm/src/ZmCache.hh", "zm/src/ZmPolyCache.hh",
        "zm/src/ZmNode.hh", "zm/src/ZmNodeFn.hh", "zm/src/ZmPolymorph.hh",
        "zm/src/ZmFn.hh", "zm/src/ZmFn_.hh",
        "ztcp/src/Ztcp.hh", "ztls/src/Ztls.hh", "ztls/src/ZtlsPico.hh",
        "ztls/src/ZtlsPico.cc", "ztls/src/ZtlsBackend.hh", "ztls/src/ZtlsBackend.cc",
        "ztls/src/ZtlsHMAC.hh")]
    result = {"platform": platform.platform(), "machine": platform.machine(),
              "cpu": hardware, "logicalCpus": os.cpu_count(),
              "affinity": sorted(os.sched_getaffinity(0)) if hasattr(os, "sched_getaffinity")
                          else None,
              "buildLabel": options.build_label, "buildFlags": variables,
              "compiler": command_output(shlex.split(variables.get("CXX", "c++")) +
                                         ["--version"]),
              "git": command_output(["git", "-C", str(BASE.parent), "rev-parse", "HEAD"]),
              "dictionary": str(options.dictionary.resolve()),
              "dictionarySha256": pair.digest(options.dictionary),
              "quickfixLock": (BASE / "interop" / "quickfix.lock").read_text(),
              "harnessSha256": pair.digest(Path(__file__).resolve()),
              "sourceFiles": {str(path.relative_to(BASE.parent)): pair.digest(path)
                              for path in sorted(sources)},
              "programs": {}, "linkedLibraries": {},
              "arguments": {k: str(v) if isinstance(v, Path) else v
                                              for k, v in vars(options).items()},
              "differences": {
                  "bind": "native loopback; QuickFIX wildcard IPv4; local clients use localhost",
                  "validation": "native declared typed dictionary; QuickFIX canonical dictionary "
                                "and default validation; directional interop expectations "
                                "are recorded separately",
                  "archive": "application-selected native bodies; "
                             "QuickFIX serialized FIX messages; "
                             "same configured count/byte bounds, different stored representation",
                  "logging": "peer wire/event logging disabled; timing relay aggregate summary",
                  "wireCounters": "native socket-completed; QuickFIX SDK wire attempts",
                  "measuredWindow": "client CLOCK_MONOTONIC nanoseconds; first session's "
                                    "measured start through last session's measured finish; "
                                    "sum of session rates also reported",
                  "resources": "wait4 whole process including startup/warmup/shutdown and "
                               "libtool wrapper/fork-exec highwater; inherited pre-exec RSS can "
                               "dominate small cases; latency/throughput cover measured workload",
                  "relay": "fragmentation/replay traverse Python and, for TLS, verified "
                           "termination/re-encryption; compare to same relay profile only",
                  "remote": "remote server resources and binary identity unmeasured unless "
                            "provided separately; remote service lifecycle externally managed"}}
    if options.process_window:
        result["processWindowObserver"] = {
            "scope": "paired control-bracketed shared measured-workload process counters",
            "protocol": "inherited task-owned UnixSEQPACKET; fixed27u64 START/END exchanges",
            "sampling": "two process CPU/resource snapshots per peer, no per-order CPU calls",
            "overhead": "one sleeping helper per peer; CPU deltas, calls and sample/guard brackets retained",
            "warmup": "existing independent session warmup preserved; overlap at client envelope open recorded",
            "fallback": "none; missing/invalid paired samples fail the case"}
    if options.interface:
        spec = importlib.util.spec_from_file_location("zfix_netmeta", HERE / "netmeta.py")
        helper = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(helper)
        result["network"] = helper.capture(options.interface)
    else:
        result["network"] = {"scope": "local interface metadata not requested"}
    if options.syscalls:
        result["syscallObserver"] = {
            "version": command_output(["strace", "-V"]),
            "arguments": ["strace", "-f", "-c", "-qq", "-o", "PER_PEER_SUMMARY"],
            "scope": "whole peer process tree including wrapper/startup/warmup/shutdown"}
    for name, path in programs.items():
        binary = pair.executable(path)
        result["programs"][name] = {
            "path": str(path), "wrapperSha256": pair.digest(path),
            "elf": str(binary), "elfSha256": pair.digest(binary),
            "fileBytes": binary.stat().st_size,
            "sections": command_output(["size", "-A", str(binary)]),
            "dependencies": command_output([str(BASE.parent / "libtool"),
                                             "--mode=execute", "ldd", str(path)])}
        libraries = result["programs"][name]["dependencies"]["output"]
        for token in libraries.split():
            path = Path(token)
            if (token.startswith("/") and path.is_file() and
                    token not in result["linkedLibraries"]):
                result["linkedLibraries"][token] = {
                    "resolvedPath": str(path.resolve()), "sha256": pair.digest(path),
                    "fileBytes": path.stat().st_size,
                    "sections": command_output(["size", "-A", str(path)])}
    result["observerLibraries"] = {}
    for name in ("heaptrace", "audittrace"):
        path = getattr(options, name)
        if path:
            path = path.resolve()
            result["observerLibraries"][name] = {
                "path": str(path), "sha256": pair.digest(path), "fileBytes": path.stat().st_size}
    return result


def syscall_summary(text):
    """Parse strace -f -c output; reject incomplete or inconsistent evidence."""
    entries, total = {}, None
    for line in text.splitlines():
        fields = line.split()
        if not fields or fields[0].startswith(("%", "-")):
            continue
        if len(fields) not in (5, 6):
            raise ValueError(f"unexpected strace summary row: {line}")
        percent, seconds, micros, calls = fields[:4]
        value = {"calls": int(calls), "errors": int(fields[4]) if len(fields) == 6 else 0,
                 "seconds": float(seconds), "usecsPerCall": int(micros),
                 "percentTime": float(percent)}
        if any(value[key] < 0 for key in value):
            raise ValueError(f"negative strace summary row: {line}")
        name = fields[-1]
        if name == "total":
            if total is not None:
                raise ValueError("duplicate strace total")
            total = value
        elif name in entries:
            raise ValueError(f"duplicate strace syscall: {name}")
        else:
            entries[name] = value
    if total is None or not entries:
        raise ValueError("missing strace syscall summary")
    for key in ("calls", "errors"):
        if sum(item[key] for item in entries.values()) != total[key]:
            raise ValueError(f"inconsistent strace {key} total")
    return {"scope": "strace -f -c whole peer process tree including wrapper/startup/warmup/shutdown",
            "total": total, "syscalls": entries}


def peer_command(options, command, directory, role, diagnostic):
    environment = ["ZFIX_HEAPSTATS=1"] if options.framework_heaps else []
    if diagnostic and options.syscalls:
        if environment:
            command = [str(BASE.parent / "libtool"), "--mode=execute", "env", *environment, *command]
        return ["strace", "-f", "-c", "-qq", "-o",
                str(directory / f"{role}-syscalls.txt"), *command]
    if diagnostic and not options.io_diagnostics:
        if options.audittrace:
            environment += [f"LD_AUDIT={options.audittrace.resolve()}",
                            f"ZFIX_AUDITTRACE_FILE={directory / (role + '-audit.json')}"]
        else:
            environment += [f"LD_PRELOAD={options.heaptrace.resolve()}",
                            f"ZFIX_HEAPTRACE_FILE={directory / (role + '-heap.json')}"]
    if not environment:
        return command
    # libtool sources the wrapper to set library paths and substitutes its ELF
    # argument before env installs the observer. Python/libtool/env are unobserved.
    return [str(BASE.parent / "libtool"), "--mode=execute", "env", *environment, *command]


def common_args(options, profile):
    rate = options.overload_rate if profile == "overload" else options.rate
    args = ["--count", str(options.count), "--warmup", str(options.warmup),
            "--sessions", str(options.sessions), "--inflight", str(options.inflight),
            "--rate", str(rate), "--deadline", str(options.deadline),
            "--retain", str(options.retain), "--cache-bytes", str(options.cache_bytes),
            "--queue-bytes", str(options.queue_bytes), "--seed", str(options.seed),
            "--sender", options.sender, "--target", options.target,
            "--mode", "4", "--version", "FIX.4.4"]
    if profile == "groups":
        args += ["--scenario", "groups", "--parties", str(options.parties),
                 "--subs", str(options.subs)]
    if profile in ("text", "fragmentation"):
        args += ["--text", str(options.text)]
    return args


def rows(pair, path):
    return [row for row in pair.records(path) if row.get("event") == "result"]


def io_metric_coverage(case, client_kind, server_kind):
    result = {}
    for role, kind, key in (("client", client_kind, "clients"),
                            ("server", server_kind, "servers")):
        observed = case.get("ioDiagnostics", {}).get(role)
        backends = {row.get("backend") for row in case.get(key, [])}
        backend = observed["backend"] if observed else next(iter(backends)) if len(backends) == 1 else None
        native = kind == "native"
        fallback_applies = native and backend == "uring"
        result[role] = {
            "engine": kind, "backend": backend,
            "nativeDiagnosticRecord": observed is not None,
            "ordinaryFallback": {
                "applicable": fallback_applies,
                "status": "observed" if fallback_applies and observed else
                          "not-collected" if fallback_applies else
                          "unavailable" if native and backend is None else "not-applicable",
                "scope": "actual io_uring TxData policy-classified ordinary results/bytes; not all socket attempts",
                "excluded": "epoll has no ring TxData event channel; QuickFIX is external; syscall attempts are a separate gate"},
            "deliveryToFramer": {
                "applicable": native,
                "status": "observed" if native and observed and observed["provenance"] else
                          "not-collected" if native else "not-applicable",
                "origin": "latest contributing userspace payload-delivery callback to native FIX framing",
                "excluded": "not raw CQE dequeue, kernel/NIC arrival, first-fragment delay or an all-engine framer comparison"}}
    return result


def run_case(options, pair, root, programs, label, profile, transport, repeat, diagnostic):
    observer = ("syscalls" if options.syscalls else "io" if options.io_diagnostics else
                "audit" if options.audittrace else "heap") if diagnostic else None
    tag = f"{label}-{transport}-{profile}-{repeat}" + (f"-{observer}" if diagnostic else "")
    directory = root / tag
    directory.mkdir()
    common = common_args(options, profile)
    remote = bool(options.remote_host)
    client_kind, server_kind = (options.remote_client, "external") if remote else label.split("-")
    if transport == "tcp":
        common += ["--tcp"]
    elif remote:
        common += ["--ca", str(options.ca.resolve())]
    else:
        common += ["--cert", str(root / "cert.pem"), "--key", str(root / "key.pem"),
                   "--ca", str(root / "cert.pem")]
    case = {"pair": label, "profile": profile, "transport": transport, "repeat": repeat,
            "diagnostic": diagnostic, "diagnosticObserver": observer,
            "remote": remote, "commands": {},
            "resources": {}, "status": "running", "timeouts": 0,
            "expectedOrders": options.count * options.sessions}
    processes = {}
    window_sockets = None
    if options.process_window:
        if sys.platform != "linux":
            raise ValueError("process-window measurement requires Linux; ordinary benchmark remains available")
        if remote:
            raise ValueError("process window requires both local task-owned peers")
        window_sockets = socket.socketpair(socket.AF_UNIX, socket.SOCK_SEQPACKET)
    started = time.monotonic()
    try:
        ports = options.remote_ports
        if not remote:
            command = [str(programs[f"{server_kind}_server"]), "--port", "0", *common]
            if server_kind == "native":
                command += ["--backend", options.backend]
                command += ["--rx-ring-size", str(options.rx_ring_size),
                            "--tx-ring-size", str(options.tx_ring_size)]
                if diagnostic and options.io_diagnostics:
                    command += ["--observe-io", "--diag-samples", str(options.diag_samples)]
            command = peer_command(options, command, directory, "server", diagnostic)
            case["commands"]["server"] = command
            with (directory / "server.log").open("wb") as errors:
                extra = {}
                if window_sockets:
                    extra = dict(pass_fds=(window_sockets[0].fileno(),),
                                 env=dict(os.environ, ZFIX_WINDOW_FD=str(window_sockets[0].fileno())))
                server = Process(command, stdout=subprocess.PIPE, stderr=errors,
                                 start_new_session=True, **extra)
                if window_sockets: window_sockets[0].close()
            processes["server"] = server
            opened = pair.ready(server, directory / "server.jsonl", options.ready_timeout)
            ports = opened["ports"]
            assert len(ports) == options.sessions and len(set(ports)) == len(ports), ports
        if profile in ("fragmentation", "replay"):
            action = "fragment" if profile == "fragmentation" else "drop"
            relay = [sys.executable, str(BASE / "interop" / "fault-proxy.py"),
                     "--host", options.remote_host or "127.0.0.1",
                     "--peer", options.remote_host or "localhost",
                     "--ports", ",".join(map(str, ports)), "--summary", "--action", action,
                     "--kind", "D" if action == "fragment" else "8",
                     "--direction", "up" if action == "fragment" else "down",
                     "--at", str((options.warmup if action == "fragment" else
                                   2 * options.warmup) + 1),
                     "--fragment", str(options.fragment),
                     "--trace", str(directory / "relay-summary.jsonl"),
                     "--deadline", str(options.process_timeout)]
            if transport == "tcp":
                relay += ["--tcp"]
            else:
                relay += ["--cert", str(root / "cert.pem"), "--key", str(root / "key.pem"),
                          "--ca", str(options.ca.resolve() if remote else root / "cert.pem")]
            case["commands"]["relay"] = relay
            with (directory / "relay.log").open("wb") as errors:
                proxy = Process(relay, stdout=subprocess.PIPE, stderr=errors,
                                start_new_session=True)
            processes["relay"] = proxy
            opened = pair.ready(proxy, directory / "relay.jsonl", options.ready_timeout)
            ports = opened["ports"]
        host = options.remote_host or "localhost"
        if remote and profile in ("fragmentation", "replay"):
            host = "localhost"
        command = [str(programs[f"{client_kind}_client"]), "--host", host,
                   "--ports", ",".join(map(str, ports)), *common]
        if client_kind == "native":
            command += ["--backend", options.backend]
            command += ["--rx-ring-size", str(options.rx_ring_size),
                        "--tx-ring-size", str(options.tx_ring_size)]
            if diagnostic and options.io_diagnostics:
                command += ["--observe-io", "--diag-samples", str(options.diag_samples)]
        if remote and profile in ("fragmentation", "replay") and transport == "tls":
            # The client verifies the local relay cert; relay verifies the remote CA/name.
            command[command.index("--ca") + 1] = str(root / "cert.pem")
        command = peer_command(options, command, directory, "client", diagnostic)
        case["commands"]["client"] = command
        with (directory / "client.jsonl").open("wb") as output:
            with (directory / "client.log").open("wb") as errors:
                extra = {}
                if window_sockets:
                    extra = dict(pass_fds=(window_sockets[1].fileno(),),
                                 env=dict(os.environ, ZFIX_WINDOW_FD=str(window_sockets[1].fileno())))
                processes["client"] = Process(command, stdout=output, stderr=errors,
                                              start_new_session=True, **extra)
                if window_sockets: window_sockets[1].close()
        end = time.monotonic() + options.process_timeout
        for role in ("server", "client", "relay"):
            process = processes.get(role)
            if process is None:
                continue
            remainder, _ = process.communicate(timeout=max(0, end - time.monotonic()))
            if process.stdout is not None:
                with (directory / f"{role}.jsonl").open("ab") as output:
                    output.write(remainder)
            assert process.returncode == 0, (role, process.returncode)
        clients = rows(pair, directory / "client.jsonl")
        servers = rows(pair, directory / "server.jsonl") if not remote else []
        case.update(clients=clients, servers=servers)
        for kind, observations in ((client_kind, clients), (server_kind, servers)):
            if kind == "native":
                assert all(row.get("backend") in ("epoll", "uring") for row in observations)
                if options.backend != "auto":
                    assert all(row["backend"] == options.backend for row in observations)

        assert len(clients) == options.sessions, clients
        if not remote:
            assert len(servers) == options.sessions, servers
        for row in clients:
            progress = row["progress"]
            assert row["errors"] == progress["invalid"] == progress["unknown"] == 0, row
            assert progress["complete"] == progress["admitted"] == options.count, row
            assert progress["reports"] == 2 * options.count and progress["inFlight"] == 0, row
            assert 0 < row["startNs"] < row["endNs"], row
            assert row["endNs"] - row["startNs"] == row["elapsedNs"], row
        for row in servers:
            assert row["errors"] == 0, row
            assert row["progress"]["complete"] == options.count + options.warmup, row
        if "relay" in processes:
            summary = pair.records(directory / "relay-summary.jsonl")
            assert not any(row["event"] in ("frame", "fault") for row in summary), summary
            outcome = [row for row in summary if row["event"] == "result"]
            assert len(outcome) == 1 and outcome[0]["errors"] == 0, outcome
            assert outcome[0]["faults"] > 0, outcome
            if profile == "fragmentation":
                assert outcome[0]["chunks"] > outcome[0]["frames"], outcome
            case["relay"] = outcome[0]
        case["startNs"] = min(row["startNs"] for row in clients)
        case["endNs"] = max(row["endNs"] for row in clients)
        case["elapsedNs"] = case["endNs"] - case["startNs"]
        case["status"] = "passed"
    except subprocess.TimeoutExpired:
        case.update(status="failed", timeouts=1, error="process diagnostic deadline expired")
    except Exception:
        case.update(status="failed", error=traceback.format_exc())
    finally:
        if window_sockets:
            for sock in window_sockets: sock.close()
        for process in processes.values():
            pair.stop(process)
        for role, process in processes.items():
            if process.stdout is not None and not process.stdout.closed:
                remainder, _ = process.communicate(timeout=3)
                with (directory / f"{role}.jsonl").open("ab") as output:
                    output.write(remainder)
            case["resources"][role] = {"pid": process.pid, "exit": process.returncode,
                                       "wait4": process.usage}
            if process.usage is None:
                case.update(status="failed", resourceError=f"missing wait4 usage: {role}")
        for role in ("client", "server"):
            path = directory / f"{role}.jsonl"
            if path.exists():
                try:
                    output = pair.records(path)
                    case["clients" if role == "client" else "servers"] = [
                        row for row in output if row.get("event") == "result"]
                    framework = [row for row in output if row.get("event") == "heap"]
                    assert len(framework) <= 1, framework
                    if options.framework_heaps:
                        assert len(framework) == 1, "missing requested framework heap observation"
                    if framework:
                        value = framework[0]
                        assert value["scope"] == "process-framework-heaps-at-logical-drain", value
                        for name in ("frameworkAllocCalls", "frameworkLiveBytes"):
                            assert isinstance(value[name], int) and value[name] >= 0, value
                        case.setdefault("frameworkHeap", {})[role] = value
                except (ValueError, AssertionError, KeyError):
                    case.update(status="failed", outputError=f"invalid JSON: {role}")
            if diagnostic and options.io_diagnostics and path.exists():
                kind = client_kind if role == "client" else server_kind
                if kind == "native":
                    try:
                        events = [item for item in pair.records(path)
                                  if item.get("event") == "io_diag"]
                        assert len(events) == 1, "missing or duplicate native I/O diagnostics"
                        value = events[0]
                        assert value["backend"] in ("epoll", "uring"), value
                        for owner in value["owners"]:
                            assert owner["pendingNotifs"] == owner["unpairedNotifs"] == 0, owner
                            assert owner["droppedSamples"] == 0, owner
                        if value["provenance"]:
                            assert value["rxOrigin"] == "userspace receive completion", value
                            assert value["rxPool"]["appSlots"] == value["rxPool"]["appBytes"] == 0, value
                            for frame in value["frames"]:
                                assert frame["completionToFramerNs"]["count"] > 0, frame
                                assert frame["missingOrigin"] == frame["droppedSamples"] == 0, frame
                        else:
                            assert not value["frames"] and not value["rxOrigin"], value
                        case.setdefault("ioDiagnostics", {})[role] = value
                    except (OSError, ValueError, KeyError, AssertionError, TypeError) as error:
                        case.update(status="failed", observerError=f"invalid io output: {role}: {error}")
            if diagnostic and options.syscalls and role in processes:
                try:
                    value = syscall_summary((directory / f"{role}-syscalls.txt").read_text())
                    case.setdefault("syscalls", {})[role] = value
                except (OSError, ValueError) as error:
                    case.update(status="failed", observerError=f"invalid syscall output: {role}: {error}")
            if diagnostic and not options.io_diagnostics and not options.syscalls and role in processes:
                path = directory / f"{role}-{observer}.json"
                try:
                    value = json.loads(path.read_text())
                    if observer == "audit":
                        assert value["scope"].startswith("LD_AUDIT main-namespace PLT entries")
                        for key in ("allocAttempts", "requestedBytes", "requestOverflows",
                                    "freeCalls", "copyCalls", "copyBytes",
                                    "asanCopyCalls", "asanCopyBytes"):
                            assert type(value[key]) is int and value[key] >= 0, value
                        assert value["asanCopyCalls"] <= value["copyCalls"], value
                        assert value["asanCopyBytes"] <= value["copyBytes"], value
                    case.setdefault(observer, {})[role] = value
                except (OSError, ValueError, KeyError, AssertionError, AttributeError, TypeError):
                    case.update(status="failed",
                                observerError=f"invalid {observer} output: {role}")
        case["ioMetricCoverage"] = io_metric_coverage(case, client_kind, server_kind)
        if options.process_window:
            try:
                source = HERE / "window-validate.py"
                spec = importlib.util.spec_from_file_location("zfix_window_validate", source)
                helper = importlib.util.module_from_spec(spec); spec.loader.exec_module(helper)
                raw = {}
                for role in ("client", "server"):
                    events = [v for v in pair.records(directory / f"{role}.jsonl")
                              if v.get("event") == "process_window"]
                    assert len(events) == 1, (role, "missing/duplicate measured process window")
                    raw[role] = events[0]
                case["processWindow"] = helper.validate(raw, case, options.sessions, options.count)
            except Exception:
                case.update(status="failed", processWindowError=traceback.format_exc())
        case["wallSeconds"] = time.monotonic() - started
        case["complete"] = sum(row["progress"]["complete"] for row in case.get("clients", []))
        case["unfinished"] = max(0, case["expectedOrders"] - case["complete"])
        if not diagnostic and case["complete"]:
            for observed in case["resources"].values():
                usage = observed["wait4"]
                if usage is not None:
                    observed["cpuNsPerMeasuredOrder"] = (
                        (usage["userSeconds"] + usage["systemSeconds"]) * 1e9 / case["complete"])
        case["errors"] = sum(row["errors"] for row in case.get("clients", []) +
                              case.get("servers", []))
        case["processErrors"] = sum(value["exit"] != 0 for value in case["resources"].values())
        (directory / "case.json").write_text(json.dumps(case, indent=2) + "\n")
    return case


def report(root, manifest, results):
    lines = ["# FIX process load measurements", "",
             f"Build label: {manifest['buildLabel']}. Platform: {manifest['platform']}.",
             "No configured clean release or platform acceptance is inferred from a label.", "",
             "Timing tables exclude instrumented reruns; their raw resource usage includes observer overhead. "
             "CPU/RSS cover whole local processes; latency and orders/s use each client's "
             "measured phase. Wire bytes include warmup and controls, so wire B/s uses "
             "case wall duration including setup/teardown. Shared-window orders/s divides "
             "all measured completions by the interval from the first session's measured "
             "start to the last session's measured finish in the same client process. "
             "Sum-of-session rates are also retained. LinkState quantiles are reported "
             "separately and are not averaged across sessions or repeats.", "",
             "| Pair / profile / transport / repeat | Transport | Status | Complete / unfinished | "
             "Errors / bad exits / timeouts | Shared-window orders/s | Sum session orders/s | "
             "Client CPU s / CPU ns per measured order / RSS bytes | "
             "Server CPU s / CPU ns per measured order / RSS bytes |",
             "| --- | --- | --- | --- | --- | --- | --- | --- | --- |"]

    def resource(case, role):
        value = case["resources"].get(role, {}).get("wait4")
        if value is None:
            return "unmeasured"
        cpu = value["userSeconds"] + value["systemSeconds"]
        per_order = f"{cpu * 1e9 / case['complete']:.3f}" if case["complete"] else "unmeasured"
        return f"{cpu:.6f} / {per_order} / {value['maxRssBytes']}"

    for case in results:
        if case["diagnostic"]:
            continue
        clients = case.get("clients", [])
        throughput = sum(row["progress"]["complete"] * 1e9 / row["elapsedNs"]
                         for row in clients if row["elapsedNs"])
        shared = (f"{case['complete'] * 1e9 / case['elapsedNs']:.3f}"
                  if case.get("elapsedNs") else "unmeasured")
        label = f"{case['pair']} / {case['profile']} / {case['transport']} / {case['repeat']}"
        mode = f"{case['diagnosticObserver']} diagnostic" if case["diagnostic"] else "timing"
        lines.append(f"| {label} | {mode} | "
                     f"{case['status']} | {case['complete']} / {case['unfinished']} | "
                     f"{case['errors']} / {case['processErrors']} / {case['timeouts']} | "
                     f"{shared} | {throughput:.3f} | "
                     f"{resource(case, 'client')} | {resource(case, 'server')} |")
    lines += ["", "## Work and process counters", "",
              "Wire counts include warmup, controls and any replay. Retention is the "
              "application history cache's observed high-water mark, not total heap usage.", "",
              "| Case / role / session | Offered / admitted / complete | Ack / reports | "
              "In-flight / peak | Rx / Tx frames | Rx / Tx bytes | Retained bytes |",
              "| --- | --- | --- | --- | --- | --- | --- |"]
    for case in results:
        if case["diagnostic"]:
            continue
        for role, key in (("client", "clients"), ("server", "servers")):
            for row in case.get(key, []):
                label = f"{case['pair']}/{case['profile']}/{case['transport']}/{case['repeat']}"
                p = row["progress"]
                lines.append(f"| {label} / {role} / {row['session']} | "
                             f"{p['offered']} / {p['admitted']} / {p['complete']} | "
                             f"{p['ack']} / {p['reports']} | {p['inFlight']} / {p['peak']} | "
                             f"{row['rxFrames']} / {row['txFrames']} | "
                             f"{row['rxBytes']} / {row['txBytes']} | {row['retainedPeak']} |")
    lines += ["", "Whole-process CPU/order includes startup, warmup and shutdown; "
              "its denominator is completed measured client orders across sessions.", "",
              "| Case / role | Voluntary / involuntary context switches |",
              "| --- | --- |"]
    for case in results:
        if case["diagnostic"]:
            continue
        for role, observed in case["resources"].items():
            usage = observed.get("wait4")
            if usage is None:
                continue
            label = f"{case['pair']}/{case['profile']}/{case['transport']}/{case['repeat']}"
            lines.append(f"| {label} / {role} | {usage['voluntarySwitches']} / "
                         f"{usage['involuntarySwitches']} |")
    lines += ["", "## Measured-workload process CPU", "",
              "Both endpoint intervals bracket the shared measured workload. Raw CPU includes the explicit control helper; helper-adjusted CPU bounds and monotonic sample/guard brackets remain in case.json. Warmup overlap is recorded, never silently subtracted.", "",
              "| Case / role | Phase CPU ns per measured order | Phase voluntary / involuntary switches | Helper CPU ns | Boundary sample bracket ns |",
              "| --- | --- | --- | --- | --- |"]
    for case in results:
        if case["diagnostic"] or "processWindow" not in case: continue
        for role, value in case["processWindow"]["peers"].items():
            label = "/".join(str(case[k]) for k in ("pair", "profile", "transport", "repeat"))
            delta = value["counterDelta"]
            cells = [label + " / " + role, value["phaseProcessCpuNsPerMeasuredOrder"], str(delta["voluntary"]) + " / " + str(delta["involuntary"]), delta["helperCpuNs"], value["boundarySampleBracketNs"]]
            lines.append("| " + " | ".join(str(v) for v in cells) + " |")
    lines += ["", "## Per-session latency and retention", "",
              "All latency entries are ns, in p50 / p95 / p99 / p99.9 / max order.", "",
              "| Case / session | Ack | Terminal | Scheduling delay | Intended ack | "
              "Intended terminal | Client retained bytes | Client wire B/s |",
              "| --- | --- | --- | --- | --- | --- | --- | --- |"]
    for case in results:
        if case["diagnostic"]:
            continue
        for row in case.get("clients", []):
            cells = []
            for name in ("ackNs", "terminalNs", "scheduleNs", "intendedAckNs",
                         "intendedTerminalNs"):
                value = row.get(name)
                cells.append(" / ".join(str(value[key]) for key in
                                        ("p50", "p95", "p99", "p999", "max"))
                             if value else "unavailable")
            label = f"{case['pair']}/{case['profile']}/{case['transport']}/{case['repeat']}"
            wire = (row["rxBytes"] + row["txBytes"]) / case["wallSeconds"]
            lines.append(f"| {label} / {row['session']} | " + " | ".join(cells) +
                         f" | {row['retainedPeak']} | {wire:.3f} |")
    lines += ["", "## Diagnostic allocation/copy observations", "",
              "These separate reruns change execution and are excluded from timing results. "
              "The glibc observer counts dynamic calls after initialization through its exit "
              "hook; it excludes inlined copies and custom allocator internals. Counts cover "
              "startup, warmup, measured work and shutdown. Allocation counts include only "
              "successful calls; realloc adds its full requested size, not a live-byte delta. "
              "Free counts exclude null pointers; copy counts cover dynamic memcpy/memmove.", "",
              "| Case / role | Successful allocation calls | Requested bytes | Free calls | "
              "Copy calls | Copy bytes |", "| --- | --- | --- | --- | --- | --- |"]
    for case in results:
        for role, observed in case.get("heap", {}).items():
            label = f"{case['pair']}/{case['profile']}/{case['transport']}/{case['repeat']}"
            lines.append(f"| {label} / {role} | " + " | ".join(str(observed[key]) for key in
                         ("allocCalls", "requestedBytes", "freeCalls",
                          "copyCalls", "copyBytes")) +
                         " |")
    lines += ["", "## Separate PLT audit observations", "",
              "LD_AUDIT diagnostic reruns preserve sanitizer checks and do not replace the "
              "allocator. Observations cover main-namespace PLT entries from preinit through "
              "the audit exit hook. Selected entries are malloc/calloc/realloc/aligned_alloc/"
              "posix_memalign, free, memcpy/memmove and __asan_memcpy/__asan_memmove. Other "
              "APIs, direct/local/inlined/non-PLT calls and custom allocator internals are "
              "unobserved. Allocations count attempts, not successes or live bytes; calloc "
              "products that overflow are omitted from requested bytes and counted separately. "
              "Free includes null pointers. Copy sums can include nested calls; ASan copy "
              "counts/bytes are a subset of those sums, not additional totals. These rows "
              "are excluded from timing results.", "",
              "| Case / role | Allocation attempts | Requested bytes | Request overflows | "
              "Free calls | Copy entries | Copy bytes | ASan subset entries / bytes |",
              "| --- | --- | --- | --- | --- | --- | --- | --- |"]
    for case in results:
        for role, observed in case.get("audit", {}).items():
            label = f"{case['pair']}/{case['profile']}/{case['transport']}/{case['repeat']}"
            lines.append(f"| {label} / {role} | " + " | ".join(str(observed[key]) for key in
                         ("allocAttempts", "requestedBytes", "requestOverflows", "freeCalls",
                          "copyCalls", "copyBytes")) +
                         f" | {observed['asanCopyCalls']} / {observed['asanCopyBytes']} |")
    lines += ["", "## Whole-process syscall observations", "",
              "Separate strace -f -c reruns cover each local native/QuickFIX peer process tree, "
              "including wrapper, startup, warmup and shutdown. Tracing perturbs execution; "
              "these are not latency comparisons. Raw per-syscall/error counts remain in case JSON.", "",
              "| Case / role | Syscall calls | Syscall errors | Traced syscall seconds |",
              "| --- | --- | --- | --- |"]
    for case in results:
        for role, observed in case.get("syscalls", {}).items():
            label = f"{case['pair']}/{case['profile']}/{case['transport']}/{case['repeat']}"
            total = observed["total"]
            lines.append(f"| {label} / {role} | {total['calls']} | {total['errors']} | "
                         f"{total['seconds']} |")
    lines += ["", "## Per-role I/O metric coverage", "",
              "n/a means no applicable observation channel; raw zero counters are not reinterpreted as measured socket sends.", "",
              "| Case / role | Engine / backend | Ordinary-fallback coverage | Native delivery-to-framer coverage |",
              "| --- | --- | --- | --- |"]
    for case in results:
        if not case["diagnostic"] or case.get("diagnosticObserver") != "io": continue
        for role, coverage in case.get("ioMetricCoverage", {}).items():
            label = "/".join(str(case[k]) for k in ("pair", "profile", "transport", "repeat"))
            status = lambda field: "n/a" if coverage[field]["status"] == "not-applicable" else coverage[field]["status"]
            lines.append(f"| {label} / {role} | {coverage['engine']} / {coverage['backend']} | "
                         f"{status('ordinaryFallback')} | {status('deliveryToFramer')} |")
    lines += ["", "## Native I/O observations", "",
              "Instrumented reruns only. Enter counts are actual ring syscalls; "
              "epoll has no ring events. Pool/copy provenance requires ZiMultiplex_DEBUG. "
              "Fallback counts are actual io_uring TxData policy-classified ordinary results/bytes, "
              "not all ordinary socket attempts. Epoll has no ring TxData event channel and "
              "QuickFIX is external: fallback is n/a for both; syscall attempts are measured separately. "
              "Copy bytes cover buffer preservation/moves/gathers, "
              "excluding crypto internals. Full per-SID/opcode and pool snapshots remain in case JSON.", "",
              "| Case / role | Enter calls / waits | Accepted SQEs / CQEs | "
              "io_uring fallback results / bytes | Rx / Tx peak slot bytes | Pool depletion | Copy bytes |",
              "| --- | --- | --- | --- | --- | --- | --- |"]
    for case in results:
        for role, observed in case.get("ioDiagnostics", {}).items():
            label = f"{case['pair']}/{case['profile']}/{case['transport']}/{case['repeat']}"
            owners = observed["owners"]
            total = lambda key: sum(owner[key] for owner in owners)
            fallback = (f"{total('ordinarySends')} / {total('ordinaryBytes')}"
                        if observed["backend"] == "uring" else "n/a")
            pool_stats = "disabled | disabled | disabled"
            if observed["provenance"]:
                rx, tx = observed["rxPool"], observed["txPool"]
                copies = sum(pool[key] for pool in (rx, tx)
                             for key in ("preserveBytes", "moveBytes", "gatherBytes"))
                pool_stats = (f"{rx['peakSlotBytes']} / {tx['peakSlotBytes']} | "
                              f"{rx['depletion'] + tx['depletion']} | {copies}")
            lines.append(f"| {label} / {role} | {total('enterCalls')} / {total('enterWaits')} | "
                         f"{total('acceptedSQEs')} / {total('cqes')} | "
                         f"{fallback} | "
                         f"{pool_stats} |")
    lines += ["", "Native DEBUG latest-delivery-to-framer ns (p50 / p99 / p99.9 / max); "
              "origin is the latest contributing userspace payload-delivery callback. "
              "This is not raw CQE dequeue, kernel/NIC arrival, first-fragment delay or "
              "an all-engine framer comparison.", "",
              "| Case / role / session | Completion-to-framer | Data-CQE-to-NOTIF by SID |",
              "| --- | --- | --- |"]
    for case in results:
        for role, observed in case.get("ioDiagnostics", {}).items():
            label = f"{case['pair']}/{case['profile']}/{case['transport']}/{case['repeat']}"
            fmt = lambda q: " / ".join(str(q[k]) for k in ("p50", "p99", "p999", "max"))
            notif = "; ".join(f"{owner['sid']}: {fmt(owner['notificationNs'])}"
                              for owner in observed["owners"] if owner["notificationNs"]["count"])
            if not observed["provenance"]:
                lines.append(f"| {label} / {role} | disabled | {notif or 'no notifications'} |")
            for frame in observed["frames"]:
                lines.append(f"| {label} / {role} / {frame['session']} | "
                             f"{fmt(frame['completionToFramerNs'])} | {notif or 'no notifications'} |")
    lines += ["", "## Framework heap observations", "",
              "Cold logical-drain samples cover Ztc::HeapMgr arenas only. They exclude SDK, "
              "STL and libc allocation paths; live bytes are a drain-time sample, not peak "
              "resident memory or total process heap usage.", "",
              "| Case / role | Framework allocation calls | Framework live bytes |",
              "| --- | --- | --- |"]
    for case in results:
        for role, observed in case.get("frameworkHeap", {}).items():
            label = f"{case['pair']}/{case['profile']}/{case['transport']}/{case['repeat']}"
            if case["diagnostic"]:
                label += f"/{case['diagnosticObserver']}-diagnostic"
            lines.append(f"| {label} / {role} | {observed['frameworkAllocCalls']} | "
                         f"{observed['frameworkLiveBytes']} |")
    lines += ["", "## Comparability and evidence", ""]
    lines += [f"- {key}: {value}." for key, value in manifest["differences"].items()]
    lines += ["", "Actual ELF hashes, file bytes, section sizes, linked-library lists, compiler "
              "flags, SDK/dictionary pins and CPU metadata are in manifest.json. All commands, "
              "exits, raw session quantiles, wire counts, resource observations and failures "
              "are in each case.json. The independent correctness gate supplies detailed "
              "replay/OriginalSendingTime proof; aggregate timing relay counters do not.", ""]
    (root / "report.md").write_text("\n".join(lines))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name, default in (("native-client", BASE / "util" / "zfix"),
                          ("native-server", BASE / "util" / "zfixd"),
                          ("quickfix-client", BASE / "interop" / "quickfix"),
                          ("quickfix-server", BASE / "interop" / "quickfixd")):
        parser.add_argument(f"--{name}", type=Path, default=default)
    parser.add_argument("--dictionary", type=Path, required=True)
    parser.add_argument("--artifacts", type=Path)
    parser.add_argument("--pair", choices=PAIRS, action="append")
    parser.add_argument("--profile", choices=PROFILES, action="append")
    parser.add_argument("--transport", choices=("tcp", "tls"), action="append")
    parser.add_argument("--backend", choices=("auto", "epoll", "uring"), default="auto")
    parser.add_argument("--rx-ring-size", type=int, default=0,
                        help="native Rx slots; zero sizes for sessions/in-flight traffic")
    parser.add_argument("--tx-ring-size", type=int, default=0,
                        help="native Tx slots per policy; zero sizes for in-flight traffic")
    parser.add_argument("--count", type=int, default=10000)
    parser.add_argument("--warmup", type=int, default=1000)
    parser.add_argument("--rate", type=int, default=0)
    parser.add_argument("--overload-rate", type=int, default=1000000)
    parser.add_argument("--inflight", type=int, default=64)
    parser.add_argument("--sessions", type=int, default=1)
    parser.add_argument("--repeat", type=int, default=3)
    parser.add_argument("--retain", type=int, default=4096)
    parser.add_argument("--cache-bytes", type=int, default=64 << 20)
    parser.add_argument("--queue-bytes", type=int, default=16 << 20)
    parser.add_argument("--parties", type=int, default=32)
    parser.add_argument("--subs", type=int, default=8)
    parser.add_argument("--text", type=int, default=4096)
    parser.add_argument("--fragment", type=int, default=64)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--sender", default="ZFIX-C")
    parser.add_argument("--target", default="ZFIX-S")
    parser.add_argument("--deadline", type=int, default=120)
    parser.add_argument("--ready-timeout", type=float, default=15)
    parser.add_argument("--process-timeout", type=float, default=150)
    parser.add_argument("--build-label", default="unverified")
    observer = parser.add_mutually_exclusive_group()
    observer.add_argument("--heaptrace", type=Path)
    observer.add_argument("--audittrace", type=Path)
    observer.add_argument("--syscalls", action="store_true",
                          help="repeat cases under strace -f -c; exclude from timing tables")
    observer.add_argument("--io-diagnostics", action="store_true",
                          help="collect diagnostic cases only; use an ordinary build for timing")
    parser.add_argument("--diag-samples", type=int, default=0,
                        help="native per-session sample cap; zero derives a bounded cap from workload")
    parser.add_argument("--framework-heaps", action="store_true")
    parser.add_argument("--process-window", action="store_true",
                        help="require exact paired process CPU windows around shared measured work")
    parser.add_argument("--interface", help="capture passive metadata for this explicit local interface")
    parser.add_argument("--remote-host")
    parser.add_argument("--remote-ports",
                        type=lambda value: [int(port) for port in value.split(",")])
    parser.add_argument("--remote-client", choices=("native", "quickfix"), default="native")
    parser.add_argument("--remote-server-label", default="external")
    parser.add_argument("--ca", type=Path)
    options = parser.parse_args()
    if options.syscalls and (sys.platform != "linux" or not shutil.which("strace")):
        parser.error("--syscalls requires Linux strace")
    if options.diag_samples < 0:
        parser.error("--diag-samples must be nonnegative")
    if options.rx_ring_size < 0 or options.tx_ring_size < 0:
        parser.error("ring sizes must be nonnegative; native peers validate kernel bounds")
    if not hasattr(os, "wait4"):
        parser.error("wait4 process resource measurement requires a supported POSIX host")
    if any(getattr(options, name) <= 0 for name in (
            "count", "inflight", "sessions", "repeat", "retain", "cache_bytes", "queue_bytes",
            "parties", "subs", "text", "fragment", "deadline", "ready_timeout",
            "process_timeout", "overload_rate")) or options.warmup < 0 or options.rate < 0:
        parser.error("positive bounds and nonnegative warmup/rate required")
    if bool(options.remote_host) != bool(options.remote_ports):
        parser.error("--remote-host and --remote-ports must be supplied together")
    transports = options.transport or ["tcp", "tls"]
    if options.remote_host:
        if options.pair:
            parser.error("select --remote-client instead of --pair in remote mode")
        if len(options.remote_ports) != options.sessions or any(
                not 0 < port < 65536 for port in options.remote_ports):
            parser.error("remote ports must match sessions and be valid TCP ports")
        if "tls" in transports and options.ca is None:
            parser.error("remote TLS requires an explicit trusted --ca")
    if (options.heaptrace or options.audittrace) and any(
            os.environ.get(name) for name in ("LD_PRELOAD", "LD_AUDIT")):
        parser.error("diagnostics require no inherited preload/audit observer")
    if options.heaptrace and any(os.environ.get(name) for name in
                                 ("ASAN_OPTIONS", "LSAN_OPTIONS")):
        parser.error("heap diagnostics must run without another preload or sanitizer environment")
    if options.heaptrace and (not options.heaptrace.is_file() or
                              platform.libc_ver()[0] != "glibc"):
        parser.error("heap diagnostics require the existing glibc observer shared library")
    if options.audittrace and (not options.audittrace.is_file() or
                               sys.platform != "linux" or platform.libc_ver()[0] != "glibc" or
                               platform.machine() not in ("x86_64", "amd64")):
        parser.error("audit diagnostics require the existing Linux x64/glibc observer library")
    pair = load_pair()
    expected = next(line.split("=", 1)[1] for line in
                    (BASE / "interop" / "quickfix.lock").read_text().splitlines()
                    if line.startswith("QUICKFIX_FIX44_SHA256="))
    if pair.digest(options.dictionary) != expected:
        parser.error("canonical dictionary hash mismatch")
    root = options.artifacts.resolve() if options.artifacts else Path(
        tempfile.mkdtemp(prefix="zfix-load-"))
    root.mkdir(parents=True, exist_ok=True)
    if any(root.iterdir()):
        parser.error(f"artifact directory must be empty: {root}")
    names = ((f"{options.remote_client}_client",) if options.remote_host else
             ("native_client", "native_server", "quickfix_client", "quickfix_server"))
    programs = {name: getattr(options, name).resolve() for name in names}
    manifest = metadata(options, pair, programs)
    if "tls" in transports:
        pair.certificate(root)
        manifest["certificateSha256"] = pair.digest(root / "cert.pem")
    (root / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(json.dumps({"event": "artifacts", "path": str(root)}), flush=True)
    labels = options.pair or PAIRS
    if options.remote_host:
        labels = [f"{options.remote_client}-{options.remote_server_label}"]
    instrumented = options.heaptrace or options.audittrace or options.io_diagnostics or options.syscalls
    modes = [False]
    if options.io_diagnostics:
        modes = [True]
    elif instrumented:
        modes.append(True)
    jobs = [(label, transport, profile, repeat, diagnostic)
            for label in labels
            for transport in transports
            for profile in options.profile or PROFILES
            for repeat in range(1, options.repeat + 1)
            for diagnostic in modes]
    random.Random(options.seed).shuffle(jobs)
    manifest["executionOrder"] = jobs
    (root / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    results = []
    for label, transport, profile, repeat, diagnostic in jobs:
        case = run_case(options, pair, root, programs, label, profile,
                        transport, repeat, diagnostic)
        results.append(case)
        report(root, manifest, results)
        print(json.dumps({key: case[key] for key in (
            "pair", "profile", "transport", "repeat", "diagnostic", "status",
            "complete", "unfinished", "errors", "timeouts")}), flush=True)
    summary = {"cases": len(results),
               "failed": sum(case["status"] != "passed" for case in results),
               "buildLabel": options.build_label, "artifacts": str(root)}
    (root / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    return bool(summary["failed"])


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:
        print(f"load benchmark: {error}", file=sys.stderr)
        sys.exit(1)
