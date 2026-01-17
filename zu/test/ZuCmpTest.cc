//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuLib.hh>

#include <stdlib.h>
#include <time.h>

#include <iostream>
#include <tuple>
#include <utility>
#include <array>

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

inline void out(const char *s) {
  std::cout << s << '\n' << std::flush;
}

#define CHECK(x) ((x) ? out("OK  " #x) : out("NOK " #x))

#define TEST(t) \
  CHECK(ZuCmp<t>::cmp(1, 0) > 0), \
  CHECK(ZuCmp<t>::cmp(0, 1) < 0), \
  CHECK(!ZuCmp<t>::cmp(0, 0)), \
  CHECK(!ZuCmp<t>::cmp(1, 1)), \
  CHECK(ZuCmp<t>::null(ZuCmp<t>::null())), \
  CHECK(!ZuCmp<t>::null((t)1))

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

template <typename T1, typename T2> void checkNull() {
  ZuBox<T1> t;
  ZuBox<T2> u = t;
  ZuBox<T2> v(t);
  ZuBox<T2> w;
  w = t;
  CHECK(!*u && !*v && !*w);
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
    {
      ZuArray<int, 1> foo{};
      ZuCArray<80> s;
      test_(foo, s);
      CHECK(s == "");
      CHECK(ZuSearch(&foo[0], 0, 0) == 0);
      CHECK(ZuInterSearch(&foo[0], 0, 0) == 0);
    }
    {
      ZuArray<int, 1> foo{1};
      ZuCArray<80> s;
      test_(foo, s);
      CHECK(s == "1");
      CHECK(ZuSearch(&foo[0], 1, 0) == 0);
      CHECK(ZuInterSearch(&foo[0], 1, 0) == 0);
      CHECK(ZuSearch(&foo[0], 1, 1) == 1);
      CHECK(ZuInterSearch(&foo[0], 1, 1) == 1);
      CHECK(ZuSearch<false>(&foo[0], 1, 1) == 0);
      CHECK(ZuInterSearch<false>(&foo[0], 1, 1) == 0);
    }
    {
      ZuArray<int, 2> foo{0, 1};
      ZuCArray<80> s;
      test_(foo, s);
      CHECK(s == "0 1");
      CHECK(ZuSearch(&foo[0], 2, 0) == 1);
      CHECK(ZuInterSearch(&foo[0], 2, 0) == 1);
      CHECK(ZuSearch(&foo[0], 2, 1) == 3);
      CHECK(ZuInterSearch(&foo[0], 2, 1) == 3);
      CHECK(ZuSearch<false>(&foo[0], 2, 0) == 0);
      CHECK(ZuInterSearch<false>(&foo[0], 2, 0) == 0);
      CHECK(ZuSearch<false>(&foo[0], 2, 1) == 2);
      CHECK(ZuInterSearch<false>(&foo[0], 2, 1) == 2);
    }
    {
      ZuArray<int, 2> foo{1, 0};
      ZuCArray<80> s;
      test_(foo, s);
      CHECK(s == "0 1");
    }
    {
      ZuArray<int, 3> foo{3, 1, 2};
      ZuCArray<80> s;
      test_(foo, s);
      CHECK(s == "1 2 3");
      CHECK(ZuSearch(&foo[0], 3, 0) == 0);
      CHECK(ZuInterSearch(&foo[0], 3, 0) == 0);
      CHECK(ZuSearch(&foo[0], 3, 1) == 1);
      CHECK(ZuInterSearch(&foo[0], 3, 1) == 1);
      CHECK(ZuSearch(&foo[0], 3, 2) == 3);
      CHECK(ZuInterSearch(&foo[0], 3, 2) == 3);
      CHECK(ZuSearch<false>(&foo[0], 3, 2) == 2);
      CHECK(ZuInterSearch<false>(&foo[0], 3, 2) == 2);
      CHECK(ZuSearch<false>(&foo[0], 3, 3) == 4);
      CHECK(ZuInterSearch<false>(&foo[0], 3, 3) == 4);
    }
    {
      ZuArray<int, 4> foo{4, 1, 3, 0};
      ZuCArray<80> s;
      test_(foo, s);
      CHECK(s == "0 1 3 4");
      CHECK(ZuSearch(&foo[0], 4, 0) == 1);
      CHECK(ZuInterSearch(&foo[0], 4, 0) == 1);
      CHECK(ZuSearch(&foo[0], 4, 1) == 3);
      CHECK(ZuInterSearch(&foo[0], 4, 1) == 3);
      CHECK(ZuSearch(&foo[0], 4, 2) == 4);
      CHECK(ZuInterSearch(&foo[0], 4, 2) == 4);
      CHECK(ZuSearch<false>(&foo[0], 4, 3) == 4);
      CHECK(ZuInterSearch<false>(&foo[0], 4, 3) == 4);
      CHECK(ZuSearch<false>(&foo[0], 4, 4) == 6);
      CHECK(ZuInterSearch<false>(&foo[0], 4, 4) == 6);
    }
    {
      ZuArray<int, 13> foo{3, 1, 2, 9, 5, 3, 5, 1, 10, 4, 0, 7, 6};
      ZuCArray<80> s;
      test_(foo, s);
      CHECK(s == "0 1 1 2 3 3 4 5 5 6 7 9 10");
      CHECK(ZuSearch(&foo[0], 13, 0) == 1);
      CHECK(ZuInterSearch(&foo[0], 13, 0) == 1);
      CHECK(ZuSearch(&foo[0], 13, 2) == 7);
      CHECK(ZuInterSearch(&foo[0], 13, 2) == 7);
      CHECK(ZuSearch<false>(&foo[0], 13, 5) == 14);
      CHECK(ZuInterSearch<false>(&foo[0], 13, 5) == 14);
      CHECK(ZuSearch<false>(&foo[0], 13, 10) == 24);
      CHECK(ZuInterSearch<false>(&foo[0], 13, 10) == 24);
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
  CHECK(ZuIsStatelessLambda<L>{});
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

int main()
{
  {
    struct X { };
    CHECK(ZuTraits<X>::IsComposite);
    CHECK(!ZuTraits<int>::IsComposite);
    enum { Foo = 42 };
    enum _ { Bar = 42 };
    CHECK(ZuTraits<decltype(Foo)>::IsEnum);
    CHECK(ZuTraits<_>::IsEnum);
    CHECK(!ZuTraits<int>::IsEnum);
    CHECK(!ZuTraits<X>::IsEnum);
  }

  TEST(bool);
  CHECK(ZuCmp<char>::cmp(1, 0) > 0),
  CHECK(ZuCmp<char>::cmp(0, 1) < 0),
  CHECK(!ZuCmp<char>::cmp(0, 0)),
  CHECK(!ZuCmp<char>::cmp(1, 1)),
  CHECK(ZuCmp<char>::null(ZuCmp<char>::null())),
  CHECK(!ZuCmp<char>::null((char)0x80)),
  CHECK(!ZuCmp<char>::null((char)1));
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
    CHECK(ZuCmp<V>::cmp(i, T(p, q)) == 0);
    q = 3;
    CHECK(ZuCmp<V>::cmp(i, T(p, q)) < 0);
    q = 1;
    CHECK(ZuCmp<V>::cmp(i, T(p, q)) > 0);
    p = 1, q = 2;
    CHECK(ZuCmp<V>::cmp(i, ZuFwdTuple(p, q)) == 0);
    q = 3;
    CHECK(ZuCmp<V>::cmp(i, ZuFwdTuple(p, q)) < 0);
    q = 1;
    CHECK(ZuCmp<V>::cmp(i, ZuFwdTuple(p, q)) > 0);
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
    CHECK(i.id() == 1);
    CHECK(i.age() == 2);
    CHECK(i.height() == 3);
    ZuBox<int> p = 1;
    ZuBox<int> q = 2;
    ZuBox<int> r = 3;
    CHECK(ZuCmp<V>::cmp(i, T(p, q, r)) == 0);
    q = 3;
    CHECK(ZuCmp<V>::cmp(i, T(p, q, r)) < 0);
    q = r = 2;
    CHECK(ZuCmp<V>::cmp(i, T(p, q, r)) > 0);
    q = 2, r = 3;
    CHECK(ZuCmp<V>::cmp(i, ZuFwdTuple(p, q, r)) == 0);
    q = 3;
    CHECK(ZuCmp<V>::cmp(i, ZuFwdTuple(p, q, r)) < 0);
    q = r = 2;
    CHECK(ZuCmp<V>::cmp(i, ZuFwdTuple(p, q, r)) > 0);
  }

  {
    using namespace T2;
    int c = 42;

    {
      V j;
      j.name("3");
      V i;
      i = j;
      CHECK(i.name() == j.name());
      CHECK(i == j);
      CHECK(ZuCmp<V>::cmp(i, j) == 0);
      j.name("4");
      CHECK(ZuCmp<V>::cmp(i, j) < 0);
      i.income(200.0);
      CHECK(ZuCmp<V>::cmp(i, j) < 0);
      j.id(42);
      CHECK(ZuCmp<V>::cmp(i, j) > 0);
      i.dependents(ZuFwdTuple(1, 2));
      j = i;
      CHECK(i == j);
      CHECK(ZuCmp<V>::cmp(i, j) == 0);
      CHECK(i.dependents() == j.dependents());
      j.dependents(ZuFwdTuple(1, 3));
      CHECK(ZuCmp<V>::cmp(i, j) < 0);
      i.dependents(ZuFwdTuple(1, 4));
      CHECK(ZuCmp<V>::cmp(i, j) > 0);
      i.foo(&c);
      CHECK(*(i.foo()) == 42);
      ++*(i.foo());
    }
    CHECK(c == 43);
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
    CHECK((ZuCmp<ZuTuple<int, const S &, const S &> >::cmp(t1, t2) < 0));
    CHECK((ZuCmp<ZuTuple<int, const S &, const S &> >::cmp(t1, t1) == 0));
    CHECK((ZuCmp<ZuTuple<int, const S &, const S &> >::cmp(t2, t1) > 0));
    ZuTuple<int, const S &, const S &> t3 = ZuFwdTuple(42, s3, s3);
    CHECK((ZuCmp<ZuTuple<int, const S &, const S &> >::cmp(t1, t3) < 0));
    S s4{"hello"};
    S s5{"world"};
    std::cout << "t1=" << t1 << '\n' << std::flush;
    std::cout << "t2=" << ZuFwdTuple(42, s4, s5) << '\n' << std::flush;
    CHECK((ZuCmp<ZuTuple<int, const S &, const S &> >::cmp(t1,
	    ZuFwdTuple(42, s4, s5)) > 0));
    // ZuTuple<int, const S &, const S &> t4(42, "string1", "string2");
    // CHECK((ZuCmp<ZuTuple<int, const S &, const S &> >::cmp(t4, t2) < 0));
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
    CHECK((s.p<0>()[1] == s.p<0>()[0]));
    // below deliberately triggers use of uninitialized memory
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wuninitialized"
#endif
    std::cout << int(t.p<1>()[1]) << '\n';
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
  }

  {
    ZuCArray<10> s = "hello world";
    CHECK(s == "hello worl");
    s = ZuArray<char, 10>("hello world");
    CHECK(s == "hello worl");
    s = 'h';
    CHECK(s == "h");
    s << ZuArray<char, 2>("el");
    s += "lo ";
    s << ZuCArray<6>("world");
    CHECK(s == "hello worl");
  }

  {
    checkNull<int16_t, uint32_t>();
    checkNull<uint32_t, int16_t>();
    checkNull<int16_t, int32_t>();
    checkNull<int32_t, int16_t>();
    checkNull<int16_t, uint64_t>();
    checkNull<int64_t, uint16_t>();
    checkNull<double, uint16_t>();
    checkNull<int32_t, double>();
  }

  {
    SortTest<0>::test();
    SortTest<1>::test();
    SortTest<2>::test();
    SortTest<8>::test();
    SortTest<20>::test();
  }

  {
    using U = ZuUnion<int, float, double>;
    char c_[sizeof(U)];
    *reinterpret_cast<double *>(c_) = 42.0;
    auto c = reinterpret_cast<U *>(c_);
    c->type_(U::Index<double>{});
    CHECK(c->p<double>() == 42.0);
    auto d = get<double>(*c);
    CHECK(d == 42.0);
    *c = 42.0;
    d = get<double>(*c);
    CHECK(d == 42.0);
    c->~U();
    new (c) U{42.0};
    d = get<double>(*c);
    CHECK(d == 42.0);
  }

  {
    using U = ZuUnion<void, int>;
    U u;
    std::cout << u.type() << '\n';
  }

  // structured binding smoke tests
  {
    ZuArray<int, 3> foo = { 1, 2, 3 };
    auto [a, b, c] = foo;
    CHECK(a == 1 && b == 2 && c == 3);
  }

  {
    ZuTuple<uint64_t, uint32_t> foo = { 1U, 2U };
    auto [a, b] = foo;
    CHECK(a == 1 && b == 2);
  }

  {
    ZuTuple<uint64_t, uint32_t, uint16_t> foo =
      { 1U, 2U, static_cast<uint16_t>(3U) };
    auto [a, b, c] = foo;
    CHECK(a == 1 && b == 2 && c == 3);
  }

  {
    ZuUnsigned<1> i;
    ZuUnsigned<i> j;
    CHECK(K<j>::k == 1);
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
    CHECK(!a);
    CHECK(!ZuCmp<A>::cmp(a, a));
    CHECK(!ZuCmp<A>::cmp(c, c));
    CHECK(a < b);
    CHECK(ZuCmp<A>::cmp(a, b) < 0);
    CHECK(ZuCmp<A>::cmp(c, b) > 0);
  }

  {
    CHECK(M{} == ZuCmp<M>::null());
  }

  {
    ZuRef<O> o = new O{};
    CHECK(ZuObjectTraits<O>::IsObject);
  }

  {
    ZuUnion<void, bool> a, b = true;
    CHECK(ZuCmp<unsigned>::cmp(a.type(), 0) == 0);
    CHECK(ZuCmp<unsigned>::cmp(b.type(), 1) == 0);
    CHECK(ZuCmp<bool>::cmp(b.p<bool>(), true) == 0);
  }

  {
    CHECK(bool(ZuHash_Can_hash<T1::V>{}));
  }

  foo([]{});

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
      CHECK(true);
    }
    A b = B::foo(ZuMv(u));
    try {
      throw ZuMv(b);
    } catch (A &a) {
      CHECK(true);
    }
  }
  {
    ZuID id = "foobar";
    ZuCSpan s(id);
    CHECK(s == "foobar");
  }
  {
    int x = 0;
    auto m = [x]() mutable { ++x; return x; };
    auto c = [&x]() { return x; };
    using M = ZuDecay<decltype(m)>;
    using C = ZuDecay<decltype(c)>;
    CHECK(ZuIsMutableLambda<M>{});
    CHECK(ZuIsMutableLambda<M &>{});
    CHECK(ZuIsMutableLambda<const M &>{});
    CHECK(!ZuIsMutableLambda<C>{});
    CHECK(!ZuIsMutableLambda<C &>{});
    CHECK(!ZuIsMutableLambda<const C &>{});
  }
  {
    Foo foo;
    const Foo &cfoo = foo;
    CHECK((ZuIsSame<CRef, decltype(bar(cfoo))>{}));
    CHECK((ZuIsSame<Ref, decltype(bar(foo))>{}));
    CHECK((ZuIsSame<RRef, decltype(bar(ZuMv(foo)))>{}));
  }
  {
    struct _ { };
    using N = Narrow<_, ZuTuple<int>>;
    N n{42}, o(43);
    o = n = 44;
    CHECK(o == n);
  }
  {
    std::tuple<int, int> p{ 1, 2 };
    std::pair<int, int> q = { 3, 4 };
    std::array<int, 2> a = { -3, -4 };
    ZuTuple<int, int> r{ 5, 6 };
    ZuTuple<int, int> s = { 7, 8 };
    CHECK(r.p<0>() == 5);
    CHECK(s.p<0>() == 7);
    r = a;
    s = ZuTuple<int, int>{a};
    CHECK(r.p<0>() == -3);
    CHECK(s.p<1>() == -4);
    ZuTuple<int, int> t{p};
    ZuTuple<int, int> u = q;
    ZuTuple<int, int> v; v = q;
    CHECK(t.p<1>() == 2);
    CHECK(u.p<1>() == 4);
    CHECK(v.p<1>() == 4);
    ZuTuple<int, int> w = { 42 };
    CHECK(w.p<0>() == 42);
    CHECK(w.p<1>() == 0);
  }

  {
    std::cout
      << "sizeof(ZuUnion<void, uintptr_t>)="
      << sizeof(ZuUnion<void, uintptr_t>) << '\n';
    std::cout
      << "sizeof(std::optional<uintptr_t>)="
      << sizeof(std::optional<uintptr_t>) << '\n';
  }

  {
    using A = ZuSpan<ZuTuple<ZuCSpan, ZuCSpan>>;
    A a = { { "foo", "bar" }, { "baz", "bah" } };
    CHECK(a[0].p<0>() == "foo");
    CHECK(a[0].p<1>() == "bar");
    CHECK(a[1].p<0>() == "baz");
    CHECK(a[1].p<1>() == "bah");
  }
  {
    enum { I = (ZuSpan<ZuTuple<int>>( { 42 } ))[0].p<0>() };
    CHECK(I == 42);
  }
  {
    const auto x = ZuTuple{42, 43};
    CHECK(x.p<0>() == 42);
  }

  {
    struct A { int i = 42; };

    A a;
    CHECK((ZuIsSame<int &, decltype(ZuFwdLike<A &>(a.i))>{}));
    CHECK((ZuIsSame<const int &, decltype(ZuFwdLike<const A &>(a.i))>{}));
    CHECK((ZuIsSame<int &&, decltype(ZuFwdLike<A &&>(a.i))>{}));
  }

  {
    char buf[] = "foo bar baz";
    const char buf2[] = "foo bar baz";
    CHECK((ZuCSpan::IsStrLiteral<decltype(buf)>{}));
    CHECK((ZuCSpan::IsStrLiteral<decltype(buf2)>{}));
  }

  {
    ZuTuple<int, int> i{42, 42};
    CHECK((i == decltype(i){42, 42}));
  }
}
