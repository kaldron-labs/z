//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// ZmPQueueTx unit test

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

using Msg_Data = ZuTuple<uint32_t, unsigned>;
struct Msg_ : public ZuObject, public Msg_Data {
  using Msg_Data::Msg_Data;
  using Msg_Data::operator =;
  Msg_(const Msg_Data &v) : Msg_Data(v) { }
  Msg_(Msg_Data &&v) : Msg_Data(ZuMv(v)) { }
  uint32_t key() const { return p<0>(); }
  unsigned length() const { return p<1>(); }
  unsigned clipHead(unsigned length) {
    p<0>() += length;
    return p<1>() -= length;
  }
  unsigned clipTail(unsigned length) {
    return p<1>() -= length;
  }
  template <typename I>
  void write(const I &i) { }
};

ZuDerive(Queue, (ZmPQueue<Msg_, ZmPQueueNode<ZuObject>>));

using XXX = decltype(ZuDeclVal<const typename Queue::Node &>().key());

class App : public ZmPQTx<App, Queue, ZmNoLock> {
public:
  using Tx = ZmPQTx<App, Queue, ZmNoLock>;
  using Msg = Queue::Node;
  using Gap = Queue::Gap;
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

  // --- sender callback interface

  Queue *txQueue() { return &m_queue; }

  // send message
  bool send_(Msg *msg, bool) {
    log("send ", msg->key(), ", ", msg->length());
    m_sent = msg;
    return true;
  }
  bool resend_(Msg *msg, bool) { // resend
    log("resend ", msg->key(), ", ", msg->length());
    m_resent = msg;
    return true;
  }

  // send gap (optional)
  bool sendGap_(const Gap &gap, bool) {
    log("sendGap ", gap.key(), ", ", gap.length());
    m_sentGap = gap;
    return true;
  }
  bool resendGap_(const Gap &gap, bool) { // resend
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
  }
  void rescheduleSend() { scheduleSend(); }
  void idleSend() { }

  // schedule resend() to be called (possibly from different thread)
  void scheduleResend() {
    log("schedule resend");
    ++m_resends;
  }
  void rescheduleResend() { scheduleResend(); }
  void idleResend() { }

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
  bool checkSentGap(const Gap &gap) {
    bool r = m_sentGap == gap;
    m_sentGap = Gap();
    return r;
  }
  bool checkResent(Msg *msg) {
    bool b = m_resent == msg;
    m_resent = nullptr;
    return b;
  }
  bool checkResentGap(const Gap &gap) {
    bool r = m_resentGap == gap;
    m_resentGap = Gap();
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
  ZmRef<Msg>			m_sent;
  Gap				m_sentGap;
  ZmRef<Msg>			m_resent;
  Gap				m_resentGap;
  ZmRef<Msg>			m_ackd;
};

int main(int argc, char **argv)
{
  parse(argc, argv);

  ZuTestMain();

  ZmHeapMgr::init("ZmPQueue", 0, ZmHeapConfig{100});

  App a(1);
  ZmRef<App::Msg> msg, msg2;

  a.start();

  // basic load, send, resend, ack, resend test
  msg = new App::Msg(App::Gap(1, 1));
  a.send(msg);
  while (a.runSend());
  ZuCheck(a.checkSent(msg));
  a.resend(App::Gap(1, 1));
  while (a.runResend());
  ZuCheck(a.checkResent(msg));
  a.ackd(2);
  while (a.runArchive());
  ZuCheck(a.checkArchived(msg));
  a.resend(App::Gap(1, 1));
  while (a.runResend());
  ZuCheck(a.checkResent(msg));

  // load, send, resend, ack, resend test with gap
  msg = new App::Msg(App::Gap(3, 1));
  a.send(msg);
  while (a.runSend());
  ZuCheck(a.checkSentGap(App::Gap(2, 1)));
  ZuCheck(a.checkSent(msg));
  a.resend(App::Gap(2, 2));
  while (a.runResend());
  ZuCheck(a.checkResentGap(App::Gap(2, 1)));
  ZuCheck(a.checkResent(msg));
  a.ackd(4);
  while (a.runArchive());
  ZuCheck(a.checkArchived(msg));
  a.resend(App::Gap(2, 2));
  while (a.runResend());
  ZuCheck(a.checkResentGap(App::Gap(2, 1)));
  ZuCheck(a.checkResent(msg));
 
  // load, send, resend, ack, resend test with
  // misaligned partially overlapping resend requests, including gaps
  a.txReset(1);
  msg = new App::Msg(App::Gap(3, 3));
  a.send(msg);
  while (a.runSend());
  ZuCheck(a.checkSentGap(App::Gap(1, 2)));
  ZuCheck(a.checkSent(msg));
  msg2 = new App::Msg(App::Gap(8, 3));
  a.send(msg2);
  while (a.runSend());
  ZuCheck(a.checkSentGap(App::Gap(6, 2)));
  ZuCheck(a.checkSent(msg2));
  a.resend(App::Gap(4, 5));
  while (a.runResend());
  ZuCheck(a.checkResentGap(App::Gap(6, 2)));
  ZuCheck(a.checkResent(msg2));
  a.ackd(4);
  while (a.runArchive());
  ZuCheck(a.checkArchived(msg));
  a.resend(App::Gap(4, 5));
  while (a.runResend());
  ZuCheck(a.checkResentGap(App::Gap(6, 2)));
  ZuCheck(a.checkResent(msg2));

  // resend request spanning unsent data
  a.txReset(1);
  msg = new App::Msg(App::Gap(3, 3));
  a.send(msg);
  msg2 = new App::Msg(App::Gap(8, 3));
  a.send(msg2);
  a.resend(App::Gap(1, 12));
  a.runResend();
  ZuCheck(a.checkResentGap(App::Gap(1, 2)));
  ZuCheck(a.checkResent(msg));
  a.runResend();
  ZuCheck(a.checkResentGap(App::Gap(6, 2)));
  ZuCheck(a.checkResent(msg2));
  a.runResend();
  ZuCheck(a.checkResentGap(App::Gap(11, 2)));

  log(ZmHeapMgr::csv());
}
