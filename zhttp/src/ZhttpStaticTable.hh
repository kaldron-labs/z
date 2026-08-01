//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z HTTP compile-time static header table lookup

#ifndef ZhttpStaticTable_HH
#define ZhttpStaticTable_HH

#ifndef ZhttpLib_HH
#include <zlib/ZhttpLib.hh>
#endif

#include <zlib/ZuString.hh>
#include <zlib/ZuTL.hh>

namespace Zhttp {

template <typename U> struct StaticTail_ { using T = ZuTypeList<U>; };
template <typename ...Us>
struct StaticTail_<ZuTypeList<Us...>> { using T = ZuTypeList<Us...>; };

template <typename U, typename List, bool = ZuTypeIn<U, List>{}>
struct StaticUniqueAdd_ {
  using T = typename List::template Unshift<U>;
};
template <typename U, typename List>
struct StaticUniqueAdd_<U, List, true> { using T = List; };

template <typename ...> struct StaticUnique_;
template <> struct StaticUnique_<> { using T = ZuTypeList<>; };
template <typename U> struct StaticUnique_<U> {
  using T = ZuTypeList<U>;
};
template <typename U, typename V>
struct StaticUnique_<U, V> :
  public StaticUniqueAdd_<U, typename StaticTail_<V>::T> { };
template <typename ...Us>
using StaticUnique = typename StaticUnique_<Us...>::T;

template <typename Key_, typename Value_ = void>
struct StaticEntry {
  using Key = Key_;
  using Value = Value_;
};

template <typename KV> using StaticKey = typename KV::Key;
template <typename KV> using StaticValue = typename KV::Value;

template <typename KV, bool = ZuIsSame<StaticValue<KV>, void>{}>
struct StaticMatchValue_ { using T = StaticValue<KV>; };
template <typename KV>
struct StaticMatchValue_<KV, true> { using T = ZuStringT<"">; };
template <typename KV>
using StaticMatchValue = typename StaticMatchValue_<KV>::T;

template <typename Tbl>
struct StaticTable {
  using Keys = ZuTypeMap<StaticKey, Tbl>;
  using Names = ZuTypeReduce<StaticUnique, Keys>;

  template <typename Key>
  struct Entries_ {
    template <typename KV>
    using Is = ZuIsSame<Key, StaticKey<KV>>;
    using T = ZuTypeGrep<Is, Tbl>;
  };
  template <typename Key>
  using Entries = typename Entries_<Key>::T;
  template <typename Key>
  using Values = ZuTypeMap<StaticMatchValue, Entries<Key>>;
};

} // namespace Zhttp

#endif /* ZhttpStaticTable_HH */
