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

ZuDerive(NoOverlapPQueue,
  (ZmPQueue<Msg,
    ZmPQueueNode<Msg,
      ZmPQueueStats<false,
	ZmPQueueOverlap<false,
	  ZmPQueueBits<3,
	    ZmPQueueLevels<3>>>>>>));

using NoOverlapQMsg = NoOverlapPQueue::Node;

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

void testItemHelpers()
{
  ZuTestScope(testItemHelpers);

  const Msg_Data msgData{1, 5};
  Msg msgCopy{msgData};
  Msg_Data msgMoveData{2, 6};
  Msg msgMove{ZuMv(msgMoveData)};
  msgCopy.write(msgMove);
  ZuCheck(msgCopy.elems() == 1);
  ZuCheck(msgCopy.key() == 1 && msgCopy.length() == 5);
  ZuCheck(msgMove.key() == 2 && msgMove.length() == 6);

  const PlainMsg_Data plainData{10, 5};
  PlainMsg plainCopy{plainData};
  PlainMsg_Data plainMoveData{20, 6};
  PlainMsg plainMove{ZuMv(plainMoveData)};
  plainCopy.write(plainMove);
  ZuCheck(plainCopy.clipHead(2) == 3);
  ZuCheck(plainCopy.key() == 12 && plainCopy.length() == 3);
  ZuCheck(plainCopy.clipTail(1) == 2);
  ZuCheck(plainCopy.key() == 12 && plainCopy.length() == 2);
  ZuCheck(plainMove.key() == 20 && plainMove.length() == 6);

  const WriteMsg_Data writeData{30, 5, 1};
  WriteMsg writeCopy{writeData};
  WriteMsg_Data writeMoveData{40, 6, 2};
  WriteMsg writeMove{ZuMv(writeMoveData)};
  writeCopy.write(writeMove);
  ZuCheck(writeCopy.value() == 2);
  ZuCheck(writeCopy.clipHead(1) == 4);
  ZuCheck(writeCopy.key() == 31 && writeCopy.length() == 4);
  ZuCheck(writeCopy.clipTail(2) == 2);
  ZuCheck(writeCopy.key() == 31 && writeCopy.length() == 2);
  ZuCheck(writeMove.key() == 40 && writeMove.length() == 6 &&
    writeMove.value() == 2);
}

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
  auto result = q.rotate(ZmRef<QMsg>(new QMsg(ZuFwdTuple(seqNo, length))));
  ZmRef<QMsg> msg = ZuMv(result.template p<1>());
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
    auto i = q.rciter();
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
      keys[0] == 5 && lengths[0] == 1 &&
      keys[1] == 2 && lengths[1] == 2 &&
      keys[2] == 0 && lengths[2] == 1);
  }

  {
    auto i = q.rciter(4);
    auto node = i();
    ZuCheck(node && node->data().key() == 2 &&
      node->data().length() == 2);
    node = i();
    ZuCheck(node && node->data().key() == 0);
    ZuCheck(!i());
  }

  {
    auto i = q.rciter(6);
    auto node = i();
    ZuCheck(node && node->data().key() == 5);
    node = i();
    ZuCheck(node && node->data().key() == 2);
    node = i();
    ZuCheck(node && node->data().key() == 0);
    ZuCheck(!i());
  }

  {
    auto i = q.rciter(0);
    auto node = i();
    ZuCheck(node && node->data().key() == 0);
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

  {
    auto i = q.riter();
    auto node = i();
    ZuCheck(node && node->data().key() == 5);
    auto removed = i.del();
    ZuCheck(removed && removed->data().key() == 5 &&
      removed->data().length() == 1);
    node = i();
    ZuCheck(node && node->data().key() == 0);
    ZuCheck(!i());
  }

  ZuCheck(q.count_() == 1 && q.length_() == 1 &&
    !q.find(2) && !q.find(5));
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

void testSubtract()
{
  ZuTestScope(testSubtract);

  {
    PQueue q(0);
    q.add(new QMsg(ZuFwdTuple(2, 4)));
    ZuCheck(q.clear(2, 4) && !q.count_() && !q.length_() && q.verify());
  }

  {
    PQueue q(0);
    q.add(new QMsg(ZuFwdTuple(2, 6)));
    ZuCheck(q.clear(2, 2));
    ZmRef<QMsg> msg = q.find(4);
    ZuCheck(msg && msg->data().key() == 4 && msg->data().length() == 4 &&
      q.count_() == 1 && q.length_() == 4 && q.verify());
  }

  {
    PQueue q(0);
    q.add(new QMsg(ZuFwdTuple(2, 6)));
    ZuCheck(q.clear(6, 2));
    ZmRef<QMsg> msg = q.find(2);
    ZuCheck(msg && msg->data().key() == 2 && msg->data().length() == 4 &&
      q.count_() == 1 && q.length_() == 4 && q.verify());
  }

  {
    PlainPQueue q(0);
    q.add(new PlainQMsg(ZuFwdTuple(2, 8)));
    ZuCheck(q.clear(5, 2));
    auto head = q.find(2);
    auto tail = q.find(7);
    ZuCheck(head && tail &&
      head->data().key() == 2 && head->data().length() == 3 &&
      tail->data().key() == 7 && tail->data().length() == 3 &&
      q.count_() == 2 && q.length_() == 6 && q.verify());
  }

  {
    PQueue q(0);
    q.add(new QMsg(ZuFwdTuple(2, 3)));
    q.add(new QMsg(ZuFwdTuple(6, 3)));
    q.add(new QMsg(ZuFwdTuple(10, 3)));
    ZuCheck(q.clear(4, 7));
    ZmRef<QMsg> head = q.find(2);
    ZmRef<QMsg> tail = q.find(11);
    ZuCheck(head && tail &&
      head->data().key() == 2 && head->data().length() == 2 &&
      tail->data().key() == 11 && tail->data().length() == 2 &&
      !q.find(6) &&
      q.count_() == 2 && q.length_() == 4 && q.verify());
  }

  {
    PQueue q(0);
    q.add(new QMsg(ZuFwdTuple(2, 3)));
    ZuCheck(!q.clear(8, 2) &&
      q.count_() == 1 && q.length_() == 3 && q.verify());
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

void testAddResults()
{
  ZuTestScope(testAddResults);

  {
    PQueue q(0);
    ZuCheck(q.add(new QMsg(ZuFwdTuple(2, 1))) == ZmPQResult::Inserted);
    ZuCheck(q.add(new QMsg(ZuFwdTuple(~uint32_t{0}, 2))) ==
      ZmPQResult::Invalid);
  }

  {
    PQueue q(10);
    ZuCheck(q.add(new QMsg(ZuFwdTuple(9, 2))) == ZmPQResult::Clipped);
    ZuCheck(q.head() == 10 && q.tail() == 11 && q.verify());
  }

  {
    PQueue q(0);
    auto result = q.rotate(new QMsg(ZuFwdTuple(0, 1)));
    ZuCheck(result.template p<0>() == ZmPQResult::Inserted);
    ZmRef<QMsg> msg = ZuMv(result.template p<1>());
    ZuCheck(msg && msg->Msg::key() == 0 && q.head() == 1);

    auto queued = q.rotate(new QMsg(ZuFwdTuple(3, 1)));
    ZuCheck(queued.template p<0>() == ZmPQResult::Inserted);
    ZuCheck(!queued.template p<1>() && q.gap().equals(ZuFwdTuple(1, 2)));
  }

  {
    PQueue q(0);
    ZuCheck(q.add(new QMsg(ZuFwdTuple(2, 1))) == ZmPQResult::Inserted);
    auto result = q.rotate(new QMsg(ZuFwdTuple(0, 3)));
    ZuCheck(result.template p<0>() == ZmPQResult::Clipped);
    ZmRef<QMsg> msg = ZuMv(result.template p<1>());
    ZuCheck(msg && msg->Msg::key() == 0 && msg->length() == 3 &&
      q.head() == 3 && q.verify());
  }

  {
    WritePQueue q(0);
    ZuCheck(q.add(new WriteQMsg(ZuFwdTuple(0, 5, 1))) ==
      ZmPQResult::Inserted);
    ZuCheck(q.add(new WriteQMsg(ZuFwdTuple(1, 1, 2))) ==
      ZmPQResult::Overwrote);
  }

  {
    NoWritePQueue q(0);
    ZuCheck(q.add(new NoWriteQMsg(ZuFwdTuple(0, 5, 1))) ==
      ZmPQResult::Inserted);
    ZuCheck(q.add(new NoWriteQMsg(ZuFwdTuple(1, 1, 2))) ==
      ZmPQResult::Duplicate);
  }

  {
    PQueue q(10);
    ZuCheck(q.unshift(new QMsg(ZuFwdTuple(8, 2))) ==
      ZmPQResult::Inserted);
    ZuCheck(q.unshift(new QMsg(ZuFwdTuple(7, 2))) ==
      ZmPQResult::Clipped);
    ZuCheck(q.unshift(new QMsg(ZuFwdTuple(7, 1))) ==
      ZmPQResult::Duplicate);
    ZuCheck(q.head() == 7 && q.verify());

    PQueue q2(~uint32_t{0});
    ZuCheck(q2.unshift(new QMsg(ZuFwdTuple(~uint32_t{0} - 1, 3))) ==
      ZmPQResult::Invalid);
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

void testNoStatsNoOverlap()
{
  ZuTestScope(testNoStatsNoOverlap);

  NoOverlapPQueue q(1);

  q.add(new NoOverlapQMsg(ZuFwdTuple(0, 2)));
  q.add(new NoOverlapQMsg(ZuFwdTuple(2, 2)));
  q.add(new NoOverlapQMsg(ZuFwdTuple(4, 2)));
  q.add(new NoOverlapQMsg(ZuFwdTuple(6, 2)));

  q.head(3);
  ZuCheck(q.head() == 2);

  {
    ZmRef<NoOverlapQMsg> msg = q.find(4);
    ZuCheck(msg);
    ZuCheck(msg->Msg::key() == 4 && msg->length() == 2);
  }

  {
    ZmRef<NoOverlapQMsg> msg = q.find(7);
    ZuCheck(msg);
    ZuCheck(msg->Msg::key() == 6 && msg->length() == 2);
  }

  ZuCheck(q.verify());
  log(q);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testItemHelpers);
  ZuTestCall(testIterators);
  ZuTestCall(testSpansAndGaps);
  ZuTestCall(testSubtract);
  ZuTestCall(testOverwritePolicy);
  ZuTestCall(testAddResults);
  ZuTestCall(testNodeMvRef);
  ZuTestCall(testNoStatsNoOverlap);
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
