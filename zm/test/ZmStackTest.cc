//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZuCmp.hh>

#include <zlib/ZmAtomic.hh>

#include <zlib/ZmStack.hh>

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

ZmStackDerive(S, C, ZmStack_Defaults);

void testParamsAndEmpty()
{
  ZuTestScope(testParamsAndEmpty);

  ZmStackParams params;
  ZuCheck(params.initial() == 0);
  ZuCheck(params.maxFrag() == ZmStackMaxFrag);

  S s{ZmStackParams{}.initial(2).maxFrag(25)};
  ZuCheck(s.size() == 2);
  ZuCheck(s.length() == 0);
  ZuCheck(s.count() == 0);
  ZuCheck(s.maxFrag() == 25);

  ZuCheck(ZuCmp<C>::null(s.pop()));
  ZuCheck(ZuCmp<C>::null(s.head()));
  ZuCheck(ZuCmp<C>::null(s.tail()));
  ZuCheck(ZuCmp<C>::null(s.find(C{1})));
  ZuCheck(ZuCmp<C>::null(s.del(C{1})));
}

void testPushPopAndAccess()
{
  ZuTestScope(testPushPopAndAccess);

  ZmStack<C> s{ZmStackParams{}.initial(1).maxFrag(50)};
  s.push(C{1});
  s.push(C{2});
  s.push(C{3});

  ZuCheck(s.size() >= 3);
  ZuCheck(s.length() == 3);
  ZuCheck(s.count() == 3);
  ZuCheck(s.head().value() == 1);
  ZuCheck(s.tail().value() == 3);
  ZuCheck(s.find(C{2}).value() == 2);

  ZuCheck(s.pop().value() == 3);
  ZuCheck(s.pop().value() == 2);
  ZuCheck(s.pop().value() == 1);
  ZuCheck(ZuCmp<C>::null(s.pop()));
}

void testDeleteAndDefrag()
{
  ZuTestScope(testDeleteAndDefrag);

  ZmStack<C> s{ZmStackParams{}.initial(8).maxFrag(0)};
  for (int i = 1; i <= 6; i++) s.push(C{i});

  C *ptr = s.findPtr(C{4});
  ZuCheck(ptr && ptr->value() == 4);
  s.delPtr(ptr);
  ZuCheck(ZuCmp<C>::null(s.find(C{4})));
  ZuCheck(s.del(C{2}).value() == 2);
  ZuCheck(ZuCmp<C>::null(s.del(C{2})));
  ZuCheck(s.length_() == s.count_());

  s.clean();
  ZuCheck(s.length() == 0);
  ZuCheck(s.count() == 0);
}

void testIter()
{
  ZuTestScope(testIter);

  ZmStack<C> s{ZmStackParams{}.initial(8).maxFrag(80)};
  for (int i = 1; i <= 5; i++) s.push(C{i});
  ZuCheck(s.del(C{3}).value() == 3);

  {
    auto i = s.iter();
    ZuCheck(i().value() == 5);
    ZuCheck(i().value() == 4);
    ZuCheck(i().value() == 2);
    ZuCheck(i().value() == 1);
    ZuCheck(ZuCmp<C>::null(i()));
  }
  {
    auto i = s.riter();
    ZuCheck(i().value() == 1);
    ZuCheck(i().value() == 2);
    ZuCheck(i().value() == 4);
    ZuCheck(i().value() == 5);
    ZuCheck(ZuCmp<C>::null(i()));
  }
  {
    auto i = s.iter();
    C *ptr = i.ptr();
    ZuCheck(ptr && ptr->value() == 5);
  }
  {
    auto i = s.riter();
    C *ptr = i.ptr();
    ZuCheck(ptr && ptr->value() == 1);
  }
}

void testMove()
{
  ZuTestScope(testMove);

  ZmStack<C> s{ZmStackParams{}.initial(2).maxFrag(30)};
  s.push(C{7});
  s.push(C{8});

  ZmStack<C> moved{ZuMv(s)};
  ZuCheck(moved.maxFrag() == 30);
  ZuCheck(moved.pop().value() == 8);

  ZmStack<C> assigned;
  assigned = ZuMv(moved);
  ZuCheck(assigned.pop().value() == 7);
  ZuCheck(ZuCmp<C>::null(assigned.pop()));
}

ZmAtomic<uint32_t> C::m_count = 0;

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testParamsAndEmpty);
  ZuTestCall(testPushPopAndAccess);
  ZuTestCall(testDeleteAndDefrag);
  ZuTestCall(testIter);
  ZuTestCall(testMove);
  ZuCheck(C::m_count <= 1);
  return 0;
}
