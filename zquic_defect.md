# Deferred H2 defect: connection WINDOW_UPDATE overflow loses GOAWAY

Recorded 2026-11-06. This is an accepted baseline exception for the current
QUIC repair, by explicit user decision. The defect remains unrepaired; this
record proposes a separate H2 repair and does not authorize expanding the
current source changes into H2.

## Observed failure and scope

Failing local HTTP/2 interoperability runs pass 145 of 146 h2spec cases. The
failure is HTTP/2 section 6.9.1, case 2: multiple WINDOW_UPDATE frames increase
the connection flow-control window above `2^31 - 1`.

The peer expects a GOAWAY frame with `FLOW_CONTROL_ERROR` (`0x3`). Instead, it
observes the connection closing before receiving that frame. The corresponding
stream-window overflow case, section 6.9.1 case 3, passes.

WINDOW_UPDATE grants the receiving endpoint additional permission to send
DATA. Stream ID zero updates the connection-wide send window. Each increment
can be valid individually while the accumulated window exceeds its maximum.
The connection must detect that overflow without overflowing its arithmetic,
report the connection error with GOAWAY, and then close. Closing the transport
alone loses the protocol error explanation. A stream-specific overflow has a
different scope and must retain its stream error handling.

This is an error-signaling/teardown defect observed with invalid peer input.
The inspected code detects the excessive window before adding the increment;
the evidence does not show it accepting excess credit or an arithmetic wrap.

## Baseline evidence

The same failure occurs in both the repaired working tree and an independent
build of pre-repair revision `bc710942eefda5cc5572db282c84927abc460976`.
Both use native x86-64 Clang 21.1.8 debug builds and h2spec 2.6.0
(`70ac2294010887f48b18e2d64f5cccd48421fad1`). H2 implementation source was
unchanged by this QUIC repair.

Retained evidence under `/tmp/zquic-repair-phase3-20261106`:

- Repaired serial run: `interop-test.log` and
  `logs/zhttp-h2-interop.5dxQ1EF0/`.
- Baseline run: `baseline-http-interop.log` and
  `logs/zhttp-h2-interop.uG4iU6e8/`, especially `h2spec.out`, `h2spec.err`,
  `tools.txt`, and `zhttpd.err`.

An earlier run also had HTTP matrix port collisions because integration and
interop ran concurrently. Serial execution cleared those other failures;
the single h2spec conformance failure persisted and reproduced on the baseline.
The accepted exception is this exact case, not arbitrary H2 failures.

Reproduction uses the built default targets followed by
`make -C zhttp/interop test`, including `zhttph2interoptest.sh`, with h2spec
installed. Run separately from the HTTP integration matrix to avoid its shared
fixture port. The shell fixture defaults to TLS port 18443 and invokes h2spec
against localhost with TLS and certificate verification disabled for the test
certificate. Preserve its residue directory on failure. The focused h2spec
selector is `http2/6.9.1/2`; use the fixture's running server and connection
options when narrowing the run.

## Inspected implementation and likely cause

The owning component is `H2_::Wire` in `zhttp/src/ZhttpH2Hub.hh`:

1. `applyWindowUpdateTx_(0, value)` checks
   `m_txWindow > int64_t(MaxWindow) - value`. On overflow it sets
   `m_windowPhase = WindowPhase::Failed` and calls `txError_(0)`.
2. `txError_(0)` posts to Rx and calls `h2Error(Error::FlowControlError)`.
3. `h2Error()` sets `m_errorSent`, calls `stopWire()`, calls
   `sendGoaway_(error)`, and immediately calls `impl_()->disconnect_()`.
4. `sendGoaway_()` constructs a `ControlTx<true>`. Its buffer admission posts
   `queueControlTx_()` to Tx; the frame subsequently goes through the bounded
   frame-drain scheduler and native transmit stream.

GOAWAY admission is asynchronous. A successful `ControlTx::flush()` means
the buffer was admitted for processing, not that the peer received it or that
the native transmit queue drained. Immediate disconnect can therefore overtake
GOAWAY admission, scheduled emission, or native transmission.

Importantly, `stopWire()` itself only marks Rx stopping and cancels the settings
timer. `ControlTx::sendBuf_()` does not reject on `m_stopping`.
`queueControlTx_()` rejects on `m_txStopping`, and `clearFramesTx_()` sets that
flag and discards queued controls during cleanup. The generic `sendFrame()`
Rx stopping check is not the control-frame path used here. Also,
`WindowPhase::Failed` does not by itself prevent control frames from draining.

The wire failure and the immediate-disconnect ordering are established by the
logs and source inspection. The precise point where this execution loses
GOAWAY has not been instrumented or confirmed with a packet trace. Treat the
disconnect/transmit race as the repair hypothesis to verify, rather than a
proven attribution to a particular queue rejection.

## Suggested repair

Make connection-error shutdown an ordered completion chain owned by H2:

1. Mark the connection as failing once, stop new application work, and preserve
   the detected error and last processed peer stream ID.
2. Admit the terminal GOAWAY through the existing Tx-owned control path while
   keeping that path and the native transport alive. Preserve HTTP/2 header
   block/CONTINUATION ordering and existing bounded scheduler work.
3. After the terminal frame reaches the native transmit stream, use its existing
   completion/end-of-stream mechanism to wait for pending native output before
   posting disconnect to Rx. The server's `streamTxEnd_()`/`sent()`/
   `streamTxClose_()` pattern in `ZhttpServerImpl.hh` is a candidate mechanism
   to inspect and reuse; confirm the matching client contract before applying
   it to both directions. A barrier after control admission alone is insufficient
   because frame draining and socket output can still be pending.
4. If GOAWAY admission or transmission fails, use the existing failure cleanup
   and terminate once. Retain correct admission accounting and lifetime until
   outstanding continuations complete. Do not add polling, sleeps, global
   queue scans, or a new hot-path synchronization mechanism.

Simply swapping `stopWire()` and `sendGoaway_()` leaves the asynchronous
disconnect race unresolved. Similarly, suppressing overflow detection or
turning this connection error into a stream reset would violate the intended
contract. Keep the currently passing stream-overflow behavior unchanged.

## Required verification for a future repair

- Add a deterministic regression that overflows the connection window and
  observes a complete GOAWAY with stream ID zero, `FLOW_CONTROL_ERROR`, and
  the correct last processed stream ID before transport close.
- Hold Tx/control/native output completion in the fixture so the regression
  proves disconnect waits for completion rather than scheduler timing. Cover
  failed admission/transmission and duplicate error notification without
  introducing polling into the test.
- Retain the passing stream-window overflow check and verify error teardown
  during queued output, including an admitted header continuation chain.
- Rerun the focused h2spec case, all 146 cases, the serial H2 interoperability
  matrix, and affected HTTP unit/integration suites under the existing debug
  configuration. Require 146/146 rather than counting the accepted exception
  as a pass.

No H2 source repair or new H2 regression has been made for this record.


## Subsequent observation

A later serial interop run after the endpoint-only DNS resolver lifecycle repair
passed h2spec (interop-r4.log / interop-test.log at that time). No H2 source
changed. This shows the conformance failure is intermittent; it does not prove
a repair. Earlier failures on both the repaired branch and independent baseline
remain valid evidence. Preserve the completion-order hypothesis and require a
deterministic delayed-output regression before treating the H2 defect as fixed.
