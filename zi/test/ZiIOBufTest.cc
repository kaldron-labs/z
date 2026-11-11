//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdlib.h>
#include <string.h>

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZtString.hh>

#include <zlib/ZiIOBuf.hh>
#include <zlib/ZiMultiplex.hh>

using namespace ZuTestUtil;

void testSliceAndEnsurePreserveSpan()
{
  ZuTestScope(testSliceAndEnsurePreserveSpan);

  ZiMultiplex allocatorMx;
  auto allocator = allocatorMx.allocator<>();
  ZmRef<ZiIOBuf> buf = allocator->allocBuf();
  *buf << "fbah";
  ZtString<> s;
  s << ZuCSpan(buf->cspan());
  ZuCheck(s == "fbah");
  ++buf->skip, --buf->length;
  s.clear();
  s << ZuCSpan(buf->cspan());
  ZuCheck(s == "bah");

  buf->clear();
  *buf << "0123456789";
  buf->skip = 2;
  buf->length = 5;
  ZtString<> before;
  before << ZuCSpan(buf->cspan());
  auto old_skip = buf->skip;
  auto old_length = buf->length;
  ZuCheck(buf->ensure(buf->size + 128));
  ZuCheck(buf->skip == old_skip);
  ZuCheck(buf->length == old_length);
  ZtString<> after;
  after << ZuCSpan(buf->cspan());
  ZuCheck(after == before);
}

void testAdvanceRewind()
{
  ZuTestScope(testAdvanceRewind);

  ZiMultiplex allocatorMx;
  auto allocator = allocatorMx.allocator<>();
  ZmRef<ZiIOBuf> buf = allocator->allocBuf();
  *buf << "abcdef";
  buf->advance(2);
  ZuCheck(buf->length == 4);
  ZuCheck(buf->skip == 2);
  ZtString<> s;
  s << ZuCSpan(buf->cspan());
  ZuCheck(s == "cdef");

  buf->rewind(1);
  ZuCheck(buf->length == 5);
  ZuCheck(buf->skip == 1);
  s.clear();
  s << ZuCSpan(buf->cspan());
  ZuCheck(s == "bcdef");

  buf->advance(1000);
  ZuCheck(buf->length == 0);
}

void testPrependAndGrow()
{
  ZuTestScope(testPrependAndGrow);

  ZiMultiplex allocatorMx;
  auto allocator = allocatorMx.allocator<
    Zi::IOBufType::App, ZiIOBuf, 8, ZiIOBuf_DefltAlign, ZiIOBuf_HeapID, 4096>();
  ZmRef<ZiIOBuf> buf = allocator->allocBuf();
  buf->clear();
  buf->skip = buf->size;
  uint8_t *tail = buf->prepend(4);
  memcpy(tail, "tail", 4);

  auto oldSize = buf->size;

  // Force a prepend requiring growth.
  buf->prepend(32);
  ZuCheck(buf->length == 36);
  ZuCheck(buf->size >= oldSize);
}

void testSpanOffsetAndLargeEnsure()
{
  ZuTestScope(testSpanOffsetAndLargeEnsure);

  ZiMultiplex allocatorMx;
  auto allocator = allocatorMx.allocator<
    Zi::IOBufType::App, ZiIOBuf, 16, ZiIOBuf_DefltAlign, ZiIOBuf_HeapID, 1<<20>();
  ZmRef<ZiIOBuf> buf = allocator->allocBuf();
  *buf << "0123456789";

  auto s = buf->cspan(1000);
  ZuCheck(s.length() == 0);

  unsigned large = 256 * 1024;
  ZuCheck(buf->ensure(large));
  ZuCheck(buf->size >= large);

  ZtString<> out;
  out << ZuCSpan(buf->cspan());
  ZuCheck(out == "0123456789");
}

void testPrependTailRoom()
{
  ZuTestScope(testPrependTailRoom);

  ZiMultiplex allocatorMx;
  auto allocator = allocatorMx.allocator<
    Zi::IOBufType::App, ZiIOBuf, 8, ZiIOBuf_DefltAlign, ZiIOBuf_HeapID, 4096>();
  ZmRef<ZiIOBuf> buf = allocator->allocBuf();
  buf->owner = &buf;
  auto data = buf->data_();
  auto size = buf->size;
  buf->skip = 8;
  buf->length = 4;
  memcpy(buf->data(), "tail", 4);
  auto end = buf->end();
  auto avail = buf->avail();
  memcpy(buf->prepend(4), "head", 4);
  ZuCheck(buf->cspan() == "headtail");
  ZuCheck(buf->data_() == data && buf->size == size);
  ZuCheck(buf->end() == end && buf->avail() == avail);
  ZuCheck(buf->owner == &buf);

  // Insufficient headroom, but the combined free space avoids allocation.
  buf->skip = 2;
  buf->length = 4;
  memcpy(buf->data(), "tail", 4);
  memcpy(buf->prepend(4), "head", 4);
  ZuCheck(buf->cspan() == "headtail");
  ZuCheck(buf->data_() == data && buf->size == size);
  ZuCheck(buf->skip + buf->length == size && buf->owner == &buf);

  // Existing headroom must not be subtracted from the required capacity.
  buf->skip = 2;
  buf->length = size - buf->skip;
  auto length = buf->length;
  for (unsigned i = 0; i < length; ++i) buf->data()[i] = i;
  memcpy(buf->prepend(4), "head", 4);
  ZuCheck(buf->size >= length + 4 && buf->length == length + 4);
  ZuCheck((ZuBSpan{buf->data(), 4} == "head"));
  bool intact = true;
  for (unsigned i = 0; i < length; ++i)
    if (buf->data()[i + 4] != uint8_t(i)) intact = false;
  ZuCheck(intact && buf->owner == &buf);
}

void testAllocatorPolicy()
{
  ZuTestScope(testAllocatorPolicy);
  ZiMultiplex mx;
  auto allocator =
    mx.allocator<Zi::IOBufType::TxTCP, ZiIOBuf, 32, ZiIOBuf_DefltAlign, ZiIOBuf_HeapID, 64>();
  auto buf = allocator->allocBuf();
  ZuCheck(allocator->mx == &mx && allocator->type == Zi::IOBufType::TxTCP);
  ZuCheck(buf->builtin() && !buf->registered() && buf->size == allocator->size);
  int owner = 0;
  auto length = allocator->size + 1;
  auto large = allocator->allocBuf(&owner, length);
  ZuCheck(large->allocator() == allocator && large->owner == &owner);
  ZuCheck(!large->builtin() && !large->registered() && large->length == length);

  // A packet's exposed capacity must honor MPS even with larger inline backing.
  unsigned mps = allocator->capacity() - 1;
  auto packet = allocator->allocBuf(&owner, mps, mps);
  ZuCheck(bool(packet));
  if (!packet) return;
  ZuCheck(packet->builtin() && packet->size == mps && packet->length == mps);
  ZuCheck(packet->owner == &owner && !packet->avail());
  ZuCheck(!allocator->allocBuf(&owner, mps + 1, mps));
}

struct BufPlugin : Zi::IOBufPlugin {
  uint8_t *registeredPtr = nullptr;
  unsigned registeredAllocs = 0, registeredFrees = 0;
  bool exhausted = false, valid = true;

  BufPlugin() {
    size = 32;
    headRoom = 3;
    tailRoom = 4;
  }

  uint8_t *alloc() override {
    if (exhausted) return nullptr;
    ++registeredAllocs;
    registeredPtr = static_cast<uint8_t *>(malloc(size));
    return registeredPtr;
  }
  void free(uint8_t *ptr) override {
    ++registeredFrees;
    valid &= ptr == registeredPtr;
    ::free(ptr);
  }
};

struct MxPlugin : ZiMxPlugin {
  ZmRef<BufPlugin> backing = new BufPlugin;

  explicit MxPlugin(ZiMultiplex *mx) : ZiMxPlugin(mx) { }

  Zi::IOBufPlugin *allocator(Zi::IOBufType::T, unsigned, unsigned, unsigned) override {
    return backing;
  }
  void start() override { }
  void stop() override { }
  void rx() override { }
  void listen(ZiListenFn, ZiFailFn, ZiConnectFn, ZiIP, uint16_t, unsigned,
    Zi::IOBufAllocator *, ZiCxnOptions) override { }
  void stopListening(ZiIP, uint16_t) override { }
  void connect(ZiConnectFn, ZiFailFn, ZiIPType::T, ZiIP, uint16_t, ZiIP, uint16_t,
    Zi::IOBufAllocator *, ZiCxnOptions) override { }
  void udp(ZiConnectFn, ZiFailFn, ZiIPType::T, ZiIP, uint16_t, ZiIP, uint16_t,
    Zi::IOBufAllocator *, ZiCxnOptions) override { }
  void recv(ZiConnection *) override { }
  void send(ZiConnection *) override { }
  void disconnect(ZiConnection *, bool) override { }
  void close(ZiConnection *, bool) override { }
  void wake() override { }
  TelKey telKey() const override { return {}; }
  void telemetry(Ztc::MxTelemetry &) const override { }
  unsigned allCxns(AllCxnsFn) const override { return 0; }
  unsigned allQueues(Ztc::QueueMgr::AllFn) const override { return 0; }
  void watch(AddCxnFn, DelCxnFn) override { }
  void unwatch() override { }
};

void testPluginBacking(bool grow)
{
  ZuTestScopeRT(testPluginBacking);
  ZiMultiplex mx;
  ZmRef<MxPlugin> transport = new MxPlugin{&mx};
  mx.plugin(transport);
  auto plugin = transport->backing.ptr();
  auto allocator = mx.allocator<Zi::IOBufType::TxTCP, ZiIOBuf, 32>();
  int owner = 0;
  auto buf = allocator->allocBuf(&owner);
  ZuCheckRT(bool(buf));
  if (!buf) return;
  ZuCheckRT(buf->registered() && !buf->builtin() && buf->size == plugin->size);
  ZuCheckRT(buf->skip == plugin->headRoom);
  ZuCheckRT(plugin->registeredAllocs == 1 && !plugin->registeredFrees);
  *buf << "payload";
  if (grow) {
    ZuCheckRT(buf->ensure(128));
    ZuCheckRT(!buf->registered() && !buf->builtin() && buf->size >= 131);
    ZuCheckRT(buf->skip == 3 && buf->cspan() == "payload" && buf->owner == &owner);
    ZuCheckRT(plugin->registeredAllocs == 1 && plugin->registeredFrees == 1);
  }
  buf = nullptr;
  ZuCheckRT(plugin->valid && plugin->registeredAllocs == 1 && plugin->registeredFrees == 1);
}

void testPluginAllocation()
{
  ZuTestScope(testPluginAllocation);
  ZiMultiplex mx;
  ZmRef<MxPlugin> transport = new MxPlugin{&mx};
  mx.plugin(transport);
  auto plugin = transport->backing.ptr();
  auto tx = mx.allocator<Zi::IOBufType::TxTCP, ZiIOBuf, 32>();
  auto rx = mx.allocator<Zi::IOBufType::RxTCP, ZiIOBuf, 32>();
  int owner = 0;
  ZuCheck(!tx->allocBuf(&owner, tx->capacity() + 1));
  ZuCheck(!rx->allocBuf(&owner, 1));
  plugin->exhausted = true;
  ZuCheck(!tx->allocBuf(&owner));
  {
    auto buf = rx->allocBuf(&owner);
    ZuCheck(bool(buf));
    if (!buf) return;
    ZuCheck(buf->registered() && !buf->builtin() && !buf->data_() && !buf->size);
    ZuCheck(buf->owner == &owner && buf->skip == plugin->headRoom);
    ZuCheck(buf->ensure(8) && !buf->registered() && buf->avail() >= 8);
  }
  ZuCheck(!plugin->registeredAllocs && !plugin->registeredFrees);
  plugin->exhausted = false;
  {
    auto buf = tx->allocBuf(&owner, tx->capacity());
    ZuCheck(bool(buf));
    if (!buf) return;
    ZuCheck(buf->registered() && buf->length == tx->capacity());
    ZuCheck(buf->avail() == plugin->tailRoom);
  }
  ZuCheck(plugin->valid && plugin->registeredAllocs == 1 && plugin->registeredFrees == 1);

  // MPS excludes the plugin's reserved bytes on both sides of the payload.
  unsigned mps = tx->capacity() - 1;
  ZuCheck(!tx->allocBuf(&owner, mps + 1, mps));
  {
    auto buf = tx->allocBuf(&owner, mps, mps);
    ZuCheck(bool(buf));
    if (!buf) return;
    ZuCheck(buf->registered() && buf->length == mps);
    ZuCheck(buf->size == mps + plugin->headRoom + plugin->tailRoom);
    ZuCheck(buf->skip == plugin->headRoom && buf->avail() == plugin->tailRoom);
  }
  ZuCheck(plugin->valid && plugin->registeredAllocs == 2 && plugin->registeredFrees == 2);
}

struct HugePrint;
struct HugePrintFn : ZuPrintBuffer {
  static unsigned length(const HugePrint &) { return UINT_MAX; }
  static unsigned print(char *, unsigned, const HugePrint &) { ZmAssert(false); return 0; }
};
struct HugePrint { friend HugePrintFn ZuPrintType(HugePrint *); };
struct FailingDelegate {
  unsigned *calls;
  template <typename S> void print(S &s) const {
    ++*calls;
    s << HugePrint{} << "ignored";
  }
  friend ZuPrintFn ZuPrintType(FailingDelegate *);
};

// The deliberate UINT_MAX append is rejected before reading the short source.
#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ >= 15
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
#pragma GCC diagnostic ignored "-Wstringop-overread"
#endif
void testOutputFailure()
{
  ZuTestScope(testOutputFailure);

  ZiMultiplex allocatorMx;
  auto allocator = allocatorMx.allocator<>();
  ZmRef<ZiIOBuf> buf = allocator->allocBuf();
  *buf << "prefix";
  ZuCheck(!buf->append(reinterpret_cast<const uint8_t *>("x"), UINT_MAX));
  ZuCheck(buf->failed());
  *buf << "ignored" << '!' << 42;
  ZuCheck(buf->cspan() == "prefix");
  ZuCheck(!buf->ensure(buf->length));

  buf->clear();
  ZuCheck(!buf->failed());
  *buf << 'a' << 42;
  ZuCheck(buf->cspan() == "a42");
  unsigned calls = 0;
  *buf << FailingDelegate{&calls};
  ZuCheck(buf->failed() && calls == 1);
  *buf << FailingDelegate{&calls};
  ZuCheck(calls == 1 && buf->cspan() == "a42");

  buf->clear();
  buf->skip = 1;
  *buf << HugePrint{};
  ZuCheck(buf->failed() && !buf->length);
  buf->clear();
  *buf << "prefix";
  auto deniedGrowth = [](unsigned size, unsigned) { return size; };
  ZuCheck(!buf->ensure<deniedGrowth>(buf->size + 1));
  ZuCheck(buf->failed() && buf->cspan() == "prefix");
  *buf << "ignored";
  ZuCheck(buf->cspan() == "prefix");

  buf->clear();
  *buf << "ok";
  ZuCheck(!buf->failed() && buf->cspan() == "ok");
}


#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ >= 15
#pragma GCC diagnostic pop
#endif

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testSliceAndEnsurePreserveSpan);
  ZuTestCall(testAdvanceRewind);
  ZuTestCall(testPrependAndGrow);
  ZuTestCall(testPrependTailRoom);
  ZuTestCall(testSpanOffsetAndLargeEnsure);
  ZuTestCall(testOutputFailure);
  ZuTestCall(testPluginAllocation);
  ZuTestCall(testAllocatorPolicy);
  ZuTestCall(testPluginBacking, false);
  ZuTestCall(testPluginBacking, true);
  return 0;
}
