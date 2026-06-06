//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZquicPacket.hh>

#include <openssl/crypto.h>
#include <openssl/evp.h>

namespace Zquic {

unsigned VarInt::length(uint64_t v)
{
  if (v < (1ULL<<6)) return 1;
  if (v < (1ULL<<14)) return 2;
  if (v < (1ULL<<30)) return 4;
  if (v < (1ULL<<62)) return 8;
  return 0;
}

int VarInt::encode(uint8_t *out, unsigned len, uint64_t v)
{
  unsigned n = length(v);
  if (!n || len < n) return -1;
  switch (n) {
    case 1:
      out[0] = uint8_t(v);
      return 1;
    case 2:
      v |= 0x4000U;
      out[0] = uint8_t(v>>8);
      out[1] = uint8_t(v);
      return 2;
    case 4:
      v |= 0x80000000ULL;
      out[0] = uint8_t(v>>24);
      out[1] = uint8_t(v>>16);
      out[2] = uint8_t(v>>8);
      out[3] = uint8_t(v);
      return 4;
    default:
      v |= 0xc000000000000000ULL;
      for (unsigned i = 0; i < 8; ++i) out[i] = uint8_t(v>>(56 - i*8));
      return 8;
  }
}

int VarInt::put(uint8_t *out, unsigned len, uint64_t v, unsigned &o)
{
  if (o > len) return -1;
  int n = encode(out + o, len - o, v);
  if (n < 0) return -1;
  o += n;
  return 0;
}

int VarInt::decode(ZuCSpan in, uint64_t &v, unsigned &n)
{
  if (!in.length()) return -1;
  n = 1U << ((uint8_t(in[0]) & 0xc0) >> 6);
  if (in.length() < n) return -1;
  v = uint8_t(in[0]) & 0x3fU;
  for (unsigned i = 1; i < n; ++i) v = (v<<8) | uint8_t(in[i]);
  return 0;
}

bool ConnectionID::set(ZuCSpan s)
{
  if (s.length() > Max) return false;
  m_length = s.length();
  if (m_length) memcpy(m_data, s.data(), m_length);
  return true;
}

bool ConnectionID::equals(const ConnectionID &cid) const
{
  return m_length == cid.m_length && !memcmp(m_data, cid.m_data, m_length);
}

int ConnectionID::cmp(const ConnectionID &cid) const
{
  if (int i = int(m_length) - int(cid.m_length)) return i;
  return memcmp(m_data, cid.m_data, m_length);
}

unsigned PacketNumber::encodedLength(uint64_t pn, uint64_t largestAcked)
{
  uint64_t n = pn - largestAcked;
  if (n < (1ULL<<7)) return 1;
  if (n < (1ULL<<15)) return 2;
  if (n < (1ULL<<23)) return 3;
  return 4;
}

int PacketNumber::encode(uint8_t *out, unsigned len, uint64_t pn, unsigned n)
{
  if (n < 1 || n > 4 || len < n) return -1;
  for (unsigned i = 0; i < n; ++i)
    out[i] = uint8_t(pn >> ((n - i - 1) * 8));
  return int(n);
}

uint64_t PacketNumber::decode(uint64_t largestPN, uint64_t truncated, unsigned bits)
{
  uint64_t expected = largestPN + 1;
  uint64_t window = 1ULL << bits;
  uint64_t half = window >> 1;
  uint64_t mask = window - 1;
  uint64_t candidate = (expected & ~mask) | truncated;
  if (candidate + half <= expected) return candidate + window;
  if (candidate > expected + half && candidate >= window) return candidate - window;
  return candidate;
}

bool Packet::isLong(ZuCSpan p)
{
  return p.length() && (uint8_t(p[0]) & 0x80);
}

bool Packet::isVersionNegotiation(ZuCSpan p)
{
  return isLong(p) && p.length() >= 5 &&
    !uint8_t(p[1]) && !uint8_t(p[2]) && !uint8_t(p[3]) && !uint8_t(p[4]);
}

static uint32_t load32_(const char *p)
{
  return (uint32_t(uint8_t(p[0]))<<24) |
    (uint32_t(uint8_t(p[1]))<<16) |
    (uint32_t(uint8_t(p[2]))<<8) |
    uint32_t(uint8_t(p[3]));
}

static void store32_(uint8_t *p, uint32_t v)
{
  p[0] = uint8_t(v>>24);
  p[1] = uint8_t(v>>16);
  p[2] = uint8_t(v>>8);
  p[3] = uint8_t(v);
}

int Packet::parseLong(ZuCSpan p, LongHeader &h)
{
  if (p.length() < 7 || !isLong(p)) return -1;
  h.version = load32_(p.data() + 1);
  if (isVersionNegotiation(p)) h.type = PacketType::Initial;
  else {
    switch ((uint8_t(p[0]) >> 4) & 0x03) {
      case 0: h.type = PacketType::Initial; break;
      case 1: h.type = PacketType::ZeroRTT; break;
      case 2: h.type = PacketType::Handshake; break;
      default: h.type = PacketType::Retry; break;
    }
  }
  unsigned o = 5;
  unsigned dlen = uint8_t(p[o++]);
  if (dlen > ConnectionID::Max || p.length() < o + dlen + 1) return -1;
  h.dcid.set(ZuCSpan{p.data() + o, dlen});
  o += dlen;
  unsigned slen = uint8_t(p[o++]);
  if (slen > ConnectionID::Max || p.length() < o + slen) return -1;
  h.scid.set(ZuCSpan{p.data() + o, slen});
  o += slen;
  if (isVersionNegotiation(p) || h.type == PacketType::Retry) {
    h.payloadOffset = o;
    return int(o);
  }
  if (h.type == PacketType::Initial) {
    unsigned n = 0;
    if (VarInt::decode(
	  ZuCSpan{p.data() + o, p.length() - o}, h.tokenLength, n) < 0)
      return -1;
    o += n + h.tokenLength;
    if (p.length() < o) return -1;
  }
  unsigned n = 0;
  if (VarInt::decode(ZuCSpan{p.data() + o, p.length() - o}, h.length, n) < 0)
    return -1;
  o += n;
  h.pnLength = (uint8_t(p[0]) & 0x03) + 1;
  h.pnOffset = o;
  h.payloadOffset = o + h.pnLength;
  if (p.length() < h.payloadOffset) return -1;
  return int(h.payloadOffset);
}

int Packet::parseRetry(ZuCSpan p, RetryPacket &retry)
{
  retry = {};
  int o = parseLong(p, retry.header);
  if (o < 0 || retry.header.type != PacketType::Retry) return -1;
  if (p.length() < unsigned(o) + 16) return -1;
  unsigned payloadLen = p.length() - unsigned(o);
  retry.token = ZuCSpan{p.data() + o, payloadLen - 16};
  retry.integrityTag = ZuCSpan{p.data() + p.length() - 16, 16};
  return int(p.length());
}

int Packet::retryIntegrityTag(
  uint8_t *tag, unsigned len, ZuCSpan retryWithoutTag,
  const ConnectionID &originalDCID)
{
  static constexpr uint8_t Key[16] = {
    0xbe, 0x0c, 0x69, 0x0b, 0x9f, 0x66, 0x57, 0x5a,
    0x1d, 0x76, 0x6b, 0x54, 0xe3, 0x68, 0xc8, 0x4e
  };
  static constexpr uint8_t Nonce[12] = {
    0x46, 0x15, 0x99, 0xd3, 0x5d, 0x63,
    0x2b, 0xf2, 0x23, 0x98, 0x25, 0xbb
  };

  if (!tag || len < 16 || !retryWithoutTag || originalDCID.length() > 20)
    return -1;

  EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
  if (!ctx) return -1;

  int ok = 0;
  int outLen = 0;
  uint8_t odcidLen = originalDCID.length();
  if (EVP_EncryptInit_ex(ctx, EVP_aes_128_gcm(), nullptr, nullptr, nullptr) == 1 &&
      EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, sizeof(Nonce), nullptr) == 1 &&
      EVP_EncryptInit_ex(ctx, nullptr, nullptr, Key, Nonce) == 1 &&
      EVP_EncryptUpdate(ctx, nullptr, &outLen, &odcidLen, 1) == 1 &&
      (!originalDCID.length() ||
	EVP_EncryptUpdate(ctx, nullptr, &outLen,
	  originalDCID.data(), int(originalDCID.length())) == 1) &&
      EVP_EncryptUpdate(ctx, nullptr, &outLen,
	reinterpret_cast<const uint8_t *>(retryWithoutTag.data()),
	int(retryWithoutTag.length())) == 1 &&
      EVP_EncryptFinal_ex(ctx, nullptr, &outLen) == 1 &&
      EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, tag) == 1)
    ok = 1;

  EVP_CIPHER_CTX_free(ctx);
  return ok ? 16 : -1;
}

bool Packet::validateRetryIntegrity(ZuCSpan p, const ConnectionID &originalDCID)
{
  RetryPacket retry;
  if (parseRetry(p, retry) < 0) return false;
  uint8_t tag[16];
  if (retryIntegrityTag(
	tag, sizeof(tag),
	ZuCSpan{p.data(), p.length() - retry.integrityTag.length()},
	originalDCID) < 0)
    return false;
  return !CRYPTO_memcmp(tag, retry.integrityTag.data(), sizeof(tag));
}

int Packet::parseShort(ZuCSpan p, unsigned cidLen, ShortHeader &h)
{
  if (p.length() < 1 + cidLen + 1 || isLong(p) || cidLen > ConnectionID::Max)
    return -1;
  h.dcid.set(ZuCSpan{p.data() + 1, cidLen});
  h.pnLength = (uint8_t(p[0]) & 0x03) + 1;
  h.pnOffset = 1 + cidLen;
  return int(h.pnOffset + h.pnLength);
}

int Packet::parseVersionNegotiation(
  ZuCSpan p, uint32_t *versions, unsigned capacity, unsigned &nVersions)
{
  nVersions = 0;
  if (!isVersionNegotiation(p)) return -1;
  LongHeader h;
  int o = parseLong(p, h);
  if (o < 0 || ((p.length() - unsigned(o)) % 4)) return -1;
  for (unsigned i = unsigned(o); i < p.length(); i += 4) {
    if (nVersions >= capacity) return -1;
    versions[nVersions++] = load32_(p.data() + i);
  }
  return 0;
}

static int longTypeBits_(PacketType::T type)
{
  if (type == PacketType::Initial) return 0;
  if (type == PacketType::ZeroRTT) return 1;
  if (type == PacketType::Handshake) return 2;
  if (type == PacketType::Retry) return 3;
  return -1;
}

int Packet::writeLong(
  uint8_t *out, unsigned len, PacketType::T type,
  const ConnectionID &dcid, const ConnectionID &scid,
  uint64_t payloadLength, unsigned pnLength)
{
  if (pnLength < 1 || pnLength > 4) return -1;
  int typeBits = longTypeBits_(type);
  if (typeBits < 0 || type == PacketType::Retry) return -1;
  if (len < 7 + dcid.length() + scid.length() + 8) return -1;
  unsigned o = 0;
  out[o++] = uint8_t(0xc0 | (typeBits << 4) | (pnLength - 1));
  store32_(out + o, Version1); o += 4;
  out[o++] = dcid.length();
  memcpy(out + o, dcid.data(), dcid.length()); o += dcid.length();
  out[o++] = scid.length();
  memcpy(out + o, scid.data(), scid.length()); o += scid.length();
  if (type == PacketType::Initial) {
    int n = VarInt::encode(out + o, len - o, 0);
    if (n < 0) return -1;
    o += n;
  }
  int n = VarInt::encode(out + o, len - o, payloadLength + pnLength);
  if (n < 0) return -1;
  o += n;
  return int(o);
}

int Packet::writeInitial(
  uint8_t *out, unsigned len, const ConnectionID &dcid, const ConnectionID &scid,
  uint64_t payloadLength, unsigned pnLength)
{
  return writeLong(
    out, len, PacketType::Initial, dcid, scid, payloadLength, pnLength);
}

int Packet::writeHandshake(
  uint8_t *out, unsigned len, const ConnectionID &dcid, const ConnectionID &scid,
  uint64_t payloadLength, unsigned pnLength)
{
  return writeLong(
    out, len, PacketType::Handshake, dcid, scid, payloadLength, pnLength);
}

int Packet::writeRetry(
  uint8_t *out, unsigned len, const ConnectionID &dcid, const ConnectionID &scid,
  ZuCSpan token, ZuCSpan retryIntegrityTag)
{
  unsigned need =
    7 + dcid.length() + scid.length() + token.length() +
    retryIntegrityTag.length();
  if (len < need) return -1;
  unsigned o = 0;
  out[o++] = 0xf0;
  store32_(out + o, Version1); o += 4;
  out[o++] = dcid.length();
  memcpy(out + o, dcid.data(), dcid.length()); o += dcid.length();
  out[o++] = scid.length();
  memcpy(out + o, scid.data(), scid.length()); o += scid.length();
  if (token.length()) {
    memcpy(out + o, token.data(), token.length());
    o += token.length();
  }
  if (retryIntegrityTag.length()) {
    memcpy(out + o, retryIntegrityTag.data(), retryIntegrityTag.length());
    o += retryIntegrityTag.length();
  }
  return int(o);
}

int Packet::writeRetryAuthenticated(
  uint8_t *out, unsigned len, const ConnectionID &dcid, const ConnectionID &scid,
  ZuCSpan token, const ConnectionID &originalDCID)
{
  int n = writeRetry(out, len, dcid, scid, token);
  if (n < 0 || len < unsigned(n) + 16) return -1;
  if (retryIntegrityTag(
	out + n, len - unsigned(n),
	ZuCSpan{reinterpret_cast<const char *>(out), unsigned(n)},
	originalDCID) < 0)
    return -1;
  return n + 16;
}

int Packet::writeShort(
  uint8_t *out, unsigned len, const ConnectionID &dcid, uint64_t pn,
  unsigned pnLength)
{
  if (pnLength < 1 || pnLength > 4) return -1;
  unsigned need = 1 + dcid.length() + pnLength;
  if (len < need) return -1;
  unsigned o = 0;
  out[o++] = uint8_t(0x40 | (pnLength - 1));
  memcpy(out + o, dcid.data(), dcid.length()); o += dcid.length();
  if (PacketNumber::encode(out + o, len - o, pn, pnLength) < 0) return -1;
  return int(o + pnLength);
}

int Packet::writeVersionNegotiation(
  uint8_t *out, unsigned len, const ConnectionID &dcid, const ConnectionID &scid,
  const uint32_t *versions, unsigned nVersions)
{
  unsigned need = 7 + dcid.length() + scid.length() + nVersions*4;
  if (len < need) return -1;
  unsigned o = 0;
  out[o++] = 0x80;
  store32_(out + o, 0); o += 4;
  out[o++] = dcid.length();
  memcpy(out + o, dcid.data(), dcid.length()); o += dcid.length();
  out[o++] = scid.length();
  memcpy(out + o, scid.data(), scid.length()); o += scid.length();
  for (unsigned i = 0; i < nVersions; ++i) {
    store32_(out + o, versions[i]);
    o += 4;
  }
  return int(o);
}

} // namespace Zquic
