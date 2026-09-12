//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>

#include <zlib/ZuJoin.hh>
#include <zlib/ZuSwitch.hh>

#include <zlib/ZmScratch.hh>

#include <zlib/ZtCase.hh>

#include <zlib/ZiLog.hh>

#include <zlib/ZdbSL.hh>

namespace ZdbSL {

ZtEnumImplNS(Synchronous);

using Value = ZdbMem::Value;

struct Stmt {
  sqlite3_stmt *ptr = nullptr;
  ~Stmt() { if (ptr) sqlite3_finalize(ptr); }
  sqlite3_stmt **operator &() { return &ptr; }
  operator sqlite3_stmt *() const { return ptr; }
};

struct Reset {
  sqlite3_stmt *stmt;
  ~Reset() {
    sqlite3_reset(stmt);
    sqlite3_clear_bindings(stmt);
  }
};

static ZeException cxnError(
  sqlite3 *cxn, ZuCSpan operation, int rc = SQLITE_ERROR)
{
  int xrc = cxn ? sqlite3_extended_errcode(cxn) : rc;
  if (xrc == SQLITE_OK) xrc = rc;
  ZeString message{
    cxn && sqlite3_errcode(cxn) != SQLITE_OK ?
      sqlite3_errmsg(cxn) : sqlite3_errstr(rc)};
  return ZeEXCEPT(Error, "ZdbSL", ([
    operation = ZeString{operation}, message = ZuMv(message), rc, xrc
  ](auto &s, const auto &) {
    s << operation << " failed: " << message << " (" << rc << '/' << xrc << ')';
  }));
}

static void exec(sqlite3 *cxn, ZuCSpan sql)
{
  char *message = nullptr;
  int rc = sqlite3_exec(cxn, sql.data(), nullptr, nullptr, &message);
  if (rc == SQLITE_OK) return;
  ZeString text{message ? message : sqlite3_errmsg(cxn)};
  sqlite3_free(message);
  throw ZeEXCEPT(Error, "ZdbSL", ([
    text = ZuMv(text), rc, xrc = sqlite3_extended_errcode(cxn)
  ](auto &s, const auto &) {
    s << "SQLite query failed: " << text << " (" << rc << '/' << xrc << ')';
  }));
}

static sqlite3_stmt *prepare(sqlite3 *cxn, ZuCSpan sql)
{
  sqlite3_stmt *stmt = nullptr;
  int rc = sqlite3_prepare_v3(
    cxn, sql.data(), sql.length(), SQLITE_PREPARE_PERSISTENT, &stmt, nullptr);
  if (rc != SQLITE_OK) throw cxnError(cxn, "sqlite3_prepare_v3()", rc);
  return stmt;
}

static void done(sqlite3 *cxn, sqlite3_stmt *stmt)
{
  int rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) throw cxnError(cxn, "sqlite3_step()", rc);
}

static SQLString quoteID(ZuCSpan);

static void validateStrict(sqlite3 *cxn, ZuCSpan table)
{
  Stmt stmt;
  stmt.ptr = prepare(cxn,
    "SELECT \"strict\" FROM pragma_table_list "
    "WHERE \"schema\"='main' AND \"name\"=?1");
  if (sqlite3_bind_text64(stmt, 1, table.data(), table.length(),
      SQLITE_STATIC, SQLITE_UTF8) != SQLITE_OK)
    throw cxnError(cxn, "bind table name");
  int rc = sqlite3_step(stmt);
  if (rc != SQLITE_ROW || sqlite3_column_int(stmt, 0) != 1)
    throw ZeEXCEPT(Fatal, "ZdbSL", ([table = ZeString{table}](auto &s) {
      s << "table is missing or is not STRICT: " << table;
    }));
  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) throw cxnError(cxn, "inspect STRICT table", rc);
}

enum CoreTable : uint8_t { Meta, Schema, MRD };

static void validateCoreTable(
    sqlite3 *cxn, ZuCSpan table, CoreTable core, unsigned nColumns)
{
  SQLString sql;
  sql << "PRAGMA table_info(" << quoteID(table) << ')';
  Stmt stmt;
  stmt.ptr = prepare(cxn, sql);
  unsigned column = 0;
  int rc;
  while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
    const char *id = nullptr;
    const char *type = nullptr;
    unsigned pk = 0;
    switch (core) {
      case Meta:
        switch (column) {
          case 0: id = "id"; type = "TEXT"; pk = 1; break;
          case 1: id = "value"; type = "INTEGER"; break;
        }
        break;
      case Schema:
        switch (column) {
          case 0: id = "internal"; type = "INTEGER"; pk = 1; break;
          case 1: id = "tbl"; type = "TEXT"; pk = 2; break;
          case 2: id = "ordinal"; type = "INTEGER"; pk = 3; break;
          case 3: id = "id"; type = "TEXT"; break;
          case 4: id = "type"; type = "INTEGER"; break;
          case 5: id = "encoding"; type = "INTEGER"; break;
          case 6: id = "storage"; type = "TEXT"; break;
          case 7: id = "props"; type = "INTEGER"; break;
          case 8: id = "keys"; type = "INTEGER"; break;
          case 9: id = "grp"; type = "INTEGER"; break;
          case 10: id = "descend"; type = "INTEGER"; break;
        }
        break;
      case MRD:
        switch (column) {
          case 0: id = "internal"; type = "INTEGER"; pk = 1; break;
          case 1: id = "tbl"; type = "TEXT"; pk = 2; break;
          case 2: id = "shard"; type = "INTEGER"; pk = 3; break;
          case 3: id = "un"; type = "BLOB"; break;
          case 4: id = "sn"; type = "BLOB"; break;
        }
        break;
    }
    if (!id || column >= nColumns) break;
    ZuCSpan actualID{
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1)),
      unsigned(sqlite3_column_bytes(stmt, 1))};
    ZuCSpan actualType{
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2)),
      unsigned(sqlite3_column_bytes(stmt, 2))};
    if (actualID != id || actualType != type ||
        !sqlite3_column_int(stmt, 3) ||
        unsigned(sqlite3_column_int(stmt, 5)) != pk)
      break;
    ++column;
  }
  if (rc != SQLITE_DONE && rc != SQLITE_ROW)
    throw cxnError(cxn, "inspect internal schema", rc);
  if (column != nColumns || rc == SQLITE_ROW)
    throw ZeEXCEPT(Fatal, "ZdbSL", ([table = ZeString{table}](auto &s) {
      s << "inconsistent internal table " << table;
    }));
}

static SQLString quoteID(ZuCSpan id)
{
  SQLString out;
  out << '"';
  for (char c: id) {
    out << c;
    if (c == '"') out << c;
  }
  out << '"';
  return out;
}

static const char *storage(unsigned type)
{
  switch (type) {
    case Value::Index<bool>{}:
    case Value::Index<int8_t>{}:
    case Value::Index<uint8_t>{}:
    case Value::Index<int16_t>{}:
    case Value::Index<uint16_t>{}:
    case Value::Index<int32_t>{}:
    case Value::Index<uint32_t>{}:
    case Value::Index<int64_t>{}:
      return "INTEGER";
    case Value::Index<ZdbMem::String>{}:
      return "TEXT";
    default:
      return "BLOB";
  }
}

static unsigned fixedSize(unsigned type)
{
  switch (type) {
    case Value::Index<uint64_t>{}:
    case Value::Index<double>{}: return 8;
    case Value::Index<ZuTime>{}:
    case Value::Index<ZuDateTime>{}: return 12;
    case Value::Index<ZuFixed>{}:
    case Value::Index<ZuDecimal>{}:
    case Value::Index<int128_t>{}:
    case Value::Index<uint128_t>{}: return 16;
    default: return 0;
  }
}

template <typename T>
static void saveUnsigned(uint8_t *ptr, T value)
{
  if constexpr (sizeof(T) == 1)
    *ptr = value;
  else {
    T data = ZuBE(value);
    memcpy(ptr, &data, sizeof(data));
  }
}

template <typename T, typename U>
static void saveSigned(uint8_t *ptr, T value)
{
  U data = ZuPun<T, U>(value).out ^ (U(1)<<(sizeof(U) * 8 - 1));
  saveUnsigned(ptr, data);
}

template <typename T>
static bool loadUnsigned(ZuBSpan &data, T &value)
{
  if (data.length() < sizeof(T)) return false;
  if constexpr (sizeof(T) == 1) {
    value = data[0];
    data.offset(1);
  } else {
    T encoded;
    memcpy(&encoded, data.data(), sizeof(encoded));
    value = ZuBE(encoded);
    data.offset(sizeof(encoded));
  }
  return true;
}

template <typename T, typename U>
static bool loadSigned(ZuBSpan &data, T &value)
{
  U encoded;
  if (!loadUnsigned(data, encoded)) return false;
  encoded ^= U(1)<<(sizeof(U) * 8 - 1);
  value = ZuPun<U, T>(encoded).out;
  return true;
}

static uint64_t varSize(const Value &value)
{
  auto vecSize = [](unsigned n, unsigned width) {
    return uint64_t(4) + uint64_t(n) * width;
  };
  auto addSize = [](uint64_t &size, uint64_t n) {
    if (n > UINT64_MAX - size) size = UINT64_MAX;
    else size += n;
  };
  switch (value.type()) {
    case Value::Index<ZtBitmap>{}:
      return vecSize(value.p<ZtBitmap>().data.length(), 8);
    case Value::Index<ZiIP>{}:
      switch (value.p<ZiIP>().type()) {
        case ZiIPType::V4: return 5;
        case ZiIPType::V6: return 17;
        default: return 1;
      }
    case Value::Index<ZdbMem::StringVec>{}: {
      uint64_t size = 4;
      value.p<ZdbMem::StringVec>().all(
        [&size, addSize](const auto &v) {
          addSize(size, uint64_t(4) + v.length());
        });
      return size;
    }
    case Value::Index<ZdbMem::BytesVec>{}: {
      uint64_t size = 4;
      value.p<ZdbMem::BytesVec>().all(
        [&size, addSize](const auto &v) {
          addSize(size, uint64_t(4) + v.length());
        });
      return size;
    }
    case Value::Index<ZdbMem::Int8Vec>{}:
      return vecSize(value.p<ZdbMem::Int8Vec>().length(), 1);
    case Value::Index<ZdbMem::UInt8Vec>{}:
      return vecSize(value.p<ZdbMem::UInt8Vec>().length(), 1);
    case Value::Index<ZdbMem::Int16Vec>{}:
      return vecSize(value.p<ZdbMem::Int16Vec>().length(), 2);
    case Value::Index<ZdbMem::UInt16Vec>{}:
      return vecSize(value.p<ZdbMem::UInt16Vec>().length(), 2);
    case Value::Index<ZdbMem::Int32Vec>{}:
      return vecSize(value.p<ZdbMem::Int32Vec>().length(), 4);
    case Value::Index<ZdbMem::UInt32Vec>{}:
      return vecSize(value.p<ZdbMem::UInt32Vec>().length(), 4);
    case Value::Index<ZdbMem::Int64Vec>{}:
      return vecSize(value.p<ZdbMem::Int64Vec>().length(), 8);
    case Value::Index<ZdbMem::UInt64Vec>{}:
      return vecSize(value.p<ZdbMem::UInt64Vec>().length(), 8);
    case Value::Index<ZdbMem::Int128Vec>{}:
      return vecSize(value.p<ZdbMem::Int128Vec>().length(), 16);
    case Value::Index<ZdbMem::UInt128Vec>{}:
      return vecSize(value.p<ZdbMem::UInt128Vec>().length(), 16);
    case Value::Index<ZdbMem::FloatVec>{}:
      return vecSize(value.p<ZdbMem::FloatVec>().length(), 8);
    case Value::Index<ZdbMem::FixedVec>{}:
      return vecSize(value.p<ZdbMem::FixedVec>().length(), 16);
    case Value::Index<ZdbMem::DecimalVec>{}:
      return vecSize(value.p<ZdbMem::DecimalVec>().length(), 16);
    case Value::Index<ZdbMem::TimeVec>{}:
      return vecSize(value.p<ZdbMem::TimeVec>().length(), 12);
    case Value::Index<ZdbMem::DateTimeVec>{}:
      return vecSize(value.p<ZdbMem::DateTimeVec>().length(), 12);
    default: return fixedSize(value.type());
  }
}

template <typename V, typename L>
static void saveVec(uint8_t *ptr, const V &values, L &&save)
{
  saveUnsigned(ptr, uint32_t(values.length()));
  ptr += 4;
  values.all([&ptr, &save](const auto &value) { save(ptr, value); });
}

static void encode(uint8_t *ptr, const Value &value)
{
  switch (value.type()) {
    case Value::Index<uint64_t>{}: saveU64(ptr, value.p<uint64_t>()); break;
    case Value::Index<int128_t>{}: saveS128(ptr, value.p<int128_t>()); break;
    case Value::Index<uint128_t>{}: saveU128(ptr, value.p<uint128_t>()); break;
    case Value::Index<double>{}: saveFloat(ptr, value.p<double>()); break;
    case Value::Index<ZuFixed>{}: saveFixed(ptr, value.p<ZuFixed>()); break;
    case Value::Index<ZuDecimal>{}: saveDecimal(ptr, value.p<ZuDecimal>()); break;
    case Value::Index<ZuTime>{}: saveTime(ptr, value.p<ZuTime>()); break;
    case Value::Index<ZuDateTime>{}:
      saveDateTime(ptr, value.p<ZuDateTime>()); break;
    case Value::Index<ZtBitmap>{}: {
      const auto &v = value.p<ZtBitmap>().data;
      saveUnsigned(ptr, uint32_t(v.length()));
      ptr += 4;
      v.all([&ptr](uint64_t word) { saveUnsigned(ptr, word); ptr += 8; });
    } break;
    case Value::Index<ZiIP>{}: {
      const auto &v = value.p<ZiIP>();
      switch (v.type()) {
        case ZiIPType::V4:
          *ptr++ = 4; memcpy(ptr, &v.inAddr(), 4); break;
        case ZiIPType::V6:
          *ptr++ = 6; memcpy(ptr, &v.in6Addr(), 16); break;
        default: *ptr = 0; break;
      }
    } break;
    case Value::Index<ZdbMem::StringVec>{}:
      saveVec(ptr, value.p<ZdbMem::StringVec>(), [](uint8_t *&ptr, const auto &v) {
        saveUnsigned(ptr, uint32_t(v.length())); ptr += 4;
        memcpy(ptr, v.data(), v.length()); ptr += v.length();
      });
      break;
    case Value::Index<ZdbMem::BytesVec>{}:
      saveVec(ptr, value.p<ZdbMem::BytesVec>(), [](uint8_t *&ptr, const auto &v) {
        saveUnsigned(ptr, uint32_t(v.length())); ptr += 4;
        memcpy(ptr, v.data(), v.length()); ptr += v.length();
      });
      break;
#define ZdbSL_SaveVec(Type, Elem, ...) \
    case Value::Index<ZdbMem::Type##Vec>{}: \
      saveVec(ptr, value.p<ZdbMem::Type##Vec>(), \
        [](uint8_t *&ptr, Elem v) { __VA_ARGS__; }); \
      break
    ZdbSL_SaveVec(Int8, int8_t, saveSigned<int8_t, uint8_t>(ptr, v); ptr += 1;);
    ZdbSL_SaveVec(UInt8, uint8_t, *ptr++ = v;);
    ZdbSL_SaveVec(Int16, int16_t, saveSigned<int16_t, uint16_t>(ptr, v); ptr += 2;);
    ZdbSL_SaveVec(UInt16, uint16_t, saveUnsigned(ptr, v); ptr += 2;);
    ZdbSL_SaveVec(Int32, int32_t, saveSigned<int32_t, uint32_t>(ptr, v); ptr += 4;);
    ZdbSL_SaveVec(UInt32, uint32_t, saveUnsigned(ptr, v); ptr += 4;);
    ZdbSL_SaveVec(Int64, int64_t, saveSigned<int64_t, uint64_t>(ptr, v); ptr += 8;);
    ZdbSL_SaveVec(UInt64, uint64_t, saveU64(ptr, v); ptr += 8;);
    ZdbSL_SaveVec(Int128, int128_t, saveS128(ptr, v); ptr += 16;);
    ZdbSL_SaveVec(UInt128, uint128_t, saveU128(ptr, v); ptr += 16;);
    ZdbSL_SaveVec(Float, double, saveFloat(ptr, v); ptr += 8;);
    ZdbSL_SaveVec(Fixed, ZuFixed,
      saveFixed(ptr, v); ptr += 16;);
    ZdbSL_SaveVec(Decimal, ZuDecimal, saveDecimal(ptr, v); ptr += 16;);
    ZdbSL_SaveVec(Time, ZuTime, saveTime(ptr, v); ptr += 12;);
    ZdbSL_SaveVec(DateTime, ZuDateTime, saveDateTime(ptr, v); ptr += 12;);
#undef ZdbSL_SaveVec
    default: break;
  }
}

static int bindValue(
  sqlite3_stmt *stmt, unsigned param, const Value &value, ZuSpan<uint8_t> scratch)
{
  switch (value.type()) {
    case Value::Index<ZdbMem::String>{}: {
      const auto &v = value.p<ZdbMem::String>();
      static const char empty = 0;
      return sqlite3_bind_text64(stmt, param, v.length() ? v.data() : &empty,
        v.length(), SQLITE_STATIC, SQLITE_UTF8);
    }
    case Value::Index<ZdbMem::Bytes>{}: {
      const auto &v = value.p<ZdbMem::Bytes>();
      static const uint8_t empty = 0;
      return sqlite3_bind_blob64(stmt, param, v.length() ? v.data() : &empty,
        v.length(), SQLITE_STATIC);
    }
    case Value::Index<bool>{}:
      return sqlite3_bind_int64(stmt, param, value.p<bool>());
    case Value::Index<int8_t>{}:
      return sqlite3_bind_int64(stmt, param, value.p<int8_t>());
    case Value::Index<uint8_t>{}:
      return sqlite3_bind_int64(stmt, param, value.p<uint8_t>());
    case Value::Index<int16_t>{}:
      return sqlite3_bind_int64(stmt, param, value.p<int16_t>());
    case Value::Index<uint16_t>{}:
      return sqlite3_bind_int64(stmt, param, value.p<uint16_t>());
    case Value::Index<int32_t>{}:
      return sqlite3_bind_int64(stmt, param, value.p<int32_t>());
    case Value::Index<uint32_t>{}:
      return sqlite3_bind_int64(stmt, param, value.p<uint32_t>());
    case Value::Index<int64_t>{}:
      return sqlite3_bind_int64(stmt, param, value.p<int64_t>());
    default: {
      uint64_t size_ = varSize(value);
      if (size_ > UINT_MAX || scratch.length() != size_) return SQLITE_TOOBIG;
      unsigned size = size_;
      encode(scratch.data(), value);
      static const uint8_t empty = 0;
      return sqlite3_bind_blob64(stmt, param,
        size ? scratch.data() : &empty, size, SQLITE_STATIC);
    }
  }
}

static bool decode(Value *, unsigned type, sqlite3_stmt *, unsigned);

static unsigned inputSize(
  sqlite3 *cxn, unsigned n, const XFields &fields, const Zfb::Table *fbo)
{
  uint64_t total = 0;
  uint64_t limit = sqlite3_limit(cxn, SQLITE_LIMIT_LENGTH, -1);
  for (unsigned i = 0; i < n; ++i) {
    unsigned type = fields[i].field.type;
    if (type == Value::Index<ZdbMem::String>{} ||
        type == Value::Index<ZdbMem::Bytes>{} || storage(type)[0] == 'I')
      continue;
    Value value;
    ZuSwitch::dispatch<Value::N>(type,
      [&value, field = fields[i].field.field, fbo](auto I) {
        ZdbMem::loadValue<I>(value.new_<I, true>(), field, fbo);
      });
    uint64_t size = varSize(value);
    if (size > limit || size > UINT_MAX || total > UINT_MAX - size)
      throw cxnError(cxn, "encode SQLite value", SQLITE_TOOBIG);
    total += size;
  }
  return unsigned(total);
}

static void bindInput(
  sqlite3 *cxn, sqlite3_stmt *stmt, unsigned first,
  unsigned n, const XFields &fields, const Zfb::Table *fbo,
  ZuSpan<uint8_t> data)
{
  unsigned offset = 0;
  for (unsigned i = 0; i < n; ++i) {
    const auto &xfield = fields[i].field;
    unsigned type = xfield.type;
    int rc;
    if (type == Value::Index<ZdbMem::String>{}) {
      auto v = Zfb::Load::str(Zfb::GetFieldS(*fbo, *xfield.field));
      static const char empty = 0;
      rc = sqlite3_bind_text64(stmt, first + i,
        v.length() ? v.data() : &empty, v.length(), SQLITE_STATIC, SQLITE_UTF8);
      if (rc != SQLITE_OK) throw cxnError(cxn, "sqlite3_bind_text64()", rc);
      continue;
    }
    if (type == Value::Index<ZdbMem::Bytes>{}) {
      auto v = Zfb::Load::bytes(Zfb::GetFieldV<uint8_t>(*fbo, *xfield.field));
      static const uint8_t empty = 0;
      rc = sqlite3_bind_blob64(stmt, first + i,
        v.length() ? v.data() : &empty, v.length(), SQLITE_STATIC);
      if (rc != SQLITE_OK) throw cxnError(cxn, "sqlite3_bind_blob64()", rc);
      continue;
    }
    Value value;
    ZuSwitch::dispatch<Value::N>(type,
      [&value, field = xfield.field, fbo](auto I) {
        ZdbMem::loadValue<I>(value.new_<I, true>(), field, fbo);
      });
    uint64_t size_ = varSize(value);
    if (offset > data.length() || size_ > data.length() - offset)
      throw cxnError(cxn, "encode SQLite value", SQLITE_TOOBIG);
    unsigned size = size_;
    uint8_t *ptr = size ? data.data() + offset : nullptr;
    rc = bindValue(stmt, first + i, value,
      ZuSpan<uint8_t>{ptr, size});
    if (rc != SQLITE_OK) throw cxnError(cxn, "sqlite3_bind()", rc);
    offset += size;
  }
}

static ZuBSpan columnBlob(sqlite3_stmt *stmt, unsigned col)
{
  return {
    static_cast<const uint8_t *>(sqlite3_column_blob(stmt, col)),
    unsigned(sqlite3_column_bytes(stmt, col))};
}

template <typename V, typename E, typename L>
static bool loadVec(ZuBSpan data, V &values, unsigned width, L &&load)
{
  uint32_t n;
  if (!loadUnsigned(data, n) ||
      uint64_t(n) * width != data.length()) return false;
  values.size(n);
  for (unsigned i = 0; i < n; ++i) {
    E value;
    if (!load(data, value)) return false;
    values.push(ZuMv(value));
  }
  return !data.length();
}

template <typename V, typename E>
static bool loadUVec(ZuBSpan data, V &values)
{
  return loadVec<V, E>(data, values, sizeof(E),
    [](ZuBSpan &data, E &value) { return loadUnsigned(data, value); });
}

template <typename V, typename E, typename U>
static bool loadSVec(ZuBSpan data, V &values)
{
  return loadVec<V, E>(data, values, sizeof(E),
    [](ZuBSpan &data, E &value) { return loadSigned<E, U>(data, value); });
}

template <typename V, typename E, bool (*Load)(ZuBSpan, E &)>
static bool loadObjVec(ZuBSpan data, V &values, unsigned width)
{
  return loadVec<V, E>(data, values, width,
    [width](ZuBSpan &data, E &value) {
      if (data.length() < width) return false;
      ZuBSpan part{data.data(), width};
      data.offset(width);
      return Load(part, value);
    });
}

template <typename V, typename E>
static bool loadVarVec(ZuBSpan data, V &values)
{
  uint32_t n;
  if (!loadUnsigned(data, n)) return false;
  values.size(n);
  for (unsigned i = 0; i < n; ++i) {
    uint32_t length;
    if (!loadUnsigned(data, length) || data.length() < length) return false;
    new (values.push()) E{ZuSpan<const typename ZuTraits<E>::Elem>{
      data.data(), length}};
    data.offset(length);
  }
  return !data.length();
}

static bool decode(Value *value, unsigned type, sqlite3_stmt *stmt, unsigned col)
{
  if (storage(type)[0] == 'I') {
    if (sqlite3_column_type(stmt, col) != SQLITE_INTEGER) return false;
    sqlite3_int64 v = sqlite3_column_int64(stmt, col);
    switch (type) {
      case Value::Index<bool>{}: new (value) Value{bool(v)}; return true;
      case Value::Index<int8_t>{}: new (value) Value{int8_t(v)}; return true;
      case Value::Index<uint8_t>{}: new (value) Value{uint8_t(v)}; return true;
      case Value::Index<int16_t>{}: new (value) Value{int16_t(v)}; return true;
      case Value::Index<uint16_t>{}: new (value) Value{uint16_t(v)}; return true;
      case Value::Index<int32_t>{}: new (value) Value{int32_t(v)}; return true;
      case Value::Index<uint32_t>{}: new (value) Value{uint32_t(v)}; return true;
      case Value::Index<int64_t>{}: new (value) Value{int64_t(v)}; return true;
    }
  }
  if (type == Value::Index<ZdbMem::String>{}) {
    if (sqlite3_column_type(stmt, col) != SQLITE_TEXT) return false;
    ZuCSpan data{
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, col)),
      unsigned(sqlite3_column_bytes(stmt, col))};
    new (value) Value{ZdbMem::String{data}};
    return true;
  }
  if (sqlite3_column_type(stmt, col) != SQLITE_BLOB) return false;
  ZuBSpan data = columnBlob(stmt, col);
  switch (type) {
    case Value::Index<ZdbMem::Bytes>{}:
      new (value) Value{ZdbMem::Bytes{data}};
      return true;
    case Value::Index<uint64_t>{}: {
      uint64_t v;
      if (!loadU64(data, v)) return false;
      new (value) Value{v};
    } return true;
    case Value::Index<int128_t>{}: {
      int128_t v;
      if (!loadS128(data, v)) return false;
      new (value) Value{v};
    } return true;
    case Value::Index<uint128_t>{}: {
      uint128_t v;
      if (!loadU128(data, v)) return false;
      new (value) Value{v};
    } return true;
    case Value::Index<double>{}: {
      double v;
      if (!loadFloat(data, v)) return false;
      new (value) Value{v};
    } return true;
    case Value::Index<ZuFixed>{}: {
      ZuFixed v;
      if (!loadFixed(data, v)) return false;
      new (value) Value{v};
    } return true;
    case Value::Index<ZuDecimal>{}: {
      ZuDecimal v;
      if (!loadDecimal(data, v)) return false;
      new (value) Value{v};
    } return true;
    case Value::Index<ZuTime>{}: {
      ZuTime v;
      if (!loadTime(data, v)) return false;
      new (value) Value{v};
    } return true;
    case Value::Index<ZuDateTime>{}: {
      ZuDateTime v;
      if (!loadDateTime(data, v)) return false;
      new (value) Value{v};
    } return true;
    case Value::Index<ZtBitmap>{}: {
      uint32_t n;
      if (!loadUnsigned(data, n) || uint64_t(n) * 8 != data.length())
        return false;
      ZtBitmap v;
      v.data.length(n);
      for (unsigned i = 0; i < n; ++i)
        if (!loadUnsigned(data, v.data[i])) return false;
      new (value) Value{ZuMv(v)};
    } return true;
    case Value::Index<ZiIP>{}: {
      if (!data.length()) return false;
      uint8_t family = data[0];
      data.offset(1);
      ZiIP v;
      if (!family) {
        if (data.length()) return false;
      } else if (family == 4 && data.length() == sizeof(in_addr)) {
        in_addr addr;
        memcpy(&addr, data.data(), sizeof(addr));
        v = ZiIP{addr};
      } else if (family == 6 && data.length() == sizeof(in6_addr)) {
        in6_addr addr;
        memcpy(&addr, data.data(), sizeof(addr));
        v = ZiIP{addr};
      } else return false;
      new (value) Value{v};
    } return true;
    case Value::Index<ZdbMem::StringVec>{}: {
      ZdbMem::StringVec v;
      if (!loadVarVec<ZdbMem::StringVec, ZdbMem::String>(data, v)) return false;
      new (value) Value{ZuMv(v)};
    } return true;
    case Value::Index<ZdbMem::BytesVec>{}: {
      ZdbMem::BytesVec v;
      if (!loadVarVec<ZdbMem::BytesVec, ZdbMem::Bytes>(data, v)) return false;
      new (value) Value{ZuMv(v)};
    } return true;
#define ZdbSL_LoadUVec(Type, Elem) \
    case Value::Index<ZdbMem::Type##Vec>{}: { \
      ZdbMem::Type##Vec v; \
      if (!loadUVec<ZdbMem::Type##Vec, Elem>(data, v)) return false; \
      new (value) Value{ZuMv(v)}; \
    } return true
#define ZdbSL_LoadSVec(Type, Elem, UInt) \
    case Value::Index<ZdbMem::Type##Vec>{}: { \
      ZdbMem::Type##Vec v; \
      if (!loadSVec<ZdbMem::Type##Vec, Elem, UInt>(data, v)) return false; \
      new (value) Value{ZuMv(v)}; \
    } return true
    ZdbSL_LoadSVec(Int8, int8_t, uint8_t);
    ZdbSL_LoadUVec(UInt8, uint8_t);
    ZdbSL_LoadSVec(Int16, int16_t, uint16_t);
    ZdbSL_LoadUVec(UInt16, uint16_t);
    ZdbSL_LoadSVec(Int32, int32_t, uint32_t);
    ZdbSL_LoadUVec(UInt32, uint32_t);
    ZdbSL_LoadSVec(Int64, int64_t, uint64_t);
    ZdbSL_LoadUVec(UInt64, uint64_t);
    ZdbSL_LoadSVec(Int128, int128_t, uint128_t);
    ZdbSL_LoadUVec(UInt128, uint128_t);
#undef ZdbSL_LoadSVec
#undef ZdbSL_LoadUVec
    case Value::Index<ZdbMem::FloatVec>{}: {
      ZdbMem::FloatVec v;
      if (!loadObjVec<ZdbMem::FloatVec, double, loadFloat>(data, v, 8))
        return false;
      new (value) Value{ZuMv(v)};
    } return true;
    case Value::Index<ZdbMem::FixedVec>{}: {
      ZdbMem::FixedVec v;
      if (!loadObjVec<ZdbMem::FixedVec, ZuFixed, loadFixed>(data, v, 16))
        return false;
      new (value) Value{ZuMv(v)};
    } return true;
    case Value::Index<ZdbMem::DecimalVec>{}: {
      ZdbMem::DecimalVec v;
      if (!loadObjVec<ZdbMem::DecimalVec, ZuDecimal, loadDecimal>(data, v, 16))
        return false;
      new (value) Value{ZuMv(v)};
    } return true;
    case Value::Index<ZdbMem::TimeVec>{}: {
      ZdbMem::TimeVec v;
      if (!loadObjVec<ZdbMem::TimeVec, ZuTime, loadTime>(data, v, 12))
        return false;
      new (value) Value{ZuMv(v)};
    } return true;
    case Value::Index<ZdbMem::DateTimeVec>{}: {
      ZdbMem::DateTimeVec v;
      if (!loadObjVec<ZdbMem::DateTimeVec, ZuDateTime, loadDateTime>(data, v, 12))
        return false;
      new (value) Value{ZuMv(v)};
    } return true;
  }
  return false;
}

InitResult Store::init(
    const ZfCf::AnyNode *cf, ZiMultiplex *mx, unsigned nShards,
    FailFn failFn)
{
  m_mx = mx;
  m_nShards = nShards;
  m_failFn = ZuMv(failFn);
  try {
    auto config = ZfCf::handler<StoreCf>(cf).ctor();
    const auto &tid = config.thread;
    auto sid = m_mx->sid(tid);
    if (!sid || sid > m_mx->params().nThreads() ||
        sid == m_mx->rxThread() || sid == m_mx->txThread())
      return ZeEXCEPT(Fatal, "ZdbSL", ([tid = ZeString{tid}](auto &s, const auto &) {
        s << "Store::init() failed: invalid thread configuration \""
          << tid << '"';
      }));
    m_sid = sid;
    m_connection = ZuMv(config.connection);
    m_synchronous = config.synchronous;
  } catch (const ZeException &e) {
    return ZeEXCEPT(Fatal, "ZdbSL", ([e](auto &s) {
      s << "Store::init() failed: invalid configuration: " << e;
    }));
  }
  if (!m_storeTbls) m_storeTbls = new StoreTbls{};
  return InitData{.replicated = false};
}

void Store::final()
{
  m_failFn = FailFn{};
  m_storeTbls->clean();
  m_storeTbls = nullptr;
  m_connection.null();
  m_mx = nullptr;
  m_sid = 0;
  m_nShards = 0;
}

void Store::start(StartFn fn)
{
  run([this, fn = ZuMv(fn)]() mutable { start_(ZuMv(fn)); });
}

void Store::start_(StartFn fn)
{
  if (m_conn) { fn(StartResult{}); return; }
  m_stopping = false;
  try {
    int rc = sqlite3_open_v2(
      m_connection.data(), &m_conn,
      SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX,
      nullptr);
    if (rc != SQLITE_OK) throw cxnError(m_conn, "sqlite3_open_v2()", rc);
    sqlite3_extended_result_codes(m_conn, 1);
    startup_();
    fn(StartResult{});
  } catch (const ZeException &e) {
    if (m_conn) sqlite3_close_v2(m_conn);
    m_conn = nullptr;
    fn(StartResult{e});
  }
}

void Store::startup_()
{
  {
    Stmt stmt;
    stmt.ptr = prepare(m_conn, "PRAGMA journal_mode=WAL");
    int rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW || sqlite3_column_type(stmt, 0) != SQLITE_TEXT ||
        sqlite3_stricmp(
          reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0)),
          "wal"))
      throw cxnError(m_conn, "PRAGMA journal_mode=WAL", rc);
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE)
      throw cxnError(m_conn, "PRAGMA journal_mode=WAL", rc);
  }
  switch (m_synchronous) {
    case Synchronous::NORMAL: exec(m_conn, "PRAGMA synchronous=NORMAL"); break;
    case Synchronous::FULL: exec(m_conn, "PRAGMA synchronous=FULL"); break;
    case Synchronous::OFF: exec(m_conn, "PRAGMA synchronous=OFF"); break;
    default:
      throw ZeEXCEPT(Fatal, "ZdbSL", "invalid synchronous configuration");
  }
  exec(m_conn, "BEGIN IMMEDIATE");
  try {
    exec(m_conn,
      "CREATE TABLE IF NOT EXISTS \"zdbsl_meta\" ("
      "\"id\" TEXT NOT NULL PRIMARY KEY, "
      "\"value\" INTEGER NOT NULL) STRICT");
    exec(m_conn,
      "CREATE TABLE IF NOT EXISTS \"zdbsl_schema\" ("
      "\"internal\" INTEGER NOT NULL, \"tbl\" TEXT NOT NULL, "
      "\"ordinal\" INTEGER NOT NULL, \"id\" TEXT NOT NULL, "
      "\"type\" INTEGER NOT NULL, \"encoding\" INTEGER NOT NULL, "
      "\"storage\" TEXT NOT NULL, "
      "\"props\" INTEGER NOT NULL, \"keys\" INTEGER NOT NULL, "
      "\"grp\" INTEGER NOT NULL, \"descend\" INTEGER NOT NULL, "
      "PRIMARY KEY (\"internal\", \"tbl\", \"ordinal\")) STRICT");
    exec(m_conn,
      "CREATE TABLE IF NOT EXISTS \"zdbsl_mrd\" ("
      "\"internal\" INTEGER NOT NULL, \"tbl\" TEXT NOT NULL, "
      "\"shard\" INTEGER NOT NULL CHECK (\"shard\" BETWEEN 0 AND 255), "
      "\"un\" BLOB NOT NULL, \"sn\" BLOB NOT NULL, "
      "PRIMARY KEY (\"internal\", \"tbl\", \"shard\")) STRICT");
    validateCoreTable(m_conn, "zdbsl_meta", Meta, 2);
    validateCoreTable(m_conn, "zdbsl_schema", Schema, 11);
    validateCoreTable(m_conn, "zdbsl_mrd", MRD, 5);
    validateStrict(m_conn, "zdbsl_meta");
    validateStrict(m_conn, "zdbsl_schema");
    validateStrict(m_conn, "zdbsl_mrd");
    {
      Stmt stmt;
      stmt.ptr = prepare(m_conn,
        "INSERT INTO \"zdbsl_meta\" (\"id\", \"value\") "
        "VALUES ('format', 1), ('nShards', ?) ON CONFLICT DO NOTHING");
      if (sqlite3_bind_int64(stmt, 1, m_nShards) != SQLITE_OK)
        throw cxnError(m_conn, "bind shard topology");
      done(m_conn, stmt);
    }
    {
      Stmt stmt;
      stmt.ptr = prepare(m_conn,
        "SELECT \"id\", \"value\" FROM \"zdbsl_meta\" "
        "WHERE \"id\" IN ('format', 'nShards') ORDER BY \"id\"");
      unsigned seen = 0;
      int rc;
      while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        ZuCSpan id{
          reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0)),
          unsigned(sqlite3_column_bytes(stmt, 0))};
        sqlite3_int64 value = sqlite3_column_int64(stmt, 1);
        if ((id == "format" && value != 1) ||
            (id == "nShards" && value != m_nShards))
          throw ZeEXCEPT(Fatal, "ZdbSL", ([
            id = ZeString{id}, value, nShards = m_nShards
          ](auto &s, const auto &) {
            s << "inconsistent " << id << " metadata: stored=" << value
              << " configured=" << nShards;
          }));
        ++seen;
      }
      if (rc != SQLITE_DONE)
        throw cxnError(m_conn, "read store metadata", rc);
      if (seen != 2)
        throw ZeEXCEPT(Fatal, "ZdbSL", "incomplete store metadata");
    }
    exec(m_conn, "COMMIT");
  } catch (...) {
    sqlite3_exec(m_conn, "ROLLBACK", nullptr, nullptr, nullptr);
    throw;
  }
}

void Store::stop(StopFn fn)
{
  run([this, fn = ZuMv(fn)]() mutable { stop_(ZuMv(fn)); });
}

void Store::stop_(StopFn fn)
{
  if (!m_conn) { fn(StopResult{}); return; }
  if (m_stopping) {
    run([this, fn = ZuMv(fn)]() mutable { stop_1(ZuMv(fn)); });
    return;
  }
  m_stopping = true;
  run([this, fn = ZuMv(fn)]() mutable { stop_1(ZuMv(fn)); });
}

void Store::stop_1(StopFn fn)
{
  StoreTbls::CIter i{*m_storeTbls};
  while (auto tbl = i()) {
    tbl->finalize_();
    tbl->m_openState = OpenState::Closed;
  }
  int rc = m_conn ? sqlite3_close(m_conn) : SQLITE_OK;
  if (rc != SQLITE_OK) {
    fn(StopResult{cxnError(m_conn, "sqlite3_close()", rc)});
    return;
  }
  m_conn = nullptr;
  fn(StopResult{});
}

void Store::open(
  bool internal, IDString id,
  ZfVFieldArray fields, ZfVKeyFieldArray keyFields,
  const reflection::Schema *schema, IOBufAllocFn bufAllocFn, OpenFn openFn)
{
  run([
    this, internal, id = ZuMv(id), fields = ZuMv(fields),
    keyFields = ZuMv(keyFields), schema,
    bufAllocFn = ZuMv(bufAllocFn), openFn = ZuMv(openFn)
  ]() mutable {
    if (stopping() || !m_conn) {
      openFn(OpenResult{ZeEXCEPT(Error, "ZdbSL", ([id = ZeString{id}](auto &s, const auto &) {
        s << "open(" << id << ") failed - DB shutdown in progress";
      }))});
      return;
    }
    auto tbl = m_storeTbls->find(ZuTuple<bool, ZuCSpan>{internal, id});
    if (tbl) {
      if (tbl->m_openState != OpenState::Closed &&
          tbl->m_openState != OpenState::Reopen) {
        openFn(OpenResult{ZeEXCEPT(Error, "ZdbSL", ([id = ZeString{id}](auto &s, const auto &) {
          s << "open(" << id << ") failed - already open";
        }))});
        return;
      }
      tbl->open_(ZuMv(openFn));
      return;
    }
    try {
      auto tbl_ = new StoreTbls::Node{
        this, internal, ZuMv(id), m_nShards,
        ZuMv(fields), ZuMv(keyFields), schema, ZuMv(bufAllocFn)};
      m_storeTbls->addNode(tbl_);
      tbl_->open_(ZuMv(openFn));
    } catch (const ZeException &e) {
      openFn(OpenResult{e});
    }
  });
}

StoreTbl::StoreTbl(
  Store *store, bool internal, IDString id, unsigned nShards,
  ZfVFieldArray fields, ZfVKeyFieldArray keyFields,
  const reflection::Schema *schema, IOBufAllocFn bufAllocFn)
:
  m_store{store}, m_id{ZuMv(id)}, m_fields{ZuMv(fields)},
  m_keyFields{ZuMv(keyFields)},
  m_fieldMap{ZmHashParams(m_fields.length())},
  m_bufAllocFn{ZuMv(bufAllocFn)}, m_internal{internal}
{
  SQLString physical;
  physical << (m_internal ? "i_" : "a_") << m_id;
  m_relation = quoteID(physical);
  auto fbFields = schema->root_table()->fields();
  unsigned n = m_fields.length();
  m_xFields.size(n);
  for (unsigned i = 0; i < n; ++i) {
    ZtCase::camelSnake(m_fields[i]->id, [this, fbFields, i](ZuCSpan id_) {
      XField xfield{
        .id = FieldID{id_}, .vfield = m_fields[i],
        .field = ZdbMem::xField(fbFields, m_fields[i], id_)};
      if (!xfield.field.field || !xfield.field.type)
        throw ZeEXCEPT(Fatal, "ZdbSL", ([id = ZeString{id_}](auto &s, const auto &) {
          s << "unsupported or inconsistent field " << id;
        }));
      m_xFields.push(xfield);
      m_fieldMap.add(xfield.id, i);
      if (m_fields[i]->props & ZfVFieldProp::Mutable()) {
        m_updFields.push(m_fields[i]);
        m_xUpdFields.push(xfield);
      }
    });
  }
  n = m_keyFields.length();
  m_xKeyFields.size(n);
  m_keyGroup.length(n);
  for (unsigned i = 0; i < n; ++i) {
    new (m_xKeyFields.push()) XFields{m_keyFields[i].length()};
    m_keyGroup[i] = 0;
    for (unsigned j = 0; j < m_keyFields[i].length(); ++j) {
      if (m_keyFields[i][j]->group & (uint64_t(1)<<i)) m_keyGroup[i] = j + 1;
      ZtCase::camelSnake(m_keyFields[i][j]->id, [this, i](ZuCSpan id_) {
        auto field = m_fieldMap.findVal(id_);
        m_xKeyFields[i].push(m_xFields[field]);
      });
    }
  }
  m_maxUN.length(nShards);
  m_stmts.keys.length(n);
  memset(m_stmts.keys.data(), 0, n * sizeof(KeyStmts));
}

StoreTbl::~StoreTbl() { finalize_(); }

void StoreTbl::schema_()
{
  sqlite3 *cxn = m_store->conn();
  exec(cxn, "BEGIN IMMEDIATE");
  try {
    SQLString physical;
    physical << (m_internal ? "i_" : "a_") << m_id;
    bool existed;
    {
      Stmt stmt;
      stmt.ptr = prepare(cxn,
        "SELECT 1 FROM sqlite_schema WHERE type='table' AND name=?1");
      if (sqlite3_bind_text64(stmt, 1, physical.data(), physical.length(),
          SQLITE_STATIC, SQLITE_UTF8) != SQLITE_OK)
        throw cxnError(cxn, "bind table name");
      int rc = sqlite3_step(stmt);
      if (rc != SQLITE_ROW && rc != SQLITE_DONE)
        throw cxnError(cxn, "inspect table", rc);
      existed = rc == SQLITE_ROW;
    }
    SQLString ddl;
    ddl << "CREATE TABLE IF NOT EXISTS " << m_relation << " ("
      "\"_shard\" INTEGER NOT NULL CHECK (\"_shard\" BETWEEN 0 AND 255), "
      "\"_un\" BLOB NOT NULL, \"_sn\" BLOB NOT NULL, "
      "\"_vn\" INTEGER NOT NULL";
    for (unsigned i = 0; i < m_xFields.length(); ++i) {
      const auto &field = m_xFields[i];
      ddl << ", " << quoteID(field.id) << ' ' << storage(field.field.type)
        << " NOT NULL";
      switch (field.field.type) {
        case Value::Index<bool>{}:
          ddl << " CHECK (" << quoteID(field.id) << " IN (0, 1))";
          break;
        case Value::Index<int8_t>{}:
          ddl << " CHECK (" << quoteID(field.id) << " BETWEEN -128 AND 127)";
          break;
        case Value::Index<uint8_t>{}:
          ddl << " CHECK (" << quoteID(field.id) << " BETWEEN 0 AND 255)";
          break;
        case Value::Index<int16_t>{}:
          ddl << " CHECK (" << quoteID(field.id)
            << " BETWEEN -32768 AND 32767)";
          break;
        case Value::Index<uint16_t>{}:
          ddl << " CHECK (" << quoteID(field.id) << " BETWEEN 0 AND 65535)";
          break;
        case Value::Index<int32_t>{}:
          ddl << " CHECK (" << quoteID(field.id)
            << " BETWEEN -2147483648 AND 2147483647)";
          break;
        case Value::Index<uint32_t>{}:
          ddl << " CHECK (" << quoteID(field.id)
            << " BETWEEN 0 AND 4294967295)";
          break;
      }
    }
    ddl << ") STRICT";
    exec(cxn, ddl);
    validateStrict(cxn, physical);

    for (unsigned keyID = 0; keyID < m_xKeyFields.length(); ++keyID) {
      SQLString indexID;
      indexID << "zdbsl_k" << ZuBoxed(keyID) << '_'
        << (m_internal ? 'i' : 'a') << '_' << m_id;
      SQLString sql;
      sql << "CREATE ";
      if (!keyID) sql << "UNIQUE ";
      sql << "INDEX IF NOT EXISTS " << quoteID(indexID) << " ON "
        << m_relation << " (";
      const auto &fields = m_xKeyFields[keyID];
      for (unsigned i = 0; i < fields.length(); ++i) {
        if (i) sql << ", ";
        sql << quoteID(fields[i].id);
        if (m_keyFields[keyID][i]->descend & (uint64_t(1)<<keyID))
          sql << " DESC";
      }
      sql << ')';
      exec(cxn, sql);
    }
    {
      SQLString indexID;
      indexID << "zdbsl_un_" << (m_internal ? 'i' : 'a') << '_' << m_id;
      SQLString sql;
      sql << "CREATE UNIQUE INDEX IF NOT EXISTS " << quoteID(indexID)
        << " ON " << m_relation << " (\"_shard\", \"_un\")";
      exec(cxn, sql);
    }

    if (!existed) {
      Stmt insert;
      insert.ptr = prepare(cxn,
        "INSERT INTO \"zdbsl_schema\" ("
        "\"internal\", \"tbl\", \"ordinal\", \"id\", \"type\", "
        "\"encoding\", \"storage\", \"props\", \"keys\", \"grp\", "
        "\"descend\") VALUES (?, ?, ?, ?, ?, 1, ?, ?, ?, ?, ?)");
      for (unsigned i = 0; i < m_xFields.length(); ++i) {
        Reset reset{insert};
        const auto &field = m_xFields[i];
        if (sqlite3_bind_int(insert, 1, m_internal) != SQLITE_OK ||
            sqlite3_bind_text64(insert, 2, m_id.data(), m_id.length(),
              SQLITE_STATIC, SQLITE_UTF8) != SQLITE_OK ||
            sqlite3_bind_int64(insert, 3, i) != SQLITE_OK ||
            sqlite3_bind_text64(insert, 4, field.id.data(), field.id.length(),
              SQLITE_STATIC, SQLITE_UTF8) != SQLITE_OK ||
            sqlite3_bind_int64(insert, 5,
              field.vfield->type->code) != SQLITE_OK ||
            sqlite3_bind_text(insert, 6, storage(field.field.type), -1,
              SQLITE_STATIC) != SQLITE_OK ||
            sqlite3_bind_int64(insert, 7,
              int64_t(field.vfield->props)) != SQLITE_OK ||
            sqlite3_bind_int64(insert, 8,
              int64_t(field.vfield->keys)) != SQLITE_OK ||
            sqlite3_bind_int64(insert, 9,
              int64_t(field.vfield->group)) != SQLITE_OK ||
            sqlite3_bind_int64(insert, 10,
              int64_t(field.vfield->descend)) != SQLITE_OK)
          throw cxnError(cxn, "bind logical schema");
        done(cxn, insert);
      }
    }

    Stmt check;
    check.ptr = prepare(cxn,
      "SELECT \"ordinal\", \"id\", \"type\", \"encoding\", \"storage\", "
      "\"props\", \"keys\", \"grp\", \"descend\" "
      "FROM \"zdbsl_schema\" WHERE \"internal\"=? AND \"tbl\"=? "
      "ORDER BY \"ordinal\"");
    if (sqlite3_bind_int(check, 1, m_internal) != SQLITE_OK ||
        sqlite3_bind_text64(check, 2, m_id.data(), m_id.length(),
          SQLITE_STATIC, SQLITE_UTF8) != SQLITE_OK)
      throw cxnError(cxn, "bind logical schema identity");
    unsigned ordinal = 0;
    int rc;
    while ((rc = sqlite3_step(check)) == SQLITE_ROW) {
      if (ordinal >= m_xFields.length())
        throw ZeEXCEPT(Fatal, "ZdbSL", "stored schema has extra fields");
      const auto &field = m_xFields[ordinal];
      ZuCSpan id{
        reinterpret_cast<const char *>(sqlite3_column_text(check, 1)),
        unsigned(sqlite3_column_bytes(check, 1))};
      ZuCSpan storage_{
        reinterpret_cast<const char *>(sqlite3_column_text(check, 4)),
        unsigned(sqlite3_column_bytes(check, 4))};
      if (sqlite3_column_int64(check, 0) != ordinal || id != field.id ||
          sqlite3_column_int64(check, 2) != field.vfield->type->code ||
          sqlite3_column_int64(check, 3) != 1 ||
          storage_ != storage(field.field.type) ||
          uint64_t(sqlite3_column_int64(check, 5)) != field.vfield->props ||
          uint64_t(sqlite3_column_int64(check, 6)) != field.vfield->keys ||
          uint64_t(sqlite3_column_int64(check, 7)) != field.vfield->group ||
          uint64_t(sqlite3_column_int64(check, 8)) != field.vfield->descend)
        throw ZeEXCEPT(Fatal, "ZdbSL", ([
          table = ZeString{m_id}, ordinal
        ](auto &s, const auto &) {
          s << "inconsistent schema for table " << table
            << " at field " << ordinal;
        }));
      ++ordinal;
    }
    if (rc != SQLITE_DONE) throw cxnError(cxn, "read logical schema", rc);
    if (ordinal != m_xFields.length())
      throw ZeEXCEPT(Fatal, "ZdbSL", "stored schema has missing fields");

    SQLString pragma;
    pragma << "PRAGMA table_info(" << m_relation << ')';
    Stmt tableInfo;
    tableInfo.ptr = prepare(cxn, pragma);
    unsigned columns = 0;
    while ((rc = sqlite3_step(tableInfo)) == SQLITE_ROW) {
      ZuCSpan name{
        reinterpret_cast<const char *>(sqlite3_column_text(tableInfo, 1)),
        unsigned(sqlite3_column_bytes(tableInfo, 1))};
      ZuCSpan type{
        reinterpret_cast<const char *>(sqlite3_column_text(tableInfo, 2)),
        unsigned(sqlite3_column_bytes(tableInfo, 2))};
      ZuCSpan expectedName;
      const char *expectedType;
      switch (columns) {
        case 0: expectedName = "_shard"; expectedType = "INTEGER"; break;
        case 1: expectedName = "_un"; expectedType = "BLOB"; break;
        case 2: expectedName = "_sn"; expectedType = "BLOB"; break;
        case 3: expectedName = "_vn"; expectedType = "INTEGER"; break;
        default:
          if (columns - 4 >= m_xFields.length())
            throw ZeEXCEPT(Fatal, "ZdbSL", "physical table has extra columns");
          expectedName = m_xFields[columns - 4].id;
          expectedType = storage(m_xFields[columns - 4].field.type);
          break;
      }
      if (name != expectedName || type != expectedType ||
          !sqlite3_column_int(tableInfo, 3))
        throw ZeEXCEPT(Fatal, "ZdbSL", ([
          table = ZeString{m_id}, columns
        ](auto &s, const auto &) {
          s << "inconsistent physical schema for table " << table
            << " at column " << columns;
        }));
      ++columns;
    }
    if (rc != SQLITE_DONE) throw cxnError(cxn, "PRAGMA table_info", rc);
    if (columns != m_xFields.length() + 4)
      throw ZeEXCEPT(Fatal, "ZdbSL", "physical table has missing columns");

    auto validateIndex = [this, cxn, &physical](
        const SQLString &indexID, bool unique, const XFields *fields) {
      SQLString pragma;
      pragma << "PRAGMA index_list(" << quoteID(physical) << ')';
      Stmt list;
      list.ptr = prepare(cxn, pragma);
      bool found = false;
      int rc;
      while ((rc = sqlite3_step(list)) == SQLITE_ROW) {
        ZuCSpan id{
          reinterpret_cast<const char *>(sqlite3_column_text(list, 1)),
          unsigned(sqlite3_column_bytes(list, 1))};
        if (id != indexID) continue;
        if (bool(sqlite3_column_int(list, 2)) != unique ||
            sqlite3_column_int(list, 4))
          throw ZeEXCEPT(Fatal, "ZdbSL", "inconsistent index definition");
        found = true;
      }
      if (rc != SQLITE_DONE) throw cxnError(cxn, "PRAGMA index_list", rc);
      if (!found)
        throw ZeEXCEPT(Fatal, "ZdbSL", "missing index after creation");

      pragma.null();
      pragma << "PRAGMA index_xinfo(" << quoteID(indexID) << ')';
      Stmt info;
      info.ptr = prepare(cxn, pragma);
      unsigned ordinal = 0;
      while ((rc = sqlite3_step(info)) == SQLITE_ROW) {
        if (!sqlite3_column_int(info, 5)) continue;
        ZuCSpan id{
          reinterpret_cast<const char *>(sqlite3_column_text(info, 2)),
          unsigned(sqlite3_column_bytes(info, 2))};
        ZuCSpan expected;
        bool descend = false;
        if (fields) {
          if (ordinal >= fields->length())
            throw ZeEXCEPT(Fatal, "ZdbSL", "index has extra columns");
          expected = (*fields)[ordinal].id;
          unsigned keyID = unsigned(fields - m_xKeyFields.data());
          descend = m_keyFields[keyID][ordinal]->descend & (uint64_t(1)<<keyID);
        } else {
          switch (ordinal) {
            case 0: expected = "_shard"; break;
            case 1: expected = "_un"; break;
            default:
              throw ZeEXCEPT(Fatal, "ZdbSL", "recovery index has extra columns");
          }
        }
        if (id != expected || bool(sqlite3_column_int(info, 3)) != descend)
          throw ZeEXCEPT(Fatal, "ZdbSL", "inconsistent index columns");
        ++ordinal;
      }
      if (rc != SQLITE_DONE) throw cxnError(cxn, "PRAGMA index_xinfo", rc);
      unsigned expected = fields ? fields->length() : 2;
      if (ordinal != expected)
        throw ZeEXCEPT(Fatal, "ZdbSL", "index has missing columns");
    };
    for (unsigned keyID = 0; keyID < m_xKeyFields.length(); ++keyID) {
      SQLString indexID;
      indexID << "zdbsl_k" << ZuBoxed(keyID) << '_'
        << (m_internal ? 'i' : 'a') << '_' << m_id;
      validateIndex(indexID, !keyID, &m_xKeyFields[keyID]);
    }
    SQLString unIndex;
    unIndex << "zdbsl_un_" << (m_internal ? 'i' : 'a') << '_' << m_id;
    validateIndex(unIndex, true, nullptr);
    exec(cxn, "COMMIT");
  } catch (...) {
    sqlite3_exec(cxn, "ROLLBACK", nullptr, nullptr, nullptr);
    throw;
  }
}

static void columns(SQLString &sql, const XFields &fields)
{
  for (unsigned i = 0; i < fields.length(); ++i) {
    if (i) sql << ", ";
    sql << quoteID(fields[i].id);
  }
}

static void equal(SQLString &sql, const XFields &fields, unsigned n)
{
  for (unsigned i = 0; i < n; ++i) {
    sql << (i ? " AND " : " WHERE ") << quoteID(fields[i].id)
      << "=?" << ZuBoxed(i + 1);
  }
}

static SQLString selectSQL(
  const SQLString &relation, const XFields &projection,
  const ZfVFieldArray &keyFields, const XFields &xKeyFields,
  KeyID keyID, unsigned group, bool next, bool inclusive)
{
  SQLString sql;
  sql << "SELECT ";
  columns(sql, projection);
  sql << " FROM " << relation;
  equal(sql, xKeyFields, group);
  unsigned n = xKeyFields.length();
  if (next && group < n) {
    sql << (group ? " AND (" : " WHERE (");
    for (unsigned i = group; i < n; ++i) {
      if (i > group) sql << " OR ";
      sql << '(';
      for (unsigned j = group; j < i; ++j)
        sql << quoteID(xKeyFields[j].id) << "=?" << ZuBoxed(j + 1)
          << " AND ";
      sql << quoteID(xKeyFields[i].id)
        << ((keyFields[i]->descend & (uint64_t(1)<<keyID)) ? '<' : '>');
      if (inclusive && i + 1 == n) sql << '=';
      sql << '?' << ZuBoxed(i + 1) << ')';
    }
    sql << ')';
  }
  if (group < n) {
    sql << " ORDER BY ";
    for (unsigned i = group; i < n; ++i) {
      if (i > group) sql << ", ";
      sql << quoteID(xKeyFields[i].id);
      if (keyFields[i]->descend & (uint64_t(1)<<keyID)) sql << " DESC";
    }
  }
  sql << " LIMIT ?" << ZuBoxed((next ? n : group) + 1);
  return sql;
}

void StoreTbl::statements_()
{
  sqlite3 *cxn = m_store->conn();
  for (unsigned keyID = 0; keyID < m_xKeyFields.length(); ++keyID) {
    auto &stmts = m_stmts.keys[keyID];
    const auto &fields = m_xKeyFields[keyID];
    const auto &keyFields = m_keyFields[keyID];
    unsigned group = m_keyGroup[keyID];
    SQLString sql{"SELECT count(*) FROM "};
    sql << m_relation;
    equal(sql, fields, group);
    stmts.count = prepare(cxn, sql);
    stmts.selectKIX = prepare(cxn,
      selectSQL(m_relation, fields, keyFields, fields, keyID,
        group, false, false));
    stmts.selectKNX = prepare(cxn,
      selectSQL(m_relation, fields, keyFields, fields, keyID,
        group, true, false));
    stmts.selectKNI = prepare(cxn,
      selectSQL(m_relation, fields, keyFields, fields, keyID,
        group, true, true));
    stmts.selectRIX = prepare(cxn,
      selectSQL(m_relation, m_xFields, keyFields, fields, keyID,
        group, false, false));
    stmts.selectRNX = prepare(cxn,
      selectSQL(m_relation, m_xFields, keyFields, fields, keyID,
        group, true, false));
    stmts.selectRNI = prepare(cxn,
      selectSQL(m_relation, m_xFields, keyFields, fields, keyID,
        group, true, true));
    sql.null();
    sql << "SELECT \"_shard\", \"_un\", \"_sn\", \"_vn\", ";
    columns(sql, m_xFields);
    sql << " FROM " << m_relation;
    equal(sql, fields, fields.length());
    sql << " LIMIT 1";
    stmts.find = prepare(cxn, sql);
  }
  {
    SQLString sql;
    sql << "SELECT \"_shard\", \"_un\", \"_sn\", \"_vn\", ";
    columns(sql, m_xFields);
    sql << " FROM " << m_relation
      << " WHERE \"_shard\"=?1 AND \"_un\"=?2 LIMIT 1";
    m_stmts.recover = prepare(cxn, sql);
  }
  {
    SQLString sql;
    sql << "INSERT INTO " << m_relation
      << " (\"_shard\", \"_un\", \"_sn\", \"_vn\", ";
    columns(sql, m_xFields);
    sql << ") VALUES (?1, ?2, ?3, ?4";
    for (unsigned i = 0; i < m_xFields.length(); ++i)
      sql << ", ?" << ZuBoxed(i + 5);
    sql << ") ON CONFLICT (";
    const auto &key = m_xKeyFields[0];
    for (unsigned i = 0; i < key.length(); ++i) {
      if (i) sql << ", ";
      sql << quoteID(key[i].id);
    }
    sql << ") DO NOTHING";
    m_stmts.insert = prepare(cxn, sql);
  }
  {
    SQLString sql;
    sql << "UPDATE " << m_relation
      << " SET \"_un\"=?1, \"_sn\"=?2, \"_vn\"=?3";
    unsigned param = 4;
    for (const auto &field: m_xUpdFields)
      sql << ", " << quoteID(field.id) << "=?" << ZuBoxed(param++);
    sql << " WHERE ";
    const auto &key = m_xKeyFields[0];
    for (unsigned i = 0; i < key.length(); ++i) {
      if (i) sql << " AND ";
      sql << quoteID(key[i].id) << "=?" << ZuBoxed(param++);
    }
    m_stmts.update = prepare(cxn, sql);
  }
  {
    SQLString sql;
    sql << "DELETE FROM " << m_relation << " WHERE ";
    const auto &key = m_xKeyFields[0];
    for (unsigned i = 0; i < key.length(); ++i) {
      if (i) sql << " AND ";
      sql << quoteID(key[i].id) << "=?" << ZuBoxed(i + 1);
    }
    m_stmts.del = prepare(cxn, sql);
  }
  m_stmts.mrd = prepare(cxn,
    "INSERT INTO \"zdbsl_mrd\" ("
    "\"internal\", \"tbl\", \"shard\", \"un\", \"sn\") "
    "VALUES (?1, ?2, ?3, ?4, ?5) ON CONFLICT "
    "(\"internal\", \"tbl\", \"shard\") DO UPDATE SET "
    "\"un\"=excluded.\"un\", \"sn\"=excluded.\"sn\"");
}

void StoreTbl::state_()
{
  sqlite3 *cxn = m_store->conn();
  {
    SQLString sql{"SELECT count(*) FROM "};
    sql << m_relation;
    Stmt stmt;
    stmt.ptr = prepare(cxn, sql);
    int rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) throw cxnError(cxn, "open row count", rc);
    m_count = sqlite3_column_int64(stmt, 0);
  }
  for (unsigned shard = 0; shard < m_maxUN.length(); ++shard)
    m_maxUN[shard] = ZdbNullUN();
  m_maxSN = ZdbNullSN();
  SQLString liveSQL;
  liveSQL << "SELECT \"_un\" FROM " << m_relation
    << " WHERE \"_shard\"=?1 ORDER BY \"_un\" DESC LIMIT 1";
  Stmt live;
  live.ptr = prepare(cxn, liveSQL);
  Stmt mrd;
  mrd.ptr = prepare(cxn,
    "SELECT \"un\", \"sn\" FROM \"zdbsl_mrd\" "
    "WHERE \"internal\"=?1 AND \"tbl\"=?2 AND \"shard\"=?3");
  for (unsigned shard = 0; shard < m_maxUN.length(); ++shard) {
    {
      Reset reset{live};
      if (sqlite3_bind_int64(live, 1, shard) != SQLITE_OK)
        throw cxnError(cxn, "bind live high-water shard");
      int rc = sqlite3_step(live);
      if (rc == SQLITE_ROW) {
        uint64_t un;
        if (!loadU64(columnBlob(live, 0), un))
          throw ZeEXCEPT(Fatal, "ZdbSL", "invalid live high-water value");
        m_maxUN[shard] = un;
      } else if (rc != SQLITE_DONE) throw cxnError(cxn, "read live high-water", rc);
    }
    {
      Reset reset{mrd};
      if (sqlite3_bind_int(mrd, 1, m_internal) != SQLITE_OK ||
          sqlite3_bind_text64(mrd, 2, m_id.data(), m_id.length(),
            SQLITE_STATIC, SQLITE_UTF8) != SQLITE_OK ||
          sqlite3_bind_int64(mrd, 3, shard) != SQLITE_OK)
        throw cxnError(cxn, "bind delete high-water identity");
      int rc = sqlite3_step(mrd);
      if (rc == SQLITE_ROW) {
        uint64_t un;
        uint128_t sn;
        if (!loadU64(columnBlob(mrd, 0), un) ||
            !loadU128(columnBlob(mrd, 1), sn))
          throw ZeEXCEPT(Fatal, "ZdbSL", "invalid delete high-water value");
        if (un != ZdbNullUN() &&
            (m_maxUN[shard] == ZdbNullUN() || un > m_maxUN[shard]))
          m_maxUN[shard] = un;
        if (sn != ZdbNullSN() && (m_maxSN == ZdbNullSN() || sn > m_maxSN))
          m_maxSN = sn;
      } else if (rc != SQLITE_DONE) throw cxnError(cxn, "read delete high-water", rc);
    }
  }
  {
    SQLString sql{"SELECT \"_sn\" FROM "};
    sql << m_relation << " ORDER BY \"_sn\" DESC LIMIT 1";
    Stmt stmt;
    stmt.ptr = prepare(cxn, sql);
    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
      uint128_t sn;
      if (!loadU128(columnBlob(stmt, 0), sn))
        throw ZeEXCEPT(Fatal, "ZdbSL", "invalid live sequence value");
      if (m_maxSN == ZdbNullSN() || sn > m_maxSN) m_maxSN = sn;
    } else if (rc != SQLITE_DONE) {
      throw cxnError(cxn, "read live sequence", rc);
    }
  }
}

void StoreTbl::open_(OpenFn fn)
{
  try {
    if (m_openState == OpenState::Closed) {
      schema_();
      statements_();
    }
    state_();
    m_openState = OpenState::Opened;
    fn(OpenResult{OpenData{
      .storeTbl = this, .count = m_count, .un = m_maxUN, .sn = m_maxSN}});
  } catch (const ZeException &e) {
    if (m_openState == OpenState::Closed) finalize_();
    fn(OpenResult{e});
  }
}

void StoreTbl::close(CloseFn fn)
{
  m_store->run([this, fn = ZuMv(fn)]() mutable { close_(ZuMv(fn)); });
}

void StoreTbl::close_(CloseFn fn)
{
  if (m_openState == OpenState::Opened) m_openState = OpenState::Reopen;
  fn();
}

void StoreTbl::warmup() { }

void StoreTbl::finalize_()
{
  auto finalize = [](sqlite3_stmt *&stmt) {
    if (stmt) sqlite3_finalize(stmt);
    stmt = nullptr;
  };
  for (auto &stmts: m_stmts.keys) {
    finalize(stmts.count);
    finalize(stmts.selectKIX);
    finalize(stmts.selectKNX);
    finalize(stmts.selectKNI);
    finalize(stmts.selectRIX);
    finalize(stmts.selectRNX);
    finalize(stmts.selectRNI);
    finalize(stmts.find);
  }
  finalize(m_stmts.recover);
  finalize(m_stmts.insert);
  finalize(m_stmts.update);
  finalize(m_stmts.del);
  finalize(m_stmts.mrd);
}

static ZdbMem::Offset saveTuple(
    Zfb::Builder &fbb, sqlite3_stmt *stmt, const XFields &fields,
    unsigned base = 0)
{
  unsigned n = fields.length();
  auto offsets_ = ZmScratch(ZdbMem::Offset, n);
  ZdbMem::Offsets offsets(offsets_.data());
  if (!offsets) return {};
  for (unsigned i = 0; i < n; ++i) {
    unsigned type = fields[i].field.type;
    if (type != Value::Index<ZdbMem::String>{} &&
        type != Value::Index<ZdbMem::Bytes>{} &&
        type != Value::Index<ZtBitmap>{} &&
        type != Value::Index<ZiIP>{} && !ZdbMem::isVec(type))
      continue;
    Value value;
    if (!decode(&value, type, stmt, base + i))
      throw ZeEXCEPT(Fatal, "ZdbSL", "invalid stored tuple value");
    ZuSwitch::dispatch<Value::N>(type,
      [&fbb, &offsets, &value](auto I) {
        ZdbMem::saveOffset<I>(fbb, offsets, value);
      });
  }
  auto start = fbb.StartTable();
  for (unsigned i = 0; i < n; ++i) {
    unsigned type = fields[i].field.type;
    Value value;
    if ((type == Value::Index<ZiIP>{} ||
         (type != Value::Index<ZdbMem::String>{} &&
          type != Value::Index<ZdbMem::Bytes>{} &&
          type != Value::Index<ZtBitmap>{} && !ZdbMem::isVec(type))) &&
        !decode(&value, type, stmt, base + i))
      throw ZeEXCEPT(Fatal, "ZdbSL", "invalid stored tuple value");
    ZuSwitch::dispatch<Value::N>(type,
      [&fbb, &offsets, field = fields[i].field.field,
       &value](auto I) {
        ZdbMem::saveValue<I>(fbb, offsets, field, value);
      });
  }
  return ZdbMem::Offset{fbb.EndTable(start)};
}

ZmRef<IOBuf> StoreTbl::tuple_(sqlite3_stmt *stmt, const XFields &fields)
{
  Zfb::IOBuilder fbb{m_bufAllocFn()};
  fbb.Finish(saveTuple(fbb, stmt, fields));
  return fbb.buf();
}

template <bool Recovery>
ZmRef<IOBuf> StoreTbl::row_(sqlite3_stmt *stmt)
{
  if (sqlite3_column_type(stmt, 0) != SQLITE_INTEGER ||
      sqlite3_column_type(stmt, 3) != SQLITE_INTEGER)
    throw ZeEXCEPT(Fatal, "ZdbSL", "invalid stored row metadata");
  Shard shard = sqlite3_column_int64(stmt, 0);
  UN un;
  SN sn;
  if (!loadU64(columnBlob(stmt, 1), un) ||
      !loadU128(columnBlob(stmt, 2), sn))
    throw ZeEXCEPT(Fatal, "ZdbSL", "invalid stored row sequence metadata");
  VN vn = sqlite3_column_int64(stmt, 3);
  Zfb::IOBuilder fbb{m_bufAllocFn()};
  auto data = Zfb::Save::nest(fbb, [this, &fbb, stmt](Zfb::Builder &) {
    return saveTuple(fbb, stmt, m_xFields, 4);
  });
  auto sn_ = ZfbTransform::UInt128::save(sn);
  auto msg = fbs::CreateMsg(
    fbb, Recovery ? fbs::Body::Recovery : fbs::Body::Replication,
    fbs::CreateRecord(
      fbb, Zfb::Save::str(fbb, id()), un, &sn_, vn, shard, data).Union());
  fbb.Finish(msg);
  return saveHdr(fbb);
}

static Event shutdownError(ZuCSpan operation, ZuCSpan id)
{
  return ZeEXCEPT(Error, "ZdbSL", ([
    operation = ZeString{operation}, id = ZeString{id}
  ](auto &s, const auto &) {
    s << operation << '(' << id << ") failed - DB shutdown in progress";
  }));
}

void StoreTbl::count(KeyID keyID, ZmRef<IOBuf> buf, CountFn fn)
{
  m_store->run([
    this, keyID, buf = ZuMv(buf), fn = ZuMv(fn)
  ]() mutable { count_(keyID, ZuMv(buf), ZuMv(fn)); });
}

void StoreTbl::count_(KeyID keyID, ZmRef<IOBuf> buf, CountFn fn)
{
  if (m_store->stopping()) {
    fn(CountResult{shutdownError("count", m_id)});
    return;
  }
  sqlite3_stmt *stmt = m_stmts.keys[keyID].count;
  Reset reset{stmt};
  try {
    unsigned n = m_keyGroup[keyID];
    auto fbo = Zfb::GetAnyRoot(buf->data());
    auto size = inputSize(m_store->conn(), n, m_xKeyFields[keyID], fbo);
    auto scratch = ZmScratch(uint8_t, size);
    bindInput(m_store->conn(), stmt, 1,
      n, m_xKeyFields[keyID], fbo, {scratch.data(), size});
    int rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) throw cxnError(m_store->conn(), "count", rc);
    fn(CountResult{CountData{uint64_t(sqlite3_column_int64(stmt, 0))}});
  } catch (const ZeException &e) {
    fn(CountResult{e});
  }
}

void StoreTbl::select(
  bool selectRow, bool selectNext, bool inclusive,
  KeyID keyID, ZmRef<IOBuf> buf, unsigned limit, TupleFn fn)
{
  m_store->run([
    this, selectRow, selectNext, inclusive, keyID,
    buf = ZuMv(buf), limit, fn = ZuMv(fn)
  ]() mutable {
    select_(selectRow, selectNext, inclusive, keyID,
      ZuMv(buf), limit, ZuMv(fn));
  });
}

void StoreTbl::select_(
  bool selectRow, bool selectNext, bool inclusive,
  KeyID keyID, ZmRef<IOBuf> buf, unsigned limit, TupleFn fn)
{
  if (m_store->stopping()) {
    fn(TupleResult{shutdownError("select", m_id)});
    return;
  }
  auto &stmts = m_stmts.keys[keyID];
  sqlite3_stmt *stmt;
  if (!selectNext)
    stmt = selectRow ? stmts.selectRIX : stmts.selectKIX;
  else if (inclusive)
    stmt = selectRow ? stmts.selectRNI : stmts.selectKNI;
  else
    stmt = selectRow ? stmts.selectRNX : stmts.selectKNX;
  Reset reset{stmt};
  try {
    unsigned n = selectNext ? m_xKeyFields[keyID].length() : m_keyGroup[keyID];
    auto fbo = Zfb::GetAnyRoot(buf->data());
    auto size = inputSize(m_store->conn(), n, m_xKeyFields[keyID], fbo);
    auto scratch = ZmScratch(uint8_t, size);
    bindInput(m_store->conn(), stmt, 1,
      n, m_xKeyFields[keyID], fbo, {scratch.data(), size});
    int rc = sqlite3_bind_int64(stmt, n + 1, limit);
    if (rc != SQLITE_OK) throw cxnError(m_store->conn(), "bind select limit", rc);
    const auto &fields = selectRow ? m_xFields : m_xKeyFields[keyID];
    unsigned count = 0;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
      auto result = tuple_(stmt, fields);
      fn(TupleResult{TupleData{
        .keyID = selectRow ? KeyID(ZuStructKeyID::All) : keyID,
        .buf = ZuMv(result), .count = ++count}});
    }
    if (rc != SQLITE_DONE) throw cxnError(m_store->conn(), "select", rc);
    fn(TupleResult{});
  } catch (const ZeException &e) {
    fn(TupleResult{e});
  }
}

void StoreTbl::find(KeyID keyID, ZmRef<IOBuf> buf, RowFn fn)
{
  m_store->run([
    this, keyID, buf = ZuMv(buf), fn = ZuMv(fn)
  ]() mutable { find_(keyID, ZuMv(buf), ZuMv(fn)); });
}

void StoreTbl::find_(KeyID keyID, ZmRef<IOBuf> buf, RowFn fn)
{
  if (m_store->stopping()) {
    fn(RowResult{shutdownError("find", m_id)});
    return;
  }
  sqlite3_stmt *stmt = m_stmts.keys[keyID].find;
  Reset reset{stmt};
  try {
    unsigned n = m_xKeyFields[keyID].length();
    auto fbo = Zfb::GetAnyRoot(buf->data());
    auto size = inputSize(m_store->conn(), n, m_xKeyFields[keyID], fbo);
    auto scratch = ZmScratch(uint8_t, size);
    bindInput(m_store->conn(), stmt, 1,
      n, m_xKeyFields[keyID], fbo, {scratch.data(), size});
    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_DONE) { fn(RowResult{}); return; }
    if (rc != SQLITE_ROW) throw cxnError(m_store->conn(), "find", rc);
    fn(RowResult{RowData{.buf = row_<false>(stmt)}});
  } catch (const ZeException &e) {
    fn(RowResult{e});
  }
}

void StoreTbl::recover(Shard shard, UN un, RowFn fn)
{
  m_store->run([
    this, shard, un, fn = ZuMv(fn)
  ]() mutable { recover_(shard, un, ZuMv(fn)); });
}

void StoreTbl::recover_(Shard shard, UN un, RowFn fn)
{
  if (m_store->stopping()) {
    fn(RowResult{shutdownError("recover", m_id)});
    return;
  }
  sqlite3_stmt *stmt = m_stmts.recover;
  Reset reset{stmt};
  try {
    uint8_t data[8];
    saveU64(data, un);
    if (sqlite3_bind_int64(stmt, 1, shard) != SQLITE_OK ||
        sqlite3_bind_blob64(stmt, 2, data, sizeof(data),
          SQLITE_STATIC) != SQLITE_OK)
      throw cxnError(m_store->conn(), "bind recovery key");
    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_DONE) { fn(RowResult{}); return; }
    if (rc != SQLITE_ROW) throw cxnError(m_store->conn(), "recover", rc);
    fn(RowResult{RowData{.buf = row_<true>(stmt)}});
  } catch (const ZeException &e) {
    fn(RowResult{e});
  }
}

static void bindMeta(
  sqlite3 *cxn, sqlite3_stmt *stmt, unsigned first,
  UN un, SN sn, VN vn, uint8_t *unData, uint8_t *snData)
{
  saveU64(unData, un);
  saveU128(snData, sn);
  if (sqlite3_bind_blob64(stmt, first, unData, 8, SQLITE_STATIC) != SQLITE_OK ||
      sqlite3_bind_blob64(stmt, first + 1, snData, 16, SQLITE_STATIC) != SQLITE_OK ||
      sqlite3_bind_int64(stmt, first + 2, vn) != SQLITE_OK)
    throw cxnError(cxn, "bind row metadata");
}

void StoreTbl::write(ZmRef<IOBuf> buf, CommitFn fn)
{
  m_store->run([
    this, buf = ZuMv(buf), fn = ZuMv(fn)
  ]() mutable { write_(ZuMv(buf), ZuMv(fn)); });
}

void StoreTbl::write_(ZmRef<IOBuf> buf, CommitFn fn)
{
  if (m_store->stopping()) {
    fn(ZuMv(buf), CommitResult{shutdownError("write", m_id)});
    return;
  }
  auto record = record_(msg_(buf->hdr()));
  Shard shard = record->shard();
  UN un = record->un();
  SN sn = ZfbTransform::UInt128::load(record->sn());
  VN vn = record->vn();
  if (shard >= m_maxUN.length()) {
    fn(ZuMv(buf), CommitResult{ZeEXCEPT(Error, "ZdbSL", "invalid write shard")});
    return;
  }
  if (m_maxUN[shard] != ZdbNullUN() && un <= m_maxUN[shard]) {
    fn(ZuMv(buf), CommitResult{});
    return;
  }
  sqlite3 *cxn = m_store->conn();
  bool inserted = false;
  bool removed = false;
  try {
    exec(cxn, "BEGIN IMMEDIATE");
    auto fbo = Zfb::GetAnyRoot(record->data()->data());
    uint8_t unData[8], snData[16];
    bool mrd = false;
    if (!vn) {
      sqlite3_stmt *stmt = m_stmts.insert;
      Reset reset{stmt};
      if (sqlite3_bind_int64(stmt, 1, shard) != SQLITE_OK)
        throw cxnError(cxn, "bind insert shard");
      bindMeta(cxn, stmt, 2, un, sn, vn, unData, snData);
      unsigned n = m_fields.length();
      auto size = inputSize(cxn, n, m_xFields, fbo);
      auto scratch = ZmScratch(uint8_t, size);
      bindInput(cxn, stmt, 5, n, m_xFields, fbo, {scratch.data(), size});
      done(cxn, stmt);
      inserted = sqlite3_changes(cxn) == 1;
      mrd = !inserted;
    } else if (vn > 0) {
      sqlite3_stmt *stmt = m_stmts.update;
      Reset reset{stmt};
      bindMeta(cxn, stmt, 1, un, sn, vn, unData, snData);
      unsigned nUpdate = m_updFields.length();
      unsigned nKey = m_keyFields[0].length();
      auto updateSize = inputSize(cxn, nUpdate, m_xUpdFields, fbo);
      auto keySize = inputSize(cxn, nKey, m_xKeyFields[0], fbo);
      if (keySize > UINT_MAX - updateSize)
        throw cxnError(cxn, "encode SQLite values", SQLITE_TOOBIG);
      unsigned size = updateSize + keySize;
      auto scratch = ZmScratch(uint8_t, size);
      bindInput(cxn, stmt, 4, nUpdate, m_xUpdFields, fbo,
        {scratch.data(), updateSize});
      bindInput(cxn, stmt, 4 + nUpdate, nKey, m_xKeyFields[0], fbo,
        {scratch.data() + updateSize, keySize});
      done(cxn, stmt);
      if (sqlite3_changes(cxn) != 1)
        throw ZeEXCEPT(Error, "ZdbSL", "update failed - primary key missing");
    } else {
      sqlite3_stmt *stmt = m_stmts.del;
      Reset reset{stmt};
      unsigned n = m_keyFields[0].length();
      auto size = inputSize(cxn, n, m_xKeyFields[0], fbo);
      auto scratch = ZmScratch(uint8_t, size);
      bindInput(cxn, stmt, 1,
        n, m_xKeyFields[0], fbo, {scratch.data(), size});
      done(cxn, stmt);
      if (sqlite3_changes(cxn) != 1)
        throw ZeEXCEPT(Error, "ZdbSL", "delete failed - primary key missing");
      removed = true;
      mrd = true;
    }
    if (mrd) {
      sqlite3_stmt *stmt = m_stmts.mrd;
      Reset reset{stmt};
      saveU64(unData, un);
      saveU128(snData, sn);
      if (sqlite3_bind_int(stmt, 1, m_internal) != SQLITE_OK ||
          sqlite3_bind_text64(stmt, 2, m_id.data(), m_id.length(),
            SQLITE_STATIC, SQLITE_UTF8) != SQLITE_OK ||
          sqlite3_bind_int64(stmt, 3, shard) != SQLITE_OK ||
          sqlite3_bind_blob64(stmt, 4, unData, 8,
            SQLITE_STATIC) != SQLITE_OK ||
          sqlite3_bind_blob64(stmt, 5, snData, 16,
            SQLITE_STATIC) != SQLITE_OK)
        throw cxnError(cxn, "bind delete high-water state");
      done(cxn, stmt);
    }
    exec(cxn, "COMMIT");
    m_maxUN[shard] = un;
    if (m_maxSN == ZdbNullSN() || sn > m_maxSN) m_maxSN = sn;
    if (inserted) ++m_count;
    if (removed) --m_count;
    fn(ZuMv(buf), CommitResult{});
  } catch (const ZeException &e) {
    sqlite3_exec(cxn, "ROLLBACK", nullptr, nullptr, nullptr);
    fn(ZuMv(buf), CommitResult{e});
  }
}

} // ZdbSL

Zdb_::Store *ZdbStore() { return new ZdbSL::Store{}; }
