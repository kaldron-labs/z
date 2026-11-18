//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmScheduler.hh>

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
  ZuCHECK(!instance->start() && instance->state() == ZmEngineState::Stopped &&
      !!instance->error(), "start before init fails and returns the engine to Stopped");
  Zxdp::Params missing(
    Zxdp::Params::New<Zxdp::Params::Index<Zxdp::Xdp::Params>{}>{});
  ZuCHECK(instance->init(missing).is<ZeException>() &&
      instance->init(missing).is<ZeException>() && !instance->ready(),
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
  auto rx = factory->bufPool(2, 1514);
  auto tx = factory->bufPool(1, 128);
  auto control = factory->bufPool(1, 64);
  factory->final();
  factory->final();
  factory = nullptr;
  ZuCHECK(rx && tx && control && rx->count == 2 && tx->size == 128,
    "unbound pools survive factory finalization and destruction");
  auto instance = Zxdp::instance(Zxdp::Type::Xdp);
  ZmScheduler scheduler{ZmSchedParams{}.nThreads(1)};
  Zxdp::Params config(
    Zxdp::Params::New<Zxdp::Params::Index<Zxdp::Xdp::Params>{}>{});
  auto &params = config.p<Zxdp::Xdp::Params>();
  params.ifindex = 1;
  params.rxPool = rx;
  params.txPools.pushNode(tx.ptr());
  params.txPools.pushNode(control.ptr());
  params.rx = [](uint8_t *, unsigned) { };
  params.scheduler = &scheduler;
  params.txSid = 1;
  auto initialized = instance->init(config);
  auto rxRefs = rx->refCount(), txRefs = tx->refCount();
  auto repeated = instance->init(config);
  ZuCHECK(initialized.is<void>() && repeated.is<void>() && !instance->ready() &&
      instance->state() == ZmEngineState::Stopped && rx->refCount() == rxRefs &&
      tx->refCount() == txRefs && !rx->alloc() && !tx->alloc(),
    "idempotent init retains configuration without starting I/O or duplicating pool references");
  auto pools = params.txPools.citer();
  ZuCHECK(params.rxPool.ptr() == rx.ptr() && params.txPools.count_() == 2 &&
      params.txPools.headPtr() == tx.ptr() && pools() == tx.ptr() &&
      pools() == control.ptr() && !pools(),
    "initialization preserves the caller's intrusive membership and order");
  ZuCHECK(!instance->start() && instance->state() == ZmEngineState::Stopped,
    "start requires the caller's scheduler to be running");
  ZuCHECK(instance->stop(), "failed start leaves an idempotently stopped instance");
  instance->final();
  instance->final();
  ZuCHECK(instance->init(config).is<void>() && !instance->ready(),
    "configuration can be initialized again after final without activating I/O");
  instance->final();
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testCreation);
  ZuTestCall(testPoolFactory);
}
