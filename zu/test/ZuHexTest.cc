//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <iostream>

#include <zlib/ZuSpan.hh>
#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuHex.hh>

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
  auto n = ZuHex::enclen(src.length());
  char *buf = static_cast<char *>(ZuAlloca(n, 1));
  auto dst = ZuSpan<uint8_t>(buf, n);
  dst.trunc(ZuHex::encode(dst, src));
  ZuCheck(ZuCSpan(dst) == check, encOut_(msg, ZuCSpan(dst)));
}

void dec(ZuBSpan src, ZuBSpan check, const char *msg)
{
  ZuTestScope(dec);
  auto n = ZuHex::declen(src.length());
  char *buf = static_cast<char *>(ZuAlloca(n, 1));
  auto dst = ZuSpan<uint8_t>(buf, n);
  dst.trunc(ZuHex::decode(dst, src));
  ZuCheck(ZuBSpan(dst) == check, decOut_(msg, dst));
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
  TEST((ZuBSpan{ 2 }), "02");
  TEST((ZuBSpan{ 2, 4 }), "0204");
  TEST((ZuBSpan{ 2, 4, 6 }), "020406");
  TEST((ZuBSpan{ 2, 4, 6, 8 }), "02040608");
  TEST((ZuBSpan{ 2, 4, 6, 8, 10 }), "020406080A");
  TEST((ZuBSpan{ 2, 4, 6, 8, 10, 12 }), "020406080A0C");
  TEST((ZuBSpan{ 0xa1 }), "A1");
  TEST((ZuBSpan{ 0xa1, 0x2b }), "A12B");
  TEST((ZuBSpan{ 0xa1, 0x2b, 0xc3 }), "A12BC3");
  TEST((ZuBSpan{ 0xa1, 0x2b, 0xc3, 0x4d }), "A12BC34D");
  TEST((ZuBSpan{ 0xa1, 0x2b, 0xc3, 0x4d, 0xe5 }), "A12BC34DE5");
  TEST((ZuBSpan{ 0xa1, 0x2b, 0xc3, 0x4d, 0xe5, 0x6f }), "A12BC34DE56F");
  TEST((ZuBSpan{ 0xa1, 0x2b, 0xc3, 0x4d, 0xe5, 0x6f, 0xaa }), "A12BC34DE56FAA");
}
