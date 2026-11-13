# Zquic

## Overview

QUIC v1 transport over UDP with TLS, streams, loss recovery, flow/congestion control, migration and optional qlog.

## Subdirectories & Boundaries

- [Library](./src/EGM.md): connection/stream APIs, codecs, protection, recovery, endpoints and logging.
- [Examples](./example/EGM.md): QUIC echo client/server with explicit scheduler setup.

## Interface

[The source manifest](./src/Makefile.am) builds libZquic and exports transport/runtime headers. Client and Server use application-provided CRTP links, configured multiplexer threads and typed allocators.

## Invariants

Keep Rx/Tx state on its owner, retain packet/plaintext backing for receive/recovery lifetimes, and drain endpoint work before finalization. UDP readiness, handshake establishment, queued send acceptance, local completion and peer acknowledgment are different states. See [the library contract](./src/EGM.md) and [module guidance](./GUIDELINES.md).

## Side Effects

The module opens UDP endpoints, loads TLS material, generates tokens/CIDs, schedules timers and callbacks, and registers telemetry. Optional TLS key logging writes traffic secrets; debug qlog writes rotated event files that can include reset tokens. See the library contract for configuration and lifecycle limits.

## Verification

From a configured repository root, run `make -j8 && make -C zquic test`. [The module target](./Makefile.am) runs unit and integration suites. Commands were not run for this documentation change.
