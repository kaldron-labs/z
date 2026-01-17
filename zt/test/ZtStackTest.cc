//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuLib.hh>

#include <stdio.h>

#include <zlib/ZuCmp.hh>

#include <zlib/ZmAtomic.hh>
#include <zlib/ZmQueue.hh>
#include <zlib/ZmAssert.hh>

#include <zlib/ZtStack.hh>

void out(bool ok, const char *s) 
{
  std::cout << (ok ? "OK  " : "NOK ") << s << '\n' << std::flush;
  ZmAssert(ok);
}

#define CHECK(x) (out((x), #x))

struct C {
  C() : m_i(0) { m_count++; }
  C(int i) : m_i(i) { m_count++; }
  C(const C &c) : m_i(c.m_i) { m_count++; }
  C &operator =(const C &c) {
    if (this != &c) m_i = c.m_i;
    return *this;
  }
  ~C() { --m_count; }
  int value() const { return m_i; }
  bool equals(const C &c) const { return m_i == c.m_i; }
  int cmp(const C &c) const { return ZuCmp<int>::cmp(m_i, c.m_i); }
  friend inline bool operator ==(const C &l, const C &r) {
    return l.equals(r);
  }
  friend inline int operator <=>(const C &l, const C &r) {
    return l.cmp(r);
  }
  bool operator !() const { return !m_i; }

  int				m_i;
  static ZmAtomic<uint32_t>	m_count;
};

template <typename S>
void dump(S &s)
{
  typename S::Iter i(s);
  {
    C c;

    while (!ZuCmp<C>::null(c = i()))
      std::cout << c.value() << ' ';
  }
  std::cout << '\n' << std::flush;
}

template <typename S>
void dump2(S &s)
{
  typename S::RevIter i(s);
  {
    C c;

    while (!ZuCmp<C>::null(c = i()))
      std::cout << c.value() << ' ';
  }
  std::cout << '\n' << std::flush;
}

template <typename S>
void doit(S &s)
{
  static int del1[] = { 8,7,6,4,3,1,-1 };
  static int del2[] = { 1,3,4,6,7,8,-1 };
  int i;

  for (i = 1; i < 10; i++) s.push(C{i});
  for (i = 0; del1[i] >= 0; i++) s.del(C{del1[i]});
  dump(s);
  CHECK(s.pop().value() == 9);
  CHECK(s.pop().value() == 5);
  CHECK(s.pop().value() == 2);
  CHECK(ZuCmp<C>::null(s.pop()));
  for (i = 9; i > 0; i--) s.push(C{i});
  for (i = 0; del2[i] >= 0; i++) s.del(C{del2[i]});
  dump(s);
  CHECK(s.pop().value() == 2);
  CHECK(s.pop().value() == 5);
  CHECK(s.pop().value() == 9);
  CHECK(ZuCmp<C>::null(s.pop()));
}

template <typename S>
void doit2(S &s)
{
  static int del1[] = { 8,7,6,4,3,1,-1 };
  static int del2[] = { 1,3,4,6,7,8,-1 };
  int i;

  for (i = 1; i < 10; i++) s.push(C{i});
  for (i = 0; del1[i] >= 0; i++) s.del(C{del1[i]});
  dump(s);
  CHECK(s.pop().value() == 9);
  CHECK(s.pop().value() == 5);
  CHECK(s.pop().value() == 2);
  CHECK(ZuCmp<C>::null(s.pop()));
  for (i = 1; i < 10; i++) s.push(C{i});
  for (i = 0; del2[i] >= 0; i++) s.del(C{del2[i]});
  dump(s);
  CHECK(s.shift().value() == 2);
  CHECK(s.shift().value() == 5);
  CHECK(s.shift().value() == 9);
  CHECK(ZuCmp<C>::null(s.shift()));
  for (i = 1; i < 10; i++) s.unshift(C{i});
  for (i = 0; del1[i] >= 0; i++) s.del(C{del1[i]});
  dump2(s);
  CHECK(s.shift().value() == 9);
  CHECK(s.shift().value() == 5);
  CHECK(s.shift().value() == 2);
  CHECK(ZuCmp<C>::null(s.shift()));
  for (i = 1; i < 10; i++) s.unshift(C{i});
  for (i = 0; del2[i] >= 0; i++) s.del(C{del2[i]});
  dump2(s);
  CHECK(s.pop().value() == 2);
  CHECK(s.pop().value() == 5);
  CHECK(s.pop().value() == 9);
  CHECK(ZuCmp<C>::null(s.pop()));
  s.clean();
  int n = s.size();
  s.push(C{0});
  for (i = 1; i < n; i++) { s.push(C{i}); s.shift(); }
  for (i = 0; i < n - 1; i++) s.push(C{i});
  s.clean();
  n = s.size();
  s.push(C{0});
  for (i = 1; i < n; i++) { s.push(C{i}); s.shift(); }
  n = s.size() + 1;
  for (i = 0; i < n; i++) s.push(C{i});
}

ZmAtomic<uint32_t> C::m_count = 0;

int main(int argc, char **argv)
{
  for (int i = 0; i < 100; i += 10) {
    ZtStack<C> s1, s2, s3;

    s1.init(ZtStackParams().initial(1).maxFrag(i));
    s2.init(ZtStackParams().initial(2).maxFrag(i));
    s3.init(ZtStackParams().initial(9).maxFrag(i));

    doit(s1);
    doit(s2);
    doit(s3);
  }

  CHECK(C::m_count <= 1);

  for (int i = 0; i < 100; i += 10) {
    ZmQueue<C> r1, r2, r3;

    r1.init(ZmQueueParams().initial(1).maxFrag(i));
    r2.init(ZmQueueParams().initial(2).maxFrag(i));
    r3.init(ZmQueueParams().initial(9).maxFrag(i));

    doit2(r1);
    doit2(r2);
    doit2(r3);
  }

  CHECK(C::m_count <= 1);
}
