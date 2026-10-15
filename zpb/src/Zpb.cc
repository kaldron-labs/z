//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <limits.h>

#include <zlib/ZuByteSwap.hh>

#include <zlib/Zpb.hh>

namespace Zpb::Wire {

ZtEnumImpl(WireType);

uint8_t *prependVarint(uint8_t *end, uint64_t n)
{
  unsigned size = varintSize(n);
  auto p = end - size;
  for (unsigned i = 0; i < size; ++i) {
    p[i] = uint8_t(n) | (i + 1 < size ? 0x80 : 0);
    n >>= 7;
  }
  return p;
}

bool readVarint(ZuBSpan bytes, unsigned &pos, uint64_t &value)
{
  uint64_t result = 0;
  for (unsigned i = 0; i < 10; ++i) {
    if (pos >= bytes.length()) return false;
    uint8_t byte = bytes[pos++];
    if (i == 9 && byte > 1) return false;
    result |= uint64_t(byte & 0x7f) << (i * 7);
    if (!(byte & 0x80)) { value = result; return true; }
  }
  return false;
}

bool readTag(ZuBSpan bytes, unsigned &pos, uint32_t &field, T &type)
{
  uint64_t tag;
  if (!readVarint(bytes, pos, tag) || tag > UINT32_MAX) return false;
  auto n = uint32_t(tag >> 3);
  // Schema declarations cannot use the reserved interval, but an unknown
  // wire field there still has a valid tag and must be skippable.
  if (!n || n > MaxField) return false;
  auto wire = uint8_t(tag & 7);
  switch (wire) {
    case Varint:
    case Fixed64:
    case Length:
    case StartGroup:
    case EndGroup:
    case Fixed32:
      field = n;
      type = T(wire);
      return true;
    default:
      return false;
  }
}

bool readLength(ZuBSpan bytes, unsigned &pos, ZuBSpan &value)
{
  uint64_t n;
  if (!readVarint(bytes, pos, n) || n > bytes.length() - pos) return false;
  value = {bytes.data() + pos, unsigned(n)};
  pos += unsigned(n);
  return true;
}

static bool skipValue(ZuBSpan bytes, unsigned &pos, T type)
{
  if (pos > bytes.length()) return false;
  switch (type) {
    case Varint: {
      uint64_t n;
      return readVarint(bytes, pos, n);
    }
    case Fixed64:
      if (bytes.length() - pos < 8) return false;
      pos += 8;
      return true;
    case Length: {
      ZuBSpan value;
      return readLength(bytes, pos, value);
    }
    case Fixed32:
      if (bytes.length() - pos < 4) return false;
      pos += 4;
      return true;
    case StartGroup:
    case EndGroup:
      return false;
  }
  return false;
}

bool skip(ZuBSpan bytes, unsigned &pos, uint32_t field, T type,
  unsigned maxDepth)
{
  if (type != StartGroup) return skipValue(bytes, pos, type);
  if (!maxDepth) return false;

  ZtArray<uint32_t, ZtArrayHeapID<"Zpb.Groups">> groups;
  groups.push(field);
  while (pos < bytes.length()) {
    uint32_t next;
    T wire;
    if (!readTag(bytes, pos, next, wire)) return false;
    switch (wire) {
      case StartGroup:
	if (groups.length() >= maxDepth) return false;
	groups.push(next);
	break;
      case EndGroup:
	if (groups.pop() != next) return false;
	if (!groups.length()) return true;
	break;
      default:
	if (!skipValue(bytes, pos, wire)) return false;
    }
  }
  return false;
}

bool unframe(ZuBSpan frame, ZuBSpan &payload)
{
  if (frame.length() < sizeof(uint32_t)) return false;
  ZuLittleEndian<uint32_t> size;
  memcpy(&size, frame.data(), sizeof(size));
  if (uint32_t(size) != frame.length() - sizeof(size)) return false;
  payload = {frame.data() + sizeof(size), uint32_t(size)};
  return true;
}

} // Zpb::Wire
