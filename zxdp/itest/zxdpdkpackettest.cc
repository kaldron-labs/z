//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>
#include <unistd.h>

#include <rte_mbuf.h>

#include <zlib/ZuTestUtil.hh>

#include "zxdpdkfixture.hh"

using namespace ZuTestUtil;
using namespace ZxdpDpdkTest;

static void frame(ZuSpan<uint8_t> bytes, unsigned seed)
{
  for (unsigned i = 0; i < bytes.length(); ++i) bytes[i] = uint8_t(seed ^ i);
  bytes[12] = 0x88;
  bytes[13] = 0xb5;
}

static void testRing()
{
  ZuTestScopeRT(testRing);
  RingPort port;
  auto attached = port.init("packet", 2, {{0x02, 0, 0, 0, 0, 1}});
  ZuCheckRT(attached.is<void>(), log_(errorText(attached.p<ZeException>())));
  if (!attached.is<void>()) return;
  Device endpoint;
  auto allocated = endpoint.init(port.name(), 50, 0, 2);
  ZuCheckRT(allocated.is<void>(), log_(errorText(allocated.p<ZeException>())));
  if (!allocated.is<void>()) return;

  uint8_t *received = nullptr;
  unsigned receivedLen = 0;
  auto receive = [&endpoint, &received, &receivedLen](uint8_t *packet, unsigned len) {
    received = packet;
    receivedLen = len;
    endpoint.device->wake();
  };
  auto started = endpoint.start(receive);
  ZuCheckRT(started.is<void>(), log_(errorText(started.p<ZeException>())));
  if (!started.is<void>()) return;
  ZuCheckRT(endpoint.device->zeroCopy(), log_("ring PMD forwards mbufs directly"));

  uint8_t a[64], b[64];
  frame(a, 0x22);
  frame(b, 0x73);
  auto first = endpoint.tx->alloc();
  auto second = endpoint.tx->alloc();
  ZuCheckRT(first && second, log_("two requested native Tx slots are available"));
  if (!first || !second) {
    if (first) endpoint.tx->free(first);
    if (second) endpoint.tx->free(second);
    return;
  }
  auto offset = first + 17;
  memcpy(offset, a, sizeof(a));
  memcpy(second, b, sizeof(b));
  bool sent = false, full = false, duplicate = false;
  unsigned premature = 0;
  endpoint.onTx([&endpoint, offset, second, &sent, &full, &duplicate, &premature] {
    sent = endpoint.device->send(offset, 64);
    duplicate = !endpoint.device->send(offset, 64);
    full = !endpoint.device->send(second, 64);
    premature = endpoint.device->pollTx();
  });
  ZuCheckRT(sent && full && duplicate && !premature && !memcmp(second, b, sizeof(b)),
    log_("one outstanding submission per slot and full-ring false send preserve caller bytes"));

  // The fixture independently dequeues and retires the PMD's reference.
  auto mbuf = port.take();
  ZuCheckRT(mbuf && mbuf->pool == native(endpoint.tx) &&
      rte_pktmbuf_mtod(mbuf, uint8_t *) == offset && rte_pktmbuf_pkt_len(mbuf) == sizeof(a) &&
      !memcmp(rte_pktmbuf_mtod(mbuf, const void *), a, sizeof(a)),
    log_("the PMD sees the exact mbuf, offset pointer and outgoing bytes"));
  endpoint.tx->free(first);
  auto tooEarly = endpoint.tx->alloc();
  ZuCheckRT(!tooEarly, log_("the driver reference protects an early-returned allocation"));
  if (tooEarly) endpoint.tx->free(tooEarly);
  if (mbuf) rte_pktmbuf_free(mbuf);
  unsigned retired = 0, extra = 0;
  endpoint.onTx([&endpoint, &retired, &extra] {
    retired = endpoint.device->pollTx();
    extra = endpoint.device->pollTx();
  });
  ZuCheckRT(retired == 1 && !extra, log_("retirement is reported exactly once"));
  auto reused = endpoint.tx->alloc();
  ZuCheckRT(reused == first, log_("the original pool recycles the exact slot"));
  if (reused) endpoint.tx->free(reused);

  bool accepted = false;
  endpoint.onTx([&endpoint, second, &accepted] { accepted = endpoint.device->send(second, 64); });
  mbuf = port.take();
  ZuCheckRT(accepted && mbuf && rte_pktmbuf_mtod(mbuf, uint8_t *) == second,
    log_("the refused packet can be resubmitted without reacquiring its caller lease"));
  if (mbuf) rte_pktmbuf_free(mbuf);
  endpoint.onTx([&endpoint, &retired, &extra] {
    retired = endpoint.device->pollTx();
    extra = endpoint.device->pollTx();
  });
  ZuCheckRT(retired == 1 && !extra && !memcmp(second, b, sizeof(b)) &&
      rte_mempool_avail_count(native(endpoint.tx)) == 1,
    log_("completion leaves a retained caller lease and contents valid"));
  endpoint.tx->free(second);

  auto ingress = native(endpoint.ingress);
  uint8_t *injected = nullptr;
  bool queued = port.inject(ingress, a, &injected);
  ZuCheckRT(queued && rte_mempool_avail_count(ingress) == 1,
    log_("fixture consumes a receiver-pool slot before enqueuing it"));
  if (queued) {
    ZmSemaphore drained;
    endpoint.mx.rxRun([&endpoint, &drained] {
      endpoint.device->rx();
      drained.post();
    });
    drained.wait();
  }
  ZuCheckRT(received && received == injected && receivedLen == sizeof(a) &&
      !memcmp(received, a, sizeof(a)) && rte_mempool_avail_count(ingress) == 1,
    log_("receive transfers the exact backing pointer and bytes without copying"));
  ZuCheckRT(endpoint.stop() && received && !memcmp(received, a, sizeof(a)),
    log_("retained Rx bytes survive stopped queues"));
  if (received) endpoint.ingress->free(received);
  ZuCheckRT(rte_mempool_avail_count(ingress) == 2,
    log_("off-owner final return restores receiver pool capacity"));
}

static void testPcap(const Runtime &runtime)
{
  ZuTestScopeRT(testPcap);
  uint8_t input[64], output[64];
  frame(input, 0x38);
  frame(output, 0xc8);
  PcapPort port;
  auto attached = port.init(runtime, input);
  ZuCheckRT(attached.is<void>(), log_(errorText(attached.p<ZeException>())));
  if (!attached.is<void>()) return;
  Device endpoint;
  auto allocated = endpoint.init(port.name(), 50);
  ZuCheckRT(allocated.is<void>(), log_(errorText(allocated.p<ZeException>())));
  if (!allocated.is<void>()) return;

  bool observed = false;
  auto started = endpoint.start([&endpoint, &observed, &input](uint8_t *packet, unsigned len) {
    observed = len == sizeof(input) && !memcmp(packet, input, sizeof(input));
    endpoint.ingress->free(packet);
    endpoint.device->wake();
  });
  ZuCheckRT(started.is<void>(), log_(errorText(started.p<ZeException>())));
  if (!started.is<void>()) return;
  ZuCheckRT(!endpoint.device->zeroCopy(), log_("pcap file input/output classifies as copy mode"));

  ZmSemaphore drained;
  endpoint.mx.rxRun([&endpoint, &drained] {
    endpoint.device->rx();
    drained.post();
  });
  drained.wait();
  auto available = rte_mempool_avail_count(native(endpoint.ingress));
  ZuCheckRT(observed && available == endpoint.ingress->count,
    log_("pcap input arrives byte-for-byte and is returned to the common ingress pool"));

  bool accepted = false;
  unsigned retired = 0;
  endpoint.onTx([&endpoint, &output, &accepted, &retired] {
    auto packet = endpoint.tx->alloc();
    if (!packet) return;
    memcpy(packet, output, sizeof(output));
    accepted = endpoint.device->send(packet, sizeof(output));
    endpoint.tx->free(packet);
    retired = endpoint.device->pollTx();
  });
  ZuCheckRT(accepted && retired == 1, log_("pcap retires its output submission once"));
  ZuCheckRT(endpoint.stop() && port.captured(output),
    log_("pcap file contains exactly the submitted Ethernet frame"));
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  alarm(25);
  Runtime runtime;
  auto result = runtime.init();
  ZuCHECK(result.is<void>(), errorText(result.p<ZeException>()));
  if (!result.is<void>()) return 1;
  ZuTestCall(testRing);
  ZuTestCall(testPcap, runtime);
}
