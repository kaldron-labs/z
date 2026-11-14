//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>

#include <zlib/ZutcpOptions.hh>

namespace Zutcp {

static uint32_t get32(const uint8_t *in)
{
  return (uint32_t(in[0])<<24) | (uint32_t(in[1])<<16) |
    (uint32_t(in[2])<<8) | uint32_t(in[3]);
}

static void put32(uint8_t *out, uint32_t value)
{
  out[0] = uint8_t(value>>24);
  out[1] = uint8_t(value>>16);
  out[2] = uint8_t(value>>8);
  out[3] = uint8_t(value);
}

int TCPOptions::encodedLength(bool syn) const
{
  if ((flags & ~(MSS | WS | SACKPermitted | TS | TFO)) ||
      sackCount > MaxSACKBlocks || ((flags & WS) && scale > MaxScale)) return -1;
  if (syn ? sackCount : (flags & (MSS | WS | SACKPermitted | TFO))) return -1;
  if ((flags & TFO) && tfoLength &&
      (tfoLength < 4 || tfoLength > MaxTFOCookie || (tfoLength & 1))) return -1;
  unsigned length = 0;
  if (flags & MSS) length += 4;
  if (flags & WS) length += 3;
  if (flags & SACKPermitted) length += 2;
  if (flags & TS) length += 10;
  if (flags & TFO) length += 2 + unsigned(tfoLength);
  if (sackCount) length += 2 + (unsigned(sackCount)<<3);
  for (unsigned i = 0; i < sackCount; ++i)
    if (!sack[i].valid()) return -1;
  length = (length + 3) & ~unsigned(3);
  return length <= MaxBytes ? int(length) : -1;
}

int TCPOptions::encode(uint8_t *out, unsigned capacity, bool syn) const
{
  int length = encodedLength(syn);
  if (length < 0 || unsigned(length) > capacity) return -1;
  unsigned offset = 0;
  if (flags & MSS) {
    out[offset++] = 2;
    out[offset++] = 4;
    out[offset++] = uint8_t(mss>>8);
    out[offset++] = uint8_t(mss);
  }
  if (flags & WS) {
    out[offset++] = 3;
    out[offset++] = 3;
    out[offset++] = scale;
  }
  if (flags & SACKPermitted) {
    out[offset++] = 4;
    out[offset++] = 2;
  }
  if (flags & TS) {
    out[offset++] = 8;
    out[offset++] = 10;
    put32(out + offset, tsval);
    put32(out + offset + 4, tsecr);
    offset += 8;
  }
  if (flags & TFO) {
    out[offset++] = 34;
    out[offset++] = uint8_t(2 + unsigned(tfoLength));
    memcpy(out + offset, tfoCookie, tfoLength);
    offset += tfoLength;
  }
  if (sackCount) {
    out[offset++] = 5;
    out[offset++] = uint8_t(2 + (unsigned(sackCount)<<3));
    for (unsigned i = 0; i < sackCount; ++i) {
      put32(out + offset, sack[i].left);
      put32(out + offset + 4, sack[i].right);
      offset += 8;
    }
  }
  while (offset < unsigned(length)) out[offset++] = 0;
  return length;
}

int TCPOptions::decode(ZuBSpan bytes, bool syn)
{
  if (bytes.length() > MaxBytes) return -1;
  TCPOptions result;
  unsigned offset = 0;
  while (offset < bytes.length()) {
    unsigned kind = bytes[offset++];
    if (!kind) break;
    if (kind == 1) continue;
    if (offset >= bytes.length()) return -1;
    unsigned length = bytes[offset++];
    if (length < 2 || length - 2 > bytes.length() - offset) return -1;
    const uint8_t *value = bytes.data() + offset;
    switch (kind) {
      case 2:
	if (length != 4) return -1;
	if (syn && !(result.flags & MSS)) {
	  result.mss = uint16_t((unsigned(value[0])<<8) | value[1]);
	  result.flags |= MSS;
	}
	break;
      case 3:
	if (length != 3) return -1;
	if (syn && !(result.flags & WS)) {
	  result.scale = value[0] > MaxScale ? MaxScale : value[0];
	  result.flags |= WS;
	}
	break;
      case 4:
	if (length != 2) return -1;
	if (syn) result.flags |= SACKPermitted;
	break;
      case 5:
	if (length < 10 || ((length - 2) & 7)) return -1;
	if (!syn && !result.sackCount) {
	  unsigned count = (length - 2)>>3;
	  if (count > MaxSACKBlocks) return -1;
	  for (unsigned i = 0; i < count; ++i) {
	    result.sack[i] = {get32(value + (i<<3)), get32(value + (i<<3) + 4)};
	  }
	  result.sackCount = uint8_t(count);
	}
	break;
      case 8:
	if (length != 10) return -1;
	if (!(result.flags & TS)) {
	  result.tsval = get32(value);
	  result.tsecr = get32(value + 4);
	  result.flags |= TS;
	}
	break;
      case 34:
	if (syn && !(result.flags & TFO) &&
	    (length == 2 || (length >= 6 && length <= 18 && !(length & 1)))) {
	  result.tfoLength = uint8_t(length - 2);
	  memcpy(result.tfoCookie, value, result.tfoLength);
	  result.flags |= TFO;
	}
	break;
    }
    offset += length - 2;
  }
  *this = result;
  return int(bytes.length());
}

TCPOptionState TCPOptions::negotiate(const TCPOptions &peer) const
{
  TCPOptionState result;
  result.flags = (flags & peer.flags & (WS | TS)) | (peer.flags & MSS);
  if (peer.flags & SACKPermitted) result.flags |= TCPOptionState::SACKSend;
  if (flags & SACKPermitted) result.flags |= TCPOptionState::SACKReceive;
  if (result.flags & MSS) result.peerMSS = peer.mss;
  if (result.flags & WS) {
    result.localScale = scale > MaxScale ? MaxScale : scale;
    result.peerScale = peer.scale > MaxScale ? MaxScale : peer.scale;
  }
  return result;
}

bool TCPOptions::dsack(uint32_t packetACK) const
{
  if (!sackCount || sackCount > MaxSACKBlocks || !sack[0].valid()) return false;
  uint32_t below = packetACK - sack[0].left;
  uint32_t length = sack[0].right - sack[0].left;
  if (below < uint32_t(0x80000000) && length <= below) return true;
  if (sackCount < 2 || !sack[1].valid()) return false;
  uint32_t above = sack[0].left - packetACK;
  if (!above || above >= uint32_t(0x80000000) ||
      length >= uint32_t(0x80000000) - above) return false;
  uint32_t second = sack[1].left - packetACK;
  uint32_t enclosing = sack[1].right - sack[1].left;
  if (!second || second >= uint32_t(0x80000000) ||
      enclosing >= uint32_t(0x80000000) - second) return false;
  uint32_t offset = sack[0].left - sack[1].left;
  return offset <= enclosing && length <= enclosing - offset;
}

} // namespace Zutcp
