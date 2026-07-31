//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// heap-allocated C string class (i.e. null-terminated)
// - explicitly a contiguous span
// - lightweight
// - direct read/write access to the buffer
// - no heap allocation for small strings below a built-in size
// - total instance size set to 32 (i.e. 1/2 typical cache line size)
// - very thin layer on C library string functions
// - no C library locale or character set overhead (except when requested)
// - minimal STL cruft
// - policy-based control of builtin size, fallback heap, etc.

// Note: use ZuCArray<> for fixed-size strings, by value, without heap overhead
// - ZuCArray<N> in an alias for ZuArray<char, N>

#ifndef ZtString_HH
#define ZtString_HH

#ifndef ZtLib_HH
#include <zlib/ZtLib.hh>
#endif

#include <string.h>
#include <wchar.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>

#include <zlib/ZuInt.hh>
#include <zlib/ZuTraits.hh>
#include <zlib/ZuCmp.hh>
#include <zlib/ZuHash.hh>
#include <zlib/ZuStringFn.hh>
#include <zlib/ZuUTF.hh>
#include <zlib/ZuPrint.hh>
#include <zlib/ZuBox.hh>
#include <zlib/ZuEquiv.hh>
#include <zlib/ZuAlt.hh>

#include <zlib/ZuVStream.hh>
#include <zlib/ZmVHeap.hh>
#include <zlib/ZmAlloc.hh>

#include <zlib/ZtPlatform.hh>
#include <zlib/ZtArray.hh>
#include <zlib/ZtIconv.hh>

// built-in buffer size (before falling back to ZmVHeap)
#ifndef ZtString_Builtin
#define ZtString_Builtin	16	// sizeof(ZtString<>) == 32
#endif

// uses NTP (named template parameters):
//
// ZtString<			// char ZtString
//   ZtStringHeapID<"HeapID">>	// override heap ID

// NTP defaults
struct ZtString_Defaults {
  enum { Builtin = ZtString_Builtin };
  struct HeapID : public ZuStringT<"ZtString"> { };
  enum { HeapMin = ZtString_Builtin };
  enum { HeapMax = 1024 };
  enum { Sharded = 0 };
};

// ZtStringBuiltin - override built-in size
template <unsigned Builtin_, typename NTP = ZtString_Defaults>
struct ZtStringBuiltin : public NTP {
  enum { Builtin = Builtin_ };
  enum { HeapMin = Builtin_ };
};

// ZtStringHeapID - the heap ID
template <typename HeapID_, typename NTP = ZtString_Defaults>
struct ZtStringHeapID_ : public NTP {
  using HeapID = HeapID_;
};
template <ZuString HeapID, typename NTP = ZtString_Defaults>
using ZtStringHeapID = ZtStringHeapID_<ZuStringT<HeapID>, NTP>;

// ZtStringHeapMin - min size of heap allocation
template <unsigned Min_, typename NTP = ZtString_Defaults>
struct ZtStringHeapMin : public NTP {
  enum { HeapMin = Min_ };
};

// ZtStringHeapMax - max size of heap allocation
template <unsigned Max_, typename NTP = ZtString_Defaults>
struct ZtStringHeapMax : public NTP {
  enum { HeapMax = Max_ };
};

// ZtStringSharded - heap sharding
template <bool Sharded_, typename NTP = ZtString_Defaults>
struct ZtStringSharded : public NTP {
  enum { Sharded = Sharded_ };
};

namespace Zt_ {

// buffer size increment for vsnprintf()
constexpr unsigned String_vsnprintf_Growth = 256;
constexpr unsigned String_vsnprintf_MaxSize = (1<<20); // 1M

template <typename Char> inline const Char *String_Null();
template <> inline const char *String_Null() { return ""; }
template <> inline const wchar_t *String_Null() { return Zu::nullWString(); }

template <typename> struct String_ { };
template <> struct String_<char> {
  friend ZuPrintString ZuPrintType(String_ *);
};

template <typename Char_, typename NTP>
class String :
  private ZmVHeap_<
    typename NTP::HeapID,
    NTP::HeapMin,
    NTP::HeapMax,
    1,
    NTP::Sharded>,
  public String_<ZuStrip<Char_>> {
public:
  using Char = Char_;
  using AltChar = ZuAlt<Char>;
  enum { IsWString = ZuIsSame<Char, wchar_t>{} };
  enum { BuiltinSize_ = NTP::Builtin / sizeof(Char) };
  enum { BuiltinSize =
    BuiltinSize_ < sizeof(uintptr_t) ? sizeof(uintptr_t) : BuiltinSize_ };
  using HeapID = typename NTP::HeapID;
  enum { HeapMin = NTP::HeapMin };
  enum { HeapMax = NTP::HeapMax };
  enum { Sharded = NTP::Sharded };
  using VHeap = ZmVHeap_<HeapID, HeapMin, HeapMax, 1, Sharded>;

private:
  // from same type String
  template <typename U, typename V = Char>
  struct IsString : public ZuIsBase<U, String_<V>> { };
  template <typename U, typename R = void>
  using MatchString = ZuIfT<IsString<U>{}, R>;

  // from string literal with same char
  template <
    typename U,
    typename V = ZuStrip<Char>,
    unsigned N = sizeof(U) / sizeof(V)>
  struct IsStrLiteral : public ZuBool<
    bool(ZuIsSame<U, V [N]>{}) ||
    bool(ZuIsSame<U, V (&)[N]>{}) ||
    bool(ZuIsSame<U, const V [N]>{}) ||
    bool(ZuIsSame<U, const V (&)[N]>{})> { };
  template <typename U, typename R = void>
  using MatchStrLiteral = ZuIfT<IsStrLiteral<U>{}, R>;

  // from some other C string with same char (including string literals)
  template <typename U, typename V = Char>
  struct IsAnyCString : public ZuBool<
    !IsString<U>{} &&
    ZuTraits<U>::IsCString &&
    bool(ZuEquiv<typename ZuTraits<U>::Elem, V>{})> { };
  template <typename U, typename R = void>
  using MatchAnyCString = ZuIfT<IsAnyCString<U>{}, R>;

  // from some other C string with same char (other than a string literal)
  template <typename U, typename V = Char>
  struct IsCString : public ZuBool<
    !IsStrLiteral<U>{} &&
    bool(IsAnyCString<U>{})> { };
  template <typename U, typename R = void>
  using MatchCString = ZuIfT<IsCString<U>{}, R>;

  // from some other non-C string with same char (non-null-terminated)
  template <typename U, typename V = Char>
  struct IsOtherString : public ZuBool<
    !IsString<U>{} && !ZuTraits<U>::IsCString &&
    (ZuTraits<U>::IsSpan || ZuTraits<U>::IsString) &&
    bool(ZuEquiv<typename ZuTraits<U>::Elem, V>{})> { };
  template <typename U, typename R = void>
  using MatchOtherString = ZuIfT<IsOtherString<U>{}, R>;

  // from char2 string (requires conversion)
  template <typename U, typename V = AltChar>
  struct IsAltString : public ZuBool<
    (ZuTraits<U>::IsSpan || ZuTraits<U>::IsString) &&
    bool(ZuEquiv<typename ZuTraits<U>::Elem, V>{})> { };
  template <typename U, typename R = void>
  using MatchAltString = ZuIfT<IsAltString<U>{}, R>;

  // from individual char
  template <typename U, typename V = Char>
  struct IsChar : public ZuBool<
    bool(ZuIsSame<V, wchar_t>{}) ?
      bool(ZuIsSame<ZuDecay<U>, V>{}) :
      bool(ZuEquiv<U, V>{})> { };
  template <typename U, typename R = void>
  using MatchChar = ZuIfT<IsChar<U>{}, R>;

  // from individual char2 (requires conversion)
  template <typename U, typename V = AltChar>
  struct IsAltChar : public IsChar<U, V> { };
  template <typename U, typename R = void>
  using MatchAltChar = ZuIfT<IsAltChar<U>{}, R>;

  // from printable type
  template <typename U>
  struct IsPrint : public ZuBool<
    !IsString<U>{} &&
    !IsAnyCString<U>{} &&
    !IsAltString<U>{} &&
    ZuPrint<U>::OK && !ZuPrint<U>::String> { };
  template <typename U, typename R = void>
  using MatchPrint = ZuIfT<IsPrint<U>{}, R>;
  template <typename U>
  struct IsPDelegate : public ZuBool<ZuPrint<U>::Delegate> { };
  template <typename U, typename R = void>
  using MatchPDelegate = ZuIfT<IsPDelegate<U>{}, R>;
  template <typename U>
  struct IsPBuffer : public ZuBool<ZuPrint<U>::Buffer> { };
  template <typename U, typename R = void>
  using MatchPBuffer = ZuIfT<IsPBuffer<U>{}, R>;

  // from any other real and primitive type (integers, floating point, etc.)
  template <typename U>
  struct IsReal : public ZuBool<
    !IsChar<U>{} && !IsAltChar<U>{} &&
    ZuTraits<U>::IsReal && ZuTraits<U>::IsPrimitive &&
    !ZuTraits<U>::IsArray> { };
  template <typename U, typename R = void>
  using MatchReal = ZuIfT<IsReal<U>{}, R>;

  // from primitive pointer (not an array, string, or otherwise printable)
  template <typename U>
  struct IsPtr : public ZuBool<
    ZuTraits<U>::IsPointer && ZuTraits<U>::IsPrimitive &&
    !ZuTraits<U>::IsArray && !ZuTraits<U>::IsString> { };
  template <typename U, typename R = void>
  using MatchPtr = ZuIfT<IsPtr<U>{}, R>;

  // limit member operator <<() overload resolution to supported types
  template <typename U> struct IsStreamable : public ZuBool<
    bool(IsString<U>{}) ||
    bool(IsAnyCString<U>{}) ||
    bool(IsOtherString<U>{}) ||
    bool(IsChar<U>{}) ||
    bool(IsAltString<U>{}) ||
    bool(IsAltChar<U>{}) ||
    bool(IsPDelegate<U>{}) ||
    bool(IsPBuffer<U>{}) ||
    bool(IsReal<U>{}) ||
    bool(IsPtr<U>{})> { };
  template <typename U, typename R = void>
  using MatchStreamable = ZuIfT<IsStreamable<U>{}, R>;

  // an integer parameter to the constructor is a buffer size
  // - except for integer types that are also character element types
  template <typename U, typename V = Char>
  struct IsCtorSize : public ZuBool<
    ZuTraits<U>::IsIntegral && (sizeof(U) > 2 || !ZuEquiv<V, U>{})> { };
  template <typename U, typename R = void>
  using MatchCtorSize = ZuIfT<IsCtorSize<U>{}, R>;
  // disambiguate ZuBox<int>, etc.
  template <typename U, typename R = void>
  using MatchCtorPDelegate =
    ZuIfT<bool(IsPDelegate<U>{}) && !IsCtorSize<U>{}, R>;
  template <typename U, typename R = void>
  using MatchCtorPBuffer =
    ZuIfT<bool(IsPBuffer<U>{}) && !IsCtorSize<U>{}, R>;

  // construction from any other real and primitive type
  template <typename U, typename V = Char> struct IsCtorReal :
    public ZuBool<IsReal<U>{} && !IsCtorSize<U>{}> { };
  template <typename U, typename R = void>
  using MatchCtorReal = ZuIfT<IsCtorReal<U>{}, R>;

public:
// constructors, assignment operators and destructor

  String() noexcept { null_(); }
  String(const String &s) noexcept {
    copy_(s.data_(), s.length());
  }
  // note that the built-in size and heap parameters form part of the type, so
  // moving will only happen among identically-typed strings; this is both
  // intentional and important for heap instrumentation and tuning
  String(String &&s) noexcept {
    if (ZuUnlikely(s.null__())) { null_(); return; }
    if (ZuLikely(s.builtin())) { copy_(s.data_(), s.length()); return; }
    if (ZuUnlikely(!s.mutable_())) { shadow_(s.data_(), s.length()); return; }
    own_(s.data_(), s.length(), s.size(), s.vallocd());
    s.mutable_(s.builtin());
    s.vallocd(0);
  }

  template <typename S> String(S &&s) { ctor(ZuFwd<S>(s)); }

private:
  template <typename S> MatchString<S> ctor(const S &s)
    { copy_(s.data_(), s.length()); }
  template <typename S> MatchStrLiteral<S> ctor(S &&s_)
    { ZuSpan<const Char> s(s_); shadow_(s.data(), s.length()); }
  template <typename S> MatchCString<S> ctor(S &&s_)
    { ZuSpan<const Char> s(s_); copy_(s.data(), s.length()); }
  template <typename S> MatchOtherString<S> ctor(S &&s_)
    { ZuSpan<const Char> s(s_); copy_(s.data(), s.length()); }
  template <typename C> MatchChar<C> ctor(C c)
    { copy_(&c, 1); }

  template <typename S> MatchAltString<S> ctor(S &&s_) {
    ZuSpan<const AltChar> s(s_);
    uint64_t o = ZuUTF<Char, AltChar>::len(s);
    if (!o) { null_(); return; }
    length_(ZuUTF<Char, AltChar>::cvt({alloc_(o + 1, 0), o}, s));
  }

  template <typename C> MatchAltChar<C> ctor(C c) {
    ZuSpan<const AltChar> s(&c, 1);
    uint64_t o = ZuUTF<Char, AltChar>::len(s);
    if (!o) { null_(); return; }
    length_(ZuUTF<Char, AltChar>::cvt({alloc_(o + 1, 0), o}, s));
  }

  template <typename P> MatchCtorPDelegate<P> ctor(const P &p)
    { null_(); ZuPrint<P>::print(*this, p); }
  template <typename P> MatchCtorPBuffer<P> ctor(const P &p) {
    unsigned o = ZuPrint<P>::length(p);
    if (!o) { null_(); return; }
    if constexpr (ZuEquiv<Char, char>{}) {
      length_(ZuPrint<P>::print(alloc_(o + 1, 0), o, p));
    } else {
      auto buf = ZmAlloc(char, o);
      ZuCSpan s(&buf[0], ZuPrint<P>::print(&buf[0], o, p));
      o = ZuUTF<Char, AltChar>::len(s);
      if (!o) { null_(); return; }
      length_(ZuUTF<Char, AltChar>::cvt({alloc_(o + 1, 0), o}, s));
    }
  }

  template <typename V> MatchCtorSize<V> ctor(V size) {
    if (!size) { null_(); return; }
    alloc_(size, 0)[0] = 0;
  }

  template <typename R> MatchCtorReal<R> ctor(R r) {
    ctor(ZuBoxed(r));
  }

public:
  void copy(const String &s) {
    copy_(s.data_(), s.length());
  }
  template <typename S> MatchAnyCString<S> copy(S &&s_) {
    ZuSpan<const Char> s(s_);
    copy_(s.data(), s.length());
  }
  template <typename S> MatchOtherString<S> copy(S &&s_) {
    ZuSpan<const Char> s(s_);
    copy_(s.data(), s.length());
  }
  template <typename C> MatchChar<C> copy(C c) {
    copy_(&c, 1);
  }

  template <typename S> MatchAltString<S> copy(S &&s_) {
    ZuSpan<const AltChar> s(s_);
    uint64_t o = ZuUTF<Char, AltChar>::len(s);
    if (!o) { length_(0); return; }
    length_(ZuUTF<Char, AltChar>::cvt({ensure(o + 1), o}, s));
  }
	  
  template <typename C> MatchAltChar<C> copy(C c) {
    ZuSpan<const AltChar> s(&c, 1);
    uint64_t o = ZuUTF<Char, AltChar>::len(s);
    if (!o) { length_(0); return; }
    length_(ZuUTF<Char, AltChar>::cvt({ensure(o + 1), o}, s));
  }

public:
  String &operator =(const String &s) noexcept {
    if (ZuLikely(this != &s)) {
      Char *oldData = free_1();
      copy_(s.data_(), s.length());
      free_2(oldData);
    }
    return *this;
  }
  String &operator =(String &&s) noexcept {
    if (ZuLikely(this != &s)) {
      this->~String();
      new (this) String(ZuMv(s));
    }
    return *this;
  }

  template <typename S>
  String &operator =(S &&s) { assign(ZuFwd<S>(s)); return *this; }

private:
  template <typename S>
  MatchString<S> assign(const S &s) {
    if constexpr (ZuIsSame<S, String>{})
      if (this == &s) return;
    Char *oldData = free_1();
    copy_(s.data_(), s.length());
    free_2(oldData);
  }
  template <typename S> MatchStrLiteral<S> assign(S &&s_) {
    ZuSpan<const Char> s(s_);
    free_();
    shadow_(s.data(), s.length());
  }
  template <typename S> MatchCString<S> assign(S &&s_) {
    ZuSpan<const Char> s(s_);
    Char *oldData = free_1();
    copy_(s.data(), s.length());
    free_2(oldData);
  }
  template <typename S> MatchOtherString<S> assign(S &&s_) {
    ZuSpan<const Char> s(s_);
    Char *oldData = free_1();
    copy_(s.data(), s.length());
    free_2(oldData);
  }
  template <typename C> MatchChar<C> assign(C c) {
    Char *oldData = free_1();
    copy_(&c, 1);
    free_2(oldData);
  }

  template <typename S> MatchAltString<S> assign(S &&s_) {
    ZuSpan<const AltChar> s(s_);
    uint64_t o = ZuUTF<Char, AltChar>::len(s);
    if (!o) { length_(0); return; }
    length_(ZuUTF<Char, AltChar>::cvt({ensure(o + 1), o}, s));
  }
  template <typename C> MatchAltChar<C> assign(C c) {
    ZuSpan<const AltChar> s(&c, 1);
    uint64_t o = ZuUTF<Char, AltChar>::len(s);
    if (!o) { length_(0); return; }
    length_(ZuUTF<Char, AltChar>::cvt({ensure(o + 1), o}, s));
  }

  template <typename P> MatchPDelegate<P> assign(P &&p) {
    ZuPrint<P>::print(*this, ZuFwd<P>(p));
  }
  template <typename P> MatchPBuffer<P> assign(const P &p) {
    unsigned o = ZuPrint<P>::length(p);
    if (!o) { length_(0); return; }
    if constexpr (ZuEquiv<Char, char>{}) {
      length_(ZuPrint<P>::print(ensure(o + 1), o, p));
    } else {
      auto buf = ZmAlloc(char, o);
      ZuCSpan s(&buf[0], ZuPrint<P>::print(&buf[0], o, p));
      o = ZuUTF<Char, AltChar>::len(s);
      if (!o) { null_(); return; }
      length_(ZuUTF<Char, AltChar>::cvt({ensure(o + 1), o}, s));
    }
  }

  template <typename V> MatchReal<V> assign(V v) {
    assign(ZuBoxed(v));
  }
  template <typename V> MatchPtr<V> assign(V v) {
    assign(ZuBoxPtr(v).hex<false, ZuFmt::Alt<>>());
  }

public:
  template <typename S> String &operator -=(const S &s) {
    shadow(s);
    return *this;
  }

private:
  template <typename S>
  MatchString<S> shadow(const S &s) {
    if constexpr (ZuIsSame<S, String>{})
      if (this == &s) return;
    free_();
    shadow_(s.data_(), s.length());
  }
  template <typename S>
  MatchAnyCString<S> shadow(S &&s_) {
    ZuSpan<const Char> s(s_);
    free_();
    shadow_(s.data(), s.length());
  }
  template <typename S>
  MatchOtherString<S> shadow(S &&s_) {
    ZuSpan<const Char> s(s_);
    free_();
    shadow_(s.data(), s.length());
  }

public:
  template <typename S, ZuMatchString<S, int> = 0>
  String(S &&s_, ZtIconv *iconv) {
    ZuSpan<const typename ZuTraits<S>::Elem> s(s_);
    convert_(s, iconv);
  }
  String(const Char *data, uint64_t length, ZtIconv *iconv) {
    ZuSpan<const Char> s(data, length);
    convert_(s, iconv);
  }
  String(const AltChar *data, uint64_t length, ZtIconv *iconv) {
    ZuSpan<const AltChar> s(data, length);
    convert_(s, iconv);
  }

public:
  String(uint64_t length, uint64_t size) {
    if (!size) { null_(); return; }
    alloc_(size, length)[length] = 0;
  }
  explicit String(const Char *data, uint64_t length) {
    if (!length) { null_(); return; }
    copy_(data, length);
  }
  explicit String(
      Char *data, uint64_t length, uint64_t size, bool vallocd) {
    if (!data) { null_(); return; }
    own_(data, length, size, vallocd);
  }

  ~String() { free_(); }

// re-initializers
  void init() { free_(); init_(); }
  void init_() { null_(); }

  template <typename S> void init(const S &s) { assign(s); }
  template <typename S> void init_(const S &s) { ctor(s); }

  void init(uint64_t length, uint64_t size) {
    if (!size) { null_(); return; }
    uint64_t z = this->size();
    if (z < size) {
      free_();
      alloc_(size, length);
    } else
      length_(length);
  }
  void init_(uint64_t length, uint64_t size) {
    if (!size) { null_(); return; }
    alloc_(size, length);
  }
  void init(const Char *data, uint64_t length) {
    Char *oldData = free_1();
    init_(data, length);
    free_2(oldData);
  }
  void init_(const Char *data, uint64_t length) {
    if (!length) { null_(); return; }
    copy_(data, length);
  }
  void init(
      const Char *data, uint64_t length, uint64_t size, bool vallocd) {
    free_();
    init_(data, length, size, vallocd);
  }
  void init_(
      const Char *data, uint64_t length, uint64_t size, bool vallocd) {
    if (!data) { null_(); return; }
    own_(data, length, size, vallocd);
  }

// internal initializers / finalizer
private:
  using VHeap::valloc;
  using VHeap::vfree;

public: // useful if the caller is sure that the length is being reduced
  void length_(uint64_t n) {
    null__(0);
    length__(n);
    data_()[n] = 0;
  }

protected:
  void null_() {
    ptr__(nullptr);
    size_mutable_null(BuiltinSize, 1, 1);
    length_vallocd_builtin(0, 0, 1);
  }

  void own_(const Char *data, uint64_t length, uint64_t size, bool vallocd) {
    ZmAssert(size >= length);
    if (!size) {
      if (data && vallocd) vfree(data);
      null_();
      return;
    }
    ptr__(data);
    size_mutable_null(size, 1, 0);
    length_vallocd_builtin(length, vallocd, 0);
  }

  void shadow_(const Char *data, uint64_t length) {
    if (!length) { null_(); return; }
    ptr__(data);
    size_mutable_null(length + 1, 0, 0);
    length_vallocd_builtin(length, 0, 0);
  }

  Char *alloc_(uint64_t size, uint64_t length) {
    if (ZuLikely(size <= BuiltinSize)) {
      size_mutable_null(size, 1, 0);
      length_vallocd_builtin(length, 0, 1);
      return data__();
    }
    Char *newData = static_cast<Char *>(valloc(size * sizeof(Char)));
    if (!newData) throw std::bad_alloc{};
    ptr__(newData);
    size_mutable_null(size, 1, 0);
    length_vallocd_builtin(length, 1, 0);
    return newData;
  }

  void copy_(const Char *copyData, uint64_t length) {
    if (!length) { null_(); return; }
    if (length < BuiltinSize - 1) {
      memcpy(data__(), copyData, length * sizeof(Char));
      (data__())[length] = 0;
      size_mutable_null(BuiltinSize, 1, 0);
      length_vallocd_builtin(length, 0, 1);
      return;
    }
    Char *newData = static_cast<Char *>(valloc((length + 1) * sizeof(Char)));
    if (!newData) throw std::bad_alloc{};
    memcpy(newData, copyData, length * sizeof(Char));
    newData[length] = 0;
    ptr__(newData);
    size_mutable_null(length + 1, 1, 0);
    length_vallocd_builtin(length, 1, 0);
  }

  template <typename S> void convert_(const S &s, ZtIconv *iconv);

  void free_() {
    if (vallocd())
      if (Char *data = ptr__())
	vfree(data);
  }
  Char *free_1() {
    if (!vallocd()) return nullptr;
    return data_();
  }
  void free_2(Char *data) {
    if (data) vfree(data);
  }

public:
// truncation (to minimum size)
  void truncate() { size(length() + 1); }

// array / ptr operators
  ZuInline Char &operator [](uint64_t i) { return data_()[i]; }
  ZuInline const Char &operator [](uint64_t i) const { return data_()[i]; }

  ZuInline operator Char *() {
    return null__() ? nullptr : data_();
  }
  ZuInline operator const Char *() const {
    return null__() ? nullptr : data_();
  }

// accessors
  using iterator = Char *;
  using const_iterator = const Char *;
  using iterator_category = std::contiguous_iterator_tag;
  ZuInline const Char *begin() const {
    if (null__()) return nullptr;
    return data_();
  }
  ZuInline const Char *end() const {
    if (null__()) return nullptr;
    return data_() + length();
  }
  ZuInline Char *begin() {
    return const_cast<Char *>(static_cast<const String &>(*this).begin());
  }
  ZuInline Char *end() {
    return const_cast<Char *>(static_cast<const String &>(*this).end());
  }
  ZuInline const Char *cbegin() const { return begin(); }
  ZuInline const Char *cend() const { return end(); }

  Char *data() {
    if (null__()) return nullptr;
    return data_();
  }
  Char *data_() {
    return builtin() ? data__() : ptr__();
  }
  const Char *data() const {
    if (null__()) return nullptr;
    return data_();
  }
  const Char *data_() const {
    return builtin() ? data__() : ptr__();
  }
  const Char *ndata() const {
    if (null__()) return String_Null<Char>();
    return data_();
  }

  uint64_t length() const {
    return m_length_vallocd_builtin & ~(uint64_t(3)<<62);
  }
  uint64_t size() const {
    uint64_t u = m_size_mutable_null;
    return u & ~(uint64_t(3)<<62);
  }
  bool vallocd() const { return (m_length_vallocd_builtin>>62) & 1; }
  bool builtin() const { return m_length_vallocd_builtin>>63; }
  bool mutable_() const { return (m_size_mutable_null>>62) & 1; }

// direct buffer access
  auto span() { return ZuSpan(data_(), length()); }
  auto cspan() const { return ZuSpan(data_(), length()); }

private:
  const Char *data__() const {
    return reinterpret_cast<const Char *>(&m_data[0]);
  }
  Char *data__() { return reinterpret_cast<Char *>(&m_data[0]); }

  const Char *ptr__() const {
    return *reinterpret_cast<const Char *const *>(&m_data[0]);
  }
  Char *ptr__() { return *reinterpret_cast<Char **>(&m_data[0]); }
  void ptr__(const Char *p) {
    *reinterpret_cast<const Char **>(&m_data[0]) = p;
  }

  void length__(uint64_t v) {
    m_length_vallocd_builtin =
      (m_length_vallocd_builtin & (uint64_t(3)<<62)) | v;
  }
  void vallocd(bool v) {
    m_length_vallocd_builtin =
      (m_length_vallocd_builtin & ~(uint64_t(1)<<62)) | (uint64_t(v)<<62);
  }
  void builtin(bool v) {
    m_length_vallocd_builtin =
      (m_length_vallocd_builtin & ~(uint64_t(1)<<63)) | (uint64_t(v)<<63);
  }
  void length_vallocd_builtin(uint64_t l, bool m, bool b) {
    m_length_vallocd_builtin = l | (uint64_t(m)<<62) | (uint64_t(b)<<63);
  }
  uint64_t size_() const {
    return m_size_mutable_null & ~(uint64_t(3)<<62);
  }
  void size_(uint64_t v) {
    m_size_mutable_null = (m_size_mutable_null & (uint64_t(3)<<62)) | v;
  }
  void mutable_(bool v) {
    m_size_mutable_null =
      (m_size_mutable_null & ~(uint64_t(1)<<62)) | (uint64_t(v)<<62);
  }
  bool null__() const { return m_size_mutable_null>>63; }
  void null__(bool v) {
    m_size_mutable_null =
      (m_size_mutable_null & ~(uint64_t(1)<<63)) | (uint64_t(v)<<63);
  }
  void size_mutable_null(uint64_t z, bool o, bool n) {
    m_size_mutable_null = z | (uint64_t(o)<<62) | (uint64_t(n)<<63);
  }

public:
// release / free
  Char *release() && {
    if (null__()) return nullptr;
    if (builtin()) {
      Char *newData = static_cast<Char *>(valloc(BuiltinSize * sizeof(Char)));
      if (!newData) throw std::bad_alloc{};
      memcpy(newData, m_data, (length() + 1) * sizeof(Char));
      return newData;
    } else {
      mutable_(0);
      vallocd(0);
      return ptr__();
    }
  }
  static void free(const Char *ptr) { vfree(ptr); }

// reset to null string
  void null() {
    free_();
    null_();
  }

// clear without freeing
  void clear() {
    if (!null__()) {
      if (!mutable_()) { null_(); return; }
      length_(0);
    }
  }

// set length
  void length(uint64_t n) {
    if (!mutable_() || n >= size_()) size(n + 1);
    length_(n);
  }
  void calcLength() {
    if (null__())
      length__(0);
    else {
      auto data = data_();
      data[size_() - 1] = 0;
      length__(Zu::strlen_(data));
    }
  }

// ensure size
  Char *ensure(uint64_t o) {
    uint64_t z = size_();
    if (ZuLikely(mutable_() && o <= z)) return data_();
    return size(grow_(z, o));
  }

// set size
  Char *size(uint64_t z) {
    if (ZuUnlikely(!z)) { null(); return nullptr; }
    if (mutable_() && z == size_()) return data_();
    Char *oldData = data_();
    Char *newData;
    if (z <= BuiltinSize)
      newData = data__();
    else {
      newData = static_cast<Char *>(valloc(z * sizeof(Char)));
      if (!newData) throw std::bad_alloc{};
    }
    uint64_t n = z - 1;
    if (n > length()) n = length();
    if (oldData && oldData != newData) {
      memcpy(newData, oldData, (n + 1) * sizeof(Char));
      if (vallocd()) vfree(oldData);
    }
    if (z <= BuiltinSize) {
      size_mutable_null(z, 1, 0);
      length_vallocd_builtin(n, 0, 1);
      return newData;
    }
    ptr__(newData);
    size_mutable_null(z, 1, 0);
    length_vallocd_builtin(n, 1, 0);
    return newData;
  }

// common prefix
  template <typename S>
  MatchString<S, ZuSpan<const Char>> prefix(const S &s) {
    if constexpr (ZuIsSame<S, String>{})
      if (this == &s) return cspan();
    if (null__()) return {};
    return ZuSpan(data_(), cspan().prefix(s.cspan()));
  }
  template <typename S>
  MatchAnyCString<S &&, ZuSpan<const Char>> prefix(S &&s_) {
    ZuSpan<const Char> s(s_);
    if (null__()) return {};
    return ZuSpan(data_(), cspan().prefix(s));
  }
  template <typename S>
  MatchOtherString<S &&, ZuSpan<const Char>> prefix(S &&s_) {
    ZuSpan<const Char> s(s_);
    if (null__()) return {};
    return ZuSpan(data_(), cspan().prefix(s));
  }

public:
// find (forwards to ZuSpan)
  template <typename Arg>
  int64_t find(Arg &&arg) const {
    return cspan().find(ZuFwd<Arg>(arg));
  }
  template <ZuString S, typename V = Char>
  ZuIfT<ZuEquiv<V, char>{}, int64_t>
  find() const { return cspan().template find<S>(); }

// match at start (forwards to ZuSpan)
  template <typename Arg>
  bool match(Arg &&arg) const {
    return cspan().match(ZuFwd<Arg>(arg));
  }
  template <ZuString S, typename V = Char>
  ZuIfT<ZuEquiv<V, char>{}, bool>
  match() const { return cspan().template match<S>(); }

// hash()
  uint64_t hash() const { return ZuHash<String>::hash(*this); }

// comparison
  bool operator !() const { return !length(); }

  template <typename S>
  bool equals(const S &s) const { return ZuEquals(*this, s); }
  template <typename S>
  int cmp(const S &s) const { return ZuCompare(*this, s); }
  template <typename L, typename R>
  friend inline
  ZuIfT<ZuIs_<L, String>{} && ZuTraits<R>::IsString, bool>
  operator ==(const L &l, const R &r) { return l.equals(r); }
  template <typename L, typename R>
  friend inline
  ZuIfT<ZuIs_<L, String>{} && ZuTraits<R>::IsString, int>
  operator <=>(const L &l, const R &r) { return l.cmp(r); }

  bool equals(const Char *s, uint64_t n) const {
    if (null__()) return !s;
    if (!s) return false;
    return !Zu::strcmp_(data_(), s, n);
  }
  int cmp(const Char *s, uint64_t n) const {
    if (null__()) return s ? -1 : 0;
    if (!s) return 1;
    return Zu::strcmp_(data_(), s, n);
  }
  int icmp(const Char *s, uint64_t n) const {
    if (null__()) return s ? -1 : 0;
    if (!s) return 1;
    return Zu::stricmp_(data_(), s, n);
  }

// +, += operators
  template <typename S>
  String operator +(const S &s) const { return add(s); }

private:
  template <typename S>
  MatchString<S, String>
  add(const S &s) const {
    return add_([data = s.data_()](Char *ptr, uint64_t length) {
      if (length) memcpy(ptr, data, length * sizeof(Char));
      return length;
    }, s.length());
  }
  template <typename S>
  MatchAnyCString<S &&, String> add(S &&s_) const {
    ZuSpan<const Char> s(s_);
    return add_([data = s.data()](Char *ptr, uint64_t length) {
      if (length) memcpy(ptr, data, length * sizeof(Char));
      return length;
    }, s.length());
  }
  template <typename S>
  MatchOtherString<S &&, String> add(S &&s_) const {
    ZuSpan<const Char> s(s_);
    return add_([data = s.data()](Char *ptr, uint64_t length) {
      if (length) memcpy(ptr, data, length * sizeof(Char));
      return length;
    }, s.length());
  }
  template <typename C>
  MatchChar<C, String> add(C c) const {
    return add_([c](Char *ptr, uint64_t) { *ptr = c; return 1; }, 1);
  }

  template <typename S>
  MatchAltString<S, String>
  add(const S &s_) const {
    ZuSpan<const AltChar> s(s_);
    return add_([s](Char *ptr, uint64_t length) -> uint64_t {
      if (!length) return 0;
      return ZuUTF<Char, AltChar>::cvt({ptr, length}, s);
    }, ZuUTF<Char, AltChar>::len(s));
  }
  template <typename C>
  MatchAltChar<C, String>
  add(C c_) const {
    AltChar c = c_;
    return add_([c](Char *ptr, uint64_t length) {
      return ZuUTF<Char, AltChar>::cvt({ptr, length}, {&c, 1});
    }, ZuUTF<Char, AltChar>::len({&c, 1}));
  }

  template <typename P>
  MatchPDelegate<P, String> add(P &&p) const {
    String s(*this);
    s.append_(ZuFwd<P>(p));
    return s;
  }
  template <typename P>
  MatchPBuffer<P, String> add(P &&p) const {
    unsigned o = ZuPrint<P>::length(p);
    if (!o) return *this;
    if constexpr (ZuEquiv<Char, char>{}) {
      return add_([&p](Char *ptr, uint64_t length) {
	return ZuPrint<P>::print(ptr, length, p);
      }, ZuPrint<P>::length(p));
    } else {
      auto buf = ZmAlloc(char, o);
      ZuCSpan s(&buf[0], ZuPrint<P>::print(&buf[0], o, p));
      return add_([s](Char *ptr, uint64_t length) -> uint64_t {
	if (!length) return 0;
	return ZuUTF<Char, AltChar>::cvt({ptr, length}, s);
      }, ZuUTF<Char, AltChar>::len(s));
    }
  }

  template <typename Add>
  String add_(Add add, uint64_t length) const {
    uint64_t n = this->length();
    uint64_t o = n + length;
    if (!o) return String{};
    String s(o + 1);
    auto data = s.data_();
    if (n) memcpy(data, data_(), n * sizeof(Char));
    length = add(data + n, length);
    s.length_(n + length); // length may have been reduced
    return s;
  }

public:
  template <typename U>
  String &operator +=(U &&v) { return *this << ZuFwd<U>(v); }
  template <typename U>
  MatchStreamable<U, String &>
  operator <<(U &&v) {
    append_(ZuFwd<U>(v));
    return *this;
  }

private:
  template <typename S>
  MatchString<S> append_(const S &s) {
    if (ZuUnlikely(!s.length())) return;
    if constexpr (ZuIsSame<S, String>{})
      if (this == &s) {
	auto buf = ZmAlloc(Char, s.length());
	memcpy(&buf[0], s.data_(), s.length() * sizeof(Char));
	append__([data = &buf[0]](Char *ptr, uint64_t rlength) {
	  memcpy(ptr, data, rlength * sizeof(Char));
	  return rlength;
	}, s.length());
	return;
      }
    append__([data = s.data_()](Char *ptr, uint64_t rlength) {
      memcpy(ptr, data, rlength * sizeof(Char));
      return rlength;
    }, s.length());
  }
  template <typename S>
  MatchAnyCString<S> append_(S &&s_) {
    ZuSpan<const Char> s(s_);
    append__([data = s.data()](Char *ptr, uint64_t rlength) {
      if (rlength) memcpy(ptr, data, rlength * sizeof(Char));
      return rlength;
    }, s.length());
  }
  template <typename S>
  MatchOtherString<S> append_(S &&s_) {
    ZuSpan<const Char> s(s_);
    append__([data = s.data()](Char *ptr, uint64_t rlength) {
      if (rlength) memcpy(ptr, data, rlength * sizeof(Char));
      return rlength;
    }, s.length());
  }
  template <typename C>
  MatchChar<C> append_(C c) {
    uint64_t n = length();
    Char *data = ensure(n + 2);
    data[n++] = c;
    length_(n);
  }

  template <typename S>
  MatchAltString<S> append_(const S &s_) {
    ZuSpan<const AltChar> s(s_);
    append__([s](Char *ptr, uint64_t rlength) -> uint64_t {
      if (!rlength) return 0;
      return ZuUTF<Char, AltChar>::cvt({ptr, rlength}, s);
    }, ZuUTF<Char, AltChar>::len(s));
  }
  template <typename C>
  MatchAltChar<C> append_(C c_) {
    AltChar c = c_;
    append__([c](Char *ptr, uint64_t rlength) {
      return ZuUTF<Char, AltChar>::cvt({ptr, rlength}, {&c, 1});
    }, ZuUTF<Char, AltChar>::len({&c, 1}));
  }

  template <typename P>
  MatchPDelegate<P> append_(P &&p) {
    ZuPrint<P>::print(*this, ZuFwd<P>(p));
  }
  template <typename P>
  MatchPBuffer<P> append_(P &&p) {
    uint64_t o = ZuPrint<P>::length(p);
    if (!o) return;
    if constexpr (ZuEquiv<Char, char>{}) {
      append__([&p](Char *ptr, uint64_t length) {
	return ZuPrint<P>::print(ptr, length, p);
      }, o);
    } else {
      auto buf = ZmAlloc(char, o);
      ZuCSpan s(&buf[0], ZuPrint<P>::print(&buf[0], o, p));
      append__([s](Char *ptr, uint64_t length) {
	return ZuUTF<Char, AltChar>::cvt({ptr, length}, s);
      }, ZuUTF<Char, AltChar>::len(s));
    }
  }

  template <typename Append>
  void append__(Append append, uint64_t length) {
    uint64_t n = this->length();
    length_(n + append(ensure(n + length + 1) + n, length));
  }

  template <typename V>
  MatchReal<V> append_(V v) {
    append_(ZuBoxed(v));
  }
  template <typename V>
  MatchPtr<V> append_(V v) {
    append_(ZuBoxPtr(v).hex<false, ZuFmt::Alt<>>());
  }

public:
  void append(const Char *data, uint64_t length) {
    if (!data) return;
    append__([data](Char *ptr, uint64_t rlength) {
      if (rlength) memcpy(ptr, data, rlength * sizeof(Char));
      return rlength;
    }, length);
  }

// splice()

  template <typename L, typename = void>
  struct IsCallable : public ZuFalse { };
  template <typename L>
  struct IsCallable<L, decltype(ZuDeclVal<L &>()(ZuDeclVal<ZuSpan<Char>>()))> :
    public ZuTrue { };

  template <typename Removed, typename Replace>
  void splice(
    Removed &&removed, int64_t offset, int64_t length,
    Replace &&replace, uint64_t rlength)
  {
    uint64_t n = this->length();
    uint64_t z = size_();

    if (offset < 0) { if ((offset += n) < 0) offset = 0; }
    if (length < 0) { if ((length += (n - offset)) < 0) length = 0; }

    if (offset > int64_t(n)) {
      if constexpr (IsCallable<Removed>{})
	removed(ZuSpan<Char>());
      else
	removed = {};
      Char *data;
      if (!mutable_() || offset + rlength >= int64_t(z)) {
	z = grow_(z, offset + rlength + 1);
	data = size(z);
      } else
	data = data_();
      Zu::strpad(data + n, offset - n);
      if (rlength)
	rlength = replace(ZuSpan(data + offset, rlength));
      length_(offset + rlength); // rlength may have been reduced
      return;
    }

    if (length == LLONG_MAX || offset + length > int64_t(n))
      length = n - offset;

    int64_t l = n + rlength - length;

    if (!mutable_() || l >= int64_t(z)) {
      z = l > 0 ? grow_(z, l + 1) : 1;
      Char *oldData = data_();
      if constexpr (IsCallable<Removed>{})
	removed(ZuSpan(oldData + offset, length));
      else
	removed = ZuSpan(oldData + offset, length);
      Char *newData;
      if (z <= BuiltinSize)
	newData = data__();
      else {
	newData = static_cast<Char *>(valloc(z * sizeof(Char)));
	if (!newData) throw std::bad_alloc{};
      }
      if (oldData != newData && offset)
	memcpy(newData, oldData, offset * sizeof(Char));
      if (rlength)
	rlength = replace(ZuSpan(newData + offset, rlength));
      l = n + rlength - length; // rlength may have been reduced
      if (offset + length < int64_t(n) &&
	  (oldData != newData || int64_t(rlength) != length))
	memmove(newData + offset + rlength,
		oldData + offset + length,
		(n - (offset + length)) * sizeof(Char));
      if (oldData != newData && vallocd()) vfree(oldData);
      newData[l] = 0;
      if (z <= BuiltinSize) {
	size_mutable_null(z, 1, 0);
	length_vallocd_builtin(l, 0, 1);
	return;
      }
      ptr__(newData);
      size_mutable_null(z, 1, 0);
      length_vallocd_builtin(l, 1, 0);
      return;
    }

    Char *data = data_();
    if constexpr (IsCallable<Removed>{})
      removed(ZuSpan(data + offset, length));
    else
      removed = ZuSpan(data + offset, length);
    if (l > 0) {
      int64_t tail = int64_t(n) - (offset + length);
      if (tail > 0 && int64_t(rlength) > length) {
	memmove(data + offset + rlength,
		data + offset + length,
		tail * sizeof(Char));
      }
      auto nrlength =
	rlength ? replace(ZuSpan(data + offset, rlength)) : 0;
      if (tail > 0) {
	if (int64_t(rlength) < length) {
	  memmove(data + offset + nrlength, // NOT rlength
		  data + offset + length,
		  tail * sizeof(Char));
	} else if (nrlength < rlength) {
	  memmove(data + offset + nrlength,
		  data + offset + rlength,
		  tail * sizeof(Char));
	}
      }
      l = n + nrlength - length;
    }
    length_(l);
  }
  void splice(int64_t offset) {
    splice([](ZuSpan<Char>) { }, offset, LLONG_MAX, [](ZuSpan<Char>) { return 0; }, 0);
  }
  void splice(int64_t offset, int64_t length) {
    splice([](ZuSpan<Char>) { }, offset, length, [](ZuSpan<Char>) { return 0; }, 0);
  }
  template <typename Removed>
  void splice(Removed &&removed, int64_t offset, int64_t length) {
    splice(ZuFwd<Removed>(removed), offset, length, [](ZuSpan<Char>) { return 0; }, 0);
  }
  template <typename S>
  MatchString<S>
  splice(int64_t offset, int64_t length, const S &s) {
    splice([](ZuSpan<Char>) { }, offset, length, s);
  }
  template <typename Removed, typename S>
  MatchString<S>
  splice(Removed &&removed, int64_t offset, int64_t length, const S &s) {
    auto n = s.length();
    if (ZuUnlikely(!n)) {
      splice(ZuFwd<Removed>(removed), offset, length,
	[](ZuSpan<Char>) -> uint64_t { return 0; }, 0);
      return;
    }
    if constexpr (ZuIsSame<S, String>{})
      if (ZuUnlikely(this == &s)) {
	auto buf = ZmAlloc(Char, n);
	memcpy(&buf[0], s.data_(), n * sizeof(Char));
	splice(ZuFwd<Removed>(removed), offset, length,
	  [data = &buf[0]](ZuSpan<Char> span) -> uint64_t {
	    auto n = span.length();
	    memcpy(span.data(), data, n * sizeof(Char));
	    return n;
	  }, n);
	return;
      }
    splice(ZuFwd<Removed>(removed), offset, length,
      [&s](ZuSpan<Char> span) -> uint64_t {
	auto n = span.length();
	if (n) memcpy(span.data(), s.data(), n * sizeof(Char));
	return n;
      }, n);
  }
  template <typename S>
  MatchAnyCString<S>
  splice(int64_t offset, int64_t length, const S &s) {
    splice([](ZuSpan<Char>) { }, offset, length, s);
  }
  template <typename Removed, typename S>
  MatchAnyCString<S>
  splice(Removed &&removed, int64_t offset, int64_t length, const S &s_) {
    ZuSpan<const Char> s(s_);
    splice(ZuFwd<Removed>(removed), offset, length,
      [s](ZuSpan<Char> span) -> uint64_t {
	auto n = span.length();
	if (n) memcpy(span.data(), s.data(), n * sizeof(Char));
	return n;
      }, s.length());
  }
  template <typename S>
  MatchOtherString<S>
  splice(int64_t offset, int64_t length, const S &s) {
    splice([](ZuSpan<Char>) { }, offset, length, s);
  }
  template <typename Removed, typename S>
  MatchOtherString<S>
  splice(Removed &&removed, int64_t offset, int64_t length, const S &s_) {
    ZuSpan<const Char> s(s_);
    splice(ZuFwd<Removed>(removed), offset, length,
      [s](ZuSpan<Char> span) -> uint64_t {
	auto n = span.length();
	if (n) memcpy(span.data(), s.data(), n * sizeof(Char));
	return n;
      }, s.length());
  }
  template <typename S>
  MatchAltString<S>
  splice(int64_t offset, int64_t length, const S &s) {
    splice([](ZuSpan<Char>) { }, offset, length, s);
  }
  template <typename Removed, typename S>
  MatchAltString<S>
  splice(Removed &&removed, int64_t offset, int64_t length, const S &s_) {
    ZuSpan<const AltChar> s(s_);
    splice(ZuFwd<Removed>(removed), offset, length,
      [s](ZuSpan<Char> span) -> uint64_t {
	if (!span.length()) return 0;
	return ZuUTF<Char, AltChar>::cvt(span, s);
      }, ZuUTF<Char, AltChar>::len(s));
  }
  template <typename C>
  MatchAltChar<C>
  splice(int64_t offset, int64_t length, C c) {
    splice([](ZuSpan<Char>) { }, offset, length, c);
  }
  template <typename Removed, typename C>
  MatchAltChar<C>
  splice(Removed &&removed, int64_t offset, int64_t length, C c_) {
    AltChar c = c_;
    splice(ZuFwd<Removed>(removed), offset, length,
      [c](ZuSpan<Char> span) -> uint64_t {
	if (!span.length()) return 0;
	return ZuUTF<Char, AltChar>::cvt(span, {&c, 1});
      }, ZuUTF<Char, AltChar>::len({&c, 1}));
  }

// shift() - simplified splice() for common use case

  void shift(uint64_t o) {
    if (ZuUnlikely(!o)) return;
    if (!mutable_()) truncate();
    uint64_t n = length();
    if (o >= n) { null(); return; }
    n -= o;
    Char *data = data_();
    memmove(data, data + o, n * sizeof(Char));
    length_(n);
  }

// chomp(), trim(), strip()

private:
  // match whitespace
  constexpr auto matchS() {
    return [](char c) constexpr {
      return ((c >= '\t' && c <= '\r') || c == ' ');
    };
  }
public:
  // remove trailing characters
  template <typename Match>
  void chomp(Match &&match) {
    if (!mutable_()) truncate();
    int64_t o = length();
    if (!o) return;
    Char *data = data_();
    while (--o >= 0 && match(data[o]));
    length_(o + 1);
  }
  void chomp() { return chomp(matchS()); }

  // remove leading characters
  template <typename Match>
  void trim(Match &&match) {
    if (!mutable_()) truncate();
    uint64_t n = length();
    uint64_t o;
    Char *data = data_();
    for (o = 0; o < n && match(data[o]); o++);
    if (!o) return;
    if (!(n -= o)) { null(); return; }
    memmove(data, data + o, n * sizeof(Char));
    length_(n);
  }
  void trim() { return trim(matchS()); }

  // remove leading & trailing characters
  template <typename Match>
  void strip(Match &&match) {
    if (!mutable_()) truncate();
    int64_t o = length();
    if (!o) return;
    Char *data = data_();
    while (--o >= 0 && match(data[o]));
    if (o < 0) { null(); return; }
    length_(o + 1);
    uint64_t n = o + 1;
    for (o = 0; o < int64_t(n) && match(data[o]); o++);
    if (!o) { length_(n); return; }
    if (!(n -= o)) { null(); return; }
    memmove(data, data + o, n * sizeof(Char));
    length_(n);
  }
  void strip() { return strip(matchS()); }
 
// sprintf(), vsprintf()

  String &sprintf(const Char *format, ...) {
    va_list args;

    va_start(args, format);
    vsnprintf(format, args);
    va_end(args);
    return *this;
  }
  String &vsprintf(const Char *format, va_list args) {
    vsnprintf(format, args);
    return *this;
  }

// growth algorithm

  void grow(uint64_t length) {
    uint64_t o = mutable_() ? size_() : 0;
    if (ZuLikely(length + 1 > o)) size(grow_(o, length + 1));
    o = this->length();
    if (ZuUnlikely(length > o)) length_(length);
  }

private:
  static uint64_t grow_(uint64_t o, uint64_t n) {
    if (n <= BuiltinSize) return BuiltinSize;
    return ZmGrow(o * sizeof(Char), n * sizeof(Char)) / sizeof(Char);
  }

  uint64_t vsnprintf_grow(uint64_t z) {
    z = grow_(z, z + String_vsnprintf_Growth);
    size(z);
    return z;
  }

public:
  void vsnprintf(const Char *format, va_list args) {
    uint64_t n = length();
    uint64_t z = size_();

    if (!mutable_() || n + 2 >= z)
      z = vsnprintf_grow(z);

retry:
    Char *data = data_();

    int r = Zu::vsnprintf(data + n, z - n, format, args);

    if (r < 0 || (n += r) == z || n == z - 1) {
      if (z >= String_vsnprintf_MaxSize) goto truncate;
      z = vsnprintf_grow(z);
      n = length();
      goto retry;
    }

    if (n > z) {
      if (z >= String_vsnprintf_MaxSize) goto truncate;
      size(z = grow_(z, n + 2));
      n = length();
      goto retry;
    }

    length_(n);
    return;

truncate:
    length_(z - 1);
  }

public:
  // traits
  struct Traits : public ZuBaseTraits<String> {
    using Elem = Char;
    enum {
      IsCString = 1, IsString = 1,
      IsWString = bool(ZuEquiv<wchar_t, Char>{})
    };
    static Char *data(String &s) { return s.data(); }
    static const Char *data(const String &s) { return s.data(); }
    static uint64_t length(const String &s) { return s.length(); }
  };
  friend Traits ZuTraitsType(String *);

private:
  alignas(void *) uint8_t	m_data[BuiltinSize * sizeof(Char)];
  uint64_t			m_size_mutable_null;
  uint64_t			m_length_vallocd_builtin;
};

template <typename Char, typename NTP>
template <typename S>
inline void String<Char, NTP>::convert_(const S &s, ZtIconv *iconv)
{
  null_();
  iconv->convert(*this, s);
}

} // Zt_

template <typename NTP = ZtString_Defaults>
using ZtString = Zt_::String<char, NTP>;
template <typename NTP = ZtString_Defaults>
using ZtWString = Zt_::String<wchar_t, NTP>;

// RVO printf shortcuts

#ifdef __GNUC__
template <typename S>
S ZtSprintf(const char *format, ...)
  __attribute__((format(printf, 1, 2)));
#endif
template <typename S>
inline S ZtSprintf(const char *format, ...) {
  va_list args;

  va_start(args, format);
  S s;
  s.vsprintf(format, args);
  va_end(args);
  return s;
}

#endif /* ZtString_HH */
