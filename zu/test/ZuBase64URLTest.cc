//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <iostream>

#include <zlib/ZuSpan.hh>
#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuBase64URL.hh>

inline void encOut_(const char *msg, ZuCSpan actual)
{
  std::cerr << msg << '\n';
  std::cerr << "  " << actual << '\n';
}

inline void decOut_(const char *msg, ZuBSpan actual)
{
  std::cerr << msg << '\n';
  std::cerr << "  ";
  unsigned n = actual.length();
  char *buf = static_cast<char *>(ZuAlloca(n * 3, 1));
  char *ptr = buf;
  for (unsigned i = 0; i < n; i++) {
    if (i) *ptr++ = ' ';
    auto c = actual[i];
    static auto hex = [](uint8_t v) {
      return v < 10 ? v + '0' : (v - 10) + 'A';
    };
    *ptr++ = hex(c>>4);
    *ptr++ = hex(c & 0xf);
  }
  std::cerr << ZuCSpan(buf, ptr - buf) << '\n';
}

void enc(ZuBSpan src, ZuCSpan check, const char *msg)
{
  ZuTestScope(enc);
  auto n = ZuBase64URL::enclen(src.length());
  char *buf = static_cast<char *>(ZuAlloca(n, 1));
  auto dst = ZuSpan<uint8_t>(buf, n);
  dst.trunc(ZuBase64URL::encode(dst, src));
  ZuCheck(ZuCSpan(dst) == check && n == dst.length(), encOut_(msg, ZuCSpan(dst)));
}

void dec(ZuBSpan src, ZuBSpan check, const char *msg)
{
  ZuTestScope(dec);
  auto n = ZuBase64URL::declen(src.length());
  char *buf = static_cast<char *>(ZuAlloca(n, 1));
  auto dst = ZuSpan<uint8_t>(buf, n);
  dst.trunc(ZuBase64URL::decode(dst, src));
  ZuCheck(ZuBSpan(dst) == check && n == dst.length(), decOut_(msg, dst));
}

void test(ZuBSpan src, ZuBSpan dst, const char *encMsg, const char *decMsg)
{
  ZuTestScope(test);
  ZuTestCall(enc, src, dst, encMsg);
  ZuTestCall(dec, dst, src, decMsg);
}

void invalid()
{
  ZuTestScope(invalid);
  uint8_t data[8] = { 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5 };
  auto n = ZuBase64URL::decode(data, ZuBSpan{"_-A"});
  ZuCheck(n == 2 && data[0] == 0xff && data[1] == 0xe0);

  ZuCheck(ZuBase64URL::decode(data, ZuBSpan{"AA="}) !=
    ZuBase64URL::declen(3));
}

#define TEST_(src, dst, src_q, dst_q) ZuTestCall_( \
    src_q " -> " dst_q, test, \
    src, dst, src_q " -> " dst_q, dst_q " -> " src_q)
#define TEST(src, dst) TEST_(src, dst, \
    ZuPP_Eval(ZuPP_Defer(ZuPP_Q)(ZuPP_Strip(src))), \
    ZuPP_Eval(ZuPP_Defer(ZuPP_Q)(ZuPP_Strip(dst))))

int main()
{
  ZuTestMain();
  TEST((ZuBSpan{ }), "");
  TEST((ZuBSpan{ 2 }), "Ag");
  TEST((ZuBSpan{ 2, 4 }), "AgQ");
  TEST((ZuBSpan{ 2, 4, 6 }), "AgQG");
  TEST((ZuBSpan{ 0x11 }), "EQ");
  TEST((ZuBSpan{ 0x11, 0x22 }), "ESI");
  TEST((ZuBSpan{ 0x11, 0x22, 0x33 }), "ESIz");
  TEST((ZuBSpan{ 0x11, 0x22, 0x33, 0x44 }), "ESIzRA");
  TEST((ZuBSpan{ 0x11, 0x22, 0x33, 0x44, 0x55 }), "ESIzRFU");
  TEST((ZuBSpan{ 0x11, 0x22, 0x33, 0x44, 0x55, 0x66 }), "ESIzRFVm");
  TEST((ZuBSpan{ 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77 }), "ESIzRFVmdw");
  TEST((ZuBSpan{ 0xff, 0xe0 }), "_-A");
  ZuTestCall(invalid);
}
