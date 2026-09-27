//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Julian (serial) date based date/time class - JD(UT1) in IAU nomenclature

// * handles dates from Jan 1st 4713BC to Oct 14th 1465002AD
//   Note: 4713BC is -4712 when printed in ISO8601 format, since
//   historians do not include a 0 year (1BC immediately precedes 1AD)

// * date/times are internally represented as a Julian date with
//   intraday time in POSIX time_t seconds (i.e. UT1 seconds)
//   from midnight in the GMT timezone, and a number of nanoseconds

// * intraday timespan of each day is midnight-to-midnight instead
//   of the historical / astronomical noon-to-noon convention for
//   Julian dates

// * year/month/day conversions account for the reformation of
//   15th October 1582 and the subsequent adoption of the Gregorian
//   calendar; the default effective date is 14th September 1752
//   when Britain and the American colonies adopted the Gregorian
//   calendar by Act of Parliament (consistent with Unix cal); this
//   can be changed by calling ZuDateTime::reformation(year, month, day)

// * UTC time is composed of atomic time seconds which are of fixed
//   duration; leap seconds have been intermittently added to UTC time
//   since 1972 to correct for perturbations in the Earth's rotation;
//   ZuDateTime is based on POSIX time_t which is UT1 time, defined
//   directly in terms of the Earth's rotation and composed of
//   seconds which have slightly variable duration; all POSIX times
//   driven by typical computer clocks therefore require occasional
//   adjustment due to the difference between UTC and UT1 - see
//   http://www.boulder.nist.gov/timefreq/pubs/bulletin/leapsecond.htm -
//   this is typically done by running an NTP client on the system;
//   when performing calculations involving time values that are exchanged
//   with external systems requiring accuracy to the atomic second (such
//   as astronomical systems, etc.), the caller may need to compensate for
//   the difference between atomic time (UTC) and POSIX time_t (UT1):
//
//   * when repeatedly adding/subtracting atomic time values to advance or
//     reverse a POSIX time, it is possible to accumulate more than one
//     second of error due to the accumulated difference between UTC and
//     UT1; to compensate, the caller should add/remove a leap second when
//     the POSIX time crosses the midnight that a leap second was applied
//     (typically applied by NTP software)
//
//   * when calculating the time interval between two absolute POSIX times,
//     leap seconds that were applied to UTC for the intervening period
//     should be added/removed, to get an accurate interval

// * conversion functions accept an arbitrary intraday offset in seconds
//   for adjustment between GMT and another time zone; for local time
//   caller should use the value returned by Zt::tzOffset() which works for
//   the entire date range supported by ZuDateTime and performs DST and
//   other adjustments according to the default timezone in effect or
//   the timezone specified by the caller; timezone names are the same as
//   the values that can be assigned to the TZ environment variable:
//
//     ZuDateTime date;		// date/time
//     date = Zm::now();
//     ZuDateTimeFmt::ISO gmt;			// GMT/UTC
//     ZuDateTimeFmt::ISO local(Zt::tzOffset());// default local time (tzset())
//     ZuDateTimeFmt::ISO gb(Zt::tzOffset("GB"));// local time in UK
//     ZuDateTimeFmt::ISO est(Zt::tzOffset("EST"));// local time in New York
//
//     std::cout << date.iso(gmt);	// print time in ISO8601 format

#ifndef ZuDateTime_HH
#define ZuDateTime_HH

#ifndef ZuLib_HH
#include <zlib/ZuLib.hh>
#endif

#include <time.h>
#include <limits.h>

#include <zlib/ZuTraits.hh>
#include <zlib/ZuSpan.hh>
#include <zlib/ZuArray.hh>
#include <zlib/ZuPrint.hh>
#include <zlib/ZuBox.hh>
#include <zlib/ZuTime.hh>
#include <zlib/ZuTuple.hh>
#include <zlib/ZuUnion.hh>
#include <zlib/ZuDerive.hh>

#define ZuDateTime_MaxJulian	(0x1fffffff - 68572)

class ZuDateTime;

// compile-time date/time input formatting
namespace ZuDateTimeScan {
  struct CSV {
    int		tzOffset = 0;
  };
  struct FIX { };
  struct ISO {
    int		tzOffset = 0;
  };
  struct ASN1_U { };	// ASN.1 UTC
  struct ASN1_G { };	// ASN.1 Generalized

  ZuDeclUnion(Any,
      (CSV, csv),
      (FIX, fix),
      (ISO, iso),
      (ASN1_U, asn1_u),
      (ASN1_G, asn1_g));
}

// compile-time date/time output formatting
namespace ZuDateTimeFmt {

class CSV {
friend ::ZuDateTime;
public:
  CSV(const CSV &) = default;
  CSV &operator =(const CSV &) = default;
  CSV(CSV &&) = default;
  CSV &operator =(CSV &&) = default;

  CSV(int tzOffset = 0) : m_tzOffset{tzOffset} { reset(); }

  void tzOffset(int o) { if (m_tzOffset != o) { m_tzOffset = o; reset(); } }
  int tzOffset() const { return m_tzOffset; }

  void pad(char c) { m_pad = c; }
  char pad() const { return m_pad; }

private:
  void reset() {
    m_julian = 0;
    m_sec = 0;
    memcpy(m_yyyymmdd, "0001/01/01", 10);
    memcpy(m_hhmmss, "00:00:00", 8);
  }

  int		m_tzOffset;
  char		m_pad = 0;

  mutable int	m_julian;
  mutable int	m_sec;
  mutable char	m_yyyymmdd[10];
  mutable char	m_hhmmss[8];
};

template <unsigned NDP, char Trim> struct FIX_ {
  template <typename S> static void frac_print(S &s, unsigned nsec) {
    s << '.' << ZuBoxed(nsec).fmt<ZuFmt::Frac<9, NDP, Trim>>();
  }
};
template <unsigned NDP> struct FIX_<NDP, '\0'> {
  template <typename S> static void frac_print(S &s, unsigned nsec) {
    if (ZuLikely(nsec))
      s << '.' << ZuBoxed(nsec).fmt<ZuFmt::Frac<9, NDP, '\0'>>();
  }
};
template <char Trim> struct FIX_<0, Trim> {
  template <typename S> static void frac_print(S &, unsigned) { }
};
template <int NDP_, class Null_ = ZuPrintNull>
class FIX :
    public FIX_<(NDP_ < 0 ? -NDP_ : NDP_), (NDP_ < 0 ? '\0' : '0')> {
friend ::ZuDateTime;
public:
  enum { NDP = NDP_ };
  using Null = Null_;

  ZuAssert(NDP >= -9 && NDP <= 9);

  FIX() {
    memcpy(m_yyyymmdd, "00010101", 8);
    memcpy(m_hhmmss, "00:00:00", 8);
  }

  FIX(const FIX &) = default;
  FIX &operator =(const FIX &) = default;
  FIX(FIX &&) = default;
  FIX &operator =(FIX &&) = default;

private:
  mutable int	m_julian = 0;
  mutable int	m_sec = 0;
  mutable char	m_yyyymmdd[8];
  mutable char	m_hhmmss[8];
};

class ISO {
friend ::ZuDateTime;
public:
  ISO(int tzOffset = 0) : m_tzOffset{tzOffset} { reset(); }

  ISO(const ISO &) = default;
  ISO &operator =(const ISO &) = default;
  ISO(ISO &&) = default;
  ISO &operator =(ISO &&) = default;

  void tzOffset(int o) { if (m_tzOffset != o) { m_tzOffset = o; reset(); } }
  int tzOffset() const { return m_tzOffset; }

private:
  void reset() {
    m_julian = 0;
    m_sec = 0;
    memcpy(m_yyyymmdd, "0001-01-01", 10);
    memcpy(m_hhmmss, "00:00:00", 8);
  }

  int		m_tzOffset = 0;

  mutable int	m_julian;
  mutable int	m_sec;
  mutable char	m_yyyymmdd[10];
  mutable char	m_hhmmss[8];
};

class ASN1_U {
friend ::ZuDateTime;
public:
  ASN1_U() { reset(); }

  ASN1_U(const ASN1_U &) = default;
  ASN1_U &operator =(const ASN1_U &) = default;
  ASN1_U(ASN1_U &&) = default;
  ASN1_U &operator =(ASN1_U &&) = default;

private:
  void reset() {
    m_julian = 0;
    m_sec = 0;
    memcpy(m_yymmdd, "010101", 6);
    memcpy(m_hhmmss, "000000", 6);
  }

  mutable int	m_julian;
  mutable int	m_sec;
  mutable char	m_yymmdd[6];
  mutable char	m_hhmmss[6];
};

class ASN1_G {
friend ::ZuDateTime;
public:
  ASN1_G() { reset(); }

  ASN1_G(const ASN1_G &) = default;
  ASN1_G &operator =(const ASN1_G &) = default;
  ASN1_G(ASN1_G &&) = default;
  ASN1_G &operator =(ASN1_G &&) = default;

private:
  void reset() {
    m_julian = 0;
    m_sec = 0;
    memcpy(m_yyyymmdd, "00010101", 8);
    memcpy(m_hhmmss, "000000", 6);
  }

  mutable int	m_julian;
  mutable int	m_sec;
  mutable char	m_yyyymmdd[8];
  mutable char	m_hhmmss[6];
};

struct Strftime {
  const char	*format;
  int		tzOffset;
};

// run-time variable date/time formatting
struct FIXDeflt_Null : public ZuPrintable {
  template <typename S> void print(S &) const { }
};
using FIXDeflt = FIX<-9, FIXDeflt_Null>;
ZuDeclUnion(Any,
    (CSV, csv),
    (FIXDeflt, fix),
    (ISO, iso),
    (ASN1_U, asn1_u),
    (ASN1_G, asn1_g),
    (Strftime, strftime));

} // ZuDateTimeFmt

struct ZuDateTimePrintCSV {
  const ZuDateTime			&value;
  const ZuDateTimeFmt::CSV		&fmt;

  template <typename S> void print(S &) const;
  friend ZuPrintFn ZuPrintType(ZuDateTimePrintCSV *);
};
template <int NDP = -9, class Null = ZuDateTimeFmt::FIXDeflt_Null>
struct ZuDateTimePrintFIX {
  const ZuDateTime			&value;
  const ZuDateTimeFmt::FIX<NDP, Null>	&fmt;

  template <typename S> void print(S &) const;
  friend ZuPrintFn ZuPrintType(ZuDateTimePrintFIX *);
};
struct ZuDateTimePrintISO {
  const ZuDateTime			&value;
  const ZuDateTimeFmt::ISO		&fmt;

  template <typename S> void print(S &) const;
  friend ZuPrintFn ZuPrintType(ZuDateTimePrintISO *);
};
struct ZuDateTimePrintASN1_U {
  const ZuDateTime			&value;
  const ZuDateTimeFmt::ASN1_U		&fmt;

  template <typename S> void print(S &) const;
  friend ZuPrintFn ZuPrintType(ZuDateTimePrintASN1_U *);
};
struct ZuDateTimePrintASN1_G {
  const ZuDateTime			&value;
  const ZuDateTimeFmt::ASN1_G		&fmt;

  template <typename S> void print(S &) const;
  friend ZuPrintFn ZuPrintType(ZuDateTimePrintASN1_G *);
};
struct ZuDateTimePrintStrftime {
  const ZuDateTime			&value;
  ZuDateTimeFmt::Strftime		fmt;

  template <typename Boxed>
  static auto vfmt(ZuVFmt &fmt,
      const Boxed &boxed, unsigned width, bool alt) {
    if (alt)
      fmt.reset();
    else
      fmt.right(width);
    return boxed.vfmt(fmt);
  }
  template <typename Boxed>
  static auto vfmt(ZuVFmt &fmt,
      const Boxed &boxed, ZuBox<unsigned> width, int deflt, bool alt) {
    if (alt)
      fmt.reset();
    else {
      if (!*width) width = deflt;
      fmt.right(width);
    }
    return boxed.vfmt(fmt);
  }
  template <typename Boxed>
  static auto vfmt(ZuVFmt &fmt,
      const Boxed &boxed, ZuBox<unsigned> width, int deflt, bool alt,
      char pad) {
    if (alt)
      fmt.reset();
    else {
      if (!*width) width = deflt;
      fmt.right(width, pad);
    }
    return boxed.vfmt(fmt);
  }

  template <typename S> void print(S &) const;
  friend ZuPrintFn ZuPrintType(ZuDateTimePrintStrftime *);
};

struct ZuDateTimePrint {
  const ZuDateTime			&value;
  const ZuDateTimeFmt::Any		&fmt;

  template <typename S> void print(S &) const;
  friend ZuPrintFn ZuPrintType(ZuDateTimePrint *);
};

class ZuAPI ZuDateTime {
public:
  struct Julian { int32_t v; };

  constexpr ZuDateTime() noexcept = default;

  constexpr ZuDateTime(const ZuDateTime &date) noexcept :
    m_julian{date.m_julian}, m_sec{date.m_sec}, m_nsec{date.m_nsec} { }

  constexpr ZuDateTime &operator =(const ZuDateTime &date) noexcept {
    if (ZuUnlikely(this == &date)) return *this;
    m_julian = date.m_julian;
    m_sec = date.m_sec;
    m_nsec = date.m_nsec;
    return *this;
  }

  void update(const ZuDateTime &date) {
    if (ZuUnlikely(this == &date) || !*date) return;
    m_julian = date.m_julian;
    m_sec = date.m_sec;
    m_nsec = date.m_nsec;
  }

  // ZuTime

  template <typename T, ZuSame<ZuTime, T, int> = 0>
  constexpr ZuDateTime(const T &v) noexcept {
    if (!*v) return;
    init(v.sec()), m_nsec = v.nsec();
  }
  template <typename T, typename = ZuSame<ZuTime, T>>
  constexpr ZuDateTime & operator =(const T &v) noexcept {
    if (!*v) { null(); return *this; }
    init(v.sec());
    m_nsec = v.nsec();
    return *this;
  }

  // integral / time_t

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
  constexpr ZuDateTime(T v) noexcept { init(v); m_nsec = 0; }
  template <typename T, typename = MatchInt<T>>
  constexpr ZuDateTime & operator =(T v) noexcept {
    init(v);
    m_nsec = 0;
    return *this;
  }

  // struct tm

  constexpr ZuDateTime(const struct tm *tm_) noexcept {
    int year = tm_->tm_year + 1900, month = tm_->tm_mon + 1, day = tm_->tm_mday;
    int hour = tm_->tm_hour, minute = tm_->tm_min, sec = tm_->tm_sec;
    int nsec = 0;
    normalize(year, month);
    normalize(day, hour, minute, sec, nsec);
    ctor(year, month, day, hour, minute, sec, nsec);
  }
  ZuDateTime &operator =(const struct tm *tm_) noexcept {
    // this->~ZuDateTime();
    new (this) ZuDateTime(tm_);
    return *this;
  }

  // multiple parameter constructors

  template <typename T, typename = MatchInt<T>>
  constexpr ZuDateTime(T v, int nsec) noexcept { init(v); m_nsec = nsec; }

  constexpr explicit ZuDateTime(Julian julian, int sec, int nsec) noexcept :
    m_julian(julian.v), m_sec(sec), m_nsec(nsec) { }

  constexpr ZuDateTime(int year, int month, int day) noexcept {
    normalize(year, month);
    ctor(year, month, day);
  }
  constexpr ZuDateTime(
    int year, int month, int day, int hour, int minute, int sec) noexcept
  {
    normalize(year, month);
    int nsec = 0;
    normalize(day, hour, minute, sec, nsec);
    ctor(year, month, day, hour, minute, sec, nsec);
  }
  constexpr ZuDateTime(
    int year, int month, int day,
    int hour, int minute, int sec, int nsec) noexcept
  {
    ctor(year, month, day, hour, minute, sec, nsec);
  }

  constexpr ZuDateTime(struct tm *tm_) noexcept {
    int year = tm_->tm_year + 1900, month = tm_->tm_mon + 1, day = tm_->tm_mday;
    int hour = tm_->tm_hour, minute = tm_->tm_min, sec = tm_->tm_sec;
    int nsec = 0;

    normalize(year, month);
    normalize(day, hour, minute, sec, nsec);
  }

  // CSV format: YYYY/MM/DD HH:MM:SS with an optional timezone parameter
  template <typename S, ZuMatchString<S, int> = 0>
  ZuDateTime(const ZuDateTimeScan::CSV &fmt, const S &s) noexcept {
    scan(fmt, s);
  }

  // FIX format: YYYYMMDD-HH:MM:SS.nnnnnnnnn
  template <typename S, ZuMatchString<S, int> = 0>
  ZuDateTime(const ZuDateTimeScan::FIX &fmt, const S &s) noexcept {
    scan(fmt, s);
  }

  // the ISO8601 ctor accepts the two standard ISO8601 date/time formats
  // "yyyy-mm-dd" and "yyyy-mm-ddThh:mm:ss[.n]Z", where Z is an optional
  // timezone: "Z" (GMT), "+hhmm", "+hh:mm", "-hhmm", or "-hh:mm"
  template <typename S, ZuMatchString<S, int> = 0>
  ZuDateTime(const ZuDateTimeScan::ISO &fmt, const S &s) noexcept {
    scan(fmt, s);
  }

  // ASN.1 UTC format
  template <typename S, ZuMatchString<S, int> = 0>
  ZuDateTime(const ZuDateTimeScan::ASN1_U &fmt, const S &s) noexcept {
    scan(fmt, s);
  }

  // ASN.1 Generalized format
  template <typename S, ZuMatchString<S, int> = 0>
  ZuDateTime(const ZuDateTimeScan::ASN1_G &fmt, const S &s) noexcept {
    scan(fmt, s);
  }

  // default to ISO
  template <typename S, ZuMatchString<S, int> = 0>
  ZuDateTime(const S &s) noexcept {
    scan(ZuDateTimeScan::ISO{}, s);
  }

  template <typename S, ZuMatchString<S, int> = 0>
  ZuDateTime(const ZuDateTimeScan::Any &fmt, const S &s) noexcept {
    scan(fmt, s);
  }

  // common integral forms

  struct YYYYMMDD { unsigned v; };
  struct YYMMDD { unsigned v; };
  struct HHMMSSmmm { unsigned v; };
  struct HHMMSS { unsigned v; };

  constexpr ZuDateTime(int year, int month, int day, HHMMSSmmm time) noexcept {
    int hour, minute, sec, nsec;
    hour = time.v / 10000000; minute = (time.v / 100000) % 100;
    sec = (time.v / 1000) % 100; nsec = (time.v % 1000) * 1000000;
    normalize(year, month);
    normalize(day, hour, minute, sec, nsec);
    ctor(year, month, day, hour, minute, sec, nsec);
  }
  constexpr ZuDateTime(int year, int month, int day, HHMMSS time) noexcept {
    int hour, minute, sec, nsec = 0;
    hour = time.v / 10000; minute = (time.v / 100) % 100; sec = time.v % 100;
    normalize(year, month);
    normalize(day, hour, minute, sec, nsec);
    ctor(year, month, day, hour, minute, sec, nsec);
  }
  constexpr ZuDateTime(YYYYMMDD date, HHMMSSmmm time) noexcept {
    int year, month, day, hour, minute, sec, nsec;
    year = date.v / 10000; month = (date.v / 100) % 100; day = date.v % 100;
    hour = time.v / 10000000; minute = (time.v / 100000) % 100;
    sec = (time.v / 1000) % 100; nsec = (time.v % 1000) * 1000000;
    normalize(year, month);
    normalize(day, hour, minute, sec, nsec);
    ctor(year, month, day, hour, minute, sec, nsec);
  }
  constexpr ZuDateTime(YYYYMMDD date, HHMMSS time) noexcept {
    int year, month, day, hour, minute, sec, nsec = 0;
    year = date.v / 10000; month = (date.v / 100) % 100; day = date.v % 100;
    hour = time.v / 10000; minute = (time.v / 100) % 100; sec = time.v % 100;
    normalize(year, month);
    normalize(day, hour, minute, sec, nsec);
    ctor(year, month, day, hour, minute, sec, nsec);
  }
  constexpr ZuDateTime(YYMMDD date, HHMMSSmmm time) noexcept {
    int year, month, day, hour, minute, sec, nsec;
    year = date.v / 10000; month = (date.v / 100) % 100; day = date.v % 100;
    hour = time.v / 10000000; minute = (time.v / 100000) % 100;
    sec = (time.v / 1000) % 100; nsec = (time.v % 1000) * 1000000;
    year += (year < 70) ? 2000 : 1900;
    normalize(year, month);
    normalize(day, hour, minute, sec, nsec);
    ctor(year, month, day, hour, minute, sec, nsec);
  }
  constexpr ZuDateTime(YYMMDD date, HHMMSS time) noexcept {
    int year, month, day, hour, minute, sec, nsec = 0;
    year = date.v / 10000; month = (date.v / 100) % 100; day = date.v % 100;
    hour = time.v / 10000; minute = (time.v / 100) % 100; sec = time.v % 100;
    year += (year < 70) ? 2000 : 1900;
    normalize(year, month);
    normalize(day, hour, minute, sec, nsec);
    ctor(year, month, day, hour, minute, sec, nsec);
  }

  // these functions return -1 if null
  constexpr int yyyymmdd() const {
    if (ZuUnlikely(!**this)) return -1;
    int year, month, day;
    ymd(year, month, day);
    year *= 10000;
    if (year >= 0) return year + month * 100 + day;
    return year - month * 100 - day;
  }
  constexpr int yymmdd() const {
    if (ZuUnlikely(!**this)) return -1;
    int year, month, day;
    ymd(year, month, day);
    year = (year % 100) * 10000;
    if (year >= 0) return year + month * 100 + day;
    return year - month * 100 - day;
  }
  constexpr int hhmmssmmm() const {
    if (ZuUnlikely(!**this)) return -1;
    int hour, minute, sec, nsec;
    hmsn(hour, minute, sec, nsec);
    return hour * 10000000 + minute * 100000 + sec * 1000 + nsec / 1000000;
  }
  constexpr int hhmmss() const {
    if (ZuUnlikely(!**this)) return -1;
    int hour, minute, sec;
    hms(hour, minute, sec);
    return hour * 10000 + minute * 100 + sec;
  }

// accessors for external persistency providers

  const int &julian() const { return m_julian; }
  int &julian() { return m_julian; }
  const int &sec() const { return m_sec; }
  int &sec() { return m_sec; }
  const int &nsec() const { return m_nsec; }
  int &nsec() { return m_nsec; }

// set effective date of reformation (default 14th September 1752)

  // reformation() is purposely not MT safe since MT safety is an
  // unlikely requirement for a function only intended to be called
  // once during program initialization, MT safety would degrade performance
  // considerably by lock acquisition on every conversion

  static void reformation(int year, int month, int day);

// conversions

  // as_time_t() and as_time() can result in out of range conversion to null

  constexpr int64_t as_time_t() const {
    int64_t v;
    if (ZuUnlikely(!**this ||
	ZuIntrin::sub(int64_t(m_julian), 2440588, &v) ||
	ZuIntrin::mul(v, 86400, &v) ||
	ZuIntrin::add(v, m_sec, &v)))
      return ZuCmp<int64_t>::null();
    return v;
  }
  constexpr ZuTime as_time() const {
    if (ZuUnlikely(!**this)) return {};
    return ZuTime{this->as_time_t(), m_nsec};
  }

  struct tm *tm(struct tm *tm) const;

  void ymd(int &year, int &month, int &day) const;
  void hms(int &hour, int &minute, int &sec) const;
  void hmsn(int &hour, int &minute, int &sec, int &nsec) const;

  // number of days relative to a base date
  int days(int year, int month, int day) const {
    return m_julian - julian(year, month, day);
  }
  // days arg below should be the value of days(year, 1, 1)
  // week (0-53) wkDay (1-7) 1st Monday in year is 1st day of week 1
  void ywd(int days, int &week, int &wkDay) const;
  // week (0-53) wkDay (1-7) 1st Sunday in year is 1st day of week 1
  void ywdSun(int days, int &week, int &wkDay) const;
  // week (1-53) wkDay (1-7) 1st Thursday in year is 4th day of week 1
  void ywdISO(int year, int days, int &wkYear, int &week, int &wkDay) const;

  static ZuCSpan dayShortName(int i); // 1-7
  static ZuCSpan dayLongName(int i); // 1-7
  static ZuCSpan monthShortName(int i); // 1-12
  static ZuCSpan monthLongName(int i); // 1-12

  auto fmt(const ZuDateTimeFmt::CSV &fmt) const {
    return ZuDateTimePrintCSV{*this, fmt};
  }
  template <typename S_>
  void csv_print(S_ &s, const ZuDateTimeFmt::CSV &fmt) const {
    if (ZuUnlikely(!**this)) return;
    ZuDateTime date = *this + fmt.m_tzOffset;
    if (ZuUnlikely(date.m_julian != fmt.m_julian)) {
      fmt.m_julian = date.m_julian;
      int y, m, d;
      date.ymd(y, m, d);
      if (ZuUnlikely(y < -9998 || y > 9999)) return;
      if (ZuUnlikely(y < 1)) { s << '-'; y = 1 - y; }
      fmt.m_yyyymmdd[0] = y / 1000 + '0';
      fmt.m_yyyymmdd[1] = (y / 100) % 10 + '0';
      fmt.m_yyyymmdd[2] = (y / 10) % 10 + '0';
      fmt.m_yyyymmdd[3] = y % 10 + '0';
      fmt.m_yyyymmdd[5] = m / 10 + '0';
      fmt.m_yyyymmdd[6] = m % 10 + '0';
      fmt.m_yyyymmdd[8] = d / 10 + '0';
      fmt.m_yyyymmdd[9] = d % 10 + '0';
    }
    s << ZuCSpan(fmt.m_yyyymmdd, 10) << ' ';
    if (ZuUnlikely(date.m_sec != fmt.m_sec)) {
      fmt.m_sec = date.m_sec;
      int H, M, S;
      date.hms(H, M, S);
      fmt.m_hhmmss[0] = H / 10 + '0';
      fmt.m_hhmmss[1] = H % 10 + '0';
      fmt.m_hhmmss[3] = M / 10 + '0';
      fmt.m_hhmmss[4] = M % 10 + '0';
      fmt.m_hhmmss[6] = S / 10 + '0';
      fmt.m_hhmmss[7] = S % 10 + '0';
    }
    s << ZuCSpan(fmt.m_hhmmss, 8);
    if (unsigned N = date.m_nsec) {
      char buf[9];
      if (fmt.m_pad) {
	Zu_ntoa::Base10_print_frac(N, 9, 9, fmt.m_pad, buf);
	s << '.' << ZuCSpan(buf, 9);
      } else {
	N = Zu_ntoa::Base10_print_frac_truncate(N, 9, 9, buf);
	if (N > 1 || buf[0] != '0')
	  s << '.' << ZuCSpan(buf, N);
      }
    }
  }

  template <int NDP, class Null>
  auto fmt(const ZuDateTimeFmt::FIX<NDP, Null> &fmt) const {
    return ZuDateTimePrintFIX<NDP, Null>{*this, fmt};
  }
  template <typename S_, int NDP, class Null>
  void fix_print(S_ &s, const ZuDateTimeFmt::FIX<NDP, Null> &fmt) const {
    if (ZuUnlikely(!**this)) { s << Null{}; return; }
    if (ZuUnlikely(m_julian != fmt.m_julian)) {
      fmt.m_julian = m_julian;
      int y, m, d;
      ymd(y, m, d);
      if (ZuUnlikely(y < 1 || y > 9999)) { s << Null{}; return; }
      fmt.m_yyyymmdd[0] = y / 1000 + '0';
      fmt.m_yyyymmdd[1] = (y / 100) % 10 + '0';
      fmt.m_yyyymmdd[2] = (y / 10) % 10 + '0';
      fmt.m_yyyymmdd[3] = y % 10 + '0';
      fmt.m_yyyymmdd[4] = m / 10 + '0';
      fmt.m_yyyymmdd[5] = m % 10 + '0';
      fmt.m_yyyymmdd[6] = d / 10 + '0';
      fmt.m_yyyymmdd[7] = d % 10 + '0';
    }
    s << ZuCSpan(fmt.m_yyyymmdd, 8) << '-';
    if (ZuUnlikely(m_sec != fmt.m_sec)) {
      fmt.m_sec = m_sec;
      int H, M, S;
      hms(H, M, S);
      fmt.m_hhmmss[0] = H / 10 + '0';
      fmt.m_hhmmss[1] = H % 10 + '0';
      fmt.m_hhmmss[3] = M / 10 + '0';
      fmt.m_hhmmss[4] = M % 10 + '0';
      fmt.m_hhmmss[6] = S / 10 + '0';
      fmt.m_hhmmss[7] = S % 10 + '0';
    }
    s << ZuCSpan(fmt.m_hhmmss, 8);
    fmt.frac_print(s, m_nsec);
  }

  // iso_print() always generates a full date/time format parsable by the ctor
  auto fmt(const ZuDateTimeFmt::ISO &fmt) const {
    return ZuDateTimePrintISO{*this, fmt};
  }
  template <typename S_>
  void iso_print(S_ &s, const ZuDateTimeFmt::ISO &fmt) const {
    if (ZuUnlikely(!**this)) return;
    ZuDateTime date = *this + fmt.m_tzOffset;
    if (ZuUnlikely(date.m_julian != fmt.m_julian)) {
      fmt.m_julian = date.m_julian;
      int y, m, d;
      date.ymd(y, m, d);
      if (ZuUnlikely(y < -9998 || y > 9999)) return;
      if (ZuUnlikely(y < 1)) { s << '-'; y = 1 - y; }
      fmt.m_yyyymmdd[0] = y / 1000 + '0';
      fmt.m_yyyymmdd[1] = (y / 100) % 10 + '0';
      fmt.m_yyyymmdd[2] = (y / 10) % 10 + '0';
      fmt.m_yyyymmdd[3] = y % 10 + '0';
      fmt.m_yyyymmdd[5] = m / 10 + '0';
      fmt.m_yyyymmdd[6] = m % 10 + '0';
      fmt.m_yyyymmdd[8] = d / 10 + '0';
      fmt.m_yyyymmdd[9] = d % 10 + '0';
    }
    s << ZuCSpan(fmt.m_yyyymmdd, 10) << 'T';
    if (ZuUnlikely(date.m_sec != fmt.m_sec)) {
      fmt.m_sec = date.m_sec;
      int H, M, S;
      date.hms(H, M, S);
      fmt.m_hhmmss[0] = H / 10 + '0';
      fmt.m_hhmmss[1] = H % 10 + '0';
      fmt.m_hhmmss[3] = M / 10 + '0';
      fmt.m_hhmmss[4] = M % 10 + '0';
      fmt.m_hhmmss[6] = S / 10 + '0';
      fmt.m_hhmmss[7] = S % 10 + '0';
    }
    s << ZuCSpan(fmt.m_hhmmss, 8);
    if (unsigned N = date.m_nsec) {
      char buf[9];
      N = Zu_ntoa::Base10_print_frac_truncate(N, 9, 9, buf);
      if (N > 1 || buf[0] != '0')
	s << '.' << ZuCSpan(buf, N);
    }
    if (fmt.m_tzOffset) {
      int offset_ = (fmt.m_tzOffset < 0) ? -fmt.m_tzOffset : fmt.m_tzOffset;
      int oH = offset_ / 3600, oM = (offset_ % 3600) / 60;
      char buf[5];
      buf[0] = oH / 10 + '0';
      buf[1] = oH % 10 + '0';
      buf[2] = ':';
      buf[3] = oM / 10 + '0';
      buf[4] = oM % 10 + '0';
      s << ((fmt.m_tzOffset < 0) ? '-' : '+') << ZuCSpan(buf, 5);
    } else
      s << 'Z';
  }

  auto fmt(const ZuDateTimeFmt::ASN1_U &fmt) const {
    return ZuDateTimePrintASN1_U{*this, fmt};
  }
  template <typename S_>
  void asn1_u_print(S_ &s, const ZuDateTimeFmt::ASN1_U &fmt) const {
    if (ZuUnlikely(!**this)) return;
    if (ZuUnlikely(m_julian != fmt.m_julian)) {
      fmt.m_julian = m_julian;
      int y, m, d;
      ymd(y, m, d);
      if (ZuUnlikely(y < 1950 || y >= 2050)) return;
      if (ZuUnlikely((y -= 2000) < 0)) y += 100;
      fmt.m_yymmdd[0] = y / 10 + '0';
      fmt.m_yymmdd[1] = y % 10 + '0';
      fmt.m_yymmdd[2] = m / 10 + '0';
      fmt.m_yymmdd[3] = m % 10 + '0';
      fmt.m_yymmdd[4] = d / 10 + '0';
      fmt.m_yymmdd[5] = d % 10 + '0';
    }
    s << ZuCSpan(fmt.m_yymmdd, 6);
    if (ZuUnlikely(m_sec != fmt.m_sec)) {
      fmt.m_sec = m_sec;
      int H, M, S;
      hms(H, M, S);
      fmt.m_hhmmss[0] = H / 10 + '0';
      fmt.m_hhmmss[1] = H % 10 + '0';
      fmt.m_hhmmss[2] = M / 10 + '0';
      fmt.m_hhmmss[3] = M % 10 + '0';
      fmt.m_hhmmss[4] = S / 10 + '0';
      fmt.m_hhmmss[5] = S % 10 + '0';
    }
    s << ZuCSpan(fmt.m_hhmmss, 6);
    if (unsigned N = m_nsec) {
      char buf[9];
      N = Zu_ntoa::Base10_print_frac_truncate(N, 9, 9, buf);
      if (N > 1 || buf[0] != '0')
	s << '.' << ZuCSpan(buf, N) << 'Z';
      else
	s << 'Z';
    } else {
      s << 'Z';
    }
  }

  auto fmt(const ZuDateTimeFmt::ASN1_G &fmt) const {
    return ZuDateTimePrintASN1_G{*this, fmt};
  }
  template <typename S_>
  void asn1_g_print(S_ &s, const ZuDateTimeFmt::ASN1_G &fmt) const {
    if (ZuUnlikely(!**this)) return;
    if (ZuUnlikely(m_julian != fmt.m_julian)) {
      fmt.m_julian = m_julian;
      int y, m, d;
      ymd(y, m, d);
      if (ZuUnlikely(y < 1 || y > 9999)) return;
      fmt.m_yyyymmdd[0] = y / 1000 + '0';
      fmt.m_yyyymmdd[1] = (y / 100) % 10 + '0';
      fmt.m_yyyymmdd[2] = (y / 10) % 10 + '0';
      fmt.m_yyyymmdd[3] = y % 10 + '0';
      fmt.m_yyyymmdd[4] = m / 10 + '0';
      fmt.m_yyyymmdd[5] = m % 10 + '0';
      fmt.m_yyyymmdd[6] = d / 10 + '0';
      fmt.m_yyyymmdd[7] = d % 10 + '0';
    }
    s << ZuCSpan(fmt.m_yyyymmdd, 8);
    if (ZuUnlikely(m_sec != fmt.m_sec)) {
      fmt.m_sec = m_sec;
      int H, M, S;
      hms(H, M, S);
      fmt.m_hhmmss[0] = H / 10 + '0';
      fmt.m_hhmmss[1] = H % 10 + '0';
      fmt.m_hhmmss[2] = M / 10 + '0';
      fmt.m_hhmmss[3] = M % 10 + '0';
      fmt.m_hhmmss[4] = S / 10 + '0';
      fmt.m_hhmmss[5] = S % 10 + '0';
    }
    s << ZuCSpan(fmt.m_hhmmss, 6);
    if (unsigned N = m_nsec) {
      char buf[9];
      N = Zu_ntoa::Base10_print_frac_truncate(N, 9, 9, buf);
      if (N > 1 || buf[0] != '0')
	s << '.' << ZuCSpan(buf, N) << 'Z';
      else
	s << 'Z';
    } else {
      s << 'Z';
    }
  }

  // this strftime(3) is neither system timezone- nor locale- dependent;
  // timezone is specified by the (optional) tzOffset parameter, output is
  // equivalent to the C library strftime under the 'C' locale; it does
  // not call tzset() and is thread-safe; conforms to, variously:
  //   (C90) - ANSI C '90
  //   (C99) - ANSI C '99
  //   (SU) - Single Unix Specification
  //   (MS) - Microsoft CRT
  //   (GNU) - glibc (not all glibc-specific extensions are supported)
  //   (TZ) - Arthur Olson's timezone library
  // the following conversion specifiers are supported:
  //   %# (MS) alt. format
  //   %E (SU) alt. format
  //   %O (SU) alt. digits (has no effect)
  //   %0-9 (GNU) field width (precision) specifier
  //   %a (C90) day of week - short name
  //   %A (C90) day of week - long name
  //   %b (C90) month - short name
  //   %h (SU) ''
  //   %B (C90) month - long name
  //   %c (C90) Unix asctime() / ctime() (%a %b %e %T %Y)
  //   %C (SU) century
  //   %d (C90) day of month
  //   %x (C90) %m/%d/%y
  //   %D (SU) ''
  //   %e (SU) day of month - space padded
  //   %F (C99) %Y-%m-%d per ISO 8601
  //   %g (TZ) ISO week date year (2 digits)
  //   %G (TZ) '' (4 digits)
  //   %H (C90) hour (24hr)
  //   %I (C90) hour (12hr)
  //   %j (C90) day of year
  //   %m (C90) month
  //   %M (C90) minute
  //   %n (SU) newline
  //   %p (C90) AM/PM
  //   %P (GNU) am/pm
  //   %r (SU) %I:%M:%S %p
  //   %R (SU) %H:%M
  //   %s (TZ) number of seconds since the Epoch
  //   %S (C90) second
  //   %t (SU) TAB
  //   %X (C90) %H:%M:%S
  //   %T (SU) ''
  //   %u (SU) week day as decimal (1-7), 1 is Monday (7 is Sunday)
  //   %U (C90) week (00-53), 1st Sunday in year is 1st day of week 1
  //   %V (SU) week (01-53), per ISO week date
  //   %w (C90) week day as decimal (0-6), 0 is Sunday
  //   %W (C90) week (00-53), 1st Monday in year is 1st day of week 1
  //   %y (C90) year (2 digits)
  //   %Y (C90) year (4 digits)
  //   %z (GNU) RFC 822 timezone offset
  //   %Z (C90) timezone
  //   %% (C90) percent sign
  auto strftime(const char *format, int tzOffset = 0) const {
    return ZuDateTimePrintStrftime{
      *this, ZuDateTimeFmt::Strftime{format, tzOffset}};
  }

  // run-time variable date/time formatting
  auto fmt(const ZuDateTimeFmt::Any &fmt) const {
    return ZuDateTimePrint{*this, fmt};
  }

// operators

  void null() {
    m_julian = ZuCmp<int32_t>::null();
    m_sec = 0;
  }

  // ZuDateTime is an absolute time, not a time interval, so the difference
  // between two ZuDateTimes is a ZuTime;
  // for the same reason no operator +(ZuDateTime) is defined

  ZuTime operator -(const ZuDateTime &date) const {
    int64_t day = int64_t(m_julian) - date.m_julian;
    int64_t sec = int64_t(m_sec) - date.m_sec;
    int64_t nsec = int64_t(m_nsec) - date.m_nsec;

    if (nsec < 0) nsec += 1000000000, --sec;
    if (sec < 0) sec += 86400, --day;

    if (ZuUnlikely(ZuIntrin::mul(day, 86400, &day) ||
	ZuIntrin::add(day, sec, &sec))) return {};

    return ZuTime{sec, int32_t(nsec)};
  }

  template <typename T, typename = ZuSame<ZuTime, T>>
  ZuDateTime operator +(const T &t) const {
    int64_t julian, sec, nsec;

    sec = m_sec;
    nsec = int64_t(m_nsec) + t.nsec();
    if (nsec < 0)
      nsec += 1000000000, --sec;
    else if (nsec >= 1000000000)
      nsec -= 1000000000, ++sec;

    {
      int64_t sec_ = t.sec();

      if (sec_ < 0) {
        sec_ = -sec_;
	julian = int64_t(m_julian) - sec_ / 86400;
	sec -= sec_ % 86400;

	if (sec < 0) sec += 86400, --julian;
      } else {
	julian = int64_t(m_julian) + sec_ / 86400;
	sec += sec_ % 86400;

	if (sec >= 86400) sec -= 86400, ++julian;
      }
    }

    if (julian != int32_t(julian)) return {}; // overflow

    return ZuDateTime{Julian{int32_t(julian)}, int32_t(sec), int32_t(nsec)};
  }
  template <typename T, typename = MatchInt<T>>
  ZuDateTime operator +(T sec_) const {
    int64_t julian, sec = sec_;

    if (sec < 0) {
      sec = -sec;
      if (ZuLikely(sec < 86400))
	julian = m_julian, sec = int64_t(m_sec) - sec;
      else
	julian = int64_t(m_julian) - sec / 86400,
	sec = int64_t(m_sec) - sec % 86400;

      if (sec < 0) sec += 86400, --julian;
    } else {
      if (ZuLikely(sec < 86400))
	julian = m_julian, sec = int64_t(m_sec) + sec;
      else
	julian = int64_t(m_julian) + sec / 86400,
	sec = int64_t(m_sec) + sec % 86400;

      if (sec >= 86400) sec -= 86400, ++julian;
    }

    if (julian != int32_t(julian)) return {}; // overflow

    return ZuDateTime{Julian{int32_t(julian)}, int32_t(sec), m_nsec};
  }

  template <typename T, typename = ZuSame<ZuTime, T>>
  ZuDateTime & operator +=(const T &t) {
    int64_t julian, sec, nsec;

    sec = m_sec;
    nsec = int64_t(m_nsec) + t.nsec();
    if (nsec < 0)
      nsec += 1000000000, --sec;
    else if (nsec >= 1000000000)
      nsec -= 1000000000, ++sec;

    {
      int64_t sec_ = t.sec();

      if (sec_ < 0) {
        sec_ = -sec_;
	julian = int64_t(m_julian) - sec_ / 86400;
	sec -= sec_ % 86400;

	if (sec < 0) sec += 86400, --julian;
      } else {
	julian = int64_t(m_julian) + sec_ / 86400;
	sec += sec_ % 86400;

	if (sec >= 86400) sec -= 86400, ++julian;
      }
    }

    if (julian != int32_t(julian)) { null(); return *this; } // overflow

    m_julian = julian;
    m_sec = sec;
    m_nsec = nsec;

    return *this;
  }
  template <typename T, typename = MatchInt<T>>
  ZuDateTime & operator +=(T sec_) {
    int64_t julian, sec = sec_;

    if (sec < 0) {
      sec = -sec;
      if (ZuLikely(sec < 86400))
	julian = int64_t(m_julian), sec = int64_t(m_sec) - sec;
      else
	julian = int64_t(m_julian) - sec / 86400,
	sec = int64_t(m_sec) - sec % 86400;

      if (sec < 0) sec += 86400, --julian;
    } else {
      if (ZuLikely(sec < 86400))
	julian = int64_t(m_julian), sec = int64_t(m_sec) + sec;
      else
	julian = int64_t(m_julian) + sec / 86400,
        sec = int64_t(m_sec) + sec % 86400;

      if (sec >= 86400) sec -= 86400, ++julian;
    }

    if (julian != int32_t(julian)) { null(); return *this; } // overflow

    m_julian = julian;
    m_sec = sec;

    return *this;
  }

  template <typename T, typename = ZuSame<ZuTime, T>>
  ZuDateTime operator -(const T &t) const {
    return ZuDateTime::operator +(-t);
  }
  template <typename T, typename = MatchInt<T>>
  ZuDateTime operator -(T sec_) const {
    return ZuDateTime::operator +(-sec_);
  }
  template <typename T, typename = ZuSame<ZuTime, T>>
  ZuDateTime & operator -=(const T &t) {
    return ZuDateTime::operator +=(-t);
  }
  template <typename T, typename = MatchInt<T>>
  ZuDateTime & operator -=(T sec_) {
    return ZuDateTime::operator +=(-sec_);
  }

  bool equals(const ZuDateTime &v) const {
    return m_julian == v.m_julian && m_sec == v.m_sec && m_nsec == v.m_nsec;
  }
  int cmp(const ZuDateTime &v) const {
    // note that ZuCmp<int32_t>::null() is the most negative value
    if (int i = ZuCompare(m_julian, v.m_julian)) return i;
    if (int i = ZuCompare(m_sec, v.m_sec)) return i;
    return ZuCompare(m_nsec, v.m_nsec);
  }
  friend inline bool operator ==(const ZuDateTime &l, const ZuDateTime &r) {
    return l.equals(r);
  }
  friend inline int operator <=>(const ZuDateTime &l, const ZuDateTime &r) {
    return l.cmp(r);
  }

  constexpr bool operator *() const {
    return !ZuNull(m_julian);
  }

// utility functions

  uint32_t hash() const { return m_julian ^ m_sec ^ m_nsec; }

private:
  void ctor(int year, int month, int day) {
    normalize(year, month);
    m_julian = julian(year, month, day);
    m_sec = 0, m_nsec = 0;
  }
  void ctor(
    int year, int month, int day, int hour, int minute, int sec, int nsec)
  {
    normalize(year, month);
    normalize(day, hour, minute, sec, nsec);
    m_julian = julian(year, month, day);
    m_sec = second(hour, minute, sec);
    m_nsec = nsec;
  }

private:
  // parse fixed-width integers
  template <unsigned Width> struct Int {
    template <typename T, typename S>
    static int scan(T &v, S &s) {
      int i = v.scan(ZuFmt::Right<Width>(), s);
      if (ZuLikely(i > 0)) s.offset(i);
      return i;
    }
  };
  // parse nanoseconds
  template <typename T, typename S>
  static int scanFrac(T &v, S &s) {
    int i = v.scan(ZuFmt::Frac<9, 9>(), s);
    if (ZuLikely(i > 0)) s.offset(i);
    return i;
  }

  unsigned scan_(const ZuDateTimeScan::CSV &, ZuCSpan);
  unsigned scan_(const ZuDateTimeScan::FIX &, ZuCSpan);
  unsigned scan_(const ZuDateTimeScan::ISO &, ZuCSpan);
  unsigned scan_(const ZuDateTimeScan::ASN1_U &, ZuCSpan);
  unsigned scan_(const ZuDateTimeScan::ASN1_G &, ZuCSpan);
  unsigned scan_(const ZuDateTimeScan::Any &, ZuCSpan);

public:
  int scan(const ZuDateTimeScan::CSV &, ZuCSpan);
  static ZuTuple<int, ZuDateTime> eov(
    const ZuDateTimeScan::CSV &, ZuCSpan);
  int scan(const ZuDateTimeScan::FIX &, ZuCSpan);
  static ZuTuple<int, ZuDateTime> eov(
    const ZuDateTimeScan::FIX &, ZuCSpan);
  int scan(const ZuDateTimeScan::ISO &, ZuCSpan);
  static ZuTuple<int, ZuDateTime> eov(
    const ZuDateTimeScan::ISO &, ZuCSpan);
  int scan(const ZuDateTimeScan::ASN1_U &, ZuCSpan);
  static ZuTuple<int, ZuDateTime> eov(
    const ZuDateTimeScan::ASN1_U &, ZuCSpan);
  int scan(const ZuDateTimeScan::ASN1_G &, ZuCSpan);
  static ZuTuple<int, ZuDateTime> eov(
    const ZuDateTimeScan::ASN1_G &, ZuCSpan);
  int scan(const ZuDateTimeScan::Any &, ZuCSpan);
  static ZuTuple<int, ZuDateTime> eov(
    const ZuDateTimeScan::Any &, ZuCSpan);

  void normalize(unsigned &year, unsigned &month);
  void normalize(int &year, int &month);

  void normalize(unsigned &day, unsigned &hour, unsigned &minute,
      unsigned &sec, unsigned &nsec);
  void normalize(int &day, int &hour, int &minute, int &sec, int &nsec);

  static int julian(int year, int month, int day);

  static int second(int hour, int minute, int second) {
    return hour * 3600 + minute * 60 + second;
  }

  void init(int64_t t) {
    if (ZuUnlikely(ZuNull(t))) {
      null();
    } else {
      if (ZuLikely(t >= 0)) {
	int64_t j = t / 86400 + 2440588;
	if (j >= int64_t(1U<<31)) goto overflow;
	m_julian = j;
	m_sec = t % 86400;
      } else {
	if (ZuIntrin::sub(t, 86399, &t)) goto overflow;
	int64_t j = t / 86400 + 2440588;
	if (j < -int64_t(1U<<31)) goto overflow;
	m_julian = j;
	t = 86399 - (-t % 86400);
	m_sec = t;
      }
    }
    m_nsec = 0;
    return;
  overflow:
    null();
  }

  // default printing (CSV format)
  struct DefltPrint : public ZuPrintDelegate {
    template <typename S>
    static void print(S &s, const ZuDateTime &v) {
      thread_local ZuDateTimeFmt::CSV fmt;
      s << v.fmt(fmt);
    }
  };
  friend DefltPrint ZuPrintType(ZuDateTime *);

  // traits
  struct Traits : public ZuBaseTraits<ZuDateTime> { enum { IsPOD = 1 }; };
  friend Traits ZuTraitsType(ZuDateTime *);

private:
  int32_t	m_julian = ZuCmp<int32_t>::null();	// julian day
  int32_t	m_sec = 0;	// time within day, in seconds, from midnight
  int32_t	m_nsec = 0;	// nanoseconds

// effective date of the reformation (default 14th September 1752)

  static int		m_reformationJulian;
  static int		m_reformationYear;
  static int		m_reformationMonth;
  static int		m_reformationDay;
};

template <typename S>
inline void ZuDateTimePrintCSV::print(S &s) const {
  value.csv_print(s, fmt);
}
template <int NDP, class Null>
template <typename S>
inline void ZuDateTimePrintFIX<NDP, Null>::print(S &s) const {
  value.fix_print(s, fmt);
}
template <typename S>
inline void ZuDateTimePrintISO::print(S &s) const {
  value.iso_print(s, fmt);
}
template <typename S>
inline void ZuDateTimePrintASN1_U::print(S &s) const {
  value.asn1_u_print(s, fmt);
}
template <typename S>
inline void ZuDateTimePrintASN1_G::print(S &s) const {
  value.asn1_g_print(s, fmt);
}
template <typename S>
inline void ZuDateTimePrintStrftime::print(S &s) const
{
  auto format = fmt.format;

  if (!format || !*format) return;

  ZuDateTime value = this->value + fmt.tzOffset;

  ZuBox<int> year, month, day, hour, minute, second;
  ZuBox<int> days, week, wkDay, hour12;
  ZuBox<int> wkYearISO, weekISO, weekSun;
  ZuBox<time_t> seconds;

  // Conforming to, variously:
  // (C90) - ANSI C '90
  // (C99) - ANSI C '99
  // (SU) - Single Unix Specification
  // (MS) - Microsoft CRT
  // (GNU) - glibc (not all glibc-specific extensions are supported)
  // (TZ) - Arthur Olson's timezone library

  ZuVFmt fmt_;

  while (char c = *format++) {
    if (c != '%') { s << c; continue; }
    bool alt = false;
    ZuBox<unsigned> width;
fmtchar:
    c = *format++;
    switch (c) {
      case '#': // (MS) alt. format
      case 'E': // (SU) ''
	alt = true;
      case 'O': // (SU) alt. digits
	goto fmtchar;
      case '0':
      case '1':
      case '2':
      case '3':
      case '4':
      case '5':
      case '6':
      case '7':
      case '8':
      case '9': // (GNU) field width specifier
	format += width.scan(format);
	goto fmtchar;
      case 'a': // (C90) day of week - short name
	if (!*wkDay) wkDay = value.julian() % 7 + 1;
	s << ZuDateTime::dayShortName(wkDay);
	break;
      case 'A': // (C90) day of week - long name
	if (!*wkDay) wkDay = value.julian() % 7 + 1;
	s << ZuDateTime::dayLongName(wkDay);
	break;
      case 'b': // (C90) month - short name
      case 'h': // (SU) ''
	if (!*month) value.ymd(year, month, day);
	s << ZuDateTime::monthShortName(month);
	break;
      case 'B': // (C90) month - long name
	if (!*month) value.ymd(year, month, day);
	s << ZuDateTime::monthLongName(month);
	break;
      case 'c': // (C90) Unix asctime() / ctime() (%a %b %e %T %Y)
	if (!*year) value.ymd(year, month, day);
	if (!*hour) value.hms(hour, minute, second);
	if (!*wkDay) wkDay = value.julian() % 7 + 1;
	s << ZuDateTime::dayShortName(wkDay) << ' ' <<
	  ZuDateTime::monthShortName(month) << ' ' <<
	  vfmt(fmt_, day, 2, alt) << ' ' <<
	  vfmt(fmt_, hour, 2, alt) << ':' <<
	  vfmt(fmt_, minute, 2, alt) << ':' <<
	  vfmt(fmt_, second, 2, alt) << ' ' <<
	  vfmt(fmt_, year, 4, alt);
	break;
      case 'C': // (SU) century
	if (!*year) value.ymd(year, month, day);
	{
	  ZuBox<int> century = year / 100;
	  s << vfmt(fmt_, century, width, 2, alt);
	}
	break;
      case 'd': // (C90) day of month
	if (!*day) value.ymd(year, month, day);
	s << vfmt(fmt_, day, width, 2, alt);
	break;
      case 'x': // (C90) %m/%d/%y
      case 'D': // (SU) ''
	if (!*year) value.ymd(year, month, day);
	{
	  ZuBox<int> year_ = year % 100;
	  s << vfmt(fmt_, month, 2, alt) << '/' <<
	    vfmt(fmt_, day, 2, alt) << '/' <<
	    vfmt(fmt_, year_, 2, alt);
	}
	break;
      case 'e': // (SU) day of month - space padded
	if (!*year) value.ymd(year, month, day);
	s << vfmt(fmt_, day, width, 2, alt, ' ');
	break;
      case 'F': // (C99) %Y-%m-%d per ISO 8601
	if (!*year) value.ymd(year, month, day);
	s << vfmt(fmt_, year, 4, alt) << '-' <<
	  vfmt(fmt_, month, 2, alt) << '-' <<
	  vfmt(fmt_, day, 2, alt);
	break;
      case 'g': // (TZ) ISO week t year (2 digits)
      case 'G': // (TZ) '' (4 digits)
	if (!*wkYearISO) {
	  if (!*year) value.ymd(year, month, day);
	  if (!*days) days = value.days(year, 1, 1);
	  value.ywdISO(year, days, wkYearISO, weekISO, wkDay);
	}
	{
	  ZuBox<int> wkYearISO_ = wkYearISO;
	  if (c == 'g') wkYearISO_ %= 100;
	  s << vfmt(fmt_, wkYearISO_, width, (c == 'g' ? 2 : 4), alt);
	}
	break;
      case 'H': // (C90) hour (24hr)
	if (!*hour) value.hms(hour, minute, second);
	s << vfmt(fmt_, hour, width, 2, alt);
	break;
      case 'I': // (C90) hour (12hr)
	if (!*hour12) {
	  if (!*hour) value.hms(hour, minute, second);
	  hour12 = hour % 12;
	  if (!hour12) hour12 += 12;
	}
	s << vfmt(fmt_, hour12, width, 2, alt);
	break;
      case 'j': // (C90) day of year
	if (!*year) value.ymd(year, month, day);
	if (!*days) days = value.days(year, 1, 1);
	{
	  ZuBox<int> days_ = days + 1;
	  s << vfmt(fmt_, days_, width, 3, alt);
	}
	break;
      case 'm': // (C90) month
	if (!*month) value.ymd(year, month, day);
	s << vfmt(fmt_, month, width, 2, alt);
	break;
      case 'M': // (C90) minute
	if (!*minute) value.hms(hour, minute, second);
	s << vfmt(fmt_, minute, width, 2, alt);
	break;
      case 'n': // (SU) newline
	s << '\n';
	break;
      case 'p': // (C90) AM/PM
	if (!*hour) value.hms(hour, minute, second);
	s << (hour >= 12 ? "PM" : "AM");
	break;
      case 'P': // (GNU) am/pm
	if (!*hour) value.hms(hour, minute, second);
	s << (hour >= 12 ? "pm" : "am");
	break;
      case 'r': // (SU) %I:%M:%S %p
	if (!*hour) value.hms(hour, minute, second);
	if (!*hour12) {
	  hour12 = hour % 12;
	  if (!hour12) hour12 += 12;
	}
	s << vfmt(fmt_, hour12, 2, alt) << ':' <<
	  vfmt(fmt_, minute, 2, alt) << ':' <<
	  vfmt(fmt_, second, 2, alt) << ' ' <<
	  (hour >= 12 ? "PM" : "AM");
	break;
      case 'R': // (SU) %H:%M
	if (!*hour) value.hms(hour, minute, second);
	s << vfmt(fmt_, hour, 2, alt) << ':' <<
	  vfmt(fmt_, minute, 2, alt);
	break;
      case 's': // (TZ) number of seconds since the Epoch
	if (!*seconds) seconds = value.as_time_t();
	if (!alt && *width)
	  s << seconds.vfmt(ZuVFmt{}.right(width));
	else
	  s << seconds;
	break;
      case 'S': // (C90) second
	if (!*second) value.hms(hour, minute, second);
	s << vfmt(fmt_, second, width, 2, alt);
	break;
      case 't': // (SU) TAB
	s << '\t';
	break;
      case 'X': // (C90) %H:%M:%S
      case 'T': // (SU) ''
	if (!*hour) value.hms(hour, minute, second);
	s << vfmt(fmt_, hour, 2, alt) << ':' <<
	  vfmt(fmt_, minute, 2, alt) << ':' <<
	  vfmt(fmt_, second, 2, alt);
	break;
      case 'u': // (SU) week day as decimal (1-7), 1 is Monday (7 is Sunday)
	if (!*wkDay) wkDay = value.julian() % 7 + 1;
	s << vfmt(fmt_, wkDay, width, 1, alt);
	break;
      case 'U': // (C90) week (00-53), 1st Sunday in year is 1st day of week 1
	if (!*weekSun) {
	  if (!*year) value.ymd(year, month, day);
	  if (!*days) days = value.days(year, 1, 1);
	  {
	    int wkDay_;
	    value.ywdSun(days, weekSun, wkDay_);
	  }
	}
	s << vfmt(fmt_, weekSun, width, 2, alt);
	break;
      case 'V': // (SU) week (01-53), per ISO week t
	if (!*weekISO) {
	  if (!*year) value.ymd(year, month, day);
	  if (!*days) days = value.days(year, 1, 1);
	  value.ywdISO(year, days, wkYearISO, weekISO, wkDay);
	}
	s << vfmt(fmt_, weekISO, width, 2, alt);
	break;
      case 'w': // (C90) week day as decimal, 0 is Sunday
	if (!*wkDay) wkDay = value.julian() % 7 + 1;
	{
	  ZuBox<int> wkDay_ = wkDay;
	  if (wkDay_ == 7) wkDay_ = 0;
	  s << vfmt(fmt_, wkDay_, width, 1, alt);
	}
	break;
      case 'W': // (C90) week (00-53), 1st Monday in year is 1st day of week 1
	if (!*week) {
	  if (!*year) value.ymd(year, month, day);
	  if (!*days) days = value.days(year, 1, 1);
	  value.ywd(days, week, wkDay);
	}
	s << vfmt(fmt_, week, width, 2, alt);
	break;
      case 'y': // (C90) year (2 digits)
	if (!*year) value.ymd(year, month, day);
	{
	  ZuBox<int> year_ = year % 100;
	  s << vfmt(fmt_, year_, width, 2, alt);
	}
	break;
      case 'Y': // (C90) year (4 digits)
	if (!*year) value.ymd(year, month, day);
	s << vfmt(fmt_, year, width, 4, alt);
	break;
      case 'z': // (GNU) RFC 822 timezone tzOffset
      case 'Z': // (C90) timezone
	{
	  ZuBox<int> tzOffset_ = fmt.tzOffset;
	  if (tzOffset_ < 0) { s << '-'; tzOffset_ = -tzOffset_; }
	  tzOffset_ = (tzOffset_ / 3600) * 100 + (tzOffset_ % 3600) / 60;
	  s << tzOffset_;
	}
	break;
      case '%':
	s << '%';
	break;
    }
  }
}
template <typename S>
inline void ZuDateTimePrint::print(S &s) const {
  using namespace ZuDateTimeFmt;
  switch (fmt.type()) {
    default:
    case Any::Index<CSV>{}:
      s << value.fmt(fmt.csv());
      break;
    case Any::Index<FIXDeflt>{}:
      s << value.fmt(fmt.fix());
      break;
    case Any::Index<ISO>{}:
      s << value.fmt(fmt.iso());
      break;
    case Any::Index<ASN1_U>{}:
      s << value.fmt(fmt.asn1_u());
      break;
    case Any::Index<ASN1_G>{}:
      s << value.fmt(fmt.asn1_g());
      break;
    case Any::Index<Strftime>{}: {
      const auto &strftime = fmt.strftime();
      s << value.strftime(strftime.format, strftime.tzOffset);
    } break;
  }
}

#endif /* ZuDateTime_HH */
