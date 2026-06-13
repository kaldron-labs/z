//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuPercent.hh>
#include <zlib/ZuTestUtil.hh>

using namespace ZuTestUtil;

struct Esc {
  static constexpr bool esc(uint8_t c) {
    static constexpr uint8_t map[] = {
      0xed, 0x00, 0x00, 0x70, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x80
    };
    if (c < 32 || c >= 128) return true;
    c -= 32;
    return map[c>>3] & (1U<<(c & 7));
  }
};

struct Path : public Esc {
  static constexpr bool term(uint8_t c) {
    return c == '/' || c == '?' || esc(c);
  }
  static constexpr bool plus() { return false; }
  static constexpr bool spacePlus() { return false; }
};

struct Query : public Esc {
  static constexpr bool term(uint8_t c) { return esc(c); }
  static constexpr bool plus() { return true; }
  static constexpr bool spacePlus() { return false; }
};

struct Quote : public Esc {
  static constexpr bool term(uint8_t) { return false; }
  static constexpr bool plus() { return false; }
  static constexpr bool spacePlus() { return false; }
};

struct Body : public Esc {
  static constexpr bool term(uint8_t) { return false; }
  static constexpr bool plus() { return true; }
  static constexpr bool spacePlus() { return true; }
};

struct Stream {
  char		*ptr;

  Stream(char *ptr_) : ptr{ptr_} { }

  Stream &operator <<(char c) {
    *ptr++ = c;
    return *this;
  }
};

template <typename Policy>
ZuCSpan encode(ZuBSpan src, ZuSpan<char> buf)
{
  auto n = ZuPercent::Codec<Policy>::encode(buf, src);
  return {buf.data(), n};
}

void hex()
{
  ZuTestScope(hex);

  for (char c = '0'; c <= '9'; c++)
    ZuCheck(ZuPercent::hex(c) == c - '0');
  for (char c = 'a'; c <= 'f'; c++)
    ZuCheck(ZuPercent::hex(c) == c - 'a' + 10);
  for (char c = 'A'; c <= 'F'; c++)
    ZuCheck(ZuPercent::hex(c) == c - 'A' + 10);
  ZuCheck(ZuPercent::hex('g') < 0);
  ZuCheck(ZuPercent::hex('/') < 0);
  ZuCheck(ZuPercent::digit(0) == '0');
  ZuCheck(ZuPercent::digit(10) == 'A');
  ZuCheck(ZuPercent::digit(15) == 'F');
}

void span()
{
  ZuTestScope(span);

  auto a = ZuPercent::Codec<Quote>::span("hello");
  ZuCheck(a.inLen == 5 && a.outLen == 5);
  ZuCheck(ZuPercent::Codec<Quote>::len("a b") == 5);
  ZuCheck(ZuPercent::Codec<Quote>::len(" %&") == 9);
  ZuCheck(ZuPercent::Codec<Body>::len("a b") == 3);
}

void encode()
{
  ZuTestScope(encode);

  char buf[32];
  ZuCheck(encode<Quote>("hello", ZuSpan<char>{buf, sizeof(buf)}) == "hello");
  ZuCheck(encode<Quote>("a b", ZuSpan<char>{buf, sizeof(buf)}) == "a%20b");
  ZuCheck(encode<Body>("a b", ZuSpan<char>{buf, sizeof(buf)}) == "a+b");
  ZuCheck(encode<Quote>("%", ZuSpan<char>{buf, sizeof(buf)}) == "%25");
  ZuCheck(encode<Quote>(ZuBSpan{0x80, 0xff}, ZuSpan<char>{buf, sizeof(buf)}) ==
    "%80%FF");
  ZuCheck(encode<Quote>("a b", ZuSpan<char>{buf, 2}) == "a");
  ZuCheck(encode<Quote>("a b", ZuSpan<char>{buf, 4}) == "a%20");
}

void encodeOverflow()
{
  ZuTestScope(encodeOverflow);

  char buf[8];
  auto r = ZuPercent::Codec<Quote>::encode_overflow(
    ZuSpan<char>{buf, 5}, "a b");
  ZuCheck(r.p<0>() == 5 && !r.p<1>() && ZuCSpan(buf, 5) == "a%20b");
  r = ZuPercent::Codec<Quote>::encode_overflow(ZuSpan<char>{buf, 2}, "a b");
  ZuCheck(r.p<0>() == 1 && r.p<1>() && ZuCSpan(buf, 1) == "a");
  r = ZuPercent::Codec<Quote>::encode_overflow(ZuSpan<char>{buf, 4}, "a b");
  ZuCheck(r.p<0>() == 4 && r.p<1>() && ZuCSpan(buf, 4) == "a%20");
  r = ZuPercent::Codec<Body>::encode_overflow(ZuSpan<char>{buf, 2}, "a b");
  ZuCheck(r.p<0>() == 2 && r.p<1>() && ZuCSpan(buf, 2) == "a+");
}

void print()
{
  ZuTestScope(print);

  char buf[32];
  Stream s{buf};
  ZuPercent::Codec<Quote>::print(s, "a b%");
  ZuCheck(ZuCSpan(buf, s.ptr - buf) == "a%20b%25");
}

void decode()
{
  ZuTestScope(decode);

  {
    char buf[] = "foo%20bar&";
    auto r = ZuPercent::Codec<Query>::decode(buf);
    ZuCheck(r.out == 7 && r.in == 10 && r.term == '&');
    ZuCheck(ZuCSpan(buf, 7) == "foo bar");
  }
  {
    char buf[] = "foo+bar&";
    auto r = ZuPercent::Codec<Query>::decode(buf);
    ZuCheck(r.out == 7 && r.in == 8 && r.term == '&');
    ZuCheck(ZuCSpan(buf, 7) == "foo bar");
  }
  {
    char buf[] = "foo+bar/";
    auto r = ZuPercent::Codec<Path>::decode(buf);
    ZuCheck(r.out == 7 && r.in == 8 && r.term == '/');
    ZuCheck(ZuCSpan(buf, 7) == "foo+bar");
  }
  {
    char buf[] = "%";
    ZuCheck(!ZuPercent::Codec<Query>::decode(buf));
  }
  {
    char buf[] = "%x";
    ZuCheck(!ZuPercent::Codec<Query>::decode(buf));
  }
  {
    char buf[] = "%zz";
    ZuCheck(!ZuPercent::Codec<Query>::decode(buf));
  }
  {
    char buf[] = "foo&x";
    auto r = ZuPercent::Codec<Query>::decode(buf);
    ZuCheck(r.out == 3 && r.in == 4 && r.term == '&');
    ZuCheck(!buf[3] && buf[4] == 'x');
  }
  {
    char buf[] = "foo%20bar&xx";
    auto r = ZuPercent::Codec<Query>::decode(buf);
    ZuCheck(r.out == 7 && r.in == 10 && r.term == '&');
    ZuCheck(ZuCSpan(buf, 7) == "foo bar");
    ZuCheck(!buf[7] && !buf[8] && !buf[9] && buf[10] == 'x');
  }
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(hex);
  ZuTestCall(span);
  ZuTestCall(encode);
  ZuTestCall(encodeOverflow);
  ZuTestCall(print);
  ZuTestCall(decode);
  return 0;
}
