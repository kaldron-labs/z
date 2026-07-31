# `Ztc::Link` queue telemetry implementation specification

## Required result

After this change, every concrete transport link in `ztcp`, `ztls`, and
`zquic` returns two stable, non-null, link-owned `Ztc::Queue` objects:

```c++
Ztc::Queue *rxQueue() const override;
Ztc::Queue *txQueue() const override;
```

The objects live for exactly as long as their owning link.  They are not
allocated, registered independently, reference-counted, locked, or dispatched
to an owner thread.

Every call to `Ztc::Queue::telemetry()` is intentionally an unclean
cross-thread snapshot.  The implementation must:

- read the current fields directly;
- perform no Rx/Tx shard dispatch;
- perform no locking, retry, validation, or consistency loop;
- perform no allocation;
- perform no unbounded traversal;
- tolerate fields being sampled at different logical instants.

Do not attempt to make `count == inCount - outCount`.  `QueueTelemetry`
explicitly permits those values to differ.

## Audit result and scope

The only direct null implementations are:

| File | Class | Null methods |
| --- | --- | --- |
| `ztcp/src/Ztcp.hh` | `Ztcp::Link` | `rxQueue()`, `txQueue()` |
| `ztls/src/Ztls.hh` | `Ztls::Link` | `rxQueue()`, `txQueue()` |
| `zquic/src/ZquicLink.hh` | `Zquic::Link` | `rxQueue()`, `txQueue()` |

There are no direct implementations in `zhttp` or `zws`:

- HTTP/1 TCP links inherit `Ztcp::Link`.
- HTTP/1 TLS links and native HTTP/2 sessions inherit `Ztls::Link`.
- Native HTTP/3 sessions inherit `Zquic::Link`.
- Logical HTTP/2 and HTTP/3 request links are not `Ztc::Link` objects.
- WebSocket HTTP/1 links inherit the relevant HTTP/1 transport link.
- Extended CONNECT WebSockets are logical HTTP/2 or HTTP/3 streams and are not
  `Ztc::Link` objects.

Make no production changes in `zhttp` or `zws`.  In particular:

- do not add `Ztc::Link` to a logical HTTP or WebSocket stream;
- do not return HTTP pending/active arrays as queues;
- do not return `Zws::Codec`'s `Zi::RxLayer` as a queue;
- do not change a native link's telemetry type to H1, H3, or WS.

The implementation consists of:

1. one reusable link-owned queue adapter in `zi/src/ZtcLink.hh`;
2. two embedded adapters and snapshot methods in each transport link;
3. unconditional, always-on telemetry-source counters in every transport;
4. four QUIC diagnostic fields changed from debug-only to always-on storage;
5. focused transport tests and dependent-module rebuilds.

## Telemetry-source counter rule

Apply one rule to all three transport modules:

> Every counter read by `Ztc::Link::telemetry()` or
> `Ztc::Queue::telemetry()` must have unconditional storage and unconditional
> update sites in debug and release builds.

Do not introduce a second counter when an existing always-on counter already
has the required unit and update semantics.

- TCP and TLS wire-call/wire-byte telemetry must continue to read the existing
  `ZiConnection::{rxCalls,rxBytes,txCalls,txBytes}` counters.  Those fields are
  unconditional `uint64_t` members in `ZiConnection`; do not shadow them in
  `Ztcp::Link` or `Ztls::Link`.
- The new TCP/TLS queue-ingress counters specified below must be unconditional
  `ZmAtomic<uint64_t>` members.  Do not place their declarations or updates
  behind `Ztcp_DEBUG`, `Ztls_DEBUG`, `ZiMultiplex_DEBUG`, `NDEBUG`, or any
  logging/qlog conditional.
- QUIC must convert the four existing diagnostic fields used for link/queue
  traffic telemetry to unconditional atomic storage.  Do not add parallel
  queue-only counters.
- HTTP and WebSocket must inherit their native transport counters.  Do not add
  HTTP- or WebSocket-level shadows for the same activity.

The existing `ZiConnection` counters remain plain owner-thread-exclusive
`uint64_t` values.  Reading them uncleanly through an active connection is
intentional and already part of the link telemetry contract.  Do not broaden
this task by changing every `ZiConnection` counter to atomic storage.

## 1. Add `Ztc::LinkQueue`

Add the following class template to `zi/src/ZtcLink.hh` immediately after
`struct Link` and before the namespace closes:

```c++
template <typename Owner_, QueueType::T Type_>
class LinkQueue final : public Queue {
  static_assert(Type_ == QueueType::Rx || Type_ == QueueType::Tx);

public:
  using Owner = Owner_;
  enum { Type = Type_ };

  LinkQueue(Owner *owner) : m_owner{owner} { }

  ZuTuple<ZuID, QueueType::T> telKey() const override {
    auto key = m_owner->telKey();
    return {key.template p<1>(), Type};
  }

  void telemetry(QueueTelemetry &data) const override {
    auto key = m_owner->telKey();
    data = {};
    data.id = key.template p<1>();
    data.type = Type;
    if constexpr (Type == QueueType::Rx)
      m_owner->rxQueueTelemetry_(data);
    else
      m_owner->txQueueTelemetry_(data);
  }

private:
  Owner *const	m_owner;
};
```

Use the exact ownership model above:

- the adapter stores a raw immutable back-pointer;
- the queue ID is the second component of the owner's existing
  `{hubID, linkID}` key;
- `telemetry()` resets the complete output structure before filling it;
- `size` and `full` therefore remain zero unless an owner snapshot method
  explicitly overrides them; none of the three owners will override them;
- no generic callback, `ZmFn`, virtual owner hook, or heap allocation is added.

Each owner keeps `rxQueueTelemetry_()` and `txQueueTelemetry_()` private and
grants access with:

```c++
template <typename, Ztc::QueueType::T>
friend class Ztc::LinkQueue;
```

## 2. Implement `Ztcp::Link`

### Types, construction, and returned pointers

Inside `Ztcp::Link`, add:

```c++
using RxTelQueue =
  Ztc::LinkQueue<Link, Ztc::QueueType::Rx>;
using TxTelQueue =
  Ztc::LinkQueue<Link, Ztc::QueueType::Tx>;
```

Declare the two adapters in the immutable member group, after `m_app`:

```c++
mutable RxTelQueue	m_rxTelQueue;
mutable TxTelQueue	m_txTelQueue;
```

Change the constructor to initialize members in declaration order:

```c++
Link(App *app) :
  m_app{app}, m_rxTelQueue{this}, m_txTelQueue{this} { }
```

Replace the two null methods with:

```c++
Ztc::Queue *rxQueue() const override { return &m_rxTelQueue; }
Ztc::Queue *txQueue() const override { return &m_txTelQueue; }
```

The adapter members are `mutable` only because the existing const virtual
functions return non-const `Ztc::Queue *`.

### Add Tx ingress counters

Add a Tx-thread-exclusive cache-line group after the existing Rx-exclusive
members:

```c++
// Tx thread exclusive
alignas(Zm::CacheLineSize)
ZmAtomic<uint64_t>	m_txInCount = 0;
ZmAtomic<uint64_t>	m_txInBytes = 0;
```

`send_()` is the single writer.  After its existing null/empty/disconnecting
checks, explicitly reject a missing current connection before counting:

```c++
if (ZuUnlikely(!m_cxn)) return;
```

Immediately before `Tx::send(ZuMv(buf))`, snapshot `buf->length` and update:

```c++
m_txInCount.store_(m_txInCount.load_() + 1);
m_txInBytes.store_(m_txInBytes.load_() + buf->length);
```

Do not use `++`, `+=`, `xchAdd()`, or any other atomic RMW.  The storage is
atomic only so a cross-thread scrape obtains an integral-width relaxed load.
The app Tx shard remains the only writer.

The counters are link-lifetime cumulative.  Do not reset them on connect,
disconnect, or reconnect.

The declarations and both updates are unconditional in every build mode.
Do not reuse a debug/logging counter for these fields.

### Exact snapshots

Implement:

```c++
void rxQueueTelemetry_(Ztc::QueueTelemetry &data) const;
void txQueueTelemetry_(Ztc::QueueTelemetry &data) const;
```

`rxQueueTelemetry_()` must:

1. snapshot `m_cxn` once into a local `Cxn *`;
2. when non-null, assign:
   - `data.inCount = cxn->rxCalls()`;
   - `data.inBytes = cxn->rxBytes()`;
3. assign `data.count = m_rxStream.count_()`;
4. leave `outCount`, `outBytes`, `size`, and `full` zero.

`txQueueTelemetry_()` must:

1. assign `data.inCount = m_txInCount.load_()`;
2. assign `data.inBytes = m_txInBytes.load_()`;
3. snapshot `m_cxn` once into a local `Cxn *`;
4. when non-null, assign:
   - `data.outCount = cxn->txCalls()`;
   - `data.outBytes = cxn->txBytes()`;
5. assign `data.count = Tx::txQueue.count_()`;
6. leave `size` and `full` zero.

Do not add Rx egress counters.  Accounting every possible
`ZiRxStream::advance()` and `consume()` removal would be a generic stream
instrumentation change, not a cheap link-local update.

## 3. Implement `Ztls::Link`

### Types, construction, and returned pointers

Add the same aliases, friend declaration, mutable adapter members, and pointer
returns used by `Ztcp::Link`.

Declare the adapters in the immutable member group after `m_app` and
`m_isServer`.  Initialize them after those members:

```c++
Link(App *app, bool isServer) :
  m_app{app}, m_isServer{isServer},
  m_rxTelQueue{this}, m_txTelQueue{this} { }
```

### Add plaintext Rx and serialized-record Tx ingress counters

Add these four counters to the existing Rx-thread-exclusive member group:

```c++
ZmAtomic<uint64_t>	m_rxInCount = 0;
ZmAtomic<uint64_t>	m_rxInBytes = 0;
ZmAtomic<uint64_t>	m_txInCount = 0;
ZmAtomic<uint64_t>	m_txInBytes = 0;
```

All four counters are Rx-shard-written:

- TLS record decryption and plaintext publication run on Rx;
- TLS record protection/finalization also runs on Rx in the current design.

Do not place the Tx ingress counters on another cache line merely because they
describe Tx.  That would separate them from their actual mutating owner.

The counters are link-lifetime cumulative.  Do not clear them in
`reset_tls_()`, on handshake restart, or on reconnect.

The declarations and all four updates are unconditional in every build mode.
Do not derive these values from TLS debug diagnostics or logging.

### Plaintext Rx update point

In `rcvd_()`, within the existing `if (pbuf.off)` block:

1. set `buf->skip` and `buf->length` exactly as today;
2. before moving `buf` into `m_rxStream`, update:

```c++
m_rxInCount.store_(m_rxInCount.load_() + 1);
m_rxInBytes.store_(m_rxInBytes.load_() + buf->length);
```

3. push the buffer as today.

Count one ingress item per non-empty plaintext buffer published to
`m_rxStream`.  Do not count:

- encrypted record bytes;
- handshake records before application data is enabled;
- post-handshake control records that produce no plaintext;
- failed decryptions.

### Serialized Tx update point

In `finalizeTxBuf_()`:

1. retain the existing buffer assertions;
2. return without counting when `pbuf.off == 0`;
3. set `buf->skip` and `buf->length` exactly as today;
4. immediately before `Tx::send(ZuMv(buf))`, update:

```c++
m_txInCount.store_(m_txInCount.load_() + 1);
m_txInBytes.store_(m_txInBytes.load_() + buf->length);
```

This single point counts every non-empty serialized TLS output record,
including handshake, control, alert, and application records.

Use relaxed load/modify/store only.  Do not use atomic RMW.

### Exact snapshots

`rxQueueTelemetry_()` must assign:

- `inCount = m_rxInCount.load_()`;
- `inBytes = m_rxInBytes.load_()`;
- `count = m_rxStream.count_()`.

It must leave `outCount`, `outBytes`, `size`, and `full` zero.

Do not use `ZiConnection::rxBytes()` here.  That counter is encrypted wire
traffic, while this queue contains plaintext.

`txQueueTelemetry_()` must assign:

- `inCount = m_txInCount.load_()`;
- `inBytes = m_txInBytes.load_()`;
- `count = Tx::txQueue.count_()`.

It must snapshot `m_cxn` once.  When non-null, it must also assign:

- `outCount = cxn->txCalls()`;
- `outBytes = cxn->txBytes()`.

The Tx ingress item is a TLS record; the wire egress call may be a partial
write.  The counts are deliberately not required to match.

## 4. Implement `Zquic::Link`

Add the direct dependency to the ordered `Zu` includes in
`zquic/src/Zquic.hh`:

```c++
#include <zlib/ZuUnroll.hh>
```

### Make the existing traffic diagnostics always-on

Do not add duplicate QUIC telemetry counters.

In `zquic/src/ZquicDiag.hh`, change exactly these four fields:

```c++
// LinkRxDiag
ZmAtomic<uint64_t>	datagramsRx = 0;
ZmAtomic<uint64_t>	bytesRx = 0;

// LinkTxDiag
ZmAtomic<uint64_t>	packetsTx = 0;
ZmAtomic<uint64_t>	bytesTx = 0;
```

Leave every other `DiagCounter` unchanged.  These four fields are special
because both `Ztc::LinkTelemetry` and `Ztc::QueueTelemetry` require them in
release builds.

The generated copy construction/assignment of `LinkRxDiag` and `LinkTxDiag`
continues to work because `ZmAtomic` is copyable.  Preserve
`resetRuntimeDiag_()`; assigning `{}` must reset these four counters along with
the other runtime diagnostics.

Replace the four hot-path RMW expressions with owner-thread relaxed
load/modify/store:

In `receiveDatagram_()`:

```c++
m_rxDiag.datagramsRx.store_(m_rxDiag.datagramsRx.load_() + 1);
```

After validating `d.buf`, add its length with:

```c++
m_rxDiag.bytesRx.store_(
  m_rxDiag.bytesRx.load_() + d.buf->length);
```

In `recordProtPktTx_()`, after a packet has been successfully recorded:

```c++
m_txDiag.packetsTx.store_(m_txDiag.packetsTx.load_() + 1);
m_txDiag.bytesTx.store_(m_txDiag.bytesTx.load_() + bytes);
```

Do not move these updates to endpoint completion.  Preserve the current
semantics:

- Rx counts received datagrams presented to the link;
- Tx counts protected QUIC packets successfully entered into recovery
  accounting;
- a later endpoint-send failure may discard the packet from recovery without
  decrementing the cumulative activity totals.

Change direct telemetry reads to explicit relaxed loads:

```c++
data.rxCalls = m_rxDiag.datagramsRx.load_();
data.rxBytes = m_rxDiag.bytesRx.load_();
data.txCalls = m_txDiag.packetsTx.load_();
data.txBytes = m_txDiag.bytesTx.load_();
```

Also change `txPackets_()` to return
`m_txDiag.packetsTx.load_()`.  Other cold diagnostic snapshots may continue to
copy the containing diagnostic structs.

### Types, construction, and returned pointers

Add the same `RxTelQueue`/`TxTelQueue` aliases, friend declaration, mutable
adapter members, and pointer-return implementations used by TCP and TLS.

Declare the adapters in the immutable member group after `m_app` and
`m_isServer`.  Add them to the constructor initializer list before
`m_streams` and `m_closedStreams`, matching declaration order.

### Exact snapshots

`rxQueueTelemetry_()` must assign:

- `inCount = m_rxDiag.datagramsRx.load_()`;
- `inBytes = m_rxDiag.bytesRx.load_()`;
- `count = 0`.

It must leave `outCount`, `outBytes`, `size`, and `full` zero.

Do not calculate an Rx depth.  Retained Rx state is split across crypto packet
spaces and arbitrary live streams.  In particular, do not traverse
`m_streams`, `m_rxCrypto`, stream `rxQueue()` objects, or ACK ranges.

`txQueueTelemetry_()` must assign:

- `outCount = m_txDiag.packetsTx.load_()`;
- `outBytes = m_txDiag.bytesTx.load_()`;
- `count = txQueueCount_()`.

Add this protected helper so the implementation and its test use the same
definition:

```c++
uint64_t txQueueCount_() const {
  uint64_t count = 0;
  ZuUnroll::all<PktNumSpace::N>([this, &count](auto I) {
    count += m_txPkts[I()].count();
  });
  return count;
}
```

`ZuUnroll` must expand the fixed packet-space sequence at compile time; do not
use a runtime `for` loop.  Each expanded operation calls the O(1)
`PktTxSpace::count()` accessor and must not inspect packet nodes.

Leave `inCount`, `inBytes`, `size`, and `full` zero.  Do not combine stream,
crypto, control, retransmission, and packet values: they use different units.
Do not call `queuedControlFrames()`; it traverses all streams and asserts Tx
ownership.

## 5. Required tests

### Common assertions

In each live-link test below, add a local helper that accepts `Ztc::Link &` and
checks:

1. `rxQueue()` and `txQueue()` are non-null;
2. repeated calls return the same pointers;
3. Rx and Tx pointers differ;
4. `rxQueue()->telKey()` equals `{link.telKey().p<1>(), QueueType::Rx}`;
5. `txQueue()->telKey()` equals `{link.telKey().p<1>(), QueueType::Tx}`;
6. queue telemetry resets a deliberately pre-filled `QueueTelemetry`;
7. telemetry `id` and `type` match the queue key;
8. `size == 0` and `full == 0`.

Run the helper directly from a non-owner test thread at least once.  Do not
post the scrape to Rx or Tx.

### TCP

Extend `ztcp/test/ZtcpLoopTest.cc`.

- Check queue identity immediately after constructing the client link.
- In the client `process()` callback, after consuming `Pong` and before
  `disconnect()`, snapshot both queues into fields on `State`.
- After deterministic completion, assert:
  - Rx `inCount >= 1`;
  - Rx `inBytes >= Pong.length()`;
  - Tx `inCount >= 1`;
  - Tx `inBytes >= Ping.length()`;
  - Tx `outCount >= 1`;
  - Tx `outBytes >= Ping.length()`;
  - both queue depths are zero after the ping/pong drain.

Use the existing semaphore completion.  Add no sleeps.

### TLS

Extend `ztls/test/ZtlsBufHookTest.cc`; do not add a new test binary.

- Invoke the common identity helper on both live client and server links.
- Snapshot before link teardown, after the existing payload exchange has
  completed.
- Assert that plaintext Rx ingress bytes equal the application bytes delivered
  for the selected test case.
- Assert Tx ingress is non-zero and includes serialized handshake/application
  records.
- Assert wire Tx egress is non-zero.
- Assert the Rx and Tx depths are zero after the controlled drain.
- Include the existing zero-output/control-record path and verify that it does
  not increment plaintext Rx `inCount`.

Use the test's existing semaphores; add no polling or sleeps.

### QUIC

Extend `zquic/test/ZquicLoopTest.cc`.

- Check queue identity on both established links.
- After deterministic bidirectional traffic and before teardown, assert:
  - Rx `inCount` and `inBytes` are non-zero;
  - Tx `outCount` and `outBytes` are non-zero;
  - the four corresponding `LinkTelemetry` fields equal the queue fields from
    the same immediate snapshot;
  - Rx `count` remains zero;
  - Tx `count` equals a public test-only wrapper in the derived test link that
    returns `Base::txQueueCount_()`.

The QUIC counter assertions must compile and run in both debug and release
builds.  They are the regression check that these four diagnostic fields no
longer compile away.

### Release coverage for every transport

The focused TCP and TLS counter assertions must also run in release builds.
They must demonstrate that:

- TCP retains non-zero wire Rx/Tx counters from `ZiConnection` and non-zero Tx
  queue ingress counters;
- TLS retains non-zero plaintext Rx ingress, serialized-record Tx ingress, and
  wire Tx counters;
- none of those values depends on a debug, logging, or assertion build flag.

### HTTP and WebSocket coverage

Do not add production telemetry code to either module.

- Rebuild and run `zhttp/test/ZhttpLifecycleTest` to instantiate TCP, TLS, and
  QUIC native link paths with the changed bases.
- Rebuild and run `zhttp/test/ZhttpH2EngineTest` and
  `zhttp/test/ZhttpH3EngineTest` to cover native session inheritance.
- Rebuild and run `zws/test/ZwsH1EngineTest` and `ZwsH2EngineTest`.
- Do not add queue assertions to logical H2/H3/extended-CONNECT links because
  those objects are intentionally not `Ztc::Link`.

## 6. Build and validation sequence

For an incremental debug build:

```sh
make -C zi/src -j8
make -C ztcp/src -j8
make -C ztcp/test -j8
make -C ztls/src -j8
make -C ztls/test -j8
make -C zquic/src -j8
make -C zquic/test -j8
make -C zhttp/src -j8
make -C zhttp/test -j8
make -C zws/src -j8
make -C zws/test -j8
```

Run:

```sh
./ztcp/test/ZtcpLoopTest
./ztls/test/ZtlsBufHookTest
./zquic/test/ZquicLoopTest
./zhttp/test/ZhttpLifecycleTest
./zhttp/test/ZhttpH2EngineTest
./zhttp/test/ZhttpH3EngineTest
./zws/test/ZwsH1EngineTest
./zws/test/ZwsH2EngineTest
make test
```

Release validation must use the repository-wide sequence required by
`GUIDELINES.md`:

```sh
./z.config /opt/z
make clean
make -j8
```

Then rerun the focused transport and dependent-module tests above.  Do not
validate release behavior with a partial rebuild.

## Acceptance checklist

- No requested transport `Ztc::Link` returns `nullptr` for Rx or Tx.
- Each queue pointer is stable, distinct by direction, and owned by the link.
- Queue keys use the link ID and `QueueType::Rx`/`Tx`.
- Every snapshot initializes every `QueueTelemetry` field.
- TCP/TLS added counters use single-writer relaxed load/modify/store.
- Every TCP/TLS counter used by telemetry exists and is updated in release
  builds; existing `ZiConnection` counters are reused rather than shadowed.
- The four QUIC traffic diagnostic counters exist in release builds and use
  single-writer relaxed load/modify/store.
- No atomic RMW is added on a telemetry hot path.
- No queue snapshot locks, posts, waits, allocates, or performs an unbounded
  traversal.
- Plaintext, encrypted-record, wire-call, datagram, and packet units are not
  conflated.
- Unknown fields remain zero.
- HTTP and WebSocket logical streams remain outside native link telemetry.
