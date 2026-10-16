# Ztls Tx-Owned Record Protection Plan

## Objective

Remove the application-data Tx -> Rx scheduler hop from `Ztls::Link`.
After the handshake, the Rx shard continues to own the TLS handshake and
receive state, while the Tx shard exclusively owns all outbound record
protection state and performs record encryption directly.

The split must preserve:

- TLS record ordering and sequence numbers;
- TLS 1.3 automatic, locally requested, and peer-requested KeyUpdate;
- TLS close-alert ordering;
- TLS 1.2 explicit-record-IV state, even though the normal Ztls configuration
  currently negotiates TLS 1.3;
- the existing in-place `ZiIOBuf` record path and allocator hooks;
- reconnect generation checks and graceful teardown;
- existing zpicotls behavior for users that do not detach Tx state.

H2/H3 queue admission and `ZiTxStream` error propagation are separate work and
are not part of this change.

## Design decision

Add an opaque, directional `ptls_tx_t` record-protection object to zpicotls.
At successful handshake completion, `ptls_detach_tx` transfers the established
outbound traffic-protection state from `ptls_t` into a new `ptls_tx_t`.
`Ztls::Link` posts that object to its Tx shard and does not expose raw traffic
keys, construct TLS records, or implement the KeyUpdate key schedule itself.

This is preferable to the alternatives:

- Do not copy key/IV/sequence into Ztls and reimplement TLS record construction.
  That would put TLS 1.2 nonce handling, TLS 1.3 inner content types, record
  limits, KeyUpdate derivation, and alerts in the transport wrapper.
- Do not export/import or clone the complete `ptls_t`. That duplicates inbound
  secrets and handshake/session state on the Tx shard and obscures ownership.
- Do not keep two live outbound states synchronized by posting sequence numbers
  back to Rx. Cross-shard ordering would be fragile and both shards would still
  mutate logically identical state.
- Do not keep application encryption on Rx for exceptional records. KeyUpdate,
  alerts, and application records share one outbound sequence and must have one
  owner.

The directional object keeps TLS protocol concerns in zpicotls and threading,
buffer ownership, scheduling, and connection lifecycle in Ztls.

## zpicotls API

Declare the following opaque type and functions in
`zpicotls/include/zpicotls.h`:

```c
typedef struct st_ptls_tx_t ptls_tx_t;

int ptls_detach_tx(ptls_t *tls, ptls_tx_t **tx);
void ptls_tx_free(ptls_tx_t *tx);

int ptls_tx_send(ptls_tx_t *tx, ptls_buffer_t *sendbuf,
                 const void *input, size_t inlen);
int ptls_tx_send_alert(ptls_tx_t *tx, ptls_buffer_t *sendbuf,
                       uint8_t level, uint8_t description);
int ptls_tx_update_key(ptls_tx_t *tx, ptls_buffer_t *sendbuf,
                       int request_update);

ptls_cipher_suite_t *ptls_tx_get_cipher(ptls_tx_t *tx);
uint16_t ptls_tx_get_protocol_version(ptls_tx_t *tx);
size_t ptls_tx_get_record_overhead(ptls_tx_t *tx);

int ptls_take_key_update_request(ptls_t *tls);
```

Exact API semantics:

- `ptls_detach_tx` is valid once the handshake is in either client or server
  post-handshake state and one-RTT outbound protection is installed.
- Detach returns `PTLS_ERROR_NOT_AVAILABLE` when the context uses
  `update_traffic_key`; that mode already delegates traffic protection to an
  external record layer (as QUIC does), so there is no internal record state to
  transfer.
- It moves, rather than copies, `traffic_protection.enc`, including its AEAD
  context, traffic secret, sequence, epoch, TLS 1.2 mode, and explicit record-IV
  counter. The source is marked Tx-detached and cannot subsequently call
  `ptls_send`, `ptls_send_alert`, `ptls_update_key`, or detach again; those calls
  return `PTLS_ERROR_NOT_AVAILABLE` rather than asserting.
- Detach leaves receive protection, the receive key schedule, handshake receive
  buffering, ticket handling, and peer alert processing in the original
  `ptls_t`.
- `ptls_tx_send` has the post-handshake behavior of `ptls_send`, including the
  confidentiality-limit check and automatic TLS 1.3 KeyUpdate before the next
  application record.
- `ptls_tx_update_key` immediately emits the KeyUpdate record under the current
  key, advances outbound protection, and returns with the new key active. It is
  a no-op error for TLS 1.2. Combining emission and advancement prevents a
  caller from interleaving application data between the two operations.
- `ptls_tx_send_alert` uses the same outbound protection and sequence as data
  and KeyUpdate records.
- `ptls_take_key_update_request` returns `1` once for each successfully received
  TLS 1.3 KeyUpdate carrying `update_requested`, then clears that notification;
  it returns `0` otherwise. A KeyUpdate with `update_not_requested` updates only
  receive protection and does not create a notification.
- When Tx has not been detached, peer-requested KeyUpdate retains the existing
  behavior (`needs_key_update` on the original `ptls_t`). When detached, it
  records the notification instead and never mutates nonexistent outbound
  state.

Do not return traffic secrets through the public API. `ptls_tx_t` is the
ownership boundary.

## zpicotls internals

Implement the directional object in `zpicotls/lib/picotls.c` adjacent to
`st_ptls_traffic_protection_t` and the record helpers.

`ptls_tx_t` should contain only:

- the moved outbound traffic-protection structure;
- the negotiated cipher suite/hash references needed for traffic-secret update;
- TLS version/mode and the pending automatic-update flags;
- immutable connection metadata required by existing secret logging;
- a Tx-owned logging identity, if needed to preserve `log_event` callbacks
  without passing the Rx-owned `ptls_t` across threads.

Store peer-request notifications as a bounded integer count, not a lossy bit.
If a peer can drive the count beyond its representation before the application
takes it, fail the connection with a TLS protocol/internal error rather than
wrapping or silently dropping a required response.

Refactor existing private helpers rather than duplicating record code:

1. Make record encryption, record overhead, and alert encryption operate on an
   outbound traffic-protection argument.
2. Factor traffic-secret advancement so both the legacy `ptls_t` path and
   `ptls_tx_t` use the same HKDF `traffic upd` implementation.
3. Factor KeyUpdate record emission around that directional state.
4. Keep `ptls_send`, `ptls_send_alert`, and `ptls_update_key` as compatibility
   entry points over the embedded outbound state when it has not been detached.
5. Zero the moved source structure immediately so `ptls_free(tls)` cannot free
   the AEAD context twice.
6. Clear traffic secrets and dispose the AEAD context in `ptls_tx_free` using
   the same secure-clear conventions as `ptls_free`.

Preserve secret logging on the Tx caller's thread. Do not retain a pointer to
the live Rx-owned `ptls_t` merely to satisfy the old `log_event` signature.
Instead, give `ptls_tx_t` an immutable, Tx-owned identity snapshot sufficient
for the getters used during secret logging (at minimum client random, role,
cipher, protocol version, data pointer, SNI, and negotiated protocol), or
factor an internal common logging view. Document that post-detach outbound
secret events execute on the thread calling `ptls_tx_*`, as normal callback
execution follows the calling thread.

Do not alter QUIC's `update_traffic_key` callback mode. QUIC does not use the
TLS record layer and must continue through its existing path unchanged.

## Ztls handshake and installation

Change `ztls/src/Ztls.hh` as follows:

1. Replace Rx-owned `m_txSeqEst` and `m_txControlPending` with a Tx-exclusive
   `ptls_tx_t *m_txTLS`, beginning on its own cache line with any other
   Tx-exclusive state.
2. Keep `m_tls` strictly Rx-exclusive.
3. In `finishHandshake_`, calculate and validate record headroom/overhead before
   detach, then call `ptls_detach_tx`.
4. Post the resulting object and current `m_tlsGen` to the configured Ztls Tx
   shard. Installation rejects and frees stale generations.
5. Post an acknowledgement back to Rx after successful installation. Only then
   set the link to `Up` and invoke `impl()->connected(...)`.

The Tx acknowledgement is intentional. Relying on incidental FIFO ordering
between the installation post and application callbacks would make the first
send race-sensitive. Until acknowledgement, the TLS parser is post-handshake
but the public link remains `Connecting`; received records may still be
processed on Rx.

If detach, Tx installation, or acknowledgement fails, report a Ztls error and
disconnect without publishing `connected`.

## Ztls application transmission

Change the application transmit path to:

```text
application ZiTxStream
  -> Ztls Tx scheduler
  -> ptls_tx_send (in-place TLS record protection)
  -> ZiTx::send
  -> ZiMultiplex Tx scheduler/socket
```

Specifically:

- `TxStream_::sendBuf_` and `send()` retain their current application-to-Tx
  dispatch and generation capture.
- `send_` validates the buffer, generation, connection state, headroom, and
  capacity on the Tx shard, then calls `ptls_tx_send` directly.
- Remove `sendRx_` and the `app()->rxRun` hop.
- Generalize the current `txBuf`, alignment, assertion, and finalization helpers
  so handshake output uses Rx-owned `m_tls`, while application/control output
  uses Tx-owned `m_txTLS`. Buffer allocation and in-place publication remain
  unchanged.
- Encryption errors are reported through the existing app error channel by
  posting a fixed exception snapshot to Rx/control ownership as required; the
  Tx shard must not call Rx-exclusive disconnect internals directly.

Do not add a mutex or atomic access to `m_txTLS`. Every access, replacement,
and destruction occurs on the configured Tx shard.

## KeyUpdate flow

TLS 1.3 KeyUpdate is not an acknowledgement protocol in the general case. The
sender encrypts the KeyUpdate record with its current write key and switches to
the next write key immediately after that record. All following records use the
new key. If the message carries `update_requested`, the receiver must send a
reciprocal KeyUpdate before sending further application data; that reciprocal
message is the only acknowledgement-like exchange.

The ordering boundary is the Tx workstream and its ordered `ZiTx` output queue,
not application callback time, socket completion, or peer acknowledgement.
Work dequeued before the KeyUpdate task uses the old key and is queued before
the KeyUpdate record. The KeyUpdate task emits its record with the old key,
commits that ciphertext to `ZiTx`, then installs the new key. Work dequeued
after that task uses the new key.

### Locally initiated or automatic

- `updateKey_(requestUpdate)` becomes a thin generation-tagged post into the Tx
  workstream. Callers that enqueue subsequent application work through the same
  workstream naturally place it after the update task.
- Tx calls `ptls_tx_update_key`, sends the resulting record through `ZiTx`, and
  retains the advanced protection state.
- The automatic confidentiality limit remains inside `ptls_tx_send`, so Ztls
  does not duplicate zpicotls policy. `ptls_tx_send` emits the update and
  advances the key before encrypting the application input passed to that call.

### Peer requested

- After each successful post-handshake `ptls_receive`, Rx calls
  `ptls_take_key_update_request`.
- If set, Rx posts a generation-tagged response task into the Tx workstream.
- When dequeued, that task calls `ptls_tx_update_key(..., false)` and queues the
  response before advancing the Tx-owned write key.

No pending-response drain or shared key-update flag is needed. Concurrent
producers are linearized when their work is enqueued to the Tx scheduler. Any
application task already ahead of the response task is encrypted with the old
key and enters `ZiTx` before the KeyUpdate. The response task then emits the
KeyUpdate with that old key, commits it to `ZiTx`, advances protection, and
returns. Every later task observes only the new Tx-owned state. The update task
must not advance the key before the KeyUpdate buffer has been accepted by
`ZiTx`, and it must never return successfully while retaining the old key.

## Alerts, disconnect, and teardown

Graceful close must be serialized on Tx:

1. Rx decides to close and captures the connection reference and generation.
2. It posts a Tx task that emits `close_notify` with `ptls_tx_send_alert`.
3. That same Tx task queues the encrypted alert through `ZiTx` and only then
   schedules `ZiConnection::disconnect` on `ZiMultiplex`.
4. Abrupt/error close skips alert generation and closes as today.

On socket disconnection, retain the existing Rx -> Tx -> Rx drain sequence but
free `m_txTLS` during its Tx stage before the final Rx reset/callback. Reconnect
increments `m_tlsGen`; stale install, send, update, alert, and cleanup tasks
must free their captured resources without touching the new generation.

`Link` destruction and hub finalization must assert or deterministically drain
both TLS halves. No Tx object may be freed from Rx as a fallback.

## zpicotls tests

Extend `zpicotls/t/picotls.c` with directional-detach tests covering:

- TLS 1.3 client and server detach followed by bidirectional application data;
- continued receive processing on the original `ptls_t` after detach;
- rejection of legacy send/update/alert APIs on the detached source;
- automatic outbound update at a test-injectable near-limit sequence;
- locally requested KeyUpdate, including request and no-request variants;
- peer-request notification and exactly one response;
- alert encryption after application records;
- TLS 1.2 AES-GCM explicit IV and ChaCha20-Poly1305 implicit nonce;
- detach/free in both orders, with no double free or secret leak;
- allocator failure during detach;
- buffer direction, origin, alignment, and in-place encryption invariants;
- unchanged QUIC `update_traffic_key` callback behavior.

Run clang-format only on touched zpicotls C/header regions, then build in a
separate build directory and run `make check`.

## Ztls tests

Extend `ztls/test/ZtlsBufHookTest.cc` and, if separation improves diagnostics,
add a focused Tx-sharding test binary. Cover:

- application `ptls_send` equivalent executes on the configured Tx shard and
  no application send posts to Rx;
- the first application write cannot run before Tx-state installation;
- H1 and H2 payload round trips with distinct Rx/Tx scheduler IDs;
- client- and server-initiated KeyUpdate followed by data in both directions;
- peer-requested KeyUpdate response ordering relative to queued application
  records;
- graceful `close_notify` after queued data;
- abrupt disconnect without alert;
- reconnect while stale Tx work exists;
- allocation/encryption failures and generation rejection;
- existing nested `ZiTxLayer` headroom/tailroom and in-place buffer-hook tests;
- TLS queue telemetry and complete cleanup after shutdown.

Tests must synchronize with callbacks/blocks, not timing sleeps. Add explicit
thread-ID assertions at the encryption hook boundary so a regression back to
Rx execution cannot pass merely because the payload round trip succeeds.

## Delivery sequence

1. Implement and test `ptls_tx_t` entirely inside zpicotls while preserving all
   legacy APIs.
2. Install that zpicotls build and update Ztls to consume the new API.
3. Convert application encryption and KeyUpdate.
4. Convert alerts and teardown only after data transmission is stable.
5. Run zpicotls `make check`.
6. Reconfigure the Z tree against the installed dependency if required, then
   run a top-level `make -j8` before module tests.
7. Run the Ztls tests and the dependent Zhttp H1/H2 tests.
8. Perform a clean release rebuild through `z.config`, followed by top-level
   `make clean && make -j8`, because this changes an installed dependency and a
   hot cross-module header.

## Acceptance criteria

- No application-data path in Ztls posts from Tx to Rx.
- Rx never reads or mutates detached outbound record state.
- Tx never reads or mutates the live Rx `ptls_t`.
- Data, KeyUpdate, and alerts use one Tx-exclusive sequence and queue order.
- Ztls contains no TLS record encoding or traffic-secret derivation.
- TLS 1.2 and TLS 1.3 record behavior remain covered.
- QUIC TLS behavior and zpicotls callers that do not detach Tx are unchanged.
- Shutdown and reconnect leave no AEAD context, secret, buffer, or queued task
  behind.
