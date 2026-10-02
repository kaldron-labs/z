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
  String	vaultStore;
  String	vaultModule;
  bool		vaultTestStore = false;
};

ZfStruct(ZtcAPI, (AgentCf, Cf),
  (((maxFrame),		((Range<64U, unsigned(AgentCf::MaxFrame)>),
    Deflt<AgentCf::DefltMaxFrame>)),						UInt32),
  (((telFrames),	((Range<1U, 1U<<30U>),
    Deflt<AgentCf::DefltTelFrames>)),						UInt32),
  (((telBytes),		((Range<64ULL, 1ULL<<40U>),
    Deflt<AgentCf::DefltTelBytes>)),						UInt64),
  (((reqBytes),		((Range<64ULL, 1ULL<<40U>),
    Deflt<AgentCf::DefltReqBytes>)),						UInt64),
  (((fanoutBatch),	((Range<1U, 65536U>),
    Deflt<AgentCf::DefltFanoutBatch>)),						UInt32),
  (((reconnMin),	((Range<1U, 3600U>), Deflt<AgentCf::DefltReconnMin>)),	UInt32),
  (((reconnMax),	((Range<1U, 86400U>), Deflt<AgentCf::DefltReconnMax>)),	UInt32),
  (((reconnBackoff),	((Range<1.0, 16.0>),
    Deflt<AgentCf::DefltReconnBackoff>)),					Float),
  (((telSize),		((Range<64U, 1U<<30U>), Deflt<AgentCf::DefltTelSize>)),	UInt32),
  (((telLL)),									Bool),
  (((telSpin),		((Range<0U, 1U<<30U>), Deflt<0>)),			UInt32),
  (((telTimeout),	((Range<1U, 3600U>), Deflt<AgentCf::DefltTelTimeout>)),	UInt32),
  (((reqTimeout),	((Range<1U, 3600U>), Deflt<AgentCf::DefltReqTimeout>)),	UInt32),
  (((pubGCInterval),	((Range<1U, 3600U>),
    Deflt<AgentCf::DefltPubGCInterval>)),					UInt32),
  (((pubGCBatch),	((Range<1U, 65536U>),
    Deflt<AgentCf::DefltPubGCBatch>)),						UInt32),
  (((upgradeTimeout),	((Range<1U, 3600U>), Deflt<10>)),			UInt32),
  (((closeTimeout),	((Range<1U, 3600U>), Deflt<5>)),			UInt32),
  (((pingInterval),	((Range<0U, 3600U>), Deflt<30>)),			UInt32),
  (((idleTimeout),	((Range<0U, 3600U>), Deflt<60>)),			UInt32),
  (((loopbackTest)),								Bool),
  (((vaultStore),	(Required)),						String),
  (((vaultModule)),								String),
  (((vaultTestStore)),								Bool));

} // Ztc

#endif /* ztcagent_config_HH */
