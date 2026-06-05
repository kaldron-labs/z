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

using Msg_Data = ZuTuple<uint32_t, uint64_t>;
struct Msg : public ZuObject, public Msg_Data {
  using Msg_Data::Msg_Data;
  using Msg_Data::operator =;
  Msg(const Msg_Data &v) : Msg_Data(v) { }
  Msg(Msg_Data &&v) : Msg_Data(ZuMv(v)) { }
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
  void write(const I &) { }
  unsigned elems() const { return 1; }
};

ZuDerive(PQueue,
  (ZmPQueue<Msg,
    ZmPQueueNode<Msg,
      ZmPQueueBits<1,
	ZmPQueueLevels<4>>>>));

using QMsg = PQueue::Node;

using PlainMsg_Data = ZuTuple<uint32_t, uint64_t>;
struct PlainMsg : public PlainMsg_Data {
  using PlainMsg_Data::PlainMsg_Data;
  using PlainMsg_Data::operator =;
  PlainMsg(const PlainMsg_Data &v) : PlainMsg_Data(v) { }
  PlainMsg(PlainMsg_Data &&v) : PlainMsg_Data(ZuMv(v)) { }
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
  void write(const I &) { }
};

ZuDerive(PlainPQueue,
  (ZmPQueue<PlainMsg,
    ZmPQueueBits<1,
      ZmPQueueLevels<3>>>));

using PlainQMsg = PlainPQueue::Node;

using WriteMsg_Data = ZuTuple<uint32_t, uint64_t, unsigned>;
struct WriteMsg : public ZuObject, public WriteMsg_Data {
  using WriteMsg_Data::WriteMsg_Data;
  using WriteMsg_Data::operator =;
  WriteMsg(const WriteMsg_Data &v) : WriteMsg_Data(v) { }
  WriteMsg(WriteMsg_Data &&v) : WriteMsg_Data(ZuMv(v)) { }
  uint32_t key() const { return p<0>(); }
  uint64_t length() const { return p<1>(); }
  unsigned value() const { return p<2>(); }
  uint64_t clipHead(uint64_t length) {
    p<0>() += length;
    return p<1>() -= length;
  }
  uint64_t clipTail(uint64_t length) {
    return p<1>() -= length;
  }
  template <typename I>
  void write(const I &i) { p<2>() = i.value(); }
};

ZuDerive(WritePQueue,
  (ZmPQueue<WriteMsg,
    ZmPQueueNode<WriteMsg,
      ZmPQueueBits<1,
	ZmPQueueLevels<3>>>>));

ZuDerive(NoWritePQueue,
  (ZmPQueue<WriteMsg,
    ZmPQueueNode<WriteMsg,
      ZmPQueueOverwrite<false,
	ZmPQueueBits<1,
	  ZmPQueueLevels<3>>>>>));

using WriteQMsg = WritePQueue::Node;
using NoWriteQMsg = NoWritePQueue::Node;

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
void add(PQueue &q, uint32_t seqNo, uint64_t length)
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

void testIterators()
{
  ZuTestScope(testIterators);

  PQueue q(0);
  q.add(new QMsg(ZuFwdTuple(0, 1)));
  q.add(new QMsg(ZuFwdTuple(2, 2)));
  q.add(new QMsg(ZuFwdTuple(5, 1)));
  ZuCheck(q.verify());
  ZuCheck(q.has(0) && !q.has(1) && q.has(3) &&
    !q.has(4) && q.has(5) && !q.has(6));

  {
    auto i = q.citer();
    uint32_t keys[3]{};
    uint64_t lengths[3]{};
    unsigned n = 0;
    while (auto node = i()) {
      ZuCheck(n < 3);
      keys[n] = node->data().key();
      lengths[n] = node->data().length();
      ++n;
    }
    ZuCheck(n == 3 &&
      keys[0] == 0 && lengths[0] == 1 &&
      keys[1] == 2 && lengths[1] == 2 &&
      keys[2] == 5 && lengths[2] == 1);
  }

  {
    auto i = q.citer(3);
    auto node = i();
    ZuCheck(node && node->data().key() == 2 &&
      node->data().length() == 2);
    node = i();
    ZuCheck(node && node->data().key() == 5);
    ZuCheck(!i());
  }

  {
    auto i = q.citer(4);
    auto node = i();
    ZuCheck(node && node->data().key() == 5);
    ZuCheck(!i());
  }

  {
    auto i = q.citer(6);
    ZuCheck(!i());
  }

  {
    auto i = q.iter(2);
    auto node = i();
    ZuCheck(node && node->data().key() == 2);
    auto removed = i.del();
    ZuCheck(removed && removed->data().key() == 2 &&
      removed->data().length() == 2);
    node = i();
    ZuCheck(node && node->data().key() == 5);
    ZuCheck(!i());
  }

  ZuCheck(q.count_() == 2 && q.length_() == 2 && !q.find(2));
  ZuCheck(q.verify());
}

void testSpansAndGaps()
{
  ZuTestScope(testSpansAndGaps);

  PQueue q(0);
  q.add(new QMsg(ZuFwdTuple(0, 1)));
  q.add(new QMsg(ZuFwdTuple(2, 2)));
  q.add(new QMsg(ZuFwdTuple(5, 1)));
  q.add(new QMsg(ZuFwdTuple(6, 2)));
  ZuCheck(q.verify());

  {
    uint32_t keys[3]{};
    uint64_t lengths[3]{};
    unsigned n = 0;
    bool boundsOk = true;
    bool ok = q.spans([&](const PQueue::Span &span) {
      if (n >= 3) { boundsOk = false; return false; }
      keys[n] = span.key();
      lengths[n] = span.length();
      ++n;
      return true;
    });
    ZuCheck(ok && boundsOk);
    ZuCheck(n == 3 &&
      keys[0] == 0 && lengths[0] == 1 &&
      keys[1] == 2 && lengths[1] == 2 &&
      keys[2] == 5 && lengths[2] == 3);
  }

  {
    uint32_t keys[3]{};
    uint64_t lengths[3]{};
    unsigned n = 0;
    bool boundsOk = true;
    bool ok = q.rspans([&](const PQueue::Span &span) {
      if (n >= 3) { boundsOk = false; return false; }
      keys[n] = span.key();
      lengths[n] = span.length();
      ++n;
      return true;
    });
    ZuCheck(ok && boundsOk);
    ZuCheck(n == 3 &&
      keys[0] == 5 && lengths[0] == 3 &&
      keys[1] == 2 && lengths[1] == 2 &&
      keys[2] == 0 && lengths[2] == 1);
  }

  {
    uint32_t keys[2]{};
    uint64_t lengths[2]{};
    unsigned n = 0;
    bool boundsOk = true;
    bool ok = q.spans(3, 4, [&](const PQueue::Span &span) {
      if (n >= 2) { boundsOk = false; return false; }
      keys[n] = span.key();
      lengths[n] = span.length();
      ++n;
      return true;
    });
    ZuCheck(ok && boundsOk);
    ZuCheck(n == 2 &&
      keys[0] == 3 && lengths[0] == 1 &&
      keys[1] == 5 && lengths[1] == 2);
  }

  {
    uint32_t keys[2]{};
    uint64_t lengths[2]{};
    unsigned n = 0;
    bool boundsOk = true;
    bool ok = q.rspans(3, 4, [&](const PQueue::Span &span) {
      if (n >= 2) { boundsOk = false; return false; }
      keys[n] = span.key();
      lengths[n] = span.length();
      ++n;
      return true;
    });
    ZuCheck(ok && boundsOk);
    ZuCheck(n == 2 &&
      keys[0] == 5 && lengths[0] == 2 &&
      keys[1] == 3 && lengths[1] == 1);
  }

  {
    uint32_t keys[2]{};
    uint64_t lengths[2]{};
    unsigned n = 0;
    bool boundsOk = true;
    bool ok = q.gaps([&](const PQueue::Span &gap) {
      if (n >= 2) { boundsOk = false; return false; }
      keys[n] = gap.key();
      lengths[n] = gap.length();
      ++n;
      return true;
    });
    ZuCheck(ok && boundsOk);
    ZuCheck(n == 2 &&
      keys[0] == 1 && lengths[0] == 1 &&
      keys[1] == 4 && lengths[1] == 1);
  }

  {
    uint32_t keys[1]{};
    uint64_t lengths[1]{};
    unsigned n = 0;
    bool boundsOk = true;
    bool ok = q.gaps(3, 4, [&](const PQueue::Span &gap) {
      if (n >= 1) { boundsOk = false; return false; }
      keys[n] = gap.key();
      lengths[n] = gap.length();
      ++n;
      return true;
    });
    ZuCheck(ok && boundsOk);
    ZuCheck(n == 1 && keys[0] == 4 && lengths[0] == 1);
  }

  {
    uint32_t keys[1]{};
    uint64_t lengths[1]{};
    unsigned n = 0;
    PQueue q2(10);
    q2.add(new QMsg(ZuFwdTuple(12, 2)));
    bool boundsOk = true;
    bool ok = q2.gaps(8, 6, [&](const PQueue::Span &gap) {
      if (n >= 1) { boundsOk = false; return false; }
      keys[n] = gap.key();
      lengths[n] = gap.length();
      ++n;
      return true;
    });
    ZuCheck(ok && boundsOk);
    ZuCheck(n == 1 && keys[0] == 10 && lengths[0] == 2);
  }

  {
    unsigned n = 0;
    ZuCheck(!q.spans([&](const PQueue::Span &) {
      ++n;
      return n < 2;
    }) && n == 2);
  }
}

void testOverwritePolicy()
{
  ZuTestScope(testOverwritePolicy);

  {
    WritePQueue q(0);
    q.add(new WriteQMsg(ZuFwdTuple(0, 5, 1)));
    q.add(new WriteQMsg(ZuFwdTuple(1, 1, 2)));
    ZmRef<WriteQMsg> msg = q.find(0);
    ZuCheck(msg && msg->data().value() == 2);
    ZuCheck(q.count_() == 1 && q.length_() == 5 && q.verify());
  }

  {
    NoWritePQueue q(0);
    q.add(new NoWriteQMsg(ZuFwdTuple(0, 5, 1)));
    q.add(new NoWriteQMsg(ZuFwdTuple(1, 1, 2)));
    ZmRef<NoWriteQMsg> msg = q.find(0);
    ZuCheck(msg && msg->data().value() == 1);
    ZuCheck(q.count_() == 1 && q.length_() == 5 && q.verify());
  }
}

void testNodeMvRef()
{
  ZuTestScope(testNodeMvRef);

  static_assert(
    ZuIsSame<PlainPQueue::NodeMvRef, ZuPtr<PlainPQueue::Node>>{});
  static_assert(
    !ZuIsSame<PlainPQueue::NodeRef, PlainPQueue::NodeMvRef>{});

  PlainPQueue q(0);
  q.add(new PlainQMsg(ZuFwdTuple(0, 1)));
  q.add(new PlainQMsg(ZuFwdTuple(2, 1)));
  ZuCheck(q.verify());

  PlainPQueue::NodeMvRef first = q.dequeue();
  ZuCheck(first && first->data().key() == 0 &&
    first->data().length() == 1);

  PlainPQueue::NodeMvRef aborted = q.abort(2);
  ZuCheck(aborted && aborted->data().key() == 2 &&
    aborted->data().length() == 1 && !q.count_());

  q.add(new PlainQMsg(ZuFwdTuple(5, 1)));
  PlainPQueue::NodeMvRef shifted = q.shift();
  ZuCheck(shifted && shifted->data().key() == 5 &&
    shifted->data().length() == 1 && q.head() == 6);

  q.add(new PlainQMsg(ZuFwdTuple(7, 1)));
  auto i = q.iter();
  ZuCheck(i());
  PlainPQueue::NodeMvRef removed = i.del();
  ZuCheck(removed && removed->data().key() == 7 && !q.count_());
  ZuCheck(q.verify());
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testIterators);
  ZuTestCall(testSpansAndGaps);
  ZuTestCall(testOverwritePolicy);
  ZuTestCall(testNodeMvRef);
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
