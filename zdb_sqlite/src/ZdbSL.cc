//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>

#include <zlib/ZuJoin.hh>
#include <zlib/ZuSwitch.hh>

#include <zlib/ZmLHash.hh>
#include <zlib/ZmObject.hh>

#include <zlib/ZtCase.hh>
#include <zlib/ZtScratch.hh>

#include <zlib/ZiLog.hh>

#include <zlib/ZdbSL.hh>
#include <zlib/ZdbSLCodec.hh>

namespace ZdbSL {

static const char *fieldMapID() { return "ZdbSL.FieldMap"; }
ZmLHashKVDerive(FieldMap, IDString, unsigned,
  ZmLHashID<fieldMapID, ZmLHashLocal<>>);
struct FieldMapObj : public ZmObject {
  FieldMap map;

  FieldMapObj(unsigned n) : map{ZmHashParams(n)} { }
};

struct ExpectedIndex {
  SQLString		id;
  const XFieldRefs	*fields = nullptr;
  bool			unique = false;
  bool			seen = false;
};
ZuDerive(ExpectedIndices,
  (ZtArray<ExpectedIndex, ZtArrayHeapID<"ZdbSL.ExpectedIndex">>));
static const char *indexMapID() { return "ZdbSL.IndexMap"; }
ZmLHashKVDerive(IndexMap, ZuCSpan, unsigned,
  ZmLHashID<indexMapID, ZmLHashLocal<>>);
struct IndexMapObj : public ZmObject {
  IndexMap map;

  IndexMapObj(unsigned n) : map{ZmHashParams(n)} { }
};

ZtEnumImplNS(Synchronous);

struct Type {
  enum {
    Void,
    String, Bytes, Bool,
    Int8, UInt8, Int16, UInt16, Int32, UInt32, Int64, UInt64,
    Float, Fixed, Decimal, Time, DateTime, Int128, UInt128, Bitmap, IP,
    StringVec, BytesVec,
    Int8Vec, UInt8Vec, Int16Vec, UInt16Vec, Int32Vec, UInt32Vec,
    Int64Vec, UInt64Vec, Int128Vec, UInt128Vec, FloatVec, FixedVec,
    DecimalVec, TimeVec, DateTimeVec,
    N
  };
};

struct Storage { enum { Integer, Text, Blob }; };

static bool isVec(unsigned type) { return type >= Type::StringVec; }

static const XField &fieldAt(const XFields &fields, unsigned i)
{
  return fields[i];
}

static const XField &fieldAt(const XFieldRefs &fields, unsigned i)
{
  return *fields[i].field;
}

struct Stmt {
  sqlite3_stmt *ptr = nullptr;
  ~Stmt() { if (ptr) sqlite3_finalize(ptr); }
  sqlite3_stmt **operator &() { return &ptr; }
  operator sqlite3_stmt *() const { return ptr; }
};

struct Reset {
  sqlite3_stmt *stmt;
  // All parameters are rebound before the next step.  Reset while any
  // SQLITE_STATIC source and scratch storage is still alive.
  ~Reset() { sqlite3_reset(stmt); }
};

static ZeException cxnError(
  sqlite3 *, ZuCSpan, int = SQLITE_ERROR);

class Txn {
public:
  Txn(sqlite3 *cxn, sqlite3_stmt *begin, sqlite3_stmt *commit) :
    m_cxn{cxn}, m_commit{commit}
  {
    int rc = sqlite3_step(begin);
    if (rc != SQLITE_DONE) {
      sqlite3_reset(begin);
      throw cxnError(m_cxn, "begin transaction", rc);
    }
    m_active = true;
    rc = sqlite3_reset(begin);
    if (rc != SQLITE_OK) {
      sqlite3_exec(m_cxn, "ROLLBACK", nullptr, nullptr, nullptr);
      m_active = false;
      throw cxnError(m_cxn, "begin transaction", rc);
    }
  }
  ~Txn() {
    if (m_active) sqlite3_exec(m_cxn, "ROLLBACK", nullptr, nullptr, nullptr);
  }

  void commit() {
    int rc = sqlite3_step(m_commit);
    if (rc == SQLITE_DONE) m_active = false;
    int resetRC = sqlite3_reset(m_commit);
    if (rc != SQLITE_DONE) throw cxnError(m_cxn, "commit transaction", rc);
    if (resetRC != SQLITE_OK)
      throw cxnError(m_cxn, "commit transaction", resetRC);
  }

private:
  sqlite3	*m_cxn;
  sqlite3_stmt	*m_commit;
  bool		m_active = false;
};

static ZeException cxnError(
  sqlite3 *cxn, ZuCSpan operation, int rc)
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

struct QuoteID {
  ZuCSpan id;

  template <typename S> void print(S &s) const {
    s << '"';
    for (char c: id) {
      s << c;
      if (c == '"') s << c;
    }
    s << '"';
  }
  friend ZuPrintFn ZuPrintType(QuoteID *);
};

static QuoteID quoteID(ZuCSpan id) { return {id}; }

static unsigned resolveType(
  const Zfb::Vector<Zfb::Offset<reflection::Field>> *fields,
  const ZfVField *field, ZuCSpan id, const reflection::Field *&fbField)
{
  // StoreTbl receives erased Zf metadata, so run-time reflection is the
  // required boundary for matching it to the generated FlatBuffer schema.
  fbField = fields->LookupByKey(id);
  if (!fbField) return Type::Void;
  auto ftype = field->type;
  switch (fbField->type()->base_type()) {
    case reflection::String:
      if (ftype->code == ZfFieldTC::CString ||
          ftype->code == ZfFieldTC::String) return Type::String;
      break;
    case reflection::Bool:
      if (ftype->code == ZfFieldTC::Bool) return Type::Bool;
      break;
    case reflection::Byte:
      if (ftype->code == ZfFieldTC::Int8) return Type::Int8;
      break;
    case reflection::UByte:
      if (ftype->code == ZfFieldTC::UInt8) return Type::UInt8;
      break;
    case reflection::Short:
      if (ftype->code == ZfFieldTC::Int16) return Type::Int16;
      break;
    case reflection::UShort:
      if (ftype->code == ZfFieldTC::UInt16) return Type::UInt16;
      break;
    case reflection::Int:
      if (ftype->code == ZfFieldTC::Int32) return Type::Int32;
      break;
    case reflection::UInt:
      if (ftype->code == ZfFieldTC::UInt32) return Type::UInt32;
      break;
    case reflection::Long:
      if (ftype->code == ZfFieldTC::Int64) return Type::Int64;
      break;
    case reflection::ULong:
      if (ftype->code == ZfFieldTC::UInt64) return Type::UInt64;
      break;
    case reflection::Double:
      if (ftype->code == ZfFieldTC::Float) return Type::Float;
      break;
    case reflection::Obj:
      switch (ftype->code) {
        case ZfFieldTC::Int128: return Type::Int128;
        case ZfFieldTC::UInt128: return Type::UInt128;
        case ZfFieldTC::Fixed: return Type::Fixed;
        case ZfFieldTC::Decimal: return Type::Decimal;
        case ZfFieldTC::Time: return Type::Time;
        case ZfFieldTC::DateTime: return Type::DateTime;
        case ZfFieldTC::UDT:
          if (ftype->info.udt()->id == "Bitmap") return Type::Bitmap;
          if (ftype->info.udt()->id == "IP") return Type::IP;
          break;
      }
      break;
    case reflection::Union:
      if (ftype->code == ZfFieldTC::UDT && ftype->info.udt()->id == "IP")
        return Type::IP;
      break;
    case reflection::Vector:
      switch (fbField->type()->element()) {
        case reflection::String:
          if (ftype->code == ZfFieldTC::StringVec) return Type::StringVec;
          break;
        case reflection::Byte:
          if (ftype->code == ZfFieldTC::Int8Vec) return Type::Int8Vec;
          break;
        case reflection::UByte:
          if (ftype->code == ZfFieldTC::Bytes) return Type::Bytes;
          if (ftype->code == ZfFieldTC::UInt8Vec) return Type::UInt8Vec;
          break;
        case reflection::Short:
          if (ftype->code == ZfFieldTC::Int16Vec) return Type::Int16Vec;
          break;
        case reflection::UShort:
          if (ftype->code == ZfFieldTC::UInt16Vec) return Type::UInt16Vec;
          break;
        case reflection::Int:
          if (ftype->code == ZfFieldTC::Int32Vec) return Type::Int32Vec;
          break;
        case reflection::UInt:
          if (ftype->code == ZfFieldTC::UInt32Vec) return Type::UInt32Vec;
          break;
        case reflection::Long:
          if (ftype->code == ZfFieldTC::Int64Vec) return Type::Int64Vec;
          break;
        case reflection::ULong:
          if (ftype->code == ZfFieldTC::UInt64Vec) return Type::UInt64Vec;
          break;
        case reflection::Double:
          if (ftype->code == ZfFieldTC::FloatVec) return Type::FloatVec;
          break;
        case reflection::Obj:
          switch (ftype->code) {
            case ZfFieldTC::BytesVec: return Type::BytesVec;
            case ZfFieldTC::Int128Vec: return Type::Int128Vec;
            case ZfFieldTC::UInt128Vec: return Type::UInt128Vec;
            case ZfFieldTC::FixedVec: return Type::FixedVec;
            case ZfFieldTC::DecimalVec: return Type::DecimalVec;
            case ZfFieldTC::TimeVec: return Type::TimeVec;
            case ZfFieldTC::DateTimeVec: return Type::DateTimeVec;
          }
          break;
        default: break;
      }
      break;
    default: break;
  }
  return Type::Void;
}

static void validateCoreStrict(sqlite3 *cxn)
{
  Stmt stmt;
  stmt.ptr = prepare(cxn,
    "SELECT \"name\", \"strict\" FROM pragma_table_list "
    "WHERE \"schema\"='main' AND \"name\" IN "
    "('zdbsl_meta','zdbsl_schema','zdbsl_mrd')");
  bool meta = false, schema = false, mrd = false;
  int rc;
  while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
    ZuCSpan table{
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0)),
      unsigned(sqlite3_column_bytes(stmt, 0))};
    bool strict = sqlite3_column_int(stmt, 1) == 1;
    if (table == "zdbsl_meta") meta = strict;
    else if (table == "zdbsl_schema") schema = strict;
    else if (table == "zdbsl_mrd") mrd = strict;
  }
  if (rc != SQLITE_DONE) throw cxnError(cxn, "inspect STRICT tables", rc);
  if (!meta || !schema || !mrd)
    throw ZeEXCEPT(Fatal, "ZdbSL", "internal table is missing or not STRICT");
}

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

struct CoreTable { enum { Meta, Schema, MRD }; };
struct KeyDir { enum { Ascending, Descending, Mixed }; };

static void validateCoreTable(
    sqlite3 *cxn, ZuCSpan table, unsigned core, unsigned nColumns)
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
      case CoreTable::Meta:
        switch (column) {
          case 0: id = "id"; type = "TEXT"; pk = 1; break;
          case 1: id = "value"; type = "INTEGER"; break;
        }
        break;
      case CoreTable::Schema:
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
      case CoreTable::MRD:
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

static const char *storageName(unsigned storage)
{
  switch (storage) {
    case Storage::Integer: return "INTEGER";
    case Storage::Text: return "TEXT";
    default: return "BLOB";
  }
}

static unsigned storageID(unsigned type)
{
  switch (type) {
    case Type::Bool:
    case Type::Int8:
    case Type::UInt8:
    case Type::Int16:
    case Type::UInt16:
    case Type::Int32:
    case Type::UInt32:
    case Type::Int64: return Storage::Integer;
    case Type::String: return Storage::Text;
    default: return Storage::Blob;
  }
}

static unsigned fixedSize(unsigned type)
{
  switch (type) {
    case Type::UInt64:
    case Type::Float:
    case Type::Int64Vec:
    case Type::UInt64Vec:
    case Type::FloatVec: return 8;
    case Type::Time:
    case Type::DateTime:
    case Type::TimeVec:
    case Type::DateTimeVec: return 12;
    case Type::Fixed:
    case Type::Decimal:
    case Type::Int128:
    case Type::UInt128:
    case Type::FixedVec:
    case Type::DecimalVec:
    case Type::Int128Vec:
    case Type::UInt128Vec: return 16;
    case Type::Int8Vec:
    case Type::UInt8Vec: return 1;
    case Type::Int16Vec:
    case Type::UInt16Vec: return 2;
    case Type::Int32Vec:
    case Type::UInt32Vec: return 4;
    default: return 0;
  }
}

struct VarPart {
  unsigned offset;
  unsigned length;
};
ZuDerive(VarPartsScratch,
  (ZtArray<VarPart, ZtArrayHeapID<"ZdbSL.VarParts",
    ZtArraySharded<true>>>));
ZuDerive(VarBufScratch,
  (ZtArray<uint8_t, ZtArrayHeapID<"ZdbSL.VarBuf",
    ZtArraySharded<true>>>));

static uint64_t vecSize(uint64_t n, unsigned width)
{
  return sizeof(uint32_t) + n * width;
}

static uint64_t encodedSize(const XField &xfield, const Zfb::Table *fbo)
{
  const reflection::Field &field = *xfield.field.field;
  switch (xfield.field.type) {
    case Type::String:
    case Type::Bytes:
    case Type::Bool:
    case Type::Int8:
    case Type::UInt8:
    case Type::Int16:
    case Type::UInt16:
    case Type::Int32:
    case Type::UInt32:
    case Type::Int64:
      return 0;
    case Type::Bitmap: {
      auto bitmap = fbo->GetPointer<const Zfb::Bitmap *>(field.offset());
      auto data = bitmap ? bitmap->data() : nullptr;
      return vecSize(data ? data->size() : 0, sizeof(uint64_t));
    }
    case Type::IP: {
      ZiIP ip = ZfbTransform::IP::load(
        static_cast<Zfb::IP>(
          fbo->GetField<uint8_t>(field.offset() - 2, 0)),
        fbo->GetPointer<const void *>(field.offset()));
      switch (ip.type()) {
        case ZiIPType::V4: return 1 + sizeof(in_addr);
        case ZiIPType::V6: return 1 + sizeof(in6_addr);
        default: return 1;
      }
    }
    case Type::StringVec: {
      auto values =
        Zfb::GetFieldV<Zfb::Offset<Zfb::String>>(*fbo, field);
      uint64_t size = sizeof(uint32_t);
      unsigned n = values ? values->size() : 0;
      for (unsigned i = 0; i < n; ++i) {
        uint64_t length = values->Get(i)->size();
        if (length > UINT64_MAX - sizeof(uint32_t) ||
            size > UINT64_MAX - sizeof(uint32_t) - length)
          return UINT64_MAX;
        size += sizeof(uint32_t) + length;
      }
      return size;
    }
    case Type::BytesVec: {
      auto values =
        Zfb::GetFieldV<Zfb::Offset<Zfb::Bytes>>(*fbo, field);
      uint64_t size = sizeof(uint32_t);
      unsigned n = values ? values->size() : 0;
      for (unsigned i = 0; i < n; ++i) {
        auto data = values->Get(i)->data();
        uint64_t length = data ? data->size() : 0;
        if (length > UINT64_MAX - sizeof(uint32_t) ||
            size > UINT64_MAX - sizeof(uint32_t) - length)
          return UINT64_MAX;
        size += sizeof(uint32_t) + length;
      }
      return size;
    }
#define ZdbSL_VecSize(Type_, Elem_) \
    case Type::Type_: { \
      auto values = Zfb::GetFieldV<Elem_>(*fbo, field); \
      return vecSize(values ? values->size() : 0, xfield.field.fixedSize); \
    }
    ZdbSL_VecSize(Int8Vec, int8_t)
    ZdbSL_VecSize(UInt8Vec, uint8_t)
    ZdbSL_VecSize(Int16Vec, int16_t)
    ZdbSL_VecSize(UInt16Vec, uint16_t)
    ZdbSL_VecSize(Int32Vec, int32_t)
    ZdbSL_VecSize(UInt32Vec, uint32_t)
    ZdbSL_VecSize(Int64Vec, int64_t)
    ZdbSL_VecSize(UInt64Vec, uint64_t)
    ZdbSL_VecSize(Int128Vec, Zfb::Int128 *)
    ZdbSL_VecSize(UInt128Vec, Zfb::UInt128 *)
    ZdbSL_VecSize(FloatVec, double)
    ZdbSL_VecSize(FixedVec, Zfb::Fixed *)
    ZdbSL_VecSize(DecimalVec, Zfb::Decimal *)
    ZdbSL_VecSize(TimeVec, Zfb::Time *)
    ZdbSL_VecSize(DateTimeVec, Zfb::DateTime *)
#undef ZdbSL_VecSize
    default:
      return xfield.field.fixedSize;
  }
}

template <typename Fields>
static unsigned planInput(
  sqlite3 *cxn, unsigned lengthLimit, unsigned n,
  const Fields &fields, const Zfb::Table *fbo, ZuSpan<VarPart> parts)
{
  uint64_t total = 0;
  for (unsigned i = 0; i < n; ++i) {
    uint64_t size = encodedSize(fieldAt(fields, i), fbo);
    if (size > lengthLimit || size > UINT_MAX || total > UINT_MAX - size)
      throw cxnError(cxn, "encode SQLite value", SQLITE_TOOBIG);
    parts[i] = {unsigned(total), unsigned(size)};
    total += size;
  }
  return unsigned(total);
}

static void saveCount(uint8_t *&ptr, unsigned n)
{
  saveUnsigned(ptr, uint32_t(n));
  ptr += sizeof(uint32_t);
}

template <typename V, typename L>
static void savePVec(uint8_t *ptr, const V *values, L &&save)
{
  unsigned n = values ? values->size() : 0;
  saveCount(ptr, n);
  for (unsigned i = 0; i < n; ++i) save(ptr, values->Get(i));
}

static void encodeField(
  uint8_t *ptr, const XField &xfield, const Zfb::Table *fbo)
{
  const reflection::Field &field = *xfield.field.field;
  switch (xfield.field.type) {
    case Type::UInt64:
      saveU64(ptr, Zfb::GetFieldI<uint64_t>(*fbo, field));
      break;
    case Type::Int128:
      saveS128(ptr, ZfbTransform::Int128::load(
        fbo->GetStruct<const Zfb::Int128 *>(field.offset())));
      break;
    case Type::UInt128:
      saveU128(ptr, ZfbTransform::UInt128::load(
        fbo->GetStruct<const Zfb::UInt128 *>(field.offset())));
      break;
    case Type::Float:
      saveFloat(ptr, Zfb::GetFieldF<double>(*fbo, field));
      break;
    case Type::Fixed:
      saveFixed(ptr, ZfbTransform::Fixed::load(
        fbo->GetStruct<const Zfb::Fixed *>(field.offset())));
      break;
    case Type::Decimal:
      saveDecimal(ptr, ZfbTransform::Decimal::load(
        fbo->GetStruct<const Zfb::Decimal *>(field.offset())));
      break;
    case Type::Time:
      saveTime(ptr, ZfbTransform::Time::load(
        fbo->GetStruct<const Zfb::Time *>(field.offset())));
      break;
    case Type::DateTime:
      saveDateTime(ptr, ZfbTransform::DateTime::load(
        fbo->GetStruct<const Zfb::DateTime *>(field.offset())));
      break;
    case Type::Bitmap: {
      auto bitmap = fbo->GetPointer<const Zfb::Bitmap *>(field.offset());
      auto data = bitmap ? bitmap->data() : nullptr;
      unsigned n = data ? data->size() : 0;
      saveCount(ptr, n);
      for (unsigned i = 0; i < n; ++i) {
        saveUnsigned(ptr, data->Get(i));
        ptr += sizeof(uint64_t);
      }
    } break;
    case Type::IP: {
      ZiIP ip = ZfbTransform::IP::load(
        static_cast<Zfb::IP>(
          fbo->GetField<uint8_t>(field.offset() - 2, 0)),
        fbo->GetPointer<const void *>(field.offset()));
      switch (ip.type()) {
        case ZiIPType::V4:
          *ptr++ = 4;
          memcpy(ptr, &ip.inAddr(), sizeof(in_addr));
          break;
        case ZiIPType::V6:
          *ptr++ = 6;
          memcpy(ptr, &ip.in6Addr(), sizeof(in6_addr));
          break;
        default:
          *ptr = 0;
          break;
      }
    } break;
    case Type::StringVec: {
      auto values =
        Zfb::GetFieldV<Zfb::Offset<Zfb::String>>(*fbo, field);
      unsigned n = values ? values->size() : 0;
      saveCount(ptr, n);
      for (unsigned i = 0; i < n; ++i) {
        auto value = values->Get(i);
        unsigned length = value->size();
        saveCount(ptr, length);
        memcpy(ptr, value->Data(), length);
        ptr += length;
      }
    } break;
    case Type::BytesVec: {
      auto values =
        Zfb::GetFieldV<Zfb::Offset<Zfb::Bytes>>(*fbo, field);
      unsigned n = values ? values->size() : 0;
      saveCount(ptr, n);
      for (unsigned i = 0; i < n; ++i) {
        auto data = values->Get(i)->data();
        unsigned length = data ? data->size() : 0;
        saveCount(ptr, length);
        if (length) memcpy(ptr, data->Data(), length);
        ptr += length;
      }
    } break;
#define ZdbSL_SaveUVec(Type_, Elem_) \
    case Type::Type_: { \
      auto values = Zfb::GetFieldV<Elem_>(*fbo, field); \
      savePVec(ptr, values, [](uint8_t *&out, Elem_ value) { \
        saveUnsigned(out, value); out += sizeof(Elem_); \
      }); \
    } break
#define ZdbSL_SaveSVec(Type_, Elem_, UInt_) \
    case Type::Type_: { \
      auto values = Zfb::GetFieldV<Elem_>(*fbo, field); \
      savePVec(ptr, values, [](uint8_t *&out, Elem_ value) { \
        saveSigned<Elem_, UInt_>(out, value); out += sizeof(Elem_); \
      }); \
    } break
    ZdbSL_SaveSVec(Int8Vec, int8_t, uint8_t);
    ZdbSL_SaveUVec(UInt8Vec, uint8_t);
    ZdbSL_SaveSVec(Int16Vec, int16_t, uint16_t);
    ZdbSL_SaveUVec(UInt16Vec, uint16_t);
    ZdbSL_SaveSVec(Int32Vec, int32_t, uint32_t);
    ZdbSL_SaveUVec(UInt32Vec, uint32_t);
    ZdbSL_SaveSVec(Int64Vec, int64_t, uint64_t);
    ZdbSL_SaveUVec(UInt64Vec, uint64_t);
#undef ZdbSL_SaveSVec
#undef ZdbSL_SaveUVec
#define ZdbSL_SaveObjVec(Type_, FBType_, Native_, Width_, Transform_, Save_) \
    case Type::Type_: { \
      auto values = Zfb::GetFieldV<FBType_ *>(*fbo, field); \
      savePVec(ptr, values, [](uint8_t *&out, const FBType_ *value) { \
        Native_ native = ZfbTransform::Transform_::load(value); \
        Save_(out, native); \
        out += sizeof(Width_); \
      }); \
    } break
    ZdbSL_SaveObjVec(
      Int128Vec, Zfb::Int128, int128_t, int128_t, Int128, saveS128);
    ZdbSL_SaveObjVec(
      UInt128Vec, Zfb::UInt128, uint128_t, uint128_t, UInt128, saveU128);
    ZdbSL_SaveObjVec(
      FixedVec, Zfb::Fixed, ZuFixed, int128_t, Fixed, saveFixed);
    ZdbSL_SaveObjVec(
      DecimalVec, Zfb::Decimal, ZuDecimal, int128_t, Decimal, saveDecimal);
    ZdbSL_SaveObjVec(
      TimeVec, Zfb::Time, ZuTime, TimeData, Time, saveTime);
    ZdbSL_SaveObjVec(
      DateTimeVec, Zfb::DateTime, ZuDateTime, DateTimeData,
      DateTime, saveDateTime);
#undef ZdbSL_SaveObjVec
    case Type::FloatVec: {
      auto values = Zfb::GetFieldV<double>(*fbo, field);
      savePVec(ptr, values, [](uint8_t *&out, double value) {
        saveFloat(out, value);
        out += sizeof(double);
      });
    } break;
    default:
      break;
  }
}

template <typename Fields>
static void bindInput(
  sqlite3 *cxn, sqlite3_stmt *stmt, unsigned first,
  unsigned n, const Fields &fields, const Zfb::Table *fbo,
  ZuSpan<const VarPart> parts, ZuSpan<uint8_t> data)
{
  static const char emptyText = 0;
  static const uint8_t emptyBlob = 0;
  for (unsigned i = 0; i < n; ++i) {
    const XField &xfield = fieldAt(fields, i);
    const reflection::Field &field = *xfield.field.field;
    int rc;
    switch (xfield.field.type) {
      case Type::String: {
        ZuCSpan value = Zfb::Load::str(Zfb::GetFieldS(*fbo, field));
        rc = sqlite3_bind_text64(stmt, first + i,
          value.length() ? value.data() : &emptyText, value.length(),
          SQLITE_STATIC, SQLITE_UTF8);
      } break;
      case Type::Bytes: {
        ZuBSpan value =
          Zfb::Load::bytes(Zfb::GetFieldV<uint8_t>(*fbo, field));
        rc = sqlite3_bind_blob64(stmt, first + i,
          value.length() ? value.data() : &emptyBlob, value.length(),
          SQLITE_STATIC);
      } break;
      case Type::Bool:
        rc = sqlite3_bind_int64(
          stmt, first + i, Zfb::GetFieldI<bool>(*fbo, field));
        break;
#define ZdbSL_BindInt(Type_, CType_) \
      case Type::Type_: \
        rc = sqlite3_bind_int64( \
          stmt, first + i, Zfb::GetFieldI<CType_>(*fbo, field)); \
        break
      ZdbSL_BindInt(Int8, int8_t);
      ZdbSL_BindInt(UInt8, uint8_t);
      ZdbSL_BindInt(Int16, int16_t);
      ZdbSL_BindInt(UInt16, uint16_t);
      ZdbSL_BindInt(Int32, int32_t);
      ZdbSL_BindInt(UInt32, uint32_t);
      ZdbSL_BindInt(Int64, int64_t);
#undef ZdbSL_BindInt
      default: {
        const VarPart &part = parts[i];
        uint8_t *ptr = part.length ? data.data() + part.offset : nullptr;
        encodeField(ptr, xfield, fbo);
        rc = sqlite3_bind_blob64(stmt, first + i,
          part.length ? ptr : &emptyBlob, part.length, SQLITE_STATIC);
      } break;
    }
    if (rc != SQLITE_OK) throw cxnError(cxn, "bind SQLite value", rc);
  }
}
static ZuBSpan columnBlob(sqlite3_stmt *stmt, unsigned col)
{
  return {
    static_cast<const uint8_t *>(sqlite3_column_blob(stmt, col)),
    unsigned(sqlite3_column_bytes(stmt, col))};
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
  m_lengthLimit = 0;
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
    m_lengthLimit = sqlite3_limit(m_conn, SQLITE_LIMIT_LENGTH, -1);
    startup_();
    m_beginStmt = prepare(m_conn, "BEGIN IMMEDIATE");
    m_commitStmt = prepare(m_conn, "COMMIT");
    fn(StartResult{});
  } catch (const ZeException &e) {
    if (m_beginStmt) sqlite3_finalize(m_beginStmt);
    if (m_commitStmt) sqlite3_finalize(m_commitStmt);
    m_beginStmt = m_commitStmt = nullptr;
    if (m_conn) sqlite3_close_v2(m_conn);
    m_conn = nullptr;
    m_lengthLimit = 0;
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
      "\"shard\" INTEGER NOT NULL, "
      "\"un\" BLOB NOT NULL, \"sn\" BLOB NOT NULL, "
      "PRIMARY KEY (\"internal\", \"tbl\", \"shard\")) STRICT");
    validateCoreTable(m_conn, "zdbsl_meta", CoreTable::Meta, 2);
    validateCoreTable(m_conn, "zdbsl_schema", CoreTable::Schema, 11);
    validateCoreTable(m_conn, "zdbsl_mrd", CoreTable::MRD, 5);
    validateCoreStrict(m_conn);
    {
      Stmt stmt;
      stmt.ptr = prepare(m_conn,
        "INSERT INTO \"zdbsl_meta\" (\"id\", \"value\") "
        "VALUES ('format', 2), ('nShards', ?) ON CONFLICT DO NOTHING");
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
        if ((id == "format" && value != 2) ||
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
  while (auto tbl = i()) tbl->stopped();
  if (m_beginStmt) sqlite3_finalize(m_beginStmt);
  if (m_commitStmt) sqlite3_finalize(m_commitStmt);
  m_beginStmt = m_commitStmt = nullptr;
  int rc = m_conn ? sqlite3_close(m_conn) : SQLITE_OK;
  if (rc != SQLITE_OK) {
    fn(StopResult{cxnError(m_conn, "sqlite3_close()", rc)});
    return;
  }
  m_conn = nullptr;
  m_lengthLimit = 0;
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
      if (!tbl->openable()) {
        openFn(OpenResult{ZeEXCEPT(Error, "ZdbSL", ([id = ZeString{id}](auto &s, const auto &) {
          s << "open(" << id << ") failed - already open";
        }))});
        return;
      }
      tbl->open(ZuMv(openFn));
      return;
    }
    try {
      auto tbl_ = new StoreTbls::Node{
        this, internal, ZuMv(id), m_nShards,
        ZuMv(fields), ZuMv(keyFields), schema, ZuMv(bufAllocFn)};
      m_storeTbls->addNode(tbl_);
      tbl_->open(ZuMv(openFn));
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
  m_store{store}, m_id{ZuMv(id)},
  m_bufAllocFn{ZuMv(bufAllocFn)}, m_internal{internal}
{
  SQLString physical;
  physical << (m_internal ? "i_" : "a_") << m_id;
  m_relation = quoteID(physical);
  auto fbFields = schema->root_table()->fields();
  unsigned n = fields.length();
  m_xFields.size(n);
  ZmRef<FieldMapObj> fieldMap = new FieldMapObj{n};
  for (unsigned i = 0; i < n; ++i) {
    ZtCase::camelSnake(fields[i]->id, [this, &fields, fieldMap, fbFields, i](ZuCSpan id_) {
      const reflection::Field *fbField = nullptr;
      unsigned type = resolveType(fbFields, fields[i], id_, fbField);
      XField *xfield = m_xFields.push();
      new (xfield) XField{
        .id = IDString{id_}, .vfield = fields[i],
        .field = {
          .field = fbField,
          .type = uint8_t(type),
          .storage = uint8_t(storageID(type)),
          .fixedSize = uint8_t(fixedSize(type))
        }};
      if (!xfield->field.field || !xfield->field.type)
        throw ZeEXCEPT(Fatal, "ZdbSL", ([id = ZeString{id_}](auto &s, const auto &) {
          s << "unsupported or inconsistent field " << id;
        }));
      fieldMap->map.add(xfield->id, i);
      if (fields[i]->props & ZfVFieldProp::Mutable())
        m_xUpdFields.push(XFieldRef{.field = xfield});
    });
  }
  n = keyFields.length();
  m_xKeyFields.size(n);
  m_keyGroup.length(n);
  m_keyDirs.length(n);
  for (unsigned i = 0; i < n; ++i) {
    new (m_xKeyFields.push()) XFieldRefs{keyFields[i].length()};
    m_keyGroup[i] = 0;
    for (unsigned j = 0; j < keyFields[i].length(); ++j) {
      if (keyFields[i][j]->group & (uint64_t(1)<<i)) m_keyGroup[i] = j + 1;
      bool descend = keyFields[i][j]->descend & (uint64_t(1)<<i);
      ZtCase::camelSnake(keyFields[i][j]->id,
          [this, fieldMap, i, descend](ZuCSpan id_) {
        auto field = fieldMap->map.findVal(id_);
        m_xKeyFields[i].push(XFieldRef{
          .field = &m_xFields[field], .descend = descend});
      });
    }
    unsigned group = m_keyGroup[i];
    unsigned dir = KeyDir::Ascending;
    if (group < keyFields[i].length()) {
      bool descend = m_xKeyFields[i][group].descend;
      dir = descend ? KeyDir::Descending : KeyDir::Ascending;
      for (unsigned j = group + 1; j < keyFields[i].length(); ++j)
        if (m_xKeyFields[i][j].descend != descend) {
          dir = KeyDir::Mixed;
          break;
        }
    }
    m_keyDirs[i] = dir;
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
      "\"_shard\" INTEGER NOT NULL, "
      "\"_un\" BLOB NOT NULL, \"_sn\" BLOB NOT NULL, "
      "\"_vn\" INTEGER NOT NULL";
    for (unsigned i = 0; i < m_xFields.length(); ++i) {
      const auto &field = m_xFields[i];
      ddl << ", " << quoteID(field.id) << ' '
        << storageName(field.field.storage)
        << " NOT NULL";
    }
    ddl << ") STRICT";
    exec(cxn, ddl);
    validateStrict(cxn, physical);

    unsigned nIndices = m_xKeyFields.length() + 1;
    ExpectedIndices expectedIndices{nIndices};
    for (unsigned keyID = 0; keyID < m_xKeyFields.length(); ++keyID) {
      ExpectedIndex *expected = expectedIndices.push();
      new (expected) ExpectedIndex{
        .fields = &m_xKeyFields[keyID], .unique = !keyID};
      expected->id << "zdbsl_k" << ZuBoxed(keyID) << '_'
        << (m_internal ? 'i' : 'a') << '_' << m_id;
      SQLString sql;
      sql << "CREATE ";
      if (!keyID) sql << "UNIQUE ";
      sql << "INDEX IF NOT EXISTS " << quoteID(expected->id) << " ON "
        << m_relation << " (";
      const auto &fields = m_xKeyFields[keyID];
      for (unsigned i = 0; i < fields.length(); ++i) {
        if (i) sql << ", ";
        sql << quoteID(fields[i].field->id);
        if (fields[i].descend) sql << " DESC";
      }
      sql << ')';
      exec(cxn, sql);
    }
    {
      ExpectedIndex *expected = expectedIndices.push();
      new (expected) ExpectedIndex{.unique = true};
      expected->id << "zdbsl_un_" << (m_internal ? 'i' : 'a') << '_' << m_id;
      SQLString sql;
      sql << "CREATE UNIQUE INDEX IF NOT EXISTS " << quoteID(expected->id)
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
            sqlite3_bind_text(insert, 6, storageName(field.field.storage), -1,
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
          storage_ != storageName(field.field.storage) ||
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
          expectedType = storageName(m_xFields[columns - 4].field.storage);
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

    ZmRef<IndexMapObj> indexMap = new IndexMapObj{nIndices};
    for (unsigned i = 0; i < nIndices; ++i)
      indexMap->map.add(expectedIndices[i].id, i);
    {
      SQLString pragma;
      pragma << "PRAGMA index_list(" << quoteID(physical) << ')';
      Stmt list;
      list.ptr = prepare(cxn, pragma);
      int rc;
      while ((rc = sqlite3_step(list)) == SQLITE_ROW) {
        ZuCSpan id{
          reinterpret_cast<const char *>(sqlite3_column_text(list, 1)),
          unsigned(sqlite3_column_bytes(list, 1))};
        unsigned i = indexMap->map.findVal(id);
        if (ZuNull(i))
          throw ZeEXCEPT(Fatal, "ZdbSL", "unexpected index definition");
        ExpectedIndex &expected = expectedIndices[i];
        if (expected.seen ||
            bool(sqlite3_column_int(list, 2)) != expected.unique ||
            sqlite3_column_int(list, 4))
          throw ZeEXCEPT(Fatal, "ZdbSL", "inconsistent index definition");
        expected.seen = true;
      }
      if (rc != SQLITE_DONE) throw cxnError(cxn, "PRAGMA index_list", rc);
    }
    indexMap = nullptr;
    for (unsigned i = 0; i < nIndices; ++i) {
      const ExpectedIndex &expectedIndex = expectedIndices[i];
      if (!expectedIndex.seen)
        throw ZeEXCEPT(Fatal, "ZdbSL", "missing index after creation");

      SQLString pragma;
      pragma << "PRAGMA index_xinfo(" << quoteID(expectedIndex.id) << ')';
      Stmt info;
      info.ptr = prepare(cxn, pragma);
      unsigned ordinal = 0;
      int rc;
      while ((rc = sqlite3_step(info)) == SQLITE_ROW) {
        if (!sqlite3_column_int(info, 5)) continue;
        ZuCSpan id{
          reinterpret_cast<const char *>(sqlite3_column_text(info, 2)),
          unsigned(sqlite3_column_bytes(info, 2))};
        ZuCSpan expected;
        bool descend = false;
        if (expectedIndex.fields) {
          if (ordinal >= expectedIndex.fields->length())
            throw ZeEXCEPT(Fatal, "ZdbSL", "index has extra columns");
          expected = (*expectedIndex.fields)[ordinal].field->id;
          descend = (*expectedIndex.fields)[ordinal].descend;
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
      unsigned expected = expectedIndex.fields ?
        expectedIndex.fields->length() : 2;
      if (ordinal != expected)
        throw ZeEXCEPT(Fatal, "ZdbSL", "index has missing columns");
    }
    exec(cxn, "COMMIT");
  } catch (...) {
    sqlite3_exec(cxn, "ROLLBACK", nullptr, nullptr, nullptr);
    throw;
  }
}

template <typename Fields>
static void columns(SQLString &sql, const Fields &fields)
{
  for (unsigned i = 0; i < fields.length(); ++i) {
    if (i) sql << ", ";
    sql << quoteID(fieldAt(fields, i).id);
  }
}

template <typename Fields>
static void equal(SQLString &sql, const Fields &fields, unsigned n)
{
  for (unsigned i = 0; i < n; ++i) {
    sql << (i ? " AND " : " WHERE ") << quoteID(fieldAt(fields, i).id)
      << "=?" << ZuBoxed(i + 1);
  }
}

template <typename Projection>
static SQLString selectSQL(
  const SQLString &relation, const Projection &projection,
  const XFieldRefs &keyFields, unsigned group, unsigned direction,
  bool next, bool inclusive)
{
  SQLString sql;
  sql << "SELECT ";
  columns(sql, projection);
  sql << " FROM " << relation;
  equal(sql, keyFields, group);
  unsigned n = keyFields.length();
  if (next && group < n) {
    sql << (group ? " AND (" : " WHERE (");
    if (direction != KeyDir::Mixed) {
      sql << '(';
      for (unsigned i = group; i < n; ++i) {
        if (i > group) sql << ", ";
        sql << quoteID(keyFields[i].field->id);
      }
      sql << ") " << (direction == KeyDir::Descending ? '<' : '>');
      if (inclusive) sql << '=';
      sql << " (";
      for (unsigned i = group; i < n; ++i) {
        if (i > group) sql << ", ";
        sql << '?' << ZuBoxed(i + 1);
      }
      sql << ')';
    } else {
      for (unsigned i = group; i < n; ++i) {
        bool descend = keyFields[i].descend;
        sql << quoteID(keyFields[i].field->id) << (descend ? '<' : '>');
        if (inclusive && i + 1 == n) sql << '=';
        sql << '?' << ZuBoxed(i + 1);
        if (i + 1 < n)
          sql << " OR (" << quoteID(keyFields[i].field->id) << "=?"
            << ZuBoxed(i + 1) << " AND (";
      }
      for (unsigned i = group + 1; i < n; ++i) sql << "))";
    }
    sql << ')';
  }
  if (group < n) {
    sql << " ORDER BY ";
    for (unsigned i = group; i < n; ++i) {
      if (i > group) sql << ", ";
      sql << quoteID(keyFields[i].field->id);
      if (keyFields[i].descend) sql << " DESC";
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
    unsigned group = m_keyGroup[keyID];
    SQLString sql{"SELECT count(*) FROM "};
    sql << m_relation;
    equal(sql, fields, group);
    stmts.count = prepare(cxn, sql);
    stmts.selectKIX = prepare(cxn,
      selectSQL(m_relation, fields, fields,
        group, m_keyDirs[keyID], false, false));
    stmts.selectKNX = prepare(cxn,
      selectSQL(m_relation, fields, fields,
        group, m_keyDirs[keyID], true, false));
    stmts.selectKNI = prepare(cxn,
      selectSQL(m_relation, fields, fields,
        group, m_keyDirs[keyID], true, true));
    stmts.selectRIX = prepare(cxn,
      selectSQL(m_relation, m_xFields, fields,
        group, m_keyDirs[keyID], false, false));
    stmts.selectRNX = prepare(cxn,
      selectSQL(m_relation, m_xFields, fields,
        group, m_keyDirs[keyID], true, false));
    stmts.selectRNI = prepare(cxn,
      selectSQL(m_relation, m_xFields, fields,
        group, m_keyDirs[keyID], true, true));
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
      sql << quoteID(key[i].field->id);
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
      sql << ", " << quoteID(field.field->id) << "=?" << ZuBoxed(param++);
    sql << " WHERE ";
    const auto &key = m_xKeyFields[0];
    for (unsigned i = 0; i < key.length(); ++i) {
      if (i) sql << " AND ";
      sql << quoteID(key[i].field->id) << "=?" << ZuBoxed(param++);
    }
    m_stmts.update = prepare(cxn, sql);
  }
  {
    SQLString sql;
    sql << "DELETE FROM " << m_relation << " WHERE ";
    const auto &key = m_xKeyFields[0];
    for (unsigned i = 0; i < key.length(); ++i) {
      if (i) sql << " AND ";
      sql << quoteID(key[i].field->id) << "=?" << ZuBoxed(i + 1);
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
  liveSQL << "SELECT \"_un\", \"_sn\" FROM " << m_relation
    << " WHERE \"_shard\"=?1 ORDER BY \"_un\" DESC LIMIT 1";
  Stmt live;
  live.ptr = prepare(cxn, liveSQL);
  Stmt mrd;
  mrd.ptr = prepare(cxn,
    "SELECT \"shard\", \"un\", \"sn\" FROM \"zdbsl_mrd\" "
    "WHERE \"internal\"=?1 AND \"tbl\"=?2 ORDER BY \"shard\"");
  if (sqlite3_bind_int(mrd, 1, m_internal) != SQLITE_OK ||
      sqlite3_bind_text64(mrd, 2, m_id.data(), m_id.length(),
        SQLITE_STATIC, SQLITE_UTF8) != SQLITE_OK)
    throw cxnError(cxn, "bind delete high-water identity");
  int mrdRC;
  while ((mrdRC = sqlite3_step(mrd)) == SQLITE_ROW) {
    sqlite3_int64 shard_ = sqlite3_column_int64(mrd, 0);
    if (shard_ < 0 || uint64_t(shard_) >= m_maxUN.length())
      throw ZeEXCEPT(Fatal, "ZdbSL", "invalid delete high-water shard");
    unsigned shard = shard_;
    uint64_t un;
    uint128_t sn;
    if (!loadU64(columnBlob(mrd, 1), un) ||
        !loadU128(columnBlob(mrd, 2), sn))
      throw ZeEXCEPT(Fatal, "ZdbSL", "invalid delete high-water value");
    if (un != ZdbNullUN() &&
        (m_maxUN[shard] == ZdbNullUN() || un > m_maxUN[shard]))
      m_maxUN[shard] = un;
    if (sn != ZdbNullSN() && (m_maxSN == ZdbNullSN() || sn > m_maxSN))
      m_maxSN = sn;
  }
  if (mrdRC != SQLITE_DONE)
    throw cxnError(cxn, "read delete high-water", mrdRC);
  for (unsigned shard = 0; shard < m_maxUN.length(); ++shard) {
    Reset reset{live};
    if (sqlite3_bind_int64(live, 1, shard) != SQLITE_OK)
      throw cxnError(cxn, "bind live high-water shard");
    int rc = sqlite3_step(live);
    if (rc == SQLITE_ROW) {
      uint64_t un;
      uint128_t sn;
      if (!loadU64(columnBlob(live, 0), un) ||
          !loadU128(columnBlob(live, 1), sn))
        throw ZeEXCEPT(Fatal, "ZdbSL", "invalid live high-water value");
      if (m_maxUN[shard] == ZdbNullUN() || un > m_maxUN[shard])
        m_maxUN[shard] = un;
      if (m_maxSN == ZdbNullSN() || sn > m_maxSN) m_maxSN = sn;
    } else if (rc != SQLITE_DONE)
      throw cxnError(cxn, "read live high-water", rc);
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

using Offset = Zfb::Offset<void>;

struct SavedOffset {
  Offset	offset;
  uint8_t	ipType = 0;
  bool		valid = false;
};
ZuDerive(SavedOffsetsScratch,
  (ZtArray<SavedOffset, ZtArrayHeapID<"ZdbSL.SavedOffset",
    ZtArraySharded<true>>>));

static ZuCSpan columnText(sqlite3_stmt *stmt, unsigned col)
{
  return {
    reinterpret_cast<const char *>(sqlite3_column_text(stmt, col)),
    unsigned(sqlite3_column_bytes(stmt, col))};
}

static bool vecData(ZuBSpan &data, unsigned width, unsigned &n)
{
  uint32_t count;
  if (!loadUnsigned(data, count) ||
      uint64_t(count) * width != data.length()) return false;
  n = count;
  return true;
}

static bool varVecData(ZuBSpan &data, unsigned &n)
{
  uint32_t count;
  if (!loadUnsigned(data, count)) return false;
  ZuBSpan scan = data;
  for (unsigned i = 0; i < count; ++i) {
    uint32_t length;
    if (!loadUnsigned(scan, length) || scan.length() < length) return false;
    scan.offset(length);
  }
  if (scan.length()) return false;
  n = count;
  return true;
}

template <typename T, typename L>
static SavedOffset savePVec(Zfb::Builder &fbb, ZuBSpan data, L &&load)
{
  unsigned n;
  if (!vecData(data, sizeof(T), n)) return {};
  return {Zfb::Save::pvectorIter<T>(
    fbb, n, [&data, load = ZuFwd<L>(load)](unsigned) mutable {
      T value = load(data.data());
      data.offset(sizeof(T));
      return value;
    }).Union(), 0, true};
}

template <typename FB, typename T, typename L, typename S>
static SavedOffset saveStructVec(
  Zfb::Builder &fbb, ZuBSpan data, unsigned width, L &&load, S &&save)
{
  unsigned n;
  if (!vecData(data, width, n)) return {};
  return {Zfb::Save::structVecIter<FB>(
    fbb, n, [&data, width,
      load = ZuFwd<L>(load), save = ZuFwd<S>(save)](FB *out, unsigned) mutable {
      T value = load(data.data());
      data.offset(width);
      *out = save(value);
    }).Union(), 0, true};
}

static SavedOffset saveOffset(
  Zfb::Builder &fbb, sqlite3_stmt *stmt, unsigned col, unsigned type)
{
  if (type == Type::String) {
    if (sqlite3_column_type(stmt, col) != SQLITE_TEXT) return {};
    return {Zfb::Save::str(fbb, columnText(stmt, col)).Union(), 0, true};
  }
  if (sqlite3_column_type(stmt, col) != SQLITE_BLOB) return {};
  ZuBSpan data = columnBlob(stmt, col);
  switch (type) {
    case Type::Bytes:
      return {Zfb::Save::bytes(fbb, data).Union(), 0, true};
    case Type::Bitmap: {
      unsigned n;
      if (!vecData(data, sizeof(uint64_t), n)) return {};
      auto offset = Zfb::CreateBitmap(
        fbb, Zfb::Save::pvectorIter<uint64_t>(
          fbb, n, [&data](unsigned) {
            uint64_t value = loadUnsigned_<uint64_t>(data.data());
            data.offset(sizeof(value));
            return value;
          }));
      return {offset.Union(), 0, true};
    }
    case Type::IP: {
      if (!data.length()) return {};
      uint8_t family = data[0];
      data.offset(1);
      ZiIP ip;
      switch (family) {
        case 0:
          if (data.length()) return {};
          break;
        case 4: {
          if (data.length() != sizeof(in_addr)) return {};
          in_addr addr;
          memcpy(&addr, data.data(), sizeof(addr));
          ip = ZiIP{addr};
        } break;
        case 6: {
          if (data.length() != sizeof(in6_addr)) return {};
          in6_addr addr;
          memcpy(&addr, data.data(), sizeof(addr));
          ip = ZiIP{addr};
        } break;
        default:
          return {};
      }
      auto saved = ZfbTransform::IP::save(fbb, ip);
      return {saved.offset, uint8_t(saved.type), true};
    }
    case Type::StringVec: {
      unsigned n;
      if (!varVecData(data, n)) return {};
      auto offset = Zfb::Save::strVecIter(fbb, n, [&data](unsigned) {
        uint32_t length = loadUnsigned_<uint32_t>(data.data());
        data.offset(sizeof(length));
        ZuCSpan value{
          reinterpret_cast<const char *>(data.data()), length};
        data.offset(length);
        return value;
      });
      return {offset.Union(), 0, true};
    }
    case Type::BytesVec: {
      unsigned n;
      if (!varVecData(data, n)) return {};
      auto offset = Zfb::Save::vectorIter<Zfb::Bytes>(
        fbb, n, [&data](Zfb::Builder &fbb, unsigned) {
          uint32_t length = loadUnsigned_<uint32_t>(data.data());
          data.offset(sizeof(length));
          ZuBSpan value{data.data(), length};
          data.offset(length);
          return Zfb::CreateBytes(fbb, Zfb::Save::bytes(fbb, value));
        });
      return {offset.Union(), 0, true};
    }
#define ZdbSL_SaveUVec(Type_, Elem_) \
    case Type::Type_: \
      return savePVec<Elem_>(fbb, data, \
	[](const uint8_t *in) { return loadUnsigned_<Elem_>(in); })
#define ZdbSL_SaveSVec(Type_, Elem_, UInt_) \
    case Type::Type_: \
      return savePVec<Elem_>(fbb, data, \
	[](const uint8_t *in) { return loadSigned_<Elem_, UInt_>(in); })
    ZdbSL_SaveSVec(Int8Vec, int8_t, uint8_t);
    ZdbSL_SaveUVec(UInt8Vec, uint8_t);
    ZdbSL_SaveSVec(Int16Vec, int16_t, uint16_t);
    ZdbSL_SaveUVec(UInt16Vec, uint16_t);
    ZdbSL_SaveSVec(Int32Vec, int32_t, uint32_t);
    ZdbSL_SaveUVec(UInt32Vec, uint32_t);
    ZdbSL_SaveSVec(Int64Vec, int64_t, uint64_t);
    ZdbSL_SaveUVec(UInt64Vec, uint64_t);
#undef ZdbSL_SaveSVec
#undef ZdbSL_SaveUVec
    case Type::Int128Vec:
      return saveStructVec<Zfb::Int128, int128_t>(
	fbb, data, sizeof(int128_t), loadS128_,
	[](int128_t value) { return ZfbTransform::Int128::save(value); });
    case Type::UInt128Vec:
      return saveStructVec<Zfb::UInt128, uint128_t>(
	fbb, data, sizeof(uint128_t), loadU128_,
	[](uint128_t value) { return ZfbTransform::UInt128::save(value); });
    case Type::FloatVec:
      return savePVec<double>(fbb, data, loadFloat_);
    case Type::FixedVec:
      return saveStructVec<Zfb::Fixed, ZuFixed>(
	fbb, data, sizeof(int128_t), loadFixed_,
	[](ZuFixed value) { return ZfbTransform::Fixed::save(value); });
    case Type::DecimalVec:
      return saveStructVec<Zfb::Decimal, ZuDecimal>(
	fbb, data, sizeof(int128_t), loadDecimal_,
	[](ZuDecimal value) { return ZfbTransform::Decimal::save(value); });
    case Type::TimeVec:
      return saveStructVec<Zfb::Time, ZuTime>(
	fbb, data, sizeof(TimeData), loadTime_,
	[](ZuTime value) { return ZfbTransform::Time::save(value); });
    case Type::DateTimeVec:
      return saveStructVec<Zfb::DateTime, ZuDateTime>(
	fbb, data, sizeof(DateTimeData), loadDateTime_,
	[](ZuDateTime value) { return ZfbTransform::DateTime::save(value); });
    default:
      return {};
  }
}

static bool hasOffset(unsigned type)
{
  return type == Type::String || type == Type::Bytes ||
    type == Type::Bitmap || type == Type::IP || isVec(type);
}

static bool saveValue(
  Zfb::Builder &fbb, sqlite3_stmt *stmt, unsigned col,
  const XField &xfield, const SavedOffset &saved)
{
  const reflection::Field *field = xfield.field.field;
  unsigned type = xfield.field.type;
  if (hasOffset(type)) {
    if (!saved.valid) return false;
    if (!saved.offset.o) return true;
    if (type == Type::IP)
      fbb.AddElement<uint8_t>(field->offset() - 2, saved.ipType, 0);
    fbb.AddOffset(field->offset(), saved.offset);
    return true;
  }
  if (xfield.field.storage == Storage::Integer) {
    if (sqlite3_column_type(stmt, col) != SQLITE_INTEGER) return false;
    sqlite3_int64 value = sqlite3_column_int64(stmt, col);
    switch (type) {
      case Type::Bool:
        fbb.AddElement<bool>(
          field->offset(), bool(value), field->default_integer());
        return true;
#define ZdbSL_SaveInt(Type_, CType_) \
      case Type::Type_: \
        fbb.AddElement<CType_>( \
          field->offset(), CType_(value), field->default_integer()); \
        return true
      ZdbSL_SaveInt(Int8, int8_t);
      ZdbSL_SaveInt(UInt8, uint8_t);
      ZdbSL_SaveInt(Int16, int16_t);
      ZdbSL_SaveInt(UInt16, uint16_t);
      ZdbSL_SaveInt(Int32, int32_t);
      ZdbSL_SaveInt(UInt32, uint32_t);
      ZdbSL_SaveInt(Int64, int64_t);
#undef ZdbSL_SaveInt
    }
    return false;
  }
  if (sqlite3_column_type(stmt, col) != SQLITE_BLOB) return false;
  ZuBSpan data = columnBlob(stmt, col);
  switch (type) {
    case Type::UInt64: {
      uint64_t value;
      if (!loadU64(data, value)) return false;
      fbb.AddElement<uint64_t>(
        field->offset(), value, field->default_integer());
    } return true;
    case Type::Float: {
      double value;
      if (!loadFloat(data, value)) return false;
      fbb.AddElement<double>(
        field->offset(), value, field->default_real());
    } return true;
    case Type::Int128: {
      int128_t value;
      if (!loadS128(data, value)) return false;
      auto fb = ZfbTransform::Int128::save(value);
      fbb.AddStruct(field->offset(), &fb);
    } return true;
    case Type::UInt128: {
      uint128_t value;
      if (!loadU128(data, value)) return false;
      auto fb = ZfbTransform::UInt128::save(value);
      fbb.AddStruct(field->offset(), &fb);
    } return true;
    case Type::Fixed: {
      ZuFixed value;
      if (!loadFixed(data, value)) return false;
      auto fb = ZfbTransform::Fixed::save(value);
      fbb.AddStruct(field->offset(), &fb);
    } return true;
    case Type::Decimal: {
      ZuDecimal value;
      if (!loadDecimal(data, value)) return false;
      auto fb = ZfbTransform::Decimal::save(value);
      fbb.AddStruct(field->offset(), &fb);
    } return true;
    case Type::Time: {
      ZuTime value;
      if (!loadTime(data, value)) return false;
      auto fb = ZfbTransform::Time::save(value);
      fbb.AddStruct(field->offset(), &fb);
    } return true;
    case Type::DateTime: {
      ZuDateTime value;
      if (!loadDateTime(data, value)) return false;
      auto fb = ZfbTransform::DateTime::save(value);
      fbb.AddStruct(field->offset(), &fb);
    } return true;
    default:
      return false;
  }
}

template <typename Fields>
static Offset saveTuple(
    Zfb::Builder &fbb, sqlite3_stmt *stmt, const Fields &fields,
    unsigned base = 0)
{
  unsigned n = fields.length();
  auto offsets = ZtScratch(SavedOffsetsScratch, n, n);
  for (unsigned i = 0; i < n; ++i)
    offsets[i] = hasOffset(fieldAt(fields, i).field.type) ?
      saveOffset(fbb, stmt, base + i, fieldAt(fields, i).field.type) :
      SavedOffset{};
  auto start = fbb.StartTable();
  for (unsigned i = 0; i < n; ++i)
    if (!saveValue(fbb, stmt, base + i, fieldAt(fields, i), offsets[i]))
      throw ZeEXCEPT(Fatal, "ZdbSL", "invalid stored tuple value");
  return Offset{fbb.EndTable(start)};
}
template <typename Fields>
ZmRef<IOBuf> StoreTbl::tuple_(sqlite3_stmt *stmt, const Fields &fields)
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
    auto parts = ZtScratch(VarPartsScratch, n, n);
    auto size = planInput(m_store->conn(), m_store->lengthLimit(),
      n, m_xKeyFields[keyID], fbo, parts.span());
    auto scratch = ZtScratch(VarBufScratch, size, size);
    bindInput(m_store->conn(), stmt, 1,
      n, m_xKeyFields[keyID], fbo, parts.cspan(), scratch.span());
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
    auto parts = ZtScratch(VarPartsScratch, n, n);
    auto size = planInput(m_store->conn(), m_store->lengthLimit(),
      n, m_xKeyFields[keyID], fbo, parts.span());
    auto scratch = ZtScratch(VarBufScratch, size, size);
    bindInput(m_store->conn(), stmt, 1,
      n, m_xKeyFields[keyID], fbo, parts.cspan(), scratch.span());
    int rc = sqlite3_bind_int64(stmt, n + 1, limit);
    if (rc != SQLITE_OK) throw cxnError(m_store->conn(), "bind select limit", rc);
    unsigned count = 0;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
      auto result = selectRow ? tuple_(stmt, m_xFields) :
        tuple_(stmt, m_xKeyFields[keyID]);
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
    auto parts = ZtScratch(VarPartsScratch, n, n);
    auto size = planInput(m_store->conn(), m_store->lengthLimit(),
      n, m_xKeyFields[keyID], fbo, parts.span());
    auto scratch = ZtScratch(VarBufScratch, size, size);
    bindInput(m_store->conn(), stmt, 1,
      n, m_xKeyFields[keyID], fbo, parts.cspan(), scratch.span());
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
    UNData data{un};
    if (sqlite3_bind_int64(stmt, 1, shard) != SQLITE_OK ||
        sqlite3_bind_blob64(stmt, 2, &data, sizeof(data),
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
  UN un, SN sn, VN vn, UNData &unData, SNData &snData)
{
  unData = un;
  snData = sn;
  if (sqlite3_bind_blob64(stmt, first, &unData, sizeof(unData),
        SQLITE_STATIC) != SQLITE_OK ||
      sqlite3_bind_blob64(stmt, first + 1, &snData, sizeof(snData),
        SQLITE_STATIC) != SQLITE_OK ||
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
    Txn txn{cxn, m_store->beginStmt(), m_store->commitStmt()};
    auto fbo = Zfb::GetAnyRoot(record->data()->data());
    UNData unData;
    SNData snData;
    bool mrd = false;
    if (!vn) {
      sqlite3_stmt *stmt = m_stmts.insert;
      Reset reset{stmt};
      if (sqlite3_bind_int64(stmt, 1, shard) != SQLITE_OK)
        throw cxnError(cxn, "bind insert shard");
      bindMeta(cxn, stmt, 2, un, sn, vn, unData, snData);
      unsigned n = m_xFields.length();
      auto parts = ZtScratch(VarPartsScratch, n, n);
      auto size = planInput(
        cxn, m_store->lengthLimit(), n, m_xFields, fbo, parts.span());
      auto scratch = ZtScratch(VarBufScratch, size, size);
      bindInput(cxn, stmt, 5, n, m_xFields, fbo,
        parts.cspan(), scratch.span());
      done(cxn, stmt);
      inserted = sqlite3_changes(cxn) == 1;
      mrd = !inserted;
    } else if (vn > 0) {
      sqlite3_stmt *stmt = m_stmts.update;
      Reset reset{stmt};
      bindMeta(cxn, stmt, 1, un, sn, vn, unData, snData);
      unsigned nUpdate = m_xUpdFields.length();
      unsigned nKey = m_xKeyFields[0].length();
      auto updateParts = ZtScratch(VarPartsScratch, nUpdate, nUpdate);
      auto keyParts = ZtScratch(VarPartsScratch, nKey, nKey);
      auto updateSize = planInput(cxn, m_store->lengthLimit(),
        nUpdate, m_xUpdFields, fbo, updateParts.span());
      auto keySize = planInput(cxn, m_store->lengthLimit(),
        nKey, m_xKeyFields[0], fbo, keyParts.span());
      if (keySize > UINT_MAX - updateSize)
        throw cxnError(cxn, "encode SQLite values", SQLITE_TOOBIG);
      unsigned size = updateSize + keySize;
      auto scratch = ZtScratch(VarBufScratch, size, size);
      bindInput(cxn, stmt, 4, nUpdate, m_xUpdFields, fbo,
        updateParts.cspan(), {scratch.data(), updateSize});
      bindInput(cxn, stmt, 4 + nUpdate, nKey, m_xKeyFields[0], fbo,
        keyParts.cspan(), {scratch.data() + updateSize, keySize});
      done(cxn, stmt);
      if (sqlite3_changes(cxn) != 1)
        throw ZeEXCEPT(Error, "ZdbSL", "update failed - primary key missing");
    } else {
      sqlite3_stmt *stmt = m_stmts.del;
      Reset reset{stmt};
      unsigned n = m_xKeyFields[0].length();
      auto parts = ZtScratch(VarPartsScratch, n, n);
      auto size = planInput(cxn, m_store->lengthLimit(),
        n, m_xKeyFields[0], fbo, parts.span());
      auto scratch = ZtScratch(VarBufScratch, size, size);
      bindInput(cxn, stmt, 1,
        n, m_xKeyFields[0], fbo, parts.cspan(), scratch.span());
      done(cxn, stmt);
      if (sqlite3_changes(cxn) != 1)
        throw ZeEXCEPT(Error, "ZdbSL", "delete failed - primary key missing");
      removed = true;
      mrd = true;
    }
    if (mrd) {
      sqlite3_stmt *stmt = m_stmts.mrd;
      Reset reset{stmt};
      unData = un;
      snData = sn;
      if (sqlite3_bind_int(stmt, 1, m_internal) != SQLITE_OK ||
          sqlite3_bind_text64(stmt, 2, m_id.data(), m_id.length(),
            SQLITE_STATIC, SQLITE_UTF8) != SQLITE_OK ||
          sqlite3_bind_int64(stmt, 3, shard) != SQLITE_OK ||
          sqlite3_bind_blob64(stmt, 4, &unData, sizeof(unData),
            SQLITE_STATIC) != SQLITE_OK ||
          sqlite3_bind_blob64(stmt, 5, &snData, sizeof(snData),
            SQLITE_STATIC) != SQLITE_OK)
        throw cxnError(cxn, "bind delete high-water state");
      done(cxn, stmt);
    }
    txn.commit();
    m_maxUN[shard] = un;
    if (m_maxSN == ZdbNullSN() || sn > m_maxSN) m_maxSN = sn;
    if (inserted) ++m_count;
    if (removed) --m_count;
    fn(ZuMv(buf), CommitResult{});
  } catch (const ZeException &e) {
    fn(ZuMv(buf), CommitResult{e});
  }
}

} // ZdbSL

Zdb_::Store *ZdbStore() { return new ZdbSL::Store{}; }
