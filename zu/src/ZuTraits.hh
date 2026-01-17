//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// type traits and array type normalization

// cv qualifiers are stripped:
// ZuTraits<T> is equivalent to ZuTraits<ZuStrip<T>>
//
// Trait		Description
// -----		-----------
// IsArray		array
// IsSpan		span (contiguous in memory) (implies IsArray)
//
// IsComposite		class/struct/union
// IsEmpty		empty composite type
// IsEnum		enum
// IsPOD		plain ole' data
//
// IsReference		reference
// IsRValueRef		rvalue reference
// IsPointer		pointer
// IsPrimitive		primitive
// IsReal		real (integer, floating point, fixed point, etc.)
// IsSigned		signed
// IsIntegral		integer
// IsFloatingPoint	floating point
//
// IsString		string (possibly wide)
// IsCString		C string (i.e. null-terminated)
// IsWString		wide string (using wchar_t)
// IsVoid		is void (convenience trait for ZuIsSame<void, T>{})
// IsBool		is bool (convenience trait for ZuIsSame<bool, T>{})
// 
// Array Type Normalization
// ------------------------
// Elem				type of element, if IsArray (void if not)
// Elem *data(T &)		pointer to start of array data, if IsSpan
// const Elem *data(const T &)	const pointer to start of array data, if IsSpan
// length(const T &)		length of array, if IsArray

// class UDT {
//   ...
//   int cmp(const UDT &t) { ... }
//   // returns -ve if *this < t, 0 if *this == t, +ve if *this > t
//   ...
//   struct Traits : public ZuBaseTraits<UDT> {
//     enum { IsPOD = 1 }; // overrides
//   };
//   friend Traits ZuTraitsType(UDT *);
// };

#ifndef ZuTraits_HH
#define ZuTraits_HH

#ifndef ZuLib_HH
#include <zlib/ZuLib.hh>
#endif

#include <wchar.h>
#include <string.h>
#include <limits.h>
#include <string.h>

#include <zlib/ZuFP_.hh>

// generic traits (overridden by specializations)

template <typename T, typename = void>
struct ZuTraits_Composite : public ZuFalse { };
template <typename T>
struct ZuTraits_Composite<T, decltype((int T::*){}, void())> :
  public ZuTrue { };

template <typename T, typename = void>
struct ZuTraits_Empty : public ZuFalse { };
template <typename T>
struct ZuTraits_Empty<T, decltype(sizeof(T), (int T::*){}, void())> :
  public ZuBool<__is_empty(T)> { };

template <typename T>
struct ZuTraits_Enum : public ZuBool<__is_enum(T)> { };

template <typename U, typename = void>
struct ZuTraits_POD : public ZuBool<!ZuTraits_Composite<U>{}> { };
template <typename U>
struct ZuTraits_POD<U, decltype(sizeof(U), void())> :
#ifdef _MSC_VER
  public ZuBool<__is_pod(U)>
#else
  public ZuBool<__is_standard_layout(U) && __is_trivial(U)>
#endif
{ };

// an iterable array with a non-pointer as an iterator is not a span
template <typename, typename Iter>
struct ZuBaseTraits_Array_4 {
  enum { IsSpan = 0 };
  using Elem = ZuDecay<decltype(*ZuDeclVal<const Iter &>())>;
};
// an iterable array with a pointer as an iterator is a span
template <typename U, typename Elem_>
struct ZuBaseTraits_Array_4<U, Elem_ *> {
  enum { IsSpan = 1 };
  using Elem = Elem_;
  static auto data(U &v) { return &v[0]; }
  static auto data(const U &v) { return &v[0]; }
};
// non-iterable types are discriminated as non-arrays
template <typename U, typename = void>
struct ZuBaseTraits_Array_3 {
  enum { IsArray = 0, IsSpan = 0 };
  using Elem = void;
};
// STL iterable arrays
template <typename U>
struct ZuBaseTraits_Array_3<U, decltype(
  ZuDeclVal<const U &>().end() - ZuDeclVal<const U &>().begin(), void())> :
    public ZuBaseTraits_Array_4<U, decltype(ZuDeclVal<const U &>().begin())> {
  enum { IsArray = 1 };
  using Elem = ZuDecay<decltype(*(ZuDeclVal<const U &>().begin()))>;
  static constexpr unsigned length(const U &v) { return v.end() - v.begin(); }
};
// non-composite arrays are primitive arrays, handled explicitly below
template <typename U, bool = ZuTraits_Composite<U>{}>
struct ZuBaseTraits_Array_2 { };
template <typename U>
struct ZuBaseTraits_Array_2<U, true> : public ZuBaseTraits_Array_3<U> { };
// type that cannot perform an array operation
template <typename U, typename = void>
struct ZuBaseTraits_Array_ {
  enum { IsArray = 0, IsSpan = 0 };
  using Elem = void;
};
// test for array operator
template <typename U>
struct ZuBaseTraits_Array_<U,
  decltype(ZuDeclVal<const ZuDecay<U> &>()[0], void())> :
    public ZuBaseTraits_Array_2<ZuDecay<U>> { };
// test for pointer
template <typename U>
struct ZuBaseTraits_Array : public ZuBaseTraits_Array_<U> { };
// pointers are not arrays (for the purposes of traits)
template <typename Elem_>
struct ZuBaseTraits_Array<Elem_ *> {
  enum { IsArray = 0, IsSpan = 0 };
  using Elem = Elem_;
};

// default generic traits

template <typename T> struct ZuBaseTraits : public ZuBaseTraits_Array<T> {
  enum { IsComposite = ZuTraits_Composite<T>{} };
  enum { IsEmpty = ZuTraits_Empty<T>{} };
  enum { IsEnum = ZuTraits_Enum<T>{} };
  enum { IsPOD = ZuTraits_POD<T>{} };
  enum {
    IsReference	= 0,		IsRValueRef	= 0,
    IsPointer	= 0,
    IsPrimitive	= IsEnum,
    IsReal	= IsEnum,
    IsSigned	= IsEnum,
    IsIntegral	= IsEnum,	IsFloatingPoint	= 0,
    IsString	= 0,		IsCString	= 0,	IsWString	= 0,
    IsVoid	= 0,		IsBool		= 0
  };
};

void ZuTraitsType(...);
template <typename T_, typename Traits>
struct ZuDefaultTraits_ { using T = Traits; };
template <typename T_>
struct ZuDefaultTraits_<T_, void> { using T = ZuBaseTraits<T_>; };
template <typename T>
using ZuDefaultTraits =
  typename ZuDefaultTraits_<T, decltype(ZuTraitsType(ZuDeclVal<T *>()))>::T;

template <typename T> struct ZuTraits : public ZuDefaultTraits<T> { };

// strip cv

template <typename T> struct ZuTraits<const T> : public ZuTraits<T> { };
template <typename T> struct ZuTraits<volatile T> : public ZuTraits<T> { };
template <typename T>
struct ZuTraits<const volatile T> : public ZuTraits<T> { };

// real numbers

template <typename T> struct ZuTraits_Real : public ZuBaseTraits<T> {
  enum {
    IsPrimitive = 1, IsReal = 1, IsPOD = 1
  };
};

// signed integers

template <typename T> struct ZuTraits_Signed : public ZuTraits_Real<T> {
  enum { IsIntegral = 1, IsSigned = 1 };
};

// unsigned integers

template <typename T> struct ZuTraits_Unsigned : public ZuTraits_Real<T> {
  enum { IsIntegral = 1 };
};

// primitive integral types

template <> struct ZuTraits<bool> : public ZuTraits_Unsigned<bool> {
  enum { IsBool = 1 };
};
#if CHAR_MIN
template <> struct ZuTraits<char> : public ZuTraits_Signed<char> { };
#else
template <> struct ZuTraits<char> : public ZuTraits_Unsigned<char> { };
#endif
template <>
struct ZuTraits<signed char> : public ZuTraits_Signed<signed char> { };
template <>
struct ZuTraits<unsigned char> : public ZuTraits_Unsigned<unsigned char> { };
#if WCHAR_MIN
template <> struct ZuTraits<wchar_t> : public ZuTraits_Signed<wchar_t> { };
#else
template <>
struct ZuTraits<wchar_t> : public ZuTraits_Unsigned<wchar_t> { };
#endif
template <> struct ZuTraits<short> : public ZuTraits_Signed<short> { };
template <>
struct ZuTraits<unsigned short> : public ZuTraits_Unsigned<unsigned short> { };
template <> struct ZuTraits<int> : public ZuTraits_Signed<int> { };
template <>
struct ZuTraits<unsigned int> : public ZuTraits_Unsigned<unsigned int> { };
template <> struct ZuTraits<long> : public ZuTraits_Signed<long> { };
template <>
struct ZuTraits<unsigned long> : public ZuTraits_Unsigned<unsigned long> { };
template <>
struct ZuTraits<long long> : public ZuTraits_Signed<long long> { };
template <>
struct ZuTraits<unsigned long long> :
  public ZuTraits_Unsigned<unsigned long long> { };
template <>
struct ZuTraits<__int128_t> : public ZuTraits_Signed<__int128_t> { };
template <>
struct ZuTraits<__uint128_t> : public ZuTraits_Unsigned<__uint128_t> { };

// primitive floating point types

template <typename T>
struct ZuTraits_Floating : public ZuTraits_Real<T>, public ZuFP<T> {
  enum { IsSigned = 1, IsFloatingPoint = 1 };
};

template <> struct ZuTraits<float> : public ZuTraits_Floating<float> { };
template <> struct ZuTraits<double> : public ZuTraits_Floating<double> { };
template <>
struct ZuTraits<long double> : public ZuTraits_Floating<long double> { };

// references

template <typename T> struct ZuTraits<T &> : public ZuTraits<T> {
  enum { IsReference = 1 };
};

template <typename T> struct ZuTraits<const T &> : public ZuTraits<T &> { };
template <typename T> struct ZuTraits<volatile T &> : public ZuTraits<T &> { };
template <typename T>
struct ZuTraits<const volatile T &> : public ZuTraits<T &> { };

template <typename T> struct ZuTraits<T &&> : public ZuTraits<T> {
  enum { IsRValueRef = 1 };
};

// pointers

template <typename T, typename Elem_>
struct ZuTraits_Pointer : public ZuBaseTraits<T> {
  enum {
    IsPrimitive = 1, IsPOD = 1, IsPointer = 1
  };
  using Elem = Elem_;
};

template <typename T>
struct ZuTraits<T *> : public ZuTraits_Pointer<T *, T> { };
template <typename T>
struct ZuTraits<const T *> :
  public ZuTraits_Pointer<const T *, const T> { };
template <typename T>
struct ZuTraits<volatile T *> :
  public ZuTraits_Pointer<volatile T *, volatile T> { };
template <typename T>
struct ZuTraits<const volatile T *> :
  public ZuTraits_Pointer<const volatile T *, const volatile T> { };

// primitive arrays

template <typename T, typename Elem_>
struct ZuTraits_PArray : public ZuBaseTraits<T> {
  using Elem = Elem_;
  enum {
    IsPrimitive = 1, // the array is primitive, the element might not be
    IsPOD = ZuTraits<Elem>::IsPOD,
    IsArray = 1, IsSpan = 1
  };
  template <typename U = T>
  ZuInline static constexpr ZuMutable<U, Elem *> data(U &a) {
    if (ZuConstEval())
      return ZuAddr(a[0]);
    else
      return &a[0];
  }
  ZuInline static constexpr const Elem *data(const T &a) {
    if (ZuConstEval())
      return ZuAddr(a[0]);
    else
      return &a[0];
  }
  ZuInline static constexpr unsigned length(const T &a) {
    return sizeof(a) / sizeof(a[0]);
  }
};

template <typename T>
struct ZuTraits<T[]> : public ZuTraits_PArray<T[], T> { };
template <typename T>
struct ZuTraits<const T[]> : public ZuTraits_PArray<const T[], const T> { };
template <typename T>
struct ZuTraits<volatile T[]> :
  public ZuTraits_PArray<volatile T[], volatile T> { };
template <typename T>
struct ZuTraits<const volatile T[]> :
  public ZuTraits_PArray<const volatile T[], const volatile T> { };

template <typename T, unsigned N>
struct ZuTraits<T [N]> : public ZuTraits_PArray<T [N], T> { };
template <typename T, unsigned N>
struct ZuTraits<const T [N]> : public ZuTraits_PArray<const T [N], const T> { };
template <typename T, unsigned N>
struct ZuTraits<volatile T [N]> :
  public ZuTraits_PArray<volatile T [N], volatile T> { };
template <typename T, unsigned N>
struct ZuTraits<const volatile T [N]> :
  public ZuTraits_PArray<const volatile T [N], const volatile T> { };

template <typename T, unsigned N>
struct ZuTraits<T (&)[N]> : public ZuTraits_PArray<T [N], T> { };
template <typename T, unsigned N>
struct ZuTraits<const T (&)[N]> :
  public ZuTraits_PArray<const T [N], const T> { };
template <typename T, unsigned N>
struct ZuTraits<volatile T (&)[N]> :
  public ZuTraits_PArray<volatile T [N], volatile T> { };
template <typename T, unsigned N>
struct ZuTraits<const volatile T (&)[N]> :
  public ZuTraits_PArray<const volatile T [N], const volatile T> { };

// strings

template <class Base>
struct ZuTraits_CString : public Base {
  enum { IsCString = 1, IsString = 1 };
  ZuInline static constexpr char *data(char *s) { return s; }
  ZuInline static constexpr const char *data(const char *s) { return s; }
  ZuInline static constexpr unsigned length(const char *s) {
    if (ZuConstEval()) {
      if (!s) return 0;
      unsigned i = 0;
      while (s[i]) ++i;
      return i;
    } else {
      return s ? strlen(s) : 0;
    }
  }
};

template <> struct ZuTraits<char *> :
  public ZuTraits_CString<ZuTraits_Pointer<char *, char>> { };
template <> struct ZuTraits<const char *> :
  public ZuTraits_CString<ZuTraits_Pointer<const char *, const char>> { };
template <> struct ZuTraits<volatile char *> :
  public ZuTraits_CString<ZuTraits_Pointer<volatile char *, volatile char>> { };
template <> struct ZuTraits<const volatile char *> :
  public ZuTraits_CString<ZuTraits_Pointer<
    const volatile char *, const volatile char>> { };

template <> struct ZuTraits<char[]> :
  public ZuTraits_CString<ZuTraits_PArray<char[], char>> { };
template <>
struct ZuTraits<const char[]> :
  public ZuTraits_CString<ZuTraits_PArray<const char[], const char>> { };
template <>
struct ZuTraits<volatile char[]> :
  public ZuTraits_CString<ZuTraits_PArray<volatile char[], volatile char>> { };
template <>
struct ZuTraits<const volatile char[]> :
  public ZuTraits_CString<ZuTraits_PArray<
    const volatile char[], const volatile char>> { };

// literal string
template <class Base, unsigned N> struct ZuTraits_LString : public Base {
  enum { IsCString = 1, IsString = 1 };
  ZuInline static constexpr char *data(char *s) { return s; }
  ZuInline static constexpr const char *data(const char *s) { return s; }
  ZuInline static constexpr unsigned length(const char (&s)[N]) {
    return N - !s[N - 1];
  }
};

template <unsigned N>
struct ZuTraits<char[N]> :
  public ZuTraits_LString<ZuTraits_PArray<char[N], char>, N> { };
template <unsigned N>
struct ZuTraits<const char[N]> :
  public ZuTraits_LString<ZuTraits_PArray<const char[N], const char>, N> { };
template <unsigned N>
struct ZuTraits<volatile char[N]> :
  public ZuTraits_LString<
    ZuTraits_PArray<volatile char[N], volatile char>, N> {
};
template <unsigned N>
struct ZuTraits<const volatile char[N]> :
  public ZuTraits_LString<
    ZuTraits_PArray<const volatile char[N], const volatile char>, N> { };

template <unsigned N>
struct ZuTraits<char (&)[N]> :
  public ZuTraits_LString<ZuTraits_PArray<char[N], char>, N> { };
template <unsigned N>
struct ZuTraits<const char (&)[N]> :
  public ZuTraits_LString<ZuTraits_PArray<const char[N], const char>, N> { };
template <unsigned N>
struct ZuTraits<volatile char (&)[N]> :
  public ZuTraits_LString<
    ZuTraits_PArray<volatile char[N], volatile char>, N> { };
template <unsigned N>
struct ZuTraits<const volatile char (&)[N]> :
  public ZuTraits_LString<
    ZuTraits_PArray<const volatile char[N], const volatile char>, N> { };

template <class Base> struct ZuTraits_WString : public Base {
  enum { IsCString = 1, IsString = 1, IsWString = 1 };
  ZuInline static constexpr wchar_t *data(wchar_t *s) { return s; }
  ZuInline static constexpr const wchar_t *data(const wchar_t *s) { return s; }
  ZuInline static constexpr unsigned length(const wchar_t *s) {
    if (ZuConstEval()) {
      if (!s) return 0;
      unsigned i = 0;
      while (s[i]) ++i;
      return i;
    } else {
      return s ? wcslen(s) : 0;
    }
  }
};

template <> struct ZuTraits<wchar_t *> :
  public ZuTraits_WString<ZuTraits_Pointer<wchar_t *, wchar_t>> { };
template <> struct ZuTraits<const wchar_t *> :
  public ZuTraits_WString<ZuTraits_Pointer<
      const wchar_t *, const wchar_t>> { };
template <> struct ZuTraits<volatile wchar_t *> :
  public ZuTraits_WString<ZuTraits_Pointer<
      volatile wchar_t *, volatile wchar_t>> { };
template <> struct ZuTraits<const volatile wchar_t *> :
  public ZuTraits_WString<ZuTraits_Pointer<
      const volatile wchar_t *, const volatile wchar_t>> { };

template <> struct ZuTraits<wchar_t []> :
  public ZuTraits_WString<ZuTraits_PArray<wchar_t [], wchar_t>> { };
template <>
struct ZuTraits<const wchar_t []> :
  public ZuTraits_WString<
    ZuTraits_PArray<const wchar_t [], const wchar_t>> { };
template <>
struct ZuTraits<volatile wchar_t []> :
  public ZuTraits_WString<
    ZuTraits_PArray<volatile wchar_t [], volatile wchar_t>> { };
template <>
struct ZuTraits<const volatile wchar_t []> :
  public ZuTraits_WString<
    ZuTraits_PArray<const volatile wchar_t [], const volatile wchar_t>> { };

// literal wide string
template <class Base, unsigned N> struct ZuTraits_LWString : public Base {
  enum { IsCString = 1, IsString = 1, IsWString = 1 };
  ZuInline static constexpr const wchar_t *data(const wchar_t *s) { return s; }
  ZuInline static constexpr unsigned length(const wchar_t (&s)[N]) {
    return N - !s[N - 1];
  }
};

template <unsigned N> struct ZuTraits<wchar_t [N]> :
  public ZuTraits_LWString<ZuTraits_PArray<wchar_t [N], wchar_t>, N> { };
template <unsigned N>
struct ZuTraits<const wchar_t [N]> :
  public ZuTraits_LWString<
    ZuTraits_PArray<const wchar_t [N], const wchar_t>, N> { };
template <unsigned N>
struct ZuTraits<volatile wchar_t [N]> :
  public ZuTraits_LWString<
    ZuTraits_PArray<volatile wchar_t [N], volatile wchar_t>, N> { };
template <unsigned N>
struct ZuTraits<const volatile wchar_t [N]> :
  public ZuTraits_LWString<
    ZuTraits_PArray<const volatile wchar_t [N], const volatile wchar_t>, N> { };

template <unsigned N> struct ZuTraits<wchar_t (&)[N]> :
  public ZuTraits_LWString<ZuTraits_PArray<wchar_t [N], wchar_t>, N> { };
template <unsigned N>
struct ZuTraits<const wchar_t (&)[N]> :
  public ZuTraits_LWString<
    ZuTraits_PArray<const wchar_t [N], const wchar_t>, N> { };
template <unsigned N>
struct ZuTraits<volatile wchar_t (&)[N]> :
  public ZuTraits_LWString<
    ZuTraits_PArray<volatile wchar_t [N], volatile wchar_t>, N> { };
template <unsigned N>
struct ZuTraits<const volatile wchar_t (&)[N]> :
  public ZuTraits_LWString<
    ZuTraits_PArray<const volatile wchar_t [N], const volatile wchar_t>, N> { };

// void

template <> struct ZuTraits<void> : public ZuBaseTraits<void> {
  enum { IsPrimitive = 1, IsPOD = 1, IsVoid = 1 };
};

// SFINAE techniques...
#define ZuTraits_SFINAE(Trait) \
template <typename U, typename R = void> \
using ZuMatch##Trait = ZuIfT<ZuTraits<U>::Is##Trait, R>; \
template <typename U, typename R = void> \
using ZuNot##Trait = ZuIfT<!ZuTraits<U>::Is##Trait, R>;

ZuTraits_SFINAE(Array)
ZuTraits_SFINAE(Span)
ZuTraits_SFINAE(Composite)
ZuTraits_SFINAE(Empty)
ZuTraits_SFINAE(Enum)
ZuTraits_SFINAE(POD)
ZuTraits_SFINAE(Reference)
ZuTraits_SFINAE(RValueRef)
ZuTraits_SFINAE(Pointer)
ZuTraits_SFINAE(Primitive)
ZuTraits_SFINAE(Real)
ZuTraits_SFINAE(Signed)
ZuTraits_SFINAE(Integral)
ZuTraits_SFINAE(FloatingPoint)
ZuTraits_SFINAE(String)
ZuTraits_SFINAE(CString)
ZuTraits_SFINAE(WString)
ZuTraits_SFINAE(Void)
ZuTraits_SFINAE(Bool)

#undef ZuTraits_SFINAE

template <typename U, typename R = void>
using ZuMatchCharString =
  ZuIfT<ZuTraits<U>::IsString && !ZuTraits<U>::IsWString, R>;

// derived traits for wrapper types
template <typename Wrapper, typename Under>
struct ZuWrapTraits : public ZuTraits<Under> {
  enum { IsPrimitive = 0, IsPOD = 0 };
  using Elem = typename ZuTraits<Under>::Elem;
  template <typename U = Under>
  ZuInline static ZuIfT<
    ZuTraits<U>::IsSpan &&
    !ZuIsConst<U>{} &&
    bool(ZuIsConvertible<Wrapper &, Under &>{}), Elem *>
  data(Wrapper &v) {
    return ZuTraits<Under>::data(v);
  }
  template <typename U = Under>
  ZuInline static ZuMatchSpan<U, const Elem *> data(const Wrapper &v) {
    return ZuTraits<Under>::data(v);
  }
  template <typename U = Under>
  ZuInline static ZuMatchArray<U, unsigned> length(const Wrapper &v) {
    return ZuTraits<Under>::length(v);
  }
};

// specialize traits for ZuConstant
template <typename T, T V>
struct ZuTraits<ZuConstant<T, V>> :
  public ZuWrapTraits<ZuConstant<T, V>, T> { };

// STL / Boost interoperability
template <typename T, typename Char>
struct ZuStdStringTraits_ : public ZuBaseTraits<T> {
  enum { IsString = 1 };
  using Elem = Char;
  template <typename U = T>
  static ZuMutable<U, Char *> data(T &s) { return s.data(); }
  static const Char *data(const T &s) { return s.data(); }
  static unsigned length(const T &s) { return s.length(); }
};
template <typename T_>
struct ZuStdStringTraits : public ZuStdStringTraits_<T_, char> { };
template <typename T_>
struct ZuStdWStringTraits : public ZuStdStringTraits_<T_, wchar_t>
  { enum { IsWString = 1 }; };

template <typename T, typename Elem_>
struct ZuStdArrayTraits_ : public ZuBaseTraits<T> {
  using Elem = Elem_;
  enum { IsArray = 1 };
  template <typename U = T>
  static ZuMutable<U, Elem *> data(T &a) { return a.data(); }
  static const Elem *data(const T &a) { return a.data(); }
  static unsigned length(const T &a) { return a.size(); }
};
template <typename T, typename Elem>
struct ZuStdVectorTraits : public ZuStdArrayTraits_<T, Elem> { };
template <typename T, typename Elem, size_t N>
struct ZuStdArrayTraits : public ZuStdArrayTraits_<T, Elem> { };

#include <zlib/ZuStdString.hh>

namespace std {
  template <typename, class> class vector;
  template <typename, size_t> class array;
}
template <typename Traits, typename Alloc>
struct ZuTraits<std::basic_string<char, Traits, Alloc> > :
    public ZuStdStringTraits<std::basic_string<char, Traits, Alloc> > { };
template <typename Traits, typename Alloc>
struct ZuTraits<std::basic_string<wchar_t, Traits, Alloc> > :
    public ZuStdWStringTraits<std::basic_string<wchar_t, Traits, Alloc> > { };
template <typename T, typename Alloc>
struct ZuTraits<std::vector<T, Alloc> > :
    public ZuStdVectorTraits<std::vector<T, Alloc>, T> { };
template <typename T, size_t N>
struct ZuTraits<std::array<T, N> > :
    public ZuStdArrayTraits<std::array<T, N>, T, N> { };

// pre-declare boost::basic_string_ref without pulling in all of Boost
namespace boost {
  template <typename, typename> class basic_string_ref;
  template <typename, size_t> class array;
}
template <typename Traits>
struct ZuTraits<boost::basic_string_ref<char, Traits> > :
    public ZuStdStringTraits<boost::basic_string_ref<char, Traits> > { };
template <typename Traits>
struct ZuTraits<boost::basic_string_ref<wchar_t, Traits> > :
    public ZuStdWStringTraits<boost::basic_string_ref<wchar_t, Traits> > { };
template <typename T, size_t N>
struct ZuTraits<boost::array<T, N> > :
    public ZuStdArrayTraits<boost::array<T, N>, T, N> { };

namespace std {
  template <typename> class initializer_list;
}
template <typename Elem_>
struct ZuTraits<std::initializer_list<Elem_> > :
    public ZuBaseTraits<std::initializer_list<Elem_> > {
  enum { IsArray = 1 };
  using Elem = Elem_;
private:
  using T = std::initializer_list<Elem_>;
public:
  static const Elem *data(const T &a) { return a.begin(); }
  static unsigned length(const T &a) { return a.size(); }
};

#endif /* ZuTraits_HH */
