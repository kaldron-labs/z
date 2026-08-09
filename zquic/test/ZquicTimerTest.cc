//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/Zquic.hh>

#include <zpicotls/openssl.h>

using namespace ZuTestUtil;

using StreamTxBufAlloc = Zquic::StreamTxBufAlloc<>;

static ZuBSpan span_(const uint8_t *data, unsigned len)
{
  return ZuBSpan{data, len};
}

static bool trafficSecret_(Zquic::TrafficSecret &secret, uint8_t seed)
{
  uint8_t bytes[32];
  for (unsigned i = 0; i < sizeof(bytes); ++i) bytes[i] = seed + i;
  return Zquic::PktProt::deriveSecret(
    secret, &ptls_openssl_aes128gcmsha256, span_(bytes, sizeof(bytes)));
}

struct TimerApp : public Zquic::Hub<TimerApp> {
  using Base = Zquic::Hub<TimerApp>;

  TimerApp() : m_mx{mxParams_()} {
    ZiAssert(m_mx.start(), "Zquic", (),
      "timer test multiplexer start failed", return);
    ZiAssert(Base::init(
	Zquic::HubParams(&m_mx, "3", "4")
	  .heartBeat(ZuTime{10})),
      "Zquic", (), "timer test app init failed", return);
  }
  ~TimerApp() {
    Base::final();
    m_mx.stop();
  }

  bool rxInvoked() const { return true; }
  bool txInvoked() const { return true; }
  template <typename L> void rxRun(L l) { l(); }
  template <typename L> void rxInvoke(L l) { l(); }
  template <typename O, typename L> void rxInvoke(O *, L l) { l(); }
  template <typename L> void txRun(L l) { l(); }
  template <typename L> void txInvoke(L l) { l(); }
  template <typename O, typename L> void txInvoke(O *, L l) { l(); }

private:
  static ZiMxParams mxParams_() {
    return ZiMxParams()
      .scheduler([](auto &s) { s.nThreads(4); })
      .rxThread(1).txThread(2);
  }

  ZiMultiplex	m_mx;
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
  public Zquic::Link<TimerApp, TimerLink, StreamTxBufAlloc, TimerStream> {
  using Base = Zquic::Link<TimerApp, TimerLink, StreamTxBufAlloc, TimerStream>;
  using Base::Base;

  void armAll(ZuTime out) {
    Base::scheduleAckDelayTimer_(out);
    Base::scheduleLossTimer_(out);
    Base::schedulePTOTimer_(out);
    Base::startIdleTimerTx_(out - Zm::now());
    Base::scheduleHBTimer_(out);
    Base::scheduleCloseTimer_(out);
    Base::scheduleKeyDiscardTimer_(out);
    Base::schedulePMTUDTimer_(out);
    Base::schedulePathTimer_(out);
  }
  void cancelAll() { Base::cancelTimers_(); }
  void armAckPTO(ZuTime ack, ZuTime pto) {
    Base::scheduleAckDelayTimer_(ack);
    Base::schedulePTOTimer_(pto);
  }
  bool installOneRTT(const Zquic::TrafficSecret &secret) {
    return Base::installAppDataKeys_(secret, secret, {});
  }

  void ackDelayExpired_() { fired_(ackDelay); }
  void lossTimeExpired_() { fired_(lossTime); }
  void pto_() { fired_(pto); }
  void idleExpired_() { fired_(idle); }
  void heartBeatExpired_() { fired_(heartBeat); }
  void closeExpired_() { fired_(close); }
  void keyDiscardExpired_() { fired_(keyDiscard); }
  void pmtudExpired_() { fired_(pmtud); }
  void pathExpired_() { fired_(path); }

  unsigned fired() const {
    return ackDelay + lossTime + pto + idle + close +
      heartBeat + keyDiscard + pmtud + path;
  }

  bool wait() { return event.timedwait(Zm::now(10)) == 0; }

  void fired_(ZmAtomic<unsigned> &counter) {
    ++counter;
    event.post();
  }

  ZmSemaphore		event;
  ZmAtomic<unsigned>	ackDelay = 0;
  ZmAtomic<unsigned>	lossTime = 0;
  ZmAtomic<unsigned>	pto = 0;
  ZmAtomic<unsigned>	idle = 0;
  ZmAtomic<unsigned>	heartBeat = 0;
  ZmAtomic<unsigned>	close = 0;
  ZmAtomic<unsigned>	keyDiscard = 0;
  ZmAtomic<unsigned>	pmtud = 0;
  ZmAtomic<unsigned>	path = 0;
};

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

  ZuCHECK(trafficSecret_(secret, 1) && link->installOneRTT(secret),
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
  link->disconnect();
  app.telemetry(hubData);
  ZuCHECK(hubData.nLinks == 1 && hubData.down == 1 &&
      !hubData.transient && !hubData.up,
    "closed QUIC link was not reflected in hub telemetry");
}

void testTimerCancel()
{
  ZuTestScope(testTimerCancel);

  TimerApp app;
  ZmRef<TimerLink> link = new TimerLink{&app};
  Zquic::TrafficSecret secret;

  ZuCHECK(trafficSecret_(secret, 2) && link->installOneRTT(secret),
    "timer cancel link establish failed");
  ZmRef<TimerLink> sentinel = new TimerLink{&app};
  ZuTime now = Zm::now();
  link->armAll(now + Zquic::timeUS(20000));
  link->cancelAll();
  sentinel->armAckPTO(
    now + Zquic::timeUS(100000), now + Zquic::timeUS(1000000));

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

  ZuTime now = Zm::now();
  link->armAckPTO(now + Zquic::timeUS(10000), now + Zquic::timeUS(200000));

  ZuCHECK(link->wait() && link->ackDelay == 1,
    "earlier ACK timer did not fire");
  ZuCHECK(!link->pto, "later PTO timer fired early");
  link->disconnect();
}

void testTimerDisconnect()
{
  ZuTestScope(testTimerDisconnect);

  TimerApp app;
  ZmRef<TimerLink> link = new TimerLink{&app};
  ZmRef<TimerLink> sentinel = new TimerLink{&app};

  ZuTime now = Zm::now();
  link->armAll(now + Zquic::timeUS(20000));
  link->disconnect();
  sentinel->armAckPTO(
    now + Zquic::timeUS(100000), now + Zquic::timeUS(1000000));

  ZuCHECK(sentinel->wait() && !link->fired(),
    "disconnect-disarmed timer callback fired before a later sentinel");
  sentinel->disconnect();
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();

  ZuTestCall(testTimerInventory);
  ZuTestCall(testTimerCancel);
  ZuTestCall(testTimerPriority);
  ZuTestCall(testTimerDisconnect);
}
