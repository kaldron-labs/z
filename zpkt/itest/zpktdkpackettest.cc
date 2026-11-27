//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <dlfcn.h>
#include <net/if.h>
#include <string.h>
#include <unistd.h>
#include <iostream>

#include <rte_ethdev.h>
#include <rte_flow.h>
#include <rte_mbuf.h>

#include <zlib/ZuTestUtil.hh>

#include "../dpdk/ZpktDpdkPool.hh"
#include "zpktdkfixture.hh"

using namespace ZuTestUtil;
using namespace ZpktDpdkTest;
using namespace Zpkt::Dpdk::ReturnPoint;

struct ReturnGate {
  ZmAtomic<unsigned> point{0};
  ZmSemaphore reached, proceed;

  void arm(unsigned p) { point = p; }
  void observe(unsigned p) {
    if (point == p && point.xch(0)) {
      reached.post();
      proceed.wait();
    }
  }
};

struct ReturnObserver {
  ReturnGate owner, producer, stop;
  ZmAtomic<unsigned> visits{0}, entries{0}, peak{0};
};

static ZmAtomic<ReturnObserver *> observer{nullptr};

extern "C" __attribute__((visibility("default")))
void ZpktTestDpdkReturns(unsigned point, unsigned count)
{
  auto obs = static_cast<ReturnObserver *>(observer);
  if (!obs) return;
  switch (point) {
    case QueuePush: obs->peak.maximum(obs->entries.xchAdd(count) + count); break;
    case QueuePop: obs->entries.xchAdd(-count); break;
    case Visit: obs->visits.xchAdd(count); break;
    case Published: obs->producer.observe(point); break;
    case Wait: obs->stop.observe(point); break;
    default: obs->owner.observe(point); break;
  }
}

static ZmAtomic<rte_mbuf *> returnAtStop{nullptr};

extern "C" __attribute__((visibility("default")))
int rte_eth_dev_stop(uint16_t port)
{
  static const auto stop = reinterpret_cast<decltype(&rte_eth_dev_stop)>(
    dlsym(RTLD_NEXT, "rte_eth_dev_stop"));
  if (auto mbuf = returnAtStop.xch(nullptr)) rte_pktmbuf_free(mbuf);
  return stop(port);
}

// Exercise bifurcated-port lifecycle on a real ethdev queue and private netdev.
// Only the hardware match/action API and PMD identity are supplied by this test.
static unsigned flowPort = RTE_MAX_ETHPORTS, flowIndex, creates, destroys;
static bool isolated, rejectCreate, rejectDestroy;
static rte_flow *flowHandle() { return reinterpret_cast<rte_flow *>(&flowPort); }

extern "C" __attribute__((visibility("default")))
int rte_eth_dev_info_get(uint16_t port, rte_eth_dev_info *info)
{
  static const auto get = reinterpret_cast<decltype(&rte_eth_dev_info_get)>(
    dlsym(RTLD_NEXT, "rte_eth_dev_info_get"));
  int rc = get(port, info);
  if (!rc && port == flowPort) {
    info->driver_name = "net_mlx5";
    info->if_index = flowIndex;
  }
  return rc;
}

extern "C" __attribute__((visibility("default")))
int rte_flow_isolate(uint16_t port, int set, rte_flow_error *)
{
  if (port != flowPort || set != 1) return -ENOTSUP;
  isolated = true;
  return 0;
}

extern "C" __attribute__((visibility("default")))
int rte_flow_validate(uint16_t port, const rte_flow_attr *attr,
    const rte_flow_item *pattern, const rte_flow_action *actions, rte_flow_error *)
{
  auto q = static_cast<const rte_flow_action_queue *>(actions[0].conf);
  return port == flowPort && isolated && attr->ingress && !attr->egress && !attr->transfer &&
    !attr->group && pattern[0].type == RTE_FLOW_ITEM_TYPE_ETH && !pattern[0].spec &&
    !pattern[0].mask && pattern[1].type == RTE_FLOW_ITEM_TYPE_END &&
    actions[0].type == RTE_FLOW_ACTION_TYPE_QUEUE && q && !q->index &&
    actions[1].type == RTE_FLOW_ACTION_TYPE_END ? 0 : -EINVAL;
}

extern "C" __attribute__((visibility("default")))
rte_flow *rte_flow_create(uint16_t port, const rte_flow_attr *attr,
    const rte_flow_item *pattern, const rte_flow_action *actions, rte_flow_error *err)
{
  if (rejectCreate || rte_flow_validate(port, attr, pattern, actions, err)) {
    rte_errno = ENOTSUP;
    return nullptr;
  }
  ++creates;
  return flowHandle();
}

extern "C" __attribute__((visibility("default")))
int rte_flow_destroy(uint16_t port, rte_flow *flow, rte_flow_error *)
{
  if (port != flowPort || flow != flowHandle()) return -EINVAL;
  if (rejectDestroy) return -EBUSY;
  ++destroys;
  return 0;
}

static void testLivePcap(const Runtime &runtime)
{
  ZuTestScopeRT(testLivePcap);
  const char *name = "net_pcap_zpkt0";
  bool attached = !rte_eal_hotplug_add("vdev", name, "iface=net_ring_packet");
  ZuCheckRT(attached, log_("attach live pcap to private dummy interface"));
  if (!attached) return;
  ZiMultiplex mx;
  auto opened = runtime.env()->nif(&mx, name);
  ZuCheckRT(opened.is<ZeException>(), log_("reject live pcap with uncontrollable capture size"));
  if (opened.is<Zpkt::Nif *>()) {
    ZmRef<Zpkt::Nif> nif = opened.p<Zpkt::Nif *>();
    nif->final();
  }
  ZuCheckRT(!rte_eal_hotplug_remove("vdev", name), log_("release test pcap port"));
}

static void testSteering(const Runtime &runtime)
{
  ZuTestScopeRT(testSteering);
  RingPort port;
  auto attached = port.init("packet", 128, {{2,0,0,0,0,1}});
  uint16_t id;
  bool setup = attached.is<void>() && !rte_eth_dev_get_port_by_name(port.name(), &id);
  ZuCheckRT(setup, log_("create ethdev for hardware steering fixture"));
  if (!setup) return;
  flowIndex = if_nametoindex("net_ring_packet");
  flowPort = id;
  Device endpoint;
  auto opened = endpoint.init(runtime, port.name(), 128);
  ZuCheckRT(opened.is<void>(), log_("claim private host interface for simulated mlx5"));
  if (opened.is<void>()) {
    auto discard = [&endpoint](uint8_t *p, unsigned, ZiRxBurst *) { endpoint.ingress->free(p); };
    rejectCreate = true;
    ZuCheckRT(endpoint.start(discard).is<ZeException>() && !endpoint.device->ready() && !creates,
      log_("failed catch-all prevents activation"));
    rejectCreate = false;
    ZuCheckRT(endpoint.start(discard).is<void>() && creates == 1,
      log_("retry publishes queue zero after catch-all installation"));
    rejectDestroy = true;
    ZuCheckRT(!endpoint.stop() && !destroys, log_("failed removal retains flow ownership"));
    rejectDestroy = false;
    ZuCheckRT(endpoint.start(discard).is<void>() && destroys == 1 && creates == 2,
      log_("restart first retires the retained handle"));
    ZuCheckRT(endpoint.stop() && destroys == 2, log_("stop removes only this run's flow"));
    endpoint.device->final();
  }
  flowPort = RTE_MAX_ETHPORTS;
}

static void frame(ZuSpan<uint8_t> bytes, unsigned seed)
{
  for (unsigned i = 0; i < bytes.length(); ++i) bytes[i] = uint8_t(seed ^ i);
  bytes[12] = 0x88;
  bytes[13] = 0xb5;
}

static void testRing(const Runtime &runtime)
{
  ZuTestScopeRT(testRing);
  RingPort port;
  auto attached = port.init("packet", 2, {{0x02, 0, 0, 0, 0, 1}});
  ZuCheckRT(attached.is<void>(), log_(errorText(attached.p<ZeException>())));
  if (!attached.is<void>()) return;
  Device endpoint;
  auto allocated = endpoint.init(runtime, port.name(), 2);
  ZuCheckRT(allocated.is<void>(), log_(errorText(allocated.p<ZeException>())));
  if (!allocated.is<void>()) return;

  uint8_t *received = nullptr;
  unsigned receivedLen = 0;
  auto receive = [&endpoint, &received, &receivedLen](uint8_t *packet, unsigned len, ZiRxBurst *) {
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
    sent = endpoint.device->txReady(offset) == Zi::OK &&
      endpoint.device->send(offset, endpoint.tx->size) == Zi::IOError &&
      endpoint.device->send(offset, 64) == Zi::OK;
    duplicate = endpoint.device->txReady(offset) == Zi::NotReady &&
      endpoint.device->send(offset, 64) == Zi::NotReady;
    full = endpoint.device->txReady(second) == Zi::OK &&
      endpoint.device->send(second, 64) == Zi::NotReady;
    premature = endpoint.device->pollTx();
  });
  ZuCheckRT(sent && full && duplicate && !premature && !memcmp(second, b, sizeof(b)),
    log_("one outstanding submission per slot and full-ring refusal preserve caller bytes"));

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
  endpoint.onTx([&endpoint, second, &accepted] {
    accepted = endpoint.device->txReady(second) == Zi::OK &&
      endpoint.device->send(second, 64) == Zi::OK;
  });
  mbuf = port.take();
  ZuCheckRT(accepted && mbuf && rte_pktmbuf_mtod(mbuf, uint8_t *) == second,
    log_("the refused packet can be resubmitted without reacquiring its caller lease"));
  if (mbuf) rte_pktmbuf_free(mbuf);
  endpoint.onTx([&endpoint, &retired, &extra] {
    retired = endpoint.device->pollTx();
    extra = endpoint.device->pollTx();
  });
  auto available = endpoint.tx->alloc(), unexpected = endpoint.tx->alloc();
  ZuCheckRT(retired == 1 && !extra && !memcmp(second, b, sizeof(b)) &&
      available == first && !unexpected,
    log_("completion leaves a retained caller lease and contents valid"));
  if (available) endpoint.tx->free(available);
  if (unexpected) endpoint.tx->free(unexpected);

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
  ZuCheckRT(endpoint.device->txReady(second) == Zi::IOError &&
      endpoint.device->send(second, 64) == Zi::IOError,
    log_("stopped DPDK provider rejects retained backing terminally"));
  endpoint.tx->free(second);
  if (received) endpoint.ingress->free(received);
  ZuCheckRT(rte_mempool_avail_count(ingress) == 2,
    log_("off-owner final return restores receiver pool capacity"));
}

static void testPrefix(const Runtime &runtime)
{
  ZuTestScopeRT(testPrefix);
  RingPort port;
  auto attached = port.init("prefix", 4, {{0x02, 0, 0, 0, 0, 2}});
  ZuCheckRT(attached.is<void>(), log_(errorText(attached.p<ZeException>())));
  if (!attached.is<void>()) return;
  Device endpoint;
  auto allocated = endpoint.init(runtime, port.name(), 6);
  ZuCheckRT(allocated.is<void>(), log_(errorText(allocated.p<ZeException>())));
  if (!allocated.is<void>()) return;
  unsigned received = 0;
  bool ordered = true;
  auto started = endpoint.start([&endpoint, &received, &ordered](
      uint8_t *packet, unsigned length, ZiRxBurst *burst) {
    uint8_t expected[64];
    frame(expected, 0xb0 + received);
    ordered &= burst && length == sizeof(expected) && !memcmp(packet, expected, sizeof(expected));
    endpoint.ingress->free(packet);
    if (++received == 3) endpoint.device->wake();
  });
  ZuCheckRT(started.is<void>(), log_(errorText(started.p<ZeException>())));
  if (!started.is<void>()) return;

  enum { Count = 3, Size = 64 };
  uint8_t *blockers[Count]{}, *backing[Count]{};
  Zpkt::TxPacket packets[Count]{};
  bool available = true;
  for (unsigned i = 0; i < Count; ++i) {
    blockers[i] = endpoint.tx->alloc();
    backing[i] = endpoint.tx->alloc();
    if (!blockers[i] || !backing[i]) { available = false; break; }
    packets[i] = {backing[i] + i, Size};
    frame({blockers[i], Size}, i);
    frame({packets[i].data, Size}, i + Count);
  }
  ZuCheckRT(available, log_("fixture has room for all caller leases"));
  if (available)
    for (unsigned k = 0; k <= Count; ++k) {
      const unsigned filled = Count - k;
      unsigned accepted = 0, retired = 0, retried = 0;
      bool prefilled = true, ready = true, bytes = true;
      endpoint.onTx([&endpoint, &blockers, &packets, &prefilled, &accepted, &ready, filled, k] {
	for (unsigned i = 0; i < filled; ++i)
	  prefilled &= endpoint.device->send(blockers[i], Size) == Zi::OK;
	accepted = endpoint.device->send(packets);
	for (unsigned i = 0; i < Count; ++i)
	  ready &= endpoint.device->txReady(packets[i].data) == (i >= k ? Zi::OK : Zi::NotReady);
      });
      rte_mbuf *held[Count]{};
      for (unsigned i = 0; i < Count; ++i) {
	auto mbuf = held[i] = port.take();
	auto expected = i < filled ? blockers[i] : packets[i - filled].data;
	bytes &= mbuf && rte_pktmbuf_mtod(mbuf, uint8_t *) == expected &&
	  rte_pktmbuf_pkt_len(mbuf) == Size;
      }
      if (k > 1 && held[Count - 1]) {
	rte_pktmbuf_free(held[Count - 1]);
	held[Count - 1] = nullptr;
	endpoint.onTx([&endpoint, &retired] { retired = endpoint.device->pollTx(); });
	ZuCheckRT(retired == 1, log_("the last accepted slot can retire ahead of the prefix"));
      }
      for (auto mbuf : held) if (mbuf) rte_pktmbuf_free(mbuf);
      endpoint.onTx([&endpoint, &packets, &retired, &retried, k] {
	retired += endpoint.device->pollTx();
	retried = endpoint.device->send({packets + k, Count - k});
      });
      for (unsigned i = k; i < Count; ++i) {
	auto mbuf = port.take();
	uint8_t expected[Size];
	frame(expected, i + Count);
	bytes &= mbuf && rte_pktmbuf_mtod(mbuf, uint8_t *) == packets[i].data &&
	  rte_pktmbuf_pkt_len(mbuf) == Size && !memcmp(packets[i].data, expected, Size);
	if (mbuf) rte_pktmbuf_free(mbuf);
      }
      unsigned suffixRetired = 0;
      endpoint.onTx([&endpoint, &suffixRetired] { suffixRetired = endpoint.device->pollTx(); });
      ZuCheckRT(prefilled && k == accepted && ready && bytes && retired == Count &&
	  retried == Count - k && suffixRetired == retried,
	log_("ring acceptance boundary ", k, " preserves original order and suffix leases"));
    }
  for (auto p : blockers) if (p) endpoint.tx->free(p);
  for (auto p : backing) if (p) endpoint.tx->free(p);
  bool injected = true;
  for (unsigned i = 0; i < 3; ++i) {
    uint8_t input[Size];
    frame(input, 0xb0 + i);
    injected &= port.inject(native(endpoint.ingress), input);
  }
  if (injected) {
    ZmSemaphore done;
    endpoint.mx.rxRun([&endpoint, &done] {
      endpoint.device->rx();
      done.post();
    });
    done.wait();
  }
  ZuCheckRT(injected && ordered && received == 3,
    log_("a three-packet Rx dequeue delivers every packet in order within the burst scope"));
  ZuCheckRT(endpoint.stop(), log_("stop after all prefix submissions retire"));
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
  auto allocated = endpoint.init(runtime, port.name());
  ZuCheckRT(allocated.is<void>(), log_(errorText(allocated.p<ZeException>())));
  if (!allocated.is<void>()) return;

  bool observed = false;
  auto started = endpoint.start([&endpoint, &observed, &input](uint8_t *packet, unsigned len, ZiRxBurst *) {
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
    accepted = endpoint.device->send(packet, sizeof(output)) == Zi::OK;
    endpoint.tx->free(packet);
    retired = endpoint.device->pollTx();
  });
  ZuCheckRT(accepted && retired == 1, log_("pcap retires its output submission once"));
  ZuCheckRT(endpoint.stop() && port.captured(output),
    log_("pcap file contains exactly the submitted Ethernet frame"));
}

static void testManyReturns(const Runtime &runtime)
{
  ZuTestScopeRT(testManyReturns);
  RingPort port;
  auto attached = port.init("packet", 128, {{0x02, 0, 0, 0, 0, 1}});
  ZuCheckRT(attached.is<void>(), log_(errorText(attached.p<ZeException>())));
  if (!attached.is<void>()) return;
  Device endpoint;
  auto opened = runtime.env()->nif(&endpoint.mx, port.name());
  ZuCheckRT(opened.is<Zpkt::Nif *>(), log_(errorText(opened.p<ZeException>())));
  if (!opened.is<Zpkt::Nif *>()) return;
  enum { Count = 60, Size = 64 };
  endpoint.device = opened.p<Zpkt::Nif *>();
  auto mtu = endpoint.device->mtu();
  endpoint.ingress = endpoint.device->bufPool(Zi::IOBufType::Rx, Count, mtu + RTE_ETHER_HDR_LEN);
  endpoint.tx = endpoint.device->bufPool(Zi::IOBufType::TxTCP, Count, mtu + RTE_ETHER_HDR_LEN);
  auto control = endpoint.device->bufPool(Zi::IOBufType::TxUDP, Count/6, Size, Size);
  endpoint.pools.pushNode(endpoint.tx.ptr());
  Zi::IOBufPool *reserves[]{control.ptr()};
  endpoint.device->reserveTxPools(reserves);
  auto allocated = endpoint.device->alloc(endpoint.ingress, endpoint.pools);
  ZuCheckRT(allocated.is<void>(), log_(errorText(allocated.p<ZeException>())));
  if (!allocated.is<void>()) return;
  auto started = endpoint.start([](uint8_t *, unsigned, ZiRxBurst *) {});
  ZuCheckRT(started.is<void>(), log_(errorText(started.p<ZeException>())));
  if (!started.is<void>()) return;

  Zpkt::TxPacket packets[Count]{};
  Zi::IOBufPool *pools[Count];
  unsigned count = 0;
  for (; count < Count; ++count) {
    auto pool = pools[count] = count % 6 == 5 ? control.ptr() : endpoint.tx.ptr();
    auto data = pool->alloc();
    if (!data) break;
    packets[count] = {data, Size};
    frame({data, Size}, count);
  }
  ZuCheckRT(count == Count, log_("claim mixed stream and exact-fit control allocations"));
  ReturnObserver obs;
  observer = &obs;

  unsigned accepted = 0, empty = 0;
  endpoint.onTx([&endpoint, &packets, &accepted, &empty, count] {
    accepted = endpoint.device->send({packets, count});
    empty = endpoint.device->pollTx();
  });
  ZuCheckRT(accepted == Count && !empty && !obs.visits,
    log_("holding sixty submissions gives an empty poll with zero slot visits"));
  rte_mbuf *held[Count]{};
  bool exact = accepted == Count;
  for (unsigned i = 0; i < accepted; ++i) {
    auto mbuf = held[i] = port.take();
    uint8_t expected[Size];
    frame(expected, i);
    exact &= mbuf && mbuf->pool == native(pools[i]) && rte_mbuf_refcnt_read(mbuf) == 1 &&
      rte_pktmbuf_mtod(mbuf, uint8_t *) == packets[i].data && mbuf->pkt_len == Size &&
      !memcmp(packets[i].data, expected, Size);
  }
  ZuCheckRT(exact, log_("mixed-pool burst preserves original direct mbufs, order and bytes"));

  if (exact) {
    rte_pktmbuf_free(held[Count - 1]);
    held[Count - 1] = nullptr;
    bool prefixBusy = true, lastBusy = false, lastReady = false;
    unsigned retired = 0;
    endpoint.onTx([&endpoint, &packets, &prefixBusy, &lastBusy, &lastReady, &retired] {
      lastBusy = endpoint.device->txReady(packets[Count - 1].data) == Zi::NotReady;
      retired = endpoint.device->pollTx();
      lastReady = endpoint.device->txReady(packets[Count - 1].data) == Zi::OK;
      for (unsigned i = 0; i < Count - 1; ++i)
	prefixBusy &= endpoint.device->txReady(packets[i].data) == Zi::NotReady;
    });
    ZuCheckRT(retired == 1 && obs.visits == 1 && lastBusy && lastReady && prefixBusy,
      log_("first poll visits only the last submitted mbuf and retires its exact slot"));

    // Park the owner in a live poll after it drains the first stream return.
    // Both producers then publish the remaining mbufs during that poll.
    rte_pktmbuf_free(held[Count - 2]);
    held[Count - 2] = nullptr;
    obs.owner.arm(BeforeClear);
    unsigned full = 0, rest = 0;
    ZmSemaphore polled;
    endpoint.mx.txRun([&endpoint, &full, &polled] {
      full = endpoint.device->pollTx();
      polled.post();
    });
    obs.owner.reached.wait();
    ZmSemaphore go, waiting;
    auto release = [&held, &go, &waiting](int start) {
      waiting.post();
      go.wait();
      for (int i = start; i >= 0; i -= 2)
	if (auto mbuf = held[i]) rte_pktmbuf_free(mbuf);
    };
    ZmThread first{[release] { release(Count - 2); }};
    ZmThread second{[release] { release(Count - 3); }};
    waiting.wait();
    waiting.wait();
    go.post();
    go.post();
    first.join();
    second.join();
    obs.owner.proceed.post();
    polled.wait();
    endpoint.onTx([&endpoint, &rest, &empty] {
      rest = endpoint.device->pollTx();
      empty = endpoint.device->pollTx();
    });
    ZuCheckRT(full == 32 && rest == Count - 33 && !empty && obs.visits == Count &&
	!obs.entries && obs.peak <= 2,
      log_("two producers return mixed pools out of order across bounded polls"));
  } else {
    for (auto mbuf : held) if (mbuf) rte_pktmbuf_free(mbuf);
  }
  for (unsigned i = 0; i < count; ++i) pools[i]->free(packets[i].data);
  ZuCheckRT(endpoint.stop(), log_("stop after mixed-pool submissions retire"));
  observer = nullptr;
}

static void testReturnRace(const Runtime &runtime, unsigned gate, bool pauseProducer)
{
  ZuTestScopeRT(testReturnRace);
  RingPort port;
  auto attached = port.init("packet", 8, {{0x02, 0, 0, 0, 0, 1}});
  ZuCheckRT(attached.is<void>(), log_(errorText(attached.p<ZeException>())));
  if (!attached.is<void>()) return;
  Device endpoint;
  auto allocated = endpoint.init(runtime, port.name(), 2);
  ZuCheckRT(allocated.is<void>(), log_(errorText(allocated.p<ZeException>())));
  if (!allocated.is<void>()) return;
  auto started = endpoint.start([](uint8_t *, unsigned, ZiRxBurst *) {});
  ZuCheckRT(started.is<void>(), log_(errorText(started.p<ZeException>())));
  if (!started.is<void>()) return;
  auto a = endpoint.tx->alloc(), b = endpoint.tx->alloc();
  if (!a || !b) {
    if (a) endpoint.tx->free(a);
    if (b) endpoint.tx->free(b);
    ZuCheckRT(false, log_("two caller allocations for the notification race"));
    return;
  }
  frame({a, 64}, 1);
  frame({b, 64}, 2);
  // A second interior pointer into the already claimed slot stops the prefix.
  const Zpkt::TxPacket duplicate[]{{a, 64}, {a + 1, 64}, {b, 64}};
  unsigned prefix = 0;
  Zu::IO::Result submitted = Zi::IOError;
  endpoint.onTx([&endpoint, &duplicate, b, &prefix, &submitted] {
    prefix = endpoint.device->send(duplicate);
    submitted = endpoint.device->send(b, 64);
  });
  auto first = port.take(), last = port.take();
  bool sent = prefix == 1 && submitted == Zi::OK && first && last &&
    rte_pktmbuf_mtod(first, uint8_t *) == a && rte_pktmbuf_mtod(last, uint8_t *) == b;
  ZuCheckRT(sent, log_("interior duplicate ends the prefix and a distinct slot remains eligible"));
  if (!sent) {
    if (first) rte_pktmbuf_free(first);
    if (last) rte_pktmbuf_free(last);
    port.drain();
    endpoint.tx->free(a);
    endpoint.tx->free(b);
    return;
  }
  ReturnObserver obs;
  observer = &obs;
  rte_pktmbuf_free(first);
  obs.owner.arm(gate);
  unsigned retired = 0;
  ZmSemaphore polled;
  endpoint.mx.txRun([&endpoint, &retired, &polled] {
    retired = endpoint.device->pollTx();
    polled.post();
  });
  obs.owner.reached.wait();
  ZmThread producer;
  if (pauseProducer) {
    obs.producer.arm(Published);
    ZmAssert_(!producer.run([last] { rte_pktmbuf_free(last); }));
    obs.producer.reached.wait();
  } else {
    rte_pktmbuf_free(last);
  }
  obs.owner.proceed.post();
  polled.wait();

  bool ready = false;
  unsigned extra = 0;
  endpoint.onTx([&endpoint, &retired, &extra, &ready, b] {
    retired += endpoint.device->pollTx();
    extra = endpoint.device->pollTx();
    ready = endpoint.device->txReady(b) == Zi::OK;
  });
  ZuCheckRT(retired == 2 && !extra && ready && obs.visits == 2 && obs.peak == 1,
    log_("each exact mbuf retires once without an extra send or duplicate announcement"));

  if (pauseProducer) {
    obs.stop.arm(Wait);
    ZmAtomic<unsigned> stopped{0};
    ZmSemaphore done;
    endpoint.device->stop([&stopped, &done](bool ok) {
      stopped = ok ? 1 : 2;
      done.post();
    });
    obs.stop.reached.wait();
    ZuCheckRT(!stopped, log_("stop waits for the publisher after its slot already retired"));
    obs.producer.proceed.post();
    producer.join();
    obs.stop.proceed.post();
    done.wait();
    ZuCheckRT(stopped == 1 && !obs.entries && obs.peak == 1,
      log_("stop drains the last publisher's residual pool announcement"));
  }
  endpoint.tx->free(a);
  endpoint.tx->free(b);
  ZuCheckRT(endpoint.stop(), log_("finish the notification race in a clean stopped run"));
  observer = nullptr;
}

static void testStopRecovery(const Runtime &runtime, bool atStop)
{
  ZuTestScopeRT(testStopRecovery);
  RingPort port;
  auto attached = port.init("packet", 8, {{0x02, 0, 0, 0, 0, 1}});
  ZuCheckRT(attached.is<void>(), log_(errorText(attached.p<ZeException>())));
  if (!attached.is<void>()) return;
  Device endpoint;
  auto allocated = endpoint.init(runtime, port.name(), 2);
  ZuCheckRT(allocated.is<void>(), log_(errorText(allocated.p<ZeException>())));
  if (!allocated.is<void>()) return;
  auto receive = [](uint8_t *, unsigned, ZiRxBurst *) {};
  auto started = endpoint.start(receive);
  ZuCheckRT(started.is<void>(), log_(errorText(started.p<ZeException>())));
  if (!started.is<void>()) return;
  auto packet = endpoint.tx->alloc();
  ZuCheckRT(packet, log_("allocate retained Tx backing"));
  if (!packet) return;
  uint8_t expected[64];
  frame(expected, 3);
  memcpy(packet, expected, sizeof(expected));
  bool accepted = false;
  endpoint.onTx([&endpoint, packet, &accepted] {
    accepted = endpoint.device->send(packet, 64) == Zi::OK;
  });
  auto held = port.take();
  ZuCheckRT(accepted && held, log_("ring peer withholds the final idle submission"));
  if (!held) {
    endpoint.tx->free(packet);
    return;
  }
  if (atStop) returnAtStop = held;
  bool stopped = endpoint.device->stop();
  ZuCheckRT(stopped == atStop && !returnAtStop,
    log_("stop handles a native return during deactivation or a driver withholding it"));
  if (!atStop) {
    endpoint.device->final();
    ZuCheckRT(endpoint.device->error() && endpoint.device->mx() == &endpoint.mx,
      log_("finalization retains the device while the driver still owns a submission"));
    ZuCheckRT(!endpoint.device->start(receive),
      log_("unresolved submission prevents a new run"));
    rte_pktmbuf_free(held);
  }
  auto restarted = endpoint.start(receive);
  ZuCheckRT(restarted.is<void>() && !memcmp(packet, expected, sizeof(expected)),
    log_("restart retires late returns and preserves caller bytes at the same address"));
  if (restarted.is<void>()) {
    endpoint.onTx([&endpoint, packet, &accepted] {
      accepted = endpoint.device->txReady(packet) == Zi::OK &&
	endpoint.device->send(packet, 64) == Zi::OK;
    });
    auto repeated = port.take();
    ZuCheckRT(accepted && repeated == held && rte_pktmbuf_mtod(repeated, uint8_t *) == packet,
      log_("retained frame resubmits its same direct native object after restart"));
    if (repeated) rte_pktmbuf_free(repeated);
    unsigned retired = 0, extra = 0;
    endpoint.onTx([&endpoint, &retired, &extra] {
      retired = endpoint.device->pollTx();
      extra = endpoint.device->pollTx();
    });
    ZuCheckRT(retired == 1 && !extra, log_("one completion belongs to the resubmission"));
  }
  endpoint.tx->free(packet);
  ZuCheckRT(endpoint.stop(), log_("stop the recovered run"));
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  if (geteuid()) {
    std::cout << "1..0 # SKIP requires private dummy kernel Nif fixture privileges\n";
    return 0;
  }
  ZuTestMain();
  alarm(45);
  Runtime runtime;
  auto result = runtime.init(true, ZPKT_DPDK_TEST_MODULE);
  ZuCHECK(result.is<void>(), errorText(result.p<ZeException>()));
  if (!result.is<void>()) return 1;
  ZuTestCall(testSteering, runtime);
  ZuTestCall(testRing, runtime);
  ZuTestCall(testPrefix, runtime);
  ZuTestCall(testLivePcap, runtime);
  ZuTestCall(testPcap, runtime);
  ZuTestCall(testManyReturns, runtime);
  ZuTestCall(testReturnRace, runtime, BeforeClear, false);
  ZuTestCall(testReturnRace, runtime, AfterClear, false);
  ZuTestCall(testReturnRace, runtime, BeforeClear, true);
  ZuTestCall(testReturnRace, runtime, AfterRecheck, false);
  ZuTestCall(testStopRecovery, runtime, true);
  ZuTestCall(testStopRecovery, runtime, false);
}
