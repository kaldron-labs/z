//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef zfbmapfixture_HH
#define zfbmapfixture_HH

#include <zlib/ZmHash.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmRBTree.hh>
#include <zlib/ZmRef.hh>
#include <zlib/ZuPtr.hh>
#include <zlib/ZtString.hh>
#include <zlib/Zfb.hh>

namespace MapFixture {

template <typename Heap = ZuVoid>
struct Ints_ : public Heap,
  public ZmHashKV<int, int, ZmHashHeapID<"zfbmapfixture.Ints">> { };
using IntsHeap = ZmHeap<"zfbmapfixture.Ints.Obj", Ints_<>>;
ZuDerive(Ints, Ints_<IntsHeap>);
Zfb::AsMap<"exchange.Ints", ZfFieldTC::Int32, ZfFieldTC::Int32>
  Zfb_Fmt(Ints *);

ZuDerive(Texts, (ZmRBTreeKV<ZtString<>, ZtString<>,
  ZmRBTreeHeapID<"zfbmapfixture.Texts">>));
Zfb::AsMap<"exchange.Texts", ZfFieldTC::String, ZfFieldTC::String>
  Zfb_Fmt(Texts *);

template <typename Heap = ZuVoid>
struct Child_ : public Heap {
  int value;
  explicit Child_(int v = 0) : value{v} { }
};
using ChildHeap = ZmHeap<"zfbmapfixture.Child", Child_<>>;
ZuDerive(Child, Child_<ChildHeap>);
ZfStruct(, (Child, FB), (value, (Ctor<0>, Deflt<0>), Int32));
Zfb::AsTable<"exchange.Child"> Zfb_Fmt(Child *);

ZuDerive(Children, (ZmRBTreeKV<unsigned, ZuPtr<Child>,
  ZmRBTreeHeapID<"zfbmapfixture.Children">>));
Zfb::AsMap<"exchange.Children", ZfFieldTC::UInt32, ZfFieldTC::UDT>
  Zfb_Fmt(Children *);
ZuDerive(Nested, (ZmRBTreeKV<int64_t, Children,
  ZmRBTreeHeapID<"zfbmapfixture.Nested">>));
Zfb::AsMap<"exchange.Nested", ZfFieldTC::Int64, ZfFieldTC::UDT>
  Zfb_Fmt(Nested *);

struct Row { ZmRef<Ints> ints; Texts texts; Nested nested; };
ZfStruct(, (Row, FB),
  (ints, (Mutable), UDT), (texts, (Mutable), UDT), (nested, (Mutable), UDT));
Zfb::AsTable<"exchange.Row"> Zfb_Fmt(Row *);

inline bool valid(const Row &row, int value)
{
  auto outer = row.nested.find(12);
  if (!row.ints || row.ints->findVal(7) != value ||
      row.texts.findVal("a") != "first" || !outer) return false;
  auto child = outer->val().find(9);
  return child && child->val() && child->val()->value == value + 1;
}

} // MapFixture

#endif // zfbmapfixture_HH
