//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z QUIC socket controls

#ifndef Zquic_HH
#error "include zlib/Zquic.hh before this header"
#endif

#include <zlib/ZiMultiplex.hh>

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

inline uint8_t ecnBits(EcnMark::T ecn)
{
  switch (ecn) {
    case EcnMark::ECT1: return 0x01;
    case EcnMark::ECT0: return 0x02;
    case EcnMark::CE: return 0x03;
    default: return 0x00;
  }
}

inline ZiTOS ecnTOS(EcnMark::T ecn)
{
  if (ecn == EcnMark::NotECT) return {};
  return uint8_t(ecnBits(ecn));
}

inline EcnMark::T ecnMarkFromTOS(ZiTOS tos)
{
  if (!tos.template is<uint8_t>()) return EcnMark::NotECT;
  switch (tos.template p<uint8_t>() & 0x03) {
    case 0x01: return EcnMark::ECT1;
    case 0x02: return EcnMark::ECT0;
    case 0x03: return EcnMark::CE;
    default: return EcnMark::NotECT;
  }
}

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

struct ZquicAPI Sock {
  static SockPlan plan(const SockConfig &);
  static bool initUDP(Zi::Socket, const SockConfig &, SockDiag * = nullptr);
  static PathHint pathHint(Zi::Socket, const SockConfig &, SockDiag * = nullptr);
};

} // namespace Zquic
