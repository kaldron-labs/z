//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// ZmLock debugging

#include <zlib/ZmLock.hh>

#ifdef ZmLock_DEBUG
#include <zlib/ZmBackTracer.hh>
#include <zlib/ZmSingleton.hh>

class ZmLock_Debug_ {
public:
  ZmLock_Debug_() : m_enabled(0) { }

  void enable() { m_enabled = 1; }
  void disable() { m_enabled = 0; }
  void capture(unsigned skip = 1) {
    if (m_enabled) m_tracer.capture(skip);
  }
  ZmBackTracer<64> *tracer() { return &m_tracer; }

private:
  ZmAtomic<uint32_t>	m_enabled;
  ZmBackTracer<64>	m_tracer;
};

static ZmLock_Debug_ *instance()
{
  return
    ZmSingleton<ZmLock_Debug_,
      ZmSingletonCleanup<ZmCleanup::Platform>>::instance();
}

void ZmLock_Debug::enable()
{
  instance()->enable();
}
void ZmLock_Debug::disable()
{
  instance()->disable();
}
void ZmLock_Debug::capture(unsigned skip)
{
  instance()->capture(skip + 1);
}
ZmBackTracer<64> *ZmLock_Debug::tracer()
{
  return instance()->tracer();
}
#endif
