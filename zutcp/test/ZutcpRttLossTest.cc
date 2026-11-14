//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/Zutcp.hh>

using namespace ZuTestUtil;

static Zutcp::TxPkt packet(uint64_t pn, ZuTime sent)
{
  Zutcp::TxPkt p;
  p.pn = pn;
  p.sentTime = sent;
  p.bytes = 1200;
  p.space = Zutcp::PktNumSpace::AppData;
  p.ackEliciting = true;
  p.inFlight = true;
  return p;
}

static void deadline(const Zutcp::RttEstimator &rtt, ZuTime expected)
{
  ZuTestScopeRT(deadline);
  ZuTime sent = Zutcp::timeUS(1000000);
  Zutcp::PktTxSpace tx;
  ZuCheckRT(tx.add(packet(1, sent)) &&
      tx.add(packet(2, sent + Zutcp::timeUS(1000))) && tx.ack(2),
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
  Zutcp::RttEstimator rtt;
  rtt.sample(Zutcp::timeUS(100000), ZuTime{0}, false);
  rtt.sample(Zutcp::timeUS(10000), ZuTime{0}, false);
  ZuCHECK(rtt.smoothed() == Zutcp::timeUS(88750) &&
      rtt.timeThreshold() == (ZuTime{0, 99843750}),
    "100 ms then 10 ms must yield exactly 99.84375 ms");
  Zutcp::PktTxSpace tx;
  ZuTime sent = Zutcp::timeUS(1000000);
  ZuCHECK(tx.add(packet(1, sent)) && tx.add(packet(2, sent)) && tx.ack(2) &&
      !tx.markPktThreshLoss(2, 3) &&
      !tx.markTimeThreshLoss(2, sent + Zutcp::timeUS(20000), rtt.timeThreshold()) &&
      tx.bytesInFlight() == 1200,
    "falling RTT must retain a 20 ms-old packet with a later ACK");
  ZuTestCall(deadline, rtt, ZuTime{0, 99843750});
}

void testOtherRTT()
{
  ZuTestScope(testOtherRTT);
  Zutcp::RttEstimator initial;
  ZuTestCall(deadline, initial, Zutcp::timeUS(374625));
  Zutcp::RttEstimator rising;
  rising.sample(Zutcp::timeUS(10000), ZuTime{0}, false);
  rising.sample(Zutcp::timeUS(100000), ZuTime{0}, false);
  ZuTestCall(deadline, rising, Zutcp::timeUS(112500));
  Zutcp::RttEstimator constant;
  constant.sample(Zutcp::timeUS(10000), ZuTime{0}, false);
  constant.sample(Zutcp::timeUS(10000), ZuTime{0}, false);
  ZuTestCall(deadline, constant, Zutcp::timeUS(11250));
  Zutcp::RttEstimator floor;
  floor.sample(Zutcp::timeUS(100), ZuTime{0}, false);
  ZuTestCall(deadline, floor, Zutcp::RttEstimator::Granularity);
}

void testDataRto()
{
  ZuTestScope(testDataRto);
  Zutcp::RtoParams params;
  Zutcp::RttEstimator rtt;
  Zutcp::DataRto timer;
  ZuTime now{10};
  ZuCHECK(params.valid() && timer.timeout(rtt, params) == ZuTime{1},
    "unsampled DATA RTO must start at one second");
  timer.sent(now, rtt, params);
  timer.sent(now + Zutcp::timeUS(500000), rtt, params);
  ZuCHECK(timer.deadline() == ZuTime{11} &&
      !timer.expire(ZuTime{11} - ZuTime{0, 1}, rtt, params),
    "new sends cannot postpone oldest DATA or expire it early");
  ZuCHECK(timer.expire(ZuTime{11}, rtt, params) && timer.backoff() == 1 &&
      timer.deadline() == ZuTime{13},
    "expiry must double and restart the RTO");
  timer.sent(ZuTime{11, 100}, rtt, params, true);
  ZuCHECK(timer.deadline() == (ZuTime{13, 100}),
    "next RTO must be measured from actual retransmission acceptance");
  for (unsigned i = 0; i < 8; ++i)
    timer.expire(timer.deadline(), rtt, params);
  ZuCHECK(timer.timeout(rtt, params) == ZuTime{60},
    "repeated expiry must cap exponential backoff at sixty seconds");
  rtt.sample(Zutcp::timeUS(10000), ZuTime{0}, false);
  timer.ack(ZuTime{200}, true, rtt, params);
  ZuCHECK(!timer.backoff() && timer.deadline() == (ZuTime{200, 200000000}),
    "partial cumulative progress must restart the adaptive RTO at its floor");
  timer.ack(ZuTime{201}, false, rtt, params);
  ZuCHECK(!*timer.deadline() && !timer.expire(ZuTime{300}, rtt, params),
    "complete acknowledgment must disarm the DATA timer");
  params.min = Zutcp::timeUS(1);
  params.granularity = Zutcp::timeUS(100000);
  ZuCHECK(timer.timeout(rtt, params) == Zutcp::timeUS(110000),
    "adaptive RTO must use the configured variance granularity");
  params.max = ZuTime{0};
  ZuCHECK(!params.valid(), "inconsistent RTO bounds must be refused");
  timer.reset();
  ZuCHECK(!*timer.deadline() && !timer.backoff(), "reset must clear timer state");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testFallingRTT);
  ZuTestCall(testOtherRTT);
  ZuTestCall(testDataRto);
}
