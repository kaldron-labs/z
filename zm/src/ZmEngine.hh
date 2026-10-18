//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// generic engine with start, stop control

#ifndef ZmEngine_HH
#define ZmEngine_HH

#ifndef ZmLib_HH
#include <zlib/ZmLib.hh>
#endif

#include <zlib/ZuDerive.hh>

#include <zlib/ZmFn.hh>
#include <zlib/ZmQueue.hh>
#include <zlib/ZmRWLock.hh>
#include <zlib/ZmBlock.hh>

namespace ZmEngineState {
  using T = int8_t;
  enum {
    Stopped = 0, Starting, Running, Stopping,
    StartPending,	// started while stopping
    StopPending,	// stopped while starting
    N
  };
}

// CRTP - implementation must conform to the following interface:
#if 0
struct Impl : public ZmEngine<Impl> {
  void start_(); // start engine - impl should eventually call started(ok)
  void stop_(); // stop engine - impl should eventually call stopped(ok)

  // optional functions

  void stateChanged();	// state change notification

  template <typename L>
  bool spawn(L);	// spawn control thread - returns true if successful
  void wake();		// wake-up control thread, have it evaluate stopped()
			// and exit if true
};
#endif

template <typename Impl>
class ZmEngine {
  auto engineImpl_() const { return static_cast<const Impl *>(this); }
  auto engineImpl_() { return static_cast<Impl *>(this); }

private:
  using Lock = ZmPRWLock;
  using Guard = ZmGuard<Lock>;
  using ReadGuard = ZmReadGuard<Lock>;

  struct HeapID : public ZuStringT<"ZmEngine"> { };
  using CtrlFn = ZmFn<void(bool), ZmFnHeapID<"ZmEngine.CtrlFn">>;
  ZmQueueDerive(CtrlFnRing, CtrlFn, ZmQueueHeapID_<HeapID>);

public:
  template <typename L>
  bool lock(int state, L &&l) {
    Guard guard(m_lock);
    if (m_state != state) {
      std::cerr << (ZuCArray<120>{}
	<< "ZmEngine::lock() m_state=" << int(m_state)
	<< " state=" << state << '\n') << std::flush;
      return false;
    }
    return ZuFwd<L>(l)();
  }

  // Callbacks run unlocked. A queued opposite operation is already reflected
  // in state() before callbacks run; only Stopped permits final/destruction.
  // A superseded opposite request completes when the current operation
  // settles, with whether its requested state was attained.
  void start(CtrlFn);	// async
  // Retains stopFn until the implementation completes via stopped(ok).
  // Stop is best effort: completion leaves Stopped even when ok is false.
  // Only a separately queued start advances to Starting and invokes start_.
  void stop(CtrlFn);

  bool start();		// sync
  bool stop();

  int state() const { return m_state; }
  bool running() const {
    using namespace ZmEngineState;
    switch (m_state) {
      case Starting:
      case Running:
	return true;
    }
    return false;
  }
  bool stopping() const {
    using namespace ZmEngineState;
    switch (m_state) {
      case Stopping:
      case StartPending:
	return true;
    }
    return false;
  }

  // Control-thread poll used by wake(); do not poll concurrently from a
  // second control thread. Completion callbacks may reenter start/stop.
  bool stopped();

protected:
  void started(bool ok);
  void stopped(bool ok);

  void stateChanged() { }			// optional

  template <typename L>
  bool spawn(L &&l) { ZuFwd<L>(l)(); return true; } // default
  void wake() { stopped(); }			// default

private:
  Lock			m_lock;
    CtrlFnRing		  m_startFn, m_stopFn;
    ZmAtomic<int>	  m_state = ZmEngineState::Stopped;
};

template <typename Impl>
inline void ZmEngine<Impl>::start(CtrlFn startFn)
{
  using namespace ZmEngineState;
  bool ok;

  {
    Guard guard(m_lock);

    if (m_state == Running) { // idempotent
      guard.unlock();
      startFn(true);
      return;
    }

    if (startFn) m_startFn.push(ZuMv(startFn));

    switch (m_state.load_()) {
      case Stopped:
	m_state = Starting;
	break;
      case Stopping:
	m_state = StartPending;
	guard.unlock();
	engineImpl_()->stateChanged();
	return;
      case StopPending:
	m_state = Starting;
	guard.unlock();
	engineImpl_()->stateChanged();
	return;
      default:
	return; // Starting || Running || StartPending
    }
  }

  // The hook may complete inline and its callback may release the owner.
  // Publish Starting before invoking it; successful spawn needs no owner
  // access after returning from that callback.
  engineImpl_()->stateChanged();
  ok = engineImpl_()->spawn([this]() { engineImpl_()->start_(); });
  if (!ok) started(false);
}
template <typename Impl>
inline bool ZmEngine<Impl>::start()
{
  return ZmBlock<bool>{}([this](auto wake) { this->start(ZuMv(wake)); });
}
template <typename Impl>
inline void ZmEngine<Impl>::started(bool ok)
{
  using namespace ZmEngineState;
  Guard guard(m_lock);
  bool stop = false;
  auto startFn = ZuMv(m_startFn);
  m_startFn.clean();
  CtrlFnRing stopFn;
  switch (m_state.load_()) {
    case Starting:
      m_state = ok ? Running : Stopped;
      // A newer start can cancel StopPending. Its stop waiters still need
      // the result of that cancellation once startup has actually settled.
      stopFn = ZuMv(m_stopFn);
      m_stopFn.clean();
      break;
    case StopPending:
      if (ok) {
	// Reserve the queued operation before callbacks can reenter. It still
	// needs the owner, so this completion must not expose Running/Stopped.
	m_state = Stopping;
	stop = true;
      } else {
	// Failed startup has already rolled back. Complete both queues from
	// local storage; a terminal callback can release the owner.
	m_state = Stopped;
	stopFn = ZuMv(m_stopFn);
	m_stopFn.clean();
      }
      break;
  }
  guard.unlock();
  engineImpl_()->stateChanged();
  while (auto fn = startFn.shift()) fn(ok);
  if (stop) {
    engineImpl_()->wake();
    return;
  }
  while (auto fn = stopFn.shift()) fn(!ok);
}

template <typename Impl>
inline void ZmEngine<Impl>::stop(CtrlFn stopFn)
{
  using namespace ZmEngineState;

  {
    Guard guard(m_lock);

    if (m_state == Stopped) { // idempotent
      guard.unlock();
      stopFn(true);
      return;
    }

    if (stopFn) m_stopFn.push(ZuMv(stopFn));

    switch (m_state.load_()) {
      case Running:
	m_state = Stopping;
	break;
      case Starting:
	m_state = StopPending;
	guard.unlock();
	engineImpl_()->stateChanged();
	return;
      case StartPending:
	m_state = Stopping;
	guard.unlock();
	engineImpl_()->stateChanged();
	return;
      default:
	return; // Stopping || Stopped || StopPending
    }
  }

  engineImpl_()->stateChanged();

  engineImpl_()->wake();
}
template <typename Impl>
inline bool ZmEngine<Impl>::stop()
{
  return ZmBlock<bool>{}([this](auto wake) { this->stop(ZuMv(wake)); });
}
template <typename Impl>
inline bool ZmEngine<Impl>::stopped()
{
  using namespace ZmEngineState;
  switch (m_state) {
    case Stopping:
    case StartPending:
      engineImpl_()->stop_();
      return true;
  }
  return false;
}
template <typename Impl>
inline void ZmEngine<Impl>::stopped(bool ok)
{
  using namespace ZmEngineState;
  Guard guard(m_lock);
  bool start = false;
  auto stopFn = ZuMv(m_stopFn);
  m_stopFn.clean();
  CtrlFnRing startFn;
  switch (m_state.load_()) {
    case Stopping:
      m_state = Stopped;
      // A newer stop can cancel StartPending without losing its waiters.
      startFn = ZuMv(m_startFn);
      m_startFn.clean();
      break;
    case StartPending:
      // Stop always leaves resources stopped, including on failure. A queued
      // start needs a fresh hook; callbacks cannot finalize its owner yet.
      m_state = Starting;
      start = true;
      break;
  }
  guard.unlock();
  engineImpl_()->stateChanged();
  while (auto fn = stopFn.shift()) fn(ok);
  if (start) {
    // As in start(), successful spawn may complete inline and release us.
    if (!engineImpl_()->spawn([this]() { engineImpl_()->start_(); })) started(false);
    return;
  }
  while (auto fn = startFn.shift()) fn(false);
}

#endif /* ZmEngine_HH */
