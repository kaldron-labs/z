//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

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

#include <rte_errno.h>
#include <rte_ethdev.h>
#include <rte_mbuf.h>

#include <zlib/ZxdpDpdk.hh>

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
  for (unsigned i = 0; i < 2; ++i) {
    const auto &link = Links[i];
    const auto &peer = Links[i ^ 1];
    auto result = ip("link", "add", link.name, "type", "dummy");
    if (result.is<ZeException>()) return result;
    result = ip("link", "set", "dev", link.name, "address", link.mac, "mtu", "1500", "up");
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
  auto result = runtime.init();
  if (result.is<ZeException>()) return result;
  for (unsigned i = 0; i < 2; ++i) {
    result = ports[i].init(Links[i].ring, 256, Links[i].address);
    if (result.is<ZeException>()) return result;
  }
  return {};
}

Result Fixture::startWire(Zi::IOBufPool *a, Zi::IOBufPool *b)
{
  return wire.start({&ports[0], ZxdpDpdkTest::native(a)},
    {&ports[1], ZxdpDpdkTest::native(b)});
}

void Fixture::drain()
{
  wire.stop();
  for (auto &port : ports) port.drain();
}

Zxdp::Params deviceParams(const char *port, unsigned ifindex)
{
  Zxdp::Params result{Zxdp::Params::New<Zxdp::Params::Index<Zxdp::Dpdk::Params>{}>{}};
  auto &params = result.p<Zxdp::Dpdk::Params>();
  params.name = port;
  params.ifindex = ifindex;
  params.spin = 4;
  params.rxInterval = 100;
  return result;
}

Zutcp::Instance::Params tcpParams()
{
  Zutcp::Instance::Params params{};
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
rte_mempool *rte_pktmbuf_pool_create(
  const char *name, unsigned count, unsigned cache, uint16_t priv, uint16_t room, int socket)
{
  static const auto create = reinterpret_cast<decltype(&rte_pktmbuf_pool_create)>(
    dlsym(RTLD_NEXT, "rte_pktmbuf_pool_create"));
  if (!create) {
    rte_errno = ENOSYS;
    return nullptr;
  }
  if (!strncmp(name, "zxdp_", 5)) {
    unsigned remaining = poolFailure;
    if (remaining) {
      --poolFailure;
      if (remaining == 1) {
	rte_errno = ENOMEM;
	return nullptr;
      }
      snprintf(provisional, sizeof(provisional), "%s", name);
    }
  }
  return create(name, count, cache, priv, room, socket);
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
  using namespace Zxdp::Dpdk;
  EALParams ealParams{};
  ealParams.noPCI = ealParams.noHuge = ealParams.noShconf = ealParams.noTelemetry = true;
  auto eal = EAL::instance();
  if (eal->init(ealParams).is<ZeException>()) return 3;
  auto ifindex = if_nametoindex(Links[0].name);
  ZxdpDpdkTest::RingPort port;
  if (!ifindex || port.init("fatal", 64, Links[0].address).is<ZeException>()) return 4;
  ZiMultiplex mx;
  auto device = Zxdp::instance(Zxdp::Type::Dpdk);
  if (device->init(&mx, deviceParams(port.name(), ifindex)).is<ZeException>()) return 5;
  ZmRef<Zutcp::Instance> tcp = new Zutcp::Instance;
  if (scenario == "device-mx") {
    ZiMultiplex wrong;
    tcp->init(&wrong, ZuMv(device), tcpParams());
  } else if (scenario == "plugin-mx") {
    if (tcp->init(&mx, ZuMv(device), tcpParams()).is<ZeException>()) return 6;
    ZiMultiplex wrong;
    wrong.plugin(ZuMv(tcp));
  } else {
    ZxdpDpdkTest::RingPort another;
    if (scenario == "repeat") {
      auto secondIfindex = if_nametoindex(Links[1].name);
      if (!secondIfindex || another.init("first", 64, Links[1].address).is<ZeException>())
	return 6;
      auto first = Zxdp::instance(Zxdp::Type::Dpdk);
      if (first->init(&mx, deviceParams(another.name(), secondIfindex)).is<ZeException>() ||
	  tcp->init(&mx, ZuMv(first), tcpParams()).is<ZeException>()) return 6;
    }
    auto rx = device->bufPool(Zi::IOBufType::Rx, 8, 1514);
    auto tx = device->bufPool(Zi::IOBufType::TxTCP, 8, 128);
    Zi::IOBufPoolList pools;
    pools.pushNode(tx.ptr());
    if (device->alloc(rx, pools).is<ZeException>() || !device->allocated() ||
	device->everStarted() || device->mx() != &mx || device->ifindex() != ifindex) return 6;
    tcp->init(&mx, ZuMv(device), tcpParams());
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
