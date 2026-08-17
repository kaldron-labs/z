//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// common compile-time contract and iterator access for Zf map formatting

#ifndef ZfMap_HH
#define ZfMap_HH

#ifndef ZfLib_HH
#include <zlib/ZfLib.hh>
#endif

#include <zlib/ZuTraits.hh>

#include <zlib/ZmRef.hh>

namespace ZfMap {

struct Access {
  template <typename Node>
  static auto key_(Node node, int) -> decltype(node->key())
    { return node->key(); }
  template <typename Node>
  static decltype(auto) key_(Node node, ...) { return node->template p<0>(); }
  template <typename Node>
  static decltype(auto) key(Node node) { return key_(node, 0); }

  template <typename Node>
  static auto val_(Node node, int) -> decltype(node->val())
    { return node->val(); }
  template <typename Node>
  static decltype(auto) val_(Node node, ...) { return node->template p<1>(); }
  template <typename Node>
  static decltype(auto) val(Node node) { return val_(node, 0); }
};

template <typename O_>
struct Traits : public Access {
  using O = O_;
  using Container = typename O::T;
  using Key = typename Container::Key;
  using Val = typename Container::Val;

  ZuAssert((ZuIsSame<O, ZmRef<Container>>{}));
  ZuAssert(ZuTraits<Key>::IsString && !ZuTraits<Key>::IsWString);
};

} // ZfMap

#endif /* ZfMap_HH */
