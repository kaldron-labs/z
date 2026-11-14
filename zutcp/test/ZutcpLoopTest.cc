//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>
#include <errno.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZiLog.hh>
#include <zlib/Zutcp.hh>
#include "ZutcpTestAlloc.hh"

using namespace ZuTestUtil;

#define ZUTCP_CHECK_RT(x, ...) ZuCheckRT(x, log_(__VA_ARGS__))

namespace {

using TxBufAlloc = Zutcp::TxBufAlloc<>;

struct App : public Zutcp::Hub<App> {
  using Base = Zutcp::Hub<App>;
  Zi::IOBufAllocator *dataTxAllocator() const { return Zutcp::inlineAllocator(); }

  App() : m_mx{mxParams_()} {
    bool ok = Base::init(
      Zutcp::HubParams(&m_mx).maxPayload(Zutcp::BufSize));
    ZiAssert(ok, "Zutcp", (), "loop test app init failed", return);
    ZiAssert(m_mx.start(), "Zutcp", (),
      "loop test multiplexer start failed", return);
  }
  ~App() {
    Base::final();
    m_mx.stop();
  }

  bool rxInvoked() const { return true; }
  bool txInvoked() const { return true; }
  template <typename L> void rxRun(L l) { l(); }
  template <typename L> void rxInvoke(L l) { l(); }
  template <typename L> void txRun(L l) { l(); }
  template <typename L> void txInvoke(L l) { l(); }
  template <typename O, typename L> void txInvoke(O *, L l) { l(); }

private:
  static ZiMxParams mxParams_() {
    return ZiMxParams()
      .scheduler([](auto &s) {
	s.nThreads(4);
      })
      .rxThread(1).txThread(2);
  }

  ZiMultiplex	m_mx;
};
struct TestLink;
struct TestLink :
  public ZmNHeap<TestLink>,
  public Zutcp::Connection<App, TestLink,
    TxBufAlloc>
{
  using Base = Zutcp::Connection<App, TestLink,
    TxBufAlloc>;
  int process(Zutcp::Reader &) { return 0; }
  friend inline ZuStringT<"Zutcp.Loop.Link"> ZmNHeapID(TestLink *);
  using ZmNHeap<TestLink>::operator new;
  using ZmNHeap<TestLink>::operator delete;
  TestLink(App *app) : Base{app, false, ZuID{"client"}} { configLocalParams(app); }
  TestLink(App *app, bool isServer) : Base{app, isServer} { configLocalParams(app); }
  void flushTx_() { }
  void queueTxFlush() { }
  void pto_() { }
  void queueRetransmit() { }
  bool retransmit_(bool = false, unsigned = 2,
    Zutcp::PktNumSpace::T = Zutcp::PktNumSpace::N) { return false; }
  void initPath(ZiSockAddr local, ZiSockAddr remote) {
    Base::initClientPathTx_(ZuMv(local), ZuMv(remote));
  }
  void closeForTest(uint64_t errorCode = 0) {
    Base::closeState(errorCode);
  }
  bool recordTx(unsigned bytes) {
    return Base::recordPktTx(
      Zutcp::PktNumSpace::AppData, 0, bytes, {}, nullptr, false);
  }
  void receiveDatagram(unsigned bytes) {
    ZmRef<ZiIOBuf> buf = ([](void *owner) {
      static Zi::IOBufAllocator *allocator =
	([] { static ZiMultiplex mx; return ZutcpTest::pktRxAllocator(&mx); })();
      return allocator->allocBuf(owner, 0);
    })(this);
    if (ZuUnlikely(!buf)) return;
    memset(buf->data_(), 0, bytes);
    buf->skip = 0;
    buf->length = bytes;
    Base::receiveDatagram_(
      Zutcp::Datagram{ZuMv(buf), {}},
      [](Zutcp::Datagram &, unsigned, unsigned) { return true; },
      [](Zutcp::Datagram &, unsigned, unsigned) { return true; });
  }
  bool receivePayload(ZmRef<ZiIOBuf> buf, unsigned longBytes) {
    auto original = buf.ptr();
    auto payload = buf->data();
    unsigned length = buf->length, longs = 0, shorts = 0;
    bool geometry = true;
    Base::receiveDatagram_(Zutcp::Datagram{ZuMv(buf), {}},
      [&geometry, &longs, original, payload, longBytes](
	  Zutcp::Datagram &d, unsigned offset, unsigned bytes) {
	++longs;
	geometry &= d.buf.ptr() == original && d.buf->data() == payload &&
	  !offset && bytes == longBytes;
	return true;
      },
      [&geometry, &shorts, original, payload, longBytes, length](
	  Zutcp::Datagram &d, unsigned offset, unsigned bytes) {
	++shorts;
	geometry &= d.buf.ptr() == original && d.buf->data() == payload &&
	  offset == longBytes && bytes == length - longBytes;
	return true;
      });
    return geometry && longs == 1 && shorts == 1;
  }
  uint64_t txQueueCount() const { return Base::txQueueCount_(); }
#ifdef Zutcp_DEBUG
  void growActivePath(unsigned size) { Base::forceActivePathMTU(size); }
  bool startPMTUDProbe(unsigned size) {
    return Base::startPMTUDProbe_(size);
  }
  void ackPMTUDProbe(unsigned size) {
    Base::ackPMTUDProbe_(size);
  }
  void losePMTUDProbe(unsigned size) {
    Base::losePMTUDProbe_(size);
  }
  void expirePMTUDProbe() { Base::expirePMTUDProbe_(); }
  void applyPathHint(Zutcp::PathHint hint) {
    Base::applyPathHint_(hint);
  }
  unsigned activePathMaxPayload() const {
    return Base::activePathMaxPayload_();
  }
  unsigned pathProbeSize() const {
    return Base::pathProbeSize_();
  }
  bool pathProbeRetryPending() const {
    return Base::pathProbeRetryPending_();
  }
  Zutcp::PathDiag pathDiag() const {
    return Base::pathDiag_();
  }
  Zutcp::PktBudget sendBudget() const { return Base::sendBudget_(); }
#endif
};

template <typename Link>
void queueTelemetry(
  Link &link, Ztc::QueueTelemetry &rx, Ztc::QueueTelemetry &tx)
{
  auto linkKey = link.telKey();
  unsigned count = 0;
  unsigned allQueues = link.allQueues(
      [&linkKey, &rx, &tx, &count](Ztc::Queue *queue) {
    auto key = queue->telKey();
    ZUTCP_CHECK_RT(key.template p<0>() == linkKey.template p<0>(),
      "telemetry queue owner ID mismatch");
    ZUTCP_CHECK_RT(key.template p<1>() == linkKey.template p<1>(),
      "telemetry queue ID mismatch");
    ZUTCP_CHECK_RT(key.template p<2>() ==
	(count ? Ztc::QueueType::Tx : Ztc::QueueType::Rx),
      "telemetry queue iteration order mismatch");

    Ztc::QueueTelemetry data;
    data.ownerID = ZuID{} << "dirtyOwner";
    data.id = ZuID{} << "dirty";
    data.inBytes = data.outBytes = data.inCount = data.outCount = 1;
    data.count = data.size = data.full = 1;
    data.type = Ztc::QueueType::Thread;
    queue->telemetry(data);
    ZUTCP_CHECK_RT(data.ownerID == key.template p<0>(),
      "queue telemetry owner ID mismatch");
    ZUTCP_CHECK_RT(data.id == key.template p<1>(),
      "queue telemetry ID mismatch");
    ZUTCP_CHECK_RT(data.type == key.template p<2>(),
      "queue telemetry type mismatch");
    ZUTCP_CHECK_RT(!data.size && !data.full,
      "queue telemetry fields were not overwritten");
    if (data.type == Ztc::QueueType::Rx) {
      ZUTCP_CHECK_RT(!data.outBytes && !data.outCount,
	"Rx telemetry output fields were not overwritten");
      rx = data;
    } else {
      ZUTCP_CHECK_RT(!data.inBytes && !data.inCount,
	"Tx telemetry input fields were not overwritten");
      tx = data;
    }
    ++count;
  });
  ZUTCP_CHECK_RT(allQueues == 2, "allQueues returned incorrect count");
  ZUTCP_CHECK_RT(count == allQueues,
    "allQueues did not enumerate returned count");
  unsigned repeat = 0;
  link.allQueues([&repeat](Ztc::Queue *queue) {
    auto key = queue->telKey();
    ZUTCP_CHECK_RT(key.template p<2>() ==
	(repeat++ ? Ztc::QueueType::Tx : Ztc::QueueType::Rx),
      "allQueues queue order is not stable");
  });
  ZUTCP_CHECK_RT(rx.ownerID == linkKey.template p<0>() &&
      tx.ownerID == linkKey.template p<0>(),
    "queue owner ID mismatch");
  ZUTCP_CHECK_RT(rx.id == linkKey.template p<1>() &&
      tx.id == linkKey.template p<1>(),
    "queue ID mismatch");
  ZUTCP_CHECK_RT(rx.type != tx.type,
    "Rx and Tx queue types do not discriminate the keys");
}

template <typename Link>
void checkQueues(Link &link)
{
  Ztc::QueueTelemetry rx;
  Ztc::QueueTelemetry tx;
  queueTelemetry(link, rx, tx);
}

void checkTrafficTelemetry(TestLink &link)
{
  Ztc::QueueTelemetry rx;
  Ztc::QueueTelemetry tx;
  Ztc::LinkTelemetry data;
  queueTelemetry(link, rx, tx);
  link.telemetry(data);
  ZUTCP_CHECK_RT(rx.inCount && rx.inBytes,
    "QUIC Rx queue activity is zero");
  ZUTCP_CHECK_RT(tx.outCount && tx.outBytes,
    "QUIC Tx queue activity is zero");
  ZUTCP_CHECK_RT(data.rxCalls == rx.inCount && data.rxBytes == rx.inBytes,
    "QUIC Rx link and queue telemetry differ");
  ZUTCP_CHECK_RT(data.txCalls == tx.outCount && data.txBytes == tx.outBytes,
    "QUIC Tx link and queue telemetry differ");
  ZUTCP_CHECK_RT(!rx.count, "QUIC Rx queue depth is non-zero");
  ZUTCP_CHECK_RT(tx.count == link.txQueueCount(),
    "QUIC Tx queue depth differs from packet-space count");
}

ZuBSpan bytes_(const uint8_t *data, unsigned len)
{
  return ZuBSpan{data, len};
}

static ZmRef<ZiIOBuf> dataPkt(
  uint64_t offset, ZuBSpan payload, bool fin,
  Zutcp::Frame &frame, unsigned &used)
{
  ZmRef<ZiIOBuf> packet = ([](void *owner) {
      static Zi::IOBufAllocator *allocator =
	([] { static ZiMultiplex mx; return ZutcpTest::pktRxAllocator(&mx); })();
      return allocator->allocBuf(owner, 0);
    })(nullptr);
  if (ZuUnlikely(!packet)) return nullptr;
  int n = Zutcp::FrameCodec::writeData(
    packet->data_(), packet->size, offset, payload, fin);
  if (n <= 0) return nullptr;
  packet->skip = 0;
  packet->length = unsigned(n);
  if (Zutcp::FrameCodec::parse(
      ZuBSpan{packet->data(), packet->length}, frame, used) ||
      used != packet->length)
    return nullptr;
  return packet;
}

} // namespace

void testHandshakeDataAndClose()
{
  ZuTestScopeRT(testHandshakeDataAndClose);

  App app;
  ZmRef<TestLink> client = new TestLink{&app};
  ZmRef<TestLink> server = new TestLink{&app, true};
  checkQueues(*client);
  checkQueues(*server);
  client->receiveDatagram(17);
  server->receiveDatagram(23);
  ZUTCP_CHECK_RT(client->recordTx(29), "client Tx packet recording failed");
  ZUTCP_CHECK_RT(server->recordTx(31), "server Tx packet recording failed");
  checkTrafficTelemetry(*client);
  checkTrafficTelemetry(*server);
  ZUTCP_CHECK_RT(!client->isServer() && server->isServer(), "connection role mismatch");
  {
    auto tx = client->writer_();
    tx << "request-body" << Zi::flush();
  }
  ZUTCP_CHECK_RT(client->txBytes() == 12,
    "loop stream Tx accounting mismatch");

  client->closeForTest(Zutcp::TransportError::NoError);
  ZUTCP_CHECK_RT(client->closed() && !client->closeError(),
    "loop graceful close failed");
}

void splitReorderedData()
{
  ZuTestScope(splitReorderedData);

  uint8_t p0[700], p1[700], p2[200];
  memset(p0, 'a', sizeof(p0));
  memset(p1, 'b', sizeof(p1));
  memset(p2, 'c', sizeof(p2));
  Zutcp::Frame f;
  unsigned used = 0;
  App app;
  ZmRef<TestLink> client = new TestLink{&app};

  auto packet = dataPkt(
    sizeof(p0), bytes_(p1, sizeof(p1)), false, f, used);
  ZuCHECK(packet &&
      f.offset == sizeof(p0) && f.length == sizeof(p1),
    "loop second DATA frame parse failed");
  ZuCHECK(client->processFrame(f, packet) == 0 &&
      client->rxPending() == 1 &&
      !client->rxBytes(),
    "loop out-of-order stream receive failed");

  packet = dataPkt(
    0, bytes_(p0, sizeof(p0)), false, f, used);
  ZuCHECK(packet &&
      f.offset == 0 && f.length == sizeof(p0),
    "loop first DATA frame parse failed");
  ZuCHECK(client->processFrame(f, packet) == 0 &&
      client->rxBytes() == sizeof(p0) + sizeof(p1) &&
      !client->rxPending(),
    "loop stream gap fill failed");

  packet = dataPkt(
    sizeof(p0) + sizeof(p1), bytes_(p2, sizeof(p2)), true,
    f, used);
  ZuCHECK(packet &&
      f.offset == sizeof(p0) + sizeof(p1) && f.length == sizeof(p2) && f.fin,
    "loop final DATA frame parse failed");
  ZuCHECK(client->processFrame(f, packet) == 0 &&
      client->rxComplete() &&
      client->finalSize() == sizeof(p0) + sizeof(p1) + sizeof(p2),
    "loop stream final-size completion failed");
}

void testRecoveryFlowAndPMTUD()
{
  ZuTestScope(testRecoveryFlowAndPMTUD);

  Zutcp::FlowCredit credit{1200};
  ZuCHECK(credit.consume(1200) && credit.blocked(),
    "loop flow block failed");
  ZuCHECK(!credit.consume(1), "loop blocked flow allowed data");
  credit.extend(2400);
  ZuCHECK(credit.consume(600) && !credit.blocked(),
    "loop flow unblock failed");

  Zutcp::SentPktTracker sent;
  ZuCHECK(sent.add({
	.pn = 1,
	.sentTime = Zutcp::timeUS(100),
	.bytes = 1200,
	.space = Zutcp::PktNumSpace::AppData,
	.ackEliciting = true,
	.inFlight = true
      }) &&
      sent.add({
	.pn = 2,
	.sentTime = Zutcp::timeUS(110),
	.bytes = 1200,
	.space = Zutcp::PktNumSpace::AppData,
	.ackEliciting = true,
	.inFlight = true
      }) &&
      sent.add({
	.pn = 5,
	.sentTime = Zutcp::timeUS(120),
	.bytes = 1200,
	.space = Zutcp::PktNumSpace::AppData,
	.ackEliciting = true,
	.inFlight = true
      }),
    "loop sent-packet add failed");
  ZuCHECK(sent.ack(5), "loop ACK failed");
  ZuCHECK(sent.markPktThreshLoss(5) == 2 &&
      sent.lost() == 2 && sent.retransmittable() == 2,
    "loop packet-threshold loss failed");

  Zutcp::SentPktTracker pmtudTx;
  Zutcp::SentPkt probeAck{
    .pn = 10,
    .sentTime = Zutcp::timeUS(100),
    .bytes = 1300,
    .space = Zutcp::PktNumSpace::AppData,
    .ackEliciting = true,
    .inFlight = true,
    .pmtudProbe = true,
    .pmtudSize = 1300};
  ZuCHECK(pmtudTx.add(probeAck), "loop PMTUD sent-packet add failed");
  Zutcp::AckRange probeAckRange{10, 10};
  Zutcp::PktTxUpdate probeAckUpdate;
  ZuCHECK(pmtudTx.ack(
      &probeAckRange, 1, nullptr, 3, nullptr, nullptr, nullptr, nullptr,
      &probeAckUpdate) == 1 &&
      probeAckUpdate.pmtudAckdSize == 1300 &&
      probeAckUpdate.normalAckdBytes == 0,
    "loop PMTUD ACK accounting failed");

  Zutcp::SentPktTracker pmtudLossTx;
  Zutcp::SentPkt probeLoss{
    .pn = 11,
    .sentTime = Zutcp::timeUS(100),
    .bytes = 1400,
    .space = Zutcp::PktNumSpace::AppData,
    .ackEliciting = true,
    .inFlight = true,
    .pmtudProbe = true,
    .pmtudSize = 1400};
  ZuCHECK(pmtudLossTx.add(probeLoss), "loop PMTUD loss add failed");
  Zutcp::PktTxUpdate probeLossUpdate;
  ZuCHECK(pmtudLossTx.markTimeThreshLoss(
      13, Zutcp::timeUS(500), Zutcp::timeUS(100), nullptr, nullptr,
      &probeLossUpdate) == 1 &&
      probeLossUpdate.pmtudLostSize == 1400 &&
      probeLossUpdate.normalLostBytes == 0,
    "loop PMTUD loss accounting failed");

  Zutcp::Path path = Zutcp::Path::client(
    ZiSockAddr{ZiIP("127.0.0.1"), 10000},
    ZiSockAddr{ZiIP("127.0.0.1"), 10001});
  ZuCHECK(!path.canSend(1200), "loop anti-amplification allowed send");
  path.received(1200);
  ZuCHECK(path.canSend(1200), "loop anti-amplification budget failed");
  path.configuredMaxPayload(Zutcp::BufSize);
  path.startProbe(1400);
  path.probeAckd();
  ZuCHECK(path.activeMaxPayload() == 1400, "loop PMTUD growth failed");
  path.startProbe(Zutcp::BufSize);
  path.probeLost();
  ZuCHECK(path.activeMaxPayload() == 1400 &&
      path.diag().probesLost == 1,
    "loop PMTUD probe loss handling failed");

  Zutcp::Path retry = Zutcp::Path::client(
    ZiSockAddr{ZiIP("127.0.0.1"), 10002},
    ZiSockAddr{ZiIP("127.0.0.1"), 10003});
  retry.validated();
  ZuCHECK(retry.startNextProbe(128) &&
      retry.probeExpired() &&
      retry.probeRetryPending() &&
      retry.nextProbeSize(64) == retry.retryProbeSize(),
    "loop PMTUD expiry retry setup failed");
  unsigned retrySize = retry.retryProbeSize();
  ZuCHECK(retry.startNextProbe(64) &&
      retry.probeAttempts() == 2 &&
      retry.probeSize() == retrySize &&
      !retry.probeRetryPending(),
    "loop PMTUD expiry retry start failed");

  Zutcp::Path blackhole = Zutcp::Path::client(
    ZiSockAddr{ZiIP("127.0.0.1"), 10004},
    ZiSockAddr{ZiIP("127.0.0.1"), 10005});
  blackhole.validated();
  blackhole.startProbe(1400);
  blackhole.probeAckd();
  blackhole.startProbe(1400);
  ZuCHECK(blackhole.probeExpired() &&
      blackhole.startNextProbe(64) &&
      blackhole.probeExpired() &&
      blackhole.startNextProbe(64) &&
      blackhole.probeAttempts() == Zutcp::Path::MaxProbeAttempts,
    "loop PMTUD active-size expiry retry sequence failed");
  ZuCHECK(!blackhole.probeExpired() &&
      blackhole.activeMaxPayload() == Zutcp::MinPktPayload &&
      blackhole.diag().blackholes == 1,
    "loop PMTUD active-size blackhole fallback failed");

#ifdef Zutcp_DEBUG
  App app;
  ZmRef<TestLink> link = new TestLink{&app};
  link->initPath(
    ZiSockAddr{ZiIP("127.0.0.1"), 10006},
    ZiSockAddr{ZiIP("127.0.0.1"), 10007});
  ZuCHECK(link->sendBudget().pmtu ==
      Zutcp::MinPktPayload - Zutcp::TxDataPktReserve,
    "runtime PMTUD initial budget mismatch");
  ZuCHECK(link->startPMTUDProbe(1400) &&
      link->pathProbeSize() == 1400,
    "runtime PMTUD probe start failed");
  link->ackPMTUDProbe(1400);
  ZuCHECK(link->activePathMaxPayload() == 1400 &&
      link->sendBudget().pmtu == 1400 - Zutcp::TxDataPktReserve &&
      link->pathDiag().probesAckd == 1,
    "runtime PMTUD ACK did not grow active size");
  ZuCHECK(link->startPMTUDProbe(1500),
    "runtime PMTUD loss probe start failed");
  link->losePMTUDProbe(1500);
  ZuCHECK(link->activePathMaxPayload() == 1400 &&
      link->pathDiag().probesLost == 1,
    "runtime PMTUD loss handling failed");

  TestLink expiryLink{&app};
  expiryLink.initPath(
    ZiSockAddr{ZiIP("127.0.0.1"), 10008},
    ZiSockAddr{ZiIP("127.0.0.1"), 10009});
  ZuCHECK(expiryLink.startPMTUDProbe(1400),
    "runtime PMTUD expiry probe start failed");
  expiryLink.expirePMTUDProbe();
  ZuCHECK(expiryLink.pathProbeRetryPending() &&
      expiryLink.pathDiag().probesExpired == 1,
    "runtime PMTUD expiry did not schedule retry");

  TestLink hintLink{&app};
  hintLink.initPath(
    ZiSockAddr{ZiIP("127.0.0.1"), 10010},
    ZiSockAddr{ZiIP("127.0.0.1"), 10011});
  hintLink.growActivePath(1500);
  hintLink.applyPathHint(
    {Zutcp::PathHintKind::SendTooBig, 1300, EMSGSIZE});
  ZuCHECK(hintLink.activePathMaxPayload() == 1300 &&
      hintLink.sendBudget().pmtu == 1300 - Zutcp::TxDataPktReserve &&
      hintLink.pathDiag().sendTooBigHints == 1,
    "runtime PMTUD send-too-big hint handling failed");
  hintLink.applyPathHint({Zutcp::PathHintKind::KernelMTU, 1250, 0});
  ZuCHECK(hintLink.activePathMaxPayload() == 1250 &&
      hintLink.pathDiag().kernelHints == 1,
    "runtime PMTUD kernel hint handling failed");
#endif
}

void testDatagramPayloadHeadroom(unsigned headroom)
{
  ZuTestScopeRT(testDatagramPayloadHeadroom);
  App app;
  TestLink link{&app};
  Zi::IOBufAllocator *allocator = ([] { static ZiMultiplex mx; return ZutcpTest::pktRxAllocator(&mx); })();
  auto buf = allocator->allocBuf();
  ZuCheckRT(bool(buf));
  if (!buf) return;
  memset(buf->data_(), 0, headroom); // model metadata before received payload
  buf->skip = headroom;
  Zutcp::CxnID dcid{"dst"}, scid{"src"};
  int header = Zutcp::Pkt::writeInitial(
    buf->data(), buf->size - headroom, dcid, scid, 1, 1);
  ZuCheckRT(header > 0);
  if (header < 0) return;
  auto data = buf->data();
  data[header] = 0; data[header + 1] = 0; // packet number and payload byte
  data[header + 2] = 0x40; data[header + 3] = 0; // coalesced short packet
  buf->length = unsigned(header) + 4;
  ZuCheckRT(link.receivePayload(buf, unsigned(header) + 2));
  ZuCheckRT(buf->skip == headroom && buf->data() == buf->data_() + headroom &&
    buf->length == unsigned(header) + 4 && (!headroom || !buf->data_()[0]));
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testHandshakeDataAndClose);
  ZuTestCall(splitReorderedData);
  ZuTestCall(testRecoveryFlowAndPMTUD);
  ZuTestCall(testDatagramPayloadHeadroom, 0);
  ZuTestCall(testDatagramPayloadHeadroom, Zm::CacheLineSize);
  ZiLog::stop();
}
