//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// exponential backoff with random perturbation

#ifndef ZmBackoff_HH
#define ZmBackoff_HH

#ifndef ZmLib_HH
#include <zlib/ZmLib.hh>
#endif

#include <zlib/ZuTime.hh>
#include <zlib/ZmRandom.hh>

// ZmBackoff(minimum, maximum, backoff multiplier, random offset)

class ZmAPI ZmBackoff {
public:
  ZmBackoff(
      ZuTime minimum, ZuTime maximum,
      double backoff, double random) :
    m_min(ZuMv(minimum)), m_max(ZuMv(maximum)),
    m_backoff(backoff), m_random(random) { }

  ZuTime minimum() { return(m_min); }
  ZuTime maximum() { return(m_max); }

  ZuTime initial() {
    double d = m_min.as_fp();
    if (m_random) d += ZmRand::rand(m_random);
    return ZuTime{d};
  }

  ZuTime backoff(const ZuTime &interval) {
    if (interval >= m_max) return m_max;
    double d = interval.as_fp();
    d *= m_backoff;
    if (m_random) d += ZmRand::rand(m_random);
    auto max = m_max.as_fp();
    if (d >= max) d = max;
    return ZuTime{d};
  }

private:
  const ZuTime	m_min;
  const ZuTime	m_max;
  const double	m_backoff;
  const double	m_random;
};

#endif /* ZmBackoff_HH */
