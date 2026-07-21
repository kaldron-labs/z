//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Julian date based date/time class

#include <zlib/ZuBox.hh>
#include <zlib/ZuDateTime.hh>

int ZuDateTime::m_reformationYear = 1752;
int ZuDateTime::m_reformationMonth = 9;
int ZuDateTime::m_reformationDay = 14;
int ZuDateTime::m_reformationJulian = 2361222;

void ZuDateTime::reformation(int year, int month, int day)
{
  ZuDateTime r(year, month, day);

  if (ZuUnlikely(!*r)) return; // overflow

  m_reformationJulian = 0;

  m_reformationYear = 0, m_reformationMonth = 0, m_reformationDay = 0;

  m_reformationJulian = r.m_julian;

  r.ymd(m_reformationYear, m_reformationMonth, m_reformationDay);
}

struct tm *ZuDateTime::tm(struct tm *tm_) const
{
  memset(tm_, 0, sizeof(struct tm));

  ymd(tm_->tm_year, tm_->tm_mon, tm_->tm_mday);
  tm_->tm_year -= 1900, tm_->tm_mon--;
  hms(tm_->tm_hour, tm_->tm_min, tm_->tm_sec);

  return tm_;
}


void ZuDateTime::ymd(int &year, int &month, int &day) const
{
  if (ZuUnlikely(!**this)) { year = month = day = -1; return; }
  if (ZuLikely(m_julian >= m_reformationJulian)) {
    int i, j, l, n;

    l = m_julian + 68569;
    n = (l<<2) / 146097;
    l = l - ((146097 * n + 3)>>2);
    i = (4000 * (l + 1)) / 1461001;
    l = l - ((1461 * i)>>2) + 31;
    j = (80 * l) / 2447;
    day = l - (2447 * j) / 80;
    l = j / 11;
    month = j + 2 - 12 * l;
    year = (100 * (n - 49) + i + l);
  } else {
    int i, j, k, l, n;

    j = m_julian + 1402;
    k = (j - 1) / 1461;
    l = j - 1461 * k;
    n = (l - 1) / 365 - l / 1461;
    i = l - 365 * n + 30;
    j = (80 * i) / 2447;
    day = i - (2447 * j) / 80;
    i = j / 11;
    month = j + 2 - 12 * i;
    year = (k<<2) + n + i - 4716;
  }
}

void ZuDateTime::hms(int &hour, int &minute, int &sec) const
{
  if (ZuUnlikely(!**this)) { hour = minute = sec = -1; return; }

  int sec_ = m_sec;

  hour = sec_ / 3600, sec_ %= 3600,
  minute = sec_ / 60, sec = sec_ % 60;
}

void ZuDateTime::hmsn(int &hour, int &minute, int &sec, int &nsec) const
{
  if (ZuUnlikely(!**this)) { hour = minute = sec = nsec = -1; return; }
  hms(hour, minute, sec);
  nsec = m_nsec;
}

// week (0-53) wkDay (1-7)
// 1st Monday in year is 1st day of week 1
void ZuDateTime::ywd(int days, int &week, int &wkDay) const
{
  if (ZuUnlikely(!**this)) { wkDay = -1; return; }
  int wkDay_ = m_julian % 7;
  if (wkDay_ < 0) wkDay_ += 7;
  wkDay = wkDay_ + 1;
  week = days < wkDay_ ? 0 : ((days - wkDay_) / 7 + 1);
}

// week (0-53) wkDay (1-7)
// 1st Sunday in year is 1st day of week 1
void ZuDateTime::ywdSun(int days, int &week, int &wkDay) const
{
  if (ZuUnlikely(!**this)) { week = wkDay = -1; return; }
  int wkDay_ = (m_julian + 1) % 7;
  if (wkDay_ < 0) wkDay_ += 7;
  wkDay = wkDay_ + 1;
  week = days < wkDay_ ? 0 : ((days - wkDay_) / 7 + 1);
}

// week (1-53) wkDay (1-7)
// 1st Thursday in year is 4th day of week 1
void ZuDateTime::ywdISO(
    int year, int days, int &wkYear, int &week, int &wkDay) const
{
  if (ZuUnlikely(!**this)) { wkYear = week = wkDay = -1; return; }
  int wkDay_ = m_julian % 7;
  if (wkDay_ < 0) wkDay_ += 7;
  wkDay = wkDay_ + 1;
  int days_;
  if (days < wkDay_ - 3)
    days_ = this->days(wkYear = year - 1, 1, 1);
  else
    days_ = days, wkYear = year;
  week = ((days_ - wkDay_) + 3) / 7 + 1;
}

ZuCSpan ZuDateTime::dayShortName(int i)
{
  static const char *s[] =
    { "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun" };
  if (--i < 0 || i >= 7) return ZuCSpan("???", 3);
  return ZuCSpan(s[i], 3);
}

ZuCSpan ZuDateTime::dayLongName(int i)
{
  static const char *s[] =
    { "Monday", "Tuesday", "Wednesday", "Thursday",
      "Friday", "Saturday", "Sunday" };
  static uint8_t l[] = { 6, 7, 9, 8, 6, 8, 6 };
  if (--i < 0 || i >= 7) return ZuCSpan("???", 3);
  return ZuCSpan(s[i], l[i]);
}

ZuCSpan ZuDateTime::monthShortName(int i)
{
  static const char *s[] =
    { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep",
      "Oct", "Nov", "Dec" };
  if (--i < 0 || i >= 12) return ZuCSpan("???", 3);
  return ZuCSpan(s[i], 3);
}

ZuCSpan ZuDateTime::monthLongName(int i)
{
  static const char *s[] =
    { "January", "February", "March", "April", "May", "June", "July",
      "August", "September", "October", "November", "December" };
  static uint8_t l[] = { 7, 8, 5, 5, 3, 4, 4, 6, 9, 7, 8, 8  };
  if (--i < 0 || i >= 12) return ZuCSpan("???", 3);
  return ZuCSpan(s[i], l[i]);
}

int ZuDateTime::julian(int year, int month, int day)
{
  if (year > m_reformationYear ||
      (year == m_reformationYear &&
	(month > m_reformationMonth ||
	  (month == m_reformationMonth && day >= m_reformationDay)))) {
    int o = (month <= 2 ? -1 : 0);

    return ((1461 * (year + 4800 + o))>>2) +
      (367 * (month - 2 - 12 * o)) / 12 -
      ((3 * ((year + 4900 + o) / 100))>>2) +
      day - 32075;
  } else {
    return 367 * year - ((7 * (year + 5001 + (month - 9) / 7))>>2) +
      (275 * month) / 9 + day + 1729777;
  }
}

void ZuDateTime::normalize(unsigned &year, unsigned &month)
{
  if (month < 1) {
    year -= (12 - month) / 12;
    month = 12 - ((12 - month) % 12);
    return;
  }

  if (month > 12) {
    year += (month - 1) / 12;
    month = ((month - 1) % 12) + 1;
    return;
  }
}
void ZuDateTime::normalize(int &year, int &month)
{
  if (month < 1) {
    year -= (12 - month) / 12;
    month = 12 - ((12 - month) % 12);
    return;
  }

  if (month > 12) {
    year += (month - 1) / 12;
    month = ((month - 1) % 12) + 1;
    return;
  }
}

void ZuDateTime::normalize(
  unsigned &day, unsigned &hour, unsigned &minute,
  unsigned &sec, unsigned &nsec)
{
  if (nsec > 999999999) {
    sec += nsec / 1000000000;
    nsec = nsec % 1000000000;
  }

  if (sec > 59) {
    minute += sec / 60;
    sec = sec % 60;
  }

  if (minute > 59) {
    hour += minute / 60;
    minute = minute % 60;
  }

  if (hour > 23) {
    day += hour / 24;
    hour = hour % 24;
  }
}

void ZuDateTime::normalize(
  int &day, int &hour, int &minute, int &sec, int &nsec)
{
  if (nsec < 0) {
    sec -= (999999999 - nsec) / 1000000000;
    nsec = 999999999 - ((999999999 - nsec) % 1000000000);
  } else if (nsec > 999999999) {
    sec += nsec / 1000000000;
    nsec = nsec % 1000000000;
  }

  if (sec < 0) {
    minute -= (59 - sec) / 60;
    sec = 59 - ((59 - sec) % 60);
  } else if (sec > 59) {
    minute += sec / 60;
    sec = sec % 60;
  }

  if (minute < 0) {
    hour -= (59 - minute) / 60;
    minute = 59 - ((59 - minute) % 60);
  } else if (minute > 59) {
    hour += minute / 60;
    minute = minute % 60;
  }

  if (hour < 0) {
    day -= (23 - hour) / 24;
    hour = 23 - ((23 - hour) % 24);
  } else if (hour > 23) {
    day += hour / 24;
    hour = hour % 24;
  }
}

int ZuDateTime::scan(const ZuDateTimeScan::CSV &fmt, ZuCSpan s)
{
  auto r = eov(fmt, s);
  *this = r.p<1>();
  return r.p<0>();
}

ZuTuple<int, ZuDateTime>
ZuDateTime::eov(const ZuDateTimeScan::CSV &fmt, ZuCSpan s)
{
  ZuDateTime v;
  unsigned n = v.scan_(fmt, s);
  if (!n) return {-1, ZuDateTime{}};
  return {int(n), v};
}

unsigned ZuDateTime::scan_(const ZuDateTimeScan::CSV &fmt, ZuCSpan s)
{
  {
    unsigned year, month, day, hour, minute, sec, nsec;
    const char *ptr = s.data();
    const char *end = ptr + s.length();
    unsigned c;
    bool bc = 0;

  bc:
    if (ZuUnlikely(end - ptr < 10)) goto invalid;
    c = *ptr++;
    if (ZuUnlikely(c == '-')) { bc = 1; goto bc; }
    c = char(c) - '0'; year = c * 1000;
    c = *ptr++ - '0'; year += c * 100;
    c = *ptr++ - '0'; year += c * 10;
    c = *ptr++ - '0'; year += c;
    if (ZuUnlikely(*ptr++ != '/')) goto invalid;
    c = *ptr++ - '0'; month = c * 10;
    c = *ptr++ - '0'; month += c;
    if (ZuUnlikely(*ptr++ != '/')) goto invalid;
    c = *ptr++ - '0'; day = c * 10;
    c = *ptr++ - '0'; day += c;
    if (ZuUnlikely(
	!year || year > 9999U ||
	!month || month > 12U ||
	!day || day > 31U)) goto invalid;

    if (ptr >= end || *ptr != ' ') {
      int year_ = year, month_ = month;
      if (ZuUnlikely(bc)) year_ = -year_;
      normalize(year_, month_);
      m_julian = julian(year_, month_, day);
      m_sec = 0;
      m_nsec = 0;
      if (ZuUnlikely(fmt.tzOffset)) *this += fmt.tzOffset;
      return ptr - s.data();
    }
    ++ptr;

    if (ZuUnlikely(end - ptr < 8)) goto invalid;
    c = *ptr++ - '0'; hour = c * 10;
    c = *ptr++ - '0'; hour += c;
    if (ZuUnlikely(*ptr++ != ':')) goto invalid;
    c = *ptr++ - '0'; minute = c * 10;
    c = *ptr++ - '0'; minute += c;
    if (ZuUnlikely(*ptr++ != ':')) goto invalid;
    c = *ptr++ - '0'; sec = c * 10;
    c = *ptr++ - '0'; sec += c;
    if (ZuUnlikely(hour > 23U || minute > 59U || sec > 59U)) goto invalid;

    nsec = 0;
    if (ZuLikely(ptr < end)) {
      if (ZuUnlikely(*ptr != '.' || end - ptr < 2)) goto end;
      ++ptr;
      unsigned pow = 100000000;
      c = *ptr++ - '0'; if (ZuUnlikely(c >= 10)) goto invalid;
      nsec = c * pow;
      while (ptr < end) {
	c = *ptr - '0'; if (ZuUnlikely(c >= 10)) break;
	++ptr;
	nsec += c * (pow /= 10);
      }
    }

  end:
    normalize(year, month);
    normalize(day, hour, minute, sec, nsec);
    m_julian = julian(year, month, day);
    m_sec = second(hour, minute, sec);
    m_nsec = nsec;

    if (ZuUnlikely(fmt.tzOffset)) *this += fmt.tzOffset;
    return ptr - s.data();
  }

invalid:
  m_julian = ZuCmp<int32_t>::null(), m_sec = 0, m_nsec = 0;
  return 0;
}

int ZuDateTime::scan(const ZuDateTimeScan::FIX &fmt, ZuCSpan s)
{
  auto r = eov(fmt, s);
  *this = r.p<1>();
  return r.p<0>();
}

ZuTuple<int, ZuDateTime>
ZuDateTime::eov(const ZuDateTimeScan::FIX &fmt, ZuCSpan s)
{
  ZuDateTime v;
  unsigned n = v.scan_(fmt, s);
  if (!n) return {-1, ZuDateTime{}};
  return {int(n), v};
}

unsigned ZuDateTime::scan_(const ZuDateTimeScan::FIX &fmt, ZuCSpan s)
{
  {
    unsigned year, month, day, hour, minute, sec, nsec;
    const char *ptr = s.data();
    const char *end = ptr + s.length();
    unsigned c;

    if (ZuUnlikely(end - ptr < 17)) goto invalid;
    c = *ptr++ - '0'; year = c * 1000;
    c = *ptr++ - '0'; year += c * 100;
    c = *ptr++ - '0'; year += c * 10;
    c = *ptr++ - '0'; year += c;
    c = *ptr++ - '0'; month = c * 10;
    c = *ptr++ - '0'; month += c;
    c = *ptr++ - '0'; day = c * 10;
    c = *ptr++ - '0'; day += c;
    if (ZuUnlikely(
	!year || year > 9999U ||
	!month || month > 12U ||
	!day || day > 31U)) goto invalid;
    if (ZuUnlikely(*ptr++ != '-')) goto invalid;
    c = *ptr++ - '0'; hour = c * 10;
    c = *ptr++ - '0'; hour += c;
    if (ZuUnlikely(*ptr++ != ':')) goto invalid;
    c = *ptr++ - '0'; minute = c * 10;
    c = *ptr++ - '0'; minute += c;
    if (ZuUnlikely(*ptr++ != ':')) goto invalid;
    c = *ptr++ - '0'; sec = c * 10;
    c = *ptr++ - '0'; sec += c;
    if (ZuUnlikely(hour > 23U || minute > 59U || sec > 59U)) goto invalid;

    nsec = 0;
    if (ZuLikely(ptr < end)) {
      if (ZuUnlikely(*ptr != '.' || end - ptr < 2)) goto end;
      ++ptr;
      unsigned pow = 100000000;
      c = *ptr++ - '0'; if (ZuUnlikely(c >= 10)) goto invalid;
      nsec = c * pow;
      while (ptr < end) {
	c = *ptr - '0'; if (ZuUnlikely(c >= 10)) break;
	++ptr;
	nsec += c * (pow /= 10);
      }
    }

  end:
    // normalize(year, month);
    // normalize(day, hour, minute, sec, nsec);
    m_julian = julian(year, month, day);
    m_sec = second(hour, minute, sec);
    m_nsec = nsec;

    return ptr - s.data();
  }

invalid:
  m_julian = ZuCmp<int32_t>::null(), m_sec = 0, m_nsec = 0;
  return 0;
}

int ZuDateTime::scan(const ZuDateTimeScan::ISO &fmt, ZuCSpan s)
{
  auto r = eov(fmt, s);
  *this = r.p<1>();
  return r.p<0>();
}

ZuTuple<int, ZuDateTime>
ZuDateTime::eov(const ZuDateTimeScan::ISO &fmt, ZuCSpan s)
{
  ZuDateTime v;
  unsigned n = v.scan_(fmt, s);
  if (!n) return {-1, ZuDateTime{}};
  return {int(n), v};
}

unsigned ZuDateTime::scan_(const ZuDateTimeScan::ISO &fmt, ZuCSpan s)
{
  {
    unsigned year, month, day, hour, minute, sec, nsec;
    const char *ptr = s.data();
    const char *end = ptr + s.length();
    unsigned c;
    bool bc = 0;

  bc:
    if (ZuUnlikely(end - ptr < 10)) goto invalid;
    c = *ptr++;
    if (ZuUnlikely(c == '-')) { bc = 1; goto bc; }
    c = char(c) - '0'; year = c * 1000;
    c = *ptr++ - '0'; year += c * 100;
    c = *ptr++ - '0'; year += c * 10;
    c = *ptr++ - '0'; year += c;
    if (ZuUnlikely(*ptr++ != '-')) goto invalid;
    c = *ptr++ - '0'; month = c * 10;
    c = *ptr++ - '0'; month += c;
    if (ZuUnlikely(*ptr++ != '-')) goto invalid;
    c = *ptr++ - '0'; day = c * 10;
    c = *ptr++ - '0'; day += c;
    if (ZuUnlikely(
	!year || year > 9999U ||
	!month || month > 12U ||
	!day || day > 31U)) goto invalid;

    if (ptr >= end || *ptr != 'T') {
      int year_ = year, month_ = month;
      if (ZuUnlikely(bc)) year_ = -year_;
      normalize(year_, month_);
      m_julian = julian(year_, month_, day);
      m_sec = 0;
      m_nsec = 0;
      if (ZuUnlikely(fmt.tzOffset)) *this += fmt.tzOffset;
      return ptr - s.data();
    }
    ++ptr;

    if (ZuUnlikely(end - ptr < 8)) goto invalid;
    c = *ptr++ - '0'; hour = c * 10;
    c = *ptr++ - '0'; hour += c;
    if (ZuUnlikely(*ptr++ != ':')) goto invalid;
    c = *ptr++ - '0'; minute = c * 10;
    c = *ptr++ - '0'; minute += c;
    if (ZuUnlikely(*ptr++ != ':')) goto invalid;
    c = *ptr++ - '0'; sec = c * 10;
    c = *ptr++ - '0'; sec += c;
    if (ZuUnlikely(hour > 23U || minute > 59U || sec > 59U)) goto invalid;

    nsec = 0;
    if (ZuLikely(ptr < end)) {
      if (ZuUnlikely(end - ptr < 2)) goto tz;
      if (ZuUnlikely((c = *ptr++) != '.')) { --ptr; goto tz; }
      unsigned pow = 100000000;
      c = *ptr++ - '0'; if (ZuUnlikely(c >= 10)) goto invalid;
      nsec = c * pow;
      while (ptr < end) {
	c = *ptr++ - '0'; if (ZuUnlikely(c >= 10)) { --ptr; break; }
	nsec += c * (pow /= 10);
      }
    }

  tz:
    normalize(year, month);
    normalize(day, hour, minute, sec, nsec);
    m_julian = julian(year, month, day);
    m_sec = second(hour, minute, sec);
    m_nsec = nsec;

    if (ptr >= end) {
      if (ZuUnlikely(fmt.tzOffset)) *this += fmt.tzOffset;
      return ptr - s.data();
    }

    c = *ptr;
    if (c == 'Z') return ++ptr - s.data();

    int offset;

    if (c == '+') {
      offset = 1;
    } else if (ZuLikely(c == '-')) {
      offset = -1;
    } else {
      if (ZuUnlikely(fmt.tzOffset)) *this += fmt.tzOffset;
      return ptr - s.data();
    }
    ++ptr;

    if (ZuUnlikely(end - ptr < 2)) goto invalid;

    unsigned offsetHours, offsetMinutes = 0;

    c = *ptr++ - '0'; offsetHours = c * 10;
    c = *ptr++ - '0'; offsetHours += c;
    if (ZuLikely(ptr >= end)) goto offset;
    c = *ptr;
    if (c == ':') {
      ++ptr;
      if (ZuUnlikely(end - ptr < 2)) goto invalid;
      c = *ptr++;
    } else {
      if (unsigned(char(c) - '0') >= 10) goto offset;
      ++ptr;
      if (ZuUnlikely(end - ptr < 1)) goto invalid;
    }
    c = char(c) - '0'; offsetMinutes = c * 10;
    c = *ptr++ - '0'; offsetMinutes += c;
    if (ZuUnlikely(offsetHours > 23U || offsetMinutes > 59U)) goto invalid;

  offset:
    if (offset >= 0)
      offset = -(int)(offsetHours * 60 + offsetMinutes) * 60;
    else
      offset = (offsetHours * 60 + offsetMinutes) * 60;

    *this += offset;
    return ptr - s.data();
  }

invalid:
  m_julian = ZuCmp<int32_t>::null(), m_sec = 0, m_nsec = 0;
  return 0;
}

int ZuDateTime::scan(const ZuDateTimeScan::ASN1_U &fmt, ZuCSpan s)
{
  auto r = eov(fmt, s);
  *this = r.p<1>();
  return r.p<0>();
}

ZuTuple<int, ZuDateTime>
ZuDateTime::eov(const ZuDateTimeScan::ASN1_U &fmt, ZuCSpan s)
{
  ZuDateTime v;
  unsigned n = v.scan_(fmt, s);
  if (!n) return {-1, ZuDateTime{}};
  return {int(n), v};
}

unsigned ZuDateTime::scan_(const ZuDateTimeScan::ASN1_U &fmt, ZuCSpan s)
{
  {
    unsigned year, month, day, hour, minute, sec, nsec = 0;
    const char *ptr = s.data();
    const char *end = ptr + s.length();
    unsigned c;

    if (ZuUnlikely(end - ptr < 13)) goto invalid;
    c = *ptr++;
    c = char(c) - '0'; year = c * 10;
    c = *ptr++ - '0'; year += c;
    c = *ptr++ - '0'; month = c * 10;
    c = *ptr++ - '0'; month += c;
    c = *ptr++ - '0'; day = c * 10;
    c = *ptr++ - '0'; day += c;
    if (ZuUnlikely(
	!month || month > 12U ||
	!day || day > 31U)) goto invalid;
    year += 2000; if (year >= 2050) year -= 100;

    c = *ptr++ - '0'; hour = c * 10;
    c = *ptr++ - '0'; hour += c;
    c = *ptr++ - '0'; minute = c * 10;
    c = *ptr++ - '0'; minute += c;
    c = *ptr++ - '0'; sec = c * 10;
    c = *ptr++ - '0'; sec += c;
    if (ZuUnlikely(hour > 23U || minute > 59U || sec > 59U)) goto invalid;

    c = *ptr++; if (c != 'Z') goto invalid;

    normalize(year, month);
    normalize(day, hour, minute, sec, nsec);
    m_julian = julian(year, month, day);
    m_sec = second(hour, minute, sec);
    m_nsec = nsec;
    return ptr - s.data();
  }

invalid:
  m_julian = ZuCmp<int32_t>::null(), m_sec = 0, m_nsec = 0;
  return 0;
}

int ZuDateTime::scan(const ZuDateTimeScan::ASN1_G &fmt, ZuCSpan s)
{
  auto r = eov(fmt, s);
  *this = r.p<1>();
  return r.p<0>();
}

ZuTuple<int, ZuDateTime>
ZuDateTime::eov(const ZuDateTimeScan::ASN1_G &fmt, ZuCSpan s)
{
  ZuDateTime v;
  unsigned n = v.scan_(fmt, s);
  if (!n) return {-1, ZuDateTime{}};
  return {int(n), v};
}

unsigned ZuDateTime::scan_(const ZuDateTimeScan::ASN1_G &fmt, ZuCSpan s)
{
  {
    unsigned year, month, day, hour, minute, sec, nsec;
    const char *ptr = s.data();
    const char *end = ptr + s.length();
    unsigned c;

    if (ZuUnlikely(end - ptr < 15)) goto invalid;
    c = *ptr++;
    c = char(c) - '0'; year = c * 1000;
    c = *ptr++ - '0'; year += c * 100;
    c = *ptr++ - '0'; year += c * 10;
    c = *ptr++ - '0'; year += c;
    c = *ptr++ - '0'; month = c * 10;
    c = *ptr++ - '0'; month += c;
    c = *ptr++ - '0'; day = c * 10;
    c = *ptr++ - '0'; day += c;
    if (ZuUnlikely(
	!year || year > 9999U ||
	!month || month > 12U ||
	!day || day > 31U)) goto invalid;

    c = *ptr++ - '0'; hour = c * 10;
    c = *ptr++ - '0'; hour += c;
    c = *ptr++ - '0'; minute = c * 10;
    c = *ptr++ - '0'; minute += c;
    c = *ptr++ - '0'; sec = c * 10;
    c = *ptr++ - '0'; sec += c;
    if (ZuUnlikely(hour > 23U || minute > 59U || sec > 59U)) goto invalid;

    nsec = 0;
    c = *ptr; if (c == 'Z') goto end;
    if (ZuUnlikely(c != '.')) goto invalid;
    if (ZuUnlikely(end - ptr < 2)) goto end;
    ptr++;
    {
      unsigned pow = 100000000;
      c = *ptr++ - '0'; if (ZuUnlikely(c >= 10)) goto invalid;
      nsec = c * pow;
      while (ptr < end) {
	c = *ptr++ - '0'; if (ZuUnlikely(c >= 10)) break;
	nsec += c * (pow /= 10);
      }
    }

  end:
    normalize(year, month);
    normalize(day, hour, minute, sec, nsec);
    m_julian = julian(year, month, day);
    m_sec = second(hour, minute, sec);
    m_nsec = nsec;
    return ptr - s.data();
  }

invalid:
  m_julian = ZuCmp<int32_t>::null(), m_sec = 0, m_nsec = 0;
  return 0;
}

int ZuDateTime::scan(const ZuDateTimeScan::Any &fmt, ZuCSpan s)
{
  auto r = eov(fmt, s);
  *this = r.p<1>();
  return r.p<0>();
}

ZuTuple<int, ZuDateTime>
ZuDateTime::eov(const ZuDateTimeScan::Any &fmt, ZuCSpan s)
{
  ZuDateTime v;
  unsigned n = v.scan_(fmt, s);
  if (!n) return {-1, ZuDateTime{}};
  return {int(n), v};
}

unsigned ZuDateTime::scan_(const ZuDateTimeScan::Any &fmt, ZuCSpan s)
{
  return fmt.cdispatch([this, s]<typename Fmt>(auto, Fmt &&fmt) {
    return scan_(ZuFwd<Fmt>(fmt), s);
  });
}
