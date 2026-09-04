//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// ZfStruct ASN.1 load/save
// - maps ZfStruct-defined data structures to/from ASN.1
// - compile-time encoding/decoding

// Generic mapping of C++ structs to ASN.1 requires expressing field encodings
// as a composition of (see recap below for context):
// - ASN1::Fmt, controlling:
//   - position within the containing ASN.1 Sequence or Set
//   - optional tag overrides
//   - optional nesting within a subsidiary Sequence or Set
//   - optional tag overrides for the nested encoding
// - ASN1::Type - optional ASN.1 encoding overrides (e.g. Bytes as Integer)
// - ASN1::ElemType - optional ASN.1 encoding overrides for array elements
//   - distinct from overrides for the array itself
// - ASN1::Optional - optional values that can be omitted

// ASN.1 recap
// - overall structure is a sequence of Tag/Length/Value (TLV) elements
// - BER encoding is byte-aligned, big-endian ("big end first")
// - DER is BER with constraints to ensure deterministically reproducible
//   byte-equivalent encoding: it is constrained by requiring minimal
//   value lengths, ordering of elements within Sets and constrained
//   encoding options for types such as date/time
// - Sequence and Set elements both contain nested sequences of
//   heterogeneous TLV elements; OctetString and BitString are also
//   commonly used for nesting opaque variant data, typically
//   discriminated by an OID in an outer containing Sequence
// - Sets typically contain context-specific elements notated [0], [1], etc.
//   where the number is an index key (the field identifier within an object)
// - Sequences are ordered, typically representing arrays or objects with
//   fields identified by sequence position; optional fields are typically
//   encoded in an optional trailing Set within the Sequence
// - DER of Sets requires ascending sorted tag IDs, or ascending sorted
//   tag types if context tags are not used; values are sorted ascending
//   by byte encoding as tie-breaker; this is so that DER-encoded data
//   can be reliably matched without ambiguity
// - ContextSpecific (ASN.1 terminology, would be better termed Indexed),
//   is an element annotated with [N], where N is a context number;
//   the Constructed bit is set or clear to align with the contained element;
//   numbers above 0x1e are encoded as 0x1f (together with other potential
//   flag bits including constructed and context-specific) followed by
//   the tag ID using base-128 - additional bytes as minimally required by
//   the number, all of which except the final byte have the MSB set; the
//   ID is big-endian encoded, 7 bits per byte; the final byte has a clear MSB;
//   this is the same encoding used for elements of an OID
// - lengths are single-byte if < 128, otherwise the length is minimally
//   encoded as unsigned big-endian preceded by the first byte that contains
//   the "length of the length", with MSB set

#ifndef ZfASN1_HH
#define ZfASN1_HH

#ifndef ZfLib_HH
#include <zlib/ZfLib.hh>
#endif

#include <zlib/ZuDecimal.hh>
#include <zlib/ZuFixed.hh>
#include <zlib/ZuMArray.hh>
#include <zlib/ZmScratch.hh>

#include <zlib/ZfStruct.hh>
#include <zlib/ZfJSON.hh>
#include <zlib/ZtHexDump.hh>

ZuStructFacet(ASN1); // canonical ASN.1 facet, others can be defined

namespace ZfASN1 {

// ZfASN1::Spec is a subsidiary namespace that apps can import to access
// ASN.1 encoding-related constants and functions without dragging
// in the rest of ZfASN1
namespace Encoding {

enum {
  Boolean		= 0x01,	// single byte 0x00 or 0xff
  Integer		= 0x02,	// signed minimal big-endian number
  BitString		= 0x03,	// bit string, with leading "unused bits" byte
  OctetString		= 0x04,	// bytes
  Null			= 0x05,	// should always be zero-length
  OID			= 0x06,	// bytes (sequence of base-128 numbers)
  Real			= 0x09,	// IEEE 754 binary (length determines size)
  Enumerated		= 0x0a,	// unsigned minimal big-endian number
  UTF8String		= 0x0c,	// UTF8 string
  Sequence		= 0x30,	// ordered, composed of nested TLV elements
  Set			= 0x31,	// unordered, '' (but DER requires sorting)
  PrintableString	= 0x13,	// ASCII string
  T61String		= 0x14,	// ASCII string
  IA5String		= 0x16,	// ASCII string
  UTCTime		= 0x17,	// YYMMDDHHMMSSZ
  GeneralizedTime	= 0x18,	// YYYYMMDDHHMMSS[.(fraction)]Z
  UniversalString	= 0x1c,	// UTF32 string
  BmpString		= 0x1e,	// UTF16 string
  Constructed		= 0x20	// Set, Sequence
};

// tag classes - [XI][UACP] - per X.680 / X.690
// - X is eXplicit
// - I is Implicit
// - U is Universal
// - A is Application
// - C is Context-specific
// - P is Private
// IU with the primitive ASN type is the default
enum {
  XUTag = 0x0, XATag, XCTag, XPTag,	// explicit tags
  IUTag = 0x4, IATag, ICTag, IPTag,	// implicit tags
  Class = 0x3, Implicit = 0x4
};

// tagged coding, e.g.
// tagU()   is IU, primitive, using ASN Type (default)
// tag(0)   is XC, "[0]"
// tagI(0)  is IC, "[0] IMPLICIT"
// tagXA(0) is XA, "[APPLICATION 0]"
constexpr uint32_t tag(uint16_t n) { return (uint32_t(XCTag)<<28) | n; }
constexpr uint32_t tag(uint16_t n, uint8_t i) {
  return (uint32_t(XCTag)<<28) | (uint32_t(i)<<16) | n;
}
constexpr uint32_t tagI(uint16_t n) { return (uint32_t(ICTag)<<28) | n; }
constexpr uint32_t tagXA(uint16_t n) { return (uint32_t(XATag)<<28) | n; }
constexpr uint32_t tagXA(uint16_t n, uint8_t i) {
  return (uint32_t(XATag)<<28) | (uint32_t(i)<<16) | n;
}
constexpr uint32_t tagXP(uint16_t n) { return (uint32_t(XPTag)<<28) | n; }
constexpr uint32_t tagXP(uint16_t n, uint8_t i) {
  return (uint32_t(XPTag)<<28) | (uint32_t(i)<<16) | n;
}
constexpr uint32_t tagU(uint16_t n) { return (uint32_t(IUTag)<<28) | n; }
constexpr uint32_t tagIA(uint16_t n) { return (uint32_t(IATag)<<28) | n; }
constexpr uint32_t tagIP(uint16_t n) { return (uint32_t(IPTag)<<28) | n; }
constexpr uint32_t tagU() { return uint32_t(IUTag)<<28; }

constexpr bool tagImplicit(uint32_t tag) { return (tag>>28) & Implicit; }
constexpr uint8_t tagClass(uint32_t tag) { return (tag>>28) & Class; }
constexpr uint16_t tagNumber(uint32_t tag) { return tag; }
constexpr uint8_t tagInner(uint32_t tag) { return (tag>>16) & 0xff; }

// nested coding
constexpr uint32_t seq(uint32_t pos) { return (uint32_t(1)<<29) | pos; }
constexpr uint32_t set_(uint32_t pos) { return (uint32_t(2)<<29) | pos; }
constexpr uint32_t str(uint32_t pos) { return (uint32_t(3)<<29) | pos; }
constexpr uint32_t bstr(uint32_t pos) { return (uint32_t(4)<<29) | pos; }
constexpr uint32_t seq(uint8_t id, uint32_t pos) {
  return (uint32_t(1)<<29) | (uint32_t(id)<<21) | pos;
}
constexpr uint32_t set_(uint8_t id, uint32_t pos) {
  return (uint32_t(2)<<29) | (uint32_t(id)<<21) | pos;
}
constexpr uint32_t str(uint8_t id, uint32_t pos) {
  return (uint32_t(3)<<29) | (uint32_t(id)<<21) | pos;
}
constexpr uint32_t bstr(uint8_t id, uint32_t pos) {
  return (uint32_t(4)<<29) | (uint32_t(id)<<21) | pos;
}
constexpr int nestID(uint32_t nesting) {
  return nesting ? int(uint8_t(nesting>>21)) : -1;
}
constexpr unsigned nestType(uint32_t nesting) {
  switch (nesting>>29) {
    default: return 0;
    case 1: return Sequence;
    case 2: return Set;
    case 3: return OctetString;
    case 4: return BitString;
  }
}
constexpr uint32_t nestPos(uint32_t nesting) {
  return nesting & ~(uint32_t(0x3ff)<<21);
}

} // Encoding

using namespace Encoding;

// overall field formatting
// - position defaults to field definition order if unspecified
template <
  uint32_t Pos_,		// position (within sequence/set)
  uint32_t Tag_ = tagU(),	// tag
  uint32_t Nesting_ = 0,	// nesting
  uint32_t NestTag_ = tagU()>	// tag within nested sequence/set
struct Fmt {
  static constexpr uint32_t Pos = Pos_;
  static constexpr uint32_t Tag = Tag_;
  static constexpr uint32_t Nesting = Nesting_;
  static constexpr uint32_t NestTag = NestTag_;

  static constexpr uint32_t InnerTag = Nesting ? NestTag : Tag;

  // compile-time constant-evaluated tuple for field sorting
  using T = ZuTuple<uint32_t, uint32_t>;
  static constexpr T V{Pos, Nesting};
  constexpr operator const T &() const noexcept { return V; }
  constexpr const T &operator ()() const noexcept { return V; }
};

} // ZfASN1

namespace ZuFieldProp::ASN1 {

template <typename T_> struct Fmt_ { using T = T_; };
template <uint8_t Type_> struct Type { };
template <uint8_t Type_> struct ElemType { };
template <bool> struct Optional { };

// shorthand field formatting
// - ASN1::Fmt<0, tag(0)>
//   [0] value
// - ASN1::Fmt<0, tagI(0), set_(0), tag(0)>
//   [0] IMPLICIT SET { [0] value ... } ...
template <
  uint32_t Pos,
  uint32_t Tag = ZfASN1::tagU(),
  uint32_t Nesting = 0,
  uint32_t NestTag = ZfASN1::tagU()>
using Fmt = Fmt_<ZfASN1::Fmt<Pos, Tag, Nesting, NestTag>>;

// shorthand for Optional<true>
using Opt = Optional<true>;

// GetFmt - {Pos, Tag, Nesting, NestTag}
template <typename Props, uint32_t Deflt, bool = HasType<Props, Fmt_>{}>
struct GetFmt_ { using T = ZfASN1::Fmt<Deflt>; };
template <typename Props, uint32_t Deflt>
struct GetFmt_<Props, Deflt, true> {
  using T = ZuFieldProp::GetType<Props, Fmt_>;
};
template <typename Props, uint32_t Deflt = 0>
using GetFmt = typename GetFmt_<Props, Deflt>::T;

// GetType - ZuConstant<uint8_t>
// - used to encode scalars as strings instead of Integer or Real
// - also used to encode vectors as OIDs
// - can also be used to encode raw bytes as big integers
//   (e.g. RSA key elements)
template <
  typename Props,
  uint8_t Deflt,
  bool = HasValue<Props, Type>{}>
struct GetType_ {
  using T = ZuConstant<uint8_t, Deflt>; // default
};
template <typename Props, uint8_t Deflt>
struct GetType_<Props, Deflt, true> {
  using T = GetValue<Props, Type>;
};
template <typename Props, uint8_t Deflt>
using GetType = typename GetType_<Props, Deflt>::T;

// GetElemProps
// - derive element properties for an array
template <
  typename Props,
  bool = HasValue<Props, ElemType>{}>
struct GetElemProps_ {
  using T = ZuTypeList<>;
};
template <typename Props>
struct GetElemProps_<Props, true> {
  using T = ZuTypeList<Type<GetValue<Props, ElemType>{}>>;
};
template <typename Props>
using GetElemProps = typename GetElemProps_<Props>::T;

// GetOptional
template <
  typename Props,
  bool = HasValue<Props, Optional>{}>
struct GetOptional_ {
  using T = ZuFalse; // default
};
template <typename Props>
struct GetOptional_<Props, true> {
  using T = GetValue<Props, Optional>;
};
template <typename Props>
using GetOptional = typename GetOptional_<Props>::T;

} // ZuFieldProp::ASN1

namespace ZfASN1 {

// default ASN types for field element type codes
template <unsigned TypeCode>
constexpr uint8_t DefltASNType() {
  if constexpr (
      TypeCode == ZfFieldTC::CString ||
      TypeCode == ZfFieldTC::String)
    return UTF8String;
  else if constexpr (TypeCode == ZfFieldTC::Bytes)
    return OctetString;
  else if constexpr (TypeCode == ZfFieldTC::Bool)
    return Boolean;
  else if constexpr (
      TypeCode == ZfFieldTC::Int8 ||
      TypeCode == ZfFieldTC::Int16 ||
      TypeCode == ZfFieldTC::Int32 ||
      TypeCode == ZfFieldTC::Int64 ||
      TypeCode == ZfFieldTC::Int128 ||
      TypeCode == ZfFieldTC::UInt8 ||
      TypeCode == ZfFieldTC::UInt16 ||
      TypeCode == ZfFieldTC::UInt32 ||
      TypeCode == ZfFieldTC::UInt64 ||
      TypeCode == ZfFieldTC::UInt128)
    return Integer;
  else if constexpr (
      TypeCode == ZfFieldTC::Float ||
      TypeCode == ZfFieldTC::Fixed ||
      TypeCode == ZfFieldTC::Decimal)
    return Real;
  else if constexpr (
      TypeCode == ZfFieldTC::Time ||
      TypeCode == ZfFieldTC::DateTime)
    return GeneralizedTime;
  else if constexpr (
      TypeCode == ZfFieldTC::UDT ||
      ZfFieldTC::IsVec<TypeCode>{})
    return Sequence;
  // ZuUnreachable();
}

// --- field save/load

// length in bytes of an unsigned integer value
template <typename U>
constexpr uint8_t len_uint(U n) {
  if (!n) return 1;
  return (((sizeof(n)<<3) - ZuIntrin::clz(n)) + 7)>>3;
}
// length in bytes of a possibly-signed integer value
// - all ASN.1 integers are encoded as signed
// - if positive and MSB of first big-endian byte is set, then need
//   a leading 0-byte to disambiguate from a negative value
template <typename U>
constexpr uint8_t len_int(U n) {
  if (!n) return 1;
  if constexpr (ZuTraits<U>::IsSigned)
    if (n < 0) {
      n = ~n;
      return (((sizeof(n)<<3) - ZuIntrin::clz(n)) + 8)>>3;
    }
  auto i = (((sizeof(n)<<3) - ZuIntrin::clz(n)) + 7)>>3;
  if (n & (U(1)<<((i<<3) - 1))) ++i;
  return i;
}

// ASN.1 DER requires a 2-pass save; the first pass partially digests the
// values to be encoded and establishes the ASN.1 DER length of each item;
// the second pass retrieves the information from the first pass and uses
// it to complete the encoding

#pragma pack(push, 1)
// partially-digested individual value
struct SaveValue {
  ZuInline constexpr SaveValue() noexcept { }
  ZuInline constexpr ~SaveValue() noexcept { }

  uint64_t	length;		// ASN.1 DER total length of item (TL included)
  uint32_t	count;		// contained item count (1 unless Set/Sequence)
  union {
    // string value as *String
    struct {
      uint32_t	  n;		// output length of string in (wide) characters
    }		string;
    // integer value as Integer or *String
    struct {
      uint16_t	  n;		// output length of integer in bytes (*)
    }		integer;
    // floating point value as Real
    struct {
      int64_t	  m;		// mantissa
      int16_t	  e;		// exponent
      uint8_t	  en;		// output length of exponent in bytes
      uint8_t	  mn;		// output length of mantissa in bytes
    }		real;
    // floating point value as *String
    struct {
      int64_t	  i;		// integer part
      uint64_t	  fr;		// fractional part
      int16_t	  e;		// exponent
      uint8_t	  en;		// output length of exponent in bytes
      uint8_t	  in;		// output length of integer part in bytes
      uint8_t	  ndp;		// output length of fractional part in bytes
    }		fstring;
    // date/time value as *Time
    struct {
      uint32_t	  nr;		// nanoseconds
      uint16_t	  y;		// year
      uint8_t	  m_;		// month
      uint8_t	  d;		// day
      uint8_t	  h;		// hour
      uint8_t	  m;		// minute
      uint8_t	  s;		// second
      uint8_t	  ndp;		// output length of fractional part in bytes
    }		time;
    // int[] value as OID
    struct {
      uint32_t	  n;		// content length of OID
    }		oid;
    // composite/array value as sequence/set
    struct {
      uint32_t	  n;		// content length of sequence/set
    }		constructed;
  };

  // (*) integer output can be used for MPI values in crypto applications -
  // the output byte length can be much larger than 128 bits / 16 bytes
};
#pragma pack(pop)

// first pass builds a SaveArray
ZuDerive(SaveArray, (ZtArray<SaveValue, ZtArrayHeapID<"ZfASN1.SaveArray">>));
// second pass uses spans contained within the SaveArray
using SaveSpan = ZuSpan<const SaveValue>;

// calculate overall DER length given Tag, content length
template <uint32_t Tag>
inline uint64_t lenTL(uint64_t l)
{
  uint64_t n = ZuLikely(l < 0x80) ? 1 : ((79 - ZuIntrin::clz(l))>>3);
  // constexpr uint32_t Tag = Fmt::Nesting ? Fmt::NestTag : Fmt::Tag;
  if constexpr (Tag == tagU())
    return l + n + 1;
  else {
    if constexpr (!tagImplicit(Tag)) {
      l = l + n + 1;
      n = ZuLikely(l < 0x80) ? 1 : ((79 - ZuIntrin::clz(l))>>3);
    }
    constexpr uint32_t Number = tagNumber(Tag);
    if constexpr (Number >= 0x1f) n += (38 - ZuIntrin::clz(Number)) / 7;
    return l + n + 1;
  }
}

// write DER length
template <typename S>
inline void saveLen(S &s, uint64_t l)
{
  if (l < 0x80)
    s << char(l);
  else {
    uint8_t o = (71 - ZuIntrin::clz(l))>>3;
    s << char(o | 0x80);
    while (o--) s << char(l>>(o<<3));
  }
}

// write DER prefix given Tag, ASNType, content length
template <uint32_t Tag, uint8_t ASNType, typename S>
inline void saveTL(S &s, uint64_t l)
{
  uint64_t n = ZuLikely(l < 0x80) ? 1 : ((79 - ZuIntrin::clz(l))>>3);
  // explicit tagging encloses the TLV in an outer tag and length
  if constexpr (Tag != tagU()) {
    // specified tag
    constexpr uint32_t Number = tagNumber(Tag);
    constexpr uint8_t Constructed_ =
      tagImplicit(Tag) ? (ASNType & Constructed) : Constructed;
    if constexpr (Number < 0x1f)
      s << char((tagClass(Tag)<<6) | Constructed_ | Number);
    else {
      s << char((tagClass(Tag)<<6) | Constructed_ | 0x1f);
      auto o = (38 - ZuIntrin::clz(Number)) / 7;
      while (--o) s << char(((Number>>(o * 7)) & 0x7f) | 0x80);
      s << char(Number & 0x7f);
    }
    if constexpr (!tagImplicit(Tag)) {
      // explicit tag outer length
      saveLen(s, l + n + 1);
    }
  }
  if constexpr (Tag == tagU() || !tagImplicit(Tag)) {
    // inner universal tag (i.e. type)
    if constexpr (tagInner(Tag)) {
      s << char(tagInner(Tag));
    } else {
      s << char(ASNType);
    }
  }
  // inner length
  saveLen(s, l);
}

// used during first pass to stash lengths and partially-digested values
template <typename Stash>
struct SaveContext {
  Stash		&stash;
  int		nestID = -1;
  unsigned	offset = 0;
  bool		begun = false;

  SaveContext(Stash &stash_) : stash{stash_} { }

  template <int NestID>
  void begin() {
    begun = true;
    nestID = NestID;
    offset = stash.length();
    stash.push();
  }

  template <uint32_t Tag, uint8_t NestType, bool Optional>
  void end() {
    uint64_t total = 0, count = 1;
    for (unsigned i = offset + 1, end = stash.length(); i < end; ) {
      total += stash[i].length;
      count += stash[i].count;
      i += stash[i].count;
    }
    if (Optional && !total) {
      stash[offset].length = 0;
    } else {
      if constexpr (NestType == BitString) ++total;
      stash[offset].length = lenTL<Tag>(total);
      stash[offset].count = count;
      stash[offset].constructed.n = total;
    }
    nestID = -1;
    offset = 0;
    begun = false;
  }
};

// read DER length
// - returns {offset, length} of content
// - returns {-1, 0} if length is greater than span length
inline ZuTuple<int8_t, uint64_t> loadLen(ZuCSpan span)
{
  unsigned n = span.length();

  if (ZuUnlikely(n < 1)) goto bad;

  {
    uint8_t b;
    b = span[0];
    if (b < 0x80)
      return {1, b};
    else {
      uint8_t m = (b & 0x7f) + 1;
      if (n < m) goto bad;
      uint64_t l = 0;
      uint8_t o = 1;
      while (o < m) l = (l<<8) | (b = span[o++]);
      return {m, l};
    }
  }
bad:
  return {-1, -1};
}

// read DER prefix given Tag, ASNType
// - returns {offset, length} of content
// - returns {-1, next} on mismatch - next is offset of next item
// - returns {=1, -1} on corrupt data
template <uint32_t Tag, uint8_t ASNType>
inline ZuTuple<int8_t, uint64_t> loadTL(ZuCSpan span)
{
  unsigned n = span.length();

  if (ZuUnlikely(n < 1)) goto bad;

  {
    uint8_t o = 0;

    // explicit tagging encloses the TLV in an outer tag and length
    if constexpr (Tag != tagU()) {
      // specified tag
      constexpr uint32_t Number = tagNumber(Tag);
      constexpr uint8_t Constructed_ =
	tagImplicit(Tag) ? (ASNType & Constructed) : Constructed;
      uint8_t b = span[o++];
      if ((b & Constructed) != Constructed_ || (b>>6) != tagClass(Tag))
	goto mismatch;
      if constexpr (Number < 0x1f) {
	if ((b & 0x1f) != tagNumber(Tag)) goto mismatch;
      } else {
	if ((b & 0x1f) != 0x1f) goto mismatch;
	uint64_t t = 0;
	while (o < n && ((b = span[o++]) & 0x80))
	  t = (t<<7) | (b & 0x7f);
	if (o >= n) goto mismatch;
	t = (t<<7) | b;
	if (t != tagNumber(Tag)) goto mismatch;
      }
      if constexpr (!tagImplicit(Tag)) {
	// explicit tag outer length
	auto [o_, l_] = loadLen({&span[o], n - o});
	if (o_ < 0 || o_ + l_ > n) goto bad;
	o += o_;
	span.trunc(n = o + l_);
      }
    }
    if constexpr (Tag == tagU() || !tagImplicit(Tag)) {
      // inner universal tag
      if (n < 2) goto mismatch;
      uint8_t b = span[o++];
      if constexpr (tagInner(Tag)) {
	if (b != tagInner(Tag)) goto mismatch;
      } else {
	if (b != ASNType) goto mismatch;
      }
    }
    // inner length
    auto [o_, l_] = loadLen({&span[o], n - o});
    if (o_ < 0 || o_ + l_ > n) goto bad;
    return {o + o_, l_};
  }

mismatch:
  if (n > 2) {
    auto [o_, l_] = loadLen({&span[1], n - 1});
    if (o_ >= 0 && o_ + l_ <= n) return {-1, o_ + l_ + 1};
  }

bad:
  return {-1, -1};
}

template <typename Props, typename Fields>
struct LoadContext {
  ZuSpan<char>	fields[Fields::N];

  template <typename TopLevel>
  LoadContext(TopLevel, ZuSpan<char> span) {
    using Fmt = ZuFieldProp::ASN1::GetFmt<Props>;
    using ASNType = ZuFieldProp::ASN1::GetType<Props, Sequence>;
    constexpr uint32_t Tag = Fmt::Tag;

    uint64_t o = 0, n = span.length();

    if constexpr (TopLevel{}) {
      auto [o_, l_] = loadTL<Tag, ASNType{}>(span);
      if (ZuUnlikely(o_ < 0)) return;
      o += o_;
      span.trunc(n = o_ + l_);
    }

    int nestID_ = -1; // current nested seq/set
    uint64_t nestEnd = 0; // end offset within span of current nested seq/set
    ZuUnroll::all<Fields>([
      this, &span, &n, &o, &nestID_, &nestEnd
    ]<typename Field>() {
      using FieldProps = typename Field::Props;
      using FieldFmt = ZuFieldProp::ASN1::GetFmt<FieldProps>;
      enum { TypeCode = Field::Type::Code };
      using ASNType =
	ZuFieldProp::ASN1::GetType<FieldProps, DefltASNType<TypeCode>()>;
      enum { FieldNestID = nestID(FieldFmt::Nesting) };
      if (FieldNestID != nestID_) {
	if (nestID_ >= 0) o = nestEnd;
	if ((nestID_ = FieldNestID) >= 0) {
	  constexpr uint8_t NestType = nestType(FieldFmt::Nesting);
	  auto [o_, l_] = loadTL<FieldFmt::Tag, NestType>({&span[o], n - o});
	  if (ZuUnlikely(o_ < 0)) return;
	  o += o_;
	  if constexpr (NestType == BitString) ++o;
	  nestEnd = o + l_; // remember the end of this nested seq/set
	}
      }
      auto [o_, l_] =
	loadTL<FieldFmt::InnerTag, ASNType{}>({&span[o], n - o});
      if (ZuLikely(o_ >= 0)) {
	o += o_;
	fields[ZuTypeIndex<Field, Fields>{}] = {&span[o], unsigned(l_)};
	o += l_;
      } else {
	if (nestID_ < 0) return;
	o = nestEnd; // skip to the end of current nested seq/set
	nestID_ = -1;
      }
    });
  }
};

struct AsObject;	// as sequence/set
template <unsigned ElemCode>
struct AsArray;		// as homogeneous sequence

} // ZfASN1

ZfASN1::AsObject ZfASN1_Fmt(...);	// default

namespace ZfASN1 {

template <typename O>
using As = decltype(ZfASN1_Fmt(ZuDeclVal<O *>()));

// save individual value
template <
  typename Facet,
  unsigned TypeCode, typename Props,
  typename Stash, typename T>
inline void saveValue1(Stash &stash, const T &);
template <
  typename Facet,
  unsigned TypeCode, typename Props,
  typename S, typename T>
inline void saveValue2(S &, const T &, SaveSpan stash);

// save individual field
template <typename Facet, typename Field, typename Stash, typename O>
void saveField1(Stash &stash, const O &);
template <
  typename Facet, typename Field,
  typename S, typename O>
void saveField2(S &, const O &, SaveSpan stash);

// load individual value
template <
  typename Facet,
  unsigned TypeCode, typename Props,
  typename T>
auto loadValue(ZuSpan<char> span);

// save/load handler for objects formatted as Sequence / Set
struct AsObject {
  template <
    typename O_,
    typename Facet,
    typename Props = ZuTypeList<>>
  struct Handler {
    using O = O_;

    template <typename Field>
    using CtorIndex = ZuFieldProp::GetCtor<typename Field::Props>;

    using AllFields = ZuFields<O, Facet>;

    // sort fields per ASN.1 order
    template <typename Field>
    using ASN1Order =
      ZuFieldProp::ASN1::GetFmt<typename Field::Props,
	int(ZuTypeIndex<Field, AllFields>{}())>;

    using LoadFields = ZuTypeGrep<ZfFieldFilter::Load, AllFields>;
    using SaveFields_ = ZuTypeGrep<ZfFieldFilter::Save, AllFields>;
    using SaveFields = ZuTypeSort<ASN1Order, SaveFields_>;
    using CtorFields_ = ZuTypeGrep<ZfFieldFilter::Ctor, AllFields>;
    using CtorFields = ZuTypeSort<CtorIndex, CtorFields_>;
    using InitFields = ZuTypeGrep<ZfFieldFilter::Init, AllFields>;

    template <typename Stash>
    static void save1(Stash &stash, const O &o) {
      using Fmt = ZuFieldProp::ASN1::GetFmt<Props>;
      constexpr bool Optional = ZuFieldProp::ASN1::GetOptional<Props>{};

      SaveContext outer{stash};
      outer.template begin<nestID(Fmt::Nesting)>();
      SaveContext inner{stash};
      ZuUnroll::all<SaveFields>([&stash, &inner, &o]<typename Field>() mutable {
	using FieldProps = typename Field::Props;
	using FieldFmt = ZuFieldProp::ASN1::GetFmt<FieldProps>;
	enum { FieldNestID = nestID(FieldFmt::Nesting) };
	if (!inner.begun) {
	  if constexpr (FieldNestID >= 0)
            inner.template begin<unsigned(FieldNestID)>();
	}
	saveField1<Facet, Field>(stash, o);
	if (inner.begun) {
	  constexpr unsigned Next = ZuTypeIndex<Field, SaveFields>{} + 1;
	  if constexpr (Next >= SaveFields::N)
            inner.template end<
	      FieldFmt::Tag, nestType(FieldFmt::Nesting), true>();
	  else {
	    using NextField = ZuType<Next, SaveFields>;
	    using NextFieldProps = typename NextField::Props;
	    using NextFieldFmt = ZuFieldProp::ASN1::GetFmt<NextFieldProps>;
	    enum { NextFieldNestID = nestID(NextFieldFmt::Nesting) };
	    if constexpr (FieldNestID != NextFieldNestID)
              inner.template end<
		FieldFmt::Tag, nestType(FieldFmt::Nesting), true>();
	  }
	}
      });
      outer.template end<Fmt::InnerTag, 0, Optional>();
    }
    template <typename S>
    static void save2(S &s, const O &o, SaveSpan stash) {
      using Fmt = ZuFieldProp::ASN1::GetFmt<Props>;
      using ASNType = ZuFieldProp::ASN1::GetType<Props, Sequence>;

      if (!stash[0].length) return;
      saveTL<Fmt::InnerTag, ASNType{}>(s, stash[0].constructed.n);
      int nestID_ = -1;
      unsigned i = 1;
      ZuUnroll::all<SaveFields>([
	&s, &o, &stash, &nestID_, &i
      ]<typename Field>() {
	using FieldProps = typename Field::Props;
	using FieldFmt = ZuFieldProp::ASN1::GetFmt<FieldProps>;
	enum { FieldNestID = nestID(FieldFmt::Nesting) };
	if (FieldNestID != nestID_ && (nestID_ = FieldNestID) >= 0) {
	  constexpr uint8_t NestType = nestType(FieldFmt::Nesting);
	  saveTL<FieldFmt::Tag, NestType>(s, stash[i++].constructed.n);
	  if constexpr (NestType == BitString) s << char(0);
	}
	auto count = stash[i].count;
	saveField2<Facet, Field>(s, o, {&stash[i], count});
	i += count;
      });
    }

    LoadContext<Props, SaveFields>	context;

    template <typename TopLevel>
    Handler(TopLevel, ZuSpan<char> span) : context{TopLevel{}, span} { };

    template <typename Field>
    auto loadField() const {
      enum { TypeCode = Field::Type::Code };
      using FieldProps = typename Field::Props;
      using T = typename Field::T;
      using R = decltype(
	loadValue<Facet, TypeCode, FieldProps, T>(ZuSpan<char>()));
      enum { I = ZuTypeIndex<Field, SaveFields>{} };
      if (context.fields[I])
	return loadValue<Facet, TypeCode, FieldProps, T>(context.fields[I]);
      if constexpr (ZfFieldTC::IsVec<TypeCode>{})
	return R(ZuSpan<char>());
      else
	return R{Field::deflt()};
    }

    template <typename ...Field>
    struct Ctor {
      template <typename ...Args>
      static O ctor(const Handler &handler, Args &&...args) {
	return O(ZuFwd<Args>(args)..., handler.loadField<Field>()...);
      }
      template <typename ...Args>
      static O *alloc(const Handler &handler, Args &&...args) {
	return new O(ZuFwd<Args>(args)..., handler.loadField<Field>()...);
      }
      template <typename ...Args>
      static void new_(void *o, const Handler &handler, Args &&...args) {
	new (o) O(ZuFwd<Args>(args)..., handler.loadField<Field>()...);
      }
    };
    template <typename ...Args>
    O ctor(Args &&...args) const {
      if constexpr (!InitFields::N) // exploit guaranteed copy elision
	return ZuTypeApply<Ctor, CtorFields>::ctor(*this, ZuFwd<Args>(args)...);
      else {
	O o = ZuTypeApply<Ctor, CtorFields>::ctor(*this, ZuFwd<Args>(args)...);
	ZuUnroll::all<InitFields>([this, &o]<typename Field>() {
	  Field::set(o, this->loadField<Field>());
	});
	return o;
      }
    }
    template <typename ...Args>
    O *alloc(Args &&...args) const {
      O *o = ZuTypeApply<Ctor, CtorFields>::alloc(
	*this, ZuFwd<Args>(args)...);
      ZuUnroll::all<InitFields>([this, o]<typename Field>() {
	Field::set(*o, this->loadField<Field>());
      });
      return o;
    }
    template <typename ...Args>
    void new_(void *o_, Args &&...args) const {
      ZuTypeApply<Ctor, CtorFields>::new_(o_, *this, ZuFwd<Args>(args)...);
      O &o = *static_cast<O *>(o_);
      ZuUnroll::all<InitFields>([this, &o]<typename Field>() {
	Field::set(o, this->loadField<Field>());
      });
    }

    void load(O &o) const {
      ZuUnroll::all<LoadFields>([this, &o]<typename Field>() {
	Field::set(o, this->loadField<Field>());
      });
    }
  };
};

// Note: LoadOID and LoadVec only fall back to heap allocations if their
// builtin size limits are exceeded; the C++ ABI ensures that callers
// stack-allocate memory to hold returned instances of these types
// and elide copying

// LoadOID wraps an array of uint64_t OID elements
using LoadOID_ = ZtArray<uint64_t, ZtArrayHeapID<"ZfASN1.LoadOID">>;
struct LoadOID : public LoadOID_ {
  using Base = LoadOID_;
  using Base::push;

  // built-in size; ASN.1 OIDs are generally shorter than 16 elements
  enum { BuiltinSize = 16 };

  uint64_t	builtin[BuiltinSize];

  LoadOID(ZuSpan<char> span) : Base(&builtin[0], 0, BuiltinSize, false) {
    uint64_t n = span.length();
    if (ZuUnlikely(n < 1)) return;
    push(uint8_t(span[0]) / 40);
    push(uint8_t(span[0]) % 40);
    uint64_t o = 1;
    while (o < n) {
      uint64_t i = 0;
      uint8_t b;
      do {
	b = span[o++];
	i = (i<<7) | (b & 0x7f);
      } while (o < n && (b & 0x80));
      push(i);
    }
  }
};

// LoadVec wraps an array of sub-spans, parsing each sub-span on demand
ZuDerive(LoadVec_, (ZtArray<ZuSpan<char>, ZtArrayHeapID<"ZfASN1.LoadVec">>));
template <
  typename Facet,
  typename Props,	// sequence props
  unsigned TypeCode,	// type code of element
  typename ElemProps,	// element props
  typename T>		// element type
struct LoadVec :
  public ZuMArray<LoadVec<Facet, Props, TypeCode, ElemProps, T>, LoadVec_, T>
{
  using Base = ZuMArray<LoadVec, LoadVec_, T>;
  using Base::underlying;

  // built-in size; ASN.1 sequences are generally small
  enum { BuiltinSize = 8 };

  ZuSpan<char>	builtin[BuiltinSize];
  LoadVec_	underlying_ = LoadVec_(&builtin[0], 0, BuiltinSize, false);

  LoadVec(ZuSpan<char> span) : Base(underlying_) {
    unsigned o = 0, n = span.length();

    using ElemFmt = ZuFieldProp::ASN1::GetFmt<ElemProps>;
    using ElemASNType =
      ZuFieldProp::ASN1::GetType<ElemProps, DefltASNType<TypeCode>()>;
    enum { ElemNestID = nestID(ElemFmt::Nesting) };
    if constexpr (ElemNestID >= 0) {
      if (n <= 2) return;
      constexpr uint8_t NestType = nestType(ElemFmt::Nesting);
      auto [o_, l_] = loadTL<ElemFmt::Tag, NestType>({&span[o], n - o});
      if (ZuUnlikely(o_ < 0)) return;
      o += o_;
      if constexpr (NestType == BitString) ++o;
      span.trunc(o_ + l_);
    }

    while (o < n) {
      auto [o_, l_] =
	loadTL<ElemFmt::InnerTag, ElemASNType{}>({&span[o], n - o});
      if (ZuUnlikely(o_ < 0)) break;
      o += o_;
      underlying.push(ZuSpan<char>(&span[o], l_));
      o += l_;
    }
  }

  T get(unsigned i) const & {
    return loadValue<Facet, TypeCode, Props, T>(underlying[i]);
  }
  template <typename V> void set(unsigned, const V &) { } // unused
};

// save/load handler for array-formatted types
template <unsigned ElemCode>
struct AsArray {
  template <
    typename O_,
    typename Facet,
    typename Props = ZuTypeList<>>
  struct Handler {
    using O = O_;
    using Elem = ZuDecay<decltype(ZuDeclVal<const O &>()[0])>;
    using ElemProps = ZuFieldProp::ASN1::GetElemProps<Props>;

    template <typename Stash>
    static void save1(Stash &stash, const O &o) {
      using Fmt = ZuFieldProp::ASN1::GetFmt<Props>;
      constexpr bool Optional = ZuFieldProp::ASN1::GetOptional<Props>{};

      SaveContext outer{stash};
      outer.template begin<nestID(Fmt::Nesting)>();
      unsigned n = ZuTraits<O>::length(o);
      if constexpr (ElemCode == ZfFieldTC::UDT) {
	using ElemHandler =
	  typename As<Elem>::template Handler<Elem, Facet, ElemProps>;
	for (unsigned i = 0; i < n; i++)
	  ElemHandler::save1(stash, o[i]);
      } else {
	for (unsigned i = 0; i < n; i++)
	  saveValue1<Facet, ElemCode, ElemProps>(stash, o[i]);
      }
      outer.template end<Fmt::InnerTag, 0, Optional>();
    }
    template <typename S>
    static void save2(S &s, const O &o, SaveSpan stash) {
      using Fmt = ZuFieldProp::ASN1::GetFmt<Props>;
      using ASNType = ZuFieldProp::ASN1::GetType<Props, Sequence>;

      if (!stash[0].length) return;
      saveTL<Fmt::InnerTag, ASNType>(s, stash[0].constructed.n);
      unsigned n = ZuTraits<O>::length(o);
      unsigned i = 1;
      if constexpr (ElemCode == ZfFieldTC::UDT) {
	using ElemHandler =
	  typename As<Elem>::template Handler<Elem, Facet, ElemProps>;
	for (unsigned j = 0; j < n; j++) {
	  auto count = stash[i].count;
	  ElemHandler::save2(s, o[j], {stash[i], count});
	  i += count;
	}
      } else {
	for (unsigned j = 0; j < n; j++) {
	  auto count = stash[i].count;
	  saveValue2<Facet, ElemCode, ElemProps>(s, o[j], {&stash[i], count});
	  i += count;
	}
      }
    }

    using LoadVec_ = LoadVec<Facet, Props, ElemCode, ElemProps, Elem>;

    LoadVec_	vec;

    template <typename TopLevel, ZuIfT<!TopLevel{}, int> = 0>
    Handler(TopLevel, ZuSpan<char> span) : vec(span) { };

    template <typename ...Args>
    O ctor(Args &&...args) const { return O(vec); }
    template <typename ...Args>
    O *alloc(Args &&...args) const { return new O(vec); }
    template <typename ...Args>
    void new_(void *o, Args &&...args) const { new (o) O(vec); }

    void load(O &o) const { o = vec; }
  };
};

template <
  typename Facet,
  unsigned TypeCode, typename Props,
  typename T>
inline void saveValue1_(SaveValue &sv, const T &v_)
{
  using Fmt = ZuFieldProp::ASN1::GetFmt<Props>;
  constexpr uint32_t Tag = Fmt::InnerTag;
  constexpr bool Optional = ZuFieldProp::ASN1::GetOptional<Props>{};
  using ASNType = ZuFieldProp::ASN1::GetType<Props, DefltASNType<TypeCode>()>;

  sv.count = 1;
  if constexpr (
      TypeCode == ZfFieldTC::CString ||
      TypeCode == ZfFieldTC::String) {
    if constexpr (ASNType{} == UniversalString) {	// UTF32
      sv.string.n = ZuUTF<uint32_t, uint8_t>::span(v_).outLen();
      sv.length = (Optional && !sv.string.n) ? 0 : lenTL<Tag>(sv.string.n<<2);
    } else if constexpr (ASNType{} == BmpString) {	// UTF16
      sv.string.n = ZuUTF<uint16_t, uint8_t>::span(v_).outLen();
      sv.length = (Optional && !sv.string.n) ? 0 : lenTL<Tag>(sv.string.n<<1);
    } else if constexpr (
	ASNType{} == OctetString ||
	ASNType{} == UTF8String ||
	ASNType{} == PrintableString ||
	ASNType{} == T61String ||
	ASNType{} == IA5String) {			// UTF8
      auto n = ZuCSpan(v_).length();
      sv.length = (Optional && !n) ? 0 : lenTL<Tag>(n);
    } else {
      sv.length = 0;
    }
  } else if constexpr (TypeCode == ZfFieldTC::Bytes) {
    auto n = ZuBSpan{v_}.length();
    if constexpr (ASNType{} == Integer) {
      ZuBSpan bytes{v_};
      int i;
      n = bytes.length();
      for (i = 0; i < n; i++) if (bytes[i]) break;
      if (i < n) {
	if (bytes[i] & 0x80) --i;
	n -= i;
      } else {
	n = 1;
      }
      sv.integer.n = n;
    } else if constexpr (ASNType{} == BitString) {
      if (n) ++n;
    }
    sv.length = (Optional && !n) ? 0 : lenTL<Tag>(n);
  } else if constexpr (TypeCode == ZfFieldTC::Bool) {
    sv.length = lenTL<Tag>(1);
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
    using U = ZfFieldTC::Type<TypeCode>;
    U v{v_};
    if (Optional && ZuNull(v)) { sv.length = 0; return; }
    if constexpr (ASNType{} == Integer) {
      sv.integer.n = len_int(v);
      sv.length = lenTL<Tag>(sv.integer.n);
    } else if constexpr (ASNType{} == BitString) {
      sv.length = lenTL<Tag>(sizeof(U) + 1);
    } else if constexpr (
	ASNType{} == OctetString ||
	ASNType{} == UTF8String ||
	ASNType{} == PrintableString ||
	ASNType{} == T61String ||
	ASNType{} == IA5String) {
      bool negative = v_ < 0;
      Zu_ntoa::Unsigned<U> u = negative ? -v : v;
      sv.integer.n = Zu_ntoa::Log10<sizeof(U)>::log(u);
      sv.length = lenTL<Tag>(sv.integer.n + negative);
    } else {
      sv.length = 0;
    }
  } else if constexpr (TypeCode == ZfFieldTC::Float) {
    using F = ZuFPType<sizeof(T)>;
    F v = v_;
    if constexpr (ASNType{} == Real) {
      using FP = ZuFP<F>;
      auto decode = FP::decode(v);
      if (decode == FP::decodeNaN()) {
	if constexpr (Optional) { sv.length = 0; return; }
	sv.real = { .m = 0, .e = INT16_MIN, .en = 0, .mn = 0 };
	sv.length = lenTL<Tag>(1);
	return;
      }
      if (decode == FP::decodePosInf()) {
	sv.real = {
	  .m = INT64_MAX, .e = INT16_MAX, .en = 0, .mn = 0
	};
	sv.length = lenTL<Tag>(1);
	return;
      }
      if (decode == FP::decodeNegInf()) {
	sv.real = {
	  .m = INT64_MIN, .e = INT16_MAX, .en = 0, .mn = 0
	};
	sv.length = lenTL<Tag>(1);
	return;
      }
      if (v == 0) {
	sv.real = { .m = 0, .e = 0, .en = 0, .mn = 0 };
	sv.length = lenTL<Tag>(0);
	return;
      }
      auto [e, m] = decode;
      sv.real = {
	.m = m, .e = int16_t(e), .en = len_int(e), .mn = len_uint(m)
      };
      sv.length = lenTL<Tag>(1 + sv.real.en + sv.real.mn);
    } else if constexpr (
	ASNType{} == OctetString ||
	ASNType{} == UTF8String ||
	ASNType{} == PrintableString ||
	ASNType{} == T61String ||
	ASNType{} == IA5String) {
      if (ZuUnlikely(ZuCmp<F>::nan(v))) { // nan
	if constexpr (Optional) { sv.length = 0; return; }
	sv.fstring = {
	  .i = 0, .fr = 0, .e = INT16_MIN, .en = 0, .in = 0, .ndp = 0
	};
	sv.length = lenTL<Tag>(3);
	return;
      }
      if (v == 0) {
	sv.fstring = {
	  .i = 0, .fr = 0, .e = 0, .en = 0, .in = 0, .ndp = 0
	};
	sv.length = lenTL<Tag>(1); // 0
	return;
      }
      bool negative = v < 0;
      if (negative) v = -v;
      if (ZuUnlikely(v == ZuCmp<F>::inf())) { // inf
	sv.fstring = {
	  .i = INT64_MIN, .fr = 0, .e = INT16_MAX,
	  .en = 0, .in = 0, .ndp = 0
	};
	if (!negative) sv.fstring.i = ~sv.fstring.i;
	sv.length = lenTL<Tag>(3 + negative);
	return;
      }
      int16_t e = 0;
      uint8_t en = 0;
      unsigned n = negative;
      if (ZuUnlikely(v < 1.0e-9 || v >= 1.0e18)) {
	e = log10(v);
	if (e < 0) --e;
	v = v * pow(10, -e);
	if (v >= 10.0) { v /= 10.0; ++e; }
	en = Zu_ntoa::Log10<sizeof(int16_t)>::log(e < 0 ? -e : e);
	n += 2 + en;
      }
      ZuDecimal d{v};
      int64_t i = d.value / ZuDecimal::scale();
      if (negative) i = -i;
      uint64_t f = d.value % ZuDecimal::scale(); // d is positive
      uint8_t in = Zu_ntoa::Log10<8>::log(i);
      n += in;
      auto [ndp, fr] = ZuDecimalFn::ndp<ZuTuple<uint8_t, uint64_t>>(f);
      if constexpr (ZuFieldProp::HasNDP<Props>{}) {
	enum { NDP = ZuFieldProp::GetNDP<Props>{} };
	if (ndp > NDP) ndp = NDP;
      }
      if (ndp) n += 1 + ndp;
      sv.fstring = {
	.i = i, .fr = fr, .e = e, .en = en, .in = in, .ndp = ndp
      };
      sv.length = lenTL<Tag>(n);
    } else {
      sv.length = 0;
    }
  } else if constexpr (
      TypeCode == ZfFieldTC::Fixed ||
      TypeCode == ZfFieldTC::Decimal) {
    ZuDecimal v = v_;
    if constexpr (ASNType{} == Real) {
      if (ZuUnlikely(!*v)) { // nan
	if constexpr (Optional) { sv.length = 0; return; }
	sv.real = { .m = 0, .e = INT16_MIN, .en = 0, .mn = 0 };
	sv.length = lenTL<Tag>(1);
	return;
      }
      if (!v) { // 0
	sv.real = { .m = 0, .e = 0, .en = 0, .mn = 0 };
	sv.length = lenTL<Tag>(0);
	return;
      }
      auto [e, m] = ZuFP<double>::decode(v.as_fp());
      sv.real = {
	.m = m, .e = int16_t(e), .en = len_int(e), .mn = len_uint(m)
      };
      sv.length = lenTL<Tag>(1 + sv.real.en + sv.real.mn);
    } else if constexpr (
	ASNType{} == OctetString ||
	ASNType{} == UTF8String ||
	ASNType{} == PrintableString ||
	ASNType{} == T61String ||
	ASNType{} == IA5String) {
      if (ZuUnlikely(!*v)) { // nan
	if constexpr (Optional) { sv.length = 0; return; }
	sv.fstring = {
	  .i = 0, .fr = 0, .e = INT16_MIN, .en = 0, .in = 0, .ndp = 0
	};
	sv.length = lenTL<Tag>(3);
	return;
      }
      int64_t i = v.value / ZuDecimal::scale();
      bool negative = i < 0;
      if (negative) i = -i;
      uint64_t f = v.value % ZuDecimal::scale(); // d is positive
      uint8_t in = Zu_ntoa::Log10<8>::log(i);
      unsigned n = negative + in;
      auto [ndp, fr] = ZuDecimalFn::ndp<ZuTuple<uint8_t, uint64_t>>(f);
      if constexpr (ZuFieldProp::HasNDP<Props>{}) {
	enum { NDP = ZuFieldProp::GetNDP<Props>{} };
	if (ndp > NDP) ndp = NDP;
      }
      if (ndp) n += 1 + ndp;
      sv.fstring = {
	.i = i, .fr = fr, .e = 0, .en = 0, .in = in, .ndp = ndp
      };
      sv.length = lenTL<Tag>(n);
    } else {
      sv.length = 0;
    }
  } else if constexpr (
      TypeCode == ZfFieldTC::Time ||
      TypeCode == ZfFieldTC::DateTime) {
    ZuDateTime dt{v_};
    int y, m_, d, h, m, s, n;
    dt.ymd(y, m_, d);
    dt.hmsn(h, m, s, n);
    if (y < 0) { sv.length = 0; return; } // skip
    if constexpr (ASNType{} == UTCTime) {
      if (y < 1950 || y >= 2050) { sv.length = 0; return; } // skip
      sv.time = {
	.nr = 0, .y = uint16_t(y), .m_ = uint8_t(m_), .d = uint8_t(d),
	.h = uint8_t(h), .m = uint8_t(m), .s = uint8_t(s), .ndp = 0
      };
      sv.length = lenTL<Tag>(13);
      return;
    } else if constexpr (ASNType{} == GeneralizedTime) {
      if (!n) {
	sv.time = {
	  .nr = 0, .y = uint16_t(y), .m_ = uint8_t(m_), .d = uint8_t(d),
	  .h = uint8_t(h), .m = uint8_t(m), .s = uint8_t(s), .ndp = 0
	};
	sv.length = lenTL<Tag>(15);
      } else {
	auto [ndp, nr] = ZuDecimalFn::ndp<ZuTuple<uint8_t, uint32_t>, 9>(n);
	if constexpr (ZuFieldProp::HasNDP<Props>{}) {
	  enum { NDP = ZuFieldProp::GetNDP<Props>{} };
	  if (ndp > NDP) ndp = NDP;
	}
	sv.time = {
	  .nr = nr, .y = uint16_t(y), .m_ = uint8_t(m_), .d = uint8_t(d),
	  .h = uint8_t(h), .m = uint8_t(m), .s = uint8_t(s), .ndp = ndp
	};
	sv.length = lenTL<Tag>(16 + ndp);
      }
    } else {
      sv.length = 0;
    }
  }
}

template <
  typename Facet,
  unsigned TypeCode, typename Props,
  typename Stash, typename T_>
inline void saveValue1(Stash &stash, const T_ &v)
{
  using T = ZuDecay<T_>;
  if constexpr (!ZfFieldTC::IsVec<TypeCode>{}) {
    if constexpr (TypeCode == ZfFieldTC::UDT) {
      using Handler = typename As<T>::template Handler<T, Facet, Props>;
      Handler::save1(stash, v);
    } else {
      saveValue1_<Facet, TypeCode, Props>(*stash.push(), v);
    }
  } else {
    using Fmt = ZuFieldProp::ASN1::GetFmt<Props>;
    using ASNType = ZuFieldProp::ASN1::GetType<Props, Sequence>;

    ZuAssert(ASNType{} == OID || ASNType{} == Sequence);

    unsigned n = ZuTraits<T>::length(v);
    if constexpr (ASNType{} == OID) {
      auto &sv = *stash.push();
      if (!n) { sv.length = 0; return; }
      using Elem = ZuDecay<decltype(ZuDeclVal<const T &>()[0])>;
      ZuAssert((ZuIsConstructible<Elem, uint64_t>{}));
      uint32_t l = 1;
      for (unsigned i = 2; i < n; i++) {
	uint64_t j = v[i];
	l += !j ? 1 : (70 - ZuIntrin::clz(j)) / 7;
      }
      sv.length = lenTL<Fmt::InnerTag>(l);
      sv.count = 1;
      sv.oid.n = l;
    } else if constexpr (ASNType{} == Sequence) {
      enum { ElemCode = ZfFieldTC::Elem<TypeCode>{} };
      using ElemProps = ZuFieldProp::ASN1::GetElemProps<Props>;
      constexpr bool Optional = ZuFieldProp::ASN1::GetOptional<Props>{};

      SaveContext outer{stash};
      outer.template begin<0>();
      for (unsigned i = 0; i < n; i++)
	saveValue1_<Facet, ElemCode, ElemProps>(*stash.push(), v[i]);
      outer.template end<Fmt::InnerTag, 0, Optional>();
    }
  }
}

template <typename Facet, typename Field, typename Stash, typename O>
inline void saveField1(Stash &stash, const O &o)
{
  enum { TypeCode = Field::Type::Code };
  using Props = typename Field::Props;
  saveValue1<Facet, TypeCode, Props>(stash, Field::get(o));
}

template <
  typename Facet,
  unsigned TypeCode, typename Props,
  typename S, typename T>
inline void saveValue2_(S &s, const T &v_, const SaveValue &sv)
{
  if (ZuUnlikely(!sv.length)) return;

  using Fmt = ZuFieldProp::ASN1::GetFmt<Props>;
  constexpr uint32_t Tag = Fmt::InnerTag;
  using ASNType = ZuFieldProp::ASN1::GetType<Props, DefltASNType<TypeCode>()>;

  if constexpr (
      TypeCode == ZfFieldTC::CString ||
      TypeCode == ZfFieldTC::String) {
    ZuCSpan v(v_);
    if constexpr (ASNType{} == UniversalString) {	// UTF32
      unsigned n = sv.string.n;
      auto buf = ZmScratch(uint32_t, n);
      buf.length(n);
      ZuUTF<uint32_t, uint8_t>::cvt(buf, v);
      saveTL<Tag, UniversalString>(s, n<<2);
      for (unsigned i = 0; i < n; i++) {
	uint32_t w = buf[i];
	s << char(w>>24) << char(w>>16)
	  << char(w>>8) << char(w);
      }
    } else if constexpr (ASNType{} == BmpString) {	// UTF16
      unsigned n = sv.string.n;
      auto buf = ZmScratch(uint16_t, n);
      buf.length(n);
      ZuUTF<uint16_t, uint8_t>::cvt(buf, v);
      saveTL<Tag, BmpString>(s, n<<1);
      for (unsigned i = 0; i < n; i++) {
	uint16_t w = buf[i];
	s << char(w>>8) << char(w);
      }
    } else if constexpr (
	ASNType{} == OctetString ||
	ASNType{} == UTF8String ||
	ASNType{} == PrintableString ||
	ASNType{} == T61String ||
	ASNType{} == IA5String) {			// UTF8
      saveTL<Tag, ASNType{}>(s, v.length());
      s << v;
    }
  } else if constexpr (TypeCode == ZfFieldTC::Bytes) {
    ZuBSpan v{v_};
    auto n = v.length();
    if constexpr (ASNType{} == Integer) {
      saveTL<Tag, ASNType{}>(s, sv.integer.n);
      if (n > sv.integer.n)
	v.offset(n - sv.integer.n);
      else if (n < sv.integer.n)
	s << char(0);
    } else if constexpr (ASNType{} == BitString) {
      saveTL<Tag, ASNType{}>(s, n + 1);
      s << char(0);
    } else {
      saveTL<Tag, ASNType{}>(s, n);
    }
    s << ZuCSpan(v);
  } else if constexpr (TypeCode == ZfFieldTC::Bool) {
    bool v = v_;
    saveTL<Tag, Boolean>(s, 1);
    s << char(v ? 0xff : 0);
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
    using U = ZfFieldTC::Type<TypeCode>;
    U v{v_};
    if constexpr (ASNType{} == Integer) {
      auto n = sv.integer.n;
      saveTL<Tag, Integer>(s, n);
      while (n--) s << char(v>>(n<<3));
    } else if constexpr (ASNType{} == BitString) {
      auto n = sizeof(v);
      saveTL<Tag, BitString>(s, n + 1);
      s << char(0);
      while (n--) s << char(v>>(n<<3));
    } else if constexpr (
	ASNType{} == OctetString ||
	ASNType{} == UTF8String ||
	ASNType{} == PrintableString ||
	ASNType{} == T61String ||
	ASNType{} == IA5String) {
      bool negative = v < 0;
      Zu_ntoa::Unsigned<U> u = negative ? -v : v;
      auto n = sv.integer.n;
      saveTL<Tag, ASNType{}>(s, n + negative);
      if (negative) s << '-';
      char buf[Zu_ntoa::Log10_MaxLog<sizeof(U)>::N];
      Zu_ntoa::Base10_print(u, n, buf);
      s << ZuCSpan(&buf[0], n);
    }
  } else if constexpr (TypeCode == ZfFieldTC::Float) {
    if constexpr (ASNType{} == Real) {
      auto e = sv.real.e;
      auto m = sv.real.m;
      if (ZuUnlikely(e == INT16_MIN && !m)) { // nan
	saveTL<Tag, Real>(s, 1);
	s << char(0x42);
	return;
      }
      if (ZuUnlikely(e == INT16_MAX && m == INT64_MAX)) {// +inf
	saveTL<Tag, Real>(s, 1);
	s << char(0x40);
	return;
      }
      if (ZuUnlikely(e == INT16_MAX && m == INT64_MIN)) { // -inf
	saveTL<Tag, Real>(s, 1);
	s << char(0x41);
	return;
      }
      if (!e && !m) { // 0
	saveTL<Tag, Real>(s, 0);
	return;
      }
      bool negative = m < 0;
      if (negative) m = -m;
      auto en = sv.real.en;
      auto mn = sv.real.mn;
      saveTL<Tag, Real>(s, 1 + en + mn);
      s << char(0x80 | (negative ? 0x40 : 0) | (en - 1));
      while (en--) s << char(e>>(en<<3));
      while (mn--) s << char(m>>(mn<<3));
    } else if constexpr (
	ASNType{} == OctetString ||
	ASNType{} == UTF8String ||
	ASNType{} == PrintableString ||
	ASNType{} == T61String ||
	ASNType{} == IA5String) {
      auto i = sv.fstring.i;
      auto e = sv.fstring.e;
      if (ZuUnlikely(e == INT16_MIN && !i)) { // nan
	saveTL<Tag, ASNType{}>(s, 3);
	s << "nan";
	return;
      }
      if (!e && !i) { // 0
	saveTL<Tag, ASNType{}>(s, 1);
	s << "0";
	return;
      }
      bool negative = i < 0;
      if (negative) i = -i;
      if (ZuUnlikely(e == INT16_MAX && i == INT64_MAX)) { // inf
	saveTL<Tag, ASNType{}>(s, 3 + negative);
	if (negative) s << '-';
	s << "inf";
	return;
      }
      auto in = sv.fstring.in;
      auto en = sv.fstring.en;
      auto ndp = sv.fstring.ndp;
      {
	unsigned n = negative + in;
	if (ndp) n += ndp + 1;
	if (en) n += en + 2;
	saveTL<Tag, ASNType{}>(s, n);
      }
      if (negative) s << '-';
      char buf[Zu_ntoa::Log10_MaxLog<8>::N];
      Zu_ntoa::Base10_print(i, in, buf);
      s << ZuCSpan(&buf[0], in);
      if (ndp) {
	s << '.';
	Zu_ntoa::Base10_print(sv.fstring.fr, ndp, buf);
	s << ZuCSpan(&buf[0], ndp);
      }
      if (en) {
	s << 'e' << (e < 0 ? '-' : '+');
	Zu_ntoa::Base10_print(e < 0 ? -e : e, en, buf);
	s << ZuCSpan(&buf[0], en);
      }
    }
  } else if constexpr (
      TypeCode == ZfFieldTC::Fixed ||
      TypeCode == ZfFieldTC::Decimal) {
    ZuDecimal v = v_;
    if constexpr (ASNType{} == Real) {
      auto e = sv.real.e;
      auto m = sv.real.m;
      if (ZuUnlikely(e == INT16_MIN && !m)) { // nan
	saveTL<Tag, Real>(s, 1);
	s << char(0x42);
	return;
      }
      if (!e && !m) { // 0
	saveTL<Tag, Real>(s, 0);
	return;
      }
      bool negative = m < 0;
      if (negative) m = -m;
      auto en = sv.real.en;
      auto mn = sv.real.mn;
      saveTL<Tag, Real>(s, 1 + en + mn);
      s << char(0x80 | (negative ? 0x40 : 0) | (en - 1));
      while (en--) s << char(e>>(en<<3));
      while (mn--) s << char(m>>(mn<<3));
    } else if constexpr (
	ASNType{} == OctetString ||
	ASNType{} == UTF8String ||
	ASNType{} == PrintableString ||
	ASNType{} == T61String ||
	ASNType{} == IA5String) {
      auto i = sv.fstring.i;
      auto e = sv.fstring.e;
      if (ZuUnlikely(e == INT16_MIN && !i)) { // nan
	saveTL<Tag, ASNType{}>(s, 3);
	s << "nan";
	return;
      }
      if (!e && !i) { // 0
	saveTL<Tag, ASNType{}>(s, 1);
	s << "0";
	return;
      }
      bool negative = i < 0;
      if (negative) i = -i;
      auto in = sv.fstring.in;
      auto ndp = sv.fstring.ndp;
      {
	unsigned n = negative + in;
	if (ndp) n += ndp + 1;
	saveTL<Tag, ASNType{}>(s, n);
      }
      if (negative) s << '-';
      char buf[Zu_ntoa::Log10_MaxLog<8>::N];
      Zu_ntoa::Base10_print(i, in, buf);
      s << ZuCSpan(&buf[0], in);
      if (ndp) {
	s << '.';
	Zu_ntoa::Base10_print(sv.fstring.fr, ndp, buf);
	s << ZuCSpan(&buf[0], ndp);
      }
    }
  } else if constexpr (
      TypeCode == ZfFieldTC::Time ||
      TypeCode == ZfFieldTC::DateTime) {
    if (sv.time.y < 0) return;
    if constexpr (ASNType{} == UTCTime) {
      if (sv.time.y < 1950 || sv.time.y >= 2050) return;
      int y = sv.time.y;
      if (ZuUnlikely((y -= 2000) < 0)) y += 100;
      char buf[13];
      Zu_ntoa::Base10_print(y, 2, &buf[0]);
      Zu_ntoa::Base10_print(sv.time.m_, 2, &buf[2]);
      Zu_ntoa::Base10_print(sv.time.d, 2, &buf[4]);
      Zu_ntoa::Base10_print(sv.time.h, 2, &buf[6]);
      Zu_ntoa::Base10_print(sv.time.m, 2, &buf[8]);
      Zu_ntoa::Base10_print(sv.time.s, 2, &buf[10]);
      buf[12] = 'Z';
      saveTL<Tag, UTCTime>(s, 13);
      s << ZuCSpan(&buf[0], 13);
    } else if constexpr (ASNType{} == GeneralizedTime) {
      char buf[25];
      Zu_ntoa::Base10_print(sv.time.y, 4, &buf[0]);
      Zu_ntoa::Base10_print(sv.time.m_, 2, &buf[4]);
      Zu_ntoa::Base10_print(sv.time.d, 2, &buf[6]);
      Zu_ntoa::Base10_print(sv.time.h, 2, &buf[8]);
      Zu_ntoa::Base10_print(sv.time.m, 2, &buf[10]);
      Zu_ntoa::Base10_print(sv.time.s, 2, &buf[12]);
      auto ndp = sv.time.ndp;
      if (!ndp) {
	buf[14] = 'Z';
	saveTL<Tag, GeneralizedTime>(s, 15);
	s << ZuCSpan(&buf[0], 15);
	return;
      }
      buf[14] = '.';
      Zu_ntoa::Base10_print(sv.time.nr, ndp, &buf[15]);
      buf[15 + ndp] = 'Z';
      saveTL<Tag, GeneralizedTime>(s, 16 + ndp);
      s << ZuCSpan(&buf[0], 16U + ndp);
    }
  }
}

template <
  typename Facet,
  unsigned TypeCode, typename Props,
  typename S, typename T_>
inline void saveValue2(S &s, const T_ &v, SaveSpan stash)
{
  using T = ZuDecay<T_>;
  if constexpr (!ZfFieldTC::IsVec<TypeCode>{}) {
    if constexpr (TypeCode == ZfFieldTC::UDT) {
      using Handler = typename As<T>::template Handler<T, Facet, Props>;
      Handler::save2(s, v, stash);
    } else {
      saveValue2_<Facet, TypeCode, Props>(s, v, stash[0]);
    }
  } else {
    using Fmt = ZuFieldProp::ASN1::GetFmt<Props>;
    using ASNType = ZuFieldProp::ASN1::GetType<Props, Sequence>;

    ZuAssert(ASNType{} == OID || ASNType{} == Sequence);

    using Elem = ZuDecay<decltype(ZuDeclVal<const T &>()[0])>;
    unsigned n = ZuTraits<T>::length(v);
    if constexpr (ASNType{} == OID) {
      ZuAssert((ZuIsConstructible<Elem, uint64_t>{}));
      const auto &sv = stash[0];
      if (!sv.length) return;
      saveTL<Fmt::InnerTag, OID>(s, sv.oid.n);
      s << char(uint8_t(v[0]) * 40 + uint8_t(v[1]));
      for (unsigned i = 2; i < n; i++) {
	uint64_t j = v[i];
	auto o = (70 - ZuIntrin::clz(j)) / 7;
	while (--o) s << char(((j>>(o * 7)) & 0x7f) | 0x80);
	s << char(j & 0x7f);
      }
    } else if constexpr (ASNType{} == Sequence) {
      enum { ElemCode = ZfFieldTC::Elem<TypeCode>{} };
      using ElemProps = ZuFieldProp::ASN1::GetElemProps<Props>;

      const auto &sv = stash[0];
      if (!sv.length) return;
      saveTL<Fmt::InnerTag, Sequence>(s, sv.constructed.n);
      stash.offset(1);
      for (unsigned i = 0; i < n; i++)
	saveValue2_<Facet, ElemCode, ElemProps>(s, v[i], stash[i]);
    }
  }
}

template <
  typename Facet,
  typename Field,
  typename S, typename O>
inline void saveField2(S &s, const O &o, SaveSpan stash)
{
  enum { TypeCode = Field::Type::Code };
  using Props = typename Field::Props;
  saveValue2<Facet, TypeCode, Props>(s, Field::get(o), stash);
}

template <
  typename Facet, unsigned TypeCode, typename Props,
  typename T>
inline T loadValue_(ZuSpan<char> span)
{
  using ASNType = ZuFieldProp::ASN1::GetType<Props, DefltASNType<TypeCode>()>;
  unsigned n = span.length();

  if constexpr (
      TypeCode == ZfFieldTC::CString ||
      TypeCode == ZfFieldTC::String) {
    if constexpr (ASNType{} == UniversalString) {	// UTF32
      n &= ~3;
      if (ZuUnlikely(!n)) return T{};
      auto buf = ZmScratch(uint32_t, n>>2);
      buf.length(n>>2);
      auto bytes = ZuBSpan{span};
      for (unsigned i = 0; i < n; i += 4)
	buf[i>>2] =
	  (uint32_t(bytes[i])<<24) | (uint32_t(bytes[i + 1])<<16) |
	  (uint32_t(bytes[i + 2])<<8) | uint32_t(bytes[i + 3]);
      auto l = ZuUTF<uint8_t, uint32_t>::cvt(span, buf);
      return ZuCSpan(&span[0], l);
    } else if constexpr (ASNType{} == BmpString) {	// UTF16
      n &= ~1;
      if (ZuUnlikely(!n)) return T{};
      auto buf = ZmScratch(uint16_t, n>>1);
      buf.length(n>>1);
      auto bytes = ZuBSpan{span};
      for (unsigned i = 0; i < n; i += 2)
	buf[i>>1] = (uint16_t(bytes[i])<<8) | uint16_t(bytes[i + 1]);
      auto l = ZuUTF<uint8_t, uint16_t>::cvt(span, buf);
      return ZuCSpan(&span[0], l);
    } else if constexpr (
	ASNType{} == OctetString ||
	ASNType{} == UTF8String ||
	ASNType{} == PrintableString ||
	ASNType{} == T61String ||
	ASNType{} == IA5String) {			// UTF8
      return ZuCSpan(span);
    } else {
      return ZuCSpan();
    }
  } else if constexpr (TypeCode == ZfFieldTC::Bytes) {
    if constexpr (ASNType{} == BitString) span.offset(1);
    return ZuBSpan{span};
  } else if constexpr (TypeCode == ZfFieldTC::Bool) {
    if (ZuUnlikely(!n)) return ZuCmp<T>::null();
    return span[0];
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
    using U = ZfFieldTC::Type<TypeCode>;
    if constexpr (ASNType{} == Integer) {
      if (ZuUnlikely(!n)) return ZuCmp<T>::null();
      U v = 0;
      if (span[0] & 0x80) v = ~v;
      for (unsigned i = 0; i < n; i++) v = (v<<8) | span[i];
      return v;
    } else if constexpr (ASNType{} == BitString) {
      U v = 0;
      if (ZuLikely(n)) {
	span.offset(1); --n; // leading "unused bits" byte
	for (unsigned i = 0; i < n; i++) v = (v<<8) | span[i];
      }
      return v;
    } else if constexpr (
	ASNType{} == OctetString ||
	ASNType{} == UTF8String ||
	ASNType{} == PrintableString ||
	ASNType{} == T61String ||
	ASNType{} == IA5String) {
      return ZuBox<U>{span};
    } else {
      return ZuCmp<T>::null();
    }
  } else if constexpr (TypeCode == ZfFieldTC::Float) {
    using F = ZuFPType<sizeof(T)>;
    using FP = ZuFP<F>;
    if constexpr (ASNType{} == Real) {
      if (!n) return 0;
      uint8_t b = span[0];
      if (b == 0x42) return FP::nan();
      if (b == 0x40) return FP::inf();
      if (b == 0x41) return -FP::inf();
      if ((b & 0xfc) != 0x80) return FP::nan();
      auto en = (b & 0x3) + 1;
      if (n < en + 2) return FP::nan();
      auto mn = n - (en + 1);
      span.offset(1);
      int16_t e = 0;
      if (span[0] & 0x80) e = ~e;
      for (unsigned i = 0; i < en; i++) e = (e<<8) | span[i];
      span.offset(en);
      int64_t m = 0;
      if (span[0] & 0x80) m = ~m;
      for (unsigned i = 0; i < mn; i++) m = (m<<8) | span[i];
      return FP::encode(e, m);
    } else if constexpr (
	ASNType{} == OctetString ||
	ASNType{} == UTF8String ||
	ASNType{} == PrintableString ||
	ASNType{} == T61String ||
	ASNType{} == IA5String) {
      auto d = ZfJSON::eov_Float(span);
      if (d.p<0>() < 0) return ZuCmp<T>::null();
      return d.p<1>();
    } else {
      return ZuCmp<T>::null();
    }
  } else if constexpr (
      TypeCode == ZfFieldTC::Fixed ||
      TypeCode == ZfFieldTC::Decimal) {
    if constexpr (ASNType{} == Real) {
      if (!n) return 0;
      uint8_t b = span[0];
      if (b == 0x40 || b == 0x41 || b == 0x42) return T{};
      if ((b & 0xfc) != 0x80) return T{};
      auto en = (b & 0x3) + 1;
      if (n < en + 2) return T{};
      auto mn = n - (en + 1);
      span.offset(1);
      int16_t e = 0;
      if (span[0] & 0x80) e = ~e;
      for (unsigned i = 0; i < en; i++) e = (e<<8) | span[i];
      span.offset(en);
      int64_t m = 0;
      if (span[0] & 0x80) m = ~m;
      for (unsigned i = 0; i < mn; i++) m = (m<<8) | span[i];
      return ZuFP<double>::encode(e, m);
    } else if constexpr (
	ASNType{} == OctetString ||
	ASNType{} == UTF8String ||
	ASNType{} == PrintableString ||
	ASNType{} == T61String ||
	ASNType{} == IA5String) {
      auto d = ZfJSON::eov_Decimal(span);
      if (d.p<0>() < 0) return T{};
      if constexpr (TypeCode == ZfFieldTC::Decimal)
	return d.p<1>();
      else {
	if (!*d.p<1>()) return ZuFixed{};
	if constexpr (ZuFieldProp::HasNDP<Props>{})
	  return ZuFixed{d.p<1>(), ZuFieldProp::GetNDP<Props>{}};
	else
	  return ZuFixed{d.p<1>()};
      }
    } else {
      return T{};
    }
  } else if constexpr (
      TypeCode == ZfFieldTC::Time ||
      TypeCode == ZfFieldTC::DateTime) {
    if constexpr (ASNType{} == UTCTime) {
      return ZuDateTime{ZuDateTimeScan::ASN1_U{}, span};
    } else if (ASNType{} == GeneralizedTime) {
      return ZuDateTime{ZuDateTimeScan::ASN1_G{}, span};
    } else {
      return T{};
    }
  } else if constexpr (TypeCode == ZfFieldTC::UDT) {
    return typename As<T>::template Handler<T, Facet>{ZuFalse{}, span}.ctor();
  }
}

template <typename Facet, unsigned TypeCode, typename Props, typename T>
inline auto loadValue(ZuSpan<char> span)
{
  if constexpr (!ZfFieldTC::IsVec<TypeCode>{}) {
    return loadValue_<Facet, TypeCode, Props, T>(span);
  } else {
    using ASNType = ZuFieldProp::ASN1::GetType<Props, Sequence>;

    ZuAssert(ASNType{} == OID || ASNType{} == Sequence);

    if constexpr (ASNType{} == OID) {
      using Elem = ZuDecay<decltype(ZuDeclVal<const T &>()[0])>;
      ZuAssert((ZuIsConstructible<uint64_t, Elem>{}));
      return LoadOID(span);
    } else {
      enum { ElemCode = ZfFieldTC::Elem<TypeCode>{} };
      using ElemProps = ZuFieldProp::ASN1::GetElemProps<Props>;
      using Elem = ZfFieldTC::Type<ElemCode>;
      using LoadVec_ = LoadVec<Facet, Props, ElemCode, ElemProps, Elem>;
      return LoadVec_(span);
    }
  }
}

template <
  typename Facet = ZuFacet::ASN1,
  unsigned StashSize = 32,
  typename S, typename O>
inline S &save(S &s, const O &v)
{
  auto stash = ZtScratch(SaveArray, StashSize);
  using Handler = typename As<O>::template Handler<O, Facet>;
  Handler::save1(stash, v);
  Handler::save2(s, v, stash);
  return s;
}

template <typename O, typename Facet = ZuFacet::ASN1>
auto handler(ZuSpan<char> span) {
  return typename As<O>::template Handler<O, Facet>{ZuTrue{}, span};
}

} // ZfASN1

#endif /* ZfASN1_HH */
