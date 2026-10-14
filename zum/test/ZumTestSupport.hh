//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef ZumTestSupport_HH
#define ZumTestSupport_HH

#include <zlib/ZumLib.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZtlsCOSE.hh>
#include <zlib/ZtlsPK.hh>
#include <zlib/ZtlsRandom.hh>
#include <zlib/zumd_db.hh>
#include <zlib/zumd_request.hh>
#include <zlib/zumd_webauthn.hh>

namespace zumtest {

struct RuntimeDB : public Zum::DB {
  ZmSemaphore	active;
  ZmRef<Zum::Requests> requests;
};

extern Zum::StringVec logRows;

void prepareIdentity(ZmRef<RuntimeDB>, ZmRef<Zum::DB>,
  Ztls::Random &, Zum::Bytes &, ZmRef<Ztls::PK::SK_EC> &);

struct OIDCSetup {
  Zum::Client browser;
  Zum::IDVec appRoles;
  Zum::String code;
};

void prepareOIDC(ZmRef<RuntimeDB>, ZmRef<Zum::DB>,
  Ztls::Random &, OIDCSetup &);

struct TokenSetup {
  Zum::Bytes codeFamilyID;
  Zum::String browserCode;
};

void prepareToken(ZmRef<RuntimeDB>, ZmRef<Zum::DB>,
  Ztls::Random &, OIDCSetup &, const Zum::Bytes &,
  Ztls::PK::SK_EC &, Ztls::PK::SK_EC &,
  uint8_t (&)[Ztls::COSE::ES256::PublicKeySize], TokenSetup &);

void checkKeys(ZmRef<RuntimeDB>, ZmRef<Zum::DB>, Ztls::Random &,
  Ztls::PK::SK_EC &,
  uint8_t (&)[Ztls::COSE::ES256::PublicKeySize]);

inline constexpr char assertionClientData_[] =
  "{\"type\":\"webauthn.get\",\"challenge\":\"Y2hhbGxlbmdl\","
  "\"origin\":\"https://example.com\","
  "\"tokenBinding\":{\"status\":\"supported\"}}";

Zum::String testJwk(ZuCSpan);
Zum::String base64URL(ZuBSpan);
bool oidcParams(Zum::String, Zum::String &, Zum::String &);
bool signJWT(Ztls::Random &, Ztls::PK::SK_EC &,
  ZuCSpan, ZuCSpan, Zum::String &);
bool makeAssertion(Ztls::Random &, Ztls::PK::SK_EC &, uint32_t,
  Zum::AssertionInput &, bool backupEligible = false);
bool makeAssertion(Ztls::Random &, Ztls::PK::SK_EC &, uint32_t,
  ZuBSpan, ZuCSpan, ZuCSpan, Zum::AssertionInput &);
bool makeRegistration(ZuBSpan, uint32_t, ZuCSpan,
  ZuBSpan, ZuCSpan, ZuCSpan, Zum::RegistrationInput &);

} // namespace zumtest

#endif /* ZumTestSupport_HH */
