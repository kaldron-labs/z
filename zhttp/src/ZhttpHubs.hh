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
#include <zlib/ZmEngine.hh>
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

class Hubs : public ZmEngine<Hubs> {
public:
  using Engine = ZmEngine<Hubs>;
  using DoneFn = Hubs_::DoneFn;
  using Entries =
    ZtArray<Hubs_::Entry, ZtArrayHeapID<"Zhttp.Hubs">>;
  using Engine::running;
  using Engine::start;
  using Engine::state;
  using Engine::stop;
  using Engine::stopping;
  unsigned count() const { return m_entries.length(); }

  template <typename Done>
  void start(Done &&done) {
    Engine::start([done = DoneFn{ZuFwd<Done>(done)}](bool ok) mutable {
      done(ok);
    });
  }
  template <typename Done>
  void stop(Done &&done) {
    Engine::stop([done = DoneFn{ZuFwd<Done>(done)}](bool ok) mutable {
      done(ok);
    });
  }

  template <typename Hub, typename ...Args>
  bool init(Hub &hub, Args &&...args) {
    return Engine::lock(ZmEngineState::Stopped, [&]() {
      if (!hub.init(ZuFwd<Args>(args)...)) {
	final_();
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
      return true;
    });
  }

  void final() {
    (void)Engine::stop();
    (void)Engine::lock(ZmEngineState::Stopped, [this]() {
      final_();
      return true;
    });
  }

private:
  friend Engine;

  void start_() {
    if (!m_entries) {
	Engine::started(false);
	return;
    }
    m_next = 0;
    startNext_();
  }

  void startNext_() {
    if (m_next >= m_entries.length()) {
      Engine::started(true);
      return;
    }
    unsigned i = m_next++;
    auto &entry = m_entries[i];
    entry.start(entry.ptr, DoneFn{this, [i](Hubs *hubs, bool ok) {
      hubs->started_(i, ok);
    }});
  }

  void started_(unsigned i, bool ok) {
    if (ok) {
      m_entries[i].started = true;
      startNext_();
      return;
    }
    m_rollback = true;
    m_stopOK = true;
    m_next = i;
    stopNext_();
  }

  void stop_() {
    m_rollback = false;
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
    if (m_rollback) {
      m_rollback = false;
      Engine::started(false);
    } else
      Engine::stopped(m_stopOK);
  }

  void stopped_(bool ok) {
    if (!ok) m_stopOK = false;
    m_entries[m_next].started = false;
    stopNext_();
  }

  void final_() {
    for (unsigned i = m_entries.length(); i; --i)
      m_entries[i - 1].final(m_entries[i - 1].ptr);
    m_entries.length(0);
  }

  Entries	m_entries;
  unsigned	m_next = 0;
  bool		m_stopOK = true;
  bool		m_rollback = false;
};

} // namespace Zhttp

#endif /* ZhttpHubs_HH */
