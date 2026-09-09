//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// typed Zum administration operations

#ifndef ZumAdmin_HH
#define ZumAdmin_HH

#ifndef ZumLib_HH
#include <zlib/ZumLib.hh>
#endif

#include <zlib/ZumDB.hh>
#include <zlib/ZumRequest.hh>

namespace Zum {

namespace AdminError {
  enum { OK = 0, Invalid, Storage };
}

ZuDerive(AdminFn, (ZmFn<void(int), ZmFnHeapID<"Zum.AdminFn">>));
ZuDerive(ActionFn,
  (ZmFn<void(int, ActionID), ZmFnHeapID<"Zum.ActionFn">>));
ZuDerive(CleanupFn, (ZmFn<void(int, unsigned),
  ZmFnHeapID<"Zum.CleanupFn">>));

ZumExtern String auditID(ZuBSpan);
ZumExtern void auditWrite(DBContext *, Audit, AdminFn);
ZumExtern bool actionAdd(
  Requests *, ZuTime deadline, DBContext *, String issuer,
  String actor, String name, int64_t now, ActionFn);
ZumExtern bool actionState(
  Requests *, ZuTime deadline, DB *, DBContext *, Ztls::Random &,
  String issuer, String actor, ActionID, State::T, int64_t now, AdminFn);
ZumExtern bool grantRevoke(
  Requests *, ZuTime deadline, DBContext *, String issuer, Bytes id,
  String actor, int64_t now, AdminFn);
ZumExtern bool signKeyAdd(
  Requests *, ZuTime deadline, DBContext *, String issuer,
  String actor, SignKey, int64_t now, AdminFn);
ZumExtern bool signKeyRetire(
  Requests *, ZuTime deadline, DBContext *, String issuer, String actor,
  String id, int64_t retireAfter, int64_t now, AdminFn);
ZumExtern bool userRoles(
  Requests *, ZuTime deadline, DB *, DBContext *, Ztls::Random &,
  String issuer, String actor, UserID, IDVec, int64_t now, AdminFn);
ZumExtern bool userState(
  Requests *, ZuTime deadline, DB *, DBContext *, Ztls::Random &,
  String issuer, String actor, UserID, State::T, int64_t now, AdminFn);
ZumExtern bool roleActions(
  Requests *, ZuTime deadline, DB *, DBContext *, Ztls::Random &,
  String issuer, String actor, RoleID, ZtBitmap, int64_t now, AdminFn);
ZumExtern bool roleState(
  Requests *, ZuTime deadline, DB *, DBContext *, Ztls::Random &,
  String issuer, String actor, RoleID, State::T, int64_t now, AdminFn);
ZumExtern bool credentialState(
  Requests *, ZuTime deadline, DB *, DBContext *, Ztls::Random &,
  String issuer, String actor, Bytes credentialID, State::T,
  int64_t now, AdminFn);
ZumExtern bool scopeRoles(
  Requests *, ZuTime deadline, DB *, DBContext *, Ztls::Random &,
  String issuer, String actor, ScopeID, IDVec, int64_t now, AdminFn);
ZumExtern bool scopeState(
  Requests *, ZuTime deadline, DB *, DBContext *, Ztls::Random &,
  String issuer, String actor, ScopeID, State::T, int64_t now, AdminFn);
ZumExtern bool clientRoles(
  Requests *, ZuTime deadline, DB *, DBContext *, Ztls::Random &,
  String issuer, String actor, String clientID, IDVec, int64_t now,
  AdminFn);
ZumExtern bool clientState(
  Requests *, ZuTime deadline, DB *, DBContext *, Ztls::Random &,
  String issuer, String actor, String clientID, State::T,
  int64_t now, AdminFn);
ZumExtern bool clientSecretDigest(
  Requests *, ZuTime deadline, DB *, DBContext *, Ztls::Random &,
  String issuer, String actor, String clientID, Bytes secretDigest,
  int64_t now, AdminFn);
ZumExtern bool grantCleanup(
  Requests *, ZuTime deadline, DBContext *, int64_t now,
  unsigned limit, CleanupFn);
ZumExtern bool auditCleanup(
  Requests *, ZuTime deadline, DBContext *, int64_t before,
  unsigned limit, CleanupFn);

} // namespace Zum

#endif /* ZumAdmin_HH */
