# `zhttp` Engine-to-Hub Rename Plan

## Summary

Rename the concrete `zhttp` coordinator role from **engine** to **hub**. The
change is deliberately mechanical and breaking for `zhttp`-owned names: types,
template parameters, aliases, namespaces, variables, members, accessors,
filenames, include guards, build targets, and associated allocator/log/test
identifiers that name this role will use the corresponding `Hub`, `Hubs`,
`hub`, or `hubs` spelling.

This is not a prohibition on the word or name **engine**. A `zhttp` hub derives
from and remains a `ZmEngine`; descriptions of that base-class relationship,
the lower-layer engine lifecycle, or engine behavior remain accurate and may
continue to say “engine.” Exact identifiers owned by lower-layer modules,
including `ZmEngine`, also remain unchanged. The deciding question is whether a
name identifies the concrete `zhttp` coordinator role (rename it to hub) or its
engine abstraction/base behavior (retain engine where accurate).

This brings the HTTP transport coordinators into line with Z's I/O naming
conventions without changing their ownership, threading, lifecycle, transport,
or protocol behavior. No compatibility aliases, forwarding headers, duplicate
test targets, or deprecated spellings will be retained; dependent code will
move to the new API in the same change.

The repository audit found direct consumers of `EngineConfig` and
`ClientEngine` in `zws`. The authorized implementation scope therefore includes
the mechanical propagation of the breaking `zhttp` API into `zws`. Subsequent
authorized work applies the same concrete coordinator rename throughout `zws`,
including its types, files, targets, variables, and fixtures. No compatibility
layer is required.

The current tracked `zhttp` sources contain approximately 612 case-insensitive
occurrences of `engine` across 29 files. They form the review inventory, not a
zero-match deletion target: each occurrence must be classified by ownership
and meaning. Generated build-tree artifacts such as `.deps/`, `.libs/`, object
files, configured `Makefile`s, and `Makefile.in` are not source inputs and will
not be hand-edited; they will be regenerated or replaced by a clean build.

## Architecture Documentation

This is a terminology/API migration, not an architectural change.

- `EngineConfig` becomes `HubConfig`, while retaining the same multiplex,
  Rx-thread, Tx-thread, and async-thread configuration.
- The normalized client and protocol-specific client/server coordinators become
  `ClientHub`, `ServerHub`, and `TLSHub` variants. Their CRTP inheritance and
  links to `Ztls` and `Zquic` remain unchanged; through those lower layers, a
  hub continues to derive from and be a `ZmEngine`.
- The lifecycle aggregate `Engines` becomes `Hubs`; its state machine,
  type-erased entries, start/stop continuations, finalization, and
  `stopAccepting()` detection remain unchanged.
- `Client` and `Service` continue to own the same TCP, TLS, and QUIC
  coordinators, but their aggregate member changes from `m_engines` to
  `m_hubs`.
- No threads, queues, callbacks, wire formats, network behavior, configuration
  semantics, or persistence formats change.

### Canonical rename map

| Existing | Replacement |
| --- | --- |
| `EngineConfig` | `HubConfig` |
| `ClientEngine` | `ClientHub` |
| `ServerEngine` | `ServerHub` |
| `TLSEngine` | `TLSHub` |
| `Engine` / `Engines` | `Hub` / `Hubs` |
| `Engines_` | `Hubs_` |
| template parameter or alias `Engine` | `Hub` |
| parameter/local `engine` | `hub` |
| local/aggregate `engines` | `hubs` |
| member `m_engine` / `m_engines` | `m_hub` / `m_hubs` |
| `engineCount()` | `hubCount()` |
| `quicEngineConfig()` | `quicHubConfig()` |
| `Zhttp.*Engine*` heap/log/test strings | corresponding `Zhttp.*Hub*` string |

The substitution applies equally to singular/plural forms embedded in longer
`zhttp`-owned identifiers when those names denote the concrete hub role.
Fully-qualified or otherwise unambiguous lower-layer identifiers, including
`ZmEngine`, retain their canonical spelling. Prose may also continue to use
“engine” when it describes that abstraction or behavior rather than naming the
concrete `zhttp` hub.

### Source code file rename map

Rename every tracked source-code basename containing the old term. Perform
these as version-control-aware moves so history follows the files, and do not
leave copies or forwarding headers under the old names.

Library source files:

- `zhttp/src/ZhttpClientEngine.hh` -> `zhttp/src/ZhttpClientHub.hh`
- `zhttp/src/ZhttpEngines.hh` -> `zhttp/src/ZhttpHubs.hh`
- `zhttp/src/ZhttpH2Engine.hh` -> `zhttp/src/ZhttpH2Hub.hh`
- `zhttp/src/ZhttpH3Engine.hh` -> `zhttp/src/ZhttpH3Hub.hh`
- `zhttp/src/ZhttpTLSEngine.hh` -> `zhttp/src/ZhttpTLSHub.hh`
- `zhttp/src/ZhttpTLSEngine.cc` -> `zhttp/src/ZhttpTLSHub.cc`

Test source files:

- `zhttp/test/ZhttpEngineFixture.hh` -> `zhttp/test/ZhttpHubFixture.hh`
- `zhttp/test/ZhttpEngineLifecycleTest.cc` ->
  `zhttp/test/ZhttpHubLifecycleTest.cc`
- `zhttp/test/ZhttpH1EngineTest.cc` -> `zhttp/test/ZhttpH1HubTest.cc`
- `zhttp/test/ZhttpH2EngineTest.cc` -> `zhttp/test/ZhttpH2HubTest.cc`
- `zhttp/test/ZhttpH3EngineTest.cc` -> `zhttp/test/ZhttpH3HubTest.cc`
- `zhttp/test/ZhttpMessageEngineTest.cc` ->
  `zhttp/test/ZhttpMessageHubTest.cc`; retain the four profile-specific message
  test binaries but point their `_SOURCES` variables at the renamed source.

The renamed test source files also rename their corresponding binaries where a
one-to-one binary exists:

- `ZhttpEngineLifecycleTest` -> `ZhttpHubLifecycleTest`
- `ZhttpH1EngineTest` -> `ZhttpH1HubTest`
- `ZhttpH2EngineTest` -> `ZhttpH2HubTest`
- `ZhttpH3EngineTest` -> `ZhttpH3HubTest`

All include directives, include guards, Automake source lists, test program
names, `_SOURCES` variables, test namespaces, and `ZiLog::init()` names must
follow these source-file basename changes. Files whose basenames do not contain
the old term remain in place and receive in-file identifier substitutions only.

## Detailed Design and Implementation Plan

### Phase 1: Move the source files and update the build graph

- Move all six library source files and all six test source files listed in the
  source code file rename map to their `Hub` basenames.
- Immediately update direct includes and header guards so no translation unit
  references an old path during the migration.
- Update `zhttp/src/Makefile.am` with the six renamed library basenames.
- Update `zhttp/test/Makefile.am` with the six renamed test source basenames,
  the four renamed one-to-one test programs, their `_SOURCES` variables, and
  the four profile-specific message test `_SOURCES` entries.
- Keep generated `Makefile`, `Makefile.in`, dependency files, objects, and
  binaries out of the patch; regenerate them after the source/build-input
  rename is complete.

### Phase 2: Rename the public configuration and lifecycle vocabulary

- Rename `EngineConfig` and all its constructors/fluent return types to
  `HubConfig` in `ZhttpConfig.hh`.
- Rename `Engines_`/`Engines`, their `Engine` template parameters, `engine`
  arguments, callback receiver names, heap IDs, comments, and include guard in
  the lifecycle coordinator, then rename the header to `ZhttpHubs.hh`.
- Preserve the lifecycle coordinator's exact state transitions and callback
  behavior. This phase changes names only; it must not opportunistically alter
  start, stop, rollback, or finalization logic.
- Rename the normalized `ClientEngine` template to `ClientHub`, including its
  `HubConfig` input and CRTP references, and rename its header.
- Update the umbrella header and direct consumers to include the new basenames.
  Do not leave forwarding headers under the old filenames.

### Phase 3: Rename the TLS, HTTP/2, and HTTP/3 coordinator families

- Rename `ZhttpTLSEngine.hh/.cc` to `ZhttpTLSHub.hh/.cc`, including guards,
  source includes, comments, and all `HubConfig hub` parameters used to create
  TLS client/server parameters.
- Rename `ZhttpH2Engine.hh` to `ZhttpH2Hub.hh`. Within both the H2-only and
  shared H1/H2 TLS implementations, rename every forward declaration, CRTP
  class (`ClientEngine`/`ServerEngine`), constructor, alias, template argument,
  callback parameter, and configuration local to its `Hub` form.
- Rename `ZhttpH3Engine.hh` to `ZhttpH3Hub.hh` and apply the same conversion to
  the QUIC client/server coordinators, link aliases, constructors, shutdown
  comments, and H3 specializations.
- Update `ZhttpClientPool.hh` and `ZhttpTLSClientPool.hh` to derive from/include
  the renamed classes and headers. Keep exact lower-layer names such as
  `ZmEngine` unchanged in lifecycle-hook comments, and retain “engine” where it
  describes the inherited engine hook; rename the `zhttp`-owned
  `H3_::ClientEngine` type reference to `H3_::ClientHub`.
- Keep all base classes from `Ztls` and `Zquic`, link ownership, migration
  behavior, and stop/drain sequencing intact.

### Phase 4: Propagate the API through client, service, and transport code

- In `ZhttpTransport.hh`, rename `EngineConfig` to `HubConfig` and all generic
  `Engine`/`engine` template parameters and values to `Hub`/`hub` across TCP,
  TLS, and QUIC traits.
- In `ZhttpClient.hh`, rename configuration parameters to `hub`, include
  `ZhttpHubs.hh`, and change `Engines m_engines` and every lifecycle use to
  `Hubs m_hubs`. Keep the current ingress-disable and Rx-drain ordering.
- In `ZhttpService.hh`:
  - rename `quicEngineConfig()` to `quicHubConfig()`;
  - rename the nested `Engine<Protocol>` and `TLSEngine` types to
    `Hub<Protocol>` and `TLSHub`;
  - rename temporary `engine` back-pointers to `hub`;
  - rename `Engines`, `m_engines`, and `engineCount()` to `Hubs`, `m_hubs`, and
    `hubCount()`;
  - propagate `HubConfig hub` through initialization and preserve shard
    dispatch, stop, link-drain, and finalization behavior.
- Update `ZhttpServer.hh` and every remaining source consumer to accept
  `HubConfig hub` and invoke the same transport parameter factories.

### Phase 5: Rename tests, fixtures, utilities, and documentation

- Rename the fixture, H1/H2/H3 integration tests, lifecycle test, and shared
  message test according to the file map. Rename their namespaces, guards,
  includes, source variables, binary targets, log component strings, heap IDs,
  locals, and assertion text.
- Update the remaining unit tests (`ZhttpClientCancelTest.cc`,
  `ZhttpServiceIdleTest.cc`, and `ZhttpTransportContractTest.cc`) to construct
  `HubConfig`, use `hub` locals, and call `quicHubConfig()`.
- Update integration tests (`zhttpclientfallbacktest.cc` and
  `zhttplifecycletest.cc`) so their local names and lifecycle diagnostics say
  hub/hubs.
- Update `zhttp/util/zhttp.cc` and `zhttp/util/zhttpdapp.cc` to construct
  `HubConfig`; change user-visible startup diagnostics to “HTTP hub”.
- Update `zhttp/README.md`, the lifecycle contract in `Zhttp.hh`, and
  `zhttp/util/zhttpboundary.md` so concrete coordinator names and examples use
  hub/hubs and compile against the renamed API. Retain engine terminology where
  the text intentionally explains `ZmEngine` inheritance or engine semantics.
- Change `ZhttpBoundaryFixture.sh` from forbidding `Zhttp::Engines` to
  forbidding `Zhttp::Hubs`, preserving the architectural boundary under the new
  name.
- Update `zhttp/src/Makefile.am` and `zhttp/test/Makefile.am` atomically with the
  file renames. Regenerate configured build files through the normal Autotools
  flow rather than editing generated files.
- Propagate the breaking API through `zws`: replace its `Zhttp::ClientEngine`
  bases with `Zhttp::ClientHub`, replace `Zhttp::EngineConfig` with
  `Zhttp::HubConfig`, and rename corresponding configuration parameters and
  locals from `engine` to `hub`. Apply the same rename to the concrete `zws`
  coordinator role, including `ZwsEngine.hh`, `ZwsH1Engine.hh`, their aliases,
  members, fixtures, and H1/H2/H3 test targets.

### Phase 6: Exhaustive residue check and verification

- Search the tracked module case-insensitively for `engine` and classify every
  hit. Rename occurrences that identify a concrete `zhttp` hub, including its
  types, aliases, parameters, locals, members, accessors, filenames, targets,
  and role-specific strings. Retain occurrences that accurately identify
  `ZmEngine`, another lower-layer symbol, the engine base abstraction, or engine
  behavior; record the rationale for non-obvious retained hits.
- Separately search filenames to ensure no tracked basename contains `Engine`
  or `engine`, and search the full repository for references to the removed
  `Zhttp*Engine*` files and APIs. Migrate active dependents such as `zws` and
  distinguish their own engine vocabulary from references to the removed
  `zhttp` role.
- Perform a clean/configure regeneration as needed, build `zhttp`, and run its
  unit and integration tests. A clean build is important because stale `.deps`,
  `.libs`, and old test binaries can make the deleted basenames appear to work.
- Review the final diff to confirm it is a pure naming migration: no lifecycle,
  I/O-shard ownership, buffer ownership, callback, or protocol behavior should
  have changed.

## Code References to Impacted Code

- `zhttp/src/ZhttpConfig.hh:288` - public `EngineConfig` definition.
- `zhttp/src/ZhttpEngines.hh:22` - lifecycle helper namespace, detector,
  allocator IDs, aggregate, and generic coordinator parameters.
- `zhttp/src/ZhttpClientEngine.hh:21` - normalized client coordinator template.
- `zhttp/src/ZhttpTLSEngine.hh:59` - shared TLS parameter construction.
- `zhttp/src/ZhttpH2Engine.hh:1688` - H2 and shared TLS client/server coordinator
  families and their link aliases.
- `zhttp/src/ZhttpH3Engine.hh:100` - H3 client/server coordinator families and
  their link aliases.
- `zhttp/src/ZhttpTransport.hh:87` - transport-neutral configuration plumbing
  and generic start/stop helpers.
- `zhttp/src/ZhttpClient.hh:208` - client initialization and owned lifecycle
  aggregate.
- `zhttp/src/ZhttpService.hh:90` - QUIC config accessor; nested server
  coordinators and service lifecycle aggregate begin at lines 214 and 747.
- `zhttp/src/ZhttpClientPool.hh:104` and
  `zhttp/src/ZhttpTLSClientPool.hh:301` - normalized coordinator consumers.
- `zhttp/src/Zhttp.hh:71` - public lifecycle contract and umbrella includes.
- `zhttp/src/Makefile.am:15` - installed source/header manifest.
- `zhttp/test/Makefile.am:26` - test target and source manifest.
- `zhttp/test/ZhttpEngineLifecycleTest.cc:13` - aggregate lifecycle state-machine
  coverage.
- `zhttp/test/ZhttpEngineFixture.hh:7` - shared H1/H3 integration fixture and
  reusable client coordinator types.
- `zhttp/test/ZhttpH2EngineTest.cc:20` - H2/shared-TLS integration coverage.
- `zhttp/test/ZhttpMessageEngineTest.cc:20` - profile-neutral message lifecycle
  coverage used by four binaries.
- `zhttp/test/ZhttpBoundaryFixture.sh:8` - forbidden dependency vocabulary.
- `zhttp/README.md:65` - public overview and API examples.
- `zws/src/ZwsH1Hub.hh:160` and `zws/src/ZwsHub.hh:34` - dependent
  WebSocket client bases and initialization APIs.
- `zws/src/zws.cc:190`, `zws/src/zwsd.cc:207`, `zws/bench/zwsbench.cc:336`,
  `zws/test/ZwsH1HubTest.cc:310`, and
  `zws/interop/zwsautobahnclient.cc:220` - dependent configuration consumers.

All additional files listed by the tracked residue scan in Phase 6 are part of
the same mechanical propagation even when not repeated individually above.

## Detailed Test Plan

1. Run a source terminology audit using the tracked file set, for example
   `git ls-files zhttp | xargs rg -n -i 'engine'`. Review every match rather
   than requiring empty output: no match may still name the concrete `zhttp`
   coordinator role as an engine, while references to `ZmEngine`, inherited
   engine mechanics, and engine behavior may remain.
2. Run a tracked filename gate, for example
   `git ls-files zhttp | rg -i 'engine'`; it must produce no matches.
3. Search repository-wide for the removed public names and basenames
   (`EngineConfig`, `ClientEngine`, `ServerEngine`, `Engines`,
   `quicEngineConfig`, `engineCount`, and `Zhttp*Engine*`). Confirm there are no
   missed `zhttp` references or active dependent references.
4. Regenerate/configure if the renamed Automake targets require it, then run a
   clean parallel build of the enabled `zhttp` configuration.
5. Run the renamed unit-test suite, with particular focus on:
   - `ZhttpHubLifecycleTest` for initialization rollback, repeated starts/stops,
     asynchronous completion, failure, and finalization;
   - `ZhttpH1HubTest`, `ZhttpH2HubTest`, and `ZhttpH3HubTest` for protocol and
     shutdown behavior;
   - all four message-profile binaries backed by `ZhttpMessageHubTest.cc`;
   - client cancellation, service idle handling, transport contracts, and the
     boundary test.
6. Run the `zhttp` integration tests, especially lifecycle, fallback, and
   multi-protocol client/server coverage, to demonstrate that the terminology
   change did not alter observable I/O behavior.
7. Build `zws/src`, its benchmark, and its interop client; run the `zws` test
   and available interop suites to verify the breaking API propagation.

## Acceptance Criteria

- Every tracked `zhttp`-owned name for the concrete coordinator role is changed
  to the matching hub vocabulary, including embedded, plural, lowercase,
  filename, build-target, and associated string forms.
- Reviewed uses of “engine” remain where they accurately describe `ZmEngine`,
  another lower-layer API, the base engine abstraction, or inherited engine
  behavior; the implementation does not distort those concepts merely to make
  a text search empty.
- The public API exposes `HubConfig`, `ClientHub`, protocol-specific
  `ClientHub`/`ServerHub`, `Hubs`, `hubCount()`, and `quicHubConfig()` with no
  compatibility aliases for the removed names.
- All renamed headers are installed and included under their new basenames; no
  forwarding copies of the old headers remain.
- Automake builds the renamed sources and test programs from a clean tree.
- The lifecycle state machine, asynchronous completion behavior, shard
  ownership, link draining, and protocol behavior remain unchanged.
- The full enabled `zhttp` unit and integration test suites pass.
- Outside `zhttp`, source changes are limited to the authorized mechanical
  propagation and equivalent concrete coordinator rename through `zws`.
- No active source retains a reference to the removed `zhttp` APIs or
  basenames.

## Non-goals

- Preserving source or binary compatibility with the old `zhttp` API.
- Renaming or locally masking lower-layer names such as `ZmEngine`, or similarly
  named types in `Zm`, `Ztls`, `Zquic`, or any other module.
- Adding aliases, deprecation periods, forwarding headers, or dual test names.
- Refactoring lifecycle logic, transport abstractions, protocol code, thread
  ownership, or public behavior while performing the vocabulary migration.
- Hand-editing generated or ignored build artifacts.

## Options and Open Questions

There are no blocking design questions. Renaming the concrete `zhttp` role and
the repository's no-compatibility posture select an atomic breaking migration,
while the underlying engine abstraction and its lower-layer identifiers remain
intact. The discovered `zws` dependents are migrated directly under the
authorized scope expansion.
