//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z QUIC protocol vocabulary

#ifndef Zquic_HH
#error "include zlib/Zquic.hh before this header"
#endif

#include <stddef.h>
#include <stdint.h>

#include <zlib/ZuDerive.hh>

#include <zlib/ZiTransport.hh>

#include <zlib/ZtEnum.hh>
#include <zlib/ZtString.hh>

namespace Zquic {

ZuDerive(Host, ZtString<ZtStringHeapID<"Zquic.Host">>);
ZuDerive(ParamString, ZtString<ZtStringHeapID<"Zquic.Param">>);

class ResetToken {
public:
  static constexpr unsigned Length = 16;

  ResetToken() = default;
  explicit ResetToken(ZuBSpan token) { set(token); }

  bool set(ZuBSpan);
  bool generate();
  bool valid() const { return m_valid; }
  unsigned length() const { return m_valid ? Length : 0; }
  const uint8_t *data() const { return m_data; }
  ZuBSpan bspan() const {
    return ZuBSpan{m_data, m_valid ? Length : 0};
  }

  bool equals(const ResetToken &) const;

  friend inline bool operator ==(
    const ResetToken &l, const ResetToken &r) {
    return l.equals(r);
  }

private:
  uint8_t	m_data[Length] = {};
  bool		m_valid = false;
};

class PathChallenge {
public:
  static constexpr unsigned Length = 8;

  PathChallenge() = default;
  explicit PathChallenge(ZuBSpan data) { set(data); }

  bool set(ZuBSpan);
  bool generate();
  bool valid() const { return m_valid; }
  unsigned length() const { return m_valid ? Length : 0; }
  const uint8_t *data() const { return m_data; }
  ZuBSpan bspan() const {
    return ZuBSpan{m_data, m_valid ? Length : 0};
  }

  bool equals(ZuBSpan) const;
  bool equals(const PathChallenge &) const;

  friend inline bool operator ==(
    const PathChallenge &l, const PathChallenge &r) {
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

ZtEnumStruct(StreamError, int8_t, None, Reset, Stop);

struct PathRole {
  ZtEnum(PathRole, int8_t, Active, Candidate, Previous);
  ZtEnumMap(PathRole, JSON,
    "active", "candidate", "previous", "unknown");
};

struct MigrationState {
  ZtEnum(MigrationState, int8_t,
    Idle, Requested, Validating, Promoted, Failed);
  ZtEnumMap(MigrationState, JSON,
    "idle", "requested", "validating", "promoted", "failed", "unknown");
};

struct MigrationReason {
  ZtEnum(MigrationReason, int8_t,
    None, Passive, NATRebind, Active, Disabled, PeerDisabled, NoPeerCID,
    Endpoint, Validation, Timeout, Abandoned, Closed);
  ZtEnumMap(MigrationReason, JSON,
    "none", "passive", "nat_rebind", "active", "disabled",
    "peer_disabled", "no_peer_cid", "endpoint", "validation", "timeout",
    "abandoned", "closed", "unknown");
};

struct MigrationMode {
  ZtEnum(MigrationMode, int8_t, Disabled, Passive, Active);
  ZtEnumMap(MigrationMode, JSON,
    "disabled", "passive", "active", "unknown");
};

inline bool parseMigrationMode(ZuCSpan s, MigrationMode::T &mode)
{
  if (s == "disable") s = "disabled";
  auto mode_ = MigrationMode::JSON::exact(s);
  if (mode_ < 0 || mode_ >= MigrationMode::N) return false;
  mode = mode_;
  return true;
}

inline bool validMigrationMode(ZuCSpan s)
{
  MigrationMode::T mode;
  return parseMigrationMode(s, mode);
}

inline MigrationMode::T migrationMode(
  ZuCSpan s, MigrationMode::T deflt = MigrationMode::Passive)
{
  MigrationMode::T mode;
  return parseMigrationMode(s, mode) ? mode : deflt;
}

struct PathInfo {
  ZiSockAddr		local;
  ZiSockAddr		remote;
  uint64_t		peerCIDSequence = U64Null;
  uint64_t		bytesRx = 0;
  uint64_t		bytesTx = 0;
  unsigned		activeMaxUDP = 0;
  PathRole::T		role = PathRole::Active;
  MigrationState::T	migrationState = MigrationState::Idle;
  MigrationReason::T	reason = MigrationReason::None;
  bool			validated = false;
};

struct MigrationParams {
  ZiSockAddr		local;
  ZiSockAddr		remote;
  MigrationReason::T	reason = MigrationReason::Active;
  bool			rebindLocal = false;
  bool			requireNewPeerCID = true;
  bool			closeOnFailure = false;
};

struct MigrationResult {
  PathInfo		active;
  PathInfo		candidate;
  MigrationState::T	state = MigrationState::Idle;
  MigrationReason::T	reason = MigrationReason::None;
  uint64_t		attemptID = 0;
  bool			success = false;
};

struct PktNumSpace {
  ZtEnum(PktNumSpace, int8_t, Initial, Handshake, AppData);
  ZtEnumMap(PktNumSpace, JSON,
    "initial", "handshake", "application_data", "unknown");
};

struct PktType {
  ZtEnum(PktType, int8_t, Initial, ZeroRTT, Handshake, Retry, Short);
  ZtEnumMap(PktType, JSON,
    "initial", "0RTT", "handshake", "retry", "1RTT", "unknown");
};

struct PktKeyLevel {
  ZtEnum(PktKeyLevel, int8_t, Initial, Handshake, ZeroRTT, OneRTT);
  ZtEnumMap(PktKeyLevel, JSON,
    "initial", "handshake", "0RTT", "1RTT", "unknown");
};

struct ZeroRTTReason {
  ZtEnum(ZeroRTTReason, int8_t,
    None, Disabled, MissingTicket, TLSRejected, AppParams,
    TransportParams, FlowLimit, StreamLimit, ActiveCIDLimit,
    FramePolicy, MissingKeys, AfterOneRTT);
  ZtEnumMap(ZeroRTTReason, JSON,
    "none", "disabled", "missing_ticket", "tls_rejected", "app_params",
    "transport_params", "flow_limit", "stream_limit",
    "active_connection_id_limit", "frame_policy", "missing_keys",
    "after_1rtt", "unknown");
};

struct EarlyDataState {
  ZtEnum(EarlyDataState, int8_t,
    Disabled, Enabled, Offered, Accepted, Rejected, Done);
  ZtEnumMap(EarlyDataState, JSON,
    "disabled", "enabled", "offered", "accepted", "rejected", "done",
    "unknown");
};

struct LinkEarlyState {
  ZtEnum(LinkEarlyState, int8_t, None, Offered, Recv, Accepted, Rejected, Done);
  ZtEnumMap(LinkEarlyState, JSON,
    "none", "offered", "received", "accepted", "rejected", "done", "unknown");
};

// TLS epochs are not packet number spaces: 0-RTT and 1-RTT both use AppData.
inline bool spaceFromTLSEpoch(size_t epoch, PktNumSpace::T &space)
{
  if (epoch == 0) {
    space = PktNumSpace::Initial;
    return true;
  }
  if (epoch == 2) {
    space = PktNumSpace::Handshake;
    return true;
  }
  if (epoch >= 3) {
    space = PktNumSpace::AppData;
    return true;
  }
  return false;
}

inline bool keyLevelFromTLSEpoch(size_t epoch, PktKeyLevel::T &level)
{
  if (epoch == 0) {
    level = PktKeyLevel::Initial;
    return true;
  }
  if (epoch == 1) {
    level = PktKeyLevel::ZeroRTT;
    return true;
  }
  if (epoch == 2) {
    level = PktKeyLevel::Handshake;
    return true;
  }
  if (epoch >= 3) {
    level = PktKeyLevel::OneRTT;
    return true;
  }
  return false;
}

inline bool tlsEpochFromSpace(PktNumSpace::T space, size_t &epoch)
{
  switch (space) {
    case PktNumSpace::Initial:
      epoch = 0;
      return true;
    case PktNumSpace::Handshake:
      epoch = 2;
      return true;
    case PktNumSpace::AppData:
      epoch = 3;
      return true;
    default:
      return false;
  }
}

inline bool tlsEpochFromKeyLevel(PktKeyLevel::T level, size_t &epoch)
{
  switch (level) {
    case PktKeyLevel::Initial:
      epoch = 0;
      return true;
    case PktKeyLevel::ZeroRTT:
      epoch = 1;
      return true;
    case PktKeyLevel::Handshake:
      epoch = 2;
      return true;
    case PktKeyLevel::OneRTT:
      epoch = 3;
      return true;
    default:
      return false;
  }
}

inline PktType::T pktTypeFromSpace(PktNumSpace::T space)
{
  switch (space) {
    case PktNumSpace::Initial: return PktType::Initial;
    case PktNumSpace::Handshake: return PktType::Handshake;
    case PktNumSpace::AppData: return PktType::Short;
    default: return PktType::N;
  }
}

inline PktNumSpace::T spaceFromKeyLevel(PktKeyLevel::T level)
{
  switch (level) {
    case PktKeyLevel::Initial: return PktNumSpace::Initial;
    case PktKeyLevel::Handshake: return PktNumSpace::Handshake;
    case PktKeyLevel::ZeroRTT:
    case PktKeyLevel::OneRTT:
      return PktNumSpace::AppData;
    default:
      return PktNumSpace::N;
  }
}

inline bool isAppDataKeyLevel(PktKeyLevel::T level)
{
  return level == PktKeyLevel::ZeroRTT || level == PktKeyLevel::OneRTT;
}

inline PktType::T pktTypeFromKeyLevel(PktKeyLevel::T level)
{
  switch (level) {
    case PktKeyLevel::Initial: return PktType::Initial;
    case PktKeyLevel::Handshake: return PktType::Handshake;
    case PktKeyLevel::ZeroRTT: return PktType::ZeroRTT;
    case PktKeyLevel::OneRTT: return PktType::Short;
    default: return PktType::N;
  }
}

struct LinkState {
  ZtEnum(LinkState, int8_t,
    Starting, Handshaking, Established, Closing, Draining, Closed);
  ZtEnumMap(LinkState, JSON,
    "attempted", "handshake_started", "handshake_complete",
    "closing", "draining", "closed");
};

ZtEnumStruct(TransportError, uint16_t,
  NoError, InternalError, CxnRefused, FlowControl,
  StreamLimit, StreamState, FinalSize, FrameEncoding,
  TransportParam, CxnIDLimit, ProtViolation);

struct FrameType {
  ZtEnum(FrameType, int8_t,
    Padding, Ping, Ack, Crypto, Stream, MaxData, MaxStreamData,
    MaxStreams, DataBlocked, StreamDataBlocked, StreamsBlocked,
    ResetStream, StopSending, NewToken, NewCxnID, RetireCxnID,
    PathChallenge, PathResponse, ConnectionClose, ApplicationClose,
    HandshakeDone, Unknown);
  ZtEnumMap(FrameType, JSON,
    "padding", "ping", "ack", "crypto", "stream", "max_data",
    "max_stream_data", "max_streams", "data_blocked",
    "stream_data_blocked", "streams_blocked", "reset_stream",
    "stop_sending", "new_token", "new_connection_id",
    "retire_connection_id", "path_challenge", "path_response",
    "connection_close", "application_close", "handshake_done", "unknown");
};

ZtEnumStruct(CxnState, int8_t, Active, Retired, Tombstone);

ZtEnumStruct(PathMode, int8_t, ClientConnected, ServerUnconnected);

ZtEnumStruct(IPFamily, int8_t, IPv4, IPv6);

ZtEnumStruct(PMTUDState, int8_t, Base, Searching, SearchComplete, Error);

ZtEnumStruct(PathECNState, int8_t, Disabled, Testing, Capable, Failed);

ZtEnumStruct(PathHintKind, int8_t, None, KernelMTU, PktTooBig, SendTooBig);

struct EcnMark {
  ZtEnum(EcnMark, int8_t, NotECT, ECT0, ECT1, CE);
  ZtEnumMap(EcnMark, JSON, "Not-ECT", "ECT0", "ECT1", "CE", "unknown");
};

ZtEnumStruct(ServerPktAction, int8_t, Drop, AcceptInitial, VersionNeg);

ZtEnumStruct(RecEvt, int8_t, Ackd, Lost, PTO, PersistCong);

ZtEnumStruct(SentFrameKind, int8_t, None, Stream, Crypto, Control);

} // namespace Zquic
