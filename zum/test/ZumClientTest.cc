//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuBase64URL.hh>
#include <zlib/ZmBlock.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZtlsCOSE.hh>
#include <zlib/ZtlsMD.hh>
#include <zlib/ZtlsPK.hh>
#include <zlib/ZtlsRandom.hh>

#include <zlib/ZumService.hh>
#include <zlib/zumd_revoke.hh>

using namespace ZuTestUtil;

static void publicAPI()
{
  ZuTestScope(publicAPI);
  Zum::Service service;
  ZuCheck(!service.init({}, {}));
  service.final();
}

static void responseLifetime()
{
  ZuTestScope(responseLifetime);
  ZmSchedParams params;
  params.nThreads(1);
  params.thread(1).isolated(true);
  ZmScheduler scheduler{ZuMv(params)};
  ZuCheck(scheduler.start());
  unsigned requests = 0;
  Zum::Service service;
  ZuCheck(service.init(Zum::ServiceConfig{
    .scheduler = &scheduler, .sid = 1,
    .issuerURL = "https://issuer/oauth2/9",
    .managementIssuerURL = "https://issuer/oauth2/9",
    .managementURL = "https://issuer",
    .clientID = "service", .clientSecret = Zum::Bytes{ZuBSpan{"secret"}},
    .audience = "ping"}, [&requests](Zum::ServiceHTTPRequest request,
      Zum::ServiceHTTPDoneFn complete) {
    ++requests;
    if (request.url == "https://tokens.example/custom/exchange") {
      complete(Zum::ServiceHTTPResponse{200,
	"{\"access_token\":\"not-a-jwt\",\"expires_in\":300}"});
    } else if (request.url == "https://issuer/.well-known/"
        "oauth-authorization-server/oauth2/9") {
	complete(Zum::ServiceHTTPResponse{200,
	"{\"issuer\":\"https://issuer/oauth2/9\","
	"\"token_endpoint\":\"https://tokens.example/custom/exchange\","
	"\"jwks_uri\":\"https://keys.example/jwks/app-9\"}"});
    } else if (request.url == "https://keys.example/jwks/app-9") {
      complete(Zum::ServiceHTTPResponse{200, "{\"keys\":[]}"});
    } else complete(Zum::ServiceHTTPResponse{404, {}});
  }));
  // Parsed token/discovery nodes must remain alive through field extraction;
  // startup must reach JWKS, then reject the unusable key set/token.
  ZuCheck(ZmBlock<int>{}([&service](auto wake) mutable {
    service.start([wake = ZuMv(wake)](int error) mutable { wake(error); });
  }) == Zum::ServiceError::Unauthorized);
  ZuCheck(requests == 4);
  ZuCheck(ZmBlock<int>{}([&service](auto wake) mutable {
    service.stop([wake = ZuMv(wake)](int error) mutable { wake(error); });
  }) == Zum::ServiceError::OK);
  service.final();
  ZuCheck(scheduler.stop());
}

static Zum::String encode(ZuBSpan data)
{
  Zum::String result;
  result.length(ZuBase64URL::enclen(data.length()));
  result.length(ZuBase64URL::encode(result.span(), data));
  return result;
}

static void serviceToken()
{
  ZuTestScope(serviceToken);
  Ztls::Random rng;
  ZuCheck(rng.init());
  Ztls::PK::SK_EC key{rng, Ztls::PK::OIDs::EC_GRP_SECP256R1};
  uint8_t point[Ztls::COSE::ES256::PublicKeySize];
  ZuCheck(Ztls::Backend::pkey_ec_export_public(key.key, point));
  Zum::String jwk{"{\"kid\":\"service-key\",\"kty\":\"EC\",\"crv\":\"P-256\",\"x\":\""};
  jwk << encode({point + 1, Ztls::COSE::ES256::CoordinateSize}) <<
    "\",\"y\":\"" << encode({point + 1 + Ztls::COSE::ES256::CoordinateSize,
      Ztls::COSE::ES256::CoordinateSize}) << "\"}";
  Zum::String jwks{"{\"keys\":["};
  jwks << jwk << "]}";
  int64_t now = Zm::now().sec();
  Zum::String claims{"{\"iss\":\"https://issuer/oauth2/7\",\"sub\":\"service\","
    "\"client_id\":\"service\",\"aud\":\"https://issuer/admin\","
    "\"zum_app_id\":\"7\",\"scope\":\"zum.catalog\",\"jti\":\"test\",\"iat\":"};
  claims << now << ",\"nbf\":" << now << ",\"exp\":" << now + 300 << '}';
  Zum::String token = encode(ZuBSpan{
    "{\"alg\":\"ES256\",\"typ\":\"at+jwt\",\"kid\":\"service-key\"}"});
  token << '.' << encode(ZuBSpan{claims});
  uint8_t digest[Ztls::MD<>::Size];
  Ztls::MD<> md;
  md.update(ZuBSpan{token});
  md.finish(digest);
  Zum::Bytes der;
  auto signed_ = key.sign(rng, digest, [&der](ZuBSpan value) {
    der = Zum::Bytes{value};
  });
  uint8_t raw[Ztls::COSE::ES256::SignatureSize];
  ZuCheck(!signed_.template is<ZeException>() &&
    Ztls::COSE::ES256::derToRaw(der, raw));
  token << '.' << encode(raw);

  ZmSchedParams params;
  params.nThreads(1);
  params.thread(1).isolated(true);
  ZmScheduler scheduler{ZuMv(params)};
  ZuCheck(scheduler.start());
  unsigned tokens = 0, calls = 0, introspections = 0;
  bool introspectionAuth = false;
  bool holdRenewal = false;
  Zum::ServiceHTTPDoneFn renewal;
  ZmSemaphore renewalHeld, operationDone;
  Zum::Service service;
  Zum::ServiceConfig config{.scheduler = &scheduler, .sid = 1,
    .issuerURL = "https://issuer/oauth2/9",
    .managementIssuerURL = "https://issuer/oauth2/7",
    .managementURL = "https://issuer", .clientID = "service",
    .clientSecret = Zum::Bytes{ZuBSpan{"secret:+ &%"}}, .audience = "ping",
    .renewSkew = 600,
    .introspectionURL = "https://issuer/oauth2/9/v1/introspect",
    .introspectionClientID = "introspector",
    .introspectionSecret = Zum::Bytes{ZuBSpan{"introspect-secret"}},
    .ssf = Zum::ServiceSSFConfig{
      .enabled = true, .receiverID = "service", .callbackPath = "/ssf",
      .callbackAuth = "Bearer callback",
      .transmitterIssuer = "https://issuer/oauth2/8", .audience = "ping"}};
  Zum::ServiceHTTPFn http{[&token, &jwks, &tokens, &calls, &introspections,
      &introspectionAuth,
      &holdRenewal, &renewal, &renewalHeld](
      Zum::ServiceHTTPRequest request, Zum::ServiceHTTPDoneFn complete) {
    if (request.url == "https://issuer/oauth2/7/v1/token") {
      ++tokens;
      if (request.authorization != "Basic c2VydmljZTpzZWNyZXQlM0ElMkIlMjAlMjYlMjU=") {
	complete(Zum::ServiceHTTPResponse{401, "{}"});
	return;
      }
      if (holdRenewal) {
	renewal = ZuMv(complete);
	renewalHeld.post();
	return;
      }
      complete(Zum::ServiceHTTPResponse{200, Zum::String{
	"{\"access_token\":\""} << token << "\",\"expires_in\":3600}"});
    } else if (request.url == "https://issuer/.well-known/"
        "oauth-authorization-server/oauth2/9") {
      complete(Zum::ServiceHTTPResponse{200,
	"{\"issuer\":\"https://issuer/oauth2/9\","
	"\"token_endpoint\":\"https://issuer/oauth2/9/v1/token\","
	"\"jwks_uri\":\"https://issuer/oauth2/9/v1/keys\"}"});
    } else if (request.url == "https://issuer/.well-known/"
        "oauth-authorization-server/oauth2/7") {
      complete(Zum::ServiceHTTPResponse{200,
	"{\"issuer\":\"https://issuer/oauth2/7\","
	"\"token_endpoint\":\"https://issuer/oauth2/7/v1/token\","
	"\"jwks_uri\":\"https://issuer/oauth2/7/v1/keys\"}"});
    } else if (request.url == "https://issuer/.well-known/"
        "oauth-authorization-server/oauth2/8") {
      complete(Zum::ServiceHTTPResponse{200,
	"{\"issuer\":\"https://issuer/oauth2/8\","
	"\"token_endpoint\":\"https://issuer/oauth2/8/v1/token\","
	"\"jwks_uri\":\"https://issuer/oauth2/8/v1/keys\"}"});
    } else if (request.url == "https://issuer/oauth2/7/v1/keys" ||
        request.url == "https://issuer/oauth2/8/v1/keys" ||
        request.url == "https://issuer/oauth2/9/v1/keys") {
      complete(Zum::ServiceHTTPResponse{200, jwks});
    } else if (request.url == "https://issuer/oauth2/9/v1/introspect") {
      ++introspections;
      introspectionAuth = request.authorization ==
        "Basic aW50cm9zcGVjdG9yOmludHJvc3BlY3Qtc2VjcmV0";
      Zum::String body{
        "{\"active\":true,\"iss\":\"https://issuer/oauth2/9\","
        "\"aud\":\"ping\",\"sub\":\"service\","
        "\"client_id\":\"service\",\"zum_app_id\":\"9\","
        "\"jti\":\"introspected\",\"scope\":\"ping\","
        "\"actions\":[\"ping\"],\"exp\":"};
      body << (Zm::now().sec() + 300) << '}';
      complete(Zum::ServiceHTTPResponse{200, ZuMv(body)});
    } else if (request.url == "https://issuer/admin/apps/9/catalog") {
      ++calls;
      complete(Zum::ServiceHTTPResponse{
	request.authorization == Zum::String{"Bearer "} << token ? 200U : 401U, "{}"});
    } else complete(Zum::ServiceHTTPResponse{404, {}});
  }};
  config.sid = 0;
  ZuCheck(!service.init(config, http));
  config.sid = 2;
  ZuCheck(!service.init(config, http));
  config.sid = 1;
  ZuCheck(service.init(config, http));
  ZuCheck(ZmBlock<int>{}([&service](auto wake) mutable {
    service.start([wake = ZuMv(wake)](int error) mutable { wake(error); });
  }) == Zum::ServiceError::OK);
  ZuCheck(tokens == 1);
  ZuCheck(ZmBlock<int>{}([&service](auto wake) mutable {
    service.publish(Zum::ServiceManifest{.revision = 1},
      [wake = ZuMv(wake)](Zum::ServiceProtocolResult result) mutable {
	wake(result.error);
      });
  }) == Zum::ServiceError::OK);
  // The signed expiry is inside renewSkew even though expires_in is not.
  ZuCheck(tokens == 2 && calls == 1);
  Zum::String unknown = encode(ZuBSpan{
    "{\"alg\":\"ES256\",\"typ\":\"at+jwt\",\"kid\":\"unknown\"}"});
  unknown << '.' << encode(ZuBSpan{"{}"}) << ".invalid";
  bool introspectOK = false;
  ZuCheck(ZmBlock<int>{}([&service, &unknown, &introspectOK](auto wake) mutable {
    service.verify(ZuMv(unknown), [&introspectOK, wake = ZuMv(wake)](int error,
        Zum::ServicePrincipal principal) mutable {
      introspectOK = error == Zum::ServiceError::OK &&
        principal.tokenID.issuer == "https://issuer/oauth2/9" &&
        principal.tokenID.jti == "introspected";
      wake(error);
    });
  }) == Zum::ServiceError::OK);
  ZuCheck(introspectOK);
  ZuCheck(introspectionAuth);
  ZuCheck(introspections == 1);
  Zum::String known = token;
  ZuCheck(ZmBlock<int>{}([&service, &known](auto wake) mutable {
    service.verify(ZuMv(known), [wake = ZuMv(wake)](int error,
        Zum::ServicePrincipal) mutable { wake(error); });
  }) == Zum::ServiceError::Unauthorized);
  ZuCheck(introspections == 1);
  unsigned refreshRevoked = 0;
  service.setRefreshRevocationFn([&refreshRevoked](Zum::RefreshID refreshID,
      int64_t expires) {
    ++refreshRevoked;
    ZuCheck(refreshID.issuer == "https://issuer/oauth2/9" &&
      refreshID.familyID == "family-1" && expires > Zm::now().sec());
  });
  Zum::SignKey setKey{.id = "service-key", .issuer =
    "https://issuer/oauth2/8"};
  Zum::String set;
  int64_t setNow = Zm::now().sec();
  Zum::makeSSF(setKey, "ping", Zum::RefreshID{
    .issuer = "https://issuer/oauth2/9", .familyID = "family-1"},
    setNow + 120, setNow,
    [&key, &rng](const Zum::SignKey &, ZuBSpan digest,
        Zum::SignatureFn complete) {
      key.sign(rng, digest, [complete = ZuMv(complete)](ZuBSpan der) mutable {
        complete(Zum::Bytes{der});
      });
    }, [&set](Zum::String value) { set = ZuMv(value); });
  ZuCheck(set);
  int setError = ZmBlock<int>{}([&service, &set](auto wake) mutable {
    service.receiveSET(Zum::ServiceSETRequest{
      .authorization = "Bearer callback",
      .contentType = "application/secevent+jwt", .body = set},
      [wake = ZuMv(wake)](int error) mutable {
        wake(error);
      });
  });
  ZuCheck(setError == Zum::ServiceError::OK);
  ZuCheck(refreshRevoked == 1);
  auto receive = [&service](Zum::String value) {
    return ZmBlock<int>{}([&service, value = ZuMv(value)](auto wake) mutable {
      service.receiveSET(Zum::ServiceSETRequest{
        .authorization = "Bearer callback",
        .contentType = "application/secevent+jwt", .body = ZuMv(value)},
        [wake = ZuMv(wake)](int error) mutable { wake(error); });
    });
  };
  Zum::String wrongIssuer;
  Zum::makeSSF(Zum::SignKey{.id = "service-key",
      .issuer = "https://issuer/oauth2/wrong"}, "ping", Zum::RefreshID{
      .issuer = "https://issuer/oauth2/9", .familyID = "family-2"},
    setNow + 120, setNow,
    [&key, &rng](const Zum::SignKey &, ZuBSpan digest,
        Zum::SignatureFn complete) {
      key.sign(rng, digest, [complete = ZuMv(complete)](ZuBSpan der) mutable {
        complete(Zum::Bytes{der});
      });
    }, [&wrongIssuer](Zum::String value) { wrongIssuer = ZuMv(value); });
  ZuCheck(receive(ZuMv(wrongIssuer)) == Zum::ServiceError::Unauthorized);
  Zum::String wrongAudience;
  Zum::makeSSF(setKey, "other", Zum::RefreshID{
      .issuer = "https://issuer/oauth2/9", .familyID = "family-3"},
    setNow + 120, setNow,
    [&key, &rng](const Zum::SignKey &, ZuBSpan digest,
        Zum::SignatureFn complete) {
      key.sign(rng, digest, [complete = ZuMv(complete)](ZuBSpan der) mutable {
        complete(Zum::Bytes{der});
      });
    }, [&wrongAudience](Zum::String value) { wrongAudience = ZuMv(value); });
  ZuCheck(receive(ZuMv(wrongAudience)) == Zum::ServiceError::Unauthorized);
  Zum::String stale;
  Zum::makeSSF(setKey, "ping", Zum::RefreshID{
      .issuer = "https://issuer/oauth2/9", .familyID = "family-4"},
    setNow + 120, setNow - 100,
    [&key, &rng](const Zum::SignKey &, ZuBSpan digest,
        Zum::SignatureFn complete) {
      key.sign(rng, digest, [complete = ZuMv(complete)](ZuBSpan der) mutable {
        complete(Zum::Bytes{der});
      });
    }, [&stale](Zum::String value) { stale = ZuMv(value); });
  ZuCheck(receive(ZuMv(stale)) == Zum::ServiceError::Unauthorized);
  setError = ZmBlock<int>{}([&service, &set](auto wake) mutable {
    service.receiveSET(Zum::ServiceSETRequest{
      .authorization = "Bearer callback", .contentType = "application/jwt",
      .body = set}, [wake = ZuMv(wake)](int error) mutable { wake(error); });
  });
  ZuCheck(setError == Zum::ServiceError::Invalid && refreshRevoked == 1);
  setError = ZmBlock<int>{}([&service, &set](auto wake) mutable {
    service.receiveSET(Zum::ServiceSETRequest{
      .authorization = "Bearer wrong", .contentType = "application/secevent+jwt",
      .body = set}, [wake = ZuMv(wake)](int error) mutable { wake(error); });
  });
  ZuCheck(setError == Zum::ServiceError::Invalid && refreshRevoked == 1);
  Zum::String malformed = set;
  malformed << 'x';
  setError = ZmBlock<int>{}([&service, &malformed](auto wake) mutable {
    service.receiveSET(Zum::ServiceSETRequest{
      .authorization = "Bearer callback",
      .contentType = "application/secevent+jwt", .body = malformed},
      [wake = ZuMv(wake)](int error) mutable { wake(error); });
  });
  ZuCheck(setError == Zum::ServiceError::Unauthorized && refreshRevoked == 1);
  setError = ZmBlock<int>{}([&service, &set](auto wake) mutable {
    service.receiveSET(Zum::ServiceSETRequest{
      .authorization = "Bearer callback",
      .contentType = "application/secevent+jwt", .body = set},
      [wake = ZuMv(wake)](int error) mutable {
        wake(error);
      });
  });
  ZuCheck(setError == Zum::ServiceError::OK);
  ZuCheck(refreshRevoked == 1);
  // The service's administrative bearer is not an application access token.
  ZuCheck(ZmBlock<int>{}([&service, &token](auto wake) mutable {
    service.verify(token, [wake = ZuMv(wake)](int error,
        Zum::ServicePrincipal) mutable { wake(error); });
  }) == Zum::ServiceError::Unauthorized);
  // A successful renewal delivered during stop must not advance the operation.
  holdRenewal = true;
  int operationError = Zum::ServiceError::Invalid;
  service.publish(Zum::ServiceManifest{.revision = 2},
    [&operationError, &operationDone](Zum::ServiceProtocolResult result) {
      operationError = result.error;
      operationDone.post();
    });
  renewalHeld.wait();
  ZuCheck(ZmBlock<int>{}([&service, &scheduler, &renewal, &token](auto wake) mutable {
    service.stop([wake = ZuMv(wake)](int error) mutable { wake(error); });
    scheduler.run([complete = ZuMv(renewal), token]() mutable {
      complete(Zum::ServiceHTTPResponse{200, Zum::String{
	"{\"access_token\":\""} << token << "\",\"expires_in\":3600}"});
    }, 1);
  }) == Zum::ServiceError::OK);
  operationDone.wait();
  ZuCheck(operationError == Zum::ServiceError::Stopped && calls == 1);
  holdRenewal = false;
  service.final();
  Zum::StringVec invalidKeys;
  invalidKeys.push(Zum::String{"{\"keys\":["} << jwk << ',' << jwk << "]}");
  invalidKeys.push(Zum::String{"{\"keys\":[],\"keys\":["} << jwk << "]}");
  Zum::String duplicateField = jwk;
  duplicateField.length(duplicateField.length() - 1);
  duplicateField << ",\"kid\":\"service-key\"}";
  invalidKeys.push(Zum::String{"{\"keys\":["} << duplicateField << "]}");
  for (auto &invalid: invalidKeys) {
    jwks = invalid;
    ZuCheck(service.init(config, http));
    ZuCheck(ZmBlock<int>{}([&service](auto wake) mutable {
      service.start([wake = ZuMv(wake)](int error) mutable { wake(error); });
    }) == Zum::ServiceError::Unauthorized);
    ZuCheck(ZmBlock<int>{}([&service](auto wake) mutable {
      service.stop([wake = ZuMv(wake)](int error) mutable { wake(error); });
    }) == Zum::ServiceError::OK);
    service.final();
  }
  ZuCheck(scheduler.stop());
}

static void stopStarting()
{
  ZuTestScope(stopStarting);
  ZmSchedParams params;
  params.nThreads(1);
  params.thread(1).isolated(true);
  ZmScheduler scheduler{ZuMv(params)};
  ZuCheck(scheduler.start());
  // Hold each startup HTTP phase in turn; queue stop before its response.
  for (unsigned phase = 0; phase < 4; ++phase) {
    Zum::Service service;
    Zum::ServiceHTTPDoneFn pending;
    ZmSemaphore held, started;
    unsigned requests = 0;
    int startError = Zum::ServiceError::Invalid;
    ZuCheck(service.init(Zum::ServiceConfig{
      .scheduler = &scheduler, .sid = 1,
      .issuerURL = "https://issuer/oauth2/9",
      .managementIssuerURL = "https://issuer/oauth2/9",
      .managementURL = "https://issuer",
      .clientID = "service", .clientSecret = Zum::Bytes{ZuBSpan{"secret"}},
      .audience = "ping"}, [phase, &requests, &pending, &held](
        Zum::ServiceHTTPRequest request, Zum::ServiceHTTPDoneFn complete) {
      if (requests++ == phase) {
	pending = ZuMv(complete);
	held.post();
	return;
      }
      if (request.url == "https://issuer/oauth2/9/v1/token") {
	complete(Zum::ServiceHTTPResponse{200,
	  "{\"access_token\":\"pending-token\",\"expires_in\":300}"});
      } else {
	complete(Zum::ServiceHTTPResponse{200,
	  "{\"issuer\":\"https://issuer/oauth2/9\","
	  "\"token_endpoint\":\"https://issuer/oauth2/9/v1/token\","
	  "\"jwks_uri\":\"https://issuer/oauth2/9/v1/keys\"}"});
      }
    }));
    service.start([&startError, &started](int error) {
      startError = error;
      started.post();
    });
    held.wait();
    ZuCheck(ZmBlock<int>{}([&service, &scheduler, &pending](auto wake) mutable {
      service.stop([wake = ZuMv(wake)](int error) mutable { wake(error); });
      scheduler.run([complete = ZuMv(pending)]() mutable {
	complete(Zum::ServiceHTTPResponse{503, "{}"});
      }, 1);
    }) == Zum::ServiceError::OK);
    started.wait();
    ZuCheck(startError == Zum::ServiceError::Stopped && requests == phase + 1);
    service.final();
  }
  ZuCheck(scheduler.stop());
}

static void ssfDelivery()
{
  ZuTestScope(ssfDelivery);
  Zum::SSFRx receiver{
    .receiverID = "receiver", .appID = 9, .audience = "ping",
    .deliveryURL = "https://receiver.example/ssf", .secretRef = "secret"};
  Zum::SSFDelivery delivery{
    .eventID = "event", .receiverID = "receiver",
    .familyIssuer = "https://issuer/oauth2/9", .familyID = "family",
    .familyExpires = Zm::now().sec() + 60,
    .set = Zum::Bytes{ZuBSpan{"signed-set"}},
    .nextDelivery = Zm::now().sec()};
  unsigned calls = 0;
  int result = 0;
  Zum::sendSSF(ZuMv(receiver), ZuMv(delivery), "Bearer callback",
    [&calls](Zum::OIDCHTTPRequest request, Zum::OIDCHTTPDoneFn complete) {
      ++calls;
      ZuCheck(request.method == Zum::OIDCHTTPMethod::POST &&
        request.url == "https://receiver.example/ssf" &&
        request.contentType == "application/secevent+jwt" &&
        request.authorization == "Bearer callback" &&
        request.body == "signed-set");
      complete(503, Zum::String{});
    }, [&result](int status) { result = status; });
  ZuCheck(calls == 1 && result == Zum::OAuthError::TemporarilyUnavailable);
  result = 0;
  Zum::sendSSF(Zum::SSFRx{
      .receiverID = "receiver", .appID = 9, .audience = "ping",
      .deliveryURL = "https://receiver.example/ssf", .secretRef = "secret"},
    Zum::SSFDelivery{
      .eventID = "event", .receiverID = "receiver",
      .familyIssuer = "https://issuer/oauth2/9", .familyID = "family",
      .familyExpires = Zm::now().sec() + 60,
      .set = Zum::Bytes{ZuBSpan{"signed-set"}},
      .nextDelivery = Zm::now().sec()}, "Bearer callback",
    [&calls](Zum::OIDCHTTPRequest, Zum::OIDCHTTPDoneFn complete) {
      ++calls;
      complete(202, Zum::String{});
    }, [&result](int status) { result = status; });
  ZuCheck(calls == 2 && result == Zum::RevokeIssue::OK);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(publicAPI);
  ZuTestCall(responseLifetime);
  ZuTestCall(serviceToken);
  ZuTestCall(stopStarting);
  ZuTestCall(ssfDelivery);
  return 0;
}
