//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/Zquic.hh>

namespace Zquic {

static constexpr uint64_t TPOrigDCID = 0x00;
static constexpr uint64_t TPMaxIdleTimeout = 0x01;
static constexpr uint64_t TPStatelessResetToken = 0x02;
static constexpr uint64_t TPMaxUDPPayloadSize = 0x03;
static constexpr uint64_t TPInitialMaxData = 0x04;
static constexpr uint64_t TPInitMaxStreamDataDuplexLcl = 0x05;
static constexpr uint64_t TPInitMaxStreamDataDuplexRmt = 0x06;
static constexpr uint64_t TPInitialMaxStreamDataUni = 0x07;
static constexpr uint64_t TPInitMaxStreamsDuplex = 0x08;
static constexpr uint64_t TPInitialMaxStreamsUni = 0x09;
static constexpr uint64_t TPAckDelayExponent = 0x0a;
static constexpr uint64_t TPMaxAckDelay = 0x0b;
static constexpr uint64_t TPDisableMig = 0x0c;
static constexpr uint64_t TPPreferredAddress = 0x0d;
static constexpr uint64_t TPActiveCxnIDLimit = 0x0e;
static constexpr uint64_t TPInitialSCID = 0x0f;
static constexpr uint64_t TPRetrySCID = 0x10;

bool PreferredAddress::validate() const
{
  if (!cid) return !ipv4 && !ipv6 && !resetToken.valid();
  return resetToken.valid() &&
    (!ipv4 || (ipv4.ip().v4() && !ipv4.ip().wildcard() && ipv4.port())) &&
    (!ipv6 || (ipv6.ip().v6() && !ipv6.ip().wildcard() && ipv6.port()));
}

int PreferredAddress::encode(uint8_t *out, unsigned len) const
{
  if (!cid || !validate()) return -1;
  in_addr a4{};
  in6_addr a6{};
  if (ipv4) a4 = ipv4.ip().inAddr();
  if (ipv6) a6 = ipv6.ip().in6Addr();
  PktWriter w{out, len};
  w.put(ZuBSpan{reinterpret_cast<const uint8_t *>(&a4), 4});
  w.put16(ipv4 ? ipv4.port() : 0);
  w.put(ZuBSpan{reinterpret_cast<const uint8_t *>(&a6), 16});
  w.put16(ipv6 ? ipv6.port() : 0);
  w.put(uint8_t(cid.length()));
  w.put(cid);
  w.put(resetToken.bspan());
  return w.finish();
}

int PreferredAddress::decode(ZuBSpan in)
{
  *this = {};
  if (in.length() < FixedLength + 1) return -1;
  const auto p = in.data();
  unsigned n = p[24];
  if (!n || n > CxnIDMax || in.length() != FixedLength + n) return -1;
  in_addr a4;
  in6_addr a6;
  memcpy(&a4, p, 4);
  memcpy(&a6, p + 6, 16);
  ZiIP ip4{a4}, ip6{a6};
  unsigned port4 = (unsigned(p[4]) << 8) | p[5];
  unsigned port6 = (unsigned(p[22]) << 8) | p[23];
  if (!ip4.wildcard() || port4) ipv4.init(ip4, port4);
  if (!ip6.wildcard() || port6) ipv6.init(ip6, port6);
  cid = ZuBSpan{p + 25, n};
  resetToken = ResetToken{ZuBSpan{p + 25 + n, ResetToken::Length}};
  return validate() ? 0 : -1;
}

ZiSockAddr PreferredAddress::select(const ZiIP &peer) const
{
  // Keep the existing local address family; cross-family probing requires
  // a separately selected local interface.
  return peer.v6() ? ipv6 : ipv4;
}

bool TransportParams::validate() const
{
  return maxUDPPayloadSize >= MinUDPPayload &&
    maxUDPPayloadSize <= BufSize &&
    ackDelayExponent <= 20 &&
    maxAckDelay < (1ULL<<14) &&
    activeCxnIDLimit >= 2 && preferredAddress.validate();
}

ZeroRTTReason::T validateZeroRTTParams(
  const TransportParams &remembered, const TransportParams &current)
{
  if (!remembered.validate() || !current.validate())
    return ZeroRTTReason::TransportParams;
  if (remembered.initialMaxData > current.initialMaxData ||
      remembered.initMaxStreamDataDuplexLcl >
	current.initMaxStreamDataDuplexLcl ||
      remembered.initMaxStreamDataDuplexRmt >
	current.initMaxStreamDataDuplexRmt ||
      remembered.initialMaxStreamDataUni > current.initialMaxStreamDataUni)
    return ZeroRTTReason::FlowLimit;
  if (remembered.initMaxStreamsDuplex > current.initMaxStreamsDuplex ||
      remembered.initialMaxStreamsUni > current.initialMaxStreamsUni)
    return ZeroRTTReason::StreamLimit;
  if (remembered.activeCxnIDLimit > current.activeCxnIDLimit)
    return ZeroRTTReason::ActiveCIDLimit;
  if (remembered.maxUDPPayloadSize > current.maxUDPPayloadSize ||
      remembered.maxIdleTimeout != current.maxIdleTimeout ||
      remembered.ackDelayExponent != current.ackDelayExponent ||
      remembered.maxAckDelay != current.maxAckDelay ||
      remembered.disableActiveMigration != current.disableActiveMigration)
    return ZeroRTTReason::TransportParams;
  return ZeroRTTReason::None;
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
  if (preferredAddress) {
    unsigned len = preferredAddress.encodedLength();
    n += VarInt::length(TPPreferredAddress) + VarInt::length(len) + len;
  }
  if (origDCID)
    n += paramBytesLen(TPOrigDCID, origDCID);
  if (statelessResetTokenPresent)
    n += paramBytesLen(TPStatelessResetToken, statelessResetToken.bspan());
  n +=
    paramVarLen(TPMaxIdleTimeout, maxIdleTimeout) +
    paramVarLen(TPMaxUDPPayloadSize, maxUDPPayloadSize) +
    paramVarLen(TPInitialMaxData, initialMaxData) +
    paramVarLen(
      TPInitMaxStreamDataDuplexLcl,
      initMaxStreamDataDuplexLcl) +
    paramVarLen(
      TPInitMaxStreamDataDuplexRmt,
      initMaxStreamDataDuplexRmt) +
    paramVarLen(TPInitialMaxStreamDataUni, initialMaxStreamDataUni) +
    paramVarLen(TPInitMaxStreamsDuplex, initMaxStreamsDuplex) +
    paramVarLen(TPInitialMaxStreamsUni, initialMaxStreamsUni) +
    paramVarLen(TPAckDelayExponent, ackDelayExponent) +
    paramVarLen(TPMaxAckDelay, maxAckDelay) +
    paramVarLen(TPActiveCxnIDLimit, activeCxnIDLimit);
  if (disableActiveMigration)
    n += VarInt::length(TPDisableMig) + VarInt::length(0);
  if (initialSCID)
    n += paramBytesLen(TPInitialSCID, initialSCID);
  if (retrySCID)
    n += paramBytesLen(TPRetrySCID, retrySCID);
  return n;
}

int TransportParams::encode(uint8_t *out, unsigned len) const
{
  unsigned o = 0;
  if (preferredAddress) {
    uint8_t bytes[PreferredAddress::FixedLength + CxnIDMax];
    int n = preferredAddress.encode(bytes, sizeof(bytes));
    if (n < 0 || putParamBytes(out, len, TPPreferredAddress,
      ZuBSpan{bytes, unsigned(n)}, o) < 0) return -1;
  }
  if (origDCID &&
      putParamBytes(out, len, TPOrigDCID, origDCID, o) < 0)
    return -1;
  if (statelessResetTokenPresent &&
      putParamBytes(
	out, len, TPStatelessResetToken, statelessResetToken.bspan(), o) < 0)
    return -1;
  if (putParamVar(out, len, TPMaxIdleTimeout, maxIdleTimeout, o) < 0 ||
      putParamVar(out, len, TPMaxUDPPayloadSize, maxUDPPayloadSize, o) < 0 ||
      putParamVar(out, len, TPInitialMaxData, initialMaxData, o) < 0 ||
      putParamVar(
	out, len, TPInitMaxStreamDataDuplexLcl,
	initMaxStreamDataDuplexLcl, o) < 0 ||
      putParamVar(
	out, len, TPInitMaxStreamDataDuplexRmt,
	initMaxStreamDataDuplexRmt, o) < 0 ||
      putParamVar(
	out, len, TPInitialMaxStreamDataUni, initialMaxStreamDataUni, o) < 0 ||
      putParamVar(out, len, TPInitMaxStreamsDuplex,
	initMaxStreamsDuplex, o) < 0 ||
      putParamVar(out, len, TPInitialMaxStreamsUni,
	initialMaxStreamsUni, o) < 0 ||
      putParamVar(out, len, TPAckDelayExponent, ackDelayExponent, o) < 0 ||
      putParamVar(out, len, TPMaxAckDelay, maxAckDelay, o) < 0 ||
      putParamVar(
	out, len, TPActiveCxnIDLimit, activeCxnIDLimit, o) < 0)
    return -1;
  if (disableActiveMigration &&
      (VarInt::put(out, len, TPDisableMig, o) < 0 ||
       VarInt::put(out, len, 0, o) < 0))
    return -1;
  if (initialSCID &&
      putParamBytes(out, len, TPInitialSCID, initialSCID, o) < 0)
    return -1;
  if (retrySCID &&
      putParamBytes(out, len, TPRetrySCID, retrySCID, o) < 0)
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
      case TPPreferredAddress:
	if (preferredAddress || preferredAddress.decode(value) < 0) return -1;
	break;
      case TPOrigDCID:
	if (value.length() > CxnIDMax) return -1;
	origDCID = value;
	break;
      case TPStatelessResetToken:
	if (value.length() != ResetToken::Length) return -1;
	statelessResetToken = ResetToken{value};
	statelessResetTokenPresent = true;
	break;
      case TPMaxIdleTimeout:
	if (getParamVar(value, maxIdleTimeout) < 0) return -1;
	break;
      case TPMaxUDPPayloadSize:
	if (getParamVar(value, maxUDPPayloadSize) < 0) return -1;
	break;
      case TPInitialMaxData:
	if (getParamVar(value, initialMaxData) < 0) return -1;
	break;
      case TPInitMaxStreamDataDuplexLcl:
	if (getParamVar(value, initMaxStreamDataDuplexLcl) < 0)
	  return -1;
	break;
      case TPInitMaxStreamDataDuplexRmt:
	if (getParamVar(value, initMaxStreamDataDuplexRmt) < 0)
	  return -1;
	break;
      case TPInitialMaxStreamDataUni:
	if (getParamVar(value, initialMaxStreamDataUni) < 0) return -1;
	break;
      case TPInitMaxStreamsDuplex:
	if (getParamVar(value, initMaxStreamsDuplex) < 0) return -1;
	break;
      case TPInitialMaxStreamsUni:
	if (getParamVar(value, initialMaxStreamsUni) < 0) return -1;
	break;
      case TPAckDelayExponent:
	if (getParamVar(value, ackDelayExponent) < 0) return -1;
	break;
      case TPMaxAckDelay:
	if (getParamVar(value, maxAckDelay) < 0) return -1;
	break;
      case TPDisableMig:
	if (len) return -1;
	disableActiveMigration = true;
	break;
      case TPActiveCxnIDLimit:
	if (getParamVar(value, activeCxnIDLimit) < 0) return -1;
	break;
      case TPInitialSCID:
	if (value.length() > CxnIDMax) return -1;
	initialSCID = value;
	break;
      case TPRetrySCID:
	if (value.length() > CxnIDMax) return -1;
	retrySCID = value;
	break;
      default:
	break;
    }
  }
  return validate() ? 0 : -1;
}

} // namespace Zquic
