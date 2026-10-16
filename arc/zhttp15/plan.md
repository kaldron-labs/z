# Adopt a fallible `Parser::body` contract

## Goal

Change the application and protocol parser body callback from
`void body(Rx &)` to `bool body(Rx &)`, where:

- `true` accepts the current body prompt and permits parsing to continue;
- `true` does not require the callback to consume all queued bytes;
- `false` rejects the current message, suppresses further message callbacks,
  and results in exactly one `complete(false)` application callback;
- body rejection is an application failure, not malformed HTTP;
- H1 terminates the connection after rejection unless a future explicit drain
  policy is introduced; H2 and H3 terminate only the affected stream.

The base `Parser::body(Rx &)` returns `false`: the default implementation does
not consume, validate, or otherwise handle body data and therefore must not
report successful acceptance. Applications which accept bodies must override
the callback explicitly.

This is a breaking contract change. Update dependent code directly; do not add
`void` return probing or compatibility adapters.

## Build discipline

- Use the existing Clang debug build. If the current configuration is not a
  Clang debug build, reconfigure with `./z.config -d -L /usr`.
- Do not clean a valid Clang debug build.
- Build incrementally at meaningful slice boundaries rather than after every
  edit.
- Some intermediate slices may not compile while the return type is being
  propagated; keep each such interval short and restore a buildable tree before
  moving to protocol behavior.

## Phase 1: define the contract and failure classification

1. Update the application-facing contract in `zhttp/src/Zhttp.hh`:
   - make the default `Parser::body(Rx &)` return `false`;
   - document partial consumption, rejection, callback suppression, and the
     eventual `complete(false)` result;
   - update the `ParserAdapter` sketch to show `bool body(Rx &)`;
   - retain the exact `bool` contract without probing for `void`.
2. Add a distinct body-rejection classification rather than reporting
   `RequestErrorCode::Malformed` or an internal transport error. Prefer a
   concise `RequestErrorCode::BodyRejected` value, with the existing generic
   client/server failure accounting mapping it to a body/application failure.
3. Define the server default response mapping for `BodyRejected` explicitly
   (initially the existing generic 400 response is sufficient), while retaining
   the workload's request-error callback for application policy.
4. Audit telemetry so rejection increments application/body failure counters,
   not peer parse-failure counters.

## Phase 2: propagate `bool` through the protocol parsers

1. Change the H1, H2, and H3 CRTP parser defaults to return the result of
   `bodyDrain(rx)`.
2. At every decoded-body prompt, inspect the adapter's return value immediately.
   On `false`:
   - latch `BodyRejected` once;
   - discard retained decoded-body buffers;
   - enter the terminal error/rejected state;
   - prevent subsequent body, trailer, status, header, or successful-completion
     callbacks for that message;
   - invoke the protocol adapter's `complete(State::Error)` exactly once.
3. Keep framing and transport errors distinct from rejection:
   - H1 must not relabel rejection as malformed chunks or malformed framing;
   - H2 should use stream cancellation rather than a connection protocol error;
   - H3 should use request/stream cancellation rather than `MessageError` or
     `InternalError`.
4. Preserve queued-body semantics on `true`: unconsumed application framing may
   remain queued and be presented again after a later decoded-body append.

## Phase 3: implement facade and transport outcomes

1. Make client and server protocol adapters return the application parser's
   `bool` result instead of discarding it.
2. Client response rejection:
   - classify the attempt as a body/application failure;
   - call the response parser's `complete(false)` once;
   - H1: close the connection and do not reuse it;
   - H2/H3: cancel/reset only the response stream and leave sibling streams and
     the physical connection usable.
3. Server request rejection:
   - finalize body accounting, including retained/discarded bytes;
   - call the request parser's `complete(false)` once;
   - route the distinct rejection through `Workload::requestError` so the
     application can choose the response policy;
   - H1: force connection close after any error response. Do not honor a
     `Continue` disposition without an implemented drain-to-message-boundary
     path, because unread body bytes cannot be treated as the next request;
   - H2/H3: stop receiving the affected request stream while preserving the
     connection and unrelated streams; preserve the ability to send the chosen
     error response when the existing stream lifecycle permits it.
4. Ensure reset/reuse clears the rejection latch only when beginning a genuinely
   new message or logical stream.

## Phase 4: migrate all implementations

1. Convert every application, test, utility, integration, and interoperability
   `body(Rx &)` implementation under `zhttp/` to return `bool`.
2. Return `bodyDrain(rx)` or `bodyEach(rx, ...)` directly where applicable.
3. For custom consumers, return `false` on decoder/application failure and
   `true` after a successful or intentionally partial prompt.
4. Update wrappers such as `bodyInput(rx)` so their return types and failure
   propagation remain explicit.
5. Search the full repository for dependent `Zhttp::Parser` implementations and
   propagate the breaking change outside `zhttp` where required. Do not retain
   a detector accepting legacy `void` callbacks.

## Phase 5: focused tests

Add tests before broad regression runs:

1. Common contract tests:
   - the base `Parser::body` returns `false` and rejects a body-bearing message;
   - `bodyDrain` success is propagated;
   - partial consumption followed by `true` remains supported;
   - `false` after consuming none or some of a prompt aborts immediately;
   - no later body/trailer callback occurs;
   - `complete(false)` occurs exactly once.
2. H1 tests:
   - fixed-length and chunked bodies reject correctly;
   - a pipelined request following a rejected body is not parsed;
   - the connection closes after the optional error response;
   - response-body rejection prevents client-side connection reuse.
3. H2 tests:
   - rejection resets/cancels only the affected stream;
   - a sibling stream completes successfully on the same connection;
   - no connection-level protocol error is emitted.
4. H3 tests:
   - rejection uses request cancellation rather than message/internal error;
   - both receive termination and stream cleanup complete once;
   - a sibling stream remains operational.
5. Facade/accounting tests:
   - client outcome is body/application failure rather than protocol failure;
   - server receives the distinct request error and can produce its error
     response;
   - received, consumed, reset, and discarded body byte counts remain correct.

## Phase 6: incremental verification and hardening

1. Run `git diff --check` and targeted builds:
   - `make -C zhttp/src -j`
   - `make -C zhttp/test -j`
   - `make -C zhttp/util -j`
   - `make -C zhttp/itest -j`
   - build `zhttp/interop` targets enabled by the current configuration.
2. Run the focused parser, message, H1, H2, H3, client, and server tests first.
3. Run `make -C zhttp/test test` after the focused tests pass.
4. Repeatedly run the new cancellation/rejection tests, especially multiplexed
   H2/H3 cases, to expose hangs, duplicate completion, retained-buffer leaks,
   and transient stream-lifecycle failures; harden deterministic synchronization
   rather than adding sleeps.
5. Review the final diff against `GUIDELINES.md`, checking callback contracts,
   Rx ownership, error classification, one-shot completion, allocation behavior,
   naming, hard-tab formatting, and absence of legacy return-type probing.

## Completion criteria

- Every application and protocol `body(Rx &)` callback returns `bool`.
- No caller accepts or synthesizes success for a `void` body callback.
- `true` preserves partial-consumption behavior.
- `false` aborts promptly and produces exactly one `complete(false)`.
- H1 is not reused after rejection; H2/H3 preserve unrelated streams.
- Body rejection is distinguishable from malformed HTTP and internal failure in
  error handling and telemetry.
- Incremental Clang debug builds and the full `zhttp` test suite pass, including
  repeated rejection/cancellation stress runs.
