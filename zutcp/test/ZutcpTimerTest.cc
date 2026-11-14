//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/Zutcp.hh>


using namespace ZuTestUtil;

using TxBufAlloc = Zutcp::TxBufAlloc<>;

namespace Zutcp {
struct TCPStateTest {
  template <typename T> static bool rtoLossHandoff(T &link) {
    auto &c = static_cast<typename T::Base &>(link);
    c.cancelTimers_();
    auto start = c.app()->transportNow();
    c.m_txTCPState.flags |= TCPOptionState::SACKReceive;
    c.m_rack.rtt = timeUS(30000);
    c.m_txNext = 2;
    c.m_txDataCredit.set(2);
    for (unsigned i = 0; i < 2; ++i) {
      ZmRef<TxRanges::Node> source = new TxRanges::Node{};
      TxRange range{source, 0, 1, i};
      SentPkt packet;
      packet.pn = i + 1;
      packet.bytes = 1;
      packet.ackEliciting = packet.inFlight = true;
      packet.addFrame(SentFrameRef::data(range, false));
      if (!c.m_txPkts.add(packet) || !c.recordTxUnackd(i, 1, false)) return false;
      c.m_txPkts.acceptedData(i + 1, start + timeUS(i * 15000));
      c.m_congestion.sent(1);
    }
    c.m_dataRto.sent(start, c.m_rtt, c.app()->rtoParams());
    c.app()->clock += timeUS(20000);
    c.dataRtoExpired();
    return c.m_txPkts.lost() == 1 && c.m_txPkts.bytesInFlight() == 1 &&
      c.m_rack.deadline == start + timeUS(45000) && c.m_lossTimer &&
      c.m_lossTimerOut == c.m_rack.deadline && !c.m_rtoTimer &&
      !*c.m_rtoTimerOut && c.m_dataRto.deadline() == start + timeUS(60000);
  }
  template <typename T> static bool restoreRtoAfterLoss(T &link) {
    auto &c = static_cast<typename T::Base &>(link);
    c.app()->clock += timeUS(25000);
    // Model the scheduled node being dequeued, then run the actual RACK pass.
    c.app()->mx()->cancel(&c.m_lossTimer);
    c.rackExpired(c.m_txRuntimeGen, {});
    return !*c.m_rack.deadline && !c.m_lossTimer && !*c.m_lossTimerOut &&
      c.m_rtoTimer && c.m_rtoTimerOut == c.m_dataRto.deadline() &&
      c.m_rtoTimerOut == c.app()->clock + timeUS(15000);
  }
  template <typename T> static void armLoss(T &link, ZuTime out) {
    auto &c = static_cast<typename T::Base &>(link);
    c.m_txTCPState.flags |= TCPOptionState::SACKReceive;
    c.m_rack.deadline = out;
    c.scheduleLossTimer();
  }
  template <typename T> static void heartbeatDeadline(T &link, ZuTime out) {
    auto &c = static_cast<typename T::Base &>(link);
    c.m_heartBeatBase = out - c.app()->heartBeat();
  }
  template <typename T> static void byteAck(T &link, uint64_t next) {
    auto &c = static_cast<typename T::Base &>(link);
    Zutcp::TxAckWork work;
    work.ack.byteNext = next;
    work.gen = c.m_txRuntimeGen;
    c.processAckTx(ZuMv(work));
  }
};
} // namespace Zutcp


static ZuBSpan span_(const uint8_t *data, unsigned len)
{
  return ZuBSpan{data, len};
}



struct TimerApp : public Zutcp::Hub<TimerApp> {
  using Base = Zutcp::Hub<TimerApp>;
  ZiMultiplex multiplex;
  ZuTime clock;
  bool native = false;
  Zi::IOBufAllocator *dataTxAllocator() const { return Zutcp::inlineAllocator(); }
  bool nativeTx() const { return native; }

  TimerApp(ZuTime user = ZuTime{120}) : multiplex{mxParams()} {
    ZiAssert(Base::init(
	Zutcp::HubParams(&multiplex)
	  .heartBeat(ZuTime{10})
	  .userTimeout(user)
	  .initialRto(Zutcp::timeUS(20000)).minRto(Zutcp::timeUS(10000))),
      "Zutcp", (), "timer test app init failed", return);
    ZiAssert(multiplex.start(), "Zutcp", (),
      "timer test multiplexer start failed", return);
  }
  ~TimerApp() {
    Base::final();
    multiplex.stop();
  }

  ZuTime transportNow() const {
    return *clock ? clock : Base::transportNow();
  }

  bool rxInvoked() const { return true; }
  bool txInvoked() const { return true; }
  template <typename L> void rxRun(L l) { l(); }
  template <typename L> void rxInvoke(L l) { l(); }
  template <typename O, typename L> void rxInvoke(O *, L l) { l(); }
  template <typename L> void txRun(L l) { l(); }
  template <typename L> void txInvoke(L l) { l(); }
  template <typename O, typename L> void txInvoke(O *, L l) { l(); }

  static ZiMxParams mxParams() {
    return ZiMxParams()
      .scheduler([](auto &s) { s.nThreads(4); })
      .rxThread(1).txThread(2);
  }

};

struct TimerLink;
struct TimerLink :
  public ZmNHeap<TimerLink>,
  public Zutcp::Connection<TimerApp, TimerLink, TxBufAlloc> {
friend inline ZuStringT<"Zutcp.TimerTest.Link"> ZmNHeapID(TimerLink *);
  using ZmNHeap<TimerLink>::operator new;
  using ZmNHeap<TimerLink>::operator delete;

  using Base = Zutcp::Connection<TimerApp, TimerLink, TxBufAlloc>;
  ZmSemaphore		event;
  ZmAtomic<unsigned>	connect = 0;
  ZmAtomic<unsigned>	user = 0;
  ZmAtomic<unsigned>	closeDrain = 0, finWait = 0;
  ZmAtomic<unsigned>	ackDelay = 0;
  ZmAtomic<unsigned>	lossTime = 0;
  ZmAtomic<unsigned>	ptoCount = 0;
  ZmAtomic<unsigned>	rto = 0;
  ZmAtomic<unsigned>	persist = 0;
  ZmAtomic<unsigned>	idle = 0;
  ZmAtomic<unsigned>	heartBeat = 0;
  ZmAtomic<unsigned>	pmtud = 0;

  using Base::txNext;
  int process(Zutcp::Reader &) { return 0; }
  using Base::Base;

  void armAll(ZuTime out, bool data = false) {
    Base::scheduleAckDelayTimer(out);
    Zutcp::TCPStateTest::armLoss(*this, out);
    Base::schedulePTOTimer(out);
    Base::startIdleTimerTx(out - app()->transportNow());
    Zutcp::TCPStateTest::heartbeatDeadline(*this, out);
    Base::scheduleHBTimer(out);
    Base::schedulePMTUDTimer(out);
    if (data) armData();
  }
  void armData() {
    Zutcp::TxRange range{nullptr, 0, 1, 0};
    Base::acceptTxData(Zutcp::SentFrameRef::data(range, false));
  }
  bool flushTx() { return false; }
  bool armPersist() {
    Base::txCredit(0);
    return Base::enqueue_(Zutcp::inlineAllocator()->allocBuf(nullptr, 1));
  }
  bool sendPersistProbe() { recordFire(persist); return true; }
  bool establish() {
    if (!Base::startHandshake()) return false;
    Base::establish();
    return Base::established();
  }
  void cancelAll() { Base::cancelTimers_(); }
  void deliverPTO() { Base::cxnTimer(Zutcp::CxnTimer::PTO); }
  void deliverAck() { Base::cxnTimer(Zutcp::CxnTimer::AckDelay); }
  bool startSetup() { return Base::startHandshake(); }
  void finishSetup() { Base::establish(); Base::cancelHBTimer(); }
  void resetSetup() { Base::resetLink(); }
  void deliverConnect() { Base::cxnTimer(Zutcp::CxnTimer::Connect); }
  bool pendingTimers() const { return Base::timersActive(); }
  void deliverUser() { Base::cxnTimer(Zutcp::CxnTimer::User); }
  void deliverCloseDrain() { Base::cxnTimer(Zutcp::CxnTimer::CloseDrain); }
  void deliverFinWait() { Base::cxnTimer(Zutcp::CxnTimer::FinWait); }
  bool peerData(unsigned offset, unsigned length, bool fin) {
    Base::rxCredit(128);
    auto packet = Zutcp::inlineAllocator()->allocBuf(nullptr, length);
    if (!packet) return false;
    memset(packet->data(), 0x6b, length);
    Zutcp::ByteData frame{.offset = offset, .length = length,
      .payload = {packet->data(), length}, .fin = fin};
    return Base::receiveDataFrame(frame, ZuMv(packet));
  }
  void deliverPersist() { Base::cxnTimer(Zutcp::CxnTimer::Persist); }
  void sentRange(uint64_t offset, unsigned length, bool fin = false) {
    Base::acceptTxData(Zutcp::SentFrameRef::data(
      Zutcp::TxRange{nullptr, 0, length, offset}, fin));
  }
  void byteAck(uint64_t next) { Zutcp::TCPStateTest::byteAck(*this, next); }
  void armAckPTO(ZuTime ack, ZuTime pto) {
    Base::scheduleAckDelayTimer(ack);
    Base::schedulePTOTimer(pto);
  }


  void ackDelayExpired() { recordFire(ackDelay); }
  void connectExpired() { recordFire(connect); }
  void policyExpired(Zutcp::CxnTimer::T action) {
    if (action == Zutcp::CxnTimer::User) recordFire(user);
    else if (action == Zutcp::CxnTimer::CloseDrain) recordFire(closeDrain);
    else if (action == Zutcp::CxnTimer::FinWait) recordFire(finWait);
  }
  void lossTimeExpired() { recordFire(lossTime); }
  void pto() { recordFire(ptoCount); }
  void queueRetransmit() { recordFire(rto); }
  bool retransmit(bool = false, unsigned = 2) {
    recordFire(rto);
    return false;
  }
  void idleExpired() { recordFire(idle); }
  void heartBeatExpired() { recordFire(heartBeat); }

  void pmtudExpired() { recordFire(pmtud); }

  unsigned fired() const {
    return connect + user + closeDrain + finWait + ackDelay + lossTime + ptoCount + idle +
      heartBeat + pmtud + rto + persist;
  }

  bool wait() { return event.timedwait(Zm::now(10)) == 0; }

  void recordFire(ZmAtomic<unsigned> &counter) {
    ++counter;
    event.post();
  }


};

void testConnectDeadline()
{
  ZuTestScope(testConnectDeadline);
  TimerApp app;
  app.clock = Zm::now() + ZuTime{1000};
  ZmRef<TimerLink> link = new TimerLink{&app};
  ZuCHECK(link->startSetup() && link->pendingTimers(),
    "setup must arm its absolute connect deadline");
  app.clock += ZuTime{29};
  link->deliverConnect();
  ZuCHECK(!link->connect && !link->startSetup(),
    "an early callback or repeated setup cannot expire or restart the deadline");
  app.clock += ZuTime{1};
  link->deliverConnect();
  link->deliverConnect();
  ZuCHECK(link->connect == 1, "connect deadline must dispatch exactly once");

  link->resetSetup();
  ZuCHECK(link->startSetup(), "reset must admit a fresh setup deadline");
  app.clock += ZuTime{10};
  link->resetSetup();
  ZuCHECK(link->startSetup(), "another setup generation must replace the old deadline");
  app.clock += ZuTime{20};
  link->deliverConnect();
  ZuCHECK(link->connect == 1 && link->pendingTimers(),
    "a callback at the old deadline must leave the newer setup timer armed");
  link->finishSetup();
  app.clock += ZuTime{100};
  link->deliverConnect();
  ZuCHECK(link->connect == 1 && !link->pendingTimers(),
    "establishment must cancel the connect deadline and remove its scheduler node");
  link->disconnect();
}

void testUserDeadline()
{
  ZuTestScope(testUserDeadline);
  {
    TimerApp app;
    app.clock = Zm::now() + ZuTime{1000};
    ZmRef<TimerLink> link = new TimerLink{&app};
    ZuCHECK(link->startSetup(), "user policy setup must begin");
    link->finishSetup();
    link->sentRange(0, 2);
    app.clock += ZuTime{60};
    link->byteAck(1);
    app.clock += ZuTime{40};
    link->sentRange(2, 1);
    link->sentRange(0, 2);
    app.clock += ZuTime{20};
    link->deliverUser();
    ZuCHECK(!link->user && !link->closed(),
      "partial cumulative progress must replace the old user deadline");
    app.clock += ZuTime{59};
    link->byteAck(1);
    link->deliverUser();
    ZuCHECK(!link->user, "duplicate cumulative ACKs cannot postpone the deadline");
    app.clock += ZuTime{1};
    link->deliverUser();
    link->deliverUser();
    ZuCHECK(link->user == 1 && link->closed() && !link->pendingTimers(),
      "new sends and retransmission cannot extend the last cumulative-progress deadline");
    link->disconnect();
  }
  {
    TimerApp app;
    app.clock = Zm::now() + ZuTime{1000};
    ZmRef<TimerLink> link = new TimerLink{&app};
    ZuCHECK(link->startSetup(), "queued zero-window setup must begin");
    link->finishSetup();
    ZuCHECK(link->armPersist(), "zero credit must retain one queued byte");
    app.clock += ZuTime{60};
    link->deliverPersist();
    app.clock += ZuTime{59};
    link->deliverPersist();
    link->deliverUser();
    ZuCHECK(!link->user && link->persist == 2 && !link->txNext(),
      "persist probes must leave the original queued-data deadline unchanged");
    app.clock += ZuTime{1};
    link->deliverUser();
    ZuCHECK(link->user == 1 && link->closed() && link->txBufferedBytes() == 1,
      "unsent zero-window data must expire without freeing backing before owner teardown");
    link->disconnect();
  }
  for (bool fin : {false, true}) {
    TimerApp app;
    app.clock = Zm::now() + ZuTime{1000};
    ZmRef<TimerLink> link = new TimerLink{&app};
    ZuCHECK(link->startSetup(), "quiescence policy setup must begin");
    link->finishSetup();
    link->sentRange(0, fin ? 0 : 2, fin);
    app.clock += ZuTime{60};
    link->byteAck(fin ? 1 : 2);
    ZuCHECK(!link->pendingTimers(), "full data or FIN acknowledgment must cancel the episode");
    app.clock += ZuTime{60};
    link->deliverUser();
    ZuCHECK(!link->user && !link->closed(), "an old timer cannot abort a quiescent connection");
    if (!fin) {
      link->sentRange(2, 1);
      app.clock += ZuTime{119};
      link->deliverUser();
      ZuCHECK(!link->user, "new data after quiescence must start a fresh deadline");
      app.clock += ZuTime{1};
      link->deliverUser();
      ZuCHECK(link->user == 1, "a fresh outstanding episode must still expire");
    }
    link->disconnect();
  }
  {
    TimerApp app{ZuTime{0}};
    app.clock = Zm::now() + ZuTime{1000};
    ZmRef<TimerLink> link = new TimerLink{&app};
    ZuCHECK(link->startSetup(), "disabled user policy setup must begin");
    link->finishSetup();
    link->sentRange(0, 1);
    app.clock += ZuTime{1000};
    link->deliverUser();
    ZuCHECK(!link->user && !link->closed(), "zero userTimeout must disable policy expiry");
    link->disconnect();
  }
}

void testCloseDeadlines()
{
  ZuTestScope(testCloseDeadlines);
  using State = Zutcp::TcpCloseState;
  {
    TimerApp app{ZuTime{0}};
    app.clock = Zm::now() + ZuTime{1000};
    ZmRef<TimerLink> link = new TimerLink{&app};
    ZuCHECK(link->startSetup(), "active close setup must begin");
    link->finishSetup();
    ZuCHECK(link->armPersist(), "zero credit must retain admitted data before FIN");
    link->fin();
    ZuCHECK(link->tcpCloseState() == State::FinWait1 && !link->finReady(),
      "local FIN must wait behind admitted data without closing Rx");
    app.clock += ZuTime{29};
    link->fin();
    link->deliverPersist();
    link->deliverCloseDrain();
    ZuCHECK(!link->closeDrain && !link->closed(),
      "repeated FIN and persist cannot reset the absolute close deadline");
    app.clock += ZuTime{1};
    link->deliverCloseDrain();
    link->deliverCloseDrain();
    ZuCHECK(link->closeDrain == 1 && link->closed() && !link->pendingTimers() &&
	link->txBufferedBytes() == 1,
      "close drain expires once and retains backing until actual owner teardown");
    link->disconnect();
  }
  for (unsigned order = 0; order < 3; ++order) {
    TimerApp app{ZuTime{0}};
    app.clock = Zm::now() + ZuTime{1000};
    ZmRef<TimerLink> link = new TimerLink{&app};
    ZuCHECK(link->startSetup(), "FIN transition setup must begin");
    link->finishSetup();
    if (order == 2) {
      ZuCHECK(link->peerData(2, 0, true) && link->tcpCloseState() == State::Open,
	"out-of-order FIN cannot advance close state ahead of missing data");
      ZuCHECK(link->peerData(0, 2, false) && link->tcpCloseState() == State::CloseWait,
	"contiguous gap fill must publish CLOSE-WAIT without closing local output");
    }
    link->fin();
    ZuCHECK(link->tcpCloseState() == (order == 2 ? State::LastAck : State::FinWait1),
      "local close must distinguish passive LAST-ACK from active FIN-WAIT-1");
    link->recordTxUnackd(0, 0, true);
    link->sentRange(0, 0, true);
    app.clock += ZuTime{10};
    if (order == 0) {
      link->byteAck(1);
      link->ackTxUnackd(0, 0, true);
      ZuCHECK(link->tcpCloseState() == State::FinWait2,
	"local FIN acknowledgment must enter FIN-WAIT-2 while peer direction remains open");
    } else if (order == 1) {
      ZuCHECK(link->peerData(0, 0, true) && link->tcpCloseState() == State::Closing,
	"peer FIN before local acknowledgment must enter simultaneous CLOSING");
    }
    app.clock += ZuTime{20};
    link->deliverCloseDrain();
    if (order == 0) {
      ZuCHECK(!link->closeDrain && !link->closed(),
	"acknowledged FIN must cancel the original drain deadline");
      app.clock += ZuTime{39};
      link->deliverFinWait();
      ZuCHECK(!link->finWait, "FIN-WAIT-2 needs its independent full interval");
      app.clock += ZuTime{1};
      link->deliverFinWait();
      link->deliverFinWait();
      ZuCHECK(link->finWait == 1 && link->closed() && !link->pendingTimers(),
	"orphaned FIN-WAIT-2 must expire once and cancel all protocol timers");
    } else {
      ZuCHECK(link->closeDrain == 1 && link->closed(),
	"unacknowledged simultaneous or passive FIN retains the original drain deadline");
    }
    link->disconnect();
  }
  for (unsigned order = 0; order < 3; ++order) {
    TimerApp app{ZuTime{0}};
    app.clock = Zm::now() + ZuTime{1000};
    ZmRef<TimerLink> link = new TimerLink{&app};
    ZuCHECK(link->startSetup(), "completed close setup must begin");
    link->finishSetup();
    if (order == 2) ZuCHECK(link->peerData(0, 0, true), "passive FIN must be admitted");
    link->fin();
    link->recordTxUnackd(0, 0, true);
    link->sentRange(0, 0, true);
    if (order == 1) ZuCHECK(link->peerData(0, 0, true), "simultaneous FIN must be admitted");
    link->byteAck(1);
    link->ackTxUnackd(0, 0, true);
    if (!order) ZuCHECK(link->peerData(0, 0, true), "peer FIN must finish FIN-WAIT-2");
    ZuCHECK(link->tcpCloseState() == (order == 2 ? State::Closed : State::TimeWait) &&
	!link->pendingTimers(),
      "both FIN directions must cancel drain and FIN-wait policy timers");
    app.clock += ZuTime{120};
    link->deliverCloseDrain();
    link->deliverFinWait();
    ZuCHECK(!link->closeDrain && !link->finWait && !link->closed(),
      "retired close-policy callbacks cannot abort a completed handshake");
    link->disconnect();
  }
}

void testTimerInventory()
{
  ZuTestScope(testTimerInventory);

  TimerApp app;
  ZmRef<TimerLink> link = new TimerLink{&app};
  Ztc::HubTelemetry hubData;

  app.telemetry(hubData);
  ZuCHECK(hubData.nLinks == 1 && hubData.transient == 1 &&
      !hubData.down && !hubData.up,
    "new TCP link was not reflected in hub telemetry");

  ZuCHECK(link->establish(),
    "timer test link establish failed");
  app.telemetry(hubData);
  ZuCHECK(hubData.nLinks == 1 && hubData.up == 1 &&
      !hubData.down && !hubData.transient,
    "established TCP link was not reflected in hub telemetry");
  link->armAll(app.transportNow() + Zutcp::timeUS(10000));
  bool fired = true;
  for (unsigned i = 0; i < 6; ++i) fired &= link->wait();

  ZuCHECK(fired && link->ackDelay == 1,
    "timer inventory did not complete before its deadline");
  ZuCHECK(link->lossTime == 1, "loss timer did not fire once");
  ZuCHECK(link->ptoCount == 1, "PTO timer did not fire once");
  ZuCHECK(link->idle == 1, "idle timer did not fire once");
  ZuCHECK(link->heartBeat == 1, "heartBeat timer did not fire once");
  ZuCHECK(link->pmtud == 1, "PMTUD timer did not fire once");
  link->disconnect();
  app.telemetry(hubData);
  ZuCHECK(hubData.nLinks == 1 && hubData.down == 1 &&
      !hubData.transient && !hubData.up,
    "closed TCP link was not reflected in hub telemetry");
}

void testTimerCancel()
{
  ZuTestScope(testTimerCancel);

  TimerApp app;
  ZmRef<TimerLink> link = new TimerLink{&app};

  ZuCHECK(link->establish(),
    "timer cancel link establish failed");
  ZmRef<TimerLink> sentinel = new TimerLink{&app};
  ZuTime now = app.transportNow();
  link->armAll(now + Zutcp::timeUS(20000), true);
  link->cancelAll();
  sentinel->armAckPTO(
    now + Zutcp::timeUS(100000), now + Zutcp::timeUS(1000000));

  ZuCHECK(sentinel->wait() && !link->fired(),
    "cancelled timer callback fired before a later sentinel");
  sentinel->disconnect();
  link->disconnect();
}

void testTimerPriority()
{
  ZuTestScope(testTimerPriority);

  TimerApp app;
  ZmRef<TimerLink> link = new TimerLink{&app};

  ZuTime now = app.transportNow();
  link->armAckPTO(now + Zutcp::timeUS(10000), now + Zutcp::timeUS(200000));

  ZuCHECK(link->wait() && link->ackDelay == 1,
    "earlier ACK timer did not fire");
  ZuCHECK(!link->ptoCount, "later PTO timer fired early");
  link->disconnect();
}

void testTimerDisconnect()
{
  ZuTestScope(testTimerDisconnect);

  TimerApp app;
  ZmRef<TimerLink> link = new TimerLink{&app};
  ZmRef<TimerLink> sentinel = new TimerLink{&app};

  ZuTime now = app.transportNow();
  ZuCHECK(link->establish(), "RTO disconnect test setup failed");
  link->armAll(now + Zutcp::timeUS(20000), true);
  link->disconnect();
  sentinel->armAckPTO(
    now + Zutcp::timeUS(100000), now + Zutcp::timeUS(1000000));

  ZuCHECK(sentinel->wait() && !link->fired(),
    "disconnect-disarmed timer callback fired before a later sentinel");
  sentinel->disconnect();
}

void testDataRtoTimer()
{
  ZuTestScope(testDataRtoTimer);
  TimerApp app;
  ZmRef<TimerLink> link = new TimerLink{&app};
  ZuCHECK(link->establish(), "DATA RTO timer setup failed");
  link->armData();
  ZuCHECK(link->wait() && link->rto == 1 && !link->ptoCount,
    "DATA RTO must dispatch independently of SYN retry timer");
  link->disconnect();
}

void testPersistTimer()
{
  ZuTestScope(testPersistTimer);
  TimerApp app;
  ZmRef<TimerLink> link = new TimerLink{&app};
  ZuCHECK(link->establish() && link->armPersist(), "persist timer setup failed");
  ZuCHECK(link->wait() && link->persist == 1 && !link->ptoCount && !link->rto,
    "persist must dispatch separately from SYN retry timer and DATA RTO");
  link->cancelAll();
  ZuCHECK(link->armPersist(), "a canceled persist schedule must permit a new episode");
  link->cancelAll();
  ZmRef<TimerLink> sentinel = new TimerLink{&app};
  ZuTime now = app.transportNow();
  sentinel->armAckPTO(now + Zutcp::timeUS(1100000), now + ZuTime{10});
  ZuCHECK(sentinel->wait() && link->persist == 1,
    "a canceled persist callback must not run before a later sentinel");
  sentinel->disconnect();
  link->disconnect();
}

void testTimerGuard()
{
  ZuTestScope(testTimerGuard);
  TimerApp app;
  app.clock = Zm::now();
  ZmRef<TimerLink> link = new TimerLink{&app};
  ZuCHECK(link->establish(), "timer guard setup failed");
  link->armAckPTO(app.clock + ZuTime{100}, app.clock + ZuTime{200});
  link->deliverPTO();
  ZuCHECK(!link->ptoCount, "an early scheduler callback cannot expire transport time");
  link->armAckPTO(app.clock + ZuTime{150}, app.clock + ZuTime{300});
  app.clock += ZuTime{100};
  link->deliverAck();
  ZuCHECK(link->ackDelay == 1, "Advance must retain the earlier deadline");
  app.clock += ZuTime{100};
  link->deliverPTO();
  ZuCHECK(!link->ptoCount, "an old callback cannot expire a later replacement deadline");
  app.clock += ZuTime{100};
  link->deliverPTO();
  link->deliverPTO();
  ZuCHECK(link->ptoCount == 1, "due transport timer must dispatch exactly once");
  link->armAckPTO(app.clock + ZuTime{100}, app.clock + ZuTime{200});
  app.clock += ZuTime{100};
  link->deliverAck();
  ZuCHECK(link->ackDelay == 2, "a dispatched target must permit a fresh Advance arm");
  link->cancelAll();
  app.clock += ZuTime{100};
  link->deliverPTO();
  ZuCHECK(link->ptoCount == 1, "cancel must disarm a callback already queued on Tx");
  link->disconnect();
}

void testRtoLossTimerHandoff()
{
  ZuTestScope(testRtoLossTimerHandoff);
  TimerApp app;
  app.native = true;
  app.clock = Zm::now() + ZuTime{1000};
  ZmRef<TimerLink> link = new TimerLink{&app};
  ZuCHECK(link->establish(), "RTO/RACK handoff requires an established native timer owner");
  ZuCHECK(Zutcp::TCPStateTest::rtoLossHandoff(*link),
    "RTO scan must arm the actual earlier Loss node before suppressing the physical RTO");
  ZuCHECK(Zutcp::TCPStateTest::restoreRtoAfterLoss(*link),
    "a Loss pass with no later delivery evidence must restore the actual fallback RTO node");
  link->disconnect();
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();

  ZuTestCall(testTimerInventory);
  ZuTestCall(testConnectDeadline);
  ZuTestCall(testUserDeadline);
  ZuTestCall(testCloseDeadlines);
  ZuTestCall(testTimerCancel);
  ZuTestCall(testTimerPriority);
  ZuTestCall(testTimerDisconnect);
  ZuTestCall(testDataRtoTimer);
  ZuTestCall(testRtoLossTimerHandoff);
  ZuTestCall(testPersistTimer);
  ZuTestCall(testTimerGuard);
}
