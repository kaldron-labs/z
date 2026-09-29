//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// OAuth/WSS agent configuration

#ifndef ztcagent_config_HH
#define ztcagent_config_HH

#ifndef ZtcLib_HH
#include <zlib/ZtcLib.hh>
#endif

#include <limits.h>
#include <stdint.h>

#include <zlib/ZuID.hh>

#include <zlib/ZmFn.hh>

#include <zlib/ZtString.hh>

#include <zlib/ZfStruct.hh>
#include <zlib/ZfCf.hh>

#include <zlib/ZiFile.hh>

#include <zlib/ZtcLib.hh>

namespace Ztc {

struct AgentCf {
  // Protocol frames are below INT_MAX; queue counts/bytes are independently
  // bounded resident-memory and per-turn workload controls.
  enum {
    MaxFrame = 1U<<30,
    DefltMaxFrame = 1U<<20,
    DefltTelFrames = 1024,
    DefltTelBytes = 1U<<24,
    DefltReqBytes = 1U<<22,
    DefltFanoutBatch = 64,
    DefltReconnMin = 1,
    DefltReconnMax = 60,
    DefltTelSize = 1U<<21,
    DefltTelTimeout = 1,
    DefltReqTimeout = 1,
    DefltPubGCInterval = 1,
    DefltPubGCBatch = 100
  };
  static constexpr double DefltReconnBackoff = 2;

  unsigned	maxFrame = DefltMaxFrame;
  unsigned	telFrames = DefltTelFrames;
  uint64_t	telBytes = DefltTelBytes;
	uint64_t	reqBytes = DefltReqBytes;
	unsigned	fanoutBatch = DefltFanoutBatch;
  unsigned	reconnMin = DefltReconnMin;
  unsigned	reconnMax = DefltReconnMax;
  double	reconnBackoff = DefltReconnBackoff;
  unsigned	telSize = DefltTelSize;
  bool		telLL = false;
  unsigned	telSpin = 0;
  unsigned	telTimeout = DefltTelTimeout;
  unsigned	reqTimeout = DefltReqTimeout;
  unsigned	pubGCInterval = DefltPubGCInterval;
  unsigned	pubGCBatch = DefltPubGCBatch;
	unsigned	upgradeTimeout = 10;
	unsigned	closeTimeout = 5;
	unsigned	pingInterval = 30;
	unsigned	idleTimeout = 60;
  bool		loopbackTest = false;
  ZtString<ZtStringHeapID<"Ztc.Agent.Vault">> vaultStore;
  ZtString<ZtStringHeapID<"Ztc.Agent.Vault">> vaultModule;
  bool		vaultTestStore = false;
};

ZfStruct(ZtcAPI, (AgentCf, Cf),
  (((maxFrame),		((Range<64U, unsigned(AgentCf::MaxFrame)>))),
						(UInt32, 1U<<20U)),
  (((telFrames),	((Range<1U, 1U<<30U>))), (UInt32, 1024)),
  (((telBytes),		((Range<64ULL, 1ULL<<40U>))),	(UInt64, 1U<<24U)),
  (((reqBytes),		((Range<64ULL, 1ULL<<40U>))),	(UInt64, 1U<<22U)),
  (((fanoutBatch),	((Range<1U, 65536U>))),	(UInt32, 64)),
  (((reconnMin),	((Range<1U, 3600U>))),		(UInt32, 1)),
  (((reconnMax),	((Range<1U, 86400U>))),	(UInt32, 60)),
  (((reconnBackoff),	((Range<1.0, 16.0>))),		(Float, 2)),
  (((telSize),		((Range<64U, 1U<<30U>))),	(UInt32, 1U<<21U)),
  (((telLL)),					(Bool)),
  (((telSpin),		((Range<0U, 1U<<30U>))), (UInt32)),
  (((telTimeout),	((Range<1U, 3600U>))),		(UInt32, 1)),
  (((reqTimeout),	((Range<1U, 3600U>))),		(UInt32, 1)),
  (((pubGCInterval),	((Range<1U, 3600U>))),		(UInt32, 1)),
  (((pubGCBatch),	((Range<1U, 65536U>))),		(UInt32, 100)),
  (((upgradeTimeout),	((Range<1U, 3600U>))),		(UInt32, 10)),
  (((closeTimeout),	((Range<1U, 3600U>))),		(UInt32, 10)),
  (((pingInterval),	((Range<0U, 3600U>))),		(UInt32)),
  (((idleTimeout),	((Range<0U, 3600U>))),		(UInt32)),
  (((loopbackTest)),					(Bool)),
  (((vaultStore), (Required)), (String)),
  (((vaultModule)), (String)),
  (((vaultTestStore)), (Bool)));

} // Ztc

#endif /* ztcagent_config_HH */
