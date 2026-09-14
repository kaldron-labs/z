//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// WebAuthn ES256 assertion verification

#ifndef zumd_webauthn_HH
#define zumd_webauthn_HH

#ifndef ZumLib_HH
#include <zlib/ZumLib.hh>
#endif

#include <zlib/ZuSpan.hh>

#include <zlib/ZfCBOR.hh>

#include <zlib/zumd.hh>

namespace Ztls { class Random; }

namespace Zum {

enum { WebAuthnChallengeSize = 32 };

ZumExtern bool webAuthnChallenge(Ztls::Random &, Bytes &);
ZumExtern String assertionOptions(
  ZuBSpan challenge, ZuCSpan rpID, uint64_t timeoutMS);
ZumExtern String registrationOptions(
  ZuBSpan challenge, ZuCSpan rpID, ZuCSpan rpName,
  ZuBSpan userHandle, ZuCSpan userName, ZuCSpan displayName,
  uint64_t timeoutMS);

namespace WebAuthnError {
  enum {
    OK = 0,
    JSON,
    Fields,
    Type,
    Challenge,
    Origin,
    AuthData,
    RPID,
    Flags,
    Signature,
    Counter,
    Attestation,
    Credential,
    PublicKey,
    Ceremony,
    Storage
  };
}

namespace CounterState {
  enum { None, Advanced, Regression };
}

struct AssertionState {
  ZuBSpan	challenge;
  ZuCSpan	origin;
  ZuCSpan	rpID;
  ZuBSpan	publicKey;
  ZuBSpan	userHandle;
  uint32_t	signCount = 0;
  bool		backupEligible = false;
};

struct AssertionResult {
  uint32_t	signCount = 0;
  int		counter = CounterState::None;
  bool		backedUp = false;
};

struct RegistrationState {
  ZuBSpan	challenge;
  ZuCSpan	origin;
  ZuCSpan	rpID;
};

struct RegistrationResult {
  Bytes		credentialID;
  Bytes		publicKey;
  uint32_t	signCount = 0;
  bool		backupEligible = false;
  bool		backedUp = false;
};

struct AssertionInput {
  Bytes		credentialID;
  Bytes		clientDataJSON;
  Bytes		authenticatorData;
  Bytes		signature;
  Bytes		userHandle;
};

struct RegistrationInput {
  Bytes		credentialID;
  Bytes		clientDataJSON;
  Bytes		attestationObject;
};

struct WebAuthnInputLimits {
  // Tunable allocation bounds for browser-controlled credential payloads.
  unsigned	credentialID = 1024;
  unsigned	clientDataJSON = 4096;
  unsigned	authenticatorData = 4096;
  unsigned	signature = 1024;
  unsigned	userHandle = 1024;
  unsigned	attestationObject = 64U<<10;
};

ZumExtern int parseAssertion(
  ZuSpan<char>, const WebAuthnInputLimits &, AssertionInput &);
ZumExtern int parseRegistration(
  ZuSpan<char>, const WebAuthnInputLimits &, RegistrationInput &);

ZumExtern int verifyAssertion(
  AssertionInput &, const AssertionState &, AssertionResult &);
ZumExtern int verifyRegistration(
  RegistrationInput &, const RegistrationState &,
  unsigned credentialIDMax,
  RegistrationResult &);

} // namespace Zum

#endif /* zumd_webauthn_HH */
