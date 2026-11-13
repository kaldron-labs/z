# D-Bus fixture utilities

## Overview

Private bus launcher and external-libdbus reference peer for integration and interoperability checks.

## Interface

- `zdbusbus` takes no arguments, launches a private bus, and prints its address. Any completed one-byte stdin read, including a byte or EOF, starts shutdown. [zdbusbus.cc:main]
- `zdbuspeer ADDRESS service|caller|subscriber` uses external libdbus-1 directly. Service mode owns `org.example.ZdbusPeer` at `/org/example/ZdbusPeer` and handles Quit, Emit, Echo, Complex, OddReply, OtherError, and Fail. [zdbuspeer.cc:main] [zdbuspeer.cc:connect] [zdbuspeer.cc:serviceCall]
- Caller mode targets `org.example.ZdbusService`; subscriber mode prints READY after installing its match. Shared helpers read control lines, reap children, and search PATH. [zdbuspeer.cc:caller] [zdbuspeer.cc:subscriber] [zdbustesttool.hh:line] [zdbustesttool.hh:waitChild] [zdbustesttool.hh:available]

## Invariants

- Replies must match sent serials and use 5000-ms pending-call timeouts. Signal search limits message count to 64 with per-read timeouts, rather than one overall deadline. [zdbuspeer.cc:awaitReply] [zdbuspeer.cc:waitSignal]
- `line` requires nonempty newline-terminated input within 4096 bytes. `waitChild` uses pidfd readiness and SIGKILL fallback, followed by blocking reaping; it is not an absolute wall-clock bound. [zdbustesttool.hh:line] [zdbustesttool.hh:waitChild]
- `available` checks existence and non-directory status, not executability; empty PATH entries mean the current directory. [zdbustesttool.hh:available]
- Linux builds `zdbusbus`; `zdbuspeer` additionally requires HAVE_DBUS and links DBUS_LIBS rather than the inherited Z library list. [Makefile.am:LINUX] [Makefile.am:HAVE_DBUS] [Makefile.am:zdbuspeer_LDADD]

## Side Effects

- Forks/execs `dbus-daemon --session --nofork --print-address=1`, installs parent-death SIGTERM, then terminates and reaps the child. [zdbusbus.cc:main]
- Opens real private bus connections, registers clients, claims names, installs matches, and sends and flushes messages, errors, and signals. [zdbuspeer.cc:connect] [zdbuspeer.cc:service] [zdbuspeer.cc:send]
- Helpers change supplied descriptors to nonblocking mode, start scheduler/event-loop machinery, read PATH, and stat candidates. Readiness/address records are flushed stdout protocol messages. [zdbustesttool.hh:line] [zdbustesttool.hh:waitChild] [zdbustesttool.hh:available] [zdbuspeer.cc:ready]

## Verification

Build with `make -j8` from the configured Linux repository root; module validation uses `make -C zdbus test`, including integration and interop targets. External libdbus enables the reference peer. Utilities and tests were not executed for this documentation change.
