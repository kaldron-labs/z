//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z QUIC path state

#ifndef Zutcp_HH
#error "include zlib/Zutcp.hh before this header"
#endif

#include <zlib/ZiIP.hh>

namespace Zutcp {

struct PathHint {
  PathHintKind::T	kind = PathHintKind::None;
  unsigned		mtu = 0;
  int			error = 0;

  ZuOpBool
  bool operator !() const { return kind == PathHintKind::None; }
};

struct PathDiag {
  uint64_t	bytesRx = 0;
  uint64_t	bytesTx = 0;
  uint64_t	probesSent = 0;
  uint64_t	probesAckd = 0;
  uint64_t	probesExpired = 0;
  uint64_t	probesLost = 0;
  uint64_t	blackholes = 0;
  uint64_t	kernelHints = 0;
  uint64_t	sendTooBigHints = 0;
  uint64_t	ecnProbesSent = 0;
  uint64_t	ecnProbesAckd = 0;
  uint64_t	ecnFailures = 0;
  uint64_t	ecnValidationFailures = 0;
  uint64_t	ecnCEEvents = 0;
  uint64_t	ecnCEBytes = 0;
};

class Path {
public:
  static constexpr unsigned MaxProbeAttempts = 3;
  static constexpr unsigned ECNProbeThreshold = 10;

  Path() = default;
  Path(PathMode::T mode, ZiSockAddr local, ZiSockAddr remote) :
      m_mode{mode}, m_local{ZuMv(local)}, m_remote{ZuMv(remote)} { }

  static Path client(ZiSockAddr local, ZiSockAddr remote) {
    return Path{PathMode::ClientConnected, ZuMv(local), ZuMv(remote)};
  }
  static Path server(ZiSockAddr local, ZiSockAddr remote) {
    return Path{PathMode::ServerUnconnected, ZuMv(local), ZuMv(remote)};
  }

  PathMode::T mode() const { return m_mode; }
  const ZiSockAddr &local() const { return m_local; }
  const ZiSockAddr &remote() const { return m_remote; }
  bool validated() const { return m_validated; }
  bool ecnDisabled() const {
    return m_ecnState == PathECNState::Disabled ||
      m_ecnState == PathECNState::Failed;
  }
  PathECNState::T ecnState() const { return m_ecnState; }
  EcnMark::T activeECN() const { return m_activeECN; }
  const AckECN &peerAckECN(PktNumSpace::T space) const {
    return m_peerAckECN[space];
  }
  AckECN &peerAckECN(PktNumSpace::T space) {
    return m_peerAckECN[space];
  }
  const AckECN &sentECN(PktNumSpace::T space) const {
    return m_sentECN[space];
  }
  AckECN &sentECN(PktNumSpace::T space) {
    return m_sentECN[space];
  }
  unsigned ecnProbeSent() const { return m_ecnProbeSent; }
  unsigned ecnProbeAckd() const { return m_ecnProbeAckd; }
  bool ecnProbeExpired() const {
    return m_ecnState == PathECNState::Testing &&
      m_ecnProbeSent >= ECNProbeThreshold;
  }

  unsigned activeMaxPayload() const { return m_activeMaxPayload; }
  unsigned peerMaxPayload() const { return m_peerMaxPayload; }
  unsigned configuredMaxPayload() const { return m_configuredMaxPayload; }
  unsigned probeSize() const { return m_probeSize; }
  bool probePending() const { return m_probeSize; }
  unsigned probeAttempts() const { return m_probeAttempts; }
  unsigned retryProbeSize() const { return m_retryProbeSize; }
  bool probeRetryPending() const { return m_retryProbeSize; }
  unsigned failureFloor() const { return m_failureFloor; }
  PMTUDState::T pmtudState() const { return m_pmtudState; }

  const PathDiag &diag() const { return m_diag; }
  PathDiag &diag() { return m_diag; }

  void validated(bool b = true) { m_validated = b; }
  void setEcnDisabled(bool b = true) {
    if (b)
      failECN();
    else
      ecnTesting();
  }
  void ecn(bool enabled) {
    if (enabled)
      ecnTesting();
    else
      ecnDisabledState();
  }
  void ecnTesting() {
    clearECNCounters();
    m_ecnState = PathECNState::Testing;
    m_activeECN = EcnMark::ECT0;
  }
  void ecnCapable() {
    m_ecnState = PathECNState::Capable;
    m_activeECN = EcnMark::ECT0;
  }
  void failECN() {
    if (m_ecnState != PathECNState::Failed) ++m_diag.ecnFailures;
    m_ecnState = PathECNState::Failed;
    m_activeECN = EcnMark::NotECT;
  }
  void ecnDisabledState() {
    clearECNCounters();
    m_ecnState = PathECNState::Disabled;
    m_activeECN = EcnMark::NotECT;
  }
  EcnMark::T txECN(
      PktNumSpace::T level, bool ackEliciting, bool pmtudProbe) const {
    if (level != PktNumSpace::AppData || !ackEliciting || pmtudProbe)
      return EcnMark::NotECT;
    switch (m_ecnState) {
      case PathECNState::Testing:
      case PathECNState::Capable:
	return m_activeECN;
      default:
	return EcnMark::NotECT;
    }
  }
  void sentECN(PktNumSpace::T space, EcnMark::T ecn) {
    AckECN &sent = m_sentECN[space];
    switch (ecn) {
      case EcnMark::ECT0: ++sent.ect0; break;
      case EcnMark::ECT1: ++sent.ect1; break;
      case EcnMark::CE: ++sent.ce; break;
      default: break;
    }
    if (m_ecnState == PathECNState::Testing && ecn != EcnMark::NotECT) {
      ++m_ecnProbeSent;
      ++m_diag.ecnProbesSent;
    }
  }
  void ackdECN(unsigned packets) {
    if (!packets) return;
    if (m_ecnState == PathECNState::Testing) {
      m_ecnProbeAckd += packets;
      m_diag.ecnProbesAckd += packets;
      ecnCapable();
    }
  }
  void ceECN(uint64_t bytes) {
    if (!bytes) return;
    ++m_diag.ecnCEEvents;
    m_diag.ecnCEBytes += bytes;
  }
  void ecnValidationFailed() {
    ++m_diag.ecnValidationFailures;
    failECN();
  }

  void peerMaxPayload(unsigned v) {
    ZiAssert(v >= MinPktPayload && v <= BufSize, "Zutcp", (v),
      "invalid peer max packet payload " << v, return);
    m_peerMaxPayload = v;
    clampActive();
  }
  void configuredMaxPayload(unsigned v) {
    ZiAssert(v >= MinPktPayload && v <= BufSize, "Zutcp", (v),
      "invalid configured max packet payload " << v, return);
    m_configuredMaxPayload = v;
    clampActive();
  }

  void routePayload(unsigned v) {
    ZiAssert(v >= MinPktPayload, "Zutcp", (v), "route payload below setup floor", return);
    m_routePayload = v;
    clampActive();
  }

  unsigned ceiling() const {
    unsigned n = m_peerMaxPayload;
    if (m_configuredMaxPayload < n) n = m_configuredMaxPayload;
    if (BufSize < n) n = BufSize;
    if (m_routePayload < n) n = m_routePayload;
    return n;
  }

  unsigned nextProbeSize(unsigned step = 64) const {
    if (m_probeSize) return 0;
    if (m_retryProbeSize) return m_retryProbeSize;
    unsigned c = ceiling();
    if (m_activeMaxPayload >= c) return 0;
    if (!step) step = 64;
    unsigned n =
      step > unsigned(-1) - m_activeMaxPayload ? c : m_activeMaxPayload + step;
    if (n > c) n = c;
    if (m_failureFloor && n >= m_failureFloor) {
      if (m_failureFloor <= m_activeMaxPayload + 1) return 0;
      n = m_failureFloor - 1;
    }
    return n > m_activeMaxPayload ? n : 0;
  }

  bool startNextProbe(unsigned step = 64) {
    unsigned n = nextProbeSize(step);
    if (!n) return false;
    return startProbeChecked(n);
  }

  bool canSend(unsigned bytes) const {
    if (bytes > m_activeMaxPayload) return false;
    if (m_validated) return true;
    return bytes <= antiAmpRemaining();
  }
  bool canSendProbe(unsigned bytes) const {
    if (bytes < MinPktPayload || bytes > ceiling()) return false;
    if (m_validated) return true;
    return bytes <= antiAmpRemaining();
  }

  uint64_t antiAmpLimit() const {
    if (m_validated) return uint64_t(-1);
    if (m_bytesRx > uint64_t(-1) / 3) return uint64_t(-1);
    return m_bytesRx * 3;
  }
  uint64_t antiAmpRemaining() const {
    if (m_validated) return uint64_t(-1);
    uint64_t limit = antiAmpLimit();
    return m_bytesTx < limit ? limit - m_bytesTx : 0;
  }
  unsigned sendAllowance() const {
    unsigned n = m_activeMaxPayload;
    uint64_t r = antiAmpRemaining();
    if (r < n) n = r;
    return n;
  }
  bool reserveSend(unsigned bytes) {
    if (!canSend(bytes)) return false;
    sent(bytes);
    return true;
  }

  void received(unsigned bytes) {
    m_bytesRx += bytes;
    m_diag.bytesRx += bytes;
  }
  void sent(unsigned bytes) {
    m_bytesTx += bytes;
    m_diag.bytesTx += bytes;
  }

  void startProbe(unsigned size) {
    ZiAssert(!m_probeSize, "Zutcp", (),
      "PMTUD probe started while another probe is pending", return);
    unsigned c = ceiling();
    if (size > c) size = c;
    if (size < MinPktPayload) size = MinPktPayload;
    if (size == m_retryProbeSize)
      m_retryProbeSize = 0;
    else if (size != m_probeAttemptSize) {
      m_probeAttemptSize = size;
      m_probeAttempts = 0;
      m_retryProbeSize = 0;
    }
    m_probeSize = size;
    m_pmtudState = PMTUDState::Searching;
    ++m_probeAttempts;
    ++m_diag.probesSent;
  }
  bool startProbeChecked(unsigned size) {
    unsigned c = ceiling();
    if (size > c) size = c;
    if (size < MinPktPayload) size = MinPktPayload;
    if (!canSendProbe(size)) return false;
    startProbe(size);
    return true;
  }
  void probeAckd() {
    if (m_probeSize > m_activeMaxPayload) m_activeMaxPayload = m_probeSize;
    m_probeSize = 0;
    m_retryProbeSize = 0;
    m_probeAttemptSize = 0;
    m_probeAttempts = 0;
    m_pmtudState =
      m_activeMaxPayload >= ceiling() ? PMTUDState::SearchComplete : PMTUDState::Base;
    ++m_diag.probesAckd;
  }
  void probeLost() {
    if (!m_probeSize) return;
    unsigned size = m_probeSize;
    m_probeSize = 0;
    failProbe(size);
  }
  bool probeExpired() {
    if (!m_probeSize) return false;
    unsigned size = m_probeSize;
    m_probeSize = 0;
    ++m_diag.probesExpired;
    if (m_probeAttempts < MaxProbeAttempts) {
      m_retryProbeSize = size;
      m_pmtudState = PMTUDState::Base;
      return true;
    }
    if (size <= m_activeMaxPayload && m_activeMaxPayload > MinPktPayload) {
      blackhole();
      return false;
    }
    failProbe(size);
    return false;
  }
  void blackhole() {
    m_activeMaxPayload = MinPktPayload;
    m_probeSize = 0;
    m_retryProbeSize = 0;
    m_probeAttemptSize = 0;
    m_probeAttempts = 0;
    m_pmtudState = PMTUDState::Error;
    ++m_diag.blackholes;
  }

  void applyHint(PathHint hint) {
    if (!hint || hint.mtu < MinPktPayload) return;
    if (hint.mtu < m_activeMaxPayload) m_activeMaxPayload = hint.mtu;
    if (hint.mtu < m_peerMaxPayload) m_peerMaxPayload = hint.mtu;
    if (!m_failureFloor || hint.mtu < m_failureFloor)
      m_failureFloor = hint.mtu;
    if (hint.kind == PathHintKind::KernelMTU ||
	hint.kind == PathHintKind::PktTooBig)
      ++m_diag.kernelHints;
    if (hint.kind == PathHintKind::SendTooBig) ++m_diag.sendTooBigHints;
    if (m_probeSize >= hint.mtu || m_retryProbeSize >= hint.mtu) {
      m_probeSize = 0;
      m_retryProbeSize = 0;
      m_probeAttemptSize = 0;
      m_probeAttempts = 0;
      m_pmtudState = PMTUDState::Base;
    }
    clampActive();
  }

private:
  void clearECNCounters() {
    for (auto &ecn : m_peerAckECN) ecn.reset();
    for (auto &ecn : m_sentECN) ecn.reset();
    m_ecnProbeSent = 0;
    m_ecnProbeAckd = 0;
  }
  void clampActive() {
    unsigned c = ceiling();
    if (m_activeMaxPayload > c) m_activeMaxPayload = c;
    if (m_activeMaxPayload < MinPktPayload) m_activeMaxPayload = MinPktPayload;
  }
  void failProbe(unsigned size) {
    if (size && (!m_failureFloor || size < m_failureFloor))
      m_failureFloor = size;
    m_retryProbeSize = 0;
    m_probeAttemptSize = 0;
    m_probeAttempts = 0;
    m_pmtudState = PMTUDState::Base;
    ++m_diag.probesLost;
  }

  PathMode::T	m_mode = PathMode::ServerUnconnected;
  ZiSockAddr	m_local;
  ZiSockAddr	m_remote;
  bool		m_validated = false;
  PathECNState::T m_ecnState = PathECNState::Disabled;
  EcnMark::T	m_activeECN = EcnMark::NotECT;
  uint64_t	m_bytesRx = 0;
  uint64_t	m_bytesTx = 0;
  ZuArray<AckECN, PktNumSpace::N>
		m_peerAckECN =
		  ZuArray<AckECN, PktNumSpace::N>(PktNumSpace::N);
  ZuArray<AckECN, PktNumSpace::N>
		m_sentECN =
		  ZuArray<AckECN, PktNumSpace::N>(PktNumSpace::N);
  unsigned	m_ecnProbeSent = 0;
  unsigned	m_ecnProbeAckd = 0;
  unsigned	m_activeMaxPayload = MinPktPayload;
  unsigned	m_peerMaxPayload = BufSize;
  unsigned	m_routePayload = BufSize;
  unsigned	m_configuredMaxPayload = BufSize;
  unsigned	m_probeSize = 0;
  unsigned	m_retryProbeSize = 0;
  unsigned	m_probeAttemptSize = 0;
  unsigned	m_probeAttempts = 0;
  unsigned	m_failureFloor = 0;
  PMTUDState::T m_pmtudState = PMTUDState::Base;
  PathDiag	m_diag;
};

} // namespace Zutcp
