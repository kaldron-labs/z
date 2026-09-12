//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <alloca.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmStackAvail.hh>
#include <zlib/ZmThread.hh>
#include <zlib/ZdbPQ.hh>

using namespace ZuTestUtil;

static void vectorLayout()
{
  ZuTestScope(vectorLayout);
  ZuCHECK(ZdbPQ::vecSize(0, 8) == sizeof(ZdbPQ::VecHdr),
      "empty vector header size");
  ZuCHECK(ZdbPQ::vecSize(3, 8) ==
      sizeof(ZdbPQ::VecHdr) + 3 * (sizeof(ZdbPQ::VecElem) + 8),
      "fixed vector wire size");
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
    fields.push(ZdbPQ::XField{ZuMv(id), nullptr, nullptr, 0});
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
    Zfb::Builder fbb;
    ZdbPQ::SavedOffsets saved;
    saved.size(1);
    ZdbPQ::Offsets offsets(saved.data());
    ZdbPQ::saveOffset<I>(fbb, offsets, value);
    auto value_ = offsets.shift();
    ZuCHECK(value_.offset.o != 0, "saved IP offset");
    ZuCHECK(value_.ipType == uint8_t(ZfbTransform::IP::type(ip)),
	"saved IP discriminator");
  }
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(vectorLayout);
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
