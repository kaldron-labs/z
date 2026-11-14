//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/Zutcp.hh>

using namespace ZuTestUtil;

#define ZUTCP_CHECK_RT(x, ...) ZuCheckRT(x, log_(__VA_ARGS__))

namespace {

struct TestRouteLink { };

} // namespace



void testCxnRouter()
{
  ZuTestScope(testCxnRouter);

  TestRouteLink link1;
  TestRouteLink link2;
  Zutcp::CxnRouter<TestRouteLink> router;
  Zutcp::CxnID cid{"server01"};
  ZuCHECK(router.add(cid, &link1), "CID add failed");
  ZuCHECK(router.find(cid) == &link1 && router.active() == 1,
    "CID lookup mismatch");
  ZuCHECK(router.retire(cid), "CID retire failed");
  ZuCHECK(!router.find(cid) && !router.count(),
    "retired CID was retained");
  ZuCHECK(router.add(cid, &link2), "CID reactivate failed");
  ZuCHECK(router.find(cid) == &link2, "reactivated CID mismatch");
  ZuCHECK(router.tombstone(cid), "CID tombstone failed");
  ZuCHECK(!router.add(cid, &link1), "tombstoned CID reactivated");
  ZuCHECK(router.state(cid) == Zutcp::CxnState::Tombstone,
    "CID tombstone state mismatch");
}

void testCxnRouterTombstoneFIFO()
{
  ZuTestScopeRT(testCxnRouterTombstoneFIFO);

  TestRouteLink link;
  Zutcp::CxnRouter<TestRouteLink> router;
  Zutcp::CxnID active{"active01"};
  ZUTCP_CHECK_RT(router.add(active, &link), "active CID add failed");

  char tomb[9] = "tmb00000";
  for (unsigned i = 0; i < Zutcp::CxnRouter<TestRouteLink>::TombstoneMax + 4;
      ++i) {
    tomb[3] = char('0' + ((i / 1000) % 10));
    tomb[4] = char('0' + ((i / 100) % 10));
    tomb[5] = char('0' + ((i / 10) % 10));
    tomb[6] = char('0' + (i % 10));
    Zutcp::CxnID cid{ZuBSpan{tomb, 8}};
    ZUTCP_CHECK_RT(router.tombstone(cid), "tombstone route setup failed");
  }
  Zutcp::CxnID oldTombstone{"tmb00000"};
  ZUTCP_CHECK_RT(router.add(oldTombstone, &link),
    "oldest FIFO-evicted tombstone was retained");
  char kept[9] = "tmb00000";
  kept[6] = '4';
  Zutcp::CxnID newTombstone{ZuBSpan{kept, 8}};
  ZUTCP_CHECK_RT(!router.add(newTombstone, &link),
    "retained tombstone was reactivated");
  ZUTCP_CHECK_RT(router.find(active) == &link && router.active() == 2,
    "tombstone FIFO disturbed active routes");
  ZUTCP_CHECK_RT(router.tombstoneCount() == Zutcp::CxnRouter<TestRouteLink>::TombstoneMax,
    "tombstone FIFO exceeded retention bound");
}



void variableShortCIDRouteMatch()
{
  ZuTestScope(variableShortCIDRouteMatch);

  TestRouteLink shortLink;
  TestRouteLink longLink;
  Zutcp::CxnRouter<TestRouteLink> router;
  Zutcp::CxnID shortCID{"server01"};
  Zutcp::CxnID longCID{"server019"};

  ZuCHECK(router.add(shortCID, &shortLink),
    "short CID route add failed");
  ZuCHECK(router.add(longCID, &longLink),
    "long CID route add failed");

  uint8_t packet[64] = {};
  packet[0] = 0x40;
  memcpy(packet + 1, longCID.data(), longCID.length());
  ZuBSpan datagram{packet, sizeof(packet)};

  Zutcp::CxnID matched;
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
#ifdef ZDEBUG
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

  TestRouteLink links[Zutcp::CxnIDMax];
  uint8_t cidBytes[Zutcp::CxnIDMax];
  uint8_t packet[Zutcp::CxnIDMax + 8] = {};
  packet[0] = 0x40;
  for (unsigned i = 0; i < Zutcp::CxnIDMax; ++i) {
    cidBytes[i] = uint8_t(i + 1);
    packet[i + 1] = cidBytes[i];
  }
  Zutcp::CxnRouter<TestRouteLink> router;
  for (unsigned length = 1; length <= Zutcp::CxnIDMax; ++length)
    ZUTCP_CHECK_RT(router.add(
	Zutcp::CxnID{ZuBSpan{cidBytes, length}}, &links[length - 1]),
      "CID length route add failed");
  ZuBSpan datagram{packet, sizeof(packet)};
  for (unsigned length = Zutcp::CxnIDMax; length; --length) {
#ifdef ZDEBUG
    router.resetShortProbes();
#endif
    ZUTCP_CHECK_RT(router.matchShort(datagram) == &links[length - 1],
      "CID prefix routing did not choose longest installed length");
#ifdef ZDEBUG
    ZUTCP_CHECK_RT(router.shortProbes() == 1,
      "CID prefix routing probed an uninstalled length");
#endif
    ZUTCP_CHECK_RT(router.retire(Zutcp::CxnID{ZuBSpan{cidBytes, length}}),
      "CID length route retire failed");
  }
#ifdef ZDEBUG
  router.resetShortProbes();
  ZUTCP_CHECK_RT(!router.matchShort(datagram) && !router.shortProbes(),
    "retired CID lengths remained in the active-length index");
#endif
}

void testCxnIDGen()
{
  ZuTestScope(testCxnIDGen);

  Zutcp::CxnID dcid;
  Zutcp::CxnID scid;
  ZuCHECK(Zutcp::CxnIDGen::randomPair(dcid, scid),
    "random CID pair generation failed");
  ZuCHECK(dcid.length() == Zutcp::CxnIDGen::InitialLength &&
      scid.length() == Zutcp::CxnIDGen::InitialLength,
    "random CID default length mismatch");

  Zutcp::CxnID max;
  ZuCHECK(Zutcp::CxnIDGen::random(max, Zutcp::CxnIDMax),
    "max-length random CID generation failed");
  ZuCHECK(max.length() == Zutcp::CxnIDMax,
    "max-length random CID length mismatch");

  Zutcp::ClientBootstrap bootstrap;
  ZuCHECK(bootstrap.startRandom(), "random bootstrap start failed");
  ZuCHECK(bootstrap.started() &&
      bootstrap.initialDCID().length() >= Zutcp::CxnIDGen::InitialLength &&
      bootstrap.initialSCID().length() >= Zutcp::CxnIDGen::InitialLength,
    "random bootstrap CID state mismatch");
  ZuCHECK(!bootstrap.startRandom(7, Zutcp::CxnIDGen::InitialLength),
    "short random DCID was accepted");
  ZuCHECK(!bootstrap.startRandom(
      Zutcp::CxnIDGen::InitialLength, Zutcp::CxnIDMax + 1),
    "oversize random SCID was accepted");
}

void testServerInitialBootstrap()
{
  ZuTestScope(testServerInitialBootstrap);

  Zutcp::LongHdr initial;
  initial.type = Zutcp::PktType::Initial;
  initial.version = Zutcp::Version1;
  initial.dcid = "client-dc";
  initial.scid = "client-sc";

  Zutcp::ServerBootstrap server;
  ZuCHECK(!server.acceptInitial(
      initial, Zutcp::MinPktPayload - 1),
    "undersized Initial datagram was accepted");
  ZuCHECK(server.acceptInitial(initial, Zutcp::MinPktPayload),
    "server Initial accept failed");
  ZuCHECK(server.accepted() &&
      server.origDCID() == initial.dcid &&
      server.clientInitialSCID() == initial.scid &&
      server.localInitialSCID().length() >= Zutcp::CxnIDGen::InitialLength,
    "server Initial CID state mismatch");
  Zutcp::TransportParams params;
  ZuCHECK(server.transportParams(params) &&
      params.origDCID == initial.dcid &&
      params.initialSCID == server.localInitialSCID(),
    "server transport parameter fill failed");

  Zutcp::ClientBootstrap client;
  ZuCHECK(client.start(initial.dcid, initial.scid) &&
      client.validateServerParams(params, server.localInitialSCID()),
    "client rejected server Initial transport parameters");
  ZuCHECK(!server.acceptInitial(initial, Zutcp::MinPktPayload),
    "server accepted a second Initial");

  Zutcp::LongHdr bad = initial;
  bad.version = 0xff00001dU;
  Zutcp::ServerBootstrap badServer;
  ZuCHECK(!badServer.acceptInitial(bad, Zutcp::MinPktPayload),
    "unsupported Initial version was accepted");
  bad = initial;
  bad.dcid = "short";
  ZuCHECK(!badServer.acceptInitial(bad, Zutcp::MinPktPayload),
    "short client DCID was accepted");
  bad = initial;
  bad.type = Zutcp::PktType::Short;
  ZuCHECK(!badServer.acceptInitial(bad, Zutcp::MinPktPayload),
    "non-Initial packet was accepted");

  Zutcp::LongHdr zeroSCID = initial;
  zeroSCID.scid.null();
  Zutcp::ServerBootstrap zeroSCIDServer;
  ZuCHECK(zeroSCIDServer.acceptInitial(zeroSCID, Zutcp::MinPktPayload),
    "zero-length client SCID was rejected");
  ZuCHECK(!zeroSCIDServer.clientInitialSCID(),
    "zero-length client SCID was not preserved");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testCxnRouter);
  ZuTestCall(testCxnRouterTombstoneFIFO);
  ZuTestCall(variableShortCIDRouteMatch);
  ZuTestCall(testAllShortCIDLengths);
  ZuTestCall(testCxnIDGen);
  ZuTestCall(testServerInitialBootstrap);
}
