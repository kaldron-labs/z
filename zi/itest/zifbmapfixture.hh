//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef zifbmapfixture_HH
#define zifbmapfixture_HH

#include <zlib/ZmHash.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmRBTree.hh>
#include <zlib/ZmRef.hh>
#include <zlib/ZuPtr.hh>
#include <zlib/ZtString.hh>
#include <zlib/ZiFB.hh>

namespace MapFixture {

template <typename Heap = ZuVoid>
struct Ints_ : public Heap,
  public ZmHashKV<int, int, ZmHashHeapID<"zifbmapfixture.Ints">> { };
using IntsHeap = ZmHeap<"zifbmapfixture.Ints.Obj", Ints_<>>;
ZuDerive(Ints, Ints_<IntsHeap>);
ZiFB::AsMap<"exchange.Ints", ZfFieldTC::Int32, ZfFieldTC::Int32>
  ZiFB_Fmt(Ints *);

ZuDerive(Texts, (ZmRBTreeKV<ZtString<>, ZtString<>,
  ZmRBTreeHeapID<"zifbmapfixture.Texts">>));
ZiFB::AsMap<"exchange.Texts", ZfFieldTC::String, ZfFieldTC::String>
  ZiFB_Fmt(Texts *);

template <typename Heap = ZuVoid>
struct Child_ : public Heap {
  int value;
  explicit Child_(int v = 0) : value{v} { }
};
using ChildHeap = ZmHeap<"zifbmapfixture.Child", Child_<>>;
ZuDerive(Child, Child_<ChildHeap>);
ZfStruct(, (Child, FB), (value, (Ctor<0>, Deflt<0>), Int32));
ZiFB::AsTable<"exchange.Child"> ZiFB_Fmt(Child *);

ZuDerive(Children, (ZmRBTreeKV<unsigned, ZuPtr<Child>,
  ZmRBTreeHeapID<"zifbmapfixture.Children">>));
ZiFB::AsMap<"exchange.Children", ZfFieldTC::UInt32, ZfFieldTC::UDT>
  ZiFB_Fmt(Children *);
ZuDerive(Nested, (ZmRBTreeKV<int64_t, Children,
  ZmRBTreeHeapID<"zifbmapfixture.Nested">>));
ZiFB::AsMap<"exchange.Nested", ZfFieldTC::Int64, ZfFieldTC::UDT>
  ZiFB_Fmt(Nested *);

struct Row { ZmRef<Ints> ints; Texts texts; Nested nested; };
ZfStruct(, (Row, FB),
  (ints, (Mutable), UDT), (texts, (Mutable), UDT), (nested, (Mutable), UDT));
ZiFB::AsTable<"exchange.Row"> ZiFB_Fmt(Row *);

inline bool valid(const Row &row, int value)
{
  auto outer = row.nested.find(12);
  if (!row.ints || row.ints->findVal(7) != value ||
      row.texts.findVal("a") != "first" || !outer) return false;
  auto child = outer->val().find(9);
  return child && child->val() && child->val()->value == value + 1;
}

} // MapFixture

#endif // zifbmapfixture_HH
