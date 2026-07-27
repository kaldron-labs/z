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
struct SyntheticProtocol { };
struct SyntheticTransport {
  enum {
    HTTPVersion = Zhttp::Version::H1,
    Secure = true,
    Multiplexed = true
  };
};
using SyntheticMessage =
  Zhttp::MessageTraits<SyntheticProtocol, SyntheticTransport>;
static_assert(SyntheticMessage::ID == Zhttp::Version::H1);
static_assert(!SyntheticMessage::OneMessagePerLink);
static_assert(SyntheticMessage::CloseDelimited);
static_assert(SyntheticMessage::Transport::Multiplexed);

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
      TCP::HTTPVersion == Zhttp::Version::H1 &&
      !TCP::Secure && !TCP::Multiplexed && !TCP::Datagram,
    "TCP trait identity mismatch");
  ZuCHECK(TLS::ID == Zhttp::Transport::TLS &&
      TLS::HTTPVersion == Zhttp::Version::H1 &&
      TLS::Secure && !TLS::Multiplexed && !TLS::Datagram,
    "TLS trait identity mismatch");
  ZuCHECK(QUIC::ID == Zhttp::Transport::QUIC &&
      QUIC::HTTPVersion == Zhttp::Version::H3 &&
      QUIC::Secure && QUIC::Multiplexed && QUIC::Datagram,
    "QUIC trait identity mismatch");
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
    .discoveryLimits(limits).altSvcCrossHost(true);
  ZuCHECK(agent.requestTimeout() == 13 &&
      agent.maxAltSvc() == 11 && agent.altSvcCrossHost() &&
      agent.discoveryLimits().maxRecords == 3 &&
      agent.discoveryLimits().maxHints == 5 &&
      agent.discoveryLimits().maxEndpoints == 7 &&
      agent.discoveryLimits().maxAliasDepth == 2,
    "agent discovery bounds mapping mismatch");
}

void testMetadata()
{
  ZuTestScope(testMetadata);

  auto tcp = TCP::connected(Ztcp::Connected{});
  auto tls = TLS::connected(Ztls::Connected{"http/1.1", 0x304});
  auto quic = QUIC::connected(Zquic::Connected{"h3", 1});
  ZuCHECK(tcp.transport == Zhttp::Transport::TCP &&
      !tcp.secure && tcp.httpVersion == Zhttp::Version::H1,
    "TCP connected metadata mismatch");
  ZuCHECK(tls.transport == Zhttp::Transport::TLS &&
      tls.secure && !tls.multiplexed && tls.alpn == "http/1.1",
    "TLS connected metadata mismatch");
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
