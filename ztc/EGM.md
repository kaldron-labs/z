# Ztc

## Overview

Telemetry and control services connecting application publishers over local shared rings to authenticated WebSocket subscribers through an agent and hub.

## Subdirectories & Boundaries

- [Library and daemons](./src/EGM.md): application capture, alert persistence, wire messages, agent and hub.
- [Client example](./example/EGM.md): OAuth login and telemetry subscription.

## Interface

The module builds libZtc, libZtcApp, ztcagent, and ztchub. [ztcagent.conf](./ztcagent.conf) selects keyring storage and queue/time defaults; agent identity and OAuth inputs come from ZTC_* environment variables. See [the source build manifest](./src/Makefile.am).

## Invariants

Application and daemon lifecycle requires stop before finalization. Publisher admission and queued work drain before scheduler/ring destruction. Hub readiness follows its DB role independently of engine Running state; session/generation checks preserve route identity. See [App](./src/ZtcApp.cc), [Agent](./src/ztcagent_daemon.cc), and [Hubd](./src/ztchub_daemon.cc).

## Side Effects

Publishing opens shared rings/PID files and can evict obstructing ring readers. Optional alert storage appends, recovers, and expires dated files. Daemons use OAuth, TLS WebSocket/REST listeners, credentials, timers, and logs; the example can launch a browser and persist refresh tokens. See the child contracts.

## Verification

From a configured repository root, run `make -j8 && make -C ztc test`. [The module target](./Makefile.am) runs unit and integration tests; database-backed integration scripts require ZDB_MODULE. Commands were not run for this documentation change.
