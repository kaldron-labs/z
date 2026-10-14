//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuBase64URL.hh>
#include <zlib/ZuMatcher.hh>
#include <zlib/ZmBlock.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZtlsCOSE.hh>
#include <zlib/ZtlsMD.hh>
#include <zlib/ZtlsPK.hh>
#include <zlib/ZtlsRandom.hh>

#include <zlib/ZumService.hh>
#include <zlib/zum_cli.hh>
#include <zlib/zumd_revoke.hh>

using namespace ZuTestUtil;

static bool cliRequest(const char *const *argv, unsigned argc,
    ZumCLI::String &json)
{
  ZfCLI::InArgv<false> input(argc, argv);
  ZfCLI::Parser<ZumCLI::Options> parser;
  try {
    parser.scanArgv(input.argv);
    ZumCLI::Options options;
    ZfCLI::handler<ZumCLI::Options>(parser.root).load(options);
  }
  catch (const ZeException &) { return false; }
  auto &object = parser.root->data<ZfCLI::AnyNode::Object>();
  auto node = ZumCLI::field(object, "command");
  if (!node) return false;
  auto op = ZumCLI::operation(parser);
  ZumCLI::String error;
  return Zum::managementRoute(op) && ZumCLI::request(parser, op, json, error);
}

static void cliArgs()
{
  ZuTestScope(cliArgs);
  ZumCLI::String json;
  const char *enroll[] = {"zum", "app", "add", "ztchub-local",
    "http://localhost:8090/admin", "--label", "Hub \"local\""};
  ZuCheck(cliRequest(enroll, ZuArray{enroll}.length(), json));
  ZuCheck(json == "{\"name\":\"ztchub-local\","
    "\"audience\":\"http://localhost:8090/admin\","
    "\"label\":\"Hub \\\"local\\\"\"}");
  const char *roles[] = {"zum", "assign", "roles", "18446744073709551614",
    "42", "9,18446744073709551614", "--if-match", "\"v1\""};
  ZuCheck(cliRequest(roles, ZuArray{roles}.length(), json));
  ZuCheck(json == "{\"app_id\":\"18446744073709551614\",\"user_id\":\"42\","
    "\"role_ids\":[\"9\",\"18446744073709551614\"]}");
  roles[5] = "";
  ZuCheck(cliRequest(roles, ZuArray{roles}.length(), json));
  ZuCheck(json == "{\"app_id\":\"18446744073709551614\",\"user_id\":\"42\",\"role_ids\":[]}");
  roles[3] = "18446744073709551615";
  roles[4] = "18446744073709551615";
  ZuCheck(cliRequest(roles, ZuArray{roles}.length(), json));
  ZuCheck(json == "{\"app_id\":\"18446744073709551615\","
    "\"user_id\":\"18446744073709551615\",\"role_ids\":[]}");
  roles[3] = "-1";
  ZuCheck(!cliRequest(roles, ZuArray{roles}.length(), json));
  roles[3] = "18446744073709551616";
  ZuCheck(!cliRequest(roles, ZuArray{roles}.length(), json));
  roles[3] = "1oops";
  ZuCheck(!cliRequest(roles, ZuArray{roles}.length(), json));
  const char *client[] = {"zum", "client", "add", "9", "native", "--grants", "AuthCode,Refresh",
    "--refresh-allowed", "false", "--redirect-uris", "http://localhost/cb"};
  ZuCheck(cliRequest(client, ZuArray{client}.length(), json));
  ZuCheck(json == "{\"app_id\":\"9\",\"profile\":\"native\","
    "\"redirect_uris\":[\"http://localhost/cb\"],\"grants\":\"AuthCode,Refresh\",\"refresh_allowed\":false}");
  client[6] = "Refresh,AuthCode";
  ZuCheck(cliRequest(client, ZuArray{client}.length(), json));
  ZuCheck(json.find<"\"grants\":\"AuthCode,Refresh\"">() >= 0);
  for (auto grants: {"5", "AuthCode|Refresh", "AuthCode,RefreshToken",
      "AuthCode,", "Unknown"}) {
    client[6] = grants;
    ZuCheck(!cliRequest(client, ZuArray{client}.length(), json));
  }
  client[6] = "AuthCode,Refresh";
  client[8] = "yes";
  ZuCheck(!cliRequest(client, ZuArray{client}.length(), json));
  const char *missing[] = {"zum", "app", "add", "hub"};
  ZuCheck(!cliRequest(missing, ZuArray{missing}.length(), json));
  const char *extra[] = {"zum", "app", "add", "hub", "aud", "extra"};
  ZuCheck(!cliRequest(extra, ZuArray{extra}.length(), json));
  const char *wrong[] = {"zum", "app", "add", "hub", "aud", "--grants", "AuthCode,Refresh"};
  ZuCheck(!cliRequest(wrong, ZuArray{wrong}.length(), json));
  const char *legacy[] = {"zum", "app", "add", "hub", "aud", "--json", "request.json"};
  ZuCheck(!cliRequest(legacy, ZuArray{legacy}.length(), json));
  const char *catalog[] = {"zum", "catalog", "publish", "9", "{\"actions\":[]}", "1", "digest"};
  ZuCheck(cliRequest(catalog, ZuArray{catalog}.length(), json));
  ZuCheck(json == "{\"app_id\":\"9\",\"catalog\":{\"actions\":[]},"
    "\"revision\":1,\"digest\":\"digest\"}");
  const char *users[] = {"zum", "user", "list", "admin@localhost",
    "--source", "Local"};
  ZuCheck(cliRequest(users, ZuArray{users}.length(), json));
  ZuCheck(json == "{\"name\":\"admin@localhost\",\"source\":\"Local\"}");
  ZuCheck(cliRequest(users, 3, json));
  ZuCheck(json == "{}");
  const char *duplicateSource[] = {"zum", "user", "list",
    "--source", "Local", "--source", "External"};
  ZuCheck(!cliRequest(duplicateSource, ZuArray{duplicateSource}.length(), json));
  const char *shortHelp[] = {"zum", "issuer", "list", "-h"};
  ZuCheck(cliRequest(shortHelp, ZuArray{shortHelp}.length(), json));
  ZuCheck(json == "{}");
  const char *userID[] = {"zum", "user", "list", "--id", "42"};
  ZuCheck(cliRequest(userID, ZuArray{userID}.length(), json));
  ZuCheck(json == "{\"id\":\"42\"}");
  const char *namedUser[] = {"zum", "user", "list", "--name", "admin"};
  ZuCheck(!cliRequest(namedUser, ZuArray{namedUser}.length(), json));
  const char *invite[] = {"zum", "user", "add", "user@example.test"};
  ZuCheck(cliRequest(invite, ZuArray{invite}.length(), json));
  ZuCheck(json == "{\"name\":\"user@example.test\"}");
  const char *access[] = {"zum", "client", "access", "set", "9", "cli", "1,2"};
  ZuCheck(cliRequest(access, ZuArray{access}.length(), json));
  ZuCheck(json == "{\"app_id\":\"9\",\"client_id\":\"cli\",\"role_ids\":[\"1\",\"2\"]}");
  const char *policy[] = {"zum", "auth", "policy", "set", "9", "true", "Any",
    "300", "600", "3600", "900", "Always"};
  ZuCheck(cliRequest(policy, ZuArray{policy}.length(), json));
  ZuCheck(json == "{\"app_id\":\"9\",\"local_first\":true,\"eligibility_mode\":\"Any\","
    "\"assignment_max_age\":300,\"session_idle\":600,\"session_absolute\":3600,"
    "\"token_lifetime\":900,\"consent_policy\":\"Always\"}");
  const char *assignments[] = {"zum", "assign", "list", "9", "--user-id", "42"};
  ZuCheck(cliRequest(assignments, ZuArray{assignments}.length(), json));
  ZuCheck(json == "{\"app_id\":\"9\",\"user_id\":\"42\"}");
  const char *flat[] = {"zum", "userQuery"};
  ZuCheck(!cliRequest(flat, ZuArray{flat}.length(), json));
  const char *group[] = {"zum", "client", "access"};
  ZuCheck(!cliRequest(group, ZuArray{group}.length(), json));
  const char *verb[] = {"zum", "user", "unknown"};
  ZuCheck(!cliRequest(verb, ZuArray{verb}.length(), json));
}

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
  auto x = encode({point + 1, Ztls::COSE::ES256::CoordinateSize});
  auto y = encode({point + 1 + Ztls::COSE::ES256::CoordinateSize,
    Ztls::COSE::ES256::CoordinateSize});
  Zum::String jwk{"{\"kid\":\"service-key\",\"kty\":\"EC\",\"crv\":\"P-256\",\"x\":\""};
  jwk << x << "\",\"y\":\"" << y << "\"}";
  Zum::String jwks{"{\"keys\":["};
  for (unsigned i = 0; i < 7; ++i)
    jwks << "{\"kid\":\"unused-" << i << "\",\"kty\":\"EC\","
      "\"crv\":\"P-256\",\"x\":\"" << x << "\",\"y\":\"" << y << "\"},";
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
  unsigned tokens = 0, calls = 0, introspections = 0, registrations = 0;
  bool introspectionAuth = false;
  Zum::String introspectionContext;
  bool holdRenewal = false;
  Zum::ServiceHTTPDoneFn renewal;
  ZmSemaphore renewalHeld, operationDone, registrationDone;
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
      .enabled = true, .receiverID = "service", .deliveryURL = "https://service/ssf",
      .callbackAuth = "Bearer callback",
      .transmitterIssuer = "https://issuer/oauth2/8", .audience = "ping"}};
  Zum::ServiceHTTPFn http{[&token, &jwks, &tokens, &calls, &introspections,
      &introspectionAuth, &introspectionContext, &registrations, &registrationDone,
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
      body << (Zm::now().sec() + 300) << introspectionContext << '}';
      complete(Zum::ServiceHTTPResponse{200, ZuMv(body)});
    } else if (request.url == "https://issuer/admin/apps/9/ssf") {
      ++registrations;
      ZuCheck(request.method == Zum::ServiceMethod::POST &&
        request.authorization == Zum::String{"Bearer "} << token &&
        request.contentType == "application/json" &&
        request.body.find<"https://service/ssf">() >= 0 &&
        request.body.find<"Bearer callback">() >= 0);
      complete(Zum::ServiceHTTPResponse{200, Zum::String{
        "{\"expires_in\":"} << (registrations == 1 ? 2 : 300) <<
          ",\"expires\":" << (Zm::now().sec() + 300) << '}'});
      if (registrations == 2) registrationDone.post();
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
  ZuCheck(tokens == 2 && registrations == 1);
  registrationDone.wait();
  ZuCheck(tokens == 3 && registrations == 2);
  ZuCheck(ZmBlock<int>{}([&service](auto wake) mutable {
    service.publish(Zum::ServiceManifest{.revision = 1},
      [wake = ZuMv(wake)](Zum::ServiceProtocolResult result) mutable {
	wake(result.error);
      });
  }) == Zum::ServiceError::OK);
  // The signed expiry is inside renewSkew even though expires_in is not.
  ZuCheck(tokens == 4 && calls == 1);
  Zum::String unknown = encode(ZuBSpan{
    "{\"alg\":\"ES256\",\"typ\":\"at+jwt\",\"kid\":\"unknown\"}"});
  unknown << '.' << encode(ZuBSpan{"{}"}) << ".invalid";
  bool introspectOK = false;
  ZuCheck(ZmBlock<int>{}([&service, &unknown, &introspectOK](auto wake) mutable {
    service.verify(ZuMv(unknown), [&introspectOK, wake = ZuMv(wake)](int error,
        Zum::ServicePrincipal principal) mutable {
      introspectOK = error == Zum::ServiceError::OK &&
        principal.tokenID.issuerURL == "https://issuer/oauth2/9" &&
        principal.tokenID.jti == "introspected" && !principal.authMethod;
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
  auto checkContext = [&service, &introspectionContext](
      ZuCSpan context, ZuCSpan expected) {
    introspectionContext = context;
    Zum::String unknown = encode(ZuBSpan{
      "{\"alg\":\"ES256\",\"typ\":\"at+jwt\",\"kid\":\"unknown\"}"});
    unknown << '.' << encode(ZuBSpan{"{}"}) << ".invalid";
    return ZmBlock<bool>{}([&service, &unknown, expected](auto wake) mutable {
      service.verify(ZuMv(unknown), [wake = ZuMv(wake), expected](int error,
          Zum::ServicePrincipal principal) mutable {
        wake(error == Zum::ServiceError::OK &&
          (expected ? principal.authMethod == expected : !principal.authMethod));
      });
    });
  };
  ZuCheck(checkContext(",\"grant_type\":\"client_credentials\"", "client_credentials"));
  ZuCheck(checkContext(",\"amr\":[\"passkey\"]", "passkey"));
  ZuCheck(checkContext(",\"amr\":[\"oidc\"]", "oidc"));
  ZuCheck(checkContext(",\"amr\":[\"unknown\"]", ""));
  service.setRefreshRevocationFn([&refreshRevoked](Zum::RefreshID refreshID,
      int64_t expires) {
    ++refreshRevoked;
    ZuCheck(refreshID.issuerURL == "https://issuer/oauth2/9" &&
      refreshID.familyID == "family-1" && expires > Zm::now().sec());
  });
  Zum::SignKey setKey{.id = "service-key", .issuer =
    "https://issuer/oauth2/8"};
  Zum::String set;
  int64_t setNow = Zm::now().sec();
  Zum::makeSSF(setKey, "ping", Zum::RefreshID{
    .issuerURL = "https://issuer/oauth2/9", .familyID = "family-1"},
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
      .issuerURL = "https://issuer/oauth2/9", .familyID = "family-2"},
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
      .issuerURL = "https://issuer/oauth2/9", .familyID = "family-3"},
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
      .issuerURL = "https://issuer/oauth2/9", .familyID = "family-4"},
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

static Zum::String keyJWK(Ztls::PK::SK_EC &key, ZuCSpan id)
{
  uint8_t point[Ztls::COSE::ES256::PublicKeySize];
  ZuCheckRT(Ztls::Backend::pkey_ec_export_public(key.key, point));
  return Zum::String{"{\"kid\":\""} << id <<
    "\",\"kty\":\"EC\",\"crv\":\"P-256\",\"x\":\"" <<
    encode({point + 1, Ztls::COSE::ES256::CoordinateSize}) <<
    "\",\"y\":\"" << encode({point + 1 + Ztls::COSE::ES256::CoordinateSize,
      Ztls::COSE::ES256::CoordinateSize}) << "\"}";
}

static Zum::String keyJWT(Ztls::Random &rng, Ztls::PK::SK_EC &key,
    ZuCSpan id, bool workload = false)
{
  int64_t now = Zm::now().sec();
  Zum::String claims{"{\"iss\":\"https://issuer/oauth2/"};
  claims << (workload ? 7 : 9) << "\",\"sub\":\"service\","
    "\"client_id\":\"service\",\"aud\":\"" <<
    (workload ? "https://issuer/admin" : "ping") <<
    "\",\"zum_app_id\":\"" << (workload ? 7 : 9) <<
    "\",\"scope\":\"zum.catalog\",\"jti\":\"test\",\"iat\":" << now <<
    ",\"nbf\":" << now << ",\"exp\":" << now + 300 << '}';
  Zum::String header{"{\"alg\":\"ES256\",\"typ\":\"at+jwt\",\"kid\":\""};
  header << id << "\"}";
  Zum::String token = encode(header);
  token << '.' << encode(claims);
  uint8_t digest[Ztls::MD<>::Size];
  Ztls::MD<> md;
  md.update(ZuBSpan{token});
  md.finish(digest);
  Zum::Bytes der;
  auto signed_ = key.sign(rng, digest, [&der](ZuBSpan value) {
    der = Zum::Bytes{value};
  });
  uint8_t raw[Ztls::COSE::ES256::SignatureSize];
  ZuCheckRT(!signed_.template is<ZeException>() &&
    Ztls::COSE::ES256::derToRaw(der, raw));
  token << '.' << encode(raw);
  return token;
}

static Zum::String keySET(Ztls::Random &rng, Ztls::PK::SK_EC &signer,
    ZuCSpan id, ZuCSpan issuer)
{
  Zum::String token;
  int64_t now = Zm::now().sec();
  Zum::makeSSF({.id = id, .issuer = issuer}, "ping",
    {.issuerURL = "https://issuer/oauth2/9", .familyID = "family"},
    now + 120, now,
    [&rng, &signer](const Zum::SignKey &, ZuBSpan digest,
	Zum::SignatureFn complete) {
      signer.sign(rng, digest, [complete = ZuMv(complete)](ZuBSpan der) mutable {
	complete(Zum::Bytes{der});
      });
    }, [&token](Zum::String value) { token = ZuMv(value); });
  return token;
}

static Zum::String keyJWKS(ZuCSpan entries)
{
  return Zum::String{"{\"keys\":["} << entries << "]}";
}

struct KeyURLs {
  using Keys = ZuStringTL<
    "https://issuer/.well-known/oauth-authorization-server/oauth2/9",
    "https://issuer/.well-known/oauth-authorization-server/oauth2/7",
    "https://issuer/.well-known/oauth-authorization-server/oauth2/8",
    "https://issuer/oauth2/7/v1/token",
    "https://issuer/oauth2/7/v1/keys",
    "https://issuer/oauth2/9/v1/keys",
    "https://issuer/oauth2/8/v1/keys",
    "https://issuer/oauth2/9/v1/introspect",
    "https://issuer/admin/apps/9/ssf",
    "https://issuer/admin/apps/9/catalog",
    "https://issuer/oauth2/9/v1/token">;
};

// Transport inputs change only between completed service operations.
struct KeyClient {
  Zum::Service service;
  Zum::ServiceConfig config;
  Zum::String management;
  Zum::String resource;
  Zum::String ssf;
  Zum::String workload;
  unsigned resourceLoads = 0;
  unsigned managementLoads = 0;
  unsigned ssfLoads = 0;
  unsigned introspections = 0;
  bool resourceAuth = false;

  KeyClient(ZmScheduler &scheduler) : config{
    .scheduler = &scheduler, .sid = 1,
    .issuerURL = "https://issuer/oauth2/9",
    .managementIssuerURL = "https://issuer/oauth2/7",
    .managementURL = "https://issuer", .clientID = "service",
    .clientSecret = Zum::Bytes{ZuBSpan{"secret"}}, .audience = "ping",
    .jwksRefresh = 0, .keyMax = 64,
    .introspectionURL = "https://issuer/oauth2/9/v1/introspect",
    .introspectionClientID = "introspector",
    .introspectionSecret = Zum::Bytes{ZuBSpan{"secret"}}} { }

  int start() {
    ZuCheckRT(service.init(config, [this](Zum::ServiceHTTPRequest request,
	Zum::ServiceHTTPDoneFn complete) {
      constexpr auto matcher = ZuMatcher<KeyURLs>();
      switch (int url = matcher.exact(request.url)) {
	case 0: case 1: case 2: {
	  unsigned app = url == 0 ? 9 : url == 1 ? 7 : 8;
	  complete(Zum::ServiceHTTPResponse{200, Zum::String{"{\"issuer\":\"https://issuer/oauth2/"} <<
	    app << "\",\"token_endpoint\":\"https://issuer/oauth2/" << app <<
	    "/v1/token\",\"jwks_uri\":\"https://issuer/oauth2/" << app <<
	    "/v1/keys\"}"});
	  return;
	}
	case 3:
	  if (resourceAuth) { complete(Zum::ServiceHTTPResponse{401, "{}"}); return; }
	  [[fallthrough]];
	case 10:
	  complete(Zum::ServiceHTTPResponse{200, Zum::String{"{\"access_token\":\""} << workload <<
	    "\",\"expires_in\":300}"});
	  return;
	case 4: ++managementLoads; complete(Zum::ServiceHTTPResponse{200, management}); return;
	case 5: ++resourceLoads; complete(Zum::ServiceHTTPResponse{200, resource}); return;
	case 6: ++ssfLoads; complete(Zum::ServiceHTTPResponse{200, ssf}); return;
	case 7:
	  ++introspections;
	  complete(Zum::ServiceHTTPResponse{200, "{\"active\":false}"});
	  return;
	case 8:
	  complete(Zum::ServiceHTTPResponse{200, Zum::String{"{\"expires_in\":300,\"expires\":"} <<
	    Zm::now().sec() + 300 << '}'});
	  return;
	case 9: complete(Zum::ServiceHTTPResponse{200, "{}"}); return;
	default: complete(Zum::ServiceHTTPResponse{404, {}}); return;
      }
    }));
    return ZmBlock<int>{}([this](auto wake) mutable {
      service.start([wake = ZuMv(wake)](int error) mutable { wake(error); });
    });
  }

  int verify(Zum::String token) {
    return ZmBlock<int>{}([this, token = ZuMv(token)](auto wake) mutable {
      service.verify(ZuMv(token), [wake = ZuMv(wake)](int error,
	  Zum::ServicePrincipal principal) mutable {
	if (error == Zum::ServiceError::OK)
	  ZuCheckRT(principal.appID == 9 && principal.audience == "ping");
	wake(error);
      });
    });
  }

  int receive(Zum::String token) {
    return ZmBlock<int>{}([this, token = ZuMv(token)](auto wake) mutable {
      service.receiveSET({.authorization = "Bearer callback",
	.contentType = "application/secevent+jwt", .body = ZuMv(token)},
	[wake = ZuMv(wake)](int error) mutable { wake(error); });
    });
  }

  void stop() {
    ZuCheckRT(ZmBlock<int>{}([this](auto wake) mutable {
      service.stop([wake = ZuMv(wake)](int error) mutable { wake(error); });
    }) == Zum::ServiceError::OK);
    service.final();
  }
};

static void serviceKeys()
{
  ZuTestScopeRT(serviceKeys);
  Ztls::Random rng;
  ZuCheckRT(rng.init());
  Ztls::PK::SK_EC management{rng, Ztls::PK::OIDs::EC_GRP_SECP256R1};
  Ztls::PK::SK_EC a{rng, Ztls::PK::OIDs::EC_GRP_SECP256R1};
  Ztls::PK::SK_EC b{rng, Ztls::PK::OIDs::EC_GRP_SECP256R1};
  auto managementJWK = keyJWK(management, "management");
  auto workload = keyJWT(rng, management, "management", true);
  ZmSchedParams params;
  params.nThreads(1);
  params.thread(1).isolated(true);
  ZmScheduler scheduler{ZuMv(params)};
  ZuCheckRT(scheduler.start());
  // These are the former branch boundaries plus a larger configured set.
  for (unsigned count: {1U, 2U, 3U, 7U, 8U, 33U}) {
    KeyClient client{scheduler};
    client.management = keyJWKS(managementJWK);
    client.workload = workload;
    Zum::String entries;
    Zum::StringVec tokens;
    for (unsigned i = 0; i < count; ++i) {
      Ztls::PK::SK_EC key{rng, Ztls::PK::OIDs::EC_GRP_SECP256R1};
      auto id = Zum::String{"key-"} << i;
      if (i) entries << ',';
      entries << keyJWK(key, id);
      tokens.push(keyJWT(rng, key, id));
    }
    client.resource = keyJWKS(entries);
    ZuCheckRT(client.start() == Zum::ServiceError::OK);
    for (unsigned repeat = 0; repeat < 3; ++repeat)
      for (auto &token: tokens)
	ZuCheckRT(client.verify(token) == Zum::ServiceError::OK);
    ZuCheckRT(client.resourceLoads == 1 && !client.introspections);
    auto loads = client.resourceLoads;
    ZuCheckRT(client.verify(keyJWT(rng, a, "missing")) ==
      Zum::ServiceError::Unauthorized);
    ZuCheckRT(client.resourceLoads == loads + 1 && client.introspections == 1);
    // The same key ID with different case is a miss.
    ZuCheckRT(client.verify(keyJWT(rng, a, "KEY-0")) ==
      Zum::ServiceError::Unauthorized);
    loads = client.resourceLoads;
    auto introspections = client.introspections;
    ZuCheckRT(client.verify(keyJWT(rng, management, "key-0")) ==
      Zum::ServiceError::Unauthorized);
    ZuCheckRT(client.resourceLoads == loads &&
      client.introspections == introspections);
    client.stop();
  }

  KeyClient client{scheduler};
  client.management = keyJWKS(managementJWK);
  client.workload = workload;
  auto oldJWK = keyJWK(a, "old");
  auto keepA = keyJWK(a, "keep");
  auto keepB = keyJWK(b, "keep");
  auto newJWK = keyJWK(b, "new");
  auto oldToken = keyJWT(rng, a, "old");
  auto keepToken = keyJWT(rng, a, "keep");
  auto newToken = keyJWT(rng, b, "new");
  client.resource = keyJWKS(Zum::String{oldJWK} << ',' << keepA);
  ZuCheckRT(client.start() == Zum::ServiceError::OK);

  Zum::StringVec failures;
  failures.push(keyJWKS(Zum::String{keepB} << ',' << newJWK << ',' << newJWK));
  failures.push(Zum::String{"{\"keys\":["} << newJWK << ",}");
  failures.push("{\"keys\":[]}");
  failures.push(keyJWKS("{\"kid\":\"invalid\",\"kty\":\"EC\","
    "\"crv\":\"P-256\",\"x\":\"bad\",\"y\":\"bad\"}"));
  Zum::String overLimit;
  for (unsigned i = 0; i <= client.config.keyMax; ++i) {
    if (i) overLimit << ',';
    overLimit << keyJWK(a, Zum::String{"over-"} << i);
  }
  failures.push(keyJWKS(overLimit));
  for (auto &failure: failures) {
    client.resource = failure;
    ZuCheckRT(client.verify(newToken) == Zum::ServiceError::Unauthorized);
    ZuCheckRT(client.verify(oldToken) == Zum::ServiceError::OK);
    ZuCheckRT(client.verify(keepToken) == Zum::ServiceError::OK);
  }
  // Growth replaces the public key for keep and introduces new.
  client.resource = keyJWKS(Zum::String{keepB} << ',' << newJWK << ',' <<
    keyJWK(a, "extra"));
  ZuCheckRT(client.verify(newToken) == Zum::ServiceError::OK);
  ZuCheckRT(client.verify(keepToken) == Zum::ServiceError::Unauthorized);
  ZuCheckRT(client.verify(keyJWT(rng, b, "keep")) == Zum::ServiceError::OK);
  ZuCheckRT(client.verify(oldToken) == Zum::ServiceError::Unauthorized);
  // Shrink and ensure all previous entries are gone.
  client.resource = keyJWKS(keyJWK(a, "only"));
  ZuCheckRT(client.verify(keyJWT(rng, a, "only")) == Zum::ServiceError::OK);
  ZuCheckRT(client.verify(newToken) == Zum::ServiceError::Unauthorized);
  ZuCheckRT(client.verify(keyJWT(rng, b, "keep")) == Zum::ServiceError::Unauthorized);
  client.stop();

  // Collision found with ZuCSpan::hash(), checked against the active framework.
  constexpr auto left = "collision-1018"_z;
  constexpr auto right = "collision-2000"_z;
  ZuCheckRT(ZuCSpan{left}.hash() == ZuCSpan{right}.hash() && left != right);
  client.resource = keyJWKS(Zum::String{keyJWK(a, left)} << ',' << keyJWK(b, right));
  ZuCheckRT(client.start() == Zum::ServiceError::OK);
  ZuCheckRT(client.verify(keyJWT(rng, a, left)) == Zum::ServiceError::OK);
  ZuCheckRT(client.verify(keyJWT(rng, b, right)) == Zum::ServiceError::OK);
  ZuCheckRT(client.verify(keyJWT(rng, a, right)) == Zum::ServiceError::Unauthorized);
  client.resource = keyJWKS(Zum::String{keyJWK(a, left)} << ',' <<
    keyJWK(b, right) << ',' << keyJWK(b, left));
  ZuCheckRT(client.verify(newToken) == Zum::ServiceError::Unauthorized);
  ZuCheckRT(client.verify(keyJWT(rng, a, left)) == Zum::ServiceError::OK);
  ZuCheckRT(client.verify(keyJWT(rng, b, right)) == Zum::ServiceError::OK);
  client.stop();

  // Skipped records do not reserve IDs, regardless of their wire order.
  Zum::String skipped{"{\"kid\":\"new\",\"kty\":\"RSA\",\"crv\":\"P-256\","
    "\"x\":\"bad\",\"y\":\"bad\"}"};
  Zum::String invalid{"{\"kid\":\"new\",\"kty\":\"EC\",\"crv\":\"P-256\","
    "\"x\":\"bad\",\"y\":\"bad\"}"};
  uint8_t zero[Ztls::COSE::ES256::CoordinateSize] = {};
  auto coordinate = encode(zero);
  auto badPoint = Zum::String{"{\"kid\":\"new\",\"kty\":\"EC\","
    "\"crv\":\"P-256\",\"x\":\""} << coordinate <<
    "\",\"y\":\"" << coordinate << "\"}";
  for (bool reverse: {false, true}) {
    client.resource = keyJWKS(reverse ?
      Zum::String{newJWK} << ',' << skipped << ',' << invalid << ',' << badPoint :
      Zum::String{skipped} << ',' << invalid << ',' << badPoint << ',' << newJWK);
    ZuCheckRT(client.start() == Zum::ServiceError::OK);
    ZuCheckRT(client.verify(newToken) == Zum::ServiceError::OK);
    client.stop();
  }

  // Distinct issuer key material; exercise both SSF load and startup copy.
  for (bool sharedIssuer: {false, true}) {
    client.config.renewSkew = 600;
    client.config.ssf = {.enabled = true, .receiverID = "service",
      .deliveryURL = "https://service/ssf", .callbackAuth = "Bearer callback",
      .transmitterIssuer = sharedIssuer ?
	"https://issuer/oauth2/7" : "https://issuer/oauth2/8", .audience = "ping"};
    client.resource = keyJWKS(newJWK);
    client.management = keyJWKS(managementJWK);
    client.workload = workload;
    client.ssf = keyJWKS(keyJWK(a, "ssf"));
    auto ssfLoads = client.ssfLoads;
    ZuCheckRT(client.start() == Zum::ServiceError::OK);
    ZuCheckRT(client.ssfLoads == ssfLoads + (sharedIssuer ? 0 : 1));
    ZuCheckRT(client.verify(newToken) == Zum::ServiceError::OK);
    // Renew management keys after SSF's snapshot has been installed.
    client.management = keyJWKS(keyJWK(b, "management"));
    client.workload = keyJWT(rng, b, "management", true);
    ZuCheckRT(ZmBlock<int>{}([&client](auto wake) mutable {
      client.service.publish({.revision = 1},
	[wake = ZuMv(wake)](Zum::ServiceProtocolResult result) mutable {
	  wake(result.error);
	});
    }) == Zum::ServiceError::OK);
    auto id = sharedIssuer ? "management" : "ssf";
    auto issuer = client.config.ssf.transmitterIssuer;
    ZuCheckRT(client.receive(keySET(rng, sharedIssuer ? management : a,
      id, issuer)) == Zum::ServiceError::OK);
    ZuCheckRT(client.receive(keySET(rng, b, id, issuer)) ==
      Zum::ServiceError::Unauthorized);
    client.stop();
  }
  // Resource authentication fallback does not install management keys.
  {
    KeyClient fallback{scheduler};
    fallback.resourceAuth = true;
    fallback.workload = keyJWT(rng, b, "new");
    fallback.resource = keyJWKS(newJWK);
    fallback.config.ssf = {.enabled = true, .receiverID = "service",
      .deliveryURL = "https://service/ssf", .callbackAuth = "Bearer callback",
      .transmitterIssuer = "https://issuer/oauth2/7", .audience = "ping"};
    ZuCheckRT(fallback.start() == Zum::ServiceError::OK);
    ZuCheckRT(!fallback.managementLoads && !fallback.ssfLoads);
    ZuCheckRT(fallback.verify(newToken) == Zum::ServiceError::OK);
    ZuCheckRT(fallback.receive(keySET(rng, management, "management",
      "https://issuer/oauth2/7")) == Zum::ServiceError::Unauthorized);
    fallback.stop();
  }
  ZuCheckRT(scheduler.stop());
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
    .appID = 9, .receiverID = "receiver", .audience = "ping",
    .deliveryURL = "https://receiver.example/ssf", .callbackAuth = Zum::Bytes{ZuBSpan{"secret"}}};
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
      .appID = 9, .receiverID = "receiver", .audience = "ping",
      .deliveryURL = "https://receiver.example/ssf", .callbackAuth = Zum::Bytes{ZuBSpan{"secret"}}},
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
  ZuTestCall(cliArgs);
  ZuTestCall(publicAPI);
  ZuTestCall(responseLifetime);
  ZuTestCall(serviceToken);
  ZuTestCall(serviceKeys);
  ZuTestCall(stopStarting);
  ZuTestCall(ssfDelivery);
  return 0;
}
