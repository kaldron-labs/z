//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZiMultiplex.hh>
#include <zlib/ZiRxStream.hh>

#include <zlib/ZtlsBuf.hh>
#include <zlib/ZtlsCredentials.hh>
#include <zlib/ZtlsEngineOpenSSL.hh>
#include <zlib/ZtlsEngineOpenSSL.hh>

#include "ZtlsTestHandshake.hh"

using namespace ZuTestUtil;

// One fixed destination with observable head/tail room and final release.
struct PlainPool : Zi::IOBufAllocator {
  enum { Head = 17, Tail = 11, Extent = Ztls_RxRecordCapacity };
  alignas(64)
  uint8_t	bytes[Head + Extent + Tail];
  unsigned	acquired = 0, released = 0;
  bool		live = false, intact = true, throws = false;
  unsigned	geometry = Extent;
  PlainPool() : Zi::IOBufAllocator{nullptr, Zi::IOBufType::RxTLS, sizeof(bytes), Head, Tail}
  { memset(bytes, 0xa5, sizeof(bytes)); }
  uint8_t *alloc(unsigned) const override { return nullptr; }
  void free(uint8_t *ptr, unsigned, bool registered) const override
  {
    auto self = const_cast<PlainPool *>(this);
    self->intact &= ptr == bytes && !registered && live;
    for (unsigned i = 0; i < Head; ++i) self->intact &= bytes[i] == 0xa5;
    for (unsigned i = Head + Extent; i < sizeof(bytes); ++i) self->intact &= bytes[i] == 0xa5;
    self->live = false;
    ++self->released;
  }
  ZmRef<ZiIOBuf> allocBuf(void *owner, uint32_t length, uint32_t) override
  {
    if (throws) throw 1;
    if (live || length) return nullptr;
    live = true;
    ++acquired;
    // clang-format off
    auto buf = ZmRef<ZiIOBuf>{new Zi::IOBufStub<ZiRxQueue::Node>{
      bytes, Head + geometry + Tail, this, owner, 0, false, false}};
    // clang-format on
    buf->skip = Head;
    return buf;
  }
};

void sinkGeometry()
{
  ZuTestScope(sinkGeometry);
  PlainPool	      pool;
  Ztls::PlaintextSink output{&pool, &pool};
  void		     *base = nullptr;
  ZuCheck(!output.reserve(PlainPool::Extent, &base));
  ZuCheck(base == pool.bytes + PlainPool::Head);
  memset(base, 0x39, PlainPool::Extent);
  ZuCheck(!output.take() && pool.acquired == 1 && !pool.released);
  ZuCheck(!output.commit(Ztls_RxAppCapacity));
  auto buf = output.take();
  ZuCheck(buf && buf->owner == &pool && buf->allocator() == &pool);
  ZuCheck(buf && buf->length == Ztls_RxAppCapacity && !buf->registered());
  ZuCheck(!output.take() && pool.live);
  bool paddingCleared = true;
  for (unsigned i = Ztls_RxAppCapacity; i < PlainPool::Extent; ++i)
    paddingCleared &= pool.bytes[PlainPool::Head + i] == 0;
  ZuCheck(paddingCleared);
  buf = nullptr;
  ZuCheck(!pool.live && pool.intact && pool.released == pool.acquired);
  ZuCheck(!output.reserve(37, &base));
  memset(base, 0x91, 37);
  output.discard();
  bool discardedCleared = true;
  for (unsigned i = 0; i < 37; ++i) discardedCleared &= pool.bytes[PlainPool::Head + i] == 0;
  ZuCheck(discardedCleared);
  ZuCheck(!output.take() && !pool.live && pool.intact);
  pool.geometry = PlainPool::Extent - 1;
  ZuCheck(output.reserve(37, &base) == Ztls_ErrorLibrary);
  ZuCheck(!pool.live && pool.acquired == pool.released);
  pool.throws = true;
  ZuCheck(output.reserve(37, &base) == Ztls_ErrorNoMemory);
}

// Checks exact record boundaries and zero-copy receive behavior.
static void exactRecord(unsigned length, bool corruptHeader = false)
{
  ZuTestScopeRT(exactRecord);
  auto		 cipher = &Ztls_::opensslAes128gcmsha256;
  Ztls_::Context ctx{};
  ctx.random = &Ztls_::opensslRandom;
  ctx.getTime = &Ztls_::getTime;
  ctx.keyExchanges = Ztls_::opensslKeyExchanges;
  ctx.cipherSuites.push(cipher);
  ZtlsTest::useTestPSK(ctx, cipher);
  Ztls::Cxn txConnection{&ctx, false}, rxConnection{&ctx, true};
  auto	    tx = &txConnection, rx = &rxConnection;
  bool	    ready = ZtlsTest::handshake(tx, rx);
  ZuCheckRT(ready);
  if (!ready) return;

  uint8_t payload[Ztls_RxAppCapacity];
  uint8_t wire[Ztls_RxAppCapacity + 5 + 1 + Ztls_MaxDigestSize + 1];
  memset(payload, 0x37, length);
  Ztls_::Buffer send, receive;
  Ztls_::bufferInitTx(&send, wire, sizeof(wire) - 1);
  ZuGuard releaseSend{[&send]() { Ztls_::bufferDispose(&send); }};
  int	  sent = Ztls_::send(tx, &send, payload, length);
  ZuCheckRT(!sent && send.base == wire);
  if (sent || send.base != wire) return;
  wire[send.off] = 0xa5;
  Ztls_::bufferInitRx(&receive, wire + 5, send.off - 5);
  ZuGuard releaseReceive{[&receive]() { Ztls_::bufferDispose(&receive); }};
  auto consumed = send.off;
  if (corruptHeader) {
    wire[2] ^= 1;
    ZuCheckRT(Ztls_::receive(rx, &receive, wire, &consumed) == Ztls_AlertBadRecordMac);
    ZuCheckRT(!receive.off);
    return;
  }
  ZuCheckRT(!Ztls_::receive(rx, &receive, wire, &consumed));
  ZuCheckRT(consumed == send.off && receive.off == length);
  ZuCheckRT(receive.base == wire + 5 && !receive.isAllocated);
  ZuCheckRT(!memcmp(receive.base, payload, length) && wire[send.off] == 0xa5);
}

enum { RxClean, RxBadTag, RxBadHeader };

void fragmentedRecord(
  const Ztls_::CipherSuite *cipher, unsigned step, unsigned corrupt, unsigned truncate,
  bool partialHandshake = false)
{
  ZuTestScopeRT(fragmentedRecord);
  Ztls_::Context ctx{};
  ctx.random = &Ztls_::opensslRandom;
  ctx.getTime = &Ztls_::getTime;
  ctx.keyExchanges = Ztls_::opensslKeyExchanges;
  ctx.cipherSuites.push(cipher);
  ZtlsTest::useTestPSK(ctx, cipher);
  Ztls::Cxn txConnection{&ctx, false}, rxConnection{&ctx, true};
  auto	    tx = &txConnection, rx = &rxConnection;
  bool	    ready = ZtlsTest::handshake(tx, rx);
  ZuCheckRT(ready);
  if (!ready) return;
  uint8_t partial = Ztls_HsTypeNewTicket;
  if (partialHandshake) {
    Ztls_::bufferInitRx(&rx->recvbuf.mess, &partial, sizeof(partial));
    rx->recvbuf.mess.off = sizeof(partial);
  }
  uint8_t source[Ztls_RxAppCapacity];
  for (unsigned i = 0; i < sizeof(source); ++i) source[i] = uint8_t(i * 19 + (i >> 6));
  Ztls_::Buffer wire;
  uint8_t	small;
  Ztls_::bufferInitTx(&wire, &small, 0);
  ZuCheckRT(!Ztls_::send(tx, &wire, source, sizeof(source)));
  if (corrupt) wire.base[corrupt == RxBadHeader ? 2 : wire.off - 1] ^= 1;
  size_t    length = truncate ? truncate : wire.off;
  PlainPool pool;
  auto	   &output = rx->plaintext;
  output.reset(&pool, &pool);
  ZiMultiplex mx;
  auto	      packetOutput = mx.allocator<Zi::IOBufType::TxTCP, Ztls::TxPacket, 71>();
  struct DiscardPublisher : Ztls::RecordPublisher {
    int publish(ZmRef<ZiIOBuf>) override { return 0; }
  } publisher;
  Ztls::RecordSink writer{packetOutput, &pool, 71, publisher};
  auto		   input = mx.allocator<Zi::IOBufType::Rx, ZiRxQueue::Node, 31>();
  unsigned	   acquired = 0;
  int		   result = Ztls_ErrorInProgress;
  bool		   spansValid = true, unpublished = true;
  for (size_t off = 0; off < length;) {
    auto   packet = input->allocBuf(nullptr);
    size_t chunk = length - off;
    if (chunk > step) chunk = step;
    if (chunk > (packet->size - packet->skip - packet->allocator()->tailRoom))
      chunk = (packet->size - packet->skip - packet->allocator()->tailRoom);
    memcpy(packet->data(), wire.base + off, chunk);
    packet->length = uint32_t(chunk);
    auto   address = packet->data();
    size_t consumed = chunk;
    result = Ztls_::rxStream(rx, &writer, address, &consumed, nullptr);
    spansValid &= consumed <= chunk && consumed > 0;
    spansValid &= address != pool.bytes + PlainPool::Head && packet->length == chunk;
    off += consumed;
    acquired = pool.acquired;
    if (off < wire.off || corrupt || truncate || partialHandshake) unpublished &= !output.take();
    if (!consumed || (result != 0 && result != Ztls_ErrorInProgress)) break;
  }
  ZuCheckRT(spansValid && unpublished);
  if (partialHandshake)
    ZuCheckRT(result == Ztls_AlertDecodeError && !pool.live);
  else if (corrupt)
    ZuCheckRT(result == Ztls_AlertBadRecordMac && !pool.live);
  else if (truncate)
    ZuCheckRT(result == Ztls_ErrorInProgress && !output.take());
  else {
    ZuCheckRT(!result);
    auto plain = output.take();
    ZuCheckRT(plain && plain->length == sizeof(source));
    if (plain) ZuCheckRT(!memcmp(plain->data(), source, sizeof(source)));
  }
  // Teardown discards any partial operation while its sink remains alive.
  rx->retireRx();
  output.clear();
  ZuCheckRT(pool.acquired == acquired && pool.acquired <= 1);
  ZuCheckRT(!pool.live && pool.intact && pool.acquired == pool.released);
  if (corrupt || truncate || partialHandshake) {
    bool cleared = true;
    for (unsigned i = 0; i < PlainPool::Extent; ++i)
      cleared &= pool.bytes[PlainPool::Head + i] == 0 || pool.bytes[PlainPool::Head + i] == 0xa5;
    ZuCheckRT(cleared);
  }
  tx->retireTx();
  Ztls_::bufferDispose(&wire);
}

void records()
{
  ZuTestScopeRT(records);
  auto cipher = &Ztls_::opensslAes128gcmsha256;
  for (unsigned step : {1U, 7U, 16U, 31U})
    ZuTestCallRT(fragmentedRecord, cipher, step, RxClean, 0);
  ZuTestCallRT(fragmentedRecord, cipher, 7, RxBadTag, 0);
  ZuTestCallRT(fragmentedRecord, cipher, 7, RxBadHeader, 0);
  ZuTestCallRT(fragmentedRecord, cipher, 7, RxClean, 0, true);
  for (unsigned end : {1U, 4U, 5U, 9U, 14U, 37U, 16397U, 16405U})
    ZuTestCallRT(fragmentedRecord, cipher, 7, RxClean, end);
}

int main(int argc, char **argv)
{
  ZuTestUtil::parse(argc, argv);
  ZuTestMain();
  ZuTestCall(sinkGeometry);
  ZuTestCall(exactRecord, 1U);
  ZuTestCall(exactRecord, 1U, true);
  ZuTestCall(exactRecord, Ztls_RxAppCapacity);
  ZuTestCall(records);
}
