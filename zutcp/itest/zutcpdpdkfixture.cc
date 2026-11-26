//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <iostream>
#include <dlfcn.h>
#include <errno.h>
#include <net/if.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#include <rte_ethdev.h>
#include <rte_mbuf.h>

#include "zutcpdpdkfixture.hh"

namespace ZutcpDpdkTest {

static ZeException error(const char *op, int code)
{
  return ZeEXCEPT(Error, "Zutcp.DpdkTest", ([op, code](auto &s) {
    s << op << ": " << strerror(code);
  }));
}

static int waitChild(pid_t pid)
{
  if (pid < 0) return -1;
  int status;
  pid_t result;
  do result = waitpid(pid, &status, 0); while (result < 0 && errno == EINTR);
  return result == pid ? status : -1;
}

template <typename ...Args> static Result ip(Args... args)
{
  char *argv[] = {const_cast<char *>("ip"), const_cast<char *>(args)..., nullptr};
  auto pid = fork();
  if (!pid) {
    execvp(argv[0], argv);
    _exit(127);
  }
  auto status = waitChild(pid);
  if (status) {
    ZtString<> command;
    for (auto arg : argv) if (arg) command << ' ' << arg;
    return ZeEXCEPT(Error, "Zutcp.DpdkTest", ([command = ZuMv(command), status](auto &s) {
      s << "iproute2 setup failed, status " << status << ':' << command;
    }));
  }
  return {};
}

Result Fixture::network()
{
  if (unshare(CLONE_NEWNET) < 0) return error("create isolated fixture network", errno);
  const char *routingMAC[] = {"02:00:00:91:ff:01", "02:00:00:91:ff:02"};
  for (unsigned i = 0; i < 2; ++i) {
    const auto &link = Links[i];
    const auto &peer = Links[i ^ 1];
    auto result = ip("link", "add", link.name, "type", "dummy");
    if (result.is<ZeException>()) return result;
    result = ip("link", "set", "dev", link.name, "address", routingMAC[i], "mtu", "1600", "up");
    if (result.is<ZeException>()) return result;
    result = ip("route", "add", peer.route, "dev", link.name);
    if (result.is<ZeException>()) return result;
    result = ip("neighbor", "add", peer.ip, "lladdr", peer.mac, "nud", "permanent", "dev", link.name);
    if (result.is<ZeException>()) return result;
    result = ip("-6", "route", "add", peer.route6, "dev", link.name);
    if (result.is<ZeException>()) return result;
    result = ip("-6", "neighbor", "add", peer.ip6, "lladdr", peer.mac,
      "nud", "permanent", "dev", link.name);
    if (result.is<ZeException>()) return result;
    ifindex[i] = if_nametoindex(link.name);
    if (!ifindex[i]) return error("query dummy fixture interface", errno);
  }
  return {};
}

Result Fixture::devices()
{
  auto result = runtime.init(false);
  if (result.is<ZeException>()) return result;
  for (unsigned i = 0; i < 2; ++i) {
    result = ports[i].init(Links[i].ring, 256, Links[i].address);
    if (result.is<ZeException>()) return result;
  }
  return {};
}

Result Fixture::startWire(Zi::IOBufPool *a, Zi::IOBufPool *b)
{
  return wire.start({&ports[0], ZpktDpdkTest::native(a)},
    {&ports[1], ZpktDpdkTest::native(b)});
}

void Fixture::drain()
{
  wire.stop();
  for (auto &port : ports) port.drain();
}

Zutcp::MxPlugin::Params tcpParams(unsigned routingIfindex)
{
  Zutcp::MxPlugin::Params params{};
  params.routingIfindex = routingIfindex;
  params.packetCount = 128;
  params.controlBufCount = 32;
  params.txControlCredits = 16;
  params.rxRetainFramesPerCxn = 8;
  params.connectTimeout = ZuTime{3};
  params.closeDrainTimeout = ZuTime{3};
  params.finWaitTimeout = ZuTime{3};
  return params;
}

static ZmAtomic<unsigned> poolFailure{0}, startFailure{0}, startInjected{0};
static char provisional[RTE_MEMPOOL_NAMESIZE];

void failAllocation(unsigned call)
{
  *provisional = 0;
  poolFailure = call;
}

bool allocationFailed() { return !poolFailure && *provisional; }
bool allocationRolledBack() { return allocationFailed() && !rte_mempool_lookup(provisional); }
void failActivation()
{
  startInjected = 0;
  startFailure = 1;
}
bool activationFailed() { return startInjected; }

bool portStopped(const char *name)
{
  uint16_t port;
  rte_eth_link link{};
  return !rte_eth_dev_get_port_by_name(name, &port) && !rte_eth_link_get_nowait(port, &link) &&
    link.link_status == RTE_ETH_LINK_DOWN;
}

extern "C" __attribute__((visibility("default")))
int rte_mempool_set_ops_byname(rte_mempool *pool, const char *name, void *config)
{
  static const auto setOps = reinterpret_cast<decltype(&rte_mempool_set_ops_byname)>(
    dlsym(RTLD_NEXT, "rte_mempool_set_ops_byname"));
  if (!setOps) return -ENOSYS;
  if (!strcmp(name, "zpkt_tx_returns")) {
    unsigned remaining = poolFailure;
    if (remaining) {
      --poolFailure;
      if (remaining == 1) return -ENOMEM;
      snprintf(provisional, sizeof(provisional), "%s", pool->name);
    }
  }
  return setOps(pool, name, config);
}

extern "C" __attribute__((visibility("default")))
int rte_eth_dev_start(uint16_t port)
{
  static const auto start = reinterpret_cast<decltype(&rte_eth_dev_start)>(
    dlsym(RTLD_NEXT, "rte_eth_dev_start"));
  if (!start) return -ENOSYS;
  auto inject = startFailure.xch(0);
  int rc = start(port);
  if (inject && !rc) {
    // Model an ethdev error after the PMD has brought both queues up.
    startInjected = 1;
    return -EIO;
  }
  return rc;
}

int fatalHandoff(ZuCSpan scenario)
{
  rlimit noCore{0, 0};
  if (setrlimit(RLIMIT_CORE, &noCore)) return 2;
  ZpktDpdkTest::Runtime runtime;
  if (runtime.init().is<ZeException>()) return 3;
  auto env = runtime.env();
  auto ifindex = if_nametoindex("zdpfatal");
  ZpktDpdkTest::RingPort port;
  if (!ifindex || port.init("fatal", 64, Links[0].address).is<ZeException>()) return 4;
  ZiMultiplex mx;
  auto opened = env->nif(&mx, "zdpfatal");
  if (opened.is<ZeException>()) return 5;
  ZmRef<Zpkt::Nif> device = opened.p<Zpkt::Nif *>();
  ZmRef<Zutcp::MxPlugin> tcp = new Zutcp::MxPlugin;
  if (scenario == "device-mx") {
    ZiMultiplex wrong;
    tcp->init(&wrong, env, ZuMv(device), tcpParams(ifindex));
  } else {
    ZpktDpdkTest::RingPort another;
    if (scenario == "repeat") {
      auto secondIfindex = if_nametoindex("zdpfirst");
      if (!secondIfindex || another.init("first", 64, Links[1].address).is<ZeException>())
	return 6;
      auto firstResult = env->nif(&mx, "zdpfirst");
      if (firstResult.is<ZeException>()) return 6;
      ZmRef<Zpkt::Nif> first = firstResult.p<Zpkt::Nif *>();
      try { tcp->init(&mx, env, ZuMv(first), tcpParams(secondIfindex)); }
      catch (ZeException &e) { std::cerr << e << '\n' << std::flush; return 6; }
    }
    auto rx = device->bufPool(Zi::IOBufType::Rx, 8, 1514);
    auto tx = device->bufPool(Zi::IOBufType::TxTCP, 8, 128);
    Zi::IOBufPoolList pools;
    pools.pushNode(tx.ptr());
    if (device->alloc(rx, pools).is<ZeException>() || !device->allocated() ||
	device->state() != ZmEngineState::Stopped || device->mx() != &mx ||
	device->ifindex()) return 6;
    tcp->init(&mx, env, ZuMv(device), tcpParams(ifindex));
  }
  return 7;
}

bool checksFatalHandoff(const char *scenario, const char *assertion)
{
  char dir[] = "/tmp/zutcpdpdk-fatal.XXXXXX";
  if (!mkdtemp(dir)) return false;
  int diagnostic[2];
  if (pipe(diagnostic) < 0) { rmdir(dir); return false; }
  auto pid = fork();
  if (!pid) {
    close(diagnostic[0]);
    if (dup2(diagnostic[1], STDERR_FILENO) < 0 ||
	setenv("RUNTIME_DIRECTORY", dir, 1) < 0) _exit(126);
    close(diagnostic[1]);
    execl("/proc/self/exe", "zutcpdpdktest", "--fatal-handoff", scenario, nullptr);
    _exit(127);
  }
  close(diagnostic[1]);
  char bytes[4096];
  ZtString<> output;
  for (;;) {
    auto n = read(diagnostic[0], bytes, sizeof(bytes));
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) break;
    output << ZuCSpan{bytes, unsigned(n)};
  }
  close(diagnostic[0]);
  auto status = waitChild(pid);
  bool fatal = status >= 0 && WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT &&
    strstr(output.ndata(), assertion);
  if (!fatal)
    fprintf(stderr, "fatal handoff [%s] status %d\n%s", scenario, status, output.ndata());
  ZtString<> path{dir};
  path << "/dpdk/rte";
  rmdir(path.ndata());
  path = dir;
  path << "/dpdk";
  rmdir(path.ndata());
  return !rmdir(dir) && fatal;
}

} // namespace ZutcpDpdkTest
