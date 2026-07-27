//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z http library - process shutdown and diagnostic wait coordination

#ifndef ZhttpRuntime_HH
#define ZhttpRuntime_HH

#ifndef ZhttpLib_HH
#include <zlib/ZhttpLib.hh>
#endif

#include <signal.h>

#include <zlib/ZmFn.hh>
#include <zlib/ZmSemaphore.hh>

#include <zlib/ZtArray.hh>

namespace Zhttp {

using DiagnosticFn =
  ZmFn<void(), ZmFnHeapID<"Zhttp.Runtime.Diagnostic">>;

class ZhttpAPI Runtime {
  struct Interval {
    DiagnosticFn	fn;
    unsigned	period = 0;
    unsigned	elapsed = 0;
  };
  using Intervals =
    ZtArray<Interval, ZtArrayHeapID<"Zhttp.Runtime.Intervals">>;

public:
  Runtime() = default;
  ~Runtime() { final(); }

  Runtime(const Runtime &) = delete;
  Runtime &operator =(const Runtime &) = delete;

  bool init();
  void add(unsigned seconds, DiagnosticFn);
  void wait();
  bool wait(unsigned timeout);
  void stop() { m_done.post(); }
  void final();

private:
  static void signal_(int);

  Intervals	m_intervals;
  ZmSemaphore	m_done;
#ifndef _WIN32
  struct sigaction m_oldInt{};
  struct sigaction m_oldTerm{};
#else
  using Handler = void (*)(int);
  Handler	m_oldInt = nullptr;
  Handler	m_oldTerm = nullptr;
#endif
  bool		m_initialized = false;
};

} // namespace Zhttp

#endif /* ZhttpRuntime_HH */
