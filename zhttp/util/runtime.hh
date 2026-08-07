//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Local process-wait mechanics shared by the zhttp utilities.

#ifndef zhttp_runtime_HH
#define zhttp_runtime_HH

#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmTime.hh>

namespace ZhttpUtil {

class Runtime {
public:
  Runtime(const Runtime &) = delete;
  Runtime &operator =(const Runtime &) = delete;

  static void post() { instance_().m_done.post(); }
  static void wait() { instance_().m_done.wait(); }
  static bool wait(unsigned seconds) {
    return instance_().m_done.timedwait(Zm::now(seconds)) == 0;
  }
  static bool trywait() { return instance_().m_done.trywait() == 0; }

private:
  Runtime() = default;

  static Runtime &instance_() {
    static Runtime runtime;
    return runtime;
  }

  ZmSemaphore m_done;
};

} // namespace ZhttpUtil

#endif /* zhttp_runtime_HH */
