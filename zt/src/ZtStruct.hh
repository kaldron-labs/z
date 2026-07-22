//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// object introspection / reflection
// - extends ZuStruct
// - compile-time (ZtField*) and run-time dynamic polymorphic (ZtVField*)
// - print/scan (CSV, etc.)
// - ORM
// - data series
// - ... any other application that needs to introspect structured data

// metadata macro DSL for identifying and using data fields and keys
//
// ZtStruct((Type[, Facets...]), Fields...)
//
// the parentheses around Type are optional if no facets are specified
//
// each field has compile-time properties, an extensible typelist of types
// that are injected into the ZuFieldProp namespace
//
// a Field is of the form:
// (((Accessor)[, (Props...)]), (Type[, Args...]))
//
// Example: (((id, Rd), (Keys<0>, Ctor<0>)), (String, "default"))
// Meaning: Read-only string field named "id" with a default
//   value of "default" that is also the containing object's zeroth
//   constructor parameter
//
// ZtField Type  C/C++ Type      ZtField Args
// ------------  ----------      ------------
// CString       char *          [, default]
// String        <String>        [, default]
// Bytes         <uint8_t[]>     [, default]
// Bool          <Integral>      [, default]
// Int<Size>     <Integral>      [, default]
// UInt<Size>    <Integral>      [, default]
// Float         <FloatingPoint> [, default]
// Fixed         ZuFixed         [, default]
// Decimal       ZuDecimal       [, default]
// Time          ZuTime          [, default]
// DateTime      ZuDateTime      [, default]
// UDT           <UDT>           [, default]
//
// Range<Minimum, Maximum> is a field property specifying inclusive bounds
// for Int, UInt, Float, Fixed and Decimal fields
//
// *Vec          ZuSpan<T>       [, default]
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
// ZtVField provides run-time introspection via a monomorphic
// (aka type-erased) type - virtual polymorphism and RTTI are intentionally
// avoided:
// - if ZtVField were virtually polymorphic, passing it to dynamically
//   loaded libraries (e.g. data store adapters performing serdes) would
//   entail a far more complex type hierarchy with diamond-shaped
//   inheritance, use of dynamic_cast, etc.
// - ZtVField (and derived classes) benefit from being POD
// - very little syntactic benefit would be obtained

// ZuField<O> is extended to provide:
//   Type	- ZtFieldType<...>
//   deflt()	- canonical default value
//   minimum()	- minimum value (for scalars)
//   maximum()	- maximum value ('')
//
// ZtField(O, ID) is the derived type inheriting from ZuField(O, ID)
//
// ZtFieldType is keyed on <Code, T, Props>, provides:
//   Code	- type code (ZtFieldTC)
//   T		- underlying type
//   Map	- map (if either Enum or Flags)
//   Props	- properties type list
//   Print	- Print<Fmt>{const T &} - compile-time formatted printing
//   vtype()	- ZtVFieldType * instance
// 
// ZtVFieldType provides:
//   code	- ZtFieldTC
//   props	- ZtVFieldProp properties bitfield
//   info	- enum / flags / UDT metadata
//
// ZtVField{ZtField{}} instantiates ZtVField from ZtField
//
// ZtVField provides:
//   type	- ZtVFieldType * instance
//   id		- ZtField::id()
//   props	- ZtVFieldProp properties bitfield
//   keys	- ZtField::keys()
//   get	- ZtVFieldGet
//   set	- ZtVFieldSet
//   constant	- ZtVFieldGet for constants (default, minimum, maximum)
//   cget	- cast ZtVFieldConstant to const void * for ZtVFieldGet
//
// ZtVFieldGet provides:
//   get<Code>(const void *o)
//   print<Code>(auto &s, const void *o, const ZtVField *, const ZtVFmt &)
//
// ZtVFieldSet provides:
//   set<Code>(void *o, auto &&v)
//   scan<Code>(void *o, ZuCSpan s, const ZtVField *, const ZtVFmt &)
//
// ZtVFields<O>() returns the ZtVFieldArray for O
// ZtVKeyFields<O>() returns the ZtVKeyFieldArray for O
// ZtVKeyFields<O>()[KeyID] == ZtVFields<ZuStructKeyT<O, KeyID>>()

#ifndef ZtStruct_HH
#define ZtStruct_HH

#ifndef ZtLib_HH
#include <zlib/ZtLib.hh>
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

#include <zlib/ZmAlloc.hh>
#include <zlib/ZmSingleton.hh>

#include <zlib/ZtQuote.hh>
#include <zlib/ZtEnum.hh>
#include <zlib/ZtString.hh>
#include <zlib/ZtRegex.hh>
#include <zlib/ZtScanBool.hh>
#include <zlib/ZtFmt.hh>

namespace ZtFieldTC {

ZtEnum(ZtFieldTC, int8_t,
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
  struct Hidden { };		// do not print
  struct Hex { };		// print hex value
  struct Required { };		// required - do not default
  struct Series { };		// data series column
  struct Index { };		// - index (e.g. time, nonce, offset, seq#)
  struct Delta { };		// - first derivative
  struct Delta2 { };		// - second derivative

  template <typename Map> struct Enum { using T = Map; };	// enum
  template <typename Map> struct Flags { using T = Map; };	// flags

  template <auto Min, auto Max> struct Range {
    static constexpr auto minimum() { return Min; }
    static constexpr auto maximum() { return Max; }
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
namespace ZtVFieldProp {
  using namespace ZuFieldProp;

  // default Ctor property to -1 for ZtVField
  template <typename Props, bool = HasValue<Props, Ctor>{}>
  struct GetCtor_ { using T = GetValue<Props, Ctor>; };
  template <typename Props>
  struct GetCtor_<Props, false> { using T = ZuInt<-1>; };
  template <typename Props> using GetCtor = typename GetCtor_<Props>::T;

  // default NDP to sentinel null for ZtVField
  template <typename Props, bool = HasValue<Props, NDP>{}>
  struct GetNDP_ { using T = GetValue<Props, NDP>; };
  template <typename Props>
  struct GetNDP_<Props, false> {
    using T = ZuConstant<int8_t, ZuCmp<int8_t>::null()>;
  };
  template <typename Props> using GetNDP = typename GetNDP_<Props>::T;
}

// type properties are a subset of field properties
template <typename Prop> struct ZtFieldType_Props : public ZuFalse { };
template <> struct ZtFieldType_Props<ZuFieldProp::Hidden> : public ZuTrue { };
template <> struct ZtFieldType_Props<ZuFieldProp::Hex> : public ZuTrue { };
template <typename Map>
struct ZtFieldType_Props<ZuFieldProp::Enum<Map>> : public ZuTrue { };
template <typename Map>
struct ZtFieldType_Props<ZuFieldProp::Flags<Map>> : public ZuTrue { };
template <auto I>
struct ZtFieldType_Props<ZuFieldProp::NDP<I>> : public ZuTrue { };

// ZtVFieldProp bitfield encapsulates introspected ZtField properties
namespace ZtVFieldProp {
  ZtFlags(ZtVFieldProp, uint16_t,
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
struct ZtFieldType_ {
  static constexpr ZtVFieldProp::T mprops() {
    return ZtVFieldProp::Value<Props>{};
  }
};

template <int Code, typename T, typename Props>
struct ZtFieldType;

// deduced function to scan from a string
template <typename T, typename = void>
struct ZtFieldType_Scan {
  static auto fn() {
    typedef void (*Fn)(void *, ZuCSpan, const ZtVFmt &);
    return static_cast<Fn>(nullptr);
  }
};
template <typename T>
struct ZtFieldType_Scan<T, decltype((ZuDeclVal<T &>() = ZuCSpan()), void())> {
  static auto fn() {
    return [](void *ptr, ZuCSpan s, const ZtVFmt &) {
      *static_cast<T *>(ptr) = s;
    };
  }
};

// deduced comparison function
template <typename T, typename = void>
struct ZtFieldType_Cmp {
  static auto fn() {
    typedef int (*Fn)(const void *, const void *);
    return static_cast<Fn>(nullptr);
  }
};
template <typename T>
struct ZtFieldType_Cmp<T, decltype(&ZuCmp<T>::cmp, void())> {
  static auto fn() {
    return [](const void *p1, const void *p2) -> int {
      return ZuCmp<T>::cmp(
	  *static_cast<const T *>(p1), *static_cast<const T *>(p2));
    };
  }
};

// ZtVFieldEnum encapsulates introspected enum metadata
struct ZtVFieldEnum {
  const char	*(*id)();
  ZuCSpan	(*print)(int);
  int		(*scan)(ZuCSpan);
};
template <typename Map>
struct ZtVFieldEnum_ : public ZtVFieldEnum {
  ZtVFieldEnum_() : ZtVFieldEnum{
    .id = []() -> const char * { return Map::id(); },
    .print = [](int i) -> ZuCSpan { return Map::v2s(i); },
    .scan = [](ZuCSpan s) -> int { return Map::s2v(s); }
  } { }

  static ZtVFieldEnum *instance() {
    return ZmSingleton<ZtVFieldEnum_>::instance();
  }
};

// ZtVFieldFlags encapsulates introspected flags metadata
struct ZtVFieldFlags {
  const char	*(*id)();
  void		(*print)(uint128_t, ZuVStream &, const ZtVFmt &);
  uint128_t	(*scan)(ZuCSpan, const ZtVFmt &);
};
template <typename Map>
struct ZtVFieldFlags_ : public ZtVFieldFlags {
  ZtVFieldFlags_() : ZtVFieldFlags{
    .id = []() -> const char * { return Map::id(); },
    .print = [](uint128_t v, ZuVStream &s, const ZtVFmt &fmt) -> void {
      s << typename Map::Print(v, fmt.flagsDelim);
    },
    .scan = [](ZuCSpan s, const ZtVFmt &fmt) -> uint128_t {
      return typename Map::Scan{s, fmt.flagsDelim};
    }
  } { }

  static ZtVFieldFlags *instance() {
    return ZmSingleton<ZtVFieldFlags_>::instance();
  }
};

typedef void (*ZtVFieldPrint)(const void *, ZuVStream &, const ZtVFmt &);
typedef void (*ZtVFieldScan)(
  void (*)(void *, const void *), void *, ZuCSpan, const ZtVFmt &);

inline ZuCSpan ZtVFieldTypeID(...) { return {}; }	// default

// ZtVFieldUDT encapsulates introspected UDT metadata
struct ZtVFieldUDT {
  ZuID			id;	// ZtVFieldTypeID(T *);
  const std::type_info	*info;
  ZtVFieldPrint		print;
  ZtVFieldScan		scan;
};

// ZtVFieldType encapsulates introspected type metadata
struct ZtVFieldType {
  int			code;		// ZtFieldTC
  ZtVFieldProp::T	props;		// ZtVFieldProp

  union {
    void		*null;
    ZtVFieldEnum *	(*enum_)();	// Enum
    ZtVFieldFlags *	(*flags)();	// Flags
    ZtVFieldUDT *	(*udt)();	// UDT
  } info;
};

// ZtVFieldConstant is used to retrieve field constants
namespace ZtVFieldConstant {
  enum { Null = 0, Deflt, Minimum, Maximum };
}

// monomorphic (type-erased) equivalent of ZtField
struct ZtVField;

namespace ZtStruct_ {

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

  ZtExtern unsigned string(ZuSpan<char> dst, ZuCSpan &src);

  ZtExtern unsigned strElem(
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

#define ZtVField_GetFn(code, type, fn) \
  template <unsigned Code> \
  ZuIfT<Code == ZtFieldTC::code, type> \
  get(const void *o) const { return get_.fn(o); }

  ZtVField_GetFn(CString, const char *, cstring)
  ZtVField_GetFn(String, ZuCSpan, string)
  ZtVField_GetFn(Bytes, ZuBSpan, bytes)
  ZtVField_GetFn(Bool, bool, bool_)
  ZtVField_GetFn(Int8, int8_t, int8)
  ZtVField_GetFn(UInt8, uint8_t, uint8)
  ZtVField_GetFn(Int16, int16_t, int16)
  ZtVField_GetFn(UInt16, uint16_t, uint16)
  ZtVField_GetFn(Int32, int32_t, int32)
  ZtVField_GetFn(UInt32, uint32_t, uint32)
  ZtVField_GetFn(Int64, int64_t, int64)
  ZtVField_GetFn(UInt64, uint64_t, uint64)
  ZtVField_GetFn(Int128, int128_t, int128)
  ZtVField_GetFn(UInt128, uint128_t, uint128)
  ZtVField_GetFn(Float, double, float_)
  ZtVField_GetFn(Fixed, ZuFixed, fixed)
  ZtVField_GetFn(Decimal, ZuDecimal, decimal)
  ZtVField_GetFn(Time, ZuTime, time)
  ZtVField_GetFn(DateTime, ZuDateTime, dateTime)
  ZtVField_GetFn(UDT, const void *, udt)
  ZtVField_GetFn(CStringVec, CStringVec, cstringVec)
  ZtVField_GetFn(StringVec, StringVec, stringVec)
  ZtVField_GetFn(BytesVec, BytesVec, bytesVec)
  ZtVField_GetFn(Int8Vec, Int8Vec, int8Vec)
  ZtVField_GetFn(UInt8Vec, UInt8Vec, uint8Vec)
  ZtVField_GetFn(Int16Vec, Int16Vec, int16Vec)
  ZtVField_GetFn(UInt16Vec, UInt16Vec, uint16Vec)
  ZtVField_GetFn(Int32Vec, Int32Vec, int32Vec)
  ZtVField_GetFn(UInt32Vec, UInt32Vec, uint32Vec)
  ZtVField_GetFn(Int64Vec, Int64Vec, int64Vec)
  ZtVField_GetFn(UInt64Vec, UInt64Vec, uint64Vec)
  ZtVField_GetFn(Int128Vec, Int128Vec, int128Vec)
  ZtVField_GetFn(UInt128Vec, UInt128Vec, uint128Vec)
  ZtVField_GetFn(FloatVec, FloatVec, floatVec)
  ZtVField_GetFn(FixedVec, FixedVec, fixedVec)
  ZtVField_GetFn(DecimalVec, DecimalVec, decimalVec)
  ZtVField_GetFn(TimeVec, TimeVec, timeVec)
  ZtVField_GetFn(DateTimeVec, DateTimeVec, dateTimeVec)

#define ZtVField_PrintFn(Code_) \
  template <unsigned Code, typename S> \
  ZuIfT<Code == ZtFieldTC::Code_> \
  print(S &, const void *, const ZtVField *, const ZtVFmt &) const;

  ZtVField_PrintFn(CString)
  ZtVField_PrintFn(String)
  ZtVField_PrintFn(Bytes)
  ZtVField_PrintFn(Bool)
  ZtVField_PrintFn(Int8)
  ZtVField_PrintFn(UInt8)
  ZtVField_PrintFn(Int16)
  ZtVField_PrintFn(UInt16)
  ZtVField_PrintFn(Int32)
  ZtVField_PrintFn(UInt32)
  ZtVField_PrintFn(Int64)
  ZtVField_PrintFn(UInt64)
  ZtVField_PrintFn(Int128)
  ZtVField_PrintFn(UInt128)
  ZtVField_PrintFn(Float)
  ZtVField_PrintFn(Fixed)
  ZtVField_PrintFn(Decimal)
  ZtVField_PrintFn(Time)
  ZtVField_PrintFn(DateTime)
  ZtVField_PrintFn(UDT)
  ZtVField_PrintFn(CStringVec)
  ZtVField_PrintFn(StringVec)
  ZtVField_PrintFn(BytesVec)
  ZtVField_PrintFn(Int8Vec)
  ZtVField_PrintFn(UInt8Vec)
  ZtVField_PrintFn(Int16Vec)
  ZtVField_PrintFn(UInt16Vec)
  ZtVField_PrintFn(Int32Vec)
  ZtVField_PrintFn(UInt32Vec)
  ZtVField_PrintFn(Int64Vec)
  ZtVField_PrintFn(UInt64Vec)
  ZtVField_PrintFn(Int128Vec)
  ZtVField_PrintFn(UInt128Vec)
  ZtVField_PrintFn(FloatVec)
  ZtVField_PrintFn(FixedVec)
  ZtVField_PrintFn(DecimalVec)
  ZtVField_PrintFn(TimeVec)
  ZtVField_PrintFn(DateTimeVec)
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

#define ZtVField_SetFn(code, type, fn) \
  template <unsigned Code> \
  ZuIfT<Code == ZtFieldTC::code> \
  set(void *o, type v) const { set_.fn(o, v); }

  ZtVField_SetFn(CString, const char *, cstring)
  ZtVField_SetFn(String, ZuCSpan, string)
  ZtVField_SetFn(Bytes, ZuBSpan, bytes)
  ZtVField_SetFn(Bool, bool, bool_)
  ZtVField_SetFn(Int8, int8_t, int8)
  ZtVField_SetFn(UInt8, uint8_t, uint8)
  ZtVField_SetFn(Int16, int16_t, int16)
  ZtVField_SetFn(UInt16, uint16_t, uint16)
  ZtVField_SetFn(Int32, int32_t, int32)
  ZtVField_SetFn(UInt32, uint32_t, uint32)
  ZtVField_SetFn(Int64, int64_t, int64)
  ZtVField_SetFn(UInt64, uint64_t, uint64)
  ZtVField_SetFn(Int128, int128_t, int128)
  ZtVField_SetFn(UInt128, uint128_t, uint128)
  ZtVField_SetFn(Float, double, float_)
  ZtVField_SetFn(Fixed, ZuFixed, fixed)
  ZtVField_SetFn(Decimal, ZuDecimal, decimal)
  ZtVField_SetFn(Time, ZuTime, time)
  ZtVField_SetFn(DateTime, ZuDateTime, dateTime)
  ZtVField_SetFn(UDT, const void *, udt)
  ZtVField_SetFn(CStringVec, CStringVec, cstringVec)
  ZtVField_SetFn(StringVec, StringVec, stringVec)
  ZtVField_SetFn(BytesVec, BytesVec, bytesVec)
  ZtVField_SetFn(Int8Vec, Int8Vec, int8Vec)
  ZtVField_SetFn(UInt8Vec, UInt8Vec, uint8Vec)
  ZtVField_SetFn(Int16Vec, Int16Vec, int16Vec)
  ZtVField_SetFn(UInt16Vec, UInt16Vec, uint16Vec)
  ZtVField_SetFn(Int32Vec, Int32Vec, int32Vec)
  ZtVField_SetFn(UInt32Vec, UInt32Vec, uint32Vec)
  ZtVField_SetFn(Int64Vec, Int64Vec, int64Vec)
  ZtVField_SetFn(UInt64Vec, UInt64Vec, uint64Vec)
  ZtVField_SetFn(Int128Vec, Int128Vec, int128Vec)
  ZtVField_SetFn(UInt128Vec, UInt128Vec, uint128Vec)
  ZtVField_SetFn(FloatVec, FloatVec, floatVec)
  ZtVField_SetFn(FixedVec, FixedVec, fixedVec)
  ZtVField_SetFn(DecimalVec, DecimalVec, decimalVec)
  ZtVField_SetFn(TimeVec, TimeVec, timeVec)
  ZtVField_SetFn(DateTimeVec, DateTimeVec, dateTimeVec)

#define ZtVField_ScanFn(code) \
  template <unsigned Code> \
  ZuIfT<Code == ZtFieldTC::code> \
  scan(void *, ZuCSpan, const ZtVField *, const ZtVFmt &) const;

  ZtVField_ScanFn(CString)
  ZtVField_ScanFn(String)
  ZtVField_ScanFn(Bytes)
  ZtVField_ScanFn(Bool)
  ZtVField_ScanFn(Int8)
  ZtVField_ScanFn(UInt8)
  ZtVField_ScanFn(Int16)
  ZtVField_ScanFn(UInt16)
  ZtVField_ScanFn(Int32)
  ZtVField_ScanFn(UInt32)
  ZtVField_ScanFn(Int64)
  ZtVField_ScanFn(UInt64)
  ZtVField_ScanFn(Int128)
  ZtVField_ScanFn(UInt128)
  ZtVField_ScanFn(Float)
  ZtVField_ScanFn(Fixed)
  ZtVField_ScanFn(Decimal)
  ZtVField_ScanFn(Time)
  ZtVField_ScanFn(DateTime)
  ZtVField_ScanFn(UDT)
  ZtVField_ScanFn(CStringVec)
  ZtVField_ScanFn(StringVec)
  ZtVField_ScanFn(BytesVec)
  ZtVField_ScanFn(Int8Vec)
  ZtVField_ScanFn(UInt8Vec)
  ZtVField_ScanFn(Int16Vec)
  ZtVField_ScanFn(UInt16Vec)
  ZtVField_ScanFn(Int32Vec)
  ZtVField_ScanFn(UInt32Vec)
  ZtVField_ScanFn(Int64Vec)
  ZtVField_ScanFn(UInt64Vec)
  ZtVField_ScanFn(Int128Vec)
  ZtVField_ScanFn(UInt128Vec)
  ZtVField_ScanFn(FloatVec)
  ZtVField_ScanFn(FixedVec)
  ZtVField_ScanFn(DecimalVec)
  ZtVField_ScanFn(TimeVec)
  ZtVField_ScanFn(DateTimeVec)
};

} // ZtStruct_

using ZtVFieldGet = ZtStruct_::VGet;
using ZtVFieldSet = ZtStruct_::VSet;

// ZtVField is the monomorphic (type-erased) equivalent of ZtField
struct ZtVField {
  ZtVFieldType		*type;
  ZuCSpan		id;
  ZtVFieldProp::T	props;
  uint64_t		keys;
  uint64_t		group;
  uint64_t		descend;
  int16_t		ctor;	// -1 if not a constructor parameter
  int8_t		ndp;	// defaults to sentinel null

  ZtVFieldGet		get;
  ZtVFieldSet		set;

  ZtVFieldGet		constant;

  template <typename Field>
  ZtVField(Field) :
      type{Field::Type::vtype()},
      id{Field::id()},
      props{Field::mprops()},
      keys{ZuSeqBitmap<ZuFieldProp::GetKeys<typename Field::Props>>()},
      group{ZuSeqBitmap<ZuFieldProp::GetGroup<typename Field::Props>>()},
      descend{ZuSeqBitmap<ZuFieldProp::GetDescend<typename Field::Props>>()},
      ctor{ZtVFieldProp::GetCtor<typename Field::Props>{}},
      ndp{ZtVFieldProp::GetNDP<typename Field::Props>{}},
      get{Field::getFn()},
      set{Field::setFn()},
      constant{Field::constantFn()} { }

  // pseudo-pointer parameter to ZtVFieldGet::get() for the constant values,
  // - see ZtVFieldConstant
  static const void *cget(int c) {
    return reinterpret_cast<void *>(static_cast<uintptr_t>(c));
  }

  // need to de-conflict with print
  template <typename S> void print_(S &s) const {
    s << "id=" << id << " type=" << ZtFieldTC::name(type->code);
    s << " props=" << ZtVFieldProp::Map::Print(
      props & ~(ZtVFieldProp::Ctor() | ZtVFieldProp::NDP()));
    if (props & ZtVFieldProp::Ctor()) {
      if (props & ~(ZtVFieldProp::Ctor() | ZtVFieldProp::NDP())) s << '|';
      s << "Ctor(" << ctor << ')';
    }
    if (props & ZtVFieldProp::NDP()) {
      if (props & ~ZtVFieldProp::NDP()) s << '|';
      s << "NDP(" << int(ndp) << ')';
    }
    s << " keys=" << *reinterpret_cast<const ZuBitmap<64> *>(&keys);
    s << " group=" << *reinterpret_cast<const ZuBitmap<64> *>(&group);
    s << " descend=" << *reinterpret_cast<const ZuBitmap<64> *>(&descend);
  }
  friend ZuPrintLambda<[]() {
    return [](auto &s, const auto &v) { v.print_(s); };
  }> ZuPrintType(ZtVField *);
};

namespace ZtStruct_ {

// VGet print functions
template <unsigned Code, typename S>
inline ZuIfT<Code == ZtFieldTC::CString>
VGet::print(
  S &s, const void *o, const ZtVField *field,
  const ZtVFmt &fmt
) const {
  auto v = get_.cstring(o);
  s << Print::CString{v};
}
template <unsigned Code, typename S>
inline ZuIfT<Code == ZtFieldTC::String>
VGet::print(
  S &s, const void *o, const ZtVField *field,
  const ZtVFmt &fmt
) const {
  auto v = get_.string(o);
  s << Print::String{v};
}
template <unsigned Code, typename S>
inline ZuIfT<Code == ZtFieldTC::Bytes>
VGet::print(
  S &s, const void *o, const ZtVField *, const ZtVFmt &
) const {
  ZuBSpan v = get_.bytes(o);
  auto n = ZuBase64::enclen(v.length());
  auto buf_ = ZmAlloc(uint8_t, n);
  ZuSpan<uint8_t> buf(&buf_[0], n);
  buf.trunc(ZuBase64::encode(buf, v));
  s << ZuCSpan(buf);
}
template <unsigned Code, typename S>
inline ZuIfT<Code == ZtFieldTC::Bool>
VGet::print(
  S &s, const void *o, const ZtVField *, const ZtVFmt &
) const {
  s << (get_.bool_(o) ? '1' : '0');
}

template <typename S, typename T>
inline void ZtVField_printInt_(
  S &s, const T &v, const ZtVField *field, const ZtVFmt &fmt)
{
  if (ZuUnlikely(field->props & ZtVFieldProp::Enum())) {
    s << field->type->info.enum_()->print(v);
    return;
  }
  if (ZuUnlikely(field->props & ZtVFieldProp::Flags())) {
    ZuVStream s_{s};
    field->type->info.flags()->print(v, s_, fmt);
    return;
  }
  if (field->props & ZtVFieldProp::Hex()) {
    s << v.vfmt(fmt.scalar).hex();
    return;
  }
  s << v.vfmt(fmt.scalar);
}

#define ZtVField_printInt(width) \
template <unsigned Code, typename S> \
inline ZuIfT<Code == ZtFieldTC::Int##width> \
VGet::print( \
  S &s, const void *o, const ZtVField *field, const ZtVFmt &fmt \
) const { \
  ZuBox<int##width##_t> v = get_.int##width(o); \
  ZtVField_printInt_(s, v, field, fmt); \
} \
template <unsigned Code, typename S> \
inline ZuIfT<Code == ZtFieldTC::UInt##width> \
VGet::print( \
  S &s, const void *o, const ZtVField *field, const ZtVFmt &fmt \
) const { \
  ZuBox<uint##width##_t> v = get_.uint##width(o); \
  ZtVField_printInt_(s, v, field, fmt); \
}

ZtVField_printInt(8)
ZtVField_printInt(16)
ZtVField_printInt(32)
ZtVField_printInt(64)
ZtVField_printInt(128)

template <unsigned Code, typename S>
inline ZuIfT<Code == ZtFieldTC::Float>
VGet::print(
  S &s, const void *o, const ZtVField *field, const ZtVFmt &fmt
) const {
  ZuBox<double> v = get_.float_(o);
  auto ndp = field->ndp;
  if (!ZuNull(ndp))
    s << v.vfmt(fmt.scalar).fp(ndp);
  else
    s << v.vfmt(fmt.scalar);
}
template <unsigned Code, typename S>
inline ZuIfT<Code == ZtFieldTC::Fixed>
VGet::print(
  S &s, const void *o, const ZtVField *field, const ZtVFmt &fmt
) const {
  ZuFixed v = get_.fixed(o);
  auto ndp = field->ndp;
  if (!ZuNull(ndp))
    s << v.vfmt(fmt.scalar).fp(ndp);
  else
    s << v.vfmt(fmt.scalar);
}
template <unsigned Code, typename S>
inline ZuIfT<Code == ZtFieldTC::Decimal>
VGet::print(
  S &s, const void *o, const ZtVField *field, const ZtVFmt &fmt
) const {
  ZuDecimal v = get_.decimal(o);
  auto ndp = field->ndp;
  if (!ZuNull(ndp))
    s << v.vfmt(fmt.scalar).fp(ndp);
  else
    s << v.vfmt(fmt.scalar);
}
template <unsigned Code, typename S>
inline ZuIfT<Code == ZtFieldTC::Time>
VGet::print(
  S &s, const void *o, const ZtVField *, const ZtVFmt &fmt
) const {
  ZuDateTime v{get_.time(o)};
  s << v.fmt(fmt.datePrint);
}
template <unsigned Code, typename S>
inline ZuIfT<Code == ZtFieldTC::DateTime>
VGet::print(
  S &s, const void *o, const ZtVField *, const ZtVFmt &fmt
) const {
  ZuDateTime v{get_.dateTime(o)};
  s << v.fmt(fmt.datePrint);
}
template <unsigned Code, typename S>
inline ZuIfT<Code == ZtFieldTC::UDT>
VGet::print(
  S &s_, const void *o, const ZtVField *field, const ZtVFmt &fmt
) const {
  ZuVStream s{s_};
  field->type->info.udt()->print(get_.udt(o), s, fmt);
}

template <unsigned Code, typename S>
inline ZuIfT<Code == ZtFieldTC::CStringVec>
VGet::print(
  S &s, const void *o, const ZtVField *, const ZtVFmt &fmt
) const {
  s << fmt.vecPrefix;
  bool first = true;
  CStringVec vec{get_.cstringVec(o)};
  vec.all([&s, &first, &fmt](const char *v) {
    if (!first) s << fmt.vecDelim; else first = false;
    s << Print::CString{v};
  });
  s << fmt.vecSuffix;
}
template <unsigned Code, typename S>
inline ZuIfT<Code == ZtFieldTC::StringVec>
VGet::print(
  S &s, const void *o, const ZtVField *, const ZtVFmt &fmt
) const {
  s << fmt.vecPrefix;
  bool first = true;
  StringVec vec{get_.stringVec(o)};
  vec.all([&s, &first, &fmt](ZuCSpan v) {
    if (!first) s << fmt.vecDelim; else first = false;
    s << Print::String{v};
  });
  s << fmt.vecSuffix;
}
template <unsigned Code, typename S>
inline ZuIfT<Code == ZtFieldTC::BytesVec>
VGet::print(
  S &s, const void *o, const ZtVField *, const ZtVFmt &fmt
) const {
  s << fmt.vecPrefix;
  bool first = true;
  BytesVec vec{get_.bytesVec(o)};
  vec.all([&s, &fmt, &first](ZuBSpan v) {
    if (!first) s << fmt.vecDelim; else first = false;
    s << Print::Bytes{v};
  });
  s << fmt.vecSuffix;
}

#define ZtVField_printIntVec(width) \
template <unsigned Code, typename S> \
inline ZuIfT<Code == ZtFieldTC::Int##width##Vec> \
VGet::print( \
  S &s, const void *o, const ZtVField *field, const ZtVFmt &fmt \
) const { \
  s << fmt.vecPrefix; \
  Int##width##Vec vec{get_.int##width##Vec(o)}; \
  bool first = true; \
  vec.all([&s, field, &fmt, &first](ZuBox<int##width##_t> v) { \
    if (!first) s << fmt.vecDelim; else first = false; \
    ZtVField_printInt_(s, v, field, fmt); \
  }); \
  s << fmt.vecSuffix; \
} \
template <unsigned Code, typename S> \
inline ZuIfT<Code == ZtFieldTC::UInt##width##Vec> \
VGet::print( \
  S &s, const void *o, const ZtVField *field, const ZtVFmt &fmt \
) const { \
  s << fmt.vecPrefix; \
  UInt##width##Vec vec{get_.uint##width##Vec(o)}; \
  bool first = true; \
  vec.all([&s, field, &fmt, &first](ZuBox<uint##width##_t> v) { \
    if (!first) s << fmt.vecDelim; else first = false; \
    ZtVField_printInt_(s, v, field, fmt); \
  }); \
  s << fmt.vecSuffix; \
}
ZtVField_printIntVec(8)
ZtVField_printIntVec(16)
ZtVField_printIntVec(32)
ZtVField_printIntVec(64)
ZtVField_printIntVec(128)

template <unsigned Code, typename S>
inline ZuIfT<Code == ZtFieldTC::FloatVec>
VGet::print(
  S &s, const void *o, const ZtVField *field, const ZtVFmt &fmt
) const {
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
}
template <unsigned Code, typename S>
inline ZuIfT<Code == ZtFieldTC::FixedVec>
VGet::print(
  S &s, const void *o, const ZtVField *field, const ZtVFmt &fmt
) const {
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
}
template <unsigned Code, typename S>
inline ZuIfT<Code == ZtFieldTC::DecimalVec>
VGet::print(
  S &s, const void *o, const ZtVField *field, const ZtVFmt &fmt
) const {
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
}
template <unsigned Code, typename S>
inline ZuIfT<Code == ZtFieldTC::TimeVec>
VGet::print(
  S &s, const void *o, const ZtVField *field, const ZtVFmt &fmt
) const {
  s << fmt.vecPrefix;
  bool first = true;
  TimeVec vec{get_.timeVec(o)};
  vec.all([&s, &fmt, &first](const ZuTime &v_) {
    ZuDateTime v{v_};
    if (!first) s << fmt.vecDelim; else first = false;
    s << v.fmt(fmt.datePrint);
  });
  s << fmt.vecSuffix;
}
template <unsigned Code, typename S>
inline ZuIfT<Code == ZtFieldTC::DateTimeVec>
VGet::print(
  S &s, const void *o, const ZtVField *field, const ZtVFmt &fmt
) const {
  s << fmt.vecPrefix;
  bool first = true;
  DateTimeVec vec{get_.dateTimeVec(o)};
  vec.all([&s, &fmt, &first](const ZuDateTime &v) {
    if (!first) s << fmt.vecDelim; else first = false;
    s << v.fmt(fmt.datePrint);
  });
  s << fmt.vecSuffix;
}

// VSet scan functions
template <unsigned Code>
inline ZuIfT<Code == ZtFieldTC::CString>
VSet::scan(
  void *o, ZuCSpan s, const ZtVField *, const ZtVFmt &
) const {
  if (!s) {
    set_.cstring(o, nullptr);
    return;
  }
  unsigned n = s.length() + 1;
  auto buf_ = ZmAlloc(char, n);
  ZuSpan<char> buf(&buf_[0], n);
  buf[Scan::string(buf, s)] = 0;
  set_.cstring(o, &buf[0]);
}
template <unsigned Code>
inline ZuIfT<Code == ZtFieldTC::String>
VSet::scan(
  void *o, ZuCSpan s, const ZtVField *, const ZtVFmt &
) const {
  if (!s) {
    set_.string(o, s);
    return;
  }
  unsigned n = s.length();
  auto buf_ = ZmAlloc(char, n);
  ZuSpan<char> buf(&buf_[0], n);
  buf.trunc(Scan::string(buf, s));
  set_.string(o, buf);
}
template <unsigned Code>
inline ZuIfT<Code == ZtFieldTC::Bytes>
VSet::scan(
  void *o, ZuCSpan s, const ZtVField *, const ZtVFmt &
) const {
  auto n = ZuBase64::declen(s.length());
  auto buf_ = ZmAlloc(uint8_t, n);
  ZuSpan<uint8_t> buf(&buf_[0], n);
  buf.trunc(ZuBase64::decode(buf, ZuBSpan{s}));
  set_.bytes(o, buf);
}
template <unsigned Code>
inline ZuIfT<Code == ZtFieldTC::Bool>
VSet::scan(
  void *o, ZuCSpan s, const ZtVField *, const ZtVFmt &
) const {
  set_.bool_(o, ZtScanBool(s));
}

template <typename T>
T ZtVField_scanInt_(ZuCSpan s, const ZtVField *field, const ZtVFmt &fmt)
{
  if (ZuUnlikely(field->props & ZtVFieldProp::Enum()))
    return field->type->info.enum_()->scan(s);
  if (ZuUnlikely(field->props & ZtVFieldProp::Flags()))
    return field->type->info.flags()->scan(s, fmt);
  if (field->props & ZtVFieldProp::Hex())
    return ZuBox<T>{ZuFmt::Hex<>{}, s};
  return ZuBox<T>{s};
}

#define ZtVField_scanInt(width) \
template <unsigned Code> \
inline ZuIfT<Code == ZtFieldTC::Int##width> \
VSet::scan( \
  void *o, ZuCSpan s, const ZtVField *field, const ZtVFmt &fmt) const \
{ \
  set_.int##width(o, ZtVField_scanInt_<int##width##_t>(s, field, fmt)); \
} \
template <unsigned Code> \
inline ZuIfT<Code == ZtFieldTC::UInt##width> \
VSet::scan( \
  void *o, ZuCSpan s, const ZtVField *field, const ZtVFmt &fmt) const \
{ \
  set_.uint##width(o, ZtVField_scanInt_<uint##width##_t>(s, field, fmt)); \
}

ZtVField_scanInt(8)
ZtVField_scanInt(16)
ZtVField_scanInt(32)
ZtVField_scanInt(64)
ZtVField_scanInt(128)

template <unsigned Code>
inline ZuIfT<Code == ZtFieldTC::Float>
VSet::scan(
  void *o, ZuCSpan s, const ZtVField *, const ZtVFmt &
) const {
  set_.float_(o, ZuBox<double>{s});
}
template <unsigned Code>
inline ZuIfT<Code == ZtFieldTC::Fixed>
VSet::scan(
  void *o, ZuCSpan s, const ZtVField *, const ZtVFmt &
) const {
  set_.fixed(o, ZuFixed{s});
}
template <unsigned Code>
inline ZuIfT<Code == ZtFieldTC::Decimal>
VSet::scan(
  void *o, ZuCSpan s, const ZtVField *, const ZtVFmt &
) const {
  set_.decimal(o, ZuDecimal{s});
}
template <unsigned Code>
inline ZuIfT<Code == ZtFieldTC::Time>
VSet::scan(
  void *o, ZuCSpan s, const ZtVField *, const ZtVFmt &fmt
) const {
  set_.time(o, ZuDateTime{fmt.dateScan, s}.as_time());
}
template <unsigned Code>
inline ZuIfT<Code == ZtFieldTC::DateTime>
VSet::scan(
  void *o, ZuCSpan s, const ZtVField *, const ZtVFmt &fmt
) const {
  set_.dateTime(o, ZuDateTime{fmt.dateScan, s});
}
template <unsigned Code>
inline ZuIfT<Code == ZtFieldTC::UDT>
VSet::scan(
  void *o, ZuCSpan s, const ZtVField *field,
  const ZtVFmt &fmt
) const {
  field->type->info.udt()->scan(field->set.set_.udt, o, s, fmt);
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

template <unsigned Code>
inline ZuIfT<Code == ZtFieldTC::CStringVec>
VSet::scan(
  void *o, ZuCSpan s, const ZtVField *, const ZtVFmt &fmt
) const {
  VecScan::scan(s, fmt, [this, o, &fmt](ZuCSpan &s) {
    auto m = s.length();
    auto buf_ = ZmAlloc(char, m + 1);
    ZuSpan<char> buf(&buf_[0], m + 1);
    unsigned n = Scan::strElem(buf, s, fmt.vecDelim, fmt.vecSuffix);
    if (n) {
      buf[n] = 0;
      set_.cstring(o, &buf[0]);
      return true;
    }
    return false;
  });
}
template <unsigned Code>
inline ZuIfT<Code == ZtFieldTC::StringVec>
VSet::scan(
  void *o, ZuCSpan s, const ZtVField *, const ZtVFmt &fmt
) const {
  VecScan::scan(s, fmt, [this, o, &fmt](ZuCSpan &s) {
    auto m = s.length();
    auto buf_ = ZmAlloc(char, m);
    ZuSpan<char> buf(&buf_[0], m);
    unsigned n = Scan::strElem(buf, s, fmt.vecDelim, fmt.vecSuffix);
    if (n) {
      buf.trunc(n);
      set_.string(o, ZuCSpan(&buf[0], buf.length()));
      return true;
    }
    return false;
  });
}
template <unsigned Code>
inline ZuIfT<Code == ZtFieldTC::BytesVec>
VSet::scan(
  void *o, ZuCSpan s, const ZtVField *, const ZtVFmt &fmt
) const {
  VecScan::scan(s, fmt, [this, o](ZuCSpan &s) {
    unsigned n = 0;
    auto m = s.length();
    while (n < m && ZuBase64::is(s[n])) n++;
    n = ZuBase64::declen(m = n);
    if (n) {
      auto buf_ = ZmAlloc(uint8_t, n);
      ZuSpan<uint8_t> buf(&buf_[0], n);
      buf.trunc(ZuBase64::decode(buf, ZuBSpan{s}));
      set_.bytes(o, ZuBSpan{&buf[0], buf.length()});
      s.offset(m);
      return true;
    }
    return false;
  });
}

// scan forward until a delimiter, suffix or end of string is encountered
inline ZuCSpan ZtVField_scanVecElem(ZuCSpan s, const ZtVFmt &fmt)
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

// scan integer from a vector string
template <typename T>
int ZtVField_scanIntVec_(
  T &v, ZuCSpan s, const ZtVField *field, const ZtVFmt &fmt)
{
  if (ZuUnlikely(field->props & ZtVFieldProp::Enum())) {
    auto s_ = ZtVField_scanVecElem(s, fmt);
    auto v_ = field->type->info.enum_()->scan(s_);
    v = v_;
    if (v_ < 0) return -1;
    return int(s_.length());
  }
  if (ZuUnlikely(field->props & ZtVFieldProp::Flags())) {
    auto s_ = ZtVField_scanVecElem(s, fmt);
    auto v_ = field->type->info.flags()->scan(s_, fmt);
    v = v_;
    if (!v_) return -1;
    return int(s_.length());
  }
  if (field->props & ZtVFieldProp::Hex())
    return v.template scan<ZuFmt::Hex<>>(s);
  return v.scan(s);
}

#define ZtVField_scanIntVec(width) \
template <unsigned Code> \
inline ZuIfT<Code == ZtFieldTC::Int##width##Vec> \
VSet::scan( \
  void *o, ZuCSpan s, const ZtVField *field, const ZtVFmt &fmt \
) const { \
  VecScan::scan(s, fmt, [this, o, field, fmt](ZuCSpan &s) { \
    ZuBox<int##width##_t> v; \
    int n = ZtVField_scanIntVec_(v, s, field, fmt); \
    if (n > 0) { \
      set_.int##width(o, v); \
      s.offset(n); \
      return true; \
    } \
    return false; \
  }); \
} \
template <unsigned Code> \
inline ZuIfT<Code == ZtFieldTC::UInt##width##Vec> \
VSet::scan( \
  void *o, ZuCSpan s, const ZtVField *field, const ZtVFmt &fmt \
) const { \
  VecScan::scan(s, fmt, [this, o, field, fmt](ZuCSpan &s) { \
    ZuBox<uint##width##_t> v; \
    int n = ZtVField_scanIntVec_(v, s, field, fmt); \
    if (n > 0) { \
      set_.uint##width(o, v); \
      s.offset(n); \
      return true; \
    } \
    return false; \
  }); \
}

ZtVField_scanIntVec(8)
ZtVField_scanIntVec(16)
ZtVField_scanIntVec(32)
ZtVField_scanIntVec(64)
ZtVField_scanIntVec(128)

template <unsigned Code>
inline ZuIfT<Code == ZtFieldTC::FloatVec>
VSet::scan(
  void *o, ZuCSpan s, const ZtVField *, const ZtVFmt &fmt
) const {
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
}
template <unsigned Code>
inline ZuIfT<Code == ZtFieldTC::FixedVec>
VSet::scan(
  void *o, ZuCSpan s, const ZtVField *, const ZtVFmt &fmt
) const {
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
}
template <unsigned Code>
inline ZuIfT<Code == ZtFieldTC::DecimalVec>
VSet::scan(
  void *o, ZuCSpan s, const ZtVField *, const ZtVFmt &fmt
) const {
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
}
template <unsigned Code>
inline ZuIfT<Code == ZtFieldTC::TimeVec>
VSet::scan(
  void *o, ZuCSpan s, const ZtVField *, const ZtVFmt &fmt
) const {
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
}
template <unsigned Code>
inline ZuIfT<Code == ZtFieldTC::DateTimeVec>
VSet::scan(
  void *o, ZuCSpan s, const ZtVField *, const ZtVFmt &fmt
) const {
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

} // ZtStruct_

// ZtField compile-time encapsulates an individual field, derives from ZuField
template <typename Base_>
struct ZtField_ : public Base_ {
  using Base = Base_;
  using Orig = Base;
  using O = typename Base::O;
  using T = typename Base::T;
  using Props = typename Base::Props;
  static constexpr ZtVFieldProp::T mprops() {
    return ZtVFieldProp::Value<Props>{};
  }
};

// --- CString

template <typename T, typename Props>
struct ZtFieldType_CString;
template <typename Props_>
struct ZtFieldType_CString<char *, Props_> : public ZtFieldType_<Props_> {
  enum { Code = ZtFieldTC::CString };
  using T = char *;
  using Props = Props_;
  template <typename = ZtFmt::Default>
  using Print = ZtStruct_::Print::CString;
  inline static ZtVFieldType *vtype();
};
template <typename T, typename Props>
struct ZtFieldType<ZtFieldTC::CString, T, Props> :
    public ZtFieldType_CString<T, Props> { };

template <typename T, typename Props>
struct ZtVFieldType_CString;
template <typename Props>
struct ZtVFieldType_CString<char *, Props> : public ZtVFieldType {
  using T = char *;
  ZtVFieldType_CString() : ZtVFieldType{
    .code = ZtFieldTC::CString,
    .props = ZtVFieldProp::Value<Props>{},
    .info = {.null = nullptr}
  } { }
};
template <typename Props>
ZtVFieldType *ZtFieldType_CString<char *, Props>::vtype() {
  return ZmSingleton<ZtVFieldType_CString<char *, Props>>::instance();
}

inline const char *ZtField_CString_Def() { return nullptr; }
template <
  typename Base,
  auto Def = ZtField_CString_Def,
  bool = Base::ReadOnly>
struct ZtField_CString : public ZtField_<Base> {
  template <template <typename> class Override>
  using Adapt = ZtField_CString<Override<Base>>;
  using O = typename Base::O;
  using T = const char *;
  using Props = typename Base::Props;
  using Type =
    ZtFieldType_CString<char *, ZuTypeGrep<ZtFieldType_Props, Props>>;
  enum { Code = Type::Code };
  static ZtVFieldGet getFn() {
    return {.get_ = {.cstring = [](const void *o) -> const char * {
      return Base::get(*static_cast<const O *>(o));
    }}};
  }
  static ZtVFieldSet setFn() {
    return {.set_ = {.cstring = [](void *, const char *) { }}};
  }
  static const char *deflt() { return Def(); }
  static ZtVFieldGet constantFn() {
    using namespace ZtVFieldConstant;
    return {.get_ = {.cstring = [](const void *o) -> const char * {
      switch (int(reinterpret_cast<uintptr_t>(o))) {
	case Deflt: return Def();
	default:    return nullptr;
      }
    }}};
  }
};
template <typename Base, auto Def>
struct ZtField_CString<Base, Def, false> :
    public ZtField_CString<Base, Def, true> {
  using O = typename Base::O;
  static ZtVFieldSet setFn() {
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
struct ZtFieldType_String : public ZtFieldType_<Props_> {
  enum { Code = ZtFieldTC::String };
  using T = T_;
  using Props = Props_;
  template <typename = ZtFmt::Default>
  using Print = ZtStruct_::Print::String;
  inline static ZtVFieldType *vtype();
};
template <typename T, typename Props>
struct ZtFieldType<ZtFieldTC::String, T, Props> :
    public ZtFieldType_String<T, Props> { };

template <typename T, typename Props>
struct ZtVFieldType_String : public ZtVFieldType {
  ZtVFieldType_String() : ZtVFieldType{
    .code = ZtFieldTC::String,
    .props = ZtVFieldProp::Value<Props>{},
    .info = {.null = nullptr}
  } { }
};
template <typename T, typename Props>
ZtVFieldType *ZtFieldType_String<T, Props>::vtype() {
  return ZmSingleton<ZtVFieldType_String<T, Props>>::instance();
}

inline ZuCSpan ZtField_String_Def() { return {}; }
template <typename Base, typename = void>
struct ZtField_String_Get {
  static ZtVFieldGet getFn() {
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
struct ZtField_String_Get<Base,
    decltype(&Base::get(ZuDeclVal<const typename Base::O &>()), void())> {
  static ZtVFieldGet getFn() {
    using O = typename Base::O;
    // field get() returns a crvalue
    return {.get_ = {.string = [](const void *o) -> ZuCSpan {
      return Base::get(*static_cast<const O *>(o));
    }}};
  }
};
template <
  typename Base,
  auto Def = ZtField_String_Def,
  bool = Base::ReadOnly>
struct ZtField_String :
    public ZtField_<Base>,
    public ZtField_String_Get<Base> {
  template <template <typename> class Override>
  using Adapt = ZtField_String<Override<Base>>;
  using O = typename Base::O;
  using T = typename Base::T;
  using Props = typename Base::Props;
  using Type = ZtFieldType_String<T, ZuTypeGrep<ZtFieldType_Props, Props>>;
  enum { Code = Type::Code };
  static ZtVFieldSet setFn() {
    return {.set_ = {.string = [](void *, ZuCSpan) { }}};
  }
  static ZuCSpan deflt() { return Def(); }
  static ZtVFieldGet constantFn() {
    using namespace ZtVFieldConstant;
    return {.get_ = {.string = [](const void *o) -> ZuCSpan {
      switch (int(reinterpret_cast<uintptr_t>(o))) {
	case Deflt: return Def();
	default:    return {};
      }
    }}};
  }
};
template <typename Base, auto Def>
struct ZtField_String<Base, Def, false> :
    public ZtField_String<Base, Def, true> {
  using O = typename Base::O;
  static ZtVFieldSet setFn() {
    return {.set_ = {.string = [](void *o, ZuCSpan s) {
      Base::set(*static_cast<O *>(o), s);
    }}};
  }
};

// --- Bytes

template <typename T_, typename Props_>
struct ZtFieldType_Bytes : public ZtFieldType_<Props_> {
  enum { Code = ZtFieldTC::Bytes };
  using T = T_;
  using Props = Props_;
  template <typename = ZtFmt::Default>
  using Print = ZtStruct_::Print::Bytes;
  inline static ZtVFieldType *vtype();
};
template <typename T, typename Props>
struct ZtFieldType<ZtFieldTC::Bytes, T, Props> :
    public ZtFieldType_Bytes<T, Props> { };

template <typename T, typename Props>
struct ZtVFieldType_Bytes : public ZtVFieldType {
  ZtVFieldType_Bytes() : ZtVFieldType{
    .code = ZtFieldTC::Bytes,
    .props = ZtVFieldProp::Value<Props>{},
    .info = {.null = nullptr}
  } { }
};
template <typename T, typename Props>
ZtVFieldType *ZtFieldType_Bytes<T, Props>::vtype() {
  return ZmSingleton<ZtVFieldType_Bytes<T, Props>>::instance();
}

inline ZuBSpan ZtField_Bytes_Def() { return {}; }
template <typename Base, typename = void>
struct ZtField_Bytes_Get {
  static ZtVFieldGet getFn() {
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
struct ZtField_Bytes_Get<Base,
    decltype(&Base::get(ZuDeclVal<const typename Base::O &>()), void())> {
  static ZtVFieldGet getFn() {
    using O = typename Base::O;
    // field get() returns a crvalue
    return {.get_ = {.bytes = [](const void *o) -> ZuBSpan {
      return Base::get(*static_cast<const O *>(o));
    }}};
  }
};
template <
  typename Base,
  auto Def = ZtField_Bytes_Def,
  bool = Base::ReadOnly>
struct ZtField_Bytes :
    public ZtField_<Base>,
    public ZtField_Bytes_Get<Base> {
  template <template <typename> class Override>
  using Adapt = ZtField_Bytes<Override<Base>>;
  using O = typename Base::O;
  using T = typename Base::T;
  using Props = typename Base::Props;
  using Type = ZtFieldType_Bytes<T, ZuTypeGrep<ZtFieldType_Props, Props>>;
  enum { Code = Type::Code };
  static ZtVFieldSet setFn() {
    return {.set_ = {.bytes = [](void *, ZuBSpan) { }}};
  }
  static ZuBSpan deflt() { return Def(); }
  static ZtVFieldGet constantFn() {
    using namespace ZtVFieldConstant;
    return {.get_ = {.bytes = [](const void *o) -> ZuBSpan {
      switch (int(reinterpret_cast<uintptr_t>(o))) {
	case Deflt: return Def();
	default:    return {};
      }
    }}};
  }
};
template <typename Base, auto Def>
struct ZtField_Bytes<Base, Def, false> :
    public ZtField_Bytes<Base, Def, true> {
  using O = typename Base::O;
  static ZtVFieldSet setFn() {
    return {.set_ = {.bytes = [](void *o, ZuBSpan v) {
      Base::set(*static_cast<O *>(o), v);
    }}};
  }
};

// --- Bool

template <typename T_, typename Props_>
struct ZtFieldType_Bool : public ZtFieldType_<Props_> {
  enum { Code = ZtFieldTC::Bool };
  using T = T_;
  using Props = Props_;
  template <typename = ZtFmt::Default> struct Print {
    bool v;
    template <typename S>
    friend inline decltype(auto) operator <<(S &s, const Print &print) {
      return s << (print.v ? '1' : '0');
    }
  };
  inline static ZtVFieldType *vtype();
};
template <typename T, typename Props>
struct ZtFieldType<ZtFieldTC::Bool, T, Props> :
    public ZtFieldType_Bool<T, Props> { };

template <typename T, typename Props>
struct ZtVFieldType_Bool : public ZtVFieldType {
  ZtVFieldType_Bool() : ZtVFieldType{
    .code = ZtFieldTC::Bool,
    .props = ZtVFieldProp::Value<Props>{},
    .info = {.null = nullptr}
  } { }
};
template <typename T, typename Props>
ZtVFieldType *ZtFieldType_Bool<T, Props>::vtype() {
  return ZmSingleton<ZtVFieldType_Bool<T, Props>>::instance();
}

constexpr bool ZtField_Bool_Def() { return false; }
template <
  typename Base,
  auto Def = ZtField_Bool_Def,
  bool = Base::ReadOnly>
struct ZtField_Bool : public ZtField_<Base> {
  template <template <typename> class Override>
  using Adapt = ZtField_Bool<Override<Base>>;
  using O = typename Base::O;
  using T = typename Base::T;
  using Props = typename Base::Props;
  using Type = ZtFieldType_Bool<T, ZuTypeGrep<ZtFieldType_Props, Props>>;
  enum { Code = Type::Code };
  static ZtVFieldGet getFn() {
    return {.get_ = {.bool_ = [](const void *o) -> bool {
      return Base::get(*static_cast<const O *>(o));
    }}};
  }
  static ZtVFieldSet setFn() {
    return {.set_ = {.bool_ = [](void *, bool) { }}};
  }
  static constexpr auto deflt() { return Def(); }
  static ZtVFieldGet constantFn() {
    using namespace ZtVFieldConstant;
    return {.get_ = {.bool_ = [](const void *o) -> bool {
      switch (int(reinterpret_cast<uintptr_t>(o))) {
	case Deflt:   return Def();
	case Minimum: return false;
	case Maximum: return true;
	default:      return false;
      }
    }}};
  }
};
template <typename Base, auto Def>
struct ZtField_Bool<Base, Def, false> :
    public ZtField_Bool<Base, Def, true> {
  using O = typename Base::O;
  static ZtVFieldSet setFn() {
    return {.set_ = {.bool_ = [](void *o, bool v) {
      Base::set(*static_cast<O *>(o), v);
    }}};
  }
};

// --- {Int,UInt}{8,16,32,64,128}

template <typename Props, typename Fmt, typename B>
struct ZtFieldPrintInt {
  B	value;

  template <typename S>
  friend inline decltype(auto) operator <<(S &s, const ZtFieldPrintInt &v) {
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
inline bool ZtFieldLimit(T &v) {
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
struct ZtFieldScanInt {
  B	value;

  using Result = ZuTuple<int, B>;

  ZtFieldScanInt() = default;
  ZtFieldScanInt(ZuCSpan s) { scan(s); }

  int scan(ZuCSpan s) {
    auto r = eov(s);
    value = r.template p<1>();
    return r.template p<0>();
  }
  static Result eov(ZuCSpan s) {
    using namespace ZuFieldProp;
    if constexpr (ZuFieldProp::HasEnum<Props>{}) {
      using Map = ZuFieldProp::GetEnum<Props>;
      auto i = Map::s2v(s);
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
      return validate(B::eov(s));
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
struct ZtFieldRange {
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

#define ZtField_Int(Code_, Type_) \
template <typename T_, typename Props_> \
struct ZtFieldType_##Code_ : public ZtFieldType_<Props_> { \
  enum { Code = ZtFieldTC::Code_ }; \
  using T = T_; \
  using Props = Props_; \
  template <typename Fmt = ZtFmt::Default> struct Print { \
    ZuBox<T> v; \
    template <typename S> \
    friend inline decltype(auto) operator <<(S &s, const Print &print) { \
      return s << ZtFieldPrintInt<Props, Fmt, ZuBox<T>>(print.v); \
    } \
  }; \
  inline static ZtVFieldType *vtype(); \
}; \
template <typename T, typename Props> \
struct ZtFieldType<ZtFieldTC::Code_, T, Props> : \
    public ZtFieldType_##Code_<T, Props> { }; \
 \
template < \
  typename T, typename Props, \
  bool = ZuFieldProp::HasEnum<Props>{}, \
  bool = ZuFieldProp::HasFlags<Props>{}> \
struct ZtVFieldType_##Code_; \
template <typename T, typename Props> \
struct ZtVFieldType_##Code_<T, Props, false, false> : public ZtVFieldType { \
  ZtVFieldType_##Code_() : ZtVFieldType{ \
    .code = ZtFieldTC::Code_, \
    .props = ZtVFieldProp::Value<Props>{}, \
    .info = {.null = nullptr} \
  } { } \
}; \
template <typename T, typename Props> \
struct ZtVFieldType_##Code_<T, Props, true, false> : public ZtVFieldType { \
  ZtVFieldType_##Code_() : ZtVFieldType{ \
    .code = ZtFieldTC::Code_, \
    .props = ZtVFieldProp::Value<Props>{}, \
    .info = {.enum_ = []() -> ZtVFieldEnum * { \
      return ZtVFieldEnum_<ZuFieldProp::GetEnum<Props>>::instance(); \
    }} \
  } { } \
}; \
template <typename T, typename Props> \
struct ZtVFieldType_##Code_<T, Props, false, true> : public ZtVFieldType { \
  ZtVFieldType_##Code_() : ZtVFieldType{ \
    .code = ZtFieldTC::Code_, \
    .props = ZtVFieldProp::Value<Props>{}, \
    .info = {.flags = []() -> ZtVFieldFlags * { \
      return ZtVFieldFlags_<ZuFieldProp::GetFlags<Props>>::instance(); \
    }} \
  } { } \
}; \
template <typename T, typename Props> \
ZtVFieldType *ZtFieldType_##Code_<T, Props>::vtype() { \
  return ZmSingleton<ZtVFieldType_##Code_<T, Props>>::instance(); \
} \
 \
template <typename T> \
struct ZtFieldType_##Code_##_Def { \
  static constexpr auto deflt() { return ZuCmp<T>::null(); } \
  static constexpr auto minimum() { return ZuCmp<T>::minimum(); } \
  static constexpr auto maximum() { return ZuCmp<T>::maximum(); } \
}; \
template < \
  typename Base, \
  auto Def = ZtFieldType_##Code_##_Def<typename Base::T>::deflt, \
  bool = Base::ReadOnly> \
struct ZtField_##Code_ : public ZtField_<Base> { \
  template <template <typename> class Override> \
  using Adapt = ZtField_##Code_<Override<Base>>; \
  using O = typename Base::O; \
  using T = typename Base::T; \
  using Props = typename Base::Props; \
  using Range = ZtFieldRange<T, Props, \
    ZtFieldType_##Code_##_Def<T>::minimum, \
    ZtFieldType_##Code_##_Def<T>::maximum>; \
  using Type = ZtFieldType_##Code_<T, ZuTypeGrep<ZtFieldType_Props, Props>>; \
  enum { Code = Type::Code }; \
  static ZtVFieldGet getFn() { \
    return {.get_ = {.Type_= [](const void *o) -> Type_##_t { \
      return Base::get(*static_cast<const O *>(o)); \
    }}}; \
  } \
  static ZtVFieldSet setFn() { \
    return {.set_ = {.Type_= [](void *, Type_##_t) { }}}; \
  } \
  static constexpr auto deflt() { return Def(); } \
  static constexpr auto minimum() { return Range::minimum(); } \
  static constexpr auto maximum() { return Range::maximum(); } \
  static ZtVFieldGet constantFn() { \
    using namespace ZtVFieldConstant; \
    return {.get_ = {.Type_= [](const void *o) -> Type_##_t { \
      switch (int(reinterpret_cast<uintptr_t>(o))) { \
	case Deflt:   return Def(); \
	case Minimum: return minimum(); \
	case Maximum: return maximum(); \
	default:      return ZuCmp<Type_##_t>::null(); \
      } \
    }}}; \
  } \
}; \
template <typename Base, auto Def> \
struct ZtField_##Code_<Base, Def, false> : \
    public ZtField_##Code_<Base, Def, true> { \
  using O = typename Base::O; \
  using T = typename Base::T; \
  static ZtVFieldSet setFn() { \
    return {.set_ = {.Type_= [](void *o, Type_##_t v) { \
      Base::set(*static_cast<O *>(o), v); \
    }}}; \
  } \
};

ZtField_Int(Int8, int8);
ZtField_Int(UInt8, uint8);
ZtField_Int(Int16, int16);
ZtField_Int(UInt16, uint16);
ZtField_Int(Int32, int32);
ZtField_Int(UInt32, uint32);
ZtField_Int(Int64, int64);
ZtField_Int(UInt64, uint64);
ZtField_Int(Int128, int128);
ZtField_Int(UInt128, uint128);

// --- Float

template <typename T_, typename Props_>
struct ZtFieldType_Float : public ZtFieldType_<Props_> {
  enum { Code = ZtFieldTC::Float };
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
  inline static ZtVFieldType *vtype();
};
template <typename T, typename Props>
struct ZtFieldType<ZtFieldTC::Float, T, Props> :
    public ZtFieldType_Float<T, Props> { };

template <typename T, typename Props>
struct ZtVFieldType_Float : public ZtVFieldType {
  ZtVFieldType_Float() : ZtVFieldType{
    .code = ZtFieldTC::Float,
    .props = ZtVFieldProp::Value<Props>{},
    .info = {.null = nullptr}
  } { }
};
template <typename T, typename Props>
ZtVFieldType *ZtFieldType_Float<T, Props>::vtype() {
  return ZmSingleton<ZtVFieldType_Float<T, Props>>::instance();
}

template <typename T>
struct ZtField_Float_Def {
  static constexpr auto deflt() { return ZuCmp<T>::null(); }
  static constexpr auto minimum() { return T{-ZuFP<ZuUnder<T>>::inf()}; }
  static constexpr auto maximum() { return T{ZuFP<ZuUnder<T>>::inf()}; }
};
template <
  typename Base,
  auto Def = ZtField_Float_Def<typename Base::T>::deflt,
  bool = Base::ReadOnly>
struct ZtField_Float : public ZtField_<Base> {
  template <template <typename> class Override>
  using Adapt = ZtField_Float<Override<Base>>;
  using O = typename Base::O;
  using T = typename Base::T;
  using Props = typename Base::Props;
  using Range = ZtFieldRange<T, Props,
    ZtField_Float_Def<T>::minimum, ZtField_Float_Def<T>::maximum>;
  using Type = ZtFieldType_Float<T, ZuTypeGrep<ZtFieldType_Props, Props>>;
  enum { Code = Type::Code };
  static ZtVFieldGet getFn() {
    return {.get_ = {.float_ = [](const void *o) -> double {
      return Base::get(*static_cast<const O *>(o));
    }}};
  }
  static ZtVFieldSet setFn() {
    return {.set_ = {.float_ = [](void *, double) { }}};
  }
  static constexpr auto deflt() { return Def(); }
  static constexpr auto minimum() { return Range::minimum(); }
  static constexpr auto maximum() { return Range::maximum(); }
  static ZtVFieldGet constantFn() {
    using namespace ZtVFieldConstant;
    return {.get_ = {.float_ = [](const void *o) -> double {
      switch (int(reinterpret_cast<uintptr_t>(o))) {
	case Deflt:   return Def();
	case Minimum: return minimum();
	case Maximum: return maximum();
	default:      return ZuCmp<double>::null();
      }
    }}};
  }
};
template <typename Base, auto Def>
struct ZtField_Float<Base, Def, false> :
    public ZtField_Float<Base, Def, true> {
  using O = typename Base::O;
  using T = typename Base::T;
  static ZtVFieldSet setFn() {
    return {.set_ = {.float_ = [](void *o, double v) {
      Base::set(*static_cast<O *>(o), v);
    }}};
  }
};

// --- Fixed

template <typename T_, typename Props_>
struct ZtFieldType_Fixed : public ZtFieldType_<Props_> {
  enum { Code = ZtFieldTC::Fixed };
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
  inline static ZtVFieldType *vtype();
};
template <typename T, typename Props>
struct ZtFieldType<ZtFieldTC::Fixed, T, Props> :
    public ZtFieldType_Fixed<T, Props> { };

template <typename T, typename Props>
struct ZtVFieldType_Fixed : public ZtVFieldType {
  ZtVFieldType_Fixed() : ZtVFieldType{
    .code = ZtFieldTC::Fixed,
    .props = ZtVFieldProp::Value<Props>{},
    .info = {.null = nullptr}
  } { }
};
template <typename T, typename Props>
ZtVFieldType *ZtFieldType_Fixed<T, Props>::vtype() {
  return ZmSingleton<ZtVFieldType_Fixed<T, Props>>::instance();
}

struct ZtField_Fixed_Def {
  static constexpr ZuFixed deflt() { return {}; }
  static constexpr ZuFixed minimum() { return {ZuFixedMin, 0}; }
  static constexpr ZuFixed maximum() { return {ZuFixedMax, 0}; }
};
template <
  typename Base,
  auto Def = ZtField_Fixed_Def::deflt,
  bool = Base::ReadOnly>
struct ZtField_Fixed : public ZtField_<Base> {
  template <template <typename> class Override>
  using Adapt = ZtField_Fixed<Override<Base>>;
  using O = typename Base::O;
  using T = typename Base::T;
  using Props = typename Base::Props;
  using Range = ZtFieldRange<T, Props,
    ZtField_Fixed_Def::minimum, ZtField_Fixed_Def::maximum>;
  using Type = ZtFieldType_Fixed<T, ZuTypeGrep<ZtFieldType_Props, Props>>;
  enum { Code = Type::Code };
  static ZtVFieldGet getFn() {
    return {.get_ = {.fixed = [](const void *o) -> ZuFixed {
      return Base::get(*static_cast<const O *>(o));
    }}};
  }
  static ZtVFieldSet setFn() {
    return {.set_ = {.fixed = [](void *, ZuFixed) { }}};
  }
  static constexpr auto deflt() { return Def(); }
  static constexpr auto minimum() { return Range::minimum(); }
  static constexpr auto maximum() { return Range::maximum(); }
  static ZtVFieldGet constantFn() {
    using namespace ZtVFieldConstant;
    return {.get_ = {.fixed = [](const void *o) -> ZuFixed {
      switch (int(reinterpret_cast<uintptr_t>(o))) {
	case Deflt:   return Def();
	case Minimum: return minimum();
	case Maximum: return maximum();
	default:      return {};
      }
    }}};
  }
};
template <typename Base, auto Def>
struct ZtField_Fixed<Base, Def, false> :
    public ZtField_Fixed<Base, Def, true> {
  using O = typename Base::O;
  using T = typename Base::T;
  static ZtVFieldSet setFn() {
    return {.set_ = {.fixed = [](void *o, ZuFixed v) {
      Base::set(*static_cast<O *>(o), ZuMv(v));
    }}};
  }
};

// --- Decimal

template <typename T_, typename Props_>
struct ZtFieldType_Decimal : public ZtFieldType_<Props_> {
  enum { Code = ZtFieldTC::Decimal };
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
  inline static ZtVFieldType *vtype();
};
template <typename T, typename Props>
struct ZtFieldType<ZtFieldTC::Decimal, T, Props> :
    public ZtFieldType_Decimal<T, Props> { };

template <typename T, typename Props>
struct ZtVFieldType_Decimal : public ZtVFieldType {
  ZtVFieldType_Decimal() : ZtVFieldType{
    .code = ZtFieldTC::Decimal,
    .props = ZtVFieldProp::Value<Props>{},
    .info = {.null = nullptr}
  } { }
};
template <typename T, typename Props>
ZtVFieldType *ZtFieldType_Decimal<T, Props>::vtype() {
  return ZmSingleton<ZtVFieldType_Decimal<T, Props>>::instance();
}

struct ZtField_Decimal_Def {
  static constexpr ZuDecimal deflt() {
    return ZuCmp<ZuDecimal>::null();
  }
  static constexpr ZuDecimal minimum() {
    return {ZuDecimal::Unscaled{ZuDecimal::minimum()}};
  }
  static constexpr ZuDecimal maximum() {
    return {ZuDecimal::Unscaled{ZuDecimal::maximum()}};
  }
};
template <
  typename Base,
  auto Def = ZtField_Decimal_Def::deflt,
  bool = Base::ReadOnly>
struct ZtField_Decimal : public ZtField_<Base> {
  template <template <typename> class Override>
  using Adapt = ZtField_Decimal<Override<Base>>;
  using O = typename Base::O;
  using T = typename Base::T;
  using Props = typename Base::Props;
  using Range = ZtFieldRange<T, Props,
    ZtField_Decimal_Def::minimum, ZtField_Decimal_Def::maximum>;
  using Type = ZtFieldType_Decimal<T, ZuTypeGrep<ZtFieldType_Props, Props>>;
  enum { Code = Type::Code };
  static ZtVFieldGet getFn() {
    return {.get_ = {.decimal = [](const void *o) -> ZuDecimal {
      return Base::get(*static_cast<const O *>(o));
    }}};
  }
  static ZtVFieldSet setFn() {
    return {.set_ = {.decimal = [](void *, ZuDecimal) { }}};
  }
  static constexpr auto deflt() { return Def(); }
  static constexpr auto minimum() { return Range::minimum(); }
  static constexpr auto maximum() { return Range::maximum(); }
  static ZtVFieldGet constantFn() {
    using namespace ZtVFieldConstant;
    return {.get_ = {.decimal = [](const void *o) -> ZuDecimal {
      switch (int(reinterpret_cast<uintptr_t>(o))) {
	case Deflt:   return Def();
	case Minimum: return minimum();
	case Maximum: return maximum();
	default:      return {};
      }
    }}};
  }
};
template <typename Base, auto Def>
struct ZtField_Decimal<Base, Def, false> :
    public ZtField_Decimal<Base, Def, true> {
  using O = typename Base::O;
  static ZtVFieldSet setFn() {
    return {.set_ = {.decimal = [](void *o, ZuDecimal v) {
      Base::set(*static_cast<O *>(o), ZuMv(v));
    }}};
  }
};

// --- Time

template <typename T_, typename Props_>
struct ZtFieldType_Time : public ZtFieldType_<Props_> {
  enum { Code = ZtFieldTC::Time };
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
  inline static ZtVFieldType *vtype();
};
template <typename T, typename Props>
struct ZtFieldType<ZtFieldTC::Time, T, Props> :
    public ZtFieldType_Time<T, Props> { };

template <typename T, typename Props>
struct ZtVFieldType_Time : public ZtVFieldType {
  ZtVFieldType_Time() : ZtVFieldType{
    .code = ZtFieldTC::Time,
    .props = ZtVFieldProp::Value<Props>{},
    .info = {.null = nullptr}
  } { }
};
template <typename T, typename Props>
ZtVFieldType *ZtFieldType_Time<T, Props>::vtype() {
  return ZmSingleton<ZtVFieldType_Time<T, Props>>::instance();
}

constexpr ZuTime ZtField_Time_Def() { return {}; }
template <
  typename Base,
  auto Def = ZtField_Time_Def,
  bool = Base::ReadOnly>
struct ZtField_Time : public ZtField_<Base> {
  template <template <typename> class Override>
  using Adapt = ZtField_Time<Override<Base>>;
  using O = typename Base::O;
  using T = typename Base::T;
  using Props = typename Base::Props;
  using Type = ZtFieldType_Time<T, ZuTypeGrep<ZtFieldType_Props, Props>>;
  enum { Code = Type::Code };
  static ZtVFieldGet getFn() {
    return {.get_ = {.time = [](const void *o) -> ZuTime {
      return Base::get(*static_cast<const O *>(o));
    }}};
  }
  static ZtVFieldSet setFn() {
    return {.set_ = {.time = [](void *, ZuTime) { }}};
  }
  static constexpr auto deflt() { return Def(); }
  static ZtVFieldGet constantFn() {
    using namespace ZtVFieldConstant;
    return {.get_ = {.time = [](const void *o) -> ZuTime {
      switch (int(reinterpret_cast<uintptr_t>(o))) {
	case Deflt:   return Def();
	default:      return {};
      }
    }}};
  }
};
template <typename Base, auto Def>
struct ZtField_Time<Base, Def, false> :
    public ZtField_Time<Base, Def, true> {
  using O = typename Base::O;
  static ZtVFieldSet setFn() {
    return {.set_ = {.time = [](void *o, ZuTime v) {
      Base::set(*static_cast<O *>(o), ZuMv(v));
    }}};
  }
};

// --- DateTime

template <typename T_, typename Props_>
struct ZtFieldType_DateTime : public ZtFieldType_<Props_> {
  enum { Code = ZtFieldTC::DateTime };
  using T = T_;
  using Props = Props_;
  template <typename Fmt = ZtFmt::Default> struct Print {
    ZuDateTime v;
    template <typename S>
    friend inline decltype(auto) operator <<(S &s, const Print &print) {
      return s << print.v.fmt(Fmt::DatePrint_());
    }
  };
  inline static ZtVFieldType *vtype();
};
template <typename T, typename Props>
struct ZtFieldType<ZtFieldTC::DateTime, T, Props> :
    public ZtFieldType_DateTime<T, Props> { };

template <typename T, typename Props>
struct ZtVFieldType_DateTime : public ZtVFieldType {
  ZtVFieldType_DateTime() : ZtVFieldType{
    .code = ZtFieldTC::DateTime,
    .props = ZtVFieldProp::Value<Props>{},
    .info = {.null = nullptr}
  } { }
};
template <typename T, typename Props>
ZtVFieldType *ZtFieldType_DateTime<T, Props>::vtype() {
  return ZmSingleton<ZtVFieldType_DateTime<T, Props>>::instance();
}

constexpr ZuDateTime ZtField_DateTime_Def() { return {}; }
template <
  typename Base,
  auto Def = ZtField_DateTime_Def,
  bool = Base::ReadOnly>
struct ZtField_DateTime : public ZtField_<Base> {
  template <template <typename> class Override>
  using Adapt = ZtField_DateTime<Override<Base>>;
  using O = typename Base::O;
  using T = typename Base::T;
  using Props = typename Base::Props;
  using Type = ZtFieldType_DateTime<T, ZuTypeGrep<ZtFieldType_Props, Props>>;
  enum { Code = Type::Code };
  static ZtVFieldGet getFn() {
    return {.get_ = {.dateTime = [](const void *o) -> ZuDateTime {
      return Base::get(*static_cast<const O *>(o));
    }}};
  }
  static ZtVFieldSet setFn() {
    return {.set_ = {.dateTime = [](void *, ZuDateTime) { }}};
  }
  static constexpr auto deflt() { return Def(); }
  static ZtVFieldGet constantFn() {
    using namespace ZtVFieldConstant;
    return {.get_ = {.dateTime = [](const void *o) -> ZuDateTime {
      switch (int(reinterpret_cast<uintptr_t>(o))) {
	case Deflt:   return Def();
	default:      return {};
      }
    }}};
  }
};
template <typename Base, auto Def>
struct ZtField_DateTime<Base, Def, false> :
    public ZtField_DateTime<Base, Def, true> {
  using O = typename Base::O;
  static ZtVFieldSet setFn() {
    return {.set_ = {.dateTime = [](void *o, ZuDateTime v) {
      Base::set(*static_cast<O *>(o), ZuMv(v));
    }}};
  }
};

// --- UDT

template <typename T, typename Fmt, typename = void>
struct ZtFieldType_UDT_HasFmt : public ZuFalse { };
template <typename T, typename Fmt>
struct ZtFieldType_UDT_HasFmt<T, Fmt,
  decltype(ZuDeclVal<const T &>().template fmt<Fmt>(), void())> :
    public ZuTrue { };

template <typename T_, typename Props_>
struct ZtFieldType_UDT : public ZtFieldType_<Props_> {
  enum { Code = ZtFieldTC::UDT };
  using T = T_;
  using Props = Props_;
  template <typename Fmt = ZtFmt::Default> struct Print {
    const T &v;
    template <typename S, typename U = T>
    friend ZuIfT<ZtFieldType_UDT_HasFmt<U, Fmt>{}, S &>
    operator <<(S &s, const Print &print) {
      return s << print.v.template fmt<Fmt>();
    }
    template <typename S, typename U = T>
    friend ZuIfT<!ZtFieldType_UDT_HasFmt<U, Fmt>{}, S &>
    operator <<(S &s, const Print &print) {
      return s << print.v;
    }
  };
  inline static ZtVFieldType *vtype();
};
template <typename T, typename Props>
struct ZtFieldType<ZtFieldTC::UDT, T, Props> :
    public ZtFieldType_UDT<T, Props> { };

template <typename T, typename = void>
struct ZtVFieldType_UDT_Print {
  static auto printFn() {
    return [](const void *, ZuVStream &, const ZtVFmt &) { };
  }
};
template <typename T>
struct ZtVFieldType_UDT_Print<T,
  decltype((ZuDeclVal<ZuVStream &>() << ZuDeclVal<const T &>()), void())> {
  static auto printFn() {
    return [](const void *v, ZuVStream &s, const ZtVFmt &) {
      s << *reinterpret_cast<const T *>(v);
    };
  }
};
template <typename T, typename = void>
struct ZtVFieldType_UDT_Scan {
  static auto scanFn() {
    return [](
      void (*)(void *, const void *), void *,
      ZuCSpan, const ZtVFmt &) { };
  }
};
template <typename T>
struct ZtVFieldType_UDT_Scan<
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
struct ZtVFieldType_UDT : public ZtVFieldType {
  ZtVFieldType_UDT() : ZtVFieldType{
    .code = ZtFieldTC::UDT,
    .props = ZtVFieldProp::Value<Props>{},
    .info = {.udt = []() -> ZtVFieldUDT * {
      static ZtVFieldUDT info{
	.id = ZtVFieldTypeID(static_cast<T *>(nullptr)),
	.info = &typeid(T),
	.print = ZtVFieldType_UDT_Print<T>::printFn(),
	.scan = ZtVFieldType_UDT_Scan<T>::scanFn()
      };
      return &info;
    }}
  } { }
};
template <typename T, typename Props>
ZtVFieldType *ZtFieldType_UDT<T, Props>::vtype() {
  return ZmSingleton<ZtVFieldType_UDT<T, Props>>::instance();
}

template <typename T, typename = void>
struct ZtField_UDT_Def {
  static constexpr void value() { }
};
template <typename T>
struct ZtField_UDT_Def<T, decltype(T{}, void())> {
  static constexpr T value() { return {}; }
};
template <typename T, typename = void>
struct ZtField_UDT_Null {
  static const void *value() { return nullptr; }
};
template <typename T>
struct ZtField_UDT_Null<T, decltype(T{}, void())> {
  static const void *value() {
    static T null_;
    return static_cast<const void *>(&null_);
  }
};
template <typename, auto, typename = void>
struct ZtField_UDT_Constant {
  static ZtVFieldGet constantFn() {
    using namespace ZtVFieldConstant;
    return {.get_ = {.udt = [](const void *o) -> const void * {
      return nullptr;
    }}};
  }
};
template <typename Base, auto Def>
struct ZtField_UDT_Constant<Base, Def,
    decltype(typename Base::T{Def()}, void())> {
  static ZtVFieldGet constantFn() {
    using T = typename Base::T;
    using namespace ZtVFieldConstant;
    return {.get_ = {.udt = [](const void *o) -> const void * {
      static T deflt_{Def()};
      switch (int(reinterpret_cast<uintptr_t>(o))) {
	case Deflt: return static_cast<const void *>(&deflt_);
	default:    return ZtField_UDT_Null<T>::value();
      }
    }}};
  }
};
template <typename Base, typename = void>
struct ZtField_UDT_Get {
  static ZtVFieldGet getFn() {
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
struct ZtField_UDT_Get<Base,
    decltype(&Base::get(ZuDeclVal<const typename Base::O &>()), void())> {
  static ZtVFieldGet getFn() {
    using O = typename Base::O;
    // field get() returns a crvalue
    return {.get_ = {.udt = [](const void *o) -> const void * {
      return static_cast<const void *>(&Base::get(*static_cast<const O *>(o)));
    }}};
  }
};
template <
  typename Base,
  auto Def = ZtField_UDT_Def<typename Base::T>::value,
  bool = Base::ReadOnly>
struct ZtField_UDT :
    public ZtField_<Base>,
    public ZtField_UDT_Constant<Base, Def>,
    public ZtField_UDT_Get<Base> {
  template <template <typename> class Override>
  using Adapt = ZtField_UDT<Override<Base>>;
  using O = typename Base::O;
  using T = typename Base::T;
  using Props = typename Base::Props;
  using Type = ZtFieldType_UDT<T, ZuTypeGrep<ZtFieldType_Props, Props>>;
  enum { Code = Type::Code };
  static ZtVFieldSet setFn() {
    return {.set_ = {.udt = [](void *, const void *) { }}};
  }
  static auto deflt() { return Def(); }
};
template <typename Base, auto Def>
struct ZtField_UDT<Base, Def, false> :
    public ZtField_UDT<Base, Def, true> {
  using O = typename Base::O;
  using T = typename Base::T;
  static ZtVFieldSet setFn() {
    return {.set_ = {.udt = [](void *o, const void *p) {
      Base::set(*static_cast<O *>(o), *static_cast<const T *>(p));
    }}};
  }
};

// --- CStringVec

template <typename T_, typename Props_>
struct ZtFieldType_CStringVec : public ZtFieldType_<Props_> {
  enum { Code = ZtFieldTC::CStringVec };
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
	s << ZtStruct_::Print::CString{print.vec[i]};
      }
      return s << Fmt::VecSuffix();
    }
  };
  inline static ZtVFieldType *vtype();
};
template <typename T, typename Props>
struct ZtFieldType<ZtFieldTC::CStringVec, T, Props> :
    public ZtFieldType_CStringVec<T, Props> { };

template <typename T, typename Props>
struct ZtVFieldType_CStringVec : public ZtVFieldType {
  ZtVFieldType_CStringVec() : ZtVFieldType{
    .code = ZtFieldTC::CStringVec,
    .props = ZtVFieldProp::Value<Props>{},
    .info = {.null = nullptr}
  } { }
};
template <typename T, typename Props>
ZtVFieldType *ZtFieldType_CStringVec<T, Props>::vtype() {
  return ZmSingleton<ZtVFieldType_CStringVec<T, Props>>::instance();
}

inline ZtStruct_::CStringVec ZtField_CStringVec_Def() { return {}; }
template <
  typename Base,
  auto Def = ZtField_CStringVec_Def,
  bool = Base::ReadOnly>
struct ZtField_CStringVec : public ZtField_<Base> {
  template <template <typename> class Override>
  using Adapt = ZtField_CStringVec<Override<Base>>;
  using O = typename Base::O;
  using T = typename Base::T;
  using Elem = typename ZuTraits<T>::Elem;
  using Props = typename Base::Props;
  using Type = ZtFieldType_CStringVec<T, ZuTypeGrep<ZtFieldType_Props, Props>>;
  using CStringVec = ZtStruct_::CStringVec;
  enum { Code = Type::Code };
  static ZtVFieldGet getFn() {
    return {.get_ = {.cstringVec = [](const void *o) -> CStringVec {
      return Base::get(*static_cast<const O *>(o));
    }}};
  }
  static ZtVFieldSet setFn() {
    return {.set_ = {.cstringVec = [](void *, CStringVec) { }}};
  }
  static constexpr auto deflt() { return Def(); }
  static ZtVFieldGet constantFn() {
    using namespace ZtVFieldConstant;
    return {.get_ = {.cstringVec = [](const void *o) -> CStringVec {
      switch (int(reinterpret_cast<uintptr_t>(o))) {
	case Deflt:   return Def();
	default:      return {};
      }
    }}};
  }
};
template <typename Base, auto Def>
struct ZtField_CStringVec<Base, Def, false> :
    public ZtField_CStringVec<Base, Def, true> {
  using O = typename Base::O;
  static ZtVFieldSet setFn() {
    using namespace ZtStruct_;
    return {.set_ = {.cstringVec = [](void *o, CStringVec v) {
      Base::set(*static_cast<O *>(o), ZuMv(v));
    }}};
  }
};

// --- StringVec

template <typename T_, typename Props_>
struct ZtFieldType_StringVec : public ZtFieldType_<Props_> {
  enum { Code = ZtFieldTC::StringVec };
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
	s << ZtStruct_::Print::String{print.vec[i]};
      }
      return s << Fmt::VecSuffix();
    }
  };
  inline static ZtVFieldType *vtype();
};
template <typename T, typename Props>
struct ZtFieldType<ZtFieldTC::StringVec, T, Props> :
    public ZtFieldType_StringVec<T, Props> { };

template <typename T, typename Props>
struct ZtVFieldType_StringVec : public ZtVFieldType {
  ZtVFieldType_StringVec() : ZtVFieldType{
    .code = ZtFieldTC::StringVec,
    .props = ZtVFieldProp::Value<Props>{},
    .info = {.null = nullptr}
  } { }
};
template <typename T, typename Props>
ZtVFieldType *ZtFieldType_StringVec<T, Props>::vtype() {
  return ZmSingleton<ZtVFieldType_StringVec<T, Props>>::instance();
}

inline ZtStruct_::StringVec ZtField_StringVec_Def() { return {}; }
template <
  typename Base,
  auto Def = ZtField_StringVec_Def,
  bool = Base::ReadOnly>
struct ZtField_StringVec : public ZtField_<Base> {
  template <template <typename> class Override>
  using Adapt = ZtField_StringVec<Override<Base>>;
  using O = typename Base::O;
  using T = typename Base::T;
  using Props = typename Base::Props;
  using Type = ZtFieldType_StringVec<T, ZuTypeGrep<ZtFieldType_Props, Props>>;
  using StringVec = ZtStruct_::StringVec;
  enum { Code = Type::Code };
  static ZtVFieldGet getFn() {
    return {.get_ = {.stringVec = [](const void *o) -> StringVec {
      return Base::get(*static_cast<const O *>(o));
    }}};
  }
  static ZtVFieldSet setFn() {
    using namespace ZtStruct_;
    return {.set_ = {.stringVec = [](void *, StringVec) { }}};
  }
  static constexpr auto deflt() { return Def(); }
  static ZtVFieldGet constantFn() {
    using namespace ZtVFieldConstant;
    return {.get_ = {.stringVec = [](const void *o) -> StringVec {
      switch (int(reinterpret_cast<uintptr_t>(o))) {
	case Deflt:   return Def();
	default:      return {};
      }
    }}};
  }
};
template <typename Base, auto Def>
struct ZtField_StringVec<Base, Def, false> :
    public ZtField_StringVec<Base, Def, true> {
  using O = typename Base::O;
  static ZtVFieldSet setFn() {
    using namespace ZtStruct_;
    return {.set_ = {.stringVec = [](void *o, StringVec v) {
      Base::set(*static_cast<O *>(o), ZuMv(v));
    }}};
  }
};

// --- BytesVec

template <typename T_, typename Props_>
struct ZtFieldType_BytesVec : public ZtFieldType_<Props_> {
  enum { Code = ZtFieldTC::BytesVec };
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
	s << ZtStruct_::Print::Bytes{print.vec[i]};
      }
      return s << Fmt::VecSuffix();
    }
  };
  inline static ZtVFieldType *vtype();
};
template <typename T, typename Props>
struct ZtFieldType<ZtFieldTC::BytesVec, T, Props> :
    public ZtFieldType_BytesVec<T, Props> { };

template <typename T, typename Props>
struct ZtVFieldType_BytesVec : public ZtVFieldType {
  ZtVFieldType_BytesVec() : ZtVFieldType{
    .code = ZtFieldTC::BytesVec,
    .props = ZtVFieldProp::Value<Props>{},
    .info = {.null = nullptr}
  } { }
};
template <typename T, typename Props>
ZtVFieldType *ZtFieldType_BytesVec<T, Props>::vtype() {
  return ZmSingleton<ZtVFieldType_BytesVec<T, Props>>::instance();
}

inline ZtStruct_::BytesVec ZtField_BytesVec_Def() { return {}; }
template <
  typename Base,
  auto Def = ZtField_BytesVec_Def,
  bool = Base::ReadOnly>
struct ZtField_BytesVec : public ZtField_<Base> {
  template <template <typename> class Override>
  using Adapt = ZtField_BytesVec<Override<Base>>;
  using O = typename Base::O;
  using T = typename Base::T;
  using Props = typename Base::Props;
  using Type = ZtFieldType_BytesVec<T, ZuTypeGrep<ZtFieldType_Props, Props>>;
  using BytesVec = ZtStruct_::BytesVec;
  enum { Code = Type::Code };
  static ZtVFieldGet getFn() {
    return {.get_ = {.bytesVec = [](const void *o) -> BytesVec {
      return Base::get(*static_cast<const O *>(o));
    }}};
  }
  static ZtVFieldSet setFn() {
    using namespace ZtStruct_;
    return {.set_ = {.bytesVec = [](void *, BytesVec) { }}};
  }
  static constexpr auto deflt() { return Def(); }
  static ZtVFieldGet constantFn() {
    using namespace ZtVFieldConstant;
    return {.get_ = {.bytesVec = [](const void *o) -> BytesVec {
      switch (int(reinterpret_cast<uintptr_t>(o))) {
	case Deflt:   return Def();
	default:      return {};
      }
    }}};
  }
};
template <typename Base, auto Def>
struct ZtField_BytesVec<Base, Def, false> :
    public ZtField_BytesVec<Base, Def, true> {
  using O = typename Base::O;
  static ZtVFieldSet setFn() {
    using namespace ZtStruct_;
    return {.set_ = {.bytesVec = [](void *o, BytesVec v) {
      Base::set(*static_cast<O *>(o), ZuMv(v));
    }}};
  }
};

// --- {Int,UInt}{8,16,32,64,128}Vec

#define ZtField_IntVec(Code_, Type_) \
template <typename T_, typename Props_> \
struct ZtFieldType_##Code_##Vec : public ZtFieldType_<Props_> { \
  enum { Code = ZtFieldTC::Code_##Vec }; \
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
	s << ZtFieldPrintInt<Props, Fmt, B>(print.vec[i]); \
      } \
      return s << Fmt::VecSuffix(); \
    } \
  }; \
  inline static ZtVFieldType *vtype(); \
}; \
template <typename T, typename Props> \
struct ZtFieldType<ZtFieldTC::Code_##Vec, T, Props> : \
    public ZtFieldType_##Code_##Vec<T, Props> { }; \
 \
template < \
  typename T, typename Props, \
  bool = ZuFieldProp::HasEnum<Props>{}, \
  bool = ZuFieldProp::HasFlags<Props>{}> \
struct ZtVFieldType_##Code_##Vec; \
template <typename T, typename Props> \
struct ZtVFieldType_##Code_##Vec<T, Props, false, false> : \
  public ZtVFieldType \
{ \
  ZtVFieldType_##Code_##Vec() : ZtVFieldType{ \
    .code = ZtFieldTC::Code_##Vec, \
    .props = ZtVFieldProp::Value<Props>{}, \
    .info = {.null = nullptr} \
  } { } \
}; \
template <typename T, typename Props> \
struct ZtVFieldType_##Code_##Vec<T, Props, true, false> : \
  public ZtVFieldType \
{ \
  ZtVFieldType_##Code_##Vec() : ZtVFieldType{ \
    .code = ZtFieldTC::Code_##Vec, \
    .props = ZtVFieldProp::Value<Props>{}, \
    .info = {.enum_ = []() -> ZtVFieldEnum * { \
      return ZtVFieldEnum_<ZuFieldProp::GetEnum<Props>>::instance(); \
    }} \
  } { } \
}; \
template <typename T, typename Props> \
struct ZtVFieldType_##Code_##Vec<T, Props, false, true> : \
  public ZtVFieldType \
{ \
  ZtVFieldType_##Code_##Vec() : ZtVFieldType{ \
    .code = ZtFieldTC::Code_##Vec, \
    .props = ZtVFieldProp::Value<Props>{}, \
    .info = {.flags = []() -> ZtVFieldFlags * { \
      return ZtVFieldFlags_<ZuFieldProp::GetFlags<Props>>::instance(); \
    }} \
  } { } \
}; \
template <typename T, typename Props> \
ZtVFieldType *ZtFieldType_##Code_##Vec<T, Props>::vtype() { \
  return ZmSingleton<ZtVFieldType_##Code_##Vec<T, Props>>::instance(); \
} \
 \
inline ZtStruct_::Code_##Vec ZtFieldType_##Code_##Vec_Def() { return {}; } \
template < \
  typename Base, \
  auto Def = ZtFieldType_##Code_##Vec_Def, \
  bool = Base::ReadOnly> \
struct ZtField_##Code_##Vec : public ZtField_<Base> { \
  template <template <typename> class Override> \
  using Adapt = ZtField_##Code_##Vec<Override<Base>>; \
  using O = typename Base::O; \
  using T = typename Base::T; \
  using Props = typename Base::Props; \
  using Type = \
    ZtFieldType_##Code_##Vec<T, ZuTypeGrep<ZtFieldType_Props, Props>>; \
  using Code_##Vec = ZtStruct_::Code_##Vec; \
  enum { Code = Type::Code }; \
  static ZtVFieldGet getFn() { \
    return {.get_ = {.Type_##Vec = [](const void *o) -> Code_##Vec { \
      return Base::get(*static_cast<const O *>(o)); \
    }}}; \
  } \
  static ZtVFieldSet setFn() { \
    using namespace ZtStruct_; \
    return {.set_ = {.Type_##Vec = [](void *, Code_##Vec) { }}}; \
  } \
  static constexpr auto deflt() { return Def(); } \
  static ZtVFieldGet constantFn() { \
    using namespace ZtVFieldConstant; \
    return {.get_ = {.Type_##Vec = [](const void *o) -> Code_##Vec { \
      switch (int(reinterpret_cast<uintptr_t>(o))) { \
	case Deflt:   return Def(); \
	default:      return {}; \
      } \
    }}}; \
  } \
}; \
template <typename Base, auto Def> \
struct ZtField_##Code_##Vec<Base, Def, false> : \
    public ZtField_##Code_##Vec<Base, Def, true> { \
  using O = typename Base::O; \
  static ZtVFieldSet setFn() { \
    using namespace ZtStruct_; \
    return {.set_ = {.Type_##Vec = [](void *o, Code_##Vec v) { \
      Base::set(*static_cast<O *>(o), ZuMv(v)); \
    }}}; \
  } \
};

ZtField_IntVec(Int8, int8);
ZtField_IntVec(UInt8, uint8);
ZtField_IntVec(Int16, int16);
ZtField_IntVec(UInt16, uint16);
ZtField_IntVec(Int32, int32);
ZtField_IntVec(UInt32, uint32);
ZtField_IntVec(Int64, int64);
ZtField_IntVec(UInt64, uint64);
ZtField_IntVec(Int128, int128);
ZtField_IntVec(UInt128, uint128);

// --- FloatVec

template <typename T_, typename Props_>
struct ZtFieldType_FloatVec : public ZtFieldType_<Props_> {
  enum { Code = ZtFieldTC::FloatVec };
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
	ZuBox<ZuFPType<sizeof(T)>> v = print.vec[i];
	if constexpr (ZuFieldProp::HasNDP<Props>{})
	  s << v.template fp<ZuFieldProp::GetNDP<Props>{}, '\0', Fmt>();
	else
	  s << v.template fmt<Fmt>();
      }
      return s << Fmt::VecSuffix();
    }
  };
  inline static ZtVFieldType *vtype();
};
template <typename T, typename Props>
struct ZtFieldType<ZtFieldTC::FloatVec, T, Props> :
    public ZtFieldType_FloatVec<T, Props> { };

template <typename T, typename Props>
struct ZtVFieldType_FloatVec : public ZtVFieldType {
  ZtVFieldType_FloatVec() : ZtVFieldType{
    .code = ZtFieldTC::FloatVec,
    .props = ZtVFieldProp::Value<Props>{},
    .info = {.null = nullptr}
  } { }
};
template <typename T, typename Props>
ZtVFieldType *ZtFieldType_FloatVec<T, Props>::vtype() {
  return ZmSingleton<ZtVFieldType_FloatVec<T, Props>>::instance();
}

inline ZtStruct_::FloatVec ZtField_FloatVec_Def() { return {}; }
template <
  typename Base,
  auto Def = ZtField_FloatVec_Def,
  bool = Base::ReadOnly>
struct ZtField_FloatVec : public ZtField_<Base> {
  template <template <typename> class Override>
  using Adapt = ZtField_FloatVec<Override<Base>>;
  using O = typename Base::O;
  using T = typename Base::T;
  using Props = typename Base::Props;
  using Type = ZtFieldType_FloatVec<T, ZuTypeGrep<ZtFieldType_Props, Props>>;
  using FloatVec = ZtStruct_::FloatVec;
  enum { Code = Type::Code };
  static ZtVFieldGet getFn() {
    return {.get_ = {.floatVec = [](const void *o) -> FloatVec {
      return Base::get(*static_cast<const O *>(o));
    }}};
  }
  static ZtVFieldSet setFn() {
    using namespace ZtStruct_;
    return {.set_ = {.floatVec = [](void *, FloatVec) { }}};
  }
  static constexpr auto deflt() { return Def(); }
  static ZtVFieldGet constantFn() {
    using namespace ZtVFieldConstant;
    return {.get_ = {.floatVec = [](const void *o) -> FloatVec {
      switch (int(reinterpret_cast<uintptr_t>(o))) {
	case Deflt:   return Def();
	default:      return {};
      }
    }}};
  }
};
template <typename Base, auto Def>
struct ZtField_FloatVec<Base, Def, false> :
    public ZtField_FloatVec<Base, Def, true> {
  using O = typename Base::O;
  static ZtVFieldSet setFn() {
    using namespace ZtStruct_;
    return {.set_ = {.floatVec = [](void *o, FloatVec v) {
      Base::set(*static_cast<O *>(o), ZuMv(v));
    }}};
  }
};

// --- FixedVec

template <typename T_, typename Props_>
struct ZtFieldType_FixedVec : public ZtFieldType_<Props_> {
  enum { Code = ZtFieldTC::FixedVec };
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
  inline static ZtVFieldType *vtype();
};
template <typename T, typename Props>
struct ZtFieldType<ZtFieldTC::FixedVec, T, Props> :
    public ZtFieldType_FixedVec<T, Props> { };

template <typename T, typename Props>
struct ZtVFieldType_FixedVec : public ZtVFieldType {
  ZtVFieldType_FixedVec() : ZtVFieldType{
    .code = ZtFieldTC::FixedVec,
    .props = ZtVFieldProp::Value<Props>{},
    .info = {.null = nullptr}
  } { }
};
template <typename T, typename Props>
ZtVFieldType *ZtFieldType_FixedVec<T, Props>::vtype() {
  return ZmSingleton<ZtVFieldType_FixedVec<T, Props>>::instance();
}

inline ZtStruct_::FixedVec ZtField_FixedVec_Def() { return {}; }
template <
  typename Base,
  auto Def = ZtField_FixedVec_Def,
  bool = Base::ReadOnly>
struct ZtField_FixedVec : public ZtField_<Base> {
  template <template <typename> class Override>
  using Adapt = ZtField_FixedVec<Override<Base>>;
  using O = typename Base::O;
  using T = typename Base::T;
  using Props = typename Base::Props;
  using Type = ZtFieldType_FixedVec<T, ZuTypeGrep<ZtFieldType_Props, Props>>;
  using FixedVec = ZtStruct_::FixedVec;
  enum { Code = Type::Code };
  static ZtVFieldGet getFn() {
    return {.get_ = {.fixedVec = [](const void *o) -> FixedVec {
      return Base::get(*static_cast<const O *>(o));
    }}};
  }
  static ZtVFieldSet setFn() {
    using namespace ZtStruct_;
    return {.set_ = {.fixedVec = [](void *, FixedVec) { }}};
  }
  static constexpr auto deflt() { return Def(); }
  static ZtVFieldGet constantFn() {
    using namespace ZtVFieldConstant;
    return {.get_ = {.fixedVec = [](const void *o) -> FixedVec {
      switch (int(reinterpret_cast<uintptr_t>(o))) {
	case Deflt:   return Def();
	default:      return {};
      }
    }}};
  }
};
template <typename Base, auto Def>
struct ZtField_FixedVec<Base, Def, false> :
    public ZtField_FixedVec<Base, Def, true> {
  using O = typename Base::O;
  static ZtVFieldSet setFn() {
    using namespace ZtStruct_;
    return {.set_ = {.fixedVec = [](void *o, FixedVec v) {
      Base::set(*static_cast<O *>(o), ZuMv(v));
    }}};
  }
};

// --- DecimalVec

template <typename T_, typename Props_>
struct ZtFieldType_DecimalVec : public ZtFieldType_<Props_> {
  enum { Code = ZtFieldTC::DecimalVec };
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
  inline static ZtVFieldType *vtype();
};
template <typename T, typename Props>
struct ZtFieldType<ZtFieldTC::DecimalVec, T, Props> :
    public ZtFieldType_DecimalVec<T, Props> { };

template <typename T, typename Props>
struct ZtVFieldType_DecimalVec : public ZtVFieldType {
  ZtVFieldType_DecimalVec() : ZtVFieldType{
    .code = ZtFieldTC::DecimalVec,
    .props = ZtVFieldProp::Value<Props>{},
    .info = {.null = nullptr}
  } { }
};
template <typename T, typename Props>
ZtVFieldType *ZtFieldType_DecimalVec<T, Props>::vtype() {
  return ZmSingleton<ZtVFieldType_DecimalVec<T, Props>>::instance();
}

inline ZtStruct_::DecimalVec ZtField_DecimalVec_Def() { return {}; }
template <
  typename Base,
  auto Def = ZtField_DecimalVec_Def,
  bool = Base::ReadOnly>
struct ZtField_DecimalVec : public ZtField_<Base> {
  template <template <typename> class Override>
  using Adapt = ZtField_DecimalVec<Override<Base>>;
  using O = typename Base::O;
  using T = typename Base::T;
  using Props = typename Base::Props;
  using Type = ZtFieldType_DecimalVec<T, ZuTypeGrep<ZtFieldType_Props, Props>>;
  using DecimalVec = ZtStruct_::DecimalVec;
  enum { Code = Type::Code };
  static ZtVFieldGet getFn() {
    return {.get_ = {.decimalVec = [](const void *o) -> DecimalVec {
      return Base::get(*static_cast<const O *>(o));
    }}};
  }
  static ZtVFieldSet setFn() {
    using namespace ZtStruct_;
    return {.set_ = {.decimalVec = [](void *, DecimalVec) { }}};
  }
  static constexpr auto deflt() { return Def(); }
  static ZtVFieldGet constantFn() {
    using namespace ZtVFieldConstant;
    return {.get_ = {.decimalVec = [](const void *o) -> DecimalVec {
      switch (int(reinterpret_cast<uintptr_t>(o))) {
	case Deflt:   return Def();
	default:      return {};
      }
    }}};
  }
};
template <typename Base, auto Def>
struct ZtField_DecimalVec<Base, Def, false> :
    public ZtField_DecimalVec<Base, Def, true> {
  using O = typename Base::O;
  static ZtVFieldSet setFn() {
    using namespace ZtStruct_;
    return {.set_ = {.decimalVec = [](void *o, DecimalVec v) {
      Base::set(*static_cast<O *>(o), ZuMv(v));
    }}};
  }
};

// --- TimeVec

template <typename T_, typename Props_>
struct ZtFieldType_TimeVec : public ZtFieldType_<Props_> {
  enum { Code = ZtFieldTC::TimeVec };
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
  inline static ZtVFieldType *vtype();
};
template <typename T, typename Props>
struct ZtFieldType<ZtFieldTC::TimeVec, T, Props> :
    public ZtFieldType_TimeVec<T, Props> { };

template <typename T, typename Props>
struct ZtVFieldType_TimeVec : public ZtVFieldType {
  ZtVFieldType_TimeVec() : ZtVFieldType{
    .code = ZtFieldTC::TimeVec,
    .props = ZtVFieldProp::Value<Props>{},
    .info = {.null = nullptr}
  } { }
};
template <typename T, typename Props>
ZtVFieldType *ZtFieldType_TimeVec<T, Props>::vtype() {
  return ZmSingleton<ZtVFieldType_TimeVec<T, Props>>::instance();
}

inline ZtStruct_::TimeVec ZtField_TimeVec_Def() { return {}; }
template <
  typename Base,
  auto Def = ZtField_TimeVec_Def,
  bool = Base::ReadOnly>
struct ZtField_TimeVec : public ZtField_<Base> {
  template <template <typename> class Override>
  using Adapt = ZtField_TimeVec<Override<Base>>;
  using O = typename Base::O;
  using T = typename Base::T;
  using Props = typename Base::Props;
  using Type = ZtFieldType_TimeVec<T, ZuTypeGrep<ZtFieldType_Props, Props>>;
  using TimeVec = ZtStruct_::TimeVec;
  enum { Code = Type::Code };
  static ZtVFieldGet getFn() {
    return {.get_ = {.timeVec = [](const void *o) -> TimeVec {
      return Base::get(*static_cast<const O *>(o));
    }}};
  }
  static ZtVFieldSet setFn() {
    using namespace ZtStruct_;
    return {.set_ = {.timeVec = [](void *, TimeVec) { }}};
  }
  static constexpr auto deflt() { return Def(); }
  static ZtVFieldGet constantFn() {
    using namespace ZtVFieldConstant;
    return {.get_ = {.timeVec = [](const void *o) -> TimeVec {
      switch (int(reinterpret_cast<uintptr_t>(o))) {
	case Deflt:   return Def();
	default:      return {};
      }
    }}};
  }
};
template <typename Base, auto Def>
struct ZtField_TimeVec<Base, Def, false> :
    public ZtField_TimeVec<Base, Def, true> {
  using O = typename Base::O;
  static ZtVFieldSet setFn() {
    using namespace ZtStruct_;
    return {.set_ = {.timeVec = [](void *o, TimeVec v) {
      Base::set(*static_cast<O *>(o), ZuMv(v));
    }}};
  }
};

// --- DateTimeVec

template <typename T_, typename Props_>
struct ZtFieldType_DateTimeVec : public ZtFieldType_<Props_> {
  enum { Code = ZtFieldTC::DateTimeVec };
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
  inline static ZtVFieldType *vtype();
};
template <typename T, typename Props>
struct ZtFieldType<ZtFieldTC::DateTimeVec, T, Props> :
    public ZtFieldType_DateTimeVec<T, Props> { };

template <typename T, typename Props>
struct ZtVFieldType_DateTimeVec : public ZtVFieldType {
  ZtVFieldType_DateTimeVec() : ZtVFieldType{
    .code = ZtFieldTC::DateTimeVec,
    .props = ZtVFieldProp::Value<Props>{},
    .info = {.null = nullptr}
  } { }
};
template <typename T, typename Props>
ZtVFieldType *ZtFieldType_DateTimeVec<T, Props>::vtype() {
  return ZmSingleton<ZtVFieldType_DateTimeVec<T, Props>>::instance();
}

inline ZtStruct_::DateTimeVec ZtField_DateTimeVec_Def() { return {}; }
template <
  typename Base,
  auto Def = ZtField_DateTimeVec_Def,
  bool = Base::ReadOnly>
struct ZtField_DateTimeVec : public ZtField_<Base> {
  template <template <typename> class Override>
  using Adapt = ZtField_DateTimeVec<Override<Base>>;
  using O = typename Base::O;
  using T = typename Base::T;
  using Props = typename Base::Props;
  using Type = ZtFieldType_DateTimeVec<T, ZuTypeGrep<ZtFieldType_Props, Props>>;
  using DateTimeVec = ZtStruct_::DateTimeVec;
  enum { Code = Type::Code };
  static ZtVFieldGet getFn() {
    return {.get_ = {.dateTimeVec = [](const void *o) -> DateTimeVec {
      return Base::get(*static_cast<const O *>(o));
    }}};
  }
  static ZtVFieldSet setFn() {
    using namespace ZtStruct_;
    return {.set_ = {.dateTimeVec = [](void *, DateTimeVec) { }}};
  }
  static constexpr auto deflt() { return Def(); }
  static ZtVFieldGet constantFn() {
    using namespace ZtVFieldConstant;
    return {.get_ = {.dateTimeVec = [](const void *o) -> DateTimeVec {
      switch (int(reinterpret_cast<uintptr_t>(o))) {
	case Deflt:   return Def();
	default:      return {};
      }
    }}};
  }
};
template <typename Base, auto Def>
struct ZtField_DateTimeVec<Base, Def, false> :
    public ZtField_DateTimeVec<Base, Def, true> {
  using O = typename Base::O;
  static ZtVFieldSet setFn() {
    using namespace ZtStruct_;
    return {.set_ = {.dateTimeVec = [](void *o, DateTimeVec v) {
      Base::set(*static_cast<O *>(o), ZuMv(v));
    }}};
  }
};

#define ZtField_BaseID__(ID, ...) ID
#define ZtField_BaseID_(Axor, ...) ZuPP_Defer(ZtField_BaseID__)Axor
#define ZtField_BaseID(Base) ZuPP_Defer(ZtField_BaseID_)Base

#define ZtField_TypeName_(Name, ...) Name
#define ZtField_TypeName(Type) ZuPP_Defer(ZtField_TypeName_)Type

#define ZtField_LambdaArg(Arg) []{ return Arg; }

#define ZtField_TypeArgs_CString(...) \
  ZuPP_MapComma(ZtField_LambdaArg, __VA_ARGS__)
#define ZtField_TypeArgs_String(...) \
  ZuPP_MapComma(ZtField_LambdaArg, __VA_ARGS__)
#define ZtField_TypeArgs_Bytes(...) \
  ZuPP_MapComma(ZtField_LambdaArg, __VA_ARGS__)
#define ZtField_TypeArgs_Bool(...) \
  ZuPP_MapComma(ZtField_LambdaArg, __VA_ARGS__)
#define ZtField_TypeArgs_Int8(...) \
  ZuPP_MapComma(ZtField_LambdaArg, __VA_ARGS__)
#define ZtField_TypeArgs_UInt8(...) \
  ZuPP_MapComma(ZtField_LambdaArg, __VA_ARGS__)
#define ZtField_TypeArgs_Int16(...) \
  ZuPP_MapComma(ZtField_LambdaArg, __VA_ARGS__)
#define ZtField_TypeArgs_UInt16(...) \
  ZuPP_MapComma(ZtField_LambdaArg, __VA_ARGS__)
#define ZtField_TypeArgs_Int32(...) \
  ZuPP_MapComma(ZtField_LambdaArg, __VA_ARGS__)
#define ZtField_TypeArgs_UInt32(...) \
  ZuPP_MapComma(ZtField_LambdaArg, __VA_ARGS__)
#define ZtField_TypeArgs_Int64(...) \
  ZuPP_MapComma(ZtField_LambdaArg, __VA_ARGS__)
#define ZtField_TypeArgs_UInt64(...) \
  ZuPP_MapComma(ZtField_LambdaArg, __VA_ARGS__)
#define ZtField_TypeArgs_Int128(...) \
  ZuPP_MapComma(ZtField_LambdaArg, __VA_ARGS__)
#define ZtField_TypeArgs_UInt128(...) \
  ZuPP_MapComma(ZtField_LambdaArg, __VA_ARGS__)
#define ZtField_TypeArgs_Float(...) \
  ZuPP_MapComma(ZtField_LambdaArg, __VA_ARGS__)
#define ZtField_TypeArgs_Fixed(...) \
  ZuPP_MapComma(ZtField_LambdaArg, __VA_ARGS__)
#define ZtField_TypeArgs_Decimal(...) \
  ZuPP_MapComma(ZtField_LambdaArg, __VA_ARGS__)
#define ZtField_TypeArgs_Time(...) \
  ZuPP_MapComma(ZtField_LambdaArg, __VA_ARGS__)
#define ZtField_TypeArgs_DateTime(...) \
  ZuPP_MapComma(ZtField_LambdaArg, __VA_ARGS__)
#define ZtField_TypeArgs_UDT(...) \
  ZuPP_MapComma(ZtField_LambdaArg, __VA_ARGS__)

#define ZtField_TypeArgs_CStringVec(...) \
  ZuPP_MapComma(ZtField_LambdaArg, __VA_ARGS__)
#define ZtField_TypeArgs_StringVec(...) \
  ZuPP_MapComma(ZtField_LambdaArg, __VA_ARGS__)
#define ZtField_TypeArgs_BytesVec(...) \
  ZuPP_MapComma(ZtField_LambdaArg, __VA_ARGS__)
#define ZtField_TypeArgs_Int8Vec(...) \
  ZuPP_MapComma(ZtField_LambdaArg, __VA_ARGS__)
#define ZtField_TypeArgs_UInt8Vec(...) \
  ZuPP_MapComma(ZtField_LambdaArg, __VA_ARGS__)
#define ZtField_TypeArgs_Int16Vec(...) \
  ZuPP_MapComma(ZtField_LambdaArg, __VA_ARGS__)
#define ZtField_TypeArgs_UInt16Vec(...) \
  ZuPP_MapComma(ZtField_LambdaArg, __VA_ARGS__)
#define ZtField_TypeArgs_Int32Vec(...) \
  ZuPP_MapComma(ZtField_LambdaArg, __VA_ARGS__)
#define ZtField_TypeArgs_UInt32Vec(...) \
  ZuPP_MapComma(ZtField_LambdaArg, __VA_ARGS__)
#define ZtField_TypeArgs_Int64Vec(...) \
  ZuPP_MapComma(ZtField_LambdaArg, __VA_ARGS__)
#define ZtField_TypeArgs_UInt64Vec(...) \
  ZuPP_MapComma(ZtField_LambdaArg, __VA_ARGS__)
#define ZtField_TypeArgs_Int128Vec(...) \
  ZuPP_MapComma(ZtField_LambdaArg, __VA_ARGS__)
#define ZtField_TypeArgs_UInt128Vec(...) \
  ZuPP_MapComma(ZtField_LambdaArg, __VA_ARGS__)
#define ZtField_TypeArgs_FloatVec(...) \
  ZuPP_MapComma(ZtField_LambdaArg, __VA_ARGS__)
#define ZtField_TypeArgs_FixedVec(...) \
  ZuPP_MapComma(ZtField_LambdaArg, __VA_ARGS__)
#define ZtField_TypeArgs_DecimalVec(...) \
  ZuPP_MapComma(ZtField_LambdaArg, __VA_ARGS__)
#define ZtField_TypeArgs_TimeVec(...) \
  ZuPP_MapComma(ZtField_LambdaArg, __VA_ARGS__)
#define ZtField_TypeArgs_DateTimeVec(...) \
  ZuPP_MapComma(ZtField_LambdaArg, __VA_ARGS__)

#define ZtField_TypeArgs_(Name, ...) \
  __VA_OPT__(, ZtField_TypeArgs_##Name(__VA_ARGS__))
#define ZtField_TypeArgs(Type) ZuPP_Defer(ZtField_TypeArgs_)Type

#define ZtField(O, ID) ZtField_##O##_##ID

// get field index within fields
#define ZtFieldIndex(O, ID) (ZuTypeIndex<ZtField(O, ID), ZuFields<O>>{})

// main structure declaration macros ZtStruct()
#define ZtField_Decl__(O, ID, Base, TypeName, Type) \
  ZuField_Decl(O, Base) \
  using ZtField(O, ID) = \
    ZtField_##TypeName<ZuField(O, ID) ZtField_TypeArgs(Type)>;
#define ZtField_Decl_(O, Base, Type) \
  ZuPP_Defer(ZtField_Decl__)(O, \
      ZuPP_Eval_(ZtField_BaseID(Base)), Base, \
      ZuPP_Eval_(ZtField_TypeName(Type)), Type)
#define ZtField_Decl(O, Args) ZuPP_Defer(ZtField_Decl_)(O, ZuPP_Strip(Args))

#define ZtField_Type_(O, Base, ...) \
  ZuPP_Defer(ZtField)(O, ZuPP_Eval_(ZtField_BaseID(Base)))
#define ZtField_Type(O, Args) ZuPP_Defer(ZtField_Type_)(O, ZuPP_Strip(Args))

#define ZtStruct_(O, Facets, ...) \
  __VA_OPT__(ZuPP_MapArg(ZtField_Decl, O, __VA_ARGS__)) \
  using ZuFields_##O = ZuTypeList< \
    __VA_OPT__(ZuPP_MapArgComma(ZtField_Type, O, __VA_ARGS__))>; \
  O ZuStructured_(O *); \
  ZuStruct_Render(O, Core ZuPP_StripAppend(Facets))

#define ZtStruct(O_Facets, ...) \
  ZuPP_Eval(ZuPP_Defer(ZtStruct_)( \
    ZuPP_Eval_(ZuStruct_Object(O_Facets)), \
    ZuPP_Eval_(ZuStruct_Facets(O_Facets)) \
    __VA_OPT__(, __VA_ARGS__)))

// render fields to a facet (ZtStruct layer)
// - optionally extends selected fields with additional properties
// - ZtStructRender(Object, Facet[, (FieldID[, Property...])...])
#define ZtField_RenderDecl__(O, ID, ...) \
  using Props_ = ZtField(O, ID)::Props; \
  using Props = typename Props_::template Push<ZuField_Props_(__VA_ARGS__)>;
#define ZtField_RenderDecl_(O, Facet, ID, ...) \
  struct ZtField(O##_##Facet, ID) : public ZtField(O, ID) { \
    __VA_OPT__(ZtField_RenderDecl__(O, ID, __VA_ARGS__)) \
  };
#define ZtField_RenderDecl(O_Facet, Args) \
  ZuPP_Defer(ZtField_RenderDecl_)(ZuPP_Strip(O_Facet), ZuPP_Strip(Args))

#define ZtField_RenderType_(O, Facet, ID, ...) ZtField(O##_##Facet, ID)
#define ZtField_RenderType(O_Facet, Args) \
  ZuPP_Defer(ZtField_RenderType_)(ZuPP_Strip(O_Facet), ZuPP_Strip(Args))

#define ZtStructRender(O, Facet, ...) \
  ZuPP_Eval(ZuPP_MapArg(ZtField_RenderDecl, (O, Facet), __VA_ARGS__)) \
  using ZtFields_##O##_##Facet = ZuTypeList< \
    ZuPP_Eval(ZuPP_MapArgComma(ZtField_RenderType, (O, Facet), __VA_ARGS__))>; \
  ZtFields_##O##_##Facet ZuFields_(O *, ZuFacet::Facet *)

template <typename Field>
struct ZtFieldPrint_ {
  using O = typename Field::O;
  using Print = typename Field::Type::template Print<ZtFmt::Default>;
  const O &o;
  template <typename S>
  friend inline decltype(auto) operator <<(S &s, const ZtFieldPrint_ &print) {
    return s << Field::id() << ':' << Print{Field::get(print.o)};
  }
};

struct ZtStructPrint : public ZuPrintDelegate {
  template <typename U>
  using Print_Filter =
    ZuBool<!ZuTypeIn<ZuFieldProp::Hidden, typename U::Props>{}>;
  template <typename S, typename O>
  static void print(S &s, const O &o) {
    using Fields = ZuTypeGrep<Print_Filter, ZuFields<O>>;
    s << '{';
    ZuUnroll::all<Fields>([&s, &o]<typename Field>() {
      if constexpr (ZuTypeIndex<Field, Fields>{}) s << ',';
      s << ZtFieldPrint_<Field>{o};
    });
    s << '}';
  }
};

// run-time fields

using ZtVFieldArray = ZuSpan<const ZtVField *>;

template <typename VField, typename ...Fields>
struct ZtVFieldFactory {
  enum { N = sizeof...(Fields) };

  ZtVFieldArray	fields;

  static ZtVFieldFactory *instance() {
    return ZmSingleton<ZtVFieldFactory>::instance();
  }

  ZtVFieldFactory() {
    static const VField fields_[N] =
      // std::initializer_list<ZtVField>
    {
      VField{Fields{}}...
    };
    static const ZtVField *ptr_[N];
    ZuUnroll::all<N>([](auto i) {
      ptr_[i] = static_cast<const ZtVField *>(&fields_[i]);
    });
    fields = {&ptr_[0], N};
  }
};
template <typename Fields, typename VField = ZtVField>
inline ZtVFieldArray ZtVFields_() {
  using Factory = ZuTypeApply<
    ZtVFieldFactory, typename Fields::template Unshift<VField>>;
  return Factory::instance()->fields;
}
template <typename O, typename VField = ZtVField>
inline ZtVFieldArray ZtVFields() {
  return ZtVFields_<ZuFields<O>, VField>();
}

// run-time keys
// - each key is a ZuStructKeyT<O, KeyID>, i.e. a value tuple of a
//   subset of the values in the object itself, used to identify the
//   object as a primary or secondary key
// - each key tuple has its own field array, which is extracted and
//   transformed from the underlying object field array

using ZtVKeyFieldArray = ZuSpan<const ZtVFieldArray>;

template <typename O, typename VField>
struct ZtVKeyFields_ {
  ZtVKeyFieldArray	keys;

  static ZtVKeyFields_ *instance() {
    return ZmSingleton<ZtVKeyFields_>::instance();
  }

  ZtVKeyFields_() {
    using KeyIDs = ZuStructKeyIDs<O>;
    static ZtVFieldArray data_[KeyIDs::N];
    ZuUnroll::all<KeyIDs>([](auto i) {
      data_[i] = ZtVFields<ZuStructKeyT<O, i>, VField>();
    });
    keys = {&data_[0], KeyIDs::N};
  }
};
template <typename O, typename VField = ZtVField>
inline ZtVKeyFieldArray ZtVKeyFields() {
  return ZtVKeyFields_<O, VField>::instance()->keys;
}

// standardized field filters
// - Load - fields needed for constructing a new object (all non-read-only)
// - Ctor - fields passed to constructor (may include read-only fields)
// - Init - fields initialized post-construction
// - Save - fields needed to fully persist an object
// - Upd - mutable fields that may be present in an update, and the primary key
// - Del - the primary key

// ZtFieldFiltered<ZtFieldFilter::Load, Fields>;
namespace ZtFieldFilter {
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
  using Del = ZuFieldProp::Key<typename Field::Props, 0>;
}

#endif /* ZtStruct_HH */
