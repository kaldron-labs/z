//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>
#include <zlib/ZuTestUtil.hh>
#include <zlib/ZtlsBackend.hh>
#include <zlib/ZiMultiplex.hh>

using namespace ZuTestUtil;

// Fixed registered backing with observable allocation, lifetime and framing.
struct PacketPool : Zi::IOBufAllocator {
  enum { Slots = 32, Size = 128, Head = 17, Tail = 9 };
  alignas(64) uint8_t bytes[Slots][Size];
  bool used[Slots]{};
  unsigned extent[Slots]{};
  unsigned attempts = 0, acquired = 0, released = 0, live = 0;
  unsigned failAt = unsigned(-1);
  uint32_t cap = 0;
  bool intact = true;

  PacketPool() : Zi::IOBufAllocator{nullptr, Zi::IOBufType::TxTCP, Size, Head, Tail} { }
  uint8_t *alloc(unsigned) const override { return nullptr; }
  void free(uint8_t *ptr, unsigned, bool registered) const override {
    auto self = const_cast<PacketPool *>(this);
    for (unsigned i = 0; i < Slots; ++i) if (ptr == bytes[i]) {
      self->intact &= registered && used[i];
      for (unsigned j = 0; j < Head; ++j) self->intact &= ptr[j] == 0xa5;
      for (unsigned j = extent[i] - Tail; j < Size; ++j)
	self->intact &= ptr[j] == 0xa5;
      self->used[i] = false;
      --self->live; ++self->released;
      return;
    }
    self->intact = false;
  }
  ZmRef<ZiIOBuf> allocBuf(void *owner, uint32_t length, uint32_t mps) override {
    cap = mps;
    if (attempts++ == failAt) return nullptr;
    unsigned payload = capacity();
    if (payload > mps) payload = mps;
    if (!payload || length > payload) return nullptr;
    for (unsigned i = 0; i < Slots; ++i) if (!used[i]) {
      used[i] = true; ++live; ++acquired;
      memset(bytes[i], 0xa5, Size);
      extent[i] = payload + Head + Tail;
      auto buf = ZmRef<ZiIOBuf>{new Zi::IOBufStub<Ztls::Backend::TxPacket>{
	bytes[i], extent[i], this, owner, length, false, true}};
      buf->skip = Head;
      return buf;
    }
    return nullptr;
  }
};

struct Published {
  ZiTxQueue packets;
  unsigned calls = 0, failAt = unsigned(-1);
  static int publish(void *ctx, ZmRef<ZiIOBuf> buf) {
    auto self = static_cast<Published *>(ctx);
    if (self->calls++ == self->failAt) return PTLS_ERROR_LIBRARY;
    self->packets.pushNode(ZuMv(buf));
    return 0;
  }
};

static int write(ptls_record_sink_t &sink, const uint8_t *input, size_t length)
{
  size_t off = 0;
  while (off < length) {
    void *base = nullptr;
    size_t capacity = 0;
    int n = sink.next(sink.ctx, &base, &capacity);
    if (n) return n;
    if (capacity > length - off) capacity = length - off;
    memcpy(base, input + off, capacity);
    if ((n = sink.commit(sink.ctx, capacity))) return n;
    off += capacity;
  }
  return 0;
}

void reservationFailure()
{
  ZuTestScopeRT(reservationFailure);
  uint8_t plaintext[301], original[301];
  memset(plaintext, 0x37, sizeof(plaintext));
  memcpy(original, plaintext, sizeof(plaintext));
  // 301 bytes at a 53-byte MPS requires six detached reservations.
  for (unsigned fail = 0; fail < 6; ++fail) {
    PacketPool pool;
    pool.failAt = fail;
    Published output;
    Ztls::Backend::RecordSink writer{&pool, &output, 53, &output, Published::publish};
    auto sink = writer.sink();
    ZuCheckRT(sink.reserve(sink.ctx, sizeof(plaintext)) == PTLS_ERROR_NO_MEMORY);
    sink.discard(sink.ctx);
    ZuCheckRT(!pool.live && pool.acquired == fail && pool.released == fail);
    ZuCheckRT(pool.intact && !output.calls && pool.cap == 53);
    ZuCheckRT(!memcmp(plaintext, original, sizeof(plaintext)));
  }
}

void geometryAndLifetime()
{
  ZuTestScopeRT(geometryAndLifetime);
  uint32_t limits[] = {1U, 53U, 1024U, UINT32_MAX};
  for (uint32_t mps : limits) {
    PacketPool pool;
    Published output;
    Ztls::Backend::RecordSink writer{&pool, &output, mps, &output, Published::publish};
    auto sink = writer.sink();
    uint8_t input[23];
    for (unsigned i = 0; i < sizeof(input); ++i) input[i] = uint8_t(i);
    ZuCheckRT(!sink.reserve(sink.ctx, sizeof(input)));
    ZuCheckRT(!output.calls && pool.live);
    ZuCheckRT(!write(sink, input, sizeof(input)));
    ZuCheckRT(!sink.finish(sink.ctx));
    unsigned off = 0;
    unsigned payload = pool.capacity();
    if (payload > mps) payload = mps;
    while (auto buf = output.packets.shift()) {
      ZuCheckRT(buf->owner == &output && buf->registered() && buf->skip == PacketPool::Head);
      ZuCheckRT(buf->allocator() == &pool && buf->size == payload + PacketPool::Head + PacketPool::Tail);
      ZuCheckRT(buf->length <= payload && buf->data_() != input);
      ZuCheckRT(!memcmp(static_cast<ZiIOBuf *>(buf.ptr())->data(), input + off, buf->length));
      off += buf->length;
    }
    ZuCheckRT(off == sizeof(input) && pool.intact && !pool.live);
    ZuCheckRT(pool.acquired == pool.released && pool.cap == mps);
  }
}

void sharedGeometry()
{
  ZuTestScopeRT(sharedGeometry);
  PacketPool pool;
  Published first, second;
  Ztls::Backend::RecordSink small{&pool, &first, 31, &first, Published::publish};
  Ztls::Backend::RecordSink large{&pool, &second, 73, &second, Published::publish};
  auto a = small.sink(), b = large.sink();
  uint8_t bytes[97]{};
  ZuCheckRT(!a.reserve(a.ctx, sizeof(bytes)) && !b.reserve(b.ctx, sizeof(bytes)));
  ZuCheckRT(pool.size == PacketPool::Size && pool.headRoom == PacketPool::Head &&
    pool.tailRoom == PacketPool::Tail);
  ZuCheckRT(!write(a, bytes, sizeof(bytes)) && !write(b, bytes, sizeof(bytes)));
  ZuCheckRT(!a.finish(a.ctx) && !b.finish(b.ctx));
  ZuCheckRT(first.calls == 4 && second.calls == 2);
  while (auto buf = first.packets.shift())
    ZuCheckRT(buf->size == 31 + PacketPool::Head + PacketPool::Tail);
  while (auto buf = second.packets.shift())
    ZuCheckRT(buf->size == 73 + PacketPool::Head + PacketPool::Tail);
  ZuCheckRT(!pool.live && pool.intact && pool.acquired == pool.released);

  ZiMultiplex mx;
  auto kernel = mx.allocator<Zi::IOBufType::TxTCP, Ztls::Backend::TxPacket, 64>();
  ZuCheckRT(!kernel->allocBuf(nullptr, 18, 17));
  Published output;
  Ztls::Backend::RecordSink builtin{kernel, &output, 17, &output, Published::publish};
  auto sink = builtin.sink();
  ZuCheckRT(!sink.reserve(sink.ctx, sizeof(bytes)));
  ZuCheckRT(!write(sink, bytes, sizeof(bytes)) && !sink.finish(sink.ctx));
  unsigned length = 0;
  while (auto buf = output.packets.shift()) {
    ZuCheckRT(!buf->skip && !buf->registered() && buf->size == 17);
    length += buf->length;
  }
  ZuCheckRT(length == sizeof(bytes));
}

void sequencingAndPublishFailure()
{
  ZuTestScopeRT(sequencingAndPublishFailure);
  PacketPool pool;
  Published output;
  Ztls::Backend::RecordSink writer{&pool, &output, 13, &output, Published::publish};
  auto sink = writer.sink();
  void *base;
  size_t capacity;
  ZuCheckRT(sink.next(sink.ctx, &base, &capacity) == PTLS_ERROR_LIBRARY);
  ZuCheckRT(!sink.reserve(sink.ctx, 31));
  ZuCheckRT(sink.reserve(sink.ctx, 31) == PTLS_ERROR_LIBRARY);
  ZuCheckRT(sink.finish(sink.ctx) == PTLS_ERROR_LIBRARY);
  ZuCheckRT(!sink.next(sink.ctx, &base, &capacity));
  ZuCheckRT(sink.commit(sink.ctx, capacity + 1) == PTLS_ERROR_LIBRARY);
  uint8_t bytes[31]{};
  ZuCheckRT(!write(sink, bytes, sizeof(bytes)));
  output.failAt = 1;
  ZuCheckRT(sink.finish(sink.ctx) == PTLS_ERROR_LIBRARY);
  ZuCheckRT(output.calls == 2 && pool.live == 1 && pool.released == 2);
  output.packets.clean();
  ZuCheckRT(!pool.live && pool.intact && pool.acquired == pool.released);
  Ztls::Backend::RecordSink zero{&pool, nullptr, 0, &output, Published::publish};
  auto invalid = zero.sink();
  auto attempts = pool.attempts;
  ZuCheckRT(invalid.reserve(invalid.ctx, 1) == PTLS_ERROR_LIBRARY);
  ZuCheckRT(pool.attempts == attempts);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(reservationFailure);
  ZuTestCall(geometryAndLifetime);
  ZuTestCall(sequencingAndPublishFailure);
  ZuTestCall(sharedGeometry);
}
