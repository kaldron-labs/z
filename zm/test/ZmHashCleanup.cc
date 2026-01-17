//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

/* test program */

#include <zlib/ZmHash.hh>

struct Object : public ZmObject {
  Object(int val) : m_val(val) { }
  int hash() const { return m_val; }
  int cmp(const Object &i) const { return ZuCmp<int>::cmp(m_val, i.m_val); }
  bool operator !() const { return !m_val; }

  int m_val;
};

inline bool operator ==(const Object &l, const Object &r) {
  return l.m_val == r.m_val;
}

ZuDerive(ObjectHash, (ZmHash<ZmRef<Object>>));

int main(int argc, char *argv[])
{
  ZmRef<ObjectHash> hash =
    new ObjectHash(ZmHashParams().bits(4).loadFactor(2));

  for (int i = 0; i < 32; ++i)
    hash->add(ZmRef<Object>(new Object(i)));

  ObjectHash::Iter it(*hash);

  while (it.key()) it.del();

  hash = 0;

  return 0;
}
