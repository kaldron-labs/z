//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef ZdbPQ_HH
#define ZdbPQ_HH

#ifndef ZdbPQLib_HH
#include <zlib/ZdbPQLib.hh>
#endif

#include <libpq-fe.h>

#include <zlib/ZuDerive.hh>
#include <zlib/ZuArray.hh>
#include <zlib/ZuPtr.hh>
#include <zlib/ZuSpan.hh>
#include <zlib/ZuInt.hh>
#include <zlib/ZuDecimal.hh>
#include <zlib/ZuFixed.hh>
#include <zlib/ZuTime.hh>
#include <zlib/ZuDateTime.hh>
#include <zlib/ZuIntrin.hh>

#include <zlib/ZmLHash.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmScratch.hh>

#include <zlib/ZiAssert.hh>
#include <zlib/ZiEventLoop.hh>

#include <zlib/ZiIP.hh>

#include <zlib/ZfbStruct.hh>

#include <zlib/ZdbStore.hh>

namespace ZdbPQ {

class Store;
class StoreTbl;

using namespace Zdb_;

// SQL strings are only used during initialization
// - all queries are prepared
// - all run-time I/O is binary
ZuDerive(SQLString, ZtString<ZtStringHeapID<"ZdbPQ.SQLString">>);

// Value     C++        flatbuffers     PG SQL        PG send/recv
// -----     ---        -----------     ------        ------------
// String    ZuCSpan    string          text          raw data
// Bytes     ZuBSpan    [ubyte]         bytea         raw data
// Bool      bool       bool            bool          uint8_t
// Int8      int8_t     int8            int1     (*)  int8_t
// UInt8     uint8_t    uint8           uint1    (*)  uint8_t
// Int16     int16_t    int16           int2     (*)  int16_t BE
// UInt16    uint16_t   uint16          uint2    (*)  uint16_t BE
// Int32     int32_t    int32           int4          int32_t BE
// UInt32    uint32_t   uint32          uint4    (*)  uint32_t BE
// Int64     int64_t    int64           int8          int64_t BE
// UInt64    uint64_t   uint64          uint8    (*)  uint64_t BE
// Float     double     double          float8        double BE
// Fixed     ZuFixed    Zfb.Fixed       zdecimal (**) int128_t BE
// Decimal   ZuDecimal  Zfb.Decimal     zdecimal (**) int128_t BE
// Time      ZuTime     Zfb.Time        ztime    (**) int64_t BE, int32_t BE
// DateTime  ZuDateTime Zfb.DateTime    ztime    (**) int64_t BE, int32_t BE
// Int128    int128_t   Zfb.Int128      int16    (*)  int128_t BE
// UInt128   uint128_t  Zfb.UInt128     uint16   (*)  uint128_t BE
// Bitmap    ZtBitmap   Zfb.Bitmap      zbitmap  (**) uint64_t BE, uint64_t[] BE
// IP        ZiIP       Zfb.IP          inet          IPHdr, address bytes
//
// <Type>Vec            [<Type>]        type[]        array (see below)

// Postgres array binary format:
// int32_t BE	number of dimensions
// int32_t BE	flags (0 for no nulls, 1 for possible nulls)
// int32_t BE	oid (OID of elements)
// [{int32_t BE length, int32_t BE lower bound}...] dimensions (lb is 1-based)
// [{int32_t BE length, data...}...] elements (-1 length for null)
// Note that elements are appended without padding/alignment

// (*)   Postgres uint extension https://github.com/djnz00/pguint
// (**)  libz Postgres extension

// --- extended field information

struct XField {
  IDString			id_;		// snake case field ID
  const ZfVField		*vfield;	// Z field
  const reflection::Field	*field;		// flatbuffers reflection field
  unsigned			type;		// Value union discriminator
};
ZuDerive(XFields, (ZtArray<XField, ZtArrayHeapID<"ZdbPQ.XField">>));

using XFieldIx = uint16_t;
ZuAssert(Zdb_::maxFields() <= ZuCmp<XFieldIx>::maximum());

ZuDerive(XFieldIxs,
  (ZtArray<XFieldIx, ZtArrayHeapID<"ZdbPQ.XFieldIx">>));

struct XKeyField {
  XFieldIx	field;
  uint8_t	flags;

  enum { Descend = 1, IndexDesc = 2 };

  bool descend() const { return flags & Descend; }
  bool indexDesc() const { return flags & IndexDesc; }
};
ZuDerive(XKeyFields,
  (ZtArray<XKeyField, ZtArrayHeapID<"ZdbPQ.XKeyField">>));

struct XKey {
  XKeyFields	fields;
  uint16_t	group = 0;
  uint16_t	descending = 0;
  uint16_t	offsets = 0;
  int8_t	direction = 0;

  enum { Ascending = 0, Descending, Mixed };
};
ZuDerive(XKeys, (ZtArray<XKey, ZtArrayHeapID<"ZdbPQ.XKey">>));

struct IndexRow {
  ZuCSpan	indexID;
  ZuCSpan	fieldID;
  unsigned	oid;
  unsigned	field;
  unsigned	nKey;
  unsigned	nTotal;
  bool		unique;
  bool		descending;
};

struct IndexMatch {
  uint16_t next = ZuCmp<uint16_t>::null();
};

inline bool matchIndex(
  IndexMatch &match, ZuCSpan expectedID,
  const XFields &fields, const XKey &key, KeyID keyID,
  const IndexRow &row, bool oidMatch)
{
  unsigned n = key.fields.length();
  if (row.indexID != expectedID || row.unique != !keyID ||
      row.nKey != n || row.nTotal != n ||
      !row.field || row.field > n ||
      (!ZuNull(match.next) && row.field != match.next + 1) ||
      (ZuNull(match.next) && row.field != 1))
    return false;
  const auto &keyField = key.fields[row.field - 1];
  const auto &field = fields[keyField.field];
  if (row.fieldID != field.id_ || !oidMatch ||
      row.descending != keyField.indexDesc())
    return false;
  match.next = row.field;
  return true;
}

inline const XField &xFieldAt(
    const XFields &, const XFields &fields, unsigned i)
{
  return fields[i];
}
inline const XField &xFieldAt(
    const XFields &fields, const XFieldIxs &projection, unsigned i)
{
  return fields[projection[i]];
}
inline const XField &xFieldAt(
    const XFields &fields, const XKeyFields &projection, unsigned i)
{
  return fields[projection[i].field];
}

template <typename Param>
inline void continuation(
  SQLString &sql, const XFields &fields, const XKey &key,
  unsigned begin, bool inclusive, Param param)
{
  unsigned n = key.fields.length();
  if (begin >= n) return;
  bool descending = key.direction == XKey::Descending;
  if (key.direction != XKey::Mixed) {
    sql << '(';
    for (unsigned i = begin; i < n; i++) {
      if (i > begin) sql << ',';
      sql << '"' << fields[key.fields[i].field].id_ << '"';
    }
    sql << ')' << (descending ? '<' : '>');
    if (inclusive) sql << '=';
    sql << '(';
    for (unsigned i = begin; i < n; i++) {
      if (i > begin) sql << ',';
      param(sql, i);
    }
    sql << ')';
    return;
  }

  const auto &first = fields[key.fields[begin].field];
  sql << '"' << first.id_ << '"'
    << (key.fields[begin].descend() ? '<' : '>') << '=';
  param(sql, begin);
  sql << " AND (";
  unsigned open = 1;
  for (unsigned i = begin; i < n; i++) {
    const auto &xField = fields[key.fields[i].field];
    sql << '"' << xField.id_ << '"'
      << (key.fields[i].descend() ? '<' : '>');
    if (inclusive && i + 1 == n) sql << '=';
    param(sql, i);
    if (i + 1 < n) {
      sql << " OR (\"" << xField.id_ << "\"=";
      param(sql, i);
      sql << " AND (";
      open += 2;
    }
  }
  while (open--) sql << ')';
}

// --- value union (postgres binary send/receive formats)

#pragma pack(push, 1)
// 1-dimensional array - postgres binary send/receive format
// - Note: Postgres sends empty arrays with ndim=0, but can also receive
//   empty arrays with ndim=1, length=0; this implementation
//   receives either representation but consistently sends ndim=1, length=0
struct VecHdr {
  ZuBigEndian<int32_t>	ndim;	// = 1 (can be 0 if received vector is empty)
  ZuBigEndian<int32_t>	flags;	// = 0
  ZuBigEndian<int32_t>	oid;	// OID of elements
  ZuBigEndian<int32_t>	length;	// = N
  ZuBigEndian<int32_t>	lbound;	// = 1 (SQL, sigh)
};
struct VecElem {
  ZuBigEndian<int32_t>	length;	// size of element
};
#pragma pack(pop)
// return total size of an array buffer (fixed-size elements)
inline unsigned vecSize(unsigned n, unsigned size) {
  return sizeof(VecHdr) + n * (sizeof(VecElem) + size);
}
// return total size of an array buffer (variable-size elements)
template <typename L>
inline unsigned vecVarSize(unsigned n, L &&l) {
  unsigned size = 0;
  for (unsigned i = 0; i < n; i++) size += ZuFwd<L>(l)(i);
  return sizeof(VecHdr) + n * sizeof(VecElem) + size;
}
// initialize array header
inline void vecInit(ZuSpan<uint8_t> &buf, unsigned oid, unsigned n) {
  new (buf.data()) VecHdr{1, 0, int32_t(oid), int32_t(n), 1};
  buf.offset(sizeof(VecHdr));
}
// append array element
template <typename L>
inline void vecAppend(ZuSpan<uint8_t> &buf, unsigned length, L &&l) {
  new (buf.data()) VecElem{length};
  buf.offset(sizeof(VecElem));
  ZuFwd<L>(l)(buf.data(), length);
  buf.offset(length);
}
// read array header (advances buf)
inline const VecHdr *vecHdr(ZuBSpan &buf) {
  auto hdr = reinterpret_cast<const VecHdr *>(buf.data());
  buf.offset(sizeof(VecHdr));
  return hdr;
}
// validate received array header
inline int validateVecHdr(const VecHdr *hdr)
{
  if (!int32_t(hdr->ndim)) return 0; // empty
  ZiAssert(int32_t(hdr->ndim) == 1, "ZdbPQ",
    (i = int32_t(hdr->ndim)), "ndim=" << i, return -1);
  ZiAssert(int32_t(hdr->lbound) == 1, "ZdbPQ",
    (i = int32_t(hdr->lbound)), "lbound=" << i, return -1);
  return int32_t(hdr->length);
}
// read array element (advances buf)
template <typename L>
inline auto vecElem(ZuBSpan &buf, L &&l) {
  auto elem = reinterpret_cast<const VecElem *>(buf.data());
  unsigned length = elem->length;
  buf.offset(sizeof(VecElem));
  auto ptr = buf.data();
  buf.offset(length);
  return ZuFwd<L>(l)(ptr, length);
}

// Note: all the types comprising the Value<> union must be distinct type IDs
// even if their definition is identical
using String = ZuCSpan;
using Bytes = ZuBSpan;
struct Bitmap { ZuBSpan v; };
#pragma pack(push, 1)
struct Bool { uint8_t v; };
struct Int8 { int8_t v; };
struct UInt8 { uint8_t v; };
struct Int16 { ZuBigEndian<int16_t> v; };
struct UInt16 { ZuBigEndian<uint16_t> v; };
struct Int32 { ZuBigEndian<int32_t> v; };
struct UInt32 { ZuBigEndian<uint32_t> v; };
struct Int64 { ZuBigEndian<int64_t> v; };
struct UInt64 { ZuBigEndian<uint64_t> v; };
struct Float { ZuBigEndian<double> v; };
struct Fixed { ZuBigEndian<int128_t> v; };
struct Decimal { ZuBigEndian<int128_t> v; };
struct Time { ZuBigEndian<int64_t> sec; ZuBigEndian<int32_t> nsec; };
struct DateTime { ZuBigEndian<int64_t> sec; ZuBigEndian<int32_t> nsec; };
struct Int128 { ZuBigEndian<int128_t> v; };
struct UInt128 { ZuBigEndian<uint128_t> v; };
struct IPHdr {
  uint8_t	family = 2;	// PGSQL_AF_INET
  uint8_t	bits = 32;
  uint8_t	is_cidr = 0;
  uint8_t	len = 4;
};
enum {
  PGSQL_AF_INET = 2,
  PGSQL_AF_INET6 = 3
};
struct IP {
  IPHdr		hdr;
  uint8_t	addr[16] = {};

  ZiIP ziIP() const {
    switch (hdr.family) {
      case PGSQL_AF_INET:
	if (hdr.len != sizeof(in_addr)) return {};
	{
	  in_addr a;
	  memcpy(&a, addr, sizeof(a));
	  return ZiIP{a};
	}
      case PGSQL_AF_INET6:
	if (hdr.len != sizeof(in6_addr)) return {};
	{
	  in6_addr a;
	  memcpy(&a, addr, sizeof(a));
	  return ZiIP{a};
	}
      default:
	return {};
    }
  }
  bool ziIP(ZiIP ip) {
    memset(addr, 0, sizeof(addr));
    switch (ip.type()) {
      case ZiIPType::V4:
	hdr = {PGSQL_AF_INET, 32, 0, sizeof(in_addr)};
	memcpy(addr, &ip.inAddr(), sizeof(in_addr));
	return true;
      case ZiIPType::V6:
	hdr = {PGSQL_AF_INET6, 128, 0, sizeof(in6_addr)};
	memcpy(addr, &ip.in6Addr(), sizeof(in6_addr));
	return true;
      default:
	return false;
    }
  }
};
#pragma pack(pop)
// these must all be distinct types
struct StringVec { ZuBSpan v; };
struct BytesVec { ZuBSpan v; };
struct Int8Vec { ZuBSpan v; };
struct UInt8Vec { ZuBSpan v; };
struct Int16Vec { ZuBSpan v; };
struct UInt16Vec { ZuBSpan v; };
struct Int32Vec { ZuBSpan v; };
struct UInt32Vec { ZuBSpan v; };
struct Int64Vec { ZuBSpan v; };
struct UInt64Vec { ZuBSpan v; };
struct Int128Vec { ZuBSpan v; };
struct UInt128Vec { ZuBSpan v; };
struct FloatVec { ZuBSpan v; };
struct FixedVec { ZuBSpan v; };
struct DecimalVec { ZuBSpan v; };
struct TimeVec { ZuBSpan v; };
struct DateTimeVec { ZuBSpan v; };

template <typename U> struct Elem_ { using T = void; };
template <> struct Elem_<StringVec> { using T = String; };
template <> struct Elem_<BytesVec> { using T = Bytes; };
template <> struct Elem_<Int8Vec> { using T = Int8; };
template <> struct Elem_<UInt8Vec> { using T = UInt8; };
template <> struct Elem_<Int16Vec> { using T = Int16; };
template <> struct Elem_<UInt16Vec> { using T = UInt16; };
template <> struct Elem_<Int32Vec> { using T = Int32; };
template <> struct Elem_<UInt32Vec> { using T = UInt32; };
template <> struct Elem_<Int64Vec> { using T = Int64; };
template <> struct Elem_<UInt64Vec> { using T = UInt64; };
template <> struct Elem_<Int128Vec> { using T = Int128; };
template <> struct Elem_<UInt128Vec> { using T = UInt128; };
template <> struct Elem_<FloatVec> { using T = Float; };
template <> struct Elem_<FixedVec> { using T = Fixed; };
template <> struct Elem_<DecimalVec> { using T = Decimal; };
template <> struct Elem_<TimeVec> { using T = Time; };
template <> struct Elem_<DateTimeVec> { using T = DateTime; };
template <typename U> using Elem = typename Elem_<U>::T;

using Value_ = ZuUnion<
  void,	// must be default zeroth type
  String,
  Bytes,
  Bool,
  Int8,
  UInt8,
  Int16,
  UInt16,
  Int32,
  UInt32,
  Int64,
  UInt64,
  Float,
  Fixed,
  Decimal,
  Time,
  DateTime,
  Int128,
  UInt128,
  Bitmap,
  IP,

  // all types after this are vectors, see isVec() below
  StringVec,
  BytesVec,
  Int8Vec,
  UInt8Vec,
  Int16Vec,
  UInt16Vec,
  Int32Vec,
  UInt32Vec,
  Int64Vec,
  UInt64Vec,
  Int128Vec,
  UInt128Vec,
  FloatVec,
  FixedVec,
  DecimalVec,
  TimeVec,
  DateTimeVec>;

enum { VecBase = Value_::Index<StringVec>{} };

// isVec() - is a vector/array
constexpr bool isVec(unsigned i) { return i >= VecBase; }
// isVar() - is variable-length
constexpr bool isVar(unsigned i) {
  // * Bytes and String are excluded because they can be zero-copied
  //   - PG -> flatbuffers - the PGresult remains in scope
  //   - flatbuffers -> PG - the flatbuffer remains in scope
  // * Bitmap needs copying and byte-swapping (sigh)
  // * All vectors/arrays need copying and transforming into PG binary format
  //   when transforming from flatbuffers to PG (they are zero-copy in
  //   the other direction)
  return i == Value_::Index<Bitmap>{} || isVec(i);
}

// Postgres binary format - load from PGresult
struct Value : public Value_ {
  ZuDerive_(Value, Value_)

  // void

  // String - zero-copy - relies on the PGresult remaining in scope

  // Bytes - zero-copy - relies on the PGresult remaining in scope

  // Vectors - zero-copy - relies on the PGresult remaining in scope

  // All other types - memcpy
#if defined(__GNUC__) && !defined(__llvm__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wclass-memaccess"
#endif

#if defined(__GNUC__) && !defined(__llvm__)
#pragma GCC diagnostic pop
#endif

  template <unsigned I, typename T = Value_::Type<I>,
    typename = decltype(void(bool(ZuIsSame<void, T>{})), void(bool(ZuIsSame<String,
      T>{})), void(bool(ZuIsSame<Bytes, T>{})), void(bool(isVar(I))),
      void(bool(ZuIsSame<IP, T>{})))>
  bool
  load(const char *data, unsigned length) {
    if constexpr (ZuIsSame<void, T>{}) {
      type_(I); return true;
    } else if constexpr (ZuIsSame<String, T>{}) {
      new (new_<I, true>()) T{data, length};
      return true;
    } else if constexpr (ZuIsSame<Bytes, T>{}) {
      new (new_<I, true>()) T{ZuCSpan{data, length}};
      return true;
    } else if constexpr (isVar(I)) {
      new (new_<I, true>()) T{ZuCSpan{data, length}};
      return true;
    } else if constexpr (!ZuIsSame<void, T>{} && !ZuIsSame<String, T>{} &&
      !ZuIsSame<Bytes, T>{} && !ZuIsSame<IP, T>{} && !isVar(I)) {
      if (length != sizeof(T)) return false;
      memcpy(static_cast<void *>(new_<I, true>()), data, length);
      return true;
    } else {
      if (length < sizeof(IPHdr)) return false;
      IP ip;
      memcpy(&ip.hdr, data, sizeof(IPHdr));
      if (ip.hdr.is_cidr) return false;
      switch (ip.hdr.family) {
	case PGSQL_AF_INET:
	  if (ip.hdr.bits != 32 || ip.hdr.len != sizeof(in_addr)) return false;
	  break;
	case PGSQL_AF_INET6:
	  if (ip.hdr.bits != 128 || ip.hdr.len != sizeof(in6_addr)) return false;
	  break;
	default:
	  return false;
      }
      if (length != sizeof(IPHdr) + ip.hdr.len) return false;
      memcpy(ip.addr, data + sizeof(IPHdr), ip.hdr.len);
      new (new_<I, true>()) IP{ip};
      return true;
    }
  }

  // Postgres binary format - save to params - data<I>(), length<I>()

  // void - return {nullptr, 0}

  // String - return raw string data

  // Bytes - return raw byte data

  // variable-sized - return raw byte data

  // All other types - return bigendian packed struct
  template <unsigned I, typename T = Value_::Type<I>,
    typename = decltype(void(bool(ZuIsSame<void, T>{})), void(bool(ZuIsSame<String,
      T>{})), void(bool(ZuIsSame<Bytes, T>{})), void(bool(isVar(I))),
      void(bool(ZuIsSame<IP, T>{})))>
  const char *
  data() const {
    if constexpr (ZuIsSame<void, T>{}) {
      return nullptr;
    } else if constexpr (ZuIsSame<String, T>{}) {
      return p<T>().data();
    } else if constexpr (ZuIsSame<Bytes, T>{}) {
      return reinterpret_cast<const char *>(p<T>().data());
    } else if constexpr (isVar(I)) {
      return reinterpret_cast<const char *>(p<T>().v.data());
    } else if constexpr (ZuIsSame<IP, T>{}) {
      return reinterpret_cast<const char *>(&p<T>());
    } else {
      return reinterpret_cast<const char *>(this);
    }
  }
  template <unsigned I, typename T = Value_::Type<I>,
    typename = decltype(void(bool(ZuIsSame<void, T>{})), void(bool(ZuIsSame<String,
      T>{})), void(bool(ZuIsSame<Bytes, T>{})), void(bool(isVar(I))),
      void(bool(ZuIsSame<IP, T>{})))>
  unsigned
  length() const {
    if constexpr (ZuIsSame<void, T>{}) {
      return 0;
    } else if constexpr (ZuIsSame<String, T>{}) {
      return p<T>().length();
    } else if constexpr (ZuIsSame<Bytes, T>{}) {
      return p<T>().length();
    } else if constexpr (isVar(I)) {
      return p<T>().v.length();
    } else if constexpr (ZuIsSame<IP, T>{}) {
      return sizeof(IPHdr) + p<T>().hdr.len;
    } else {
      return sizeof(T);
    }
  }

  // print value

  template <unsigned I, typename S,
    typename = ZuIfT<
      (I == Value_::Index<void>{}) ||
      (I == Value_::Index<String>{}) ||
      (I == Value_::Index<Bytes>{}) ||
      (I == Value_::Index<Bool>{} || I == Value_::Index<Int8>{} ||
	I == Value_::Index<UInt8>{} || I == Value_::Index<Int16>{} ||
	I == Value_::Index<UInt16>{} || I == Value_::Index<Int32>{} ||
	I == Value_::Index<UInt32>{} || I == Value_::Index<Int64>{} ||
	I == Value_::Index<UInt64>{} || I == Value_::Index<Int128>{} ||
	I == Value_::Index<UInt128>{} || I == Value_::Index<Float>{}) ||
      (I == Value_::Index<Fixed>{} || I == Value_::Index<Decimal>{}) ||
      (I == Value_::Index<Time>{} || I == Value_::Index<DateTime>{}) ||
      (I == Value_::Index<Bitmap>{}) ||
      (I == Value_::Index<IP>{}) ||
      (I == Value_::Index<StringVec>{}) ||
      (I == Value_::Index<BytesVec>{}) ||
      (I == Value_::Index<Int8Vec>{} || I == Value_::Index<UInt8Vec>{} ||
	I == Value_::Index<Int16Vec>{} || I == Value_::Index<UInt16Vec>{} ||
	I == Value_::Index<Int32Vec>{} || I == Value_::Index<UInt32Vec>{} ||
	I == Value_::Index<Int64Vec>{} || I == Value_::Index<UInt64Vec>{} ||
	I == Value_::Index<Int128Vec>{} || I == Value_::Index<UInt128Vec>{} ||
	I == Value_::Index<FloatVec>{}) ||
      (I == Value_::Index<FixedVec>{} || I == Value_::Index<DecimalVec>{}) ||
      (I == Value_::Index<TimeVec>{} || I == Value_::Index<DateTimeVec>{})>>
  void
  print_(S &s) const {
    if constexpr (I == Value_::Index<void>{}) {

    } else if constexpr (I == Value_::Index<String>{}) {
      s << ZfStruct_::Print::String{p<I>()};
    } else if constexpr (I == Value_::Index<Bytes>{}) {
      s << ZfStruct_::Print::Bytes{p<I>()};
    } else if constexpr (I == Value_::Index<Bool>{} || I == Value_::Index<Int8>{} ||
      I == Value_::Index<UInt8>{} || I == Value_::Index<Int16>{} ||
      I == Value_::Index<UInt16>{} || I == Value_::Index<Int32>{} ||
      I == Value_::Index<UInt32>{} || I == Value_::Index<Int64>{} ||
      I == Value_::Index<UInt64>{} || I == Value_::Index<Int128>{} ||
      I == Value_::Index<UInt128>{} || I == Value_::Index<Float>{}) {
      s << ZuBoxed(ZuUnderlying(p<I>().v));
    } else if constexpr (I == Value_::Index<Fixed>{} || I == Value_::Index<Decimal>{}) {
      s << ZuDecimal{ZuDecimal::Unscaled{ZuUnderlying(p<I>().v)}};
    } else if constexpr (I == Value_::Index<Time>{} || I == Value_::Index<DateTime>{}) {
      s << ZuTime{p<I>().sec, p<I>().nsec};
    } else if constexpr (I == Value_::Index<Bitmap>{}) {
      using Word = ZuBigEndian<uint64_t>;
      const auto &data_ = p<I>().v;
      ZuSpan<const Word> data(
	reinterpret_cast<const Word *>(&data_[0]),
	data_.length() / sizeof(uint64_t));
      ZtBitmap b;
      unsigned n = data.length() - 1;
      b.data.length(n);
      for (unsigned i = 0; i < n; i++) b.data[i] = data[i + 1];
      s << b;
    } else if constexpr (I == Value_::Index<IP>{}) {
      s << p<I>().ziIP();
    } else if constexpr (I == Value_::Index<StringVec>{}) {
      auto varBuf = p<I>().v;
      auto hdr = vecHdr(varBuf);
      unsigned n = int32_t(hdr->length);
      s << '[';
      for (unsigned i = 0; i < n; i++)
	if (i) s << ',';
	vecElem(varBuf, [&s](const uint8_t *ptr, unsigned length) {
	  s << ZfStruct_::Print::String{ZuCSpan(ptr, length)};
	});
      s << ']';
    } else if constexpr (I == Value_::Index<BytesVec>{}) {
      auto varBuf = p<I>().v;
      auto hdr = vecHdr(varBuf);
      unsigned n = int32_t(hdr->length);
      s << '[';
      for (unsigned i = 0; i < n; i++)
	if (i) s << ',';
	vecElem(varBuf, [&s](const uint8_t *ptr, unsigned length) {
	  s << ZfStruct_::Print::Bytes{ZuBSpan{ptr, length}};
	});
      s << ']';
    } else if constexpr (I == Value_::Index<Int8Vec>{} ||
      I == Value_::Index<UInt8Vec>{} || I == Value_::Index<Int16Vec>{} ||
      I == Value_::Index<UInt16Vec>{} || I == Value_::Index<Int32Vec>{} ||
      I == Value_::Index<UInt32Vec>{} || I == Value_::Index<Int64Vec>{} ||
      I == Value_::Index<UInt64Vec>{} || I == Value_::Index<Int128Vec>{} ||
      I == Value_::Index<UInt128Vec>{} || I == Value_::Index<FloatVec>{}) {
      using Elem = ZdbPQ::Elem<Value_::Type<I>>;
      auto varBuf = p<I>().v;
      auto hdr = vecHdr(varBuf);
      unsigned n = int32_t(hdr->length);
      s << '[';
      for (unsigned i = 0; i < n; i++)
	if (i) s << ',';
	vecElem(varBuf, [&s](const uint8_t *ptr, unsigned) {
	  auto elem = reinterpret_cast<const Elem *>(ptr);
	  s << ZuBoxed(ZuUnderlying(elem->v));
	});
      s << ']';
    } else if constexpr (I == Value_::Index<FixedVec>{} || I == Value_::Index<DecimalVec>{}) {
      using Elem = ZdbPQ::Elem<Value_::Type<I>>;
      auto varBuf = p<I>().v;
      auto hdr = vecHdr(varBuf);
      unsigned n = int32_t(hdr->length);
      s << '[';
      for (unsigned i = 0; i < n; i++)
	if (i) s << ',';
	vecElem(varBuf, [&s](const uint8_t *ptr, unsigned) {
	  auto elem = reinterpret_cast<const Elem *>(ptr);
	  s << ZuDecimal{ZuDecimal::Unscaled{elem->v}};
	});
      s << ']';
    } else {
      using Elem = ZdbPQ::Elem<Value_::Type<I>>;
      auto varBuf = p<I>().v;
      auto hdr = vecHdr(varBuf);
      unsigned n = int32_t(hdr->length);
      s << '[';
      for (unsigned i = 0; i < n; i++)
	if (i) s << ',';
	vecElem(varBuf, [&s](const uint8_t *ptr, unsigned) {
	  auto elem = reinterpret_cast<const Elem *>(ptr);
	  s << ZuTime{elem->sec, elem->nsec};
	});
      s << ']';
    }
  }

  template <typename S>
  void print(S &s) const {
    ZuSwitch::dispatch<Value_::N>(this->type(), [this, &s](auto I) {
      this->print_<I>(s);
    });
  }
  friend ZuPrintFn ZuPrintType(Value *);
};

// --- vector buffer

// VarBuf contains the entire Postgres array in native binary format
ZuDerive(VarBuf, (ZtArray<uint8_t, ZtArrayHeapID<"ZdbPQ.VarBuf">>));
// VarBufPart references the VarBuf fragment for an individual element
using VarBufPart = ZuTuple<unsigned, unsigned>;	// offset, length
// VarBufParts tracks VarBuf fragments for multiple elements
ZuDerive(VarBufParts, (ZtArray<VarBufPart, ZtArrayHeapID<"ZdbPQ.VarBufPart">>));

// varBufSize() calculates the size of a variable-sized type
template <unsigned Type, typename = ZuIfT<(Type < Value::N)>>
inline unsigned varBufSize(
  const reflection::Field *field, const Zfb::Table *fbo)
{
  if constexpr (!isVar(Type)) { return 0; } else if constexpr (Type == Value::Index<Bitmap>{}) {
    auto bitmap = fbo->GetPointer<const Zfb::Bitmap *>(field->offset());
    if (!bitmap || !bitmap->data()) return sizeof(uint64_t);
    return (bitmap->data()->size() + 1) * sizeof(uint64_t);
  } else if constexpr (Type == Value::Index<StringVec>{}) {
    auto v = Zfb::GetFieldV<Zfb::Offset<Zfb::String>>(*fbo, *field);
    if (!v) return vecVarSize(0, [](unsigned) { return 0U; });
    return vecVarSize(v->size(), [&v](unsigned i) -> unsigned {
      return v->Get(i)->size();
    });
  } else if constexpr (Type == Value::Index<BytesVec>{}) {
    auto v = Zfb::GetFieldV<Zfb::Offset<Zfb::Bytes>>(*fbo, *field);
    if (!v) return vecVarSize(0, [](unsigned) { return 0U; });
    return vecVarSize(v->size(), [&v](unsigned i) -> unsigned {
      auto data = v->Get(i)->data();
      return data ? data->size() : 0;
    });
  }

#define ZdbPQ_IntVarBufSize(width) \
  else if constexpr (Type == Value::Index<Int##width##Vec>{}) {  \
    auto v = Zfb::GetFieldV<int##width##_t>(*fbo, *field);  \
    return vecSize(v ? v->size() : 0, sizeof(Int##width));  \
  } \
  else if constexpr (Type == Value::Index<UInt##width##Vec>{}) {  \
    auto v = Zfb::GetFieldV<uint##width##_t>(*fbo, *field);  \
    return vecSize(v ? v->size() : 0, sizeof(UInt##width));  \
  }

  ZdbPQ_IntVarBufSize(8)
  ZdbPQ_IntVarBufSize(16)
  ZdbPQ_IntVarBufSize(32)
  ZdbPQ_IntVarBufSize(64)

  else if constexpr (Type == Value::Index<Int128Vec>{}) {
    auto v = Zfb::GetFieldV<Zfb::Int128 *>(*fbo, *field);
    return vecSize(v ? v->size() : 0, sizeof(Int128));
  } else if constexpr (Type == Value::Index<UInt128Vec>{}) {
    auto v = Zfb::GetFieldV<Zfb::UInt128 *>(*fbo, *field);
    return vecSize(v ? v->size() : 0, sizeof(UInt128));
  } else if constexpr (Type == Value::Index<FloatVec>{}) {
    auto v = Zfb::GetFieldV<double>(*fbo, *field);
    return vecSize(v ? v->size() : 0, sizeof(Float));
  } else if constexpr (Type == Value::Index<FixedVec>{}) {
    auto v = Zfb::GetFieldV<Zfb::Fixed *>(*fbo, *field);
    return vecSize(v ? v->size() : 0, sizeof(Fixed));
  } else if constexpr (Type == Value::Index<DecimalVec>{}) {
    auto v = Zfb::GetFieldV<Zfb::Decimal *>(*fbo, *field);
    return vecSize(v ? v->size() : 0, sizeof(Decimal));
  } else if constexpr (Type == Value::Index<TimeVec>{}) {
    auto v = Zfb::GetFieldV<Zfb::Time *>(*fbo, *field);
    return vecSize(v ? v->size() : 0, sizeof(Time));
  } else if constexpr (Type == Value::Index<DateTimeVec>{}) {
    auto v = Zfb::GetFieldV<Zfb::DateTime *>(*fbo, *field);
    return vecSize(v ? v->size() : 0, sizeof(DateTime));
  }
}

// --- postgres OIDs, type names

class OIDs {
  using OIDs_ = ZuArray<unsigned, Value::N - 1>;
  enum { Bits = ZuIntrin::log2(Value::N) };
  ZmLHashKVDerive(Types, unsigned, int8_t,
    ZmLHashStatic<Bits, ZmLHashLocal<>>);
  ZmLHashKVDerive(Lookup, ZuCSpan, int8_t,
    ZmLHashStatic<Bits, ZmLHashLocal<>>);

public:
  static const char *name(unsigned i);
  unsigned oid(unsigned i) const {
    if (i < 1 || i >= Value::N) return ZuCmp<unsigned>::null();
    return m_oids[i - 1];
  }
  bool match(unsigned oid, unsigned type) const {
    auto i = m_types.citer(oid);
    while (auto kv = i())
      if (kv->p<1>() == type) return true;
    return false;
  }
  unsigned oid(ZuCSpan name) const {
    int8_t i = m_lookup.findVal(name);
    if (ZuNull(i)) return ZuCmp<unsigned>::null();
    ZmAssert(i >= 1 && i < Value::N);
    return m_oids[i - 1];
  }

  void init(int8_t i, unsigned oid) {
    ZmAssert(i >= 1 && i < Value::N);
    m_oids[i - 1] = oid;
    m_types.add(unsigned(oid), i);
    m_lookup.add(name(i), i);	// only add resolved names to lookup
  }

private:
  OIDs_		m_oids;
  Types		m_types;
  Lookup	m_lookup;
};

// --- load value from flatbuffer (for sending to postgres)

template <unsigned Type>
inline void loadValue(
  void *ptr, const reflection::Field *field, const Zfb::Table *fbo)
{
  if constexpr (isVar(Type)) { } else if constexpr (Type == Value::Index<void>{}) { } else if constexpr (Type == Value::Index<String>{}) {
    new (ptr) String{Zfb::Load::str(Zfb::GetFieldS(*fbo, *field))};
  } else if constexpr (Type == Value::Index<Bytes>{}) {
    new (ptr) Bytes{Zfb::Load::bytes(Zfb::GetFieldV<uint8_t>(*fbo, *field))};
  } else if constexpr (Type == Value::Index<Bool>{}) {
    new (ptr) Bool{Zfb::GetFieldI<bool>(*fbo, *field)};
  }

#define ZdbPQ_LoadInt(width) \
  else if constexpr (Type == Value::Index<Int##width>{}) {  \
    new (ptr) Int##width{Zfb::GetFieldI<int##width##_t>(*fbo, *field)};  \
  } \
  else if constexpr (Type == Value::Index<UInt##width>{}) {  \
    new (ptr) UInt##width{Zfb::GetFieldI<uint##width##_t>(*fbo, *field)};  \
  }

  ZdbPQ_LoadInt(8)
  ZdbPQ_LoadInt(16)
  ZdbPQ_LoadInt(32)
  ZdbPQ_LoadInt(64)

  else if constexpr (Type == Value::Index<Float>{}) {
    new (ptr) Float{Zfb::GetFieldF<double>(*fbo, *field)};
  } else if constexpr (Type == Value::Index<Fixed>{}) {
    ZuDecimal v = ZfbTransform::Fixed::load(
      fbo->GetStruct<const Zfb::Fixed *>(field->offset())).decimal();
    new (ptr) Fixed{v.value};
  } else if constexpr (Type == Value::Index<Decimal>{}) {
    ZuDecimal v = ZfbTransform::Decimal::load(
      fbo->GetStruct<const Zfb::Decimal *>(field->offset()));
    new (ptr) Decimal{v.value};
  } else if constexpr (Type == Value::Index<Time>{}) {
    auto t = fbo->GetStruct<const Zfb::Time *>(field->offset());
    new (ptr) Time{t->sec(), t->nsec()};
  } else if constexpr (Type == Value::Index<DateTime>{}) {
    auto t =
      ZfbTransform::DateTime::load(
	fbo->GetStruct<const Zfb::DateTime *>(field->offset())).as_time();
    new (ptr) DateTime{t.sec(), t.nsec()};
  } else if constexpr (Type == Value::Index<Int128>{}) {
    new (ptr) Int128{ZfbTransform::Int128::load(
      fbo->GetStruct<const Zfb::Int128 *>(field->offset()))};
  } else if constexpr (Type == Value::Index<UInt128>{}) {
    new (ptr) UInt128{ZfbTransform::UInt128::load(
      fbo->GetStruct<const Zfb::UInt128 *>(field->offset()))};
  } else if constexpr (Type == Value::Index<IP>{}) {
    IP ip;
    ip.ziIP(ZfbTransform::IP::load(
      static_cast<Zfb::IP>(fbo->GetField<uint8_t>(field->offset() - 2, 0)),
      fbo->GetPointer<const void *>(field->offset())));
    new (ptr) IP{ip};
  }
}

template <unsigned Type, typename = ZuIfT<(Type < Value::N)>>
inline void loadValue(
  void *ptr, ZuSpan<uint8_t> varBuf_, const VarBufPart &varBufPart, const OIDs &oids,
    const reflection::Field *field, const Zfb::Table *fbo)
{
  if constexpr (!isVar(Type)) { } else if constexpr (Type == Value::Index<Bitmap>{}) {
    ZuSpan<uint8_t> varBuf(&varBuf_[varBufPart.p<0>()], varBufPart.p<1>());
    new (ptr) Bitmap{ZuBSpan(varBuf)};
    using Word = ZuBigEndian<uint64_t>;
    ZuSpan<Word> data(
      reinterpret_cast<Word *>(&varBuf[0]),
      varBuf.length() / sizeof(uint64_t));
    auto bitmap = fbo->GetPointer<const Zfb::Bitmap *>(field->offset());
    if (!bitmap || !bitmap->data()) { data[0] = 0; return; }
    auto vec = bitmap->data();
    unsigned n = vec->size();
    data[0] = n;
    for (unsigned i = 0; i < n; i++) data[i + 1] = vec->Get(i);
  } else if constexpr (Type == Value::Index<StringVec>{}) {
    ZuSpan<uint8_t> varBuf(&varBuf_[varBufPart.p<0>()], varBufPart.p<1>());
    new (ptr) StringVec{ZuBSpan(varBuf)};
    auto v = Zfb::GetFieldV<Zfb::Offset<Zfb::String>>(*fbo, *field);
    unsigned n = v ? v->size() : 0;
    vecInit(varBuf, oids.oid(Value::Index<String>{}), n);
    for (unsigned i = 0; i < n; i++) {
      auto s = v->Get(i);
      vecAppend(varBuf, s->size(), [s](uint8_t *ptr, unsigned size) {
	memcpy(ptr, s->Data(), size);
      });
    }
  } else if constexpr (Type == Value::Index<BytesVec>{}) {
    ZuSpan<uint8_t> varBuf(&varBuf_[varBufPart.p<0>()], varBufPart.p<1>());
    new (ptr) BytesVec{ZuBSpan(varBuf)};
    auto v = Zfb::GetFieldV<Zfb::Offset<Zfb::Bytes>>(*fbo, *field);
    unsigned n = v ? v->size() : 0;
    vecInit(varBuf, oids.oid(Value::Index<Bytes>{}), n);
    for (unsigned i = 0; i < n; i++) {
      auto data = v->Get(i)->data();
      auto size = data ? data->size() : 0;
      vecAppend(varBuf, size, [data](uint8_t *ptr, unsigned size) {
	if (size) memcpy(ptr, data->Data(), size);
      });
    }
  }

#define ZdbPQ_LoadIntVec(width) \
  else if constexpr (Type == Value::Index<Int##width##Vec>{}) {  \
    ZuSpan<uint8_t> varBuf(&varBuf_[varBufPart.p<0>()], varBufPart.p<1>());  \
    new (ptr) Int##width##Vec{ZuBSpan(varBuf)};  \
    auto v = Zfb::GetFieldV<int##width##_t>(*fbo, *field);  \
    unsigned n = v ? v->size() : 0;  \
    vecInit(varBuf, oids.oid(Value::Index<Int##width>{}), n);  \
    for (unsigned i = 0; i < n; i++) {  \
      auto e = v->Get(i);  \
      vecAppend(varBuf, sizeof(Int##width), [e](uint8_t *ptr, unsigned) {  \
	new (ptr) Int##width{e};  \
      });  \
    }  \
  } \
  else if constexpr (Type == Value::Index<UInt##width##Vec>{}) {  \
    ZuSpan<uint8_t> varBuf(&varBuf_[varBufPart.p<0>()], varBufPart.p<1>());  \
    new (ptr) UInt##width##Vec{ZuBSpan(varBuf)};  \
    auto v = Zfb::GetFieldV<uint##width##_t>(*fbo, *field);  \
    unsigned n = v ? v->size() : 0;  \
    vecInit(varBuf, oids.oid(Value::Index<UInt##width>{}), n);  \
    for (unsigned i = 0; i < n; i++) {  \
      auto e = v->Get(i);  \
      vecAppend(varBuf, sizeof(UInt##width), [e](uint8_t *ptr, unsigned) {  \
	new (ptr) UInt##width{e};  \
      });  \
    }  \
  }

  ZdbPQ_LoadIntVec(8)
  ZdbPQ_LoadIntVec(16)
  ZdbPQ_LoadIntVec(32)
  ZdbPQ_LoadIntVec(64)

  else if constexpr (Type == Value::Index<Int128Vec>{}) {
    ZuSpan<uint8_t> varBuf(&varBuf_[varBufPart.p<0>()], varBufPart.p<1>());
    new (ptr) Int128Vec{ZuBSpan(varBuf)};
    auto v = Zfb::GetFieldV<Zfb::Int128 *>(*fbo, *field);
    unsigned n = v ? v->size() : 0;
    vecInit(varBuf, oids.oid(Value::Index<Int128>{}), n);
    for (unsigned i = 0; i < n; i++) {
      auto e = v->Get(i);
      vecAppend(varBuf, sizeof(Int128), [e](uint8_t *ptr, unsigned) {
	new (ptr) Int128{ZfbTransform::Int128::load(e)};
      });
    }
  } else if constexpr (Type == Value::Index<UInt128Vec>{}) {
    ZuSpan<uint8_t> varBuf(&varBuf_[varBufPart.p<0>()], varBufPart.p<1>());
    new (ptr) UInt128Vec{ZuBSpan(varBuf)};
    auto v = Zfb::GetFieldV<Zfb::UInt128 *>(*fbo, *field);
    unsigned n = v ? v->size() : 0;
    vecInit(varBuf, oids.oid(Value::Index<UInt128>{}), n);
    for (unsigned i = 0; i < n; i++) {
      auto e = v->Get(i);
      vecAppend(varBuf, sizeof(UInt128), [e](uint8_t *ptr, unsigned) {
	new (ptr) UInt128{ZfbTransform::UInt128::load(e)};
      });
    }
  } else if constexpr (Type == Value::Index<FloatVec>{}) {
    ZuSpan<uint8_t> varBuf(&varBuf_[varBufPart.p<0>()], varBufPart.p<1>());
    new (ptr) FloatVec{ZuBSpan(varBuf)};
    auto v = Zfb::GetFieldV<double>(*fbo, *field);
    unsigned n = v ? v->size() : 0;
    vecInit(varBuf, oids.oid(Value::Index<Float>{}), n);
    for (unsigned i = 0; i < n; i++) {
      auto e = v->Get(i);
      vecAppend(varBuf, sizeof(double), [e](uint8_t *ptr, unsigned) {
	new (ptr) Float{e};
      });
    }
  } else if constexpr (Type == Value::Index<FixedVec>{}) {
    ZuSpan<uint8_t> varBuf(&varBuf_[varBufPart.p<0>()], varBufPart.p<1>());
    new (ptr) FixedVec{ZuBSpan(varBuf)};
    auto v = Zfb::GetFieldV<Zfb::Fixed *>(*fbo, *field);
    unsigned n = v ? v->size() : 0;
    vecInit(varBuf, oids.oid(Value::Index<Fixed>{}), n);
    for (unsigned i = 0; i < n; i++) {
      auto e = v->Get(i);
      vecAppend(varBuf, sizeof(Fixed), [e](uint8_t *ptr, unsigned) {
	new (ptr) Fixed{ZuDecimal{ZfbTransform::Fixed::load(e).decimal()}.value};
      });
    }
  } else if constexpr (Type == Value::Index<DecimalVec>{}) {
    ZuSpan<uint8_t> varBuf(&varBuf_[varBufPart.p<0>()], varBufPart.p<1>());
    new (ptr) DecimalVec{ZuBSpan(varBuf)};
    auto v = Zfb::GetFieldV<Zfb::Decimal *>(*fbo, *field);
    unsigned n = v ? v->size() : 0;
    vecInit(varBuf, oids.oid(Value::Index<Decimal>{}), n);
    for (unsigned i = 0; i < n; i++) {
      auto e = v->Get(i);
      vecAppend(varBuf, sizeof(Decimal), [e](uint8_t *ptr, unsigned) {
	new (ptr) Decimal{ZfbTransform::Decimal::load(e).value};
      });
    }
  } else if constexpr (Type == Value::Index<TimeVec>{}) {
    ZuSpan<uint8_t> varBuf(&varBuf_[varBufPart.p<0>()], varBufPart.p<1>());
    new (ptr) TimeVec{ZuBSpan(varBuf)};
    auto v = Zfb::GetFieldV<Zfb::Time *>(*fbo, *field);
    unsigned n = v ? v->size() : 0;
    vecInit(varBuf, oids.oid(Value::Index<Time>{}), n);
    for (unsigned i = 0; i < n; i++) {
      auto e = v->Get(i);
      vecAppend(varBuf, sizeof(Time), [e](uint8_t *ptr, unsigned) {
	new (ptr) Time{e->sec(), e->nsec()};
      });
    }
  } else if constexpr (Type == Value::Index<DateTimeVec>{}) {
    ZuSpan<uint8_t> varBuf(&varBuf_[varBufPart.p<0>()], varBufPart.p<1>());
    new (ptr) DateTimeVec{ZuBSpan(varBuf)};
    auto v = Zfb::GetFieldV<Zfb::DateTime *>(*fbo, *field);
    unsigned n = v ? v->size() : 0;
    vecInit(varBuf, oids.oid(Value::Index<DateTime>{}), n);
    for (unsigned i = 0; i < n; i++) {
      auto e = v->Get(i);
      vecAppend(varBuf, sizeof(DateTime), [e](uint8_t *ptr, unsigned) {
	auto t = ZfbTransform::DateTime::load(e).as_time();
	new (ptr) DateTime{t.sec(), t.nsec()};
      });
    }
  }
}

// --- save value to flatbuffer (for passing to Zdb/app)

using Offset = Zfb::Offset<void>;

struct SavedOffset {
  Offset	offset;
  uint8_t	ipType;
};

struct Offsets {
  SavedOffset		*data;
  unsigned		in = 0;
  mutable unsigned	out = 0;

  Offsets(SavedOffset *data_) : data{data_} { }

  void push(Offset o) { data[in++] = {o, 0}; }
  void push(Offset o, uint8_t ipType) { data[in++] = {o, ipType}; }
  SavedOffset shift() const { return data[out++]; }
};

template <unsigned Type, typename = ZuIfT<(Type < Value::N)>>
inline void saveOffset(
  Zfb::Builder &fbb, Offsets &offsets, const Value &value)
{
  if constexpr (Type == Value::Index<String>{}) {
    offsets.push(Zfb::Save::str(fbb, value.p<Type>()).Union());
  } else if constexpr (Type == Value::Index<Bytes>{}) {
    offsets.push(Zfb::Save::bytes(fbb, value.p<Type>()).Union());
  } else if constexpr (Type == Value::Index<Bitmap>{}) {
    using Word = ZuBigEndian<uint64_t>;
    const auto &data_ = value.p<Type>().v;
    ZuSpan<const Word> data(
      reinterpret_cast<const Word *>(&data_[0]),
      data_.length() / sizeof(uint64_t));
    unsigned n = data.length() - 1;
    offsets.push(
      Zfb::CreateBitmap(fbb, Zfb::Save::pvectorIter<uint64_t>(
	  fbb, n, [&data](unsigned i) { return data[i + 1]; })).Union());
  } else if constexpr (Type == Value::Index<IP>{}) {
    auto ip = value.p<Type>().ziIP();
    offsets.push(
      ZfbTransform::IP::save(fbb, ip).offset,
      uint8_t(ZfbTransform::IP::type(ip)));
  } else if constexpr (Type == Value::Index<StringVec>{}) {
    auto varBuf = value.p<StringVec>().v;
    auto hdr = vecHdr(varBuf);
    int n = validateVecHdr(hdr);
    if (n < 0) return;
    offsets.push(
      Zfb::Save::strVecIter(fbb, n, [&varBuf](unsigned) {
	return vecElem(varBuf, [](const uint8_t *ptr, unsigned length) {
	  return ZuCSpan(ptr, length);
	});
      }).Union());
  } else if constexpr (Type == Value::Index<BytesVec>{}) {
    auto varBuf = value.p<BytesVec>().v;
    auto hdr = vecHdr(varBuf);
    int n = validateVecHdr(hdr);
    if (n < 0) return;
    offsets.push(Zfb::Save::vectorIter<Zfb::Bytes>(fbb, n,
      [&varBuf](Zfb::Builder &fbb, unsigned) {
	return Zfb::CreateBytes(fbb, Zfb::Save::bytes(fbb,
	  vecElem(varBuf, [](const uint8_t *ptr, unsigned length) {
	    return ZuBSpan{ptr, length};
	  })));
      }).Union());
  }

#define ZdbPQ_SaveIntVec(width) \
  else if constexpr (Type == Value::Index<Int##width##Vec>{}) {  \
    auto varBuf = value.p<Int##width##Vec>().v;  \
    auto hdr = vecHdr(varBuf);  \
    int n = validateVecHdr(hdr);  \
    if (n < 0) return;  \
    offsets.push(Zfb::Save::pvectorIter<int##width##_t>(  \
      fbb, n, [&varBuf](unsigned) {  \
	return vecElem(varBuf, [](const uint8_t *ptr, unsigned) {  \
	  return int##width##_t(reinterpret_cast<const Int##width *>(ptr)->v);  \
	});  \
    }).Union());  \
  } \
  else if constexpr (Type == Value::Index<UInt##width##Vec>{}) {  \
    auto varBuf = value.p<UInt##width##Vec>().v;  \
    auto hdr = vecHdr(varBuf);  \
    int n = validateVecHdr(hdr);  \
    if (n < 0) return;  \
    offsets.push(Zfb::Save::pvectorIter<uint##width##_t>(  \
      fbb, n, [&varBuf](unsigned) {  \
	return vecElem(varBuf, [](const uint8_t *ptr, unsigned) {  \
	  return uint##width##_t(reinterpret_cast<const UInt##width *>(ptr)->v);  \
	});  \
    }).Union());  \
  }

  ZdbPQ_SaveIntVec(8)
  ZdbPQ_SaveIntVec(16)
  ZdbPQ_SaveIntVec(32)
  ZdbPQ_SaveIntVec(64)

  else if constexpr (Type == Value::Index<Int128Vec>{}) {
    auto varBuf = value.p<Int128Vec>().v;
    auto hdr = vecHdr(varBuf);
    int n = validateVecHdr(hdr);
    if (n < 0) return;
    offsets.push(Zfb::Save::structVecIter<Zfb::Int128>(fbb, n,
      [&varBuf](Zfb::Int128 *ptr, unsigned) {
	*ptr = ZfbTransform::Int128::save(vecElem(varBuf,
	  [](const uint8_t *ptr, unsigned) {
	    return int128_t(reinterpret_cast<const Int128 *>(ptr)->v);
	  }));
      }).Union());
  } else if constexpr (Type == Value::Index<UInt128Vec>{}) {
    auto varBuf = value.p<UInt128Vec>().v;
    auto hdr = vecHdr(varBuf);
    int n = validateVecHdr(hdr);
    if (n < 0) return;
    offsets.push(Zfb::Save::structVecIter<Zfb::UInt128>(fbb, n,
      [&varBuf](Zfb::UInt128 *ptr, unsigned) {
	*ptr = ZfbTransform::UInt128::save(vecElem(varBuf,
	  [](const uint8_t *ptr, unsigned) {
	    return uint128_t(reinterpret_cast<const UInt128 *>(ptr)->v);
	  }));
      }).Union());
  } else if constexpr (Type == Value::Index<FloatVec>{}) {
    auto varBuf = value.p<FloatVec>().v;
    auto hdr = vecHdr(varBuf);
    int n = validateVecHdr(hdr);
    if (n < 0) return;
    offsets.push(Zfb::Save::pvectorIter<double>(fbb, n, [&varBuf](unsigned) {
      return vecElem(varBuf, [](const uint8_t *ptr, unsigned) {
	return double(reinterpret_cast<const Float *>(ptr)->v);
      });
    }).Union());
  } else if constexpr (Type == Value::Index<FixedVec>{}) {
    auto varBuf = value.p<FixedVec>().v;
    auto hdr = vecHdr(varBuf);
    int n = validateVecHdr(hdr);
    if (n < 0) return;
    offsets.push(Zfb::Save::structVecIter<Zfb::Fixed>(fbb, n,
      [&varBuf](Zfb::Fixed *ptr, unsigned) {
	*ptr = ZfbTransform::Fixed::save(vecElem(varBuf,
	  [](const uint8_t *ptr, unsigned) {
	    return ZuFixed{ZuDecimal{ZuDecimal::Unscaled{
	      reinterpret_cast<const Fixed *>(ptr)->v
	    }}};
	  }));
      }).Union());
  } else if constexpr (Type == Value::Index<DecimalVec>{}) {
    auto varBuf = value.p<DecimalVec>().v;
    auto hdr = vecHdr(varBuf);
    int n = validateVecHdr(hdr);
    if (n < 0) return;
    offsets.push(Zfb::Save::structVecIter<Zfb::Decimal>(fbb, n,
      [&varBuf](Zfb::Decimal *ptr, unsigned) {
	*ptr = ZfbTransform::Decimal::save(vecElem(varBuf,
	  [](const uint8_t *ptr, unsigned) {
	    return ZuDecimal{ZuDecimal::Unscaled{
	      reinterpret_cast<const Decimal *>(ptr)->v}};
	  }));
      }).Union());
  } else if constexpr (Type == Value::Index<TimeVec>{}) {
    auto varBuf = value.p<TimeVec>().v;
    auto hdr = vecHdr(varBuf);
    int n = validateVecHdr(hdr);
    if (n < 0) return;
    offsets.push(Zfb::Save::structVecIter<Zfb::Time>(fbb, n,
      [&varBuf](Zfb::Time *ptr, unsigned) {
	*ptr = ZfbTransform::Time::save(vecElem(varBuf,
	  [](const uint8_t *ptr, unsigned) {
	    auto t = reinterpret_cast<const Time *>(ptr);
	    return ZuTime{t->sec, t->nsec};
	  }));
      }).Union());
  } else if constexpr (Type == Value::Index<DateTimeVec>{}) {
    auto varBuf = value.p<DateTimeVec>().v;
    auto hdr = vecHdr(varBuf);
    int n = validateVecHdr(hdr);
    if (n < 0) return;
    offsets.push(Zfb::Save::structVecIter<Zfb::DateTime>(fbb, n,
      [&varBuf](Zfb::DateTime *ptr, unsigned) {
	*ptr = ZfbTransform::DateTime::save(vecElem(varBuf,
	  [](const uint8_t *ptr, unsigned) {
	    auto t = reinterpret_cast<const DateTime *>(ptr);
	    return ZuDateTime{ZuTime{t->sec, t->nsec}};
	  }));
      }).Union());
  } else if constexpr (Type != Value::Index<String>{} &&
    Type != Value::Index<Bytes>{} &&
    Type != Value::Index<IP>{} &&
    !isVar(Type)) { }
}

template <unsigned Type>
inline void saveValue(
  Zfb::Builder &fbb, const Offsets &offsets, const reflection::Field *field, const Value &value)
{
  if constexpr (Type == Value::Index<void>{}) { } else if constexpr (Type == Value::Index<String>{} ||
    Type == Value::Index<Bytes>{} ||
    isVar(Type)) {
    fbb.AddOffset(field->offset(), offsets.shift().offset);
  } else if constexpr (Type == Value::Index<Bool>{}) {
    fbb.AddElement<bool>(
      field->offset(), value.p<Type>().v, field->default_integer());
  }

#define ZdbPQ_SaveInt(width) \
  else if constexpr (Type == Value::Index<Int##width>{}) {  \
    fbb.AddElement<int##width##_t>(  \
      field->offset(), value.p<Type>().v, field->default_integer());  \
  } \
  else if constexpr (Type == Value::Index<UInt##width>{}) {  \
    fbb.AddElement<uint##width##_t>(  \
      field->offset(), value.p<Type>().v, field->default_integer());  \
  }

  ZdbPQ_SaveInt(8)
  ZdbPQ_SaveInt(16)
  ZdbPQ_SaveInt(32)
  ZdbPQ_SaveInt(64)

  else if constexpr (Type == Value::Index<Float>{}) {
    fbb.AddElement<double>(
      field->offset(), value.p<Type>().v, field->default_real());
  } else if constexpr (Type == Value::Index<Fixed>{}) {
    auto v = ZfbTransform::Fixed::save(
      ZuFixed{ZuDecimal{ZuDecimal::Unscaled{value.p<Type>().v}}});
    fbb.AddStruct(field->offset(), &v);
  } else if constexpr (Type == Value::Index<Decimal>{}) {
    auto v = ZfbTransform::Decimal::save(
      ZuDecimal{ZuDecimal::Unscaled{value.p<Type>().v}});
    fbb.AddStruct(field->offset(), &v);
  } else if constexpr (Type == Value::Index<Time>{}) {
    const auto &v_ = value.p<Type>();
    auto v = ZfbTransform::Time::save(ZuTime{v_.sec, v_.nsec});
    fbb.AddStruct(field->offset(), &v);
  } else if constexpr (Type == Value::Index<DateTime>{}) {
    const auto &v_ = value.p<Type>();
    auto v = ZfbTransform::DateTime::save(ZuDateTime{ZuTime{v_.sec, v_.nsec}});
    fbb.AddStruct(field->offset(), &v);
  } else if constexpr (Type == Value::Index<Int128>{}) {
    auto v = ZfbTransform::Int128::save(value.p<Type>().v);
    fbb.AddStruct(field->offset(), &v);
  } else if constexpr (Type == Value::Index<UInt128>{}) {
    auto v = ZfbTransform::UInt128::save(value.p<Type>().v);
    fbb.AddStruct(field->offset(), &v);
  } else if constexpr (Type == Value::Index<IP>{}) {
    auto saved = offsets.shift();
    fbb.AddElement<uint8_t>(
      field->offset() - 2, saved.ipType, 0);
    fbb.AddOffset(field->offset(), saved.offset);
  }
}

// --- data tuple

ZuDerive(Tuple, (ZtArray<Value, ZtArrayHeapID<"ZdbPQ.Tuple">>));
ZuDerive(ResultTuple,
  (ZtArray<Value, ZtArrayHeapID<"ZdbPQ.ResultTuple">>));
ZuDerive(QueryParams,
  (ZtArray<uintptr_t, ZtArrayHeapID<"ZdbPQ.QueryParams">>));
ZuDerive(PrepParams,
  (ZtArray<uintptr_t, ZtArrayHeapID<"ZdbPQ.PreparedParams">>));
ZuDerive(SavedOffsets,
  (ZtArray<SavedOffset, ZtArrayHeapID<"ZdbPQ.SavedOffset">>));

struct ParamLayout {
  static constexpr unsigned WordSize = sizeof(uintptr_t);

  static constexpr unsigned oidWords(unsigned n) {
    return (n * sizeof(Oid) + WordSize - 1) / WordSize;
  }
  static constexpr unsigned intWords(unsigned n) {
    return (n * sizeof(int) + WordSize - 1) / WordSize;
  }
  static constexpr unsigned words(unsigned n, bool types) {
    return (types ? oidWords(n) : 0) + n + (intWords(n) << 1);
  }

  ParamLayout(uintptr_t *ptr, unsigned n, bool types)
  {
    if (!n) return;
    if (types) {
      oids = reinterpret_cast<Oid *>(ptr);
      ptr += oidWords(n);
    }
    values = reinterpret_cast<const char **>(ptr);
    ptr += n;
    lengths = reinterpret_cast<int *>(ptr);
    ptr += intWords(n);
    formats = reinterpret_cast<int *>(ptr);
  }

  Oid		*oids = nullptr;
  const char	**values = nullptr;
  int		*lengths = nullptr;
  int		*formats = nullptr;
};

ZuAssert(alignof(Oid) <= alignof(uintptr_t));
ZuAssert(alignof(int) <= alignof(uintptr_t));
ZuAssert(ParamLayout::words(Zdb_::maxFields() + 4, true) <=
  ZuCmp<unsigned>::maximum());

// load tuple from flatbuffer
// - when called from select_send(), nParams is < fields.length()
template <typename Tuple_, typename Fields>
void loadTuple(
  Tuple_ &tuple,
  ZuSpan<uint8_t> varBuf,
  ZuSpan<const VarBufPart> varBufParts,
  const OIDs &oids,
  unsigned nParams,
  const XFields &xFields, const Fields &fields,
  const Zfb::Table *fbo)
{
  unsigned j = 0;
  for (unsigned i = 0; i < nParams; i++) {
    auto value = static_cast<Value *>(tuple.push());
    const auto &xField = xFieldAt(xFields, fields, i);
    auto type = xField.type;
    if (!isVar(type))
      ZuSwitch::dispatch<Value_::N>(type, [
	value, field = xField.field, fbo
      ](auto I) {
	loadValue<I>(value->new_<I, true>(), field, fbo);
      });
    else {
      auto &varBufPart = varBufParts[j++];
      ZuSwitch::dispatch<Value_::N>(type, [
	value, &varBuf, &varBufPart, &oids, field = xField.field, fbo
      ](auto I) {
	loadValue<I>(
	  value->new_<I, true>(), varBuf, varBufPart, oids, field, fbo);
      });
    }
  }
}

// save tuple to flatbuffer
template <typename Fields>
Offset saveTuple(
  Zfb::Builder &fbb,
  const XFields &xFields, const Fields &fields,
  unsigned nOffsets,
  ZuSpan<const Value> tuple)
{
  unsigned n = fields.length();
  ZmAssert(tuple.length() == n);
  auto offsets_ =
    ZmScratch(SavedOffset, nOffsets, SavedOffsets::VHeap);
  Offsets offsets(offsets_.data());
  for (unsigned i = 0; i < n; i++) {
    const auto &xField = xFieldAt(xFields, fields, i);
    auto type = xField.type;
    const auto &value = tuple[i];
    ZuSwitch::dispatch<Value::N>(type,
      [&fbb, &offsets, &value](auto I) {
	saveOffset<I>(fbb, offsets, value);
      });
  }
  auto start = fbb.StartTable();
  for (unsigned i = 0; i < n; i++) {
    const auto &xField = xFieldAt(xFields, fields, i);
    auto type = xField.type;
    const auto &value = tuple[i];
    ZuSwitch::dispatch<Value::N>(type,
      [&fbb, &offsets, field = xField.field, &value](auto I) {
	saveValue<I>(fbb, offsets, field, value);
      });
  }
  auto end = fbb.EndTable(start);
  return Offset{end};
}

ZtEnumNS(ZdbPQAPI, SendState, int8_t,
  Unsent,	// unsent
  Again,	// send attempted, need to retry
  Sent,	// sent, no server-side flush or sync needed
  Flush,	// sent, PQsendFlushRequest() needed
  Sync);	// sent, PQpipelineSync() needed

namespace Work {

struct Open { };

struct Count {
  KeyID			keyID;
  ZmRef<IOBuf>		buf;
  CountFn		countFn;
};

struct Select {
  KeyID			keyID;
  unsigned		limit;
  ZmRef<IOBuf>		buf;
  TupleFn		tupleFn;
  unsigned		count = 0;
  bool			selectRow;
  bool			selectNext;
  bool			inclusive;
};

struct Find {
  KeyID			keyID;
  ZmRef<IOBuf>		buf;
  RowFn			rowFn;
  bool			found = false;
};

struct Recover {
  Shard			shard;
  UN			un;
  RowFn			rowFn;
  bool			found = false;
};

struct Write {
  ZmRef<IOBuf>		buf;
  CommitFn		commitFn;
  bool			mrd = false;	// delete or ignored insert
  bool			skipped = false;	// insert left existing row unchanged
};

using Query = ZuUnion<Open, Count, Select, Find, Recover, Write>;

struct Start { };			// start data store

struct Stop { };

struct TblQuery {
  StoreTbl	*tbl;
  Query		query;
  bool		sync = false;	// pipeline sync
  bool		srm = false;	// single row mode
};

using Task = ZuUnion<Start, Stop, TblQuery>;

ZmListDerive(Queue, Task, ZmListHeapID<"ZdbPQ.Queue">);

} // Work

// start state for the data store
// - start is a heavy lift, involving several distinct phases
// - care is taken to alert and error out on schema inconsistencies
//   ... while automatically creating new tables and indices as needed
// - OIDs for all vocabulary types are retrieved and populated
// - metadata and MRD tables are idempotently created
class StartState {
public:
  uint32_t v = 0;	// public for printing/logging

  // phases
  enum {
    PreStart = 0,
    GetOIDs,	// retrieve OIDs
    MkSchema,	// idempotent create schema
    MkTblMeta,	// idempotent create DB metadata table
    SetNShards,	// initialize persistent DB shard count
    GetNShards,	// validate persistent DB shard count
    MkTblMRD,	// idempotent create MRD table
    Started	// start complete (possibly failed)
  };

private:
  // flags and masks
  enum {
    Create	= 0x8000,	// used by MkMRD
    Failed	= 0x4000,	// used by all
    TypeMask	= 0x3fff,	// used by GetOIDs
    PhaseShift	= 16
  };

public:
  constexpr void reset() { v = 0; }

  constexpr bool create() const { return v & Create; }
  constexpr bool failed() const { return v & Failed; }
  constexpr unsigned type() const { return v & TypeMask; }
  constexpr unsigned phase() const { return v>>PhaseShift; }

  constexpr void phase(uint32_t p) { v = p<<PhaseShift; }
  constexpr void incType() {
    ZmAssert(type() < TypeMask);
    ++v;
  }

  constexpr void setCreate() { v |= Create; }
  constexpr void clrCreate() { v &= ~Create; }

  constexpr void setFailed() { v |= Failed; }
  constexpr void clrFailed() { v &= ~Failed; }
};

// open state for a table
// - table open is a heavy lift, involving multiple distinct phases
// - some phases iterate over individual keys and fields
// - care is taken to alert and error out on schema inconsistencies
//   ... while automatically creating new tables and indices as needed
// - recover, find, insert, update and delete statements are prepared
// - max UN, SN are recovered
// - explicit state management is used, encapsulated with OpenState
class OpenState {
public:
  uint32_t v = 0;	// public for printing/logging

  // phases
  enum {
    Closed = 0,
    MkTable,	// idempotent create table
    MkIndices,	// idempotent create indices for all keys
    PrepCount,	// prepare count for all keys
    PrepSelectKIX, // Key, Initial, eXclusive - prepare select for all keys
    PrepSelectKNX, // Key, Next,    eXclusive - ''
    PrepSelectKNI, // Key, Next,    Inclusive - ''
    PrepSelectRIX, // Row, Initial, eXclusive - ''
    PrepSelectRNX, // Row, Next,    eXclusive - ''
    PrepSelectRNI, // Row, Next,    Inclusive - ''
    PrepFind,	// prepare recover and find for all keys
    PrepInsert,	// prepare insert query
    PrepUpdate,	// prepare update query
    PrepDelete,	// prepare delete query
    PrepMRD,	// prepare MRD update
    Count,	// query count
    MaxUN,	// query max UN, max SN from main table
    EnsureMRD,	// ensure MRD table has row for this table
    MRD,	// query max UN, max SN from mrd table
    Opened,	// open complete (possibly failed)
    Reopen	// closed, statements retained on this connection
  };

private:
  // flags and masks
  enum {
    Create	= 0x8000,	// MkTable, MkIndices
    Failed	= 0x4000,	// *
    FieldMask	= 0x3fff,	// MkTable, MkIndices
    KeyShift	= 16,		// MkIndices, PrepCount, PrepSelect*, PrepFind
    KeyMask	= 0x7f,		// up to 127 keys
    ShardShift	= 16,		// MaxUN, EnsureMRD, MRD
    ShardMask	= 0x3f,		// up to 64 shards
    PhaseShift	= 27		// up to 32 phases
  };

  ZuAssert(FieldMask >= Zdb_::maxFields());
  ZuAssert(KeyMask >= Zdb_::maxKeys());

public:
  constexpr void reset() { v = 0; }

  constexpr bool create() const { return v & Create; }
  constexpr bool failed() const { return v & Failed; }
  constexpr unsigned field() const { return v & FieldMask; }
  constexpr KeyID keyID() const { return (v>>KeyShift) & KeyMask; }
  constexpr Shard shard() const { return (v>>ShardShift) & ShardMask; }
  constexpr unsigned phase() const { return v>>PhaseShift; }

  constexpr void phase(uint32_t p) { v = p<<PhaseShift; }
  constexpr void incKey() {
    ZmAssert(keyID() < KeyMask);
    v = (v + (1<<KeyShift)) & ~(Create | Failed | FieldMask);
  }
  constexpr void incShard() {
    ZmAssert(shard() < ShardMask);
    v = (v + (1<<ShardShift)) & ~(Create | Failed | FieldMask);
  }
  constexpr void incField() {
    ZmAssert(field() < FieldMask);
    ++v;
  }

  constexpr void setCreate() { v |= Create; }
  constexpr void clrCreate() { v &= ~Create; }

  constexpr void setFailed() { v |= Failed; }
  constexpr void clrFailed() { v &= ~Failed; }
};

class StoreTbl : public Zdb_::StoreTbl {
friend Store;

public:
  StoreTbl(
    Store *store, bool internal, IDString id, unsigned nShards,
    ZfVFieldArray fields, ZfVKeyFieldArray keyFields,
    const reflection::Schema *schema, IOBufAllocFn bufAllocFn);

  Store *store() const { return m_store; }
  const auto &id() const { return m_id; }
  bool internal() const { return m_internal; }

protected:
  ~StoreTbl();

public:
  void open(OpenFn);
  void close(CloseFn);

  void warmup();

  void count(KeyID keyID, ZmRef<IOBuf>, CountFn);

  void select(
    bool selectRow, bool selectNext, bool inclusive,
    KeyID keyID, ZmRef<IOBuf>,
    unsigned limit, TupleFn);

  void find(KeyID keyID, ZmRef<IOBuf>, RowFn);

  void recover(Shard shard, UN, RowFn);

  void write(ZmRef<IOBuf>, CommitFn);

private:
  // open orchestration
  void open_enqueue(bool sync, bool srm);
  int open_send();
  void open_rcvd(PGresult *);
  void open_failed(ZeException);
  void opened();
  void initFieldMap();

  // open phases
  void mkTable();
  int mkTable_send();
  void mkTable_rcvd(PGresult *);

  void mkIndices();
  int mkIndices_send();
  void mkIndices_rcvd(PGresult *);

  void prepCount();
  int prepCount_send();
  void prepCount_rcvd(PGresult *);

  void prepSelect();
  int prepSelect_send();
  void prepSelect_rcvd(PGresult *);

  void prepFind();
  int prepFind_send();
  void prepFind_rcvd(PGresult *);

  void prepInsert();
  int prepInsert_send();
  void prepInsert_rcvd(PGresult *);

  void prepUpdate();
  int prepUpdate_send();
  void prepUpdate_rcvd(PGresult *);

  void prepDelete();
  int prepDelete_send();
  void prepDelete_rcvd(PGresult *);

  void prepMRD();
  int prepMRD_send();
  void prepMRD_rcvd(PGresult *);

  void openCount();
  int openCount_send();
  void openCount_rcvd(PGresult *);

  void maxUN();
  int maxUN_send();
  void maxUN_rcvd(PGresult *);

  void ensureMRD();
  int ensureMRD_send();
  void ensureMRD_rcvd(PGresult *);

  void mrd();
  int mrd_send();
  void mrd_rcvd(PGresult *);

  // principal queries
  int count_send(Work::Count &);
  void count_rcvd(Work::Count &, PGresult *);
  void count_failed(Work::Count &, ZeException);

  int select_send(Work::Select &);
  void select_rcvd(Work::Select &, PGresult *);
  template <typename Fields>
  ZmRef<IOBuf> select_save(
    ZuSpan<const Value> tuple, const Fields &fields, unsigned nOffsets);
  void select_failed(Work::Select &, ZeException);

  int find_send(Work::Find &);
  void find_rcvd(Work::Find &, PGresult *);
  template <bool Recovery>
  void find_rcvd_(RowFn &, bool &found, PGresult *);
  template <bool Recovery>
  ZmRef<IOBuf> find_save(ZuSpan<const Value> tuple);
  void find_failed(Work::Find &, ZeException);
  void find_failed_(RowFn, ZeException);

  int recover_send(Work::Recover &);
  void recover_rcvd(Work::Recover &, PGresult *);
  void recover_failed(Work::Recover &, ZeException);

  int write_send(Work::Write &);
  void write_rcvd(Work::Write &, PGresult *);
  void write_failed(Work::Write &, ZeException);

private:
  ZuDerive(FieldID, ZtString<ZtStringHeapID<"ZdbPQ.FieldID">>);
  ZmLHashKVDerive(FieldMapHash, FieldID, unsigned, ZmLHashLocal<>);
  template <typename Heap = ZuVoid> struct FieldMap_ : FieldMapHash, Heap {
    using FieldMapHash::FieldMapHash;
  };
  ZuDerive(FieldMapHeap, (ZmHeap<"ZdbPQ.FieldMap", FieldMap_<>>));
  ZuDerive(FieldMap, (FieldMap_<FieldMapHeap>));
  struct IndexState : IndexMatch {
    IDString	id;
    KeyID	keyID = 0;
  };
  ZuDerive(IndexStates,
    (ZtArray<IndexState, ZtArrayHeapID<"ZdbPQ.IndexState">>));
  ZmLHashKVDerive(IndexMapHash, ZuCSpan, unsigned, ZmLHashLocal<>);
  template <typename Heap = ZuVoid> struct IndexMap_ : IndexMapHash, Heap {
    using IndexMapHash::IndexMapHash;
  };
  ZuDerive(IndexMapHeap, (ZmHeap<"ZdbPQ.IndexMap", IndexMap_<>>));
  ZuDerive(IndexMap, (IndexMap_<IndexMapHeap>));
  ZuDerive(MissingIndices,
    (ZtArray<KeyID, ZtArrayHeapID<"ZdbPQ.MissingIndex">>));
  ZuDerive(MaxUN, (ZtArray<UN, ZtArrayHeapID<"ZdbPQ.MaxUN">>));

  Store			*m_store = nullptr;
  IDString		m_id;
  IDString		m_id_;		// snake case
  IDString		m_stmtID;	// statement prefix and zdb.mrd identity
  SQLString		m_relation;	// qualified SQL relation
  XFields		m_xFields;
  XFieldIxs		m_xUpdFields;
  XKeys			m_xKeys;
  uint16_t		m_xOffsets = 0;
  ZuPtr<FieldMap>	m_fieldMap;
  IndexStates		m_indexStates;
  ZuPtr<IndexMap>	m_indexMap;
  MissingIndices	m_missingIndices;
  uint16_t		m_missingPos = 0;
  IOBufAllocFn		m_bufAllocFn;
  bool			m_internal;

  OpenState		m_openState;
  OpenFn		m_openFn;	// open callback

  uint64_t		m_count = 0;
  MaxUN			m_maxUN;
  SN			m_maxSN = ZdbNullSN();
};

// --- PostgreSQL data store

inline auto StoreTbl_IDAxor(const StoreTbl &storeTbl) {
  return ZuTuple<bool, ZuCSpan>{storeTbl.internal(), storeTbl.id()};
}
ZmHashDerive(StoreTbls, StoreTbl,
  (ZmHashNode<StoreTbl,
    ZmHashKey<StoreTbl_IDAxor,
      ZmHashLock<ZmPLock,
	ZmHashHeapID<"ZdbPQ.StoreTbl">>>>));

struct StoreCf {
  String	thread;
  String	connect;
  bool		replicated = false;
};

ZfStruct(ZdbPQAPI, (StoreCf, Cf),
  (((thread), (Mutable, Required)),	String),
  (((connect), (Mutable, Required)),	String),
  (((replicated), (Mutable)),		Bool));

class Store : public Zdb_::Store {
public:
  InitResult init(const ZfCf::AnyNode *, ZiMultiplex *, unsigned, FailFn);
  void final();

  void start(StartFn);
  void stop(StopFn);

  void open(
    bool internal,
    IDString id,
    ZfVFieldArray fields,
    ZfVKeyFieldArray keyFields,
    const reflection::Schema *schema,
    IOBufAllocFn bufAllocFn,
    OpenFn openFn);

  bool stopping() const { return m_stopping; }

  void notice(const PGresult *); // async notice from libpq

  void enqueue(Work::Task task);

  template <typename ...Args> void run(Args &&...args) {
    m_mx->run(ZuFwd<Args>(args)..., m_sid);
  }
  template <typename ...Args> void invoke(Args &&...args) {
    m_mx->invoke(ZuFwd<Args>(args)..., m_sid);
  }

  const OIDs &oids() const { return m_oids; }

  template <int State>
  int sendQuery(const SQLString &query, const Tuple &params);
  int sendPrepare(
    const IDString &id, const SQLString &query, ZuSpan<unsigned> oids);
  template <int State, typename Params>
  int sendPrepared(const IDString &id, const Params &params);

  void disconnect(); // simulate connection failure

private:
  bool start_();
  void stop_();
  void stop_0();
  void stop_1();

  void wake();
  void run_();

  void recv();
  void rcvd(Work::Queue::Node *, PGresult *);
  void failed(Work::Queue::Node *, ZeException);

  void send();

  void setSRM();

  // start orchestration
  void start_enqueue();
  int start_send();
  void start_rcvd(PGresult *);
  void start_failed(bool running, ZeException);
  void start_failed_(ZeException);
  void started();

  // start phases
  void getOIDs();
  int getOIDs_send();
  void getOIDs_rcvd(PGresult *);

  void mkSchema();
  int mkSchema_send();
  void mkSchema_rcvd(PGresult *);

  void mkTblMeta();
  int mkTblMeta_send();
  void mkTblMeta_rcvd(PGresult *);

  void setNShards();
  int setNShards_send();
  void setNShards_rcvd(PGresult *);

  void getNShards();
  int getNShards_send();
  void getNShards_rcvd(PGresult *);

  void mkTblMRD();
  int mkTblMRD_send();
  void mkTblMRD_rcvd(PGresult *);

private:
  Zdb_::String		m_connect;
  ZiMultiplex		*m_mx = nullptr;
  unsigned		m_sid = 0;
  unsigned		m_nShards = 0;
  unsigned		m_storedNShards = 0;
  FailFn		m_failFn;

  ZmRef<StoreTbls>	m_storeTbls;

  PGconn		*m_conn = nullptr;
  int			m_connFD = -1;
  ZiEventLoop		m_eventLoop;

  // LATER - telemetry for queue lengths
  // LATER - test transient send failure, pushback, resend
  Work::Queue		m_queue;		// not yet sent
  Work::Queue		m_sent;			// sent, awaiting response
  bool			m_syncSRM = false;	// setSRM() following sync

  OIDs			m_oids;

  StartState		m_startState;
  StartFn		m_startFn;
  StopFn		m_stopFn;
  bool			m_stopping = false;
};

} // ZdbPQ

// main data store driver entry point
extern "C" {
  ZdbPQExtern Zdb_::Store *ZdbStore();
}

#endif /* ZdbPQ_HH */
