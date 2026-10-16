//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmAlloc.hh>
#include <zlib/ZmLHash.hh>

#include <zlib/ZfYAML.hh>

#include "ZfHeapTest.hh"
#include "ZfMapTest.hh"

using namespace ZuTestUtil;

ZuAssert((ZuIs_<ZfYAML::AnyNode, ZfTree::AnyNode>{}));

ZtEnumNS(, YAMLValues, int8_t, High, Low, Normal);
namespace YAMLFlags { ZtFlags(, Flags, uint8_t, Bit0, Bit1, Bit2); }
ZtEnumImplNS(YAMLValues);
ZtEnumImplNS(YAMLFlags);

namespace ZuFieldProp::YAML {
  using Unix9 = Unix<ZfYAML::Sec, 9>;
}

struct YAMLNested { int value = 0; };
ZfStruct(, (YAMLNested, YAML),
  (value, (Ctor<0>),		Int32));

ZuDerive(YAMLMapKey, (ZtString<ZtStringHeapID<"ZfTest.YAMLMapKey">>));

using YAMLIntMap =
  ZfMapTest<"ZfTest.YAML.IntMap", ZmLHashKV<YAMLMapKey, int>>;
using YAMLIntMapRef = ZmRef<YAMLIntMap>;
inline ZfYAML::AsMap<ZfFieldTC::Int32> ZfYAML_Fmt(YAMLIntMap *);

using YAMLObjMap =
  ZfMapTest<"ZfTest.YAML.ObjMap", ZmLHashKV<YAMLMapKey, YAMLNested>>;
using YAMLObjMapRef = ZmRef<YAMLObjMap>;
inline ZfYAML::AsMap<ZfFieldTC::UDT> ZfYAML_Fmt(YAMLObjMap *);

struct YAMLMapHolder { YAMLIntMapRef map; };
ZfStruct(, (YAMLMapHolder, YAML),
  (map, (Ctor<0>, Mutable),	UDT));

struct YAMLUnionA { int foo = 0; };
struct YAMLUnionB { int bar = 0; };
ZfStruct(, (YAMLUnionA, YAML), (foo, (Ctor<0>, Mutable), Int32));
ZfStruct(, (YAMLUnionB, YAML), (bar, (Ctor<0>, Mutable), Int32));
struct YAMLUnionArray : public ZtArray<int> {
  using ZtArray<int>::ZtArray;
  friend ZfYAML::AsArray<ZfFieldTC::Int32> ZfYAML_Fmt(YAMLUnionArray *);
};
struct YAMLUnionText {
  ZtString<> value;
  YAMLUnionText() = default;
  YAMLUnionText(ZuCSpan value_) : value{value_} { }
  template <typename S> friend S &operator <<(S &s, const YAMLUnionText &v) {
    s << v.value;
    return s;
  }
  friend ZfYAML::AsString ZfYAML_Fmt(YAMLUnionText *);
};
struct YAMLUnionHolder {
  ZfYAML::Union<YAMLUnionA, YAMLUnionB,
    YAMLIntMapRef, YAMLUnionArray, YAMLUnionText> value;
};
ZfStruct(, (YAMLUnionHolder, YAML),
  (value, (Ctor<0>, Mutable),		UDT));

struct YAMLPtrObj_ : public ZmObject {
  int value = 0;
  YAMLPtrObj_(int value_ = 0) : value{value_} { }
};
using YAMLPtrObj = ZfHeapTest<"ZfTest.YAML.PtrObj", YAMLPtrObj_>;
ZfStruct(, (YAMLPtrObj, YAML),
  (value, (Ctor<0>, Mutable),		Int32));

struct YAMLFmtOpt : public ZmObject { };
inline ZfYAML::AsString ZfYAML_Fmt(ZmRef<YAMLFmtOpt> *);
ZuAssert((!ZfYAML::IsObjPtr<ZmRef<YAMLFmtOpt>>{}));

struct YAMLPtrText_ : public ZmObject {
  ZtString<> value;
  YAMLPtrText_() = default;
  YAMLPtrText_(ZuCSpan value_) : value{value_} { }
  template <typename S>
  friend S &operator <<(S &s, const YAMLPtrText_ &v) {
    s << v.value;
    return s;
  }
};
using YAMLPtrText = ZfHeapTest<"ZfTest.YAML.PtrText", YAMLPtrText_>;

struct YAMLPtrArray_ : public ZmObject, public ZtArray<ZmRef<YAMLPtrObj>> {
  using Base = ZtArray<ZmRef<YAMLPtrObj>>;
  using Base::Base;
  using Base::operator =;
};
using YAMLPtrArray = ZfHeapTest<"ZfTest.YAML.PtrArray", YAMLPtrArray_>;
inline ZfYAML::AsArray<ZfFieldTC::UDT> ZfYAML_Fmt(YAMLPtrArray *);

using YAMLPtrMap = ZfMapTest<
  "ZfTest.YAML.PtrMap", ZmLHashKV<YAMLMapKey, ZmRef<YAMLPtrObj>>>;
inline ZfYAML::AsMap<ZfFieldTC::UDT> ZfYAML_Fmt(YAMLPtrMap *);

struct YAMLPtrHolder {
  ZmRef<YAMLPtrObj> object;
  ZmRef<YAMLPtrArray> objects;
  ZmRef<YAMLPtrText> text;
};
ZfStruct(, (YAMLPtrHolder, YAML),
  (object, (Mutable),		UDT),
  (objects, (Mutable),		UDT),
  (text, (Mutable),		UDT));

ZuAssert((ZuIsSame<
  ZmNHeapID_<YAMLIntMap>, ZuStringT<"ZfTest.YAML.IntMap">>{}));
ZuAssert((ZuIsSame<
  ZmNHeapID_<YAMLPtrObj>, ZuStringT<"ZfTest.YAML.PtrObj">>{}));
ZuAssert((ZuIsSame<
  ZmNHeapID_<YAMLPtrText>, ZuStringT<"ZfTest.YAML.PtrText">>{}));
ZuAssert((ZuIsSame<
  ZmNHeapID_<YAMLPtrArray>, ZuStringT<"ZfTest.YAML.PtrArray">>{}));

struct YAMLJSON {
  int value = 0;
  friend ZfYAML::AsJSON ZfYAML_Fmt(YAMLJSON *);
};
ZfStruct(, (YAMLJSON, YAML),
  (value, (Ctor<0>),		Int32));

struct YAMLText {
  ZtString<> value;

  YAMLText() = default;
  YAMLText(ZuCSpan value_) : value{value_} { }
  friend ZfYAML::AsString ZfYAML_Fmt(YAMLText *);
};

struct YAMLTextData { YAMLText text; };
ZfStruct(, (YAMLTextData, YAML),
  (text, (Ctor<0>),	UDT));

struct YAMLData {
  const char *cstr = nullptr;
  ZtString<> string;
  int number = 0;
  bool bool_ = false;
  ZtArray<uint8_t> bytes;
  ZtArray<ZtString<>> strings;
  YAMLNested nested;
};
ZfStruct(, (YAMLData, YAML),
  (cstr, (Ctor<0>),		CString),
  (string, (Ctor<1>),		String),
  (number, (Ctor<2>),		Int32),
  (bool_, (Ctor<3>),		Bool),
  (bytes, (Ctor<4>),		Bytes),
  (strings, (Ctor<5>),		StringVec),
  (nested, (Ctor<6>),		UDT));

struct YAMLNumbers {
  int i = 0;
  unsigned hex = 0;
  int enum_ = YAMLValues::Normal;
  uint128_t flags = 0;
  double float_ = 0;
  ZuFixed fixed;
  ZuDecimal decimal;
  ZuTime time;
  ZtArray<int> ints;
};
ZfStruct(, (YAMLNumbers, YAML),
  (i, (Ctor<0>, Mutable),				Int32),
  (hex, (Ctor<1>, Hex),					UInt32),
  (enum_, (Ctor<2>, Enum<YAMLValues::Map>),		Int32),
  (flags, (Ctor<3>, Flags<YAMLFlags::Map>),		UInt128),
  (float_, (Ctor<4>, (Range<-1000.0, 1000.0>)),		Float),
  (fixed, (Ctor<5>,
    (Range<ZuDecimal{-1000}, ZuDecimal{1000}>)),	Fixed),
  (decimal, (Ctor<6>,
    (Range<ZuDecimal{-1000}, ZuDecimal{1000}>)),	Decimal),
  (time, (Ctor<7>, YAML::Unix9),			Time),
  (ints, (Ctor<8>),					Int32Vec));

struct YAMLBytes {
  ZtArray<uint8_t> base64;
  ZtArray<uint8_t> base64URL;
  ZtArray<uint8_t> base32;
  ZtArray<uint8_t> hex;
  ZtArray<uint8_t> raw;
};
ZfStruct(, (YAMLBytes, YAML),
  (base64, (Ctor<0>, YAML::Base64),		Bytes),
  (base64URL, (Ctor<1>, YAML::Base64URL),	Bytes),
  (base32, (Ctor<2>, YAML::Base32),		Bytes),
  (hex, (Ctor<3>, YAML::Hex),			Bytes),
  (raw, (Ctor<4>, YAML::Raw),			Bytes));

struct YAMLRequired { int required; int optional; };
ZfStruct(, (YAMLRequired, YAML),
  (required, (Ctor<0>, Required),	Int32),
  (optional, (Ctor<1>),			Int32));

struct YAMLScalars {
  ZtString<> plain;
  ZtString<> single;
  ZtString<> double_;
  ZtString<> literal;
  ZtString<> folded;
  bool quotedBool = false;
  int quotedInt = 0;
};
ZfStruct(, (YAMLScalars, YAML),
  (plain, (Ctor<0>, YAML::Plain),			String),
  (single, (Ctor<1>, YAML::SingleQuoted),		String),
  (double_, (Ctor<2>, YAML::DoubleQuoted),		String),
  (literal, (Ctor<3>, YAML::BlockLiteral),		String),
  (folded, (Ctor<4>, YAML::BlockFolded),		String),
  (quotedBool, (Ctor<5>, YAML::SingleQuoted),		Bool),
  (quotedInt, (Ctor<6>, YAML::DoubleQuoted),		Int32));

struct YAMLFacet { int value = 0; };
ZfStruct(, YAMLFacet,
  (value, (Ctor<0>),		Int32));
ZfStructRender(, YAMLFacet, JSON,
  (value, JSON::ID<"json-value">));
ZfStructRender(, YAMLFacet, YAML,
  (value, YAML::ID<"yaml-value">, YAML::DoubleQuoted));

static const ZfYAML::AnyNode *field(
    const ZfYAML::AnyNode *node, ZuCSpan id)
{
  if (!node || !node->has<ZfYAML::AnyNode::Object>()) return nullptr;
  const auto &fields = node->data<ZfYAML::AnyNode::Object>();
  for (unsigned i = fields.length(); i; ) {
    const auto &entry = fields[--i];
    if (entry.p<0>() == id) return entry.p<1>();
  }
  return nullptr;
}

static ZuCSpan string(const ZfYAML::AnyNode *node)
{
  if (!node || !node->has<ZfYAML::AnyNode::String>()) return {};
  return node->data<ZfYAML::AnyNode::String>();
}

static bool equal(
    const ZfYAML::AnyNode *a, const ZfYAML::AnyNode *b)
{
  if (!a || !b || a->type != b->type || a->scalarType != b->scalarType)
    return false;
  if (a->has<ZfYAML::AnyNode::String>())
    return a->data<ZfYAML::AnyNode::String>() ==
      b->data<ZfYAML::AnyNode::String>();
  if (a->has<ZfYAML::AnyNode::Array>()) {
    const auto &aa = a->data<ZfYAML::AnyNode::Array>();
    const auto &ba = b->data<ZfYAML::AnyNode::Array>();
    if (aa.length() != ba.length()) return false;
    for (unsigned i = 0, n = aa.length(); i < n; ++i)
      if (!equal(aa[i], ba[i])) return false;
    return true;
  }
  const auto &ao = a->data<ZfYAML::AnyNode::Object>();
  const auto &bo = b->data<ZfYAML::AnyNode::Object>();
  if (ao.length() != bo.length()) return false;
  for (unsigned i = 0, n = ao.length(); i < n; ++i)
    if (ao[i].p<0>() != bo[i].p<0>() ||
	!equal(ao[i].p<1>(), bo[i].p<1>())) return false;
  return true;
}

static ZeString error(ZuCSpan source, ZfYAML::Limits limits = {})
{
  try {
    ZfYAML::scan(source, limits);
  } catch (const ZeException &e) {
    ZeString message;
    message << e;
    return message;
  }
  return {};
}

static bool loadError(auto &&fn)
{
  try {
    fn();
  } catch (const ZeException &) {
    return true;
  }
  return false;
}

static void block()
{
  ZuTestScope(block);
  auto result = ZfYAML::scan(
    "name: demo\n"
    "enabled: yes\n"
    "count: 12\n"
    "items:\n"
    "  - one\n"
    "  - two\n"
    "nested: {key: \"value\", nil: null}\n");
  ZuCheck(result.p<0>() > 0);
  auto &root = result.p<1>();
  ZuCheck(root->has<ZfYAML::AnyNode::Object>());
  ZuCheck(string(field(root, "name")) == "demo");
  ZuCheck(field(root, "name")->scalarType == ZfYAML::ScalarTC::String);
  ZuCheck(field(root, "enabled")->scalarType == ZfYAML::ScalarTC::True);
  ZuCheck(field(root, "count")->scalarType == ZfYAML::ScalarTC::Number);
  auto items = field(root, "items");
  ZuCheck(items->has<ZfYAML::AnyNode::Array>());
  ZuCheck(items->data<ZfYAML::AnyNode::Array>().length() == 2);
  ZuCheck(string(items->data<ZfYAML::AnyNode::Array>()[1]) == "two");
  ZuCheck(string(field(field(root, "nested"), "key")) == "value");
  ZuCheck(field(field(root, "nested"), "nil")->scalarType == ZfYAML::ScalarTC::Null);
}

static void flow()
{
  ZuTestScope(flow);
  auto result = ZfYAML::scan(
    "{a: [1, 2, 3,], quoted: 'yes', escaped: \"a\\nb\", empty:}");
  auto &root = result.p<1>();
  ZuCheck(field(root, "a")->data<ZfYAML::AnyNode::Array>()[2].ptr()->scalarType == ZfYAML::ScalarTC::Number);
  ZuCheck(string(field(root, "quoted")) == "yes");
  ZuCheck(field(root, "quoted")->scalarType == ZfYAML::ScalarTC::String);
  ZuCheck(string(field(root, "escaped")) == "a\nb");
  ZuCheck(field(root, "empty")->scalarType == ZfYAML::ScalarTC::Null);
}

static void documents()
{
  ZuTestScope(documents);
  auto blank = ZfYAML::scan(" # comment\n");
  ZuCheck(blank.p<1>()->has<ZfYAML::AnyNode::Object>());
  auto marker = ZfYAML::scan("---\n");
  ZuCheck(marker.p<1>()->scalarType == ZfYAML::ScalarTC::Null);
  ZuCheck(error("---\na: 1\n---\nb: 2\n").find("second document") >= 0);
  ZuCheck(error("%YAML 1.2\n---\na: 1\n").find("directive") >= 0);
  auto repeated = ZfYAML::scan("a: 1\na: 2\n").p<1>();
  ZuCheck(string(field(repeated, "a")) == "2");
  auto typed = ZfYAML::scan("value: 1\nvalue: 2\n").p<1>();
  ZuCheck(loadError([&] {
    ZfYAML::handler<YAMLNested>(typed).ctor();
  }));
  ZuCheck(loadError([] {
    auto tree = ZfYAML::scan("{value: 1, 'value': 2}");
    ZfYAML::handler<YAMLNested>(tree.p<1>()).ctor();
  }));
  auto unknown = ZfYAML::scan("other: 1\nother: 2\nvalue: 3\n");
  ZuCheck(ZfYAML::handler<YAMLNested>(unknown.p<1>()).ctor().value == 3);
  ZtString<> large;
  for (unsigned i = 0; i < 40; ++i)
    large << "key" << i << ": " << i << '\n';
  large << "key0: 41\n";
  ZuCheck(string(field(ZfYAML::scan(large).p<1>(), "key0")) == "41");
  large.null();
  large << '{';
  for (unsigned i = 0; i < 40; ++i)
    large << "key" << i << ": " << i << ", ";
  large << "key0: 41}";
  ZuCheck(string(field(ZfYAML::scan(large).p<1>(), "key0")) == "41");
  large.null();
  large << "root:\n";
  for (unsigned i = 0; i < 40; ++i)
    large << "  key" << i << ": " << i << '\n';
  large << "  child:\n";
  for (unsigned i = 0; i < 40; ++i)
    large << "    key" << i << ": " << i << '\n';
  ZuCheck(ZfYAML::scan(large).p<1>());
  large << "  key0: 41\n";
  ZuCheck(string(field(field(ZfYAML::scan(large).p<1>(), "root"), "key0")) == "41");
  ZuCheck(error("? [a, b]\n: value\n").find("complex key") >= 0);
  ZuCheck(error("<<: *base\n").find("merge key") >= 0);
  const char bom[] = "\xef\xbb\xbf---\r\na: one\rcomment: two\n...\r\n";
  auto mixed = ZfYAML::scan({bom, unsigned(sizeof(bom) - 1)});
  ZuCheck(string(field(mixed.p<1>(), "a")) == "one");
  ZuCheck(string(field(mixed.p<1>(), "comment")) == "two");
  ZuCheck(error("a: [1,, 2]\n").find(
    "line 1, column 7 (offset 6)") >= 0);
}

static void aliases()
{
  ZuTestScope(aliases);
  auto result = ZfYAML::scan(
    "base: &base {name: one, values: [1, 2]}\n"
    "copy: *base\n");
  auto &root = result.p<1>();
  auto base = field(root, "base");
  auto copy = field(root, "copy");
  ZuCheck(base != copy);
  ZuCheck(string(field(copy, "name")) == "one");
  ZuCheck(copy->parent == root.ptr());
  ZuCheck(field(copy, "values")->data<ZfYAML::AnyNode::Array>()[0].ptr()->parent == field(copy, "values"));
  ZuCheck(error("value: *missing\n").find("unknown alias") >= 0);
  ZuCheck(error("value: &self [*self]\n").find("cyclic alias") >= 0);
  ZuCheck(error("base: &x one\ncopy: *x\n",
    ZfYAML::Limits{0, 32, 100}).find("alias limit") >= 0);
  auto kinds = ZfYAML::scan(
    "scalar: &x yes\n"
    "scalarCopy: *x\n"
    "sequence: &seq\n"
    "  - one\n"
    "sequenceCopy: *seq\n"
    "mapping: &map\n"
    "  child: value\n"
    "mappingCopy: *map\n"
    "replace: &x two\n"
    "latest: *x\n").p<1>();
  ZuCheck(field(kinds, "scalarCopy")->scalarType == ZfYAML::ScalarTC::True);
  ZuCheck(string(field(kinds, "sequenceCopy")->data<ZfYAML::AnyNode::Array>()[0].ptr()) == "one");
  ZuCheck(string(field(field(kinds, "mappingCopy"), "child")) == "value");
  ZuCheck(string(field(kinds, "latest")) == "two");
  ZuCheck(field(kinds, "sequence") != field(kinds, "sequenceCopy"));
  ZuCheck(field(kinds, "sequence")->data<ZfYAML::AnyNode::Array>()[0].ptr() !=
    field(kinds, "sequenceCopy")->data<ZfYAML::AnyNode::Array>()[0].ptr());
  ZuCheck(error(
    "a: &a value\nb: &b [*a]\nc: *b\n",
    ZfYAML::Limits{100, 1, 100}).find("alias depth limit") >= 0);
  ZuCheck(error("[one]\n", ZfYAML::Limits{100, 32, 1}).find(
    "node limit") >= 0);
}

static void transforms()
{
  ZuTestScope(transforms);
  auto flat = ZfYAML::flatten(ZuMv(ZfYAML::scan(
    "type: object\n"
    "required: [a]\n"
    "allOf:\n"
    "  - {required: [a, b], properties: {a: {type: string}}}\n"
    "  - {required: [b, c], properties: {b: {type: number}}}\n").p<1>()));
  ZuCheck(!field(flat, "allOf"));
  auto required = field(flat, "required");
  ZuCheck(required->data<ZfYAML::AnyNode::Array>().length() == 3);
  ZuCheck(string(required->data<ZfYAML::AnyNode::Array>()[2]) == "c");
  ZuCheck(string(field(field(field(flat, "properties"), "a"), "type")) == "string");
  ZuCheck(string(field(field(field(flat, "properties"), "b"), "type")) == "number");

  auto nullRoot = ZfYAML::flatten(ZuMv(ZfYAML::scan("null\n").p<1>()));
  ZuCheck(nullRoot->has<ZfYAML::AnyNode::Object>());
  ZuCheck(error("x: %GG\n"));
  try {
    auto bad = ZfYAML::flatten(ZuMv(ZfYAML::scan(
      "oneOf: [{type: string}]\n").p<1>()), true, false);
    ZuCheck(false);
  } catch (const ZeException &e) {
    ZeString message;
    message << e;
    ZuCheck(message.find("cannot flatten oneOf alternative") >= 0);
  }
}

static void collections()
{
  ZuTestScope(collections);
  auto result = ZfYAML::scan(
    "root:\n"
    "  - key: value\n"
    "    other:\n"
    "      - one\n"
    "      - two\n"
    "  - {key: flow}\n"
    "indentless:\n"
    "- a\n"
    "- b\n"
    "emptyArray: []\n"
    "emptyObject: {}\n"
    "omitted:\n");
  auto &root = result.p<1>();
  ZuCheck(string(field(field(root, "root")->data<ZfYAML::AnyNode::Array>()[0].ptr(), "key")) == "value");
  ZuCheck(string(field(field(root, "root")->data<ZfYAML::AnyNode::Array>()[0].ptr(), "other")->data<ZfYAML::AnyNode::Array>()[1].ptr()) == "two");
  ZuCheck(string(field(field(root, "root")->data<ZfYAML::AnyNode::Array>()[1].ptr(), "key")) == "flow");
  ZuCheck(string(field(root, "indentless")->data<ZfYAML::AnyNode::Array>()[1].ptr()) == "b");
  ZuCheck(field(root, "emptyArray")->has<ZfYAML::AnyNode::Array>());
  ZuCheck(field(root, "emptyObject")->has<ZfYAML::AnyNode::Object>());
  ZuCheck(field(root, "omitted")->scalarType == ZfYAML::ScalarTC::Null);
}

static void scalars()
{
  ZuTestScope(scalars);
  {
    ZuTestRepeat(booleansTrue, 11);
    for (auto value: {
      ZuCSpan{"y"}, ZuCSpan{"Y"}, ZuCSpan{"yes"}, ZuCSpan{"Yes"},
      ZuCSpan{"YES"}, ZuCSpan{"true"}, ZuCSpan{"True"}, ZuCSpan{"TRUE"},
      ZuCSpan{"on"}, ZuCSpan{"On"}, ZuCSpan{"ON"}}) {
      auto root = ZfYAML::scan(value).p<1>();
      ZuCheck(root->scalarType == ZfYAML::ScalarTC::True);
    }
  }
  {
    ZuTestRepeat(booleansFalse, 11);
    for (auto value: {
      ZuCSpan{"n"}, ZuCSpan{"N"}, ZuCSpan{"no"}, ZuCSpan{"No"},
      ZuCSpan{"NO"}, ZuCSpan{"false"}, ZuCSpan{"False"}, ZuCSpan{"FALSE"},
      ZuCSpan{"off"}, ZuCSpan{"Off"}, ZuCSpan{"OFF"}}) {
      auto root = ZfYAML::scan(value).p<1>();
      ZuCheck(root->scalarType == ZfYAML::ScalarTC::False);
    }
  }
  {
    ZuTestRepeat(numbers, 17);
    for (auto value: {
      ZuCSpan{"0"}, ZuCSpan{"-0"}, ZuCSpan{"12"}, ZuCSpan{"-12.5"},
      ZuCSpan{"1e3"}, ZuCSpan{"1E-3"}, ZuCSpan{"0o17"},
      ZuCSpan{"-0o17"}, ZuCSpan{"0x2a"}, ZuCSpan{"+0x2A"},
      ZuCSpan{"+1"}, ZuCSpan{"01"}, ZuCSpan{"0b1010_0111"},
      ZuCSpan{"02472256"}, ZuCSpan{"190:20:30"}, ZuCSpan{"+685_230"},
      ZuCSpan{"0x_0A_74_AE"}}) {
      auto root = ZfYAML::scan(value).p<1>();
      ZuCheck(root->scalarType == ZfYAML::ScalarTC::Number);
    }
  }
  {
    ZuTestRepeat(strings, 11);
    for (auto value: {
      ZuCSpan{"~"}, ZuCSpan{"Null"}, ZuCSpan{"NULL"}, ZuCSpan{"08"},
      ZuCSpan{"0o8"}, ZuCSpan{"0x"}, ZuCSpan{"0b2"}, ZuCSpan{"1:60"},
      ZuCSpan{".inf"}, ZuCSpan{".nan"}, ZuCSpan{"2024-01-01"}}) {
      auto root = ZfYAML::scan(value).p<1>();
      ZuCheck(root->scalarType == ZfYAML::ScalarTC::String);
    }
  }
  auto root = ZfYAML::scan(
    "single: 'it''s yes'\n"
    "double: \"A\\n\\u20ac\\U0001f600\"\n"
    "control: \"\\0\\a\\b\\t\\v\\f\\r\\e\"\n"
    "url: http://example/#part\n"
    "colon: a:b\n"
    "comment: value # ignored\n").p<1>();
  ZuCheck(string(field(root, "single")) == "it's yes");
  ZuCheck(field(root, "single")->scalarType == ZfYAML::ScalarTC::String);
  ZuCheck(string(field(root, "double")) == "A\n\xe2\x82\xac\xf0\x9f\x98\x80");
  ZuCheck(string(field(root, "control")).length() == 8);
  ZuCheck(string(field(root, "url")) == "http://example/#part");
  ZuCheck(string(field(root, "colon")) == "a:b");
  ZuCheck(string(field(root, "comment")) == "value");
  auto multiline = ZfYAML::scan(
    "plain: first\n"
    "  second\n"
    "\n"
    "  third\n"
    "quoted: \"first\n"
    "  second\n"
    "\n"
    "  third\"\n").p<1>();
  ZuCheck(string(field(multiline, "plain")) == "first second\nthird");
  ZuCheck(string(field(multiline, "quoted")) == "first second\nthird");
}

static void invalid()
{
  ZuTestScope(invalid);
  {
    ZuTestRepeat(syntax, 11);
    for (auto input: {
        ZuCSpan{"\ta: 1\n"}, ZuCSpan{"a: [1,, 2]\n"},
        ZuCSpan{"a: {b 1}\n"}, ZuCSpan{"a: [1, 2\n"},
        ZuCSpan{"a: 'unterminated\n"}, ZuCSpan{"a: \"unterminated\n"},
        ZuCSpan{"a: \"\\q\"\n"}, ZuCSpan{"a: \"\\uD800\"\n"},
        ZuCSpan{"a: !tag value\n"},
        ZuCSpan{"a: &x &y value\n"}, ZuCSpan{"a: *forward\n"}}) {
      ZuCheck(error(input));
    }
  }
  ZuCheck(error("a: !tag value\n").find("tag not supported") >= 0);
  ZuCheck(error("a: &x &y value\n").find("invalid anchor") >= 0);
  ZuCheck(error("a: 1\n  b: 2\n"));
  ZuCheck(!error("'<<': value\n\"also\": ok\n"));
  ZuCheck(!error("{'<<': value, \"also\": ok}"));
  ZuCheck(error("---\na: 1\n...\n---\nb: 2\n").find("second document") >= 0);
  ZuCheck(ZfYAML::scan("---\n...\n").p<1>()->scalarType ==
    ZfYAML::ScalarTC::Null);

  const char bom[] = "a: \xef\xbb\xbfvalue\n";
  ZuCheck(error({bom, unsigned(sizeof(bom) - 1)}).find("invalid Unicode") >= 0);
  const char overlong[] = "a: \xe0\x80\x80\n";
  ZuCheck(error({overlong, unsigned(sizeof(overlong) - 1)}).
    find("invalid Unicode") >= 0);
}

static void scalarFormats()
{
  ZuTestScope(scalarFormats);
  YAMLScalars scalars{
    "hello world", "it's quoted", "a\nb", "one\ntwo", "one\ntwo", true, 42};
  ZtString<> saved;
  ZfYAML::save(saved, scalars);
  ZuCheck(saved ==
    "plain: hello world\nsingle: 'it''s quoted'\n"
    "double_: \"a\\nb\"\nliteral: |-\n  one\n  two\n"
    "folded: >-\n  one\n  two\nquotedBool: 'true'\nquotedInt: \"42\"");
  auto tree = ZfYAML::scan(saved).p<1>();
  auto round = ZfYAML::handler<YAMLScalars>(tree).ctor();
  ZuCheck(round.plain == scalars.plain);
  ZuCheck(round.single == scalars.single);
  ZuCheck(round.double_ == scalars.double_);
  ZuCheck(round.literal == scalars.literal);
  ZuCheck(round.folded == "one two");
  ZuCheck(round.quotedBool && round.quotedInt == 42);

  ZtString<> facetYAML, facetJSON;
  ZfYAML::save(facetYAML, YAMLFacet{7});
  ZfJSON::save(facetJSON, YAMLFacet{7});
  ZuCheck(facetYAML == "yaml-value: \"7\"" &&
    facetJSON == "{\"json-value\":7}");
}

static void handlers()
{
  ZuTestScope(handlers);
  ZtString<> source =
    "cstr: hello\n"
    "string: 'world'\n"
    "number: 42\n"
    "bool_: yes\n"
    "bytes: eHh4\n"
    "strings: [one, 'two', \"three\"]\n"
    "nested: {value: 7}\n";
  ZtString<> original = source;
  auto scan = ZfYAML::scan(source);
  auto value = ZfYAML::handler<YAMLData>(scan.p<1>()).ctor();
  ZuCheck(ZuCSpan{value.cstr} == "hello");
  ZuCheck(value.string == "world");
  ZuCheck(value.number == 42);
  ZuCheck(value.bool_);
  ZuCheck(ZuBSpan{value.bytes} == ZuBSpan{"xxx"});
  ZuCheck(value.strings.length() == 3 && value.strings[2] == "three");
  ZuCheck(value.nested.value == 7);
  ZuCheck(source == original);
  auto again = ZfYAML::handler<YAMLData>(scan.p<1>()).ctor();
  ZuCheck(ZuBSpan{again.bytes} == ZuBSpan{"xxx"});

  auto numberTree = ZfYAML::scan(
    "i: -42\n"
    "hex: deadbeef\n"
    "enum_: Low\n"
    "flags: Bit0|Bit2\n"
    "float_: 1.25e2\n"
    "fixed: 12.5\n"
    "decimal: -0.125\n"
    "time: 1700000000.25\n"
    "ints: [1, -2, 3]\n").p<1>();
  auto numbers = ZfYAML::handler<YAMLNumbers>(numberTree).ctor();
  ZuCheck(numbers.i == -42 && numbers.hex == 0xdeadbeef);
  ZuCheck(numbers.enum_ == YAMLValues::Low);
  ZuCheck(numbers.flags == (YAMLFlags::Bit0() | YAMLFlags::Bit2()));
  ZuCheck(numbers.float_ == 125.0);
  ZuCheck(numbers.fixed == ZuFixed{ZuDecimal{"12.5"}});
  ZuCheck(numbers.decimal == ZuDecimal{"-0.125"});
  ZuCheck(numbers.time.sec() == 1700000000 &&
    numbers.time.nsec() == 250000000);
  ZuCheck(numbers.ints.length() == 3 && numbers.ints[1] == -2);
  ZtString<> numbersSaved;
  ZfYAML::save(numbersSaved, numbers);
  ZuCheck(numbersSaved ==
    "i: -42\nhex: \"deadbeef\"\nenum_: \"Low\"\n"
    "flags: \"Bit0|Bit2\"\nfloat_: 125\nfixed: 12.5\n"
    "decimal: -0.125\ntime: \"1700000000.25\"\n"
    "ints:\n  - 1\n  - -2\n  - 3");
  ZtString<> jsonSaved;
  ZfYAML::save(jsonSaved, YAMLJSON{7});
  ZuCheck(jsonSaved == "{\"value\":7}");
  ZtString<> keySaved;
  ZfYAML::saveKey(keySaved, "ordinary-key");
  keySaved << ' ';
  ZfYAML::saveKey(keySaved, "yes");
  ZuCheck(keySaved == "ordinary-key \"yes\"");
  YAMLNumbers updated;
  ZfYAML::handler<YAMLNumbers>(numberTree).load(updated);
  auto update = ZfYAML::scan("i: 7\n");
  ZfYAML::handler<YAMLNumbers>(update.p<1>()).update(updated);
  ZuCheck(updated.i == 7 && updated.hex == 0xdeadbeef);

  auto prefixed = ZfYAML::handler<YAMLNumbers>(ZfYAML::scan(
    "i: -0x2a\nhex: 0o33653337357\nenum_: Low\nflags: Bit0\n"
    "float_: 1\nfixed: 1\ndecimal: 1\ntime: 1\n"
    "ints: [+0x1, -0o2, 0b11, 010, 1:20]\n").
    p<1>()).ctor();
  ZuCheck(prefixed.i == -42 && prefixed.hex == 0xdeadbeef);
  ZuCheck(prefixed.ints.length() == 5 && prefixed.ints[0] == 1 &&
    prefixed.ints[1] == -2 && prefixed.ints[2] == 3 &&
    prefixed.ints[3] == 8 && prefixed.ints[4] == 80);

  auto bytesTree = ZfYAML::scan(
    "base64: eHh4\nbase64URL: _w\nbase32: PB4HQ===\n"
    "hex: '787878'\nraw: 'raw bytes'\n").p<1>();
  auto bytes = ZfYAML::handler<YAMLBytes>(bytesTree).ctor();
  ZuCheck(ZuBSpan{bytes.base64} == ZuBSpan{"xxx"});
  ZuCheck(bytes.base64URL.length() == 1 && bytes.base64URL[0] == 0xff);
  ZuCheck(ZuBSpan{bytes.base32} == ZuBSpan{"xxx"});
  ZuCheck(ZuBSpan{bytes.hex} == ZuBSpan{"xxx"});
  ZuCheck(ZuBSpan{bytes.raw} == ZuBSpan{"raw bytes"});

  ZuCheck(loadError([] {
    auto tree = ZfYAML::scan("optional: 1\n");
    ZfYAML::handler<YAMLRequired>(tree.p<1>()).ctor();
  }));
  {
    ZuTestRepeat(wrongKinds, 6);
    for (auto input: {
        ZuCSpan{"number: true\n"}, ZuCSpan{"bool_: \"true\"\n"},
        ZuCSpan{"string: 42\n"}, ZuCSpan{"nested: null\n"},
        ZuCSpan{"strings: nope\n"}, ZuCSpan{"bytes: null\n"}}) {
      ZuCheck(loadError([input] {
	auto tree = ZfYAML::scan(input);
	ZfYAML::handler<YAMLData>(tree.p<1>()).ctor();
      }));
    }
  }
  {
    ZuTestRepeat(badNumbers, 8);
    for (auto input: {
        ZuCSpan{"i: 1x\n"}, ZuCSpan{"enum_: unknown\n"},
        ZuCSpan{"flags: Bit0|unknown\n"}, ZuCSpan{"float_: 1001\n"},
        ZuCSpan{"fixed: 1.0x\n"}, ZuCSpan{"decimal: 1e19\n"},
        ZuCSpan{"time: 1.0x\n"}, ZuCSpan{"ints: nope\n"}}) {
      ZuCheck(loadError([input] {
	auto tree = ZfYAML::scan(input);
	ZfYAML::handler<YAMLNumbers>(tree.p<1>()).ctor();
      }));
    }
  }
  ZuCheck(loadError([] {
    auto tree = ZfYAML::scan("base64: 'eA==junk'\n");
    ZfYAML::handler<YAMLBytes>(tree.p<1>()).ctor();
  }));

  ZtString<> saved;
  ZfYAML::save(saved, value);
  auto roundTree = ZfYAML::scan(saved);
  auto round = ZfYAML::handler<YAMLData>(roundTree.p<1>()).ctor();
  ZuCheck(round.string == value.string && round.number == value.number &&
    round.nested.value == value.nested.value);
}

static void transformEdges()
{
  ZuTestScope(transformEdges);
  auto flat = ZfYAML::flatten(ZuMv(ZfYAML::scan(
    "value: old\n"
    "array: [1, 1.0, yes]\n"
    "allOf:\n"
    "  - {value: {nested: true}, array: [1e0, no, yes]}\n"
    "oneOf: [{type: string, anyOf: [{x: 1}]}]\n"
    "anyOf: []\n").p<1>()));
  ZuCheck(field(field(flat, "value"), "nested")->scalarType == ZfYAML::ScalarTC::True);
  auto array = field(flat, "array");
  ZuCheck(array->data<ZfYAML::AnyNode::Array>().length() == 4);
  ZuCheck(array->data<ZfYAML::AnyNode::Array>()[3]->scalarType ==
    ZfYAML::ScalarTC::False);
  ZuCheck(field(flat, "oneOf"));
  ZuCheck(field(flat, "anyOf"));
  ZuCheck(field(field(flat, "oneOf")->data<ZfYAML::AnyNode::Array>()[0].ptr(), "anyOf"));

  auto merged = ZfYAML::flatten(ZuMv(ZfYAML::scan(
    "a: old\nb: old\nc: old\nd: old\ne: old\nf: old\ng: old\nh: old\n"
    "nested: {a: old, b: old, c: old, d: old, e: old, f: old, "
      "g: old, h: old}\n"
    "allOf: [{a: new, b: new, c: new, d: new, e: new, f: new, "
      "g: new, h: new, nested: {a: new, b: new, c: new, d: new, "
      "e: new, f: new, g: new, h: new}, extra: added}]\n").p<1>()));
  ZuCheck(string(field(merged, "a")) == "new");
  ZuCheck(string(field(merged, "h")) == "new");
  ZuCheck(string(field(field(merged, "nested"), "a")) == "new");
  ZuCheck(string(field(field(merged, "nested"), "h")) == "new");
  ZuCheck(string(field(merged, "extra")) == "added");
  ZuCheck(!field(merged, "allOf"));

  ZtString<> indexedMergeText;
  for (unsigned i = 0; i < 64; ++i)
    indexedMergeText << "key" << i << ": old\n";
  indexedMergeText << "nested: {";
  for (unsigned i = 0; i < 64; ++i) {
    if (i) indexedMergeText << ", ";
    indexedMergeText << "key" << i << ": old";
  }
  indexedMergeText << "}\nallOf: [{";
  for (unsigned i = 0; i < 64; ++i) {
    if (i) indexedMergeText << ", ";
    indexedMergeText << "key" << i << ": new";
  }
  indexedMergeText << ", nested: {";
  for (unsigned i = 0; i < 64; ++i) {
    if (i) indexedMergeText << ", ";
    indexedMergeText << "key" << i << ": new";
  }
  indexedMergeText << "}, extra: added}]\nkey63: final\n";
  auto indexedMerge = ZfYAML::flatten(
    ZuMv(ZfYAML::scan(indexedMergeText).p<1>()));
  ZuCheck(string(field(indexedMerge, "key63")) == "final");
  ZuCheck(string(field(field(indexedMerge, "nested"), "key63")) == "new");
  ZuCheck(string(field(indexedMerge, "extra")) == "added");

  ZtString<> scalarMergeText{"values: ["};
  for (unsigned i = 0; i < 32; ++i) {
    if (i) scalarMergeText << ", ";
    scalarMergeText << "value" << i;
  }
  scalarMergeText << ", 1]\nallOf: [{values: [";
  for (unsigned i = 16; i < 48; ++i) {
    if (i != 16) scalarMergeText << ", ";
    scalarMergeText << "value" << i;
  }
  scalarMergeText << ", 1.0]}]\n";
  auto scalarMerge = ZfYAML::flatten(
    ZuMv(ZfYAML::scan(scalarMergeText).p<1>()));
  auto mergedValues = field(scalarMerge, "values");
  ZuCheck(mergedValues->data<ZfYAML::AnyNode::Array>().length() == 49);
  ZuCheck(string(field(scalarMerge, "values")->data<ZfYAML::AnyNode::Array>()[32].ptr()) == "1");
  ZuCheck(string(field(scalarMerge, "values")->data<ZfYAML::AnyNode::Array>()[48].ptr()) == "value47");

  auto empty = ZfYAML::flatten(ZuMv(ZfYAML::scan("oneOf: []\n").p<1>()),
    true, false);
  ZuCheck(!field(empty, "oneOf"));
  auto arrayFlags = ZfYAML::flatten(
    ZuMv(ZfYAML::scan("[{oneOf: [{x: 1}]}]\n").p<1>()), true, true);
  ZuCheck(field(arrayFlags->data<ZfYAML::AnyNode::Array>()[0].ptr(), "oneOf"));
  ZuCheck(loadError([] {
    auto bad = ZfYAML::scan("object: {}\nallOf: [{object: []}]\n");
    ZfYAML::flatten(ZuMv(bad.p<1>()));
  }));
}

static void transformGolden()
{
  ZuTestScope(transformGolden);
  auto loaded = ZfYAML::scan(
    "components:\n"
    "  schemas:\n"
    "    Base:\n"
    "      type: object\n"
    "      required: [id]\n"
    "      properties: {id: {type: string}}\n"
    "    Named:\n"
    "      allOf:\n"
    "        - {type: object, required: [id], properties: {id: {type: string}}}\n"
    "        - {required: [name], properties: {name: {type: string}}}\n"
    "request:\n"
    "  allOf:\n"
    "    - {description: local, type: object, required: [id, name],\n"
    "       properties: {id: {type: string}, name: {type: string}}}\n"
    "    - {required: [id, extra], properties: {extra: {type: number}}}\n"
    "  oneOf: [{type: string}, {type: number}]\n").p<1>();
  ZuCheck(loaded);
  auto actual = ZfYAML::flatten(ZuMv(loaded));
  ZuCheck(actual);
  auto expected = ZfYAML::scan(
    "components:\n"
    "  schemas:\n"
    "    Base: {type: object, required: [id], "
      "properties: {id: {type: string}}}\n"
    "    Named: {type: object, required: [id, name], "
      "properties: {id: {type: string}, name: {type: string}}}\n"
    "request:\n"
    "  oneOf: [{type: string}, {type: number}]\n"
    "  description: local\n"
    "  type: object\n"
    "  required: [id, name, extra]\n"
    "  properties: {id: {type: string}, name: {type: string}, "
      "extra: {type: number}}\n").p<1>();
  ZuCheck(equal(actual, expected));
}

static void blockScalars()
{
  ZuTestScope(blockScalars);
  auto root = ZfYAML::scan(
    "literal: |\n"
    "  one\n"
    "  two\n"
    "strip: |-\n"
    "  one\n"
    "  two\n"
    "fold: >\n"
    "  one\n"
    "  two\n"
    "paragraph: >-\n"
    "  one\n"
    "\n"
    "  two\n"
    "keep: |+\n"
    "  one\n"
    "\n"
    "both: |+2\n"
    "  x\n"
    "explicit: |2-\n"
    "  x\n").p<1>();
  ZuCheck(string(field(root, "literal")) == "one\ntwo\n");
  ZuCheck(string(field(root, "strip")) == "one\ntwo");
  ZuCheck(string(field(root, "fold")) == "one two\n");
  ZuCheck(string(field(root, "paragraph")) == "one\ntwo");
  ZuCheck(string(field(root, "keep")) == "one\n\n");
  ZuCheck(string(field(root, "both")) == "x\n");
  ZuCheck(string(field(root, "explicit")) == "x");
  ZuCheck(field(root, "fold")->scalarType == ZfYAML::ScalarTC::String);
}

static void maps()
{
  ZuTestScope(maps);

  auto scan = ZfYAML::scan(
    "a: 1\n"
    "b: 2\n"
    "\"a.b\": 3\n"
    "\"true\": 4\n");
  auto map = ZmRef(ZfYAML::handler<YAMLIntMap>(scan.p<1>()).alloc());
  ZuCheck(map);
  ZuCheck(map->count_() == 4);
  ZuCheck(map->findVal("a") == 1);
  ZuCheck(map->findVal("a.b") == 3);
  ZuCheck(map->findVal("true") == 4);

  ZtString<> saved;
  ZfYAML::save(saved, map);
  ZtString<> direct;
  ZfYAML::save(direct, *map);
  ZuCheck(direct == saved);
  direct.null();
  ZfYAML::save(direct, map.ptr());
  ZuCheck(direct == saved);
  auto roundScan = ZfYAML::scan(saved);
  auto round = ZmRef(ZfYAML::handler<YAMLIntMap>(roundScan.p<1>()).alloc());
  ZuCheck(round->count_() == 4);
  ZuCheck(round->findVal("a.b") == 3);
  ZuCheck(round->findVal("true") == 4);

  auto ptr = map.ptr();
  auto loadScan = ZfYAML::scan("loaded: 5\n");
  ZfYAML::handler<YAMLIntMap>(loadScan.p<1>()).load(*map);
  ZuCheck(map.ptr() == ptr);
  ZuCheck(map->count_() == 1);
  ZuCheck(map->findVal("loaded") == 5);

  map->add("kept", 6);
  auto updateScan = ZfYAML::scan("loaded: 7\nadded: 8\n");
  ZfYAML::handler<YAMLIntMap>(updateScan.p<1>()).update(*map);
  ZuCheck(map.ptr() == ptr);
  ZuCheck(map->count_() == 3);
  ZuCheck(map->findVal("loaded") == 7);
  ZuCheck(map->findVal("kept") == 6);
  ZuCheck(map->findVal("added") == 8);

  auto storage = ZmAlloc(YAMLIntMap, 1);
  ZfYAML::handler<YAMLIntMap>(loadScan.p<1>()).new_(storage.data);
  auto &placed = storage[0];
  ZuCheck(placed.findVal("loaded") == 5);
  placed.~YAMLIntMap();

  auto objectScan = ZfYAML::scan("one:\n  value: 9\n");
  auto objects = ZmRef(ZfYAML::handler<YAMLObjMap>(objectScan.p<1>()).alloc());
  ZuCheck(objects->findVal("one").value == 9);
  saved.null();
  ZfYAML::save(saved, objects);
  auto objectsRound = ZmRef(ZfYAML::handler<YAMLObjMap>(
    ZfYAML::scan(saved).p<1>()).alloc());
  ZuCheck(objectsRound->findVal("one").value == 9);

  auto holderScan = ZfYAML::scan("map:\n  nested: 10\n");
  auto holder = ZfYAML::handler<YAMLMapHolder>(holderScan.p<1>()).ctor();
  ZuCheck(holder.map->findVal("nested") == 10);

  YAMLIntMapRef empty = new YAMLIntMap{};
  saved.null();
  ZfYAML::save(saved, empty);
  ZuCheck(saved == "{}");
  YAMLIntMapRef null;
  saved.null();
  ZfYAML::save(saved, null);
  ZuCheck(saved == "null");
  saved.null();
  ZfYAML::saveUpd(saved, null);
  ZuCheck(saved == "null");
  saved.null();
  ZfYAML::saveDel(saved, null);
  ZuCheck(saved == "null");
}

static void unions()
{
  ZuTestScope(unions);

  YAMLUnionHolder value;
  value.value = YAMLUnionA{42};
  ZtString<> saved;
  ZfYAML::save(saved, value);
  auto tree = ZfYAML::scan(saved);
  auto loaded = ZfYAML::handler<YAMLUnionHolder>(tree.p<1>()).ctor();
  auto node = loaded.value.p<const ZfYAML::AnyNode *>();
  loaded.value = ZfYAML::handler<YAMLUnionA>(node).ctor();
  ZtString<> round;
  ZfYAML::save(round, loaded);
  ZuCheck(round == saved);

  auto arrayTree = ZfYAML::scan("value: [1, 2, 3]\n");
  auto array = ZfYAML::handler<YAMLUnionHolder>(arrayTree.p<1>()).ctor();
  auto arrayNode = array.value.p<const ZfYAML::AnyNode *>();
  ZuCheck(arrayNode && arrayNode->has<ZfYAML::AnyNode::Array>());
  array.value = ZfYAML::handler<YAMLUnionArray>(arrayNode).ctor();
  ZuCheck(array.value.p<YAMLUnionArray>().length() == 3);
  saved.null();
  ZfYAML::save(saved, array);
  ZuCheck(saved == "value:\n  - 1\n  - 2\n  - 3");

  auto scalarTree = ZfYAML::scan("value: text\n");
  auto scalar = ZfYAML::handler<YAMLUnionHolder>(scalarTree.p<1>()).ctor();
  auto scalarNode = scalar.value.p<const ZfYAML::AnyNode *>();
  ZuCheck(scalarNode && scalarNode->has<ZfYAML::AnyNode::String>());
  scalar.value = ZfYAML::handler<YAMLUnionText>(scalarNode).ctor();
  saved.null();
  ZfYAML::save(saved, scalar);
  ZuCheck(saved == "value:\n  \"text\"");

  auto nullTree = ZfYAML::scan("value: null\n");
  auto nullValue = ZfYAML::handler<YAMLUnionHolder>(nullTree.p<1>()).ctor();
  auto nullNode = nullValue.value.p<const ZfYAML::AnyNode *>();
  ZuCheck(nullNode && nullNode->scalarType == ZfYAML::ScalarTC::Null);
  using UnionHandler = ZfYAML::As<decltype(nullValue.value)>::
    template Handler<decltype(nullValue.value), ZuFacet::YAML>;
  ZuCheck(!UnionHandler::valid(nullptr));

  auto numberTree = ZfYAML::scan("value: 42\n");
  auto number = ZfYAML::handler<YAMLUnionHolder>(numberTree.p<1>()).ctor();
  ZuCheck(number.value.p<const ZfYAML::AnyNode *>()->scalarType ==
    ZfYAML::ScalarTC::Number);
  auto boolTree = ZfYAML::scan("value: true\n");
  auto boolean = ZfYAML::handler<YAMLUnionHolder>(boolTree.p<1>()).ctor();
  ZuCheck(boolean.value.p<const ZfYAML::AnyNode *>()->scalarType ==
    ZfYAML::ScalarTC::True);

  YAMLIntMapRef map = new YAMLIntMap{};
  map->add("answer", 42);
  YAMLUnionHolder mapped;
  mapped.value = map;
  saved.null();
  ZfYAML::save(saved, mapped);
  ZuCheck(saved == "value:\n  answer: 42");

  YAMLUnionHolder empty;
  saved.null();
  ZfYAML::save(saved, empty);
  ZuCheck(saved == "value:\n  null");

  ZfYAML::Union<YAMLUnionA, ZmRef<YAMLPtrObj>> pointer;
  pointer = ZmRef<YAMLPtrObj>{new YAMLPtrObj{9}};
  saved.null();
  ZfYAML::save(saved, pointer);
  ZuCheck(saved == "value: 9");
  pointer = ZmRef<YAMLPtrObj>{};
  saved.null();
  ZfYAML::save(saved, pointer);
  ZuCheck(saved == "null");
}

static void pointers()
{
  ZuTestScope(pointers);

  auto tree = ZfYAML::scan(
    "object: {value: 1}\nobjects: [{value: 2}, {value: 3}]\ntext: hello\n");
  auto value = ZfYAML::handler<YAMLPtrHolder>(tree.p<1>()).ctor();
  ZuCheck(value.object && value.object->value == 1);
  ZuCheck(value.objects && value.objects->length() == 2);
  ZuCheck((*value.objects)[0] && (*value.objects)[0]->value == 2);
  ZuCheck(value.text && value.text->value == "hello");

  ZtString<> saved;
  ZfYAML::save(saved, value);
  ZuCheck(saved.find("text: hello") >= 0);
  ZuCheck(saved.find("objects:\n  -\n    value: 2") >= 0);

  auto first = (*value.objects)[0].ptr();
  (*value.objects)[1] = nullptr;
  auto update = ZfYAML::scan("[{value: 4}, {value: 5}]\n");
  ZfYAML::handler<YAMLPtrArray>(update.p<1>()).update(*value.objects);
  ZuCheck((*value.objects)[0].ptr() == first && first->value == 4);
  ZuCheck((*value.objects)[1] && (*value.objects)[1]->value == 5);

  auto bad = ZfYAML::scan("[wrong, {value: 6}]\n");
  ZuCheck(loadError([&bad, &value] {
    ZfYAML::handler<YAMLPtrArray>(bad.p<1>()).update(*value.objects);
  }));
  ZuCheck(first->value == 4);

  saved.null();
  ZfYAML::save(saved, value.object);
  ZuCheck(saved == "value: 1");
  ZmRef<YAMLPtrObj> null;
  saved.null();
  ZfYAML::save(saved, null);
  ZuCheck(saved == "null");

  auto raw = ZfYAML::handler<YAMLPtrObj>(
    ZfYAML::scan("value: 7\n").p<1>()).alloc();
  ZuCheck(raw && raw->value == 7);
  delete raw;

  auto mapTree = ZfYAML::scan("one: {value: 8}\n");
  auto map = ZmRef(ZfYAML::handler<YAMLPtrMap>(mapTree.p<1>()).alloc());
  ZuCheck(map->findVal("one") && map->findVal("one")->value == 8);
  saved.null();
  ZfYAML::save(saved, map);
  ZuCheck(saved.find("one:\n  value: 8") >= 0);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();

  ZuTestCall(block);
  ZuTestCall(flow);
  ZuTestCall(documents);
  ZuTestCall(aliases);
  ZuTestCall(transforms);
  ZuTestCall(collections);
  ZuTestCall(scalars);
  ZuTestCall(blockScalars);
  ZuTestCall(invalid);
  ZuTestCall(scalarFormats);
  ZuTestCall(handlers);
  ZuTestCall(transformEdges);
  ZuTestCall(transformGolden);
  ZuTestCall(maps);
  ZuTestCall(unions);
  ZuTestCall(pointers);
}
