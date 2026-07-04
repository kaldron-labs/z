//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/Zquic.hh>

#include <string.h>

#include <zlib/ZtlsBackend.hh>
#include <zlib/ZtlsHMAC.hh>

namespace Zquic {

static uint64_t tokenNowSec_()
{
  return uint64_t(Zm::now().sec());
}

static void tokenPut32_(uint8_t *out, uint32_t v)
{
  out[0] = uint8_t(v >> 24);
  out[1] = uint8_t(v >> 16);
  out[2] = uint8_t(v >> 8);
  out[3] = uint8_t(v);
}

static void tokenPut64_(uint8_t *out, uint64_t v)
{
  for (unsigned i = 0; i < 8; ++i) out[i] = uint8_t(v >> (56 - i * 8));
}

static uint64_t tokenGet64_(const uint8_t *in)
{
  uint64_t v = 0;
  for (unsigned i = 0; i < 8; ++i) v = (v << 8) | in[i];
  return v;
}

static bool tokenHMAC_(uint8_t *out, ZuBSpan secret, ZuBSpan data)
{
  if (!secret || secret.length() < AddressToken::SecretLength) return false;
  Ztls::HMAC<> hmac;
  hmac.start(secret);
  hmac.update(data);
  uint8_t mac[Ztls::HMAC<>::Size];
  hmac.finish(ZuSpan<uint8_t>{mac, sizeof(mac)});
  memcpy(out, mac, AddressToken::TagLength);
  return true;
}

static bool tokenTagEquals_(ZuBSpan a, const uint8_t *b)
{
  if (a.length() != AddressToken::TagLength) return false;
  uint8_t diff = 0;
  for (unsigned i = 0; i < AddressToken::TagLength; ++i)
    diff |= a[i] ^ b[i];
  return !diff;
}

bool AddressToken::generateSecret(TokenSecret &secret)
{
  secret.length(SecretLength);
  return Ztls::Backend::init() &&
    Ztls::Backend::random_bytes(
      ZuSpan<uint8_t>{secret.data(), SecretLength});
}

bool AddressToken::encode(
  TokenBytes &token, TokenKind::T kind, ZuBSpan secret,
  const ZiSockAddr &addr, const CxnID &origDCID, const CxnID &serverCID,
  uint64_t nowSec, bool bindPort)
{
  enum {
    MagicLength = 4,
    FixedLength =
      MagicLength + 1 + 1 + 1 + 1 + 1 + 8 + 1 + 1 + 2 + NonceLength
  };
  ZiIP ip = addr.ip();
  unsigned addrLen;
  switch (ip.type()) {
    case ZiIPType::V4:
      addrLen = sizeof(in_addr);
      break;
    case ZiIPType::V6:
      addrLen = sizeof(in6_addr);
      break;
    default:
      return false;
  }
  if (!addr ||
      origDCID.length() > CxnIDMax ||
      serverCID.length() > CxnIDMax)
    return false;
  unsigned len =
    FixedLength + addrLen + origDCID.length() + serverCID.length() + TagLength;
  if (len > MaxLength) return false;
  token.length(len);
  uint8_t *out = token.data();
  unsigned o = 0;
  out[o++] = 'Z';
  out[o++] = 'Q';
  out[o++] = 'A';
  out[o++] = 'V';
  out[o++] = 2;
  out[o++] = uint8_t(kind);
  out[o++] = bindPort ? 1 : 0;
  out[o++] = origDCID.length();
  out[o++] = serverCID.length();
  tokenPut64_(out + o, nowSec ? nowSec : tokenNowSec_()); o += 8;
  out[o++] = uint8_t(ip.type());
  out[o++] = uint8_t(addrLen);
  switch (ip.type()) {
    case ZiIPType::V4:
      memcpy(out + o, &ip.inAddr(), addrLen);
      break;
    case ZiIPType::V6:
      memcpy(out + o, &ip.in6Addr(), addrLen);
      break;
  }
  o += addrLen;
  out[o++] = uint8_t(addr.port() >> 8);
  out[o++] = uint8_t(addr.port());
  if (!Ztls::Backend::init() ||
      !Ztls::Backend::random_bytes(
	ZuSpan<uint8_t>{out + o, NonceLength}))
    return false;
  o += NonceLength;
  if (origDCID) {
    memcpy(out + o, origDCID.data(), origDCID.length());
    o += origDCID.length();
  }
  if (serverCID) {
    memcpy(out + o, serverCID.data(), serverCID.length());
    o += serverCID.length();
  }
  uint8_t tag[TagLength];
  if (!tokenHMAC_(tag, secret, ZuBSpan{out, o})) return false;
  memcpy(out + o, tag, TagLength);
  return true;
}

TokenStatus::T AddressToken::validate(
  TokenInfo &info, ZuBSpan token, ZuBSpan secret, const ZiSockAddr &addr,
  uint64_t nowSec, uint64_t lifetimeSec, bool bindPort)
{
  enum {
    MagicLength = 4,
    FixedLength =
      MagicLength + 1 + 1 + 1 + 1 + 1 + 8 + 1 + 1 + 2 + NonceLength
  };
  info = {};
  if (!addr ||
      token.length() < FixedLength + TagLength ||
      token.length() > MaxLength ||
      token[0] != 'Z' || token[1] != 'Q' ||
      token[2] != 'A' || token[3] != 'V' ||
      uint8_t(token[4]) != 2)
    return TokenStatus::Malformed;
  unsigned odcidLen = uint8_t(token[7]);
  unsigned scidLen = uint8_t(token[8]);
  unsigned o = 17;
  ZiIPType::T ipType = ZiIPType::T(uint8_t(token[o++]));
  unsigned addrLen = uint8_t(token[o++]);
  switch (ipType) {
    case ZiIPType::V4:
      if (addrLen != sizeof(in_addr)) return TokenStatus::Malformed;
      break;
    case ZiIPType::V6:
      if (addrLen != sizeof(in6_addr)) return TokenStatus::Malformed;
      break;
    default:
      return TokenStatus::Malformed;
  }
  unsigned len = FixedLength + addrLen + odcidLen + scidLen + TagLength;
  if (odcidLen > CxnIDMax || scidLen > CxnIDMax || token.length() != len)
    return TokenStatus::Malformed;
  uint8_t tag[TagLength];
  auto auth = token;
  auth.trunc(token.length() - TagLength);
  if (!tokenHMAC_(tag, secret, auth))
    return TokenStatus::Auth;
  auto tokenTag = token;
  tokenTag.offset(token.length() - TagLength);
  if (!tokenTagEquals_(tokenTag, tag))
    return TokenStatus::Auth;
  switch (uint8_t(token[5])) {
    case TokenKind::Retry:
    case TokenKind::NewToken:
      info.kind = TokenKind::T(uint8_t(token[5]));
      break;
    default:
      return TokenStatus::Kind;
  }
  bool tokenBindPort = uint8_t(token[6]) & 1;
  if (bindPort != tokenBindPort) return TokenStatus::Address;
  o = 9;
  uint64_t issueSec = tokenGet64_(token.data() + o); o += 8;
  ipType = ZiIPType::T(uint8_t(token[o++]));
  addrLen = uint8_t(token[o++]);
  ZiIP ip;
  switch (ipType) {
    case ZiIPType::V4:
      {
	in_addr a;
	memcpy(&a, token.data() + o, sizeof(a));
	ip = a;
      }
      break;
    case ZiIPType::V6:
      {
	in6_addr a;
	memcpy(&a, token.data() + o, sizeof(a));
	ip = a;
      }
      break;
    default:
      return TokenStatus::Malformed;
  }
  o += addrLen;
  uint16_t port = (uint16_t(token[o]) << 8) | token[o + 1];
  o += 2 + NonceLength;
  uint64_t now = nowSec ? nowSec : tokenNowSec_();
  if (lifetimeSec && (issueSec > now || now - issueSec > lifetimeSec))
    return TokenStatus::Expired;
  if (ip != addr.ip() || (bindPort && port != addr.port()))
    return TokenStatus::Address;
  if (odcidLen) {
    info.origDCID = ZuBSpan{token.data() + o, odcidLen};
    o += odcidLen;
  }
  if (scidLen)
    info.serverCID = ZuBSpan{token.data() + o, scidLen};
  return TokenStatus::OK;
}

ServerPktDecision ServerPkt::routeLongHdr(
  ZuBSpan datagram, uint8_t *response, unsigned responseLen)
{
  ServerPktDecision decision;
  if (!Pkt::isLong(datagram) || Pkt::isVersionNegotiation(datagram))
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

bool ResetToken::set(ZuBSpan token)
{
  if (token.length() != Length) return false;
  memcpy(m_data, token.data(), Length);
  m_valid = true;
  return true;
}

bool ResetToken::generate()
{
  uint8_t bytes[Length];
  if (!Ztls::Backend::init() ||
      !Ztls::Backend::random_bytes(ZuSpan<uint8_t>{bytes, Length}))
    return false;
  return set(ZuBSpan{bytes, Length});
}

bool ResetToken::equals(const ResetToken &token) const
{
  return m_valid == token.m_valid &&
    (!m_valid || !memcmp(m_data, token.m_data, Length));
}

bool PathChallenge::set(ZuBSpan data)
{
  if (data.length() != Length) return false;
  memcpy(m_data, data.data(), Length);
  m_valid = true;
  return true;
}

bool PathChallenge::generate()
{
  uint8_t bytes[Length];
  if (!Ztls::Backend::init() ||
      !Ztls::Backend::random_bytes(ZuSpan<uint8_t>{bytes, Length}))
    return false;
  return set(ZuBSpan{bytes, Length});
}

bool PathChallenge::equals(ZuBSpan data) const
{
  return m_valid && data.length() == Length &&
    !memcmp(m_data, data.data(), Length);
}

bool PathChallenge::equals(const PathChallenge &challenge) const
{
  return m_valid == challenge.m_valid &&
    (!m_valid || !memcmp(m_data, challenge.m_data, Length));
}

int StatelessRst::decode(ResetToken &token, ZuBSpan datagram)
{
  if (!datagram || datagram.length() <= MinLength || Pkt::isLong(datagram))
    return -1;
  ZuBSpan suffix{
    datagram.data() + datagram.length() - TokenLength, TokenLength};
  return token.set(suffix) ? 0 : -1;
}

bool StatelessRst::verify(ZuBSpan datagram, const ResetToken &token)
{
  if (!token.valid()) return false;
  ResetToken decoded;
  if (decode(decoded, datagram) < 0) return false;
  uint8_t diff = 0;
  for (unsigned i = 0; i < TokenLength; ++i)
    diff |= decoded.data()[i] ^ token.data()[i];
  return !diff;
}

int StatelessRst::writeForUnknownCID(
  uint8_t *out, unsigned len, ZuBSpan receivedPkt,
  const ResetToken &token)
{
  if (!token.valid() || !receivedPkt || Pkt::isLong(receivedPkt) ||
      receivedPkt.length() <= MinLength)
    return -1;

  unsigned n = receivedPkt.length() - 1;
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
  const LongHdr &initial, unsigned datagramLength)
{
  return acceptInitial(InitialInfo{initial, {}, datagramLength}, initial,
    datagramLength);
}

bool ServerBootstrap::acceptInitial(
  const InitialInfo &info, const LongHdr &initial, unsigned datagramLength)
{
  if (m_accepted ||
      initial.type != PktType::Initial ||
      !VersionNeg::supported(initial.version) ||
      initial.dcid.length() < CxnIDGen::InitialLength ||
      initial.scid.length() < CxnIDGen::InitialLength ||
      datagramLength < MinUDPPayload)
    return false;

  CxnID localSCID;
  ResetToken resetToken;
  if (info.addressValidated && info.retrySCID)
    localSCID = info.retrySCID;
  else if (!CxnIDGen::random(localSCID))
    return false;
  if (!resetToken.generate()) return false;

  m_initialDCID = initial.dcid;
  m_origDCID = info.origDCID.length() ? info.origDCID : initial.dcid;
  m_clientInitialSCID = initial.scid;
  m_localInitialSCID = localSCID;
  m_retrySCID = info.retrySCID;
  m_statelessResetToken = resetToken;
  m_accepted = true;
  m_retried = info.addressValidated && info.retrySCID.length();
  return true;
}

bool ServerBootstrap::transportParams(TransportParams &params) const
{
  if (!m_accepted) return false;
  params.origDCID = m_origDCID;
  params.initialSCID = m_localInitialSCID;
  if (m_retried) params.retrySCID = m_retrySCID;
  params.statelessResetToken = m_statelessResetToken;
  params.statelessResetTokenPresent = true;
  return true;
}

} // namespace Zquic
