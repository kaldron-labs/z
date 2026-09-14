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
#include <zlib/ZumMgmt.hh>

namespace Zum {

namespace ServiceMethod { enum { GET, POST, PUT }; }
namespace ServiceError {
  enum { OK = -1, Invalid, Unauthorized, Forbidden, Unavailable, Stopped };
}

struct ServiceManifest {
  CatalogData catalog;
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

struct ServiceSSFConfig {
  bool		enabled = false;
	String	receiverID;
	String	callbackPath;
	String	callbackAuth;
	String	transmitterIssuer;
	String	audience;
  unsigned	maxBytes = 64U<<10;
  uint32_t	clockSkew = 30;
  unsigned	dedupMax = 1024;
};

struct ServiceConfig {
  ZmScheduler	*scheduler = nullptr;
  unsigned	sid = 0;
  String	issuerURL;
  String	managementIssuerURL;
  String	managementURL;
  String	clientID;
  Bytes		clientSecret;
  String	audience;
  uint64_t	requestTimeout = 15;
  uint32_t	renewSkew = 30;
  uint32_t	jwksRefresh = 30;
  unsigned	responseMax = 64U<<10;
  unsigned	keyMax = 8;
	JWTLimits	jwtLimits;
	String		introspectionURL;
	String		introspectionClientID;
	Bytes		introspectionSecret;
	ServiceSSFConfig ssf;
};

struct ServiceProtocolResult {
  String body;
  unsigned status = 0;
  int error = ServiceError::Invalid;
};

struct ServiceSETRequest {
  String	authorization;
  String	contentType;
  String	body;
};

struct ServicePrincipal {
  TokenID tokenID;
  String subject;
  AppID appID = 0;
  String audience;
  StringVec actions;
  int64_t expires = 0;
};

ZuDerive(ServiceDoneFn, (ZmFn<void(int),
  ZmFnHeapID<"Zum.Service.Done">>));
ZuDerive(ServiceProtocolFn, (ZmFn<void(ServiceProtocolResult),
  ZmFnHeapID<"Zum.Service.Protocol">>));
ZuDerive(ServiceVerifyFn, (ZmFn<void(int, ServicePrincipal),
  ZmFnHeapID<"Zum.Service.Verify">>));
ZuDerive(ServiceRefreshFn, (ZmFn<void(RefreshID, int64_t),
  ZmFnHeapID<"Zum.Service.Refresh">>));
ZuDerive(ServiceSETDoneFn, (ZmFn<void(int),
  ZmFnHeapID<"Zum.Service.SETDone">>));

struct ServiceState;

class ZumAPI Service {
  Service(const Service &) = delete;
  Service &operator =(const Service &) = delete;

public:
  Service();
  ~Service();

  bool init(ServiceConfig, ServiceHTTPFn);
  void start(ServiceDoneFn);
  void verify(String accessToken, ServiceVerifyFn);
  void setRefreshRevocationFn(ServiceRefreshFn);
  void receiveSET(ServiceSETRequest, ServiceSETDoneFn);
  void publish(ServiceManifest, ServiceProtocolFn);
  void stop(ServiceDoneFn);
  void final();

private:
  ZmRef<ServiceState> m_state;
};

} // namespace Zum

#endif /* ZumService_HH */
