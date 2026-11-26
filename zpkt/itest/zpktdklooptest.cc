//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>
#include <unistd.h>
#include <iostream>

#include <rte_mbuf.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmAssert.hh>

#include "zpktdkfixture.hh"

using namespace ZuTestUtil;
using namespace ZpktDpdkTest;

static ZuUnion<Zpkt::NifInfo, ZeException> unexpectedQuery(ZuCSpan)
{
  ZmAssert_(false);
  return ZeEXCEPT(Error, "Zpkt.DpdkTest", "unexpected host query");
}

struct Transfer {
  Device	*source, *dest;
  uint8_t	*packet = nullptr;
  const uint8_t	*expected;
  unsigned	bytes;
  ZmSemaphore	released;
  bool		sent = false, observed = false;
};

static void testLoop(const Runtime &runtime)
{
  ZuTestScopeRT(testLoop);
  auto env = runtime.env();
  env->nifQueryFn(unexpectedQuery);
  ZuCheckRT(env->loopback(), log_("loaded DPDK advertises loopback before any open"));
  RingPort aPort, bPort;
  auto aa = aPort.init("loopa", 64, {{0x02, 0, 0, 0, 0, 1}});
  auto ba = bPort.init("loopb", 64, {{0x02, 0, 0, 0, 0, 2}});
  ZuCheckRT(aa.is<void>(), log_(errorText(aa.p<ZeException>())));
  ZuCheckRT(ba.is<void>(), log_(errorText(ba.p<ZeException>())));
  if (!aa.is<void>() || !ba.is<void>()) return;

  Device a, b;
  auto ar = a.init(runtime, "_loop"), br = b.init(runtime, bPort.name());
  ZuCheckRT(ar.is<void>(), log_(errorText(ar.p<ZeException>())));
  ZuCheckRT(br.is<void>(), log_(errorText(br.p<ZeException>())));
  if (!ar.is<void>() || !br.is<void>()) return;
  auto duplicate = env->nif(&a.mx, "_loop");
  ZuCheckRT(duplicate.is<Zpkt::Nif *>() && duplicate.p<Zpkt::Nif *>() == a.device.ptr() &&
      env->nif(&b.mx, "_loop").is<ZeException>() && !a.device->ifindex() &&
      a.device->mtu() == 1500 && a.device->mac()[5] == 1 &&
      env->nif(&a.mx, aPort.name()).is<ZeException>(),
    log_("reserved name preserves the cache and publishes native ring geometry"));
  uint8_t large[1514], small[64];
  for (unsigned i = 0; i < sizeof(large); ++i) large[i] = uint8_t(0x56 ^ i);
  for (unsigned i = 0; i < sizeof(small); ++i) small[i] = uint8_t(0xab ^ i);
  Transfer ab{.source = &a, .dest = &b, .expected = large, .bytes = sizeof(large)};
  Transfer baTx{.source = &b, .dest = &a, .expected = small, .bytes = sizeof(small)};

  auto receive = [](Transfer &rx) {
    return [&rx](uint8_t *packet, unsigned len, ZiRxBurst *) {
      auto &dest = *rx.dest;
      rx.observed = packet != rx.packet && len == rx.bytes &&
	!memcmp(packet, rx.expected, rx.bytes);
      dest.ingress->free(packet);
      dest.device->wake();
    };
  };
  ar = a.start(receive(baTx));
  br = b.start(receive(ab));
  ZuCheckRT(ar.is<void>(), log_(errorText(ar.p<ZeException>())));
  ZuCheckRT(br.is<void>(), log_(errorText(br.p<ZeException>())));
  if (!ar.is<void>() || !br.is<void>()) return;

  Wire wire;
  auto linked = wire.start(
    {&aPort, native(a.ingress), &ab.released}, {&bPort, native(b.ingress), &baTx.released});
  ZuCheckRT(linked.is<void>(), log_(errorText(linked.p<ZeException>())));
  if (!linked.is<void>()) return;
  auto send = [](Transfer &tx) {
    tx.packet = tx.source->tx->alloc();
    if (!tx.packet) return;
    memcpy(tx.packet, tx.expected, tx.bytes);
    tx.source->onTx([&tx] {
      tx.sent = tx.source->device->send(tx.packet, tx.bytes) == Zi::OK;
    });
  };
  send(ab);
  send(baTx);
  ZuCheckRT(ab.sent && baTx.sent, log_("both ports submit their frame to the independent peer"));
  if (ab.sent && baTx.sent) {
    auto deadline = Zm::now() + ZuTime{5};
    bool releasedA = !ab.released.timedwait(deadline);
    bool releasedB = !baTx.released.timedwait(deadline);
    ZuCheckRT(releasedA && releasedB, log_("the wire releases both driver references"));
    auto rx = [deadline](Device &endpoint) {
      ZmSemaphore done;
      endpoint.mx.rxRun([&endpoint, &done] {
	endpoint.device->rx();
	done.post();
      });
      if (!done.timedwait(deadline)) return true;
      endpoint.device->wake();
      done.wait(); // explicit cancellation drains the posted callback
      return false;
    };
    if (releasedA && releasedB) {
      bool receivedA = rx(a);
      bool receivedB = rx(b);
      ZuCheckRT(receivedA && receivedB && ab.observed && baTx.observed,
	log_("independent wire delivers a full-MTU frame and a small frame in opposite directions"));
      for (auto endpoint : {&a, &b}) {
	unsigned retired = 0;
	endpoint->onTx([endpoint, &retired] { retired = endpoint->device->pollTx(); });
	ZuCheckRT(retired == 1 && rte_mempool_avail_count(native(endpoint->ingress)) ==
	    endpoint->ingress->count,
	  log_("the wire retires one Tx and the typed ingress backing returns to the receiver"));
      }
    }
  }
  if (ab.packet) a.tx->free(ab.packet);
  if (baTx.packet) b.tx->free(baTx.packet);

  wire.quiesce();
  ZuCheckRT(a.stop(), log_("stop the first device with the peer still draining submissions"));
  ZuCheckRT(b.stop(), log_("stop the second device without new ingress"));
  wire.stop();
  aPort.drain();
  bPort.drain();
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  if (geteuid()) {
    std::cout << "1..0 # SKIP requires private dummy kernel Nif fixture privileges\n";
    return 0;
  }
  ZuTestMain();
  alarm(25);
  Runtime runtime;
  auto result = runtime.init();
  ZuCHECK(result.is<void>(), errorText(result.p<ZeException>()));
  if (!result.is<void>()) return 1;
  ZuTestCall(testLoop, runtime);
}
