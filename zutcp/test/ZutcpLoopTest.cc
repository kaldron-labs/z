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

namespace Zutcp {
struct TCPStateTest {
  template <typename T> static void pathBase(T &link) {
    auto &c = static_cast<typename T::Base &>(link);
    c.m_path.configuredMaxPayload(1200);
    c.m_path.configuredMaxPayload(BufSize);
  }
};
} // namespace Zutcp

namespace LoopTest {

using TxBufAlloc = Zutcp::TxBufAlloc<>;

struct App : public Zutcp::Hub<App> {
  using Base = Zutcp::Hub<App>;
  ZiMultiplex multiplex;
  Zi::IOBufAllocator *dataTxAllocator() const { return Zutcp::inlineAllocator(); }

  App() : multiplex{mxParams()} {
    bool ok = Base::init(
      Zutcp::HubParams(&multiplex).maxPayload(Zutcp::BufSize));
    ZiAssert(ok, "Zutcp", (), "loop test app init failed", return);
    ZiAssert(multiplex.start(), "Zutcp", (),
      "loop test multiplexer start failed", return);
  }
  ~App() {
    Base::final();
    multiplex.stop();
  }

  bool rxInvoked() const { return true; }
  bool txInvoked() const { return true; }
  template <typename L> void rxRun(L l) { l(); }
  template <typename L> void rxInvoke(L l) { l(); }
  template <typename L> void txRun(L l) { l(); }
  template <typename L> void txInvoke(L l) { l(); }
  template <typename O, typename L> void txInvoke(O *, L l) { l(); }

  static ZiMxParams mxParams() {
    return ZiMxParams()
      .scheduler([](auto &s) {
	s.nThreads(4);
      })
      .rxThread(1).txThread(2);
  }

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
  void flushTx() { }
  void queueTxFlush() { }
  void pto() { }
  void queueRetransmit() { }
  bool retransmit(bool = false, unsigned = 2) { return false; }
  void initPath(ZiSockAddr local, ZiSockAddr remote) {
    Base::initClientPathTx(ZuMv(local), ZuMv(remote));
    Zutcp::TCPStateTest::pathBase(*this);
  }
  void closeForTest(uint64_t errorCode = 0) {
    Base::closeState(errorCode);
  }
  bool receivePayload(ZmRef<ZiIOBuf> buf) {
    auto original = buf.ptr();
    auto payload = buf->data();
    auto length = buf->length;
    Zutcp::ByteData data{.offset = 0, .length = length, .payload = buf->cspan()};
    int result = Base::processFrame(data, buf);
    return result == 0 && rxBytes() == length && buf.ptr() == original &&
      buf->data() == payload && buf->length == length;
  }
  uint64_t txQueueCount() const { return Base::sentPktCount(); }
#ifdef Zutcp_DEBUG
  void growActivePath(unsigned size) { Base::forceActivePathMTU(size); }
  bool startPMTUDProbe(unsigned size) {
    return Base::startPMTUDProbe(size);
  }
  void ackPMTUDProbe(unsigned size) {
    Base::ackPMTUDProbe(size);
  }
  void losePMTUDProbe(unsigned size) {
    Base::losePMTUDProbe(size);
  }
  void expirePMTUDProbe() { Base::expirePMTUDProbe(); }
  void applyPathHint(Zutcp::PathHint hint) {
    Base::applyPathHint(hint);
  }
  unsigned activePathMaxPayload() const {
    return Base::activePathMaxPayload_();
  }
  unsigned pathProbeSize() const {
    return Base::pathProbeSize();
  }
  bool pathProbeRetryPending() const {
    return Base::pathProbeRetryPending();
  }
  Zutcp::PathDiag pathDiag() const {
    return Base::pathDiag_();
  }
  Zutcp::PktBudget sendBudget() const { return Base::sendBudget(); }
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
  ZUTCP_CHECK_RT(data.rxCalls == rx.inCount && data.rxBytes == rx.inBytes,
    "TCP Rx link and queue telemetry differ");
  ZUTCP_CHECK_RT(data.txCalls == tx.outCount && data.txBytes == tx.outBytes,
    "TCP Tx link and queue telemetry differ");
  ZUTCP_CHECK_RT(!rx.count, "TCP Rx queue depth is non-zero");
  ZUTCP_CHECK_RT(tx.count == link.txQueueCount(),
    "TCP Tx queue depth differs from queued original count");
}

ZuBSpan bytes_(const uint8_t *data, unsigned len)
{
  return ZuBSpan{data, len};
}

static ZmRef<ZiIOBuf> dataPkt(
  uint64_t offset, ZuBSpan payload, bool fin,
  Zutcp::ByteData &frame, unsigned &used)
{
  ZmRef<ZiIOBuf> packet = ([](void *owner) {
      static Zi::IOBufAllocator *allocator =
	([] { static ZiMultiplex mx; return ZutcpTest::pktRxAllocator(&mx); })();
      return allocator->allocBuf(owner, 0);
    })(nullptr);
  if (ZuUnlikely(!packet)) return nullptr;
  memcpy(packet->data(), payload.data(), payload.length());
  packet->length = payload.length();
  frame = Zutcp::ByteData{.offset = offset, .length = payload.length(),
    .payload = packet->cspan(), .fin = fin};
  used = payload.length();
  return packet;
}

} // namespace LoopTest

using namespace LoopTest;

void testLinkStateAndTelemetry()
{
  ZuTestScopeRT(testLinkStateAndTelemetry);

  App app;
  ZmRef<TestLink> client = new TestLink{&app};
  ZmRef<TestLink> server = new TestLink{&app, true};
  checkQueues(*client);
  checkQueues(*server);
  checkTrafficTelemetry(*client);
  checkTrafficTelemetry(*server);
  ZUTCP_CHECK_RT(!client->isServer() && server->isServer(), "connection role mismatch");
  {
    auto tx = client->writer_();
    tx << "request-body" << Zi::flush();
  }
  checkTrafficTelemetry(*client);
  ZUTCP_CHECK_RT(client->txBytes() == 12 && client->txRangeCount() == 1,
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
  Zutcp::ByteData f;
  unsigned used = 0;
  App app;
  ZmRef<TestLink> client = new TestLink{&app};

  auto packet = dataPkt(
    sizeof(p0), bytes_(p1, sizeof(p1)), false, f, used);
  ZuCHECK(packet &&
      f.offset == sizeof(p0) && f.length == sizeof(p1),
    "loop second byte metadata mismatch");
  ZuCHECK(client->processFrame(f, packet) == 0 &&
      client->rxPending() == 1 &&
      !client->rxBytes(),
    "loop out-of-order stream receive failed");

  packet = dataPkt(
    0, bytes_(p0, sizeof(p0)), false, f, used);
  ZuCHECK(packet &&
      f.offset == 0 && f.length == sizeof(p0),
    "loop first byte metadata mismatch");
  ZuCHECK(client->processFrame(f, packet) == 0 &&
      client->rxBytes() == sizeof(p0) + sizeof(p1) &&
      !client->rxPending(),
    "loop stream gap fill failed");

  packet = dataPkt(
    sizeof(p0) + sizeof(p1), bytes_(p2, sizeof(p2)), true,
    f, used);
  ZuCHECK(packet &&
      f.offset == sizeof(p0) + sizeof(p1) && f.length == sizeof(p2) && f.fin,
    "loop final byte metadata mismatch");
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
  bool recorded = true;
  for (unsigned id = 0; id < 3; ++id) {
    Zutcp::SentPkt packet;
    packet.pn = id;
    packet.sentTime = Zutcp::timeUS(100 + id * 10);
    packet.bytes = 1200;
    packet.ackEliciting = packet.inFlight = true;
    packet.addFrame(Zutcp::SentFrameRef::data(
      Zutcp::TxRange{nullptr, 0, 10, uint64_t(id) * 10}, false));
    recorded &= sent.add(packet);
  }
  const Zutcp::RxSpan block[] = {{20,30}};
  Zutcp::PktSACKBatch sack;
  Zutcp::PktSACKLossBatch holes;
  Zutcp::PktTxUpdate lost;
  ZuCHECK(recorded && sent.sackBytesBatch(block, 1, sack, 8) && sack.marked == 1 &&
      sent.loseSACKHolesBatch(20, holes, 8, lost) && holes.lost == 2 &&
      sent.lost() == 2 && sent.retransmittable() == 2,
    "byte SACK evidence retains one original while queuing its two predecessor holes");
  Zutcp::PktAckBatch cumulative;
  ZuCHECK(sent.ackBytesBatch(30, cumulative, 8) && cumulative.ackd == 1 &&
      !sent.count() && !sent.bytesInFlight(), "cumulative byte ACK retires all copies once");

  Zutcp::SentPktTracker pmtudTx;
  Zutcp::SentPkt probe;
  probe.pn = 10;
  probe.sentTime = Zutcp::timeUS(100);
  probe.bytes = 1300;
  probe.ackEliciting = probe.inFlight = probe.pmtudProbe = true;
  probe.pmtudSize = 1300;
  probe.addFrame(Zutcp::SentFrameRef::data(Zutcp::TxRange{nullptr, 0, 1, 0}, false));
  Zutcp::PktAckBatch probeACK;
  Zutcp::PktTxUpdate probeAckUpdate;
  ZuCHECK(pmtudTx.add(probe) && pmtudTx.ackBytesBatch(1, probeACK, 8, &probeAckUpdate) &&
      probeAckUpdate.pmtudAckdSize == 1300 && !probeAckUpdate.normalAckdBytes,
    "byte-covered PMTUD probe acknowledgment remains retirement-only");
  probe.pn = 11;
  probe.bytes = probe.pmtudSize = 1400;
  Zutcp::PktSACKLossBatch probeLoss;
  Zutcp::PktTxUpdate probeLossUpdate;
  ZuCHECK(pmtudTx.add(probe) && pmtudTx.loseSACKHolesBatch(1, probeLoss, 8, probeLossUpdate) &&
      probeLossUpdate.pmtudLostSize == 1400 && !probeLossUpdate.normalLostBytes &&
      !pmtudTx.retransmitPending(), "PMTUD loss preserves separate congestion accounting");

  Zutcp::Path path = Zutcp::Path::client(
    ZiSockAddr{ZiIP("127.0.0.1"), 10000},
    ZiSockAddr{ZiIP("127.0.0.1"), 10001});
  ZuCHECK(path.canSend(1200), "ordinary TCP path has no envelope anti-amplification gate");
  path.configuredMaxPayload(1200);
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
  retry.configuredMaxPayload(1200);
  retry.configuredMaxPayload(Zutcp::BufSize);
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
  blackhole.configuredMaxPayload(1400);
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
      blackhole.activeMaxPayload() == 536 &&
      blackhole.diag().blackholes == 1,
    "loop PMTUD active-size blackhole fallback failed");

#ifdef Zutcp_DEBUG
  App app;
  ZmRef<TestLink> link = new TestLink{&app};
  link->initPath(
    ZiSockAddr{ZiIP("127.0.0.1"), 10006},
    ZiSockAddr{ZiIP("127.0.0.1"), 10007});
  ZuCHECK(link->sendBudget().pmtu == 1200,
    "modeled byte budget uses the active path without an inner frame reserve");
  ZuCHECK(link->startPMTUDProbe(1400) &&
      link->pathProbeSize() == 1400,
    "runtime PMTUD probe start failed");
  link->ackPMTUDProbe(1400);
  ZuCHECK(link->activePathMaxPayload() == 1400 &&
      link->sendBudget().pmtu == 1400 &&
      link->pathDiag().probesAckd == 1,
    "PMTUD updates the modeled raw-byte budget without an envelope prefix");
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
      hintLink.sendBudget().pmtu == 1300 &&
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
  Zi::IOBufAllocator *allocator = ([] {
    static ZiMultiplex mx;
    return ZutcpTest::pktRxAllocator(&mx);
  })();
  auto buf = allocator->allocBuf();
  ZuCheckRT(bool(buf));
  if (!buf) return;
  memset(buf->data_(), 0, headroom); // model metadata before received payload
  buf->skip = headroom;
  memcpy(buf->data(), "abc", 3);
  buf->length = 3;
  ZuCheckRT(link.receivePayload(buf));
  ZuCheckRT(buf->skip == headroom && buf->data() == buf->data_() + headroom &&
    buf->length == 3 && (!headroom || !buf->data_()[0]));
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testLinkStateAndTelemetry);
  ZuTestCall(splitReorderedData);
  ZuTestCall(testRecoveryFlowAndPMTUD);
  ZuTestCall(testDatagramPayloadHeadroom, 0);
  ZuTestCall(testDatagramPayloadHeadroom, Zm::CacheLineSize);
  ZiLog::stop();
}
