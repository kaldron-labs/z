//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// "boxed" primitive types (concept borrowed from C#)

#ifndef ZuBox_HH
#define ZuBox_HH

#ifndef ZuLib_HH
#include <zlib/ZuLib.hh>
#endif

#include <limits.h>
#include <math.h>
#include <float.h>

#include <zlib/ZuAssert.hh>
#include <zlib/ZuTraits.hh>
#include <zlib/ZuCmp.hh>
#include <zlib/ZuHash.hh>
#include <zlib/ZuInt.hh>
#include <zlib/ZuPrint.hh>
#include <zlib/ZuSpan.hh>

#include <zlib/ZuFP.hh>
#include <zlib/ZuFmt.hh>

#include <zlib/Zu_ntoa.hh>
#include <zlib/Zu_aton.hh>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:4312 4800)
#endif

template <typename T, typename NTP> class ZuBox;
template <typename> struct ZuIsBoxed : public ZuFalse { };
template <typename T, typename NTP>
struct ZuIsBoxed<ZuBox<T, NTP>> : public ZuTrue { };

template <typename U, typename R = void>
using ZuMatchBoxed = ZuIfT<ZuIsBoxed<U>{}, R>;
template <typename U, typename R = void>
using ZuNotBoxed = ZuIfT<!ZuIsBoxed<U>{}, R>;

// compile-time formatting
constexpr auto ZuBox_NullAsIs() { // sentinel to disable null string
  return []() -> ZuCSpan { return {}; };
};
constexpr auto ZuBox_NullString() { // null as empty string
  return []() -> ZuCSpan { return {}; };
};

template <typename T, typename Fmt, auto IsNull, auto NullString,
  bool Signed = ZuTraits<T>::IsSigned,
  bool FloatingPoint = Signed && ZuTraits<T>::IsFloatingPoint>
struct ZuBox_Print;
template <typename T, typename Fmt, auto IsNull>
struct ZuBox_Print<T, Fmt, IsNull, ZuBox_NullAsIs(), 0, 0> {
  static unsigned length(T v) { return Zu_nprint<Fmt>::ulen(v); }
  static unsigned print(T v, char *buf) { return Zu_nprint<Fmt>::utoa(v, buf); }
};
template <typename T, typename Fmt, auto IsNull>
struct ZuBox_Print<T, Fmt, IsNull, ZuBox_NullAsIs(), 1, 0> {
  static unsigned length(T v) { return Zu_nprint<Fmt>::ilen(v); }
  static unsigned print(T v, char *buf) { return Zu_nprint<Fmt>::itoa(v, buf); }
};
template <typename T, typename Fmt, auto IsNull>
struct ZuBox_Print<T, Fmt, IsNull, ZuBox_NullAsIs(), 1, 1> {
  static unsigned length(T v) { return Zu_nprint<Fmt>::flen(v); }
  static unsigned print(T v, char *buf) { return Zu_nprint<Fmt>::ftoa(v, buf); }
};
template <typename T, typename Fmt, auto IsNull, auto NullString>
struct ZuBox_Print<T, Fmt, IsNull, NullString, 0, 0> {
  static unsigned length(T v) {
    if (ZuUnlikely(IsNull(v))) return NullString().length();
    return Zu_nprint<Fmt>::ulen(v);
  }
  static unsigned print(T v, char *buf) {
    if (ZuUnlikely(IsNull(v))) {
      auto s = NullString();
      memcpy(buf, s.data(), s.length());
      return s.length();
    }
    return Zu_nprint<Fmt>::utoa(v, buf);
  }
};
template <typename T, typename Fmt, auto IsNull, auto NullString>
struct ZuBox_Print<T, Fmt, IsNull, NullString, 1, 0> {
  static unsigned length(T v) {
    if (ZuUnlikely(IsNull(v))) return NullString().length();
    return Zu_nprint<Fmt>::ilen(v);
  }
  static unsigned print(T v, char *buf) {
    if (ZuUnlikely(IsNull(v))) {
      auto s = NullString();
      memcpy(buf, s.data(), s.length());
      return s.length();
    }
    return Zu_nprint<Fmt>::itoa(v, buf);
  }
};
template <typename T, typename Fmt, auto IsNull, auto NullString>
struct ZuBox_Print<T, Fmt, IsNull, NullString, 1, 1> {
  static unsigned length(T v) {
    if (ZuUnlikely(IsNull(v))) return NullString().length();
    return Zu_nprint<Fmt>::flen(v);
  }
  static unsigned print(T v, char *buf) {
    if (ZuUnlikely(IsNull(v))) {
      auto s = NullString();
      memcpy(buf, s.data(), s.length());
      return s.length();
    }
    return Zu_nprint<Fmt>::ftoa(v, buf);
  }
};

// variable run-time formatting
template <typename T, auto IsNull, auto NullString,
  bool Signed = ZuTraits<T>::IsSigned,
  bool FloatingPoint = Signed && ZuTraits<T>::IsFloatingPoint>
struct ZuBox_VPrint;
template <typename T, auto IsNull>
struct ZuBox_VPrint<T, IsNull, ZuBox_NullAsIs(), 0, 0> {
  static unsigned length(const ZuVFmt &fmt, T v) {
    return Zu_vprint::ulen(fmt, v);
  }
  static unsigned print(const ZuVFmt &fmt, T v, char *buf) {
    return Zu_vprint::utoa(fmt, v, buf);
  }
};
template <typename T, auto IsNull>
struct ZuBox_VPrint<T, IsNull, ZuBox_NullAsIs(), 1, 0> {
  static unsigned length(const ZuVFmt &fmt, T v) {
    return Zu_vprint::ilen(fmt, v);
  }
  static unsigned print(const ZuVFmt &fmt, T v, char *buf) {
    return Zu_vprint::itoa(fmt, v, buf);
  }
};
template <typename T, auto IsNull>
struct ZuBox_VPrint<T, IsNull, ZuBox_NullAsIs(), 1, 1> {
  static unsigned length(const ZuVFmt &fmt, T v) {
    return Zu_vprint::flen(fmt, v);
  }
  static unsigned print(const ZuVFmt &fmt, T v, char *buf) {
    return Zu_vprint::ftoa(fmt, v, buf);
  }
};
template <typename T, auto IsNull, auto NullString>
struct ZuBox_VPrint<T, IsNull, NullString, 0, 0> {
  static unsigned length(const ZuVFmt &fmt, T v) {
    if (ZuUnlikely(IsNull(v))) return NullString().length();
    return Zu_vprint::ulen(fmt, v);
  }
  static unsigned print(const ZuVFmt &fmt, T v, char *buf) {
    if (ZuUnlikely(IsNull(v))) {
      auto s = NullString();
      memcpy(buf, s.data(), s.length());
      return s.length();
    }
    return Zu_vprint::utoa(fmt, v, buf);
  }
};
template <typename T, auto IsNull, auto NullString>
struct ZuBox_VPrint<T, IsNull, NullString, 1, 0> {
  static unsigned length(const ZuVFmt &fmt, T v) {
    if (ZuUnlikely(IsNull(v))) return NullString().length();
    return Zu_vprint::ilen(fmt, v);
  }
  static unsigned print(const ZuVFmt &fmt, T v, char *buf) {
    if (ZuUnlikely(IsNull(v))) {
      auto s = NullString();
      memcpy(buf, s.data(), s.length());
      return s.length();
    }
    return Zu_vprint::itoa(fmt, v, buf);
  }
};
template <typename T, auto IsNull, auto NullString>
struct ZuBox_VPrint<T, IsNull, NullString, 1, 1> {
  static unsigned length(const ZuVFmt &fmt, T v) {
    if (ZuUnlikely(IsNull(v))) return NullString().length();
    return Zu_vprint::flen(fmt, v);
  }
  static unsigned print(const ZuVFmt &fmt, T v, char *buf) {
    if (ZuUnlikely(IsNull(v))) {
      auto s = NullString();
      memcpy(buf, s.data(), s.length());
      return s.length();
    }
    return Zu_vprint::ftoa(fmt, v, buf);
  }
};

template <typename T, typename Fmt, auto Null, auto NullString,
  bool Signed = ZuTraits<T>::IsSigned,
  bool FloatingPoint = Signed && ZuTraits<T>::IsFloatingPoint>
struct ZuBox_Scan;
template <typename T_, typename Fmt, auto Null>
struct ZuBox_Scan<T_, Fmt, Null, ZuBox_NullAsIs(), 0, 0> {
  using T = uint64_t;
  static unsigned scan(T &v, const char *buf, unsigned n) {
    return Zu_nscan<Fmt>::atou(v, buf, n);
  }
};
template <typename T_, typename Fmt, auto Null>
struct ZuBox_Scan<T_, Fmt, Null, ZuBox_NullAsIs(), 1, 0> {
  using T = int64_t;
  static unsigned scan(T &v, const char *buf, unsigned n) {
    return Zu_nscan<Fmt>::atoi(v, buf, n);
  }
};
template <typename T_, typename Fmt, auto Null>
struct ZuBox_Scan<T_, Fmt, Null, ZuBox_NullAsIs(), 1, 1> {
  using T = T_;
  static unsigned scan(T &v, const char *buf, unsigned n) {
    return Zu_nscan<Fmt>::atof(v, buf, n);
  }
};
template <typename T_, typename Fmt, auto Null, auto NullString>
struct ZuBox_Scan<T_, Fmt, Null, NullString, 0, 0> {
  using T = uint64_t;
  static unsigned scan(T &v, const char *buf, unsigned n) {
    unsigned o = Zu_nscan<Fmt>::atou(v, buf, n);
    if (ZuLikely(o)) return o;
    auto null = NullString();
    o = null.length();
    if (n >= o && !memcmp(null.data(), buf, o)) {
      v = Null();
      return o;
    }
    return 0;
  }
};
template <typename T_, typename Fmt, auto Null, auto NullString>
struct ZuBox_Scan<T_, Fmt, Null, NullString, 1, 0> {
  using T = int64_t;
  static unsigned scan(T &v, const char *buf, unsigned n) {
    unsigned o = Zu_nscan<Fmt>::atoi(v, buf, n);
    if (ZuLikely(o)) return o;
    auto null = NullString();
    o = null.length();
    if (n >= o && !memcmp(null.data(), buf, o)) {
      v = Null();
      return o;
    }
    return 0;
  }
};
template <typename T_, typename Fmt, auto Null, auto NullString>
struct ZuBox_Scan<T_, Fmt, Null, NullString, 1, 1> {
  using T = T_;
  static unsigned scan(T &v, const char *buf, unsigned n) {
    unsigned o = Zu_nscan<Fmt>::atof(v, buf, n);
    if (ZuLikely(o)) return o;
    auto null = NullString();
    o = null.length();
    if (n >= o && !memcmp(null.data(), buf, o)) {
      v = Null();
      return o;
    }
    return 0;
  }
};

// generic printing
template <typename B> struct ZuBoxPrint : public ZuPrintBuffer {
  static unsigned length(const B &b) {
    return b.length();
  }
  static unsigned print(char *buf, unsigned, const B &b) {
    return b.print(buf);
  }
};

// compile-time formatting
template <typename Boxed, typename Fmt>
class ZuBoxFmt {
template <typename, typename> friend class ZuBox;

  using Print =
    ZuBox_Print<typename Boxed::T, Fmt, Boxed::isNull, Boxed::NullString>;

  ZuBoxFmt(const Boxed &ref) : m_ref(ref) { }

public:
  unsigned length() const { return Print::length(m_ref); }
  unsigned print(char *buf) const { return Print::print(m_ref, buf); }

  friend ZuBoxPrint<ZuBoxFmt> ZuPrintType(ZuBoxFmt *);

private:
  const Boxed	&m_ref;
};

// run-time formatting
template <typename Boxed>
class ZuBoxVFmt : public ZuVFmtWrapper<ZuBoxVFmt<Boxed>> {
template <typename, typename> friend class ZuBox;

  using Print =
    ZuBox_VPrint<typename Boxed::T, Boxed::isNull, Boxed::NullString>;

public:
  ZuBoxVFmt(const Boxed &v) : m_value(v) { }
  template <typename VFmt>
  ZuBoxVFmt(const Boxed &v, VFmt &&fmt) :
    ZuVFmtWrapper<ZuBoxVFmt>{ZuFwd<VFmt>(fmt)}, m_value{v} { }

  // print
  unsigned length() const {
    return Print::length(this->fmt, m_value);
  }
  unsigned print(char *buf) const {
    return Print::print(this->fmt, m_value, buf);
  }
  friend ZuBoxPrint<ZuBoxVFmt> ZuPrintType(ZuBoxVFmt *);

private:
  const Boxed	&m_value;
};

// SFINAE...
template <typename U, typename T, typename R = void>
using ZuBox_MatchReal =
  ZuIfT<!ZuIsBoxed<U>{} && !ZuTraits<U>::IsString && (
      ZuTraits<U>::IsReal ||
      ZuIsConvertible<U, T>{} ||
      ZuIsConvertible<U, int>{}), R>;

template <typename Traits, typename R, bool IsPointer, bool IsArray>
struct ZuBox_MatchCharPtr_ { };
template <typename Traits, typename R>
struct ZuBox_MatchCharPtr_<Traits, R, true, false> :
    public ZuIfT_<ZuIsSame<ZuStrip<typename Traits::Elem>, char>{}, R> { };
template <typename Traits, typename R>
struct ZuBox_MatchCharPtr_<Traits, R, false, true> :
    public ZuIfT_<ZuIsSame<ZuStrip<typename Traits::Elem>, char>{}, R> { };
template <typename S, typename R = void>
using ZuBox_MatchCharPtr =
  typename ZuBox_MatchCharPtr_<ZuTraits<S>, R,
    ZuTraits<S>::IsPointer,
    ZuTraits<S>::IsArray>::T;

// NTP (named template parameters):

// NTP defaults
struct ZuBox_Defaults {
  template <typename T> using CmpT = ZuCmp<T>;
  static constexpr auto NullString = ZuBox_NullAsIs();
};

// ZuBoxCmp - the comparator
template <template <typename> class Cmp_, typename NTP = ZuBox_Defaults>
struct ZuBoxCmp : public NTP {
  template <typename T> using CmpT = Cmp_<T>;
};

// ZuBoxNullString - printing/scanning of sentinel null value
template <auto NullString_, typename NTP = ZuBox_Defaults>
struct ZuBoxNullString : public NTP {
  static constexpr auto NullString = NullString_;
};

template <typename T_, typename NTP = ZuBox_Defaults>
class ZuBox {
template <typename, typename> friend class ZuBox;
template <typename, typename> friend class ZuBoxFmt;
template <typename> friend class ZuBoxVFmt;

public:
  using T = ZuUnder<T_>;
  using Cmp = typename NTP::template CmpT<T>;
  static constexpr auto NullString = NTP::NullString;

  static constexpr T null() { return Cmp::null(); }
  static constexpr bool isNull(T v) { return Cmp::null(v); }

private:
  template <typename Fmt = ZuFmt::Default>
  using Scan = ZuBox_Scan<T, Fmt, null, NullString>;

  template <typename Fmt = ZuFmt::Default>
  using Print = ZuBox_Print<T, Fmt, isNull, NullString>;

  ZuAssert(ZuTraits<T>::IsPrimitive && ZuTraits<T>::IsReal);

public:
  constexpr ZuBox() noexcept : m_val(Cmp::null()) { }

  constexpr ZuBox(const ZuBox &b) noexcept : m_val(b.m_val) { }
  constexpr ZuBox &operator =(const ZuBox &b) noexcept {
    m_val = b.m_val;
    return *this;
  }

  template <typename R, decltype(ZuBox_MatchReal<R, T>(), int()) = 0>
  constexpr ZuBox(R r) noexcept : m_val(r) { }

  template <typename B, decltype(ZuMatchBoxed<B>(), int()) = 0>
  constexpr ZuBox(B b) noexcept :
    m_val(!*b ? static_cast<T>(Cmp::null()) : static_cast<T>(b.m_val)) { }

  template <typename S, decltype(ZuMatchCharString<S>(), int()) = 0>
  ZuBox(S &&s_) noexcept : m_val(Cmp::null()) {
    ZuCSpan s(s_);
    typename Scan<>::T val = 0;
    if (ZuLikely(s && Scan<>::scan(val, s.data(), s.length())))
      m_val = val;
  }
  template <
    typename Fmt, typename S,
    decltype(ZuMatchCharString<S>(), int()) = 0>
  ZuBox(Fmt, S &&s_) noexcept : m_val(Cmp::null()) {
    ZuCSpan s(s_);
    typename Scan<Fmt>::T val = 0;
    if (ZuLikely(s && Scan<Fmt>::scan(val, s.data(), s.length())))
      m_val = val;
  }

  template <typename S, decltype(ZuBox_MatchCharPtr<S>(), int()) = 0>
  ZuBox(S s, unsigned len) noexcept : m_val(Cmp::null()) {
    typename Scan<>::T val = 0;
    if (ZuLikely(s && Scan<>::scan(val, s, len)))
      m_val = val;
  }
  template <
    typename Fmt, typename S,
    decltype(ZuBox_MatchCharPtr<S>(), int()) = 0>
  ZuBox(Fmt, S s, unsigned len) noexcept : m_val(Cmp::null()) {
    typename Scan<Fmt>::T val = 0;
    if (ZuLikely(s && Scan<Fmt>::scan(val, s, len)))
      m_val = val;
  }

  T val() const { return m_val; }

private:
  template <typename R>
  ZuBox_MatchReal<R, T> assign(R r) { m_val = r; }

  template <typename B>
  ZuMatchBoxed<B> assign(const B &b) {
    m_val = !*b ? static_cast<T>(Cmp::null()) : static_cast<T>(b.m_val);
  }

  template <typename S>
  ZuMatchCharString<S> assign(S &&s_) {
    ZuCSpan s(s_);
    typename Scan<>::T val = 0;
    if (ZuUnlikely(!s || !Scan<>::scan(val, s.data(), s.length())))
      m_val = Cmp::null();
    else
      m_val = val;
  }

public:
  template <typename T>
  ZuBox &operator =(T &&t) { assign(ZuFwd<T>(t)); return *this; }

  template <typename T>
  static constexpr ZuMatchFloatingPoint<T, bool>
  equals_(T t1, T t2) {
    if (Cmp::null(t2)) return Cmp::null(t1);
    if (Cmp::null(t1)) return false;
    return t1 == t2;
  }
  template <typename T>
  static constexpr ZuNotFloatingPoint<T, bool>
  equals_(T t1, T t2) {
    return t1 == t2;
  }
  constexpr bool equals(const ZuBox &b) const {
    return equals_(m_val, b.m_val);
  }

  template <typename Cmp__ = Cmp>
  constexpr ZuIfT<!ZuIsSame<Cmp__, ZuCmp0<T>>{}, int>
  cmp_(const ZuBox &b) const {
    if (Cmp::null(b.m_val)) return Cmp::null(m_val) ? 0 : 1;
    if (Cmp::null(m_val)) return -1;
    return Cmp::cmp(m_val, b.m_val);
  }
  template <typename Cmp__ = Cmp>
  constexpr ZuIfT<ZuIsSame<Cmp__, ZuCmp0<T>>{}, int>
  cmp_(const ZuBox &b) const {
    return Cmp::cmp(m_val, b.m_val);
  }
  constexpr int cmp(const ZuBox &b) const { return cmp_(b.m_val); }
  template <typename L, typename R>
  friend constexpr ZuIfT<ZuIs_<L, ZuBox>{}, bool>
  operator ==(const L &l, const R &r) { return l.equals(r); }
  template <typename L, typename R>
  friend constexpr ZuIfT<ZuIs_<L, ZuBox>{}, int>
  operator <=>(const L &l, const R &r) { return l.cmp(r); }

  constexpr bool operator !() const { return !m_val; }
  constexpr bool operator *() const { return !Cmp::null(m_val); }

  uint32_t hash() const { return ZuHash<T>::hash(m_val); }

  constexpr operator T() const { return m_val; }
  constexpr operator T &() & { return m_val; }

  // compile-time formatting
  template <typename Fmt = ZuFmt::Default>
  ZuBoxFmt<ZuBox, Fmt> fmt() const { 
    return ZuBoxFmt<ZuBox, Fmt>{*this};
  }
  template <bool Upper = false, typename Fmt = ZuFmt::Default>
  ZuBoxFmt<ZuBox, ZuFmt::Hex<Upper, Fmt>> hex() const {
    return ZuBoxFmt<ZuBox, ZuFmt::Hex<Upper, Fmt>>{*this};
  }
  template <
    int NDP = -ZuFmt::Default::NDP_,
    char Trim = '\0',
    typename Fmt = ZuFmt::Default>
  ZuBoxFmt<ZuBox, ZuFmt::FP<NDP, Trim, Fmt>> fp() const {
    return ZuBoxFmt<ZuBox, ZuFmt::FP<NDP, Trim, Fmt>>{*this};
  }
  // run-time formatting
  ZuBoxVFmt<ZuBox> vfmt() const {
    return ZuBoxVFmt<ZuBox>{*this};
  }
  template <typename VFmt>
  ZuBoxVFmt<ZuBox> vfmt(VFmt &&fmt) const {
    return ZuBoxVFmt<ZuBox>{*this, ZuFwd<VFmt>(fmt)};
  }

  template <typename Fmt = ZuFmt::Default, typename S>
  ZuMatchCharString<S, unsigned> scan(S &&s_) {
    ZuCSpan s(s_);
    typename Scan<>::T val = 0;
    unsigned n = Scan<Fmt>::scan(val, s.data(), s.length());
    if (ZuUnlikely(!n)) {
      m_val = Cmp::null();
      return 0;
    }
    m_val = val;
    return n;
  }
  template <typename Fmt = ZuFmt::Default, typename S>
  ZuBox_MatchCharPtr<S, unsigned> scan(S s, unsigned len) {
    typename Scan<Fmt>::T val = 0;
    unsigned n = Scan<Fmt>::scan(val, s, len);
    if (ZuUnlikely(!n)) {
      m_val = Cmp::null();
      return 0;
    }
    m_val = val;
    return n;
  }

  unsigned length() const { return Print<>::length(m_val); }
  unsigned print(char *buf) const { return Print<>::print(m_val, buf); }

  // ZuBox operators intentionally behave identically to those
  // on the underlying primitive type - there is no explicit
  // handling of sentinel nulls, NaNs, infinities, overflow etc.

  constexpr ZuBox operator -() { return -m_val; }

  template <typename R>
  constexpr ZuBox operator +(const R &r) const { return m_val + r; }
  template <typename R>
  constexpr ZuBox operator -(const R &r) const { return m_val - r; }
  template <typename R>
  constexpr ZuBox operator *(const R &r) const { return m_val * r; }
  template <typename R>
  constexpr ZuBox operator /(const R &r) const { return m_val / r; }
  template <typename R>
  constexpr ZuBox operator %(const R &r) const { return m_val % r; }
  template <typename R>
  constexpr ZuBox operator |(const R &r) const { return m_val | r; }
  template <typename R>
  constexpr ZuBox operator &(const R &r) const { return m_val & r; }
  template <typename R>
  constexpr ZuBox operator ^(const R &r) const { return m_val ^ r; }

  constexpr ZuBox operator ++(int) { return m_val++; }
  constexpr ZuBox &operator ++() { ++m_val; return *this; }
  constexpr ZuBox operator --(int) { return m_val--; }
  constexpr ZuBox &operator --() { --m_val; return *this; }

  template <typename R>
  constexpr ZuBox &operator +=(const R &r) { m_val += r; return *this; }
  template <typename R>
  constexpr ZuBox &operator -=(const R &r) { m_val -= r; return *this; }
  template <typename R>
  constexpr ZuBox &operator *=(const R &r) { m_val *= r; return *this; }
  template <typename R>
  constexpr ZuBox &operator /=(const R &r) { m_val /= r; return *this; }
  template <typename R>
  constexpr ZuBox &operator %=(const R &r) { m_val %= r; return *this; }
  template <typename R>
  constexpr ZuBox &operator |=(const R &r) { m_val |= r; return *this; }
  template <typename R>
  constexpr ZuBox &operator &=(const R &r) { m_val &= r; return *this; }
  template <typename R>
  constexpr ZuBox &operator ^=(const R &r) { m_val ^= r; return *this; }

  // apply update (leaves existing value in place if u is null)
  constexpr ZuBox &update(const ZuBox &u) {
    if (!Cmp::null(u)) m_val = u.m_val;
    return *this;
  }
  // apply update, with additional sentinel value signifying "reset to null"
  constexpr ZuBox &update(const ZuBox &u, const ZuBox &reset) {
    if (!Cmp::null(u)) {
      if (u == reset)
	m_val = Cmp::null();
      else
	m_val = u.m_val;
    }
    return *this;
  }

  // infinity (positive)
  constexpr ZuInline static ZuBox inf() { return ZuBox{Cmp::inf()}; }

  // the decimal epsilon functions below are intended for use with
  // systems that use floating point for decimal values - unless
  // this is required, use ZuDecimal and/or ZuFixed in preference
  // to ZuBox<double>

  // decimal epsilon of floating point type
  ZuBox epsilon() const { return ZuBox{Cmp::epsilon(m_val)}; }

  // floating point equality using decimal epsilon
  bool feq(T r) const {
    if (Cmp::null(m_val)) return Cmp::null(r);
    if (Cmp::null(r)) return false;
    return feq_(r);
  }
  bool feq_(T r) const {
    if (ZuLikely(m_val == r)) return true;
    if (ZuLikely(m_val >= 0.0)) {
      if (r < 0.0) return false;
      if (m_val > r) return m_val - r < Cmp::epsilon_(m_val);
      return r - m_val < Cmp::epsilon_(r);
    }
    if (r > 0.0) return false;
    T val = -m_val;
    r = -r;
    if (val > r) return val - r < Cmp::epsilon_(val);
    return r - val < Cmp::epsilon_(r);
  }

  // floating point comparison using decimal epsilon
  bool fne(T r) const { return !feq(r); }
  bool fge(T r) const {
    if (Cmp::null(m_val)) return Cmp::null(r);
    if (Cmp::null(r)) return true;
    if (ZuLikely(m_val >= r)) return true;
    if (ZuLikely(m_val >= 0.0)) return r - m_val < Cmp::epsilon_(r);
    if (r > 0.0) return false;
    T val = -m_val;
    r = -r;
    if (val > r) return val - r < Cmp::epsilon_(val);
    return r - val < Cmp::epsilon_(r);
  }
  bool fle(T r) const {
    if (Cmp::null(m_val)) return Cmp::null(r);
    if (Cmp::null(r)) return false;
    if (ZuLikely(m_val <= r)) return true;
    if (ZuLikely(m_val >= 0.0)) {
      if (r < 0.0) return false;
      if (m_val > r) return m_val - r < Cmp::epsilon_(m_val);
      return r - m_val < Cmp::epsilon_(r);
    }
    T val = -m_val;
    r = -r;
    return r - val < Cmp::epsilon_(r);
  }
  bool fgt(T r) const { return !fle(r); }
  bool flt(T r) const { return !fge(r); }

  int fcmp(T r) const {
    if (Cmp::null(r)) return Cmp::null(m_val) ? 0 : 1;
    if (Cmp::null(m_val)) return -1;
    if (feq_(r)) return 0;
    return m_val > r ? 1 : -1;
  }

  // traits
  struct Traits : public ZuTraits<T> { enum { IsPrimitive = 0 }; };
  friend Traits ZuTraitsType(ZuBox *);

  // printing
  friend ZuBoxPrint<ZuBox> ZuPrintType(ZuBox *);

  // underlying
  friend T ZuUnderType(ZuBox *);

private:
  T	m_val;
};

#define ZuBox0(T) ZuBox<T, ZuBoxCmp<ZuCmp0>>
#define ZuBox_1(T) ZuBox<T, ZuBoxCmp<ZuCmp_1>>
template <auto N> struct ZuBox_CmpN {
  template <typename T> using Cmp = ZuCmpN<T, N>;
};
#define ZuBoxN(T, N) ZuBox<T, ZuBoxCmp<ZuBox_CmpN<N>::template Cmp>>

// ZuCmp has to be specialized since null() is otherwise !t (instead of !*t)
template <typename T_, typename NTP>
struct ZuCmp<ZuBox<T_, NTP>> : public ZuCmp<T_> {
  using T = ZuBox<T_, NTP>;
  static int cmp(const T &t1, const T &t2) { return t1.cmp(t2); }
  static bool less(const T &t1, const T &t2) { return t1 < t2; }
  static bool equals(const T &t1, const T &t2) { return t1 == t2; }
  static bool null(const T &t) { return !*t; }
  static const T &null() { static const T v; return v; }
};

// ZuBoxed(v) - convenience function to cast primitives to boxed
template <typename T>
ZuInline const ZuMatchBoxed<T, T> &ZuBoxed(const T &v) noexcept { return v; }
template <typename T>
ZuInline ZuMatchBoxed<T, T> &ZuBoxed(T &v) noexcept { return v; }
template <typename T>
ZuInline const ZuNotBoxed<T, ZuBox<T>> &ZuBoxed(const T &v) noexcept {
  return *ZuLaunder(reinterpret_cast<const ZuBox<T> *>(&v));
}
template <typename T>
ZuInline ZuNotBoxed<T, ZuBox<T>> &ZuBoxed(T &v) noexcept {
  return *ZuLaunder(reinterpret_cast<ZuBox<T> *>(&v));
}

// ZuBoxPtr(x) - convenience function to box pointers as uintptr_t
#define ZuBoxPtr(x) \
  (ZuBox<uintptr_t, ZuBoxCmp<ZuCmp0>>{reinterpret_cast<uintptr_t>(x)})

// ZuNBox* - same as ZuBox but null values are printed/scanned as ""
template <typename T, typename NTP = ZuBox_Defaults>
using ZuNBox = ZuBox<T, ZuBoxNullString<ZuBox_NullString(), NTP>>;
#define ZuNBox0(T) ZuNBox<T, ZuBoxCmp<ZuCmp0>>
#define ZuNBox_1(T) ZuNBox<T, ZuBoxCmp<ZuCmp_1>>
#define ZuNBoxN(T, N) ZuNBox<T, ZuBoxCmp<ZuBox_CmpN<N>::template Cmp>>

// ZuNBoxed(v) - ZuNBox equivalent of ZuBoxed
template <typename T>
ZuInline const ZuMatchBoxed<T, T> &ZuNBoxed(const T &v) noexcept { return v; }
template <typename T>
ZuInline ZuMatchBoxed<T, T> &ZuNBoxed(T &v) noexcept { return v; }
template <typename T>
ZuInline const ZuNotBoxed<T, ZuNBox<T>> &ZuNBoxed(const T &v) noexcept {
  return *ZuLaunder(reinterpret_cast<const ZuNBox<T> *>(&v));
}
template <typename T>
ZuInline ZuNotBoxed<T, ZuNBox<T>> &ZuNBoxed(T &v) noexcept {
  return *ZuLaunder(reinterpret_cast<ZuNBox<T> *>(&v));
}

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#endif /* ZuBox_HH */
