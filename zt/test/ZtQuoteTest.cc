//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZuBase32.hh>
#include <zlib/ZuBase64.hh>
#include <zlib/ZuHex.hh>

#include <zlib/ZtArray.hh>
#include <zlib/ZtString.hh>
#include <zlib/ZtQuote.hh>

using namespace ZuTestUtil;

static ZuBSpan asBytes(const ZtString<> &s)
{
  return ZuBSpan(reinterpret_cast<const uint8_t *>(s.data()), s.length());
}

void testStringQuoting()
{
  ZuTestScope(testStringQuoting);

  ZtString<> s;
  s << ZtQuote::CString{"hello \"world\""};
  ZuCheck(s == "\"hello \\\"world\\\"\"");

  s.null();
  s << ZtQuote::String{ZuCSpan{"x\"y"}};
  ZuCheck(s == "\"x\\\"y\"");
}

void testBinaryWrappersMatchCodecs()
{
  ZuTestScope(testBinaryWrappersMatchCodecs);

  uint8_t raw[] = { 0x10, 0x20, 0x30, 0x40, 0x50 };
  ZuBSpan input(raw, sizeof(raw));

  ZtString<> b32;
  b32 << ZtQuote::Base32{input};
  ZtArray<uint8_t> d32;
  d32.length(ZuBase32::declen(b32.length()));
  unsigned l32 = ZuBase32::decode(
    ZuSpan<uint8_t>(d32.data(), d32.length()), asBytes(b32));
  ZuCheck(l32 == sizeof(raw));

  ZtString<> b64;
  b64 << ZtQuote::Base64{input};
  ZtArray<uint8_t> d64;
  d64.length(ZuBase64::declen(b64.length()));
  unsigned l64 = ZuBase64::decode(
    ZuSpan<uint8_t>(d64.data(), d64.length()), asBytes(b64));
  ZuCheck(l64 == sizeof(raw));

  ZtString<> hex;
  hex << ZtQuote::Hex{input};
  ZtArray<uint8_t> dhex;
  dhex.length(ZuHex::declen(hex.length()));
  unsigned lhex = ZuHex::decode(
    ZuSpan<uint8_t>(dhex.data(), dhex.length()), asBytes(hex));
  ZuCheck(lhex == sizeof(raw));
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testStringQuoting);
  ZuTestCall(testBinaryWrappersMatchCodecs);
  return 0;
}
