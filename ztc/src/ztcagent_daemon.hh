//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// OAuth/WSS agent transport

#ifndef ztcagent_daemon_HH
#define ztcagent_daemon_HH

#include <zlib/ZuDerive.hh>
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

class Agent {
public:
  struct Link;

  Agent() = default;
  ~Agent();
  Agent(const Agent &) = delete;
  Agent &operator =(const Agent &) = delete;

  bool init(const AgentCf &, AgentEnv);
  bool start();
  bool stop();
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
  State *m_state = nullptr;
};

struct Agent::Link :
    public Zws::Client<Agent, Zhttp::H1TLS>::Link {
  using Base = Zws::Client<Agent, Zhttp::H1TLS>::Link;
  Link(Zws::Client<Agent, Zhttp::H1TLS> *client,
      const Zws::URI &uri, ZuBSpan authorization) :
    Base{client, uri, Protocol, authorization} { }
  ~Link() { clearAuthorization(); }
};

} // Ztc

#endif /* ztcagent_daemon_HH */
