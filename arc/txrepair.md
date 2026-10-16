# Tx stream repair plan

## Objective and scope

Make framework-owned Tx stream failures observable through retained state,
without throwing for expected formatting, allocation, or transmit failures.
Expose failure through `failed()`; no new error categories or diagnostic storage
are required. Failure is sticky and subsequent appends are ignored. An earlier
valid buffered prefix may still flush after a formatting-capacity failure.
Propagate failure through output adapters and callers, and make `flush()` return
`bool ok`.

`ZuStream` (including `ZuWStream`) and `ZuVStream` (including `ZuVStreamBuf`)
are out of scope. Leave their implementations and tests unchanged. `ZiIOBuf`
is in scope as an output sink: its append and `operator <<` paths must retain
failure and behave consistently with the Tx streams. Rx streams, Rx layers,
receive parsing, and receive error contracts remain out of scope. Dependent
callers change as needed to consume the repaired output APIs and results.
Transport repairs must preserve existing shard ownership and asynchronous error
delivery; they must not add access to Rx-owned state.

The implementation inventory covers all repository-owned source directories,
including tests, utilities, examples, integration tests, benchmarks, and interop
programs. Primary components are:

- `zi/src/ZiTxStream.hh`: `TxStream`, `TxLayer`, formatting and buffer callbacks.
- `zi/src/ZiFileTxStream.hh`: buffered and direct file output.
- `zi/src/ZiIOBuf.hh`: append/printing failure state, checked growth, and Tx
  callers that must propagate buffer failure.
- `ztcp/src/Ztcp.hh`, `ztls/src/Ztls.hh`,
  `zquic/src/ZquicStream.hh`: concrete Tx streams and allocation/send adapters.
- `zhttp/src/Zhttp.hh`, `ZhttpH1.hh`, `ZhttpH2Hub.hh`, `ZhttpH3.hh`:
  retained output, frame/body streams, deferred header/data commits, and producers.
- `zws/src/ZwsTx.hh`, `ZwsCodec.hh`: WebSocket Tx preparation, empty-frame
  emission, finalization, and producer status.
- `zv/src/ZvCf.hh`, `ZvYAML.hh`, `ZvTOML.hh`: file-stream save callers.
- Dependent output wrappers and callers: migrate only interfaces and status
  checks required by the repaired streams.

Do not impose an I/O failure API on unbounded string/array containers or on
caller-bounded bit writers solely because they support streaming syntax.

## Type inventory

Template entries cover every specialization, including application-thread and
Tx-thread variants. Public aliases identify the same type, not separate repairs.
This inventory names repair owners separately from dependent migration/checks;
existing successful sinks do not acquire new failure APIs merely to match a name.

### Types requiring repairs or changed callback contracts

| Type | Definition | Planned repair |
| --- | --- | --- |
| `Zi::TxStream<Impl>` (`ZiTxStream<Impl>`) | `zi/src/ZiTxStream.hh` | Sticky failure, nullable callback allocation and existing `ensure()` result checks, printable rejection with prefix preservation, delegate rollover, boolean flush, moves and finalization. |
| `Zi::TxLayer<Impl, Below>` (`ZiTxLayer<Impl, Below>`) | `zi/src/ZiTxStream.hh` | Checked preparation, local failure propagation, lower-stream status, boolean finalization and callback lifetimes. |
| `Zi::FileTxStream<NTP>` (`ZiFileTxStream<NTP>`) | `zi/src/ZiFileTxStream.hh` | Add `failed()`, remove capacity throw, preserve earlier prefix, check writes, return boolean flush, prevent revival/replay. |
| `Zi::IOBuf` (`ZiIOBuf`) | `zi/src/ZiIOBuf.hh` | Add sticky `failed()` for append/printing, check nullable growth before writing, suppress subsequent output after failure, and propagate status to Tx producers. |
| `Ztcp::Link<...>::TxStream_<AppThread>` | `ztcp/src/Ztcp.hh` | Remove explicit allocation throw; migrate callbacks and ensure safe concrete finalization. |
| `Ztls::Link<...>::TxStream_<AppThread>` | `ztls/src/Ztls.hh` | Remove allocation/geometry throws from recoverable paths; migrate callbacks and concrete finalization. |
| `Zquic::Stream<...>::TxStream_<AppThread>` | `zquic/src/ZquicStream.hh` | Consistent nullable allocation, lower admission status, and concrete finalization. |
| `Zhttp::RetainedTx<Lower>` | `zhttp/src/Zhttp.hh` | Checked commit/seal, sticky rejection, inherited failure in validity, and release unsent entries without replay. |
| `Zhttp::MessageTx<Message, Ops>` | `zhttp/src/Zhttp.hh` | Check flush and commits; stop body submission/completion after rejection. |
| `Zhttp::H1::BodyStream<Lower>` | `zhttp/src/ZhttpH1.hh` | Migrate checked preparation and boolean flush; preserve content-length validity and verify concrete finalization. |
| `Zhttp::H1::ChunkedStream<Lower>` | `zhttp/src/ZhttpH1.hh` | Migrate checked preparation; suppress sends after preparation failure and propagate flush status. |
| `Zhttp::H2_::DataStream<Lower>` | `zhttp/src/ZhttpH2Hub.hh` | Checked DATA preparation, inherited status, boolean flush, and finalization. |
| `Zhttp::H2_::FrameStream<Native>` | `zhttp/src/ZhttpH2Hub.hh` | Checked allocation/collection status and safe concrete finalization. |
| `Zhttp::H2_::HeaderBlock<Native, AppThread>` | `zhttp/src/ZhttpH2Hub.hh` | Boolean flush/commit, rejected data/header/end handling, retained validity and compression commit ownership. |
| `Zhttp::H2_::HeaderBlock<Native, AppThread>::Bytes` | `zhttp/src/ZhttpH2Hub.hh` | Propagate frame-stream failure through byte emission, rollover, and boolean flush. |
| `Zhttp::H2_::Wire<Impl, Logical>` | `zhttp/src/ZhttpH2Hub.hh` | Expose/check synchronous header/end rejection and raw sends used by deferred commits, within Tx-owned methods only. |
| `Zhttp::H3::DataStream<Lower>` | `zhttp/src/ZhttpH3.hh` | Checked preparation, inherited status, boolean flush, and finalization. |
| `Zws::TxLayer<Lower, Masked, Random>` | `zws/src/ZwsTx.hh` | Checked preparation/masking, inherited failure in validity, boolean flush, no empty fallback after failure, and safe finalization. |
| `Zws::Codec<...>` | `zws/src/ZwsCodec.hh` | Observe layer failure and explicit finalization before reporting successful output. |

### Dependent types to migrate and verify

Edit these only where their Tx path needs the changed interfaces or demonstrably
loses failure. Their inclusion is not a claim that each needs new retained state.

| Type | Definition | Migration/check |
| --- | --- | --- |
| `Ztcp::Link<...>` and `Ztls::Link<...>` | `ztcp/src/Ztcp.hh`, `ztls/src/Ztls.hh` | Allocation helpers, removal of nested `TxStreamAllocFailure`, and existing admission/error callback contract. |
| `Zquic::Stream<...>`; `Zquic::CliStream`, `Zquic::SrvStream` | `zquic/src/ZquicStream.hh` | Tx-only allocation/admission dependencies and application-facing completion behavior. |
| `Zhttp::H2_::StreamBytes<Stream>` | `zhttp/src/ZhttpH2Hub.hh` | Preserve underlying failure while adapting byte writers. |
| `Zhttp::H3::TxBytes<Stream>` | `zhttp/src/ZhttpH3.hh` | Check underlying Tx status before treating counted byte output as successful. |
| `Zhttp::H1::Builder_`, `Request`, `Response`; H2 and H3 builder/request/response counterparts | `zhttp/src/ZhttpH1.hh`, `ZhttpH2.hh`, `ZhttpH3.hh` | Header/body/finish status and direct buffer users; public `Zhttp::H1Request`/`H1Response`, H2/H3 aliases follow these owners. |
| `Zhttp::H2_::LogicalStream`, `Zhttp::H3::LogicalStream`, `Zhttp::Stream`, `StreamDispatch`, `StreamBinding` | `zhttp/src/ZhttpH2Hub.hh`, `ZhttpH3.hh`, `ZhttpTransport.hh` | Propagate producer and submission status at the existing operation boundary. |
| `Zhttp::ClientSession<...>`, nested `ReqOps`, `TxOps`, `Builder_` | `zhttp/src/ZhttpClient.hh` | Follow `MessageTx` completion/failure into request submission and accounting; edit only affected Tx methods. |
| `Zhttp::Server<...>`, nested `ResponseOps`, `ResponseTx`, `ServerTxOps`, `BodyEmit_`, `StreamBody_`; `Zhttp::ResBuilder` | `zhttp/src/ZhttpServer.hh` | Follow `MessageTx` completion/failure into response submission, body output, and accounting. |
| `Zhttp::Compression::PrintBytes<Bytes>`, `FieldSectionBuffer<Bytes>` | `zhttp/src/ZhttpCompression.hh` | Migrate affected direct buffer output paths and propagate buffer/stream failure at encoding completion boundaries. |
| `Zhttp::H2::HPackEncoder`, `HPackTxTable` | `zhttp/src/ZhttpHPack.hh` | Verify commit ownership after rejected/partially accepted output; change only if required by that contract. |
| `Zmcp::HTTPSSEBuilder<Catalog>` and `Server<...>::Work_` | `zmcp/src/ZmcpServer.hh` | Reject failed direct-buffer output and retain the builder through synchronous SSE resume/finalization. |
| `Zmcp::StdioOutput` | `zmcp/src/Zmcp.hh` | Observe underlying `ZiIOBuf` failure and reject failed stdio frames at submission. |
| `Zmcp::HTTPOutput<Out>` | `zmcp/src/Zmcp.hh` | Propagate boolean flush and underlying output failure through the bounded HTTP body wrapper; preserve its existing overflow policy. |
| `ZfCLI::OutStream<S>`, `OutStream<Argv>`; `ZfURI::OutStream<S, Delimiter>` | `zf/src/ZfCLI.hh`, `ZfURI.hh` | Verify forwarding and concrete sink checks where used for Tx output. |

File-stream dependent functions, rather than types, also require migration:
`ZiHeapTune::save`, `ZiHashTune::save`, `ZvCf::save` (including `saveUpd` and
`saveDel`), `ZvYAML::save`, `ZvTOML::save`, and the telemetry save paths in
`zi/itest/zheapwork.cc` and `zhashwork.cc`. Check stream/flush status even when
`file.error()` is clear.

### Test types requiring callback/lifetime migration and regression coverage

- `ZiTxStreamTest_::TestTxStream`, `TestLayer`, and `StreamHarness`
  (`zi/test/ZiTxStreamTest.cc`).
- `ReserveLayer` in `ztcp/test/ZtcpLoopTest.cc` and
  `ztls/test/ZtlsBufHookTest.cc`.
- `CaptureTxStream` in `zhttp/test/ZhttpQPackDynamicTest.cc`.
- `BodyTx`, `StreamLink::Tx`, and `TxLink::Stream` in
  `zhttp/test/ZhttpTransportContractTest.cc`.
- `FlowWire::Tx` in `zhttp/test/ZhttpH2HubTest.cc`.
- `WireTx` in `zws/test/ZwsProtocolTest.cc`.
- `ContractBody<Tx_>` in `zhttp/test/ZhttpTransportContractTest.cc`;
  `CaptureStream` and nested no-op `Tx` fixtures in
  `zhttp/test/ZhttpH2MessageTest.cc` and `ZhttpQPackDynamicTest.cc`.
- `BufTx` in `zhttp/interop/zhttpfallbacktest.cc`, `HTTPBodyOut` in
  `zmcp/test/ZmcpPeerTest.cc`, and `BodySink` in `zrest/test/ZrestTest.cc`,
  where a migrated producer requires boolean finalization.
- `ZiIOBuf` append/printing test fixtures: extend existing buffer tests or add
  a TAP test entry point for deterministic growth rejection and sticky failure.
- File-stream specializations in `zi/test/ZiFileTest.cc`, including the
  8-byte and default-buffer cases.

Re-run the type/callback inventory at implementation time and extend this list if
new owners appear. Rx methods of mixed Rx/Tx owner types remain excluded.

## Evidence and invariants

The audit reproduced these failures with a temporary Linux probe:

- Oversized formatted file output throws while `ZiFile::error()` remains clear.
- A file stream accepts and writes more output after a failed write, if the
  underlying file is reopened.
- Oversized formatted `ZiTxStream` output throws while `failed()` remains false.
- Delegate formatting sends a 20-byte frame through an 8-byte `ZiTxStream`.

Static inspection also found throwing TCP/TLS allocation callbacks, an unchecked
null allocation result in the generic Tx stream, and ignored lower-send failures
in HTTP retained commits. H2 deferred commits additionally ignore `sendData()`
results; their header/end submission APIs return void and need a contract review.

The agreed target invariants are:

- Expected framework failures latch state before returning to the caller.
- Failure is sticky for the lifetime of that output operation; success later in
  the operation cannot erase it.
- Failed streams accept no further output and perform no further allocation or
  delegate invocation. After rejecting an oversized printable, explicit flush or
  destruction may prepare and send only the valid prefix buffered before failure.
  Allocation, preparation, or send rejection must not trigger retry or replay.
- `flush()` returns false whenever the stream has failed, even if sending an
  earlier valid prefix succeeds. Successful empty flushes return true.
- Every transmitted frame respects payload capacity plus reserved head/tail room.
- Synchronous admission failure reaches the producing stream and its caller.
- Acceptance into an asynchronous queue is distinguished from eventual delivery.
- Destruction safely releases retained buffers and does not throw.
- Bytes already written or submitted cannot be rolled back. A failure must not
  be reported as success, but no implicit retry or replay is introduced.

## Build and validation constraints

Use the current clang debug configuration throughout. ASan, LSan, GCC, MinGW,
and release builds are out of scope. Do not reconfigure or clean the build to
exercise another toolchain or instrumentation mode.

Rebuilding is expensive. Batch related implementation, dependent API migrations,
and regression tests before rebuilding. Prefer source inspection and review
between batches; rebuild again only after material changes or a failure that
requires another iteration. The numbered repair steps are logical work items,
not separate rebuild checkpoints.

Use the configured module traversal order, with `-j8` up to and including `ztc`
and `-j4` for all modules thereafter. `ztc` is the cutoff, not `ztcp`. Preserve
optional-module selection from the current configuration. For a full rebuild,
read the configured top-level `SUBDIRS` and run each module’s default `all`
target in that order with the respective job limit. Do not pass `SUBDIRS`
overrides, because recursive make propagates them into module builds; do not run
an unrestricted top-level `make -j8` across the later modules. Build changed
libraries before their dependent tests/programs, preserving dependency order.

Valgrind is the final memory-validation gate after the batched build and ordinary
functional checks pass. Run it earlier only when needed to investigate observed
memory corruption. Use the source-tree libtool wrappers, existing applicable
suppressions, and a nonzero error exit code; do not run `.libs` executables
directly. Report unresolved memory errors and leaks as gate failures.

## Repair sequence

### 1. Establish the error and callback contracts

Apply the numbered decisions below when choosing storage and changing APIs.
Resolve the remaining implementation ambiguities at their owning boundaries.
Inventory all `allocBuf_`, `sendBuf_`, `prepareBuf_`, `txAllocBuf`, `valid`,
`seal`, and `commit` implementations and their callers. Trace each status to
the application's completion/failure decision, including destructor flushes.

Document the contract in the owning headers. Keep successful hot paths allocation
free beyond their existing pooled buffers. Use framework types and direct CRTP
callbacks, and propagate API changes through dependents without compatibility
forwarders. Read module guidelines before implementation.

### 2. Repair the generic Tx stream and layer boundary

Replace oversized formatted-output throws with the agreed failure transition.
Check an individual printable's required length against empty-buffer payload
capacity before rollover. Oversized printables fail without rendering or splitting;
retain the earlier valid prefix so it can flush once, without clearing failure.
Keep this transition separate from allocation/preparation/send failure, where
buffer release must prevent retry or replay. Continue splitting raw spans and
streaming delegates within frame capacity.

Make allocation return success/failure to append and rollover paths. Check null
results before dereferencing, latch failure, and stop the current operation.
Where stream code calls `ZiIOBuf::ensure()`, check its nullable result before
writing and latch stream failure. Repair `ZiIOBuf` append and formatted printing
so growth failure is retained rather than silently ignored or dereferenced.
Expose `failed()` and suppress subsequent append/delegate output after failure.
Preserve length accounting for bytes actually written; migrate return values
only where a checked result is needed by callers. A Tx producer must observe
buffer failure before reporting success or sending invalid output. Keep protocol
frame capacity enforcement in the stream; buffer growth does not authorize
oversized protocol frames.

Route delegate printers through the stream itself so nested appends use capacity,
rollover, and failure handling. Maintain reserved head/tail room and avoid an
intermediate contiguous rendering allocation.

Make `prepareBuf_` return a checked result for preparation failures that suppress
transmission. Keep failure state local and propagate rejection to the producer.
Do not let raw callback success revive a failed lower stream. Review lower-send
propagation. Ensure every
output operation observes failure even where adapters call a lower stream's
raw callbacks instead of its public append interface. Apply the agreed ownership
rule rather than adding shared state or locks.

Change stream and relevant output-adapter `flush()` APIs to return `bool ok`,
and check that result at explicit success boundaries. Review move construction/
assignment, pending output, and destruction. Flush while
the concrete callback implementation and its dependencies remain alive; avoid
CRTP callbacks into destroyed derived members. Preserve transferred failure state
and define how an assignment-time flush failure is observed.

### 3. Repair file Tx output

Add stream-owned sticky failure and `failed()`. Record formatted-capacity failure
locally, retaining an earlier valid prefix for flushing; this must not depend on
a file operation having failed. Return `bool ok` from `flush()`.

Check every `ZiFile::write()` result, including buffer rollover, explicit flush,
destructor flush, and large direct writes. Keep existing OS diagnostics on
`ZiFile`; do not add stream error categories or snapshots. Stop a direct write
if flushing its preceding buffered output failed. Release failed buffers without replaying them,
because a failed write can already have written a prefix.

Keep stream failure independent of subsequent changes to the referenced `ZiFile`,
and require a new stream for recovery. Historical file errors do not by themselves
fail a newly constructed stream. Update heap/hash tuning saves, CF/YAML/TOML saves, telemetry writers,
and tests to check `flush()` and stream failure before claiming success. Existing
OS diagnostics remain available through the file. Higher-level save functions may
translate failure into their existing exception contract at that boundary.

### 4. Repair transport Tx adapters

Replace TCP/TLS `TxStreamAllocFailure` throws with the agreed allocation failure
result, remove the dead exception types, and update direct `txAllocBuf` callers.
Make QUIC allocation failure follow the same result contract.

Keep invalid internal geometry under the existing assertion policy. Do not add repeated checks
for geometry already guaranteed at its origin.

Preserve existing TCP/TLS/QUIC admission and asynchronous error callbacks.
Document which failures can reach a short-lived Tx stream synchronously and which
belong to the longer-lived link/request operation after queued submission.
Callbacks must run on their established owning shard; do not retain stack streams
in posted work or introduce Rx/Tx shared mutable error state.

### 5. Repair HTTP retained and deferred output

Make `RetainedTx::commit()` return success/failure and latch rejection. Stop on
the first failed send and release remaining entries. Include inherited stream
failure in validity/sealing checks. Stop the fixed-message producer before body
commit if header commit failed, and before completion/finish if body commit failed.

Apply the same admission accounting to H2 `HeaderBlock::commit()`: check data
submission, and give header/end submission the status needed by its caller.
Establish HPACK state ownership on rejected or partially accepted commits before
changing encoder rollback behavior. Do not replay accepted frames or leave a
compression context usable when its peer-visible state is uncertain.

Verify H1 body/chunked streams, H2/H3 data streams, frame collectors, and their
application producers use the agreed failure status. Content-length validity
remains a distinct protocol outcome unless the contract deliberately unifies it.

Include WebSocket `Zws::TxLayer` in this preparation/finalization migration.
Its current flush can attempt `emitEmpty_()` after a failed base flush and ignores
lower flush status. Return `bool ok`, stop empty-frame fallback after failure,
check RNG/preparation results before sending, and make `valid()` include inherited
failure. Finalize while WebSocket framing state is alive; update `Zws::Codec`
producers and protocol tests to check that result.

### 6. Repair buffer output and migrate dependent callers

Update in-scope output wrappers and application callers only where the repaired
stream APIs require migration. Check concrete stream failure and boolean flush
results at their existing completion boundaries. Keep unrelated storage,
formatting, compression, and sink contracts unchanged.

Cover every `ZiIOBuf` insertion path: spans, characters, primitives, buffered
printables, and delegates. Propagate failure through direct Tx buffer users and
output wrappers. Retain pooled allocation and buffer tuning; review added state
for its effect on packet layout and allocation size. Define failure-state
initialization and reuse at the buffer lifecycle boundary before adding reset
behavior. Do not modify `ZuStream`, `ZuVStream`, their variants, or their tests.

### 7. Validate and complete the dependent migration

Use existing TAP test facilities and deterministic fake allocation/send callbacks
for failures. Cover:

- Oversized formatted output with empty and nonempty buffers, exact capacity,
  reserved head/tail room, and raw spans larger than a frame. A rejected value
  contributes no bytes; an earlier prefix flushes once while failure stays set
  and `flush()` returns false.
- Delegate formatting across frame boundaries, nested delegates, and failure
  during delegate output.
- Initial callback allocation, rollover allocation, checked buffer growth, send,
  and flush failure; no further callbacks after these failures. Test the permitted
  prefix finalization after capacity failure separately.
- Move and destruction with pending output and failed state; flush failures do
  not disappear or cause replay.
- File rollover, flush, and direct-write failure; sticky failure and no
  revival after changing/reopening the file. Use portable failure fixtures where
  possible, with a guarded Linux `/dev/full` case if useful.
- Layer preparation/send rejection and the selected status propagation rule.
- HTTP header/body commit rejection, H2 data/end rejection, no false completion,
  and compression state behavior after partial submission.
- `ZiIOBuf` raw/formatted/delegate growth rejection, accurate length accounting,
  sticky `failed()`, ignored subsequent insertions, and propagation into Tx
  streams and direct Tx buffer producers.
- In-scope adapter completion checks against repaired Tx sinks.

Replace `testOversizePrintableThrows` with tests of the agreed retained failure.
Extend `ZiFileTest` beyond successful buffering and formatting. Add relevant
transport and HTTP contract tests in their existing module test directories.

After a coherent batch of repairs and dependent migrations, rebuild with the
current clang debug configuration and the staged job limits above: `-j8` through
`ztc`, `-j4` thereafter. Build the default `all` target before affected module test
targets and relevant integration tests. Avoid repeating already-passing builds
and checks unless further changes, failures, or unresolved concerns require it.

Once functional checks pass, run affected regression and lifecycle paths under
Valgrind as the final gate, including buffer growth rejection, move/destruction,
file output, layered stream finalization, and retained/deferred commits. Earlier
Valgrind runs are reserved for investigating observed memory corruption. Review
successful-path allocations, copies, geometry, and shard ownership before
completion. No ASan, LSan, GCC, MinGW, or release-build validation is required.

## Numbered decisions

These record the user's answers to the original questions, preserving numbering.

1. **Failure representation:** `failed()` is sufficient. Do not add error
   categories or stream-owned diagnostic storage. Preserve existing diagnostics
   at their current owners.

2. **Oversized printable policy:** Fail. Keep the single-frame limit for
   `ZuPrintBuffer` values; continue splitting raw spans and streaming delegates.

3. **Pending output on failure:** Allow an earlier valid prefix to flush.
   Preserve that prefix after rejecting an oversized printable, suppress later
   appends, and leave failure latched after prefix finalization. This does not
   authorize retrying an allocation/preparation/send failure or partially written
   output. Retained HTTP message validity still gates commit: a failed retained
   message must not become valid merely because it contains an earlier prefix.

4. **Allocation and exception boundary:** Remove explicit framework throws and
   handle nullable allocation/growth results in streams and `ZiIOBuf` output
   paths. `ZiIOBuf` is included by the latest scope instruction because it exposes
   `operator <<`. Do not catch arbitrary application printer/callback exceptions
   or declare every formatting API `noexcept`.
   Verify allocator exhaustion behavior during implementation.

5. **Layer failure ownership:** Keep state local, propagate failure to the
   producing layer, and prevent raw callbacks from reviving failed lower streams.
   Choose the exact interface after auditing callback callers; add no shared
   mutable failure state or locks.

6. **Preparation failure:** Return a checked result from `prepareBuf_` when
   preparation failure must suppress transmission. Preserve explicit
   content-length truncation/validity semantics.

7. **Admission versus delivery:** Short-lived stream status covers synchronous
   production/admission. Later failures use existing link/request callbacks and
   longer-lived operation state. Keep their established shard ownership.

8. **File status and recovery:** Record failure of this stream's operations;
   later file changes cannot clear it. A new stream is required for recovery.
   Historical file error alone does not fail a new stream, and file boolean
   conversion indicates handle validity rather than write health. With decision
   1, retained stream state is the failure latch; OS diagnostics stay on `ZiFile`.

9. **Commit and compression semantics:** Expose synchronous rejection explicitly.
   Preserve encoder state for accepted work and fail the owning operation or
   connection when peer-visible compression state cannot safely continue.

10. **Finalization and move assignment:** `flush()` returns `bool ok`. Require
    explicit finalization at success boundaries; false indicates failure even if
    an earlier prefix was successfully sent. Destructor flushing remains best
    effort with safe callback lifetimes. Resolve assignment-time flush failure
    semantics before changing moves.

11. **Dependent output adapters:** Propagate underlying status at actual Tx
    completion boundaries and add adapter status queries only where an actual
    caller needs them. The later scope instruction supersedes the original
    proposal for `ZuStream` and `ZuVStream`: both, including their variants and
    tests, are excluded. Leave caller-bounded bit-stream contracts unchanged.

## Resolved numbered implementation ambiguities

1. **Move assignment after a failed destination flush:** Transfer ownership from
   the source while retaining the destination's failure. Release transferred
   pending output if the destination flush failed, so it cannot be replayed.
   Move construction transfers pending output and failure together.

2. **Allocator exhaustion guarantees:** `ZmHeap` and `ZmVHeap` retry via
   `ZmHeapFail`; a handler must request retry or terminate. The repairs check
   nullable callback/growth results and rejected growth geometry without
   promising recovery from allocator termination.

3. **H2 submission and compression ownership:** Header/end admission returns
   `bool`. Reserve admission for the entire header block before accepting it;
   commit HPACK updates after that acceptance. Preserve accepted encoder updates
   if later DATA/end admission fails. Reject the operation and disconnect the
   connection if a queued frame fails its raw transport handoff. Clear moved
   `ZtArray` views before subsequent validity checks.

4. **Buffer failure lifetime and reuse:** New buffers start healthy; `clear()`
   explicitly begins a new output operation and resets failure. Subsequent
   insertions cannot reset it. Existing length-based boolean conversion still
   describes readable data. Tx handoff rejects failed buffers; streams retain
   their own failure independently of buffer reuse.

The repair is complete when these contracts are documented, all dependent Tx
paths are migrated, expected framework failures cannot throw or produce a false
success within that scope, and the current clang debug functional checks and
final Valgrind gate pass under the specified rebuild limits.


## Implementation and validation record

The repair now uses sticky local failure, checked buffer handoff and preparation,
and boolean flush/commit results. Concrete CRTP implementations finalize while
callback members remain alive; the base destructor only releases buffers.
Higher-level file save APIs preserve their exception contract while checking
stream finalization, including failures without a file OS error.

Direct buffer producers in MCP stdio/SSE and QUIC handshake coalescing now
observe buffer failure. CLI/URI formatters forward to their concrete sink without
claiming output success and need no new failure state. `ZiCSV` retains its
existing writer/error contract and needs no stream repair. No Rx stream or
excluded `ZuStream`/`ZuVStream` implementation or test has changed.

On the configured x64 clang debug build, `ZiIOBuf` remains 56 bytes: the failure
bit at offset 52 occupies prior tail padding. Pool tuning and allocation layout
are preserved. Failure tracking adds no heap allocation, contiguous copy, lock, or
cross-shard state access. Delegate formatting uses frame rollover as required
by the transmit contract.

The configured full build and subsequent affected rebuilds passed with `-j8`
through `ztc` and `-j4` afterward. No reconfiguration, clean, alternate compiler,
or sanitizer build was used. Logs: `/tmp/txrepair-build-1.log`,
`/tmp/txrepair-build-2.log`, and `/tmp/txrepair-build-zi-test.log`.

Functional checks passed: 56 regression binaries (434 TAP cases), all seven HTTP
integration binaries, both MCP integration binaries, and the heap/hash telemetry
save workloads. HTTP integration binaries that launch `../util` programs must
run from `zhttp/itest`. Logs: `/tmp/txrepair-functional.log`,
`/tmp/txrepair-http-integration.log`,
`/tmp/txrepair-http-integration-retry.log`,
`/tmp/txrepair-mcp-integration-final.log`,
`/tmp/txrepair-heap-functional.log`, and `/tmp/txrepair-hash-functional.log`.

The regressions exposed a stale moved-array view in H2 header validity and a
builder use-after-free during synchronous MCP SSE finalization. Both were fixed.
GDB identified the H2 lifetime issue; the earlier investigative Valgrind run
identified the SSE error after its observed crash, within the permitted exception
to the final-gate rule. Added checks cover these completion boundaries and
failure propagation.

The final memory gate also exposed two pre-existing fixture issues: QPACK bounded
storage tests passed entire partially filled character arrays instead of the
formatted lengths, and TLS positive cases could count delayed logs from earlier
negative cases. The fixtures now pass actual lengths and drain the logger before
resetting the error counter. Rx implementations and parsing contracts remain
unchanged. The existing process-lifetime `ZmThread_Main` marker suppression now
also covers its narrowly identified main-thread startup stack (32 bytes).
The QUIC loop fixture now explicitly stops its logger before exit, avoiding
an intermittent logger-thread TLS allocation reported as possibly lost during
process teardown.

The final Valgrind gate passed all 24 selected regression/lifecycle binaries,
including TCP/TLS/QUIC, HTTP H1/H2/H3, buffer/move/file failure paths, WebSocket,
MCP wrappers, and MCP HTTP SSE integration. There are no reported memory errors
or unsuppressed definite, indirect, or possible losses. Source-tree libtool
wrappers were used, with a nonzero error exit code and narrowly scoped
process-lifetime suppressions. Gate manifest: `/tmp/txrepair-valgrind-result.txt`;
per-binary logs: `/tmp/txrepair-valgrind-*.log`. Fixture corrections were rebuilt
with `-j8` and rerun normally before their final Valgrind checks; logs:
`/tmp/txrepair-build-final-tests.log`, `/tmp/txrepair-build-quic-test.log`,
`/tmp/txrepair-functional-final-tests.log`, and
`/tmp/txrepair-functional-quic-final.log`.

The main-thread marker suppression extends `.valgrind-suppressions` in the
workspace. That file is ignored by Git; the exact added entry is retained in
`/tmp/txrepair-startup.supp` for review.

All repair contracts, dependent migrations, configured-build checks, functional
checks, and the final memory gate are complete. `git diff --check` passes.
