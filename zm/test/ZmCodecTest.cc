//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuArray.hh>

#include <zlib/ZmCodec.hh>

using namespace ZuTestUtil;

void testBase64()
{
  ZuTestScope(testBase64);

  const uint8_t data[] = {0x00, 0x01, 0xfe, 0xff, 0x42};
  ZuArray<uint8_t, 16> decoded;

  ZmBase64::enc({data, sizeof(data)}, [&decoded](ZuSpan<uint8_t> enc) {
    ZuCheck(enc.length() <= ZmBase64::enclen(sizeof(data)));
    ZmBase64::dec(enc, [&decoded](ZuSpan<uint8_t> dec) {
      decoded.length(dec.length());
      for (unsigned i = 0; i < dec.length(); i++) decoded[i] = dec[i];
    });
  });

  ZuCheck(decoded.length() == sizeof(data));
  for (unsigned i = 0; i < sizeof(data); i++) ZuCheck(decoded[i] == data[i]);
}

void testBase32AndHexAndEmpty()
{
  ZuTestScope(testBase32AndHexAndEmpty);

  const uint8_t data[] = {0xaa, 0xbb, 0xcc, 0xdd};
  unsigned base32Len = 0;
  unsigned hexLen = 0;

  ZmBase32::enc({data, sizeof(data)}, [&base32Len, &data](
      ZuSpan<uint8_t> enc) {
    base32Len = enc.length();
    ZmBase32::dec(enc, [&data](ZuSpan<uint8_t> dec) {
      ZuCheck(dec.length() == sizeof(data));
      for (unsigned i = 0; i < sizeof(data); i++) ZuCheck(dec[i] == data[i]);
    });
  });

  ZmHex::enc({data, sizeof(data)}, [&hexLen, &data](ZuSpan<uint8_t> enc) {
    hexLen = enc.length();
    ZmHex::dec(enc, [&data](ZuSpan<uint8_t> dec) {
      ZuCheck(dec.length() == sizeof(data));
      for (unsigned i = 0; i < sizeof(data); i++) ZuCheck(dec[i] == data[i]);
    });
  });

  ZuCheck(base32Len <= ZmBase32::enclen(sizeof(data)));
  ZuCheck(hexLen == ZmHex::enclen(sizeof(data)));

  ZmBase64::enc({}, [](ZuSpan<uint8_t> enc) {
    ZuCheck(enc.length() == 0);
    ZmBase64::dec(enc, [](ZuSpan<uint8_t> dec) {
      ZuCheck(dec.length() == 0);
    });
  });
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testBase64);
  ZuTestCall(testBase32AndHexAndEmpty);
  return 0;
}
