//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZquicSock.hh>

#ifndef _WIN32
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#endif

namespace Zquic {

SockPlan Sock::plan(const SockConfig &config)
{
  SockPlan p;

#ifdef _WIN32
  p.noFragment = true;
  p.probeMode = config.probe;
  p.errorQueue = false;
  p.pmtuQuery = config.mode == PathMode::ClientConnected;
#ifdef IP_USER_MTU
  p.userMTU = true;
#endif
#ifdef IPV6_USER_MTU
  if (config.family == IPFamily::IPv6) p.userMTU = true;
#endif
#else
  p.noFragment = true;
  p.probeMode = config.probe;
  p.errorQueue = config.errorQueue;
  p.pmtuQuery = config.mode == PathMode::ClientConnected;
  p.userMTU = false;
#endif

  p.nOptions =
    (p.noFragment ? 1 : 0) +
    (p.errorQueue ? 1 : 0) +
    (p.probeMode ? 1 : 0) +
    (p.pmtuQuery ? 1 : 0) +
    (p.userMTU ? 1 : 0);
  return p;
}

static void diagAttempt_(SockDiag *diag)
{
  if (diag) ++diag->optionsAttempted;
}

static void diagApplied_(SockDiag *diag)
{
  if (diag) ++diag->optionsApplied;
}

static void diagUnsupported_(SockDiag *diag)
{
  if (diag) ++diag->optionsUnsupported;
}

static void diagError_(SockDiag *diag)
{
  if (diag) ++diag->optionErrors;
}

bool Sock::initUDP(Zi::Socket s, const SockConfig &config, SockDiag *diag)
{
  bool ok = true;
  if (Zi::nullSocket(s)) return false;

#ifdef _WIN32
  if (config.family == IPFamily::IPv4) {
#ifdef IP_DONTFRAGMENT
    BOOL yes = TRUE;
    diagAttempt_(diag);
    if (::setsockopt(s, IPPROTO_IP, IP_DONTFRAGMENT,
	  reinterpret_cast<const char *>(&yes), sizeof(yes)))
      diagError_(diag), ok = false;
    else
      diagApplied_(diag);
#else
    diagUnsupported_(diag);
#endif
#ifdef IP_MTU_DISCOVER
    DWORD mode = config.probe ? 2U : 1U;
    diagAttempt_(diag);
    if (::setsockopt(s, IPPROTO_IP, IP_MTU_DISCOVER,
	  reinterpret_cast<const char *>(&mode), sizeof(mode)))
      diagError_(diag);
    else
      diagApplied_(diag);
#else
    diagUnsupported_(diag);
#endif
  } else {
#ifdef IPV6_DONTFRAG
    BOOL yes = TRUE;
    diagAttempt_(diag);
    if (::setsockopt(s, IPPROTO_IPV6, IPV6_DONTFRAG,
	  reinterpret_cast<const char *>(&yes), sizeof(yes)))
      diagError_(diag), ok = false;
    else
      diagApplied_(diag);
#else
    diagUnsupported_(diag);
#endif
#ifdef IPV6_MTU_DISCOVER
    DWORD mode = config.probe ? 2U : 1U;
    diagAttempt_(diag);
    if (::setsockopt(s, IPPROTO_IPV6, IPV6_MTU_DISCOVER,
	  reinterpret_cast<const char *>(&mode), sizeof(mode)))
      diagError_(diag);
    else
      diagApplied_(diag);
#else
    diagUnsupported_(diag);
#endif
  }
#else
  if (config.family == IPFamily::IPv4) {
#ifdef IP_MTU_DISCOVER
    int mode =
#ifdef IP_PMTUDISC_PROBE
      config.probe ? IP_PMTUDISC_PROBE :
#endif
#ifdef IP_PMTUDISC_DO
      IP_PMTUDISC_DO
#else
      2
#endif
      ;
    diagAttempt_(diag);
    if (::setsockopt(s, IPPROTO_IP, IP_MTU_DISCOVER, &mode, sizeof(mode)) < 0)
      diagError_(diag), ok = false;
    else
      diagApplied_(diag);
#else
    diagUnsupported_(diag);
#endif
#ifdef IP_RECVERR
    if (config.errorQueue) {
      int yes = 1;
      diagAttempt_(diag);
      if (::setsockopt(s, IPPROTO_IP, IP_RECVERR, &yes, sizeof(yes)) < 0)
	diagError_(diag);
      else
	diagApplied_(diag);
    }
#else
    if (config.errorQueue) diagUnsupported_(diag);
#endif
  } else {
#ifdef IPV6_MTU_DISCOVER
    int mode =
#ifdef IPV6_PMTUDISC_PROBE
      config.probe ? IPV6_PMTUDISC_PROBE :
#endif
#ifdef IPV6_PMTUDISC_DO
      IPV6_PMTUDISC_DO
#else
      2
#endif
      ;
    diagAttempt_(diag);
    if (::setsockopt(
	  s, IPPROTO_IPV6, IPV6_MTU_DISCOVER, &mode, sizeof(mode)) < 0)
      diagError_(diag), ok = false;
    else
      diagApplied_(diag);
#else
    diagUnsupported_(diag);
#endif
#ifdef IPV6_RECVERR
    if (config.errorQueue) {
      int yes = 1;
      diagAttempt_(diag);
      if (::setsockopt(s, IPPROTO_IPV6, IPV6_RECVERR, &yes, sizeof(yes)) < 0)
	diagError_(diag);
      else
	diagApplied_(diag);
    }
#else
    if (config.errorQueue) diagUnsupported_(diag);
#endif
  }
#endif

  return ok;
}

PathHint Sock::pathHint(Zi::Socket s, const SockConfig &config, SockDiag *diag)
{
  if (diag) ++diag->mtuQueries;
  if (Zi::nullSocket(s) || config.mode != PathMode::ClientConnected) {
    if (diag) ++diag->mtuQueryErrors;
    return {};
  }

#ifdef _WIN32
  DWORD mtu = 0;
  int len = sizeof(mtu);
  int rc = -1;
  if (config.family == IPFamily::IPv4) {
#ifdef IP_MTU
    rc = ::getsockopt(s, IPPROTO_IP, IP_MTU,
      reinterpret_cast<char *>(&mtu), &len);
#endif
  } else {
#ifdef IPV6_MTU
    rc = ::getsockopt(s, IPPROTO_IPV6, IPV6_MTU,
      reinterpret_cast<char *>(&mtu), &len);
#endif
  }
  if (rc || mtu < MinUDPPayload) {
    if (diag) ++diag->mtuQueryErrors;
    return {};
  }
  return { PathHintKind::KernelMTU, unsigned(mtu), 0 };
#else
  int mtu = 0;
  socklen_t len = sizeof(mtu);
  int rc = -1;
  if (config.family == IPFamily::IPv4) {
#ifdef IP_MTU
    rc = ::getsockopt(s, IPPROTO_IP, IP_MTU, &mtu, &len);
#endif
  } else {
#ifdef IPV6_MTU
    rc = ::getsockopt(s, IPPROTO_IPV6, IPV6_MTU, &mtu, &len);
#endif
  }
  if (rc < 0 || mtu < int(MinUDPPayload)) {
    if (diag) ++diag->mtuQueryErrors;
    return {};
  }
  return { PathHintKind::KernelMTU, unsigned(mtu), 0 };
#endif
}

} // namespace Zquic
