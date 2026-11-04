#!/usr/bin/env python3
"""Capture local interface metadata without changing network or IRQ settings."""
import argparse
import datetime
import json
import os
from pathlib import Path
import re
import subprocess


SYS_NET = Path("/sys/class/net")
PROC_IRQ = Path("/proc/irq")


def read(path):
    result = {"path": str(path)}
    try:
        result["text"] = path.read_text().strip()
    except OSError as error:
        result["error"] = str(error)
    return result


def entries(path):
    result = {"path": str(path)}
    try:
        result["names"] = sorted(item.name for item in path.iterdir())
    except OSError as error:
        result["error"] = str(error)
    return result


def command(args):
    result = {"command": args}
    env = dict(os.environ, LC_ALL="C")
    try:
        done = subprocess.run(args, capture_output=True, text=True,
                              errors="replace", timeout=5, env=env)
        result.update(exit=done.returncode, stdout=done.stdout, stderr=done.stderr)
        if done.returncode:
            result["error"] = "command exited with status " + str(done.returncode)
    except subprocess.TimeoutExpired as error:
        def text(value):
            return value.decode(errors="replace") if isinstance(value, bytes) else value or ""
        result.update(exit=None, error="command timed out after 5 seconds",
                      stdout=text(error.stdout), stderr=text(error.stderr))
    except OSError as error:
        result.update(exit=None, error=str(error), stdout="", stderr="")
    return result


def capture(iface):
    """Return JSON-serializable evidence for exactly one named local interface.

    Invalid/path-like names raise ValueError. Missing files, permissions, tools,
    unsupported ioctls and command failures remain explicit in the result.
    """
    if not isinstance(iface, str) or not re.fullmatch(r"[A-Za-z0-9_][A-Za-z0-9_.:-]{0,14}", iface):
        raise ValueError("interface must be a local name of 1-15 ASCII letters/digits or _ . : -; "
                         "its first character must be a letter, digit or underscore")
    path = SYS_NET / iface
    result = {
        "interface": iface,
        "capturedAt": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "scope": "read-only local metadata; no remote peer or path verification",
        "timestampLimit": "timestamp capabilities do not establish packet timestamps or "
                          "NIC-arrival-to-framer latency",
        "activeTimestamping": {"status": "not inspected",
                               "note": "no socket SO_TIMESTAMPING state or NIC-arrival "
                                       "timestamp source is captured by this helper"},
    }
    try:
        target = path.resolve(strict=True)
    except OSError as error:
        result["interfaceError"] = str(error)
        return result
    result["sysfsPath"] = str(target)
    result["topology"] = "virtual" if target.is_relative_to(Path("/sys/devices/virtual/net")) else "device-backed"
    result["topologyLimit"] = "device-backed sysfs topology alone does not prove a real-Ethernet traffic path"
    result["link"] = {name: read(path / name) for name in (
        "mtu", "operstate", "carrier", "address", "type", "ifindex", "iflink",
        "speed", "duplex", "flags", "phys_port_name")}
    device = path / "device"
    try:
        result["devicePath"] = str(device.resolve(strict=True))
    except OSError as error:
        result["deviceError"] = str(error)
    try:
        result["driverPath"] = str((device / "driver").resolve(strict=True))
    except OSError as error:
        result["driverError"] = str(error)
    result["numaNode"] = read(device / "numa_node")
    result["commands"] = {
        "link": command(["ip", "-details", "-statistics", "-json", "link", "show", "dev", iface]),
        "settings": command(["ethtool", iface]),
        "offloads": command(["ethtool", "-k", iface]),
        "driver": command(["ethtool", "-i", iface]),
        "timestampCapabilities": command(["ethtool", "-T", iface]),
    }

    # Device IRQ membership is primary. Interface-labelled /proc lines also
    # cover queue IRQs where sysfs has no MSI directory or a shared parent.
    msi = entries(device / "msi_irqs")
    legacy = read(device / "irq")
    selected = {int(name) for name in msi.get("names", []) if name.isdecimal()}
    if legacy.get("text", "").isdecimal() and int(legacy["text"]) > 0:
        selected.add(int(legacy["text"]))
    interrupts = read(Path("/proc/interrupts"))
    lines = interrupts.pop("text", "").splitlines()
    interrupts["empty"] = not lines
    interrupts["header"] = lines[0] if lines else ""
    interrupts["lines"] = []
    label = re.compile(r"(?:^|[\s,:])" + re.escape(iface) + r"(?:$|[\s,:-])")
    for line in lines[1:]:
        irq, separator, rest = line.partition(":")
        if not separator or not irq.strip().isdecimal():
            continue
        number = int(irq.strip())
        if number in selected or label.search(rest):
            selected.add(number)
            interrupts["lines"].append(line)
    result["irqs"] = {
        "msi": msi, "legacy": legacy, "interrupts": interrupts,
        "selection": "device MSI/legacy IRQs and exact interface-labelled interrupt lines; "
                     "missing or empty evidence does not establish absence of other IRQs",
        "affinity": {str(irq): {name: read(PROC_IRQ / str(irq) / name) for name in (
            "smp_affinity", "smp_affinity_list", "effective_affinity",
            "effective_affinity_list", "node")} for irq in sorted(selected)},
    }
    queues = entries(path / "queues")
    queues["affinity"] = {}
    for name in queues.get("names", []):
        if re.fullmatch(r"rx-[0-9]+", name):
            fields = ("rps_cpus", "rps_flow_cnt")
        elif re.fullmatch(r"tx-[0-9]+", name):
            fields = ("xps_cpus", "xps_rxqs")
        else:
            continue
        queues["affinity"][name] = {field: read(path / "queues" / name / field)
                                    for field in fields}
    result["queues"] = queues
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("interface", help="explicit local interface name; no settings are changed")
    args = parser.parse_args()
    try:
        result = capture(args.interface)
    except ValueError as error:
        parser.error(str(error))
    print(json.dumps(result, indent=2, sort_keys=True))
    return int("interfaceError" in result)


if __name__ == "__main__":
    raise SystemExit(main())
