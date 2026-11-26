//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef ZutcpUDPLeaseTest_HH
#define ZutcpUDPLeaseTest_HH

#include <zlib/ZiTx.hh>

#include "ZutcpTestUDPDevice.hh"

// Supply deterministic next-hop changes to the adapter's real asynchronous
// resolver handoff. The private-network integration tests use the kernel.
static ZmAtomic<unsigned> nextHop{10};

namespace Zutcp {
ZuUnion<IPPath, ZeException> resolveUDPPath(unsigned, IPPath route)
{
  auto result = path(route.remote.v6());
  result.remote = route.remote;
  result.remoteMAC[5] = nextHop.load_();
  return result;
}
}

namespace UDPLeaseTest {

class Cxn : public ZmNHeap<Cxn>, public ZiConnection, public ZiTx<Cxn> {
friend inline ZuStringT<"Zutcp.UDPTest.Cxn"> ZmNHeapID(Cxn *);
public:
  using ZmNHeap<Cxn>::operator new;
  using ZmNHeap<Cxn>::operator delete;
  Cxn(ZiMultiplex *mx, const ZiCxnInfo &info) : ZiConnection{mx, info} { }
  void connected(ZiRxContext &) override { }
  void disconnected(bool) override {
    mx()->txRun([cxn = ZmRef{this}] {
      cxn->resetTxQueue();
      cxn->closed.post();
    });
  }
  void sending(ZiTxContext &io) {
    io.addr = info().remote;
    io.tos = ZiTOS{uint8_t(0xab)};
  }
  void sent(ZmRef<ZiIOBuf> buf, bool ok) {
    auto &io = txContext();
    ZmAssert_(buf.ptr() == io.buf.ptr());
    results.push(io.length);
    if (ok) {
      ZmAssert_(buf->length == 1 && io.length == 1);
      bytes.push(buf->data()[0]);
    }
    completed.post();
  }
  void enqueue(Zi::IOBufAllocator *tx, char value, bool finish = false) {
    auto buf = tx->allocBuf(this);
    ZmAssert_(buf && buf->append(ZuSpan{&value, 1U}));
    ZiTx<Cxn>::send(ZuMv(buf), finish);
  }

  ZtArray<int> results;
  ZtArray<uint8_t> bytes;
  ZmSemaphore completed, closed;
};

struct Fixture {
  ZmRef<Zpkt::Env> env = ZutcpTest::fixtureEnv();
  ZmRef<Zutcp::MxPlugin> stack = new Zutcp::MxPlugin;
  ZiMultiplex mx{ZiMxParams{}.scheduler([](auto &s) { s.nThreads(3); })
    .rxThread(1).txThread(2), stack};
  ZmRef<Device> device = new Device{&mx};
  Zi::IOBufAllocator *tx = nullptr, *app = nullptr;
  ZmAtomic<unsigned> loops{0};

  bool start(unsigned retries, ZuTime backoff) {
    Zutcp::MxParams config;
    config.txRetries = retries;
    config.txBackoff = backoff;
    config.packet = [this](ZmRef<ZiIOBuf>) { ++loops; };
    stack->init(&mx, env, ZmRef<Zpkt::Nif>{device}, ZuMv(config));
    tx = mx.allocator<Zi::IOBufType::TxUDP, ZiIOBuf, 128>(8);
    app = mx.allocator<Zi::IOBufType::App, ZiIOBuf, 128>();
    return tx && app && mx.start();
  }
  template <typename Fn> void onTx(Fn fn) {
    ZmBlock<>{}([&](auto done) {
      mx.txRun([&fn, done = ZuMv(done)]() mutable { fn(); done(); });
    });
  }
  void fence() {
    ZmBlock<>{}([this](auto done) {
      mx.rxRun([this, done = ZuMv(done)]() mutable { mx.txRun(ZuMv(done)); });
    });
  }
  ZmRef<Cxn> open(Zutcp::IPPath route, bool cached = true) {
    return ZmBlock<ZmRef<Cxn>>{}([&](auto done) {
      mx.rxRun([&, done = ZuMv(done)]() mutable {
        ZiCxnInfo info{.type = ZiCxnType::UDP, .socket = Zi::nullSocket(),
          .options = ZiCxnOptions{}.udp(true), .local = {route.local, 2381},
          .remote = {route.remote, 49252}};
        ZmRef<Cxn> cxn = new Cxn{&mx, info};
        bool ok = ZutcpTest::MxPlugin::publishUDP(*stack,
          ZiConnectFn{[&cxn](const ZiCxnInfo &) { return cxn.ptr(); }},
          info, cached ? route : Zutcp::IPPath{});
        done(ok ? ZuMv(cxn) : ZmRef<Cxn>{});
      });
    });
  }
  bool expire(Cxn *cxn) { return ZutcpTest::MxPlugin::expireUDP(*stack, cxn); }
  void retire() {
    device->retire = true;
    stack->reconcileTx();
    device->retire = false;
  }
  bool wire(unsigned i, const Zutcp::IPPath &route, char value) {
    if (i >= device->frames.length()) return false;
    const auto &bytes = device->frames[i];
    Zutcp::IPView ip;
    Zutcp::UDPView udp;
    return Zutcp::readIP({bytes.data(), bytes.length()}, ip) && ip.local == route.remote &&
      ip.tos == 0xab && ip.ecn == 3 && Zutcp::readUDP(ip, udp) &&
      udp.dstPort == 49252 && udp.payload.length() == 1 && udp.payload[0] == value;
  }
};
} // UDPLeaseTest

static void testUDPLeases()
{
  using UDPLeaseTest::Fixture;
  ZuTestScopeRT(testUDPLeases);
  Fixture f;
  bool started = f.start(2, ZuTime{60});
  ZuCheckRT(started, log_("start UDP adapter with independent registered slots"));
  if (!started) return;
  f.onTx([&] { ZutcpTest::MxPlugin::udpRouteEpoch(*f.stack); });

  for (bool ipv6 : {false, true}) {
    auto route = path(ipv6);
    auto cxn = f.open(route);
    bool ok = !!cxn;
    if (!ok) break;
    unsigned i = 0;
    f.onTx([&] {
      f.device->early = ipv6;
      f.device->refuse = true;
      auto calls = f.device->calls;
      i = f.device->frames.length();
      cxn->enqueue(f.tx, 'A');
      auto head = cxn->txContext().buf.ptr();
      cxn->enqueue(f.tx, 'B');
      f.stack->send(cxn);
      ok &= head && cxn->txContext().buf == head && cxn->txQueue.count_() == 2 &&
        !cxn->results.length() && f.device->calls == calls + 3;
      ok &= f.expire(cxn) && f.expire(cxn) && f.device->calls == calls + 5;
      f.device->refuse = false;
      ok &= f.expire(cxn) && !f.expire(cxn);
      ok &= f.device->frames.length() == i+2 && cxn->results.length() == 2 &&
        cxn->bytes.length() == 2 && cxn->bytes[0] == 'A' && cxn->bytes[1] == 'B' &&
        f.wire(i, route, 'A') && f.wire(i+1, route, 'B') && !cxn->txQueue.count_() &&
        f.device->pool->balanced() == ipv6;
      f.retire();
      ok &= f.device->pool->balanced();
    });
    ZuCheckRT(ok, log_("idle refusal preserves FIFO, leases, ECN and retry budget for IPv",
      ipv6 ? 6 : 4));
    cxn->close();
    ZuCheckRT(!cxn->closed.timedwait(Zm::now(5)), log_("published endpoint closes"));
  }

  // Start with idle backoff, then put another endpoint's accepted frame in flight.
  // Its final completion wakes this head, which must fall back to the timer again.
  auto route = path(false);
  auto a = f.open(route, false);
  auto b = f.open(path(true));
  bool transitions = a && b;
  f.onTx([&] {
    f.device->early = false;
    f.device->refuse = true;
    a->enqueue(f.tx, 'a');
  });
  f.fence();
  unsigned before = 0;
  f.onTx([&] {
    f.device->refuse = false;
    b->enqueue(f.tx, 'x');
    f.device->refuse = true;
    before = f.device->calls;
    transitions &= f.expire(a) && f.device->calls == before + 1 && !f.expire(a);
    f.retire();
  });
  f.onTx([&] {
    transitions &= f.device->calls == before + 2 && !a->results.length();
    // Invalidate the cached path while parked. The resumed datagram must use
    // the result of a new lookup, and the async submission may itself park.
    nextHop = 42;
    ZutcpTest::MxPlugin::udpRouteEpoch(*f.stack, true);
    transitions &= f.expire(a);
  });
  f.fence();
  f.onTx([&] {
    f.device->refuse = false;
    transitions &= f.expire(a) && a->bytes.length() == 1 &&
      f.device->frames[f.device->frames.length()-1][5] == 42 && f.wire(f.device->frames.length()-1, route, 'a');
    f.retire();
  });
  ZuCheckRT(transitions, log_("resumed route lookup and last completion select one wake source"));

  // Drop provider readiness without stopping the multiplexer or delivering a CQ.
  f.onTx([&] {
    f.device->refuse = true;
    a->enqueue(f.tx, 't'); // timer
    a->enqueue(f.tx, 'u');
    f.device->refuse = false;
    b->enqueue(f.tx, 'o'); // unrelated outstanding submission
  });
  f.fence(); // b may need a new route for the current epoch
  f.onTx([&] {
    f.device->refuse = true;
    b->enqueue(f.tx, 'w'); // waits on o
    b->enqueue(f.tx, 'z');
    f.device->down = 1;
  });
  bool down = !b->completed.timedwait(Zm::now(5)); // previous x
  down &= !b->completed.timedwait(Zm::now(5)); // previous o
  down &= !b->completed.timedwait(Zm::now(5)); // provider-down terminal wake
  f.onTx([&] {
    down &= f.expire(a) && a->results.length() == 2 && a->results[1] == Zi::IOError &&
      b->results.length() == 3 && b->results[2] == Zi::IOError &&
      !a->txQueue.count_() && !b->txQueue.count_();
    f.device->down = 0;
    f.retire();
  });
  ZuCheckRT(down, log_("provider loss fails both parked heads once and releases successors"));

  // Queue a counted completion wake then close the physical connection in the
  // same turn. A fresh endpoint can send independently of that queued resume.
  bool closed = true;
  f.onTx([&] {
    f.device->refuse = false;
    b->enqueue(f.tx, 'o');
    f.device->refuse = true;
    a->enqueue(f.tx, 'c');
    f.retire();
    before = f.device->calls;
    a->close();
  });
  closed &= !a->closed.timedwait(Zm::now(5));
  a = f.open(route, false);
  f.onTx([&] { f.device->refuse = false; a->enqueue(f.tx, 'f', true); });
  closed &= !a->closed.timedwait(Zm::now(5));
  f.onTx([&] {
    closed &= f.device->calls == before+1 && a->bytes.length() == 1 && a->bytes[0] == 'f';
    f.retire();
  });
  ZuCheckRT(closed, log_("close cancels queued resume and rebind delivers the graceful final head"));

  bool invalid = true;
  f.onTx([&] {
    before = f.device->calls;
    for (bool cursor : {false, true}) {
      auto buf = (cursor ? f.tx : f.app)->allocBuf(b.ptr());
      ZmAssert_(buf && buf->append("abcde"));
      auto start = buf->data();
      bool called = false;
      auto &io = b->txContext();
      io.init(ZiTxFn{[&](ZiTxContext &io) {
        called = io.length < 0 && io.buf->data() == start && io.buf->cspan() == "abcde" &&
          io.offset == unsigned(cursor);
        io.complete();
        return true;
      }}, ZuMv(buf), b->info().remote);
      io.offset = unsigned(cursor);
      f.stack->send(b);
      invalid &= called;
    }
    invalid &= f.device->calls == before && f.device->pool->balanced();
  });
  ZuCheckRT(invalid, log_("invalid allocator and cursor fail with producer bytes intact"));

  // Reuse one caller-held buffer while the device still owns the first frame.
  // A new TOS would change its headers if the pre-write check ran too late.
  ZmRef<ZiIOBuf> held;
  unsigned callbacks = 0;
  bool busy = true;
  auto submit = [&](uint8_t tos) {
    b->txContext().init(ZiTxFn{[&](ZiTxContext &io) {
      busy &= io.buf == held && io.buf->cspan() == "q" && io.length == 1;
      ++callbacks;
      io.complete();
      return true;
    }}, held, b->info().remote, ZiTOS{tos});
    f.stack->send(b);
  };
  f.onTx([&] {
    before = f.device->calls;
    held = f.tx->allocBuf(b.ptr());
    ZmAssert_(held && held->append("q"));
    submit(0);
    auto &first = f.device->frames[f.device->frames.length()-1];
    auto wire = held->data() - Zutcp::udpHead(b->info().remote.ip().type());
    submit(0xab);
    busy &= callbacks == 1 && f.device->calls == before+1 &&
      !memcmp(first.data(), wire, first.length());
    f.retire();
  });
  f.onTx([&] {
    busy &= callbacks == 2 && f.device->calls == before+2 &&
      f.wire(f.device->frames.length()-1, path(true), 'q');
    held = nullptr;
    f.retire();
    busy &= f.device->pool->balanced();
  });
  ZuCheckRT(busy, log_("prior device lease protects headers until completion"));

  // The receive owner's stop retirement must cancel an armed retry timer.
  f.onTx([&] { f.device->refuse = true; b->enqueue(f.tx, 's'); });
  bool stopped = f.mx.stop();
  ZuCheckRT(stopped && !b->txUp() && b->txContext().completed() && f.device->pool->balanced(),
    log_("multiplexer stop retires timer-backed endpoint"));
}

static void testUDPTimer()
{
  using UDPLeaseTest::Fixture;
  ZuTestScopeRT(testUDPTimer);
  Fixture f;
  bool started = f.start(0, ZuTime{0, 5000000});
  ZuCheckRT(started, log_("start UDP timer fixture"));
  if (!started) return;
  auto route = path(false);
  route.remote = ZiIP{"239.18.0.1"};
  auto cxn = f.open(route);
  bool ok = !!cxn;
  f.onTx([&] {
    ZutcpTest::MxPlugin::udpRouteEpoch(*f.stack);
    f.device->refuse = true;
    cxn->enqueue(f.tx, 'm', true);
    ok &= f.device->calls == 1 && !cxn->results.length();
    f.device->refuse = false;
  });
  ok &= !cxn->completed.timedwait(Zm::now(5)) && !cxn->closed.timedwait(Zm::now(5));
  f.fence();
  f.onTx([&] {
    ok &= f.device->calls == 2 && f.device->frames.length() == 1 && f.loops == 1 &&
      f.wire(0, route, 'm');
    f.retire();
    ok &= f.device->pool->balanced();
  });
  bool stopped = f.mx.stop();
  ZuCheckRT(ok && stopped, log_("real timer retries idle graceful send and loops it back once"));
}

#endif /* ZutcpUDPLeaseTest_HH */
