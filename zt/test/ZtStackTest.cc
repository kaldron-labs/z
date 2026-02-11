//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZuCmp.hh>

#include <zlib/ZmAtomic.hh>
#include <zlib/ZmQueue.hh>

#include <zlib/ZtStack.hh>

using namespace ZuTestUtil;

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
  if (!verbose) return;
  typename S::Iter i(s);
  C c;
  while (!ZuCmp<C>::null(c = i()))
    std::cerr << c.value() << ' ';
  std::cerr << '\n';
}

template <typename S>
void dump2(S &s)
{
  if (!verbose) return;
  typename S::RevIter i(s);
  C c;
  while (!ZuCmp<C>::null(c = i()))
    std::cerr << c.value() << ' ';
  std::cerr << '\n';
}

template <typename S>
void doit(S &s)
{
  ZuTestScope(doit);

  static int del1[] = { 8,7,6,4,3,1,-1 };
  static int del2[] = { 1,3,4,6,7,8,-1 };

  for (int i = 1; i < 10; i++) s.push(C{i});
  for (int i = 0; del1[i] >= 0; i++) s.del(C{del1[i]});
  dump(s);
  ZuCheck(s.pop().value() == 9);
  ZuCheck(s.pop().value() == 5);
  ZuCheck(s.pop().value() == 2);
  ZuCheck(ZuCmp<C>::null(s.pop()));

  for (int i = 9; i > 0; i--) s.push(C{i});
  for (int i = 0; del2[i] >= 0; i++) s.del(C{del2[i]});
  dump(s);
  ZuCheck(s.pop().value() == 2);
  ZuCheck(s.pop().value() == 5);
  ZuCheck(s.pop().value() == 9);
  ZuCheck(ZuCmp<C>::null(s.pop()));
}

template <typename S>
void doit2(S &s)
{
  ZuTestScope(doit2);

  static int del1[] = { 8,7,6,4,3,1,-1 };
  static int del2[] = { 1,3,4,6,7,8,-1 };

  for (int i = 1; i < 10; i++) s.push(C{i});
  for (int i = 0; del1[i] >= 0; i++) s.del(C{del1[i]});
  dump(s);
  ZuCheck(s.pop().value() == 9);
  ZuCheck(s.pop().value() == 5);
  ZuCheck(s.pop().value() == 2);
  ZuCheck(ZuCmp<C>::null(s.pop()));

  for (int i = 1; i < 10; i++) s.push(C{i});
  for (int i = 0; del2[i] >= 0; i++) s.del(C{del2[i]});
  dump(s);
  ZuCheck(s.shift().value() == 2);
  ZuCheck(s.shift().value() == 5);
  ZuCheck(s.shift().value() == 9);
  ZuCheck(ZuCmp<C>::null(s.shift()));

  for (int i = 1; i < 10; i++) s.unshift(C{i});
  for (int i = 0; del1[i] >= 0; i++) s.del(C{del1[i]});
  dump2(s);
  ZuCheck(s.shift().value() == 9);
  ZuCheck(s.shift().value() == 5);
  ZuCheck(s.shift().value() == 2);
  ZuCheck(ZuCmp<C>::null(s.shift()));

  for (int i = 1; i < 10; i++) s.unshift(C{i});
  for (int i = 0; del2[i] >= 0; i++) s.del(C{del2[i]});
  dump2(s);
  ZuCheck(s.pop().value() == 2);
  ZuCheck(s.pop().value() == 5);
  ZuCheck(s.pop().value() == 9);
  ZuCheck(ZuCmp<C>::null(s.pop()));

  s.clean();
  int n = s.size();
  s.push(C{0});
  for (int i = 1; i < n; i++) { s.push(C{i}); s.shift(); }
  for (int i = 0; i < n - 1; i++) s.push(C{i});
  s.clean();
  n = s.size();
  s.push(C{0});
  for (int i = 1; i < n; i++) { s.push(C{i}); s.shift(); }
  n = s.size() + 1;
  for (int i = 0; i < n; i++) s.push(C{i});
}

template <int MaxFrag>
void runStackFrag()
{
  ZuTestScope(runStackFrag);

  ZtStack<C> s1, s2, s3;
  s1.init(ZtStackParams().initial(1).maxFrag(MaxFrag));
  s2.init(ZtStackParams().initial(2).maxFrag(MaxFrag));
  s3.init(ZtStackParams().initial(9).maxFrag(MaxFrag));

  ZuTestCall_( "initial=1", doit, s1);
  ZuTestCall_( "initial=2", doit, s2);
  ZuTestCall_( "initial=9", doit, s3);
}

template <int MaxFrag>
void runQueueFrag()
{
  ZuTestScope(runQueueFrag);

  ZmQueue<C> r1, r2, r3;
  r1.init(ZmQueueParams().initial(1).maxFrag(MaxFrag));
  r2.init(ZmQueueParams().initial(2).maxFrag(MaxFrag));
  r3.init(ZmQueueParams().initial(9).maxFrag(MaxFrag));

  ZuTestCall_( "initial=1", doit2, r1);
  ZuTestCall_( "initial=2", doit2, r2);
  ZuTestCall_( "initial=9", doit2, r3);
}

void testStackVariants()
{
  ZuTestScope(testStackVariants);

  ZuTestCall_("maxFrag=0", (runStackFrag<0>));
  ZuTestCall_("maxFrag=10", (runStackFrag<10>));
  ZuTestCall_("maxFrag=20", (runStackFrag<20>));
  ZuTestCall_("maxFrag=30", (runStackFrag<30>));
  ZuTestCall_("maxFrag=40", (runStackFrag<40>));
  ZuTestCall_("maxFrag=50", (runStackFrag<50>));
  ZuTestCall_("maxFrag=60", (runStackFrag<60>));
  ZuTestCall_("maxFrag=70", (runStackFrag<70>));
  ZuTestCall_("maxFrag=80", (runStackFrag<80>));
  ZuTestCall_("maxFrag=90", (runStackFrag<90>));
  ZuCheck(C::m_count <= 1);
}

void testQueueVariants()
{
  ZuTestScope(testQueueVariants);

  ZuTestCall_("maxFrag=0", (runQueueFrag<0>));
  ZuTestCall_("maxFrag=10", (runQueueFrag<10>));
  ZuTestCall_("maxFrag=20", (runQueueFrag<20>));
  ZuTestCall_("maxFrag=30", (runQueueFrag<30>));
  ZuTestCall_("maxFrag=40", (runQueueFrag<40>));
  ZuTestCall_("maxFrag=50", (runQueueFrag<50>));
  ZuTestCall_("maxFrag=60", (runQueueFrag<60>));
  ZuTestCall_("maxFrag=70", (runQueueFrag<70>));
  ZuTestCall_("maxFrag=80", (runQueueFrag<80>));
  ZuTestCall_("maxFrag=90", (runQueueFrag<90>));
  ZuCheck(C::m_count <= 1);
}

ZmAtomic<uint32_t> C::m_count = 0;

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testStackVariants);
  ZuTestCall(testQueueVariants);
  return 0;
}
