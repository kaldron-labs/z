//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmBlock.hh>
#include <zlib/Zquic.hh>

using namespace ZuTestUtil;

struct Endpoint : public Zquic_::Endpoint_<Endpoint> {
  using Base = Zquic_::Endpoint_<Endpoint>;
  using ReadyFn = ZmFn<void(bool), ZmFnHeapID<"Zquic.TxQueue.Ready">>;
  using DrainFn = ZmFn<void(), ZmFnHeapID<"Zquic.TxQueue.Drain">>;
  enum { EndpointRef = false };

  ReadyFn readyFn;
  DrainFn drainFn;
  ZmSemaphore done;
  unsigned received = 0;
  bool ordered = true;

  void endpointReady_(Base *) { auto fn = ZuMv(readyFn); fn(true); }
  void endpointFailed(bool) { auto fn = ZuMv(readyFn); fn(false); }
  void endpointDown_(Base *) { }
  void endpointDatagram(Zquic::Datagram d) {
    if (!d.buf || d.buf->length != 1 || d.buf->data()[0] != received + 1)
      ordered = false;
    if (++received == 3) done.post();
  }
  void endpointTxDrained() {
    auto fn = ZuMv(drainFn);
    if (fn) fn();
  }
  bool open() {
    return ZmBlock<bool>{}([this](auto wake) {
      readyFn = [wake = ZuMv(wake)](bool ok) mutable { wake(ok); };
      if (!openUDP(Zquic::PathMode::ServerUnconnected,
	  ZiIP{"127.0.0.1"}, 0)) {
	auto fn = ZuMv(readyFn);
	fn(false);
      }
    });
  }
  bool packet(unsigned value, ZiSockAddr addr) {
    auto buf = allocTxPkt();
    *buf->end() = value;
    buf->length = 1;
    return send(ZuMv(buf), ZuMv(addr));
  }
  void close() {
    ZmBlock<>{}([this](auto wake) {
      disconnect([wake = ZuMv(wake)]() mutable { wake(); });
    });
  }

};

static void testDrain()
{
  ZuTestScope(testDrain);
  ZiMultiplex mx{ZiMxParams{}
    .scheduler([](auto &s) {
      s.nThreads(2).thread(1, [](auto &t) { t.isolated(1); })
	.thread(2, [](auto &t) { t.isolated(1); });
    }).rxThread(1).txThread(2)};
  Zi::IOBufAllocator *rxAllocator = Zquic::pktRxAllocator(&mx);
  Zi::IOBufAllocator *txAllocator = Zquic::pktTxAllocator(&mx);
  bool registered = rxAllocator && txAllocator;
  Endpoint rx, tx;
  ZuCheck(mx.start(), (return));
  bool opened = registered && rx.init(&mx, rxAllocator, txAllocator) &&
    tx.init(&mx, rxAllocator, txAllocator) && rx.open() && tx.open();
  ZuCheck(opened);
  bool active = false, admitted = false, received = false;
  if (opened) {
    auto addr = rx.local();
    ZmBlock<>{}([&mx, &tx, &active, &admitted, addr](auto wake) {
      mx.txRun([&tx, &active, &admitted, addr,
	  wake = ZuMv(wake)]() mutable {
	active = true;
	admitted = tx.packet(1, addr) && tx.packet(2, addr) && tx.packet(3, addr);
	wake();
      });
    });
    received = rx.done.timedwait(Zm::now(3)) == 0;
  }
  ZuCheck(received);
  tx.close();
  rx.close();
  ZuCheck(mx.stop());
  ZuCheck(admitted);
  ZuCheck(active);
  ZuCheck(rx.received == 3 && rx.ordered);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testDrain);
}
