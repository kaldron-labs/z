//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZquicTransport.hh>

namespace Zquic {

static constexpr uint64_t TPOrigDCID = 0x00;
static constexpr uint64_t TPMaxIdleTimeout = 0x01;
static constexpr uint64_t TPStatelessResetToken = 0x02;
static constexpr uint64_t TPMaxUDPPayloadSize = 0x03;
static constexpr uint64_t TPInitialMaxData = 0x04;
static constexpr uint64_t TPInitialMaxStreamDataBidiLocal = 0x05;
static constexpr uint64_t TPInitialMaxStreamDataBidiRemote = 0x06;
static constexpr uint64_t TPInitialMaxStreamDataUni = 0x07;
static constexpr uint64_t TPInitialMaxStreamsBidi = 0x08;
static constexpr uint64_t TPInitialMaxStreamsUni = 0x09;
static constexpr uint64_t TPAckDelayExponent = 0x0a;
static constexpr uint64_t TPMaxAckDelay = 0x0b;
static constexpr uint64_t TPDisableActiveMigration = 0x0c;
static constexpr uint64_t TPActiveCxnIDLimit = 0x0e;
static constexpr uint64_t TPInitialSCID = 0x0f;
static constexpr uint64_t TPRetrySCID = 0x10;

bool TransportParams::validate() const
{
  return maxUDPPayloadSize >= MinUDPPayload &&
    maxUDPPayloadSize <= BufSize &&
    ackDelayExponent <= 20 &&
    maxAckDelay < (1ULL<<14) &&
    activeCxnIDLimit >= 2;
}

ZeroRTTReason::T validateZeroRTTParams(
  const TransportParams &remembered, const TransportParams &current)
{
  if (!remembered.validate() || !current.validate())
    return ZeroRTTReason::TransportParams;
  if (remembered.initialMaxData > current.initialMaxData ||
      remembered.initialMaxStreamDataBidiLocal >
	current.initialMaxStreamDataBidiLocal ||
      remembered.initialMaxStreamDataBidiRemote >
	current.initialMaxStreamDataBidiRemote ||
      remembered.initialMaxStreamDataUni > current.initialMaxStreamDataUni)
    return ZeroRTTReason::FlowLimit;
  if (remembered.initialMaxStreamsBidi > current.initialMaxStreamsBidi ||
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

static int putParamVar_(uint8_t *out, unsigned len, uint64_t id, uint64_t v,
    unsigned &o)
{
  if (o > len) return -1;
  uint8_t tmp[8];
  int n = VarInt::encode(tmp, sizeof(tmp), v);
  if (n < 0) return -1;
  PktWriter w{out + o, len - o};
  w.putVar(id);
  w.putVar(unsigned(n));
  w.put(ZuBSpan{tmp, unsigned(n)});
  if (!w.ok()) return -1;
  o += w.offset();
  return 0;
}

static int putParamBytes_(uint8_t *out, unsigned len, uint64_t id, ZuBSpan v,
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

static unsigned paramVarLen_(uint64_t id, uint64_t v)
{
  unsigned n = VarInt::length(v);
  return VarInt::length(id) + VarInt::length(n) + n;
}

static unsigned paramBytesLen_(uint64_t id, ZuBSpan v)
{
  return VarInt::length(id) + VarInt::length(v.length()) + v.length();
}

unsigned TransportParams::encodedLength() const
{
  unsigned n = 0;
  if (origDCID)
    n += paramBytesLen_(TPOrigDCID, origDCID);
  if (statelessResetTokenPresent)
    n += paramBytesLen_(TPStatelessResetToken, statelessResetToken.bspan());
  n +=
    paramVarLen_(TPMaxIdleTimeout, maxIdleTimeout) +
    paramVarLen_(TPMaxUDPPayloadSize, maxUDPPayloadSize) +
    paramVarLen_(TPInitialMaxData, initialMaxData) +
    paramVarLen_(
      TPInitialMaxStreamDataBidiLocal,
      initialMaxStreamDataBidiLocal) +
    paramVarLen_(
      TPInitialMaxStreamDataBidiRemote,
      initialMaxStreamDataBidiRemote) +
    paramVarLen_(TPInitialMaxStreamDataUni, initialMaxStreamDataUni) +
    paramVarLen_(TPInitialMaxStreamsBidi, initialMaxStreamsBidi) +
    paramVarLen_(TPInitialMaxStreamsUni, initialMaxStreamsUni) +
    paramVarLen_(TPAckDelayExponent, ackDelayExponent) +
    paramVarLen_(TPMaxAckDelay, maxAckDelay) +
    paramVarLen_(TPActiveCxnIDLimit, activeCxnIDLimit);
  if (disableActiveMigration)
    n += VarInt::length(TPDisableActiveMigration) + VarInt::length(0);
  if (initialSCID)
    n += paramBytesLen_(TPInitialSCID, initialSCID);
  if (retrySCID)
    n += paramBytesLen_(TPRetrySCID, retrySCID);
  return n;
}

int TransportParams::encode(uint8_t *out, unsigned len) const
{
  unsigned o = 0;
  if (origDCID &&
      putParamBytes_(out, len, TPOrigDCID, origDCID, o) < 0)
    return -1;
  if (statelessResetTokenPresent &&
      putParamBytes_(
	out, len, TPStatelessResetToken, statelessResetToken.bspan(), o) < 0)
    return -1;
  if (putParamVar_(out, len, TPMaxIdleTimeout, maxIdleTimeout, o) < 0 ||
      putParamVar_(out, len, TPMaxUDPPayloadSize, maxUDPPayloadSize, o) < 0 ||
      putParamVar_(out, len, TPInitialMaxData, initialMaxData, o) < 0 ||
      putParamVar_(
	out, len, TPInitialMaxStreamDataBidiLocal,
	initialMaxStreamDataBidiLocal, o) < 0 ||
      putParamVar_(
	out, len, TPInitialMaxStreamDataBidiRemote,
	initialMaxStreamDataBidiRemote, o) < 0 ||
      putParamVar_(
	out, len, TPInitialMaxStreamDataUni, initialMaxStreamDataUni, o) < 0 ||
      putParamVar_(out, len, TPInitialMaxStreamsBidi,
	initialMaxStreamsBidi, o) < 0 ||
      putParamVar_(out, len, TPInitialMaxStreamsUni,
	initialMaxStreamsUni, o) < 0 ||
      putParamVar_(out, len, TPAckDelayExponent, ackDelayExponent, o) < 0 ||
      putParamVar_(out, len, TPMaxAckDelay, maxAckDelay, o) < 0 ||
      putParamVar_(
	out, len, TPActiveCxnIDLimit, activeCxnIDLimit, o) < 0)
    return -1;
  if (disableActiveMigration &&
      (VarInt::put(out, len, TPDisableActiveMigration, o) < 0 ||
       VarInt::put(out, len, 0, o) < 0))
    return -1;
  if (initialSCID &&
      putParamBytes_(out, len, TPInitialSCID, initialSCID, o) < 0)
    return -1;
  if (retrySCID &&
      putParamBytes_(out, len, TPRetrySCID, retrySCID, o) < 0)
    return -1;
  return int(o);
}

static int getParamVar_(ZuBSpan v, uint64_t &out)
{
  unsigned n = 0;
  if (VarInt::decode(v, out, n) < 0 || n != v.length()) return -1;
  return 0;
}

int TransportParams::decode(ZuBSpan in)
{
  *this = {};
  unsigned o = 0;
  while (o < in.length()) {
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
      case TPStatelessResetToken:
	if (value.length() != ResetToken::Length) return -1;
	statelessResetToken = ResetToken{value};
	statelessResetTokenPresent = true;
	break;
      case TPMaxIdleTimeout:
	if (getParamVar_(value, maxIdleTimeout) < 0) return -1;
	break;
      case TPMaxUDPPayloadSize:
	if (getParamVar_(value, maxUDPPayloadSize) < 0) return -1;
	break;
      case TPInitialMaxData:
	if (getParamVar_(value, initialMaxData) < 0) return -1;
	break;
      case TPInitialMaxStreamDataBidiLocal:
	if (getParamVar_(value, initialMaxStreamDataBidiLocal) < 0)
	  return -1;
	break;
      case TPInitialMaxStreamDataBidiRemote:
	if (getParamVar_(value, initialMaxStreamDataBidiRemote) < 0)
	  return -1;
	break;
      case TPInitialMaxStreamDataUni:
	if (getParamVar_(value, initialMaxStreamDataUni) < 0) return -1;
	break;
      case TPInitialMaxStreamsBidi:
	if (getParamVar_(value, initialMaxStreamsBidi) < 0) return -1;
	break;
      case TPInitialMaxStreamsUni:
	if (getParamVar_(value, initialMaxStreamsUni) < 0) return -1;
	break;
      case TPAckDelayExponent:
	if (getParamVar_(value, ackDelayExponent) < 0) return -1;
	break;
      case TPMaxAckDelay:
	if (getParamVar_(value, maxAckDelay) < 0) return -1;
	break;
      case TPDisableActiveMigration:
	if (len) return -1;
	disableActiveMigration = true;
	break;
      case TPActiveCxnIDLimit:
	if (getParamVar_(value, activeCxnIDLimit) < 0) return -1;
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
