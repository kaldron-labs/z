//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// monomorphic (type-erased) meta-array
// - uses function pointers to wrap any array type into a single
//   realized type for compiled interfaces

#ifndef ZuVArray_HH
#define ZuVArray_HH

#ifndef ZuLib_HH
#include <zlib/ZuLib.hh>
#endif

#include <iterator>

#include <zlib/ZuFmt.hh>
#include <zlib/ZuIter.hh>

namespace ZuVArray_ {

template <typename Array_>
class Elem {
public:
  using Array = Array_;
  using T = typename Array::T;
  static constexpr bool Mutable = Array::Mutable;
  using R = typename Array::R;

  Elem() = delete;
  Elem(Array &array_, uint64_t i_) : array{array_}, i{i_} { }
  Elem(const Elem &) = default;
  Elem &operator =(const Elem &) = default;
  Elem(Elem &&) = default;
  Elem &operator =(Elem &&) = default;

  R get() const;

  operator R() const { return get(); }
  template <
    typename U,
    typename _ = T,
    decltype(ZuIfT<!ZuIsSame<U, _>{} && ZuIsSame<U, ZuUnder<_>>{}>(), int()) = 0>
  operator U() const { return get(); }

  template <bool _ = Mutable>
  ZuIfT<_, Elem &> operator =(T v);

  bool equals(const Elem &r) const { return get() == r.get(); }
  int cmp(const Elem &r) const { return ZuCmp<T>::cmp(get(), r.get()); }
  friend inline bool
  operator ==(const Elem &l, const Elem &r) { return l.equals(r); }
  friend inline int
  operator <=>(const Elem &l, const Elem &r) { return l.cmp(r); }

  bool operator !() const { return !get(); }

  template <typename ...Args>
  decltype(auto) operator [](Args &&...args) const {
    return get().operator [](ZuFwd<Args>(args)...);
  }

  // traits
  using Traits = ZuWrapTraits<Elem, R>;
  friend Traits ZuTraitsType(Elem *);

  // underlying type
  friend R ZuUnderType(Elem *);

private:
  Array		&array;
  uint64_t	i;
};

template <typename T, bool Mutable>
struct Array_SetFn {
  typedef void (*SetFn)(void *, uint64_t, T);

  SetFn		m_setFn = nullptr;
};
template <typename T>
struct Array_SetFn<T, false> { };

template <typename T_, bool Mutable_ = true, typename R_ = T_>
class Array : private Array_SetFn<T_, Mutable_> {
public:
  using T = T_;
  static constexpr bool Mutable = Mutable_;
  using R = R_;
  using SetFn_ = Array_SetFn<T, Mutable>;
  using Elem = ZuVArray_::Elem<Array>;

friend Elem;

  template <typename Array_>
  Array(const Array_ &array) noexcept :
    m_ptr{const_cast<Array_ *>(&array)},
    m_length{ZuTraits<Array_>::length(array)},
    m_getFn{[](const void *ptr, uint64_t i) -> R {
      return (*static_cast<const Array_ *>(ptr))[i];
    }} { }

  template <typename Array_, typename Elem_ = typename ZuTraits<Array_>::Elem>
  Array(Array_ &array) noexcept :
    SetFn_{[](void *ptr, uint64_t i, T elem) {
      (*static_cast<Array_ *>(ptr))[i] = Elem_(ZuMv(elem));
    }},
    m_ptr{&array},
    m_length{ZuTraits<Array_>::length(array)},
    m_getFn{[](const void *ptr, uint64_t i) -> R {
      return (*static_cast<const Array_ *>(ptr))[i];
    }} { }

  template <typename Array_, typename GetFn_>
  Array(Array_ &array, uint64_t length, GetFn_ getFn) noexcept :
    m_ptr{&array},
    m_length{length},
    m_getFn{getFn} { }

  template <
    typename Array_, typename GetFn_, typename SetFn__,
    bool _ = Mutable, ZuIfT<_, int> = 0>
  Array(Array_ &array, uint64_t length, GetFn_ getFn, SetFn__ setFn) noexcept :
    SetFn_{setFn},
    m_ptr{&array},
    m_length{length},
    m_getFn{getFn} { }

  Array() = default;
  Array(const Array &) = default;
  Array &operator =(const Array &) = default;
  Array(Array &&) = default;
  Array &operator =(Array &&) = default;
  ~Array() = default;

  uint64_t length() const { return m_length; }

  const Elem operator[](uint64_t i) const {
    return {const_cast<Array &>(*this), i};
  }
  Elem operator[](uint64_t i) { return {*this, i}; }

// iteration - all() is const by default, all<true>() is mutable
  template <bool _ = false, typename L>
  ZuIfT<!_> all(L &&l) const {
    for (uint64_t i = 0, n = m_length; i < n; i++) ZuFwd<L>(l)((*this)[i]);
  }
  template <bool _, typename L>
  ZuIfT<_> all(L &&l) {
    for (uint64_t i = 0, n = m_length; i < n; i++) ZuFwd<L>(l)((*this)[i]);
  }

// find element - lambda should return true on match
  template <typename L>
  int64_t find(L &&l) const {
    for (uint64_t i = 0, n = m_length; i < n; i++)
      if (ZuFwd<L>(l)((*this)[i])) return i;
    return -1;
  }

// match at start
  template <typename A>
  bool match(const A &a) const {
    uint64_t n = ZuTraits<A>::length(a);
    if (m_length < n) return false;
    for (uint64_t i = 0; i < n; i++)
      if (!(R((*this)[i]) == a[i])) return false;
    return true;
  }

  bool equals(const Array &r) const {
    if (this == &r) return true;
    uint64_t l = length();
    uint64_t n = r.length();
    if (l != n) return false;
    for (uint64_t i = 0; i < n; i++)
      if (R((*this)[i]) != R(r[i])) return false;
    return true;
  }
  int cmp(const Array &r) const {
    if (this == &r) return 0;
    uint64_t ln = m_length;
    uint64_t rn = r.m_length;
    uint64_t n = ln < rn ? ln : rn;
    for (uint64_t i = 0; i < n; i++)
      if (int j = ZuCmp<T>::cmp(R((*this)[i]), R(r[i]))) return j;
    return ZuCompare(ln, rn);
  }
  friend inline bool
  operator ==(const Array &l, const Array &r) { return l.equals(r); }
  friend inline bool
  operator <(const Array &l, const Array &r) { return l.cmp(r) < 0; }
  friend inline int
  operator <=>(const Array &l, const Array &r) { return l.cmp(r); }

  bool operator !() const { return !m_length; }
  ZuOpBool

  using iterator = ZuIter<Array, Elem>;
  using const_iterator = ZuIter<const Array, const Elem>;
  using iterator_category = std::random_access_iterator_tag;
  const_iterator begin() const { return const_iterator{*this, 0}; }
  const_iterator end() const { return const_iterator{*this, m_length}; }
  const_iterator cbegin() const { return const_iterator{*this, 0}; }
  const_iterator cend() const { return const_iterator{*this, m_length}; }
  iterator begin() { return iterator{*this, 0}; }
  iterator end() { return iterator{*this, m_length}; }

private:
  typedef R (*GetFn)(const void *, uint64_t);

  void		*m_ptr = nullptr;
  uint64_t	m_length = 0;
  GetFn		m_getFn = nullptr;
};

template <typename Array>
inline typename Array::R Elem<Array>::get() const {
  return (*array.m_getFn)(array.m_ptr, i);
}

template <typename Array>
template <bool Mutable_>
inline ZuIfT<Mutable_, Elem<Array> &>
Elem<Array>::operator =(typename Array::T v) {
  (*array.m_setFn)(array.m_ptr, i, ZuMv(v));
  return *this;
}

} // ZuVArray_

template <typename T, bool Mutable = true, typename R = T>
using ZuVArray = ZuVArray_::Array<T, Mutable, R>;

#endif /* ZuVArray_HH */
