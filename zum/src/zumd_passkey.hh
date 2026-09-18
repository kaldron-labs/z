//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// persistent browser passkey enrollment

#ifndef zumd_passkey_HH
#define zumd_passkey_HH

#ifndef ZumLib_HH
#include <zlib/ZumLib.hh>
#endif

#include <zlib/zumd.hh>
#include <zlib/zumd_webauthn.hh>
#include <zlib/zumd_request.hh>

namespace Zum {

struct DB;
template <typename Heap> struct DBContext_;
using DBContext = DBContext_<
  ZmHeap<"Zum.zumd.db.context.DBContext", DBContext_<ZuVoid>>>;

struct EnrollmentBeginConfig {
  String	issuer;
  String	rpID;
  String	rpName;
  String	name;
  String	displayName;
  String	label;
  UserID	userID = 0;
  int64_t	now = 0;
  int64_t	expires = 0;
  uint64_t	timeout = 0;
};

struct EnrollmentBeginResult {
  Bytes		ceremonyID;
  String	options;
};

struct BootstrapConfig {
  String	issuer;
  AppID		appID = 0;
  String	userName;
  String	label;
  UserID	userID = 0;
  int64_t	now = 0;
  int64_t	expires = 0;
};

struct RecoveryIssueConfig {
  String	issuer;
  String	actor;
  UserID	userID = 0;
  int64_t	now = 0;
  int64_t	expires = 0;
  uint64_t	version = 0;
  IdemRequest request;
};

struct RecoveryBeginConfig {
  String	issuer;
  String	rpID;
  String	rpName;
  String	displayName;
  String	label;
  int64_t	now = 0;
  int64_t	expires = 0;
  uint64_t	timeout = 0;
};

struct EnrollmentFinishConfig {
  String		origin;
  String		rpID;
  unsigned		credentialIDMax = 0;
  int64_t		now = 0;
};

struct CredentialBeginConfig {
  String	issuer;
  String	rpID;
  String	rpName;
  String	displayName;
  String	label;
  UserID	userID = 0;
  int64_t	now = 0;
  int64_t	expires = 0;
  uint64_t	timeout = 0;
};

ZuDerive(EnrollmentBeginFn, (ZmFn<void(int, EnrollmentBeginResult),
  ZmFnHeapID<"Zum.EnrollmentBeginFn">>));
ZuDerive(EnrollmentFinishFn, (ZmFn<void(int),
  ZmFnHeapID<"Zum.EnrollmentFinishFn">>));
ZuDerive(CapabilityFn, (ZmFn<void(bool, String),
  ZmFnHeapID<"Zum.CapabilityFn">>));
using BootstrapFn = CapabilityFn;
using RecoveryIssueFn = CapabilityFn;

ZumExtern bool bootstrapIssue(
  Requests *, ZuTime deadline, DBContext *, Ztls::Random &,
  BootstrapConfig, BootstrapFn);
ZumExtern bool recoveryIssue(
  Requests *, ZuTime deadline, DB *, DBContext *, Ztls::Random &,
  RecoveryIssueConfig, RecoveryIssueFn);
ZumExtern bool recoveryBegin(
  Requests *, ZuTime deadline, DBContext *, Ztls::Random &,
  String capability, Bytes bindingDigest, RecoveryBeginConfig,
  EnrollmentBeginFn);
ZumExtern bool recoveryFinish(
  Requests *, ZuTime deadline, DB *, DBContext *, Bytes ceremonyID,
  Bytes bindingDigest, RegistrationInput, EnrollmentFinishConfig,
  EnrollmentFinishFn);
ZumExtern bool enrollmentBegin(
  Requests *, ZuTime deadline, DBContext *, Ztls::Random &,
  Bytes bindingDigest, EnrollmentBeginConfig, EnrollmentBeginFn);
ZumExtern bool bootstrapBegin(
  Requests *, ZuTime deadline, DBContext *, Ztls::Random &,
  String capability, Bytes bindingDigest, EnrollmentBeginConfig,
  EnrollmentBeginFn);
ZumExtern bool enrollmentFinish(
  Requests *, ZuTime deadline, DB *, DBContext *, Bytes ceremonyID,
  Bytes bindingDigest, RegistrationInput, EnrollmentFinishConfig,
  EnrollmentFinishFn);
ZumExtern bool credentialBegin(
  Requests *, ZuTime deadline, DBContext *, Ztls::Random &,
  Bytes bindingDigest, CredentialBeginConfig, EnrollmentBeginFn);
ZumExtern bool credentialFinish(
  Requests *, ZuTime deadline, DB *, DBContext *, Bytes ceremonyID,
  Bytes bindingDigest, RegistrationInput, EnrollmentFinishConfig,
  EnrollmentFinishFn);

} // namespace Zum

#endif /* zumd_passkey_HH */
