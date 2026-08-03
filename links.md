# H2/H3 Link Alignment Plan

## Objective

Align the native H2 and H3 transports with the Z I/O object model:

```text
Engine
  `-- CliLink/SrvLink
       |-- 0..1 Cxn or UDP endpoint association
       `-- 0..N Streams
```

The native H2 and H3 types currently named `ClientSession` and
`ServerSession` are already Links in ownership, lifetime, inheritance, and
engine contracts. Rename them accordingly.

H2 stream state cannot span multiple Links. Fold `H2::Session` directly into
the common H2 Link implementation rather than retaining it as an independent
`Session` or `StreamSet` object.

This is an intentional breaking change. Propagate every renamed type, method,
member, file, include, and test target to all dependents in the repository.
Do not leave any dependent on a legacy name, and do not add compatibility
aliases, forwarding headers, wrapper methods, deprecated entry points, or any
other shim for legacy compatibility.

## Scope

In scope:

- native H2 Links in `H2_`;
- mixed-ALPN H1/H2 native TLS Links in `TLS_`;
- native H3 Links in `H3_`;
- H2 connection-scoped stream state currently in `H2::Session`;
- native-Link aliases, collections, callbacks, captures, and bindings;
- `Session`-named H2/H3 utility files and affected tests/build inputs.
- every repository dependent of the renamed APIs, headers, and test targets.

Out of scope:

- renaming or behavioral redesign of the higher-level `Zhttp::ClientLink` and
  `Zhttp::ServerLink` logical message types; their native-Link binding names
  still change as required by this plan;
- application parser state such as `Zhttp::ServerSession` and
  `Service::Session<Profile>`;
- `H3::Cxn`, `H3::CxnState`, `H3::CxnParser`, and `H3::CxnStream`;
- ownership, reconnect, pooling, admission, wire, or shutdown semantics.

## Invariants

The refactor must preserve the following:

- A native H2/H3 Link remains the same object that currently carries the
  `Session` name; no new object or allocation layer is introduced.
- A `Ztls::Link` has at most one current `Ztls::Cxn`; server/client ownership
  remains unchanged.
- Every H2 Stream belongs to exactly one H2 Link and cannot migrate.
- H2 stream IDs, limits, closed-stream history, and admission state remain
  scoped to one Link.
- H2 Rx-owned state remains on the Rx shard.
- H2 Tx windows, pending frames, and scheduling state remain on the Tx shard.
- H3 control, QPACK, migration, and QUIC Stream behavior remain unchanged.
- Client pool keys and selection behavior remain unchanged.
- Server accepted-Link ownership and release accounting remain unchanged.

## Target naming

### Native types

| Current | Target |
|---|---|
| `H2_::ClientSession` | `H2_::CliLink` |
| `H2_::ServerSession` | `H2_::SrvLink` |
| `TLS_::ClientSession` | `TLS_::CliLink` |
| `TLS_::ServerSession` | `TLS_::SrvLink` |
| `H3_::ClientSession` | `H3_::CliLink` |
| `H3_::ServerSession` | `H3_::SrvLink` |
| `ClientSessionSlot` | `CliLinkSlot` |
| native `Sessions` collection | `Links` |
| native `sessionDown()` | `linkDown()` |
| Stream-local native `using Session` | `using Link` |

### Native bindings

Higher-level logical adapters need a distinct name for their underlying
native Link:

| Current | Target |
|---|---|
| `NativeSession` | `NativeLink` |
| native `m_session` | `m_native` |
| native `session(...)` | `native(...)` |
| native `session()` | `native()` |

Do not mechanically rename parser/application `Session` template parameters
or embedded message-processing `m_session` members.

The target names replace the current names outright. Do not temporarily retain
`using ClientSession = CliLink`, `using ServerSession = SrvLink`, legacy
`session()` accessors, or equivalent migration aids.

### Files

| Current | Target |
|---|---|
| `ZhttpH2Session.hh` | `ZhttpH2Stream.hh` |
| `ZhttpH2Session.cc` | deleted; no replacement needed |
| `ZhttpH2SessionTest.cc` | `ZhttpH2StreamTest.cc` |
| `ZhttpH2SessionTest` | `ZhttpH2StreamTest` |
| `ZhttpH3Session.hh` | `ZhttpH3Cxn.hh` |

The existing `H2::Stream`, `StreamStateMachine`, `FlowWindow`,
`StreamRegistry`, and `Scheduler` types are not used by production code except
through `H2::Session`; several APIs are exercised only by
`ZhttpH2SessionTest`. Do not preserve this parallel/test-only model. The H2
utility header should instead contain the actual active Stream record currently
named `H2_::LogicalEntry`, its hash definition, and only helpers used by the
Link implementation. The old `.cc` only emits `StreamState` metadata and can
be deleted once that dead state machine is removed.

The H3 utility header continues to contain `H3::Cxn` and its connection-stream
adapters; only the misleading filename changes.

## Phase 1: Fold H2 stream state into the Link

The common `H2_::Wire<Impl, Logical>` base is the current per-Link H2 protocol
implementation and is the natural destination for `H2::Session` state.

1. Make the existing `Wire::m_entries` registry the single source of truth for
   active H2 Streams. Rename the model to reflect its actual role:

   - `H2_::LogicalEntry<Logical>` to `H2_::Stream<Logical>`;
   - `LogicalHash<Logical>` to a Stream-oriented name;
   - `m_entries` to `m_streams`;
   - `entry()`, `add()`, `remove()`, `clear()`, and `all()` as needed for a
     consistent Stream API.

   Each record continues to hold its associated higher logical object, but it
   is the native H2 Stream owned by the Link. Do not introduce a second active
   Stream registry.

2. Move only production-required `H2::Session` responsibilities into
   `H2_::Wire`:

   - local and peer Stream creation;
   - next local and last peer Stream IDs;
   - local and peer Stream limits/counts;
   - local Stream-ID exhaustion;
   - active Stream lookup and closure;
   - recently closed Stream tracking;
   - peer-idle/refused Stream handling;
   - peer maximum-concurrent-stream updates.

3. Do not migrate `H2::Session::queueLocal()`, `admitQueued()`, `m_pending`, or
   `m_pendingMax`. The engines use their own pending logical arrays and never
   call this separate pending API. Delete this dead state and its isolated
   tests.

4. Use the canonical native Stream record's existing `rxWindow` and
   `txWindowHint` fields. Keep the Tx-owned `TxWindowHash` as the required
   cross-shard counterpart. Delete the unused parallel `H2::Stream` record,
   `FlowWindow`, `StreamStateMachine`, and `StreamRegistry` rather than
   migrating them.

5. Move the remaining required storage into the Rx-owned portion of `Wire`:

   - closed Stream array/set and ring cursor;
   - Stream ID and count fields;
   - configured limits;
   - server/client role state where it is not already present.

6. Extend `initWire(bool server, const H2Config &)` to initialize all Link-local
   Stream state. Derive client/server limits from the existing configuration
   exactly as the concrete Links do today. Apply `maxStreamID()` to client
   Links.

7. Extend `finalWire()` to clear all active/recent Stream state and reset its
   counters before releasing the existing wire/HPACK state.

8. Integrate local/peer counters and recently-closed tracking with the
   canonical `Wire` Stream add/remove/clear paths so one lifecycle transition
   updates all Link-owned state exactly once.

   Replace the current two-step `H2::Session::open*()` then `Wire::add()` flow
   with one Wire-owned admission/insert path. Preserve existing externally
   observable failure results. Ensure the consolidation itself cannot consume
   a Stream ID/count or leave a partially admitted canonical Stream when
   logical-object construction or hash insertion fails.

9. Replace concrete-Link calls through `m_session` or `m_h2` with inherited
   `Wire` operations.

10. Remove all four concrete `H2::Session` members from the forced-H2 and
   mixed-ALPN client/server Links.

11. Delete `H2::Session` and every now-dead helper after all production callers
    have migrated. Do not migrate an API solely to keep its old unit test.

### H2 state audit

`Wire` and `H2::Session` currently contain similarly named state. Audit each
pair before consolidating it:

- `m_lastPeer`;
- peer initial-window state;
- the obsolete `H2::Session` registry versus `Wire::m_entries`;
- the obsolete `H2::Stream` windows versus the active Stream record's
  `rxWindow`/`txWindowHint` and the Tx-owned windows.

Use the `Wire` registry and active Stream record as canonical. Unify other
fields only when they have identical semantics and shard ownership. In
particular, the two current `m_lastPeer` values have different update points:
one controls peer Stream-ID validity while the other records the last processed
peer Stream for GOAWAY. Preserve both roles under precise names unless code
inspection proves they can be unified. Where windows are intentional Rx/Tx
shadows, retain both and rename them to make ownership and purpose explicit.
Do not merge Rx and Tx containers.

## Phase 2: Rename native H2/TLS Links

Apply the native type rename throughout `ZhttpH2Engine.hh`.

1. Rename the forced-H2 `H2_::ClientSession` and `H2_::ServerSession` classes,
   constructors, destructors, forward declarations, CRTP arguments, and Wire
   arguments to `CliLink` and `SrvLink`.

2. Rename the mixed-ALPN `TLS_::ClientSession` and `TLS_::ServerSession`
   classes in the same way. They remain Links even when ALPN selects H1.

3. Change Engine aliases and containers to use `Link` terminology directly;
   remove aliases of the form `using Session = ...; using Link = Session`.

4. Rename pool slots, local variables, lambda captures, teardown callbacks,
   and `sessionDown()` notifications to Link terminology.

5. Change logical/native bindings to `NativeLink`, `m_native`, and
   `native(...)`. Keep higher-level logical Link types unchanged.

6. Update `HeaderBlock`, `Wire`, logical-entry, and accepted-Cxn template
   arguments without changing their behavior.

Avoid global textual replacement: `Session` is also used for the deliberately
out-of-scope application parser state in the same header.

## Phase 3: Rename native H3 Links

Apply the native type rename throughout `ZhttpH3Engine.hh`.

1. Rename `H3_::ClientSession` and `H3_::ServerSession` to `CliLink` and
   `SrvLink`, including forward declarations, constructors, CRTP arguments,
   and `Zquic::Client`/`Zquic::Server` parameters.

2. Rename `ClientSessionSlot`, `Sessions`, the `sessions` member, and
   `sessionDown()` to Link terminology.

3. Change H3 Stream aliases from `Session` to `Link` and update their
   `Zquic::CliStream`/`SrvStream` and `H3::Cxn` template arguments.

4. Rename local variables and cross-shard captures from `session` to `link`
   where they refer to the native H3 Link.

5. Change higher logical/native bindings from `NativeSession`/`m_session` to
   `NativeLink`/`m_native` and expose `native(...)` accessors.

6. Leave the embedded `H3::Cxn` member and all `Cxn*` parser/control names
   unchanged.

The type-erased client Link slot contains function pointers and `ZmContext`.
Update every cast and callback together so stop/finalization continues to pin
and address the same native object.

## Phase 4: Rename utility files and tests

1. Rename the H2 utility header and include guard to `ZhttpH2Stream.hh`. Move
   the actual active H2 Stream record/hash from `ZhttpH2Engine.hh` into it as
   appropriate. Delete `ZhttpH2Session.cc`; do not create an empty replacement.

2. Rename `ZhttpH2SessionTest` to `ZhttpH2StreamTest` in the source tree and
   `zhttp/test/Makefile.am`.

3. Retain direct tests for `QueueAdmission`, which is used by `Wire`. Delete
   tests for the production-dead `StreamStateMachine`, `FlowWindow`,
   `StreamRegistry`, `Schedule`, and `Scheduler` types along with those types.

4. Replace the former direct `H2::Session` behavior tests with an H2 Link/Wire
   fixture so Stream admission, IDs, limits, closure, and recent-history logic
   are tested through their actual owner and canonical Stream registry. Do not
   preserve test-only pending admission behavior, and do not add production
   accessors or hooks solely for the test.

5. Rename `ZhttpH3Session.hh` to `ZhttpH3Cxn.hh` and update its guard,
   comments, includes, and installed-header entry.

6. Update `Zhttp.hh`, `ZhttpH3Engine.hh`, `zhttp/src/Makefile.am`, test build
   inputs, and every repository include and dependent. Delete the old header,
   source, and test paths; do not retain forwarding headers at the old paths.

7. Search the entire repository, not only `zhttp`, and propagate the breaking
   changes through examples, utilities, tests, interop programs, build files,
   and any other dependent code in the same change.

## Phase 5: Static audit

Use scoped searches to prove the migration is complete:

```sh
rg -n "H2_::(ClientSession|ServerSession)|H3_::(ClientSession|ServerSession)" . --glob '!links.md'
rg -n "TLS_::(ClientSession|ServerSession)|ClientSessionSlot|sessionDown" . --glob '!links.md'
rg -n "H2::Session|ZhttpH2Session|ZhttpH3Session" . --glob '!links.md'
rg -n "H2::(Stream|StreamState|StreamStateMachine|FlowWindow|StreamRegistry|Schedule|Scheduler)\b" . --glob '!links.md'
rg -n "NativeSession" zhttp
```

The repository-wide searches exclude this plan because it documents the legacy
names being removed. All searches above must return no implementation results.
Then review remaining `Session`,
`m_session`, and `session(...)` occurrences manually. They are acceptable only
when they refer to the explicitly out-of-scope higher message/parser layer.
No match may be dismissed merely because it is in a test, example, utility,
interop program, or build input; all dependents must use the new API directly.

Also run:

```sh
git diff --check
```

## Phase 6: Verification

Do not clean, rebuild, or run tests without explicit instruction. When build
and test execution is authorized, verify in this order:

1. Build the affected ZHTTP library and test binaries with the repository's
   existing configured build.
2. Run the renamed H2 stream-state test.
3. Run the H2 and H3 engine tests.
4. Run H3 control/QPACK propagation tests.
5. Run the transport contract and mixed H1/H2 message tests.
6. Run the H2/H3 end-to-end message tests and engine lifecycle test.

Relevant test binaries include:

```text
zhttp/test/ZhttpH2StreamTest
zhttp/test/ZhttpH2EngineTest
zhttp/test/ZhttpH3EngineTest
zhttp/test/ZhttpH3PushTest
zhttp/test/ZhttpTransportContractTest
zhttp/test/ZhttpMessageH1TLSTest
zhttp/test/ZhttpMessageH2Test
zhttp/test/ZhttpMessageH3Test
zhttp/test/ZhttpEngineLifecycleTest
```

Run source-tree binaries through the normal build-tree paths; use `libtool
exec` when running them under a debugger or instrumentation.

## Review checklist

- No native H2/H3/TLS transport type retains a `Session` name.
- No `H2::Session` or independent `StreamSet` remains.
- No production-dead parallel `H2::Stream`, state machine, flow-window,
  registry, or scheduler model remains.
- The native H2 Link has one canonical active-Stream registry and directly
  owns `0..N` H2 Streams plus all Link-scoped Stream state.
- Forced-H2 and mixed-ALPN paths use the same common H2 Link state.
- No higher-level `zhttp` logical Link or parser Session was unintentionally
  renamed.
- Every repository dependent uses the new names and file paths directly.
- No compatibility aliases, forwarding headers, wrapper methods, deprecated
  entry points, or other legacy shims were introduced.
- No cross-shard access was made acceptable with locking.
- Client pool selection and server acceptance/release behavior are unchanged.
- H3 `Cxn` and QPACK/control-stream behavior are unchanged.
- Installed header lists and test targets contain only the new filenames.

## Completion criteria

The work is complete when the static audit is clean, all repository dependents
have been propagated, the review checklist is satisfied, and—once execution is
authorized—the affected library and all listed tests build and pass without
changes to runtime behavior.
