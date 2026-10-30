#!/usr/bin/env python3
"""Summarize retained process/codec measurements without merging quantiles."""
import argparse
import collections
import json
from pathlib import Path
import statistics


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("artifacts", type=Path)
    parser.add_argument("--codec", type=Path, action="append", default=[])
    parser.add_argument("--output", type=Path, required=True)
    options = parser.parse_args()
    root = options.artifacts.resolve()
    manifest = json.loads((root / "manifest.json").read_text())
    summary = json.loads((root / "summary.json").read_text())
    assert not summary["failed"], summary
    paths = sorted(root.glob("*/case.json"))
    assert len(paths) == summary["cases"] > 0, "incomplete retained case evidence"
    groups = collections.defaultdict(list)
    for path in paths:
        case = json.loads(path.read_text())
        assert case["status"] == "passed", path
        if case["diagnostic"]:
            continue
        assert case["clients"], f"missing client results: {path}"
        for row in case["clients"]:
            assert row["progress"]["complete"] > 0 and row["elapsedNs"] > 0, path
            groups[(case["pair"], case["transport"], case["profile"], row["session"])].append(row)
    assert groups, "no retained timing cases"
    flags = manifest["buildFlags"]
    lines = ["# FIX measured baseline", "",
             f"Build label: `{manifest['buildLabel']}`. Platform: {manifest['platform']}.",
             f"CPU: {'; '.join(manifest['cpu'])}. Affinity: {manifest['affinity']}.",
             f"Compiler: `{flags.get('CXX')}`. Flags: `{flags.get('CXXFLAGS')}`.", "",
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
             "| Pair / transport / shape / session | Runs | Orders/s median | "
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
                        "session": key[3], "runs": len(rows),
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
    lines += ["", "Raw artifacts: `" + str(root) + "`.", ""]
    options.output.write_text("\n".join(lines))
    (root / "budgets.json").write_text(json.dumps({"scope": manifest["buildLabel"],
        "multiplier": 1.25, "hostSpecific": True, "budgets": budgets}, indent=2) + "\n")


if __name__ == "__main__":
    main()
