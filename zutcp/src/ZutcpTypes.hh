//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// TCP transport vocabulary

#ifndef Zutcp_HH
#error "include zlib/Zutcp.hh before this header"
#endif

#include <stddef.h>
#include <stdint.h>

#include <zlib/ZuArray.hh>
#include <zlib/ZuDerive.hh>
#include <zlib/ZuSpan.hh>

#include <zlib/ZtEnum.hh>
#include <zlib/ZtString.hh>

#include <zlib/ZiIP.hh>

namespace Zutcp {

template <typename T, unsigned N>
inline ZuArray<T, N> fixedArray(const T &value = T{}) {
  ZuArray<T, N> array(N);
  for (auto &v : array) v = value;
  return array;
}


struct Connected { };

struct Vantage {
  ZtEnum(ZutcpAPI, Vantage, int8_t, Unknown, Client, Server);
  ZtEnumMap(ZutcpAPI, Vantage, JSON, "unknown", "client", "server");
};

struct LinkInfo {
  Vantage::T	vantage = Vantage::Unknown;
  ZiSockAddr	local, remote;
};

struct ByteData {
  uint64_t	offset = 0;
  uint64_t	length = 0;
  ZuBSpan	payload;
  bool		fin = false;
};

ZuDerive(Host, ZtString<ZtStringHeapID<"Zutcp.Host">>);
ZuDerive(ParamString, ZtString<ZtStringHeapID<"Zutcp.Param">>);

enum { DefaultMaxPayload = 1200 }; // configured cap, not a wire floor
enum { BufSize = 1472 };

ZtEnumStruct(ZutcpAPI, DataError, int8_t, None, Reset, Stop);

struct LinkState {
  ZtEnum(ZutcpAPI, LinkState, int8_t,
    Starting, Handshaking, Established, Closing, Draining, Closed);
  ZtEnumMap(ZutcpAPI, LinkState, JSON,
    "attempted", "handshake_started", "handshake_complete",
    "closing", "draining", "closed");
};

ZtEnumStruct(ZutcpAPI, TransportError, uint16_t,
  NoError, InternalError, CxnRefused, FlowControl,
  DataState, FinalSize, ProtViolation);

ZtEnumStruct(ZutcpAPI, PathMode, int8_t, ClientConnected, ServerUnconnected);

ZtEnumStruct(ZutcpAPI, IPFamily, int8_t, IPv4, IPv6);

ZtEnumStruct(ZutcpAPI, PMTUDState, int8_t, Base, Searching, SearchComplete, Error);

ZtEnumStruct(ZutcpAPI, PathECNState, int8_t, Disabled, Testing, Capable, Failed);

ZtEnumStruct(ZutcpAPI, PathHintKind, int8_t, None, KernelMTU, PktTooBig, SendTooBig);

struct EcnMark {
  ZtEnum(ZutcpAPI, EcnMark, int8_t, NotECT, ECT0, ECT1, CE);
  ZtEnumMap(ZutcpAPI, EcnMark, JSON, "Not-ECT", "ECT0", "ECT1", "CE", "unknown");
};

ZtEnumStruct(ZutcpAPI, RecEvt, int8_t, Ackd, Lost, RTO, PersistCong);

ZtEnumStruct(ZutcpAPI, SentFrameKind, int8_t, None, Data);

} // namespace Zutcp
