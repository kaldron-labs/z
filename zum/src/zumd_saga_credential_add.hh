//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef zumd_saga_credential_add_HH
#define zumd_saga_credential_add_HH

#include <zlib/zumd_saga.hh>
#include <zlib/zumd.hh>
#include <zlib/zumd_webauthn.hh>
#include <zlib/zumd_identity_db.hh>

namespace Zum {

struct CredentialAddArgs {
  Bytes		ceremonyID;
  String	issuer;
  UserID	userID = 0;
  Bytes		userHandle;
  Bytes		credentialID;
  Bytes		publicKey;
  uint32_t	signCount = 0;
  int64_t	created = 0;
  bool		backupEligible = false;
  bool		backedUp = false;
  String	label;
  uint64_t	userVersion = 1;
  Grant		beforeGrant;
};

Zdb_::SagaDef credentialAddDef();
SagaRef credentialAdd(ZuBSpan);
SagaRef credentialAdd(CredentialAddArgs);

int credentialPrepare(
  const Grant &, ZuBSpan bindingDigest, RegistrationInput &,
  ZuCSpan origin, ZuCSpan rpID,
  unsigned credentialIDMax,
  int64_t now, CredentialAddArgs &);

} // namespace Zum

#endif /* zumd_saga_credential_add_HH */
