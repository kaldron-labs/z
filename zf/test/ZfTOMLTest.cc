//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdio.h>

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmAlloc.hh>
#include <zlib/ZmRBTree.hh>

#include <zlib/ZfCf.hh>
#include <zlib/ZfTOML.hh>

#include "ZfHeapTest.hh"
#include "ZfMapTest.hh"

using namespace ZuTestUtil;

#ifndef ZF_TOML_TEST_SRCDIR
#define ZF_TOML_TEST_SRCDIR "zf/test"
#endif

ZtEnumNS(, TOMLValues, int8_t, High, Low, Normal);
namespace TOMLFlags {
  ZtFlags(, Flags, uint8_t, Bit0, Bit1, Bit2);
}
ZtEnumImplNS(TOMLValues);
ZtEnumImplNS(TOMLFlags);

namespace ZuFieldProp::TOML {
  using Unix9 = Unix<ZfTOML::Sec, 9>;
  using CSV9 = CSV<ZfTOML::Sec, 9>;
}

static_assert(ZuIs_<ZfTOML::AnyNode, ZfTree::AnyNode>{});
static_assert(ZuIs_<ZfTOML::AnyNode, ZfCf::AnyNode>{});
static_assert(ZfTOML::ScalarN == 5); // extend the exhaustive matrix below
static_assert(ZuIsSame<decltype(&ZfTOML::TOMLPolicy::scalar),
  bool (*)(int, ZfTreeLoad::ScalarMask::T)>{});
static_assert(ZuFieldProp::TOML::GetScalarFmt<ZuTypeList<>>{} ==
  ZfTOML::NativeScalar);
static_assert(ZuFieldProp::TOML::GetScalarFmt<
  ZuTypeList<ZuFieldProp::TOML::Literal>>{} == ZfTOML::LiteralScalar);
static_assert(ZuFieldProp::TOML::GetScalarFmt<
  ZuTypeList<ZuFieldProp::TOML::Basic>>{} == ZfTOML::BasicScalar);
static_assert(ZuFieldProp::TOML::GetScalarFmt<
  ZuTypeList<ZuFieldProp::TOML::MultilineBasic>>{} ==
    ZfTOML::MultilineBasicScalar);
static_assert(ZuFieldProp::TOML::GetScalarFmt<
  ZuTypeList<ZuFieldProp::TOML::MultilineLiteral>>{} ==
    ZfTOML::MultilineLiteralScalar);
static_assert(ZuFieldProp::TOML::GetBytesFmt<ZuTypeList<>>{} ==
  ZfTOML::Base64);
static_assert(ZuFieldProp::TOML::GetBytesFmt<
  ZuTypeList<ZuFieldProp::TOML::Base64URL>>{} == ZfTOML::Base64URL);
static_assert(ZuFieldProp::TOML::GetBytesFmt<
  ZuTypeList<ZuFieldProp::TOML::Base32>>{} == ZfTOML::Base32);
static_assert(ZuFieldProp::TOML::GetBytesFmt<
  ZuTypeList<ZuFieldProp::TOML::Hex>>{} == ZfTOML::Hex);
static_assert(ZuFieldProp::TOML::GetBytesFmt<
  ZuTypeList<ZuFieldProp::TOML::Raw>>{} == ZfTOML::Raw);
static_assert(!ZuFieldProp::TOML::GetNumberFmt<ZuTypeList<>>::String);
static_assert(ZuFieldProp::TOML::GetNumberFmt<
  ZuTypeList<ZuFieldProp::TOML::String<>>>::String);
static_assert(ZuFieldProp::TOML::GetTimeFmt<ZuTypeList<>>::Fmt == ZfTOML::ISO);
static_assert(ZuFieldProp::TOML::GetTimeFmt<
  ZuTypeList<ZuFieldProp::TOML::Unix<ZfTOML::Sec, 9>>>::Fmt ==
    ZfTOML::Unix);
static_assert(!ZuFieldProp::TOML::GetOptional<ZuTypeList<>>{});
static_assert(ZuFieldProp::TOML::GetOptional<
  ZuTypeList<ZuFieldProp::TOML::Opt>>{});
static_assert(!ZfTOML::ScalarFmtValid<ZfFieldTC::Bool,
  ZuTypeList<ZuFieldProp::TOML::Basic>>{});
static_assert(!ZfTOML::ScalarFmtValid<ZfFieldTC::Int32,
  ZuTypeList<ZuFieldProp::TOML::Basic>>{});
static_assert(ZfTOML::ScalarFmtValid<ZfFieldTC::Int32,
  ZuTypeList<ZuFieldProp::TOML::String<>, ZuFieldProp::TOML::Basic>>{});
static_assert(ZfTOML::ScalarFmtValid<ZfFieldTC::Int32,
  ZuTypeList<ZuFieldProp::Enum<TOMLValues::Map>,
    ZuFieldProp::TOML::Basic>>{});
static_assert(!ZfTOML::ScalarFmtValid<ZfFieldTC::DateTime,
  ZuTypeList<ZuFieldProp::TOML::Basic>>{});
static_assert(ZfTOML::ScalarFmtValid<ZfFieldTC::DateTime,
  ZuTypeList<ZuFieldProp::TOML::CSV<ZfTOML::Sec, 3>,
    ZuFieldProp::TOML::Basic>>{});

template <typename Style>
struct TOMLExplicitNativeInvalid : public ZuBool<
    !ZfTOML::ScalarFmtValid<ZfFieldTC::Bool, ZuTypeList<Style>>{} &&
    !ZfTOML::ScalarFmtValid<ZfFieldTC::Int32, ZuTypeList<Style>>{} &&
    !ZfTOML::ScalarFmtValid<ZfFieldTC::DateTime, ZuTypeList<Style>>{}> { };

static_assert(TOMLExplicitNativeInvalid<ZuFieldProp::TOML::Basic>{});
static_assert(TOMLExplicitNativeInvalid<ZuFieldProp::TOML::Literal>{});
static_assert(TOMLExplicitNativeInvalid<
  ZuFieldProp::TOML::MultilineBasic>{});
static_assert(TOMLExplicitNativeInvalid<
  ZuFieldProp::TOML::MultilineLiteral>{});
static_assert(ZfTOML::ScalarFmtValid<ZfFieldTC::Bool,
  ZuTypeList<ZuFieldProp::TOML::Native>>{});
static_assert(ZfTOML::ScalarFmtValid<ZfFieldTC::Int32,
  ZuTypeList<ZuFieldProp::TOML::Native>>{});
static_assert(ZfTOML::ScalarFmtValid<ZfFieldTC::DateTime,
  ZuTypeList<ZuFieldProp::TOML::Native>>{});

struct TOMLNested { int value = 0; };
ZfStruct((TOMLNested, TOML),
  (((value), (Ctor<0>)), (Int32)));

using TOMLMapKey = ZtString<>;

using TOMLIntMap_ =
  ZmRBTreeKV<TOMLMapKey, int, ZmRBTreeUnique<true>>;
using TOMLIntMap = ZfMapTest<
  "ZfTest.TOML.IntMap", ZfRefMapTest<TOMLIntMap_>>;
using TOMLIntMapRef = ZmRef<TOMLIntMap>;
inline ZfTOML::AsMap<ZfFieldTC::Int32> ZfTOML_Fmt(TOMLIntMap *);

using TOMLObjMap_ =
  ZmRBTreeKV<TOMLMapKey, TOMLNested, ZmRBTreeUnique<true>>;
using TOMLObjMap = ZfMapTest<
  "ZfTest.TOML.ObjMap", ZfRefMapTest<TOMLObjMap_>>;
using TOMLObjMapRef = ZmRef<TOMLObjMap>;
inline ZfTOML::AsMap<ZfFieldTC::UDT> ZfTOML_Fmt(TOMLObjMap *);

struct TOMLMapHolder {
  TOMLIntMapRef inline_;
  TOMLIntMapRef table;
  TOMLObjMapRef objects;
};
ZfStruct((TOMLMapHolder, TOML),
  (((inline_), (Ctor<0>, TOML::Inline)), (UDT)),
  (((table), (Ctor<1>)), (UDT)),
  (((objects), (Ctor<2>, TOML::Inline)), (UDT)));

struct TOMLUnionA { int foo = 0; };
struct TOMLUnionB { int bar = 0; };
ZfStruct((TOMLUnionA, TOML), (((foo), (Ctor<0>, Mutable)), (Int32)));
ZfStruct((TOMLUnionB, TOML), (((bar), (Ctor<0>, Mutable)), (Int32)));
struct TOMLUnionHolder { ZfTOML::Union<TOMLUnionA, TOMLUnionB> value; };
ZfStruct((TOMLUnionHolder, TOML),
  (((value), (Ctor<0>, Mutable)), (UDT)));

struct TOMLUnionArray : public ZtArray<int> {
  using ZtArray<int>::ZtArray;
  friend ZfTOML::AsArray<ZfFieldTC::Int32> ZfTOML_Fmt(TOMLUnionArray *);
};
struct TOMLUnionText {
  ZtString<> value;
  TOMLUnionText() = default;
  TOMLUnionText(ZuCSpan value_) : value{value_} { }
  template <typename S> friend S &operator <<(S &s, const TOMLUnionText &v) {
    s << v.value;
    return s;
  }
  friend ZfTOML::AsString ZfTOML_Fmt(TOMLUnionText *);
};
using TOMLMixedUnion = ZfTOML::Union<
  TOMLUnionA, TOMLIntMap, TOMLUnionArray, TOMLUnionText>;
struct TOMLMixedHolder { TOMLMixedUnion value; };
ZfStruct((TOMLMixedHolder, TOML),
  (((value), (Ctor<0>, Mutable)), (UDT)));
struct TOMLMixedInlineHolder { TOMLMixedUnion value; };
ZfStruct((TOMLMixedInlineHolder, TOML),
  (((value), (Ctor<0>, Mutable, TOML::Inline)), (UDT)));
struct TOMLMixedElements : public ZtArray<TOMLMixedUnion> {
  using ZtArray<TOMLMixedUnion>::ZtArray;
  friend ZfTOML::AsArray<ZfFieldTC::UDT> ZfTOML_Fmt(TOMLMixedElements *);
};
struct TOMLMixedElementsHolder { TOMLMixedElements values; };
ZfStruct((TOMLMixedElementsHolder, TOML),
  (((values), (Ctor<0>, Mutable)), (UDT)));
using TOMLObjectUnion = ZfTOML::Union<TOMLUnionA, TOMLUnionB>;
struct TOMLObjectUnionElements : public ZtArray<TOMLObjectUnion> {
  using ZtArray<TOMLObjectUnion>::ZtArray;
  friend ZfTOML::AsArray<ZfFieldTC::UDT>
    ZfTOML_Fmt(TOMLObjectUnionElements *);
};
struct TOMLObjectUnionElementsHolder { TOMLObjectUnionElements values; };
ZfStruct((TOMLObjectUnionElementsHolder, TOML),
  (((values), (Ctor<0>, Mutable)), (UDT)));
struct TOMLObjectUnionInlineHolder { TOMLObjectUnionElements values; };
ZfStruct((TOMLObjectUnionInlineHolder, TOML),
  (((values), (Ctor<0>, Mutable, TOML::Inline)), (UDT)));

struct TOMLTableUnionHolder {
  ZfTOML::Union<TOMLUnionA, TOMLMapHolder> value;
};
ZfStruct((TOMLTableUnionHolder, TOML),
  (((value), (Ctor<0>, Mutable)), (UDT)));

struct TOMLPtrObj_ : public ZmObject {
  int value = 0;
  TOMLPtrObj_(int value_ = 0) : value{value_} { }
};
using TOMLPtrObj = ZfHeapTest<"ZfTest.TOML.PtrObj", TOMLPtrObj_>;
ZfStruct((TOMLPtrObj, TOML),
  (((value), (Ctor<0>, Mutable)), (Int32)));

struct TOMLFmtOpt : public ZmObject { };
inline ZfTOML::AsString ZfTOML_Fmt(ZmRef<TOMLFmtOpt> *);
ZuAssert((!ZfTOML::IsObjPtr<ZmRef<TOMLFmtOpt>>{}));

struct TOMLPtrText_ : public ZmObject {
  ZtString<> value;
  TOMLPtrText_() = default;
  TOMLPtrText_(ZuCSpan value_) : value{value_} { }
  template <typename S>
  friend S &operator <<(S &s, const TOMLPtrText_ &v) {
    s << v.value;
    return s;
  }
};
using TOMLPtrText = ZfHeapTest<"ZfTest.TOML.PtrText", TOMLPtrText_>;

struct TOMLPtrArray_ : public ZmObject, public ZtArray<ZmRef<TOMLPtrObj>> {
  using Base = ZtArray<ZmRef<TOMLPtrObj>>;
  using Base::Base;
  using Base::operator =;
};
using TOMLPtrArray = ZfHeapTest<"ZfTest.TOML.PtrArray", TOMLPtrArray_>;
inline ZfTOML::AsArray<ZfFieldTC::UDT> ZfTOML_Fmt(TOMLPtrArray *);

using TOMLPtrMap_ =
  ZmRBTreeKV<TOMLMapKey, ZmRef<TOMLPtrObj>, ZmRBTreeUnique<true>>;
using TOMLPtrMap = ZfMapTest<
  "ZfTest.TOML.PtrMap", ZfRefMapTest<TOMLPtrMap_>>;
inline ZfTOML::AsMap<ZfFieldTC::UDT> ZfTOML_Fmt(TOMLPtrMap *);

struct TOMLPtrHolder {
  ZmRef<TOMLPtrObj> object;
  ZmRef<TOMLPtrArray> objects;
  ZmRef<TOMLPtrText> text;
};
ZfStruct((TOMLPtrHolder, TOML),
  (((object), (Mutable)), (UDT)),
  (((objects), (Mutable)), (UDT)),
  (((text), (Mutable, TOML::Opt)), (UDT)));

struct TOMLPtrInlineHolder { TOMLPtrHolder value; };
ZfStruct((TOMLPtrInlineHolder, TOML),
  (((value), (Mutable, TOML::Inline)), (UDT)));

struct TOMLPtrTables { ZmRef<TOMLPtrArray> values; };
ZfStruct((TOMLPtrTables, TOML),
  (((values), (Mutable)), (UDT)));
ZuAssert((ZuIsSame<
  ZmHeapID<TOMLIntMap>, ZuStringT<"ZfTest.TOML.IntMap">>{}));
ZuAssert((ZuIsSame<
  ZmHeapID<TOMLPtrObj>, ZuStringT<"ZfTest.TOML.PtrObj">>{}));
ZuAssert((ZuIsSame<
  ZmHeapID<TOMLPtrText>, ZuStringT<"ZfTest.TOML.PtrText">>{}));
ZuAssert((ZuIsSame<
  ZmHeapID<TOMLPtrArray>, ZuStringT<"ZfTest.TOML.PtrArray">>{}));

struct TOMLData {
  ZtString<> name;
  int number = 0;
  bool enabled = false;
  ZtArray<ZtString<>> values;
  TOMLNested nested;
};
ZfStruct((TOMLData, TOML),
  (((name), (Ctor<0>)), (String)),
  (((number), (Ctor<1>)), (Int32)),
  (((enabled), (Ctor<2>)), (Bool)),
  (((values), (Ctor<3>)), (StringVec)),
  (((nested), (Ctor<4>)), (UDT)));

struct TOMLScalars {
  ZtString<> native;
  ZtString<> basic;
  ZtString<> literal;
  ZtString<> multiBasic;
  ZtString<> multiLiteral;
};
ZfStruct((TOMLScalars, TOML),
  (((native), (Ctor<0>, Keys<0>, TOML::Native)), (String)),
  (((basic), (Ctor<1>, Mutable, TOML::Basic)), (String)),
  (((literal), (Ctor<2>, Mutable, TOML::Literal)), (String)),
  (((multiBasic), (Ctor<3>, Mutable, TOML::MultilineBasic)), (String)),
  (((multiLiteral), (Ctor<4>, Mutable, TOML::MultilineLiteral)), (String)));

struct TOMLScalarArrays {
  ZtArray<ZtString<>> native;
  ZtArray<ZtString<>> basic;
  ZtArray<ZtString<>> literal;
  ZtArray<ZtString<>> multiBasic;
  ZtArray<ZtString<>> multiLiteral;
};
ZfStruct((TOMLScalarArrays, TOML),
  (((native), (Ctor<0>, TOML::Native)), (StringVec)),
  (((basic), (Ctor<1>, TOML::Basic)), (StringVec)),
  (((literal), (Ctor<2>, TOML::Literal)), (StringVec)),
  (((multiBasic), (Ctor<3>, TOML::MultilineBasic)), (StringVec)),
  (((multiLiteral), (Ctor<4>, TOML::MultilineLiteral)), (StringVec)));

struct TOMLScalarInline { TOMLScalars values; };
ZfStruct((TOMLScalarInline, TOML),
  (((values), (Ctor<0>, TOML::Inline)), (UDT)));

struct TOMLScalarRows : public ZtArray<TOMLScalars> {
  using ZtArray<TOMLScalars>::ZtArray;
  friend ZfTOML::AsArray<ZfFieldTC::UDT> ZfTOML_Fmt(TOMLScalarRows *);
};
struct TOMLScalarTableArray { TOMLScalarRows rows; };
ZfStruct((TOMLScalarTableArray, TOML),
  (((rows), (Ctor<0>)), (UDT)));

struct TOMLProduct { ZtString<> name; int count = 0; };
ZfStruct((TOMLProduct, TOML),
  (((name), (Ctor<0>, Keys<0>)), (String)),
  (((count), (Ctor<1>, Mutable)), (Int32)));

struct TOMLProducts : public ZtArray<TOMLProduct> {
  using ZtArray<TOMLProduct>::ZtArray;
  friend ZfTOML::AsArray<ZfFieldTC::UDT> ZfTOML_Fmt(TOMLProducts *);
};
struct TOMLStrings : public ZtArray<ZtString<>> {
  using ZtArray<ZtString<>>::ZtArray;
  friend ZfTOML::AsArray<ZfFieldTC::String> ZfTOML_Fmt(TOMLStrings *);
};

struct TOMLCatalog { ZtString<> title; TOMLProducts products; };
ZfStruct((TOMLCatalog, TOML),
  (((title), (Ctor<0>, Keys<0>)), (String)),
  (((products), (Ctor<1>, Mutable)), (UDT)));
struct TOMLCatalogInline { ZtString<> title; TOMLProducts products; };
ZfStruct((TOMLCatalogInline, TOML),
  (((title), (Ctor<0>)), (String)),
  (((products), (Ctor<1>, TOML::Inline)), (UDT)));

struct TOMLSaveContext {
  TOMLCatalog catalog;
  TOMLObjMapRef objects;
  TOMLMixedUnion mixed;
  TOMLObjectUnionElements unions;
};
ZfStruct((TOMLSaveContext, TOML),
  (((catalog), (Ctor<0>, Mutable)), (UDT)),
  (((objects), (Ctor<1>, Mutable)), (UDT)),
  (((mixed), (Ctor<2>, Mutable)), (UDT)),
  (((unions), (Ctor<3>, Mutable)), (UDT)));
struct TOMLSaveContextTable { TOMLSaveContext value; };
ZfStruct((TOMLSaveContextTable, TOML),
  (((value), (Ctor<0>, Mutable)), (UDT)));
struct TOMLSaveContextInline { TOMLSaveContext value; };
ZfStruct((TOMLSaveContextInline, TOML),
  (((value), (Ctor<0>, Mutable, TOML::Inline)), (UDT)));

struct TOMLInterop {
  ZtString<> title;
  bool enabled = false;
  ZtArray<ZtString<>> values;
  TOMLNested nested;
  ZuDateTime when;
  TOMLIntMapRef inlineMap;
  TOMLIntMapRef tableMap;
  TOMLProducts products;
};
ZfStruct((TOMLInterop, TOML),
  (((title), (Ctor<0>)), (String)),
  (((enabled), (Ctor<1>)), (Bool)),
  (((values), (Ctor<2>)), (StringVec)),
  (((nested), (Ctor<3>)), (UDT)),
  (((when), (Ctor<4>)), (DateTime)),
  (((inlineMap), (Ctor<5>, TOML::Inline)), (UDT)),
  (((tableMap), (Ctor<6>)), (UDT)),
  (((products), (Ctor<7>)), (UDT)));
struct TOMLSiblingTables { TOMLProducts tools; TOMLProducts supplies; };
ZfStruct((TOMLSiblingTables, TOML),
  (((tools), (Ctor<0>)), (UDT)),
  (((supplies), (Ctor<1>)), (UDT)));

using TOMLHex8 = ZuFmt::Hex<false, ZuFmt::Right<8>>;
struct TOMLFormats { unsigned hex = 0; const char *optional = nullptr; };
ZfStruct((TOMLFormats, TOML),
  (((hex), (Ctor<0>, Hex, TOML::Number<TOMLHex8>)), (UInt32)),
  (((optional), (Ctor<1>, TOML::Opt)), (CString)));

struct TOMLFacet { unsigned value = 0; };
ZfStruct(TOMLFacet,
  (((value), (Ctor<0>)), (UInt32)));
ZfStructRender(TOMLFacet, JSON,
  (value, JSON::ID<"json-value">));
ZfStructRender(TOMLFacet, TOML,
  (value, TOML::ID<"toml.value">, Hex, TOML::Number<TOMLHex8>));
using TOMLFacetFields = ZuFields<TOMLFacet, ZuFacet::TOML>;
using TOMLFacetField = ZuType<0, TOMLFacetFields>;
using JSONFacetField = ZuType<0, ZuFields<TOMLFacet, ZuFacet::JSON>>;
static_assert(!ZuIs_<typename TOMLFacetField::Props,
  typename JSONFacetField::Props>{});
static_assert(ZuIs_<ZuFieldProp::TOML::GetID<TOMLFacetField>,
  ZuStringT<"toml.value">>{});
static_assert(ZuIs_<ZuFieldProp::TOML::GetIDs<TOMLFacetFields>,
  ZuTypeList<ZuStringT<"toml.value">>>{});
using TOMLDataField = ZuType<0, ZuFields<TOMLData, ZuFacet::TOML>>;
static_assert(ZuIs_<ZuFieldProp::TOML::GetID<TOMLDataField>,
  ZuStringT<"name">>{});

struct TOMLChild { ZtString<> name; };
ZfStruct((TOMLChild, TOML),
  (((name), (Ctor<0>)), (String)));
struct TOMLChildren : public ZtArray<TOMLChild> {
  using ZtArray<TOMLChild>::ZtArray;
  friend ZfTOML::AsArray<ZfFieldTC::UDT> ZfTOML_Fmt(TOMLChildren *);
};
struct TOMLParent { ZtString<> name; TOMLChildren children; };
ZfStruct((TOMLParent, TOML),
  (((name), (Ctor<0>)), (String)),
  (((children), (Ctor<1>)), (UDT)));
struct TOMLParents : public ZtArray<TOMLParent> {
  using ZtArray<TOMLParent>::ZtArray;
  friend ZfTOML::AsArray<ZfFieldTC::UDT> ZfTOML_Fmt(TOMLParents *);
};

using TOMLParentMap_ =
  ZmRBTreeKV<TOMLMapKey, TOMLParent, ZmRBTreeUnique<true>>;
using TOMLParentMap = ZfMapTest<
  "ZfTest.TOML.ParentMap", ZfRefMapTest<TOMLParentMap_>>;
using TOMLParentMapRef = ZmRef<TOMLParentMap>;
inline ZfTOML::AsMap<ZfFieldTC::UDT> ZfTOML_Fmt(TOMLParentMap *);

using TOMLMapMap_ =
  ZmRBTreeKV<TOMLMapKey, TOMLParentMapRef, ZmRBTreeUnique<true>>;
using TOMLMapMap = ZfMapTest<
  "ZfTest.TOML.MapMap", ZfRefMapTest<TOMLMapMap_>>;
using TOMLMapMapRef = ZmRef<TOMLMapMap>;
inline ZfTOML::AsMap<ZfFieldTC::UDT> ZfTOML_Fmt(TOMLMapMap *);

struct TOMLMapArray : public ZtArray<TOMLParentMapRef> {
  using ZtArray<TOMLParentMapRef>::ZtArray;
  friend ZfTOML::AsArray<ZfFieldTC::UDT> ZfTOML_Fmt(TOMLMapArray *);
};

struct TOMLParentMapHolder { TOMLParentMapRef parents; };
ZfStruct((TOMLParentMapHolder, TOML),
  (((parents), (Ctor<0>)), (UDT)));
struct TOMLNestedTables { TOMLParents parents; };
ZfStruct((TOMLNestedTables, TOML),
  (((parents), (Ctor<0>)), (UDT)));
struct TOMLTableGroup { ZtString<> id; TOMLParents parents; };
ZfStruct((TOMLTableGroup, TOML),
  (((id), (Ctor<0>)), (String)),
  (((parents), (Ctor<1>, TOML::ID<"child group">)), (UDT)));
struct TOMLGroupedTables { TOMLTableGroup group; };
ZfStruct((TOMLGroupedTables, TOML),
  (((group), (Ctor<0>, TOML::ID<"unsafe.group">)), (UDT)));

struct TOMLScalarTable {
  ZtString<> native;
  ZtString<> basic;
  ZtString<> literal;
  ZtString<> multiBasic;
  ZtString<> multiLiteral;
  TOMLProducts products;
};
ZfStruct((TOMLScalarTable, TOML),
  (((native), (Ctor<0>, TOML::Native)), (String)),
  (((basic), (Ctor<1>, TOML::Basic)), (String)),
  (((literal), (Ctor<2>, TOML::Literal)), (String)),
  (((multiBasic), (Ctor<3>, TOML::MultilineBasic)), (String)),
  (((multiLiteral), (Ctor<4>, TOML::MultilineLiteral)), (String)),
  (((products), (Ctor<5>)), (UDT)));
struct TOMLScalarNestedTable { TOMLScalarTable nested; };
ZfStruct((TOMLScalarNestedTable, TOML),
  (((nested), (Ctor<0>)), (UDT)));

struct TOMLBytes {
  ZtArray<uint8_t> base64;
  ZtArray<uint8_t> base64URL;
  ZtArray<uint8_t> base32;
  ZtArray<uint8_t> hex;
  ZtArray<uint8_t> raw;
  ZtArray<ZtArray<uint8_t>> vec;
};
ZfStruct((TOMLBytes, TOML),
  (((base64), (Ctor<0>, TOML::Base64, TOML::Native)), (Bytes)),
  (((base64URL), (Ctor<1>, TOML::Base64URL, TOML::Basic)), (Bytes)),
  (((base32), (Ctor<2>, TOML::Base32, TOML::Literal)), (Bytes)),
  (((hex), (Ctor<3>, TOML::Hex, TOML::MultilineBasic)), (Bytes)),
  (((raw), (Ctor<4>, TOML::Raw, TOML::MultilineLiteral)), (Bytes)),
  (((vec), (Ctor<5>)), (BytesVec)));

struct TOMLStringNumbers {
  int native = 0;
  int basic = 0;
  int literal = 0;
  int multiBasic = 0;
  int multiLiteral = 0;
};
ZfStruct((TOMLStringNumbers, TOML),
  (((native), (Ctor<0>, TOML::String<>, TOML::Native)), (Int32)),
  (((basic), (Ctor<1>, TOML::String<>, TOML::Basic)), (Int32)),
  (((literal), (Ctor<2>, TOML::String<>, TOML::Literal)), (Int32)),
  (((multiBasic),
    (Ctor<3>, TOML::String<>, TOML::MultilineBasic)), (Int32)),
  (((multiLiteral),
    (Ctor<4>, TOML::String<>, TOML::MultilineLiteral)), (Int32)));

struct TOMLText {
  TOMLText() = default;
  TOMLText(ZuCSpan value_) : value{value_} { }
  ZtString<> value;
  template <typename S> void print(S &s) const { s << value; }
  friend ZuPrintFn ZuPrintType(TOMLText *);
  friend ZfTOML::AsString ZfTOML_Fmt(TOMLText *);
};
struct TOMLTextData { TOMLText text; };
ZfStruct((TOMLTextData, TOML),
  (((text), (Ctor<0>)), (UDT)));

struct TOMLTextStyles {
  TOMLText native, basic, literal, multiBasic, multiLiteral;
};
ZfStruct((TOMLTextStyles, TOML),
  (((native), (Ctor<0>, TOML::Native)), (UDT)),
  (((basic), (Ctor<1>, TOML::Basic)), (UDT)),
  (((literal), (Ctor<2>, TOML::Literal)), (UDT)),
  (((multiBasic), (Ctor<3>, TOML::MultilineBasic)), (UDT)),
  (((multiLiteral), (Ctor<4>, TOML::MultilineLiteral)), (UDT)));

struct TOMLNumbers {
  int i = 0;
  unsigned hex = 0;
  int enum_ = TOMLValues::Normal;
  uint128_t flags = 0;
  double float_ = 0;
  ZuFixed fixed;
  ZuDecimal decimal;
  ZuTime time;
  ZtArray<int> ints;
};
ZfStruct((TOMLNumbers, TOML),
  (((i), (Ctor<0>, Mutable)), (Int32)),
  (((hex), (Ctor<1>, Hex)), (UInt32)),
  (((enum_), (Ctor<2>, Enum<TOMLValues::Map>)), (Int32)),
  (((flags), (Ctor<3>, Flags<TOMLFlags::Map>)), (UInt128)),
  (((float_), (Ctor<4>, (Range<-1000.0, 1000.0>))), (Float)),
  (((fixed), (Ctor<5>,
    (Range<ZuDecimal{-1000}, ZuDecimal{1000}>))), (Fixed)),
  (((decimal), (Ctor<6>,
    (Range<ZuDecimal{-1000}, ZuDecimal{1000}>))), (Decimal)),
  (((time), (Ctor<7>, TOML::Unix9)), (Time)),
  (((ints), (Ctor<8>)), (Int32Vec)));

struct TOMLOptional {
  const char *head = nullptr;
  const char *req = "";
  const char *tail = "tail";
};
ZfStruct((TOMLOptional, TOML),
  (((head), (Ctor<0>, TOML::Opt)), (CString)),
  (((req), (Ctor<1>)), (CString)),
  (((tail), (Ctor<2>)), (CString)));

struct TOMLRange { int value = 42; };
ZfStruct((TOMLRange, TOML),
  (((value), (Ctor<0>, (Range<0, 100>))), (Int32, 42)));

struct TOMLRequired { int required; int optional; };
ZfStruct((TOMLRequired, TOML),
  (((required), (Ctor<0>, Required)), (Int32)),
  (((optional), (Ctor<1>)), (Int32)));

struct TOMLDates {
  ZuDateTime offset;
  ZuDateTime local;
  ZuDateTime date;
  ZuDateTime time;
};
ZfStruct((TOMLDates, TOML),
  (((offset), (Ctor<0>)), (DateTime)),
  (((local), (Ctor<1>)), (DateTime)),
  (((date), (Ctor<2>)), (DateTime)),
  (((time), (Ctor<3>)), (DateTime)));

struct TOMLStringDates {
  ZuDateTime native, basic, literal, multiBasic, multiLiteral;
};
ZfStruct((TOMLStringDates, TOML),
  (((native), (Ctor<0>, TOML::CSV9, TOML::Native)),
    (DateTime)),
  (((basic), (Ctor<1>, TOML::CSV9, TOML::Basic)),
    (DateTime)),
  (((literal), (Ctor<2>, TOML::CSV9, TOML::Literal)),
    (DateTime)),
  (((multiBasic),
    (Ctor<3>, TOML::CSV9, TOML::MultilineBasic)),
    (DateTime)),
  (((multiLiteral),
    (Ctor<4>, TOML::CSV9, TOML::MultilineLiteral)),
    (DateTime)));

struct TOMLFloats { double finite = 0; double inf = 0; double nan = 0; };
ZfStruct((TOMLFloats, TOML),
  (((finite), (Ctor<0>)), (Float)),
  (((inf), (Ctor<1>)), (Float)),
  (((nan), (Ctor<2>)), (Float)));
struct TOMLWideNumbers { ZuFixed fixed; ZuDecimal decimal; };
ZfStruct((TOMLWideNumbers, TOML),
  (((fixed), (Ctor<0>)), (Fixed)),
  (((decimal), (Ctor<1>)), (Decimal)));

struct TOMLKeys {
  int safe = 0;
  int numeric = 0;
  int dot = 0;
  int space = 0;
  int control = 0;
  int unicode = 0;
};
ZfStruct((TOMLKeys, TOML),
  (((safe), (Ctor<0>, TOML::ID<"AZaz-09_">)), (Int32)),
  (((numeric), (Ctor<1>, TOML::ID<"123">)), (Int32)),
  (((dot), (Ctor<2>, TOML::ID<"a.b">)), (Int32)),
  (((space), (Ctor<3>, TOML::ID<"space key">)), (Int32)),
  (((control), (Ctor<4>, TOML::ID<"\x01">)), (Int32)),
  (((unicode), (Ctor<5>, TOML::ID<"\xe2\x98\xba">)), (Int32)));

static const ZfTOML::AnyNode *field(
    const ZfTOML::AnyNode *node, ZuCSpan id)
{
  if (!node || !node->has<ZfTOML::AnyNode::Object>()) return nullptr;
  const auto &fields = node->data<ZfTOML::AnyNode::Object>();
  for (unsigned i = 0, n = fields.length(); i < n; ++i)
    if (fields[i].p<0>() == id || (!fields[i].p<0>() && !id))
      return fields[i].p<1>();
  return nullptr;
}

static ZeString error(ZuCSpan input, ZfTOML::Limits limits = {})
{
  try { ZfTOML::scan(input, limits); }
  catch (const ZeException &e) { ZeString text; text << e; return text; }
  return {};
}

using TOMLFixture = ZtString<ZtStringHeapID<"ZfTOML.Fixture">>;
using TOMLFixturePath = ZtString<ZtStringHeapID<"ZfTOML.FixturePath">>;

static bool fixture(TOMLFixture &data, ZuCSpan name)
{
  const char *roots[] = {ZF_TOML_TEST_SRCDIR, "zf/test", "."};
  FILE *file = nullptr;
  for (auto root: roots) {
    TOMLFixturePath path;
    path << root << "/toml-test/" << name;
    file = fopen(path, "rb");
    if (file) break;
  }
  if (!file) return false;
  if (fseek(file, 0, SEEK_END)) { fclose(file); return false; }
  long length = ftell(file);
  if (length < 0 || fseek(file, 0, SEEK_SET)) {
    fclose(file);
    return false;
  }
  data.length(unsigned(length));
  bool ok = !length ||
    fread(data.data(), 1, unsigned(length), file) == unsigned(length);
  if (fclose(file)) ok = false;
  return ok;
}

template <typename L>
static bool loadError(L l)
{
  try { l(); }
  catch (const ZeException &) { return true; }
  return false;
}

template <typename L>
static ZeString loadException(L l)
{
  try { l(); }
  catch (const ZeException &e) { ZeString text; text << e; return text; }
  return {};
}

static void rootScalars()
{
  ZuTestScope(rootScalars);

  auto scan = ZfTOML::scan(
    "name = \"toml\"\n"
    "enabled = true\n"
    "hex = 0xdead_beef\n"
    "when = 1979-05-27T07:32:00Z\n");
  ZuCheck(scan.p<0>() > 0);
  const auto *root = scan.p<1>().ptr();
  ZuCheck(root->has<ZfTOML::AnyNode::Object>());
  ZuCheck(field(root, "name")->data<ZfTOML::AnyNode::String>() == "toml");
  ZuCheck(field(root, "enabled")->scalarType == ZfTOML::ScalarTC::True);
  ZuCheck(field(root, "hex")->data<ZfTOML::AnyNode::String>() ==
    "0xdead_beef");
  ZuCheck(field(root, "when")->has<ZfTOML::AnyNode::DateTime>());
}

static void collections()
{
  ZuTestScope(collections);

  auto scan = ZfTOML::scan(
    "title = 'example'\n"
    "values = [1, \"two\", true,]\n"
    "point = { x = 1, y = 2, }\n"
    "[owner]\n"
    "name = \"Tom\"\n"
    "[[products]]\n"
    "name = \"Hammer\"\n"
    "[[products]]\n"
    "name = \"Nail\"\n");
  const auto *root = scan.p<1>().ptr();
  ZuCheck(field(root, "values")->data<ZfTOML::AnyNode::Array>().length() == 3);
  ZuCheck(field(root, "point")->has<ZfTOML::AnyNode::Object>());
  auto inlineDotted = ZfTOML::scan(
    "inline = {a.b = 1, a.c = 2,}\n").p<1>();
  ZuCheck(inlineDotted->resolve("inline.a.b") &&
    inlineDotted->resolve("inline.a.c"));
  ZuCheck(root->resolve("owner.name"));
  ZuCheck(root->resolve("products[0].name"));
  ZuCheck(root->resolve("products[1].name"));
}

static void strings()
{
  ZuTestScope(strings);

  auto scan = ZfTOML::scan(
    "basic = \"a\\tb\\u263a\"\n"
    "escapes = \"\\b\\t\\n\\f\\r\\e\\x41\\u263a\\U0001f600\"\n"
    "literal = 'a\\tb'\n"
    "multi = \"\"\"\nline 1\r\nline 2\"\"\"\n"
    "mlit = '''\r\nline 1\rline 2'''\n"
    "continued = \"\"\"one\\\n  \n  two\"\"\"\n"
    "quotes = \"\"\"two quotes: \"\" remain\"\"\"\n");
  const auto *root = scan.p<1>().ptr();
  ZuCheck(field(root, "basic")->data<ZfTOML::AnyNode::String>() ==
    "a\tb\xe2\x98\xba");
  ZuCheck(field(root, "literal")->data<ZfTOML::AnyNode::String>() == "a\\tb");
  ZuCheck(field(root, "escapes")->data<ZfTOML::AnyNode::String>() ==
    "\b\t\n\f\r\x1b" "A\xe2\x98\xba\xf0\x9f\x98\x80");
  ZuCheck(field(root, "multi")->data<ZfTOML::AnyNode::String>() ==
    "line 1\nline 2");
  ZuCheck(field(root, "mlit")->data<ZfTOML::AnyNode::String>() ==
    "line 1\nline 2");
  ZuCheck(field(root, "continued")->data<ZfTOML::AnyNode::String>() ==
    "onetwo");
  ZuCheck(field(root, "quotes")->data<ZfTOML::AnyNode::String>() ==
    "two quotes: \"\" remain");

  ZuCheck(error("x = \"\\uD800\"\n").find("invalid Unicode scalar") >= 0);
  ZuCheck(error("x = \"\\U00110000\"\n").find("invalid Unicode scalar") >= 0);
  ZuCheck(error("x = \"\\xG0\"\n").find("invalid escape") >= 0);
}

static void failures()
{
  ZuTestScope(failures);

  {
    ZfTOML::Scan scan{ZuCSpan{}};
    auto result = scan.scan();
    ZuCheck(result.p<0>() < 0 && !result.p<1>() && scan.error().failed &&
      scan.error().code == ZfTOML::SyntaxErrorCode::Syntax);
  }

  unsigned caught = 0;
  const char *bad[] = {
    "x = 1\nx = 2\n",
    "x = 9223372036854775808\n",
    "x = [1,,2]\n",
    "x = \"\\q\"\n",
    "[x]\n[x]\n"
  };
  for (auto input: bad) {
    try { (void)ZfTOML::scan(input); }
    catch (const ZeException &) { ++caught; }
  }
  ZuCheck(caught == sizeof(bad) / sizeof(bad[0]));

  ZtString<> invalidComment{"# invalid "};
  invalidComment << char(0xc0) << char(0x80) << '\n';
  ZuCheck(error(invalidComment).find("invalid UTF-8") >= 0);
  ZtString<> controlComment{"# invalid "};
  controlComment << char(0x7f) << '\n';
  ZuCheck(error(controlComment).find("invalid control character") >= 0);
  ZtString<> controlString{"value = \"invalid"};
  controlString << char(0x7f) << "\"\n";
  ZuCheck(error(controlString).find("invalid control character") >= 0);
}

static void reflection()
{
  ZuTestScope(reflection);

  auto tree = ZfTOML::scan(
    "name = \"demo\"\nnumber = 0x2a\nenabled = true\n"
    "values = ['a', \"b\"]\nnested = { value = 7 }\n").p<1>();
  auto value = ZfTOML::handler<TOMLData>(tree).ctor();
  auto repeated = ZfTOML::handler<TOMLData>(tree).ctor();
  ZuCheck(value.name == "demo");
  ZuCheck(value.number == 42);
  ZuCheck(value.enabled);
  ZuCheck(value.values.length() == 2 && value.values[1] == "b");
  ZuCheck(value.nested.value == 7);
  ZuCheck(repeated.name == value.name && repeated.number == value.number &&
    repeated.values.length() == value.values.length());

  ZtString<> out;
  ZfTOML::save(out, value);
  ZuCheck(out ==
    "name = \"demo\"\nnumber = 42\nenabled = true\n"
    "values = [\"a\", \"b\"]\n[nested]\nvalue = 7\n");
  auto round = ZfTOML::handler<TOMLData>(ZfTOML::scan(out).p<1>()).ctor();
  ZuCheck(round.number == 42 && round.nested.value == 7);
}

static void scalarFormats()
{
  ZuTestScope(scalarFormats);

  TOMLScalars value{
    "native", "basic\\text", "literal", "multi\nline", "other\nline"};
  ZtString<> out;
  ZfTOML::save(out, value);
  ZuCheck(out ==
    "native = \"native\"\n"
    "basic = \"basic\\\\text\"\n"
    "literal = 'literal'\n"
    "multiBasic = \"\"\"multi\nline\"\"\"\n"
    "multiLiteral = '''other\nline'''\n");
  auto round = ZfTOML::handler<TOMLScalars>(ZfTOML::scan(out).p<1>()).ctor();
  ZuCheck(round.native == value.native);
  ZuCheck(round.basic == value.basic);
  ZuCheck(round.literal == value.literal);
  ZuCheck(round.multiBasic == value.multiBasic);
  ZuCheck(round.multiLiteral == value.multiLiteral);

  out.null();
  ZfTOML::saveUpd(out, value);
  ZuCheck(out ==
    "native = \"native\"\n"
    "basic = \"basic\\\\text\"\n"
    "literal = 'literal'\n"
    "multiBasic = \"\"\"multi\nline\"\"\"\n"
    "multiLiteral = '''other\nline'''\n");
  out.null();
  ZfTOML::saveDel(out, value);
  ZuCheck(out == "native = \"native\"\n");

  const char *forms[] = {
    "\"same\"", "'same'", "\"\"\"same\"\"\"", "'''same'''"
  };
  {
    ZuTestRepeat(inputStringForms, 4);
    for (auto form: forms) {
      ZtString<> source;
      source << "native = " << form << '\n'
        << "basic = " << form << '\n'
        << "literal = " << form << '\n'
        << "multiBasic = " << form << '\n'
        << "multiLiteral = " << form << '\n';
      auto loaded = ZfTOML::handler<TOMLScalars>(
        ZfTOML::scan(source).p<1>()).ctor();
      ZuCheck(loaded.native == "same" && loaded.basic == "same" &&
        loaded.literal == "same" && loaded.multiBasic == "same" &&
        loaded.multiLiteral == "same");
    }
  }
}

static void scalarFormatContexts()
{
  ZuTestScope(scalarFormatContexts);

  TOMLScalars scalars{"n", "b", "l", "mb", "ml"};
  ZtString<> out;

  TOMLScalarInline inline_{scalars};
  ZfTOML::save(out, inline_);
  ZuCheck(out ==
    "values = {native = \"n\", basic = \"b\", literal = 'l', "
    "multiBasic = \"\"\"mb\"\"\", multiLiteral = '''ml'''}\n");
  auto inlineRound = ZfTOML::handler<TOMLScalarInline>(
    ZfTOML::scan(out).p<1>()).ctor();
  ZuCheck(inlineRound.values.multiLiteral == "ml");

  TOMLScalarArrays arrays;
  arrays.native.push("n");
  arrays.basic.push("b");
  arrays.literal.push("l");
  arrays.multiBasic.push("mb");
  arrays.multiLiteral.push("ml");
  out.null();
  ZfTOML::save(out, arrays);
  ZuCheck(out ==
    "native = [\"n\"]\n"
    "basic = [\"b\"]\n"
    "literal = ['l']\n"
    "multiBasic = [\"\"\"mb\"\"\"]\n"
    "multiLiteral = ['''ml''']\n");
  auto arraysRound = ZfTOML::handler<TOMLScalarArrays>(
    ZfTOML::scan(out).p<1>()).ctor();
  ZuCheck(arraysRound.native[0] == "n" &&
    arraysRound.multiLiteral[0] == "ml");

  TOMLScalarTableArray tableArray;
  tableArray.rows.push(scalars);
  out.null();
  ZfTOML::save(out, tableArray);
  ZuCheck(out ==
    "[[rows]]\n"
    "native = \"n\"\n"
    "basic = \"b\"\n"
    "literal = 'l'\n"
    "multiBasic = \"\"\"mb\"\"\"\n"
    "multiLiteral = '''ml'''\n");
  auto tableRound = ZfTOML::handler<TOMLScalarTableArray>(
    ZfTOML::scan(out).p<1>()).ctor();
  ZuCheck(tableRound.rows.length() == 1 &&
    tableRound.rows[0].multiBasic == "mb");

  TOMLScalarNestedTable nested;
  nested.nested.native = "n";
  nested.nested.basic = "b";
  nested.nested.literal = "l";
  nested.nested.multiBasic = "mb";
  nested.nested.multiLiteral = "ml";
  out.null();
  ZfTOML::save(out, nested);
  ZuCheck(out ==
    "[nested]\n"
    "native = \"n\"\n"
    "basic = \"b\"\n"
    "literal = 'l'\n"
    "multiBasic = \"\"\"mb\"\"\"\n"
    "multiLiteral = '''ml'''\n"
    "products = []\n");
  auto nestedRound = ZfTOML::handler<TOMLScalarNestedTable>(
    ZfTOML::scan(out).p<1>()).ctor();
  ZuCheck(nestedRound.nested.literal == "l");
}

static void bytesFormats()
{
  ZuTestScope(bytesFormats);

  TOMLBytes value;
  value.base64 = ZuBSpan{"xxx"};
  value.base64URL = ZuBSpan{"xxx"};
  value.base32 = ZuBSpan{"xxx"};
  value.hex = ZuBSpan{"xxx"};
  value.raw = ZuBSpan{"raw bytes"};
  value.vec.push(ZuBSpan{"x"});
  value.vec.push(ZuBSpan{"yy"});
  ZtString<> out;
  ZfTOML::save(out, value);
  ZuCheck(out ==
    "base64 = \"eHh4\"\n"
    "base64URL = \"eHh4\"\n"
    "base32 = 'PB4HQ==='\n"
    "hex = \"\"\"787878\"\"\"\n"
    "raw = '''raw bytes'''\n"
    "vec = [\"eA==\", \"eXk=\"]\n");
  auto round = ZfTOML::handler<TOMLBytes>(
    ZfTOML::scan(out).p<1>()).ctor();
  ZuCheck(ZuBSpan{round.base64} == ZuBSpan{"xxx"});
  ZuCheck(ZuBSpan{round.base64URL} == ZuBSpan{"xxx"});
  ZuCheck(ZuBSpan{round.base32} == ZuBSpan{"xxx"});
  ZuCheck(ZuBSpan{round.hex} == ZuBSpan{"xxx"});
  ZuCheck(ZuBSpan{round.raw} == ZuBSpan{"raw bytes"});
  ZuCheck(round.vec.length() == 2 && round.vec[0][0] == 'x' &&
    round.vec[1][0] == 'y' && round.vec[1][1] == 'y');

  auto alternate = ZfTOML::handler<TOMLBytes>(ZfTOML::scan(
    "base64 = 'eHh4'\nbase64URL = '''eHh4'''\n"
    "base32 = \"PB4HQ===\"\nhex = '787878'\n"
    "raw = \"raw bytes\"\nvec = ['eA==', '''eXk=''']\n").p<1>()).ctor();
  ZuCheck(ZuBSpan{alternate.base64} == ZuBSpan{"xxx"} &&
    ZuBSpan{alternate.raw} == ZuBSpan{"raw bytes"});

  const char *open[] = {"\"", "'", "\"\"\"", "'''"};
  const char *close[] = {"\"", "'", "\"\"\"", "'''"};
  {
    ZuTestRepeat(inputByteStringForms, 4);
    for (unsigned i = 0; i < 4; ++i) {
      ZtString<> source;
      source << "base64 = " << open[i] << "eHh4" << close[i] << '\n'
        << "base64URL = " << open[i] << "eHh4" << close[i] << '\n'
        << "base32 = " << open[i] << "PB4HQ===" << close[i] << '\n'
        << "hex = " << open[i] << "787878" << close[i] << '\n'
        << "raw = " << open[i] << "raw bytes" << close[i] << '\n'
        << "vec = [" << open[i] << "eA==" << close[i] << "]\n";
      auto loaded = ZfTOML::handler<TOMLBytes>(
        ZfTOML::scan(source).p<1>()).ctor();
      ZuCheck(ZuBSpan{loaded.base64} == ZuBSpan{"xxx"} &&
        ZuBSpan{loaded.base64URL} == ZuBSpan{"xxx"} &&
        ZuBSpan{loaded.base32} == ZuBSpan{"xxx"} &&
        ZuBSpan{loaded.hex} == ZuBSpan{"xxx"} &&
        ZuBSpan{loaded.raw} == ZuBSpan{"raw bytes"} &&
        loaded.vec.length() == 1 && ZuBSpan{loaded.vec[0]} == ZuBSpan{"x"});
    }
  }
}

static void stringNumberFormats()
{
  ZuTestScope(stringNumberFormats);

  TOMLStringNumbers value{1, 2, 3, 4, 5};
  ZtString<> out;
  ZfTOML::save(out, value);
  ZuCheck(out ==
    "native = \"1\"\n"
    "basic = \"2\"\n"
    "literal = '3'\n"
    "multiBasic = \"\"\"4\"\"\"\n"
    "multiLiteral = '''5'''\n");
  auto round = ZfTOML::handler<TOMLStringNumbers>(
    ZfTOML::scan(out).p<1>()).ctor();
  ZuCheck(round.native == 1 && round.basic == 2 && round.literal == 3 &&
    round.multiBasic == 4 && round.multiLiteral == 5);

  auto nativeInput = ZfTOML::handler<TOMLStringNumbers>(ZfTOML::scan(
    "native = 1\nbasic = 2\nliteral = 3\n"
    "multiBasic = 4\nmultiLiteral = 5\n").p<1>()).ctor();
  ZuCheck(nativeInput.native == 1 && nativeInput.basic == 2 &&
    nativeInput.literal == 3 && nativeInput.multiBasic == 4 &&
    nativeInput.multiLiteral == 5);

  TOMLTextStyles text{{"n"}, {"b"}, {"l"}, {"mb"}, {"ml"}};
  out.null();
  ZfTOML::save(out, text);
  ZuCheck(out ==
    "native = \"n\"\nbasic = \"b\"\nliteral = 'l'\n"
    "multiBasic = \"\"\"mb\"\"\"\nmultiLiteral = '''ml'''\n");
  auto textRound = ZfTOML::handler<TOMLTextStyles>(
    ZfTOML::scan(out).p<1>()).ctor();
  ZuCheck(textRound.native.value == "n" && textRound.basic.value == "b" &&
    textRound.literal.value == "l" && textRound.multiBasic.value == "mb" &&
    textRound.multiLiteral.value == "ml");

  const char *open[] = {"\"", "'", "\"\"\"", "'''"};
  const char *close[] = {"\"", "'", "\"\"\"", "'''"};
  {
    ZuTestRepeat(inputNumberStringForms, 4);
    for (unsigned i = 0; i < 4; ++i) {
      ZtString<> source;
      for (auto id: {"native", "basic", "literal", "multiBasic",
          "multiLiteral"})
        source << id << " = " << open[i] << '7' << close[i] << '\n';
      auto loaded = ZfTOML::handler<TOMLStringNumbers>(
        ZfTOML::scan(source).p<1>()).ctor();
      ZuCheck(loaded.native == 7 && loaded.basic == 7 &&
        loaded.literal == 7 && loaded.multiBasic == 7 &&
        loaded.multiLiteral == 7);
    }
  }
  {
    ZuTestRepeat(inputUDTStringForms, 4);
    for (unsigned i = 0; i < 4; ++i) {
      ZtString<> source;
      for (auto id: {"native", "basic", "literal", "multiBasic",
          "multiLiteral"})
        source << id << " = " << open[i] << "same" << close[i] << '\n';
      auto loaded = ZfTOML::handler<TOMLTextStyles>(
        ZfTOML::scan(source).p<1>()).ctor();
      ZuCheck(loaded.native.value == "same" && loaded.basic.value == "same" &&
        loaded.literal.value == "same" &&
        loaded.multiBasic.value == "same" &&
        loaded.multiLiteral.value == "same");
    }
  }
}

static void conversionMatrix()
{
  ZuTestScope(conversionMatrix);

  auto missing = ZfTOML::scan("optional = 1\n").p<1>();
  ZuCheck(loadError([&missing] {
    ZfTOML::handler<TOMLRequired>(missing).ctor();
  }));
  auto null_ = ZfTOML::handler<TOMLRequired>(ZfTOML::scan(
    "required = -2147483648\n").p<1>()).ctor();
  ZuCheck(ZuNull(null_.required));
  auto required = ZfTOML::handler<TOMLRequired>(ZfTOML::scan(
    "required = 0\n").p<1>()).ctor();
  ZuCheck(required.required == 0 && ZuNull(required.optional));
  TOMLRequired updated{ZuCmp<int>::null(), 2};
  ZfTOML::handler<TOMLRequired>(ZfTOML::scan(
    "required = 0\n").p<1>()).update(updated);
  ZuCheck(ZuNull(updated.required) && updated.optional == 2);
  ZuCheck(loadError([updated] {
    ZtString<> out;
    ZfTOML::save(out, updated);
  }));

  auto minimum = ZfTOML::handler<TOMLRange>(ZfTOML::scan(
    "value = 0\n").p<1>()).ctor();
  auto maximum = ZfTOML::handler<TOMLRange>(ZfTOML::scan(
    "value = 100\n").p<1>()).ctor();
  auto unknown = ZfTOML::handler<TOMLRange>(ZfTOML::scan(
    "value-junk = 99\n").p<1>()).ctor();
  ZuCheck(minimum.value == 0 && maximum.value == 100 && unknown.value == 42);
  ZuCheck(loadError([] {
    ZfTOML::handler<TOMLRange>(ZfTOML::scan("value = -1\n").p<1>()).ctor();
  }));
  ZuCheck(loadError([] {
    ZfTOML::handler<TOMLRange>(ZfTOML::scan("value = 101\n").p<1>()).ctor();
  }));

  auto tree = ZfTOML::scan(
    "i = -42\nhex = 0xdead_beef\nenum_ = \"Low\"\n"
    "flags = \"Bit0|Bit2\"\nfloat_ = +1.25e2\nfixed = 12.5\n"
    "decimal = -0.125\ntime = 1700000000.25\nints = [1, -2, 3]\n").p<1>();
  auto value = ZfTOML::handler<TOMLNumbers>(tree).ctor();
  ZuCheck(value.i == -42 && value.hex == 0xdeadbeef);
  ZuCheck(value.enum_ == TOMLValues::Low);
  ZuCheck(value.flags == (TOMLFlags::Bit0() | TOMLFlags::Bit2()));
  ZuCheck(value.float_ == 125.0);
  ZuCheck(value.fixed == ZuFixed{ZuDecimal{"12.5"}});
  ZuCheck(value.decimal == ZuDecimal{"-0.125"});
  ZuCheck(value.time.sec() == 1700000000 && value.time.nsec() == 250000000);
  ZuCheck(value.ints.length() == 3 && value.ints[0] == 1 &&
    value.ints[1] == -2 && value.ints[2] == 3);

  auto basedNumbers = ZfTOML::handler<TOMLNumbers>(ZfTOML::scan(
    "float_ = 0x2a\nfixed = 0o52\ndecimal = 0b101010\n").p<1>()).ctor();
  ZuCheck(basedNumbers.float_ == 42 && basedNumbers.fixed == ZuFixed{42} &&
    basedNumbers.decimal == ZuDecimal{42});
  auto separatedNumbers = ZfTOML::handler<TOMLNumbers>(ZfTOML::scan(
    "float_ = 1_2.5_0e+1\nfixed = 1_2.5_0\n"
    "decimal = -1_2.5_0e-1\n").p<1>()).ctor();
  ZuCheck(separatedNumbers.float_ == 125 &&
    separatedNumbers.fixed == ZuFixed{ZuDecimal{"12.5"}} &&
    separatedNumbers.decimal == ZuDecimal{"-1.25"});

  TOMLNumbers loaded;
  ZfTOML::handler<TOMLNumbers>(tree).load(loaded);
  ZfTOML::handler<TOMLNumbers>(ZfTOML::scan("i = 7\n").p<1>()).update(loaded);
  ZuCheck(loaded.i == 7 && loaded.hex == 0xdeadbeef);

  const char *bad[] = {
    "i = \"1x\"\n", "enum_ = \"unknown\"\n",
    "flags = \"Bit0|unknown\"\n", "float_ = \"1.0x\"\n",
    "fixed = \"1.0x\"\n", "decimal = \"1.0x\"\n",
    "time = \"1.0x\"\n", "float_ = 1001\n",
    "fixed = -1001\n", "decimal = 1001\n",
    "float_ = 1e1000000\n", "decimal = 1e1000000\n"
  };
  {
    ZuTestRepeat(invalidConversions, 12);
    for (auto input: bad) ZuCheck(loadError([input] {
      ZfTOML::handler<TOMLNumbers>(ZfTOML::scan(input).p<1>()).ctor();
    }));
  }
  ZuCheck(loadError([] {
    ZfTOML::handler<TOMLData>(ZfTOML::scan(
      "enabled = \"maybe\"\n").p<1>()).ctor();
  }));
  ZuCheck(loadError([] {
    ZfTOML::handler<TOMLData>(ZfTOML::scan(
      "nested = 42\n").p<1>()).ctor();
  }));
  ZuCheck(loadError([] {
    ZfTOML::handler<TOMLData>(ZfTOML::scan(
      "values = \"nope\"\n").p<1>()).ctor();
  }));
  auto text = ZfTOML::handler<TOMLTextData>(ZfTOML::scan(
    "text = \"null\"\n").p<1>()).ctor();
  ZuCheck(text.text.value == "null");

  TOMLOptional optional;
  ZtString<> out;
  ZfTOML::save(out, optional);
  ZuCheck(out == "req = \"\"\ntail = \"tail\"\n");
  auto optionalTree = ZfTOML::scan(out);
  auto optionalRound = ZfTOML::handler<TOMLOptional>(
    optionalTree.p<1>()).ctor();
  ZuCheck(!optionalRound.head && !optionalRound.req[0] &&
    ZuCSpan{optionalRound.tail} == "tail");

  out.null();
  ZfTOML::save(out, value);
  auto round = ZfTOML::handler<TOMLNumbers>(
    ZfTOML::scan(out).p<1>()).ctor();
  ZuCheck(round.i == value.i && round.hex == value.hex &&
    round.enum_ == value.enum_ && round.flags == value.flags &&
    round.fixed == value.fixed && round.decimal == value.decimal &&
    round.time == value.time);
}

static void tableArrays()
{
  ZuTestScope(tableArrays);

  TOMLCatalog value;
  value.title = "tools";
  value.products.push(TOMLProduct{"hammer", 1});
  value.products.push(TOMLProduct{"nail", 20});
  ZtString<> out;
  ZfTOML::save(out, value);
  ZuCheck(out ==
    "title = \"tools\"\n"
    "[[products]]\nname = \"hammer\"\ncount = 1\n"
    "[[products]]\nname = \"nail\"\ncount = 20\n");
  auto round = ZfTOML::handler<TOMLCatalog>(ZfTOML::scan(out).p<1>()).ctor();
  ZuCheck(round.products.length() == 2);
  ZuCheck(round.products[1].name == "nail");

  auto inlineInput = ZfTOML::handler<TOMLCatalog>(ZfTOML::scan(
    "title = \"tools\"\n"
    "products = [{name = \"hammer\", count = 1}, "
      "{name = \"nail\", count = 20}]\n").p<1>()).ctor();
  ZuCheck(inlineInput.products.length() == 2 &&
    inlineInput.products[0].name == "hammer");
  auto tableInput = ZfTOML::handler<TOMLCatalogInline>(
    ZfTOML::scan(out).p<1>()).ctor();
  ZuCheck(tableInput.products.length() == 2 &&
    tableInput.products[1].count == 20);

  out.null();
  ZfTOML::saveUpd(out, value);
  ZuCheck(out ==
    "title = \"tools\"\n"
    "[[products]]\nname = \"hammer\"\ncount = 1\n"
    "[[products]]\nname = \"nail\"\ncount = 20\n");
  out.null();
  ZfTOML::saveDel(out, value);
  ZuCheck(out == "title = \"tools\"\n");

  TOMLSiblingTables siblings;
  siblings.tools.push(TOMLProduct{"hammer", 1});
  siblings.supplies.push(TOMLProduct{"nail", 20});
  out.null();
  ZfTOML::save(out, siblings);
  ZuCheck(out ==
    "[[tools]]\nname = \"hammer\"\ncount = 1\n"
    "[[supplies]]\nname = \"nail\"\ncount = 20\n");
  auto siblingRound = ZfTOML::handler<TOMLSiblingTables>(
    ZfTOML::scan(out).p<1>()).ctor();
  ZuCheck(siblingRound.tools.length() == 1 &&
    siblingRound.supplies.length() == 1 &&
    siblingRound.supplies[0].name == "nail");
}

static void keysOwnershipBOM()
{
  ZuTestScope(keysOwnershipBOM);

  ZtString<> source;
  source << "\xef\xbb\xbf" <<
    "bare-key = \"value\"\r\n"
    "\"a.b\".\"\" = \"quoted\"\r"
    "[outer.\"unsafe key\"]\nnumber = 1\n";
  ZtString<> original{source};
  auto tree = ZfTOML::scan(source).p<1>();
  ZuCheck(source == original);
  source.null();
  ZuCheck(tree->resolve("bare-key")->data<ZfTOML::AnyNode::String>() == "value");
  auto dotted = field(tree, "a.b");
  auto empty = field(dotted, "");
  ZuCheck(dotted);
  ZuCheck(empty);
  ZuCheck(empty && empty->data<ZfTOML::AnyNode::String>() == "quoted");
  ZuCheck(field(field(tree, "outer"), "unsafe key"));
  ZuCheck(tree->parent == nullptr);
  ZuCheck(tree->resolve("outer.unsafe key.number")->parent->parent ==
    field(tree, "outer"));

  auto unicode = ZfTOML::scan(
    "\"\xe2\x98\xba\" = 1\n123 = 2\na . b = 3\n").p<1>();
  ZuCheck(field(unicode, "\xe2\x98\xba") && field(unicode, "123") &&
    unicode->resolve("a.b"));
  ZuCheck(error("same = 1\n\"same\" = 2\n").find("duplicate key") >= 0);

  auto blank = ZfTOML::scan(" \t# comment\r\n\r").p<1>();
  ZuCheck(blank->has<ZfTOML::AnyNode::Object>() &&
    !blank->data<ZfTOML::AnyNode::Object>().length());

  TOMLKeys keys{1, 2, 3, 4, 5, 6};
  ZtString<> out;
  ZfTOML::save(out, keys);
  ZuCheck(out ==
    "AZaz-09_ = 1\n123 = 2\n\"a.b\" = 3\n\"space key\" = 4\n"
    "\"\\u0001\" = 5\n\"\\u263a\" = 6\n");
  auto keysRound = ZfTOML::handler<TOMLKeys>(
    ZfTOML::scan(out).p<1>()).ctor();
  ZuCheck(keysRound.safe == 1 && keysRound.numeric == 2 &&
    keysRound.dot == 3 && keysRound.space == 4 &&
    keysRound.control == 5 && keysRound.unicode == 6);
}

static void numbersDates()
{
  ZuTestScope(numbersDates);

  auto tree = ZfTOML::scan(
    "dec = 42\nhex = 0x2a\noct = 0o52\nbin = 0b101010\n"
    "minimum = -9223372036854775808\nmaximum = 9223372036854775807\n"
    "float = 1_2.5E+1\nzero = -0.0\npos = +inf\nneg = -inf\nnan = -nan\n"
    "offset = 1979-05-27T07:32:00.123456789-07:00\n"
    "local = 2024-02-29t07:32\ndate = 1979-05-27\ntime = 07:32\n"
    "space = 1979-05-27 07:32z\nlower = 1979-05-27t07:32:00z\n"
    "quoted = \"1979-05-27T07:32:00Z\"\n").p<1>();
  ZuCheck(field(tree, "hex")->data<ZfTOML::AnyNode::String>() == "0x2a");
  ZuCheck(field(tree, "minimum")->data<ZfTOML::AnyNode::String>() ==
    "-9223372036854775808");
  ZuCheck(field(tree, "maximum")->data<ZfTOML::AnyNode::String>() ==
    "9223372036854775807");
  ZuCheck(field(tree, "float")->data<ZfTOML::AnyNode::String>() ==
    "1_2.5E+1");
  ZuCheck(field(tree, "zero")->data<ZfTOML::AnyNode::String>() == "-0.0");
  ZuCheck(field(tree, "offset")->has<ZfTOML::AnyNode::DateTime>());
  ZuCheck(field(tree, "local")->has<ZfTOML::AnyNode::DateTime>() &&
    field(tree, "date")->has<ZfTOML::AnyNode::DateTime>() &&
    field(tree, "time")->has<ZfTOML::AnyNode::DateTime>());
  ZuCheck(field(tree, "quoted")->has<ZfTOML::AnyNode::String>());
  ZuCheck(field(tree, "space")->has<ZfTOML::AnyNode::DateTime>() &&
    field(tree, "lower")->has<ZfTOML::AnyNode::DateTime>());
  int y, m, d, H, M, S;
  field(tree, "offset")->data<ZfTOML::AnyNode::DateTime>().ymd(y, m, d);
  field(tree, "offset")->data<ZfTOML::AnyNode::DateTime>().hms(H, M, S);
  ZuCheck(y == 1979 && m == 5 && d == 27 && H == 14 && M == 32 && S == 0);
  field(tree, "time")->data<ZfTOML::AnyNode::DateTime>().ymd(y, m, d);
  ZuCheck(y == 1970 && m == 1 && d == 1);
  field(tree, "date")->data<ZfTOML::AnyNode::DateTime>().hms(H, M, S);
  ZuCheck(H == 0 && M == 0 && S == 0);

  auto dates = ZfTOML::handler<TOMLDates>(tree).ctor();
  ZuCheck(dates.offset == field(tree, "offset")->
    data<ZfTOML::AnyNode::DateTime>() &&
    dates.local == field(tree, "local")->data<ZfTOML::AnyNode::DateTime>() &&
    dates.date == field(tree, "date")->data<ZfTOML::AnyNode::DateTime>() &&
    dates.time == field(tree, "time")->data<ZfTOML::AnyNode::DateTime>());
  ZtString<> datesOut;
  ZfTOML::save(datesOut, dates);
  ZuCheck(datesOut ==
    "offset = 1979-05-27T14:32:00.123456789Z\n"
    "local = 2024-02-29T07:32:00Z\n"
    "date = 1979-05-27T00:00:00Z\n"
    "time = 1970-01-01T07:32:00Z\n");
  auto datesRound = ZfTOML::handler<TOMLDates>(
    ZfTOML::scan(datesOut).p<1>()).ctor();
  ZuCheck(datesRound.offset == dates.offset && datesRound.local == dates.local &&
    datesRound.date == dates.date && datesRound.time == dates.time);

  TOMLStringDates stringDates{
    dates.offset, dates.offset, dates.offset, dates.offset, dates.offset};
  ZtString<> stringDatesOut;
  ZfTOML::save(stringDatesOut, stringDates);
  ZuCheck(stringDatesOut ==
    "native = \"1979/05/27 14:32:00.123456789\"\n"
    "basic = \"1979/05/27 14:32:00.123456789\"\n"
    "literal = '1979/05/27 14:32:00.123456789'\n"
    "multiBasic = \"\"\"1979/05/27 14:32:00.123456789\"\"\"\n"
    "multiLiteral = '''1979/05/27 14:32:00.123456789'''\n");
  auto stringDatesRound = ZfTOML::handler<TOMLStringDates>(
    ZfTOML::scan(stringDatesOut).p<1>()).ctor();
  ZuCheck(stringDatesRound.native == dates.offset &&
    stringDatesRound.basic == dates.offset &&
    stringDatesRound.literal == dates.offset &&
    stringDatesRound.multiBasic == dates.offset &&
    stringDatesRound.multiLiteral == dates.offset);

  const char *open[] = {"\"", "'", "\"\"\"", "'''"};
  const char *close[] = {"\"", "'", "\"\"\"", "'''"};
  {
    ZuTestRepeat(inputDateStringForms, 4);
    for (unsigned i = 0; i < 4; ++i) {
      ZtString<> source;
      for (auto id: {"native", "basic", "literal", "multiBasic",
          "multiLiteral"})
        source << id << " = " << open[i]
          << "1979/05/27 14:32:00.123456789" << close[i] << '\n';
      auto loaded = ZfTOML::handler<TOMLStringDates>(
        ZfTOML::scan(source).p<1>()).ctor();
      ZuCheck(loaded.native == dates.offset && loaded.basic == dates.offset &&
        loaded.literal == dates.offset &&
        loaded.multiBasic == dates.offset &&
        loaded.multiLiteral == dates.offset);
    }
  }

  auto decimal = ZfTOML::handler<TOMLFormats>(
    ZfTOML::scan("hex = 42\n").p<1>()).ctor();
  auto prefixed = ZfTOML::handler<TOMLFormats>(
    ZfTOML::scan("hex = 0x2a\n").p<1>()).ctor();
  auto octal = ZfTOML::handler<TOMLFormats>(
    ZfTOML::scan("hex = 0o52\n").p<1>()).ctor();
  auto binary = ZfTOML::handler<TOMLFormats>(
    ZfTOML::scan("hex = 0b101010\n").p<1>()).ctor();
  ZuCheck(decimal.hex == 42 && prefixed.hex == 42 && octal.hex == 42 &&
    binary.hex == 42);
  ZtString<> out;
  ZfTOML::save(out, prefixed);
  ZuCheck(out == "hex = 0x0000002a\n");
  ZuCheck(error("x = 9223372036854775808\n").find("integer out of range") >= 0);
  ZuCheck(error("x = -9223372036854775809\n").find("integer out of range") >= 0);
  ZuCheck(error("x = -0x1\n"));

  auto floats = ZfTOML::handler<TOMLFloats>(ZfTOML::scan(
    "finite = +1_2.5e-1\ninf = +inf\nnan = -nan\n").p<1>()).ctor();
  ZuCheck(floats.finite == 1.25 &&
    floats.inf == ZuCmp<double>::inf() && floats.nan != floats.nan);
  ZtString<> floatOut;
  ZfTOML::save(floatOut, floats);
  ZuCheck(floatOut == "finite = 1.25\ninf = inf\nnan = nan\n");
  ZuCheck(loadError([] {
    ZfTOML::handler<TOMLFloats>(ZfTOML::scan(
      "finite = 1e1000000\n").p<1>()).ctor();
  }));
  ZuCheck(loadError([] {
    ZfTOML::handler<TOMLWideNumbers>(ZfTOML::scan(
      "decimal = 1e1000000\n").p<1>()).ctor();
  }));
  ZuCheck(loadError([] {
    ZfTOML::handler<TOMLWideNumbers>(ZfTOML::scan(
      "fixed = 1e1000000\n").p<1>()).ctor();
  }));
  {
    ZuTestRepeat(invalidDates, 5);
    for (auto bad: {
      ZuCSpan{"x = 2023-02-29\n"}, ZuCSpan{"x = 24:00\n"},
      ZuCSpan{"x = 12:60\n"}, ZuCSpan{"x = 12:00:60\n"},
      ZuCSpan{"x = 2024-01-01T12:00+24:00\n"}})
      ZuCheck(error(bad).find("invalid date/time") >= 0);
  }
}

static void stateLimits()
{
  ZuTestScope(stateLimits);

  const char *bad[] = {
    "a.b = 1\n[a]\nc = 2\n",
    "a = { b = 1 }\na.c = 2\n",
    "a = { b = {} }\n[a.b]\nc = 1\n",
    "a = { b = [] }\n[[a.b]]\nc = 1\n",
    "a = []\n[[a]]\n",
    "[a]\n[[a]]\n"
  };
  unsigned failures = 0;
  for (auto input: bad) if (error(input)) ++failures;
  ZuCheck(failures == sizeof(bad) / sizeof(bad[0]));
  ZuCheck(error("a = 1\n", ZfTOML::Limits{128, 1}).find("node limit") >= 0);
  ZuCheck(error("a = [[1]]\n", ZfTOML::Limits{1, 64}).find("depth limit") >= 0);

  ZtString<> many;
  for (unsigned i = 0; i < 40; ++i)
    many << "[table" << i << "]\nvalue = " << i << '\n';
  auto tree = ZfTOML::scan(many).p<1>();
  ZuCheck(tree->resolve("table39.value"));

  ZuCheck(ZfTOML::scan("a = 1\n", ZfTOML::Limits{128, 2}).p<0>() == 6);
  ZuCheck(ZfTOML::scan("a = [[1]]\n", ZfTOML::Limits{2, 64}).p<0>() == 10);

  {
    ZfTOML::Scan scan{"[a.b]\n", ZfTOML::Limits{128, 2}};
    auto result = scan.scan();
    ZuCheck(result.p<0>() < 0 && !result.p<1>() &&
      scan.error().code == ZfTOML::SyntaxErrorCode::NodeMax &&
      scan.error().offset == 0);
  }
  ZuCheck(ZfTOML::scan("[a.b]\n", ZfTOML::Limits{128, 3}).p<0>() == 6);
  {
    ZfTOML::Scan scan{"[[a.b]]\n", ZfTOML::Limits{128, 3}};
    auto result = scan.scan();
    ZuCheck(result.p<0>() < 0 && !result.p<1>() &&
      scan.error().code == ZfTOML::SyntaxErrorCode::NodeMax &&
      scan.error().offset == 0);
  }
  ZuCheck(ZfTOML::scan("[[a.b]]\n", ZfTOML::Limits{128, 4}).p<0>() == 8);
  {
    ZfTOML::Scan scan{"a.b = 1\n", ZfTOML::Limits{128, 2}};
    auto result = scan.scan();
    ZuCheck(result.p<0>() < 0 && !result.p<1>() &&
      scan.error().code == ZfTOML::SyntaxErrorCode::NodeMax &&
      scan.error().offset == 0);
  }
  ZuCheck(ZfTOML::scan("a.b = 1\n", ZfTOML::Limits{128, 3}).p<0>() == 8);
}

static void grammarMatrix()
{
  ZuTestScope(grammarMatrix);

  const char *valid[] = {
    "",
    "# Unicode \xe2\x98\xba\r\n\r# bare CR\r",
    "a.b.c = 1\na.b.d = 2\n",
    "[a.b.c]\nx = 1\n[a]\ny = 2\n[a.b]\nz = 3\n",
    "values = [\n  1, # first\n  2,\n]\n"
      "point = {\n  x = 1,\n  y = 2,\n}\n",
    "[[fruits]]\nname = \"apple\"\n"
      "[fruits.physical]\ncolor = \"red\"\n"
      "[[fruits.varieties]]\nname = \"red delicious\"\n"
      "[[fruits.varieties]]\nname = \"granny smith\"\n"
      "[[fruits]]\nname = \"banana\"\n"
      "[[fruits.varieties]]\nname = \"plantain\"\n",
    "hex = 0xdead_beef\noct = 0o755\nbin = 0b1010_0110\n"
      "float = +1_2.5e-1\nwhen = 2026-08-15T12:34Z\n"
  };
  {
    ZuTestRepeat(validGrammar, 7);
    for (auto input: valid)
      ZuCheck(ZfTOML::scan(input).p<0>() == int(ZuCSpan{input}.length()));
  }

  const char *invalid[] = {
    "a = 1\na = 2\n",
    "a = 1\na.b = 2\n",
    "a.b = 1\n[a]\n",
    "a = {b = 1}\na.c = 2\n",
    "a = {b = 1, b = 2}\n",
    "[a]\n[a]\n",
    "[[a]]\n[a]\n",
    "a = []\n[[a]]\n",
    "a = [{b = 1}]\n[[a]]\n",
    "a = 0x_1\n",
    "a = 1__0\n",
    "a = 2026-02-30\n",
    "a = TRUE\n"
  };
  {
    ZuTestRepeat(invalidGrammar, 13);
    for (auto input: invalid) ZuCheck(error(input));
  }
  ZuCheck(error("ok=1\nbad=[1,,2]\n").find(
    "line 2, column 8 (offset 12)") >= 0);
}

static void definitionConflicts()
{
  ZuTestScope(definitionConflicts);

  struct Case {
    const char	*input;
    uint8_t	code;
    unsigned	offset;
    unsigned	line;
    unsigned	column;
    const char	*reason;
  };
  const Case cases[] = {
    {"a = 1\na = 2\n", ZfTOML::SyntaxErrorCode::DuplicateKey,
      6, 2, 1, "duplicate key"},
    {"a = 1\na.b = 2\n", ZfTOML::SyntaxErrorCode::TableConflict,
      6, 2, 1, "table conflict"},
    {"a.b = 1\n[a]\n", ZfTOML::SyntaxErrorCode::Redefine,
      8, 2, 1, "redefined table"},
    {"a = {b = 1}\na.c = 2\n", ZfTOML::SyntaxErrorCode::InlineSealed,
      12, 2, 1, "sealed inline table"},
    {"a = {b = 1, b = 2}\n", ZfTOML::SyntaxErrorCode::DuplicateKey,
      12, 1, 13, "duplicate key"},
    {"[a]\n[a]\n", ZfTOML::SyntaxErrorCode::Redefine,
      4, 2, 1, "redefined table"},
    {"[[a]]\n[a]\n", ZfTOML::SyntaxErrorCode::TableConflict,
      6, 2, 1, "table conflict"},
    {"a = []\n[[a]]\n", ZfTOML::SyntaxErrorCode::ArrayConflict,
      7, 2, 1, "array conflict"},
    {"a = [{b = 1}]\n[[a]]\n", ZfTOML::SyntaxErrorCode::ArrayConflict,
      14, 2, 1, "array conflict"},
    {"[a]\n[[a]]\n", ZfTOML::SyntaxErrorCode::ArrayConflict,
      4, 2, 1, "array conflict"},
    {"a = 1\n[a]\n", ZfTOML::SyntaxErrorCode::TableConflict,
      6, 2, 1, "table conflict"},
    {"a = {}\n[a]\n", ZfTOML::SyntaxErrorCode::InlineSealed,
      7, 2, 1, "sealed inline table"},
    {"a.b = 1\n[[a]]\n", ZfTOML::SyntaxErrorCode::ArrayConflict,
      8, 2, 1, "array conflict"},
    {"a = []\n[a.b]\n", ZfTOML::SyntaxErrorCode::TableConflict,
      7, 2, 1, "table conflict"}
  };
  {
    ZuTestRepeat(conflicts, 14);
    for (const auto &case_: cases) {
      ZfTOML::Scan scan{case_.input};
      auto result = scan.scan();
      const auto &error = scan.error();
      ZuCHECK(result.p<0>() < 0 && error.failed &&
        error.code == case_.code && error.offset == case_.offset &&
        error.line == case_.line && error.column == case_.column &&
        ZfTOMLError::reason(error.code) == case_.reason, case_.input);
    }
  }
}

static void curatedFixtures()
{
  ZuTestScope(curatedFixtures);

  const char *valid[] = {
    "valid/arrays.toml", "valid/datetime.toml",
    "valid/strings.toml", "valid/tables.toml"
  };
  {
    ZuTestRepeat(validFixtures, 4);
    for (auto name: valid) {
      TOMLFixture data;
      bool ok = fixture(data, name);
      if (ok) {
        try { ok = ZfTOML::scan(data).p<0>() == int(data.length()); }
        catch (const ZeException &) { ok = false; }
      }
      ZuCHECK(ok, name);
    }
  }
  const char *invalid[] = {
    "invalid/array-conflict.toml", "invalid/duplicate.toml",
    "invalid/inline-sealed.toml", "invalid/value-date.toml",
    "invalid/value-number.toml"
  };
  {
    ZuTestRepeat(invalidFixtures, 5);
    for (auto name: invalid) {
      TOMLFixture data;
      bool ok = fixture(data, name);
      if (ok) ok = bool(error(data));
      ZuCHECK(ok, name);
    }
  }
}

static void scalarFormatEdges()
{
  ZuTestScope(scalarFormatEdges);

  ZtString<> out;
  ZfTOML::saveText<ZuTypeList<ZuFieldProp::TOML::MultilineBasic>>(
    out, "\nlead\t\\\"");
  auto source = ZtString<>{"value = "};
  source << out << '\n';
  auto tree = ZfTOML::scan(source).p<1>();
  ZuCheck(field(tree, "value")->data<ZfTOML::AnyNode::String>() ==
    "\nlead\t\\\"");

  bool literal = false, multiline = false;
  try {
    ZtString<> text;
    ZfTOML::saveText<ZuTypeList<ZuFieldProp::TOML::Literal>>(
      text, "can't");
  } catch (const ZeException &) { literal = true; }
  try {
    ZtString<> text;
    ZfTOML::saveText<ZuTypeList<ZuFieldProp::TOML::MultilineLiteral>>(
      text, "''' delimiter");
  } catch (const ZeException &) { multiline = true; }
  ZuCheck(literal && multiline);
  auto diagnostic = loadException([] {
    ZtString<> text;
    ZfTOML::saveText<ZuTypeList<ZuFieldProp::TOML::Literal>>(
      text, "can't", "field");
  });
  ZuCheck(diagnostic.find("\"field\"") >= 0 &&
    diagnostic.find("scalar style") >= 0);

  const char *forms[] = {
    "value = \"same\"\n", "value = 'same'\n",
    "value = \"\"\"same\"\"\"\n", "value = '''same'''\n"
  };
  {
    ZuTestRepeat(scalarInputForms, 4);
    for (auto form: forms)
      ZuCheck(field(ZfTOML::scan(form).p<1>(), "value")->
        data<ZfTOML::AnyNode::String>() == "same");
  }

  {
    ZtString<> text;
    ZfTOML::saveText<ZuTypeList<ZuFieldProp::TOML::Basic>>(
      text, "a'\\\t\"\"\"\n\xe2\x98\xba");
    ZuCheck(text == "\"a'" "\\\\" "\\t" "\\\"\\\"\\\"" "\\n"
      "\\u263a" "\"");
    auto input = ZtString<>{"value = "} << text << '\n';
    ZuCheck(field(ZfTOML::scan(input).p<1>(), "value")->
      data<ZfTOML::AnyNode::String>() == "a'\\\t\"\"\"\n\xe2\x98\xba");
  }
  {
    const char control[] = {char(1)};
    ZtString<> text;
    ZfTOML::saveText<ZuTypeList<ZuFieldProp::TOML::Basic>>(
      text, ZuCSpan{control, 1});
    ZuCheck(text == "\"\\u0001\"");
    auto input = ZtString<>{"value = "} << text << '\n';
    ZuCSpan expected{control, 1};
    ZuCheck(field(ZfTOML::scan(input).p<1>(), "value")->
      data<ZfTOML::AnyNode::String>() == expected);
  }
  {
    ZtString<> empty, quotes, newlines;
    ZfTOML::saveText<ZuTypeList<ZuFieldProp::TOML::MultilineBasic>>(
      empty, "");
    ZfTOML::saveText<ZuTypeList<ZuFieldProp::TOML::MultilineBasic>>(
      quotes, "\"\"\"\\");
    ZfTOML::saveText<ZuTypeList<ZuFieldProp::TOML::MultilineBasic>>(
      newlines, "\ninside\n");
    ZuCheck(empty == "\"\"\"\"\"\"" &&
      quotes == "\"\"\"\\\"\\\"\\\"\\\\\"\"\"" &&
      newlines == "\"\"\"\\ninside\n\"\"\"");
    auto input = ZtString<>{"value = "} << newlines << '\n';
    ZuCheck(field(ZfTOML::scan(input).p<1>(), "value")->
      data<ZfTOML::AnyNode::String>() == "\ninside\n");
  }
  {
    ZtString<> empty, text;
    ZfTOML::saveText<ZuTypeList<ZuFieldProp::TOML::Literal>>(empty, "");
    ZfTOML::saveText<ZuTypeList<ZuFieldProp::TOML::MultilineLiteral>>(
      text, "\ninside\n");
    ZuCheck(empty == "''" && text == "'''\n\ninside\n'''");
    auto input = ZtString<>{"value = "} << text << '\n';
    ZuCheck(field(ZfTOML::scan(input).p<1>(), "value")->
      data<ZfTOML::AnyNode::String>() == "\ninside\n");
  }
  {
    ZuTestRepeat(invalidLiteralOutput, 3);
    for (auto value: {ZuCSpan{"apostrophe's"}, ZuCSpan{"line\nbreak"},
        ZuCSpan{"\x7f", 1}})
      ZuCheck(loadError([value] {
        ZtString<> text;
        ZfTOML::saveText<ZuTypeList<ZuFieldProp::TOML::Literal>>(text, value);
      }));
  }
  {
    ZuTestRepeat(invalidMultilineLiteralOutput, 2);
    for (auto value: {ZuCSpan{"'''"}, ZuCSpan{"\x7f", 1}})
      ZuCheck(loadError([value] {
        ZtString<> text;
        ZfTOML::saveText<ZuTypeList<ZuFieldProp::TOML::MultilineLiteral>>(
          text, value);
      }));
  }
}

static void facetsNestedTables()
{
  ZuTestScope(facetsNestedTables);

  TOMLFacet facet{42};
  ZtString<> json, toml;
  ZfJSON::save(json, facet);
  ZfTOML::save(toml, facet);
  ZuCheck(json == "{\"json-value\":42}");
  ZuCheck(toml == "\"toml.value\" = 0x0000002a\n");

  TOMLNestedTables value;
  TOMLParent parent;
  parent.name = "parent";
  parent.children.push(TOMLChild{"child"});
  value.parents.push(ZuMv(parent));
  ZtString<> out;
  ZfTOML::save(out, value);
  ZuCheck(out ==
    "[[parents]]\nname = \"parent\"\n"
    "[[parents.children]]\nname = \"child\"\n");
  auto round = ZfTOML::handler<TOMLNestedTables>(
    ZfTOML::scan(out).p<1>()).ctor();
  ZuCheck(round.parents.length() == 1);
  ZuCheck(round.parents[0].children.length() == 1);
  ZuCheck(round.parents[0].children[0].name == "child");

  TOMLGroupedTables grouped;
  grouped.group.id = "group";
  grouped.group.parents.push(TOMLParent{"nested", {}});
  out.null();
  ZfTOML::save(out, grouped);
  ZuCheck(out ==
    "[\"unsafe.group\"]\nid = \"group\"\n"
    "[[\"unsafe.group\".\"child group\"]]\nname = \"nested\"\n"
    "children = []\n");
  auto groupedRound = ZfTOML::handler<TOMLGroupedTables>(
    ZfTOML::scan(out).p<1>()).ctor();
  ZuCheck(groupedRound.group.parents.length() == 1);
}

static void runtimeSaveContext()
{
  ZuTestScope(runtimeSaveContext);

  TOMLSaveContextInline value;
  value.value.catalog.title = "tools";
  value.value.catalog.products.push(TOMLProduct{"hammer", 1});
  value.value.objects = new TOMLObjMap{};
  value.value.objects->add("one", TOMLNested{2});
  value.value.mixed = TOMLUnionA{3};
  TOMLObjectUnion elem;
  elem = TOMLUnionB{4};
  value.value.unions.push(ZuMv(elem));

  ZtString<> out;
  ZfTOML::save(out, value);
  ZuCheck(out ==
    "value = {catalog = {title = \"tools\", products = "
      "[{name = \"hammer\", count = 1}]}, "
    "objects = {one = {value = 2}}, mixed = {foo = 3}, "
      "unions = [{bar = 4}]}\n");

  auto tree = ZfTOML::scan(out);
  auto round = ZfTOML::handler<TOMLSaveContextInline>(tree.p<1>()).ctor();
  ZuCheck(round.value.catalog.products.length() == 1 &&
    round.value.catalog.products[0].name == "hammer");
  ZuCheck(round.value.objects->findVal("one").value == 2);
  auto mixedNode = round.value.mixed.p<const ZfTOML::AnyNode *>();
  ZuCheck(mixedNode && mixedNode->has<ZfTOML::AnyNode::Object>());
  round.value.mixed = ZfTOML::handler<TOMLUnionA>(mixedNode).ctor();
  auto unionNode =
    round.value.unions[0].p<const ZfTOML::AnyNode *>();
  ZuCheck(unionNode && unionNode->has<ZfTOML::AnyNode::Object>());
  round.value.unions[0] = ZfTOML::handler<TOMLUnionB>(unionNode).ctor();
  ZtString<> saved;
  ZfTOML::save(saved, round);
  ZuCheck(saved == out);

  TOMLSaveContextTable table;
  table.value = ZuMv(round.value);
  out.null();
  ZfTOML::save(out, table);
  ZuCheck(out ==
    "[value]\n"
    "\n[value.catalog]\ntitle = \"tools\"\n"
    "[[value.catalog.products]]\nname = \"hammer\"\ncount = 1\n"
    "[value.objects]\n"
    "\n[value.objects.one]\nvalue = 2\n"
    "[value.mixed]\nfoo = 3\n"
    "[[value.unions]]\nbar = 4\n");

  auto tableRound = ZfTOML::handler<TOMLSaveContextTable>(
    ZfTOML::scan(out).p<1>()).ctor();
  ZuCheck(tableRound.value.catalog.products.length() == 1 &&
    tableRound.value.objects->findVal("one").value == 2);

  TOMLSaveContextInline alternatives;
  alternatives.value.catalog.title = "empty";
  alternatives.value.objects = new TOMLObjMap{};
  alternatives.value.mixed = TOMLUnionText{"text"};
  out.null();
  ZfTOML::save(out, alternatives);
  ZuCheck(out ==
    "value = {catalog = {title = \"empty\", products = []}, "
    "objects = {}, mixed = \"text\", unions = []}\n");

  TOMLUnionArray numbers;
  numbers.push(5);
  alternatives.value.mixed = ZuMv(numbers);
  out.null();
  ZfTOML::save(out, alternatives);
  ZuCheck(out ==
    "value = {catalog = {title = \"empty\", products = []}, "
    "objects = {}, mixed = [5], unions = []}\n");

  TOMLIntMap map;
  map.add("answer", 6);
  alternatives.value.mixed = ZuMv(map);
  out.null();
  ZfTOML::save(out, alternatives);
  ZuCheck(out ==
    "value = {catalog = {title = \"empty\", products = []}, "
    "objects = {}, mixed = {answer = 6}, unions = []}\n");

  TOMLPtrInlineHolder pointers;
  pointers.value.object = new TOMLPtrObj{7};
  pointers.value.objects = new TOMLPtrArray{};
  pointers.value.objects->push(ZmRef<TOMLPtrObj>{new TOMLPtrObj{8}});
  out.null();
  ZfTOML::save(out, pointers);
  ZuCheck(out ==
    "value = {object = {value = 7}, objects = [{value = 8}]}\n");
  auto pointerRound = ZfTOML::handler<TOMLPtrInlineHolder>(
    ZfTOML::scan(out).p<1>()).ctor();
  ZuCheck(pointerRound.value.object &&
    pointerRound.value.object->value == 7 &&
    pointerRound.value.objects &&
    pointerRound.value.objects->length() == 1 &&
    (*pointerRound.value.objects)[0] &&
    (*pointerRound.value.objects)[0]->value == 8);

  pointers.value.object = nullptr;
  ZuCheck(loadError([&out, &pointers] {
    out.null();
    ZfTOML::save(out, pointers);
  }));
}

static void maps()
{
  ZuTestScope(maps);

  auto root = ZfTOML::scan(
    "alpha = 1\n"
    "\"a.b\" = 2\n").p<1>();
  auto map = ZmRef(ZfTOML::handler<TOMLIntMap>(root).alloc());
  ZuCheck(map);
  ZuCheck(map->count_() == 2);
  ZuCheck(map->findVal("alpha") == 1);
  ZuCheck(map->findVal("a.b") == 2);

  ZtString<> out;
  ZfTOML::save(out, map);
  ZuCheck(out == "\"a.b\" = 2\nalpha = 1\n");
  ZtString<> direct;
  ZfTOML::save(direct, *map);
  ZuCheck(direct == out);
  direct.null();
  ZfTOML::save(direct, map.ptr());
  ZuCheck(direct == out);
  auto round = ZmRef(ZfTOML::handler<TOMLIntMap>(
    ZfTOML::scan(out).p<1>()).alloc());
  ZuCheck(round->findVal("a.b") == 2);

  auto ptr = map.ptr();
  auto load = ZfTOML::scan("loaded = 3\n").p<1>();
  ZfTOML::handler<TOMLIntMap>(load).load(*map);
  ZuCheck(map.ptr() == ptr);
  ZuCheck(map->count_() == 1);
  ZuCheck(map->findVal("loaded") == 3);

  map->add("kept", 4);
  auto update = ZfTOML::scan("loaded = 5\nadded = 6\n").p<1>();
  ZfTOML::handler<TOMLIntMap>(update).update(*map);
  ZuCheck(map.ptr() == ptr);
  ZuCheck(map->count_() == 3);
  ZuCheck(map->findVal("loaded") == 5);
  ZuCheck(map->findVal("kept") == 4);
  ZuCheck(map->findVal("added") == 6);

  auto storage = ZmAlloc(TOMLIntMap, 1);
  ZfTOML::handler<TOMLIntMap>(load).new_(storage.data);
  auto &placed = storage[0];
  ZuCheck(placed.findVal("loaded") == 3);
  placed.~TOMLIntMap();

  TOMLMapHolder holder;
  holder.inline_ = new TOMLIntMap{};
  holder.inline_->add("a.b", 7);
  holder.table = new TOMLIntMap{};
  holder.table->add("beta", 8);
  holder.objects = new TOMLObjMap{};
  holder.objects->add("one", TOMLNested{9});
  out.null();
  ZfTOML::save(out, holder);
  ZuCheck(out ==
    "inline_ = {\"a.b\" = 7}\n"
    "objects = {one = {value = 9}}\n"
    "[table]\nbeta = 8\n");
  auto holderRound = ZfTOML::handler<TOMLMapHolder>(
    ZfTOML::scan(out).p<1>()).ctor();
  ZuCheck(holderRound.inline_->findVal("a.b") == 7);
  ZuCheck(holderRound.table->findVal("beta") == 8);
  ZuCheck(holderRound.objects->findVal("one").value == 9);

  TOMLParentMapHolder nested;
  nested.parents = new TOMLParentMap{};
  TOMLParent parent{"parent", {}};
  parent.children.push(TOMLChild{"child"});
  nested.parents->add("a.b", ZuMv(parent));
  out.null();
  ZfTOML::save(out, nested);
  ZuCheck(out ==
    "[parents]\n"
    "\n[parents.\"a.b\"]\nname = \"parent\"\n"
    "[[parents.\"a.b\".children]]\nname = \"child\"\n");
  auto nestedRound = ZfTOML::handler<TOMLParentMapHolder>(
    ZfTOML::scan(out).p<1>()).ctor();
  auto nestedParent = nestedRound.parents->findVal("a.b");
  ZuCheck(nestedParent.name == "parent");
  ZuCheck(nestedParent.children.length() == 1);
  ZuCheck(nestedParent.children[0].name == "child");

  TOMLIntMapRef empty = new TOMLIntMap{};
  out.null();
  ZfTOML::save(out, empty);
  ZuCheck(!out);
  auto emptyRound = ZfTOML::handler<TOMLIntMap>(
    ZfTOML::scan("\n").p<1>()).ctor();
  ZuCheck(!emptyRound.count_());

  TOMLIntMapRef null;
  ZuCheck(loadError([&out, &null] {
    out.null();
    ZfTOML::save(out, null);
  }));
}

static ZtString<> interopText()
{
  TOMLInterop value;
  value.title = "tools";
  value.enabled = true;
  value.values.push("one");
  value.values.push("two");
  value.nested.value = 7;
  value.when = ZuDateTime{2024, 2, 29, 12, 34, 56};
  value.inlineMap = new TOMLIntMap{};
  value.inlineMap->add("a.b", 11);
  value.tableMap = new TOMLIntMap{};
  value.tableMap->add("beta", 12);
  value.products.push(TOMLProduct{"hammer", 1});
  value.products.push(TOMLProduct{"nail", 20});
  ZtString<> out;
  ZfTOML::save(out, value);
  return out;
}

static void interoperabilityOutput()
{
  ZuTestScope(interoperabilityOutput);
  auto out = interopText();
  ZuCheck(out ==
    "title = \"tools\"\nenabled = true\nvalues = [\"one\", \"two\"]\n"
    "when = 2024-02-29T12:34:56Z\n"
    "inlineMap = {\"a.b\" = 11}\n"
    "[nested]\nvalue = 7\n"
    "[tableMap]\nbeta = 12\n"
    "[[products]]\nname = \"hammer\"\ncount = 1\n"
    "[[products]]\nname = \"nail\"\ncount = 20\n");
}

static void unions()
{
  ZuTestScope(unions);

  TOMLUnionHolder value;
  value.value = TOMLUnionA{42};
  ZtString<> saved;
  ZfTOML::save(saved, value);
  auto tree = ZfTOML::scan(saved);
  auto loaded = ZfTOML::handler<TOMLUnionHolder>(tree.p<1>()).ctor();
  auto node = loaded.value.p<const ZfTOML::AnyNode *>();
  loaded.value = ZfTOML::handler<TOMLUnionA>(node).ctor();
  ZtString<> round;
  ZfTOML::save(round, loaded);
  ZuCheck(round == saved);

  TOMLMixedHolder mixed;
  mixed.value = TOMLUnionA{7};
  saved.null();
  ZfTOML::save(saved, mixed);
  ZuCheck(saved == "[value]\nfoo = 7\n");
  TOMLMixedInlineHolder mixedInline;
  mixedInline.value = TOMLUnionA{8};
  saved.null();
  ZfTOML::save(saved, mixedInline);
  ZuCheck(saved == "value = {foo = 8}\n");

  TOMLUnionArray numbers;
  numbers.push(1);
  numbers.push(2);
  numbers.push(3);
  mixed.value = ZuMv(numbers);
  saved.null();
  ZfTOML::save(saved, mixed);
  ZuCheck(saved == "value = [1, 2, 3]\n");
  auto arrayTree = ZfTOML::scan(saved);
  auto arrayRaw = ZfTOML::handler<TOMLMixedHolder>(arrayTree.p<1>()).ctor();
  auto arrayNode = arrayRaw.value.p<const ZfTOML::AnyNode *>();
  ZuCheck(arrayNode && arrayNode->has<ZfTOML::AnyNode::Array>());
  arrayRaw.value = ZfTOML::handler<TOMLUnionArray>(arrayNode).ctor();
  ZuCheck(arrayRaw.value.p<TOMLUnionArray>().length() == 3);

  auto scalarTree = ZfTOML::scan("value = \"text\"\n");
  auto scalarRaw = ZfTOML::handler<TOMLMixedHolder>(scalarTree.p<1>()).ctor();
  auto scalarNode = scalarRaw.value.p<const ZfTOML::AnyNode *>();
  ZuCheck(scalarNode && scalarNode->has<ZfTOML::AnyNode::String>());
  scalarRaw.value = ZfTOML::handler<TOMLUnionText>(scalarNode).ctor();
  saved.null();
  ZfTOML::save(saved, scalarRaw);
  ZuCheck(saved == "value = \"text\"\n");
  mixedInline.value = TOMLUnionText{"inline"};
  saved.null();
  ZfTOML::save(saved, mixedInline);
  ZuCheck(saved == "value = \"inline\"\n");

  auto numberTree = ZfTOML::scan("value = 42\n");
  auto numberRaw = ZfTOML::handler<TOMLMixedHolder>(numberTree.p<1>()).ctor();
  ZuCheck(numberRaw.value.p<const ZfTOML::AnyNode *>()->scalarType ==
    ZfTOML::ScalarTC::Number);
  auto boolTree = ZfTOML::scan("value = true\n");
  auto boolRaw = ZfTOML::handler<TOMLMixedHolder>(boolTree.p<1>()).ctor();
  ZuCheck(boolRaw.value.p<const ZfTOML::AnyNode *>()->scalarType ==
    ZfTOML::ScalarTC::True);
  auto dateTree = ZfTOML::scan("value = 1979-05-27T07:32:00Z\n");
  auto dateRaw = ZfTOML::handler<TOMLMixedHolder>(dateTree.p<1>()).ctor();
  ZuCheck(dateRaw.value.p<const ZfTOML::AnyNode *>()->
    has<ZfTOML::AnyNode::DateTime>());

  TOMLIntMap map;
  map.add("answer", 42);
  mixed.value = ZuMv(map);
  saved.null();
  ZfTOML::save(saved, mixed);
  ZuCheck(saved == "[value]\nanswer = 42\n");
  TOMLIntMap inlineMap;
  inlineMap.add("answer", 43);
  mixedInline.value = ZuMv(inlineMap);
  saved.null();
  ZfTOML::save(saved, mixedInline);
  ZuCheck(saved == "value = {answer = 43}\n");

  TOMLUnionArray inlineNumbers;
  inlineNumbers.push(4);
  inlineNumbers.push(5);
  mixedInline.value = ZuMv(inlineNumbers);
  saved.null();
  ZfTOML::save(saved, mixedInline);
  ZuCheck(saved == "value = [4, 5]\n");

  TOMLMixedElementsHolder elements;
  TOMLMixedUnion elem;
  elem = TOMLUnionA{9};
  elements.values.push(ZuMv(elem));
  saved.null();
  ZfTOML::save(saved, elements);
  ZuCheck(saved == "values = [{foo = 9}]\n");

  TOMLObjectUnionElementsHolder tableElements;
  TOMLObjectUnion objectElem;
  objectElem = TOMLUnionA{10};
  tableElements.values.push(ZuMv(objectElem));
  saved.null();
  ZfTOML::save(saved, tableElements);
  ZuCheck(saved == "[[values]]\nfoo = 10\n");
  TOMLObjectUnionInlineHolder inlineElements;
  TOMLObjectUnion inlineElem;
  inlineElem = TOMLUnionB{11};
  inlineElements.values.push(ZuMv(inlineElem));
  saved.null();
  ZfTOML::save(saved, inlineElements);
  ZuCheck(saved == "values = [{bar = 11}]\n");

  TOMLCatalog emptyArrays{"empty", {}};
  saved.null();
  ZfTOML::save(saved, emptyArrays);
  ZuCheck(saved == "title = \"empty\"\nproducts = []\n");

  TOMLMixedUnion rootScalar;
  rootScalar = TOMLUnionText{"root"};
  ZuCheck(loadError([&saved, &rootScalar] {
    saved.null();
    ZfTOML::save(saved, rootScalar);
  }));
  TOMLMixedUnion rootArray;
  TOMLUnionArray rootNumbers;
  rootNumbers.push(1);
  rootArray = ZuMv(rootNumbers);
  ZuCheck(loadError([&saved, &rootArray] {
    saved.null();
    ZfTOML::save(saved, rootArray);
  }));

  using MixedHandler = typename ZfTOML::As<TOMLMixedUnion>::
    template Handler<TOMLMixedUnion, ZuFacet::TOML>;
  ZuCheck(!MixedHandler::valid(nullptr));

  TOMLUnionHolder empty;
  ZuCheck(loadError([&saved, &empty] {
    saved.null();
    ZfTOML::save(saved, empty);
  }));

  ZfTOML::Union<TOMLUnionA, ZmRef<TOMLPtrObj>> pointer;
  pointer = ZmRef<TOMLPtrObj>{new TOMLPtrObj{9}};
  saved.null();
  ZfTOML::save(saved, pointer);
  ZuCheck(saved == "value = 9\n");
  pointer = ZmRef<TOMLPtrObj>{};
  ZuCheck(loadError([&saved, &pointer] {
    saved.null();
    ZfTOML::save(saved, pointer);
  }));

  TOMLTableUnionHolder table;
  table.value = TOMLUnionA{10};
  saved.null();
  ZfTOML::save(saved, table);
  ZuCheck(saved == "[value]\nfoo = 10\n");
  auto tableTree = ZfTOML::scan(saved);
  auto tableRound =
    ZfTOML::handler<TOMLTableUnionHolder>(tableTree.p<1>()).ctor();
  auto tableNode = tableRound.value.p<const ZfTOML::AnyNode *>();
  tableRound.value = ZfTOML::handler<TOMLUnionA>(tableNode).ctor();
  ZtString<> tableSaved;
  ZfTOML::save(tableSaved, tableRound);
  ZuCheck(tableSaved == saved);
}

static void pointers()
{
  ZuTestScope(pointers);

  auto tree = ZfTOML::scan(
    "object = {value = 1}\n"
    "objects = [{value = 2}, {value = 3}]\n"
    "text = \"hello\"\n");
  auto value = ZfTOML::handler<TOMLPtrHolder>(tree.p<1>()).ctor();
  ZuCheck(value.object && value.object->value == 1);
  ZuCheck(value.objects && value.objects->length() == 2);
  ZuCheck((*value.objects)[0] && (*value.objects)[0]->value == 2);
  ZuCheck(value.text && value.text->value == "hello");

  ZtString<> saved;
  ZfTOML::save(saved, value);
  ZuCheck(saved.find("text = \"hello\"") >= 0);
  ZuCheck(saved.find("[[objects]]\nvalue = 2") >= 0);

  auto first = (*value.objects)[0].ptr();
  (*value.objects)[1] = nullptr;
  auto update = ZfTOML::scan(
    "v = [{value = 4}, {value = 5}]\n");
  ZfTOML::handler<TOMLPtrArray>(
    field(update.p<1>(), "v")).update(*value.objects);
  ZuCheck((*value.objects)[0].ptr() == first && first->value == 4);
  ZuCheck((*value.objects)[1] && (*value.objects)[1]->value == 5);

  auto bad = ZfTOML::scan("v = [\"wrong\", {value = 6}]\n");
  ZuCheck(loadError([&bad, &value] {
    ZfTOML::handler<TOMLPtrArray>(
      field(bad.p<1>(), "v")).update(*value.objects);
  }));
  ZuCheck(first->value == 4);

  TOMLPtrTables tables{value.objects};
  saved.null();
  ZfTOML::save(saved, tables);
  ZuCheck(saved ==
    "[[values]]\nvalue = 4\n"
    "[[values]]\nvalue = 5\n");
  auto tableRound = ZfTOML::handler<TOMLPtrTables>(
    ZfTOML::scan(saved).p<1>()).ctor();
  ZuCheck(tableRound.values && tableRound.values->length() == 2);
  ZuCheck((*tableRound.values)[1] &&
    (*tableRound.values)[1]->value == 5);

  saved.null();
  ZfTOML::save(saved, value.object);
  ZuCheck(saved == "value = 1\n");
  ZmRef<TOMLPtrObj> null;
  ZuCheck(loadError([&saved, &null] {
    saved.null();
    ZfTOML::save(saved, null);
  }));
  ZuCheck(loadError([&saved, &null] {
    saved.null();
    ZfTOML::saveUpd(saved, null);
  }));
  ZuCheck(loadError([&saved, &null] {
    saved.null();
    ZfTOML::saveDel(saved, null);
  }));

  value.text = nullptr;
  saved.null();
  ZfTOML::save(saved, value);
  ZuCheck(saved.find("text") < 0);

  auto raw = ZfTOML::handler<TOMLPtrObj>(
    ZfTOML::scan("value = 7\n").p<1>()).alloc();
  ZuCheck(raw && raw->value == 7);
  delete raw;

  auto mapTree = ZfTOML::scan("one = {value = 8}\n");
  auto map = ZmRef(ZfTOML::handler<TOMLPtrMap>(mapTree.p<1>()).alloc());
  ZuCheck(map->findVal("one") && map->findVal("one")->value == 8);
  saved.null();
  ZfTOML::save(saved, map);
  ZuCheck(saved == "[one]\nvalue = 8\n");
}

int main(int argc, char **argv)
{
  if (argc == 2 && ZuCSpan{argv[1]} == "--interop") {
    auto out = interopText();
    return fwrite(out.data(), 1, out.length(), stdout) == out.length() ? 0 : 1;
  }
  parse(argc, argv);
  ZuTestMain();

  ZuTestCall(rootScalars);
  ZuTestCall(collections);
  ZuTestCall(strings);
  ZuTestCall(failures);
  ZuTestCall(reflection);
  ZuTestCall(scalarFormats);
  ZuTestCall(scalarFormatContexts);
  ZuTestCall(bytesFormats);
  ZuTestCall(stringNumberFormats);
  ZuTestCall(conversionMatrix);
  ZuTestCall(tableArrays);
  ZuTestCall(keysOwnershipBOM);
  ZuTestCall(numbersDates);
  ZuTestCall(stateLimits);
  ZuTestCall(grammarMatrix);
  ZuTestCall(definitionConflicts);
  ZuTestCall(curatedFixtures);
  ZuTestCall(scalarFormatEdges);
  ZuTestCall(facetsNestedTables);
  ZuTestCall(runtimeSaveContext);
  ZuTestCall(maps);
  ZuTestCall(interoperabilityOutput);
  ZuTestCall(unions);
  ZuTestCall(pointers);
}
