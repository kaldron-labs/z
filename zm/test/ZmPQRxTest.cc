//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// ZmPQRx unit test

#include <stdlib.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuDerive.hh>
#include <zlib/ZuTraits.hh>
#include <zlib/ZuTuple.hh>
#include <zlib/ZuAssert.hh>

#include <zlib/ZmGuard.hh>
#include <zlib/ZmAssert.hh>
#include <zlib/ZmLock.hh>
#include <zlib/ZmPQueue.hh>
#include <zlib/ZmNoLock.hh>
#include <zlib/ZmList.hh>

using namespace ZuTestUtil;

using Msg_Data = ZuTuple<uint32_t, uint64_t>;
struct Msg_ : public ZmObject, public Msg_Data {
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

ZmPQueueDerive(Queue, Msg_, ZmPQueueNode<Msg_>);

class App : public ZmPQRx<App, Queue> {
public:
  using Rx = ZmPQRx<App, Queue>;
  using Msg = Queue::Node;
  using Span = Queue::Span;

  App(uint32_t head) : m_queue(head) { }

  // -- test interface

  // send a new message into the receiver
  void send(uint32_t key, uint64_t length) {
    unsigned precount = m_queue.count_();
    this->rcvd(new Msg(ZuFwdTuple(key, length)));
    log("send ", key, ", ", length,
	" (pre-count = ", precount,
	", post-count = ", m_queue.count_(), ')');
  }

  // respond to next queued resend request
  void respond(uint64_t clipHead, uint64_t clipTail) {
    if (ZmRef<Msg> msg = ZuMv(m_resend)) {
      uint32_t inKey = msg->Msg_::key();
      uint64_t inLength = msg->length();
      if (clipHead) msg->clipHead(clipHead);
      if (clipTail) msg->clipTail(clipTail);
      log("respond resend request in(",
	  inKey, ", ", inLength, ") out(",
	  msg->Msg_::key(),
	  ", ", msg->length(), ')');
      this->rcvd(msg);
    }
  }

  // run next queued (scheduled) dequeue job
  bool runDequeue() {
    if (!m_dequeues) return false;
    log("run dequeue");
    --m_dequeues;
    this->dequeue();
    return true;
  }

  // run next queued (scheduled) reRequest job
  bool runReRequest() {
    if (!m_reRequests) return false;
    log("run re-request");
    --m_reRequests;
    Rx::reRequest();
    return true;
  }

  // -- receiver callback interface

  Queue *rxQueue() { return &m_queue; }

  // process message
  void process(Msg *msg) {
    log("process ", msg->Msg_::key(), ", ", msg->length());
  }

  // request resend, as protocol requires it; if now is a subset of
  // prev, then a request may not need to be sent if the protocol
  // is TCP based since the previous request will still be outstanding
  void request(Span prev, Span now) {
    log("request resend prev(",
      prev.key(), ", ",
      prev.length(), ") now(",
      now.key(), ", ",
      now.length(), ')');
    if (now.length()) m_resend = new Msg(now);
  }

  // re-request resend, as protocol requires it
  void reRequest(Span now) {
    log("re-request now(",
      now.key(), ", ",
      now.length(), ')');
    if (now.length()) m_resend = new Msg(now);
  }

  // schedule dequeue() to be called (possibly from different thread)
  void scheduleDequeue() {
    log("schedule dequeue");
    ++m_dequeues;
  }
  void rescheduleDequeue() { scheduleDequeue(); }
  void idleDequeue() { }

  // schedule reRequest() to be called (possibly from different thread)
  void scheduleReRequest() {
    log("schedule re-request");
    ++m_reRequests;
  }
  void rescheduleReRequest() { scheduleReRequest(); }

  // cancel scheduled reRequest()
  void cancelReRequest() {
    log("cancel re-request");
    m_reRequests = 0;
  }

  // reset queue and pending resend requests
  void reset(uint32_t seqNo) {
    Rx::rxReset(seqNo);
    m_resend = 0;
  }

protected:
  using MsgList = ZmList<ZmRef<Msg>, ZmListLock<ZmNoLock>>;

  Queue		m_queue;
  MsgList	m_msgs;
  ZmRef<Msg>	m_resend;
  unsigned	m_dequeues = 0;
  unsigned	m_reRequests = 0;
};

void send(App &a, uint32_t seqNo, uint64_t length)
{
  a.send(seqNo, length);
  while (a.runDequeue());
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  App a(1);

  // a.send(key, length);
  // a.respond(clipHead, clipTail);
  // a.runDequeue();
  // a.runReRequest();

  a.startQueuing();
  send(a, 1, 1);
  send(a, 2, 2);
  send(a, 4, 1);
  a.stopQueuing(1);

  send(a, 7, 1);
  send(a, 8, 2);
  send(a, 7, 3); // completely overlaps, should be fully clipped (ignored)
  send(a, 9, 2); // should be head-clipped
  send(a, 12, 2);
  send(a, 10, 3); // should be head- and tail-clipped

  a.respond(1, 0); // should send 6, 1 (clipped from 5, 2)

  send(a, 6, 3); // should be head- and tail-clipped
  send(a, 4, 3); // should be head- and tail-clipped

  send(a, 15, 0);
  ZuCheck(a.rxQueue()->gap().equals(ZuFwdTuple(14, 1)));
  send(a, 15, 0);
  ZuCheck(a.rxQueue()->gap().equals(ZuFwdTuple(14, 1)));
  send(a, 15, 1);
  ZuCheck(a.rxQueue()->gap().equals(ZuFwdTuple(14, 1)));
  send(a, 17, 1);
  send(a, 17, 0);
  send(a, 18, 0);
  send(a, 19, 1);
  send(a, 21, 3);
  ZuCheck(a.rxQueue()->tail() == 24);
  send(a, 27, 0);
  ZuCheck(a.rxQueue()->tail() == 27);
  send(a, 14, 8); // should overwrite 15,17,19 and be clipped by 21

  send(a, 28, 1);
  send(a, 27, 3); // should overwrite 28
  send(a, 27, 0);
  send(a, 28, 0);
  send(a, 29, 0);
  ZuCheck(a.rxQueue()->tail() == 30);
  send(a, 24, 10); // should overwrite 27,3

  a.reset(1);
  a.startQueuing();

  send(a, 2, 1);
  send(a, 3, 1);
  send(a, 5, 1);
  send(a, 7, 1);
  send(a, 8, 2);
  send(a, 10, 1);
  send(a, 11, 3);
  ZuCheck(a.rxQueue()->tail() == 14);

  a.stopQueuing(12);

  send(a, 15, 1);
  ZuCheck(a.rxQueue()->gap().equals(ZuFwdTuple(14, 1)));

  ZuCheck(a.rxQueue()->gap().equals(ZuFwdTuple(14, 1)));
  send(a, 14, 1);

  a.reset(1);
  a.startQueuing();
  send(a, 4, 1);
  ZuCheck(a.rxQueue()->gap().equals(ZuFwdTuple(1, 3)));
  a.stopQueuing(2);
  while (a.runDequeue());
  ZuCheck(a.rxQueue()->gap().equals(ZuFwdTuple(2, 2)));
  a.respond(0, 0);
  while (a.runDequeue());
  ZuCheck(a.rxQueue()->gap().equals(ZuFwdTuple(0, 0)));
  ZuCheck(a.rxQueue()->head() == 5 && a.rxQueue()->tail() == 5);
}
