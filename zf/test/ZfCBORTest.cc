//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZfCBOR.hh>

using namespace ZuTestUtil;
using namespace ZuFieldProp;

ZuDerive(OwnedText, (ZtString<ZtStringHeapID<"ZfCBORTest.Text">>));
ZuDerive(OwnedBytes, (ZtArray<uint8_t, ZtArrayHeapID<"ZfCBORTest.Bytes">>));
ZuDerive(TestBuf, (ZtString<ZtStringHeapID<"ZfCBORTest.Buf">>));
ZuDerive(IntValues, (ZtArray<int32_t, ZtArrayHeapID<"ZfCBORTest.Values">>));

struct Object {
  int32_t	kty;
  ZuBSpan	data;
};

struct ArrayObject {
  IntValues	values;
};

struct IntKeyObject {
  int32_t	value;
};

struct MinIntKeyObject {
  int32_t	value;
};

struct TextObject {
  ZuCSpan	value;
};

struct LongKeyObject {
  int32_t	value, shortValue;
};

ZfStruct(, ArrayObject,
  (values,	(Mutable),	Int32Vec));

ZfStructRender(, ArrayObject, CBOR, values);

ZfStruct(, IntKeyObject,
  (value,	(Mutable),	Int32));

ZfStructRender(, IntKeyObject, CBOR,
  (value, (CBOR::ID<"4294967296">, CBOR::IntID)));

ZfStruct(, MinIntKeyObject,
  (value,	(Mutable),	Int32));

ZfStructRender(, MinIntKeyObject, CBOR,
  (value, (CBOR::ID<"-18446744073709551616">, CBOR::IntID)));

ZfStruct(, TextObject,
  (value,	(Mutable),	String));

ZfStructRender(, TextObject, CBOR, value);

ZfStruct(, LongKeyObject,
  (value,		(Mutable),	Int32),
  (shortValue,	(Mutable),		Int32));

ZfStructRender(, LongKeyObject, CBOR,
  (value, (CBOR::ID<"abcdefghijklmnopqrstuvwxyz0123456789">)),
  (shortValue, (CBOR::ID<"z">)));

struct IntArray : public IntValues {
  ZuDerive_(IntArray, IntValues);
  friend ZfCBOR::AsArray<ZfFieldTC::Int32> ZfCBOR_Fmt(IntArray *);
};

ZfStruct(, Object,
  (kty,	(Mutable),		Int32),
  (data,	(Mutable),	Bytes));

ZfStructRender(, Object, CBOR,
  (kty,	(CBOR::ID<"1">, CBOR::IntID)),
  data);

static void scalar()
{
  ZuTestScope(scalar);
  uint8_t u8[] = {0x18, 0x18};
  uint8_t u16[] = {0x19, 0x01, 0x00};
  uint8_t u32[] = {0x1a, 0x00, 0x01, 0x00, 0x00};
  uint8_t u[] = {0x1b, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff};
  uint8_t n[] = {0x3b, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff};
  uint8_t simple[] = {0xf8, 0x00};
  uint8_t f16[] = {0xf9, 0x7e, 0x00};
  uint8_t f32[] = {0xfa, 0x3f, 0x80, 0x00, 0x00};
  uint8_t f64[] = {0xfb, 0x3f, 0xf0, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00};
  uint8_t null[] = {0xf6};
  using Props = ZuTypeList<>;
  ZuCheck(bool(ZfCBOR::scan(u8)));
  ZuCheck(bool(ZfCBOR::scan(u16)));
  ZuCheck(bool(ZfCBOR::scan(u32)));
  ZuCheck(bool(ZfCBOR::scan(u)));
  ZuCheck(bool(ZfCBOR::scan(n)));
  ZuCheck(bool(ZfCBOR::scan(simple)));
  ZuCheck(bool(ZfCBOR::scan(f16)));
  ZuCheck(bool(ZfCBOR::scan(f32)));
  ZuCheck(bool(ZfCBOR::scan(f64)));
  for (unsigned i = 0; i <= UINT8_MAX; ++i) {
    uint8_t value[] = {uint8_t(i < 24 ? 0xe0 | i : 0xf8), uint8_t(i)};
    ZuCheck(bool(ZfCBOR::scan({value, i < 24 ? 1U : 2U})));
  }
  auto nullValue = ZfCBOR::loadValue<ZfFieldTC::Int32, Props, int32_t>(null);
  ZuCheck(nullValue == ZuCmp<int32_t>::null());

  using SimpleProps = ZuTypeList<CBOR::Simple>;
  uint8_t simple255[] = {0xf8, 0xff};
  auto simpleValue = ZfCBOR::loadValue<ZfFieldTC::UInt8, SimpleProps, uint8_t>(
    simple255);
  ZuCheck(simpleValue == 255);
  TestBuf out;
  ZfCBOR::saveValue<decltype(out), ZfFieldTC::UInt8, SimpleProps>(
    out, simpleValue);
  ZuCheck((ZuBSpan{out} == ZuBSpan{simple255}));

  OwnedBytes vec{1, 24};
  out.length(0);
  ZfCBOR::saveValue<decltype(out), ZfFieldTC::UInt8Vec, SimpleProps>(out, vec);
  uint8_t simpleVec[] = {0x82, 0xe1, 0xf8, 0x18};
  ZuCheck((ZuBSpan{out} == ZuBSpan{simpleVec}));

  using TagProps = ZuTypeList<CBOR::Tag<100>>;
  uint8_t tagged[] = {0xd8, 0x64, 0x01};
  auto taggedValue = ZfCBOR::loadValue<ZfFieldTC::Int32, TagProps, int32_t>(
    tagged);
  ZuCheck(taggedValue == 1);
  out.length(0);
  ZfCBOR::saveValue<decltype(out), ZfFieldTC::Int32, TagProps>(out,
    taggedValue);
  ZuCheck((ZuBSpan{out} == ZuBSpan{tagged}));

  uint8_t taggedVec[] = {0xd8, 0x64, 0x82, 0x01, 0x02};
  auto taggedValues = ZfCBOR::loadValue<ZfFieldTC::Int32Vec, TagProps,
    IntValues>(taggedVec);
  ZuCheck(taggedValues.length() == 2 && taggedValues[1] == 2);
  IntValues values{1, 2};
  out.length(0);
  ZfCBOR::saveValue<decltype(out), ZfFieldTC::Int32Vec, TagProps>(out, values);
  ZuCheck((ZuBSpan{out} == ZuBSpan{taggedVec}));

  using TaggedArray = ZfCBOR::AsArray<ZfFieldTC::Int32>::Handler<
    IntArray, ZuFacet::CBOR, TagProps>;
  auto taggedArray = TaggedArray{ZuFalse{}, taggedVec}.ctor();
  ZuCheck(taggedArray.length() == 2 && taggedArray[1] == 2);
}

static void containers()
{
  ZuTestScope(containers);
  uint8_t definite[] = {0xa2, 0x61, 'a', 0x82, 0x01, 0x02,
    0x61, 'b', 0x42, 0x00, 0x01};
  uint8_t indefinite[] = {0xbf, 0x61, 'a', 0x9f, 0x01, 0x02,
    0xff, 0x61, 'b', 0x5f, 0x41, 0x00, 0x41, 0x01, 0xff, 0xff};
  auto result = ZfCBOR::scan(definite);
  ZuCheck(bool(result));
  ZuCheck(result.offset == sizeof(definite));
  result = ZfCBOR::scan(indefinite);
  ZuCheck(bool(result));
  ZuCheck(result.offset == sizeof(indefinite));

  uint8_t chunkedText[] = {0x7f, 0x62, 'a', 'b', 0x63, 'c', 'd', 'e', 0xff};
  uint8_t chunkedBytes[] = {0x5f, 0x42, 1, 2, 0x43, 3, 4, 5, 0xff};
  using Props = ZuTypeList<>;
  auto borrowed = ZfCBOR::loadValue<ZfFieldTC::String, Props, ZuCSpan>(
    chunkedText);
  auto text = ZfCBOR::loadValue<ZfFieldTC::String, Props, OwnedText>(
    chunkedText);
  auto bytes = ZfCBOR::loadValue<ZfFieldTC::Bytes, Props, OwnedBytes>(
    chunkedBytes);
  ZuCheck(!borrowed);
  ZuCheck(text == "abcde");
  ZuCheck(bytes.length() == 5 && bytes[0] == 1 && bytes[4] == 5);
}

static void tags()
{
  ZuTestScope(tags);
  uint8_t nested[] = {0xdb, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xc1, 0x00};
  auto result = ZfCBOR::scan(nested);
  ZuCheck(bool(result));
  ZuCheck(result.offset == sizeof(nested));
}

static void malformed()
{
  ZuTestScope(malformed);
  uint8_t truncated[] = {0x58, 0x02, 0x01};
  uint8_t break_[] = {0xff};
  uint8_t badIndef[] = {0x1f};
  uint8_t badChunk[] = {0x5f, 0x61, 'x', 0xff};
  uint8_t oddMap[] = {0xbf, 0x01, 0xff};
  uint8_t reserved[] = {0xfc};
  uint8_t nestedBreak[] = {0x81, 0xff};
  uint8_t nestedChunk[] = {0x5f, 0x5f, 0xff, 0xff};
  ZuCheck(ZfCBOR::scan(truncated).error == ZfCBOR::Error::Syntax);
  ZuCheck(ZfCBOR::scan(break_).error == ZfCBOR::Error::Syntax);
  ZuCheck(ZfCBOR::scan(badIndef).error == ZfCBOR::Error::Syntax);
  ZuCheck(ZfCBOR::scan(badChunk).error == ZfCBOR::Error::Syntax);
  ZuCheck(ZfCBOR::scan(oddMap).error == ZfCBOR::Error::Syntax);
  ZuCheck(ZfCBOR::scan(reserved).error == ZfCBOR::Error::Syntax);
  ZuCheck(ZfCBOR::scan(nestedBreak).error == ZfCBOR::Error::Syntax);
  ZuCheck(ZfCBOR::scan(nestedChunk).error == ZfCBOR::Error::Syntax);
}

static void prefix()
{
  ZuTestScope(prefix);
  uint8_t data[] = {0x01, 0x02};
  auto result = ZfCBOR::scan(data);
  ZuCheck(bool(result));
  ZuCheck(result.offset == 1);
}

static void truncation()
{
  ZuTestScope(truncation);
  uint8_t encoded[] = {0xbf, 0x61, 'a', 0x82, 0x1a,
    0x00, 0x01, 0x00, 0x00, 0x5f, 0x42, 0x00, 0x01, 0xff, 0xff};
  for (unsigned i = 0; i < sizeof(encoded); ++i)
    ZuCheck(!ZfCBOR::scan(ZuBSpan{encoded, i}));
  ZuCheck(bool(ZfCBOR::scan(encoded)));
}

static void object()
{
  ZuTestScope(object);
  uint8_t encoded[] = {0xbf, 0x01, 0x02, 0x64, 'd', 'a', 't', 'a',
    0x42, 0x00, 0x01, 0x01, 0x03, 0xff};
  using Fields = ZuFields<Object, ZuFacet::CBOR>;
  using KtyField = ZfField_Object_CBOR_kty;
  using DataField = ZfField_Object_CBOR_data;
  enum { KtyI = ZuTypeIndex<KtyField, Fields>{} };
  enum { DataI = ZuTypeIndex<DataField, Fields>{} };
  ZuCheck(bool(ZuFieldProp::CBOR::IsIntID<typename KtyField::Props>{}));
  auto fields = ZtScratch(ZfCBOR::FieldSpansScratch, Fields::N, Fields::N);
  ZfCBOR::LoadContext<ZuTypeList<>, Fields> context(encoded);
  ZuCheck(context.index(fields));
  ZuCheck((fields[KtyI] == ZuBSpan{encoded + 12, 1}));
  ZuCheck((fields[DataI] == ZuBSpan{encoded + 8, 3}));
  ZuCheck((ZfCBOR::loadValue<ZfFieldTC::Int32, ZuTypeList<>, int32_t>(
    ZuBSpan{encoded + 12, 1}) == 3));
  Object object{};
  ZfCBOR::handler<Object>(encoded).load(object);
  ZuCheck(object.kty == 3);
  ZuCheck((object.data == ZuBSpan{encoded + 9, 2}));

  uint8_t unknown[] = {0xa3, 0x81, 0x00, 0xa1, 0x61, 'x', 0x81, 0x01,
    0x01, 0x03, 0x64, 'd', 'a', 't', 'a', 0x42, 0x00, 0x01};
  Object unknownObject{};
  ZfCBOR::handler<Object>(unknown).load(unknownObject);
  ZuCheck(unknownObject.kty == 3);
  ZuCheck((unknownObject.data == ZuBSpan{unknown + 16, 2}));

  TestBuf encoded2;
  ZfCBOR::save(encoded2, object);
  uint8_t expected[] = {0xa2, 0x01, 0x03, 0x64, 'd', 'a', 't', 'a',
    0x42, 0x00, 0x01};
  ZuCheck((ZuBSpan{encoded2} == ZuBSpan{expected}));
}

static void intKey()
{
  ZuTestScope(intKey);
  IntKeyObject value{7};
  TestBuf out;
  ZfCBOR::save(out, value);
  uint8_t expected[] = {0xa1, 0x1b, 0, 0, 0, 1, 0, 0, 0, 0, 0x07};
  ZuCheck((ZuBSpan{out} == ZuBSpan{expected}));
  auto loaded = ZfCBOR::handler<IntKeyObject>(expected).ctor();
  ZuCheck(loaded.value == 7);

  MinIntKeyObject min{7};
  out.length(0);
  ZfCBOR::save(out, min);
  uint8_t minExpected[] = {0xa1, 0x3b, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0x07};
  ZuCheck((ZuBSpan{out} == ZuBSpan{minExpected}));
  auto minLoaded = ZfCBOR::handler<MinIntKeyObject>(minExpected).ctor();
  ZuCheck(minLoaded.value == 7);
}

static void longKey()
{
  ZuTestScope(longKey);
  TestBuf encoded;
  ZfCBOR::save(encoded, LongKeyObject{7, 8});
  auto key = ZuCSpan{encoded.data() + 6, 36};
  ZuCheck(encoded.length() == 43 && uint8_t(encoded[0]) == 0xa2 &&
    encoded[1] == 'a' && encoded[2] == 'z' && uint8_t(encoded[3]) == 8 &&
    uint8_t(encoded[4]) == 0x78 && uint8_t(encoded[5]) == 36 &&
    key == "abcdefghijklmnopqrstuvwxyz0123456789" &&
    uint8_t(encoded[42]) == 7);
  auto value = ZfCBOR::handler<LongKeyObject>(encoded).ctor();
  ZuCheck(value.value == 7 && value.shortValue == 8);
}

static void text()
{
  ZuTestScope(text);
  uint8_t encoded[] = {0xa1, 0x65, 'v', 'a', 'l', 'u', 'e',
    0x65, 'h', 'e', 'l', 'l', 'o'};
  TextObject object{};
  ZfCBOR::handler<TextObject>(encoded).load(object);
  ZuCheck(object.value == "hello");
  ZuCheck(object.value.data() == reinterpret_cast<const char *>(encoded + 8));
}

static void arrays()
{
  ZuTestScope(arrays);
  uint8_t encoded[] = {0xa1, 0x66, 'v', 'a', 'l', 'u', 'e', 's',
    0x9f, 0x01, 0x02, 0x03, 0xff};
  ArrayObject object;
  ZfCBOR::handler<ArrayObject>(encoded).load(object);
  ZuCheck(object.values.length() == 3);
  ZuCheck(object.values[0] == 1);
  ZuCheck(object.values[2] == 3);
  TestBuf out;
  ZfCBOR::save(out, object);
  uint8_t expected[] = {0xa1, 0x66, 'v', 'a', 'l', 'u', 'e', 's',
    0x83, 0x01, 0x02, 0x03};
  ZuCheck((ZuBSpan{out} == ZuBSpan{expected}));
}

static void floats()
{
  ZuTestScope(floats);
  uint8_t halfOne[] = {0xf9, 0x3c, 0x00};
  uint8_t single[] = {0xfa, 0x3f, 0xc0, 0x00, 0x00};
  uint8_t nan[] = {0xfb, 0x7f, 0xf8, 0, 0, 0, 0, 0, 1};
  using Props = ZuTypeList<>;
  auto one = ZfCBOR::loadValue<ZfFieldTC::Float, Props, double>(halfOne);
  auto oneHalf = ZfCBOR::loadValue<ZfFieldTC::Float, Props, double>(single);
  auto nanValue = ZfCBOR::loadValue<ZfFieldTC::Float, Props, double>(nan);
  uint8_t huge[] = {0xfb, 0x7f, 0xe1, 0xcc, 0xf3,
    0x85, 0xeb, 0xc8, 0xa0};
  auto overflow = ZfCBOR::loadValue<ZfFieldTC::Float, Props, float>(huge);
  ZuCheck(one == 1.0);
  ZuCheck(oneHalf == 1.5);
  ZuCheck(ZuFP<double>::nan(nanValue));
  ZuCheck(overflow == 0.0f);
  TestBuf out;
  ZfCBOR::saveValue<decltype(out), ZfFieldTC::Float, Props>(out, 1.0);
  ZuCheck((ZuBSpan{out} == ZuBSpan{halfOne}));
  out.length(0);
  ZfCBOR::saveValue<decltype(out), ZfFieldTC::Float, Props>(out,
    ZuFP<double>::nan());
  uint8_t canonicalNaN[] = {0xf9, 0x7e, 0};
  ZuCheck((ZuBSpan{out} == ZuBSpan{canonicalNaN}));

  out.length(0);
  ZfCBOR::saveValue<decltype(out), ZfFieldTC::Float, Props>(out, -0.0);
  uint8_t negativeZero[] = {0xf9, 0x80, 0};
  ZuCheck((ZuBSpan{out} == ZuBSpan{negativeZero}));
}

static void decimal()
{
  ZuTestScope(decimal);
  uint8_t fixed_[] = {0xc4, 0x82, 0x21, 0x19, 0x04, 0x12};
  using Props = ZuTypeList<>;
  auto fixed = ZfCBOR::loadValue<ZfFieldTC::Fixed, Props, ZuFixed>(fixed_);
  ZuCheck(fixed.mantissa == 1042);
  ZuCheck(fixed.ndp == 2);
  TestBuf out;
  ZfCBOR::saveValue<decltype(out), ZfFieldTC::Fixed, Props>(out, fixed);
  ZuCheck((ZuBSpan{out} == ZuBSpan{fixed_}));

  uint8_t decimal_[] = {0xc4, 0x82, 0x00, 0x19, 0x30, 0x39};
  auto value = ZfCBOR::loadValue<ZfFieldTC::Decimal, Props, ZuDecimal>(decimal_);
  ZuCheck(value.value == int128_t(12345) * ZuDecimal::scale());

  uint8_t exact[] = {0xc4, 0x82, 0x32, 0x1a, 0x00, 0x01, 0xe2, 0x3a};
  uint8_t inexact[] = {0xc4, 0x82, 0x32, 0x1a, 0x00, 0x01, 0xe2, 0x3b};
  value = ZfCBOR::loadValue<ZfFieldTC::Decimal, Props, ZuDecimal>(exact);
  ZuCheck(value.value == 12345);
  auto invalid = ZfCBOR::loadValue<ZfFieldTC::Decimal, Props, ZuDecimal>(inexact);
  ZuCheck(invalid.value == ZuDecimal::null());
}

static void time()
{
  ZuTestScope(time);
  using Props = ZuTypeList<>;
  uint8_t integral[] = {0xc1, 0x1a, 0x65, 0x53, 0xf1, 0x00};
  auto value = ZfCBOR::loadValue<ZfFieldTC::Time, Props, ZuTime>(integral);
  ZuCheck(value.as_time_t() == 1700000000);
  TestBuf out;
  ZfCBOR::saveValue<decltype(out), ZfFieldTC::Time, Props>(out, value);
  ZuCheck((ZuBSpan{out} == ZuBSpan{integral}));

  uint8_t text[] = {0xc0, 0x74, '2', '0', '2', '4', '-', '0', '1', '-',
    '0', '2', 'T', '0', '3', ':', '0', '4', ':', '0', '5', 'Z'};
  auto fromText = ZfCBOR::loadValue<ZfFieldTC::Time, Props, ZuTime>(text);
  auto expected = ZuDateTime{ZuDateTimeScan::ISO{}, "2024-01-02T03:04:05Z"};
  ZuCheck(fromText == expected.as_time());

  uint8_t high[] = {0xc1, 0xfb, 0x43, 0xe0, 0, 0, 0, 0, 0, 0};
  auto clamped = ZfCBOR::loadValue<ZfFieldTC::Time, Props, ZuTime>(high);
  ZuCheck(clamped.as_time_t() == ZuCmp<int64_t>::maximum() - 1);
  ZuCheck(clamped.nanosecs() % 1000000000 == 999999999);
}

static void dateTime()
{
  ZuTestScope(dateTime);
  using Props = ZuTypeList<>;
  uint8_t encoded[] = {0xc0, 0x74, '2', '0', '2', '4', '-', '0', '1', '-',
    '0', '2', 'T', '0', '3', ':', '0', '4', ':', '0', '5', 'Z'};
  auto value = ZfCBOR::loadValue<ZfFieldTC::DateTime, Props, ZuDateTime>(encoded);
  TestBuf out;
  ZfCBOR::saveValue<decltype(out), ZfFieldTC::DateTime, Props>(out, value);
  ZuCheck((ZuBSpan{out} == ZuBSpan{encoded}));

  uint8_t epoch[] = {0xc1, 0x1a, 0x65, 0x53, 0xf1, 0x00};
  auto fromEpoch = ZfCBOR::loadValue<ZfFieldTC::DateTime, Props, ZuDateTime>(epoch);
  ZuCheck(fromEpoch.as_time_t() == 1700000000);
}

static void bignum()
{
  ZuTestScope(bignum);
  using Props = ZuTypeList<>;
  uint8_t positive[] = {0xc2, 0x49, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00};
  uint8_t negative[] = {0xc3, 0x49, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00};
  auto p = ZfCBOR::loadValue<ZfFieldTC::UInt128, Props, uint128_t>(positive);
  auto n = ZfCBOR::loadValue<ZfFieldTC::Int128, Props, int128_t>(negative);
  ZuCheck(p == (uint128_t(1) << 64));
  ZuCheck(n == -int128_t(uint128_t(1) << 64) - 1);
  TestBuf out;
  ZfCBOR::saveValue<decltype(out), ZfFieldTC::UInt128, Props>(out, p);
  ZuCheck((ZuBSpan{out} == ZuBSpan{positive}));
}

static void udt()
{
  ZuTestScope(udt);
  using Props = ZuTypeList<>;
  uint8_t encoded[] = {0xa2, 0x01, 0x03, 0x64, 'd', 'a', 't', 'a',
    0x42, 0x00, 0x01};
  auto value = ZfCBOR::loadValue<ZfFieldTC::UDT, Props, Object>(encoded);
  ZuCheck(value.kty == 3);
  ZuCheck((value.data == ZuBSpan{encoded + 9, 2}));
  TestBuf out;
  ZfCBOR::saveValue<decltype(out), ZfFieldTC::UDT, Props>(out, value);
  ZuCheck((ZuBSpan{out} == ZuBSpan{encoded}));
}

static void asArray()
{
  ZuTestScope(asArray);
  uint8_t encoded[] = {0x83, 0x01, 0x02, 0x03};
  auto value = ZfCBOR::handler<IntArray>(encoded).ctor();
  ZuCheck(value.length() == 3);
  ZuCheck(value[2] == 3);
  TestBuf out;
  ZfCBOR::save(out, value);
  ZuCheck((ZuBSpan{out} == ZuBSpan{encoded}));

  uint8_t heap[] = {0x89, 1, 2, 3, 4, 5, 6, 7, 8, 9};
  auto large = ZfCBOR::handler<IntArray>(heap).ctor();
  ZuCheck(large.length() == 9 && large[8] == 9);

  using Bounded = ZuArray<int32_t, 2>;
  using Handler = ZfCBOR::AsArray<ZfFieldTC::Int32>::Handler<
    Bounded, ZuFacet::CBOR>;
  uint8_t short_[] = {0x81, 1};
  uint8_t exact[] = {0x82, 1, 2};
  uint8_t over[] = {0x83, 1, 2, 3};
  auto shortValue = Handler{ZuFalse{}, short_}.ctor();
  auto exactValue = Handler{ZuFalse{}, exact}.ctor();
  auto overValue = Handler{ZuFalse{}, over}.ctor();
  ZuCheck(shortValue.length() == 1 && shortValue[0] == 1);
  ZuCheck(exactValue.length() == 2 && exactValue[1] == 2);
  ZuCheck(overValue.length() == 2 && overValue[1] == 2);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(scalar);
  ZuTestCall(containers);
  ZuTestCall(tags);
  ZuTestCall(malformed);
  ZuTestCall(prefix);
  ZuTestCall(truncation);
  ZuTestCall(object);
  ZuTestCall(intKey);
  ZuTestCall(longKey);
  ZuTestCall(text);
  ZuTestCall(arrays);
  ZuTestCall(floats);
  ZuTestCall(decimal);
  ZuTestCall(time);
  ZuTestCall(dateTime);
  ZuTestCall(bignum);
  ZuTestCall(udt);
  ZuTestCall(asArray);
  return 0;
}
