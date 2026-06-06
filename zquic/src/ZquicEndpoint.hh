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

#include <zlib/ZquicSock.hh>

namespace Zquic {

struct Datagram {
  ZmRef<ZiIOBuf>	buf;
  ZiSockAddr		addr;

  Datagram() = default;
  Datagram(ZmRef<ZiIOBuf> buf_, ZiSockAddr addr_) :
    buf{ZuMv(buf_)}, addr{ZuMv(addr_)} { }
};

struct EndpointDiag {
  ZmAtomic<uint64_t>	packetRxBufAllocs = 0;
  ZmAtomic<uint64_t>	packetTxBufAllocs = 0;
  ZmAtomic<uint64_t>	endpointCxnAllocs = 0;
  ZmAtomic<uint64_t>	datagramsRx = 0;
  ZmAtomic<uint64_t>	datagramsTx = 0;
  ZmAtomic<uint64_t>	bytesRx = 0;
  ZmAtomic<uint64_t>	bytesTx = 0;
  ZmAtomic<uint64_t>	openFailures = 0;
  ZmAtomic<uint64_t>	ioErrors = 0;
};

class Endpoint : public ZmPolymorph {
  class Cxn_;

public:
  using RxPacketAlloc = PacketRxBufAlloc<>;
  using TxPacketAlloc = PacketTxBufAlloc<>;
  using DatagramFn = ZmFn<void(Datagram)>;
  using ReadyFn = ZmFn<void(Endpoint *)>;
  using FailFn = ZmFn<void(bool)>;

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
    FailFn failFn = {});

  void closeUDP();

  bool listening() const { return m_listening; }
  bool connected() const { return m_cxn; }
  ZiMultiplex *mx() const { return m_mx; }
  const ZiSockAddr &local() const { return m_local; }
  const ZiSockAddr &remote() const { return m_remote; }
  PathMode::T mode() const { return m_mode; }
  const EndpointDiag &diag() const { return m_diag; }

  void datagramFn(DatagramFn fn) { m_datagramFn = ZuMv(fn); }
  ZmRef<ZiIOBuf> allocTxPacket() {
    ++m_diag.packetTxBufAllocs;
    return new TxPacketAlloc{this};
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
  void ioError_();
  ZmRef<ZiIOBuf> allocRxPacket_() {
    ++m_diag.packetRxBufAllocs;
    return new RxPacketAlloc{this};
  }

  ZiMultiplex	*m_mx = nullptr;
  Cxn_		*m_cxn = nullptr;
  PathMode::T	m_mode = PathMode::ServerUnconnected;
  ZiSockAddr	m_local;
  ZiSockAddr	m_remote;
  DatagramFn	m_datagramFn;
  ReadyFn	m_readyFn;
  FailFn	m_failFn;
  ZmAtomic<unsigned>	m_listening = 0;
  EndpointDiag	m_diag;
};

} // namespace Zquic

#endif /* ZquicEndpoint_HH */
