//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

#include <iostream>

#include <zlib/ZuSpan.hh>
#include <zlib/ZuPercent.hh>

inline void encOut(bool ok, const char *msg, ZuCSpan actual) {
  std::cout << (ok ? "OK  " : "NOK ") << msg << '\n';
  if (!ok) std::cout << "  " << actual << '\n';
}

inline void decOut(bool ok, const char *msg, ZuBSpan actual) {
  std::cout << (ok ? "OK  " : "NOK ") << msg << '\n';
  if (!ok) {
    std::cout << "  ";
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
    std::cout << ZuCSpan(buf, ptr - buf) << '\n';
  }
}

void enc(ZuBSpan src, ZuCSpan check, const char *msg) {
  auto n = ZuPercent::enclen(src);
  char *buf = static_cast<char *>(ZuAlloca(n, 1));
  auto dst = ZuSpan<uint8_t>(buf, n);
  dst.trunc(ZuPercent::encode(dst, src));
  encOut(ZuCSpan(dst) == check, msg, ZuCSpan(dst));
}

void dec(ZuBSpan src, ZuBSpan check, const char *msg) {
  auto n = ZuPercent::declen(src);
  char *buf = static_cast<char *>(ZuAlloca(n, 1));
  auto dst = ZuSpan<uint8_t>(buf, n);
  dst.trunc(ZuPercent::decode(dst, src));
  decOut(ZuBSpan(dst) == check, msg, dst);
}

#define ENC(src, dst) \
  enc(src, dst, #src " -> " #dst)

#define DEC(src, dst) \
  dec(src, dst, #src " -> " #dst)

#define TEST(src, dst) ENC(src, dst); DEC(dst, src)

int main()
{
  TEST((ZuBSpan{ }), "");
  TEST((ZuBSpan{ 0xa1 }), "%A1");
  TEST((ZuBSpan{ 0xa1, 0x2b }), "%A1%2B");
  TEST((ZuBSpan{ 0xa1, 0x2b, 0xc3 }), "%A1%2B%C3");
  TEST((ZuBSpan{ 0xa1, 0x2b, 0xc3, 0x4d }), "%A1%2B%C3M");
  TEST((ZuBSpan{ 0xa1, 0x2b, 0xc3, 0x4d, 0xe5 }), "%A1%2B%C3M%E5");
  TEST((ZuBSpan{ 0xa1, 0x2b, 0xc3, 0x4d, 0xe5, 0x6f }), "%A1%2B%C3M%E5o");
  TEST((ZuBSpan{ 0xa1, 0x2b, 0xc3, 0x4d, 0xe5, 0x6f, 0xaa }), "%A1%2B%C3M%E5o%AA");
  TEST("~!@#$%^&*()_+{}[]\\|;':\",./<>?", "~%21%40%23%24%25%5E%26%2A%28%29_%2B%7B%7D%5B%5D%5C%7C%3B%27%3A%22%2C.%2F%3C%3E%3F");
  TEST("foo/bar/baz", "foo%2Fbar%2Fbaz");
  DEC("foo%2fbar%2fbaz", "foo/bar/baz");
  TEST("foo bar baz", "foo%20bar%20baz");
  DEC("foo+bar+baz", "foo bar baz");
}
