# D-Bus session-ID example

## Overview

Typed `GetId` request/reply demonstration using `ZdbusClient` and the configured session bus.

## Interface

- Non-installed Linux executable `zdbusgetid` takes no arguments and reads `DBUS_SESSION_BUS_ADDRESS`. [Makefile.am:noinst_PROGRAMS] [zdbusgetid.cc:main]
- `GetIdHeaders` targets `/org/freedesktop/DBus` and `org.freedesktop.DBus`, with an empty request and string reply. [zdbusgetid.cc:GetIdHeaders] [zdbusgetid.cc:GetIdReq]
- Exit status is two for unavailable/unsupported address, one for call/output failure, and zero after printing the ID. [zdbusgetid.cc:main]

## Invariants

- Readiness and call-completion waits have five-second deadlines; only a parsed `GetIdRes` sets the returned flag. [zdbusgetid.cc:waitFor] [zdbusgetid.cc:main]
- Client stop completion precedes finalization and scheduler stop; the stop wait has no timeout. [zdbusgetid.cc:main]

## Side Effects

- Starts two scheduler threads, connects to the actual configured session bus, sends a request, and prints and flushes the ID to stdout. Failures print to stderr. [zdbusgetid.cc:main]

## Verification

On a configured Linux build, run `make -j8 && make -C zdbus/test test` from the repository root. This example has no local test target; execution requires a session bus and performs the effects above. It was not executed.
