#!/usr/bin/env python3
# (c) Copyright 2026 Huw Rogers
# This code is licensed by the MIT license (see LICENSE for details)

"""Verify ZiRing reclaims a reader killed without detach."""

import os
from pathlib import Path
import select
import signal
import subprocess
import uuid
from zi_test_residue import Residue


def output(process):
    if not select.select([process.stdout], [], [], 5)[0]:
        raise AssertionError("ring probe timed out")
    line = process.stdout.readline().strip()
    if not line:
        raise AssertionError("ring probe exited without a response")
    return line


def main():
    name = "zumd-gc-" + uuid.uuid4().hex
    residue = Residue("zumd-ztc-gc")
    residue.shm(name)
    probe = Path(__file__).with_name("zumdztcprobe")
    env = dict(os.environ, ZTC_RING=name)
    writer = reader = None
    passed = False
    try:
        writer = subprocess.Popen([probe, name], env=env,
                                  stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE,
                                  stderr=subprocess.PIPE, text=True)
        assert output(writer) == "ready"
        reader = subprocess.Popen([probe, name, "stale-read"], env=env,
                                  stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE,
                                  stderr=subprocess.PIPE, text=True)
        assert output(reader) == "ready"
        writer.stdin.write("p\n")
        writer.stdin.flush()
        assert output(writer) == "pushed"
        reader.kill()
        reader.wait(timeout=5)
        assert reader.returncode == -signal.SIGKILL
        writer.stdin.write("g\n")
        writer.stdin.flush()
        assert output(writer) == "reclaimed"
        writer.stdin.write("q\n")
        writer.stdin.flush()
        writer.communicate(timeout=5)
        assert writer.returncode == 0
        print("ok 1 - dead reader PID is reclaimed on writer reopen")
        passed = True
    except Exception as error:
        print("not ok 1 - dead reader PID is reclaimed on writer reopen")
        print("# " + str(error))
        raise SystemExit(1)
    finally:
        for process in (reader, writer):
            if process is not None and process.poll() is None:
                process.kill()
                process.communicate()
        residue.finish(passed)


if __name__ == "__main__":
    print("1..1")
    main()
