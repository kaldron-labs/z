//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string>
#include <sstream>
#include <iostream>

#include <zlib/ZuLib.hh>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuBox.hh>
#include <zlib/ZuArray.hh>
#include <zlib/ZuJoin.hh>

#include <zlib/ZmList.hh>

#include <zlib/ZtString.hh>
#include <zlib/ZtScratch.hh>
#include <zlib/ZtHexDump.hh>
#include <zlib/ZtCase.hh>

using namespace ZuTestUtil;

ZuDerive(SecretScratch, (ZtString<ZtStringSharded<true,
  ZtStringSecret<true, ZtStringHeapID<"ZtStringTest.Secret">>>>));

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
void testSpliceCoverage()
{
  // splice(offset)
  {
    ZtString<> s = "abcdef";
    s.splice(3);
    ZuCheck(s == "abc");
  }
  {
    ZtString<> s = "abc"; // shadow literal
    ZuCheck(!s.mutable_());
    s.splice(1, 1, "Z");
    ZuCheck(s.mutable_());
    ZuCheck(s.builtin());
    ZuCheck(s == "aZc");
  }

  // splice(offset, length) including clamp/normalization paths
  {
    ZtString<> s = "abcdef";
    s.splice(2, 100);
    ZuCheck(s == "ab");
  }
  {
    ZtString<> s = "abcdef";
    s.splice(-2, -1);
    ZuCheck(s == "abcdf");
  }
  {
    ZtString<> s = "abcdef";
    s.splice(2, -20);
    ZuCheck(s == "abcdef");
  }
  {
    ZtString<> s = "abcdef";
    s.splice(-20, 2);
    ZuCheck(s == "cdef");
  }

  // splice(removed, offset, length)
  {
    ZtString<> s = "abcdef";
    ZtString<> removed;
    s.splice([&removed](ZuSpan<char> span) {
      removed = ZuSpan<const char>(span.data(), span.length());
    }, 2, 2);
    ZuCheck(removed == "cd");
    ZuCheck(s == "abef");
  }
  {
    ZtString<> s = "abcdef";
    ZuSpan<char> removed;
    s.splice(removed, 2, 2);
    ZuCheck(removed.length() == 2);
    ZuCheck(s == "abef");
  }

  // MatchString overloads
  {
    ZtString<> s = "abcdef";
    ZtString<> repl = "XY";
    s.splice(2, 3, repl);
    ZuCheck(s == "abXYf");
  }
  {
    ZtString<> s = "abcdef";
    ZtString<> removed;
    ZtString<> repl = "12";
    s.splice([&removed](ZuSpan<char> span) {
      removed = ZuSpan<const char>(span.data(), span.length());
    }, 1, 3, repl);
    ZuCheck(removed == "bcd");
    ZuCheck(s == "a12ef");
  }
  {
    ZtString<> s = "abcdef";
    s.splice(2, 2, s); // self-splice path
    ZuCheck(s == "ababcdefef");
  }

  // MatchAnyCString overloads
  {
    ZtString<> s = "abcdef";
    const char *repl = "ZZ";
    s.splice(1, 2, repl);
    ZuCheck(s == "aZZdef");
  }
  {
    ZtString<> s = "abcdef";
    ZtString<> removed;
    const char *repl = "Q";
    s.splice([&removed](ZuSpan<char> span) {
      removed = ZuSpan<const char>(span.data(), span.length());
    }, 1, 2, repl);
    ZuCheck(removed == "bc");
    ZuCheck(s == "aQdef");
  }

  // MatchOtherString overloads
  {
    const char replBuf[] = { 'M', 'N' };
    ZuSpan<const char> repl(&replBuf[0], 2);
    ZtString<> s = "abcdef";
    s.splice(2, 2, repl);
    ZuCheck(s == "abMNef");
  }
  {
    const char replBuf[] = { 'P' };
    ZuSpan<const char> repl(&replBuf[0], 1);
    ZtString<> s = "abcdef";
    ZtString<> removed;
    s.splice([&removed](ZuSpan<char> span) {
      removed = ZuSpan<const char>(span.data(), span.length());
    }, 2, 2, repl);
    ZuCheck(removed == "cd");
    ZuCheck(s == "abPef");
  }

  // MatchAltString overloads
  {
    ZtString<> s = "abcdef";
    ZtWString<> repl = L"WX";
    s.splice(2, 2, repl);
    ZuCheck(s == "abWXef");
  }
  {
    ZtString<> s = "abcdef";
    ZtString<> removed;
    ZtWString<> repl = L"R";
    s.splice([&removed](ZuSpan<char> span) {
      removed = ZuSpan<const char>(span.data(), span.length());
    }, 2, 2, repl);
    ZuCheck(removed == "cd");
    ZuCheck(s == "abRef");
  }

  // MatchAltChar overloads
  {
    ZtString<> s = "abcdef";
    wchar_t repl = L'Z';
    s.splice(int64_t{2}, int64_t{2}, repl);
    ZuCheck(s == "abZef");
  }
  {
    ZtString<> s = "abcdef";
    ZtString<> removed;
    wchar_t repl = L'K';
    s.splice([&removed](ZuSpan<char> span) {
      removed = ZuSpan<const char>(span.data(), span.length());
    }, 2, 2, repl);
    ZuCheck(removed == "cd");
    ZuCheck(s == "abKef");
  }

  // offset > length path with callable and non-callable removed
  {
    ZtString<> s = "ab";
    ZtString<> removed;
    s.splice([&removed](ZuSpan<char> span) {
      removed = ZuSpan<const char>(span.data(), span.length());
    }, 5, 3, "XY");
    ZuCheck(!removed);
    ZuCheck(s == "ab   XY");
  }
  {
    ZtString<> s(64);
    const char *seed = "ab";
    s = seed;
    ZuSpan<char> removed;
    s.splice(removed, 5, 1, "Q");
    ZuCheck(removed.length() == 0);
    ZuCheck(s == "ab   Q");
  }

  // direct splice(Removed, offset, length, Replace, rlength) paths
  {
    ZtString<> s = "abc"; // shadow literal
    bool replaced = false;
    s.splice([](ZuSpan<char>) { }, 0, 3,
      [&replaced](ZuSpan<char>) -> uint64_t {
	replaced = true;
	return 0;
      }, 0);
    ZuCheck(s.mutable_());
    ZuCheck(!replaced); // l <= 0 path skips replace()
    ZuCheck(!s);
  }
  {
    ZtString<> s(64);
    const char *seed = "ABCDE";
    s = seed;
    s.splice([](ZuSpan<char>) { }, 1, 1,
      [](ZuSpan<char> span) -> uint64_t {
	span[0] = 'X';
	span[1] = 'Y';
	span[2] = 'Z';
	return 2;
      }, 3);
    ZuCheck(s == "AXYCDE"); // nrlength < rlength path
  }

  // tail shift paths in the non-reallocation branch
  {
    ZtString<> s(64);
    const char *seed = "ABCDE";
    s = seed;
    s.splice(1, 1, "XYZ");
    ZuCheck(s == "AXYZCDE"); // rlength > length
  }
  {
    ZtString<> s(64);
    const char *seed = "ABCDE";
    s = seed;
    s.splice(1, 3, "Q");
    ZuCheck(s == "AQE"); // rlength < length
  }
  {
    ZtString<> s(64);
    const char *seed = "abc";
    s = seed;
    s.splice(3, 0, "ZZ");
    ZuCheck(s == "abcZZ"); // tail == 0
  }

  // growth/reallocation paths
  {
    ZtString<> s = "abc";
    ZuCheck(!s.mutable_());
    s.splice(1, 1, "123");
    ZuCheck(s == "a123c");
    ZuCheck(s.mutable_());
  }
  {
    ZtString<> s(64);
    const char *seed = "abc";
    s = seed;
    ZtString<> repl(256);
    for (unsigned i = 0; i < 100; i++) repl << 'x';
    s.splice(1, 0, repl);
    ZuCheck(s.length() == 103);
    ZuCheck(s[0] == 'a');
    ZuCheck(s[1] == 'x');
    ZuCheck(s[102] == 'c');
  }
}

template <auto &ZuTest_scope>
void testString()
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
  {
    ZtString<> s = "hello world";
    ZuCheck(s.find([](char c) { return c == 'w'; }) == 6);
    ZuCheck(s.find([](char c) { return c == 'z'; }) < 0);
    ZuCheck(s.rfind([](char c) { return c == 'l'; }) == 9);
    ZuCheck(s.rfind([](char c) { return c == 'z'; }) < 0);
    ZuCheck(s.rfind("l") == 9);
    ZuCheck(s.rfind<"hello">() == 0);
    ZuCheck(s.match("hello"));
    ZuCheck(s.match(ZuCSpan{"hello"}));
    ZuCheck(!s.match("world"));
    ZuCheck(s.rmatch("world"));
    ZuCheck(s.rmatch(ZuCSpan{"world"}));
    ZuCheck(!s.rmatch("hello"));
    ZuCheck(s.rmatch<"world">());
  }
}

template <auto &ZuTest_scope>
void testSecretString()
{
  using SecretString = ZtString<ZtStringSecret<true,
    ZtStringHeapID<"ZtStringTest.Secret">>>;
  SecretString s = "abcdefgh";
  ZuCheck(!s.mutable_());
  s.size(32);
  s.splice([](ZuSpan<char>) { }, 2, 1,
    [](ZuSpan<char> span) -> uint64_t {
      span[0] = 'X';
      return 1;
  }, 4);
  ZuCheck(s == "abXdefgh");
  bool cleared = true;
  for (unsigned i = 8; i < 11; i++) cleared &= !s.data()[i];
  ZuCheck(cleared);
  s.length(4);
  ZuCheck(s == "abXd");
  cleared = true;
  for (unsigned i = 4; i < 8; i++) cleared &= !s.data()[i];
  ZuCheck(cleared);
  s.clear();
  ZuCheck(!s.length() && s.data()[0] == 0);

  SecretString compact = "abcdef";
  compact.size(7);
  compact = ZuCSpan{"abcdefghij"};
  ZuCheck(compact == "abcdefghij" && compact.builtin());
  compact = ZuCSpan{"abcdefghijklmnop"};
  ZuCheck(compact == "abcdefghijklmnop" && !compact.builtin());
  compact = ZuCSpan{"abcdefghijklmnopqrstuvwxyz"};
  ZuCheck(compact == "abcdefghijklmnopqrstuvwxyz" && !compact.builtin());

  char external[32] = "secret";
  SecretString owned{external, 6, sizeof(external), false};
  owned = ZuCSpan{"abcdefghijklmnopqrstuvwxyz0123456789"};
  ZuCheck(owned == "abcdefghijklmnopqrstuvwxyz0123456789");
  cleared = true;
  for (auto c: external) cleared &= !c;
  ZuCheck(cleared);

  SecretString source{ZuCSpan{"secret"}};
  SecretString moved{ZuMv(source)};
  ZuCheck(moved == "secret" && !source.length());
  ZuCheck(source.data()[0] == 0);

  SecretString grow{ZuCSpan{"abcdefghijklmnop"}};
  grow.size(17);
  grow.splice(2, 1, "01234567890123456789");
  ZuCheck(grow == "ab01234567890123456789defghijklmnop");
  grow.null();
  ZuCheck(!grow);

  SecretString shadow{"unchanged"};
  ZuCheck(!shadow.mutable_());
  shadow.null();

  auto scratch = ZtScratch(SecretScratch, 32);
  scratch = ZuCSpan{"secret"};
  scratch.clear();
  ZuCheck(!scratch.length() && !scratch.data()[0]);

  compact = ZuCSpan{"secret"};
  compact.size(7);
  compact.data()[2] = 0;
  compact.calcLength();
  ZuCheck(compact == "se");
  cleared = true;
  for (unsigned i = 2; i < 6; ++i) cleared &= !compact.data()[i];
  ZuCheck(cleared);
  compact = ZuCSpan{"secret"};
  compact.size(4);
  ZuCheck(compact == "sec" && !compact.data()[4] && !compact.data()[5]);

  compact = ZuCSpan{"abcd"};
  compact.size(32);
  compact.splice([](ZuSpan<char>) { }, 6, 0,
    [](ZuSpan<char> span) -> uint64_t {
      memset(span.data(), 'X', span.length());
      return 1;
    }, 4);
  ZuCheck(compact == "abcd  X");
  ZuCheck(!compact.data()[7] && !compact.data()[8] && !compact.data()[9]);

  compact = ZuCSpan{"abcdefghijklmnopqrstuvwx"};
  compact.size(25);
  compact.splice([](ZuSpan<char>) { }, 22, 2,
    [](ZuSpan<char> span) -> uint64_t {
      memset(span.data(), 'X', span.length());
      return 1;
    }, 8);
  ZuCheck(compact == "abcdefghijklmnopqrstuvX");
  cleared = true;
  for (unsigned i = 23; i < 30; ++i) cleared &= !compact.data()[i];
  ZuCheck(cleared);

  using Wide = ZtWString<ZtStringSecret<true,
    ZtStringHeapID<"ZtStringTest.Secret">>>;
  Wide formatted{ZuBox<int>{12}.fmt()};
  ZuCheck(formatted == L"12");
  formatted = ZuBox<int>{34};
  formatted << ZuBox<int>{56};
  ZuCheck(formatted == L"3456");
  ZuCheck(formatted + ZuBox<int>{78} == L"345678");
  ZuCheck(ZtWString<>{L"v="} + ZuBox<int>{12} == L"v=12");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();

  testSpliceCoverage<ZuTest_scope>();
  testString<ZuTest_scope>();
  testSecretString<ZuTest_scope>();

  return 0;
}
