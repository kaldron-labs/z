//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/Zquic.hh>

using namespace ZuTestUtil;

namespace {

struct TestRouteLink { };

} // namespace

void testCxnRouter()
{
  ZuTestScope(testCxnRouter);

  TestRouteLink link1;
  TestRouteLink link2;
  Zquic::CxnRouter<TestRouteLink> router;
  Zquic::CxnID cid{"server01"};
  ZuCHECK(router.add(cid, 0, &link1), "CID add failed");
  ZuCHECK(router.find(cid) == &link1 && router.active() == 1,
    "CID lookup mismatch");
  ZuCHECK(router.retire(cid), "CID retire failed");
  ZuCHECK(!router.find(cid) && router.state(cid) == Zquic::CxnState::Retired,
    "retired CID still active");
  ZuCHECK(router.add(cid, 1, &link2), "CID reactivate failed");
  ZuCHECK(router.find(cid) == &link2, "reactivated CID mismatch");
  ZuCHECK(router.tombstone(cid), "CID tombstone failed");
  ZuCHECK(!router.add(cid, 2, &link1), "tombstoned CID reactivated");
  ZuCHECK(router.state(cid) == Zquic::CxnState::Tombstone,
    "CID tombstone state mismatch");
}

void testCxnRouterGC()
{
  ZuTestScope(testCxnRouterGC);

  TestRouteLink link;
  Zquic::CxnRouter<TestRouteLink> router;
  Zquic::CxnID active{"active01"};
  ZuCHECK(router.add(active, 0, &link), "active CID add failed");

  char id[9] = "ret00000";
  for (unsigned i = 0; i < Zquic::CxnRouter<TestRouteLink>::RetiredMax + 4;
      ++i) {
    id[3] = char('0' + ((i / 1000) % 10));
    id[4] = char('0' + ((i / 100) % 10));
    id[5] = char('0' + ((i / 10) % 10));
    id[6] = char('0' + (i % 10));
    Zquic::CxnID cid{ZuCSpan{id, 8}};
    ZuCHECK(router.add(cid, i + 1, &link), "retired route add failed");
    ZuCHECK(router.retire(cid), "retired route setup failed");
  }
  ZuCHECK(router.find(active) == &link && router.active() == 1,
    "route GC disturbed active route");
  Zquic::CxnID oldRetired{"ret00000"};
  ZuCHECK(router.state(oldRetired) == Zquic::CxnState::Tombstone,
    "oldest retired route was not GCed");

  char tomb[9] = "tmb00000";
  for (unsigned i = 0; i < Zquic::CxnRouter<TestRouteLink>::TombstoneMax + 4;
      ++i) {
    tomb[3] = char('0' + ((i / 1000) % 10));
    tomb[4] = char('0' + ((i / 100) % 10));
    tomb[5] = char('0' + ((i / 10) % 10));
    tomb[6] = char('0' + (i % 10));
    Zquic::CxnID cid{ZuCSpan{tomb, 8}};
    ZuCHECK(router.tombstone(cid), "tombstone route setup failed");
  }
  Zquic::CxnID oldTombstone{"tmb00000"};
  ZuCHECK(router.state(oldTombstone) == Zquic::CxnState::Tombstone,
    "GCed tombstone should read as tombstoned");
  ZuCHECK(router.count() <=
      1 + Zquic::CxnRouter<TestRouteLink>::RetiredMax +
      Zquic::CxnRouter<TestRouteLink>::TombstoneMax,
    "route GC did not bound inactive routes");
}

void testCxnRouterExpiryGC()
{
  ZuTestScope(testCxnRouterExpiryGC);

  TestRouteLink link;
  Zquic::CxnRouter<TestRouteLink> router;
  Zquic::CxnID cid{"expire01"};

  ZuCHECK(router.add(cid, 1, &link), "expiring route add failed");
  ZuCHECK(router.retire(cid, Zquic::timeUS(4000)),
    "expiring route retire failed");
  ZuCHECK(!router.gcRoutes(Zquic::timeUS(3000)) &&
      router.state(cid) == Zquic::CxnState::Retired,
    "retired route expired before deadline");
  ZuCHECK(router.gcRoutes(Zquic::timeUS(4000)) == 1 &&
      router.state(cid) == Zquic::CxnState::Tombstone,
    "retired route did not expire at deadline");
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
  ZuCHECK(router.retire(cid) && router.resetToken(cid, found) &&
      found == resetToken,
    "retired CID did not retain stateless reset token");
  ZuCHECK(router.tombstone(cid) && !router.resetToken(cid, found),
    "tombstoned CID kept stateless reset token");

  uint8_t packet[64] = {};
  packet[0] = 0x40;
  uint8_t out[64] = {};
  int n = Zquic::StatelessReset::writeForUnknownCID(
    out, sizeof(out), ZuCSpan{reinterpret_cast<const char *>(packet), 64},
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
      out, sizeof(out), ZuCSpan{reinterpret_cast<const char *>(packet), 64},
      resetToken) < 0, "long-header packet produced stateless reset");
  packet[0] = 0x40;
  ZuCHECK(Zquic::StatelessReset::writeForUnknownCID(
      out, sizeof(out),
      ZuCSpan{reinterpret_cast<const char *>(packet),
	unsigned(Zquic::StatelessReset::MinLength)},
      resetToken) < 0, "too-short packet produced stateless reset");
  Zquic::ResetToken invalid;
  ZuCHECK(Zquic::StatelessReset::writeForUnknownCID(
      out, sizeof(out), ZuCSpan{reinterpret_cast<const char *>(packet), 64},
      invalid) < 0, "invalid token produced stateless reset");
}

void testVariableShortCIDRouteMatch()
{
  ZuTestScope(testVariableShortCIDRouteMatch);

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
  ZuCSpan datagram{reinterpret_cast<const char *>(packet), sizeof(packet)};

  Zquic::CxnID matched;
  ZuCHECK(router.matchShort(datagram, &matched) == &longLink &&
      matched == longCID,
    "known-length short-header route did not select longest matching CID");
  Zquic::ResetToken found;
  ZuCHECK(router.resetTokenForShort(datagram, found) && found == token,
    "known-length short-header reset token lookup failed");
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
      server.originalDCID() == initial.dcid &&
      server.clientInitialSCID() == initial.scid &&
      server.localInitialSCID().length() >= Zquic::CxnIDGen::InitialLength,
    "server Initial CID state mismatch");
  ZuCHECK(server.statelessResetToken().valid(),
    "server Initial stateless reset token generation failed");

  Zquic::TransportParams params;
  ZuCHECK(server.transportParams(params) &&
      params.originalDCID == initial.dcid &&
      params.initialSCID == server.localInitialSCID(),
    "server transport parameter fill failed");

  Zquic::ClientBootstrap client;
  ZuCHECK(client.start(initial.dcid, initial.scid) &&
      client.validateServerTransportParams(params, server.localInitialSCID()),
    "client rejected server Initial transport parameters");
  ZuCHECK(!server.acceptInitial(initial, Zquic::MinUDPPayload),
    "server accepted a second Initial");

  Zquic::LongHdr bad = initial;
  bad.version = 0xff00001dU;
  Zquic::ServerBootstrap badServer;
  ZuCHECK(!badServer.acceptInitial(bad, Zquic::MinUDPPayload),
    "unsupported Initial version was accepted");
  bad = initial;
  bad.scid = "short";
  ZuCHECK(!badServer.acceptInitial(bad, Zquic::MinUDPPayload),
    "short client SCID was accepted");
  bad = initial;
  bad.type = Zquic::PktType::Handshake;
  ZuCHECK(!badServer.acceptInitial(bad, Zquic::MinUDPPayload),
    "non-Initial packet was accepted");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testCxnRouter);
  ZuTestCall(testCxnRouterGC);
  ZuTestCall(testCxnRouterExpiryGC);
  ZuTestCall(testStatelessReset);
  ZuTestCall(testVariableShortCIDRouteMatch);
  ZuTestCall(testCxnIDGen);
  ZuTestCall(testServerInitialBootstrap);
}
