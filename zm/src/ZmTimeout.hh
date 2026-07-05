//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// timeout class with exponential backoff

#ifndef ZmTimeout_HH
#define ZmTimeout_HH

#ifndef ZmLib_HH
#include <zlib/ZmLib.hh>
#endif

#include <zlib/ZmFn.hh>
#include <zlib/ZmScheduler.hh>
#include <zlib/ZmBackoff.hh>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:4251)
#endif

class ZmAPI ZmTimeout {
public:
  using Fn = ZmFn<void(), ZmFnHeapID<"ZmTimeout.Fn">>;

  ZmTimeout(
    ZmScheduler *scheduler,
    const ZmBackoff &backoff, int maxCount) // -ve or 0 - infinite
  :
    m_scheduler(scheduler), m_backoff(backoff),
    m_maxCount(maxCount), m_count(0) { }

  ZmTimeout(const ZmTimeout &) = delete;
  ZmTimeout &operator =(const ZmTimeout &) = delete;

  void start(Fn retryFn, Fn finalFn = {}) {
    ZmGuard<ZmLock> guard(m_lock);

    m_retryFn = ZuMv(retryFn);
    m_finalFn = ZuMv(finalFn);
    start_();
  }

private:
  void start_() {
    m_count = 0;
    m_interval = m_backoff.initial();
    m_scheduler->add(
      &m_timer, Zm::now() + m_interval, ZmScheduler::Update,
      [this](auto &&arm) { return arm([this]() { work(); }); });
  }

public:
  void reset() {
    ZmGuard<ZmLock> guard(m_lock);

    m_scheduler->del(&m_timer);
    start_();
  }

  void stop() {
    ZmGuard<ZmLock> guard(m_lock);

    m_scheduler->del(&m_timer);
    m_retryFn = Fn();
    m_finalFn = Fn();
  }

  void work() {
    ZmGuard<ZmLock> guard(m_lock);

    m_count++;
    if (m_maxCount <= 0 || m_count < m_maxCount) {
      if (m_retryFn) m_retryFn();
      m_interval = m_backoff.backoff(m_interval);
      m_scheduler->add(
	&m_timer, Zm::now() + m_interval, ZmScheduler::Update,
	[this](auto &&arm) { return arm([this]() { work(); }); });
    } else {
      m_retryFn = Fn();
      m_finalFn();
      m_finalFn = Fn();
    }
  }

private:
  ZmScheduler		*m_scheduler;
  ZmLock		m_lock;
    Fn			m_retryFn;
    Fn			m_finalFn;
    ZmBackoff		m_backoff;
    int			m_maxCount;
    int			m_count;
    ZuTime		m_interval;
    ZmScheduler::Timer	m_timer;
};

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#endif /* ZmTimeout_HH */
