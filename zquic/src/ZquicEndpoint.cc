//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZquicEndpoint.hh>

#include <zlib/ZmHeap.hh>

#ifndef _WIN32
#include <sys/socket.h>
#endif

namespace Zquic {

class Endpoint::Cxn_ : public ZiConnection {
friend Endpoint;

public:
  static constexpr unsigned MaxTxQueue = 32;

  void *operator new(size_t);
  void operator delete(void *) noexcept;

  Cxn_(Endpoint *endpoint, const ZiCxnInfo &ci) :
      ZiConnection(endpoint->mx(), ci), m_endpoint{endpoint} { }

  void connected(ZiIOContext &io) override {
    m_endpoint->connected_(this, io);
  }
  void disconnected() override {
    m_endpoint->disconnected_(this);
  }

  bool sendPacket(ZmRef<ZiIOBuf> buf, ZiSockAddr addr) {
    if (!buf) return false;
    if (m_txBuf) return enqueueTx_(ZuMv(buf), ZuMv(addr));
    m_txBuf = ZuMv(buf);
    m_txAddr = ZuMv(addr);
    send(ZiIOFn{this, ZmFnPtr<&Cxn_::sendStart_>{}});
    return true;
  }

private:
  bool enqueueTx_(ZmRef<ZiIOBuf> buf, ZiSockAddr addr) {
    if (m_txQueueCount >= MaxTxQueue) return false;
    unsigned i = m_txQueueTail;
    m_txQueueBuf[i] = ZuMv(buf);
    m_txQueueAddr[i] = ZuMv(addr);
    m_txQueueTail = (m_txQueueTail + 1) % MaxTxQueue;
    ++m_txQueueCount;
    return true;
  }

  bool dequeueTx_() {
    if (!m_txQueueCount) return false;
    unsigned i = m_txQueueHead;
    m_txBuf = ZuMv(m_txQueueBuf[i]);
    m_txAddr = ZuMv(m_txQueueAddr[i]);
    m_txQueueHead = (m_txQueueHead + 1) % MaxTxQueue;
    --m_txQueueCount;
    return true;
  }

  bool recvDone_(ZiIOContext &io) {
    if (io.length < 0) {
      m_endpoint->ioError_();
      io.disconnect();
      return true;
    }
    if (io.length > 0 && m_rxBuf) {
      m_rxBuf->skip = 0;
      m_rxBuf->length = unsigned(io.length);
      auto buf = ZuMv(m_rxBuf);
      m_endpoint->received_(Datagram{ZuMv(buf), io.addr});
    }
    armRecv_(io);
    return true;
  }

  void armRecv_(ZiIOContext &io) {
    m_rxBuf = m_endpoint->allocRxPacket_();
    io.init(
      ZiIOFn{this, ZmFnPtr<&Cxn_::recvDone_>{}},
      m_rxBuf->data_(), m_rxBuf->size, 0);
  }

  bool sendStart_(ZiIOContext &io) {
    if (!m_txBuf) {
      io.complete();
      return true;
    }
    io.init(
      ZiIOFn{this, ZmFnPtr<&Cxn_::sendDone_>{}},
      m_txBuf->data(), m_txBuf->length, 0, m_txAddr);
    return true;
  }

  bool sendDone_(ZiIOContext &io) {
    if (io.length < 0) {
      m_endpoint->ioError_();
      m_txBuf = nullptr;
      io.complete();
      return true;
    }
    if ((io.offset += io.length) < io.size) return true;
    m_endpoint->sent_(io.size);
    m_txBuf = nullptr;
    if (dequeueTx_()) {
      io.init(
	ZiIOFn{this, ZmFnPtr<&Cxn_::sendDone_>{}},
	m_txBuf->data(), m_txBuf->length, 0, m_txAddr);
      return true;
    }
    io.complete();
    return true;
  }

private:
  Endpoint		*m_endpoint = nullptr;
  ZmRef<ZiIOBuf>	m_rxBuf;
  ZmRef<ZiIOBuf>	m_txBuf;
  ZiSockAddr		m_txAddr;
  ZmRef<ZiIOBuf>	m_txQueueBuf[MaxTxQueue];
  ZiSockAddr		m_txQueueAddr[MaxTxQueue];
  unsigned		m_txQueueHead = 0;
  unsigned		m_txQueueTail = 0;
  unsigned		m_txQueueCount = 0;
};

void *Endpoint::Cxn_::operator new(size_t s)
{
  using Heap = ZmHeap<"Zquic.Endpoint.Cxn", Endpoint::Cxn_>;
  return Heap::operator new(s);
}

void Endpoint::Cxn_::operator delete(void *p) noexcept
{
  using Heap = ZmHeap<"Zquic.Endpoint.Cxn", Endpoint::Cxn_>;
  Heap::operator delete(p);
}

bool Endpoint::openUDP(
  ZiMultiplex *mx,
  PathMode::T mode,
  ZiIP localIP, uint16_t localPort,
  ZiIP remoteIP, uint16_t remotePort,
  DatagramFn datagramFn,
  ReadyFn readyFn,
  FailFn failFn)
{
  ZiAssert(mx, "Zquic", (mx), "null endpoint multiplexer", return false);
  ZiAssert(mx->running(), "Zquic", (mx),
    "endpoint multiplexer is not running", return false);
  if (m_cxn) return false;

  m_mx = mx;
  m_mode = mode;
  m_local.init(localIP, localPort);
  if (!!remoteIP) m_remote.init(remoteIP, remotePort); else m_remote.null();
  m_datagramFn = ZuMv(datagramFn);
  m_readyFn = ZuMv(readyFn);
  m_failFn = ZuMv(failFn);
  m_listening = false;

  ZiCxnOptions options;
  options.udp(true);

  mx->udp(
    // ZmFn captures only this pointer here; keep it inline and off hot heaps.
    ZiConnectFn{[this](const ZiCxnInfo &ci) -> ZiConnection * {
      auto cxn = new Cxn_{this, ci};
      ++m_diag.endpointCxnAllocs;
      m_cxn = cxn;
      return cxn;
    }},
    ZiFailFn{[this](bool transient) { failed_(transient); }},
    localIP, localPort,
    mode == PathMode::ClientConnected ? remoteIP : ZiIP{},
    mode == PathMode::ClientConnected ? remotePort : 0,
    options);

  return true;
}

void Endpoint::closeUDP()
{
  m_listening = false;
  if (m_cxn) {
    m_cxn->close();
    m_cxn = nullptr;
  }
}

bool Endpoint::send(ZmRef<ZiIOBuf> buf, ZiSockAddr addr)
{
  if (!m_cxn) return false;
  return m_cxn->sendPacket(ZuMv(buf), ZuMv(addr));
}

void Endpoint::connected_(Cxn_ *cxn, ZiIOContext &io)
{
  if (cxn != m_cxn) return;

#ifndef _WIN32
  {
    ZiSockAddr local;
    socklen_t len = local.len();
    if (::getsockname(cxn->info().socket, local.sa(), &len) == 0)
      m_local = local;
  }
#endif

  m_listening = true;
  cxn->armRecv_(io);
  if (m_readyFn) m_readyFn(this);
}

void Endpoint::disconnected_(Cxn_ *cxn)
{
  if (cxn == m_cxn) {
    m_cxn = nullptr;
    m_listening = false;
  }
}

void Endpoint::failed_(bool transient)
{
  ++m_diag.openFailures;
  if (m_failFn) m_failFn(transient);
}

void Endpoint::received_(Datagram datagram)
{
  ++m_diag.datagramsRx;
  if (datagram.buf) m_diag.bytesRx += datagram.buf->length;
  if (m_datagramFn) m_datagramFn(ZuMv(datagram));
}

void Endpoint::sent_(unsigned bytes)
{
  ++m_diag.datagramsTx;
  m_diag.bytesTx += bytes;
}

void Endpoint::ioError_()
{
  ++m_diag.ioErrors;
}

} // namespace Zquic
