//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuArray.hh>
#include <zlib/ZuSpan.hh>
#include <zlib/ZuString.hh>

class I {
public:
  I() : m_i(0) { }
  I(int i) : m_i(i) { }
  ~I() { m_i = 0; }

  I(const I &i) : m_i(i.m_i) { }
  I &operator =(const I &i) {
    if (ZuLikely(this != &i)) m_i = i.m_i;
    return *this;
  }

  I &operator =(int i) { m_i = i; return *this; }

  operator int() const { return m_i; }

private:
  int	m_i;
};

template <typename T>
struct IntSink {
  enum { Cap = 8 };

  int		vals[Cap] = {};
  unsigned	len = 0;

  constexpr void operator ()(ZuSpan<ZuElem<T>> span) {
    unsigned n = span.length();
    if (n > (Cap - len)) n = Cap - len;
    if (!ZuConstEval() && ZuIsSame<T, int>{}) {
      if constexpr (ZuIsSame<T, int>{})
	memcpy(&vals[len], &span[0], n * sizeof(int));
    } else {
      for (unsigned i = 0; i < n; ) vals[len++] = span[i++];
    }
  }
};

template <typename T, unsigned N>
struct AppendSink {
  T		vals[N] = {};
  unsigned	len = 0;

  constexpr void operator ()(ZuSpan<ZuElem<T>> span) {
    unsigned n = span.length();
    if (n > (N - len)) n = N - len;
    for (unsigned i = 0; i < n; ) vals[len++] = span[i++];
  }
};

template <typename S, typename R, typename = void>
struct HasSubFind : public ZuFalse { };
template <typename S, typename R>
struct HasSubFind<S, R,
  decltype(ZuDeclVal<const S &>().find(ZuDeclVal<const R &>()), void())> :
    public ZuTrue { };

template <typename S, typename = void>
struct HasFixedFind : public ZuFalse { };
template <typename S>
struct HasFixedFind<S,
  decltype(ZuDeclVal<const S &>().template find<"ab">(), void())> :
    public ZuTrue { };

template <typename S, typename = void>
struct HasFixedMatch : public ZuFalse { };
template <typename S>
struct HasFixedMatch<S,
  decltype(ZuDeclVal<const S &>().template match<"ab">(), void())> :
    public ZuTrue { };

constexpr bool spliceConstevalBasic()
{
  ZuArray<int, 6> a;
  a << 10 << 20 << 30 << 40;
  a.splice(1, 2);
  return a.length() == 2 && a[0] == 10 && a[1] == 40;
}

constexpr bool spliceConstevalRemoved()
{
  ZuArray<int, 6> a;
  a << 1 << 2 << 3 << 4;
  IntSink<int> removed;
  a.splice(removed, 0, 2);
  return
    a.length() == 2 &&
    a[0] == 3 &&
    a[1] == 4 &&
    removed.len == 2 &&
    removed.vals[0] == 1 &&
    removed.vals[1] == 2;
}

constexpr bool spliceConstevalOffsetOnly()
{
  ZuArray<int, 6> a;
  a << 1 << 2 << 3;
  a.splice(0);
  return !a.length();
}

constexpr bool spliceConstevalReplaceShrink()
{
  ZuArray<int, 7> a;
  a << 1 << 2 << 3 << 4 << 5;
  int removed = 0;
  a.splice([&removed](auto span) {
    removed = span.length() ? span[0] : -1;
  }, 1, 1, [](auto span) {
    ZuNew<int>(ZuAddr(span[0]), 20);
    ZuNew<int>(ZuAddr(span[1]), 21);
    ZuNew<int>(ZuAddr(span[2]), 22);
    return 2U;
  }, 3);
  return
    removed == 2 &&
    a.length() == 6 &&
    a[0] == 1 &&
    a[1] == 20 &&
    a[2] == 21 &&
    a[3] == 3 &&
    a[4] == 4 &&
    a[5] == 5;
}

constexpr bool spliceConstevalCase1Replace()
{
  ZuArray<int, 6> a;
  a << 9;
  ZuSpan<ZuElem<int>> removed;
  unsigned replaced = 0;
  a.splice(removed, 3, 2, [&replaced](auto span) {
    replaced = span.length();
    ZuNew<int>(ZuAddr(span[0]), 7);
    ZuNew<int>(ZuAddr(span[1]), 8);
    return 2U;
  }, 2);
  return
    !removed.length() &&
    replaced == 2 &&
    a.length() == 5 &&
    a[0] == 9 &&
    a[1] == 0 &&
    a[2] == 0 &&
    a[3] == 7 &&
    a[4] == 8;
}

static_assert(spliceConstevalBasic());
static_assert(spliceConstevalRemoved());
static_assert(spliceConstevalOffsetOnly());
static_assert(spliceConstevalReplaceShrink());
static_assert(spliceConstevalCase1Replace());
static_assert(HasSubFind<ZuCSpan, ZuCSpan>{});
static_assert(!HasSubFind<ZuSpan<const double>, ZuSpan<const double>>{});
static_assert(HasFixedFind<ZuCSpan>{});
static_assert(HasFixedFind<ZuBSpan>{});
static_assert(!HasFixedFind<ZuSpan<const double>>{});
static_assert(HasFixedMatch<ZuCSpan>{});
static_assert(HasFixedMatch<ZuBSpan>{});
static_assert(!HasFixedMatch<ZuSpan<const double>>{});

constexpr bool findMatchConsteval()
{
  ZuArray<int, 6> a;
  a << 1 << 2 << 3 << 4;
  ZuArray<int, 3> prefix;
  prefix << 1 << 2 << 3;
  ZuArray<int, 3> mismatch;
  mismatch << 1 << 3;
  constexpr ZuString s{"abcdef"};
  ZuCSpan span{"abcdef"};
  return
    a.find([](int v) { return v == 3; }) == 2 &&
    a.find([](int v) { return v == 9; }) < 0 &&
    a.match(prefix) &&
    !a.match(mismatch) &&
    s.find([](char c) { return c == 'd'; }) == 3 &&
    s.find([](char c) { return c == 'x'; }) < 0 &&
    span.find("cd") == 2 &&
    span.find("gh") < 0 &&
    span.find("") == 0 &&
    span.find<"cd">() == 2 &&
    span.find<"gh">() < 0 &&
    span.find<"">() == 0 &&
    span.match("abc") &&
    !span.match("abd") &&
    span.match<"abc">() &&
    !span.match<"abd">() &&
    span.match<"">() &&
    s.match("abc") &&
    !s.match("abd");
}

static_assert(findMatchConsteval());

void testSpliceBasicPaths()
{
  ZuTestScope(splice_basic);

  // basic splice + shift
  {
    ZuArray<I, 5> a;
    a << I(1) << I(2) << I(3) << I(4) << I(5);
    a.splice(1, 2);
    ZuCheck(a.length() == 3);
    ZuCheck((int)a[0] == 1);
    ZuCheck((int)a[1] == 4);
    ZuCheck((int)a[2] == 5);
  }

  // offset < 0 clamps (offset + length)
  {
    ZuArray<I, 4> a;
    a << I(1) << I(2) << I(3) << I(4);
    a.splice(-2, 3);
    ZuCheck(a.length() == 2);
    ZuCheck((int)a[0] == 1);
    ZuCheck((int)a[1] == 2);
  }

  // length < 0 adjusted positive
  {
    ZuArray<I, 4> a;
    a << I(1) << I(2) << I(3) << I(4);
    a.splice(1, -1);
    ZuCheck(a.length() == 2);
    ZuCheck((int)a[0] == 1);
    ZuCheck((int)a[1] == 4);
  }

  // offset > length_ case 1: gap initialized
  {
    ZuArray<I, 5> a;
    a << I(7);
    a.splice(3, 1);
    ZuCheck(a.length() == 3);
    ZuCheck((int)a[0] == 7);
    ZuCheck((int)a[1] == 0);
    ZuCheck((int)a[2] == 0);
  }

  // length == 0 no-op
  {
    ZuArray<I, 3> a;
    a << I(1) << I(2);
    a.splice(0, 0);
    ZuCheck(a.length() == 2);
    ZuCheck((int)a[0] == 1);
    ZuCheck((int)a[1] == 2);
  }
}

void testSpliceRuntimePaths()
{
  ZuTestScope(splice_runtime);

  // length == 0 early return
  {
    ZuArray<I, 3> a;
    a << I(1) << I(2);
    a.splice(0, 0);
    ZuCheck(a.length() == 2);
    ZuCheck((int)a[0] == 1);
    ZuCheck((int)a[1] == 2);
  }

  // offset < 0 clamps to 0 (offset + length_ < 0)
  {
    ZuArray<I, 3> a;
    a << I(9);
    a.splice(-5, 1);
    ZuCheck(!a.length());
  }

  // offset == N enters case 1 and grows with default initialization
  {
    ZuArray<I, 2> a;
    a << I(5);
    a.splice(2, 1);
    ZuCheck(a.length() == 2);
    ZuCheck((int)a[0] == 5);
    ZuCheck((int)a[1] == 0);
  }

  // offset == length_ returns via length_ - offset == 0
  {
    ZuArray<I, 3> a;
    a << I(1) << I(2);
    a.splice(2, 1);
    ZuCheck(a.length() == 2);
    ZuCheck((int)a[0] == 1);
    ZuCheck((int)a[1] == 2);
  }

  // length < 0 with <= 0 result returns
  {
    ZuArray<I, 3> a;
    a << I(1) << I(2);
    a.splice(1, -5);
    ZuCheck(a.length() == 2);
    ZuCheck((int)a[0] == 1);
    ZuCheck((int)a[1] == 2);
  }

  // length < 0 adjusted positive
  {
    ZuArray<I, 5> a;
    a << I(1) << I(2) << I(3) << I(4);
    a.splice(1, -1);
    ZuCheck(a.length() == 2);
    ZuCheck((int)a[0] == 1);
    ZuCheck((int)a[1] == 4);
  }

  // offset > length_ init path
  {
    ZuArray<I, 5> a;
    a << I(7);
    a.splice(3, 1);
    ZuCheck(a.length() == 3);
    ZuCheck((int)a[0] == 7);
    ZuCheck((int)a[1] == 0);
    ZuCheck((int)a[2] == 0);
  }

  // offset + length > N
  {
    ZuArray<I, 4> a;
    a << I(10) << I(11) << I(12);
    a.splice(2, 5);
    ZuCheck(a.length() == 2);
    ZuCheck((int)a[0] == 10);
    ZuCheck((int)a[1] == 11);
  }

  // offset + length > length_ without capacity adjust
  {
    ZuArray<I, 6> a;
    a << I(1) << I(2) << I(3);
    a.splice(1, 4);
    ZuCheck(a.length() == 1);
    ZuCheck((int)a[0] == 1);
  }

  // removed null path
  {
    ZuArray<I, 4> a;
    a << I(1) << I(2) << I(3);
    a.splice(1, 1);
    ZuCheck(a.length() == 2);
    ZuCheck((int)a[0] == 1);
    ZuCheck((int)a[1] == 3);
  }

  // MatchSplice removed sink
  {
    ZuArray<I, 4> a;
    a << I(5) << I(6) << I(7);
    IntSink<I> removed;
    a.splice(removed, 1, 1);
    ZuCheck(a.length() == 2);
    ZuCheck((int)a[0] == 5);
    ZuCheck((int)a[1] == 7);
    ZuCheck(removed.len == 1);
    ZuCheck(removed.vals[0] == 6);
  }

  // MatchAppend removed sink
  {
    ZuArray<I, 4> a;
    a << I(8) << I(9) << I(10);
    AppendSink<I, 4> removed;
    a.splice(removed, 1, 2);
    ZuCheck(a.length() == 1);
    ZuCheck((int)a[0] == 8);
    ZuCheck(removed.len == 2);
    ZuCheck((int)removed.vals[0] == 9);
    ZuCheck((int)removed.vals[1] == 10);
  }
}

void testSpliceVariantPaths()
{
  ZuTestScope(splice_variants);

  // splice(offset) overload
  {
    ZuArray<I, 4> a;
    a << I(1) << I(2) << I(3);
    a.splice(0);
    ZuCheck(!a.length());
  }

  // splice(removed, offset, length) overload with non-callable removed
  {
    ZuArray<I, 4> a;
    a << I(1) << I(2) << I(3);
    ZuSpan<ZuElem<I>> removed;
    a.splice(removed, 1, 1);
    ZuCheck(a.length() == 2);
    ZuCheck((int)a[0] == 1);
    ZuCheck((int)a[1] == 3);
    ZuCheck(removed.length() == 1);
    ZuCheck((int)removed[0] == 3);
  }

  // 5-arg splice case 1 (offset > length_) with replacement
  {
    ZuArray<I, 6> a;
    a << I(9);
    ZuSpan<ZuElem<I>> removed;
    unsigned replaced = 0;
    a.splice(removed, 3, 2, [&replaced](auto span) {
      replaced = span.length();
      ZuNew<I>(ZuAddr(span[0]), 7);
      ZuNew<I>(ZuAddr(span[1]), 8);
      return span.length();
    }, 2);
    ZuCheck(!removed.length());
    ZuCheck(replaced == 2);
    ZuCheck(a.length() == 5);
    ZuCheck((int)a[0] == 9);
    ZuCheck((int)a[1] == 0);
    ZuCheck((int)a[2] == 0);
    ZuCheck((int)a[3] == 7);
    ZuCheck((int)a[4] == 8);
  }

  // 5-arg splice case 1 clamps offset > N to N, so reserved replacement is 0
  {
    ZuArray<I, 3> a;
    a << I(9);
    int removedLen = -1;
    bool replaced = false;
    a.splice([&removedLen](auto span) {
      removedLen = span.length();
    }, 9, 1, [&replaced](auto span) {
      replaced = true;
      return span.length();
    }, 2);
    ZuCheck(removedLen == 0);
    ZuCheck(!replaced);
    ZuCheck(a.length() == 3);
    ZuCheck((int)a[0] == 9);
    ZuCheck((int)a[1] == 0);
    ZuCheck((int)a[2] == 0);
  }

  // rlength clamped to N - offset
  {
    ZuArray<I, 5> a;
    a << I(1) << I(2);
    IntSink<I> removed;
    unsigned replaced = 0;
    a.splice(removed, 1, 1, [&replaced](auto span) {
      replaced = span.length();
      ZuNew<I>(ZuAddr(span[0]), 9);
      ZuNew<I>(ZuAddr(span[1]), 8);
      ZuNew<I>(ZuAddr(span[2]), 7);
      ZuNew<I>(ZuAddr(span[3]), 6);
      return span.length();
    }, 10);
    ZuCheck(removed.len == 1);
    ZuCheck(removed.vals[0] == 2);
    ZuCheck(replaced == 4);
    ZuCheck(a.length() == 5);
    ZuCheck((int)a[0] == 1);
    ZuCheck((int)a[1] == 9);
    ZuCheck((int)a[2] == 8);
    ZuCheck((int)a[3] == 7);
    ZuCheck((int)a[4] == 6);
  }

  // replace() returns < rlength, tail shifts back down
  {
    ZuArray<I, 7> a;
    a << I(1) << I(2) << I(3) << I(4) << I(5);
    IntSink<I> removed;
    a.splice(removed, 1, 1, [](auto span) {
      ZuNew<I>(ZuAddr(span[0]), 20);
      ZuNew<I>(ZuAddr(span[1]), 21);
      ZuNew<I>(ZuAddr(span[2]), 22);
      return 2U;
    }, 3);
    ZuCheck(removed.len == 1);
    ZuCheck(removed.vals[0] == 2);
    ZuCheck(a.length() == 6);
    ZuCheck((int)a[0] == 1);
    ZuCheck((int)a[1] == 20);
    ZuCheck((int)a[2] == 21);
    ZuCheck((int)a[3] == 3);
    ZuCheck((int)a[4] == 4);
    ZuCheck((int)a[5] == 5);
  }

  // tail truncation path when reserved replacement would overflow capacity
  {
    ZuArray<I, 6> a;
    a << I(1) << I(2) << I(3) << I(4) << I(5) << I(6);
    IntSink<I> removed;
    a.splice(removed, 1, 3, [](auto span) {
      ZuNew<I>(ZuAddr(span[0]), 20);
      ZuNew<I>(ZuAddr(span[1]), 21);
      ZuNew<I>(ZuAddr(span[2]), 22);
      ZuNew<I>(ZuAddr(span[3]), 23);
      return 2U;
    }, 4);
    ZuCheck(removed.len == 3);
    ZuCheck(removed.vals[0] == 2);
    ZuCheck(removed.vals[1] == 3);
    ZuCheck(removed.vals[2] == 4);
    ZuCheck(a.length() == 5);
    ZuCheck((int)a[0] == 1);
    ZuCheck((int)a[1] == 20);
    ZuCheck((int)a[2] == 21);
    ZuCheck((int)a[3] == 5);
    ZuCheck((int)a[4] == 23);
  }
}

void testStringInitializers()
{
  ZuTestScope(string_initializers);

  ZuArray b{ "foo", "bar", "baz_" };
  ZuCheck(b[0] == "foo");
  ZuCheck(b[1] == "bar");
  ZuCheck(b[2] == "baz_");
  ZuCheck((ZuIsSame<ZuDecay<decltype(b[0])>, ZuCSpan>{}));
  ZuArray c{{1}, {1,2}, {1,2,3}};
  ZuCheck((ZuIsSame<ZuDecay<decltype(c[0])>, ZuSpan<const int>>{}));

  auto fn = []<auto &ZuTest_scope>(ZuSpan<ZuCSpan> b) {
    ZuCheck(b[0] == "foo");
    ZuCheck(b[1] == "bar");
    ZuCheck(b[2] == "baz_");
  };

  fn.operator ()<ZuTest_scope>(b);
}

void testFindStarts()
{
  ZuTestScope(find_starts);

  ZuArray<int, 6> a;
  a << 10 << 20 << 30 << 40;
  ZuArray<int, 3> prefix;
  prefix << 10 << 20;
  ZuArray<int, 3> mismatch;
  mismatch << 10 << 30;

  ZuCheck(a.find([](int v) { return v == 30; }) == 2);
  ZuCheck(a.find([](int v) { return v == 99; }) < 0);
  ZuCheck(a.match(prefix));
  ZuCheck(!a.match(mismatch));

  constexpr ZuString s{"hello"};
  ZuCheck(s.find([](char c) { return c == 'l'; }) == 2);
  ZuCheck(s.find([](char c) { return c == 'z'; }) < 0);
  ZuCheck(s.match("he"));
  ZuCheck(!s.match("ha"));

  ZuCSpan cs{"hello world"};
  ZuCheck(cs.find("lo") == 3);
  ZuCheck(cs.find("world") == 6);
  ZuCheck(cs.find("x") < 0);
  ZuCheck(cs.find("") == 0);
  ZuCheck(cs.find<"lo">() == 3);
  ZuCheck(cs.find<"world">() == 6);
  ZuCheck(cs.find<"x">() < 0);
  ZuCheck(cs.find<"">() == 0);
  ZuCheck(ZuCSpan{"ababc"}.find<"abc">() == 2);
  ZuCheck(ZuCSpan{"aaaaab"}.find<"aaab">() == 2);

  const uint8_t bytes[] = { 'a', 'b', 'c', 'd', 'e' };
  ZuBSpan bs{bytes, sizeof(bytes)};
  ZuCheck(bs.find("bcd") == 1);
  ZuCheck(bs.find("xyz") < 0);
  ZuCheck(bs.find<"bcd">() == 1);
  ZuCheck(bs.find<"xyz">() < 0);
  ZuCheck(bs.match("abc"));
  ZuCheck(!bs.match("abd"));
  ZuCheck(bs.match<"abc">());
  ZuCheck(!bs.match<"abd">());
  ZuCheck(bs.match<"">());

  const signed char signedNeedle[] = { 'c', 'd' };
  ZuCheck(bs.find(ZuSpan<const signed char>{signedNeedle, 2}) == 2);
}

int main()
{
  ZuTestMain();

  ZuTestCall(testSpliceBasicPaths);
  ZuTestCall(testSpliceRuntimePaths);
  ZuTestCall(testSpliceVariantPaths);
  ZuTestCall(testStringInitializers);
  ZuTestCall(testFindStarts);

  return 0;
}
