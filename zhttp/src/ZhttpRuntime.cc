//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z http library - process shutdown and diagnostic wait coordination

#include <zlib/ZhttpRuntime.hh>

#include <zlib/ZmTime.hh>

namespace Zhttp {

static Runtime *runtime_;

void Runtime::signal_(int)
{
  if (runtime_) runtime_->stop();
}

bool Runtime::init()
{
  if (m_initialized || runtime_) return false;
  runtime_ = this;
#ifndef _WIN32
  struct sigaction action{};
  action.sa_handler = signal_;
  sigemptyset(&action.sa_mask);
  if (sigaction(SIGINT, &action, &m_oldInt)) {
    runtime_ = nullptr;
    return false;
  }
  if (sigaction(SIGTERM, &action, &m_oldTerm)) {
    sigaction(SIGINT, &m_oldInt, nullptr);
    runtime_ = nullptr;
    return false;
  }
#else
  m_oldInt = signal(SIGINT, signal_);
  if (m_oldInt == SIG_ERR) {
    runtime_ = nullptr;
    return false;
  }
  m_oldTerm = signal(SIGTERM, signal_);
  if (m_oldTerm == SIG_ERR) {
    signal(SIGINT, m_oldInt);
    runtime_ = nullptr;
    return false;
  }
#endif
  m_initialized = true;
  return true;
}

void Runtime::add(unsigned seconds, DiagnosticFn fn)
{
  if (seconds && fn)
    m_intervals.push(Interval{ZuMv(fn), seconds, 0});
}

void Runtime::wait()
{
  (void)wait(0);
}

bool Runtime::wait(unsigned timeout)
{
  if (!m_intervals.length() && !timeout) {
    m_done.wait();
    return true;
  }
  unsigned elapsed = 0;
  for (;;) {
    unsigned step = timeout ? timeout - elapsed : 0;
    for (unsigned i = 0; i < m_intervals.length(); ++i) {
      auto &interval = m_intervals[i];
      unsigned left = interval.period - interval.elapsed;
      if (!step || left < step) step = left;
    }
    if (!m_done.timedwait(Zm::now(step))) return true;
    elapsed += step;
    for (unsigned i = 0; i < m_intervals.length(); ++i) {
      auto &interval = m_intervals[i];
      interval.elapsed += step;
      if (interval.elapsed < interval.period) continue;
      interval.elapsed = 0;
      interval.fn();
    }
    if (timeout && elapsed >= timeout)
      return m_done.trywait() == 0;
  }
}

void Runtime::final()
{
  if (!m_initialized) return;
#ifndef _WIN32
  sigaction(SIGINT, &m_oldInt, nullptr);
  sigaction(SIGTERM, &m_oldTerm, nullptr);
#else
  signal(SIGINT, m_oldInt);
  signal(SIGTERM, m_oldTerm);
#endif
  if (runtime_ == this) runtime_ = nullptr;
  m_initialized = false;
  m_intervals.length(0);
}

} // namespace Zhttp
