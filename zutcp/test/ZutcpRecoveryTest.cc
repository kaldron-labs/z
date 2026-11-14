//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/Zutcp.hh>

using namespace ZuTestUtil;

static Zutcp::SentFrameRef dataRef(uint64_t offset, uint32_t length)
{
  Zutcp::TxRange range;
  range.dataOffset = offset;
  range.length = length;
  return Zutcp::SentFrameRef::data(range, false);
}


#define ZUTCP_CHECK_RT(x, ...) ZuCheckRT(x, log_(__VA_ARGS__))

static Zutcp::TxPkt txPkt(
  uint64_t pn, unsigned bytes = 100, bool ackEliciting = true)
{
  Zutcp::TxPkt p;
  p.pn = pn;
  p.sentTime = Zutcp::timeUS(pn * 100);
  p.bytes = bytes;
  p.ackEliciting = ackEliciting;
  p.inFlight = bytes;
  p.addFrame(dataRef(pn * 10, pn + 1));
  return p;
}

static Zutcp::TxPkt txRangePkt(
  uint64_t pn, uint64_t offset, uint64_t length,
  unsigned bytes = 100)
{
  Zutcp::TxPkt p = txPkt(pn, bytes);
  p.frames.clear();
  p.addFrame(dataRef(offset, length));
  return p;
}

static Zutcp::TxPkt txDataPkt(
  uint64_t pn, uint64_t offset, uint32_t length,
  bool fin = false, unsigned bytes = 100)
{
  Zutcp::TxPkt p = txPkt(pn, bytes);
  p.frames.clear();
  Zutcp::TxRange range;
  range.offset = uint32_t(offset);
  range.length = length;
  range.dataOffset = offset;
  p.addFrame(Zutcp::SentFrameRef::data(range, fin));
  return p;
}

void testFrameIdentity()
{
  ZuTestScope(testFrameIdentity);
  Zutcp::TxRange range;
  range.offset = 7;
  range.dataOffset = 13;
  range.length = 5;
  auto data = Zutcp::SentFrameRef::data(range, true);
  Zutcp::SentFrameKey original{data};
  data.range.offset = 21; // source-buffer coordinates do not identify wire bytes
  Zutcp::SentFrameKey sameBytes{data};
  ZuCHECK(original == sameBytes && !original.cmp(sameBytes) &&
    original.hash() == sameBytes.hash(),
    "data identity must depend only on byte offset, length and FIN");
  data.fin = false;
  ZuCHECK(original != Zutcp::SentFrameKey{data}, "FIN is part of data identity");
  data.fin = true;
  ++data.offset;
  ZuCHECK(original != Zutcp::SentFrameKey{data}, "different byte offsets are distinct");
  --data.offset;
  ++data.length;
  ZuCHECK(original != Zutcp::SentFrameKey{data}, "different byte extents are distinct");


}

void retainedLostByteHistory()
{
  ZuTestScope(retainedLostByteHistory);
  Zutcp::PktTxSpace tx;
  bool setup = true;
  for (unsigned id = 0; id <= Zutcp::PktTxSpace::RetainedLostMax; ++id)
    setup &= tx.add(txDataPkt(id, uint64_t(id) * 10, 10)) && tx.lose(id);
  ZuCHECK(setup && tx.retainedLost() == Zutcp::PktTxSpace::RetainedLostMax &&
      tx.count() == Zutcp::PktTxSpace::RetainedLostMax && !tx.bytesInFlight(),
    "retained lost byte records stay bounded independently of local transmission identity");
  Zutcp::PktAckBatch evicted;
  Zutcp::PktTxUpdate update;
#ifdef ZDEBUG
  tx.resetByteAckVisits();
#endif
  ZuCHECK(tx.ackBytesBatch(10, evicted, 8, &update) && !update.ackdFrames.length(),
    "evicted lost byte evidence cannot be retired a second time");
#ifdef ZDEBUG
  ZuCHECK(!tx.byteAckVisits() && tx.verifyOutstanding(),
    "eviction removes its byte-index hook without scanning future retained records");
#endif
  tx.clear();
}

void outstandingLifecycleIndex()
{
  ZuTestScope(outstandingLifecycleIndex);

#ifdef ZDEBUG
  Zutcp::PktTxSpace tx;
  uint32_t random = 0x9e3779b9U;
  for (uint64_t pn = 1; pn <= 512; ++pn) {
    random = random * 1664525U + 1013904223U;
    bool stream = random & 1;
    Zutcp::TxPkt packet = stream ?
      txDataPkt(pn, (random >> 3) & 255, 32,
	!(pn & 31)) :
      txRangePkt(pn, (random >> 3) & 255, 32);
    ZuCHECK(tx.add(packet), "outstanding-index packet add failed");
    switch ((random >> 16) & 3) {
      case 0: {
	Zutcp::PktAckBatch ack;
	while (!tx.ackBytesBatch(packet.frame(0).offset + packet.frame(0).length,
	  ack, 8)) { }
      } break;
      case 1: (void)tx.lose(pn); break;
      case 2:
	if (!(pn & 15)) (void)tx.discard(pn);
	break;
      default: break;
    }
    ZuCHECK(tx.verifyOutstanding(),
      "outstanding index diverged from packet history");
  }
  tx.clear();
  ZuCHECK(tx.verifyOutstanding(),
    "byte-history clear retained outstanding index entries");
#endif
}

void retxSkipsUnackdData()
{
  ZuTestScope(retxSkipsUnackdData);

  Zutcp::PktTxSpace tx;
  ZuCHECK(tx.add(txDataPkt(1, 0, 100)) &&
      tx.add(txDataPkt(2, 0, 50)) &&
      tx.add(txPkt(4)),
    "overlapping stream sent-packet setup failed");
  ZuCHECK(tx.lose(1) && tx.retransmitPending() == 1,
    "overlapping original loss setup failed");
  Zutcp::SentFrameRef ref;
  ZuCHECK(tx.nextRetransmit(ref) &&
      ref.kind == Zutcp::SentFrameKind::Data &&
      ref.offset == 50 &&
      ref.length == 50 &&
      ref.range.dataOffset == 50 &&
      ref.range.offset == 50 &&
      ref.range.length == 50 &&
      !ref.fin &&
      !tx.nextRetransmit(ref),
    "stream retransmit did not skip outstanding prefix");
}

void testLosslessFrameUpdates()
{
  ZuTestScope(testLosslessFrameUpdates);

  Zutcp::PktTxUpdate lost;
  bool lostSetup = true;
  for (unsigned i = 0; i < 65; ++i) {
    Zutcp::SentPkt packet;
    packet.pn = i;
    packet.bytes = 1;
    lostSetup &= packet.addFrame(
	dataRef(i, 1));
    lost.lost(packet);
  }
  ZuCHECK(lostSetup && lost.lostFrames.length() == 65 &&
      lost.lostFrames[64].ref.offset == 64,
    "lost updates stopped at built-in capacity");

  Zutcp::PktTxUpdate ackd;
  bool ackdSetup = true;
  for (unsigned packetNo = 0; packetNo < 256; ++packetNo) {
    Zutcp::SentPkt packet;
    packet.pn = packetNo;
    packet.bytes = 8;
    for (unsigned frameNo = 0; frameNo < Zutcp::SentPkt::MaxFrames; ++frameNo)
      ackdSetup &= packet.addFrame(
	  dataRef(packetNo * 8 + frameNo, 1));
    ackd.ackd(packet, packet.inFlight);
  }
  ZuCHECK(ackdSetup &&
      ackd.ackdFrames.length() == 256 * Zutcp::SentPkt::MaxFrames &&
      ackd.ackdFrames[2047].ref.offset == 2047,
    "byte update batches must not truncate beyond built-in storage");
}

void cumulativeByteHistory()
{
  ZuTestScope(cumulativeByteHistory);
  Zutcp::PktTxSpace tx;
  auto packet = txRangePkt(17, 0, 100, 140);
  packet.frames[0].ref.range.offset = 7;
  ZuCHECK(tx.add(packet), "byte ACK history setup");
  Zutcp::PktAckBatch first;
  Zutcp::PktTxUpdate update;
  ZuCHECK(tx.ackBytesBatch(40, first, 32, &update) && tx.count() == 1 &&
      tx.bytesInFlight() == 140 && !update.ackdBytes && update.ackdFrames.length() == 1,
    "partial byte ACK retains the covering packet and its flight accounting");
  const auto &part = update.ackdFrames[0].ref;
  ZuCHECK(part.offset == 0 && part.length == 40 && part.range.offset == 7 &&
      part.range.dataOffset == 0 && part.range.length == 40 && !part.fin,
    "ACK slice keeps payload coordinates independent of byte sequence coordinates");
  Zutcp::PktAckBatch duplicate, old;
  Zutcp::PktTxUpdate repeated;
  ZuCHECK(tx.ackBytesBatch(40, duplicate, 32, &repeated) &&
      tx.ackBytesBatch(10, old, 32, &repeated) && !repeated.ackdFrames.length(),
    "duplicate and older cumulative ACKs do not report byte progress again");
  Zutcp::PktAckBatch middle;
  Zutcp::PktTxUpdate progress;
  ZuCHECK(tx.ackBytesBatch(75, middle, 32, &progress) &&
      progress.ackdFrames.length() == 1 && progress.ackdFrames[0].ref.offset == 40 &&
      progress.ackdFrames[0].ref.length == 35 &&
      progress.ackdFrames[0].ref.range.offset == 47,
    "later partial ACK reports only the newly acknowledged suffix");
  ZuCHECK(tx.lose(17), "partially ACKed packet can still be lost");
  Zutcp::SentFrameRef retry;
  ZuCHECK(tx.nextRetransmit(retry) && retry.kind == Zutcp::SentFrameKind::Data &&
      retry.offset == 0 && retry.length == 100 && retry.range.offset == 7,
    "partial ACK does not trim the retained covering packet coordinates");
  ZuCHECK(tx.add(txRangePkt(99, 0, 100, 140)), "same bytes have a later send record");
  Zutcp::PktAckBatch complete;
  Zutcp::PktTxUpdate finished;
  ZuCHECK(tx.ackBytesBatch(100, complete, 32, &finished) && !tx.count() &&
      !tx.bytesInFlight() && finished.ackdBytes == 140 &&
      finished.ackdFrames.length() == 2 &&
      finished.ackdFrames[0].ref.offset == 75 && finished.ackdFrames[0].ref.length == 25 &&
      finished.ackdFrames[1].ref.offset == 0 && finished.ackdFrames[1].ref.length == 100,
    "one byte ACK retires original and repeated send records without any peer packet identity");
#ifdef ZDEBUG
  ZuCHECK(tx.verifyOutstanding(), "byte retirement preserves the outstanding-frame index");
#endif
}

void byteHistoryBatchAndFin()
{
  ZuTestScope(byteHistoryBatchAndFin);
  Zutcp::PktTxSpace tx;
  ZuCHECK(tx.add(txDataPkt(1, 50, 50)) &&
      tx.add(txDataPkt(9, 0, 50)), "packet IDs need not follow byte sequence order");
  Zutcp::PktAckBatch batch;
  Zutcp::PktTxUpdate first;
  unsigned turns = 0;
  while (!tx.ackBytesBatch(50, batch, 1, &first)) ++turns;
  ZuCHECK(turns == 1 && tx.count() == 1 && batch.ackd == 1 &&
      first.ackdFrames.length() == 1 && first.ackdFrames[0].ref.offset == 0 &&
      first.ackdFrames[0].ref.length == 50,
    "bounded continuation retires bytes independently of local transmission ordering");
#ifdef ZDEBUG
  tx.resetByteAckVisits();
  Zutcp::PktAckBatch before;
  ZuCHECK(tx.ackBytesBatch(49, before, 32) && !tx.byteAckVisits(),
    "byte ACK selection must not walk future packet history");
#endif

  Zutcp::PktTxSpace fin;
  ZuCHECK(fin.add(txDataPkt(3, 100, 10, true)), "FIN history setup");
  Zutcp::PktAckBatch payload;
  Zutcp::PktTxUpdate data;
  ZuCHECK(fin.ackBytesBatch(110, payload, 32, &data) && fin.count() == 1 &&
      data.ackdFrames.length() == 1 && data.ackdFrames[0].ref.length == 10 &&
      !data.ackdFrames[0].ref.fin,
    "payload acknowledgment retains the final sequence position");
  Zutcp::PktAckBatch tail;
  Zutcp::PktTxUpdate terminal;
  ZuCHECK(fin.ackBytesBatch(111, tail, 32, &terminal) && !fin.count() &&
      !fin.bytesInFlight() && terminal.ackdFrames.length() == 1 &&
      terminal.ackdFrames[0].ref.offset == 110 && !terminal.ackdFrames[0].ref.length &&
      terminal.ackdFrames[0].ref.fin,
    "the final position is acknowledged exactly once without inventing payload bytes");

}

void byteRtoRecovery()
{
  ZuTestScope(byteRtoRecovery);
  Zutcp::PktTxSpace tx;
  ZuCHECK(tx.add(txDataPkt(1, 0, 10)) && tx.add(txDataPkt(2, 0, 10)) &&
      tx.add(txDataPkt(3, 10, 10)), "DATA history setup failed");
  Zutcp::PktAckBatch batch;
  Zutcp::PktTxUpdate first, second, done;
  ZuCHECK(!tx.reclaimBytesBatch(0, batch, 1, first) && first.lostBytes == 100,
    "oldest-byte recovery must yield after its bounded first batch");
  ZuCHECK(!tx.reclaimBytesBatch(0, batch, 1, second) && second.lostBytes == 100 &&
      tx.reclaimBytesBatch(0, batch, 1, done) && !done.lostBytes &&
      tx.bytesInFlight() == 100,
    "RTO must reclaim copies of the oldest range without losing later DATA");
  Zutcp::SentFrameRef frame;
  while (tx.nextRetransmit(frame) && frame.kind != Zutcp::SentFrameKind::Data) { }
  ZuCHECK(frame.kind == Zutcp::SentFrameKind::Data && frame.offset == 0 &&
      frame.length == 10, "RTO must queue the oldest retained DATA range");
  Zutcp::PktAckBatch ack;
  Zutcp::PktTxUpdate prefix;
  ZuCHECK(tx.ackBytesBatch(4, ack, 8, &prefix) && prefix.ackdFrames.length() == 2 &&
      tx.add(txDataPkt(5, 4, 6)), "partial ACK/retransmission setup failed");
  batch = {};
  Zutcp::PktTxUpdate retry;
  ZuCHECK(tx.reclaimBytesBatch(4, batch, 8, retry) && retry.lostBytes == 100 &&
      tx.nextRetransmit(frame) && frame.offset == 4 && frame.length == 6,
    "repeated RTO after a partial ACK must select the surviving suffix");
  Zutcp::PktTxSpace priority;
  priority.add(txDataPkt(0,0,10));
  priority.add(txDataPkt(1,20,10));
  bool queued = priority.lose(1) && priority.lose(0);
  Zutcp::PktRetxBatch order;
  auto valid = [](const Zutcp::SentFrameRef &) { return true; };
  bool yielded = !priority.prioritizeRetransmit(0,order,1,valid);
  bool found = priority.prioritizeRetransmit(0,order,1,valid);
  Zutcp::SentFrameRef selected, later;
  ZuCHECK(queued && yielded && found && order.found &&
      priority.nextRetransmit(selected) && selected.offset == 0 &&
      priority.nextRetransmit(later) && later.offset == 20 && !priority.retransmitDropped(),
    "bounded RTO rotation must restore oldest-first order without losing pending identities");
  priority.add(txDataPkt(2,30,0,true));
  priority.lose(2);
  Zutcp::PktRetxBatch stale;
  bool held = !priority.prioritizeRetransmit(30,stale,0,valid);
  priority.forgetSACK();
  ZuCHECK(held && priority.prioritizeRetransmit(30,stale,8,valid) && !stale.found &&
      priority.retransmitPending() == 1,
    "stale priority work must not alter a newer RTO epoch's pending queue");
  Zutcp::PktRetxBatch current;
  ZuCHECK(priority.prioritizeRetransmit(30,current,8,valid) && current.found &&
      priority.nextRetransmit(selected) && selected.offset == 30 &&
      selected.fin && !selected.length,
    "oldest-byte priority includes the standalone FIN sequence endpoint");
  priority.clear();
  Zutcp::NewReno cc(1200);
  cc.sent(12000);
  cc.timeout(100);
  ZuCHECK(cc.cwnd() == 1200 && cc.ssthresh() == 6000 &&
      cc.bytesInFlight() == 12000,
    "RTO must enter one-segment slow start without freeing live flight");
  cc.release(10000);
  cc.timeout(200, true);
  ZuCHECK(cc.cwnd() == 1200 && cc.ssthresh() == 6000 && cc.bytesInFlight() == 2000,
    "repeated timeout of the same covering range must preserve ssthresh");
}

void sackWholeOriginals()
{
  ZuTestScope(sackWholeOriginals);
  Zutcp::PktTxSpace tx;
  bool setup = true;
  for (unsigned i = 0; i < 5; ++i) setup &= tx.add(txDataPkt(i, 10*i, 10, i == 4));
  const Zutcp::RxSpan blocks[] = {{10,20},{30,40}};
  Zutcp::PktSACKBatch batch;
  ZuCHECK(setup && tx.sackBytesBatch(blocks, 2, batch, 16) && batch.marked == 2 &&
      tx.sackedFlightBytes() == 200 && tx.bytesInFlight() == 500 && tx.count() == 5 &&
      !tx.ackd() && !tx.lost() && !tx.retransmitPending(),
    "whole original SACK marks must discount pipe without ACK growth or record retirement");
  Zutcp::PktSACKBatch duplicate;
  const Zutcp::RxSpan partial[] = {{21,29},{40,50}};
  Zutcp::PktSACKBatch incomplete;
  ZuCHECK(tx.sackBytesBatch(blocks, 2, duplicate, 16) && !duplicate.marked &&
      tx.sackBytesBatch(partial, 2, incomplete, 16) && !incomplete.marked &&
      tx.sackedFlightBytes() == 200 && tx.bytesInFlight() == 500,
    "duplicate SACK grants no extra discount; partial originals and FIN stay eligible");
  Zutcp::PktSACKLossBatch holes;
  Zutcp::PktTxUpdate lost;
  ZuCHECK(tx.loseSACKHolesBatch(40, holes, 16, lost) && holes.lost == 2 &&
      lost.lostBytes == 200 && lost.lostFrames.length() == 2 &&
      lost.lostFrames[0].ref.offset == 0 && lost.lostFrames[1].ref.offset == 20 &&
      tx.sackedFlightBytes() == 200 && tx.bytesInFlight() == 300 && tx.count() == 5,
    "selective loss queues two holes while retaining SACKed originals and later FIN");
  bool retransmitted = tx.add(txDataPkt(60, 0, 10));
  Zutcp::PktSACKLossBatch repeated;
  Zutcp::PktTxUpdate again;
  ZuCHECK(retransmitted && tx.loseSACKHolesBatch(40, repeated, 16, again, 50) &&
      !repeated.lost && !again.lostBytes && tx.bytesInFlight() == 400 &&
      tx.sackedFlightBytes() == 200 && tx.count() == 6,
    "the recovery-entry PN fence preserves fresh retransmissions on repeated duplicate ACKs");
  Zutcp::PktAckBatch ack;
  Zutcp::PktTxUpdate progress;
  ZuCHECK(tx.ackBytesBatch(40, ack, 16, &progress) && !tx.sackedFlightBytes() &&
      tx.bytesInFlight() == 100 && tx.count() == 1 && progress.ackdBytes == 300,
    "cumulative ACK retires marked originals through existing flight release exactly once");
  tx.clear();
}

void sackEpochBatches()
{
  ZuTestScope(sackEpochBatches);
  Zutcp::PktTxSpace tx;
  for (unsigned i = 0; i < 4; ++i) tx.add(txDataPkt(i, 10*i, 10));
  const Zutcp::RxSpan block[] = {{10,40}};
  Zutcp::PktSACKBatch first;
  ZuCHECK(!tx.sackBytesBatch(block, 1, first, 1) && first.marked == 1 &&
      tx.sackedFlightBytes() == 100 && tx.bytesInFlight() == 400,
    "SACK byte-index scanning must yield after one original without retiring it");
  uint64_t oldEpoch = tx.sackEpoch();
  tx.forgetSACK();
  ZuCHECK(tx.sackEpoch() != oldEpoch && !tx.sackedFlightBytes() &&
      tx.bytesInFlight() == 400 && tx.sackBytesBatch(block, 1, first, 8) && first.marked == 1 &&
      !tx.sackedFlightBytes(),
    "RTO epoch invalidation must forget discount in O1 and reject stale SACK continuations");
  Zutcp::PktAckBatch oldest;
  Zutcp::PktTxUpdate timeout;
  ZuCHECK(tx.reclaimBytesBatch(0, oldest, 8, timeout) && timeout.lostBytes == 100 &&
      tx.bytesInFlight() == 300 && tx.retransmitPending() == 1,
    "reneging recovery must regain the original oldest retransmission after forgetting SACK");
  Zutcp::PktSACKBatch fresh;
  unsigned turns = 0;
  while (!tx.sackBytesBatch(block, 1, fresh, 1)) ++turns;
  ZuCHECK(turns == 3 && fresh.marked == 3 && tx.sackedFlightBytes() == 300 &&
      tx.bytesInFlight() == 300 && tx.count() == 4,
    "fresh bounded evidence marks only live flight, keeping retained lost originals available");
  Zutcp::PktSACKLossBatch stale;
  Zutcp::PktTxUpdate before, after;
  bool yielded = !tx.loseSACKHolesBatch(40, stale, 1, before);
  tx.forgetSACK();
  ZuCHECK(yielded && tx.loseSACKHolesBatch(40, stale, 8, after) && !after.lostBytes &&
      tx.bytesInFlight() == 300,
    "an old selective-loss continuation must not lose replacement-epoch originals");
  Zutcp::PktSACKLossBatch retry;
  Zutcp::PktTxUpdate holes;
  turns = 0;
  while (!tx.loseSACKHolesBatch(40, retry, 1, holes)) ++turns;
  ZuCHECK(turns == 4 && retry.lost == 3 && holes.lostBytes == 300 && !tx.bytesInFlight(),
    "a fresh bounded selective-loss cursor visits all records and recovers remaining holes");
  Zutcp::PktSACKBatch pending;
  bool queued = !tx.sackBytesBatch(block, 1, pending, 0);
  tx.clear();
  tx.add(txDataPkt(0, 10, 10));
  ZuCHECK(queued && tx.sackBytesBatch(block, 1, pending, 8) && !tx.sackedFlightBytes(),
    "clear invalidates older SACK work before a replacement record reuses its key");
  tx.clear();
}

void sackReleasePaths()
{
  ZuTestScope(sackReleasePaths);
  Zutcp::PktTxSpace tx;
  const Zutcp::RxSpan block[] = {{10,20}};
  for (unsigned path = 0; path < 3; ++path) {
    tx.add(txDataPkt(path, 10, 10));
    Zutcp::PktSACKBatch mark;
    bool marked = tx.sackBytesBatch(block, 1, mark, 8) && tx.sackedFlightBytes() == 100;
    Zutcp::PktAckBatch ack;
    bool released = path == 0 ? tx.ackBytesBatch(20, ack, 8) && ack.ackd == 1 :
      path == 1 ? tx.lose(path) : tx.discard(path);
    ZuCHECK(marked && released && !tx.bytesInFlight() && !tx.sackedFlightBytes(),
      "ACK, explicit loss and discard must each remove the current discount exactly once");
    tx.clear();
  }
  auto packet = txDataPkt(10, 10, 10);
  packet.addFrame(dataRef(30, 10));
  tx.add(packet);
  Zutcp::PktSACKBatch partial;
  ZuCHECK(tx.sackBytesBatch(block, 1, partial, 8) && !partial.marked &&
      !tx.sackedFlightBytes() && tx.bytesInFlight() == 100,
    "a packet with multiple DATA intervals is marked only when every interval is covered");
  const Zutcp::RxSpan complete[] = {{10,40}};
  Zutcp::PktSACKBatch full;
  ZuCHECK(tx.sackBytesBatch(complete, 1, full, 8) && full.marked == 1 &&
      tx.sackedFlightBytes() == 100,
    "one canonical block covering all DATA intervals discounts one physical packet");
  packet.pn = 11;
  packet.sackEpoch = tx.sackEpoch();
  ZuCHECK(tx.add(packet) && tx.discard(11) && tx.bytesInFlight() == 100 &&
      tx.sackedFlightBytes() == 100,
    "a copied new send record must not inherit an older record's advisory discount");
  bool lost = tx.lose(10);
  Zutcp::PktSACKBatch late;
  ZuCHECK(lost && tx.sackBytesBatch(complete, 1, late, 8) && !late.marked &&
      !tx.sackedFlightBytes() && !tx.bytesInFlight() && tx.count() == 1,
    "late SACK on a retained lost original cannot create a discount without live flight");
  tx.clear();
}

struct SACKAllocator : Zi::IOBufAllocator {
  uint8_t bytes[128] = {};
  mutable unsigned returned = 0;
  mutable bool valid = true;

  SACKAllocator() : IOBufAllocator{nullptr, Zi::IOBufType::TxTCP, 128, 64} { }
  uint8_t *alloc(unsigned) const override { return nullptr; }
  void free(uint8_t *p, unsigned n, bool registered) const override {
    valid &= p == bytes && n == sizeof(bytes) && registered;
    ++returned;
  }
  ZmRef<ZiIOBuf> allocBuf(void *, uint32_t, uint32_t) override { return nullptr; }
};

void highestProbeOriginal()
{
  ZuTestScope(highestProbeOriginal);
  SACKAllocator allocators[4];
  ZmRef<Zutcp::TxBacking> sources[4];
  Zutcp::PktTxSpace tx;
  bool added = true;
  for (unsigned i = 0; i < 4; ++i) {
    auto &source = sources[i];
    source = new Zutcp::TxRanges::Node{};
    source->native = &allocators[i];
    source->data = allocators[i].bytes;
    source->size = sizeof(allocators[i].bytes);
    source->payloadOffset = 64;
    source->payloadLength = 10;
    auto packet = txDataPkt(i + 1, i * 10, 10);
    packet.frames[0].ref.range.storage = source;
    packet.frames[0].ref.range.offset = 64;
    added &= tx.add(packet);
    if (i != 3) tx.acceptedData(i + 1, Zutcp::timeUS(1000 + i * 100));
  }
  Zutcp::PktTailBatch batch;
  Zutcp::SentFrameRef ref;
  bool bounded = !tx.highestOriginal(0, batch, 1, ref);
  bool selected = tx.highestOriginal(0, batch, 1, ref);
  ZuCHECK(added && bounded && selected && ref.offset == 20 && ref.length == 10 &&
      ref.range.storage == sources[2] && ref.range.offset == 64,
    "bounded tail selection skips recorded-but-refused output "
    "and chooses the highest accepted original");
  Zutcp::PktAckBatch ack;
  Zutcp::PktTxUpdate update;
  bool partial = tx.ackBytesBatch(25, ack, 8, &update);
  batch = {};
  ref = {};
  while (!tx.highestOriginal(25, batch, 1, ref)) { }
  ZuCHECK(partial && ref.offset == 20 && ref.length == 10 &&
      ref.range.storage == sources[2] &&
      ref.range.storage->data + ref.range.offset == allocators[2].bytes + 64,
    "partial cumulative progress preserves the complete covering original "
    "and exact payload address");
  const Zutcp::RxSpan block[] = {{20,30}};
  Zutcp::PktSACKBatch sack;
  bool marked = tx.sackBytesBatch(block, 1, sack, 8);
  batch = {};
  ref = {};
  while (!tx.highestOriginal(25, batch, 1, ref)) { }
  ZuCHECK(marked && !ref,
    "advisory-delivered original is not selected as an outstanding tail probe");
  batch = {};
  bool pending = !tx.highestOriginal(25, batch, 0, ref);
  tx.forgetSACK();
  ZuCHECK(pending && tx.highestOriginal(25, batch, 8, ref) && !ref,
    "RTO reneging invalidates a bounded tail cursor before it can use older evidence");
  tx.clear();
}

void sackRetainsBacking()
{
  ZuTestScope(sackRetainsBacking);
  SACKAllocator sourceAllocator, packetAllocator;
  Zutcp::PktTxSpace tx;
  ZmRef<Zutcp::TxBacking> source = new Zutcp::TxRanges::Node{};
  source->data = sourceAllocator.bytes;
  source->size = sizeof(sourceAllocator.bytes);
  source->payloadOffset = 64;
  source->payloadLength = 10;
  source->backing.adopt(&sourceAllocator, source->data, source->size);
  memcpy(source->data + 64, "0123456789", 10);
  auto packet = txDataPkt(1, 10, 10);
  packet.frames[0].ref.range.storage = source;
  packet.frames[0].ref.range.offset = 64;
  Zutcp::PacketLease *lease = nullptr;
  bool added = tx.add(packet, &lease);
  if (lease)
    lease->adopt(&packetAllocator, packetAllocator.bytes, sizeof(packetAllocator.bytes));
  packet.frames.clear();
  source = nullptr;
  const Zutcp::RxSpan block[] = {{10,20}};
  Zutcp::PktSACKBatch mark;
  ZuCHECK(added && tx.sackBytesBatch(block, 1, mark, 8) && mark.marked == 1 &&
      !sourceAllocator.returned && !packetAllocator.returned && tx.count() == 1 &&
      tx.bytesInFlight() == 100 && tx.sackedFlightBytes() == 100 &&
      !memcmp(sourceAllocator.bytes + 64, "0123456789", 10),
    "SACK must retain original source refs and packet leases without changing backing bytes");
  tx.forgetSACK();
  Zutcp::PktAckBatch oldest;
  Zutcp::PktTxUpdate loss;
  bool reclaimed = tx.reclaimBytesBatch(10, oldest, 8, loss);
  Zutcp::SentFrameRef frame;
  bool retry = tx.nextRetransmit(frame);
  ZuCHECK(reclaimed && retry && frame.range.storage && frame.range.offset == 64 &&
      frame.range.storage->data + frame.range.offset == sourceAllocator.bytes + 64 &&
      !sourceAllocator.returned && !packetAllocator.returned,
    "reneging recovery must return the same original backing range without replacement refs");
  Zutcp::PktAckBatch ack;
  Zutcp::PktTxUpdate progress;
  bool completed = tx.ackBytesBatch(20, ack, 8, &progress);
  frame = {};
  loss.lostFrames.clear();
  progress.ackdFrames.clear();
  ZuCHECK(completed && !tx.count() && sourceAllocator.returned == 1 &&
      packetAllocator.returned == 1 && sourceAllocator.valid && packetAllocator.valid,
    "cumulative retirement and released update refs finally return both original leases");
  tx.clear();
}

void timestampByteWitness()
{
  ZuTestScope(timestampByteWitness);
  Zutcp::PktTxSpace tx;
  auto original = txDataPkt(1, 0, 10);
  original.tsKnown = true;
  bool stamped = tx.add(original);
  Zutcp::PktAckBatch first;
  ZuCHECK(stamped && tx.ackBytesBatch(5, first, 8, nullptr, 0, true, 0) &&
      first.tsMatch && !first.tsAmbiguous && first.tsPN == 1 &&
      first.tsSentTime == Zutcp::timeUS(100) && tx.count() == 1,
    "known timestamp zero identifies partial cumulative progress without an update buffer");
  Zutcp::PktAckBatch repeated;
  ZuCHECK(tx.ackBytesBatch(5, repeated, 8, nullptr, 0, true, 0) && !repeated.tsMatch,
    "duplicate cumulative evidence must not produce another timestamp RTT witness");
  auto retransmit = txDataPkt(2, 0, 10);
  retransmit.sentTime = Zutcp::timeUS(100);
  retransmit.tsKnown = true;
  tx.add(retransmit);
  Zutcp::PktAckBatch aliases;
  unsigned turns = 0;
  while (!tx.ackBytesBatch(10, aliases, 1, nullptr, 0, true, 0)) ++turns;
  ZuCHECK(turns == 2 && aliases.tsMatch && aliases.tsAmbiguous && aliases.tsPN == 1 &&
      aliases.tsSentTime == Zutcp::timeUS(100) && !tx.count(),
    "distinct submissions sharing an echoed timestamp/send tick stay ambiguous across batches");
  auto next = txDataPkt(3, 10, 10);
  next.tsKnown = true;
  next.tsval = 7;
  tx.add(next);
  Zutcp::PktAckBatch absent;
  ZuCHECK(tx.ackBytesBatch(20, absent, 8) && !absent.tsMatch,
    "timestamp-absent ACKs preserve the original Karn path without fabricated option evidence");
  next = txDataPkt(4, 20, 10);
  next.tsKnown = true;
  next.tsval = 7;
  tx.add(next);
  Zutcp::PktAckBatch mismatch;
  bool unmatched = tx.ackBytesBatch(30, mismatch, 8, nullptr, 0, true, 8);
  next = txDataPkt(5, 30, 10);
  next.tsKnown = true;
  next.tsval = 7;
  tx.add(next);
  bool refused = tx.discard(5);
  Zutcp::PktAckBatch missing;
  ZuCHECK(unmatched && !mismatch.tsMatch && refused &&
      tx.ackBytesBatch(40, missing, 8, nullptr, 0, true, 7) && !missing.tsMatch,
    "mismatching echoes and discarded send-refusal records provide no timestamp witness");
  tx.clear();
}

void rackSharedChronology()
{
  ZuTestScope(rackSharedChronology);
  Zutcp::PktTxSpace tx;
  ZmRef<Zutcp::TxBacking> first = new Zutcp::TxRanges::Node{};
  ZmRef<Zutcp::TxBacking> second = new Zutcp::TxRanges::Node{};
  auto original = txRangePkt(1,0,10);
  original.frames[0].ref.range.storage = first;
  auto tail = txRangePkt(2,10,10);
  tail.frames[0].ref.range.storage = second;
  ZuCHECK(tx.add(original) && tx.add(tail), "original records use existing shared backing");
  tx.acceptedData(1,ZuTime{1});
  tx.acceptedData(2,ZuTime{1,1000000});
  auto refused = original;
  refused.pn = 3;
  ZuCHECK(tx.add(refused) && tx.discard(3) && first->rack.pn == 1 &&
      !first->rack.retransmitted,
    "a refused provisional record cannot change accepted original chronology");
  auto retry = original;
  retry.pn = 4;
  ZuCHECK(tx.add(retry), "a retained copy shares the original without a new range registry");
  tx.acceptedData(4,ZuTime{1,2000000});
  Zutcp::RackState rack;
  rack.minRTT = Zutcp::timeUS(10000);
  rack.srtt = Zutcp::timeUS(10000);
  rack.beginAck(ZuTime{1,11000000});
  Zutcp::RxSpan span{10,20};
  Zutcp::PktSACKBatch mark;
  ZuCHECK(tx.sackBytesBatch(&span,1,mark,10,&rack,ZuTime{1,11000000}) &&
      rack.end == 20 && rack.sentTime == (ZuTime{1,1000000}) &&
      first->rack.pn == 4 && first->rack.retransmitted,
    "full SACK evidence uses accepted time while all original copies retain latest chronology");
  Zutcp::RackLossBatch loss;
  Zutcp::PktTxUpdate update;
  ZuCHECK(tx.rackLossBatch(rack,ZuTime{1,30000000},loss,10,update) &&
      !update.lostBytes && tx.bytesInFlight() == 300,
    "older packet IDs cannot declare the newer retransmission lost");
  // A later delivered original now makes both copies of the first original
  // eligible together. Only the last live copy may enqueue its shared range.
  Zutcp::RackTransmission nextDelivery;
  nextDelivery.known = true;
  nextDelivery.sentTime = ZuTime{1,3000000};
  nextDelivery.end = 30;
  rack.beginAck(ZuTime{1,13000000});
  rack.observe(nextDelivery,ZuTime{1,13000000},false,0);
  loss = {};
  Zutcp::PktTxUpdate copies;
  tx.rackLossBatch(rack,ZuTime{1,30000000},loss,10,copies);
  Zutcp::SentFrameRef retryRef, extra;
  bool once = tx.nextRetransmit(retryRef) && !tx.nextRetransmit(extra);
  ZuCHECK(copies.lostBytes == 200 && once && retryRef.range.storage == first &&
      retryRef.offset == 0 && retryRef.length == 10 && tx.count() == 3,
    "all copies queue the same original once after final flight release");
  Zutcp::PktAckBatch partial;
  rack.beginAck(ZuTime{1,30000000});
  ZuCHECK(tx.ackBytesBatch(5,partial,10,nullptr,0,false,0,&rack,ZuTime{1,30000000}) &&
      rack.end == 30 && tx.count() == 3,
    "partial cumulative coverage does not claim a complete segment delivered");
  Zutcp::PktAckBatch full;
  rack.beginAck(ZuTime{1,40000000});
  ZuCHECK(tx.ackBytesBatch(20,full,10,nullptr,0,false,0,&rack,ZuTime{1,40000000}) &&
      rack.sentTime == (ZuTime{1,3000000}) && rack.end == 30 &&
      rack.rtt == Zutcp::timeUS(38000) && !tx.count(),
    "cumulative ACK folds the latest accepted retransmission before retiring its records");
  tx.clear();
}

void rackBoundedLoss()
{
  ZuTestScope(rackBoundedLoss);
  Zutcp::PktTxSpace tx;
  ZmRef<Zutcp::TxBacking> storage[3];
  for (unsigned i = 0; i < 3; ++i) {
    storage[i] = new Zutcp::TxRanges::Node{};
    auto p = txRangePkt(i,10*i,10);
    p.frames[0].ref.range.storage = storage[i];
    tx.add(p);
    tx.acceptedData(i,ZuTime{1,int32_t(i * 1000000)});
  }
  Zutcp::RackState rack;
  rack.minRTT = rack.srtt = Zutcp::timeUS(10000);
  rack.beginAck(ZuTime{1,12000000});
  Zutcp::RxSpan span{20,30};
  Zutcp::PktSACKBatch mark;
  tx.sackBytesBatch(&span,1,mark,10,&rack,ZuTime{1,12000000});
  Zutcp::RackLossBatch batch;
  Zutcp::PktTxUpdate before;
  bool done = tx.rackLossBatch(rack,ZuTime{1,12000000},batch,1,before);
  unsigned turns = 1;
  while (!done) {
    done = tx.rackLossBatch(rack,ZuTime{1,12000000},batch,1,before);
    ++turns;
  }
  ZuCHECK(turns == 4 && !before.lostBytes && rack.deadline == (ZuTime{1,13500000}),
    "bounded grace scan resumes retained entries and selects the RFC group-settling deadline");
  batch = {};
  Zutcp::PktTxUpdate lost;
  while (!tx.rackLossBatch(rack,ZuTime{1,13500000},batch,1,lost)) { }
  ZuCHECK(lost.lostBytes == 200 && tx.sackedFlightBytes() == 100 &&
      tx.bytesInFlight() == 100 && tx.count() == 3,
    "expired predecessor holes use ordinary loss without freeing retained source records");
  tx.forgetSACK();
  rack.recovery(true);
  batch = {};
  Zutcp::PktTxUpdate rto;
  while (!tx.rackLossBatch(rack,ZuTime{2},batch,1,rto,true,20)) { }
  Zutcp::SentFrameRef ref;
  bool recovered = false;
  while (tx.nextRetransmit(ref)) recovered |= ref.offset == 20;
  ZuCHECK(recovered && !tx.sackedFlightBytes(),
    "RTO epoch invalidation makes the formerly SACKed retained original recoverable");
  tx.clear();
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testFrameIdentity);
  ZuTestCall(retainedLostByteHistory);
  ZuTestCall(outstandingLifecycleIndex);
  ZuTestCall(retxSkipsUnackdData);
  ZuTestCall(testLosslessFrameUpdates);
  ZuTestCall(cumulativeByteHistory);
  ZuTestCall(byteHistoryBatchAndFin);
  ZuTestCall(byteRtoRecovery);
  ZuTestCall(sackWholeOriginals);
  ZuTestCall(sackEpochBatches);
  ZuTestCall(sackReleasePaths);
  ZuTestCall(sackRetainsBacking);
  ZuTestCall(highestProbeOriginal);
  ZuTestCall(timestampByteWitness);
  ZuTestCall(rackSharedChronology);
  ZuTestCall(rackBoundedLoss);
}
