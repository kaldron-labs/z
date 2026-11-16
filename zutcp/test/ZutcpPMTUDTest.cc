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
  ZuCHECK(path.activeMaxPayload() == Zutcp::BufSize && path.canSend(1200),
    "native path starts from a bounded configured cap, without an Initial floor");
  path.received(500);
  ZuCHECK(path.reserveSend(1200) && path.diag().bytesRx == 500 &&
      path.diag().bytesTx == 1200, "path byte accounting follows accepted traffic");
  path.configuredMaxPayload(1200);
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
  loss.configuredMaxPayload(1200);
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
  retry.configuredMaxPayload(1200);
  retry.configuredMaxPayload(Zutcp::BufSize);
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
  ZuCHECK(path.activeMaxPayload() == 536,
    "blackhole fallback mismatch");
  ZuCHECK(path.ecnDisabled(), "ECN should remain disabled in first release");

  Zutcp::Path blackhole = Zutcp::Path::client(
    ZiSockAddr{ZiIP("127.0.0.1"), 10008},
    ZiSockAddr{ZiIP("127.0.0.1"), 10009});
  blackhole.validated();
  blackhole.configuredMaxPayload(1400);
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
      blackhole.activeMaxPayload() == 536 &&
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
    ZuCHECK(native.ceiling() == limit && native.activeMaxPayload() == limit &&
	!native.nextProbeSize(4096),
      "known route sets the TCP payload budget without a setup-floor probe");
    native.peerMaxPayload(Zutcp::BufSize);
    ZuCHECK(native.activeMaxPayload() == limit && native.ceiling() == limit &&
	native.canSend(limit) && !native.canSend(limit + 1),
      "later peer limits cannot reopen capacity beyond IP/TCP geometry");
  }

}

void testSmallTCPRoute()
{
  ZuTestScope(testSmallTCPRoute);
  auto path = Zutcp::Path::client({}, {});
  path.configuredMaxPayload(1000);
  path.peerMaxPayload(536);
  path.routePayload(900);
  path.validated();
  ZuCHECK(path.activeMaxPayload() == 536 && path.ceiling() == 536,
    "MSS below the configured cap remains a legal positive segment bound");
  ZuCHECK(path.canSendProbe(536) && !path.canSendProbe(537),
    "probe admission observes actual smaller ceiling");
  path.applyHint({Zutcp::PathHintKind::SendTooBig, 400, 0});
  ZuCHECK(path.activeMaxPayload() == 400 && path.ceiling() == 400,
    "kernel payload hint below the configured cap updates the active path");
  auto route = Zutcp::Path::client({}, {});
  route.configuredMaxPayload(1400);
  route.routePayload(1360);
  route.useRoutePayload();
  ZuCHECK(route.activeMaxPayload() == 1360 && !route.nextProbeSize(),
    "known native route cap requires no active sizing probe");
}

void testPathECN()
{
  ZuTestScope(testPathECN);
  auto path = Zutcp::Path::client({}, {});
  ZuCHECK(path.ecnDisabled() && path.activeECN() == Zutcp::EcnMark::NotECT,
    "classic TCP ECN is absent until negotiation enables the path");
  path.ecn(true);
  path.ecnCapable();
  path.ceECN(100);
  ZuCHECK(!path.ecnDisabled() && path.activeECN() == Zutcp::EcnMark::ECT0 &&
      path.diag().ecnCEEvents == 1 && path.diag().ecnCEBytes == 100,
    "accepted classic CE observations preserve byte accounting");
  path.failECN();
  path.failECN();
  ZuCHECK(path.ecnDisabled() && path.activeECN() == Zutcp::EcnMark::NotECT &&
      path.diag().ecnFailures == 1, "disabling ECN is idempotent");
  path.ecn(false);
  ZuCHECK(path.ecnState() == Zutcp::PathECNState::Disabled,
    "explicit absent ECN policy clears the capable state");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testPMTUD);
  ZuTestCall(testSmallTCPRoute);
  ZuTestCall(testPathECN);
}
