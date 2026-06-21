//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdlib.h>
#include <time.h>

#include <iostream>
#include <tuple>
#include <utility>
#include <array>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuHash.hh>
#include <zlib/ZuCmp.hh>
#include <zlib/ZuBox.hh>
#include <zlib/ZuTuple.hh>
#include <zlib/ZuUnion.hh>
#include <zlib/ZuArray.hh>
#include <zlib/ZuArray.hh>
#include <zlib/ZuSort.hh>
#include <zlib/ZuSearch.hh>
#include <zlib/ZuObject.hh>
#include <zlib/ZuRef.hh>
#include <zlib/ZuID.hh>
#include <zlib/ZuDemangle.hh>

using namespace ZuTestUtil;

template <typename T>
void test() {
  ZuTestScope(test);
  ZuCHECK(ZuCmp<T>::cmp(1, 0) > 0);
  ZuCHECK(ZuCmp<T>::cmp(0, 1) < 0);
  ZuCHECK(!ZuCmp<T>::cmp(0, 0));
  ZuCHECK(!ZuCmp<T>::cmp(1, 1));
  ZuCHECK(ZuCmp<T>::null(ZuCmp<T>::null()));
  ZuCHECK(!ZuCmp<T>::null(T(1)));
}

#define TEST_(T, name) ZuTestCall_("test<" name ">", test<T>)
#define TEST(T) TEST_(T, ZuPP_Eval(ZuPP_Defer(ZuPP_Q)(T)))

struct S {
  S() { m_data[0] = 0; }
  S(const S &s) { strcpy(m_data, s.m_data); }
  S &operator =(const S &s)
    { if (this != &s) strcpy(m_data, s.m_data); return *this; }
  S(const char *s) { strcpy(m_data, s); }
  S &operator =(const char *s) { strcpy(m_data, s); return *this; }
  operator char *() { return m_data; }
  operator const char *() const { return m_data; }
  friend inline bool operator ==(const S &l, const S &r) {
    return !strcmp(l.m_data, r.m_data);
  }
  friend inline int operator <=>(const S &l, const S &r) {
    return strcmp(l.m_data, r.m_data);
  }
  bool operator !() const { return !m_data[0]; }
  char m_data[32];
};

template <typename T1, typename T2>
void checkNull() {
  ZuTestScope(checkNull);
  ZuBox<T1> t;
  ZuBox<T2> u = t;
  ZuBox<T2> v(t);
  ZuBox<T2> w;
  w = t;
  ZuCHECK(!*u && !*v && !*w);
}

namespace T1 {
  using I = ZuBox<int>;
  using R = const ZuBox<int> &;
  struct _ {
    ZuDeclTuple(V, (I, id), (I, age), (I, height));
    ZuDeclTuple(T, (R, id), (R, age), (R, height));
  };
  using V = _::V;
  using T = _::T;
}

ZuAssert((ZuIsSame<uint32_t, decltype(ZuHash<T1::V>::hash(ZuDeclVal<const T1::V &>()))>{}));
ZuAssert((ZuIsSame<uint32_t, decltype(ZuHash<T1::T>::hash(ZuDeclVal<const T1::V &>()))>{}));

namespace T2 {
  using I = int;
  using D = double;
  using S = const char *;
  using P = ZuTuple<int, int>;
  using CP = int *;
  ZuDeclUnion(V, (I, id), (D, income), (S, name), (P, dependents), (CP, foo));
}

namespace T3 {
  using I = ZuBox<int>;
  ZuDeclTuple(V, (I, id), (I, age), (I, height));
  using T = ZuTuple<ZuArray<V, 3>, ZuArray<int, 3>>;
}

template <unsigned N> struct SortTest {
  template <typename A, typename S> static void test_(A &a, S &s) {
    ZuSort<N>(&a[0], a.length());
    for (unsigned i = 0, n = a.length(); i < n; i++)
      s << (i ? " " : "") << a[i];
  }
  static void test() {
    ZuTestScope(test);
    {
      ZuArray<int, 1> foo{};
      ZuCArray<80> s;
      test_(foo, s);
      ZuCHECK(s == "");
      ZuCHECK(ZuSearch(&foo[0], 0, 0) == 0);
      ZuCHECK(ZuInterSearch(&foo[0], 0, 0) == 0);
    }
    {
      ZuArray<int, 1> foo{1};
      ZuCArray<80> s;
      test_(foo, s);
      ZuCHECK(s == "1");
      ZuCHECK(ZuSearch(&foo[0], 1, 0) == 0);
      ZuCHECK(ZuInterSearch(&foo[0], 1, 0) == 0);
      ZuCHECK(ZuSearch(&foo[0], 1, 1) == 1);
      ZuCHECK(ZuInterSearch(&foo[0], 1, 1) == 1);
      ZuCHECK(ZuSearch<false>(&foo[0], 1, 1) == 0);
      ZuCHECK(ZuInterSearch<false>(&foo[0], 1, 1) == 0);
    }
    {
      ZuArray<int, 2> foo{0, 1};
      ZuCArray<80> s;
      test_(foo, s);
      ZuCHECK(s == "0 1");
      ZuCHECK(ZuSearch(&foo[0], 2, 0) == 1);
      ZuCHECK(ZuInterSearch(&foo[0], 2, 0) == 1);
      ZuCHECK(ZuSearch(&foo[0], 2, 1) == 3);
      ZuCHECK(ZuInterSearch(&foo[0], 2, 1) == 3);
      ZuCHECK(ZuSearch<false>(&foo[0], 2, 0) == 0);
      ZuCHECK(ZuInterSearch<false>(&foo[0], 2, 0) == 0);
      ZuCHECK(ZuSearch<false>(&foo[0], 2, 1) == 2);
      ZuCHECK(ZuInterSearch<false>(&foo[0], 2, 1) == 2);
    }
    {
      ZuArray<int, 2> foo{1, 0};
      ZuCArray<80> s;
      test_(foo, s);
      ZuCHECK(s == "0 1");
    }
    {
      ZuArray<int, 3> foo{3, 1, 2};
      ZuCArray<80> s;
      test_(foo, s);
      ZuCHECK(s == "1 2 3");
      ZuCHECK(ZuSearch(&foo[0], 3, 0) == 0);
      ZuCHECK(ZuInterSearch(&foo[0], 3, 0) == 0);
      ZuCHECK(ZuSearch(&foo[0], 3, 1) == 1);
      ZuCHECK(ZuInterSearch(&foo[0], 3, 1) == 1);
      ZuCHECK(ZuSearch(&foo[0], 3, 2) == 3);
      ZuCHECK(ZuInterSearch(&foo[0], 3, 2) == 3);
      ZuCHECK(ZuSearch<false>(&foo[0], 3, 2) == 2);
      ZuCHECK(ZuInterSearch<false>(&foo[0], 3, 2) == 2);
      ZuCHECK(ZuSearch<false>(&foo[0], 3, 3) == 4);
      ZuCHECK(ZuInterSearch<false>(&foo[0], 3, 3) == 4);
    }
    {
      ZuArray<int, 4> foo{4, 1, 3, 0};
      ZuCArray<80> s;
      test_(foo, s);
      ZuCHECK(s == "0 1 3 4");
      ZuCHECK(ZuSearch(&foo[0], 4, 0) == 1);
      ZuCHECK(ZuInterSearch(&foo[0], 4, 0) == 1);
      ZuCHECK(ZuSearch(&foo[0], 4, 1) == 3);
      ZuCHECK(ZuInterSearch(&foo[0], 4, 1) == 3);
      ZuCHECK(ZuSearch(&foo[0], 4, 2) == 4);
      ZuCHECK(ZuInterSearch(&foo[0], 4, 2) == 4);
      ZuCHECK(ZuSearch<false>(&foo[0], 4, 3) == 4);
      ZuCHECK(ZuInterSearch<false>(&foo[0], 4, 3) == 4);
      ZuCHECK(ZuSearch<false>(&foo[0], 4, 4) == 6);
      ZuCHECK(ZuInterSearch<false>(&foo[0], 4, 4) == 6);
    }
    {
      ZuArray<int, 13> foo{3, 1, 2, 9, 5, 3, 5, 1, 10, 4, 0, 7, 6};
      ZuCArray<80> s;
      test_(foo, s);
      ZuCHECK(s == "0 1 1 2 3 3 4 5 5 6 7 9 10");
      ZuCHECK(ZuSearch(&foo[0], 13, 0) == 1);
      ZuCHECK(ZuInterSearch(&foo[0], 13, 0) == 1);
      ZuCHECK(ZuSearch(&foo[0], 13, 2) == 7);
      ZuCHECK(ZuInterSearch(&foo[0], 13, 2) == 7);
      ZuCHECK(ZuSearch<false>(&foo[0], 13, 5) == 14);
      ZuCHECK(ZuInterSearch<false>(&foo[0], 13, 5) == 14);
      ZuCHECK(ZuSearch<false>(&foo[0], 13, 10) == 24);
      ZuCHECK(ZuInterSearch<false>(&foo[0], 13, 10) == 24);
    }
  }
};

template <unsigned k_> struct K { enum { k = k_ }; };

struct M {
  M() = default;
  M(const M &) = delete;
  M &operator =(const M &) = delete;
  M(M &&) = default;
  M &operator =(M &&) = default;
  ~M() = default;
  int cmp(const M &) const { return 0; }
  bool equals(const M &) const { return true; }
  bool operator !() const { return true; }
};

inline bool operator ==(const M &, const M &) { return true; }

struct O : public ZuObject { };

#include <zlib/ZuLambdaTraits.hh>

template <typename L>
void foo(L l) {
  ZuTestScope(foo);
  ZuCHECK(ZuIsStatelessLambda<L>{});
}

struct RRef { };
struct Ref { };
struct CRef { };
struct Foo {
  CRef bar() const & { return {}; }
  Ref bar() & { return {}; }
  RRef bar() && { return {}; }
};
template <typename T>
static decltype(auto) bar(T &&v) { return ZuFwd<T>(v).bar(); }

template <typename, typename T>
struct Narrow : public T { using T::T; using T::operator =; };

int main(int argc, char **argv)
{
  parse(argc, argv);

  ZuTestMain();

  {
    struct X { };
    ZuCHECK(ZuTraits<X>::IsComposite);
    ZuCHECK(!ZuTraits<int>::IsComposite);
    enum { Foo = 42 };
    enum _ { Bar = 42 };
    ZuCHECK(ZuTraits<decltype(Foo)>::IsEnum);
    ZuCHECK(ZuTraits<_>::IsEnum);
    ZuCHECK(!ZuTraits<int>::IsEnum);
    ZuCHECK(!ZuTraits<X>::IsEnum);
  }

  TEST(bool);
  ZuCHECK(ZuCmp<char>::cmp(1, 0) > 0);
  ZuCHECK(ZuCmp<char>::cmp(0, 1) < 0);
  ZuCHECK(!ZuCmp<char>::cmp(0, 0));
  ZuCHECK(!ZuCmp<char>::cmp(1, 1));
  ZuCHECK(ZuCmp<char>::null(ZuCmp<char>::null()));
  ZuCHECK(!ZuCmp<char>::null((char)0x80));
  ZuCHECK(!ZuCmp<char>::null((char)1));
  TEST(signed char);
  TEST(unsigned char);
  TEST(short);
  TEST(unsigned short);
  TEST(int);
  TEST(unsigned int);
  TEST(long);
  TEST(unsigned long);
  TEST(long long);
  TEST(unsigned long long);
  TEST(float);
  TEST(double);

  {
    using I = ZuBox<int>;
    using R = const ZuBox<int> &;
    using V = ZuTuple<I, I>;
    using T = ZuTuple<R, R>;

    V j(1, 2);
    V i = j;
    ZuBox<int> p = 1;
    ZuBox<int> q = 2;
    ZuCHECK(ZuCmp<V>::cmp(i, T(p, q)) == 0);
    q = 3;
    ZuCHECK(ZuCmp<V>::cmp(i, T(p, q)) < 0);
    q = 1;
    ZuCHECK(ZuCmp<V>::cmp(i, T(p, q)) > 0);
    p = 1, q = 2;
    ZuCHECK(ZuCmp<V>::cmp(i, ZuFwdTuple(p, q)) == 0);
    q = 3;
    ZuCHECK(ZuCmp<V>::cmp(i, ZuFwdTuple(p, q)) < 0);
    q = 1;
    ZuCHECK(ZuCmp<V>::cmp(i, ZuFwdTuple(p, q)) > 0);
  }

  {
    ZuTuple<int, int, int, int> s(1, 2, 3, 4);
    ZuTuple<int, int, int, int> t;
    t = s;
    printf("%d %d %d %d\n", (int)s.p<0>(), (int)s.p<1>(), (int)s.p<2>(), (int)s.p<3>());
    printf("%d %d %d %d\n", (int)t.p<0>(), (int)t.p<1>(), (int)t.p<2>(), (int)t.p<3>());
  }

  {
    using namespace T1;
    V j(1, 2, 3);
    V i;
    i = j;
    ZuCHECK(i.id() == 1);
    ZuCHECK(i.age() == 2);
    ZuCHECK(i.height() == 3);
    ZuBox<int> p = 1;
    ZuBox<int> q = 2;
    ZuBox<int> r = 3;
    ZuCHECK(ZuCmp<V>::cmp(i, T(p, q, r)) == 0);
    q = 3;
    ZuCHECK(ZuCmp<V>::cmp(i, T(p, q, r)) < 0);
    q = r = 2;
    ZuCHECK(ZuCmp<V>::cmp(i, T(p, q, r)) > 0);
    q = 2, r = 3;
    ZuCHECK(ZuCmp<V>::cmp(i, ZuFwdTuple(p, q, r)) == 0);
    q = 3;
    ZuCHECK(ZuCmp<V>::cmp(i, ZuFwdTuple(p, q, r)) < 0);
    q = r = 2;
    ZuCHECK(ZuCmp<V>::cmp(i, ZuFwdTuple(p, q, r)) > 0);
  }

  {
    using namespace T2;
    int c = 42;

    {
      V j;
      j.name("3");
      V i;
      i = j;
      ZuCHECK(i.name() == j.name());
      ZuCHECK(i == j);
      ZuCHECK(ZuCmp<V>::cmp(i, j) == 0);
      j.name("4");
      ZuCHECK(ZuCmp<V>::cmp(i, j) < 0);
      i.income(200.0);
      ZuCHECK(ZuCmp<V>::cmp(i, j) < 0);
      j.id(42);
      ZuCHECK(ZuCmp<V>::cmp(i, j) > 0);
      i.dependents(ZuFwdTuple(1, 2));
      j = i;
      ZuCHECK(i == j);
      ZuCHECK(ZuCmp<V>::cmp(i, j) == 0);
      ZuCHECK(i.dependents() == j.dependents());
      j.dependents(ZuFwdTuple(1, 3));
      ZuCHECK(ZuCmp<V>::cmp(i, j) < 0);
      i.dependents(ZuFwdTuple(1, 4));
      ZuCHECK(ZuCmp<V>::cmp(i, j) > 0);
      i.foo(&c);
      ZuCHECK(*(i.foo()) == 42);
      ++*(i.foo());
    }
    ZuCHECK(c == 43);
  }

  {
    ZuTuple<char, char, char, char> t;
    char *p = (char *)&t;
    printf("%d %d %d %d\n",
	   (int)(&t.p<0>() - p), (int)(&t.p<1>() - p),
	   (int)(&t.p<2>() - p), (int)(&t.p<3>() - p));
  }

  {
    S s1("string1");
    S s2("string2");
    S s3("string3");
    ZuTuple<int, const S &, const S &> t1(42, s1, s2);
    ZuTuple<int, const S &, const S &> t2(42, s1, s3);
    ZuCHECK((ZuCmp<ZuTuple<int, const S &, const S &> >::cmp(t1, t2) < 0));
    ZuCHECK((ZuCmp<ZuTuple<int, const S &, const S &> >::cmp(t1, t1) == 0));
    ZuCHECK((ZuCmp<ZuTuple<int, const S &, const S &> >::cmp(t2, t1) > 0));
    ZuTuple<int, const S &, const S &> t3 = ZuFwdTuple(42, s3, s3);
    ZuCHECK((ZuCmp<ZuTuple<int, const S &, const S &> >::cmp(t1, t3) < 0));
    S s4{"hello"};
    S s5{"world"};
    log("t1=", t1);
    log("t2=", ZuFwdTuple(42, s4, s5));
    ZuCHECK((ZuCmp<ZuTuple<int, const S &, const S &> >::cmp(t1,
	    ZuFwdTuple(42, s4, s5)) > 0));
    // ZuTuple<int, const S &, const S &> t4(42, "string1", "string2");
    // ZuCHECK((ZuCmp<ZuTuple<int, const S &, const S &> >::cmp(t4, t2) < 0));
  }

  {
    using namespace T3;
    T t, s;
    t.p<0>().length(1);
    t.p<0>()[0] = ZuFwdTuple(1, 2, 3);
    t.p<0>() += ZuFwdTuple(1, 2, 3);
    t.p<0>() << ZuFwdTuple(1, 2, 3);
    t.p<1>().length(3);
    t.p<1>()[0] = 42;
    t.p<1>()[2] = 42;
    s = t;
    ZuCHECK((s.p<0>()[1] == s.p<0>()[0]));
    // below deliberately triggers use of uninitialized memory
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wuninitialized"
#endif
    log(int(t.p<1>()[1]));
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
  }

  {
    ZuCArray<10> s = "hello world";
    ZuCHECK(s == "hello worl");
    s = ZuArray<char, 10>("hello world");
    ZuCHECK(s == "hello worl");
    s = 'h';
    ZuCHECK(s == "h");
    s << ZuArray<char, 2>("el");
    s += "lo ";
    s << ZuCArray<6>("world");
    ZuCHECK(s == "hello worl");
  }

  {
    ZuTestCall((checkNull<int16_t, uint32_t>));
    ZuTestCall((checkNull<uint32_t, int16_t>));
    ZuTestCall((checkNull<int16_t, int32_t>));
    ZuTestCall((checkNull<int32_t, int16_t>));
    ZuTestCall((checkNull<int16_t, uint64_t>));
    ZuTestCall((checkNull<int64_t, uint16_t>));
    ZuTestCall((checkNull<double, uint16_t>));
    ZuTestCall((checkNull<int32_t, double>));
  }

  {
    ZuTestCall((SortTest<0>::test));
    ZuTestCall((SortTest<1>::test));
    ZuTestCall((SortTest<2>::test));
    ZuTestCall((SortTest<8>::test));
    ZuTestCall((SortTest<20>::test));
  }

  {
    using U = ZuUnion<int, float, double>;
    char c_[sizeof(U)];
    *reinterpret_cast<double *>(c_) = 42.0;
    auto c = reinterpret_cast<U *>(c_);
    c->type_(U::Index<double>{});
    ZuCHECK(c->p<double>() == 42.0);
    auto d = get<double>(*c);
    ZuCHECK(d == 42.0);
    *c = 42.0;
    d = get<double>(*c);
    ZuCHECK(d == 42.0);
    c->~U();
    new (c) U{42.0};
    d = get<double>(*c);
    ZuCHECK(d == 42.0);
  }

  {
    using U = ZuUnion<void, int>;
    U u;
    log(u.type());
  }

  // structured binding smoke tests
  {
    ZuArray<int, 3> foo = { 1, 2, 3 };
    auto [a, b, c] = foo;
    ZuCHECK(a == 1 && b == 2 && c == 3);
  }

  {
    ZuTuple<uint64_t, uint32_t> foo = { 1U, 2U };
    auto [a, b] = foo;
    ZuCHECK(a == 1 && b == 2);
  }

  {
    ZuTuple<uint64_t, uint32_t, uint16_t> foo =
      { 1U, 2U, static_cast<uint16_t>(3U) };
    auto [a, b, c] = foo;
    ZuCHECK(a == 1 && b == 2 && c == 3);
  }

  {
    ZuUnsigned<1> i;
    ZuUnsigned<i> j;
    ZuCHECK(K<j>::k == 1);
  }

  {
    struct A {
      A() : i{0} { }
      A(int i_) : i{i_} { }
      A(const A &) = default;
      A &operator =(const A &) = default;
      A(A &&) = default;
      A &operator =(A &&) = default;
      ~A() = default;
      bool operator ==(const A &r) { return i == r.i; }
      int cmp(const A &r) const { return ZuCmp<int>::cmp(i, r.i); }
      int operator <=>(const A &r) const { return cmp(r); }
      bool operator !() const { return !i; }
      int i;
    };
    struct B : public A {
      using A::A;
      using A::operator =;
      using A::cmp;
    };
    B a, b{1}, c{42};
    ZuCHECK(!a);
    ZuCHECK(!ZuCmp<A>::cmp(a, a));
    ZuCHECK(!ZuCmp<A>::cmp(c, c));
    ZuCHECK(a < b);
    ZuCHECK(ZuCmp<A>::cmp(a, b) < 0);
    ZuCHECK(ZuCmp<A>::cmp(c, b) > 0);
  }

  {
    ZuCHECK(M{} == ZuCmp<M>::null());
  }

  {
    ZuRef<O> o = new O{};
    ZuCHECK(ZuObjectTraits<O>::IsObject);
  }

  {
    ZuUnion<void, bool> a, b = true;
    ZuCHECK(ZuCmp<unsigned>::cmp(a.type(), 0) == 0);
    ZuCHECK(ZuCmp<unsigned>::cmp(b.type(), 1) == 0);
    ZuCHECK(ZuCmp<bool>::cmp(b.p<bool>(), true) == 0);
  }

  {
    ZuCHECK(bool(ZuHash_Can_hash<T1::V>{}));
  }

  ZuTestCall(foo, []{});

  {
    struct A {
      A() = default;
      A(A &&) = default;
      A &operator =(A &&) = default;
      ~A() = default;
      A(const A &) = delete;
      A &operator =(const A &) = delete;
    };
    using U = ZuUnion<void, A>;
    struct B {
      static A foo(U u) { return ZuMv(u).p<A>(); }
    };
    U u{A{}};
    try {
      throw ZuMv(u).p<A>();
    } catch (A &a) {
      ZuCHECK(true);
    }
    A b = B::foo(ZuMv(u));
    try {
      throw ZuMv(b);
    } catch (A &a) {
      ZuCHECK(true);
    }
  }
  {
    ZuID id = "foobar";
    ZuCSpan s(id);
    ZuCHECK(s == "foobar");
  }
  {
    int x = 0;
    auto m = [x]() mutable { ++x; return x; };
    auto c = [&x]() { return x; };
    using M = ZuDecay<decltype(m)>;
    using C = ZuDecay<decltype(c)>;
    ZuCHECK(ZuIsMutableLambda<M>{});
    ZuCHECK(ZuIsMutableLambda<M &>{});
    ZuCHECK(ZuIsMutableLambda<const M &>{});
    ZuCHECK(!ZuIsMutableLambda<C>{});
    ZuCHECK(!ZuIsMutableLambda<C &>{});
    ZuCHECK(!ZuIsMutableLambda<const C &>{});
  }
  {
    Foo foo;
    const Foo &cfoo = foo;
    ZuCHECK((ZuIsSame<CRef, decltype(bar(cfoo))>{}));
    ZuCHECK((ZuIsSame<Ref, decltype(bar(foo))>{}));
    ZuCHECK((ZuIsSame<RRef, decltype(bar(ZuMv(foo)))>{}));
  }
  {
    struct _ { };
    using N = Narrow<_, ZuTuple<int>>;
    N n{42}, o(43);
    o = n = 44;
    ZuCHECK(o == n);
  }
  {
    std::tuple<int, int> p{ 1, 2 };
    std::pair<int, int> q = { 3, 4 };
    std::array<int, 2> a = { -3, -4 };
    ZuTuple<int, int> r{ 5, 6 };
    ZuTuple<int, int> s = { 7, 8 };
    ZuCHECK(r.p<0>() == 5);
    ZuCHECK(s.p<0>() == 7);
    r = a;
    s = ZuTuple<int, int>{a};
    ZuCHECK(r.p<0>() == -3);
    ZuCHECK(s.p<1>() == -4);
    ZuTuple<int, int> t{p};
    ZuTuple<int, int> u = q;
    ZuTuple<int, int> v; v = q;
    ZuCHECK(t.p<1>() == 2);
    ZuCHECK(u.p<1>() == 4);
    ZuCHECK(v.p<1>() == 4);
    ZuTuple<int, int> w = { 42 };
    ZuCHECK(w.p<0>() == 42);
    ZuCHECK(w.p<1>() == 0);
  }

  log("sizeof(ZuUnion<void, uintptr_t>)=",
    sizeof(ZuUnion<void, uintptr_t>));
  log("sizeof(std::optional<uintptr_t>)=",
    sizeof(std::optional<uintptr_t>));

  {
    using A = ZuArray<ZuTuple<ZuCSpan, ZuCSpan>, 2>;
    A a{{ "foo", "bar" }, { "baz", "bah" }};
    ZuCHECK(a[0].p<0>() == "foo");
    ZuCHECK(a[0].p<1>() == "bar");
    ZuCHECK(a[1].p<0>() == "baz");
    ZuCHECK(a[1].p<1>() == "bah");
  }
  {
    enum { I = (ZuSpan<ZuTuple<int>>( { 42 } ))[0].p<0>() };
    ZuCHECK(I == 42);
  }
  {
    const auto x = ZuTuple{42, 43};
    ZuCHECK(x.p<0>() == 42);
  }

  {
    struct A { int i = 42; };

    A a;
    ZuCHECK((ZuIsSame<int &, decltype(ZuFwdLike<A &>(a.i))>{}));
    ZuCHECK((ZuIsSame<const int &, decltype(ZuFwdLike<const A &>(a.i))>{}));
    ZuCHECK((ZuIsSame<int &&, decltype(ZuFwdLike<A &&>(a.i))>{}));
  }

  {
    char buf[] = "foo bar baz";
    const char buf2[] = "foo bar baz";
    ZuCHECK((ZuCSpan::IsStrLiteral<decltype(buf)>{}));
    ZuCHECK((ZuCSpan::IsStrLiteral<decltype(buf2)>{}));
  }

  {
    ZuTuple<int, int> i{42, 42};
    ZuCHECK((i == decltype(i){42, 42}));
  }
}
