//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z QUIC protocol vocabulary

#ifndef ZquicTypes_HH
#define ZquicTypes_HH

#ifndef ZquicLib_HH
#include <zlib/ZquicLib.hh>
#endif

#include <stdint.h>

#include <zlib/ZuDerive.hh>

#include <zlib/ZtEnum.hh>
#include <zlib/ZtString.hh>

namespace Zquic {

ZuDerive(Host, ZtString<ZtStringHeapID<"Zquic.Host">>);
ZuDerive(ParamString, ZtString<ZtStringHeapID<"Zquic.Param">>);

class StatelessResetToken {
public:
  static constexpr unsigned Length = 16;

  StatelessResetToken() = default;
  explicit StatelessResetToken(ZuCSpan token) { set(token); }

  bool set(ZuCSpan);
  bool generate();
  bool valid() const { return m_valid; }
  unsigned length() const { return m_valid ? Length : 0; }
  const uint8_t *data() const { return m_data; }
  ZuCSpan cspan() const {
    return ZuCSpan{
      reinterpret_cast<const char *>(m_data), m_valid ? Length : 0};
  }

  bool equals(const StatelessResetToken &) const;

  friend inline bool operator ==(
    const StatelessResetToken &l, const StatelessResetToken &r) {
    return l.equals(r);
  }

private:
  uint8_t	m_data[Length] = {};
  bool		m_valid = false;
};

inline constexpr uint32_t Version1 = 0x00000001U;
inline constexpr unsigned MinUDPPayload = 1200;
inline constexpr unsigned BufSize = 1472;
inline constexpr unsigned MinCIDLength = 8;

struct StreamType {
  ZtEnum(StreamType, int8_t, Bidi, Uni);
};

struct StreamError {
  ZtEnum(StreamError, int8_t, None, Reset, Stop);
};

struct PacketSpace {
  ZtEnum(PacketSpace, int8_t, Initial, Handshake, AppData);
};

struct PacketType {
  ZtEnum(PacketType, int8_t, Initial, ZeroRTT, Handshake, Retry, Short);
};

struct CloseState {
  ZtEnum(CloseState, int8_t, Open, Closing, Draining, Closed);
};

struct LinkState {
  ZtEnum(LinkState, int8_t,
    Starting, Handshaking, Established, Closing, Draining, Closed);
};

struct TransportError {
  ZtEnum(TransportError, uint16_t,
    NoError, InternalError, ConnectionRefused, FlowControl,
    StreamLimit, StreamState, FinalSize, FrameEncoding,
    TransportParameter, ConnectionIDLimit, ProtocolViolation);
};

struct FrameType {
  ZtEnum(FrameType, int8_t,
    Padding, Ping, Ack, Crypto, Stream, MaxData, MaxStreamData,
    MaxStreams, DataBlocked, StreamDataBlocked, StreamsBlocked,
    ResetStream, StopSending, NewToken, NewConnectionID, RetireConnectionID,
    PathChallenge, PathResponse, ConnectionClose, HandshakeDone, Unknown);
};

struct CxnState {
  ZtEnum(CxnState, int8_t, Active, Retired, Tombstone);
};

struct PathMode {
  ZtEnum(PathMode, int8_t, ClientConnected, ServerUnconnected);
};

struct IPFamily {
  ZtEnum(IPFamily, int8_t, IPv4, IPv6);
};

struct PMTUDState {
  ZtEnum(PMTUDState, int8_t, Base, Searching, SearchComplete, Error);
};

struct PathHintKind {
  ZtEnum(PathHintKind, int8_t, None, KernelMTU, PacketTooBig, SendTooBig);
};

struct ServerPacketAction {
  ZtEnum(ServerPacketAction, int8_t, Drop, AcceptInitial, VersionNegotiation);
};

struct RecoveryEvent {
  ZtEnum(RecoveryEvent, int8_t, Acked, Lost, PTO, PersistentCongestion);
};

struct SentFrameKind {
  ZtEnum(SentFrameKind, int8_t, None, Stream, Crypto, Control);
};

struct CryptoLevel {
  ZtEnum(CryptoLevel, int8_t, Initial, Handshake, OneRTT);
};

} // namespace Zquic

#endif /* ZquicTypes_HH */
