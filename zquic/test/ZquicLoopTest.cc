//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>
#include <errno.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/Zquic.hh>

using namespace ZuTestUtil;

#define ZQUIC_CHECK_RT(x, ...) ZuCheckRT(x, log_(__VA_ARGS__))

namespace {

using StreamTxBufAlloc = Zquic::StreamTxBufAlloc<>;

struct App : public Zquic::Hub<App> {
  using Base = Zquic::Hub<App>;

  App() : m_mx{mxParams_()} {
    bool ok = m_mx.start();
    ZiAssert(ok, "Zquic", (), "loop test multiplexer start failed", return);
    if (ok)
      ok = Base::init(
	Zquic::HubParams(&m_mx, "3", "4").maxUDP(Zquic::BufSize));
    ZiAssert(ok, "Zquic", (), "loop test app init failed", return);
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
struct TestStream :
  public Zquic::Stream<TestLink, TestStream, StreamTxBufAlloc>
{
  using Base = Zquic::Stream<TestLink, TestStream, StreamTxBufAlloc>;
  using Base::Base;
  int process(Zquic::RxStream &) { ++processed; return 0; }
  unsigned processed = 0;
};
struct TestLink :
  public Zquic::Link<App, TestLink,
    StreamTxBufAlloc, TestStream>
{
  using Base = Zquic::Link<App, TestLink,
    StreamTxBufAlloc, TestStream>;
  TestLink(App *app, bool isServer = false) : Base{app, isServer} { }
  void flushTx_() { }
  void queueTxFlush_() { }
  void pto_() { }
  void queueRetransmit_() { }
  void initPath(ZiSockAddr local, ZiSockAddr remote) {
    Base::initClientPathTx_(ZuMv(local), ZuMv(remote));
  }
  void closeForTest(uint64_t errorCode = 0) {
    Base::closeState_(errorCode);
  }
  bool recordTx(unsigned bytes) {
    return Base::recordProtPktTx_(
      Zquic::PktNumSpace::AppData, 0, bytes, {}, nullptr, false);
  }
  void receiveDatagram(unsigned bytes) {
    ZmRef<ZiIOBuf> buf = new Zquic::PktRxBufAlloc<>{this};
    memset(buf->data_(), 0, bytes);
    buf->skip = 0;
    buf->length = bytes;
    Base::receiveDatagram_(
      Zquic::Datagram{ZuMv(buf), {}},
      [](Zquic::Datagram &, unsigned, unsigned) { return true; },
      [](Zquic::Datagram &, unsigned, unsigned) { return true; });
  }
  uint64_t txQueueCount() const { return Base::txQueueCount_(); }
#ifdef Zquic_DEBUG
  void growActivePath(unsigned size) { Base::forceActivePathMTU_(size); }
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
  void applyPathHint(Zquic::PathHint hint) {
    Base::applyPathHint_(hint);
  }
  unsigned activePathMaxUDP() const {
    return Base::activePathMaxUDP_();
  }
  unsigned pathProbeSize() const {
    return Base::pathProbeSize_();
  }
  bool pathProbeRetryPending() const {
    return Base::pathProbeRetryPending_();
  }
  Zquic::PathDiag pathDiag() const {
    return Base::pathDiag_();
  }
  Zquic::PktBudget sendBudget() const { return Base::sendBudget_(); }
#endif
};

template <typename Link>
void checkQueues(Link &link)
{
  auto rx = link.rxQueue();
  auto tx = link.txQueue();
  ZQUIC_CHECK_RT(rx, "Rx telemetry queue is null");
  ZQUIC_CHECK_RT(tx, "Tx telemetry queue is null");
  if (!rx || !tx) return;
  ZQUIC_CHECK_RT(link.rxQueue() == rx, "Rx telemetry queue is unstable");
  ZQUIC_CHECK_RT(link.txQueue() == tx, "Tx telemetry queue is unstable");
  ZQUIC_CHECK_RT(rx != tx, "Rx and Tx telemetry queues alias");

  auto linkKey = link.telKey();
  auto rxKey = rx->telKey();
  auto txKey = tx->telKey();
  ZQUIC_CHECK_RT(rxKey.template p<0>() == linkKey.template p<1>(),
    "Rx telemetry queue ID mismatch");
  ZQUIC_CHECK_RT(txKey.template p<0>() == linkKey.template p<1>(),
    "Tx telemetry queue ID mismatch");
  ZQUIC_CHECK_RT(rxKey.template p<1>() == Ztc::QueueType::Rx,
    "Rx telemetry queue type mismatch");
  ZQUIC_CHECK_RT(txKey.template p<1>() == Ztc::QueueType::Tx,
    "Tx telemetry queue type mismatch");

  Ztc::QueueTelemetry data;
  data.id = ZuID{} << "dirty";
  data.inBytes = data.outBytes = data.inCount = data.outCount = 1;
  data.count = data.size = data.full = 1;
  data.type = Ztc::QueueType::Thread;
  rx->telemetry(data);
  ZQUIC_CHECK_RT(data.id == rxKey.template p<0>(),
    "Rx telemetry ID mismatch");
  ZQUIC_CHECK_RT(data.type == rxKey.template p<1>(),
    "Rx telemetry type mismatch");
  ZQUIC_CHECK_RT(!data.outBytes && !data.outCount &&
      !data.count && !data.size && !data.full,
    "Rx telemetry was not reset");

  data.id = ZuID{} << "dirty";
  data.inBytes = data.outBytes = data.inCount = data.outCount = 1;
  data.count = data.size = data.full = 1;
  data.type = Ztc::QueueType::Thread;
  tx->telemetry(data);
  ZQUIC_CHECK_RT(data.id == txKey.template p<0>(),
    "Tx telemetry ID mismatch");
  ZQUIC_CHECK_RT(data.type == txKey.template p<1>(),
    "Tx telemetry type mismatch");
  ZQUIC_CHECK_RT(!data.inBytes && !data.inCount && !data.size && !data.full,
    "Tx telemetry was not reset");
}

void checkTrafficTelemetry(TestLink &link)
{
  Ztc::QueueTelemetry rx;
  Ztc::QueueTelemetry tx;
  Ztc::LinkTelemetry data;
  link.rxQueue()->telemetry(rx);
  link.txQueue()->telemetry(tx);
  link.telemetry(data);
  ZQUIC_CHECK_RT(rx.inCount && rx.inBytes,
    "QUIC Rx queue activity is zero");
  ZQUIC_CHECK_RT(tx.outCount && tx.outBytes,
    "QUIC Tx queue activity is zero");
  ZQUIC_CHECK_RT(data.rxCalls == rx.inCount && data.rxBytes == rx.inBytes,
    "QUIC Rx link and queue telemetry differ");
  ZQUIC_CHECK_RT(data.txCalls == tx.outCount && data.txBytes == tx.outBytes,
    "QUIC Tx link and queue telemetry differ");
  ZQUIC_CHECK_RT(!rx.count, "QUIC Rx queue depth is non-zero");
  ZQUIC_CHECK_RT(tx.count == link.txQueueCount(),
    "QUIC Tx queue depth differs from packet-space count");
}

ZuBSpan bytes_(const uint8_t *data, unsigned len)
{
  return ZuBSpan{data, len};
}

static ZmRef<ZiIOBuf> streamPkt_(
  uint64_t id, uint64_t offset, ZuBSpan payload, bool fin,
  Zquic::Frame &frame, unsigned &used)
{
  ZmRef<ZiIOBuf> packet = new Zquic::PktRxBufAlloc<>{nullptr};
  int n = Zquic::FrameCodec::writeStream(
    packet->data_(), packet->size, id, offset, payload, fin);
  if (n <= 0) return nullptr;
  packet->skip = 0;
  packet->length = unsigned(n);
  if (Zquic::FrameCodec::parse(
      ZuBSpan{packet->data(), packet->length}, frame, used) ||
      used != packet->length)
    return nullptr;
  return packet;
}

} // namespace

void testHandshakeStreamsAndClose()
{
  ZuTestScopeRT(testHandshakeStreamsAndClose);

  Zquic::CxnID dcid{"client01"};
  Zquic::Crypto clientCrypto;
  Zquic::Crypto serverCrypto;
  ZQUIC_CHECK_RT(clientCrypto.init(Zquic::CryptoConfig{false, false, "h3"}) &&
      serverCrypto.init(Zquic::CryptoConfig{true, false, "h3"}),
    "loop crypto init failed");
  ZQUIC_CHECK_RT(
    clientCrypto.deriveInitial(dcid) && serverCrypto.deriveInitial(dcid),
    "loop Initial key derivation failed");

  clientCrypto.installSecret(Zquic::PktNumSpace::AppData, "client-app");
  serverCrypto.installSecret(Zquic::PktNumSpace::AppData, "server-app");
  ZQUIC_CHECK_RT(
    clientCrypto.completeHandshake() && serverCrypto.completeHandshake(),
    "loop 1-RTT readiness failed");

  App app;
  ZmRef<TestLink> client = new TestLink{&app};
  ZmRef<TestLink> server = new TestLink{&app, true};
  checkQueues(*client);
  checkQueues(*server);
  client->receiveDatagram(17);
  server->receiveDatagram(23);
  ZQUIC_CHECK_RT(client->recordTx(29), "client Tx packet recording failed");
  ZQUIC_CHECK_RT(server->recordTx(31), "server Tx packet recording failed");
  checkTrafficTelemetry(*client);
  checkTrafficTelemetry(*server);
  auto c0 = client->stream(Zquic::StreamType::Duplex);
  auto c1 = client->stream(Zquic::StreamType::Simplex);
  auto s0 = server->stream(Zquic::StreamType::Duplex);
  ZQUIC_CHECK_RT(c0->id() == 0 && c1->id() == 2 && s0->id() == 1,
    "loop stream IDs mismatch");
  {
    auto tx = c0->txStream_();
    tx << "request-body" << Zi::flush();
  }
  ZQUIC_CHECK_RT(c0->txBytes() == 12,
    "loop stream Tx accounting mismatch");

  client->closeForTest(Zquic::TransportError::NoError);
  ZQUIC_CHECK_RT(client->closed() && !client->closeError(),
    "loop graceful close failed");
}

void testSplitReorderedStreamFrames()
{
  ZuTestScope(testSplitReorderedStreamFrames);

  uint8_t p0[700], p1[700], p2[200];
  memset(p0, 'a', sizeof(p0));
  memset(p1, 'b', sizeof(p1));
  memset(p2, 'c', sizeof(p2));
  Zquic::Frame f;
  unsigned used = 0;
  App app;
  ZmRef<TestLink> client = new TestLink{&app};
  auto stream = client->stream(Zquic::StreamType::Duplex);

  auto packet = streamPkt_(
    stream->id(), sizeof(p0), bytes_(p1, sizeof(p1)), false, f, used);
  ZuCHECK(packet &&
      f.offset == sizeof(p0) && f.length == sizeof(p1),
    "loop second STREAM frame parse failed");
  ZuCHECK(stream->processFrame(f, packet) == 0 &&
      stream->rxPending() == 1 &&
      !stream->rxBytes(),
    "loop out-of-order stream receive failed");

  packet = streamPkt_(
    stream->id(), 0, bytes_(p0, sizeof(p0)), false, f, used);
  ZuCHECK(packet &&
      f.offset == 0 && f.length == sizeof(p0),
    "loop first STREAM frame parse failed");
  ZuCHECK(stream->processFrame(f, packet) == 0 &&
      stream->rxBytes() == sizeof(p0) + sizeof(p1) &&
      !stream->rxPending(),
    "loop stream gap fill failed");

  packet = streamPkt_(
    stream->id(), sizeof(p0) + sizeof(p1), bytes_(p2, sizeof(p2)), true,
    f, used);
  ZuCHECK(packet &&
      f.offset == sizeof(p0) + sizeof(p1) && f.length == sizeof(p2) && f.fin,
    "loop final STREAM frame parse failed");
  ZuCHECK(stream->processFrame(f, packet) == 0 &&
      stream->rxComplete() &&
      stream->finalSize() == sizeof(p0) + sizeof(p1) + sizeof(p2),
    "loop stream final-size completion failed");
}

void testRecoveryFlowAndPMTUD()
{
  ZuTestScope(testRecoveryFlowAndPMTUD);

  Zquic::FlowCredit credit{1200};
  ZuCHECK(credit.consume(1200) && credit.blocked(),
    "loop flow block failed");
  ZuCHECK(!credit.consume(1), "loop blocked flow allowed data");
  credit.extend(2400);
  ZuCHECK(credit.consume(600) && !credit.blocked(),
    "loop flow unblock failed");

  Zquic::SentPktTracker sent;
  ZuCHECK(sent.add({
	.pn = 1,
	.sentTime = Zquic::timeUS(100),
	.bytes = 1200,
	.space = Zquic::PktNumSpace::AppData,
	.ackEliciting = true,
	.inFlight = true
      }) &&
      sent.add({
	.pn = 2,
	.sentTime = Zquic::timeUS(110),
	.bytes = 1200,
	.space = Zquic::PktNumSpace::AppData,
	.ackEliciting = true,
	.inFlight = true
      }) &&
      sent.add({
	.pn = 5,
	.sentTime = Zquic::timeUS(120),
	.bytes = 1200,
	.space = Zquic::PktNumSpace::AppData,
	.ackEliciting = true,
	.inFlight = true
      }),
    "loop sent-packet add failed");
  ZuCHECK(sent.ack(5), "loop ACK failed");
  ZuCHECK(sent.markPktThreshLoss(5) == 2 &&
      sent.lost() == 2 && sent.retransmittable() == 2,
    "loop packet-threshold loss failed");

  Zquic::SentPktTracker pmtudTx;
  Zquic::SentPkt probeAck{
    .pn = 10,
    .sentTime = Zquic::timeUS(100),
    .bytes = 1300,
    .space = Zquic::PktNumSpace::AppData,
    .ackEliciting = true,
    .inFlight = true,
    .pmtudProbe = true,
    .pmtudSize = 1300};
  ZuCHECK(pmtudTx.add(probeAck), "loop PMTUD sent-packet add failed");
  Zquic::AckRange probeAckRange{10, 10};
  Zquic::PktTxUpdate probeAckUpdate;
  ZuCHECK(pmtudTx.ack(
      &probeAckRange, 1, nullptr, 3, nullptr, nullptr, nullptr, nullptr,
      &probeAckUpdate) == 1 &&
      probeAckUpdate.pmtudAckdSize == 1300 &&
      probeAckUpdate.normalAckdBytes == 0,
    "loop PMTUD ACK accounting failed");

  Zquic::SentPktTracker pmtudLossTx;
  Zquic::SentPkt probeLoss{
    .pn = 11,
    .sentTime = Zquic::timeUS(100),
    .bytes = 1400,
    .space = Zquic::PktNumSpace::AppData,
    .ackEliciting = true,
    .inFlight = true,
    .pmtudProbe = true,
    .pmtudSize = 1400};
  ZuCHECK(pmtudLossTx.add(probeLoss), "loop PMTUD loss add failed");
  Zquic::PktTxUpdate probeLossUpdate;
  ZuCHECK(pmtudLossTx.markTimeThreshLoss(
      13, Zquic::timeUS(500), Zquic::timeUS(100), nullptr, nullptr,
      &probeLossUpdate) == 1 &&
      probeLossUpdate.pmtudLostSize == 1400 &&
      probeLossUpdate.normalLostBytes == 0,
    "loop PMTUD loss accounting failed");

  Zquic::Path path = Zquic::Path::client(
    ZiSockAddr{ZiIP("127.0.0.1"), 10000},
    ZiSockAddr{ZiIP("127.0.0.1"), 10001});
  ZuCHECK(!path.canSend(1200), "loop anti-amplification allowed send");
  path.received(1200);
  ZuCHECK(path.canSend(1200), "loop anti-amplification budget failed");
  path.configuredMaxUDP(Zquic::BufSize);
  path.startProbe(1400);
  path.probeAckd();
  ZuCHECK(path.activeMaxUDP() == 1400, "loop PMTUD growth failed");
  path.startProbe(Zquic::BufSize);
  path.probeLost();
  ZuCHECK(path.activeMaxUDP() == 1400 &&
      path.diag().probesLost == 1,
    "loop PMTUD probe loss handling failed");

  Zquic::Path retry = Zquic::Path::client(
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

  Zquic::Path blackhole = Zquic::Path::client(
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
      blackhole.probeAttempts() == Zquic::Path::MaxProbeAttempts,
    "loop PMTUD active-size expiry retry sequence failed");
  ZuCHECK(!blackhole.probeExpired() &&
      blackhole.activeMaxUDP() == Zquic::MinUDPPayload &&
      blackhole.diag().blackholes == 1,
    "loop PMTUD active-size blackhole fallback failed");

#ifdef Zquic_DEBUG
  App app;
  ZmRef<TestLink> link = new TestLink{&app};
  link->initPath(
    ZiSockAddr{ZiIP("127.0.0.1"), 10006},
    ZiSockAddr{ZiIP("127.0.0.1"), 10007});
  ZuCHECK(link->sendBudget().pmtu ==
      Zquic::MinUDPPayload - Zquic::TxStreamPktReserve,
    "runtime PMTUD initial budget mismatch");
  ZuCHECK(link->startPMTUDProbe(1400) &&
      link->pathProbeSize() == 1400,
    "runtime PMTUD probe start failed");
  link->ackPMTUDProbe(1400);
  ZuCHECK(link->activePathMaxUDP() == 1400 &&
      link->sendBudget().pmtu == 1400 - Zquic::TxStreamPktReserve &&
      link->pathDiag().probesAckd == 1,
    "runtime PMTUD ACK did not grow active size");
  ZuCHECK(link->startPMTUDProbe(1500),
    "runtime PMTUD loss probe start failed");
  link->losePMTUDProbe(1500);
  ZuCHECK(link->activePathMaxUDP() == 1400 &&
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
    {Zquic::PathHintKind::SendTooBig, 1300, EMSGSIZE});
  ZuCHECK(hintLink.activePathMaxUDP() == 1300 &&
      hintLink.sendBudget().pmtu == 1300 - Zquic::TxStreamPktReserve &&
      hintLink.pathDiag().sendTooBigHints == 1,
    "runtime PMTUD send-too-big hint handling failed");
  hintLink.applyPathHint({Zquic::PathHintKind::KernelMTU, 1250, 0});
  ZuCHECK(hintLink.activePathMaxUDP() == 1250 &&
      hintLink.pathDiag().kernelHints == 1,
    "runtime PMTUD kernel hint handling failed");
#endif
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testHandshakeStreamsAndClose);
  ZuTestCall(testSplitReorderedStreamFrames);
  ZuTestCall(testRecoveryFlowAndPMTUD);
}
