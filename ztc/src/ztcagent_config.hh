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

ZuDerive(String, (ZtString<ZtStringHeapID<"Ztc.Agent.Vault">>));

struct AgentCf {
  // Protocol frames are at most INT_MAX; queue counts/bytes are independently
  // bounded resident-memory and per-turn workload controls.
  enum {
    MaxFrame = INT_MAX,
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
  String	vaultStore;
  String	vaultModule;
  bool		vaultTestStore = false;
};

ZfStruct(ZtcAPI, (AgentCf, Cf),
  (maxFrame,		(Mutable, (Range<64U, AgentCf::MaxFrame>)),	UInt32),
  (telFrames,	(Mutable, (Range<1U, INT_MAX>)),			UInt32),
  (telBytes,		(Mutable, (Range<64ULL, UINT64_MAX>)),		UInt64),
  (reqBytes,		(Mutable, (Range<64ULL, UINT64_MAX>)),		UInt64),
  (fanoutBatch,	(Mutable, (Range<1U, 65536U>)),				UInt32),
  (reconnMin,	(Mutable, (Range<1U, 3600U>)),				UInt32),
  (reconnMax,	(Mutable, (Range<1U, 86400U>)),				UInt32),
  (reconnBackoff,	(Mutable, (Range<1.0, 16.0>)),			Float),
  (telSize,		(Mutable, (Range<64U, INT_MAX>)),		UInt32),
  (telLL, (Mutable),							Bool),
  (telSpin,		(Mutable, (Range<0U, INT_MAX>)),		UInt32),
  (telTimeout,	(Mutable, (Range<1U, 3600U>)),				UInt32),
  (reqTimeout,	(Mutable, (Range<1U, 3600U>)),				UInt32),
  (pubGCInterval,	(Mutable, (Range<1U, 3600U>)),			UInt32),
  (pubGCBatch,	(Mutable, (Range<1U, 65536U>)),				UInt32),
  (upgradeTimeout,	(Mutable, (Range<1U, 3600U>)),			UInt32),
  (closeTimeout,	(Mutable, (Range<1U, 3600U>)),			UInt32),
  (pingInterval,	(Mutable, (Range<0U, 3600U>)),			UInt32),
  (idleTimeout,	(Mutable, (Range<0U, 3600U>)),				UInt32),
  (loopbackTest, (Mutable),						Bool),
  (vaultStore,	(Mutable, Required),					String),
  (vaultModule, (Mutable),						String),
  (vaultTestStore, (Mutable),						Bool));

} // Ztc

#endif /* ztcagent_config_HH */
