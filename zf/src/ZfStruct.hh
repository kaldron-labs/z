//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// object introspection / reflection
// - extends ZuStruct
// - compile-time (ZfField*) and run-time dynamic polymorphic (ZfVField*)
// - print/scan (CSV, etc.)
// - ORM
// - data series
// - ... any other application that needs to introspect structured data

// metadata macro DSL for identifying and using data fields and keys
//
// ZfStruct(API, (Type[, Facets...]), Fields...)
//
// the parentheses around Type are optional if no facets are specified
//
// each field has compile-time properties, an extensible typelist of types
// that are injected into the ZuFieldProp namespace
//
// a Field is of the form:
// (((Accessor)[, (Props...)]), Type)
//
// Example: (((id, Rd), (Keys<0>, Ctor<0>, Deflt<"default"_z>)),	String)
// Meaning: Read-only string field named "id" with a default
//   value of "default" that is also the containing object's zeroth
//   constructor parameter
//
// ZfField Type  C/C++ Type
// ------------  ----------
// CString       char *
// String        <String>
// Bytes         <uint8_t[]>
// Bool          <Integral>
// Int<Size>     <Integral>
// UInt<Size>    <Integral>
// Float         <FloatingPoint>
// Fixed         ZuFixed
// Decimal       ZuDecimal
// Time          ZuTime
// DateTime      ZuDateTime
// UDT           <UDT>
//
// Range<Minimum, Maximum> is a field property specifying inclusive bounds
// for Int, UInt, Float, Fixed and Decimal fields
//
// *Vec          ZuSpan<T>
// CStringVec
// StringVec
// BytesVec
// Int<Size>Vec
// UInt<Size>Vec
// FloatVec
// FixedVec
// DecimalVec
// TimeVec
// DateTimeVec
//
// ZfVField provides run-time introspection via a monomorphic
// (type-erased) type - virtual polymorphism and RTTI are
// intentionally avoided:
// - if ZfVField were virtually polymorphic, passing it to dynamically
//   loaded libraries (e.g. data store adapters performing serdes) would
//   entail a far more complex type hierarchy with diamond-shaped
//   inheritance, use of dynamic_cast, etc.
// - ZfVField (and derived classes) benefit from being POD
// - very little syntactic benefit would be obtained

// ZuField<O> is extended to provide:
//   Type	- ZfFieldType<...>
//   deflt()	- canonical default value
//   minimum()	- minimum value (for scalars)
//   maximum()	- maximum value ('')
//
// ZfField(O, ID) is the derived type inheriting from ZuField(O, ID)
//
// ZfFieldType is keyed on <Code, T, Props>, provides:
//   Code	- type code (ZfFieldTC)
//   T		- underlying type
//   Map	- map (if either Enum or Flags)
//   Props	- properties type list
//   Print	- Print<Fmt>{const T &} - compile-time formatted printing
//   vtype()	- ZfVFieldType * instance
//
// ZfVFieldType provides:
//   code	- ZfFieldTC
//   props	- ZfVFieldProp properties bitfield
//   info	- enum / flags / UDT metadata
//
// ZfVField{ZfField{}} instantiates ZfVField from ZfField
//
// ZfVField provides:
//   type	- ZfVFieldType * instance
//   id		- ZfField::id()
//   props	- ZfVFieldProp properties bitfield
//   keys	- ZfField::keys()
//   get	- ZfVFieldGet
//   set	- ZfVFieldSet
//   constant	- ZfVFieldGet for constants (default, minimum, maximum)
//   cget	- cast ZfVFieldConstant to const void * for ZfVFieldGet
//
// ZfVFieldGet provides:
//   get<Code>(const void *o)
//   print<Code>(auto &s, const void *o, const ZfVField *, const ZtVFmt &)
//
// ZfVFieldSet provides:
//   set<Code>(void *o, auto &&v)
//   scan<Code>(void *o, ZuCSpan s, const ZfVField *, const ZtVFmt &)
//
// ZfVFields<O>() returns the ZfVFieldArray for O
// ZfVKeyFields<O>() returns the ZfVKeyFieldArray for O
// ZfVKeyFields<O>()[KeyID] == ZfVFields<ZuStructKeyT<O, KeyID>>()

#ifndef ZfStruct_HH
#define ZfStruct_HH

#ifndef ZfLib_HH
#include <zlib/ZfLib.hh>
#endif

#include <string.h>

#include <typeinfo>

#include <zlib/ZuDerive.hh>
#include <zlib/ZuSpan.hh>
#include <zlib/ZuUnroll.hh>
#include <zlib/ZuInt.hh>
#include <zlib/ZuDecimal.hh>
#include <zlib/ZuFixed.hh>
#include <zlib/ZuTime.hh>
#include <zlib/ZuDateTime.hh>
#include <zlib/ZuPrint.hh>
#include <zlib/ZuBox.hh>
#include <zlib/ZuBase64.hh>
#include <zlib/ZuStruct.hh>
#include <zlib/ZuVArray.hh>
#include <zlib/ZuID.hh>
#include <zlib/ZuTuple.hh>
#include <zlib/ZuVStream.hh>
#include <zlib/ZuMatcher.hh>

#include <zlib/ZmScratch.hh>
#include <zlib/ZmSingleton.hh>

#include <zlib/ZtQuote.hh>
#include <zlib/ZtEnum.hh>
#include <zlib/ZtString.hh>
#include <zlib/ZtRegex.hh>
#include <zlib/ZtScanBool.hh>
#include <zlib/ZtFmt.hh>

namespace ZfFieldTC {

ZtEnum(ZfAPI, ZfFieldTC, int8_t,
  CString,	// C UTF-8 string (raw pointer), heap-allocated
  String,	// C++ contiguous UTF-8 string
  Bytes,	// byte array
  Bool,		// an integral type, interpreted as bool
  Int8,		// 8bit integer
  UInt8,	// 8bit unsigned integer
  Int16,	// 16bit integer
  UInt16,	// 16bit unsigned integer
  Int32,	// 32bit integer
  UInt32,	// 32bit unsigned integer
  Int64,	// 64bit integer
  UInt64,	// 64bit unsigned integer
  Int128,	// 128bit integer
  UInt128,	// 128bit unsigned integer
  Float,	// floating point type
  Fixed,	// ZuFixed
  Decimal,	// ZuDecimal
  Time,		// ZuTime
  DateTime,	// ZuDateTime - Julian date, seconds, nanoseconds
  UDT,		// generic udt type

  // XVec - vectors of X

  CStringVec,
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
  DateTimeVec);

// canonical type for each non-vector code
template <unsigned> struct Type_;
template <> struct Type_<CString> { using T = const char *; };
template <> struct Type_<String> { using T = ZuCSpan; };
template <> struct Type_<Bytes> { using T = ZuBSpan; };
template <> struct Type_<Bool> { using T = bool; };
template <> struct Type_<Int8> { using T = int8_t; };
template <> struct Type_<Int16> { using T = int16_t; };
template <> struct Type_<Int32> { using T = int32_t; };
template <> struct Type_<Int64> { using T = int64_t; };
template <> struct Type_<Int128> { using T = int128_t; };
template <> struct Type_<UInt8> { using T = uint8_t; };
template <> struct Type_<UInt16> { using T = uint16_t; };
template <> struct Type_<UInt32> { using T = uint32_t; };
template <> struct Type_<UInt64> { using T = uint64_t; };
template <> struct Type_<UInt128> { using T = uint128_t; };
template <> struct Type_<Float> { using T = double; };
template <> struct Type_<Fixed> { using T = ZuFixed; };
template <> struct Type_<Decimal> { using T = ZuDecimal; };
template <> struct Type_<Time> { using T = ZuTime; };
template <> struct Type_<DateTime> { using T = ZuDateTime; };
template <unsigned I> using Type = typename Type_<I>::T;

// map vector to element code
template <unsigned> struct Elem;
template <> struct Elem<CStringVec> : public ZuUnsigned<CString> { };
template <> struct Elem<StringVec> : public ZuUnsigned<String> { };
template <> struct Elem<BytesVec> : public ZuUnsigned<Bytes> { };
template <> struct Elem<Int8Vec> : public ZuUnsigned<Int8> { };
template <> struct Elem<UInt8Vec> : public ZuUnsigned<UInt8> { };
template <> struct Elem<Int16Vec> : public ZuUnsigned<Int16> { };
template <> struct Elem<UInt16Vec> : public ZuUnsigned<UInt16> { };
template <> struct Elem<Int32Vec> : public ZuUnsigned<Int32> { };
template <> struct Elem<UInt32Vec> : public ZuUnsigned<UInt32> { };
template <> struct Elem<Int64Vec> : public ZuUnsigned<Int64> { };
template <> struct Elem<UInt64Vec> : public ZuUnsigned<UInt64> { };
template <> struct Elem<Int128Vec> : public ZuUnsigned<Int128> { };
template <> struct Elem<UInt128Vec> : public ZuUnsigned<UInt128> { };
template <> struct Elem<FloatVec> : public ZuUnsigned<Float> { };
template <> struct Elem<FixedVec> : public ZuUnsigned<Fixed> { };
template <> struct Elem<DecimalVec> : public ZuUnsigned<Decimal> { };
template <> struct Elem<TimeVec> : public ZuUnsigned<Time> { };
template <> struct Elem<DateTimeVec> : public ZuUnsigned<DateTime> { };

// test if type code is vector
template <unsigned, typename = void> struct IsVec : public ZuFalse { };
template <unsigned TC>
struct IsVec<TC, decltype(Elem<TC>{}, void())> : public ZuTrue { };

}

// extended compile-time field property list (see ZuFieldProp)
namespace ZuFieldProp {
  // group key IDs
  template <unsigned ...KeyIDs> struct Group { };
  // descending key IDs
  template <unsigned ...KeyIDs> struct Descend { };

  struct Synthetic { };		// synthetic (implies read-only)
  struct Mutable { };		// include in updates (implies not read-only)
  struct Reset { };		// reset on update if missing (implies not required)
  struct Hidden { };		// do not print
  struct Hex { };		// print hex value
  struct Required { };		// required - do not default
  struct Series { };		// data series column
  struct Index { };		// - index (e.g. time, nonce, offset, seq#)
  struct Delta { };		// - first derivative
  struct Delta2 { };		// - second derivative

  template <typename Map> struct Enum { using T = Map; };	// enum
  template <typename Map> struct Flags { using T = Map; };	// flags

  template <auto Def> struct Deflt {
    static constexpr decltype(auto) deflt() { return (Def); }
  };

  template <auto Min, auto Max> struct Range {
    static constexpr decltype(auto) minimum() { return (Min); }
    static constexpr decltype(auto) maximum() { return (Max); }
  };

  template <int8_t> struct NDP { }; // NDP for printing float/fixed/decimal

  // get group key IDs
  template <typename Props>
  using GetGroup = GetSeq<Props, Group>;
  // get descending key IDs
  template <typename Props>
  using GetDescend = GetSeq<Props, Descend>;

  // IsGroup<Props, KeyID> - is this field in the group part of key KeyID?
  template <typename Props, unsigned KeyID>
  struct IsGroup_ :
    public ZuTypeIn<ZuUnsigned<KeyID>, ZuSeqTL<GetGroup<Props>>> { };
  template <typename Props, unsigned KeyID>
  struct IsGroup :
    public ZuBool<
      bool(Key<Props, KeyID>{}) &&
      bool(IsGroup_<Props, KeyID>{})> { };

  // shorthand for accessing Enum / Flags maps
  template <typename Props> using HasEnum = HasType<Props, Enum>;
  template <typename Props> using GetEnum = GetType<Props, Enum>;

  template <typename Props> using HasFlags = HasType<Props, Flags>;
  template <typename Props> using GetFlags = GetType<Props, Flags>;

  template <typename> struct IsDeflt : public ZuFalse { };
  template <auto Def>
  struct IsDeflt<Deflt<Def>> : public ZuTrue { };
  template <typename Props>
  using Deflts = ZuTypeGrep<IsDeflt, Props>;
  template <typename Props>
  using HasDeflt = ZuBool<Deflts<Props>::N>;
  template <typename Props>
  using GetDeflt = ZuType<Deflts<Props>::N - 1, Deflts<Props>>;

  template <typename> struct IsRange : public ZuFalse { };
  template <auto Min, auto Max>
  struct IsRange<Range<Min, Max>> : public ZuTrue { };
  template <typename Props>
  using Ranges = ZuTypeGrep<IsRange, Props>;
  template <typename Props>
  using HasRange = ZuBool<Ranges<Props>::N>;
  template <typename Props>
  using GetRange = ZuType<Ranges<Props>::N - 1, Ranges<Props>>;

  template <typename Props>
  using HasNDP = HasValue<Props, NDP>;
  template <typename Props>
  using GetNDP = GetValue<Props, NDP>;
}
namespace ZfVFieldProp {
  using namespace ZuFieldProp;

  // default Ctor property to -1 for ZfVField
  template <typename Props, bool = HasValue<Props, Ctor>{}>
  struct GetCtor_ { using T = GetValue<Props, Ctor>; };
  template <typename Props>
  struct GetCtor_<Props, false> { using T = ZuInt<-1>; };
  template <typename Props> using GetCtor = typename GetCtor_<Props>::T;

  // default NDP to sentinel null for ZfVField
  template <typename Props, bool = HasValue<Props, NDP>{}>
  struct GetNDP_ { using T = GetValue<Props, NDP>; };
  template <typename Props>
  struct GetNDP_<Props, false> {
    using T = ZuConstant<int8_t, ZuCmp<int8_t>::null()>;
  };
  template <typename Props> using GetNDP = typename GetNDP_<Props>::T;
}

// type properties are a subset of field properties
template <typename Prop> struct ZfFieldType_Props : public ZuFalse { };
template <> struct ZfFieldType_Props<ZuFieldProp::Hidden> : public ZuTrue { };
template <> struct ZfFieldType_Props<ZuFieldProp::Hex> : public ZuTrue { };
template <typename Map>
struct ZfFieldType_Props<ZuFieldProp::Enum<Map>> : public ZuTrue { };
template <typename Map>
struct ZfFieldType_Props<ZuFieldProp::Flags<Map>> : public ZuTrue { };
template <auto I>
struct ZfFieldType_Props<ZuFieldProp::NDP<I>> : public ZuTrue { };

// ZfVFieldProp bitfield encapsulates introspected ZfField properties
namespace ZfVFieldProp {
  ZtFlags(ZfAPI, ZfVFieldProp, uint16_t,
    Ctor,
    Synthetic,
    Mutable,
    Hidden,
    Hex,
    Required,
    Series,
    Index,
    Delta,
    Delta2,
    Enum,
    Flags,
    Range,
    Deflt,
    NDP);

  using V = T;

  template <V I> using Constant = ZuConstant<V, I>;

  template <typename> struct Value_ { using T = Constant<0>; }; // default

  template <auto I>
  struct Value_<Constant<I>> { using T = Constant<I>; }; // passthru

  template <typename U> using Value = typename Value_<U>::T;

  namespace _ = ZuFieldProp;

  template <auto I>
  struct Value_<_::Ctor<I>>               { using T = Constant<Ctor()>; };
  template <> struct Value_<_::Synthetic> { using T = Constant<Synthetic()>; };
  template <> struct Value_<_::Mutable>   { using T = Constant<Mutable()>; };
  template <> struct Value_<_::Hidden>    { using T = Constant<Hidden()>; };
  template <> struct Value_<_::Hex>       { using T = Constant<Hex()>; };
  template <> struct Value_<_::Required>  { using T = Constant<Required()>; };
  template <> struct Value_<_::Series>    { using T = Constant<Series()>; };
  template <> struct Value_<_::Index>     { using T = Constant<Index()>; };
  template <> struct Value_<_::Delta>     { using T = Constant<Delta()>; };
  template <> struct Value_<_::Delta2>    { using T = Constant<Delta2()>; };
  template <typename Map>
  struct Value_<_::Enum<Map>>             { using T = Constant<Enum()>; };
  template <typename Map>
  struct Value_<_::Flags<Map>>            { using T = Constant<Flags()>; };
  template <auto Def>
  struct Value_<_::Deflt<Def>>            { using T = Constant<Deflt()>; };
  template <auto Min, auto Max>
  struct Value_<_::Range<Min, Max>>       { using T = Constant<Range()>; };
  template <auto I>
  struct Value_<_::NDP<I>>                { using T = Constant<NDP()>; };

  // Value<List>::N - return bitfield for property list
  template <typename ...> struct Or_;
  template <> struct Or_<> {
    using T = Constant<0>;
  };
  template <typename U> struct Or_<U> {
    using T = Value<U>;
  };
  template <typename L, typename R> struct Or_<L, R> {
    using T = Constant<V(Value<L>{}) | V(Value<R>{})>;
  };
  template <typename ...Props> using Or = typename Or_<Props...>::T;

  template <typename ...Props>
  struct Value_<ZuTypeList<Props...>> {
    using T = ZuTypeReduce<Or, ZuTypeList<Props...>>;
  };
}

// type is keyed on type-code, underlying type, type properties
template <typename Props>
struct ZfFieldType_ {
  static constexpr ZfVFieldProp::T mprops() {
    return ZfVFieldProp::Value<Props>{};
  }
};

template <int Code, typename T, typename Props>
struct ZfFieldType;

// deduced function to scan from a string
template <typename T, typename = void>
struct ZfFieldType_Scan {
  static auto fn() {
    typedef void (*Fn)(void *, ZuCSpan, const ZtVFmt &);
    return static_cast<Fn>(nullptr);
  }
};
template <typename T>
struct ZfFieldType_Scan<T, decltype((ZuDeclVal<T &>() = ZuCSpan()), void())> {
  static auto fn() {
    return [](void *ptr, ZuCSpan s, const ZtVFmt &) {
      *static_cast<T *>(ptr) = s;
    };
  }
};

// deduced comparison function
template <typename T, typename = void>
struct ZfFieldType_Cmp {
  static auto fn() {
    typedef int (*Fn)(const void *, const void *);
    return static_cast<Fn>(nullptr);
  }
};
template <typename T>
struct ZfFieldType_Cmp<T, decltype(&ZuCmp<T>::cmp, void())> {
  static auto fn() {
    return [](const void *p1, const void *p2) -> int {
      return ZuCmp<T>::cmp(
	  *static_cast<const T *>(p1), *static_cast<const T *>(p2));
    };
  }
};

// ZfVFieldEnum encapsulates introspected enum metadata
struct ZfVFieldEnum {
  const char	*(*id)();
  ZuCSpan	(*print)(int);
  int		(*scan)(ZuCSpan);
};
template <typename Map>
struct ZfVFieldEnum_ : public ZfVFieldEnum {
  ZfVFieldEnum_() : ZfVFieldEnum{
    .id = []() -> const char * { return Map::id(); },
    .print = [](int i) -> ZuCSpan { return Map::v2s(i); },
    .scan = [](ZuCSpan s) -> int { return Map::s2v(s); }
  } { }

  static ZfVFieldEnum *instance() {
    return ZmSingleton<ZfVFieldEnum_>::instance();
  }
};

// ZfVFieldFlags encapsulates introspected flags metadata
struct ZfVFieldFlags {
  const char	*(*id)();
  void		(*print)(uint128_t, ZuVStream &, const ZtVFmt &);
  uint128_t	(*scan)(ZuCSpan, const ZtVFmt &);
};
template <typename Map>
struct ZfVFieldFlags_ : public ZfVFieldFlags {
  ZfVFieldFlags_() : ZfVFieldFlags{
    .id = []() -> const char * { return Map::id(); },
    .print = [](uint128_t v, ZuVStream &s, const ZtVFmt &fmt) -> void {
      s << typename Map::Print(v, fmt.flagsDelim);
    },
    .scan = [](ZuCSpan s, const ZtVFmt &fmt) -> uint128_t {
      return typename Map::Scan{s, fmt.flagsDelim};
    }
  } { }

  static ZfVFieldFlags *instance() {
    return ZmSingleton<ZfVFieldFlags_>::instance();
  }
};

typedef void (*ZfVFieldPrint)(const void *, ZuVStream &, const ZtVFmt &);
typedef void (*ZfVFieldScan)(
  void (*)(void *, const void *), void *, ZuCSpan, const ZtVFmt &);

inline ZuCSpan ZfVFieldTypeID(...) { return {}; }	// default

// ZfVFieldUDT encapsulates introspected UDT metadata
struct ZfVFieldUDT {
  ZuID			id;	// ZfVFieldTypeID(T *);
  const std::type_info	*info;
  ZfVFieldPrint		print;
  ZfVFieldScan		scan;
};

// ZfVFieldType encapsulates introspected type metadata
struct ZfVFieldType {
  int			code;		// ZfFieldTC
  ZfVFieldProp::T	props;		// ZfVFieldProp

  union {
    void		*null;
    ZfVFieldEnum *	(*enum_)();	// Enum
    ZfVFieldFlags *	(*flags)();	// Flags
    ZfVFieldUDT *	(*udt)();	// UDT
  } info;
};

// ZfVFieldConstant is used to retrieve field constants
namespace ZfVFieldConstant {
  enum { Null = 0, Deflt, Minimum, Maximum };
}

// monomorphic (type-erased) equivalent of ZfField
struct ZfVField;

namespace ZfStruct_ {

// printing and string quoting
namespace Print {

// string and C string quoting
using namespace ZtQuote;

// bytes printing (base64)
using Bytes = Base64;

} // Print

// string and string vector element scanning
namespace Scan {
  ZuInline static constexpr bool isspace__(char c) {
    return ((c >= '\t' && c <= '\r') || c == ' ');
  }

  ZfExtern unsigned string(ZuSpan<char> dst, ZuCSpan &src);

  ZfExtern unsigned strElem(
    ZuSpan<char> dst, ZuCSpan &src,
    ZuCSpan delim, ZuCSpan suffix);
} // Scan

// these types are only used by the monomorphic (type-erased) API
ZuDerive(CStringVec, ZuVArray<const char *>);
ZuDerive(StringVec, ZuVArray<ZuCSpan>);
ZuDerive(BytesVec, ZuVArray<ZuBSpan>);
ZuDerive(Int8Vec, ZuVArray<int8_t>);
ZuDerive(UInt8Vec, ZuVArray<uint8_t>);
ZuDerive(Int16Vec, ZuVArray<int16_t>);
ZuDerive(UInt16Vec, ZuVArray<uint16_t>);
ZuDerive(Int32Vec, ZuVArray<int32_t>);
ZuDerive(UInt32Vec, ZuVArray<uint32_t>);
ZuDerive(Int64Vec, ZuVArray<int64_t>);
ZuDerive(UInt64Vec, ZuVArray<uint64_t>);
ZuDerive(Int128Vec, ZuVArray<int128_t>);
ZuDerive(UInt128Vec, ZuVArray<uint128_t>);
ZuDerive(FloatVec, ZuVArray<double>);
ZuDerive(FixedVec, ZuVArray<ZuFixed>);
ZuDerive(DecimalVec, ZuVArray<ZuDecimal>);
ZuDerive(TimeVec, ZuVArray<ZuTime>);
ZuDerive(DateTimeVec, ZuVArray<ZuDateTime>);

// monomorphic field get/print
struct VGet {

  union {
    void		*null;

    const char *	(*cstring)(const void *);	// CString
    ZuCSpan		(*string)(const void *);	// String
    ZuBSpan		(*bytes)(const void *);		// Bytes
    bool		(*bool_)(const void *);		// Bool
    int8_t		(*int8)(const void *);		// Int8
    uint8_t		(*uint8)(const void *);		// UInt8
    int16_t		(*int16)(const void *);		// Int16
    uint16_t		(*uint16)(const void *);	// UInt16
    int32_t		(*int32)(const void *);		// Int32
    uint32_t		(*uint32)(const void *);	// UInt32
    int64_t		(*int64)(const void *);		// Int64
    uint64_t		(*uint64)(const void *);	// UInt64
    int128_t		(*int128)(const void *);	// Int128
    uint128_t		(*uint128)(const void *);	// UInt128
    int			(*enum_)(const void *);		// Enum
    uint128_t		(*flags)(const void *);		// Flags
    double		(*float_)(const void *);	// Float
    ZuFixed		(*fixed)(const void *);		// Fixed
    ZuDecimal		(*decimal)(const void *);	// Decimal
    ZuTime		(*time)(const void *);		// Time
    ZuDateTime		(*dateTime)(const void *);	// DateTime
    const void *	(*udt)(const void *);		// UDT

    CStringVec		(*cstringVec)(const void *);
    StringVec		(*stringVec)(const void *);
    BytesVec		(*bytesVec)(const void *);
    Int8Vec		(*int8Vec)(const void *);
    UInt8Vec		(*uint8Vec)(const void *);
    Int16Vec		(*int16Vec)(const void *);
    UInt16Vec		(*uint16Vec)(const void *);
    Int32Vec		(*int32Vec)(const void *);
    UInt32Vec		(*uint32Vec)(const void *);
    Int64Vec		(*int64Vec)(const void *);
    UInt64Vec		(*uint64Vec)(const void *);
    Int128Vec		(*int128Vec)(const void *);
    UInt128Vec		(*uint128Vec)(const void *);
    FloatVec		(*floatVec)(const void *);
    FixedVec		(*fixedVec)(const void *);
    DecimalVec		(*decimalVec)(const void *);
    TimeVec		(*timeVec)(const void *);
    DateTimeVec		(*dateTimeVec)(const void *);
  } get_;

  template <unsigned Code, typename = ZuIfT<(Code < ZfFieldTC::N)>>
  auto get(const void *o) const {
#define ZfVField_GetFn(code, type, fn) \
    if constexpr (Code == ZfFieldTC::code) return get_.fn(o);
    ZfVField_GetFn(CString, const char *, cstring)
    ZfVField_GetFn(String, ZuCSpan, string)
    ZfVField_GetFn(Bytes, ZuBSpan, bytes)
    ZfVField_GetFn(Bool, bool, bool_)
    ZfVField_GetFn(Int8, int8_t, int8)
    ZfVField_GetFn(UInt8, uint8_t, uint8)
    ZfVField_GetFn(Int16, int16_t, int16)
    ZfVField_GetFn(UInt16, uint16_t, uint16)
    ZfVField_GetFn(Int32, int32_t, int32)
    ZfVField_GetFn(UInt32, uint32_t, uint32)
    ZfVField_GetFn(Int64, int64_t, int64)
    ZfVField_GetFn(UInt64, uint64_t, uint64)
    ZfVField_GetFn(Int128, int128_t, int128)
    ZfVField_GetFn(UInt128, uint128_t, uint128)
    ZfVField_GetFn(Float, double, float_)
    ZfVField_GetFn(Fixed, ZuFixed, fixed)
    ZfVField_GetFn(Decimal, ZuDecimal, decimal)
    ZfVField_GetFn(Time, ZuTime, time)
    ZfVField_GetFn(DateTime, ZuDateTime, dateTime)
    ZfVField_GetFn(UDT, const void *, udt)
    ZfVField_GetFn(CStringVec, CStringVec, cstringVec)
    ZfVField_GetFn(StringVec, StringVec, stringVec)
    ZfVField_GetFn(BytesVec, BytesVec, bytesVec)
    ZfVField_GetFn(Int8Vec, Int8Vec, int8Vec)
    ZfVField_GetFn(UInt8Vec, UInt8Vec, uint8Vec)
    ZfVField_GetFn(Int16Vec, Int16Vec, int16Vec)
    ZfVField_GetFn(UInt16Vec, UInt16Vec, uint16Vec)
    ZfVField_GetFn(Int32Vec, Int32Vec, int32Vec)
    ZfVField_GetFn(UInt32Vec, UInt32Vec, uint32Vec)
    ZfVField_GetFn(Int64Vec, Int64Vec, int64Vec)
    ZfVField_GetFn(UInt64Vec, UInt64Vec, uint64Vec)
    ZfVField_GetFn(Int128Vec, Int128Vec, int128Vec)
    ZfVField_GetFn(UInt128Vec, UInt128Vec, uint128Vec)
    ZfVField_GetFn(FloatVec, FloatVec, floatVec)
    ZfVField_GetFn(FixedVec, FixedVec, fixedVec)
    ZfVField_GetFn(DecimalVec, DecimalVec, decimalVec)
    ZfVField_GetFn(TimeVec, TimeVec, timeVec)
    ZfVField_GetFn(DateTimeVec, DateTimeVec, dateTimeVec)
#undef ZfVField_GetFn
  }

  template <unsigned Code, typename S,
    typename = ZuIfT<(Code < ZfFieldTC::N)>>
  void print(S &, const void *, const ZfVField *, const ZtVFmt &) const;

};

// monomorphic field set/scan
struct VSet {
  union {
    void	*null;

    void	(*cstring)(void *, const char *);	// CString
    void	(*string)(void *, ZuCSpan);		// String
    void	(*bytes)(void *, ZuBSpan);		// Bytes
    void	(*bool_)(void *, bool);			// Bool
    void	(*int8)(void *, int8_t);		// Int8
    void	(*uint8)(void *, uint8_t);		// UInt8
    void	(*int16)(void *, int16_t);		// Int16
    void	(*uint16)(void *, uint16_t);		// UInt16
    void	(*int32)(void *, int32_t);		// Int32
    void	(*uint32)(void *, uint32_t);		// UInt32
    void	(*int64)(void *, int64_t);		// Int64
    void	(*uint64)(void *, uint64_t);		// UInt64
    void	(*int128)(void *, int128_t);		// Int128
    void	(*uint128)(void *, uint128_t);		// UInt128
    void	(*float_)(void *, double);		// Float
    void	(*fixed)(void *, ZuFixed);		// Fixed
    void	(*decimal)(void *, ZuDecimal);		// Decimal
    void	(*time)(void *, ZuTime);		// Time
    void	(*dateTime)(void *, ZuDateTime);	// DateTime
    void	(*udt)(void *, const void *);		// UDT

    void	(*cstringVec)(void *, CStringVec);
    void	(*stringVec)(void *, StringVec);
    void	(*bytesVec)(void *, BytesVec);
    void	(*int8Vec)(void *, Int8Vec);
    void	(*uint8Vec)(void *, UInt8Vec);
    void	(*int16Vec)(void *, Int16Vec);
    void	(*uint16Vec)(void *, UInt16Vec);
    void	(*int32Vec)(void *, Int32Vec);
    void	(*uint32Vec)(void *, UInt32Vec);
    void	(*int64Vec)(void *, Int64Vec);
    void	(*uint64Vec)(void *, UInt64Vec);
    void	(*int128Vec)(void *, Int128Vec);
    void	(*uint128Vec)(void *, UInt128Vec);
    void	(*floatVec)(void *, FloatVec);
    void	(*fixedVec)(void *, FixedVec);
    void	(*decimalVec)(void *, DecimalVec);
    void	(*timeVec)(void *, TimeVec);
    void	(*dateTimeVec)(void *, DateTimeVec);
  } set_;

#define ZfVField_SetFn(code, type, fn) \
  template <unsigned Code, typename = ZuIfT<Code == ZfFieldTC::code>> \
  void set(void *o, type v) const { set_.fn(o, v); }

  ZfVField_SetFn(CString, const char *, cstring)
  ZfVField_SetFn(String, ZuCSpan, string)
  ZfVField_SetFn(Bytes, ZuBSpan, bytes)
  ZfVField_SetFn(Bool, bool, bool_)
  ZfVField_SetFn(Int8, int8_t, int8)
  ZfVField_SetFn(UInt8, uint8_t, uint8)
  ZfVField_SetFn(Int16, int16_t, int16)
  ZfVField_SetFn(UInt16, uint16_t, uint16)
  ZfVField_SetFn(Int32, int32_t, int32)
  ZfVField_SetFn(UInt32, uint32_t, uint32)
  ZfVField_SetFn(Int64, int64_t, int64)
  ZfVField_SetFn(UInt64, uint64_t, uint64)
  ZfVField_SetFn(Int128, int128_t, int128)
  ZfVField_SetFn(UInt128, uint128_t, uint128)
  ZfVField_SetFn(Float, double, float_)
  ZfVField_SetFn(Fixed, ZuFixed, fixed)
  ZfVField_SetFn(Decimal, ZuDecimal, decimal)
  ZfVField_SetFn(Time, ZuTime, time)
  ZfVField_SetFn(DateTime, ZuDateTime, dateTime)
  ZfVField_SetFn(UDT, const void *, udt)
  ZfVField_SetFn(CStringVec, CStringVec, cstringVec)
  ZfVField_SetFn(StringVec, StringVec, stringVec)
  ZfVField_SetFn(BytesVec, BytesVec, bytesVec)
  ZfVField_SetFn(Int8Vec, Int8Vec, int8Vec)
  ZfVField_SetFn(UInt8Vec, UInt8Vec, uint8Vec)
  ZfVField_SetFn(Int16Vec, Int16Vec, int16Vec)
  ZfVField_SetFn(UInt16Vec, UInt16Vec, uint16Vec)
  ZfVField_SetFn(Int32Vec, Int32Vec, int32Vec)
  ZfVField_SetFn(UInt32Vec, UInt32Vec, uint32Vec)
  ZfVField_SetFn(Int64Vec, Int64Vec, int64Vec)
  ZfVField_SetFn(UInt64Vec, UInt64Vec, uint64Vec)
  ZfVField_SetFn(Int128Vec, Int128Vec, int128Vec)
  ZfVField_SetFn(UInt128Vec, UInt128Vec, uint128Vec)
  ZfVField_SetFn(FloatVec, FloatVec, floatVec)
  ZfVField_SetFn(FixedVec, FixedVec, fixedVec)
  ZfVField_SetFn(DecimalVec, DecimalVec, decimalVec)
  ZfVField_SetFn(TimeVec, TimeVec, timeVec)
  ZfVField_SetFn(DateTimeVec, DateTimeVec, dateTimeVec)

  template <unsigned Code,
    typename = ZuIfT<(Code < ZfFieldTC::N)>>
  void scan(void *, ZuCSpan, const ZfVField *, const ZtVFmt &) const;

};

} // ZfStruct_

using ZfVFieldGet = ZfStruct_::VGet;
using ZfVFieldSet = ZfStruct_::VSet;

// ZfVField is the monomorphic (type-erased) equivalent of ZfField
struct ZfVField {
  ZfVFieldType		*type;
  ZuCSpan		id;
  ZfVFieldProp::T	props;
  uint64_t		keys;
  uint64_t		group;
  uint64_t		descend;
  int16_t		ctor;	// -1 if not a constructor parameter
  int8_t		ndp;	// defaults to sentinel null

  ZfVFieldGet		get;
  ZfVFieldSet		set;

  ZfVFieldGet		constant;

  template <typename Field>
  ZfVField(Field) :
      type{Field::Type::vtype()},
      id{Field::id()},
      props{Field::mprops()},
      keys{ZuSeqBitmap<ZuFieldProp::GetKeys<typename Field::Props>>()},
      group{ZuSeqBitmap<ZuFieldProp::GetGroup<typename Field::Props>>()},
      descend{ZuSeqBitmap<ZuFieldProp::GetDescend<typename Field::Props>>()},
      ctor{ZfVFieldProp::GetCtor<typename Field::Props>{}},
      ndp{ZfVFieldProp::GetNDP<typename Field::Props>{}},
      get{Field::getFn()},
      set{Field::setFn()},
      constant{Field::constantFn()} { }

  // pseudo-pointer parameter to ZfVFieldGet::get() for the constant values,
  // - see ZfVFieldConstant
  static const void *cget(int c) {
    return reinterpret_cast<void *>(static_cast<uintptr_t>(c));
  }

  // need to de-conflict with print
  template <typename S> void print_(S &s) const {
    s << "id=" << id << " type=" << ZfFieldTC::name(type->code);
    s << " props=" << ZfVFieldProp::Map::Print(
      props & ~(ZfVFieldProp::Ctor() | ZfVFieldProp::NDP()));
    if (props & ZfVFieldProp::Ctor()) {
      if (props & ~(ZfVFieldProp::Ctor() | ZfVFieldProp::NDP())) s << '|';
      s << "Ctor(" << ctor << ')';
    }
    if (props & ZfVFieldProp::NDP()) {
      if (props & ~ZfVFieldProp::NDP()) s << '|';
      s << "NDP(" << int(ndp) << ')';
    }
    s << " keys=" << *reinterpret_cast<const ZuBitmap<64> *>(&keys);
    s << " group=" << *reinterpret_cast<const ZuBitmap<64> *>(&group);
    s << " descend=" << *reinterpret_cast<const ZuBitmap<64> *>(&descend);
  }
  friend ZuPrintLambda<[]() {
    return [](auto &s, const auto &v) { v.print_(s); };
  }> ZuPrintType(ZfVField *);
};

namespace ZfStruct_ {

// VGet print functions
template <typename S, typename T>
inline void ZfVField_printInt_(
  S &s, const T &v, const ZfVField *field, const ZtVFmt &fmt)
{
  if (ZuUnlikely(field->props & ZfVFieldProp::Enum())) {
    s << field->type->info.enum_()->print(v);
    return;
  }
  if (ZuUnlikely(field->props & ZfVFieldProp::Flags())) {
    ZuVStream s_{s};
    field->type->info.flags()->print(v, s_, fmt);
    return;
  }
  if (field->props & ZfVFieldProp::Hex()) {
    s << v.vfmt(fmt.scalar).hex();
    return;
  }
  s << v.vfmt(fmt.scalar);
}

template <unsigned Code, typename S, typename>
inline void
VGet::print(
  S &s, const void *o, const ZfVField *field,
  const ZtVFmt &fmt
) const {
  if constexpr (Code == ZfFieldTC::CString) {
    auto v = get_.cstring(o);
    s << Print::CString{v};
  } else if constexpr (Code == ZfFieldTC::String) {
    auto v = get_.string(o);
    s << Print::String{v};
  } else if constexpr (Code == ZfFieldTC::Bytes) {
    ZuBSpan v = get_.bytes(o);
    unsigned n = ZuBase64::enclen(v.length());
    auto buf = ZmScratch(uint8_t, n);
    buf.length(n);
    buf.length(ZuBase64::encode(buf, v));
    s << ZuCSpan(buf);
  } else if constexpr (Code == ZfFieldTC::Bool) {
    s << (get_.bool_(o) ? '1' : '0');
  }

#define ZfVField_printInt(width) \
  else if constexpr (Code == ZfFieldTC::Int##width) {  \
    ZuBox<int##width##_t> v = get_.int##width(o);  \
    ZfVField_printInt_(s, v, field, fmt);  \
  } \
  else if constexpr (Code == ZfFieldTC::UInt##width) {  \
    ZuBox<uint##width##_t> v = get_.uint##width(o);  \
    ZfVField_printInt_(s, v, field, fmt);  \
  }

  ZfVField_printInt(8)
  ZfVField_printInt(16)
  ZfVField_printInt(32)
  ZfVField_printInt(64)
  ZfVField_printInt(128)

  else if constexpr (Code == ZfFieldTC::Float) {
    ZuBox<double> v = get_.float_(o);
    auto ndp = field->ndp;
    if (!ZuNull(ndp))
      s << v.vfmt(fmt.scalar).fp(ndp);
    else
      s << v.vfmt(fmt.scalar);
  } else if constexpr (Code == ZfFieldTC::Fixed) {
    ZuFixed v = get_.fixed(o);
    auto ndp = field->ndp;
    if (!ZuNull(ndp))
      s << v.vfmt(fmt.scalar).fp(ndp);
    else
      s << v.vfmt(fmt.scalar);
  } else if constexpr (Code == ZfFieldTC::Decimal) {
    ZuDecimal v = get_.decimal(o);
    auto ndp = field->ndp;
    if (!ZuNull(ndp))
      s << v.vfmt(fmt.scalar).fp(ndp);
    else
      s << v.vfmt(fmt.scalar);
  } else if constexpr (Code == ZfFieldTC::Time) {
    ZuDateTime v{get_.time(o)};
    s << v.fmt(fmt.datePrint);
  } else if constexpr (Code == ZfFieldTC::DateTime) {
    ZuDateTime v{get_.dateTime(o)};
    s << v.fmt(fmt.datePrint);
  } else if constexpr (Code == ZfFieldTC::UDT) {
    ZuVStream stream{s};
    field->type->info.udt()->print(get_.udt(o), stream, fmt);
  } else if constexpr (Code == ZfFieldTC::CStringVec) {
    s << fmt.vecPrefix;
    bool first = true;
    CStringVec vec{get_.cstringVec(o)};
    vec.all([&s, &first, &fmt](const char *v) {
      if (!first) s << fmt.vecDelim; else first = false;
      s << Print::CString{v};
    });
    s << fmt.vecSuffix;
  } else if constexpr (Code == ZfFieldTC::StringVec) {
    s << fmt.vecPrefix;
    bool first = true;
    StringVec vec{get_.stringVec(o)};
    vec.all([&s, &first, &fmt](ZuCSpan v) {
      if (!first) s << fmt.vecDelim; else first = false;
      s << Print::String{v};
    });
    s << fmt.vecSuffix;
  } else if constexpr (Code == ZfFieldTC::BytesVec) {
    s << fmt.vecPrefix;
    bool first = true;
    BytesVec vec{get_.bytesVec(o)};
    vec.all([&s, &fmt, &first](ZuBSpan v) {
      if (!first) s << fmt.vecDelim; else first = false;
      s << Print::Bytes{v};
    });
    s << fmt.vecSuffix;
  }

#define ZfVField_printIntVec(width) \
  else if constexpr (Code == ZfFieldTC::Int##width##Vec) {  \
    s << fmt.vecPrefix;  \
    Int##width##Vec vec{get_.int##width##Vec(o)};  \
    bool first = true;  \
    vec.all([&s, field, &fmt, &first](ZuBox<int##width##_t> v) {  \
      if (!first) s << fmt.vecDelim; else first = false;  \
      ZfVField_printInt_(s, v, field, fmt);  \
    });  \
    s << fmt.vecSuffix;  \
  } \
  else if constexpr (Code == ZfFieldTC::UInt##width##Vec) {  \
    s << fmt.vecPrefix;  \
    UInt##width##Vec vec{get_.uint##width##Vec(o)};  \
    bool first = true;  \
    vec.all([&s, field, &fmt, &first](ZuBox<uint##width##_t> v) {  \
      if (!first) s << fmt.vecDelim; else first = false;  \
      ZfVField_printInt_(s, v, field, fmt);  \
    });  \
    s << fmt.vecSuffix;  \
  }
  ZfVField_printIntVec(8)
  ZfVField_printIntVec(16)
  ZfVField_printIntVec(32)
  ZfVField_printIntVec(64)
  ZfVField_printIntVec(128)

  else if constexpr (Code == ZfFieldTC::FloatVec) {
    s << fmt.vecPrefix;
    FloatVec vec{get_.floatVec(o)};
    auto ndp = field->ndp;
    bool first = true;
    if (!ZuNull(ndp))
      vec.all([&s, &fmt, ndp, &first](ZuBox<double> v) {
	if (!first) s << fmt.vecDelim; else first = false;
	s << v.vfmt(fmt.scalar).fp(ndp);
      });
    else
      vec.all([&s, &first, &fmt](ZuBox<double> v) {
	if (!first) s << fmt.vecDelim; else first = false;
	s << v.vfmt(fmt.scalar);
      });
    s << fmt.vecSuffix;
  } else if constexpr (Code == ZfFieldTC::FixedVec) {
    s << fmt.vecPrefix;
    bool first = true;
    FixedVec vec{get_.fixedVec(o)};
    auto ndp = field->ndp;
    if (!ZuNull(ndp))
      vec.all([&s, &fmt, ndp, &first](const ZuFixed &v) {
	if (!first) s << fmt.vecDelim; else first = false;
	s << v.vfmt(fmt.scalar).fp(ndp);
      });
    else
      vec.all([&s, &first, &fmt](const ZuFixed &v) {
	if (!first) s << fmt.vecDelim; else first = false;
	s << v.vfmt(fmt.scalar);
      });
    s << fmt.vecSuffix;
  } else if constexpr (Code == ZfFieldTC::DecimalVec) {
    s << fmt.vecPrefix;
    DecimalVec vec{get_.decimalVec(o)};
    auto ndp = field->ndp;
    bool first = true;
    if (!ZuNull(ndp))
      vec.all([&s, &fmt, ndp, &first](const ZuDecimal &v) {
	if (!first) s << fmt.vecDelim; else first = false;
	s << v.vfmt(fmt.scalar).fp(ndp);
      });
    else
      vec.all([&s, &first, &fmt](const ZuDecimal &v) {
	if (!first) s << fmt.vecDelim; else first = false;
	s << v.vfmt(fmt.scalar);
      });
    s << fmt.vecSuffix;
  } else if constexpr (Code == ZfFieldTC::TimeVec) {
    s << fmt.vecPrefix;
    bool first = true;
    TimeVec vec{get_.timeVec(o)};
    vec.all([&s, &fmt, &first](const ZuTime &v_) {
      ZuDateTime v{v_};
      if (!first) s << fmt.vecDelim; else first = false;
      s << v.fmt(fmt.datePrint);
    });
    s << fmt.vecSuffix;
  } else if constexpr (Code == ZfFieldTC::DateTimeVec) {
    s << fmt.vecPrefix;
    bool first = true;
    DateTimeVec vec{get_.dateTimeVec(o)};
    vec.all([&s, &fmt, &first](const ZuDateTime &v) {
      if (!first) s << fmt.vecDelim; else first = false;
      s << v.fmt(fmt.datePrint);
    });
    s << fmt.vecSuffix;
  }
}

namespace VecScan {

using namespace Scan;

inline bool match(ZuCSpan &s, ZuCSpan m) {
  unsigned n = m.length();
  if (s.length() < n || memcmp(&s[0], &m[0], n)) return false;
  s.offset(n);
  return true;
}
inline void skip(ZuCSpan &s) {
  s.trim();
}

// this is intentionally a 1-pass scan that does NOT validate the suffix
// - lambda(ZuCSpan &s) should advance s with s.offset() and return true
//   to continue
template <typename L>
inline unsigned scan(ZuCSpan &s, const ZtVFmt &fmt, L &&l) {
  auto begin = &s[0];
  skip(s);
  if (!match(s, fmt.vecPrefix)) return 0;
  skip(s);
  while (ZuFwd<L>(l)(s)) {
    skip(s);
    if (!match(s, fmt.vecDelim)) {
	match(s, fmt.vecSuffix);
	break;
    }
  }
  return &s[0] - begin;
}

} // VecScan

// VSet scan functions
template <typename T>
T ZfVField_scanInt_(ZuCSpan s, const ZfVField *field, const ZtVFmt &fmt)
{
  if (ZuUnlikely(field->props & ZfVFieldProp::Enum()))
    return field->type->info.enum_()->scan(s);
  if (ZuUnlikely(field->props & ZfVFieldProp::Flags()))
    return field->type->info.flags()->scan(s, fmt);
  if (field->props & ZfVFieldProp::Hex())
    return ZuBox<T>{ZuFmt::Hex<>{}, s};
  return ZuBox<T>{s};
}

inline ZuCSpan ZfVField_scanVecElem(ZuCSpan s, const ZtVFmt &fmt)
{
  unsigned delim = 0, suffix = 0;
  unsigned i = 0, n = s.length();
  for (i = 0; i < n; i++) {
    if (s[i] == fmt.vecDelim[delim]) {
      if (++delim == fmt.vecDelim.length()) break;
    } else
      delim = 0;
    if (s[i] == fmt.vecSuffix[suffix]) {
      if (++suffix == fmt.vecSuffix.length()) break;
    } else
      suffix = 0;
  }
  s.trunc(i);
  return s;
}

template <typename T>
int ZfVField_scanIntVec_(
  T &v, ZuCSpan s, const ZfVField *field, const ZtVFmt &fmt)
{
  if (ZuUnlikely(field->props & ZfVFieldProp::Enum())) {
    auto s_ = ZfVField_scanVecElem(s, fmt);
    auto v_ = field->type->info.enum_()->scan(s_);
    v = v_;
    if (v_ < 0) return -1;
    return int(s_.length());
  }
  if (ZuUnlikely(field->props & ZfVFieldProp::Flags())) {
    auto s_ = ZfVField_scanVecElem(s, fmt);
    auto v_ = field->type->info.flags()->scan(s_, fmt);
    v = v_;
    if (!v_) return -1;
    return int(s_.length());
  }
  if (field->props & ZfVFieldProp::Hex())
    return v.template scan<ZuFmt::Hex<>>(s);
  return v.scan(s);
}

template <unsigned Code, typename>
inline void
VSet::scan(
  void *o, ZuCSpan s, const ZfVField *field, const ZtVFmt &fmt
) const {
  if constexpr (Code == ZfFieldTC::CString) {
    if (!s) {
      set_.cstring(o, nullptr);
      return;
    }
    unsigned n = s.length() + 1;
    auto buf = ZmScratch(char, n);
    buf.length(n);
    buf.length(Scan::string(buf, s));
    buf.push('\0');
    set_.cstring(o, buf.data());
  } else if constexpr (Code == ZfFieldTC::String) {
    if (!s) {
      set_.string(o, s);
      return;
    }
    unsigned n = s.length();
    auto buf = ZmScratch(char, n);
    buf.length(n);
    buf.length(Scan::string(buf, s));
    set_.string(o, buf);
  } else if constexpr (Code == ZfFieldTC::Bytes) {
    unsigned n = ZuBase64::declen(s.length());
    auto buf = ZmScratch(uint8_t, n);
    buf.length(n);
    buf.length(ZuBase64::decode(buf, ZuBSpan{s}));
    set_.bytes(o, buf);
  } else if constexpr (Code == ZfFieldTC::Bool) {
    set_.bool_(o, ZtScanBool(s));
  }

#define ZfVField_scanInt(width) \
  else if constexpr (Code == ZfFieldTC::Int##width) {  \
    set_.int##width(o, ZfVField_scanInt_<int##width##_t>(s, field, fmt));  \
  } \
  else if constexpr (Code == ZfFieldTC::UInt##width) {  \
    set_.uint##width(o, ZfVField_scanInt_<uint##width##_t>(s, field, fmt));  \
  }

  ZfVField_scanInt(8)
  ZfVField_scanInt(16)
  ZfVField_scanInt(32)
  ZfVField_scanInt(64)
  ZfVField_scanInt(128)

  else if constexpr (Code == ZfFieldTC::Float) {
    set_.float_(o, ZuBox<double>{s});
  } else if constexpr (Code == ZfFieldTC::Fixed) {
    set_.fixed(o, ZuFixed{s});
  } else if constexpr (Code == ZfFieldTC::Decimal) {
    set_.decimal(o, ZuDecimal{s});
  } else if constexpr (Code == ZfFieldTC::Time) {
    set_.time(o, ZuDateTime{fmt.dateScan, s}.as_time());
  } else if constexpr (Code == ZfFieldTC::DateTime) {
    set_.dateTime(o, ZuDateTime{fmt.dateScan, s});
  } else if constexpr (Code == ZfFieldTC::UDT) {
    field->type->info.udt()->scan(field->set.set_.udt, o, s, fmt);
  } else if constexpr (Code == ZfFieldTC::CStringVec) {
    VecScan::scan(s, fmt, [this, o, &fmt](ZuCSpan &s) {
      unsigned m = s.length();
      auto buf = ZmScratch(char, m + 1);
      buf.length(m + 1);
      unsigned n = Scan::strElem(buf, s, fmt.vecDelim, fmt.vecSuffix);
      if (n) {
	buf.length(n);
	buf.push('\0');
	set_.cstring(o, buf.data());
	return true;
      }
      return false;
    });
  } else if constexpr (Code == ZfFieldTC::StringVec) {
    VecScan::scan(s, fmt, [this, o, &fmt](ZuCSpan &s) {
      unsigned m = s.length();
      auto buf = ZmScratch(char, m);
      buf.length(m);
      unsigned n = Scan::strElem(buf, s, fmt.vecDelim, fmt.vecSuffix);
      if (n) {
	buf.length(n);
	set_.string(o, buf);
	return true;
      }
      return false;
    });
  } else if constexpr (Code == ZfFieldTC::BytesVec) {
    VecScan::scan(s, fmt, [this, o](ZuCSpan &s) {
      unsigned n = 0;
      auto m = s.length();
      while (n < m && ZuBase64::is(s[n])) n++;
      n = ZuBase64::declen(m = n);
      if (n) {
	auto buf = ZmScratch(uint8_t, n);
	buf.length(n);
	buf.length(ZuBase64::decode(buf, ZuBSpan{s}));
	set_.bytes(o, ZuBSpan{buf});
	s.offset(m);
	return true;
      }
      return false;
    });
  }

  // scan forward until a delimiter, suffix or end of string is encountered

  // scan integer from a vector string

#define ZfVField_scanIntVec(width) \
  else if constexpr (Code == ZfFieldTC::Int##width##Vec) {  \
    VecScan::scan(s, fmt, [this, o, field, fmt](ZuCSpan &s) {  \
      ZuBox<int##width##_t> v;  \
      int n = ZfVField_scanIntVec_(v, s, field, fmt);  \
      if (n > 0) {  \
	set_.int##width(o, v);  \
	s.offset(n);  \
	return true;  \
      }  \
      return false;  \
    });  \
  } \
  else if constexpr (Code == ZfFieldTC::UInt##width##Vec) {  \
    VecScan::scan(s, fmt, [this, o, field, fmt](ZuCSpan &s) {  \
      ZuBox<uint##width##_t> v;  \
      int n = ZfVField_scanIntVec_(v, s, field, fmt);  \
      if (n > 0) {  \
	set_.uint##width(o, v);  \
	s.offset(n);  \
	return true;  \
      }  \
      return false;  \
    });  \
  }

  ZfVField_scanIntVec(8)
  ZfVField_scanIntVec(16)
  ZfVField_scanIntVec(32)
  ZfVField_scanIntVec(64)
  ZfVField_scanIntVec(128)

  else if constexpr (Code == ZfFieldTC::FloatVec) {
    VecScan::scan(s, fmt, [this, o](ZuCSpan &s) {
      ZuBox<double> v;
      int n = v.scan(s);
      if (n > 0) {
	set_.float_(o, v);
	s.offset(n);
	return true;
      }
      return false;
    });
  } else if constexpr (Code == ZfFieldTC::FixedVec) {
    VecScan::scan(s, fmt, [this, o](ZuCSpan &s) {
      ZuFixed v;
      int n = v.scan(s);
      if (n > 0) {
	set_.fixed(o, v);
	s.offset(n);
	return true;
      }
      return false;
    });
  } else if constexpr (Code == ZfFieldTC::DecimalVec) {
    VecScan::scan(s, fmt, [this, o](ZuCSpan &s) {
      ZuDecimal v;
      int n = v.scan(s);
      if (n > 0) {
	set_.decimal(o, v);
	s.offset(n);
	return true;
      }
      return false;
    });
  } else if constexpr (Code == ZfFieldTC::TimeVec) {
    VecScan::scan(s, fmt, [this, o, &fmt](ZuCSpan &s) {
      ZuDateTime v;
      int n = v.scan(fmt.dateScan, s);
      if (n > 0) {
	set_.time(o, v.as_time());
	s.offset(n);
	return true;
      }
      return false;
    });
  } else if constexpr (Code == ZfFieldTC::DateTimeVec) {
    VecScan::scan(s, fmt, [this, o, &fmt](ZuCSpan &s) {
      ZuDateTime v;
      int n = v.scan(fmt.dateScan, s);
      if (n > 0) {
	set_.dateTime(o, ZuMv(v));
	s.offset(n);
	return true;
      }
      return false;
    });
  }
}

} // ZfStruct_

// ZfField compile-time encapsulates an individual field, derives from ZuField
template <typename Base_>
struct ZfField_ : public Base_ {
  using Base = Base_;
  using Orig = Base;
  using O = typename Base::O;
  using T = typename Base::T;
  using Props = typename Base::Props;
  static constexpr ZfVFieldProp::T mprops() {
    return ZfVFieldProp::Value<Props>{};
  }
};

// --- CString

template <typename T, typename Props>
struct ZfFieldType_CString;
template <typename Props_>
struct ZfFieldType_CString<char *, Props_> : public ZfFieldType_<Props_> {
  enum { Code = ZfFieldTC::CString };
  using T = char *;
  using Props = Props_;
  template <typename = ZtFmt::Default>
  using Print = ZfStruct_::Print::CString;
  inline static ZfVFieldType *vtype();
};
template <typename T, typename Props>
struct ZfFieldType<ZfFieldTC::CString, T, Props> :
    public ZfFieldType_CString<T, Props> { };

template <typename T, typename Props>
struct ZfVFieldType_CString;
template <typename Props>
struct ZfVFieldType_CString<char *, Props> : public ZfVFieldType {
  using T = char *;
  ZfVFieldType_CString() : ZfVFieldType{
    .code = ZfFieldTC::CString,
    .props = ZfVFieldProp::Value<Props>{},
    .info = {.null = nullptr}
  } { }
};
template <typename Props>
ZfVFieldType *ZfFieldType_CString<char *, Props>::vtype() {
  return ZmSingleton<ZfVFieldType_CString<char *, Props>>::instance();
}

template <auto Def, typename = void>
struct ZfFieldDefltFn : public ZuFalse { };
template <auto Def>
struct ZfFieldDefltFn<Def, decltype(Def(), void())> : public ZuTrue { };

template <typename Props, auto Def>
static constexpr decltype(auto) ZfFieldDeflt() {
  if constexpr (ZuFieldProp::HasDeflt<Props>{})
    return ZuFieldProp::GetDeflt<Props>::deflt();
  else if constexpr (ZfFieldDefltFn<Def>{})
    return Def();
  else
    return (Def);
};

template <typename Base, bool = Base::ReadOnly>
struct ZfField_CString : public ZfField_<Base> {
  template <template <typename> class Override>
  using Adapt = ZfField_CString<Override<Base>>;
  using O = typename Base::O;
  using T = const char *;
  using Props = typename Base::Props;
  using Type =
    ZfFieldType_CString<char *, ZuTypeGrep<ZfFieldType_Props, Props>>;
  enum { Code = Type::Code };
  static ZfVFieldGet getFn() {
    return {.get_ = {.cstring = [](const void *o) -> const char * {
      return Base::get(*static_cast<const O *>(o));
    }}};
  }
  static ZfVFieldSet setFn() {
    return {.set_ = {.cstring = [](void *, const char *) { }}};
  }
  static const char *deflt() {
    return ZfFieldDeflt<Props, static_cast<const char *>(nullptr)>();
  }
  static ZfVFieldGet constantFn() {
    using namespace ZfVFieldConstant;
    return {.get_ = {.cstring = [](const void *o) -> const char * {
      switch (int(reinterpret_cast<uintptr_t>(o))) {
	case Deflt: return deflt();
	default:    return nullptr;
      }
    }}};
  }
};
template <typename Base>
struct ZfField_CString<Base, false> :
    public ZfField_CString<Base, true> {
  using O = typename Base::O;
  static ZfVFieldSet setFn() {
    return {.set_ = {.cstring = [](void *o_, unsigned, const char *s) {
      O &o = *static_cast<const O *>(o_);
      auto ptr = Base::get(o);
      if (ptr) ::free(ptr);
      Base::set(o, s ? strdup(s) : static_cast<const char *>(nullptr));
    }}};
  }
};

// --- String

template <typename T_, typename Props_>
struct ZfFieldType_String : public ZfFieldType_<Props_> {
  enum { Code = ZfFieldTC::String };
  using T = T_;
  using Props = Props_;
  template <typename = ZtFmt::Default>
  using Print = ZfStruct_::Print::String;
  inline static ZfVFieldType *vtype();
};
template <typename T, typename Props>
struct ZfFieldType<ZfFieldTC::String, T, Props> :
    public ZfFieldType_String<T, Props> { };

template <typename T, typename Props>
struct ZfVFieldType_String : public ZfVFieldType {
  ZfVFieldType_String() : ZfVFieldType{
    .code = ZfFieldTC::String,
    .props = ZfVFieldProp::Value<Props>{},
    .info = {.null = nullptr}
  } { }
};
template <typename T, typename Props>
ZfVFieldType *ZfFieldType_String<T, Props>::vtype() {
  return ZmSingleton<ZfVFieldType_String<T, Props>>::instance();
}

template <typename Base, typename = void>
struct ZfField_String_Get {
  static ZfVFieldGet getFn() {
    using O = typename Base::O;
    return {.get_ = {.string = [](const void *o) -> ZuCSpan {
      using T = decltype(Base::get(ZuDeclVal<const O &>()));
      using Char = decltype(ZuDeclVal<const T &>()[0]);
      if constexpr (ZuIsCRef<T>{} && sizeof(Char) == 1) {
	// field get returns a cref - elide the copy
	return Base::get(*static_cast<const O *>(o));
      } else if constexpr (sizeof(Char) == 1) {
	// field get returns a temporary - stash it
	using V = ZuDecay<T>;
	auto &v = ZmTLS<V, getFn>();
	v = Base::get(*static_cast<const O *>(o));
	return v;
      } else {
	// field get returns a wide string - copy it and stash it
	using String = ZtString<ZtStringHeapID_<ZmHeapID<T>>>;
	auto &v = ZmTLS<String, getFn>();
	v = Base::get(*static_cast<const O *>(o));
	return v;
      }
    }}};
  }
};
template <typename Base>
struct ZfField_String_Get<Base,
    decltype(&Base::get(ZuDeclVal<const typename Base::O &>()), void())> {
  static ZfVFieldGet getFn() {
    using O = typename Base::O;
    // field get() returns a crvalue
    return {.get_ = {.string = [](const void *o) -> ZuCSpan {
      return Base::get(*static_cast<const O *>(o));
    }}};
  }
};

template <typename Base, bool = Base::ReadOnly>
struct ZfField_String :
    public ZfField_<Base>,
    public ZfField_String_Get<Base> {
  template <template <typename> class Override>
  using Adapt = ZfField_String<Override<Base>>;
  using O = typename Base::O;
  using T = typename Base::T;
  using Props = typename Base::Props;
  using Type = ZfFieldType_String<T, ZuTypeGrep<ZfFieldType_Props, Props>>;
  enum { Code = Type::Code };
  static ZfVFieldSet setFn() {
    return {.set_ = {.string = [](void *, ZuCSpan) { }}};
  }
  static constexpr ZuCSpan deflt() {
    return ZfFieldDeflt<Props, [] { return ZuCSpan{}; }>();
  }
  static ZfVFieldGet constantFn() {
    using namespace ZfVFieldConstant;
    return {.get_ = {.string = [](const void *o) -> ZuCSpan {
      switch (int(reinterpret_cast<uintptr_t>(o))) {
	case Deflt: return deflt();
	default:    return {};
      }
    }}};
  }
};
template <typename Base>
struct ZfField_String<Base, false> :
    public ZfField_String<Base, true> {
  using O = typename Base::O;
  static ZfVFieldSet setFn() {
    return {.set_ = {.string = [](void *o, ZuCSpan s) {
      Base::set(*static_cast<O *>(o), s);
    }}};
  }
};

// --- Bytes

template <typename T_, typename Props_>
struct ZfFieldType_Bytes : public ZfFieldType_<Props_> {
  enum { Code = ZfFieldTC::Bytes };
  using T = T_;
  using Props = Props_;
  template <typename = ZtFmt::Default>
  using Print = ZfStruct_::Print::Bytes;
  inline static ZfVFieldType *vtype();
};
template <typename T, typename Props>
struct ZfFieldType<ZfFieldTC::Bytes, T, Props> :
    public ZfFieldType_Bytes<T, Props> { };

template <typename T, typename Props>
struct ZfVFieldType_Bytes : public ZfVFieldType {
  ZfVFieldType_Bytes() : ZfVFieldType{
    .code = ZfFieldTC::Bytes,
    .props = ZfVFieldProp::Value<Props>{},
    .info = {.null = nullptr}
  } { }
};
template <typename T, typename Props>
ZfVFieldType *ZfFieldType_Bytes<T, Props>::vtype() {
  return ZmSingleton<ZfVFieldType_Bytes<T, Props>>::instance();
}

template <typename Base, typename = void>
struct ZfField_Bytes_Get {
  static ZfVFieldGet getFn() {
    using O = typename Base::O;
    // field get() returns a temporary
    return {.get_ = {.bytes = [](const void *o) -> ZuBSpan {
      auto &v = ZmTLS<ZtBArray, getFn>();
      v = Base::get(*static_cast<const O *>(o));
      return v;
    }}};
  }
};
template <typename Base>
struct ZfField_Bytes_Get<Base,
    decltype(&Base::get(ZuDeclVal<const typename Base::O &>()), void())> {
  static ZfVFieldGet getFn() {
    using O = typename Base::O;
    // field get() returns a crvalue
    return {.get_ = {.bytes = [](const void *o) -> ZuBSpan {
      return Base::get(*static_cast<const O *>(o));
    }}};
  }
};
template <typename Base, bool = Base::ReadOnly>
struct ZfField_Bytes :
    public ZfField_<Base>,
    public ZfField_Bytes_Get<Base> {
  template <template <typename> class Override>
  using Adapt = ZfField_Bytes<Override<Base>>;
  using O = typename Base::O;
  using T = typename Base::T;
  using Props = typename Base::Props;
  using Type = ZfFieldType_Bytes<T, ZuTypeGrep<ZfFieldType_Props, Props>>;
  enum { Code = Type::Code };
  static ZfVFieldSet setFn() {
    return {.set_ = {.bytes = [](void *, ZuBSpan) { }}};
  }
  static constexpr ZuBSpan deflt() {
    return ZfFieldDeflt<Props, [] { return ZuBSpan{}; }>();
  }
  static ZfVFieldGet constantFn() {
    using namespace ZfVFieldConstant;
    return {.get_ = {.bytes = [](const void *o) -> ZuBSpan {
      switch (int(reinterpret_cast<uintptr_t>(o))) {
	case Deflt: return deflt();
	default:    return {};
      }
    }}};
  }
};
template <typename Base>
struct ZfField_Bytes<Base, false> :
    public ZfField_Bytes<Base, true> {
  using O = typename Base::O;
  static ZfVFieldSet setFn() {
    return {.set_ = {.bytes = [](void *o, ZuBSpan v) {
      Base::set(*static_cast<O *>(o), v);
    }}};
  }
};

// --- Bool

template <typename T_, typename Props_>
struct ZfFieldType_Bool : public ZfFieldType_<Props_> {
  enum { Code = ZfFieldTC::Bool };
  using T = T_;
  using Props = Props_;
  template <typename = ZtFmt::Default> struct Print {
    bool v;
    template <typename S>
    friend inline decltype(auto) operator <<(S &s, const Print &print) {
      return s << (print.v ? '1' : '0');
    }
  };
  inline static ZfVFieldType *vtype();
};
template <typename T, typename Props>
struct ZfFieldType<ZfFieldTC::Bool, T, Props> :
    public ZfFieldType_Bool<T, Props> { };

template <typename T, typename Props>
struct ZfVFieldType_Bool : public ZfVFieldType {
  ZfVFieldType_Bool() : ZfVFieldType{
    .code = ZfFieldTC::Bool,
    .props = ZfVFieldProp::Value<Props>{},
    .info = {.null = nullptr}
  } { }
};
template <typename T, typename Props>
ZfVFieldType *ZfFieldType_Bool<T, Props>::vtype() {
  return ZmSingleton<ZfVFieldType_Bool<T, Props>>::instance();
}

template <typename Base, bool = Base::ReadOnly>
struct ZfField_Bool : public ZfField_<Base> {
  template <template <typename> class Override>
  using Adapt = ZfField_Bool<Override<Base>>;
  using O = typename Base::O;
  using T = typename Base::T;
  using Props = typename Base::Props;
  using Type = ZfFieldType_Bool<T, ZuTypeGrep<ZfFieldType_Props, Props>>;
  enum { Code = Type::Code };
  static ZfVFieldGet getFn() {
    return {.get_ = {.bool_ = [](const void *o) -> bool {
      return Base::get(*static_cast<const O *>(o));
    }}};
  }
  static ZfVFieldSet setFn() {
    return {.set_ = {.bool_ = [](void *, bool) { }}};
  }
  static constexpr bool deflt() {
    return ZfFieldDeflt<Props, false>();
  }
  static ZfVFieldGet constantFn() {
    using namespace ZfVFieldConstant;
    return {.get_ = {.bool_ = [](const void *o) -> bool {
      switch (int(reinterpret_cast<uintptr_t>(o))) {
	case Deflt:   return deflt();
	case Minimum: return false;
	case Maximum: return true;
	default:      return false;
      }
    }}};
  }
};
template <typename Base>
struct ZfField_Bool<Base, false> :
    public ZfField_Bool<Base, true> {
  using O = typename Base::O;
  static ZfVFieldSet setFn() {
    return {.set_ = {.bool_ = [](void *o, bool v) {
      Base::set(*static_cast<O *>(o), v);
    }}};
  }
};

// --- {Int,UInt}{8,16,32,64,128}

template <typename Props, typename Fmt, typename B>
struct ZfFieldPrintInt {
  B	value;

  template <typename S>
  friend inline decltype(auto) operator <<(S &s, const ZfFieldPrintInt &v) {
    using namespace ZuFieldProp;
    if constexpr (ZuFieldProp::HasEnum<Props>{})
      return s << GetEnum<Props>::v2s(v.value);
    else if constexpr (ZuFieldProp::HasFlags<Props>{})
      return s << typename GetFlags<Props>::Print(v.value, Fmt::FlagsDelim());
    else if constexpr (ZuTypeIn<ZuFieldProp::Hex, Props>{})
      return s << v.value.template hex<false, Fmt>();
    else
      return s << v.value.template fmt<Fmt>();
  }
};

// returns true if:
// - no range is specified
// - value was within range
// - value was already null;
// returns false and sets value to null if:
// - value was out of range
template <typename Props, typename T>
inline bool ZfFieldLimit(T &v) {
  if constexpr (!ZuFieldProp::HasRange<Props>{}) {
    return true;
  } else {
    if (ZuCmp<T>::null(v)) return true;
    using Range = ZuFieldProp::GetRange<Props>;
    if (v < Range::minimum() || v > Range::maximum()) {
      v = ZuCmp<T>::null();
      return false;
    }
    return true;
  }
}

template <typename Props, typename Fmt, typename B>
struct ZfFieldScanInt {
  B	value;

  using Result = ZuTuple<int, B>;

  ZfFieldScanInt() = default;
  ZfFieldScanInt(ZuCSpan s) { scan(s); }

  int scan(ZuCSpan s) {
    auto r = eov(s);
    value = r.template p<1>();
    return r.template p<0>();
  }
  static Result eov(ZuCSpan s) {
    using namespace ZuFieldProp;
    if constexpr (ZuFieldProp::HasEnum<Props>{}) {
      using Map = ZuFieldProp::GetEnum<Props>;
      auto i = Map::match(s);
      if (i < 0) return {-1, B{}};
      return validate({int(Map::v2s(i).length()), B{i}});
    } else if constexpr (ZuFieldProp::HasFlags<Props>{}) {
      using Map = ZuFieldProp::GetFlags<Props>;
      using Scan = typename Map::Scan;
      auto r = Scan::eov(s, Fmt::FlagsDelim());
      if (r.template p<0>() < 0) return {-1, B{}};
      return validate({r.template p<0>(), B{r.template p<1>().val()}});
    } else if constexpr (ZuTypeIn<ZuFieldProp::Hex, Props>{}) {
      // hex case is immaterial in scanning
      return validate(B::template eov<ZuFmt::Hex<false, Fmt>>(s));
    } else {
      return validate(B::template eov<Fmt>(s));
    }
  }
  static Result validate(Result r) {
    if constexpr (!ZuFieldProp::HasRange<Props>{}) {
      return r;
    } else {
      if (r.template p<0>() < 0) return r;
      using Range = ZuFieldProp::GetRange<Props>;
      if (r.template p<1>() < Range::minimum() ||
	  r.template p<1>() > Range::maximum())
	return {-1, B{}};
      return r;
    }
  }
};

template <typename T, typename Props, auto Min, auto Max>
struct ZfFieldRange {
  static constexpr T minimum() {
    if constexpr (ZuFieldProp::HasRange<Props>{})
      return ZuFieldProp::GetRange<Props>::minimum();
    else
      return Min();
  }
  static constexpr T maximum() {
    if constexpr (ZuFieldProp::HasRange<Props>{})
      return ZuFieldProp::GetRange<Props>::maximum();
    else
      return Max();
  }
};

#define ZfField_Int(Code_, Type_) \
template <typename T_, typename Props_> \
struct ZfFieldType_##Code_ : public ZfFieldType_<Props_> { \
  enum { Code = ZfFieldTC::Code_ }; \
  using T = T_; \
  using Props = Props_; \
  template <typename Fmt = ZtFmt::Default> struct Print { \
    ZuBox<T> v; \
    template <typename S> \
    friend inline decltype(auto) operator <<(S &s, const Print &print) { \
      return s << ZfFieldPrintInt<Props, Fmt, ZuBox<T>>(print.v); \
    } \
  }; \
  inline static ZfVFieldType *vtype(); \
}; \
template <typename T, typename Props> \
struct ZfFieldType<ZfFieldTC::Code_, T, Props> : \
    public ZfFieldType_##Code_<T, Props> { }; \
 \
template < \
  typename T, typename Props, \
  bool = ZuFieldProp::HasEnum<Props>{}, \
  bool = ZuFieldProp::HasFlags<Props>{}> \
struct ZfVFieldType_##Code_; \
template <typename T, typename Props> \
struct ZfVFieldType_##Code_<T, Props, false, false> : public ZfVFieldType { \
  ZfVFieldType_##Code_() : ZfVFieldType{ \
    .code = ZfFieldTC::Code_, \
    .props = ZfVFieldProp::Value<Props>{}, \
    .info = {.null = nullptr} \
  } { } \
}; \
template <typename T, typename Props> \
struct ZfVFieldType_##Code_<T, Props, true, false> : public ZfVFieldType { \
  ZfVFieldType_##Code_() : ZfVFieldType{ \
    .code = ZfFieldTC::Code_, \
    .props = ZfVFieldProp::Value<Props>{}, \
    .info = {.enum_ = []() -> ZfVFieldEnum * { \
      return ZfVFieldEnum_<ZuFieldProp::GetEnum<Props>>::instance(); \
    }} \
  } { } \
}; \
template <typename T, typename Props> \
struct ZfVFieldType_##Code_<T, Props, false, true> : public ZfVFieldType { \
  ZfVFieldType_##Code_() : ZfVFieldType{ \
    .code = ZfFieldTC::Code_, \
    .props = ZfVFieldProp::Value<Props>{}, \
    .info = {.flags = []() -> ZfVFieldFlags * { \
      return ZfVFieldFlags_<ZuFieldProp::GetFlags<Props>>::instance(); \
    }} \
  } { } \
}; \
template <typename T, typename Props> \
ZfVFieldType *ZfFieldType_##Code_<T, Props>::vtype() { \
  return ZmSingleton<ZfVFieldType_##Code_<T, Props>>::instance(); \
} \
 \
template <typename T> \
struct ZfFieldType_##Code_##_Def { \
  static constexpr auto minimum() { return ZuCmp<T>::minimum(); } \
  static constexpr auto maximum() { return ZuCmp<T>::maximum(); } \
}; \
template <typename Base, bool = Base::ReadOnly> \
struct ZfField_##Code_ : public ZfField_<Base> { \
  template <template <typename> class Override> \
  using Adapt = ZfField_##Code_<Override<Base>>; \
  using O = typename Base::O; \
  using T = typename Base::T; \
  using Props = typename Base::Props; \
  using Range = ZfFieldRange<T, Props, \
    ZfFieldType_##Code_##_Def<T>::minimum, \
    ZfFieldType_##Code_##_Def<T>::maximum>; \
  using Type = ZfFieldType_##Code_<T, ZuTypeGrep<ZfFieldType_Props, Props>>; \
  enum { Code = Type::Code }; \
  static ZfVFieldGet getFn() { \
    return {.get_ = {.Type_= [](const void *o) -> Type_##_t { \
      return Base::get(*static_cast<const O *>(o)); \
    }}}; \
  } \
  static ZfVFieldSet setFn() { \
    return {.set_ = {.Type_= [](void *, Type_##_t) { }}}; \
  } \
  static constexpr auto deflt() { \
    return ZfFieldDeflt<Props, [] { return ZuCmp<T>::null(); }>(); \
  } \
  static constexpr auto minimum() { return Range::minimum(); } \
  static constexpr auto maximum() { return Range::maximum(); } \
  static ZfVFieldGet constantFn() { \
    using namespace ZfVFieldConstant; \
    return {.get_ = {.Type_= [](const void *o) -> Type_##_t { \
      switch (int(reinterpret_cast<uintptr_t>(o))) { \
	case Deflt:   return deflt(); \
	case Minimum: return minimum(); \
	case Maximum: return maximum(); \
	default:      return ZuCmp<Type_##_t>::null(); \
      } \
    }}}; \
  } \
}; \
template <typename Base> \
struct ZfField_##Code_<Base, false> : \
    public ZfField_##Code_<Base, true> { \
  using O = typename Base::O; \
  using T = typename Base::T; \
  static ZfVFieldSet setFn() { \
    return {.set_ = {.Type_= [](void *o, Type_##_t v) { \
      Base::set(*static_cast<O *>(o), v); \
    }}}; \
  } \
};

ZfField_Int(Int8, int8);
ZfField_Int(UInt8, uint8);
ZfField_Int(Int16, int16);
ZfField_Int(UInt16, uint16);
ZfField_Int(Int32, int32);
ZfField_Int(UInt32, uint32);
ZfField_Int(Int64, int64);
ZfField_Int(UInt64, uint64);
ZfField_Int(Int128, int128);
ZfField_Int(UInt128, uint128);

// --- Float

template <typename T_, typename Props_>
struct ZfFieldType_Float : public ZfFieldType_<Props_> {
  enum { Code = ZfFieldTC::Float };
  using T = T_;
  using Props = Props_;
  template <typename Fmt = ZtFmt::Default> struct Print {
    ZuBox<ZuFPType<sizeof(T)>> v;
    template <typename S>
    friend inline decltype(auto) operator <<(S &s, const Print &print) {
      if constexpr (ZuFieldProp::HasNDP<Props>{})
	return
	  s << print.v.template fp<ZuFieldProp::GetNDP<Props>{}, '\0', Fmt>();
      else
	return s << print.v.template fmt<Fmt>();
    }
  };
  inline static ZfVFieldType *vtype();
};
template <typename T, typename Props>
struct ZfFieldType<ZfFieldTC::Float, T, Props> :
    public ZfFieldType_Float<T, Props> { };

template <typename T, typename Props>
struct ZfVFieldType_Float : public ZfVFieldType {
  ZfVFieldType_Float() : ZfVFieldType{
    .code = ZfFieldTC::Float,
    .props = ZfVFieldProp::Value<Props>{},
    .info = {.null = nullptr}
  } { }
};
template <typename T, typename Props>
ZfVFieldType *ZfFieldType_Float<T, Props>::vtype() {
  return ZmSingleton<ZfVFieldType_Float<T, Props>>::instance();
}

template <typename T>
struct ZfField_Float_Def {
  static constexpr auto minimum() { return T{-ZuFP<ZuUnder<T>>::inf()}; }
  static constexpr auto maximum() { return T{ZuFP<ZuUnder<T>>::inf()}; }
};
template <typename Base, bool = Base::ReadOnly>
struct ZfField_Float : public ZfField_<Base> {
  template <template <typename> class Override>
  using Adapt = ZfField_Float<Override<Base>>;
  using O = typename Base::O;
  using T = typename Base::T;
  using Props = typename Base::Props;
  using Range = ZfFieldRange<T, Props,
    ZfField_Float_Def<T>::minimum, ZfField_Float_Def<T>::maximum>;
  using Type = ZfFieldType_Float<T, ZuTypeGrep<ZfFieldType_Props, Props>>;
  enum { Code = Type::Code };
  static ZfVFieldGet getFn() {
    return {.get_ = {.float_ = [](const void *o) -> double {
      return Base::get(*static_cast<const O *>(o));
    }}};
  }
  static ZfVFieldSet setFn() {
    return {.set_ = {.float_ = [](void *, double) { }}};
  }
  static constexpr auto deflt() {
    return ZfFieldDeflt<Props, [] { return ZuCmp<T>::null(); }>();
  }
  static constexpr auto minimum() { return Range::minimum(); }
  static constexpr auto maximum() { return Range::maximum(); }
  static ZfVFieldGet constantFn() {
    using namespace ZfVFieldConstant;
    return {.get_ = {.float_ = [](const void *o) -> double {
      switch (int(reinterpret_cast<uintptr_t>(o))) {
	case Deflt:   return deflt();
	case Minimum: return minimum();
	case Maximum: return maximum();
	default:      return ZuCmp<double>::null();
      }
    }}};
  }
};
template <typename Base>
struct ZfField_Float<Base, false> :
    public ZfField_Float<Base, true> {
  using O = typename Base::O;
  using T = typename Base::T;
  static ZfVFieldSet setFn() {
    return {.set_ = {.float_ = [](void *o, double v) {
      Base::set(*static_cast<O *>(o), v);
    }}};
  }
};

// --- Fixed

template <typename T_, typename Props_>
struct ZfFieldType_Fixed : public ZfFieldType_<Props_> {
  enum { Code = ZfFieldTC::Fixed };
  using T = T_;
  using Props = Props_;
  template <typename Fmt = ZtFmt::Default> struct Print {
    ZuFixed v;
    template <typename S>
    friend inline decltype(auto) operator <<(S &s, const Print &print) {
      if constexpr (ZuFieldProp::HasNDP<Props>{})
	return
	  s << print.v.template fp<ZuFieldProp::GetNDP<Props>{}, '\0', Fmt>();
      else
	return s << print.v.template fmt<Fmt>();
    }
  };
  inline static ZfVFieldType *vtype();
};
template <typename T, typename Props>
struct ZfFieldType<ZfFieldTC::Fixed, T, Props> :
    public ZfFieldType_Fixed<T, Props> { };

template <typename T, typename Props>
struct ZfVFieldType_Fixed : public ZfVFieldType {
  ZfVFieldType_Fixed() : ZfVFieldType{
    .code = ZfFieldTC::Fixed,
    .props = ZfVFieldProp::Value<Props>{},
    .info = {.null = nullptr}
  } { }
};
template <typename T, typename Props>
ZfVFieldType *ZfFieldType_Fixed<T, Props>::vtype() {
  return ZmSingleton<ZfVFieldType_Fixed<T, Props>>::instance();
}

struct ZfField_Fixed_Def {
  static constexpr ZuFixed minimum() { return {ZuFixedMin, 0}; }
  static constexpr ZuFixed maximum() { return {ZuFixedMax, 0}; }
};
template <typename Base, bool = Base::ReadOnly>
struct ZfField_Fixed : public ZfField_<Base> {
  template <template <typename> class Override>
  using Adapt = ZfField_Fixed<Override<Base>>;
  using O = typename Base::O;
  using T = typename Base::T;
  using Props = typename Base::Props;
  using Range = ZfFieldRange<T, Props,
    ZfField_Fixed_Def::minimum, ZfField_Fixed_Def::maximum>;
  using Type = ZfFieldType_Fixed<T, ZuTypeGrep<ZfFieldType_Props, Props>>;
  enum { Code = Type::Code };
  static ZfVFieldGet getFn() {
    return {.get_ = {.fixed = [](const void *o) -> ZuFixed {
      return Base::get(*static_cast<const O *>(o));
    }}};
  }
  static ZfVFieldSet setFn() {
    return {.set_ = {.fixed = [](void *, ZuFixed) { }}};
  }
  static constexpr auto deflt() {
    return ZfFieldDeflt<Props, ZuFixed{}>();
  }
  static constexpr auto minimum() { return Range::minimum(); }
  static constexpr auto maximum() { return Range::maximum(); }
  static ZfVFieldGet constantFn() {
    using namespace ZfVFieldConstant;
    return {.get_ = {.fixed = [](const void *o) -> ZuFixed {
      switch (int(reinterpret_cast<uintptr_t>(o))) {
	case Deflt:   return deflt();
	case Minimum: return minimum();
	case Maximum: return maximum();
	default:      return {};
      }
    }}};
  }
};
template <typename Base>
struct ZfField_Fixed<Base, false> :
    public ZfField_Fixed<Base, true> {
  using O = typename Base::O;
  using T = typename Base::T;
  static ZfVFieldSet setFn() {
    return {.set_ = {.fixed = [](void *o, ZuFixed v) {
      Base::set(*static_cast<O *>(o), ZuMv(v));
    }}};
  }
};

// --- Decimal

template <typename T_, typename Props_>
struct ZfFieldType_Decimal : public ZfFieldType_<Props_> {
  enum { Code = ZfFieldTC::Decimal };
  using T = T_;
  using Props = Props_;
  template <typename Fmt = ZtFmt::Default> struct Print {
    ZuDecimal v;
    template <typename S>
    friend inline decltype(auto) operator <<(S &s, const Print &print) {
      if constexpr (ZuFieldProp::HasNDP<Props>{})
	return
	  s << print.v.template fp<ZuFieldProp::GetNDP<Props>{}, '\0', Fmt>();
      else
	return s << print.v.template fmt<Fmt>();
    }
  };
  inline static ZfVFieldType *vtype();
};
template <typename T, typename Props>
struct ZfFieldType<ZfFieldTC::Decimal, T, Props> :
    public ZfFieldType_Decimal<T, Props> { };

template <typename T, typename Props>
struct ZfVFieldType_Decimal : public ZfVFieldType {
  ZfVFieldType_Decimal() : ZfVFieldType{
    .code = ZfFieldTC::Decimal,
    .props = ZfVFieldProp::Value<Props>{},
    .info = {.null = nullptr}
  } { }
};
template <typename T, typename Props>
ZfVFieldType *ZfFieldType_Decimal<T, Props>::vtype() {
  return ZmSingleton<ZfVFieldType_Decimal<T, Props>>::instance();
}

struct ZfField_Decimal_Def {
  static constexpr ZuDecimal minimum() {
    return {ZuDecimal::Unscaled{ZuDecimal::minimum()}};
  }
  static constexpr ZuDecimal maximum() {
    return {ZuDecimal::Unscaled{ZuDecimal::maximum()}};
  }
};
template <typename Base, bool = Base::ReadOnly>
struct ZfField_Decimal : public ZfField_<Base> {
  template <template <typename> class Override>
  using Adapt = ZfField_Decimal<Override<Base>>;
  using O = typename Base::O;
  using T = typename Base::T;
  using Props = typename Base::Props;
  using Range = ZfFieldRange<T, Props,
    ZfField_Decimal_Def::minimum, ZfField_Decimal_Def::maximum>;
  using Type = ZfFieldType_Decimal<T, ZuTypeGrep<ZfFieldType_Props, Props>>;
  enum { Code = Type::Code };
  static ZfVFieldGet getFn() {
    return {.get_ = {.decimal = [](const void *o) -> ZuDecimal {
      return Base::get(*static_cast<const O *>(o));
    }}};
  }
  static ZfVFieldSet setFn() {
    return {.set_ = {.decimal = [](void *, ZuDecimal) { }}};
  }
  static constexpr auto deflt() {
    return ZfFieldDeflt<Props, ZuCmp<ZuDecimal>::null()>();
  }
  static constexpr auto minimum() { return Range::minimum(); }
  static constexpr auto maximum() { return Range::maximum(); }
  static ZfVFieldGet constantFn() {
    using namespace ZfVFieldConstant;
    return {.get_ = {.decimal = [](const void *o) -> ZuDecimal {
      switch (int(reinterpret_cast<uintptr_t>(o))) {
	case Deflt:   return deflt();
	case Minimum: return minimum();
	case Maximum: return maximum();
	default:      return {};
      }
    }}};
  }
};
template <typename Base>
struct ZfField_Decimal<Base, false> :
    public ZfField_Decimal<Base, true> {
  using O = typename Base::O;
  static ZfVFieldSet setFn() {
    return {.set_ = {.decimal = [](void *o, ZuDecimal v) {
      Base::set(*static_cast<O *>(o), ZuMv(v));
    }}};
  }
};

// --- Time

template <typename T_, typename Props_>
struct ZfFieldType_Time : public ZfFieldType_<Props_> {
  enum { Code = ZfFieldTC::Time };
  using T = T_;
  using Props = Props_;
  template <typename Fmt = ZtFmt::Default> struct Print {
    ZuTime v;
    template <typename S>
    friend inline decltype(auto) operator <<(S &s, const Print &print) {
      ZuDateTime v{print.v};
      return s << v.fmt(Fmt::DatePrint_());
    }
  };
  inline static ZfVFieldType *vtype();
};
template <typename T, typename Props>
struct ZfFieldType<ZfFieldTC::Time, T, Props> :
    public ZfFieldType_Time<T, Props> { };

template <typename T, typename Props>
struct ZfVFieldType_Time : public ZfVFieldType {
  ZfVFieldType_Time() : ZfVFieldType{
    .code = ZfFieldTC::Time,
    .props = ZfVFieldProp::Value<Props>{},
    .info = {.null = nullptr}
  } { }
};
template <typename T, typename Props>
ZfVFieldType *ZfFieldType_Time<T, Props>::vtype() {
  return ZmSingleton<ZfVFieldType_Time<T, Props>>::instance();
}

template <typename Base, bool = Base::ReadOnly>
struct ZfField_Time : public ZfField_<Base> {
  template <template <typename> class Override>
  using Adapt = ZfField_Time<Override<Base>>;
  using O = typename Base::O;
  using T = typename Base::T;
  using Props = typename Base::Props;
  using Type = ZfFieldType_Time<T, ZuTypeGrep<ZfFieldType_Props, Props>>;
  enum { Code = Type::Code };
  static ZfVFieldGet getFn() {
    return {.get_ = {.time = [](const void *o) -> ZuTime {
      return Base::get(*static_cast<const O *>(o));
    }}};
  }
  static ZfVFieldSet setFn() {
    return {.set_ = {.time = [](void *, ZuTime) { }}};
  }
  static constexpr auto deflt() {
    return ZfFieldDeflt<Props, [] { return ZuTime{}; }>();
  }
  static ZfVFieldGet constantFn() {
    using namespace ZfVFieldConstant;
    return {.get_ = {.time = [](const void *o) -> ZuTime {
      switch (int(reinterpret_cast<uintptr_t>(o))) {
	case Deflt:   return deflt();
	default:      return {};
      }
    }}};
  }
};
template <typename Base>
struct ZfField_Time<Base, false> :
    public ZfField_Time<Base, true> {
  using O = typename Base::O;
  static ZfVFieldSet setFn() {
    return {.set_ = {.time = [](void *o, ZuTime v) {
      Base::set(*static_cast<O *>(o), ZuMv(v));
    }}};
  }
};

// --- DateTime

template <typename T_, typename Props_>
struct ZfFieldType_DateTime : public ZfFieldType_<Props_> {
  enum { Code = ZfFieldTC::DateTime };
  using T = T_;
  using Props = Props_;
  template <typename Fmt = ZtFmt::Default> struct Print {
    ZuDateTime v;
    template <typename S>
    friend inline decltype(auto) operator <<(S &s, const Print &print) {
      return s << print.v.fmt(Fmt::DatePrint_());
    }
  };
  inline static ZfVFieldType *vtype();
};
template <typename T, typename Props>
struct ZfFieldType<ZfFieldTC::DateTime, T, Props> :
    public ZfFieldType_DateTime<T, Props> { };

template <typename T, typename Props>
struct ZfVFieldType_DateTime : public ZfVFieldType {
  ZfVFieldType_DateTime() : ZfVFieldType{
    .code = ZfFieldTC::DateTime,
    .props = ZfVFieldProp::Value<Props>{},
    .info = {.null = nullptr}
  } { }
};
template <typename T, typename Props>
ZfVFieldType *ZfFieldType_DateTime<T, Props>::vtype() {
  return ZmSingleton<ZfVFieldType_DateTime<T, Props>>::instance();
}

template <typename Base, bool = Base::ReadOnly>
struct ZfField_DateTime : public ZfField_<Base> {
  template <template <typename> class Override>
  using Adapt = ZfField_DateTime<Override<Base>>;
  using O = typename Base::O;
  using T = typename Base::T;
  using Props = typename Base::Props;
  using Type = ZfFieldType_DateTime<T, ZuTypeGrep<ZfFieldType_Props, Props>>;
  enum { Code = Type::Code };
  static ZfVFieldGet getFn() {
    return {.get_ = {.dateTime = [](const void *o) -> ZuDateTime {
      return Base::get(*static_cast<const O *>(o));
    }}};
  }
  static ZfVFieldSet setFn() {
    return {.set_ = {.dateTime = [](void *, ZuDateTime) { }}};
  }
  static constexpr auto deflt() {
    return ZfFieldDeflt<Props, [] { return ZuDateTime{}; }>();
  }
  static ZfVFieldGet constantFn() {
    using namespace ZfVFieldConstant;
    return {.get_ = {.dateTime = [](const void *o) -> ZuDateTime {
      switch (int(reinterpret_cast<uintptr_t>(o))) {
	case Deflt:   return deflt();
	default:      return {};
      }
    }}};
  }
};
template <typename Base>
struct ZfField_DateTime<Base, false> :
    public ZfField_DateTime<Base, true> {
  using O = typename Base::O;
  static ZfVFieldSet setFn() {
    return {.set_ = {.dateTime = [](void *o, ZuDateTime v) {
      Base::set(*static_cast<O *>(o), ZuMv(v));
    }}};
  }
};

// --- UDT

template <typename T, typename Fmt, typename = void>
struct ZfFieldType_UDT_HasFmt : public ZuFalse { };
template <typename T, typename Fmt>
struct ZfFieldType_UDT_HasFmt<T, Fmt,
  decltype(ZuDeclVal<const T &>().template fmt<Fmt>(), void())> :
    public ZuTrue { };

template <typename T_, typename Props_>
struct ZfFieldType_UDT : public ZfFieldType_<Props_> {
  enum { Code = ZfFieldTC::UDT };
  using T = T_;
  using Props = Props_;
  template <typename Fmt = ZtFmt::Default> struct Print {
    const T &v;

    template <typename S, typename U = T,
      typename = decltype(void(bool(ZfFieldType_UDT_HasFmt<U, Fmt>{})))>
    friend S &
    operator <<(S &s, const Print &print) {
      if constexpr (ZfFieldType_UDT_HasFmt<U, Fmt>{}) {
	s << print.v.template fmt<Fmt>();
      } else {
	s << print.v;
      }
      return s;
    }
  };
  inline static ZfVFieldType *vtype();
};
template <typename T, typename Props>
struct ZfFieldType<ZfFieldTC::UDT, T, Props> :
    public ZfFieldType_UDT<T, Props> { };

template <typename T, typename = void>
struct ZfVFieldType_UDT_Print {
  static auto printFn() {
    return [](const void *, ZuVStream &, const ZtVFmt &) { };
  }
};
template <typename T>
struct ZfVFieldType_UDT_Print<T,
  decltype((ZuDeclVal<ZuVStream &>() << ZuDeclVal<const T &>()), void())> {
  static auto printFn() {
    return [](const void *v, ZuVStream &s, const ZtVFmt &) {
      s << *reinterpret_cast<const T *>(v);
    };
  }
};
template <typename T, typename = void>
struct ZfVFieldType_UDT_Scan {
  static auto scanFn() {
    return [](
      void (*)(void *, const void *), void *,
      ZuCSpan, const ZtVFmt &) { };
  }
};
template <typename T>
struct ZfVFieldType_UDT_Scan<
  T, decltype((ZuDeclVal<T &>() = ZuCSpan()), void())
> {
  static auto scanFn() {
    return [](
      void (*set)(void *, const void *), void *o,
      ZuCSpan s, const ZtVFmt &
    ) {
      T v{s};
      set(o, reinterpret_cast<const void *>(&v));
    };
  }
};
template <typename T, typename Props>
struct ZfVFieldType_UDT : public ZfVFieldType {
  ZfVFieldType_UDT() : ZfVFieldType{
    .code = ZfFieldTC::UDT,
    .props = ZfVFieldProp::Value<Props>{},
    .info = {.udt = []() -> ZfVFieldUDT * {
      static ZfVFieldUDT info{
	.id = ZfVFieldTypeID(static_cast<T *>(nullptr)),
	.info = &typeid(T),
	.print = ZfVFieldType_UDT_Print<T>::printFn(),
	.scan = ZfVFieldType_UDT_Scan<T>::scanFn()
      };
      return &info;
    }}
  } { }
};
template <typename T, typename Props>
ZfVFieldType *ZfFieldType_UDT<T, Props>::vtype() {
  return ZmSingleton<ZfVFieldType_UDT<T, Props>>::instance();
}

template <typename T, typename = void>
struct ZfField_UDT_Null {
  static const void *value() { return nullptr; }
};
template <typename T>
struct ZfField_UDT_Null<T, decltype(T{}, void())> {
  static const void *value() {
    static T null_;
    return static_cast<const void *>(&null_);
  }
};
template <typename Base, typename = void>
struct ZfField_UDT_Get {
  static ZfVFieldGet getFn() {
    using O = typename Base::O;
    using T = typename Base::T;
    // field get() returns a temporary
    return {.get_ = {.udt = [](const void *o) -> const void * {
      auto &v = ZmTLS<T, getFn>();
      v = Base::get(*static_cast<const O *>(o));
      return static_cast<const void *>(&v);
    }}};
  }
};
template <typename Base>
struct ZfField_UDT_Get<Base,
    decltype(&Base::get(ZuDeclVal<const typename Base::O &>()), void())> {
  static ZfVFieldGet getFn() {
    using O = typename Base::O;
    // field get() returns a crvalue
    return {.get_ = {.udt = [](const void *o) -> const void * {
      return static_cast<const void *>(&Base::get(*static_cast<const O *>(o)));
    }}};
  }
};
template <typename Base, bool = Base::ReadOnly>
struct ZfField_UDT :
    public ZfField_<Base>,
    public ZfField_UDT_Get<Base> {
  template <template <typename> class Override>
  using Adapt = ZfField_UDT<Override<Base>>;
  using O = typename Base::O;
  using T = typename Base::T;
  using Props = typename Base::Props;
  using Type = ZfFieldType_UDT<T, ZuTypeGrep<ZfFieldType_Props, Props>>;
  enum { Code = Type::Code };
  static ZfVFieldSet setFn() {
    return {.set_ = {.udt = [](void *, const void *) { }}};
  }
  static constexpr decltype(auto) deflt() {
    return ZfFieldDeflt<Props, [] {
      if constexpr (__is_constructible(T)) return T{};
    }>();
  }
  static ZfVFieldGet constantFn() {
    using namespace ZfVFieldConstant;
    return {.get_ = {.udt = [](const void *o) -> const void * {
      if constexpr (ZuIsSame<decltype(deflt()), T>{} ||
	  ZuIsConstructible<decltype(deflt()), T>{}) {
	switch (int(reinterpret_cast<uintptr_t>(o))) {
	  case Deflt: {
	    static T deflt_{deflt()};
	    return static_cast<const void *>(&deflt_);
	  }
	  default: return ZfField_UDT_Null<T>::value();
	}
      } else {
	return nullptr;
      }
    }}};
  }
};
template <typename Base>
struct ZfField_UDT<Base, false> :
    public ZfField_UDT<Base, true> {
  using O = typename Base::O;
  using T = typename Base::T;
  static ZfVFieldSet setFn() {
    return {.set_ = {.udt = [](void *o, const void *p) {
      Base::set(*static_cast<O *>(o), *static_cast<const T *>(p));
    }}};
  }
};

// --- CStringVec

template <typename T_, typename Props_>
struct ZfFieldType_CStringVec : public ZfFieldType_<Props_> {
  enum { Code = ZfFieldTC::CStringVec };
  using T = T_;
  using Props = Props_;
  template <typename Fmt = ZtFmt::Default> struct Print {
    const T &vec;
    template <typename S>
    friend inline decltype(auto) operator <<(S &s, const Print &print) {
      s << Fmt::VecPrefix();
      bool first = true;
      for (unsigned i = 0, n = ZuTraits<T>::length(print.vec); i < n; i++) {
	if (!first) s << Fmt::VecDelim(); else first = false;
	s << ZfStruct_::Print::CString{print.vec[i]};
      }
      return s << Fmt::VecSuffix();
    }
  };
  inline static ZfVFieldType *vtype();
};
template <typename T, typename Props>
struct ZfFieldType<ZfFieldTC::CStringVec, T, Props> :
    public ZfFieldType_CStringVec<T, Props> { };

template <typename T, typename Props>
struct ZfVFieldType_CStringVec : public ZfVFieldType {
  ZfVFieldType_CStringVec() : ZfVFieldType{
    .code = ZfFieldTC::CStringVec,
    .props = ZfVFieldProp::Value<Props>{},
    .info = {.null = nullptr}
  } { }
};
template <typename T, typename Props>
ZfVFieldType *ZfFieldType_CStringVec<T, Props>::vtype() {
  return ZmSingleton<ZfVFieldType_CStringVec<T, Props>>::instance();
}

template <typename Base, bool = Base::ReadOnly>
struct ZfField_CStringVec : public ZfField_<Base> {
  template <template <typename> class Override>
  using Adapt = ZfField_CStringVec<Override<Base>>;
  using O = typename Base::O;
  using T = typename Base::T;
  using Elem = typename ZuTraits<T>::Elem;
  using Props = typename Base::Props;
  using Type = ZfFieldType_CStringVec<T, ZuTypeGrep<ZfFieldType_Props, Props>>;
  using CStringVec = ZfStruct_::CStringVec;
  enum { Code = Type::Code };
  static ZfVFieldGet getFn() {
    return {.get_ = {.cstringVec = [](const void *o) -> CStringVec {
      return Base::get(*static_cast<const O *>(o));
    }}};
  }
  static ZfVFieldSet setFn() {
    return {.set_ = {.cstringVec = [](void *, CStringVec) { }}};
  }
  static constexpr CStringVec deflt() {
    return ZfFieldDeflt<Props, [] { return CStringVec{}; }>();
  }
  static ZfVFieldGet constantFn() {
    using namespace ZfVFieldConstant;
    return {.get_ = {.cstringVec = [](const void *o) -> CStringVec {
      switch (int(reinterpret_cast<uintptr_t>(o))) {
	case Deflt:   return deflt();
	default:      return {};
      }
    }}};
  }
};
template <typename Base>
struct ZfField_CStringVec<Base, false> :
    public ZfField_CStringVec<Base, true> {
  using O = typename Base::O;
  static ZfVFieldSet setFn() {
    using namespace ZfStruct_;
    return {.set_ = {.cstringVec = [](void *o, CStringVec v) {
      Base::set(*static_cast<O *>(o), ZuMv(v));
    }}};
  }
};

// --- StringVec

template <typename T_, typename Props_>
struct ZfFieldType_StringVec : public ZfFieldType_<Props_> {
  enum { Code = ZfFieldTC::StringVec };
  using T = T_;
  using Props = Props_;
  template <typename Fmt = ZtFmt::Default> struct Print {
    const T &vec;
    template <typename S>
    friend inline decltype(auto) operator <<(S &s, const Print &print) {
      s << Fmt::VecPrefix();
      bool first = true;
      for (unsigned i = 0, n = ZuTraits<T>::length(print.vec); i < n; i++) {
	if (!first) s << Fmt::VecDelim(); else first = false;
	s << ZfStruct_::Print::String{print.vec[i]};
      }
      return s << Fmt::VecSuffix();
    }
  };
  inline static ZfVFieldType *vtype();
};
template <typename T, typename Props>
struct ZfFieldType<ZfFieldTC::StringVec, T, Props> :
    public ZfFieldType_StringVec<T, Props> { };

template <typename T, typename Props>
struct ZfVFieldType_StringVec : public ZfVFieldType {
  ZfVFieldType_StringVec() : ZfVFieldType{
    .code = ZfFieldTC::StringVec,
    .props = ZfVFieldProp::Value<Props>{},
    .info = {.null = nullptr}
  } { }
};
template <typename T, typename Props>
ZfVFieldType *ZfFieldType_StringVec<T, Props>::vtype() {
  return ZmSingleton<ZfVFieldType_StringVec<T, Props>>::instance();
}

template <typename Base, bool = Base::ReadOnly>
struct ZfField_StringVec : public ZfField_<Base> {
  template <template <typename> class Override>
  using Adapt = ZfField_StringVec<Override<Base>>;
  using O = typename Base::O;
  using T = typename Base::T;
  using Props = typename Base::Props;
  using Type = ZfFieldType_StringVec<T, ZuTypeGrep<ZfFieldType_Props, Props>>;
  using StringVec = ZfStruct_::StringVec;
  enum { Code = Type::Code };
  static ZfVFieldGet getFn() {
    return {.get_ = {.stringVec = [](const void *o) -> StringVec {
      return Base::get(*static_cast<const O *>(o));
    }}};
  }
  static ZfVFieldSet setFn() {
    using namespace ZfStruct_;
    return {.set_ = {.stringVec = [](void *, StringVec) { }}};
  }
  static constexpr StringVec deflt() {
    return ZfFieldDeflt<Props, [] { return StringVec{}; }>();
  }
  static ZfVFieldGet constantFn() {
    using namespace ZfVFieldConstant;
    return {.get_ = {.stringVec = [](const void *o) -> StringVec {
      switch (int(reinterpret_cast<uintptr_t>(o))) {
	case Deflt:   return deflt();
	default:      return {};
      }
    }}};
  }
};
template <typename Base>
struct ZfField_StringVec<Base, false> :
    public ZfField_StringVec<Base, true> {
  using O = typename Base::O;
  static ZfVFieldSet setFn() {
    using namespace ZfStruct_;
    return {.set_ = {.stringVec = [](void *o, StringVec v) {
      Base::set(*static_cast<O *>(o), ZuMv(v));
    }}};
  }
};

// --- BytesVec

template <typename T_, typename Props_>
struct ZfFieldType_BytesVec : public ZfFieldType_<Props_> {
  enum { Code = ZfFieldTC::BytesVec };
  using T = T_;
  using Props = Props_;
  template <typename Fmt = ZtFmt::Default> struct Print {
    const T &vec;
    template <typename S>
    friend inline decltype(auto) operator <<(S &s, const Print &print) {
      s << Fmt::VecPrefix();
      bool first = true;
      for (unsigned i = 0, n = ZuTraits<T>::length(print.vec); i < n; i++) {
	if (!first) s << Fmt::VecDelim(); else first = false;
	s << ZfStruct_::Print::Bytes{print.vec[i]};
      }
      return s << Fmt::VecSuffix();
    }
  };
  inline static ZfVFieldType *vtype();
};
template <typename T, typename Props>
struct ZfFieldType<ZfFieldTC::BytesVec, T, Props> :
    public ZfFieldType_BytesVec<T, Props> { };

template <typename T, typename Props>
struct ZfVFieldType_BytesVec : public ZfVFieldType {
  ZfVFieldType_BytesVec() : ZfVFieldType{
    .code = ZfFieldTC::BytesVec,
    .props = ZfVFieldProp::Value<Props>{},
    .info = {.null = nullptr}
  } { }
};
template <typename T, typename Props>
ZfVFieldType *ZfFieldType_BytesVec<T, Props>::vtype() {
  return ZmSingleton<ZfVFieldType_BytesVec<T, Props>>::instance();
}

template <typename Base, bool = Base::ReadOnly>
struct ZfField_BytesVec : public ZfField_<Base> {
  template <template <typename> class Override>
  using Adapt = ZfField_BytesVec<Override<Base>>;
  using O = typename Base::O;
  using T = typename Base::T;
  using Props = typename Base::Props;
  using Type = ZfFieldType_BytesVec<T, ZuTypeGrep<ZfFieldType_Props, Props>>;
  using BytesVec = ZfStruct_::BytesVec;
  enum { Code = Type::Code };
  static ZfVFieldGet getFn() {
    return {.get_ = {.bytesVec = [](const void *o) -> BytesVec {
      return Base::get(*static_cast<const O *>(o));
    }}};
  }
  static ZfVFieldSet setFn() {
    using namespace ZfStruct_;
    return {.set_ = {.bytesVec = [](void *, BytesVec) { }}};
  }
  static constexpr BytesVec deflt() {
    return ZfFieldDeflt<Props, [] { return BytesVec{}; }>();
  }
  static ZfVFieldGet constantFn() {
    using namespace ZfVFieldConstant;
    return {.get_ = {.bytesVec = [](const void *o) -> BytesVec {
      switch (int(reinterpret_cast<uintptr_t>(o))) {
	case Deflt:   return deflt();
	default:      return {};
      }
    }}};
  }
};
template <typename Base>
struct ZfField_BytesVec<Base, false> :
    public ZfField_BytesVec<Base, true> {
  using O = typename Base::O;
  static ZfVFieldSet setFn() {
    using namespace ZfStruct_;
    return {.set_ = {.bytesVec = [](void *o, BytesVec v) {
      Base::set(*static_cast<O *>(o), ZuMv(v));
    }}};
  }
};

// --- {Int,UInt}{8,16,32,64,128}Vec

#define ZfField_IntVec(Code_, Type_) \
template <typename T_, typename Props_> \
struct ZfFieldType_##Code_##Vec : public ZfFieldType_<Props_> { \
  enum { Code = ZfFieldTC::Code_##Vec }; \
  using T = T_; \
  using Props = Props_; \
  template <typename Fmt = ZtFmt::Default> struct Print { \
    const T &vec; \
    template <typename S> \
    friend inline decltype(auto) operator <<(S &s, const Print &print) { \
      using Elem = ZuDecay<decltype(ZuDeclVal<const T &>()[0])>; \
      using B = ZuBox<Elem>; \
      s << Fmt::VecPrefix(); \
      bool first = true; \
      for (unsigned i = 0, n = ZuTraits<T>::length(print.vec); i < n; i++) { \
	if (!first) s << Fmt::VecDelim(); else first = false; \
	s << ZfFieldPrintInt<Props, Fmt, B>(print.vec[i]); \
      } \
      return s << Fmt::VecSuffix(); \
    } \
  }; \
  inline static ZfVFieldType *vtype(); \
}; \
template <typename T, typename Props> \
struct ZfFieldType<ZfFieldTC::Code_##Vec, T, Props> : \
    public ZfFieldType_##Code_##Vec<T, Props> { }; \
 \
template < \
  typename T, typename Props, \
  bool = ZuFieldProp::HasEnum<Props>{}, \
  bool = ZuFieldProp::HasFlags<Props>{}> \
struct ZfVFieldType_##Code_##Vec; \
template <typename T, typename Props> \
struct ZfVFieldType_##Code_##Vec<T, Props, false, false> : \
  public ZfVFieldType \
{ \
  ZfVFieldType_##Code_##Vec() : ZfVFieldType{ \
    .code = ZfFieldTC::Code_##Vec, \
    .props = ZfVFieldProp::Value<Props>{}, \
    .info = {.null = nullptr} \
  } { } \
}; \
template <typename T, typename Props> \
struct ZfVFieldType_##Code_##Vec<T, Props, true, false> : \
  public ZfVFieldType \
{ \
  ZfVFieldType_##Code_##Vec() : ZfVFieldType{ \
    .code = ZfFieldTC::Code_##Vec, \
    .props = ZfVFieldProp::Value<Props>{}, \
    .info = {.enum_ = []() -> ZfVFieldEnum * { \
      return ZfVFieldEnum_<ZuFieldProp::GetEnum<Props>>::instance(); \
    }} \
  } { } \
}; \
template <typename T, typename Props> \
struct ZfVFieldType_##Code_##Vec<T, Props, false, true> : \
  public ZfVFieldType \
{ \
  ZfVFieldType_##Code_##Vec() : ZfVFieldType{ \
    .code = ZfFieldTC::Code_##Vec, \
    .props = ZfVFieldProp::Value<Props>{}, \
    .info = {.flags = []() -> ZfVFieldFlags * { \
      return ZfVFieldFlags_<ZuFieldProp::GetFlags<Props>>::instance(); \
    }} \
  } { } \
}; \
template <typename T, typename Props> \
ZfVFieldType *ZfFieldType_##Code_##Vec<T, Props>::vtype() { \
  return ZmSingleton<ZfVFieldType_##Code_##Vec<T, Props>>::instance(); \
} \
 \
template <typename Base, bool = Base::ReadOnly> \
struct ZfField_##Code_##Vec : public ZfField_<Base> { \
  template <template <typename> class Override> \
  using Adapt = ZfField_##Code_##Vec<Override<Base>>; \
  using O = typename Base::O; \
  using T = typename Base::T; \
  using Props = typename Base::Props; \
  using Type = \
    ZfFieldType_##Code_##Vec<T, ZuTypeGrep<ZfFieldType_Props, Props>>; \
  using Code_##Vec = ZfStruct_::Code_##Vec; \
  enum { Code = Type::Code }; \
  static ZfVFieldGet getFn() { \
    return {.get_ = {.Type_##Vec = [](const void *o) -> Code_##Vec { \
      return Base::get(*static_cast<const O *>(o)); \
    }}}; \
  } \
  static ZfVFieldSet setFn() { \
    using namespace ZfStruct_; \
    return {.set_ = {.Type_##Vec = [](void *, Code_##Vec) { }}}; \
  } \
  static constexpr Code_##Vec deflt() { \
    return ZfFieldDeflt<Props, [] { return Code_##Vec{}; }>(); \
  } \
  static ZfVFieldGet constantFn() { \
    using namespace ZfVFieldConstant; \
    return {.get_ = {.Type_##Vec = [](const void *o) -> Code_##Vec { \
      switch (int(reinterpret_cast<uintptr_t>(o))) { \
	case Deflt:   return deflt(); \
	default:      return {}; \
      } \
    }}}; \
  } \
}; \
template <typename Base> \
struct ZfField_##Code_##Vec<Base, false> : \
    public ZfField_##Code_##Vec<Base, true> { \
  using O = typename Base::O; \
  static ZfVFieldSet setFn() { \
    using namespace ZfStruct_; \
    return {.set_ = {.Type_##Vec = [](void *o, Code_##Vec v) { \
      Base::set(*static_cast<O *>(o), ZuMv(v)); \
    }}}; \
  } \
};

ZfField_IntVec(Int8, int8);
ZfField_IntVec(UInt8, uint8);
ZfField_IntVec(Int16, int16);
ZfField_IntVec(UInt16, uint16);
ZfField_IntVec(Int32, int32);
ZfField_IntVec(UInt32, uint32);
ZfField_IntVec(Int64, int64);
ZfField_IntVec(UInt64, uint64);
ZfField_IntVec(Int128, int128);
ZfField_IntVec(UInt128, uint128);

// --- FloatVec

template <typename T_, typename Props_>
struct ZfFieldType_FloatVec : public ZfFieldType_<Props_> {
  enum { Code = ZfFieldTC::FloatVec };
  using T = T_;
  using Props = Props_;
  template <typename Fmt = ZtFmt::Default> struct Print {
    const T &vec;
    template <typename S>
    friend inline decltype(auto) operator <<(S &s, const Print &print) {
      s << Fmt::VecPrefix();
      bool first = true;
      for (unsigned i = 0, n = ZuTraits<T>::length(print.vec); i < n; i++) {
	if (!first) s << Fmt::VecDelim(); else first = false;
	using Elem = ZuDecay<decltype(print.vec[i])>;
	ZuBox<Elem> v = print.vec[i];
	if constexpr (ZuFieldProp::HasNDP<Props>{})
	  s << v.template fp<ZuFieldProp::GetNDP<Props>{}, '\0', Fmt>();
	else
	  s << v.template fmt<Fmt>();
      }
      return s << Fmt::VecSuffix();
    }
  };
  inline static ZfVFieldType *vtype();
};
template <typename T, typename Props>
struct ZfFieldType<ZfFieldTC::FloatVec, T, Props> :
    public ZfFieldType_FloatVec<T, Props> { };

template <typename T, typename Props>
struct ZfVFieldType_FloatVec : public ZfVFieldType {
  ZfVFieldType_FloatVec() : ZfVFieldType{
    .code = ZfFieldTC::FloatVec,
    .props = ZfVFieldProp::Value<Props>{},
    .info = {.null = nullptr}
  } { }
};
template <typename T, typename Props>
ZfVFieldType *ZfFieldType_FloatVec<T, Props>::vtype() {
  return ZmSingleton<ZfVFieldType_FloatVec<T, Props>>::instance();
}

template <typename Base, bool = Base::ReadOnly>
struct ZfField_FloatVec : public ZfField_<Base> {
  template <template <typename> class Override>
  using Adapt = ZfField_FloatVec<Override<Base>>;
  using O = typename Base::O;
  using T = typename Base::T;
  using Props = typename Base::Props;
  using Type = ZfFieldType_FloatVec<T, ZuTypeGrep<ZfFieldType_Props, Props>>;
  using FloatVec = ZfStruct_::FloatVec;
  enum { Code = Type::Code };
  static ZfVFieldGet getFn() {
    return {.get_ = {.floatVec = [](const void *o) -> FloatVec {
      return Base::get(*static_cast<const O *>(o));
    }}};
  }
  static ZfVFieldSet setFn() {
    using namespace ZfStruct_;
    return {.set_ = {.floatVec = [](void *, FloatVec) { }}};
  }
  static constexpr FloatVec deflt() {
    return ZfFieldDeflt<Props, [] { return FloatVec{}; }>();
  }
  static ZfVFieldGet constantFn() {
    using namespace ZfVFieldConstant;
    return {.get_ = {.floatVec = [](const void *o) -> FloatVec {
      switch (int(reinterpret_cast<uintptr_t>(o))) {
	case Deflt:   return deflt();
	default:      return {};
      }
    }}};
  }
};
template <typename Base>
struct ZfField_FloatVec<Base, false> :
    public ZfField_FloatVec<Base, true> {
  using O = typename Base::O;
  static ZfVFieldSet setFn() {
    using namespace ZfStruct_;
    return {.set_ = {.floatVec = [](void *o, FloatVec v) {
      Base::set(*static_cast<O *>(o), ZuMv(v));
    }}};
  }
};

// --- FixedVec

template <typename T_, typename Props_>
struct ZfFieldType_FixedVec : public ZfFieldType_<Props_> {
  enum { Code = ZfFieldTC::FixedVec };
  using T = T_;
  using Props = Props_;
  template <typename Fmt = ZtFmt::Default> struct Print {
    const T &vec;
    template <typename S>
    friend inline decltype(auto) operator <<(S &s, const Print &print) {
      s << Fmt::VecPrefix();
      bool first = true;
      for (unsigned i = 0, n = ZuTraits<T>::length(print.vec); i < n; i++) {
	if (!first) s << Fmt::VecDelim(); else first = false;
	s << print.vec[i].template fmt<Fmt>();
      }
      return s << Fmt::VecSuffix();
    }
  };
  inline static ZfVFieldType *vtype();
};
template <typename T, typename Props>
struct ZfFieldType<ZfFieldTC::FixedVec, T, Props> :
    public ZfFieldType_FixedVec<T, Props> { };

template <typename T, typename Props>
struct ZfVFieldType_FixedVec : public ZfVFieldType {
  ZfVFieldType_FixedVec() : ZfVFieldType{
    .code = ZfFieldTC::FixedVec,
    .props = ZfVFieldProp::Value<Props>{},
    .info = {.null = nullptr}
  } { }
};
template <typename T, typename Props>
ZfVFieldType *ZfFieldType_FixedVec<T, Props>::vtype() {
  return ZmSingleton<ZfVFieldType_FixedVec<T, Props>>::instance();
}

template <typename Base, bool = Base::ReadOnly>
struct ZfField_FixedVec : public ZfField_<Base> {
  template <template <typename> class Override>
  using Adapt = ZfField_FixedVec<Override<Base>>;
  using O = typename Base::O;
  using T = typename Base::T;
  using Props = typename Base::Props;
  using Type = ZfFieldType_FixedVec<T, ZuTypeGrep<ZfFieldType_Props, Props>>;
  using FixedVec = ZfStruct_::FixedVec;
  enum { Code = Type::Code };
  static ZfVFieldGet getFn() {
    return {.get_ = {.fixedVec = [](const void *o) -> FixedVec {
      return Base::get(*static_cast<const O *>(o));
    }}};
  }
  static ZfVFieldSet setFn() {
    using namespace ZfStruct_;
    return {.set_ = {.fixedVec = [](void *, FixedVec) { }}};
  }
  static constexpr FixedVec deflt() {
    return ZfFieldDeflt<Props, [] { return FixedVec{}; }>();
  }
  static ZfVFieldGet constantFn() {
    using namespace ZfVFieldConstant;
    return {.get_ = {.fixedVec = [](const void *o) -> FixedVec {
      switch (int(reinterpret_cast<uintptr_t>(o))) {
	case Deflt:   return deflt();
	default:      return {};
      }
    }}};
  }
};
template <typename Base>
struct ZfField_FixedVec<Base, false> :
    public ZfField_FixedVec<Base, true> {
  using O = typename Base::O;
  static ZfVFieldSet setFn() {
    using namespace ZfStruct_;
    return {.set_ = {.fixedVec = [](void *o, FixedVec v) {
      Base::set(*static_cast<O *>(o), ZuMv(v));
    }}};
  }
};

// --- DecimalVec

template <typename T_, typename Props_>
struct ZfFieldType_DecimalVec : public ZfFieldType_<Props_> {
  enum { Code = ZfFieldTC::DecimalVec };
  using T = T_;
  using Props = Props_;
  template <typename Fmt = ZtFmt::Default> struct Print {
    const T &vec;
    template <typename S>
    friend inline decltype(auto) operator <<(S &s, const Print &print) {
      s << Fmt::VecPrefix();
      bool first = true;
      for (unsigned i = 0, n = ZuTraits<T>::length(print.vec); i < n; i++) {
	if (!first) s << Fmt::VecDelim(); else first = false;
	s << print.vec[i].template fmt<Fmt>();
      }
      return s << Fmt::VecSuffix();
    }
  };
  inline static ZfVFieldType *vtype();
};
template <typename T, typename Props>
struct ZfFieldType<ZfFieldTC::DecimalVec, T, Props> :
    public ZfFieldType_DecimalVec<T, Props> { };

template <typename T, typename Props>
struct ZfVFieldType_DecimalVec : public ZfVFieldType {
  ZfVFieldType_DecimalVec() : ZfVFieldType{
    .code = ZfFieldTC::DecimalVec,
    .props = ZfVFieldProp::Value<Props>{},
    .info = {.null = nullptr}
  } { }
};
template <typename T, typename Props>
ZfVFieldType *ZfFieldType_DecimalVec<T, Props>::vtype() {
  return ZmSingleton<ZfVFieldType_DecimalVec<T, Props>>::instance();
}

template <typename Base, bool = Base::ReadOnly>
struct ZfField_DecimalVec : public ZfField_<Base> {
  template <template <typename> class Override>
  using Adapt = ZfField_DecimalVec<Override<Base>>;
  using O = typename Base::O;
  using T = typename Base::T;
  using Props = typename Base::Props;
  using Type = ZfFieldType_DecimalVec<T, ZuTypeGrep<ZfFieldType_Props, Props>>;
  using DecimalVec = ZfStruct_::DecimalVec;
  enum { Code = Type::Code };
  static ZfVFieldGet getFn() {
    return {.get_ = {.decimalVec = [](const void *o) -> DecimalVec {
      return Base::get(*static_cast<const O *>(o));
    }}};
  }
  static ZfVFieldSet setFn() {
    using namespace ZfStruct_;
    return {.set_ = {.decimalVec = [](void *, DecimalVec) { }}};
  }
  static constexpr DecimalVec deflt() {
    return ZfFieldDeflt<Props, [] { return DecimalVec{}; }>();
  }
  static ZfVFieldGet constantFn() {
    using namespace ZfVFieldConstant;
    return {.get_ = {.decimalVec = [](const void *o) -> DecimalVec {
      switch (int(reinterpret_cast<uintptr_t>(o))) {
	case Deflt:   return deflt();
	default:      return {};
      }
    }}};
  }
};
template <typename Base>
struct ZfField_DecimalVec<Base, false> :
    public ZfField_DecimalVec<Base, true> {
  using O = typename Base::O;
  static ZfVFieldSet setFn() {
    using namespace ZfStruct_;
    return {.set_ = {.decimalVec = [](void *o, DecimalVec v) {
      Base::set(*static_cast<O *>(o), ZuMv(v));
    }}};
  }
};

// --- TimeVec

template <typename T_, typename Props_>
struct ZfFieldType_TimeVec : public ZfFieldType_<Props_> {
  enum { Code = ZfFieldTC::TimeVec };
  using T = T_;
  using Props = Props_;
  template <typename Fmt = ZtFmt::Default> struct Print {
    const T &vec;
    template <typename S>
    friend inline decltype(auto) operator <<(S &s, const Print &print) {
      s << Fmt::VecPrefix();
      bool first = true;
      for (unsigned i = 0, n = ZuTraits<T>::length(print.vec); i < n; i++) {
	if (!first) s << Fmt::VecDelim(); else first = false;
	ZuDateTime v{print.vec[i]};
	s << v.fmt(Fmt::DatePrint_());
      }
      return s << Fmt::VecSuffix();
    }
  };
  inline static ZfVFieldType *vtype();
};
template <typename T, typename Props>
struct ZfFieldType<ZfFieldTC::TimeVec, T, Props> :
    public ZfFieldType_TimeVec<T, Props> { };

template <typename T, typename Props>
struct ZfVFieldType_TimeVec : public ZfVFieldType {
  ZfVFieldType_TimeVec() : ZfVFieldType{
    .code = ZfFieldTC::TimeVec,
    .props = ZfVFieldProp::Value<Props>{},
    .info = {.null = nullptr}
  } { }
};
template <typename T, typename Props>
ZfVFieldType *ZfFieldType_TimeVec<T, Props>::vtype() {
  return ZmSingleton<ZfVFieldType_TimeVec<T, Props>>::instance();
}

template <typename Base, bool = Base::ReadOnly>
struct ZfField_TimeVec : public ZfField_<Base> {
  template <template <typename> class Override>
  using Adapt = ZfField_TimeVec<Override<Base>>;
  using O = typename Base::O;
  using T = typename Base::T;
  using Props = typename Base::Props;
  using Type = ZfFieldType_TimeVec<T, ZuTypeGrep<ZfFieldType_Props, Props>>;
  using TimeVec = ZfStruct_::TimeVec;
  enum { Code = Type::Code };
  static ZfVFieldGet getFn() {
    return {.get_ = {.timeVec = [](const void *o) -> TimeVec {
      return Base::get(*static_cast<const O *>(o));
    }}};
  }
  static ZfVFieldSet setFn() {
    using namespace ZfStruct_;
    return {.set_ = {.timeVec = [](void *, TimeVec) { }}};
  }
  static constexpr TimeVec deflt() {
    return ZfFieldDeflt<Props, [] { return TimeVec{}; }>();
  }
  static ZfVFieldGet constantFn() {
    using namespace ZfVFieldConstant;
    return {.get_ = {.timeVec = [](const void *o) -> TimeVec {
      switch (int(reinterpret_cast<uintptr_t>(o))) {
	case Deflt:   return deflt();
	default:      return {};
      }
    }}};
  }
};
template <typename Base>
struct ZfField_TimeVec<Base, false> :
    public ZfField_TimeVec<Base, true> {
  using O = typename Base::O;
  static ZfVFieldSet setFn() {
    using namespace ZfStruct_;
    return {.set_ = {.timeVec = [](void *o, TimeVec v) {
      Base::set(*static_cast<O *>(o), ZuMv(v));
    }}};
  }
};

// --- DateTimeVec

template <typename T_, typename Props_>
struct ZfFieldType_DateTimeVec : public ZfFieldType_<Props_> {
  enum { Code = ZfFieldTC::DateTimeVec };
  using T = T_;
  using Props = Props_;
  template <typename Fmt = ZtFmt::Default> struct Print {
    const T &vec;
    template <typename S>
    friend inline decltype(auto) operator <<(S &s, const Print &print) {
      s << Fmt::VecPrefix();
      bool first = true;
      for (unsigned i = 0, n = ZuTraits<T>::length(print.vec); i < n; i++) {
	if (!first) s << Fmt::VecDelim(); else first = false;
	ZuDateTime v{print.vec[i]};
	s << v.fmt(Fmt::DatePrint_());
      }
      return s << Fmt::VecSuffix();
    }
  };
  inline static ZfVFieldType *vtype();
};
template <typename T, typename Props>
struct ZfFieldType<ZfFieldTC::DateTimeVec, T, Props> :
    public ZfFieldType_DateTimeVec<T, Props> { };

template <typename T, typename Props>
struct ZfVFieldType_DateTimeVec : public ZfVFieldType {
  ZfVFieldType_DateTimeVec() : ZfVFieldType{
    .code = ZfFieldTC::DateTimeVec,
    .props = ZfVFieldProp::Value<Props>{},
    .info = {.null = nullptr}
  } { }
};
template <typename T, typename Props>
ZfVFieldType *ZfFieldType_DateTimeVec<T, Props>::vtype() {
  return ZmSingleton<ZfVFieldType_DateTimeVec<T, Props>>::instance();
}

template <
  typename Base,
  bool = Base::ReadOnly>
struct ZfField_DateTimeVec : public ZfField_<Base> {
  template <template <typename> class Override>
  using Adapt = ZfField_DateTimeVec<Override<Base>>;
  using O = typename Base::O;
  using T = typename Base::T;
  using Props = typename Base::Props;
  using Type = ZfFieldType_DateTimeVec<T, ZuTypeGrep<ZfFieldType_Props, Props>>;
  using DateTimeVec = ZfStruct_::DateTimeVec;
  enum { Code = Type::Code };
  static ZfVFieldGet getFn() {
    return {.get_ = {.dateTimeVec = [](const void *o) -> DateTimeVec {
      return Base::get(*static_cast<const O *>(o));
    }}};
  }
  static ZfVFieldSet setFn() {
    using namespace ZfStruct_;
    return {.set_ = {.dateTimeVec = [](void *, DateTimeVec) { }}};
  }
  static constexpr DateTimeVec deflt() {
    return ZfFieldDeflt<Props, [] { return DateTimeVec{}; }>();
  }
  static ZfVFieldGet constantFn() {
    using namespace ZfVFieldConstant;
    return {.get_ = {.dateTimeVec = [](const void *o) -> DateTimeVec {
      switch (int(reinterpret_cast<uintptr_t>(o))) {
	case Deflt:   return deflt();
	default:      return {};
      }
    }}};
  }
};
template <typename Base>
struct ZfField_DateTimeVec<Base, false> :
    public ZfField_DateTimeVec<Base, true> {
  using O = typename Base::O;
  static ZfVFieldSet setFn() {
    using namespace ZfStruct_;
    return {.set_ = {.dateTimeVec = [](void *o, DateTimeVec v) {
      Base::set(*static_cast<O *>(o), ZuMv(v));
    }}};
  }
};

#define ZfField_BaseID__(ID, ...) ID
#define ZfField_BaseID_(Axor, ...) ZuPP_Defer(ZfField_BaseID__)Axor
#define ZfField_BaseID(Base) ZuPP_Defer(ZfField_BaseID_)Base

#define ZfField(O, ID) ZfField_##O##_##ID

// get field index within fields
#define ZfFieldIndex(O, ID) (ZuTypeIndex<ZfField(O, ID), ZuFields<O>>{})

// main structure declaration macros ZfStruct()
#define ZfField_Decl__(O, ID, Base, Type) \
  ZuField_Decl(O, Base) \
  using ZfField(O, ID) = ZfField_##Type<ZuField(O, ID)>;
#define ZfField_Decl_(O, Base, Type) \
  ZuPP_Defer(ZfField_Decl__)(O, \
      ZuPP_Eval_(ZfField_BaseID(Base)), Base, Type)
#define ZfField_Decl(O, Args) ZuPP_Defer(ZfField_Decl_)(O, ZuPP_Strip(Args))

#define ZfField_Type_(O, Base, ...) \
  ZuPP_Defer(ZfField)(O, ZuPP_Eval_(ZfField_BaseID(Base)))
#define ZfField_Type(O, Args) ZuPP_Defer(ZfField_Type_)(O, ZuPP_Strip(Args))

#define ZfStruct_Render__(API, O, Facet) \
  struct API ZfFields_##O##_##Facet { \
    using Fields = ZfFields_##O; \
    using Keys = ZuFieldIDs<Fields>; \
    static int match(ZuCSpan); \
  }; \
  ZfFields_##O##_##Facet ZuFields_(O *, ZuFacet::Facet *);
#define ZfStruct_Render_(API_O, Facet) \
  ZuPP_Defer(ZfStruct_Render__)(ZuPP_Strip(API_O), Facet)
#define ZfStruct_Render(API_O, ...) \
  ZuPP_Eval_(ZuPP_MapArg(ZfStruct_Render_, API_O, __VA_ARGS__))

#define ZfStruct_(API, O, Facets, ...) \
  __VA_OPT__(ZuPP_MapArg(ZfField_Decl, O, __VA_ARGS__)) \
  using ZfFields_##O = ZuTypeList< \
    __VA_OPT__(ZuPP_MapArgComma(ZfField_Type, O, __VA_ARGS__))>; \
  O ZuStructured_(O *); \
  ZfStruct_Render((API, O), Core ZuPP_StripAppend(Facets))

#define ZfStruct(API, O_Facets, ...) \
  ZuPP_Eval(ZuPP_Defer(ZfStruct_)( \
    API, \
    ZuPP_Eval_(ZuStruct_Object(O_Facets)), \
    ZuPP_Eval_(ZuStruct_Facets(O_Facets)) \
    __VA_OPT__(, __VA_ARGS__)))

// render fields to a facet (ZfStruct layer)
// - optionally extends selected fields with additional properties
// - ZfStructRender(API, Object, Facet[, (FieldID[, Property...])...])
#define ZfField_RenderDecl__(O, ID, ...) \
  using Props_ = ZfField(O, ID)::Props; \
  using Props = typename Props_::template Push<ZuField_Props_(__VA_ARGS__)>;
#define ZfField_RenderDecl_(O, Facet, ID, ...) \
  struct ZfField(O##_##Facet, ID) : public ZfField(O, ID) { \
    __VA_OPT__(ZfField_RenderDecl__(O, ID, __VA_ARGS__)) \
  };
#define ZfField_RenderDecl(O_Facet, Args) \
  ZuPP_Defer(ZfField_RenderDecl_)(ZuPP_Strip(O_Facet), ZuPP_Strip(Args))

#define ZfField_RenderType_(O, Facet, ID, ...) ZfField(O##_##Facet, ID)
#define ZfField_RenderType(O_Facet, Args) \
  ZuPP_Defer(ZfField_RenderType_)(ZuPP_Strip(O_Facet), ZuPP_Strip(Args))

#define ZfStructRender(API, O, Facet, ...) \
  ZuPP_Eval(ZuPP_MapArg(ZfField_RenderDecl, (O, Facet), __VA_ARGS__)) \
  struct API ZfFields_##O##_##Facet { \
    using Fields = ZuTypeList< \
      ZuPP_Eval(ZuPP_MapArgComma(ZfField_RenderType, (O, Facet), __VA_ARGS__))>; \
    using Keys = ZuFieldIDs<Fields>; \
    static int match(ZuCSpan); \
  }; \
  ZfFields_##O##_##Facet ZuFields_(O *, ZuFacet::Facet *)

// structure implementation code (lives in `.cc`)
// ZfStructImpl(Object[, Facet]); Facet defaults to Core
#define ZfStructImpl_(O, Facet) \
  int ZfFields_##O##_##Facet::match(ZuCSpan s) { \
    static constexpr auto matcher = ZuMatcher<ZfFields_##O##_##Facet>(); \
    return matcher.match(s); \
  }
#define ZfStructImpl_1(O) ZfStructImpl_(O, Core)
#define ZfStructImpl_2(O, Facet) ZfStructImpl_(O, Facet)
#define ZfStructImpl_N(_0, _1, Fn, ...) Fn
#define ZfStructImpl(...) \
  ZfStructImpl_N(__VA_ARGS__, \
    ZfStructImpl_2(__VA_ARGS__), ZfStructImpl_1(__VA_ARGS__))

template <typename Field>
struct ZfFieldPrint_ {
  using O = typename Field::O;
  using Print = typename Field::Type::template Print<ZtFmt::Default>;
  const O &o;
  template <typename S>
  friend inline decltype(auto) operator <<(S &s, const ZfFieldPrint_ &print) {
    return s << Field::id() << ':' << Print{Field::get(print.o)};
  }
};

struct ZfStructPrint : public ZuPrintDelegate {
  template <typename U>
  using Print_Filter =
    ZuBool<!ZuTypeIn<ZuFieldProp::Hidden, typename U::Props>{}>;
  template <typename S, typename O>
  static void print(S &s, const O &o) {
    using Fields = ZuTypeGrep<Print_Filter, ZuFields<O>>;
    s << '{';
    ZuUnroll::all<Fields>([&s, &o]<typename Field>() {
      if constexpr (ZuTypeIndex<Field, Fields>{}) s << ',';
      s << ZfFieldPrint_<Field>{o};
    });
    s << '}';
  }
};

// run-time fields

using ZfVFieldArray = ZuSpan<const ZfVField *>;

template <typename Meta, typename VField>
class ZfVFieldFactory {
  using Fields = typename ZuFields__<Meta>::T;
  enum { N = Fields::N };

  const VField		m_fields[N];
  const ZfVField	*m_ptrs[N];

  template <unsigned ...I>
  ZfVFieldFactory(ZuSeq<I...>) :
      m_fields{VField{ZuType<I, Fields>{}}...},
      m_ptrs{static_cast<const ZfVField *>(&m_fields[I])...},
      fields{m_ptrs, N} { }

public:
  ZfVFieldArray	fields;

  static ZfVFieldFactory *instance() {
    return ZmSingleton<ZfVFieldFactory>::instance();
  }

  ZfVFieldFactory() : ZfVFieldFactory{ZuMkSeq<N>{}} { }
};
template <typename Meta, typename VField = ZfVField>
inline ZfVFieldArray ZfVFields_() {
  using Factory = ZfVFieldFactory<Meta, VField>;
  return Factory::instance()->fields;
}
template <typename O, typename VField = ZfVField>
inline ZfVFieldArray ZfVFields() {
  return ZfVFields_<ZuFieldMeta<O>, VField>();
}

typedef int (*ZfVFieldMatchFn)(ZuCSpan);
template <typename O, typename Facet = ZuFacet::Core>
inline ZfVFieldMatchFn ZfVFieldMatcher() {
  return ZuFieldMeta<O, Facet>::match;
}

// run-time keys
// - each key is a ZuStructKeyT<O, KeyID>, i.e. a value tuple of a
//   subset of the values in the object itself, used to identify the
//   object as a primary or secondary key
// - each key tuple has its own field array, which is extracted and
//   transformed from the underlying object field array

using ZfVKeyFieldArray = ZuSpan<const ZfVFieldArray>;

template <typename O, typename VField>
struct ZfVKeyFields_ {
  ZfVKeyFieldArray	keys;

  static ZfVKeyFields_ *instance() {
    return ZmSingleton<ZfVKeyFields_>::instance();
  }

  ZfVKeyFields_() {
    using KeyIDs = ZuStructKeyIDs<O>;
    static ZfVFieldArray data_[KeyIDs::N];
    ZuUnroll::all<KeyIDs>([](auto i) {
      data_[i] = ZfVFields<ZuStructKeyT<O, i>, VField>();
    });
    keys = {&data_[0], KeyIDs::N};
  }
};
template <typename O, typename VField = ZfVField>
inline ZfVKeyFieldArray ZfVKeyFields() {
  return ZfVKeyFields_<O, VField>::instance()->keys;
}

// standardized field filters
// - Load - fields used for constructing a new object (all non-read-only)
// - Ctor - fields passed to constructor (may include read-only fields)
// - Init - fields initialized post-construction
// - Save - fields used to fully persist an object
// - Upd - mutable fields that may be present in an update, and the primary key
// - Del - all key fields

// ZfFieldFiltered<ZfFieldFilter::Load, Fields>;
namespace ZfFieldFilter {
  template <typename> using All = ZuTrue;

  template <typename Field>
  using Load = ZuBool<!Field::ReadOnly>;

  template <typename Field>
  using Ctor = ZuFieldProp::HasCtor<typename Field::Props>;

  template <typename Field>
  using Init = ZuBool<
    !Field::ReadOnly &&
    !ZuFieldProp::HasCtor<typename Field::Props>{}>;

  // Save is all fields that are either writable, used for constructing
  // the object, or part of a key (a key field can be synthetic)
  template <typename Field>
  using Save = ZuBool<
    bool(Load<Field>{}) ||
    bool(Ctor<Field>{}) ||
    bool(ZuFieldProp::GetKeys<typename Field::Props>::N)>;

  template <typename Field>
  using Upd = ZuBool<
    bool(ZuTypeIn<ZuFieldProp::Mutable, typename Field::Props>{}) ||
    bool(ZuFieldProp::Key<typename Field::Props, 0>{})>;

  template <typename Field>
  using Del = ZuBool<bool(ZuFieldProp::GetKeys<typename Field::Props>::N)>;
}

// ZfVStructInfo aggregates the field metadata a run-time
// introspector needs to access and update a struct;
// this can be used to abstract command/control
struct ZfVStructInfo {
  ZfVFieldMatchFn	matcher;	// match ID to fields[] index
  ZfVFieldArray		fields;
  ZfVKeyFieldArray	keyFields;
};

#endif /* ZfStruct_HH */
