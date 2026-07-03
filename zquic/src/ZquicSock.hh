//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z QUIC socket controls

#ifndef ZquicSock_HH
#define ZquicSock_HH

#ifndef ZquicLib_HH
#include <zlib/ZquicLib.hh>
#endif

#include <zlib/ZiMultiplex.hh>

#include <zlib/ZquicBuf.hh>
#include <zlib/ZquicPath.hh>

namespace Zquic {

struct Datagram {
  ZmRef<ZiIOBuf>	buf;
  ZiSockAddr		addr;
  EcnMark::T		ecn = EcnMark::NotECT;

  Datagram() = default;
  Datagram(ZmRef<ZiIOBuf> buf_, ZiSockAddr addr_) :
    buf{ZuMv(buf_)}, addr{ZuMv(addr_)} { }
  Datagram(ZmRef<ZiIOBuf> buf_, ZiSockAddr addr_, EcnMark::T ecn_) :
    buf{ZuMv(buf_)}, addr{ZuMv(addr_)}, ecn{ecn_} { }
};

struct EndpointRxDiag {
  uint64_t	datagramsRx = 0;
  uint64_t	bytesRx = 0;
  uint64_t	failures = 0;
};

struct EndpointTxDiag {
  uint64_t	sendCalls = 0;
  uint64_t	directCalls = 0;
  uint64_t	asyncCalls = 0;
  uint64_t	submittedTx = 0;
  uint64_t	submittedBytes = 0;
  uint64_t	datagramsTx = 0;
  uint64_t	bytesTx = 0;
  uint64_t	txBackPressure = 0;
  uint64_t	txDropped = 0;
  uint64_t	failures = 0;
};

struct EndpointDiag {
  uint64_t failures() const { return rx.failures + tx.failures; }

  EndpointRxDiag	rx;
  EndpointTxDiag	tx;
  uint64_t	txPending = 0;
  uint64_t	txQueued = 0;
};

struct SockConfig {
  IPFamily::T	family = IPFamily::IPv4;
  PathMode::T	mode = PathMode::ServerUnconnected;
  bool		probe = false;
  bool		errorQueue = true;
};

struct SockPlan {
  bool		noFragment = false;
  bool		probeMode = false;
  bool		errorQueue = false;
  bool		pmtuQuery = false;
  bool		userMTU = false;
  unsigned	nOptions = 0;
};

struct SockDiag {
  uint64_t	optionsAttempted = 0;
  uint64_t	optionsApplied = 0;
  uint64_t	optionsUnsupported = 0;
  uint64_t	optionErrors = 0;
  uint64_t	mtuQueries = 0;
  uint64_t	mtuQueryErrors = 0;
};

struct Sock {
  static SockPlan plan(const SockConfig &);
  static bool initUDP(Zi::Socket, const SockConfig &, SockDiag * = nullptr);
  static PathHint pathHint(Zi::Socket, const SockConfig &, SockDiag * = nullptr);
};

} // namespace Zquic

#endif /* ZquicSock_HH */
