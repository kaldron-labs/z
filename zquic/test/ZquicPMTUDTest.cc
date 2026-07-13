//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/Zquic.hh>

using namespace ZuTestUtil;

void testPMTUD()
{
  ZuTestScope(testPMTUD);

  Zquic::Path path = Zquic::Path::client(
    ZiSockAddr{ZiIP("127.0.0.1"), 10000},
    ZiSockAddr{ZiIP("127.0.0.1"), 10001});
  ZuCHECK(path.activeMaxUDP() == Zquic::MinUDPPayload,
    "initial active UDP size mismatch");
  ZuCHECK(!path.canSend(1), "unvalidated anti-amplification allowed send");
  ZuCHECK(path.antiAmpLimit() == 0 &&
      path.antiAmpRemaining() == 0 &&
      path.sendAllowance() == 0,
    "empty anti-amplification budget mismatch");
  path.received(500);
  ZuCHECK(path.canSend(1200), "anti-amplification budget not applied");
  ZuCHECK(path.antiAmpLimit() == 1500 &&
      path.antiAmpRemaining() == 1500 &&
      path.sendAllowance() == 1200,
    "anti-amplification allowance mismatch");
  ZuCHECK(!path.reserveSend(1300), "oversized reservation allowed");
  ZuCHECK(path.reserveSend(1200), "valid reservation failed");
  ZuCHECK(path.diag().bytesTx == 1200 &&
      path.antiAmpRemaining() == 300,
    "reservation accounting mismatch");
  ZuCHECK(!path.reserveSend(301), "over-budget reservation allowed");
  ZuCHECK(path.reserveSend(300), "exact anti-amplification budget failed");
  ZuCHECK(path.sendAllowance() == 0, "spent budget still allowed send");
  path.validated();
  ZuCHECK(path.reserveSend(1200) && path.antiAmpRemaining() ==
      uint64_t(-1),
    "validated path did not bypass anti-amplification");
  path.configuredMaxUDP(1400);
  path.peerMaxUDP(1360);
  ZuCHECK(path.nextProbeSize(200) == 1360 &&
      path.startNextProbe(200) &&
      path.probePending() &&
      path.probeSize() == 1360 &&
      path.probeAttempts() == 1,
    "PMTUD candidate probe setup mismatch");
  path.probeAckd();
  ZuCHECK(path.activeMaxUDP() == 1360 &&
      path.pmtudState() == Zquic::PMTUDState::SearchComplete &&
      !path.probeAttempts() &&
      !path.nextProbeSize(64),
    "PMTUD capped probe success mismatch");

  Zquic::Path loss = Zquic::Path::client(
    ZiSockAddr{ZiIP("127.0.0.1"), 10002},
    ZiSockAddr{ZiIP("127.0.0.1"), 10003});
  loss.validated();
  loss.configuredMaxUDP(Zquic::BufSize);
  ZuCHECK(loss.startNextProbe(200) &&
      loss.probeSize() == 1400,
    "PMTUD loss candidate setup mismatch");
  loss.probeLost();
  ZuCHECK(loss.failureFloor() == 1400 &&
      loss.nextProbeSize(100) == 1300 &&
      loss.diag().probesLost == 1,
    "PMTUD loss floor mismatch");

  Zquic::Path retry = Zquic::Path::client(
    ZiSockAddr{ZiIP("127.0.0.1"), 10006},
    ZiSockAddr{ZiIP("127.0.0.1"), 10007});
  retry.validated();
  ZuCHECK(retry.startNextProbe(128) &&
      retry.probeSize() == 1328 &&
      retry.probeAttempts() == 1,
    "PMTUD retry first probe setup mismatch");
  ZuCHECK(retry.probeExpired() &&
      retry.probeRetryPending() &&
      retry.retryProbeSize() == 1328 &&
      retry.nextProbeSize(64) == 1328,
    "PMTUD first expiry did not retain retry candidate");
  ZuCHECK(retry.startNextProbe(64) &&
      retry.probeSize() == 1328 &&
      retry.probeAttempts() == 2,
    "PMTUD second retry setup mismatch");
  ZuCHECK(retry.probeExpired() &&
      retry.startNextProbe(64) &&
      retry.probeAttempts() == Zquic::Path::MaxProbeAttempts,
    "PMTUD final retry setup mismatch");
  ZuCHECK(!retry.probeExpired() &&
      !retry.probeRetryPending() &&
      retry.failureFloor() == 1328 &&
      retry.nextProbeSize(64) == 1264 &&
      retry.diag().probesExpired == Zquic::Path::MaxProbeAttempts &&
      retry.diag().probesLost == 1,
    "PMTUD final expiry did not lower search floor");

  Zquic::Path limited = Zquic::Path::client(
    ZiSockAddr{ZiIP("127.0.0.1"), 10004},
    ZiSockAddr{ZiIP("127.0.0.1"), 10005});
  limited.configuredMaxUDP(Zquic::BufSize);
  ZuCHECK(!limited.canSendProbe(1400) &&
      !limited.startNextProbe(200) &&
      !limited.probePending(),
    "unvalidated PMTUD probe ignored empty anti-amplification budget");
  limited.received(400);
  ZuCHECK(limited.antiAmpRemaining() == 1200 &&
      !limited.canSendProbe(1400) &&
      !limited.startNextProbe(200),
    "unvalidated PMTUD probe exceeded anti-amplification budget");
  limited.received(100);
  ZuCHECK(limited.canSendProbe(1400) &&
      limited.startNextProbe(200) &&
      limited.probeSize() == 1400 &&
      limited.diag().probesSent == 1,
    "unvalidated PMTUD probe did not use available anti-amplification budget");

  path.configuredMaxUDP(Zquic::BufSize);
  path.peerMaxUDP(Zquic::BufSize);
  path.startProbe(1400);
  path.probeAckd();
  ZuCHECK(path.activeMaxUDP() == 1400, "PMTUD probe success not applied");
  path.applyHint({Zquic::PathHintKind::PktTooBig, 1300, 0});
  ZuCHECK(path.activeMaxUDP() == 1300 &&
      path.ceiling() == 1300 &&
      path.failureFloor() == 1300,
    "PMTU lower hint not applied");
  path.blackhole();
  ZuCHECK(path.activeMaxUDP() == Zquic::MinUDPPayload,
    "blackhole fallback mismatch");
  ZuCHECK(path.ecnDisabled(), "ECN should remain disabled in first release");

  Zquic::Path blackhole = Zquic::Path::client(
    ZiSockAddr{ZiIP("127.0.0.1"), 10008},
    ZiSockAddr{ZiIP("127.0.0.1"), 10009});
  blackhole.validated();
  blackhole.startProbe(1400);
  blackhole.probeAckd();
  ZuCHECK(blackhole.activeMaxUDP() == 1400,
    "blackhole setup did not grow active payload");
  blackhole.startProbe(1400);
  ZuCHECK(blackhole.probeExpired() &&
      blackhole.startNextProbe(64) &&
      blackhole.probeExpired() &&
      blackhole.startNextProbe(64) &&
      blackhole.probeAttempts() == Zquic::Path::MaxProbeAttempts,
    "blackhole retry sequence mismatch");
  ZuCHECK(!blackhole.probeExpired() &&
      blackhole.activeMaxUDP() == Zquic::MinUDPPayload &&
      blackhole.pmtudState() == Zquic::PMTUDState::Error &&
      blackhole.diag().blackholes == 1,
    "repeated active-size probe expiry did not trigger blackhole fallback");
}

void testPathECN()
{
  ZuTestScope(testPathECN);

  Zquic::Path path = Zquic::Path::client(
    ZiSockAddr{ZiIP("127.0.0.1"), 10100},
    ZiSockAddr{ZiIP("127.0.0.1"), 10101});
  ZuCHECK(path.ecnDisabled(), "new path should default ECN disabled");
  ZuCHECK(path.ecnState() == Zquic::PathECNState::Disabled,
    "new path ECN state mismatch");
  ZuCHECK(path.activeECN() == Zquic::EcnMark::NotECT,
    "new path active ECN mismatch");
  ZuCHECK(path.txECN(Zquic::PktNumSpace::AppData, true, false) ==
      Zquic::EcnMark::NotECT,
    "disabled path unexpectedly selected ECN mark");

  path.ecn(true);
  ZuCHECK(!path.ecnDisabled() &&
      path.ecnState() == Zquic::PathECNState::Testing &&
      path.activeECN() == Zquic::EcnMark::ECT0,
    "enabled path did not enter ECN testing");
  ZuCHECK(path.txECN(Zquic::PktNumSpace::AppData, true, false) ==
      Zquic::EcnMark::ECT0,
    "testing AppData packet did not select ECT0");
  ZuCHECK(path.txECN(Zquic::PktNumSpace::Initial, true, false) ==
      Zquic::EcnMark::NotECT,
    "Initial packet should remain NotECT");
  ZuCHECK(path.txECN(Zquic::PktNumSpace::AppData, false, false) ==
      Zquic::EcnMark::NotECT,
    "non-ack-eliciting packet should remain NotECT");
  ZuCHECK(path.txECN(Zquic::PktNumSpace::AppData, true, true) ==
      Zquic::EcnMark::NotECT,
    "PMTUD probe should remain NotECT");

  path.ecnCapable();
  ZuCHECK(path.ecnState() == Zquic::PathECNState::Capable &&
      path.txECN(Zquic::PktNumSpace::AppData, true, false) ==
	Zquic::EcnMark::ECT0,
    "capable path did not keep ECT0 active");
  path.failECN();
  ZuCHECK(path.ecnDisabled() &&
      path.ecnState() == Zquic::PathECNState::Failed &&
      path.activeECN() == Zquic::EcnMark::NotECT &&
      path.txECN(Zquic::PktNumSpace::AppData, true, false) ==
	Zquic::EcnMark::NotECT,
    "failed path did not disable ECN marking");
  path.ecn(false);
  ZuCHECK(path.ecnState() == Zquic::PathECNState::Disabled,
    "explicit ECN disable state mismatch");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testPMTUD);
  ZuTestCall(testPathECN);
}
