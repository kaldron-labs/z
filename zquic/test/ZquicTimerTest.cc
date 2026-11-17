//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmBlock.hh>
#include <zlib/Zquic.hh>

#include <zpicotls/openssl.h>

using namespace ZuTestUtil;

#define ZQUIC_CHECK_RT(x, ...) ZuCheckRT(x, log_(__VA_ARGS__))

using StreamTxBufAlloc = Zquic::StreamTxBufAlloc<>;

static bool trafficSecret(Zquic::TrafficSecret &secret, uint8_t seed)
{
  uint8_t bytes[32];
  for (unsigned i = 0; i < sizeof(bytes); ++i) bytes[i] = seed + i;
  return Zquic::PktProt::deriveSecret(
    secret, &ptls_openssl_aes128gcmsha256, Zquic::byteSpan(bytes, sizeof(bytes)));
}

class TimerApp : public Zquic::Hub<TimerApp> {
public:
  using Base = Zquic::Hub<TimerApp>;

  TimerApp() : m_mx{mxParams()} {
    ZiAssert(Base::init(
	Zquic::HubParams(&m_mx, "3", "4")
	  .heartBeat(ZuTime{10})),
      "Zquic", (), "timer test app init failed", return);
    ZiAssert(m_mx.start(), "Zquic", (),
      "timer test multiplexer start failed", return);
  }
  ~TimerApp() {
    Base::final();
    m_mx.stop();
  }

  void strict(bool value) { m_strict = value; }
  bool rxInvoked() const { return !m_strict || mx()->invoked(rxThread()); }
  bool txInvoked() const { return !m_strict || mx()->invoked(txThread()); }
  template <typename L> void rxRun(L l) {
    if (m_strict) Base::rxRun(ZuMv(l)); else l();
  }
  template <typename L> void rxInvoke(L l) {
    if (m_strict) Base::rxInvoke(ZuMv(l)); else l();
  }
  template <typename O, typename L> void rxInvoke(O *o, L l) {
    if (m_strict) Base::rxInvoke(o, ZuMv(l)); else l();
  }
  template <typename L> void txRun(L l) {
    if (m_strict) Base::txRun(ZuMv(l)); else l();
  }
  template <typename L> void txInvoke(L l) {
    if (m_strict) Base::txInvoke(ZuMv(l)); else l();
  }
  template <typename O, typename L> void txInvoke(O *o, L l) {
    if (m_strict) Base::txInvoke(o, ZuMv(l)); else l();
  }

private:
  static ZiMxParams mxParams() {
    return ZiMxParams()
      .scheduler([](auto &s) { s.nThreads(4); })
      .rxThread(1).txThread(2);
  }

  ZiMultiplex	m_mx;
  bool		m_strict = false;
};

struct TimerLink;
struct TimerStream :
  public Zquic::Stream<TimerLink, TimerStream, StreamTxBufAlloc> {
  using Base = Zquic::Stream<TimerLink, TimerStream, StreamTxBufAlloc>;
  TimerStream(int64_t id) : Base{nullptr, id} { }
  using Base::Base;
  int process(Zquic::RxStream &) { return 0; }
};

struct TimerLink :
  public ZmNHeap<TimerLink>,
  public Zquic::Link<TimerApp, TimerLink, StreamTxBufAlloc, TimerStream> {
friend inline ZuStringT<"Zquic.TimerTest.Link"> ZmNHeapID(TimerLink *);
  using ZmNHeap<TimerLink>::operator new;
  using ZmNHeap<TimerLink>::operator delete;

  using Base = Zquic::Link<TimerApp, TimerLink, StreamTxBufAlloc, TimerStream>;
  using Base::Base;

  ZmSemaphore		event;
  bool			ackOwnerOK = false;
  uint64_t		ackGeneration = 0;
  ZmAtomic<unsigned>	ackDelay = 0;
  ZmAtomic<unsigned>	lossTime = 0;
  ZmAtomic<unsigned>	pto = 0;
  ZmAtomic<unsigned>	idle = 0;
  ZmAtomic<unsigned>	heartBeat = 0;
  ZmAtomic<unsigned>	close = 0;
  ZmAtomic<unsigned>	keyDiscard = 0;
  ZmAtomic<unsigned>	pmtud = 0;
  ZmAtomic<unsigned>	path = 0;

  void armAll(ZuTime out) {
    Base::scheduleAckDelayTimer(out);
    Base::scheduleLossTimer(out);
    Base::schedulePTOTimer(out);
    Base::startIdleTimerTx(out - Zm::now());
    Base::scheduleHBTimer(out);
    Base::scheduleCloseTimer(out);
    Base::scheduleKeyDiscardTimer(out);
    Base::schedulePMTUDTimer(out);
    Base::schedulePathTimer(out);
  }
  void cancelAll() { Base::cancelTimers_(); }
  void armPTO(ZuTime out) { Base::schedulePTOTimer(out); }
  using Base::scheduleLossTimer;
  using Base::notePeerPkt;
  using Base::startIdleTimerTx;
  using Base::idleDeadline;
  using Base::timersActive;
  bool flight(uint64_t pn) {
    constexpr auto space = Zquic::PktNumSpace::Handshake;
    Base::setTxPN(space, pn + 1);
    return Base::recordTxPkt(space, pn, 100, Zquic::SentFrameRef{}, true);
  }
  bool ackFlight(uint64_t pn, bool sample = false) {
    constexpr auto space = Zquic::PktNumSpace::Handshake;
    Base::AckSnapshot ack;
    ack.level = space;
    ack.ranges.push(Zquic::AckRange{pn, pn});
    ack.receivedUS = uint64_t((sample ?
      Base::txPackets_(space).latestAckSentTime() + Zquic::timeUS(1000) :
      Zm::now()).microsecs());
    Base::processAckFrameTx(ack);
    return !Base::txPackets_(space).count();
  }
  bool discardFlight() {
    Base::discardPktNumSpace_1(Zquic::PktNumSpace::Handshake);
    return !Base::txPackets_(Zquic::PktNumSpace::Handshake).count() &&
      !Base::timersActive();
  }
  bool mixedSpaceLoss() {
    constexpr auto space = Zquic::PktNumSpace::Handshake;
    if (!Base::recordTxPkt<false>(space, 0, 100, Zquic::SentFrameRef{}, true) ||
	!Base::recordTxPkt<false>(space, 1, 100, Zquic::SentFrameRef{}, true)) return false;
    Base::setTxPN(space, 2);
    Base::AckSnapshot ack;
    ack.level = space;
    ack.ranges.push(Zquic::AckRange{1, 1});
    ack.receivedUS = uint64_t((Zm::now() + Zquic::timeUS(100000)).microsecs());
    Base::processAckFrameTx(ack);
    ZuTime expected = Base::txPackets_(space).nextLossTime(1, Base::lossThreshold());
    return *expected && expected > Zm::now() && Base::nextLossTime_() == expected;
  }
  void resetRuntime() { Base::resetTx(); }
  void armAckPTO(ZuTime ack, ZuTime pto) {
    Base::scheduleAckDelayTimer(ack);
    Base::schedulePTOTimer(pto);
  }
  bool installOneRTT(const Zquic::TrafficSecret &secret) {
    return Base::installAppDataKeys_(secret, secret, {});
  }
  void receiveOne() {
    Base::noteAck(Zquic::PktNumSpace::AppData, 1, true, {});
  }
  bool retransmit_() { return false; }
  void queueRetransmit() { }
  void flushTx_() { flushTx_(ZiSockAddr{}); }
  void flushTx_(ZiSockAddr) {
    Zquic::PktBuild packet;
    constexpr auto space = Zquic::PktNumSpace::AppData;
    if (!Base::appendPendingAck(space, packet, true) || !packet.ack(space)) return;
    ackOwnerOK = app()->mx()->invoked(app()->txThread());
    ackGeneration = packet.ackGen();
    Base::ackSentTx(space, packet.ackGen());
    event.post();
  }

  void ackDelayExpired() { fired_(ackDelay); }
  void lossTimeExpired() { fired_(lossTime); }
  void ptoTimer() { fired_(pto); }
  void idleExpired() { fired_(idle); }
  void heartBeatExpired() { fired_(heartBeat); }
  void closeExpired() { fired_(close); }
  void keyDiscardExpired() { fired_(keyDiscard); }
  void pmtudExpired() { fired_(pmtud); }
  void pathExpired() { fired_(path); }

  unsigned fired() const {
    return ackDelay + lossTime + pto + idle + close +
      heartBeat + keyDiscard + pmtud + path;
  }

  bool wait() { return event.timedwait(Zm::now(10)) == 0; }

  void fired_(ZmAtomic<unsigned> &counter) {
    ++counter;
    event.post();
  }
};

static void disconnect(const ZmRef<TimerLink> &link)
{
  ZmBlock<>{}([link](auto wake) mutable {
    link->disconnect_(false, [wake = ZuMv(wake)]() mutable { wake(); });
  });
}

void testIsolatedAckDeadline()
{
  ZuTestScope(testIsolatedAckDeadline);
  TimerApp app;
  ZmRef<TimerLink> link = new TimerLink{&app};
  Zquic::TrafficSecret secret;
  ZuCHECK(trafficSecret(secret, 7) && link->installOneRTT(secret),
    "isolated ACK setup failed");
  app.strict(true);
  app.rxRun([link]() { link->receiveOne(); });
  ZuCHECK(link->wait() && link->ackOwnerOK && link->ackGeneration == 1,
    "final isolated receive did not produce an ACK on Tx after its Rx deadline");
  disconnect(link);
}

void testTimerReservation()
{
  ZuTestScopeRT(testTimerReservation);
  for (unsigned mode = 0; mode < 4; ++mode) {
    TimerApp app;
    ZmRef<TimerLink> link = new TimerLink{&app};
    app.strict(true);
    ZmSemaphore armed, resume, handedOff;
    app.txRun([link, mode, &armed, &resume]() {
      ZuTime first = Zm::now() + Zquic::timeUS(10000);
      link->armPTO(first);
      armed.post();
      resume.wait();
      if (mode == 2) link->cancelAll();
      if (mode == 3) link->resetRuntime();
      // The old callback is already behind this owner turn. A later request
      // reuses it; an earlier request must invalidate its captured identity.
      if (mode != 2)
	link->armPTO(mode == 1 ? first - Zquic::timeUS(1) :
	  Zm::now() + Zquic::timeUS(50000));
    });
    bool ready = !armed.timedwait(Zm::now(10));
    ZmScheduler::Timer sentinel;
    app.mx()->add(&sentinel, Zm::now() + Zquic::timeUS(30000),
      ZmScheduler::Update, [&handedOff](auto &&arm) {
	return arm([&handedOff]() { handedOff.post(); });
      }, app.rxThread());
    ready &= !handedOff.timedwait(Zm::now(10));
    resume.post();
    ZQUIC_CHECK_RT(ready, "scheduler did not hand off reserved PTO");
    ZmBlock<>{}([&app](auto wake) { app.txRun(ZuMv(wake)); });
    if (mode != 1)
      ZQUIC_CHECK_RT(!link->pto, "stale or early reservation ran PTO");
    if (mode == 2) {
      app.txRun([link]() { link->armPTO(Zm::now() + Zquic::timeUS(10000)); });
    }
    ZQUIC_CHECK_RT(link->wait() && link->pto == 1,
      "reserved PTO did not expire exactly once");
    app.mx()->del(&sentinel);
    disconnect(link);
  }
}

void testFlightReservation()
{
  ZuTestScopeRT(testFlightReservation);
  for (unsigned mode = 0; mode < 3; ++mode) {
    TimerApp app;
    ZmRef<TimerLink> link = new TimerLink{&app};
    app.strict(true);
    ZmSemaphore armed, resume, handedOff;
    bool retired = false, sent = false;
    app.txRun([link, mode, &armed, &resume, &retired, &sent]() {
      sent = link->flight(0);
      link->armPTO(Zm::now() + Zquic::timeUS(10000));
      link->scheduleLossTimer(Zm::now() + Zquic::timeUS(10000));
      armed.post();
      resume.wait();
      retired = mode == 2 ? link->discardFlight() : link->ackFlight(0);
      if (mode == 1) sent &= link->flight(1);
    });
    bool ready = !armed.timedwait(Zm::now(10));
    ZmScheduler::Timer sentinel;
    app.mx()->add(&sentinel, Zm::now() + Zquic::timeUS(30000),
      ZmScheduler::Update, [&handedOff](auto &&arm) {
	return arm([&handedOff]() { handedOff.post(); });
      }, app.rxThread());
    ready &= !handedOff.timedwait(Zm::now(10));
    resume.post();
    ZmBlock<>{}([&app](auto wake) { app.txRun(ZuMv(wake)); });
    ZQUIC_CHECK_RT(ready && sent && retired,
      "flight did not retire before the handed-off recovery callbacks");
    ZQUIC_CHECK_RT(!link->pto && !link->lossTime,
      "retired flight ran recovery from an old reservation");
    if (mode == 1)
      ZQUIC_CHECK_RT(link->wait() && link->pto == 1 && !link->lossTime,
	"new flight did not retain its PTO after full ACK suppression");
    app.mx()->del(&sentinel);
    disconnect(link);
  }
}

void testIdlePublication()
{
  ZuTestScope(testIdlePublication);
  TimerApp app;
  ZmRef<TimerLink> link = new TimerLink{&app};
  Zquic::TrafficSecret secret;
  ZuCHECK(trafficSecret(secret, 11) && link->installOneRTT(secret),
    "idle publication setup failed");
  app.strict(true);
  ZmSemaphore armed, resume, activity;
  ZuTime oldDeadline;
  bool sampled = false;
  app.txRun([link, &oldDeadline, &sampled, &armed, &resume]() {
    sampled = link->flight(0) && link->ackFlight(0, true);
    link->startIdleTimerTx(Zquic::timeUS(100000));
    oldDeadline = link->idleDeadline();
    armed.post();
    resume.wait();
  });
  bool ready = !armed.timedwait(Zm::now(10));
  ZmScheduler::Timer sentinel;
  app.mx()->add(&sentinel, oldDeadline + Zquic::timeUS(10000),
    ZmScheduler::Update, [link, &activity](auto &&arm) {
      return arm([link, &activity]() {
	link->notePeerPkt();
	activity.post();
      });
    }, app.rxThread());
  ready &= !activity.timedwait(Zm::now(10));
  resume.post();
  ZuTime newDeadline = ZmBlock<ZuTime>{}([&app, link](auto wake) {
    app.txRun([link, wake = ZuMv(wake)]() mutable { wake(link->idleDeadline()); });
  });
  ZuCHECK(ready && sampled && newDeadline > oldDeadline && !link->idle,
    "handed-off idle callback ignored newly published Rx activity");
  ZuCHECK(link->wait() && link->idle == 1 && Zm::now() >= newDeadline,
    "extended idle deadline did not expire exactly once at or after its deadline");
  app.mx()->del(&sentinel);
  disconnect(link);
}

void testZeroDeadline()
{
  ZuTestScope(testZeroDeadline);
  TimerApp app;
  ZmRef<TimerLink> link = new TimerLink{&app};
  app.strict(true);
  bool initiallyIdle = ZmBlock<bool>{}([&app, link](auto wake) {
    app.txRun([link, wake = ZuMv(wake)]() mutable {
      bool inactive = !link->timersActive();
      link->armPTO(ZuTime{0});
      link->scheduleLossTimer(ZuTime{0});
      wake(inactive);
    });
  });
  ZuCHECK(initiallyIdle && link->wait() && link->wait() &&
      link->pto == 1 && link->lossTime == 1,
    "explicit zero was mistaken for an unset PTO or loss reservation");
  disconnect(link);
}

void testTimerInventory()
{
  ZuTestScope(testTimerInventory);

  TimerApp app;
  ZmRef<TimerLink> link = new TimerLink{&app};
  Zquic::TrafficSecret secret;
  Ztc::HubTelemetry hubData;

  app.telemetry(hubData);
  ZuCHECK(hubData.nLinks == 1 && hubData.transient == 1 &&
      !hubData.down && !hubData.up,
    "new QUIC link was not reflected in hub telemetry");

  ZuCHECK(trafficSecret(secret, 1) && link->installOneRTT(secret),
    "timer test link establish failed");
  app.telemetry(hubData);
  ZuCHECK(hubData.nLinks == 1 && hubData.up == 1 &&
      !hubData.down && !hubData.transient,
    "established QUIC link was not reflected in hub telemetry");
  link->armAll(Zm::now() + Zquic::timeUS(10000));
  bool fired = true;
  for (unsigned i = 0; i < 9; ++i) fired &= link->wait();

  ZuCHECK(fired && link->ackDelay == 1,
    "timer inventory did not complete before its deadline");
  ZuCHECK(link->lossTime == 1, "loss timer did not fire once");
  ZuCHECK(link->pto == 1, "PTO timer did not fire once");
  ZuCHECK(link->idle == 1, "idle timer did not fire once");
  ZuCHECK(link->heartBeat == 1, "heartBeat timer did not fire once");
  ZuCHECK(link->close == 1, "close timer did not fire once");
  ZuCHECK(link->keyDiscard == 1, "key discard timer did not fire once");
  ZuCHECK(link->pmtud == 1, "PMTUD timer did not fire once");
  ZuCHECK(link->path == 1, "path-validation timer did not fire once");
  disconnect(link);
  app.telemetry(hubData);
  ZuCHECK(hubData.nLinks == 1 && hubData.down == 1 &&
      !hubData.transient && !hubData.up,
    "closed QUIC link was not reflected in hub telemetry");
}

void testMixedSpaceDeadline()
{
  ZuTestScope(testMixedSpaceDeadline);
  TimerApp app;
  ZmRef<TimerLink> link = new TimerLink{&app};
  app.strict(true);
  bool selected = ZmBlock<bool>{}([&app, link](auto wake) {
    app.txRun([link, wake = ZuMv(wake)]() mutable {
      bool selected = link->mixedSpaceLoss();
      link->armPTO(Zm::now() + Zquic::timeUS(10000));
      wake(selected);
    });
  });
  ZuCHECK(selected, "empty packet spaces replaced the live Handshake loss deadline");
  ZuCHECK(link->wait() && link->pto == 1 && !link->lossTime,
    "an empty space scheduled loss before the earlier PTO");
  disconnect(link);
}

void testTimerCancel()
{
  ZuTestScope(testTimerCancel);

  TimerApp app;
  ZmRef<TimerLink> link = new TimerLink{&app};
  Zquic::TrafficSecret secret;

  ZuCHECK(trafficSecret(secret, 2) && link->installOneRTT(secret),
    "timer cancel link establish failed");
  ZmRef<TimerLink> sentinel = new TimerLink{&app};
  ZuTime now = Zm::now();
  link->armAll(now + Zquic::timeUS(20000));
  link->cancelAll();
  sentinel->armAckPTO(
    now + Zquic::timeUS(100000), now + Zquic::timeUS(1000000));

  ZuCHECK(sentinel->wait() && !link->fired(),
    "cancelled timer callback fired before a later sentinel");
  disconnect(sentinel);
  disconnect(link);
}

void testTimerPriority()
{
  ZuTestScope(testTimerPriority);

  TimerApp app;
  ZmRef<TimerLink> link = new TimerLink{&app};

  ZuTime now = Zm::now();
  link->armAckPTO(now + Zquic::timeUS(10000), now + Zquic::timeUS(200000));

  ZuCHECK(link->wait() && link->ackDelay == 1,
    "earlier ACK timer did not fire");
  ZuCHECK(!link->pto, "later PTO timer fired early");
  disconnect(link);
}

void testTimerDisconnect()
{
  ZuTestScope(testTimerDisconnect);

  TimerApp app;
  ZmRef<TimerLink> link = new TimerLink{&app};
  ZmRef<TimerLink> sentinel = new TimerLink{&app};

  ZuTime now = Zm::now();
  link->armAll(now + Zquic::timeUS(20000));
  disconnect(link);
  sentinel->armAckPTO(
    now + Zquic::timeUS(100000), now + Zquic::timeUS(1000000));

  ZuCHECK(sentinel->wait() && !link->fired(),
    "disconnect-disarmed timer callback fired before a later sentinel");
  disconnect(sentinel);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();

  ZuTestCall(testTimerInventory);
  ZuTestCall(testTimerReservation);
  ZuTestCall(testFlightReservation);
  ZuTestCall(testIdlePublication);
  ZuTestCall(testZeroDeadline);
  ZuTestCall(testMixedSpaceDeadline);
  ZuTestCall(testIsolatedAckDeadline);
  ZuTestCall(testTimerCancel);
  ZuTestCall(testTimerPriority);
  ZuTestCall(testTimerDisconnect);
}
