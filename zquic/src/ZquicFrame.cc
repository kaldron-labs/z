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

static int getVar_(ZuCSpan in, unsigned &o, uint64_t &v)
{
  unsigned n = 0;
  if (VarInt::decode(ZuCSpan{in.data() + o, in.length() - o}, v, n) < 0)
    return -1;
  o += n;
  return 0;
}

int FrameCodec::parse(ZuCSpan in, Frame &f, unsigned &used)
{
  used = 0;
  if (!in.length()) return -1;
  uint8_t t = uint8_t(in[0]);
  unsigned o = 1;
  f.reset();

  if (t == 0x00) {
    f.type = FrameType::Padding;
    while (o < in.length() && !in[o]) ++o;
    used = o;
    return 0;
  }
  if (t == 0x01) {
    f.type = FrameType::Ping;
    used = o;
    return 0;
  }
  if (t == 0x02 || t == 0x03) {
    f.type = FrameType::Ack;
    uint64_t rangeCount = 0;
    if (getVar_(in, o, f.offset) < 0 ||	// largest acked
	getVar_(in, o, f.value) < 0 ||	// ack delay
	getVar_(in, o, rangeCount) < 0 ||
	getVar_(in, o, f.length) < 0)	// first range length
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
      uint64_t ecn = 0;
      if (getVar_(in, o, ecn) < 0 ||
	  getVar_(in, o, ecn) < 0 ||
	  getVar_(in, o, ecn) < 0)
	return -1;
    }
    used = o;
    return 0;
  }
  if (t == 0x04) {
    unsigned n = 0;
    f.type = FrameType::ResetStream;
    if (VarInt::decode(
	  ZuCSpan{in.data() + o, in.length() - o}, f.streamID, n) < 0)
      return -1;
    o += n;
    if (VarInt::decode(
	  ZuCSpan{in.data() + o, in.length() - o}, f.errorCode, n) < 0)
      return -1;
    o += n;
    if (VarInt::decode(
	  ZuCSpan{in.data() + o, in.length() - o}, f.length, n) < 0)
      return -1;
    used = o + n;
    return 0;
  }
  if (t == 0x05) {
    unsigned n = 0;
    f.type = FrameType::StopSending;
    if (VarInt::decode(
	  ZuCSpan{in.data() + o, in.length() - o}, f.streamID, n) < 0)
      return -1;
    o += n;
    if (VarInt::decode(
	  ZuCSpan{in.data() + o, in.length() - o}, f.errorCode, n) < 0)
      return -1;
    used = o + n;
    return 0;
  }
  if (t == 0x06) {
    unsigned n = 0;
    f.type = FrameType::Crypto;
    if (VarInt::decode(
	  ZuCSpan{in.data() + o, in.length() - o}, f.offset, n) < 0)
      return -1;
    o += n;
    if (VarInt::decode(
	  ZuCSpan{in.data() + o, in.length() - o}, f.length, n) < 0)
      return -1;
    o += n;
    if (in.length() < o + f.length) return -1;
    f.payload = ZuCSpan{in.data() + o, unsigned(f.length)};
    used = o + f.length;
    return 0;
  }
  if (t == 0x07) {
    unsigned n = 0;
    f.type = FrameType::NewToken;
    if (VarInt::decode(
	  ZuCSpan{in.data() + o, in.length() - o}, f.length, n) < 0)
      return -1;
    o += n;
    if (in.length() < o + f.length) return -1;
    f.payload = ZuCSpan{in.data() + o, unsigned(f.length)};
    used = o + f.length;
    return 0;
  }
  if (t >= 0x08 && t <= 0x0f) {
    unsigned n = 0;
    f.type = FrameType::Stream;
    f.fin = t & 0x01;
    if (VarInt::decode(
	  ZuCSpan{in.data() + o, in.length() - o}, f.streamID, n) < 0)
      return -1;
    o += n;
    if (t & 0x04) {
      if (VarInt::decode(
	    ZuCSpan{in.data() + o, in.length() - o}, f.offset, n) < 0)
	return -1;
      o += n;
    }
    if (t & 0x02) {
      if (VarInt::decode(
	    ZuCSpan{in.data() + o, in.length() - o}, f.length, n) < 0)
	return -1;
      o += n;
      if (in.length() < o + f.length) return -1;
    } else
      f.length = in.length() - o;
    f.payload = ZuCSpan{in.data() + o, unsigned(f.length)};
    used = o + f.length;
    return 0;
  }
  if (t >= 0x10 && t <= 0x17) {
    unsigned n = 0;
    if (t == 0x10) {
      f.type = FrameType::MaxData;
      if (VarInt::decode(
	    ZuCSpan{in.data() + o, in.length() - o}, f.value, n) < 0)
	return -1;
      used = o + n;
      return 0;
    }
    if (t == 0x11) {
      f.type = FrameType::MaxStreamData;
      if (VarInt::decode(
	    ZuCSpan{in.data() + o, in.length() - o}, f.streamID, n) < 0)
	return -1;
      o += n;
      if (VarInt::decode(
	    ZuCSpan{in.data() + o, in.length() - o}, f.value, n) < 0)
	return -1;
      used = o + n;
      return 0;
    }
    if (t == 0x12 || t == 0x13) {
      f.type = FrameType::MaxStreams;
      f.streamType = t == 0x12 ? Zi::StreamType::Duplex : Zi::StreamType::Simplex;
      if (VarInt::decode(
	    ZuCSpan{in.data() + o, in.length() - o}, f.value, n) < 0)
	return -1;
      used = o + n;
      return 0;
    }
    if (t == 0x14) {
      f.type = FrameType::DataBlocked;
      if (VarInt::decode(
	    ZuCSpan{in.data() + o, in.length() - o}, f.value, n) < 0)
	return -1;
      used = o + n;
      return 0;
    }
    if (t == 0x15) {
      f.type = FrameType::StreamDataBlocked;
      if (VarInt::decode(
	    ZuCSpan{in.data() + o, in.length() - o}, f.streamID, n) < 0)
	return -1;
      o += n;
      if (VarInt::decode(
	    ZuCSpan{in.data() + o, in.length() - o}, f.value, n) < 0)
	return -1;
      used = o + n;
      return 0;
    }
    f.type = FrameType::StreamsBlocked;
    f.streamType = t == 0x16 ? Zi::StreamType::Duplex : Zi::StreamType::Simplex;
    if (VarInt::decode(
	  ZuCSpan{in.data() + o, in.length() - o}, f.value, n) < 0)
      return -1;
    used = o + n;
    return 0;
  }
  if (t == 0x18) {
    unsigned n = 0;
    f.type = FrameType::NewConnectionID;
    if (VarInt::decode(
	  ZuCSpan{in.data() + o, in.length() - o}, f.value, n) < 0)
      return -1;
    o += n;
    if (VarInt::decode(
	  ZuCSpan{in.data() + o, in.length() - o}, f.offset, n) < 0)
      return -1;
    o += n;
    if (in.length() < o + 1) return -1;
    f.length = uint8_t(in[o++]);
    if (f.length > CxnIDMax || in.length() < o + f.length + 16)
      return -1;
    f.payload = ZuCSpan{in.data() + o, unsigned(f.length)};
    if (!f.resetToken.set(
	  ZuCSpan{in.data() + o + unsigned(f.length), 16}))
      return -1;
    used = o + f.length + 16;
    return 0;
  }
  if (t == 0x19) {
    unsigned n = 0;
    f.type = FrameType::RetireConnectionID;
    if (VarInt::decode(
	  ZuCSpan{in.data() + o, in.length() - o}, f.value, n) < 0)
      return -1;
    used = o + n;
    return 0;
  }
  if (t == 0x1a || t == 0x1b) {
    f.type = t == 0x1a ? FrameType::PathChallenge : FrameType::PathResponse;
    if (in.length() < o + 8) return -1;
    f.payload = ZuCSpan{in.data() + o, 8};
    used = o + 8;
    return 0;
  }
  if (t == 0x1c || t == 0x1d) {
    f.type = FrameType::ConnectionClose;
    if (getVar_(in, o, f.errorCode) < 0) return -1;
    if (t == 0x1c && getVar_(in, o, f.value) < 0) return -1;
    if (getVar_(in, o, f.length) < 0) return -1;
    if (in.length() < o + f.length) return -1;
    f.payload = ZuCSpan{in.data() + o, unsigned(f.length)};
    used = o + f.length;
    return 0;
  }
  if (t == 0x1e) {
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
  if (!len) return -1;
  out[0] = 0x01;
  return 1;
}

int FrameCodec::writeCrypto(uint8_t *out, unsigned len, uint64_t offset, ZuCSpan p)
{
  int n = writeCryptoPrefix(out, len, offset, p.length());
  if (n < 0 || len - unsigned(n) < p.length()) return -1;
  memcpy(out + n, p.data(), p.length());
  return n + int(p.length());
}

int FrameCodec::writeCryptoPrefix(
  uint8_t *out, unsigned len, uint64_t offset, unsigned payloadLen)
{
  if (!len) return -1;
  unsigned o = 0;
  out[o++] = 0x06;
  if (VarInt::put(out, len, offset, o) < 0 ||
      VarInt::put(out, len, payloadLen, o) < 0)
    return -1;
  return int(o);
}

int FrameCodec::writeStreamPrefix(
  uint8_t *out, unsigned len, uint64_t streamID, uint64_t offset,
  unsigned payloadLen, bool fin)
{
  if (!len) return -1;
  unsigned o = 0;
  uint8_t t = 0x0a | (fin ? 0x01 : 0);
  if (offset) t |= 0x04;
  out[o++] = t;
  if (VarInt::put(out, len, streamID, o) < 0) return -1;
  if (offset && VarInt::put(out, len, offset, o) < 0) return -1;
  if (VarInt::put(out, len, payloadLen, o) < 0) return -1;
  return int(o);
}

int FrameCodec::writeStream(
  uint8_t *out, unsigned len, uint64_t streamID, uint64_t offset,
  ZuCSpan p, bool fin)
{
  int n = writeStreamPrefix(out, len, streamID, offset, p.length(), fin);
  if (n < 0 || len - unsigned(n) < p.length()) return -1;
  memcpy(out + n, p.data(), p.length());
  return n + int(p.length());
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
  if (!len || !ranges || !nRanges) return -1;
  unsigned o = 0;
  out[o++] = 0x02;
  const AckRange *range = &ranges[nRanges - 1];
  if (range->first > range->largest) return -1;
  if (VarInt::put(out, len, range->largest, o) < 0 ||
      VarInt::put(out, len, delay, o) < 0 ||
      VarInt::put(out, len, nRanges - 1, o) < 0 ||
      VarInt::put(out, len, range->largest - range->first, o) < 0)
    return -1;
  uint64_t smallest = range->first;
  for (unsigned i = nRanges - 1; i--;) {
    range = &ranges[i];
    if (range->first > range->largest || range->largest > smallest ||
	smallest - range->largest < 2)
      return -1;
    if (VarInt::put(out, len, smallest - range->largest - 2, o) < 0 ||
	VarInt::put(out, len, range->largest - range->first, o) < 0)
      return -1;
    smallest = range->first;
  }
  return int(o);
}

int FrameCodec::writeResetStream(
  uint8_t *out, unsigned len, uint64_t streamID, uint64_t appError,
  uint64_t finalSize)
{
  if (!len) return -1;
  unsigned o = 0;
  out[o++] = 0x04;
  if (VarInt::put(out, len, streamID, o) < 0 ||
      VarInt::put(out, len, appError, o) < 0 ||
      VarInt::put(out, len, finalSize, o) < 0)
    return -1;
  return int(o);
}

int FrameCodec::writeStopSending(
  uint8_t *out, unsigned len, uint64_t streamID, uint64_t appError)
{
  if (!len) return -1;
  unsigned o = 0;
  out[o++] = 0x05;
  if (VarInt::put(out, len, streamID, o) < 0 ||
      VarInt::put(out, len, appError, o) < 0)
    return -1;
  return int(o);
}

int FrameCodec::writeMaxData(uint8_t *out, unsigned len, uint64_t maximum)
{
  if (!len) return -1;
  unsigned o = 0;
  out[o++] = 0x10;
  if (VarInt::put(out, len, maximum, o) < 0) return -1;
  return int(o);
}

int FrameCodec::writeMaxStreamData(
  uint8_t *out, unsigned len, uint64_t streamID, uint64_t maximum)
{
  if (!len) return -1;
  unsigned o = 0;
  out[o++] = 0x11;
  if (VarInt::put(out, len, streamID, o) < 0 ||
      VarInt::put(out, len, maximum, o) < 0)
    return -1;
  return int(o);
}

int FrameCodec::writeMaxStreams(
  uint8_t *out, unsigned len, Zi::StreamType::T type, uint64_t maximum)
{
  if (!len) return -1;
  unsigned o = 0;
  out[o++] = type == Zi::StreamType::Duplex ? 0x12 : 0x13;
  if (VarInt::put(out, len, maximum, o) < 0) return -1;
  return int(o);
}

int FrameCodec::writeDataBlocked(uint8_t *out, unsigned len, uint64_t maximum)
{
  if (!len) return -1;
  unsigned o = 0;
  out[o++] = 0x14;
  if (VarInt::put(out, len, maximum, o) < 0) return -1;
  return int(o);
}

int FrameCodec::writeStreamDataBlocked(
  uint8_t *out, unsigned len, uint64_t streamID, uint64_t maximum)
{
  if (!len) return -1;
  unsigned o = 0;
  out[o++] = 0x15;
  if (VarInt::put(out, len, streamID, o) < 0 ||
      VarInt::put(out, len, maximum, o) < 0)
    return -1;
  return int(o);
}

int FrameCodec::writeStreamsBlocked(
  uint8_t *out, unsigned len, Zi::StreamType::T type, uint64_t maximum)
{
  if (!len) return -1;
  unsigned o = 0;
  out[o++] = type == Zi::StreamType::Duplex ? 0x16 : 0x17;
  if (VarInt::put(out, len, maximum, o) < 0) return -1;
  return int(o);
}

int FrameCodec::writeNewConnectionID(
  uint8_t *out, unsigned len, uint64_t sequence, uint64_t retirePriorTo,
  const CxnID &cid, const ResetToken &token)
{
  if (!len || !cid.length() || cid.length() > CxnIDMax || !token.valid())
    return -1;
  unsigned o = 0;
  out[o++] = 0x18;
  if (VarInt::put(out, len, sequence, o) < 0 ||
      VarInt::put(out, len, retirePriorTo, o) < 0)
    return -1;
  if (len < o + 1 + cid.length() + ResetToken::Length)
    return -1;
  out[o++] = uint8_t(cid.length());
  memcpy(out + o, cid.data(), cid.length());
  o += cid.length();
  memcpy(out + o, token.data(), ResetToken::Length);
  o += ResetToken::Length;
  return int(o);
}

int FrameCodec::writeRetireConnectionID(
  uint8_t *out, unsigned len, uint64_t sequence)
{
  if (!len) return -1;
  unsigned o = 0;
  out[o++] = 0x19;
  if (VarInt::put(out, len, sequence, o) < 0) return -1;
  return int(o);
}

int FrameCodec::writePathChallenge(uint8_t *out, unsigned len, ZuCSpan data)
{
  if (data.length() != 8 || len < 9) return -1;
  out[0] = 0x1a;
  memcpy(out + 1, data.data(), 8);
  return 9;
}

int FrameCodec::writePathResponse(uint8_t *out, unsigned len, ZuCSpan data)
{
  if (data.length() != 8 || len < 9) return -1;
  out[0] = 0x1b;
  memcpy(out + 1, data.data(), 8);
  return 9;
}

int FrameCodec::writeConnectionClose(uint8_t *out, unsigned len, uint64_t err)
{
  if (!len) return -1;
  unsigned o = 0;
  out[o++] = 0x1c;
  if (VarInt::put(out, len, err, o) < 0) return -1;
  if (VarInt::put(out, len, 0, o) < 0) return -1;
  if (VarInt::put(out, len, 0, o) < 0) return -1;
  return int(o);
}

int FrameCodec::writeHandshakeDone(uint8_t *out, unsigned len)
{
  if (!len) return -1;
  out[0] = 0x1e;
  return 1;
}

} // namespace Zquic
