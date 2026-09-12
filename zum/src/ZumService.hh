//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// service-side Zum authentication and authorization client

#ifndef ZumService_HH
#define ZumService_HH

#ifndef ZumLib_HH
#include <zlib/ZumLib.hh>
#endif

#include <zlib/ZuDerive.hh>

#include <zlib/ZmScheduler.hh>
#include <zlib/ZtArray.hh>
#include <zlib/ZfJSON.hh>

#include <zlib/ZumJWTVerify.hh>

namespace Zum {

namespace ServiceMethod { enum { GET, POST, PUT }; }
namespace ServiceError {
  enum { OK = -1, Invalid, Unauthorized, Forbidden, Unavailable, Stopped };
}

struct ServiceAction {
  String label;
  String name;
};
ZfStruct(, (ServiceAction, JSON),
  (((label),		(JSON::Opt)),	(String)),
  (((name),		(Required)),	(String)));
ZuDerive(ServiceActionArray, (ZtArray<ServiceAction,
  ZtArrayHeapID<"Zum.Service.Actions">>));
struct ServiceActionVec : public ServiceActionArray {
  ZuDerive_(ServiceActionVec, ServiceActionArray);
  friend ZfJSON::AsArray<ZfFieldTC::UDT> ZfJSON_Fmt(ServiceActionVec *);
};

struct ServiceRole {
  StringVec actions;
  String label;
  String name;
};
ZfStruct(, (ServiceRole, JSON),
  (((actions),		(Required)),	(StringVec)),
  (((label),		(JSON::Opt)),	(String)),
  (((name),		(Required)),	(String)));
ZuDerive(ServiceRoleArray, (ZtArray<ServiceRole,
  ZtArrayHeapID<"Zum.Service.Roles">>));
struct ServiceRoleVec : public ServiceRoleArray {
  ZuDerive_(ServiceRoleVec, ServiceRoleArray);
  friend ZfJSON::AsArray<ZfFieldTC::UDT> ZfJSON_Fmt(ServiceRoleVec *);
};

struct ServiceScope {
  AudienceID audienceID = 0;
  String name;
  StringVec roles;
};
ZfStruct(, (ServiceScope, JSON),
  (((audienceID),	(Required, JSON::String<>)),	(UInt64)),
  (((name),		(Required)),	(String)),
  (((roles),		(Required)),	(StringVec)));
ZuDerive(ServiceScopeArray, (ZtArray<ServiceScope,
  ZtArrayHeapID<"Zum.Service.Scopes">>));
struct ServiceScopeVec : public ServiceScopeArray {
  ZuDerive_(ServiceScopeVec, ServiceScopeArray);
  friend ZfJSON::AsArray<ZfFieldTC::UDT> ZfJSON_Fmt(ServiceScopeVec *);
};

struct ServiceCatalog {
  ServiceActionVec actions;
  ServiceRoleVec roles;
  ServiceScopeVec scopes;
};
ZfStruct(, (ServiceCatalog, JSON),
  (((actions),		(Required)),	(UDT)),
  (((roles),		(Required)),	(UDT)),
  (((scopes),		(Required)),	(UDT)));

struct ServiceManifest {
  ServiceCatalog catalog;
  uint64_t revision = 0;
};

struct ServiceHTTPRequest {
  unsigned method = ServiceMethod::GET;
  String url;
  String authorization;
  String contentType;
  String ifMatch;
  String idempotencyKey;
  String body;
  ServiceManifest manifest;
  uint64_t timeout = 0;
};

struct ServiceHTTPResponse {
  unsigned status = 0;
  String body;
};

ZuDerive(ServiceHTTPDoneFn, (ZmFn<void(ServiceHTTPResponse),
  ZmFnHeapID<"Zum.Service.HTTPDone">>));
ZuDerive(ServiceHTTPFn, (ZmFn<void(ServiceHTTPRequest, ServiceHTTPDoneFn),
  ZmFnHeapID<"Zum.Service.HTTP">>));

struct ServiceConfig {
  ZmScheduler	*scheduler = nullptr;
  unsigned	sid = 0;
  String	issuerURL;
  String	clientID;
  Bytes		clientSecret;
  String	audience;
  uint64_t	requestTimeout = 15;
  uint32_t	renewSkew = 30;
  uint32_t	jwksRefresh = 30;
  unsigned	responseMax = 64U<<10;
  unsigned	keyMax = 8;
  JWTLimits	jwtLimits;
};

struct ServiceAuthorizeRequest {
  String clientID;
  String redirectURI;
  String responseType{"code"};
  String scope;
  String resource;
  String state;
  String codeChallenge;
  String codeChallengeMethod{"S256"};
  String nonce;
  String prompt;
  uint32_t maxAge = 0;
  bool statePresent = false;
  bool noncePresent = false;
  bool promptPresent = false;
  bool maxAgePresent = false;
};

struct ServiceAuthorizeResult {
  String authorizationURL;
  uint64_t expiresIn = 0;
  int error = ServiceError::Invalid;
};

struct ServiceProtocolResult {
  String body;
  unsigned status = 0;
  int error = ServiceError::Invalid;
};

struct ServicePrincipal {
  String subject;
  AppID appID = 0;
  String audience;
  StringVec actions;
  int64_t expires = 0;
};

ZuDerive(ServiceDoneFn, (ZmFn<void(int),
  ZmFnHeapID<"Zum.Service.Done">>));
ZuDerive(ServiceAuthorizeFn, (ZmFn<void(ServiceAuthorizeResult),
  ZmFnHeapID<"Zum.Service.Authorize">>));
ZuDerive(ServiceProtocolFn, (ZmFn<void(ServiceProtocolResult),
  ZmFnHeapID<"Zum.Service.Protocol">>));
ZuDerive(ServiceVerifyFn, (ZmFn<void(int, ServicePrincipal),
  ZmFnHeapID<"Zum.Service.Verify">>));

struct ServiceState;

class ZumAPI Service {
  Service(const Service &) = delete;
  Service &operator =(const Service &) = delete;

public:
  Service();
  ~Service();

  bool init(ServiceConfig, ServiceHTTPFn);
  void start(ServiceDoneFn);
  void authorize(ServiceAuthorizeRequest, ServiceAuthorizeFn);
  void token(String form, ServiceProtocolFn);
  void revoke(String form, ServiceProtocolFn);
  void verify(String accessToken, ServiceVerifyFn);
  void publish(ServiceManifest, ServiceProtocolFn);
  void stop(ServiceDoneFn);
  void final();

private:
  ZmRef<ServiceState> m_state;
};

} // namespace Zum

#endif /* ZumService_HH */
