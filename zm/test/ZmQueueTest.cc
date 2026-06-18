//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZuCmp.hh>

#include <zlib/ZmAtomic.hh>
#include <zlib/ZmQueue.hh>

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

void testParamsAndEmpty()
{
  ZuTestScope(testParamsAndEmpty);

  ZmQueueParams params;
  ZuCheck(params.initial() == 0);
  ZuCheck(params.maxFrag() == ZmQueueMaxFrag);

  ZmQueue<C> q{ZmQueueParams{}.initial(2).maxFrag(25)};
  ZuCheck(q.size() == 2);
  ZuCheck(q.length() == 0);
  ZuCheck(q.count_() == 0);
  ZuCheck(q.offset_() == 0);
  ZuCheck(q.maxFrag() == 25);

  ZuCheck(ZuCmp<C>::null(q.pop()));
  ZuCheck(ZuCmp<C>::null(q.shift()));
  ZuCheck(ZuCmp<C>::null(q.head()));
  ZuCheck(ZuCmp<C>::null(q.tail()));
  ZuCheck(ZuCmp<C>::null(q.find(C{1})));
  ZuCheck(ZuCmp<C>::null(q.del(C{1})));
}

void testFIFOLIFO()
{
  ZuTestScope(testFIFOLIFO);

  ZmQueue<C> q{ZmQueueParams{}.initial(2).maxFrag(30)};

  q.push(C{1});
  q.push(C{2});
  q.push(C{3});
  ZuCheck(q.head().value() == 1);
  ZuCheck(q.tail().value() == 3);
  ZuCheck(q.shift().value() == 1);

  q.unshift(C{0});
  ZuCheck(q.shift().value() == 0);

  ZuCheck(q.pop().value() == 3);
  ZuCheck(q.pop().value() == 2);
  ZuCheck(ZuCmp<C>::null(q.pop()));
}

void testDeleteAndDefrag()
{
  ZuTestScope(testDeleteAndDefrag);

  ZmQueue<C> q{ZmQueueParams{}.initial(8).maxFrag(0)};
  for (int i = 1; i <= 6; i++) q.push(C{i});

  ZuCheck(q.find(C{4}).value() == 4);

  C *ptr = q.findPtr(C{4});
  ZuCheck(ptr && ptr->value() == 4);
  q.delPtr(ptr);
  ZuCheck(ZuCmp<C>::null(q.find(C{4})));

  ZuCheck(q.del(C{2}).value() == 2);
  ZuCheck(ZuCmp<C>::null(q.del(C{2})));
  ZuCheck(q.length_() == q.count_());

  q.clean();
  ZuCheck(q.length() == 0);
  ZuCheck(q.count_() == 0);
  ZuCheck(q.offset_() == 0);
}

void testIterAndWrap()
{
  ZuTestScope(testIterAndWrap);

  ZmQueue<C> q{ZmQueueParams{}.initial(4).maxFrag(80)};
  q.push(C{1});
  q.push(C{2});
  q.push(C{3});
  ZuCheck(q.shift().value() == 1);
  q.push(C{4});
  q.push(C{5});
  ZuCheck(q.offset_() != 0);
  ZuCheck(q.del(C{3}).value() == 3);

  {
    auto i = q.iter();
    ZuCheck(i().value() == 2);
    ZuCheck(i().value() == 4);
    ZuCheck(i().value() == 5);
    ZuCheck(ZuCmp<C>::null(i()));
  }
  {
    auto i = q.riter();
    ZuCheck(i().value() == 5);
    ZuCheck(i().value() == 4);
    ZuCheck(i().value() == 2);
    ZuCheck(ZuCmp<C>::null(i()));
  }
  {
    auto i = q.iter();
    C *ptr = i.ptr();
    ZuCheck(ptr && ptr->value() == 2);
  }
  {
    auto i = q.riter();
    C *ptr = i.ptr();
    ZuCheck(ptr && ptr->value() == 5);
  }
}

void testMove()
{
  ZuTestScope(testMove);

  ZmQueue<C> q{ZmQueueParams{}.initial(3).maxFrag(30)};
  q.push(C{7});
  q.push(C{8});

  ZmQueue<C> moved{ZuMv(q)};
  ZuCheck(moved.maxFrag() == 30);
  ZuCheck(moved.shift().value() == 7);

  ZmQueue<C> assigned;
  assigned = ZuMv(moved);
  ZuCheck(assigned.shift().value() == 8);
  ZuCheck(ZuCmp<C>::null(assigned.shift()));
}

ZmAtomic<uint32_t> C::m_count = 0;

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testParamsAndEmpty);
  ZuTestCall(testFIFOLIFO);
  ZuTestCall(testDeleteAndDefrag);
  ZuTestCall(testIterAndWrap);
  ZuTestCall(testMove);
  ZuCheck(C::m_count <= 1);
  return 0;
}
