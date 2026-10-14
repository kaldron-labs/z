//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef zumd_saga_code_family_HH
#define zumd_saga_code_family_HH

#include <zlib/zumd_saga.hh>
#include <zlib/zumd.hh>
#include <zlib/zumd_oauth.hh>
#include <zlib/zumd_identity_db.hh>
#include <zlib/ZtlsRandom.hh>

namespace Zum {

struct CodeFamilyArgs {
  Bytes		codeID;
  Bytes		codeDigest;
  Bytes		familyID;
  String	issuer;
  AppID		appID = 0;
  UserID	userID = 0;
  String	clientID;
  Bytes		credentialID;
  String	audience;
  IDVec		requestedRoleIDs;
  IDVec		roleIDs;
  ZtBitmap	actions;
  Bytes		digest;
  uint64_t	authVersion = 0;
  uint64_t	userVersion = 1;
  int64_t	authTime = 0;
  int64_t	created = 0;
  int64_t	expires = 0;
  String	scope;
  String	nonce;
  ProviderID	authorityProviderID = 0;
  uint64_t	policyVersion = 0;
  uint64_t	evidenceVersion = 0;
  UserSource::T authoritySource = UserSource::Local;

  Grant		beforeGrant;
  uint64_t	clientVersion = 0;
  uint64_t	assignmentVersion = 0;
};

Zdb_::SagaDef codeFamilyDef();
SagaRef codeFamily(ZuBSpan);
SagaRef codeFamily(CodeFamilyArgs);

bool codeFamilyPrepare(
  Ztls::Random &, const Grant &, ZuBSpan codeDigest,
  String scope, IDVec requestedRoleIDs, ZtBitmap actions, uint64_t authVersion,
  int64_t now, int64_t expires, CodeFamilyArgs &, String &refreshToken);

} // namespace Zum

#endif /* zumd_saga_code_family_HH */
