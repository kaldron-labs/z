//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// time interval statistics

#ifndef ZmTimeInterval_HH
#define ZmTimeInterval_HH

#ifndef ZmLib_HH
#include <zlib/ZmLib.hh>
#endif

#include <zlib/ZuBox.hh>
#include <zlib/ZuPrint.hh>

#include <zlib/ZmLock.hh>
#include <zlib/ZuTime.hh>

#include <stdio.h>
#include <limits.h>

template <typename Lock>
class ZmTimeInterval {
  using Guard = ZmGuard<Lock>;
  using ReadGuard = ZmReadGuard<Lock>;

public:
  ZmTimeInterval() { }

  ZmTimeInterval(const ZmTimeInterval &) = delete;
  ZmTimeInterval &operator =(const ZmTimeInterval &) = delete;

  void add(ZuTime t) {
    Guard guard(m_lock);
    if (t < m_min) m_min = t;
    if (t > m_max) m_max = t;
    m_total += t;
    m_count++;
  }

  template <typename S> void print(S &s) const {
    ZuTime min, max, total;
    ZuBox<double> mean;
    ZuBox<unsigned> count;

    stats(min, max, total, mean, count);

    s << "min=" << min.interval()
      << " max=" << max.interval()
      << " total=" << total.interval()
      << " mean=" << mean.fmt<ZuFmt::FP<9, '0'>>()
      << " count=" << count;
  }

  void stats(
      ZuTime &min, ZuTime &max, ZuTime &total,
      double &mean, unsigned &count) const {
    ReadGuard guard(m_lock);
    if (ZuUnlikely(!m_count)) {
      min = ZuTime{}, max = ZuTime{}, total = ZuTime{},
      mean = 0.0, count = 0;
    } else {
      min = m_min, max = m_max, total = m_total,
      mean = m_total.as_fp() / m_count, count = m_count;
    }
  }
  friend ZuPrintFn ZuPrintType(ZmTimeInterval *);

private:
  Lock		m_lock;
    ZuTime	  m_min{ZuCmp<time_t>::maximum(), 0};
    ZuTime	  m_max{ZuCmp<time_t>::minimum(), 0};
    ZuTime	  m_total{0, 0};
    unsigned	  m_count = 0;
};

#endif /* ZmTimeInterval_HH */
