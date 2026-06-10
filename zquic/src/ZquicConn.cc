//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZquicConn.hh>

#include <string.h>

#include <zlib/ZtlsBackend.hh>

namespace Zquic {

ServerPacketDecision ServerPacket::routeLongHeader(
  ZuCSpan datagram, uint8_t *response, unsigned responseLen)
{
  ServerPacketDecision decision;
  if (!Packet::isLong(datagram) || Packet::isVersionNegotiation(datagram))
    return decision;

  if (Packet::parseLong(datagram, decision.header) < 0) return decision;

  if (!VersionNegotiation::supported(decision.header.version)) {
    int n = VersionNegotiation::write(
      response, responseLen, decision.header.scid, decision.header.dcid);
    if (n > 0) {
      decision.responseLength = unsigned(n);
      decision.action = ServerPacketAction::VersionNegotiation;
    }
    return decision;
  }

  if (decision.header.type == PacketType::Initial)
    decision.action = ServerPacketAction::AcceptInitial;
  return decision;
}

bool StatelessResetToken::set(ZuCSpan token)
{
  if (token.length() != Length) return false;
  memcpy(m_data, token.data(), Length);
  m_valid = true;
  return true;
}

bool StatelessResetToken::generate()
{
  uint8_t bytes[Length];
  if (!Ztls::Backend::init() ||
      !Ztls::Backend::random_bytes(ZuSpan<uint8_t>{bytes, Length}))
    return false;
  return set(ZuCSpan{reinterpret_cast<const char *>(bytes), Length});
}

bool StatelessResetToken::equals(const StatelessResetToken &token) const
{
  return m_valid == token.m_valid &&
    (!m_valid || !memcmp(m_data, token.m_data, Length));
}

int StatelessReset::decode(StatelessResetToken &token, ZuCSpan datagram)
{
  if (!datagram || datagram.length() <= MinLength || Packet::isLong(datagram))
    return -1;
  ZuCSpan suffix{
    datagram.data() + datagram.length() - TokenLength, TokenLength};
  return token.set(suffix) ? 0 : -1;
}

bool StatelessReset::verify(ZuCSpan datagram, const StatelessResetToken &token)
{
  if (!token.valid()) return false;
  StatelessResetToken decoded;
  if (decode(decoded, datagram) < 0) return false;
  uint8_t diff = 0;
  for (unsigned i = 0; i < TokenLength; ++i)
    diff |= decoded.data()[i] ^ token.data()[i];
  return !diff;
}

int StatelessReset::writeForUnknownCID(
  uint8_t *out, unsigned len, ZuCSpan receivedPacket,
  const StatelessResetToken &token)
{
  if (!token.valid() || !receivedPacket || Packet::isLong(receivedPacket) ||
      receivedPacket.length() <= MinLength)
    return -1;

  unsigned n = receivedPacket.length() - 1;
  if (n > len) n = len;
  if (n < MinLength) return -1;

  unsigned randomLen = n - TokenLength;
  if (!Ztls::Backend::init() ||
      !Ztls::Backend::random_bytes(ZuSpan<uint8_t>{out, randomLen}))
    return -1;

  out[0] &= 0x7fU;
  out[0] |= 0x40U;
  memcpy(out + randomLen, token.data(), TokenLength);
  return int(n);
}

bool CxnIDGen::random(CxnID &cid, unsigned length)
{
  if (length < InitialLength || length > CxnIDMax) return false;

  uint8_t bytes[CxnIDMax];
  if (!Ztls::Backend::init() ||
      !Ztls::Backend::random_bytes(ZuSpan<uint8_t>{bytes, length}))
    return false;

  CxnID generated;
  generated = ZuBSpan{bytes, length};
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
  const LongHeader &initial, unsigned datagramLength)
{
  if (m_accepted ||
      initial.type != PacketType::Initial ||
      !VersionNegotiation::supported(initial.version) ||
      initial.dcid.length() < CxnIDGen::InitialLength ||
      initial.scid.length() < CxnIDGen::InitialLength ||
      datagramLength < MinUDPPayload)
    return false;

  CxnID localSCID;
  StatelessResetToken resetToken;
  if (!CxnIDGen::random(localSCID) || !resetToken.generate()) return false;

  m_originalDCID = initial.dcid;
  m_clientInitialSCID = initial.scid;
  m_localInitialSCID = localSCID;
  m_statelessResetToken = resetToken;
  m_accepted = true;
  return true;
}

bool ServerBootstrap::transportParams(TransportParams &params) const
{
  if (!m_accepted) return false;
  params.originalDCID = m_originalDCID;
  params.initialSCID = m_localInitialSCID;
  params.statelessResetToken = m_statelessResetToken;
  params.statelessResetTokenPresent = true;
  return true;
}

} // namespace Zquic
