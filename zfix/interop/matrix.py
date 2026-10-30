#!/usr/bin/env python3
"""Run independent FIX process pairings and retain their evidence."""
import argparse
import contextlib
import importlib.util
import json
from pathlib import Path
import sys
import tempfile

HERE = Path(__file__).resolve().parent
PAIR_HELPER = HERE.parent / "util" / "pair-test.py"
spec = importlib.util.spec_from_file_location("zfix_pair", PAIR_HELPER)
peer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(peer)


def run_profile(options, root, label, client, server, native_versions=False):
    directory = root / label
    cases = peer.pair_cases(smoke=options.profile == "smoke",
                            canonical=not native_versions,
                            workflows=not native_versions and options.profile != "smoke",
                            names=["44A", "T11"] if native_versions else None)
    with (root / f"{label}.jsonl").open("w") as output, \
            (root / f"{label}.log").open("w") as errors, \
            contextlib.redirect_stdout(output), contextlib.redirect_stderr(errors):
        peer.run_pair(client, server, directory, cases, pair=label,
                      tcp_only=options.tcp_only, canonical=not native_versions,
                      workflows=not native_versions and options.profile != "smoke",
                      arguments={"profile": options.profile, "native_versions": native_versions,
                                 "client": str(client), "server": str(server),
                                 "artifacts": str(directory)}, timeout=options.timeout)
    cases = [json.loads(path.read_text()) for path in directory.glob("*/case.json")]
    per_transport = 2 if native_versions else 1 if options.profile == "smoke" else 9
    assert len(cases) == per_transport * (1 if options.tcp_only else 2)
    assert all(case["status"] == "passed" for case in cases)
    return len(cases)


def run_fault(options, root, pair, client, server, transport, entry):
    name, action, direction, kind = entry
    receiver = pair.split("-")[1 if direction == "up" else 0]
    negative = action == "qty" or action in ("checksum", "length") and receiver == "native"
    negative |= action == "unknown" and receiver == "quickfix"
    directory = root / pair / "faults"
    directory.mkdir(parents=True, exist_ok=True)
    if transport == "tls" and not (directory / "cert.pem").exists():
        peer.certificate(directory)
    relay = ["--action", action, "--direction", direction, "--kind", kind]
    label = f"{pair}-{transport}-{name}"
    with (root / f"{label}.jsonl").open("w") as output, \
            (root / f"{label}.log").open("w") as errors, \
            contextlib.redirect_stdout(output), contextlib.redirect_stderr(errors):
        peer.run_case(str(client), str(server), directory, transport, name, [], 7, 0,
                      negative=negative, pair=pair, relay=relay)
    return 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--native-client", type=Path, default=HERE.parent / "util" / "zfix")
    parser.add_argument("--native-server", type=Path, default=HERE.parent / "util" / "zfixd")
    parser.add_argument("--quickfix-client", type=Path, default=HERE / "quickfix")
    parser.add_argument("--quickfix-server", type=Path, default=HERE / "quickfixd")
    parser.add_argument("--profile", choices=("smoke", "canonical", "faults", "all"),
                        default="canonical")
    parser.add_argument("--pair", action="append", choices=("native-native", "native-quickfix",
                                                            "quickfix-native", "quickfix-quickfix"))
    parser.add_argument("--fault-case", action="append")
    parser.add_argument("--tcp-only", action="store_true")
    parser.add_argument("--artifacts", type=Path)
    parser.add_argument("--timeout", type=float, default=300)
    options = parser.parse_args()
    root = options.artifacts.resolve() if options.artifacts else Path(
        tempfile.mkdtemp(prefix="zfix-matrix-"))
    root.mkdir(parents=True, exist_ok=True)
    if any(root.iterdir()):
        parser.error(f"artifact directory must be empty: {root}")
    pairs = {
        "native-native": (options.native_client.resolve(), options.native_server.resolve()),
        "native-quickfix": (options.native_client.resolve(), options.quickfix_server.resolve()),
        "quickfix-native": (options.quickfix_client.resolve(), options.native_server.resolve()),
        "quickfix-quickfix": (options.quickfix_client.resolve(), options.quickfix_server.resolve())}
    if options.pair:
        pairs = {key: value for key, value in pairs.items() if key in options.pair}
    faults = [(f"{action}-{direction}", action, direction, kind)
              for action in ("fragment", "drop", "duplicate", "unknown", "checksum", "length")
              for direction, kind in (("up", "D"), ("down", "8"))]
    faults += [("qty-up", "qty", "up", "D")]
    if options.fault_case:
        names = {entry[0] for entry in faults}
        if set(options.fault_case) - names:
            parser.error("unknown fault case")
        faults = [entry for entry in faults if entry[0] in options.fault_case]
    actions = []
    if options.profile != "faults":
        for label, (client, server) in pairs.items():
            actions.append((label, lambda label=label, client=client, server=server:
                            run_profile(options, root, label, client, server)))
        if options.profile != "smoke" and "native-native" in pairs:
            client, server = pairs["native-native"]
            actions.append(("native-versions", lambda client=client, server=server: run_profile(
                options, root, "native-versions", client, server, True)))
    if options.profile in ("faults", "all"):
        for pair, (client, server) in pairs.items():
            for transport in (["tcp"] if options.tcp_only else ["tcp", "tls"]):
                for entry in faults:
                    actions.append((f"{pair}-{transport}-{entry[0]}",
                                    lambda pair=pair, client=client, server=server,
                                    transport=transport, entry=entry:
                                    run_fault(options, root, pair, client, server, transport, entry)))
    print(f"TAP version 14\n1..{len(actions)}\n# artifacts: {root}", flush=True)
    passed = failed = cases = 0
    for label, run in actions:
        try:
            cases += run()
            passed += 1
            print(f"ok {passed + failed} - {label}", flush=True)
        except Exception as error:
            failed += 1
            print(f"not ok {passed + failed} - {label}\n# {error}", flush=True)
    summary = {"passed": passed, "failed": failed, "cases": cases, "artifacts": str(root),
               "profile": options.profile,
               "differences": {"native_server_bind": "127.0.0.1",
                               "quickfix_server_bind": "0.0.0.0 (SDK API)",
                               "quickfix_outgoing_counts": "SDK wire attempts, not socket completion",
                               "history": "application-selected native bodies; "
                                          "QuickFIX serialized frames"}}
    (root / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
