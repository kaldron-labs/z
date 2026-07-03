//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// read-only compile-time structural string literals
// - intended for use as template parameters
// - leverages C++20 string literal operator template for disambiguation:
//   - template <ZuString S> struct X; ... ZuDerive(Y, (X<"foo">));
//   - template <auto     S> struct X; ... ZuDerive(Y, (X<"foo"_Zu>));

#ifndef ZuString_HH
#define ZuString_HH

#ifndef ZuLib_HH
#include <zlib/ZuLib.hh>
#endif

#include <zlib/ZuDerive.hh>
#include <zlib/ZuSpan.hh>
#include <zlib/ZuCmp.hh>
#include <zlib/ZuHash.hh>
#include <zlib/ZuArrayFn.hh>

template <unsigned N_> struct ZuString {
  ZuAssert(N_ > 0);
  ZuAssert(N_ < (1U<<16) - 1U);	// keep it sane

  static constexpr unsigned N = N_;
  using Fn = ZuArrayFn<char>;
  using Cmp = ZuCmp<char>;

  // to be structural, all data members must be public

  char data_[N]{};

  consteval ZuString(const char (&data)[N]) noexcept {
    for (unsigned i = 0; i < N; i++) data_[i] = data[i];
  }

  constexpr auto &&operator [](this auto &&self, unsigned i) {
    return ZuFwdLike<decltype(self)>(self.data_[i]);
  }

  ZuInline constexpr char *data() { return &data_[0]; }
  ZuInline constexpr const char *data() const { return &data_[0]; }
  ZuInline constexpr unsigned length() const { return N - 1; }

  ZuInline constexpr auto span() { return ZuSpan(data(), length()); }
  ZuInline constexpr auto cspan() const { return ZuSpan(data(), length()); }

  ZuInline constexpr bool operator !() const { return !N; }

  ZuInline constexpr bool same(const ZuString &s) const { return this == &s; }
  template <typename A>
  ZuInline constexpr bool same(const A &a) const { return false; }
  template <typename A>
  ZuInline constexpr bool equals(const A &a) const {
    if (ZuConstEval()) {
      if (same(a)) return true;
      unsigned l = length();
      unsigned n = ZuTraits<A>::length(a);
      if (l != n) return false;
      for (unsigned i = 0; i < l; i++)
	if (data_[i] != a[i]) return false;
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
	if (int i = Cmp::cmp(data_[i], a[i])) return i;
      return ZuCompare(l, n);
    } else {
      if (same(a)) return 0;
      return cspan().cmp(a);
    }
  }
  template <typename L, typename R>
  friend ZuInline constexpr ZuIfT<ZuIs_<L, ZuString>{}, bool>
  operator ==(const L &l, const R &r) { return l.equals(r); }
  template <typename L, typename R>
  friend ZuInline constexpr ZuIfT<ZuIs_<L, ZuString>{}, int>
  operator <=>(const L &l, const R &r) { return l.cmp(r); }

  template <typename L>
  constexpr int64_t find(L &&l) const {
    for (unsigned i = 0, n = length(); i < n; i++)
      if (ZuFwd<L>(l)(data_[i])) return i;
    return -1;
  }

  template <typename A>
  ZuInline constexpr bool match(const A &a) const {
    if (ZuConstEval()) {
      unsigned l = length();
      unsigned n = ZuTraits<A>::length(a);
      if (l < n) return false;
      for (unsigned i = 0; i < n; i++)
	if (data_[i] != a[i]) return false;
      return true;
    } else {
      return cspan().match(a);
    }
  }

  uint32_t hash() const { return Fn::hash(data(), length()); }

  friend ZuPrintString ZuPrintType(ZuString *);

// STL cruft

  using iterator = char *;
  using const_iterator = const char *;
  using iterator_category = std::contiguous_iterator_tag;
  ZuInline constexpr const char *begin() const { return data(); }
  ZuInline constexpr const char *end() const { return data() + length(); }
  ZuInline constexpr const char *cbegin() const { return data(); } // sigh
  ZuInline constexpr const char *cend() const { return data() + length(); }
  ZuInline constexpr char *begin() { return data(); }
  ZuInline constexpr char *end() { return data() + length(); }
};

template <unsigned N>
ZuString(const char(&)[N]) -> ZuString<N>;
template <unsigned N>
ZuString(char(&)[N]) -> ZuString<N>;

template <ZuString S>
constexpr auto operator""_Zu() { return S; }

// constant-evaluated strings
template <ZuString A> using ZuStringT = ZuConstant<decltype(A), A>;
template <ZuString ...A> using ZuStringTL = ZuTypeList<ZuStringT<A>...>;

#endif /* ZuString_HH */
