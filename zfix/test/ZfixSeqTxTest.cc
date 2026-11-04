//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuObject.hh>
#include <zlib/ZuTestUtil.hh>
#include <zlib/ZtArray.hh>
#include <zlib/ZfixSeq.hh>

using namespace ZuTestUtil;
using namespace Zfix;

struct Item {
  uint64_t	seqNo;
  unsigned	bytes;
  uint64_t	value = 0;

  uint64_t key() const { return seqNo; }
  uint64_t length() const { return 1; }
  unsigned wireBytes() const { return bytes; }
};

using Queue = ZmPQueue<Item, ZmPQueueHeapID<"Zfix.SeqTxTest",
  ZmPQueueNode<ZuObject, ZmPQueueOverlap<false, ZmPQueueOverwrite<false>>>>>;
using Values = ZtArray<uint64_t, ZtArrayHeapID<"Zfix.SeqTxTest.Values">>;
struct Task {
  unsigned	kind;
  uint64_t	ticket;
};

struct Tx : SeqTx<Tx, Queue> {
  using Tasks = ZtArray<Task, ZtArrayHeapID<"Zfix.SeqTxTest.Tasks">>;
  Tasks		tasks;
  Values	sent, replayed, values, gaps, archived, requests;
  uint64_t	failAt = 0, retrieveFrom = 1, retrieveUntil = 0;
  uint64_t	deferAt = 0, reloadTicket = 0, value = 17, ackOnArchive = 0;
  unsigned	task = 0, reloadBytes = 20;
  bool		refuseGap = false, holdLookup = false, holdArchive = false;
  bool		stopOnFailure = false, badHistory = false;

  using SeqTx::SeqTx;
  using SeqTx::admitted_;

  void postTx(unsigned kind, uint64_t ticket) { tasks.push(Task{kind, ticket}); }

  bool publishTx(Msg *msg, bool replay) {
    auto key = msg->data().seqNo;
    if (key == failAt) {
      if (stopOnFailure) hold_();
      return false;
    }
    if (replay) { replayed.push(key); values.push(msg->data().value); }
    else sent.push(key);
    return true;
  }

  bool gapTx(const Span &gap) {
    if (refuseGap) return false;
    gaps.push(gap.key()); gaps.push(gap.length()); return true;
  }

  SeqReload<ZmRef<Msg>> retrieveTx(uint64_t key, uint64_t, uint64_t ticket, bool replay) {
    requests.push(key); requests.push(replay);
    if (holdLookup) { holdLookup = false; hold_(); }
    if (key == deferAt) { reloadTicket = ticket; return {{}, true}; }
    if (key < retrieveFrom || key > retrieveUntil) return {};
    return {new Msg{Item{key + unsigned(badHistory), reloadBytes, value}}};
  }

  void archiveTx(uint64_t begin, uint64_t end) {
    archived.push(begin); archived.push(end);
    if (holdArchive) { holdArchive = false; hold_(); }
    if (ackOnArchive) {
      auto next = ackOnArchive; ackOnArchive = 0; acknowledge_(next);
    }
  }

  unsigned commit(uint64_t key, unsigned bytes = 20) {
    auto result = commit_(new Msg{Item{key, bytes}});
    if (result == SeqResult::Accepted && retrieveUntil < key) retrieveUntil = key;
    return result;
  }

  bool step() {
    if (task == tasks.length()) return false;
    auto t = tasks[task++]; run_(t.kind, t.ticket); return true;
  }

  void drain() { while (step()); }
};

static void newSend()
{
  ZuTestScope(newSend);
  Tx tx;
  ZuCheck(tx.commit(2) == SeqResult::Invalid && tx.commit(1) == SeqResult::Accepted);
  ZuCheck(tx.commit(2) == SeqResult::Accepted && tx.queuedBytes_() == 40);
  tx.resume_(); tx.drain();
  ZuCheck(tx.published_() == 3 && !tx.retained_() && !tx.queuedBytes_());
  ZuCheck(tx.sent.length() == 2 && !tx.requests.length() && !tx.archived.length());
  ZuCheck(!tx.acknowledge_(4) && tx.acknowledge_(3)); tx.drain();
  ZuCheck(tx.archived.length() == 2 && tx.archived[0] == 1 && tx.archived[1] == 3);
}

static void publicationFailure()
{
  ZuTestScope(publicationFailure);
  Tx tx;
  tx.failAt = 1;
  ZuCheck(tx.commit(1) == SeqResult::Accepted && tx.commit(2) == SeqResult::Accepted);
  tx.resume_(); tx.drain();
  ZuCheck(tx.failed_() && tx.published_() == 1 && tx.retained_() == 2 &&
    tx.queuedBytes_() == 40);
  tx.failAt = 0; tx.resume_(); tx.drain();
  ZuCheck(tx.published_() == 3 && !tx.retained_() && !tx.queuedBytes_());
  tx.failAt = 3; tx.stopOnFailure = true;
  ZuCheck(tx.commit(3) == SeqResult::Accepted); tx.drain();
  ZuCheck(!tx.running_() && tx.retained_() == 1 && tx.published_() == 3);
  tx.failAt = 0; tx.resume_(); tx.drain();
  ZuCheck(tx.published_() == 4 && !tx.retained_());
}

static void replay()
{
  ZuTestScopeRT(replay);
  Tx tx{{.replayBatch = 2}};
  for (uint64_t key = 1; key <= 5; ++key) ZuCheckRT(tx.commit(key) == SeqResult::Accepted);
  tx.resume_(); tx.drain();
  ZuCheckRT(tx.replay_(2) && tx.commit(6) == SeqResult::Accepted); tx.drain();
  ZuCheckRT(tx.replayed.length() == 4 && tx.replayed[0] == 2 && tx.replayed[3] == 5);
  ZuCheckRT(tx.requests.length() == 8 && tx.sent.length() == 6 && !tx.retained_());
  tx.value = 99;
  ZuCheckRT(tx.replay_(2, 2)); tx.drain();
  ZuCheckRT(tx.requests.length() == 10 && tx.values[4] == 99 && !tx.retained_());
  ZuCheckRT(!tx.replay_(0) && !tx.replay_(7) && !tx.replay_(4, 3));
}

static void boundedMissing()
{
  ZuTestScope(boundedMissing);
  Tx tx{{.replayBatch = 3}};
  ZuCheck(tx.restore_(1000001, 1000001, 1)); tx.resume_();
  ZuCheck(tx.replay_(1) && tx.step());
  ZuCheck(tx.requests.length() == 6 && tx.gaps.length() == 2 && tx.gaps[1] == 3);
  tx.hold_(); tx.drain();
  ZuCheck(tx.replaying_() && !tx.waiting_() && !tx.retained_());
  Tx unsent{{.replayBatch = 3}};
  ZuCheck(unsent.restore_(1000001, 1, 1)); unsent.resume_();
  ZuCheck(unsent.step() && unsent.published_() <= 4 && !unsent.queuedBytes_());
  unsent.hold_(); unsent.drain();
}

static void replacements()
{
  ZuTestScope(replacements);
  Tx tx{{.sequenceLimit = 2, .queuedOutputBytes = 100}};
  ZuCheck(tx.commit(1) == SeqResult::Accepted && tx.commit(2) == SeqResult::Accepted);
  ZuCheck(tx.abort_(1) && tx.queuedBytes_() == 20 && tx.next_() == 3);
  tx.reloadBytes = 70; tx.resume_(); tx.drain();
  ZuCheck(tx.sent.length() == 2 && tx.published_() == 3 && !tx.queuedBytes_());
  ZuCheck(tx.requests.length() == 2 && tx.requests[0] == 1 && !tx.requests[1]);
  Tx restored;
  ZuCheck(restored.restore_(4, 1, 1) && !restored.queuedBytes_());
  restored.retrieveUntil = 3; restored.reloadBytes = 77;
  restored.resume_(); restored.drain();
  ZuCheck(restored.sent.length() == 3 && !restored.queuedBytes_() && !restored.retained_());
  Tx blocked{{.queuedOutputBytes = 50}};
  ZuCheck(blocked.commit(1) == SeqResult::Accepted && blocked.abort_(1));
  blocked.reloadBytes = 51; blocked.resume_(); blocked.drain();
  ZuCheck(blocked.failed_() && blocked.published_() == 1 && !blocked.gaps.length());
}

static void abortUnpublished()
{
  ZuTestScope(abortUnpublished);
  Tx tx;
  ZuCheck(tx.commit(1) == SeqResult::Accepted && tx.commit(2) == SeqResult::Accepted);
  tx.retrieveUntil = 0;
  ZuCheck(tx.abort_(1) && !tx.abort_(1) && tx.next_() == 3 && tx.queuedBytes_() == 20);
  tx.resume_(); tx.drain();
  ZuCheck(tx.published_() == 3 && tx.sent.length() == 1 && tx.sent[0] == 2);
  ZuCheck(tx.gaps.length() == 2 && tx.gaps[0] == 1 && tx.gaps[1] == 1);
  tx.hold_(); ZuCheck(!tx.abort_(2) && !tx.retained_());
}

static void ordinaryRetry()
{
  ZuTestScope(ordinaryRetry);
  Tx tx;
  ZuCheck(tx.restore_(2, 1, 1));
  tx.retrieveUntil = 1; tx.failAt = 1;
  tx.resume_(); tx.drain();
  ZuCheck(tx.failed_() && tx.published_() == 1 && tx.retained_() == 1 &&
    tx.queuedBytes_() == 20 && tx.requests.length() == 2);
  // This result is now ordinary assigned work, retained until admission.
  tx.retrieveUntil = 0; tx.reloadBytes = 99; tx.failAt = 0;
  tx.resume_(); tx.drain();
  ZuCheck(tx.published_() == 2 && tx.sent.length() == 1 && tx.sent[0] == 1 &&
    tx.requests.length() == 2 && !tx.retained_() && !tx.queuedBytes_());
}

static void deferred()
{
  ZuTestScope(deferred);
  Tx tx;
  ZuCheck(tx.restore_(2, 2, 1)); tx.deferAt = 1; tx.resume_();
  ZuCheck(tx.replay_(1)); tx.drain();
  auto first = tx.reloadTicket;
  ZuCheck(tx.waiting_() && !tx.retained_() && !tx.replayed.length());
  tx.hold_();
  ZuCheck(tx.completeReload_(first, new Tx::Msg{Item{1, 20, 44}}));
  ZuCheck(!tx.waiting_() && tx.replaying_() && !tx.replayed.length());
  tx.resume_(); tx.drain(); auto second = tx.reloadTicket;
  ZuCheck(second != first && tx.waiting_());
  ZuCheck(!tx.completeReload_(first, new Tx::Msg{Item{1, 20}}) && tx.waiting_());
  ZuCheck(tx.completeReload_(second, new Tx::Msg{Item{1, 20, 99}})); tx.drain();
  ZuCheck(tx.replayed.length() == 1 && tx.values[0] == 99 && !tx.retained_());
  ZuCheck(!tx.completeReload_(second, {}) && !tx.replaying_());
}

static void pendingPrefix()
{
  ZuTestScope(pendingPrefix);
  Tx tx;
  ZuCheck(tx.restore_(3, 3, 1)); tx.deferAt = 2; tx.refuseGap = true;
  tx.resume_(); ZuCheck(tx.replay_(1)); tx.drain();
  auto ticket = tx.reloadTicket;
  ZuCheck(tx.waiting_() && tx.failed_() && !tx.gaps.length());
  tx.refuseGap = false; tx.resume_(); tx.drain(); // writable before completion
  ZuCheck(tx.waiting_());
  tx.deferAt = 0; tx.retrieveFrom = tx.retrieveUntil = 2;
  ZuCheck(tx.completeReload_(ticket, new Tx::Msg{Item{2, 20, 88}})); tx.drain();
  ZuCheck(!tx.waiting_() && !tx.replaying_() && tx.gaps.length() == 2);
  ZuCheck(tx.replayed.length() == 1 && tx.values[0] == tx.value && !tx.retained_());
}

static void pendingHold()
{
  ZuTestScope(pendingHold);
  Tx tx;
  ZuCheck(tx.restore_(2, 2, 1)); tx.deferAt = 1; tx.holdLookup = true;
  tx.resume_(); ZuCheck(tx.replay_(1)); tx.drain();
  ZuCheck(!tx.running_() && tx.waiting_());
  ZuCheck(tx.completeReload_(tx.reloadTicket, {}) && !tx.waiting_() && tx.replaying_());
  tx.deferAt = 0; tx.resume_(); tx.drain();
  ZuCheck(!tx.replaying_() && tx.gaps.length() == 2 && tx.requests.length() == 4);
}

static void replayRefusal()
{
  ZuTestScope(replayRefusal);
  Tx tx;
  ZuCheck(tx.commit(1) == SeqResult::Accepted); tx.resume_(); tx.drain();
  tx.failAt = 1; ZuCheck(tx.replay_(1)); tx.drain();
  ZuCheck(tx.failed_() && tx.requests.length() == 2 && !tx.retained_());
  tx.failAt = 0; tx.value = 42; tx.resume_(); tx.drain();
  ZuCheck(tx.requests.length() == 4 && tx.values[0] == 42 && !tx.replaying_());
}

static void archives()
{
  ZuTestScopeRT(archives);
  Tx tx{{.replayBatch = 2}};
  ZuCheckRT(tx.restore_(8, 8, 1)); tx.resume_();
  tx.holdArchive = true; tx.ackOnArchive = 8;
  ZuCheckRT(tx.acknowledge_(4)); tx.drain();
  ZuCheckRT(!tx.running_() && tx.archived.length() == 2 && tx.archived[0] == 1 &&
    tx.archived[1] == 3 && !tx.retained_());
  tx.resume_(); tx.drain();
  uint64_t begin = 1;
  for (unsigned i = 0, n = tx.archived.length(); i < n; i += 2) {
    ZuCheckRT(tx.archived[i] == begin && tx.archived[i + 1] - begin <= 2);
    begin = tx.archived[i + 1];
  }
  ZuCheckRT(begin == 8 && tx.acknowledged_() == 8 && !tx.requests.length());
  Tx restored;
  ZuCheckRT(restored.restore_(8, 8, 8)); restored.resume_(); restored.drain();
  ZuCheckRT(!restored.archived.length());

  Tx reentrant{{.replayBatch = 2}};
  ZuCheckRT(reentrant.restore_(8, 8, 1)); reentrant.resume_();
  reentrant.ackOnArchive = 8;
  ZuCheckRT(reentrant.acknowledge_(4)); reentrant.drain();
  ZuCheckRT(reentrant.archived.length() == 8 && reentrant.archived[0] == 1 &&
    reentrant.archived[1] == 3 && reentrant.archived[2] == 3 &&
    reentrant.archived[3] == 5 && reentrant.archived[4] == 5 &&
    reentrant.archived[5] == 7 && reentrant.archived[6] == 7 &&
    reentrant.archived[7] == 8);
}

static void warmArchive()
{
  ZuTestScope(warmArchive);
  Tx tx{{.replayBatch = 2}};
  ZuCheck(tx.restore_(8, 8, 1)); tx.resume_();
  ZuCheck(tx.acknowledge_(8) && tx.tasks.length() == 1 && !tx.archived.length());
  auto stale = tx.tasks[0];
  tx.hold_();
  tx.run_(stale.kind, stale.ticket);
  ZuCheck(!tx.archived.length() && tx.acknowledged_() == 8);
  tx.resume_();
  ZuCheck(tx.tasks.length() == 2 && tx.tasks[1].ticket != stale.ticket);
  tx.run_(stale.kind, stale.ticket);
  ZuCheck(!tx.archived.length());
  tx.drain();
  ZuCheck(tx.archived.length() == 8 && tx.archived[0] == 1 && tx.archived[1] == 3 &&
    tx.archived[2] == 3 && tx.archived[3] == 5 && tx.archived[4] == 5 &&
    tx.archived[5] == 7 && tx.archived[6] == 7 && tx.archived[7] == 8);
  ZuCheck(!tx.requests.length() && !tx.retained_() && tx.acknowledged_() == 8);
}

static void futureReplay()
{
  ZuTestScope(futureReplay);
  Tx tx;
  ZuCheck(tx.restore_(4, 1, 1)); tx.admitted_(4);
  tx.deferAt = 1; tx.resume_(); tx.drain();
  ZuCheck(tx.waiting_() && tx.replay_(1));
  auto range = tx.replayRange_();
  ZuCheck(range.key() == 1 && range.length() == 3);
  tx.deferAt = 0; tx.retrieveUntil = 2;
  ZuCheck(tx.completeReload_(tx.reloadTicket, new Tx::Msg{Item{1, 20}})); tx.drain();
  ZuCheck(tx.published_() == 4 && !tx.replaying_() && tx.replayed.length() == 2);
  ZuCheck(tx.sent.length() == 2 && !tx.retained_());
  Tx restored;
  ZuCheck(restored.restore_(4, 1, 1, {1, 3}));
  ZuCheck(!restored.restore_(4, 1, 1, {1, 4}));
}

static void invalidReload()
{
  ZuTestScope(invalidReload);
  Tx tx;
  ZuCheck(tx.restore_(2, 2, 1)); tx.deferAt = 1;
  tx.resume_(); ZuCheck(tx.replay_(1)); tx.drain();
  ZuCheck(tx.completeReload_(tx.reloadTicket, new Tx::Msg{Item{2, 20}}));
  ZuCheck(tx.failed_() && !tx.waiting_() && tx.replaying_() && !tx.retained_());
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(newSend);
  ZuTestCall(publicationFailure);
  ZuTestCall(replay);
  ZuTestCall(boundedMissing);
  ZuTestCall(replacements);
  ZuTestCall(abortUnpublished);
  ZuTestCall(ordinaryRetry);
  ZuTestCall(deferred);
  ZuTestCall(pendingPrefix);
  ZuTestCall(pendingHold);
  ZuTestCall(replayRefusal);
  ZuTestCall(archives);
  ZuTestCall(warmArchive);
  ZuTestCall(futureReplay);
  ZuTestCall(invalidReload);
  return 0;
}
