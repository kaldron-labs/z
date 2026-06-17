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
#include <zlib/ZmSemaphore.hh>

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
  explicit Endpoint(ZiMultiplex *mx) : m_mx{mx} { }
  ~Endpoint() { closeUDP(); }

  Endpoint(const Endpoint &) = delete;
  Endpoint &operator =(const Endpoint &) = delete;

  bool init(ZiMultiplex *);

  bool openUDP(
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
  EndpointDiag diag() const { return {m_rxDiag, m_txDiag}; }
  SockDiag sockDiag() const { return m_sockDiag; }
  void failure() { ++m_rxDiag.failures; }

  void datagramFn(DatagramFn fn) { m_datagramFn = ZuMv(fn); }
  ZmRef<ZiIOBuf> allocTxPkt() {
    return new TxPktAlloc{this};
  }
  bool send(ZmRef<ZiIOBuf>, ZiSockAddr);
  PathHint pathHint();

  void inject(Datagram datagram) { received_(ZuMv(datagram)); }

private:
  friend Cxn_;

  void connected_(Cxn_ *, ZiIOContext &);	// direct call from within rx thread
  void disconnected_(Cxn_ *);			// direct call from within rx thread
  void failed_(bool);				// direct call from within rx thread
  void received_(Datagram);			// direct call from within rx thread
  void sent_(unsigned);				// direct call from within tx thread
  void txDrained_();				// direct call from within rx thread
  void ioError_();				// direct call from within rx thread
  void closeUDP_(ZmSemaphore *);
  void clearFns_();
  ZmRef<ZiIOBuf> allocRxPkt_() {
    return new RxPktAlloc{this};
  }

  // immutable after init()
  ZiMultiplex		*m_mx = nullptr;
  PathMode::T		m_mode = PathMode::ServerUnconnected;
  ZiSockAddr		m_local;
  ZiSockAddr		m_remote;
  SockConfig		m_sockConfig;

  // Rx thread exclusive
  Cxn_			*m_cxn = nullptr;
  DatagramFn		m_datagramFn;
  ReadyFn		m_readyFn;
  FailFn		m_failFn;
  DownFn		m_downFn;
  TxDrainedFn		m_txDrainedFn;
  Cxn_			*m_closingCxn = nullptr;
  ZmSemaphore		*m_closeWaiter = nullptr;
  unsigned		m_generation = 0;
  unsigned		m_closingGeneration = 0;
  EndpointRxDiag	m_rxDiag;
  SockDiag		m_sockDiag;

  // Tx thread exclusive
  EndpointTxDiag	m_txDiag;

  // shared
  ZmAtomic<unsigned>	m_listening = 0;
  ZmAtomic<unsigned>	m_open = 0;
};

} // namespace Zquic

#endif /* ZquicEndpoint_HH */
