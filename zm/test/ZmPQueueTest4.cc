//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// ZmPQueue unit test

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZuDerive.hh>
#include <zlib/ZuObject.hh>
#include <zlib/ZuTraits.hh>
#include <zlib/ZuTuple.hh>

#include <zlib/ZmPQueue.hh>

using namespace ZuTestUtil;

using Msg_Data = ZuTuple<uint32_t, uint64_t>;
struct Msg : public ZuObject, public Msg_Data {
  using Msg_Data::Msg_Data;
  using Msg_Data::operator =;
  Msg(const Msg_Data &v) : Msg_Data(v) { }
  Msg(Msg_Data &&v) : Msg_Data(ZuMv(v)) { }
  uint32_t key() const { return p<0>(); }
  uint64_t length() const { return p<1>(); }
};

ZuDerive(PQueue,
  (ZmPQueue<Msg,
    ZmPQueueNode<Msg,
      ZmPQueueStats<false,
	ZmPQueueOverlap<false,
	  ZmPQueueBits<3,
	    ZmPQueueLevels<3>>>>>>));

using QMsg = PQueue::Node;

void head(PQueue &q, uint32_t key)
{
  ZuTestScope(head);
  log("set head=", key);
  q.head(key);
  ZuCheck(q.head() <= key);
  log("get head=", q.head());
}

void find(const PQueue &q, uint32_t key)
{
  ZuTestScope(find);
  ZmRef<QMsg> msg = q.find(key);
  ZuCheck(msg);
  ZuCheck(msg->Msg::key() <= key);
  ZuCheck(key < (msg->Msg::key() + msg->length()));
  log("find ", msg->Msg::key(), ", ", msg->length());
}

void add(PQueue &q, uint32_t key, uint64_t length)
{
  log("add ", key, ", ", length);
  q.add(new QMsg(ZuFwdTuple(key, length)));
}

int main(int argc, char **argv)
{
  parse(argc, argv);

  ZuTestMain();

  PQueue q(1);

  add(q, 0, 2);
  add(q, 2, 2);
  add(q, 4, 2);
  add(q, 6, 2);
  ZuTestCall(head, q, 3);
  ZuTestCall(find, q, 4);
  ZuTestCall(find, q, 7);
  log(q);
}
