//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <alloca.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmStackAvail.hh>
#include <zlib/ZmThread.hh>
#include <zlib/ZtScratch.hh>
#include <zlib/ZfbReflect.hh>
#include <zlib/ZdbPQ.hh>

using namespace ZuTestUtil;

struct AlignedRow { ZuBSpan data; };
ZfStruct(, (AlignedRow, FB), (data, (FB::Align<32>), Bytes));
Zfb::AsTable<"test.AlignedRow"> Zfb_Fmt(AlignedRow *);

struct FloatRow {
  float real;
  ZtArray<float> values;
  ZtArray<const char *> names;
};
ZfStruct(, (FloatRow, FB),
  (real, (Ctor<0>, Deflt<1.5>), Float),
  (values, (Ctor<1>), FloatVec),
  (names, (Ctor<2>), CStringVec));
Zfb::AsTable<"test.FloatRow"> Zfb_Fmt(FloatRow *);

struct BoundRow {
  ZuCSpan text;
  ZuBSpan bytes;
  ZtArray<int8_t> a;
  ZtArray<int8_t> b;
};
ZfStruct(, (BoundRow, FB),
  (text,, String), (bytes,, Bytes), (a,, Int8Vec), (b,, Int8Vec));
Zfb::AsTable<"test.BoundRow"> Zfb_Fmt(BoundRow *);

static void paramPlanning()
{
  ZuTestScope(paramPlanning);
  namespace R = Zfb::Reflect;
  auto schemaBytes = ZtScratch(Zfb::Buffer, 0);
  Zfb::saveSchema<BoundRow>(schemaBytes);
  auto schema = Zdb_::Schema::from<BoundRow>(schemaBytes);
  auto native = ZfVFields<BoundRow>();
  auto fields = ZtScratch(ZdbPQ::XFields, 4);
  fields.push(ZdbPQ::XField{"text", native[0], schema.field("text"),
    ZdbPQ::Value::Index<ZdbPQ::String>{}});
  fields.push(ZdbPQ::XField{"bytes", native[1], schema.field("bytes"),
    ZdbPQ::Value::Index<ZdbPQ::Bytes>{}});
  fields.push(ZdbPQ::XField{"a", native[2], schema.field("a"),
    ZdbPQ::Value::Index<ZdbPQ::Int8Vec>{}});
  fields.push(ZdbPQ::XField{"b", native[3], schema.field("b"),
    ZdbPQ::Value::Index<ZdbPQ::Int8Vec>{}});
  const uint8_t byte[] = {1};
  BoundRow row{"x", byte, {1}, {1}};
  Zfb::IOBuilder<> input{new ZiIOBufAlloc<>};
  input.finish(Zfb::save(input, row));
  auto source = input.buf();
  Zfb::TableView table{Zfb::root(source->cspan())};
  auto parts = ZtScratch(ZdbPQ::VarBufParts, 2);
  auto plan = [&parts, &fields, table]() {
    parts.length(0);
    return ZdbPQ::planTuple(parts, 4, fields, fields, table);
  };
  ZuCheck(plan() == 2 * ZdbPQ::vecSize(1, 1));
  // Synthetic wire length words exercise planning only, never verification or encoding.
  auto length = [&fields, table](unsigned i, unsigned n) {
    auto p = const_cast<uint8_t *>(Zfb::follow(R::address(table, fields[i].field)));
    Zfb::write<uint32_t>(p, n);
  };
  enum { Limit = Zdb_::MaxBufSize };
  length(0, Limit + 1);
  ZuCheck(plan() == UINT_MAX && !parts.length());
  length(0, 1);
  length(1, Limit + 1);
  ZuCheck(plan() == UINT_MAX && !parts.length());
  length(1, 1);
  unsigned n = (Limit - sizeof(ZdbPQ::VecHdr))/(sizeof(ZdbPQ::VecElem) + 1);
  length(2, n + 1);
  ZuCheck(plan() == UINT_MAX && !parts.length());
  length(2, n);
  length(3, n);
  ZuCheck(plan() == UINT_MAX && parts.length() == 1);
  length(2, 1);
  length(3, 1);
  ZuCheck(plan() == 2 * ZdbPQ::vecSize(1, 1));
}

static void floatWire()
{
  ZuTestScope(floatWire);
  auto schemaBytes = ZtScratch(Zfb::Buffer, 0);
  Zfb::saveSchema<FloatRow>(schemaBytes);
  auto schema = Zdb_::Schema::from<FloatRow>(schemaBytes);
  auto native = ZfVFields<FloatRow>();
  auto fields = ZtScratch(ZdbPQ::XFields, 3);
  fields.push(ZdbPQ::XField{"real", native[0], schema.field("real"),
    ZdbPQ::Value::Index<ZdbPQ::Float>{}, 4});
  fields.push(ZdbPQ::XField{"values", native[1], schema.field("values"),
    ZdbPQ::Value::Index<ZdbPQ::FloatVec>{}, 4});
  fields.push(ZdbPQ::XField{"names", native[2], schema.field("names"),
    ZdbPQ::Value::Index<ZdbPQ::StringVec>{}, 4});
  ZdbPQ::OIDs oids;
  oids.init(ZdbPQ::Value::Index<ZdbPQ::Float>{}, 701);
  oids.init(ZdbPQ::Value::Index<ZdbPQ::String>{}, 25);
  for (float real: {1.5f, -2.5f}) {
    FloatRow row{real, {-0.0f, 1.25f, -2.5f},
      {"", "short", "a CStringVec element longer than builtin string storage"}};
    Zfb::IOBuilder<> input{new ZiIOBufAlloc<>};
    input.finish(Zfb::save(input, row));
    auto source = input.buf();
    Zfb::TableView fbo{Zfb::root(source->cspan())};
    auto parts = ZtScratch(ZdbPQ::VarBufParts, 2);
    unsigned size = ZdbPQ::planTuple(parts, 3, fields, fields, fbo);
    ZuCHECK(size != UINT_MAX, "plan Float32 and CStringVec parameters");
    if (size == UINT_MAX) continue;
    auto bytes = ZtScratch(ZdbPQ::VarBuf, size, size);
    auto values = ZtScratch(ZdbPQ::Tuple, 3);
    ZdbPQ::loadTuple(values, bytes.span(), parts.cspan(), oids,
      3, fields, fields, fbo);
    ZuCHECK(double(values[0].p<ZdbPQ::Float>().v) == real,
      "Float32 scalar widens including omitted default");
    ZuCHECK(values[1].p<ZdbPQ::FloatVec>().v.length() == ZdbPQ::vecSize(3, 8),
      "Float32 vector uses double-width postgres encoding");
    ZdbPQ::Builder output{new ZiIOBufAlloc<>};
    output.finish(ZdbPQ::saveTuple(output, fields, fields, 2, values));
    auto dest = output.buf();
    ZuCHECK(dest && Zfb::verify<FloatRow>(dest->cspan()), "Float32 result wire layout");
    if (!dest) continue;
    auto view = Zfb::view<FloatRow>(dest->cspan());
    ZuCHECK(view.get<ZfField(FloatRow, real)>() == real, "Float32 scalar round trip");
    auto reals = view.get<ZfField(FloatRow, values)>();
    ZuCHECK(reals.length() == 3 && reals[0] == 0 &&
      reals[1] == 1.25f && reals[2] == -2.5f, "Float32 vector stride round trip");
    auto names = view.get<ZfField(FloatRow, names)>();
    ZuCHECK(names.length() == 3 && ZuCSpan{names[0]} == "" && ZuCSpan{names[1]} == "short" &&
      ZuCSpan{names[2]} == "a CStringVec element longer than builtin string storage",
      "CStringVec borrowed input round trip");
  }
}

static void fieldAlignment()
{
  ZuTestScope(fieldAlignment);
  namespace R = Zfb::Reflect;
  auto bytes = ZtScratch(Zfb::Buffer, 0);
  Zfb::saveSchema<AlignedRow>(bytes);
  auto schema = Zdb_::Schema::from<AlignedRow>(bytes);
  auto field = schema.field("data");
  unsigned align = R::alignment(field);
  ZuCHECK(align == 32, "resolve forced alignment");
  auto fields = ZtScratch(ZdbPQ::XFields, 1);
  fields.push(ZdbPQ::XField{
    "data", nullptr, field, ZdbPQ::Value::Index<ZdbPQ::Bytes>{}, align});
  auto values = ZtScratch(ZdbPQ::Tuple, 1);
  ZuBSpan data{"alignment"};
  values.push(ZdbPQ::Value{data});
  ZdbPQ::Builder builder{new ZiIOBufAlloc<>};
  builder.finish(ZdbPQ::saveTuple(builder, fields, fields, 1, values));
  auto buf = builder.buf();
  ZuCHECK(buf && Zfb::verify<AlignedRow>(buf->cspan()), "aligned store output");
  if (!buf) return;
  auto result = Zfb::view<AlignedRow>(buf->cspan()).get<ZfField(AlignedRow, data)>();
  ZuCHECK(result == data, "preserve bytes");
  ZuCHECK(!(uintptr_t(result.data()) & (align - 1)), "cached alignment reaches writer");
}

static void vectorLayout()
{
  ZuTestScope(vectorLayout);
  ZuCHECK(ZdbPQ::vecSize(0, 8) == sizeof(ZdbPQ::VecHdr),
      "empty vector header size");
  ZuCHECK(ZdbPQ::vecSize(3, 8) ==
      sizeof(ZdbPQ::VecHdr) + 3 * (sizeof(ZdbPQ::VecElem) + 8),
      "fixed vector wire size");
}

static void paramBounds()
{
  ZuTestScope(paramBounds);
  auto rejected = [](auto fn) { return fn() == UINT_MAX; };
  enum { Limit = Zdb_::MaxBufSize };
  ZuCheck(ZdbPQ::sizeAdd(0, Limit) == unsigned(Limit));
  ZuCheck(rejected([] { return ZdbPQ::sizeAdd(0, uint64_t(Limit) + 1); }));
  ZuCheck(ZdbPQ::sizeAdd(Limit - 1, 1) == unsigned(Limit));
  ZuCheck(rejected([] { return ZdbPQ::sizeAdd(Limit, 1); }));
  ZuCheck(rejected([] { return ZdbPQ::sizeAdd(UINT64_MAX, 1); }));
  ZuCheck(rejected([] { return ZdbPQ::sizeAdd(1, UINT64_MAX); }));
  // Distinct fixed-width postgres array encodings, without allocating payloads.
  for (unsigned width: {1U, 2U, 4U, 8U, 12U, 16U}) {
    unsigned n = (Limit - sizeof(ZdbPQ::VecHdr))/(sizeof(ZdbPQ::VecElem) + width);
    ZuCheck(ZdbPQ::vecSize(n, width) ==
      sizeof(ZdbPQ::VecHdr) + n * (sizeof(ZdbPQ::VecElem) + width));
    ZuCheck(rejected([n, width] { return ZdbPQ::vecSize(n + 1, width); }));
  }
  ZuCheck(rejected([] { return ZdbPQ::vecSize(UINT_MAX, UINT_MAX); }));
  ZuCheck(ZdbPQ::vecVarSize(2, [](unsigned i) { return i + 1; }) ==
    sizeof(ZdbPQ::VecHdr) + 2 * sizeof(ZdbPQ::VecElem) + 3);
  unsigned head = sizeof(ZdbPQ::VecHdr) + sizeof(ZdbPQ::VecElem);
  ZuCheck(ZdbPQ::vecVarSize(1, [head](unsigned) { return Limit - head; }) ==
    unsigned(Limit));
  ZuCheck(rejected([head] {
    return ZdbPQ::vecVarSize(1, [head](unsigned) { return uint64_t(Limit) - head + 1; });
  }));
  ZuCheck(rejected([] {
    return ZdbPQ::vecVarSize(2, [](unsigned) { return unsigned(Limit)/2; });
  }));
  ZuCheck(rejected([] {
    return ZdbPQ::vecVarSize(1, [](unsigned) { return UINT64_MAX; });
  }));
}

static void paramLayout()
{
  ZuTestScope(paramLayout);
  for (unsigned n : {0U, 1U, 32U, Zdb_::maxFields() + 4}) {
    for (bool types : {false, true}) {
      auto words = ZdbPQ::ParamLayout::words(n, types);
      auto buf = ZmScratch(
	uintptr_t, words, ZdbPQ::QueryParams::VHeap);
      ZdbPQ::ParamLayout layout(buf.data(), n, types);
      if (!n) {
	ZuCHECK(!layout.oids && !layout.values &&
	    !layout.lengths && !layout.formats, "zero parameter layout");
	continue;
      }
      ZuCHECK(bool(layout.oids) == types, "optional OID region");
      ZuCHECK(layout.values && layout.lengths && layout.formats,
	  "parameter regions");
      ZuCHECK(reinterpret_cast<uintptr_t>(layout.values) %
	  alignof(const char *) == 0, "value alignment");
      ZuCHECK(reinterpret_cast<uintptr_t>(layout.lengths) %
	  alignof(int) == 0, "length alignment");
      ZuCHECK(reinterpret_cast<uintptr_t>(layout.formats) %
	  alignof(int) == 0, "format alignment");
    }
  }
}

static void paramFallback()
{
  ZuTestScope(paramFallback);
  bool queryHeap = false, prepHeap = false;
  bool resultHeap = false, offsetHeap = false;
  ZmThread thread{[
      &queryHeap, &prepHeap, &resultHeap, &offsetHeap]() {
    unsigned avail = ZmStackAvail();
    if (avail > 4096) {
      auto pad = static_cast<volatile uint8_t *>(alloca(avail - 4096));
      pad[0] = 0;
    }
    unsigned n = Zdb_::maxFields() + 4;
    auto self = ZmSelf();
    auto stack = reinterpret_cast<uint8_t *>(self->stackAddr());
    auto end = stack + self->stackSize();
    auto outside = [stack, end](const void *ptr_) {
      auto ptr = reinterpret_cast<const uint8_t *>(ptr_);
      return ptr < stack || ptr >= end;
    };
    unsigned queryWords = ZdbPQ::ParamLayout::words(n, true);
    auto query = ZmScratch(
      uintptr_t, queryWords, ZdbPQ::QueryParams::VHeap);
    queryHeap = outside(query.data());
    unsigned prepWords = ZdbPQ::ParamLayout::words(n, false);
    auto prep = ZmScratch(
      uintptr_t, prepWords, ZdbPQ::PrepParams::VHeap);
    prepHeap = outside(prep.data());
    auto result = ZmScratch(
      ZdbPQ::Value, n, ZdbPQ::ResultTuple::VHeap);
    resultHeap = outside(result.data());
    auto offsets = ZmScratch(
      ZdbPQ::SavedOffset, n, ZdbPQ::SavedOffsets::VHeap);
    offsetHeap = outside(offsets.data());
  }, ZmThreadParams{}.name("pqScratch").stackSize(64U<<10)};
  ZuCHECK(!thread.join(), "scratch worker");
  ZuCHECK(queryHeap, "query parameter fallback");
  ZuCHECK(prepHeap, "prepared parameter fallback");
  ZuCHECK(resultHeap, "result tuple fallback");
  ZuCHECK(offsetHeap, "saved offset fallback");
}

static ZdbPQ::XFields testFields(unsigned n)
{
  ZdbPQ::XFields fields;
  fields.size(n);
  for (unsigned i = 0; i < n; i++) {
    ZdbPQ::IDString id;
    id << char('a' + i);
    fields.push(ZdbPQ::XField{ZuMv(id), nullptr, {}, 0});
  }
  return fields;
}

static ZdbPQ::XKey testKey(unsigned n, uint64_t descending)
{
  ZdbPQ::XKey key;
  key.fields.size(n);
  unsigned count = 0;
  for (unsigned i = 0; i < n; i++) {
    bool descend = descending & (uint64_t(1) << i);
    count += descend;
    key.fields.push(ZdbPQ::XKeyField{
      ZdbPQ::XFieldIx(i), uint8_t(descend)});
  }
  key.descending = count;
  key.direction = !count ? ZdbPQ::XKey::Ascending :
    count == n ? ZdbPQ::XKey::Descending : ZdbPQ::XKey::Mixed;
  return key;
}

static void continuationSQL()
{
  ZuTestScope(continuationSQL);
  auto fields = testFields(3);
  auto param = [](ZdbPQ::SQLString &sql, unsigned i) {
    sql << '$' << (i + 1) << "::int8";
  };
  {
    auto key = testKey(3, 0);
    ZdbPQ::SQLString sql;
    ZdbPQ::continuation(sql, fields, key, 0, false, param);
    ZuCHECK(sql == "(\"a\",\"b\",\"c\")>("
      "$1::int8,$2::int8,$3::int8)", "ascending continuation");
  }
  {
    auto key = testKey(3, 7);
    ZdbPQ::SQLString sql;
    ZdbPQ::continuation(sql, fields, key, 1, true, param);
    ZuCHECK(sql == "(\"b\",\"c\")<=("
      "$2::int8,$3::int8)", "descending continuation");
  }
  {
    auto key = testKey(3, 2);
    ZdbPQ::SQLString sql;
    ZdbPQ::continuation(sql, fields, key, 0, true, param);
    ZuCHECK(sql == "\"a\">=$1::int8 AND ("
      "\"a\">$1::int8 OR (\"a\"=$1::int8 AND ("
      "\"b\"<$2::int8 OR (\"b\"=$2::int8 AND ("
      "\"c\">=$3::int8)))))", "mixed continuation");
  }
}

static void continuationGrowth()
{
  ZuTestScope(continuationGrowth);
  unsigned prior = 0;
  auto param = [](ZdbPQ::SQLString &sql, unsigned i) {
    sql << '$' << (i + 1) << "::int8";
  };
  for (unsigned n = 2; n <= 8; n++) {
    auto fields = testFields(n);
    auto key = testKey(n, 0x2aa);
    ZdbPQ::SQLString sql;
    ZdbPQ::continuation(sql, fields, key, 0, false, param);
    unsigned length = sql.length();
    if (prior)
      ZuCHECK(length - prior < 50, "linear continuation growth");
    prior = length;
  }
}

static void continuationCombinations()
{
  ZuTestScope(continuationCombinations);
  auto param = [](ZdbPQ::SQLString &sql, unsigned i) {
    sql << '$' << (i + 1) << "::int8";
  };
  for (unsigned n = 1; n <= 4; n++) {
    auto fields = testFields(n);
    for (uint64_t mask = 0; mask < (uint64_t(1) << n); mask++) {
      auto key = testKey(n, mask);
      for (unsigned begin = 0; begin <= n; begin++)
	for (bool inclusive : {false, true}) {
	  ZdbPQ::SQLString sql;
	  ZdbPQ::continuation(
	    sql, fields, key, begin, inclusive, param);
	  unsigned opens = 0, closes = 0, params = 0;
	  for (char c : ZuCSpan{sql}) {
	    opens += c == '(';
	    closes += c == ')';
	    params += c == '$';
	  }
	  ZuCHECK(opens == closes, "balanced continuation expression");
	  unsigned m = n - begin;
	  unsigned expected = begin == n ? 0 :
	    key.direction == ZdbPQ::XKey::Mixed ?
	    2 * m : m;
	  ZuCHECK(params == expected, "linear parameter references");
	}
    }
  }
}

static void catalogMatching()
{
  ZuTestScope(catalogMatching);
  auto fields = testFields(2);
  auto key = testKey(2, 2);
  key.fields[1].flags |= ZdbPQ::XKeyField::IndexDesc;
  ZdbPQ::IndexRow valid{
    .indexID = "table_0",
    .fieldID = "a",
    .oid = 20,
    .field = 1,
    .nKey = 2,
    .nTotal = 2,
    .unique = true,
    .descending = false
  };
  auto matches = [&fields, &key](const ZdbPQ::IndexRow &row,
      bool oidMatch = true) {
    ZdbPQ::IndexMatch match;
    return ZdbPQ::matchIndex(
      match, "table_0", fields, key, 0, row, oidMatch);
  };
  ZuCHECK(matches(valid), "valid first catalog row");
  {
    ZdbPQ::IndexMatch match;
    ZuCHECK(ZdbPQ::matchIndex(
	match, "table_0", fields, key, 0, valid, true), "first field");
    auto second = valid;
    second.fieldID = "b";
    second.field = 2;
    second.descending = true;
    ZuCHECK(ZdbPQ::matchIndex(
	match, "table_0", fields, key, 0, second, true), "second field");
    ZuCHECK(match.next == 2, "complete index");
    ZuCHECK(!ZdbPQ::matchIndex(
	match, "table_0", fields, key, 0, second, true), "duplicate row");
  }
  {
    ZdbPQ::IndexMatch missing;
    ZuCHECK(ZuNull(missing.next), "missing index sentinel");
  }
#define Mismatch(member, value, label) do { \
    auto row = valid; \
    row.member = value; \
    ZuCHECK(!matches(row), label); \
  } while (false)
  Mismatch(indexID, ZuCSpan{"table_1"}, "index name mismatch");
  Mismatch(unique, false, "uniqueness mismatch");
  Mismatch(nKey, 1U, "key field count mismatch");
  Mismatch(nTotal, 3U, "included field mismatch");
  Mismatch(field, 2U, "ordinal gap");
  Mismatch(fieldID, ZuCSpan{"b"}, "field name mismatch");
  Mismatch(descending, true, "direction mismatch");
#undef Mismatch
  ZuCHECK(!matches(valid, false), "OID type mismatch");
}

static void catalogScale()
{
  ZuTestScope(catalogScale);
  auto fields = testFields(1);
  auto key = testKey(1, 0);
  for (unsigned i = 0; i < Zdb_::maxKeys(); i++) {
    ZdbPQ::IDString id;
    id << "table_" << ZuBoxed(i);
    ZdbPQ::IndexRow row{
      .indexID = id,
      .fieldID = "a",
      .oid = 20,
      .field = 1,
      .nKey = 1,
      .nTotal = 1,
      .unique = !i,
      .descending = false
    };
    ZdbPQ::IndexMatch match;
    ZuCHECK(ZdbPQ::matchIndex(
	match, id, fields, key, Zdb_::KeyID(i), row, true),
	"maximum-key catalog match");
  }
}

static void ipOffset()
{
  ZuTestScope(ipOffset);
  for (ZiIP ip : {ZiIP{"192.0.2.1"}, ZiIP{"2001:db8::1"}}) {
    ZdbPQ::IP packed;
    ZuCHECK(packed.ziIP(ip), "pack IP");
    constexpr unsigned I = ZdbPQ::Value::Index<ZdbPQ::IP>{};
    ZdbPQ::Value value{packed};
    ZdbPQ::Builder fbb{new ZiIOBufAlloc<>()};
    ZdbPQ::SavedOffset saved;
    ZdbPQ::Offsets offsets{&saved};
    ZdbPQ::saveOffset<I>(fbb, offsets, {}, 1, value);
    auto value_ = offsets.shift();
    ZuCHECK(value_.offset != 0, "saved IP offset");
    ZuCHECK(value_.ipType == ip.type(),
	"saved IP discriminator");
  }
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(paramPlanning);
  ZuTestCall(floatWire);
  ZuTestCall(fieldAlignment);
  ZuTestCall(vectorLayout);
  ZuTestCall(paramBounds);
  ZuTestCall(paramLayout);
  ZuTestCall(paramFallback);
  ZuTestCall(continuationSQL);
  ZuTestCall(continuationGrowth);
  ZuTestCall(continuationCombinations);
  ZuTestCall(catalogMatching);
  ZuTestCall(catalogScale);
  ZuTestCall(ipOffset);
  return 0;
}
