//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuSpan.hh>
#include <zlib/ZuString.hh>

struct IntSink {
  enum { Cap = 8 };

  int		vals[Cap] = {};
  unsigned	len = 0;

  constexpr void operator ()(ZuSpan<int> span) {
    unsigned n = span.length();
    if (n > (Cap - len)) n = Cap - len;
    for (unsigned i = 0; i < n; ++i) vals[len++] = span[i];
  }
};

constexpr void restoreTail(ZuSpan<int> s, unsigned length)
{
  for (unsigned i = s.length(); i < length; ++i)
    ZuNew<int>(ZuAddr(s.data()[i]), 0);
}

constexpr bool spliceConstevalBasic()
{
  int buf[6] = {1, 2, 3, 4, 5, 6};
  ZuSpan<int> s(buf, 5);
  s.splice(1, 2);
  bool ok =
    s.length() == 3 &&
    s[0] == 1 &&
    s[1] == 4 &&
    s[2] == 5;
  restoreTail(s, 5);
  return ok;
}

constexpr bool spliceConstevalOffsetOnly()
{
  int buf[4] = {1, 2, 3, 4};
  ZuSpan<int> s(buf, 4);
  s.splice(0);
  bool ok = !s.length();
  restoreTail(s, 4);
  return ok;
}

constexpr bool spliceConstevalRemoved()
{
  int buf[4] = {1, 2, 3, 4};
  ZuSpan<int> s(buf, 4);
  IntSink removed;
  s.splice(removed, 1, 1);
  bool ok =
    removed.len == 1 &&
    removed.vals[0] == 2 &&
    s.length() == 3 &&
    s[0] == 1 &&
    s[1] == 3 &&
    s[2] == 4;
  restoreTail(s, 4);
  return ok;
}

constexpr bool spliceConstevalReplaceShrink()
{
  int buf[6] = {1, 2, 3, 4, 5, 6};
  ZuSpan<int> s(buf, 6);
  int removed = 0;
  s.splice([&removed](ZuSpan<int> span) {
    removed = span.length() ? span[0] : -1;
  }, 1, 2, [](ZuSpan<int> span) {
    ZuNew<int>(ZuAddr(span[0]), 20);
    return 0ULL;
  }, 1);
  bool ok =
    removed == 2 &&
    s.length() == 4 &&
    s[0] == 1 &&
    s[1] == 4 &&
    s[2] == 5 &&
    s[3] == 6;
  restoreTail(s, 6);
  return ok;
}

constexpr bool spliceConstevalRLengthClamp()
{
  int buf[5] = {1, 2, 3, 4, 5};
  ZuSpan<int> s(buf, 5);
  IntSink removed;
  unsigned replaced = 0;
  s.splice(removed, 1, 4, [&replaced](ZuSpan<int> span) {
    replaced = span.length();
    ZuNew<int>(ZuAddr(span[0]), 20);
    ZuNew<int>(ZuAddr(span[1]), 21);
    ZuNew<int>(ZuAddr(span[2]), 22);
    ZuNew<int>(ZuAddr(span[3]), 23);
    return span.length();
  }, 10);
  return
    replaced == 4 &&
    removed.len == 4 &&
    removed.vals[0] == 2 &&
    removed.vals[1] == 3 &&
    removed.vals[2] == 4 &&
    removed.vals[3] == 5 &&
    s.length() == 5 &&
    s[0] == 1 &&
    s[1] == 20 &&
    s[2] == 21 &&
    s[3] == 22 &&
    s[4] == 23;
}

constexpr bool spliceConstevalCase1NoOp()
{
  int buf[2] = {1, 2};
  int rbuf[1] = {9};
  ZuSpan<int> s(buf, 2);
  ZuSpan<int> removed(rbuf, 1);
  bool replaced = false;
  s.splice(removed, 3, 1, [&replaced](ZuSpan<int>) {
    replaced = true;
    return 1ULL;
  }, 1);
  return
    !removed.length() &&
    !replaced &&
    s.length() == 2 &&
    s[0] == 1 &&
    s[1] == 2;
}

ZuAssert(spliceConstevalBasic());
ZuAssert(spliceConstevalOffsetOnly());
ZuAssert(spliceConstevalRemoved());
ZuAssert(spliceConstevalReplaceShrink());
ZuAssert(spliceConstevalRLengthClamp());
ZuAssert(spliceConstevalCase1NoOp());

constexpr bool spanFindMatchConsteval()
{
  const char text[] = "aababc";
  ZuCSpan s{text, 6};
  return
    s.find("ab") == 1 &&
    s.find("abc") == 3 &&
    s.find("x") < 0 &&
    s.find("") == 0 &&
    s.find<"ab">() == 1 &&
    s.find<"abc">() == 3 &&
    s.find<"x">() < 0 &&
    s.find<"">() == 0 &&
    s.match("aa") &&
    !s.match("ab") &&
    s.match<"aa">() &&
    !s.match<"ab">() &&
    s.match<"">();
}

ZuAssert(spanFindMatchConsteval());

void testSpanSpliceRuntimePaths()
{
  ZuTestScope(splice_span_runtime);

  // length == 0 early return (5-arg core)
  {
    int buf[3] = {1, 2, 3};
    ZuSpan<int> s(buf, 3);
    IntSink removed;
    bool replaced = false;
    s.splice(removed, 0, 0, [&replaced](ZuSpan<int>) {
      replaced = true;
      return 0ULL;
    }, 0);
    ZuCheck(!removed.len);
    ZuCheck(!replaced);
    ZuCheck(s.length() == 3);
    ZuCheck(s[0] == 1);
    ZuCheck(s[1] == 2);
    ZuCheck(s[2] == 3);
  }

  // offset < 0 clamps to 0 (offset + length < 0)
  {
    int buf[3] = {1, 2, 3};
    ZuSpan<int> s(buf, 3);
    s.splice(-5, 1);
    ZuCheck(s.length() == 2);
    ZuCheck(s[0] == 2);
    ZuCheck(s[1] == 3);
  }

  // offset < 0 adjusted into range
  {
    int buf[4] = {1, 2, 3, 4};
    ZuSpan<int> s(buf, 4);
    s.splice(-2, 1);
    ZuCheck(s.length() == 3);
    ZuCheck(s[0] == 1);
    ZuCheck(s[1] == 2);
    ZuCheck(s[2] == 4);
  }

  // offset == length is a no-op (length clamps to 0)
  {
    int buf[2] = {1, 2};
    ZuSpan<int> s(buf, 2);
    s.splice(2, 1);
    ZuCheck(s.length() == 2);
    ZuCheck(s[0] == 1);
    ZuCheck(s[1] == 2);
  }

  // offset > length case 1: callable removed gets empty span, replace is not called
  {
    int buf[2] = {1, 2};
    ZuSpan<int> s(buf, 2);
    bool removedCalled = false;
    bool replaced = false;
    s.splice([&removedCalled](ZuSpan<int> span) {
      removedCalled = true;
      ZuCheck(span.length() == 0);
    }, 3, 1, [&replaced](ZuSpan<int>) {
      replaced = true;
      return 1ULL;
    }, 1);
    ZuCheck(removedCalled);
    ZuCheck(!replaced);
    ZuCheck(s.length() == 2);
    ZuCheck(s[0] == 1);
    ZuCheck(s[1] == 2);
  }

  // offset > length case 1: non-callable removed is reset to empty
  {
    int buf[2] = {1, 2};
    int rbuf[1] = {9};
    ZuSpan<int> s(buf, 2);
    ZuSpan<int> removed(rbuf, 1);
    bool replaced = false;
    s.splice(removed, 3, 1, [&replaced](ZuSpan<int>) {
      replaced = true;
      return 1ULL;
    }, 1);
    ZuCheck(!removed.length());
    ZuCheck(!replaced);
    ZuCheck(s.length() == 2);
    ZuCheck(s[0] == 1);
    ZuCheck(s[1] == 2);
  }

  // length < 0 with <= 0 result returns
  {
    int buf[3] = {1, 2, 3};
    ZuSpan<int> s(buf, 3);
    s.splice(1, -5);
    ZuCheck(s.length() == 3);
    ZuCheck(s[0] == 1);
    ZuCheck(s[1] == 2);
    ZuCheck(s[2] == 3);
  }

  // length < 0 adjusted positive
  {
    int buf[4] = {1, 2, 3, 4};
    ZuSpan<int> s(buf, 4);
    s.splice(1, -1);
    ZuCheck(s.length() == 2);
    ZuCheck(s[0] == 1);
    ZuCheck(s[1] == 4);
  }

  // offset + length clamp
  {
    int buf[4] = {1, 2, 3, 4};
    ZuSpan<int> s(buf, 4);
    s.splice(2, 5);
    ZuCheck(s.length() == 2);
    ZuCheck(s[0] == 1);
    ZuCheck(s[1] == 2);
  }
}

void testSpanSpliceVariantPaths()
{
  ZuTestScope(splice_span_variants);

  // splice(offset) overload
  {
    int buf[4] = {1, 2, 3, 4};
    ZuSpan<int> s(buf, 4);
    s.splice(0);
    ZuCheck(!s.length());
  }

  // splice(removed, offset, length) overload callable path
  {
    int buf[4] = {1, 2, 3, 4};
    ZuSpan<int> s(buf, 4);
    IntSink removed;
    s.splice(removed, 1, 1);
    ZuCheck(removed.len == 1);
    ZuCheck(removed.vals[0] == 2);
    ZuCheck(s.length() == 3);
    ZuCheck(s[0] == 1);
    ZuCheck(s[1] == 3);
    ZuCheck(s[2] == 4);
  }

  // splice(removed, offset, length) overload non-callable path
  {
    int buf[3] = {1, 2, 3};
    ZuSpan<int> s(buf, 3);
    ZuSpan<int> removed;
    s.splice(removed, 1, 1);
    ZuCheck(removed.length() == 1);
    ZuCheck(removed[0] == 3);
    ZuCheck(s.length() == 2);
    ZuCheck(s[0] == 1);
    ZuCheck(s[1] == 3);
  }

  // 5-arg core: rlength clamp path
  {
    int buf[5] = {1, 2, 3, 4, 5};
    ZuSpan<int> s(buf, 5);
    IntSink removed;
    unsigned replaced = 0;
    s.splice(removed, 1, 4, [&replaced](ZuSpan<int> span) {
      replaced = span.length();
      ZuNew<int>(ZuAddr(span[0]), 9);
      ZuNew<int>(ZuAddr(span[1]), 8);
      ZuNew<int>(ZuAddr(span[2]), 7);
      ZuNew<int>(ZuAddr(span[3]), 6);
      return span.length();
    }, 10);
    ZuCheck(removed.len == 4);
    ZuCheck(removed.vals[0] == 2);
    ZuCheck(removed.vals[1] == 3);
    ZuCheck(removed.vals[2] == 4);
    ZuCheck(removed.vals[3] == 5);
    ZuCheck(replaced == 4);
    ZuCheck(s.length() == 5);
    ZuCheck(s[0] == 1);
    ZuCheck(s[1] == 9);
    ZuCheck(s[2] == 8);
    ZuCheck(s[3] == 7);
    ZuCheck(s[4] == 6);
  }

  // 5-arg core: replace returns < reserved and shifts tail down
  {
    int buf[6] = {1, 2, 3, 4, 5, 6};
    ZuSpan<int> s(buf, 6);
    IntSink removed;
    s.splice(removed, 1, 2, [](ZuSpan<int> span) {
      ZuNew<int>(ZuAddr(span[0]), 20);
      ZuNew<int>(ZuAddr(span[1]), 21);
      return 1ULL;
    }, 2);
    ZuCheck(removed.len == 2);
    ZuCheck(removed.vals[0] == 2);
    ZuCheck(removed.vals[1] == 3);
    ZuCheck(s.length() == 5);
    ZuCheck(s[0] == 1);
    ZuCheck(s[1] == 20);
    ZuCheck(s[2] == 4);
    ZuCheck(s[3] == 5);
    ZuCheck(s[4] == 6);
  }

  // 5-arg core: tail truncation path + shrink after reserve
  {
    int buf[6] = {1, 2, 3, 4, 5, 6};
    ZuSpan<int> s(buf, 6);
    IntSink removed;
    s.splice(removed, 1, 3, [](ZuSpan<int> span) {
      ZuNew<int>(ZuAddr(span[0]), 20);
      ZuNew<int>(ZuAddr(span[1]), 21);
      ZuNew<int>(ZuAddr(span[2]), 22);
      ZuNew<int>(ZuAddr(span[3]), 23);
      return 3ULL;
    }, 4);
    ZuCheck(removed.len == 3);
    ZuCheck(removed.vals[0] == 2);
    ZuCheck(removed.vals[1] == 3);
    ZuCheck(removed.vals[2] == 4);
    ZuCheck(s.length() == 6);
    ZuCheck(s[0] == 1);
    ZuCheck(s[1] == 20);
    ZuCheck(s[2] == 21);
    ZuCheck(s[3] == 22);
    ZuCheck(s[4] == 5);
    ZuCheck(s[5] == 5);
  }
}

void testSpanSplice()
{
  ZuTestScope(splice_span_basic);

  // basic splice + shift
  {
    int buf[5] = {1, 2, 3, 4, 5};
    ZuSpan<int> s(buf, 5);
    s.splice(1, 2);
    ZuCheck(s.length() == 3);
    ZuCheck(s[0] == 1);
    ZuCheck(s[1] == 4);
    ZuCheck(s[2] == 5);
  }

  // offset < 0 clamps (offset + length)
  {
    int buf[4] = {1, 2, 3, 4};
    ZuSpan<int> s(buf, 4);
    s.splice(-2, 3);
    ZuCheck(s.length() == 2);
    ZuCheck(s[0] == 1);
    ZuCheck(s[1] == 2);
  }

  // length < 0 adjusted positive
  {
    int buf[4] = {1, 2, 3, 4};
    ZuSpan<int> s(buf, 4);
    s.splice(1, -1);
    ZuCheck(s.length() == 2);
    ZuCheck(s[0] == 1);
    ZuCheck(s[1] == 4);
  }

  // offset > length no-op
  {
    int buf[2] = {1, 2};
    ZuSpan<int> s(buf, 2);
    s.splice(3, 1);
    ZuCheck(s.length() == 2);
    ZuCheck(s[0] == 1);
    ZuCheck(s[1] == 2);
  }

  // length == 0 no-op
  {
    int buf[2] = {1, 2};
    ZuSpan<int> s(buf, 2);
    s.splice(0, 0);
    ZuCheck(s.length() == 2);
    ZuCheck(s[0] == 1);
    ZuCheck(s[1] == 2);
  }
}

void testSpanFindMatch()
{
  ZuTestScope(span_find_match);

  {
    ZuCSpan s{"hello world"};
    ZuCheck(s.find("hello") == 0);
    ZuCheck(s.find("world") == 6);
    ZuCheck(s.find("lo") == 3);
    ZuCheck(s.find("x") < 0);
    ZuCheck(s.find("") == 0);
    ZuCheck(s.match("hello"));
    ZuCheck(s.match("hello world"));
    ZuCheck(!s.match("world"));
    ZuCheck(!s.match("hello world!"));
    ZuCheck(s.match(""));
    ZuCheck(s.exact("hello world"));
    ZuCheck(!s.exact("hello"));
    ZuCheck(!s.exact("hello world!"));
  }

  {
    ZuCSpan s{"aababc"};
    ZuCheck(s.find<"ab">() == 1);
    ZuCheck(s.find<"abc">() == 3);
    ZuCheck(s.find<"aababc">() == 0);
    ZuCheck(s.find<"aababc!">() < 0);
    ZuCheck(s.find<"x">() < 0);
    ZuCheck(s.find<"">() == 0);
    ZuCheck(s.match<"aab">());
    ZuCheck(!s.match<"ab">());
    ZuCheck(s.match<"">());
  }

  {
    const uint8_t data[] = {'a', 'b', 'c', 'd', 'e'};
    ZuBSpan s{data, sizeof(data)};
    ZuCheck(s.find("bcd") == 1);
    ZuCheck(s.find<"bcd">() == 1);
    ZuCheck(s.find("xyz") < 0);
    ZuCheck(s.find<"xyz">() < 0);
    ZuCheck(s.match("abc"));
    ZuCheck(s.match<"abc">());
    ZuCheck(!s.match("abd"));
    ZuCheck(!s.match<"abd">());

    const signed char needle[] = {'c', 'd'};
    ZuCheck(s.find(ZuSpan<const signed char>{needle, 2}) == 2);
    ZuCheck(s.match(ZuSpan<const signed char>{needle, 2}) == false);
  }

  {
    const int data[] = {10, 20, 30, 40, 20, 30};
    const int needle[] = {20, 30};
    ZuSpan<const int> s{data, 6};
    ZuCheck(s.find(ZuSpan<const int>{needle, 2}) == 1);
    ZuCheck(s.match(ZuSpan<const int>{data, 3}));
    ZuCheck(!s.match(ZuSpan<const int>{needle, 2}));
  }

  {
    const float data[] = {-0.0F, 1.0F, 2.0F};
    const float needle[] = {0.0F, 1.0F};
    ZuSpan<const float> s{data, 3};
    ZuCheck(s.find(ZuSpan<const float>{needle, 2}) == 0);
    ZuCheck(s.match(ZuSpan<const float>{needle, 2}));
  }
}

void testSpanStrip()
{
  ZuTestScope(testSpanStrip);

  {
    char data[] = " \tabc \r\n";
    ZuSpan<char> s{data, sizeof(data) - 1};
    auto base = s.data();

    s.chomp();
    ZuCheck(s == " \tabc");
    ZuCheck(s.data() == base);

    s.trim();
    ZuCheck(s == "abc");
    ZuCheck(s.data() == base + 2);
  }

  {
    const char data[] = " \t abc \r\n";
    ZuCSpan s{data, sizeof(data) - 1};
    s.strip();
    ZuCheck(s == "abc");
    ZuCheck(s.data() == data + 3);
  }

  {
    const char data[] = " \t\r\n";
    ZuCSpan s{data, sizeof(data) - 1};
    s.strip();
    ZuCheck(!s);
    ZuCheck(!s.data());
  }

  {
    int data[] = {0, 0, 1, 2, 0};
    ZuSpan<int> s{data, 5};
    s.strip([](int v) { return !v; });
    ZuCheck(s.length() == 2);
    ZuCheck(s.data() == data + 2);
    ZuCheck(s[0] == 1);
    ZuCheck(s[1] == 2);
  }
}

int main()
{
  ZuTestMain();

  // initializer-list spans are non-owning; inspect them within the full
  // expression that owns the nested initializer lists
  ZuCheck(ZuSpan({{{42}}})[0][0][0] == 42);

  ZuTestCall(testSpanSplice);
  ZuTestCall(testSpanSpliceRuntimePaths);
  ZuTestCall(testSpanSpliceVariantPaths);
  ZuTestCall(testSpanFindMatch);
  ZuTestCall(testSpanStrip);

  {
    ZuBSpan foo("foo");
    ZuCSpan bar("bar");
    ZuCheck(foo == "foo");
    ZuCheck(foo != bar);
  }

  return 0;
}
