//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>
#include <zlib/ZtlsEngineOpenSSL.hh>
#include <zlib/ZuTestUtil.hh>
#include <zlib/ZiMultiplex.hh>
#include <zlib/ZiRxStream.hh>
#include <zlib/ZtlsBuf.hh>
#include <zlib/ZtlsCredentials.hh>
#include <zlib/ZtlsEngineOpenSSL.hh>
#include "ZtlsTestHandshake.hh"

using namespace ZuTestUtil;

// One fixed destination with observable head/tail room and final release.
struct PlainPool : Zi::IOBufAllocator {
  enum { Head = 17, Tail = 11, Extent = Ztls_RECEIVE_RECORD_CAPACITY };
  alignas(64) uint8_t bytes[Head + Extent + Tail];
  unsigned acquired = 0, released = 0;
  bool live = false, intact = true, throws = false;
  unsigned geometry = Extent;
  PlainPool() : Zi::IOBufAllocator{nullptr, Zi::IOBufType::RxTLS,
    sizeof(bytes), Head, Tail} { memset(bytes, 0xa5, sizeof(bytes)); }
  uint8_t *alloc(unsigned) const override { return nullptr; }
  void free(uint8_t *ptr, unsigned, bool registered) const override {
    auto self = const_cast<PlainPool *>(this);
    self->intact &= ptr == bytes && !registered && live;
    for (unsigned i = 0; i < Head; ++i) self->intact &= bytes[i] == 0xa5;
    for (unsigned i = Head + Extent; i < sizeof(bytes); ++i)
      self->intact &= bytes[i] == 0xa5;
    self->live = false; ++self->released;
  }
  ZmRef<ZiIOBuf> allocBuf(void *owner, uint32_t length, uint32_t) override {
    if (throws) throw 1;
    if (live || length) return nullptr;
    live = true; ++acquired;
    auto buf = ZmRef<ZiIOBuf>{new Zi::IOBufStub<ZiRxQueue::Node>{
      bytes, Head + geometry + Tail, this, owner, 0, false, false}};
    buf->skip = Head;
    return buf;
  }
};

void sinkGeometry()
{
  ZuTestScope(sinkGeometry);
  PlainPool pool;
  Ztls::PlaintextSink output{&pool, &pool};
  void *base = nullptr;
  size_t capacity = 0;
  ZuCheck(!output.reserve(PlainPool::Extent, &base, &capacity));
  ZuCheck(base == pool.bytes + PlainPool::Head && capacity == PlainPool::Extent);
  memset(base, 0x39, capacity);
  ZuCheck(!output.take() && pool.acquired == 1 && !pool.released);
  ZuCheck(!output.commit(Ztls_RECEIVE_APPLICATION_CAPACITY));
  auto buf = output.take();
  ZuCheck(buf && buf->owner == &pool && buf->allocator() == &pool);
  ZuCheck(buf && buf->length == Ztls_RECEIVE_APPLICATION_CAPACITY && !buf->registered());
  ZuCheck(!output.take() && pool.live);
  bool paddingCleared = true;
  for (unsigned i = Ztls_RECEIVE_APPLICATION_CAPACITY; i < capacity; ++i)
    paddingCleared &= pool.bytes[PlainPool::Head + i] == 0;
  ZuCheck(paddingCleared);
  buf = nullptr;
  ZuCheck(!pool.live && pool.intact && pool.released == pool.acquired);
  ZuCheck(!output.reserve(37, &base, &capacity));
  memset(base, 0x91, 37);
  output.discard();
  bool discardedCleared = true;
  for (unsigned i = 0; i < 37; ++i) discardedCleared &= pool.bytes[PlainPool::Head + i] == 0;
  ZuCheck(discardedCleared);
  ZuCheck(!output.take() && !pool.live && pool.intact);
  pool.geometry = PlainPool::Extent - 1;
  ZuCheck(output.reserve(37, &base, &capacity) == Ztls_ERROR_LIBRARY);
  ZuCheck(!pool.live && pool.acquired == pool.released);
  pool.throws = true;
  ZuCheck(output.reserve(37, &base, &capacity) == Ztls_ERROR_NO_MEMORY);
}

// Retains zpicotls t/exact-rx.c's record-boundary and zero-copy assertions.
static void exactRecord(unsigned length)
{
  ZuTestScopeRT(exactRecord);
  auto cipher = &Ztls_::openssl_aes128gcmsha256;
  Ztls_::cipher_suite_t *suites[] = {cipher, nullptr};
  Ztls_::context_t ctx{};
  ctx.random_bytes = Ztls_::openssl_random_bytes;
  ctx.get_time = &Ztls_::get_time;
  ctx.key_exchanges = Ztls_::openssl_key_exchanges;
  ctx.cipher_suites = suites;
  ZtlsTest::useTestPSK(ctx, cipher);
  Ztls::Cxn txConnection{&ctx, false}, rxConnection{&ctx, true};
  auto tx = &txConnection, rx = &rxConnection;
  bool ready = ZtlsTest::handshake(tx, rx);
  ZuCheckRT(ready);
  if (!ready) return;

  uint8_t payload[Ztls_RECEIVE_APPLICATION_CAPACITY];
  uint8_t wire[Ztls_RECEIVE_APPLICATION_CAPACITY + 5 + 1 + Ztls_MAX_DIGEST_SIZE + 1];
  memset(payload, 0x37, length);
  Ztls_::buffer_t send, receive;
  Ztls_::buffer_init_tx(&send, wire, sizeof(wire) - 1);
  ZuGuard releaseSend{[&send]() { Ztls_::buffer_dispose(&send); }};
  int sent = Ztls_::send(tx, &send, payload, length);
  ZuCheckRT(!sent && send.base == wire);
  if (sent || send.base != wire) return;
  wire[send.off] = 0xa5;
  Ztls_::buffer_init_rx(&receive, wire + 5, send.off - 5);
  ZuGuard releaseReceive{[&receive]() { Ztls_::buffer_dispose(&receive); }};
  auto consumed = send.off;
  ZuCheckRT(!Ztls_::receive(rx, &receive, wire, &consumed));
  ZuCheckRT(consumed == send.off && receive.off == length);
  ZuCheckRT(receive.base == wire + 5 && !receive.is_allocated);
  ZuCheckRT(!memcmp(receive.base, payload, length) && wire[send.off] == 0xa5);
}

void fragmentedRecord(Ztls_::cipher_suite_t *cipher, unsigned step, bool corrupt, unsigned truncate)
{
  ZuTestScopeRT(fragmentedRecord);
  Ztls_::cipher_suite_t *suites[] = {cipher, nullptr};
  Ztls_::context_t ctx{};
  ctx.random_bytes = Ztls_::openssl_random_bytes;
  ctx.get_time = &Ztls_::get_time;
  ctx.key_exchanges = Ztls_::openssl_key_exchanges;
  ctx.cipher_suites = suites;
  ZtlsTest::useTestPSK(ctx, cipher);
  Ztls::Cxn txConnection{&ctx, false}, rxConnection{&ctx, true};
  auto tx = &txConnection, rx = &rxConnection;
  bool ready = ZtlsTest::handshake(tx, rx);
  ZuCheckRT(ready);
  if (!ready) return;
  uint8_t source[Ztls_RECEIVE_APPLICATION_CAPACITY];
  for (unsigned i = 0; i < sizeof(source); ++i) source[i] = uint8_t(i * 19 + (i >> 6));
  Ztls_::buffer_t wire;
  uint8_t small;
  Ztls_::buffer_init_tx(&wire, &small, 0);
  ZuCheckRT(!Ztls_::send(tx, &wire, source, sizeof(source)));
  if (corrupt) wire.base[wire.off - 1] ^= 1;
  size_t length = truncate ? truncate : wire.off;
  PlainPool pool;
  auto &output = rx->plaintext;
  output.reset(&pool, &pool);
  ZiMultiplex mx;
  auto packetOutput = mx.allocator<Zi::IOBufType::TxTCP, Ztls::TxPacket, 71>();
  Ztls::RecordSink writer{packetOutput, &pool, 71, nullptr,
    [](void *, ZmRef<ZiIOBuf>) { return 0; }};
  auto input = mx.allocator<Zi::IOBufType::Rx, ZiRxQueue::Node, 31>();
  unsigned acquired = 0;
  int result = Ztls_ERROR_IN_PROGRESS;
  bool spansValid = true, unpublished = true;
  for (size_t off = 0; off < length;) {
    auto packet = input->allocBuf(nullptr);
    size_t chunk = length - off;
    if (chunk > step) chunk = step;
    if (chunk > (packet->size - packet->skip - packet->allocator()->tailRoom)) chunk = (packet->size - packet->skip - packet->allocator()->tailRoom);
    memcpy(packet->data(), wire.base + off, chunk);
    packet->length = uint32_t(chunk);
    auto address = packet->data();
    size_t consumed = chunk;
    result = Ztls_::receive_stream(rx, &writer, address, &consumed, nullptr);
    spansValid &= consumed <= chunk && consumed > 0;
    spansValid &= address != pool.bytes + PlainPool::Head && packet->length == chunk;
    off += consumed;
    acquired = pool.acquired;
    if (off < wire.off || corrupt || truncate) unpublished &= !output.take();
    if (!consumed || (result != 0 && result != Ztls_ERROR_IN_PROGRESS)) break;
  }
  ZuCheckRT(spansValid && unpublished);
  if (corrupt) ZuCheckRT(result == Ztls_ALERT_BAD_RECORD_MAC && !pool.live);
  else if (truncate) ZuCheckRT(result == Ztls_ERROR_IN_PROGRESS && !output.take());
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
  if (corrupt || truncate) {
    bool cleared = true;
    for (unsigned i = 0; i < PlainPool::Extent; ++i)
      cleared &= pool.bytes[PlainPool::Head + i] == 0 || pool.bytes[PlainPool::Head + i] == 0xa5;
    ZuCheckRT(cleared);
  }
  tx->retireTx();
  Ztls_::buffer_dispose(&wire);
}

void records()
{
  ZuTestScopeRT(records);
  auto cipher = &Ztls_::openssl_aes128gcmsha256;
  for (unsigned step : {1U, 7U, 16U, 31U})
    ZuTestCallRT(fragmentedRecord, cipher, step, false, 0);
  ZuTestCallRT(fragmentedRecord, cipher, 7, true, 0);
  for (unsigned end : {1U, 4U, 5U, 9U, 14U, 37U, 16397U, 16405U})
    ZuTestCallRT(fragmentedRecord, cipher, 7, false, end);
}

int main(int argc, char **argv)
{
  ZuTestUtil::parse(argc, argv);
  ZuTestMain();
  ZuTestCall(sinkGeometry);
  ZuTestCall(exactRecord, 1U);
  ZuTestCall(exactRecord, Ztls_RECEIVE_APPLICATION_CAPACITY);
  ZuTestCall(records);
}
