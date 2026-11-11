//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/Zquic.hh>

using namespace ZuTestUtil;

#define ZQUIC_CHECK_RT(x, ...) ZuCheckRT(x, log_(__VA_ARGS__))

ZuAssert((ZuIsBase<Zquic_::TxPkt, ZiTxBuf>{}));
ZuAssert((!ZuIsBase<Zquic_::BufNode, ZiTxBuf>{}));

struct PacketBacking : Zi::IOBufPlugin {
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
struct PacketPlugin : ZiMxPlugin {
  ZmRef<PacketBacking> backing = new PacketBacking;
  Zi::IOBufType::T requestedType = Zi::IOBufType::App;
  unsigned requestedSize = 0, requestedAlign = 0, requestedCount = 0;

  explicit PacketPlugin(ZiMultiplex *mx) : ZiMxPlugin{mx} { }
  Zi::IOBufPlugin *allocator(
      Zi::IOBufType::T type, unsigned size, unsigned align, unsigned count) override {
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

void testTxPacketGeometry(bool registered)
{
  ZuTestScopeRT(testTxPacketGeometry);

  ZiMultiplex mx;
  ZmRef<PacketPlugin> plugin;
  if (registered) {
    plugin = new PacketPlugin{&mx};
    mx.plugin(plugin);
  }
  auto allocator = Zquic::pktTxAllocator<1536, 4096, "Zquic.Test.Pkt.Tx">(&mx);
  using Heap = ZuStringT<"Zquic.Test.Pkt.Tx">;
  if (registered) {
    ZQUIC_CHECK_RT((dynamic_cast<Zi::IOBufPluginAllocator<Zi::IOBufType::TxUDP,
      Zquic_::TxPkt, 1536, ZiIOBuf_Align, Heap, 4096> *>(allocator)),
      "plugin allocator must preserve Tx node, geometry and heap policy");
    ZQUIC_CHECK_RT(plugin->requestedType == Zi::IOBufType::TxUDP &&
      plugin->requestedSize == 1536 && plugin->requestedAlign == ZiIOBuf_Align &&
      plugin->requestedCount == ZiIOBuf_DefltCount,
      "packet allocator factory request changed");
  } else {
    ZQUIC_CHECK_RT((dynamic_cast<Zi::IOBufBuiltinAllocator<Zi::IOBufType::TxUDP,
      Zquic_::TxPkt, 1536, ZiIOBuf_Align, Heap, 4096> *>(allocator)),
      "builtin allocator must preserve Tx node, geometry and heap policy");
  }
  int owner;
  auto buf = allocator->allocBuf(&owner, 0, PacketBacking::Payload);
  ZQUIC_CHECK_RT(bool(buf), "packet allocation failed");
  if (!buf) return;
  auto pkt = static_cast<Zquic_::TxPkt *>(buf.ptr());
  auto data = buf->data();
  auto skip = registered ? PacketBacking::Head : 0;
  auto tail = registered ? PacketBacking::Tail : 0;
  ZQUIC_CHECK_RT(buf->allocator() == allocator && buf->owner == &owner &&
    buf->builtin() == !registered && buf->registered() == registered &&
    buf->skip == skip && buf->size == skip + PacketBacking::Payload + tail &&
    Zquic::txPktCapacity(buf) == PacketBacking::Payload,
    "packet storage must retain allocator identity and MPS-clamped geometry");
  ZQUIC_CHECK_RT(pkt->ecn == Zquic::EcnMark::NotECT,
    "Tx packet metadata was not constructed");
  ZiSockAddr addr{ZiIP{"127.0.0.1"}, 4433};
  pkt->addr = addr;
  pkt->ecn = Zquic::EcnMark::ECT1;
  uint8_t cid[8] = {0x83, 0x94, 0xc8, 0xf0, 0x3e, 0x51, 0x57, 0x08};
  Zquic::InitialKeyMaterial keys;
  bool derived = Zquic::InitialCrypto::derive(keys, Zquic::CxnID{ZuBSpan{cid, sizeof(cid)}});
  ZQUIC_CHECK_RT(derived, "test packet key derivation failed");
  if (!derived) return;
  uint8_t plaintext[PacketBacking::Payload - Zquic::InitialSecret::TagLen];
  for (unsigned i = 0; i < sizeof(plaintext); ++i) plaintext[i] = uint8_t(i ^ 0x6d);
  int encrypted = Zquic::InitialCrypto::encrypt(
    data, Zquic::txPktCapacity(buf), keys.client, 2, "packet header",
    ZuBSpan{plaintext, sizeof(plaintext)});
  ZQUIC_CHECK_RT(encrypted == PacketBacking::Payload,
    "packet protection must write directly into allocator-provided storage");
  if (encrypted != PacketBacking::Payload) return;
  uint8_t ciphertext[PacketBacking::Payload];
  memcpy(ciphertext, data, sizeof(ciphertext));
  buf->length = PacketBacking::Payload;
  auto packet = buf.ptr();
  ZiTxQueue queue;
  queue.pushNode(ZuMv(buf));
  ZQUIC_CHECK_RT(queue.count_() == 1 && queue.headPtr() == packet,
    "Tx queue must retain the packet node directly");
  buf = queue.shift();
  ZQUIC_CHECK_RT(buf.ptr() == packet && buf->data() == data && buf->skip == skip &&
    buf->allocator() == allocator && buf->owner == &owner &&
    pkt->addr == addr && pkt->ecn == Zquic::EcnMark::ECT1,
    "queueing changed packet backing or destination metadata");
  ZQUIC_CHECK_RT(!memcmp(data, ciphertext, sizeof(ciphertext)) && buf->avail() == tail,
    "queueing changed encrypted packet bytes or reserved tailroom");
  uint8_t decrypted[sizeof(plaintext)];
  int plainLength = Zquic::InitialCrypto::decrypt(
    decrypted, sizeof(decrypted), keys.client, 2, "packet header", buf->cspan());
  ZQUIC_CHECK_RT(plainLength == sizeof(plaintext) &&
    !memcmp(decrypted, plaintext, sizeof(plaintext)),
    "queued protected packet must authenticate and recover its original plaintext");
  buf = nullptr;
  if (registered)
    ZQUIC_CHECK_RT(plugin->backing->intact && plugin->backing->acquired == 1 &&
      plugin->backing->released == 1, "registered packet must retire once with guards intact");
}

void testDiagAggregation()
{
  ZuTestScope(testDiagAggregation);

  Zquic::Diag diag;
  diag.notePktRx(100);
  diag.notePktTx(200);
  diag.noteHdrRx(11);
  diag.noteHdrTx(12);
  diag.noteBodyRx(13);
  diag.noteBodyTx(14);
  diag.noteStreamRx(30);
  diag.noteStreamTx(40);
  diag.noteLoss(Zquic::SentPkt{
    .pn = 1,
    .sentTime = ZuTime{0},
    .bytes = 100,
    .space = Zquic::PktNumSpace::AppData,
    .ackEliciting = true,
    .inFlight = true });
  diag.notePTO();
  diag.noteRetransmit(Zquic::SentFrameRef::control());
  diag.setRecovery(12000, 500);
  diag.setHandshakeState("one_rtt");
  diag.setStreamCounts(3, 2);

  Zquic::BufDiag buf;
  diag.add(buf);

  Zquic::PathDiag path;
  path.probesSent = 2;
  path.probesAckd = 1;
  path.probesLost = 1;
  path.blackholes = 1;
  diag.add(path);

  ZuCHECK(Zquic::Diag::packetSpaceName(Zquic::PktNumSpace::AppData) ==
      "AppData" &&
      Zquic::Diag::frameTypeName(Zquic::FrameType::Stream) == "Stream" &&
      Zquic::Diag::streamTypeName(Zquic::StreamType::Simplex) == "Simplex",
    "diagnostic stable names mismatch");

#ifdef Zquic_DEBUG
  ZuCHECK(diag.packetsRx == 1 &&
      diag.packetsTx == 1 &&
      diag.bytesRx == 100 &&
      diag.bytesTx == 200 &&
      diag.headerBytesRx == 11 &&
      diag.headerBytesTx == 12 &&
      diag.bodyBytesRx == 13 &&
      diag.bodyBytesTx == 14 &&
      diag.streamBytesRx == 30 &&
      diag.streamBytesTx == 40 &&
      diag.packetsLost == 1 &&
      diag.ptoCount == 1 &&
      diag.retransmittedFrames == 1 &&
      diag.cwnd == 12000 &&
      diag.bytesInFlight == 500 &&
      diag.handshakeState == "one_rtt" &&
      diag.openStreams == 3 &&
      diag.closedStreams == 2 &&
      diag.pmtudProbes == 2 &&
      diag.pmtudSuccess == 1 &&
      diag.pmtudFailure == 2,
    "diagnostic aggregation mismatch");

  Zquic::DiagText summary = diag.summary();
  ZuCHECK(strstr(summary.data(), "packetsRx=1") &&
      strstr(summary.data(), "headerBytesRx=11") &&
      strstr(summary.data(), "bodyBytesTx=14") &&
      strstr(summary.data(), "cwnd=12000") &&
      strstr(summary.data(), "handshakeState=one_rtt") &&
      strstr(summary.data(), "pmtudFailure=2"),
    "diagnostic summary text mismatch");

  Zquic::DiagText recovery = diag.recoverySummary();
  ZuCHECK(strstr(recovery.data(), "bytesInFlight=500") &&
      strstr(recovery.data(), "retransmittedFrames=1"),
    "diagnostic recovery summary mismatch");

  Zquic::DiagText streams = diag.streamSummary();
  ZuCHECK(strstr(streams.data(), "openStreams=3") &&
      strstr(streams.data(), "headerBytesTx=12") &&
      strstr(streams.data(), "bodyBytesRx=13"),
    "diagnostic stream summary mismatch");
#else
  ZuCHECK(diag.packetsRx == 0 &&
      diag.packetsTx == 0 &&
      diag.bytesRx == 0 &&
      diag.bytesTx == 0 &&
      diag.packetsLost == 0 &&
      diag.ptoCount == 0 &&
      diag.retransmittedFrames == 0,
    "release diagnostics should be inert");

  ZuCHECK(!diag.summary() &&
      !diag.recoverySummary() &&
      !diag.streamSummary(),
    "release diagnostic summaries should be empty");
#endif
}

void testPktBuildMetadata()
{
  ZuTestScopeRT(testPktBuildMetadata);

  uint8_t byte = 0x5a;
  Zquic::PktBuild build;
  for (unsigned i = 0; i < Zquic::PlainVec::Max; ++i)
    ZQUIC_CHECK_RT(build.add(ZuBSpan{&byte, 1}),
      "packet plaintext vector append failed");
  ZQUIC_CHECK_RT(build.count() == Zquic::PlainVec::Max &&
      build.bytes() == Zquic::PlainVec::Max &&
      !build.add(ZuBSpan{&byte, 1}) &&
      !build.pad(1),
    "packet plaintext vector capacity was not enforced");

  build.reset();
  ZQUIC_CHECK_RT(build.scratchAvail() == Zquic::PktBuildScratchSize &&
      build.padTo(Zquic::MinUDPPayload) &&
      build.count() == 1 &&
      build.bytes() == Zquic::MinUDPPayload &&
      build.data()[0].base[0] == 0 &&
      build.data()[0].base[Zquic::MinUDPPayload - 1] == 0,
    "QUIC Initial minimum padding metadata mismatch");

  build.reset();
  ZQUIC_CHECK_RT(build.padForProtSample(1, 16) &&
      build.count() == 1 &&
      build.bytes() == 3,
    "packet protection sample padding metadata mismatch");
}

void testPacketAllocRoles()
{
  ZuTestScope(testPacketAllocRoles);

  ZiMultiplex mx;
  auto rx = Zquic::pktRxAllocator(&mx);
  auto tx = Zquic::pktTxAllocator(&mx);
  auto streamRx = mx.allocator<Zi::IOBufType::RxTLS>();
  auto packet = rx->allocBuf();
  auto wire = tx->allocBuf();
  ZuCHECK(packet && wire, "packet buffer allocation failed");
  if (!packet || !wire) return;
  ZuCHECK(packet->avail() >= Zquic::BufSize && wire->avail() >= Zquic::BufSize,
    "UDP recvmsg metadata must not reduce packet payload capacity");
  ZuCHECK(rx->type == Zi::IOBufType::RxUDP &&
      tx->type == Zi::IOBufType::TxUDP,
    "packet allocator roles mismatch");
  packet->data()[0] = 0x5a;
  packet->data()[1] = 0xa5;
  ZmRef<ZiIOBuf> observer = packet;
  auto backing = packet.ptr();
  {
    Zquic::StreamRxData first{packet, packet->data(), 1, streamRx, nullptr, 0};
    Zquic::StreamRxData second{packet, packet->data() + 1, 1, streamRx, nullptr, 1};
    packet = nullptr;
    ZuCHECK(first.packet.ptr() == backing && second.packet.ptr() == backing &&
	first.span()[0] == 0x5a && second.span()[0] == 0xa5 && backing->refCount() == 3,
      "STREAM slices must retain original packet backing");
    first.packet = nullptr;
    ZuCHECK(second.packet.ptr() == backing && second.span()[0] == 0xa5 &&
	backing->refCount() == 2,
      "one STREAM release must not recycle another STREAM's packet");
    ZuCHECK(second.ensure(second.size + 1) && !second.builtin() &&
	second.packet.ptr() == backing && second.span()[0] == 0xa5 &&
	backing->refCount() == 2,
      "grown STREAM descriptor must preserve bytes and its packet lifetime");
  }
  ZuCHECK(observer->refCount() == 1 && observer->data()[1] == 0xa5,
    "STREAM descriptor teardown must release exactly its retained packet reference");
  observer = nullptr;

  auto stream = Zquic::plaintextAllocator<Zquic::StreamTxBufAlloc<>>(&mx)->allocBuf();
  auto crypto = Zquic::plaintextAllocator<Zquic::CryptoTxBufAlloc<>>(&mx)->allocBuf();
  auto reassembly = Zquic::plaintextAllocator<Zquic::CryptoRxBufAlloc<>>(&mx)->allocBuf();
  ZuCHECK(stream->allocator()->type == Zi::IOBufType::TxTLS &&
      crypto->allocator()->type == Zi::IOBufType::TxTLS &&
      reassembly->allocator()->type == Zi::IOBufType::RxTLS,
    "retained plaintext allocator roles mismatch");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testTxPacketGeometry, false);
  ZuTestCall(testTxPacketGeometry, true);
  ZuTestCall(testPacketAllocRoles);
  ZuTestCall(testDiagAggregation);
  ZuTestCall(testPktBuildMetadata);
}
