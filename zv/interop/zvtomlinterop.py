#!/usr/bin/env python3

import sys

try:
    import tomli
except ImportError:
    print("TAP version 14")
    print("1..0 # SKIP Python tomli unavailable")
    raise SystemExit(0)

# Verify that the independent parser actually admits the v1.1 features used to
# distinguish this gate from a TOML 1.0-only parser.
v11 = tomli.loads('value = {\n text = "\\e\\x41",\n time = 12:34,\n}\n')
assert v11["value"]["text"] == "\x1bA"
assert v11["value"]["time"].isoformat() == "12:34:00"

doc = tomli.loads(sys.stdin.read())
assert doc["title"] == "tools"
assert doc["enabled"] is True
assert doc["values"] == ["one", "two"]
assert doc["nested"] == {"value": 7}
assert doc["when"].isoformat() == "2024-02-29T12:34:56+00:00"
assert doc["inlineMap"] == {"a.b": 11}
assert doc["tableMap"] == {"beta": 12}
assert doc["products"] == [
    {"name": "hammer", "count": 1},
    {"name": "nail", "count": 20},
]
print("TAP version 14")
print("1..1")
print("ok 1 - canonical TOML accepted by Python tomli v1.1 parser")
