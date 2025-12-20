//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// utility class to capture a ring of recent backtraces and print them on demand

#ifndef ZmBackTracer_HH
#define ZmBackTracer_HH

#ifndef ZmLib_HH
#include <zlib/ZmLib.hh>
#endif

#include <zlib/ZuTuple.hh>
#include <zlib/ZuUnion.hh>

#include <zlib/ZmBackTrace.hh>
#include <zlib/ZmThread.hh>
#include <zlib/ZmPLock.hh>

template <unsigned N = 64> class ZmBackTracer {
public:
  ZmBackTracer() { }
  ~ZmBackTracer() { }

private:
  using Data = ZuTuple<ZmThreadID, ZmThreadName, ZmBackTrace>;
  using Capture = ZuUnion<void, Data>;

  using Lock = ZmPLock;
  using Guard = ZmGuard<Lock>;
  using ReadGuard = ZmReadGuard<Lock>;

public:
  void capture(unsigned skip = 0) {
    Guard guard(m_lock);
    unsigned i = m_offset;
    m_offset = (i + 1) & 63;
    Data *data = new (m_captures[i].template new_<1>()) Data();
    ZmThreadContext *self = ZmSelf();
    data->p<0>() = self->tid();
    data->p<1>() = self->name();
    data->p<2>().capture(skip + 1);
  }

  template <typename S> void dump(S &s) const {
    ReadGuard guard(m_lock);
    bool first = true;
    for (unsigned i = 0; i < N; i++) {
      unsigned j = (m_offset + (N - 1) - i) % N;
      if (m_captures[j].is<Data>()) {
	const auto &data = m_captures[j].template p<Data>();
	if (!first) s << "---\n";
	first = false;
	s << data.template p<1>()
	  << " (TID " << data.template p<0>()
	  << ")\n" << data.template p<2>();
      }
    }
  }

private:
  ZmPLock		m_lock;
  ZuBox0(unsigned)	  m_offset;
  Capture		  m_captures[64];
};

#endif /* ZmBackTracer_HH */
