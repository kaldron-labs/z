//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// One TCP connection alternates independent fixed pools and ordinary output.
#include <string.h>
#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZtArray.hh>
#include <zlib/ZiMultiplex.hh>
#include <zlib/ZiRx.hh>

using namespace ZuTestUtil;

class BindingMx;
class BindingCxn : public ZmNHeap<BindingCxn>, public ZiConnection {
friend inline ZuStringT<"Zi.Binding.Connection"> ZmNHeapID(BindingCxn *);
public:
  BindingCxn(BindingMx *, const ZiCxnInfo &);
  void connected(ZiRxContext &) override;
  void disconnected(bool) override;
private:
  bool receive_(ZiRxContext &);
  bool transmit_(ZiTxContext &);
  void queue(ZmRef<ZiIOBuf>);
  BindingMx *const m_owner;
  const bool m_client;
  alignas(Zm::CacheLineSize) ZtArray<ZmRef<ZiIOBuf>> m_pending; // Tx-owned; moved out at selection
  unsigned m_next = 0;
  alignas(Zm::CacheLineSize) unsigned m_received = 0; // Rx-owned
};

class BindingMx : public ZiMultiplex {
public:
  // A, B, Jumbo, A, B: large writes force short sends with the 2 KiB socket
  // send buffer. Four small Rx slots require recycling during the full echo.
  enum { Capacity = 256<<10, Head = ZiIOBuf_Align, Frame = Capacity - Head,
    Frames = 5, Bytes = Frame * Frames, RxSlots = 4 };
  using TxPool = Zi::TxTCPBufAllocator<ZiIOBuf, Capacity,
    ZiIOBuf_DefltMaxSize, ZuStringT<"Zi.Binding.Tx">>;

  ZmRef<TxPool> a = new TxPool{2};
  ZmRef<TxPool> b = new TxPool{2};
  ZmAtomic<unsigned> errors{0}, retired{0}, forwarded{0};
  ZmSemaphore done;
  ZmRef<BindingCxn> client, server;
#if defined(ZiMultiplex_BENCH) && Zi_URing
  // Tx-owner observations; main reads them only after stop joins that owner.
  unsigned fixedA = 0, fixedB = 0, ordinaryForward = 0, partials = 0;
  uintptr_t firstAOp = 0, firstBOp = 0;
  bool matchedA = false, matchedB = false;
#endif

  static ZiMxParams paramsFor(BindingMx *, ZiMxBackend::T, bool);
  BindingMx(ZiMxBackend::T backend, bool shared) :
    ZiMultiplex{paramsFor(this, backend, shared)} { }

  void connectTo(unsigned port) {
    connect({[this](const ZiCxnInfo &ci) { return connection(ci); }},
      {[this](bool) { fail(); }}, ZiIP{"127.0.0.1"}, 0,
      ZiIP{"127.0.0.1"}, port, {});
  }
  void startServer(BindingMx *peer) {
    listen({[peer](const ZiListenInfo &info) { peer->connectTo(info.port); }},
      {[this](bool) { fail(); }},
      {[this](const ZiCxnInfo &ci) { return connection(ci); }},
      ZiIP{"127.0.0.1"}, 0, 1, {});
  }
  ZiConnection *connection(const ZiCxnInfo &ci) {
    auto cxn = new BindingCxn{this, ci};
    (ci.type == ZiCxnType::TCPOut ? client : server) = cxn;
    return cxn;
  }
  void fail() { ++errors; done.post(); }
  bool wait() { return !done.timedwait(Zm::now(30)); }
  void received() { done.post(); }
  void close() {
    rxRun([this] {
      if (client) client->disconnect();
      if (server) server->disconnect();
    });
  }

#if defined(ZiMultiplex_BENCH) && Zi_URing
  void observe(const Zi::IOBenchEvent &event) {
    if (event.kind == Zi::IOBenchKind::TxData) {
      if (event.result >= 0 && unsigned(event.result) < event.count) ++partials;
      if (event.fallback == Zi::IOBenchFallback::RxForward) ++ordinaryForward;
      if (event.fixed && event.ring == a->ring()) {
	++fixedA;
	if (!firstAOp && (event.flags & IORING_CQE_F_MORE)) firstAOp = event.op;
      }
      if (event.fixed && event.ring == b->ring()) {
	++fixedB;
	if (!firstBOp && (event.flags & IORING_CQE_F_MORE)) firstBOp = event.op;
      }
    } else if (event.kind == Zi::IOBenchKind::TxNotif) {
      if (event.ring == a->ring() && event.op == firstAOp) matchedA = true;
      if (event.ring == b->ring() && event.op == firstBOp) matchedB = true;
    }
  }
#endif
};

ZiMxParams BindingMx::paramsFor(BindingMx *owner, ZiMxBackend::T backend, bool shared)
{
  auto params = ZiMxParams{}.backend(backend).txThread(shared ? 1 : 2).txBufSize(2048)
    .tcpBufAllocator(new Zi::RxTCPBufAllocator<ZiRxBuf, 4096,
      ZiIOBuf_DefltMaxSize, ZuStringT<"Zi.Binding.Rx">>{RxSlots});
#if defined(ZiMultiplex_BENCH) && Zi_URing
  params.benchFn([owner](const Zi::IOBenchEvent &event) { owner->observe(event); });
#endif
  return params;
}

BindingCxn::BindingCxn(BindingMx *owner, const ZiCxnInfo &ci) :
    ZiConnection{owner, ci}, m_owner{owner},
    m_client{ci.type == ZiCxnType::TCPOut} { }

void BindingCxn::connected(ZiRxContext &io)
{
  io.init(ZiRxFn{this, ZmFnPtr<&BindingCxn::receive_>{}});
  if (!m_client) return;
  m_owner->txRun([this] {
    for (unsigned i = 0; i < BindingMx::Frames; ++i) {
      auto pool = (i == 0 || i == 3) ? m_owner->a.ptr() : m_owner->b.ptr();
      auto buf = pool->alloc(nullptr, 0);
      if (i == 2) {
	if (!buf->ensure(buf->size + 1)) { m_owner->fail(); return; }
      } else if (Zi::uring() &&
	  (!buf->uring() || buf->ring() != pool->ring())) {
	m_owner->fail(); return;
      }
      buf->skip = BindingMx::Head;
      buf->length = BindingMx::Frame;
      memset(buf->data(), int(i + 1), buf->length);
      queue(ZuMv(buf));
    }
  });
}

void BindingCxn::queue(ZmRef<ZiIOBuf> buf)
{
  // send() moves this original reference to the Tx owner, then into its queue.
  send(ZiTxFn{[this, buf = ZuMv(buf)](ZiTxContext &io) mutable {
    m_pending.push(ZuMv(buf));
    if (!io.completed()) return true;
    io.init(ZiTxFn{this, ZmFnPtr<&BindingCxn::transmit_>{}});
    return io();
  }});
}

bool BindingCxn::transmit_(ZiTxContext &io)
{
  if (io.buf) {
    if (io.length < 0 || io.buf->length) {
      m_owner->fail(); io.complete(); return true;
    }
    if (m_client) ++m_owner->retired;
    io.buf = nullptr; // keep selection armed for pending output on epoll too
  } else if (m_next < m_pending.length()) {
    io.init(ZiTxFn{this, ZmFnPtr<&BindingCxn::transmit_>{}},
      ZuMv(m_pending[m_next++]));
  } else io.complete();
  return true;
}

bool BindingCxn::receive_(ZiRxContext &io)
{
  if (!io.buf) { m_owner->fail(); return true; }
  for (auto c : io.buf->cspan()) {
    if (m_received >= BindingMx::Bytes ||
	c != uint8_t(m_received / BindingMx::Frame + 1)) {
      m_owner->fail(); io.disconnect(); return true;
    }
    ++m_received;
  }
  if (m_client) {
    if (m_received == BindingMx::Bytes) m_owner->received();
    io.buf = nullptr; // consumed original input must return to the Rx pool
  } else {
    // No slice, allocation, copy or replacement descriptor at the echo handoff.
    ++m_owner->forwarded;
    queue(ZuMv(io.buf));
  }
  return true;
}

void BindingCxn::disconnected(bool)
{
  m_owner->txRun([this] { m_pending.null(); });
}

static void run(ZiMxBackend::T backend, bool shared)
{
  ZuTestScopeRT(run);
  // Distinct applications own distinct provided Rx pools. Sharing one tiny
  // pool between both ends of a self-echo can retain every slot in blocked
  // server output while preventing the client from draining its receive window.
  BindingMx client{backend, shared}, server{backend, shared};
  bool started = client.start() && server.start();
  ZuCheckRT(started);
  if (!started) { server.stop(); client.stop(); return; }
  ZuCheckRT(client.backend() == backend && server.backend() == backend);
  server.startServer(&client);
  bool received = client.wait();
  ZuCheckRT(received);
  server.close();
  ZuCheckRT(server.stop());
  client.close();
  ZuCheckRT(client.stop());
  ZuCheckRT(!client.errors.load_() && !server.errors.load_());
  ZuCheckRT(client.retired.load_() == BindingMx::Frames);
  ZuCheckRT(server.forwarded.load_() > 1);
  ZuCheckRT(client.a->refCount() == 1 && client.b->refCount() == 1);
  log("backend=", int(backend), " shared=", shared,
    " retired=", client.retired.load_(), " forwarded=", server.forwarded.load_());
#if defined(ZiMultiplex_BENCH) && Zi_URing
  if (backend == ZiMxBackend::URing) {
    ZuCheckRT(client.fixedA && client.fixedB);
    ZuCheckRT(server.ordinaryForward && client.partials);
    ZuCheckRT(client.matchedA && client.matchedB);
    log("fixed-a=", client.fixedA, " fixed-b=", client.fixedB,
      " Rx-forward=", server.ordinaryForward, " short-sends=", client.partials);
  }
#endif
  client.client = nullptr; server.server = nullptr;
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  {
    ZuTestRT(multibinding);
#if Zi_URing
    ZuCheckRT(Zi::uring());
#endif
    for (bool shared : {false, true}) {
      ZuTestCallRT(run, ZiMxBackend::EPoll, shared);
      if (Zi::uring()) ZuTestCallRT(run, ZiMxBackend::URing, shared);
    }
  }
  return 0;
}
