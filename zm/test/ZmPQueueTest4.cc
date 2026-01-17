//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// ZmPQueue unit test

#include <zlib/ZuLib.hh>

#include <iostream>

#include <zlib/ZuDerive.hh>
#include <zlib/ZuObject.hh>
#include <zlib/ZuTraits.hh>
#include <zlib/ZuTuple.hh>

#include <zlib/ZmPQueue.hh>

inline void out(bool ok, const char *s)
{
  std::cout << (ok ? "OK  " : "NOK ") << s << '\n' << std::flush;
  ZmAssert(ok);
}

#define CHECK(x) (out((x), #x))

using Msg_Data = ZuTuple<uint32_t, unsigned>;
struct Msg : public ZuObject, public Msg_Data {
  using Msg_Data::Msg_Data;
  using Msg_Data::operator =;
  Msg(const Msg_Data &v) : Msg_Data(v) { }
  Msg(Msg_Data &&v) : Msg_Data(ZuMv(v)) { }
  uint32_t key() const { return p<0>(); }
  unsigned length() const { return p<1>(); }
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
  std::cout << "set head=" << key << '\n';
  q.head(key);
  std::cout << "get head=" << q.head() << '\n';
}
void find(const PQueue &q, uint32_t key)
{
  ZmRef<QMsg> msg = q.find(key);
  std::cout << "find " << msg->Msg::key() << ", " << msg->length() << '\n';
}
void add(PQueue &q, uint32_t key, unsigned length)
{
  std::cout << "add " << key << ", " << length << '\n';
  q.add(new QMsg(ZuFwdTuple(key, length)));
}

int main()
{
  PQueue q(1);

  add(q, 0, 2);
  add(q, 2, 2);
  add(q, 4, 2);
  add(q, 6, 2);
  head(q, 3);
  find(q, 4);
  find(q, 7);
  std::cout << q << '\n';
}
