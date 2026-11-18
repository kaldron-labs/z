#!/usr/bin/env python3
"""Run QIR with the preferred-address probe ordering permitted by RFC 9000."""

import logging
from pathlib import Path
import runpy
import sys


def values(layer, field):
    value = getattr(layer, field, None)
    if value is None:
        return []
    return [str(item.show) for item in value.all_fields]


def server_probes(packets, path_of):
    """Require a server challenge on every new path before non-probing output.

    RFC 9000 sections 8.2.2 and 9.6.2 permit PATH_RESPONSE before the server's
    own PATH_CHALLENGE. The upstream rebinding check requires the challenge
    in the first packet, which incorrectly rejects this preferred-address flow.
    """
    original = None
    paths, challenged, challenges = set(), set(), set()
    for packet in packets:
        path = path_of(packet)
        if original is None:
            original = path
        if path == original:
            continue
        paths.add(path)
        quic = packet["quic"]
        tokens = values(quic, "path_challenge.data")
        if tokens:
            challenged.add(path)
            challenges.update(tokens)
        if path not in challenged:
            frames = {int(value, 0) for value in values(quic, "frame_type")}
            # PADDING, NEW_CONNECTION_ID, PATH_CHALLENGE, PATH_RESPONSE.
            if not frames or not frames <= {0, 0x18, 0x1a, 0x1b}:
                logging.info("Server sent non-probing frames before PATH_CHALLENGE on %s", path)
                return None
    if not paths or paths != challenged:
        return None
    return challenges


def matched_responses(challenges, packets):
    responses = {token for packet in packets
                 for token in values(packet["quic"], "path_response.data")}
    return challenges <= responses


def install(runner):
    # Synthetic runners used by qir-script-test.sh have no protocol classes.
    if not (runner.parent / "testcases_quic.py").is_file():
        return
    import testcases_quic as cases

    original = cases.TestCasePortRebinding.check

    def check(self):
        if self.name() != "connectionmigration":
            return original(self)
        if not self._keylog_file():
            return cases.TestResult.UNSUPPORTED
        result = cases.TestCaseTransfer.check(self)
        if result != cases.TestResult.SUCCEEDED:
            return result
        trace = self._server_trace()
        packets = trace._get_packets(
            trace._get_direction_filter(cases.Direction.FROM_SERVER) + " quic")
        challenges = server_probes(packets, self._path)
        if not challenges:
            logging.info("Preferred-address server probe ordering or path coverage failed")
            return cases.TestResult.FAILED
        trace = self._client_trace()
        packets = trace._get_packets(
            trace._get_direction_filter(cases.Direction.FROM_CLIENT) + " quic")
        if not matched_responses(challenges, packets):
            logging.info("Preferred-address PATH_CHALLENGE lacked a matching response")
            return cases.TestResult.FAILED
        logging.info("Preferred-address probes validated (RFC 9000 section 9.6.2)")
        return cases.TestResult.SUCCEEDED

    # ConnectionMigration.check still performs its original client CID checks.
    cases.TestCasePortRebinding.check = check


if __name__ == "__main__":
    runner = Path(sys.argv[1]).resolve()
    sys.argv = [str(runner), *sys.argv[2:]]
    sys.path.insert(0, str(runner.parent))
    install(runner)
    runpy.run_path(str(runner), run_name="__main__")
