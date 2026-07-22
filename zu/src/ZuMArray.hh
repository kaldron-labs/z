//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// polymorphic meta-array for on-demand zero-copy transformation of elements
// - wraps an existing array
// - applies a compile-time shim to transform elements as they are get and set
// - element and array comparisons are intentionally omitted because
//   the transformation/copy is presumed to be (somewhat) expensive

#ifndef ZuMArray_HH
#define ZuMArray_HH

#ifndef ZuLib_HH
#include <zlib/ZuLib.hh>
#endif

#include <iterator>

#include <zlib/ZuFmt.hh>
#include <zlib/ZuIter.hh>

namespace ZuMArray_ {

template <typename Array_>
class Elem {
public:
  using Array = Array_;
  using T = typename Array::T;
  using R = typename Array::R;

  Elem() = delete;
  Elem(Array &array_, uint64_t i_) : array{array_}, i{i_} { }
  Elem(const Elem &) = default;
  Elem &operator =(const Elem &) = default;
  Elem(Elem &&) = default;
  Elem &operator =(Elem &&) = default;

  // Note: this noexcept expression is dependent
  // - evaluation is deferred to the definition
  R get() const noexcept(
    noexcept(ZuDeclVal<const typename Array::Impl &>().get(0)));

  operator R() const noexcept(noexcept(get())) { return get(); }
  template <
    typename U,
    typename _ = T,
    decltype(ZuIfT<!ZuIsSame<U, _>{} && ZuIsSame<U, ZuUnder<_>>{}>(), int()) = 0>
  operator U() const noexcept(noexcept(get())) { return get(); }

  template <typename _ = Array>
  ZuMutable<_, Elem &> operator =(T v);

  // traits
  using Traits = ZuWrapTraits<Elem, R>;
  friend Traits ZuTraitsType(Elem *);

  // underlying type
  friend R ZuUnderType(Elem *);

  // if Elem is also an array, ensure it looks like one
  template <
    typename _ = R,
    decltype(ZuDeclVal<const _ &>().begin(), int()) = 0>
  ZuInline auto begin() const { return get().begin(); }
  template <
    typename _ = R,
    decltype(ZuDeclVal<const _ &>().end(), int()) = 0>
  ZuInline auto end() const { return get().end(); }
  template <
    typename _ = R,
    decltype(ZuDeclVal<const _ &>().cbegin(), int()) = 0>
  ZuInline auto cbegin() const { return get().cbegin(); }
  template <
    typename _ = R,
    decltype(ZuDeclVal<const _ &>().cend(), int()) = 0>
  ZuInline auto cend() const { return get().cend(); }

private:
  Array		&array;
  uint64_t	i;
};

// CRTP - implementation must conform to the following interface:
#if 0
using Underlying = ZtArray<U>; // or std::vector<U>, etc.
struct Array : public ZuMArray<Array, Underlying, T> {
  using ZuMArray<Array, Underlying, T>::underlying;
  T get(uint64_t i) const & {
    return underlying[i]; // U -> T
  }
  // set() is optional if Underlying is immutable (const)
  void set(uint64_t i, T &&v) & {
    underlying[i] = v; // T -> U
  }
  // length() is optional if ZuTraits<Underlying>::length() works
  uint64_t length() const { ... }
};
#endif
template <typename Impl_, typename Underlying_, typename T_, typename R_ = T_>
struct Array {
  using Impl = Impl_;
  using Underlying = Underlying_;
  using T = T_;
  using R = R_;

  Underlying	&underlying;	// underlying array

  using Elem = ZuMArray_::Elem<Array>;

friend Elem;

  ZuInline auto impl() { return static_cast<Impl *>(this); }
  ZuInline auto impl() const { return static_cast<const Impl *>(this); }

  template <typename _ = Underlying, ZuMutable<_, int> = 0>
  Array(Underlying &u) noexcept : underlying(u) { }
  Array(const Underlying &u) noexcept :
    underlying(const_cast<Underlying &>(u)) { }

  Array(const Array &) = default;
  Array &operator =(const Array &) = default;
  Array(Array &&) = default;
  Array &operator =(Array &&) = default;
  ~Array() = default;

  // length() can be overridden by Impl
  uint64_t length() const {
    return ZuTraits<Underlying>::length(underlying);
  }

  ZuInline const Elem operator[](uint64_t i) const {
    return {const_cast<Array &>(*this), i};
  }
  template <typename _ = Underlying>
  ZuInline ZuMutable<_, Elem> operator[](uint64_t i) {
    return {*this, i};
  }

// iteration - all() is const by default, all<true>() is mutable
  template <bool Mutable = false, typename _ = Underlying, typename L>
  ZuIfT<!Mutable || bool(ZuIsConst<_>{})> all(L &&l) const {
    for (uint64_t i = 0, n = impl()->length(); i < n; i++)
      ZuFwd<L>(l)((*this)[i]);
  }
  template <bool Mutable, typename _ = Underlying, typename L>
  ZuIfT<Mutable && !ZuIsConst<_>{}> all(L &&l) {
    for (uint64_t i = 0, n = impl()->length(); i < n; i++)
      ZuFwd<L>(l)((*this)[i]);
  }

// find element - lambda should return true on match
  template <typename L>
  int64_t find(L &&l) const {
    for (uint64_t i = 0, n = impl()->length(); i < n; i++)
      if (ZuFwd<L>(l)((*this)[i])) return i;
    return -1;
  }

// match at start
  template <typename A>
  bool match(const A &a) const {
    uint64_t l = impl()->length();
    uint64_t n = ZuTraits<A>::length(a);
    if (l < n) return false;
    for (uint64_t i = 0; i < n; i++)
      if (!(R((*this)[i]) == a[i])) return false;
    return true;
  }

  bool operator !() const { return !impl()->length(); }
  ZuOpBool

  using iterator = ZuIter<Array, Elem>;
  using const_iterator = ZuIter<const Array, const Elem>;
  using iterator_category = std::random_access_iterator_tag;
  const_iterator begin() const {
    return const_iterator{*this, 0};
  }
  const_iterator end() const {
    return const_iterator{*this, impl()->length()};
  }
  const_iterator cbegin() const {
    return const_iterator{*this, 0};
  }
  const_iterator cend() const {
    return const_iterator{*this, impl()->length()};
  }
  iterator begin() { return iterator{*this, 0}; }
  iterator end() { return iterator{*this, impl()->length()}; }
};

template <typename Array>
inline typename Array::R Elem<Array>::get() const noexcept(
  noexcept(ZuDeclVal<const typename Array::Impl &>().get(0)))
{
  return array.impl()->get(i);
}

template <typename Array>
template <typename _>
inline ZuMutable<_, Elem<Array> &>
Elem<Array>::operator =(typename Array::T v) {
  array.impl()->set(i, ZuMv(v));
  return *this;
}

} // ZuMArray_

template <typename Impl, typename Underlying, typename T, typename R = T>
using ZuMArray = ZuMArray_::Array<Impl, Underlying, T, R>;

#endif /* ZuMArray_HH */
