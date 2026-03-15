//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// ZuSpan<T> is a constexpr wrapper around a pointer+length pair
// unlike std::array, prioritizes run-time optimization over compile-time
// unlike std::span, prioritizes:
// - expressiveness over readability
// - intrusive integration with ZuHash/ZuCmp
// - mutability (if T is mutable)

#ifndef ZuSpan_HH
#define ZuSpan_HH

#ifndef ZuLib_HH
#include <zlib/ZuLib.hh>
#endif

#include <initializer_list>

#include <stdlib.h>

#include <zlib/ZuTraits.hh>
#include <zlib/ZuCmp.hh>
#include <zlib/ZuHash.hh>
#include <zlib/ZuPrint.hh>
#include <zlib/ZuArrayFn.hh>
#include <zlib/ZuEquiv.hh>
#include <zlib/ZuElem.hh>

// ZuSpan_IsNestedIL is used to ensure that:
// - ZuSpan s{"x", "y", "z"}
// - ZuSpan s{{1}, {1, 2}, {1, 2, 3}}
// deduce correctly as a "span of spans" - respectively matching:
// - ZuSpan(T (&..._)[N])
// - ZuSpan(std::initializer_list<std::initializer_list<T>>)
// via deduction guides instead of deducing via the
// ZuSpan(std::initializer_list<T>) constructor
template <typename>
struct ZuSpan_IsNestedIL : public ZuFalse { };
// match std::initializer_list<const T *>
template <typename T>
struct ZuSpan_IsNestedIL<const T *> : public ZuTrue { };
// match std::initializer_list<std::initializer_list<T>>
template <typename T>
struct ZuSpan_IsNestedIL<std::initializer_list<T>> : public ZuTrue { };

template <typename T> struct ZuSpan_ { };
template <> struct ZuSpan_<char> {
  friend ZuPrintString ZuPrintType(ZuSpan_ *);
};

template <typename T_>
class ZuSpan : public ZuSpan_<ZuStrip<T_>> {
template <typename> friend class ZuSpan;

public:
  using T = T_;
  using Cmp = ZuCmp<T>;
  using Ops = ZuArrayFn<T, Cmp>;

  constexpr ZuSpan() noexcept : m_data{nullptr}, m_length{0} { }
  constexpr ZuSpan(const ZuSpan &a) noexcept :
    m_data{a.m_data}, m_length{a.m_length} { }
  constexpr ZuSpan &operator =(const ZuSpan &a) noexcept {
    if (ZuLikely(this != &a)) {
      m_data = a.m_data;
      m_length = a.m_length;
    }
    return *this;
  }
  constexpr ZuSpan(ZuSpan &&a) noexcept :
    m_data{a.m_data}, m_length{a.m_length} { }
  constexpr ZuSpan &operator =(ZuSpan &&a) noexcept {
    m_data = a.m_data;
    m_length = a.m_length;
    return *this;
  }

#if defined(__GNUC__) && !defined(__llvm__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winit-list-lifetime"
#endif
  constexpr ZuSpan(std::initializer_list<T> a,
    decltype(ZuIfT<!ZuSpan_IsNestedIL<T>{}>(), int()) = 0)
  :
    m_data(const_cast<T *>(a.begin())), m_length(a.size()) { }
  constexpr ZuSpan &operator =(std::initializer_list<T> a) {
    m_data = const_cast<T *>(a.begin());
    m_length = a.size();
    return *this;
  }
#if defined(__GNUC__) && !defined(__llvm__)
#pragma GCC diagnostic pop
#endif

// from string literal
  template <typename U, typename Elem>
  struct IsLiteralArray_ : public ZuBool<
    bool(ZuIsSame<U, Elem [sizeof(U) / sizeof(Elem)]>{}) ||
    bool(ZuIsSame<U, Elem (&)[sizeof(U) / sizeof(Elem)]>{})> { };
  template <typename U, typename Elem>
  struct IsLiteralArray_<U, Elem &> : public ZuFalse { };
  template <typename U, typename Elem>
  struct IsLiteralArray_<U, Elem &&> : public ZuFalse { };
  template <typename U>
  struct IsLiteralArray_<U, void> : public ZuFalse { };
  template <typename U, typename V = T>
  struct IsChar_ : public ZuBool<(
	bool(ZuIsSame<ZuStrip<U>, char>{}) ||
	bool(ZuIsSame<ZuStrip<U>, wchar_t>{})) &&
      bool(ZuIsSame<ZuStrip<U>, ZuStrip<V>>{}) &&
      bool(ZuIsConvertible<U, V>{})> { };
  template <
    typename U, typename V = T,
    typename Elem = typename ZuTraits<U>::Elem>
  struct IsStrLiteral : public ZuBool<
    bool(IsChar_<ZuStrip<Elem>>{}) &&
    bool(IsLiteralArray_<U, Elem>{})> { };
  template <typename U, typename R = void>
  using MatchStrLiteral = ZuIfT<IsStrLiteral<U>{}, R>; 

// from array of primitive types
  template <
    typename U, typename V = T,
    typename Elem = typename ZuTraits<U>::Elem>
  struct IsPrimitiveArray : public ZuBool<
    !IsStrLiteral<U>{} &&
    ZuTraits<U>::IsArray &&
    ZuTraits<U>::IsPrimitive &&
    bool(ZuIsSame<Elem, V>{})> { };
  template <typename U, typename R = void>
  using MatchPrimitiveArray = ZuIfT<IsPrimitiveArray<U>{}, R>; 

// from C string (as a pointer, not a primitive array or literal)
  template <typename U>
  struct IsCString : public ZuBool<
    !IsStrLiteral<U>{} &&
    !IsPrimitiveArray<U>{} &&
    ZuTraits<U>::IsPrimitive &&
    bool(IsChar_<U>{}) &&
    ZuTraits<U>::IsCString> { };
  template <typename U, typename R = void>
  using MatchCString = ZuIfT<IsCString<U>{}, R>; 

// from equivalent ZuSpan
  template <
    typename U, typename V = T,
    typename Elem = typename ZuTraits<U>::Elem>
  struct IsZuSpan : public ZuBool<
    !IsStrLiteral<U>{} &&
    !IsPrimitiveArray<U>{} &&
    !IsCString<U>{} &&
    bool(ZuIs_<U, ZuSpan<Elem>>{}) &&
    bool(ZuEquiv<Elem, V>{}) &&
    (bool(ZuIsConst<V>{}) >= bool(ZuIsConst<Elem>{}))> { };
  template <typename U, typename R = void>
  using MatchZuSpan = ZuIfT<IsZuSpan<U>{}, R>; 

// from other array
  template <
    typename U, typename V = T,
    typename Elem = typename ZuTraits<U>::Elem>
  struct IsOtherSpan : public ZuBool<
    !IsStrLiteral<U>{} &&
    !IsPrimitiveArray<U>{} &&
    !IsCString<U>{} &&
    !IsZuSpan<U>{} &&
    (ZuTraits<U>::IsSpan || ZuTraits<U>::IsString) &&
    bool(ZuEquiv<Elem, V>{}) &&
    (bool(ZuIsConst<V>{}) >= bool(ZuIsConst<Elem>{}))> { };
  template <typename U, typename R = void>
  using MatchOtherSpan = ZuIfT<IsOtherSpan<U>{}, R>;

// from pointer
  template <typename U, typename V = T>
  struct IsPtrElem : public ZuBool<
    !IsCString<U *>{} &&
    bool(ZuIsConvertible<ZuNorm<U> *, ZuNorm<V> *>{})> { };
  template <typename U, typename R = void>
  using MatchPtrElem = ZuIfT<IsPtrElem<U>{}, R>;

// compile-time length from string literal
  template <typename A, decltype(MatchStrLiteral<A>(), int()) = 0>
  constexpr ZuSpan(A &&a) noexcept :
    m_data(&a[0]),
    m_length((ZuUnlikely(!(sizeof(a) / sizeof(a[0])) || !a[0])) ? 0U :
      (sizeof(a) / sizeof(a[0])) - 1U) { }
  template <typename A>
  constexpr MatchStrLiteral<A &&, ZuSpan &> operator =(A &&a) noexcept {
    m_data = &a[0];
    m_length = (ZuUnlikely(!(sizeof(a) / sizeof(a[0])) || !a[0])) ? 0U :
      (sizeof(a) / sizeof(a[0])) - 1U;
    return *this;
  }

// compile-time length from primitive array
  template <typename A, decltype(MatchPrimitiveArray<A>(), int()) = 0>
  constexpr ZuSpan(A &&a) noexcept :
    m_data(&a[0]),
    m_length(sizeof(a) / sizeof(a[0])) { }
  template <typename A>
  constexpr MatchPrimitiveArray<A &&, ZuSpan &> operator =(A &&a) noexcept {
    m_data = &a[0];
    m_length = sizeof(a) / sizeof(a[0]);
    return *this;
  }

// length from strlen/wcslen, not constexpr
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress"
#pragma GCC diagnostic ignored "-Wnonnull-compare"
#endif
  template <typename A, decltype(MatchCString<A>(), int()) = 0>
  ZuSpan(A &&a) noexcept :
    m_data{a}, m_length{!a ? 0 : ZuTraits<A>::length(a)} { }
  template <typename A>
  MatchCString<A &&, ZuSpan &> operator =(A &&a) noexcept {
    m_data = a;
    m_length = !a ? 0 : ZuTraits<A>::length(a);
    return *this;
  }
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

private:
  template <typename U, typename V = T>
  ZuInline static constexpr ZuIfT<ZuIsConvertible<U *, V *>{}, T*>
  cast(U *ptr) { return static_cast<T *>(ptr); }
  template <typename U, typename V = T>
  ZuInline static ZuIfT<!ZuIsConvertible<U *, V *>{}, T*>
  cast(U *ptr) { return reinterpret_cast<T *>(ptr); }

public:
// from equivalent ZuSpan
  template <typename A, decltype(MatchZuSpan<A>(), int()) = 0>
  constexpr ZuSpan(A &&a) :
      m_data{cast(a.m_data)},
      m_length{a.m_length} { }
  template <typename A>
  constexpr MatchZuSpan<A &&, ZuSpan &> operator =(A &&a) noexcept {
    m_data = cast(a.m_data);
    m_length = a.m_length;
    return *this;
  }

// from some other array
  template <typename A, typename V = T, decltype(ZuIfT<
      bool(IsOtherSpan<A>{}) && bool(ZuIsConst<V>{})
    >(), int()) = 0>
  constexpr ZuSpan(A &&a) noexcept :
    m_data{cast(ZuTraits<A>::data(a))},
    m_length{!m_data ? 0 : ZuTraits<A>::length(a)} { }
  template <typename A, typename V = T, decltype(ZuIfT<
      bool(IsOtherSpan<A>{}) && !ZuIsConst<V>{}
    >(), int()) = 0>
  constexpr ZuSpan(A &&a) noexcept :
    m_data{cast(ZuTraits<A>::data(const_cast<ZuDecay<A> &>(a)))},
    m_length{!m_data ? 0 : ZuTraits<A>::length(a)} { }
  template <typename A>
  constexpr MatchOtherSpan<A &&, ZuSpan &> operator =(A &&a) noexcept {
    if constexpr (ZuIsConst<T>{})
      m_data = cast(ZuTraits<A>::data(a));
    else
      m_data = cast(ZuTraits<A>::data(const_cast<ZuDecay<A> &>(a)));
    m_length = !m_data ? 0 : ZuTraits<A>::length(a);
    return *this;
  }

// from pointer, length
  template <typename V, decltype(MatchPtrElem<V>(), int()) = 0>
  constexpr ZuSpan(V *data, uint64_t length) noexcept :
    m_data{cast(data)}, m_length{length} { }

  ZuInline constexpr const T *data() const { return m_data; }
  ZuInline constexpr T *data() { return m_data; }

  ZuInline constexpr uint64_t length() const { return m_length; }

  ZuInline constexpr decltype(auto) operator [](this auto &&self, int64_t i) {
    return ZuFwdLike<decltype(self)>(ZuElemVal(self.m_data[i]));
  }

  ZuInline constexpr bool operator !() const { return !length(); }
  ZuOpBool

  constexpr ZuSpan &offset(uint64_t n) {
    if (ZuLikely(n)) {
      if (ZuLikely(n < m_length))
	m_data += n, m_length -= n;
      else
	m_data = nullptr, m_length = 0;
    }
    return *this;
  }

  constexpr ZuSpan &trunc(uint64_t n) {
    if (ZuLikely(n < m_length)) {
      if (ZuLikely(n))
	m_length = n;
      else
	m_data = nullptr, m_length = 0;
    }
    return *this;
  }

  constexpr const ZuSpan &rebase(ptrdiff_t offset) const {
    const_cast<ZuSpan *>(this)->m_data += offset;
    return *this;
  }

// splice operations

// splice():
//   - a span S is assumed to contain exclusively initialized data
//
// conceptually, splice(O, N) replaces an old span O with a new span N
//   - O is clamped to S
//   - N is clamped to S
//   - within S, the span to the left of O is the head H, and to the right is the tail T:
//     | .. H .. | ....... O ....... | ......... T ......... |
//   - O and N start at the same offset in A
//
// 3 cases need to be handled:
//
//   1. N > S - N is beyond the end of the span
//     - this is a no-op
//
//   2. |N| > |O| - this is a shift up of T, and creates a temporary uninitialized gap
//     - first O is destroyed (~) then T is truncated and moved up
//     - N is placement new'd where O used to be
//
//     | .. H .. | ....... O ....... | ......... T ......... |
//     | .. H .. | ------- ~ ------- | ......... T ......... |
//     | .. H .. | ....... U ....... | .... T .... | -- ~ -- |
//                                               \
//     | .. H .. | ............ U ............ | ---- T ---- |
//     | .. H .. | ------------ N ------------ | .... T .... |
//
//   3. |N| <= |O| - this is a shift down of T, and destroys the tail of O
//     - O is destroyed
//     - N is placement new'd where O used to be
//     - T is moved down
//     - length is reduced
//
//     | .. H .. | ....... O ....... | ......... T ......... |
//     | .. H .. | ------- ~ ------- | ......... T ......... |
//     | .. H .. | -- N -- | .. U .. | ......... T ......... |
//                                          /
//     | .. H .. | .. N .. | --------- T --------- |

  // - removed(ZuSpan<T> span)
  //   - span length may be < length if it was clamped by span length
  // - replace(ZuSpan<T> span) -> uint64_t
  //   - span length may be < rlength if it was clamped by span length
  // - ZuSpan::splice differs from ZuArray::splice
  //   - ZuArray is uninitialized in [length_, N)

  template <typename L, typename = void>
  struct IsCallable : public ZuFalse { };
  template <typename L>
  struct IsCallable<L, decltype(ZuDeclVal<L &>()(ZuDeclVal<ZuSpan<T>>()))> :
    public ZuTrue { };

  template <typename Removed, typename Replace>
  constexpr void splice(
    Removed &&removed, int64_t offset, int64_t length,
    Replace &&replace, uint64_t rlength)
  {
    if (ZuUnlikely(!length)) return;
    if (offset < 0) { if ((offset += m_length) < 0) offset = 0; }
    if (length < 0) { if ((length += (m_length - offset)) <= 0) return; }

    // case 1 - no-op for ZuSpan
    if (offset > int64_t(m_length)) {
      if constexpr (IsCallable<Removed>{})
	removed(ZuSpan<T>());
      else
	removed = {};
      return;
    }

    if (offset + rlength > int64_t(m_length)) rlength = m_length - offset;
    if (offset + length > int64_t(m_length)) {
      length = int64_t(m_length) - offset;
      if (length < 0) length = 0;
    }

    // shift up or down, depending on length <=> rlength
    auto shift = int64_t(rlength) - length;
    auto tail = int64_t(m_length) - (offset + length);
    if (tail < 0)
      tail = 0;
    else if (tail > m_length - (offset + rlength)) {
      // truncate tail to fit
      auto tail_ = tail;
      tail = m_length - (offset + rlength);
      auto base = offset + length;
      if (!ZuConstEval()) {
	Ops::destroyElems(data() + base + tail, tail_ - tail);
      } else {
	auto end = base + tail_;
	for (auto i = base + tail; i < end; i++) (*this)[i].~T();
      }
    }
    if constexpr (IsCallable<Removed>{})
      removed(ZuSpan(&m_data[offset], length));
    else
      removed = ZuSpan(&m_data[offset], length);
    if (!ZuConstEval()) {
      auto ptr = data() + offset;
      if (length) Ops::destroyElems(ptr, length);
      if (tail) Ops::moveElems(ptr + length + shift, ptr + length, tail);
    } else {
      {
	auto end = offset + length;
	for (auto i = offset; i < end; i++) (*this)[i].~T();
      }
      if (tail < 0) tail = 0;
      if (tail) {
	if (shift >= 0) {
	  auto end = offset + length;
	  for (auto src = end + tail; --src >= end; ) {
	    auto dst = src + shift;
	    ZuNew<T>(ZuAddr((*this)[dst]), ZuMv((*this)[src]));
	    (*this)[src].~T();
	  }
	} else {
	  auto end = offset + length + tail;
	  for (auto src = end - tail; src < end; ++src) {
	    auto dst = src + shift;
	    ZuNew<T>(ZuAddr((*this)[dst]), ZuMv((*this)[src]));
	    (*this)[src].~T();
	  }
	}
      }
    }
    auto nrlength = replace(ZuSpan(&m_data[offset], rlength));
    if (nrlength < rlength && tail) {
      // replace() didn't use all the space it reserved, shift tail down
      auto rshift = nrlength - rlength;
      if (!ZuConstEval()) {
	auto ptr = data() + offset;
	Ops::moveElems(ptr + nrlength, ptr + rlength, tail);
      } else {
	auto end = offset + rlength + tail;
	for (auto src = end - tail; src < end; ++src) {
	  auto dst = src + rshift;
	  ZuNew<T>(ZuAddr((*this)[dst]), ZuMv((*this)[src]));
	  (*this)[src].~T();
	}
      }
      shift += rshift;
    }
    m_length += shift;
  }
  constexpr void splice(int64_t offset) {
    splice([](ZuSpan<T>) { }, offset, LLONG_MAX, [](ZuSpan<T>) { return 0; }, 0);
  }
  constexpr void splice(int64_t offset, int64_t length) {
    splice([](ZuSpan<T>) { }, offset, length, [](ZuSpan<T>) { return 0; }, 0);
  }
  template <typename Removed>
  constexpr void splice(Removed &&removed, int64_t offset, int64_t length) {
    splice(ZuFwd<Removed>(removed), offset, length, [](ZuSpan<T>) { return 0; }, 0);
  }

  template <typename V>
  constexpr bool equals_(const V &v) const {
    uint64_t l = length();
    uint64_t n = v.length();
    if (l != n) return false;
    return Ops::equals(data(), v.data(), l);
  }
public:
  constexpr bool equals(const ZuSpan &v) const {
    if (this == &v) return true;
    return equals_(v);
  }
  template <typename V> constexpr bool equals(const V &v_) const {
    ZuSpan<const T> v(v_);
    return equals_(v);
  }
private:
  template <typename V>
  constexpr int cmp_(const V &v) const {
    uint64_t l = length();
    uint64_t n = v.length();
    if (int i = Ops::cmp(data(), v.data(), l < n ? l : n)) return i;
    return ZuCmp<uint64_t>::cmp(l, n);
  }
public:
  constexpr int cmp(const ZuSpan &v) const {
    if (this == &v) return 0;
    return cmp_(v);
  }
  template <typename V> constexpr int cmp(const V &v_) const {
    ZuSpan<const T> v(v_);
    return cmp_(v);
  }

  template <typename L, typename R>
  friend constexpr
  ZuIfT<
    ZuIs_<L, ZuSpan>{}() &&
    ZuIsConstructible<R, ZuSpan<const T>>{}(), bool>
  operator ==(const L &l, const R &r) { return l.equals(r); }
  template <typename L, typename R>
  friend constexpr
  ZuIfT<
    ZuIs_<L, ZuSpan>{}() &&
    ZuIsConstructible<R, ZuSpan<const T>>{}(), int>
  operator <=>(const L &l, const R &r) { return l.cmp(r); }

// common prefix
  template <typename R>
  constexpr ZuIfT<ZuIsConstructible<R, ZuSpan>{}, ZuSpan>
  prefix(const R &r_) const {
    ZuSpan r(r_);
    auto n = length(), nr = r.length();
    if (n > nr) n = nr;
    if (!n) return {};
    const auto *data = this->data();
    const auto *rdata = r.data();
    uint64_t i;
    for (i = 0; i < n && data[i] == rdata[i]; i++);
    return {data, i};
  }

// hash code
  uint32_t hash() const { return Ops::hash(data(), length()); }

// iteration - all() is const by default, all<true>() is mutable
  template <bool Mutable = false, typename L>
  constexpr ZuIfT<!Mutable> all(L &&l) const {
    for (uint64_t i = 0, n = length(); i < n; i++) ZuFwd<L>(l)(m_data[i]);
  }
  template <bool Mutable, typename L>
  constexpr ZuIfT<Mutable> all(L &&l) {
    for (uint64_t i = 0, n = length(); i < n; i++) ZuFwd<L>(l)(m_data[i]);
  }

// traits
  struct Traits : public ZuBaseTraits<ZuSpan> {
    using Elem = T;
    enum {
      IsArray = 1, IsPrimitive = 0,
      IsString =
	bool(ZuIsSame<ZuDecay<Elem>, char>{}) ||
	bool(ZuIsSame<ZuDecay<Elem>, wchar_t>{}),
      IsWString = bool(ZuIsSame<ZuDecay<Elem>, wchar_t>{})
    };
    static constexpr Elem *data(ZuSpan &a) { return a.data(); }
    static constexpr const Elem *data(const ZuSpan &a) { return a.data(); }
    static constexpr uint64_t length(const ZuSpan &a) { return a.length(); }
  };
  friend Traits ZuTraitsType(ZuSpan *);

// STL cruft
  using iterator = T *;
  using const_iterator = const T *;
  using iterator_category = std::contiguous_iterator_tag;
  constexpr const T *begin() const { return m_data; }
  constexpr const T *end() const { return m_data + length(); }
  constexpr const T *cbegin() const { return m_data; } // sigh
  constexpr const T *cend() const { return m_data + length(); }
  constexpr T *begin() { return m_data; }
  constexpr T *end() { return m_data + length(); }

private:
  T		*m_data;
  uint64_t	m_length;
};

template <typename T> class ZuSpan_Null {
  constexpr const T *data() const { return nullptr; }
  constexpr uint64_t length() const { return 0; }

  constexpr T operator [](int64_t i) const { return ZuCmp<T>::null(); }

  constexpr bool operator !() const { return true; }

  constexpr void offset(uint64_t) { }

  template <typename L> constexpr void all(L &&) { }
};

template <>
class ZuSpan<void> : public ZuSpan_Null<void> {
public:
  using Elem = void;

  constexpr ZuSpan() { }
  constexpr ZuSpan(const ZuSpan &a) { }
  constexpr ZuSpan &operator =(const ZuSpan &a) { return *this; }

  template <typename A, decltype(ZuIfT<
      ZuTraits<A>::IsArray &&
      ZuIsConstructible<typename ZuTraits<A>::Elem, void>{}>(), int()) = 0>
  constexpr ZuSpan(const A &a) { }
  template <typename A>
  ZuIfT<
    ZuTraits<A>::IsArray &&
    ZuIsConvertible<typename ZuTraits<A>::Elem, void>{}, ZuSpan &>
  operator =(const A &a) { return *this; }

  constexpr ZuSpan(const void *data, uint64_t length) { }
};

// deduction guides

template <typename T, uint64_t N>
ZuSpan(T(&)[N]) -> ZuSpan<T>;
template <typename T, typename N>
ZuSpan(T *, N) -> ZuSpan<T>;

// various standard spans (byte, character, wide character)

using ZuBSpan = ZuSpan<const uint8_t>;
using ZuCSpan = ZuSpan<const char>;
using ZuWSpan = ZuSpan<const wchar_t>;

#endif /* ZuSpan_HH */
