//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <flatbuffers/idl.h>
#include <flatbuffers/reflection.h>
#include "maps_generated.h"

#include <zlib/ZuPtr.hh>
#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmHash.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmRBTree.hh>
#include <zlib/ZmRef.hh>
#include <zlib/ZtArray.hh>
#include <zlib/ZtString.hh>
#include <zlib/ZfbReflect.hh>

using namespace ZuTestUtil;

namespace Native {

struct Ints_ : public ZmNHeap<Ints_>,
  public ZmHashKV<int, int, ZmHashHeapID<"zfbmapinterop.Ints">> {
friend inline ZuStringT<"zfbmapinterop.Ints.Obj"> ZmNHeapID(Ints_ *);

};

ZuDerive(Ints, Ints_);
Zfb::AsMap<"maps.IntMap", ZfFieldTC::Int32, ZfFieldTC::Int32,
  ZuTypeList<>, ZuTypeList<ZuFieldProp::Deflt<-11>>> Zfb_Fmt(Ints *);
ZuDerive(Texts, (ZmRBTreeKV<ZtString<>, ZtString<>,
  ZmRBTreeHeapID<"zfbmapinterop.Texts">>));
Zfb::AsMap<"maps.TextMap", ZfFieldTC::String, ZfFieldTC::String> Zfb_Fmt(Texts *);

struct Item_ : public ZmNHeap<Item_> {
friend inline ZuStringT<"zfbmapinterop.Item"> ZmNHeapID(Item_ *);

  int value;

  explicit Item_(int value_ = 17) : value{value_} { }
};

ZuDerive(Item, Item_);
ZfStruct(, (Item, FB), (value, (Ctor<0>, Deflt<17>), Int32));
Zfb::AsTable<"maps.Item"> Zfb_Fmt(Item *);
ZuDerive(Inner, (ZmRBTreeKV<unsigned, ZuPtr<Item>,
  ZmRBTreeHeapID<"zfbmapinterop.Inner">>));
Zfb::AsMap<"maps.InnerMap", ZfFieldTC::UInt32, ZfFieldTC::UDT> Zfb_Fmt(Inner *);
ZuDerive(Outer, (ZmRBTreeKV<int64_t, Inner,
  ZmRBTreeHeapID<"zfbmapinterop.Outer">>));
Zfb::AsMap<"maps.OuterMap", ZfFieldTC::Int64, ZfFieldTC::UDT> Zfb_Fmt(Outer *);
struct Row { ZmRef<Ints> ints; Texts texts; Outer nested; };
ZfStruct(, (Row, FB),
  (ints, (Mutable), UDT), (texts, (Mutable), UDT), (nested, (Mutable), UDT));
Zfb::AsTable<"maps.Row"> Zfb_Fmt(Row *);

} // Native

template <typename T>
using Offsets = ZtArray<flatbuffers::Offset<T>, ZtArrayHeapID<"zfbmapinterop.Offsets">>;

static void exchange()
{
  ZuTestScope(exchange);
  Native::Row row{new Native::Ints(), {}, {}};
  row.ints->add(7, 70);
  row.ints->add(-3, -11);
  row.ints->add(0, 0);
  row.texts.add("z", "last");
  row.texts.add("", "empty");
  row.texts.add("a", "first");
  Native::Inner inner;
  inner.add(9, new Native::Item());
  inner.add(2, nullptr);
  row.nested.add(-42, ZuMv(inner));
  row.nested.add(42, Native::Inner{});
  Zfb::Buffer bytes;
  ZuCheck(Zfb::save(bytes, row) == Zfb::Error::OK);
  flatbuffers::Verifier verifier{bytes.data(), bytes.length()};
  ZuCheck(maps::VerifyRowBuffer(verifier));
  auto peer = maps::GetRow(bytes.data());
  ZuCheck(peer->ints()->size() == 3);
  ZuCheck(peer->ints()->Get(0)->key() == -3 && peer->ints()->Get(2)->key() == 7);
  ZuCheck(peer->ints()->LookupByKey(-3)->value() == -11);
  ZuCheck(peer->ints()->LookupByKey(7)->value() == 70 && !peer->ints()->LookupByKey(8));
  ZuCheck(peer->texts()->LookupByKey("")->value()->str() == "empty");
  ZuCheck(peer->texts()->LookupByKey("z")->value()->str() == "last");
  auto nested = peer->nested()->LookupByKey(-42)->value();
  ZuCheck(nested->size() == 2 && nested->LookupByKey(9)->value()->value() == 17);
  ZuCheck(!nested->LookupByKey(2)->value());
  ZuCheck(peer->nested()->LookupByKey(42)->value()->size() == 0);

  flatbuffers::FlatBufferBuilder builder;
  Offsets<maps::IntMapEntry> ints;
  ints.push(maps::CreateIntMapEntry(builder, 7, 700));
  ints.push(maps::CreateIntMapEntry(builder, -3, -11));
  auto intVec = builder.CreateVectorOfSortedTables(ints.data(), ints.length());
  Offsets<maps::TextMapEntry> texts;
  texts.push(maps::CreateTextMapEntryDirect(builder, "z", "canonical"));
  texts.push(maps::CreateTextMapEntryDirect(builder, "", ""));
  auto textVec = builder.CreateVectorOfSortedTables(texts.data(), texts.length());
  Offsets<maps::InnerMapEntry> inners;
  inners.push(maps::CreateInnerMapEntry(builder, 3, maps::CreateItem(builder, 33)));
  inners.push(maps::CreateInnerMapEntry(builder, 4));
  auto innerVec = builder.CreateVectorOfSortedTables(inners.data(), inners.length());
  Offsets<maps::OuterMapEntry> outers;
  outers.push(maps::CreateOuterMapEntry(builder, 12, innerVec));
  auto outerVec = builder.CreateVectorOfSortedTables(outers.data(), outers.length());
  maps::FinishRowBuffer(builder, maps::CreateRow(builder, intVec, textVec, outerVec));
  ZuBSpan incoming{builder.GetBufferPointer(), builder.GetSize()};
  ZuCheck(Zfb::verify<Native::Row>(incoming));
  auto out = Zfb::load<Native::Row>(incoming);
  ZuCheck(out.ints->findVal(7) == 700 && out.ints->findVal(-3) == -11);
  ZuCheck(out.texts.findVal("z") == "canonical" && out.texts.find(""));
  ZuCheck(out.nested.find(12)->val().find(3)->val()->value == 33);
  ZuCheck(!out.nested.find(12)->val().find(4)->val());

  ZuCheck(Zfb::save(bytes, *row.ints) == Zfb::Error::OK);
  flatbuffers::Verifier rootVerifier{bytes.data(), bytes.length()};
  ZuCheck(rootVerifier.VerifyBuffer<maps::IntMap>(nullptr));
  auto root = flatbuffers::GetRoot<maps::IntMap>(bytes.data());
  ZuCheck(root->entries()->LookupByKey(7)->value() == 70);
}

static void ordering()
{
  ZuTestScope(ordering);
  flatbuffers::FlatBufferBuilder builder;
  Offsets<maps::IntMapEntry> entries;
  entries.push(maps::CreateIntMapEntry(builder, 7, 70));
  entries.push(maps::CreateIntMapEntry(builder, -3, 30));
  entries.push(maps::CreateIntMapEntry(builder, 7, 700));
  builder.Finish(maps::CreateIntMap(builder,
    builder.CreateVector(entries.data(), entries.length())));
  ZuBSpan incoming{builder.GetBufferPointer(), builder.GetSize()};
  flatbuffers::Verifier verifier{incoming.data(), incoming.length()};
  ZuCheck(verifier.VerifyBuffer<maps::IntMap>(nullptr));
  ZuCheck(Zfb::verify<Native::Ints>(incoming));
  ZmRef<Native::Ints> out = new Native::Ints();
  out->add(99, 99);
  auto handler = Zfb::handler<Native::Ints>(incoming);
  handler.update(*out);
  ZuCheck(out->count_() == 3 && out->findVal(7) == 700 && out->findVal(99) == 99);
  handler.load(*out);
  ZuCheck(out->count_() == 2 && out->findVal(7) == 700 && out->findVal(-3) == 30);
  Zfb::Buffer bytes;
  ZuCheck(Zfb::save(bytes, *out) == Zfb::Error::OK);
  auto canonical = flatbuffers::GetRoot<maps::IntMap>(bytes.data());
  ZuCheck(canonical->entries()->LookupByKey(7)->value() == 700);
}

static void compareSchema(const reflection::Schema &schema, const reflection::Schema &compiled)
{
  ZuTestScopeRT(compareSchema);
  ZuCheckRT(compiled.objects()->size() == schema.objects()->size());
  for (auto object : *schema.objects()) {
    auto peer = compiled.objects()->LookupByKey(object->name()->c_str());
    ZuCheckRT(peer && peer->fields()->size() == object->fields()->size());
    if (!peer) continue;
    for (auto field : *object->fields()) {
      auto other = peer->fields()->LookupByKey(field->name()->c_str());
      ZuCheckRT(other && field->id() == other->id() && field->key() == other->key() &&
	field->required() == other->required() &&
	field->default_integer() == other->default_integer() &&
	field->type()->base_type() == other->type()->base_type() &&
	field->type()->element() == other->type()->element() &&
	field->type()->index() == other->type()->index());
    }
  }
}

static void schemas()
{
  ZuTestScope(schemas);
  Zfb::Buffer bytes;
  Zfb::saveSchema<Native::Row>(bytes);
  flatbuffers::Verifier verifier{bytes.data(), bytes.length()};
  ZuCheck(reflection::VerifySchemaBuffer(verifier));
  auto schema = reflection::GetSchema(bytes.data());
  ZuCheck(schema->objects()->size() == 6);
  auto ints = schema->objects()->LookupByKey("maps.IntMapEntry");
  auto texts = schema->objects()->LookupByKey("maps.TextMapEntry");
  ZuCheck(ints && texts);
  if (!ints || !texts) return;
  ZuCheck(ints->fields()->LookupByKey("key")->key());
  ZuCheck(texts->fields()->LookupByKey("key")->key());
  ZuCheck(texts->fields()->LookupByKey("key")->required());
  ZuCheck(ints->fields()->LookupByKey("value")->default_integer() == -11);
  ZtString<> fbs;
  Zfb::saveFBS<Native::Row>(fbs);
  flatbuffers::Parser parser;
  bool parsed = parser.Parse(fbs.data());
  ZuCheck(parsed);
  if (!parsed) { log(parser.error_); return; }
  parser.Serialize();
  auto compiled = reflection::GetSchema(parser.builder_.GetBufferPointer());
  ZuTestCall(compareSchema, *schema, *compiled);
  flatbuffers::Parser imported;
  ZuCheck(imported.Deserialize(bytes.data(), bytes.length()));
  parsed = imported.ParseJson("{ ints:[{key:2,value:20}],"
    "texts:[{key:\"x\",value:\"bfbs\"}],nested:[{key:3,value:[{key:4,value:{}}]}] }");
  ZuCheck(parsed);
  if (!parsed) { log(imported.error_); return; }
  ZuBSpan incoming{imported.builder_.GetBufferPointer(), imported.builder_.GetSize()};
  ZuCheck(Zfb::verify<Native::Row>(incoming));
  auto row = Zfb::load<Native::Row>(incoming);
  ZuCheck(row.ints->findVal(2) == 20 && row.texts.findVal("x") == "bfbs");
  ZuCheck(row.nested.find(3)->val().find(4)->val()->value == 17);
}

static void malformed()
{
  ZuTestScope(malformed);
  Native::Row row{new Native::Ints(), {}, {}};
  row.texts.add("a", "value");
  Zfb::Buffer bytes, schemaBytes;
  ZuCheck(Zfb::save(bytes, row) == Zfb::Error::OK);
  Zfb::saveSchema<Native::Row>(schemaBytes);
  auto schema = Zfb::view<Zfb::Reflect::Schema>(schemaBytes);
  // The vector occupies the end of this buffer. Truncation must be rejected
  // independently by all three ingress validators.
  ZuBSpan cut{bytes.data(), bytes.length() - 1};
  flatbuffers::Verifier truncated{cut.data(), cut.length()};
  ZuCheck(!maps::VerifyRowBuffer(truncated));
  ZuCheck(!Zfb::verify<Native::Row>(cut));
  ZuCheck(!Zfb::Reflect::verify(schema, cut));
  Zfb::write<Zfb::Offset>(bytes.data(), 0);
  flatbuffers::Verifier badRoot{bytes.data(), bytes.length()};
  ZuCheck(!maps::VerifyRowBuffer(badRoot));
  ZuCheck(!Zfb::verify<Native::Row>(bytes));
  ZuCheck(!Zfb::Reflect::verify(schema, bytes));

  // Bypass the generated constructor's required-key assertion to produce an
  // entry with no string key. This is structural requiredness, not key-content
  // or ordering validation.
  flatbuffers::FlatBufferBuilder builder;
  auto start = builder.StartTable();
  flatbuffers::Offset<maps::TextMapEntry> missing{builder.EndTable(start)};
  auto vector = builder.CreateVector(&missing, 1);
  maps::FinishRowBuffer(builder, maps::CreateRow(builder, 0, vector));
  ZuBSpan incoming{builder.GetBufferPointer(), builder.GetSize()};
  flatbuffers::Verifier badKey{incoming.data(), incoming.length()};
  ZuCheck(!maps::VerifyRowBuffer(badKey));
  ZuCheck(!Zfb::verify<Native::Row>(incoming));
  ZuCheck(!Zfb::Reflect::verify(schema, incoming));
}

int main(int argc, char **argv)
{
  ZuTestUtil::parse(argc, argv);
  ZuTestMain();
  ZuTestCall(exchange);
  ZuTestCall(ordering);
  ZuTestCall(schemas);
  ZuTestCall(malformed);
}
