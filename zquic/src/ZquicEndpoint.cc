//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZquicEndpoint.hh>

#include <zlib/ZmHeap.hh>
#include <zlib/ZmList.hh>

#ifndef _WIN32
#include <sys/socket.h>
#endif

namespace Zquic {

class Endpoint::Cxn_ : public ZiConnection {
friend Endpoint;

public:
  // Stream packetization consumes queued stream ranges before endpoint send;
  // local endpoint backpressure must not reject packets here or data is lost.
  // Keep this as a diagnostic threshold only.
  static constexpr unsigned EndpointTxQueueLimit = 4096;

  struct TxNode : public ZuObject {
    TxNode() = default;
    TxNode(ZmRef<ZiIOBuf> buf_, ZiSockAddr addr_) :
      buf{ZuMv(buf_)}, addr{ZuMv(addr_)} { }

    ZmRef<ZiIOBuf>	buf;
    ZiSockAddr		addr;
  };
  ZuDerive(TxQueue,
    (ZmList<ZmRef<TxNode>,
      ZmListNode<ZmRef<TxNode>,
	ZmListHeapID<"Zquic.Endpoint.TxQueue">>>));

  void *operator new(size_t);
  void operator delete(void *) noexcept;

  Cxn_(Endpoint *endpoint, const ZiCxnInfo &ci, unsigned generation) :
      ZiConnection(endpoint->mx(), ci),
      m_endpoint{endpoint},
      m_generation{generation} { }

  void connected(ZiIOContext &io) override {
    m_endpoint->connected_(this, io);
  }
  void disconnected() override {
    m_endpoint->disconnected_(this);
  }

  unsigned generation() const { return m_generation; }

  bool sendPkt(ZmRef<ZiIOBuf> buf, ZiSockAddr addr) {
    if (m_closing.load_()) return false;
    if (!buf) return false;
    ZiAssert(m_endpoint->m_mx &&
	m_endpoint->m_mx->invoked(m_endpoint->m_mx->txThread()),
      "Zquic", (), "QUIC endpoint send outside Tx thread", return false);
    if (m_txBuf) return enqueueTx_(ZuMv(buf), ZuMv(addr));
    m_txBuf = ZuMv(buf);
    m_txAddr = ZuMv(addr);
    send(ZiIOFn{this, ZmFnPtr<&Cxn_::sendStart_>{}});
    return true;
  }

private:
  void beginCloseRx_() {
    m_closing = true;
  }

  void drainTx_() {
    m_closing = true;
    m_txBuf = nullptr;
    m_txQueue.clean();
  }

  void drainRx_() {
    m_closing = true;
    m_rxBuf = nullptr;
  }

  bool enqueueTx_(ZmRef<ZiIOBuf> buf, ZiSockAddr addr) {
    if (m_txQueue.count_() >= EndpointTxQueueLimit)
      ++m_endpoint->m_txDiag.txBackPressure;
    m_txQueue.push(new TxNode{ZuMv(buf), ZuMv(addr)});
    return true;
  }

  bool dequeueTx_() {
    auto node = m_txQueue.shiftVal();
    if (!node) return false;
    m_txBuf = ZuMv(node->buf);
    m_txAddr = ZuMv(node->addr);
    return true;
  }

  void scheduleTx_() {
    m_endpoint->m_mx->txRun([cxn = ZmMkRef(this)]() mutable {
      if (cxn->m_closing.load_() || !cxn->m_txBuf) return;
      cxn->send(ZiIOFn{cxn.ptr(), ZmFnPtr<&Cxn_::sendStart_>{}});
    });
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
    if (m_closing.load_()) {
      io.complete();
      return true;
    }
    armRecv_(io);
    return true;
  }

  void armRecv_(ZiIOContext &io) {
    m_rxBuf = m_endpoint->allocRxPkt_();
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
      bool hadQueue = m_txQueue.count_();
      m_txBuf = nullptr;
      if (dequeueTx_()) {
	if (hadQueue) m_endpoint->txDrained_();
	io.complete();
	scheduleTx_();
	return true;
      }
      if (hadQueue) m_endpoint->txDrained_();
      io.complete();
      return true;
    }
    if ((io.offset += io.length) < io.size) return true;
    bool hadQueue = m_txQueue.count_();
    m_endpoint->sent_(io.size);
    m_txBuf = nullptr;
    if (dequeueTx_()) {
      if (hadQueue) m_endpoint->txDrained_();
      io.complete();
      scheduleTx_();
      return true;
    }
    if (hadQueue) m_endpoint->txDrained_();
    io.complete();
    return true;
  }

private:
  // Shared immutable identity/back-pointer. Endpoint close/drain must make sure
  // no Cxn_ callbacks or queued sends outlive the Endpoint.
  Endpoint		*m_endpoint = nullptr;
  unsigned		m_generation = 0;
  ZmAtomic<unsigned>	m_closing = 0;

  // Rx-owned receive buffer. Only ZiMultiplex Rx callbacks touch this.
  ZmRef<ZiIOBuf>	m_rxBuf;

  // Tx-owned send buffer and local endpoint send queue. Only ZiMultiplex Tx
  // callbacks and Endpoint::send()'s Tx dispatch target touch these.
  ZmRef<ZiIOBuf>	m_txBuf;
  ZiSockAddr		m_txAddr;
  TxQueue		m_txQueue;
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

Endpoint::Endpoint() = default;

Endpoint::Endpoint(ZiMultiplex *mx) : m_mx{mx} { }

bool Endpoint::init(ZiMultiplex *mx)
{
  ZiAssert(mx, "Zquic", (mx), "null endpoint multiplexer", return false);
  ZiAssert(mx->running(), "Zquic", (mx),
    "endpoint multiplexer is not running", return false);
  if (m_mx) return m_mx == mx;
  m_mx = mx;
  return true;
}

Endpoint::~Endpoint()
{
  if (!m_open.load_()) return;
  if (rxInvoked() || txInvoked()) {
    ZiAssert(false, "Zquic", (),
      "QUIC endpoint destruction from I/O thread while open", return);
  }
  closeUDPWait_();
}

bool Endpoint::openUDP(
  PathMode::T mode,
  ZiIP localIP, uint16_t localPort,
  ZiIP remoteIP, uint16_t remotePort,
  DatagramFn datagramFn,
  ReadyFn readyFn,
  FailFn failFn,
  DownFn downFn,
  TxDrainedFn txDrainedFn)
{
  ZiAssert(m_mx, "Zquic", (), "endpoint multiplexer is not initialized",
    return false);
  ZiMultiplex *mx = m_mx;
  ZiAssert(mx->running(), "Zquic", (mx),
    "endpoint multiplexer is not running", return false);
  if (m_open.load_() || m_cxn) return false;

  m_mode = mode;
  m_local.init(localIP, localPort);
  if (!!remoteIP) m_remote.init(remoteIP, remotePort); else m_remote.null();
  m_sockConfig = SockConfig{
    IPFamily::IPv4, mode, true, true};
  m_sockDiag = {};
  m_datagramFn = ZuMv(datagramFn);
  m_readyFn = ZuMv(readyFn);
  m_failFn = ZuMv(failFn);
  m_downFn = ZuMv(downFn);
  m_listening = false;
  m_open = true;
  ++m_generation;

  m_mx->txInvoke([
    this, generation = m_generation, txDrainedFn = ZuMv(txDrainedFn)
  ]() mutable {
    m_txCxn = nullptr;
    m_txGeneration = generation;
    m_txClosing = false;
    m_txDiag = {};
    m_txDrainedFn = ZuMv(txDrainedFn);
  });

  ZiCxnOptions options;
  options.udp(true);

  m_mx->udp(
    ZiConnectFn{this, [](Endpoint *self, const ZiCxnInfo &ci) -> ZiConnection * {
      auto cxn = new Cxn_{self, ci, self->m_generation};
      self->m_cxn = cxn;
      self->m_mx->txRun([
	self, cxn = ZmMkRef(cxn), generation = self->m_generation
      ]() mutable {
	if (self->m_txGeneration != generation || self->m_txClosing) return;
	self->m_txCxn = ZuMv(cxn);
      });
      return cxn;
    }},
    ZiFailFn{this, [](Endpoint *self, bool transient) {
      self->failed_(transient);
    }},
    localIP, localPort,
    mode == PathMode::ClientConnected ? remoteIP : ZiIP{},
    mode == PathMode::ClientConnected ? remotePort : 0,
    options);

  return true;
}

void Endpoint::closeUDPWait_()
{
  if (!m_open.load_()) return;
  if (rxInvoked()) {
    closeUDP_(nullptr, CloseFn{});
    return;
  }
  if (txInvoked()) {
    m_mx->rxRun([this]() { closeUDP_(nullptr, CloseFn{}); });
    return;
  }

  ZmSemaphore stopped;
  m_mx->rxInvoke([this, &stopped]() {
    closeUDP_(&stopped, CloseFn{});
  });
  stopped.wait();
}

void Endpoint::closeUDP(CloseFn fn)
{
  if (!m_open.load_()) {
    if (!fn) return;
    if (!m_mx || rxInvoked()) {
      fn();
      return;
    }
    m_mx->rxRun([fn = ZuMv(fn)]() mutable { fn(); });
    return;
  }
  if (rxInvoked()) {
    closeUDP_(nullptr, ZuMv(fn));
    return;
  }
  m_mx->rxRun([this, fn = ZuMv(fn)]() mutable {
    closeUDP_(nullptr, ZuMv(fn));
  });
}

void Endpoint::closeUDP_(ZmSemaphore *stopped, CloseFn fn)
{
  m_listening = false;
  m_connected = false;
  m_datagramFn = DatagramFn{};
  m_readyFn = ReadyFn{};
  m_failFn = FailFn{};
  if (stopped) m_closeWaiter = stopped;
  if (fn) m_closeFn = ZuMv(fn);
  if (m_cxn) beginCloseTx_(ZmMkRef(m_cxn));
  else beginCloseTx_();
  if (m_cxn) {
    m_closingCxn = m_cxn;
    m_closingGeneration = m_cxn->generation();
    m_cxn->beginCloseRx_();
    m_cxn->close();
    m_cxn = nullptr;
    return;
  }
  completeClose_();
}

bool Endpoint::send(ZmRef<ZiIOBuf> buf, ZiSockAddr addr)
{
  if (!buf) return false;
  ZiAssert(m_mx, "Zquic", (), "null endpoint multiplexer", return false);
  if (!m_open.load_()) return false;
  m_mx->txRun([this, buf = ZuMv(buf), addr = ZuMv(addr)]() mutable {
    send_(ZuMv(buf), ZuMv(addr));
  });
  return true;
}

void Endpoint::send_(ZmRef<ZiIOBuf> buf, ZiSockAddr addr)
{
  ZiAssert(txInvoked(), "Zquic", (),
    "QUIC endpoint send outside Tx thread", return);
  if (!m_txCxn || m_txClosing) return;
  auto cxn = m_txCxn;
  if (cxn->generation() != m_txGeneration) return;
  (void)cxn->sendPkt(ZuMv(buf), ZuMv(addr));
}

bool Endpoint::connected() const
{
  return m_connected.load_();
}

EndpointDiag Endpoint::diag() const
{
  return {m_rxDiag, m_txDiag};
}

SockDiag Endpoint::sockDiag() const
{
  return m_sockDiag;
}

void Endpoint::connected_(Cxn_ *cxn, ZiIOContext &io)
{
  if (cxn != m_cxn || cxn->generation() != m_generation) return;

  Sock::initUDP(cxn->info().socket, m_sockConfig, &m_sockDiag);

#ifndef _WIN32
  {
    ZiSockAddr local;
    socklen_t len = local.len();
    if (::getsockname(cxn->info().socket, local.sa(), &len) == 0)
      m_local = local;
  }
#endif

  m_listening = true;
  m_connected = true;
  cxn->armRecv_(io);
  if (m_readyFn) m_readyFn(this);
}

void Endpoint::disconnected_(Cxn_ *cxn)
{
  bool active = cxn == m_cxn && cxn->generation() == m_generation;
  bool closing = cxn == m_closingCxn &&
    cxn->generation() == m_closingGeneration &&
    cxn->generation() == m_generation;
  if (active || closing) {
    if (active) beginCloseTx_(ZmMkRef(cxn));
    cxn->drainRx_();
    m_cxn = nullptr;
    if (closing) m_closingCxn = nullptr;
    m_listening = false;
    m_connected = false;
    if (m_downFn) m_downFn(this);
    clearFns_();
    completeClose_();
  }
}

void Endpoint::failed_(bool transient)
{
  ++m_rxDiag.failures;
  m_listening = false;
  m_connected = false;
  if (m_failFn) m_failFn(transient);
  if (m_downFn) m_downFn(this);
  beginCloseTx_();
  clearFns_();
  completeClose_();
}

void Endpoint::received_(Datagram datagram)
{
  ++m_rxDiag.datagramsRx;
  if (datagram.buf) m_rxDiag.bytesRx += datagram.buf->length;
  if (m_datagramFn) m_datagramFn(ZuMv(datagram));
}

void Endpoint::sent_(unsigned bytes)
{
  ++m_txDiag.datagramsTx;
  m_txDiag.bytesTx += bytes;
}

void Endpoint::txDrained_()
{
  if (m_txDrainedFn) m_txDrainedFn();
}

void Endpoint::ioError_()
{
  if (txInvoked())
    ++m_txDiag.failures;
  else
    ++m_rxDiag.failures;
}

void Endpoint::clearFns_()
{
  m_datagramFn = DatagramFn{};
  m_readyFn = ReadyFn{};
  m_failFn = FailFn{};
  m_downFn = DownFn{};
}

void Endpoint::beginCloseTx_(ZmRef<Cxn_> cxn)
{
  if (m_closeTxPending) return;
  m_closeTxPending = true;
  m_mx->txRun([
    this, generation = m_generation, cxn = ZuMv(cxn)
  ]() mutable {
    closeTx_(generation, ZuMv(cxn));
  });
}

void Endpoint::closeTx_(unsigned generation, ZmRef<Cxn_> cxn)
{
  if (m_txGeneration == generation) {
    m_txClosing = true;
    if (m_txCxn && m_txCxn->generation() == generation)
      m_txCxn->drainTx_();
    else if (cxn && cxn->generation() == generation)
      cxn->drainTx_();
    m_txCxn = nullptr;
  }
  m_txDrainedFn = TxDrainedFn{};
  m_mx->txRun([this, generation]() {
    m_mx->rxRun([this, generation]() { closeTxDone_(generation); });
  });
}

void Endpoint::closeTxDone_(unsigned generation)
{
  if (generation == m_generation) m_closeTxPending = false;
  completeClose_();
}

void Endpoint::completeClose_()
{
  if (m_closingCxn || m_closeTxPending) return;
  clearFns_();
  m_open = false;
  auto fn = ZuMv(m_closeFn);
  if (m_closeWaiter) {
    auto waiter = m_closeWaiter;
    m_closeWaiter = nullptr;
    waiter->post();
  }
  if (fn) fn();
}

} // namespace Zquic
