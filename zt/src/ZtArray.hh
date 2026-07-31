//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// heap-allocated dynamic array class
// - explicitly contiguous
// - lightweight
// - can shadow instead of owning/copying
// - shadows string literals without copying
// - uses ZmVHeap when not shadowing
// - provides direct read/write access to the buffer
// - zero-copy and deep-copy
// - ZtArray<T> where T is a byte is heavily overloaded as a string

// Note: use ZuArray<> for fixed-size arrays without heap overhead

#ifndef ZtArray_HH
#define ZtArray_HH

#ifndef ZtLib_HH
#include <zlib/ZtLib.hh>
#endif

#include <initializer_list>

#include <stdlib.h>
#include <string.h>

#include <zlib/ZuInt.hh>
#include <zlib/ZuTraits.hh>
#include <zlib/ZuCmp.hh>
#include <zlib/ZuHash.hh>
#include <zlib/ZuArrayFn.hh>
#include <zlib/ZuUTF.hh>
#include <zlib/ZuPrint.hh>
#include <zlib/ZuBox.hh>
#include <zlib/ZuEquiv.hh>
#include <zlib/ZuAlt.hh>

#include <zlib/ZmAssert.hh>
#include <zlib/ZmVHeap.hh>
#include <zlib/ZmAlloc.hh>

#include <zlib/ZtPlatform.hh>
#include <zlib/ZtIconv.hh>

// uses NTP (named template parameters):
//
// ZtArray<ZtString<>,			// array of ZtString<>s
//   ZtArrayCmp<ZuICmp>>		// case-insensitive comparison

// NTP defaults
struct ZtArray_Defaults {
  template <typename T> using CmpT = ZuCmp<T>;
  struct HeapID : public ZuStringT<"ZtArray"> { };
  enum { HeapMin = 32 };
  enum { HeapMax = 1024 };
  enum { Sharded = 0 };
};

// ZtArrayCmp - the comparator
template <template <typename> class Cmp_, typename NTP = ZtArray_Defaults>
struct ZtArrayCmp : public NTP {
  template <typename T> using CmpT = Cmp_<T>;
};

// ZtArrayHeapID - the heap ID
template <typename HeapID_, typename NTP = ZtArray_Defaults>
struct ZtArrayHeapID_ : public NTP {
  using HeapID = HeapID_;
};
template <ZuString HeapID, typename NTP = ZtArray_Defaults>
using ZtArrayHeapID = ZtArrayHeapID_<ZuStringT<HeapID>, NTP>;

// ZtArrayHeapMin - min size of heap allocation
template <unsigned Min_, typename NTP = ZtArray_Defaults>
struct ZtArrayHeapMin : public NTP {
  enum { HeapMin = Min_ };
};

// ZtArrayHeapMax - max size of heap allocation
template <unsigned Max_, typename NTP = ZtArray_Defaults>
struct ZtArrayHeapMax : public NTP {
  enum { HeapMax = Max_ };
};

// ZtArraySharded - heap sharding
template <bool Sharded_, typename NTP = ZtArray_Defaults>
struct ZtArraySharded : public NTP {
  enum { Sharded = Sharded_ };
};

template <typename T, typename NTP> class ZtArray;

template <typename T> struct ZtArray_ { };
template <> struct ZtArray_<char> {
  friend ZuPrintString ZuPrintType(ZtArray_ *);
};

template <typename T_, typename NTP = ZtArray_Defaults>
class ZtArray :
  private ZmVHeap_<
    typename NTP::HeapID,
    sizeof(T_) * NTP::HeapMin,
    sizeof(T_) * NTP::HeapMax,
    alignof(T_),
    NTP::Sharded>,
  public ZtArray_<ZuStrip<T_>>,
  public ZuArrayFn<T_, typename NTP::template CmpT<T_>>
{
  template <typename, typename> friend class ZtArray;

public:
  using T = T_;
  using Cmp = typename NTP::template CmpT<T>;
  using HeapID = typename NTP::HeapID;
  enum { HeapMin = NTP::HeapMin };
  enum { HeapMax = NTP::HeapMax };
  enum { Sharded = NTP::Sharded };
  using VHeap = ZmVHeap_<
    HeapID,
    sizeof(T) * HeapMin,
    sizeof(T) * HeapMax,
    alignof(T),
    Sharded>;
  using Ops = ZuArrayFn<T, Cmp>;

  using Ops::initElem;
  using Ops::initElems;
  using Ops::moveElems;
  using Ops::copyElems;
  using Ops::destroyElem;
  using Ops::destroyElems;

  struct Move { };

  using Char = T;
  using AltChar = ZuAlt<T>;

  // from equivalent ZtArray
  template <typename U, typename V = T>
  struct IsZtArray : public ZuBool<
    bool(ZuEquiv<typename ZuTraits<U>::Elem, V>{}) &&
    bool(ZuIsBase<U, ZtArray_<ZuStrip<typename ZuTraits<U>::Elem>>>{})> { };
  template <typename U, typename R = void>
  using MatchZtArray = ZuIfT<IsZtArray<U>{}, R>;

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

  // from some other string with equivalent char (including string literals)
  template <typename U, typename V = Char>
  struct IsAnyString : public ZuBool<
    !IsZtArray<U>{} &&
    (ZuTraits<U>::IsSpan || ZuTraits<U>::IsString) &&
    bool(ZuEquiv<typename ZuTraits<U>::Elem, V>{})> { };
  template <typename U, typename R = void>
  using MatchAnyString = ZuIfT<IsAnyString<U>{}, R>;

  // from some other string with equivalent char (other than a string literal)
  template <typename U, typename V = Char>
  struct IsString : public ZuBool<
    !IsStrLiteral<U>{} && bool(IsAnyString<U, V>{})> { };
  template <typename U, typename R = void>
  using MatchString = ZuIfT<IsString<U>{}, R>;

  // from char2 string (requires conversion)
  template <typename U, typename V = AltChar>
  struct IsAltString : public ZuBool<
    !ZuIsSame<V, void>{} && bool(IsString<U, V>{})> { };
  template <typename U, typename R = void>
  using MatchAltString = ZuIfT<IsAltString<U>{}, R>;

  // from another array type with convertible element type (not a string)
  template <typename U, typename V = T>
  struct IsSpan : public ZuBool<
    !IsZtArray<U>{} &&
    !IsAnyString<U>{} &&
    !IsAltString<U>{} &&
    !ZuIsSame<ZuDecay<U>, V>{} &&
    ZuTraits<U>::IsSpan &&
    bool(ZuIsConvertible<typename ZuTraits<U>::Elem, V>{})> { };
  template <typename U, typename R = void>
  using MatchSpan = ZuIfT<IsSpan<U>{}, R>;

  // from another array type with same element type (not a string)
  template <typename U, typename V = T>
  struct IsSameSpan : public ZuBool<
    !IsZtArray<U>{} &&
    !IsAnyString<U>{} &&
    !IsAltString<U>{} &&
    !ZuIsSame<ZuDecay<U>, V>{} &&
    ZuTraits<U>::IsSpan &&
    bool(ZuIsSame<typename ZuTraits<U>::Elem, V>{})> { };
  template <typename U, typename R = void>
  using MatchSameSpan = ZuIfT<IsSameSpan<U>{}, R>;

  // is this array a string?
  template <typename V = T>
  struct ThisIsString : public ZuBool<
    (bool(ZuEquiv<V, char>{}) || bool(ZuIsSame<ZuDecay<V>, wchar_t>{}))> { };

  // from individual elem
  template <typename U, typename V = T>
  struct IsElem_ : public ZuBool<
    bool(ZuIsSame<V, wchar_t>{}) ?
      bool(ZuIsSame<ZuDecay<U>, V>{}) :
      bool(ZuEquiv<U, V>{})> { };

  // from individual char2 (requires conversion)
  template <typename U, typename V = AltChar>
  struct IsAltChar : public ZuBool<
    !ZuIsSame<V, void>{} && bool(IsElem_<U, V>{})> { };
  template <typename U, typename R = void>
  using MatchAltChar = ZuIfT<IsAltChar<U>{}, R>;

  // from printable type (if this is a string)
  template <typename U, typename V = T>
  struct IsPrint : public ZuBool<
    bool(ThisIsString<V>{}) &&
    ZuPrint<U>::OK && !ZuPrint<U>::String> { };
  template <typename U, typename R = void>
  using MatchPrint = ZuIfT<IsPrint<U>{}, R>;
  template <typename U, typename V = T>
  struct IsPDelegate : public ZuBool<
    bool(ThisIsString<V>{}) && ZuPrint<U>::Delegate> { };
  template <typename U, typename R = void>
  using MatchPDelegate = ZuIfT<IsPDelegate<U>{}, R>;
  template <typename U, typename V = T>
  struct IsPBuffer : public ZuBool<
    bool(ThisIsString<V>{}) && ZuPrint<U>::Buffer> { };
  template <typename U, typename R = void>
  using MatchPBuffer = ZuIfT<IsPBuffer<U>{}, R>;

  // from any STL iterable with convertible element type (not array or string)
  template <typename U, typename = void>
  struct IsIterable_ : public ZuFalse { };
  template <typename U>
  struct IsIterable_<U, decltype(
    ZuDeclVal<const U &>().end() - ZuDeclVal<const U &>().begin(), void())> :
      public ZuTrue { };
  template <typename U, typename V = T>
  struct IsIterable : public ZuBool<
    !IsZtArray<U>{} &&
    !IsAnyString<U>{} &&
    !IsAltString<U>{} &&
    !IsPrint<U>{} &&
    !ZuIsSame<ZuDecay<U>, V>{} &&
    !ZuTraits<U>::IsSpan &&
    bool(IsIterable_<ZuDecay<U>>{}) &&
    bool(ZuIsConstructible<typename ZuTraits<U>::Elem, V>{})> { };
  template <typename U, typename R = void>
  using MatchIterable = ZuIfT<IsIterable<U>{}, R>;

  // from real primitive types other than chars (if this is a string)
  template <typename U, typename V = T>
  struct IsReal : public ZuBool<
    bool(ThisIsString<V>{}) &&
    !IsElem_<U>{} && !IsAltChar<U>{} &&
    ZuTraits<U>::IsReal && ZuTraits<U>::IsPrimitive &&
    !ZuTraits<U>::IsArray> { };
  template <typename U, typename R = void>
  using MatchReal = ZuIfT<IsReal<U>{}, R>;

  // from primitive pointer (not an array, string, or otherwise printable)
  template <typename U, typename V = T>
  struct IsPtr : public ZuBool<
    bool(ThisIsString<V>{}) &&
    ZuTraits<U>::IsPointer && ZuTraits<U>::IsPrimitive &&
    !ZuTraits<U>::IsArray && !ZuTraits<U>::IsString> { };
  template <typename U, typename R = void>
  using MatchPtr = ZuIfT<IsPtr<U>{}, R>;

  // from individual element
  template <typename U, typename V = T>
  struct IsElem : public ZuBool<
    bool(IsElem_<U>{}) ||
      (!IsZtArray<U>{} &&
       !IsString<U>{} &&
       !ZuTraits<U>::IsArray &&	// broader than !IsSpan
       !IsAltChar<U>{} &&
       !IsPrint<U>{} &&
       !IsReal<U>{} &&
       !IsPtr<U>{} &&
       bool(ZuIsConvertible<U, V>{}))> { };
  template <typename U, typename R = void>
  using MatchElem = ZuIfT<IsElem<U>{}, R>;

  // limit member operator <<() overload resolution to supported types
  template <typename U>
  struct IsStreamable : public ZuBool<
    bool(IsZtArray<U>{}) ||
    bool(IsSpan<U>{}) ||
    bool(IsAnyString<U>{}) ||
    bool(IsAltString<U>{}) ||
    bool(IsAltChar<U>{}) ||
    bool(IsPDelegate<U>{}) ||
    bool(IsPBuffer<U>{}) ||
    bool(IsReal<U>{}) ||
    bool(IsPtr<U>{}) ||
    bool(IsElem<U>{})> { };
  template <typename U, typename R = void>
  using MatchStreamable = ZuIfT<IsStreamable<U>{}, R>;

  // an integer parameter to the constructor is a buffer size
  // - except for character element types
  template <typename U, typename V = T>
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

  // construction from individual element
  template <typename U, typename V = T, typename W = AltChar>
  struct IsCtorElem : public ZuBool<
    !IsZtArray<U>{} &&
    !IsString<U>{} &&
    !ZuTraits<U>::IsArray &&	// broader than !IsSpan
    !IsAltChar<U>{} &&
    !IsPrint<U>{} &&
    !IsReal<U>{} &&
    !IsPtr<U>{} &&
    !IsCtorSize<U>{} &&
    bool(ZuIsConvertible<U, V>{})> { };
  template <typename U, typename R = void>
  using MatchCtorElem = ZuIfT<IsCtorElem<U>{}, R>;

  ZtArray() noexcept(ZuNXConstruct<T>{}) { null_(); }
  ZtArray(const ZtArray &a) noexcept(ZuNXCopy<T>{}) { ctor(a); }

  // note that the heap parameters form part of the type, so moving will
  // only happen among identically-typed arrays; this is both intentional
  // and important for heap instrumentation and tuning
  ZtArray(ZtArray &&a) noexcept(ZuNXMove<T>{}) {
    if (!a.mutable_())
      shadow_(a.m_data, a.length());
    else {
      own_(a.m_data, a.length(), a.size(), a.vallocd());
      a.mutable_(false);
    }
  }
  ZtArray(std::initializer_list<T> a) {
    copy__(a.begin(), a.size());
  }

  template <typename A> ZtArray(A &&a) { ctor(ZuFwd<A>(a)); }

  template <typename A>
  ZtArray(Move, A &a_) {
    ZuSpan<const typename ZuTraits<A>::Elem> a(a_);
    move__(a.data(), a.length());
  }

private:
  using VHeap::valloc;
  using VHeap::vfree;

  template <typename A_> struct Fwd_ZtArray {
    using A = ZuDecay<A_>;

    static void ctor_(ZtArray *this_, const A &a) {
      this_->copy__(a.m_data, a.length());
    }
    static void ctor_(ZtArray *this_, A &&a) {
      if (!a.mutable_())
	this_->shadow_(reinterpret_cast<T *>(a.m_data), a.length());
      else {
	this_->own_(
	    reinterpret_cast<T *>(a.m_data), a.length(), a.size(), a.vallocd());
	a.mutable_(false);
      }
    }

    static void assign_(ZtArray *this_, const A &a) {
      uint64_t oldLength = 0;
      T *oldData = this_->free_1(oldLength);
      this_->copy__(a.m_data, a.length());
      this_->free_2(oldData, oldLength);
    }
    static void assign_(ZtArray *this_, A &&a) {
      this_->free_();
      if (!a.mutable_())
	this_->shadow_(reinterpret_cast<T *>(a.m_data), a.length());
      else {
	this_->own_(
	    reinterpret_cast<T *>(a.m_data), a.length(), a.size(), a.vallocd());
	a.mutable_(false);
      }
    }

    static ZtArray add_(const ZtArray *this_, const A &a) {
      return this_->add_(a.m_data, a.length());
    }
    static ZtArray add_(const ZtArray *this_, A &&a) {
      return this_->add_mv(a.m_data, a.length());
    }

    template <typename Removed>
    static void splice_(ZtArray *this_,
	Removed &&removed, int64_t offset, int64_t length, const A &a) {
      auto n = a.length();
      if (ZuUnlikely(!n)) {
	this_->splice(ZuFwd<Removed>(removed), offset, length,
	  [](ZuSpan<T>) -> uint64_t { return 0; }, 0);
	return;
      }
      if constexpr (ZuIsSame<A, ZtArray>{})
	if (ZuUnlikely(this_ == &a)) {
	  auto buf = ZmAlloc(T, n);
	  copyElems(&buf[0], a.m_data, n);
	  this_->splice(ZuFwd<Removed>(removed), offset, length,
	    [data = &buf[0]](ZuSpan<T> span) -> uint64_t {
	      auto n = span.length();
	      if (n) moveElems(span.data(), data, n);
	      return n;
	    }, n);
	  destroyElems(&buf[0], n);
	  return;
	}
      this_->splice(ZuFwd<Removed>(removed), offset, length,
	[data = a.m_data](ZuSpan<T> span) -> uint64_t {
	  auto n = span.length();
	  if (n) copyElems(span.data(), data, n);
	  return n;
	}, n);
    }
    template <typename Removed>
    static void splice_(ZtArray *this_,
	Removed &&removed, int64_t offset, int64_t length, A &&a) {
      this_->splice(ZuFwd<Removed>(removed), offset, length,
	[data = a.m_data](ZuSpan<T> span) -> uint64_t {
	  auto n = span.length();
	  if (n) moveElems(span.data(), data, n);
	  return n;
	}, a.length());
    }
  };
  template <typename A_> struct Fwd_Array {
    using A = ZuDecay<A_>;
    using Elem = typename ZuTraits<A>::Elem;

    static void ctor_(ZtArray *this_, const A &a_) {
      ZuSpan<const Elem> a(a_);
      this_->copy__(a.data(), a.length());
    }
    static void ctor_(ZtArray *this_, A &&a_) {
      ZuSpan<Elem> a(a_);
      this_->move__(a.data(), a.length());
    }

    static void assign_(ZtArray *this_, const A &a_) {
      ZuSpan<const Elem> a(a_);
      uint64_t oldLength = 0;
      T *oldData = this_->free_1(oldLength);
      this_->copy__(a.data(), a.length());
      this_->free_2(oldData, oldLength);
    }
    static void assign_(ZtArray *this_, A &&a_) {
      ZuSpan<Elem> a(a_);
      uint64_t oldLength = 0;
      T *oldData = this_->free_1(oldLength);
      this_->move__(a.data(), a.length());
      this_->free_2(oldData, oldLength);
    }

    static ZtArray add_(const ZtArray *this_, const A &a_) {
      ZuSpan<const Elem> a(a_);
      return this_->add_(a.data(), a.length());
    }
    static ZtArray add_(const ZtArray *this_, A &&a_) {
      ZuSpan<Elem> a(a_);
      return this_->add_mv(a.data(), a.length());
    }

    template <typename Removed>
    static void splice_(ZtArray *this_,
	Removed &&removed, int64_t offset, int64_t length, const A &a_) {
      ZuSpan<const Elem> a(a_);
      this_->splice(ZuFwd<Removed>(removed), offset, length,
	[data = a.data()](ZuSpan<T> span) -> uint64_t {
	  auto n = span.length();
	  if (n) copyElems(span.data(), data, n);
	  return n;
	}, a.length());
    }
    template <typename Removed>
    static void splice_(ZtArray *this_,
	Removed &&removed, int64_t offset, int64_t length, A &&a_) {
      ZuSpan<Elem> a(a_);
      this_->splice(ZuFwd<Removed>(removed), offset, length,
	[data = a.data()](ZuSpan<T> span) -> uint64_t {
	  auto n = span.length();
	  if (n) moveElems(span.data(), data, n);
	  return n;
	}, a.length());
    }
  };

  template <typename A>
  MatchZtArray<A> ctor(A &&a) {
    Fwd_ZtArray<A &&>::ctor_(this, ZuFwd<A>(a));
  }
  template <typename A>
  MatchSpan<A> ctor(A &&a) {
    Fwd_Array<A>::ctor_(this, ZuFwd<A>(a));
  }
  template <typename A>
  MatchIterable<A> ctor(const A &a) {
    null_();
    auto i = a.begin();
    uint64_t n = a.end() - i;
    this->size(n);
    for (uint64_t j = 0; j < n; j++) initElem(push(), *i++);
  }

  template <typename S> MatchStrLiteral<S> ctor(S &&s_) {
    ZuSpan<const T> s(s_);
    shadow_(s.data(), s.length());
  }
  template <typename S> MatchString<S> ctor(S &&s_) {
    ZuSpan<const T> s(s_);
    copy__(s.data(), s.length());
  }

  template <typename S> MatchAltString<S> ctor(S &&s_) {
    ZuSpan<const AltChar> s(s_);
    uint64_t o = ZuUTF<Char, AltChar>::len(s);
    if (!o) { null_(); return; }
    alloc_(o, 0);
    length_(ZuUTF<Char, AltChar>::cvt({m_data, o}, s));
  }
  template <typename C> MatchAltChar<C> ctor(C c) {
    ZuSpan<const AltChar> s(&c, 1);
    uint64_t o = ZuUTF<Char, AltChar>::len(s);
    if (!o) { null_(); return; }
    alloc_(o, 0);
    length_(ZuUTF<Char, AltChar>::cvt({m_data, o}, s));
  }

  template <typename P> MatchCtorPDelegate<P> ctor(const P &p) {
    null_();
    ZuPrint<P>::print(*this, p);
  }
  template <typename P> MatchCtorPBuffer<P> ctor(const P &p) {
    unsigned o = ZuPrint<P>::length(p);
    if (!o) { null_(); return; }
    if constexpr (ZuEquiv<Char, char>{}) {
      alloc_(o, 0);
      length_(ZuPrint<P>::print(reinterpret_cast<char *>(m_data), o, p));
    } else {
      auto buf = ZmAlloc(char, o);
      ZuCSpan s(&buf[0], ZuPrint<P>::print(&buf[0], o, p));
      o = ZuUTF<Char, AltChar>::len(s);
      if (!o) { null_(); return; }
      alloc_(o, 0);
      length_(ZuUTF<Char, AltChar>::cvt({m_data, o}, s));
    }
  }

  template <typename V> MatchCtorSize<V> ctor(V size) {
    if (!size) { null_(); return; }
    alloc_(size, 0);
  }

  template <typename R> MatchCtorElem<R> ctor(R &&r) {
    uint64_t z = grow_(0, 1);
    m_data = alloc__(z);
    if (!m_data) throw std::bad_alloc{};
    size_mutable(z, 1);
    length_vallocd(1, 1);
    initElem(m_data, ZuFwd<R>(r));
  }

public:
  template <typename A> MatchZtArray<A> copy(const A &a) {
    copy__(a.m_data, a.length());
  }
  template <typename A> MatchSpan<A> copy(A &&a_) {
    ZuSpan<const typename ZuTraits<A>::Elem> a(a_);
    copy__(a.data(), a.length());
  }
  template <typename A> MatchIterable<A> copy(const A &a) {
    assign(a);
  }

  template <typename S> MatchAnyString<S> copy(S &&s) {
    ctor(ZuFwd<S>(s));
  }
  template <typename S> MatchAltString<S> copy(S &&s) {
    ctor(ZuFwd<S>(s));
  }
  template <typename C> MatchAltChar<C> copy(C c) {
    ctor(c);
  }
  template <typename R> MatchElem<R> copy(R &&r) {
    ctor(ZuFwd<R>(r));
  }

public:
  ZtArray &operator =(const ZtArray &a) noexcept(ZuNXCopy<T>{}) {
    assign(a);
    return *this;
  }
  ZtArray &operator =(ZtArray &&a) noexcept(ZuNXMove<T>{}) {
    this->~ZtArray();
    new (this) ZtArray(ZuMv(a));
    return *this;
  }

  template <typename A>
  ZtArray &operator =(A &&a) { assign(ZuFwd<A>(a)); return *this; }

  ZtArray &operator =(std::initializer_list<T> a) {
    uint64_t oldLength = 0;
    T *oldData = free_1(oldLength);
    copy__(a.begin(), a.size());
    free_2(oldData, oldLength);
    return *this;
  }

protected:
  template <typename A> MatchZtArray<A> assign(A &&a) {
    Fwd_ZtArray<A>::assign_(this, ZuFwd<A>(a));
  }
  template <typename A> MatchSpan<A> assign(A &&a) {
    Fwd_Array<A>::assign_(this, ZuFwd<A>(a));
  }
  template <typename A>
  MatchIterable<A> assign(const A &a) {
    auto i = a.begin();
    uint64_t n = a.end() - i;
    length(0);
    ensure(n);
    for (uint64_t j = 0; j < n; j++)
      initElem(push(), *i++);
  }

  template <typename S> MatchStrLiteral<S> assign(S &&s_) {
    ZuSpan<const T> s(s_);
    free_();
    shadow_(s.data(), s.length());
  }
  template <typename S> MatchString<S> assign(S &&s_) {
    ZuSpan<const T> s(s_);
    uint64_t oldLength = 0;
    T *oldData = free_1(oldLength);
    copy__(s.data(), s.length());
    free_2(oldData, oldLength);
  }

  template <typename S> MatchAltString<S> assign(S &&s_) {
    ZuSpan<const AltChar> s(s_);
    uint64_t o = ZuUTF<Char, AltChar>::len(s);
    if (!o) { null(); return; }
    if (!mutable_() || size() < o) size(o);
    length_(ZuUTF<Char, AltChar>::cvt({m_data, o}, s));
  }
  template <typename C> MatchAltChar<C> assign(C c) {
    ZuSpan<const AltChar> s(&c, 1);
    uint64_t o = ZuUTF<Char, AltChar>::len(s);
    if (!o) { null(); return; }
    if (!mutable_() || size() < o) size(o);
    length_(ZuUTF<Char, AltChar>::cvt({m_data, o}, s));
  }

  template <typename P> MatchPDelegate<P> assign(P &&p) {
    ZuPrint<P>::print(*this, ZuFwd<P>(p));
  }
  template <typename P>
  MatchPBuffer<P> assign(const P &p) {
    uint64_t o = ZuPrint<P>::length(p);
    if (!o) { null(); return; }
    if constexpr (ZuEquiv<Char, char>{}) {
      ensure(o);
      length_(ZuPrint<P>::print(reinterpret_cast<char *>(m_data), o, p));
    } else {
      auto buf = ZmAlloc(char, o);
      ZuCSpan s(&buf[0], ZuPrint<P>::print(&buf[0], o, p));
      o = ZuUTF<Char, AltChar>::len(s);
      if (!o) { null_(); return; }
      ensure(o);
      length_(ZuUTF<Char, AltChar>::cvt({m_data, o}, s));
    }
  }

  template <typename V> MatchReal<V> assign(V v) {
    assign(ZuBoxed(v));
  }
  template <typename V> MatchPtr<V> assign(V v) {
    assign(ZuBoxPtr(v).hex<false, ZuFmt::Alt<>>());
  }

  template <typename V> MatchElem<V> assign(V &&v) {
    free_();
    ctor(ZuFwd<V>(v));
  }

public:
  template <typename A> ZtArray &operator -=(A &&a) {
    shadow(ZuFwd<A>(a));
    return *this;
  }

private:
  template <typename A> MatchZtArray<A> shadow(const A &a) {
    if constexpr (ZuIsSame<A, ZtArray>{})
      if (this == &a) return;
    free_();
    shadow_(a.m_data, a.length());
  }
  template <typename A> MatchSameSpan<A> shadow(A &&a_) {
    ZuSpan<const typename ZuTraits<A>::Elem> a(a_);
    free_();
    shadow_(a.data(), a.length());
  }

public:
  template <typename S, ZuMatchString<S, int> = 0>
  ZtArray(S &&s_, ZtIconv *iconv) {
    ZuSpan<const typename ZuTraits<S>::Elem> s(s_);
    convert_(s, iconv);
  }
  ZtArray(const Char *data, uint64_t length, ZtIconv *iconv) {
    ZuSpan<const Char> s(data, length);
    convert_(s, iconv);
  }
  ZtArray(const AltChar *data, uint64_t length, ZtIconv *iconv) {
    ZuSpan<const AltChar> s(data, length);
    convert_(s, iconv);
  }

  ZtArray(uint64_t length, uint64_t size,
      bool initElems_ = !ZuTraits<T>::IsPrimitive) {
    if (!size) { null_(); return; }
    alloc_(size, length);
    if (initElems_) initElems(m_data, length);
  }
  explicit ZtArray(const T *data, uint64_t length) {
    if (!length) { null_(); return; }
    copy__(data, length);
  }
  explicit ZtArray(Move, T *data, uint64_t length) {
    if (!length) { null_(); return; }
    move__(data, length);
  }
  explicit ZtArray(
      const T *data, uint64_t length, uint64_t size, bool vallocd) {
    if (!data) { null_(); return; }
    own_(data, length, size, vallocd);
  }

  ~ZtArray() noexcept(ZuNXDestroy<T>{}) { free_(); }

// re-initializers

  void init() { free_(); init_(); }
  void init_() { null_(); }

  template <typename A> void init(A &&a) { assign(ZuFwd<A>(a)); }
  template <typename A> void init_(A &&a) { ctor(ZuFwd<A>(a)); }

  void init(
    uint64_t length, uint64_t size,
    bool initElems_ = !ZuTraits<T>::IsPrimitive)
  {
    if (this->size() < size || initElems) {
      free_();
      alloc_(size, length);
    } else
      length_(length);
    if (initElems_) initElems(m_data, length);
  }
  void init_(
    uint64_t length, uint64_t size,
    bool initElems_ = !ZuTraits<T>::IsPrimitive)
  {
    if (!size) { null_(); return; }
    alloc_(size, length);
    if (initElems_) initElems(m_data, length);
  }
  void copy(const T *data, uint64_t length) {
    uint64_t oldLength = 0;
    T *oldData = free_1(oldLength);
    copy__(data, length);
    free_2(oldData, oldLength);
  }
  void move(T *data, uint64_t length) {
    uint64_t oldLength = 0;
    T *oldData = free_1(oldLength);
    move__(data, length);
    free_2(oldData, oldLength);
  }
  void copy_(const T *data, uint64_t length) {
    if (!length) { null_(); return; }
    copy__(data, length);
  }
  void move_(T *data, uint64_t length) {
    if (!length) { null_(); return; }
    move__(data, length);
  }
  void init(
      const T *data, uint64_t length, uint64_t size, bool vallocd) {
    free_();
    init_(data, length, size, vallocd);
  }
  void init_(
      const T *data, uint64_t length, uint64_t size, bool vallocd) {
    if (!data) { null_(); return; }
    own_(data, length, size, vallocd);
  }

// internal initializers / finalizer

protected:
  void null_() {
    m_size_mutable = 0;
    m_length_vallocd = 0;
    m_data = nullptr;
  }

  void own_(const T *data, uint64_t length, uint64_t size, bool vallocd) {
    ZmAssert(size >= length);
    if (!size) {
      if (data && vallocd) vfree(data);
      null_();
      return;
    }
    own__(data, length, size, vallocd);
  }
  void own__(const T *data, uint64_t length, uint64_t size, bool vallocd) {
    size_mutable(size, 1);
    length_vallocd(length, vallocd);
    m_data = const_cast<T *>(data);
  }

  void shadow_(const T *data, uint64_t length) {
    if (!length) { null_(); return; }
    shadow__(data, length);
  }
  void shadow__(const T *data, uint64_t length) {
    size_mutable(length, 0);
    length_vallocd(length, 0);
    m_data = const_cast<T *>(data);
  }

  void alloc_(uint64_t size, uint64_t length) {
    if (!size) { null_(); return; }
    m_data = alloc__(size);
    if (!m_data) throw std::bad_alloc{};
    size_mutable(size, 1);
    length_vallocd(length, 1);
  }

  T *alloc__(uint64_t length) {
    auto ptr = static_cast<T *>(valloc(length * sizeof(T)));
    ZmAssert(!(reinterpret_cast<uintptr_t>(ptr) & (alignof(T) - 1)));
    return ptr;
  }

  template <typename U> void copy__(const U *data, uint64_t length) {
    if (!length) { null_(); return; }
    m_data = alloc__(length);
    if (!m_data) throw std::bad_alloc{};
    copy___(data, length);
  }
  template <typename U> void copy___(const U *data, uint64_t length) {
    copyElems(m_data, data, length);
    size_mutable(length, 1);
    length_vallocd(length, 1);
  }

  template <typename U> void move__(U *data, uint64_t length) {
    if (!length) { null_(); return; }
    m_data = alloc__(length);
    if (!m_data) throw std::bad_alloc{};
    move___(data, length);
  }
  template <typename U> void move___(U *data, uint64_t length) {
    this->template moveElems<false>(m_data, data, length);
    size_mutable(length, 1);
    length_vallocd(length, 1);
  }

  template <typename S> void convert_(const S &s, ZtIconv *iconv);

  void free_() {
    if (m_data && mutable_()) {
      destroyElems(m_data, length());
      if (vallocd()) vfree(m_data);
    }
  }
  T *free_1(uint64_t &length_vallocd) {
    if (!m_data || !mutable_()) return 0;
    length_vallocd = m_length_vallocd;
    return m_data;
  }
  void free_2(T *data, uint64_t length_vallocd) {
    if (data) {
      destroyElems(data, length_vallocd & ~(uint64_t(1)<<63));
      if (length_vallocd>>63) vfree(data);
    }
  }

public:
// truncation (to minimum size)
  void truncate() {
    uint64_t n = length();
    if (!n) { null(); return; }
    size(n);
    if (!m_data || size() <= n) return;
    T *newData = alloc__(n);
    if (!newData) throw std::bad_alloc{};
    this->template moveElems<false>(newData, m_data, n);
    free_();
    m_data = newData;
    vallocd(1);
    size_mutable(length(), 1);
  }

// array / ptr operators
  T &operator [](uint64_t i) { return m_data[i]; }
  const T &operator [](uint64_t i) const { return m_data[i]; }

// accessors
  T *data() { return m_data; }
  const T *data() const { return m_data; }

  uint64_t length() const { return m_length_vallocd & ~(uint64_t(1)<<63); }
  uint64_t size() const { return m_size_mutable & ~(uint64_t(1)<<63); }

  bool vallocd() const { return m_length_vallocd>>63; }
  bool mutable_() const { return m_size_mutable>>63; }

// direct buffer access
  auto span() { return ZuSpan(m_data, length()); }
  auto cspan() const { return ZuSpan(m_data, length()); }

// iteration - all() is const by default, all<true>() is mutable
  template <bool Mutable = false, typename L>
  ZuIfT<!Mutable> all(L &&l) const {
    for (uint64_t i = 0, n = length(); i < n; i++) ZuFwd<L>(l)(m_data[i]);
  }
  template <bool Mutable, typename L>
  ZuIfT<Mutable> all(L &&l) {
    for (uint64_t i = 0, n = length(); i < n; i++) ZuFwd<L>(l)(m_data[i]);
  }

// find (forwards to ZuSpan)
  template <typename Arg>
  int64_t find(Arg &&arg) const {
    return cspan().find(ZuFwd<Arg>(arg));
  }
  template <ZuString S, typename V = T>
  ZuIfT<ZuEquiv<V, char>{}, int64_t>
  find() const { return cspan().template find<S>(); }

// match at start (forwards to ZuSpan)
  template <typename Arg>
  auto match(Arg &&arg) const {
    return cspan().match(ZuFwd<Arg>(arg));
  }
  template <ZuString S, typename V = T>
  ZuIfT<ZuEquiv<V, char>{}, bool>
  match() const { return cspan().template match<S>(); }

protected:
  void length_(uint64_t v) {
    m_length_vallocd =
      (m_length_vallocd & (uint64_t(1)<<63)) | uint64_t(v);
  }
  void vallocd(bool v) {
    m_length_vallocd =
      (m_length_vallocd & ~(uint64_t(1)<<63)) | (uint64_t(v)<<63);
  }
  void length_vallocd(uint64_t l, bool m) {
    m_length_vallocd = l | ((uint64_t(m))<<63);
  }
  void size_(uint64_t v) {
    m_size_mutable = (m_size_mutable & (uint64_t(1)<<63)) | uint64_t(v);
  }
  void mutable_(bool v) {
    m_size_mutable =
      (m_size_mutable & ~(uint64_t(1)<<63)) | (uint64_t(v)<<63);
  }
  void size_mutable(uint64_t z, bool o) {
    m_size_mutable = z | (uint64_t(o)<<63);
  }

public:
// release / free
  T *release() && {
    mutable_(0);
    return m_data;
  }
  static void free(const T *ptr) { vfree(ptr); }

// reset to null array
  void null() {
    free_();
    null_();
  }

// reset without freeing
  void clear() {
    if (!mutable_()) { null_(); return; }
    if constexpr (!ZuTraits<T>::IsPrimitive)
      if (uint64_t n = this->length())
	destroyElems(m_data, n);
    length_(0);
  }

// set length
  void length(uint64_t length) {
    if (!mutable_() || length > size()) size(length);
    if constexpr (!ZuTraits<T>::IsPrimitive) {
      uint64_t n = this->length();
      if (length > n) {
	initElems(m_data + n, length - n);
      } else if (length < n) {
	destroyElems(m_data + length, n - length);
      }
    }
    length_(length);
  }
  void length(uint64_t length, bool initElems_) {
    if (!mutable_() || length > size()) size(length);
    if (initElems_) {
      uint64_t n = this->length();
      if (length > n) {
	initElems(m_data + n, length - n);
      } else if (length < n) {
	destroyElems(m_data + length, n - length);
      }
    }
    length_(length);
  }

// ensure size
  T *ensure(uint64_t o) {
    uint64_t z = size();
    if (ZuLikely(mutable_() && o <= z)) return m_data;
    return size(grow_(z, o));
  }

// set size
  T *size(uint64_t z) {
    if (!z) { null(); return 0; }
    if (mutable_() && z == size()) return m_data;
    T *newData = alloc__(z);
    if (!newData) throw std::bad_alloc{};
    uint64_t n = z;
    if (n > length()) n = length();
    if (m_data) {
      if (n) this->template moveElems<false>(newData, m_data, n);
      free_();
    }
    m_data = newData;
    size_mutable(z, 1);
    length_vallocd(n, 1);
    return newData;
  }

// set element i, extending array as needed
  void *set(uint64_t i) {
    uint64_t n = length();
    if (ZuLikely(i < n)) {
      destroyElem(m_data + i);
      return m_data + i;
    }
    uint64_t z = size();
    if (!mutable_() || i + 1 > z) {
      z = grow_(z, i + 1);
      T *newData = alloc__(z);
      if (!newData) throw std::bad_alloc{};
      this->template moveElems<false>(newData, m_data, n);
      free_();
      m_data = newData;
      size_mutable(z, 1);
      if (i > n) initElems(m_data + n, i - n);
      length_vallocd(i + 1, 1);
    } else {
      if (i > n) initElems(m_data + n, i - n);
      length_(i + 1);
    }
    return m_data + i;
  }
  template <typename V> void set(uint64_t i, V &&v) {
    auto ptr = set(i);
    initElem(ptr, ZuFwd<V>(v));
  }

  const T *getPtr(uint64_t i) const {
    if (ZuUnlikely(i >= length())) return nullptr;
    return static_cast<const T *>(m_data + i);
  }
  const T &get(uint64_t i) const {
    if (ZuUnlikely(i >= length())) return ZuNullRef<T, Cmp>();
    return m_data[i];
  }

// comparison
  bool operator !() const { return !length(); }
  ZuOpBool

  bool same(const ZtArray &a) const { return this == &a; }
  template <typename A> constexpr bool same(const A &) const { return false; }

  template <typename A>
  bool equals(const A &a) const {
    return same(a) || cspan().equals(a);
  }
  template <typename A>
  int cmp(const A &a) const {
    if (same(a)) return 0;
    return cspan().cmp(a);
  }
  template <typename L, typename R>
  friend inline ZuIfT<ZuIs_<L, ZtArray>{}, bool>
  operator ==(const L &l, const R &r) { return l.equals(r); }
  template <typename L, typename R>
  friend inline ZuIfT<ZuIs_<L, ZtArray>{}, int>
  operator <=>(const L &l, const R &r) { return l.cmp(r); }

// hash()
  uint32_t hash() const { return Ops::hash(m_data, length()); }

// +, += operators
  template <typename A>
  ZtArray operator +(const A &a) const { return add(a); }

private:
  template <typename A>
  MatchZtArray<A &&, ZtArray> add(A &&a) const {
    return Fwd_ZtArray<A &&>::add_(this, ZuFwd<A>(a));
  }
  template <typename A>
  MatchSpan<A &&, ZtArray> add(A &&a) const {
    return Fwd_Array<A>::add_(this, ZuFwd<A>(a));
  }

  template <typename S>
  MatchAnyString<S &&, ZtArray> add(S &&s_) const {
    ZuSpan<const typename ZuTraits<S>::Elem> s(s_);
    return add_(s.data(), s.length());
  }
  template <typename S>
  MatchAltString<S &&, ZtArray> add(const S &s_) const {
    ZuSpan<const AltChar> s(s_);
    return add__([s](Char *ptr, uint64_t length) -> uint64_t {
      if (!length) return 0;
      return ZuUTF<Char, AltChar>::cvt({ptr, length}, s);
    }, ZuUTF<Char, AltChar>::len(s));
  }
  template <typename C>
  MatchAltChar<C, ZtArray> add(C c_) const {
    AltChar c = c_;
    return add__([c](Char *ptr, uint64_t length) {
      return ZuUTF<Char, AltChar>::cvt({ptr, length}, {&c, 1});
    }, ZuUTF<Char, AltChar>::len({&c, 1}));
  }

  template <typename P>
  MatchPDelegate<P, ZtArray> add(P &&p) const {
    ZtArray a(*this);
    a.append_(ZuFwd<P>(p));
    return a;
  }
  template <typename P>
  MatchPBuffer<P, ZtArray> add(P &&p) const {
    uint64_t o = ZuPrint<P>::length(p);
    if (!o) return *this;
    if constexpr (ZuEquiv<Char, char>{}) {
      return add__([&p](T *ptr, uint64_t length) {
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

  template <typename R>
  MatchElem<R, ZtArray> add(R &&r) const {
    uint64_t n = length();
    uint64_t z = grow_(n, n + 1);
    T *newData = alloc__(z);
    if (!newData) throw std::bad_alloc{};
    if (n) copyElems(newData, m_data, n);
    initElem(newData + n, ZuFwd<R>(r));
    return ZtArray(newData, n + 1, z, true);
  }

  ZtArray add_(const T *data, uint64_t length) const {
    return add__([data](T *ptr, uint64_t length) {
      if (length) copyElems(ptr, data, length);
      return length;
    }, length);
  }
  ZtArray add_mv(T *data, uint64_t length) const {
    return add__([data](T *ptr, uint64_t length) {
      if (length) moveElems(ptr, data, length);
      return length;
    }, length);
  }

  template <typename Add>
  ZuInline ZtArray add__(Add add, uint64_t length) const {
    uint64_t n = this->length();
    uint64_t z = n + length;
    if (ZuUnlikely(!z)) return ZtArray{};
    T *newData = alloc__(z);
    if (!newData) throw std::bad_alloc{};
    if (n) copyElems(newData, m_data, n);
    length = add(newData + n, length);
    return ZtArray(newData, n + length, z, true);
  }

public:
  template <typename U>
  ZtArray &operator +=(U &&v) { return *this << ZuFwd<U>(v); }
  template <typename U>
  MatchStreamable<U, ZtArray &>
  operator <<(U &&v) {
    append_(ZuFwd<U>(v));
    return *this;
  }

private:
  template <typename A>
  MatchZtArray<A> append_(A &&a) {
    Fwd_ZtArray<A &&>::splice_(this, [](ZuSpan<T>) { }, length(), 0, ZuFwd<A>(a));
  }
  template <typename A>
  MatchSpan<A> append_(A &&a) {
    Fwd_Array<A>::splice_(this, [](ZuSpan<T>) { }, length(), 0, ZuFwd<A>(a));
  }
  template <typename A>
  MatchIterable<A> append_(const A &a) {
    auto i = a.begin();
    uint64_t n = a.end() - i;
    ensure(length() + n);
    for (uint64_t j = 0; j < n; j++)
      initElem(push(), *i++);
  }

  template <typename S>
  MatchAnyString<S> append_(S &&s_) {
    ZuSpan<const typename ZuTraits<S>::Elem> s(s_);
    if (ZuUnlikely(!s.length())) return;
    if constexpr (ZuIsSame<ZuDecay<S>, ZtArray>{})
      if (this == &s_) {
	auto rlength = s.length();
	auto buf = ZmAlloc(Char, rlength);
	copyElems(&buf[0], s.data(), rlength);
	append__([data = &buf[0]](Char *ptr, uint64_t rlength) {
	  moveElems(ptr, data, rlength);
	  return rlength;
	}, rlength);
	destroyElems(&buf[0], rlength); // will be optimized out
 	return;
      }
    append__([data = s.data()](Char *ptr, uint64_t rlength) {
      copyElems(ptr, data, rlength);
      return rlength;
    }, s.length());
  }

  template <typename S>
  MatchAltString<S> append_(S &&s_) {
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
  MatchPDelegate<P> append_(P &&p) { ZuPrint<P>::print(*this, ZuFwd<P>(p)); }
  template <typename P>
  MatchPBuffer<P> append_(const P &p) {
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
    length_(n + append(ensure(n + length) + n, length));
  }

  template <typename V>
  MatchReal<V> append_(V v) {
    append_(ZuBoxed(v));
  }
  template <typename V>
  MatchPtr<V> append_(V v) {
    append_(ZuBoxPtr(v).hex<false, ZuFmt::Alt<>>());
  }

  template <typename V>
  MatchElem<V> append_(V &&v) {
    initElem(push(), ZuFwd<V>(v));
  }

public:
  void append(const T *data, uint64_t length) {
    if (data) append__([data](T *ptr, uint64_t length) {
      if (length) copyElems(ptr, data, length);
      return length;
    }, length);
  }
  void append_mv(T *data, uint64_t length) {
    if (data) append__([data](T *ptr, uint64_t length) {
      if (length) moveElems(ptr, data, length);
      return length;
    }, length);
  }

// push/pop/shift/unshift

  // push() intentionally returns uninitialized storage
  // - recommended style:
  //   auto o = new (array.push()) T(...)
  T *push() {
    uint64_t n = length();
    uint64_t z = size();
    if (!mutable_() || n + 1 > z) {
      z = grow_(z, n + 1);
      T *newData = alloc__(z);
      if (!newData) throw std::bad_alloc{};
      this->template moveElems<false>(newData, m_data, n);
      free_();
      m_data = newData;
      size_mutable(z, 1);
      length_vallocd(n + 1, 1);
    } else
      length_(n + 1);
    return m_data + n;
  }
  template <typename V> T *push(V &&v) {
    auto ptr = push();
    if (ZuLikely(ptr)) initElem(ptr, ZuFwd<V>(v));
    return ptr;
  }
  T pop() {
    uint64_t n = length();
    if (!n) return ZuNullRef<T, Cmp>();
    T v;
    if (ZuUnlikely(!mutable_())) {
      v = m_data[--n];
    } else {
      v = ZuMv(m_data[--n]);
      destroyElem(m_data + n);
    }
    length_(n);
    return v;
  }
  T shift() {
    uint64_t n = length();
    if (!n) return ZuNullRef<T, Cmp>();
    T v;
    if (ZuUnlikely(!mutable_())) {
      v = m_data[0];
      ++m_data;
      --n;
    } else {
      v = ZuMv(m_data[0]);
      destroyElem(m_data);
      moveElems(m_data, m_data + 1, --n);
    }
    length_(n);
    return v;
  }

  template <typename A>
  MatchZtArray<A> unshift(A &&a) {
    Fwd_ZtArray<A &&>::splice_(this, [](ZuSpan<T>) { }, 0, 0, ZuFwd<A>(a));
  }
  template <typename A>
  MatchSpan<A> unshift(A &&a) {
    Fwd_Array<A>::splice_(this, [](ZuSpan<T>) { }, 0, 0, ZuFwd<A>(a));
  }

  T *unshift() {
    uint64_t n = length();
    uint64_t z = size();
    if (!mutable_() || n + 1 > z) {
      z = grow_(z, n + 1);
      T *newData = alloc__(z);
      if (!newData) throw std::bad_alloc{};
      this->template moveElems<false>(newData + 1, m_data, n);
      free_();
      m_data = newData;
      size_mutable(z, 1);
      length_vallocd(n + 1, 1);
    } else {
      moveElems(m_data + 1, m_data, n);
      length_(n + 1);
    }
    return m_data;
  }
  template <typename V> void unshift(V &&v) {
    initElem(unshift(), ZuFwd<V>(v));
  }

  // shift(N) - optimized version of splice(0, length)
  void shift(uint64_t length) {
    if (ZuUnlikely(!length)) return;
    uint64_t n = this->length();
    if (length > n) length = n;
    destroyElems(m_data, length);
    if (n -= length) moveElems(m_data, m_data + length, n);
    length_(n);
  }

// splice()

  template <typename L, typename = void>
  struct IsCallable : public ZuFalse { };
  template <typename L>
  struct IsCallable<L, decltype(ZuDeclVal<L &>()(ZuDeclVal<ZuSpan<T>>()))> :
    public ZuTrue { };

  template <typename Removed, typename Replace>
  void splice(
    Removed &&removed, int64_t offset, int64_t length,
    Replace &&replace, uint64_t rlength)
  {
    uint64_t n = this->length();
    uint64_t z = size();
    if (offset < 0) { if ((offset += n) < 0) offset = 0; }
    if (length < 0) { if ((length += (n - offset)) < 0) length = 0; }

    if (offset > int64_t(n)) {
      if constexpr (IsCallable<Removed>{})
	removed(ZuSpan<T>());
      else
	removed = {};
      if (!mutable_() || offset + int64_t(rlength) > int64_t(z)) {
	z = grow_(z, offset + rlength);
	size(z);
      }
      initElems(m_data + n, offset - n);
      if (rlength)
	rlength = replace(ZuSpan(m_data + offset, rlength));
      length_(offset + rlength); // rlength may have been reduced
      return;
    }

    if (length == LLONG_MAX || offset + length > int64_t(n))
      length = n - offset;

    int64_t l = n + rlength - length;

    if (!mutable_() || l > int64_t(z)) {
      z = l > 0 ? grow_(z, l) : 0;
      if constexpr (IsCallable<Removed>{})
	removed(ZuSpan(m_data + offset, length));
      else
	removed = ZuSpan(m_data + offset, length);
      if (!z) { null(); return; }
      T *newData = alloc__(z);
      if (!newData) throw std::bad_alloc{};
      this->template moveElems<false>(newData, m_data, offset);
      if (rlength)
	rlength = replace(ZuSpan(newData + offset, rlength));
      l = n + rlength - length; // rlength may have been reduced
      if (offset + length < int64_t(n))
	this->template moveElems<false>(
	    newData + offset + rlength,
	    m_data + offset + length,
	    n - (offset + length));
      free_();
      m_data = newData;
      size_mutable(z, 1);
      length_vallocd(l, 1);
      return;
    }

    if constexpr (IsCallable<Removed>{})
      removed(ZuSpan(m_data + offset, length));
    else
      removed = ZuSpan(m_data + offset, length);
    destroyElems(m_data + offset, length);
    if (l > 0) {
      int64_t tail = int64_t(n) - (offset + length);
      if (tail > 0 && int64_t(rlength) > length) {
	moveElems(
	  m_data + offset + rlength,
	  m_data + offset + length,
	  tail);
      }
      auto nrlength =
	rlength ? replace(ZuSpan(m_data + offset, rlength)) : 0;
      if (tail > 0) {
	if (int64_t(rlength) < length) {
	  moveElems(m_data + offset + nrlength, // NOT rlength
		    m_data + offset + length,
		    tail);
	} else if (nrlength < rlength) {
	  moveElems(m_data + offset + nrlength,
		    m_data + offset + rlength,
		    tail);
	}
      }
      l = n + nrlength - length;
    }
    length_(l);
  }
  void splice(int64_t offset) {
    splice([](ZuSpan<T>) { }, offset, LLONG_MAX, [](ZuSpan<T>) { return 0; }, 0);
  }
  void splice(int64_t offset, int64_t length) {
    splice([](ZuSpan<T>) { }, offset, length, [](ZuSpan<T>) { return 0; }, 0);
  }
  template <typename Removed>
  void splice(Removed &&removed, int64_t offset, int64_t length) {
    splice(ZuFwd<Removed>(removed), offset, length, [](ZuSpan<T>) { return 0; }, 0);
  }
  template <typename A>
  MatchZtArray<A>
  splice(int64_t offset, int64_t length, A &&a) {
    Fwd_ZtArray<A &&>::splice_(this,
      [](ZuSpan<T>) { }, offset, length, ZuFwd<A>(a));
  }
  template <typename Removed, typename A>
  MatchZtArray<A>
  splice(Removed &&removed, int64_t offset, int64_t length, A &&a) {
    Fwd_ZtArray<A &&>::splice_(this,
      ZuFwd<Removed>(removed), offset, length, ZuFwd<A>(a));
  }
  template <typename A>
  MatchSpan<A>
  splice(int64_t offset, int64_t length, A &&a) {
    Fwd_Array<A>::splice_(this,
      [](ZuSpan<T>) { }, offset, length, ZuFwd<A>(a));
  }
  template <typename Removed, typename A>
  MatchSpan<A>
  splice(Removed &&removed, int64_t offset, int64_t length, A &&a) {
    Fwd_Array<A>::splice_(this,
      ZuFwd<Removed>(removed), offset, length, ZuFwd<A>(a));
  }
  template <typename S>
  MatchAnyString<S>
  splice(int64_t offset, int64_t length, const S &s) {
    splice([](ZuSpan<T>) { }, offset, length, s);
  }
  template <typename Removed, typename S>
  MatchAnyString<S>
  splice(Removed &&removed, int64_t offset, int64_t length, const S &s_) {
    ZuSpan<const typename ZuTraits<S>::Elem> s(s_);
    splice(ZuFwd<Removed>(removed), offset, length,
      [data = s.data()](ZuSpan<T> span) -> uint64_t {
	auto n = span.length();
	if (n) copyElems(span.data(), data, n);
	return n;
      }, s.length());
  }
  template <typename S>
  MatchAltString<S>
  splice(int64_t offset, int64_t length, const S &s) {
    splice([](ZuSpan<T>) { }, offset, length, s);
  }
  template <typename Removed, typename S>
  MatchAltString<S>
  splice(Removed &&removed, int64_t offset, int64_t length, const S &s_) {
    ZuSpan<const AltChar> s(s_);
    splice(ZuFwd<Removed>(removed), offset, length,
      [s](ZuSpan<T> span) -> uint64_t {
	if (!span.length()) return 0;
	return ZuUTF<Char, AltChar>::cvt(span, s);
      }, ZuUTF<Char, AltChar>::len(s));
  }
  template <typename C>
  MatchAltChar<C>
  splice(int64_t offset, int64_t length, C c) {
    splice([](ZuSpan<T>) { }, offset, length, c);
  }
  template <typename Removed, typename C>
  MatchAltChar<C>
  splice(Removed &&removed, int64_t offset, int64_t length, C c_) {
    AltChar c = c_;
    splice(ZuFwd<Removed>(removed), offset, length,
      [c](ZuSpan<T> span) -> uint64_t {
	if (!span.length()) return 0;
	return ZuUTF<Char, AltChar>::cvt(span, {&c, 1});
      }, ZuUTF<Char, AltChar>::len({&c, 1}));
  }
  template <typename R>
  MatchElem<R>
  splice(int64_t offset, int64_t length, R &&r) {
    splice([](ZuSpan<T>) { }, offset, length, ZuFwd<R>(r));
  }
  template <typename Removed, typename R>
  MatchElem<R>
  splice(Removed &&removed, int64_t offset, int64_t length, R &&r_) {
    T r{ZuFwd<R>(r_)};
    splice(ZuFwd<Removed>(removed), offset, length,
      [&r](ZuSpan<T> span) -> uint64_t {
	if (!span.length()) return 0;
	moveElems(span.data(), &r, 1);
	return 1;
      }, 1);
  }

// iterate
  template <typename Fn> void iterate(Fn fn) {
    uint64_t n = length();
    for (uint64_t i = 0; i < n; i++) fn(m_data[i]);
  }

// grep
  // l(item) -> bool
  // - item is spliced out if true
  template <typename L> void grep(L &&l) {
    for (uint64_t i = 0, n = length(); i < n; i++)
      if (ZuFwd<L>(l)(m_data[i])) {
	splice(i, 1);
	--i, --n;
      }
  }

// growth algorithm
  void grow(uint64_t length) {
    uint64_t o = size();
    if (ZuUnlikely(length > o)) size(grow_(o, length));
    o = this->length();
    if (ZuUnlikely(length > o)) this->length(length);
  }
  void grow(uint64_t length, bool initElems_) {
    uint64_t o = size();
    if (ZuUnlikely(length > o)) size(grow_(o, length));
    o = this->length();
    if (ZuUnlikely(length > o)) this->length(length, initElems_);
  }
private:
  static uint64_t grow_(uint64_t o, uint64_t n) {
    return ZmGrow(o * sizeof(T), n * sizeof(T)) / sizeof(T);
  }

public:
// traits
  struct Traits : public ZuBaseTraits<ZtArray> {
    using Elem = T;
    enum {
      IsArray = 1, IsSpan = 1, IsPrimitive = 0,
      IsString =
	bool(ZuIsSame<ZuDecay<T>, char>{}) ||
	bool(ZuIsSame<ZuDecay<T>, wchar_t>{}),
      IsWString = bool(ZuIsSame<ZuDecay<T>, wchar_t>{})
    };
    static T *data(ZtArray &a) { return a.data(); }
    static const T *data(const ZtArray &a) { return a.data(); }
    static uint64_t length(const ZtArray &a) { return a.length(); }
  };
  friend Traits ZuTraitsType(ZtArray *);

// STL cruft
  using iterator = T *;
  using const_iterator = const T *;
  using iterator_category = std::contiguous_iterator_tag;
  ZuInline const T *begin() const { return m_data; }
  ZuInline const T *end() const { return &m_data[length()]; }
  ZuInline const T *cbegin() const { return m_data; } // sigh
  ZuInline const T *cend() const { return &m_data[length()]; }
  ZuInline T *begin() { return m_data; }
  ZuInline T *end() { return &m_data[length()]; }

private:
  uint64_t		m_size_mutable;	// allocated size and mutable flag
  uint64_t		m_length_vallocd;// initialized length and valloc'd flag
  T			*m_data;	// data buffer
};

template <typename T, typename NTP>
template <typename S>
inline void ZtArray<T, NTP>::convert_(const S &s, ZtIconv *iconv) {
  null_();
  iconv->convert(*this, s);
}

template <typename NTP = ZtArray_Defaults>
using ZtCArray = ZtArray<char, NTP>;
template <typename NTP = ZtArray_Defaults>
using ZtBArray = ZtArray<uint8_t, NTP>;
template <typename NTP = ZtArray_Defaults>
using ZtWArray = ZtArray<wchar_t, NTP>;

#endif /* ZtArray_HH */
