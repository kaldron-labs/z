//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Native Protocol Buffers wire primitives and facet configuration.

#ifndef Zpb_HH
#define Zpb_HH

#ifndef ZpbLib_HH
#include <zlib/ZpbLib.hh>
#endif

#include <limits.h>
#include <stdint.h>
#include <string.h>

#include <zlib/ZuAssert.hh>
#include <zlib/ZuArray.hh>
#include <zlib/ZuBitmap.hh>
#include <zlib/ZuByteSwap.hh>
#include <zlib/ZuCmp.hh>
#include <zlib/ZuIntrin.hh>
#include <zlib/ZuSeq.hh>
#include <zlib/ZuSpan.hh>
#include <zlib/ZuString.hh>
#include <zlib/ZuStruct.hh>
#include <zlib/ZuSwitch.hh>
#include <zlib/ZuUnion.hh>
#include <zlib/ZuTL.hh>
#include <zlib/ZuTraits.hh>
#include <zlib/ZuTuple.hh>
#include <zlib/ZuUnroll.hh>

#include <zlib/ZmAssert.hh>
#include <zlib/ZmBitmap.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmRef.hh>

#include <zlib/ZtArray.hh>
#include <zlib/ZtBitmap.hh>
#include <zlib/ZtEnum.hh>
#include <zlib/ZtScratch.hh>
#include <zlib/ZtString.hh>

#include <zlib/ZfStruct.hh>

#include <zlib/ZiIOBuf.hh>
#include <zlib/ZiIP.hh>
#include <zlib/ZiTxStream.hh>

ZuStructFacet(PB);

struct Zpb_DefltConfig {
  enum { SizePrefixed = false };
};

template <bool _, typename NTP = Zpb_DefltConfig>
struct Zpb_SizePrefixed_ : public NTP { enum { SizePrefixed = _ }; };
template <typename NTP = Zpb_DefltConfig>
using Zpb_SizePrefixed = Zpb_SizePrefixed_<true, NTP>;

Zpb_DefltConfig Zpb_Config(...);

#define ZpbConfig(Facet, Config) \
  namespace ZuFacet { ZuPP_Strip(Config) Zpb_Config(Facet *); }

namespace ZuFieldProp::PB {

template <unsigned N> struct FieldID { };

template <ZuString Name> struct ID { };

struct WireWord { };
struct ZigZag { };
struct Fixed { };
struct BoolVec { };
struct Packed { };

} // ZuFieldProp::PB

namespace Zpb {

template <typename Facet>
using Config = decltype(Zpb_Config(ZuDeclVal<Facet *>()));

namespace Wire {

ZtEnum(ZpbAPI, WireType, int8_t,
  Varint, Fixed64, Length, StartGroup, EndGroup, Fixed32);

// Field-number limits are imposed by the protobuf wire format.
constexpr uint32_t MaxField = (uint32_t(1) << 29) - 1;
constexpr uint32_t ReservedBegin = 19000;
constexpr uint32_t ReservedEnd = 19999;

constexpr bool fieldNumber(uint32_t n) {
  return n && n <= MaxField && (n < ReservedBegin || n > ReservedEnd);
}

constexpr unsigned varintSize(uint64_t n) {
  unsigned size = 1;
  while (n >= 0x80) { n >>= 7; ++size; }
  return size;
}

// The caller reserves varintSize(n) bytes preceding end. The result is the
// start of the encoded varint; bytes remain in ordinary protobuf order.
ZpbAPI uint8_t *prependVarint(uint8_t *end, uint64_t n);

// Reader functions consume only their own value. Validation of a complete
// message, including field semantics, is a separate operation.
ZpbAPI bool readVarint(ZuBSpan bytes, unsigned &pos, uint64_t &value);
ZpbAPI bool readTag(ZuBSpan bytes, unsigned &pos, uint32_t &field, T &type);
ZpbAPI bool readLength(ZuBSpan bytes, unsigned &pos, ZuBSpan &value);
ZpbAPI bool skip(ZuBSpan bytes, unsigned &pos, uint32_t field, T type,
  unsigned maxDepth = UINT_MAX);
ZpbAPI bool unframe(ZuBSpan frame, ZuBSpan &payload);

} // Wire

namespace Error { enum { OK, Capacity, Limit, Null, Required, Invalid }; }

struct VerifyLimits { unsigned maxDepth = 64; };

ZuDerive(CStringArg, (ZtString<ZtStringHeapID<"Zpb.CString">>));
ZuDerive(CStringVecArg,
  (ZtArray<CStringArg, ZtArrayHeapID<"Zpb.CStringVec">>));

template <typename T, bool = ZuTraits<T>::IsPointer>
struct ObjectType_ { using T_ = T; };
template <typename T>
struct ObjectType_<T, true> { using T_ = typename ZuTraits<T>::Elem; };
template <typename T> using ObjectType = typename ObjectType_<T>::T_;

// Storage grows independently of the builder. Both policies keep the wire
// suffix at the end of the allocation, so offsets remain end-relative.
class SpanStorage {
public:
  explicit SpanStorage(ZuSpan<uint8_t> span) : m_span{span} { }
  uint8_t *data() { return m_span.data(); }
  unsigned capacity() const { return m_span.length(); }
  bool grow(unsigned, unsigned) { return false; }
private:
  ZuSpan<uint8_t>	m_span;
};

template <typename Array>
class ArrayStorage {
public:
  explicit ArrayStorage(Array &array) : m_array{array} { m_array.clear(); }
  uint8_t *data() { return m_array.data(); }
  unsigned capacity() const { return m_array.size(); }
  bool grow(unsigned capacity, unsigned used) {
    Array next;
    next.length(capacity);
    if (used)
      memcpy(next.data() + capacity - used,
	m_array.data() + m_array.size() - used, used);
    m_array = ZuMv(next);
    return true;
  }
private:
  Array	&m_array;
};

inline unsigned ioGrow_(unsigned, unsigned n) { return n; }

class IOStorage {
public:
  explicit IOStorage(ZmRef<ZiIOBuf> buf, unsigned maxSize = UINT32_MAX,
    unsigned tailRoom = 0) :
    m_maxSize{maxSize}, m_tailRoom{tailRoom} { attach(ZuMv(buf)); }

  uint8_t *data() { return m_buf ? m_buf->data() : nullptr; }
  unsigned capacity() const {
    if (!m_buf) return 0;
    unsigned end = m_buf->size < m_maxSize ? m_buf->size : m_maxSize;
    uint64_t reserve = uint64_t(m_buf->skip) + m_tailRoom;
    return end > reserve ? end - reserve : 0;
  }
  unsigned limit() const {
    if (!m_buf) return 0;
    unsigned end = m_maxSize < INT_MAX ? m_maxSize : unsigned(INT_MAX);
    uint64_t reserve = uint64_t(m_buf->skip) + m_tailRoom;
    return end > reserve ? end - reserve : 0;
  }
  bool grow(unsigned capacity, unsigned used) {
    if (!m_buf || capacity > limit()) return false;
    unsigned skip = m_buf->skip;
    unsigned oldSize = m_buf->size;
    uint64_t nextSize = uint64_t(skip) + capacity + m_tailRoom;
    if (nextSize > UINT32_MAX) return false;
    m_buf->skip = 0;
    unsigned tail = oldSize >= uint64_t(skip) + m_tailRoom ?
      used + m_tailRoom : used;
    m_buf->template realloc<ioGrow_>(oldSize, unsigned(nextSize), skip, tail);
    m_buf->skip = skip;
    return true;
  }
  void attach(ZmRef<ZiIOBuf> buf) {
    if (buf) {
      ZmAssert(buf->skip <= buf->size);
      buf->length = 0;
    }
    m_buf = ZuMv(buf);
  }
  ZmRef<ZiIOBuf> detach(unsigned capacity, unsigned length) {
    if (!m_buf) return nullptr;
    auto buf = ZuMv(m_buf);
    buf->skip += capacity - length;
    buf->length = length;
    return buf;
  }
  const ZiIOBuf *buffer() const { return m_buf.ptr(); }

private:
  ZmRef<ZiIOBuf>	m_buf;
  unsigned		m_maxSize;
  unsigned		m_tailRoom;
};

template <typename Storage>
class Builder {
public:
  explicit Builder(Storage &storage, unsigned limit = INT_MAX) :
    m_storage{storage}, m_limit{limit < INT_MAX ? limit : unsigned(INT_MAX)} { }

  bool failed() const { return m_error != Error::OK; }
  int error() const { return m_error; }
  void reset() { m_used = 0; m_error = Error::OK; }
  unsigned size() const { return m_used; }
  unsigned capacity() const { return m_storage.capacity(); }
  uint8_t *data() {
    auto p = m_storage.data();
    return p ? p + capacity() - m_used : nullptr;
  }
  ZuBSpan bytes() { return {data(), m_used}; }

  void fail(int error) { if (!failed()) m_error = error; }
  uint8_t *push(unsigned n) {
    if (failed()) return nullptr;
    if (n > m_limit - m_used) { fail(Error::Limit); return nullptr; }
    unsigned need = m_used + n;
    if (need > capacity()) {
      unsigned next = capacity();
      unsigned extra = next >> 1;
      if (extra < n) extra = n;
      if (next > INT_MAX - extra) next = INT_MAX;
      else next += extra;
      if (next > m_limit) next = m_limit;
      if (next < need || !m_storage.grow(next, m_used)) {
	fail(Error::Capacity);
	return nullptr;
      }
    }
    m_used = need;
    return data();
  }
  void varint(uint64_t n) {
    unsigned size = Wire::varintSize(n);
    if (auto p = push(size)) Wire::prependVarint(p + size, n);
  }
  void bytes(ZuBSpan value) {
    if (auto p = push(value.length()))
      if (value.length()) memcpy(p, value.data(), value.length());
  }
  void tag(uint32_t field, Wire::T type) {
    varint((uint64_t(field) << 3) | type);
  }
  void fixed32(uint32_t n) {
    if (auto p = push(sizeof(n))) {
      ZuLittleEndian<uint32_t> wire{n};
      memcpy(p, &wire, sizeof(wire));
    }
  }
  void fixed64(uint64_t n) {
    if (auto p = push(sizeof(n))) {
      ZuLittleEndian<uint64_t> wire{n};
      memcpy(p, &wire, sizeof(wire));
    }
  }

private:
  Storage	&m_storage;
  unsigned	m_limit;
  unsigned	m_used = 0;
  int		m_error = Error::OK;
};

class IOBuilder : private IOStorage, public Builder<IOStorage> {
  using Base = Builder<IOStorage>;
public:
  explicit IOBuilder(ZmRef<ZiIOBuf> buf, unsigned maxSize = UINT32_MAX,
    unsigned tailRoom = 0) :
    IOStorage{ZuMv(buf), maxSize, tailRoom}, Base{*this, IOStorage::limit()} { }

  ZmRef<ZiIOBuf> buf() {
    if (failed()) return nullptr;
    auto result = IOStorage::detach(capacity(), size());
    reset();
    return result;
  }
  const ZiIOBuf *buffer() const { return IOStorage::buffer(); }

  using Base::bytes;
  using Base::capacity;
  using Base::data;
  using Base::error;
  using Base::failed;
  using Base::size;
};

template <int32_t Start, int32_t End> struct ReserveRange {
  enum { IsRange = true, Start_ = Start, End_ = End };
};
template <ZuString Name> struct ReserveName {
  enum { IsRange = false };
  static constexpr auto value = Name;
};
template <ZuString Name, typename ...Meta>
struct AsMessage {
  static constexpr auto name() { return Name; }
  using Reserved = ZuTypeList<Meta...>;
  enum { Repeated = false, Union = false };
  template <typename O, typename Facet = ZuFacet::PB> class Handler;
};

template <ZuString Name>
struct AsArray {
  static constexpr auto name() { return Name; }
  enum { Repeated = true, Union = false };
  template <typename O, typename Facet = ZuFacet::PB> struct Handler;
};

template <ZuString Name, unsigned KeyCode, unsigned ValCode,
  typename KeyProps = ZuTypeList<>, typename ValProps = ZuTypeList<>>
struct AsMap {
  static constexpr auto name() { return Name; }
  enum { Repeated = true, Union = false, KC = KeyCode, VC = ValCode };
  using KP = KeyProps;
  using VP = ValProps;
  ZuAssert(KC == ZfFieldTC::Bool ||
    (KC >= ZfFieldTC::Int8 && KC <= ZfFieldTC::UInt64) ||
    KC == ZfFieldTC::CString || KC == ZfFieldTC::String);
  template <typename O, typename Facet = ZuFacet::PB> struct Handler;
};

template <ZuString Name, unsigned Number, typename T_>
struct Alt {
  using T = T_;
  enum { Field = Number };
  static constexpr auto name() { return Name; }
};

template <ZuString Name, typename ...Alts>
struct AsUnion {
  static constexpr auto name() { return Name; }
  enum { Repeated = false, Union = true };
  using Types = ZuTypeList<Alts...>;
  template <typename O, typename Facet = ZuFacet::PB> struct Handler;
};

namespace Values {

struct Int128 { uint64_t h, l; };
ZfStruct(ZpbAPI, (Int128, PB),
  (h, (PB::FieldID<1>, PB::WireWord),	UInt64),
  (l, (PB::FieldID<2>, PB::WireWord),	UInt64));
AsMessage<"Zf.Int128"> Zpb_Fmt(Int128 *);

struct UInt128 { uint64_t h, l; };
ZfStruct(ZpbAPI, (UInt128, PB),
  (h, (PB::FieldID<1>, PB::WireWord),	UInt64),
  (l, (PB::FieldID<2>, PB::WireWord),	UInt64));
AsMessage<"Zf.UInt128"> Zpb_Fmt(UInt128 *);

struct Fixed { int64_t mantissa; uint8_t ndp; };
ZfStruct(ZpbAPI, (Fixed, PB),
  (mantissa, (PB::FieldID<1>),	Int64),
  (ndp, (PB::FieldID<2>),	UInt8));
AsMessage<"Zf.Fixed"> Zpb_Fmt(Fixed *);

struct Decimal { uint64_t h, l; };
ZfStruct(ZpbAPI, (Decimal, PB),
  (h, (PB::FieldID<1>, PB::WireWord),	UInt64),
  (l, (PB::FieldID<2>, PB::WireWord),	UInt64));
AsMessage<"Zf.Decimal"> Zpb_Fmt(Decimal *);

struct Time { int64_t sec; int32_t nsec; };
ZfStruct(ZpbAPI, (Time, PB),
  (sec, (PB::FieldID<1>),	Int64),
  (nsec, (PB::FieldID<2>),	Int32));
AsMessage<"Zf.Time"> Zpb_Fmt(Time *);

struct DateTime { int32_t julian, sec, nsec; };
ZfStruct(ZpbAPI, (DateTime, PB),
  (julian, (PB::FieldID<1>),	Int32),
  (sec, (PB::FieldID<2>),	Int32),
  (nsec, (PB::FieldID<3>),	Int32));
AsMessage<"Zf.DateTime"> Zpb_Fmt(DateTime *);

struct IP { ZuBSpan addr; };
ZfStruct(ZpbAPI, (IP, PB), (addr, (PB::FieldID<1>), Bytes));
AsMessage<"Zi.IP"> Zpb_Fmt(IP *);

struct Bitmap {
  ZuBSpan words;
#if Zu_BIGENDIAN
  using Bytes = ZtArray<uint8_t, ZtArrayHeapID<"Zpb.BitmapBytes">>;
  Bytes owned;
  Bitmap() = default;
  Bitmap(const Bitmap &o) : words{o.words}, owned{o.owned} { rebind(); }
  Bitmap(Bitmap &&o) : words{o.words}, owned{ZuMv(o.owned)} { rebind(); }
  Bitmap &operator =(const Bitmap &o) {
    if (this == &o) return *this;
    words = o.words;
    owned = o.owned;
    rebind();
    return *this;
  }
  Bitmap &operator =(Bitmap &&o) {
    if (this == &o) return *this;
    words = o.words;
    owned = ZuMv(o.owned);
    rebind();
    return *this;
  }
  void rebind() {
    if (owned.length()) words = {owned.data(), owned.length()};
  }
#endif
};
ZfStruct(ZpbAPI, (Bitmap, PB), (words, (PB::FieldID<1>), Bytes));
AsMessage<"Zf.Bitmap"> Zpb_Fmt(Bitmap *);

using HwBitmapText = ZtString<ZtStringHeapID<"Zpb.HwBitmapText">>;
struct HwBitmap { HwBitmapText ranges; };
ZfStruct(ZpbAPI, (HwBitmap, PB), (ranges, (PB::FieldID<1>), String));
AsMessage<"Zf.HwBitmap"> Zpb_Fmt(HwBitmap *);

struct Int128Map {
  using T = Int128;
  static T save(int128_t v) { return {uint64_t(uint128_t(v) >> 64), uint64_t(v)}; }
  static int128_t load(const T &v) {
    return int128_t((uint128_t(v.h) << 64) | v.l);
  }
};
struct UInt128Map {
  using T = UInt128;
  static T save(uint128_t v) { return {uint64_t(v >> 64), uint64_t(v)}; }
  static uint128_t load(const T &v) { return (uint128_t(v.h) << 64) | v.l; }
};
struct FixedMap {
  using T = Fixed;
  static T save(const ZuFixed &v) { return {int64_t(v.mantissa), uint8_t(v.ndp)}; }
  static ZuFixed load(const T &v) { return {v.mantissa, v.ndp}; }
};
struct DecimalMap {
  using T = Decimal;
  static T save(const ZuDecimal &v) {
    return {uint64_t(uint128_t(v.value) >> 64), uint64_t(v.value)};
  }
  static ZuDecimal load(const T &v) {
    return ZuDecimal{ZuDecimal::Unscaled{int128_t((uint128_t(v.h) << 64) | v.l)}};
  }
};
struct TimeMap {
  using T = Time;
  static T save(const ZuTime &v) { return {v.sec(), int32_t(v.nsec())}; }
  static ZuTime load(const T &v) { return {v.sec, v.nsec}; }
};
struct DateTimeMap {
  using T = DateTime;
  static T save(const ZuDateTime &v) { return {v.julian(), v.sec(), v.nsec()}; }
  static ZuDateTime load(const T &v) {
    return ZuDateTime{ZuDateTime::Julian{v.julian}, v.sec, v.nsec};
  }
};
struct IPMap {
  using T = IP;
  static T save(const ZiIP &v) {
    switch (v.type()) {
      case ZiIP::V4:
	return {{reinterpret_cast<const uint8_t *>(&v.inAddr()), sizeof(in_addr)}};
      case ZiIP::V6:
	return {{v.in6Addr().s6_addr, sizeof(in6_addr)}};
      default:
	return {};
    }
  }
  static ZiIP load(const T &v) {
    switch (v.addr.length()) {
      case sizeof(in_addr): {
	in_addr addr;
	memcpy(&addr, v.addr.data(), sizeof(addr));
	return ZiIP{addr};
      }
      case sizeof(in6_addr): {
	in6_addr addr;
	memcpy(&addr, v.addr.data(), sizeof(addr));
	return ZiIP{addr};
      }
      default:
	return {};
    }
  }
};
template <typename B>
struct BitmapMap {
  using T = Bitmap;
  static T save(const B &v) {
    unsigned n = v.length() / 8;
    T wire;
#if Zu_BIGENDIAN
    if (n) {
      wire.owned.length(n);
      for (unsigned i = 0; i < n / sizeof(uint64_t); ++i) {
	uint64_t word = ZuLE(v.data[i]);
	memcpy(wire.owned.data() + i * sizeof(word), &word, sizeof(word));
      }
      wire.words = {wire.owned.data(), n};
    }
#else
    wire.words = {n ? reinterpret_cast<const uint8_t *>(&v.data[0]) : nullptr, n};
#endif
    return wire;
  }
  static B load(const T &v) {
    B out;
    unsigned n = v.words.length()/sizeof(uint64_t);
    if constexpr (B::Data::Fixed) {
      if (n > B::Data::Words) n = B::Data::Words;
    } else out.data.length(n);
    if (n) {
#if Zu_BIGENDIAN
      for (unsigned i = 0; i < n; ++i) {
	uint64_t word;
	memcpy(&word, v.words.data() + i * sizeof(word), sizeof(word));
	out.data[i] = ZuLE(word);
      }
#else
      memcpy(&out.data[0], v.words.data(), n * sizeof(uint64_t));
#endif
    }
    return out;
  }
};
struct HwBitmapMap {
  using T = HwBitmap;
  static T save(const ZmBitmap &v) {
    T wire;
    v.print(wire.ranges);
    return wire;
  }
  static ZmBitmap load(const T &v) {
    ZmBitmap out;
    if (v.ranges.length()) out.scan(v.ranges);
    return out;
  }
};

} // Values

template <unsigned Code> struct MappedCode : ZuBool<
  Code == ZfFieldTC::Int128 || Code == ZfFieldTC::UInt128 ||
  Code == ZfFieldTC::Fixed || Code == ZfFieldTC::Decimal ||
  Code == ZfFieldTC::Time || Code == ZfFieldTC::DateTime> { };

template <unsigned Code> struct MappedVecCode : ZuBool<
  Code == ZfFieldTC::Int128Vec || Code == ZfFieldTC::UInt128Vec ||
  Code == ZfFieldTC::FixedVec || Code == ZfFieldTC::DecimalVec ||
  Code == ZfFieldTC::TimeVec || Code == ZfFieldTC::DateTimeVec> { };

template <unsigned Code> struct Mapping;
#define Zpb_ValueMap(Code_) \
  template <> struct Mapping<ZfFieldTC::Code_> : Values::Code_##Map { }
Zpb_ValueMap(Int128);
Zpb_ValueMap(UInt128);
Zpb_ValueMap(Fixed);
Zpb_ValueMap(Decimal);
Zpb_ValueMap(Time);
Zpb_ValueMap(DateTime);
#undef Zpb_ValueMap

template <typename Map> struct AsMapped;

} // Zpb

void ZpbTransform(...);
Zpb::Values::Int128Map ZpbTransform(int128_t *);
Zpb::Values::UInt128Map ZpbTransform(uint128_t *);
Zpb::Values::FixedMap ZpbTransform(ZuFixed *);
Zpb::Values::DecimalMap ZpbTransform(ZuDecimal *);
Zpb::Values::TimeMap ZpbTransform(ZuTime *);
Zpb::Values::DateTimeMap ZpbTransform(ZuDateTime *);
Zpb::Values::IPMap ZpbTransform(ZiIP *);
namespace ZuBitmap_ {
template <unsigned Bits>
Zpb::Values::BitmapMap<Bitmap<Bits>> ZpbTransform(Bitmap<Bits> *);
} // ZuBitmap_
namespace ZtBitmap_ {
Zpb::Values::BitmapMap<Bitmap> ZpbTransform(Bitmap *);
} // ZtBitmap_
namespace ZmBitmap_ {
Zpb::Values::HwBitmapMap ZpbTransform(Bitmap *);
} // ZmBitmap_
void Zpb_Fmt(...);

namespace Zpb {

template <typename O>
using Transform = decltype(ZpbTransform(ZuDeclVal<ZuDecay<O> *>()));

template <typename O, typename Map = Transform<O>, typename = typename Map::T>
AsMapped<Map> Zpb_Fmt(O *);

template <typename O>
using As = decltype(Zpb_Fmt(ZuDeclVal<ZuUnder<O> *>()));

template <typename Map>
struct AsMapped {
  using Wire = typename Map::T;
  static constexpr auto name() { return As<Wire>::name(); }
  enum { Repeated = false, Union = false };
  template <typename O, typename Facet = ZuFacet::PB> class Handler;
};

ZuDerive(Seen, (ZtArray<uint8_t, ZtArrayHeapID<"Zpb.Seen">>));
struct Part {
  uint32_t field;
  ZuBSpan bytes;
};
ZuDerive(Parts, (ZtArray<Part, ZtArrayHeapID<"Zpb.Parts">>));

template <ZuString Name, typename ...Alts>
template <typename O, typename Facet>
struct AsUnion<Name, Alts...>::Handler {
  using Types = ZuTypeList<Alts...>;

  static unsigned altIndex(uint32_t field) {
    return ZuSwitch::dispatch<ZuSeq<Alts::Field...>>(field, [](auto id) {
      return unsigned(ZuTypeIndex<decltype(id),
	ZuTypeList<ZuUnsigned<Alts::Field>...>>{}) + 1;
    }, 0U);
  }

  template <unsigned I>
  using Alt_ = ZuType<I - 1, Types>;

  ZuAssert(O::N == Types::N + 1);
  ZuAssert((ZuIsSame<typename O::template Type<0>, void>{}));

  template <typename B>
  static void save(B &builder, const O &o) {
    unsigned tag = o.type();
    if (!tag) return;
    ZuSwitch::dispatch<Types::N>(tag - 1, [&builder, &o](auto i) {
      using A = ZuType<i, Types>;
      using T = typename A::T;
      unsigned before = builder.size();
      As<T>::template Handler<T, Facet>::save(builder, o.template p<i + 1>());
      if (builder.failed()) return;
      builder.varint(builder.size() - before);
      builder.tag(A::Field, Wire::Length);
    });
  }

  template <unsigned I>
  static void loadAlt(O &o, ZuBSpan raw) {
    using T = typename Alt_<I>::T;
    using H = typename As<T>::template Handler<T, Facet>;
    if (o.type() != I) o.template p<I>(H{raw}.ctor());
    else H{raw}.merge(o.template p<I>());
  }

  template <bool Required = true>
  static bool verifyParts(const Part *parts, unsigned n, unsigned depth) {
    if (!n || !depth) return !n;
    unsigned tag = 0, start = 0;
    for (unsigned i = 0; i < n; ++i) {
      unsigned index = altIndex(parts[i].field);
      if (index && tag != index) {
	if (tag && !ZuSwitch::dispatch<Types::N>(tag - 1, [&](auto j) {
	    using A = ZuType<j, Types>;
	    using T = typename A::T;
	    return As<T>::template Handler<T, Facet>::template verifyParts<false>(
	      parts + start, i - start, A::Field, depth - 1);
	  })) return false;
	tag = index;
	start = i;
      }
    }
    if (!tag) return true;
    return ZuSwitch::dispatch<Types::N>(tag - 1, [&](auto i) {
      using A = ZuType<i, Types>;
      using T = typename A::T;
      return As<T>::template Handler<T, Facet>::template verifyParts<Required>(
	parts + start, n - start, A::Field, depth - 1);
    });
  }
};

template <typename O_, typename T_, unsigned Code_, ZuString ID,
  typename Props_, auto Get>
struct MapField {
  using O = O_;
  using T = T_;
  using Props = Props_;
  using Orig = MapField;
  enum { Code = Code_, ReadOnly = false };
  template <template <typename> class Override> using Adapt = Override<Orig>;
  static constexpr auto id() { return ID; }
  static decltype(auto) get(const O &o) { return Get(o); }
  static decltype(auto) get(O &o) { return Get(o); }
  template <typename V> static void set(O &o, V &&value) {
    Get(o) = ZuFwd<V>(value);
  }
  static T deflt() { return T{}; }
};

template <typename Fmt, typename K, typename V>
struct MapEntry {
  K key{};
  V val{};
  using KF = MapField<MapEntry, K, Fmt::KC, "key",
    typename Fmt::KP::template Push<ZuFieldProp::PB::FieldID<1>>,
    [](auto &o) -> decltype(auto) { return (o.key); }>;
  using VF = MapField<MapEntry, V, Fmt::VC, "value",
    typename Fmt::VP::template Push<ZuFieldProp::PB::FieldID<2>>,
    [](auto &o) -> decltype(auto) { return (o.val); }>;
  friend MapEntry ZuStructured_(MapEntry *);
  template <typename Facet>
  friend ZuTypeList<KF, VF> ZuFields_(MapEntry *, Facet *);
  friend AsMessage<Fmt::name() + "Entry"_z> Zpb_Fmt(MapEntry *);
};

template <typename Fmt, typename K, typename V>
struct MapEntryView {
  const K &key;
  const V &val;
  using KF = MapField<MapEntryView, K, Fmt::KC, "key",
    typename Fmt::KP::template Push<ZuFieldProp::PB::FieldID<1>>,
    [](auto &o) -> decltype(auto) { return (o.key); }>;
  using VF = MapField<MapEntryView, V, Fmt::VC, "value",
    typename Fmt::VP::template Push<ZuFieldProp::PB::FieldID<2>>,
    [](auto &o) -> decltype(auto) { return (o.val); }>;
  friend MapEntryView ZuStructured_(MapEntryView *);
  template <typename Facet>
  friend ZuTypeList<KF, VF> ZuFields_(MapEntryView *, Facet *);
  friend AsMessage<Fmt::name() + "Entry"_z> Zpb_Fmt(MapEntryView *);
};

template <ZuString Name, unsigned KC, unsigned VC, typename KP, typename VP>
template <typename O, typename Facet>
struct AsMap<Name, KC, VC, KP, VP>::Handler {
  using Fmt = AsMap<Name, KC, VC, KP, VP>;
  using K = typename O::Key;
  using V = typename O::Val;
  using Entry = MapEntry<Fmt, K, V>;
  using View = MapEntryView<Fmt, K, V>;
  using EH = typename As<Entry>::template Handler<Entry, Facet>;
  using VH = typename As<View>::template Handler<View, Facet>;

  static unsigned count(const O &o) { return o.count_(); }

  template <typename B>
  static void save(B &builder, const O &o, unsigned field) {
    auto i = o.citer();
    while (auto node = i()) {
      View entry{node->key(), node->val()};
      unsigned before = builder.size();
      VH::save(builder, entry);
      if (builder.failed()) return;
      builder.varint(builder.size() - before);
      builder.tag(field, Wire::Length);
      if (builder.failed()) return;
    }
  }

  template <bool Required = true>
  static bool verify(ZuBSpan bytes, unsigned depth) {
    if (!depth) return false;
    Part part{0, bytes};
    return EH::template verifyParts<Required>(&part, 1, 0, depth - 1);
  }

  static void append(O &o, ZuBSpan bytes) {
    auto entry = EH{bytes}.ctor();
    auto old = o.del(entry.key);
    if constexpr (ZuTraits<V>::IsPointer && ZuTraits<V>::IsPrimitive)
      if (old) delete old->val();
    o.add(ZuMv(entry.key), ZuMv(entry.val));
  }

  static void reset(O &o) {
    if constexpr (ZuTraits<V>::IsPointer && ZuTraits<V>::IsPrimitive) {
      auto i = o.citer();
      while (auto node = i()) delete node->val();
    }
    o.clean();
  }
};

template <ZuString Name>
template <typename O, typename Facet>
struct AsArray<Name>::Handler {
  using E = typename ZuTraits<O>::Elem;
  using T = ObjectType<E>;
  using H = typename As<T>::template Handler<T, Facet>;

  static unsigned count(const O &o) { return o.length(); }

  template <typename B>
  static void save(B &builder, const O &o, unsigned field) {
    for (uint64_t i = o.length(); i; ) {
      const auto &value = o[--i];
      if constexpr (ZuTraits<E>::IsPointer)
	if (!value) { builder.fail(Error::Null); return; }
      unsigned before = builder.size();
      if constexpr (ZuTraits<E>::IsPointer) H::save(builder, *value);
      else H::save(builder, value);
      if (builder.failed()) return;
      builder.varint(builder.size() - before);
      builder.tag(field, Wire::Length);
      if (builder.failed()) return;
    }
  }

  template <bool Required = true>
  static bool verify(ZuBSpan bytes, unsigned depth) {
    if (!depth) return false;
    Part part{0, bytes};
    return H::template verifyParts<Required>(&part, 1, 0, depth - 1);
  }

  static void append(O &o, ZuBSpan bytes) {
    if constexpr (ZuTraits<E>::IsPointer) o.push(H{bytes}.alloc());
    else o.push(H{bytes}.ctor());
  }

  static void reset(O &o) {
    if constexpr (ZuTraits<E>::IsPointer && ZuTraits<E>::IsPrimitive)
      for (unsigned i = 0, n = o.length(); i < n; i++) delete o[i];
    o.clear();
  }
};

template <typename F, unsigned Number_, unsigned Alt_ = 0>
struct FieldCase {
  using Field = F;
  enum { Number = Number_, Alt = Alt_ };
};

template <ZuString Name, typename ...Meta>
template <typename O, typename Facet>
class AsMessage<Name, Meta...>::Handler {
  using Root = ZuStructured<O>;
  using Complete = ZuFields<Root>;
  using Fields = ZuActiveFields<O, Facet>;
  template <typename F> using CtorIndex = ZuFieldProp::GetCtor<typename F::Props>;
  using CtorFields_ = ZuTypeGrep<ZfFieldFilter::Ctor, Fields>;
  using CtorFields = ZuTypeSort<CtorIndex, CtorFields_>;
  using InitFields = ZuTypeGrep<ZfFieldFilter::Init, Fields>;
  template <typename F> using PointerField = ZuBool<
    F::Code == ZfFieldTC::UDT && ZuTraits<typename F::T>::IsPointer>;

  template <typename F, unsigned = F::Code>
  struct CtorArg {
    using T = typename F::T;
    static T initial() {
      if constexpr (F::Code == ZfFieldTC::UDT && !ZuTraits<T>::IsPointer &&
	!ZuFieldProp::HasDeflt<typename F::Props>{}) {
	if constexpr (!As<T>::Union && !As<T>::Repeated)
	  return typename As<T>::template Handler<T, Facet>{ZuBSpan{}}.ctor();
	else return F::deflt();
      } else return F::deflt();
    }
    static T pass(T &value) { return ZuMv(value); }
    template <typename V>
    static void append(T &out, V &&value) { out.push(ZuFwd<V>(value)); }
  };
  template <typename F>
  struct CtorArg<F, ZfFieldTC::CString> {
    using T = CStringArg;
    static T initial() { return T{F::deflt()}; }
    static const char *pass(T &value) { return ZuMv(value).release(); }
  };
  template <typename F>
  struct CtorArg<F, ZfFieldTC::CStringVec> {
    using T = CStringVecArg;
    static T initial() {
      T result;
      auto values = F::deflt();
      for (unsigned i = 0, n = values.length(); i < n; i++) {
	const char *value = values[i];
	result.push(value);
      }
      return result;
    }
    static typename F::T pass(T &value) {
      typename F::T result;
      unsigned n = value.length();
      result.size(n);
      for (unsigned i = 0; i < n; i++)
	result.push(ZuMv(value[i]).release());
      return result;
    }
    static void append(T &out, ZuCSpan value) { out.push(value); }
  };

  template <typename ...Fs>
  struct CtorState {
    using List = ZuTypeList<Fs...>;
    ZuTuple<ZuUnion<void, typename CtorArg<Fs>::T>...> values;

    template <typename F, typename V>
    void set(V &&value) {
      values.template p<ZuTypeIndex<F, List>{}>().template p<1>(ZuFwd<V>(value));
    }
    template <typename F>
    auto &get() {
      auto &value = values.template p<ZuTypeIndex<F, List>{}>();
      if (!value.type()) value.template p<1>(CtorArg<F>::initial());
      return value.template p<1>();
    }
    template <typename F, typename V>
    void append(V &&value) { CtorArg<F>::append(get<F>(), ZuFwd<V>(value)); }
    template <typename ...Args>
    O ctor(Args &&...args) {
      return O(ZuFwd<Args>(args)...,
	CtorArg<Fs>::pass(get<Fs>())...);
    }
    template <typename ...Args>
    O *alloc(Args &&...args) {
      return new O(ZuFwd<Args>(args)...,
	CtorArg<Fs>::pass(get<Fs>())...);
    }
    template <typename ...Args>
    void new_(void *p, Args &&...args) {
      new (p) O(ZuFwd<Args>(args)...,
	CtorArg<Fs>::pass(get<Fs>())...);
    }
  };

  template <typename F>
  struct Find {
    template <typename G>
    using Match = ZuBool<ZuString{F::id()} == ZuString{G::id()}>;
  };

  template <typename F, bool = F::Code == ZfFieldTC::UDT>
  struct UnionField : ZuFalse { };
  template <typename F>
  struct UnionField<F, true> :
    ZuBool<As<ObjectType<typename F::T>>::Union> { };

  static consteval bool explicitNumbers() {
    bool explicit_ = true;
    ZuUnroll::all<Complete>([&]<typename F>() {
      if constexpr (!UnionField<F>{})
	explicit_ &= ZuFieldProp::HasValue<typename F::Props,
	  ZuFieldProp::PB::FieldID>{};
    });
    return explicit_;
  }

public:
  template <typename F>
  static consteval unsigned number() {
    ZuAssert(!UnionField<F>{});
    using Matches = ZuTypeGrep<Find<F>::template Match, Complete>;
    ZuAssert(Matches::N == 1);
    using Core = ZuType<0, Matches>;
    constexpr unsigned n = [] {
      if constexpr (explicitNumbers())
	return unsigned(ZuFieldProp::GetValue<typename Core::Props,
	  ZuFieldProp::PB::FieldID>{});
      else
	return unsigned(ZuTypeIndex<Core, Complete>{}) + 1;
    }();
    ZuAssert(Wire::fieldNumber(n));
    return n;
  }

  template <typename F, typename L>
  static consteval void eachNumber(L &&l) {
    if constexpr (UnionField<F>{}) {
      using T = ObjectType<typename F::T>;
      using Alts = typename As<T>::Types;
      ZuUnroll::all<Alts>([&l]<typename A>() { l(A::Field); });
    } else l(number<F>());
  }

  static consteval bool numbersUnique() {
    bool ok = true;
    ZuUnroll::all<Complete>([&ok]<typename F>() {
      ZuUnroll::all<Complete>([&ok]<typename G>() {
	if constexpr (ZuTypeIndex<F, Complete>{} < ZuTypeIndex<G, Complete>{})
	  eachNumber<F>([&](unsigned f) {
	    eachNumber<G>([&](unsigned g) { if (f == g) ok = false; });
	  });
      });
    });
    return ok;
  }
  ZuAssert(numbersUnique());

  template <typename F>
  static consteval bool zigzag() {
    constexpr bool enabled =
      ZuTypeIn<ZuFieldProp::PB::ZigZag, typename F::Props>{};
    if constexpr (enabled) {
      constexpr unsigned code = F::Code;
      ZuAssert(code == ZfFieldTC::Int32 || code == ZfFieldTC::Int64 ||
	code == ZfFieldTC::Int32Vec || code == ZfFieldTC::Int64Vec);
      ZuAssert((!ZuTypeIn<ZuFieldProp::PB::Fixed,
	typename F::Props>{}));
    }
    return enabled;
  }

  template <typename F>
  static consteval bool fixed() {
    constexpr bool enabled =
      ZuTypeIn<ZuFieldProp::PB::Fixed, typename F::Props>{};
    if constexpr (enabled) {
      constexpr unsigned code = F::Code;
      ZuAssert(code == ZfFieldTC::Int32 || code == ZfFieldTC::UInt32 ||
	code == ZfFieldTC::Int64 || code == ZfFieldTC::UInt64 ||
	code == ZfFieldTC::Int32Vec || code == ZfFieldTC::UInt32Vec ||
	code == ZfFieldTC::Int64Vec || code == ZfFieldTC::UInt64Vec);
      ZuAssert((!ZuTypeIn<ZuFieldProp::PB::ZigZag,
	typename F::Props>{}));
    }
    return enabled;
  }

  template <typename F>
  static consteval bool boolVec() {
    constexpr bool enabled =
      ZuTypeIn<ZuFieldProp::PB::BoolVec, typename F::Props>{};
    if constexpr (enabled)
      ZuAssert(F::Code == ZfFieldTC::UInt8Vec);
    return enabled;
  }

  template <typename F>
  static consteval bool packed() {
    constexpr bool enabled =
      ZuTypeIn<ZuFieldProp::PB::Packed, typename F::Props>{};
    if constexpr (enabled) {
      constexpr unsigned code = F::Code;
      ZuAssert((code >= ZfFieldTC::Int8Vec &&
	code <= ZfFieldTC::UInt64Vec) || code == ZfFieldTC::FloatVec);
    }
    return enabled;
  }

  static uint64_t zigzagEncode(int64_t value) {
    return (uint64_t(value) << 1) ^ uint64_t(value >> 63);
  }

  explicit Handler(ZuBSpan bytes) : m_bytes{bytes} { }

  template <template <typename> class Filter = ZfFieldFilter::Save, typename B>
  static void save(B &builder, const O &o) {
    using Active = ZuTypeGrep<Filter, Fields>;
    using Reverse = ZuTypeRev<Active>;
    ZuUnroll::all<Reverse>([&builder, &o]<typename F>() {
      if (!builder.failed()) saveField<F>(builder, F::get(o));
    });
  }

  static bool verify(ZuBSpan bytes, VerifyLimits limits = {}) {
    Part part{0, bytes};
    return verifyParts(&part, 1, 0, limits.maxDepth);
  }

  template <bool Required = true>
  static bool verifyParts(const Part *parts, unsigned n, uint32_t selector,
    unsigned depth) {
    constexpr unsigned seenSize = Fields::N ? Fields::N : 1;
    auto seen = ZtScratch(Seen, seenSize, seenSize);
    if constexpr (Fields::N) memset(seen.data(), 0, Fields::N);
    Parts nested;
    for (unsigned i = 0; i < n; ++i) {
      if (parts[i].field != selector) continue;
      bool valid = scan<ZfFieldFilter::All>(parts[i].bytes,
	[&seen, &nested, depth]<typename F>(auto alt, ZuBSpan data, unsigned &pos,
	  Wire::T type) {
	  seen[ZuTypeIndex<F, Fields>{}] = 1;
	  if constexpr (F::Code == ZfFieldTC::UDT) {
	    if (type != Wire::Length) return false;
	    ZuBSpan raw;
	    if (!Wire::readLength(data, pos, raw)) return false;
	    using T = ObjectType<typename F::T>;
	    if constexpr (As<T>::Union)
	      nested.push(Part{ZuType<decltype(alt){} - 1,
		typename As<T>::Types>::Field, raw});
	    else if constexpr (As<T>::Repeated)
	      return As<T>::template Handler<T, Facet>::template verify<Required>(
		raw, depth);
	    else nested.push(Part{number<F>(), raw});
	    return true;
	  } else if constexpr (MappedCode<F::Code>{}) {
	    if (type != Wire::Length) return false;
	    ZuBSpan raw;
	    if (!Wire::readLength(data, pos, raw)) return false;
	    nested.push(Part{number<F>(), raw});
	    return true;
	  } else
	    return readField<true, F, Required>(data, pos, type,
	      []<typename V>(const V &) { return true; }, depth);
	}, depth);
      if (!valid) return false;
    }
    return ZuUnroll::all<Fields>(true, [&seen, &nested, depth]<typename F>(bool ok) {
      if (!ok) return false;
      if constexpr (Required &&
	ZuTypeIn<ZuFieldProp::Required, typename F::Props>{})
	if (!seen[ZuTypeIndex<F, Fields>{}]) return false;
      if constexpr (F::Code == ZfFieldTC::UDT) {
	using T = ObjectType<typename F::T>;
	if constexpr (As<T>::Union) {
	  if (seen[ZuTypeIndex<F, Fields>{}])
	    return As<T>::template Handler<T, Facet>::template verifyParts<Required>(
	      nested.data(), nested.length(), depth);
	} else if constexpr (!As<T>::Repeated)
	  if (seen[ZuTypeIndex<F, Fields>{}]) {
	    if (!depth) return false;
	    return As<T>::template Handler<T, Facet>::template verifyParts<Required>(
	      nested.data(), nested.length(), number<F>(), depth - 1);
	  }
      } else if constexpr (MappedCode<F::Code>{}) {
	if (seen[ZuTypeIndex<F, Fields>{}]) {
	  if (!depth) return false;
	  using Map = Mapping<F::Code>;
	  using T = typename Map::T;
	  using H = typename As<T>::template Handler<T, ZuFacet::PB>;
	  return H::template verifyParts<Required>(nested.data(),
	    nested.length(), number<F>(), depth - 1);
	}
      }
      return true;
    });
  }

  template <typename ...Args>
  O ctor(Args &&...args) const {
    auto state = ctorState_();
    if constexpr (!InitFields::N)
      return state.ctor(ZuFwd<Args>(args)...);
    else {
      O o = state.ctor(ZuFwd<Args>(args)...);
      load_<ZfFieldFilter::Init>(o);
      return o;
    }
  }
  template <typename ...Args>
  O *alloc(Args &&...args) const {
    auto state = ctorState_();
    auto o = state.alloc(ZuFwd<Args>(args)...);
    if constexpr (InitFields::N)
      load_<ZfFieldFilter::Init>(*o);
    return o;
  }
  template <typename ...Args>
  void new_(void *p, Args &&...args) const {
    auto state = ctorState_();
    state.new_(p, ZuFwd<Args>(args)...);
    if constexpr (InitFields::N)
      load_<ZfFieldFilter::Init>(*static_cast<O *>(p));
  }
  void load(O &o) const { load_<ZfFieldFilter::Load>(o); }
  void update(O &o) const { load_<ZfFieldFilter::Upd, true>(o); }
  void merge(O &o) const { load_<ZfFieldFilter::Load, false, true>(o); }

private:
  template <unsigned Code, typename V>
  static V mergeMapped(ZuBSpan raw, const V *current) {
    using Map = Mapping<Code>;
    using T = typename Map::T;
    using H = typename As<T>::template Handler<T, ZuFacet::PB>;
    if (!current) return Map::load(H{raw}.ctor());
    T wire = Map::save(*current);
    H{raw}.merge(wire);
    return Map::load(wire);
  }

  template <unsigned Code, typename B, typename V>
  static void saveMapped(B &builder, const V &value, unsigned field) {
    using Map = Mapping<Code>;
    using T = typename Map::T;
    unsigned before = builder.size();
    As<T>::template Handler<T, ZuFacet::PB>::save(builder, Map::save(value));
    if (builder.failed()) return;
    builder.varint(builder.size() - before);
    builder.tag(field, Wire::Length);
  }

  auto ctorState_() const {
    ZuTypeApply<CtorState, CtorFields> state;
    if constexpr (CtorFields::N) {
      constexpr unsigned seenSize = Fields::N ? Fields::N : 1;
      auto seen = ZtScratch(Seen, seenSize, seenSize);
      if constexpr (Fields::N) memset(seen.data(), 0, Fields::N);
      bool ok = scan<ZfFieldFilter::Ctor>(m_bytes,
	[&]<typename F>(auto alt, ZuBSpan bytes, unsigned &pos,
	  Wire::T type) {
	  if constexpr (MappedCode<F::Code>{}) {
	    if (type != Wire::Length) return false;
	    ZuBSpan raw;
	    if (!Wire::readLength(bytes, pos, raw)) return false;
	    state.template set<F>(mergeMapped<F::Code>(raw,
	      seen[ZuTypeIndex<F, Fields>{}] ? &state.template get<F>() : nullptr));
	    seen[ZuTypeIndex<F, Fields>{}] = 1;
	    return true;
	  }
	  using Alt = decltype(alt);
	  return readField<false, F>(bytes, pos, type,
	    [&](const auto &value) {
	    if constexpr (F::Code == ZfFieldTC::UDT) {
	      using T = typename F::T;
	      using U = ObjectType<T>;
	      using H = typename As<U>::template Handler<U, Facet>;
	      if constexpr (As<U>::Union) {
		H::template loadAlt<Alt{}>(state.template get<F>(), value);
	      } else if constexpr (As<U>::Repeated) {
		if constexpr (ZuTraits<T>::IsPointer) {
		  auto &out = state.template get<F>();
		  if (!out) out = new U();
		  H::append(*out, value);
		} else H::append(state.template get<F>(), value);
	      } else if constexpr (ZuTraits<T>::IsPointer) {
		auto &out = state.template get<F>();
		if (!out) out = H{value}.alloc();
		else H{value}.merge(*out);
	      } else {
		if (!seen[ZuTypeIndex<F, Fields>{}])
		  state.template set<F>(H{value}.ctor());
		else H{value}.merge(state.template get<F>());
		seen[ZuTypeIndex<F, Fields>{}] = 1;
	      }
	    } else if constexpr (ZfFieldTC::IsVec<F::Code>{}) {
	      state.template append<F>(value);
	    } else state.template set<F>(value);
	    return true;
	  });
	});
      ZmAssert_(ok);
    }
    return state;
  }

  template <typename F, typename B, typename V>
  static void saveField(B &builder, const V &value) {
    constexpr unsigned code = F::Code;
    constexpr bool required = ZuTypeIn<ZuFieldProp::Required, typename F::Props>{};
    if constexpr (code == ZfFieldTC::Bool) {
      builder.varint(value ? 1 : 0);
      builder.tag(number<F>(), Wire::Varint);
    } else if constexpr (code >= ZfFieldTC::Int8 && code <= ZfFieldTC::UInt64) {
      if (ZuNull(value) &&
	!ZuTypeIn<ZuFieldProp::PB::WireWord, typename F::Props>{}) {
	if constexpr (required) builder.fail(Error::Required);
	return;
      }
      if constexpr (fixed<F>()) {
	if constexpr (sizeof(V) == 4) {
	  builder.fixed32(uint32_t(value));
	  builder.tag(number<F>(), Wire::Fixed32);
	} else {
	  builder.fixed64(uint64_t(value));
	  builder.tag(number<F>(), Wire::Fixed64);
	}
      } else {
	if constexpr (zigzag<F>()) builder.varint(zigzagEncode(value));
	else builder.varint(uint64_t(value));
	builder.tag(number<F>(), Wire::Varint);
      }
    } else if constexpr (code == ZfFieldTC::CString || code == ZfFieldTC::String ||
      code == ZfFieldTC::Bytes) {
      if (!ZuTraits<V>::data(value)) {
	if constexpr (required) builder.fail(Error::Required);
	return;
      }
      if constexpr (code == ZfFieldTC::Bytes) {
	ZuBSpan span{value};
	builder.bytes(span);
	builder.varint(span.length());
      } else {
	ZuCSpan span{value};
	builder.bytes({reinterpret_cast<const uint8_t *>(span.data()), span.length()});
	builder.varint(span.length());
      }
      builder.tag(number<F>(), Wire::Length);
    } else if constexpr (code == ZfFieldTC::Float) {
      if (ZuNull(value)) {
	if constexpr (required) builder.fail(Error::Required);
	return;
      }
      if constexpr (sizeof(value) == 4) {
	uint32_t bits;
	memcpy(&bits, &value, sizeof(bits));
	builder.fixed32(bits);
	builder.tag(number<F>(), Wire::Fixed32);
      } else {
	uint64_t bits;
	memcpy(&bits, &value, sizeof(bits));
	builder.fixed64(bits);
	builder.tag(number<F>(), Wire::Fixed64);
      }
    } else if constexpr (MappedCode<code>{}) {
      saveMapped<code>(builder, value, number<F>());
    } else if constexpr (code == ZfFieldTC::UDT) {
      using T = ObjectType<typename F::T>;
      if constexpr (As<T>::Union) {
	if constexpr (required)
	  if (!value.type()) { builder.fail(Error::Required); return; }
	As<T>::template Handler<T, Facet>::save(builder, value);
      } else if constexpr (As<T>::Repeated) {
	if constexpr (ZuTraits<typename F::T>::IsPointer)
	  if (!value) {
	    if constexpr (required) builder.fail(Error::Required);
	    return;
	  }
	if constexpr (required) {
	  using H = typename As<T>::template Handler<T, Facet>;
	  if constexpr (ZuTraits<typename F::T>::IsPointer) {
	    if (!H::count(*value)) { builder.fail(Error::Required); return; }
	  } else if (!H::count(value)) { builder.fail(Error::Required); return; }
	}
	if constexpr (ZuTraits<typename F::T>::IsPointer)
	  As<T>::template Handler<T, Facet>::save(builder, *value, number<F>());
	else As<T>::template Handler<T, Facet>::save(builder, value, number<F>());
      } else {
	if constexpr (ZuTraits<typename F::T>::IsPointer)
	  if (!value) {
	    if constexpr (required) builder.fail(Error::Required);
	    return;
	  }
	unsigned before = builder.size();
	if constexpr (ZuTraits<typename F::T>::IsPointer)
	  As<T>::template Handler<T, Facet>::save(builder, *value);
	else As<T>::template Handler<T, Facet>::save(builder, value);
	if (builder.failed()) return;
	builder.varint(builder.size() - before);
	builder.tag(number<F>(), Wire::Length);
      }
    } else if constexpr (code >= ZfFieldTC::Int8Vec &&
      code <= ZfFieldTC::UInt64Vec) {
      [[maybe_unused]] unsigned before = builder.size();
      for (uint64_t i = value.length(); i; ) {
	auto elem = value[--i];
	if constexpr (boolVec<F>()) {
	  builder.varint(elem ? 1 : 0);
	  if constexpr (!packed<F>()) builder.tag(number<F>(), Wire::Varint);
	} else if constexpr (fixed<F>()) {
	  if constexpr (sizeof(elem) == 4) {
	    builder.fixed32(uint32_t(elem));
	    if constexpr (!packed<F>()) builder.tag(number<F>(), Wire::Fixed32);
	  } else {
	    builder.fixed64(uint64_t(elem));
	    if constexpr (!packed<F>()) builder.tag(number<F>(), Wire::Fixed64);
	  }
	} else {
	  if constexpr (zigzag<F>()) builder.varint(zigzagEncode(elem));
	  else builder.varint(uint64_t(elem));
	  if constexpr (!packed<F>()) builder.tag(number<F>(), Wire::Varint);
	}
	if (builder.failed()) return;
      }
      if constexpr (packed<F>()) if (builder.size() != before) {
	builder.varint(builder.size() - before);
	builder.tag(number<F>(), Wire::Length);
      }
    } else if constexpr (code == ZfFieldTC::FloatVec) {
      [[maybe_unused]] unsigned before = builder.size();
      for (uint64_t i = value.length(); i; ) {
	auto elem = value[--i];
	if constexpr (sizeof(elem) == 4) {
	  uint32_t bits;
	  memcpy(&bits, &elem, sizeof(bits));
	  builder.fixed32(bits);
	  if constexpr (!packed<F>()) builder.tag(number<F>(), Wire::Fixed32);
	} else {
	  uint64_t bits;
	  memcpy(&bits, &elem, sizeof(bits));
	  builder.fixed64(bits);
	  if constexpr (!packed<F>()) builder.tag(number<F>(), Wire::Fixed64);
	}
	if (builder.failed()) return;
      }
      if constexpr (packed<F>()) if (builder.size() != before) {
	builder.varint(builder.size() - before);
	builder.tag(number<F>(), Wire::Length);
      }
    } else if constexpr (MappedVecCode<code>{}) {
      constexpr unsigned elem = ZfFieldTC::Elem<code>{};
      for (uint64_t i = value.length(); i; ) {
	auto &item = value[--i];
	saveMapped<elem>(builder, item, number<F>());
	if (builder.failed()) return;
      }
    } else if constexpr (code == ZfFieldTC::CStringVec ||
      code == ZfFieldTC::StringVec ||
      code == ZfFieldTC::BytesVec) {
      for (uint64_t i = value.length(); i; ) {
	if constexpr (code == ZfFieldTC::BytesVec) {
	  ZuBSpan elem{value[--i]};
	  builder.bytes(elem);
	  builder.varint(elem.length());
	} else {
	  ZuCSpan elem{value[--i]};
	  builder.bytes({reinterpret_cast<const uint8_t *>(elem.data()), elem.length()});
	  builder.varint(elem.length());
	}
	builder.tag(number<F>(), Wire::Length);
	if (builder.failed()) return;
      }
    } else ZuAssert(code == ZfFieldTC::Bool, "field type is not implemented");
  }

  template <bool Validate, typename T, bool ZigZag = false, typename Apply>
  static bool readInt(ZuBSpan bytes, unsigned &pos, Apply &&apply) {
    uint64_t raw;
    if (!Wire::readVarint(bytes, pos, raw)) return false;
    if constexpr (ZigZag) {
      int64_t decoded =
	int64_t((raw >> 1) ^ uint64_t(-int64_t(raw & 1)));
      T value = T(decoded);
      if constexpr (Validate)
	if (int64_t(value) != decoded) return false;
      return apply(value);
    } else {
      T value = T(raw);
      if constexpr (Validate) {
	if constexpr (ZuTraits<T>::IsSigned) {
	  if (uint64_t(int64_t(value)) != raw) return false;
	} else if (uint64_t(value) != raw) return false;
      }
      return apply(value);
    }
  }

  template <typename T, typename Apply>
  static bool readFixed(ZuBSpan bytes, unsigned &pos, Apply &&apply) {
    if (bytes.length() - pos < sizeof(T)) return false;
    if constexpr (sizeof(T) == 4) {
      ZuLittleEndian<uint32_t> wire;
      memcpy(&wire, bytes.data() + pos, sizeof(wire));
      pos += sizeof(wire);
      return apply(T(uint32_t(wire)));
    } else {
      ZuLittleEndian<uint64_t> wire;
      memcpy(&wire, bytes.data() + pos, sizeof(wire));
      pos += sizeof(wire);
      return apply(T(uint64_t(wire)));
    }
  }

  template <typename T, typename Apply>
  static bool readFloat(ZuBSpan bytes, unsigned &pos, Apply &&apply) {
    if (bytes.length() - pos < sizeof(T)) return false;
    if constexpr (sizeof(T) == 4) {
      ZuLittleEndian<uint32_t> wire;
      memcpy(&wire, bytes.data() + pos, sizeof(wire));
      pos += sizeof(wire);
      uint32_t bits = wire;
      T value;
      memcpy(&value, &bits, sizeof(value));
      return apply(value);
    } else {
      ZuLittleEndian<uint64_t> wire;
      memcpy(&wire, bytes.data() + pos, sizeof(wire));
      pos += sizeof(wire);
      uint64_t bits = wire;
      T value;
      memcpy(&value, &bits, sizeof(value));
      return apply(value);
    }
  }

  template <bool Validate, unsigned Code, bool Required = true,
    typename Apply>
  static bool readMapped(ZuBSpan bytes, unsigned &pos, Wire::T type,
    Apply &&apply, unsigned depth) {
    if (type != Wire::Length) return false;
    ZuBSpan raw;
    if (!Wire::readLength(bytes, pos, raw)) return false;
    using Map = Mapping<Code>;
    using T = typename Map::T;
    using H = typename As<T>::template Handler<T, ZuFacet::PB>;
    if constexpr (Validate) {
      if (!depth) return false;
      Part part{0, raw};
      return H::template verifyParts<Required>(&part, 1, 0, depth - 1);
    } else return apply(Map::load(H{raw}.ctor()));
  }

  template <bool Validate, typename F, bool Required = true,
    typename Apply>
  static bool readField(ZuBSpan bytes, unsigned &pos, Wire::T type,
    Apply &&apply, unsigned depth = UINT_MAX) {
    constexpr unsigned code = F::Code;
    if constexpr (code == ZfFieldTC::Bool ||
      (code >= ZfFieldTC::Int8 && code <= ZfFieldTC::UInt64)) {
      if constexpr (code == ZfFieldTC::Bool) {
	if (type != Wire::Varint) return false;
	uint64_t raw;
	return Wire::readVarint(bytes, pos, raw) && apply(bool(raw));
      } else if constexpr (fixed<F>()) {
	using T = typename F::T;
	if (type != (sizeof(T) == 4 ? Wire::Fixed32 : Wire::Fixed64))
	  return false;
	return readFixed<T>(bytes, pos, apply);
      } else {
	if (type != Wire::Varint) return false;
	return readInt<Validate, typename F::T, zigzag<F>()>(
	  bytes, pos, apply);
      }
    } else if constexpr (code == ZfFieldTC::CString ||
      code == ZfFieldTC::String || code == ZfFieldTC::Bytes) {
      if (type != Wire::Length) return false;
      ZuBSpan raw;
      if (!Wire::readLength(bytes, pos, raw)) return false;
      if constexpr (code == ZfFieldTC::Bytes) return apply(raw);
      else return apply(ZuCSpan{reinterpret_cast<const char *>(raw.data()), raw.length()});
    } else if constexpr (code == ZfFieldTC::Float) {
      using T = typename F::T;
      if (type != (sizeof(T) == 4 ? Wire::Fixed32 : Wire::Fixed64))
	return false;
      return readFloat<T>(bytes, pos, apply);
    } else if constexpr (MappedCode<code>{}) {
      return readMapped<Validate, code, Required>(
	bytes, pos, type, apply, depth);
    } else if constexpr (code == ZfFieldTC::UDT) {
      if (type != Wire::Length) return false;
      ZuBSpan raw;
      if (!Wire::readLength(bytes, pos, raw)) return false;
      return apply(raw);
    } else if constexpr (code >= ZfFieldTC::Int8Vec &&
      code <= ZfFieldTC::UInt64Vec) {
      constexpr unsigned elem = ZfFieldTC::Elem<code>{};
      using T = ZfFieldTC::Type<elem>;
      if constexpr (boolVec<F>()) {
	if (type == Wire::Varint) {
	  uint64_t raw;
	  return Wire::readVarint(bytes, pos, raw) && apply(uint8_t(raw != 0));
	}
	if (type != Wire::Length) return false;
	ZuBSpan packed;
	if (!Wire::readLength(bytes, pos, packed)) return false;
	unsigned offset = 0;
	while (offset < packed.length()) {
	  uint64_t raw;
	  if (!Wire::readVarint(packed, offset, raw) ||
	    !apply(uint8_t(raw != 0))) return false;
	}
	return true;
      } else if constexpr (fixed<F>()) {
	constexpr auto wire = sizeof(T) == 4 ? Wire::Fixed32 : Wire::Fixed64;
	if (type == wire) return readFixed<T>(bytes, pos, apply);
	if (type != Wire::Length) return false;
	ZuBSpan packed;
	if (!Wire::readLength(bytes, pos, packed) ||
	  packed.length() % sizeof(T)) return false;
	unsigned offset = 0;
	while (offset < packed.length())
	  if (!readFixed<T>(packed, offset, apply)) return false;
	return true;
      }
      if (type == Wire::Varint)
	return readInt<Validate, T, zigzag<F>()>(bytes, pos, apply);
      if (type != Wire::Length) return false;
      ZuBSpan packed;
      if (!Wire::readLength(bytes, pos, packed)) return false;
      unsigned offset = 0;
      while (offset < packed.length())
	if (!readInt<Validate, T, zigzag<F>()>(packed, offset, apply))
	  return false;
      return true;
    } else if constexpr (code == ZfFieldTC::FloatVec) {
      using T = ZuDecay<decltype(ZuDeclVal<typename F::T &>()[0])>;
      if (type == (sizeof(T) == 4 ? Wire::Fixed32 : Wire::Fixed64))
	return readFloat<T>(bytes, pos, apply);
      if (type != Wire::Length) return false;
      ZuBSpan packed;
      if (!Wire::readLength(bytes, pos, packed) ||
	packed.length() % sizeof(T)) return false;
      unsigned offset = 0;
      while (offset < packed.length())
	if (!readFloat<T>(packed, offset, apply)) return false;
      return true;
    } else if constexpr (MappedVecCode<code>{}) {
      constexpr unsigned elem = ZfFieldTC::Elem<code>{};
      return readMapped<Validate, elem, Required>(
	bytes, pos, type, apply, depth);
    } else if constexpr (code == ZfFieldTC::CStringVec ||
      code == ZfFieldTC::StringVec ||
      code == ZfFieldTC::BytesVec) {
      if (type != Wire::Length) return false;
      ZuBSpan raw;
      if (!Wire::readLength(bytes, pos, raw)) return false;
      if constexpr (code == ZfFieldTC::BytesVec) return apply(raw);
      else return apply(ZuCSpan{reinterpret_cast<const char *>(raw.data()), raw.length()});
    } else ZuAssert(code == ZfFieldTC::Bool, "field type is not implemented");
  }

  template <typename F, bool = UnionField<F>{}>
  struct FieldCases {
    using T = ZuTypeList<FieldCase<F, number<F>()>>;
  };
  template <typename F>
  struct FieldCases<F, true> {
    using Alts = typename As<ObjectType<typename F::T>>::Types;
    template <typename A>
    using Case = FieldCase<F, A::Field, ZuTypeIndex<A, Alts>{} + 1>;
    using T = ZuTypeMap<Case, Alts>;
  };
  template <typename F> using Cases = typename FieldCases<F>::T;
  template <typename C> using CaseNumber = ZuUnsigned<C::Number>;

  template <template <typename> class Filter, typename Apply>
  static bool scan(ZuBSpan bytes, Apply &&apply, unsigned maxDepth = UINT_MAX) {
    using Active = ZuTypeGrep<Filter, Fields>;
    using Dispatch = ZuTypeApply<ZuTypeConcat, ZuTypeMap<Cases, Active>>;
    using Numbers = ZuTypeMap<CaseNumber, Dispatch>;
    using Seq = ZuTypeApply<ZuTLSeq, Numbers>;
    unsigned pos = 0;
    while (pos < bytes.length()) {
      uint32_t field;
      Wire::T type;
      if (!Wire::readTag(bytes, pos, field, type)) return false;
      bool known = false;
      bool valid = ZuSwitch::dispatch<Seq>(field,
	[&known, &apply, bytes, &pos, type](auto id) {
	  using C = ZuType<ZuTypeIndex<decltype(id), Numbers>{}, Dispatch>;
	  known = true;
	  return apply.template operator()<typename C::Field>(
	    ZuUnsigned<C::Alt>{}, bytes, pos, type);
	}, false);
      if (known) {
	if (!valid) return false;
      } else if (!Wire::skip(bytes, pos, field, type, maxDepth)) return false;
    }
    return true;
  }

  template <template <typename> class Filter, bool Update = false,
    bool Merge = false>
  void load_(O &o) const {
    constexpr unsigned seenSize = Fields::N ? Fields::N : 1;
    auto seen = ZtScratch(Seen, seenSize, seenSize);
    if constexpr (Fields::N) memset(seen.data(), 0, Fields::N);
    bool ok = scan<Filter>(m_bytes,
      [&]<typename F>(auto alt, ZuBSpan bytes, unsigned &pos,
	Wire::T type) {
	if constexpr (!F::ReadOnly && MappedCode<F::Code>{}) {
	  if (type != Wire::Length) return false;
	  ZuBSpan raw;
	  if (!Wire::readLength(bytes, pos, raw)) return false;
	  const auto &current = F::get(o);
	  F::set(o, mergeMapped<F::Code>(raw,
	    Merge || seen[ZuTypeIndex<F, Fields>{}] ? &current : nullptr));
	  seen[ZuTypeIndex<F, Fields>{}] = 1;
	  return true;
	}
	using Alt = decltype(alt);
	return readField<false, F>(bytes, pos, type,
	  [&](const auto &value) {
	  (void)seen;
	  if constexpr (F::ReadOnly) return true;
	  else if constexpr (F::Code == ZfFieldTC::UDT) {
	    using T = typename F::T;
	    using U = ObjectType<T>;
	    using H = typename As<U>::template Handler<U, Facet>;
	    if constexpr (As<U>::Union) {
	      H::template loadAlt<Alt{}>(F::get(o), value);
	    } else if constexpr (As<U>::Repeated) {
	      if constexpr (ZuTraits<T>::IsPointer) {
		auto &out = F::get(o);
		if (!seen[ZuTypeIndex<F, Fields>{}] && !Update && !Merge) {
		  U *next = new U();
		  if constexpr (ZuTraits<T>::IsPrimitive) delete out;
		  F::set(o, next);
		} else if (!out) F::set(o, new U());
		seen[ZuTypeIndex<F, Fields>{}] = 1;
		H::append(*F::get(o), value);
	      } else {
		if (!seen[ZuTypeIndex<F, Fields>{}] && !Update && !Merge)
		  H::reset(F::get(o));
		seen[ZuTypeIndex<F, Fields>{}] = 1;
		H::append(F::get(o), value);
	      }
	    } else if constexpr (ZuTraits<T>::IsPointer) {
	      if (!seen[ZuTypeIndex<F, Fields>{}] && !Merge) {
		U *next = H{value}.alloc();
		if constexpr (ZuTraits<T>::IsPrimitive) delete F::get(o);
		F::set(o, next);
		seen[ZuTypeIndex<F, Fields>{}] = 1;
	      } else if (F::get(o)) H{value}.merge(*F::get(o));
	      else F::set(o, H{value}.alloc());
	    } else {
	      if (!Merge && !seen[ZuTypeIndex<F, Fields>{}])
		F::set(o, H{value}.ctor());
	      else H{value}.merge(F::get(o));
	      seen[ZuTypeIndex<F, Fields>{}] = 1;
	    }
	  } else if constexpr (F::Code == ZfFieldTC::CString) {
	    auto next = CStringArg{value}.release();
	    CStringArg::free(F::get(o));
	    F::set(o, next);
	  } else if constexpr (F::Code == ZfFieldTC::CStringVec) {
	    F::get(o).push(CStringArg{value}.release());
	  } else if constexpr (ZfFieldTC::IsVec<F::Code>{}) {
	    F::get(o).push(value);
	  } else F::set(o, value);
	  return true;
	});
      });
    ZmAssert_(ok);
    using Active = ZuTypeGrep<Filter, Fields>;
    ZuUnroll::all<Active>([&]<typename F>() {
      if constexpr (ZfFieldWritable<F>{} && PointerField<F>{} &&
	ZuTypeIn<ZuFieldProp::Reset, typename F::Props>{}) {
	if (!seen[ZuTypeIndex<F, Fields>{}]) {
	  if constexpr (ZuTraits<typename F::T>::IsPrimitive) delete F::get(o);
	  F::set(o, F::deflt());
	}
      }
    });
  }

  ZuBSpan m_bytes;
};

template <typename Map>
template <typename O, typename Facet>
class AsMapped<Map>::Handler {
  using Wire = typename Map::T;
  using H = typename As<Wire>::template Handler<Wire, ZuFacet::PB>;
public:
  explicit Handler(ZuBSpan bytes) : m_bytes{bytes} { }

  template <template <typename> class Filter = ZfFieldFilter::Save, typename B>
  static void save(B &builder, const O &o) {
    H::template save<Filter>(builder, Map::save(o));
  }
  static bool verify(ZuBSpan bytes, VerifyLimits limits = {}) {
    return H::verify(bytes, limits);
  }
  template <bool Required = true>
  static bool verifyParts(const Part *parts, unsigned n, uint32_t selector,
    unsigned depth) {
    return H::template verifyParts<Required>(parts, n, selector, depth);
  }

  O ctor() const { return Map::load(H{m_bytes}.ctor()); }
  O *alloc() const { return new O{ctor()}; }
  void new_(void *p) const { new (p) O{ctor()}; }
  void load(O &o) const { o = ctor(); }
  void merge(O &o) const {
    Wire wire = Map::save(o);
    H{m_bytes}.merge(wire);
    o = Map::load(wire);
  }
  void update(O &o) const { load(o); }
private:
  ZuBSpan m_bytes;
};

template <typename Storage> ZuTrue IsBuilder_(Builder<Storage> *);
ZuFalse IsBuilder_(...);
template <typename T>
using IsBuilder = decltype(IsBuilder_(ZuDeclVal<T *>()));

template <typename Facet = ZuFacet::PB,
  template <typename> class Filter = ZfFieldFilter::Save,
  typename B, typename O, typename = ZuIfT<IsBuilder<B>{}>>
ZuBSpan save(B &builder, const O &o) {
  As<O>::template Handler<O, Facet>::template save<Filter>(builder, o);
  if constexpr (Config<Facet>::SizePrefixed)
    if (!builder.failed()) builder.fixed32(builder.size());
  return builder.failed() ? ZuBSpan{} : builder.bytes();
}

template <typename Facet = ZuFacet::PB,
  template <typename> class Filter = ZfFieldFilter::Save,
  typename S, typename O, ZuIfT<!IsBuilder<S>{}, int> = 0>
int save(S &stream, const O &o) {
  if constexpr (ZiIsTxStream<S>{}) {
    auto buf = stream.allocBuf(stream.headRoom());
    if (!buf) return Error::OK;
    unsigned capacity = stream.maxSize() >
      uint64_t(buf->skip) + stream.tailRoom() ?
      stream.maxSize() - buf->skip - stream.tailRoom() : 0;
    IOBuilder builder{ZuMv(buf), UINT32_MAX, stream.tailRoom()};
    auto bytes = save<Facet, Filter>(builder, o);
    if (builder.failed()) return builder.error();
    if (bytes.length() <= capacity)
      stream << builder.buf() << Zi::Flush{};
    else
      stream << bytes << Zi::Flush{};
    return Error::OK;
  } else {
    using Output = ZtArray<uint8_t, ZtArrayHeapID<"Zpb.Stream">>;
    auto output = ZtScratch(Output, ZiIOBuf_DefltSize);
    ArrayStorage<Output> storage{output};
    Builder builder{storage};
    auto bytes = save<Facet, Filter>(builder, o);
    if (builder.failed()) return builder.error();
    stream << bytes;
    return Error::OK;
  }
}

template <typename O, typename Facet = ZuFacet::PB>
bool verify(ZuBSpan bytes, VerifyLimits limits = {}) {
  if constexpr (Config<Facet>::SizePrefixed) {
    ZuBSpan payload;
    if (!Wire::unframe(bytes, payload)) return false;
    bytes = payload;
  }
  return As<O>::template Handler<O, Facet>::verify(bytes, limits);
}

template <typename O, typename Facet = ZuFacet::PB>
auto handler(ZuBSpan bytes) {
  if constexpr (Config<Facet>::SizePrefixed) {
    ZuBSpan payload;
    ZmAssert_(Wire::unframe(bytes, payload));
    bytes = payload;
  }
  return typename As<O>::template Handler<O, Facet>{bytes};
}

template <typename O, typename Facet = ZuFacet::PB, typename ...Args>
O load(ZuBSpan bytes, Args &&...args) {
  return handler<O, Facet>(bytes).ctor(ZuFwd<Args>(args)...);
}

} // Zpb

#endif /* Zpb_HH */
