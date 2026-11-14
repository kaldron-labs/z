//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z QUIC protocol vocabulary

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

namespace Zutcp {

template <typename T, unsigned N>
inline ZuArray<T, N> fixedArray(const T &value = T{}) {
  ZuArray<T, N> array(N);
  for (auto &v : array) v = value;
  return array;
}


struct Connected {
  uint32_t	version = 0;
};

ZuDerive(Host, ZtString<ZtStringHeapID<"Zutcp.Host">>);
ZuDerive(ParamString, ZtString<ZtStringHeapID<"Zutcp.Param">>);

inline constexpr uint32_t Version1 = 0x00000001U;
// Retained intermediate Initial payload floor, removed at full TCP wire conversion.
inline constexpr unsigned MinPktPayload = 1200;
inline constexpr unsigned BufSize = 1472;
inline constexpr unsigned MinCIDLength = 8;

ZtEnumStruct(ZutcpAPI, DataError, int8_t, None, Reset, Stop);

struct PktNumSpace {
  ZtEnum(ZutcpAPI, PktNumSpace, int8_t, Initial, AppData);
  ZtEnumMap(ZutcpAPI, PktNumSpace, JSON,
    "initial", "application_data", "unknown");
};

struct PktType {
  ZtEnum(ZutcpAPI, PktType, int8_t, Initial, Short);
  ZtEnumMap(ZutcpAPI, PktType, JSON,
    "initial", "short", "unknown");
};

inline PktType::T pktTypeFromSpace(PktNumSpace::T space)
{
  switch (space) {
    case PktNumSpace::Initial: return PktType::Initial;
    case PktNumSpace::AppData: return PktType::Short;
    default: return PktType::N;
  }
}







struct LinkState {
  ZtEnum(ZutcpAPI, LinkState, int8_t,
    Starting, Handshaking, Established, Closing, Draining, Closed);
  ZtEnumMap(ZutcpAPI, LinkState, JSON,
    "attempted", "handshake_started", "handshake_complete",
    "closing", "draining", "closed");
};

ZtEnumStruct(ZutcpAPI, TransportError, uint16_t,
  NoError, InternalError, CxnRefused, FlowControl,
  DataState, FinalSize, FrameEncoding,
  TransportParam, CxnIDLimit, ProtViolation);

struct FrameType {
  ZtEnum(ZutcpAPI, FrameType, int8_t,
    Padding, Ping, Ack, Data, MaxData,
    DataBlocked,
    Reset, StopSending,
    ConnectionClose, ApplicationClose,
    Unknown);
  ZtEnumMap(ZutcpAPI, FrameType, JSON,
    "padding", "ping", "ack", "data", "max_data",
    "data_blocked", "reset",
    "stop_sending",
    "connection_close", "application_close", "unknown");
};

ZtEnumStruct(ZutcpAPI, CxnState, int8_t, Active, Tombstone);

ZtEnumStruct(ZutcpAPI, PathMode, int8_t, ClientConnected, ServerUnconnected);

ZtEnumStruct(ZutcpAPI, IPFamily, int8_t, IPv4, IPv6);

ZtEnumStruct(ZutcpAPI, PMTUDState, int8_t, Base, Searching, SearchComplete, Error);

ZtEnumStruct(ZutcpAPI, PathECNState, int8_t, Disabled, Testing, Capable, Failed);

ZtEnumStruct(ZutcpAPI, PathHintKind, int8_t, None, KernelMTU, PktTooBig, SendTooBig);

struct EcnMark {
  ZtEnum(ZutcpAPI, EcnMark, int8_t, NotECT, ECT0, ECT1, CE);
  ZtEnumMap(ZutcpAPI, EcnMark, JSON, "Not-ECT", "ECT0", "ECT1", "CE", "unknown");
};

ZtEnumStruct(ZutcpAPI, ServerPktAction, int8_t, Drop, AcceptInitial, VersionNeg);

ZtEnumStruct(ZutcpAPI, RecEvt, int8_t, Ackd, Lost, PTO, PersistCong);

ZtEnumStruct(ZutcpAPI, SentFrameKind, int8_t, None, Data, Control);

} // namespace Zutcp
