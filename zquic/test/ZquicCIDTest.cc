//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/Zquic.hh>

using namespace ZuTestUtil;

#define ZQUIC_CHECK_RT(x, ...) ZuCheckRT(x, log_(__VA_ARGS__))


struct TestRouteLink { };



void testFixedProtocolValues()
{
  ZuTestScope(testFixedProtocolValues);

  Zquic::ResetToken token{"0123456789abcdef"};
  Zquic::ResetToken saved = token;
  uint8_t shortToken[Zquic::ResetToken::Length - 1] = {};
  ZuCHECK(!token.set(ZuBSpan{shortToken, sizeof(shortToken)}) &&
      token == saved,
    "invalid reset token input modified the value");

  uint8_t zeroToken[Zquic::ResetToken::Length] = {};
  Zquic::ResetToken zero{ZuBSpan{zeroToken, sizeof(zeroToken)}};
  ZuCHECK(zero.valid() && zero.length() == sizeof(zeroToken) &&
      !memcmp(zero.data(), zeroToken, sizeof(zeroToken)),
    "all-zero reset token was not retained as valid");

  Zquic::PathChallenge challenge{"12345678"};
  Zquic::PathChallenge savedChallenge = challenge;
  uint8_t shortChallenge[Zquic::PathChallenge::Length - 1] = {};
  ZuCHECK(!challenge.set(
	ZuBSpan{shortChallenge, sizeof(shortChallenge)}) &&
      challenge == savedChallenge,
    "invalid path challenge input modified the value");

  uint8_t zeroChallenge[Zquic::PathChallenge::Length] = {};
  Zquic::PathChallenge zeroPath{
    ZuBSpan{zeroChallenge, sizeof(zeroChallenge)}};
  ZuCHECK(zeroPath.valid() &&
      zeroPath.length() == sizeof(zeroChallenge) &&
      zeroPath.equals(ZuBSpan{zeroChallenge, sizeof(zeroChallenge)}),
    "all-zero path challenge was not retained as valid");
}

void testCxnRouter()
{
  ZuTestScope(testCxnRouter);

  TestRouteLink link1;
  TestRouteLink link2;
  Zquic::CxnRouter<TestRouteLink> router;
  Zquic::CxnID cid{"server01"};
  Zquic::ResetToken first{"0123456789abcdef"}, conflict{"fedcba9876543210"}, saved;
  ZuCHECK(router.add(cid, 0, &link1, first), "CID add failed");
  ZuCHECK(!router.add(cid, 1, &link2, conflict) && router.find(cid) == &link1 &&
      router.resetToken(cid, saved) && saved == first && router.count() == 1,
    "conflicting binding replaced an active CID owner or token");
  uint64_t seq = Zquic::U64Null;
  router.all([&seq](const auto &entry) { seq = entry.sequence; });
  ZuCHECK(!seq, "conflicting binding changed the active route sequence");
  ZuCHECK(router.find(cid) == &link1 && router.active() == 1,
    "CID lookup mismatch");
  ZuCHECK(router.retire(cid), "CID retire failed");
  ZuCHECK(!router.find(cid) && !router.count(),
    "retired CID was retained");
  ZuCHECK(router.add(cid, 1, &link2), "CID reactivate failed");
  ZuCHECK(router.find(cid) == &link2, "reactivated CID mismatch");
  ZuCHECK(router.tombstone(cid), "CID tombstone failed");
  ZuCHECK(!router.add(cid, 2, &link1), "tombstoned CID reactivated");
  ZuCHECK(router.state(cid) == Zquic::CxnState::Tombstone,
    "CID tombstone state mismatch");
}

void testPreferredCIDRoutes()
{
  ZuTestScope(testPreferredCIDRoutes);

  TestRouteLink first;
  TestRouteLink second;
  Zquic::CxnRouter<TestRouteLink> routes;
  Zquic::PreferredAddress preferred[2];
  for (unsigned i = 0; i < 2; ++i) {
    preferred[i].ipv4 = {ZiIP{"127.0.0.1"}, 8443};
    preferred[i].ipv6 = {ZiIP{"::1"}, 8443};
    ZuCHECK(Zquic::CxnIDGen::random(preferred[i].cid) &&
	preferred[i].resetToken.generate(),
      "preferred CID/token generation failed");
  }
  ZuCHECK(preferred[0].cid != preferred[1].cid &&
      preferred[0].resetToken != preferred[1].resetToken,
    "connections shared a preferred CID or reset token");
  ZuCHECK(routes.add(preferred[0].cid, 1, &first, preferred[0].resetToken) &&
      routes.add(preferred[1].cid, 1, &second, preferred[1].resetToken),
    "per-connection sequence-1 routes failed");
  uint8_t packet[64] = {};
  int n = Zquic::Pkt::writeShort(packet, sizeof(packet), preferred[0].cid, 0, 2);
  Zquic::CxnID matched;
  ZuCHECK(n > 0 && routes.matchShort(Zquic::byteSpan(packet, unsigned(n)), &matched) ==
      &first && matched == preferred[0].cid,
    "preferred CID did not route to its original connection");
  Zquic::ResetToken reset;
  ZuCHECK(routes.resetToken(matched, reset) && reset == preferred[0].resetToken,
    "advertised reset token differed from the registered route");
  ZuCHECK(routes.tombstone(preferred[0].cid) && !routes.find(preferred[0].cid) &&
      routes.find(preferred[1].cid) == &second,
    "preferred retirement disrupted another connection");
}


void testCxnRouterTombstoneFIFO()
{
  ZuTestScopeRT(testCxnRouterTombstoneFIFO);

  TestRouteLink link;
  Zquic::CxnRouter<TestRouteLink> router;
  Zquic::CxnID active{"active01"};
  ZQUIC_CHECK_RT(router.add(active, 0, &link), "active CID add failed");

  char tomb[9] = "tmb00000";
  for (unsigned i = 0; i < Zquic::CxnRouter<TestRouteLink>::TombstoneMax + 4;
      ++i) {
    tomb[3] = char('0' + ((i / 1000) % 10));
    tomb[4] = char('0' + ((i / 100) % 10));
    tomb[5] = char('0' + ((i / 10) % 10));
    tomb[6] = char('0' + (i % 10));
    Zquic::CxnID cid{ZuBSpan{tomb, 8}};
    ZQUIC_CHECK_RT(router.tombstone(cid), "tombstone route setup failed");
  }
  Zquic::CxnID oldTombstone{"tmb00000"};
  ZQUIC_CHECK_RT(router.add(oldTombstone, 1000, &link),
    "oldest FIFO-evicted tombstone was retained");
  char kept[9] = "tmb00000";
  kept[6] = '4';
  Zquic::CxnID newTombstone{ZuBSpan{kept, 8}};
  ZQUIC_CHECK_RT(!router.add(newTombstone, 1001, &link),
    "retained tombstone was reactivated");
  ZQUIC_CHECK_RT(router.find(active) == &link && router.active() == 2,
    "tombstone FIFO disturbed active routes");
  ZQUIC_CHECK_RT(router.tombstoneCount() == Zquic::CxnRouter<TestRouteLink>::TombstoneMax,
    "tombstone FIFO exceeded retention bound");
}

void testStatelessReset()
{
  ZuTestScope(testStatelessReset);

  Zquic::ResetToken resetToken{"0123456789abcdef"};
  ZuCHECK(resetToken.valid() &&
      resetToken.length() == Zquic::ResetToken::Length,
    "stateless reset token setup failed");

  TestRouteLink link;
  Zquic::CxnRouter<TestRouteLink> router;
  Zquic::CxnID cid{"server01"};
  ZuCHECK(router.add(cid, 0, &link, resetToken),
    "CID add with stateless reset token failed");
  Zquic::ResetToken found;
  ZuCHECK(router.resetToken(cid, found) && found == resetToken,
    "CID stateless reset token lookup failed");
  ZuCHECK(router.retire(cid) && !router.resetToken(cid, found),
    "retired CID kept stateless reset token");
  ZuCHECK(router.add(cid, 1, &link, resetToken) && router.tombstone(cid) &&
      !router.resetToken(cid, found),
    "tombstoned CID kept stateless reset token");

  uint8_t packet[64] = {};
  packet[0] = 0x40;
  uint8_t out[64] = {};
  int n = Zquic::StatelessReset::writeForUnknownCID(
    out, sizeof(out), ZuBSpan{packet, 64},
    resetToken);
  ZuCHECK(n == 63, "stateless reset size mismatch");
  ZuCHECK(!(out[0] & 0x80) && (out[0] & 0x40),
    "stateless reset first byte is not short-header-like");
  ZuCHECK(!memcmp(
      out + n - Zquic::ResetToken::Length,
      resetToken.data(), Zquic::ResetToken::Length),
    "stateless reset token suffix mismatch");

  packet[0] = 0xc0;
  ZuCHECK(Zquic::StatelessReset::writeForUnknownCID(
      out, sizeof(out), ZuBSpan{packet, 64},
      resetToken) < 0, "long-header packet produced stateless reset");
  packet[0] = 0x40;
  ZuCHECK(Zquic::StatelessReset::writeForUnknownCID(
      out, sizeof(out),
      ZuBSpan{packet, unsigned(Zquic::StatelessReset::MinLength)},
      resetToken) < 0, "too-short packet produced stateless reset");
  Zquic::ResetToken invalid;
  ZuCHECK(Zquic::StatelessReset::writeForUnknownCID(
      out, sizeof(out), ZuBSpan{packet, 64},
      invalid) < 0, "invalid token produced stateless reset");
}

void variableShortCIDRouteMatch()
{
  ZuTestScope(variableShortCIDRouteMatch);

  TestRouteLink shortLink;
  TestRouteLink longLink;
  Zquic::CxnRouter<TestRouteLink> router;
  Zquic::CxnID shortCID{"server01"};
  Zquic::CxnID longCID{"server019"};
  Zquic::ResetToken token{"0123456789abcdef"};

  ZuCHECK(router.add(shortCID, 0, &shortLink),
    "short CID route add failed");
  ZuCHECK(router.add(longCID, 1, &longLink, token),
    "long CID route add failed");

  uint8_t packet[64] = {};
  packet[0] = 0x40;
  memcpy(packet + 1, longCID.data(), longCID.length());
  ZuBSpan datagram{packet, sizeof(packet)};

  Zquic::CxnID matched;
#ifdef ZDEBUG
  router.resetShortProbes();
#endif
  ZuCHECK(router.matchShort(datagram, &matched) == &longLink &&
      matched == longCID,
    "known-length short-header route did not select longest matching CID");
#ifdef ZDEBUG
  ZuCHECK(router.shortProbes() == 1,
    "longest installed CID was not matched with one hash probe");
  router.resetShortProbes();
#endif
  Zquic::ResetToken found;
  ZuCHECK(router.resetTokenForShort(datagram, found) && found == token,
    "known-length short-header reset token lookup failed");
#ifdef ZDEBUG
  ZuCHECK(router.shortProbes() == 1,
    "reset-token route lookup repeated CID-length probes");
  packet[1] ^= 0xff;
  router.resetShortProbes();
  ZuCHECK(!router.matchShort(datagram) && router.shortProbes() == 2,
    "unknown short CID did not probe exactly the installed lengths");
  router.clear();
  router.resetShortProbes();
  ZuCHECK(!router.matchShort(datagram) && !router.shortProbes(),
    "cleared router retained active CID lengths");
#endif
}

void testAllShortCIDLengths()
{
  ZuTestScopeRT(testAllShortCIDLengths);

  TestRouteLink links[Zquic::CxnIDMax];
  uint8_t cidBytes[Zquic::CxnIDMax];
  uint8_t packet[Zquic::CxnIDMax + 8] = {};
  packet[0] = 0x40;
  for (unsigned i = 0; i < Zquic::CxnIDMax; ++i) {
    cidBytes[i] = uint8_t(i + 1);
    packet[i + 1] = cidBytes[i];
  }
  Zquic::CxnRouter<TestRouteLink> router;
  for (unsigned length = 1; length <= Zquic::CxnIDMax; ++length)
    ZQUIC_CHECK_RT(router.add(
	Zquic::CxnID{ZuBSpan{cidBytes, length}}, length, &links[length - 1]),
      "CID length route add failed");
  ZuBSpan datagram{packet, sizeof(packet)};
  for (unsigned length = Zquic::CxnIDMax; length; --length) {
#ifdef ZDEBUG
    router.resetShortProbes();
#endif
    ZQUIC_CHECK_RT(router.matchShort(datagram) == &links[length - 1],
      "CID prefix routing did not choose longest installed length");
#ifdef ZDEBUG
    ZQUIC_CHECK_RT(router.shortProbes() == 1,
      "CID prefix routing probed an uninstalled length");
#endif
    ZQUIC_CHECK_RT(router.retire(Zquic::CxnID{ZuBSpan{cidBytes, length}}),
      "CID length route retire failed");
  }
#ifdef ZDEBUG
  router.resetShortProbes();
  ZQUIC_CHECK_RT(!router.matchShort(datagram) && !router.shortProbes(),
    "retired CID lengths remained in the active-length index");
#endif
}

void testCxnIDGen()
{
  ZuTestScope(testCxnIDGen);

  Zquic::CxnID dcid;
  Zquic::CxnID scid;
  ZuCHECK(Zquic::CxnIDGen::randomPair(dcid, scid),
    "random CID pair generation failed");
  ZuCHECK(dcid.length() == Zquic::CxnIDGen::InitialLength &&
      scid.length() == Zquic::CxnIDGen::InitialLength,
    "random CID default length mismatch");

  Zquic::CxnID max;
  ZuCHECK(Zquic::CxnIDGen::random(max, Zquic::CxnIDMax),
    "max-length random CID generation failed");
  ZuCHECK(max.length() == Zquic::CxnIDMax,
    "max-length random CID length mismatch");

  Zquic::ClientBootstrap bootstrap;
  ZuCHECK(bootstrap.startRandom(), "random bootstrap start failed");
  ZuCHECK(bootstrap.started() &&
      bootstrap.initialDCID().length() >= Zquic::CxnIDGen::InitialLength &&
      bootstrap.initialSCID().length() >= Zquic::CxnIDGen::InitialLength,
    "random bootstrap CID state mismatch");
  ZuCHECK(!bootstrap.startRandom(7, Zquic::CxnIDGen::InitialLength),
    "short random DCID was accepted");
  ZuCHECK(!bootstrap.startRandom(
      Zquic::CxnIDGen::InitialLength, Zquic::CxnIDMax + 1),
    "oversize random SCID was accepted");
}

void testServerInitialBootstrap()
{
  ZuTestScope(testServerInitialBootstrap);

  Zquic::LongHdr initial;
  initial.type = Zquic::PktType::Initial;
  initial.version = Zquic::Version1;
  initial.dcid = "client-dc";
  initial.scid = "client-sc";

  Zquic::ServerBootstrap server;
  ZuCHECK(!server.acceptInitial(
      initial, Zquic::MinUDPPayload - 1),
    "undersized Initial datagram was accepted");
  ZuCHECK(server.acceptInitial(initial, Zquic::MinUDPPayload),
    "server Initial accept failed");
  ZuCHECK(server.accepted() &&
      server.origDCID() == initial.dcid &&
      server.clientInitialSCID() == initial.scid &&
      server.localInitialSCID().length() >= Zquic::CxnIDGen::InitialLength,
    "server Initial CID state mismatch");
  ZuCHECK(server.statelessResetToken().valid(),
    "server Initial stateless reset token generation failed");

  Zquic::TransportParams params;
  ZuCHECK(server.transportParams(params) &&
      params.origDCID == initial.dcid &&
      params.initialSCID == server.localInitialSCID(),
    "server transport parameter fill failed");

  Zquic::ClientBootstrap client;
  ZuCHECK(client.start(initial.dcid, initial.scid) &&
      client.validateServerParams(params, server.localInitialSCID()),
    "client rejected server Initial transport parameters");
  ZuCHECK(!server.acceptInitial(initial, Zquic::MinUDPPayload),
    "server accepted a second Initial");

  Zquic::LongHdr bad = initial;
  bad.version = 0xff00001dU;
  Zquic::ServerBootstrap badServer;
  ZuCHECK(!badServer.acceptInitial(bad, Zquic::MinUDPPayload),
    "unsupported Initial version was accepted");
  bad = initial;
  bad.dcid = "short";
  ZuCHECK(!badServer.acceptInitial(bad, Zquic::MinUDPPayload),
    "short client DCID was accepted");
  bad = initial;
  bad.type = Zquic::PktType::Handshake;
  ZuCHECK(!badServer.acceptInitial(bad, Zquic::MinUDPPayload),
    "non-Initial packet was accepted");

  Zquic::LongHdr zeroSCID = initial;
  zeroSCID.scid.null();
  Zquic::ServerBootstrap zeroSCIDServer;
  ZuCHECK(zeroSCIDServer.acceptInitial(zeroSCID, Zquic::MinUDPPayload),
    "zero-length client SCID was rejected");
  ZuCHECK(!zeroSCIDServer.clientInitialSCID(),
    "zero-length client SCID was not preserved");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testFixedProtocolValues);
  ZuTestCall(testCxnRouter);
  ZuTestCall(testPreferredCIDRoutes);
  ZuTestCall(testCxnRouterTombstoneFIFO);
  ZuTestCall(testStatelessReset);
  ZuTestCall(variableShortCIDRouteMatch);
  ZuTestCall(testAllShortCIDLengths);
  ZuTestCall(testCxnIDGen);
  ZuTestCall(testServerInitialBootstrap);
}
