//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z QUIC UDP endpoint

#ifndef ZquicEndpoint_HH
#define ZquicEndpoint_HH

#ifndef ZquicLib_HH
#include <zlib/ZquicLib.hh>
#endif

#include <zlib/ZmFn.hh>
#include <zlib/ZmPolymorph.hh>

#include <zlib/ZquicSock.hh>

namespace Zquic {

class Endpoint : public ZmPolymorph {
  class Cxn_;

public:
  using RxPktAlloc = PktRxBufAlloc<>;
  using TxPktAlloc = PktTxBufAlloc<>;
  using DatagramFn = ZmFn<void(Datagram)>;
  using ReadyFn = ZmFn<void(Endpoint *)>;
  using FailFn = ZmFn<void(bool)>;
  using DownFn = ZmFn<void(Endpoint *)>;
  using TxDrainedFn = ZmFn<void()>;

  Endpoint() = default;
  ~Endpoint() { closeUDP(); }

  Endpoint(const Endpoint &) = delete;
  Endpoint &operator =(const Endpoint &) = delete;

  bool openUDP(
    ZiMultiplex *mx,
    PathMode::T mode,
    ZiIP localIP, uint16_t localPort,
    ZiIP remoteIP, uint16_t remotePort,
    DatagramFn datagramFn = {},
    ReadyFn readyFn = {},
    FailFn failFn = {},
    DownFn downFn = {},
    TxDrainedFn txDrainedFn = {});

  void closeUDP();

  bool listening() const { return m_listening; }
  bool connected() const { return m_cxn; }
  ZiMultiplex *mx() const { return m_mx; }
  const ZiSockAddr &local() const { return m_local; }
  const ZiSockAddr &remote() const { return m_remote; }
  PathMode::T mode() const { return m_mode; }
  const EndpointDiag &diag() const { return m_diag; }
  void failure() { ++m_diag.failures; }

  void datagramFn(DatagramFn fn) { m_datagramFn = ZuMv(fn); }
  ZmRef<ZiIOBuf> allocTxPkt() {
    return new TxPktAlloc{this};
  }
  bool send(ZmRef<ZiIOBuf>, ZiSockAddr);

  void inject(Datagram datagram) { received_(ZuMv(datagram)); }

private:
  friend Cxn_;

  void connected_(Cxn_ *, ZiIOContext &);
  void disconnected_(Cxn_ *);
  void failed_(bool);
  void received_(Datagram);
  void sent_(unsigned);
  void txDrained_();
  void ioError_();
  void clearFns_();
  ZmRef<ZiIOBuf> allocRxPkt_() {
    return new RxPktAlloc{this};
  }

  // Shared immutable/configuration after openUDP(), read from Rx and Tx.
  ZiMultiplex		*m_mx = nullptr;
  PathMode::T		m_mode = PathMode::ServerUnconnected;
  ZiSockAddr		m_local;
  ZiSockAddr		m_remote;

  // Rx-owned endpoint lifecycle/callback state. Tx work must not retain Link
  // ownership or depend on these callbacks after closeUDP() has begun draining.
  Cxn_			*m_cxn = nullptr;
  DatagramFn		m_datagramFn;
  ReadyFn		m_readyFn;
  FailFn		m_failFn;
  DownFn		m_downFn;
  TxDrainedFn		m_txDrainedFn;
  Cxn_			*m_closingCxn = nullptr;
  unsigned		m_generation = 0;
  unsigned		m_closingGeneration = 0;
  ZmAtomic<unsigned>	m_listening = 0;

  // Shared diagnostics; individual counters are touched by their owning path.
  EndpointDiag		m_diag;
};

} // namespace Zquic

#endif /* ZquicEndpoint_HH */
