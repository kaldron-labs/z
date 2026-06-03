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

struct HuffmanSymbol_ {
  uint32_t	code;
  uint8_t	bits;
  uint16_t	symbol;
};

static const HuffmanSymbol_ huffman_[] = {
  {0x1ff8U, 13, 0}, {0x7fffd8U, 23, 1}, {0xfffffe2U, 28, 2}, {0xfffffe3U, 28, 3},
  {0xfffffe4U, 28, 4}, {0xfffffe5U, 28, 5}, {0xfffffe6U, 28, 6}, {0xfffffe7U, 28, 7},
  {0xfffffe8U, 28, 8}, {0xffffeaU, 24, 9}, {0x3ffffffcU, 30, 10}, {0xfffffe9U, 28, 11},
  {0xfffffeaU, 28, 12}, {0x3ffffffdU, 30, 13}, {0xfffffebU, 28, 14}, {0xfffffecU, 28, 15},
  {0xfffffedU, 28, 16}, {0xfffffeeU, 28, 17}, {0xfffffefU, 28, 18}, {0xffffff0U, 28, 19},
  {0xffffff1U, 28, 20}, {0xffffff2U, 28, 21}, {0x3ffffffeU, 30, 22}, {0xffffff3U, 28, 23},
  {0xffffff4U, 28, 24}, {0xffffff5U, 28, 25}, {0xffffff6U, 28, 26}, {0xffffff7U, 28, 27},
  {0xffffff8U, 28, 28}, {0xffffff9U, 28, 29}, {0xffffffaU, 28, 30}, {0xffffffbU, 28, 31},
  {0x14U, 6, 32}, {0x3f8U, 10, 33}, {0x3f9U, 10, 34}, {0xffaU, 12, 35},
  {0x1ff9U, 13, 36}, {0x15U, 6, 37}, {0xf8U, 8, 38}, {0x7faU, 11, 39},
  {0x3faU, 10, 40}, {0x3fbU, 10, 41}, {0xf9U, 8, 42}, {0x7fbU, 11, 43},
  {0xfaU, 8, 44}, {0x16U, 6, 45}, {0x17U, 6, 46}, {0x18U, 6, 47},
  {0x0U, 5, 48}, {0x1U, 5, 49}, {0x2U, 5, 50}, {0x19U, 6, 51},
  {0x1aU, 6, 52}, {0x1bU, 6, 53}, {0x1cU, 6, 54}, {0x1dU, 6, 55},
  {0x1eU, 6, 56}, {0x1fU, 6, 57}, {0x5cU, 7, 58}, {0xfbU, 8, 59},
  {0x7ffcU, 15, 60}, {0x20U, 6, 61}, {0xffbU, 12, 62}, {0x3fcU, 10, 63},
  {0x1ffaU, 13, 64}, {0x21U, 6, 65}, {0x5dU, 7, 66}, {0x5eU, 7, 67},
  {0x5fU, 7, 68}, {0x60U, 7, 69}, {0x61U, 7, 70}, {0x62U, 7, 71},
  {0x63U, 7, 72}, {0x64U, 7, 73}, {0x65U, 7, 74}, {0x66U, 7, 75},
  {0x67U, 7, 76}, {0x68U, 7, 77}, {0x69U, 7, 78}, {0x6aU, 7, 79},
  {0x6bU, 7, 80}, {0x6cU, 7, 81}, {0x6dU, 7, 82}, {0x6eU, 7, 83},
  {0x6fU, 7, 84}, {0x70U, 7, 85}, {0x71U, 7, 86}, {0x72U, 7, 87},
  {0xfcU, 8, 88}, {0x73U, 7, 89}, {0xfdU, 8, 90}, {0x1ffbU, 13, 91},
  {0x7fff0U, 19, 92}, {0x1ffcU, 13, 93}, {0x3ffcU, 14, 94}, {0x22U, 6, 95},
  {0x7ffdU, 15, 96}, {0x3U, 5, 97}, {0x23U, 6, 98}, {0x4U, 5, 99},
  {0x24U, 6, 100}, {0x5U, 5, 101}, {0x25U, 6, 102}, {0x26U, 6, 103},
  {0x27U, 6, 104}, {0x6U, 5, 105}, {0x74U, 7, 106}, {0x75U, 7, 107},
  {0x28U, 6, 108}, {0x29U, 6, 109}, {0x2aU, 6, 110}, {0x7U, 5, 111},
  {0x2bU, 6, 112}, {0x76U, 7, 113}, {0x2cU, 6, 114}, {0x8U, 5, 115},
  {0x9U, 5, 116}, {0x2dU, 6, 117}, {0x77U, 7, 118}, {0x78U, 7, 119},
  {0x79U, 7, 120}, {0x7aU, 7, 121}, {0x7bU, 7, 122}, {0x7ffeU, 15, 123},
  {0x7fcU, 11, 124}, {0x3ffdU, 14, 125}, {0x1ffdU, 13, 126}, {0x3fffffffU, 30, 256},
};

int QPack::decodeHuffman(HeaderBytes &out, ZuCSpan in)
{
  out.length(0);
  uint32_t code = 0;
  unsigned bits = 0;
  for (unsigned i = 0; i < in.length(); ++i) {
    uint8_t byte = uint8_t(in[i]);
    for (int bit = 7; bit >= 0; --bit) {
      code = (code << 1) | ((byte >> bit) & 1U);
      ++bits;
      bool matched = false;
      for (auto &sym : huffman_) {
	if (sym.bits != bits || sym.code != code) continue;
	if (sym.symbol == 256) return -1;
	out.push(uint8_t(sym.symbol));
	code = 0;
	bits = 0;
	matched = true;
	break;
      }
      if (!matched && bits > 30) return -1;
    }
  }
  if (!bits) return int(out.length());
  if (bits > 7 || code != ((1U << bits) - 1U)) return -1;
  return int(out.length());
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
