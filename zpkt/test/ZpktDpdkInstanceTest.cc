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

#include <zlib/ZmAtomic.hh>

#include <zlib/ZiMultiplex.hh>
#include <zlib/Zpkt.hh>
#include "ZpktDpdk.hh"
#include "ZpktTestCf.hh"
#include <zlib/ZpktEnv.hh>

using namespace ZuTestUtil;
using namespace Zpkt::Dpdk;

static ZtString<> errorText(const ZeException &error)
{
  ZeLogBuf msg;
  error.fn()(msg, error);
  return ZtString<>{msg.data(), msg.length()};
}

static ZeException caseError(const char *text, ZeError native = ZeOK)
{
  return ZeEXCEPT(Error, "Zpkt.DpdkTest", ([text, native](auto &out) {
    out << text;
    if (native) out << ": " << native;
  }));
}

static ZmAtomic<unsigned> affinityFailure{0};
static unsigned cleanupCalls = 0;
static bool nativeInitialized = false;

extern "C" __attribute__((visibility("default")))
int sched_setaffinity(pid_t pid, size_t size, const cpu_set_t *mask) noexcept
{
  static const auto native = reinterpret_cast<decltype(&sched_setaffinity)>(
    dlsym(RTLD_NEXT, "sched_setaffinity"));
  if (nativeInitialized && affinityFailure.xch(0)) { errno = EIO; return -1; }
  return native(pid, size, mask);
}

extern "C" __attribute__((visibility("default"))) int rte_eal_init(int argc, char **argv)
{
  static const auto native = reinterpret_cast<decltype(&rte_eal_init)>(
    dlsym(RTLD_NEXT, "rte_eal_init"));
  int result = native(argc, argv);
  nativeInitialized = result >= 0;
  return result;
}

extern "C" __attribute__((visibility("default"))) int rte_eal_cleanup()
{
  static const auto native = reinterpret_cast<decltype(&rte_eal_cleanup)>(
    dlsym(RTLD_NEXT, "rte_eal_cleanup"));
  ++cleanupCalls;
  return native();
}

static ZeException cleanupCase()
{
  cpu_set_t before;
  if (sched_getaffinity(0, sizeof(before), &before) < 0)
    return caseError("cannot inspect initial affinity", ZeLastError);
  {
    EAL eal;
    EALParams params;
    params.noPCI = params.noHuge = params.noShconf = params.noTelemetry = true;
    affinityFailure = 1;
    auto result = eal.init(params);
    if (affinityFailure || !result.is<ZeException>() || eal.ready() ||
	!strstr(errorText(result.p<ZeException>()).ndata(), "restore"))
      return caseError("post-native-init affinity restoration failure was not injected");
  }
  if (cleanupCalls != 1)
    return caseError("native success followed by later failure did not clean EAL");
  if (sched_setaffinity(0, sizeof(before), &before) < 0)
    return caseError("cannot restore test affinity", ZeLastError);
  EAL replacement;
  if (!replacement.init({}).is<ZeException>())
    return caseError("destroying an EAL owner allowed another native initialization attempt");
  return {};
}

static ZeException configCase(ZuCSpan scenario)
{
  auto ealOptions = [](ZpktTest::Node *root) {
    auto eal = ZpktTest::object(root, "eal");
    for (auto flag : {"noPCI", "noHuge", "noShconf", "noTelemetry"})
      ZpktTest::value(eal, flag, "true");
    ZpktTest::value(eal, "iova", "va");
  };
  auto cf = ZpktTest::object();
  ealOptions(cf.ptr());
  ZpktTest::value(cf.ptr(), "module", ZPKT_DPDK_MODULE);
  auto nif = ZpktTest::object(ZpktTest::object(cf.ptr(), "interfaces"), "dummy");
  char longName[RTE_ETH_NAME_MAX_LEN];
  memset(longName, 'x', sizeof(longName));
  constexpr char embedded[] = {'n', 'e', 't', 0, 'r', 'i', 'n', 'g'};
  ZuCSpan port;
  if (scenario == "config-long") port = {longName, sizeof(longName)};
  else if (scenario == "config-nul") port = {embedded, sizeof(embedded)};
  ZpktTest::value(nif, "port", port);
  ZmRef<Zpkt::Env> env = new Zpkt::Env;
  ZeException failure;
  try { env->init(cf); }
  catch (const ZeException &error) { failure = error; }
  ZeException presentation = failure;
  auto message = presentation ? errorText(presentation) : ZtString<>{};
  if (!failure || failure.component != "Zpkt.Dpdk"_z ||
      !strstr(message.ndata(), "invalid configured DPDK port") ||
      !strstr(message.ndata(), "dummy") || nativeInitialized)
    return caseError("invalid port configuration was not rejected before native EAL initialization");
  if (!env->final().is<void>())
    return caseError("invalid configuration published a Plugin");

  // A new valid configuration in this same child still gets the process's
  // only EAL initialization attempt. No named Linux interface or port is needed.
  cf = ZpktTest::config(ZPKT_DPDK_MODULE);
  ealOptions(cf.ptr());
  try { env->init(cf); }
  catch (const ZeException &error) { return error; }
  cf = nullptr;
  if (!nativeInitialized)
    return caseError("valid empty-interface Plugin did not initialize native EAL");
  auto finalized = env->final();
  if (finalized.is<ZeException>()) return ZuMv(finalized).p<ZeException>();
  env = nullptr;
  if (cleanupCalls != 1)
    return caseError("valid Plugin ownership did not clean up EAL exactly once");
  return {};
}

static ZeException runCase(ZuCSpan scenario)
{
  if (scenario == "cleanup") return cleanupCase();
  if (scenario == "config-empty" || scenario == "config-long" || scenario == "config-nul")
    return configCase(scenario);
  EAL ownedEAL;
  auto eal = &ownedEAL;
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
    params.args = {"-l", ZuMv(mapping), "--vdev", device, "--file-prefix", "zpkt test"};
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
    if (params.args[4] != "--file-prefix" || params.args[5] != "zpkt test" ||
	!strstr(rte_eal_get_runtime_dir(), "/zpkt test"))
      return caseError("raw arguments did not remain separate, unmodified tokens");
  }
  return {};
}

static bool cleanRuntime(const char *dir)
{
  ZtString<> runtime{ZuCSpan{dir}};
  runtime << "/dpdk";
  for (auto prefix : {"rte", "zpkt test"}) {
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

  char cli[] = "app --main-lcore=0 --no-pci --iova-mode=va --file-prefix='zpkt test'";
  ZfCLI::InCLI in{cli};
  EALParams fromCLI;
  ZuCheckRT(ZfCLI::load(fromCLI, in.argv) == 1);
  ZuCheckRT(*fromCLI.mainLcore && fromCLI.mainLcore.val() == 0);
  ZuCheckRT(fromCLI.noPCI && fromCLI.iova == IOVA::VA && fromCLI.filePrefix == "zpkt test");
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
    "automatic", "raw", "duplicate", "alias", "core-alias", "optional", "native-error", "cleanup",
    "config-empty", "config-long", "config-nul"
  };
  for (auto scenario : cases) {
    char dir[] = "/tmp/ZpktDpdkInstanceTest.XXXXXX";
    if (!mkdtemp(dir)) {
      ZuCheckRT(false, log_("create isolated DPDK runtime directory"));
      continue;
    }
    auto pid = fork();
    if (!pid) {
      if (setenv("RUNTIME_DIRECTORY", dir, 1) < 0) _exit(126);
      execl("/proc/self/exe", "ZpktDpdkInstanceTest", "--case", scenario, nullptr);
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
}
