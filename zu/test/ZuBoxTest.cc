//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

#include <string>
#include <sstream>
#include <iostream>
#include <iomanip>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuInt.hh>
#include <zlib/ZuTraits.hh>
#include <zlib/ZuBox.hh>
#include <zlib/ZuArray.hh>
#include <zlib/ZuDerive.hh>

using namespace ZuTestUtil;

template <class Fmt, class Boxed>
struct VFmt_ {
  static void _(ZuVFmt &fmt) {
    if (Fmt::Alt_ == 1) fmt.alt();
    if (Fmt::Comma_ != '\0') fmt.comma(Fmt::Comma_);
    if (Fmt::Hex_ == 1) fmt.hex(Fmt::Upper_);
  }
};

template <
  class Fmt, class Boxed,
  int Justification = Fmt::Justification_>
struct VFmt;

template <class Fmt, class Boxed>
struct VFmt<Fmt, Boxed, ZuFmt::Just::None> {
  static ZuVFmt _() {
    ZuVFmt fmt;
    fmt.fp(Fmt::NDP_, Fmt::Trim_);
    VFmt_<Fmt, Boxed>::_(fmt);
    return fmt;
  }
};
template <class Fmt, class Boxed>
struct VFmt<Fmt, Boxed, ZuFmt::Just::Left> {
  static ZuVFmt _() {
    ZuVFmt fmt;
    fmt.left(Fmt::Width_, Fmt::Pad_);
    VFmt_<Fmt, Boxed>::_(fmt);
    return fmt;
  }
};
template <class Fmt, class Boxed>
struct VFmt<Fmt, Boxed, ZuFmt::Just::Right> {
  static ZuVFmt _() {
    ZuVFmt fmt;
    fmt.right(Fmt::Width_, Fmt::Pad_);
    VFmt_<Fmt, Boxed>::_(fmt);
    return fmt;
  }
};
template <class Fmt, class Boxed>
struct VFmt<Fmt, Boxed, ZuFmt::Just::Frac> {
  static ZuVFmt _() {
    ZuVFmt fmt;
    fmt.frac(Fmt::Width_, Fmt::NDP_, Fmt::Trim_);
    VFmt_<Fmt, Boxed>::_(fmt);
    return fmt;
  }
};

template <class Fmt, typename S>
void test_std_string2(const char *type, const S &s, const char *s_)
{
  ZuTestScope(test_std_string2);
  log("type=", type, " Width_=", unsigned(Fmt::Width_), " Comma_=", unsigned(Fmt::Comma_), " s=", ZuCSpan(ZuTraits<S>::data(s), ZuTraits<S>::length(s)), " s_=", s_);
  ZuCHECK(!strcmp(s.c_str(), s_), s.c_str(), s_);
}

template <typename T, class Fmt, typename S>
void test_std_string1(const char *type, T v, const char *s_)
{
  ZuTestScope(test_std_string1);
  ZuBox<T> b = v;
  ZuVFmt vfmt = VFmt<Fmt, ZuBox<T> >::_();
  { S s; s += b.template fmt<Fmt>(); ZuTestCall((test_std_string2<Fmt, S>), type, s, s_); }
  { S s; s += b.vfmt(vfmt); ZuTestCall((test_std_string2<Fmt, S>), type, s, s_); }
}

template <class Fmt, typename S>
void test_std_stream2(const char *type, S &s, const char *s_)
{
  ZuTestScope(test_std_stream2);
  char buf[64];
  buf[s.rdbuf()->sgetn(buf, 63)] = 0;
  log("type=", type, " Width_=", unsigned(Fmt::Width_), " Comma_=", unsigned(Fmt::Comma_), " buf=", buf, " s_=", s_);
  ZuCHECK(!strcmp(buf, s_), buf, s_);
}

template <typename T, class Fmt, typename S>
void test_std_stream1(const char *type, T v, const char *s_)
{
  ZuTestScope(test_std_stream1);
  ZuBox<T> b = v;
  ZuVFmt vfmt = VFmt<Fmt, ZuBox<T> >::_();
  { S s; s << b.template fmt<Fmt>(); ZuTestCall((test_std_stream2<Fmt, S>), type, s, s_); }
  { S s; s << b.vfmt(vfmt); ZuTestCall((test_std_stream2<Fmt, S>), type, s, s_); }
}

template <typename T, class Fmt>
void test_std(T v, const char *s)
{
  ZuTestScope(test_std);
  ZuTestCall((test_std_string1<T, Fmt, std::string>), "std::string", v, s);
  ZuTestCall((test_std_stream1<T, Fmt, std::stringstream>), "std::stringstream", v, s);
}

template <typename T, class Fmt>
void test(const char *type, T v, const char *s)
{
  ZuTestScope(test);

  log(type);
  ZuTestCall((test_std<T, Fmt>), v, s);
  ZuTestCall((test_std<T, Fmt>), v, s);
  ZuBox<T> i;
  ZuCHECK(!*i, i);
  i = v;
  ZuCHECK(*i, i);
  ZuCHECK(i > ZuBox<T>{}, i);
  ZuCHECK(ZuBox<T>(static_cast<T>(-i)) > ZuBox<T>{}, i);
  ZuCHECK(i > (v - 1), i, v);
  ZuVFmt vfmt = VFmt<Fmt, ZuBox<T> >::_();
  char buf[64], buf2[64];
  log("strlen(s)=", strlen(s), " i.fmt<Fmt>().length()=", i.template fmt<Fmt>().length(), " i.vfmt(vfmt).length()=", i.vfmt(vfmt).length());
  ZuCHECK(i.template fmt<Fmt>().length() >= strlen(s), i, s);
  ZuCHECK(i.vfmt(vfmt).length() >= strlen(s), i, s);
  buf[i.template fmt<Fmt>().print(buf)] = 0;
  buf2[i.vfmt(vfmt).print(buf2)] = 0;
  log("s=", s, " buf=", buf, " buf2=", buf2);
  ZuCHECK(!strcmp(s, buf), s, buf);
  ZuCHECK(!strcmp(s, buf2), s, buf2);
  ZuBox<T> j(Fmt(), buf, strlen(buf));
  log("i=", int64_t(i), " j=", int64_t(j));
  ZuCHECK(i == j, i, j);
}

template <typename T, class Fmt>
void testf(const char *type, T v, const char *s, T d = (T)0)
{
  ZuTestScope(testf);

  ZuTestCall((test_std<T, ZuFmt::Comma<',', Fmt>>), v, s);
  ZuBox<T> f;
  ZuCHECK(!*f, f);
  f = v;
  ZuCHECK(*f, f);
  ZuCHECK(f > ZuBox<T>(), f);
  ZuCHECK(ZuBox<T>(-f) > ZuBox<T>(), f);
  ZuCHECK(f > (v - 1), f, v);
  char buf[64], buf2[64];
  buf[f.template fmt<Fmt>().print(buf)] = 0;
  buf2[f.template fmt<ZuFmt::Comma<',', Fmt>>().print(buf2)] = 0;
  log("s=", s, " buf=", buf, " buf2=", buf2);
  ZuCHECK(!strcmp(s, buf2), s, buf2);
  ZuBox<T> g, h;
  g.scan(buf, strlen(buf));
  h.scan(buf2, strlen(buf2));
  T i = (T)strtod(buf, 0);
  log(std::fixed, std::setprecision(12), "f=", double(f), " g=", double(g), " h=", double(h), " i=", double(i), " d=", double(d));
  ZuCHECK((f > g) ? ((f - g) <= d) : ((g - f) <= d), f, g);
}

int foo() { return 42; }
ZuBox<int> bar() { return ZuBox<int>(); }
ZuBox<int> bah() { return ZuBox<int>(42); }

using BoxedInt = ZuBox<int>;

int foo2() { return 42; }
BoxedInt bar2() { return BoxedInt(); }
BoxedInt bah2() { return BoxedInt(42); }

unsigned itoa(char *buf, int i) {
  bool negative = 0;
  unsigned j;
  if (i < 0) { *buf++ = '-'; j = -i; negative = 1; }
  else j = i;
  unsigned n = 11;
  do { buf[--n] = (j % 10) + '0'; j /= 10; } while (j);
  unsigned o = 11 - n;
  if (n) memmove(buf, buf + n, o);
  buf[o] = 0;
  return o + negative;
}

ZuDerive(A, ZuBox<int>);
ZuDerive(B, ZuBox<int>);

int main(int argc, char **argv)
{
  parse(argc, argv);

  ZuTestMain();

  ZuTestCall((test<char, ZuFmt::Default>), "char", 42, "42");
  ZuTestCall((test<char, ZuFmt::Right<3>>), "char", 42, "042");

  ZuTestCall((test<unsigned char, ZuFmt::Default>), "unsigned char", 42, "42");
  ZuTestCall((test<unsigned char, ZuFmt::Right<3>>), "unsigned char", 42, "042");

  ZuTestCall((test<signed char, ZuFmt::Default>), "signed char", 42, "42");
  ZuTestCall((test<signed char, ZuFmt::Right<3>>), "signed char", 42, "042");
  ZuTestCall((test<signed char, ZuFmt::Left<3, '_'>>), "signed char", 42, "42_");
  ZuTestCall((test<signed char, ZuFmt::Default>), "signed char", -42, "-42");
  ZuTestCall((test<signed char, ZuFmt::Right<5>>), "signed char", -42, "-0042");
  ZuTestCall((test<signed char, ZuFmt::Left<5, '_'>>), "signed char", -42, "-42__");
  ZuTestCall((test<signed char, ZuFmt::Frac<5, 5>>), "signed char", 42, "00042");

  ZuTestCall((test<unsigned short, ZuFmt::Default>), "unsigned short", 42, "42");
  ZuTestCall((test<unsigned short, ZuFmt::Right<3>>), "unsigned short", 42, "042");
  ZuTestCall((test<unsigned short, ZuFmt::Right<3>>), "unsigned short", 420, "420");
  ZuTestCall((test<unsigned short, ZuFmt::Right<7, '_', ZuFmt::Comma<','> >>), 
      "unsigned short", 42420, "_42,420");

  ZuTestCall((test<short, ZuFmt::Default>), "short", 42, "42");
  ZuTestCall((test<short, ZuFmt::Right<3>>), "short", 42, "042");
  ZuTestCall((test<short, ZuFmt::Left<3, '_'>>), "short", 42, "42_");
  ZuTestCall((test<short, ZuFmt::Default>), "short", -42, "-42");
  ZuTestCall((test<short, ZuFmt::Right<5>>), "short", -42, "-0042");
  ZuTestCall((test<short, ZuFmt::Left<5, '_'>>), "short", -42, "-42__");
  ZuTestCall((test<short, ZuFmt::Frac<5, 5>>), "short", 42, "00042");
  ZuTestCall((test<short, ZuFmt::Frac<5, 5>>), "short", 420, "0042");
  ZuTestCall((test<short, ZuFmt::Frac<5, 5, '0'>>), "short", 420, "00420");
  ZuTestCall((test<short, ZuFmt::Frac<5, 5, '_'>>), "short", 420, "0042_");

  ZuTestCall((test<unsigned int, ZuFmt::Default>), "unsigned int", 42, "42");
  ZuTestCall((test<unsigned int, ZuFmt::Right<3>>), "unsigned int", 42, "042");
  ZuTestCall((test<unsigned int, ZuFmt::Right<3>>), "unsigned int", 420, "420");

  ZuTestCall((test<int, ZuFmt::Default>), "int", 42, "42");
  ZuTestCall((test<int, ZuFmt::Right<3>>), "int", 42, "042");
  ZuTestCall((test<int, ZuFmt::Left<3, '_'>>), "int", 42, "42_");
  ZuTestCall((test<int, ZuFmt::Default>), "int", -42, "-42");
  ZuTestCall((test<int, ZuFmt::Right<5>>), "int", -42, "-0042");
  ZuTestCall((test<int, ZuFmt::Left<5, '_'>>), "int", -42, "-42__");
  ZuTestCall((test<int, ZuFmt::Frac<5, 5>>), "int", 42, "00042");
  ZuTestCall((test<int, ZuFmt::Frac<5, 5>>), "int", 420, "0042");
  ZuTestCall((test<int, ZuFmt::Frac<5, 5, '0'>>), "int", 420, "00420");
  ZuTestCall((test<int, ZuFmt::Frac<5, 5, '_'>>), "int", 420, "0042_");
  ZuTestCall((test<int, ZuFmt::Right<12, '_', ZuFmt::Comma<','> >>), 
      "int", -1420420, "__-1,420,420");

  ZuTestCall((test<unsigned long, ZuFmt::Default>), "unsigned long", 42, "42");
  ZuTestCall((test<unsigned long, ZuFmt::Right<3>>), "unsigned long", 42, "042");
  ZuTestCall((test<unsigned long, ZuFmt::Right<3>>), "unsigned long", 420, "420");

  ZuTestCall((test<long, ZuFmt::Default>), "long", 42, "42");
  ZuTestCall((test<long, ZuFmt::Right<3>>), "long", 42, "042");
  ZuTestCall((test<long, ZuFmt::Left<3, '_'>>), "long", 42, "42_");
  ZuTestCall((test<long, ZuFmt::Default>), "long", -42, "-42");
  ZuTestCall((test<long, ZuFmt::Right<5>>), "long", -42, "-0042");
  ZuTestCall((test<long, ZuFmt::Left<5, '_'>>), "long", -42, "-42__");
  ZuTestCall((test<long, ZuFmt::Frac<5, 5>>), "long", 42, "00042");
  ZuTestCall((test<long, ZuFmt::Frac<5, 5>>), "long", 420, "0042");
  ZuTestCall((test<long, ZuFmt::Frac<5, 5, '0'>>), "long", 420, "00420");
  ZuTestCall((test<long, ZuFmt::Frac<5, 5, '_'>>), "long", 420, "0042_");
  ZuTestCall((test<long, ZuFmt::Right<12, '_', ZuFmt::Comma<','> >>), 
      "long", -1420420, "__-1,420,420");

  ZuTestCall((test<unsigned long long, ZuFmt::Default>), "unsigned long long", 42, "42");
  ZuTestCall((test<unsigned long long, ZuFmt::Right<3>>), "unsigned long long", 42, "042");
  ZuTestCall((test<unsigned long long, ZuFmt::Right<3>>), "unsigned long long", 420, "420");

  ZuTestCall((test<long long, ZuFmt::Default>), "long long", 42, "42");
  ZuTestCall((test<long long, ZuFmt::Right<3>>), "long long", 42, "042");
  ZuTestCall((test<long long, ZuFmt::Left<3, '_'>>), "long long", 42, "42_");
  ZuTestCall((test<long long, ZuFmt::Default>), "long long", -42, "-42");
  ZuTestCall((test<long long, ZuFmt::Right<5>>), "long long", -42, "-0042");
  ZuTestCall((test<long long, ZuFmt::Left<5, '_'>>), "long long", -42, "-42__");
  ZuTestCall((test<long long, ZuFmt::Frac<5, 5>>), "long long", 42, "00042");
  ZuTestCall((test<long long, ZuFmt::Frac<5, 5>>), "long long", 420, "0042");
  ZuTestCall((test<long long, ZuFmt::Frac<5, 5, '0'>>), "long long", 420, "00420");
  ZuTestCall((test<long long, ZuFmt::Frac<5, 5, '_'>>), "long long", 420, "0042_");
  ZuTestCall((test<long long, ZuFmt::Right<22, '_', ZuFmt::Comma<','> >>), 
      "long long", -14242012345678, "___-14,242,012,345,678");
  ZuTestCall((test<long long, ZuFmt::Left<22, '_', ZuFmt::Comma<','> >>), 
      "long long", -14242012345678, "-14,242,012,345,678___");
  ZuTestCall((test<long long, ZuFmt::Frac<19, 19, '_'>>), 
      "long long", 14242012345000, "0000014242012345___");

  {
    ZuBox<int> i;
    i.scan("-", 1); ZuCHECK(!i, i);
    i.scan("-0", 2); ZuCHECK(!i, i);
    i.scan("0", 1); ZuCHECK(!i, i);
    i.scan("420", 0); ZuCHECK(!*i, i);
    i.scan("420", 2); ZuCHECK(i == 42, i);
    i.scan("420", 2); ZuCHECK(i == 42, i);
    char buf[256];
    i = 0;
    buf[i.print(buf)] = 0;
    ZuCHECK(!strcmp(buf, "0"), buf);
  }

  // 7-8 SD
  ZuTestCall((testf<float, ZuFmt::FP<-2>>), "float", (float)16777216, "16,777,216");
  ZuTestCall((testf<float, ZuFmt::FP<2>>), "float", (float)16777216, "16,777,216.00");
  ZuTestCall((testf<float, ZuFmt::FP<>>), "float", 0.0F, "0");
  ZuTestCall((testf<float, ZuFmt::FP<>>), "float", 42.0F, "42");
  ZuTestCall((testf<float, ZuFmt::FP<-2>>), "float", 42.0F, "42");
  ZuTestCall((testf<float, ZuFmt::FP<-2>>), "float", 42.004F, "42", 42.004F - 42.0F);
  ZuTestCall((testf<float, ZuFmt::FP<-2>>), "float", 42.006F, "42.01", 42.01F - 42.006F);
  ZuTestCall((testf<float, ZuFmt::FP<2, '0'>>), "float", 42.004F, "42.00", 42.004F - 42.0F);
  ZuTestCall((testf<float, ZuFmt::FP<2, '0'>>), "float", 42.006F, "42.01", 42.01F - 42.006F);
  ZuTestCall((testf<float, ZuFmt::FP<>>), "float", 42.000004F, "42.000004");
  ZuTestCall((testf<float, ZuFmt::FP<>>), "float", 42.000006F, "42.000008");
  ZuTestCall((testf<float, ZuFmt::FP<-2>>), "float", .42F, "0.42");
  ZuTestCall((testf<float, ZuFmt::FP<-3>>), "float", .42F, "0.42");
  ZuTestCall((testf<float, ZuFmt::FP<>>), "float", -42.0F, "-42");
  ZuTestCall((testf<float, ZuFmt::FP<-2>>), "float", -42.0F, "-42");
  ZuTestCall((testf<float, ZuFmt::FP<-2>>), "float", -42.004F, "-42", -42.0F - -42.004F);
  ZuTestCall((testf<float, ZuFmt::FP<-2>>), "float", -42.006F, "-42.01", -42.006F - -42.01F);
  ZuTestCall((testf<float, ZuFmt::FP<-2>>), "float", -.42F, "-0.42");
  ZuTestCall((testf<float, ZuFmt::FP<-3>>), "float", -.42F, "-0.42");
  ZuTestCall((testf<float, ZuFmt::FP<-3>>), "float", 100.0001F, "100", 100.0001F - 100.0F);
  ZuTestCall((testf<float, ZuFmt::FP<-3>>), "float", 1100.101F, "1,100.101");
  ZuTestCall((testf<float, ZuFmt::FP<3, '0'>>), "float", 100.0001F, "100.000", 100.0001F - 100.0F);
  ZuTestCall((testf<float, ZuFmt::FP<3, '0'>>), "float", 1100.101F, "1,100.101");
  ZuTestCall((testf<float, ZuFmt::FP<>>), "float", 100.0001F, "100.0001");
  ZuTestCall((testf<float, ZuFmt::FP<>>), "float", 1100.101F, "1,100.101");
  ZuTestCall((testf<float, ZuFmt::FP<0, '0'>>), "float", 4200100.0F, "4,200,100", 1.0F);
  // 16 SD
  ZuTestCall((testf<double, ZuFmt::FP<>>), "double", 0.0, "0");
  ZuTestCall((testf<double, ZuFmt::FP<>>), "double", 42.0, "42");
  ZuTestCall((testf<double, ZuFmt::FP<-2>>), "double", 42.0, "42");
  ZuTestCall((testf<double, ZuFmt::FP<-2>>), "double", 42.004, "42", 42.004 - 42.0);
  ZuTestCall((testf<double, ZuFmt::FP<-2>>), "double", 42.006, "42.01", 42.01 - 42.006);
  ZuTestCall((testf<double, ZuFmt::FP<2, '0'>>), "double", 42.004, "42.00", 42.004 - 42.0);
  ZuTestCall((testf<double, ZuFmt::FP<2, '0'>>), "double", 42.006, "42.01", 42.01 - 42.006);
  ZuTestCall((testf<double, ZuFmt::FP<>>), "double", 42.00000000000004, "42.00000000000004");
  ZuTestCall((testf<double, ZuFmt::FP<>>), "double", 42.00000000000006, "42.00000000000006");
  ZuTestCall((testf<double, ZuFmt::FP<-2>>), "double", .42, "0.42");
  ZuTestCall((testf<double, ZuFmt::FP<-3>>), "double", .42, "0.42");
  ZuTestCall((testf<double, ZuFmt::FP<>>), "double", -42.0, "-42");
  ZuTestCall((testf<double, ZuFmt::FP<-2>>), "double", -42.0, "-42");
  ZuTestCall((testf<double, ZuFmt::FP<-2>>), "double", -42.004, "-42", .004);
  ZuTestCall((testf<double, ZuFmt::FP<-2>>), "double", -42.006, "-42.01", .004);
  ZuTestCall((testf<double, ZuFmt::FP<-2>>), "double", -.42, "-0.42");
  ZuTestCall((testf<double, ZuFmt::FP<-3>>), "double", -.42, "-0.42");
  ZuTestCall((testf<double, ZuFmt::FP<-6>>), "double", 100.0000001, "100", 100.0000001 - 100.0);
  ZuTestCall((testf<double, ZuFmt::FP<-6>>), "double", 1100.1000001, "1,100.1", 1100.1000001 - 1100.1);
  ZuTestCall((testf<double, ZuFmt::FP<6, '0'>>), "double", 100.0000001, "100.000000", 100.0000001 - 100.0);
  ZuTestCall((testf<double, ZuFmt::FP<6, '0'>>), "double", 1100.1000001, "1,100.100000", 1100.1000001 - 1100.1);
  ZuTestCall((testf<double, ZuFmt::FP<>>), "double", 100.0000000000001, "100.0000000000001");
  ZuTestCall((testf<double, ZuFmt::FP<>>), "double", 1100.100000000001, "1,100.100000000001");
  ZuTestCall((testf<double, ZuFmt::FP<-6>>), "double", 42000100.0000001, "42,000,100", 42000100.0000001 - 42000100.0);
  ZuTestCall((testf<double, ZuFmt::FP<-2>>), "double", 41.999, "42", 42 - 41.999);
  ZuTestCall((testf<double, ZuFmt::FP<>>), "double", 8.981016216, "8.981016216");
  ZuTestCall((testf<double, ZuFmt::FP<-2>>), "double", 99.999, "100", 100 - 99.999);
  ZuTestCall((testf<double, ZuFmt::FP<>>), "double", 0.437464744, "0.437464744");
  ZuTestCall((testf<double, ZuFmt::FP<9>>), "double", 12.673215776-12.061490938, "0.611724838", .0000000001);
  // 20 SD
  ZuTestCall((testf<long double, ZuFmt::FP<>>), "long double", 0.0L, "0");
  ZuTestCall((testf<long double, ZuFmt::FP<>>), "long double", 42.0L, "42");
  ZuTestCall((testf<long double, ZuFmt::FP<-2>>), "long double", 42.0L, "42");
  ZuTestCall((testf<long double, ZuFmt::FP<-2>>), "long double", 42.004L, "42", 42.004L - 42.0L);
  ZuTestCall((testf<long double, ZuFmt::FP<-2>>), "long double", 42.006L, "42.01", 42.01L - 42.006L);
  ZuTestCall((testf<long double, ZuFmt::FP<2, '0'>>), "long double", 42.004L, "42.00", 42.004L - 42.0L);
  ZuTestCall((testf<long double, ZuFmt::FP<2, '0'>>), "long double", 42.006L, "42.01", 42.01L - 42.006L);
  ZuTestCall((testf<long double, ZuFmt::FP<-2>>), "long double", .42L, "0.42");
  ZuTestCall((testf<long double, ZuFmt::FP<-3>>), "long double", .42L, "0.42");
  ZuTestCall((testf<long double, ZuFmt::FP<>>), "long double", -42.0L, "-42");
  ZuTestCall((testf<long double, ZuFmt::FP<-2>>), "long double", -42.0L, "-42");
  ZuTestCall((testf<long double, ZuFmt::FP<-2>>), "long double", -42.004L, "-42", 42.004L - 42.0L);
  ZuTestCall((testf<long double, ZuFmt::FP<-2>>), "long double", -42.006L, "-42.01", 42.01L - 42.006L);
  ZuTestCall((testf<long double, ZuFmt::FP<-2>>), "long double", -.42L, "-0.42");
  ZuTestCall((testf<long double, ZuFmt::FP<-3>>), "long double", -.42L, "-0.42");
  ZuTestCall((testf<long double, ZuFmt::FP<-6>>), "long double", 100.0000001L, "100", 100.0000001L - 100.0L);
  ZuTestCall((testf<long double, ZuFmt::FP<-6>>), "long double", 1100.1000001L, "1,100.1", 1100.1000001L - 1100.1L);
  ZuTestCall((testf<long double, ZuFmt::FP<6, '0'>>), "long double", 100.0000001L, "100.000000", 100.0000001L - 100.0L);
  ZuTestCall((testf<long double, ZuFmt::FP<6, '0'>>), "long double", 1100.1000001L, "1,100.100000", 1100.1000001L - 1100.1L);
  ZuTestCall((testf<long double, ZuFmt::FP<>>), "long double", 100.00000000000000001L, "100.00000000000000001");
  ZuTestCall((testf<long double, ZuFmt::FP<>>), "long double", 1100.100000000001L, "1,100.100000000001");
  ZuTestCall((testf<long double, ZuFmt::FP<-6>>), "long double", 42000100.0000001L, "42,000,100", 42000100.0000001L - 42000100.0L);

  //testf<long double, ZuFmt::FP<-1, '\0', ZuFmt::Right<10, '_'> > >("long double", -1100.100000000001L, "____-1,100.100000000001");

  {
    ZuBox<double> f;
    f.scan(".", 1); ZuCHECK(!(int)f, f);
    f.scan("-", 1); ZuCHECK(!*f, f);
    f.scan("-0", 2); ZuCHECK(!(int)f, f);
    f.scan("-.", 2); ZuCHECK(!(int)f, f);
    f.scan("0", 1); ZuCHECK(!(int)f, f);
    f.scan("42.001", 6); ZuCHECK((int)(f * 1000) == 42001, f);
    f.scan("42.001", 0); ZuCHECK(ZuCmp<double>::null(f), f);
    f.scan("42.001", 5); ZuCHECK((int)(f * 1000) == 42000, f);
    f.scan("42.001", 6); ZuCHECK((int)(f * 1000) == 42001, f);
    f.scan((const char *)0, -1); ZuCHECK(ZuCmp<double>::null(f), f);
    f.scan("", -1); ZuCHECK(ZuCmp<double>::null(f), f);
    f.scan("nan", -1); ZuCHECK(ZuCmp<double>::null(f), f);
    f.scan((const char *)0, 0); ZuCHECK(ZuCmp<double>::null(f), f);
    f.scan("", 0); ZuCHECK(ZuCmp<double>::null(f), f);
    f.scan("nan", 3); ZuCHECK(ZuCmp<double>::null(f), f);
    f.scan("inf", 3); ZuCHECK(ZuCmp<double>::inf(f), f);
    f.scan("-inf", 4); ZuCHECK(ZuCmp<double>::inf(-f), f);
    f.scan("inf", 3); ZuCHECK(ZuCmp<double>::inf(f), f);
    f.scan("-inf", 4); ZuCHECK(ZuCmp<double>::inf(-f), f);
    ZuCHECK(f.scan("inf", 2) < 0, f);
    ZuCHECK(f.scan("nan", 2) < 0, f);
    auto prefix = ZuBox<double>::eov("42.5junk");
    ZuCHECK(prefix.p<0>() == 4, prefix.p<0>());
    ZuCHECK(prefix.p<1>() == 42.5, prefix.p<1>());
    auto invalid = ZuBox<double>::eov("junk");
    ZuCHECK(invalid.p<0>() < 0, invalid.p<0>());
    ZuCHECK(!*invalid.p<1>(), invalid.p<1>());
    char buf[256];
    f = 0;
    buf[f.print(buf)] = 0;
    ZuCHECK(!strcmp(buf, "0"), buf);
  }

  {
    int i;
    ZuBox<int> j;
    i = foo();
    log(i);
    j = i, i = j++, ++j;
    ZuCHECK(j == i + 2, i, j);
    j = i, i = j--, --j;
    ZuCHECK(j == i - 2, i, j);
  }

  {
    int x = 42;
    ZuCArray<8> s;
    s << ZuBoxed(x).fmt<ZuFmt::Hex<1, ZuFmt::Right<4> >>(); ZuCHECK(s == "002A", s); s = {};
    s << ZuBoxed(x).fmt<ZuFmt::Hex<1, ZuFmt::Right<3> >>(); ZuCHECK(s == "02A", s); s = {};
    s << ZuBoxed(x).fmt<ZuFmt::Hex<1, ZuFmt::Right<2> >>(); ZuCHECK(s == "2A", s); s = {};
    s << ZuBoxed(x).fmt<ZuFmt::Hex<1, ZuFmt::Right<1> >>(); ZuCHECK(s == "", s); s = {};
    s << ZuBoxed(x).fmt<ZuFmt::Hex<1, ZuFmt::Left<1> >>(); ZuCHECK(s == "", s); s = {};
    s << ZuBoxed(x).fmt<ZuFmt::Hex<1, ZuFmt::Left<2> >>(); ZuCHECK(s == "2A", s); s = {};
    s << ZuBoxed(x).fmt<ZuFmt::Hex<1, ZuFmt::Left<3> >>(); ZuCHECK(s == "2A", s); s = {};
    s << ZuBoxed(x).fmt<ZuFmt::Hex<1, ZuFmt::Left<4> >>(); ZuCHECK(s == "2A", s); s = {};
  }

  {
    ZuCArray<64> s;
    ZuNBox<uint64_t> v;
    ZuBox<uint64_t> w;
    s << v;
    ZuCHECK(!s, v);
    s << w;
    log(s);
    ZuCHECK(!!s, w);
  }

  {
    A a = 42;
    B b = 1;
    a += b; // 43
    b -= a; // -42
    b = -b; // 42
    log(b);
    ZuCHECK(b == 42, b);
  }
}
