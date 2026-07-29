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

#include <zlib/ZuMatcher.hh>
#include <zlib/ZuString.hh>
#include <zlib/ZuSwitch.hh>
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

template <typename KV> using StaticKey = ZuType<0, KV>;

template <typename KV, bool = (KV::N > 1)>
struct StaticValue_ { using T = void; };
template <typename KV>
struct StaticValue_<KV, true> { using T = ZuType<1, KV>; };
template <typename KV>
using StaticValue = typename StaticValue_<KV>::T;

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

  static int name(ZuCSpan value) {
    static constexpr auto matcher = ZuMatcher<Names>();
    return matcher.exact(value);
  }

  static int nameIndex(ZuCSpan value) {
    int i = name(value);
    if (i < 0) return -1;
    int index = -1;
    ZuSwitch::dispatch<Names::N>(unsigned(i), [&index](auto i) {
      using Key = ZuType<i, Names>;
      index = ZuTypeIndex<Key, Keys>{};
    });
    return index;
  }

  static int index(ZuCSpan name_, ZuCSpan value) {
    int i = name(name_);
    if (i < 0) return -1;
    int index = -1;
    ZuSwitch::dispatch<Names::N>(
      unsigned(i), [&index, &value](auto i) {
	using Key = ZuType<i, Names>;
	using KeyEntries = Entries<Key>;
	using KeyValues = Values<Key>;
	static constexpr auto matcher = ZuMatcher<KeyValues>();
	int j = matcher.exact(value);
	if (j < 0) return;
	ZuSwitch::dispatch<KeyEntries::N>(unsigned(j), [&index](auto j) {
	  using KV = ZuType<j, KeyEntries>;
	  index = ZuTypeIndex<KV, Tbl>{};
	});
      });
    return index;
  }
};

} // namespace Zhttp

#endif /* ZhttpStaticTable_HH */
