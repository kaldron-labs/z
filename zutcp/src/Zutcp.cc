//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/Zutcp.hh>

#include <string.h>

#include <errno.h>
#include <sys/random.h>

namespace Zutcp {

static bool randomBytes(ZuSpan<uint8_t> bytes)
{
  while (bytes) {
    auto n = getrandom(bytes.data(), bytes.length(), 0);
    if (n < 0) { if (errno == EINTR) continue; return false; }
    if (!n) return false;
    bytes.offset(n);
  }
  return true;
}

ServerPktDecision ServerPkt::routeLongHdr(
  ZuBSpan datagram, uint8_t *response, unsigned responseLen)
{
  ServerPktDecision decision;
  if (!Pkt::isLong(datagram) || Pkt::isVerNeg(datagram))
    return decision;

  if (Pkt::parseLong(datagram, decision.header) < 0) return decision;

  if (!VersionNeg::supported(decision.header.version)) {
    int n = VersionNeg::write(
      response, responseLen, decision.header.scid, decision.header.dcid);
    if (n > 0) {
      decision.responseLength = unsigned(n);
      decision.action = ServerPktAction::VersionNeg;
    }
    return decision;
  }

  if (decision.header.type == PktType::Initial)
    decision.action = ServerPktAction::AcceptInitial;
  return decision;
}





















bool CxnIDGen::random(CxnID &cid, unsigned length)
{
  if (length < InitialLength || length > CxnIDMax) return false;

  CxnID generated;
  generated.length(length);
  if (!randomBytes(
	ZuSpan<uint8_t>{generated.data(), length}))
    return false;

  cid = generated;
  return true;
}

bool CxnIDGen::randomPair(
  CxnID &initialDCID, CxnID &initialSCID,
  unsigned dcidLength, unsigned scidLength)
{
  CxnID dcid;
  CxnID scid;
  if (!random(dcid, dcidLength) || !random(scid, scidLength)) return false;
  initialDCID = dcid;
  initialSCID = scid;
  return true;
}

bool ClientBootstrap::startRandom(unsigned dcidLength, unsigned scidLength)
{
  CxnID initialDCID;
  CxnID initialSCID;
  if (!CxnIDGen::randomPair(
	initialDCID, initialSCID, dcidLength, scidLength))
    return false;
  return start(initialDCID, initialSCID);
}





bool ServerBootstrap::acceptInitial(
  const LongHdr &initial, unsigned datagramLength)
{
  if (m_accepted || initial.type != PktType::Initial ||
      !VersionNeg::supported(initial.version) ||
      initial.dcid.length() < CxnIDGen::InitialLength ||
      initial.scid.length() > CxnIDMax || datagramLength < MinPktPayload)
    return false;
  CxnID localSCID;
  if (!CxnIDGen::random(localSCID)) return false;
  m_initialDCID = m_origDCID = initial.dcid;
  m_clientInitialSCID = initial.scid;
  m_localInitialSCID = localSCID;
  m_accepted = true;
  return true;
}

bool ServerBootstrap::transportParams(TransportParams &params) const
{
  if (!m_accepted) return false;
  params.origDCID = m_origDCID;
  params.initialSCID = m_localInitialSCID;
  return true;
}

} // namespace Zutcp
