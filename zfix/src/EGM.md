# Zfix formatting and session transport

## Overview

Schema-driven FIX formatting, framing and dispatch, with managed session sequencing and raw TCP/TLS adapters. Applications supply business queues, persistence/history, admission policy, and correlation.

## Interface

- [ZfixFmt.hh](./ZfixFmt.hh): FIX field properties, schema checks, Sink, borrowed Indexed/Live decoders, and object formatting. [ZfixFrame.hh](./ZfixFrame.hh): FrameView, Framer, FrameWriter and message handlers.
- [Zfix.hh](./Zfix.hh), [ZfixDispatch.hh](./ZfixDispatch.hh), and [ZfixContext.hh](./ZfixContext.hh): message catalogs/selectors, checked-frame dispatch, retained RequestContext, and receive/reply hooks.
- [ZfixLink.hh](./ZfixLink.hh), [ZfixClient.hh](./ZfixClient.hh), and [ZfixServer.hh](./ZfixServer.hh): managed CRTP links and TCP/TLS client/server bindings. [ZfixRaw.hh](./ZfixRaw.hh) supplies framing/output with session policy delegated to an external manager.
- [ZfixSession.hh](./ZfixSession.hh), [ZfixSeq.hh](./ZfixSeq.hh), [ZfixRx.hh](./ZfixRx.hh), and [ZfixTx.hh](./ZfixTx.hh): administrative messages, sequence admission/replay, owner-specific receive and transmit state.
- [ZfixTxMsg.hh](./ZfixTxMsg.hh), [ZfixWire.hh](./ZfixWire.hh), [ZfixWork.hh](./ZfixWork.hh), and [ZfixTrace.hh](./ZfixTrace.hh): immutable bodies, output accounting, retained work, and bounded wire tracing. [Makefile.am](./Makefile.am) builds libZfixFmt and libZfix.

## Invariants

- Schema validation rejects conflicting/reserved tags, invalid binary/signature/group properties, and ambiguous selectors. Parse limits bound frame/field bytes, group entries/depth, and position storage. Binary payloads use declared length, not delimiter scanning. [ZfixFmt.hh](./ZfixFmt.hh), [ZfixFmt.cc](./ZfixFmt.cc)
- Views borrow input; direct decode and applying indexed views can partially mutate destination objects before reporting failure. Live validity initially checks frame size only. Dispatch requires a validated FrameView and calls the selected handler after decoding succeeds. RequestContext retains its receive node/backing, not the physical link. [ZfixFmt.hh](./ZfixFmt.hh), [ZfixDispatch.hh](./ZfixDispatch.hh), [ZfixContext.hh](./ZfixContext.hh)
- Framer storage may relocate only while preserving its prefix. Completed paused input must remain retained until admitted or abandoned. FrameWriter publishes the authoritative output span only after successful finish; growth may invalidate borrowed signing spans. [ZfixFrame.hh](./ZfixFrame.hh), [ZfixRxFramer.hh](./ZfixRxFramer.hh)
- Receive/session state and ingress/work credits belong to Rx; transmit sequencing, history association, frame construction and wire completion belong to Tx. Immutable bodies/fixed metadata cross owners; mutable queue nodes do not. Per-link and hub byte/count credits must drain before destruction. [ZfixLink.hh](./ZfixLink.hh), [ZfixNet.hh](./ZfixNet.hh), [ZfixWork.hh](./ZfixWork.hh)
- Connection and epoch checks fence stale work. Binding revokes prior protocol authority before TLS authentication; FIX starts after transport authentication. Deferred admission/stop requests must explicitly retain their completion callbacks; borrowed hook request objects cannot escape. [ZfixLink.hh](./ZfixLink.hh), [ZfixRaw.hh](./ZfixRaw.hh)
- Sequence cursors are exclusive; replay requests use inclusive ends. Loaded, published, acknowledged, and socket-completed states differ. Published bodies retire from operational storage; replay retrieves application history. Restore/reset require quiescent owner state and matching epoch/reset agreement. [ZfixSeq.hh](./ZfixSeq.hh), [ZfixTx.hh](./ZfixTx.hh), [ZfixLinkTx.hh](./ZfixLinkTx.hh)
- Rx holds one first Logon during admission and processes business bodies in sequence order. Low duplicates, gaps, resets and early administrative handling follow distinct paths; stale epochs do not reach business processing. [ZfixRx.hh](./ZfixRx.hh)
- Wire admission stages a complete frame before publication/flush and requires exactly one ordered completion per admission. Local handoff/write completion does not mean peer acknowledgment. Keep hubs running while stopped callbacks wait for output, posted work, history and deferred hooks to finish or cancel. [ZfixWire.hh](./ZfixWire.hh), [ZfixLink.hh](./ZfixLink.hh)

- Framing requires ordered BeginString/BodyLength/MsgType, exact checksum trailer, and a matching modulo-256 checksum. A successful parse consumes only the first complete frame. Managed readiness requires both Rx and Tx recovery; probes acknowledge progress only after matching published challenge identity. [ZfixFrame.cc](./ZfixFrame.cc), [ZfixLinkState.cc](./ZfixLinkState.cc)

## Side Effects

- Formatting writes caller storage and may grow temporary/index storage; C-string conversion allocates owned copies. Failures can leave partial destination bytes or object changes. [ZfixFmt.hh](./ZfixFmt.hh), [ZfixFmt.cc](./ZfixFmt.cc)
- Managed adapters open TCP/TLS connections/listeners, schedule Rx/Tx/timer work, retain buffers and invoke application admission/history/reset callbacks. Raw mode supplies output/framing while its external manager owns session behavior. [ZfixClient.hh](./ZfixClient.hh), [ZfixServer.hh](./ZfixServer.hh), [ZfixRaw.hh](./ZfixRaw.hh)

- WireTrace creates bounded escaped records, redacts sensitive fields, fails closed on unknown/ambiguous interpretation, and rotates old records to stay within its retained-memory budget. Records own text rather than original wire backing. [ZfixTrace.cc](./ZfixTrace.cc)

## Verification

From a configured repository root, run `make -j8 && make -C zfix test`. The module runs unit and integration targets; unit checks include schema rejection and signing tests. [Unit manifest](../test/Makefile.am), [module target](../Makefile.am). Commands were not run for this documentation change.
