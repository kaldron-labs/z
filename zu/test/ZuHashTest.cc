//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include <iostream>

#include <zlib/ZuTest.hh>
#include <zlib/ZuHash.hh>
#include <zlib/ZuCmp.hh>

#include "Analyze.hh"

static int count[256] = { 0 };

template <int Size> struct HiBits_;

struct HiBits {
  static uint32_t hashBits(uint32_t i) { return i>>24; }
};

struct LoBits {
  static uint32_t hashBits(uint32_t i) { return i & 0xff; }
};

template <typename T, unsigned Size = sizeof(T)> struct Rand;
template <typename T> struct Rand<T, 1> {
  static T rand() {
    typedef uint8_t Data[1];
    ZuPun<Data, T> pun;
    pun.in[0] = ::rand() & 0xff;
    return pun.out;
  }
};
template <typename T> struct Rand<T, 2> {
  static T rand() {
    typedef uint16_t Data[1];
    ZuPun<Data, T> pun;
    pun.in[0] = ::rand() & 0xffff;
    return pun.out;
  }
};
template <typename T> struct Rand<T, 4> {
  static T rand() {
    typedef uint16_t Data[2];
    ZuPun<Data, T> pun;
    pun.in[0] = ::rand() & 0xffff;
    pun.in[1] = ::rand() & 0xffff;
    return pun.out;
  }
};
template <typename T> struct Rand<T, 8> {
  static T rand() {
    typedef uint16_t Data[4];
    ZuPun<Data, T> pun;
    pun.in[0] = ::rand() & 0xffff;
    pun.in[1] = ::rand() & 0xffff;
    pun.in[2] = ::rand() & 0xffff;
    pun.in[3] = ::rand() & 0xffff;
    return pun.out;
  }
};
template <typename T> struct Rand<T, 16> {
  static T rand() {
    typedef uint16_t Data[8];
    ZuPun<Data, T> pun;
    pun.in[0] = ::rand() & 0xffff;
    pun.in[1] = ::rand() & 0xffff;
    pun.in[2] = ::rand() & 0xffff;
    pun.in[3] = ::rand() & 0xffff;
    pun.in[4] = ::rand() & 0xffff;
    pun.in[5] = ::rand() & 0xffff;
    pun.in[6] = ::rand() & 0xffff;
    pun.in[7] = ::rand() & 0xffff;
    return pun.out;
  }
};

template <typename Bits, typename T> struct IntTest {
  static void run(const char *s) {
    ZuTestScope(run);
    memset(count, 0, 256 * sizeof(int));

    for (int i = 0; i < (1<<16); i++)
      count[Bits::hashBits(ZuHash<T>::hash(Rand<T>::rand()))]++;
    unsigned total = 0;
    for (unsigned i = 0; i < 256; i++) total += count[i];
    ZuCheck(total == (1U<<16));
    analyze(s, count, 256);
  }
};

template <typename Bits, typename T> struct FloatTest {
  static void run(const char *s) {
    ZuTestScope(run);
    memset(count, 0, 256 * sizeof(int));

    T f = (T)RAND_MAX + 1;
    for (int i = 0; i < (1<<16); i++) {
      T g = (T)rand() / f;
      count[Bits::hashBits(ZuHash<T>::hash(g))]++;
    }
    unsigned total = 0;
    for (unsigned i = 0; i < 256; i++) total += count[i];
    ZuCheck(total == (1U<<16));
    analyze(s, count, 256);
  }
};

static char *randomString()
{
  static char characters[] =
    "abcdefghijklmnopqrstuvwxyz"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "0123456789";
#define rand_62() (((::rand() & 0xff00) * 62)>>16)
#define rand_64() ((::rand() & 0x3f00)>>8)
  int l = 16 + rand_64();
  char *buf = static_cast<char *>(malloc(l + 1));

  for (int i = 0; i < l; i++) buf[i] = characters[rand_62()];
  buf[l] = 0;
  return buf;
}

template <typename Bits> struct StringTest {
  static void run(const char *s) {
    ZuTestScope(run);
    memset(count, 0, 256 * sizeof(int));

    for (int i = 0; i < (1<<16); i++) {
      char *buf = randomString();
      count[Bits::hashBits(ZuHash<char *>::hash(buf))]++;
      free(buf);
    }
    unsigned total = 0;
    for (unsigned i = 0; i < 256; i++) total += count[i];
    ZuCheck(total == (1U<<16));
    analyze(s, count, 256);
  }
};

#ifdef _MSC_VER
#pragma warning(disable:4996)
#endif

void testString(const char *s)
{
  ZuTestScope(testString);
  char buf[16];

  buf[0] = ' ';
  strcpy(buf + 1, s);
  ZuCheck(
    ZuHash<char *>::hash(s) == ZuHash<char *>::hash(buf + 1) &&
    ZuHash<char *>::hash(s + 1) == ZuHash<char *>::hash(buf + 2),
    std::cerr << "Failed to hash \"" << s << "\" to identical values\n");
}

int main()
{
  ZuTestMain();

#define TestInt(bits, type, name) \
  ZuTestCall_(name, (IntTest<bits, type>::run), name)
#define TestFloat(bits, type, name) \
  ZuTestCall_(name, (FloatTest<bits, type>::run), name)
#define TestString(bits, name) \
  ZuTestCall_(name, (StringTest<bits>::run), name)

  ::srand(unsigned(time(0)));

  TestInt(HiBits, char, "Hi char");
  TestInt(LoBits, char, "Lo char");
  TestInt(HiBits, unsigned char, "Hi unsigned char");
  TestInt(LoBits, unsigned char, "Lo unsigned char");
  TestInt(HiBits, signed char, "Hi signed char");
  TestInt(LoBits, signed char, "Lo signed char");
  TestInt(HiBits, short, "Hi short");
  TestInt(LoBits, short, "Lo short");
  TestInt(HiBits, unsigned short, "Hi unsigned short");
  TestInt(LoBits, unsigned short, "Lo unsigned short");
  TestInt(HiBits, int, "Hi int");
  TestInt(LoBits, int, "Lo int");
  TestInt(HiBits, unsigned int, "Hi unsigned int");
  TestInt(LoBits, unsigned int, "Lo unsigned int");
  TestInt(HiBits, long, "Hi long");
  TestInt(LoBits, long, "Lo long");
  TestInt(HiBits, unsigned long, "Hi unsigned long");
  TestInt(LoBits, unsigned long, "Lo unsigned long");
  TestInt(HiBits, long long, "Hi long long");
  TestInt(LoBits, long long, "Lo long long");
  TestInt(HiBits, unsigned long long, "Hi unsigned long long");
  TestInt(LoBits, unsigned long long, "Lo unsigned long long");
  TestInt(HiBits, int128_t, "Hi int128_t");
  TestInt(LoBits, int128_t, "Lo int128_t");
  TestInt(HiBits, uint128_t, "Hi uint128_t");
  TestInt(LoBits, uint128_t, "Lo uint128_t");
  TestInt(HiBits, wchar_t, "Hi wchar_t");
  TestInt(LoBits, wchar_t, "Lo wchar_t");
  TestFloat(HiBits, float, "Hi float");
  TestFloat(LoBits, float, "Lo float");
  TestFloat(HiBits, double, "Hi double");
  TestFloat(LoBits, double, "Lo double");
  TestFloat(HiBits, long double, "Hi long double");
  TestFloat(LoBits, long double, "Lo long double");
  TestString(HiBits, "Hi string");
  TestString(LoBits, "Lo string");

  ZuTestCall(testString, "f");
  ZuTestCall(testString, "fo");
  ZuTestCall(testString, "foo");
  ZuTestCall(testString, "foob");
  ZuTestCall(testString, "fooba");
  ZuTestCall(testString, "foobar");
  ZuTestCall(testString, "foobar!");
  ZuTestCall(testString, "foobar!!");
}
