//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZuDateTime.hh>

#include <zlib/ZmSpecific.hh>
#include <zlib/ZmTime.hh>

#include <zlib/ZtString.hh>
#include <zlib/ZtTimeZone.hh>

using namespace ZuTestUtil;

ZuDateTimePrintISO isoPrint(const ZuDateTime &d, int tzOffset = 0) {
  auto &fmt = ZmTLS<ZuDateTimeFmt::ISO, isoPrint>();
  fmt.tzOffset(tzOffset);
  return d.fmt(fmt);
}

ZuCArray<40> isoStr(const ZuDateTime &d, int tzOffset = 0) {
  return ZuCArray<40>{} << isoPrint(d, tzOffset);
}

void weekDate(ZuDateTime d, int year, int weekChk, int wkDayChk)
{
  ZuTestScope(weekDate);
  int week, wkDay;
  int days = d.days(year, 1, 1);
  d.ywd(days, week, wkDay);
  ZuCheck(week == weekChk);
  ZuCheck(wkDay == wkDayChk);
}

void weekDateSun(ZuDateTime d, int year, int weekChk, int wkDayChk)
{
  ZuTestScope(weekDateSun);
  int week, wkDay;
  int days = d.days(year, 1, 1);
  d.ywdSun(days, week, wkDay);
  ZuCheck(week == weekChk);
  ZuCheck(wkDay == wkDayChk);
}

void weekDateISO(
  ZuDateTime d, int year, int yearChk, int weekChk, int wkDayChk
) {
  ZuTestScope(weekDateISO);
  int yearISO, weekISO, wkDay;
  int days = d.days(year, 1, 1);
  d.ywdISO(year, days, yearISO, weekISO, wkDay);
  ZuCheck(yearISO == yearChk);
  ZuCheck(weekISO == weekChk);
  ZuCheck(wkDay == wkDayChk);
}

void strftimeChk(ZuDateTime d, const char *format, const char *chk)
{
  ZuTestScope(strftimeChk);
  ZtString<> s;
  s << d.strftime(format);
  ZuCheck(s == chk);
}

void testISOAndRoundTrip()
{
  ZuTestScope(testISOAndRoundTrip);

  ZuDateTime d(1998, 12, 1, 10, 30, 0);
  ZuCArray<40> s = isoStr(d);
  ZuDateTime e(s);
  ZuCheck(d == e);

  auto s1 = "2011-04-07T10:30:00+0800";
  auto d1 = ZuDateTime(ZuCSpan(s1));
  ZuCheck(isoStr(d1).length() > 0);

  auto s2 = "2011-04-07T10:30:00.0012345+08:00";
  auto d2 = ZuDateTime(ZuCSpan(s2));
  ZuCheck(isoStr(d2).length() > 0);
}

void testWeekDateCalcs()
{
  ZuTestScope(testWeekDateCalcs);

  ZuTestCall_("weekDate 20080106", weekDate, ZuDateTime(
      ZuDateTime::YYYYMMDD{20080106}, ZuDateTime::HHMMSS{0}), 2008, 0, 7);
  ZuTestCall_("weekDate 20080107", weekDate, ZuDateTime(
      ZuDateTime::YYYYMMDD{20080107}, ZuDateTime::HHMMSS{0}), 2008, 1, 1);
  ZuTestCall_("weekDateSun 20070106", weekDateSun, ZuDateTime(
      ZuDateTime::YYYYMMDD{20070106}, ZuDateTime::HHMMSS{0}), 2007, 0, 7);
  ZuTestCall_("weekDateSun 20070107", weekDateSun, ZuDateTime(
      ZuDateTime::YYYYMMDD{20070107}, ZuDateTime::HHMMSS{0}), 2007, 1, 1);

  {
    ZuDateTime d(ZuDateTime::YYYYMMDD{20071231}, ZuDateTime::HHMMSS{0});
    int year, month, day;
    d.ymd(year, month, day);
    ZuCheck(year == 2007);
    ZuCheck(month == 12);
    ZuCheck(day == 31);
    ZuTestCall_("weekDateISO 20071231", weekDateISO, d, year, 2007, 53, 1);
  }
  ZuTestCall_("weekDateISO 20070101", weekDateISO, ZuDateTime(
      ZuDateTime::YYYYMMDD{20070101},
      ZuDateTime::HHMMSS{0}), 2007, 2007, 1, 1);
  ZuTestCall_("weekDateISO 20100103", weekDateISO, ZuDateTime(
      ZuDateTime::YYYYMMDD{20100103},
      ZuDateTime::HHMMSS{0}), 2010, 2009, 53, 7);
  ZuTestCall_("weekDateISO 20110102", weekDateISO, ZuDateTime(
      ZuDateTime::YYYYMMDD{20110102},
      ZuDateTime::HHMMSS{0}), 2011, 2010, 52, 7);
  ZuTestCall_("weekDateISO 17520902", weekDateISO, ZuDateTime(
      ZuDateTime::YYYYMMDD{17520902},
      ZuDateTime::HHMMSS{0}), 1752, 1752, 36, 3);
  ZuTestCall_("weekDateISO 17520914", weekDateISO, ZuDateTime(
      ZuDateTime::YYYYMMDD{17520914},
      ZuDateTime::HHMMSS{0}), 1752, 1752, 36, 4);
  ZuTestCall_("weekDateISO 17521231", weekDateISO, ZuDateTime(
      ZuDateTime::YYYYMMDD{17521231},
      ZuDateTime::HHMMSS{0}), 1752, 1752, 51, 7);
}

void testStrftimeAndTZ()
{
  ZuTestScope(testStrftimeAndTZ);

  ZuTestCall(strftimeChk,
    ZuDateTime(ZuDateTime::YYYYMMDD{17520902}, ZuDateTime::HHMMSS{143000}),
    "%a %A %b %B %C %d %e %g %G %H %I %j %m %M %p %P %S %u %V %Y",
    "Wed Wednesday Sep September 17 02  2 52 1752 "
    "14 02 246 09 30 PM pm 00 3 36 1752"
  );

#ifdef _WIN32
  static const char *ukTZ = "GMT";
#else
  static const char *ukTZ = "GB";
#endif
  ZuDateTime winter{ZuDateTime::YYYYMMDD{20070107}, ZuDateTime::HHMMSS{0}};
  ZuDateTime summer{ZuDateTime::YYYYMMDD{20070707}, ZuDateTime::HHMMSS{0}};
  ZuDateTimeFmt::CSV fmt;
  auto winterOff = Zt::tzOffset(winter, ukTZ);
  auto summerOff = Zt::tzOffset(summer, ukTZ);
  fmt.tzOffset(winterOff);
  ZtString<> winterUK; winterUK << winter.fmt(fmt);
  fmt.tzOffset(summerOff);
  ZtString<> summerUK; summerUK << summer.fmt(fmt);
  ZuCheck(summerUK == "2007/07/07 01:00:00");
  ZuCheck(winterUK == "2007/01/07 00:00:00");
}

void testExtremesAndNow()
{
  ZuTestScope(testExtremesAndNow);

  ZuDateTime d = ZuDateTime(ZuDateTime::Julian{0}, 0, 0);
  ZuCheck(isoStr(d).length() > 0);
  d = ZuDateTime(d.as_time_t());
  ZuCheck(isoStr(d).length() > 0);

  ZuDateTime d1{Zm::now()};
  Zm::sleep(.01);
  ZuDateTime d2{Zm::now()};
  ZuCheck(d2 >= d1);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  Zt::tzset();
  ZuTestCall(testISOAndRoundTrip);
  ZuTestCall(testWeekDateCalcs);
  ZuTestCall(testStrftimeAndTZ);
  ZuTestCall(testExtremesAndNow);
  return 0;
}
