//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuLib.hh>

#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <limits.h>
#include <math.h>

#include <zlib/ZuDateTime.hh>

#include <zlib/ZmSpecific.hh>
#include <zlib/ZmTime.hh>

#include <zlib/ZtString.hh>
#include <zlib/ZtTimeZone.hh>

#define CHECK(x) ((x) ? puts("OK  " #x) : puts("NOK " #x))

ZuDateTimePrintISO isoPrint(const ZuDateTime &d, int tzOffset = 0) {
  auto &fmt = ZmTLS<ZuDateTimeFmt::ISO, isoPrint>();
  fmt.tzOffset(tzOffset);
  return d.fmt(fmt);
}

ZuCArray<40> isoStr(const ZuDateTime &d, int tzOffset = 0) {
  return ZuCArray<40>{} << isoPrint(d, tzOffset);
}

struct LocalDT {
  LocalDT(const ZuDateTime &d) {
    int tzOffset = Zt::tzOffset(d);
    ZuDateTime l = d + tzOffset;
    l.ymd(m_Y, m_M, m_D);
    l.hmsn(m_h, m_m, m_s, m_n);
    ZmAssert(d == ZuDateTime(m_Y, m_M, m_D, m_h, m_m, m_s, m_n) - tzOffset);
  }
  ZtString<> dump() {
    return ZtSprintf<ZtString<>>(
      "Lcl %.4d/%.2d/%.2d %.2d:%.2d:%.2d.%.6d",
      m_Y, m_M, m_D, m_h, m_m, m_s, m_n / 1000
    );
  }
  int m_Y, m_M, m_D, m_h, m_m, m_s, m_n;
};

struct GMTDT {
  GMTDT(const ZuDateTime &d) {
    d.ymd(m_Y, m_M, m_D);
    d.hmsn(m_h, m_m, m_s, m_n);
    ZmAssert(d == ZuDateTime(m_Y, m_M, m_D, m_h, m_m, m_s, m_n));
  }
  ZtString<> dump() {
    return ZtSprintf<ZtString<>>(
      "GMT %.4d/%.2d/%.2d %.2d:%.2d:%.2d.%.6d",
      m_Y, m_M, m_D, m_h, m_m, m_s, m_n / 1000
    );
  }
  int m_Y, m_M, m_D, m_h, m_m, m_s, m_n;
};

void weekDate(ZuDateTime d, int year, int weekChk, int wkDayChk) {
  ZuDateTimeFmt::ISO fmt;
  int week, wkDay;
  int days = d.days(year, 1, 1);
  d.ywd(days, week, wkDay);
  printf(
    "%s: %d+%d %d+%d.%dW %d %d\n",
    isoStr(d).data(), year, days, year, days / 7, days % 7, week, wkDay
  );
  CHECK(week == weekChk);
  CHECK(wkDay == wkDayChk);
}

void weekDateSun(ZuDateTime d, int year, int weekChk, int wkDayChk) {
  ZuDateTimeFmt::ISO fmt;
  int week, wkDay;
  int days = d.days(year, 1, 1);
  d.ywdSun(days, week, wkDay);
  printf(
    "%s: %d+%d %d+%d.%dW %d %d\n",
    isoStr(d).data(), year, days, year, days / 7, days % 7, week, wkDay
  );
  CHECK(week == weekChk);
  CHECK(wkDay == wkDayChk);
}

void weekDateISO(
  ZuDateTime d, int year, int yearChk, int weekChk, int wkDayChk
) {
  ZuDateTimeFmt::ISO fmt;
  int yearISO, weekISO, wkDay;
  int days = d.days(year, 1, 1);
  d.ywdISO(year, days, yearISO, weekISO, wkDay);
  printf(
    "%s: %d+%d %d+%d.%dW %d %d %d\n",
    isoStr(d).data(), year, days, year, days / 7, days % 7,
    yearISO, weekISO, wkDay
  );
  CHECK(yearISO == yearChk);
  CHECK(weekISO == weekChk);
  CHECK(wkDay == wkDayChk);
}

void strftimeChk(ZuDateTime d, const char *format, const char *chk) {
  ZtString<> s;
  s << d.strftime(format);
  puts(s);
  puts(chk);
  CHECK(s == chk);
}

int main() {
#ifdef _WIN32
  static const char *t[3] = {"JST-9", "GMT", "EST5EDT"};
#else
  static const char *t[3] = {"Japan", "GB", "EST5EDT"};
#endif

  Zt::tzset();

  // ZuDateTime d(1998, 12, 31);
  // ZuDateTime d(51179 + 2400000,0);
  ZuDateTime d(1998, 12, 1, 10, 30, 0);
  // ZuDateTime d(1998, 13, 0, 24, -1, 59);
  // ZuDateTime d(1970, 1, 1, 9, 0, 0);

  int i;
  ZuCArray<32> s = isoStr(d);

  printf("GMT %s\n", s.data());
  {
    ZuDateTime e(s);
    printf(
      "GMT %s\n%s\n%s\n",
      isoStr(e).data(), LocalDT(e).dump().data(), GMTDT(e).dump().data()
    );
  }
  for (i = 0; i < 3; i++) {
    printf("%s %s\n", t[i], isoStr(d, Zt::tzOffset(d, t[i])).data());
    {
      ZuDateTime e(s);
      printf("%s %s\n\n", t[i], isoStr(e, Zt::tzOffset(e, t[i])).data());
    }
  }
  printf("local %s\n", isoStr(d, Zt::tzOffset(d)).data());
  {
    ZuDateTime e(s);
    printf(
      "local %s\n%s\n%s\n",
      isoStr(e, Zt::tzOffset(e)).data(),
      LocalDT(e).dump().data(), GMTDT(e).dump().data()
    );
  }

  d -= ZuTime(180 * 86400, 999995000); // 180 days, .999995 seconds

  printf("GMT %s\n", isoStr(d).data());
  {
    ZuDateTime e(s);
    printf("GMT %s\n\n", isoStr(e).data());
  }
  for (i = 0; i < 3; i++) {
    printf("%s %s\n", t[i], isoStr(d, Zt::tzOffset(d, t[i])).data());
    {
      ZuDateTime e(s);
      printf("%s %s\n\n", t[i], isoStr(e, Zt::tzOffset(e, t[i])).data());
    }
  }
  printf("local %s\n", isoStr(d, Zt::tzOffset(d)).data());
  {
    ZuDateTime e(s);
    printf("local %s\n\n", isoStr(e, Zt::tzOffset(e)).data());
  }

  printf(
    "local now %s\n", isoStr(ZuDateTime{Zm::now()}, Zt::tzOffset(d)).data()
  );

  // ZuDateTime d(2050, 2, 3, 10, 0, 0, -timezone);

  // ZuDateTime d_(1752, 9, 1, 12, 30, 0, -timezone);

  d = ZuDateTime(ZuDateTime::Julian{0}, 0, 0);
  printf("ZuDateTime min: %s\n", isoStr(d).data());
  d = ZuDateTime(d.as_time_t());
  printf("time_t min: %s\n", isoStr(d).data());

  d = ZuDateTime(ZuDateTime::Julian{ZuDateTime_MaxJulian}, 0, 0);
  printf("ZuDateTime max: %s\n", isoStr(d).data());
  d = ZuDateTime(d.as_time_t());
  printf("time_t max: %s\n", isoStr(d).data());

  {
    const char *s = "2011-04-07T10:30:00+0800";
    d = ZuDateTime(ZuCSpan(s));
    printf("%s = %s\n", s, isoStr(d).data());
  }
  {
    const char *s = "2011-04-07T10:30:00.0012345+08:00";
    d = ZuDateTime(ZuCSpan(s));
    printf("%s = %s\n", s, isoStr(d).data());
  }

  {
    ZuDateTime d1{Zm::now()};
    Zm::sleep(.1);
    ZuDateTime d2{Zm::now()};
    ZuTime t = d2 - d1;

    printf(
      "\n1/10 sec delta time check: %s\n\n",
      (ZuCArray<32>{} << t.interval()).data()
    );
  }

  {
    weekDate(ZuDateTime(
	ZuDateTime::YYYYMMDD{20080106}, ZuDateTime::HHMMSS{0}), 2008, 0, 7);
    weekDate(ZuDateTime(
	ZuDateTime::YYYYMMDD{20080107}, ZuDateTime::HHMMSS{0}), 2008, 1, 1);
    weekDateSun(ZuDateTime(
	ZuDateTime::YYYYMMDD{20070106}, ZuDateTime::HHMMSS{0}), 2007, 0, 7);
    weekDateSun(ZuDateTime(
	ZuDateTime::YYYYMMDD{20070107}, ZuDateTime::HHMMSS{0}), 2007, 1, 1);
    {
      ZuDateTime d(ZuDateTime::YYYYMMDD{20071231}, ZuDateTime::HHMMSS{0});
      int year, month, day;
      d.ymd(year, month, day);
      CHECK(year == 2007);
      CHECK(month == 12);
      CHECK(day == 31);
      weekDateISO(d, year, 2007, 53, 1);
    }
    weekDateISO(ZuDateTime(
	ZuDateTime::YYYYMMDD{20070101},
	ZuDateTime::HHMMSS{0}), 2007, 2007, 1, 1);
    weekDateISO(ZuDateTime(
	ZuDateTime::YYYYMMDD{20100103},
	ZuDateTime::HHMMSS{0}), 2010, 2009, 53, 7);
    weekDateISO(ZuDateTime(
	ZuDateTime::YYYYMMDD{20110102},
	ZuDateTime::HHMMSS{0}), 2011, 2010, 52, 7);
    weekDateISO(ZuDateTime(
	ZuDateTime::YYYYMMDD{17520902},
	ZuDateTime::HHMMSS{0}), 1752, 1752, 36, 3);
    weekDateISO(ZuDateTime(
	ZuDateTime::YYYYMMDD{17520914},
	ZuDateTime::HHMMSS{0}), 1752, 1752, 36, 4);
    weekDateISO(ZuDateTime(
	ZuDateTime::YYYYMMDD{17521231},
	ZuDateTime::HHMMSS{0}), 1752, 1752, 51, 7);
  }

  {
    strftimeChk(
      ZuDateTime(ZuDateTime::YYYYMMDD{17520902}, ZuDateTime::HHMMSS{143000}),
      "%a %A %b %B %C %d %e %g %G %H %I %j %m %M %p %P %S %u %V %Y",
      "Wed Wednesday Sep September 17 02  2 52 1752 "
      "14 02 246 09 30 PM pm 00 3 36 1752"
    );
  }

  {
    ZuDateTime winter{ZuDateTime::YYYYMMDD{20070107}, ZuDateTime::HHMMSS{0}};
    ZuDateTime summer{ZuDateTime::YYYYMMDD{20070707}, ZuDateTime::HHMMSS{0}};
    ZuDateTimeFmt::CSV fmt;
    auto winterOff = Zt::tzOffset(winter, "GB");
    auto summerOff = Zt::tzOffset(summer, "GB");
    fmt.tzOffset(winterOff);
    ZtString<> winterUK; winterUK << winter.fmt(fmt);
    fmt.tzOffset(summerOff);
    ZtString<> summerUK; summerUK << summer.fmt(fmt);
    CHECK(summerUK == "2007/07/07 01:00:00");
    CHECK(winterUK == "2007/01/07 00:00:00");
  }

#ifndef _WIN32
  ZuTime t1, t1_;
#else
  ZuTime o1, o2, t1, t1_, t2, t2_;
  double d1 = 0, d2 = 0, d3 = 0, d1sum = 0, d2sum = 0, d3sum = 0, d1vsum = 0,
	 d2vsum = 0, d3vsum = 0, d1avg = 0, d2avg = 0, d3avg = 0, d1std = 0,
	 d2std = 0, d3std = 0;
  FILETIME f;
#endif

  t1_ = Zm::now();
  for (i = 1; i <= 1000000; i++) t1 = Zm::now();
  t1_ = Zm::now() - t1_;
  auto intrinsic = t1_.as_decimal() / 1000000;

  printf(
    "\nZm::now() intrinsic cost: %s\n",
    (ZuCArray<32>{} << intrinsic).data()
  );

#ifdef _WIN32
  GetSystemTimeAsFileTime(&f);
  t2_ = f;
  t1_ = Zm::now();

  o2 = t2_;
  o1 = t1_;

  for (i = 1; i <= 5000000; i++) {
    GetSystemTimeAsFileTime(&f);
    t2 = f;
    t1 = Zm::now();
    t1 -= intrinsic;
    d1 = (t1 - t1_).as_ldouble();
    d2 = (t2 - t2_).as_ldouble();
    d3 = (t1 - t2).as_ldouble();
    d1sum += d1;
    d2sum += d2;
    d3sum += d3;
    d1vsum += d1 * d1;
    d2vsum += d2 * d2;
    d3vsum += d3 * d3;
    t1_ = t1;
    t2_ = t2;
  }
  --i;

  d1avg = d1sum / i;
  d2avg = d2sum / i;
  d3avg = d3sum / i;
  d1std = sqrt(d1vsum / i - d1avg * d1avg);
  d2std = sqrt(d2vsum / i - d2avg * d2avg);
  d3std = sqrt(d3vsum / i - d3avg * d3avg);

  printf(
    "\n"
    "ZuTime cnt: % 10d avg: %12.10f std: %12.10f\n"
    "GSTAFT cnt: % 10d avg: %12.10f std: %12.10f\n"
    "ZuTime - GSTAFT skew   avg: %12.10f std: %12.10f\n",
    i, d1avg, d1std, i, d2avg, d2std, d3avg, d3std
  );

  for (++i; i <= 10000000; i++) {
    GetSystemTimeAsFileTime(&f);
    t2 = f;
    t1 = Zm::now();
    t1 -= intrinsic;
    d1 = (t1 - t1_).as_ldouble();
    d2 = (t2 - t2_).as_ldouble();
    d3 = (t1 - t2).as_ldouble();
    d1sum += d1;
    d2sum += d2;
    d3sum += d3;
    d1vsum += d1 * d1;
    d2vsum += d2 * d2;
    d3vsum += d3 * d3;
    t1_ = t1;
    t2_ = t2;
  }
  --i;

  GetSystemTimeAsFileTime(&f);
  o2 = ZuTime(f) - o2;
  o1 = Zm::now() - o1;

  d1avg = d1sum / i;
  d2avg = d2sum / i;
  d3avg = d3sum / i;
  d1std = sqrt(d1vsum / i - d1avg * d1avg);
  d2std = sqrt(d2vsum / i - d2avg * d2avg);
  d3std = sqrt(d3vsum / i - d3avg * d3avg);

  printf(
    "\n"
    "ZuTime cnt: % 10d act: %12.10f avg: %12.10f std: %12.10f\n"
    "GSTAFT cnt: % 10d act: %12.10f avg: %12.10f std: %12.10f\n"
    "ZuTime - GSTAFT skew                     avg: %12.10f std: %12.10f\n",
    i, double(o1.as_ldouble() / i), d1avg, d1std, i,
    double(o2.as_ldouble() / i), d2avg, d2std, d3avg, d3std
  );
#endif

#if 0
  long j = d_.julian();
  int s = d_.seconds();
  int i;

  for (i = 0; i < 35; i++) {
    int y,m,da,h,mi,se;

    ZuDateTime d(j + i, s);

    puts(d.iso(-timezone));

    d.ymd(y, m, da, -timezone);
    d.hms(h, mi, se, -timezone);

    ZuDateTime d2(y, m, da, h, mi, se, -timezone);

    puts(ZuCArray<32>() << d2.iso(-timezone));

    // struct tm t;
    // char buf[128];

    // strftime(buf, 128, "%Y-%m-%dT%H:%M:%S %Z", d.tm(&t, -timezone));
    // puts(buf);
  }
#endif

  //   {
  //     ZuDateTime mabbit;
  //     mabbit = 20091204;
  //     ZuDateTime dmabbit{Zm::now()};
  //     printf("Mabbit D Time: %f\n", dmabbit.dtime());
  //     printf("Mabbit Time: %d\n", mabbit.yyyymmdd());
  //   }
}
