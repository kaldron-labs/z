#!/usr/bin/env python3
# (c) Copyright 2026 Huw Rogers
# This code is licensed by the MIT license (see LICENSE for details)

"""Run a real GTK dashboard through publisher, agent and hub lifecycles."""

import os
from pathlib import Path
import shutil
import sys

root = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(root / path) for path in
               ("zi/itest", "zum/itest", "ztc/itest")]

from zi_test_residue import Residue
from ztchubtest import main


if __name__ == "__main__":
    if not os.environ.get("ZDB_MODULE") or not shutil.which("xvfb-run"):
        print("1..0 # SKIP ZDB_MODULE and xvfb-run are required")
        sys.exit(0)
    with Residue("zdash-live") as residue:
        os.environ["ZDB_CONNECT"] = str(residue.directory / "iam.db")
        os.environ["ZDASH_LIVE_TEST"] = "1"
        for name in ("ZTC_LOAD", "ZTC_CLUSTER", "ZTC_AGENT_ONLY"):
            os.environ.pop(name, None)
        main()
        residue.success()
    print("1..1\nok 1 - live GTK telemetry, late discovery and generation replacement")
