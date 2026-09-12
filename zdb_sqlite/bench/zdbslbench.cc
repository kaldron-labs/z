//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sqlite3.h>

#include <zlib/ZtArray.hh>
#include <zlib/ZtString.hh>

using Text = ZtString<ZtStringHeapID<"ZdbSL.BenchText">>;
using Samples = ZtArray<uint64_t, ZtArrayHeapID<"ZdbSL.BenchSamples">>;
using PrimVec = ZtArray<uint32_t, ZtArrayHeapID<"ZdbSL.BenchPrimVec">>;
using Bytes = ZtArray<uint8_t, ZtArrayHeapID<"ZdbSL.BenchBytes">>;

enum { RowCount = 20000, PayloadSize = 4096, PrimCount = 4096 };

static void fail(sqlite3 *db, const char *op)
{
  Text text;
  text << op << ": " << sqlite3_errmsg(db) << '\n';
  write(STDERR_FILENO, text.data(), text.length());
  _exit(1);
}

static uint64_t now()
{
  timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return uint64_t(ts.tv_sec) * UINT64_C(1000000000) + ts.tv_nsec;
}

static int cmpSample(const void *l, const void *r)
{
  uint64_t lhs = *static_cast<const uint64_t *>(l);
  uint64_t rhs = *static_cast<const uint64_t *>(r);
  return lhs < rhs ? -1 : lhs > rhs;
}

static sqlite3_stmt *prepare(sqlite3 *db, const char *sql)
{
  sqlite3_stmt *stmt = nullptr;
  if (sqlite3_prepare_v3(db, sql, -1, SQLITE_PREPARE_PERSISTENT,
      &stmt, nullptr) != SQLITE_OK) fail(db, "prepare");
  return stmt;
}

static void exec(sqlite3 *db, const char *sql)
{
  if (sqlite3_exec(db, sql, nullptr, nullptr, nullptr) != SQLITE_OK)
    fail(db, "exec");
}

static void plan(sqlite3 *db, const char *id, const char *sql)
{
  Text query{"EXPLAIN QUERY PLAN "};
  query << sql;
  sqlite3_stmt *stmt = prepare(db, query.data());
  Text text;
  text << "plan " << id << ": ";
  bool comma = false;
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    if (comma) text << "; ";
    comma = true;
    text << reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3));
  }
  text << '\n';
  write(STDOUT_FILENO, text.data(), text.length());
  sqlite3_finalize(stmt);
}

static void seek(sqlite3 *db, const char *id, const char *sql, unsigned nBind)
{
  enum { NWarm = 64, NMeasure = 1024 };
  sqlite3_stmt *stmt = prepare(db, sql);
  Samples samples{NMeasure};
  samples.length(NMeasure);
  for (unsigned sample = 0; sample < NWarm + NMeasure; ++sample) {
    for (unsigned i = 0; i < nBind; ++i)
      if (sqlite3_bind_int64(stmt, i + 1, 18000 + i) != SQLITE_OK)
        fail(db, "bind seek");
    uint64_t begin = now();
    while (sqlite3_step(stmt) == SQLITE_ROW) { }
    uint64_t elapsed = now() - begin;
    if (sample >= NWarm) samples[sample - NWarm] = elapsed;
    sqlite3_reset(stmt);
  }
  qsort(samples.data(), samples.length(), sizeof(samples[0]), cmpSample);
  Text text;
  text << id << " p50_ns=" << samples[NMeasure / 2]
    << " p99_ns=" << samples[NMeasure * 99 / 100] << '\n';
  write(STDOUT_FILENO, text.data(), text.length());
  sqlite3_finalize(stmt);
  plan(db, id, sql);
}

static void operation(sqlite3 *db, const char *id, sqlite3_stmt *stmt)
{
  enum { NWarm = 64, NMeasure = 1024 };
  Samples samples{NMeasure};
  samples.length(NMeasure);
  for (unsigned sample = 0; sample < NWarm + NMeasure; ++sample) {
    uint64_t begin = now();
    int rc;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) { }
    uint64_t elapsed = now() - begin;
    if (rc != SQLITE_DONE) fail(db, id);
    if (sample >= NWarm) samples[sample - NWarm] = elapsed;
    sqlite3_reset(stmt);
  }
  qsort(samples.data(), samples.length(), sizeof(samples[0]), cmpSample);
  Text text;
  text << id << " p50_ns=" << samples[NMeasure / 2]
    << " p99_ns=" << samples[NMeasure * 99 / 100] << '\n';
  write(STDOUT_FILENO, text.data(), text.length());
}

int main()
{
  char path[] = "/tmp/zdbslbench-XXXXXX";
  int fd = mkstemp(path);
  if (fd < 0) return 1;
  close(fd);
  sqlite3 *db = nullptr;
  if (sqlite3_open_v2(path, &db,
      SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX,
      nullptr) != SQLITE_OK) fail(db, "open");
  exec(db, "PRAGMA journal_mode=WAL; PRAGMA synchronous=OFF;"
    "CREATE TABLE t(k1 INTEGER NOT NULL,k2 INTEGER NOT NULL,"
    "k3 INTEGER NOT NULL,k4 INTEGER NOT NULL,k5 INTEGER NOT NULL,"
    "k6 INTEGER NOT NULL,k7 INTEGER NOT NULL,k8 INTEGER NOT NULL,"
    "payload BLOB NOT NULL) STRICT;"
    "CREATE INDEX k1 ON t(k1);"
    "CREATE INDEX k3 ON t(k1,k2,k3);"
    "CREATE INDEX k8 ON t(k1,k2,k3,k4,k5,k6,k7,k8);"
    "CREATE INDEX kd ON t(k1 DESC,k2 DESC,k3 DESC);"
    "CREATE INDEX km ON t(k1,k2 DESC,k3);");
  sqlite3_stmt *insert = prepare(db,
    "INSERT INTO t VALUES(?1,?1,?1,?1,?1,?1,?1,?1,zeroblob(4096))");
  exec(db, "BEGIN IMMEDIATE");
  for (unsigned i = 0; i < RowCount; ++i) {
    sqlite3_bind_int64(insert, 1, i);
    if (sqlite3_step(insert) != SQLITE_DONE) fail(db, "insert");
    sqlite3_reset(insert);
  }
  exec(db, "COMMIT");
  sqlite3_finalize(insert);

  seek(db, "seek1",
    "SELECT payload FROM t WHERE (k1)>?1 ORDER BY k1 LIMIT 32", 1);
  seek(db, "seek3",
    "SELECT payload FROM t WHERE (k1,k2,k3)>(?1,?2,?3) "
    "ORDER BY k1,k2,k3 LIMIT 32", 3);
  seek(db, "seek8",
    "SELECT payload FROM t WHERE (k1,k2,k3,k4,k5,k6,k7,k8)>"
    "(?1,?2,?3,?4,?5,?6,?7,?8) "
    "ORDER BY k1,k2,k3,k4,k5,k6,k7,k8 LIMIT 32", 8);
  seek(db, "seekDesc",
    "SELECT payload FROM t WHERE (k1,k2,k3)<(?1,?2,?3) "
    "ORDER BY k1 DESC,k2 DESC,k3 DESC LIMIT 32", 3);
  seek(db, "seekMixed",
    "SELECT payload FROM t WHERE k1>?1 OR (k1=?1 AND "
    "(k2<?2 OR (k2=?2 AND (k3>?3)))) "
    "ORDER BY k1,k2 DESC,k3 LIMIT 32", 3);

  exec(db, "CREATE TABLE op(scalar INTEGER NOT NULL,vec BLOB NOT NULL,"
    "text TEXT NOT NULL,bytes BLOB NOT NULL) STRICT;"
    "CREATE INDEX op_scalar ON op(scalar);");
  PrimVec primVec{PrimCount};
  primVec.length(PrimCount);
  for (unsigned i = 0; i < primVec.length(); ++i) primVec[i] = i;
  Text longText;
  longText.length(PayloadSize);
  memset(longText.data(), 'x', longText.length());
  Bytes bytesPayload;
  bytesPayload.length(PayloadSize);
  memset(bytesPayload.data(), 0xa5, bytesPayload.length());
  sqlite3_stmt *opInsert = prepare(db, "INSERT INTO op VALUES(?1,?2,?3,?4)");
  sqlite3_bind_int64(opInsert, 1, 7);
  sqlite3_bind_blob64(opInsert, 2, primVec.data(),
    primVec.length() * sizeof(primVec[0]), SQLITE_STATIC);
  sqlite3_bind_text64(opInsert, 3, longText.data(), longText.length(),
    SQLITE_STATIC, SQLITE_UTF8);
  sqlite3_bind_blob64(opInsert, 4, bytesPayload.data(), bytesPayload.length(),
    SQLITE_STATIC);
  if (sqlite3_step(opInsert) != SQLITE_DONE) fail(db, "seed operation");
  sqlite3_reset(opInsert);
  sqlite3_stmt *opUpdate = prepare(db,
    "UPDATE op SET scalar=?1,vec=?2,text=?3,bytes=?4 WHERE rowid=1");
  sqlite3_bind_int64(opUpdate, 1, 7);
  sqlite3_bind_blob64(opUpdate, 2, primVec.data(),
    primVec.length() * sizeof(primVec[0]), SQLITE_STATIC);
  sqlite3_bind_text64(opUpdate, 3, longText.data(), longText.length(),
    SQLITE_STATIC, SQLITE_UTF8);
  sqlite3_bind_blob64(opUpdate, 4, bytesPayload.data(), bytesPayload.length(),
    SQLITE_STATIC);
  sqlite3_stmt *opFind = prepare(db,
    "SELECT rowid FROM op WHERE scalar=?1 LIMIT 1");
  sqlite3_bind_int64(opFind, 1, 7);
  sqlite3_stmt *opSelect = prepare(db,
    "SELECT scalar,vec,text,bytes FROM op WHERE rowid=?1");
  sqlite3_bind_int64(opSelect, 1, 1);
  operation(db, "updatePayload", opUpdate);
  operation(db, "findScalar", opFind);
  operation(db, "selectPayload", opSelect);
  operation(db, "insertPayload", opInsert);
  sqlite3_finalize(opSelect);
  sqlite3_finalize(opFind);
  sqlite3_finalize(opUpdate);
  sqlite3_finalize(opInsert);

  sqlite3_stmt *begin = prepare(db, "BEGIN IMMEDIATE");
  sqlite3_stmt *commit = prepare(db, "COMMIT");
  Samples tx{1024};
  tx.length(1024);
  for (unsigned i = 0; i < tx.length(); ++i) {
    uint64_t start = now();
    if (sqlite3_step(begin) != SQLITE_DONE) fail(db, "begin");
    sqlite3_reset(begin);
    if (sqlite3_step(commit) != SQLITE_DONE) fail(db, "commit");
    sqlite3_reset(commit);
    tx[i] = now() - start;
  }
  qsort(tx.data(), tx.length(), sizeof(tx[0]), cmpSample);
  Text text;
  text << "txn p50_ns=" << tx[512] << " p99_ns=" << tx[1013] << '\n';
  write(STDOUT_FILENO, text.data(), text.length());
  sqlite3_finalize(begin);
  sqlite3_finalize(commit);

  sqlite3_close(db);
  uint64_t start = now();
  if (sqlite3_open_v2(path, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_NOMUTEX,
      nullptr) != SQLITE_OK) fail(db, "reopen");
  uint64_t reopen = (now() - start) / 1000;
  sqlite3_int64 bytes = 0, high = 0, allocations = 0;
  sqlite3_status64(SQLITE_STATUS_MEMORY_USED, &bytes, &high, 0);
  sqlite3_status64(SQLITE_STATUS_MALLOC_COUNT, &allocations, &high, 0);
  text.null();
  text << "reopen_us=" << reopen << " sqlite_allocs=" << allocations
    << " sqlite_bytes=" << bytes << '\n';
  write(STDOUT_FILENO, text.data(), text.length());
  sqlite3_close(db);
  unlink(path);
  return 0;
}
