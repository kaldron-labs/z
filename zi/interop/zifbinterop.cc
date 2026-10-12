//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <flatbuffers/idl.h>
#include <flatbuffers/reflection.h>
#include <flatbuffers/reflection_generated.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuUnion.hh>
#include <zlib/ZtScratch.hh>
#include <zlib/ZiFB.hh>
#include <zlib/ZiFBReflect.hh>

using namespace ZuTestUtil;

ZuStructFacet(Prefixed);
ZiFBConfig(Prefixed, ZiFB_SizePrefixed<>);

namespace State {
  ZtEnum(, State, int8_t, Idle, Ready);
  ZiFB::AsEnum<"test.State"> ZiFB_Fmt(Map *);
}
namespace Perms {
  ZtFlags(, Perms, uint8_t, Read, Write, Admin);
  ZiFB::AsEnum<"test.Perms"> ZiFB_Fmt(Map *);
}
ZtEnumImplNS(State);
ZtEnumImplNS(Perms);

struct Root;
struct Child { int value; const Root *parent; };
struct Root {
  ZuCSpan name;
  const Child *child;
  bool enabled;
  int8_t state, unknown;
  ZuSpan<const int8_t> states;
  uint8_t perms;
};
ZfStruct(, (Child, FB),
  (value,	(Deflt<-42>),	Int32),
  (parent,,			UDT));
ZfStruct(, (Root, FB),
  (name,			(Required),				String),
  ((retired, Skip, int),	(Deflt<7>),				Int32),
  (child,,								UDT),
  (enabled,			(Deflt<true>),				Bool),
  (state,			(Deflt<1>, Enum<State::Map>),		Int8),
  (unknown,			(Deflt<-7>, Enum<State::Map>),		Int8),
  (states,			(Enum<State::Map>, FB::Align<32>),	Int8Vec),
  (perms,			(Deflt<3>, Flags<Perms::Map>),		UInt8));
ZiFB::AsTable<"test.Child"> ZiFB_Fmt(Child *);
ZiFB::AsTable<"test.Root"> ZiFB_Fmt(Root *);
ZuStructReflect(Root, ZuFacet::Prefixed, ZuFacet::FB);
ZuStructReflect(Child, ZuFacet::Prefixed, ZuFacet::FB);

ZuDerive(Text, (ZtString<ZtStringHeapID<"zifbinterop.Text">>));

// Independent canonical declaration, used only by the interop test.
static constexpr auto fbs =
  "namespace test;"
  "enum State:byte { Idle, Ready }"
  "enum Perms:ubyte(bit_flags) { Read, Write, Admin }"
  "table Child { value:int=-42; parent:Root; }"
  "table Root { name:string(required); retired:int=7(deprecated);"
  " child:Child; enabled:bool=true; state:State=Ready; unknown:byte=-7;"
  " states:[State](force_align:32); perms:Perms=3; }"
  "root_type Root;";

template <typename T>
static void compareAlign(const T &a, const T &b)
{
  ZuTestScopeRT(compareAlign);
  auto x = a.attributes() ? a.attributes()->LookupByKey("force_align") : nullptr;
  auto y = b.attributes() ? b.attributes()->LookupByKey("force_align") : nullptr;
  ZuCheckRT(bool(x) == bool(y));
  if (x && y) ZuCheckRT(x->value()->string_view() == y->value()->string_view());
}

static void compare(const reflection::Schema &a, const reflection::Schema &b)
{
  ZuTestScopeRT(compare);
  ZuCheckRT(a.objects()->size() == b.objects()->size());
  ZuCheckRT(a.enums()->size() == b.enums()->size());
  ZuCheckRT(a.advanced_features() == b.advanced_features());
  ZuCheckRT(a.root_table()->name()->string_view() == b.root_table()->name()->string_view());
  for (unsigned i = 0; i < a.objects()->size(); ++i) {
    auto x = a.objects()->Get(i);
    auto y = b.objects()->LookupByKey(x->name()->c_str());
    ZuCheckRT(y);
    if (!y) continue;
    ZuTestCallRT(compareAlign, *x, *y);
    ZuCheckRT(x->is_struct() == y->is_struct());
    ZuCheckRT(x->bytesize() == y->bytesize());
    ZuCheckRT(x->minalign() == y->minalign());
    ZuCheckRT(x->fields()->size() == y->fields()->size());
    for (unsigned j = 0; j < x->fields()->size(); ++j) {
      auto f = x->fields()->Get(j);
      auto g = y->fields()->LookupByKey(f->name()->c_str());
      ZuCheckRT(g);
      if (!g) continue;
      ZuTestCallRT(compareAlign, *f, *g);
      ZuCheckRT(f->id() == g->id() && f->offset() == g->offset());
      ZuCheckRT(f->default_integer() == g->default_integer());
      ZuCheckRT(f->default_real() == g->default_real() ||
	(ZuFP<double>::nan(f->default_real()) && ZuFP<double>::nan(g->default_real())));
      ZuCheckRT(f->deprecated() == g->deprecated());
      ZuCheckRT(f->required() == g->required());
      ZuCheckRT(f->optional() == g->optional());
      ZuCheckRT(f->padding() == g->padding());
      ZuCheckRT(f->type()->base_type() == g->type()->base_type());
      ZuCheckRT(f->type()->element() == g->type()->element());
      ZuCheckRT(f->type()->index() == g->type()->index());
      ZuCheckRT(f->type()->fixed_length() == g->type()->fixed_length());
      ZuCheckRT(f->type()->base_size() == g->type()->base_size());
      ZuCheckRT(f->type()->element_size() == g->type()->element_size());
    }
  }
  for (unsigned i = 0; i < a.enums()->size(); ++i) {
    auto x = a.enums()->Get(i);
    auto y = b.enums()->LookupByKey(x->name()->c_str());
    ZuCheckRT(y);
    if (!y) continue;
    ZuCheckRT(x->is_union() == y->is_union());
    auto xf = x->attributes() ? x->attributes()->LookupByKey("bit_flags") : nullptr;
    auto yf = y->attributes() ? y->attributes()->LookupByKey("bit_flags") : nullptr;
    ZuCheckRT(bool(xf) == bool(yf));
    if (xf && yf) ZuCheckRT(xf->value()->string_view() == yf->value()->string_view());
    ZuCheckRT(x->underlying_type()->base_type() == y->underlying_type()->base_type());
    ZuCheckRT(x->underlying_type()->index() == y->underlying_type()->index());
    ZuCheckRT(x->underlying_type()->base_size() == y->underlying_type()->base_size());
    ZuCheckRT(x->values()->size() == y->values()->size());
    for (unsigned j = 0; j < x->values()->size(); ++j) {
      auto f = x->values()->Get(j);
      auto g = y->values()->LookupByKey(f->value());
      ZuCheckRT(g);
      if (!g) continue;
      ZuCheckRT(f->name()->string_view() == g->name()->string_view());
      ZuCheckRT(f->union_type()->base_type() == g->union_type()->base_type());
      ZuCheckRT(f->union_type()->index() == g->union_type()->index());
      ZuCheckRT(f->union_type()->base_size() == g->union_type()->base_size());
      ZuCheckRT(f->union_type()->element_size() == g->union_type()->element_size());
    }
  }
}

struct Record { unsigned seq; };
ZfStruct(, (Record, FB), (seq, (Deflt<0>), UInt32));
ZiFB::AsTable<"test.Record"> ZiFB_Fmt(Record *);
struct Tail { uint16_t value; };
ZfStruct(, (Tail, FB), (value, (Deflt<23>, FB::Align<16>), UInt16));
ZiFB::AsStruct<"test.ZTail"> ZiFB_Fmt(Tail *);
struct Quad { uint8_t bytes[4]; Tail tail; };
ZfStruct(, (Quad, FB),
  (bytes,,	Bytes),
  (tail,,	UDT));
ZiFB::AsStruct<"test.Quad"> ZiFB_Fmt(Quad *);
ZuDerive(Body, (ZuUnion<void, Record, Record, Quad>));
ZiFB::AsUnion<"test.Body", ZiFB::Alt<"Replication", Record>,
  ZiFB::Alt<"Recovery", Record>, ZiFB::Alt<"Quad", Quad>> ZiFB_Fmt(Body *);
struct Message { Body body; unsigned seq; };
ZfStruct(, (Message, FB),
  ((retired, Skip, Body),,					UDT),
  (body,			(Required, FB::ID<"value">),	UDT),
  (seq,				(Deflt<0>),			UInt32));
ZiFB::AsTable<"test.Message"> ZiFB_Fmt(Message *);

static void unions()
{
  ZuTestScope(unions);
  flatbuffers::Parser canonical;
  canonical.opts.lang_to_generate = flatbuffers::IDLOptions::kCpp;
  canonical.opts.binary_schema_builtins = true;
  bool parsed = canonical.Parse(
    "namespace test;"
    "table Record { seq:uint; }"
    "struct ZTail(force_align:16) { value:ushort; }"
    "struct Quad { bytes:[ubyte:4]; tail:ZTail; }"
    "union Body { Replication:Record, Recovery:Record, Quad }"
    "table Message { retired:Body(deprecated); value:Body(required); seq:uint; }"
    "root_type Message;");
  ZuCheck(parsed);
  if (!parsed) { log(canonical.error_); return; }
  canonical.Serialize();
  auto expected = reflection::GetSchema(canonical.builder_.GetBufferPointer());

  auto bytes = ZtScratch(Text, ZiFB::SchemaOut::Schema<Message>::Size);
  ZiFB::saveSchema<Message>(bytes);
  ZuBSpan span{bytes};
  flatbuffers::Verifier verifier{span.data(), span.length()};
  bool valid = reflection::VerifySchemaBuffer(verifier);
  ZuCheck(valid);
  if (!valid) return;
  ZuTestCall(compare, *reflection::GetSchema(span.data()), *expected);
  auto reflected = ZiFB::view<ZiFB::Reflect::Schema>(span);
  ZuCheck(ZiFB::Reflect::verifySchema(span));
  flatbuffers::Parser imported;
  bool importedOK = imported.Deserialize(span.data(), span.length());
  ZuCheck(importedOK);
  if (!importedOK) { log(imported.error_); return; }

  auto source = ZtScratch(Text, 0);
  ZiFB::saveFBS<Message>(source);
  flatbuffers::Parser exported;
  exported.opts.lang_to_generate = flatbuffers::IDLOptions::kCpp;
  exported.opts.binary_schema_builtins = true;
  bool sourceOK = exported.Parse(source.data());
  ZuCheck(sourceOK);
  if (!sourceOK) { log(exported.error_); return; }
  exported.Serialize();
  ZuTestCall(compare, *expected, *reflection::GetSchema(exported.builder_.GetBufferPointer()));

  auto buf = ZtScratch(ZiFB::Buffer, 0);
  ZiFB::ArrayStorage<32> storage{buf};
  ZiFB::Builder<decltype(storage)> builder{storage, 32};
  Message message;
  message.seq = 17;
  message.body.p<2>(Record{42});
  auto out = builder.finish(ZiFB::save(builder, message));
  ZuCheck(flatbuffers::Verify(*expected, *expected->root_table(), out.data(), out.length()));
  ZuCheck(ZiFB::Reflect::verify(reflected, out));
  builder.clear();
  message.body.p<3>(Quad{{1, 2, 3, 4}, {123}});
  out = builder.finish(ZiFB::save(builder, message));
  ZuCheck(flatbuffers::Verify(*expected, *expected->root_table(), out.data(), out.length()));
  ZuCheck(ZiFB::Reflect::verify(reflected, out));
  bool jsonOK = imported.ParseJson(
    "{\"value_type\":\"Recovery\",\"value\":{\"seq\":99},\"seq\":23}");
  ZuCheck(jsonOK);
  if (!jsonOK) { log(imported.error_); return; }
  ZuBSpan incoming{imported.builder_.GetBufferPointer(), imported.builder_.GetSize()};
  ZuCheck(ZiFB::verify<Message>(incoming));
  ZuCheck(ZiFB::Reflect::verify(reflected, incoming));
  auto body = ZiFB::view<Message>(incoming).get<ZfField(Message, body)>();
  ZuCheck(body.tag() == 2 && body.get<2>().get<ZfField(Record, seq)>() == 99);
}

struct Bitmaps { ZuBitmap<65> fixed; ZmBitmap hw; ZtBitmap dynamic; };
ZfStruct(, (Bitmaps, FB),
  (fixed,	(Ctor<0>),	UDT),
  (hw,		(Ctor<1>),	UDT),
  (dynamic,	(Ctor<2>),	UDT));
ZiFB::AsTable<"test.Bitmaps"> ZiFB_Fmt(Bitmaps *);

static void bitmaps()
{
  ZuTestScope(bitmaps);
  flatbuffers::Parser canonical;
  bool parsed = canonical.Parse(
    "namespace Zf; table Bitmap { data:[ulong]; }"
    "namespace test; table Bitmaps { fixed:Zf.Bitmap; hw:Zf.Bitmap; dynamic:Zf.Bitmap; }"
    "root_type Bitmaps;");
  ZuCheck(parsed);
  if (!parsed) { log(canonical.error_); return; }
  canonical.Serialize();
  auto schema = reflection::GetSchema(canonical.builder_.GetBufferPointer());
  auto schemaBytes = ZtScratch(Text, 0);
  ZiFB::saveSchema<Bitmaps>(schemaBytes);
  ZuBSpan schemaSpan{schemaBytes};
  flatbuffers::Verifier verifier{schemaSpan.data(), schemaSpan.length()};
  bool valid = reflection::VerifySchemaBuffer(verifier);
  ZuCheck(valid);
  if (!valid) return;
  ZuTestCall(compare, *schema, *reflection::GetSchema(schemaSpan.data()));
  Bitmaps row;
  row.fixed.set(0).set(64);
  row.hw.set(0);
  row.dynamic.set(130);
  auto bytes = ZtScratch(Text, 0);
  ZuCheck(ZiFB::save(bytes, row) == ZiFB::Error::OK);
  ZuBSpan span{bytes};
  ZuCheck(flatbuffers::Verify(*schema, *schema->root_table(), span.data(), span.length()));

  bool jsonOK = canonical.ParseJson(
    "{\"fixed\":{\"data\":[1,1]},\"hw\":{\"data\":[4294967296,1]},"
    "\"dynamic\":{\"data\":[0,0,4]}}");
  ZuCheck(jsonOK);
  if (!jsonOK) { log(canonical.error_); return; }
  ZuBSpan incoming{canonical.builder_.GetBufferPointer(), canonical.builder_.GetSize()};
  ZuCheck(ZiFB::verify<Bitmaps>(incoming));
  auto decoded = ZiFB::load<Bitmaps>(incoming);
  ZuCheck(decoded.fixed.get(0) && decoded.fixed.get(64));
  ZuCheck(decoded.hw.get(32) && decoded.hw.get(64) && !decoded.hw.get(0));
  ZuCheck(decoded.dynamic.get(130));
}

struct Values {
  int128_t signedValue;
  uint128_t unsignedValue;
  ZuFixed fixed;
  ZuDecimal decimal;
  ZuTime time;
  ZuDateTime date;
  ZuSpan<const int128_t> numbers;
  ZuSpan<const ZuBSpan> bytes;
};
ZfStruct(, (Values, FB),
  (signedValue,,	Int128),
  (unsignedValue,,	UInt128),
  (fixed,,		Fixed),
  (decimal,,		Decimal),
  (time,,		Time),
  (date,,		DateTime),
  (numbers,,		Int128Vec),
  (bytes,,		BytesVec));
ZiFB::AsTable<"test.Values"> ZiFB_Fmt(Values *);

static void values()
{
  ZuTestScope(values);
  namespace R = ZiFB::Reflect;
  flatbuffers::Parser canonical;
  bool parsed = canonical.Parse(
    "namespace Zf;"
    "struct Int128 { h:ulong; l:ulong; }"
    "struct UInt128 { h:ulong; l:ulong; }"
    "struct Fixed { mantissa:long; ndp:ubyte; }"
    "struct Decimal { h:ulong; l:ulong; }"
    "struct Time { sec:long; nsec:int; }"
    "struct DateTime { julian:int; sec:int; nsec:int; }"
    "table Bytes { data:[ubyte]; }"
    "namespace test; table Values { signed_value:Zf.Int128;"
    " unsigned_value:Zf.UInt128; fixed:Zf.Fixed; decimal:Zf.Decimal;"
    " time:Zf.Time; date:Zf.DateTime; numbers:[Zf.Int128]; bytes:[Zf.Bytes]; }"
    "root_type Values;");
  ZuCheck(parsed);
  if (!parsed) { log(canonical.error_); return; }
  canonical.Serialize();
  auto schemaBytes = ZtScratch(Text, 0);
  ZiFB::saveSchema<Values>(schemaBytes);
  ZuBSpan schemaSpan{schemaBytes};
  flatbuffers::Verifier schemaVerifier{schemaSpan.data(), schemaSpan.length()};
  ZuCheck(reflection::VerifySchemaBuffer(schemaVerifier));
  auto schema = reflection::GetSchema(schemaSpan.data());
  auto expected = reflection::GetSchema(canonical.builder_.GetBufferPointer());
  ZuTestCall(compare, *schema, *expected);
  auto reflected = ZiFB::view<R::Schema>(schemaSpan);
  ZuCheck(R::verifySchema(schemaSpan));
  auto source = ZtScratch(Text, 0);
  ZiFB::saveFBS<Values>(source);
  flatbuffers::Parser exported;
  parsed = exported.Parse(source.data());
  ZuCheck(parsed);
  if (!parsed) { log(exported.error_); return; }
  exported.Serialize();
  ZuTestCall(compare, *schema, *reflection::GetSchema(exported.builder_.GetBufferPointer()));

  const int128_t numbers[] = {7, int128_t(1) << 64};
  const uint8_t bytes[] = {0, 255};
  const ZuBSpan blobs[] = {bytes, {}};
  Values row{-1, (uint128_t(1) << 64) | 2,
    ZuFixed{1234, 2}, ZuDecimal{ZuDecimal::Unscaled{42}},
    ZuTime{1700000000, 123}, ZuDateTime{ZuDateTime::Julian{2460000}, 43210, 987},
    numbers, blobs};
  auto payload = ZtScratch(Text, 0);
  ZuCheck(ZiFB::save(payload, row) == ZiFB::Error::OK);
  ZuBSpan out{payload};
  ZuCheck(flatbuffers::Verify(*expected, *expected->root_table(), out.data(), out.length()));
  auto table = flatbuffers::GetRoot<flatbuffers::Table>(out.data());
  auto timeField = expected->root_table()->fields()->LookupByKey("time");
  auto timeObject = expected->objects()->Get(timeField->type()->index());
  auto time = flatbuffers::GetFieldStruct(*table, *timeField);
  ZuCheck(flatbuffers::GetAnyFieldI(*time, *timeObject->fields()->LookupByKey("sec")) ==
    1700000000);
  ZuCheck(flatbuffers::GetAnyFieldI(*time, *timeObject->fields()->LookupByKey("nsec")) == 123);

  // Independent canonical values exercise signed high/low words, scale,
  // subsecond units, inline vectors, and empty versus nonempty byte wrappers.
  parsed = canonical.ParseJson(
    "{\"signed_value\":{\"h\":18446744073709551615,\"l\":18446744073709551615},"
    "\"unsigned_value\":{\"h\":1,\"l\":2},\"fixed\":{\"mantissa\":1234,\"ndp\":2},"
    "\"decimal\":{\"h\":0,\"l\":42},\"time\":{\"sec\":1700000000,\"nsec\":123},"
    "\"date\":{\"julian\":2460000,\"sec\":43210,\"nsec\":987},"
    "\"numbers\":[{\"h\":0,\"l\":7},{\"h\":1,\"l\":0}],"
    "\"bytes\":[{\"data\":[0,255]},{\"data\":[]}]}");
  ZuCheck(parsed);
  if (!parsed) { log(canonical.error_); return; }
  ZuBSpan incoming{canonical.builder_.GetBufferPointer(), canonical.builder_.GetSize()};
  ZuCheck(ZiFB::verify<Values>(incoming) && R::verify(reflected, incoming));
  auto view = ZiFB::view<Values>(incoming);
  ZuCheck(view.get<ZfField(Values, signedValue)>() == row.signedValue);
  ZuCheck(view.get<ZfField(Values, unsignedValue)>() == row.unsignedValue);
  ZuCheck(view.get<ZfField(Values, fixed)>() == row.fixed);
  ZuCheck(view.get<ZfField(Values, decimal)>() == row.decimal);
  ZuCheck(view.get<ZfField(Values, time)>() == row.time);
  ZuCheck(view.get<ZfField(Values, date)>() == row.date);
  auto vector = view.get<ZfField(Values, numbers)>();
  ZuCheck(vector.length() == 2 && vector[0] == 7 && vector[1] == numbers[1]);
  auto data = view.get<ZfField(Values, bytes)>();
  ZuCheck(data.length() == 2 && data[0] == blobs[0] && data[1].length() == 0);
}

struct Floating {
  float small;
  double absent, positive, negative, tiny;
  ZuSpan<const float> values;
};
ZfStruct(, (Floating, FB),
  (small,,					Float),
  (absent,,					Float),
  (positive,	(Deflt<ZuFP<double>::inf()>),	Float),
  (negative,	(Deflt<-ZuFP<double>::inf()>),	Float),
  (tiny,	(Deflt<0x1p-1074>),		Float),
  (values,,					FloatVec));
ZiFB::AsTable<"test.Floating"> ZiFB_Fmt(Floating *);

static void floating()
{
  ZuTestScope(floating);
  namespace R = ZiFB::Reflect;
  flatbuffers::Parser canonical;
  bool parsed = canonical.Parse(
    "namespace test; table Floating { small:float=nan; absent:double=nan;"
    "positive:double=inf; negative:double=-inf; tiny:double=0x1p-1074; values:[float]; }"
    "root_type Floating;");
  ZuCheck(parsed);
  if (!parsed) { log(canonical.error_); return; }
  canonical.Serialize();
  auto expected = reflection::GetSchema(canonical.builder_.GetBufferPointer());
  auto bfbs = ZtScratch(Text, 0);
  ZiFB::saveSchema<Floating>(bfbs);
  ZuBSpan schemaBytes{bfbs};
  flatbuffers::Verifier verifier{schemaBytes.data(), schemaBytes.length()};
  ZuCheck(reflection::VerifySchemaBuffer(verifier));
  ZuCheck(R::verifySchema(schemaBytes));
  auto schema = reflection::GetSchema(schemaBytes.data());
  ZuTestCall(compare, *schema, *expected);
  auto source = ZtScratch(Text, 0);
  ZiFB::saveFBS<Floating>(source);
  flatbuffers::Parser exported;
  parsed = exported.Parse(source.data());
  ZuCheck(parsed);
  if (!parsed) { log(exported.error_); return; }
  exported.Serialize();
  ZuTestCall(compare, *schema, *reflection::GetSchema(exported.builder_.GetBufferPointer()));

  const float values[] = {ZuFP<float>::nan(), ZuFP<float>::inf(), -ZuFP<float>::inf(), 1.5f};
  Floating row{ZuFP<float>::nan(), ZuFP<double>::nan(),
    ZuFP<double>::inf(), -ZuFP<double>::inf(), 0x1p-1074, values};
  auto payload = ZtScratch(Text, 0);
  ZuCheck(ZiFB::save(payload, row) == ZiFB::Error::OK);
  ZuBSpan out{payload};
  ZuCheck(flatbuffers::Verify(*expected, *expected->root_table(), out.data(), out.length()));
  auto table = flatbuffers::GetRoot<flatbuffers::Table>(out.data());
  auto fields = expected->root_table()->fields();
  ZuCheck(ZuFP<double>::nan(flatbuffers::GetAnyFieldF(*table, *fields->LookupByKey("absent"))));
  ZuCheck(flatbuffers::GetAnyFieldF(*table, *fields->LookupByKey("tiny")) == 0x1p-1074);
  auto vector = flatbuffers::GetFieldV<float>(*table, *fields->LookupByKey("values"));
  ZuCheck(vector->size() == 4 && ZuFP<float>::nan(vector->Get(0)) &&
    ZuFP<float>::inf(vector->Get(1)) && ZuFP<float>::inf(-vector->Get(2)) &&
      vector->Get(3) == 1.5f);

  parsed = canonical.ParseJson("{values:[nan,inf,-inf,1.5]}");
  ZuCheck(parsed);
  if (!parsed) { log(canonical.error_); return; }
  ZuBSpan incoming{canonical.builder_.GetBufferPointer(), canonical.builder_.GetSize()};
  ZuCheck(ZiFB::verify<Floating>(incoming));
  ZuCheck(R::verify(ZiFB::view<R::Schema>(schemaBytes), incoming));
  auto view = ZiFB::view<Floating>(incoming);
  ZuCheck(!view.has<ZfField(Floating, absent)>() &&
    ZuFP<double>::nan(view.get<ZfField(Floating, absent)>()));
  ZuCheck(ZuFP<float>::nan(view.get<ZfField(Floating, small)>()));
  ZuCheck(ZuFP<double>::inf(view.get<ZfField(Floating, positive)>()) &&
    ZuFP<double>::inf(-view.get<ZfField(Floating, negative)>()));
  ZuCheck(view.get<ZfField(Floating, tiny)>() == 0x1p-1074);
  auto v = view.get<ZfField(Floating, values)>();
  ZuCheck(v.length() == 4 && ZuFP<float>::nan(v[0]) &&
    ZuFP<float>::inf(v[1]) && ZuFP<float>::inf(-v[2]) && v[3] == 1.5f);
}

static void nested()
{
  ZuTestScope(nested);
  flatbuffers::Parser canonical;
  bool parsed = canonical.Parse(
    "namespace test; table Record { seq:uint; }"
    "table Envelope { body:[ubyte](nested_flatbuffer:\"Record\"); outside:Record; }"
    "root_type Envelope;");
  ZuCheck(parsed);
  if (!parsed) { log(canonical.error_); return; }
  canonical.Serialize();
  auto schema = reflection::GetSchema(canonical.builder_.GetBufferPointer());
  auto record = schema->objects()->LookupByKey("test.Record");
  auto buf = ZtScratch(ZiFB::Buffer, 0);
  ZiFB::ArrayStorage<> storage{buf};
  ZiFB::Builder<decltype(storage)> builder{storage};
  auto outside = ZiFB::save(builder, Record{7});
  auto state = builder.startNested();
  auto inner = builder.startTable();
  builder.field<uint32_t>(inner, 0, 0, 0, true);
  auto body = builder.endNested(state, builder.endTable(inner));
  auto envelope = builder.startTable();
  builder.fieldOffset(envelope, 0, body);
  builder.fieldOffset(envelope, 1, outside);
  auto out = builder.finish(builder.endTable(envelope));
  ZuCheck(flatbuffers::Verify(*schema, *schema->root_table(), out.data(), out.length()));
  auto extracted = ZiFB::root(out).vector(0).bytes();
  ZuCheck(flatbuffers::Verify(*schema, *record, extracted.data(), extracted.length()));
  auto saved = ZtScratch(Text, extracted.length());
  saved << ZuCSpan{extracted};
  auto copyBuf = ZtScratch(ZiFB::Buffer, 0);
  ZiFB::ArrayStorage<> copyStorage{copyBuf};
  ZiFB::Builder<decltype(copyStorage)> copy{copyStorage};
  auto copiedBody = copy.copyRoot(extracted, 8);
  auto copiedEnvelope = copy.startTable();
  copy.fieldOffset(copiedEnvelope, 0, copiedBody);
  auto copied = ZiFB::root(copy.finish(copy.endTable(copiedEnvelope))).vector(0).bytes();
  memset(builder.data(), 0, builder.size()); // prove independence from original storage
  ZuCheck(copied == ZuBSpan{saved});
  ZuCheck(flatbuffers::Verify(*schema, *record, copied.data(), copied.length()));
  ZuCheck(ZiFB::verify<Record>(copied));
  ZuCheck(ZiFB::view<Record>(copied).has<ZfField(Record, seq)>());

  flatbuffers::FlatBufferBuilder other;
  other.ForceDefaults(true);
  auto start = other.StartTable();
  other.AddElement<uint32_t>(4, 0, 0);
  other.Finish(flatbuffers::Offset<flatbuffers::Table>{other.EndTable(start)});
  ZuBSpan incoming{other.GetBufferPointer(), other.GetSize()};
  ZuCheck(ZiFB::verify<Record>(incoming));
  copy.clear();
  copiedBody = copy.copyRoot(incoming, 8);
  copiedEnvelope = copy.startTable();
  copy.fieldOffset(copiedEnvelope, 0, copiedBody);
  copied = ZiFB::root(copy.finish(copy.endTable(copiedEnvelope))).vector(0).bytes();
  ZuCheck(copied == incoming && ZiFB::view<Record>(copied).has<ZfField(Record, seq)>());
}

struct Evolved { ZuCSpan tail; unsigned seq; int changed; };
ZfStruct(, (Evolved, FB),
  (tail,			(Ctor<0>, FB::FieldID<3>),			String),
  (seq,				(Ctor<1>, FB::FieldID<0>, Deflt<0>, Keys<0>),	UInt32),
  ((retired, Skip, int),	(FB::FieldID<1>, Deflt<9>),			Int32),
  (changed,			(Ctor<2>, FB::FieldID<2>, Deflt<7>, Mutable),	Int32));
ZiFB::AsTable<"test.Evolved"> ZiFB_Fmt(Evolved *);

static void evolution()
{
  ZuTestScope(evolution);
  namespace R = ZiFB::Reflect;
  flatbuffers::Parser canonical;
  bool parsed = canonical.Parse(
    "namespace test; table Evolved { seq:uint; retired:int=9(deprecated);"
    " changed:int=7; tail:string; } root_type Evolved;");
  ZuCheck(parsed);
  if (!parsed) { log(canonical.error_); return; }
  canonical.Serialize();
  auto schema = reflection::GetSchema(canonical.builder_.GetBufferPointer());
  auto bytes = ZtScratch(Text, 0);
  ZiFB::saveSchema<Evolved>(bytes);
  ZuBSpan bfbs{bytes};
  flatbuffers::Verifier verifier{bfbs.data(), bfbs.length()};
  ZuCheck(reflection::VerifySchemaBuffer(verifier));
  ZuTestCall(compare, *reflection::GetSchema(bfbs.data()), *schema);
  auto reflected = ZiFB::view<R::Schema>(bfbs);

  // A canonical peer retains the deprecated slot as live and adds a new
  // field. Receiving this is ordinary FlatBuffers evolution, not a promise
  // to retain the repo's former application schemas or generated APIs.
  flatbuffers::Parser peer;
  parsed = peer.Parse(
    "namespace test; table Evolved { seq:uint; retired:int=9;"
    " changed:int=7; tail:string; extra:ulong; } root_type Evolved;");
  ZuCheck(parsed);
  if (!parsed) { log(peer.error_); return; }
  parsed = peer.ParseJson(
    "{\"seq\":13,\"retired\":99,\"changed\":23,\"tail\":\"peer\",\"extra\":77}");
  ZuCheck(parsed);
  if (!parsed) { log(peer.error_); return; }
  ZuBSpan incoming{peer.builder_.GetBufferPointer(), peer.builder_.GetSize()};
  ZuCheck(ZiFB::verify<Evolved>(incoming) && R::verify(reflected, incoming));
  auto loaded = ZiFB::load<Evolved>(incoming);
  ZuCheck(loaded.seq == 13 && loaded.changed == 23 && loaded.tail == "peer");
  auto view = ZiFB::view<Evolved>(incoming);
  ZuCheck(ZiFB::root(incoming).has(1) && ZiFB::root(incoming).has(4));
  auto mutableTable = flatbuffers::GetMutableRoot<flatbuffers::Table>(
    peer.builder_.GetBufferPointer());
  ZuCheck(mutableTable->SetField<int32_t>(8, 7)); // retains the present default
  ZuCheck(view.has<ZfField(Evolved, changed)>() && view.get<ZfField(Evolved, changed)>() == 7);
  auto seq = view.field<ZfField(Evolved, seq)>();
  ZiFB::write<uint32_t>(peer.builder_.GetBufferPointer() + (seq - incoming.data()), 17);
  ZuCheck(mutableTable->GetField<uint32_t>(4, 0) == 17);
  ZuCheck(flatbuffers::Verify(*schema, *schema->root_table(), incoming.data(),
    incoming.length()));

  auto update = ZtScratch(Text, 0);
  Evolved row{"not selected", 29, 31};
  ZuCheck((ZiFB::save<ZuFacet::FB, ZfFieldFilter::Upd>(update, row) ==
    ZiFB::Error::OK));
  ZuBSpan out{update};
  ZuCheck(flatbuffers::Verify(*schema, *schema->root_table(), out.data(), out.length()));
  auto table = flatbuffers::GetRoot<flatbuffers::Table>(out.data());
  ZuCheck(table->GetField<uint32_t>(4, 0) == 29 && table->GetField<int32_t>(8, 7) == 31);
  ZuCheck(!table->CheckField(6) && !table->CheckField(10));
  update.clear();
  ZuCheck((ZiFB::save<ZuFacet::FB, ZfFieldFilter::Del>(update, row) ==
    ZiFB::Error::OK));
  out = update;
  table = flatbuffers::GetRoot<flatbuffers::Table>(out.data());
  ZuCheck(table->GetField<uint32_t>(4, 0) == 29 && !table->CheckField(8));
  loaded = ZiFB::load<Evolved>(out);
  ZuCheck(loaded.seq == 29 && loaded.changed == 7 && !loaded.tail);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(unions);
  ZuTestCall(bitmaps);
  ZuTestCall(values);
  ZuTestCall(floating);
  ZuTestCall(nested);
  ZuTestCall(evolution);
  namespace R = ZiFB::Reflect;
  flatbuffers::Parser canonical;
  canonical.opts.binary_schema_builtins = true;
  bool parsed = canonical.Parse(fbs);
  ZuCheck(parsed);
  if (!parsed) { log(canonical.error_); return 1; }
  canonical.Serialize();
  auto canonicalBytes = ZuBSpan{canonical.builder_.GetBufferPointer(),
    canonical.builder_.GetSize()};
  ZuCheck(ZiFB::verify<R::Schema>(canonicalBytes, "BFBS"));
  ZuCheck(R::verifySchema(canonicalBytes));
  auto canonicalSchema = reflection::GetSchema(canonicalBytes.data());

  auto schemaBytes = ZtScratch(Text, ZiFB::SchemaOut::Schema<Root>::Size);
  ZiFB::saveSchema<Root>(schemaBytes);
  ZuBSpan span{schemaBytes};
  flatbuffers::Verifier verifier{span.data(), span.length()};
  bool valid = reflection::VerifySchemaBuffer(verifier);
  ZuCheck(valid);
  if (!valid) return 1;
  auto schema = reflection::GetSchema(span.data());
  auto reflected = ZiFB::view<R::Schema>(span);
  ZuCheck(R::verifySchema(span));
  ZuTestCall(compare, *schema, *canonicalSchema);
  auto source = ZtScratch(Text, 0);
  ZiFB::saveFBS<Root>(source);
  flatbuffers::Parser exported;
  exported.opts.binary_schema_builtins = true;
  bool sourceOK = exported.Parse(source.data());
  ZuCheck(sourceOK);
  if (!sourceOK) { log(exported.error_); return 1; }
  exported.Serialize();
  ZuTestCall(compare, *schema, *reflection::GetSchema(exported.builder_.GetBufferPointer()));
  flatbuffers::Parser imported;
  bool importedOK = imported.Deserialize(span.data(), span.length());
  ZuCheck(importedOK);
  if (!importedOK) { log(imported.error_); return 1; }

  Child child{17, nullptr};
  const int8_t states[] = {State::Idle, State::Ready};
  Root row{"native", &child, true, State::Ready, -7, states, 3};
  Child leaf{29, nullptr};
  Root parent{"parent", &leaf, true, State::Idle, -7, {}, 3};
  child.parent = &parent;
  auto buf = ZtScratch(ZiFB::Buffer, 0);
  ZiFB::ArrayStorage<32> storage{buf};
  ZiFB::Builder<decltype(storage)> builder{storage, 32};
  auto output = builder.finish(ZiFB::save(builder, row));
  ZuCheck(!builder.failed());
  auto statesView = ZiFB::view<Root>(output).get<ZfField(Root, states)>();
  ZuCheck(!(uintptr_t(statesView.data()) & 31));
  ZuCheck(flatbuffers::Verify(*canonicalSchema, *canonicalSchema->root_table(),
    output.data(), output.length()));
  ZuCheck(R::verify(reflected, output));
  ZuCheck(R::verify(ZiFB::view<R::Schema>(canonicalBytes), output));
  auto nativeParent = ZiFB::view<Root>(output).get<ZfField(Root, child)>()
    .get<ZfField(Child, parent)>();
  ZuCheck(nativeParent.get<ZfField(Root, name)>() == "parent");
  ZuCheck(nativeParent.get<ZfField(Root, child)>().get<ZfField(Child, value)>() == 29);
  auto nameField = R::field(R::rootObject(reflected), ZuCSpan{"name"});
  ZuCheck(R::string(ZiFB::root(output), nameField) == "native");
  auto seqField = R::field(R::object(reflected, ZuCSpan{"test.Child"}), 0U);
  auto childField = R::field(R::rootObject(reflected), ZuCSpan{"child"});
  auto childTable = ZiFB::TableView{R::object(reflected, ZiFB::root(output), childField)};
  ZuCheck(R::scalar<int>(childTable, seqField) == 17);
  auto table = flatbuffers::GetAnyRoot(output.data());
  auto name = canonicalSchema->root_table()->fields()->LookupByKey("name");
  ZuCheck(flatbuffers::GetFieldS(*table, *name)->string_view() == "native");

  auto prefixed = ZtScratch(Text, 0);
  ZuCheck(ZiFB::save<ZuFacet::Prefixed>(prefixed, row) == ZiFB::Error::OK);
  ZuBSpan prefixedBytes{prefixed};
  ZuCheck(flatbuffers::VerifySizePrefixed(*canonicalSchema, *canonicalSchema->root_table(),
    prefixedBytes.data(), prefixedBytes.length()));
  ZuCheck(R::verify<ZuFacet::Prefixed>(reflected, prefixedBytes));

  bool jsonOK = imported.ParseJson("{\"name\":\"canonical\",\"child\":{\"value\":23,"
    "\"parent\":{\"name\":\"parent\",\"child\":{\"value\":29}}}}");
  ZuCheck(jsonOK);
  if (!jsonOK) { log(imported.error_); return 1; }
  ZuBSpan incoming{imported.builder_.GetBufferPointer(), imported.builder_.GetSize()};
  ZuCheck(ZiFB::verify<Root>(incoming));
  ZuCheck(R::verify(reflected, incoming));
  auto view = ZiFB::view<Root>(incoming);
  ZuCheck(view.get<ZfField(Root, name)>() == "canonical");
  ZuCheck(view.get<ZfField(Root, enabled)>());
  ZuCheck(view.get<ZfField(Root, state)>() == State::Ready);
  ZuCheck(view.get<ZfField(Root, unknown)>() == -7);
  ZuCheck(view.get<ZfField(Root, perms)>() == 3);
  ZuCheck(view.get<ZfField(Root, child)>().get<ZfField(Child, value)>() == 23);
  auto parentView = view.get<ZfField(Root, child)>().get<ZfField(Child, parent)>();
  ZuCheck(parentView.get<ZfField(Root, name)>() == "parent");
  ZuCheck(parentView.get<ZfField(Root, child)>().get<ZfField(Child, value)>() == 29);
  imported.opts.size_prefixed = true;
  jsonOK = imported.ParseJson("{\"name\":\"prefixed\",\"states\":[\"Ready\",\"Idle\"]}");
  ZuCheck(jsonOK);
  if (!jsonOK) { log(imported.error_); return 1; }
  incoming = {imported.builder_.GetBufferPointer(), imported.builder_.GetSize()};
  ZuCheck((ZiFB::verify<Root, ZuFacet::Prefixed>(incoming)));
  ZuCheck(R::verify<ZuFacet::Prefixed>(reflected, incoming));
  auto prefixedView = ZiFB::view<Root, ZuFacet::Prefixed>(incoming);
  ZuCheck(prefixedView.get<ZfField(Root, name)>() == "prefixed");
  ZuCheck(prefixedView.get<ZfField(Root, states)>()[0] == State::Ready);

  auto bad = ZtScratch(Text, output.length());
  bad << ZuCSpan{output};
  ZiFB::write<ZiFB::Offset>(bad.data(), 0);
  ZuCheck(!R::verify(reflected, bad));
}
