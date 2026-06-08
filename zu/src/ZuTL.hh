//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z library type lists

#ifndef ZuTL_HH
#define ZuTL_HH

#ifndef ZuLib_HH
#include <zlib/ZuLib.hh>
#endif

// main template
template <typename ...Ts> struct ZuTypeList {
  enum { N = sizeof...(Ts) };
  template <typename ...Ts_> struct Unshift_ {
    using T = ZuTypeList<Ts_..., Ts...>;
  };
  template <typename ...Ts_> struct Unshift_<ZuTypeList<Ts_...>> {
    using T = ZuTypeList<Ts_..., Ts...>;
  };
  template <typename ...Ts_>
  using Unshift = typename Unshift_<Ts_...>::T;

  template <typename ...Ts_> struct Push_ {
    using T = ZuTypeList<Ts..., Ts_...>;
  };
  template <typename ...Ts_> struct Push_<ZuTypeList<Ts_...>> {
    using T = ZuTypeList<Ts..., Ts_...>;
  };
  template <typename ...Ts_>
  using Push = typename Push_<Ts_...>::T;
};

// typelist of repeated type
template <unsigned N, typename List, typename E>
struct ZuTypeRepeat__ {
  using T =
    typename ZuTypeRepeat__<N - 1, typename List::template Push<E>, E>::T;
};
template <typename List, typename E>
struct ZuTypeRepeat__<0, List, E> {
  using T = List;
};
template <unsigned N, typename E>
struct ZuTypeRepeat_ : public ZuTypeRepeat__<N, ZuTypeList<>, E> { };
template <unsigned N, typename E>
using ZuTypeRepeat = typename ZuTypeRepeat_<N, E>::T;

// reverse typelist
template <typename ...Ts> struct ZuTypeRev_;
template <typename ...Ts> struct ZuTypeRev__;
template <typename T0, typename ...Ts>
struct ZuTypeRev__<T0, Ts...> {
  using T = typename ZuTypeRev__<Ts...>::T::template Push<T0>;
};
template <typename T0>
struct ZuTypeRev__<T0> { using T = ZuTypeList<T0>; };
template <>
struct ZuTypeRev__<> { using T = ZuTypeList<>; };
template <typename ...Ts>
struct ZuTypeRev_ { using T = typename ZuTypeRev__<Ts...>::T; };
template <typename ...Ts>
struct ZuTypeRev_<ZuTypeList<Ts...>> {
  using T = typename ZuTypeRev__<Ts...>::T;
};
template <typename ...Ts>
using ZuTypeRev = typename ZuTypeRev_<Ts...>::T;

// index -> type
template <unsigned, typename ...> struct ZuType_;
template <unsigned I, typename T0, typename ...Ts>
struct ZuType__ {
  using T = typename ZuType_<I - 1, Ts...>::T;
};
template <typename T0, typename ...Ts>
struct ZuType__<0, T0, Ts...> {
  using T = T0;
};
template <unsigned I, typename ...Ts>
struct ZuType_ : public ZuType__<I, Ts...> { };
template <unsigned I, typename ...Ts>
struct ZuType_<I, ZuTypeList<Ts...>> : public ZuType_<I, Ts...> { };
template <unsigned I, typename ...Ts>
using ZuType = typename ZuType_<I, Ts...>::T;

// type -> index (returns first match) (undefined if type is not in list)
template <typename, typename ...> struct ZuTypeIndex;
template <typename T, typename ...Ts>
struct ZuTypeIndex<T, T, Ts...> : public ZuUnsigned<0> { };
template <typename T, typename O, typename ...Ts>
struct ZuTypeIndex<T, O, Ts...> :
  public ZuUnsigned<1 + ZuTypeIndex<T, Ts...>{}> { };
template <typename T, typename ...Ts>
struct ZuTypeIndex<T, ZuTypeList<Ts...>> :
  public ZuTypeIndex<T, Ts...> { };

// typelist map
// - maps T to Map<T> for each T in the list
template <template <typename> class, typename ...> struct ZuTypeMap_;
template <template <typename> class Map>
struct ZuTypeMap_<Map> {
  using T = ZuTypeList<>;
};
template <template <typename> class Map, typename T0>
struct ZuTypeMap_<Map, T0> {
  using T = ZuTypeList<Map<T0>>;
};
template <template <typename> class Map, typename T0, typename ...Ts>
struct ZuTypeMap_<Map, T0, Ts...> {
  using T =
    typename ZuTypeList<Map<T0>>::template Push<
      typename ZuTypeMap_<Map, Ts...>::T>;
};
template <template <typename> class Map, typename ...Ts>
struct ZuTypeMap_<Map, ZuTypeList<Ts...>> :
  public ZuTypeMap_<Map, Ts...> { };
template <template <typename> class Map, typename ...Ts>
using ZuTypeMap = typename ZuTypeMap_<Map, Ts...>::T;

// typelist grep
// - Filter<T>{} should be true to include T in resulting list
template <typename T0, bool> struct ZuTypeGrep__ {
  using T = ZuTypeList<T0>;
};
template <typename T0> struct ZuTypeGrep__<T0, false> {
  using T = ZuTypeList<>;
};
template <template <typename> class, typename ...>
struct ZuTypeGrep_ {
  using T = ZuTypeList<>;
};
template <template <typename> class Filter, typename T0>
struct ZuTypeGrep_<Filter, T0> {
  using T = typename ZuTypeGrep__<T0, Filter<T0>{}>::T;
};
template <template <typename> class Filter, typename T0, typename ...Ts>
struct ZuTypeGrep_<Filter, T0, Ts...> {
  using T =
    typename ZuTypeGrep__<T0, Filter<T0>{}>::T::template Push<
      typename ZuTypeGrep_<Filter, Ts...>::T>;
};
template <template <typename> class Filter, typename ...Ts>
struct ZuTypeGrep_<Filter, ZuTypeList<Ts...>> :
  public ZuTypeGrep_<Filter, Ts...> { };
template <template <typename> class Filter, typename ...Ts>
using ZuTypeGrep = typename ZuTypeGrep_<Filter, Ts...>::T;

// test for inclusion in typelist
// ZuTypeIn<T, List>{} evaluates to true if T is in List
template <typename, typename ...>
struct ZuTypeIn : public ZuFalse { };
template <typename U>
struct ZuTypeIn<U, U> : public ZuTrue { };
template <typename U, typename ...Ts>
struct ZuTypeIn<U, U, Ts...> : public ZuTrue { };
template <typename U, typename T0, typename ...Ts>
struct ZuTypeIn<U, T0, Ts...> : public ZuTypeIn<U, Ts...> { };
template <typename U, typename ...Ts>
struct ZuTypeIn<U, ZuTypeList<Ts...>> : public ZuTypeIn<U, Ts...> { };

// typelist reduce (recursive pair-wise reduction)
// - T0 will be reduced to Reduce<T0>
// - T0, T1 will be reduced to Reduce<T0, T1>
template <template <typename...> class, typename ...>
struct ZuTypeReduce_;
template <template <typename...> class Reduce>
struct ZuTypeReduce_<Reduce> {
  using T = Reduce<>;
};
template <template <typename...> class Reduce, typename T0>
struct ZuTypeReduce_<Reduce, T0> {
  using T = Reduce<T0>;
};
template <template <typename...> class Reduce, typename T0, typename T1>
struct ZuTypeReduce_<Reduce, T0, T1> {
  using T = Reduce<T0, T1>;
};
template <
  template <typename...> class Reduce,
  typename T0, typename T1, typename ...Ts>
struct ZuTypeReduce_<Reduce, T0, T1, Ts...> {
  using T = Reduce<T0, typename ZuTypeReduce_<Reduce, T1, Ts...>::T>;
};
template <template <typename...> class Reduce, typename ...Ts>
struct ZuTypeReduce_<Reduce, ZuTypeList<Ts...>> :
    public ZuTypeReduce_<Reduce, Ts...> { };
template <template <typename...> class Reduce, typename ...Ts>
using ZuTypeReduce = typename ZuTypeReduce_<Reduce, Ts...>::T;

// split typelist left, 0..N-1
template <unsigned N, typename ...Ts> struct ZuTypeHead__;
template <unsigned N, typename T0, typename ...Ts>
struct ZuTypeHead__<N, T0, Ts...> {
  using T = typename ZuTypeHead__<N - 1, Ts...>::T::template Unshift<T0>;
};
template <typename T0, typename ...Ts>
struct ZuTypeHead__<0, T0, Ts...> {
  using T = ZuTypeList<>;
};
template <typename ...Ts>
struct ZuTypeHead__<0, Ts...> {
  using T = ZuTypeList<>;
};
template <unsigned N, typename ...Ts>
struct ZuTypeHead_ :
    public ZuTypeHead__<N, Ts...> { };
template <unsigned N, typename ...Ts>
struct ZuTypeHead_<N, ZuTypeList<Ts...>> :
    public ZuTypeHead__<N, Ts...> { };
template <unsigned N, typename ...Ts>
using ZuTypeHead = typename ZuTypeHead_<N, Ts...>::T;

// split typelist right, N..
template <unsigned N, typename ...Ts> struct ZuTypeTail__;
template <unsigned N, typename T0, typename ...Ts>
struct ZuTypeTail__<N, T0, Ts...> {
  using T = typename ZuTypeTail__<N - 1, Ts...>::T;
};
template <typename T0, typename ...Ts>
struct ZuTypeTail__<0, T0, Ts...> {
  using T = ZuTypeList<T0, Ts...>;
};
template <typename ...Ts>
struct ZuTypeTail__<0, Ts...> {
  using T = ZuTypeList<Ts...>;
};
template <unsigned N, typename ...Ts>
struct ZuTypeTail_ :
    public ZuTypeTail__<N, Ts...> { };
template <unsigned N, typename ...Ts>
struct ZuTypeTail_<N, ZuTypeList<Ts...>> :
    public ZuTypeTail__<N, Ts...> { };
template <unsigned N, typename ...Ts>
using ZuTypeTail = typename ZuTypeTail_<N, Ts...>::T;

// compile-time merge-sort typelist using Index<T>{}
template <template <typename> class Index, typename Left, typename Right>
struct ZuTypeMerge_;
template <template <typename> class, typename, typename, bool>
struct ZuTypeMerge__;
template <
  template <typename> class Index,
  typename LeftT0, typename ...LeftTs,
  typename RightT0, typename ...RightTs>
struct ZuTypeMerge__<Index,
    ZuTypeList<LeftT0, LeftTs...>,
    ZuTypeList<RightT0, RightTs...>, false> {
  using T = typename ZuTypeMerge_<Index,
    ZuTypeList<LeftTs...>,
    ZuTypeList<RightT0, RightTs...>>::T::template Unshift<LeftT0>;
};
template <
  template <typename> class Index,
  typename LeftT0, typename ...LeftTs,
  typename RightT0, typename ...RightTs>
struct ZuTypeMerge__<Index,
    ZuTypeList<LeftT0, LeftTs...>,
    ZuTypeList<RightT0, RightTs...>, true> {
  using T = typename ZuTypeMerge_<Index,
    ZuTypeList<LeftT0, LeftTs...>,
    ZuTypeList<RightTs...>>::T::template Unshift<RightT0>;
};
template <
  template <typename> class Index,
  typename LeftT0, typename ...LeftTs,
  typename RightT0, typename ...RightTs>
struct ZuTypeMerge_<Index,
  ZuTypeList<LeftT0, LeftTs...>,
  ZuTypeList<RightT0, RightTs...>> :
    public ZuTypeMerge__<Index,
      ZuTypeList<LeftT0, LeftTs...>,
      ZuTypeList<RightT0, RightTs...>,
      (Index<LeftT0>{}() > Index<RightT0>{}())> { };
template <template <typename> class Index, typename ...Ts>
struct ZuTypeMerge_<Index, ZuTypeList<>, ZuTypeList<Ts...>> {
  using T = ZuTypeList<Ts...>;
};
template <template <typename> class Index, typename ...Ts>
struct ZuTypeMerge_<Index, ZuTypeList<Ts...>, ZuTypeList<>> {
  using T = ZuTypeList<Ts...>;
};
template <template <typename> class Index>
struct ZuTypeMerge_<Index, ZuTypeList<>, ZuTypeList<>> {
  using T = ZuTypeList<>;
};
template <template <typename> class Index, typename Left, typename Right>
using ZuTypeMerge = typename ZuTypeMerge_<Index, Left, Right>::T;
template <template <typename> class Index, typename ...Ts>
struct ZuTypeSort_ {
  enum { N = sizeof...(Ts) };
  using T = ZuTypeMerge<Index,
    typename ZuTypeSort_<Index, ZuTypeHead<(N>>1), Ts...>>::T,
    typename ZuTypeSort_<Index, ZuTypeTail<(N>>1), Ts...>>::T
  >;
};
template <template <typename> class Index, typename T0>
struct ZuTypeSort_<Index, T0> {
  using T = ZuTypeList<T0>;
};
template <template <typename> class Index>
struct ZuTypeSort_<Index> {
  using T = ZuTypeList<>;
};
template <template <typename> class Index, typename ...Ts>
struct ZuTypeSort_<Index, ZuTypeList<Ts...>> :
  public ZuTypeSort_<Index, Ts...> { };
template <template <typename> class Index, typename ...Ts>
using ZuTypeSort = typename ZuTypeSort_<Index, Ts...>::T;

// compile-time typelist slice
template <
  unsigned Stride, unsigned Offset, typename Ts,
  bool = (Offset < Stride && Offset < Ts::N)>
struct ZuTypeSlice__ {
  using T = typename ZuTypeSlice__<
    Stride, Offset, ZuTypeTail<Stride, Ts>>::T::
      template Unshift<ZuType<Offset, Ts>>;
};
template <unsigned Stride, unsigned Offset, typename Ts>
struct ZuTypeSlice__<Stride, Offset, Ts, false> {
  using T = ZuTypeList<>;
};
template <unsigned Stride, unsigned Offset, typename ...Ts>
struct ZuTypeSlice_ :
  public ZuTypeSlice__<Stride, Offset, ZuTypeList<Ts...>> { };
template <unsigned Stride, unsigned Offset, typename ...Ts>
struct ZuTypeSlice_<Stride, Offset, ZuTypeList<Ts...>> :
  public ZuTypeSlice__<Stride, Offset, ZuTypeList<Ts...>> { };
template <unsigned Stride, unsigned Offset, typename ...Ts>
using ZuTypeSlice = typename ZuTypeSlice_<Stride, Offset, Ts...>::T;

// apply typelist to template
template <template <typename...> class Type, typename ...Ts>
struct ZuTypeApply_ { using T = Type<Ts...>; };
template <template <typename...> class Type, typename ...Ts>
struct ZuTypeApply_<Type, ZuTypeList<Ts...>> :
  public ZuTypeApply_<Type, Ts...> { };
template <template <typename...> class Type, typename ...Ts>
using ZuTypeApply = typename ZuTypeApply_<Type, Ts...>::T;

// type list convertibility
template <typename Ts, typename Us> // true if Ts converts to Us
struct ZuTLConverts : public ZuFalse { };
template <typename, typename, unsigned, bool>
struct ZuTLConverts_ : public ZuFalse { };
template <>
struct ZuTLConverts_<ZuTypeList<>, ZuTypeList<>, 0, true> : public ZuTrue { };
template <typename T0, typename ...Ts, typename U0, typename ...Us, unsigned N>
struct ZuTLConverts_<ZuTypeList<T0, Ts...>, ZuTypeList<U0, Us...>, N, true> :
  public ZuTLConverts_<
    ZuTypeList<Ts...>, ZuTypeList<Us...>, N - 1,
    ZuIsConvertible<T0, U0>{}> { };
template <typename ...Ts, typename ...Us>
struct ZuTLConverts<ZuTypeList<Ts...>, ZuTypeList<Us...>> :
  public ZuTLConverts_<
    ZuTypeList<Ts...>, ZuTypeList<Us...>,
    sizeof...(Us), sizeof...(Ts) == sizeof...(Us)> { };

// type list constructibility
template <typename Ts, typename Us> // true if Us can be constructed from Ts
struct ZuTLConstructs : public ZuFalse { };
template <typename, typename, unsigned, bool>
struct ZuTLConstructs_ : public ZuFalse { };
template <>
struct ZuTLConstructs_<ZuTypeList<>, ZuTypeList<>, 0, true> : public ZuTrue { };
template <typename T0, typename ...Ts, typename U0, typename ...Us, unsigned N>
struct ZuTLConstructs_<ZuTypeList<T0, Ts...>, ZuTypeList<U0, Us...>, N, true> :
  public ZuTLConstructs_<
    ZuTypeList<Ts...>, ZuTypeList<Us...>, N - 1,
    ZuIsConstructible<T0, U0>{}> { };
template <typename ...Ts, typename ...Us>
struct ZuTLConstructs<ZuTypeList<Ts...>, ZuTypeList<Us...>> :
  public ZuTLConstructs_<
    ZuTypeList<Ts...>, ZuTypeList<Us...>,
    sizeof...(Us), sizeof...(Ts) == sizeof...(Us)> { };

#endif /* ZuTL_HH */
