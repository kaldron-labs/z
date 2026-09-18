//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// persistent provider-browser session lifecycle

#ifndef zumd_session_HH
#define zumd_session_HH

#ifndef ZumLib_HH
#include <zlib/ZumLib.hh>
#endif

#include <zlib/zumd.hh>
#include <zlib/zumd_request.hh>

#include <zlib/ZtlsRandom.hh>

namespace Zum {

template <typename Heap> struct DBContext_;
using DBContext = DBContext_<
  ZmHeap<"Zum.zumd.db.context.DBContext", DBContext_<ZuVoid>>>;

namespace SessionError { enum { OK = -1, Invalid, Expired, Storage }; }

struct SessionConfig {
  String	issuer;
  String	subject;
  UserID	userID = 0;
  ProviderID	providerID = 0;
  int64_t	authTime = 0;
  int64_t	now = 0;
  int64_t	idleLifetime = 0;
  int64_t	absoluteLifetime = 0;
  uint64_t	authVersion = 0;
};

ZuDerive(SessionFn, (ZmFn<void(int, Session, String),
  ZmFnHeapID<"Zum.SessionFn">>));
ZuDerive(SessionDoneFn, (ZmFn<void(int),
  ZmFnHeapID<"Zum.SessionDoneFn">>));

ZumExtern bool sessionIssue(
  Requests *, ZuTime, DBContext *, Ztls::Random &, SessionConfig, SessionFn);
ZumExtern bool sessionIssueGrant(
  Requests *, ZuTime, DBContext *, Ztls::Random &, Bytes grantID,
  String issuer, int64_t now, int64_t idleLifetime, int64_t absoluteLifetime,
  SessionFn);
ZumExtern bool sessionUse(
  Requests *, ZuTime, DBContext *, String token, String issuer,
  int64_t now, int64_t idleLifetime, SessionFn);
ZumExtern bool sessionRevoke(
  Requests *, ZuTime, DBContext *, String token, String issuer,
  int64_t now, SessionDoneFn);

} // namespace Zum

#endif /* zumd_session_HH */
