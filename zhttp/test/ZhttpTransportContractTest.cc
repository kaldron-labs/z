//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// HTTP native transport trait and public configuration contract

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZhttpService.hh>

using namespace ZuTestUtil;

namespace ZhttpTransportContractTest_ {

using TCP = Zhttp::Transport_::Traits<Zhttp::TCP>;
using TLS = Zhttp::Transport_::Traits<Zhttp::TLS>;
using QUIC = Zhttp::Transport_::Traits<Zhttp::QUIC>;

struct Rx { };
struct Tx { };

template <typename Profile, typename = void>
struct HasProfileTraits : public ZuFalse { };
template <typename Profile>
struct HasProfileTraits<Profile,
  decltype(sizeof(Zhttp::ProfileTraits<Profile>), void())> : public ZuTrue { };

using Invalid =
  Zhttp::Profile<Zhttp::QUIC, Zhttp::Version::H1>;
static_assert(Zhttp::IsProfile<Zhttp::H1TCP>{});
static_assert(Zhttp::IsProfile<Zhttp::H1TLS>{});
static_assert(Zhttp::IsProfile<Zhttp::H2TLS>{});
static_assert(Zhttp::IsProfile<Zhttp::H3QUIC>{});
static_assert(!Zhttp::IsProfile<Invalid>{});
static_assert(HasProfileTraits<Zhttp::H1TCP>{});
static_assert(!HasProfileTraits<Invalid>{});

struct Link {
  Tx txStream() { return {}; }
  int process(Rx &) { return 0; }
  void connected(Zhttp::ConnectedInfo) { }
  void disconnected(bool) { }
};

static_assert(Zhttp::Transport_::HasTxStream<Link>{});
static_assert(Zhttp::Transport_::HasProcess<Link, Rx>{});
static_assert(Zhttp::Transport_::HasConnected<
  Link, Zhttp::ConnectedInfo>{});
static_assert(Zhttp::Transport_::HasDisconnected<Link>{});
using Contract =
  Zhttp::Transport_::LinkContract<Link, Rx, Zhttp::ConnectedInfo>;
static_assert(sizeof(Contract) == 1);

void testTraits()
{
  ZuTestScope(testTraits);

  ZuCHECK(TCP::ID == Zhttp::Transport::TCP &&
      !TCP::Secure && !TCP::Datagram,
    "TCP trait identity mismatch");
  ZuCHECK(TLS::ID == Zhttp::Transport::TLS &&
      TLS::Secure && !TLS::Datagram,
    "TLS trait identity mismatch");
  ZuCHECK(QUIC::ID == Zhttp::Transport::QUIC &&
      QUIC::Secure && QUIC::Datagram,
    "QUIC trait identity mismatch");

  using H1TCP = Zhttp::MessageTraits<Zhttp::H1TCP>;
  using H1TLS = Zhttp::MessageTraits<Zhttp::H1TLS>;
  using H2TLS = Zhttp::MessageTraits<Zhttp::H2TLS>;
  using H3QUIC = Zhttp::MessageTraits<Zhttp::H3QUIC>;
  ZuCHECK(H1TCP::ID == Zhttp::Version::H1 &&
      H1TCP::Transport::ID == Zhttp::Transport::TCP &&
      !H1TCP::Multiplexed && !H1TCP::OneMessagePerLink &&
      H1TCP::CloseDelimited,
    "H1/TCP profile mismatch");
  ZuCHECK(H1TLS::ID == Zhttp::Version::H1 &&
      H1TLS::Transport::ID == Zhttp::Transport::TLS &&
      !H1TLS::Multiplexed && !H1TLS::OneMessagePerLink &&
      H1TLS::CloseDelimited,
    "H1/TLS profile mismatch");
  ZuCHECK(H2TLS::ID == Zhttp::Version::H2 &&
      H2TLS::Transport::ID == Zhttp::Transport::TLS &&
      H2TLS::Multiplexed && H2TLS::OneMessagePerLink &&
      !H2TLS::CloseDelimited,
    "H2/TLS metadata placeholder mismatch");
  ZuCHECK(H3QUIC::ID == Zhttp::Version::H3 &&
      H3QUIC::Transport::ID == Zhttp::Transport::QUIC &&
      H3QUIC::Multiplexed && H3QUIC::OneMessagePerLink &&
      !H3QUIC::CloseDelimited,
    "H3/QUIC profile mismatch");
}

void testParams()
{
  ZuTestScope(testParams);

  Zhttp::EngineConfig engine{nullptr, "rx", "tx"};
  auto tcpCli = TCP::clientParams(engine, Zhttp::TCPConfig{});
  auto tcpSrv = TCP::serverParams(engine, Zhttp::TCPConfig{});
  ZuCHECK(tcpCli.rxThread == "rx" && tcpCli.txThread == "tx" &&
      tcpSrv.rxThread == "rx" && tcpSrv.txThread == "tx",
    "TCP common parameter mapping mismatch");

  auto tlsCli = TLS::clientParams(
    engine, Zhttp::TLSConfig{}.caPath("ca.pem"));
  auto tlsSrv = TLS::serverParams(
    engine, Zhttp::TLSConfig{}
      .certPath("cert.pem").keyPath("key.pem"));
  ZuCHECK(tlsCli.caPath() == "ca.pem" &&
      tlsCli.alpn().length() == 1 &&
      tlsCli.alpn()[0] == "http/1.1",
    "TLS client defaults mismatch");
  ZuCHECK(tlsSrv.certPath() == "cert.pem" &&
      tlsSrv.keyPath() == "key.pem" &&
      tlsSrv.alpn().length() == 1 &&
      tlsSrv.alpn()[0] == "http/1.1",
    "TLS server defaults mismatch");

  Zhttp::H2Config force;
  force.policy(Zhttp::H2Policy::Force);
  Zhttp::H2Config prefer;
  prefer.policy(Zhttp::H2Policy::Prefer);
  Zhttp::H2Config disable;
  disable.policy(Zhttp::H2Policy::Disable);
  auto forceCli = Zhttp::TLS_::clientParams(engine, force);
  auto preferSrv = Zhttp::TLS_::serverParams(engine, prefer);
  auto disableCli = Zhttp::TLS_::clientParams(engine, disable);
  ZuCHECK(forceCli.alpn().length() == 1 &&
      forceCli.alpn()[0] == "h2",
    "force-H2 ALPN mismatch");
  ZuCHECK(preferSrv.alpn().length() == 2 &&
      preferSrv.alpn()[0] == "h2" &&
      preferSrv.alpn()[1] == "http/1.1",
    "prefer-H2 ALPN mismatch");
  ZuCHECK(disableCli.alpn().length() == 1 &&
      disableCli.alpn()[0] == "http/1.1",
    "disable-H2 ALPN mismatch");
  ZuCHECK(Zhttp::TLS_::valid(force) &&
      !Zhttp::TLS_::valid(
	Zhttp::H2Config{}.maxFrameSize(
	  Zhttp::H2::DefltFrameSize - 1)) &&
      !Zhttp::TLS_::valid(Zhttp::H2Config{}.maxPending(0)) &&
      !Zhttp::TLS_::valid(Zhttp::H2Config{}.maxStreamID(2)),
    "shared TLS/H2 validation mismatch");
  ZuCHECK(
    Zhttp::TLS_::version("h2", Zhttp::H2Policy::Force) ==
      Zhttp::Version::H2 &&
    Zhttp::TLS_::version("h2", Zhttp::H2Policy::Prefer) ==
      Zhttp::Version::H2 &&
    Zhttp::TLS_::version("http/1.1", Zhttp::H2Policy::Prefer) ==
      Zhttp::Version::H1 &&
    Zhttp::TLS_::version("http/1.1", Zhttp::H2Policy::Disable) ==
      Zhttp::Version::H1 &&
    Zhttp::TLS_::version("http/1.1", Zhttp::H2Policy::Force) < 0 &&
    Zhttp::TLS_::version("h2", Zhttp::H2Policy::Disable) < 0 &&
    Zhttp::TLS_::version({}, Zhttp::H2Policy::Prefer) < 0,
    "shared TLS negotiated-profile classification mismatch");

  auto quicCli = QUIC::clientParams(engine, Zhttp::QUICConfig{});
  auto quicSrv = QUIC::serverParams(engine, Zhttp::QUICConfig{});
  ZuCHECK(quicCli.alpn().length() == 1 && quicCli.alpn()[0] == "h3" &&
      quicSrv.alpn().length() == 1 && quicSrv.alpn()[0] == "h3",
    "H3 ALPN defaults mismatch");
  ZuCHECK(
    quicCli.maxData() == Zhttp::QUICConfig::DefltMaxData &&
    quicCli.maxStreamData() == Zhttp::QUICConfig::DefltMaxStreamData &&
    quicCli.maxStreamsDuplex() ==
      Zhttp::QUICConfig::DefltClientStreams &&
    quicSrv.maxStreamsDuplex() ==
      Zhttp::QUICConfig::DefltServerStreams &&
    quicCli.maxStreamsSimplex() ==
      Zhttp::QUICConfig::DefltControlStreams,
    "H3 transport defaults mismatch");

  auto serviceQUIC = Zhttp::ServiceConfig{}.idleTimeout(7)
    .quic(Zhttp::QUICConfig{}).quicEngineConfig();
  auto explicitQUIC = Zhttp::ServiceConfig{}.idleTimeout(7)
    .quic(Zhttp::QUICConfig{}.maxIdleTimeout(2500)).quicEngineConfig();
  ZuCHECK(serviceQUIC.maxIdleTimeout() == 7000 &&
      explicitQUIC.maxIdleTimeout() == 2500,
    "service idle timeout mapping mismatch");

  Zhttp::DiscoveryLimits limits{
    .maxRecords = 3, .maxHints = 5,
    .maxEndpoints = 7, .maxAliasDepth = 2};
  auto agent = Zhttp::AgentConfig{}
    .requestTimeout(13).maxAltSvc(11)
    .discoveryLimits(limits).altSvcCrossHost(true)
    .h2Policy(Zhttp::H2Policy::Disable);
  ZuCHECK(agent.requestTimeout() == 13 &&
      agent.maxAltSvc() == 11 && agent.altSvcCrossHost() &&
      agent.h2Policy() == Zhttp::H2Policy::Disable &&
      agent.discoveryLimits().maxRecords == 3 &&
      agent.discoveryLimits().maxHints == 5 &&
      agent.discoveryLimits().maxEndpoints == 7 &&
      agent.discoveryLimits().maxAliasDepth == 2,
    "agent discovery bounds mapping mismatch");
  ZuCHECK(
    Zhttp::ServiceConfig{}.tlsConfig().policy() ==
      Zhttp::H2Policy::Prefer &&
    Zhttp::ServiceConfig{}
      .tls(Zhttp::H2Config{}.policy(Zhttp::H2Policy::Force))
      .tlsConfig().policy() == Zhttp::H2Policy::Force,
    "service H2 policy mapping mismatch");
}

void testMetadata()
{
  ZuTestScope(testMetadata);

  auto tcp = Zhttp::ProfileTraits<Zhttp::H1TCP>::connected(
    Ztcp::Connected{});
  auto tls = Zhttp::ProfileTraits<Zhttp::H1TLS>::connected(
    Ztls::Connected{"http/1.1", 0x304});
  auto h2 = Zhttp::ProfileTraits<Zhttp::H2TLS>::connected(
    Ztls::Connected{"h2", 0x304});
  auto quic = Zhttp::ProfileTraits<Zhttp::H3QUIC>::connected(
    Zquic::Connected{"h3", 1});
  ZuCHECK(tcp.transport == Zhttp::Transport::TCP &&
      !tcp.secure && tcp.httpVersion == Zhttp::Version::H1,
    "TCP connected metadata mismatch");
  ZuCHECK(tls.transport == Zhttp::Transport::TLS &&
      tls.secure && !tls.multiplexed && tls.alpn == "http/1.1",
    "TLS connected metadata mismatch");
  ZuCHECK(h2.transport == Zhttp::Transport::TLS &&
      h2.secure && h2.multiplexed &&
      h2.httpVersion == Zhttp::Version::H2 && h2.alpn == "h2",
    "H2 connected metadata mismatch");
  ZuCHECK(quic.transport == Zhttp::Transport::QUIC &&
      quic.secure && quic.multiplexed &&
      quic.httpVersion == Zhttp::Version::H3 && quic.alpn == "h3",
    "QUIC connected metadata mismatch");
  ZuCHECK(
    Zhttp::migrationMode("disabled") == Zhttp::Migration::Disabled &&
    Zhttp::migrationMode("passive") == Zhttp::Migration::Passive &&
    Zhttp::migrationMode("active") == Zhttp::Migration::Active,
    "migration configuration vocabulary mismatch");
}

} // namespace ZhttpTransportContractTest_

int main(int argc, char **argv)
{
  using namespace ZhttpTransportContractTest_;

  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testTraits);
  ZuTestCall(testParams);
  ZuTestCall(testMetadata);
  return 0;
}
