//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z http library - hub lifecycle coordinator

#ifndef ZhttpHubs_HH
#define ZhttpHubs_HH

#ifndef ZhttpLib_HH
#include <zlib/ZhttpLib.hh>
#endif

#include <zlib/ZtArray.hh>
#include <zlib/ZmBlock.hh>
#include <zlib/ZmFn.hh>

namespace Zhttp {

namespace Hubs_ {

template <typename Hub, typename = void>
struct HasStopAccepting : public ZuFalse { };
template <typename Hub>
struct HasStopAccepting<Hub,
  decltype(ZuDeclVal<Hub &>().stopAccepting(), void())> : public ZuTrue { };

using DoneFn = ZmFn<void(bool), ZmFnHeapID<"Zhttp.Hubs.Done">>;

struct Entry {
  using StartFn = void (*)(void *, DoneFn);
  using StopFn = void (*)(void *, DoneFn);
  using FinalFn = void (*)(void *);
  using StopAcceptingFn = void (*)(void *);

  void			*ptr = nullptr;
  StartFn		start = nullptr;
  StopFn		stop = nullptr;
  FinalFn		final = nullptr;
  StopAcceptingFn	stopAccepting = nullptr;
  bool			started = false;
};

} // namespace Hubs_

class Hubs {
public:
  struct State {
    enum T : int8_t {
      Empty,
      Ready,
      Starting,
      Running,
      Stopping,
      Stopped,
      Final,
      Failed
    };
  };

  using DoneFn = Hubs_::DoneFn;
  using Entries =
    ZtArray<Hubs_::Entry, ZtArrayHeapID<"Zhttp.Hubs">>;
  using DoneFns =
    ZtArray<DoneFn, ZtArrayHeapID<"Zhttp.Hubs.DoneFns">>;

  State::T state() const { return m_state; }
  unsigned count() const { return m_entries.length(); }

  template <typename Hub, typename ...Args>
  bool init(Hub &hub, Args &&...args) {
    switch (m_state) {
      case State::Empty:
      case State::Ready:
	break;
      default:
	return false;
    }
    if (!hub.init(ZuFwd<Args>(args)...)) {
      final_();
      m_state = State::Failed;
      return false;
    }
    m_entries.push(Hubs_::Entry{
      .ptr = &hub,
      .start = [](void *ptr, DoneFn done) {
	static_cast<Hub *>(ptr)->start(
	  [done = ZuMv(done)](bool ok) mutable { done(ok); });
      },
      .stop = [](void *ptr, DoneFn done) {
	static_cast<Hub *>(ptr)->stop(
	  [done = ZuMv(done)](bool ok) mutable { done(ok); });
      },
      .final = [](void *ptr) {
	static_cast<Hub *>(ptr)->final();
      },
      .stopAccepting = [](void *ptr) {
	if constexpr (Hubs_::HasStopAccepting<Hub>{})
	  static_cast<Hub *>(ptr)->stopAccepting();
      }
    });
    m_state = State::Ready;
    return true;
  }

  bool start() {
    return ZmBlock<bool>{}(
      [this](auto wake) { start(DoneFn{ZuMv(wake)}); });
  }

  template <typename Done>
  void start(Done &&done) {
    start_(DoneFn{ZuFwd<Done>(done)});
  }

  bool stop() {
    return ZmBlock<bool>{}(
      [this](auto wake) { stop(DoneFn{ZuMv(wake)}); });
  }

  template <typename Done>
  void stop(Done &&done) {
    stop_(DoneFn{ZuFwd<Done>(done)});
  }

  void final() {
    if (m_state == State::Running || m_state == State::Starting ||
	m_state == State::Stopping)
      (void)stop();
    switch (m_state) {
      case State::Ready:
      case State::Failed:
      case State::Stopped:
	if (m_entries) final_();
	m_state = State::Final;
	break;
      default:
	break;
    }
  }

private:
  void start_(DoneFn done) {
    switch (m_state) {
      case State::Running:
	done(true);
	return;
      case State::Starting:
	m_startFns.push(ZuMv(done));
	return;
      case State::Stopping:
	m_restartPending = true;
	m_startFns.push(ZuMv(done));
	return;
      case State::Ready:
      case State::Stopped:
	break;
      case State::Empty:
	m_state = State::Failed;
	done(false);
	return;
      default:
	done(false);
	return;
    }
    if (!m_entries) {
      m_state = State::Failed;
      done(false);
      return;
    }
    m_startFns.push(ZuMv(done));
    m_stopPending = false;
    m_startFailed = false;
    m_state = State::Starting;
    m_next = 0;
    startNext_();
  }

  void startNext_() {
    if (m_stopPending || m_next >= m_entries.length()) {
      if (!m_stopPending) {
	m_state = State::Running;
	completeStarts_(true);
	return;
      }
      m_state = State::Stopping;
      stopBegin_();
      return;
    }
    unsigned i = m_next++;
    auto &entry = m_entries[i];
    entry.start(entry.ptr, DoneFn{this, [i](Hubs *hubs, bool ok) {
      hubs->started_(i, ok);
    }});
  }

  void started_(unsigned i, bool ok) {
    if (m_state != State::Starting) return;
    if (ok) {
      m_entries[i].started = true;
      startNext_();
      return;
    }
    m_startFailed = true;
    m_state = State::Stopping;
    stopBegin_();
  }

  void stop_(DoneFn done) {
    switch (m_state) {
      case State::Ready:
	m_state = State::Stopped;
	done(true);
	return;
      case State::Starting:
	m_stopPending = true;
	m_stopFns.push(ZuMv(done));
	return;
      case State::Running:
	m_stopFns.push(ZuMv(done));
	m_state = State::Stopping;
	stopBegin_();
	return;
      case State::Stopping:
	m_stopFns.push(ZuMv(done));
	return;
      case State::Stopped:
	done(true);
	return;
      default:
	done(false);
	return;
    }
  }

  void stopBegin_() {
    m_stopOK = true;
    m_next = m_entries.length();
    for (auto &entry: m_entries)
      if (entry.started) entry.stopAccepting(entry.ptr);
    stopNext_();
  }

  void stopNext_() {
    while (m_next) {
      auto &entry = m_entries[--m_next];
      if (!entry.started) continue;
      entry.stop(entry.ptr,
	DoneFn{this, [](Hubs *hubs, bool ok) {
	  hubs->stopped_(ok);
	}});
      return;
    }
    bool failed = m_startFailed;
    bool restart = m_restartPending && !failed;
    if (failed) final_();
    m_state = failed ? State::Failed : State::Stopped;
    if (m_stopPending || failed) completeStarts_(false);
    completeStops_(m_stopOK);
    if (restart) {
      m_restartPending = false;
      m_stopPending = false;
      m_state = State::Starting;
      m_next = 0;
      startNext_();
    }
  }

  void stopped_(bool ok) {
    if (m_state != State::Stopping) return;
    if (!ok) m_stopOK = false;
    m_entries[m_next].started = false;
    stopNext_();
  }

  void completeStarts_(bool ok) {
    auto fns = ZuMv(m_startFns);
    m_startFns.init_();
    for (auto &fn: fns) {
      fn(ok);
      fn = {};
    }
  }

  void completeStops_(bool ok) {
    auto fns = ZuMv(m_stopFns);
    m_stopFns.init_();
    for (auto &fn: fns) {
      fn(ok);
      fn = {};
    }
  }

  void final_() {
    for (unsigned i = m_entries.length(); i; --i)
      m_entries[i - 1].final(m_entries[i - 1].ptr);
    m_entries.length(0);
  }

  Entries	m_entries;
  DoneFns	m_startFns;
  DoneFns	m_stopFns;
  State::T	m_state = State::Empty;
  unsigned	m_next = 0;
  bool		m_stopPending = false;
  bool		m_restartPending = false;
  bool		m_startFailed = false;
  bool		m_stopOK = true;
};

} // namespace Zhttp

#endif /* ZhttpHubs_HH */
