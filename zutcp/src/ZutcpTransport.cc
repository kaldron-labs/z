//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/Zutcp.hh>

namespace Zutcp {

int InitialPkt::write(
  uint8_t *out, unsigned capacity, const CxnID &dcid, const CxnID &scid,
  uint64_t number, unsigned pnLength, const TransportParams *params,
  unsigned frameLength)
{
  if (params && !params->validate()) return -1;
  unsigned n = params ? params->encodedLength() : 0;
  unsigned prefix = VarInt::length(n);
  uint64_t payload = uint64_t(prefix) + n + frameLength;
  int h = Pkt::initialHdrLen(dcid, scid, payload, pnLength);
  if (h < 0 || uint64_t(h) + pnLength + payload > capacity) return -1;
  if (Pkt::writeInitial(out, capacity, dcid, scid, payload, pnLength) != h ||
      PktNumber::encode(out + h, capacity - unsigned(h), number, pnLength) != int(pnLength))
    return -1;
  unsigned offset = unsigned(h) + pnLength;
  if (VarInt::encode(out + offset, capacity - offset, n) != int(prefix)) return -1;
  offset += prefix;
  if (params && params->encode(out + offset, n) != int(n)) return -1;
  return int(offset + n);
}

int InitialPkt::parse(ZuBSpan packet, uint64_t largestPN)
{
  LongHdr h;
  if (Pkt::parseLong(packet, h) < 0 || h.type != PktType::Initial ||
      h.version != Version1 || h.length < h.pnLength) return -1;
  uint64_t end = uint64_t(h.pnOffset) + h.length;
  if (end > packet.length()) return -1;
  auto body = packet.data() + h.payloadOffset;
  unsigned length = unsigned(h.length - h.pnLength), prefix = 0;
  uint64_t n = 0;
  if (VarInt::decode({body, length}, n, prefix) < 0 || n > length - prefix) return -1;
  uint64_t truncated = 0;
  for (unsigned i = 0; i < h.pnLength; ++i)
    truncated = (truncated<<8) | packet[h.pnOffset + i];
  header = ZuMv(h);
  number = PktNumber::decode(largestPN, truncated, header.pnLength<<3);
  params = {body + prefix, unsigned(n)};
  frames = {body + prefix + n, length - prefix - unsigned(n)};
  return int(end);
}

static constexpr uint64_t TPOrigDCID = 0x00;
static constexpr uint64_t TPMaxIdleTimeout = 0x01;
static constexpr uint64_t TPMaxPayloadSize = 0x03;
static constexpr uint64_t TPInitialMaxData = 0x04;
static constexpr uint64_t TPAckDelayExponent = 0x0a;
static constexpr uint64_t TPMaxAckDelay = 0x0b;
static constexpr uint64_t TPInitialSCID = 0x0f;

bool TransportParams::validate() const
{
  return maxPayloadSize >= MinPktPayload &&
    maxPayloadSize <= BufSize &&
    ackDelayExponent <= 20 &&
    maxAckDelay < (1ULL<<14);
}



static int putParamVar(uint8_t *out, unsigned len, uint64_t id, uint64_t v,
    unsigned &o)
{
  if (o > len) return -1;
  ZuBArray<8> tmp = ZuBArray<8>(8, false);
  int n = VarInt::encode(tmp.data(), tmp.length(), v);
  if (n < 0) return -1;
  PktWriter w{out + o, len - o};
  w.putVar(id);
  w.putVar(unsigned(n));
  w.put(ZuBSpan{tmp.data(), unsigned(n)});
  if (!w.ok()) return -1;
  o += w.offset();
  return 0;
}

static int putParamBytes(uint8_t *out, unsigned len, uint64_t id, ZuBSpan v,
    unsigned &o)
{
  if (o > len) return -1;
  PktWriter w{out + o, len - o};
  w.putVar(id);
  w.putVar(v.length());
  w.put(v);
  if (!w.ok()) return -1;
  o += w.offset();
  return 0;
}

static unsigned paramVarLen(uint64_t id, uint64_t v)
{
  unsigned n = VarInt::length(v);
  return VarInt::length(id) + VarInt::length(n) + n;
}

static unsigned paramBytesLen(uint64_t id, ZuBSpan v)
{
  return VarInt::length(id) + VarInt::length(v.length()) + v.length();
}

unsigned TransportParams::encodedLength() const
{
  unsigned n = 0;
  if (origDCID)
    n += paramBytesLen(TPOrigDCID, origDCID);
  n +=
    paramVarLen(TPMaxIdleTimeout, maxIdleTimeout) +
    paramVarLen(TPMaxPayloadSize, maxPayloadSize) +
    paramVarLen(TPInitialMaxData, initialMaxData) +
    paramVarLen(TPAckDelayExponent, ackDelayExponent) +
    paramVarLen(TPMaxAckDelay, maxAckDelay);
  if (initialSCID)
    n += paramBytesLen(TPInitialSCID, initialSCID);
  return n;
}

int TransportParams::encode(uint8_t *out, unsigned len) const
{
  unsigned o = 0;
  if (origDCID &&
      putParamBytes(out, len, TPOrigDCID, origDCID, o) < 0)
    return -1;
  if (putParamVar(out, len, TPMaxIdleTimeout, maxIdleTimeout, o) < 0 ||
      putParamVar(out, len, TPMaxPayloadSize, maxPayloadSize, o) < 0 ||
      putParamVar(out, len, TPInitialMaxData, initialMaxData, o) < 0 ||
      putParamVar(out, len, TPAckDelayExponent, ackDelayExponent, o) < 0 ||
      putParamVar(out, len, TPMaxAckDelay, maxAckDelay, o) < 0)
    return -1;
  if (initialSCID &&
      putParamBytes(out, len, TPInitialSCID, initialSCID, o) < 0)
    return -1;
  return int(o);
}

static int getParamVar(ZuBSpan v, uint64_t &out)
{
  unsigned n = 0;
  if (VarInt::decode(v, out, n) < 0 || n != v.length()) return -1;
  return 0;
}

int TransportParams::decode(ZuBSpan in)
{
  *this = {};
  unsigned o = 0;
  unsigned n = in.length();
  while (o < n) {
    uint64_t id = 0, len = 0;
    unsigned n = 0;
    if (VarInt::decode(ZuBSpan{in.data() + o, in.length() - o}, id, n) < 0)
      return -1;
    o += n;
    if (VarInt::decode(ZuBSpan{in.data() + o, in.length() - o}, len, n) < 0)
      return -1;
    o += n;
    if (in.length() < o + len) return -1;
    ZuBSpan value{in.data() + o, unsigned(len)};
    o += len;

    switch (id) {
      case TPOrigDCID:
	if (value.length() > CxnIDMax) return -1;
	origDCID = value;
	break;
      case TPMaxIdleTimeout:
	if (getParamVar(value, maxIdleTimeout) < 0) return -1;
	break;
      case TPMaxPayloadSize:
	if (getParamVar(value, maxPayloadSize) < 0) return -1;
	break;
      case TPInitialMaxData:
	if (getParamVar(value, initialMaxData) < 0) return -1;
	break;
      case TPAckDelayExponent:
	if (getParamVar(value, ackDelayExponent) < 0) return -1;
	break;
      case TPMaxAckDelay:
	if (getParamVar(value, maxAckDelay) < 0) return -1;
	break;
      case TPInitialSCID:
	if (value.length() > CxnIDMax) return -1;
	initialSCID = value;
	break;
      default:
	break;
    }
  }
  return validate() ? 0 : -1;
}

} // namespace Zutcp
