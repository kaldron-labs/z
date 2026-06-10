//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZquicConn.hh>

using namespace ZuTestUtil;

void testCxnIDRouter()
{
  ZuTestScope(testCxnIDRouter);

  Zquic::CxnIDRouter router;
  Zquic::CxnID cid{"server01"};
  ZuCHECK(router.add(cid, 0, 42), "CID add failed");
  ZuCHECK(router.find(cid) == 42 && router.active() == 1,
    "CID lookup mismatch");
  ZuCHECK(router.retire(cid), "CID retire failed");
  ZuCHECK(!router.find(cid) && router.state(cid) == Zquic::CxnIDState::Retired,
    "retired CID still active");
  ZuCHECK(router.add(cid, 1, 43), "CID reactivate failed");
  ZuCHECK(router.find(cid) == 43, "reactivated CID mismatch");
  ZuCHECK(router.tombstone(cid), "CID tombstone failed");
  ZuCHECK(!router.add(cid, 2, 44), "tombstoned CID reactivated");
  ZuCHECK(router.state(cid) == Zquic::CxnIDState::Tombstone,
    "CID tombstone state mismatch");
}

void testStatelessReset()
{
  ZuTestScope(testStatelessReset);

  Zquic::StatelessResetToken resetToken{"0123456789abcdef"};
  ZuCHECK(resetToken.valid() &&
      resetToken.length() == Zquic::StatelessResetToken::Length,
    "stateless reset token setup failed");

  Zquic::CxnIDRouter router;
  Zquic::CxnID cid{"server01"};
  ZuCHECK(router.add(cid, 0, 42, resetToken),
    "CID add with stateless reset token failed");
  Zquic::StatelessResetToken found;
  ZuCHECK(router.resetToken(cid, found) && found == resetToken,
    "CID stateless reset token lookup failed");
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
      out + n - Zquic::StatelessResetToken::Length,
      resetToken.data(), Zquic::StatelessResetToken::Length),
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
  Zquic::StatelessResetToken invalid;
  ZuCHECK(Zquic::StatelessReset::writeForUnknownCID(
      out, sizeof(out), ZuCSpan{reinterpret_cast<const char *>(packet), 64},
      invalid) < 0, "invalid token produced stateless reset");
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

  Zquic::LongHeader initial;
  initial.type = Zquic::PacketType::Initial;
  initial.version = Zquic::Version1;
  initial.dcid = "client-dc";
  initial.scid = "client-sc";

  Zquic::ServerBootstrap server;
  ZuCHECK(!server.acceptInitial(
      initial, Zquic::MinUDPPayload - 1, 42),
    "undersized Initial datagram was accepted");
  ZuCHECK(server.acceptInitial(initial, Zquic::MinUDPPayload, 42),
    "server Initial accept failed");
  ZuCHECK(server.accepted() &&
      server.originalDCID() == initial.dcid &&
      server.clientInitialSCID() == initial.scid &&
      server.localInitialSCID().length() >= Zquic::CxnIDGen::InitialLength,
    "server Initial CID state mismatch");
  ZuCHECK(server.initialDCIDs().find(initial.dcid) == 42 &&
      server.localCIDs().find(server.localInitialSCID()) == 42,
    "server Initial routing table mismatch");
  Zquic::StatelessResetToken resetToken;
  ZuCHECK(server.localCIDs().resetToken(server.localInitialSCID(), resetToken) &&
      resetToken == server.statelessResetToken(),
    "server Initial stateless reset token routing mismatch");

  Zquic::TransportParams params;
  ZuCHECK(server.transportParams(params) &&
      params.originalDCID == initial.dcid &&
      params.initialSCID == server.localInitialSCID(),
    "server transport parameter fill failed");

  Zquic::ClientBootstrap client;
  ZuCHECK(client.start(initial.dcid, initial.scid) &&
      client.validateServerTransportParams(params, server.localInitialSCID()),
    "client rejected server Initial transport parameters");
  ZuCHECK(!server.acceptInitial(initial, Zquic::MinUDPPayload, 43),
    "server accepted a second Initial");

  Zquic::LongHeader bad = initial;
  bad.version = 0xff00001dU;
  Zquic::ServerBootstrap badServer;
  ZuCHECK(!badServer.acceptInitial(bad, Zquic::MinUDPPayload, 42),
    "unsupported Initial version was accepted");
  bad = initial;
  bad.scid = "short";
  ZuCHECK(!badServer.acceptInitial(bad, Zquic::MinUDPPayload, 42),
    "short client SCID was accepted");
  bad = initial;
  bad.type = Zquic::PacketType::Handshake;
  ZuCHECK(!badServer.acceptInitial(bad, Zquic::MinUDPPayload, 42),
    "non-Initial packet was accepted");
  ZuCHECK(!badServer.acceptInitial(initial, Zquic::MinUDPPayload, 0),
    "zero route token was accepted");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testCxnIDRouter);
  ZuTestCall(testStatelessReset);
  ZuTestCall(testCxnIDGen);
  ZuTestCall(testServerInitialBootstrap);
}
