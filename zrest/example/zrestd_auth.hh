//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef zrestd_auth_HH
#define zrestd_auth_HH

#include <zlib/ZtEnum.hh>

#include "zrestauth_srv.hh"
#include "zrestjwt.hh"

class App;

struct AuthParser : public AuthParser_<AuthParser> {
  App *app = nullptr;

  template <typename Link> void complete(Link *, bool);
};

struct RefreshParser : public RefreshParser_<RefreshParser> {
  App *app = nullptr;

  template <typename Link> void complete(Link *, bool);
};

enum {
  ReqBodyMax = 1U<<20,
  AccessLifetimeDefault = 5 * 60,
  RefreshLifetimeDefault = 24 * 60 * 60
};

using ServerDefaultUser = ZuStringT<"test">;
using ServerDefaultPass = ZuStringT<"test123">;
using DefaultJWTSecret = ZuStringT<"your_secret_key">;
using DefaultAccessLifetime = ZuStringT<"5m">;
using DefaultRefreshLifetime = ZuStringT<"24h">;

ZtEnumNS(, Http2Mode, int8_t, force, prefer, disable);

struct Options {
  ZuCSpan	addr{"0.0.0.0"};
  unsigned	port = 8080;
  ZuCSpan	cert;
  ZuCSpan	key;
  ZuCSpan	keyLog;
  ZuCSpan	logPath{"-"};
  ZuCSpan	pidfile;
  int		eventFD = -1;
  ZuCSpan	user{ServerDefaultUser{}()};
  ZuCSpan	pass{ServerDefaultPass{}()};
  ZuCSpan	jwtSecret{DefaultJWTSecret{}()};
  ZuCSpan	accessTokenLifetime{DefaultAccessLifetime{}()};
  ZuCSpan	refreshTokenLifetime{DefaultRefreshLifetime{}()};
  uint64_t	accessSecs = AccessLifetimeDefault;
  uint64_t	refreshSecs = RefreshLifetimeDefault;
  unsigned	requests = 1;
  unsigned	maxconn = 0;
  unsigned	timeout = 30;
  bool		ipv6 = false;
  bool		daemon = false;
  bool		syslog = false;
  bool		noKeepalive = false;
  bool		noServerID = false;
  bool		http = true;
  bool		https = false;
  bool		http3 = false;
  bool		verbose = false;
  Http2Mode::T	http2 = Http2Mode::prefer;
  uint32_t	quicHeartbeat = 0;
#ifdef ZiMultiplex_DEBUG
  bool		debug = false;
  bool		frag = false;
  bool		yield = false;
#endif
#ifdef ZiMultiplex_FILTER
  ZuCSpan	quicRxDrop;
  ZuCSpan	quicTxDrop;
#endif
#ifdef Zquic_DEBUG
  uint32_t	quicDiag = 0;
#endif
  uint32_t	memDiag = 0;
  bool		help = false;
};

namespace AuthResult {
  enum { Failed, Unauthorized, OK, InternalError };
}

#endif /* zrestd_auth_HH */
