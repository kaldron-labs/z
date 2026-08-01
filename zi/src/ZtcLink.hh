//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// generic command/telemetry for I/O links

#ifndef ZtcLink_HH
#define ZtcLink_HH

#ifndef ZiLib_HH
#include <zlib/ZiLib.hh>
#endif

#include <zlib/ZuID.hh>
#include <zlib/ZuTuple.hh>

#include <zlib/ZtEnum.hh>

#include <zlib/ZtcQueue.hh>

namespace Ztc {

ZtEnumNS(ZiAPI, LinkType, int8_t, TCP, TLS, QUIC, H1, H3, WS, FIX);

ZtEnumNS(ZiAPI, LinkState, int8_t,
  Down,
  Disabled,
  Deleted,
  Connecting,
  Up,
  ReconnectPending,
  Reconnecting,
  Failed,
  Disconnecting,
  ConnectPending,
  DisconnectPending);

struct LinkTelemetry {
  ZuID		hubID;		// primary key
  ZuID		id;		// primary key
  uint64_t	rxCalls = 0;
  uint64_t	txCalls = 0;
  uint64_t	rxBytes = 0;
  uint64_t	txBytes = 0;
  uint32_t	reconnects = 0;
  LinkType::T	type = -1;
  LinkState::T	state = -1;

  RAG::T rag() const {
    switch (state) {
      case LinkState::Down:
      case LinkState::Failed:
	return RAG::Red;
      case LinkState::Disabled:
      case LinkState::Deleted:
	return RAG::Off;
      case LinkState::Up:
	return RAG::Green;
      default:
	return RAG::Amber;
    }
  }
  void rag(RAG::T) { }
};

struct Link : public QueueMgr {
  virtual ZuTuple<const ZuID &, const ZuID &>
    telKey() const = 0;	// { hubID, id }
  virtual void telemetry(LinkTelemetry &data) const = 0;
  virtual void up() = 0;
  virtual void down() = 0;
};

} // Ztc

#endif /* ZtcLink_HH */
