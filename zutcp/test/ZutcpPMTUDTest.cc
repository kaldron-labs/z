//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/Zutcp.hh>

using namespace ZuTestUtil;

void testPMTUD()
{
  ZuTestScope(testPMTUD);

  Zutcp::Path path = Zutcp::Path::client(
    ZiSockAddr{ZiIP("127.0.0.1"), 10000},
    ZiSockAddr{ZiIP("127.0.0.1"), 10001});
  ZuCHECK(path.activeMaxPayload() == Zutcp::MinPktPayload,
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
  path.configuredMaxPayload(1400);
  path.peerMaxPayload(1360);
  ZuCHECK(path.nextProbeSize(200) == 1360 &&
      path.startNextProbe(200) &&
      path.probePending() &&
      path.probeSize() == 1360 &&
      path.probeAttempts() == 1,
    "PMTUD candidate probe setup mismatch");
  path.probeAckd();
  ZuCHECK(path.activeMaxPayload() == 1360 &&
      path.pmtudState() == Zutcp::PMTUDState::SearchComplete &&
      !path.probeAttempts() &&
      !path.nextProbeSize(64),
    "PMTUD capped probe success mismatch");

  Zutcp::Path loss = Zutcp::Path::client(
    ZiSockAddr{ZiIP("127.0.0.1"), 10002},
    ZiSockAddr{ZiIP("127.0.0.1"), 10003});
  loss.validated();
  loss.configuredMaxPayload(Zutcp::BufSize);
  ZuCHECK(loss.startNextProbe(200) &&
      loss.probeSize() == 1400,
    "PMTUD loss candidate setup mismatch");
  loss.probeLost();
  ZuCHECK(loss.failureFloor() == 1400 &&
      loss.nextProbeSize(100) == 1300 &&
      loss.diag().probesLost == 1,
    "PMTUD loss floor mismatch");

  Zutcp::Path retry = Zutcp::Path::client(
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
      retry.probeAttempts() == Zutcp::Path::MaxProbeAttempts,
    "PMTUD final retry setup mismatch");
  ZuCHECK(!retry.probeExpired() &&
      !retry.probeRetryPending() &&
      retry.failureFloor() == 1328 &&
      retry.nextProbeSize(64) == 1264 &&
      retry.diag().probesExpired == Zutcp::Path::MaxProbeAttempts &&
      retry.diag().probesLost == 1,
    "PMTUD final expiry did not lower search floor");

  Zutcp::Path limited = Zutcp::Path::client(
    ZiSockAddr{ZiIP("127.0.0.1"), 10004},
    ZiSockAddr{ZiIP("127.0.0.1"), 10005});
  limited.configuredMaxPayload(Zutcp::BufSize);
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

  path.configuredMaxPayload(Zutcp::BufSize);
  path.peerMaxPayload(Zutcp::BufSize);
  path.startProbe(1400);
  path.probeAckd();
  ZuCHECK(path.activeMaxPayload() == 1400, "PMTUD probe success not applied");
  path.applyHint({Zutcp::PathHintKind::PktTooBig, 1300, 0});
  ZuCHECK(path.activeMaxPayload() == 1300 &&
      path.ceiling() == 1300 &&
      path.failureFloor() == 1300,
    "PMTU lower hint not applied");
  path.blackhole();
  ZuCHECK(path.activeMaxPayload() == Zutcp::MinPktPayload,
    "blackhole fallback mismatch");
  ZuCHECK(path.ecnDisabled(), "ECN should remain disabled in first release");

  Zutcp::Path blackhole = Zutcp::Path::client(
    ZiSockAddr{ZiIP("127.0.0.1"), 10008},
    ZiSockAddr{ZiIP("127.0.0.1"), 10009});
  blackhole.validated();
  blackhole.startProbe(1400);
  blackhole.probeAckd();
  ZuCHECK(blackhole.activeMaxPayload() == 1400,
    "blackhole setup did not grow active payload");
  blackhole.startProbe(1400);
  ZuCHECK(blackhole.probeExpired() &&
      blackhole.startNextProbe(64) &&
      blackhole.probeExpired() &&
      blackhole.startNextProbe(64) &&
      blackhole.probeAttempts() == Zutcp::Path::MaxProbeAttempts,
    "blackhole retry sequence mismatch");
  ZuCHECK(!blackhole.probeExpired() &&
      blackhole.activeMaxPayload() == Zutcp::MinPktPayload &&
      blackhole.pmtudState() == Zutcp::PMTUDState::Error &&
      blackhole.diag().blackholes == 1,
    "repeated active-size probe expiry did not trigger blackhole fallback");
  for (bool v6 : {false, true}) {
    auto limit = Zutcp::tcpPayload(1500, v6 ? ZiIPType::V6 : ZiIPType::V4);
    auto native = Zutcp::Path::client({}, {});
    native.validated();
    native.configuredMaxPayload(Zutcp::BufSize);
    native.peerMaxPayload(Zutcp::BufSize);
    native.routePayload(limit);
    ZuCHECK(native.ceiling() == limit && native.startNextProbe(4096) &&
        native.probeSize() == limit, "cold route caps probing at the actual TCP payload budget");
    native.probeAckd();
    native.peerMaxPayload(Zutcp::BufSize);
    ZuCHECK(native.activeMaxPayload() == limit && native.ceiling() == limit &&
        native.canSend(limit) && !native.canSend(limit + 1),
      "later peer limits cannot reopen capacity beyond IP/TCP geometry");
  }

}

void testPathECN()
{
  ZuTestScope(testPathECN);

  Zutcp::Path path = Zutcp::Path::client(
    ZiSockAddr{ZiIP("127.0.0.1"), 10100},
    ZiSockAddr{ZiIP("127.0.0.1"), 10101});
  ZuCHECK(path.ecnDisabled(), "new path should default ECN disabled");
  ZuCHECK(path.ecnState() == Zutcp::PathECNState::Disabled,
    "new path ECN state mismatch");
  ZuCHECK(path.activeECN() == Zutcp::EcnMark::NotECT,
    "new path active ECN mismatch");
  ZuCHECK(path.txECN(Zutcp::PktNumSpace::AppData, true, false) ==
      Zutcp::EcnMark::NotECT,
    "disabled path unexpectedly selected ECN mark");

  path.ecn(true);
  ZuCHECK(!path.ecnDisabled() &&
      path.ecnState() == Zutcp::PathECNState::Testing &&
      path.activeECN() == Zutcp::EcnMark::ECT0,
    "enabled path did not enter ECN testing");
  ZuCHECK(path.txECN(Zutcp::PktNumSpace::AppData, true, false) ==
      Zutcp::EcnMark::ECT0,
    "testing AppData packet did not select ECT0");
  ZuCHECK(path.txECN(Zutcp::PktNumSpace::Initial, true, false) ==
      Zutcp::EcnMark::NotECT,
    "Initial packet should remain NotECT");
  ZuCHECK(path.txECN(Zutcp::PktNumSpace::AppData, false, false) ==
      Zutcp::EcnMark::NotECT,
    "non-ack-eliciting packet should remain NotECT");
  ZuCHECK(path.txECN(Zutcp::PktNumSpace::AppData, true, true) ==
      Zutcp::EcnMark::NotECT,
    "PMTUD probe should remain NotECT");

  path.ecnCapable();
  ZuCHECK(path.ecnState() == Zutcp::PathECNState::Capable &&
      path.txECN(Zutcp::PktNumSpace::AppData, true, false) ==
	Zutcp::EcnMark::ECT0,
    "capable path did not keep ECT0 active");
  path.failECN();
  ZuCHECK(path.ecnDisabled() &&
      path.ecnState() == Zutcp::PathECNState::Failed &&
      path.activeECN() == Zutcp::EcnMark::NotECT &&
      path.txECN(Zutcp::PktNumSpace::AppData, true, false) ==
	Zutcp::EcnMark::NotECT,
    "failed path did not disable ECN marking");
  path.ecn(false);
  ZuCHECK(path.ecnState() == Zutcp::PathECNState::Disabled,
    "explicit ECN disable state mismatch");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testPMTUD);
  ZuTestCall(testPathECN);
}
