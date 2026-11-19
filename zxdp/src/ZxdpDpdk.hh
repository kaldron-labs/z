//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Explicit process-wide DPDK initialization.

#ifndef ZxdpDpdk_HH
#define ZxdpDpdk_HH

#ifndef ZxdpLib_HH
#include <zlib/ZxdpLib.hh>
#endif

#include <stdint.h>

#include <zlib/ZuBox.hh>
#include <zlib/ZuFmt.hh>
#include <zlib/ZuUnion.hh>

#include <zlib/ZmAtomic.hh>

#include <zlib/ZtArray.hh>
#include <zlib/ZtEnum.hh>
#include <zlib/ZtString.hh>

#include <zlib/ZePlatform.hh>

#include <zlib/ZfCLI.hh>

namespace Zxdp::Dpdk {

ZtEnumNS(ZxdpAPI, Proc, int8_t, Primary, Secondary, Auto);
ZtEnumNS(ZxdpAPI, IOVA, int8_t, PA, VA);
ZtEnumNS(ZxdpAPI, HugeUnlink, int8_t, Existing, Always, Never);
ZtEnumNS(ZxdpAPI, VFIOIntr, int8_t, Legacy, MSI, MSIX);
ZtEnumNS(ZxdpAPI, TraceMode, int8_t, Overwrite, Discard);

namespace Proc {
  inline constexpr T Default = -1;
  ZtEnumMap(ZxdpAPI, Proc, CLIMap, "primary", "secondary", "auto", "invalid");
}
namespace IOVA {
  inline constexpr T Default = -1;
  ZtEnumMap(ZxdpAPI, IOVA, CLIMap, "pa", "va", "invalid");
}
namespace HugeUnlink {
  inline constexpr T Default = -1;
  ZtEnumMap(ZxdpAPI, HugeUnlink, CLIMap, "existing", "always", "never", "invalid");
}
namespace VFIOIntr {
  inline constexpr T Default = -1;
  ZtEnumMap(ZxdpAPI, VFIOIntr, CLIMap, "legacy", "msi", "msix", "invalid");
}
namespace TraceMode {
  inline constexpr T Default = -1;
  ZtEnumMap(ZxdpAPI, TraceMode, CLIMap, "overwrite", "discard", "invalid");
}

struct EALParams {
  using String = ZtString<ZtStringHeapID<"Zxdp.Dpdk.EALParams.String">>;
  using Strings = ZtArray<String, ZtArrayHeapID<"Zxdp.Dpdk.EALParams">>;

  // EAL execution context; default policy enables only the initializing lcore.
  String		lcores;			// EAL lcore mapping expression
  ZuBox<unsigned>	mainLcore;

  // Discovery and device construction; repeats retain their order.
  bool			noPCI = false;
  Strings		allow;			// PCI selectors with optional driver args
  Strings		block;
  Strings		vdev;			// complete PMD argument strings
  Strings		driverPath;

  // Memory; null numbers and empty strings leave the EAL option unspecified.
  bool			noHuge = false;
  bool			inMemory = false;
  ZuBox<unsigned>	memoryMB;
  ZuBox<unsigned>	memoryChannels;
  ZuBox<unsigned>	memoryRanks;
  String		numaMem;		// EAL's per-node list, in MB
  String		numaLimit;		// EAL's per-node list, in MB
  String		hugeDir;
  IOVA::T		iova = IOVA::Default;
  HugeUnlink::T		hugeUnlink = HugeUnlink::Default;
  bool			legacyMem = false;
  bool			singleFileSegments = false;
  bool			matchAllocations = false;
  ZuBox<uintptr_t>	baseVirtAddr;
  String		mbufPoolOps;

  // Process isolation.
  Proc::T		procType = Proc::Default;
  String		filePrefix;
  bool			noShconf = false;

  // Diagnostics and device-specific runtime controls.
  Strings		logLevel;		// e.g. "lib.eal:debug"
  Strings		trace;			// component regular expressions
  String		traceDir;
  String		traceBufSize;		// EAL size syntax, e.g. "2M"
  TraceMode::T		traceMode = TraceMode::Default;
  bool			noTelemetry = false;
  ZuBox<unsigned>	maxSIMDBits;
  VFIOIntr::T		vfioIntr = VFIOIntr::Default;
  String		vfioVFToken;

  // Additional EAL options, one argv token per element; no shell parsing.
  Strings		args;
};

using EALAddrFmt = ZuFmt::Hex<false, ZuFmt::Alt<>>;

ZfStruct(ZxdpAPI, (EALParams, CLI),
  (lcores,		(CLI::Long<"lcores">),					String),
  (mainLcore,		(CLI::Long<"main-lcore">),				UInt32),
  (noPCI,		(CLI::Long<"no-pci">, CLI::Flag<>),			Bool),
  (allow,		(CLI::Opt<'a'>),					StringVec),
  (block,		(CLI::Opt<'b'>),					StringVec),
  (vdev,		(CLI::Long<"vdev">),					StringVec),
  (driverPath,		(CLI::Long<"driver-path">, CLI::Opt<'d'>),		StringVec),
  (noHuge,		(CLI::Long<"no-huge">, CLI::Flag<>),			Bool),
  (inMemory,		(CLI::Long<"in-memory">, CLI::Flag<>),			Bool),
  (memoryMB,		(CLI::Opt<'m'>, CLI::Long<"memory-size">),		UInt32),
  (memoryChannels,	(CLI::Opt<'n'>, CLI::Long<"memory-channels">),		UInt32),
  (memoryRanks,		(CLI::Opt<'r'>, CLI::Long<"memory-ranks">),		UInt32),
  (numaMem,		(CLI::Long<"numa-mem">),				String),
  (numaLimit,		(CLI::Long<"numa-limit">),				String),
  (hugeDir,		(CLI::Long<"huge-dir">),				String),
  (iova,		(CLI::Long<"iova-mode">, Enum<IOVA::CLIMap>),		Int8),
  (hugeUnlink,		(CLI::Long<"huge-unlink">, Enum<HugeUnlink::CLIMap>),	Int8),
  (legacyMem,		(CLI::Long<"legacy-mem">, CLI::Flag<>),			Bool),
  (singleFileSegments,	(CLI::Long<"single-file-segments">, CLI::Flag<>),	Bool),
  (matchAllocations,	(CLI::Long<"match-allocations">, CLI::Flag<>),		Bool),
  (baseVirtAddr,	(CLI::Long<"base-virtaddr">, CLI::Number<EALAddrFmt>),	UInt64),
  (mbufPoolOps,		(CLI::Long<"mbuf-pool-ops-name">),			String),
  (procType,		(CLI::Long<"proc-type">, Enum<Proc::CLIMap>),		Int8),
  (filePrefix,		(CLI::Long<"file-prefix">),				String),
  (noShconf,		(CLI::Long<"no-shconf">, CLI::Flag<>),			Bool),
  (logLevel,		(CLI::Long<"log-level">),				StringVec),
  (trace,		(CLI::Long<"trace">),					StringVec),
  (traceDir,		(CLI::Long<"trace-dir">),				String),
  (traceBufSize,	(CLI::Long<"trace-bufsz">),				String),
  (traceMode,		(CLI::Long<"trace-mode">, Enum<TraceMode::CLIMap>),	Int8),
  (noTelemetry,		(CLI::Long<"no-telemetry">, CLI::Flag<>),		Bool),
  (maxSIMDBits,		(CLI::Long<"force-max-simd-bitwidth">),			UInt32),
  (vfioIntr,		(CLI::Long<"vfio-intr">, Enum<VFIOIntr::CLIMap>),	Int8),
  (vfioVFToken,		(CLI::Long<"vfio-vf-token">),				String));

class ZxdpAPI EAL {
friend class Instance;

public:
  ~EAL();
  EAL(const EAL &) = delete;
  EAL &operator =(const EAL &) = delete;

  static EAL *instance();

  // Device instances and their retained backing must retire before EAL cleanup.
  ZuUnion<void, ZeException> init(const EALParams &);
  bool ready() const { return m_state == Ready; }

private:
  EAL() = default;
  bool claim(unsigned port);
  void release(unsigned port);

  enum { Cold, Attempted, Initialized, Ready };
  struct Ports;

  ZmAtomic<unsigned>	m_state = Cold;
  Ports			*m_ports = nullptr;
};

} // namespace Zxdp::Dpdk
#endif /* ZxdpDpdk_HH */
