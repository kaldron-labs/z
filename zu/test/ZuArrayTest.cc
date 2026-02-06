//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuLib.hh>

#include <iostream>

#include <assert.h>
#include <stdlib.h>

#include <zlib/ZuTest.hh>
#include <zlib/ZuArray.hh>
#include <zlib/ZuVArray.hh>
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

struct IntSink {
  enum { Cap = 8 };
  int		vals[Cap] = {};
  unsigned	len = 0;

  constexpr IntSink &operator <<(int v) {
    if (len < Cap) vals[len++] = v;
    return *this;
  }
};

template <typename T, unsigned N>
struct AppendSink {
  T		vals[N] = {};
  unsigned	len = 0;

  void append(const T *data, unsigned length) {
    unsigned n = length;
    if (len + n > N) n = N - len;
    for (unsigned i = 0; i < n; i++) vals[len++] = data[i];
  }
};

struct TrSpan {
  static int dtor;
  int v;

  TrSpan(int v_ = 0) : v(v_) { }
  TrSpan(const TrSpan &) = delete;
  TrSpan &operator =(const TrSpan &) = delete;
  TrSpan(TrSpan &&o) noexcept : v(o.v) { o.v = -1; }
  TrSpan &operator =(TrSpan &&o) noexcept { v = o.v; o.v = -1; return *this; }
  ~TrSpan() { ++dtor; }
};
int TrSpan::dtor = 0;

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
  IntSink removed;
  a.splice(0, 2, removed);
  return
    a.length() == 2 &&
    a[0] == 3 &&
    a[1] == 4 &&
    removed.len == 2 &&
    removed.vals[0] == 1 &&
    removed.vals[1] == 2;
}

static_assert(spliceConstevalBasic());
static_assert(spliceConstevalRemoved());

template <typename A>
void testSplice(A &a, int offset, int length, int check1, int check2)
{
  ZuTestScope(splice);
  A a_;
  a.splice(offset, length, a_);
  ZuCheck((!a.length() && !check1) || (int)a[0] == check1);
  ZuCheckBlock(({
    if (check2 < 0)
      ZuCheck_(!a_.length());
    else
      ZuCheck_((!a.length() && !check2) || (int)a_[0] == check2);
  }));
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

  // offset >= N early return
  {
    ZuArray<I, 2> a;
    a << I(5);
    a.splice(2, 1);
    ZuCheck(a.length() == 1);
    ZuCheck((int)a[0] == 5);
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
    IntSink removed;
    a.splice(1, 1, removed);
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
    a.splice(1, 2, removed);
    ZuCheck(a.length() == 1);
    ZuCheck((int)a[0] == 8);
    ZuCheck(removed.len == 2);
    ZuCheck((int)removed.vals[0] == 9);
    ZuCheck((int)removed.vals[1] == 10);
  }
}

void testSpanSplice()
{
  ZuTestScope(splice_span);

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

template <typename U, typename = void>
struct IsIterable_ : public ZuFalse { };
template <typename U>
struct IsIterable_<U, decltype(
  ZuDeclVal<const U &>().end() - ZuDeclVal<const U &>().begin(), void())> :
    public ZuTrue { };
template <typename U, typename V>
struct IsIterable : public ZuBool<
    !ZuIsSame<U, V>{} &&
    !ZuTraits<U>::IsSpan &&
    bool(IsIterable_<ZuDecay<U>>{}) &&
    ZuIsConvertible<typename ZuTraits<U>::Elem, V>{}> { };

template <ZuArray S> struct A { enum { IsNull = 0 }; };
template <> struct A<ZuArray("")> { enum { IsNull = 1 }; };

ZuAssert((A<"">::IsNull));
ZuAssert((!A<"foo">::IsNull));

int main()
{
  ZuTestMain();

  {
    ZuArray<I, 1> a;
    a << I(42);
    ZuCheck((int)a[0] == 42);
    a << I(43);
    ZuCheck((int)a[0] == 42);
    ZuTestCall(testSplice, a, 0, 1, 0, 42);
    a << I(42);
    ZuTestCall(testSplice, a, 1, 1, 42, -1);
    ZuTestCall(testSplice, a, 0, 2, 0, 42);
  }
  {
    ZuArray<I, 2> a;
    a << I(42);
    ZuCheck((int)a[0] == 42);
    a << I(43);
    ZuCheck((int)a[1] == 43);
    ZuTestCall(testSplice, a, 0, 1, 43, 42);
    a << I(42);
    ZuTestCall(testSplice, a, 1, 1, 43, 42);
    ZuTestCall(testSplice, a, -1, 3, 0, 43);
  }
  {
    ZuArray<I, 3> a;
    a << I(42);
    a << I(43);
    a << I(44);
    a << I(45);
    ZuCheck((int)a[0] == 42);
    ZuCheck((int)a[2] == 44);
    ZuTestCall(testSplice, a, 0, 2, 44, 42);
    a << I(45);
    ZuTestCall(testSplice, a, 1, 1, 44, 45);
    ZuTestCall(testSplice, a, -2, 4, 0, 44);
  }
  ZuTestCall(testSpliceRuntimePaths);
  ZuTestCall(testSpanSplice);
  {
    ZuWArray<80> w;
    w << L"hello " << "world" << L'!' << ' ' << 42;
    ZuCArray<80> s = w;
    ZuCheck(s == "hello world! 42");
    s = {};
    s << L"hello " << "world" << L'!' << ' ' << 42;
    w = s;
    ZuCheck(w == L"hello world! 42");
  }

  {
    ZuVArray<ZuBSpan> a;
    ZuCheck(ZuTraits<decltype(a)>::IsArray);
    ZuCheck(!ZuTraits<decltype(a)>::IsSpan);
    ZuCheck(IsIterable_<decltype(a)>{});
    ZuCheck((IsIterable<decltype(a), ZuBSpan>()));
    ZuCheck((ZuIsConstructible<
	typename ZuTraits<decltype(a)>::Elem,
	ZuBSpan>()));
  }

  return 0;
}
