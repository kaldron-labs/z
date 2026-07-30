//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// generic command/telemetry for I/O hubs

#ifndef ZtcHub_HH
#define ZtcHub_HH

#ifndef ZiLib_HH
#include <zlib/ZiLib.hh>
#endif

#include <zlib/ZuID.hh>
#include <zlib/ZuTuple.hh>

#include <zlib/ZmEngine.hh>
#include <zlib/ZmFn_.hh>

#include <zlib/ZtEnum.hh>

#include <zlib/ZtcLink.hh>
#include <zlib/ZtcPool.hh>

namespace Ztc {

namespace HubState {
  using namespace ZmEngineState;
  ZtEnumNames(HubState,
    Stopped, Starting, Running, Stopping, StartPending, StopPending);
}

struct HubTelemetry {
  ZuID		id;		// primary key
  ZuID		mxID;
  uint16_t	down = 0;
  uint16_t	disabled = 0;
  uint16_t	transient = 0;
  uint16_t	up = 0;
  uint16_t	reconn = 0;
  uint16_t	failed = 0;
  uint16_t	nLinks = 0;
  uint8_t	rxThread = 0;
  uint8_t	txThread = 0;
  LinkType::T	linkType = 0;	// primary key
  HubState::T	state = -1;
};

struct Hub {
  using AllLinksFn =
    ZmFn<void(Link *), ZmFnHeapID<"Ztc.Hub.AllLinksFn">>;
  using AllPoolsFn =
    ZmFn<void(Pool *), ZmFnHeapID<"Ztc.Hub.AllPoolsFn">>;

  virtual ZuTuple<LinkType::T, ZuID> telKey() const = 0;
  virtual void telemetry(HubTelemetry &data) const = 0;
  virtual bool start() = 0;
  virtual bool stop() = 0;
  virtual unsigned allLinks(AllLinksFn fn) const = 0;
  virtual unsigned allPools(AllPoolsFn fn) const = 0;
};

struct HubMgr {
  using AllFn = ZmFn<void(Hub *), ZmFnHeapID<"Ztc.Hub.AllFn">>;

  static void add(Hub *);
  static void del(Hub *);
  static void all(AllFn);
};

} // Ztc

#endif /* ZtcHub_HH */
