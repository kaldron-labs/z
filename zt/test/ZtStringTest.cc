//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdio.h>

#include <string>
#include <sstream>
#include <iostream>

#include <zlib/ZuLib.hh>

#include <zlib/ZuBox.hh>
#include <zlib/ZuArray.hh>
#include <zlib/ZuJoin.hh>

#include <zlib/ZmList.hh>

#include <zlib/ZtString.hh>
#include <zlib/ZtHexDump.hh>
#include <zlib/ZtCase.hh>

void out(bool ok, ZuCSpan check, ZuCSpan diag) {
  std::cout
    << (ok ? "OK  " : "NOK ") << check << ' ' << diag
    << '\n' << std::flush;
}

#define CHECK_(x) out((x), #x, "")
#define CHECK(x, y) out((x), #x, y)

void foo(const ZtString<> &s, ZtString<> t)
{
  puts(s);
  puts(t);
}

void bar(bool b, ZtString<> &s)
{
  ZtString<> baz;
  if (b)
    baz = s;
  else
    baz = "bah";
  puts(baz);
}

int main()
{
  ZtString<> s1, s2, s3, s4;

  s1 = (char *)0;
  s2 = "hello";
  s3 = "world";
  s4 = s3;

  puts(s2 + s1);
  puts(s3 + s1);
  puts(s2 + " " + s3);
  s2 += ZtString<>(" ") + s3;
  puts(s2);
  puts(s4);

  CHECK_((s3 == s4));
  CHECK_((s3 == s3));
  CHECK_((s2 == s2));
  CHECK_((s1 == s1));
  CHECK_((s2 != s3));
  CHECK_((s1 != s3));
  CHECK_(!s1);

  CHECK_((s3 > s2));
  CHECK_((s3 > s1));
  CHECK_((s3 >= s4));
  CHECK_((s3 >= s3));
  CHECK_((s3 >= s2));
  CHECK_((s3 >= s1));
  CHECK_((s2 < s3));
  CHECK_((s1 < s3));
  CHECK_((s3 <= s4));
  CHECK_((s3 <= s3));
  CHECK_((s2 <= s3));
  CHECK_((s1 <= s3));

  s2.splice(0, 5, "'bye ");

  CHECK_(s2 == "'bye  world");

  s2.splice(16, 3, "!!!");

  CHECK_(s2 == "'bye  world     !!!");

  s1.splice(2, 17, "hello world again");

  CHECK_(s1 == "  hello world again");

  s1.splice(0, 0, ZtString<>(0));

  CHECK_(s1 == "  hello world again");

  s1.splice(14, 15, "again and again");

  CHECK_(s1 == "  hello world again and again");

  s1 = "this string";
  CHECK_(s1 == "this string");
  s1.splice(0, 0, "beginning of ");
  CHECK_(s1 == "beginning of this string");
  s1.splice(0, 0, "inserted at ");
  CHECK_(s1 == "inserted at beginning of this string");

  s1 = "the string";
  CHECK_(s1 == "the string");
  s1.splice(4, 0, "middle of this ");
  CHECK_(s1 == "the middle of this string");
  s1.splice(s2, 0, 4, "inserted at the ");
  CHECK_(s2 == "the ");
  CHECK_(s1 == "inserted at the middle of this string");

  {
    ZtString<> s;
    s.sprintf("%s %.1d %.2d %.3d %s", "hello", 1, 2, 3, "world");
    CHECK_(s == "hello 1 02 003 world");
  }
  {
    ZtString<> s =
      ZtString<>().sprintf("%s %.1d %.2d %.3d %s", "goodbye", 1, 2, 3, "world");
    CHECK_(s == "goodbye 1 02 003 world");
  }
  {
    ZtWString<> w;
    w.sprintf(L"%ls %.1d %.2d %.3d %ls", L"hello", 1, 2, 3, L"world");
    ZtString<> s = w;
    CHECK_(s == "hello 1 02 003 world");
  }
  {
    ZtWString<> w = ZtWString<>{}.sprintf(
      L"%ls %.1d %.2d %.3d %ls", L"goodbye", 1, 2, 3, L"world");
    ZtString<> s = ZtString<>{w};
    CHECK_(s == "goodbye 1 02 003 world");
  }

  {
    ZtString<> s1, s2, s3;
    ZtWString<> w1, w2, w3;

    s1 += "Hello";
    w1 += ZtWString<>(s1);
    w1 += L" ";
    s1 += ZtString<>(w2 = L" ");
    s3 = "World";
    w3 = L"World";
    s1 += s3;
    w1 += w3;
    if (s1 != ZtString<>{w1}) {
      s2 = w1;
      // w2 = s1;
      // std::cout << "NOK \"" << w2 << "\" != \"" << w1 << "\"\n";
      std::cout << "NOK \"" << s1 << "\" != \"" << s2 << "\"\n";
    }
    if (w1 != ZtWString<>{s1}) {
      s2 = w1;
      // w2 = s1;
      // std::cout << "NOK \"" << w1 << "\" != \"" << w2 << "\"\n";
      std::cout << "NOK \"" << s2 << "\" != \"" << s1 << "\"\n";
    }
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

    CHECK_(s == "42 42 42 42 42 42 42 42 42 42 42 Hello World! (11 x 42)");
  }

  {
    using Queue = ZmList<ZtString<>>;

    Queue q;

    ZtString<> msg = "Hello World";
    q.push(msg);
    ZtString<> res = q.shiftVal();
    CHECK_(res == "Hello World");
  }

  {
    ZtString<> s = "Hello World \r\n";
    s.chomp();
    CHECK(s == "Hello World", "chomp() 1");
    s.null(); s.chomp();
    CHECK(!s, "chomp() 2");
    s = "\r\n-\r\n\r\n\r\n"; s.chomp();
    CHECK(s == "\r\n-", "chomp() 3");
    s = " \t \t \r\n\r\n Hello World";
    s.strip();
    CHECK(s == "Hello World", "strip() 1");
    s = " \t \t \r\n\r\n Hello World \r\n";
    s.strip();
    CHECK(s == "Hello World", "strip() 2");
    s.null(); s.strip();
    CHECK(!s, "strip() 3");
    s = " \t \t \r\n \r\n\r\n\r\n \t \t \r\n \r\n\r\n\r\n"; s.strip();
    CHECK(!s, "strip() 4");
  }

  {
    char buf[12];
    ZtString<> s(buf, 0, 12, false);
    s += "Hello World";
    CHECK_(s == "Hello World");
    CHECK(!s.vallocd(), "buffer 1");
    CHECK(s.data() == buf, "buffer 2");
    s.splice(0, 5, "'Bye");
    CHECK_(s == "'Bye World");
    CHECK(!s.vallocd(), "buffer 3");
    CHECK(s.data() == buf, "buffer 4");
    s += " - and what a nice day";
    CHECK_(s == "'Bye World - and what a nice day");
    CHECK(s.length() < ZtString<>::BuiltinSize || s.vallocd(), "buffer 5");
    CHECK(s.data() != buf, "buffer 6");
  }

  {
    ZuCArray<16> s;
    s = "Hello World";
    s += ZuBox<int>(123456789);
    CHECK(s == "Hello World", "ZuCArray append 1");
    s += ZuBox<int>(12345);
    CHECK(s == "Hello World", "ZuCArray append 2");
    s << (ZuCArray<12>() << ZuBox<int>(1234));
    puts(s.terminate());
    CHECK(s == "Hello World1234", "ZuCArray append 3");
    s = "";
    s << "Hello ";
    s << "World";
    CHECK(s == "Hello World", "ZuCArray append 4");
  }

  {
    if (ZuCArray<2> s = "x") std::cout << "OK  ZuCArray as boolean true\n";
    else std::cout << "NOK ZuCArray as boolean true\n";
    if (ZuCArray<2> s = "") std::cout << "NOK ZuCArray as boolean false\n";
    else std::cout << "OK  ZuCArray as boolean false\n";
  }
  {
    std::string s;
    s += ZuCArray<4>("foo");
    CHECK(s == "foo", "ZuCArray appending to std::string");
    s += ZtString<>(" bar");
    CHECK(s == "foo bar", "ZtString<> appending to std::string");
  }
  {
    std::stringstream s;
    s << ZuCArray<4>("foo");
    char buf[64];
    buf[s.rdbuf()->sgetn(buf, 63)] = 0;
    CHECK(!strcmp(buf, "foo"), "ZuCArray writing to std::ostream");
    s << ZuCArray<4>("foo") << ' ' << ZtString<>("bar");
    buf[s.rdbuf()->sgetn(buf, 63)] = 0;
    CHECK(!strcmp(buf, "foo bar"), "ZtString<> writing to std::ostream");
  }

  std::cout << (ZtString<>{} << "hello " << "world") << '\n';

  {
    ZtString<> j = (ZtString<>{} << ZuJoin({ "x", "y" }, ","));
    CHECK(j == "x,y", "ZuJoin");
  }

  {
    std::cout << ZuCSpan("Hello World 2\n") << std::flush;
    std::cout << ZtHexDump{"Whoot!", "This\x1cis\x09""a\x05test\x01of\x04the\x1ehexadecimal\x13""dumper!", 42};
  }

  {
    ZtString<> s{"inline const char *"};
    // s[0] = 'X';
  }

  {
    using namespace ZtCase;
    snakeCamel("", [](ZuCSpan s) {
      CHECK(!s, "snakeCamel(\"\")");
    });
    snakeCamel("a", [](ZuCSpan s) {
      CHECK(s == "a", "snakeCamel(\"a\")");
    });
    snakeCamel("aa", [](ZuCSpan s) {
      CHECK(s == "aa", "snakeCamel(\"aa\")");
    });
    snakeCamel("aA0a", [](ZuCSpan s) {
      CHECK(s == "aA0a", "snakeCamel(\"aA0a\")");
    });
    snakeCamel("_", [](ZuCSpan s) {
      CHECK(s == "_", "snakeCamel(\"_\")");
    });
    snakeCamel("__", [](ZuCSpan s) {
      CHECK(s == "__", "snakeCamel(\"__\")");
    });
    snakeCamel("___", [](ZuCSpan s) {
      CHECK(s == "___", "snakeCamel(\"___\")");
    });
    snakeCamel("_a_", [](ZuCSpan s) {
      CHECK(s == "A_", "snakeCamel(\"_a_\")");
    });
    snakeCamel("_a", [](ZuCSpan s) {
      CHECK(s == "A", "snakeCamel(\"_a\")");
    });
    snakeCamel("_aa", [](ZuCSpan s) {
      CHECK(s == "Aa", "snakeCamel(\"_aa\")");
    });
    snakeCamel("a_a", [](ZuCSpan s) {
      CHECK(s == "aA", "snakeCamel(\"a_a\")");
    });
    snakeCamel("a_a_a", [](ZuCSpan s) {
      CHECK(s == "aAA", "snakeCamel(\"a_a_a\")");
    });
    snakeCamel("a_a_a_", [](ZuCSpan s) {
      CHECK(s == "aAA_", "snakeCamel(\"a_a_a_\")");
    });
    snakeCamel("a_a_a__", [](ZuCSpan s) {
      CHECK(s == "aAA__", "snakeCamel(\"a_a_a__\")");
    });
    camelSnake("", [](ZuCSpan s) {
      CHECK(!s, "camelSnake(\"\")");
    });
    camelSnake("a", [](ZuCSpan s) {
      CHECK(s == "a", "camelSnake(\"a\")");
    });
    camelSnake("A", [](ZuCSpan s) {
      CHECK(s == "_a", "camelSnake(\"a\")");
    });
    camelSnake("A_", [](ZuCSpan s) {
      CHECK(s == "_a_", "camelSnake(\"a\")");
    });
    camelSnake("_A", [](ZuCSpan s) {
      CHECK(s == "__a", "camelSnake(\"a\")");
    });
    camelSnake("_A0_", [](ZuCSpan s) {
      CHECK(s == "__a0_", "camelSnake(\"a\")");
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
      CHECK(s == span, "appending BuiltinSize");
    }
    {
      ZtString<> s;
      s << under;
      CHECK(s == under, "appending under BuiltinSize");
      s << 'x';
      CHECK(s == span, "incrementally appending to BuiltinSize");
      s << 'x';
      CHECK(s == over, "appending over BuiltinSize");
    }
  }
}
