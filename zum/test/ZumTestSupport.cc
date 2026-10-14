//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include "ZumTestSupport.hh"

#include <zlib/ZuBase64URL.hh>
#include <zlib/ZuMatcher.hh>
#include <zlib/ZfJSON.hh>
#include <zlib/zumd_oauth.hh>
#include <zlib/ZtlsMD.hh>
#include <zlib/ZtlsCOSE.hh>

#include <string.h>

namespace zumtest {

Zum::String testJwk(ZuCSpan id)
{
  // Public P-256 generator point; no test private material is published.
  Zum::String json{"{\"kty\":\"EC\",\"crv\":\"P-256\",\"kid\":\""};
  json << id << "\",\"x\":\"axfR8uEsQkf4vOblY6RA8ncDfYEt6zOg9KE5RdiYwpY\","
    "\"y\":\"T-NC4v4af5uO5-tKfA-eFivOM1drMV7Oy7ZAaDe_UfU\"}";
  return json;
}

static bool jwtPart(Zum::String &token, ZuBSpan value)
{
  auto offset = token.length();
  auto length = ZuBase64URL::enclen(value.length());
  token.length(offset + length);
  return ZuBase64URL::encode(token.span().offset(offset), value) == length;
}

Zum::String base64URL(ZuBSpan value)
{
  Zum::String encoded;
  encoded.length(ZuBase64URL::enclen(value.length()));
  encoded.length(ZuBase64URL::encode(encoded.span(), value));
  return encoded;
}

bool oidcParams(
    Zum::String location, Zum::String &state, Zum::String &nonce)
{
  struct IDs { using Keys = ZuStringTL<"state", "nonce">; };
  static constexpr auto matcher = ZuMatcher<IDs>();
  int offset = location.find<"?">();
  if (offset < 0) return false;
  location.splice(0, unsigned(offset + 1));
  uint8_t lookup[2]{};
  Zum::formEach({location.data(), location.length()},
    [&lookup, &state, &nonce](ZuCSpan name, ZuCSpan value) {
      int id = matcher.exact(name);
      if (id < 0) return;
      lookup[id] = 1;
      switch (id) {
        case 0: state = value; break;
        case 1: nonce = value; break;
      }
    });
  return lookup[0] && lookup[1] && state && nonce;
}

bool signJWT(
    Ztls::Random &rng, Ztls::PK::SK_EC &key,
    ZuCSpan header, ZuCSpan claims, Zum::String &token)
{
  Zum::String next;
  if (!jwtPart(next, header)) return false;
  next << '.';
  if (!jwtPart(next, claims)) return false;
  uint8_t digest[Ztls::MD<>::Size];
  Ztls::MD<> md;
  md.update(ZuBSpan{next});
  md.finish(digest);
  Zum::Bytes der;
  auto result = key.sign(rng, digest, [&der](ZuBSpan value) {
    der = Zum::Bytes{value};
  });
  uint8_t raw[Ztls::COSE::ES256::SignatureSize];
  if (result.template is<ZeException>() ||
      !Ztls::COSE::ES256::derToRaw(der, raw)) return false;
  next << '.';
  if (!jwtPart(next, raw))
    return false;
  token = ZuMv(next);
  return true;
}

// Fixed WebAuthn assertion layout: SHA-256 RP hash, flags, and counter.
enum { TestAuthDataSize = Ztls::MD<>::Size + 1 + 4 };

bool makeAssertion(
    Ztls::Random &rng, Ztls::PK::SK_EC &sk, uint32_t signCount,
    Zum::AssertionInput &input, bool backupEligible)
{
  uint8_t authData[TestAuthDataSize];
  {
    Ztls::MD<> md;
    md.update(ZuBSpan{"example.com"});
    md.finish(authData);
  }
  authData[32] = 5 | (backupEligible ? 8 : 0);
  authData[33] = signCount>>24;
  authData[34] = signCount>>16;
  authData[35] = signCount>>8;
  authData[36] = signCount;
  uint8_t clientHash[Ztls::MD<>::Size];
  {
    Ztls::MD<> md;
    md.update(ZuBSpan{
      assertionClientData_, sizeof(assertionClientData_) - 1});
    md.finish(clientHash);
  }
  uint8_t signedHash[Ztls::MD<>::Size];
  {
    Ztls::MD<> md;
    md.update(authData);
    md.update(clientHash);
    md.finish(signedHash);
  }
  Zum::Bytes signature;
  auto signed_ = sk.sign(rng, signedHash, [&signature](ZuBSpan der) {
    signature = Zum::Bytes{der};
  });
  if (signed_.template is<ZeException>() || !signature) return false;
  input = Zum::AssertionInput{
    .credentialID = Zum::Bytes{ZuBSpan{"credential"}},
    .clientDataJSON = Zum::Bytes{ZuBSpan{
      assertionClientData_, sizeof(assertionClientData_) - 1}},
    .authenticatorData = Zum::Bytes{ZuBSpan{authData}},
    .signature = ZuMv(signature),
    .userHandle = Zum::Bytes{ZuBSpan{"handle"}}
  };
  return true;
}

bool makeAssertion(
    Ztls::Random &rng, Ztls::PK::SK_EC &sk, uint32_t signCount,
    ZuBSpan challenge, ZuCSpan origin, ZuCSpan rpID,
    Zum::AssertionInput &input)
{
  Zum::String encoded;
  encoded.length(ZuBase64URL::enclen(challenge.length()));
  encoded.length(ZuBase64URL::encode(encoded.span(), challenge));
  Zum::String clientData{"{\"type\":\"webauthn.get\",\"challenge\":"};
  ZfJSON::quote(clientData, encoded);
  clientData << ",\"origin\":";
  ZfJSON::quote(clientData, origin);
  clientData << '}';

  uint8_t authData[TestAuthDataSize];
  {
    Ztls::MD<> md;
    md.update(ZuBSpan{rpID});
    md.finish(authData);
  }
  authData[32] = 5;
  authData[33] = signCount>>24;
  authData[34] = signCount>>16;
  authData[35] = signCount>>8;
  authData[36] = signCount;
  uint8_t clientHash[Ztls::MD<>::Size];
  {
    Ztls::MD<> md;
    md.update(clientData);
    md.finish(clientHash);
  }
  uint8_t signedHash[Ztls::MD<>::Size];
  {
    Ztls::MD<> md;
    md.update(authData);
    md.update(clientHash);
    md.finish(signedHash);
  }
  Zum::Bytes signature;
  auto signed_ = sk.sign(rng, signedHash, [&signature](ZuBSpan der) {
    signature = Zum::Bytes{der};
  });
  if (signed_.template is<ZeException>() || !signature) return false;
  input = Zum::AssertionInput{
    .credentialID = Zum::Bytes{ZuBSpan{"credential"}},
    .clientDataJSON = Zum::Bytes{ZuBSpan{clientData}},
    .authenticatorData = Zum::Bytes{ZuBSpan{authData}},
    .signature = ZuMv(signature),
    .userHandle = Zum::Bytes{ZuBSpan{"handle"}}
  };
  return true;
}

bool makeRegistration(
    ZuBSpan publicKey, uint32_t signCount, ZuCSpan credentialID,
    ZuBSpan challenge,
    ZuCSpan origin, ZuCSpan rpID, Zum::RegistrationInput &input)
{
  if (publicKey.length() != Ztls::COSE::ES256::PublicKeySize) return false;
  Zum::String encoded;
  encoded.length(ZuBase64URL::enclen(challenge.length()));
  encoded.length(ZuBase64URL::encode(encoded.span(), challenge));
  Zum::String clientData{"{\"type\":\"webauthn.create\",\"challenge\":"};
  ZfJSON::quote(clientData, encoded);
  clientData << ",\"origin\":";
  ZfJSON::quote(clientData, origin);
  clientData << '}';

  Zum::Bytes authData;
  authData.length(55 + credentialID.length() + 77, false);
  memset(authData.data(), 0, authData.length());
  {
    Ztls::MD<> md;
    md.update(ZuBSpan{rpID});
    md.finish(authData);
  }
  authData[32] = 0x45; // user present, verified, attested data
  authData[33] = signCount>>24;
  authData[34] = signCount>>16;
  authData[35] = signCount>>8;
  authData[36] = signCount;
  authData[53] = credentialID.length()>>8;
  authData[54] = credentialID.length();
  memcpy(authData.data() + 55, credentialID.data(), credentialID.length());
  unsigned o = 55 + credentialID.length();
  authData[o++] = 0xa5;
  authData[o++] = 0x01;
  authData[o++] = 0x02;
  authData[o++] = 0x03;
  authData[o++] = 0x26;
  authData[o++] = 0x20;
  authData[o++] = 0x01;
  authData[o++] = 0x21;
  authData[o++] = 0x58;
  authData[o++] = 0x20;
  memcpy(authData.data() + o, publicKey.data() + 1, 32);
  o += 32;
  authData[o++] = 0x22;
  authData[o++] = 0x58;
  authData[o++] = 0x20;
  memcpy(authData.data() + o, publicKey.data() + 33, 32);
  o += 32;
  if (o != authData.length()) return false;

  Zum::Bytes attestation;
  attestation.length(authData.length() + 30, false);
  o = 0;
  attestation[o++] = 0xa3;
  memcpy(attestation.data() + o, "\x63" "fmt\x64" "none", 9);
  o += 9;
  memcpy(attestation.data() + o, "\x68" "authData", 9);
  o += 9;
  attestation[o++] = 0x58;
  attestation[o++] = authData.length();
  memcpy(attestation.data() + o, authData.data(), authData.length());
  o += authData.length();
  memcpy(attestation.data() + o, "\x67" "attStmt\xa0", 9);
  o += 9;
  if (o != attestation.length()) return false;

  input = Zum::RegistrationInput{
    .credentialID = Zum::Bytes{ZuBSpan{credentialID}},
    .clientDataJSON = Zum::Bytes{ZuBSpan{clientData}},
    .attestationObject = ZuMv(attestation)
  };
  return true;
}

} // namespace zumtest
