//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZePlatform.hh>

#include <zlib/ZiIOBuf.hh>

#include <zlib/Zxdp.hh>

using namespace ZuTestUtil;

static void testCreation()
{
  ZuTestScope(testCreation);
  auto instance = Zxdp::instance(Zxdp::Type::Xdp);
  ZuCHECK(instance && !instance->ready(), "selected backend starts cold");
  Zxdp::Params missing(
    Zxdp::Params::New<Zxdp::Params::Index<Zxdp::Xdp::Params>{}>{});
  auto result = instance->init(missing);
  ZuCHECK(result.is<ZeException>() && !!result.p<ZeException>(),
    "missing initialization parameters fail without kernel resources");
  ZuCHECK(instance->init(missing).is<ZeException>(),
    "failed initialization cannot be repeated");
  auto closed = instance->close();
  ZuCHECK(closed.is<bool>() && closed.p<bool>(), "failed initialization closes cleanly");
  ZuCHECK(instance->init(missing).is<ZeException>(),
    "closed instance cannot be initialized again through variant dispatch");
}

static void testPoolFactory()
{
  ZuTestScope(testPoolFactory);
  auto factory = Zxdp::instance(Zxdp::Type::Xdp);
  auto rx = factory->bufPool(2, 1514);
  auto tx = factory->bufPool(1, 128);
  auto control = factory->bufPool(1, 64);
  auto closed = factory->close();
  ZuCHECK(closed.is<bool>() && closed.p<bool>(), "cold instance closes without initialization");
  closed = factory->close();
  ZuCHECK(closed.is<bool>() && closed.p<bool>(), "cold close is idempotent");
  Zxdp::Params empty(
    Zxdp::Params::New<Zxdp::Params::Index<Zxdp::Xdp::Params>{}>{});
  ZuCHECK(factory->init(empty).is<ZeException>(), "cold close prevents later initialization");
  factory = nullptr;
  ZuCHECK(rx && tx && control && rx->count == 2 && tx->size == 128 &&
      !rx->ready() && !tx->ready() && !control->ready(),
    "unbound pools survive their factory instance independently");

  auto instance = Zxdp::instance(Zxdp::Type::Xdp);
  Zxdp::Params config(
    Zxdp::Params::New<Zxdp::Params::Index<Zxdp::Xdp::Params>{}>{});
  auto &params = config.p<Zxdp::Xdp::Params>();
  params.rxPool = rx;
  params.txPools.pushNode(tx.ptr());
  params.txPools.pushNode(control.ptr());
  params.rx = [](uint8_t *, unsigned) { };
  auto unbound = instance->init(config); // zero interface fails without privileges
  const auto &configured = config.p<Zxdp::Xdp::Params>();
  ZuCHECK(unbound.is<ZeException>() && !instance->ready() && !rx->ready(),
    "a different backend accepts factory pools but rejects incomplete configuration");
  auto pools = configured.txPools.citer();
  ZuCHECK(configured.rxPool.ptr() == rx.ptr() && configured.txPools.count_() == 2 &&
      configured.txPools.headPtr() == tx.ptr() && pools() == tx.ptr() &&
      pools() == control.ptr() && !pools(),
    "initialization preserves caller Rx reference and intrusive list identity");
  ZuCHECK(instance->init(config).is<ZeException>(),
    "failed initialization cannot be repeated");
  closed = instance->close();
  ZuCHECK(closed.is<bool>() && closed.p<bool>(), "unbound instance closes cleanly");
  ZuCHECK(instance->init(config).is<ZeException>(),
    "closed instance rejects initialization");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testCreation);
  ZuTestCall(testPoolFactory);
}
