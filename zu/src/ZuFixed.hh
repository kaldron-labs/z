//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// 64bit decimal variable point with variable decimal places,
// 18 significant digits and 10^-<ndp> scaling:
//   <18 - ndp> integer digits
//   <ndp> decimals / fractional digits / number of decimal places
// Note: <ndp> is the negative of the decimal exponent

// use decimal() or fp() for math operations, depending on the use case:
// - ZuDecimal for precise decimal calculations of financial values
// - double for quantitative analysis

// ZuFixedVal is an alias for int64_t - contains the mantissa without the NDP

// ZuFixed combines the value and the ndp - used as an intermediary type
// for conversions, I/O, constructors, scanning:
//   ZuFixed(<integer>, ndp)		// {1042, 2} -> 10.42
//   ZuFixed(<floating point>, ndp)	// {10.42, 2} -> 10.42
//   ZuFixed(<string>, ndp)		// {"10.42", 2} -> 10.42
//   int64_t x = ZuFixed{"42.42", 2}.mantissa	// x == 4242
//   ZuFixed xn{x, 2}; xn *= ZuFixed{2000, 3}; x = xn.mantissa // x == 8484
// printing:
//   s << ZuFixed{...}			// print (default)
//   s << ZuFixed{...}.fmt(ZuFmt...)	// print (compile-time formatted)
//   s << ZuFixed{...}.vfmt(ZuVFmt...)	// print (variable run-time formatted)
//   s << ZuFixed(x, 2)				// s << "42.42"
//   x = 4200042;				// 42000.42
//   s << ZuFixed(x, 2).fmt(ZuFmt::Comma<>())	// s << "42,000.42"

#ifndef ZuFixed_HH
#define ZuFixed_HH

#ifndef ZuLib_HH
#include <zlib/ZuLib.hh>
#endif

#include <zlib/ZuInt.hh>
#include <zlib/ZuCmp.hh>
#include <zlib/ZuDecimalFn.hh>
#include <zlib/ZuTraits.hh>
#include <zlib/ZuPrint.hh>
#include <zlib/ZuTuple.hh>
#include <zlib/ZuFmt.hh>
#include <zlib/ZuBox.hh>
#include <zlib/ZuDecimal.hh>

// ZuFixedVal value range is +/- 10^18

#define ZuFixedMin	(int64_t(-999999999999999999LL))
#define ZuFixedMax	(int64_t(999999999999999999LL))
// ZuFixedReset is the distinct sentinel value used to reset values to null
#define ZuFixedReset	(int64_t(-1000000000000000000LL))
// ZuFixedNull is the null sentinel value
#define ZuFixedNull	(int64_t(-0x8000000000000000LL))

using ZuFixedVal = ZuBoxN(int64_t, ZuFixedNull); // mantissa numerator
using ZuFixedNDP = ZuBox0(uint8_t); // ndp (number of decimal places)

template <typename Fmt> struct ZuFixed_Fmt;	// internal
struct ZuFixed_VFmt;				// internal

struct ZuFixed {
  int64_t	mantissa;
  uint8_t	ndp;

  constexpr ZuFixed() noexcept : mantissa{ZuFixedNull}, ndp{0} { }

  template <typename M, decltype(ZuMatchIntegral<M>(), int()) = 0>
  constexpr ZuFixed(M mantissa_, unsigned ndp_) noexcept :
    mantissa{int64_t(mantissa_)}, ndp{uint8_t(ndp_)} { }

  template <typename V, decltype(ZuMatchFloatingPoint<V>(), int()) = 0>
  constexpr ZuFixed(V v, unsigned ndp_) noexcept :
    mantissa{int64_t(double(v) * ZuDecimalFn::pow10_64(ndp_))},
    ndp{uint8_t(ndp_)} { }

  constexpr ZuFixed(const ZuDecimal &v) noexcept {
    unsigned ndp_ = v.ndp();
    mantissa = v.value / ZuDecimalFn::pow10_128(18 - ndp_);
    ndp = ndp_;
  }
  constexpr ZuFixed(const ZuDecimal &v, unsigned ndp_) noexcept {
    mantissa = v.value / ZuDecimalFn::pow10_128(18 - ndp_);
    ndp = ndp_;
  }

  void init(int64_t mantissa_, unsigned ndp_) {
    mantissa = mantissa_;
    ndp = ndp_;
  }

  void null() {
    mantissa = ZuFixedNull;
    ndp = 0;
  }

  // convert to floating point
  double fp() const {
    if (ZuUnlikely(!operator *())) return ZuFP<double>::nan();
    return double(mantissa) / double(ZuDecimalFn::pow10_64(ndp));
  }

  // convert to ZuDecimal
  ZuDecimal decimal() const {
    return ZuDecimal::Unscaled{
      int128_t(mantissa) * ZuDecimalFn::pow10_64(18 - ndp)};
  }

  // adjust mantissa to another ndp
  ZuFixedVal adjust(unsigned ndp_) const {
    if (ZuUnlikely(!operator *())) return {};
    if (ZuLikely(ndp_ == ndp)) return mantissa;
    if (ndp_ > ndp)
      return int128_t(mantissa) * ZuDecimalFn::pow10_64(ndp_ - ndp);
    return mantissa / ZuDecimalFn::pow10_64(ndp - ndp_);
  }

  // comparisons - uses ZuDecimal intermediaries if NDPs are inconsistent
  bool equals(const ZuFixed &v) const {
    if (ZuLikely(ndp == v.ndp || !**this || !*v))
      return mantissa == v.mantissa;
    return decimal() == v.decimal();
  }
  int cmp(const ZuFixed &v) const {
    if (ZuLikely(ndp == v.ndp || !**this || !*v))
      return (mantissa > v.mantissa) - (mantissa < v.mantissa);
    return decimal().cmp(v.decimal());
  }
  template <typename L, typename R>
  friend inline ZuIfT<ZuIs_<L, ZuFixed>{}, bool>
  operator ==(const L &l, const R &r) { return l.equals(r); }
  template <typename L, typename R>
  friend inline ZuIfT<ZuIs_<L, ZuFixed>{}, bool>
  operator <(const L &l, const R &r) { return l.cmp(r) < 0; }
  template <typename L, typename R>
  friend inline ZuIfT<ZuIs_<L, ZuFixed>{}, int>
  operator <=>(const L &l, const R &r) { return l.cmp(r); }

  // ! is zero, unary * is !null
  bool operator !() const { return !mantissa; }
  ZuOpBool

  constexpr bool operator *() const { return mantissa != ZuFixedNull; }

  // hash
  uint32_t hash() const {
    return
      ZuHash<int64_t>::hash(mantissa) ^
      ZuHash<uint8_t>::hash(ndp);
  }

  // scan from string
  template <typename S, decltype(ZuMatchString<S>(), int()) = 0>
  ZuFixed(const S &s) {
    scan(s);
  }
  template <typename S, decltype(ZuMatchString<S>(), int()) = 0>
  ZuFixed(const S &s, unsigned ndp_) {
    scan(s, ndp_);
  }

  template <bool NDP = true>
  int scan(ZuCSpan s, unsigned ndp_) {
    auto r = eov<NDP>(s, ndp_);
    *this = r.template p<1>();
    return r.template p<0>();
  }
  template <bool NDP = true>
  static ZuTuple<int, ZuFixed> eov(ZuCSpan s, unsigned ndp_) {
    ZuFixed out;
    unsigned int m = 0;
    if (ZuUnlikely(!s)) goto null;
    if (ZuUnlikely(s.length() >= 3 &&
	  s[0] == 'n' && s[1] == 'a' && s[2] == 'n')) {
      return {3, out};
    }
    if constexpr (NDP) if (ndp_ > 18) ndp_ = 18;
    {
      bool negative = s[0] == '-';
      if (ZuUnlikely(negative)) {
	s.offset(1), ++m;
	if (ZuUnlikely(!s)) goto null;
      }
      while (s[0] == '0') {
	s.offset(1), ++m;
	if (!s) goto zero;
      }
      uint64_t iv = 0, fv = 0;
      unsigned n = s.length();
      if (ZuUnlikely(s[0] == '.')) {
	++m;
	if (ZuUnlikely(n == 1)) goto zero;
	if constexpr (!NDP) ndp_ = n - 1;
	goto frac;
      }
      n = Zu_atou(iv, s.data(), n);
      if (ZuUnlikely(!n)) goto null;
      if (ZuUnlikely(n > (18 - ndp_))) goto null; // overflow
      s.offset(n), m += n;
      if constexpr (!NDP) ndp_ = 18 - n;
      if ((n = s.length()) > 1 && s[0] == '.') {
	++m;
  frac:
	if (--n > ndp_) n = ndp_;
	n = Zu_atou(fv, &s[1], n);
	m += n;
	if (fv && n < ndp_)
	  fv *= ZuDecimalFn::pow10_64(ndp_ - n);
      }
      int64_t v = iv * ZuDecimalFn::pow10_64(ndp_) + fv;
      if (ZuUnlikely(negative)) v = -v;
      out.init(v, ndp_);
    }
    return {int(m), out};
  zero:
    out.init(0, ndp_);
    return {int(m), out};
  null:
    return {-1, ZuFixed{}};
  }
  int scan(ZuCSpan s) {
    auto r = eov(s);
    *this = r.p<1>();
    return r.p<0>();
  }
  static ZuTuple<int, ZuFixed> eov(ZuCSpan s) {
    return eov<false>(s, 0);
  }

public:
  // traits
  struct Traits : public ZuBaseTraits<ZuFixed> {
    enum { IsReal = 1, IsSigned = 1 };
  };
  friend Traits ZuTraitsType(ZuFixed *);

  // printing
  template <typename S> void print(S &s) const;
  friend ZuPrintFn ZuPrintType(ZuFixed *);

  template <typename Fmt> ZuFixed_Fmt<Fmt> fmt() const;
  template <bool Upper = false, typename Fmt = ZuFmt::Default>
  ZuFixed_Fmt<ZuFmt::Hex<Upper, Fmt>> hex() const;
  template <
    int NDP = -ZuFmt::Default::NDP_,
    char Trim = '\0',
    typename Fmt = ZuFmt::Default>
  ZuFixed_Fmt<ZuFmt::FP<NDP, Trim, Fmt>> fp() const;

  ZuFixed_VFmt vfmt() const;
  template <typename VFmt>
  ZuFixed_VFmt vfmt(VFmt &&) const;
};

template <typename Fmt> struct ZuFixed_Fmt {
  const ZuFixed	&value;

  template <typename S> void print(S &s) const {
    if (ZuUnlikely(!*value)) { s << "nan"; return; }
    auto iv = value.mantissa;
    if (ZuUnlikely(iv < 0)) { s << '-'; iv = -iv; }
    uint64_t factor = ZuDecimalFn::pow10_64(value.ndp);
    ZuFixedVal fv = iv % factor;
    iv /= factor;
    s << ZuBoxed(iv).template fmt<Fmt>();
    auto ndp_ = value.ndp;
    if (fv) s << '.' << ZuBoxed(fv).vfmt().frac(ndp_, ndp_);
  }
  friend ZuPrintFn ZuPrintType(ZuFixed_Fmt *);
};
template <class Fmt>
inline ZuFixed_Fmt<Fmt> ZuFixed::fmt() const
{
  return ZuFixed_Fmt<Fmt>{*this};
}
template <bool Upper, typename Fmt>
inline ZuFixed_Fmt<ZuFmt::Hex<Upper, Fmt>> ZuFixed::hex() const
{
  return ZuFixed_Fmt<ZuFmt::Hex<Upper, Fmt>>{*this};
}
template <int NDP, char Trim, typename Fmt>
inline ZuFixed_Fmt<ZuFmt::FP<NDP, Trim, Fmt>> ZuFixed::fp() const
{
  return ZuFixed_Fmt<ZuFmt::FP<NDP, Trim, Fmt>>{*this};
}
template <typename S>
inline void ZuFixed::print(S &s) const
{
  s << ZuFixed_Fmt<ZuFmt::Default>{*this};
}
struct ZuFixed_VFmt : public ZuVFmtWrapper<ZuFixed_VFmt> {
  const ZuFixed	&value;

  ZuFixed_VFmt(const ZuFixed &value_) : value{value_} { }
  template <typename VFmt>
  ZuFixed_VFmt(const ZuFixed &value_, VFmt &&fmt) :
    ZuVFmtWrapper<ZuFixed_VFmt>{ZuFwd<VFmt>(fmt)}, value{value_} { }

  template <typename S> void print(S &s) const {
    if (ZuUnlikely(!*value)) { s << "nan"; return; }
    ZuFixedVal iv = value.mantissa;
    if (ZuUnlikely(iv < 0)) { s << '-'; iv = -iv; }
    uint64_t factor = ZuDecimalFn::pow10_64(value.ndp);
    ZuFixedVal fv = iv % factor;
    iv /= factor;
    s << ZuBoxed(iv).vfmt(this->fmt);
    auto ndp_ = value.ndp;
    if (fv) s << '.' << ZuBoxed(fv).vfmt().frac(ndp_, ndp_);
  }
  friend ZuPrintFn ZuPrintType(ZuFixed_VFmt *);
};
inline ZuFixed_VFmt ZuFixed::vfmt() const {
  return ZuFixed_VFmt{*this};
}
template <typename VFmt>
inline ZuFixed_VFmt ZuFixed::vfmt(VFmt &&fmt) const {
  return ZuFixed_VFmt{*this, ZuFwd<VFmt>(fmt)};
}

template <> struct ZuCmp<ZuFixed> {
  template <typename L, typename R>
  static constexpr int cmp(const L &l, const R &r) { return l.cmp(r); }
  template <typename L, typename R>
  static constexpr bool equals(const L &l, const R &r) { return l == r; }
  template <typename L, typename R>
  static constexpr bool less(const L &l, const R &r) { return l < r; }
  static constexpr bool null(const ZuFixed &v) { return !*v; }
  static constexpr ZuFixed null() { return {}; }
  static constexpr ZuFixed minimum() { return {ZuFixedMin, 0}; }
  static constexpr ZuFixed maximum() { return {ZuFixedMax, 0}; }
};

#endif /* ZuFixed_HH */
