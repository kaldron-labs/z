//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// IP address

#include <zlib/ZiIP.hh>

namespace {

bool ZiIP_pton4(const Zi::Hostname &host, in_addr &addr)
{
#ifndef _WIN32
  return ::inet_pton(AF_INET, host.data(), &addr) == 1;
#else
  if (::InetPtonW(AF_INET, host.data(), &addr) == 1) return true;
  sockaddr_in sa;
  int len = sizeof(sa);
  memset(&sa, 0, sizeof(sa));
  sa.sin_family = AF_INET;
  if (::WSAStringToAddressW(
	const_cast<wchar_t *>(host.data()), AF_INET, 0,
	reinterpret_cast<sockaddr *>(&sa), &len))
    return false;
  addr = sa.sin_addr;
  return true;
#endif
}

bool ZiIP_pton6(const Zi::Hostname &host, in6_addr &addr)
{
#ifndef _WIN32
  return ::inet_pton(AF_INET6, host.data(), &addr) == 1;
#else
  if (::InetPtonW(AF_INET6, host.data(), &addr) == 1) return true;
  sockaddr_in6 sa;
  int len = sizeof(sa);
  memset(&sa, 0, sizeof(sa));
  sa.sin6_family = AF_INET6;
  if (::WSAStringToAddressW(
	const_cast<wchar_t *>(host.data()), AF_INET6, 0,
	reinterpret_cast<sockaddr *>(&sa), &len))
    return false;
  addr = sa.sin6_addr;
  return true;
#endif
}

} // namespace

int ZiIP::resolve_(Zi::Hostname host, ZeError *e)
{
  in_addr v4;
  if (ZiIP_pton4(host, v4)) {
    *this = v4;
    return Zi::OK;
  }

  in6_addr v6;
  if (ZiIP_pton6(host, v6)) {
    *this = v6;
    return Zi::OK;
  }

  return ZiResolver::resolve(ZuMv(host), ZmFn<bool(ZiIP)>{[this](ZiIP ip) {
    *this = ip;
    return false;
  }}, e);
}
