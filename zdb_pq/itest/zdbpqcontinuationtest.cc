//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdlib.h>

#include <iostream>

#include <zlib/ZuSort.hh>
#include <zlib/ZuTestUtil.hh>

#include <zlib/ZdbPQ.hh>

using namespace ZuTestUtil;

struct Row {
  int64_t a;
  int64_t b;
  int64_t c;
  int64_t d;

  int64_t operator [](unsigned i) const {
    switch (i) {
      case 0: return a;
      case 1: return b;
      case 2: return c;
      default: return d;
    }
  }
};

ZuDerive(Rows, (ZtArray<Row, ZtArrayHeapID<"ZdbPQ.ContRows">>));

static ZdbPQ::XFields fields(unsigned n)
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

static ZdbPQ::XKey key(unsigned n, uint64_t mask)
{
  ZdbPQ::XKey key;
  key.fields.size(n);
  unsigned descending = 0;
  for (unsigned i = 0; i < n; i++) {
    bool descend = mask & (uint64_t(1) << i);
    descending += descend;
    key.fields.push(ZdbPQ::XKeyField{
      ZdbPQ::XFieldIx(i), uint8_t(descend)});
  }
  key.descending = descending;
  key.direction = !descending ? ZdbPQ::XKey::Ascending :
    descending == n ? ZdbPQ::XKey::Descending : ZdbPQ::XKey::Mixed;
  return key;
}

static int compare(
  const Row &l, const Row &r, const ZdbPQ::XKey &key, unsigned begin)
{
  unsigned n = key.fields.length();
  for (unsigned i = begin; i < n; i++) {
    int cmp = ZuCmp<int64_t>::cmp(l[i], r[i]);
    if (cmp) return key.fields[i].descend() ? -cmp : cmp;
  }
  return 0;
}

static bool selected(
  const Row &row, const ZdbPQ::XKey &key,
  unsigned begin, bool inclusive)
{
  Row cursor{1, 1, 1, 1};
  for (unsigned i = 0; i < begin; i++)
    if (row[i] != cursor[i]) return false;
  if (begin >= key.fields.length()) return true;
  int cmp = compare(row, cursor, key, begin);
  return cmp > 0 || (inclusive && !cmp);
}

static PGresult *exec(PGconn *conn, const char *sql)
{
  PGresult *res = PQexec(conn, sql);
  auto status = PQresultStatus(res);
  if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK) {
    std::cerr << PQerrorMessage(conn);
    PQclear(res);
    return nullptr;
  }
  return res;
}

static PGresult *exec(PGconn *conn, const ZdbPQ::SQLString &sql)
{
  return exec(conn, sql.data());
}

static Rows fixture()
{
  Rows rows;
  rows.size(81);
  for (int64_t a = 0; a < 3; a++)
    for (int64_t b = 0; b < 3; b++)
      for (int64_t c = 0; c < 3; c++)
	for (int64_t d = 0; d < 3; d++)
	  rows.push(Row{a, b, c, d});
  return rows;
}

static bool check(
  PGconn *conn, const Rows &rows, unsigned n,
  uint64_t mask, unsigned begin, bool inclusive)
{
  auto fields_ = fields(n);
  auto key_ = key(n, mask);
  ZdbPQ::SQLString sql;
  sql << "SELECT ";
  for (unsigned i = 0; i < n; i++) {
    if (i) sql << ',';
    sql << '"' << fields_[i].id_ << '"';
  }
  sql << " FROM zdbpq_cont WHERE ";
  for (unsigned i = 0; i < begin; i++) {
    if (i) sql << " AND ";
    sql << '"' << fields_[i].id_ << "\"=1::int8";
  }
  if (begin < n) {
    if (begin) sql << " AND ";
    ZdbPQ::continuation(sql, fields_, key_, begin, inclusive,
      [](ZdbPQ::SQLString &sql, unsigned) { sql << "1::int8"; });
  }
  if (begin < n) {
    sql << " ORDER BY ";
    for (unsigned i = begin; i < n; i++) {
      if (i > begin) sql << ',';
      sql << '"' << fields_[i].id_ << '"';
      if (key_.fields[i].descend()) sql << " DESC";
    }
  }
  PGresult *res = exec(conn, sql);
  if (!res) return false;

  Rows expected;
  expected.size(rows.length());
  rows.all([&expected, &key_, begin, inclusive](const Row &row) {
    if (selected(row, key_, begin, inclusive)) expected.push(row);
  });
  ZuSort(expected.data(), expected.length(),
    [&key_, begin](const Row &l, const Row &r) {
      return compare(l, r, key_, begin);
    });

  bool matched = unsigned(PQntuples(res)) == expected.length() &&
    unsigned(PQnfields(res)) == n;
  for (unsigned i = 0; matched && i < expected.length(); i++)
    for (unsigned j = 0; j < n; j++)
      if (strtoll(PQgetvalue(res, i, j), nullptr, 10) != expected[i][j]) {
	matched = false;
	break;
      }
  PQclear(res);
  return matched;
}

static void continuations()
{
  ZuTestScope(continuations);
  const char *connect = getenv("ZDB_CONNECT");
  if (!connect) {
    ZuCHECK(true, "ZDB_CONNECT not set - skipped");
    return;
  }
  PGconn *conn = PQconnectdb(connect);
  ZuCHECK(PQstatus(conn) == CONNECTION_OK, "connect");
  if (PQstatus(conn) != CONNECTION_OK) {
    PQfinish(conn);
    return;
  }
  PGresult *res = exec(conn,
    "CREATE TEMP TABLE zdbpq_cont AS SELECT "
    "a::int8, b::int8, c::int8, d::int8 "
    "FROM generate_series(0, 2) a, generate_series(0, 2) b, "
    "generate_series(0, 2) c, generate_series(0, 2) d");
  ZuCHECK(res, "fixture");
  if (!res) {
    PQfinish(conn);
    return;
  }
  PQclear(res);
  auto rows = fixture();
  bool matched = true;
  unsigned queries = 0;
  for (unsigned n = 1; n <= 4; n++)
    for (uint64_t mask = 0; mask < (uint64_t(1) << n); mask++)
      for (unsigned begin = 0; begin <= n; begin++)
	for (bool inclusive : {false, true}) {
	  matched &= check(conn, rows, n, mask, begin, inclusive);
	  ++queries;
	}
  ZuCHECK(matched, "PostgreSQL results match lexicographic oracle");
  ZuCHECK(queries == 256, "all direction/group/inclusive combinations");
  PQfinish(conn);
}

int main()
{
  ZuTestMain();
  ZuTestCall(continuations);
  return 0;
}
