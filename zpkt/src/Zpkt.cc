//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <errno.h>
#include <net/if.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <poll.h>
#include <time.h>

#include <zlib/ZuBox.hh>
#include <zlib/ZuArray.hh>
#include <zlib/ZuTime.hh>

#include <zlib/ZmAssert.hh>
#include <zlib/ZmBlock.hh>
#include <zlib/ZmLock.hh>
#include <zlib/ZmGuard.hh>

#include <zlib/ZtArray.hh>

#include <zlib/ZePlatform.hh>

#include <zlib/ZiLog.hh>
#include <zlib/ZiMultiplex.hh>

#include <zlib/Zpkt.hh>
#include <zlib/ZpktNif.hh>
namespace Zpkt {

template <typename Fn>
void DeviceEngine::control(Fn fn)
{
  // Bind per-run callbacks to the engine transition, then run backend work
  // and application completions after releasing the device control lock.
  Work work;
  {
    ZmGuard guard(m_controlLock);
    m_work = &work;
    fn(work);
    m_work = nullptr;
  }
  work.retired = {};
  while (work.completions.length()) {
    auto done = work.completions.shift();
    done.fn(done.ok);
  }
  if (work.start) work.start();
  if (work.wake) static_cast<Nif *>(this)->wake();
}

auto DeviceEngine::completion(StartFn fn)
{
  return [this, fn = ZuMv(fn)](bool ok) mutable {
    if (fn) m_work->completions.push(Completion{ZuMv(fn), ok});
  };
}

void DeviceEngine::start(RunFns run, StartFn done)
{
  control([this, &run, &done](Work &work) {
    int state_ = state();
    if (state_ == ZmEngineState::Stopped || state_ == ZmEngineState::Stopping) {
      work.retired = ZuMv(m_nextRun);
      m_nextRun = ZuMv(run);
    }
    Base::start(completion(ZuMv(done)));
  });
}

bool DeviceEngine::stop()
{
  return ZmBlock<bool>{}([this](auto done) { stop(ZuMv(done)); });
}

void DeviceEngine::stop(StartFn done)
{
  control([this, &done](Work &) { Base::stop(completion(ZuMv(done))); });
}

void DeviceEngine::started(bool ok)
{
  control([this, ok](Work &) { Base::started(ok); });
}

void DeviceEngine::stopped(bool ok)
{
  control([this, ok](Work &work) {
    if (state() == ZmEngineState::Stopping) work.retired = ZuMv(m_nextRun);
    Base::stopped(ok);
  });
}

DeviceEngine::RunFns DeviceEngine::takeRunFns()
{
  ZmGuard guard(m_controlLock);
  return ZuMv(m_nextRun);
}

void DeviceEngine::start_() { static_cast<Nif *>(this)->start_(); }
void DeviceEngine::stop_() { static_cast<Nif *>(this)->stop_(); }

bool Nif::start(RecvFn receive, StopFn txStopped)
{
  return ZmBlock<bool>{}([this, &receive, &txStopped](auto wake) {
    start(ZuMv(receive), ZuMv(txStopped), ZuMv(wake));
  });
}

void Nif::start(RecvFn receive, StopFn txStopped, StartFn completion)
{
  m_everStarted = 1;
  Engine::start({ZuMv(receive), ZuMv(txStopped)}, ZuMv(completion));
}

ZuTime Nif::monotonicNow()
{
  timespec now{};
  clock_gettime(CLOCK_MONOTONIC, &now);
  return ZuTime{now};
}

void Nif::signalWake(int fd, const ZmAtomic<unsigned> &parking)
{
  if (!parking) return;
  uint64_t one = 1;
  while (::write(fd, &one, sizeof(one)) < 0) {
    if (errno == EINTR) continue;
    ZmAssert_(errno == EAGAIN);
    break;
  }
}

void Nif::drainWake(int fd)
{
  uint64_t value;
  while (::read(fd, &value, sizeof(value)) < 0) {
    if (errno == EINTR) continue;
    ZmAssert_(errno == EAGAIN);
    break;
  }
}

bool Nif::takeWake(ZmAtomic<uint64_t> &wakes)
{
  if (!wakes) return false;
  --wakes; // only the Rx owner consumes pulses
  return true;
}

} // namespace Zpkt
