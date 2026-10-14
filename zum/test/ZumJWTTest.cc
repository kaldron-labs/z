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


static void jwt()
{
  ZuTestScope(jwt);
  Ztls::Random rng;
  ZuCheck(rng.init());
  Ztls::PK::SK_EC sk{rng, Ztls::PK::OIDs::EC_GRP_SECP256R1};
  uint8_t publicKey[Ztls::COSE::ES256::PublicKeySize];
  ZuCheck(Ztls::Backend::pkey_ec_export_public(sk.key, publicKey));

  Zum::User user;
  user.handle = Zum::Bytes{ZuBSpan{"opaque user handle"}};
  user.state = Zum::State::Active;
  Zum::Client client;
  client.id = "browser";
  client.appID = 27;
  client.profile = Zum::ClientProfile::Browser;
  client.grants = Zum::ClientGrant::AuthCode();
  client.state = Zum::State::Active;
  Zum::ScopeSelection selection;
  Zum::App app{.id = 27, .authVersion = 9, .audience = "orders"};
  selection.scope = "orders.read";
  ZtBitmap authority{8U};
  authority.set(3);
  Zum::Action actions[] = {
    {.id = 3, .name = "orders.read"},
    {.id = 4, .name = "orders.disabled", .state = Zum::State::Disabled}
  };
  Zum::AccessClaims claims;
  ZuCheck(Zum::interactiveClaims(rng, "https://issuer", app, user, client,
    selection, authority, actions, "passkey", 100, 100, 160, claims));
  ZuCheck(claims.subject != "user-42" && claims.jti &&
    claims.appID == 27 && claims.audience == app.audience &&
    claims.actions.length() == 1 && claims.actions[0] == "orders.read");
  client.profile = Zum::ClientProfile::Server;
  ZuCheck(Zum::interactiveClaims(rng, "https://issuer", app, user, client,
    selection, authority, actions, "passkey", 100, 100, 160, claims));
  client.grants = Zum::ClientGrant::ClientCredentials();
  ZuCheck(!Zum::interactiveClaims(rng, "https://issuer", app, user, client,
    selection, authority, actions, "passkey", 100, 100, 160, claims));
  client.grants = Zum::ClientGrant::AuthCode();
  client.profile = Zum::ClientProfile::Browser;
  Zum::String subject = claims.subject;
  Zum::JWTLimits limits;
  Zum::PreparedJWT prepared;
  ZuCheck(Zum::jwtPrepare(claims, "key-1", limits, prepared));
  uint8_t signature[Ztls::COSE::ES256::DERMax];
  unsigned signatureLength = 0;
  auto signed_ = sk.sign(rng, prepared.digest, [
    &signature, &signatureLength
  ](ZuBSpan der) {
    signatureLength = der.length();
    memcpy(signature, der.data(), signatureLength);
  });
  ZuCheck(!signed_.template is<ZeException>() && signatureLength);
  ZuCheck(Zum::jwtFinish(
    prepared, ZuBSpan{signature, signatureLength}, limits));

  Zum::Principal principal;
  ZuCheck(Zum::jwtVerify(prepared.token, "key-1", "https://issuer", "orders",
    publicKey, 120, limits, principal));
  ZuCheck(principal.subject == subject &&
    principal.tokenID.issuerURL == claims.issuerURL &&
    principal.tokenID.jti == claims.jti &&
    principal.clientID == "browser" && principal.scope == "orders.read" &&
    principal.appID == 27 &&
    principal.actions.length() == 1 &&
    principal.actions[0] == "orders.read" &&
    principal.authMethod == "passkey" &&
    principal.authTime == 100 && principal.expires == 160);
  ZuCheck(!Zum::jwtVerify(prepared.token, "key-1", "https://issuer", "other",
    publicKey, 120, limits, principal));
  ZuCheck(!Zum::jwtVerify(prepared.token, "key-1", "https://issuer", "orders",
    publicKey, 160, limits, principal));

  Zum::SignKey verifyKey{.id = "key-1", .issuer = "https://issuer",
    .algorithm = "ES256", .notBefore = 100, .retireAfter = 150,
    .state = Zum::State::Active};
  verifyKey.publicJwk << "{\"kty\":\"EC\",\"crv\":\"P-256\",\"kid\":\"key-1\",\"x\":\"" <<
    base64URL({publicKey + 1, Ztls::COSE::ES256::CoordinateSize}) <<
    "\",\"y\":\"" << base64URL({publicKey + 1 + Ztls::COSE::ES256::CoordinateSize,
      Ztls::COSE::ES256::CoordinateSize}) << "\"}";
  auto verifyRecord = [&verifyKey, &prepared, &limits](
      int64_t now, ZuCSpan issuerURL, ZuCSpan audience) {
    Zum::Principal principal;
    return Zum::signKeyVerify(verifyKey, prepared.token, issuerURL, audience,
      now, limits, principal);
  };
  ZuCheck(verifyRecord(120, "https://issuer", "orders"));
  ZuCheck(verifyRecord(120, "https://issuer", {}));
  ZuCheck(!verifyRecord(120, "https://other-issuer", "orders"));
  ZuCheck(!verifyRecord(120, "https://issuer", "other"));
  verifyKey.state = Zum::State::Suspended;
  ZuCheck(verifyRecord(149, "https://issuer", "orders"));
  ZuCheck(!verifyRecord(150, "https://issuer", "orders"));
  verifyKey.state = Zum::State::Revoked;
  ZuCheck(!verifyRecord(120, "https://issuer", "orders"));
  verifyKey.state = Zum::State::Pending;
  ZuCheck(!verifyRecord(120, "https://issuer", "orders"));
  verifyKey.state = Zum::State::Disabled;
  ZuCheck(!verifyRecord(120, "https://issuer", "orders"));
  verifyKey.state = Zum::State::Active;
  verifyKey.notBefore = 121;
  ZuCheck(!verifyRecord(120, "https://issuer", "orders"));
  verifyKey.notBefore = 100;
  verifyKey.algorithm = "RS256";
  ZuCheck(!verifyRecord(120, "https://issuer", "orders"));
  verifyKey.algorithm = "ES256";
  verifyKey.id = "other-key";
  ZuCheck(!verifyRecord(120, "https://issuer", "orders"));

  Zum::String changed = prepared.token;
  changed[changed.length() / 2] ^= 1;
  ZuCheck(!Zum::jwtVerify(changed, "key-1", "https://issuer", "orders",
    publicKey, 120, limits, principal));

  Zum::ScopeSelection oidcSelection = selection;
  oidcSelection.scope = "openid profile email orders.read";
  user.name = "alice";
  user.profile = "Alice Example";
  user.email = "alice@example.com";
  Zum::IDClaims idClaims;
  ZuCheck(Zum::idClaims("https://issuer", user, client, oidcSelection,
    "nonce-1", "passkey", 100, 120, 180, idClaims));
  Zum::PreparedJWT idPrepared;
  ZuCheck(Zum::idTokenPrepare(idClaims, "key-1", limits, idPrepared));
  signatureLength = 0;
  signed_ = sk.sign(rng, idPrepared.digest, [
    &signature, &signatureLength
  ](ZuBSpan der) {
    signatureLength = der.length();
    memcpy(signature, der.data(), signatureLength);
  });
  ZuCheck(!signed_.template is<ZeException>() && signatureLength &&
    Zum::jwtFinish(idPrepared, ZuBSpan{signature, signatureLength}, limits));
  Zum::JWTHeader idHeader;
  Zum::String idJSON;
  ZuCheck(Zum::jwtES256(idPrepared.token, publicKey, limits,
    idHeader, idJSON) && idHeader.type == "JWT" &&
    idJSON.find<"\"aud\":\"browser\"">() >= 0 &&
    idJSON.find<"\"nonce\":\"nonce-1\"">() >= 0 &&
    idJSON.find<"\"email\":\"alice@example.com\"">() >= 0);
  principal.scope = oidcSelection.scope;
  principal.subject = idClaims.subject;
  Zum::String userInfo;
  ZuCheck(Zum::userInfoJSON(user, principal, userInfo) &&
    userInfo == "{\"sub\":\"b3BhcXVlIHVzZXIgaGFuZGxl\","
      "\"name\":\"Alice Example\",\"preferred_username\":\"alice\","
      "\"email\":\"alice@example.com\"}");
  ZuCheck(Zum::jwtVerifyIssuer(prepared.token, "key-1", "https://issuer",
    publicKey, 120, limits, principal));

  client.id = "workload";
  client.profile = Zum::ClientProfile::Server;
  client.grants = Zum::ClientGrant::ClientCredentials();
  claims = {};
  ZuCheck(Zum::clientClaims(rng, "https://issuer", app, client, selection,
    authority, actions, 27, 200, 260, claims));
  ZuCheck(claims.subject == "workload" && claims.clientID == "workload" &&
    claims.appID == 27 && claims.audience == app.audience && !claims.authTime && !claims.amr &&
    claims.actions.length() == 1);
  ZuCheck(Zum::jwtPrepare(claims, "key-1", limits, prepared));
  signatureLength = 0;
  signed_ = sk.sign(rng, prepared.digest, [
    &signature, &signatureLength
  ](ZuBSpan der) {
    signatureLength = der.length();
    memcpy(signature, der.data(), signatureLength);
  });
  ZuCheck(!signed_.template is<ZeException>() && signatureLength);
  ZuCheck(Zum::jwtFinish(
    prepared, ZuBSpan{signature, signatureLength}, limits));
  ZuCheck(Zum::jwtVerify(prepared.token, "key-1", "https://issuer", "orders",
    publicKey, 220, limits, principal));
  ZuCheck(principal.subject == "workload" &&
    principal.clientID == "workload" && principal.appID == 27 &&
    !principal.authMethod && !principal.authTime && principal.expires == 260);
}
static void ssfSET()
{
  ZuTestScope(ssfSET);
  Ztls::Random rng;
  ZuCheck(rng.init());
  Ztls::PK::SK_EC sk{rng, Ztls::PK::OIDs::EC_GRP_SECP256R1};
  uint8_t publicKey[Ztls::COSE::ES256::PublicKeySize];
  ZuCheck(Ztls::Backend::pkey_ec_export_public(sk.key, publicKey));
  Zum::SignKey key{.id = "key-1", .issuer = "https://issuer"};
  Zum::String set;
  Zum::makeSSF(key, "https://receiver", Zum::RefreshID{
    .issuerURL = "https://issuer/oauth2/9", .familyID = "family-1"}, 180, 120,
    [&sk, &rng](const Zum::SignKey &, ZuBSpan digest,
        Zum::SignatureFn complete) {
      sk.sign(rng, digest, [complete = ZuMv(complete)](ZuBSpan der) mutable {
        complete(Zum::Bytes{der});
      });
    }, [&set](Zum::String value) { set = ZuMv(value); });
  ZuCheck(set);
  Zum::JWTHeader header;
  Zum::String claims;
  Zum::JWTLimits limits;
  ZuCheck(Zum::jwtES256(set, publicKey, limits, header, claims) &&
    header.type == "secevent+jwt" && header.algorithm == "ES256" &&
    claims.find<"urn:zum:events:refresh-token-revoked">() >= 0 &&
    claims.find<"family-1">() >= 0);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(jwt);
  ZuTestCall(ssfSET);
  return 0;
}
