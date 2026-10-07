#!/usr/bin/env python3
# (c) Copyright 2026 Huw Rogers
# This code is licensed by the MIT license (see LICENSE for details)

"""Process-level opt-in telemetry publisher tests."""

import http.client
import os
from pathlib import Path
import base64
import secrets
import selectors
import signal
import subprocess
import time

from zumhttp import Fixture
from zi_test_residue import Residue


HERE = Path(__file__).resolve().parent
PROBE = HERE / "zumdztcprobe"
SERVER = HERE.parent / "src" / "zumd"


def wait_line(process, wanted, timeout=30):
    pending = b""
    deadline = time.monotonic() + timeout
    with selectors.DefaultSelector() as selector:
        selector.register(process.stdout, selectors.EVENT_READ)
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0 or not selector.select(remaining):
                raise AssertionError("timed out waiting for " + repr(wanted))
            data = os.read(process.stdout.fileno(), 4096)
            if not data:
                raise AssertionError("process exited before " + repr(wanted))
            pending += data
            lines = pending.split(b"\n")
            pending = lines.pop()
            if wanted in lines:
                return


def launch(fixture, config, flags):
    fixture.log = (fixture.directory / "server.log").open("ab")
    fixture.process = subprocess.Popen([
        str(SERVER), "--config=" + str(config),
        "--issuer=" + fixture.origin, "--admin=http-admin",
        "--vault-store=file", "--vault-test-store",
        "--bootstrap-output=" + str(fixture.directory / "enrollment"),
        "--port=" + str(fixture.port), "--rp-id=localhost", *flags],
        env=fixture.env, stdout=subprocess.PIPE, stderr=fixture.log)
    fixture.starts += 1
    pending = b""
    seen = set()
    deadline = time.monotonic() + 30
    with selectors.DefaultSelector() as selector:
        selector.register(fixture.process.stdout, selectors.EVENT_READ)
        while not {b"zumd: listening", b"zumd: active"} <= seen:
            remaining = deadline - time.monotonic()
            if remaining <= 0 or not selector.select(remaining):
                raise AssertionError("zumd startup timed out")
            data = os.read(fixture.process.stdout.fileno(), 4096)
            if not data:
                raise AssertionError("zumd exited during startup")
            pending += data
            lines = pending.split(b"\n")
            pending = lines.pop()
            seen.update(lines)


def probe(name, env, group):
    result = subprocess.run([str(PROBE), name, group], env=env,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            timeout=30)
    assert result.returncode == 0, (group + " telemetry failed: " +
                                    result.stderr.decode(errors="replace"))


def case(label, *, node=False, env_value=None, cli=False, enabled=False,
         collect=False, collect_late=False, malformed_ztc=False):
    residue = Residue("zumdztctest")
    directory = residue.directory
    registry = "zumd-ztc-" + secrets.token_hex(8)
    ring = registry + "-ring"
    publisher_id = registry + "-pub"
    regdir = residue.tmp_dir("registry")
    registry = regdir.name
    residue.shm(ring)
    residue.shm(publisher_id)
    fixture = Fixture(directory)
    fixture.env.update(ZDB_MODULE=os.environ["ZDB_MODULE"],
                       ZDB_CONNECT=str(directory / "zumd.db"),
                       ZTC_DIR=registry, ZTC_RING=ring)
    fixture.env.pop("ZUMD_ZTC_PUBLISH", None)
    if env_value is not None:
        fixture.env["ZUMD_ZTC_PUBLISH"] = env_value
    config = directory / "node.cf"
    tuning = ('ztc: {id: "' + publisher_id +
              '", reqSize: "invalid"}\n' if malformed_ztc else
              'ztc: {id: "' + publisher_id + '", alertPrefix: "' +
              str(directory / "alerts") + '"}\n')
    config.write_text((HERE / "zumd.cf").read_text().rstrip() +
                      (",\nztcPublish: true,\n" if node else ",\n") +
                      tuning)
    setup = None
    passed = False
    try:
        if enabled and not collect_late:
            setup = subprocess.Popen([str(PROBE), publisher_id],
                                     env=fixture.env, stdin=subprocess.PIPE,
                                     stdout=subprocess.PIPE,
                                     stderr=subprocess.PIPE)
            wait_line(setup, b"ready")
        launch(fixture, config, ["--ztcPublish"] if cli else [])
        registered = regdir / (publisher_id + ".pid")
        threads = {path.read_text().strip() for path in
                   Path("/proc", str(fixture.process.pid), "task").glob("*/comm")}
        if enabled:
            assert registered.exists(), "publisher registration missing"
            assert "ztcReq" in threads, "publisher request worker missing"
            if collect_late:
                setup = subprocess.Popen([str(PROBE), publisher_id],
                                         env=fixture.env, stdin=subprocess.PIPE,
                                         stdout=subprocess.PIPE,
                                         stderr=subprocess.PIPE)
                wait_line(setup, b"ready")
                probe(publisher_id, fixture.env, "app")
            if collect:
                connection = http.client.HTTPConnection("127.0.0.1", fixture.port,
                                                        timeout=10)
                connection.request("GET", "/health/ready")
                response = connection.getresponse()
                assert response.status == 503, "enrollment unexpectedly complete"
                response.read()
                connection.close()
                for group in ("app", "mx", "db"):
                    probe(publisher_id, fixture.env, group)
                probe(publisher_id, fixture.env, "db-active")
                hold = subprocess.Popen([str(PROBE), publisher_id,
                                         "app", "hold"], env=fixture.env,
                                        stdout=subprocess.PIPE,
                                        stderr=subprocess.PIPE)
                try:
                    wait_line(hold, b"ready")
                    fixture.stop()
                    hold.communicate(timeout=30)
                    assert hold.returncode == 0, "shutdown frame missing"
                finally:
                    if hold.poll() is None:
                        hold.kill()
                        hold.communicate()
        else:
            assert not registered.exists(), "disabled publisher registered"
            assert "ztcReq" not in threads, "disabled publisher started worker"
            assert not Path("/dev/shm", publisher_id + ".ctrl").exists(), \
                "disabled publisher created request ring"
        fixture.stop()
        assert not registered.exists(), "publisher registration retained"
        passed = True
    finally:
        if fixture.process is not None:
            try:
                fixture.stop()
            except AssertionError:
                pass
        if setup is not None:
            setup.stdin.write(b"q\n")
            setup.stdin.flush()
            setup.communicate(timeout=5)
        residue.finish(passed)


def startup_failure(label, publisher_failure):
    residue = Residue("zumdztcstartup")
    directory = residue.directory
    registry = "zumd-ztc-" + secrets.token_hex(8)
    ring = registry + "-ring"
    publisher_id = registry + "-pub"
    regdir = (residue.tmp_file("registry") if publisher_failure
              else residue.tmp_dir("registry"))
    registry = regdir.name
    residue.shm(ring)
    residue.shm(publisher_id)
    fixture = Fixture(directory)
    fixture.env.update(ZDB_MODULE=os.environ["ZDB_MODULE"],
                       ZDB_CONNECT=str(directory / "zumd.db"),
                       ZTC_DIR=registry, ZTC_RING=ring)
    if not publisher_failure:
        fixture.env["ZUM_DB_KEY"] = "invalid-key"
    config = directory / "node.cf"
    config.write_text((HERE / "zumd.cf").read_text().rstrip() +
                      ',\nztcPublish: true,\nztc: {id: "' + publisher_id +
                      '", alertPrefix: "' + str(directory / "alerts") + '"}\n')
    setup = None
    passed = False
    try:
        setup = subprocess.Popen([str(PROBE), publisher_id], env=fixture.env,
                                 stdin=subprocess.PIPE,
                                 stdout=subprocess.PIPE,
                                 stderr=subprocess.PIPE)
        wait_line(setup, b"ready")
        with (directory / "server.log").open("wb") as log:
            process = subprocess.run([
                str(SERVER), "--config=" + str(config),
                "--issuer=" + fixture.origin, "--admin=http-admin",
                "--vault-store=file", "--vault-test-store",
                "--bootstrap-output=" + str(directory / "enrollment"),
                "--port=" + str(fixture.port), "--rp-id=localhost"],
                env=fixture.env, stdout=subprocess.PIPE, stderr=log,
                timeout=30)
        assert process.returncode != 0, "startup unexpectedly succeeded"
        error = (directory / "server.log").read_text()
        expected = ("telemetry publisher startup failed" if publisher_failure
                    else "ZUM_DB_KEY must be a base64-encoded 256-bit key")
        assert expected in error, "wrong startup failure: " + error[-500:]
        if not publisher_failure:
            assert not (regdir / (publisher_id + ".pid")).exists(), \
                "publisher registration retained after startup error"
        passed = True
    finally:
        if setup is not None:
            setup.stdin.write(b"q\n")
            setup.stdin.flush()
            setup.communicate(timeout=5)
        residue.finish(passed)


def once_case(malformed=False):
    residue = Residue("zumdztconce")
    directory = residue.directory
    registry = "zumd-ztc-" + secrets.token_hex(8)
    ring = registry + "-ring"
    publisher_id = registry + "-pub"
    regdir = residue.tmp_dir("registry")
    registry = regdir.name
    residue.shm(ring)
    residue.shm(publisher_id)
    fixture = Fixture(directory)
    fixture.env.update(ZDB_MODULE=os.environ["ZDB_MODULE"],
                       ZDB_CONNECT=str(directory / "zumd.db"),
                       ZTC_DIR=registry, ZTC_RING=ring)
    config = directory / "node.cf"
    config.write_text((HERE / "zumd.cf").read_text().rstrip() +
                      (',\nztcPublish: "invalid",\n' if malformed else
                       ',\nztcPublish: true,\n') +
                      'ztc: {id: "' + publisher_id + '", alertPrefix: "' +
                      str(directory / "alerts") + '"}\n')
    setup = None
    passed = False
    try:
        if not malformed:
            setup = subprocess.Popen([str(PROBE), publisher_id], env=fixture.env,
                                     stdin=subprocess.PIPE,
                                     stdout=subprocess.PIPE,
                                     stderr=subprocess.PIPE)
            wait_line(setup, b"ready")
        with (directory / "server.log").open("wb") as log:
            process = subprocess.run([
                str(SERVER), "--config=" + str(config),
                "--issuer=" + fixture.origin, "--admin=http-admin",
                "--vault-store=file", "--vault-test-store",
                "--bootstrap-output=" + str(directory / "enrollment"),
                "--port=" + str(fixture.port), "--rp-id=localhost", "--once"],
                env=fixture.env, stdout=subprocess.PIPE, stderr=log,
                timeout=30)
        if malformed:
            assert process.returncode != 0, "malformed boolean was accepted"
            assert b"zumd: active" not in process.stdout
        else:
            assert process.returncode == 0, "enabled --once failed"
            assert b"zumd: active" in process.stdout
        assert not (regdir / (publisher_id + ".pid")).exists(), \
            "publisher registration retained after --once"
        passed = True
    finally:
        if setup is not None:
            setup.stdin.write(b"q\n")
            setup.stdin.flush()
            setup.communicate(timeout=5)
        residue.finish(passed)


def rekey_case():
    residue = Residue("zumdztcrekey")
    directory = residue.directory
    registry = "zumd-ztc-" + secrets.token_hex(8)
    ring = registry + "-ring"
    publisher_id = registry + "-pub"
    regdir = residue.tmp_dir("registry")
    registry = regdir.name
    residue.shm(ring)
    residue.shm(publisher_id)
    fixture = Fixture(directory)
    fixture.env.update(ZDB_MODULE=os.environ["ZDB_MODULE"],
                       ZDB_CONNECT=str(directory / "zumd.db"),
                       ZTC_DIR=registry, ZTC_RING=ring)
    config = directory / "node.cf"
    config.write_text((HERE / "zumd.cf").read_text().rstrip() +
                      ',\nztcPublish: true,\nztc: {id: "' + publisher_id +
                      '", alertPrefix: "' + str(directory / "alerts") + '"}\n')
    fixture.node_config = config
    setup = None
    passed = False
    try:
        setup = subprocess.Popen([str(PROBE), publisher_id], env=fixture.env,
                                 stdin=subprocess.PIPE,
                                 stdout=subprocess.PIPE,
                                 stderr=subprocess.PIPE)
        wait_line(setup, b"ready")
        fixture.start()
        fixture.stop()
        new_key = base64.b64encode(secrets.token_bytes(32)).decode()
        with (directory / "rekey.log").open("wb") as log:
            process = subprocess.run([
                str(SERVER), "--config=" + str(config), "--rekey",
                "--issuer=" + fixture.origin, "--vault-store=file",
                "--vault-test-store"],
                env=dict(fixture.env, ZUM_DB_KEY=new_key),
                stdout=subprocess.PIPE, stderr=log, timeout=30)
        assert process.returncode == 0, "enabled rekey failed"
        assert b"secret-key rotation complete" in process.stdout
        assert not (regdir / (publisher_id + ".pid")).exists(), \
            "publisher registration retained after rekey"
        fixture.env["ZUM_DB_KEY"] = new_key
        fixture.start()
        connection = http.client.HTTPConnection("127.0.0.1", fixture.port,
                                                timeout=10)
        connection.request("GET", "/health/live")
        response = connection.getresponse()
        assert response.status == 200, "rotated database failed to start"
        response.read()
        connection.close()
        fixture.stop()
        passed = True
    finally:
        if fixture.process is not None:
            try:
                fixture.stop()
            except AssertionError:
                pass
        if setup is not None:
            setup.stdin.write(b"q\n")
            setup.stdin.flush()
            setup.communicate(timeout=5)
        residue.finish(passed)


def cli_reject_case(flag):
    residue = Residue("zumdztccli")
    directory = residue.directory
    ring = residue.shm("zumd-ztc-" + secrets.token_hex(8))
    fixture = Fixture(directory)
    fixture.env.update(ZDB_MODULE=os.environ["ZDB_MODULE"],
                       ZDB_CONNECT=str(directory / "zumd.db"),
                       ZTC_RING=ring)
    fixture.env.pop("ZUMD_ZTC_PUBLISH", None)
    passed = False
    try:
        with (directory / "server.log").open("wb") as log:
            process = subprocess.run([
                str(SERVER), "--config=" + str(HERE / "zumd.cf"),
                "--issuer=" + fixture.origin, "--admin=http-admin",
                "--vault-store=file", "--vault-test-store",
                "--bootstrap-output=" + str(directory / "enrollment"),
                "--port=" + str(fixture.port), "--rp-id=localhost",
                "--once", flag], env=fixture.env,
                stdout=subprocess.PIPE, stderr=log, timeout=30)
        assert process.returncode != 0, "invalid CLI flag was accepted"
        assert b"zumd: active" not in process.stdout
        assert not Path("/dev/shm", ring + ".ctrl").exists(), \
            "invalid invocation initialized telemetry"
        passed = True
    finally:
        residue.finish(passed)


def main():
    cases = [
        ("default disabled", dict()),
        ("disabled ignores publisher tuning", dict(malformed_ztc=True)),
        ("node enabled", dict(node=True, enabled=True, collect=True)),
        ("exact environment enabled", dict(env_value="1", enabled=True)),
        ("CLI enabled", dict(cli=True, enabled=True)),
        ("CLI overrides environment zero", dict(cli=True, env_value="0",
                                                enabled=True)),
        ("collector attaches after daemon", dict(node=True, enabled=True,
                                                 collect_late=True)),
        ("environment zero disabled", dict(env_value="0")),
        ("environment true disabled", dict(env_value="true")),
        ("environment 01 disabled", dict(env_value="01")),
        ("empty environment disabled", dict(env_value="")),
    ]
    cases.extend([
        ("publisher startup failure cleans up", {"failure": True}),
        ("IAM startup failure cleans up publisher", {"failure": False}),
        ("enabled once completes and cleans up", {"once": True}),
        ("malformed node boolean fails", {"once": False}),
        ("enabled rekey drains and restarts", {"rekey": True}),
        ("CLI flag rejects a value", {"cli_error": "--ztcPublish=1"}),
        ("CLI flag is case-sensitive", {"cli_error": "--ztcpublish"}),
    ])
    print("1.." + str(len(cases)), flush=True)
    for index, (label, options) in enumerate(cases, 1):
        try:
            if "failure" in options:
                startup_failure(label, options["failure"])
            elif "once" in options:
                once_case(not options["once"])
            elif "rekey" in options:
                rekey_case()
            elif "cli_error" in options:
                cli_reject_case(options["cli_error"])
            else:
                case(label, **options)
            print("ok " + str(index) + " - " + label, flush=True)
        except Exception as error:
            print("not ok " + str(index) + " - " + label, flush=True)
            print("# " + str(error), flush=True)
            raise


if __name__ == "__main__":
    main()
