//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZquicConn.hh>

using namespace ZuTestUtil;

void testVersionNegotiation()
{
  ZuTestScope(testVersionNegotiation);

  uint32_t offered[] = { 0xff00001dU, Zquic::Version1 };
  ZuCHECK(Zquic::VersionNegotiation::choose(offered, 2) == Zquic::Version1,
    "version chooser did not select v1");
  ZuCHECK(!Zquic::VersionNegotiation::supported(0xff00001dU),
    "unsupported version accepted");

  Zquic::CxnID dcid{"client01"};
  Zquic::CxnID scid{"server01"};
  uint8_t b[128];
  int n = Zquic::VersionNegotiation::write(b, sizeof(b), dcid, scid);
  ZuCHECK(n > 0, "version negotiation write failed");

  uint32_t parsed[2] = {};
  unsigned nVersions = 0;
  ZuCHECK(!Zquic::VersionNegotiation::parse(
    ZuCSpan{reinterpret_cast<char *>(b), unsigned(n)},
    parsed, 2, nVersions), "version negotiation parse failed");
  ZuCHECK(nVersions == 1 && parsed[0] == Zquic::Version1,
    "version negotiation parse mismatch");
}

void testServerVersionDecision()
{
  ZuTestScope(testServerVersionDecision);

  Zquic::CxnID dcid{"client-dc"};
  Zquic::CxnID scid{"client-sc"};
  uint8_t b[128];
  int n = Zquic::Packet::writeInitial(b, sizeof(b), dcid, scid, 0, 1);
  ZuCHECK(n > 0, "Initial write failed");
  b[n++] = 0;

  uint8_t response[128];
  Zquic::ServerPacketDecision d = Zquic::ServerPacket::routeLongHeader(
    ZuCSpan{reinterpret_cast<const char *>(b), unsigned(n)},
    response, sizeof(response));
  ZuCHECK(d.action == Zquic::ServerPacketAction::AcceptInitial &&
      d.header.dcid == dcid && d.header.scid == scid &&
      !d.responseLength,
    "supported Initial was not accepted for bootstrap");

  b[1] = 0xff; b[2] = 0x00; b[3] = 0x00; b[4] = 0x1d;
  d = Zquic::ServerPacket::routeLongHeader(
    ZuCSpan{reinterpret_cast<const char *>(b), unsigned(n)},
    response, sizeof(response));
  ZuCHECK(d.action == Zquic::ServerPacketAction::VersionNegotiation &&
      d.responseLength > 0,
    "unsupported Initial did not produce Version Negotiation");

  Zquic::LongHeader h;
  ZuCHECK(Zquic::Packet::parseLong(
      ZuCSpan{reinterpret_cast<const char *>(response),
	unsigned(d.responseLength)}, h) > 0 &&
      Zquic::Packet::isVersionNegotiation(
	ZuCSpan{reinterpret_cast<const char *>(response),
	  unsigned(d.responseLength)}) &&
      h.dcid == scid && h.scid == dcid,
    "Version Negotiation response CID mapping mismatch");

  uint32_t versions[1] = {};
  unsigned nVersions = 0;
  ZuCHECK(!Zquic::Packet::parseVersionNegotiation(
      ZuCSpan{reinterpret_cast<const char *>(response),
	unsigned(d.responseLength)}, versions, 1, nVersions) &&
      nVersions == 1 && versions[0] == Zquic::Version1,
    "Version Negotiation response version list mismatch");

  d = Zquic::ServerPacket::routeLongHeader(
    ZuCSpan{reinterpret_cast<const char *>(response), unsigned(d.responseLength)},
    response, sizeof(response));
  ZuCHECK(d.action == Zquic::ServerPacketAction::Drop,
    "server accepted a Version Negotiation packet");

  n = Zquic::Packet::writeHandshake(b, sizeof(b), dcid, scid, 0, 1);
  ZuCHECK(n > 0, "Handshake write failed");
  d = Zquic::ServerPacket::routeLongHeader(
    ZuCSpan{reinterpret_cast<const char *>(b), unsigned(n)},
    response, sizeof(response));
  ZuCHECK(d.action == Zquic::ServerPacketAction::Drop,
    "server accepted non-Initial bootstrap packet");
}

void testRetryHeader()
{
  ZuTestScope(testRetryHeader);

  Zquic::CxnID dcid{"client01"};
  Zquic::CxnID scid{"server01"};
  static constexpr char Tag[] = "0123456789abcdef";
  uint8_t b[128];
  int n = Zquic::Packet::writeRetry(
    b, sizeof(b), dcid, scid, "retry-token", Tag);
  ZuCHECK(n > 0, "Retry write failed");

  Zquic::LongHeader h;
  ZuCHECK(Zquic::Packet::parseLong(
    ZuCSpan{reinterpret_cast<char *>(b), unsigned(n)}, h) > 0,
    "Retry parse failed");
  ZuCHECK(h.type == Zquic::PacketType::Retry && h.dcid == dcid && h.scid == scid,
    "Retry header mismatch");

  Zquic::RetryPacket retry;
  ZuCHECK(Zquic::Packet::parseRetry(
      ZuCSpan{reinterpret_cast<char *>(b), unsigned(n)}, retry) == n,
    "Retry packet parse failed");
  ZuCHECK(retry.token == "retry-token" && retry.integrityTag == Tag,
    "Retry token/tag parse mismatch");

  Zquic::CxnID odcid{"original"};
  n = Zquic::Packet::writeRetryAuthenticated(
    b, sizeof(b), dcid, scid, "retry-token", odcid);
  ZuCHECK(n > 0, "authenticated Retry write failed");
  ZuCHECK(Zquic::Packet::parseRetry(
      ZuCSpan{reinterpret_cast<char *>(b), unsigned(n)}, retry) == n &&
      retry.token == "retry-token" &&
      retry.integrityTag.length() == 16,
    "authenticated Retry parse failed");
  ZuCHECK(Zquic::Packet::validateRetryIntegrity(
      ZuCSpan{reinterpret_cast<char *>(b), unsigned(n)}, odcid),
    "authenticated Retry integrity validation failed");
  b[n - 1] ^= 1;
  ZuCHECK(!Zquic::Packet::validateRetryIntegrity(
      ZuCSpan{reinterpret_cast<char *>(b), unsigned(n)}, odcid),
    "mutated Retry integrity tag was accepted");
}

void testRetryTransportParameterValidation()
{
  ZuTestScope(testRetryTransportParameterValidation);

  Zquic::CxnID initialDCID{"client-dc"};
  Zquic::CxnID initialSCID{"client-sc"};
  Zquic::CxnID retrySCID{"retry-sc"};
  Zquic::CxnID serverInitialSCID{"server-sc"};
  static constexpr char Tag[] = "0123456789abcdef";

  Zquic::ClientBootstrap bootstrap;
  ZuCHECK(bootstrap.start(initialDCID, initialSCID),
    "client bootstrap start failed");

  uint8_t b[128];
  int n = Zquic::Packet::writeRetry(
    b, sizeof(b), initialSCID, retrySCID, "retry-token", Tag);
  Zquic::RetryPacket retry;
  ZuCHECK(n > 0 && Zquic::Packet::parseRetry(
      ZuCSpan{reinterpret_cast<char *>(b), unsigned(n)}, retry) == n,
    "Retry packet setup failed");
  ZuCHECK(bootstrap.onRetry(retry) && bootstrap.retried() &&
      bootstrap.retrySCID() == retrySCID &&
      bootstrap.retryTokenLength() == strlen("retry-token"),
    "Retry bootstrap state mismatch");

  Zquic::ClientBootstrap checkedBootstrap;
  ZuCHECK(checkedBootstrap.start(initialDCID, initialSCID),
    "checked Retry bootstrap start failed");
  n = Zquic::Packet::writeRetryAuthenticated(
    b, sizeof(b), initialSCID, retrySCID, "retry-token", initialDCID);
  ZuCHECK(n > 0 && checkedBootstrap.onRetry(
      ZuCSpan{reinterpret_cast<char *>(b), unsigned(n)}) &&
      checkedBootstrap.retried() &&
      checkedBootstrap.retrySCID() == retrySCID,
    "authenticated Retry bootstrap validation failed");

  Zquic::ClientBootstrap badBootstrap;
  ZuCHECK(badBootstrap.start(initialDCID, initialSCID),
    "bad Retry bootstrap start failed");
  b[n - 1] ^= 1;
  ZuCHECK(!badBootstrap.onRetry(
      ZuCSpan{reinterpret_cast<char *>(b), unsigned(n)}),
    "mutated Retry bootstrap was accepted");

  Zquic::TransportParams params;
  params.originalDCID = initialDCID;
  params.initialSCID = serverInitialSCID;
  params.retrySCID = retrySCID;
  ZuCHECK(bootstrap.validateServerTransportParams(params, serverInitialSCID),
    "Retry transport parameter validation failed");
  params.retrySCID = "wrong-sc";
  ZuCHECK(!bootstrap.validateServerTransportParams(params, serverInitialSCID),
    "bad Retry SCID was accepted");

  Zquic::ClientBootstrap noRetry;
  ZuCHECK(noRetry.start(initialDCID, initialSCID),
    "no-Retry bootstrap start failed");
  params.retrySCID = {};
  ZuCHECK(noRetry.validateServerTransportParams(params, serverInitialSCID),
    "no-Retry transport parameter validation failed");
  params.retrySCID = retrySCID;
  ZuCHECK(!noRetry.validateServerTransportParams(params, serverInitialSCID),
    "unexpected Retry SCID was accepted");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testVersionNegotiation);
  ZuTestCall(testServerVersionDecision);
  ZuTestCall(testRetryHeader);
  ZuTestCall(testRetryTransportParameterValidation);
}
