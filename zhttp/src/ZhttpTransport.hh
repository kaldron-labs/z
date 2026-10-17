//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z HTTP backend/channel contracts and shared lifecycle

#ifndef ZhttpTransport_HH
#define ZhttpTransport_HH

#ifndef ZhttpLib_HH
#include <zlib/ZhttpLib.hh>
#endif

#include <zlib/ZmAssert.hh>
#include <zlib/ZmEngine.hh>
#include <zlib/ZmFn.hh>
#include <zlib/ZtArray.hh>
#include <zlib/ZhttpCore.hh>
#include <zlib/ZhttpProfiles.hh>
#include <zlib/ZhttpConfig.hh>

namespace Zhttp {

class ServerCore;
class PoolCore;

struct PoolInit {
  const HubConfig &hub;
  const TCPConfig &tcp;
  const H2Config &tls;
  const QUICConfig &quic;
};

struct ProfileDesc {
  Transport::T transport;
  unsigned versions;
  bool (*server)(ServerCore &, const HubConfig &, unsigned);
  bool (*pool)(PoolCore &, const PoolInit &, unsigned);
};

// Small selection storage shared by endpoint cores. Registration is synchronous
// on the control thread; descriptors and factories have static lifetime.
class ZhttpAPI ProfileCore {
  template <typename, typename> friend struct Profiles_;

private:
  void profiles(unsigned);
  void add(const ProfileDesc &);

protected:
  using Descs = ZtArray<ProfileDesc, ZtArrayHeapID<"Zhttp.Profiles">>;

  ProfileCore() = default;
  ~ProfileCore() = default;
  const Descs &profiles() const { return m_profiles; }
  Config selected(const Config &) const;

private:
  Descs m_profiles;
};

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

class ZhttpAPI Hubs : public ZmEngine<Hubs> {
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
    return Engine::lock(ZmEngineState::Stopped, [this, &hub, &args...]() {
      if (!hub.init(ZuFwd<Args>(args)...)) {
	final_();
	return false;
      }
      add_(hub);
      return true;
    });
  }

  // Register an already initialized hub; its owner retains its lifetime.
  template <typename Hub>
  bool add(Hub &hub) {
    return Engine::lock(ZmEngineState::Stopped, [this, &hub]() {
      add_(hub);
      return true;
    });
  }

  void final();
  void stopAccepting();

private:
  friend Engine;

  template <typename Hub>
  void add_(Hub &hub) {
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
  }

  void start_();

  void startNext_();

  void started_(unsigned i, bool ok);

  void stop_();

  void stopNext_();

  void stopped_(bool ok);

  void final_();

  Entries	m_entries;
  unsigned	m_next = 0;
  bool		m_stopOK = true;
  bool		m_rollback = false;
};

} // namespace Zhttp

#endif /* ZhttpTransport_HH */
