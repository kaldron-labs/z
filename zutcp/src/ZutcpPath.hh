//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// TCP route, MTU and congestion-mark observations

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
  uint64_t	ecnFailures = 0;
  uint64_t	ecnCEEvents = 0;
  uint64_t	ecnCEBytes = 0;
};

class Path {
public:
  enum { MaxProbeAttempts = 3 };

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
    m_ecnState = PathECNState::Disabled;
    m_activeECN = EcnMark::NotECT;
  }
  void ceECN(uint64_t bytes) {
    if (!bytes) return;
    ++m_diag.ecnCEEvents;
    m_diag.ecnCEBytes += bytes;
  }
  void peerMaxPayload(unsigned v) {
    ZiAssert(v && v <= BufSize, "Zutcp", (v),
      "invalid peer max packet payload " << v, return);
    m_peerMaxPayload = v;
    clampActive();
  }
  void configuredMaxPayload(unsigned v) {
    ZiAssert(v && v <= BufSize, "Zutcp", (v),
      "invalid configured max packet payload " << v, return);
    m_configuredMaxPayload = v;
    clampActive();
  }

  void routePayload(unsigned v) {
    ZiAssert(v, "Zutcp", (), "zero route payload", return);
    m_routePayload = v;
    clampActive();
  }

  void useRoutePayload() { m_activeMaxPayload = ceiling(); }

  unsigned basePayload() const {
    unsigned fallback = m_local.type() == ZiIPType::V6 ? 1220 : 536;
    unsigned limit = ceiling();
    return fallback < limit ? fallback : limit;
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

  bool canSend(unsigned bytes) const { return bytes <= m_activeMaxPayload; }
  bool canSendProbe(unsigned bytes) const { return bytes && bytes <= ceiling(); }
  unsigned sendAllowance() const { return m_activeMaxPayload; }
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
    if (size <= m_activeMaxPayload && m_activeMaxPayload > basePayload()) {
      blackhole();
      return false;
    }
    failProbe(size);
    return false;
  }
  void blackhole() {
    m_activeMaxPayload = basePayload();
    m_probeSize = 0;
    m_retryProbeSize = 0;
    m_probeAttemptSize = 0;
    m_probeAttempts = 0;
    m_pmtudState = PMTUDState::Error;
    ++m_diag.blackholes;
  }

  void applyHint(PathHint hint) {
    if (!hint || !hint.mtu) return;
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
  void clampActive() {
    unsigned c = ceiling();
    if (m_activeMaxPayload > c) m_activeMaxPayload = c;
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
  unsigned	m_activeMaxPayload = BufSize;
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
