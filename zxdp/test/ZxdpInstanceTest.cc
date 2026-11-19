//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZiMultiplex.hh>

#include <zlib/ZePlatform.hh>

#include <zlib/ZiIOBuf.hh>

#include <zlib/Zxdp.hh>

using namespace ZuTestUtil;

static void testCreation()
{
  ZuTestScope(testCreation);
  auto instance = Zxdp::instance(Zxdp::Type::Xdp);
  ZuCHECK(instance && !instance->ready() && instance->state() == ZmEngineState::Stopped,
    "selected instance starts uninitialized and stopped");
  ZuCHECK(!instance->start([](uint8_t *, unsigned) { }) && instance->state() == ZmEngineState::Stopped &&
      !!instance->error(), "start before init fails and returns the engine to Stopped");
  Zxdp::Params missing(
    Zxdp::Params::New<Zxdp::Params::Index<Zxdp::Xdp::Params>{}>{});
  ZiMultiplex mx;
  missing.p<Zxdp::Xdp::Params>().txSid = 3;
  ZuCHECK(instance->init(&mx, missing).is<ZeException>() &&
      instance->init(&mx, missing).is<ZeException>() && !instance->ready() &&
      !instance->mx() && !instance->mtu() && !instance->rxQueues() && !instance->allocated(),
    "invalid configuration can be retried without creating active resources");
  ZuCHECK(instance->stop() && instance->stop(), "stopped stop is idempotent");
  instance->final();
  instance->final();
  ZuCHECK(instance->state() == ZmEngineState::Stopped && !instance->ready(),
    "repeated final preserves the uninitialized stopped state");
}

static void testPoolFactory()
{
  ZuTestScope(testPoolFactory);
  auto factory = Zxdp::instance(Zxdp::Type::Xdp);
  auto rx = factory->bufPool(Zi::IOBufType::Rx, 2, 1514);
  auto tx = factory->bufPool(Zi::IOBufType::TxTCP, 1, 128);
  auto udp = factory->bufPool(Zi::IOBufType::TxUDP, 1, 64, 8, 4, 128);
  factory->final();
  factory = nullptr;
  ZuCHECK(rx && tx && udp && rx->type == Zi::IOBufType::Rx &&
      tx->type == Zi::IOBufType::TxTCP && udp->type == Zi::IOBufType::TxUDP &&
      rx->count == 2 && tx->size == 128 && udp->size == 64 &&
      udp->headRoom == 8 && udp->tailRoom == 4 && udp->align == 128,
    "cold pool factory preserves the requested roles and geometry");

  Zi::IOBufPoolList pools;
  pools.pushNode(tx.ptr());
  pools.pushNode(udp.ptr());
  auto instance = Zxdp::instance(Zxdp::Type::Xdp);
  ZuCHECK(instance->alloc(rx, pools).is<ZeException>() && !instance->allocated() &&
      pools.headPtr() == tx.ptr() && pools.tailPtr() == udp.ptr() &&
      !rx->alloc() && !tx->alloc() && !udp->alloc(),
    "allocation before initialization leaves the pools unbacked and unmodified");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testCreation);
  ZuTestCall(testPoolFactory);
}
