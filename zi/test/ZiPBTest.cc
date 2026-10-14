//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdint.h>
#include <string.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuDerive.hh>
#include <zlib/ZuPtr.hh>
#include <zlib/ZuRef.hh>

#include <zlib/ZmRef.hh>
#include <zlib/ZmObject.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmHash.hh>
#include <zlib/ZmRBTree.hh>

#include <zlib/ZtArray.hh>
#include <zlib/ZtString.hh>

#include <zlib/ZiPB.hh>

using namespace ZuTestUtil;

ZuDerive(Output, (ZtArray<uint8_t, ZtArrayHeapID<"ZiPBTest.Output">>));

ZuStructFacet(PrefixedPB);
ZiPBConfig(PrefixedPB, ZiPB_SizePrefixed<>);
ZuStructFacet(InheritedPB);
ZiPBConfig(InheritedPB, (ZiPB::Config<ZuFacet::PrefixedPB>));
ZuStructFacet(RawChildPB);
ZiPBConfig(RawChildPB,
  (ZiPB_SizePrefixed_<false, ZiPB::Config<ZuFacet::PrefixedPB>>));
ZuStructFacet(WirePB);
ZuAssert(!ZiPB::Config<ZuFacet::PB>::SizePrefixed);
ZuAssert(ZiPB::Config<ZuFacet::PrefixedPB>::SizePrefixed);
ZuAssert(ZiPB::Config<ZuFacet::InheritedPB>::SizePrefixed);
ZuAssert(!ZiPB::Config<ZuFacet::RawChildPB>::SizePrefixed);

struct Row {
  uint32_t id;
  ZuCSpan name;
  bool enabled;
};
ZfStruct(, (Row, PB),
  (id, (PB::FieldID<1>, Mutable),	UInt32),
  (name, (PB::FieldID<2>),		String),
  (enabled, (PB::FieldID<3>),		Bool));
ZiPB::AsMessage<"test.Row"> ZiPB_Fmt(Row *);
ZuStructReflect(Row, ZuFacet::PrefixedPB, ZuFacet::PB);
ZuStructReflect(Row, ZuFacet::InheritedPB, ZuFacet::PB);
ZuStructReflect(Row, ZuFacet::RawChildPB, ZuFacet::PB);
ZfStructRender(, Row, WirePB, (name, PB::ID<"label">), id);

using RowHandler = ZiPB::As<Row>::Handler<Row>;
using WireHandler = ZiPB::As<Row>::Handler<Row, ZuFacet::WirePB>;
ZuAssert(RowHandler::number<ZfField(Row, id)>() == 1);
ZuAssert(RowHandler::number<ZfField(Row, enabled)>() == 3);
ZuAssert(WireHandler::number<ZfField(Row_WirePB, name)>() == 2);
ZuAssert(WireHandler::number<ZfField(Row_WirePB, id)>() == 1);

struct ImplicitRow { uint32_t first = 0, last = 0; };
ZfStruct(, (ImplicitRow, PB),
  (first,,			UInt32),
  ((retired, Skip, int),,	Int32),
  (last,,			UInt32));
ZiPB::AsMessage<"test.ImplicitRow"> ZiPB_Fmt(ImplicitRow *);
ZfStructRender(, ImplicitRow, WirePB, last, first);
using ImplicitHandler = ZiPB::As<ImplicitRow>::Handler<ImplicitRow>;
using ImplicitWireHandler = ZiPB::As<ImplicitRow>::Handler<ImplicitRow,
  ZuFacet::WirePB>;
ZuAssert(ImplicitHandler::number<ZfField(ImplicitRow, first)>() == 1);
ZuAssert(ImplicitHandler::number<ZfField(ImplicitRow, retired)>() == 2);
ZuAssert(ImplicitHandler::number<ZfField(ImplicitRow, last)>() == 3);
ZuAssert(ImplicitWireHandler::number<ZfField(ImplicitRow_WirePB, last)>() == 3);
ZuAssert(ImplicitWireHandler::number<ZfField(ImplicitRow_WirePB, first)>() == 1);

struct ExplicitSkipRow { uint32_t first = 0, last = 0; };
ZfStruct(, (ExplicitSkipRow, PB),
  (first, (PB::FieldID<4>),			UInt32),
  ((retired, Skip, int), (PB::FieldID<8>),	Int32),
  (last, (PB::FieldID<9>),			UInt32));
ZiPB::AsMessage<"test.ExplicitSkipRow"> ZiPB_Fmt(ExplicitSkipRow *);
using ExplicitSkipHandler = ZiPB::As<ExplicitSkipRow>::Handler<ExplicitSkipRow>;
ZuAssert(ExplicitSkipHandler::number<ZfField(ExplicitSkipRow, retired)>() == 8);

// Compile each ZIPB_BAD_* branch separately; each must fail to compile.
#if defined(ZIPB_BAD_MIXED)
struct Bad { uint32_t first, last; };
ZfStruct(, (Bad, PB),
  (first, (PB::FieldID<1>), UInt32), (last,, UInt32));
ZiPB::AsMessage<"test.Bad"> ZiPB_Fmt(Bad *);
ZuAssert(sizeof(ZiPB::As<Bad>::Handler<Bad>) > 0);
#elif defined(ZIPB_BAD_SKIP)
struct Bad { uint32_t first, last; };
ZfStruct(, (Bad, PB),
  (first, (PB::FieldID<1>),	UInt32),
  ((retired, Skip, int),,	Int32),
  (last, (PB::FieldID<3>),	UInt32));
ZiPB::AsMessage<"test.Bad"> ZiPB_Fmt(Bad *);
ZuAssert(sizeof(ZiPB::As<Bad>::Handler<Bad>) > 0);
#elif defined(ZIPB_BAD_DUP)
struct Bad { uint32_t first, last; };
ZfStruct(, (Bad, PB),
  (first, (PB::FieldID<1>),	UInt32),
  (last, (PB::FieldID<1>),	UInt32));
ZiPB::AsMessage<"test.Bad"> ZiPB_Fmt(Bad *);
ZuAssert(sizeof(ZiPB::As<Bad>::Handler<Bad>) > 0);
#elif defined(ZIPB_BAD_RESERVED)
struct Bad { uint32_t value; };
ZfStruct(, (Bad, PB), (value, (PB::FieldID<19000>), UInt32));
ZiPB::AsMessage<"test.Bad"> ZiPB_Fmt(Bad *);
using BadHandler = ZiPB::As<Bad>::Handler<Bad>;
ZuAssert(BadHandler::number<ZfField(Bad, value)>() == 19000);
#elif defined(ZIPB_BAD_ALT_COLLISION)
struct BadAlt { uint32_t value; };
ZfStruct(, (BadAlt, PB), (value, (PB::FieldID<1>), UInt32));
ZiPB::AsMessage<"test.BadAlt"> ZiPB_Fmt(BadAlt *);
ZuDerive(BadUnion, (ZuUnion<void, BadAlt>));
ZiPB::AsUnion<"test.Bad.choice", ZiPB::Alt<"alt", 4, BadAlt>>
  ZiPB_Fmt(BadUnion *);
struct Bad { BadUnion choice; uint32_t id; };
ZfStruct(, (Bad, PB),
  (choice, (Mutable), UDT), (id, (PB::FieldID<4>), UInt32));
ZiPB::AsMessage<"test.Bad"> ZiPB_Fmt(Bad *);
ZuAssert(sizeof(ZiPB::As<Bad>::Handler<Bad>) > 0);
#elif defined(ZIPB_BAD_ALT_DUP)
struct BadAlt { uint32_t value; };
ZfStruct(, (BadAlt, PB), (value, (PB::FieldID<1>), UInt32));
ZiPB::AsMessage<"test.BadAlt"> ZiPB_Fmt(BadAlt *);
using BadUnion = ZiPB::AsUnion<"test.Bad",
  ZiPB::Alt<"first", 4, BadAlt>, ZiPB::Alt<"second", 4, BadAlt>>;
ZuAssert(sizeof(BadUnion) > 0);
#elif defined(ZIPB_BAD_ALT_RESERVED)
struct BadAlt { uint32_t value; };
ZfStruct(, (BadAlt, PB), (value, (PB::FieldID<1>), UInt32));
ZiPB::AsMessage<"test.BadAlt"> ZiPB_Fmt(BadAlt *);
using BadUnion = ZiPB::AsUnion<"test.Bad",
  ZiPB::Alt<"alt", 19000, BadAlt>>;
ZuAssert(sizeof(BadUnion) > 0);
#endif

struct RequiredRow { uint32_t id; };
ZfStruct(, (RequiredRow, PB),
  (id, (PB::FieldID<7>, Required), UInt32));
ZiPB::AsMessage<"test.RequiredRow"> ZiPB_Fmt(RequiredRow *);

struct FloatRow { double value; };
ZfStruct(, (FloatRow, PB),
  (value, (PB::FieldID<1>), Float));
ZiPB::AsMessage<"test.FloatRow"> ZiPB_Fmt(FloatRow *);

struct Float32Row { float value; };
ZfStruct(, (Float32Row, PB),
  (value, (PB::FieldID<1>), Float));
ZiPB::AsMessage<"test.Float32Row"> ZiPB_Fmt(Float32Row *);

struct MappedRow {
  int128_t signedWide;
  uint128_t unsignedWide;
  ZuFixed fixed;
  ZuDecimal decimal;
  ZuTime time;
  ZuDateTime dateTime;
};
ZfStruct(, (MappedRow, PB),
  (signedWide, (PB::FieldID<1>),	Int128),
  (unsignedWide, (PB::FieldID<2>),	UInt128),
  (fixed, (PB::FieldID<3>),		Fixed),
  (decimal, (PB::FieldID<4>),		Decimal),
  (time, (PB::FieldID<5>),		Time),
  (dateTime, (PB::FieldID<6>),		DateTime));
ZiPB::AsMessage<"test.MappedRow"> ZiPB_Fmt(MappedRow *);

ZuDerive(SignedWideValues,
  (ZtArray<int128_t, ZtArrayHeapID<"ZiPBTest.SignedWideValues">>));
ZuDerive(UnsignedWideValues,
  (ZtArray<uint128_t, ZtArrayHeapID<"ZiPBTest.UnsignedWideValues">>));
ZuDerive(FixedValues,
  (ZtArray<ZuFixed, ZtArrayHeapID<"ZiPBTest.FixedValues">>));
ZuDerive(DecimalValues,
  (ZtArray<ZuDecimal, ZtArrayHeapID<"ZiPBTest.DecimalValues">>));
ZuDerive(TimeValues,
  (ZtArray<ZuTime, ZtArrayHeapID<"ZiPBTest.TimeValues">>));
ZuDerive(DateTimeValues,
  (ZtArray<ZuDateTime, ZtArrayHeapID<"ZiPBTest.DateTimeValues">>));
struct MappedVecRow {
  SignedWideValues signedWide;
  UnsignedWideValues unsignedWide;
  FixedValues fixed;
  DecimalValues decimal;
  TimeValues time;
  DateTimeValues dateTime;
};
ZfStruct(, (MappedVecRow, PB),
  (signedWide, (PB::FieldID<1>),	Int128Vec),
  (unsignedWide, (PB::FieldID<2>),	UInt128Vec),
  (fixed, (PB::FieldID<3>),		FixedVec),
  (decimal, (PB::FieldID<4>),		DecimalVec),
  (time, (PB::FieldID<5>),		TimeVec),
  (dateTime, (PB::FieldID<6>),		DateTimeVec));
ZiPB::AsMessage<"test.MappedVecRow"> ZiPB_Fmt(MappedVecRow *);

struct WideRow { uint128_t value = 0; };
ZfStruct(, (WideRow, PB), (value, (PB::FieldID<1>, Mutable), UInt128));
ZiPB::AsMessage<"test.WideRow"> ZiPB_Fmt(WideRow *);
struct CtorWideRow {
  uint128_t value;
  explicit CtorWideRow(uint128_t value_) : value{value_} { }
};
ZfStruct(, (CtorWideRow, PB), (value, (PB::FieldID<1>, Ctor<0>), UInt128));
ZiPB::AsMessage<"test.CtorWideRow"> ZiPB_Fmt(CtorWideRow *);

struct WrappedWide { uint128_t bits = 0; };
struct WrappedWideMap {
  using T = ZiPB::Values::UInt128;
  static T save(const WrappedWide &v) { return ZiPB::Values::UInt128Map::save(v.bits); }
  static WrappedWide load(const T &v) {
    return {ZiPB::Values::UInt128Map::load(v)};
  }
};
WrappedWideMap ZiPBTransform(WrappedWide *);
struct WrappedParent { WrappedWide value; };
ZfStruct(, (WrappedParent, PB), (value, (PB::FieldID<1>), UDT));
ZiPB::AsMessage<"test.WrappedParent"> ZiPB_Fmt(WrappedParent *);
struct DecimalParent { ZuDecimal value; };
ZfStruct(, (DecimalParent, PB), (value, (PB::FieldID<1>), UDT));
ZiPB::AsMessage<"test.DecimalParent"> ZiPB_Fmt(DecimalParent *);
struct IPParent { ZiIP value; };
ZfStruct(, (IPParent, PB), (value, (PB::FieldID<1>), UDT));
ZiPB::AsMessage<"test.IPParent"> ZiPB_Fmt(IPParent *);
ZuDerive(IPValues, (ZtArray<ZiIP, ZtArrayHeapID<"ZiPBTest.IPValues">>));
ZiPB::AsArray<"test.IPValues"> ZiPB_Fmt(IPValues *);
struct IPVecRow { IPValues values; };
ZfStruct(, (IPVecRow, PB), (values, (PB::FieldID<1>), UDT));
ZiPB::AsMessage<"test.IPVecRow"> ZiPB_Fmt(IPVecRow *);
struct BitmapRow { ZuBitmap<128> fixed; ZtBitmap dynamic; };
ZfStruct(, (BitmapRow, PB),
  (fixed, (PB::FieldID<1>),	UDT),
  (dynamic, (PB::FieldID<2>),	UDT));
ZiPB::AsMessage<"test.BitmapRow"> ZiPB_Fmt(BitmapRow *);
ZuDerive(BitmapValues, (ZtArray<ZtBitmap,
  ZtArrayHeapID<"ZiPBTest.BitmapValues">>));
ZiPB::AsArray<"test.BitmapValues"> ZiPB_Fmt(BitmapValues *);
struct BitmapVecRow { BitmapValues values; };
ZfStruct(, (BitmapVecRow, PB), (values, (PB::FieldID<1>), UDT));
ZiPB::AsMessage<"test.BitmapVecRow"> ZiPB_Fmt(BitmapVecRow *);
struct HwBitmapRow { ZmBitmap value; };
ZfStruct(, (HwBitmapRow, PB), (value, (PB::FieldID<1>), UDT));
ZiPB::AsMessage<"test.HwBitmapRow"> ZiPB_Fmt(HwBitmapRow *);
ZuDerive(HwBitmapValues, (ZtArray<ZmBitmap,
  ZtArrayHeapID<"ZiPBTest.HwBitmapValues">>));
ZiPB::AsArray<"test.HwBitmapValues"> ZiPB_Fmt(HwBitmapValues *);
struct HwBitmapVecRow { HwBitmapValues values; };
ZfStruct(, (HwBitmapVecRow, PB), (values, (PB::FieldID<1>), UDT));
ZiPB::AsMessage<"test.HwBitmapVecRow"> ZiPB_Fmt(HwBitmapVecRow *);

struct AdapterRow {
  unsigned ro, rw, init = 0, mut = 0, key = 0;
  AdapterRow() = delete;
  AdapterRow(unsigned ro_, unsigned rw_) : ro{ro_}, rw{rw_} { }
};
ZfStruct(, (AdapterRow, PB),
  ((ro, Rd), (Ctor<0>, Keys<0>), UInt32),
  (rw, (Ctor<1>), UInt32),
  (init,, UInt32),
  (mut, (Mutable), UInt32),
  (key, (Keys<0>), UInt32));
ZiPB::AsMessage<"test.AdapterRow"> ZiPB_Fmt(AdapterRow *);

struct MoveChild {
  unsigned id;
  explicit MoveChild(unsigned id_) : id{id_} { }
  MoveChild(MoveChild &&) = default;
  MoveChild &operator =(MoveChild &&) = default;
  MoveChild(const MoveChild &) = delete;
  MoveChild &operator =(const MoveChild &) = delete;
};
ZfStruct(, (MoveChild, PB), (id, (Ctor<0>), UInt32));
ZiPB::AsMessage<"test.MoveChild"> ZiPB_Fmt(MoveChild *);
struct MoveParent {
  MoveChild child;
  explicit MoveParent(MoveChild child_) : child{ZuMv(child_)} { }
};
ZfStruct(, (MoveParent, PB), (child, (Ctor<0>), UDT));
ZiPB::AsMessage<"test.MoveParent"> ZiPB_Fmt(MoveParent *);

struct CtorRow {
  int32_t id;
  ZuCSpan name;
  uint32_t version = 0;
  CtorRow(int32_t id_, ZuCSpan name_) : id{id_}, name{name_} { }
};
ZfStruct(, (CtorRow, PB),
  (id, (PB::FieldID<1>, Ctor<0>),	Int32),
  (name, (PB::FieldID<2>, Ctor<1>),	String),
  (version, (PB::FieldID<3>, Mutable),	UInt32));
ZiPB::AsMessage<"test.CtorRow"> ZiPB_Fmt(CtorRow *);

struct BytesRow { ZuBSpan payload; };
ZfStruct(, (BytesRow, PB),
  (payload, (PB::FieldID<1>), Bytes));
ZiPB::AsMessage<"test.BytesRow"> ZiPB_Fmt(BytesRow *);

struct Child { uint32_t a = 0, b = 0; };
ZfStruct(, (Child, PB),
  (a, (PB::FieldID<1>),	UInt32),
  (b, (PB::FieldID<2>),	UInt32));
ZiPB::AsMessage<"test.Child"> ZiPB_Fmt(Child *);

struct Parent { Child child; };
ZfStruct(, (Parent, PB),
  (child, (PB::FieldID<1>), UDT));
ZiPB::AsMessage<"test.Parent"> ZiPB_Fmt(Parent *);

ZuStructFacet(NestedPB);
ZfStructRender(, Child, NestedPB, b, a);
ZuStructReflect(Parent, ZuFacet::NestedPB, ZuFacet::PB);

struct ParentCtor {
  Child child;
  explicit ParentCtor(Child child_) : child{child_} { }
};
ZfStruct(, (ParentCtor, PB),
  (child, (PB::FieldID<1>, Ctor<0>), UDT));
ZiPB::AsMessage<"test.ParentCtor"> ZiPB_Fmt(ParentCtor *);

struct ReqChild { uint32_t a = 0, b = 0; };
ZfStruct(, (ReqChild, PB),
  (a, (PB::FieldID<1>, Required),	UInt32),
  (b, (PB::FieldID<2>),			UInt32));
ZiPB::AsMessage<"test.ReqChild"> ZiPB_Fmt(ReqChild *);

struct ReqParent { ReqChild child; };
ZfStruct(, (ReqParent, PB),
  (child, (PB::FieldID<1>), UDT));
ZiPB::AsMessage<"test.ReqParent"> ZiPB_Fmt(ReqParent *);

ZuDerive(ChildArray, (ZtArray<Child, ZtArrayHeapID<"ZiPBTest.ChildArray">>));
ZiPB::AsArray<"test.ChildArray"> ZiPB_Fmt(ChildArray *);
struct ArrayParent { ChildArray children; };
ZfStruct(, (ArrayParent, PB),
  (children, (PB::FieldID<1>, Mutable), UDT));
ZuStructReflect(ArrayParent, ZuFacet::NestedPB, ZuFacet::PB);
ZiPB::AsMessage<"test.ArrayParent"> ZiPB_Fmt(ArrayParent *);

struct CtorArrayParent {
  ChildArray children;
  explicit CtorArrayParent(ChildArray children_) : children{ZuMv(children_)} { }
};
ZfStruct(, (CtorArrayParent, PB),
  (children, (PB::FieldID<1>, Ctor<0>), UDT));
ZiPB::AsMessage<"test.CtorArrayParent"> ZiPB_Fmt(CtorArrayParent *);

struct RequiredArrayParent { ChildArray children; };
ZfStruct(, (RequiredArrayParent, PB),
  (children, (PB::FieldID<1>, Required), UDT));
ZiPB::AsMessage<"test.RequiredArrayParent"> ZiPB_Fmt(RequiredArrayParent *);

ZuDerive(ChildPtrArray,
  (ZtArray<ZuPtr<Child>, ZtArrayHeapID<"ZiPBTest.ChildPtrArray">>));
ZiPB::AsArray<"test.ChildPtrArray"> ZiPB_Fmt(ChildPtrArray *);
struct PtrArrayParent { ChildPtrArray children; };
ZfStruct(, (PtrArrayParent, PB),
  (children, (PB::FieldID<1>, Mutable), UDT));
ZiPB::AsMessage<"test.PtrArrayParent"> ZiPB_Fmt(PtrArrayParent *);

ZuDerive(RawChildArray,
  (ZtArray<Child *, ZtArrayHeapID<"ZiPBTest.RawChildArray">>));
ZiPB::AsArray<"test.RawChildArray"> ZiPB_Fmt(RawChildArray *);
struct RawArrayParent {
  RawChildArray children;
  RawArrayParent() = default;
  RawArrayParent(RawArrayParent &&o) : children{ZuMv(o.children)} { }
  ~RawArrayParent() {
    for (unsigned i = 0, n = children.length(); i < n; i++) delete children[i];
  }
};
ZfStruct(, (RawArrayParent, PB),
  (children, (PB::FieldID<1>, Mutable), UDT));
ZiPB::AsMessage<"test.RawArrayParent"> ZiPB_Fmt(RawArrayParent *);

ZuDerive(ReqChildArray,
  (ZtArray<ReqChild, ZtArrayHeapID<"ZiPBTest.ReqChildArray">>));
ZiPB::AsArray<"test.ReqChildArray"> ZiPB_Fmt(ReqChildArray *);
struct ReqArrayParent { ReqChildArray children; };
ZfStruct(, (ReqArrayParent, PB),
  (children, (PB::FieldID<1>), UDT));
ZiPB::AsMessage<"test.ReqArrayParent"> ZiPB_Fmt(ReqArrayParent *);

static unsigned ptrDeletes = 0;
struct PtrChild {
  uint32_t a = 0, b = 0;
  ~PtrChild() { ++ptrDeletes; }
};
ZfStruct(, (PtrChild, PB),
  (a, (PB::FieldID<1>),	UInt32),
  (b, (PB::FieldID<2>),	UInt32));
ZiPB::AsMessage<"test.PtrChild"> ZiPB_Fmt(PtrChild *);

struct PtrValTree : ZmRBTreeKV<int32_t, PtrChild *,
  ZmRBTreeHeapID<"ZiPBTest.PtrValTree">> {
  using Base = ZmRBTreeKV<int32_t, PtrChild *,
    ZmRBTreeHeapID<"ZiPBTest.PtrValTree">>;
  PtrValTree() = default;
  PtrValTree(PtrValTree &&o) : Base{ZuMv(o)} { }
  ~PtrValTree() {
    auto i = citer();
    while (auto node = i()) delete node->val();
  }
};
ZiPB::AsMap<"test.PtrValMap", ZfFieldTC::Int32, ZfFieldTC::UDT>
  ZiPB_Fmt(PtrValTree *);
struct PtrValRow { PtrValTree entries; };
ZfStruct(, (PtrValRow, PB),
  (entries, (PB::FieldID<1>, Mutable), UDT));
ZiPB::AsMessage<"test.PtrValRow"> ZiPB_Fmt(PtrValRow *);

struct PtrParent {
  PtrChild *child = nullptr;
  PtrParent() = default;
  PtrParent(PtrParent &&o) : child{o.child} { o.child = nullptr; }
  ~PtrParent() { delete child; }
};
ZfStruct(, (PtrParent, PB),
  (child, (PB::FieldID<1>, Mutable), UDT));
ZiPB::AsMessage<"test.PtrParent"> ZiPB_Fmt(PtrParent *);

struct ReadOnlyPtrParent {
  PtrChild *child;
  explicit ReadOnlyPtrParent(PtrChild *child_) : child{child_} { }
  ~ReadOnlyPtrParent() { delete child; }
};
ZfStruct(, (ReadOnlyPtrParent, PB),
  ((child, Rd), (Ctor<0>, Keys<0>, Reset), UDT));
ZiPB::AsMessage<"test.ReadOnlyPtrParent"> ZiPB_Fmt(ReadOnlyPtrParent *);

struct RequiredPtrParent { PtrChild *child = nullptr; };
ZfStruct(, (RequiredPtrParent, PB),
  (child, (PB::FieldID<1>, Required), UDT));
ZiPB::AsMessage<"test.RequiredPtrParent"> ZiPB_Fmt(RequiredPtrParent *);

struct PartialRequiredRow { PtrChild *child = nullptr; uint32_t id = 7; };
ZfStruct(, (PartialRequiredRow, PB),
  (child, (Required), UDT), (id,, UInt32));
ZiPB::AsMessage<"test.PartialRequiredRow"> ZiPB_Fmt(PartialRequiredRow *);

struct ResetPtrParent {
  PtrChild *child = nullptr;
  ~ResetPtrParent() { delete child; }
};
ZfStruct(, (ResetPtrParent, PB),
  (child, (PB::FieldID<1>, Mutable, Reset), UDT));
ZiPB::AsMessage<"test.ResetPtrParent"> ZiPB_Fmt(ResetPtrParent *);

struct SmartParent { ZuPtr<PtrChild> child; };
ZfStruct(, (SmartParent, PB),
  (child, (PB::FieldID<1>, Mutable), UDT));
ZiPB::AsMessage<"test.SmartParent"> ZiPB_Fmt(SmartParent *);

struct RefChild : ZmObject { uint32_t a = 0, b = 0; };
ZfStruct(, (RefChild, PB),
  (a, (PB::FieldID<1>),	UInt32),
  (b, (PB::FieldID<2>),	UInt32));
ZiPB::AsMessage<"test.RefChild"> ZiPB_Fmt(RefChild *);

struct RefParent { ZmRef<RefChild> child; };
ZfStruct(, (RefParent, PB),
  (child, (PB::FieldID<1>, Mutable), UDT));
ZiPB::AsMessage<"test.RefParent"> ZiPB_Fmt(RefParent *);

struct LocalRefParent { ZuRef<RefChild> child; };
ZfStruct(, (LocalRefParent, PB),
  (child, (PB::FieldID<1>, Mutable), UDT));
ZiPB::AsMessage<"test.LocalRefParent"> ZiPB_Fmt(LocalRefParent *);

template <typename T>
class ForeignPtr {
public:
  ForeignPtr() = default;
  ForeignPtr(T *value) : m_value{value} { }
  ForeignPtr(ForeignPtr &&o) : m_value{o.m_value} { o.m_value = nullptr; }
  ForeignPtr(const ForeignPtr &) = delete;
  ~ForeignPtr() { delete m_value; }
  ForeignPtr &operator =(ForeignPtr &&o) {
    if (this != &o) {
      delete m_value;
      m_value = o.m_value;
      o.m_value = nullptr;
    }
    return *this;
  }
  ForeignPtr &operator =(T *value) {
    auto old = m_value;
    m_value = value;
    delete old;
    return *this;
  }
  T &operator *() const { return *m_value; }
  T *operator ->() const { return m_value; }
  explicit operator bool() const { return m_value; }
  bool operator !() const { return !m_value; }
  struct Traits : ZuTraits<T *> { enum { IsPrimitive = 0, IsPOD = 0 }; };
  friend Traits ZuTraitsType(ForeignPtr *) { return {}; }
private:
  T *m_value = nullptr;
};

struct ForeignParent { ForeignPtr<PtrChild> child; };
ZfStruct(, (ForeignParent, PB),
  (child, (PB::FieldID<1>, Mutable), UDT));
ZiPB::AsMessage<"test.ForeignParent"> ZiPB_Fmt(ForeignParent *);
ZuDerive(ForeignArray, (ZtArray<ForeignPtr<PtrChild>,
  ZtArrayHeapID<"ZiPBTest.ForeignArray">>));
ZiPB::AsArray<"test.ForeignArray"> ZiPB_Fmt(ForeignArray *);
struct ForeignArrayParent { ForeignArray children; };
ZfStruct(, (ForeignArrayParent, PB),
  (children, (PB::FieldID<1>, Mutable), UDT));
ZiPB::AsMessage<"test.ForeignArrayParent"> ZiPB_Fmt(ForeignArrayParent *);
ZuDerive(ForeignMap, (ZmRBTreeKV<int32_t, ForeignPtr<PtrChild>,
  ZmRBTreeHeapID<"ZiPBTest.ForeignMap">>));
ZiPB::AsMap<"test.ForeignMap", ZfFieldTC::Int32, ZfFieldTC::UDT>
  ZiPB_Fmt(ForeignMap *);
struct ForeignMapParent { ForeignMap entries; };
ZfStruct(, (ForeignMapParent, PB),
  (entries, (PB::FieldID<1>, Mutable), UDT));
ZiPB::AsMessage<"test.ForeignMapParent"> ZiPB_Fmt(ForeignMapParent *);

struct CtorPtrParent {
  PtrChild *child;
  explicit CtorPtrParent(PtrChild *child_) : child{child_} { }
  CtorPtrParent(CtorPtrParent &&o) : child{o.child} { o.child = nullptr; }
  ~CtorPtrParent() { delete child; }
};
ZfStruct(, (CtorPtrParent, PB),
  (child, (PB::FieldID<1>, Ctor<0>), UDT));
ZiPB::AsMessage<"test.CtorPtrParent"> ZiPB_Fmt(CtorPtrParent *);

struct CtorSmartParent {
  ZuPtr<PtrChild> child;
  explicit CtorSmartParent(ZuPtr<PtrChild> child_) : child{ZuMv(child_)} { }
};
ZfStruct(, (CtorSmartParent, PB),
  (child, (PB::FieldID<1>, Ctor<0>), UDT));
ZiPB::AsMessage<"test.CtorSmartParent"> ZiPB_Fmt(CtorSmartParent *);

struct RecursiveNode {
  uint32_t value = 0;
  ZuPtr<RecursiveNode> next;
};
ZfStruct(, (RecursiveNode, PB),
  (value, (PB::FieldID<1>),	UInt32),
  (next, (PB::FieldID<2>),	UDT));
ZiPB::AsMessage<"test.RecursiveNode"> ZiPB_Fmt(RecursiveNode *);

struct EmptyMessage { };
ZfStruct(, (EmptyMessage, PB));
ZiPB::AsMessage<"test.EmptyMessage"> ZiPB_Fmt(EmptyMessage *);

struct ChoiceA { uint32_t a = 0, b = 0; };
ZfStruct(, (ChoiceA, PB),
  (a, (PB::FieldID<1>, Mutable),	UInt32),
  (b, (PB::FieldID<2>, Mutable),	UInt32));
ZfStructRender(, ChoiceA, NestedPB, b);
ZiPB::AsMessage<"test.ChoiceA"> ZiPB_Fmt(ChoiceA *);
struct ChoiceB { uint32_t value = 0; };
ZfStruct(, (ChoiceB, PB),
  (value, (PB::FieldID<1>, Mutable), UInt32));
ZuStructReflect(ChoiceB, ZuFacet::NestedPB, ZuFacet::PB);
ZiPB::AsMessage<"test.ChoiceB"> ZiPB_Fmt(ChoiceB *);
struct ChoicePtr {
  ForeignPtr<PtrChild> child;
  ZtArray<uint32_t, ZtArrayHeapID<"ZiPBTest.ChoiceVals">> values;
};
ZfStruct(, (ChoicePtr, PB),
  (child,	(PB::FieldID<1>, Mutable),	UDT),
  (values,	(PB::FieldID<2>, Mutable),	UInt32Vec));
ZiPB::AsMessage<"test.ChoicePtr"> ZiPB_Fmt(ChoicePtr *);
ZuDerive(PtrChoice, (ZuUnion<void, ChoicePtr, ChoiceB>));
ZiPB::AsUnion<"test.PtrChoice", ZiPB::Alt<"owned", 4, ChoicePtr>,
  ZiPB::Alt<"other", 9, ChoiceB>> ZiPB_Fmt(PtrChoice *);
struct PtrChoiceRow { PtrChoice choice; };
ZfStruct(, (PtrChoiceRow, PB), (choice, (Mutable), UDT));
ZiPB::AsMessage<"test.PtrChoiceRow"> ZiPB_Fmt(PtrChoiceRow *);
struct CtorChoiceAlt {
  uint32_t id;
  uint32_t value = 0;
  CtorChoiceAlt() : id{0} { }
  explicit CtorChoiceAlt(uint32_t id_) : id{id_} { }
};
ZfStruct(, (CtorChoiceAlt, PB),
  ((id, Rd), (PB::FieldID<1>, Ctor<0>),	UInt32),
  (value, (PB::FieldID<2>, Mutable),	UInt32));
ZiPB::AsMessage<"test.CtorChoiceAlt"> ZiPB_Fmt(CtorChoiceAlt *);
struct WrappedCtor { uint32_t id = 0, value = 0; };
static unsigned mappedLoads = 0;
struct WrappedCtorMap {
  using T = CtorChoiceAlt;
  static T save(const WrappedCtor &v) {
    T wire{v.id};
    wire.value = v.value;
    return wire;
  }
  static WrappedCtor load(const T &wire) { return {wire.id, wire.value}; }
};
WrappedCtorMap ZiPBTransform(WrappedCtor *);
struct WrappedCtorParent { WrappedCtor child; };
ZfStruct(, (WrappedCtorParent, PB), (child, (Mutable), UDT));
ZiPB::AsMessage<"test.WrappedCtorParent"> ZiPB_Fmt(WrappedCtorParent *);
struct CtorChildParent { ForeignPtr<CtorChoiceAlt> child; };
ZfStruct(, (CtorChildParent, PB), (child, (Mutable), UDT));
ZiPB::AsMessage<"test.CtorChildParent"> ZiPB_Fmt(CtorChildParent *);
struct CtorChildResetParent { ForeignPtr<CtorChoiceAlt> child; };
ZfStruct(, (CtorChildResetParent, PB),
  (child, (Mutable, Reset), UDT));
ZiPB::AsMessage<"test.CtorChildResetParent">
  ZiPB_Fmt(CtorChildResetParent *);
struct CtorChildValueParent { CtorChoiceAlt child; };
ZfStruct(, (CtorChildValueParent, PB), (child, (Mutable), UDT));
ZiPB::AsMessage<"test.CtorChildValueParent">
  ZiPB_Fmt(CtorChildValueParent *);
struct CtorChildCtorParent {
  ForeignPtr<CtorChoiceAlt> child;
  explicit CtorChildCtorParent(ForeignPtr<CtorChoiceAlt> child_) :
    child{ZuMv(child_)} { }
};
ZfStruct(, (CtorChildCtorParent, PB), (child, (Ctor<0>), UDT));
ZiPB::AsMessage<"test.CtorChildCtorParent"> ZiPB_Fmt(CtorChildCtorParent *);
ZuDerive(CtorChoice, (ZuUnion<void, CtorChoiceAlt, ChoiceB>));
ZiPB::AsUnion<"test.CtorChoice", ZiPB::Alt<"ctor", 4, CtorChoiceAlt>,
  ZiPB::Alt<"other", 9, ChoiceB>> ZiPB_Fmt(CtorChoice *);
struct CtorChoiceParent { CtorChoice choice; };
ZfStruct(, (CtorChoiceParent, PB), (choice, (Mutable), UDT));
ZiPB::AsMessage<"test.CtorChoiceParent"> ZiPB_Fmt(CtorChoiceParent *);
struct CtorChoiceCtorParent {
  CtorChoice choice;
  explicit CtorChoiceCtorParent(CtorChoice choice_) :
    choice{ZuMv(choice_)} { }
};
ZfStruct(, (CtorChoiceCtorParent, PB), (choice, (Ctor<0>), UDT));
ZiPB::AsMessage<"test.CtorChoiceCtorParent">
  ZiPB_Fmt(CtorChoiceCtorParent *);
ZuDerive(Choice, (ZuUnion<void, ChoiceA, ChoiceA, ChoiceB>));
ZiPB::AsUnion<"test.Choice", ZiPB::Alt<"first", 4, ChoiceA>,
  ZiPB::Alt<"second", 6, ChoiceA>,
  ZiPB::Alt<"other", 9, ChoiceB>> ZiPB_Fmt(Choice *);
struct ChoiceRow { uint32_t id = 0; Choice choice; };
ZfStruct(, (ChoiceRow, PB),
  (id, (PB::FieldID<1>),	UInt32),
  (choice, (Mutable),		UDT));
ZuStructReflect(ChoiceRow, ZuFacet::NestedPB, ZuFacet::PB);
ZiPB::AsMessage<"test.ChoiceRow"> ZiPB_Fmt(ChoiceRow *);
struct RequiredChoiceRow { Choice choice; };
ZfStruct(, (RequiredChoiceRow, PB),
  (choice, (Required, Mutable), UDT));
ZiPB::AsMessage<"test.RequiredChoiceRow"> ZiPB_Fmt(RequiredChoiceRow *);
struct RequiredAlt { uint32_t value = 0; };
ZfStruct(, (RequiredAlt, PB),
  (value, (Required), UInt32));
ZiPB::AsMessage<"test.RequiredAlt"> ZiPB_Fmt(RequiredAlt *);
ZuDerive(DiscardedRequiredChoice, (ZuUnion<void, RequiredAlt, ChoiceB>));
ZiPB::AsUnion<"test.DiscardedRequiredChoice",
  ZiPB::Alt<"required", 4, RequiredAlt>,
  ZiPB::Alt<"final", 9, ChoiceB>> ZiPB_Fmt(DiscardedRequiredChoice *);
struct DiscardedRequiredRow { DiscardedRequiredChoice choice; };
ZfStruct(, (DiscardedRequiredRow, PB), (choice,, UDT));
ZiPB::AsMessage<"test.DiscardedRequiredRow"> ZiPB_Fmt(DiscardedRequiredRow *);
struct CtorChoiceRow {
  Choice choice;
  explicit CtorChoiceRow(Choice choice_) : choice{ZuMv(choice_)} { }
};
ZfStruct(, (CtorChoiceRow, PB),
  (choice, (Ctor<0>), UDT));
ZiPB::AsMessage<"test.CtorChoiceRow"> ZiPB_Fmt(CtorChoiceRow *);

ZtEnumNS(, PBColor, int32_t, Red, Green, Blue);
ZtEnumImplNS(PBColor);
struct EnumRow { int32_t color = PBColor::Red; };
ZfStruct(, (EnumRow, PB),
  (color, (PB::FieldID<1>, Enum<PBColor::Map>), Int32));
ZiPB::AsMessage<"test.EnumRow"> ZiPB_Fmt(EnumRow *);

ZuDerive(IntMapTree, (ZmRBTreeKV<int32_t, int32_t,
  ZmRBTreeHeapID<"ZiPBTest.IntMapTree">>));
ZiPB::AsMap<"test.IntMap", ZfFieldTC::Int32, ZfFieldTC::Int32>
  ZiPB_Fmt(IntMapTree *);
struct MapRow { IntMapTree entries; };
ZfStruct(, (MapRow, PB),
  (entries, (PB::FieldID<1>, Mutable), UDT));
ZiPB::AsMessage<"test.MapRow"> ZiPB_Fmt(MapRow *);

struct CtorMapRow {
  IntMapTree entries;
  explicit CtorMapRow(IntMapTree entries_) : entries{ZuMv(entries_)} { }
};
ZfStruct(, (CtorMapRow, PB),
  (entries, (PB::FieldID<1>, Ctor<0>), UDT));
ZiPB::AsMessage<"test.CtorMapRow"> ZiPB_Fmt(CtorMapRow *);

ZuDerive(ChildMapTree, (ZmRBTreeKV<int32_t, Child,
  ZmRBTreeHeapID<"ZiPBTest.ChildMapTree">>));
ZiPB::AsMap<"test.ChildMap", ZfFieldTC::Int32, ZfFieldTC::UDT>
  ZiPB_Fmt(ChildMapTree *);
struct ChildMapRow { ChildMapTree entries; };
ZfStruct(, (ChildMapRow, PB),
  (entries, (PB::FieldID<1>, Mutable), UDT));
ZiPB::AsMessage<"test.ChildMapRow"> ZiPB_Fmt(ChildMapRow *);
ZuStructReflect(ChildMapRow, ZuFacet::NestedPB, ZuFacet::PB);

struct IntMapHash_ :
  ZmHashKV<int32_t, int32_t, ZmHashHeapID<"ZiPBTest.IntMapHash.Node">> { };
using IntMapHashHeap = ZmHeap<"ZiPBTest.IntMapHash.Obj", IntMapHash_>;
struct IntMapHash : IntMapHashHeap, IntMapHash_ { };
ZiPB::AsMap<"test.IntMap", ZfFieldTC::Int32, ZfFieldTC::Int32>
  ZiPB_Fmt(IntMapHash *);
struct HashMapRow { ZmRef<IntMapHash> entries; };
ZfStruct(, (HashMapRow, PB),
  (entries, (PB::FieldID<1>, Mutable), UDT));
ZiPB::AsMessage<"test.HashMapRow"> ZiPB_Fmt(HashMapRow *);

using MapText = ZtString<ZtStringHeapID<"ZiPBTest.MapText">>;
ZuDerive(TextMapTree, (ZmRBTreeKV<MapText, MapText,
  ZmRBTreeHeapID<"ZiPBTest.TextMapTree">>));
ZiPB::AsMap<"test.TextMap", ZfFieldTC::String, ZfFieldTC::String>
  ZiPB_Fmt(TextMapTree *);
struct TextMapRow { TextMapTree entries; };
ZfStruct(, (TextMapRow, PB),
  (entries, (PB::FieldID<1>), UDT));
ZiPB::AsMessage<"test.TextMapRow"> ZiPB_Fmt(TextMapRow *);
ZuDerive(BytesMapVal, (ZtArray<uint8_t,
  ZtArrayHeapID<"ZiPBTest.BytesMapVal">>));
ZuDerive(BytesMapTree, (ZmRBTreeKV<uint32_t, BytesMapVal,
  ZmRBTreeHeapID<"ZiPBTest.BytesMapTree">>));
ZiPB::AsMap<"test.BytesMap", ZfFieldTC::UInt32, ZfFieldTC::Bytes>
  ZiPB_Fmt(BytesMapTree *);
struct BytesMapRow { BytesMapTree entries; };
ZfStruct(, (BytesMapRow, PB),
  (entries, (PB::FieldID<1>), UDT));
ZiPB::AsMessage<"test.BytesMapRow"> ZiPB_Fmt(BytesMapRow *);

ZuDerive(DecimalMapTree, (ZmRBTreeKV<uint32_t, ZuDecimal,
  ZmRBTreeHeapID<"ZiPBTest.DecimalMapTree">>));
ZiPB::AsMap<"test.DecimalMap", ZfFieldTC::UInt32, ZfFieldTC::Decimal>
  ZiPB_Fmt(DecimalMapTree *);
struct DecimalMapRow { DecimalMapTree entries; };
ZfStruct(, (DecimalMapRow, PB),
  (entries, (PB::FieldID<1>), UDT));
ZiPB::AsMessage<"test.DecimalMapRow"> ZiPB_Fmt(DecimalMapRow *);

ZuDerive(WrappedMapTree, (ZmRBTreeKV<uint32_t, WrappedWide,
  ZmRBTreeHeapID<"ZiPBTest.WrappedMapTree">>));
ZiPB::AsMap<"test.WrappedMap", ZfFieldTC::UInt32, ZfFieldTC::UDT>
  ZiPB_Fmt(WrappedMapTree *);
struct WrappedMapRow { WrappedMapTree entries; };
ZfStruct(, (WrappedMapRow, PB),
  (entries, (PB::FieldID<1>), UDT));
ZiPB::AsMessage<"test.WrappedMapRow"> ZiPB_Fmt(WrappedMapRow *);

ZuDerive(IntValues, (ZtArray<int32_t, ZtArrayHeapID<"ZiPBTest.IntValues">>));
struct IntVecRow { IntValues values; };
ZfStruct(, (IntVecRow, PB),
  (values, (PB::FieldID<1>), Int32Vec));
ZiPB::AsMessage<"test.IntVecRow"> ZiPB_Fmt(IntVecRow *);

ZuDerive(FloatValues, (ZtArray<double, ZtArrayHeapID<"ZiPBTest.FloatValues">>));
struct FloatVecRow { FloatValues values; };
ZfStruct(, (FloatVecRow, PB),
  (values, (PB::FieldID<1>), FloatVec));
ZiPB::AsMessage<"test.FloatVecRow"> ZiPB_Fmt(FloatVecRow *);

ZuDerive(Float32Values,
  (ZtArray<float, ZtArrayHeapID<"ZiPBTest.Float32Values">>));
struct Float32VecRow { Float32Values values; };
ZfStruct(, (Float32VecRow, PB),
  (values, (PB::FieldID<1>), FloatVec));
ZiPB::AsMessage<"test.Float32VecRow"> ZiPB_Fmt(Float32VecRow *);

ZuDerive(SmallInts, (ZtArray<int8_t, ZtArrayHeapID<"ZiPBTest.SmallInts">>));
ZuDerive(BigInts, (ZtArray<uint64_t, ZtArrayHeapID<"ZiPBTest.BigInts">>));
struct EdgeVecRow { SmallInts small; BigInts big; };
ZfStruct(, (EdgeVecRow, PB),
  (small, (PB::FieldID<1>),	Int8Vec),
  (big, (PB::FieldID<2>),	UInt64Vec));
ZiPB::AsMessage<"test.EdgeVecRow"> ZiPB_Fmt(EdgeVecRow *);

struct NarrowRow {
  int8_t	signed8;
  uint8_t	unsigned8;
  int16_t	signed16;
  uint16_t	unsigned16;
  int64_t	signed64;
  uint64_t	unsigned64;
};
ZfStruct(, (NarrowRow, PB),
  (signed8,,	Int8),
  (unsigned8,,	UInt8),
  (signed16,,	Int16),
  (unsigned16,,	UInt16),
  (signed64,,	Int64),
  (unsigned64,,	UInt64));
ZiPB::AsMessage<"test.NarrowRow"> ZiPB_Fmt(NarrowRow *);

ZuDerive(U8Values, (ZtArray<uint8_t, ZtArrayHeapID<"ZiPBTest.U8Values">>));
ZuDerive(I16Values, (ZtArray<int16_t, ZtArrayHeapID<"ZiPBTest.I16Values">>));
ZuDerive(U16Values, (ZtArray<uint16_t, ZtArrayHeapID<"ZiPBTest.U16Values">>));
ZuDerive(I64Values, (ZtArray<int64_t, ZtArrayHeapID<"ZiPBTest.I64Values">>));
ZuDerive(U32Values, (ZtArray<uint32_t, ZtArrayHeapID<"ZiPBTest.U32Values">>));
struct NarrowVecRow {
  U8Values	unsigned8;
  I16Values	signed16;
  U16Values	unsigned16;
  I64Values	signed64;
  U32Values	unsigned32;
};
ZfStruct(, (NarrowVecRow, PB),
  (unsigned8,,	UInt8Vec),
  (signed16,,	Int16Vec),
  (unsigned16,,	UInt16Vec),
  (signed64,,	Int64Vec),
  (unsigned32,,	UInt32Vec));
ZiPB::AsMessage<"test.NarrowVecRow"> ZiPB_Fmt(NarrowVecRow *);

ZuDerive(BoolValues,
  (ZtArray<uint8_t, ZtArrayHeapID<"ZiPBTest.BoolValues">>));
struct BoolVecRow { BoolValues values; };
ZfStruct(, (BoolVecRow, PB),
  (values, (PB::BoolVec), UInt8Vec));
ZiPB::AsMessage<"test.BoolVecRow"> ZiPB_Fmt(BoolVecRow *);

ZuDerive(ZigValues, (ZtArray<int32_t, ZtArrayHeapID<"ZiPBTest.ZigValues">>));
struct ZigRow { int32_t small; int64_t large; ZigValues values; };
ZfStruct(, (ZigRow, PB),
  (small, (PB::FieldID<1>, PB::ZigZag),		Int32),
  (large, (PB::FieldID<2>, PB::ZigZag),		Int64),
  (values, (PB::FieldID<3>, PB::ZigZag),	Int32Vec));
ZiPB::AsMessage<"test.ZigRow"> ZiPB_Fmt(ZigRow *);

ZuDerive(FixedWords,
  (ZtArray<int32_t, ZtArrayHeapID<"ZiPBTest.FixedWords">>));
struct FixedRow {
  int32_t	signed32 = 0;
  uint32_t	unsigned32 = 0;
  int64_t	signed64 = 0;
  uint64_t	unsigned64 = 0;
  FixedWords	values;
};
ZfStruct(, (FixedRow, PB),
  (signed32, (PB::FieldID<1>, PB::Fixed),	Int32),
  (unsigned32, (PB::FieldID<2>, PB::Fixed),	UInt32),
  (signed64, (PB::FieldID<3>, PB::Fixed),	Int64),
  (unsigned64, (PB::FieldID<4>, PB::Fixed),	UInt64),
  (values, (PB::FieldID<5>, PB::Fixed),		Int32Vec));
ZiPB::AsMessage<"test.FixedRow"> ZiPB_Fmt(FixedRow *);

struct PackedRow {
  ZigValues	zig;
  FixedWords	fixed;
  Float32Values	floats;
  BoolValues	flags;
  U8Values	nums;
};
ZfStruct(, (PackedRow, PB),
  (zig, (PB::Packed, PB::ZigZag),	Int32Vec),
  (fixed, (PB::Packed, PB::Fixed),	Int32Vec),
  (floats, (PB::Packed),		FloatVec),
  (flags, (PB::Packed, PB::BoolVec),	UInt8Vec),
  (nums, (PB::Packed),			UInt8Vec));
ZiPB::AsMessage<"test.PackedRow"> ZiPB_Fmt(PackedRow *);

ZuDerive(TextValues, (ZtArray<ZuCSpan, ZtArrayHeapID<"ZiPBTest.TextValues">>));
ZuDerive(BlobValues, (ZtArray<ZuBSpan, ZtArrayHeapID<"ZiPBTest.BlobValues">>));
struct TextVecRow { TextValues texts; BlobValues blobs; };
ZfStruct(, (TextVecRow, PB),
  (texts, (PB::FieldID<1>),	StringVec),
  (blobs, (PB::FieldID<2>),	BytesVec));
ZiPB::AsMessage<"test.TextVecRow"> ZiPB_Fmt(TextVecRow *);

struct CStringRow {
  char *text = nullptr;
  CStringRow() = default;
  CStringRow(CStringRow &&o) : text{o.text} { o.text = nullptr; }
  ~CStringRow() { ZiPB::CStringArg::free(text); }
};
ZfStruct(, (CStringRow, PB),
  (text, (PB::FieldID<1>, Mutable), CString));
ZiPB::AsMessage<"test.CStringRow"> ZiPB_Fmt(CStringRow *);

struct CtorCStringRow {
  char *text;
  explicit CtorCStringRow(const char *text_) : text{const_cast<char *>(text_)} { }
  CtorCStringRow(CtorCStringRow &&o) : text{o.text} { o.text = nullptr; }
  ~CtorCStringRow() { ZiPB::CStringArg::free(text); }
};
ZfStruct(, (CtorCStringRow, PB),
  (text, (PB::FieldID<1>, Ctor<0>), CString));
ZiPB::AsMessage<"test.CtorCStringRow"> ZiPB_Fmt(CtorCStringRow *);

ZuDerive(CStrings, (ZtArray<char *, ZtArrayHeapID<"ZiPBTest.CStrings">>));
struct CStringVecRow {
  CStrings values;
  CStringVecRow() = default;
  CStringVecRow(CStringVecRow &&o) : values{ZuMv(o.values)} { }
  ~CStringVecRow() {
    for (unsigned i = 0, n = values.length(); i < n; i++) ZiPB::CStringArg::free(values[i]);
  }
};
ZfStruct(, (CStringVecRow, PB),
  (values, (PB::FieldID<1>, Mutable), CStringVec));
ZiPB::AsMessage<"test.CStringVecRow"> ZiPB_Fmt(CStringVecRow *);

struct CtorCStringVecRow {
  CStrings values;
  explicit CtorCStringVecRow(CStrings values_) : values{ZuMv(values_)} { }
  CtorCStringVecRow(CtorCStringVecRow &&o) : values{ZuMv(o.values)} { }
  ~CtorCStringVecRow() {
    for (unsigned i = 0, n = values.length(); i < n; i++) ZiPB::CStringArg::free(values[i]);
  }
};
ZfStruct(, (CtorCStringVecRow, PB),
  (values, (PB::FieldID<1>, Ctor<0>), CStringVec));
ZiPB::AsMessage<"test.CtorCStringVecRow"> ZiPB_Fmt(CtorCStringVecRow *);

static void testImplicitIDs()
{
  ZuTestScope(testImplicitIDs);
  const uint8_t expected[] = {0x08, 7, 0x18, 9};
  ImplicitRow row{7, 9};
  Output output;
  ZiPB::ArrayStorage storage{output};
  ZiPB::Builder builder{storage};
  auto wire = ZiPB::save(builder, row);
  ZuCheck(!builder.failed() && wire.length() == sizeof(expected) &&
    !memcmp(wire.data(), expected, sizeof(expected)));
  ZuCheck(ZiPB::verify<ImplicitRow>({expected, sizeof(expected)}));
  auto copy = ZiPB::load<ImplicitRow>({expected, sizeof(expected)});
  ZuCheck(copy.first == 7 && copy.last == 9);

  const uint8_t oldField[] = {0x10, 5, 0x18, 9};
  ZuCheck(ZiPB::verify<ImplicitRow>({oldField, sizeof(oldField)}));
  auto afterOld = ZiPB::load<ImplicitRow>({oldField, sizeof(oldField)});
  ZuCheck(afterOld.first == 0 && afterOld.last == 9);

  const uint8_t explicitWire[] = {0x20, 7, 0x48, 9};
  ExplicitSkipRow explicitRow{7, 9};
  Output explicitOutput;
  ZiPB::ArrayStorage explicitStorage{explicitOutput};
  ZiPB::Builder explicitBuilder{explicitStorage};
  auto saved = ZiPB::save(explicitBuilder, explicitRow);
  ZuCheck(!explicitBuilder.failed() && saved.length() == sizeof(explicitWire) &&
    !memcmp(saved.data(), explicitWire, sizeof(explicitWire)));
  const uint8_t retiredWire[] = {0x40, 5, 0x48, 9};
  ZuCheck(ZiPB::verify<ExplicitSkipRow>({retiredWire, sizeof(retiredWire)}));
  auto explicitCopy = ZiPB::load<ExplicitSkipRow>(
    {retiredWire, sizeof(retiredWire)});
  ZuCheck(explicitCopy.first == 0 && explicitCopy.last == 9);
}

static void testVarint()
{
  ZuTestScope(testVarint);

  const uint8_t encoded[] = {0x96, 0x01}; // canonical protobuf fixture: 150
  uint64_t value = 0;
  unsigned pos = 0;
  ZuCheck(ZiPB::Wire::readVarint({encoded, sizeof(encoded)}, pos, value));
  ZuCheck(value == 150 && pos == sizeof(encoded));
  ZuCheck(ZiPB::Wire::varintSize(150) == 2);

  // A varint is at most ten bytes; this storage is its wire-defined maximum.
  uint8_t output[10];
  auto start = ZiPB::Wire::prependVarint(output + sizeof(output), UINT64_MAX);
  ZuCheck(start == output);
  ZuCheck(output[0] == 0xff && output[8] == 0xff && output[9] == 0x01);
  pos = 0;
  ZuCheck(ZiPB::Wire::readVarint({output, sizeof(output)}, pos, value));
  ZuCheck(value == UINT64_MAX && pos == sizeof(output));

  const uint8_t truncated[] = {0x80};
  pos = 0;
  ZuCheck(!ZiPB::Wire::readVarint({truncated, sizeof(truncated)}, pos, value));
  const uint8_t overflow[] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x02};
  pos = 0;
  ZuCheck(!ZiPB::Wire::readVarint({overflow, sizeof(overflow)}, pos, value));
}

static void testFields()
{
  ZuTestScope(testFields);

  const uint8_t message[] = {0x08, 0x96, 0x01, 0x12, 0x03, 'a', 'b', 'c'};
  ZuBSpan bytes{message, sizeof(message)};
  unsigned pos = 0;
  uint32_t field = 0;
  ZiPB::Wire::T type = ZiPB::Wire::Varint;
  ZuCheck(ZiPB::Wire::readTag(bytes, pos, field, type));
  ZuCheck(field == 1 && type == ZiPB::Wire::Varint);
  ZuCheck(ZiPB::Wire::skip(bytes, pos, field, type));
  ZuCheck(ZiPB::Wire::readTag(bytes, pos, field, type));
  ZuCheck(field == 2 && type == ZiPB::Wire::Length);
  ZuBSpan value;
  ZuCheck(ZiPB::Wire::readLength(bytes, pos, value));
  ZuCheck(value.length() == 3 && !memcmp(value.data(), "abc", 3));
  ZuCheck(pos == bytes.length());

  const uint8_t invalidTag[] = {0x00};
  pos = 0;
  ZuCheck(!ZiPB::Wire::readTag({invalidTag, sizeof(invalidTag)}, pos, field, type));
  const uint8_t invalidWire[] = {0x0e};
  pos = 0;
  ZuCheck(!ZiPB::Wire::readTag({invalidWire, sizeof(invalidWire)}, pos, field, type));
  const uint8_t shortLength[] = {0x04, 'a', 'b', 'c'};
  pos = 0;
  ZuCheck(!ZiPB::Wire::readLength({shortLength, sizeof(shortLength)}, pos, value));
  ZuCheck(!ZiPB::Wire::fieldNumber(19000));
  ZuCheck(ZiPB::Wire::fieldNumber(19999 + 1));
  const uint8_t reservedWire[] = {0xc0, 0xa3, 0x09, 0x01};
  pos = 0;
  ZuCheck(ZiPB::Wire::readTag({reservedWire, sizeof(reservedWire)},
    pos, field, type));
  ZuCheck(field == 19000 && type == ZiPB::Wire::Varint);
  ZuCheck(ZiPB::Wire::skip({reservedWire, sizeof(reservedWire)}, pos, field, type));
  ZuCheck(pos == sizeof(reservedWire));

  const uint8_t nested[] = {0x23, 0x28, 0x01, 0x33,
    0x3a, 0x01, 'x', 0x34, 0x24, 0x08, 0x2a};
  pos = 0;
  ZuCheck(ZiPB::Wire::readTag({nested, sizeof(nested)}, pos, field, type));
  ZuCheck(field == 4 && type == ZiPB::Wire::StartGroup);
  ZuCheck(ZiPB::Wire::skip({nested, sizeof(nested)}, pos, field, type));
  ZuCheck(pos == sizeof(nested) - 2);
  ZuCheck(ZiPB::verify<Row>({nested, sizeof(nested)}));
  ZuCheck(!ZiPB::verify<Row>({nested, sizeof(nested)}, {1}));
  ZuCheck(ZiPB::verify<Row>({nested, sizeof(nested)}, {2}));
  ZuCheck(ZiPB::load<Row>({nested, sizeof(nested)}).id == 42);

  const uint8_t wrongEnd[] = {0x23, 0x2c};
  ZuCheck(!ZiPB::verify<Row>({wrongEnd, sizeof(wrongEnd)}));
  const uint8_t unclosed[] = {0x23, 0x28, 0x01};
  ZuCheck(!ZiPB::verify<Row>({unclosed, sizeof(unclosed)}));
  const uint8_t strayEnd[] = {0x24};
  ZuCheck(!ZiPB::verify<Row>({strayEnd, sizeof(strayEnd)}));
}

static void testBuilder()
{
  ZuTestScope(testBuilder);

  Output output;
  ZiPB::ArrayStorage storage{output};
  ZiPB::Builder builder{storage};
  const uint8_t text[] = {'a', 'b', 'c'};
  builder.bytes({text, sizeof(text)});
  builder.varint(sizeof(text));
  builder.tag(2, ZiPB::Wire::Length);
  builder.varint(150);
  builder.tag(1, ZiPB::Wire::Varint);
  const uint8_t expected[] = {0x08, 0x96, 0x01, 0x12, 0x03, 'a', 'b', 'c'};
  auto wire = builder.bytes();
  ZuCheck(!builder.failed());
  ZuCheck(wire.length() == sizeof(expected));
  ZuCheck(!memcmp(wire.data(), expected, sizeof(expected)));
  ZuCheck(builder.capacity() >= wire.length());

  uint8_t exact[sizeof(expected)];
  ZiPB::SpanStorage exactStorage{{exact, sizeof(exact)}};
  ZiPB::Builder exactBuilder{exactStorage};
  exactBuilder.bytes({text, sizeof(text)});
  exactBuilder.varint(sizeof(text));
  exactBuilder.tag(2, ZiPB::Wire::Length);
  exactBuilder.varint(150);
  exactBuilder.tag(1, ZiPB::Wire::Varint);
  ZuCheck(!exactBuilder.failed());
  ZuCheck(!memcmp(exactBuilder.bytes().data(), expected, sizeof(expected)));

  uint8_t small[2];
  ZiPB::SpanStorage fixed{{small, sizeof(small)}};
  ZiPB::Builder limited{fixed};
  limited.bytes({text, sizeof(text)});
  ZuCheck(limited.failed() && limited.error() == ZiPB::Error::Capacity);
  ZuCheck(!limited.size());
}

static void testRow()
{
  ZuTestScope(testRow);

  Row input{150, "abc", false};
  Output output;
  ZiPB::ArrayStorage storage{output};
  ZiPB::Builder builder{storage};
  auto wire = ZiPB::save(builder, input);
  const uint8_t expected[] = {0x08, 0x96, 0x01, 0x12, 0x03, 'a', 'b', 'c',
    0x18, 0x00};
  ZuCheck(!builder.failed());
  ZuCheck(wire.length() == sizeof(expected));
  ZuCheck(!memcmp(wire.data(), expected, sizeof(expected)));
  ZuCheck(ZiPB::verify<Row>(wire));
  ZuCheck(ZiPB::verify<Row>(wire, {0}));

  Output exactOutput;
  ZiPB::ArrayStorage exactStorage{exactOutput};
  ZiPB::Builder exactBuilder{exactStorage, sizeof(expected)};
  auto exact = ZiPB::save(exactBuilder, input);
  ZuCheck(exact.length() == sizeof(expected) && !exactBuilder.failed());
  ZuCheck(exactBuilder.capacity() <= sizeof(expected));
  Output shortOutput;
  ZiPB::ArrayStorage shortStorage{shortOutput};
  ZiPB::Builder shortBuilder{shortStorage, sizeof(expected) - 1};
  auto tooLong = ZiPB::save(shortBuilder, input);
  ZuCheck(!tooLong.length() && shortBuilder.error() == ZiPB::Error::Limit);
  ZuCheck(shortBuilder.capacity() < sizeof(expected));
  shortBuilder.fail(ZiPB::Error::Required);
  ZuCheck(shortBuilder.error() == ZiPB::Error::Limit);

  auto copy = ZiPB::handler<Row>(wire).ctor();
  ZuCheck(copy.id == 150 && copy.name == "abc" && !copy.enabled);
  auto loaded = ZiPB::load<Row>(wire);
  ZuCheck(loaded.id == copy.id && loaded.name == copy.name);
  Row updated{9, "old", true};
  ZiPB::handler<Row>(wire).update(updated);
  ZuCheck(updated.id == 150 && updated.name == "old" && updated.enabled);

  Output framedOutput;
  ZiPB::ArrayStorage framedStorage{framedOutput};
  ZiPB::Builder framedBuilder{framedStorage};
  auto framed = ZiPB::save<ZuFacet::PrefixedPB>(framedBuilder, input);
  const uint8_t prefix[] = {sizeof(expected), 0, 0, 0};
  ZuCheck(framed.length() == sizeof(prefix) + sizeof(expected));
  ZuCheck(!memcmp(framed.data(), prefix, sizeof(prefix)));
  ZuCheck((ZiPB::verify<Row, ZuFacet::PrefixedPB>(framed)));
  auto framedCopy = ZiPB::load<Row, ZuFacet::PrefixedPB>(framed);
  ZuCheck(framedCopy.id == input.id && framedCopy.name == input.name);
  auto directFramed = ZiPB::handler<Row, ZuFacet::PrefixedPB>(framed).ctor();
  ZuCheck(directFramed.id == input.id && directFramed.name == input.name);
  ZuCheck(!ZiPB::verify<Row>(framed));

  Output inheritedOutput;
  ZiPB::ArrayStorage inheritedStorage{inheritedOutput};
  ZiPB::Builder inheritedBuilder{inheritedStorage};
  auto inherited = ZiPB::save<ZuFacet::InheritedPB>(inheritedBuilder, input);
  ZuCheck(!inheritedBuilder.failed() && inherited == framed &&
    (ZiPB::verify<Row, ZuFacet::InheritedPB>(inherited)));
  auto inheritedCopy = ZiPB::load<Row, ZuFacet::InheritedPB>(inherited);
  ZuCheck(inheritedCopy.id == input.id && inheritedCopy.name == input.name);

  Output rawChildOutput;
  ZiPB::ArrayStorage rawChildStorage{rawChildOutput};
  ZiPB::Builder rawChildBuilder{rawChildStorage};
  auto rawChild = ZiPB::save<ZuFacet::RawChildPB>(rawChildBuilder, input);
  ZuCheck(!rawChildBuilder.failed() && rawChild == wire &&
    (ZiPB::verify<Row, ZuFacet::RawChildPB>(rawChild)));

  Output shortFrameOutput;
  ZiPB::ArrayStorage shortFrameStorage{shortFrameOutput};
  ZiPB::Builder shortFrameBuilder{shortFrameStorage,
    sizeof(prefix) + sizeof(expected) - 1};
  auto shortFrame = ZiPB::save<ZuFacet::PrefixedPB>(shortFrameBuilder, input);
  ZuCheck(!shortFrame.length() && shortFrameBuilder.error() == ZiPB::Error::Limit);

  Output filteredOutput;
  ZiPB::ArrayStorage filteredStorage{filteredOutput};
  ZiPB::Builder filteredBuilder{filteredStorage};
  auto filtered = ZiPB::save<ZuFacet::WirePB>(filteredBuilder, input);
  const uint8_t filteredBytes[] = {0x12, 0x03, 'a', 'b', 'c', 0x08, 0x96, 0x01};
  ZuCheck(filtered.length() == sizeof(filteredBytes));
  ZuCheck(!memcmp(filtered.data(), filteredBytes, sizeof(filteredBytes)));
  ZuCheck((ZiPB::verify<Row, ZuFacet::WirePB>(filtered)));
  Output updateOutput;
  ZiPB::ArrayStorage updateStorage{updateOutput};
  ZiPB::Builder updateBuilder{updateStorage};
  auto updateWire = ZiPB::save<ZuFacet::WirePB, ZfFieldFilter::Upd>(
    updateBuilder, input);
  const uint8_t updateBytes[] = {0x08, 0x96, 0x01};
  ZuCheck(!updateBuilder.failed() && updateWire.length() == sizeof(updateBytes) &&
    !memcmp(updateWire.data(), updateBytes, sizeof(updateBytes)));

  const uint8_t invalidID[] = {0x08, 0xff, 0xff, 0xff, 0xff, 0x0f};
  ZuCheck(ZiPB::verify<Row>({invalidID, sizeof(invalidID)}));
  const uint8_t emptyName[] = {0x12, 0x00};
  ZuCheck(ZiPB::verify<Row>({emptyName, sizeof(emptyName)}));

  const uint8_t duplicate[] = {0x08, 0x01, 0x20, 0x2a, 0x08, 0x02};
  ZuCheck(ZiPB::verify<Row>({duplicate, sizeof(duplicate)}));
  auto last = ZiPB::load<Row>({duplicate, sizeof(duplicate)});
  ZuCheck(last.id == 2);

  RequiredRow missing{ZuCmp<uint32_t>::null()};
  Output requiredOutput;
  ZiPB::ArrayStorage requiredStorage{requiredOutput};
  ZiPB::Builder requiredBuilder{requiredStorage};
  auto failed = ZiPB::save(requiredBuilder, missing);
  ZuCheck(requiredBuilder.error() == ZiPB::Error::Required && !failed.length());
  ZuCheck(!ZiPB::verify<RequiredRow>({}));

  Row nullID{ZuCmp<uint32_t>::null(), "abc", false};
  Output nullOutput;
  ZiPB::ArrayStorage nullStorage{nullOutput};
  ZiPB::Builder nullBuilder{nullStorage};
  auto omitted = ZiPB::save(nullBuilder, nullID);
  const uint8_t omittedBytes[] = {0x12, 0x03, 'a', 'b', 'c', 0x18, 0x00};
  ZuCheck(omitted.length() == sizeof(omittedBytes));
  ZuCheck(!memcmp(omitted.data(), omittedBytes, sizeof(omittedBytes)));
}

static void testFloat()
{
  ZuTestScope(testFloat);

  FloatRow input{1.5};
  Output output;
  ZiPB::ArrayStorage storage{output};
  ZiPB::Builder builder{storage};
  auto wire = ZiPB::save(builder, input);
  const uint8_t expected[] = {0x09, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0xf8, 0x3f};
  ZuCheck(!builder.failed());
  ZuCheck(wire.length() == sizeof(expected));
  ZuCheck(!memcmp(wire.data(), expected, sizeof(expected)));
  ZuCheck(ZiPB::verify<FloatRow>(wire));
  auto decoded = ZiPB::load<FloatRow>(wire);
  ZuCheck(decoded.value == input.value);

  const uint8_t wrongWire[] = {0x08, 0x01};
  ZuCheck(!ZiPB::verify<FloatRow>({wrongWire, sizeof(wrongWire)}));
  const uint8_t shortWire[] = {0x09, 0, 0, 0, 0, 0, 0, 0};
  ZuCheck(!ZiPB::verify<FloatRow>({shortWire, sizeof(shortWire)}));

  FloatRow nullInput{ZuCmp<double>::null()};
  Output nullOutput;
  ZiPB::ArrayStorage nullStorage{nullOutput};
  ZiPB::Builder nullBuilder{nullStorage};
  auto omitted = ZiPB::save(nullBuilder, nullInput);
  ZuCheck(!nullBuilder.failed() && !omitted.length());
  const uint8_t sentinel[] = {0x09, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0xf8, 0x7f};
  ZuCheck(ZiPB::verify<FloatRow>({sentinel, sizeof(sentinel)}));
}

static void testFloat32()
{
  ZuTestScope(testFloat32);
  Float32Row input{1.5f};
  Output output;
  ZiPB::ArrayStorage storage{output};
  ZiPB::Builder builder{storage};
  auto wire = ZiPB::save(builder, input);
  const uint8_t expected[] = {0x0d, 0, 0, 0xc0, 0x3f};
  ZuCheck(!builder.failed() && wire.length() == sizeof(expected) &&
    !memcmp(wire.data(), expected, sizeof(expected)));
  ZuCheck(ZiPB::verify<Float32Row>(wire));
  ZuCheck(ZiPB::load<Float32Row>(wire).value == 1.5f);
  const uint8_t wrong[] = {0x09, 0, 0, 0, 0, 0, 0, 0xf8, 0x3f};
  ZuCheck(!ZiPB::verify<Float32Row>({wrong, sizeof(wrong)}));

  Float32VecRow values;
  values.values.push(1.5f);
  values.values.push(2.5f);
  Output vectorOutput;
  ZiPB::ArrayStorage vectorStorage{vectorOutput};
  ZiPB::Builder vectorBuilder{vectorStorage};
  auto vectorWire = ZiPB::save(vectorBuilder, values);
  const uint8_t unpacked[] = {
    0x0d, 0, 0, 0xc0, 0x3f, 0x0d, 0, 0, 0x20, 0x40};
  ZuCheck(!vectorBuilder.failed() && vectorWire.length() == sizeof(unpacked) &&
    !memcmp(vectorWire.data(), unpacked, sizeof(unpacked)));
  const uint8_t packed[] = {0x0a, 0x08, 0, 0, 0xc0, 0x3f, 0, 0, 0x20, 0x40};
  ZuCheck(ZiPB::verify<Float32VecRow>({packed, sizeof(packed)}));
  auto decoded = ZiPB::load<Float32VecRow>({packed, sizeof(packed)});
  ZuCheck(decoded.values.length() == 2 && decoded.values[0] == 1.5f &&
    decoded.values[1] == 2.5f);
  const uint8_t shortPacked[] = {0x0a, 0x03, 0, 0, 0};
  ZuCheck(!ZiPB::verify<Float32VecRow>({shortPacked, sizeof(shortPacked)}));
}

static void testAdapterRules()
{
  ZuTestScope(testAdapterRules);
  const uint8_t wire[] = {0x08, 1, 0x10, 2, 0x18, 3, 0x20, 4, 0x28, 5};
  auto h = ZiPB::handler<AdapterRow>({wire, sizeof(wire)});
  auto row = h.ctor();
  ZuCheck(row.ro == 1 && row.rw == 2 && row.init == 3 &&
    row.mut == 4 && row.key == 5);
  auto allocated = h.alloc();
  ZuCheck(allocated->ro == 1 && allocated->rw == 2 && allocated->init == 3 &&
    allocated->mut == 4 && allocated->key == 5);
  delete allocated;
  alignas(AdapterRow) unsigned char memory[sizeof(AdapterRow)];
  h.new_(memory);
  auto placed = ZuLaunder(reinterpret_cast<AdapterRow *>(memory));
  ZuCheck(placed->ro == 1 && placed->rw == 2 && placed->init == 3 &&
    placed->mut == 4 && placed->key == 5);
  placed->~AdapterRow();

  AdapterRow loaded{11, 12};
  loaded.init = 13; loaded.mut = 14; loaded.key = 15;
  h.load(loaded);
  ZuCheck(loaded.ro == 11 && loaded.rw == 2 && loaded.init == 3 &&
    loaded.mut == 4 && loaded.key == 5);
  AdapterRow updated{11, 12};
  updated.init = 13; updated.mut = 14; updated.key = 15;
  h.update(updated);
  ZuCheck(updated.ro == 11 && updated.rw == 12 && updated.init == 13 &&
    updated.mut == 4 && updated.key == 5);

  ReadOnlyPtrParent pointer{new PtrChild{}};
  auto saved = pointer.child;
  unsigned deleted = ptrDeletes;
  ZiPB::handler<ReadOnlyPtrParent>({}).update(pointer);
  ZuCheck(pointer.child == saved && ptrDeletes == deleted);
  const uint8_t child[] = {0x0a, 0x02, 0x08, 7};
  ZiPB::handler<ReadOnlyPtrParent>({child, sizeof(child)}).update(pointer);
  ZuCheck(pointer.child == saved && ptrDeletes == deleted);

  auto moved = ZiPB::load<MoveParent>({child, sizeof(child)});
  ZuCheck(moved.child.id == 7);

}

static void testCtor()
{
  ZuTestScope(testCtor);

  const uint8_t wire[] = {0x08, 0x2a, 0x12, 0x03, 'a', 'b', 'c', 0x18, 0x07};
  ZuCheck(ZiPB::verify<CtorRow>({wire, sizeof(wire)}));
  auto row = ZiPB::load<CtorRow>({wire, sizeof(wire)});
  ZuCheck(row.id == 42 && row.name == "abc" && row.version == 7);

  const uint8_t duplicate[] = {0x08, 0x01, 0x08, 0x2a, 0x12, 0x03,
    'a', 'b', 'c'};
  auto last = ZiPB::load<CtorRow>({duplicate, sizeof(duplicate)});
  ZuCheck(last.id == 42 && last.name == "abc" && !last.version);

  alignas(CtorRow) unsigned char place[sizeof(CtorRow)];
  ZiPB::handler<CtorRow>({wire, sizeof(wire)}).new_(place);
  auto placed = static_cast<CtorRow *>(static_cast<void *>(place));
  ZuCheck(placed->id == 42 && placed->name == "abc" && placed->version == 7);
  placed->~CtorRow();

  Output output;
  ZiPB::ArrayStorage storage{output};
  ZiPB::Builder builder{storage};
  auto saved = ZiPB::save(builder, row);
  ZuCheck(saved.length() == sizeof(wire));
  ZuCheck(!memcmp(saved.data(), wire, sizeof(wire)));
}

static void testBytes()
{
  ZuTestScope(testBytes);

  const uint8_t value[] = {'a', 0, 'b'};
  BytesRow input{{value, sizeof(value)}};
  Output output;
  ZiPB::ArrayStorage storage{output};
  ZiPB::Builder builder{storage};
  auto wire = ZiPB::save(builder, input);
  const uint8_t expected[] = {0x0a, 0x03, 'a', 0, 'b'};
  ZuCheck(wire.length() == sizeof(expected));
  ZuCheck(!memcmp(wire.data(), expected, sizeof(expected)));
  ZuCheck(ZiPB::verify<BytesRow>(wire));
  auto decoded = ZiPB::load<BytesRow>(wire);
  ZuCheck(decoded.payload.length() == sizeof(value));
  ZuCheck(!memcmp(decoded.payload.data(), value, sizeof(value)));

  const uint8_t empty[] = {0x0a, 0};
  ZuCheck(ZiPB::verify<BytesRow>({empty, sizeof(empty)}));
  const uint8_t shortValue[] = {0x0a, 0x04, 'a', 0, 'b'};
  ZuCheck(!ZiPB::verify<BytesRow>({shortValue, sizeof(shortValue)}));
}

static void testNested()
{
  ZuTestScope(testNested);

  Parent input{{1, 2}};
  Output output;
  ZiPB::ArrayStorage storage{output};
  ZiPB::Builder builder{storage};
  auto wire = ZiPB::save(builder, input);
  const uint8_t expected[] = {0x0a, 0x04, 0x08, 0x01, 0x10, 0x02};
  ZuCheck(wire.length() == sizeof(expected));
  ZuCheck(!memcmp(wire.data(), expected, sizeof(expected)));
  ZuCheck(ZiPB::verify<Parent>(wire));
  ZuCheck(!ZiPB::verify<Parent>(wire, {0}));
  ZuCheck(ZiPB::verify<Parent>(wire, {1}));
  auto decoded = ZiPB::load<Parent>(wire);
  ZuCheck(decoded.child.a == 1 && decoded.child.b == 2);

  const uint8_t split[] = {0x0a, 0x02, 0x08, 0x01,
    0x0a, 0x02, 0x10, 0x02};
  ZuCheck(ZiPB::verify<Parent>({split, sizeof(split)}));
  auto merged = ZiPB::load<Parent>({split, sizeof(split)});
  ZuCheck(merged.child.a == 1 && merged.child.b == 2);
  auto ctorMerged = ZiPB::load<ParentCtor>({split, sizeof(split)});
  ZuCheck(ctorMerged.child.a == 1 && ctorMerged.child.b == 2);

  Output nestedOutput;
  ZiPB::ArrayStorage nestedStorage{nestedOutput};
  ZiPB::Builder nestedBuilder{nestedStorage};
  auto nestedWire = ZiPB::save<ZuFacet::NestedPB>(nestedBuilder, input);
  const uint8_t nestedExpected[] = {0x0a, 0x04, 0x10, 0x02, 0x08, 0x01};
  ZuCheck(nestedWire.length() == sizeof(nestedExpected));
  ZuCheck(!memcmp(nestedWire.data(), nestedExpected, sizeof(nestedExpected)));
  ZuCheck((ZiPB::verify<Parent, ZuFacet::NestedPB>(nestedWire)));
  auto nestedCopy = ZiPB::load<Parent, ZuFacet::NestedPB>(nestedWire);
  ZuCheck(nestedCopy.child.a == 1 && nestedCopy.child.b == 2);

  const uint8_t empty[] = {0x0a, 0x00};
  ZuCheck(ZiPB::verify<Parent>({empty, sizeof(empty)}));
  const uint8_t invalid[] = {0x0a, 0x02, 0x08, 0x80};
  ZuCheck(!ZiPB::verify<Parent>({invalid, sizeof(invalid)}));

  const uint8_t splitRequired[] = {0x0a, 0x02, 0x10, 0x02,
    0x0a, 0x02, 0x08, 0x01};
  ZuCheck(ZiPB::verify<ReqParent>({splitRequired, sizeof(splitRequired)}));
  ZuCheck(ZiPB::verify<ReqParent>({splitRequired, sizeof(splitRequired)}, {1}));
  auto required = ZiPB::load<ReqParent>({splitRequired, sizeof(splitRequired)});
  ZuCheck(required.child.a == 1 && required.child.b == 2);
  const uint8_t missingRequired[] = {0x0a, 0x02, 0x10, 0x02};
  ZuCheck(!ZiPB::verify<ReqParent>({missingRequired, sizeof(missingRequired)}));
  ZuCheck(ZiPB::verify<ReqParent>({}));
}

static void testVectors()
{
  ZuTestScope(testVectors);

  IntVecRow input;
  input.values.push(1);
  input.values.push(2);
  input.values.push(3);
  Output output;
  ZiPB::ArrayStorage storage{output};
  ZiPB::Builder builder{storage};
  auto wire = ZiPB::save(builder, input);
  const uint8_t unpacked[] = {0x08, 0x01, 0x08, 0x02, 0x08, 0x03};
  ZuCheck(wire.length() == sizeof(unpacked));
  ZuCheck(!memcmp(wire.data(), unpacked, sizeof(unpacked)));
  ZuCheck(ZiPB::verify<IntVecRow>(wire));

  const uint8_t packed[] = {0x0a, 0x03, 0x01, 0x02, 0x03};
  ZuCheck(ZiPB::verify<IntVecRow>({packed, sizeof(packed)}));
  auto packedRow = ZiPB::load<IntVecRow>({packed, sizeof(packed)});
  ZuCheck(packedRow.values.length() == 3 && packedRow.values[0] == 1 &&
    packedRow.values[1] == 2 && packedRow.values[2] == 3);

  const uint8_t mixed[] = {0x08, 0x01, 0x0a, 0x02, 0x02, 0x03};
  auto mixedRow = ZiPB::load<IntVecRow>({mixed, sizeof(mixed)});
  ZuCheck(mixedRow.values.length() == 3 && mixedRow.values[0] == 1 &&
    mixedRow.values[1] == 2 && mixedRow.values[2] == 3);
  IntVecRow appended;
  appended.values.push(9);
  ZiPB::handler<IntVecRow>({mixed, sizeof(mixed)}).load(appended);
  ZuCheck(appended.values.length() == 4 && appended.values[0] == 9 &&
    appended.values[1] == 1 && appended.values[2] == 2 && appended.values[3] == 3);
  const uint8_t narrow[] = {0x08, 0xff, 0xff, 0xff, 0xff, 0x0f};
  ZuCheck(!ZiPB::verify<IntVecRow>({narrow, sizeof(narrow)}));
  const uint8_t sentinel[] = {0x08, 0x80, 0x80, 0x80, 0x80,
    0xf8, 0xff, 0xff, 0xff, 0xff, 0x01};
  ZuCheck(ZiPB::verify<IntVecRow>({sentinel, sizeof(sentinel)}));

  IntVecRow nullInput;
  nullInput.values.push(ZuCmp<int32_t>::null());
  Output nullOutput;
  ZiPB::ArrayStorage nullStorage{nullOutput};
  ZiPB::Builder nullBuilder{nullStorage};
  auto invalid = ZiPB::save(nullBuilder, nullInput);
  ZuCheck(!nullBuilder.failed() && ZiPB::verify<IntVecRow>(invalid));

  FloatVecRow floats;
  floats.values.push(1.5);
  floats.values.push(2.5);
  Output floatOutput;
  ZiPB::ArrayStorage floatStorage{floatOutput};
  ZiPB::Builder floatBuilder{floatStorage};
  auto floatWire = ZiPB::save(floatBuilder, floats);
  const uint8_t floatUnpacked[] = {
    0x09, 0, 0, 0, 0, 0, 0, 0xf8, 0x3f,
    0x09, 0, 0, 0, 0, 0, 0, 0x04, 0x40};
  ZuCheck(floatWire.length() == sizeof(floatUnpacked));
  ZuCheck(!memcmp(floatWire.data(), floatUnpacked, sizeof(floatUnpacked)));
  const uint8_t floatPacked[] = {
    0x0a, 0x10, 0, 0, 0, 0, 0, 0, 0xf8, 0x3f,
    0, 0, 0, 0, 0, 0, 0x04, 0x40};
  ZuCheck(ZiPB::verify<FloatVecRow>({floatPacked, sizeof(floatPacked)}));
  auto floatRow = ZiPB::load<FloatVecRow>({floatPacked, sizeof(floatPacked)});
  ZuCheck(floatRow.values.length() == 2 && floatRow.values[0] == 1.5 &&
    floatRow.values[1] == 2.5);
  const uint8_t shortFloat[] = {0x0a, 0x01, 0};
  ZuCheck(!ZiPB::verify<FloatVecRow>({shortFloat, sizeof(shortFloat)}));

  EdgeVecRow edges;
  edges.small.push(-1);
  edges.big.push(UINT64_MAX - 1);
  Output edgeOutput;
  ZiPB::ArrayStorage edgeStorage{edgeOutput};
  ZiPB::Builder edgeBuilder{edgeStorage};
  auto edgeWire = ZiPB::save(edgeBuilder, edges);
  const uint8_t edgeExpected[] = {
    0x08, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x01,
    0x10, 0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x01};
  ZuCheck(edgeWire.length() == sizeof(edgeExpected));
  ZuCheck(!memcmp(edgeWire.data(), edgeExpected, sizeof(edgeExpected)));
  ZuCheck(ZiPB::verify<EdgeVecRow>(edgeWire));
  auto edgeCopy = ZiPB::load<EdgeVecRow>(edgeWire);
  ZuCheck(edgeCopy.small.length() == 1 && edgeCopy.small[0] == -1 &&
    edgeCopy.big.length() == 1 && edgeCopy.big[0] == UINT64_MAX - 1);
}

static void testTextVectors()
{
  ZuTestScope(testTextVectors);

  const uint8_t blob[] = {'x', 0, 'y'};
  TextVecRow input;
  input.texts.push(ZuCSpan{"ab"});
  input.texts.push(ZuCSpan{"c"});
  input.blobs.push(ZuBSpan{blob, sizeof(blob)});
  Output output;
  ZiPB::ArrayStorage storage{output};
  ZiPB::Builder builder{storage};
  auto wire = ZiPB::save(builder, input);
  const uint8_t expected[] = {
    0x0a, 0x02, 'a', 'b', 0x0a, 0x01, 'c',
    0x12, 0x03, 'x', 0, 'y'};
  ZuCheck(wire.length() == sizeof(expected));
  ZuCheck(!memcmp(wire.data(), expected, sizeof(expected)));
  ZuCheck(ZiPB::verify<TextVecRow>(wire));
  auto decoded = ZiPB::load<TextVecRow>(wire);
  ZuCheck(decoded.texts.length() == 2 && decoded.texts[0] == "ab" &&
    decoded.texts[1] == "c");
  ZuCheck(decoded.blobs.length() == 1 && decoded.blobs[0].length() == 3 &&
    !memcmp(decoded.blobs[0].data(), blob, sizeof(blob)));

  const uint8_t emptyText[] = {0x0a, 0x00};
  ZuCheck(ZiPB::verify<TextVecRow>({emptyText, sizeof(emptyText)}));
  const uint8_t emptyBlob[] = {0x12, 0x00};
  ZuCheck(ZiPB::verify<TextVecRow>({emptyBlob, sizeof(emptyBlob)}));
  TextVecRow nullInput;
  nullInput.texts.push(ZuCSpan{});
  Output nullOutput;
  ZiPB::ArrayStorage nullStorage{nullOutput};
  ZiPB::Builder nullBuilder{nullStorage};
  auto failed = ZiPB::save(nullBuilder, nullInput);
  ZuCheck(!nullBuilder.failed() && failed.length() == 2);
}

static void testCString()
{
  ZuTestScope(testCString);

  const uint8_t duplicate[] = {0x0a, 0x03, 'o', 'n', 'e',
    0x0a, 0x03, 't', 'w', 'o'};
  ZuCheck(ZiPB::verify<CStringRow>({duplicate, sizeof(duplicate)}));
  auto row = ZiPB::load<CStringRow>({duplicate, sizeof(duplicate)});
  ZuCheck(row.text && !strcmp(row.text, "two") && row.text[3] == 0);
  auto ctor = ZiPB::load<CtorCStringRow>({duplicate, sizeof(duplicate)});
  ZuCheck(ctor.text && !strcmp(ctor.text, "two"));

  const uint8_t update[] = {0x0a, 0x03, 'n', 'e', 'w'};
  ZiPB::handler<CStringRow>({update, sizeof(update)}).update(row);
  ZuCheck(row.text && !strcmp(row.text, "new"));

  Output output;
  ZiPB::ArrayStorage storage{output};
  ZiPB::Builder builder{storage};
  auto wire = ZiPB::save(builder, row);
  ZuCheck(wire.length() == sizeof(update));
  ZuCheck(!memcmp(wire.data(), update, sizeof(update)));

  const uint8_t empty[] = {0x0a, 0x00};
  ZuCheck(ZiPB::verify<CStringRow>({empty, sizeof(empty)}));
}

static void testCStringVec()
{
  ZuTestScope(testCStringVec);

  const uint8_t input[] = {0x0a, 0x03, 'o', 'n', 'e',
    0x0a, 0x03, 't', 'w', 'o'};
  ZuCheck(ZiPB::verify<CStringVecRow>({input, sizeof(input)}));
  auto row = ZiPB::load<CStringVecRow>({input, sizeof(input)});
  ZuCheck(row.values.length() == 2 && !strcmp(row.values[0], "one") &&
    !strcmp(row.values[1], "two"));
  ZuCheck(row.values[0][3] == 0 && row.values[1][3] == 0);
  auto ctor = ZiPB::load<CtorCStringVecRow>({input, sizeof(input)});
  ZuCheck(ctor.values.length() == 2 && !strcmp(ctor.values[0], "one") &&
    !strcmp(ctor.values[1], "two"));

  Output output;
  ZiPB::ArrayStorage storage{output};
  ZiPB::Builder builder{storage};
  auto wire = ZiPB::save(builder, row);
  ZuCheck(wire.length() == sizeof(input) &&
    !memcmp(wire.data(), input, sizeof(input)));

  const uint8_t append[] = {0x0a, 0x03, 'n', 'e', 'w'};
  ZiPB::handler<CStringVecRow>({append, sizeof(append)}).update(row);
  ZuCheck(row.values.length() == 3 && !strcmp(row.values[2], "new"));

  const uint8_t empty[] = {0x0a, 0x00};
  ZuCheck(ZiPB::verify<CStringVecRow>({empty, sizeof(empty)}));
  CStringVecRow nullInput;
  nullInput.values.push(nullptr);
  Output nullOutput;
  ZiPB::ArrayStorage nullStorage{nullOutput};
  ZiPB::Builder nullBuilder{nullStorage};
  auto failed = ZiPB::save(nullBuilder, nullInput);
  ZuCheck(!nullBuilder.failed() && failed.length() == 2);
}

static void testPointer()
{
  ZuTestScope(testPointer);

  const uint8_t split[] = {0x0a, 0x02, 0x08, 0x01,
    0x0a, 0x02, 0x10, 0x02};
  ZuCheck(ZiPB::verify<PtrParent>({split, sizeof(split)}));
  auto row = ZiPB::load<PtrParent>({split, sizeof(split)});
  ZuCheck(row.child && row.child->a == 1 && row.child->b == 2);
  auto smart = ZiPB::load<SmartParent>({split, sizeof(split)});
  ZuCheck(smart.child && smart.child->a == 1 && smart.child->b == 2);
  auto ref = ZiPB::load<RefParent>({split, sizeof(split)});
  ZuCheck(ref.child && ref.child->a == 1 && ref.child->b == 2);
  auto localRef = ZiPB::load<LocalRefParent>({split, sizeof(split)});
  ZuCheck(localRef.child && localRef.child->a == 1 && localRef.child->b == 2);
  auto foreign = ZiPB::load<ForeignParent>({split, sizeof(split)});
  ZuCheck(foreign.child && foreign.child->a == 1 && foreign.child->b == 2);
  auto foreignArray = ZiPB::load<ForeignArrayParent>(
    {split, sizeof(split)});
  ZuCheck(foreignArray.children.length() == 2 &&
    foreignArray.children[0] && foreignArray.children[0]->a == 1 &&
    foreignArray.children[1] && foreignArray.children[1]->b == 2);
  auto ctor = ZiPB::load<CtorPtrParent>({split, sizeof(split)});
  ZuCheck(ctor.child && ctor.child->a == 1 && ctor.child->b == 2);
  auto ctorSmart = ZiPB::load<CtorSmartParent>({split, sizeof(split)});
  ZuCheck(ctorSmart.child && ctorSmart.child->a == 1 && ctorSmart.child->b == 2);

  Output output;
  ZiPB::ArrayStorage storage{output};
  ZiPB::Builder builder{storage};
  auto wire = ZiPB::save(builder, row);
  const uint8_t expected[] = {0x0a, 0x04, 0x08, 0x01, 0x10, 0x02};
  ZuCheck(wire.length() == sizeof(expected) &&
    !memcmp(wire.data(), expected, sizeof(expected)));
  auto sameWire = [&expected]<typename V>(const V &value) {
    Output bytes;
    ZiPB::ArrayStorage storage{bytes};
    ZiPB::Builder builder{storage};
    auto wire = ZiPB::save(builder, value);
    return wire.length() == sizeof(expected) &&
      !memcmp(wire.data(), expected, sizeof(expected));
  };
  ZuCheck(sameWire(smart) && sameWire(ref) && sameWire(localRef) &&
    sameWire(foreign) && sameWire(ctor) && sameWire(ctorSmart));
  Output foreignOutput;
  ZiPB::ArrayStorage foreignStorage{foreignOutput};
  ZiPB::Builder foreignBuilder{foreignStorage};
  auto foreignWire = ZiPB::save(foreignBuilder, foreignArray);
  ZuCheck(!foreignBuilder.failed() &&
    ZiPB::verify<ForeignArrayParent>(foreignWire));
  auto foreignSaved = ZiPB::load<ForeignArrayParent>(foreignWire);
  ZuCheck(foreignSaved.children.length() == 2 &&
    foreignSaved.children[0]->a == 1 &&
    foreignSaved.children[1]->b == 2);

  unsigned beforeForeign = ptrDeletes;
  const uint8_t foreignReplace[] = {0x0a, 0x02, 0x08, 0x03};
  ZiPB::handler<ForeignArrayParent>(
    {foreignReplace, sizeof(foreignReplace)}).load(foreignArray);
  ZuCheck(foreignArray.children.length() == 1 &&
    foreignArray.children[0]->a == 3 && ptrDeletes == beforeForeign + 2);

  unsigned before = ptrDeletes;
  const uint8_t replace[] = {0x0a, 0x02, 0x08, 0x03};
  ZiPB::handler<PtrParent>({replace, sizeof(replace)}).update(row);
  ZuCheck(ptrDeletes == before + 1 && row.child &&
    row.child->a == 3 && row.child->b == 0);
  ZiPB::handler<SmartParent>({replace, sizeof(replace)}).update(smart);
  ZuCheck(smart.child && smart.child->a == 3 && smart.child->b == 0);
  ZiPB::handler<RefParent>({replace, sizeof(replace)}).update(ref);
  ZuCheck(ref.child && ref.child->a == 3 && ref.child->b == 0);
  ZiPB::handler<LocalRefParent>({replace, sizeof(replace)}).update(localRef);
  ZuCheck(localRef.child && localRef.child->a == 3 && localRef.child->b == 0);
  ZiPB::handler<ForeignParent>({replace, sizeof(replace)}).update(foreign);
  ZuCheck(foreign.child && foreign.child->a == 3 && foreign.child->b == 0);
  auto retained = row.child;
  ZiPB::handler<PtrParent>({}).update(row);
  ZuCheck(row.child == retained && row.child->a == 3);

  PtrParent absent;
  Output emptyOutput;
  ZiPB::ArrayStorage emptyStorage{emptyOutput};
  ZiPB::Builder emptyBuilder{emptyStorage};
  ZuCheck(!ZiPB::save(emptyBuilder, absent).length() && !emptyBuilder.failed());
  ZuCheck(!ZiPB::verify<RequiredPtrParent>({}));
  RequiredPtrParent requiredNull;
  Output requiredOutput;
  ZiPB::ArrayStorage requiredStorage{requiredOutput};
  ZiPB::Builder requiredBuilder{requiredStorage};
  ZuCheck(!ZiPB::save(requiredBuilder, requiredNull).length() &&
    requiredBuilder.error() == ZiPB::Error::Required);
  PartialRequiredRow partial;
  Output partialOutput;
  ZiPB::ArrayStorage partialStorage{partialOutput};
  ZiPB::Builder partialBuilder{partialStorage};
  ZuCheck(!ZiPB::save(partialBuilder, partial).length() &&
    partialBuilder.error() == ZiPB::Error::Required &&
    partialBuilder.size() == 2);
  ZuCheck(!ZiPB::save(partialBuilder, partial).length() &&
    partialBuilder.error() == ZiPB::Error::Required &&
    partialBuilder.size() == 2);
  ZiPB::IOBuilder pooledPartial{new ZiIOBufAlloc<>};
  ZuCheck(!ZiPB::save(pooledPartial, partial).length() &&
    pooledPartial.error() == ZiPB::Error::Required &&
    !pooledPartial.buf());
  ResetPtrParent reset;
  reset.child = new PtrChild;
  unsigned beforeReset = ptrDeletes;
  ZiPB::handler<ResetPtrParent>({}).update(reset);
  ZuCheck(!reset.child && ptrDeletes == beforeReset + 1);
  const uint8_t presentEmpty[] = {0x0a, 0x00};
  ZuCheck(ZiPB::verify<PtrParent>({presentEmpty, sizeof(presentEmpty)}));
  auto present = ZiPB::load<PtrParent>({presentEmpty, sizeof(presentEmpty)});
  ZuCheck(present.child && present.child->a == 0 && present.child->b == 0);

  const uint8_t recursive[] = {0x08, 0x01, 0x12, 0x02, 0x08, 0x02};
  ZuCheck(ZiPB::verify<RecursiveNode>({recursive, sizeof(recursive)}));
  auto chain = ZiPB::load<RecursiveNode>({recursive, sizeof(recursive)});
  ZuCheck(chain.value == 1 && chain.next && chain.next->value == 2 &&
    !chain.next->next);
  Output recursiveOutput;
  ZiPB::ArrayStorage recursiveStorage{recursiveOutput};
  ZiPB::Builder recursiveBuilder{recursiveStorage};
  auto recursiveWire = ZiPB::save(recursiveBuilder, chain);
  ZuCheck(recursiveWire.length() == sizeof(recursive) &&
    !memcmp(recursiveWire.data(), recursive, sizeof(recursive)));

  ZuCheck(ZiPB::verify<EmptyMessage>({}));
  auto emptyMessage = ZiPB::load<EmptyMessage>({});
  Output noFields;
  ZiPB::ArrayStorage noFieldStorage{noFields};
  ZiPB::Builder noFieldBuilder{noFieldStorage};
  ZuCheck(!ZiPB::save(noFieldBuilder, emptyMessage).length() &&
    !noFieldBuilder.failed());
}

static void testEnum()
{
  ZuTestScope(testEnum);

  const uint8_t known[] = {0x08, 0x02};
  ZuCheck(ZiPB::verify<EnumRow>({known, sizeof(known)}));
  auto row = ZiPB::load<EnumRow>({known, sizeof(known)});
  ZuCheck(row.color == PBColor::Blue);

  const uint8_t unknown[] = {0x08, 0x07};
  ZuCheck(ZiPB::verify<EnumRow>({unknown, sizeof(unknown)}));
  auto other = ZiPB::load<EnumRow>({unknown, sizeof(unknown)});
  ZuCheck(other.color == 7);
  Output output;
  ZiPB::ArrayStorage storage{output};
  ZiPB::Builder builder{storage};
  auto wire = ZiPB::save(builder, other);
  ZuCheck(wire.length() == sizeof(unknown) &&
    !memcmp(wire.data(), unknown, sizeof(unknown)));
}

static void testArrays()
{
  ZuTestScope(testArrays);

  const uint8_t input[] = {0x0a, 0x02, 0x08, 0x01,
    0x0a, 0x02, 0x10, 0x02};
  ZuCheck(ZiPB::verify<ArrayParent>({input, sizeof(input)}));
  auto row = ZiPB::load<ArrayParent>({input, sizeof(input)});
  ZuCheck(row.children.length() == 2 && row.children[0].a == 1 &&
    row.children[1].b == 2);
  auto ctor = ZiPB::load<CtorArrayParent>({input, sizeof(input)});
  ZuCheck(ctor.children.length() == 2 && ctor.children[0].a == 1 &&
    ctor.children[1].b == 2);
  auto ptr = ZiPB::load<PtrArrayParent>({input, sizeof(input)});
  ZuCheck(ptr.children.length() == 2 && ptr.children[0]->a == 1 &&
    ptr.children[1]->b == 2);
  auto raw = ZiPB::load<RawArrayParent>({input, sizeof(input)});
  ZuCheck(raw.children.length() == 2 && raw.children[0]->a == 1 &&
    raw.children[1]->b == 2);
  Output output;
  ZiPB::ArrayStorage storage{output};
  ZiPB::Builder builder{storage};
  auto wire = ZiPB::save(builder, row);
  const uint8_t expected[] = {0x0a, 0x04, 0x08, 0x01, 0x10, 0x00,
    0x0a, 0x04, 0x08, 0x00, 0x10, 0x02};
  ZuCheck(wire.length() == sizeof(expected) &&
    !memcmp(wire.data(), expected, sizeof(expected)));
  Output facetOutput;
  ZiPB::ArrayStorage facetStorage{facetOutput};
  ZiPB::Builder facetBuilder{facetStorage};
  auto facetWire = ZiPB::save<ZuFacet::NestedPB>(facetBuilder, row);
  const uint8_t facetExpected[] = {
    0x0a, 0x04, 0x10, 0x00, 0x08, 0x01,
    0x0a, 0x04, 0x10, 0x02, 0x08, 0x00};
  ZuCheck(!facetBuilder.failed() &&
    facetWire.length() == sizeof(facetExpected) &&
    !memcmp(facetWire.data(), facetExpected, sizeof(facetExpected)));
  ZuCheck((ZiPB::verify<ArrayParent, ZuFacet::NestedPB>(facetWire)));
  auto facetRound = ZiPB::load<ArrayParent, ZuFacet::NestedPB>(facetWire);
  ZuCheck(facetRound.children.length() == 2 &&
    facetRound.children[0].a == 1 && facetRound.children[0].b == 0 &&
    facetRound.children[1].a == 0 && facetRound.children[1].b == 2);
  Output ptrOutput;
  ZiPB::ArrayStorage ptrStorage{ptrOutput};
  ZiPB::Builder ptrBuilder{ptrStorage};
  auto ptrWire = ZiPB::save(ptrBuilder, ptr);
  ZuCheck(ptrWire.length() == sizeof(expected) &&
    !memcmp(ptrWire.data(), expected, sizeof(expected)));
  Output rawOutput;
  ZiPB::ArrayStorage rawStorage{rawOutput};
  ZiPB::Builder rawBuilder{rawStorage};
  auto rawWire = ZiPB::save(rawBuilder, raw);
  ZuCheck(rawWire.length() == sizeof(expected) &&
    !memcmp(rawWire.data(), expected, sizeof(expected)));
  ZuCheck(!ZiPB::verify<ReqArrayParent>({input, sizeof(input)}));
  ZuCheck(!ZiPB::verify<ArrayParent>({input, sizeof(input)}, {0}));

  ChildPtrArray invalidValues;
  invalidValues.push(ZuPtr<Child>{});
  PtrArrayParent invalid{ZuMv(invalidValues)};
  Output nullOutput;
  ZiPB::ArrayStorage nullStorage{nullOutput};
  ZiPB::Builder nullBuilder{nullStorage};
  ZuCheck(!ZiPB::save(nullBuilder, invalid).length() &&
    nullBuilder.error() == ZiPB::Error::Null);
  RequiredArrayParent required;
  Output requiredOutput;
  ZiPB::ArrayStorage requiredStorage{requiredOutput};
  ZiPB::Builder requiredBuilder{requiredStorage};
  ZuCheck(!ZiPB::save(requiredBuilder, required).length() &&
    requiredBuilder.error() == ZiPB::Error::Required);

  const uint8_t append[] = {0x0a, 0x02, 0x08, 0x03};
  ZiPB::handler<ArrayParent>({append, sizeof(append)}).update(row);
  ZuCheck(row.children.length() == 3 && row.children[2].a == 3);
  ZiPB::handler<PtrArrayParent>({append, sizeof(append)}).update(ptr);
  ZuCheck(ptr.children.length() == 3 && ptr.children[2]->a == 3);
  ZiPB::handler<RawArrayParent>({append, sizeof(append)}).update(raw);
  ZuCheck(raw.children.length() == 3 && raw.children[2]->a == 3);
  ZiPB::handler<ArrayParent>({append, sizeof(append)}).load(row);
  ZuCheck(row.children.length() == 1 && row.children[0].a == 3);
  ZiPB::handler<RawArrayParent>({append, sizeof(append)}).load(raw);
  ZuCheck(raw.children.length() == 1 && raw.children[0]->a == 3);
}

static void testMap()
{
  ZuTestScope(testMap);

  const uint8_t input[] = {0x0a, 0x04, 0x08, 0x01, 0x10, 0x0a,
    0x0a, 0x04, 0x08, 0x02, 0x10, 0x14,
    0x0a, 0x04, 0x08, 0x01, 0x10, 0x1e};
  ZuCheck(ZiPB::verify<MapRow>({input, sizeof(input)}));
  auto row = ZiPB::load<MapRow>({input, sizeof(input)});
  ZuCheck(row.entries.count_() == 2 && row.entries.findVal(1) == 30 &&
    row.entries.findVal(2) == 20);
  auto ctor = ZiPB::load<CtorMapRow>({input, sizeof(input)});
  ZuCheck(ctor.entries.count_() == 2 && ctor.entries.findVal(1) == 30);
  auto hash = ZiPB::load<HashMapRow>({input, sizeof(input)});
  ZuCheck(hash.entries && hash.entries->count_() == 2 &&
    hash.entries->findVal(1) == 30 && hash.entries->findVal(2) == 20);
  Output output;
  ZiPB::ArrayStorage storage{output};
  ZiPB::Builder builder{storage};
  auto wire = ZiPB::save(builder, row);
  ZuCheck(ZiPB::verify<MapRow>(wire));
  auto round = ZiPB::load<MapRow>(wire);
  ZuCheck(round.entries.count_() == 2 && round.entries.findVal(1) == 30 &&
    round.entries.findVal(2) == 20);
  Output hashOutput;
  ZiPB::ArrayStorage hashStorage{hashOutput};
  ZiPB::Builder hashBuilder{hashStorage};
  auto hashWire = ZiPB::save(hashBuilder, hash);
  ZuCheck(ZiPB::verify<HashMapRow>(hashWire));
  auto hashRound = ZiPB::load<HashMapRow>(hashWire);
  ZuCheck(hashRound.entries && hashRound.entries->count_() == 2 &&
    hashRound.entries->findVal(1) == 30 && hashRound.entries->findVal(2) == 20);

  const uint8_t emptyEntry[] = {0x0a, 0x00};
  ZuCheck(ZiPB::verify<MapRow>({emptyEntry, sizeof(emptyEntry)}));
  auto defaultEntry = ZiPB::load<MapRow>({emptyEntry, sizeof(emptyEntry)});
  ZuCheck(defaultEntry.entries.count_() == 1 &&
    defaultEntry.entries.findVal(0) == 0);

  const uint8_t update[] = {0x0a, 0x04, 0x08, 0x02, 0x10, 0x19};
  ZiPB::handler<MapRow>({update, sizeof(update)}).update(row);
  ZuCheck(row.entries.count_() == 2 && row.entries.findVal(1) == 30 &&
    row.entries.findVal(2) == 25);
  ZiPB::handler<HashMapRow>({update, sizeof(update)}).update(hash);
  ZuCheck(hash.entries->count_() == 2 && hash.entries->findVal(1) == 30 &&
    hash.entries->findVal(2) == 25);
  ZiPB::handler<MapRow>({update, sizeof(update)}).load(row);
  ZuCheck(row.entries.count_() == 1 && row.entries.findVal(2) == 25);
  auto priorHash = hash.entries;
  ZiPB::handler<HashMapRow>({update, sizeof(update)}).load(hash);
  ZuCheck(hash.entries && hash.entries != priorHash &&
    hash.entries->count_() == 1 && hash.entries->findVal(2) == 25 &&
    priorHash->count_() == 2);

  const uint8_t child[] = {0x0a, 0x06, 0x08, 0x05, 0x12, 0x02, 0x08, 0x07};
  ZuCheck(ZiPB::verify<ChildMapRow>({child, sizeof(child)}));
  auto childRow = ZiPB::load<ChildMapRow>({child, sizeof(child)});
  ZuCheck(childRow.entries.count_() == 1 &&
    childRow.entries.findVal(5).a == 7);
  ZuCheck((ZiPB::verify<ChildMapRow, ZuFacet::NestedPB>(
    {child, sizeof(child)})));
  auto childFacet = ZiPB::load<ChildMapRow, ZuFacet::NestedPB>(
    {child, sizeof(child)});
  ZuCheck(childFacet.entries.findVal(5).a == 7);
  Output childFacetOutput;
  ZiPB::ArrayStorage childFacetStorage{childFacetOutput};
  ZiPB::Builder childFacetBuilder{childFacetStorage};
  auto childFacetWire = ZiPB::save<ZuFacet::NestedPB>(
    childFacetBuilder, childRow);
  const uint8_t childFacetExpected[] = {
    0x0a, 0x08, 0x08, 0x05, 0x12, 0x04, 0x10, 0x00, 0x08, 0x07};
  ZuCheck(!childFacetBuilder.failed() &&
    childFacetWire.length() == sizeof(childFacetExpected) &&
    !memcmp(childFacetWire.data(), childFacetExpected,
      sizeof(childFacetExpected)));
  ZuCheck((ZiPB::verify<ChildMapRow, ZuFacet::NestedPB>(childFacetWire)));
  auto childFacetRound = ZiPB::load<ChildMapRow, ZuFacet::NestedPB>(
    childFacetWire);
  ZuCheck(childFacetRound.entries.count_() == 1 &&
    childFacetRound.entries.findVal(5).a == 7 &&
    childFacetRound.entries.findVal(5).b == 0);

  const uint8_t text[] = {0x0a, 0x08,
    0x0a, 0x01, 'b', 0x12, 0x03, 't', 'w', 'o'};
  ZuCheck(ZiPB::verify<TextMapRow>({text, sizeof(text)}));
  auto textRow = ZiPB::load<TextMapRow>({text, sizeof(text)});
  ZuCheck(textRow.entries.count_() == 1 &&
    textRow.entries.findVal("b") == "two");
  Output textOutput;
  ZiPB::ArrayStorage textStorage{textOutput};
  ZiPB::Builder textBuilder{textStorage};
  auto textWire = ZiPB::save(textBuilder, textRow);
  ZuCheck(textWire.length() == sizeof(text) &&
    !memcmp(textWire.data(), text, sizeof(text)));

  uint8_t bytes[] = {
    0x0a, 0x07, 0x08, 0x08, 0x12, 0x03, 'a', 0, 'b',
    0x0a, 0x05, 0x08, 0x07, 0x12, 0x01, 'x',
    0x0a, 0x05, 0x08, 0x07, 0x12, 0x01, 'c'};
  ZuCheck(ZiPB::verify<BytesMapRow>({bytes, sizeof(bytes)}));
  auto bytesRow = ZiPB::load<BytesMapRow>({bytes, sizeof(bytes)});
  memset(bytes, 0, sizeof(bytes));
  auto bytesEntry = bytesRow.entries.find(7);
  auto bytesBinary = bytesRow.entries.find(8);
  ZuCheck(bytesRow.entries.count_() == 2 && bytesEntry && bytesBinary &&
    bytesEntry->val().length() == 1 && bytesEntry->val()[0] == 'c' &&
    bytesBinary->val().length() == 3 &&
    !memcmp(bytesBinary->val().data(), "a\0b", 3));
  Output bytesOutput;
  ZiPB::ArrayStorage bytesStorage{bytesOutput};
  ZiPB::Builder bytesBuilder{bytesStorage};
  auto bytesWire = ZiPB::save(bytesBuilder, bytesRow);
  ZuCheck(!bytesBuilder.failed() && ZiPB::verify<BytesMapRow>(bytesWire));
  auto bytesCopy = ZiPB::load<BytesMapRow>(bytesWire);
  auto bytesSaved = bytesCopy.entries.find(7);
  auto bytesSavedBinary = bytesCopy.entries.find(8);
  ZuCheck(bytesCopy.entries.count_() == 2 && bytesSaved &&
    bytesSavedBinary && bytesSaved->val().length() == 1 &&
    bytesSaved->val()[0] == 'c' &&
    bytesSavedBinary->val().length() == 3 &&
    !memcmp(bytesSavedBinary->val().data(), "a\0b", 3));

  unsigned beforePtrVal = ptrDeletes;
  {
    const uint8_t ptrValues[] = {0x0a, 0x06, 0x08, 0x01, 0x12, 0x02, 0x08, 0x07,
      0x0a, 0x06, 0x08, 0x01, 0x12, 0x02, 0x08, 0x08};
    ZuCheck(ZiPB::verify<PtrValRow>({ptrValues, sizeof(ptrValues)}));
    auto ptrRow = ZiPB::load<PtrValRow>({ptrValues, sizeof(ptrValues)});
    ZuCheck(ptrRow.entries.count_() == 1 &&
      ptrRow.entries.findVal(1)->a == 8 && ptrDeletes == beforePtrVal + 1);
    ZiPB::handler<PtrValRow>({child, sizeof(child)}).load(ptrRow);
    ZuCheck(ptrRow.entries.count_() == 1 &&
      ptrRow.entries.findVal(5)->a == 7 && ptrDeletes == beforePtrVal + 2);
  }
  ZuCheck(ptrDeletes == beforePtrVal + 3);

  unsigned beforeForeignMap = ptrDeletes;
  {
    const uint8_t duplicate[] = {
      0x0a, 0x06, 0x08, 0x01, 0x12, 0x02, 0x08, 0x07,
      0x0a, 0x06, 0x08, 0x01, 0x12, 0x02, 0x08, 0x08};
    ZuCheck(ZiPB::verify<ForeignMapParent>(
      {duplicate, sizeof(duplicate)}));
    auto foreignMap = ZiPB::load<ForeignMapParent>(
      {duplicate, sizeof(duplicate)});
    auto current = foreignMap.entries.find(1);
    ZuCheck(foreignMap.entries.count_() == 1 && current &&
      current->val()->a == 8 &&
      ptrDeletes == beforeForeignMap + 1);
    Output foreignOutput;
    ZiPB::ArrayStorage foreignStorage{foreignOutput};
    ZiPB::Builder foreignBuilder{foreignStorage};
    auto foreignWire = ZiPB::save(foreignBuilder, foreignMap);
    ZuCheck(!foreignBuilder.failed() &&
      ZiPB::verify<ForeignMapParent>(foreignWire));
    auto foreignCopy = ZiPB::load<ForeignMapParent>(foreignWire);
    auto saved = foreignCopy.entries.find(1);
    ZuCheck(foreignCopy.entries.count_() == 1 && saved &&
      saved->val()->a == 8);
    unsigned beforeReplace = ptrDeletes;
    ZiPB::handler<ForeignMapParent>({child, sizeof(child)}).load(foreignMap);
    auto replaced = foreignMap.entries.find(5);
    ZuCheck(foreignMap.entries.count_() == 1 && replaced &&
      replaced->val()->a == 7 &&
      ptrDeletes == beforeReplace + 1);
  }
  ZuCheck(ptrDeletes == beforeForeignMap + 4);
}

static void testMappedMap()
{
  ZuTestScope(testMappedMap);

  DecimalMapRow decimal;
  decimal.entries.add(7, ZuDecimal{"123.456"});
  decimal.entries.add(2, ZuDecimal{"-9.25"});
  Output output;
  ZiPB::ArrayStorage storage{output};
  ZiPB::Builder builder{storage};
  auto wire = ZiPB::save(builder, decimal);
  ZuCheck(!builder.failed() && ZiPB::verify<DecimalMapRow>(wire));
  auto round = ZiPB::load<DecimalMapRow>(wire);
  ZuCheck(round.entries.count_() == 2 &&
    round.entries.findVal(7) == ZuDecimal{"123.456"} &&
    round.entries.findVal(2) == ZuDecimal{"-9.25"});
  DecimalMapRow invalid;
  invalid.entries.add(1, ZuDecimal{ZuDecimal::Unscaled{
    ZuDecimal::maximum() + 1}});
  Output invalidOutput;
  ZiPB::ArrayStorage invalidStorage{invalidOutput};
  ZiPB::Builder invalidBuilder{invalidStorage};
  auto invalidWire = ZiPB::save(invalidBuilder, invalid);
  ZuCheck(!invalidBuilder.failed() &&
    ZiPB::verify<DecimalMapRow>(invalidWire));

  WrappedMapRow wrapped;
  wrapped.entries.add(3, WrappedWide{(uint128_t(1) << 64) | 2});
  Output wrappedOutput;
  ZiPB::ArrayStorage wrappedStorage{wrappedOutput};
  ZiPB::Builder wrappedBuilder{wrappedStorage};
  auto wrappedWire = ZiPB::save(wrappedBuilder, wrapped);
  ZuCheck(!wrappedBuilder.failed() &&
    ZiPB::verify<WrappedMapRow>(wrappedWire));
  auto wrappedRound = ZiPB::load<WrappedMapRow>(wrappedWire);
  ZuCheck(wrappedRound.entries.count_() == 1 &&
    wrappedRound.entries.findVal(3).bits == ((uint128_t(1) << 64) | 2));

  WrappedMapRow replacement;
  replacement.entries.add(3, WrappedWide{(uint128_t(4) << 64) | 5});
  Output replacementOutput;
  ZiPB::ArrayStorage replacementStorage{replacementOutput};
  ZiPB::Builder replacementBuilder{replacementStorage};
  auto replacementWire = ZiPB::save(replacementBuilder, replacement);
  ZuCheck(!replacementBuilder.failed() &&
    ZiPB::verify<WrappedMapRow>(replacementWire));
  Output duplicate;
  for (auto byte : wrappedWire) duplicate.push(byte);
  for (auto byte : replacementWire) duplicate.push(byte);
  ZuCheck(ZiPB::verify<WrappedMapRow>(duplicate));
  auto duplicateRound = ZiPB::load<WrappedMapRow>(duplicate);
  ZuCheck(duplicateRound.entries.count_() == 1 &&
    duplicateRound.entries.findVal(3).bits == ((uint128_t(4) << 64) | 5));
}

static void testMapped()
{
  ZuTestScope(testMapped);
  MappedRow input{
    -42,
    (uint128_t(0x1234) << 64) | 0x5678,
    ZuFixed{12345, 2},
    ZuDecimal{"123.456"},
    ZuTime{123, 456},
    ZuDateTime{2024, 2, 29, 12, 34, 56, 789}
  };
  Output output;
  ZiPB::ArrayStorage storage{output};
  ZiPB::Builder builder{storage};
  auto wire = ZiPB::save(builder, input);
  ZuCheck(!builder.failed() && wire.length());
  ZuCheck(ZiPB::verify<MappedRow>(wire));
  auto loaded = ZiPB::load<MappedRow>(wire);
  ZuCheck(loaded.signedWide == input.signedWide &&
    loaded.unsignedWide == input.unsignedWide);
  ZuCheck(loaded.fixed == input.fixed && loaded.decimal == input.decimal);
  ZuCheck(loaded.time == input.time && loaded.dateTime == input.dateTime);

  MappedVecRow vectors;
  vectors.signedWide.push(-42);
  vectors.signedWide.push(123);
  vectors.unsignedWide.push((uint128_t(1) << 100) | 99);
  vectors.fixed.push(ZuFixed{-12345, 3});
  vectors.fixed.push(ZuFixed{0, 2});
  vectors.decimal.push(ZuDecimal{"-123.456"});
  vectors.decimal.push(ZuDecimal{0});
  vectors.time.push(ZuTime{123, 456});
  vectors.time.push(ZuTime{int64_t(0)});
  vectors.dateTime.push(ZuDateTime{2024, 2, 29, 12, 34, 56, 789});
  Output vecOutput;
  ZiPB::ArrayStorage vecStorage{vecOutput};
  ZiPB::Builder vecBuilder{vecStorage};
  auto vecWire = ZiPB::save(vecBuilder, vectors);
  ZuCheck(!vecBuilder.failed() && ZiPB::verify<MappedVecRow>(vecWire));
  auto vecLoaded = ZiPB::load<MappedVecRow>(vecWire);
  ZuCheck(vecLoaded.signedWide == vectors.signedWide &&
    vecLoaded.unsignedWide == vectors.unsignedWide);
  ZuCheck(vecLoaded.fixed == vectors.fixed &&
    vecLoaded.decimal == vectors.decimal);
  ZuCheck(vecLoaded.time == vectors.time &&
    vecLoaded.dateTime == vectors.dateTime);

  const uint8_t merge[] = {0x0a, 0x02, 0x08, 0x01,
    0x0a, 0x02, 0x10, 0x02};
  ZuCheck(ZiPB::verify<WideRow>({merge, sizeof(merge)}));
  auto merged = ZiPB::load<WideRow>({merge, sizeof(merge)});
  ZuCheck(merged.value == ((uint128_t(1) << 64) | 2));
  WideRow existing{99};
  ZiPB::handler<WideRow>({merge, sizeof(merge)}).load(existing);
  ZuCheck(existing.value == merged.value);
  ZuCheck(ZiPB::verify<CtorWideRow>({merge, sizeof(merge)}) &&
    ZiPB::load<CtorWideRow>({merge, sizeof(merge)}).value == merged.value);
  ZuCheck(!ZiPB::verify<WideRow>({merge, sizeof(merge)}, {0}));
  ZuCheck(ZiPB::verify<WideRow>({merge, sizeof(merge)}, {1}));
  const uint8_t truncated[] = {0x0a, 0x02, 0x08, 0x80};
  ZuCheck(!ZiPB::verify<WideRow>({truncated, sizeof(truncated)}));
  const uint8_t wrongWire[] = {0x0a, 0x03, 0x0a, 0x01, 0x01};
  ZuCheck(!ZiPB::verify<WideRow>({wrongWire, sizeof(wrongWire)}));

  auto root = []<typename V>(const V &value) {
    Output bytes;
    ZiPB::ArrayStorage storage{bytes};
    ZiPB::Builder builder{storage};
    auto wire = ZiPB::save(builder, value);
    return !builder.failed() && ZiPB::verify<V>(wire) &&
      ZiPB::load<V>(wire) == value;
  };
  ZuCheck(root(input.signedWide) && root(input.unsignedWide));
  ZuCheck(root(input.fixed) && root(input.decimal));
  ZuCheck(root(input.time) && root(input.dateTime));
  ZuFixed fixedZero{0, 2};
  ZuDecimal decimalZero{0};
  ZuTime timeZero{int64_t(0)};
  ZuCheck(root(fixedZero) && root(decimalZero) && root(timeZero));
  MappedRow zeroInput{0, 0, fixedZero, decimalZero, timeZero, input.dateTime};
  Output zeroOutput;
  ZiPB::ArrayStorage zeroStorage{zeroOutput};
  ZiPB::Builder zeroBuilder{zeroStorage};
  auto zeroWire = ZiPB::save(zeroBuilder, zeroInput);
  ZuCheck(!zeroBuilder.failed() && ZiPB::verify<MappedRow>(zeroWire));
  auto zeroRound = ZiPB::load<MappedRow>(zeroWire);
  ZuCheck(zeroRound.fixed.mantissa == 0 && zeroRound.fixed.ndp == 2 &&
    zeroRound.decimal == decimalZero && zeroRound.time == timeZero);
  auto nullRoot = []<typename V>(const V &value) {
    Output bytes;
    ZiPB::ArrayStorage storage{bytes};
    ZiPB::Builder builder{storage};
    auto wire = ZiPB::save(builder, value);
    return !builder.failed() && ZiPB::verify<V>(wire);
  };
  ZuCheck(nullRoot(ZuFixed{}) && nullRoot(ZuDecimal{}) && nullRoot(ZuTime{}));

  WrappedWide wrapped{(uint128_t(1) << 64) | 2};
  Output wrappedOutput;
  ZiPB::ArrayStorage wrappedStorage{wrappedOutput};
  ZiPB::Builder wrappedBuilder{wrappedStorage};
  auto wrappedWire = ZiPB::save<ZuFacet::PrefixedPB>(wrappedBuilder, wrapped);
  ZuCheck((!wrappedBuilder.failed() &&
    ZiPB::verify<WrappedWide, ZuFacet::PrefixedPB>(wrappedWire)));
  ZuCheck((ZiPB::load<WrappedWide, ZuFacet::PrefixedPB>(wrappedWire).bits ==
    wrapped.bits));
  WrappedParent parent;
  parent.value = wrapped;
  Output parentOutput;
  ZiPB::ArrayStorage parentStorage{parentOutput};
  ZiPB::Builder parentBuilder{parentStorage};
  auto parentWire = ZiPB::save(parentBuilder, parent);
  ZuCheck(!parentBuilder.failed() && ZiPB::verify<WrappedParent>(parentWire));
  ZuCheck(ZiPB::load<WrappedParent>(parentWire).value.bits == wrapped.bits);
  ZuCheck(ZiPB::verify<WrappedParent>({merge, sizeof(merge)}) &&
    ZiPB::load<WrappedParent>({merge, sizeof(merge)}).value.bits == wrapped.bits);
  DecimalParent decimalParent{input.decimal};
  Output decimalOutput;
  ZiPB::ArrayStorage decimalStorage{decimalOutput};
  ZiPB::Builder decimalBuilder{decimalStorage};
  auto decimalWire = ZiPB::save(decimalBuilder, decimalParent);
  ZuCheck(!decimalBuilder.failed() && ZiPB::verify<DecimalParent>(decimalWire));
  ZuCheck(ZiPB::load<DecimalParent>(decimalWire).value == input.decimal);
}

static void testMappedRange()
{
  ZuTestScope(testMappedRange);
  auto root = []<typename V>(const V &value) {
    Output output;
    ZiPB::ArrayStorage storage{output};
    ZiPB::Builder builder{storage};
    auto wire = ZiPB::save(builder, value);
    return !builder.failed() && ZiPB::verify<V>(wire) &&
      ZiPB::load<V>(wire) == value;
  };
  ZuCheck(root(ZuFixed{ZuFixedMin, 0}) &&
    root(ZuFixed{ZuFixedMax, 18}));
  ZuCheck(root(ZuDecimal{ZuDecimal::Unscaled{ZuDecimal::minimum()}}) &&
    root(ZuDecimal{ZuDecimal::Unscaled{ZuDecimal::maximum()}}));

  ZiPB::Values::Fixed fixed{ZuFixedReset, 0};
  Output fixedOutput;
  ZiPB::ArrayStorage fixedStorage{fixedOutput};
  ZiPB::Builder fixedBuilder{fixedStorage};
  auto fixedWire = ZiPB::save(fixedBuilder, fixed);
  ZuCheck(!fixedBuilder.failed() && ZiPB::verify<ZuFixed>(fixedWire));
  fixed.ndp = 19;
  fixed.mantissa = 0;
  Output ndpOutput;
  ZiPB::ArrayStorage ndpStorage{ndpOutput};
  ZiPB::Builder ndpBuilder{ndpStorage};
  auto ndpWire = ZiPB::save(ndpBuilder, fixed);
  ZuCheck(!ndpBuilder.failed() && ZiPB::verify<ZuFixed>(ndpWire));

  auto invalidDecimal = ZuDecimal{ZuDecimal::Unscaled{
    ZuDecimal::maximum() + 1}};
  auto decimal = ZiPB::Values::DecimalMap::save(invalidDecimal);
  Output decimalOutput;
  ZiPB::ArrayStorage decimalStorage{decimalOutput};
  ZiPB::Builder decimalBuilder{decimalStorage};
  auto decimalWire = ZiPB::save(decimalBuilder, decimal);
  ZuCheck(!decimalBuilder.failed() &&
    ZiPB::verify<ZuDecimal>(decimalWire));

  MappedRow scalar{};
  scalar.fixed = ZuFixed{ZuFixedReset, 0};
  Output scalarOutput;
  ZiPB::ArrayStorage scalarStorage{scalarOutput};
  ZiPB::Builder scalarBuilder{scalarStorage};
  auto scalarWire = ZiPB::save(scalarBuilder, scalar);
  ZuCheck(!scalarBuilder.failed() && ZiPB::verify<MappedRow>(scalarWire));
  MappedVecRow vector;
  vector.decimal.push(invalidDecimal);
  Output vectorOutput;
  ZiPB::ArrayStorage vectorStorage{vectorOutput};
  ZiPB::Builder vectorBuilder{vectorStorage};
  auto vectorWire = ZiPB::save(vectorBuilder, vector);
  ZuCheck(!vectorBuilder.failed() && ZiPB::verify<MappedVecRow>(vectorWire));
}

static void testIP()
{
  ZuTestScope(testIP);
  const uint8_t v4[] = {0x0a, 0x04, 127, 0, 0, 1};
  const uint8_t v6[] = {0x0a, 0x10,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
  ZiIP loop4{"127.0.0.1"};
  ZiIP loop6{"::1"};
  ZuCheck(ZiPB::verify<ZiIP>({v4, sizeof(v4)}) &&
    ZiPB::load<ZiIP>({v4, sizeof(v4)}) == loop4);
  ZuCheck(ZiPB::verify<ZiIP>({v6, sizeof(v6)}) &&
    ZiPB::load<ZiIP>({v6, sizeof(v6)}) == loop6);
  Output output;
  ZiPB::ArrayStorage storage{output};
  ZiPB::Builder builder{storage};
  auto wire = ZiPB::save(builder, loop4);
  ZuCheck(!builder.failed() && wire.length() == sizeof(v4) &&
    !memcmp(wire.data(), v4, sizeof(v4)));
  Output output6;
  ZiPB::ArrayStorage storage6{output6};
  ZiPB::Builder builder6{storage6};
  auto wire6 = ZiPB::save(builder6, loop6);
  ZuCheck(!builder6.failed() && wire6.length() == sizeof(v6) &&
    !memcmp(wire6.data(), v6, sizeof(v6)));
  const uint8_t nested[] = {0x0a, sizeof(v4), 0x0a, 0x04, 127, 0, 0, 1};
  ZuCheck(ZiPB::verify<IPParent>({nested, sizeof(nested)}) &&
    ZiPB::load<IPParent>({nested, sizeof(nested)}).value == loop4);
  IPParent parent{loop4};
  Output parentOutput;
  ZiPB::ArrayStorage parentStorage{parentOutput};
  ZiPB::Builder parentBuilder{parentStorage};
  auto parentWire = ZiPB::save(parentBuilder, parent);
  ZuCheck(!parentBuilder.failed() && parentWire.length() == sizeof(nested) &&
    !memcmp(parentWire.data(), nested, sizeof(nested)));
  IPVecRow vector;
  vector.values.push(loop4);
  vector.values.push(loop6);
  Output vectorOutput;
  ZiPB::ArrayStorage vectorStorage{vectorOutput};
  ZiPB::Builder vectorBuilder{vectorStorage};
  auto vectorWire = ZiPB::save(vectorBuilder, vector);
  ZuCheck(!vectorBuilder.failed() && ZiPB::verify<IPVecRow>(vectorWire));
  auto vectorRound = ZiPB::load<IPVecRow>(vectorWire);
  ZuCheck(vectorRound.values.length() == 2 &&
    vectorRound.values[0] == loop4 && vectorRound.values[1] == loop6);
  const uint8_t badLength[] = {0x0a, 0x03, 127, 0, 1};
  ZuCheck(ZiPB::verify<ZiIP>({badLength, sizeof(badLength)}));
  const uint8_t empty[] = {0x0a, 0x00};
  ZuCheck(ZiPB::verify<ZiIP>({empty, sizeof(empty)}));
  const uint8_t duplicate[] = {0x0a, 0x04, 127, 0, 0, 1,
    0x0a, 0x10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
  ZuCheck(ZiPB::verify<ZiIP>({duplicate, sizeof(duplicate)}) &&
    ZiPB::load<ZiIP>({duplicate, sizeof(duplicate)}) == loop6);
}

static void testBitmap()
{
  ZuTestScope(testBitmap);
  const uint8_t expected[] = {0x0a, 0x10,
    1, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0};
  ZuBitmap<128> fixed;
  fixed.set(0).set(65);
  ZuCheck(ZiPB::verify<ZuBitmap<128>>({expected, sizeof(expected)}));
  auto fixedRound = ZiPB::load<ZuBitmap<128>>({expected, sizeof(expected)});
  ZuCheck(fixedRound == fixed);
  Output output;
  ZiPB::ArrayStorage storage{output};
  ZiPB::Builder builder{storage};
  auto wire = ZiPB::save(builder, fixed);
  ZuCheck(!builder.failed() && wire.length() == sizeof(expected) &&
    !memcmp(wire.data(), expected, sizeof(expected)));
  ZuBitmap<128> zero;
  Output zeroOutput;
  ZiPB::ArrayStorage zeroStorage{zeroOutput};
  ZiPB::Builder zeroBuilder{zeroStorage};
  auto zeroWire = ZiPB::save(zeroBuilder, zero);
  ZuCheck(!zeroBuilder.failed() && ZiPB::verify<ZuBitmap<128>>(zeroWire) &&
    ZiPB::load<ZuBitmap<128>>(zeroWire) == zero);

  ZtBitmap dynamic{128};
  dynamic.set(0).set(65);
  Output dynamicOutput;
  ZiPB::ArrayStorage dynamicStorage{dynamicOutput};
  ZiPB::Builder dynamicBuilder{dynamicStorage};
  auto dynamicWire = ZiPB::save(dynamicBuilder, dynamic);
  ZuCheck(!dynamicBuilder.failed() && dynamicWire.length() == sizeof(expected) &&
    !memcmp(dynamicWire.data(), expected, sizeof(expected)));
  ZuCheck(ZiPB::verify<ZtBitmap>(dynamicWire) &&
    ZiPB::load<ZtBitmap>(dynamicWire) == dynamic);
  ZtBitmap empty;
  Output emptyOutput;
  ZiPB::ArrayStorage emptyStorage{emptyOutput};
  ZiPB::Builder emptyBuilder{emptyStorage};
  auto emptyWire = ZiPB::save(emptyBuilder, empty);
  ZuCheck(!emptyBuilder.failed() && !emptyWire.length() &&
    ZiPB::verify<ZtBitmap>(emptyWire) &&
    !ZiPB::load<ZtBitmap>(emptyWire).length());

  BitmapRow row{fixed, dynamic};
  Output rowOutput;
  ZiPB::ArrayStorage rowStorage{rowOutput};
  ZiPB::Builder rowBuilder{rowStorage};
  auto rowWire = ZiPB::save(rowBuilder, row);
  ZuCheck(!rowBuilder.failed() && ZiPB::verify<BitmapRow>(rowWire));
  auto rowRound = ZiPB::load<BitmapRow>(rowWire);
  ZuCheck(rowRound.fixed == fixed && rowRound.dynamic == dynamic);
  BitmapVecRow vector;
  vector.values.push(dynamic);
  vector.values.push(ZtBitmap{128});
  Output vectorOutput;
  ZiPB::ArrayStorage vectorStorage{vectorOutput};
  ZiPB::Builder vectorBuilder{vectorStorage};
  auto vectorWire = ZiPB::save(vectorBuilder, vector);
  ZuCheck(!vectorBuilder.failed() && ZiPB::verify<BitmapVecRow>(vectorWire));
  auto vectorRound = ZiPB::load<BitmapVecRow>(vectorWire);
  ZuCheck(vectorRound.values.length() == 2 &&
    vectorRound.values[0] == dynamic &&
    vectorRound.values[1].length() == 128 && !vectorRound.values[1]);
  const uint8_t shortFixed[] = {0x0a, 0x08, 1, 0, 0, 0, 0, 0, 0, 0};
  ZuCheck(ZiPB::verify<ZuBitmap<128>>({shortFixed, sizeof(shortFixed)}));
  const uint8_t shortWord[] = {0x0a, 0x01, 1};
  ZuCheck(ZiPB::verify<ZtBitmap>({shortWord, sizeof(shortWord)}));
}

static void testHwBitmap()
{
  ZuTestScope(testHwBitmap);
  const uint8_t expected[] = {0x0a, 0x08,
    '3', '-', '5', ',', '7', ',', '9', '-'};
  ZmBitmap bits{"3-5,7,9-"};
  ZuCheck(ZiPB::verify<ZmBitmap>({expected, sizeof(expected)}) &&
    ZiPB::load<ZmBitmap>({expected, sizeof(expected)}) == bits);
  Output output;
  ZiPB::ArrayStorage storage{output};
  ZiPB::Builder builder{storage};
  auto wire = ZiPB::save(builder, bits);
  ZuCheck(!builder.failed() && wire.length() == sizeof(expected) &&
    !memcmp(wire.data(), expected, sizeof(expected)));
  const uint8_t finite[] = {0x0a, 0x05, '3', '-', '5', ',', '7'};
  ZuCheck(ZiPB::verify<ZmBitmap>({finite, sizeof(finite)}) &&
    ZiPB::load<ZmBitmap>({finite, sizeof(finite)}) == ZmBitmap{"3-5,7"});
  ZmBitmap empty;
  Output emptyOutput;
  ZiPB::ArrayStorage emptyStorage{emptyOutput};
  ZiPB::Builder emptyBuilder{emptyStorage};
  auto emptyWire = ZiPB::save(emptyBuilder, empty);
  ZuCheck(!emptyBuilder.failed() && !emptyWire.length() &&
    ZiPB::verify<ZmBitmap>(emptyWire) &&
    ZiPB::load<ZmBitmap>(emptyWire) == empty);
  HwBitmapRow row{bits};
  Output rowOutput;
  ZiPB::ArrayStorage rowStorage{rowOutput};
  ZiPB::Builder rowBuilder{rowStorage};
  auto rowWire = ZiPB::save(rowBuilder, row);
  ZuCheck(!rowBuilder.failed() && ZiPB::verify<HwBitmapRow>(rowWire) &&
    ZiPB::load<HwBitmapRow>(rowWire).value == bits);
  HwBitmapVecRow vector;
  vector.values.push(empty);
  vector.values.push(bits);
  Output vectorOutput;
  ZiPB::ArrayStorage vectorStorage{vectorOutput};
  ZiPB::Builder vectorBuilder{vectorStorage};
  auto vectorWire = ZiPB::save(vectorBuilder, vector);
  ZuCheck(!vectorBuilder.failed() && ZiPB::verify<HwBitmapVecRow>(vectorWire));
  auto vectorRound = ZiPB::load<HwBitmapVecRow>(vectorWire);
  ZuCheck(vectorRound.values.length() == 2 &&
    vectorRound.values[0] == empty && vectorRound.values[1] == bits);
  const uint8_t invalid[] = {0x0a, 0x03, '1', '-', 'x'};
  ZuCheck(ZiPB::verify<ZmBitmap>({invalid, sizeof(invalid)}));
}

static void testUnion()
{
  ZuTestScope(testUnion);
  ChoiceRow row;
  row.id = 3;
  row.choice.p<2>(ChoiceA{7, 8});
  Output output;
  ZiPB::ArrayStorage storage{output};
  ZiPB::Builder builder{storage};
  auto wire = ZiPB::save(builder, row);
  const uint8_t expected[] = {0x08, 0x03, 0x32, 0x04,
    0x08, 0x07, 0x10, 0x08};
  ZuCheck(!builder.failed() && wire.length() == sizeof(expected) &&
    !memcmp(wire.data(), expected, sizeof(expected)) &&
    ZiPB::verify<ChoiceRow>(wire));
  auto round = ZiPB::load<ChoiceRow>(wire);
  ZuCheck(round.id == 3 && round.choice.type() == 2 &&
    round.choice.p<2>().a == 7 && round.choice.p<2>().b == 8);

  const uint8_t merged[] = {0x22, 0x02, 0x08, 0x01,
    0x22, 0x02, 0x10, 0x02};
  ZuCheck(ZiPB::verify<ChoiceRow>({merged, sizeof(merged)}));
  auto joined = ZiPB::load<ChoiceRow>({merged, sizeof(merged)});
  ZuCheck(joined.choice.type() == 1 && joined.choice.p<1>().a == 1 &&
    joined.choice.p<1>().b == 2);

  const uint8_t switched[] = {0x22, 0x02, 0x08, 0x01,
    0x4a, 0x02, 0x08, 0x09, 0x22, 0x02, 0x10, 0x04};
  ZuCheck(ZiPB::verify<ChoiceRow>({switched, sizeof(switched)}));
  auto last = ZiPB::load<ChoiceRow>({switched, sizeof(switched)});
  ZuCheck(last.choice.type() == 1 && last.choice.p<1>().a == 0 &&
    last.choice.p<1>().b == 4);
  const uint8_t pointerMerge[] = {
    0x22, 0x06, 0x0a, 0x02, 0x08, 0x01, 0x10, 0x03,
    0x22, 0x06, 0x0a, 0x02, 0x10, 0x02, 0x10, 0x04};
  ZuCheck(ZiPB::verify<PtrChoiceRow>(
    {pointerMerge, sizeof(pointerMerge)}));
  auto mergedPtr = ZiPB::load<PtrChoiceRow>(
    {pointerMerge, sizeof(pointerMerge)});
  ZuCheck(mergedPtr.choice.type() == 1 &&
    mergedPtr.choice.p<1>().child &&
    mergedPtr.choice.p<1>().child->a == 1 &&
    mergedPtr.choice.p<1>().child->b == 2 &&
    mergedPtr.choice.p<1>().values.length() == 2 &&
    mergedPtr.choice.p<1>().values[0] == 3 &&
    mergedPtr.choice.p<1>().values[1] == 4);
  const uint8_t pointerSwitch[] = {
    0x22, 0x04, 0x0a, 0x02, 0x08, 0x01,
    0x4a, 0x02, 0x08, 0x03};
  unsigned beforeSwitch = ptrDeletes;
  ZuCheck(ZiPB::verify<PtrChoiceRow>(
    {pointerSwitch, sizeof(pointerSwitch)}));
  auto switchedPtr = ZiPB::load<PtrChoiceRow>(
    {pointerSwitch, sizeof(pointerSwitch)});
  ZuCheck(switchedPtr.choice.type() == 2 &&
    switchedPtr.choice.p<2>().value == 3 &&
    ptrDeletes == beforeSwitch + 1);
  const uint8_t ctorMerge[] = {
    0x22, 0x04, 0x08, 0x01, 0x10, 0x07,
    0x22, 0x02, 0x08, 0x02};
  ZuCheck(ZiPB::verify<CtorChoiceParent>({ctorMerge, sizeof(ctorMerge)}));
  auto mergedCtor = ZiPB::load<CtorChoiceParent>(
    {ctorMerge, sizeof(ctorMerge)});
  ZuCheck(mergedCtor.choice.type() == 1 &&
    mergedCtor.choice.p<1>().id == 1 &&
    mergedCtor.choice.p<1>().value == 7);
  const uint8_t childMerge[] = {
    0x0a, 0x04, 0x08, 0x01, 0x10, 0x07,
    0x0a, 0x02, 0x08, 0x02};
  ZuCheck(ZiPB::verify<CtorChildParent>(
    {childMerge, sizeof(childMerge)}));
  auto mergedChild = ZiPB::load<CtorChildParent>(
    {childMerge, sizeof(childMerge)});
  ZuCheck(mergedChild.child && mergedChild.child->id == 1 &&
    mergedChild.child->value == 7);
  auto resetChild = ZiPB::load<CtorChildResetParent>(
    {childMerge, sizeof(childMerge)});
  ZuCheck(resetChild.child && resetChild.child->id == 1 &&
    resetChild.child->value == 7);
  ZiPB::handler<CtorChildResetParent>({}).update(resetChild);
  ZuCheck(!resetChild.child);
  auto mergedChildValue = ZiPB::load<CtorChildValueParent>(
    {childMerge, sizeof(childMerge)});
  ZuCheck(mergedChildValue.child.id == 1 &&
    mergedChildValue.child.value == 7);
  auto mergedChildCtor = ZiPB::load<CtorChildCtorParent>(
    {childMerge, sizeof(childMerge)});
  ZuCheck(mergedChildCtor.child && mergedChildCtor.child->id == 1 &&
    mergedChildCtor.child->value == 7);
  Output ctorOutput;
  ZiPB::ArrayStorage ctorStorage{ctorOutput};
  ZiPB::Builder ctorBuilder{ctorStorage};
  auto ctorWire = ZiPB::save(ctorBuilder, mergedCtor);
  const uint8_t ctorExpected[] = {0x22, 0x04, 0x08, 0x01, 0x10, 0x07};
  ZuCheck(!ctorBuilder.failed() && ctorWire.length() == sizeof(ctorExpected) &&
    !memcmp(ctorWire.data(), ctorExpected, sizeof(ctorExpected)));
  auto constructedCtor = ZiPB::load<CtorChoiceCtorParent>(
    {ctorMerge, sizeof(ctorMerge)});
  ZuCheck(constructedCtor.choice.type() == 1 &&
    constructedCtor.choice.p<1>().id == 1 &&
    constructedCtor.choice.p<1>().value == 7);
  const uint8_t ctorLoad[] = {0x08, 0x09, 0x10, 0x0b};
  auto ctorHandler = ZiPB::handler<CtorChoiceAlt>({ctorLoad, sizeof(ctorLoad)});
  ctorHandler.load(mergedCtor.choice.p<1>());
  ZuCheck(mergedCtor.choice.p<1>().id == 1 &&
    mergedCtor.choice.p<1>().value == 11);
  ctorHandler.update(constructedCtor.choice.p<1>());
  ZuCheck(constructedCtor.choice.p<1>().id == 1 &&
    constructedCtor.choice.p<1>().value == 11);
  const uint8_t ctorSwitch[] = {
    0x22, 0x02, 0x08, 0x01,
    0x4a, 0x02, 0x08, 0x09,
    0x22, 0x02, 0x08, 0x02};
  ZuCheck(ZiPB::verify<CtorChoiceParent>(
    {ctorSwitch, sizeof(ctorSwitch)}));
  auto switchedCtor = ZiPB::load<CtorChoiceParent>(
    {ctorSwitch, sizeof(ctorSwitch)});
  ZuCheck(switchedCtor.choice.type() == 1 &&
    switchedCtor.choice.p<1>().id == 2 &&
    switchedCtor.choice.p<1>().value == 0);
  const uint8_t malformedDiscarded[] = {
    0x22, 0x02, 0x08, 0x80, 0x4a, 0x02, 0x08, 0x09};
  ZuCheck(!ZiPB::verify<ChoiceRow>(
    {malformedDiscarded, sizeof(malformedDiscarded)}));
  const uint8_t absentDiscardedRequired[] = {
    0x22, 0x00, 0x4a, 0x02, 0x08, 0x09};
  ZuCheck(ZiPB::verify<DiscardedRequiredRow>(
    {absentDiscardedRequired, sizeof(absentDiscardedRequired)}));
  auto ctor = ZiPB::load<CtorChoiceRow>({merged, sizeof(merged)});
  ZuCheck(ctor.choice.type() == 1 && ctor.choice.p<1>().a == 1 &&
    ctor.choice.p<1>().b == 2);

  const uint8_t wrongWire[] = {0x20, 0x01};
  ZuCheck(!ZiPB::verify<ChoiceRow>({wrongWire, sizeof(wrongWire)}));
  RequiredChoiceRow required;
  Output requiredOutput;
  ZiPB::ArrayStorage requiredStorage{requiredOutput};
  ZiPB::Builder requiredBuilder{requiredStorage};
  ZuCheck(!ZiPB::save(requiredBuilder, required).length() &&
    requiredBuilder.error() == ZiPB::Error::Required &&
    !ZiPB::verify<RequiredChoiceRow>({}));
  required.choice.p<3>(ChoiceB{});
  Output selectedOutput;
  ZiPB::ArrayStorage selectedStorage{selectedOutput};
  ZiPB::Builder selectedBuilder{selectedStorage};
  auto selected = ZiPB::save(selectedBuilder, required);
  const uint8_t selectedBytes[] = {0x4a, 0x02, 0x08, 0x00};
  ZuCheck(!selectedBuilder.failed());
  ZuCheck(selected.length() == sizeof(selectedBytes));
  ZuCheck(!memcmp(selected.data(), selectedBytes, sizeof(selectedBytes)));
  ZuCheck(ZiPB::verify<RequiredChoiceRow>(selected));

  ChoiceRow faceted;
  faceted.id = 3;
  faceted.choice.p<1>(ChoiceA{7, 8});
  Output facetOutput;
  ZiPB::ArrayStorage facetStorage{facetOutput};
  ZiPB::Builder facetBuilder{facetStorage};
  auto facetWire = ZiPB::save<ZuFacet::NestedPB>(facetBuilder, faceted);
  const uint8_t facetExpected[] = {0x08, 0x03, 0x22, 0x02, 0x10, 0x08};
  ZuCheck(!facetBuilder.failed() && facetWire.length() == sizeof(facetExpected) &&
    !memcmp(facetWire.data(), facetExpected, sizeof(facetExpected)));
  ZuCheck((ZiPB::verify<ChoiceRow, ZuFacet::NestedPB>(facetWire)));
  auto facetRound = ZiPB::load<ChoiceRow, ZuFacet::NestedPB>(facetWire);
  ZuCheck(facetRound.id == 3 && facetRound.choice.type() == 1 &&
    facetRound.choice.p<1>().a == 0 && facetRound.choice.p<1>().b == 8);
}

static void testMappedCtor()
{
  ZuTestScope(testMappedCtor);
  const uint8_t merged[] = {
    0x0a, 0x04, 0x08, 0x01, 0x10, 0x07,
    0x0a, 0x02, 0x08, 0x02};
  ZuBSpan wire{merged, sizeof(merged)};
  unsigned before = mappedLoads;
  ZuCheck(ZiPB::verify<WrappedCtorParent>(wire));
  ZuCheck(mappedLoads == before);
  auto parent = ZiPB::load<WrappedCtorParent>(wire);
  ZuCheck(parent.child.id == 1 && parent.child.value == 7);
  Output output;
  ZiPB::ArrayStorage storage{output};
  ZiPB::Builder builder{storage};
  auto saved = ZiPB::save(builder, parent);
  ZuCheck(!builder.failed() && ZiPB::verify<WrappedCtorParent>(saved));
  const uint8_t expected[] = {0x0a, 0x04, 0x08, 0x01, 0x10, 0x07};
  ZuCheck(saved.length() == sizeof(expected) &&
    !memcmp(saved.data(), expected, sizeof(expected)));
  const uint8_t invalid[] = {0x0a, 0x02, 0x08, 0x00};
  ZuCheck(ZiPB::verify<WrappedCtorParent>({invalid, sizeof(invalid)}));
}

static void testPacked()
{
  ZuTestScope(testPacked);
  PackedRow row;
  row.zig.push(-1);
  row.zig.push(0);
  row.zig.push(1);
  row.fixed.push(1);
  row.fixed.push(2);
  row.floats.push(1.5f);
  row.floats.push(-2.0f);
  row.flags.push(0);
  row.flags.push(2);
  row.nums.push(1);
  row.nums.push(2);
  Output output;
  ZiPB::ArrayStorage storage{output};
  ZiPB::Builder builder{storage};
  auto wire = ZiPB::save(builder, row);
  const uint8_t expected[] = {
    0x0a, 0x03, 0x01, 0x00, 0x02,
    0x12, 0x08, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
    0x1a, 0x08, 0x00, 0x00, 0xc0, 0x3f, 0x00, 0x00, 0x00, 0xc0,
    0x22, 0x02, 0x00, 0x01,
    0x2a, 0x02, 0x01, 0x02};
  ZuCheck(!builder.failed() && wire.length() == sizeof(expected) &&
    !memcmp(wire.data(), expected, sizeof(expected)) &&
    ZiPB::verify<PackedRow>(wire));
  auto loaded = ZiPB::load<PackedRow>(wire);
  ZuCheck(loaded.zig.length() == 3 && loaded.zig[0] == -1 &&
    loaded.zig[1] == 0 && loaded.zig[2] == 1 &&
    loaded.fixed.length() == 2 && loaded.fixed[0] == 1 &&
    loaded.fixed[1] == 2 && loaded.floats.length() == 2 &&
    loaded.floats[0] == 1.5f && loaded.floats[1] == -2.0f &&
    loaded.flags.length() == 2 && !loaded.flags[0] && loaded.flags[1] == 1 &&
    loaded.nums.length() == 2 && loaded.nums[0] == 1 && loaded.nums[1] == 2);
  PackedRow empty;
  Output emptyOutput;
  ZiPB::ArrayStorage emptyStorage{emptyOutput};
  ZiPB::Builder emptyBuilder{emptyStorage};
  ZuCheck(!ZiPB::save(emptyBuilder, empty).length() &&
    !emptyBuilder.failed());
}

static void testZigZag()
{
  ZuTestScope(testZigZag);
  ZigRow row{-1, -2};
  row.values.push(-1);
  row.values.push(0);
  row.values.push(1);
  Output output;
  ZiPB::ArrayStorage storage{output};
  ZiPB::Builder builder{storage};
  auto wire = ZiPB::save(builder, row);
  const uint8_t expected[] = {0x08, 0x01, 0x10, 0x03,
    0x18, 0x01, 0x18, 0x00, 0x18, 0x02};
  ZuCheck(!builder.failed() && wire.length() == sizeof(expected) &&
    !memcmp(wire.data(), expected, sizeof(expected)) &&
    ZiPB::verify<ZigRow>(wire));
  auto copy = ZiPB::load<ZigRow>(wire);
  ZuCheck(copy.small == -1 && copy.large == -2 &&
    copy.values.length() == 3 && copy.values[0] == -1 &&
    copy.values[1] == 0 && copy.values[2] == 1);

  const uint8_t packed[] = {0x08, 0x01, 0x10, 0x03,
    0x1a, 0x03, 0x01, 0x00, 0x02};
  ZuCheck(ZiPB::verify<ZigRow>({packed, sizeof(packed)}));
  auto packedRow = ZiPB::load<ZigRow>({packed, sizeof(packed)});
  ZuCheck(packedRow.values.length() == 3 && packedRow.values[0] == -1 &&
    packedRow.values[1] == 0 && packedRow.values[2] == 1);

  const uint8_t narrow[] = {0x08, 0x80, 0x80, 0x80, 0x80, 0x10};
  ZuCheck(!ZiPB::verify<ZigRow>({narrow, sizeof(narrow)}));
  const uint8_t sentinel[] = {0x08, 0xff, 0xff, 0xff, 0xff, 0x0f};
  ZuCheck(ZiPB::verify<ZigRow>({sentinel, sizeof(sentinel)}));
}

static void testFixedWire()
{
  ZuTestScope(testFixedWire);
  FixedRow row;
  row.signed32 = -2;
  row.unsigned32 = 0x12345678;
  row.signed64 = -2;
  row.unsigned64 = 0x0102030405060708;
  row.values.push(-2);
  row.values.push(3);
  Output output;
  ZiPB::ArrayStorage storage{output};
  ZiPB::Builder builder{storage};
  auto wire = ZiPB::save(builder, row);
  const uint8_t expected[] = {
    0x0d, 0xfe, 0xff, 0xff, 0xff,
    0x15, 0x78, 0x56, 0x34, 0x12,
    0x19, 0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x21, 0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01,
    0x2d, 0xfe, 0xff, 0xff, 0xff,
    0x2d, 0x03, 0x00, 0x00, 0x00};
  ZuCheck(!builder.failed() && wire.length() == sizeof(expected) &&
    !memcmp(wire.data(), expected, sizeof(expected)) &&
    ZiPB::verify<FixedRow>(wire));
  auto copy = ZiPB::load<FixedRow>(wire);
  ZuCheck(copy.signed32 == -2 && copy.unsigned32 == 0x12345678 &&
    copy.signed64 == -2 && copy.unsigned64 == 0x0102030405060708 &&
    copy.values.length() == 2 && copy.values[0] == -2 &&
    copy.values[1] == 3);

  const uint8_t packed[] = {0x2a, 0x08,
    0xfe, 0xff, 0xff, 0xff, 0x03, 0, 0, 0};
  ZuCheck(ZiPB::verify<FixedRow>({packed, sizeof(packed)}));
  auto packedRow = ZiPB::load<FixedRow>({packed, sizeof(packed)});
  ZuCheck(packedRow.values.length() == 2 &&
    packedRow.values[0] == -2 && packedRow.values[1] == 3);
  const uint8_t wrongWire[] = {0x08, 0x01};
  ZuCheck(!ZiPB::verify<FixedRow>({wrongWire, sizeof(wrongWire)}));
  const uint8_t shortPacked[] = {0x2a, 0x03, 1, 2, 3};
  ZuCheck(!ZiPB::verify<FixedRow>({shortPacked, sizeof(shortPacked)}));
}

static void testNarrowTypes()
{
  ZuTestScope(testNarrowTypes);
  NarrowRow scalar{1, 2, 3, 4, 5, 6};
  const uint8_t scalarBytes[] = {
    0x08, 1, 0x10, 2, 0x18, 3, 0x20, 4, 0x28, 5, 0x30, 6};
  ZuCheck(ZiPB::verify<NarrowRow>({scalarBytes, sizeof(scalarBytes)}));
  auto scalarCopy = ZiPB::load<NarrowRow>({scalarBytes, sizeof(scalarBytes)});
  ZuCheck(scalarCopy.signed8 == 1 && scalarCopy.unsigned8 == 2 &&
    scalarCopy.signed16 == 3 && scalarCopy.unsigned16 == 4 &&
    scalarCopy.signed64 == 5 && scalarCopy.unsigned64 == 6);
  Output scalarOutput;
  ZiPB::ArrayStorage scalarStorage{scalarOutput};
  ZiPB::Builder scalarBuilder{scalarStorage};
  auto encoded = ZiPB::save(scalarBuilder, scalar);
  ZuCheck(!scalarBuilder.failed() && encoded.length() == sizeof(scalarBytes) &&
    !memcmp(encoded.data(), scalarBytes, sizeof(scalarBytes)));

  NarrowVecRow vectors;
  vectors.unsigned8.push(7);
  vectors.signed16.push(8);
  vectors.unsigned16.push(9);
  vectors.signed64.push(10);
  vectors.unsigned32.push(11);
  const uint8_t vectorBytes[] = {
    0x08, 7, 0x10, 8, 0x18, 9, 0x20, 10, 0x28, 11};
  ZuCheck(ZiPB::verify<NarrowVecRow>({vectorBytes, sizeof(vectorBytes)}));
  auto vectorCopy = ZiPB::load<NarrowVecRow>({vectorBytes, sizeof(vectorBytes)});
  ZuCheck(vectorCopy.unsigned8.length() == 1 &&
    vectorCopy.unsigned8[0] == 7 && vectorCopy.signed16.length() == 1 &&
    vectorCopy.signed16[0] == 8 && vectorCopy.unsigned16.length() == 1 &&
    vectorCopy.unsigned16[0] == 9 && vectorCopy.signed64.length() == 1 &&
    vectorCopy.signed64[0] == 10 && vectorCopy.unsigned32.length() == 1 &&
    vectorCopy.unsigned32[0] == 11);
  Output vectorOutput;
  ZiPB::ArrayStorage vectorStorage{vectorOutput};
  ZiPB::Builder vectorBuilder{vectorStorage};
  auto vectorWire = ZiPB::save(vectorBuilder, vectors);
  ZuCheck(!vectorBuilder.failed() &&
    vectorWire.length() == sizeof(vectorBytes) &&
    !memcmp(vectorWire.data(), vectorBytes, sizeof(vectorBytes)));

  const uint8_t packed[] = {0x0a, 0x02, 7, 8};
  ZuCheck(ZiPB::verify<NarrowVecRow>({packed, sizeof(packed)}));
  auto packedRow = ZiPB::load<NarrowVecRow>({packed, sizeof(packed)});
  ZuCheck(packedRow.unsigned8.length() == 2 &&
    packedRow.unsigned8[0] == 7 && packedRow.unsigned8[1] == 8);
  const uint8_t wide8[] = {0x08, 0x80, 0x02};
  ZuCheck(!ZiPB::verify<NarrowVecRow>({wide8, sizeof(wide8)}));
  const uint8_t wide16[] = {0x20, 0x80, 0x80, 0x04};
  ZuCheck(!ZiPB::verify<NarrowRow>({wide16, sizeof(wide16)}));
}

static void testBoolVec()
{
  ZuTestScope(testBoolVec);
  const uint8_t unpacked[] = {0x08, 0, 0x08, 1, 0x08, 0xac, 0x02};
  ZuCheck(ZiPB::verify<BoolVecRow>({unpacked, sizeof(unpacked)}));
  auto row = ZiPB::load<BoolVecRow>({unpacked, sizeof(unpacked)});
  ZuCheck(row.values.length() == 3 && row.values[0] == 0 &&
    row.values[1] == 1 && row.values[2] == 1);
  const uint8_t packed[] = {0x0a, 0x04, 0, 1, 0xac, 0x02};
  ZuCheck(ZiPB::verify<BoolVecRow>({packed, sizeof(packed)}));
  auto packedRow = ZiPB::load<BoolVecRow>({packed, sizeof(packed)});
  ZuCheck(packedRow.values.length() == 3 && packedRow.values[0] == 0 &&
    packedRow.values[1] == 1 && packedRow.values[2] == 1);
  const uint8_t malformed[] = {0x0a, 0x01, 0x80};
  ZuCheck(!ZiPB::verify<BoolVecRow>({malformed, sizeof(malformed)}));
  row.values.push(255);
  Output output;
  ZiPB::ArrayStorage storage{output};
  ZiPB::Builder builder{storage};
  auto wire = ZiPB::save(builder, row);
  const uint8_t expected[] = {0x08, 0, 0x08, 1, 0x08, 1, 0x08, 1};
  ZuCheck(!builder.failed() && wire.length() == sizeof(expected) &&
    !memcmp(wire.data(), expected, sizeof(expected)));
}

int main(int argc, char **argv)
{
  ZuTestMain();
  ZuTestCall(testImplicitIDs);
  ZuTestCall(testVarint);
  ZuTestCall(testFields);
  ZuTestCall(testBuilder);
  ZuTestCall(testRow);
  ZuTestCall(testFloat);
  ZuTestCall(testFloat32);
  ZuTestCall(testAdapterRules);
  ZuTestCall(testCtor);
  ZuTestCall(testBytes);
  ZuTestCall(testNested);
  ZuTestCall(testVectors);
  ZuTestCall(testTextVectors);
  ZuTestCall(testCString);
  ZuTestCall(testCStringVec);
  ZuTestCall(testPointer);
  ZuTestCall(testEnum);
  ZuTestCall(testArrays);
  ZuTestCall(testMap);
  ZuTestCall(testMappedMap);
  ZuTestCall(testMapped);
  ZuTestCall(testMappedRange);
  ZuTestCall(testIP);
  ZuTestCall(testBitmap);
  ZuTestCall(testHwBitmap);
  ZuTestCall(testUnion);
  ZuTestCall(testMappedCtor);
  ZuTestCall(testPacked);
  ZuTestCall(testZigZag);
  ZuTestCall(testFixedWire);
  ZuTestCall(testNarrowTypes);
  ZuTestCall(testBoolVec);
}
