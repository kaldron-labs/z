//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZhttpQPack.hh>

using namespace ZuTestUtil;

void testQPackLiteral()
{
  ZuTestScope(testQPackLiteral);

  ZuCHECK(Zhttp::H3::QPack::staticIndex(":method", "GET") > 0,
    "QPACK static method lookup failed");
  ZuCHECK(Zhttp::H3::QPack::staticIndex(":status", "200") > 0,
    "QPACK static status lookup failed");
  ZuCHECK(Zhttp::H3::QPack::staticIndex(
      "content-type", "application/json") == 46,
    "QPACK content-type static index mismatch");

  Zhttp::H3::Params params;
  params.maxHeaderListSize(128).qpackNeverIndex("authorization");
  Zhttp::H3::Header headers[] = {
    { ":method", "GET" },
    { ":path", "/" },
    { "accept", "application/json" }
  };
  Zhttp::H3::HeaderBytes bytes;
  ZuCHECK(Zhttp::H3::QPack::encodeLiteral(
    bytes, ZuSpan<Zhttp::H3::Header>{headers, 3}, params) > 0,
    "QPACK literal encode failed");
  ZuCHECK(bytes.length() > 3 && bytes[0] == 0 && bytes[1] == 0 &&
      (bytes[2] & 0xc0) == 0xc0,
    "QPACK field section did not use static indexed wire shape");

  unsigned n = 0;
  int used = Zhttp::H3::QPack::decodeLiteral(
    ZuCSpan{reinterpret_cast<const char *>(bytes.data()), bytes.length()},
    [&n](Zhttp::H3::Header h) {
      if (!n) ZuCHECK(h.name == ":method" && h.value == "GET",
	"decoded first header mismatch");
      ++n;
    },
    params);
  ZuCHECK(used == int(bytes.length()), "QPACK literal decode length mismatch");
  ZuCHECK(n == 3, "QPACK literal header count mismatch");

  uint8_t badHuffmanName[] = { 0x00, 0x00, 0x29, 0x00, 0x00 };
  ZuCHECK(Zhttp::H3::QPack::decodeLiteral(
      ZuCSpan{reinterpret_cast<const char *>(badHuffmanName),
	sizeof(badHuffmanName)},
      [](Zhttp::H3::Header) { }) < 0,
    "QPACK accepted malformed Huffman-coded literal name");
  uint8_t badHuffmanValue[] = { 0x00, 0x00, 0x21, 'x', 0x81, 0x00 };
  ZuCHECK(Zhttp::H3::QPack::decodeLiteral(
      ZuCSpan{reinterpret_cast<const char *>(badHuffmanValue),
	sizeof(badHuffmanValue)},
      [](Zhttp::H3::Header) { }) < 0,
    "QPACK accepted malformed Huffman-coded literal value");
}

void testQPackPolicy()
{
  ZuTestScope(testQPackPolicy);

  Zhttp::H3::Params params;
  params.qpackTableCapacity(256).qpackIndex("accept").qpackNeverIndex("cookie");
  ZuCHECK(params.indexAllowed("accept"), "QPACK index allowlist mismatch");
  ZuCHECK(!params.indexAllowed("cookie"), "QPACK never-index override failed");
  ZuCHECK(params.neverIndex("authorization"),
    "authorization should be never-index by default");
}

void testQPackFieldSectionPrefix()
{
  ZuTestScope(testQPackFieldSectionPrefix);

  Zhttp::H3::HeaderBytes bytes;
  Zhttp::H3::FieldSectionPrefix prefix;
  ZuCHECK(Zhttp::H3::QPack::encodeFieldSectionPrefix(bytes, prefix) > 0,
    "zero field-section prefix encode failed");
  Zhttp::H3::FieldSectionPrefix decoded;
  ZuCHECK(Zhttp::H3::QPack::decodeFieldSectionPrefix(
      ZuCSpan{reinterpret_cast<const char *>(bytes.data()), bytes.length()},
      decoded) == int(bytes.length()) &&
      !decoded.requiredInsertCount &&
      !decoded.base &&
      !decoded.baseNegative &&
      Zhttp::H3::QPack::validateFieldSectionPrefix(decoded, 0),
    "zero field-section prefix decode/validate failed");

  prefix.requiredInsertCount = 2;
  prefix.base = 1;
  prefix.baseNegative = false;
  bytes.length(0);
  ZuCHECK(Zhttp::H3::QPack::encodeFieldSectionPrefix(bytes, prefix) > 0 &&
      Zhttp::H3::QPack::decodeFieldSectionPrefix(
	ZuCSpan{reinterpret_cast<const char *>(bytes.data()), bytes.length()},
	decoded) == int(bytes.length()),
    "dynamic field-section prefix round trip failed");
  uint64_t base = 0;
  ZuCHECK(Zhttp::H3::QPack::fieldSectionBase(decoded, 3, base) &&
      base == 3 &&
      !Zhttp::H3::QPack::validateFieldSectionPrefix(decoded, 2),
    "positive field-section base validation mismatch");

  prefix.requiredInsertCount = 2;
  prefix.base = 1;
  prefix.baseNegative = true;
  bytes.length(0);
  ZuCHECK(Zhttp::H3::QPack::encodeFieldSectionPrefix(bytes, prefix) > 0 &&
      Zhttp::H3::QPack::decodeFieldSectionPrefix(
	ZuCSpan{reinterpret_cast<const char *>(bytes.data()), bytes.length()},
	decoded) == int(bytes.length()) &&
      Zhttp::H3::QPack::fieldSectionBase(decoded, 2, base) &&
      base == 1,
    "negative field-section base validation mismatch");

  Zhttp::H3::Header headers[] = { { "accept", "application/json" } };
  prefix.requiredInsertCount = 1;
  prefix.base = 0;
  prefix.baseNegative = false;
  Zhttp::H3::Params params;
  ZuCHECK(Zhttp::H3::QPack::encodeLiteral(
      bytes, ZuSpan<Zhttp::H3::Header>{headers, 1}, params, prefix) > 0,
    "dynamic-prefixed literal encode failed");
  unsigned n = 0;
  ZuCHECK(Zhttp::H3::QPack::decodeLiteral(
      ZuCSpan{reinterpret_cast<const char *>(bytes.data()), bytes.length()},
      [&n](Zhttp::H3::Header) { ++n; }, params) < 0 && !n,
    "dynamic-prefixed literal decoded without insert count");
  ZuCHECK(Zhttp::H3::QPack::decodeLiteral(
      ZuCSpan{reinterpret_cast<const char *>(bytes.data()), bytes.length()},
      [&n](Zhttp::H3::Header) { ++n; }, params, 1) == int(bytes.length()) &&
      n == 1,
    "dynamic-prefixed literal did not decode with insert count");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testQPackLiteral);
  ZuTestCall(testQPackPolicy);
  ZuTestCall(testQPackFieldSectionPrefix);
}
