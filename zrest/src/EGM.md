# Zrest typed REST adapters

## Overview

Catalog-driven request/response codecs and asynchronous client/server lifecycle over Zhttp, including bounded input retention and server-sent events. Concrete application routes belong to caller catalogs.

## Interface

- [Zrest.hh](./Zrest.hh): `Request`/`Response` defaults, body/query policies, `Reply`, `SSEMeta`, `RequestContext`, `Failure`, configuration/limits, signing helpers and catalog macros. Request defaults are GET `/` with prefix matching and no body/query; response defaults are status 200 with no body.
- [ZrestClient.hh](./ZrestClient.hh): `ReqBuilder`, `ResParser` and catalog variants; `Client` exposes init/start/stop/final, pools, calls, state and limits. Calls retain reference-counted request/callback objects.
- [ZrestServer.hh](./ZrestServer.hh): `ReqParser`, `ResBuilder`, `SSEBuilder`, catalog/root selection, `Completion`, and `Server` lifecycle/admission. Applications receive typed request/context/completion tokens.
- [ZrestInput.hh](./ZrestInput.hh): `Input` assembles bounded bytes and transfers buffers; `BorrowedData` retains parse trees/backing. [ZrestSSE.hh](./ZrestSSE.hh): `SSEProducer::emit`, `start`, `close` and queue/failure queries.
- [Makefile.am](./Makefile.am): exports `libZrest.la` and six headers; the compiled unit is `ZrestLib.cc`, including path `skip`/`splitRoot` helpers. Dependencies include HTTP, QUIC, TLS/TCP and framework libraries.

## Invariants

- `valid(Limits)` requires every bound nonzero. Request catalogs reject duplicate paths within exact/prefix sets per method; matching strips queries and prefers exact matches. `SaveField` excludes signature fields from ordinary serialization; signing receives the encoded suffix after any prefix. [Zrest.hh](./Zrest.hh)
- Request/response body codecs require configured maxima, bound fixed lengths, reject streamed URI bodies, and finish at most once. JSON/URI bodies require successful observed decoding; JSON accepts one value with valid trailing whitespace. Borrowed mode retains backing. Standalone response codecs reject SSE; the catalog SSE path requires `text/event-stream` and bounded total/line/event sizes. [ZrestClient.hh](./ZrestClient.hh), [ZrestServer.hh](./ZrestServer.hh)
- `Input::begin` requires a nonzero maximum and fitting advertised size; feed failure resets assembly. Shared storage is copied before appending. `SSEProducer` enforces event/line/queue limits, excludes events after terminal admission, and confines start/drain to its scheduler thread. [ZrestInput.hh](./ZrestInput.hh), [ZrestSSE.hh](./ZrestSSE.hh)
- `Completion::complete` admits one atomic claim and constrains response types to the request catalog. Completion posts to the owner thread; producer access requires a live owner-thread token. Work selection verifies token, request and route; SSE uses that work's producer. [ZrestServer.hh](./ZrestServer.hh)
- Client finalization requires stopped state and no ingress/pending work. Server work remains tracked through response retirement; finalization requires stopped state, empty work/session queues and drained cancellation/stop state. [ZrestClient.hh](./ZrestClient.hh), [ZrestServer.hh](./ZrestServer.hh)

## Side Effects

- Builders serialize/flush bytes and patch content lengths; signing uses mutable owned buffers. Input allocates pooled storage, extracts received bytes and transfers backing. SSE serializes events, schedules bounded drain work, transfers output buffers, and drops queued work/releases callbacks on close. [ZrestClient.hh](./ZrestClient.hh), [ZrestServer.hh](./ZrestServer.hh), [ZrestInput.hh](./ZrestInput.hh), [ZrestSSE.hh](./ZrestSSE.hh)
- `Client::call` allocates requests and nonzero IDs, reserves bounded ingress for off-owner calls, admits on Rx and submits HTTP or failure callbacks. Delivery releases callback ownership and catches callback exceptions. Stop disables admission, stops HTTP and drains through Tx/Rx barriers. [ZrestClient.hh](./ZrestClient.hh)
- Server parsing derives transport scheme, normalizes authority and rejects mismatched/invalid decoded requests with disconnecting 400 when possible. Work dispatch invokes application handlers; cancellation invalidates tokens, closes producers and optionally notifies the application. Session/stop cancellation scans are bounded by workBatch; response retirement occurs once on the owner thread. [ZrestServer.hh](./ZrestServer.hh)

## Verification

From a configured repository root, run `make -j8 && make -C zrest/test test`. Module `make test` additionally invokes integration tests. No builds/tests were executed for this documentation change.
