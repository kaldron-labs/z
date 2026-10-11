//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdint.h>
#include <string.h>

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZtScratch.hh>
#include <zlib/ZtString.hh>

#include <zlib/ZiFBReflect.hh>

#include "ZiFBTest.hh"

using namespace ZuTestUtil;

// Every mapped representation participates in the same reachable graph.
ZuAssert(ZiFB::SchemaOut::Schema<ValueTest::Row>::Objects::N == 8);
ZuAssert(ZiFB::SchemaOut::Schema<ValueTest::Bitmaps>::Objects::N == 3);

static void reflection()
{
  ZuTestScope(reflection);
  namespace R = ZiFB::Reflect;
  // A fixture graph on the stack exercises the ordinary typed codec. The
  // production BFBS emitter specializes metadata directly into a stream.
  R::Type type{R::BaseType::Long, R::BaseType::None, -1, 0, 8, 0};
  R::Field field{};
  field.name = "value";
  field.type = &type;
  field.offset = 4;
  field.defaultInteger = -1;
  const R::Field *fields[] = {&field};
  R::Object object{};
  object.name = "test.Record";
  object.fields = fields;
  const R::Object *objects[] = {&object};
  const R::Enum *empty = nullptr;
  R::Schema schema{};
  schema.objects = objects;
  schema.enums = {&empty, 0}; // present required empty vector
  schema.rootTable = &object;

  auto buf = ZtScratch(ZiFB::Buffer, 0);
  ZiFB::ArrayStorage<> storage{buf};
  ZiFB::Builder<decltype(storage)> builder{storage};
  auto bytes = builder.finish(ZiFB::saveObject(builder, schema), "BFBS");
  ZuCheck(!builder.failed() && ZiFB::verify<R::Schema>(bytes, "BFBS"));
  auto view = ZiFB::view<R::Schema>(bytes);
  auto root = view.get<R::ZfField(Schema, rootTable)>();
  ZuCheck(root.get<R::ZfField(Object, name)>() == "test.Record");
  auto f = root.get<R::ZfField(Object, fields)>()[0];
  ZuCheck(f.get<R::ZfField(Field, defaultInteger)>() == -1);
  ZuCheck(f.get<R::ZfField(Field, type)>().get<R::ZfField(Type, baseType)>() ==
    R::BaseType::Long);
  ZuCheck(view.get<R::ZfField(Schema, enums)>().length() == 0);
  ZuCheck(ZiFB::Meta<R::EnumVal>::id<R::ZfField(EnumVal, unionType)>() == 3);
  ZuCheck(ZiFB::Meta<R::EnumVal>::Fields::N == 6);
  ZuCheck(ZiFB::Meta<R::EnumVal>::Active::N == 5);
}

namespace SchemaTest {

struct Root;
struct Child { int value; const Root *parent; };
struct Root { ZuCSpan name; const Child *child; bool enabled; };
ZfStruct(, Child,
  (value,	(Deflt<-42>),	Int32),
  (parent,,			UDT));
ZfStruct(, Root,
  (name,			(Required),	String),
  ((retired, Skip, int),	(Deflt<7>),	Int32),
  (child,,					UDT),
  (enabled,			(Deflt<true>),	Bool));
ZiFB::Table<"test.Child"> ZiFBType(Child *);
ZiFB::Table<"test.Root"> ZiFBType(Root *);

ZuDerive(Text, (ZtString<ZtStringHeapID<"ZiFBReflectTest.Text">>));

// This destination offers only operator <<. No seek, reserve, capacity,
// data, length or failure API is required by the schema generator.
struct Stream {
  Text &out;
  Stream &operator <<(ZuCSpan s) { out << s; return *this; }
};

static void run()
{
  ZuTestScope(schema);
  namespace R = ZiFB::Reflect;
  using Schema = ZiFB::SchemaOut::Schema<Root>;
  ZuAssert(Schema::Objects::N == 2); // recursive references do not rediscover types
  auto text = ZtScratch(Text, Schema::Size);
  Stream stream{text};
  ZiFB::saveSchema<Root>(stream);
  ZuCheck(text.length() == Schema::Size);
  ZuBSpan bytes{text};
  ZuCheck(ZiFB::verify<R::Schema>(bytes, "BFBS"));
  auto schema = ZiFB::view<R::Schema>(bytes);
  auto objects = schema.get<R::ZfField(Schema, objects)>();
  ZuCheck(objects.length() == 2);
  ZuCheck(objects[0].get<R::ZfField(Object, name)>() == "test.Child");
  ZuCheck(objects[1].get<R::ZfField(Object, name)>() == "test.Root");
  auto root = schema.get<R::ZfField(Schema, rootTable)>();
  ZuCheck(root.data() == objects[1].data());
  auto fields = root.get<R::ZfField(Object, fields)>();
  ZuCheck(fields.length() == 4);
  ZuCheck(fields[0].get<R::ZfField(Field, name)>() == "child");
  ZuCheck(fields[0].get<R::ZfField(Field, id)>() == 2);
  ZuCheck(fields[0].get<R::ZfField(Field, optional)>());
  ZuCheck(fields[0].get<R::ZfField(Field, type)>().get<R::ZfField(Type, index)>() == 0);
  ZuCheck(fields[1].get<R::ZfField(Field, defaultInteger)>() == 1);
  ZuCheck(fields[1].get<R::ZfField(Field, type)>().get<R::ZfField(Type, baseType)>() ==
    R::BaseType::Bool);
  ZuCheck(fields[2].get<R::ZfField(Field, required)>());
  ZuCheck(!fields[2].get<R::ZfField(Field, optional)>());
  ZuCheck(fields[3].get<R::ZfField(Field, deprecated)>());
  ZuCheck(fields[3].get<R::ZfField(Field, defaultInteger)>() == 7);
  auto childFields = objects[0].get<R::ZfField(Object, fields)>();
  ZuCheck(childFields[1].get<R::ZfField(Field, defaultInteger)>() == -42);
  ZuCheck(childFields[0].get<R::ZfField(Field, type)>().get<R::ZfField(Type, index)>() == 1);
}

} // SchemaTest

namespace UnionTest {

ZuDerive(SchemaBuf, (ZtString<ZtStringHeapID<"ZiFBReflectTest.Union">>));

static void schema()
{
  ZuTestScope(schema);
  namespace R = ZiFB::Reflect;
  using S = ZiFB::SchemaOut::Schema<Message>;
  ZuAssert(S::Objects::N == 4 && S::Enums::N == 1);
  auto bytes = ZtScratch(SchemaBuf, S::Size);
  ZiFB::saveSchema<Message>(bytes);
  ZuCheck(bytes.length() == S::Size);
  ZuCheck(ZiFB::verify<R::Schema>(bytes, "BFBS"));
  auto view = ZiFB::view<R::Schema>(bytes);
  auto fields = view.get<R::ZfField(Schema, rootTable)>().get<R::ZfField(Object, fields)>();
  ZuCheck(fields.length() == 3);
  ZuCheck(fields[0].get<R::ZfField(Field, name)>() == "body");
  ZuCheck(fields[0].get<R::ZfField(Field, id)>() == 2);
  ZuCheck(fields[1].get<R::ZfField(Field, name)>() == "body_type");
  ZuCheck(fields[1].get<R::ZfField(Field, id)>() == 1);
  ZuCheck(!fields[1].get<R::ZfField(Field, optional)>());
  auto type = fields[1].get<R::ZfField(Field, type)>();
  ZuCheck(type.get<R::ZfField(Type, baseType)>() == R::BaseType::UType);
  ZuCheck(type.get<R::ZfField(Type, index)>() == 0);
  auto enums = view.get<R::ZfField(Schema, enums)>();
  ZuCheck(enums.length() == 1 && enums[0].get<R::ZfField(Enum, isUnion)>());
  auto values = enums[0].get<R::ZfField(Enum, values)>();
  ZuCheck(values.length() == 5);
  ZuCheck(values[0].get<R::ZfField(EnumVal, name)>() == "NONE");
  ZuCheck(values[1].get<R::ZfField(EnumVal, name)>() == "Replication");
  ZuCheck(values[2].get<R::ZfField(EnumVal, name)>() == "Recovery");
  ZuCheck(values[1].get<R::ZfField(EnumVal, unionType)>().get<R::ZfField(Type, index)>() ==
    values[2].get<R::ZfField(EnumVal, unionType)>().get<R::ZfField(Type, index)>());
}

} // UnionTest

namespace StreamTest {

static void facets()
{
  ZuTestScope(facets);
  namespace R = ZiFB::Reflect;
  auto text = ZtScratch(Text, 0);
  Stream stream{text};
  ZiFB::saveSchema<Row, ZuFacet::Wire>(stream);
  ZuCheck(ZiFB::verify<R::Schema>(text, "BFBS"));
  auto schema = ZiFB::view<R::Schema>(text);
  auto fields = schema.get<R::ZfField(Schema, rootTable)>().get<R::ZfField(Object, fields)>();
  ZuCheck(fields.length() == 3);
  ZuCheck(fields[0].get<R::ZfField(Field, name)>() == "payload");
  ZuCheck(fields[0].get<R::ZfField(Field, id)>() == 2);
  ZuCheck(fields[1].get<R::ZfField(Field, name)>() == "sequence");
  ZuCheck(fields[1].get<R::ZfField(Field, id)>() == 0);
  text.null();
  ZiFB::saveFBS<Row, ZuFacet::Wire>(text);
  ZuCheck(text.find("sequence:uint") >= 0);
  ZuCheck(text.find("payload:[ubyte]") >= 0);
}

static void reflected()
{
  ZuTestScope(reflected);
  namespace R = ZiFB::Reflect;
  auto schemaBytes = ZtScratch(Text, 0);
  ZiFB::saveSchema<Row>(schemaBytes);
  auto schema = ZiFB::view<R::Schema>(schemaBytes);
  auto root = R::rootObject(schema);
  auto seq = R::field(root, ZuCSpan{"seq"});
  auto stamp = R::field(root, 1U);
  auto bytes = R::field(root, ZuCSpan{"bytes"});
  auto buf = ZtScratch(ZiFB::Buffer, 0);
  ZiFB::ArrayStorage<32> storage{buf};
  ZiFB::Builder<decltype(storage)> builder{storage, 32};
  auto payload = builder.vector(3, 1, R::alignment(bytes),
    [](uint8_t *p) { memcpy(p, "a\0b", 3); });
  auto table = builder.startTable();
  R::saveScalar(builder, table, seq, 7U, true);
  R::saveInline(builder, table, schema, stamp,
    [](uint8_t *p) { ZiFB::writeInline(p, Stamp{123}); });
  R::saveOffset(builder, table, bytes, payload);
  auto out = builder.finish(builder.endTable(table));
  ZuCheck(!builder.failed() && R::verify(schema, out) && ZiFB::verify<Row>(out));
  auto native = ZiFB::load<Row>(out);
  ZuCheck(native.seq == 7 && native.stamp.value == 123 && native.bytes.length() == 3);
  auto borrowed = ZiFB::root(out);
  ZuCheck(borrowed.has(0)); // forced-present default survives reflected writing
  ZuCheck(R::scalar<unsigned>(borrowed, seq) == 7);
  ZuCheck(R::vector(borrowed, bytes).bytes() == native.bytes);

  ZiFB::Verifier verifier{out};
  unsigned at;
  ZiFB::TableBounds bounds;
  ZuCheck(verifier.root(at) && verifier.table(at, bounds));
  auto slot = builder.data() + bounds.vtable + ZiFB::Wire::VHeader;
  auto saved = ZiFB::read<ZiFB::VOffset>(slot);
  ZiFB::write<ZiFB::VOffset>(slot, saved + 1);
  ZuCheck(!R::verify(schema, out)); // unaligned scalar
  ZiFB::write<ZiFB::VOffset>(slot, bounds.size);
  ZuCheck(!R::verify(schema, out)); // scalar extends beyond the object
  ZiFB::write<ZiFB::VOffset>(slot, 0);
  ZuCheck(R::verify(schema, out)); // omitted scalar uses its default
  ZiFB::write<ZiFB::VOffset>(slot, saved);

  builder.clear();
  table = builder.startTable();
  R::saveOffset(builder, table, bytes, 0);
  ZuCheck(builder.error() == ZiFB::Error::Required);
}

static void reflectedVectors()
{
  ZuTestScope(reflectedVectors);
  namespace R = ZiFB::Reflect;
  auto schemaBytes = ZtScratch(Text, 0);
  ZiFB::saveSchema<Batch>(schemaBytes);
  auto schema = ZiFB::view<R::Schema>(schemaBytes);
  Row first{8, {123}, ZuBSpan{"one"}}, second{9, {456}, ZuBSpan{"two"}};
  const Row *rows[] = {&first, &second};
  const ZuCSpan names[] = {"one", "two"};
  const Stamp stamps[] = {{123}, {456}};
  auto bytes = ZtScratch(Text, 0);
  ZuCheck(ZiFB::save(bytes, Batch{rows, names, stamps}) == ZiFB::Error::OK);
  ZuCheck(R::verify(schema, bytes) && ZiFB::verify<Batch>(bytes));
  ZuBSpan span{bytes};
  for (unsigned id = 0; id < 2; ++id) {
    auto vector = ZiFB::root(span).vector(id);
    auto at = vector.data() + sizeof(ZiFB::Offset) - span.data();
    auto slot = bytes.data() + at;
    auto saved = ZiFB::read<ZiFB::Offset>(slot);
    ZiFB::write<ZiFB::Offset>(slot, UINT32_MAX);
    ZuCheck(!R::verify(schema, bytes));
    ZiFB::write<ZiFB::Offset>(slot, saved);
    ZuCheck(R::verify(schema, bytes));
  }
  bytes.clear();
  rows[1] = nullptr;
  ZuCheck(ZiFB::save(bytes, Batch{rows, names, stamps}) == ZiFB::Error::Required);
  ZuCheck(!bytes.length());
}

static void externalSchema()
{
  ZuTestScope(externalSchema);
  namespace R = ZiFB::Reflect;
  auto bytes = ZtScratch(Text, 0);
  ZiFB::saveSchema<Row>(bytes);
  ZuCheck(R::verifySchema(bytes));
  auto s = ZiFB::view<R::Schema>(bytes);
  auto stamp = R::field(R::rootObject(s), ZuCSpan{"stamp"});
  auto child = R::object(s, R::type(stamp));
  auto corrupt = [&bytes](const uint8_t *p, auto value) {
    ZuTestScope(corrupt);
    using T = decltype(value);
    auto original = ZiFB::read<T>(p);
    ZuBSpan span{bytes};
    auto at = p - span.data();
    ZiFB::write<T>(bytes.data() + at, value);
    // These mutations retain a structurally valid BFBS table graph.
    ZuCheck(ZiFB::verify<R::Schema>(bytes, "BFBS"));
    ZuCheck(!R::verifySchema(bytes));
    ZiFB::write<T>(bytes.data() + at, original);
  };
  ZuTestCall(corrupt, R::type(stamp).field<R::ZfField(Type, index)>(), int32_t(INT32_MAX));
  ZuTestCall(corrupt, child.field<R::ZfField(Object, minalign)>(), int32_t(3));
  auto member = R::field(child, 0U);
  ZuTestCall(corrupt, member.field<R::ZfField(Field, offset)>(), uint16_t(UINT16_MAX));
  ZuTestCall(corrupt, stamp.field<R::ZfField(Field, offset)>(), uint16_t(0));
  auto payload = R::field(R::rootObject(s), ZuCSpan{"bytes"});
  auto attrs = payload.get<R::ZfField(Field, attributes)>();
  auto align = attrs[0].get<R::ZfField(KeyValue, value)>();
  ZuBSpan alignBytes{align};
  ZuTestCall(corrupt, alignBytes.data() + 1, uint8_t('3')); // 32 -> 33
  ZuCheck(R::verifySchema(bytes));
  ZuCheck(!R::verifySchema(ZuBSpan{bytes.data(), 8}));
}

} // StreamTest

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(reflection);
  ZuTestCall(SchemaTest::run);
  ZuTestCall(UnionTest::schema);
  ZuTestCall(StreamTest::facets);
  ZuTestCall(StreamTest::reflected);
  ZuTestCall(StreamTest::reflectedVectors);
  ZuTestCall(StreamTest::externalSchema);
}
