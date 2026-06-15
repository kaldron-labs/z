//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// nanosecond precision time class
// - essentially a C++ equivalent of POSIX timespec
// - used indiscriminately for intervals, relative and absolute times
// - default construction is sentinel null; use ZuTime{0} for zero
// - operator * tests for sentinel null; operator bool tests for non-zero

#ifndef ZuTime_HH
#define ZuTime_HH

#ifndef ZuLib_HH
#include <zlib/ZuLib.hh>
#endif

#include <time.h>

#include <zlib/ZuInt.hh>
#include <zlib/ZuIntrin.hh>
#include <zlib/ZuTraits.hh>
#include <zlib/ZuCmp.hh>
#include <zlib/ZuHash.hh>
#include <zlib/ZuPrint.hh>
#include <zlib/ZuBox.hh>
#include <zlib/ZuDecimal.hh>

#ifdef _WIN32
#define ZuTime_FT_Epoch	0x019db1ded53e8000ULL	// 00:00:00 Jan 1 1970
#endif

class ZuAPI ZuTime {
  using ldouble = long double;

public:
  struct Nano { int128_t v; };

  constexpr ZuTime() = default;

  constexpr ZuTime(const ZuTime &t) noexcept :
    tv_sec{t.tv_sec}, tv_nsec{t.tv_nsec} { }
  constexpr ZuTime &operator =(const ZuTime &t) noexcept {
    if (this == &t) return *this;
    tv_sec = t.tv_sec, tv_nsec = t.tv_nsec;
    return *this;
  }

  constexpr ~ZuTime() noexcept { }

  template <typename T, typename U = ZuStrip<T>>
  struct IsInt : public ZuBool<
      ZuIsSame<U, int32_t>{} ||
      ZuIsSame<U, uint32_t>{} ||
      ZuIsSame<U, int64_t>{} ||
      ZuIsSame<U, uint64_t>{} ||
      ZuIsSame<U, time_t>{}> { };
  template <typename T, typename R = void>
  using MatchInt = ZuIfT<IsInt<T>{}, R>;

  template <typename T, typename = MatchInt<T>>
  constexpr ZuTime(T v) noexcept : tv_sec{v}, tv_nsec{0} { }
  constexpr ZuTime(ldouble v) noexcept {
    if (ZuCmp<ldouble>::null(v) ||
	v >= ldouble(ZuCmp<int64_t>::maximum()) ||
	v <= ldouble(ZuCmp<int64_t>::minimum())) {
      null();
    } else {
      tv_sec = v;
      tv_nsec = (v - ldouble(tv_sec)) * 1000000000;
    }
  }
  constexpr ZuTime(const ZuDecimal &v) noexcept {
    if (!*v) {
      null();
    } else {
      tv_sec = v.floor();
      tv_nsec = v.frac() / 1000000000;
      if (ZuUnlikely(v < 0 && tv_nsec)) {
	--tv_sec;
	tv_nsec = 1000000000 - tv_nsec;
      }
    }
  }
  constexpr ZuTime(int64_t t, int32_t n) noexcept : tv_sec{t}, tv_nsec{n} { }

  constexpr ZuTime(Nano nano) noexcept :
    tv_sec{int64_t(nano.v / 1000000000)},
    tv_nsec{int32_t(nano.v % 1000000000)} { }

#ifndef _WIN32
  constexpr ZuTime(const timespec &t) noexcept :
    tv_sec{int64_t(t.tv_sec)}, tv_nsec{int32_t(t.tv_nsec)} { }
  constexpr ZuTime &operator =(const timespec &t) noexcept {
    if (ZuConstEval())
      ZuNew<ZuTime>(this, t);
    else
      new (this) ZuTime{t};
    return *this;
  }
  constexpr ZuTime(const timeval &t) noexcept :
    tv_sec{int64_t(t.tv_sec)}, tv_nsec{int32_t(t.tv_usec) * 1000} { }
  constexpr ZuTime &operator =(const timeval &t) noexcept {
    if (ZuConstEval())
      ZuNew<ZuTime>(this, t);
    else
      new (this) ZuTime{t};
    return *this;
  }
#else
  constexpr ZuTime(FILETIME f) noexcept {
    auto t = ZuPun<FILETIME, int64_t>(f).out;
    t -= ZuTime_FT_Epoch;
    tv_sec = t / 10000000;
    tv_nsec = (t % 10000000) * 100;
  }
  constexpr ZuTime &operator =(FILETIME t) noexcept {
    if (ZuConstEval())
      ZuNew<ZuTime>(this, t);
    else
      new (this) ZuTime{t};
    return *this;
  }
#endif

  template <typename S, decltype(ZuMatchString<S>(), int()) = 0>
  ZuTime(const S &s) noexcept { scan(s); }

  void null() {
    tv_sec = ZuCmp<int64_t>::null();
    tv_nsec = 0;
  }

  constexpr int64_t as_time_t() const { return(tv_sec); }
  constexpr ldouble as_fp() const {
    if (ZuUnlikely(!**this)) return ZuCmp<double>::null();
    return (ldouble(tv_sec) * 1000000000 + tv_nsec) / 1000000000;
  }
  constexpr ZuDecimal as_decimal() const {
    if (ZuUnlikely(!**this)) return {};
    return ZuDecimal{ZuDecimal::Unscaled{
      (int128_t(tv_sec) * 1000000000 + tv_nsec) * 1000000000}};
  }
#ifndef _WIN32
  timeval as_timeval() const {
    return timeval{tv_sec, tv_nsec / 1000};
  }
#else
  FILETIME as_FILETIME() const {
    return ZuPun<int64_t, FILETIME>(
      int64_t(tv_sec) * 10000000 + tv_nsec / 100 + ZuTime_FT_Epoch).out;
  }
#endif

  constexpr int64_t millisecs() const {
    return int64_t(tv_sec) * 1000 + tv_nsec / 1000000;
  }
  constexpr int64_t microsecs() const {
    return int64_t(tv_sec) * 1000000 + tv_nsec / 1000;
  }
  constexpr int128_t nanosecs() const {
    return int128_t(tv_sec) * 1000000000 + tv_nsec;
  }

  constexpr ZuTime &operator =(int64_t t) noexcept {
    tv_sec = t;
    tv_nsec = 0;
    return *this;
  }
  constexpr ZuTime &operator =(const ZuDecimal &d) noexcept {
    tv_sec = d.floor();
    tv_nsec = d.frac() / 1000000000;
    return *this;
  }

  constexpr void normalize() {
    if (tv_nsec >= 1000000000) {
      tv_nsec -= 1000000000;
      if (ZuIntrin::add(tv_sec, 1, &tv_sec)) null();
    } else if (tv_nsec < 0) {
      tv_nsec += 1000000000;
      if (ZuIntrin::sub(tv_sec, 1, &tv_sec))
	null();
      else if (ZuUnlikely(tv_nsec < 0)) {
	tv_nsec += 1000000000;
	if (ZuIntrin::sub(tv_sec, 1, &tv_sec)) null();
      }
    }
  }

  constexpr ZuTime operator -() const {
    return ZuTime{-tv_sec - 1, int32_t(1000000000) - tv_nsec};
  }

  template <typename T>
  constexpr MatchInt<T, ZuTime> operator +(T v) const {
    return ZuTime::operator +(ZuTime{v, 0});
  }
  constexpr ZuTime operator +(const ZuDecimal &d) const {
    return ZuTime::operator +(ZuTime{d});
  }
  constexpr ZuTime operator +(const ZuTime &t_) const {
    int64_t sec;
    int32_t nsec;
    if (ZuUnlikely(!**this || !*t_ ||
	ZuIntrin::add(tv_sec, t_.tv_sec, &sec) ||
	ZuIntrin::add(tv_nsec, t_.tv_nsec, &nsec)))
      return ZuTime{};
    ZuTime t{sec, nsec};
    t.normalize();
    return t;
  }
  template <typename T>
  constexpr MatchInt<T, ZuTime &> operator +=(T v) {
    return ZuTime::operator +=(ZuTime{v, 0});
  }
  constexpr ZuTime &operator +=(const ZuDecimal &d) {
    return ZuTime::operator +=(ZuTime{d});
  }
  constexpr ZuTime &operator +=(const ZuTime &t_) {
    if (ZuUnlikely(!**this || !*t_ ||
	ZuIntrin::add(tv_sec, t_.tv_sec, &tv_sec) ||
	ZuIntrin::add(tv_nsec, t_.tv_nsec, &tv_nsec)))
      null();
    else
      normalize();
    return *this;
  }
  template <typename T>
  constexpr MatchInt<T, ZuTime> operator -(T v) const {
    return ZuTime::operator -(ZuTime{v, 0});
  }
  constexpr ZuTime operator -(const ZuDecimal &d) const {
    return ZuTime::operator -(ZuTime{d});
  }
  constexpr ZuTime operator -(const ZuTime &t_) const {
    int64_t sec;
    int32_t nsec;
    if (ZuUnlikely(!**this || !*t_ ||
	ZuIntrin::sub(tv_sec, t_.tv_sec, &sec) ||
	ZuIntrin::sub(tv_nsec, t_.tv_nsec, &nsec)))
      return ZuTime{};
    ZuTime t{sec, nsec};
    t.normalize();
    return t;
  }
  template <typename T>
  constexpr MatchInt<T, ZuTime &> operator -=(T v) {
    return ZuTime::operator -=(ZuTime{v, 0});
  }
  constexpr ZuTime &operator -=(const ZuDecimal &d) {
    return ZuTime::operator -=(ZuTime{d});
  }
  constexpr ZuTime &operator -=(const ZuTime &t_) {
    if (ZuUnlikely(!**this || !*t_ ||
	ZuIntrin::sub(tv_sec, t_.tv_sec, &tv_sec) ||
	ZuIntrin::sub(tv_nsec, t_.tv_nsec, &tv_nsec)))
      null();
    else
      normalize();
    return *this;
  }

  constexpr ZuTime operator *(const ZuDecimal &d) {
    return ZuTime{as_decimal() * d};
  }
  constexpr ZuTime &operator *=(const ZuDecimal &d) {
    return operator =(as_decimal() * d);
  }
  constexpr ZuTime operator /(const ZuDecimal &d) {
    return ZuTime{as_decimal() / d};
  }
  constexpr ZuTime &operator /=(const ZuDecimal &d) {
    return operator =(as_decimal() / d);
  }

  constexpr bool equals(const ZuTime &t) const {
    return tv_sec == t.tv_sec && tv_nsec == t.tv_nsec;
  }
  constexpr int cmp(const ZuTime &t) const {
    // note that ZuCmp<int64_t>::null() is the most negative value
    if (int i = ZuCmp<int64_t>::cmp(tv_sec, t.tv_sec)) return i;
    return ZuCmp<int32_t>::cmp(tv_nsec, t.tv_nsec);
  }
  template <typename L, typename R>
  friend constexpr ZuIfT<
    bool(ZuIsSame<L, ZuTime>{}) &&
    bool(ZuIsSame<R, ZuTime>{}), bool>
  operator ==(const L &l, const R &r) { return l.equals(r); }
  template <typename L, typename R>
  friend constexpr ZuIfT<
    bool(ZuIsSame<L, ZuTime>{}) &&
    bool(ZuIsSame<R, ZuTime>{}), bool>
  operator <(const L &l, const R &r) { return l.cmp(r) < 0; }
  template <typename L, typename R>
  friend constexpr ZuIfT<
    bool(ZuIsSame<L, ZuTime>{}) &&
    bool(ZuIsSame<R, ZuTime>{}), int>
  operator <=>(const L &l, const R &r) { return l.cmp(r); }

  template <typename L, typename R>
  friend constexpr ZuIfT<
    bool(ZuIsSame<L, ZuTime>{}) &&
    !ZuIsSame<R, ZuTime>{}, bool>
  operator ==(const L &l, const R &r) { return l.equals(ZuTime{r}); }
  template <typename L, typename R>
  friend constexpr ZuIfT<
    bool(ZuIsSame<L, ZuTime>{}) &&
    !ZuIsSame<R, ZuTime>{}, bool>
  operator <(const L &l, const R &r) { return l.cmp(ZuTime{r}) < 0; }
  template <typename L, typename R>
  friend constexpr ZuIfT<
    bool(ZuIsSame<L, ZuTime>{}) &&
    !ZuIsSame<R, ZuTime>{}, int>
  operator <=>(const L &l, const R &r) { return l.cmp(ZuTime{r}); }

  constexpr bool operator *() const { return !ZuCmp<int64_t>::null(tv_sec); }
  constexpr bool operator !() const { return !tv_sec && !tv_nsec; }
  constexpr operator bool() const { return tv_sec || tv_nsec; }

  int64_t sec() const { return tv_sec; }
  int64_t &sec() { return tv_sec; }
  int32_t nsec() const { return tv_nsec; }
  int32_t &nsec() { return tv_nsec; }

  uint32_t hash() const {
    return ZuHash<int64_t>::hash(tv_sec) ^ ZuHash<int32_t>::hash(tv_nsec);
  }

  // CSV format scan/print
  unsigned scan(ZuCSpan);

  void ymdhmsn(
    int &year, int &month, int &day,
    int &hour, int &minute, int &sec, int &nsec) const
  {
    {
      int julian;
      int i, j, l, n;

      julian = static_cast<int>((tv_sec / 86400) + 2440588);
      sec = static_cast<int>(tv_sec % 86400);

      l = julian + 68569;
      n = (l<<2) / 146097;
      l = l - ((146097 * n + 3)>>2);
      i = (4000 * (l + 1)) / 1461001;
      l = l - ((1461 * i)>>2) + 31;
      j = (80 * l) / 2447;
      day = l - (2447 * j) / 80;
      l = j / 11;
      month = j + 2 - 12 * l;
      year = (100 * (n - 49) + i + l);
      hour = sec / 3600, sec %= 3600,
      minute = sec / 60, sec %= 60;
    }
    nsec = tv_nsec;
  }

  template <typename S> void print(S &s) const {
    if (ZuUnlikely(!**this)) return;
    int year, month, day, hour, minute, sec, nsec;
    ymdhmsn(year, month, day, hour, minute, sec, nsec);
    if (year < 0) { s << '-'; year = -year; }
    s <<
      ZuBoxed(year).fmt<ZuFmt::Right<4>>() << '/' <<
      ZuBoxed(month).fmt<ZuFmt::Right<2>>() << '/' <<
      ZuBoxed(day).fmt<ZuFmt::Right<2>>() << ' ' <<
      ZuBoxed(hour).fmt<ZuFmt::Right<2>>() << ':' <<
      ZuBoxed(minute).fmt<ZuFmt::Right<2>>() << ':' <<
      ZuBoxed(sec).fmt<ZuFmt::Right<2>>() << '.' <<
      ZuBoxed(nsec).fmt<ZuFmt::Frac<9, 9>>();
  }
  friend ZuPrintFn ZuPrintType(ZuTime *);

  // print as interval
  struct Interval {
    const ZuTime &time;
    template <typename S> void print(S &s) const {
      if (!time) return;
      s << ZuBoxed(time.tv_sec) << '.' <<
	ZuBoxed(time.tv_nsec).fmt<ZuFmt::Frac<9, 9>>();
    }
    friend ZuPrintFn ZuPrintType(Interval *);
  };
  Interval interval() const { return {*this}; }

  // traits
  struct Traits : public ZuBaseTraits<ZuTime> { enum { IsPOD = 1 }; };
  friend Traits ZuTraitsType(ZuTime *);

private:
  int64_t	tv_sec = ZuCmp<int64_t>::null();
  int32_t	tv_nsec = 0;
  uint32_t	_ = 0;	// pad to 16 bytes
};

#endif /* ZuTime_HH */
