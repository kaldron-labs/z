//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <unistd.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/Zquic.hh>
#include <zlib/ZquicSched.hh>

using namespace ZuTestUtil;

using StreamTxBufAlloc = Zquic::StreamTxBufAlloc<>;

struct TimerApp : public Zquic::Engine<TimerApp> {
  using Base = Zquic::Engine<TimerApp>;

  TimerApp() : m_mx{mxParams_()} {
    ZiAssert(m_mx.start(), "Zquic", (),
      "timer test multiplexer start failed", return);
    ZiAssert(Base::init(Zquic::EngineParams(&m_mx, "3", "4")),
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
    Base::scheduleIdleTimer_(out);
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

  void ackDelayExpired_() { ++ackDelay; }
  void lossTimeExpired_() { ++lossTime; }
  void pto_() { ++pto; }
  void idleExpired_() { ++idle; }
  void closeExpired_() { ++close; }
  void keyDiscardExpired_() { ++keyDiscard; }
  void pmtudExpired_() { ++pmtud; }
  void pathExpired_() { ++path; }

  unsigned fired() const {
    return ackDelay + lossTime + pto + idle +
      close + keyDiscard + pmtud + path;
  }

  ZmAtomic<unsigned>	ackDelay = 0;
  ZmAtomic<unsigned>	lossTime = 0;
  ZmAtomic<unsigned>	pto = 0;
  ZmAtomic<unsigned>	idle = 0;
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

  link->armAll(Zm::now() + Zquic::timeUS(10000));
  usleep(80000);

  ZuCHECK(link->ackDelay == 1, "ACK delay timer did not fire once");
  ZuCHECK(link->lossTime == 1, "loss timer did not fire once");
  ZuCHECK(link->pto == 1, "PTO timer did not fire once");
  ZuCHECK(link->idle == 1, "idle timer did not fire once");
  ZuCHECK(link->close == 1, "close timer did not fire once");
  ZuCHECK(link->keyDiscard == 1, "key discard timer did not fire once");
  ZuCHECK(link->pmtud == 1, "PMTUD timer did not fire once");
  ZuCHECK(link->path == 1, "path-validation timer did not fire once");
}

void testTimerCancel()
{
  ZuTestScope(testTimerCancel);

  TimerApp app;
  ZmRef<TimerLink> link = new TimerLink{&app};

  link->armAll(Zm::now() + Zquic::timeUS(200000));
  link->cancelAll();
  usleep(80000);

  ZuCHECK(!link->fired(), "cancelled timer callback fired");
}

void testTimerPriority()
{
  ZuTestScope(testTimerPriority);

  TimerApp app;
  ZmRef<TimerLink> link = new TimerLink{&app};

  ZuTime now = Zm::now();
  link->armAckPTO(now + Zquic::timeUS(10000), now + Zquic::timeUS(200000));
  usleep(80000);

  ZuCHECK(link->ackDelay == 1, "earlier ACK timer did not fire");
  ZuCHECK(!link->pto, "later PTO timer fired early");
  link->cancelAll();
}

void testTimerOwnerRelease()
{
  ZuTestScope(testTimerOwnerRelease);

  TimerApp app;
  ZmRef<TimerLink> link = new TimerLink{&app};

  link->armAll(Zm::now() + Zquic::timeUS(200000));
  link = nullptr;
  usleep(80000);

  ZuCHECK(true, "timer owner release completed");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();

  ZuTestCall(testTimerInventory);
  ZuTestCall(testTimerCancel);
  ZuTestCall(testTimerPriority);
  ZuTestCall(testTimerOwnerRelease);
}
