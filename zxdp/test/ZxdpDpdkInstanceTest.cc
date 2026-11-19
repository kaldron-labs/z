//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <dlfcn.h>
#include <sched.h>
#include <iostream>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include <rte_eal.h>
#include <rte_errno.h>
#include <rte_mbuf.h>
#include <rte_ethdev.h>
#include <rte_lcore.h>
#include <rte_vect.h>

#include <zlib/ZuArray.hh>
#include <zlib/ZuTestUtil.hh>

#include <zlib/ZiMultiplex.hh>
#include <zlib/Zxdp.hh>
#include <zlib/ZxdpDpdk.hh>

using namespace ZuTestUtil;
using namespace Zxdp::Dpdk;

static ZtString<> errorText(const ZeException &error)
{
  ZeLogBuf msg;
  error.fn()(msg, error);
  return ZtString<>{msg.data(), msg.length()};
}

static ZeException caseError(const char *text, ZeError native = ZeOK)
{
  return ZeEXCEPT(Error, "Zxdp.DpdkTest", ([text, native](auto &out) {
    out << text;
    if (native) out << ": " << native;
  }));
}

static ZeException runCase(ZuCSpan scenario)
{
  auto eal = EAL::instance();
  if (eal->ready()) return caseError("EAL is ready before explicit initialization");

  EALParams params{};
  params.noPCI = params.noHuge = params.noShconf = params.noTelemetry = true;
  params.iova = IOVA::VA;
  params.procType = Proc::Primary;

  if (scenario == "duplicate" || scenario == "alias" ||
      scenario == "core-alias" || scenario == "optional") {
    if (scenario == "duplicate") {
      params.vdev = {"net_ring0"};
      params.args = {"--vdev=net_ring1"};
    } else if (scenario == "alias") {
      params.numaMem = "32";
      params.args = {"--socket-mem", "32"};
    } else if (scenario == "core-alias") {
      params.lcores = "0";
      params.args = {"-c1"};
    } else {
      params.lcores = "0";
      params.args = {"--huge-unlink", "--lcores=0"};
    }
    auto result = eal->init(params);
    if (!result.is<ZeException>() || eal->ready() ||
	!strstr(errorText(result.p<ZeException>()).ndata(), "named parameter and in args"))
      return caseError("duplicate options did not produce the wrapper's structured error");
    if (!eal->init({}).is<ZeException>() || eal->ready())
      return caseError("a failed initialization allowed a second attempt");
    return {};
  }

  if (scenario == "native-error") {
    params.maxSIMDBits = 1U;
    auto result = eal->init(params);
    if (!result.is<ZeException>() || eal->ready() ||
	!strstr(errorText(result.p<ZeException>()).ndata(), "rte_eal_init failed"))
      return caseError("native SIMD validation failure was not returned");
    return {};
  }

  cpu_set_t allowed;
  if (sched_getaffinity(0, sizeof(allowed), &allowed) < 0)
    return caseError("cannot inspect initial affinity", ZeLastError);
  // Exercise a mask that excludes CPU zero whenever the environment permits it.
  auto before = allowed;
  CPU_CLR(0, &before);
  if (!CPU_COUNT(&before)) before = allowed;
  if (sched_setaffinity(0, sizeof(before), &before) < 0)
    return caseError("cannot set test affinity", ZeLastError);

  const char *device = "net_ring0";
  cpu_set_t expected = before;
  unsigned mainLcore = 0;
  if (scenario == "automatic") {
    params.vdev = {device, "net_ring1"};
    mainLcore = 3;
    params.mainLcore = mainLcore;
  } else if (scenario == "raw") {
    int cpu = 0;
    while (cpu < CPU_SETSIZE && !CPU_ISSET(cpu, &before)) ++cpu;
    if (cpu == CPU_SETSIZE) return caseError("no allowed CPU for explicit mapping");
    EALParams::String mapping;
    mapping << "0@" << cpu;
    params.args = {"-l", ZuMv(mapping), "--vdev", device, "--file-prefix", "zxdp test"};
    params.maxSIMDBits = 0U;
    CPU_ZERO(&expected);
    CPU_SET(cpu, &expected);
  } else {
    return caseError("unknown test case");
  }

  auto result = eal->init(params);
  if (result.is<ZeException>()) return ZuMv(result).p<ZeException>();
  if (!eal->ready() || rte_lcore_count() != 1 || rte_get_main_lcore() != mainLcore)
    return caseError("EAL did not enable exactly one main lcore");
  if (!eal->init({}).is<ZeException>() || !eal->ready())
    return caseError("a second EAL init did not preserve the initialized state");
  cpu_set_t after;
  if (sched_getaffinity(0, sizeof(after), &after) < 0)
    return caseError("cannot inspect final affinity", ZeLastError);
  if (!CPU_EQUAL(&expected, &after))
    return caseError("initializing thread did not retain its intended affinity");
  uint16_t port;
  if (int rc = rte_eth_dev_get_port_by_name(device, &port))
    return caseError("EAL did not register the requested ring PMD", ZeError{-rc});
  if (scenario == "automatic") {
    if (int rc = rte_eth_dev_get_port_by_name("net_ring1", &port))
      return caseError("EAL did not register the second named vdev", ZeError{-rc});
    if (rte_vect_set_max_simd_bitwidth(RTE_VECT_SIMD_DISABLED))
      return caseError("omitting the SIMD bitwidth forced a limit");
  } else {
    if (rte_vect_get_max_simd_bitwidth() != RTE_VECT_SIMD_MAX ||
	rte_vect_set_max_simd_bitwidth(RTE_VECT_SIMD_DISABLED) != -EPERM)
      return caseError("an explicit zero did not force an unlimited SIMD bitwidth");
    if (params.args[4] != "--file-prefix" || params.args[5] != "zxdp test" ||
	!strstr(rte_eal_get_runtime_dir(), "/zxdp test"))
      return caseError("raw arguments did not remain separate, unmodified tokens");
  }
  return {};
}

static bool cleanRuntime(const char *dir)
{
  ZtString<> runtime{ZuCSpan{dir}};
  runtime << "/dpdk";
  for (auto prefix : {"rte", "zxdp test"}) {
    ZtString<> path = runtime;
    path << '/' << prefix;
    rmdir(path);
  }
  rmdir(runtime);
  return !rmdir(dir);
}

static void testEALCLI()
{
  ZuTestScopeRT(testEALCLI);

  char cli[] = "app --main-lcore=0 --no-pci --iova-mode=va --file-prefix='zxdp test'";
  ZfCLI::InCLI in{cli};
  EALParams fromCLI;
  ZuCheckRT(ZfCLI::load(fromCLI, in.argv) == 1);
  ZuCheckRT(*fromCLI.mainLcore && fromCLI.mainLcore.val() == 0);
  ZuCheckRT(fromCLI.noPCI && fromCLI.iova == IOVA::VA && fromCLI.filePrefix == "zxdp test");
  ZuCheckRT(!*fromCLI.memoryMB && fromCLI.procType == Proc::Default);

  const char *argv[] = {
    "app", "-a", "0000:00:00.0", "-a", "0000:00:00.1",
    "-m", "128", "--proc-type=auto", "--base-virtaddr=0xabc"
  };
  EALParams fromArgv;
  ZuCheckRT(ZfCLI::load(fromArgv, sizeof(argv) / sizeof(*argv), argv) == 1);
  ZuCheckRT(fromArgv.allow.length() == 2 &&
    fromArgv.allow[0] == "0000:00:00.0" && fromArgv.allow[1] == "0000:00:00.1");
  ZuCheckRT(fromArgv.memoryMB == 128 && fromArgv.procType == Proc::Auto);
  ZuCheckRT(fromArgv.baseVirtAddr == 0xabc && !*fromArgv.mainLcore);
}

static void testEAL()
{
  ZuTestScopeRT(testEAL);
  const char *cases[] = {
    "automatic", "raw", "duplicate", "alias", "core-alias", "optional", "native-error"
  };
  for (auto scenario : cases) {
    char dir[] = "/tmp/ZxdpDpdkInstanceTest.XXXXXX";
    if (!mkdtemp(dir)) {
      ZuCheckRT(false, log_("create isolated DPDK runtime directory"));
      continue;
    }
    auto pid = fork();
    if (!pid) {
      if (setenv("RUNTIME_DIRECTORY", dir, 1) < 0) _exit(126);
      execl("/proc/self/exe", "ZxdpDpdkInstanceTest", "--case", scenario, nullptr);
      _exit(127);
    }
    int status = 0;
    pid_t waited = -1;
    if (pid > 0)
      do waited = waitpid(pid, &status, 0); while (waited < 0 && errno == EINTR);
    ZuCheckRT(pid > 0 && waited == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0,
      log_("fresh EAL process [", scenario, "] status ", status));
    ZuCheckRT(cleanRuntime(dir), log_("remove test runtime directory"));
  }
}

static ZmAtomic<unsigned> poolFailure{0};
static ZmAtomic<unsigned> startFailure{0};
static ZuCArray<RTE_MEMPOOL_NAMESIZE> provisional;

extern "C" __attribute__((visibility("default")))
rte_mempool *rte_pktmbuf_pool_create(
  const char *name, unsigned count, unsigned cache, uint16_t priv, uint16_t room, int socket)
{
  static const auto create = reinterpret_cast<decltype(&rte_pktmbuf_pool_create)>(
    dlsym(RTLD_NEXT, "rte_pktmbuf_pool_create"));
  if (unsigned remaining = poolFailure) {
    if (!strncmp(name, "zxdp_", 5)) {
      --poolFailure;
      if (remaining == 1) {
	rte_errno = ENOMEM;
	return nullptr;
      }
      provisional = ZuCSpan{name};
    }
  }
  return create(name, count, cache, priv, room, socket);
}

extern "C" __attribute__((visibility("default")))
int rte_eth_dev_start(uint16_t port)
{
  static const auto start = reinterpret_cast<decltype(&rte_eth_dev_start)>(
    dlsym(RTLD_NEXT, "rte_eth_dev_start"));
  if (startFailure.xch(0)) return -EIO;
  return start(port);
}

static void runDevice()
{
  EALParams ealParams{};
  ealParams.noPCI = ealParams.noHuge = ealParams.noShconf = ealParams.noTelemetry = true;
  ealParams.vdev = {"net_ring0"};
  auto eal = EAL::instance();
  bool missingEAL = false;
  try { Zxdp::instance(Zxdp::Type::Dpdk); }
  catch (const ZeException &error) { missingEAL = !!error; }
  ZuCheckRT(missingEAL, log_("DPDK factory requires initialized EAL"));
  auto ealResult = eal->init(ealParams);
  ZuCheckRT(ealResult.is<void>(), log_(errorText(ealResult.p<ZeException>())));
  if (ealResult.is<ZeException>()) return;

  ZiMultiplex mx;
  auto device = Zxdp::instance(Zxdp::Type::Dpdk);
  auto other = Zxdp::instance(Zxdp::Type::Dpdk);
  Zxdp::Params config(
    Zxdp::Params::New<Zxdp::Params::Index<Zxdp::Dpdk::Params>{}>{});
  auto &params = config.p<Zxdp::Dpdk::Params>();
  params.name = "net_ring_absent";
  ZuCheckRT(device->init(&mx, config).is<ZeException>() && !device->mx() && !device->mtu(),
    log_("missing native port gives a synchronous error without device state"));
  params.name = "net_ring0";
  params.ifindex = unsigned(-1);
  ZuCheckRT(device->init(&mx, config).is<ZeException>() && !device->mx() && !device->mtu(),
    log_("invalid Linux link rolls back partial open"));
  params.ifindex = 0;
  bool opened = device->init(&mx, config).is<void>();
  ZuCheckRT(opened && device->mx() == &mx && device->mtu() == 1500 &&
      device->mac().length() == 6 && device->rxQueues() == 1 && !device->ifindex() &&
      !device->allocated() && !device->ready() && !device->everStarted() && device->zeroCopy(),
    log_("successful init discovers a cold ring port without requiring pools or callbacks"));
  if (!opened) return;

  ZuCheckRT(device->init(&mx, config).is<void>(), log_("repeat stopped init is idempotent"));
  ++params.rxInterval;
  ZuCheckRT(device->init(&mx, config).is<ZeException>() && device->mx() == &mx,
    log_("changed idle interval is a recoverable configuration conflict"));
  --params.rxInterval;
  auto busy = other->init(&mx, config);
  ZuCheckRT(busy.is<ZeException>() && !other->mx() &&
      strstr(errorText(busy.p<ZeException>()).ndata(), rte_strerror(EBUSY)),
    log_("a competing instance gets a synchronous port-busy exception"));

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
    auto receive = [&rx](uint8_t *packet, unsigned) { rx->free(packet); };
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
    ZuCheckRT(ready && device->ready() && device->everStarted() && intact(),
    log_(  "the same pool addresses survive activation retry"));
    ZuCheckRT(device->stop() && stops == 1 && !device->ready() && device->allocated() &&
	intact() && other->init(&mx, config).is<ZeException>(),
    log_(  "idle stop preserves the allocation and the exclusive port claim"));
    bool restarted = device->start(receive, stopped);
    ZuCheckRT(restarted && device->ready() && intact(),
    log_(  "restart reuses all native pool backing, including a retained small slot"));
    ZuCheckRT(device->stop() && stops == 2, log_("stop restarted DPDK device"));
    ZuCheckRT(mx.stop(), log_("stop direct-device scheduler after the device quiesces"));
  }
  if (retained) small->free(retained);
  device->final();
  ZuCheckRT(!device->mx() && !device->allocated() &&
      !rx->alloc() && !tx->alloc() && !small->alloc(),
    log_("final retires native backing and releases the port"));
  ZuCheckRT(other->init(&mx, config).is<void>(), log_("reopen the same port after final"));
  other->final();
  ZuCheckRT(device->init(&mx, config).is<void>() && device->alloc(rx, pools).is<void>(),
    log_("standalone device can provision the existing pool objects on a new open"));
  device->final();
  ZuCheckRT(!device->mx() && !small->alloc(), log_("final after allocation without start"));
}

static void testDevice()
{
  ZuTestScopeRT(testDevice);
  char dir[] = "/tmp/ZxdpDpdkInstanceTest.XXXXXX";
  auto runtime = mkdtemp(dir);
  ZuCheckRT(runtime, log_("create device test runtime directory"));
  if (!runtime) return;
  int configured = setenv("RUNTIME_DIRECTORY", dir, 1);
  ZuCheckRT(!configured, log_("configure EAL test runtime directory"));
  if (!configured) runDevice();
  ZuCheckRT(cleanRuntime(dir), log_("remove device test runtime directory"));
}

int main(int argc, char **argv)
{
  if (argc == 3 && ZuCSpan{argv[1]} == "--case") {
    if (auto error = runCase(argv[2])) {
      std::cerr << argv[2] << ": " << errorText(error) << '\n';
      return 1;
    }
    return 0;
  }
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testEALCLI);
  ZuTestCall(testEAL);
  ZuTestCall(testDevice);
}
