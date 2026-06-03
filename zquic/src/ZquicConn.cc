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
    decision.responseLength = VersionNegotiation::write(
      response, responseLen, decision.header.scid, decision.header.dcid);
    if (decision.responseLength > 0)
      decision.action = ServerPacketAction::VersionNegotiation;
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

bool CIDGenerator::random(ConnectionID &cid, unsigned length)
{
  if (length < InitialLength || length > ConnectionID::Max) return false;

  uint8_t bytes[ConnectionID::Max];
  if (!Ztls::Backend::init() ||
      !Ztls::Backend::random_bytes(ZuSpan<uint8_t>{bytes, length}))
    return false;

  ConnectionID generated;
  if (!generated.set(ZuCSpan{reinterpret_cast<const char *>(bytes), length}))
    return false;
  cid = generated;
  return true;
}

bool CIDGenerator::randomPair(
  ConnectionID &initialDCID, ConnectionID &initialSCID,
  unsigned dcidLength, unsigned scidLength)
{
  ConnectionID dcid;
  ConnectionID scid;
  if (!random(dcid, dcidLength) || !random(scid, scidLength)) return false;
  initialDCID = dcid;
  initialSCID = scid;
  return true;
}

bool ClientBootstrap::startRandom(unsigned dcidLength, unsigned scidLength)
{
  ConnectionID initialDCID;
  ConnectionID initialSCID;
  if (!CIDGenerator::randomPair(
	initialDCID, initialSCID, dcidLength, scidLength))
    return false;
  return start(initialDCID, initialSCID);
}

bool ServerBootstrap::acceptInitial(
  const LongHeader &initial, unsigned datagramLength, uintptr_t routeToken)
{
  if (m_accepted ||
      initial.type != PacketType::Initial ||
      !VersionNegotiation::supported(initial.version) ||
      initial.dcid.length() < CIDGenerator::InitialLength ||
      initial.scid.length() < CIDGenerator::InitialLength ||
      datagramLength < MinUDPPayload ||
      !routeToken)
    return false;

  ConnectionID localSCID;
  StatelessResetToken resetToken;
  if (!CIDGenerator::random(localSCID) || !resetToken.generate()) return false;

  if (!m_initialDCIDs.add(initial.dcid, 0, routeToken) ||
      !m_localCIDs.add(localSCID, 0, routeToken, resetToken))
    return false;

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
  return true;
}

} // namespace Zquic
