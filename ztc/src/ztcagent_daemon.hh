//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// OAuth/WSS agent transport

#ifndef ztcagent_daemon_HH
#define ztcagent_daemon_HH

#include <zlib/ZuDerive.hh>
#include <zlib/ZmAtomic.hh>
#include <zlib/ZmEngine.hh>
#include <zlib/ZmPlatform.hh>
#include <zlib/ZtString.hh>
#include <zlib/ZtlsVault.hh>

#include <zlib/Zws.hh>
#include <zlib/ztcagent_config.hh>
#include <zlib/ZtcMsg.hh>
#include <zlib/ZtcRing.hh>

namespace Ztc {

ZuDerive(AgentString, (ZtString<ZtStringHeapID<"Ztc.Agent.Env">>));
ZuDerive(AgentSecret, (ZtString<ZtStringSecret<true,
  ZtStringHeapID<"Ztc.Agent.Secret">>>));

struct AgentEnv {
  AgentString issuerURL;
  AgentString clientID;
  AgentString deviceID;
  AgentString caPath;
  AgentString wssURL;
  AgentSecret clientSecret;
  bool provision = false;
  ZmFn<void(), ZmFnHeapID<"Ztc.Agent.Provision">> onProvision;
  AgentString pidDir{"ztc"};
  AgentString ring{"ztc"};
};

namespace AgentCredit {
  enum : uint64_t { Closed = uint64_t{1} << 62, Count = Closed - 1 };
}

class Agent : private ZmEngine<Agent> {
friend ZmEngine<Agent>;
  using Engine = ZmEngine<Agent>;
public:
  using Link = Zws::ClientLink<Agent, Zhttp::H1TLS>;

  Agent() = default;
  ~Agent();
  Agent(const Agent &) = delete;
  Agent &operator =(const Agent &) = delete;

  bool init(const AgentCf &, AgentEnv);
  // Callback forms are asynchronous; blocking forms belong on the main thread.
  // Complete stop before final; startup does not promise OAuth/WSS readiness.
  using Engine::start;
  using Engine::stop;
  using Engine::state;
  using Engine::running;
  using Engine::stopping;
  void final();
  void provisioned(bool);
  ZuBSpan clientSecret() const;

  template <typename Link_>
  void connected(Link_ &, const Zhttp::ConnectedInfo &);
  template <typename Link_>
  void disconnected(Link_ &, bool);
  template <typename Link_>
  void connectFailed(Link_ &, bool);
  template <typename Link_>
  int messageStart(Link_ &, Zws::Opcode::T);
  template <typename Link_, typename Rx>
  int process(Link_ &, Rx &);
  template <typename Link_>
  int messageEnd(Link_ &);

public:
  struct State;

private:
  void start_();
  bool initRuntime_();
  void startHTTP_();
  void startWS_();
  void startScan_();
  void scanBatch_();
  void startReady_();
  void stop_();
  void stopRx_(bool);
  void telDone_();
  void drain_(bool);
  void clearBatch_(bool);
  void closeRuntime_(bool);

private:
  alignas(Zm::CacheLineSize)
  ZmAtomic<uint64_t> m_credit = AgentCredit::Closed;
  State *m_state = nullptr;
};

} // Ztc

#endif /* ztcagent_daemon_HH */
