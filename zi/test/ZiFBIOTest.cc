//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZtScratch.hh>
#include <zlib/ZiFB.hh>
#include <zlib/ZiTxStream.hh>

using namespace ZuTestUtil;

ZuStructFacet(Prefixed);
ZiFBConfig(Prefixed, ZiFB_SizePrefixed<>);

struct Address { ZiIP ip; };
ZfStruct(, (Address, FB), (ip, (Ctor<0>), UDT));
ZiFB::AsTable<"test.Address"> ZiFB_Fmt(Address *);
ZuAssert(ZiFB::As<Address>::template Handler<Address>::N == 2);
ZuAssert(ZiFB::As<ZiFB::IPv4>::template Handler<ZiFB::IPv4>::Size == 4);
ZuAssert(ZiFB::As<ZiFB::IPv6>::template Handler<ZiFB::IPv6>::Size == 16);

static void addresses()
{
  ZuTestScope(addresses);
  in_addr v4;
  memset(&v4, 0x7f, sizeof(v4));
  in6_addr v6;
  memset(&v6, 0xa5, sizeof(v6));
  // Exercise each address alternative through the same pooled writer.
  ZiFB::IOBuilder<> builder{new ZiIOBufAlloc<>};
  auto check = [&builder](ZiIP ip) {
    ZuTestScope(check);
    builder.clear();
    auto bytes = builder.finish(ZiFB::save(builder, Address{ip}));
    ZuCheck(!builder.failed() && ZiFB::verify<Address>(bytes));
    ZuCheck(ZiFB::view<Address>(bytes).get<ZfField(Address, ip)>() == ip);
    ZuCheck(ZiFB::handler<Address>(bytes).ctor().ip == ip);
  };
  ZuTestCall(check, ZiIP{});
  ZuTestCall(check, ZiIP{v4});
  ZuTestCall(check, ZiIP{v6});
}

template <typename B>
static void save(B &builder, ZuBSpan payload)
{
  auto vector = builder.copyRoot(payload, 1);
  auto table = builder.startTable();
  builder.fieldOffset(table, 0, vector);
  builder.finish(builder.endTable(table));
}

static void pooled()
{
  ZuTestScope(pooled);
  ZmRef<ZiIOBuf> owner = new ZiIOBufAlloc<>;
  owner->skip = ZiFB::IOBuilder<>::Align;
  void *tag = owner.ptr();
  owner->owner = tag;
  auto initial = owner->data();
  unsigned capacity = (owner->size & ~7U) - owner->skip;
  ZiFB::IOBuilder<> builder{owner};
  ::save(builder, ZuBSpan{"abc"});
  auto bytes = builder.span();
  auto buf = builder.buf();
  ZuCheck(buf.ptr() == owner.ptr() && buf->owner == tag);
  ZuCheck(buf->data() == bytes.data() && buf->length == bytes.length());
  ZuCheck(buf->data() == initial + capacity - bytes.length());
  ZuCheck(ZiFB::root(buf->cspan()).vector(0).string() == "abc");
  ZuCheck(!builder.buf());

  owner->clear();
  builder.buf(owner);
  auto payload = ZtScratch(ZiFB::Buffer, capacity<<2, capacity<<2);
  for (unsigned i = 0, n = payload.length(); i < n; ++i) payload[i] = i;
  builder.bytes(payload);
  ZuCheck(builder.capacity() == payload.length());
  ZuCheck(builder.span() == payload);
  builder.clear();
  ::save(builder, payload);
  auto grown = builder.buf();
  ZuCheck(grown.ptr() == owner.ptr() && grown->owner == tag);
  ZuCheck(ZiFB::root(grown->cspan()).vector(0).bytes() == payload);
}

static void alignment()
{
  ZuTestScope(alignment);
  ZmRef<ZiIOBuf> owner = new ZiIOBufAlloc<>;
  owner->skip = 32;
  ZiFB::IOBuilder<32> builder{owner};
  auto bytes = builder.copyRoot(ZuBSpan{"abcdefgh"}, 32);
  auto table = builder.startTable();
  builder.fieldOffset(table, 0, bytes);
  builder.finish<true>(builder.endTable(table));
  auto buf = builder.buf();
  auto data = ZiFB::root<true>(buf->cspan()).vector(0).data();
  ZuCheck(!(uintptr_t(data) & 31));
  ZuCheck(buf.ptr() == owner.ptr());
}

static void abandonment()
{
  ZuTestScope(abandonment);
  ZmRef<ZiIOBuf> owner = new ZiIOBufAlloc<>;
  unsigned n = owner->size<<2;
  {
    ZiFB::IOBuilder<> builder{owner};
    auto data = builder.push(n);
    ZuCheck(data != nullptr);
    memset(data, 0xa5, n);
  }
  // The buffer owns the allocation even when construction is abandoned.
  ZuCheck(owner->size >= n && owner->length == 0);
  owner->clear();
  ZiFB::IOBuilder<> reused{owner};
  ::save(reused, ZuBSpan{"reused"});
  auto buf = reused.buf();
  ZuCheck(ZiFB::root(buf->cspan()).vector(0).string() == "reused");
}

static void framingTailRoom()
{
  ZuTestScope(framingTailRoom);
  ZmRef<ZiIOBuf> owner = new ZiIOBufAlloc<208>;
  ZiFB::IOBuilder<ZiFB::Wire::MaxAlign> builder{owner};
  ::save(builder, ZuBSpan{"tail room"});
  auto buf = builder.buf();
  auto data = buf->data();
  auto size = buf->size;
  auto length = buf->length;
  auto avail = buf->avail();
  ZuCheck(avail && buf->skip >= sizeof(uint32_t));
  ZiFB::write<uint32_t>(buf->prepend(sizeof(uint32_t)), length);
  ZuCheck(buf.ptr() == owner.ptr() && buf->size == size);
  ZuCheck(buf->data() + sizeof(uint32_t) == data && buf->avail() == avail);
  ZuCheck(ZiFB::read<uint32_t>(buf->data()) == length);
  ZuCheck(ZiFB::root({data, length}).vector(0).string() == "tail room");
}

static void runtimeAlign(unsigned align)
{
  ZuTestScopeRT(runtimeAlign);
  ZmRef<ZiIOBuf> owner = new ZiIOBufAlloc<>;
  unsigned initial = owner->size;
  owner->skip = align;
  ZiFB::IOBuilder<ZiFB::Wire::MaxAlign> builder{owner, align};
  unsigned capacity = (initial & ~(align - 1)) - align;
  ZuCheckRT(builder.capacity() == capacity);
  builder.bytes(ZuBSpan{"tail"});
  unsigned tail = builder.size();
  unsigned head = 2 * sizeof(ZiFB::Offset);
  memset(builder.pushScratch(head), 0x5a, head);
  builder.push(capacity<<1);
  ZuCheckRT(!builder.failed() && !(builder.capacity() & (align - 1)));
  ZuCheckRT((ZuBSpan{builder.at(tail), 4} == "tail"));
  for (unsigned i = 0; i < head; ++i) ZuCheckRT(builder.scratch()[i] == 0x5a);

  builder.clear();
  auto payload = ZtScratch(ZiFB::Buffer, capacity<<2, capacity<<2);
  for (unsigned i = 0, n = payload.length(); i < n; ++i) payload[i] = i;
  auto nested = builder.startNested();
  auto vector = builder.copyRoot(payload, align);
  auto table = builder.startTable();
  builder.fieldOffset(table, 0, vector);
  auto data = builder.endNested(nested, builder.endTable(table));
  auto outer = builder.startTable();
  builder.fieldOffset(outer, 0, data);
  builder.finish<true>(builder.endTable(outer));
  auto buf = builder.buf();
  ZuCheckRT(buf.ptr() == owner.ptr());
  unsigned length = buf->length;
  ZiFB::write<uint32_t>(buf->prepend(sizeof(uint32_t)), length);
  ZuCheckRT(ZiFB::read<uint32_t>(buf->data()) == length);
  auto root = ZiFB::root<true>({buf->data() + sizeof(uint32_t), length});
  auto inner = ZiFB::root(root.vector(0).bytes()).vector(0);
  ZuCheckRT(!(uintptr_t(inner.data()) & (align - 1)));
  ZuCheckRT(inner.bytes() == payload);
  owner->clear();
  builder.buf(owner);
  ::save(builder, ZuBSpan{"reused"});
  auto reused = builder.buf();
  ZuCheckRT(ZiFB::root(reused->cspan()).vector(0).string() == "reused");
}

struct Message { ZuBSpan payload; };
ZfStruct(, (Message, FB), (payload, (Required, FB::Align<32>), Bytes));
ZiFB::AsTable<"test.Message"> ZiFB_Fmt(Message *);
ZuStructReflect(Message, ZuFacet::Prefixed, ZuFacet::FB);

struct TxTest : public ZiTxStream<TxTest> {
  using Base = ZiTxStream<TxTest>;
  using Base::operator <<;

  ZmRef<ZiIOBuf>	allocated;
  ZmRef<ZiIOBuf>	sent;
  ZiFB::Buffer		output;
  unsigned		allocs = 0;
  unsigned		sends = 0;
  unsigned		initialSize = 0;
  uint64_t		spanBytes = 0;
  bool			final = false;
  bool			reject = false;
  bool			sendOK = true;

  TxTest() : ZiTxStream<TxTest>{ZiIOBuf_DefltSize, 3, 5} { }
  ~TxTest() { flush(); }
  TxTest &operator <<(ZuBSpan bytes) {
    spanBytes += bytes.length();
    Base::operator <<(bytes);
    return *this;
  }
  ZmRef<ZiIOBuf> allocBuf_(unsigned head) {
    ++allocs;
    if (reject) return nullptr;
    allocated = new ZiIOBufAlloc<>;
    initialSize = allocated->size;
    allocated->skip = head;
    return allocated;
  }
  bool sendBuf_(ZmRef<ZiIOBuf> buf, bool flush) {
    ++sends;
    final = flush;
    output << buf->cspan();
    sent = ZuMv(buf);
    return sendOK;
  }
};
ZuAssert(ZiIsTxStream<TxTest>{});
ZuAssert(ZiIsTxStream<ZiTxStream<TxTest>>{});
ZuAssert(!ZiIsTxStream<ZiFB::Buffer>{});

// Observe the existing storage implementation; do not replace its allocation,
// growth or detach policy. Counters exclude test output/reference copies.
struct GrowStorage : public ZiFB::IOStorage<32> {
  using Base = ZiFB::IOStorage<32>;
  using Base::Base;

  unsigned reallocs = 0;
  uint64_t headBytes = 0;
  uint64_t tailBytes = 0;
  uint64_t inplaceBytes = 0;
  uint64_t compactBytes = 0;
  unsigned tailRoom = 0;

  uint8_t *grow(unsigned oldSize, unsigned newSize, unsigned head, unsigned tail) {
    auto buf = Base::buffer();
    unsigned preserved = tail ? tail + tailRoom : 0;
    if (uint64_t(newSize) + tailRoom + buf->skip > buf->size) {
      ++reallocs;
      headBytes += head;
      tailBytes += preserved;
    } else
      inplaceBytes += preserved;
    return Base::grow(oldSize, newSize, head, tail);
  }
  ZmRef<ZiIOBuf> detach(unsigned capacity, unsigned length) {
    if (length <= Base::size() && capacity > Base::size()) compactBytes += length;
    return Base::detach(capacity, length);
  }
};

static void ioRelocation()
{
  ZuTestScope(ioRelocation);
  TxTest probe;
  constexpr unsigned Align = ZiFB::BufferAlign<Message>::N;
  unsigned head = (probe.headRoom() + Align - 1) & ~(Align - 1);
  unsigned capacity = (probe.maxSize() - head - probe.tailRoom()) & ~(Align - 1);
  auto payload = ZtScratch(ZiFB::Buffer, capacity<<2, capacity<<2);
  for (unsigned i = 0, n = payload.length(); i < n; ++i) payload[i] = i ^ (i>>8);
  uint64_t small = capacity - (Align<<1) + 1;
  for (auto n : {small, payload.length()}) {
    Message msg{{payload.data(), n}};
    TxTest stream;
    ZuCheck(ZiFB::save(stream, msg) == ZiFB::Error::OK);
    ZmRef<ZiIOBuf> owner = new ZiIOBufAlloc<>;
    owner->skip = head;
    GrowStorage storage{owner, Align, stream.maxSize(), stream.tailRoom()};
    storage.tailRoom = stream.tailRoom();
    ZiFB::Builder<GrowStorage> builder{storage, Align};
    auto bytes = builder.finish(ZiFB::save(builder, msg));
    ZuCheck(!builder.failed() && bytes == stream.output);
    ZuCheck(storage.reallocs && storage.headBytes + storage.tailBytes);
    if (bytes.length() <= capacity) {
      auto sent = storage.detach(builder.capacity(), builder.size());
      ZuCheck(sent.ptr() == owner.ptr() && sent->cspan() == stream.sent->cspan());
      ZuCheck(sent->size == stream.sent->size && sent->skip == stream.sent->skip);
      ZuCheck(storage.compactBytes == bytes.length() && !stream.spanBytes);
    } else
      ZuCheck(stream.spanBytes == bytes.length() && stream.allocs > 1);
    log("I/O growth: payload=", n, " output=", bytes.length(),
      " backing allocations=", storage.reallocs,
      " relocated head bytes=", storage.headBytes,
      " relocated tail bytes=", storage.tailBytes,
      " in-place tail bytes=", storage.inplaceBytes,
      " detach compaction bytes=", storage.compactBytes,
      " emitted buffer allocations=", stream.allocs,
      " completed-span insertion bytes=", stream.spanBytes);
  }
}

static void txStream()
{
  ZuTestScope(txStream);
  Message msg{"direct"};
  ZiFB::Buffer expected;
  ZuCheck(ZiFB::save<ZuFacet::Prefixed>(expected, msg, "TXFB") == ZiFB::Error::OK);
  TxTest stream;
  ZiTxStream<TxTest> &base = stream;
  ZuCheck(ZiFB::save<ZuFacet::Prefixed>(base, msg, "TXFB") == ZiFB::Error::OK);
  ZuCheck(stream.allocs == 1 && stream.sends == 1 && stream.final);
  ZuCheck(stream.sent.ptr() == stream.allocated.ptr());
  ZuCheck(stream.sent->skip > stream.headRoom());
  ZuCheck(stream.sent->avail() >= stream.tailRoom());
  ZuCheck(stream.output == expected);
  ZuCheck((ZiFB::verify<Message, ZuFacet::Prefixed>(stream.output, "TXFB")));
  auto data = ZiFB::view<Message, ZuFacet::Prefixed>(stream.sent->cspan()).
    get<ZfField(Message, payload)>().data();
  ZuCheck(!(uintptr_t(data) & 31));

  unsigned before = stream.sends;
  Message invalid{};
  ZuCheck(ZiFB::save(stream, invalid) == ZiFB::Error::Required);
  ZuCheck(!stream.failed() && stream.sends == before);

  stream.output.clear();
  auto payload = ZtScratch(ZiFB::Buffer, stream.maxSize()<<2,
    stream.maxSize()<<2);
  for (unsigned i = 0, n = payload.length(); i < n; ++i) payload[i] = i ^ (i>>8);
  // The completed output is within the emitted-buffer limit; scratch grows
  // the arena, then detach compacts the output within the same owner.
  TxTest fitting;
  enum { Align = ZiFB::BufferAlign<Message, ZuFacet::FB>::N };
  unsigned head = (fitting.headRoom() + Align - 1) & ~(Align - 1);
  unsigned capacity = (fitting.maxSize() - head - fitting.tailRoom()) & ~(Align - 1);
  msg.payload = {payload.data(), capacity - (Align<<1) + 1};
  expected.clear();
  ZuCheck(ZiFB::save(expected, msg) == ZiFB::Error::OK);
  ZuCheck(ZiFB::save(fitting, msg) == ZiFB::Error::OK);
  ZuCheck(fitting.allocs == 1 && fitting.sends == 1 && fitting.final);
  ZuCheck(fitting.sent.ptr() == fitting.allocated.ptr());
  ZuCheck(fitting.sent->size > fitting.initialSize);
  ZuCheck(!fitting.spanBytes);
  ZuCheck(fitting.sent->skip + fitting.sent->length + fitting.tailRoom() <= fitting.maxSize());
  ZuCheck(fitting.sent->skip + fitting.sent->length == head + capacity);
  ZuCheck(fitting.output == expected);
  log("direct handoff: span insertion bytes=", fitting.spanBytes,
    " detach compaction bytes=", expected.length());

  msg.payload = payload;
  expected.clear();
  ZuCheck(ZiFB::save(expected, msg) == ZiFB::Error::OK);
  stream << "prefix";
  uint64_t prefixBytes = stream.spanBytes;
  ZuCheck(ZiFB::save(stream, msg) == ZiFB::Error::OK);
  ZuCheck(stream.final && stream.output == (ZiFB::Buffer{} << "prefix" << expected));
  ZuCheck(stream.spanBytes - prefixBytes == expected.length());
  log("splitting fallback: object span bytes=", stream.spanBytes - prefixBytes,
    " preceding span bytes=", prefixBytes);

  TxTest rejected;
  rejected.reject = true;
  ZuCheck(ZiFB::save(rejected, msg) == ZiFB::Error::OK);
  ZuCheck(rejected.failed() && rejected.sends == 0);
  ZuCheck(ZiFB::save(rejected, msg) == ZiFB::Error::OK);
  ZuCheck(rejected.allocs == 1);

  TxTest failed;
  failed.sendOK = false;
  msg.payload = "send failure";
  ZuCheck(ZiFB::save(failed, msg) == ZiFB::Error::OK);
  ZuCheck(failed.failed() && failed.sends == 1);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(txStream);
  ZuTestCall(ioRelocation);
  ZuTestCall(pooled);
  ZuTestCall(alignment);
  ZuTestCall(framingTailRoom);
  ZuTestCall(runtimeAlign, 8);
  ZuTestCall(runtimeAlign, 16);
  ZuTestCall(runtimeAlign, 32);
  ZuTestCall(abandonment);
  ZuTestCall(addresses);
}
