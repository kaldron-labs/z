//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>
#include <zpicotls/openssl.h>
#include <zlib/ZuTestUtil.hh>
#include <zlib/ZiMultiplex.hh>
#include <zlib/ZiRxStream.hh>
#include <zlib/ZtlsPico.hh>

using namespace ZuTestUtil;

// One fixed destination with observable head/tail room and final release.
struct PlainPool : Zi::IOBufAllocator {
  enum { Head = 17, Tail = 11, Extent = PTLS_RECEIVE_RECORD_CAPACITY };
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

static ptls_t *tls12(ptls_context_t &ctx, ptls_cipher_suite_t *cipher, int server)
{
  uint8_t secret[PTLS_TLS12_MASTER_SECRET_SIZE], random[PTLS_HELLO_RANDOM_SIZE * 2];
  memset(secret, 0x43, sizeof(secret)); memset(random, 0x76, sizeof(random));
  ptls_buffer_t params;
  uint8_t small;
  ptls_buffer_init_tx(&params, &small, 0);
  ptls_t *tls = nullptr;
  int n = ptls_build_tls12_export_params(&ctx, &params, server, 0, cipher,
    secret, random, 1, "localhost", ptls_iovec_init(nullptr, 0));
  if (!n) n = ptls_import(&ctx, &tls, ptls_iovec_init(params.base, params.off));
  ptls_buffer_dispose(&params);
  return n ? nullptr : tls;
}

void sinkGeometry()
{
  ZuTestScope(sinkGeometry);
  PlainPool pool;
  Ztls::Pico::PlaintextSink output{&pool, &pool};
  auto sink = output.sink();
  void *base = nullptr;
  size_t capacity = 0;
  ZuCheck(!sink.reserve(sink.ctx, PlainPool::Extent, &base, &capacity));
  ZuCheck(base == pool.bytes + PlainPool::Head && capacity == PlainPool::Extent);
  memset(base, 0x39, capacity);
  ZuCheck(!output.take() && pool.acquired == 1 && !pool.released);
  ZuCheck(!sink.commit(sink.ctx, PTLS_RECEIVE_APPLICATION_CAPACITY));
  auto buf = output.take();
  ZuCheck(buf && buf->owner == &pool && buf->allocator() == &pool);
  ZuCheck(buf && buf->length == PTLS_RECEIVE_APPLICATION_CAPACITY && !buf->registered());
  ZuCheck(!output.take() && pool.live);
  bool paddingCleared = true;
  for (unsigned i = PTLS_RECEIVE_APPLICATION_CAPACITY; i < capacity; ++i)
    paddingCleared &= pool.bytes[PlainPool::Head + i] == 0;
  ZuCheck(paddingCleared);
  buf = nullptr;
  ZuCheck(!pool.live && pool.intact && pool.released == pool.acquired);
  ZuCheck(!sink.reserve(sink.ctx, 37, &base, &capacity));
  memset(base, 0x91, 37);
  sink.discard(sink.ctx);
  bool discardedCleared = true;
  for (unsigned i = 0; i < 37; ++i) discardedCleared &= pool.bytes[PlainPool::Head + i] == 0;
  ZuCheck(discardedCleared);
  ZuCheck(!output.take() && !pool.live && pool.intact);
  pool.geometry = PlainPool::Extent - 1;
  ZuCheck(sink.reserve(sink.ctx, 37, &base, &capacity) == PTLS_ERROR_LIBRARY);
  ZuCheck(!pool.live && pool.acquired == pool.released);
  pool.throws = true;
  ZuCheck(sink.reserve(sink.ctx, 37, &base, &capacity) == PTLS_ERROR_NO_MEMORY);
}

void fragmentedRecord(ptls_cipher_suite_t *cipher, unsigned step, bool corrupt, unsigned truncate)
{
  ZuTestScopeRT(fragmentedRecord);
  ptls_cipher_suite_t *suites[] = {cipher, nullptr};
  ptls_context_t ctx{};
  ctx.random_bytes = ptls_openssl_random_bytes;
  ctx.get_time = &ptls_get_time;
  ctx.tls12_cipher_suites = suites;
  auto tx = tls12(ctx, cipher, 0), rx = tls12(ctx, cipher, 1);
  ZuCheckRT(tx && rx);
  if (!tx || !rx) { ptls_free(tx); ptls_free(rx); return; }
  uint8_t source[PTLS_RECEIVE_APPLICATION_CAPACITY];
  for (unsigned i = 0; i < sizeof(source); ++i) source[i] = uint8_t(i * 19 + (i >> 6));
  ptls_buffer_t wire;
  uint8_t small;
  ptls_buffer_init_tx(&wire, &small, 0);
  ZuCheckRT(!ptls_send(tx, &wire, source, sizeof(source)));
  if (corrupt) wire.base[wire.off - 1] ^= 1;
  size_t length = truncate ? truncate : wire.off;
  PlainPool pool;
  Ztls::Pico::PlaintextSink output{&pool, &pool};
  auto sink = output.sink();
  ZiMultiplex mx;
  auto packetOutput = mx.allocator<Zi::IOBufType::TxTCP, Ztls::Pico::TxPacket, 71>();
  Ztls::Pico::RecordSink writer{packetOutput, &pool, 71, nullptr,
    [](void *, ZmRef<ZiIOBuf>) { return 0; }};
  auto recordOutput = writer.sink();
  auto input = mx.allocator<Zi::IOBufType::RxTCP, ZiRxQueue::Node, 31>();
  unsigned acquired = 0;
  int result = PTLS_ERROR_IN_PROGRESS;
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
    result = ptls_receive_stream(rx, &sink, &recordOutput, address, &consumed, nullptr);
    spansValid &= consumed <= chunk && consumed > 0;
    spansValid &= address != pool.bytes + PlainPool::Head && packet->length == chunk;
    off += consumed;
    acquired = pool.acquired;
    if (off < wire.off || corrupt || truncate) unpublished &= !output.take();
    if (!consumed || (result != 0 && result != PTLS_ERROR_IN_PROGRESS)) break;
  }
  ZuCheckRT(spansValid && unpublished);
  if (corrupt) ZuCheckRT(result == PTLS_ALERT_BAD_RECORD_MAC && !pool.live);
  else if (truncate) ZuCheckRT(result == PTLS_ERROR_IN_PROGRESS && !output.take());
  else {
    ZuCheckRT(!result);
    auto plain = output.take();
    ZuCheckRT(plain && plain->length == sizeof(source));
    if (plain) ZuCheckRT(!memcmp(plain->data(), source, sizeof(source)));
  }
  // Teardown discards any partial operation while its sink remains alive.
  ptls_free(rx);
  output.clear();
  ZuCheckRT(pool.acquired == acquired && pool.acquired <= 1);
  ZuCheckRT(!pool.live && pool.intact && pool.acquired == pool.released);
  if (corrupt || truncate) {
    bool cleared = true;
    for (unsigned i = 0; i < PlainPool::Extent; ++i)
      cleared &= pool.bytes[PlainPool::Head + i] == 0 || pool.bytes[PlainPool::Head + i] == 0xa5;
    ZuCheckRT(cleared);
  }
  ptls_free(tx);
  ptls_buffer_dispose(&wire);
}

void records()
{
  ZuTestScopeRT(records);
  auto cipher = &ptls_openssl_tls12_ecdhe_rsa_aes128gcmsha256;
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
  ZuTestCall(records);
}
