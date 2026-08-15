//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/Zquic.hh>

#include "ZquicOpenSSL.hh"

#include <openssl/crypto.h>
#include <openssl/evp.h>

namespace Zquic {

ZtEnumImplStruct(Vantage);
ZtEnumImplStruct(Vantage, JSON);

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

int VarInt::decode(ZuBSpan in, uint64_t &v, unsigned &n)
{
  if (!in) return -1;
  n = 1U << ((in[0] & 0xc0) >> 6);
  if (in.length() < n) return -1;
  v = in[0] & 0x3fU;
  for (unsigned i = 1; i < n; ++i) v = (v<<8) | in[i];
  return 0;
}

unsigned PktNumber::encodedLength(uint64_t pn, uint64_t largestAckd)
{
  uint64_t n = pn - largestAckd;
  if (n < (1ULL<<7)) return 1;
  if (n < (1ULL<<15)) return 2;
  if (n < (1ULL<<23)) return 3;
  return 4;
}

int PktNumber::encode(uint8_t *out, unsigned len, uint64_t pn, unsigned n)
{
  if (n < 1 || n > 4 || len < n) return -1;
  for (unsigned i = 0; i < n; ++i)
    out[i] = uint8_t(pn >> ((n - i - 1) * 8));
  return int(n);
}

uint64_t PktNumber::decode(uint64_t largestPN, uint64_t truncated, unsigned bits)
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

bool Pkt::isLong(ZuBSpan p)
{
  return p && (p[0] & 0x80);
}

bool Pkt::isVerNeg(ZuBSpan p)
{
  return isLong(p) && p.length() >= 5 &&
    !p[1] && !p[2] && !p[3] && !p[4];
}

static uint32_t load32_(const uint8_t *p)
{
  return (uint32_t(p[0])<<24) |
    (uint32_t(p[1])<<16) |
    (uint32_t(p[2])<<8) |
    uint32_t(p[3]);
}

int Pkt::parseLong(ZuBSpan p, LongHdr &h)
{
  if (p.length() < 7 || !isLong(p)) return -1;
  h.version = load32_(p.data() + 1);
  if (isVerNeg(p)) h.type = PktType::Initial;
  else {
    switch ((p[0] >> 4) & 0x03) {
      case 0: h.type = PktType::Initial; break;
      case 1: h.type = PktType::ZeroRTT; break;
      case 2: h.type = PktType::Handshake; break;
      default: h.type = PktType::Retry; break;
    }
  }
  unsigned o = 5;
  unsigned dlen = p[o++];
  if (dlen > CxnIDMax || p.length() < o + dlen + 1) return -1;
  h.dcid = ZuBSpan{p.data() + o, dlen};
  o += dlen;
  unsigned slen = p[o++];
  if (slen > CxnIDMax || p.length() < o + slen) return -1;
  h.scid = ZuBSpan{p.data() + o, slen};
  o += slen;
  if (isVerNeg(p) || h.type == PktType::Retry) {
    h.payloadOffset = o;
    return int(o);
  }
  if (h.type == PktType::Initial) {
    unsigned n = 0;
    if (VarInt::decode(
	  ZuBSpan{p.data() + o, p.length() - o}, h.tokenLength, n) < 0)
      return -1;
    o += n;
    if (h.tokenLength > p.length() - o) return -1;
    h.tokenOffset = o;
    o += unsigned(h.tokenLength);
  }
  unsigned n = 0;
  if (VarInt::decode(ZuBSpan{p.data() + o, p.length() - o}, h.length, n) < 0)
    return -1;
  o += n;
  h.pnLength = (p[0] & 0x03) + 1;
  h.pnOffset = o;
  h.payloadOffset = o + h.pnLength;
  if (h.pnLength > p.length() - h.pnOffset) return -1;
  return int(h.payloadOffset);
}

int Pkt::parseRetry(ZuBSpan p, RetryPkt &retry)
{
  retry = {};
  int o = parseLong(p, retry.header);
  if (o < 0 || retry.header.type != PktType::Retry) return -1;
  if (p.length() < unsigned(o) + 16) return -1;
  unsigned payloadLen = p.length() - unsigned(o);
  retry.token = ZuBSpan{p.data() + o, payloadLen - 16};
  retry.integrityTag = ZuBSpan{p.data() + p.length() - 16, 16};
  return int(p.length());
}

int Pkt::retryIntegrityTag(
  uint8_t *tag, unsigned len, ZuBSpan retryWithoutTag,
  const CxnID &origDCID)
{
  static constexpr ZuBArray<16> Key = {
    0xbe, 0x0c, 0x69, 0x0b, 0x9f, 0x66, 0x57, 0x5a,
    0x1d, 0x76, 0x6b, 0x54, 0xe3, 0x68, 0xc8, 0x4e
  };
  static constexpr ZuBArray<12> Nonce = {
    0x46, 0x15, 0x99, 0xd3, 0x5d, 0x63,
    0x2b, 0xf2, 0x23, 0x98, 0x25, 0xbb
  };

  if (!tag || len < 16 || !retryWithoutTag || origDCID.length() > 20)
    return -1;

  EVP_CIPHER_CTX *ctx = opensslCipherCtx();
  if (!ctx) return -1;

  int ok = 0;
  int outLen = 0;
  uint8_t odcidLen = origDCID.length();
  if (EVP_EncryptInit_ex(ctx, EVP_aes_128_gcm(), nullptr, nullptr, nullptr) == 1 &&
      EVP_CIPHER_CTX_ctrl(
	ctx, EVP_CTRL_GCM_SET_IVLEN, Nonce.length(), nullptr) == 1 &&
      EVP_EncryptInit_ex(
	ctx, nullptr, nullptr, Key.data(), Nonce.data()) == 1 &&
      EVP_EncryptUpdate(ctx, nullptr, &outLen, &odcidLen, 1) == 1 &&
      (!origDCID ||
	EVP_EncryptUpdate(ctx, nullptr, &outLen,
	  origDCID.data(), int(origDCID.length())) == 1) &&
      EVP_EncryptUpdate(ctx, nullptr, &outLen,
	retryWithoutTag.data(), int(retryWithoutTag.length())) == 1 &&
      EVP_EncryptFinal_ex(ctx, nullptr, &outLen) == 1 &&
      EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, tag) == 1)
    ok = 1;

  return ok ? 16 : -1;
}

bool Pkt::validateRetryIntegrity(ZuBSpan p, const CxnID &origDCID)
{
  RetryPkt retry;
  if (parseRetry(p, retry) < 0) return false;
  ZuBArray<16> tag(16, false);
  if (retryIntegrityTag(
	tag.data(), tag.length(),
	ZuBSpan{p.data(), p.length() - retry.integrityTag.length()},
	origDCID) < 0)
    return false;
  return !CRYPTO_memcmp(
    tag.data(), retry.integrityTag.data(), tag.length());
}

int Pkt::parseShort(ZuBSpan p, unsigned cidLen, ShortHdr &h)
{
  if (p.length() < 1 + cidLen + 1 || isLong(p) || cidLen > CxnIDMax)
    return -1;
  h.dcid = ZuBSpan{p.data() + 1, cidLen};
  h.pnLength = (p[0] & 0x03) + 1;
  h.pnOffset = 1 + cidLen;
  h.keyPhase = p[0] & 0x04;
  return int(h.pnOffset + h.pnLength);
}

int Pkt::parseVerNeg(
  ZuBSpan p, uint32_t *versions, unsigned capacity, unsigned &nVersions)
{
  nVersions = 0;
  if (!isVerNeg(p)) return -1;
  LongHdr h;
  int o = parseLong(p, h);
  if (o < 0 || ((p.length() - unsigned(o)) % 4)) return -1;
  for (unsigned i = unsigned(o), n = p.length(); i < n; i += 4) {
    if (nVersions >= capacity) return -1;
    versions[nVersions++] = load32_(p.data() + i);
  }
  return 0;
}

static int longTypeBits_(PktType::T type)
{
  if (type == PktType::Initial) return 0;
  if (type == PktType::ZeroRTT) return 1;
  if (type == PktType::Handshake) return 2;
  if (type == PktType::Retry) return 3;
  return -1;
}

int Pkt::longHdrLen(
  PktType::T type, const CxnID &dcid, const CxnID &scid,
  uint64_t payloadLength, unsigned pnLength, ZuBSpan token)
{
  if (pnLength < 1 || pnLength > 4) return -1;
  int typeBits = longTypeBits_(type);
  if (typeBits < 0 || type == PktType::Retry) return -1;
  if (type != PktType::Initial && token) return -1;
  if (payloadLength > uint64_t(-1) - pnLength) return -1;
  unsigned lengthLen = VarInt::length(payloadLength + pnLength);
  if (!lengthLen) return -1;
  unsigned tokenLenLen = 0;
  if (type == PktType::Initial) {
    tokenLenLen = VarInt::length(token.length());
    if (!tokenLenLen) return -1;
  }
  uint64_t need =
    1 + 4 + 1 + dcid.length() + 1 + scid.length() +
    tokenLenLen + token.length() + lengthLen;
  return need <= UINT_MAX ? int(need) : -1;
}

int Pkt::initialHdrLen(
  const CxnID &dcid, const CxnID &scid, uint64_t payloadLength,
  unsigned pnLength)
{
  return longHdrLen(
    PktType::Initial, dcid, scid, payloadLength, pnLength);
}

int Pkt::initialHdrLen(
  const CxnID &dcid, const CxnID &scid, ZuBSpan token,
  uint64_t payloadLength, unsigned pnLength)
{
  return longHdrLen(
    PktType::Initial, dcid, scid, payloadLength, pnLength, token);
}

int Pkt::handshakeHdrLen(
  const CxnID &dcid, const CxnID &scid, uint64_t payloadLength,
  unsigned pnLength)
{
  return longHdrLen(
    PktType::Handshake, dcid, scid, payloadLength, pnLength);
}

int Pkt::shortHdrLen(const CxnID &dcid, unsigned pnLength)
{
  if (pnLength < 1 || pnLength > 4) return -1;
  uint64_t need = 1 + dcid.length() + pnLength;
  return need <= UINT_MAX ? int(need) : -1;
}

int Pkt::writeLong(
  uint8_t *out, unsigned len, PktType::T type,
  const CxnID &dcid, const CxnID &scid,
  uint64_t payloadLength, unsigned pnLength, ZuBSpan token)
{
  int typeBits = longTypeBits_(type);
  int need = longHdrLen(type, dcid, scid, payloadLength, pnLength, token);
  if (typeBits < 0 || need < 0 || len < unsigned(need)) return -1;
  PktWriter w{out, len};
  w.put(uint8_t(0xc0 | (typeBits << 4) | (pnLength - 1)));
  w.put32(Version1);
  w.put(uint8_t(dcid.length()));
  w.put(dcid);
  w.put(uint8_t(scid.length()));
  w.put(scid);
  if (type == PktType::Initial) {
    w.putVar(token.length());
    w.put(token);
  }
  w.putVar(payloadLength + pnLength);
  return w.finish();
}

int Pkt::writeInitial(
  uint8_t *out, unsigned len, const CxnID &dcid, const CxnID &scid,
  uint64_t payloadLength, unsigned pnLength)
{
  return writeLong(
    out, len, PktType::Initial, dcid, scid, payloadLength, pnLength);
}

int Pkt::writeInitial(
  uint8_t *out, unsigned len, const CxnID &dcid, const CxnID &scid,
  ZuBSpan token, uint64_t payloadLength, unsigned pnLength)
{
  return writeLong(
    out, len, PktType::Initial, dcid, scid,
    payloadLength, pnLength, token);
}

int Pkt::writeHandshake(
  uint8_t *out, unsigned len, const CxnID &dcid, const CxnID &scid,
  uint64_t payloadLength, unsigned pnLength)
{
  return writeLong(
    out, len, PktType::Handshake, dcid, scid, payloadLength, pnLength);
}

int Pkt::writeRetry(
  uint8_t *out, unsigned len, const CxnID &dcid, const CxnID &scid,
  ZuBSpan token, ZuBSpan retryIntegrityTag)
{
  unsigned need =
    7 + dcid.length() + scid.length() + token.length() +
    retryIntegrityTag.length();
  if (len < need) return -1;
  PktWriter w{out, len};
  w.put(0xf0);
  w.put32(Version1);
  w.put(uint8_t(dcid.length()));
  w.put(dcid);
  w.put(uint8_t(scid.length()));
  w.put(scid);
  w.put(token);
  w.put(retryIntegrityTag);
  return w.finish();
}

int Pkt::writeRetryAuth(
  uint8_t *out, unsigned len, const CxnID &dcid, const CxnID &scid,
  ZuBSpan token, const CxnID &origDCID)
{
  int n = writeRetry(out, len, dcid, scid, token);
  if (n < 0 || len < unsigned(n) + 16) return -1;
  if (retryIntegrityTag(
	out + n, len - unsigned(n),
	ZuBSpan{out, unsigned(n)},
	origDCID) < 0)
    return -1;
  return n + 16;
}

int Pkt::writeShort(
  uint8_t *out, unsigned len, const CxnID &dcid, uint64_t pn,
  unsigned pnLength, bool keyPhase)
{
  int need = shortHdrLen(dcid, pnLength);
  if (need < 0 || len < unsigned(need)) return -1;
  PktWriter w{out, len};
  w.put(uint8_t(0x40 | (keyPhase ? 0x04 : 0) | (pnLength - 1)));
  w.put(dcid);
  auto pnOut = w.reserve(pnLength);
  if (!pnOut || PktNumber::encode(pnOut, pnLength, pn, pnLength) < 0)
    return -1;
  return w.finish();
}

int Pkt::writeVerNeg(
  uint8_t *out, unsigned len, const CxnID &dcid, const CxnID &scid,
  const uint32_t *versions, unsigned nVersions)
{
  unsigned need = 7 + dcid.length() + scid.length() + nVersions*4;
  if (len < need) return -1;
  PktWriter w{out, len};
  w.put(0x80);
  w.put32(0);
  w.put(uint8_t(dcid.length()));
  w.put(dcid);
  w.put(uint8_t(scid.length()));
  w.put(scid);
  for (unsigned i = 0; i < nVersions; ++i) w.put32(versions[i]);
  return w.finish();
}

} // namespace Zquic
