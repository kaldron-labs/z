//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef ZdbPQ_HH
#define ZdbPQ_HH

#ifndef ZdbPQLib_HH
#include <zlib/ZdbPQLib.hh>
#endif

#include <limits.h>

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

#include <zlib/ZiFB.hh>

#include <zlib/ZdbStore.hh>
#include <zlib/ZiFBReflect.hh>

namespace ZdbPQ {

class Store;
class StoreTbl;

using namespace Zdb_;
namespace R = ZiFB::Reflect;

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
// Fixed     ZuFixed    Zf.Fixed       zdecimal (**) int128_t BE
// Decimal   ZuDecimal  Zf.Decimal     zdecimal (**) int128_t BE
// Time      ZuTime     Zf.Time        ztime    (**) int64_t BE, int32_t BE
// DateTime  ZuDateTime Zf.DateTime    ztime    (**) int64_t BE, int32_t BE
// Int128    int128_t   Zf.Int128      int16    (*)  int128_t BE
// UInt128   uint128_t  Zf.UInt128     uint16   (*)  uint128_t BE
// Bitmap    ZtBitmap   Zf.Bitmap      zbitmap  (**) uint64_t BE, uint64_t[] BE
// IP        ZiIP       Zi.IP          inet          IPHdr, address bytes
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
  R::FieldView		field;		// flatbuffers reflection field
  unsigned			type;		// Value union discriminator
  unsigned			align = 1;	// resolved BFBS force_align
  unsigned			bufAlign = ZiFB::Wire::Align;
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
  unsigned	bufAlign = ZiFB::Wire::Align;
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
// UINT_MAX is the sizing-failure sentinel; valid sizes obey the Zdb cap.
inline unsigned sizeAdd(uint64_t size, uint64_t more, unsigned limit = Zdb_::MaxBufSize) {
  if (ZuUnlikely(size > limit || more > limit - size)) return UINT_MAX;
  return unsigned(size + more);
}
// return total size of an array buffer (fixed-size elements)
inline unsigned vecSize(unsigned n, unsigned size) {
  unsigned budget = Zdb_::MaxBufSize - sizeof(VecHdr);
  // Compare width before adding the prefix, then bound count before multiplying.
  if (ZuUnlikely(size > budget - sizeof(VecElem))) return UINT_MAX;
  unsigned width = sizeof(VecElem) + size;
  if (ZuUnlikely(n > budget/width)) return UINT_MAX;
  return sizeof(VecHdr) + n * width;
}
// return total size of an array buffer (variable-size elements)
template <typename L>
inline unsigned vecVarSize(unsigned n, L &&l) {
  unsigned size = vecSize(n, 0);
  if (size == UINT_MAX) return UINT_MAX;
  for (unsigned i = 0; i < n; i++) {
    size = sizeAdd(size, ZuFwd<L>(l)(i));
    if (size == UINT_MAX) return UINT_MAX;
  }
  return unsigned(size);
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

template <typename T, unsigned Code>
auto vector(ZiFB::TableView fbo, R::FieldView field) {
  using W = ZiFB::Value<ZuSpan<const T>, Code>;
  return ZiFB::VectorValue<typename W::Elem>{fbo.object(field.get<R::ZfField(Field, id)>())};
}

template <typename T, unsigned Code>
auto loadInline(ZiFB::TableView fbo, R::FieldView field,
  const ZfVField *native) {
  auto p = R::address(fbo, field);
  if (!p) return T(native->constant.template get<Code>(
    ZfVField::cget(ZfVFieldConstant::Deflt)));
  return ZiFB::valueAt<ZiFB::Value<T, Code>>(p);
}

inline auto bitmapData(ZiFB::TableView fbo, R::FieldView field) {
  return ZiFB::View<ZiFB::Values::Bitmap>{
    fbo.object(field.get<R::ZfField(Field, id)>())}.
    get<ZiFB::Values::ZfField(Bitmap, data)>();
}

// varBufSize() calculates the size of a variable-sized type
template <unsigned Type, typename = ZuIfT<(Type < Value::N)>>
inline unsigned varBufSize(
  R::FieldView field, ZiFB::TableView fbo)
{
  if constexpr (!isVar(Type)) { return 0; } else if constexpr (Type == Value::Index<Bitmap>{}) {
    unsigned n = bitmapData(fbo, field).length();
    if (ZuUnlikely(n > (Zdb_::MaxBufSize - sizeof(uint64_t))/sizeof(uint64_t)))
      return UINT_MAX;
    return sizeof(uint64_t) + n * sizeof(uint64_t);
  } else if constexpr (Type == Value::Index<StringVec>{}) {
    auto v = vector<ZuCSpan, ZfFieldTC::StringVec>(fbo, field);
    return vecVarSize(v.length(), [&v](unsigned i) -> unsigned {
      return v[i].length();
    });
  } else if constexpr (Type == Value::Index<BytesVec>{}) {
    auto v = vector<ZuBSpan, ZfFieldTC::BytesVec>(fbo, field);
    return vecVarSize(v.length(), [&v](unsigned i) -> unsigned {
      auto data = v[i];
      return data.length();
    });
  }

#define ZdbPQ_IntVarBufSize(width) \
  else if constexpr (Type == Value::Index<Int##width##Vec>{}) {  \
    auto v = vector<int##width##_t, ZfFieldTC::Int##width##Vec>(fbo, field);  \
    return vecSize(v.length(), sizeof(Int##width));  \
  } \
  else if constexpr (Type == Value::Index<UInt##width##Vec>{}) {  \
    auto v = vector<uint##width##_t, ZfFieldTC::UInt##width##Vec>(fbo, field);  \
    return vecSize(v.length(), sizeof(UInt##width));  \
  }

  ZdbPQ_IntVarBufSize(8)
  ZdbPQ_IntVarBufSize(16)
  ZdbPQ_IntVarBufSize(32)
  ZdbPQ_IntVarBufSize(64)

  else if constexpr (Type == Value::Index<Int128Vec>{}) {
    auto v = vector<int128_t, ZfFieldTC::Int128Vec>(fbo, field);
    return vecSize(v.length(), sizeof(Int128));
  } else if constexpr (Type == Value::Index<UInt128Vec>{}) {
    auto v = vector<uint128_t, ZfFieldTC::UInt128Vec>(fbo, field);
    return vecSize(v.length(), sizeof(UInt128));
  } else if constexpr (Type == Value::Index<FloatVec>{}) {
    return vecSize(R::vector(fbo, field).length(), sizeof(Float));
  } else if constexpr (Type == Value::Index<FixedVec>{}) {
    auto v = vector<ZuFixed, ZfFieldTC::FixedVec>(fbo, field);
    return vecSize(v.length(), sizeof(Fixed));
  } else if constexpr (Type == Value::Index<DecimalVec>{}) {
    auto v = vector<ZuDecimal, ZfFieldTC::DecimalVec>(fbo, field);
    return vecSize(v.length(), sizeof(Decimal));
  } else if constexpr (Type == Value::Index<TimeVec>{}) {
    auto v = vector<ZuTime, ZfFieldTC::TimeVec>(fbo, field);
    return vecSize(v.length(), sizeof(Time));
  } else if constexpr (Type == Value::Index<DateTimeVec>{}) {
    auto v = vector<ZuDateTime, ZfFieldTC::DateTimeVec>(fbo, field);
    return vecSize(v.length(), sizeof(DateTime));
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
  static ZdbPQAPI const char *name(unsigned i);
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
  void *ptr, R::FieldView field, ZiFB::TableView fbo,
  const ZfVField *native)
{
  if constexpr (isVar(Type)) { } else if constexpr (Type == Value::Index<void>{}) { } else if constexpr (Type == Value::Index<String>{}) {
    new (ptr) String{R::string(fbo, field)};
  } else if constexpr (Type == Value::Index<Bytes>{}) {
    new (ptr) Bytes{R::vector(fbo, field).bytes()};
  } else if constexpr (Type == Value::Index<Bool>{}) {
    new (ptr) Bool{R::scalar<uint8_t>(fbo, field)};
  }

#define ZdbPQ_LoadInt(width) \
  else if constexpr (Type == Value::Index<Int##width>{}) {  \
    new (ptr) Int##width{R::scalar<int##width##_t>(fbo, field)};  \
  } \
  else if constexpr (Type == Value::Index<UInt##width>{}) {  \
    new (ptr) UInt##width{R::scalar<uint##width##_t>(fbo, field)};  \
  }

  ZdbPQ_LoadInt(8)
  ZdbPQ_LoadInt(16)
  ZdbPQ_LoadInt(32)
  ZdbPQ_LoadInt(64)

  else if constexpr (Type == Value::Index<Float>{}) {
    if (R::type(field).get<R::ZfField(Type, baseType)>() == R::BaseType::Float)
      new (ptr) Float{R::scalar<float>(fbo, field)};
    else
      new (ptr) Float{R::scalar<double>(fbo, field)};
  } else if constexpr (Type == Value::Index<Fixed>{}) {
    ZuDecimal v = loadInline<ZuFixed, ZfFieldTC::Fixed>(fbo, field, native).decimal();
    new (ptr) Fixed{v.value};
  } else if constexpr (Type == Value::Index<Decimal>{}) {
    ZuDecimal v = loadInline<ZuDecimal, ZfFieldTC::Decimal>(fbo, field, native);
    new (ptr) Decimal{v.value};
  } else if constexpr (Type == Value::Index<Time>{}) {
    auto t = loadInline<ZuTime, ZfFieldTC::Time>(fbo, field, native);
    new (ptr) Time{t.sec(), t.nsec()};
  } else if constexpr (Type == Value::Index<DateTime>{}) {
    auto t =
      loadInline<ZuDateTime, ZfFieldTC::DateTime>(fbo, field, native).as_time();
    new (ptr) DateTime{t.sec(), t.nsec()};
  } else if constexpr (Type == Value::Index<Int128>{}) {
    new (ptr) Int128{loadInline<int128_t, ZfFieldTC::Int128>(fbo, field, native)};
  } else if constexpr (Type == Value::Index<UInt128>{}) {
    new (ptr) UInt128{loadInline<uint128_t, ZfFieldTC::UInt128>(fbo, field, native)};
  } else if constexpr (Type == Value::Index<IP>{}) {
    IP ip;
    ip.ziIP(ZiFB::IPMap::load(ZiFB::UnionView<ZiIP>{R::tag(fbo, field),
      fbo.object(field.get<R::ZfField(Field, id)>())}));
    new (ptr) IP{ip};
  }
}

template <unsigned Type, typename = ZuIfT<(Type < Value::N)>>
inline void loadValue(
  void *ptr, ZuSpan<uint8_t> varBuf_, const VarBufPart &varBufPart, const OIDs &oids,
    R::FieldView field, ZiFB::TableView fbo)
{
  if constexpr (!isVar(Type)) { } else if constexpr (Type == Value::Index<Bitmap>{}) {
    ZuSpan<uint8_t> varBuf(&varBuf_[varBufPart.p<0>()], varBufPart.p<1>());
    new (ptr) Bitmap{ZuBSpan(varBuf)};
    using Word = ZuBigEndian<uint64_t>;
    ZuSpan<Word> data(
      reinterpret_cast<Word *>(&varBuf[0]),
      varBuf.length() / sizeof(uint64_t));
    auto vec = bitmapData(fbo, field);
    unsigned n = vec.length();
    data[0] = n;
    for (unsigned i = 0; i < n; i++) data[i + 1] = vec[i];
  } else if constexpr (Type == Value::Index<StringVec>{}) {
    ZuSpan<uint8_t> varBuf(&varBuf_[varBufPart.p<0>()], varBufPart.p<1>());
    new (ptr) StringVec{ZuBSpan(varBuf)};
    auto v = vector<ZuCSpan, ZfFieldTC::StringVec>(fbo, field);
    unsigned n = v.length();
    vecInit(varBuf, oids.oid(Value::Index<String>{}), n);
    for (unsigned i = 0; i < n; i++) {
      auto s = v[i];
      vecAppend(varBuf, s.length(), [s](uint8_t *ptr, unsigned size) {
	memcpy(ptr, s.data(), size);
      });
    }
  } else if constexpr (Type == Value::Index<BytesVec>{}) {
    ZuSpan<uint8_t> varBuf(&varBuf_[varBufPart.p<0>()], varBufPart.p<1>());
    new (ptr) BytesVec{ZuBSpan(varBuf)};
    auto v = vector<ZuBSpan, ZfFieldTC::BytesVec>(fbo, field);
    unsigned n = v.length();
    vecInit(varBuf, oids.oid(Value::Index<Bytes>{}), n);
    for (unsigned i = 0; i < n; i++) {
      auto data = v[i];
      auto size = data.length();
      vecAppend(varBuf, size, [data](uint8_t *ptr, unsigned size) {
	if (size) memcpy(ptr, data.data(), size);
      });
    }
  }

#define ZdbPQ_LoadIntVec(width) \
  else if constexpr (Type == Value::Index<Int##width##Vec>{}) {  \
    ZuSpan<uint8_t> varBuf(&varBuf_[varBufPart.p<0>()], varBufPart.p<1>());  \
    new (ptr) Int##width##Vec{ZuBSpan(varBuf)};  \
    auto v = vector<int##width##_t, ZfFieldTC::Int##width##Vec>(fbo, field);  \
    unsigned n = v.length();  \
    vecInit(varBuf, oids.oid(Value::Index<Int##width>{}), n);  \
    for (unsigned i = 0; i < n; i++) {  \
      auto e = v[i];  \
      vecAppend(varBuf, sizeof(Int##width), [e](uint8_t *ptr, unsigned) {  \
	new (ptr) Int##width{e};  \
      });  \
    }  \
  } \
  else if constexpr (Type == Value::Index<UInt##width##Vec>{}) {  \
    ZuSpan<uint8_t> varBuf(&varBuf_[varBufPart.p<0>()], varBufPart.p<1>());  \
    new (ptr) UInt##width##Vec{ZuBSpan(varBuf)};  \
    auto v = vector<uint##width##_t, ZfFieldTC::UInt##width##Vec>(fbo, field);  \
    unsigned n = v.length();  \
    vecInit(varBuf, oids.oid(Value::Index<UInt##width>{}), n);  \
    for (unsigned i = 0; i < n; i++) {  \
      auto e = v[i];  \
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
    auto v = vector<int128_t, ZfFieldTC::Int128Vec>(fbo, field);
    unsigned n = v.length();
    vecInit(varBuf, oids.oid(Value::Index<Int128>{}), n);
    for (unsigned i = 0; i < n; i++) {
      auto e = v[i];
      vecAppend(varBuf, sizeof(Int128), [e](uint8_t *ptr, unsigned) {
	new (ptr) Int128{e};
      });
    }
  } else if constexpr (Type == Value::Index<UInt128Vec>{}) {
    ZuSpan<uint8_t> varBuf(&varBuf_[varBufPart.p<0>()], varBufPart.p<1>());
    new (ptr) UInt128Vec{ZuBSpan(varBuf)};
    auto v = vector<uint128_t, ZfFieldTC::UInt128Vec>(fbo, field);
    unsigned n = v.length();
    vecInit(varBuf, oids.oid(Value::Index<UInt128>{}), n);
    for (unsigned i = 0; i < n; i++) {
      auto e = v[i];
      vecAppend(varBuf, sizeof(UInt128), [e](uint8_t *ptr, unsigned) {
	new (ptr) UInt128{e};
      });
    }
  } else if constexpr (Type == Value::Index<FloatVec>{}) {
    ZuSpan<uint8_t> varBuf(&varBuf_[varBufPart.p<0>()], varBufPart.p<1>());
    new (ptr) FloatVec{ZuBSpan(varBuf)};
    auto encode = [&varBuf, &oids, field, fbo]<typename T>() {
      auto v = vector<T, ZfFieldTC::FloatVec>(fbo, field);
      unsigned n = v.length();
      vecInit(varBuf, oids.oid(Value::Index<Float>{}), n);
      for (unsigned i = 0; i < n; i++) {
	auto e = v[i];
	vecAppend(varBuf, sizeof(Float), [e](uint8_t *out, unsigned) {
	  new (out) Float{double(e)};
	});
      }
    };
    if (R::type(field).get<R::ZfField(Type, element)>() == R::BaseType::Float)
      encode.template operator ()<float>();
    else
      encode.template operator ()<double>();
  } else if constexpr (Type == Value::Index<FixedVec>{}) {
    ZuSpan<uint8_t> varBuf(&varBuf_[varBufPart.p<0>()], varBufPart.p<1>());
    new (ptr) FixedVec{ZuBSpan(varBuf)};
    auto v = vector<ZuFixed, ZfFieldTC::FixedVec>(fbo, field);
    unsigned n = v.length();
    vecInit(varBuf, oids.oid(Value::Index<Fixed>{}), n);
    for (unsigned i = 0; i < n; i++) {
      auto e = v[i];
      vecAppend(varBuf, sizeof(Fixed), [e](uint8_t *ptr, unsigned) {
	new (ptr) Fixed{ZuDecimal{e.decimal()}.value};
      });
    }
  } else if constexpr (Type == Value::Index<DecimalVec>{}) {
    ZuSpan<uint8_t> varBuf(&varBuf_[varBufPart.p<0>()], varBufPart.p<1>());
    new (ptr) DecimalVec{ZuBSpan(varBuf)};
    auto v = vector<ZuDecimal, ZfFieldTC::DecimalVec>(fbo, field);
    unsigned n = v.length();
    vecInit(varBuf, oids.oid(Value::Index<Decimal>{}), n);
    for (unsigned i = 0; i < n; i++) {
      auto e = v[i];
      vecAppend(varBuf, sizeof(Decimal), [e](uint8_t *ptr, unsigned) {
	new (ptr) Decimal{e.value};
      });
    }
  } else if constexpr (Type == Value::Index<TimeVec>{}) {
    ZuSpan<uint8_t> varBuf(&varBuf_[varBufPart.p<0>()], varBufPart.p<1>());
    new (ptr) TimeVec{ZuBSpan(varBuf)};
    auto v = vector<ZuTime, ZfFieldTC::TimeVec>(fbo, field);
    unsigned n = v.length();
    vecInit(varBuf, oids.oid(Value::Index<Time>{}), n);
    for (unsigned i = 0; i < n; i++) {
      auto e = v[i];
      vecAppend(varBuf, sizeof(Time), [e](uint8_t *ptr, unsigned) {
	new (ptr) Time{e.sec(), e.nsec()};
      });
    }
  } else if constexpr (Type == Value::Index<DateTimeVec>{}) {
    ZuSpan<uint8_t> varBuf(&varBuf_[varBufPart.p<0>()], varBufPart.p<1>());
    new (ptr) DateTimeVec{ZuBSpan(varBuf)};
    auto v = vector<ZuDateTime, ZfFieldTC::DateTimeVec>(fbo, field);
    unsigned n = v.length();
    vecInit(varBuf, oids.oid(Value::Index<DateTime>{}), n);
    for (unsigned i = 0; i < n; i++) {
      auto e = v[i];
      vecAppend(varBuf, sizeof(DateTime), [e](uint8_t *ptr, unsigned) {
	auto t = e.as_time();
	new (ptr) DateTime{t.sec(), t.nsec()};
      });
    }
  }
}

// --- save value to flatbuffer (for passing to Zdb/app)

using Offset = ZiFB::Offset;
using Builder = ZiFB::IOBuilder<ZiFB::Wire::MaxAlign>;

struct SavedOffset { Offset offset; uint8_t ipType; };
struct Offsets {
  SavedOffset *data;
  unsigned in = 0;
  mutable unsigned out = 0;

  Offsets(SavedOffset *data_) : data{data_} { }
  void push(Offset o, uint8_t tag = 0) { data[in++] = {o, tag}; }
  SavedOffset shift() const { return data[out++]; }
};

// PostgreSQL endian/layout conversion is separate from FlatBuffers layout.
template <typename P> struct Native;
template <> struct Native<Bool> {
  using T = bool;
  enum { Code = ZfFieldTC::Bool };
  static T load(const Bool &v) { return bool(v.v); }
};
template <> struct Native<Int8> {
  using T = int8_t;
  enum { Code = ZfFieldTC::Int8 };
  static T load(const Int8 &v) { return int8_t(v.v); }
};
template <> struct Native<UInt8> {
  using T = uint8_t;
  enum { Code = ZfFieldTC::UInt8 };
  static T load(const UInt8 &v) { return uint8_t(v.v); }
};
template <> struct Native<Int16> {
  using T = int16_t;
  enum { Code = ZfFieldTC::Int16 };
  static T load(const Int16 &v) { return int16_t(v.v); }
};
template <> struct Native<UInt16> {
  using T = uint16_t;
  enum { Code = ZfFieldTC::UInt16 };
  static T load(const UInt16 &v) { return uint16_t(v.v); }
};
template <> struct Native<Int32> {
  using T = int32_t;
  enum { Code = ZfFieldTC::Int32 };
  static T load(const Int32 &v) { return int32_t(v.v); }
};
template <> struct Native<UInt32> {
  using T = uint32_t;
  enum { Code = ZfFieldTC::UInt32 };
  static T load(const UInt32 &v) { return uint32_t(v.v); }
};
template <> struct Native<Int64> {
  using T = int64_t;
  enum { Code = ZfFieldTC::Int64 };
  static T load(const Int64 &v) { return int64_t(v.v); }
};
template <> struct Native<UInt64> {
  using T = uint64_t;
  enum { Code = ZfFieldTC::UInt64 };
  static T load(const UInt64 &v) { return uint64_t(v.v); }
};
template <> struct Native<Int128> {
  using T = int128_t;
  enum { Code = ZfFieldTC::Int128 };
  static T load(const Int128 &v) { return int128_t(v.v); }
};
template <> struct Native<UInt128> {
  using T = uint128_t;
  enum { Code = ZfFieldTC::UInt128 };
  static T load(const UInt128 &v) { return uint128_t(v.v); }
};
template <> struct Native<Float> {
  using T = double;
  enum { Code = ZfFieldTC::Float };
  static T load(const Float &v) { return double(v.v); }
};
template <> struct Native<Fixed> {
  using T = ZuFixed;
  enum { Code = ZfFieldTC::Fixed };
  static T load(const Fixed &v) { return ZuFixed{ZuDecimal{ZuDecimal::Unscaled{v.v}}}; }
};
template <> struct Native<Decimal> {
  using T = ZuDecimal;
  enum { Code = ZfFieldTC::Decimal };
  static T load(const Decimal &v) { return ZuDecimal{ZuDecimal::Unscaled{v.v}}; }
};
template <> struct Native<Time> {
  using T = ZuTime;
  enum { Code = ZfFieldTC::Time };
  static T load(const Time &v) { return ZuTime{v.sec, v.nsec}; }
};
template <> struct Native<DateTime> {
  using T = ZuDateTime;
  enum { Code = ZfFieldTC::DateTime };
  static T load(const DateTime &v) { return ZuDateTime{ZuTime{v.sec, v.nsec}}; }
};

template <typename P, typename T = typename Native<P>::T>
Offset saveArray(Builder &fbb, unsigned align, ZuBSpan bytes) {
  using N = Native<P>;
  using W = ZiFB::Value<T, N::Code>;
  int count = validateVecHdr(vecHdr(bytes));
  if (count < 0) { fbb.fail(ZiFB::Error::Capacity); return 0; }
  return fbb.vector(count, W::Size, (align > W::Align ? align : W::Align),
    [&bytes, count](uint8_t *out) {
      for (int i = 0; i < count; ++i) {
	auto value = vecElem(bytes, [](const uint8_t *p, unsigned) {
	  return N::load(*reinterpret_cast<const P *>(p));
	});
	if constexpr (W::Kind == ZiFB::ValueKind::Scalar)
	  ZiFB::write<typename W::WireType>(out + i * W::Size, value);
	else
	  ZiFB::save(out + i * W::Size, W::toWire(value));
      }
    });
}

template <unsigned Type>
void saveOffset(Builder &fbb, Offsets &offsets,
  R::FieldView field, unsigned align, const Value &value) {
  if constexpr (Type == Value::Index<String>{})
    offsets.push(fbb.string(value.p<Type>()));
  else if constexpr (Type == Value::Index<Bytes>{})
    offsets.push(fbb.copyRoot(value.p<Type>(), align));
  else if constexpr (Type == Value::Index<Bitmap>{}) {
    using Word = ZuBigEndian<uint64_t>;
    const auto &bytes = value.p<Type>().v;
    ZuSpan<const Word> words(reinterpret_cast<const Word *>(bytes.data()),
      bytes.length()/sizeof(uint64_t));
    unsigned n = words.length() - 1;
    auto data = fbb.vector(n, sizeof(uint64_t), alignof(uint64_t),
      [words, n](uint8_t *out) {
	for (unsigned i = 0; i < n; ++i)
	  ZiFB::write<uint64_t>(out + i * sizeof(uint64_t), words[i + 1]);
      });
    auto table = fbb.startTable();
    ZiFB::As<ZiFB::Values::Bitmap>::template Handler<ZiFB::Values::Bitmap>::template fieldOffset<ZiFB::Values::ZfField(Bitmap, data)>(fbb, table, data);
    offsets.push(fbb.endTable(table));
  } else if constexpr (Type == Value::Index<IP>{}) {
    auto saved = ZiFB::save<ZuFacet::FB, ZfFieldFilter::All>(
      fbb, value.p<Type>().ziIP());
    offsets.push(saved.offset, saved.tag);
  } else if constexpr (Type == Value::Index<StringVec>{} || Type == Value::Index<BytesVec>{}) {
    auto bytes = value.p<Type>().v;
    int count = validateVecHdr(vecHdr(bytes));
    if (count < 0) { fbb.fail(ZiFB::Error::Capacity); offsets.push(0); return; }
    auto elements = ZtScratch(ZiFB::Offsets, count, count);
    for (int i = 0; i < count; ++i) {
      auto data = vecElem(bytes, [](const uint8_t *p, unsigned n) { return ZuBSpan{p, n}; });
      if constexpr (Type == Value::Index<StringVec>{})
	elements[i] = fbb.string(data);
      else
	elements[i] = ZiFB::save(fbb, ZiFB::Values::Bytes{data});
    }
    fbb.align(align, count * sizeof(Offset));
    offsets.push(fbb.offsets(elements));
  }
  else if constexpr (Type == Value::Index<Int8Vec>{})
    offsets.push(saveArray<Int8>(fbb, align, value.p<Type>().v));
  else if constexpr (Type == Value::Index<UInt8Vec>{})
    offsets.push(saveArray<UInt8>(fbb, align, value.p<Type>().v));
  else if constexpr (Type == Value::Index<Int16Vec>{})
    offsets.push(saveArray<Int16>(fbb, align, value.p<Type>().v));
  else if constexpr (Type == Value::Index<UInt16Vec>{})
    offsets.push(saveArray<UInt16>(fbb, align, value.p<Type>().v));
  else if constexpr (Type == Value::Index<Int32Vec>{})
    offsets.push(saveArray<Int32>(fbb, align, value.p<Type>().v));
  else if constexpr (Type == Value::Index<UInt32Vec>{})
    offsets.push(saveArray<UInt32>(fbb, align, value.p<Type>().v));
  else if constexpr (Type == Value::Index<Int64Vec>{})
    offsets.push(saveArray<Int64>(fbb, align, value.p<Type>().v));
  else if constexpr (Type == Value::Index<UInt64Vec>{})
    offsets.push(saveArray<UInt64>(fbb, align, value.p<Type>().v));
  else if constexpr (Type == Value::Index<Int128Vec>{})
    offsets.push(saveArray<Int128>(fbb, align, value.p<Type>().v));
  else if constexpr (Type == Value::Index<UInt128Vec>{})
    offsets.push(saveArray<UInt128>(fbb, align, value.p<Type>().v));
  else if constexpr (Type == Value::Index<FloatVec>{}) {
    if (R::type(field).get<R::ZfField(Type, element)>() == R::BaseType::Float)
      offsets.push(saveArray<Float, float>(fbb, align, value.p<Type>().v));
    else
      offsets.push(saveArray<Float>(fbb, align, value.p<Type>().v));
  }
  else if constexpr (Type == Value::Index<FixedVec>{})
    offsets.push(saveArray<Fixed>(fbb, align, value.p<Type>().v));
  else if constexpr (Type == Value::Index<DecimalVec>{})
    offsets.push(saveArray<Decimal>(fbb, align, value.p<Type>().v));
  else if constexpr (Type == Value::Index<TimeVec>{})
    offsets.push(saveArray<Time>(fbb, align, value.p<Type>().v));
  else if constexpr (Type == Value::Index<DateTimeVec>{})
    offsets.push(saveArray<DateTime>(fbb, align, value.p<Type>().v));
}

template <unsigned Type>
void saveValue(Builder &fbb, ZiFB::TableState &table, const Offsets &offsets,
    R::FieldView field, unsigned align, const Value &value) {
  if constexpr (!Type) { }
  else if constexpr (Type == Value::Index<String>{} || Type == Value::Index<Bytes>{} ||
      isVar(Type))
    R::saveOffset(fbb, table, field, offsets.shift().offset);
  else if constexpr (Type == Value::Index<IP>{}) {
    auto saved = offsets.shift();
    R::saveUnion(fbb, table, field, saved.ipType, saved.offset);
  } else if constexpr (Type == Value::Index<Float>{}) {
    double native = Native<Float>::load(value.p<Type>());
    if (R::type(field).get<R::ZfField(Type, baseType)>() == R::BaseType::Float)
      R::saveScalar<float>(fbb, table, field, float(native));
    else
      R::saveScalar<double>(fbb, table, field, native);
  } else {
    using N = Native<typename Value::template Type<Type>>;
    using W = ZiFB::Value<typename N::T, N::Code>;
    auto native = N::load(value.p<Type>());
    if constexpr (W::Kind == ZiFB::ValueKind::Scalar)
      R::saveScalar<typename W::WireType>(fbb, table, field, native);
    else {
      fbb.fieldInline(table, field.get<R::ZfField(Field, id)>(), W::Size,
	(align > W::Align ? align : W::Align),
	[&native](uint8_t *out) { ZiFB::save(out, W::toWire(native)); });
    }
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

// Plan every parameter before allocating/encoding aggregate conversion scratch.
// Strings/bytes stay borrowed, but obey the same parameter-buffer cap.
template <typename Parts, typename Fields>
unsigned planTuple(
  Parts &parts, unsigned nParams,
  const XFields &xFields, const Fields &fields, ZiFB::TableView fbo)
{
  unsigned total = 0;
  for (unsigned i = 0; i < nParams; ++i) {
    const auto &field = xFieldAt(xFields, fields, i);
    switch (field.type) {
      case Value::Index<String>{}:
	if (R::string(fbo, field.field).length() > Zdb_::MaxBufSize) return UINT_MAX;
	continue;
      case Value::Index<Bytes>{}:
	if (R::vector(fbo, field.field).bytes().length() > Zdb_::MaxBufSize)
	  return UINT_MAX;
	continue;
      default: break;
    }
    if (!isVar(field.type)) continue;
    unsigned size = ZuSwitch::dispatch<Value::N>(field.type,
      [fbField = field.field, fbo](auto Type) {
	return varBufSize<Type>(fbField, fbo);
      });
    unsigned end = sizeAdd(total, size);
    if (end == UINT_MAX) return UINT_MAX;
    parts.push(VarBufPart{unsigned(total), size});
    total = end;
  }
  return unsigned(total);
}

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
  ZiFB::TableView fbo)
{
  unsigned j = 0;
  for (unsigned i = 0; i < nParams; i++) {
    auto value = static_cast<Value *>(tuple.push());
    const auto &xField = xFieldAt(xFields, fields, i);
    auto type = xField.type;
    if (!isVar(type))
      ZuSwitch::dispatch<Value_::N>(type, [
	value, field = xField.field, native = xField.vfield, fbo
      ](auto I) {
	loadValue<I>(value->new_<I, true>(), field, fbo, native);
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
  Builder &fbb,
  const XFields &xFields, const Fields &fields,
  unsigned nOffsets,
  ZuSpan<const Value> tuple)
{
  unsigned n = fields.length();
  ZmAssert(tuple.length() == n);
  auto offsets_ = ZtScratch(SavedOffsets, nOffsets, nOffsets);
  Offsets offsets(offsets_.data());
  for (unsigned i = 0; i < n; i++) {
    const auto &xField = xFieldAt(xFields, fields, i);
    auto type = xField.type;
    const auto &value = tuple[i];
    ZuSwitch::dispatch<Value::N>(type,
      [&fbb, &offsets, &xField, &value](auto I) {
	saveOffset<I>(fbb, offsets, xField.field, xField.align, value);
      });
  }
  auto table = fbb.startTable();
  for (unsigned i = 0; i < n; i++) {
    const auto &xField = xFieldAt(xFields, fields, i);
    auto type = xField.type;
    const auto &value = tuple[i];
    ZuSwitch::dispatch<Value::N>(type,
      [&fbb, &table, &offsets, &xField, &value](auto I) {
	saveValue<I>(fbb, table, offsets, xField.field, xField.align, value);
      });
  }
  return fbb.endTable(table);
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
    Schema schema, IOBufAllocFn bufAllocFn);

  // Closed store tables may outlive the previous BFBS owner.
  void schema(Schema schema) {
    m_rowAlign = ZiFB::Wire::Align;
    for (auto &field: m_xFields) {
      field.field = schema.field(field.vfield->id);
      field.align = R::alignment(field.field);
      field.bufAlign = R::bufferAlign(schema.view, field.field);
      if (m_rowAlign < field.bufAlign) m_rowAlign = field.bufAlign;
    }
    for (auto &key: m_xKeys) {
      key.bufAlign = ZiFB::Wire::Align;
      for (const auto &field: key.fields)
	if (key.bufAlign < m_xFields[field.field].bufAlign)
	  key.bufAlign = m_xFields[field.field].bufAlign;
    }
  }
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
    ZuSpan<const Value> tuple, const Fields &fields, unsigned nOffsets, unsigned align);
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
  unsigned		m_rowAlign = ZiFB::Wire::Align;
  ZuPtr<FieldMap>	m_fieldMap;
  IndexStates		m_indexStates;
  ZuPtr<IndexMap>	m_indexMap;
  MissingIndices	m_missingIndices;
  uint16_t		m_missingPos = 0;
  IOBufAllocFn		m_bufAllocFn;
  bool			m_internal;

  OpenState		m_openState;
  OpenFn		m_openFn;	// open callback
  unsigned		m_badField = ZuCmp<unsigned>::null();

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
  (thread, (Mutable, Required),		String),
  (connect, (Mutable, Required),	String),
  (replicated, (Mutable),		Bool));

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
    Schema schema,
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
