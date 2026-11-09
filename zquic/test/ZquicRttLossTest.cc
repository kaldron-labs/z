//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/Zquic.hh>

using namespace ZuTestUtil;

static Zquic::TxPkt packet(uint64_t pn, ZuTime sent)
{
  Zquic::TxPkt p;
  p.pn = pn;
  p.sentTime = sent;
  p.bytes = 1200;
  p.space = Zquic::PktNumSpace::AppData;
  p.ackEliciting = true;
  p.inFlight = true;
  return p;
}

static void deadline(const Zquic::RttEstimator &rtt, ZuTime expected)
{
  ZuTestScopeRT(deadline);
  ZuTime sent = Zquic::timeUS(1000000);
  Zquic::PktTxSpace tx;
  ZuCheckRT(tx.add(packet(1, sent)) &&
      tx.add(packet(2, sent + Zquic::timeUS(1000))) && tx.ack(2),
    log_("loss deadline setup must acknowledge a later packet"));
  ZuCheckRT(rtt.timeThreshold() == expected,
    log_("loss threshold must use max(latest, smoothed) at full precision"));
  ZuTime due = sent + expected;
  ZuCheckRT(tx.nextLossTime(2, rtt.timeThreshold()) == due &&
      !tx.markPktThreshLoss(2, 3),
    log_("deadline must apply without packet-threshold evidence"));
  ZuCheckRT(!tx.markTimeThreshLoss(
      2, due - ZuTime{0, 1}, rtt.timeThreshold()) &&
      tx.bytesInFlight() == 1200 && !tx.lost(),
    log_("packet must remain in flight one nanosecond before deadline"));
  uint64_t bytes = 0;
  ZuTime lostSent;
  ZuCheckRT(tx.markTimeThreshLoss(
      2, due, rtt.timeThreshold(), &bytes, &lostSent) == 1 &&
      bytes == 1200 && lostSent == sent && !tx.bytesInFlight() &&
      !tx.nextLossTime(2, rtt.timeThreshold()),
    log_("packet must become lost exactly at deadline and retire once"));
  ZuCheckRT(!tx.markTimeThreshLoss(2, due, rtt.timeThreshold(), &bytes) && !bytes,
    log_("repeated time loss scan must not retire bytes twice"));
}

void testFallingRTT()
{
  ZuTestScope(testFallingRTT);
  Zquic::RttEstimator rtt;
  rtt.sample(Zquic::timeUS(100000), ZuTime{0}, false);
  rtt.sample(Zquic::timeUS(10000), ZuTime{0}, false);
  ZuCHECK(rtt.smoothed() == Zquic::timeUS(88750) &&
      rtt.timeThreshold() == (ZuTime{0, 99843750}),
    "100 ms then 10 ms must yield exactly 99.84375 ms");
  Zquic::PktTxSpace tx;
  ZuTime sent = Zquic::timeUS(1000000);
  ZuCHECK(tx.add(packet(1, sent)) && tx.add(packet(2, sent)) && tx.ack(2) &&
      !tx.markPktThreshLoss(2, 3) &&
      !tx.markTimeThreshLoss(2, sent + Zquic::timeUS(20000), rtt.timeThreshold()) &&
      tx.bytesInFlight() == 1200,
    "falling RTT must retain a 20 ms-old packet with a later ACK");
  ZuTestCall(deadline, rtt, ZuTime{0, 99843750});
}

void testOtherRTT()
{
  ZuTestScope(testOtherRTT);
  Zquic::RttEstimator initial;
  ZuTestCall(deadline, initial, Zquic::timeUS(374625));
  Zquic::RttEstimator rising;
  rising.sample(Zquic::timeUS(10000), ZuTime{0}, false);
  rising.sample(Zquic::timeUS(100000), ZuTime{0}, false);
  ZuTestCall(deadline, rising, Zquic::timeUS(112500));
  Zquic::RttEstimator constant;
  constant.sample(Zquic::timeUS(10000), ZuTime{0}, false);
  constant.sample(Zquic::timeUS(10000), ZuTime{0}, false);
  ZuTestCall(deadline, constant, Zquic::timeUS(11250));
  Zquic::RttEstimator floor;
  floor.sample(Zquic::timeUS(100), ZuTime{0}, false);
  ZuTestCall(deadline, floor, Zquic::RttEstimator::Granularity);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testFallingRTT);
  ZuTestCall(testOtherRTT);
}
