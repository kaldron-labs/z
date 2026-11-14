//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/Zutcp.hh>

using namespace ZuTestUtil;

void testVersionNegotiation()
{
  ZuTestScope(testVersionNegotiation);

  uint32_t offered[] = { 0xff00001dU, Zutcp::Version1 };
  ZuCHECK(Zutcp::VersionNeg::choose(offered, 2) == Zutcp::Version1,
    "version chooser did not select v1");
  ZuCHECK(!Zutcp::VersionNeg::supported(0xff00001dU),
    "unsupported version accepted");

  Zutcp::CxnID dcid{"client01"};
  Zutcp::CxnID scid{"server01"};
  uint8_t b[128];
  int n = Zutcp::VersionNeg::write(b, sizeof(b), dcid, scid);
  ZuCHECK(n > 0, "version negotiation write failed");

  uint32_t parsed[2] = {};
  unsigned nVersions = 0;
  ZuCHECK(!Zutcp::VersionNeg::parse(
    ZuBSpan{b, unsigned(n)},
    parsed, 2, nVersions), "version negotiation parse failed");
  ZuCHECK(nVersions == 1 && parsed[0] == Zutcp::Version1,
    "version negotiation parse mismatch");
}

void testServerVersionDecision()
{
  ZuTestScope(testServerVersionDecision);

  Zutcp::CxnID dcid{"client-dc"};
  Zutcp::CxnID scid{"client-sc"};
  uint8_t b[128];
  int n = Zutcp::Pkt::writeInitial(b, sizeof(b), dcid, scid, 0, 1);
  ZuCHECK(n > 0, "Initial write failed");
  b[n++] = 0;

  uint8_t response[128];
  Zutcp::ServerPktDecision d = Zutcp::ServerPkt::routeLongHdr(
    ZuBSpan{b, unsigned(n)},
    response, sizeof(response));
  ZuCHECK(d.action == Zutcp::ServerPktAction::AcceptInitial &&
      d.header.dcid == dcid && d.header.scid == scid &&
      !d.responseLength,
    "supported Initial was not accepted for bootstrap");

  b[1] = 0xff; b[2] = 0x00; b[3] = 0x00; b[4] = 0x1d;
  d = Zutcp::ServerPkt::routeLongHdr(
    ZuBSpan{b, unsigned(n)},
    response, sizeof(response));
  ZuCHECK(d.action == Zutcp::ServerPktAction::VersionNeg &&
      d.responseLength > 0,
    "unsupported Initial did not produce Version Negotiation");

  Zutcp::LongHdr h;
  ZuCHECK(Zutcp::Pkt::parseLong(
      ZuBSpan{response, unsigned(d.responseLength)}, h) > 0 &&
      Zutcp::Pkt::isVerNeg(
	ZuBSpan{response, unsigned(d.responseLength)}) &&
      h.dcid == scid && h.scid == dcid,
    "Version Negotiation response CID mapping mismatch");

  uint32_t versions[1] = {};
  unsigned nVersions = 0;
  ZuCHECK(!Zutcp::Pkt::parseVerNeg(
      ZuBSpan{response, unsigned(d.responseLength)}, versions, 1, nVersions) &&
      nVersions == 1 && versions[0] == Zutcp::Version1,
    "Version Negotiation response version list mismatch");

  d = Zutcp::ServerPkt::routeLongHdr(
    ZuBSpan{response, unsigned(d.responseLength)},
    response, sizeof(response));
  ZuCHECK(d.action == Zutcp::ServerPktAction::Drop,
    "server accepted a Version Negotiation packet");

  b[0] = 0xe0;
  d = Zutcp::ServerPkt::routeLongHdr(
    ZuBSpan{b, unsigned(n)}, response, sizeof(response));
  ZuCHECK(d.action == Zutcp::ServerPktAction::Drop,
    "server accepted an excluded long packet type");
}





int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testVersionNegotiation);
  ZuTestCall(testServerVersionDecision);
}
