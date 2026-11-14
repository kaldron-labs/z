//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/Zutcp.hh>



namespace Zutcp {

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

static uint32_t load32(const uint8_t *p)
{
  return (uint32_t(p[0])<<24) |
    (uint32_t(p[1])<<16) |
    (uint32_t(p[2])<<8) |
    uint32_t(p[3]);
}

int Pkt::parseLong(ZuBSpan p, LongHdr &h)
{
  if (p.length() < 7 || !isLong(p)) return -1;
  h.version = load32(p.data() + 1);
  if (isVerNeg(p)) h.type = PktType::Initial;
  else {
    if (p[0] & 0x30) return -1;
    h.type = PktType::Initial;
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
  if (isVerNeg(p)) {
    h.payloadOffset = o;
    return int(o);
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







int Pkt::parseShort(ZuBSpan p, unsigned cidLen, ShortHdr &h)
{
  if (p.length() < 1 + cidLen + 1 || isLong(p) || cidLen > CxnIDMax)
    return -1;
  h.dcid = ZuBSpan{p.data() + 1, cidLen};
  h.pnLength = (p[0] & 0x03) + 1;
  h.pnOffset = 1 + cidLen;
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
    versions[nVersions++] = load32(p.data() + i);
  }
  return 0;
}

int Pkt::initialHdrLen(
  const CxnID &dcid, const CxnID &scid, uint64_t payloadLength,
  unsigned pnLength)
{
  if (pnLength < 1 || pnLength > 4 ||
      payloadLength > uint64_t(-1) - pnLength) return -1;
  unsigned lengthLen = VarInt::length(payloadLength + pnLength);
  if (!lengthLen) return -1;
  return 7 + dcid.length() + scid.length() + lengthLen;
}

int Pkt::shortHdrLen(const CxnID &dcid, unsigned pnLength)
{
  if (pnLength < 1 || pnLength > 4) return -1;
  uint64_t need = 1 + dcid.length() + pnLength;
  return need <= UINT_MAX ? int(need) : -1;
}

int Pkt::writeInitial(
  uint8_t *out, unsigned len, const CxnID &dcid, const CxnID &scid,
  uint64_t payloadLength, unsigned pnLength)
{
  int need = initialHdrLen(dcid, scid, payloadLength, pnLength);
  if (need < 0 || len < unsigned(need)) return -1;
  PktWriter w{out, len};
  w.put(uint8_t(0xc0 | (pnLength - 1)));
  w.put32(Version1);
  w.put(uint8_t(dcid.length()));
  w.put(dcid);
  w.put(uint8_t(scid.length()));
  w.put(scid);
  w.putVar(payloadLength + pnLength);
  return w.finish();
}

int Pkt::writeShort(
  uint8_t *out, unsigned len, const CxnID &dcid, uint64_t pn,
  unsigned pnLength)
{
  int need = shortHdrLen(dcid, pnLength);
  if (need < 0 || len < unsigned(need)) return -1;
  PktWriter w{out, len};
  w.put(uint8_t(0x40 | (pnLength - 1)));
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

} // namespace Zutcp
