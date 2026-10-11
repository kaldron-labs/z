//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdint.h>
#include <string.h>

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmHeap.hh>

#include <zlib/ZtArray.hh>
#include <zlib/ZtCase.hh>
#include <zlib/ZtScratch.hh>
#include <zlib/ZtString.hh>

#include <zlib/ZfJSON.hh>

#include "ZiFBTest.hh"

using namespace ZuTestUtil;

namespace MetaTest {

struct Ping { };
ZfStruct(, Ping);
ZiFB::Table<"test.Ping"> ZiFBType(Ping *);

struct Body { };
ZiFB::Union<"test.Body",
  ZiFB::Alt<"Ping", Ping>, ZiFB::Alt<"Again", Ping>> ZiFBType(Body *);

struct Implicit {
  unsigned id;
  Body body;
  uint64_t roleIDs;
};
ZfStruct(, Implicit,
  ((retired, Skip, int),	(Deflt<7>),	Int32),
  (id,				(Keys<0>),	UInt32),
  (body,,					UDT),
  (roleIDs,,					UInt64));
ZfStructRender(, Implicit, Wire, (roleIDs, FB::ID<"roles">), id);
ZiFB::Table<"test.Implicit"> ZiFBType(Implicit *);

struct Explicit {
  uint64_t value;
  Body body;
  unsigned tail;
};
ZfStruct(, Explicit,
  (value,			(FB::FieldID<3>),	UInt64),
  ((retired, Skip, int),	(FB::FieldID<0>),	Int32),
  (body,			(FB::FieldID<2>),	UDT),
  (tail,			(FB::FieldID<4>),	UInt32));
ZiFB::Table<"test.Explicit"> ZiFBType(Explicit *);

using MI = ZiFB::Meta<Implicit>;
using ME = ZiFB::Meta<Explicit>;
using MF = ZiFB::Meta<Implicit, ZuFacet::Wire>;
using Key = ZuStructKeyT<Implicit>;
using MK = ZiFB::Meta<Key>;
ZuAssert(MI::N == 5);
ZuAssert(MI::Fields::N == 4);
ZuAssert(MI::Active::N == 3);
ZuAssert(MI::id<ZfField(Implicit, retired)>() == 0);
ZuAssert(MI::id<ZfField(Implicit, id)>() == 1);
ZuAssert(MI::id<ZfField(Implicit, body)>() == 3);
ZuAssert(MI::id<ZfField(Implicit, roleIDs)>() == 4);
ZuAssert(ME::id<ZfField(Explicit, value)>() == 3);
ZuAssert(ME::id<ZfField(Explicit, retired)>() == 0);
ZuAssert(ME::id<ZfField(Explicit, body)>() == 2);
ZuAssert(ME::id<ZfField(Explicit, tail)>() == 4);
ZuAssert(MF::N == MI::N);
ZuAssert(MF::Fields::N == 4);
ZuAssert(MF::Active::N == 2);
ZuAssert(MF::id<ZfField(Implicit_Wire, roleIDs)>() == 4);
ZuAssert(MK::N == MI::N);
ZuAssert((MK::id<ZuType<0, MK::Active>>() == 1));
ZuAssert(MI::name() == "test.Implicit");
ZuAssert(MI::fieldName<ZfField(Implicit, roleIDs)>() == "role_i_ds");
ZuAssert(MF::fieldName<ZfField(Implicit_Wire, roleIDs)>() == "roles");
ZuAssert(ZiFB::IsUnion<Body>{});
ZuAssert(ZiFB::Type<Body>::Types::N == 2);

// Compile each ZIFB_BAD_* branch separately; each must fail to compile.
#if defined(ZIFB_BAD_MIXED)
struct Bad { int a, b; };
ZfStruct(, Bad,
  (a,	(FB::FieldID<0>),	Int32),
  (b,,				Int32));
ZuAssert(sizeof(ZiFB::Meta<Bad>) > 0);
#elif defined(ZIFB_BAD_GAP)
struct Bad { int a, b; };
ZfStruct(, Bad,
  (a,	(FB::FieldID<0>),	Int32),
  (b,	(FB::FieldID<2>),	Int32));
ZuAssert(sizeof(ZiFB::Meta<Bad>) > 0);
#elif defined(ZIFB_BAD_UNION)
struct Bad { Body body; };
ZfStruct(, Bad, (body, (FB::FieldID<0>), UDT));
ZuAssert(sizeof(ZiFB::Meta<Bad>) > 0);
#elif defined(ZIFB_BAD_NAME)
struct Bad { int a, b; };
ZfStruct(, Bad,
  (a,,			Int32),
  (b,	(FB::ID<"a">),	Int32));
ZuAssert(sizeof(ZiFB::Meta<Bad>) > 0);
#elif defined(ZIFB_BAD_ALIGN)
struct Bad { ZuBSpan bytes; };
ZfStruct(, Bad, (bytes, (FB::Align<3>), Bytes));
ZuAssert(ZuFieldProp::FB::align<ZfField(Bad, bytes)>() == 3);
#elif defined(ZIFB_BAD_TAG_NAME)
struct Bad { Body body; unsigned bodyType; };
ZfStruct(, Bad,
  (body,,	UDT),
  (bodyType,,	UInt32));
ZuAssert(sizeof(ZiFB::Meta<Bad>) > 0);
#elif defined(ZIFB_BAD_ALT_NAME)
using Bad = ZiFB::Union<"test.Bad", ZiFB::Alt<"NONE", int>>;
ZuAssert(sizeof(Bad) > 0);
#endif

static void run()
{
  ZuTestScope(meta);
  ZuCheck(MI::N == 5 && ME::N == 5);
  ZuCheck(MK::name() == MI::name());
  ZtCase::camelSnake("roleIDs", [](ZuCSpan name) {
    ZuCheck(name == MI::fieldName<ZfField(Implicit, roleIDs)>());
  });
}

} // MetaTest

namespace WireTest {

// Explicit wire fixtures, independent of the writer under development:
// table Row { value:uint; text:string; }
static constexpr uint8_t row[] = {
  12, 0, 0, 0,
  8, 0, 12, 0, 4, 0, 8, 0,
  8, 0, 0, 0, 42, 0, 0, 0, 4, 0, 0, 0,
  3, 0, 0, 0, 'x', 0, 'y', 0
};

static bool verify(ZuBSpan bytes)
{
  ZiFB::Verifier v{bytes};
  ZiFB::TableBounds bounds;
  unsigned at, field, text;
  if (!v.root(at) || !v.table(at, bounds) ||
      !v.field(bounds, 0, 4, 4, field) ||
      !v.field(bounds, 1, 4, 4, field)) return false;
  return !field || (v.offset(field, text) && v.string(text));
}

static void borrowed()
{
  ZuTestScope(borrowed);
  ZuCheck(verify(row));
  auto view = ZiFB::root(ZuBSpan{row});
  ZuCheck(view.scalar<uint32_t>(0, 7) == 42);
  ZuCheck(view.scalar<uint32_t>(2, 7) == 7);
  ZuCheck(view.has(0) && view.has(1) && !view.has(2));
  auto text = view.vector(1).string();
  ZuCheck(text.length() == 3 && text[0] == 'x' && !text[1] && text[2] == 'y');
  ZuCheck(view.vector(1).data() == row + 28);
  ZuCheck(!view.vector(2) && !view.vector(2).length());

  uint8_t mutated[sizeof(row)];
  memcpy(mutated, row, sizeof(row));
  ZiFB::write<uint32_t>(mutated + 16, 7);
  auto changed = ZiFB::root(ZuBSpan{mutated});
  ZuCheck(changed.has(0) && changed.scalar<uint32_t>(0, 7) == 7);
  ZuCheck(verify(mutated));
}

static void forward()
{
  ZuTestScope(forward);
  // A shared vtable can follow its table: displacement is -12.
  uint8_t bytes[] = {
    4, 0, 0, 0,
    0xf4, 0xff, 0xff, 0xff, 99, 0, 0, 0,
    0, 0, 0, 0,
    6, 0, 8, 0, 4, 0, 0, 0
  };
  ZuCheck(verify(bytes));
  auto view = ZiFB::root(ZuBSpan{bytes});
  ZuCheck(view.scalar<uint32_t>(0, 0) == 99 && !view.has(1));
}

static void prefix()
{
  ZuTestScope(prefix);
  uint8_t bytes[sizeof(row) + sizeof(ZiFB::Offset)];
  ZiFB::write<uint32_t>(bytes, sizeof(row));
  memcpy(bytes + sizeof(ZiFB::Offset), row, sizeof(row));
  ZiFB::Verifier v{bytes};
  ZiFB::TableBounds bounds;
  unsigned at;
  ZuCheck(v.root<true>(at) && v.table(at, bounds));
  ZuCheck(ZiFB::root<true>(ZuBSpan{bytes}).scalar<uint32_t>(0, 0) == 42);
  ZiFB::write<uint32_t>(bytes, UINT32_MAX);
  ZiFB::Verifier bad{bytes};
  ZuCheck(!bad.root<true>(at));
}

static void malformed()
{
  ZuTestScope(malformed);
  for (unsigned n = 0; n < sizeof(row); ++n)
    ZuCheck(!verify({row, n}));
  uint8_t bytes[sizeof(row)];
  memcpy(bytes, row, sizeof(row));
  ZiFB::write<uint32_t>(bytes, 0);
  ZuCheck(!verify(bytes));
  memcpy(bytes, row, sizeof(row));
  ZiFB::write<int32_t>(bytes + 12, INT32_MIN);
  ZuCheck(!verify(bytes));
  memcpy(bytes, row, sizeof(row));
  ZiFB::write<uint16_t>(bytes + 8, UINT16_MAX);
  ZuCheck(!verify(bytes));
  memcpy(bytes, row, sizeof(row));
  ZiFB::write<uint32_t>(bytes + 20, UINT32_MAX);
  ZuCheck(!verify(bytes));
  memcpy(bytes, row, sizeof(row));
  ZiFB::write<uint32_t>(bytes + 24, UINT32_MAX);
  ZuCheck(!verify(bytes));
  memcpy(bytes, row, sizeof(row));
  bytes[31] = 1;
  ZuCheck(!verify(bytes));

  ZiFB::Verifier limited{row, {.depth = 1, .tables = 2}};
  ZuCheck(limited.enter());
  ZuCheck(!limited.enter());
  limited.leave();
  ZuCheck(limited.enter());
  limited.leave();
  ZuCheck(!limited.enter());
}

static void emptyVector()
{
  ZuTestScope(emptyVector);
  // Canonical empty vectors need only their aligned length word.
  uint8_t bytes[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  unsigned n;
  ZiFB::Verifier empty{{bytes, sizeof(ZiFB::Offset)}};
  ZuCheck(empty.vector(0, sizeof(uint64_t), alignof(uint64_t), n) && !n);
  ZiFB::Verifier truncated{{bytes, sizeof(ZiFB::Offset) - 1}};
  ZuCheck(!truncated.vector(0, sizeof(uint64_t), alignof(uint64_t), n));
  ZiFB::write<ZiFB::Offset>(bytes, 1);
  ZiFB::Verifier nonempty{bytes};
  ZuCheck(!nonempty.vector(0, sizeof(uint64_t), alignof(uint64_t), n));
  // Wide products must not wrap into an apparently small byte count.
  ZiFB::write<ZiFB::Offset>(bytes, 0x40000000U);
  ZuCheck(!nonempty.vector(0, sizeof(uint32_t), alignof(uint32_t), n));
  ZiFB::write<ZiFB::Offset>(bytes, UINT32_MAX);
  ZuCheck(!nonempty.vector(0, UINT32_MAX, 1, n));
  ZiFB::write<ZiFB::Offset>(bytes, 2);
  ZuCheck(nonempty.vector(0, sizeof(uint32_t), alignof(uint32_t), n) && n == 2);
  ZiFB::write<ZiFB::Offset>(bytes, 3);
  ZuCheck(!nonempty.vector(0, sizeof(uint32_t), alignof(uint32_t), n));
}

static void bounds()
{
  ZuTestScope(bounds);
  ZiFB::Verifier v{row};
  ZuCheck(v.range(0, sizeof(row)));
  ZuCheck(v.range(sizeof(row), 0));
  ZuCheck(!v.range(sizeof(row), 1));
  ZuCheck(!v.range(sizeof(row) + 1, 0));
  ZuCheck(!v.range(UINT_MAX, 1));
  ZuCheck(!v.range(1, UINT_MAX));
  ZuCheck(!v.range(UINT_MAX, UINT_MAX));
  ZiFB::TableBounds table;
  unsigned at, field;
  ZuCheck(v.root(at) && v.table(at, table));
  ZuCheck(v.field(table, 0, sizeof(uint32_t), alignof(uint32_t), field));
  ZuCheck(!v.field(table, 0, UINT_MAX, 1, field));
}

static void unaligned()
{
  ZuTestScopeRT(unaligned);
  ZuAssert(alignof(ZiFB::Wire::Scalar<uint64_t>) == 1);
  alignas(uint64_t) uint8_t bytes[2 * sizeof(uint64_t)];
  for (unsigned offset = 0; offset < sizeof(uint64_t); ++offset) {
    auto p = bytes + offset;
    ZiFB::write<uint64_t>(p, 0xfedcba9876543210ULL);
    ZuCheckRT(p[0] == 0x10 && p[7] == 0xfe);
    ZuCheckRT(ZiFB::read<uint64_t>(p) == 0xfedcba9876543210ULL);
    ZiFB::write<int64_t>(p, -42);
    ZuCheckRT(ZiFB::read<int64_t>(p) == -42);
    ZiFB::write<double>(p, -1.25);
    ZuCheckRT(ZiFB::read<double>(p) == -1.25);
  }
  alignas(uint64_t) uint8_t shifted[sizeof(row) + 1];
  memcpy(shifted + 1, row, sizeof(row));
  ZuBSpan span{shifted + 1, sizeof(row)};
  ZuCheckRT(verify(span));
  ZuCheckRT(ZiFB::root(span).scalar<uint32_t>(0, 0) == 42);
}

} // WireTest

namespace BuilderTest {

template <typename B>
static unsigned row(B &builder, uint64_t value, ZuCSpan text)
{
  auto string = builder.string(text);
  builder.align(sizeof(uint64_t)); // make both tested table layouts identical
  auto table = builder.startTable();
  builder.fieldOffset(table, 1, string);
  builder.field(table, 0, value, uint64_t(0));
  return builder.endTable(table);
}

static bool verify(ZuBSpan bytes)
{
  ZiFB::Verifier v{bytes};
  ZiFB::TableBounds table;
  unsigned at, field, text;
  if (!v.root(at) || !v.table(at, table) ||
      !v.field(table, 0, 8, 8, field) ||
      !v.field(table, 1, 4, 4, field)) return false;
  return !field || (v.offset(field, text) && v.string(text));
}

static void tables()
{
  ZuTestScope(tables);
  auto buf = ZtScratch(ZiFB::Buffer, 0);
  ZiFB::ArrayStorage<> storage{buf};
  ZiFB::Builder<decltype(storage)> builder{storage};
  auto first = row(builder, 42, "first");
  auto second = row(builder, 43, "other");
  ZuCheck(ZiFB::TableView{builder.at(first)}.vtable() ==
    ZiFB::TableView{builder.at(second)}.vtable());
  auto output = builder.finish(second);
  ZuCheck(!builder.failed() && verify(output));
  auto view = ZiFB::root(output);
  ZuCheck(view.scalar<uint64_t>(0, 0) == 43);
  ZuCheck(view.vector(1).string() == "other");
  unsigned capacity = builder.capacity();
  builder.clear();
  auto table = builder.startTable();
  builder.field(table, 0, uint64_t(0), uint64_t(0), true);
  auto forced = builder.finish(builder.endTable(table));
  ZuCheck(builder.capacity() == capacity);
  ZuCheck(ZiFB::root(forced).has(0));
  ZuCheck(verify(forced));
}

static void nesting()
{
  ZuTestScope(nesting);
  auto buf = ZtScratch(ZiFB::Buffer, 0);
  ZiFB::ArrayStorage<> storage{buf};
  ZiFB::Builder<decltype(storage)> builder{storage};
  auto first = row(builder, 42, "outside");
  auto nested = builder.startNested();
  auto inside = row(builder, 43, "inside");
  auto nestedVec = builder.endNested(nested, inside);
  auto table = builder.startTable();
  builder.fieldOffset(table, 0, first);
  builder.fieldOffset(table, 1, nestedVec);
  auto output = builder.finish(builder.endTable(table));
  auto span = ZiFB::root(output).vector(1).bytes();
  ZuCheck(!builder.failed() && verify(span));
  ZuCheck(ZiFB::root(span).scalar<uint64_t>(0, 0) == 43);
  auto vt = ZiFB::root(span).vtable();
  ZuCheck(vt >= span.data() && vt < span.data() + span.length());

  auto copyBuf = ZtScratch(ZiFB::Buffer, 0);
  ZiFB::ArrayStorage<> copyStorage{copyBuf};
  ZiFB::Builder<decltype(copyStorage)> copy{copyStorage};
  auto payload = copy.copyRoot(span, 8);
  auto envelope = copy.startTable();
  copy.fieldOffset(envelope, 0, payload);
  auto copied = ZiFB::root(copy.finish(copy.endTable(envelope))).vector(0).bytes();
  ZuCheck(copied == span && verify(copied));
}

static void growth()
{
  ZuTestScope(growth);
  // Initial scratch comes from one scalar row's protocol-derived footprint.
  constexpr unsigned Initial = sizeof(uint64_t) + 3 * sizeof(ZiFB::Offset) +
    4 * sizeof(ZiFB::VOffset);
  auto buf = ZtScratch(ZiFB::Buffer, Initial);
  ZiFB::ArrayStorage<> storage{buf};
  ZiFB::Builder<decltype(storage)> builder{storage};
  auto first = row(builder, 1, "first");
  unsigned before = builder.capacity();
  auto payload = ZtScratch(ZiFB::Buffer, before<<2, before<<2);
  for (unsigned i = 0, n = payload.length(); i < n; ++i) payload[i] = i;
  auto second = row(builder, 2, payload);
  auto bytes = builder.finish(second);
  ZuCheck(builder.capacity() > before);
  ZuCheck(verify(bytes));
  ZuCheck(ZiFB::root(bytes).vector(1).bytes() == payload);
  ZuCheck(ZiFB::TableView{builder.at(first)}.scalar<uint64_t>(0, 0) == 1);
}

static void alignment()
{
  ZuTestScope(alignment);
  auto buf = ZtScratch(ZiFB::Buffer, 0);
  ZiFB::ArrayStorage<32> storage{buf};
  ZiFB::Builder<decltype(storage)> builder{storage, 32};
  auto vector = builder.vector(2, sizeof(uint64_t), 32, [](uint8_t *p) {
    ZiFB::write<uint64_t>(p, 42);
    ZiFB::write<uint64_t>(p + sizeof(uint64_t), 43);
  });
  auto table = builder.startTable();
  builder.fieldOffset(table, 0, vector);
  auto bytes = builder.finish<true>(builder.endTable(table), "TEST");
  ZuCheck(!builder.failed());
  ZuCheck(ZiFB::read<ZiFB::Offset>(bytes.data()) == bytes.length() - 4);
  auto view = ZiFB::root<true>(bytes).vector(0);
  ZuCheck(!(uintptr_t(view.data()) & 31));
  ZuCheck(view.length() == 2 && view.scalar<uint64_t>(1) == 43);
  ZiFB::Verifier v{bytes};
  ZiFB::TableBounds bounds;
  unsigned at;
  ZuCheck(v.root<true>(at, "TEST") && v.table(at, bounds));
}

} // BuilderTest

namespace ViewTest {

struct Point { uint8_t tag; int64_t value; };
ZfStruct(, (Point, JSON),
  (tag,		(Deflt<0>, FB::Align<32>),	UInt8),
  (value,	(Deflt<0>),			Int64));
ZiFB::Inline<"test.Point"> ZiFBType(Point *);

struct Row {
  uint64_t seq;
  ZuCSpan name;
  Point point;
  ZuSpan<const uint64_t> values;
};
ZfStruct(, Row,
  (seq,				(Deflt<UINT64_MAX>),	UInt64),
  ((retired, Skip, int),	(Deflt<0>),		Int32),
  (name,			(Required, Keys<0>),	String),
  (point,,						UDT),
  (values,			(FB::Align<32>),	UInt64Vec));
ZfStructRender(, Row, JSON, seq, (name, JSON::ID<"label">), point, values);
ZiFB::Table<"test.Row"> ZiFBType(Row *);

struct Scalars {
  ZuSpan<const int64_t> narrow;
  ZuSpan<const float> floats;
  ZuSpan<const double> doubles;
};
ZfStruct(, Scalars,
  (narrow,,	Int32Vec),
  (floats,,	FloatVec),
  (doubles,,	FloatVec));
ZiFB::Table<"test.Scalars"> ZiFBType(Scalars *);

ZuDerive(Text, (ZtString<ZtStringHeapID<"ZiFBTest.View.Text">>));

ZuAssert(ZiFB::Layout<Point>::Align == 32);
ZuAssert(ZiFB::Layout<Point>::Size == 32);
ZuAssert(ZiFB::Layout<Point>::offset<ZfField(Point, tag)>() == 0);
ZuAssert(ZiFB::Layout<Point>::offset<ZfField(Point, value)>() == 8);

static void payload()
{
  ZuTestScopeRT(payload);
  const uint64_t values[] = {0, 42, UINT64_MAX};
  Row row{UINT64_MAX, "row", {7, -42}, values};
  auto buf = ZtScratch(ZiFB::Buffer, 0);
  ZiFB::ArrayStorage<32> storage{buf};
  ZiFB::Builder<decltype(storage)> builder{storage, 32};
  auto bytes = builder.finish(ZiFB::saveObject(builder, row));
  ZuCheckRT(!builder.failed() && ZiFB::verify<Row>(bytes));
  auto view = ZiFB::view<Row>(bytes);
  ZuCheckRT(!view.has<ZfField(Row, seq)>());
  ZuCheckRT(view.get<ZfField(Row, seq)>() == UINT64_MAX);
  ZuCheckRT(!view.has<ZfField(Row, retired)>());
  ZuCheckRT(view.get<ZfField(Row, name)>() == "row");
  auto point = view.get<ZfField(Row, point)>();
  ZuCheckRT(point.get<ZfField(Point, tag)>() == 7);
  ZuCheckRT(point.get<ZfField(Point, value)>() == -42);
  auto vector = view.get<ZfField(Row, values)>();
  ZuCheckRT(vector.length() == 3 && vector[1] == 42 && vector[2] == UINT64_MAX);
  ZuCheckRT(!(uintptr_t(vector.data()) & 31));
  {
    auto key = ZuStructKey<0>(row);
    ZuAssert(ZiFB::BufferAlign<decltype(key)>::N == 8);
    ZuAssert(ZiFB::BufferAlign<Row>::N == 32);
    auto keyBuf = ZtScratch(ZiFB::Buffer, 0);
    ZiFB::ArrayStorage<ZiFB::BufferAlign<decltype(key)>::N> keyStorage{keyBuf};
    ZiFB::Builder<decltype(keyStorage)> keyBuilder{keyStorage};
    auto keyBytes = keyBuilder.finish(ZiFB::saveObject(keyBuilder, key));
    ZuCheckRT(!keyBuilder.failed() && ZiFB::verify<Row>(keyBytes));
    auto keyView = ZiFB::view<Row>(keyBytes);
    ZuCheckRT(keyView.get<ZfField(Row, name)>() == row.name);
    ZuCheckRT(!keyView.has<ZfField(Row, point)>() && !keyView.has<ZfField(Row, values)>());
  }
  using Borrowed = decltype(view);
  ZuAssert(ZuStructRO<Borrowed>{});
  ZuAssert(ZuFields<Borrowed>::N == 4);
  ZuCheckRT(ZuStructKey(view).p<0>() == "row");
  ZuCheckRT(ZuStructKey(view).p<0>().data() == view.get<ZfField(Row, name)>().data());
  auto nativeJSON = ZtScratch(Text, 0);
  auto viewJSON = ZtScratch(Text, 0);
  ZfJSON::save(nativeJSON, row);
  ZfJSON::save(viewJSON, view);
  if (nativeJSON != viewJSON) log("native: ", nativeJSON, " borrowed: ", viewJSON);
  ZuCheckRT(nativeJSON == viewJSON);
  auto fields = ZfVFields<Borrowed>();
  ZuCheckRT(fields.length() == 4);
  auto matcher = ZfVFieldMatcher<Borrowed>();
  for (unsigned i = 0; i < fields.length(); ++i)
    ZuCheckRT(matcher(fields[i]->id) == i);
  ZuCheckRT(matcher("retired") < 0);
  for (unsigned n = 0, length = bytes.length(); n < length; ++n)
    ZuCheckRT(!ZiFB::verify<Row>({bytes.data(), n}));

  builder.clear();
  auto projected = builder.finish(builder.endTable(builder.startTable()));
  ZuCheckRT(!ZiFB::verify<Row>(projected));
  ZuCheckRT((ZiFB::verify<Row, false, ZuFacet::Core, ZuAlwaysFalse>(projected)));
  ZuCheckRT(!(ZiFB::verify<Row, false, ZuFacet::Core,
    ZuStructKeyFilter<0>::template Filter>(projected)));
  builder.clear();
  auto bad = builder.startTable();
  builder.field<uint32_t>(bad, ZiFB::Meta<Row>::id<ZfField(Row, name)>(), UINT32_MAX, 0);
  auto malformed = builder.finish(builder.endTable(bad));
  // Excluded fields still require valid wire data when present.
  ZuCheckRT(!(ZiFB::verify<Row, false, ZuFacet::Core, ZuAlwaysFalse>(malformed)));

  builder.clear();
  row.name = {};
  ZiFB::saveObject(builder, row);
  ZuCheckRT(builder.error() == ZiFB::Error::Required);
}

static void scalars()
{
  ZuTestScope(scalars);
  const int64_t narrow[] = {-1, 0x100000002};
  const float floats[] = {-1.25f, 0.0f, 2.5f};
  const double doubles[] = {-1.25, 0.0, 2.5};
  auto buf = ZtScratch(ZiFB::Buffer, 0);
  ZiFB::ArrayStorage<> storage{buf};
  ZiFB::Builder<decltype(storage)> builder{storage};
  auto bytes = builder.finish(ZiFB::saveObject(builder, Scalars{narrow, floats, doubles}));
  ZuCheck(!builder.failed() && ZiFB::verify<Scalars>(bytes));
  auto view = ZiFB::view<Scalars>(bytes);
  auto n = view.get<ZfField(Scalars, narrow)>();
  auto f = view.get<ZfField(Scalars, floats)>();
  auto d = view.get<ZfField(Scalars, doubles)>();
  ZuCheck(n.length() == 2 && n[0] == -1 && n[1] == 2);
  ZuCheck(f.length() == 3 && f[0] == -1.25f && f[1] == 0 && f[2] == 2.5f);
  ZuCheck(d.length() == 3 && d[0] == -1.25 && d[1] == 0 && d[2] == 2.5);
}

static void keyProjection()
{
  ZuTestScope(keyProjection);
  Row row{7, "key", {}, {}};
  auto key = ZuStructKey(row);
  auto buf = ZtScratch(ZiFB::Buffer, 0);
  ZiFB::ArrayStorage<> storage{buf};
  ZiFB::Builder<decltype(storage)> builder{storage};
  auto bytes = builder.finish(ZiFB::saveObject(builder, key));
  ZuCheck(!builder.failed() && ZiFB::verify<Row>(bytes));
  auto view = ZiFB::view<Row>(bytes);
  ZuCheck(view.get<ZfField(Row, name)>() == "key");
  ZuCheck(view.has<ZfField(Row, name)>());
  ZuCheck(!view.has<ZfField(Row, seq)>());
  ZuCheck(!view.has<ZfField(Row, values)>());
}

} // ViewTest

namespace ValueTest {

static void bitmaps()
{
  ZuTestScope(bitmaps);
  Bitmaps row;
  row.fixed.set(0).set(64);
  row.dynamic.set(130);
  row.hw.set(0); // a nonempty hwloc bitmap whose last bit is zero
  const ZuBSpan blobs[] = {ZuBSpan{"bitmap"}};
  row.bytes = blobs;
  auto buf = ZtScratch(ZiFB::Buffer, 0);
  ZiFB::ArrayStorage<> storage{buf};
  ZiFB::Builder<decltype(storage)> builder{storage};
  auto output = builder.finish(ZiFB::saveObject<Bitmaps, ZuFacet::Core,
    ZfFieldFilter::Del>(builder, row));
  ZuCheck(!builder.failed() && ZiFB::verify<Bitmaps>(output));
  auto view = ZiFB::view<Bitmaps>(output);
  auto fixed = view.get<ZfField(Bitmaps, fixed)>();
  auto dynamic = view.get<ZfField(Bitmaps, dynamic)>();
  auto hw = view.get<ZfField(Bitmaps, hw)>();
  ZuCheck(fixed.get(0) && fixed.get(64) && !fixed.get(63));
  ZuCheck(dynamic.get(130) && !dynamic.get(0));
  ZuCheck(hw.get(0) && hw.last() == 0);
  ZuCheck(view.get<ZfField(Bitmaps, bytes)>()[0] == blobs[0]);

  builder.clear();
  row.hw.set(31).set(32).set(63).set(64).set(128);
  output = builder.finish(ZiFB::saveObject(builder, row));
  hw = ZiFB::view<Bitmaps>(output).get<ZfField(Bitmaps, hw)>();
  ZuCheck(hw.get(0) && hw.get(31) && hw.get(32) && hw.get(63) && hw.get(64) && hw.get(128));
  ZuCheck(!hw.get(127));

  builder.clear();
  row.hw.zero();
  output = builder.finish(ZiFB::saveObject(builder, row));
  ZuCheck(!ZiFB::view<Bitmaps>(output).get<ZfField(Bitmaps, hw)>());
  builder.clear();
  row.hw.fill(); // an infinite bitmap has no finite wire representation
  ZiFB::saveObject(builder, row);
  ZuCheck(builder.error() == ZiFB::Error::Value);
}

static void run()
{
  ZuTestScope(value);
  ZuTestCall(bitmaps);
  const int128_t values[] = {-1, 0, int128_t(1) << 100};
  const uint8_t data[] = {0, 1, 0, 255};
  const ZuBSpan bytes[] = {data, {}};
  Row row{-(int128_t(1) << 100), (uint128_t(1) << 127) | 42,
    ZuFixed{1234, 2}, ZuDecimal{ZuDecimal::Unscaled{-123456}},
    ZuTime{1700000000, 123456789},
    ZuDateTime{ZuDateTime::Julian{2460000}, 43210, 987654321},
    values, bytes, {0x10203}};
  auto buf = ZtScratch(ZiFB::Buffer, 0);
  ZiFB::ArrayStorage<> storage{buf};
  ZiFB::Builder<decltype(storage)> builder{storage};
  auto output = builder.finish(ZiFB::saveObject(builder, row));
  ZuCheck(!builder.failed() && ZiFB::verify<Row>(output));
  auto view = ZiFB::view<Row>(output);
  ZuCheck(view.get<ZfField(Row, signedValue)>() == row.signedValue);
  ZuCheck(view.get<ZfField(Row, unsignedValue)>() == row.unsignedValue);
  ZuCheck(view.get<ZfField(Row, fixed)>() == row.fixed);
  ZuCheck(view.get<ZfField(Row, decimal)>() == row.decimal);
  ZuCheck(view.get<ZfField(Row, time)>() == row.time);
  ZuCheck(view.get<ZfField(Row, date)>() == row.date);
  ZuCheck(view.get<ZfField(Row, packed)>().word == row.packed.word);
  auto vector = view.get<ZfField(Row, values)>();
  ZuCheck(vector.length() == 3 && vector[0] == -1 && vector[2] == values[2]);
  auto blobs = view.get<ZfField(Row, bytes)>();
  ZuCheck(blobs.length() == 2 && blobs[0] == ZuBSpan{data} && blobs[1].length() == 0);

  builder.clear();
  auto empty = builder.finish(builder.endTable(builder.startTable()));
  auto absent = ZiFB::view<Row>(empty);
  ZuCheck(absent.get<ZfField(Row, signedValue)>() == ZfField(Row, signedValue)::deflt());
  auto missingTime = absent.get<ZfField(Row, time)>();
  ZuCheck(!*missingTime && missingTime == ZfField(Row, time)::deflt());

}

} // ValueTest

namespace LoadTest {

struct Elem { int value; };
ZfStruct(, Elem, (value, (Ctor<0>, Deflt<0>), Int32));
ZiFB::Table<"test.Elem"> ZiFBType(Elem *);
struct Point { int value = 0; };
ZfStruct(, Point, (value, (Deflt<0>), Int32));
ZiFB::Inline<"test.Point"> ZiFBType(Point *);

template <typename Heap = ZuVoid>
struct Row_ : public Heap {
  unsigned context, id;
  ZtString<ZtStringHeapID<"ZiFBTest.Load.Name">> name;
  ZtArray<Elem, ZtArrayHeapID<"ZiFBTest.Load.Elems">> elems;
  Point point;
  uint64_t changed = 0;
  int unchanged = 0;

  Row_(unsigned context_, unsigned id_, ZuCSpan name_) :
    context{context_}, id{id_}, name{name_} { }
};
ZuDerive(RowHeap, (ZmHeap<"ZiFBTest.Load.Row", Row_<>>));
ZuDerive(Row, (Row_<RowHeap>));
ZfStruct(, Row,
  (id,				(Ctor<0>, Keys<0>, Deflt<0>),	UInt32),
  (name,			(Ctor<1>, Mutable),		String),
  ((retired, Skip, int),	(Ctor<7>, Mutable, Keys<3>),	Int32),
  (elems,,							UDT),
  (point,,							UDT),
  (changed,			(Mutable, Deflt<0>),		UInt64),
  (unchanged,			(Deflt<0>),			Int32));
ZiFB::Table<"test.LoadRow"> ZiFBType(Row *);

static void check(const Row &row, unsigned context)
{
  ZuTestScope(check);
  ZuCheck(row.context == context && row.id == 42 && row.name == "row");
  ZuCheck(row.elems.length() == 2 && row.elems[0].value == 7 && row.elems[1].value == 9);
  ZuCheck(row.point.value == 11 && row.changed == 13 && row.unchanged == 17);
}

struct BoolCtor { bool active; };
ZfStruct(, BoolCtor, (active, (Ctor<0>), Bool));
ZiFB::Table<"test.BoolCtor"> ZiFBType(BoolCtor *);

static void boolean()
{
  ZuTestScope(boolean);
  auto buf = ZtScratch(ZiFB::Buffer, 0);
  ZiFB::ArrayStorage<> storage{buf};
  ZiFB::Builder<decltype(storage)> builder{storage};
  for (bool active : {false, true}) {
    builder.clear();
    auto bytes = builder.finish(ZiFB::saveObject(builder, BoolCtor{active}));
    ZuCheck(ZiFB::verify<BoolCtor>(bytes));
    auto view = ZiFB::view<BoolCtor>(bytes);
    ZuAssert((ZuIsSame<decltype(view.get<ZfField(BoolCtor, active)>()), bool>{}));
    ZuCheck(view.get<ZfField(BoolCtor, active)>() == active);
    auto handler = ZiFB::handler(view);
    ZuCheck(handler.ctor().active == active);
    BoolCtor placed;
    handler.new_(&placed);
    ZuCheck(placed.active == active);
  }
}

static void run()
{
  ZuTestScope(load);
  ZuTestCall(boolean);
  Row source{1, 42, "row"};
  source.elems.push(Elem{7});
  source.elems.push(Elem{9});
  source.point.value = 11;
  source.changed = 13;
  source.unchanged = 17;
  auto buf = ZtScratch(ZiFB::Buffer, 0);
  ZiFB::ArrayStorage<> storage{buf};
  ZiFB::Builder<decltype(storage)> builder{storage};
  auto bytes = builder.finish(ZiFB::saveObject(builder, source));
  ZuCheck(!builder.failed() && ZiFB::verify<Row>(bytes));
  auto h = ZiFB::handler<Row>(bytes);
  auto row = h.ctor(2U);
  ZuTestCall(check, row, 2);
  h.load(row);
  ZuTestCall(check, row, 2);
  auto allocated = h.alloc(3U);
  ZuTestCall(check, *allocated, 3);
  delete allocated;
  alignas(Row) uint8_t memory[sizeof(Row)];
  h.new_(memory, 4U);
  auto placed = ZuLaunder(reinterpret_cast<Row *>(memory));
  ZuTestCall(check, *placed, 4);
  placed->~Row();

  builder.clear();
  source.name = "updated";
  source.changed = 23;
  source.unchanged = 29;
  auto update = builder.finish(ZiFB::saveObject<Row, ZuFacet::Core, ZfFieldFilter::Upd>(builder,
    source));
  ZiFB::handler<Row>(update).update(row);
  ZuCheck(row.name == "updated" && row.changed == 23 && row.unchanged == 17);
  ZuCheck(row.elems.length() == 2 && row.point.value == 11 && row.context == 2);

  builder.clear();
  auto empty = builder.finish(builder.endTable(builder.startTable()));
  auto defaults = ZiFB::handler<Row>(empty).ctor(5U);
  ZuCheck(defaults.context == 5 && defaults.id == 0 && !defaults.name);
  ZuCheck(defaults.point.value == 0 && defaults.elems.length() == 0);
}

} // LoadTest

namespace UnionTest {

static void run()
{
  ZuTestScope(union);
  auto buf = ZtScratch(ZiFB::Buffer, 0);
  ZiFB::ArrayStorage<> storage{buf};
  ZiFB::Builder<decltype(storage)> builder{storage};
  Message message{17, {}};
  message.body.p<2>(Record{42});
  auto output = builder.finish(ZiFB::saveObject(builder, message));
  ZuCheck(!builder.failed() && ZiFB::verify<Message>(output));
  auto body = ZiFB::view<Message>(output).get<ZfField(Message, body)>();
  ZuCheck(body.tag() == 2 && body.get<2>().get<ZfField(Record, value)>() == 42);
  auto native = ZiFB::handler<Message>(output).ctor();
  ZuCheck(native.seq == 17 && native.body.type() == 2 && native.body.p<2>().value == 42);

  builder.clear();
  message.body.p<4>(Point{-23});
  output = builder.finish(ZiFB::saveObject(builder, message));
  ZuCheck(ZiFB::verify<Message>(output));
  ZuCheck(ZiFB::handler<Message>(output).ctor().body.p<4>().value == -23);

  builder.clear();
  message.body.p<3>(Empty{});
  output = builder.finish(ZiFB::saveObject(builder, message));
  ZuCheck(ZiFB::verify<Message>(output));
  ZuCheck(ZiFB::handler<Message>(output).ctor().body.type() == 3);

  builder.clear();
  message.body.null();
  output = builder.finish(ZiFB::saveObject(builder, message));
  ZuCheck(ZiFB::verify<Message>(output));
  ZuCheck(ZiFB::view<Message>(output).get<ZfField(Message, body)>().tag() == 0);

  builder.clear();
  auto payload = ZiFB::saveObject(builder, Record{99});
  auto table = builder.startTable();
  ZiFB::fieldUnion<ZfField(Message, body)>(builder, table, ZiFB::UnionOffset<Body>{payload, 1});
  output = builder.finish(builder.endTable(table));
  ZuCheck(!builder.failed() && ZiFB::verify<Message>(output));
  ZuCheck(ZiFB::view<Message>(output).get<ZfField(Message, body)>().get<1>()
    .get<ZfField(Record, value)>() == 99);

  builder.clear();
  table = builder.startTable();
  builder.field<uint8_t>(table, 1, 2, 0); // tag without a payload
  output = builder.finish(builder.endTable(table));
  ZuCheck(!ZiFB::verify<Message>(output));

  builder.clear();
  table = builder.startTable();
  ZiFB::fieldUnion<ZfField(Message, body)>(builder, table, ZiFB::UnionOffset<Body>{0, 2});
  ZuCheck(builder.error() == ZiFB::Error::Union);
}

} // UnionTest

namespace StreamTest {

template <bool SizePrefix>
static void roundtrip()
{
  ZuTestScope(roundtrip);
  auto text = ZtScratch(Text, 0);
  Stream stream{text};
  Row row{7, {123}, ZuBSpan{"a\0b", 3}};
  ZuCheck(ZiFB::save<SizePrefix>(stream, row, "STRM") == ZiFB::Error::OK);
  ZuBSpan bytes{text};
  ZuCheck((ZiFB::verify<Row, SizePrefix>(bytes, "STRM")));
  auto result = ZiFB::load<Row, SizePrefix>(bytes);
  ZuCheck(result.seq == 7 && result.stamp.value == 123);
  ZuCheck(result.bytes == row.bytes);
  auto view = ZiFB::view<Row, SizePrefix>(bytes);
  ZuCheck(!view.template has<ZfField(Row, seq)>());
  ZuCheck(result.bytes.data() == view.template get<ZfField(Row, bytes)>().data());

  // Invalid required data never publishes a partially constructed span.
  auto before = text.length();
  row.bytes = {};
  ZuCheck(ZiFB::save<SizePrefix>(stream, row) == ZiFB::Error::Required);
  ZuCheck(text.length() == before);
}

} // StreamTest

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(MetaTest::run);
  ZuTestCall(WireTest::borrowed);
  ZuTestCall(WireTest::forward);
  ZuTestCall(WireTest::prefix);
  ZuTestCall(WireTest::malformed);
  ZuTestCall(WireTest::emptyVector);
  ZuTestCall(WireTest::bounds);
  ZuTestCall(WireTest::unaligned);
  ZuTestCall(BuilderTest::tables);
  ZuTestCall(BuilderTest::nesting);
  ZuTestCall(BuilderTest::growth);
  ZuTestCall(BuilderTest::alignment);
  ZuTestCall(ViewTest::keyProjection);
  ZuTestCall(ViewTest::payload);
  ZuTestCall(ViewTest::scalars);
  ZuTestCall(ValueTest::run);
  ZuTestCall(LoadTest::run);
  ZuTestCall(UnionTest::run);
  ZuTestCall((StreamTest::roundtrip<false>));
  ZuTestCall((StreamTest::roundtrip<true>));
}
