//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Named native device construction and retained allocation lifecycle.

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <dirent.h>
#include <stdlib.h>
#include <net/if.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <linux/rtnetlink.h>
#include <dlfcn.h>
#include <string.h>
#include <unistd.h>
#include <iostream>

#ifdef HAVE_DPDK
#include <rte_errno.h>
#include <rte_mbuf.h>
#include <rte_ethdev.h>
#endif

#include <zlib/ZuArray.hh>
#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmAtomic.hh>
#include <zlib/ZiMultiplex.hh>
#include <zlib/ZpktNif.hh>
#include <zlib/ZpktEnv.hh>

#include "ZpktTestCf.hh"
#ifdef HAVE_DPDK
#include "zpktdkfixture.hh"
#endif

using namespace ZuTestUtil;
static ZmAtomic<unsigned> nifQueries{0};

extern "C" __attribute__((visibility("default")))
ssize_t sendto(int fd, const void *data, size_t size, int flags,
    const sockaddr *address, socklen_t length)
{
  if (address && length >= sizeof(sa_family_t) && address->sa_family == AF_NETLINK &&
      data && size >= sizeof(nlmsghdr) &&
      static_cast<const nlmsghdr *>(data)->nlmsg_type == RTM_GETLINK) ++nifQueries;
  return syscall(SYS_sendto, fd, data, size, flags, address, length);
}

enum { Sender = 3 };
static void configureSender(ZmSchedParams &scheduler)
{
  scheduler.thread(Sender, [](auto &thread) { thread.isolated(true); });
}

static unsigned fdCount()
{
  unsigned count = 0;
  auto dir = opendir("/proc/self/fd");
  ZmAssert_(dir);
  while (auto entry = readdir(dir)) if (entry->d_name[0] != '.') ++count;
  closedir(dir);
  return count;
}

static void testFactory()
{
  ZuTestScopeRT(testFactory);
  auto index = unsigned(strtoul(getenv("ZPKT_TEST_IFINDEX"), nullptr, 10));
  char text[IF_NAMESIZE]{};
  ZmAssert_(if_indextoname(index, text));
  ZuCSpan name{static_cast<const char *>(text)};
  auto queried = Zpkt::nifQuery(name);
  ZuCheckRT(queried.is<Zpkt::NifInfo>());
  if (queried.is<ZeException>()) return;
  auto info = queried.p<Zpkt::NifInfo>();
  ZmRef<Zpkt::Env> env = new Zpkt::Env;
  ZiMultiplex mx{ZiMxParams{}.scheduler(configureSender)};
  {
    auto cf = ZpktTest::config(ZPKT_XDP_MODULE, name, Sender);
    env->init(cf.ptr());
  }
  ZiMultiplex wrong;
  auto resources = fdCount(), requests = unsigned(nifQueries);
  ZuCheckRT(!env->loopback() && env->nif(&mx, "_loop").is<ZeException>() &&
      unsigned(nifQueries) == requests && fdCount() == resources,
    log_("loaded AF_XDP reports no backend loopback and performs no host lookup"));
  auto opened = env->nif(&mx, name);
  ZuCheckRT(opened.is<Zpkt::Nif *>(), log_("named factory opens AF_XDP after source Cf release"));
  if (opened.is<ZeException>()) return;
  ZmRef<Zpkt::Nif> nif = opened.p<Zpkt::Nif *>();
  ZuCheckRT((nif->env() == env.ptr() && nif->mx() == &mx &&
      nif->state() == ZmEngineState::Stopped && !nif->allocated() && !nif->ready() &&
      nif->ifindex() == info.ifindex && nif->mtu() == info.mtu &&
      nif->rxQueues() == info.rxQueues && nif->mac() == ZuBSpan{info.mac, 6} &&
      unsigned(nifQueries) == requests + 1 && fdCount() == resources + 2),
    log_("factory publishes trusted geometry once with only stopped native descriptors"));
  auto repeated = env->nif(&mx, name);
  auto conflict = env->nif(&wrong, name);
  ZuCheckRT(repeated.is<Zpkt::Nif *>() && repeated.p<Zpkt::Nif *>() == nif.ptr() &&
      conflict.is<ZeException>() && unsigned(nifQueries) == requests + 1,
    log_("cache hit and multiplexer conflict make no additional native query"));
  nif->final();
  nif = nullptr;
  ZuCheckRT(fdCount() == resources, log_("final releases all construction descriptors"));
  auto absent = env->nif(&mx, "zpkt-absent");
  ZuCheckRT(absent.is<ZeException>() && fdCount() == resources &&
      unsigned(nifQueries) == requests + 1,
    log_("bad Linux key returns an error without opening native resources"));
  ZmRef<Zpkt::Env> invalid = new Zpkt::Env;
  auto bad = ZpktTest::config(ZPKT_XDP_MODULE, name, mx.rxThread());
  invalid->init(bad.ptr());
  auto refused = invalid->nif(&mx, name);
  auto retry = invalid->nif(&mx, name);
  ZuCheckRT(refused.is<ZeException>() && retry.is<ZeException>() && fdCount() == resources &&
      unsigned(nifQueries) == requests + 3,
    log_("invalid sender slot leaves no native resources or cached device"));
  ZuCheckRT(invalid->final().is<void>() && env->final().is<void>());
}

#ifdef HAVE_DPDK
using namespace ZpktDpdkTest;

struct NifOwner {
  ZmRef<Zpkt::Nif> nif;
  ~NifOwner() { if (nif) nif->final(); }
};

static ZmAtomic<unsigned> poolFailure{0};
static ZmAtomic<unsigned> startFailure{0};
static ZuCArray<RTE_MEMPOOL_NAMESIZE> provisional;

extern "C" __attribute__((visibility("default")))
int rte_mempool_set_ops_byname(rte_mempool *pool, const char *name, void *config)
{
  static const auto setOps = reinterpret_cast<decltype(&rte_mempool_set_ops_byname)>(
    dlsym(RTLD_NEXT, "rte_mempool_set_ops_byname"));
  if (unsigned remaining = poolFailure) {
    if (!strcmp(name, "zpkt_tx_returns")) {
      --poolFailure;
      if (remaining == 1) return -ENOMEM;
      provisional = ZuCSpan{pool->name};
    }
  }
  return setOps(pool, name, config);
}

extern "C" __attribute__((visibility("default")))
int rte_eth_dev_start(uint16_t port)
{
  static const auto start = reinterpret_cast<decltype(&rte_eth_dev_start)>(
    dlsym(RTLD_NEXT, "rte_eth_dev_start"));
  if (startFailure.xch(0)) return -EIO;
  return start(port);
}

static void testDpdk(const Runtime &runtime)
{
  ZuTestScopeRT(testDpdk);
  RingPort port;
  auto attached = port.init("packet", 64, {{0x02, 0, 0, 0, 0, 1}});
  ZuCheckRT(attached.is<void>(), log_(errorText(attached.p<ZeException>())));
  if (!attached.is<void>()) return;
  auto env = runtime.env();
  ZiMultiplex mx, otherMx;
  auto requests = unsigned(nifQueries);
  auto missing = env->nif(&mx, "missing");
  ZuCheckRT(missing.is<ZeException>(), log_("missing native port publishes no device"));
  auto badLinux = env->nif(&mx, "zpkt_absent");
  ZuCheckRT(badLinux.is<ZeException>() && unsigned(nifQueries) == requests,
    log_("missing configured DPDK key fails without host lookup"));
  auto opened = env->nif(&mx, "net_ring_packet");
  ZuCheckRT(opened.is<Zpkt::Nif *>(), log_(errorText(opened.p<ZeException>())));
  if (!opened.is<Zpkt::Nif *>()) return;
  ZmRef<Zpkt::Nif> device = opened.p<Zpkt::Nif *>();
  NifOwner owner{device};
  ZuCheckRT(device->mx() == &mx && device->mtu() == 1500 &&
      device->mac().length() == 6 && device->rxQueues() == 1 && !device->ifindex() &&
      unsigned(nifQueries) == requests &&
      !device->allocated() && !device->ready() && device->state() == ZmEngineState::Stopped && device->zeroCopy(),
    log_("factory returns initialized, unallocated, stopped ring device with native geometry"));
  auto repeated = env->nif(&mx, "net_ring_packet");
  ZuCheckRT(repeated.is<Zpkt::Nif *>() && repeated.p<Zpkt::Nif *>() == device.ptr(),
    log_("same-name lookup retains the initialized device"));
  ZuCheckRT(env->nif(&otherMx, "net_ring_packet").is<ZeException>() && device->mx() == &mx,
    log_("different multiplexer lookup preserves original initialization"));
  auto busy = env->nif(&mx, "competing");
  ZuCheckRT(busy.is<ZeException>() &&
      strstr(errorText(busy.p<ZeException>()).ndata(), rte_strerror(EBUSY)),
    log_("a different configured key cannot acquire an owned native port"));

  auto rx = device->bufPool(Zi::IOBufType::Rx, 32, 1514);
  auto tx = device->bufPool(Zi::IOBufType::TxTCP, 16, 1514);
  auto small = device->bufPool(Zi::IOBufType::TxUDP, 1, 96, 8, 4);
  Zi::IOBufPoolList pools;
  pools.pushNode(tx.ptr());
  pools.pushNode(small.ptr());
  ZuCheckRT(!rx->alloc() && !tx->alloc() && !small->alloc(),
    log_("typed native pools have no slots before allocation"));

  poolFailure = 2;
  auto failed = device->alloc(rx, pools);
  ZuCheckRT(failed.is<ZeException>() && !poolFailure && !!provisional &&
      strstr(errorText(failed.p<ZeException>()).ndata(), rte_strerror(ENOMEM)) &&
      !rte_mempool_lookup(provisional.terminate()) && !device->allocated() &&
      !rx->alloc() && !tx->alloc() && !small->alloc(),
    log_("partial native allocation failure frees provisional backing"));
  poolFailure = 0;
  bool allocated = device->alloc(rx, pools).is<void>();
  ZuCheckRT(allocated && device->allocated() && device->alloc(rx, pools).is<void>(),
    log_("repeating allocation with the original set succeeds without replacing its storage"));
  if (!allocated) return;

  auto replacement = device->bufPool(Zi::IOBufType::TxTCP, 4, 256);
  Zi::IOBufPoolList different;
  different.pushNode(replacement.ptr());
  ZuCheckRT(device->alloc(rx, different).is<ZeException>() && !replacement->alloc() &&
      pools.headPtr() == tx.ptr() && pools.tailPtr() == small.ptr(),
    log_("changed pool identity preserves the original pool set and intrusive membership"));

  auto retained = small->alloc();
  ZuCheckRT(retained && !small->alloc(),
    log_("the small Tx pool provides one slot before start"));
  if (retained) memset(retained, 0x5a, small->size);
  auto intact = [retained, &small]() {
    if (!retained) return false;
    for (unsigned i = 0; i < small->size; ++i)
      if (retained[i] != 0x5a) return false;
    return !small->alloc();
  };
  bool started = mx.start();
  ZuCheckRT(started, log_("start the direct-device scheduler"));
  if (started) {
    auto receive = [&rx](uint8_t *packet, unsigned, ZiRxBurst *) { rx->free(packet); };
    ZmAtomic<unsigned> stops{0};
    auto stopped = [&stops] { ++stops; };
    startFailure = 1;
    bool activated = device->start(receive, stopped);
    auto activationError = device->error();
    ZuCheckRT(!activated && !startFailure && activationError &&
        strstr(errorText(activationError).ndata(), rte_strerror(EIO)) && !device->ready() &&
	device->state() == ZmEngineState::Stopped && device->allocated() && intact() && !stops,
    log_(  "failed PMD activation leaves the native allocation and caller-held slot intact"));
    startFailure = 0;
    bool ready = device->start(receive, stopped);
    ZuCheckRT(ready && device->ready() && intact(),
    log_(  "the same pool addresses survive activation retry"));
    ZuCheckRT(device->stop() && stops == 1 && !device->ready() && device->allocated() &&
	intact() && env->nif(&otherMx, "competing").is<ZeException>(),
    log_(  "idle stop preserves the allocation and the exclusive port claim"));
    bool restarted = device->start(receive, stopped);
    ZuCheckRT(restarted && device->ready() && intact(),
    log_(  "restart reuses all native pool backing, including a retained small slot"));
    ZuCheckRT(device->stop() && stops == 2, log_("stop restarted DPDK device"));

    ZmSemaphore retiring, release, settled, canceled;
    ZmAtomic<unsigned> stopResults{0}, startResult{0}, canceledRuns{0};
    bool began = device->start(receive, [&retiring, &release] {
      retiring.post();
      release.wait();
    });
    ZuCheckRT(began, log_("start a run for the queued-start cancellation case"));
    if (began) {
      auto refs = rx->refCount();
      device->stop([&stopResults, &rx, refs](bool ok) {
	stopResults += ok && rx->refCount() == refs;
      });
      bool retiringRun = !retiring.timedwait(Zm::now(5));
      ZuCheckRT(retiringRun, log_("wait for native stop to enter retirement"));
      if (retiringRun) {
	device->start(receive, [pool = rx, &canceledRuns] { if (pool) ++canceledRuns; },
	  [&startResult, &canceled](bool ok) {
	    startResult = ok ? 2 : 1;
	    canceled.post();
	  });
	ZuCheckRT(device->state() == ZmEngineState::StartPending && rx->refCount() == refs + 1);
	device->stop([&stopResults](bool ok) { stopResults += ok; });
	ZuCheckRT(device->state() == ZmEngineState::Stopping && !stopResults && !startResult);
      }
      release.post();
      device->stop([&settled](bool) { settled.post(); });
      ZuCheckRT(!settled.timedwait(Zm::now(5)));
      if (retiringRun) {
	ZuCheckRT(!canceled.timedwait(Zm::now(5)));
	ZuCheckRT(stopResults == 2 && startResult == 1 && !canceledRuns &&
	  device->state() == ZmEngineState::Stopped);
      }
    }
    ZmSemaphore nextStarted;
    ZmAtomic<unsigned> starts{0};
    auto queueRestart = [&] {
      device->start(receive, stopped, [&](bool ok) { starts += ok; });
      device->start(receive, [&] { stops += 100; }, [&](bool ok) {
	starts += ok;
	nextStarted.post();
      });
    };
    bool active = device->start(receive, queueRestart);
    ZuCheckRT(active);
    if (active) {
      ZuCheckRT(device->stop() && !nextStarted.timedwait(Zm::now(5)) && starts == 2);
      ZuCheckRT(device->ready() && device->stop() && stops == 3,
	log_("a repeated start joins the queued restart and keeps its original callbacks"));
    }
    ZuCheckRT(mx.stop(), log_("stop direct-device scheduler after the device quiesces"));
  }
  if (retained) small->free(retained);
  device->final();
  ZuCheckRT(!device->mx() && !device->allocated() &&
      !rx->alloc() && !tx->alloc() && !small->alloc(),
    log_("final retires native backing and releases the port"));
  auto recreated = env->nif(&mx, "net_ring_packet");
  ZuCheckRT(recreated.is<Zpkt::Nif *>() && recreated.p<Zpkt::Nif *>() != device.ptr(),
    log_("successful finalization permits a new device for the same name"));
  if (!recreated.is<Zpkt::Nif *>()) return;
  ZmRef<Zpkt::Nif> next = recreated.p<Zpkt::Nif *>();
  device->final();
  auto current = env->nif(&mx, "net_ring_packet");
  ZuCheckRT(current.is<Zpkt::Nif *>() && current.p<Zpkt::Nif *>() == next.ptr(),
    log_("old finalized device cannot retire its replacement"));
  ZuCheckRT(next->alloc(rx, pools).is<void>(),
    log_("replacement can provision the existing pool objects"));
  next->final();
  ZuCheckRT(!next->mx() && !small->alloc(), log_("final after allocation without start"));

  // The configured selector has a different MAC from its native ring port.
  constexpr uint8_t expectedMAC[] = {2, 0, 0, 0, 0, 1};
  auto independent = env->nif(&mx, "competing");
  ZuCheckRT(independent.is<Zpkt::Nif *>(),
    log_("independent Linux key selects the configured native port after retirement"));
  if (independent.is<ZeException>()) return;
  ZmRef<Zpkt::Nif> paired = independent.p<Zpkt::Nif *>();
  NifOwner pairedOwner{paired};
  auto cached = env->nif(&mx, "competing");
  ZuCheckRT((paired->name() == "competing" &&
      !paired->ifindex() && paired->mtu() == 1500 &&
      paired->mac() == ZuBSpan{expectedMAC, sizeof(expectedMAC)} &&
      unsigned(nifQueries) == requests && cached.is<Zpkt::Nif *>() &&
      cached.p<Zpkt::Nif *>() == paired.ptr()),
    log_("configured selector and cache expose the native port without a host query"));
  paired->final();
}

#endif // HAVE_DPDK

int main(int argc, char **argv)
{
  parse(argc, argv);
  if (geteuid()) {
    std::cout << "1..0 # SKIP requires private dummy kernel Nif fixture privileges\n";
    return 0;
  }
  auto mode = getenv("ZPKT_NIF_CASE");
  if (mode && !strcmp(mode, "xdp")) {
    if (!getenv("ZPKT_TEST_IFINDEX")) {
      std::cout << "1..0 # SKIP requires fixture-owned Linux interface index\n";
      return 0;
    }
    ZuTestMain();
    ZuTestCall(testFactory);
    return 0;
  }
#ifdef HAVE_DPDK
  ZuTestMain();
  alarm(30);
  Runtime runtime;
  auto result = runtime.init();
  ZuCHECK(result.is<void>(), errorText(result.p<ZeException>()));
  if (!result.is<void>()) return 1;
  ZuTestCall(testDpdk, runtime);
#else
  std::cout << "1..0 # SKIP select ZPKT_NIF_CASE=xdp with a private fixture\n";
#endif
}
