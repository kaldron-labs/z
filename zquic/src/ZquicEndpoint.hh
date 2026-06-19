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
  using CloseFn = ZmFn<void()>;

  Endpoint();
  explicit Endpoint(ZiMultiplex *);
  ~Endpoint();

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

  void closeUDP(CloseFn);
  void final(CloseFn = {});

  bool listening() const { return m_listening; }
  bool connected() const;
  ZiMultiplex *mx() const { return m_mx; }
  const ZiSockAddr &local() const { return m_local; }
  const ZiSockAddr &remote() const { return m_remote; }
  PathMode::T mode() const { return m_mode; }
  EndpointDiag diag() const;
  SockDiag sockDiag() const;
  void failure() { ++m_rxDiag.failures; }

  void datagramFn(DatagramFn fn) { m_datagramFn = ZuMv(fn); }
  ZmRef<ZiIOBuf> allocTxPkt() {
    return new TxPktAlloc{this};
  }
  bool send(ZmRef<ZiIOBuf>, ZiSockAddr);

  void inject(Datagram datagram) { received_(ZuMv(datagram)); }

private:
  friend Cxn_;

  void connected_(Cxn_ *, ZiIOContext &);	// direct call from within rx thread
  void disconnected_(Cxn_ *);			// direct call from within rx thread
  void failed_(bool);				// direct call from within rx thread
  void received_(Datagram);			// direct call from within rx thread
  void sent_(unsigned);				// direct call from within tx thread
  void txDrained_();				// direct call from within tx thread
  void ioError_();				// direct call from within rx thread
  bool send_(ZmRef<ZiIOBuf>, ZiSockAddr);	// direct call from within tx thread
  void closeUDP_(CloseFn);
  void beginCloseTx_(ZmRef<Cxn_> = {});
  void closeTx_(unsigned, ZmRef<Cxn_>);
  void closeTxDone_(unsigned);
  void completeClose_();
  void clearFns_();
  bool rxInvoked() const { return m_mx && m_mx->invoked(m_mx->rxThread()); }
  bool txInvoked() const { return m_mx && m_mx->invoked(m_mx->txThread()); }
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
  Cxn_			*m_closingCxn = nullptr;
  CloseFn		m_closeFn;
  unsigned		m_generation = 0;
  unsigned		m_closingGeneration = 0;
  bool			m_closeTxPending = false;
  EndpointRxDiag	m_rxDiag;
  SockDiag		m_sockDiag;

  // Tx thread exclusive
  ZmRef<Cxn_>		m_txCxn;
  unsigned		m_txGeneration = 0;
  bool			m_txClosing = false;
  EndpointTxDiag	m_txDiag;
  TxDrainedFn		m_txDrainedFn;

  // shared
  ZmAtomic<unsigned>	m_connected = 0;
  ZmAtomic<unsigned>	m_listening = 0;
  ZmAtomic<unsigned>	m_open = 0;
};

} // namespace Zquic

#endif /* ZquicEndpoint_HH */
