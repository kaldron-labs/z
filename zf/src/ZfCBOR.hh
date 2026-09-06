//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// CBOR structural scanner

#ifndef ZfCBOR_HH
#define ZfCBOR_HH

#ifndef ZfLib_HH
#include <zlib/ZfLib.hh>
#endif

#include <zlib/ZuSpan.hh>
#include <zlib/ZuUnroll.hh>
#include <zlib/ZuMArray.hh>

#include <zlib/ZfStruct.hh>

#include <zlib/ZtArray.hh>
#include <zlib/ZtScratch.hh>

ZuStructFacet(CBOR);

namespace ZuFieldProp::CBOR {

template <ZuString ID_> struct ID { };
struct IntID { };
template <uint64_t N> struct Tag { };
template <uint8_t Type_> struct Type { };
template <uint8_t Type_> struct ElemType { };
struct Simple { };
template <bool> struct Optional { };

using Opt = Optional<true>;

template <typename Props, uint8_t Deflt,
  bool = HasValue<Props, Type>{}>
struct GetType_ { using T = ZuConstant<uint8_t, Deflt>; };
template <typename Props, uint8_t Deflt>
struct GetType_<Props, Deflt, true> {
  using T = GetValue<Props, Type>;
};
template <typename Props, uint8_t Deflt>
using GetType = typename GetType_<Props, Deflt>::T;

template <typename Props, bool = HasValue<Props, ElemType>{},
  bool = ZuTypeIn<Simple, Props>{}>
struct GetElemProps_ { using T = ZuTypeList<>; };
template <typename Props>
struct GetElemProps_<Props, true, false> {
  using T = ZuTypeList<Type<GetValue<Props, ElemType>{}>>;
};
template <typename Props>
struct GetElemProps_<Props, false, true> { using T = ZuTypeList<Simple>; };
template <typename Props>
struct GetElemProps_<Props, true, true> {
  using T = ZuTypeList<Type<GetValue<Props, ElemType>{}>, Simple>;
};
template <typename Props>
using GetElemProps = typename GetElemProps_<Props>::T;

template <typename Props, bool = HasValue<Props, Optional>{}>
struct GetOptional_ { using T = ZuFalse; };
template <typename Props>
struct GetOptional_<Props, true> {
  using T = GetValue<Props, Optional>;
};
template <typename Props>
using GetOptional = typename GetOptional_<Props>::T;

template <typename Props, bool = HasValue<Props, Tag>{}>
struct GetTag_ { using T = ZuConstant<uint64_t, UINT64_MAX>; };
template <typename Props>
struct GetTag_<Props, true> { using T = GetValue<Props, Tag>; };
template <typename Props>
using GetTag = typename GetTag_<Props>::T;

template <typename Props, bool = ZuTypeIn<Simple, Props>{}>
struct IsSimple_ : public ZuFalse { };
template <typename Props>
struct IsSimple_<Props, true> : public ZuTrue { };
template <typename Props>
using IsSimple = IsSimple_<Props>;

template <typename Field,
  bool = HasValue<typename Field::Props, ID>{}>
struct GetID_ {
  using T = ZuStringT<Field::id()>;
};
template <typename Field>
struct GetID_<Field, true> {
  using T = GetValue<typename Field::Props, ID>;
};
template <typename Field>
using GetID = typename GetID_<Field>::T;

template <typename Props, bool = ZuTypeIn<IntID, Props>{}>
struct IsIntID_ : public ZuFalse { };
template <typename Props>
struct IsIntID_<Props, true> : public ZuTrue { };
template <typename Props>
using IsIntID = IsIntID_<Props>;

} // ZuFieldProp::CBOR

namespace ZfCBOR {

namespace Error {
  enum { OK, Syntax };
}

struct Result {
  unsigned	offset = 0;
  int		error = Error::Syntax;

  explicit operator bool() const { return error == Error::OK; }
};

ZfExtern Result scan(ZuBSpan);

namespace Detail {

using StringScratch = ZtArray<uint8_t, ZtArrayHeapID<"ZfCBOR.String">>;

struct Head {
  uint64_t	arg;
  unsigned	length;
  uint8_t	major;
  uint8_t	ai;
};

inline bool head(ZuBSpan span, Head &head)
{
  if (!span.length()) return false;
  uint8_t initial = span[0];
  head.major = initial >> 5;
  head.ai = initial & 31;
  head.length = 1;
  if (head.ai < 24) {
    head.arg = head.ai;
    return true;
  }
  if (head.ai == 31) {
    head.arg = UINT64_MAX;
    return true;
  }
  unsigned n;
  switch (head.ai) {
    case 24: n = 1; break;
    case 25: n = 2; break;
    case 26: n = 4; break;
    case 27: n = 8; break;
    default: return false;
  }
  if (span.length() - 1 < n) return false;
  uint64_t arg = 0;
  for (unsigned i = 0; i < n; ++i)
    arg = (arg << 8) | span[head.length++];
  head.arg = arg;
  return true;
}

inline bool item(ZuBSpan span, ZuBSpan &item)
{
  auto result = scan(span);
  if (!result) return false;
  item = {span.begin(), result.offset};
  return true;
}

inline bool content(ZuBSpan span, Head &head, ZuBSpan &content)
{
  if (!Detail::head(span, head) || head.ai == 31 ||
      head.arg > span.length() - head.length)
    return false;
  content = {span.begin() + head.length, unsigned(head.arg)};
  return true;
}

template <typename T, unsigned Major>
inline T stringValue(ZuBSpan span)
{
  Head initial;
  if (!head(span, initial) || initial.major != Major) return T{};
  if (initial.ai != 31) {
    ZuBSpan value;
    if (!content(span, initial, value)) return T{};
    if constexpr (Major == 2) return T(value);
    else return T(ZuCSpan(value));
  }

  const uint8_t *p = span.begin() + initial.length;
  const uint8_t *end = span.end();
  unsigned n = 0, chunks = 0;
  for (;;) {
    if (p >= end) return T{};
    if (*p == 0xff) { ++p; break; }
    Head chunk;
    if (!head({p, unsigned(end - p)}, chunk) ||
	chunk.major != Major || chunk.ai == 31 ||
	chunk.arg > unsigned(end - p - chunk.length) ||
	chunk.arg > UINT_MAX - n)
      return T{};
    n += unsigned(chunk.arg);
    ++chunks;
    p += chunk.length + unsigned(chunk.arg);
  }
  if (p != end) return T{};
  if constexpr (ZuIs_<T, ZuBSpan>{} || ZuIs_<T, ZuCSpan>{}) {
    // A non-owning span cannot represent multiple discontiguous chunks.
    if (chunks != 1) return T{};
    Head chunk;
    if (!head({span.begin() + initial.length,
	unsigned(span.end() - span.begin() - initial.length)}, chunk)) return T{};
    ZuBSpan value{span.begin() + initial.length + chunk.length,
	unsigned(chunk.arg)};
    if constexpr (Major == 2) return T(value);
    else return T(ZuCSpan(value));
  } else {
    // Size from chunk headers only, then coalesce once into stack/heap scratch.
    auto value = ZtScratch(StringScratch, n, n);
    p = span.begin() + initial.length;
    unsigned o = 0;
    while (*p != 0xff) {
      Head chunk;
      if (!head({p, unsigned(end - p)}, chunk)) return T{};
      p += chunk.length;
      for (unsigned i = 0, l = unsigned(chunk.arg); i < l; ++i)
	value[o++] = p[i];
      p += unsigned(chunk.arg);
    }
    if constexpr (Major == 2) return T(ZuBSpan(value));
    else return T(ZuCSpan(ZuBSpan(value)));
  }
}

} // Detail

inline bool untag(ZuBSpan &span, uint64_t tag);

struct FieldSpanHeap : public ZuStringT<"ZfCBOR.FieldSpan"> { };
using FieldSpans = ZtArray<ZuBSpan, ZtArrayHeapID_<FieldSpanHeap>>;

template <typename Field>
consteval bool validIntID()
{
  auto id = ZuFieldProp::CBOR::GetID<Field>{}().cspan();
  if (!id.length()) return false;
  bool negative = id[0] == '-';
  unsigned i = negative;
  if (i == id.length() || (id[i] == '0' && i + 1 != id.length())) return false;
  uint128_t value = 0;
  for (; i < id.length(); ++i) {
    if (id[i] < '0' || id[i] > '9') return false;
    unsigned digit = id[i] - '0';
    value = value * 10 + digit;
  }
  if (!value || value > uint128_t(UINT64_MAX) + negative) return false;
  return true;
}

struct IntIDValue {
  uint64_t	arg;
  bool		negative;
};

template <typename Field>
consteval IntIDValue intIDValue()
{
  auto id = ZuFieldProp::CBOR::GetID<Field>{}().cspan();
  bool negative = id[0] == '-';
  uint128_t value = 0;
  for (unsigned i = negative; i < id.length(); ++i)
    value = value * 10 + unsigned(id[i] - '0');
  return {uint64_t(negative ? value - 1 : value), negative};
}

template <typename Props, typename Fields>
struct LoadContext {
  ZuBSpan	input;

  LoadContext(ZuBSpan input_) : input(input_) { }

  template <typename Field>
  static bool match(ZuBSpan key)
  {
    Detail::Head head;
    if (!Detail::head(key, head)) return false;
    auto id = ZuFieldProp::CBOR::GetID<Field>{}().cspan();
    using FieldProps = typename Field::Props;
    if constexpr (!ZuFieldProp::CBOR::IsIntID<FieldProps>{}) {
      ZuBSpan text;
      return Detail::content(key, head, text) && head.major == 3 &&
	ZuCSpan(text) == id;
    } else {
	ZuAssert(validIntID<Field>());
	constexpr IntIDValue value = intIDValue<Field>();
	return head.major == (value.negative ? 1 : 0) && head.arg == value.arg;
    }
  }

  bool index(FieldSpans &fields) const
  {
    for (unsigned i = 0, n = fields.length(); i < n; ++i) fields[i] = {};
    ZuBSpan input_ = input;
    Detail::Head head;
    if (!Detail::head(input_, head)) return false;
    constexpr uint64_t Tag = ZuFieldProp::CBOR::GetTag<Props>{};
    if constexpr (Tag != UINT64_MAX) {
      if (!untag(input_, Tag) || !Detail::head(input_, head)) return false;
    }
    if (head.major != 5) return false;
    const uint8_t *p = input_.begin() + head.length;
    const uint8_t *end = input_.end();
    uint64_t n = head.arg;
    for (;;) {
      if (head.ai == 31) {
	if (p >= end || *p == 0xff) {
	  if (p < end) ++p;
	  return p == end;
	}
      } else if (!n--) return p == end;
      ZuBSpan key, value;
      if (!Detail::item({p, unsigned(end - p)}, key)) break;
      p += key.length();
      if (!Detail::item({p, unsigned(end - p)}, value)) break;
      p += value.length();
      ZuUnroll::all<Fields>([&fields, &key, &value]<typename Field>() {
	if (match<Field>(key))
	  fields[ZuTypeIndex<Field, Fields>{}] = value;
      });
    }
    for (unsigned i = 0, l = fields.length(); i < l; ++i) fields[i] = {};
    return false;
  }
};

inline bool untag(ZuBSpan &span, uint64_t tag)
{
  Detail::Head head;
  if (!Detail::head(span, head) || head.major != 6 || head.arg != tag)
    return false;
  span.offset(head.length);
  return true;
}

template <unsigned TypeCode, typename Props, typename T>
inline auto loadValue(ZuBSpan span);
template <typename T, typename Props>
inline T loadUDT(ZuBSpan span);

ZuDerive(LoadVec_, (ZtArray<ZuBSpan, ZtArrayHeapID<"ZfCBOR.LoadVec">>));
template <unsigned TypeCode, typename Props, typename T>
struct LoadVec : public ZuMArray<LoadVec<TypeCode, Props, T>, LoadVec_, T> {
  using Base = ZuMArray<LoadVec, LoadVec_, T>;
  using Base::underlying;

  // Matches ZfASN1: representative sequences are generally small.
  enum { BuiltinSize = 8 };

  ZuBSpan	builtin[BuiltinSize];
  LoadVec_	underlying_ = LoadVec_(&builtin[0], 0, BuiltinSize, false);

  LoadVec(ZuBSpan span) : Base(underlying_) {
    Detail::Head head;
    if (!Detail::head(span, head) || head.major != 4) return;
    const uint8_t *p = span.begin() + head.length;
    const uint8_t *end = span.end();
    uint64_t n = head.arg;
    for (;;) {
      if (head.ai == 31) {
	if (p >= end || *p == 0xff) {
	  if (p < end) ++p;
	  if (p != end) underlying.length(0);
	  return;
	}
      } else if (!n--) {
	if (p != end) underlying.length(0);
	return;
      }
      ZuBSpan item;
      if (!Detail::item({p, unsigned(end - p)}, item)) {
	underlying.length(0);
	return;
      }
      underlying.push(item);
      p += item.length();
    }
  }

  auto get(unsigned i) const & {
    return loadValue<ZuFieldProp::CBOR::GetType<Props, TypeCode>{}, Props, T>(
      underlying[i]);
  }
  template <typename V> void set(unsigned, const V &) { }
};

inline double half(uint16_t bits)
{
  unsigned sign = bits >> 15;
  unsigned exponent = (bits >> 10) & 31;
  unsigned fraction = bits & 1023;
  double v;
  if (exponent == 31)
    v = fraction ? ZuFP<double>::nan() : ZuFP<double>::inf();
  else if (!exponent)
    v = fraction ? double(fraction) * 0x1p-24 : 0.0;
  else
    v = (1.0 + double(fraction) * 0x1p-10) *
      double(uint64_t(1) << (exponent - 15 + 52)) * 0x1p-52;
  return sign ? -v : v;
}

template <typename T>
inline T floatValue(ZuBSpan span)
{
  Detail::Head head;
  if (!Detail::head(span, head) || head.major != 7) return T{};
  if (head.ai == 25) {
    if (span.length() != 3) return T{};
    return T(half((uint16_t(span[1]) << 8) | span[2]));
  }
  if (head.ai == 26) {
    if (span.length() != 5) return T{};
    uint32_t bits = (uint32_t(span[1]) << 24) | (uint32_t(span[2]) << 16) |
      (uint32_t(span[3]) << 8) | span[4];
    float v = ZuPun<uint32_t, float>(bits).out;
    if (ZuFP<float>::nan(v)) return ZuFP<T>::nan();
    T value = T(v);
    if (!ZuFP<float>::inf(v) && !ZuFP<float>::inf(-v) &&
	(ZuFP<T>::inf(value) || ZuFP<T>::inf(-value))) return T{};
    return value;
  }
  if (head.ai == 27) {
    if (span.length() != 9) return T{};
    uint64_t bits = 0;
    for (unsigned i = 1; i < 9; ++i) bits = (bits << 8) | span[i];
    double v = ZuPun<uint64_t, double>(bits).out;
    if (ZuFP<double>::nan(v)) return ZuFP<T>::nan();
    T value = T(v);
    if (!ZuFP<double>::inf(v) && !ZuFP<double>::inf(-v) &&
	(ZuFP<T>::inf(value) || ZuFP<T>::inf(-value))) return T{};
    return value;
  }
  return T{};
}

template <typename T>
inline T bignumValue(ZuBSpan span, Detail::Head tag)
{
  if (tag.arg != 2 && tag.arg != 3) return T{};
  span.offset(tag.length);
  Detail::Head bytes;
  ZuBSpan content;
  if (!Detail::content(span, bytes, content) || bytes.major != 2) return T{};
  uint128_t value = 0;
  unsigned first = 0;
  while (first < content.length() && !content[first]) ++first;
  if (content.length() - first > sizeof(value)) return T{};
  for (; first < content.length(); ++first) value = (value << 8) | content[first];
  if (tag.arg == 2) {
    if constexpr (ZuTraits<T>::IsSigned) {
      uint128_t maximum = uint128_t(T(-1)) >> 1;
      return value <= maximum ? T(value) : T{};
    } else return value <= uint128_t(T(-1)) ? T(value) : T{};
  }
  if constexpr (ZuTraits<T>::IsSigned) {
    uint128_t maximum = uint128_t(T(-1)) >> 1;
    return value <= maximum ? T(-T(value) - T(1)) : T{};
  }
  return T{};
}

inline bool pair(ZuBSpan span, uint64_t tag, ZuBSpan &first, ZuBSpan &second)
{
  if (!untag(span, tag)) return false;
  Detail::Head head;
  if (!Detail::head(span, head) || head.major != 4 || head.arg != 2) return false;
  const uint8_t *p = span.begin() + head.length;
  const uint8_t *end = span.end();
  if (!Detail::item({p, unsigned(end - p)}, first)) return false;
  p += first.length();
  if (!Detail::item({p, unsigned(end - p)}, second)) return false;
  return p + second.length() == end;
}

inline bool pow10(int n, int128_t &out)
{
  if (n < 0 || n > 38) return false;
  out = 1;
  while (n--) out *= 10;
  return true;
}

inline ZuFixed fixedValue(ZuBSpan span);
inline ZuDecimal decimalValue(ZuBSpan span);
inline ZuTime timeValue(ZuBSpan span);
inline ZuDateTime dateTimeValue(ZuBSpan span);

template <unsigned TypeCode, typename Props, typename T>
inline auto loadValue(ZuBSpan span)
{
  ZuAssert(ZfFieldTC::IsVec<TypeCode>{} ||
    !ZuFieldProp::CBOR::IsSimple<Props>{} || TypeCode == ZfFieldTC::UInt8);
  if constexpr (ZfFieldTC::IsVec<TypeCode>{}) {
    enum { ElemCode = ZfFieldTC::Elem<TypeCode>{} };
    using ElemProps = ZuFieldProp::CBOR::GetElemProps<Props>;
    using Elem = ZfFieldTC::Type<ElemCode>;
    constexpr uint64_t Tag = ZuFieldProp::CBOR::GetTag<Props>{};
    if constexpr (Tag != UINT64_MAX)
      if (!untag(span, Tag)) return LoadVec<ElemCode, ElemProps, Elem>(ZuBSpan{});
    return LoadVec<ElemCode, ElemProps, Elem>(span);
  } else {
  Detail::Head head;
  auto result = scan(span);
  if (!Detail::head(span, head) || !result || result.offset != span.length())
    return T{};
  constexpr uint64_t Tag = ZuFieldProp::CBOR::GetTag<Props>{};
  if constexpr (Tag != UINT64_MAX && TypeCode != ZfFieldTC::UDT) {
    if (!untag(span, Tag) || !Detail::head(span, head)) return T{};
  } else if constexpr (TypeCode != ZfFieldTC::UDT &&
      TypeCode != ZfFieldTC::Int8 && TypeCode != ZfFieldTC::Int16 &&
      TypeCode != ZfFieldTC::Int32 && TypeCode != ZfFieldTC::Int64 &&
      TypeCode != ZfFieldTC::Int128 && TypeCode != ZfFieldTC::UInt8 &&
      TypeCode != ZfFieldTC::UInt16 && TypeCode != ZfFieldTC::UInt32 &&
      TypeCode != ZfFieldTC::UInt64 && TypeCode != ZfFieldTC::UInt128 &&
      TypeCode != ZfFieldTC::Fixed && TypeCode != ZfFieldTC::Decimal &&
      TypeCode != ZfFieldTC::Time && TypeCode != ZfFieldTC::DateTime) {
    if (head.major == 6) return T{};
  }
  if constexpr (!ZfFieldTC::IsVec<TypeCode>{} && TypeCode != ZfFieldTC::UDT &&
      !(TypeCode == ZfFieldTC::UInt8 && ZuFieldProp::CBOR::IsSimple<Props>{})) {
    if (head.major == 7 && head.ai == 22) return ZuCmp<T>::null();
  }
  if constexpr (TypeCode == ZfFieldTC::CString ||
      TypeCode == ZfFieldTC::String) {
    return Detail::stringValue<T, 3>(span);
  } else if constexpr (TypeCode == ZfFieldTC::Bytes) {
    return Detail::stringValue<T, 2>(span);
  } else if constexpr (TypeCode == ZfFieldTC::Bool) {
    if (head.major != 7 || (head.ai != 20 && head.ai != 21)) return T{};
    return head.ai == 21;
  } else if constexpr (TypeCode == ZfFieldTC::UInt8 &&
      ZuFieldProp::CBOR::IsSimple<Props>{}) {
    if (head.major != 7 || head.ai > 24) return T{};
    return T(head.arg);
  } else if constexpr (TypeCode == ZfFieldTC::Float) {
    return floatValue<T>(span);
  } else if constexpr (TypeCode == ZfFieldTC::Fixed) {
    return fixedValue(span);
  } else if constexpr (TypeCode == ZfFieldTC::Decimal) {
    return decimalValue(span);
  } else if constexpr (TypeCode == ZfFieldTC::Time) {
    return timeValue(span);
  } else if constexpr (TypeCode == ZfFieldTC::DateTime) {
    return dateTimeValue(span);
  } else if constexpr (TypeCode == ZfFieldTC::UDT) {
    return loadUDT<T, Props>(span);
  } else if constexpr (
      TypeCode == ZfFieldTC::Int8 ||
      TypeCode == ZfFieldTC::Int16 ||
      TypeCode == ZfFieldTC::Int32 ||
      TypeCode == ZfFieldTC::Int64 ||
      TypeCode == ZfFieldTC::Int128 ||
      TypeCode == ZfFieldTC::UInt8 ||
      TypeCode == ZfFieldTC::UInt16 ||
      TypeCode == ZfFieldTC::UInt32 ||
      TypeCode == ZfFieldTC::UInt64 ||
      TypeCode == ZfFieldTC::UInt128) {
    if (head.major == 6) return bignumValue<T>(span, head);
    if constexpr (ZuTraits<T>::IsSigned) {
      uint128_t maximum = uint128_t(T(-1)) >> 1;
      if (head.major == 0 && uint128_t(head.arg) <= maximum)
	return T(head.arg);
      if (head.major == 1 && uint128_t(head.arg) <= maximum)
	return T(-T(head.arg) - T(1));
    } else if (head.major == 0 && uint128_t(head.arg) <= uint128_t(T(-1)))
      return T(head.arg);
    return T{};
  } else {
    return T{};
  }
  }
}

inline ZuFixed fixedValue(ZuBSpan span)
{
  ZuBSpan exponent, mantissa;
  if (!pair(span, 4, exponent, mantissa)) return {};
  int64_t e = loadValue<ZfFieldTC::Int64, ZuTypeList<>, int64_t>(exponent);
  int128_t m = loadValue<ZfFieldTC::Int128, ZuTypeList<>, int128_t>(mantissa);
  if (e > 0 || e < -255 || m < ZuFixedMin || m > ZuFixedMax) return {};
  return {int64_t(m), unsigned(-e)};
}

inline ZuDecimal decimalValue(ZuBSpan span)
{
  ZuBSpan exponent, mantissa;
  if (!pair(span, 4, exponent, mantissa)) return {};
  int64_t e = loadValue<ZfFieldTC::Int64, ZuTypeList<>, int64_t>(exponent);
  int128_t m = loadValue<ZfFieldTC::Int128, ZuTypeList<>, int128_t>(mantissa);
  if (e >= -18) {
    if (e > 20) return m ? ZuDecimal{} : ZuDecimal::Unscaled{0};
    int128_t scale, value;
    if (!pow10(int(18 + e), scale) || ZuIntrin::mul(m, scale, &value) ||
	value < ZuDecimal::minimum() || value > ZuDecimal::maximum()) return {};
    return ZuDecimal::Unscaled{value};
  }
  uint64_t n = uint64_t(-(e + 18));
  if (n > 38) return m ? ZuDecimal{} : ZuDecimal::Unscaled{0};
  int128_t scale;
  if (!pow10(int(n), scale) || m % scale) return {};
  return ZuDecimal::Unscaled{m / scale};
}

inline ZuTime timeValue(ZuBSpan span)
{
  Detail::Head tag;
  if (!Detail::head(span, tag) || tag.major != 6) return {};
  if (tag.arg == 0) {
    span.offset(tag.length);
    Detail::Head head;
    ZuBSpan text;
    if (!Detail::content(span, head, text) || head.major != 3) return {};
    return ZuDateTime{ZuDateTimeScan::ISO{}, ZuCSpan{text}}.as_time();
  }
  if (tag.arg != 1 || !untag(span, 1)) return {};
  Detail::Head head;
  if (!Detail::head(span, head)) return {};
  if (head.major == 0 || head.major == 1)
    return loadValue<ZfFieldTC::Int64, ZuTypeList<>, int64_t>(span);
  if (head.major != 7 || (head.ai != 25 && head.ai != 26 && head.ai != 27))
    return {};
  double value = floatValue<double>(span);
  if (ZuFP<double>::nan(value) || ZuFP<double>::inf(value) ||
      ZuFP<double>::inf(-value)) return {};
  if (value >= double(ZuCmp<int64_t>::maximum()))
    return {ZuCmp<int64_t>::maximum() - 1, 999999999};
  if (value <= double(ZuCmp<int64_t>::minimum()))
    return {ZuCmp<int64_t>::minimum() + 1, 0};
  return ZuTime{(long double)value};
}

inline ZuDateTime dateTimeValue(ZuBSpan span)
{
  Detail::Head tag;
  if (!Detail::head(span, tag) || tag.major != 6) return {};
  if (tag.arg == 1) return ZuDateTime{timeValue(span)};
  if (tag.arg != 0 || !untag(span, 0)) return {};
  Detail::Head head;
  ZuBSpan text;
  if (!Detail::content(span, head, text) || head.major != 3) return {};
  return ZuDateTime{ZuDateTimeScan::ISO{}, ZuCSpan{text}};
}

struct AsObject;

} // namespace ZfCBOR

ZfCBOR::AsObject ZfCBOR_Fmt(...);

namespace ZfCBOR {

template <typename O>
using As = decltype(ZfCBOR_Fmt(ZuDeclVal<O *>()));

template <typename T, typename Props>
inline T loadUDT(ZuBSpan span)
{
  return typename As<T>::template Handler<T, ZuFacet::CBOR, Props>{
    ZuFalse{}, span}.ctor();
}

template <typename S>
inline void writeHead(S &s, unsigned major, uint64_t value)
{
  if (value < 24) {
    s << char((major << 5) | value);
  } else if (value <= UINT8_MAX) {
    s << char((major << 5) | 24) << char(value);
  } else if (value <= UINT16_MAX) {
    s << char((major << 5) | 25) << char(value >> 8) << char(value);
  } else if (value <= UINT32_MAX) {
    s << char((major << 5) | 26) << char(value >> 24) << char(value >> 16)
      << char(value >> 8) << char(value);
  } else {
    s << char((major << 5) | 27);
    for (int i = 7; i >= 0; --i) s << char(value >> (i << 3));
  }
}

template <typename S, typename Field>
inline void saveKey(S &s)
{
  using Props = typename Field::Props;
  auto id = ZuFieldProp::CBOR::GetID<Field>{}().cspan();
  if constexpr (ZuFieldProp::CBOR::IsIntID<Props>{}) {
    ZuAssert(validIntID<Field>());
    constexpr IntIDValue value = intIDValue<Field>();
    writeHead(s, value.negative ? 1 : 0, value.arg);
  } else {
    writeHead(s, 3, id.length());
    s << id;
  }
}

struct KeyOrderValue {
  uint8_t	n = 0;
  uint8_t	data[34]{};

  constexpr bool operator >(const KeyOrderValue &r) const {
    if (n != r.n) return n > r.n;
    for (unsigned i = 0; i < n; ++i)
      if (data[i] != r.data[i]) return data[i] > r.data[i];
    return false;
  }
};

template <typename Field>
struct CBOROrder {
  static consteval KeyOrderValue value() {
    KeyOrderValue value;
    auto id = ZuFieldProp::CBOR::GetID<Field>{}().cspan();
    using Props = typename Field::Props;
    if constexpr (ZuFieldProp::CBOR::IsIntID<Props>{}) {
      constexpr IntIDValue idValue = intIDValue<Field>();
      uint64_t n = idValue.arg;
      unsigned major = idValue.negative ? 1 : 0;
      if (n < 24) value.data[value.n++] = uint8_t((major << 5) | n);
      else if (n <= UINT8_MAX) {
	value.data[value.n++] = uint8_t((major << 5) | 24);
	value.data[value.n++] = uint8_t(n);
      } else if (n <= UINT16_MAX) {
	value.data[value.n++] = uint8_t((major << 5) | 25);
	value.data[value.n++] = uint8_t(n >> 8);
	value.data[value.n++] = uint8_t(n);
      } else if (n <= UINT32_MAX) {
	value.data[value.n++] = uint8_t((major << 5) | 26);
	for (int i = 3; i >= 0; --i) value.data[value.n++] = uint8_t(n >> (i << 3));
      } else {
	value.data[value.n++] = uint8_t((major << 5) | 27);
	for (int i = 7; i >= 0; --i) value.data[value.n++] = uint8_t(n >> (i << 3));
      }
    } else {
      unsigned n = id.length();
      if (n < 24) value.data[value.n++] = uint8_t(0x60 | n);
      else value.data[value.n++] = 0x78, value.data[value.n++] = uint8_t(n);
      for (unsigned i = 0; i < n; ++i) value.data[value.n++] = id[i];
    }
    return value;
  }
  static constexpr KeyOrderValue V = value();
  constexpr const KeyOrderValue &operator ()() const { return V; }
};

template <typename S, unsigned TypeCode, typename Props, typename T>
inline void saveValue(S &s, const T &v);

struct AsObject {
  template <typename O_, typename Facet, typename Props = ZuTypeList<>>
  struct Handler {
    using O = O_;
    using AllFields = ZuFields<O, Facet>;
    using LoadFields = ZuTypeGrep<ZfFieldFilter::Load, AllFields>;
    using CtorFields = ZuTypeGrep<ZfFieldFilter::Ctor, AllFields>;
    using InitFields = ZuTypeGrep<ZfFieldFilter::Init, AllFields>;

    template <typename S>
    static void save(S &s, const O &o) {
      using SaveFields_ = ZuTypeGrep<ZfFieldFilter::Save, AllFields>;
      using SaveFields = ZuTypeSort<CBOROrder, SaveFields_>;
      constexpr uint64_t Tag = ZuFieldProp::CBOR::GetTag<Props>{};
      unsigned n = 0;
      ZuUnroll::all<SaveFields>([&o, &n]<typename Field>() {
	using FieldProps = typename Field::Props;
	if constexpr (!ZuFieldProp::CBOR::GetOptional<FieldProps>{}) ++n;
	else if (!ZuNull(Field::get(o))) ++n;
      });
      if constexpr (Tag != UINT64_MAX) writeHead(s, 6, Tag);
      writeHead(s, 5, n);
      ZuUnroll::all<SaveFields>([&s, &o]<typename Field>() {
	using FieldProps = typename Field::Props;
	enum { TypeCode = ZuFieldProp::CBOR::GetType<
	  FieldProps, Field::Type::Code>{} };
	if constexpr (!ZuFieldProp::CBOR::GetOptional<FieldProps>{}) {
	  saveKey<S, Field>(s);
	  saveValue<S, TypeCode, FieldProps>(s, Field::get(o));
	} else if (!ZuNull(Field::get(o))) {
	  saveKey<S, Field>(s);
	  saveValue<S, TypeCode, FieldProps>(s, Field::get(o));
	}
      });
    }

    ZuBSpan	input;

    template <typename Field>
    auto loadField(const FieldSpans &fields) const {
      enum { I = ZuTypeIndex<Field, AllFields>{} };
      enum { TypeCode = ZuFieldProp::CBOR::GetType<
	typename Field::Props, Field::Type::Code>{} };
      using T = typename Field::T;
      using R = decltype(
	loadValue<TypeCode, typename Field::Props, T>(ZuBSpan{}));
      if (fields[I]) return loadValue<TypeCode, typename Field::Props, T>(fields[I]);
      if constexpr (ZfFieldTC::IsVec<TypeCode>{}) return R(ZuBSpan{});
      else return R{Field::deflt()};
    }

    template <typename ...Field>
    struct Ctor {
      template <typename ...Args>
      static O ctor(const Handler &handler, const FieldSpans &fields,
	  Args &&...args) {
	return O(ZuFwd<Args>(args)..., handler.template loadField<Field>(fields)...);
      }
      template <typename ...Args>
      static O *alloc(const Handler &handler, const FieldSpans &fields,
	  Args &&...args) {
	return new O(ZuFwd<Args>(args)...,
	  handler.template loadField<Field>(fields)...);
      }
      template <typename ...Args>
      static void new_(void *ptr, const Handler &handler,
	  const FieldSpans &fields, Args &&...args) {
	new (ptr) O(ZuFwd<Args>(args)...,
	  handler.template loadField<Field>(fields)...);
      }
    };

    template <typename TopLevel>
    Handler(TopLevel, ZuBSpan input_) : input(input_) { }

    template <typename ...Args>
    O ctor(Args &&...args) const {
      auto index = ZtScratch(FieldSpans, AllFields::N, AllFields::N);
      LoadContext<Props, AllFields>{input}.index(index);
      O o = ZuTypeApply<Ctor, CtorFields>::ctor(
	*this, index, ZuFwd<Args>(args)...);
      ZuUnroll::all<InitFields>([this, &index, &o]<typename Field>() {
	Field::set(o, this->template loadField<Field>(index));
      });
      return o;
    }
    template <typename ...Args>
    O *alloc(Args &&...args) const {
      auto index = ZtScratch(FieldSpans, AllFields::N, AllFields::N);
      LoadContext<Props, AllFields>{input}.index(index);
      O *o = ZuTypeApply<Ctor, CtorFields>::alloc(
	*this, index, ZuFwd<Args>(args)...);
      ZuUnroll::all<InitFields>([this, &index, o]<typename Field>() {
	Field::set(*o, this->template loadField<Field>(index));
      });
      return o;
    }
    template <typename ...Args>
    void new_(void *ptr, Args &&...args) const {
      auto index = ZtScratch(FieldSpans, AllFields::N, AllFields::N);
      LoadContext<Props, AllFields>{input}.index(index);
      ZuTypeApply<Ctor, CtorFields>::new_(
	ptr, *this, index, ZuFwd<Args>(args)...);
      O &o = *static_cast<O *>(ptr);
      ZuUnroll::all<InitFields>([this, &index, &o]<typename Field>() {
	Field::set(o, this->template loadField<Field>(index));
      });
    }
    void load(O &o) const {
      auto index = ZtScratch(FieldSpans, AllFields::N, AllFields::N);
      LoadContext<Props, AllFields>{input}.index(index);
      ZuUnroll::all<LoadFields>([this, &index, &o]<typename Field>() {
	Field::set(o, this->template loadField<Field>(index));
      });
    }
  };
};

template <unsigned ElemCode>
struct AsArray {
  template <typename O_, typename Facet, typename Props = ZuTypeList<>>
  struct Handler {
    using O = O_;
    using Elem = ZuDecay<decltype(ZuDeclVal<const O &>()[0])>;
    using ElemProps = ZuFieldProp::CBOR::GetElemProps<Props>;
    using Vec = LoadVec<ElemCode, ElemProps, Elem>;

    Vec	vec;

    static ZuBSpan untag_(ZuBSpan span) {
      constexpr uint64_t Tag = ZuFieldProp::CBOR::GetTag<Props>{};
      if constexpr (Tag != UINT64_MAX)
	if (!untag(span, Tag)) return {};
      return span;
    }

    template <typename TopLevel>
    Handler(TopLevel, ZuBSpan span) : vec(untag_(span)) { }

    template <typename S>
    static void save(S &s, const O &o) {
      constexpr uint64_t Tag = ZuFieldProp::CBOR::GetTag<Props>{};
      if constexpr (Tag != UINT64_MAX) writeHead(s, 6, Tag);
      unsigned n = ZuTraits<O>::length(o);
      writeHead(s, 4, n);
      for (unsigned i = 0; i < n; ++i)
      saveValue<S, ZuFieldProp::CBOR::GetType<ElemProps, ElemCode>{},
	ElemProps>(s, o[i]);
    }
    template <typename ...Args>
    O ctor(Args &&...) const { return O(vec); }
    template <typename ...Args>
    O *alloc(Args &&...) const { return new O(vec); }
    template <typename ...Args>
    void new_(void *ptr, Args &&...) const { new (ptr) O(vec); }
    void load(O &o) const { o = vec; }
  };
};

template <typename S, typename T>
inline void saveFloat(S &s, T value)
{
  if (ZuFP<T>::nan(value)) {
    s << char(0xf9) << char(0x7e) << char(0);
    return;
  }
  float f = value;
  uint32_t fbits = ZuPun<float, uint32_t>(f).out;
  unsigned sign = fbits >> 16 & 0x8000;
  int exponent = int((fbits >> 23) & 0xff) - 127 + 15;
  uint16_t hbits;
  if (exponent <= 0) {
    if (exponent < -10) hbits = sign;
    else {
      uint32_t mantissa = (fbits & 0x7fffff) | 0x800000;
      unsigned shift = unsigned(14 - exponent);
      hbits = uint16_t(sign | ((mantissa + (uint32_t(1) << (shift - 1))) >> shift));
    }
  } else if (exponent >= 31) {
    hbits = uint16_t(sign | 0x7c00 | ((fbits & 0x7fffff) ? 1 : 0));
  } else {
    hbits = uint16_t(sign | (unsigned(exponent) << 10) |
      ((fbits + 0x1000) >> 13 & 0x3ff));
  }
  float hf = float(half(hbits));
  if (T(hf) == value &&
      (!(value == 0) || ((ZuPun<float, uint32_t>(hf).out ^ fbits) >> 31) == 0)) {
    s << char(0xf9) << char(hbits >> 8) << char(hbits);
    return;
  }
  if (T(f) == value) {
    s << char(0xfa);
    for (int i = 3; i >= 0; --i) s << char(fbits >> (i << 3));
    return;
  }
  double d = value;
  uint64_t bits = ZuPun<double, uint64_t>(d).out;
  s << char(0xfb);
  for (int i = 7; i >= 0; --i) s << char(bits >> (i << 3));
}

template <typename S>
inline void save2(S &s, unsigned value)
{
  s << char('0' + value / 10) << char('0' + value % 10);
}

template <typename S>
inline void save4(S &s, unsigned value)
{
  s << char('0' + value / 1000) << char('0' + value / 100 % 10)
    << char('0' + value / 10 % 10) << char('0' + value % 10);
}

template <typename S>
inline void saveDateTime(S &s, const ZuDateTime &value)
{
  int year, month, day, hour, minute, second, nsec;
  value.ymd(year, month, day);
  value.hmsn(hour, minute, second, nsec);
  if (year < 0) {
    year = 0, month = 1, day = 1, hour = minute = second = nsec = 0;
  } else if (year > 9999) {
    year = 9999, month = 12, day = 31, hour = minute = second = 59;
    nsec = 999999999;
  }
  unsigned ndp = 9;
  unsigned fraction = nsec;
  while (ndp && !(fraction % 10)) fraction /= 10, --ndp;
  writeHead(s, 6, 0);
  writeHead(s, 3, 20 + (ndp ? ndp + 1 : 0));
  save4(s, year);
  s << '-' ; save2(s, month); s << '-'; save2(s, day);
  s << 'T'; save2(s, hour); s << ':'; save2(s, minute); s << ':';
  save2(s, second);
  if (ndp) {
    s << '.';
    unsigned div = 100000000;
    while (ndp--) {
      s << char('0' + nsec / div);
      nsec %= div;
      div /= 10;
    }
  }
  s << 'Z';
}

template <typename S>
inline void saveBignum(S &s, unsigned tag, uint128_t value)
{
  uint8_t bytes[sizeof(value)];
  unsigned n = sizeof(bytes);
  do bytes[--n] = uint8_t(value), value >>= 8; while (value);
  writeHead(s, 6, tag);
  writeHead(s, 2, sizeof(bytes) - n);
  s << ZuCSpan{bytes + n, sizeof(bytes) - n};
}

template <typename S, typename T>
inline void saveInt(S &s, T value)
{
  if constexpr (ZuTraits<T>::IsSigned) {
    if (value < 0) {
      uint128_t n = uint128_t(-(value + 1));
      if (n <= UINT64_MAX) writeHead(s, 1, uint64_t(n));
      else saveBignum(s, 3, n);
    } else {
      uint128_t n = value;
      if (n <= UINT64_MAX) writeHead(s, 0, uint64_t(n));
      else saveBignum(s, 2, n);
    }
  } else {
    uint128_t n = value;
    if (n <= UINT64_MAX) writeHead(s, 0, uint64_t(n));
    else saveBignum(s, 2, n);
  }
}

template <typename S, unsigned TypeCode, typename Props, typename T>
inline void saveValue(S &s, const T &v)
{
  ZuAssert(ZfFieldTC::IsVec<TypeCode>{} ||
    !ZuFieldProp::CBOR::IsSimple<Props>{} || TypeCode == ZfFieldTC::UInt8);
  constexpr uint64_t Tag = ZuFieldProp::CBOR::GetTag<Props>{};
  if constexpr (Tag != UINT64_MAX && TypeCode != ZfFieldTC::UDT)
    writeHead(s, 6, Tag);
  if constexpr (TypeCode == ZfFieldTC::CString ||
      TypeCode == ZfFieldTC::String) {
    ZuCSpan value{v};
    writeHead(s, 3, value.length());
    s << value;
  } else if constexpr (TypeCode == ZfFieldTC::Bytes) {
    ZuBSpan value{v};
    writeHead(s, 2, value.length());
    s << ZuCSpan(value);
  } else if constexpr (TypeCode == ZfFieldTC::Bool) {
    s << char(v ? 0xf5 : 0xf4);
  } else if constexpr (TypeCode == ZfFieldTC::UInt8 &&
      ZuFieldProp::CBOR::IsSimple<Props>{}) {
    if (v < 24) s << char(0xe0 | v);
    else s << char(0xf8) << char(v);
  } else if constexpr (TypeCode == ZfFieldTC::Float) {
    saveFloat(s, v);
  } else if constexpr (TypeCode == ZfFieldTC::Fixed) {
    if (!*v) { s << char(0xf6); return; }
    writeHead(s, 6, 4);
    writeHead(s, 4, 2);
    saveInt(s, -int64_t(v.ndp));
    saveInt(s, v.mantissa);
  } else if constexpr (TypeCode == ZfFieldTC::Decimal) {
    if (!*v) { s << char(0xf6); return; }
    int128_t mantissa = v.value;
    int64_t exponent = -18;
    while (!(mantissa % 10) && exponent < 18) {
      mantissa /= 10;
      ++exponent;
    }
    writeHead(s, 6, 4);
    writeHead(s, 4, 2);
    saveInt(s, exponent);
    saveInt(s, mantissa);
  } else if constexpr (TypeCode == ZfFieldTC::Time) {
    if (!*v) { s << char(0xf6); return; }
    writeHead(s, 6, 1);
    int128_t nanos = v.nanosecs();
    if (!(nanos % 1000000000)) saveInt(s, v.as_time_t());
    else saveFloat(s, double(v.as_fp()));
  } else if constexpr (TypeCode == ZfFieldTC::DateTime) {
    if (!*v) { s << char(0xf6); return; }
    saveDateTime(s, v);
  } else if constexpr (ZfFieldTC::IsVec<TypeCode>{}) {
    enum { ElemCode = ZfFieldTC::Elem<TypeCode>{} };
    using ElemProps = ZuFieldProp::CBOR::GetElemProps<Props>;
    unsigned n = ZuTraits<T>::length(v);
    writeHead(s, 4, n);
    for (unsigned i = 0; i < n; ++i)
      saveValue<S, ZuFieldProp::CBOR::GetType<ElemProps, ElemCode>{},
	ElemProps>(s, v[i]);
  } else if constexpr (
      TypeCode == ZfFieldTC::Int8 || TypeCode == ZfFieldTC::Int16 ||
      TypeCode == ZfFieldTC::Int32 || TypeCode == ZfFieldTC::Int64 ||
      TypeCode == ZfFieldTC::Int128 || TypeCode == ZfFieldTC::UInt8 ||
      TypeCode == ZfFieldTC::UInt16 || TypeCode == ZfFieldTC::UInt32 ||
      TypeCode == ZfFieldTC::UInt64 || TypeCode == ZfFieldTC::UInt128) {
    saveInt(s, v);
  } else if constexpr (TypeCode == ZfFieldTC::UDT) {
    using Handler = typename As<T>::template Handler<T, ZuFacet::CBOR, Props>;
    Handler::save(s, v);
  }
}

template <typename Facet = ZuFacet::CBOR, typename S, typename O>
inline S &save(S &s, const O &v)
{
  using Handler = typename As<O>::template Handler<O, Facet>;
  Handler::save(s, v);
  return s;
}

template <typename O, typename Facet = ZuFacet::CBOR>
auto handler(ZuBSpan span) {
  return typename As<O>::template Handler<O, Facet>{ZuTrue{}, span};
}

} // namespace ZfCBOR

#endif /* ZfCBOR_HH */
