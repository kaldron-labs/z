//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuLib.hh>

#include <assert.h>

#include <iostream>

#include <zlib/ZtArray.hh>
#include <zlib/ZtString.hh>

#ifdef _MSC_VER
#pragma warning(disable:4355)
#endif

template <typename T>
void out(T &&v) {
  std::cout << ZuFwd<T>(v) << '\n';
}

#define CHECK(x) ((x) ? out("OK  " #x) : out("NOK " #x))

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
  assert(a.length() == length);
  uint64_t n = a.length();
  for (uint64_t i = 0; i < n; i++) {
    if (a[i].ptr() != (const void *)&a[i]) {
      printf("\n%u %p != %p\n", unsigned(i), a[i].ptr(), &a[i]);
    }
    printf("%u:%d:%d ", unsigned(i), a[i].copied(), a[i].moved());
  }
  putc('\n', stdout);
}

struct Foo {
  Foo() { }
  template <typename S> Foo(const S &s) : bar(s) { }
  ZtString<> bar;
};

#include <vector>

int main()
{
  {
    E e[8];
    ZtArray<E> a;
    a = ZtArray<E>{e, 8, 8, false};
    ZtArray<E> b;

    validate(a, 8);
    a.splice(0, 0, ZuSpan(e, 4));
    validate(a, 12);
    a.splice(b, 0, 4);
    validate(a, 8);
    validate(b, 4);
    a.splice(0, 0, b);
    validate(a, 12);
    validate(b, 4);
    b.splice(8, 100, ZtArray<E>{b});
    validate(b, 12);
    for (int i = 0; i < 8; i++) a.push(a.shift());
    validate(a, 12);
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
    CHECK(ptr == a.data());
  }

  {
    ZtArray<wchar_t> w;
    w << L"hello " << "world" << L'!' << ' ' << 42;
    ZtArray<char> s = w;
    CHECK(s == "hello world! 42");
    s = {};
    s << L"hello " << "world" << L'!' << ' ' << 42;
    w = s;
    CHECK(w == L"hello world! 42");
  }

  {
    ZtArray<ZtString<>> a = { "foo", "bar", "baz", "bah" };
    ZtString<> o;
    a.all([&o, first = true](const ZtString<> &s) mutable {
      if (first) first = false; else o << '.';
      o << s;
    });
    CHECK(o == "foo.bar.baz.bah");
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

  return 0;
}
