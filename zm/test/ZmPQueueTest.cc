//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// ZmPQueue unit test

#include <stdlib.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuDerive.hh>
#include <zlib/ZuObject.hh>
#include <zlib/ZuTraits.hh>
#include <zlib/ZuTuple.hh>

#include <zlib/ZmPQueue.hh>
#include <zlib/ZmNoLock.hh>

using namespace ZuTestUtil;

using Msg_Data = ZuTuple<uint32_t, unsigned>;
struct Msg : public ZuObject, public Msg_Data {
  using Msg_Data::Msg_Data;
  using Msg_Data::operator =;
  Msg(const Msg_Data &v) : Msg_Data(v) { }
  Msg(Msg_Data &&v) : Msg_Data(ZuMv(v)) { }
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
  void write(const I &) { }
  unsigned elems() const { return 1; }
};

ZuDerive(PQueue,
  (ZmPQueue<Msg,
    ZmPQueueNode<Msg,
      ZmPQueueBits<1,
	ZmPQueueLevels<4>>>>));

using QMsg = PQueue::Node;

void head(PQueue &q, uint32_t seqNo)
{
  log("head ", seqNo);
  q.head(seqNo);
}
void dequeue(PQueue &q)
{
  while (ZmRef<QMsg> msg = q.dequeue())
    log("process ", msg->Msg::key(), ", ", msg->length());
}
void add(PQueue &q, uint32_t seqNo, unsigned length)
{
  log("send ", seqNo, ", ", length);
  ZmRef<QMsg> msg = q.rotate(ZmRef<QMsg>(new QMsg(ZuFwdTuple(seqNo, length))));
  log("send - head ", q.head(),
      " gap ", q.gap().key(),
      ", ", q.gap().length());
  while (msg) {
    log("send - process ", msg->Msg::key(), ", ", msg->length());
    msg = q.dequeue();
    log("send - head ", q.head(),
	" gap ", q.gap().key(),
	", ", q.gap().length());
  }
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  PQueue q(1);

  add(q, 1, 1);
  add(q, 2, 2);
  add(q, 4, 1);

  add(q, 7, 1);
  add(q, 8, 2);
  add(q, 7, 3); // completely overlaps, should be fully clipped (ignored)
  add(q, 9, 2); // should be head-clipped
  add(q, 12, 2);
  add(q, 10, 3); // should be head- and tail-clipped
  add(q, 6, 3); // should be tail-clipped

  add(q, 4, 3); // should be head- and tail-clipped, trigger dequeue

  add(q, 15, 1);
  ZuCheck(q.gap().equals(ZuFwdTuple(14, 1)));
  add(q, 17, 1);
  add(q, 19, 1);
  add(q, 21, 3);
  add(q, 14, 8); // should overwrite 15,17,19 and be clipped by 21

  add(q, 28, 1);
  add(q, 27, 3); // should overwrite 28
  add(q, 24, 10); // should overwrite 27

  head(q, 1);

  add(q, 2, 1);
  add(q, 3, 1);
  add(q, 5, 1);
  add(q, 7, 1);
  add(q, 8, 2);
  add(q, 10, 1);
  add(q, 11, 3);

  head(q, 12); // should leave 12+2 in place
  add(q, 15, 1);
  ZuCheck(q.gap().equals(ZuFwdTuple(14, 1)));
  dequeue(q);
  ZuCheck(q.gap().equals(ZuFwdTuple(14, 1)));
  add(q, 14, 1);
}
