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
  ZuTestCall(stopStarting);
  ZuTestCall(ssfDelivery);
  return 0;
}
