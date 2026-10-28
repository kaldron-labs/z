//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// ZmPQTx unit test

#include <stdlib.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuDerive.hh>
#include <zlib/ZuObject.hh>
#include <zlib/ZuTraits.hh>
#include <zlib/ZuTuple.hh>
#include <zlib/ZuTime.hh>

#include <zlib/ZmPQueue.hh>
#include <zlib/ZmNoLock.hh>
#include <zlib/ZmHash.hh>

using namespace ZuTestUtil;

using Msg_Data = ZuTuple<uint32_t, uint64_t>;
struct Msg_ : public ZuObject, public Msg_Data {
  using Msg_Data::Msg_Data;
  using Msg_Data::operator =;
  Msg_(const Msg_Data &v) : Msg_Data(v) { }
  Msg_(Msg_Data &&v) : Msg_Data(ZuMv(v)) { }
  uint32_t key() const { return p<0>(); }
  uint64_t length() const { return p<1>(); }
  uint64_t clipHead(uint64_t length) {
    p<0>() += length;
    return p<1>() -= length;
  }
  uint64_t clipTail(uint64_t length) {
    return p<1>() -= length;
  }
  template <typename I>
  void write(const I &i) { }
};

ZmPQueueDerive(Queue, Msg_, ZmPQueueNode<ZuObject>);

using XXX = decltype(ZuDeclVal<const Queue::Node &>().key());

class App : public ZmPQTx<App, Queue> {
public:
  using Tx = ZmPQTx<App, Queue>;
  using Msg = Queue::Node;
  using Span = Queue::Span;
  using Fn = Queue::Fn;

  App(uint32_t head) : m_queue(head) { }

  // --- test interface

  bool runSend() {
    if (!m_sends) return false;
    log("run send");
    --m_sends;
    this->Tx::send();
    return true;
  }

  bool runResend() {
    if (!m_resends) return false;
    log("run resend");
    --m_resends;
    this->resend();
    return true;
  }

  bool runArchive() {
    if (!m_archives) return false;
    log("run archive");
    --m_archives;
    this->archive();
    return true;
  }

  void failSend(bool v) { m_failSend = v; }
  void failResend(bool v) { m_failResend = v; }
  void stopOnFailure(bool v) { m_stopOnFailure = v; }
  bool stopPending() const { return m_stopPending; }
  bool completeStop() {
    if (!m_stopPending) return false;
    m_stopPending = false;
    Tx::stop();
    return true;
  }
  unsigned sendSchedules() const { return m_sendSchedules; }
  unsigned resendSchedules() const { return m_resendSchedules; }
  unsigned sendIdles() const { return m_sendIdles; }
  unsigned resendIdles() const { return m_resendIdles; }
  unsigned sendFlags() const { return m_sendFlags; }
  unsigned resendFlags() const { return m_resendFlags; }
  unsigned sendCalls() const { return m_sendCalls; }
  unsigned sendGapCalls() const { return m_sendGapCalls; }

  // --- sender callback interface

  Queue *txQueue() { return &m_queue; }

  // send message
  bool send_(Msg *msg, bool) {
    if (m_failSend) {
      if (m_stopOnFailure) m_stopPending = true;
      return false;
    }
    ++m_sendCalls;
    log("send ", msg->key(), ", ", msg->length());
    m_sent = msg;
    return true;
  }
  bool resend_(Msg *msg, bool) { // resend
    if (m_failResend) {
      if (m_stopOnFailure) m_stopPending = true;
      return false;
    }
    log("resend ", msg->key(), ", ", msg->length());
    m_resent = msg;
    return true;
  }

  // send gap (optional)
  bool sendGap_(const Span &gap, bool) {
    ++m_sendGapCalls;
    log("sendGap ", gap.key(), ", ", gap.length());
    m_sentGap = gap;
    return true;
  }
  bool resendGap_(const Span &gap, bool) { // resend
    log("resendGap ", gap.key(), ", ", gap.length());
    m_resentGap = gap;
    return true;
  }

  // archive message (once ackd by receiver(s))
  void archive_(Msg *msg) {
    log("ackd ", msg->key(), ", ", msg->length());
    m_ackd = msg;
    archived(msg->key() + msg->length());
  }

  // retrieve message from archive
  ZmRef<Msg> retrieve_(Key key, Key) {
    log("retrieve ", key);
    if (!m_ackd) return 0;
    Fn item{m_ackd->data()};
    if (key >= item.key() && (key - item.key()) < item.length())
      return m_ackd;
    return 0;
  }

  // schedule send() to be called (possibly from different thread)
  void scheduleSend() {
    log("schedule send");
    ++m_sends;
    ++m_sendSchedules;
    m_sendFlags = flags();
  }
  void rescheduleSend() { scheduleSend(); }
  void idleSend() { ++m_sendIdles; }

  // schedule resend() to be called (possibly from different thread)
  void scheduleResend() {
    log("schedule resend");
    ++m_resends;
    ++m_resendSchedules;
    m_resendFlags = flags();
  }
  void rescheduleResend() { scheduleResend(); }
  void idleResend() { ++m_resendIdles; }

  // schedule archive() to be called (possibly from different thread)
  void scheduleArchive() {
    log("schedule archive");
    ++m_archives;
  }
  void rescheduleArchive() { scheduleArchive(); }
  void idleArchive() { }

  bool checkSent(Msg *msg) {
    bool b = m_sent == msg;
    m_sent = nullptr;
    return b;
  }
  bool checkSentGap(const Span &gap) {
    bool r = m_sentGap == gap;
    m_sentGap = Span();
    return r;
  }
  bool checkResent(Msg *msg) {
    bool b = m_resent == msg;
    m_resent = nullptr;
    return b;
  }
  bool checkResentGap(const Span &gap) {
    bool r = m_resentGap == gap;
    m_resentGap = Span();
    return r;
  }
  bool checkArchived(Msg *msg) {
    return m_ackd == msg;
  }

protected:
  Queue				m_queue;
  unsigned			m_sends = 0;
  unsigned			m_resends = 0;
  unsigned			m_archives = 0;
  bool				m_failSend = false;
  bool				m_failResend = false;
  bool				m_stopOnFailure = false;
  bool				m_stopPending = false;
  unsigned			m_sendSchedules = 0;
  unsigned			m_resendSchedules = 0;
  unsigned			m_sendIdles = 0;
  unsigned			m_resendIdles = 0;
  unsigned			m_sendFlags = 0;
  unsigned			m_resendFlags = 0;
  unsigned			m_sendCalls = 0;
  unsigned			m_sendGapCalls = 0;
  ZmRef<Msg>			m_sent;
  Span				m_sentGap;
  ZmRef<Msg>			m_resent;
  Span				m_resentGap;
  ZmRef<Msg>			m_ackd;
};

class TestLock {
public:
  void lock() {
    if (m_depth) m_reentered = true;
    ++m_depth;
  }
  int trylock() { lock(); return 0; }
  void unlock() { if (m_depth) --m_depth; }

  static void reset() { m_depth = 0; m_reentered = false; }
  static bool reentered() { return m_reentered; }

private:
  static inline unsigned	m_depth = 0;
  static inline bool		m_reentered = false;
};

template <typename NTP>
class AckApp : public ZmPQTx<AckApp<NTP>, Queue, NTP> {
public:
  using Tx = ZmPQTx<AckApp<NTP>, Queue, NTP>;
  using Msg = Queue::Node;
  using Span = Queue::Span;
  using Key = Queue::Key;

  AckApp(Key head) : m_queue{head} { }

  Queue *txQueue() { return &m_queue; }

  bool send_(Msg *, bool) { return true; }
  bool resend_(Msg *, bool) { return true; }
  bool sendGap_(const Span &, bool) { return true; }
  bool resendGap_(const Span &, bool) { return true; }

  void archive_(Msg *msg) {
    (void)this->flags();
    ++m_archived;
    m_archivedKey = msg->key();
    if constexpr (Tx::Ordered)
      this->archived(msg->key() + msg->length());
  }
  ZmRef<Msg> retrieve_(Key, Key) { return nullptr; }

  void scheduleSend() { ++m_sendPending; }
  void rescheduleSend() { ++m_sendPending; }
  void idleSend() { }
  void scheduleResend() { ++m_resendPending; }
  void rescheduleResend() { ++m_resendPending; }
  void idleResend() { }
  void scheduleArchive() { ++m_archivePending; ++m_archiveSchedules; }
  void rescheduleArchive() { ++m_archivePending; ++m_archiveSchedules; }
  void idleArchive() { }

  bool runSend() {
    if (!m_sendPending) return false;
    --m_sendPending;
    this->Tx::send();
    return true;
  }
  bool runArchive() {
    if (!m_archivePending) return false;
    --m_archivePending;
    this->archive();
    return true;
  }

  unsigned archivedCount() const { return m_archived; }
  Key archivedKey() const { return m_archivedKey; }
  unsigned archiveSchedules() const { return m_archiveSchedules; }

private:
  Queue		m_queue;
  unsigned	m_sendPending = 0;
  unsigned	m_resendPending = 0;
  unsigned	m_archivePending = 0;
  unsigned	m_archiveSchedules = 0;
  unsigned	m_archived = 0;
  Key		m_archivedKey = 0;
};

template <bool FromKey>
static bool restart(App &a, App::Key key)
{
  // The owner must complete its queued stop before the next queued start.
  if (a.stopPending()) return false;
  if constexpr (FromKey) a.start(key);
  else a.start();
  return true;
}

template <bool FromKey>
void testStopFailure()
{
  ZuTestScope(testStopFailure);
  App a{1};
  a.txReset(1);
  a.stopOnFailure(true);
  ZmRef<App::Msg> msg = new App::Msg(App::Span{1, 1});
  ZuCheck(restart<FromKey>(a, 1));
  a.failSend(true);
  a.send(msg);
  ZuCheck(a.runSend() && a.stopPending());
  ZuCheck(!a.runSend() && !a.checkSent(msg));
  // Return from the failed sender first; only its owner continuation stops it.
  ZuCheck(a.completeStop() && !a.stopPending());
  a.failSend(false);
  ZuCheck(restart<FromKey>(a, 1));
  while (a.runSend());
  ZuCheck(a.checkSent(msg));
  a.failResend(true);
  a.resend(App::Span{1, 1});
  ZuCheck(a.runResend() && a.stopPending());
  ZuCheck(!a.runResend() && !a.checkResent(msg));
  ZuCheck(a.completeStop() && !a.stopPending());
  a.failResend(false);
  ZuCheck(restart<FromKey>(a, 2));
  while (a.runResend());
  ZuCheck(a.checkResent(msg));
}

template <bool FromKey>
void testRunningRetry()
{
  ZuTestScope(testRunningRetry);
  App a{1};
  a.txReset(1);
  ZuCheck(restart<FromKey>(a, 1));
  ZmRef<App::Msg> msg = new App::Msg(App::Span{1, 1});
  a.failSend(true);
  a.send(msg);
  ZuCheck(a.runSend() && !a.runSend() && !a.stopPending());
  unsigned schedules = a.sendSchedules(), idles = a.sendIdles();
  a.failSend(false);
  ZuCheck(restart<FromKey>(a, 1));
  ZuCheck(a.sendSchedules() == schedules + 1 && a.sendIdles() == idles);
  while (a.runSend());
  ZuCheck(a.checkSent(msg));
  a.failResend(true);
  a.resend(App::Span{1, 1});
  ZuCheck(a.runResend() && !a.runResend() && !a.stopPending());
  schedules = a.resendSchedules();
  idles = a.resendIdles();
  a.failResend(false);
  ZuCheck(restart<FromKey>(a, 2));
  ZuCheck(a.resendSchedules() == schedules + 1 && a.resendIdles() == idles);
  while (a.runResend());
  ZuCheck(a.checkResent(msg));
}

template <bool FromKey>
void testStartIdle()
{
  ZuTestScope(testStartIdle);
  App a{1};
  a.txReset(1);
  ZuCheck(restart<FromKey>(a, 1));
  ZuCheck(a.sendIdles() == 1 && a.resendIdles() == 1);
  ZuCheck(restart<FromKey>(a, 1));
  ZuCheck(a.sendIdles() == 2 && a.resendIdles() == 2);
  ZmRef<App::Msg> msg = new App::Msg(App::Span{1, 1});
  a.send(msg);
  ZuCheck(a.sendSchedules() == 1 && a.sendFlags() == a.flags());
  unsigned sendIdles = a.sendIdles();
  ZuCheck(restart<FromKey>(a, 1));
  ZuCheck(restart<FromKey>(a, 1));
  ZuCheck(a.sendSchedules() == 1 && a.sendIdles() == sendIdles);
  while (a.runSend());
  ZuCheck(a.checkSent(msg) && a.sendIdles() == sendIdles + 1);
  a.resend(App::Span{1, 1});
  ZuCheck(a.resendSchedules() == 1 && a.resendFlags() == a.flags());
  unsigned resendIdles = a.resendIdles();
  ZuCheck(restart<FromKey>(a, 2));
  ZuCheck(restart<FromKey>(a, 2));
  ZuCheck(a.resendSchedules() == 1 && a.resendIdles() == resendIdles);
  while (a.runResend());
  ZuCheck(a.checkResent(msg) && a.resendIdles() == resendIdles + 1);
}

void testRestore()
{
  ZuTestScope(testRestore);
  App a{1};
  a.txReset(1);
  ZuCheck(a.txRestore(5, 3));
  ZuCheck(a.txQueue()->head() == 1 && a.txQueue()->tail() == 1);
  ZuCheck(!a.txRestore(4, 5));
  ZmRef<App::Msg> old = new App::Msg(App::Span{3, 1});
  ZmRef<App::Msg> next = new App::Msg(App::Span{5, 1});
  a.txQueue()->add(old);
  a.txQueue()->add(next);
  ZuCheck(!a.txRestore(1, 1));
  a.start();
  unsigned flags = a.flags();
  ZuCheck(!a.txRestore(1, 1) && a.flags() == flags);
  while (a.runSend());
  ZuCheck(a.checkSent(next) && a.sendCalls() == 1 && !a.sendGapCalls());
  a.resend(App::Span{3, 1});
  while (a.runResend());
  ZuCheck(a.checkResent(old));
  ZuCheck(a.txQueue()->has(3) && a.txQueue()->has(5));
  a.ackd(4);
  while (a.runArchive());
  ZuCheck(a.checkArchived(old));
  ZuCheck(!a.txQueue()->has(3) && a.txQueue()->has(5));
  a.stop();
  ZuCheck(!a.txRestore(1, 1));
  a.start();
  ZuCheck(!a.runSend());
}

int main(int argc, char **argv)
{
  parse(argc, argv);

  ZuTestMain();

  ZmHeapMgr::init("ZmPQueue", 0, 0, ZmHeapConfig{100});

  ZuTestCall(testRestore);
  ZuTestCall(testStopFailure<false>);
  ZuTestCall(testStopFailure<true>);
  ZuTestCall(testRunningRetry<false>);
  ZuTestCall(testRunningRetry<true>);
  ZuTestCall(testStartIdle<false>);
  ZuTestCall(testStartIdle<true>);

  App a(1);
  ZmRef<App::Msg> msg, msg2;

  a.start();

  // basic load, send, resend, ack, resend test
  msg = new App::Msg(App::Span(1, 1));
  a.send(msg);
  while (a.runSend());
  ZuCheck(a.checkSent(msg));
  a.resend(App::Span(1, 1));
  while (a.runResend());
  ZuCheck(a.checkResent(msg));
  a.ackd(2);
  while (a.runArchive());
  ZuCheck(a.checkArchived(msg));
  a.resend(App::Span(1, 1));
  while (a.runResend());
  ZuCheck(a.checkResent(msg));

  // load, send, resend, ack, resend test with gap
  msg = new App::Msg(App::Span(3, 1));
  a.send(msg);
  while (a.runSend());
  ZuCheck(a.checkSentGap(App::Span(2, 1)));
  ZuCheck(a.checkSent(msg));
  a.resend(App::Span(2, 2));
  while (a.runResend());
  ZuCheck(a.checkResentGap(App::Span(2, 1)));
  ZuCheck(a.checkResent(msg));
  a.ackd(4);
  while (a.runArchive());
  ZuCheck(a.checkArchived(msg));
  a.resend(App::Span(2, 2));
  while (a.runResend());
  ZuCheck(a.checkResentGap(App::Span(2, 1)));
  ZuCheck(a.checkResent(msg));
 
  // load, send, resend, ack, resend test with
  // misaligned partially overlapping resend requests, including gaps
  a.txReset(1);
  msg = new App::Msg(App::Span(3, 3));
  a.send(msg);
  while (a.runSend());
  ZuCheck(a.checkSentGap(App::Span(1, 2)));
  ZuCheck(a.checkSent(msg));
  msg2 = new App::Msg(App::Span(8, 3));
  a.send(msg2);
  while (a.runSend());
  ZuCheck(a.checkSentGap(App::Span(6, 2)));
  ZuCheck(a.checkSent(msg2));
  a.resend(App::Span(4, 5));
  while (a.runResend());
  ZuCheck(a.checkResentGap(App::Span(6, 2)));
  ZuCheck(a.checkResent(msg2));
  a.ackd(4);
  while (a.runArchive());
  ZuCheck(a.checkArchived(msg));
  a.resend(App::Span(4, 5));
  while (a.runResend());
  ZuCheck(a.checkResentGap(App::Span(6, 2)));
  ZuCheck(a.checkResent(msg2));

  // resend request spanning unsent data
  a.txReset(1);
  msg = new App::Msg(App::Span(3, 3));
  a.send(msg);
  msg2 = new App::Msg(App::Span(8, 3));
  a.send(msg2);
  a.resend(App::Span(1, 12));
  a.runResend();
  ZuCheck(a.checkResentGap(App::Span(1, 2)));
  ZuCheck(a.checkResent(msg));
  a.runResend();
  ZuCheck(a.checkResentGap(App::Span(6, 2)));
  ZuCheck(a.checkResent(msg2));
  a.runResend();
  ZuCheck(a.checkResentGap(App::Span(11, 2)));

  using ExplicitOrdered = AckApp<ZmPQTxOrdered<true>>;
  using Unordered = AckApp<ZmPQTxOrdered<false>>;
  using LockedOrdered = AckApp<ZmPQTxLock<TestLock>>;
  using LockedUnordered =
    AckApp<ZmPQTxLock<TestLock, ZmPQTxOrdered<false>>>;

  ZuAssert(ExplicitOrdered::Tx::Ordered);
  ZuAssert(!Unordered::Tx::Ordered);
  ZuAssert(LockedOrdered::Tx::Ordered);
  ZuAssert(!LockedUnordered::Tx::Ordered);

  {
    ExplicitOrdered ordered{1};
    ordered.start();
    ZmRef<ExplicitOrdered::Msg> orderedMsg =
      new ExplicitOrdered::Msg(ExplicitOrdered::Span(1, 1));
    ordered.send(orderedMsg);
    while (ordered.runSend());
    ordered.ackd(2);
    ZuCheck(ordered.archiveSchedules() == 1 &&
      !ordered.archivedCount() && ordered.txQueue()->count_() == 1);
    while (ordered.runArchive());
    ZuCheck(ordered.archivedCount() == 1 && ordered.archivedKey() == 1 &&
      !ordered.txQueue()->count_() && !ordered.txQueue()->length_());
  }

  {
    Unordered unordered{1};
    unordered.start(1);
    unordered.stop();
    unordered.start();
    ZuCheck(!unordered.archiveSchedules());

    ZmRef<Unordered::Msg> msg1 =
      new Unordered::Msg(Unordered::Span(1, 1));
    ZmRef<Unordered::Msg> msg2_ =
      new Unordered::Msg(Unordered::Span(2, 1));
    ZmRef<Unordered::Msg> msg3 =
      new Unordered::Msg(Unordered::Span(3, 1));
    unordered.send(msg1);
    unordered.send(msg2_);
    unordered.send(msg3);
    while (unordered.runSend());

    unordered.ackd(2);
    ZuCheck(unordered.archivedCount() == 1 &&
      unordered.archivedKey() == 2 && !unordered.txQueue()->find(2) &&
      unordered.txQueue()->find(1) && unordered.txQueue()->find(3) &&
      unordered.txQueue()->count_() == 2 &&
      unordered.txQueue()->length_() == 2);
    unordered.ackd(3);
    ZuCheck(unordered.archivedCount() == 2 &&
      unordered.archivedKey() == 3 && unordered.txQueue()->count_() == 1);
    unordered.ackd(1);
    ZuCheck(unordered.archivedCount() == 3 &&
      unordered.archivedKey() == 1 && !unordered.txQueue()->count_() &&
      !unordered.txQueue()->length_());
    unordered.ackd(1);
    unordered.ackd(2);
    unordered.ackd(9);
    ZuCheck(unordered.archivedCount() == 3 &&
      !unordered.archiveSchedules());
  }

  {
    Unordered unordered{10};
    unordered.start(10);
    ZmRef<Unordered::Msg> msg =
      new Unordered::Msg(Unordered::Span(10, 3));
    unordered.send(msg);
    while (unordered.runSend());
    unordered.ackd(9);
    unordered.ackd(11);
    unordered.ackd(13);
    ZuCheck(!unordered.archivedCount() &&
      unordered.txQueue()->count_() == 1 &&
      unordered.txQueue()->length_() == 3);
    unordered.ackd(10);
    ZuCheck(unordered.archivedCount() == 1 &&
      unordered.archivedKey() == 10 && !unordered.txQueue()->count_());
  }

  {
    TestLock::reset();
    LockedUnordered unordered{1};
    unordered.start();
    unordered.send(new LockedUnordered::Msg(LockedUnordered::Span(1, 1)));
    unordered.ackd(1);
    ZuCheck(unordered.archivedCount() == 1 && !TestLock::reentered());

    TestLock::reset();
    LockedOrdered ordered{1};
    ordered.start();
    ordered.stop();
    ZuCheck(!TestLock::reentered());
  }

  log(Ztc::heapCSV());
}
