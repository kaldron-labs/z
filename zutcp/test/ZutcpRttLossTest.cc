//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/Zutcp.hh>

using namespace ZuTestUtil;

void testRTTSmoothing()
{
  ZuTestScope(testRTTSmoothing);
  Zutcp::RttEstimator rtt;
  rtt.sample(Zutcp::timeUS(100000));
  rtt.sample(Zutcp::timeUS(10000));
  ZuCHECK(rtt.latest() == Zutcp::timeUS(10000) && rtt.min() == Zutcp::timeUS(10000) &&
      rtt.smoothed() == Zutcp::timeUS(88750) && rtt.variance() == Zutcp::timeUS(60000),
    "falling RTT preserves the RFC6298 smoothed and variance precision");
  rtt.sample(ZuTime{0});
  ZuCHECK(rtt.smoothed() == Zutcp::timeUS(88750), "zero RTT is not a timing witness");
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
  rtt.sample(Zutcp::timeUS(10000));
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

void rackChronology()
{
  ZuTestScope(rackChronology);
  Zutcp::RackState rack;
  rack.minRTT = Zutcp::timeUS(10000);
  rack.srtt = Zutcp::timeUS(20000);
  Zutcp::RackTransmission later;
  later.known = true;
  later.sentTime = ZuTime{1, 1};
  later.end = 30;
  rack.beginAck(ZuTime{1, 10000001});
  rack.observe(later, ZuTime{1, 10000001}, false, 0);
  Zutcp::RackTransmission earlier = later;
  earlier.end = 20;
  rack.observe(earlier, ZuTime{1, 10000001}, false, 0);
  ZuCHECK(rack.known && rack.end == 30 && rack.sentTime == (ZuTime{1,1}) &&
      rack.reordering && rack.allowance(3) == Zutcp::timeUS(2500),
    "equal-clock sequence tie and original reordering preserve nanosecond chronology");
  rack.ack(0,100,true);
  rack.ack(0,100,true);
  ZuCHECK(rack.multiplier == 2 && rack.persist == 16 &&
      rack.allowance(3) == Zutcp::timeUS(5000),
    "DSACK increases the reordering allowance only once in one round");
  rack.ack(100,200,true);
  ZuCHECK(rack.multiplier == 3 && rack.round == 200,
    "a later completed round permits one further DSACK adjustment");
  for (unsigned i = 0; i < 16; ++i) { rack.recovery(true); rack.recovery(false); }
  ZuCHECK(rack.multiplier == 1 && !rack.persist,
    "sixteen DSACK-free recovery exits restore the initial allowance");
  later.retransmitted = true;
  later.tsKnown = true;
  later.tsval = 10;
  later.end = 40;
  later.sentTime = ZuTime{2};
  rack.beginAck(ZuTime{2,1000000});
  rack.observe(later, ZuTime{2,1000000}, false, 0);
  rack.observe(later, ZuTime{2,20000000}, true, 9);
  ZuCHECK(rack.end == 30,
    "too-young retransmission or echo predating it cannot advance RACK");
  rack.beginAck(ZuTime{2,20000000});
  rack.observe(later, ZuTime{2,20000000}, false, 0);
  ZuCHECK(rack.end == 40 && rack.sentTime == ZuTime{2},
    "timestamp-absent retransmission evidence works after the min-RTT ambiguity bound");
  rack.multiplier = 100;
  ZuCHECK(rack.allowance(0) == rack.srtt,
    "adapted allowance remains bounded by smoothed RTT");
  rack.reordering = false;
  ZuCHECK(!rack.allowance(3),
    "three SACKed segments permit zero allowance only before reordering is known");
}

void testTailLossProbe()
{
  ZuTestScope(testTailLossProbe);
  using Zutcp::TailLossProbe;
  ZuTime now{10};
  ZuTime rtt = Zutcp::timeUS(10000), peer = Zutcp::timeUS(200000);
  ZuCHECK(TailLossProbe::interval({}, ZuTime{1}, peer, true) == ZuTime{1} &&
      TailLossProbe::interval(rtt, ZuTime{1}, peer, false) == Zutcp::timeUS(20000) &&
      TailLossProbe::interval(rtt, ZuTime{1}, peer, true) == Zutcp::timeUS(220000),
    "TLP distinguishes unsampled, multiple-original and peer-delayed single-original flights");
  TailLossProbe probe;
  probe.arm(now, rtt, ZuTime{1}, peer, true, now + Zutcp::timeUS(200000), true);
  ZuCHECK(probe.deadline() == now + Zutcp::timeUS(200000) &&
      !probe.expire(probe.deadline()) && !probe.pending() && !*probe.deadline(),
    "RTO caps PTO exactly, but a connection without a fresh RTT sample cannot probe");
  probe.sampledRTT();
  probe.original(100);
  probe.arm(now, rtt, ZuTime{1}, peer, probe.singleOriginal(50), now + ZuTime{1}, true);
  ZuCHECK(probe.deadline() == now + Zutcp::timeUS(20000) &&
      !probe.expire(now + Zutcp::timeUS(19999)) && probe.expire(probe.deadline()),
    "a fresh-sampled multi-original flight expires at twice SRTT without early probing");
  // A refused output has no accepted() call and cannot consume the RTT generation.
  ZuCHECK(!probe.pending() && probe.freshRTT(),
    "refused probe output does not create an outstanding probe or consume sample evidence");
  probe.accepted(200, true, now);
  probe.arm(now, rtt, ZuTime{1}, peer, true, now + ZuTime{1}, true);
  ZuCHECK(probe.pending() && !probe.freshRTT() && !*probe.deadline() &&
      !probe.ack(200, false, false, false) && probe.pending(),
    "one accepted retransmission holds its episode through an ambiguous exact cumulative ACK");
  ZuCHECK(!probe.ack(200, true, false, false) && !probe.pending(),
    "a validated duplicate ACK without SACK proves both copies arrived");
  probe.arm(now, rtt, ZuTime{1}, peer, true, now + ZuTime{1}, true);
  ZuCHECK(!probe.expire(probe.deadline()),
    "completing an episode without a new RTT sample cannot enable another probe");
  probe.sampledRTT();
  probe.accepted(300, true, now);
  ZuCHECK(probe.ack(301, false, false, false) &&
      !probe.ack(302, false, false, false) && !probe.pending(),
    "an ACK beyond the retransmitted probe requests its repaired-loss response exactly once");
  probe.sampledRTT();
  probe.accepted(400, true, now);
  ZuCHECK(!probe.ack(400, false, true, true) && !probe.pending(),
    "matching DSACK clears a spurious tail retransmission without congestion response");
  probe.sampledRTT();
  probe.accepted(500, false, now);
  ZuCHECK(!probe.ack(500, false, false, false) && !probe.pending(),
    "new-data probe delivery completes without retransmission ambiguity");
  auto generation = probe.generation();
  probe.recovery();
  ZuCHECK(probe.generation() != generation && !probe.pending() && !*probe.deadline(),
    "recovery entry invalidates bounded probe work and pending deadlines");
  probe.reset();
  ZuCHECK(!probe.freshRTT() && !probe.pending() && !*probe.deadline(),
    "a replacement connection cannot inherit probe or sample permission");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testRTTSmoothing);
  ZuTestCall(testDataRto);
  ZuTestCall(rackChronology);
  ZuTestCall(testTailLossProbe);
}
