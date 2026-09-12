//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// SQLite CLI inspection functions for ZdbSL storage values

#include <sqlite3ext.h>
SQLITE_EXTENSION_INIT1

#include <zlib/ZtString.hh>
#include <zlib/ZtScratch.hh>

#include <zlib/ZdbSLCodec.hh>

namespace ZdbSL {

using Text = ZtString<ZtStringHeapID<"ZdbSL.Inspect">>;

template <typename T, bool (*Load)(ZuBSpan, T &)>
static void inspect(sqlite3_context *cxn, sqlite3_value *arg)
{
  if (sqlite3_value_type(arg) != SQLITE_BLOB) {
    sqlite3_result_error(cxn, "ZdbSL value must be a BLOB", -1);
    return;
  }
  T value;
  ZuBSpan data{
    static_cast<const uint8_t *>(sqlite3_value_blob(arg)),
    unsigned(sqlite3_value_bytes(arg))};
  if (!Load(data, value)) {
    sqlite3_result_error(cxn, "malformed ZdbSL value", -1);
    return;
  }
  auto text = ZtScratch(Text, 128);
  text << value;
  sqlite3_result_text(cxn, text.data(), text.length(), SQLITE_TRANSIENT);
}

static void datetime(sqlite3_context *cxn, int argc, sqlite3_value **argv)
{
  if (argc == 1) inspect<ZuDateTime, loadDateTime>(cxn, argv[0]);
}

static void time(sqlite3_context *cxn, int argc, sqlite3_value **argv)
{
  if (argc == 1) inspect<ZuTime, loadTime>(cxn, argv[0]);
}

static void decimal(sqlite3_context *cxn, int argc, sqlite3_value **argv)
{
  if (argc == 1) inspect<ZuDecimal, loadDecimal>(cxn, argv[0]);
}

static void floating(sqlite3_context *cxn, int argc, sqlite3_value **argv)
{
  if (argc == 1) inspect<double, loadFloat>(cxn, argv[0]);
}

} // ZdbSL

extern "C" ZdbSLAPI int sqlite3_zdbslinspect_init(
  sqlite3 *db, char **, const sqlite3_api_routines *api)
{
  SQLITE_EXTENSION_INIT2(api);
  auto add = [db](const char *id,
      void (*fn)(sqlite3_context *, int, sqlite3_value **)) {
    return sqlite3_create_function_v2(
      db, id, 1, SQLITE_UTF8 | SQLITE_DETERMINISTIC | SQLITE_INNOCUOUS,
      nullptr, fn, nullptr, nullptr, nullptr);
  };
  int rc = add("zdbsl_datetime", ZdbSL::datetime);
  if (rc != SQLITE_OK) return rc;
  rc = add("zdbsl_time", ZdbSL::time);
  if (rc != SQLITE_OK) return rc;
  rc = add("zdbsl_decimal", ZdbSL::decimal);
  if (rc != SQLITE_OK) return rc;
  rc = add("zdbsl_float", ZdbSL::floating);
  if (rc != SQLITE_OK) return rc;
  return SQLITE_OK;
}
