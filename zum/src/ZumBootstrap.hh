//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// resumable empty-database bootstrap for zumd

#ifndef ZumBootstrap_HH
#define ZumBootstrap_HH

#ifndef ZumLib_HH
#include <zlib/ZumLib.hh>
#endif

#include <zlib/ZuDerive.hh>

#include <zlib/ZmFn.hh>

#include <zlib/Zum.hh>
#include <zlib/ZumRequest.hh>
#include <zlib/ZumSecret.hh>

#include <zlib/ZtlsRandom.hh>

namespace Zum {

struct DB;
struct DBContext;

struct ServerBootstrapConfig {
  String	issuer;
  String	admin;
  String	output;
  String	adminClientID{"zum-admin"};
  String	adminRedirect{"http://127.0.0.1/callback"};
  Bytes		dbKey;
  int64_t	now = 0;
  uint32_t	ttl = 900;
  bool		reissue = false;
};

struct ServerBootstrapResult {
  BootstrapPhase::T phase = BootstrapPhase::Empty;
  AppID		coreAppID = 0;
  UserID	adminUserID = 0;
  String	adminClientID;
  bool		initialized = false;
  bool		capabilityWritten = false;
};

ZuDerive(ServerBootstrapFn, (ZmFn<void(bool, ServerBootstrapResult),
  ZmFnHeapID<"Zum.ServerBootstrapFn">>));

void serverBootstrap(
  DB *, Requests *, DBContext *, Ztls::Random &,
  ServerBootstrapConfig, ServerBootstrapFn);

} // namespace Zum

#endif /* ZumBootstrap_HH */
