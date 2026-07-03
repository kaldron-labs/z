//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z QUIC path state

#ifndef Zquic_HH
#error "include zlib/Zquic.hh before this header"
#endif

#include <zlib/ZiIP.hh>


namespace Zquic {

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
};

class Path {
public:
  static constexpr unsigned MaxProbeAttempts = 3;

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
  bool ecnDisabled() const { return m_ecnDisabled; }

  unsigned activeMaxUDP() const { return m_activeMaxUDP; }
  unsigned peerMaxUDP() const { return m_peerMaxUDP; }
  unsigned configuredMaxUDP() const { return m_configuredMaxUDP; }
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
  void setEcnDisabled(bool b = true) { m_ecnDisabled = b; }

  void peerMaxUDP(unsigned v) {
    ZiAssert(v >= MinUDPPayload && v <= BufSize, "Zquic", (v),
      "invalid peer max UDP payload " << v, return);
    m_peerMaxUDP = v;
    clampActive_();
  }
  void configuredMaxUDP(unsigned v) {
    ZiAssert(v >= MinUDPPayload && v <= BufSize, "Zquic", (v),
      "invalid configured max UDP payload " << v, return);
    m_configuredMaxUDP = v;
    clampActive_();
  }

  unsigned ceiling() const {
    unsigned n = m_peerMaxUDP;
    if (m_configuredMaxUDP < n) n = m_configuredMaxUDP;
    if (BufSize < n) n = BufSize;
    return n;
  }

  unsigned nextProbeSize(unsigned step = 64) const {
    if (m_probeSize) return 0;
    if (m_retryProbeSize) return m_retryProbeSize;
    unsigned c = ceiling();
    if (m_activeMaxUDP >= c) return 0;
    if (!step) step = 64;
    unsigned n =
      step > unsigned(-1) - m_activeMaxUDP ? c : m_activeMaxUDP + step;
    if (n > c) n = c;
    if (m_failureFloor && n >= m_failureFloor) {
      if (m_failureFloor <= m_activeMaxUDP + 1) return 0;
      n = m_failureFloor - 1;
    }
    return n > m_activeMaxUDP ? n : 0;
  }

  bool startNextProbe(unsigned step = 64) {
    unsigned n = nextProbeSize(step);
    if (!n) return false;
    return startProbeChecked(n);
  }

  bool canSend(unsigned bytes) const {
    if (bytes > m_activeMaxUDP) return false;
    if (m_validated) return true;
    return bytes <= antiAmplificationRemaining();
  }
  bool canSendProbe(unsigned bytes) const {
    if (bytes < MinUDPPayload || bytes > ceiling()) return false;
    if (m_validated) return true;
    return bytes <= antiAmplificationRemaining();
  }

  uint64_t antiAmplificationLimit() const {
    if (m_validated) return uint64_t(-1);
    if (m_bytesRx > uint64_t(-1) / 3) return uint64_t(-1);
    return m_bytesRx * 3;
  }
  uint64_t antiAmplificationRemaining() const {
    if (m_validated) return uint64_t(-1);
    uint64_t limit = antiAmplificationLimit();
    return m_bytesTx < limit ? limit - m_bytesTx : 0;
  }
  unsigned sendAllowance() const {
    unsigned n = m_activeMaxUDP;
    uint64_t r = antiAmplificationRemaining();
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
    ZiAssert(!m_probeSize, "Zquic", (),
      "PMTUD probe started while another probe is pending", return);
    unsigned c = ceiling();
    if (size > c) size = c;
    if (size < MinUDPPayload) size = MinUDPPayload;
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
    if (size < MinUDPPayload) size = MinUDPPayload;
    if (!canSendProbe(size)) return false;
    startProbe(size);
    return true;
  }
  void probeAckd() {
    if (m_probeSize > m_activeMaxUDP) m_activeMaxUDP = m_probeSize;
    m_probeSize = 0;
    m_retryProbeSize = 0;
    m_probeAttemptSize = 0;
    m_probeAttempts = 0;
    m_pmtudState =
      m_activeMaxUDP >= ceiling() ? PMTUDState::SearchComplete : PMTUDState::Base;
    ++m_diag.probesAckd;
  }
  void probeLost() {
    if (!m_probeSize) return;
    unsigned size = m_probeSize;
    m_probeSize = 0;
    failProbe_(size);
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
    if (size <= m_activeMaxUDP && m_activeMaxUDP > MinUDPPayload) {
      blackhole();
      return false;
    }
    failProbe_(size);
    return false;
  }
  void blackhole() {
    m_activeMaxUDP = MinUDPPayload;
    m_probeSize = 0;
    m_retryProbeSize = 0;
    m_probeAttemptSize = 0;
    m_probeAttempts = 0;
    m_pmtudState = PMTUDState::Error;
    ++m_diag.blackholes;
  }

  void applyHint(PathHint hint) {
    if (!hint || hint.mtu < MinUDPPayload) return;
    if (hint.mtu < m_activeMaxUDP) m_activeMaxUDP = hint.mtu;
    if (hint.mtu < m_peerMaxUDP) m_peerMaxUDP = hint.mtu;
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
    clampActive_();
  }

private:
  void clampActive_() {
    unsigned c = ceiling();
    if (m_activeMaxUDP > c) m_activeMaxUDP = c;
    if (m_activeMaxUDP < MinUDPPayload) m_activeMaxUDP = MinUDPPayload;
  }
  void failProbe_(unsigned size) {
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
  bool		m_ecnDisabled = true;
  uint64_t	m_bytesRx = 0;
  uint64_t	m_bytesTx = 0;
  unsigned	m_activeMaxUDP = MinUDPPayload;
  unsigned	m_peerMaxUDP = BufSize;
  unsigned	m_configuredMaxUDP = BufSize;
  unsigned	m_probeSize = 0;
  unsigned	m_retryProbeSize = 0;
  unsigned	m_probeAttemptSize = 0;
  unsigned	m_probeAttempts = 0;
  unsigned	m_failureFloor = 0;
  PMTUDState::T m_pmtudState = PMTUDState::Base;
  PathDiag	m_diag;
};

} // namespace Zquic
