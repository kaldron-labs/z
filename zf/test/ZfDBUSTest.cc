//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuPtr.hh>
#include <zlib/ZuUnion.hh>
#include <zlib/ZmHash.hh>
#include <zlib/ZmLHash.hh>
#include <zlib/ZmRBTree.hh>
#include <zlib/ZfJSON.hh>
#include <zlib/ZfCBOR.hh>
#include <zlib/ZfDBUS.hh>

#include "ZfMapTest.hh"

using namespace ZuTestUtil;
using namespace ZuFieldProp;

using Text = ZtString<ZtStringHeapID<"ZfDBUSTest.Text">>;
using Buffer = ZtString<ZtStringHeapID<"ZfDBUSTest.Buffer">>;
using Bytes = ZtArray<uint8_t, ZtArrayHeapID<"ZfDBUSTest.Bytes">>;
using Values = ZtArray<uint32_t, ZtArrayHeapID<"ZfDBUSTest.Values">>;
using TextValues = ZtArray<Text, ZtArrayHeapID<"ZfDBUSTest.TextValues">>;

ZuStructFacet(CustomDBUS);

struct Body {
  Text		text;
  Bytes		bytes;
  uint32_t	value;
  bool		flag;
};

struct Inner { uint16_t value; };
struct Outer { Inner inner; uint8_t tag; };
struct ArrayBody { Values values; };
struct PathArrayBody { TextValues paths; };
struct Names { Text path, signature; int8_t code; };
struct Scalars {
  double	floating;
  uint64_t	u64;
  int64_t	i64;
  uint32_t	u32;
  int32_t	i32;
  uint16_t	u16;
  int16_t	i16;
  uint8_t	u8;
  bool		boolean;
};
struct Borrowed { ZuBSpan bytes; const char *text; };
struct SpanText { ZuCSpan text; };
struct SpanArray { ZuSpan<const uint32_t> values; };
struct BoolOnly { bool value; };
struct TextOnly { Text value; };
struct Access {
  uint32_t	readOnly = 11;
  uint32_t	storedValue = 0;

  uint32_t value() const { return storedValue; }
  void value(uint32_t value_) { storedValue = value_; }
};
struct Empty { };
struct UIntArray : public Values {
  ZuDerive_(UIntArray, Values);
  friend ZfDBUS::AsArray<ZfFieldTC::UInt32> ZfDBUS_Fmt(UIntArray *);
};
struct ReservedArray : public Values {
  unsigned rawSized = 0;
  unsigned ensured = 0;

  ZuDerive_(ReservedArray, Values);
  using Values::length;
  void ensure(uint64_t n) { ensured = unsigned(n); Values::ensure(n); }
  void length(uint64_t n, bool init) {
    rawSized = unsigned(n);
    Values::length(n, init);
  }
  friend ZfDBUS::AsArray<ZfFieldTC::UInt32> ZfDBUS_Fmt(ReservedArray *);
};
using UIntArrayValues = ZtArray<UIntArray,
  ZtArrayHeapID<"ZfDBUSTest.UIntArrayValues">>;
struct NestedArrays : public UIntArrayValues {
  ZuDerive_(NestedArrays, UIntArrayValues);
  friend ZfDBUS::AsArray<ZfFieldTC::UDT> ZfDBUS_Fmt(NestedArrays *);
};
using DictTree = ZmRBTreeKV<Text, uint32_t, ZmRBTreeUnique<true>>;
using Dict = ZfMapTest<"ZfDBUSTest.Dict", DictTree>;
inline ZfDBUS::AsMap<> ZfDBUS_Fmt(Dict *);

using HashDict = ZfMapTest<"ZfDBUSTest.HashDict",
  ZmHashKV<Text, uint32_t>>;
inline ZfDBUS::AsMap<> ZfDBUS_Fmt(HashDict *);

struct Variant : public ZuUnion<uint32_t, Text, ZfDBUS::Any> {
  ZuDerive_(Variant, (ZuUnion<uint32_t, Text, ZfDBUS::Any>));
  friend ZuDefaultRDecayer ZuRDecayer(Variant *);
  friend ZfDBUS::AsVariant ZfDBUS_Fmt(Variant *);
};
using VariantDict = ZfMapTest<"ZfDBUSTest.VariantDict",
  ZmLHashKV<Text, Variant>>;
inline ZfDBUS::AsMap<> ZfDBUS_Fmt(VariantDict *);
struct Containers { Variant variant; Dict dict; UIntArray array; };
struct MapBody { VariantDict dict; uint32_t tail; };
struct Constructed {
  uint32_t	second;
  uint16_t	first;

  Constructed(uint32_t second_, uint16_t first_) :
    second{second_}, first{first_} { }
};
struct NestedConstructed {
  Constructed inner;
  NestedConstructed(Constructed inner_) : inner(ZuMv(inner_)) { }
};
struct Reused { Text text; uint32_t value; };

ZfStruct(, Body,
  (((text),	(Mutable)),	(String)),
  (((value),	(Mutable)),	(UInt32)),
  (((flag),	(Mutable)),	(Bool)),
  (((bytes),	(Mutable)),	(Bytes)));

ZfStructRender(, Body, DBUS, text, value, flag, bytes);

ZfStruct(, Inner, (((value), (Mutable)), (UInt16)));
ZfStructRender(, Inner, DBUS, value);
ZfStruct(, Outer,
  (((tag), (Mutable)), (UInt8)),
  (((inner), (Mutable)), (UDT)));
ZfStructRender(, Outer, DBUS, tag, inner);
ZfStruct(, ArrayBody, (((values), (Mutable)), (UInt32Vec)));
ZfStructRender(, ArrayBody, DBUS, values);
ZfStruct(, PathArrayBody, (((paths), (Mutable)), (StringVec)));
ZfStructRender(, PathArrayBody, DBUS,
  (paths, (DBUS::ElemType<ZfDBUS::Type::ObjectPath>)));
ZfStruct(, Names,
  (((path), (Mutable)), (String)),
  (((signature), (Mutable)), (String)),
  (((code), (Mutable)), (Int8)));
ZfStructRender(, Names, DBUS,
  (path, (DBUS::Type<ZfDBUS::Type::ObjectPath>)),
  (signature, (DBUS::Type<ZfDBUS::Type::Signature>)),
  code);
ZfStruct(, Scalars,
  (((boolean), (Mutable)), (Bool)),
  (((u8), (Mutable)), (UInt8)),
  (((i16), (Mutable)), (Int16)),
  (((u16), (Mutable)), (UInt16)),
  (((i32), (Mutable)), (Int32)),
  (((u32), (Mutable)), (UInt32)),
  (((i64), (Mutable)), (Int64)),
  (((u64), (Mutable)), (UInt64)),
  (((floating), (Mutable)), (Float)));
ZfStructRender(, Scalars, DBUS,
  boolean, u8, i16, u16, i32, u32, i64, u64, floating);
ZfStruct(, Borrowed,
  (((text), (Mutable)), (CString)),
  (((bytes), (Mutable)), (Bytes)));
ZfStructRender(, Borrowed, DBUS, text, bytes);
ZfStruct(, SpanText, (((text), (Mutable)), (String)));
ZfStructRender(, SpanText, DBUS, text);
ZfStruct(, SpanArray, (((values), (Mutable)), (UInt32Vec)));
ZfStructRender(, SpanArray, DBUS, values);
ZfStruct(, BoolOnly, (((value), (Mutable)), (Bool)));
ZfStructRender(, BoolOnly, DBUS, value);
ZfStruct(, TextOnly, (((value), (Mutable)), (String)));
ZfStructRender(, TextOnly, DBUS, value);
ZfStruct(, Access,
  (((readOnly, Rd), (Mutable)), (UInt32)),
  (((value, Fn), (Mutable)), (UInt32)));
ZfStructRender(, Access, DBUS, readOnly, value);
ZfStruct(, Containers,
  (((array), (Mutable)), (UDT)),
  (((dict), (Mutable)), (UDT)),
  (((variant), (Mutable)), (UDT)));
ZfStructRender(, Containers, DBUS, array, dict, variant);
ZfStruct(, MapBody,
  (((dict), (Mutable)), (UDT)),
  (((tail), (Mutable)), (UInt32)));
ZfStructRender(, MapBody, DBUS, dict, tail);
ZuTypeList<> ZuFields_(Empty *, ZuFacet::DBUS *);
ZfStruct(, Constructed,
  (((first), (Ctor<1>)), (UInt16)),
  (((second), (Ctor<0>)), (UInt32)));
ZfStructRender(, Constructed, DBUS, first, second);
ZfStruct(, NestedConstructed,
  (((inner), (Ctor<0>)), (UDT)));
ZfStructRender(, NestedConstructed, DBUS, inner);
ZfStruct(, Reused,
  (((text), (Mutable)), (String)),
  (((value), (Mutable)), (UInt32)));
ZfStructRender(, Reused, JSON, text, value);
ZfStructRender(, Reused, CBOR, text, value);
ZfStructRender(, Reused, DBUS, text, value);
ZfStructRender(, Reused, CustomDBUS, value, text);

static void scalar()
{
  ZuTestScope(scalar);
  Body body{"hi", {3, 4}, 0x01020304, true};
  ZuCheck(ZfDBUS::signature<Body>() == "subay");
  Buffer buffer;
  ZuCheck(&ZfDBUS::save(buffer, body) == &buffer);
  Buffer measuredBuffer;
  auto measured = ZfDBUS::measure(body);
  ZuCheck(bool(measured));
  ZuCheck(&ZfDBUS::saveMeasured(measuredBuffer, body, {}, measured) ==
    &measuredBuffer);
  ZuCheck(measuredBuffer == buffer);
  uint8_t expected[] = {
    2, 0, 0, 0, 'h', 'i', 0, 0,
    4, 3, 2, 1, 1, 0, 0, 0, 2, 0, 0, 0, 3, 4
  };
  ZuCheck(ZuBSpan(buffer) == ZuBSpan(expected));
  auto h = ZfDBUS::handler<Body>({buffer, "subay"});
  ZuCheck(bool(h));
  ZuCheck(!ZfDBUS::handler<Body>({buffer, "s"}));
  auto copy = h.ctor();
  ZuCheck(copy.text == body.text && copy.value == body.value && copy.flag == body.flag &&
    copy.bytes == body.bytes);

  uint8_t bad[] = {2, 0, 0, 0, 'h'};
  ZuCheck(!ZfDBUS::handler<Body>({bad, "sub"}));
}

static void nested()
{
  ZuTestScope(nested);
  Outer outer{{0x0102}, 7};
  ZuCheck(ZfDBUS::signature<Outer>() == "y(q)");
  Buffer buffer;
  ZfDBUS::save(buffer, outer);
  uint8_t expected[] = {7, 0, 0, 0, 0, 0, 0, 0, 2, 1};
  ZuCheck(ZuBSpan(buffer) == ZuBSpan(expected));
  auto h = ZfDBUS::handler<Outer>({buffer, "y(q)"});
  ZuCheck(bool(h));
  auto copy = h.ctor();
  ZuCheck(copy.tag == 7 && copy.inner.value == 0x0102);
}

static void array()
{
  ZuTestScope(array);
  ZuCheck(ZfDBUS::signature<ArrayBody>() == "au");
  ArrayBody body{{0x01020304, 0xa0b0c0d0}};
  Buffer buffer;
  ZfDBUS::save(buffer, body);
  uint8_t expected[] = {
    8, 0, 0, 0, 4, 3, 2, 1, 0xd0, 0xc0, 0xb0, 0xa0
  };
  ZuCheck(ZuBSpan(buffer) == ZuBSpan(expected));
  auto h = ZfDBUS::handler<ArrayBody>({buffer, "au"});
  ZuCheck(bool(h));
  auto copy = h.ctor();
  ZuCheck(copy.values == body.values);
  ArrayBody existing{{7, 8, 9}};
  h.load(existing);
  ZuCheck(existing.values == body.values);
  auto reserved = ZfDBUS::handler<ReservedArray>({buffer, "au"}).ctor();
  ZuCheck(reserved.length() == 2 && reserved.rawSized == 2 &&
    !reserved.ensured);
  ReservedArray reused;
  reused.ensure(8);
  auto storage = reused.begin();
  reused.rawSized = reused.ensured = 0;
  ZfDBUS::handler<ReservedArray>({buffer, "au"}).load(reused);
  ZuCheck(reused.begin() == storage && reused.length() == 2 &&
    reused.rawSized == 2 && !reused.ensured);

  buffer.length(0);
  ZfDBUS::save(buffer, body, {.order = ZfDBUS::Order::Big});
  reserved = ZfDBUS::handler<ReservedArray>({
    buffer, "au", 0, ZfDBUS::Order::Big}).ctor();
  ZuCheck(reserved.length() == 2 && !reserved.rawSized &&
    reserved.ensured == 2 && reserved[0] == body.values[0] &&
    reserved[1] == body.values[1]);
}

static void construction()
{
  ZuTestScope(construction);
  uint8_t data[] = {2, 1, 0, 0, 6, 5, 4, 3};
  auto h = ZfDBUS::handler<Constructed>({data, "qu"});
  ZuCheck(bool(h));
  auto value = h.ctor();
  ZuCheck(value.first == 0x0102 && value.second == 0x03040506);
  ZuPtr<Constructed> allocated = h.alloc();
  ZuCheck(allocated->first == value.first && allocated->second == value.second);
  alignas(Constructed) uint8_t storage[sizeof(Constructed)];
  h.new_(storage);
  auto placed = reinterpret_cast<Constructed *>(storage);
  ZuCheck(placed->first == value.first && placed->second == value.second);
  placed->~Constructed();

  Body unchanged{"unchanged", {8}, 9, false};
  uint8_t bad[] = {2, 0, 0, 0, 'x'};
  auto invalid = ZfDBUS::handler<Body>({bad, "subay"});
  ZuCheck(!invalid);
  invalid.load(unchanged);
  ZuCheck(unchanged.text == "unchanged" && unchanged.value == 9 &&
    !unchanged.flag && unchanged.bytes.length() == 1 && unchanged.bytes[0] == 8);
}

static void overrides()
{
  ZuTestScope(overrides);
  Names names{"/x", "su", 42};
  ZuCheck(ZfDBUS::signature<Names>() == "ogy");
  Buffer buffer;
  ZfDBUS::save(buffer, names);
  uint8_t expected[] = {2, 0, 0, 0, '/', 'x', 0, 2, 's', 'u', 0, 42};
  ZuCheck(ZuBSpan(buffer) == ZuBSpan(expected));
  auto h = ZfDBUS::handler<Names>({buffer, "ogy"});
  ZuCheck(bool(h));
  auto copy = h.ctor();
  ZuCheck(copy.path == names.path && copy.signature == names.signature &&
    copy.code == names.code);

  Names badPath{"x", "su", 1};
  buffer.length(0);
  auto result = ZfDBUS::measure(badPath);
  ZuCheck(!result && result.error == ZfDBUS::Error::Syntax);
  ZfDBUS::save(buffer, badPath);
  ZuCheck(!buffer.length());
  Names badSignature{"/x", "a{", 1};
  buffer.length(0);
  result = ZfDBUS::measure(badSignature);
  ZuCheck(!result && result.error == ZfDBUS::Error::Syntax);
  ZfDBUS::save(buffer, badSignature);
  ZuCheck(!buffer.length());
  Names nonASCIIPath{"/\xff", "su", 1};
  ZuCheck(!ZfDBUS::measure(nonASCIIPath));
  Names nonASCIISignature{"/x", "\xff", 1};
  ZuCheck(!ZfDBUS::measure(nonASCIISignature));
  Names signedByte{"/x", "su", -1};
  buffer.length(0);
  ZfDBUS::save(buffer, signedByte);
  ZuCheck(uint8_t(buffer[buffer.length() - 1]) == 0xff);
}

static void framing()
{
  ZuTestScope(framing);
  Constructed value{0x03040506, 0x0102};
  Buffer buffer;
  ZfDBUS::save(buffer, value,
    {.offset = 1, .order = ZfDBUS::Order::Big});
  uint8_t expected[] = {0, 1, 2, 3, 4, 5, 6};
  ZuCheck(ZuBSpan(buffer) == ZuBSpan(expected));
  auto h = ZfDBUS::handler<Constructed>({
    buffer, "qu", 1, ZfDBUS::Order::Big});
  ZuCheck(bool(h) && h.result().offset == 8);
  auto copy = h.ctor();
  ZuCheck(copy.first == value.first && copy.second == value.second);

  expected[0] = 1;
  auto badPadding = ZfDBUS::handler<Constructed>({
    expected, "qu", 1, ZfDBUS::Order::Big});
  ZuCheck(!badPadding && badPadding.result().offset == 2);
  ZuCheck(!ZfDBUS::handler<Constructed>({buffer, "qu", 1, 'x'}));

  ZuCheck(!ZfDBUS::signature<Empty>());
  buffer.length(0);
  ZfDBUS::save(buffer, Empty{});
  ZuCheck(!buffer.length());
  ZuCheck(bool(ZfDBUS::handler<Empty>({buffer, ""})));

  auto overflow = ZfDBUS::measure(BoolOnly{true},
    {.offset = UINT_MAX});
  ZuCheck(!overflow && overflow.error == ZfDBUS::Error::Overflow &&
    !buffer.length());
  overflow = ZfDBUS::measure(Empty{},
    {.offset = uint64_t(UINT_MAX) + 1});
  ZuCheck(!overflow && overflow.error == ZfDBUS::Error::Overflow &&
    !buffer.length());
}

static void containers()
{
  ZuTestScope(containers);
  Containers value{Variant{uint32_t(7)}, {}, {1, 2}};
  value.dict.add("k", 9);
  ZuCheck(ZfDBUS::signature<UIntArray>() == "au");
  ZuCheck(ZfDBUS::signature<Dict>() == "a{su}");
  ZuCheck(ZfDBUS::signature<Variant>() == "v");
  ZuCheck(ZfDBUS::signature<Containers>() == "aua{su}v");
  Buffer buffer;
  ZfDBUS::save(buffer, value);
  uint8_t expected[] = {
    8, 0, 0, 0, 1, 0, 0, 0, 2, 0, 0, 0,
    12, 0, 0, 0, 1, 0, 0, 0, 'k', 0, 0, 0, 9, 0, 0, 0,
    1, 'u', 0, 0, 7, 0, 0, 0
  };
  ZuCheck(ZuBSpan(buffer) == ZuBSpan(expected));
  auto h = ZfDBUS::handler<Containers>({buffer, "aua{su}v"});
  ZuCheck(bool(h));
  auto copy = h.ctor();
  ZuCheck(copy.array.length() == 2 && copy.array[1] == 2);
  ZuCheck(copy.dict.count_() == 1 && copy.dict.findVal("k") == 9);
  ZuCheck(copy.variant.template is<uint32_t>() &&
    copy.variant.template p<uint32_t>() == 7);

  Variant textVariant{Text{"value"}};
  buffer.length(0);
  ZfDBUS::save(buffer, textVariant);
  auto textCopy = ZfDBUS::handler<Variant>({buffer, "v"}).ctor();
  ZuCheck(textCopy.template is<Text>() &&
    textCopy.template p<Text>() == "value");

  PathArrayBody paths{{"/a", "/b"}};
  ZuCheck(ZfDBUS::signature<PathArrayBody>() == "ao");
  buffer.length(0);
  ZfDBUS::save(buffer, paths);
  auto pathsCopy = ZfDBUS::handler<PathArrayBody>({buffer, "ao"}).ctor();
  ZuCheck(pathsCopy.paths == paths.paths);
  PathArrayBody existingPaths{{"/old"}};
  ZfDBUS::handler<PathArrayBody>({buffer, "ao"}).load(existingPaths);
  ZuCheck(existingPaths.paths == paths.paths);
  paths.paths[1] = "invalid";
  buffer.length(0);
  ZuCheck(!ZfDBUS::measure(paths));
  ZfDBUS::save(buffer, paths);
  ZuCheck(!buffer.length());

  NestedArrays nestedArrays{{1, 2}, {3}};
  ZuCheck(ZfDBUS::signature<NestedArrays>() == "aau");
  buffer.length(0);
  ZfDBUS::save(buffer, nestedArrays);
  auto nestedArraysCopy = ZfDBUS::handler<NestedArrays>({
    buffer, "aau"}).ctor();
  ZuCheck(nestedArraysCopy.length() == 2 &&
    nestedArraysCopy[0].length() == 2 && nestedArraysCopy[1][0] == 3);

  VariantDict variantDict;
  variantDict.add("answer", Variant{uint32_t(42)});
  ZuCheck(ZfDBUS::signature<VariantDict>() == "a{sv}");
  buffer.length(0);
  ZfDBUS::save(buffer, variantDict);
  ZuPtr<VariantDict> variantDictCopy{
    ZfDBUS::handler<VariantDict>({buffer, "a{sv}"}).alloc()};
  ZuCheck(variantDictCopy->count_() == 1);
  auto variantNode = variantDictCopy->find("answer");
  ZuCheck(variantNode && variantNode->template p<1>().template is<uint32_t>() &&
    variantNode->template p<1>().template p<uint32_t>() == 42);

  MapBody nestedMap;
  nestedMap.dict.add("answer", Variant{uint32_t(42)});
  nestedMap.tail = 7;
  ZuCheck(ZfDBUS::signature<MapBody>() == "a{sv}u");
  buffer.length(0);
  ZfDBUS::save(buffer, nestedMap);
  auto nestedHandler = ZfDBUS::handler<MapBody>({buffer, "a{sv}u"});
  ZuCheck(bool(nestedHandler));
  MapBody loadedMap;
  loadedMap.dict.add("stale", Variant{uint32_t(1)});
  if (nestedHandler) nestedHandler.load(loadedMap);
  auto loadedNode = loadedMap.dict.find("answer");
  ZuCheck(loadedMap.tail == 7 && loadedMap.dict.count_() == 1 &&
    loadedNode && loadedNode->template p<1>().template is<uint32_t>() &&
    loadedNode->template p<1>().template p<uint32_t>() == 42);

  HashDict hashDict;
  hashDict.add("answer", 42);
  ZuCheck(ZfDBUS::signature<HashDict>() == "a{su}");
  buffer.length(0);
  ZfDBUS::save(buffer, hashDict);
  ZuPtr<HashDict> hashDictCopy{
    ZfDBUS::handler<HashDict>({buffer, "a{su}"}).alloc()};
  ZuCheck(hashDictCopy->count_() == 1 &&
    hashDictCopy->findVal("answer") == 42);

  uint8_t unknown[] = {2, 'a', 'y', 0, 2, 0, 0, 0, 3, 4};
  auto unknownHandler = ZfDBUS::handler<Variant>({unknown, "v"});
  ZuCheck(bool(unknownHandler));
  auto unknownValue = unknownHandler.ctor();
  ZuCheck(unknownValue.template is<ZfDBUS::Any>());
  buffer.length(0);
  ZfDBUS::save(buffer, unknownValue,
    {.offset = 1, .order = ZfDBUS::Order::Big});
  uint8_t transcoded[] = {
    2, 'a', 'y', 0, 0, 0, 0, 0, 0, 0, 2, 3, 4};
  ZuCheck(ZuBSpan(buffer) == ZuBSpan(transcoded));
}

struct CountSink {
  unsigned length = 0;
  CountSink &operator <<(char) { ++length; return *this; }
};

static void mappings()
{
  ZuTestScope(mappings);
  Scalars value{1.5, 0xf102030405060708ULL, -4, 0xf1020304, -3,
    0xff01, -2, 0xfe, true};
  ZuCheck(ZfDBUS::signature<Scalars>() == "bynqiuxtd");
  Buffer buffer;
  auto result = ZfDBUS::measure(value,
    {.order = ZfDBUS::Order::Big});
  ZfDBUS::save(buffer, value, {.order = ZfDBUS::Order::Big});
  ZuCheck(bool(result) && result.offset == buffer.length());
  auto h = ZfDBUS::handler<Scalars>({
    buffer, "bynqiuxtd", 0, ZfDBUS::Order::Big});
  ZuCheck(bool(h));
  auto copy = h.ctor();
  ZuCheck(copy.boolean == value.boolean && copy.u8 == value.u8 &&
    copy.i16 == value.i16 && copy.u16 == value.u16 &&
    copy.i32 == value.i32 && copy.u32 == value.u32 &&
    copy.i64 == value.i64 && copy.u64 == value.u64 &&
    copy.floating == value.floating);

  uint8_t raw[] = {1, 2, 3};
  Borrowed borrowed{raw, "abc"};
  buffer.length(0);
  ZfDBUS::save(buffer, borrowed);
  auto borrowedHandler = ZfDBUS::handler<Borrowed>({buffer, "say"});
  ZuCheck(bool(borrowedHandler));
  auto borrowedCopy = borrowedHandler.ctor();
  ZuCheck(ZuCSpan{borrowedCopy.text} == "abc" &&
    borrowedCopy.text == ZuCSpan{buffer}.begin() + 4);
  ZuCheck(borrowedCopy.bytes == ZuBSpan(raw) &&
    borrowedCopy.bytes.begin() ==
      ZuBSpan{buffer}.begin() + 12);

  CountSink count;
  result = ZfDBUS::measure(value, {.offset = 3});
  ZfDBUS::save(count, value, {.offset = 3});
  ZuCheck(bool(result) && result.offset - 3 == count.length);
}

static void metadata()
{
  ZuTestScope(metadata);
  uint8_t accessData[] = {5, 0, 0, 0, 7, 0, 0, 0};
  auto accessHandler = ZfDBUS::handler<Access>({accessData, "uu"});
  ZuCheck(bool(accessHandler));
  auto access = accessHandler.ctor();
  ZuCheck(access.readOnly == 11 && access.value() == 7);
  access.readOnly = 19;
  access.value(1);
  accessHandler.load(access);
  ZuCheck(access.readOnly == 19 && access.value() == 7);

  NestedConstructed nested{Constructed{0x03040506, 0x0102}};
  Buffer dbus;
  ZuCheck(ZfDBUS::signature<NestedConstructed>() == "(qu)");
  ZfDBUS::save(dbus, nested);
  auto nestedCopy = ZfDBUS::handler<NestedConstructed>({dbus, "(qu)"}).ctor();
  ZuCheck(nestedCopy.inner.first == 0x0102 &&
    nestedCopy.inner.second == 0x03040506);

  Reused reused{"x", 2};
  ZuCheck(ZfDBUS::signature<Reused>() == "su");
  ZuCheck((ZfDBUS::signature<Reused, ZuFacet::CustomDBUS>() == "us"));
  dbus.length(0);
  ZfDBUS::save(dbus, reused);
  auto dbusCopy = ZfDBUS::handler<Reused>({dbus, "su"}).ctor();
  ZuCheck(dbusCopy.text == reused.text && dbusCopy.value == reused.value);

  Buffer json, cbor;
  ZfJSON::save(json, reused);
  ZfCBOR::save(cbor, reused);
  ZuCheck(json == "{\"text\":\"x\",\"value\":2}" && cbor.length());
  auto cborCopy = ZfCBOR::handler<Reused>(cbor).ctor();
  ZuCheck(cborCopy.text == reused.text && cborCopy.value == reused.value);

  dbus.length(0);
  ZfDBUS::save<ZuFacet::CustomDBUS>(dbus, reused);
  auto customCopy = ZfDBUS::handler<Reused, ZuFacet::CustomDBUS>(
    {dbus, "us"}).ctor();
  ZuCheck(customCopy.text == reused.text && customCopy.value == reused.value);
}

static void malformed()
{
  ZuTestScope(malformed);
  uint8_t badBool[] = {2, 0, 0, 0};
  auto result = ZfDBUS::handler<BoolOnly>({badBool, "b"}).result();
  ZuCheck(!result && result.error == ZfDBUS::Error::Syntax);

  uint8_t badUTF8[] = {2, 0, 0, 0, 0xc0, 0x80, 0};
  ZuCheck(!ZfDBUS::handler<TextOnly>({badUTF8, "s"}));
  uint8_t embeddedNUL[] = {2, 0, 0, 0, 'x', 0, 0};
  ZuCheck(!ZfDBUS::handler<TextOnly>({embeddedNUL, "s"}));

  uint8_t shortArray[] = {3, 0, 0, 0, 1, 2, 3};
  ZuCheck(!ZfDBUS::handler<ArrayBody>({shortArray, "au"}));
  // 0x04000001: one byte above D-Bus's 64MiB array-data limit.
  uint8_t longArray[] = {1, 0, 0, 4};
  result = ZfDBUS::handler<ArrayBody>({longArray, "au"}).result();
  ZuCheck(!result && result.error == ZfDBUS::Error::Overflow);
  uint8_t oversizedBytes[] = {
    1, 0, 0, 0, 'x', 0, 0, 0, 1, 0, 0, 4
  };
  result = ZfDBUS::handler<Borrowed>({oversizedBytes, "say"}).result();
  ZuCheck(!result && result.error == ZfDBUS::Error::Overflow);
  uint8_t byte = 0;
  Borrowed tooLarge{{&byte, ZfDBUS::Limit::Array + 1}, "x"};
  result = ZfDBUS::measure(tooLarge);
  ZuCheck(!result && result.error == ZfDBUS::Error::Overflow);
  tooLarge.bytes = {&byte, uint64_t(UINT_MAX) + 1};
  result = ZfDBUS::measure(tooLarge);
  ZuCheck(!result && result.error == ZfDBUS::Error::Overflow);
  char character = 0;
  SpanText longText{{&character, uint64_t(UINT_MAX) + 1}};
  result = ZfDBUS::measure(longText);
  ZuCheck(!result && result.error == ZfDBUS::Error::Overflow);
  uint32_t word = 0;
  SpanArray hugeArray{{&word, uint64_t(UINT_MAX) + 1}};
  result = ZfDBUS::measure(hugeArray);
  ZuCheck(!result && result.error == ZfDBUS::Error::Overflow);
  uint8_t paddedArray[] = {0, 1, 0, 0, 0, 0, 0, 0};
  result = ZfDBUS::handler<ArrayBody>({
    paddedArray, "au", 1, ZfDBUS::Order::Little}).result();
  ZuCheck(!result && result.offset == 3);

  uint8_t names[] = {
    2, 0, 0, 0, '/', 'x', 0, 2, 's', 'u', 0, 255};
  auto namesHandler = ZfDBUS::handler<Names>({names, "ogy"});
  ZuCheck(bool(namesHandler) && namesHandler.ctor().code == -1);
  names[4] = 0xff;
  ZuCheck(!ZfDBUS::handler<Names>({names, "ogy"}));
  names[4] = '/';
  names[8] = 0xff;
  ZuCheck(!ZfDBUS::handler<Names>({names, "ogy"}));

  Buffer valid;
  ZfDBUS::save(valid, BoolOnly{true});
  valid << char(0);
  result = ZfDBUS::handler<BoolOnly>({valid, "b"}).result();
  ZuCheck(!result && result.error == ZfDBUS::Error::Trailing);

  ZuCheck(!ZfDBUS::validSignature("a{"));
  ZuCheck(!ZfDBUS::validSignature("()"));
  ZuCheck(!ZfDBUS::validSignature("{su}"));
  Buffer deep;
  for (unsigned i = 0; i <= ZfDBUS::Limit::TypeDepth; ++i) deep << 'a';
  deep << 'y';
  ZuCheck(!ZfDBUS::validSignature(deep));
  deep.length(0);
  for (unsigned i = 0; i < ZfDBUS::Limit::TypeDepth; ++i) deep << 'a';
  for (unsigned i = 0; i < ZfDBUS::Limit::TypeDepth; ++i) deep << '(';
  deep << 'y';
  for (unsigned i = 0; i < ZfDBUS::Limit::TypeDepth; ++i) deep << ')';
  ZuCheck(ZfDBUS::validSignature(deep, true));
  deep.length(0);
  for (unsigned i = 0; i <= ZfDBUS::Limit::TypeDepth; ++i) deep << '(';
  deep << 'y';
  for (unsigned i = 0; i <= ZfDBUS::Limit::TypeDepth; ++i) deep << ')';
  ZuCheck(!ZfDBUS::validSignature(deep));

  auto variantBody = [](Buffer &body, unsigned depth) {
    body.length(0);
    for (unsigned i = 1; i < depth; ++i) {
      body << char(1) << 'v' << char(0);
    }
    body << char(1) << 'y' << char(0) << char(7);
  };
  Buffer variants;
  variantBody(variants, ZfDBUS::Limit::ValueDepth);
  ZuCheck(bool(ZfDBUS::handler<Variant>({variants, "v"})));
  variantBody(variants, ZfDBUS::Limit::ValueDepth + 1);
  ZuCheck(!ZfDBUS::handler<Variant>({variants, "v"}));
  Variant dynamic{ZfDBUS::Any{
    {ZuBSpan{variants}.begin() + 3, unsigned(variants.length() - 3)},
    "v", 3}};
  Buffer output;
  result = ZfDBUS::measure(dynamic);
  ZuCheck(!result && result.error == ZfDBUS::Error::Overflow &&
    !output.length());

  uint8_t fd[] = {0, 0, 0, 0};
  result = ZfDBUS::validate({fd, "h"}, "h");
  ZuCheck(!result && result.error == ZfDBUS::Error::Unsupported);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(scalar);
  ZuTestCall(nested);
  ZuTestCall(array);
  ZuTestCall(construction);
  ZuTestCall(overrides);
  ZuTestCall(framing);
  ZuTestCall(containers);
  ZuTestCall(mappings);
  ZuTestCall(metadata);
  ZuTestCall(malformed);
  return 0;
}
