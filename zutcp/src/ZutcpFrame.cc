//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/Zutcp.hh>

namespace Zutcp {

bool FrameCodec::ackEliciting(FrameType::T t)
{
  return t != FrameType::Padding && t != FrameType::Ack;
}

static int getVar(ZuBSpan in, unsigned &o, uint64_t &v)
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
    case 0x00: {
      f.type = FrameType::Padding;
      unsigned n = in.length();
      while (o < n && !in[o]) ++o;
      used = o;
      return 0;
    }
    case 0x01:
      f.type = FrameType::Ping;
      used = o;
      return 0;
    case 0x1f: {
      uint64_t next;
      if (getVar(in, o, next) < 0 || o >= in.length() ||
	  (in[o] != 0x02 && in[o] != 0x03)) return -1;
      unsigned n;
      if (parse({in.data() + o, in.length() - o}, f, n) < 0) return -1;
      f.byteNext = next;
      used = o + n;
      return 0;
    }
    case 0x02:
    case 0x03: {
      f.type = FrameType::Ack;
      uint64_t rangeCount = 0;
      if (getVar(in, o, f.offset) < 0 ||
	  getVar(in, o, f.value) < 0 ||
	  getVar(in, o, rangeCount) < 0 ||
	  getVar(in, o, f.length) < 0)
	return -1;
      if (rangeCount + 1 > Frame::MaxAckRanges) return -1;
      if (f.length > f.offset) return -1;
      uint64_t smallest = f.offset - f.length;
      f.ackRanges.push(AckRange{f.offset, smallest});
      for (uint64_t i = 0; i < rangeCount; ++i) {
	uint64_t gap = 0, range = 0;
	if (getVar(in, o, gap) < 0 || getVar(in, o, range) < 0)
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
	if (getVar(in, o, f.ackECN.ect0) < 0 ||
	    getVar(in, o, f.ackECN.ect1) < 0 ||
	    getVar(in, o, f.ackECN.ce) < 0)
	  return -1;
      }
      used = o;
      return 0;
    }
    case 0x04:
      f.type = FrameType::Reset;
      if (getVar(in, o, f.errorCode) < 0 ||
	  getVar(in, o, f.length) < 0)
	return -1;
      used = o;
      return 0;
    case 0x05:
      f.type = FrameType::StopSending;
      if (getVar(in, o, f.errorCode) < 0)
	return -1;
      used = o;
      return 0;
    case 0x08 ... 0x0f:
      f.type = FrameType::Data;
      f.fin = t & 0x01;
      if ((t & 0x04) && getVar(in, o, f.offset) < 0) return -1;
      if (t & 0x02) {
	if (getVar(in, o, f.length) < 0) return -1;
	if (in.length() < o + f.length) return -1;
      } else
	f.length = in.length() - o;
      f.payload = ZuBSpan{in.data() + o, unsigned(f.length)};
      used = o + f.length;
      return 0;
    case 0x10:
      f.type = FrameType::MaxData;
      if (getVar(in, o, f.value) < 0) return -1;
      used = o;
      return 0;
    case 0x14:
      f.type = FrameType::DataBlocked;
      if (getVar(in, o, f.value) < 0) return -1;
      used = o;
      return 0;
    case 0x1c:
      f.type = FrameType::ConnectionClose;
      if (getVar(in, o, f.errorCode) < 0 ||
	  getVar(in, o, f.value) < 0 ||
	  getVar(in, o, f.length) < 0)
	return -1;
      if (in.length() < o + f.length) return -1;
      f.payload = ZuBSpan{in.data() + o, unsigned(f.length)};
      used = o + f.length;
      return 0;
    case 0x1d:
      f.type = FrameType::ApplicationClose;
      if (getVar(in, o, f.errorCode) < 0 ||
	  getVar(in, o, f.length) < 0)
	return -1;
      if (in.length() < o + f.length) return -1;
      f.payload = ZuBSpan{in.data() + o, unsigned(f.length)};
      used = o + f.length;
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







int FrameCodec::writeDataPrefix(
  uint8_t *out, unsigned len, uint64_t offset,
  unsigned payloadLen, bool fin)
{
  uint8_t t = 0x0a | (fin ? 0x01 : 0);
  if (offset) t |= 0x04;
  PktWriter w{out, len};
  w.put(t);
  if (offset) w.putVar(offset);
  w.putVar(payloadLen);
  return w.finish();
}

int FrameCodec::writeData(
  uint8_t *out, unsigned len, uint64_t offset,
  ZuBSpan p, bool fin)
{
  uint8_t t = 0x0a | (fin ? 0x01 : 0);
  if (offset) t |= 0x04;
  PktWriter w{out, len};
  w.put(t);
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

int FrameCodec::writeDataAck(
  uint8_t *out, unsigned len, uint64_t next,
  const AckRange *ranges, unsigned nRanges, const AckECN &ecn)
{
  PktWriter w{out, len};
  w.put(0x1f);
  w.putVar(next);
  int head = w.finish();
  if (head < 0) return -1;
  int n = writeAckECN(out + head, len - unsigned(head), ranges, nRanges, 0, ecn);
  return n < 0 ? n : head + n;
}

int FrameCodec::writeReset(
  uint8_t *out, unsigned len, uint64_t appError,
  uint64_t finalSize)
{
  PktWriter w{out, len};
  w.put(0x04);
  w.putVar(appError);
  w.putVar(finalSize);
  return w.finish();
}

int FrameCodec::writeStopSending(
  uint8_t *out, unsigned len, uint64_t appError)
{
  PktWriter w{out, len};
  w.put(0x05);
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

int FrameCodec::writeDataBlocked(uint8_t *out, unsigned len, uint64_t maximum)
{
  PktWriter w{out, len};
  w.put(0x14);
  w.putVar(maximum);
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



} // namespace Zutcp
