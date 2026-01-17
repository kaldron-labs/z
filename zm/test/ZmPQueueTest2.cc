//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// ZmPQueueRx unit test

#include <zlib/ZuLib.hh>

#include <stdio.h>
#include <stdlib.h>

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

void out(bool ok, const char *s) {
  std::cout << (ok ? "OK  " : "NOK ") << s << '\n' << std::flush;
  ZmAssert(ok);
}

#define CHECK(x) (out((x), #x))

using Msg_Data = ZuTuple<uint32_t, unsigned>;
struct Msg_ : public ZmObject, public Msg_Data {
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

ZuDerive(Queue, (ZmPQueue<Msg_, ZmPQueueNode<Msg_>>));

class App : public ZmPQRx<App, Queue, ZmNoLock> {
public:
  using Rx = ZmPQRx<App, Queue, ZmNoLock>;
  using Msg = Queue::Node;
  using Gap = Queue::Gap;

  App(uint32_t head) : m_queue(head) { }

  // -- test interface

  // send a new message into the receiver
  void send(uint32_t key, unsigned length) {
    unsigned precount = m_queue.count_();
    this->rcvd(new Msg(ZuFwdTuple(key, length)));
    printf("send %u, %u (pre-count = %u, post-count = %u)\n",
	(unsigned)key, length, precount, (unsigned)m_queue.count_());
  }

  // respond to next queued resend request
  void respond(unsigned clipHead, unsigned clipTail) {
    if (ZmRef<Msg> msg = ZuMv(m_resend)) {
      printf("respond resend request in(%u, %u) ",
	  (unsigned)msg->Msg_::key(), msg->length());
      if (clipHead) msg->clipHead(clipHead);
      if (clipTail) msg->clipTail(clipTail);
      printf("out(%u, %u)\n",
	  (unsigned)msg->Msg_::key(), msg->length());
      this->rcvd(msg);
    }
  }

  // run next queued (scheduled) dequeue job
  bool runDequeue() {
    if (!m_dequeues) return false;
    puts("run dequeue");
    --m_dequeues;
    this->dequeue();
    return true;
  }

  // run next queued (scheduled) reRequest job
  bool runReRequest() {
    if (!m_reRequests) return false;
    puts("run re-request");
    --m_reRequests;
    Rx::reRequest();
    return true;
  }

  // -- receiver callback interface

  Queue *rxQueue() { return &m_queue; }

  // process message
  void process(Msg *msg) {
    printf("process %u, %u\n", (unsigned)msg->Msg_::key(), msg->length());
  }

  // request resend, as protocol requires it; if now is a subset of
  // prev, then a request may not need to be sent if the protocol
  // is TCP based since the previous request will still be outstanding
  void request(Gap prev, Gap now) {
    printf("request resend prev(%u, %u) now(%u, %u)\n",
      (unsigned)prev.key(), (unsigned)prev.length(),
      (unsigned)now.key(), (unsigned)now.length());
    if (now.length()) m_resend = new Msg(now);
  }

  // re-request resend, as protocol requires it
  void reRequest(Gap now) {
    printf("re-request now(%u, %u)\n",
      (unsigned)now.key(), (unsigned)now.length());
    if (now.length()) m_resend = new Msg(now);
  }

  // schedule dequeue() to be called (possibly from different thread)
  void scheduleDequeue() {
    puts("schedule dequeue");
    ++m_dequeues;
  }
  void rescheduleDequeue() { scheduleDequeue(); }
  void idleDequeue() { }

  // schedule reRequest() to be called (possibly from different thread)
  void scheduleReRequest() {
    puts("schedule re-request");
    ++m_reRequests;
  }
  void rescheduleReRequest() { scheduleReRequest(); }

  // cancel scheduled reRequest()
  void cancelReRequest() {
    puts("cancel re-request");
    m_reRequests = 0;
  }

  // reset queue and pending resend requests
  void reset(uint32_t seqNo) {
    Rx::rxReset(seqNo);
    m_resend = 0;
  }

protected:
  Queue						m_queue;
  ZmList<ZmRef<Msg>, ZmListLock<ZmNoLock> >	m_msgs;
  ZmRef<Msg>					m_resend;
  unsigned					m_dequeues = 0;
  unsigned					m_reRequests = 0;
};

void send(App &a, uint32_t seqNo, unsigned length)
{
  a.send(seqNo, length);
  while (a.runDequeue());
}

int main()
{
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
  CHECK(a.rxQueue()->gap().equals(ZuFwdTuple(14, 1)));
  send(a, 15, 0);
  CHECK(a.rxQueue()->gap().equals(ZuFwdTuple(14, 1)));
  send(a, 15, 1);
  CHECK(a.rxQueue()->gap().equals(ZuFwdTuple(14, 1)));
  send(a, 17, 1);
  send(a, 17, 0);
  send(a, 18, 0);
  send(a, 19, 1);
  send(a, 21, 3);
  CHECK(a.rxQueue()->tail() == 24);
  send(a, 27, 0);
  CHECK(a.rxQueue()->tail() == 27);
  send(a, 14, 8); // should overwrite 15,17,19 and be clipped by 21

  send(a, 28, 1);
  send(a, 27, 3); // should overwrite 28
  send(a, 27, 0);
  send(a, 28, 0);
  send(a, 29, 0);
  CHECK(a.rxQueue()->tail() == 30);
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
  CHECK(a.rxQueue()->tail() == 14);

  a.stopQueuing(12);

  send(a, 15, 1);
  CHECK(a.rxQueue()->gap().equals(ZuFwdTuple(14, 1)));

  CHECK(a.rxQueue()->gap().equals(ZuFwdTuple(14, 1)));
  send(a, 14, 1);

  a.reset(1);
  a.startQueuing();
  send(a, 4, 1);
  CHECK(a.rxQueue()->gap().equals(ZuFwdTuple(1, 3)));
  a.stopQueuing(2);
  while (a.runDequeue());
  CHECK(a.rxQueue()->gap().equals(ZuFwdTuple(2, 2)));
  a.respond(0, 0);
  while (a.runDequeue());
  CHECK(a.rxQueue()->gap().equals(ZuFwdTuple(0, 0)));
  CHECK(a.rxQueue()->head() == 5 && a.rxQueue()->tail() == 5);
}
