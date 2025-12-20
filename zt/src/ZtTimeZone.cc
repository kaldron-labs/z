//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

#include <time.h>

#include <zlib/ZuTuple.hh>

#include <zlib/ZmAssert.hh>
#include <zlib/ZmGuard.hh>
#include <zlib/ZmPLock.hh>
#include <zlib/ZmSingleton.hh>

#include <zlib/ZtTimeZone.hh>
#include <zlib/ZtPlatform.hh>

struct Zt_TzLock : public ZmPLock {
  static Zt_TzLock *instance() {
    return
      ZmSingleton<Zt_TzLock,
	ZmSingletonCleanup<ZmCleanup::Platform>>::instance();
  }
};

class Zt_TzGuard : public ZmGuard<ZmPLock> {
public:
  Zt_TzGuard(const char *tz);
  ~Zt_TzGuard();

private:
  char	*m_tz = nullptr;
  char	*m_oldTz = nullptr;
};

Zt_TzGuard::Zt_TzGuard(const char *tz) :
  ZmGuard<ZmPLock>{*Zt_TzLock::instance()}
{
  if (tz) {
    if (m_oldTz = ::getenv("TZ")) m_oldTz -= 3; // potentially non-portable

    m_tz = static_cast<char *>(malloc(strlen(tz) + 4));
    ZmAssert(m_tz);
    if (!m_tz) throw std::bad_alloc();
    strcpy(m_tz, "TZ=");
    strcpy(m_tz + 3, tz);

    Zt::putenv(m_tz);
  }

  Zt::tzset();
}

Zt_TzGuard::~Zt_TzGuard()
{
  if (m_tz) {
    if (m_oldTz)
      Zt::putenv(m_oldTz);
    else
      Zt::putenv("TZ=");
    free(m_tz);

    Zt::tzset();
  }
}

int Zt::tzOffset(ZuDateTime value, const char *tz)
{
  // 2-pass algorithm
  auto calc = [](const ZuDateTime &value) -> ZuTuple<int, bool> {
    int year, month, day, hour, minute, second;

    value.ymd(year, month, day);
    value.hms(hour, minute, second);

    if (year < 1900) return {int(-timezone), false};

    struct tm tm_ {
      .tm_sec = second,
      .tm_min = minute,
      .tm_hour = hour,
      .tm_mday = day,
      .tm_mon = month - 1,
      .tm_year = year - 1900,
      .tm_isdst = -1
    };

    time_t t = mktime(&tm_);

    if (ZuUnlikely(t == (time_t)-1 && tm_.tm_isdst < 0)) // mktime() error
      return {int(-timezone), false};

    time_t t_ = value.as_time_t();

    if (ZuUnlikely(ZuCmp<time_t>::null(t_)))
      return {int(-timezone), false};

    return {int(t_ - t), tm_.tm_isdst > 0};
  };

  Zt_TzGuard tzGuard(tz);

  auto [offset, dst] = calc(value);	// 1st pass - offset from local -> GMT
  value += offset - (dst ? 3600 : 0);	// adjust GMT to local (no DST)
  return calc(value).p<0>();		// 2nd pass (including DST)
}
