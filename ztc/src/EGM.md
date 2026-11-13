# Ztc telemetry publisher and distribution

## Overview

Application telemetry capture and alert persistence, framed FlatBuffers messages over local rings, and OAuth-authenticated agent/hub distribution.

## Interface

- [ZtcApp.hh](./ZtcApp.hh): App lifecycle, configuration, asynchronous RAG access, and alertSink. libZtcApp contains the publisher implementation.
- [ZtcMsg.hh](./ZtcMsg.hh), [ZtcFB.hh](./ZtcFB.hh), and [ZtcAppTypes.hh](./ZtcAppTypes.hh): framed Msg/Payload envelopes, Request/Ack/Telemetry/EOS/Error bodies, telemetry metadata, and group/status types. libZtc contains metadata.
- [ZtcRing.hh](./ZtcRing.hh): multiwriter framed rings. [ZtcFilter.hh](./ZtcFilter.hh) supplies canonical group-specific filters.
- [ztcagent_daemon.hh](./ztcagent_daemon.hh): Agent connects local publishers to an authenticated WSS hub. [ztchub_daemon.hh](./ztchub_daemon.hh): Hubd lifecycle/readiness, authentication/session, subscription/inventory and packet routing interfaces.
- [ztcagent.cc](./ztcagent.cc) and [ztchub.cc](./ztchub.cc) implement installed daemons; [Makefile.am](./Makefile.am) installs principal app/wire/ring headers and keeps filter/alert/agent configuration headers private.

## Invariants

- App permits one initialized instance per process. Blocking lifecycle belongs on the main thread; worker/timer/request contexts are isolated. Stop closes admission before draining request work, credits, subscriptions and alerts; scheduler join precedes ring/file cleanup. Final requires fully stopped/drained state. [ZtcApp.cc](./ZtcApp.cc)
- Wire framing uses a four-byte little-endian body length with bounds on total framed size. Checked ingress validates outer and nested payload structure and body semantics; unchecked message views require verified or locally generated data. [ZtcMsg.hh](./ZtcMsg.hh)
- Filters retain canonical encoded keys: only terminal-star prefixes, restricted uppercase percent escapes, and group-specific component shapes are accepted. Subscription cancellation matches sequence/group/canonical key. [ZtcFilter.hh](./ZtcFilter.hh), [ZtcApp.cc](./ZtcApp.cc)
- Snapshots own captured values; raw manager objects are captured under manager guards. Each subscription has one active snapshot; dirty state triggers recapture. Replay freezes a high-water mark and sends EOS before live handoff. Stored alert frames retain canonical outer sequence zero; subscriber copies carry delivery sequence. [ZtcApp.cc](./ZtcApp.cc)
- Agent discovery must match the configured issuer and same-origin token endpoint shape. Issuers require HTTPS except explicit loopback testing; WSS remains mandatory. Startup does not imply OAuth or WSS readiness. Connection generations fence requests and replies; telemetry credits bound pending routing work, not downstream transmission. [ztcagent_daemon.cc](./ztcagent_daemon.cc)
- Hub Running and readiness Up are distinct: standby remains Down. DB role transitions activate/deactivate service and retire old routing. Sessions require issuer/audience/action checks; agents use client_credentials and subject=device ID, while request principals use passkey or oidc. Routes match agent session and generation as well as sequence. [ztchub_daemon.cc](./ztchub_daemon.cc)
- Hub packet builders run synchronously on the producer shard; segmented packets retain separate head/payload owners. Routing aliases retire before owning routes. Stop waits for counted callbacks and crosses Rx→Tx→Rx before finalization. [ztchub_daemon.hh](./ztchub_daemon.hh), [ztchub_daemon.cc](./ztchub_daemon.cc)

- Hub WSS ingress accepts binary framed messages and preserves input order through bounded authentication/backlog queues. Egress maintains per-subscription order and round-robin service; control overflow closes the link, while telemetry overflow retires the affected route and emits Overflow. [ztchub.cc](./ztchub.cc)

## Side Effects

- App opens shared rings and publishes a PID file using ZTC_RING/ZTC_DIR naming. Ring publication can evict obstructing readers. Snapshot EOS does not guarantee delivery of every telemetry frame. [ZtcApp.cc](./ZtcApp.cc)
- Alert persistence appends dated data/index files, truncates recoverable incomplete/orphan tails, and removes older matching partitions. Failures mark telemetry degraded. [ZtcAlert.hh](./ZtcAlert.hh), [ZtcApp.cc](./ZtcApp.cc)
- Agent scans publisher PID files, opens rings, sends OAuth HTTP requests, connects WSS, retries, and logs. Provisioning persists its client secret through Vault; file/ephemeral agent vault stores require explicit test configuration and ZTCAGENT_HOME. [ztcagent.cc](./ztcagent.cc), [ztcagent_daemon.cc](./ztcagent_daemon.cc)
- Hub initializes its DB/service, publishes a service manifest, creates random one-use browser handoff cookies, schedules expiry, and routes inventory/subscriptions. ZTCHUB_HOSTS and ZTCHUB_HOSTID are paired configuration inputs. [ztchub_daemon.cc](./ztchub_daemon.cc)

- The hub executable opens TLS WebSocket and REST listeners, serves SET callbacks and browser session handoff, and may persist provisioned service credentials. Its default configuration is ztchub.conf; paired ZUM_CLIENT_SECRET/ZUM_SSF_AUTH can provision credentials. [ztchub.cc](./ztchub.cc)

## Verification

From a configured repository root, run `make -j8 && make -C ztc test`. This runs unit and integration targets; additional database-backed integration scripts require ZDB_MODULE. [Unit manifest](../test/Makefile.am), [integration manifest](../itest/Makefile.am). Commands were not run for this documentation change.
