//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdlib.h>
#include <stdio.h>
#include <sched.h>
#include <string.h>

#include <rte_eal.h>
#include <rte_errno.h>
#include <rte_ethdev.h>
#include <rte_lcore.h>

#include <zlib/ZuSpan.hh>

#include <zlib/ZmGuard.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmPLock.hh>

#include "ZpktDpdk.hh"

namespace Zpkt::Dpdk {

ZtEnumImplNS(Proc);
ZtEnumImplNS(IOVA);
ZtEnumImplNS(HugeUnlink);
ZtEnumImplNS(VFIOIntr);
ZtEnumImplNS(TraceMode);

ZtEnumImplNS(Proc, CLIMap);
ZtEnumImplNS(IOVA, CLIMap);
ZtEnumImplNS(HugeUnlink, CLIMap);
ZtEnumImplNS(VFIOIntr, CLIMap);
ZtEnumImplNS(TraceMode, CLIMap);

struct EAL::Ports : ZmNHeap<Ports> {
  friend inline ZuStringT<"Zpkt.Dpdk.Ports"> ZmNHeapID(Ports *);
  ZmPLock lock;
  bool claimed[RTE_MAX_ETHPORTS]{};
};

// Native EAL permits only one initialization attempt per process, even after
// an explicitly owned plugin has been destroyed.
static ZmAtomic<unsigned> attempted{0};

EAL::~EAL()
{
  if (m_state < Initialized) return;
  for (unsigned port = 0; port < RTE_MAX_ETHPORTS; ++port) {
    if (m_ports && m_ports->claimed[port]) {
      fprintf(stderr, "Zpkt: DPDK port %u still claimed during EAL cleanup\n", port);
      abort();
    }
  }
  uint16_t port;
  RTE_ETH_FOREACH_DEV(port) {
    if (int result = rte_eth_dev_close(port)) {
      fprintf(stderr, "Zpkt: DPDK port %u close failed: %s\n",
	unsigned(port), rte_strerror(-result));
      abort();
    }
  }
  if (rte_eal_cleanup() < 0)
    fprintf(stderr, "Zpkt: DPDK EAL cleanup failed: %s\n", rte_strerror(rte_errno));
  delete m_ports;
}

bool EAL::claim(unsigned port)
{
  if (!ready() || port >= RTE_MAX_ETHPORTS) return false;
  ZmGuard guard(m_ports->lock);
  if (m_ports->claimed[port]) return false;
  m_ports->claimed[port] = true;
  return true;
}

void EAL::release(unsigned port)
{
  ZmGuard guard(m_ports->lock);
  m_ports->claimed[port] = false;
}

ZfStructImpl(EALParams);
ZfStructImpl(EALParams, CLI);

using CLI = ZfCLI::Parser<EALParams>;

template <typename ...Entry> struct EALEntries {
  static constexpr unsigned N = sizeof...(Entry);
  static constexpr const char *names[] = {Entry::Key::V.data()...};
  static constexpr bool takesValue[] = {(Entry::Type == ZfCLI::OptType::Option)...};
};

using Options = ZuTypeApply<EALEntries, CLI::LongEntries_>;

static ZuCSpan canonicalName(ZuCSpan name)
{
  static constexpr struct { const char *alias, *name; } aliases[] = {
    {"-l", "lcores"}, {"-c", "lcores"}, {"--coremask", "lcores"},
    {"--socket-mem", "numa-mem"}, {"--socket-limit", "numa-limit"},
    {"--telemetry", "no-telemetry"}, {"-R", "remap-lcore-ids"},
    {"-s", "service-coremask"}, {"-S", "service-corelist"}
  };
  for (const auto &alias : aliases)
    if (name == alias.alias) return alias.name;
  if (name.length() > 2 && name[1] == '-') return name.offset(2);
  if (name.length() == 2)
    return ZfCLI::Options<EALParams, ZuFacet::CLI>::shortOpt(name[1]).expansion;
  return {};
}

struct RawOpts {
  const char *arg[Options::N]{};
  bool remap = false;

  const char *operator [](ZuCSpan name) const {
    int id = CLI::longOptionID(name);
    return id >= 0 ? arg[id] : nullptr;
  }
};

template <typename Argv>
static RawOpts rawOptions(const Argv &args, unsigned first = 0)
{
  RawOpts options;
  auto n = args.length();
  for (unsigned i = first; i < n; ++i) {
    const auto &token = args[i];
    if (token == "--") break;
    auto length = token.length();
    if (!length || token[0] != '-') continue;
    const char *begin = token.data();
    const char *split = strchr(begin, '=');
    if (!split && length > 2 && token[1] != '-') split = begin + 2;
    auto name = canonicalName(split ? ZuCSpan{begin, unsigned(split - begin)} : ZuCSpan{token});
    if (name == "remap-lcore-ids") options.remap = true;
    int id = CLI::longOptionID(name);
    // DPDK accepts a bare --huge-unlink as well as --huge-unlink=MODE.
    bool takesValue = id >= 0 ? Options::takesValue[id] && name != "huge-unlink" :
      name == "service-coremask" || name == "service-corelist";
    const char *value = "";
    if (split)
      value = *split == '=' ? split + 1 : split;
    else if (takesValue && i + 1 < n)
      value = args[++i].ndata();
    if (id >= 0 && !options.arg[id]) options.arg[id] = value;
  }
  return options;
}

void EAL::init(const EALParams &params)
{
  if (attempted.xch(1))
    throw ZeEXCEPT(Error, Log, "DPDK EAL initialization has already been attempted");
  m_state = Attempted;

  ZfCLI::OutArgv out;
  out.argv.push("zpkt");
  ZfCLI::saveArgv(out, params);
  auto raw = rawOptions(params.args);
  auto named = rawOptions(out.argv, 1);
  for (unsigned i = 0; i < Options::N; ++i) {
    if (raw.arg[i] && named.arg[i])
      throw ZeEXCEPT(Error, Log, ([option = Options::names[i]](auto &s) {
	s << "DPDK EAL option --" << option << " supplied both as a named parameter and in args";
      }));
  }

  const bool autoLcore = !params.lcores && !raw["lcores"];
  cpu_set_t affinity;
  if (autoLcore) {
    if (sched_getaffinity(0, sizeof(affinity), &affinity) < 0)
      throw ZeEXCEPT(Error, Log, ([error = ZeLastError](auto &s) {
	s << "could not read the DPDK EAL initializing thread's affinity: " << error;
      }));
    int cpu = sched_getcpu();
    if (cpu < 0 || cpu >= CPU_SETSIZE || !CPU_ISSET(cpu, &affinity)) {
      for (cpu = 0; cpu < CPU_SETSIZE && !CPU_ISSET(cpu, &affinity); ++cpu) { }
      if (cpu == CPU_SETSIZE)
	throw ZeEXCEPT(Error, Log, "no allowed CPU for the DPDK EAL main lcore");
    }
    ZeString mapping;
    if (!raw.remap) {
      unsigned lcore = 0;
      if (*params.mainLcore) {
	lcore = params.mainLcore.val();
      } else if (auto value = raw["main-lcore"]) {
	// The native parser validates the argument, including its base.
	lcore = unsigned(strtol(value, nullptr, 0));
      }
      mapping << lcore << '@';
    }
    mapping << cpu;
    out.argv.push(ZfCLI::Arg{} << "--lcores=" << mapping);
  }

  // DPDK receives writable strings and a writable, null-terminated argv.
  for (const auto &token : params.args) out.argv.push(ZuCSpan{token});
  ZtArray<char *, ZtArrayHeapID<"Zpkt.Dpdk.Argv">> argv;
  char empty[] = "";
  for (auto &token : out.argv) argv.push(token.length() ? token.data() : empty);
  argv.push(nullptr);
  int result = rte_eal_init(out.argc(), argv.data());
  int ealError = rte_errno;
  if (result >= 0) m_state = Initialized;
  // An explicit mapping includes the requested main-thread placement.
  int affinityError = 0;
  if (autoLcore && sched_setaffinity(0, sizeof(affinity), &affinity) < 0)
    affinityError = errno;
  if (result < 0)
    throw ZeEXCEPT(Error, Log, ([ealError](auto &s) {
      s << "rte_eal_init failed: " << rte_strerror(ealError);
    }));
  if (affinityError)
    throw ZeEXCEPT(Error, Log, ([affinityError](auto &s) {
      s << "could not restore the DPDK EAL initializing thread's affinity: "
	<< ZeError{affinityError};
    }));

  m_ports = new Ports{};
  m_state = Ready;
}

} // namespace Zpkt::Dpdk
