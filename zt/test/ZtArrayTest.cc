//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string>
#include <sstream>
#include <iostream>
#include <vector>
#include <cassert>

#include <zlib/ZuLib.hh>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuBox.hh>
#include <zlib/ZuArray.hh>
#include <zlib/ZuJoin.hh>

#include <zlib/ZmList.hh>

#include <zlib/ZtArray.hh>
#include <zlib/ZtBuiltin.hh>
#include <zlib/ZtString.hh>
#include <zlib/ZtHexDump.hh>
#include <zlib/ZtCase.hh>

#ifdef _MSC_VER
#pragma warning(disable:4355)
#endif

using namespace ZuTestUtil;

template <typename T>
void out(T &&v) {
  if (verbose)
    std::cerr << ZuFwd<T>(v) << '\n';
}

void foo(const ZtString<> &s, ZtString<> t)
{
  if (verbose) {
    std::cerr << s << '\n';
    std::cerr << t << '\n';
  }
}

void bar(bool b, ZtString<> &s)
{
  ZtString<> baz;
  if (b)
    baz = s;
  else
    baz = "bah";
  if (verbose)
    std::cerr << baz << '\n';
}

template <auto &ZuTest_scope>
void testStringEquiv()
{
  ZtString<> s1, s2, s3, s4;

  s1 = (char *)0;
  s2 = "hello";
  s3 = "world";
  s4 = s3;

  if (verbose) {
    std::cerr << (s2 + s1) << '\n';
    std::cerr << (s3 + s1) << '\n';
    std::cerr << (s2 + " " + s3) << '\n';
  }
  s2 += ZtString<>(" ") + s3;
  if (verbose) {
    std::cerr << s2 << '\n';
    std::cerr << s4 << '\n';
  }

  ZuCheck((s3 == s4));
  ZuCheck((s3 == s3));
  ZuCheck((s2 == s2));
  ZuCheck((s1 == s1));
  ZuCheck((s2 != s3));
  ZuCheck((s1 != s3));
  ZuCheck(!s1);

  ZuCheck((s3 > s2));
  ZuCheck((s3 > s1));
  ZuCheck((s3 >= s4));
  ZuCheck((s3 >= s3));
  ZuCheck((s3 >= s2));
  ZuCheck((s3 >= s1));
  ZuCheck((s2 < s3));
  ZuCheck((s1 < s3));
  ZuCheck((s3 <= s4));
  ZuCheck((s3 <= s3));
  ZuCheck((s2 <= s3));
  ZuCheck((s1 <= s3));

  s2.splice(0, 5, "'bye ");

  ZuCheck(s2 == "'bye  world");

  s2.splice(16, 3, "!!!");

  ZuCheck(s2 == "'bye  world     !!!");

  s1.splice(2, 17, "hello world again");

  ZuCheck(s1 == "  hello world again");

  s1.splice(0, 0, ZtString<>(0));

  ZuCheck(s1 == "  hello world again");

  s1.splice(14, 15, "again and again");

  ZuCheck(s1 == "  hello world again and again");

  s1 = "this string";
  ZuCheck(s1 == "this string");
  s1.splice(0, 0, "beginning of ");
  ZuCheck(s1 == "beginning of this string");
  s1.splice(0, 0, "inserted at ");
  ZuCheck(s1 == "inserted at beginning of this string");

  s1 = "the string";
  ZuCheck(s1 == "the string");
  s1.splice(4, 0, "middle of this ");
  ZuCheck(s1 == "the middle of this string");
  s1.splice(s2, 0, 4, "inserted at the ");
  ZuCheck(s2 == "the ");
  ZuCheck(s1 == "inserted at the middle of this string");

  {
    ZtString<> s;
    s.sprintf("%s %.1d %.2d %.3d %s", "hello", 1, 2, 3, "world");
    ZuCheck(s == "hello 1 02 003 world");
  }
  {
    ZtString<> s =
      ZtString<>().sprintf("%s %.1d %.2d %.3d %s", "goodbye", 1, 2, 3, "world");
    ZuCheck(s == "goodbye 1 02 003 world");
  }
  {
    ZtWString<> w;
    w.sprintf(L"%ls %.1d %.2d %.3d %ls", L"hello", 1, 2, 3, L"world");
    ZtString<> s = w;
    ZuCheck(s == "hello 1 02 003 world");
  }
  {
    ZtWString<> w = ZtWString<>{}.sprintf(
      L"%ls %.1d %.2d %.3d %ls", L"goodbye", 1, 2, 3, L"world");
    ZtString<> s = ZtString<>{w};
    ZuCheck(s == "goodbye 1 02 003 world");
  }

  {
    ZtString<> s1, s3;
    ZtWString<> w1, w2, w3;

    s1 += "Hello";
    w1 += ZtWString<>(s1);
    w1 += L" ";
    s1 += ZtString<>(w2 = L" ");
    s3 = "World";
    w3 = L"World";
    s1 += s3;
    w1 += w3;
    ZuCheck(s1 == ZtString<>{w1});
    ZuCheck(w1 == ZtWString<>{s1});
  }

  {
    ZtWString<> w;
    w << L"hello " << "world" << L'!' << ' ' << 42;
    ZtString<> s = w;
    ZuCheck(s == "hello world! 42");
    s = {};
    s << L"hello " << "world" << L'!' << ' ' << 42;
    w = s;
    ZuCheck(w == L"hello world! 42");
  }

  {
    ZtString<> s;

    s += "Foo";

    bar(true, s);
    bar(false, s);
  }

  foo(ZtSprintf<ZtString<>>("%d = %s %s", 42, "Hello", "World"),
      ZtSprintf<ZtString<>>("%d = %s %s", 43, "Goodbye", "World"));

  {
    ZtString<> s(256);

    using uint = unsigned int;
    using ldouble = long double;

    s = int(42);
    s += ' ';
    s += uint(42);
    s += ' ';
    s += int16_t(42);
    s += ' ';
    s += uint16_t(42);
    s += ' ';
    s += int32_t(42);
    s += ' ';
    s += uint32_t(42);
    s += ' ';
    s += int64_t(42);
    s += ' ';
    s += uint64_t(42);
    s += ' ';
    s += float(42);
    s += ' ';
    s += double(42);
    s += ' ';
    s += ldouble(42);
    s += ' ';
    s += "Hello";
    s += ' ';
    s += "World!";
    s += ' ';
    s += "(11 x 42)";

    ZuCheck(s == "42 42 42 42 42 42 42 42 42 42 42 Hello World! (11 x 42)");
  }

  {
    using Queue = ZmList<ZtString<>>;

    Queue q;

    ZtString<> msg = "Hello World";
    q.push(msg);
    ZtString<> res = q.shiftVal();
    ZuCheck(res == "Hello World");
  }

  {
    ZtString<> s = "Hello World \r\n";
    s.chomp();
    ZuCheck(s == "Hello World");
    s.null(); s.chomp();
    ZuCheck(!s);
    s = "\r\n-\r\n\r\n\r\n"; s.chomp();
    ZuCheck(s == "\r\n-");
    s = " \t \t \r\n\r\n Hello World";
    s.strip();
    ZuCheck(s == "Hello World");
    s = " \t \t \r\n\r\n Hello World \r\n";
    s.strip();
    ZuCheck(s == "Hello World");
    s.null(); s.strip();
    ZuCheck(!s);
    s = " \t \t \r\n \r\n\r\n\r\n \t \t \r\n \r\n\r\n\r\n"; s.strip();
    ZuCheck(!s);
  }

  {
    char buf[12];
    ZtString<> s(buf, 0, 12, false);
    s += "Hello World";
    ZuCheck(s == "Hello World");
    ZuCheck(!s.vallocd());
    ZuCheck(s.data() == buf);
    s.splice(0, 5, "'Bye");
    ZuCheck(s == "'Bye World");
    ZuCheck(!s.vallocd());
    ZuCheck(s.data() == buf);
    s += " - and what a nice day";
    ZuCheck(s == "'Bye World - and what a nice day");
    ZuCheck(s.length() < ZtString<>::BuiltinSize || s.vallocd());
    ZuCheck(s.data() != buf);
  }

  {
    ZuCArray<16> s;
    s = "Hello World";
    s += ZuBox<int>(123456789);
    ZuCheck(s == "Hello World");
    s += ZuBox<int>(12345);
    ZuCheck(s == "Hello World");
    s << (ZuCArray<12>() << ZuBox<int>(1234));
    if (verbose)
      std::cerr << s.terminate() << '\n';
    ZuCheck(s == "Hello World1234");
    s = "";
    s << "Hello ";
    s << "World";
    ZuCheck(s == "Hello World");
  }

  {
    ZuCArray<2> s = "x";
    ZuCheck(s);
  }
  {
    ZuCArray<2> s = "";
    ZuCheck(!s);
  }
  {
    std::string s;
    s += ZuCArray<4>("foo");
    ZuCheck(s == "foo");
    s += ZtString<>(" bar");
    ZuCheck(s == "foo bar");
  }
  {
    std::stringstream s;
    s << ZuCArray<4>("foo");
    char buf[64];
    buf[s.rdbuf()->sgetn(buf, 63)] = 0;
    ZuCheck(!strcmp(buf, "foo"));
    s << ZuCArray<4>("foo") << ' ' << ZtString<>("bar");
    buf[s.rdbuf()->sgetn(buf, 63)] = 0;
    ZuCheck(!strcmp(buf, "foo bar"));
  }

  if (verbose)
    std::cerr << (ZtString<>{} << "hello " << "world") << '\n';

  {
    ZtString<> j = (ZtString<>{} << ZuJoin({ "x", "y" }, ","));
    ZuCheck(j == "x,y");
  }

  if (verbose) {
    std::cerr << ZuCSpan("Hello World 2\n") << std::flush;
    std::cerr << ZtHexDump{"Whoot!", "This\x1cis\x09""a\x05test\x01of\x04the\x1ehexadecimal\x13""dumper!", 42};
  }

  {
    ZtString<> s{"inline const char *"};
    // s[0] = 'X';
  }

  {
    using namespace ZtCase;
    snakeCamel("", [](ZuCSpan s) {
      ZuCheck(!s);
    });
    snakeCamel("a", [](ZuCSpan s) {
      ZuCheck(s == "a");
    });
    snakeCamel("aa", [](ZuCSpan s) {
      ZuCheck(s == "aa");
    });
    snakeCamel("aA0a", [](ZuCSpan s) {
      ZuCheck(s == "aA0a");
    });
    snakeCamel("_", [](ZuCSpan s) {
      ZuCheck(s == "_");
    });
    snakeCamel("__", [](ZuCSpan s) {
      ZuCheck(s == "__");
    });
    snakeCamel("___", [](ZuCSpan s) {
      ZuCheck(s == "___");
    });
    snakeCamel("_a_", [](ZuCSpan s) {
      ZuCheck(s == "A_");
    });
    snakeCamel("_a", [](ZuCSpan s) {
      ZuCheck(s == "A");
    });
    snakeCamel("_aa", [](ZuCSpan s) {
      ZuCheck(s == "Aa");
    });
    snakeCamel("a_a", [](ZuCSpan s) {
      ZuCheck(s == "aA");
    });
    snakeCamel("a_a_a", [](ZuCSpan s) {
      ZuCheck(s == "aAA");
    });
    snakeCamel("a_a_a_", [](ZuCSpan s) {
      ZuCheck(s == "aAA_");
    });
    snakeCamel("a_a_a__", [](ZuCSpan s) {
      ZuCheck(s == "aAA__");
    });
    camelSnake("", [](ZuCSpan s) {
      ZuCheck(!s);
    });
    camelSnake("a", [](ZuCSpan s) {
      ZuCheck(s == "a");
    });
    camelSnake("A", [](ZuCSpan s) {
      ZuCheck(s == "_a");
    });
    camelSnake("A_", [](ZuCSpan s) {
      ZuCheck(s == "_a_");
    });
    camelSnake("_A", [](ZuCSpan s) {
      ZuCheck(s == "__a");
    });
    camelSnake("_A0_", [](ZuCSpan s) {
      ZuCheck(s == "__a0_");
    });
  }
  {
    char buf[ZtString<>::BuiltinSize + 1];
    memset(buf, 'x', ZtString<>::BuiltinSize + 1);
    ZuCSpan span(&buf[0], ZtString<>::BuiltinSize);
    ZuCSpan under(&buf[0], ZtString<>::BuiltinSize - 1);
    ZuCSpan over(&buf[0], ZtString<>::BuiltinSize + 1);
    {
      ZtString<> s;
      s << span;
      ZuCheck(s == span);
    }
    {
      ZtString<> s;
      s << under;
      ZuCheck(s == under);
      s << 'x';
      ZuCheck(s == span);
      s << 'x';
      ZuCheck(s == over);
    }
  }
}

struct E {
  E() : m_ptr(this), m_copied(0), m_moved(0) { }
  ~E() {
    m_ptr = 0;
  }

  E(const E &e) :
      m_ptr(this), m_copied(e.m_copied + 1), m_moved(e.m_moved) { }
  E &operator =(const E &e) {
    if (this == &e) return *this;
    m_ptr = this;
    m_copied = e.m_copied + 1;
    m_moved = e.m_moved;
    return *this;
  }
  E(E &&e) :
      m_ptr(this), m_copied(e.m_copied), m_moved(e.m_moved + 1) {
    e.m_ptr = 0;
  }
  E &operator =(E &&e) {
    m_ptr = this;
    m_copied = e.m_copied;
    m_moved = e.m_moved + 1;
    e.m_ptr = 0;
    return *this;
  }

  void *ptr() const { return m_ptr; }
  int copied() const { return m_copied; }
  int moved() const { return m_moved; }

  void	*m_ptr;
  int	m_copied;
  int	m_moved;
};

void validate(const ZtArray<E> &a, uint64_t length)
{
  ZuTestScopeRT(validate);
  ZuCheckRT(a.length() == length);
  uint64_t n = a.length();
  for (uint64_t i = 0; i < n; i++)
    ZuCheckRT(a[i].ptr() == (const void *)&a[i],
      log_(i, ' ', ZuBoxPtr(a[i].ptr()).hex(), " != ", ZuBoxPtr(&a[i]).hex()),
      log_(i, ':', a[i].copied(), ':', a[i].moved()));
}

struct Foo {
  Foo() { }
  template <typename S> Foo(const S &s) : bar(s) { }
  ZtString<> bar;
};

template <auto &ZuTest_scope>
void testNonStringArrays()
{
  {
    E e[8];
    ZtArray<E> a;
    a = ZtArray<E>{e, 8, 8, false};
    ZtArray<E> b;

    ZuTestCall_("validate a len=8 (1)", validate, a, 8);
    a.splice(0, 0, ZuSpan(e, 4));
    ZuTestCall_("validate a len=12 (1)", validate, a, 12);
    a.splice(b, 0, 4);
    ZuTestCall_("validate a len=8 (2)", validate, a, 8);
    ZuTestCall_("validate b len=4 (1)", validate, b, 4);
    a.splice(0, 0, b);
    ZuTestCall_("validate a len=12 (2)", validate, a, 12);
    ZuTestCall_("validate b len=4 (2)", validate, b, 4);
    b.splice(8, 100, ZtArray<E>{b});
    ZuTestCall_("validate b len=12", validate, b, 12);
    for (int i = 0; i < 8; i++) a.push(a.shift());
    ZuTestCall_("validate a len=12 (3)", validate, a, 12);
  }
#if 0
  {
    ZtArray<ZtString<>> b = (const char *[]){ "hello", "world" };
    out(b[0]);
    out(b[1]);
  }
#endif

  {
    std::vector<const char *> v = { { "hello", "world" } };
    ZtArray<ZuCSpan> b = v;
    out(b[0].data());
    out(b[1].data());
  }

  {
    ZtArray<Foo> a;
    a.push(Foo("hello"));
    a.push(Foo("world"));
    out(a[0].bar);
    out(a[1].bar);
  }

#if 0
  {
    // typedef const char *P;
    // typedef P N[];
    ZtArray<const char *> a {
      std::initializer_list<const char *>{ [1] = "Foo", [0] = "Bar" } };
    out(a[0]);
    out(a[1]);
  }
#endif

  {
    ZtArray<ZuCSpan> a = { "Foo", "Bar" };
    out(a[0]);
    out(a[1]);
    for (auto &&s: a) out(s);
  }
  {
    ZtArray<ZtString<>> a = { "Foo", "Bar" };
    out(a[0]);
    out(a[1]);
    for (auto &&s: a) out(s);
  }

  {
    ZtArray<char> a = "hello world";
    auto ptr = a.data();
    ZtArray<unsigned char> b = ZuMv(a);
    ZtArray<signed char> c = ZuMv(b);
    a = ZuMv(c);
    ZuCheck(ptr == a.data());
  }

  {
    ZtArray<wchar_t> w;
    w << L"hello " << "world" << L'!' << ' ' << 42;
    ZtArray<char> s = w;
    ZuCheck(s == "hello world! 42");
    s = {};
    s << L"hello " << "world" << L'!' << ' ' << 42;
    w = s;
    ZuCheck(w == L"hello world! 42");
  }

  {
    ZtArray<ZtString<>> a = { "foo", "bar", "baz", "bah" };
    ZtString<> o;
    a.all([&o, first = true](const ZtString<> &s) mutable {
      if (first) first = false; else o << '.';
      o << s;
    });
    ZuCheck(o == "foo.bar.baz.bah");
    ZuCheck(a.find([](const ZtString<> &s) { return s == "baz"; }) == 2);
    ZuCheck(a.find([](const ZtString<> &s) { return s == "qux"; }) < 0);
    ZuCheck(a.starts(ZuSpan<ZtString<>>(a.data(), 2)));
    ZtArray<ZtString<>> b = { "foo", "baz" };
    ZuCheck(!a.starts(b));
  }

  {
    using Array =
      ZtArray<char,
	ZtArrayHeapID<"Bah",
	  ZtArrayHeapMin<1, ZtArrayHeapMax<1024>>>>;
    Array buf{1};
    buf << "foo";
    for (unsigned i = 0; i < 100; i++) buf << " bar";
  }

  {
    using Builtin =
      ZtBuiltin<ZtArray<char, ZtArrayHeapID<"ZtArrayTest.Builtin">>, 4>;
    Builtin b;
    auto ptr = b.data();
    b.length(0);
    ZuCheck(b.data() == ptr && b.size() == 4 && !b.vallocd() && !b.length());
    b.length(4);
    ZuCheck(b.data() == ptr && b.size() == 4 && !b.vallocd() &&
      b.length() == 4);
    b.length(0);
    ZuCheck(b.data() == ptr && b.size() == 4 && !b.vallocd() && !b.length());
  }
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();

  testStringEquiv<ZuTest_scope>();
  testNonStringArrays<ZuTest_scope>();

  return 0;
}
