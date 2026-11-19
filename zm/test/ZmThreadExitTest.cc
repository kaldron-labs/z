//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// A library worker must retain its TLS keys until its exit destructors drain.

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmSingleton.hh>
#include <zlib/ZmThread.hh>

using namespace ZuTestUtil;

namespace ZmThreadExitTest_ {

class Worker {
public:
  Worker() {
    m_started = !m_thread.run([this]() {
      m_ready.post();
      m_stop.wait();
    });
    if (m_started) m_ready.wait();
  }
  ~Worker() {
    if (m_started) {
      m_stop.post();
      m_thread.join();
    }
  }
  bool started() const { return m_started; }

  static Worker *instance() {
    return ZmSingleton<Worker, ZmSingletonCleanup<ZmCleanup::Library>>::instance();
  }

private:
  ZmThread m_thread;
  ZmSemaphore m_ready;
  ZmSemaphore m_stop;
  bool m_started = false;
};

} // namespace ZmThreadExitTest_

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuCheck(ZmThreadExitTest_::Worker::instance()->started());
  // Deliberately leave the worker to its library cleanup, after application
  // globals have retired. Run under Valgrind to check the TLS exit ownership.
  return 0;
}
