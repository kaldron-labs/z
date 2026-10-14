//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuBase64URL.hh>
#include <zlib/ZuMatcher.hh>

#include <string.h>

#include <zlib/ZmBlock.hh>
#include <zlib/ZmSemaphore.hh>

#include <zlib/ZfCf.hh>

#include "../../zdb/test/ZdbTestCf.hh"
#include <zlib/ZiLog.hh>

#include <zlib/ZvMxParams.hh>

#include <zlib/ZdbMemStore.hh>

#include <zlib/zumd_admin.hh>
#include <zlib/zumd_authorize.hh>
#include <zlib/zumd_db.hh>
#include <zlib/zumd_db_ops.hh>
#include <zlib/zumd_key_db.hh>
#include <zlib/zumd_discovery.hh>
#include <zlib/zumd_http.hh>
#include <zlib/zumd_jwt.hh>
#include <zlib/zumd_oauth.hh>
#include <zlib/zumd_oidc.hh>
#include <zlib/zumd_passkey.hh>
#include <zlib/zumd_request.hh>
#include <zlib/zumd_session.hh>
#include <zlib/zumd_token.hh>
#include <zlib/zumd_revoke.hh>
#include <zlib/zumd_webauthn.hh>

#include <zlib/ZtlsMD.hh>
#include <zlib/ZtlsCOSE.hh>
#include <zlib/ZtlsPK.hh>
#include <zlib/ZtlsRandom.hh>
#include <zlib/ZtlsSec.hh>

#include "ZumTestSupport.hh"

using namespace ZuTestUtil;
using namespace zumtest;


static void webAuthn()
{
  ZuTestScope(webAuthn);
  Ztls::Random rng;
  ZuCheck(rng.init());
  Ztls::PK::SK_EC sk{rng, Ztls::PK::OIDs::EC_GRP_SECP256R1};
  uint8_t publicKey[Ztls::COSE::ES256::PublicKeySize];
  ZuCheck(Ztls::Backend::pkey_ec_export_public(sk.key, publicKey));

  Zum::AssertionInput input;
  ZuCheck(makeAssertion(rng, sk, 6, input));

  Zum::AssertionState state{
    .challenge = ZuBSpan{"challenge"},
    .origin = ZuCSpan{"https://example.com"},
    .rpID = ZuCSpan{"example.com"},
    .publicKey = publicKey,
    .userHandle = ZuBSpan{"handle"},
    .signCount = 5
  };
  Zum::AssertionResult result;
  ZuCheck(Zum::verifyAssertion(input, state, result) ==
      Zum::WebAuthnError::OK);
  ZuCheck(result.signCount == 6 &&
    result.counter == Zum::CounterState::Advanced && !result.backedUp);

  input.clientDataJSON =
    Zum::Bytes{ZuBSpan{
      assertionClientData_, sizeof(assertionClientData_) - 1}};
  state.origin = "https://other.example";
  ZuCheck(Zum::verifyAssertion(input, state, result) ==
      Zum::WebAuthnError::Origin);
  input.clientDataJSON =
    Zum::Bytes{ZuBSpan{
      assertionClientData_, sizeof(assertionClientData_) - 1}};
  state.origin = "https://example.com";
  state.signCount = 6;
  ZuCheck(Zum::verifyAssertion(input, state, result) ==
      Zum::WebAuthnError::Counter);
  ZuCheck(makeAssertion(rng, sk, 6, input, true));
  state.backupEligible = true;
  ZuCheck(Zum::verifyAssertion(input, state, result) ==
      Zum::WebAuthnError::OK);
  ZuCheck(result.counter == Zum::CounterState::Regression);

  Zum::RegistrationState registrationState{
    .challenge = ZuBSpan{"challenge"},
    .origin = ZuCSpan{"https://example.com"},
    .rpID = ZuCSpan{"example.com"}
  };
  Zum::RegistrationInput registrationInput;
  ZuCheck(makeRegistration(publicKey, 0, "credential",
    ZuBSpan{"challenge"},
    "https://example.com", "example.com", registrationInput));
  Zum::RegistrationResult registration;
  ZuCheck(Zum::verifyRegistration(registrationInput,
    registrationState, 128,
    registration) == Zum::WebAuthnError::OK);
  ZuCheck(registration.credentialID == ZuBSpan{"credential"} &&
    registration.publicKey == ZuBSpan{publicKey} &&
    !registration.signCount && !registration.backupEligible &&
    !registration.backedUp);
}
static void webAuthnOptions()
{
  ZuTestScope(webAuthnOptions);
  Ztls::Random rng;
  ZuCheck(rng.init());
  Zum::Bytes challenge;
  ZuCheck(Zum::webAuthnChallenge(rng, challenge));
  ZuCheck(challenge.length() == Zum::WebAuthnChallengeSize);

  uint8_t fixed[] = {0, 1, 2, 3};
  ZuCheck(Zum::assertionOptions(fixed, "login.example", 60000) ==
    "{\"publicKey\":{\"challenge\":\"AAECAw\","
    "\"rpId\":\"login.example\",\"timeout\":60000,"
    "\"userVerification\":\"required\"}}");
  uint8_t handle[] = {0xfe, 0xff};
  ZuCheck(Zum::registrationOptions(fixed, "login.example", "Example",
      handle, "a\\\"b", "A B", 60000) ==
    "{\"publicKey\":{\"challenge\":\"AAECAw\","
    "\"rp\":{\"id\":\"login.example\",\"name\":\"Example\"},"
    "\"user\":{\"id\":\"_v8\",\"name\":\"a\\\\\\\"b\","
    "\"displayName\":\"A B\"},\"pubKeyCredParams\":[{"
    "\"type\":\"public-key\",\"alg\":-7}],\"timeout\":60000,"
    "\"authenticatorSelection\":{\"residentKey\":\"required\","
    "\"requireResidentKey\":true,\"userVerification\":\"required\"},"
    "\"attestation\":\"none\"}}");
}
static void webAuthnInput()
{
  ZuTestScope(webAuthnInput);
  Zum::WebAuthnInputLimits limits;
  char assertion[] =
    "{\"type\":\"public-key\",\"rawId\":\"Y3JlZA\",\"response\":{"
    "\"clientDataJSON\":\"e30\",\"authenticatorData\":\"AA\","
    "\"signature\":\"AQ\",\"userHandle\":\"aGFuZGxl\"}}";
  Zum::AssertionInput assertionInput;
  ZuCheck(Zum::parseAssertion(
    {assertion, sizeof(assertion) - 1}, limits,
    assertionInput) == Zum::WebAuthnError::OK);
  ZuCheck(assertionInput.credentialID == ZuBSpan{"cred"} &&
    assertionInput.clientDataJSON == ZuBSpan{"{}"} &&
    assertionInput.authenticatorData == ZuBSpan{uint8_t{0}} &&
    assertionInput.signature == ZuBSpan{uint8_t{1}} &&
    assertionInput.userHandle == ZuBSpan{"handle"});

  char registration[] =
    "{\"type\":\"public-key\",\"rawId\":\"Y3JlZA\",\"response\":{"
    "\"clientDataJSON\":\"e30\",\"attestationObject\":\"Ag\"}}";
  Zum::RegistrationInput registrationInput;
  ZuCheck(Zum::parseRegistration(
    {registration, sizeof(registration) - 1}, limits,
    registrationInput) == Zum::WebAuthnError::OK);
  ZuCheck(registrationInput.credentialID == ZuBSpan{"cred"} &&
    registrationInput.clientDataJSON == ZuBSpan{"{}"} &&
    registrationInput.attestationObject == ZuBSpan{uint8_t{2}});

  char nonCanonical[] =
    "{\"type\":\"public-key\",\"rawId\":\"Y3JlZB\",\"response\":{"
    "\"clientDataJSON\":\"e30\",\"attestationObject\":\"Ag\"}}";
  ZuCheck(Zum::parseRegistration(
    {nonCanonical, sizeof(nonCanonical) - 1}, limits,
    registrationInput) == Zum::WebAuthnError::OK);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(webAuthn);
  ZuTestCall(webAuthnOptions);
  ZuTestCall(webAuthnInput);
  return 0;
}
