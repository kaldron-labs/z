# Zquic Goal

## Objective

Add a new `Zquic` library in `zquic`, `zquic/src`, `zquic/test`, etc. that
provides QUIC connections and streams using the
same application-facing style as `Ztls`: receive data through `ZiRxStream` and
transmit data through `ZiTxStream`.

The implementation must preserve buffer zero-copy below that API boundary.

## Firm Guidelines

- `../zngtcp2` is a reference implementation of QUIC, but is not depended on
  in any way, it is to be referred to exclusively for the purposes of guiding
  implementation logic
- `Zquic` layers on `Ztls` and `../zpicotls` (installed); it can directly call `zpicotls` for
  packet encrypt/decrypt
- `Zquic` independently implements QUIC state, packet number spaces, loss
  recovery, flow control, stream state, timers, and packetization making
  maximum use of `Zu`/`Zm`/`Zt`/`Ze`/`Zi`/`Ztls`
- STL is not to be used except when it is mandatory because there is no `Z` equivalent, and all such usages must be flagged
- like `Ztls`
  - the `zquic` directory structure, build system (autoconf/automake) and test frameworks should align
  - naming conventions should align, using the `Zquic` prefix
  - `ZquicLib.hh`, etc.
  - `Zquic` should make particular use of `ZiMultiplex` and underlying `ZmScheduler`, `ZiIOBuf`, `ZiRx` and `ZiTx`
  - rx and tx should be decoupled as far as possible and handled by dedicated threads,
    each with their own exclusive data (queues, etc.); this is not "shared-nothing" but it is "shared-minimum"
  - data shared by both rx and tx should be the minimum required, and that data protected with `ZmPLock` or `ZmAtomic` 
- unlike `Ztls`:
  - packet buffers and stream buffers are distinct `ZiIOBufAlloc` heaps, but compile-time built-in size is the same (1472)
  - QUIC control frames can piggyback with stream data in packets, but only if the MTU permits
  - encryption is not in-place: plaintext stream tx buffers are retained for potential resend, so UDP tx packets are built by encrypting from the plaintext to the ciphertext
    - this permits use of "non-temporal" cipher variants, as with `zngtcp2`
  - unlike `Ztls`, QUIC comprises multiple concurrent streams:
    - where `txStream()` and `process(rxStream)` (CRTP callback) are members of `Link`, these now need to be members of a new `Stream` CRTP class
    - `Stream`'s `Impl` needs to be a template parameter to `Link`
    - `Link` must provide a `ZmRef<Stream> stream(...)` for the local app to create a new stream to be informed to the peer
    - `Link`'s `Impl` must provide a `streamed(ZmRef<Stream>)` CRTP callback for the local app to handle a new stream created by the peer
    - `txStream()` needs to be a member of `Stream`
    - `process(rxStream)` needs to be a member of `Stream`'s `Impl`
    - `Link::m_streams` is exclusive to the rx thread (finding a stream from its ID is only needed for rx - the app will retain the stream ref for tx)
      - when streams are added, a job is enqueued to the rx thread to add that stream to `m_streams`
      - when streams are removed, a job is enqueued to the rx thread to remove that stream from `m_streams`

- skeleton code:
```
template <
  typename Impl, typename RxBufAlloc_, typename TxBufAlloc_>
class Stream ... {
  ...
  Stream(...) ... m_id(id) ... { ... }
  ...
  int64_t id() const { return m_id; }
  ...
  auto txStream();
  ...
private:
  int64_t   m_id;
};

template <typename Stream_>
inline int64_t Stream_IDAxor(const Stream_ &s) { return s.id(); }
template <typename Stream_>
ZuDerive(Streams_,
  (ZmHash<Stream_,
    ZmHashNode<Stream_,
      ZmHashKey<Stream_IDAxor<Stream_>,
        ZmHashHeapID<"Zquic.Stream">>>>));

template <
  typename App, typename Impl, typename RxBufAlloc_, typename TxBufAlloc_,
  typename Cxn_, typename CxnRef_, typename Stream_>
class Link ... {
  ...
  using Stream = Stream_;
  using Streams = Streams_<Stream>;
  ...
  StreamRef stream(...);
  ...
private:
  ...
  Streams   m_streams; // rx thread dedicated, used to dispatch received data
};
```

## High-level Architecture

```text
application
  -> Zquic::Stream
       ZiRxStream for application receive bytes
       ZiTxStream for application transmit bytes
  -> Zquic::Link
       stream scheduler
       packet buffer ownership
       ngtcp2_conn lifecycle
       timer and UDP path management
       QUIC state machine
       packet writer/reader
       congestion control and recovery
       stream and connection flow control
  -> Zi UDP transport
```

`ZiRxStream` and `ZiTxStream` are the application contract. Below that contract,
`Zquic` owns packet buffers, stream queues, retransmission retention and callback glue.

## Buffer Model

### Transmit

- Application writes stream bytes into `ZiIOBuf` instances through a
  `ZiTxStream` facade.
- `Zquic` retains those `ZiIOBuf` instances until the encoded
  stream byte range is acknowledged, reset, or otherwise no longer needed.
- The QUIC packet destination buffer is a `ZiIOBuf` datagram buffer owned by
  `Zquic`.

### Receive

- UDP receives must land directly in `ZiIOBuf` datagram buffers owned by
  `Zquic`.
- Each application data stream frame is decrypted out to a separate stream buffer
  that is then passed to the app via the `Stream`'s CRTP `process(rxStream)` callback
