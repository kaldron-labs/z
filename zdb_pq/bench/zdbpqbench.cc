//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdlib.h>

#include <iostream>

#include <zlib/ZdbPQ.hh>

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

static ZdbPQ::XKey key(unsigned n)
{
  ZdbPQ::XKey key;
  key.fields.size(n);
  for (unsigned i = 0; i < n; i++)
    key.fields.push(ZdbPQ::XKeyField{
      ZdbPQ::XFieldIx(i), uint8_t(i & 1)});
  key.descending = n>>1;
  key.direction = ZdbPQ::XKey::Mixed;
  return key;
}

static uint64_t value(unsigned i, uint64_t base)
{
  switch (i) {
    case 0: return base;
    case 1: return base % 101;
    case 2: return base % 97;
    case 3: return base % 89;
    case 4: return base % 83;
    case 5: return base % 79;
    case 6: return base % 73;
    default: return base % 71;
  }
}

static ZdbPQ::SQLString predicate(unsigned n, uint64_t base = 0)
{
  auto fields_ = fields(n);
  auto key_ = key(n);
  ZdbPQ::SQLString sql;
  ZdbPQ::continuation(sql, fields_, key_, 0, false,
    [base](ZdbPQ::SQLString &sql, unsigned i) {
      sql << ZuBoxed(value(i, base)) << "::int8";
    });
  return sql;
}

static void exec(PGconn *conn, const char *sql)
{
  PGresult *res = PQexec(conn, sql);
  auto status = PQresultStatus(res);
  if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK) {
    std::cerr << PQerrorMessage(conn);
    PQclear(res);
    exit(1);
  }
  if (status == PGRES_TUPLES_OK)
    for (int i = 0, n = PQntuples(res); i < n; i++)
      std::cout << PQgetvalue(res, i, 0) << '\n';
  PQclear(res);
}

static void exec(PGconn *conn, const ZdbPQ::SQLString &sql)
{
  exec(conn, sql.data());
}

static void explain(PGconn *conn)
{
  exec(conn,
    "CREATE TEMP TABLE zdbpq_bench AS SELECT "
    "s::int8 a, (s % 101)::int8 b, (s % 97)::int8 c, "
    "(s % 89)::int8 d, (s % 83)::int8 e, (s % 79)::int8 f, "
    "(s % 73)::int8 g, (s % 71)::int8 h "
    "FROM generate_series(1, 100000) s");
  exec(conn,
    "CREATE INDEX zdbpq_bench_idx ON zdbpq_bench "
    "(a, b DESC, c, d DESC, e, f DESC, g, h DESC)");
  exec(conn, "ANALYZE zdbpq_bench");
  for (uint64_t base : {uint64_t(0), uint64_t(50000)}) {
    std::cout << (base ? "deep continuation\n" : "initial continuation\n");
    auto pred = predicate(8, base);
    ZdbPQ::SQLString sql;
    sql << "EXPLAIN (ANALYZE, BUFFERS) SELECT a FROM zdbpq_bench WHERE "
      << pred << " ORDER BY a, b DESC, c, d DESC, e, f DESC, g, h DESC "
      "LIMIT 100";
    exec(conn, sql);
  }
}

int main(int argc, char **argv)
{
  for (unsigned n : {2U, 4U, 8U}) {
    auto sql = predicate(n);
    std::cout << "fields=" << n << " bytes=" << sql.length()
      << " sql=" << sql << '\n';
  }
  if (argc < 2) return 0;
  PGconn *conn = PQconnectdb(argv[1]);
  if (PQstatus(conn) != CONNECTION_OK) {
    std::cerr << PQerrorMessage(conn);
    PQfinish(conn);
    return 1;
  }
  explain(conn);
  PQfinish(conn);
  return 0;
}
