//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// fixed-size arrays for use in structs and passing by value
// - structural - can be used as a string template parameter, e.g.
//   - template <ZuArray S> struct Foo { }; Foo<"bar"> baz;
// - deduces from T (&)[N]
// - char and wchar_t arrays as strings
//   - can deduce from a compile-time string literal
//   - smoothly interoperates with other string types
// - can be used in consteval contexts
// - cached length (size is always constexpr)
// - explicitly contiguous
// - direct read/write access to the buffer
// - structured binding

#ifndef ZuArray_HH
#define ZuArray_HH

#ifndef ZuLib_HH
#include <zlib/ZuLib.hh>
#endif

#include <initializer_list>

#include <zlib/ZuAssert.hh>
#include <zlib/ZuTraits.hh>
#include <zlib/ZuSpan.hh>
#include <zlib/ZuArrayFn.hh>
#include <zlib/ZuCmp.hh>
#include <zlib/ZuPrint.hh>
#include <zlib/ZuUTF.hh>
#include <zlib/ZuBox.hh>
#include <zlib/ZuTL.hh>
#include <zlib/ZuAlt.hh>
#include <zlib/ZuElem.hh>

namespace Zu_ {

template <typename U, typename> struct Array_CanAppend_;
template <typename U> struct Array_CanAppend_<U, void> { using T = void; };
template <typename U> struct Array_CanAppend_<U, U &> { using T = void; };
template <typename U, typename T, typename = void>
struct Array_CanAppend : public ZuFalse { };
template <typename U, typename T>
struct Array_CanAppend<U, T,
  typename Array_CanAppend_<U,
    decltype(ZuDeclVal<ZuDeref<U> &>().append(ZuDeclVal<const T *>(), 1))>::T> :
  public ZuTrue { };

template <typename> struct Array_ { };
template <> struct Array_<char> {
  friend ZuPrintString ZuPrintType(Array_ *);
};

// implementation notes
// - structural implies POD (not in terms of C++ standards conformance,
//   but in the sense of "plain ole' data" - i.e. "can I memcpy it?")
//   (this is also the meaning of ZuTraits<T>::IsPOD)
// - structural requires full initialization of all array elements, meanwhile...
// - good run-time performance requires elision of unnecessary initialization
// - ... resulting in the following repeating pattern in the implementation:
//   if (ZuConstEval()) { {A} if constexpr (ZuTraits<T>::IsPOD) {B} } else {C}
//   - A is used at compile-time
//   - B is used at compile-time in a structural context
//   - C is used at run-time

template <typename T_, unsigned N_>
struct Array : public Array_<ZuStrip<T_>>, public ZuArrayFn<T_> {
  ZuAssert(N_ > 0);
  ZuAssert(N_ < (1U<<16) - 1U);	// keep it sane

  using T = T_;
  static constexpr unsigned N = N_;
  using AltChar = ZuAlt<T>;
  using Cmp = ZuCmp<T>;
  using Fn = ZuArrayFn<T>;
  using Elem_ = ZuElem<T>;

  // to be structural, all data members must be public

  uint32_t	length_;
  Elem_		data_[N];

  using Fn::initElem;
  using Fn::initElems;
  using Fn::moveElems;
  using Fn::copyElems;
  using Fn::destroyElems;

  struct Move { };

  // from some string with same char (including string literals)
  template <typename U, typename V = T>
  struct IsString : public ZuBool<
    (ZuTraits<U>::IsSpan || ZuTraits<U>::IsString) &&
    bool(ZuEquiv<typename ZuTraits<U>::Elem, V>{})> { };
  template <typename U, typename R = void>
  using MatchString = ZuIfT<IsString<U>{}, R>;

  // from char2 string (requires conversion)
  template <typename U, typename V = AltChar>
  struct IsAltString : public ZuBool<
    !ZuIsSame<V, void>{} && bool(IsString<U, V>{})> { };
  template <typename U, typename R = void>
  using MatchAltString = ZuIfT<IsAltString<U>{}, R>;

  // from any array type with convertible element type (not a string)
  template <typename U, typename V = T>
  struct IsSpan : public ZuBool<
    !IsString<U>{} &&
    !IsAltString<U>{} &&
    !ZuIsSame<U, V>{} &&
    ZuTraits<U>::IsSpan &&
    ZuIsConvertible<typename ZuTraits<U>::Elem, V>{}> { };
  template <typename U, typename R = void>
  using MatchSpan = ZuIfT<IsSpan<U>{}, R>;

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

  // from printable type (if this is a char array)
  template <typename U, typename V = T>
  struct IsPrint : public ZuBool<
    bool(ThisIsString<V>{}) &&
    ZuPrint<U>::OK && !ZuPrint<U>::String> { };
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
    !IsString<U>{} &&
    !IsAltString<U>{} &&
    !IsPrint<U>{} &&
    !ZuIsSame<U, V>{} &&
    !ZuTraits<U>::IsSpan &&
    bool(IsIterable_<ZuDecay<U>>{}) &&
    ZuIsConstructible<typename ZuTraits<U>::Elem, V>{}> { };
  template <typename U, typename R = void>
  using MatchIterable = ZuIfT<IsIterable<U>{}, R>;

  // from real primitive types other than chars (if this is a char string)
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
  template <typename U, typename V = T, unsigned M = N>
  struct IsElem : public ZuBool<
    (M > 0) &&
      (bool(ZuIsSame<U, V>{}) ||
      (!IsString<U>{} &&
       !ZuTraits<U>::IsArray &&
       !IsAltString<U>{} &&
       !IsAltChar<U>{} &&
       !IsPDelegate<U>{} &&
       !IsPBuffer<U>{} &&
       !IsReal<U>{} &&
       ZuIsConvertible<U, V>{}))> { };
  template <typename U, typename R = void>
  using MatchElem = ZuIfT<IsElem<U>{}, R>;

  // an integer parameter to the constructor is a buffer size
  // - except for character element types
  template <typename U, typename V = T>
  struct IsCtorLength : public ZuBool<
    ZuTraits<U>::IsIntegral && (sizeof(U) > 2 || !ZuEquiv<V, U>{})> { };
  template <typename U, typename R = void>
  using MatchCtorLength = ZuIfT<IsCtorLength<U>{}, R>;

  // from real, excluding what should be interpreted as a length
  template <typename U>
  struct IsCtorReal : public ZuBool<
      bool(IsReal<U>{}) && !IsCtorLength<U>{}> { };
  template <typename U, typename R = void>
  using MatchCtorReal = ZuIfT<IsCtorReal<U>{}, R>;

  // from an element, excluding what should be interpreted as a length
  template <typename U>
  struct IsCtorElem : public ZuBool<
      bool(IsElem<U>{}) && !IsCtorLength<U>{}> { };
  template <typename U, typename R = void>
  using MatchCtorElem = ZuIfT<IsCtorElem<U>{}, R>;

  // limit member operator <<() overload resolution to supported types
  template <typename U>
  struct IsStreamable : public ZuBool<
    bool(IsString<U>{}) ||
    bool(IsSpan<U>{}) ||
    bool(IsAltString<U>{}) ||
    bool(IsAltChar<U>{}) ||
    bool(IsPDelegate<U>{}) ||
    bool(IsPBuffer<U>{}) ||
    bool(IsIterable<U>{}) ||
    bool(IsReal<U>{}) ||
    bool(IsElem<U>{})> { };
  template <typename U, typename R = void>
  using MatchStreamable = ZuIfT<IsStreamable<U>{}, R>;

// constructors, assignments and destructor

  constexpr Array() noexcept : length_{0} {
    if (ZuConstEval())
      if constexpr (ZuTraits<T>::IsPOD) // structurals need full init
	for (unsigned i = 0; i < N; i++)
	  ZuNew<T>(ZuAddr((*this)[i]));
  }

  constexpr Array(const Array &a)
    noexcept(ZuNXCopy<T>{}) : length_{a.length()}
  {
    if (ZuConstEval()) {
      unsigned i = 0;
      for (i = 0; i < length_; i++)
	ZuNew<T>(ZuAddr((*this)[i]), a[i]);
      if constexpr (ZuTraits<T>::IsPOD)
	for (; i < N; i++)
	  ZuNew<T>(ZuAddr((*this)[i]));
    } else
      copyElems(data(), a.data(), length_);
  }
  constexpr Array &operator =(const Array &a) noexcept(ZuNXCopy<T>{}) {
    if (this != &a) {
      this->~Array();
      if (ZuConstEval())
	ZuNew<Array>(this, a);
      else
	new (this) Array(a);
    }
    return *this;
  }

  // no attempt is made here to reproduce std::vector's troublesome behavior
  // that depends on the noexcept qualification of the underlying type's
  // move constructor
  constexpr Array(Array &&a)
    noexcept(ZuNXMove<T>{}) : length_{a.length()}
  {
    if (ZuConstEval()) {
      unsigned i = 0;
      for (i = 0; i < length_; i++)
	ZuNew<T>(ZuAddr((*this)[i]), ZuMv(a[i]));
      if constexpr (ZuTraits<T>::IsPOD)
	for (; i < N; i++)
	  ZuNew<T>(ZuAddr((*this)[i]));
    } else
      this->template moveElems<false>(data(), a.data(), length_);
  }
  constexpr Array &operator =(Array &&a) noexcept(ZuNXMove<T>{}) {
    this->~Array();
    if (ZuConstEval())
      ZuNew<Array>(this, ZuMv(a));
    else
      new (this) Array(ZuMv(a));
    return *this;
  }

  constexpr Array(std::initializer_list<T> a_)
    noexcept(ZuNXCopy<T>{}) : length_(a_.size())
  {
    if (length_ > N) length_ = N;
    auto a = a_.begin();
    if (ZuConstEval()) {
      unsigned i;
      for (i = 0; i < length_; i++)
	ZuNew<T>(ZuAddr((*this)[i]), *a++);
      if constexpr (ZuTraits<T>::IsPOD)
	for (; i < N; i++)
	  ZuNew<T>(ZuAddr((*this)[i]));
    } else
      copyElems(data(), a, length_);
  }

  template <typename V, decltype(MatchCtorLength<V>(), int()) = 0>
  constexpr Array(V n, bool init = !ZuTraits<T>::IsPrimitive)
    noexcept(ZuNXConstruct<T>{}) : length_(n)
  {
    if (length_ > N) length_ = N;
    if (init || ZuConstEval()) {
      if (ZuConstEval()) {
	unsigned i;
	for (i = 0; i < length_; i++)
	  ZuNew<T>(ZuAddr((*this)[i]));
	if constexpr (ZuTraits<T>::IsPOD)
	  for (; i < N; i++)
	    ZuNew<T>(ZuAddr((*this)[i]));
      } else
	initElems(data(), length_);
    }
  }

  template <typename A, decltype(
    MatchSpan<A>(), T(ZuFwdLike<A>(ZuDeclVal<A &&>()[0])), int()) = 0,
    bool NoExcept = noexcept(T(ZuFwdLike<A>(ZuDeclVal<A &&>()[0])))>
  constexpr Array(A &&a) noexcept(NoExcept) : length_{ZuTraits<A>::length(a)} {
    if (length_ > N) length_ = N;
    if (ZuConstEval()) {
      unsigned i;
      for (i = 0; i < length_; i++)
	ZuNew<T>(ZuAddr((*this)[i]), ZuFwdLike<A>(a[i]));
      if constexpr (ZuTraits<T>::IsPOD)
	for (; i < N; i++)
	  ZuNew<T>(ZuAddr((*this)[i]));
    } else if constexpr (ZuIsLRef<A>{})
      copyElems(data(), &a[0], length_);
    else
      this->template moveElems<false>(data(), &a[0], length_);
  }

  template <typename A, decltype(
    MatchIterable<A>(), T(*(ZuDeclVal<A &&>().begin())), int()) = 0,
    bool NoExcept = noexcept(T(*(ZuDeclVal<A &&>().begin())))>
  constexpr Array(A &&a_) noexcept(NoExcept) : length_(a_.end() - a_.begin()) {
    if (length_ > N) length_ = N;
    auto a = a_.begin();
    if (ZuConstEval()) {
      unsigned i;
      for (i = 0; i < length_; i++)
	ZuNew<T>(ZuAddr((*this)[i]), ZuFwdLike<A>(*a++));
      if constexpr (ZuTraits<T>::IsPOD)
	for (; i < N; i++)
	  ZuNew<T>(ZuAddr((*this)[i]));
    } else
      for (unsigned i = 0; i < length_; i++)
	new (&(*this)[i]) T(ZuFwdLike<A>(*a++));
  }

  template <typename S, decltype(MatchString<S>(), int()) = 0>
  constexpr Array(const S &s) noexcept : length_(ZuTraits<S>::length(s)) {
    if (length_ > N) length_ = N;
    if (ZuConstEval()) {
      unsigned i;
      for (i = 0; i < length_; i++)
	ZuNew<T>(ZuAddr((*this)[i]), s[i]);
      if constexpr (ZuTraits<T>::IsPOD)
	for (; i < N; i++)
	  ZuNew<T>(ZuAddr((*this)[i]));
    } else
      copyElems(data(), &s[0], length_);
  }

  template <
    typename E, decltype(MatchCtorElem<E>(), int()) = 0,
    bool NoExcept = noexcept(T(ZuDeclVal<E &&>()))>
  constexpr Array(E &&e) noexcept(NoExcept) : length_{1} {
    if (ZuConstEval()) {
      ZuNew<T>(ZuAddr((*this)[0]), ZuFwd<E>(e));
      if constexpr (ZuTraits<T>::IsPOD)
	for (unsigned i = 1; i < N; i++)
	  ZuNew<T>(ZuAddr((*this)[i]));
    } else
      initElem(data(), ZuFwd<E>(e));
  }

  template <typename S, decltype(MatchAltString<S>(), int()) = 0>
  Array(S &&s) noexcept {
    data()[length_ = ZuUTF<T, AltChar>::cvt({data(), N}, s)] = 0;
  }
  template <typename C, decltype(MatchAltChar<C>(), int()) = 0>
  Array(C c) noexcept {
    data()[length_ = ZuUTF<T, AltChar>::cvt({data(), N}, {&c, 1})] = 0;
  }

  template <typename P, decltype(MatchPDelegate<P>(), int()) = 0>
  Array(P &&p) noexcept {
    ZuPrint<P>::print(*this, ZuFwd<P>(p));
  }
  template <typename P, decltype(MatchPBuffer<P>(), int()) = 0>
  Array(const P &p) noexcept {
    unsigned length = ZuPrint<P>::length(p);
    if (length > N)
      length_ = 0;
    else
      length_ = ZuPrint<P>::print(reinterpret_cast<char *>(data()), length, p);
  }

  template <typename V, decltype(MatchCtorReal<V>(), int()) = 0>
  Array(V v) noexcept {
    new (this) Array{ZuBoxed(v)};
  }

  // arrays as ptr, length
  template <typename A, decltype(ZuConvertible<A, T>(), int()) = 0>
  constexpr Array(const A *a, unsigned length)
    noexcept(ZuNXCopy<T, ZuDeref<decltype(a[0])>>{}) : length_{length}
  {
    if (length_ > N) length_ = N;
    if (ZuConstEval()) {
      unsigned i;
      for (i = 0; i < length_; i++)
	ZuNew<T>(ZuAddr((*this)[i]), a[i]);
      if constexpr (ZuTraits<T>::IsPOD)
	for (; i < N; i++)
	  ZuNew<T>(ZuAddr((*this)[i]));
    } else
      copyElems(data(), a, length_);
  }
  constexpr Array(Move, T *a, unsigned length)
    noexcept(ZuNXMove<T, ZuDeref<decltype(a[0])>>{}) : length_{length}
  {
    if (length_ > N) length_ = N;
    if (ZuConstEval()) {
      unsigned i;
      for (unsigned i = 0; i < length_; i++)
	ZuNew<T>(ZuAddr((*this)[i]), ZuMv(a[i]));
      if constexpr (ZuTraits<T>::IsPOD)
	for (; i < N; i++)
	  ZuNew<T>(ZuAddr((*this)[i]));
    } else
      this->template moveElems<false>(data(), a, length_);
  }

  constexpr ~Array() noexcept(ZuNXDestroy<T>{}) {
    if (ZuConstEval()) {
      if constexpr (ZuTraits<T>::IsComposite)
	for (unsigned i = 0; i < length_; i++) (*this)[i].~T();
    } else
      destroyElems(data(), length_);
  }

// accessors

  static constexpr unsigned size() { return N; }
  constexpr unsigned length() const { return length_; }

// array/ptr operators

  ZuInline constexpr auto &&operator [](this auto &&self, unsigned i) {
    return ZuFwdLike<decltype(self)>(self.data_[i].v);
  }
  ZuInline constexpr auto &&operator *(this auto &&self) {
    return ZuFwdLike<decltype(self)>(self.data_[0].v);
  }

// raw data access (not constexpr)

  ZuInline T *data() { return &data_[0].v; }
  ZuInline const T *data() const { return &data_[0].v; }

  ZuInline auto span() { return ZuSpan(data(), N); }
  ZuInline auto cspan() const { return ZuSpan(data(), length_); }

  const T *terminate() {
    if (ZuUnlikely(length_ >= N)) length_ = N - 1;
    data()[length_] = 0;
    return data();
  }

// comparisons

  ZuInline constexpr bool operator !() const { return !length_; }
  ZuOpBool

protected:
  ZuInline constexpr bool same(const Array &a) const { return this == &a; }
  template <typename A>
  ZuInline constexpr bool same(const A &a) const { return false; }

public:
  template <typename A>
  ZuInline constexpr bool equals(const A &a) const {
    if (ZuConstEval()) {
      if (same(a)) return true;
      unsigned l = length();
      unsigned n = ZuTraits<A>::length(a);
      if (l != n) return false;
      for (unsigned i = 0; i < l; i++)
	if (!Cmp::equals((*this)[i], a[i])) return false;
      return true;
    } else {
      return same(a) || cspan().equals(a);
    }
  }
  template <typename A>
  ZuInline constexpr int cmp(const A &a) const {
    if (ZuConstEval()) {
      if (same(a)) return 0;
      unsigned l = length();
      unsigned n = ZuTraits<A>::length(a);
      for (unsigned i = 0, m = l < n ? l : n; i < m; i++)
	if (int i = Cmp::cmp((*this)[i], a[i])) return i;
      return ZuCmp<int>::cmp(l, n);
    } else {
      if (same(a)) return 0;
      return cspan().cmp(a);
    }
  }
  template <typename L, typename R>
  friend ZuInline constexpr ZuIfT<ZuIs_<L, Array>{}, bool>
  operator ==(const L &l, const R &r) { return l.equals(r); }
  template <typename L, typename R>
  friend ZuInline constexpr ZuIfT<ZuIs_<L, Array>{}, int>
  operator <=>(const L &l, const R &r) { return l.cmp(r); }

// hash

  uint32_t hash() const { return Fn::hash(data(), length_); }

// iteration

  template <bool Mutable = false, typename L>
  constexpr ZuIfT<!Mutable> all(L &&l) const {
    auto data_ = data();
    for (unsigned i = 0, n = length_; i < n; i++) ZuFwd<L>(l)(data_[i]);
  }
  template <bool Mutable, typename L>
  constexpr ZuIfT<Mutable> all(L &&l) {
    auto data_ = data();
    for (unsigned i = 0, n = length_; i < n; i++) ZuFwd<L>(l)(data_[i]);
  }

// reset to null

  constexpr void clear() { null(); }
  constexpr void null() {
    destroyElems(data(), length_);
    length_ = 0;
  }

// set length

  template <bool InitElems = !ZuTraits<T>::IsPrimitive>
  constexpr void length(unsigned length) {
    if (length > N) length = N;
    if (InitElems || ZuConstEval()) { // not if constexpr
      if (ZuConstEval()) {
	if (length > length_) {
	  for (unsigned i = length_; i < length; i++)
	    ZuNew<T>(ZuAddr((*this)[i]));
	} else if (length < length_) {
	  for (unsigned i = length; i < length_; i++)
	    (*this)[i].~T();
	}
      } else {
	if (length > length_) {
	  initElems(data() + length_, length - length_);
	} else if (length < length_) {
	  destroyElems(data() + length, length_ - length);
	}
      }
    }
    length_ = length;
  }

// push/pop/shift/unshift

  constexpr T *push() {
    if (length_ >= N) return nullptr;
    if (ZuConstEval())
      return ZuAddr((*this)[length_++]);
    else
      return &data()[length_++];
  }
  template <typename I>
  constexpr T *push(I &&i) {
    auto ptr = push();
    if (ZuConstEval()) {
      if (ptr) initElem(ptr, ZuFwd<I>(i));
    } else {
      if (ZuLikely(ptr)) initElem(ptr, ZuFwd<I>(i));
    }
    return ptr;
  }
  constexpr T pop() {
    if (!length_) return Cmp::null();
    T v = ZuMv((*this)[--length_]);
    if (ZuConstEval())
      (*this)[length_].~T();
    else
      destroyElem(data() + length_);
    return v;
  }
  constexpr T shift() {
    if (!length_) return Cmp::null();
    T v = ZuMv((*this)[0]);
    --length_;
    if (ZuConstEval()) {
      (*this)[0].~T();
      for (unsigned i = 0; i < length_; i++) {
	ZuNew<T>(ZuAddr((*this)[i]), ZuMv((*this)[i + 1]));
	(*this)[i + 1].~T();
      }
    } else {
      destroyElem(data());
      moveElems(data(), data() + 1, length_);
    }
    return v;
  }
  template <typename I>
  constexpr T *unshift(I &&i) {
    if (length_ >= N) return 0;
    if (ZuConstEval()) {
      for (unsigned i = length_; --i > 0; ) {
	ZuNew<T>(ZuAddr((*this)[i]), ZuMv((*this)[i - 1]));
	(*this)[i - 1].~T();
      }
      ++length_;
    } else {
      moveElems(data() + 1, data(), length_++);
    }
    T *ptr = data();
    initElem(ptr, ZuFwd<I>(i));
    return ptr;
  }

  constexpr void shift(unsigned n) {
    if (ZuUnlikely(!n)) return;
    if (n > length_) n = length_;
    if (ZuConstEval()) {
      for (unsigned i = 0; i < n; i++) (*this)[i].~T();
      if (length_ -= n)
	for (unsigned i = 0; i < length_; i++) {
	  ZuNew<T>(ZuAddr((*this)[i]), ZuMv((*this)[i + n]));
	  (*this)[i + n].~T();
	}
    } else {
      destroyElems(data(), n);
      if (length_ -= n) moveElems(data(), data() + n, length_);
    }
  }

// append operations

  template <typename U>
  constexpr MatchStreamable<U &&, Array &> operator <<(U &&v) {
    this->append(ZuFwd<U>(v));
    return *this;
  }

  template <typename U>
  constexpr Array &operator +=(U &&v) {
    return *this << ZuFwd<U>(v);
  }

  template <typename A>
  constexpr MatchSpan<A> append(A &&a) {
    auto length = ZuTraits<A>::length(a);
    if (length_ + length > N) length = N - length_;
    if (ZuConstEval())
      for (unsigned i = 0; i < length; i++)
	ZuNew<T>(ZuAddr((*this)[i + length_]), ZuFwdLike<A>(a[i]));
    else if constexpr (ZuIsLRef<A>{})
      copyElems(data() + length_, &a[0], length);
    else
      this->template moveElems<false>(data() + length_, &a[0], length);
    length_ += length;
  }

  template <typename A>
  constexpr MatchIterable<A> append(A &&a_) {
    auto length = a_.end() - a_.begin();
    if (length_ + length > N) length = N - length_;
    auto a = a_.begin();
    if (ZuConstEval())
      for (unsigned i = 0; i < length; i++)
	ZuNew<T>(ZuAddr((*this)[i + length_]), ZuFwdLike<A>(*a++));
    else
      for (unsigned i = 0; i < length; i++)
	new (&(*this)[i + length_]) T(ZuFwdLike<A>(*a++));
    length_ += length;
  }

  template <typename S>
  constexpr MatchString<S> append(const S &s) {
    auto length = ZuTraits<S>::length(s);
    if (length_ + length > N) length = N - length_;
    if (ZuConstEval())
      for (unsigned i = 0; i < length; i++)
	ZuNew<T>(ZuAddr((*this)[i + length_]), s[i]);
    else
      copyElems(data() + length_, &s[0], length);
    length_ += length;
  }

  template <typename E>
  constexpr MatchElem<E> append(E &&e) {
    if (length_ >= N) return;
    if (ZuConstEval())
      ZuNew<T>(ZuAddr((*this)[length_]), ZuFwd<E>(e));
    else
      initElem(data() + length_, ZuFwd<E>(e));
    ++length_;
  }

  template <typename S>
  MatchAltString<S>
  append(const S &s) {
    if (length_ >= N) return;
    length_ += ZuUTF<T, AltChar>::cvt(
      {data() + length_, N - length_}, s);
  }
  template <typename C>
  MatchAltChar<C> append(C c) {
    if (length_ >= N) return;
    length_ += ZuUTF<T, AltChar>::cvt(
      {data() + length_, N - length_}, {&c, 1});
  }

  template <typename P>
  MatchPDelegate<P> append(P &&p) {
    ZuPrint<P>::print(*this, ZuFwd<P>(p));
  }
  template <typename P>
  MatchPBuffer<P> append(const P &p) {
    unsigned length = ZuPrint<P>::length(p);
    if (!length || length_ + length >= N) return;
    if constexpr (ZuEquiv<T, char>{}) {
      length_ += ZuPrint<P>::print(
	reinterpret_cast<char *>(data()) + length_, length, p);
    } else {
      auto buf = static_cast<char *>(ZuAlloca(length, 1));
      if (!buf) return;
      ZuCSpan s(buf, ZuPrint<P>::print(buf, length, p));
      length_ += ZuUTF<T, AltChar>::cvt(
	{data() + length_, N - length_}, s);
    }
  }

  template <typename V>
  MatchReal<V> append(V v) {
    append(ZuBoxed(v));
  }
  template <typename V>
  MatchPtr<V> append(V v) {
    append(ZuBoxPtr(v).hex<false, ZuFmt::Alt<>>());
  }

// splice operations

  template <typename U> struct IsVoid : public ZuIsSame<void, U> { };
  template <typename U, typename R = void>
  using MatchVoid = ZuIfT<IsVoid<U>{}, R>;

  template <typename U, typename V = T> struct IsAppend :
    public ZuBool<Array_CanAppend<U, V>{}> { };
  template <typename U, typename R = void>
  using MatchAppend = ZuIfT<IsAppend<U>{}, R>;

  template <typename U, typename V = T> struct IsSplice :
    public ZuBool<!IsVoid<U>{} && !IsAppend<U>{}> { };
  template <typename U, typename R = void>
  using MatchSplice = ZuIfT<IsSplice<U>{}, R>;

  constexpr void splice(int offset, int length) {
    splice_(offset, length, (void *)0);
  }
  template <typename U>
  constexpr void splice(int offset, int length, U &removed) {
    splice_(offset, length, &removed);
  }

  // differs from ZuSpan::splice
  // - ZuArray is uninitialized in [length_, N)
  template <typename U>
  constexpr void splice_(int offset, int length, U *removed) {
    if (ZuUnlikely(!length)) return;
    if (offset < 0) { if ((offset += length_) < 0) offset = 0; }
    if (offset >= int(N)) return;
    if (length < 0) { if ((length += (length_ - offset)) <= 0) return; }
    if (offset + length > int(N))
      if (!(length = N - offset)) return;

    if (offset > int(length_)) {
      if (ZuConstEval()) {
	for (unsigned i = length_; i < unsigned(offset); i++)
	  ZuNew<T>(ZuAddr((*this)[i]));
      } else {
	initElems(data() + length_, offset - length_);
      }
      length_ = offset;
      return;
    }
    if (offset + length > int(length_))
      if (!(length = length_ - offset)) return;

    if (!ZuConstEval() || bool(IsAppend<U>{})) {
      auto ptr = data() + offset;
      if (removed) splice__(ptr, length, removed);
      destroyElems(ptr, length);
      moveElems(ptr, ptr + length, length_ - (offset + length));
      length_ -= length;
      return;
    }

    // redundant at run-time but required at compile-time
    if constexpr (!IsAppend<U>{}) {
      // constant-evaluated from here
      if constexpr (!IsVoid<U>{}) {
	if (removed) {
	  unsigned end = unsigned(offset) + unsigned(length);
	  for (unsigned i = unsigned(offset); i < end; i++)
	    *removed << (*this)[i];
	}
      }
      {
	unsigned end = unsigned(offset) + unsigned(length);
	for (unsigned i = unsigned(offset); i < end; i++) (*this)[i].~T();
      }
      {
	unsigned end = length_ - unsigned(length);
	for (unsigned dst = unsigned(offset); dst < end; dst++) {
	  unsigned src = dst + unsigned(length);
	  ZuNew<T>(ZuAddr((*this)[dst]), ZuMv((*this)[src]));
	  (*this)[src].~T();
	}
      }
      length_ -= length;
    }
  }
  template <typename U>
  constexpr MatchVoid<U> splice__(const T *, unsigned, U *) { }
  template <typename U>
  constexpr MatchAppend<U> splice__(const T *data, unsigned length, U *removed) {
    removed->append(data, length);
  }
  template <typename U>
  constexpr MatchSplice<U> splice__(const T *data, unsigned length, U *removed) {
    for (unsigned i = 0; i < length; i++) *removed << data[i];
  }

  // traits
  struct Traits : public ZuBaseTraits<Array> {
    using Elem = T;
    enum {
      IsArray = 1, IsSpan = 1, IsPrimitive = 0,
      IsPOD = ZuTraits<T>::IsPOD,
      IsString =
	bool(ZuIsSame<ZuDecay<T>, char>{}) ||
	bool(ZuIsSame<ZuDecay<T>, wchar_t>{}),
      IsWString = bool(ZuIsSame<ZuDecay<T>, wchar_t>{})
    };
    ZuInline static constexpr Elem *data(Array &a) { return a.data(); }
    ZuInline static constexpr const Elem *data(const Array &a) {
      return a.data();
    }
    ZuInline static constexpr unsigned length(const Array &a) {
      return a.length();
    }
  };
  friend Traits ZuTraitsType(Array *);

// STL cruft

  using iterator = T *;
  using const_iterator = const T *;
  using iterator_category = std::contiguous_iterator_tag;
  ZuInline constexpr const T *begin() const { return data(); }
  ZuInline constexpr const T *end() const { return data() + length_; }
  ZuInline constexpr const T *cbegin() const { return data(); } // sigh
  ZuInline constexpr const T *cend() const { return data() + length_; }
  ZuInline constexpr T *begin() { return data(); }
  ZuInline constexpr T *end() { return data() + length_; }
};

template <typename T, unsigned N>
Array(const T(&)[N]) -> Array<T, N>;
template <typename T, unsigned N>
Array(T(&)[N]) -> Array<T, N>;

} // namespace Zu_

// clang up to v19 is still missing P1814R0, CTAD for alias templates
#if defined(__clang__) && (__clang_major__ <= 19)
#define ZuArray Zu_::Array
#else
template <typename T, unsigned N>
using ZuArray = Zu_::Array<T, N>;
#endif

// constant-evaluated arrays
template <ZuArray A> using ZuArrayT = ZuConstant<decltype(A), A>;
template <ZuArray ...A> using ZuArrayTL = ZuTypeList<ZuArrayT<A>...>;

// STL structured binding cruft
#include <type_traits>

namespace std {
  template <class> struct tuple_size;
  template <typename T, unsigned N>
  struct tuple_size<ZuArray<T, N>> :
    public integral_constant<size_t, N> { };

  template <size_t, typename> struct tuple_element;
  template <size_t I, typename T, unsigned N>
  struct tuple_element<I, ZuArray<T, N>> { using type = T; };
}

namespace Zu_ {
  using size_t = std::size_t;

  namespace {
    template <size_t I, typename T>
    using tuple_element_t = typename std::tuple_element<I, T>::type;
  }

  template <size_t I, typename T, unsigned N>
  constexpr tuple_element_t<I, Array<T, N>> &
  get(Array<T, N> &a) noexcept { return a[I]; }

  template <size_t I, typename T, unsigned N>
  constexpr const tuple_element_t<I, Array<T, N>> &
  get(const Array<T, N> &a) noexcept { return a[I]; }

  template <size_t I, typename T, unsigned N>
  constexpr tuple_element_t<I, Array<T, N>> &&
  get(Array<T, N> &&a) noexcept {
    return static_cast<tuple_element_t<I, Array<T, N>> &&>(a[I]);
  }

  template <size_t I, typename T, unsigned N>
  constexpr const tuple_element_t<I, Array<T, N>> &&
  get(const Array<T, N> &&a) noexcept {
    return static_cast<const tuple_element_t<I, Array<T, N>> &&>(a[I]);
  }
}

// shorthand for various standard arrays (byte, character, wide character)

template <unsigned N> using ZuBArray = ZuArray<uint8_t, N>;
template <unsigned N> using ZuCArray = ZuArray<char, N>;
template <unsigned N> using ZuWArray = ZuArray<wchar_t, N>;

#endif /* ZuArray_HH */
