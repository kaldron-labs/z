//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZhttpQPack.hh>

#include <stdio.h>

using namespace ZuTestUtil;

void testQPackLiteral()
{
  ZuTestScope(testQPackLiteral);

  ZuCHECK(Zhttp::H3::QPack::staticIndex(":method", "GET") == 17,
    "QPACK static method lookup failed");
  ZuCHECK(Zhttp::H3::QPack::staticIndex(":status", "200") == 25,
    "QPACK static status lookup failed");
  ZuCHECK(Zhttp::H3::QPack::staticIndex(
      "content-type", "application/json") == 46,
    "QPACK content-type static index mismatch");
  ZuCHECK(Zhttp::H3::QPack::staticIndex(
      "x-frame-options", "sameorigin") == 98,
    "QPACK static lookup missed table tail");
  Zhttp::H3::Header staticField;
  ZuCHECK(Zhttp::H3::QPack::staticField(72, staticField) &&
      staticField.name == "accept-language" && !staticField.value.length(),
    "QPACK static field lookup missed name-only entry");

  Zhttp::H3::Params params;
  params.maxHeaderListSize(128).qpackNeverIndex("authorization");
  Zhttp::H3::Header headers[] = {
    { ":method", "GET" },
    { ":path", "/" },
    { "accept", "application/json" }
  };
  Zhttp::H3::HdrBytes bytes;
  ZuCHECK(Zhttp::H3::QPack::encodeLiteral(
    bytes, ZuSpan<Zhttp::H3::Header>{headers, 3}, params) > 0,
    "QPACK literal encode failed");
  ZuCHECK(bytes.length() > 3 && bytes[0] == 0 && bytes[1] == 0 &&
      (bytes[2] & 0xc0) == 0xc0,
    "QPACK field section did not use static indexed wire shape");

  unsigned n = 0;
  int used = Zhttp::H3::QPack::decodeLiteral(
    bytes.cspan(),
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
      ZuBSpan{badHuffmanName},
      [](Zhttp::H3::Header) { }) < 0,
    "QPACK accepted malformed Huffman-coded literal name");
  uint8_t badHuffmanValue[] = { 0x00, 0x00, 0x21, 'x', 0x81, 0x00 };
  ZuCHECK(Zhttp::H3::QPack::decodeLiteral(
      ZuBSpan{badHuffmanValue},
      [](Zhttp::H3::Header) { }) < 0,
    "QPACK accepted malformed Huffman-coded literal value");
}

void testQPackPolicy()
{
  ZuTestScope(testQPackPolicy);

  Zhttp::H3::Params params;
  params.qpackTxCapacity(256).qpackIndex("accept").qpackNeverIndex("cookie");
  for (unsigned i = 0; i < 40; ++i) {
    char name[16];
    snprintf(name, sizeof(name), "x-qpack-%u", i);
    params.qpackIndex(name);
  }
  ZuCHECK(params.indexAllowed("accept"), "QPACK index allowlist mismatch");
  ZuCHECK(params.indexAllowed("x-qpack-39"),
    "QPACK index allowlist truncated after fixed bound");
  ZuCHECK(!params.indexAllowed("cookie"), "QPACK never-index override failed");
  ZuCHECK(params.neverIndex("authorization"),
    "authorization should be never-index by default");
}

void testQPackFieldSectionPrefix()
{
  ZuTestScope(testQPackFieldSectionPrefix);

  Zhttp::H3::HdrBytes bytes;
  Zhttp::H3::FieldSectionPrefix prefix;
  ZuCHECK(Zhttp::H3::QPack::encodeFieldSectionPrefix(bytes, prefix) > 0,
    "zero field-section prefix encode failed");
  Zhttp::H3::FieldSectionPrefix decoded;
  ZuCHECK(Zhttp::H3::QPack::decodeFieldSectionPrefix(
      bytes.cspan(), decoded, 0, 0) == int(bytes.length()) &&
      !decoded.requiredInsertCount && !decoded.base &&
      Zhttp::H3::QPack::validateFieldSectionPrefix(decoded, 0),
    "zero field-section prefix decode/validate failed");

  prefix.requiredInsertCount = 2;
  prefix.base = 3;
  bytes.length(0);
  ZuCHECK(Zhttp::H3::QPack::encodeFieldSectionPrefix(bytes, prefix, 256) > 0 &&
      bytes[0] == 3 && bytes[1] == 1 &&
      Zhttp::H3::QPack::decodeFieldSectionPrefix(
	bytes.cspan(), decoded, 3, 256) == int(bytes.length()) &&
      decoded.requiredInsertCount == 2 && decoded.base == 3,
    "positive field-section base mismatch");

  prefix.requiredInsertCount = 2;
  prefix.base = 0;
  bytes.length(0);
  ZuCHECK(Zhttp::H3::QPack::encodeFieldSectionPrefix(bytes, prefix, 256) > 0 &&
      bytes[0] == 3 && bytes[1] == 0x81 &&
      Zhttp::H3::QPack::decodeFieldSectionPrefix(
	bytes.cspan(), decoded, 2, 256) == int(bytes.length()) &&
      decoded.requiredInsertCount == 2 && decoded.base == 0,
    "negative field-section base mismatch");

  prefix.requiredInsertCount = 17;
  prefix.base = 17;
  bytes.length(0);
  ZuCHECK(Zhttp::H3::QPack::encodeFieldSectionPrefix(bytes, prefix, 256) > 0 &&
      bytes[0] == 2 &&
      Zhttp::H3::QPack::decodeFieldSectionPrefix(
	bytes.cspan(), decoded, 17, 256) == int(bytes.length()) &&
      decoded.requiredInsertCount == 17 && decoded.base == 17,
    "wrapped required insert count mismatch");

  uint8_t invalid[] = { 1, 0 };
  ZuCHECK(Zhttp::H3::QPack::decodeFieldSectionPrefix(
      ZuBSpan{invalid}, decoded, 0, 0) < 0,
    "non-zero encoded insert count accepted with zero capacity");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testQPackLiteral);
  ZuTestCall(testQPackPolicy);
  ZuTestCall(testQPackFieldSectionPrefix);
}
