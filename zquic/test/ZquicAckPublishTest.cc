//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmAtomic.hh>
#include <zlib/ZmBlock.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/Zquic.hh>

#include <zlib/ZtlsEngineOpenSSL.hh>

using namespace ZuTestUtil;

class PublishApp : public Zquic::Hub<PublishApp> {
public:
  using Base = Zquic::Hub<PublishApp>;
  PublishApp(bool cc, bool ecn) : m_mx{ZiMxParams()
      .scheduler([](auto &s) { s.nThreads(4); }).rxThread(1).txThread(2)} {
    ZiAssert(Base::init(Zquic::HubParams(&m_mx, "3", "4").congestionControl(cc).ecn(ecn)),
      "Zquic", (), "publication app init failed", return);
  }
  ~PublishApp() { Base::final(); m_mx.stop(); }
  bool start() { m_running = true; return m_mx.start(); }
  bool rxInvoked() { return !m_running || Base::rxInvoked(); }
  bool txInvoked() { return !m_running || Base::txInvoked(); }
  template <typename L> void rxRun(L l) {
    if (!m_running) { l(); return; }
    if (Base::txInvoked()) ++m_rxPosts;
    Base::rxRun(ZuMv(l));
  }
  template <typename L> void txRun(L l) {
    if (!m_running) { l(); return; }
    if (Base::rxInvoked()) ++m_txPosts;
    Base::txRun(ZuMv(l));
  }
  template <typename L> void rxInvoke(L l) {
    if (rxInvoked()) l(); else rxRun(ZuMv(l));
  }
  template <typename L> void txInvoke(L l) {
    if (txInvoked()) l(); else txRun(ZuMv(l));
  }
  template <typename O, typename L> void rxInvoke(O *o, L l) {
    if (!m_running) { l(); return; }
    if (Base::txInvoked()) ++m_rxPosts;
    Base::rxInvoke(o, ZuMv(l));
  }
  template <typename O, typename L> void txInvoke(O *o, L l) {
    if (!m_running) { l(); return; }
    if (Base::rxInvoked()) ++m_txPosts;
    Base::txInvoke(o, ZuMv(l));
  }
  unsigned rxPosts() const { return m_rxPosts; }
  unsigned txPosts() const { return m_txPosts; }

private:
  ZiMultiplex m_mx;
  bool m_running = false;
  ZmAtomic<unsigned> m_rxPosts = 0;
  ZmAtomic<unsigned> m_txPosts = 0;
};

class PublishLink;
struct PublishStream :
    Zquic::Stream<PublishLink, PublishStream> {
  using Base = Zquic::Stream<PublishLink, PublishStream>;
  using Base::Base;
  int process(Zquic::RxStream &) { return 0; }
};

class PublishLink : public ZmNHeap<PublishLink>,
    public Zquic::Link<PublishApp, PublishLink, PublishStream> {
friend inline ZuStringT<"Zquic.Publish.Link"> ZmNHeapID(PublishLink *);
public:
  using ZmNHeap<PublishLink>::operator new;
  using ZmNHeap<PublishLink>::operator delete;
  using Base =
    Zquic::Link<PublishApp, PublishLink, PublishStream>;
  using Base::Base;
  using Base::ackSentTx;
  using Base::drainPeerAcksTx;
  using Base::awaitPeerAckTx;
  using Base::resetLink;
  using Base::resetPath;
  bool probingOutput() const { return m_probing; }
  void probingOutput(bool probing) { m_probing = probing; }
  void streamed(ZmRef<PublishStream>) { }

  bool establish() {
    uint8_t bytes[32]{};
    Zquic::TrafficSecret secret;
    Base::initClientPathTx({}, {});
    return Zquic::PktProt::deriveSecret(secret,
      &Ztls_::opensslAes128gcmsha256, ZuBSpan{bytes, sizeof(bytes)}) &&
      Base::installAppDataKeys_(secret, secret, Zquic::CxnID{"publish"});
  }
  bool frameMeta(ZuBSpan frames, bool nonProbing, bool ackEliciting,
      bool controls = false) {
    Base::RxAckMeta meta;
    ZmRef<Zquic::RxPkt> packet{app()->rxPktAllocator()->allocBuf(nullptr, 0)};
    if (!packet) return false;
    bool ok = Base::consumeProtFrames(
      Zquic::PktNumSpace::AppData, 0, frames, {}, packet, meta, nullptr,
      [](size_t, ZuBSpan, ZiSockAddr) { return true; },
      [](Zquic::PktNumSpace::T, const Zquic::Frame &, ZiSockAddr) { return true; });
    return ok && meta.nonProbing == nonProbing && meta.ackEliciting == ackEliciting &&
      meta.pathControls == controls;
  }
  bool responsePreservesAck() {
    Zquic::PktBuild response;
    Base::TxPktRefs refs;
    if (!Base::buildPathResponse("12345678", response, refs, {}, false, false) ||
	response.ack(Zquic::PktNumSpace::AppData)) return false;
    Zquic::Frame frame;
    unsigned used = 0;
    if (Zquic::FrameCodec::parse(response.data()[0], frame, used) ||
	frame.type != Zquic::FrameType::PathResponse) return false;
    return encode(1);
  }
  bool responseDispatchPreservesAck(bool retry) {
    unsigned calls = 0;
    bool probingOnly = true;
    bool sent = Base::sendPathResponsePkt(
      "12345678", ZiSockAddr{ZiIP{"127.0.0.1"}, 443},
      [&calls, &probingOnly, retry](Zquic::PktBuild &build, ZiSockAddr,
          const Base::TxPktRefs *) {
        ++calls;
        Zquic::Frame frame;
        unsigned used = 0;
        bool response = false;
        for (unsigned i = 0; i < build.count(); ++i) {
          auto span = build.data()[i];
          if (Zquic::FrameCodec::parse(span, frame, used)) return false;
          if (frame.type == Zquic::FrameType::PathResponse) response = true;
          if (frame.type == Zquic::FrameType::Ack) probingOnly = false;
        }
        probingOnly &= response && !build.ack(Zquic::PktNumSpace::AppData);
        return !retry || calls == 2;
      });
    return sent && probingOnly && calls == (retry ? 2u : 1u) && encode(1);
  }
  void receive(unsigned pn, Zquic::EcnMark::T mark = Zquic::EcnMark::ECT0) {
    Base::noteAck(Zquic::PktNumSpace::AppData, pn, true, {}, false, mark);
  }
  bool encode(unsigned pn) {
    Zquic::PktBuild build;
    constexpr auto space = Zquic::PktNumSpace::AppData;
    if (!Base::appendPendingAck(space, build) || !build.ack(space) ||
	build.ackGen() != pn || build.ackLargest() != pn) return false;
    Zquic::Frame frame;
    unsigned used = 0;
    return !Zquic::FrameCodec::parse(
      build.data()[0], frame, used) &&
      frame.type == Zquic::FrameType::Ack && frame.ackECN.ect0 == pn &&
      !frame.ackECN.ect1 && !frame.ackECN.ce && frame.ackRanges.length() == 1 &&
      frame.ackRanges[0].first == 1 && frame.ackRanges[0].largest == pn;
  }
  bool packets(unsigned count) {
    constexpr auto space = Zquic::PktNumSpace::AppData;
    Base::setTxPN(space, count + 1);
    for (unsigned pn = 1; pn <= count; ++pn)
      if (!Base::recordTxPkt<false>(space, pn, 100, Zquic::SentFrameRef{}, true))
	return false;
    return true;
  }
  unsigned packetCount() const {
    return Base::txPackets_(Zquic::PktNumSpace::AppData).count();
  }
  unsigned lostPackets() const {
    return Base::txPackets_(Zquic::PktNumSpace::AppData).lost();
  }
  using Base::deferredPeerAcks_;
  void peerAck(unsigned first, unsigned largest) {
    Zquic::Frame frame;
    frame.type = Zquic::FrameType::Ack;
    frame.ackRanges.push(Zquic::AckRange{largest, first});
    Base::processAckFrame(Zquic::PktNumSpace::AppData, frame);
  }
  unsigned encodeAck(uint8_t *data, unsigned size) {
    Zquic::PktBuild build;
    constexpr auto space = Zquic::PktNumSpace::AppData;
    if (!Base::appendPendingAck(space, build) || !build.ack(space) ||
	build.count() != 1 || build.bytes() > size) return 0;
    memcpy(data, build.data()[0].data(), build.bytes());
    Base::ackSentTx(space, build.ackGen());
    return build.bytes();
  }
  bool receiveAck(ZuBSpan data) {
    Zquic::Frame frame;
    unsigned used = 0;
    if (Zquic::FrameCodec::parse(data, frame, used) || used != data.length() ||
	data[0] != 0x03 || frame.ackECN.ect0 != 1 || frame.ackECN.ect1 != 1 ||
	frame.ackECN.ce != 1 || frame.ackRanges.length() != 1 ||
	frame.ackRanges[0].first != 1 || frame.ackRanges[0].largest != 3) return false;
    Base::processAckFrame(Zquic::PktNumSpace::AppData, frame);
    return true;
  }
  bool sendProbe(Zquic::EcnMark::T &mark) {
    Zquic::PktBuild build;
    if (!Base::buildPingProbe(build)) return false;
    Base::TxPktRefs refs;
    return Base::sendProtShortPkt(
      Zquic::CxnID{"publish"}, 1, build, {}, {}, &refs, true,
      [this] { return app()->txPktAllocator()->allocBuf(nullptr, 0); },
      [this, &mark](auto packet, ZiSockAddr addr, Zquic::EcnMark::T ecn) {
	return Base::template submitPathPkt<Base::PathTx::Direct>(ZuMv(packet), ZuMv(addr),
	  [&mark](auto buf, ZiSockAddr, Zquic::EcnMark::T ecn) {
	    mark = ecn;
	    return buf && buf->length;
	  }, ecn);
      });
  }
  bool park() {
    m_park = true;
    return !Base::awaitPeerAckTx();
  }
  void flushTx_() {
    if (Base::deferTxFlush()) return;
    if (m_park && packetCount()) {
      Base::drainPeerAcksTx(false);
      (void)Base::awaitPeerAckTx();
    }
  }
  void flushTx_(ZiSockAddr) { flushTx_(); }
  bool retransmit_() { return false; }
  void queueRetransmit() { }
  void ptoTimer() { ++m_recoveries; }
  void lossTimeExpired() { ++m_recoveries; }
  unsigned recoveries() const { return m_recoveries; }
  void armRecovery(ZuTime out) {
    Base::schedulePTOTimer(out);
    Base::scheduleLossTimer(out);
  }

private:
  bool m_probing = false;
  ZmAtomic<unsigned> m_recoveries = 0;
  bool m_park = false;
};

struct PublishFixture {
  PublishApp app;
  ZmRef<PublishLink> link;
  bool ready;

  PublishFixture(bool cc = true, bool ecn = false, bool server = false) :
    app{cc, ecn}, link{new PublishLink{&app, server}} {
    ready = link->establish() && app.start();
  }
  ~PublishFixture() {
    ZmBlock<>{}([this](auto wake) {
      link->disconnect_(false, [wake = ZuMv(wake)]() mutable { wake(); });
    });
  }
  template <typename L> bool rx(L l) {
    return ZmBlock<bool>{}([this, l = ZuMv(l)](auto wake) mutable {
      app.rxRun([l = ZuMv(l), wake = ZuMv(wake)]() mutable { wake(l()); });
    });
  }
  template <typename L> bool tx(L l) {
    return ZmBlock<bool>{}([this, l = ZuMv(l)](auto wake) mutable {
      app.txRun([l = ZuMv(l), wake = ZuMv(wake)]() mutable { wake(l()); });
    });
  }
};

void testConcurrentCompletion()
{
  ZuTestScope(testConcurrentCompletion);
  PublishFixture f;
  auto link = f.link;
  ZuCHECK(f.ready, "publication fixture did not start");
  f.rx([link] { link->receive(1); return true; });
  ZuCHECK(f.tx([link] {
    bool valid = link->encode(1);
    link->ackSentTx(Zquic::PktNumSpace::AppData, 1);
    return valid;
  }), "initial ACK epoch was not installed");
  unsigned rxPosts = f.app.rxPosts();
  for (unsigned batch = 0; batch < 32; ++batch) {
    unsigned first = 2 + (batch << 4);
    f.rx([link, first] {
      for (unsigned i = 0; i < 16; ++i) link->receive(first + i);
      return true;
    });
    ZuCheck(f.tx([link, first] {
      bool coherent = link->encode(first + 15);
      link->ackSentTx(Zquic::PktNumSpace::AppData, first + 14);
      coherent &= link->encode(first + 15);
      link->ackSentTx(Zquic::PktNumSpace::AppData, first + 15);
      return coherent;
    }));
  }
  ZuCheck(f.app.rxPosts() == rxPosts);
}

void testOrdinaryPublication()
{
  ZuTestScope(testOrdinaryPublication);
  PublishFixture f;
  auto link = f.link;
  ZuCHECK(f.ready, "ordinary publication fixture did not start");
  f.rx([link] { link->receive(1); return true; });
  f.tx([link] {
    link->ackSentTx(Zquic::PktNumSpace::AppData, 1);
    return true;
  });
  unsigned rxPosts = f.app.rxPosts(), txPosts = f.app.txPosts();
  for (unsigned pn = 2; pn <= 257; ++pn) {
    f.rx([link, pn] { link->receive(pn); return true; });
    ZuCheck(f.tx([link, pn] {
      bool coherent = link->encode(pn);
      link->ackSentTx(Zquic::PktNumSpace::AppData, pn);
      return coherent;
    }));
  }
  ZuCheck(f.app.rxPosts() == rxPosts && f.app.txPosts() == txPosts + 256);
}

void testPeerAckParking()
{
  ZuTestScopeRT(testPeerAckParking);
  for (bool cc : {false, true}) {
    PublishFixture f{cc};
    auto link = f.link;
    ZuCheckRT(f.ready && f.tx([link] { return link->packets(96); }),
      log_("peer ACK fixture did not start"));
    for (unsigned pn = 1; pn <= 96; ++pn) {
      bool found = false;
      unsigned posts = f.app.txPosts();
      if (pn & 1) {
	f.rx([link, pn] { link->peerAck(pn, pn); return true; });
	bool noPost = f.app.txPosts() == posts;
	found = f.tx([link] { return link->awaitPeerAckTx(); });
	ZuCheckRT(noPost && found, log_("ACK published before parking was missed or posted"));
      } else {
	bool parked = f.tx([link] { return !link->awaitPeerAckTx(); });
	f.rx([link, pn] { link->peerAck(pn, pn); return true; });
	found = f.tx([link, pn] { return link->packetCount() == 96 - pn; });
	ZuCheckRT(parked && found && f.app.txPosts() == posts + 1,
	  log_("ACK published after parking did not supply its exceptional wake"));
      }
    }
    ZuCheckRT(f.tx([link] { return !link->packetCount(); }),
      log_("final peer ACK left retained packets"));
  }
}

void testPeerAckPressure()
{
  ZuTestScopeRT(testPeerAckPressure);
  for (bool cc : {false, true}) {
    PublishFixture f{cc};
    auto link = f.link;
    ZuCheckRT(f.ready && f.tx([link] { return link->packets(1024); }),
      log_("peer ACK pressure fixture did not start"));
    ZmSemaphore held, resume;
    f.app.txRun([&held, &resume] { held.post(); resume.wait(); });
    bool ready = !held.timedwait(Zm::now(10));
    f.rx([link] {
      for (unsigned pn = 1; pn <= 64; ++pn) link->peerAck(pn, pn);
      // The full ring falls back before the large, bounded-retirement ACK.
      link->peerAck(65, 1008);
      link->peerAck(1009, 1024);
      return true;
    });
    resume.post();
    // Each large ACK can yield a bounded number of owner continuations.
    bool retired = false;
    for (unsigned i = 0; i < 8 && !retired; ++i)
      retired = f.tx([link] { return !link->packetCount(); });
    ZuCheckRT(ready && retired, log_("peer ACK pressure or bounded retirement lost coverage"));
    ZuCheckRT(f.tx([link] { return !link->lostPackets(); }),
      log_("later peer ACK declared packets lost while their earlier ACK was retiring"));
  }
}

void testECNFeedback()
{
  ZuTestScopeRT(testECNFeedback);
  for (bool cc : {false, true}) for (bool ecn : {false, true}) {
    PublishFixture local{cc, ecn}, peer{!cc, true};
    auto link = local.link, remote = peer.link;
    ZuCheckRT(local.ready && peer.ready, log_("ECN policy fixtures did not start"));
    Zquic::EcnMark::T expected = cc && ecn ?
      Zquic::EcnMark::ECT0 : Zquic::EcnMark::NotECT;
    for (bool reset : {false, true}) {
      Zquic::EcnMark::T mark = Zquic::EcnMark::CE;
      bool sent = local.tx([link, reset, &mark] {
	if (reset) link->resetPath();
	return link->sendProbe(mark);
      });
      ZuCheckRT(sent && mark == expected,
	log_("protected output marking ignored CC/ECN policy or path reset"));
    }
    ZuCheckRT(peer.tx([remote] { return remote->packets(3); }),
      log_("mixed-mode peer did not retain its packets"));
    local.rx([link] {
      link->receive(1, Zquic::EcnMark::ECT0);
      link->receive(2, Zquic::EcnMark::ECT1);
      link->receive(3, Zquic::EcnMark::CE);
      return true;
    });
    uint8_t wire[256];
    unsigned length = 0;
    ZuCheckRT(local.tx([link, &wire, &length] {
      length = link->encodeAck(wire, sizeof(wire));
      return length != 0;
    }), log_("received ECN marks did not produce an ACK frame"));
    ZuCheckRT(peer.rx([remote, &wire, length] {
      return remote->receiveAck(ZuBSpan{wire, length});
    }), log_("mixed-mode peer did not observe the exact ACK_ECN counters"));
    ZuCheckRT(peer.tx([remote] {
      remote->drainPeerAcksTx();
      return !remote->packetCount();
    }), log_("mixed-mode ACK_ECN did not retire unmarked packets"));
  }
}

void testAckBeforeRecovery()
{
  ZuTestScope(testAckBeforeRecovery);
  PublishFixture f;
  auto link = f.link;
  ZuCHECK(f.ready && f.tx([link] { return link->packets(1024); }),
    "ACK/recovery fixture did not start");
  ZmSemaphore held, resume, received;
  f.app.txRun([link, &held, &resume] {
    link->armRecovery(Zm::now() + Zquic::timeUS(10000));
    held.post();
    resume.wait();
  });
  bool ready = !held.timedwait(Zm::now(10));
  ZmScheduler::Timer sentinel;
  f.app.mx()->add(&sentinel, Zm::now() + Zquic::timeUS(30000),
    ZmScheduler::Update, [link, &received](auto &&arm) {
      return arm([link, &received] {
	link->peerAck(1, 1024);
	received.post();
      });
    }, f.app.rxThread());
  ready &= !received.timedwait(Zm::now(10));
  resume.post();
  bool retired = false;
  for (unsigned i = 0; i < 8 && !retired; ++i)
    retired = f.tx([link] { return !link->packetCount(); });
  ZuCHECK(ready && retired && !link->recoveries(),
    "handed-off recovery callback ran ahead of a published full ACK");
  f.app.mx()->del(&sentinel);
}

void testDeferredReset()
{
  ZuTestScope(testDeferredReset);
  PublishFixture f;
  auto link = f.link;
  ZuCHECK(f.ready && f.tx([link] { return link->packets(1024); }),
    "deferred ACK reset fixture did not start");
  unsigned deferred = 0;
  ZmSemaphore held, resume, received, reset;
  f.app.txRun([link, &held, &resume] {
    link->armRecovery(Zm::now() + Zquic::timeUS(10000));
    held.post();
    resume.wait();
  });
  bool ready = !held.timedwait(Zm::now(10));
  ZmScheduler::Timer sentinel;
  f.app.mx()->add(&sentinel, Zm::now() + Zquic::timeUS(30000),
    ZmScheduler::Update, [link, &deferred, &received, &reset](auto &&arm) {
      return arm([link, &deferred, &received, &reset] {
	link->peerAck(1, 900);
	for (unsigned pn = 901; pn <= 964; ++pn) link->peerAck(pn, pn);
	link->peerAck(965, 1024);
	// Recovery callbacks are already queued before these fallback posts.
	// They begin a bounded ACK retirement; reset overtakes its continuation.
	link->app()->txRun([link, &deferred] { deferred = link->deferredPeerAcks_(); });
	link->resetLink([&reset] { reset.post(); });
	received.post();
      });
    }, f.app.rxThread());
  ready &= !received.timedwait(Zm::now(10));
  resume.post();
  ready &= !reset.timedwait(Zm::now(10));
  bool cleared = f.tx([link] { return !link->packetCount() && !link->deferredPeerAcks_(); });
  ZuCHECK(ready && deferred == 2 && cleared && !link->recoveries(),
    "reset retained deferred ACKs or allowed old retirement/recovery to run");
  f.app.mx()->del(&sentinel);
}

void testDuplicateWhileParked()
{
  ZuTestScope(testDuplicateWhileParked);
  PublishFixture f;
  auto link = f.link;
  // Bytes in flight exceed the current window, as after a congestion event.
  ZuCHECK(f.ready && f.tx([link] { return link->packets(1024) && link->park(); }),
    "congestion-blocked publication fixture did not start");
  f.rx([link] { link->peerAck(1, 1); return true; });
  ZuCHECK(f.tx([link] { return link->packetCount() == 1023; }),
    "first advancing ACK did not wake the blocked sender");
  f.rx([link] { link->peerAck(1, 1); return true; });
  ZuCHECK(f.tx([link] { return link->packetCount() == 1023; }),
    "duplicate ACK altered retained packet coverage");
  f.rx([link] { link->peerAck(2, 2); return true; });
  ZuCHECK(f.tx([link] { return link->packetCount() == 1022; }),
    "duplicate ACK consumed the blocked sender's next wake");
}

void testProbingAckMetadata()
{
  ZuTestScope(testProbingAckMetadata);
  PublishApp app{false, false};
  ZmRef<PublishLink> parser = new PublishLink{&app};
  uint8_t frames[128];
  int n = Zquic::FrameCodec::writePadding(frames, sizeof(frames), 1);
  ZuCheck(n > 0 && parser->frameMeta(ZuBSpan{frames, unsigned(n)}, false, false));
  n = Zquic::FrameCodec::writePathChallenge(frames, sizeof(frames), "12345678");
  ZuCheck(n > 0 && parser->frameMeta(ZuBSpan{frames, unsigned(n)}, false, true, true));
  n = Zquic::FrameCodec::writePathResponse(frames, sizeof(frames), "12345678");
  ZuCheck(n > 0 && parser->frameMeta(ZuBSpan{frames, unsigned(n)}, false, true, true));
  n = Zquic::FrameCodec::writeAck(frames, sizeof(frames), 0, 0, 0);
  ZuCheck(n > 0 && parser->frameMeta(ZuBSpan{frames, unsigned(n)}, true, false));
  n = Zquic::FrameCodec::writePing(frames, sizeof(frames));
  ZuCheck(n > 0 && parser->frameMeta(ZuBSpan{frames, unsigned(n)}, true, true));

  PublishFixture f{false, false};
  auto link = f.link;
  ZuCheck(f.ready && f.rx([link] { link->receive(1); return true; }));
  ZuCheck(f.tx([link] { return link->responsePreservesAck(); }));
  PublishFixture server{false, false, true};
  auto probing = server.link;
  ZuCheck(server.ready && server.rx([probing] { probing->receive(1); return true; }));
  ZuCheck(server.tx([probing] {
    probing->probingOutput(true);
    return probing->responseDispatchPreservesAck(false) &&
      probing->responseDispatchPreservesAck(true);
  }));
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testProbingAckMetadata);
  ZuTestCall(testConcurrentCompletion);
  ZuTestCall(testOrdinaryPublication);
  ZuTestCall(testPeerAckParking);
  ZuTestCall(testPeerAckPressure);
  ZuTestCall(testECNFeedback);
  ZuTestCall(testAckBeforeRecovery);
  ZuTestCall(testDeferredReset);
  ZuTestCall(testDuplicateWhileParked);
}
