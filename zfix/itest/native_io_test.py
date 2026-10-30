#!/usr/bin/env python3
"""Run separate FIX utility processes and verify their end-to-end results."""
import argparse
import importlib.util
from pathlib import Path
import sys
import tempfile

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("zfix_pair", HERE.parent / "util" / "pair-test.py")
peer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(peer)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--client", default=str(HERE.parent / "util" / "zfix"))
    parser.add_argument("--server", default=str(HERE.parent / "util" / "zfixd"))
    parser.add_argument("--pair", default="native-native")
    parser.add_argument("--artifacts", type=Path)
    parser.add_argument("--tcp-only", action="store_true")
    parser.add_argument("--smoke", action="store_true")
    parser.add_argument("--canonical", action="store_true")
    parser.add_argument("--workflows", action="store_true")
    parser.add_argument("--case", action="append", dest="cases")
    parser.add_argument("--ready-timeout", type=float, default=10)
    parser.add_argument("--process-timeout", type=float, default=40)
    parser.add_argument("--fault", choices=("pass", "drop", "duplicate", "fragment",
                                            "unknown", "qty", "checksum", "length"))
    parser.add_argument("--fault-kind", default="D")
    parser.add_argument("--fault-direction", choices=("up", "down"), default="up")
    parser.add_argument("--expect-reject", action="store_true")
    options = parser.parse_args()
    root = (options.artifacts.resolve() if options.artifacts
            else Path(tempfile.mkdtemp(prefix="zfix-pair-")))
    try:
        cases = peer.pair_cases(options.smoke, options.canonical,
                                options.workflows, options.cases)
    except ValueError as error:
        parser.error(str(error))
    receiver = options.pair.split("-")[1 if options.fault_direction == "up" else 0]
    if (options.expect_reject or options.fault == "qty" or
            options.fault in ("checksum", "length") and receiver == "native"):
        cases = [(*case[:-1], True) for case in cases]
    relay = (["--action", options.fault, "--kind", options.fault_kind,
              "--direction", options.fault_direction] if options.fault else None)
    peer.run_pair(options.client, options.server, root, cases,
                  pair=options.pair, tcp_only=options.tcp_only,
                  canonical=options.canonical, workflows=options.workflows,
                  arguments=vars(options) | {"artifacts": str(root)}, relay=relay,
                  ready_timeout=options.ready_timeout, process_timeout=options.process_timeout)
    return 0


if __name__ == "__main__":
    sys.exit(main())
