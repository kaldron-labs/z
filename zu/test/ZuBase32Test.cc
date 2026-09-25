//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <iostream>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuSpan.hh>
#include <zlib/ZuBase32.hh>

inline void encOut_(const char *msg, ZuCSpan actual) {
  std::cerr << msg << '\n';
  std::cerr << "  " << actual << '\n';
}

inline void decOut_(const char *msg, ZuBSpan actual) {
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

void enc(ZuBSpan src, ZuCSpan check, const char *msg) {
  ZuTestScope(enc);
  auto n = ZuBase32::enclen(src.length());
  char *buf = static_cast<char *>(ZuAlloca(n, 1));
  auto dst = ZuSpan<uint8_t>(buf, n);
  dst.trunc(ZuBase32::encode(dst, src));
  ZuCheck(ZuCSpan(dst) == check, encOut_(msg, ZuCSpan(dst)));
}

void dec(ZuBSpan src, ZuBSpan check, const char *msg)
{
  ZuTestScope(dec);
  auto n = ZuBase32::declen(src.length());
  char *buf = static_cast<char *>(ZuAlloca(n, 1));
  auto dst = ZuSpan<uint8_t>(buf, n);
  dst.trunc(ZuBase32::decode(dst, src));
  ZuCheck(ZuBSpan(dst) == check, decOut_(msg, dst));
}

void raw(ZuBSpan src, ZuCSpan check)
{
  ZuTestScope(raw);
  auto n = ZuBase32::enclen<false>(src.length());
  char *buf = static_cast<char *>(ZuAlloca(n, 1));
  auto dst = ZuSpan<uint8_t>(buf, n);
  dst.trunc(ZuBase32::encode<false>(dst, src));
  ZuCheck(ZuCSpan(dst) == check && dst.length() == n);
  auto decoded = ZuSpan<uint8_t>(
    static_cast<uint8_t *>(ZuAlloca(src.length(), 1)), src.length());
  decoded.trunc(ZuBase32::decode(decoded, dst));
  ZuCheck(ZuBSpan(decoded) == src);
}

void test(ZuBSpan src, ZuBSpan dst, const char *encMsg, const char *decMsg)
{
  ZuTestScope(test);
  ZuTestCall(enc, src, dst, encMsg);
  ZuTestCall(dec, dst, src, decMsg);
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
  TEST((ZuBSpan{ 2 }), "AI======");
  TEST((ZuBSpan{ 2, 4 }), "AICA====");
  TEST((ZuBSpan{ 2, 4, 6 }), "AICAM===");
  TEST((ZuBSpan{ 2, 4, 6, 8 }), "AICAMCA=");
  TEST((ZuBSpan{ 2, 4, 6, 8, 10 }), "AICAMCAK");
  TEST((ZuBSpan{ 2, 4, 6, 8, 10, 12 }), "AICAMCAKBQ======");
  TEST((ZuBSpan{ 0x11 }), "CE======");
  TEST((ZuBSpan{ 0x11, 0x22 }), "CERA====");
  TEST((ZuBSpan{ 0x11, 0x22, 0x33 }), "CERDG===");
  TEST((ZuBSpan{ 0x11, 0x22, 0x33, 0x44 }), "CERDGRA=");
  TEST((ZuBSpan{ 0x11, 0x22, 0x33, 0x44, 0x55 }), "CERDGRCV");
  TEST((ZuBSpan{ 0x11, 0x22, 0x33, 0x44, 0x55, 0x66 }), "CERDGRCVMY======");
  TEST((ZuBSpan{ 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77 }), "CERDGRCVMZ3Q====");
  ZuTestCall(raw, ZuBSpan{}, "");
  ZuTestCall(raw, ZuBSpan{0x11}, "CE");
  ZuTestCall(raw, (ZuBSpan{0x11, 0x22}), "CERA");
  ZuTestCall(raw, (ZuBSpan{0x11, 0x22, 0x33}), "CERDG");
  ZuTestCall(raw, (ZuBSpan{0x11, 0x22, 0x33, 0x44}), "CERDGRA");
  ZuTestCall(raw, (ZuBSpan{0x11, 0x22, 0x33, 0x44, 0x55}), "CERDGRCV");
  ZuTestCall(raw, (ZuBSpan{0x11, 0x22, 0x33, 0x44, 0x55, 0x66}), "CERDGRCVMY");
}
