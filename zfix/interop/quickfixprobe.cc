//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Independent SDK routing/TLS probe; no native FIX parser or session engine

#include <iostream>
#include <string.h>
#include <string>

#include <quickfix/config.h>
#include <quickfix/Application.h>
#include <quickfix/Dictionary.h>
#include <quickfix/Log.h>
#include <quickfix/MessageStore.h>
#include <quickfix/SessionSettings.h>
#include <quickfix/SocketAcceptor.h>
#include <quickfix/SocketInitiator.h>
#include <quickfix/SocketServer.h>
#include <quickfix/SSLSocketAcceptor.h>
#include <quickfix/SSLSocketInitiator.h>
#include <quickfix/Utility.h>
#include <quickfix/UtilitySSL.h>
#include <openssl/err.h>
#include <openssl/x509.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmAtomic.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmTime.hh>
#include <zlib/ZtArray.hh>
#include <zlib/ZtEnum.hh>
#include <zlib/ZtString.hh>
#include <zlib/ZfJSON.hh>

#ifdef ZfixFmtLib_HH
#error Independent SDK probe must not include the native FIX parser
#endif
#ifdef ZfixLib_HH
#error Independent SDK probe must not include the native FIX session engine
#endif
#if !HAVE_SSL
#error Independent SDK probe requires QuickFIX SSL support
#endif

using namespace ZuTestUtil;
namespace ProbeCase {

  ZtEnum(, ProbeCase, int8_t, TCP, DNS, IP, WrongName, WrongIP, Untrusted);
  ZtEnumMap(, ProbeCase, Wire, "tcp", "tls-dns", "tls-ip", "wrong-name", "wrong-ip", "untrusted");
}

ZtEnumImplNS(ProbeCase);
ZtEnumImplNS(ProbeCase, Wire);

struct ProbePorts : ZtArray<unsigned, ZtArrayHeapID<"Zfix.QFProbe.Ports">> {
  ZuDerive_(ProbePorts, (ZtArray<unsigned, ZtArrayHeapID<"Zfix.QFProbe.Ports">>));
friend ZfJSON::AsArray<ZfFieldTC::UInt32> ZfJSON_Fmt(ProbePorts *);
};

struct ProbeReady {
  ZuCSpan	event{"ready"}, scenario;
  ProbePorts	ports;
};

ZfStruct(, (ProbeReady, JSON), (event, , String), (scenario, , String), (ports, , UDT));
ZfStructImpl(ProbeReady); ZfStructImpl(ProbeReady, JSON);
struct ProbeResult {
  ZuCSpan	event{"result"}, scenario;
  unsigned	sessions = 0, client = 0, server = 0, failures = 0, errors = 0;
  bool		expectedFailure = false;
};

ZfStruct(, (ProbeResult, JSON), (event, , String), (scenario, , String),
  (sessions, , UInt32), (client, , UInt32), (server, , UInt32),
  (failures, , UInt32), (errors, , UInt32), (expectedFailure, , Bool));
ZfStructImpl(ProbeResult); ZfStructImpl(ProbeResult, JSON);
template <typename T> static void json(const T &value)
{
  ZtString<ZtStringHeapID<"Zfix.QFProbe.JSON">> text;
  ZfJSON::save(text, value); text << '\n';
  std::cout << text << std::flush;
}
struct ProbeState {
  ZmSemaphore		ready, failed;
  ZmAtomic<unsigned>	client{0}, server{0}, failures{0}, errors{0};
};

struct ProbeApp : FIX::NullApplication {
  ProbeState	&state;
  const bool	acceptor;

  ProbeApp(ProbeState &s, bool a) : state{s}, acceptor{a} { }

  void onLogon(const FIX::SessionID &id) override {
    const auto &peer = acceptor ? id.getTargetCompID().getValue() :
      id.getSenderCompID().getValue();
    unsigned bit;
    if (peer == "CLIENT1") bit = 1;
    else if (peer == "CLIENT2") bit = 2;
    else { ++state.errors; state.ready.post(); return; }
    auto &mask = acceptor ? state.server : state.client;
    if (mask.xchOr(bit) & bit) ++state.errors;
    state.ready.post();
  }
};

struct ProbeLog : ZmNHeap<ProbeLog>, FIX::Log {
friend inline ZuStringT<"Zfix.QFProbe.Log"> ZmNHeapID(ProbeLog *);
  ProbeState	&state;

  explicit ProbeLog(ProbeState &s) : state{s} { }

  void clear() override { }
  void backup() override { }

  void onIncoming(const std::string &) override { }
  void onOutgoing(const std::string &) override { }

  void onEvent(const std::string &event) override {
    log("QuickFIX: ", event);
    if (event.find("SSL_connect failed") != std::string::npos) {
      ++state.failures; state.failed.post();
    }
  }
};

struct ProbeLogs : FIX::LogFactory {
  ProbeState	&state;

  explicit ProbeLogs(ProbeState &s) : state{s} { }

  FIX::Log *create() override { return new ProbeLog{state}; }
  FIX::Log *create(const FIX::SessionID &) override { return new ProbeLog{state}; }

  void destroy(FIX::Log *log) override { delete log; }
};

template <typename Peer> struct ProbeStop {
  Peer	&peer;

  ~ProbeStop() { peer.stop(true); }
};

static std::string file(const char *root, const char *name)
{
  std::string path{root}; path += '/'; path += name; return path;
}
static FIX::Dictionary defaults(const char *dictionary, bool acceptor)
{
  FIX::Dictionary d;
  d.setString(FIX::CONNECTION_TYPE, acceptor ? "acceptor" : "initiator");
  d.setString(FIX::START_TIME, "00:00:00"); d.setString(FIX::END_TIME, "00:00:00");
  d.setString(FIX::DATA_DICTIONARY, dictionary); d.setBool(FIX::USE_DATA_DICTIONARY, true);
  d.setInt(FIX::HEARTBTINT, 30); d.setInt(FIX::RECONNECT_INTERVAL, 60);
  return d;
}
static FIX::SessionID session(bool acceptor, unsigned index)
{
  std::string client = "CLIENT" + std::to_string(index + 1);
  return FIX::SessionID{"FIX.4.4", acceptor ? "SERVER" : client,
    acceptor ? client : "SERVER"};
}
template <bool TLS>
static ProbeResult run(ProbeCase::T kind, const char *dictionary, const char *root,
    bool print, bool force = true)
{
  ProbeResult result; result.scenario = ProbeCase::Wire::v2s(kind);
  result.expectedFailure = kind >= ProbeCase::WrongName;
  result.sessions = result.expectedFailure ? 1 : 2;
  ProbeState state;
  {
    FIX::SessionSettings settings;
    auto d = defaults(dictionary, true);
    d.setInt(FIX::SOCKET_ACCEPT_PORT, 0);
    if constexpr (TLS) {
      const char *cert = kind == ProbeCase::WrongName ? "name.pem" :
	kind == ProbeCase::WrongIP ? "ip.pem" : "valid.pem";
      d.setString(FIX::SERVER_CERTIFICATE_FILE, file(root, cert));
      d.setString(FIX::SERVER_CERTIFICATE_KEY_FILE, file(root, "server.key"));
    }
    settings.set(d);
    for (unsigned i = 0; i < result.sessions; ++i) settings.set(session(true, i), {});
    ProbeApp serverApp{state, true}, clientApp{state, false};
    FIX::MemoryStoreFactory serverStore, clientStore;
    ProbeLogs logs{state};
    using Acceptor = ZuIf<TLS, FIX::SSLSocketAcceptor, FIX::SocketAcceptor>;
    using Initiator = ZuIf<TLS, FIX::SSLSocketInitiator, FIX::SocketInitiator>;
    Acceptor server{serverApp, serverStore, settings, logs};
    ProbeStop serverStop{server};
    server.start();
    ProbeReady ready; ready.scenario = result.scenario;
    const auto &ports = server.sessionToPort();
    if (ports.size() != result.sessions) ++state.errors;
    for (unsigned i = 0; i < result.sessions; ++i) {
      auto p = ports.find(session(true, i));
      if (p == ports.end() || !p->second) { ++result.errors; return result; }
      ready.ports.push(p->second);
    }
    if (ready.ports.length() == 2 && ready.ports[0] == ready.ports[1]) ++state.errors;
    if (print) json(ready);
    FIX::SessionSettings clients;
    d = defaults(dictionary, false);
    d.setString(FIX::SOCKET_CONNECT_HOST,
      kind == ProbeCase::IP || kind == ProbeCase::WrongIP ? "127.0.0.1" : "localhost");
    if constexpr (TLS)
      d.setString(FIX::CERTIFICATE_AUTHORITIES_FILE,
	file(root, kind == ProbeCase::Untrusted ? "other.pem" : "ca.pem"));
    clients.set(d);
    for (unsigned i = 0; i < result.sessions; ++i) {
      FIX::Dictionary link; link.setInt(FIX::SOCKET_CONNECT_PORT, ready.ports[i]);
      clients.set(session(false, i), link);
    }
    Initiator client{clientApp, clientStore, clients, logs};
    ProbeStop clientStop{client};
    client.start();
    auto deadline = Zm::now(10);
    if (result.expectedFailure) {
      if (state.failed.timedwait(deadline) != 0) ++state.errors;
    } else {
      for (unsigned i = 0; i < result.sessions * 2; ++i)
	if (state.ready.timedwait(deadline) != 0) { ++state.errors; break; }
    }
    // Stop and join both workers while applications, stores, logs and callback
    // state remain alive. Guards cover exceptions; ASan observes SDK shutdown.
    client.stop(force); server.stop(force);
  }
  result.client = state.client.load_(); result.server = state.server.load_();
  result.failures = state.failures.load_(); result.errors = state.errors.load_();
  unsigned expected = (1U<<result.sessions) - 1;
  if (result.expectedFailure ? result.client || result.server || !result.failures :
      result.client != expected || result.server != expected || result.failures) ++result.errors;
  return result;
}
static ProbeResult probe(ProbeCase::T kind, const char *dictionary, const char *root, bool print)
{
  return kind == ProbeCase::TCP ? run<false>(kind, dictionary, root, print) :
    run<true>(kind, dictionary, root, print);
}
static void ports()
{
  ZuTestScope(ports);
  FIX::SocketServer server;
  auto a = server.add(0), b = server.add(0);
  unsigned first = uint16_t(FIX::socket_hostport(a)), second = uint16_t(FIX::socket_hostport(b));
  ZuCheck(a != b && first && second && first != second);
  ZuCheck(uint16_t(server.socketToPort(a)) == first &&
    uint16_t(server.socketToPort(b)) == second);
  ZuCheck(server.add(first) == a && server.add(second) == b);
}
static void caNames(const char *root)
{
  ZuTestScope(caNames);
  auto ca = file(root, "ca-dupe.pem"), directory = file(root, "ca-dir");
  auto names = FIX::findCAList(ca.c_str(), directory.c_str());
  ZuCheck(names && sk_X509_NAME_num(names) == 1);
  sk_X509_NAME_pop_free(names, X509_NAME_free);
  // The upstream directory walk also attempts non-certificate entries. Retire
  // those diagnostics from this isolated CA aggregation call before TLS setup.
  ERR_clear_error();
}
static void pair(ProbeCase::T kind, const char *dictionary, const char *root)
{
  ZuTestScope(pair);
  auto result = probe(kind, dictionary, root, false);
  log("scenario=", ProbeCase::Wire::v2s(kind));
  ZuCheck(!result.errors);
  ZuCheck(result.expectedFailure ? !result.client && !result.server && result.failures :
    result.client == 3 && result.server == 3 && !result.failures);
}
static void shutdown(const char *dictionary, const char *root, bool force)
{
  ZuTestScope(shutdown);
  enum { Rounds = 4 };
  unsigned errors = 0;
  for (unsigned round = 0; round < Rounds; ++round) {
    // Recreate two real sessions each round; do not intercept SDK shutdown.
    log("shutdown force=", force, " round=", round);
    auto result = run<false>(ProbeCase::TCP, dictionary, root, false, force);
    errors += result.errors;
  }
  ZuCheck(!errors);
}

int main(int argc, char **argv)
{
  try {
    if (argc == 5 && !strcmp(argv[1], "--json")) {
      auto kind = ProbeCase::Wire::s2v(argv[2]);
      if (kind < 0) return 2;
      auto result = probe(kind, argv[3], argv[4], true);
      json(result); return result.errors ? 1 : 0;
    }
    if (argc != 3) {
      std::cerr << "usage: quickfixprobe [--json CASE] FIX44.xml CERT_DIRECTORY\n";
      return 2;
    }
    parse(1, argv); ZuTestMain();
    ZuTestCall(ports);
    ZuTestCall(caNames, argv[2]);
    ZuTestCall(pair, ProbeCase::TCP, argv[1], argv[2]);
    ZuTestCall(shutdown, argv[1], argv[2], true);
    ZuTestCall(shutdown, argv[1], argv[2], false);
    ZuTestCall(pair, ProbeCase::DNS, argv[1], argv[2]);
    ZuTestCall(pair, ProbeCase::IP, argv[1], argv[2]);
    ZuTestCall(pair, ProbeCase::WrongName, argv[1], argv[2]);
    ZuTestCall(pair, ProbeCase::WrongIP, argv[1], argv[2]);
    ZuTestCall(pair, ProbeCase::Untrusted, argv[1], argv[2]);
  } catch (const std::exception &error) {
    log("QuickFIX dependency probe: ", error.what()); return 1;
  }
  return 0;
}
