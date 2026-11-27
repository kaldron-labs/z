//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// socket I/O multiplexing

#include <new>

#include <zlib/ZmAssert.hh>
#include <zlib/ZmBlock.hh>
#include <zlib/ZmGuard.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmRBTree.hh>
#include <zlib/ZmRWLock.hh>
#include <zlib/ZmSingleton.hh>
#include <zlib/ZmScratch.hh>

#include <zlib/ZtArray.hh>

#include <zlib/ZiLog.hh>

#include <zlib/ZiMultiplex.hh>
#include <zlib/ZiIOContext.hh>
#include <zlib/ZiTxStream.hh>
#include <zlib/ZiAssert.hh>

#if defined(__linux__)
#include "ZiMxEpoll.hh"
#elif defined(_WIN32)
#include "ZiMxIOCP.hh"
#endif

ZtEnumImplNS(ZiCxnFlags);
ZtEnumImplNS(ZiCxnType);
ZtEnumImplNS(ZiIOState);

namespace Zi {
  ZtEnumImplNS(SendFlags);
}

class ZiMxMgr_ {
friend Ztc::MxMgr;

  using Lock = ZmRWLock;
  using Guard = ZmGuard<Lock>;
  using ReadGuard = ZmReadGuard<Lock>;

  ZmRBTreeDerive(Map, ZiMultiplex *,
    ZmRBTreeUnique<true,
      ZmRBTreeLock<ZmNoLock,
	ZmRBTreeHeapID<"Ztc.MxMgr">>>);
  using Captures =
    ZtArray<Ztc::MxTelemetry,
      ZtArrayHeapID<"Ztc.MxMgr.Capture">>;

public:
  ~ZiMxMgr_() {
    m_addFn = {};
    m_delFn = {};
  }

  static ZiMxMgr_ *instance() {
    return
      ZmSingleton<ZiMxMgr_,
	ZmSingletonCleanup<ZmCleanup::Library>>::instance();
  }

  void add(ZiMultiplex *mx) {
    Guard guard(m_watchLock);
    m_map.add(mx);
    if (m_addFn) m_addFn(mx);
  }
  void del(ZiMultiplex *mx) {
    Guard guard(m_watchLock);
    if (m_delFn) m_delFn(mx);
    m_map.del(mx);
  }

  unsigned all(Ztc::MxMgr::AllFn fn) const {
    ReadGuard guard(m_watchLock);
    unsigned n = 0;
    auto i = m_map.citer();
    while (auto mx = i.key()) {
      ++n;
      fn(mx);
    }
    return n;
  }

  void capture(Ztc::MxMgr::CaptureFn fn) const {
    ReadGuard guard(m_watchLock);
    unsigned count = m_map.count_();
    auto captures = ZmScratch(
      Ztc::MxTelemetry, count, Captures::VHeap);
    {
      auto i = m_map.citer();
      while (auto mx = i.key()) {
	auto data = new (captures.push()) Ztc::MxTelemetry;
	mx->telemetry(*data);
      }
    }
    guard.unlock();
    fn(captures.cspan());
  }

  void watch(Ztc::MxMgr::AddFn addFn, Ztc::MxMgr::DelFn delFn) {
    Guard guard(m_watchLock);
    ZmAssert(!m_addFn && !m_delFn, return);
    m_addFn = ZuMv(addFn);
    m_delFn = ZuMv(delFn);
  }

  void unwatch() {
    Guard guard(m_watchLock);
    m_addFn = {};
    m_delFn = {};
  }

private:
  Lock &watchLock() { return m_watchLock; }

  Map			m_map;
  mutable Lock		m_watchLock;
  Ztc::MxMgr::AddFn	m_addFn;
  Ztc::MxMgr::DelFn	m_delFn;
};

#define Log(severity, op, result, error) \
  ZiLOG(severity, "ZiMultiplex", \
    ([op_ = op, result_ = result, error_ = error](auto &s) { \
    s << op_ << ' ' << Zi::ioResult(result_) << ' ' << error_; \
  }))
#define Error(op, result, error) Log(Error, op, result, error)
#define Warning(op, result, error) Log(Warning, op, result, error)

#ifdef ZiMultiplex_DEBUG
ZiMultiplex::DebugBuf ZiMultiplex::debugBuf(const void *data, unsigned length)
{
  return DebugBuf{static_cast<const uint8_t *>(data), length};
}
#endif

ZiIP ZiMultiplex::wildcard(ZiIPType::T type)
{
  switch (type) {
    case ZiIPType::V6:
      return ZiIP{in6addr_any};
    default: {
      in_addr addr;
      addr.s_addr = 0;
      return ZiIP{addr};
    }
  }
}

bool ZiMultiplex::cxnType(ZiIP localIP, ZiIP remoteIP, ZiIPType::T &type)
{
  if (!!localIP && !!remoteIP && localIP.type() != remoteIP.type())
    return false;
  if (!!localIP)
    type = localIP.type();
  else if (!!remoteIP)
    type = remoteIP.type();
  else
    type = ZiIPType::V4;
  return true;
}


ZiConnection::ZiConnection(ZiMultiplex *mx, const ZiCxnInfo &info) :
  m_mx(mx), m_info(info), m_rxUp(1), m_txUp(1)
{
  m_rxContext.cxn = m_txContext.cxn = this;
}

ZiConnection::~ZiConnection()
{
  // precautionary code to ensure no leaking of sockets

  if (!Zi::nullSocket(m_info.socket)) {
    ZiLOG(Warning, "ZiMultiplex", "~ZiConnection() called with socket still open");
    Zi::closeSocket(m_info.socket);
  }
}

void ZiMultiplex::udp(ZiConnectFn fn, ZiFailFn failFn,
  ZiIP localIP, uint16_t localPort,
  ZiIP remoteIP, uint16_t remotePort,
  Zi::IOBufAllocator *rxAllocator, ZiCxnOptions options)
{
  if (ZuUnlikely(!running())) {
    Error("udp", Zi::NotReady, ZeOK);
    failFn(false);
    return;
  }

  rxInvoke([this, fn = ZuMv(fn), failFn = ZuMv(failFn),
      localIP, localPort, remoteIP, remotePort, rxAllocator,
      options = ZuMv(options)]() mutable {
    udp_(ZuMv(fn), ZuMv(failFn),
	localIP, localPort, remoteIP, remotePort, rxAllocator, ZuMv(options));
  });
}

void ZiMultiplex::udp_(ZiConnectFn fn, ZiFailFn failFn,
  ZiIP localIP, uint16_t localPort,
  ZiIP remoteIP, uint16_t remotePort,
  Zi::IOBufAllocator *rxAllocator, ZiCxnOptions options)
{
  ZiAssert(invoked(rxThread()), "ZiMultiplex", (), "Rx exclusive", return);
  ZiAssert(rxAllocator && rxAllocator->mx == this && rxAllocator->type == Zi::IOBufType::Rx,
    "ZiMultiplex", (), "Rx allocator", return);

  if (ZuUnlikely(!running())) {
    Error("udp", Zi::NotReady, ZeOK);
    failFn(false);
    return;
  }

  if (ZuUnlikely(!options.udp())) {
    Error("udp", Zi::IOError, ZeError(ZiEINVAL));
    failFn(false);
    return;
  }

  ZiIPType::T ipType;

  if (!cxnType(localIP, remoteIP, ipType)) {
    Error("udp", Zi::IOError, ZeError(ZiEINVAL));
    failFn(false);
    return;
  }

  if (options.multicast() && !options.multicastValid(ipType)) {
    Error("udp", Zi::IOError, ZeError(ZiEINVAL));
    failFn(false);
    return;
  }

  m_plugin->udp(ZuMv(fn), ZuMv(failFn), ipType,
    localIP, localPort, remoteIP, remotePort, rxAllocator, ZuMv(options));
}

void ZiMultiplex::connect(
  ZiConnectFn fn, ZiFailFn failFn, ZiIP localIP, uint16_t localPort,
  ZiIP remoteIP, uint16_t remotePort,
  Zi::IOBufAllocator *rxAllocator, ZiCxnOptions options)
{
  if (ZuUnlikely(!running())) {
    Error("connect", Zi::NotReady, ZeOK);
    failFn(false);
    return;
  }

  rxInvoke([this, fn = ZuMv(fn), failFn = ZuMv(failFn),
      localIP, localPort, remoteIP, remotePort, rxAllocator,
      options = ZuMv(options)]() mutable {
    connect_(ZuMv(fn), ZuMv(failFn),
	localIP, localPort, remoteIP, remotePort, rxAllocator, ZuMv(options));
  });
}

void ZiMultiplex::connect_(ZiConnectFn fn, ZiFailFn failFn,
  ZiIP localIP, uint16_t localPort,
  ZiIP remoteIP, uint16_t remotePort,
  Zi::IOBufAllocator *rxAllocator, ZiCxnOptions options)
{
  ZiAssert(invoked(rxThread()), "ZiMultiplex", (), "Rx exclusive", return);
  ZiAssert(rxAllocator && rxAllocator->mx == this && rxAllocator->type == Zi::IOBufType::Rx,
    "ZiMultiplex", (), "Rx allocator", return);

  if (ZuUnlikely(!running())) {
    Error("connect", Zi::NotReady, ZeOK);
    failFn(false);
    return;
  }

  if (ZuUnlikely(options.udp())) {
    Error("connect", Zi::IOError, ZeError(ZiEINVAL));
    failFn(false);
    return;
  }

  ZiIPType::T ipType = ZiIPType::V4;

  if (!remoteIP || !cxnType(localIP, remoteIP, ipType)) {
    Error("connect", Zi::IOError, ZeError(ZiEINVAL));
    failFn(false);
    return;
  }

  m_plugin->connect(ZuMv(fn), ZuMv(failFn), ipType,
    localIP, localPort, remoteIP, remotePort, rxAllocator, ZuMv(options));
}

void ZiMultiplex::listen(
  ZiListenFn listenFn, ZiFailFn failFn, ZiConnectFn acceptFn,
  ZiIP localIP, uint16_t localPort, unsigned nAccepts,
  Zi::IOBufAllocator *rxAllocator, ZiCxnOptions options)
{
  if (ZuUnlikely(!running())) {
    Error("listen", Zi::NotReady, ZeOK);
    failFn(false);
    return;
  }

  rxInvoke([this, listenFn = ZuMv(listenFn),
      failFn = ZuMv(failFn), acceptFn = ZuMv(acceptFn),
      localIP, localPort, nAccepts, rxAllocator, options = ZuMv(options)]() mutable {
    listen_(ZuMv(listenFn), ZuMv(failFn), ZuMv(acceptFn),
	localIP, localPort, nAccepts, rxAllocator, ZuMv(options));
  });
}

void ZiMultiplex::listen_(
  ZiListenFn listenFn, ZiFailFn failFn, ZiConnectFn acceptFn,
  ZiIP localIP, uint16_t localPort, unsigned nAccepts,
  Zi::IOBufAllocator *rxAllocator, ZiCxnOptions options)
{
  ZiAssert(invoked(rxThread()), "ZiMultiplex", (), "Rx exclusive", return);
  ZiAssert(rxAllocator && rxAllocator->mx == this && rxAllocator->type == Zi::IOBufType::Rx,
    "ZiMultiplex", (), "Rx allocator", return);

  if (ZuUnlikely(!running())) {
    Error("listen", Zi::NotReady, ZeOK);
    failFn(false);
    return;
  }

  m_plugin->listen(ZuMv(listenFn), ZuMv(failFn), ZuMv(acceptFn),
    localIP, localPort, nAccepts, rxAllocator, ZuMv(options));
}

void ZiMultiplex::stopListening(ZiIP localIP, uint16_t localPort)
{
  if (ZuUnlikely(!running())) return;

  rxInvoke([this, localIP, localPort]() {
    stopListening_(localIP, localPort);
  });
}

void ZiMultiplex::stopListening_(ZiIP localIP, uint16_t localPort)
{
  m_plugin->stopListening(localIP, localPort);
}

void ZiConnection::recv(ZiRxFn fn)
{
  m_mx->rxInvoke(this, [this, fn = ZuMv(fn)]() mutable {
    recv_(ZuMv(fn));
    return this;
  });
}

void ZiConnection::recv_(ZiRxFn fn)
{
  ZiAssert(m_mx->invoked(m_mx->rxThread()), "ZiMultiplex", (), "Rx exclusive", return);

  m_rxContext.init_(ZuMv(fn));

  if (ZuUnlikely(m_rxContext.completed())) return;

  m_mx->plugin()->recv(this);
}

void ZiConnection::errorRecv(const char *op, int status, ZeError e)
{
  bool peer = status == Zi::IOError &&
    (e.errNo() == ZiENOTCONN || e.errNo() == ZiECONNRESET);
  if (m_rxUp.load_()) close(peer);
  if (status == Zi::IOError &&
      (e.errNo() == ZiENOTCONN || e.errNo() == ZiECONNRESET)) return;
  Error(op, Zi::IOError, e);
}

void ZiConnection::executedRecv(unsigned n)
{
  ZiDEBUG(m_mx, "executedRecv()");

  ZmAssert(!m_rxContext.completed());

#ifdef ZiMultiplex_FILTER
  if (ZuUnlikely(m_mx->filterRx(
	  this, m_rxContext.buf->end(), n)))
    return;
#endif

  m_rxContext.buf->length += n;
  while (!m_rxContext());
}

void ZiConnection::send(ZiTxFn fn)
{
  m_mx->txInvoke([this, fn = ZuMv(fn)]() mutable {
    send_(ZuMv(fn));
    return this;
  });
}

void ZiConnection::send_(ZiTxFn fn)
{
  ZiAssert(m_mx->invoked(m_mx->txThread()), "ZiMultiplex", (), "Tx owner", return);
  if (ZuUnlikely(m_txContext.state || !m_txUp.load_())) return;
  bool ready = fn(m_txContext);
  if (ready || m_txContext.state) send();
}

void ZiTxContext::complete()
{
  if (ZuUnlikely(disconnected() && length >= 0 && (!cxn || cxn->txUp()) &&
      ((state & ZiIOState::Partial()) || (buf && offset < buf->length)))) return;
  fn = {};
  buf = nullptr;
  if (cxn && this == &cxn->txContext()) cxn->syncDone();
}

void ZiConnection::syncDone()
{
  if (!m_sync) return;
  ZiAssert(m_mx->invoked(m_mx->txThread()), "ZiMultiplex", (), "Tx owner", return);
  // Clear before waking: a caller may immediately post another owner operation.
  auto done = ZuMv(m_sync);
  if (done) done();
}

void ZiConnection::sync()
{
  ZiAssert(!m_mx->invoked(m_mx->rxThread()) && !m_mx->invoked(m_mx->txThread()),
    "ZiMultiplex", (), "sync() requires a non-I/O caller", return);
  ZmBlock<>{}([this](auto wake) {
    m_mx->txInvoke([this, wake = ZuMv(wake)]() mutable {
      if (m_txContext.completed()) { wake(); return; }
      m_sync = [previous = ZuMv(m_sync), wake = ZuMv(wake)]() mutable {
	if (previous) previous();
	wake();
      };
      // EAGAIN or plugin backpressure returns to the owner queue. The central
      // context completion path releases waiters after success, error or close.
      send();
    });
  });
}

void ZiConnection::send()
{
#ifdef ZiMultiplex_DEBUG
  if (m_mx->trace()) m_mx->traceCapture();
#endif

  if (ZuUnlikely(!m_txUp.load_())) { m_txContext.complete(); return; }

  if (ZuUnlikely(m_txContext.closed())) { close_1(false); return; }
  if (m_txContext.completed() || !m_txContext.buf ||
      (m_txContext.disconnected() && m_txContext.offset >= m_txContext.buf->length)) {
    if (m_txContext.disconnected()) {
      m_txContext.complete();
      disconnect_1(false);
    }
    return;
  }

  m_mx->plugin()->send(this);
}

void ZiConnection::errorSend(const char *op, int status, ZeError e)
{
  bool datagramError = m_info.options.udp() && status == Zi::IOError &&
    (e.errNo() == ZiECONNREFUSED || e.errNo() == ZiECONNRESET) && !m_txContext.state;
  if (!datagramError) m_txContext.state = ZiIOState::Close();
  m_txContext.length = -1;
  if (!m_txContext.completed()) m_txContext();
  if (datagramError && !m_txContext.state) return;
  bool peer = status == Zi::IOError &&
    (e.errNo() == ZiENOTCONN || e.errNo() == ZiECONNRESET);
  close_1(peer);
  if (status == Zi::IOError &&
      (e.errNo() == ZiENOTCONN || e.errNo() == ZiECONNRESET)) return;
  Error(op, status, e);
}

bool ZiConnection::executedSend(unsigned n)
{
  ZiDEBUG(m_mx, "executedSend()");

  auto &io = m_txContext;
  auto original = io.buf.ptr();
  unsigned accepted = io.offset + n;
  bool final = io.disconnected();
  io.length = n;
  if (ZuUnlikely(final && accepted < io.buf->length)) {
    // A completion can move or replace the public buffer and callback. Keep
    // the original suffix and sync waiter until the transport accepts it.
    auto backing = io.buf;
    auto callback = io.fn;
    io.state |= ZiIOState::Partial();
    while (!io() && !io.closed() && io.buf == original && io.fn == callback) { }
    if (!m_txUp.load_()) return false;
    if (io.closed()) { close_1(false); return false; }
    io.state = ZiIOState::Disconnect();
    io.buf = ZuMv(backing);
    io.fn = ZuMv(callback);
    io.offset = accepted;
    return true;
  }
  // An ordinary completion can promote and mark the next queued buffer.
  while (!io.completed() && !io() && (!final || io.buf.ptr() == original)) { }
  if (!m_txUp.load_()) return false;
  if (ZuUnlikely(io.closed())) { close_1(false); return false; }
  if (ZuUnlikely(final)) {
    // The transport accepted the entire original range. Drop any replacement
    // installed by its completion before dispatching the graceful teardown.
    io.buf = nullptr;
    io.complete();
    disconnect_1(false);
    return false;
  }
  if (ZuUnlikely(io.disconnected()) && io.buf.ptr() == original && io.length == int(n))
    io.offset = accepted;
  if (!io.completed() && io.buf && io.offset < io.buf->length) return true;
  io.complete();
  if (ZuUnlikely(io.disconnected())) disconnect_1(false);
  return false;
}

void ZiConnection::disconnect()
{
  disconnect(false);
}

void ZiConnection::disconnect(bool peer)
{
  m_mx->txInvoke(this, [this, peer]() { disconnect_1(peer); return this; });
}

void ZiConnection::disconnect_1(bool peer)
{
  m_mx->plugin()->disconnect(this, peer);
}

void ZiConnection::notifyDisconnected(bool peer)
{
  if (Zi::nullSocket(m_info.socket)) return;
  Zi::closeSocket(m_info.socket);
  m_info.socket = Zi::nullSocket();
  disconnected(peer);
}

void ZiConnection::close()
{
  close(false);
}

void ZiConnection::close(bool peer)
{
  m_mx->txInvoke(this, [this, peer]() { close_1(peer); return this; });
}

void ZiConnection::close_1(bool peer)
{
  m_mx->plugin()->close(this, peer);
}

ZmRef<ZiMxPlugin> ZiMultiplex::defltPlugin(const ZiMxParams &params)
{
#if defined(__linux__)
  return new ZiMxEpoll(params);
#elif defined(_WIN32)
  return new ZiMxIOCP(params);
#else
#error No default ZiMultiplex plugin for this platform
#endif
}

ZiMultiplex::ZiMultiplex(ZiMxParams mxParams, ZmRef<ZiMxPlugin> plugin) :
  ZmScheduler(ZuMv(mxParams.scheduler())),
  m_plugin(plugin ? ZuMv(plugin) : defltPlugin(mxParams)),
  m_kernel(m_plugin->kernel()),
  m_rxThread(mxParams.rxThread()),
  m_txThread(mxParams.txThread()),
  m_rxBufSize(mxParams.rxBufSize()),
  m_txBufSize(mxParams.txBufSize()),
  m_rxQuantum(mxParams.rxQuantum())
#ifdef ZiMultiplex_DEBUG
  , m_trace(mxParams.trace()),
  m_debug(mxParams.debug()),
  m_frag(mxParams.frag()),
  m_yield(mxParams.yield())
#endif
{
  if (!params().thread(m_rxThread).name())
    params_().thread(m_rxThread).name("ioRx");
  if (!params().thread(m_txThread).name())
    params_().thread(m_txThread).name("ioTx");

  m_plugin->mx(this);
  try {
    m_plugin->mxBound();
    ZiMxMgr_::instance()->add(this);
  } catch (...) {
    m_plugin->mx(nullptr);
    throw;
  }
}

ZiMultiplex::~ZiMultiplex()
{
  ZiMxMgr_::instance()->del(this);
  ZmAssert_(state() == ZmEngineState::Stopped);
  m_plugin->final();
  m_plugin->mx(nullptr);
  // Registrations and allocator storage survive stop until final teardown.
  m_rxPool = nullptr;
  m_txPools.clean();
  m_allocators.clean();
}

Zi::IOBufPool *ZiMultiplex::bufPool(
  Zi::IOBufType::T type, unsigned size, unsigned align, unsigned count)
{
  if (type == Zi::IOBufType::Rx && m_rxPool) return m_rxPool;
  auto pool = m_plugin->allocator(type, size, align, count);
  if (!pool) return nullptr;
  ZmAssert_(pool->type == type);
  if (type == Zi::IOBufType::Rx)
    m_rxPool = pool;
  else {
    {
      auto iter = m_txPools.citer();
      while (auto tx = iter())
	if (tx == pool) return pool;
    }
    m_txPools.pushNode(pool);
  }
  return pool;
}

ZuUnion<void, ZeException> ZiMultiplex::alloc()
{
  m_registration = AllocatorsClosed;
  if (m_allocated) return {};
  if (!m_kernel && m_plugin->deferredBacking()) {
    auto result = m_plugin->alloc(m_rxPool, m_txPools);
    if (result.is<ZeException>()) return result;
  }
  m_allocated = true;
  return {};
}

bool ZiMultiplex::start()
{
  m_registration = AllocatorsClosed;
  return ZmScheduler::start();
}

bool ZiMultiplex::start__()
{
  if (!m_allocated) {
    auto result = alloc();
    if (result.is<ZeException>()) {
      ZiLogEvent(ZuMv(result).p<ZeException>());
      return false;
    }
  }
  if (!m_rxQuantum || m_rxQuantum > unsigned(INT_MAX)) {
    Error("rxQuantum", Zi::NotReady, ZeError{ZiEINVAL});
    return false;
  }

  if (!ZmScheduler::start__()) return false;
  auto &starting = ZmTLS<ZmSemaphore, &ZiMultiplex::start__>();
  m_starting = &starting;
  m_pluginFailed = false;
  m_plugin->start();
  starting.wait();
  m_starting = nullptr;
  if (m_pluginFailed) {
    ZmScheduler::stop__();
    m_plugin->stopped();
    return false;
  }

  m_polling = true;
  wakeFn(rxThread(),
    WakeFn{this, [](ZiMultiplex *mx) { mx->wakeRx(); }});
  push([this]() { rx(); }, rxThread());

  return true;
}

void ZiMultiplex::pluginStarted(ZiMxPlugin::StartResult result)
{
  if (result.is<ZeException>()) {
    ZiLogEvent(ZuMv(result).p<ZeException>());
    m_pluginFailed = true;
  }
  m_starting->post();
}

bool ZiMultiplex::stop()
{
  wake();
  return ZmScheduler::stop();
}

// the scheduler's control thread synchronously blocks on shutdown
bool ZiMultiplex::stop__()
{
  auto &stopping = ZmTLS<ZmSemaphore, &ZiMultiplex::stop__>();

  m_stopping = &stopping;

  // Keep polling while the plugin drains I/O on the worker threads.
  m_plugin->stop();
  stopping.wait();

  m_polling = false;
  bool ok = ZmScheduler::stop__();
  wakeFn(rxThread(), WakeFn{});
  m_plugin->stopped();

  m_stopping = nullptr;

  return ok;
}

void ZiMultiplex::pluginStopped(ZiMxPlugin::StopResult result)
{
  if (result.is<ZeException>())
    ZiLogEvent(ZuMv(result).p<ZeException>());
  m_stopping->post();
}

void ZiMultiplex::wake()
{
  ZiDEBUG(this, "wake");

  m_plugin->wake();
}

void ZiMultiplex::wakeRx()
{
  if (m_polling) push([this]() { rx(); }, rxThread());
  wake();
}

void ZiMultiplex::telemetry(Ztc::MxTelemetry &data) const
{
  data.id = params().id();
  data.stackSize = params().stackSize();
  data.queueSize = params().queueSize();
  data.spin = params().spin();
  data.timeout = params().timeout();
  data.rxBufSize = rxBufSize();
  data.txBufSize = txBufSize(),
  data.rxThread = rxThread();
  data.txThread = txThread();
  data.partition = params().partition();
  data.state = state();
  data.ll = params().ll();
  data.priority = params().priority();
  data.nThreads = params().nThreads();
}

unsigned Ztc::MxMgr::all(AllFn fn)
{
  return ZiMxMgr_::instance()->all(ZuMv(fn));
}

ZmRWLock &Ztc::MxMgr::watchLock_()
{
  return ZiMxMgr_::instance()->watchLock();
}

void Ztc::MxMgr::capture(CaptureFn fn)
{
  ZiMxMgr_::instance()->capture(ZuMv(fn));
}

void Ztc::MxMgr::watch(AddFn addFn, DelFn delFn)
{
  ZiMxMgr_::instance()->watch(ZuMv(addFn), ZuMv(delFn));
}

void Ztc::MxMgr::unwatch()
{
  ZiMxMgr_::instance()->unwatch();
}
