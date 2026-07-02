//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZquicFrame.hh>

namespace Zquic {

bool FrameCodec::ackEliciting(FrameType::T t)
{
  return t != FrameType::Padding && t != FrameType::Ack;
}

static int getVar_(ZuBSpan in, unsigned &o, uint64_t &v)
{
  unsigned n = 0;
  if (VarInt::decode(ZuBSpan{in.data() + o, in.length() - o}, v, n) < 0)
    return -1;
  o += n;
  return 0;
}

int FrameCodec::parse(ZuBSpan in, Frame &f, unsigned &used)
{
  used = 0;
  if (!in) return -1;
  uint8_t t = uint8_t(in[0]);
  unsigned o = 1;
  f.reset();

  switch (t) {
    case 0x00:
      f.type = FrameType::Padding;
      while (o < in.length() && !in[o]) ++o;
      used = o;
      return 0;
    case 0x01:
      f.type = FrameType::Ping;
      used = o;
      return 0;
    case 0x02:
    case 0x03: {
      f.type = FrameType::Ack;
      uint64_t rangeCount = 0;
      if (getVar_(in, o, f.offset) < 0 ||
	  getVar_(in, o, f.value) < 0 ||
	  getVar_(in, o, rangeCount) < 0 ||
	  getVar_(in, o, f.length) < 0)
	return -1;
      if (rangeCount + 1 > Frame::MaxAckRanges) return -1;
      if (f.length > f.offset) return -1;
      uint64_t smallest = f.offset - f.length;
      f.ackRanges.push(AckRange{f.offset, smallest});
      for (uint64_t i = 0; i < rangeCount; ++i) {
	uint64_t gap = 0, range = 0;
	if (getVar_(in, o, gap) < 0 || getVar_(in, o, range) < 0)
	  return -1;
	if (gap > smallest || smallest - gap < 2) return -1;
	uint64_t base = smallest - gap - 2;
	if (range > base) return -1;
	smallest = base - range;
	f.ackRanges.push(AckRange{base, smallest});
      }
      for (unsigned i = 0, j = f.ackRanges.length(); i < --j; ++i) {
	AckRange tmp = f.ackRanges[i];
	f.ackRanges[i] = f.ackRanges[j];
	f.ackRanges[j] = tmp;
      }
      if (t == 0x03) {
	if (getVar_(in, o, f.ackECN.ect0) < 0 ||
	    getVar_(in, o, f.ackECN.ect1) < 0 ||
	    getVar_(in, o, f.ackECN.ce) < 0)
	  return -1;
      }
      used = o;
      return 0;
    }
    case 0x04:
      f.type = FrameType::ResetStream;
      if (getVar_(in, o, f.streamID) < 0 ||
	  getVar_(in, o, f.errorCode) < 0 ||
	  getVar_(in, o, f.length) < 0)
	return -1;
      used = o;
      return 0;
    case 0x05:
      f.type = FrameType::StopSending;
      if (getVar_(in, o, f.streamID) < 0 ||
	  getVar_(in, o, f.errorCode) < 0)
	return -1;
      used = o;
      return 0;
    case 0x06:
      f.type = FrameType::Crypto;
      if (getVar_(in, o, f.offset) < 0 ||
	  getVar_(in, o, f.length) < 0)
	return -1;
      if (in.length() < o + f.length) return -1;
      f.payload = ZuBSpan{in.data() + o, unsigned(f.length)};
      used = o + f.length;
      return 0;
    case 0x07:
      f.type = FrameType::NewToken;
      if (getVar_(in, o, f.length) < 0) return -1;
      if (in.length() < o + f.length) return -1;
      f.payload = ZuBSpan{in.data() + o, unsigned(f.length)};
      used = o + f.length;
      return 0;
    case 0x08 ... 0x0f:
      f.type = FrameType::Stream;
      f.fin = t & 0x01;
      if (getVar_(in, o, f.streamID) < 0) return -1;
      if ((t & 0x04) && getVar_(in, o, f.offset) < 0) return -1;
      if (t & 0x02) {
	if (getVar_(in, o, f.length) < 0) return -1;
	if (in.length() < o + f.length) return -1;
      } else
	f.length = in.length() - o;
      f.payload = ZuBSpan{in.data() + o, unsigned(f.length)};
      used = o + f.length;
      return 0;
    case 0x10:
      f.type = FrameType::MaxData;
      if (getVar_(in, o, f.value) < 0) return -1;
      used = o;
      return 0;
    case 0x11:
      f.type = FrameType::MaxStreamData;
      if (getVar_(in, o, f.streamID) < 0 ||
	  getVar_(in, o, f.value) < 0)
	return -1;
      used = o;
      return 0;
    case 0x12:
    case 0x13:
      f.type = FrameType::MaxStreams;
      f.streamType =
	t == 0x12 ? Zi::StreamType::Duplex : Zi::StreamType::Simplex;
      if (getVar_(in, o, f.value) < 0) return -1;
      used = o;
      return 0;
    case 0x14:
      f.type = FrameType::DataBlocked;
      if (getVar_(in, o, f.value) < 0) return -1;
      used = o;
      return 0;
    case 0x15:
      f.type = FrameType::StreamDataBlocked;
      if (getVar_(in, o, f.streamID) < 0 ||
	  getVar_(in, o, f.value) < 0)
	return -1;
      used = o;
      return 0;
    case 0x16:
    case 0x17:
      f.type = FrameType::StreamsBlocked;
      f.streamType =
	t == 0x16 ? Zi::StreamType::Duplex : Zi::StreamType::Simplex;
      if (getVar_(in, o, f.value) < 0) return -1;
      used = o;
      return 0;
    case 0x18:
      f.type = FrameType::NewCxnID;
      if (getVar_(in, o, f.value) < 0 ||
	  getVar_(in, o, f.offset) < 0)
	return -1;
      if (in.length() < o + 1) return -1;
      f.length = uint8_t(in[o++]);
      if (f.length > CxnIDMax || in.length() < o + f.length + 16)
	return -1;
      f.payload = ZuBSpan{in.data() + o, unsigned(f.length)};
      if (!f.resetToken.set(
	    ZuBSpan{in.data() + o + unsigned(f.length), 16}))
	return -1;
      used = o + f.length + 16;
      return 0;
    case 0x19:
      f.type = FrameType::RetireCxnID;
      if (getVar_(in, o, f.value) < 0) return -1;
      used = o;
      return 0;
    case 0x1a:
    case 0x1b:
      f.type = t == 0x1a ?
	FrameType::PathChallenge : FrameType::PathResponse;
      if (in.length() < o + 8) return -1;
      f.payload = ZuBSpan{in.data() + o, 8};
      used = o + 8;
      return 0;
    case 0x1c:
      f.type = FrameType::ConnectionClose;
      if (getVar_(in, o, f.errorCode) < 0 ||
	  getVar_(in, o, f.value) < 0 ||
	  getVar_(in, o, f.length) < 0)
	return -1;
      if (in.length() < o + f.length) return -1;
      f.payload = ZuBSpan{in.data() + o, unsigned(f.length)};
      used = o + f.length;
      return 0;
    case 0x1d:
      f.type = FrameType::ApplicationClose;
      if (getVar_(in, o, f.errorCode) < 0 ||
	  getVar_(in, o, f.length) < 0)
	return -1;
      if (in.length() < o + f.length) return -1;
      f.payload = ZuBSpan{in.data() + o, unsigned(f.length)};
      used = o + f.length;
      return 0;
    case 0x1e:
      f.type = FrameType::HandshakeDone;
      used = o;
      return 0;
  }

  f.type = FrameType::Unknown;
  used = 1;
  return 0;
}

int FrameCodec::writePadding(uint8_t *out, unsigned len, unsigned n)
{
  if (len < n) return -1;
  memset(out, 0, n);
  return int(n);
}

int FrameCodec::writePing(uint8_t *out, unsigned len)
{
  PktWriter w{out, len};
  w.put(0x01);
  return w.finish();
}

int FrameCodec::writeCrypto(uint8_t *out, unsigned len, uint64_t offset, ZuBSpan p)
{
  PktWriter w{out, len};
  w.put(0x06);
  w.putVar(offset);
  w.putVar(p.length());
  w.put(p);
  return w.finish();
}

int FrameCodec::writeCryptoPrefix(
  uint8_t *out, unsigned len, uint64_t offset, unsigned payloadLen)
{
  PktWriter w{out, len};
  w.put(0x06);
  w.putVar(offset);
  w.putVar(payloadLen);
  return w.finish();
}

int FrameCodec::writeNewToken(uint8_t *out, unsigned len, ZuBSpan token)
{
  if (!token) return -1;
  PktWriter w{out, len};
  w.put(0x07);
  w.putVar(token.length());
  w.put(token);
  return w.finish();
}

int FrameCodec::writeStreamPrefix(
  uint8_t *out, unsigned len, uint64_t streamID, uint64_t offset,
  unsigned payloadLen, bool fin)
{
  uint8_t t = 0x0a | (fin ? 0x01 : 0);
  if (offset) t |= 0x04;
  PktWriter w{out, len};
  w.put(t);
  w.putVar(streamID);
  if (offset) w.putVar(offset);
  w.putVar(payloadLen);
  return w.finish();
}

int FrameCodec::writeStream(
  uint8_t *out, unsigned len, uint64_t streamID, uint64_t offset,
  ZuBSpan p, bool fin)
{
  uint8_t t = 0x0a | (fin ? 0x01 : 0);
  if (offset) t |= 0x04;
  PktWriter w{out, len};
  w.put(t);
  w.putVar(streamID);
  if (offset) w.putVar(offset);
  w.putVar(p.length());
  w.put(p);
  return w.finish();
}

int FrameCodec::writeAck(
  uint8_t *out, unsigned len, uint64_t largest, uint64_t delay,
  uint64_t firstRange)
{
  if (firstRange > largest) return -1;
  AckRange range{largest, largest - firstRange};
  return writeAckRanges(out, len, &range, 1, delay);
}

int FrameCodec::writeAckRanges(
  uint8_t *out, unsigned len, const AckRange *ranges, unsigned nRanges,
  uint64_t delay)
{
  return writeAckECN(out, len, ranges, nRanges, delay, {});
}

int FrameCodec::writeAckECN(
  uint8_t *out, unsigned len, const AckRange *ranges, unsigned nRanges,
  uint64_t delay, const AckECN &ecn)
{
  if (!len || !ranges || !nRanges) return -1;
  PktWriter w{out, len};
  w.put(ecn.any() ? 0x03 : 0x02);
  const AckRange *range = &ranges[nRanges - 1];
  if (range->first > range->largest) return -1;
  w.putVar(range->largest);
  w.putVar(delay);
  w.putVar(nRanges - 1);
  w.putVar(range->largest - range->first);
  uint64_t smallest = range->first;
  for (unsigned i = nRanges - 1; i--;) {
    range = &ranges[i];
    if (range->first > range->largest || range->largest > smallest ||
	smallest - range->largest < 2)
      return -1;
    w.putVar(smallest - range->largest - 2);
    w.putVar(range->largest - range->first);
    smallest = range->first;
  }
  if (ecn.any()) {
    w.putVar(ecn.ect0);
    w.putVar(ecn.ect1);
    w.putVar(ecn.ce);
  }
  return w.finish();
}

int FrameCodec::writeResetStream(
  uint8_t *out, unsigned len, uint64_t streamID, uint64_t appError,
  uint64_t finalSize)
{
  PktWriter w{out, len};
  w.put(0x04);
  w.putVar(streamID);
  w.putVar(appError);
  w.putVar(finalSize);
  return w.finish();
}

int FrameCodec::writeStopSending(
  uint8_t *out, unsigned len, uint64_t streamID, uint64_t appError)
{
  PktWriter w{out, len};
  w.put(0x05);
  w.putVar(streamID);
  w.putVar(appError);
  return w.finish();
}

int FrameCodec::writeMaxData(uint8_t *out, unsigned len, uint64_t maximum)
{
  PktWriter w{out, len};
  w.put(0x10);
  w.putVar(maximum);
  return w.finish();
}

int FrameCodec::writeMaxStreamData(
  uint8_t *out, unsigned len, uint64_t streamID, uint64_t maximum)
{
  PktWriter w{out, len};
  w.put(0x11);
  w.putVar(streamID);
  w.putVar(maximum);
  return w.finish();
}

int FrameCodec::writeMaxStreams(
  uint8_t *out, unsigned len, Zi::StreamType::T type, uint64_t maximum)
{
  PktWriter w{out, len};
  w.put(type == Zi::StreamType::Duplex ? 0x12 : 0x13);
  w.putVar(maximum);
  return w.finish();
}

int FrameCodec::writeDataBlocked(uint8_t *out, unsigned len, uint64_t maximum)
{
  PktWriter w{out, len};
  w.put(0x14);
  w.putVar(maximum);
  return w.finish();
}

int FrameCodec::writeStreamDataBlocked(
  uint8_t *out, unsigned len, uint64_t streamID, uint64_t maximum)
{
  PktWriter w{out, len};
  w.put(0x15);
  w.putVar(streamID);
  w.putVar(maximum);
  return w.finish();
}

int FrameCodec::writeStreamsBlocked(
  uint8_t *out, unsigned len, Zi::StreamType::T type, uint64_t maximum)
{
  PktWriter w{out, len};
  w.put(type == Zi::StreamType::Duplex ? 0x16 : 0x17);
  w.putVar(maximum);
  return w.finish();
}

int FrameCodec::writeNewCxnID(
  uint8_t *out, unsigned len, uint64_t sequence, uint64_t retirePriorTo,
  const CxnID &cid, const ResetToken &token)
{
  if (!len || !cid || cid.length() > CxnIDMax || !token.valid())
    return -1;
  PktWriter w{out, len};
  w.put(0x18);
  w.putVar(sequence);
  w.putVar(retirePriorTo);
  w.put(uint8_t(cid.length()));
  w.put(cid);
  w.put(ZuBSpan{token.data(), ResetToken::Length});
  return w.finish();
}

int FrameCodec::writeRetireCxnID(
  uint8_t *out, unsigned len, uint64_t sequence)
{
  PktWriter w{out, len};
  w.put(0x19);
  w.putVar(sequence);
  return w.finish();
}

int FrameCodec::writePathChallenge(uint8_t *out, unsigned len, ZuBSpan data)
{
  if (data.length() != 8) return -1;
  PktWriter w{out, len};
  w.put(0x1a);
  w.put(data);
  return w.finish();
}

int FrameCodec::writePathResponse(uint8_t *out, unsigned len, ZuBSpan data)
{
  if (data.length() != 8) return -1;
  PktWriter w{out, len};
  w.put(0x1b);
  w.put(data);
  return w.finish();
}

int FrameCodec::writeConnectionClose(uint8_t *out, unsigned len, uint64_t err)
{
  PktWriter w{out, len};
  w.put(0x1c);
  w.putVar(err);
  w.putVar(0);
  w.putVar(0);
  return w.finish();
}

int FrameCodec::writeApplicationClose(
  uint8_t *out, unsigned len, uint64_t err, ZuBSpan reason)
{
  PktWriter w{out, len};
  w.put(0x1d);
  w.putVar(err);
  w.putVar(reason.length());
  w.put(reason);
  return w.finish();
}

int FrameCodec::writeHandshakeDone(uint8_t *out, unsigned len)
{
  PktWriter w{out, len};
  w.put(0x1e);
  return w.finish();
}

} // namespace Zquic
