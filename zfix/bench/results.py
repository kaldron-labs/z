#!/usr/bin/env python3
"""Summarize retained process/codec measurements without merging quantiles."""
import argparse
import collections
import json
from pathlib import Path
import statistics


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("artifacts", type=Path, nargs="+")
    parser.add_argument("--codec", type=Path, action="append", default=[])
    parser.add_argument("--output", type=Path, required=True)
    options = parser.parse_args()
    roots = [root.resolve() for root in options.artifacts]
    manifest = None
    groups = collections.defaultdict(list)
    processes = collections.defaultdict(list)
    backends = set()
    for root in roots:
        current = json.loads((root / "manifest.json").read_text())
        if manifest is None:
            manifest = current
        else:
            for key in ("buildFlags", "dictionarySha256",
                        "cpu", "affinity", "sourceFiles"):
                assert current[key] == manifest[key], f"different {key}: {root}"
            for key, digest in (("programs", "elfSha256"), ("linkedLibraries", "sha256")):
                assert {name: value[digest] for name, value in current[key].items()} == {
                    name: value[digest] for name, value in manifest[key].items()}, (
                        f"different {key}: {root}")
            for key in ("count", "warmup", "inflight", "rate", "overload_rate", "retain",
                        "cache_bytes", "queue_bytes", "parties", "subs", "text",
                        "fragment", "seed", "backend"):
                assert current["arguments"][key] == manifest["arguments"][key], (
                    f"different workload {key}: {root}")
        summary = json.loads((root / "summary.json").read_text())
        assert not summary["failed"], summary
        paths = sorted(root.glob("*/case.json"))
        assert len(paths) == summary["cases"] > 0, "incomplete retained case evidence"
        for path in paths:
            case = json.loads(path.read_text())
            assert case["status"] == "passed", path
            if case["diagnostic"]:
                continue
            sessions = current["arguments"]["sessions"]
            assert len(case["clients"]) == sessions, f"missing client results: {path}"
            requested = current["arguments"]["backend"]
            assert requested in ("auto", "epoll", "uring"), requested
            for row in case["clients"] + case["servers"]:
                backend = row["backend"]
                if row["engine"] == "zfix":
                    assert backend in ("epoll", "uring"), f"unresolved native backend: {path}"
                    assert requested == "auto" or backend == requested, (
                        f"backend mismatch: requested {requested}, resolved {backend}: {path}")
                    backends.add(backend)
                else:
                    assert backend == "external", f"unexpected peer backend: {path}"
            key = (case["pair"], case["transport"], case["profile"], sessions)
            processes[key].append(case)
            for row in case["clients"]:
                assert row["progress"]["complete"] > 0 and row["elapsedNs"] > 0, path
                groups[(*key, row["session"])].append(row)
    assert groups, "no retained timing cases"
    assert len(backends) <= 1, f"different resolved backends: {sorted(backends)}"
    requested_backend = manifest["arguments"]["backend"]
    resolved_backend = next(iter(backends), "external")
    flags = manifest["buildFlags"]
    lines = ["# FIX measured baseline", "",
             f"Build label: `{manifest['buildLabel']}`. Platform: {manifest['platform']}.",
             f"CPU: {'; '.join(manifest['cpu'])}. Affinity: {manifest['affinity']}.",
             f"Compiler: `{flags.get('CXX')}`. Flags: `{flags.get('CXXFLAGS')}`.", "",
             f"Native backend requested: `{requested_backend}`; resolved: `{resolved_backend}`. "
             "QuickFIX uses its independent external transport.", "",
             "These are local process measurements on the recorded host. The table uses "
             "median per-session completion rate and median p50 across repeats; worst p99 "
             "is the largest individual-run p99. Quantiles are never added or merged. "
             "Full p50/p95/p99/p99.9/max, wire counts, resources, retained bytes, errors, "
             "diagnostic allocation/copy counts, exact commands and hashes are in the "
             "adjacent report.md, manifest.json and case.json files.", "",
             "The regression envelope is 1.25 times the worst observed intended-terminal "
             "p99, rounded up. It is a measured baseline for equivalent runs on this host, "
             "not a production service-level promise or a portable CI threshold. Startup, "
             "TLS relay and shared-host effects are described in report.md. Overload "
             "intended latency includes the delay before the bounded client can submit.", "",
             "## Client and server comparison", "",
             "Shared-window rate divides all measured completions by the interval from "
             "the first session's measured start to the last session's measured finish "
             "in the same client process. Sum-of-session rates use independent intervals. "
             "Rates are medians across repetitions. Worst p99 is the largest session p99 in any "
             "repetition. Both peers run in separate local processes.", "",
             "| Pair / transport / shape | Sessions | Runs | Shared-window orders/s median | "
             "Sum session orders/s median | "
             "Ack p99 worst ms | Terminal p99 worst ms |",
             "| --- | --- | --- | --- | --- | --- | --- |"]
    comparison = []
    for key, cases in sorted(processes.items()):
        rate = statistics.median(sum(row["progress"]["complete"] * 1e9 / row["elapsedNs"]
                                     for row in case["clients"]) for case in cases)
        shared = (statistics.median(case["complete"] * 1e9 / case["elapsedNs"]
                                    for case in cases) if all(case.get("elapsedNs")
                                    for case in cases) else None)
        shared_text = f"{shared:.1f}" if shared is not None else "unmeasured"
        ack99 = max(row["ackNs"]["p99"] for case in cases for row in case["clients"])
        terminal99 = max(row["terminalNs"]["p99"] for case in cases for row in case["clients"])
        lines.append(f"| {' / '.join(map(str, key[:3]))} | {key[3]} | {len(cases)} | "
                     f"{shared_text} | {rate:.1f} | {ack99 / 1e6:.3f} | {terminal99 / 1e6:.3f} |")
        item = dict(pair=key[0], transport=key[1], profile=key[2], sessions=key[3],
                    runs=len(cases), sharedWindowRateMedian=shared, sumSessionRateMedian=rate,
                    ackP99WorstNs=ack99, terminalP99WorstNs=terminal99)
        for role in ("client", "server"):
            values = [case["resources"].get(role, {}).get("wait4") for case in cases]
            if all(value is not None for value in values):
                item[role] = dict(cpuSecondsMedian=statistics.median(
                    value["userSeconds"] + value["systemSeconds"] for value in values),
                    maxRssBytes=max(value["maxRssBytes"] for value in values))
        comparison.append(item)
    lines += ["", "## Local process resources", "",
              "CPU seconds include startup, warmup and shutdown. RSS is the largest "
              "process high-water mark across repetitions; fork/exec can inherit the "
              "parent's pre-exec high-water floor. These are whole-process observations, "
              "not isolated measured-phase costs.", "",
              "| Pair / transport / shape | Sessions | Client CPU s median | "
              "Server CPU s median | Client RSS MiB max | Server RSS MiB max |",
              "| --- | --- | --- | --- | --- | --- |"]
    for item in comparison:
        label = ' / '.join(item[key] for key in ("pair", "transport", "profile"))
        def resource(role, field, scale=1):
            return (f"{item[role][field] / scale:.3f}" if role in item else "unmeasured")
        lines.append(f"| {label} | {item['sessions']} | " + " | ".join((
            resource("client", "cpuSecondsMedian"), resource("server", "cpuSecondsMedian"),
            resource("client", "maxRssBytes", 1 << 20),
            resource("server", "maxRssBytes", 1 << 20))) + " |")
    lines += ["", "## Individual session measurements", "",
             "| Pair / transport / shape / session count / session | Runs | Orders/s median | "
             "Ack p50 median ns | Ack p99 worst ns | Terminal p99 worst ns | "
             "Intended terminal p99 worst ns | Regression envelope ns |",
             "| --- | --- | --- | --- | --- | --- | --- | --- |"]
    budgets = []
    for key, rows in sorted(groups.items()):
        assert all(r["intendedTerminalNs"]["count"] == r["progress"]["complete"]
                   for r in rows)
        throughput = statistics.median(
            r["progress"]["complete"] * 1e9 / r["elapsedNs"] for r in rows)
        ack50 = statistics.median(r["ackNs"]["p50"] for r in rows)
        ack99 = max(r["ackNs"]["p99"] for r in rows)
        terminal99 = max(r["terminalNs"]["p99"] for r in rows)
        intended99 = max(r["intendedTerminalNs"]["p99"] for r in rows)
        budget = (intended99 * 5 + 3) // 4
        lines.append(f"| {' / '.join(map(str, key))} | {len(rows)} | {throughput:.1f} | "
                     f"{ack50:.0f} | {ack99} | {terminal99} | {intended99} | {budget} |")
        budgets.append({"pair": key[0], "transport": key[1], "profile": key[2],
                        "sessions": key[3], "session": key[4], "runs": len(rows),
                        "observedIntendedTerminalP99MaxNs": intended99,
                        "regressionEnvelopeNs": budget})
    codecs = collections.defaultdict(list)
    for path in options.codec:
        values = [json.loads(line) for line in path.read_text().splitlines() if line.strip()]
        assert values, f"empty codec evidence: {path}"
        for value in values:
            assert value["event"] == "codec" and value["bytes"] > 0 and value["count"] > 0
            assert all(value[k]["count"] == value["count"] for k in
                       ("encodeNs", "decodeNs", "clockNs")), path
            codecs[(value["engine"], value["shape"])].append(value)
    if codecs:
        lines += ["", "## Standalone codec", "",
                  "Per-call encode includes typed body and full FIX framing; decode includes "
                  "framing/checksum, typed construction and validation. QuickFIX also applies "
                  "its canonical dictionary validation. Native reuses its output backing; "
                  "QuickFIX constructs its SDK message and returns its SDK string. "
                  "QuickFIX decode includes its input string and selected-field arena copies; "
                  "Native decoded spans borrow input. These ownership costs are included. "
                  "Clock-pair overhead is reported separately without subtraction.", "",
                  "| Engine / shape | Runs | Frame bytes | Encode p50 median ns | "
                  "Encode p99 worst ns | Decode p50 median ns | Decode p99 worst ns | "
                  "Clock p50 median ns |", "| --- | --- | --- | --- | --- | --- | --- | --- |"]
        for key, rows in sorted(codecs.items()):
            lines.append(f"| {' / '.join(key)} | {len(rows)} | {rows[0]['bytes']} | " +
                         " | ".join(str(int(value)) for value in (
                             statistics.median(r["encodeNs"]["p50"] for r in rows),
                             max(r["encodeNs"]["p99"] for r in rows),
                             statistics.median(r["decodeNs"]["p50"] for r in rows),
                             max(r["decodeNs"]["p99"] for r in rows),
                             statistics.median(r["clockNs"]["p50"] for r in rows))) + " |")
    lines += ["", "Raw artifacts: " + ", ".join(f"`{root}`" for root in roots) + ".", ""]
    options.output.write_text("\n".join(lines))
    (options.output.parent / "budgets.json").write_text(json.dumps({"scope": manifest["buildLabel"],
        "requestedBackend": requested_backend, "resolvedBackend": resolved_backend,
        "multiplier": 1.25, "hostSpecific": True, "budgets": budgets}, indent=2) + "\n")
    options.output.with_suffix(".json").write_text(json.dumps({
        "buildLabel": manifest["buildLabel"], "requestedBackend": requested_backend,
        "resolvedBackend": resolved_backend, "comparison": comparison}, indent=2) + "\n")


if __name__ == "__main__":
    main()
