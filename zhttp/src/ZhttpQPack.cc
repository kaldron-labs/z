//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZhttpQPack.hh>

namespace Zhttp { namespace H3 {

static int putPref_(HeaderBytes &out, uint8_t prefix, unsigned prefixBits,
  uint64_t v)
{
  if (!prefixBits || prefixBits > 8) return -1;
  uint8_t mask = uint8_t((1U << prefixBits) - 1U);
  if (v < mask) {
    out.push(prefix | uint8_t(v));
    return 0;
  }

  out.push(prefix | mask);
  v -= mask;
  while (v >= 128) {
    out.push(uint8_t((v & 0x7f) | 0x80));
    v >>= 7;
  }
  out.push(uint8_t(v));
  return 0;
}

static int putString_(HeaderBytes &out, uint8_t prefix, unsigned prefixBits,
  ZuCSpan s)
{
  if (putPref_(out, prefix, prefixBits, s.length()) < 0) return -1;
  CodecBytes::putSpan(out, s);
  return 0;
}

int QPack::decodeHuffman(HeaderBytes &out, ZuCSpan in)
{
  out.length(HPack::declen(in.length()));
  int64_t n = HPack::decode(
    ZuSpan<uint8_t>{out.data(), out.length()},
    ZuBSpan{reinterpret_cast<const uint8_t *>(in.data()), in.length()});
  if (n < 0) return -1;
  out.length(uint64_t(n));
  return int(n);
}

int QPack::decodeString(
  HeaderBytes &storage, ZuCSpan in, unsigned &o, unsigned prefixBits,
  uint8_t huffmanMask, ZuCSpan &out)
{
  uint64_t len = 0;
  uint8_t first = 0;
  if (qpackDecodePrefInt_(in, o, prefixBits, len, &first) < 0 ||
      in.length() < o + len)
    return -1;
  ZuCSpan raw{in.data() + o, unsigned(len)};
  o += unsigned(len);
  if (!(first & huffmanMask)) {
    out = raw;
    return int(raw.length());
  }
  if (decodeHuffman(storage, raw) < 0) return -1;
  out = ZuCSpan{
    reinterpret_cast<const char *>(storage.data()), storage.length()};
  return int(out.length());
}

bool QPack::staticNameIndex(ZuCSpan name, uint64_t &index)
{
  if (name == ":authority") { index = 0; return true; }
  if (name == ":path") { index = 1; return true; }
  if (name == "accept") { index = 29; return true; }
  if (name == "accept-encoding") { index = 31; return true; }
  if (name == "authorization") { index = 84; return true; }
  if (name == "content-length") { index = 4; return true; }
  if (name == "content-type") { index = 46; return true; }
  if (name == "cookie") { index = 5; return true; }
  if (name == "date") { index = 6; return true; }
  if (name == "server") { index = 92; return true; }
  if (name == "user-agent") { index = 95; return true; }
  return false;
}

int QPack::staticIndex(ZuCSpan name, ZuCSpan value)
{
  if (name == ":method" && value == "GET") return 17;
  if (name == ":method" && value == "POST") return 20;
  if (name == ":authority" && !value.length()) return 0;
  if (name == ":scheme" && value == "http") return 22;
  if (name == ":scheme" && value == "https") return 23;
  if (name == ":path" && value == "/") return 1;
  if (name == ":status" && value == "100") return 63;
  if (name == ":status" && value == "103") return 24;
  if (name == ":status" && value == "200") return 25;
  if (name == ":status" && value == "204") return 64;
  if (name == ":status" && value == "304") return 26;
  if (name == ":status" && value == "404") return 27;
  if (name == ":status" && value == "503") return 28;
  if (name == "accept" && value == "*/*") return 29;
  if (name == "accept-encoding" && value == "gzip, deflate, br") return 31;
  if (name == "content-length" && value == "0") return 4;
  if (name == "content-type" && value == "application/json") return 46;
  if (name == "content-type" && value == "text/plain") return 53;
  return -1;
}

bool QPack::staticField(uint64_t index, Header &field)
{
  if (index == 0) { field = Header{":authority", ""}; return true; }
  if (index == 1) { field = Header{":path", "/"}; return true; }
  if (index == 2) { field = Header{"age", "0"}; return true; }
  if (index == 4) { field = Header{"content-length", "0"}; return true; }
  if (index == 5) { field = Header{"cookie", ""}; return true; }
  if (index == 6) { field = Header{"date", ""}; return true; }
  if (index == 17 || index == 20) {
    field = Header{":method", index == 17 ? "GET" : "POST"};
    return true;
  }
  if (index == 22) { field = Header{":scheme", "http"}; return true; }
  if (index == 23) {
    field = Header{":scheme", "https"};
    return true;
  }
  if (index == 24) { field = Header{":status", "103"}; return true; }
  if (index == 25) { field = Header{":status", "200"}; return true; }
  if (index == 26) { field = Header{":status", "304"}; return true; }
  if (index == 27) { field = Header{":status", "404"}; return true; }
  if (index == 28) { field = Header{":status", "503"}; return true; }
  if (index == 29) { field = Header{"accept", "*/*"}; return true; }
  if (index == 30) {
    field = Header{"accept", "application/dns-message"};
    return true;
  }
  if (index == 31) {
    field = Header{"accept-encoding", "gzip, deflate, br"};
    return true;
  }
  if (index >= 44 && index <= 54) {
    ZuCSpan value = "";
    if (index == 44) value = "application/dns-message";
    else if (index == 45) value = "application/javascript";
    else if (index == 46) value = "application/json";
    else if (index == 47) value = "application/x-www-form-urlencoded";
    else if (index == 48) value = "image/gif";
    else if (index == 49) value = "image/jpeg";
    else if (index == 50) value = "image/png";
    else if (index == 51) value = "text/css";
    else if (index == 52) value = "text/html; charset=utf-8";
    else if (index == 53) value = "text/plain";
    else if (index == 54) value = "text/plain;charset=utf-8";
    field = Header{"content-type", value};
    return true;
  }
  if (index == 63) { field = Header{":status", "100"}; return true; }
  if (index == 64) { field = Header{":status", "204"}; return true; }
  if (index == 65) { field = Header{":status", "206"}; return true; }
  if (index == 66) { field = Header{":status", "302"}; return true; }
  if (index == 67) { field = Header{":status", "400"}; return true; }
  if (index == 68) { field = Header{":status", "403"}; return true; }
  if (index == 69) { field = Header{":status", "421"}; return true; }
  if (index == 70) { field = Header{":status", "425"}; return true; }
  if (index == 71) { field = Header{":status", "500"}; return true; }
  if (index == 84) { field = Header{"authorization", ""}; return true; }
  if (index == 92) { field = Header{"server", ""}; return true; }
  if (index == 95) { field = Header{"user-agent", ""}; return true; }
  return false;
}

bool QPack::staticName(uint64_t index, HeaderName &name)
{
  Header field;
  if (!staticField(index, field)) return false;
  name = field.name;
  return true;
}

int QPack::encodeFieldSectionPrefix(
  HeaderBytes &out, const FieldSectionPrefix &prefix)
{
  if (putPref_(out, 0, 8, prefix.requiredInsertCount) < 0 ||
      putPref_(out, prefix.baseNegative ? 0x80 : 0x00, 7, prefix.base) < 0)
    return -1;
  return out.length();
}

int QPack::decodeFieldSectionPrefix(ZuCSpan in, FieldSectionPrefix &prefix)
{
  prefix = {};
  unsigned o = 0;
  uint8_t baseFirst = 0;
  if (qpackDecodePrefInt_(in, o, 8, prefix.requiredInsertCount) < 0 ||
      qpackDecodePrefInt_(in, o, 7, prefix.base, &baseFirst) < 0)
    return -1;
  prefix.baseNegative = baseFirst & 0x80;
  return int(o);
}

bool QPack::fieldSectionBase(
  const FieldSectionPrefix &prefix, uint64_t insertCount, uint64_t &base)
{
  base = 0;
  if (!prefix.requiredInsertCount) {
    if (prefix.base || prefix.baseNegative) return false;
    return true;
  }
  if (prefix.requiredInsertCount > insertCount) return false;
  if (prefix.baseNegative) {
    if (prefix.base > prefix.requiredInsertCount) return false;
    base = prefix.requiredInsertCount - prefix.base;
  } else {
    if (prefix.base > uint64_t(-1) - prefix.requiredInsertCount)
      return false;
    base = prefix.requiredInsertCount + prefix.base;
  }
  return base <= insertCount;
}

bool QPack::validateFieldSectionPrefix(
  const FieldSectionPrefix &prefix, uint64_t insertCount)
{
  uint64_t base = 0;
  return fieldSectionBase(prefix, insertCount, base);
}

int QPack::encodeFieldLine(HeaderBytes &out, Header h, const Params &params)
{
  int staticIndex = QPack::staticIndex(h.name, h.value);
  if (staticIndex >= 0)
    return putPref_(out, 0xc0, 6, unsigned(staticIndex)) < 0 ?
      -1 : int(out.length());

  uint64_t nameIndex = 0;
  if (staticNameIndex(h.name, nameIndex)) {
    uint8_t prefix = uint8_t(0x50 | (params.neverIndex(h.name) ? 0x20 : 0));
    return putPref_(out, prefix, 4, nameIndex) < 0 ||
      putString_(out, 0x00, 7, h.value) < 0 ? -1 : int(out.length());
  }

  return putString_(
    out, uint8_t(0x20 | (params.neverIndex(h.name) ? 0x10 : 0)),
    3, h.name) < 0 ||
    putString_(out, 0x00, 7, h.value) < 0 ? -1 : int(out.length());
}

int QPack::encodeDynamicIndexed(HeaderBytes &out, uint64_t relativeIndex)
{
  return putPref_(out, 0x80, 6, relativeIndex) < 0 ?
    -1 : int(out.length());
}

int QPack::encodeDynamicNameRef(
  HeaderBytes &out, uint64_t relativeIndex, ZuCSpan value, bool neverIndex)
{
  uint8_t prefix = uint8_t(0x40 | (neverIndex ? 0x20 : 0));
  return putPref_(out, prefix, 4, relativeIndex) < 0 ||
    putString_(out, 0x00, 7, value) < 0 ? -1 : int(out.length());
}

int QPack::encodeLiteral(HeaderBytes &out, ZuSpan<Header> headers,
    const Params &params, const FieldSectionPrefix &prefix)
{
  out.length(0);
  if (encodeFieldSectionPrefix(out, prefix) < 0) return -1;
  unsigned headerBytes = 0;
  for (auto &h : headers) {
    if (params.neverIndex(h.name) && params.qpackTableCapacity() &&
	params.indexAllowed(h.name))
      return -1;
    headerBytes += h.name.length() + h.value.length();
    if (headerBytes > params.maxHeaderListSize()) return -1;
    if (encodeFieldLine(out, h, params) < 0) return -1;
  }
  return out.length();
}

int QPack::encodeSetCapacity(HeaderBytes &out, uint64_t capacity)
{
  out.length(0);
  if (CodecBytes::putVar(out, 0x01) < 0 ||
      CodecBytes::putVar(out, capacity) < 0)
    return -1;
  return out.length();
}

int QPack::encodeInsertWithNameRef(
  HeaderBytes &out, uint64_t index, bool dynamic, ZuCSpan value)
{
  out.length(0);
  if (CodecBytes::putVar(out, 0x07) < 0 ||
      CodecBytes::putVar(out, dynamic ? 1 : 0) < 0 ||
      CodecBytes::putVar(out, index) < 0 ||
      CodecBytes::putVar(out, value.length()) < 0)
    return -1;
  CodecBytes::putSpan(out, value);
  return out.length();
}

int QPack::encodeInsertLiteral(HeaderBytes &out, Header h)
{
  out.length(0);
  if (CodecBytes::putVar(out, 0x02) < 0 ||
      CodecBytes::putVar(out, h.name.length()) < 0)
    return -1;
  CodecBytes::putSpan(out, h.name);
  if (CodecBytes::putVar(out, h.value.length()) < 0) return -1;
  CodecBytes::putSpan(out, h.value);
  return out.length();
}

int QPack::encodeDuplicate(HeaderBytes &out, uint64_t index)
{
  out.length(0);
  if (CodecBytes::putVar(out, 0x06) < 0 ||
      CodecBytes::putVar(out, index) < 0)
    return -1;
  return out.length();
}

int QPack::encodeSectionAck(HeaderBytes &out, uint64_t streamID)
{
  out.length(0);
  if (CodecBytes::putVar(out, 0x03) < 0 ||
      CodecBytes::putVar(out, streamID) < 0)
    return -1;
  return out.length();
}

int QPack::encodeStreamCancellation(HeaderBytes &out, uint64_t streamID)
{
  out.length(0);
  if (CodecBytes::putVar(out, 0x04) < 0 ||
      CodecBytes::putVar(out, streamID) < 0)
    return -1;
  return out.length();
}

int QPack::encodeInsertCountIncrement(HeaderBytes &out, uint64_t n)
{
  out.length(0);
  if (CodecBytes::putVar(out, 0x05) < 0 ||
      CodecBytes::putVar(out, n) < 0)
    return -1;
  return out.length();
}

static int getVar_(ZuCSpan in, unsigned &o, uint64_t &v)
{
  unsigned n = 0;
  if (Zquic::VarInt::decode(ZuCSpan{in.data() + o, in.length() - o}, v, n) < 0)
    return -1;
  o += n;
  return 0;
}

int QPack::decodeInstructionOne(ZuCSpan in, QPackDecodedInstruction &i)
{
  uint64_t type = 0;
  unsigned o = 0;
  if (getVar_(in, o, type) < 0) return -1;
  i = {};

  if (type == 0x01) {
    i.type = QPackInstruction::SetCapacity;
    return getVar_(in, o, i.value) < 0 ? -1 : int(o);
  }
  if (type == 0x02) {
    uint64_t nlen = 0, vlen = 0;
    i.type = QPackInstruction::InsertWithoutNameRef;
    if (getVar_(in, o, nlen) < 0 || nlen > in.length() - o) return -1;
    i.header.name = ZuCSpan{in.data() + o, unsigned(nlen)};
    o += nlen;
    if (getVar_(in, o, vlen) < 0 || vlen > in.length() - o) return -1;
    i.header.value = ZuCSpan{in.data() + o, unsigned(vlen)};
    o += vlen;
    return int(o);
  }
  if (type == 0x07) {
    uint64_t dynamic = 0, vlen = 0;
    i.type = QPackInstruction::InsertWithNameRef;
    if (getVar_(in, o, dynamic) < 0 || dynamic > 1 ||
	getVar_(in, o, i.value) < 0 ||
	getVar_(in, o, vlen) < 0 || vlen > in.length() - o)
      return -1;
    i.nameRefDynamic = dynamic;
    i.header.value = ZuCSpan{in.data() + o, unsigned(vlen)};
    o += vlen;
    return int(o);
  }
  if (type == 0x03) {
    i.type = QPackInstruction::SectionAck;
    return getVar_(in, o, i.value) < 0 ? -1 : int(o);
  }
  if (type == 0x04) {
    i.type = QPackInstruction::StreamCancellation;
    return getVar_(in, o, i.value) < 0 ? -1 : int(o);
  }
  if (type == 0x05) {
    i.type = QPackInstruction::InsertCountIncrement;
    return getVar_(in, o, i.value) < 0 ? -1 : int(o);
  }
  if (type == 0x06) {
    i.type = QPackInstruction::Duplicate;
    return getVar_(in, o, i.value) < 0 ? -1 : int(o);
  }
  return -1;
}

int QPack::decodeInstruction(ZuCSpan in, QPackDecodedInstruction &i)
{
  int n = decodeInstructionOne(in, i);
  return n < 0 || n != int(in.length()) ? -1 : n;
}

}} // namespace Zhttp::H3
