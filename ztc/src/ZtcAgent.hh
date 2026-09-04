//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// private telemetry agent

#ifndef ZtcAgent_HH
#define ZtcAgent_HH

#ifndef ZtcLib_HH
#include <zlib/ZtcLib.hh>
#endif

#include <limits.h>
#include <stdint.h>

#include <zlib/ZuID.hh>

#include <zlib/ZmFn.hh>

#include <zlib/ZtString.hh>

#include <zlib/ZfStruct.hh>

#include <zlib/ZiFile.hh>

#include <zlib/ZvMxParams.hh>

#include <zlib/Ztls.hh>

#include <zlib/ZtcApp.hh>

namespace Ztc {

using AgentToken = ZtString<ZtStringHeapID<"Ztc.Agent.Token">>;
using AgentDeviceID = ZtString<ZtStringHeapID<"Ztc.Agent.DeviceID">>;
using AgentEnrollURL = ZtString<ZtStringHeapID<"Ztc.Agent.EnrollURL">>;

struct AgentEnv {
  AgentToken		token;
  Zi::Name		ring;
  Zi::Path		pidDir;
  AgentDeviceID		deviceID;
  AgentEnrollURL	enrollURL;
};

struct AgentCf {
  // Protocol frames are below INT_MAX; queue counts/bytes are independently
  // bounded resident-memory and per-turn workload controls.
  enum {
    MaxFrame = 1U<<30,
    DefltMaxFrame = AppCf::DefltMaxFrame,
    DefltTelFrames = 1024,
    DefltTelBytes = 1U<<24,
    DefltReqFrames = 256,
    DefltReqBytes = 1U<<22,
    DefltFanoutBatch = 64,
    DefltEnrollRetry = 1,
    DefltReconnMin = 1,
    DefltReconnMax = 60,
    DefltTelSize = 1U<<21,
    DefltTelTimeout = 1,
    DefltReqTimeout = 1,
    DefltReqKillWait = 1
  };
  static constexpr double DefltReconnBackoff = 2;
  static constexpr double DefltReconnRandom = .25;

  unsigned	maxFrame = DefltMaxFrame;
  unsigned	telFrames = DefltTelFrames;
  uint64_t	telBytes = DefltTelBytes;
  unsigned	reqFrames = DefltReqFrames;
  uint64_t	reqBytes = DefltReqBytes;
  unsigned	fanoutBatch = DefltFanoutBatch;
  unsigned	enrollRetry = DefltEnrollRetry;
  unsigned	reconnMin = DefltReconnMin;
  unsigned	reconnMax = DefltReconnMax;
  double	reconnBackoff = DefltReconnBackoff;
  double	reconnRandom = DefltReconnRandom;
  unsigned	telSize = DefltTelSize;
  bool		telLL = false;
  unsigned	telSpin = 0;
  unsigned	telTimeout = DefltTelTimeout;
  unsigned	reqTimeout = DefltReqTimeout;
  unsigned	reqKillWait = DefltReqKillWait;
  bool		reqCoredump = false;
  ZvMxCf	mx{
    .nThreads = 3,
    .rxThread = "rx",
    .txThread = "tx"
  };
  unsigned	routeThread = 3;
};

ZfStruct((AgentCf, Cf),
  (((maxFrame),		((Range<64U, unsigned(AgentCf::MaxFrame)>))),
						(UInt32, 1U<<20U)),
  (((telFrames),	((Range<1U, unsigned(INT_MAX)>))), (UInt32, 1024)),
  (((telBytes),		((Range<64ULL, 1ULL<<40U>))),	(UInt64, 1U<<24U)),
  (((reqFrames),	((Range<1U, unsigned(INT_MAX)>))), (UInt32, 256)),
  (((reqBytes),		((Range<64ULL, 1ULL<<40U>))),	(UInt64, 1U<<22U)),
  (((fanoutBatch),	((Range<1U, 65536U>))),	(UInt32, 64)),
  (((enrollRetry),	((Range<1U, 3600U>))),		(UInt32, 1)),
  (((reconnMin),	((Range<1U, 3600U>))),		(UInt32, 1)),
  (((reconnMax),	((Range<1U, 86400U>))),	(UInt32, 60)),
  (((reconnBackoff),	((Range<1.0, 16.0>))),		(Float, 2)),
  (((reconnRandom),	((Range<0.0, 60.0>))),		(Float, .25)),
  (((telSize),		((Range<64U, 1U<<30U>))),	(UInt32, 1U<<21U)),
  (((telLL)),					(Bool)),
  (((telSpin),		((Range<0U, unsigned(INT_MAX)>))), (UInt32)),
  (((telTimeout),	((Range<1U, 3600U>))),		(UInt32, 1)),
  (((reqTimeout),	((Range<1U, 3600U>))),		(UInt32, 1)),
  (((reqKillWait),	((Range<0U, 3600U>))),		(UInt32, 1)),
  (((reqCoredump)),					(Bool)),
  (((mx)),						(UDT)),
  (((routeThread),	((Range<1U, 1024U>))),		(UInt32, 3)));

namespace Agent_ { class State; }

class Agent : public Ztls::Client<Agent> {
public:
  using CtrlFn = ZmFn<void(bool), ZmFnHeapID<"Ztc.Agent.CtrlFn">>;
  struct Link;

  Agent() = default;
  ~Agent();
  Agent(const Agent &) = delete;
  Agent &operator =(const Agent &) = delete;

  bool init(const AgentCf &, AgentEnv);
  void start(CtrlFn);
  bool start();
  void stop(CtrlFn);
  bool stop();
  void final();

  unsigned reconnFreq();

private:
  bool startAgent_();

  Agent_::State	*m_state = nullptr;
};

struct Agent::Link : public Ztls::CliLink<Agent, Link> {
  using Base = Ztls::CliLink<Agent, Link>;
  Link(Agent *agent) : Base{agent} { }

  void connected(Ztls::Connected);
  void disconnected(bool);
  void connectFailed(bool);
  int process(Ztls::RxStream &);

private:
  int process_(ZuSpan<uint8_t>);

  ZmRef<ZiIOBuf>	m_frame;
  uint64_t	m_cxnGen = 0;
  unsigned	m_size = 0;
};

} // Ztc

#endif /* ZtcAgent_HH */
