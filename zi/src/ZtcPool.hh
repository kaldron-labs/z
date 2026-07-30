//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// generic command/telemetry for I/O link pools

#ifndef ZtcPool_HH
#define ZtcPool_HH

#ifndef ZiLib_HH
#include <zlib/ZiLib.hh>
#endif

#include <zlib/ZuID.hh>
#include <zlib/ZuTuple.hh>

#include <zlib/ZtEnum.hh>

#include <zlib/ZtcLink.hh>

namespace Ztc {

ZtEnumNS(PoolState, int8_t,
  Down,
  Up,
  Failed);

struct PoolTelemetry {
  ZuID		hubID;		// primary key
  ZuID		id;		// primary key
  uint64_t	rxCalls = 0;
  uint64_t	rxBytes = 0;
  uint64_t	txCalls = 0;
  uint64_t	txBytes = 0;
  uint16_t	idle = 0;	// #links available now
  uint16_t	busy = 0;	// #links available in future
  uint16_t	down = 0;	// #links unavailable
  LinkType::T	type = -1;
  PoolState::T	state = -1;
};

struct Pool {
  virtual ZuTuple<const ZuID &, const ZuID &>
    telKey() const = 0;	// { hubID, id }
  virtual void telemetry(PoolTelemetry &data) const = 0;
  virtual unsigned allQueues(QueueMgr::AllFn fn) const = 0;
};

} // Ztc

#endif /* ZtcPool_HH */
