//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// SQLite CLI inspection functions for ZdbSL storage values

#include <sqlite3ext.h>
SQLITE_EXTENSION_INIT1

#include <zlib/ZtString.hh>
#include <zlib/ZtScratch.hh>

#include <zlib/ZdbSLLib.hh>
#include <zlib/ZdbSLCodec.hh>

namespace ZdbSL {

ZuDerive(Text, ZtString<ZtStringHeapID<"ZdbSL.Inspect">>);
// Every supported scalar renders within 64 bytes, including sign and
// fractional precision.  Oversized fallback remains attributed to Text.
enum { InspectTextSize = 64 };

template <typename T, bool (*Load)(ZuBSpan, T &)>
static void inspect(sqlite3_context *cxn, int, sqlite3_value **argv)
{
  sqlite3_value *arg = argv[0];
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
  auto text = ZtScratch(Text, InspectTextSize);
  text << value;
  sqlite3_result_text(cxn, text.data(), text.length(), SQLITE_TRANSIENT);
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
  int rc = add("zdbsl_datetime",
    ZdbSL::inspect<ZuDateTime, ZdbSL::loadDateTime>);
  if (rc != SQLITE_OK) return rc;
  rc = add("zdbsl_time", ZdbSL::inspect<ZuTime, ZdbSL::loadTime>);
  if (rc != SQLITE_OK) return rc;
  rc = add("zdbsl_decimal",
    ZdbSL::inspect<ZuDecimal, ZdbSL::loadDecimal>);
  if (rc != SQLITE_OK) return rc;
  rc = add("zdbsl_float", ZdbSL::inspect<double, ZdbSL::loadFloat>);
  if (rc != SQLITE_OK) return rc;
  return SQLITE_OK;
}
