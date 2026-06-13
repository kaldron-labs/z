//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// DNS and host resolver

#ifndef ZiResolver_HH
#define ZiResolver_HH

#ifndef ZiLib_HH
#include <zlib/ZiLib.hh>
#endif

#include <zlib/ZmFn.hh>

#include <zlib/ZePlatform.hh>

#include <zlib/ZiPlatform.hh>
#include <zlib/ZiIP.hh>

namespace ZiResolver {

using Host = Zi::Hostname;

enum {
  H3MaxIPs = 8,
  H3MaxALPN = 8,
  H3AliasDepth = 4,
  DNSMsgMax = 4096
};

struct HTTPS {
  uint16_t	priority = 0;
  Host		target;
  uint16_t	port = 0;
  ZiIP		ipv4Hint[H3MaxIPs];
  uint8_t	nIPv4Hint = 0;
  bool		noDefaultALPN = false;
  bool		hasALPN = false;
  bool		hasIPv4Hint = false;
  bool		unknownMandatory = false;
  bool		hasH3 = false;

  bool alias() const { return !priority; }
};

struct H3Endpoint {
  Host		dnsHost;
  Host		tlsHost;
  ZiIP		ip;
  uint16_t	port = 443;
  bool		fromHTTPS = false;
  bool		fromIPv4Hint = false;
};

enum class H3Policy : int8_t {
  DNSOnly,
  DNSWithBlindFallback
};

ZiExtern int resolve(Host host, ZmFn<bool(ZiIP)> fn, ZeError *e = nullptr);
ZiExtern Host name(ZiIP ip, ZeError *e = nullptr);

ZiExtern int parse(
  ZuBSpan msg, Host owner, ZmFn<bool(const HTTPS &)> fn,
  ZeError *e = nullptr);
ZiExtern int https(
  Host host, ZmFn<bool(const HTTPS &)> fn, ZeError *e = nullptr);
ZiExtern int http3(
  Host dnsHost, Host tlsHost, uint16_t port, H3Policy policy,
  ZmFn<bool(const H3Endpoint &)> fn, ZeError *e = nullptr);

} // ZiResolver

#endif /* ZiResolver_HH */
