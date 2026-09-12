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
    .scheduler = &scheduler, .sid = 1, .issuerURL = "https://issuer",
    .clientID = "service", .clientSecret = Zum::Bytes{ZuBSpan{"secret"}},
    .audience = "ping"}, [&requests](Zum::ServiceHTTPRequest request,
      Zum::ServiceHTTPDoneFn complete) {
    ++requests;
    if (request.url == "https://issuer/token") {
      complete(Zum::ServiceHTTPResponse{200,
	"{\"access_token\":\"not-a-jwt\",\"expires_in\":300}"});
    } else if (request.url == "https://issuer/.well-known/openid-configuration") {
      complete(Zum::ServiceHTTPResponse{200,
	"{\"issuer\":\"https://issuer\",\"jwks_uri\":\"https://issuer/jwks\"}"});
    } else if (request.url == "https://issuer/jwks") {
      complete(Zum::ServiceHTTPResponse{200, "{\"keys\":[]}"});
    } else complete(Zum::ServiceHTTPResponse{404, {}});
  }));
  // Parsed token/discovery nodes must remain alive through field extraction;
  // startup must reach JWKS, then reject the unusable key set/token.
  ZuCheck(ZmBlock<int>{}([&service](auto wake) mutable {
    service.start([wake = ZuMv(wake)](int error) mutable { wake(error); });
  }) == Zum::ServiceError::Unauthorized);
  ZuCheck(requests == 3);
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
  Zum::String claims{"{\"iss\":\"https://issuer\",\"sub\":\"service\","
    "\"client_id\":\"service\",\"aud\":\"https://issuer/admin\","
    "\"zum_app_id\":\"9\",\"scope\":\"zum.service\",\"jti\":\"test\",\"iat\":"};
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
  unsigned tokens = 0, calls = 0;
  bool holdRenewal = false;
  Zum::ServiceHTTPDoneFn renewal;
  ZmSemaphore renewalHeld, operationDone;
  Zum::Service service;
  Zum::ServiceConfig config{.scheduler = &scheduler, .sid = 1,
    .issuerURL = "https://issuer", .clientID = "service",
    .clientSecret = Zum::Bytes{ZuBSpan{"secret:+ &%"}}, .audience = "ping",
    .renewSkew = 600};
  Zum::ServiceHTTPFn http{[&token, &jwks, &tokens, &calls,
      &holdRenewal, &renewal, &renewalHeld](
      Zum::ServiceHTTPRequest request, Zum::ServiceHTTPDoneFn complete) {
    if (request.url == "https://issuer/token") {
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
    } else if (request.url == "https://issuer/.well-known/openid-configuration") {
      complete(Zum::ServiceHTTPResponse{200,
	"{\"issuer\":\"https://issuer\",\"jwks_uri\":\"https://issuer/jwks\"}"});
    } else if (request.url == "https://issuer/jwks") {
      complete(Zum::ServiceHTTPResponse{200, jwks});
    } else if (request.url == "https://issuer/service/token") {
      ++calls;
      complete(Zum::ServiceHTTPResponse{
	request.authorization == Zum::String{"Bearer "} << token ? 200U : 401U, "{}"});
    } else if (request.url == "https://issuer/service/authorize") {
      complete(Zum::ServiceHTTPResponse{200, "{}"});
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
    service.token("grant_type=authorization_code&code=test",
      [wake = ZuMv(wake)](Zum::ServiceProtocolResult result) mutable {
	wake(result.error);
      });
  }) == Zum::ServiceError::OK);
  // The signed expiry is inside renewSkew even though expires_in is not.
  ZuCheck(tokens == 2 && calls == 1);
  ZuCheck(ZmBlock<int>{}([&service](auto wake) mutable {
    service.authorize(Zum::ServiceAuthorizeRequest{
      .clientID = "browser", .redirectURI = "http://127.0.0.1/callback",
      .scope = "ping", .codeChallenge = "challenge", .codeChallengeMethod = "S256"},
      [wake = ZuMv(wake)](Zum::ServiceAuthorizeResult result) mutable {
	wake(result.error);
      });
  }) == Zum::ServiceError::Invalid);
  // The service's administrative bearer is not an application access token.
  ZuCheck(ZmBlock<int>{}([&service, &token](auto wake) mutable {
    service.verify(token, [wake = ZuMv(wake)](int error,
        Zum::ServicePrincipal) mutable { wake(error); });
  }) == Zum::ServiceError::Unauthorized);
  // A successful renewal delivered during stop must not advance the operation.
  holdRenewal = true;
  int operationError = Zum::ServiceError::Invalid;
  service.token("grant_type=authorization_code&code=test",
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
  for (unsigned phase = 0; phase < 3; ++phase) {
    Zum::Service service;
    Zum::ServiceHTTPDoneFn pending;
    ZmSemaphore held, started;
    unsigned requests = 0;
    int startError = Zum::ServiceError::Invalid;
    ZuCheck(service.init(Zum::ServiceConfig{
      .scheduler = &scheduler, .sid = 1, .issuerURL = "https://issuer",
      .clientID = "service", .clientSecret = Zum::Bytes{ZuBSpan{"secret"}},
      .audience = "ping"}, [phase, &requests, &pending, &held](
        Zum::ServiceHTTPRequest request, Zum::ServiceHTTPDoneFn complete) {
      if (requests++ == phase) {
	pending = ZuMv(complete);
	held.post();
	return;
      }
      if (request.url == "https://issuer/token") {
	complete(Zum::ServiceHTTPResponse{200,
	  "{\"access_token\":\"pending-token\",\"expires_in\":300}"});
      } else {
	complete(Zum::ServiceHTTPResponse{200,
	  "{\"issuer\":\"https://issuer\",\"jwks_uri\":\"https://issuer/jwks\"}"});
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

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(publicAPI);
  ZuTestCall(responseLifetime);
  ZuTestCall(serviceToken);
  ZuTestCall(stopStarting);
  return 0;
}
