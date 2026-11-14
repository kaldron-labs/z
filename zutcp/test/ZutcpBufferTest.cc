//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmThread.hh>
#include <zlib/Zutcp.hh>
#include "ZutcpTestAlloc.hh"

using namespace ZuTestUtil;

#define ZUTCP_CHECK_RT(x, ...) ZuCheckRT(x, log_(__VA_ARGS__))


struct PacketBacking : ZmNHeap<PacketBacking>, Zi::IOBufPlugin {
friend inline ZuStringT<"Zutcp.Test.PacketBacking"> ZmNHeapID(PacketBacking *);
  using ZmNHeap<PacketBacking>::operator new;
  using ZmNHeap<PacketBacking>::operator delete;
  enum { Size = 1600, Head = 17, Tail = 11, Payload = 64 };
  alignas(ZiIOBuf_Align) uint8_t bytes[Size];
  unsigned acquired = 0, released = 0;
  bool intact = true;

  PacketBacking() {
    size = Size;
    headRoom = Head;
    tailRoom = Tail;
  }
  uint8_t *alloc() override {
    if (acquired != released) return nullptr;
    ++acquired;
    memset(bytes, 0xa5, Size);
    return bytes;
  }
  void free(uint8_t *ptr) override {
    intact &= ptr == bytes && acquired == released + 1;
    for (unsigned i = 0; i < Head; ++i) intact &= bytes[i] == 0xa5;
    for (unsigned i = Head + Payload; i < Size; ++i) intact &= bytes[i] == 0xa5;
    ++released;
  }
};

// Exercise the ordinary mx factory, including its construction of Tx metadata.
struct PacketPlugin : ZmNHeap<PacketPlugin>, ZiMxPlugin {
friend inline ZuStringT<"Zutcp.Test.PacketPlugin"> ZmNHeapID(PacketPlugin *);
  using ZmNHeap<PacketPlugin>::operator new;
  using ZmNHeap<PacketPlugin>::operator delete;
  ZmRef<PacketBacking> backing = new PacketBacking;
  Zi::IOBufType::T requestedType = Zi::IOBufType::App;
  unsigned requestedSize = 0, requestedAlign = 0, requestedCount = 0, requests = 0;

  explicit PacketPlugin(ZiMultiplex *mx) : ZiMxPlugin{mx} { }
  Zi::IOBufPlugin *allocator(
      Zi::IOBufType::T type, unsigned size, unsigned align, unsigned count) override {
    ++requests;
    requestedType = type;
    requestedSize = size;
    requestedAlign = align;
    requestedCount = count;
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

static void testAllocatorDirection()
{
  ZuTestScope(testAllocatorDirection);
  ZiMultiplex mx;
  ZmRef<PacketPlugin> provider = new PacketPlugin{&mx};
  mx.plugin(provider);
  struct App : Zutcp::Hub<App> { };
  App app;
  Zutcp::HubParams defaults{&mx};
  ZuCHECK(defaults.controlBufCount() == 256 && defaults.txControlCredits() == 64,
    "control reserve defaults mismatch");
  ZuCHECK(defaults.rxRetainFrames() == 1024 && defaults.rxRetainFramesPerCxn() == 256 &&
      defaults.rxReserveFrames() == 64, "Rx retention defaults mismatch");
  ZuCHECK(defaults.rtoParams().initial == ZuTime{1} &&
      defaults.rtoParams().min == Zutcp::timeUS(200000) &&
      defaults.rtoParams().max == ZuTime{60} &&
      defaults.rtoParams().granularity == Zutcp::timeUS(1000),
    "DATA RTO defaults mismatch");
  unsigned errors = 0;
  auto invalid = Zutcp::HubParams{&mx}.controlBufCount(0)
    .errorFn([&errors](ZeException) { ++errors; });
  auto noCredits = Zutcp::HubParams{&mx}.txControlCredits(0)
    .errorFn([&errors](ZeException) { ++errors; });
  auto noRx = Zutcp::HubParams{&mx}.rxRetainFrames(0)
    .errorFn([&errors](ZeException) { ++errors; });
  auto noCxn = Zutcp::HubParams{&mx}.rxRetainFramesPerCxn(0)
    .errorFn([&errors](ZeException) { ++errors; });
  auto noReserve = Zutcp::HubParams{&mx}.rxReserveFrames(0)
    .errorFn([&errors](ZeException) { ++errors; });
  auto noRto = Zutcp::HubParams{&mx}.minRto(ZuTime{0})
    .errorFn([&errors](ZeException) { ++errors; });
  ZuCHECK(!app.init(ZuMv(invalid)) && !app.init(ZuMv(noCredits)) &&
      !app.init(ZuMv(noRx)) && !app.init(ZuMv(noCxn)) && !app.init(ZuMv(noReserve)) &&
      !app.init(ZuMv(noRto)) && errors == 6 &&
      !provider->requests,
    "invalid control reserves must fail before requesting a provider");
  defaults.initialRto(ZuTime{2}).minRto(Zutcp::timeUS(100000))
    .maxRto(ZuTime{30}).rtoGranularity(Zutcp::timeUS(2000));
  bool initialized = app.init(defaults);
  ZuCHECK(initialized && app.rtoParams().initial == ZuTime{2} &&
      app.rtoParams().min == Zutcp::timeUS(100000) &&
      app.rtoParams().max == ZuTime{30} &&
      app.rtoParams().granularity == Zutcp::timeUS(2000),
    "Hub must forward configured RTO policy");
  ZuCHECK(initialized && !provider->requests,
    "transport initialization makes no request to the mx allocator factory");
  if (!initialized) return;
  ZuCHECK(!app.dataTxAllocator() && !provider->requests,
    "a non-native plugin cannot supply transport producer storage");
  auto application = mx.allocator<Zi::IOBufType::TxTCP>();
  ZuCHECK(application && provider->requests == 1 &&
      provider->requestedType == Zi::IOBufType::TxTCP &&
      application->provider() == provider->backing.ptr(),
    "an application allocator exposes its original provider without another factory request");
  app.final();
}

void testTxPacketGeometry(bool registered)
{
  ZuTestScopeRT(testTxPacketGeometry);

  ZiMultiplex mx;
  ZmRef<PacketPlugin> plugin;
  if (registered) {
    plugin = new PacketPlugin{&mx};
    mx.plugin(plugin);
  }
  auto allocator = ZutcpTest::pktTxAllocator<1536, 4096, "Zutcp.Test.Pkt.Tx">(&mx);
  using Heap = ZuStringT<"Zutcp.Test.Pkt.Tx">;
  if (registered) {
    ZUTCP_CHECK_RT((dynamic_cast<Zi::IOBufPluginAllocator<Zi::IOBufType::TxTCP,
      Zutcp_::BufNode, 1536, ZiIOBuf_Align, Heap, 4096> *>(allocator)),
      "plugin allocator must preserve Tx node, geometry and heap policy");
    ZUTCP_CHECK_RT(plugin->requestedType == Zi::IOBufType::TxTCP &&
      plugin->requestedSize == 1536 && plugin->requestedAlign == ZiIOBuf_Align &&
      plugin->requestedCount == ZiIOBuf_DefltCount,
      "packet allocator factory request changed");
  } else {
    ZUTCP_CHECK_RT((dynamic_cast<Zi::IOBufBuiltinAllocator<Zi::IOBufType::TxTCP,
      Zutcp_::BufNode, 1536, ZiIOBuf_Align, Heap, 4096> *>(allocator)),
      "builtin allocator must preserve Tx node, geometry and heap policy");
  }
  int owner;
  auto buf = allocator->allocBuf(&owner, 0, PacketBacking::Payload);
  ZUTCP_CHECK_RT(bool(buf), "packet allocation failed");
  if (!buf) return;
  auto data = buf->data();
  auto skip = registered ? PacketBacking::Head : 0;
  auto tail = registered ? PacketBacking::Tail : 0;
  ZUTCP_CHECK_RT(buf->allocator() == allocator && buf->owner == &owner &&
    buf->builtin() == !registered && buf->registered() == registered &&
    buf->skip == skip && buf->size == skip + PacketBacking::Payload + tail &&
    Zutcp::txPktCapacity(buf) == PacketBacking::Payload,
    "packet storage must retain allocator identity and MPS-clamped geometry");
  uint8_t expected[PacketBacking::Payload];
  for (unsigned i = 0; i < sizeof(expected); ++i) expected[i] = uint8_t(i ^ 0x6d);
  memcpy(data, expected, sizeof(expected));
  buf->length = PacketBacking::Payload;
  ZUTCP_CHECK_RT(!memcmp(data, expected, sizeof(expected)) && buf->avail() == tail,
    "packet construction changed bytes or reserved tailroom");
  buf = nullptr;
  if (registered)
    ZUTCP_CHECK_RT(plugin->backing->intact && plugin->backing->acquired == 1 &&
      plugin->backing->released == 1, "registered packet must retire once with guards intact");
}

void testDiagAggregation()
{
  ZuTestScope(testDiagAggregation);

  Zutcp::Diag diag;
  diag.notePktRx(100);
  diag.notePktTx(200);
  diag.noteHdrRx(11);
  diag.noteHdrTx(12);
  diag.noteBodyRx(13);
  diag.noteBodyTx(14);
  diag.noteDataRx(30);
  diag.noteDataTx(40);
  diag.noteLoss(Zutcp::SentPkt{
    .pn = 1,
    .sentTime = ZuTime{0},
    .bytes = 100,
    .ackEliciting = true,
    .inFlight = true });
  diag.noteSYNRetransmit();
  diag.noteRetransmit(Zutcp::SentFrameRef::data({}, true));
  diag.setRecovery(12000, 500);
  diag.setHandshakeState("established");

  Zutcp::BufDiag buf;
  diag.add(buf);

  Zutcp::PathDiag path;
  path.probesSent = 2;
  path.probesAckd = 1;
  path.probesLost = 1;
  path.blackholes = 1;
  diag.add(path);

#ifdef Zutcp_DEBUG
  ZuCHECK(diag.packetsRx == 1 &&
      diag.packetsTx == 1 &&
      diag.bytesRx == 100 &&
      diag.bytesTx == 200 &&
      diag.headerBytesRx == 11 &&
      diag.headerBytesTx == 12 &&
      diag.bodyBytesRx == 13 &&
      diag.bodyBytesTx == 14 &&
      diag.dataBytesRx == 30 &&
      diag.dataBytesTx == 40 &&
      diag.packetsLost == 1 &&
      diag.synRetries == 1 &&
      diag.retransmittedRanges == 1 &&
      diag.cwnd == 12000 &&
      diag.bytesInFlight == 500 &&
      diag.handshakeState == "established" &&
      diag.pmtudProbes == 2 &&
      diag.pmtudSuccess == 1 &&
      diag.pmtudFailure == 2,
    "diagnostic aggregation mismatch");

  Zutcp::DiagText summary = diag.summary();
  ZuCHECK(strstr(summary.data(), "packetsRx=1") &&
      strstr(summary.data(), "headerBytesRx=11") &&
      strstr(summary.data(), "bodyBytesTx=14") &&
      strstr(summary.data(), "cwnd=12000") &&
      strstr(summary.data(), "handshakeState=established") &&
      strstr(summary.data(), "pmtudFailure=2"),
    "diagnostic summary text mismatch");

  Zutcp::DiagText recovery = diag.recoverySummary();
  ZuCHECK(strstr(recovery.data(), "bytesInFlight=500") &&
      strstr(recovery.data(), "retransmittedRanges=1"),
    "diagnostic recovery summary mismatch");

  Zutcp::DiagText streams = diag.dataSummary();
  ZuCHECK(strstr(streams.data(), "dataBytesRx=30") &&
      strstr(streams.data(), "headerBytesTx=12") &&
      strstr(streams.data(), "bodyBytesRx=13"),
    "diagnostic stream summary mismatch");
#else
  ZuCHECK(diag.packetsRx == 0 &&
      diag.packetsTx == 0 &&
      diag.bytesRx == 0 &&
      diag.bytesTx == 0 &&
      diag.packetsLost == 0 &&
      diag.synRetries == 0 &&
      diag.retransmittedRanges == 0,
    "release diagnostics should be inert");

  ZuCHECK(!diag.summary() &&
      !diag.recoverySummary() &&
      !diag.dataSummary(),
    "release diagnostic summaries should be empty");
#endif
}

void testPacketAllocRoles()
{
  ZuTestScope(testPacketAllocRoles);

  ZiMultiplex mx;
  auto rx = ZutcpTest::pktRxAllocator(&mx);
  auto tx = ZutcpTest::pktTxAllocator(&mx);
  auto streamRx = mx.allocator<Zi::IOBufType::App>();
  auto packet = rx->allocBuf();
  auto wire = tx->allocBuf();
  ZuCHECK(packet && wire, "packet buffer allocation failed");
  if (!packet || !wire) return;
  ZuCHECK(packet->avail() >= Zutcp::BufSize && wire->avail() >= Zutcp::BufSize,
    "native receive metadata must not reduce packet payload capacity");
  ZuCHECK(rx->type == Zi::IOBufType::RxTCP &&
      tx->type == Zi::IOBufType::TxTCP,
    "packet allocator roles mismatch");
  packet->data()[0] = 0x5a;
  packet->data()[1] = 0xa5;
  ZmRef<ZiIOBuf> observer = packet;
  auto backing = packet.ptr();
  {
    Zutcp::DataRx first{packet, packet->data(), 1, streamRx, nullptr, 0};
    Zutcp::DataRx second{packet, packet->data() + 1, 1, streamRx, nullptr, 1};
    packet = nullptr;
    ZuCHECK(first.slice.packet.ptr() == backing && second.slice.packet.ptr() == backing &&
	first.span()[0] == 0x5a && second.span()[0] == 0xa5 && backing->refCount() == 3,
      "byte slices must retain original packet backing");
    ZuCHECK(first.ensure(first.size + 1) && !first.slice.packet &&
	first.span()[0] == 0x5a,
      "heap replacement must release the first slice's packet reference");
    ZuCHECK(second.slice.packet.ptr() == backing && second.span()[0] == 0xa5 &&
	backing->refCount() == 2,
      "one byte release must not recycle another byte's packet");
    ZuCHECK(second.ensure(second.size + 1) && !second.builtin() &&
	!second.slice.packet && second.span()[0] == 0xa5 &&
	backing->refCount() == 1,
      "grown receive descriptor must preserve bytes and release packet backing");
  }
  ZuCHECK(observer->refCount() == 1 && observer->data()[1] == 0xa5,
    "byte descriptor teardown must release exactly its retained packet reference");
  observer = nullptr;

  auto stream = ZutcpTest::plaintextAllocator<Zutcp::TxBufAlloc<>>(&mx)->allocBuf();
  ZuCHECK(stream->allocator()->type == Zi::IOBufType::App,
    "retained data allocator role mismatch");
}

static void testRxSliceReturn()
{
  ZuTestScopeRT(testRxSliceReturn);
  ZiMultiplex mx;
  ZmRef<PacketPlugin> plugin = new PacketPlugin{&mx};
  mx.plugin(plugin);
  auto allocator = ZutcpTest::pktTxAllocator<>(&mx);
  auto heap = mx.allocator<Zi::IOBufType::App>();
  auto packet = allocator->allocBuf(nullptr, PacketBacking::Payload);
  ZUTCP_CHECK_RT(packet && packet->registered(), "registered fixture allocation failed");
  if (!packet) return;
  memset(packet->data(), 0x6d, PacketBacking::Payload);
  {
    Zutcp::DataRx first{packet, packet->data(), 32, heap, nullptr, 0};
    Zutcp::DataRx second{packet, packet->data() + 32, 32, heap, nullptr, 32};
    packet = nullptr;
    ++first.skip;
    --first.length;
    ZUTCP_CHECK_RT(first.ensure(64) && !first.registered() &&
	first.length == 31 && first.span()[0] == 0x6d && !plugin->backing->released,
      "replacing one alias must preserve its view and keep the other alias alive");
    bool copied = false;
    ZmThread worker{[&second, &copied]() {
      copied = second.ensure(64) && !second.registered() &&
	second.length == 32 && second.span()[31] == 0x6d;
    }};
    worker.join();
    ZUTCP_CHECK_RT(copied && plugin->backing->released == 1 && plugin->backing->intact,
      "off-owner replacement of the final alias must return backing immediately once");
    ZUTCP_CHECK_RT(first.ensure(128) && second.ensure(128),
      "already heap-backed slices must support subsequent growth");
  }
  ZUTCP_CHECK_RT(plugin->backing->released == 1,
    "heap descriptor destruction must not return original backing again");
  packet = allocator->allocBuf(nullptr, PacketBacking::Payload);
  ZUTCP_CHECK_RT(packet && plugin->backing->acquired == 2,
    "returned backing must be immediately reusable");
  if (!packet) return;
  memset(packet->data(), 0x6d, PacketBacking::Payload);
  {
    Zutcp::DataRx slice{packet, packet->data(), 32, heap, nullptr, 0};
    packet = nullptr;
  }
  ZUTCP_CHECK_RT(plugin->backing->released == 2 && plugin->backing->intact,
    "a slice destroyed without growth must return backing once");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testAllocatorDirection);
  ZuTestCall(testTxPacketGeometry, false);
  ZuTestCall(testTxPacketGeometry, true);
  ZuTestCall(testPacketAllocRoles);
  ZuTestCall(testRxSliceReturn);
  ZuTestCall(testDiagAggregation);
}
