//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// OAuth authorization server and ping resource server example

#include <zlib/ZuDerive.hh>
#include <iostream>
#include <string.h>

#ifndef _WIN32
#include <unistd.h>
#endif

#include <zlib/ZuBase64URL.hh>

#include <zlib/ZmHash.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmList.hh>
#include <zlib/ZmRBTree.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmTime.hh>
#include <zlib/ZmTrap.hh>

#include <zlib/ZtPlatform.hh>

#include <zlib/ZtcHash.hh>
#include <zlib/ZtcHeap.hh>

#include <zlib/ZfCLI.hh>

#include <zlib/ZiDaemon.hh>
#include <zlib/ZiHashTune.hh>
#include <zlib/ZiHeapTune.hh>
#include <zlib/ZiLog.hh>

#include <zlib/ZhttpServer.hh>
#include <zlib/ZrestServer.hh>

#include <zlib/ZtlsCOSE.hh>
#include <zlib/ZtlsMD.hh>
#include <zlib/ZtlsPK.hh>
#include <zlib/ZtlsRandom.hh>
#include <zlib/ZtlsSec.hh>

#include "zrest.hh"

ZtEnumNS(, Http2Mode, int8_t, force, prefer, disable);
ZtEnumImplNS(Http2Mode);

enum {
  ReqBodyMax = 1U<<20,
  AccessLifetimeDefault = 5 * 60,
  RefreshLifetimeDefault = 24 * 60 * 60,
  BrowserLifetime = 5 * 60,
  CodeLifetime = 60,
  SessionLimit = 1024,
  CodeLimit = 4096,
  TokenLimit = 4096
};

struct Options {
  ZuCSpan	addr{"0.0.0.0"};
  unsigned	port = 8080;
  ZuCSpan	cert;
  ZuCSpan	key;
  ZuCSpan	keyLog;
  ZuCSpan	logPath{"-"};
  ZuCSpan	pidfile;
  int		eventFD = -1;
  ZuCSpan	user{"test"};
  ZuCSpan	pass{"test123"};
  ZuCSpan	accessTokenLifetime{"5m"};
  ZuCSpan	refreshTokenLifetime{"24h"};
  uint64_t	accessSecs = AccessLifetimeDefault;
  uint64_t	refreshSecs = RefreshLifetimeDefault;
  unsigned	requests = 1;
  unsigned	maxconn = 0;
  unsigned	maxPending = 256;
  unsigned	stateLimit = 0;
  unsigned	timeout = 30;
  bool		ipv6 = false;
  bool		daemon = false;
  bool		syslog = false;
  bool		noKeepalive = false;
  bool		noServerID = false;
  bool		http = true;
  bool		https = false;
  bool		http3 = false;
  bool		verbose = false;
  Http2Mode::T	http2 = Http2Mode::prefer;
  uint32_t	quicHeartbeat = 0;
#ifdef ZiMultiplex_DEBUG
  bool		debug = false;
  bool		frag = false;
  bool		yield = false;
#endif
#ifdef ZiMultiplex_FILTER
  ZuCSpan	quicRxDrop;
  ZuCSpan	quicTxDrop;
#endif
#ifdef Zquic_DEBUG
  uint32_t	quicDiag = 0;
#endif
  uint32_t	memDiag = 0;
  bool		help = false;
};

ZfStruct(, (Options, CLI),
  (addr,       (CLI::Long<"addr">),				String),
  (port,       (CLI::Long<"port">),				UInt32),
  (cert,       (CLI::Long<"cert">),				String),
  (key,        (CLI::Long<"key">),				String),
  (keyLog,     (CLI::Long<"key-log">),				String),
  (logPath,    (CLI::Long<"log">),				String),
  (pidfile,    (CLI::Long<"pidfile">),				String),
  (eventFD,    (CLI::Long<"event-fd">),				Int32),
  (user,       (CLI::Long<"user">),				String),
  (pass,       (CLI::Long<"pass">),				String),
  (accessTokenLifetime,
    (CLI::Long<"access-token-lifetime">),			String),
  (refreshTokenLifetime,
    (CLI::Long<"refresh-token-lifetime">),			String),
  (requests,   (CLI::Opt<'n'>, CLI::Long<"requests">),		UInt32),
  (maxconn,    (CLI::Long<"maxconn">),				UInt32),
  (maxPending, (CLI::Long<"max-pending">),			UInt32),
  (stateLimit, (CLI::Long<"state-limit">),			UInt32),
  (timeout,    (CLI::Long<"timeout">),				UInt32),
  (ipv6,       (CLI::Long<"ipv6">),				Bool),
  (daemon,     (CLI::Long<"daemon">),				Bool),
  (syslog,     (CLI::Long<"syslog">),				Bool),
  (noKeepalive, (CLI::Long<"no-keepalive">),			Bool),
  (noServerID,  (CLI::Long<"no-server-id">),			Bool),
  (http,       (CLI::Long<"http">),				Bool),
  (https,      (CLI::Long<"https">),				Bool),
  (http3,      (CLI::Long<"http3">),				Bool),
  (verbose,    (CLI::Flag<'v'>, CLI::Long<"verbose">),		Bool),
  (http2,      (Enum<Http2Mode::Map>, CLI::Long<"http2">),	Int8),
  (quicHeartbeat,
    (CLI::Long<"quic-heartbeat">),				UInt32),
#ifdef ZiMultiplex_DEBUG
  (debug,      (CLI::Long<"debug">),				Bool),
  (frag,       (CLI::Long<"frag">),				Bool),
  (yield,      (CLI::Long<"yield">),				Bool),
#endif
#ifdef ZiMultiplex_FILTER
  (quicRxDrop, (CLI::Long<"quic-rx-drop">),			String),
  (quicTxDrop, (CLI::Long<"quic-tx-drop">),			String),
#endif
#ifdef Zquic_DEBUG
  (quicDiag,   (CLI::Long<"quic-diag">),			UInt32),
#endif
  (memDiag,    (CLI::Long<"mem-diag">),				UInt32),
  (help,       (CLI::Flag<'h'>, CLI::Long<"help">),		Bool));

struct Empty : public ZmNHeap<Empty>, public ZmObject {
friend inline ZuStringT<"zrestd.Empty"> ZmNHeapID(Empty *);
};

struct Text : public ZmNHeap<Text>, public ZmObject {
friend inline ZuStringT<"zrestd.Text"> ZmNHeapID(Text *);

  OAuthString value;
  OAuthString cookie;
  template <typename S> friend S &operator <<(S &s, const Text &v) {
    s << v.value;
    return s;
  }
};

struct Redirect : public ZmNHeap<Redirect>, public ZmObject {
friend inline ZuStringT<"zrestd.Redirect"> ZmNHeapID(Redirect *);

  OAuthString location;
  OAuthString cookie;
};

struct BearerFailure : public ZmNHeap<BearerFailure>, public ZmObject {
friend inline ZuStringT<"zrestd.BearerFailure"> ZmNHeapID(BearerFailure *);

  OAuthString challenge;
};

struct LoginForm : public ZmNHeap<LoginForm>, public ZmObject {
friend inline ZuStringT<"zrestd.LoginForm"> ZmNHeapID(LoginForm *);

  OAuthString username;
  OAuthString password;
  OAuthString decision;
};

ZfStruct(, (LoginForm, URI),
  (username, (Required),	String),
  (password, (Required),	String),
  (decision, (Required),	String));

struct TokenForm : public ZmNHeap<TokenForm>, public ZmObject {
friend inline ZuStringT<"zrestd.TokenForm"> ZmNHeapID(TokenForm *);

  OAuthString grantType;
  OAuthString code;
  OAuthString redirectURI;
  OAuthString codeVerifier;
  OAuthString refreshToken;
  OAuthString scope;
  OAuthString clientID;
};

ZfStruct(, (TokenForm, URI),
  (grantType, (URI::ID<"grant_type">, Required),		String),
  (code, (JSON::Opt),						String),
  (redirectURI, (URI::ID<"redirect_uri">, JSON::Opt),		String),
  (codeVerifier, (URI::ID<"code_verifier">, JSON::Opt),		String),
  (refreshToken, (URI::ID<"refresh_token">, JSON::Opt),		String),
  (scope, (JSON::Opt),						String),
  (clientID, (URI::ID<"client_id">, Required),			String));

struct JWTHeader {
  OAuthString type;
  OAuthString algorithm;
  OAuthString keyID;
};
ZfStruct(, (JWTHeader, JSON),
  (type, (JSON::ID<"typ">, Required),		String),
  (algorithm, (JSON::ID<"alg">, Required),	String),
  (keyID, (JSON::ID<"kid">, Required),		String));

class App;
struct Application;
ZuDerive(OAuthAppString, (ZtString<
    ZtStringBuiltin<OAuthAppIDMax, ZtStringHeapID<"zrestd.AppID">>>));

struct AppRequest {
  OAuthAppString	appID;
};

struct AppState {
  ZmRef<Application>	application;
  OAuthString		issuerURL;
  OAuthString		audience;
  bool			valid = true;
  bool			admitted = false;

  void init(App &, const AppRequest &, const Zrest::RequestContext &);
};

struct MetadataParser;
struct AuthorizeGetParser;
struct AuthorizePostParser;
struct TokenParser;
struct RevokeParser;
struct KeysParser;
struct PingParser;

struct MetadataOK;
struct AuthorizePage;
struct AuthorizeRedirect;
struct OAuthBadRequest;
struct OAuthUnauthorized;
struct OAuthInternalError;
struct OAuthUnavailable;
struct TokenOK;
struct RevokeOK;
struct KeysOK;
struct PingOK;
struct PingUnauthorized;
struct PingForbidden;
struct NotFound;

using OAuthJSONHeaders = ZhttpHeaders(
  "content-type", "content-length",
  ("cache-control", "no-store"), ("pragma", "no-cache"));

template <typename Impl, typename Object, unsigned Status_>
struct OAuthJSONResponse : public Zrest::ResBuilder<Impl, Object> {
  using Base = Zrest::ResBuilder<Impl, Object>;
  using Base::header;
  using Headers = OAuthJSONHeaders;
  enum { Status = Status_, Body = Zrest::BodyPolicy::JSON };

  template <typename Key, typename L> void header(L &&l) const {
    if constexpr (Key{}() == "content-type") l("application/json");
    else Base::template header<Key>(ZuFwd<L>(l));
  }
};

struct MetadataOK : public OAuthJSONResponse<MetadataOK, OAuthMetadata, 200> { };
struct TokenOK : public OAuthJSONResponse<TokenOK, OAuthTokenRes, 200> { };
struct KeysOK : public OAuthJSONResponse<KeysOK, OAuthJWKS, 200> { };
struct PingOK : public OAuthJSONResponse<PingOK, Pong, 200> { };
struct OAuthBadRequest :
    public OAuthJSONResponse<OAuthBadRequest, OAuthError, 400> { };
struct OAuthUnauthorized :
    public OAuthJSONResponse<OAuthUnauthorized, OAuthError, 401> { };
struct OAuthInternalError :
    public OAuthJSONResponse<OAuthInternalError, OAuthError, 500> { };
struct OAuthUnavailable :
    public OAuthJSONResponse<OAuthUnavailable, OAuthError, 503> { };

struct NotFound : public Zrest::ResBuilder<NotFound, Empty> {
  using Headers = ZhttpHeaders("content-length");
  enum { Status = 404, Body = Zrest::BodyPolicy::Zero };
};

struct AuthorizePage : public Zrest::ResBuilder<AuthorizePage, Text> {
  using Base = Zrest::ResBuilder<AuthorizePage, Text>;
  using Base::header;
  using Headers = ZhttpHeaders(
    "content-type", "content-length",
    ("cache-control", "no-store"), "set-cookie");
  enum { Body = Zrest::BodyPolicy::Raw };

  template <typename Key, typename L> void header(L &&l) const {
    if constexpr (Key{}() == "content-type") l("text/html; charset=utf-8");
    else if constexpr (Key{}() == "set-cookie") l(this->object->cookie);
    else Base::template header<Key>(ZuFwd<L>(l));
  }
};

struct AuthorizeRedirect :
    public Zrest::ResBuilder<AuthorizeRedirect, Redirect> {
  using Base = Zrest::ResBuilder<AuthorizeRedirect, Redirect>;
  using Base::header;
  using Headers = ZhttpHeaders("location", "set-cookie", "content-length",
    ("cache-control", "no-store"));
  enum { Status = 302, Body = Zrest::BodyPolicy::Zero };

  template <typename Key, typename L> void header(L &&l) const {
    if constexpr (Key{}() == "location") l(this->object->location);
    else if constexpr (Key{}() == "set-cookie") {
      if (this->object->cookie) l(this->object->cookie);
    } else Base::template header<Key>(ZuFwd<L>(l));
  }
};

struct RevokeOK : public Zrest::ResBuilder<RevokeOK, Empty> {
  using Headers = ZhttpHeaders("content-length", ("cache-control", "no-store"));
  enum { Body = Zrest::BodyPolicy::Zero };
};

template <typename Impl, unsigned Status_>
struct BearerResponse : public Zrest::ResBuilder<Impl, BearerFailure> {
  using Base = Zrest::ResBuilder<Impl, BearerFailure>;
  using Base::header;
  using Headers = ZhttpHeaders("www-authenticate", "content-length");
  enum { Status = Status_, Body = Zrest::BodyPolicy::Zero };

  template <typename Key, typename L> void header(L &&l) const {
    if constexpr (Key{}() == "www-authenticate") l(this->object->challenge);
    else Base::template header<Key>(ZuFwd<L>(l));
  }
};
struct PingUnauthorized : public BearerResponse<PingUnauthorized, 401> { };
struct PingForbidden : public BearerResponse<PingForbidden, 403> { };

struct MetadataParser :
    public Zrest::ReqParser<MetadataParser, Empty>, public AppRequest {
  using Path = ZuStringT<"/">;
  using Headers = ZhttpHeaders("host");
  using Responses = ZuTypeList<MetadataOK, OAuthBadRequest, NotFound>;
  enum { Exact = 1 };
};

struct AuthorizeGetParser :
    public Zrest::ReqParser<AuthorizeGetParser, OAuthAuthorizeReq>,
    public AppRequest {
  using Path = ZuStringT<"/v1/authorize">;
  using Headers = ZhttpHeaders("host");
  using Responses = ZuTypeList<AuthorizePage, AuthorizeRedirect,
    OAuthBadRequest, OAuthInternalError, OAuthUnavailable, NotFound>;
  enum { Exact = 1, Query = Zrest::QueryPolicy::URI };
};

struct AuthorizePostParser :
    public Zrest::ReqParser<AuthorizePostParser, LoginForm>,
    public AppRequest {
  using Base = Zrest::ReqParser<AuthorizePostParser, LoginForm>;
  using Base::header;
  using Path = ZuStringT<"/v1/authorize">;
  using Headers = ZhttpHeaders("content-type", "cookie", "host");
  using Responses = ZuTypeList<AuthorizeRedirect, OAuthBadRequest,
    OAuthUnavailable, NotFound>;
  enum { Method = Zhttp::Method::POST, Exact = 1,
    Body = Zrest::BodyPolicy::URI };
  using Body_URI_Facet = ZuFacet::URI;
  OAuthString contentType;
  OAuthString cookie;
  unsigned contentTypeCount = 0;
  unsigned cookieCount = 0;

  template <typename Key>
  void header(Zhttp::FieldSection::T, ZuSpan<uint8_t> value) {
    if constexpr (Key{}() == "content-type") {
      ++contentTypeCount;
      if (value.length() <= 64) contentType = value;
    } else if constexpr (Key{}() == "cookie") {
      ++cookieCount;
      if (value.length() <= OAuthURLMax) cookie = value;
    }
  }
};

struct TokenParser : public Zrest::ReqParser<TokenParser, TokenForm>,
    public AppRequest {
  using Base = Zrest::ReqParser<TokenParser, TokenForm>;
  using Base::header;
  using Path = ZuStringT<"/v1/token">;
  using Headers = ZhttpHeaders("content-type", "host");
  using Responses = ZuTypeList<TokenOK, OAuthBadRequest,
    OAuthInternalError, OAuthUnavailable, NotFound>;
  enum { Method = Zhttp::Method::POST, Exact = 1,
    Body = Zrest::BodyPolicy::URI };
  using Body_URI_Facet = ZuFacet::URI;
  OAuthString contentType;
  unsigned contentTypeCount = 0;

  template <typename Key>
  void header(Zhttp::FieldSection::T, ZuSpan<uint8_t> value) {
    if constexpr (Key{}() == "content-type") {
      ++contentTypeCount;
      if (value.length() <= 64) contentType = value;
    }
  }
};

struct RevokeParser : public Zrest::ReqParser<RevokeParser, OAuthRevokeReq>,
    public AppRequest {
  using Base = Zrest::ReqParser<RevokeParser, OAuthRevokeReq>;
  using Base::header;
  using Path = ZuStringT<"/v1/revoke">;
  using Headers = ZhttpHeaders("content-type", "host");
  using Responses = ZuTypeList<RevokeOK, OAuthBadRequest, OAuthUnauthorized,
    NotFound>;
  enum { Method = Zhttp::Method::POST, Exact = 1,
    Body = Zrest::BodyPolicy::URI };
  using Body_URI_Facet = ZuFacet::URI;
  OAuthString contentType;
  unsigned contentTypeCount = 0;

  template <typename Key>
  void header(Zhttp::FieldSection::T, ZuSpan<uint8_t> value) {
    if constexpr (Key{}() == "content-type") {
      ++contentTypeCount;
      if (value.length() <= 64) contentType = value;
    }
  }
};

struct KeysParser : public Zrest::ReqParser<KeysParser, Empty>,
    public AppRequest {
  using Path = ZuStringT<"/v1/keys">;
  using Headers = ZhttpHeaders("host");
  using Responses = ZuTypeList<KeysOK, OAuthBadRequest, NotFound>;
  enum { Exact = 1 };
};

struct PingParser : public Zrest::ReqParser<PingParser, Ping>,
    public AppRequest {
  using Base = Zrest::ReqParser<PingParser, Ping>;
  using Base::header;
  using Path = ZuStringT<"/ping">;
  using Headers = ZhttpHeaders("authorization", "host");
  using Responses = ZuTypeList<PingOK, PingUnauthorized, PingForbidden,
    NotFound>;
  enum { Exact = 1, Query = Zrest::QueryPolicy::URI };
  OAuthString authorization;
  unsigned authorizationCount = 0;

  template <typename Key>
  void header(Zhttp::FieldSection::T, ZuSpan<uint8_t> value) {
    if constexpr (Key{}() == "authorization") {
      ++authorizationCount;
      if (value.length() <= OAuthTokenMax + 7) authorization = value;
    }
  }
};

using EndpointRequests = ZuTypeList<AuthorizeGetParser, AuthorizePostParser,
  TokenParser, RevokeParser, KeysParser>;
using MetadataRequests = ZuTypeList<MetadataParser>;
using ApplicationRequests = ZuTypeConcat<EndpointRequests, MetadataRequests>;
using ResourceRequests = ZuTypeList<PingParser>;
using Requests = ZuTypeConcat<ApplicationRequests, ResourceRequests>;

ZrestCatalogDerive(EndpointCatalog, EndpointRequests);
ZrestCatalogImpl(EndpointCatalog)
ZrestCatalogDerive(MetadataCatalog, MetadataRequests);
ZrestCatalogImpl(MetadataCatalog)
ZrestCatalogDerive(ResourceCatalog, ResourceRequests);
ZrestCatalogImpl(ResourceCatalog)

template <typename Catalog_>
struct RequestParser : public Zrest::MReqParser<Catalog_> {
  using Base = Zrest::MReqParser<Catalog_>;
  bool selected(ZuCSpan appID) {
    if (!appID || appID.length() > OAuthAppIDMax) return false;
    return this->u.dispatch([appID](auto, auto &request) {
      request.appID = appID;
      return true;
    });
  }
};

struct AppPrefix { ZuCSpan app; };
ZfStruct(, (AppPrefix, URI),
  (app, (URI::PathIndex<0>, Required),		String));

struct MetadataSuffix {
  ZuCSpan metadata;
  ZuCSpan oauth2;
  ZuCSpan app;
};
ZfStruct(, (MetadataSuffix, URI),
  (metadata, (URI::PathIndex<0>, Required),	String),
  (oauth2,   (URI::PathIndex<1>, Required),	String),
  (app,      (URI::PathIndex<2>, Required),	String));

struct EndpointParser : public RequestParser<EndpointCatalog> {
  using Base = RequestParser<EndpointCatalog>;

  bool operation(Zhttp::Method::T method, Zhttp::Target &target) {
    AppPrefix prefix;
    auto suffix = ZfURI::loadPathPrefix(prefix, target.path);
    if (!suffix) return false;
    auto projected = target;
    projected.path = suffix;
    return Base::operation(method, projected) && selected(prefix.app);
  }
};

struct MetadataReqParser : public RequestParser<MetadataCatalog> {
  using Base = RequestParser<MetadataCatalog>;

  bool operation(Zhttp::Method::T method, Zhttp::Target &target) {
    MetadataSuffix route;
    if (!ZfURI::loadPath(route, target.path) ||
	route.metadata != "oauth-authorization-server" ||
	route.oauth2 != "oauth2") return false;
    auto projected = target;
    projected.path = {target.path.data(), 1};
    return Base::operation(method, projected) && selected(route.app);
  }
};

struct ResourceParser : public RequestParser<ResourceCatalog> {
  using Base = RequestParser<ResourceCatalog>;

  bool operation(Zhttp::Method::T method, Zhttp::Target &target) {
    return Base::operation(method, target) && selected("ping");
  }
};

using Roots = ZuTypeList<
  Zrest::ReqRoot<ZuStringT<"oauth2">, EndpointParser>,
  Zrest::ReqRoot<ZuStringT<".well-known">, MetadataReqParser>,
  Zrest::ReqRoot<ZuStringT<"api">, ResourceParser>>;
ZrestRootCatalogDerive(RootCatalog, Roots);
ZrestRootCatalogImpl(RootCatalog)

ZrestCatalogDerive(Catalog, Requests);
ZrestCatalogImpl(Catalog)
using Decoder = Zrest::MReqParser<RootCatalog,
  Zrest::MReqRootPolicy<RootCatalog>>;
using RESTServer = Zrest::Server<App, Catalog, Decoder>;

struct BrowserTxn {
  OAuthString issuerURL;
  OAuthString clientID;
  OAuthString redirectURI;
  OAuthString scope;
  OAuthString state;
  OAuthString challenge;
  int64_t expires = 0;
};

struct CodeGrant : public BrowserTxn {
  OAuthString subject;
};

struct RefreshGrant {
  OAuthString family;
  OAuthString clientID;
  OAuthString subject;
  OAuthString scope;
  int64_t expires = 0;
  bool active = false;
};

struct FamilyState {
  int64_t expires = 0;
  bool revoked = false;
};

struct AccessState {
  OAuthString family;
  int64_t expires = 0;
  bool revoked = false;
};

using ExpiryKey = ZuTuple<int64_t, OAuthString>;

using SessionExpiries = ZmRBTree<ExpiryKey,
  ZmRBTreeUnique<true,
    ZmRBTreeLock<ZmNoLock,
      ZmRBTreeHeapID<"zrestd.SessionExpiries">>>>;
using CodeExpiries = ZmRBTree<ExpiryKey,
  ZmRBTreeUnique<true,
    ZmRBTreeLock<ZmNoLock,
      ZmRBTreeHeapID<"zrestd.CodeExpiries">>>>;
using RefreshExpiries = ZmRBTree<ExpiryKey,
  ZmRBTreeUnique<true,
    ZmRBTreeLock<ZmNoLock,
      ZmRBTreeHeapID<"zrestd.RefreshExpiries">>>>;
using FamilyExpiries = ZmRBTree<ExpiryKey,
  ZmRBTreeUnique<true,
    ZmRBTreeLock<ZmNoLock,
      ZmRBTreeHeapID<"zrestd.FamilyExpiries">>>>;
using AccessExpiries = ZmRBTree<ExpiryKey,
  ZmRBTreeUnique<true,
    ZmRBTreeLock<ZmNoLock,
      ZmRBTreeHeapID<"zrestd.AccessExpiries">>>>;

ZmHashKVDerive(BrowserTxns, OAuthString, BrowserTxn,
  (ZmHashLock<ZmNoLock, ZmHashHeapID<"zrestd.BrowserTxns">>));
ZmHashKVDerive(CodeGrants, OAuthString, CodeGrant,
  (ZmHashLock<ZmNoLock, ZmHashHeapID<"zrestd.CodeGrants">>));
ZmHashKVDerive(RefreshGrants, OAuthString, RefreshGrant,
  (ZmHashLock<ZmNoLock, ZmHashHeapID<"zrestd.RefreshGrants">>));
ZmHashKVDerive(FamilyStates, OAuthString, FamilyState,
  (ZmHashLock<ZmNoLock, ZmHashHeapID<"zrestd.FamilyStates">>));
ZmHashKVDerive(AccessStates, OAuthString, AccessState,
  (ZmHashLock<ZmNoLock, ZmHashHeapID<"zrestd.AccessStates">>));
ZmHashDerive(ClientIDs, OAuthString,
  (ZmHashLock<ZmNoLock, ZmHashHeapID<"zrestd.ClientIDs">>));

template <typename Map, typename Index>
static void expireState(Map &map, Index &index, int64_t now)
{
  while (index.count_()) {
    auto expiry = index.minimumKey();
    if (expiry.template p<0>() > now) break;
    map.del(expiry.template p<1>());
    index.del(expiry);
  }
}

struct Application : public ZmNHeap<Application>, public ZmObject {
friend inline ZuStringT<"zrestd.Application"> ZmNHeapID(Application *);

  ZmRef<Ztls::PK::SK_EC>	key;
  OAuthString		keyID;
  ZuBArray<Ztls::COSE::ES256::PublicKeySize> publicKey;
  ClientIDs		clients;
  BrowserTxns		sessions;
  CodeGrants		codes;
  RefreshGrants	refresh;
  FamilyStates		families;
  AccessStates		access;

  SessionExpiries	sessionExpiries;
  CodeExpiries		codeExpiries;
  RefreshExpiries	refreshExpiries;
  FamilyExpiries	familyExpiries;
  AccessExpiries	accessExpiries;
  bool			active = false;

  bool init(Ztls::Random &rng);
  void expire(int64_t now) {
    expireState(sessions, sessionExpiries, now);
    expireState(codes, codeExpiries, now);
    expireState(refresh, refreshExpiries, now);
    expireState(families, familyExpiries, now);
    expireState(access, accessExpiries, now);
  }
};

// Requests retain applications after a registry lookup; the registry shares
// that lifetime rather than owning an intrusive application node.
ZmHashKVDerive(Applications, OAuthString, ZmRef<Application>,
  (ZmHashLock<ZmNoLock, ZmHashHeapID<"zrestd.Applications">>));

ZuDerive(OAuthBytes, (ZtArray<uint8_t,
  ZtArrayHeapID<"zrestd.OAuthBytes">>));

static bool randomText(Ztls::Random &rng, OAuthString &value)
{
  ZuBArray<32> random(32, false);
  if (!rng.random(random)) return false;
  value.length(ZuBase64URL::enclen(random.length()));
  value.length(ZuBase64URL::encode(value.span(), random));
  return true;
}

static OAuthString digestText(ZuCSpan value)
{
  ZuBArray<Ztls::MD<>::Size> digest(Ztls::MD<>::Size, false);
  Ztls::MD<> md;
  md.update(ZuBSpan{value});
  md.finish(digest);
  OAuthString encoded;
  encoded.length(ZuBase64URL::enclen(digest.length()));
  encoded.length(ZuBase64URL::encode(encoded.span(), digest));
  return encoded;
}

static bool encodePart(OAuthSecret &out, ZuBSpan data)
{
  unsigned old = out.length();
  unsigned n = ZuBase64URL::enclen(data.length());
  if (old + n > OAuthTokenMax) return false;
  out.length(old + n);
  return ZuBase64URL::encode(out.span().offset(old), data) == n;
}

static bool decodePart(ZuCSpan encoded, OAuthBytes &decoded)
{
  if (!encoded || encoded.length() % 4 == 1) return false;
  for (auto c: encoded) if (!ZuBase64URL::is(c)) return false;
  decoded.length(ZuBase64URL::declen(encoded.length()), false);
  int n = ZuBase64URL::decode(decoded, ZuBSpan{encoded});
  if (n < 0) return false;
  decoded.length(unsigned(n));
  OAuthString canonical;
  canonical.length(ZuBase64URL::enclen(decoded.length()));
  canonical.length(ZuBase64URL::encode(canonical.span(), decoded));
  return canonical == encoded;
}

static bool scopeValid(ZuCSpan scope)
{
  return scope == "ping" || scope == "other" || scope == "ping other" ||
    scope == "other ping";
}

static bool scopeHas(ZuCSpan scope, ZuCSpan required)
{
  while (scope) {
    int i = scope.find([](char c) { return c == ' '; });
    auto item = scope;
    if (i >= 0) item.trunc(unsigned(i));
    if (item == required) return true;
    if (i < 0) break;
    scope.offset(unsigned(i) + 1);
  }
  return false;
}

static bool scopeSubset(ZuCSpan requested, ZuCSpan granted)
{
  if (!scopeValid(requested)) return false;
  while (requested) {
    int i = requested.find([](char c) { return c == ' '; });
    auto item = requested;
    if (i >= 0) item.trunc(unsigned(i));
    if (!scopeHas(granted, item)) return false;
    if (i < 0) break;
    requested.offset(unsigned(i) + 1);
  }
  return true;
}

static bool loopbackRedirect(ZuCSpan redirect)
{
  if (!redirect || redirect.length() > OAuthURLMax) return false;
  OAuthString data{redirect};
  Zhttp::URLView url{data};
  return url.ok() && url.scheme == Zhttp::Scheme::http &&
    url.explicitPort && !url.hasQuery && !url.hasFragment &&
    url.path == "/callback" &&
    (url.host == "127.0.0.1" || url.host == "::1");
}

static bool clientValid(
    const Application &application, ZuCSpan clientID, ZuCSpan redirect)
{
  return application.clients.find(clientID) && loopbackRedirect(redirect);
}

static bool formContentType(ZuCSpan value)
{
  int i = value.find([](char c) { return c == ';'; });
  if (i >= 0) value.trunc(unsigned(i));
  return value == "application/x-www-form-urlencoded";
}

static bool cookieValue(ZuCSpan cookies, OAuthString &value)
{
  static constexpr auto name = "__Host-zrest_auth="_z;
  while (cookies) {
    while (cookies && cookies[0] == ' ') cookies.offset(1);
    int end = cookies.find([](char c) { return c == ';'; });
    auto cookie = cookies;
    if (end >= 0) cookie.trunc(unsigned(end));
    if (cookie.length() > name.length() &&
        cookie.prefix(name) == int(name.length())) {
      value = cookie.offset(name.length());
      return true;
    }
    if (end < 0) break;
    cookies.offset(unsigned(end) + 1);
  }
  return false;
}

bool Application::init(Ztls::Random &rng)
{
  try {
    key = new Ztls::PK::SK_EC{rng, Ztls::PK::OIDs::EC_GRP_SECP256R1};
  } catch (...) {
    return false;
  }
  publicKey.length(Ztls::COSE::ES256::PublicKeySize);
  if (!randomText(rng, keyID) ||
      !Ztls::Backend::pkey_ec_export_public(key->key, publicKey)) return false;
  keyID.length(16);
  active = true;
  return true;
}

static bool jwtIssue(Application &application, Ztls::Random &rng,
    ZuCSpan issuerURL, ZuCSpan audience, ZuCSpan subject, ZuCSpan clientID,
    ZuCSpan scope, int64_t now, int64_t expires,
    OAuthSecret &token, OAuthString &tokenID)
{
  if (!randomText(rng, tokenID)) return false;
  JWTHeader header{.type = "at+jwt", .algorithm = "ES256",
    .keyID = application.keyID};
  OAuthAccessClaims claims{
    .issuerURL = issuerURL, .audience = audience, .subject = subject,
    .clientID = clientID, .scope = scope, .issuedAt = now,
    .notBefore = now, .expiry = expires, .tokenID = tokenID};
  OAuthString headerJSON, claimsJSON;
  ZfJSON::save(headerJSON, header);
  ZfJSON::save(claimsJSON, claims);
  token.null();
  if (!encodePart(token, ZuBSpan{headerJSON})) return false;
  token << '.';
  if (!encodePart(token, ZuBSpan{claimsJSON})) return false;
  ZuBArray<Ztls::MD<>::Size> digest(Ztls::MD<>::Size, false);
  Ztls::MD<> md;
  md.update(ZuBSpan{token});
  md.finish(digest);
  bool encoded = false;
  auto result = application.key->sign(rng, digest,
    [&token, &encoded](ZuBSpan der) {
      ZuBArray<Ztls::COSE::ES256::SignatureSize> raw(
        Ztls::COSE::ES256::SignatureSize, false);
      if (!Ztls::COSE::ES256::derToRaw(der, raw)) return;
      token << '.';
      encoded = encodePart(token, raw);
    });
  return !result.template is<ZeException>() && encoded;
}

static bool jwtVerify(Application &application, ZuCSpan token,
    ZuCSpan issuerURL, ZuCSpan audience, int64_t now, OAuthAccessClaims &claims)
{
  int first = token.find([](char c) { return c == '.'; });
  if (first <= 0) return false;
  auto rest = token;
  rest.offset(unsigned(first) + 1);
  int second = rest.find([](char c) { return c == '.'; });
  if (second <= 0) return false;
  auto signaturePart = rest;
  signaturePart.offset(unsigned(second) + 1);
  if (signaturePart.find([](char c) { return c == '.'; }) >= 0) return false;
  ZuCSpan headerPart{token.data(), unsigned(first)};
  ZuCSpan claimsPart{rest.data(), unsigned(second)};
  OAuthBytes headerData, claimsData, signature;
  if (!decodePart(headerPart, headerData) ||
      !decodePart(claimsPart, claimsData) ||
      !decodePart(signaturePart, signature) ||
      signature.length() != Ztls::COSE::ES256::SignatureSize) return false;
  JWTHeader header;
  try {
    auto headerScan = ZfJSON::scan(headerData.span());
    auto claimsScan = ZfJSON::scan(claimsData.span());
    if (headerScan.p<0>() != int(headerData.length()) || !headerScan.p<1>() ||
        claimsScan.p<0>() != int(claimsData.length()) || !claimsScan.p<1>())
      return false;
    auto headerHandler = ZfJSON::handler<JWTHeader>((*headerScan.p<1>())[0]);
    auto claimsHandler = ZfJSON::handler<OAuthAccessClaims>(
      (*claimsScan.p<1>())[0]);
    if (!headerHandler.valid || !claimsHandler.valid) return false;
    headerHandler.load(header);
    claimsHandler.load(claims);
  } catch (...) {
    return false;
  }
  if (header.type != "at+jwt" || header.algorithm != "ES256" ||
      header.keyID != application.keyID || claims.issuerURL != issuerURL ||
      claims.audience != audience || !claims.subject || !claims.clientID ||
      !claims.scope || !claims.tokenID || claims.issuedAt <= 0 ||
      claims.notBefore > now || claims.expiry <= now ||
      claims.expiry <= claims.issuedAt) return false;
  ZuCSpan signedData{token.data(), unsigned(first) + 1 + unsigned(second)};
  ZuBArray<Ztls::COSE::ES256::DERMax> der(
    Ztls::COSE::ES256::DERMax, false);
  unsigned derLength;
  if (!Ztls::COSE::ES256::rawToDER(signature, der, derLength)) return false;
  return Ztls::COSE::ES256::verify(application.publicKey,
    ZuBSpan{signedData}, ZuBSpan{der.data(), derLength});
}

class App {
public:
  App(const Options *options) : m_options{options} { }

  bool init() { return m_rng.init(); }
  bool activate(ZuCSpan appID) {
    if (!appID || appID.length() > OAuthAppIDMax || m_applications.find(appID))
      return false;
    ZmRef<Application> application = new Application{};
    if (!application->init(m_rng)) return false;
    if (appID == "ping") {
      application->clients.add(OAuthString{"zrest-native"});
      application->clients.add(OAuthString{"zrest-go"});
    } else if (appID == "other")
      application->clients.add(OAuthString{"zrest-other"});
    m_applications.add(OAuthString{appID}, application);
    return true;
  }
  bool suspend(ZuCSpan appID) {
    auto application = find(appID);
    if (!application) return false;
    application->active = false;
    return true;
  }
  bool reactivate(ZuCSpan appID) {
    auto application = find(appID);
    if (!application) return false;
    application->active = true;
    return true;
  }
  bool remove(ZuCSpan appID) { return bool(m_applications.del(appID)); }
  ZmRef<Application> find(ZuCSpan appID) const {
    return m_applications.findVal(appID);
  }

  template <typename Req>
  void request(Req *, const Req &parser,
      const Zrest::RequestContext &context,
      ZmRef<Zrest::Completion<Req, RESTServer>> token);

  template <typename Token, typename Request>
  void notFound(Token *token, const Request &request) {
    send_<NotFound>(token, request, new Empty{});
  }

  template <typename Token>
  void metadata(Token *token, const MetadataParser &parser,
      const AppState &state, bool ok) {
    if (!ok) { error_<OAuthBadRequest>(token, parser, "invalid_request",
      "invalid metadata route"); return; }
    state.application->expire(Zm::now().sec());
    ZmRef<OAuthMetadata> value = new OAuthMetadata{};
    value->issuerURL = state.issuerURL;
    value->authorizationEndpoint << state.issuerURL << "/v1/authorize";
    value->tokenEndpoint << state.issuerURL << "/v1/token";
    value->revocationEndpoint << state.issuerURL << "/v1/revoke";
    value->jwksURI << state.issuerURL << "/v1/keys";
    value->responseTypes.push("code");
    value->grantTypes.push("authorization_code");
    value->grantTypes.push("refresh_token");
    value->codeChallengeMethods.push("S256");
    value->scopes.push("ping");
    value->scopes.push("other");
    value->tokenAuthMethods.push("none");
    value->revokeAuthMethods.push("none");
    send_<MetadataOK>(token, parser, value.ptr());
  }

  template <typename Token>
  void authorize(Token *, const AuthorizeGetParser &, const AppState &, bool);
  template <typename Token>
  void authorize(Token *, const AuthorizePostParser &, const AppState &, bool);
  template <typename Token>
  void token(Token *, const TokenParser &, const AppState &, bool);
  template <typename Token>
  void revoke(Token *, const RevokeParser &, const AppState &, bool);

  template <typename Token>
  void keys(Token *token, const KeysParser &parser,
      const AppState &state, bool ok) {
    if (!ok) { error_<OAuthBadRequest>(token, parser, "invalid_request",
      "invalid keys route"); return; }
    state.application->expire(Zm::now().sec());
    ZmRef<OAuthJWKS> value = new OAuthJWKS{};
    OAuthJWK key;
    key.keyID = state.application->keyID;
    key.keyType = "EC";
    key.curve = "P-256";
    key.use = "sig";
    key.algorithm = "ES256";
    key.x.length(ZuBase64URL::enclen(Ztls::COSE::ES256::CoordinateSize));
    key.x.length(ZuBase64URL::encode(key.x.span(), {
      state.application->publicKey.data() + 1,
      Ztls::COSE::ES256::CoordinateSize}));
    key.y.length(ZuBase64URL::enclen(Ztls::COSE::ES256::CoordinateSize));
    key.y.length(ZuBase64URL::encode(key.y.span(), {
      state.application->publicKey.data() + 1 +
        Ztls::COSE::ES256::CoordinateSize,
      Ztls::COSE::ES256::CoordinateSize}));
    value->keys.push(ZuMv(key));
    send_<KeysOK>(token, parser, value.ptr());
  }

  template <typename Token>
  void ping(Token *token, const PingParser &parser,
      const AppState &state, bool ok) {
    state.application->expire(Zm::now().sec());
    auto authorization = parser.authorization.cspan();
    auto bearer = authorization;
    if (bearer.length() > 7) bearer.offset(7);
    if (!ok || !parser.object->ping || parser.authorizationCount != 1 ||
        authorization.length() <= 7 ||
        authorization.prefix("Bearer ") != 7 ||
        bearer.find([](char c) { return c == ' '; }) >= 0) {
      bearer_<PingUnauthorized>(token, parser, "invalid_token");
      return;
    }
    OAuthAccessClaims claims;
    if (!jwtVerify(*state.application, bearer, state.issuerURL,
        state.audience, Zm::now().sec(), claims)) {
      bearer_<PingUnauthorized>(token, parser, "invalid_token");
      return;
    }
    auto access = state.application->access.find(claims.tokenID);
    auto family = access ?
      state.application->families.find(access->val().family) : nullptr;
    if (!access || !family || access->val().revoked || family->val().revoked) {
      bearer_<PingUnauthorized>(token, parser, "invalid_token");
      return;
    }
    if (!scopeHas(claims.scope, "ping")) {
      bearer_<PingForbidden>(token, parser, "insufficient_scope");
      return;
    }
    ZmRef<Pong> pong = new Pong{};
    pong->pong = true;
    send_<PingOK>(token, parser, pong.ptr());
    pong_();
  }

  void listening(int, unsigned);
  void listenFailed(int, bool) { fail_(); }
  void connected(int) { }
  void disconnected(int) { }
  void responseComplete() {
    if (!m_finishing) return;
    m_finishing = false;
    finish_();
  }
  unsigned errors() const { return m_errors; }
  unsigned processed() const { return m_pong; }
  void transportFailed() { ++m_errors; }

private:
  template <typename Response, typename Token, typename Request, typename Object>
  void send_(Token *token, const Request &, Object *object,
      bool disconnect = false) {
    Zrest::Reply<Response> reply{ZmRef<Object>{object}};
    reply.codec.disconnect_ = disconnect || m_options->noKeepalive;
    reply.codec.terminal_ = disconnect;
    (void)token->complete(ZuMv(reply));
  }

  template <typename Response, typename Token, typename Request>
  void error_(Token *token, const Request &request, ZuCSpan code,
      ZuCSpan description) {
    ZmRef<OAuthError> error = new OAuthError{};
    error->error = code;
    error->errorDescription = description;
    send_<Response>(token, request, error.ptr());
  }

  template <typename Response, typename Token>
  void bearer_(Token *token, const PingParser &request, ZuCSpan code) {
    ZmRef<BearerFailure> failure = new BearerFailure{};
    failure->challenge << "Bearer error=\"" << code << '"';
    send_<Response>(token, request, failure.ptr());
  }

  bool issuePair_(Application &, ZuCSpan, ZuCSpan, ZuCSpan, ZuCSpan,
    ZuCSpan, OAuthTokenRes &, OAuthString = {});
  template <typename Token, typename Request>
  void redirectError_(Token *, const Request &, ZuCSpan, ZuCSpan, ZuCSpan,
    ZuCSpan);
  void event_(ZuCSpan);
  void fail_();
  void signal_(bool);
  void finish_();
  void pong_();

  const Options	*m_options;
  Ztls::Random	m_rng;
  Applications	m_applications;
  unsigned	m_authorize = 0;
  unsigned	m_token = 0;
  unsigned	m_refresh = 0;
  unsigned	m_revoke = 0;
  unsigned	m_pong = 0;
  unsigned	m_errors = 0;
  bool		m_finishing = false;
};

void AppState::init(App &app, const AppRequest &request,
    const Zrest::RequestContext &context)
{
  application = app.find(request.appID);
  admitted = application && application->active;
  valid = context.ok();
  issuerURL.null();
  issuerURL << Zhttp::Scheme::name(context.scheme) << "://" <<
    context.authority << "/oauth2/" << request.appID;
  audience.null();
  audience << Zhttp::Scheme::name(context.scheme) << "://" <<
    context.authority <<
    "/api/ping";
}

template <typename Req>
void App::request(Req *, const Req &parser,
    const Zrest::RequestContext &context,
    ZmRef<Zrest::Completion<Req, RESTServer>> completion)
{
  AppState state;
  state.init(*this, parser, context);
  auto token = completion.ptr();
  if (!state.admitted) { notFound(token, parser); return; }
  bool ok = state.valid;
  if constexpr (ZuIsSame<Req, MetadataParser>{})
    metadata(token, parser, state, ok);
  else if constexpr (ZuIsSame<Req, AuthorizeGetParser>{} ||
      ZuIsSame<Req, AuthorizePostParser>{})
    authorize(token, parser, state, ok);
  else if constexpr (ZuIsSame<Req, TokenParser>{})
    this->token(token, parser, state, ok);
  else if constexpr (ZuIsSame<Req, RevokeParser>{})
    revoke(token, parser, state, ok);
  else if constexpr (ZuIsSame<Req, KeysParser>{})
    keys(token, parser, state, ok);
  else if constexpr (ZuIsSame<Req, PingParser>{})
    ping(token, parser, state, ok);
}

template <typename Token, typename Request>
void App::redirectError_(Token *token, const Request &request,
    ZuCSpan redirect, ZuCSpan state, ZuCSpan issuerURL, ZuCSpan code)
{
  ZmRef<Redirect> response = new Redirect{};
  response->location = redirect;
  OAuthAuthorizeErrorRes query{
    .error = code, .errorDescription = "invalid authorization request",
    .state = state, .issuerURL = issuerURL};
  ZfURI::save(response->location, query);
  send_<AuthorizeRedirect>(token, request, response.ptr());
}

template <typename Token>
void App::authorize(
    Token *token, const AuthorizeGetParser &parser,
    const AppState &state, bool ok)
{
  int64_t now = Zm::now().sec();
  state.application->expire(now);
  const auto &request = *parser.object;
  bool redirectOK = clientValid(
    *state.application, request.clientID, request.redirectURI);
  bool valid = ok && state.valid && request.responseType == "code" &&
    redirectOK && scopeValid(request.scope) && request.state &&
    request.state.length() <= OAuthStateMax &&
    request.codeChallenge.length() == 43 &&
    request.codeChallengeMethod == "S256";
  if (!valid) {
    if (redirectOK)
      redirectError_(token, parser, request.redirectURI, request.state,
        state.issuerURL, "invalid_request");
    else
      error_<OAuthBadRequest>(token, parser,
        request.clientID ? "unauthorized_client" : "invalid_request",
        "invalid authorization request");
    return;
  }
  unsigned sessionLimit = m_options->stateLimit ?
    m_options->stateLimit : SessionLimit;
  if (state.application->sessions.count_() >= sessionLimit) {
    error_<OAuthUnavailable>(token, parser, "temporarily_unavailable",
      "authorization busy");
    return;
  }
  OAuthString session;
  if (!randomText(m_rng, session)) {
    error_<OAuthInternalError>(token, parser, "server_error",
      "authorization unavailable");
    fail_();
    return;
  }
  BrowserTxn transaction{
    .issuerURL = state.issuerURL, .clientID = request.clientID,
    .redirectURI = request.redirectURI, .scope = request.scope,
    .state = request.state, .challenge = request.codeChallenge,
    .expires = now + BrowserLifetime};
  auto key = digestText(session);
  state.application->sessionExpiries.add(
    ExpiryKey{transaction.expires, key});
  state.application->sessions.add(ZuMv(key), ZuMv(transaction));
  static constexpr auto page =
    "<!doctype html><html><head><meta charset=\"utf-8\">"
    "<title>Authorize ping</title></head><body><main>"
    "<h1>Authorize ping</h1><form method=\"post\">"
    "<label>Username <input name=\"username\" autocomplete=\"username\" "
    "required></label><label>Password <input name=\"password\" "
    "type=\"password\" autocomplete=\"current-password\" required></label>"
    "<button name=\"decision\" value=\"approve\" type=\"submit\">"
    "Approve</button><button name=\"decision\" value=\"deny\" "
    "type=\"submit\">Deny</button></form></main></body></html>"_z;
  ZmRef<Text> response = new Text{};
  response->value = page;
  response->cookie << "__Host-zrest_auth=" << session <<
    "; Path=/; Secure; HttpOnly; SameSite=Lax; Max-Age=300";
  send_<AuthorizePage>(token, parser, response.ptr());
}

template <typename Token>
void App::authorize(
    Token *token, const AuthorizePostParser &parser,
    const AppState &state, bool ok)
{
  int64_t now = Zm::now().sec();
  state.application->expire(now);
  OAuthString session;
  if (!ok || !state.valid || parser.contentTypeCount != 1 ||
      !formContentType(parser.contentType) || parser.cookieCount != 1 ||
      !cookieValue(parser.cookie, session)) {
    error_<OAuthBadRequest>(token, parser, "invalid_request",
      "invalid authorization form");
    return;
  }
  auto key = digestText(session);
  auto node = state.application->sessions.find(key);
  if (!node) {
    error_<OAuthBadRequest>(token, parser, "invalid_request",
      "authorization session missing");
    return;
  }
  BrowserTxn transaction = node->val();
  state.application->sessions.del(key);
  state.application->sessionExpiries.del(
    ExpiryKey{transaction.expires, key});
  ZmRef<Redirect> response = new Redirect{};
  response->cookie = "__Host-zrest_auth=; Path=/; Secure; HttpOnly; "
    "SameSite=Lax; Max-Age=0";
  if (transaction.expires <= now ||
      transaction.issuerURL != state.issuerURL) {
    error_<OAuthBadRequest>(token, parser, "invalid_request",
      "authorization session expired");
    return;
  }
  if (parser.object->decision != "approve" ||
      parser.object->username != m_options->user ||
      parser.object->password != m_options->pass) {
    response->location = transaction.redirectURI;
    OAuthAuthorizeErrorRes query{
      .error = "access_denied", .errorDescription = "access denied",
      .state = transaction.state, .issuerURL = transaction.issuerURL};
    ZfURI::save(response->location, query);
    send_<AuthorizeRedirect>(token, parser, response.ptr());
    return;
  }
  if (!state.application->active) {
    response->location = transaction.redirectURI;
    OAuthAuthorizeErrorRes query{
      .error = "temporarily_unavailable",
      .errorDescription = "application unavailable",
      .state = transaction.state, .issuerURL = transaction.issuerURL};
    ZfURI::save(response->location, query);
    send_<AuthorizeRedirect>(token, parser, response.ptr());
    return;
  }
  unsigned codeLimit = m_options->stateLimit ?
    m_options->stateLimit : CodeLimit;
  if (state.application->codes.count_() >= codeLimit) {
    response->location = transaction.redirectURI;
    OAuthAuthorizeErrorRes query{
      .error = "temporarily_unavailable",
      .errorDescription = "authorization busy",
      .state = transaction.state, .issuerURL = transaction.issuerURL};
    ZfURI::save(response->location, query);
    send_<AuthorizeRedirect>(token, parser, response.ptr());
    return;
  }
  OAuthString code;
  if (!randomText(m_rng, code)) {
    response->location = transaction.redirectURI;
    OAuthAuthorizeErrorRes query{
      .error = "server_error", .errorDescription = "authorization failed",
      .state = transaction.state, .issuerURL = transaction.issuerURL};
    ZfURI::save(response->location, query);
    send_<AuthorizeRedirect>(token, parser, response.ptr());
    fail_();
    return;
  }
  CodeGrant grant;
  static_cast<BrowserTxn &>(grant) = transaction;
  grant.expires = now + CodeLifetime;
  grant.subject = m_options->user;
  auto codeKey = digestText(code);
  state.application->codeExpiries.add(ExpiryKey{grant.expires, codeKey});
  state.application->codes.add(ZuMv(codeKey), ZuMv(grant));
  response->location = transaction.redirectURI;
  OAuthAuthorizeCodeRes query{
    .code = code, .state = transaction.state, .issuerURL = transaction.issuerURL};
  ZfURI::save(response->location, query);
  send_<AuthorizeRedirect>(token, parser, response.ptr());
  ++m_authorize;
  event_("authorize");
}

bool App::issuePair_(Application &application, ZuCSpan issuerURL,
    ZuCSpan audience, ZuCSpan subject, ZuCSpan clientID, ZuCSpan scope,
    OAuthTokenRes &response, OAuthString family)
{
  int64_t now = Zm::now().sec();
  bool newFamily = !family;
  FamilyState familyState;
  if (newFamily) {
    unsigned tokenLimit = m_options->stateLimit ?
      m_options->stateLimit : TokenLimit;
    if (application.families.count_() >= tokenLimit ||
        !randomText(m_rng, family)) return false;
    familyState.expires = now + int64_t(m_options->refreshSecs);
  } else {
    auto node = application.families.find(family);
    if (!node || node->val().revoked || node->val().expires <= now)
      return false;
    familyState = node->val();
  }
  unsigned tokenLimit = m_options->stateLimit ?
    m_options->stateLimit : TokenLimit;
  if (application.refresh.count_() >= tokenLimit ||
      application.access.count_() >= tokenLimit) return false;
  OAuthString refresh, tokenID;
  if (!randomText(m_rng, refresh) ||
      !jwtIssue(application, m_rng, issuerURL, audience, subject, clientID,
        scope, now, now + int64_t(m_options->accessSecs),
        response.accessToken, tokenID)) return false;
  auto refreshKey = digestText(refresh);
  application.refresh.add(refreshKey, RefreshGrant{
    .family = family, .clientID = clientID, .subject = subject,
    .scope = scope, .expires = familyState.expires, .active = true});
  application.refreshExpiries.add(
    ExpiryKey{familyState.expires, ZuMv(refreshKey)});
  if (newFamily) {
    application.familyExpiries.add(ExpiryKey{familyState.expires, family});
    application.families.add(family, familyState);
  }
  application.access.add(tokenID, AccessState{
    .family = family, .expires = now + int64_t(m_options->accessSecs)});
  application.accessExpiries.add(ExpiryKey{
    now + int64_t(m_options->accessSecs), ZuMv(tokenID)});
  response.tokenType = "Bearer";
  response.expiresIn = m_options->accessSecs;
  response.refreshToken = refresh;
  response.scope = scope;
  return true;
}

template <typename Token>
void App::token(Token *token, const TokenParser &parser,
    const AppState &state, bool ok)
{
  int64_t now = Zm::now().sec();
  state.application->expire(now);
  if (!ok || !state.valid || parser.contentTypeCount != 1 ||
      !formContentType(parser.contentType)) {
    error_<OAuthBadRequest>(token, parser, "invalid_request",
      "form body required");
    return;
  }
  const auto &request = *parser.object;
  ZmRef<OAuthTokenRes> response = new OAuthTokenRes{};
  if (request.grantType == "authorization_code") {
    bool shape = request.code && request.redirectURI && request.clientID &&
      request.codeVerifier && !request.refreshToken && !request.scope;
    auto key = digestText(request.code);
    auto node = shape ? state.application->codes.find(key) : nullptr;
    auto verifierChallenge = digestText(request.codeVerifier);
    bool valid = node && node->val().expires > now &&
      node->val().issuerURL == state.issuerURL &&
      node->val().clientID == request.clientID &&
      node->val().redirectURI == request.redirectURI &&
      request.codeVerifier.length() >= 43 &&
      request.codeVerifier.length() <= OAuthVerifierMax &&
      Ztls::ctEqual(ZuBSpan{verifierChallenge},
        ZuBSpan{node->val().challenge});
    if (!valid) {
      error_<OAuthBadRequest>(token, parser, "invalid_grant",
        "authorization code is invalid");
      return;
    }
    CodeGrant grant = node->val();
    state.application->codes.del(key);
    state.application->codeExpiries.del(ExpiryKey{grant.expires, key});
    if (!state.application->active ||
        !issuePair_(*state.application, state.issuerURL, state.audience,
          grant.subject, grant.clientID, grant.scope, *response)) {
      error_<OAuthInternalError>(token, parser, "server_error",
        "token issuance failed");
      return;
    }
    ++m_token;
    event_("token");
  } else if (request.grantType == "refresh_token") {
    bool shape = request.refreshToken && request.clientID &&
      !request.code && !request.redirectURI && !request.codeVerifier;
    auto key = digestText(request.refreshToken);
    auto node = shape ? state.application->refresh.find(key) : nullptr;
    if (node && !node->val().active) {
      if (auto family = state.application->families.find(node->val().family))
        family->val().revoked = true;
    }
    auto family = node ?
      state.application->families.find(node->val().family) : nullptr;
    OAuthString scope = request.scope ? request.scope :
      (node ? node->val().scope : OAuthString{});
    bool valid = node && family && node->val().active &&
      node->val().expires > now && !family->val().revoked &&
      node->val().clientID == request.clientID &&
      scopeSubset(scope, node->val().scope);
    if (!valid) {
      error_<OAuthBadRequest>(token, parser, "invalid_grant",
        "refresh token is invalid");
      return;
    }
    RefreshGrant grant = node->val();
    node->val().active = false;
    if (!state.application->active ||
        !issuePair_(*state.application, state.issuerURL, state.audience,
          grant.subject, grant.clientID, scope, *response, grant.family)) {
      error_<OAuthInternalError>(token, parser, "server_error",
        "token issuance failed");
      return;
    }
    ++m_refresh;
    event_("refresh");
  } else {
    error_<OAuthBadRequest>(token, parser, "unsupported_grant_type",
      "grant type is unsupported");
    return;
  }
  send_<TokenOK>(token, parser, response.ptr());
}

template <typename Token>
void App::revoke(Token *token, const RevokeParser &parser,
    const AppState &state, bool ok)
{
  state.application->expire(Zm::now().sec());
  if (!ok || !state.valid || parser.contentTypeCount != 1 ||
      !formContentType(parser.contentType) || !parser.object->token ||
      !parser.object->clientID) {
    error_<OAuthBadRequest>(token, parser, "invalid_request",
      "invalid revocation request");
    return;
  }
  auto clientID = parser.object->clientID.span();
  if (!state.application->clients.find(clientID)) {
    error_<OAuthUnauthorized>(token, parser, "invalid_client",
      "unknown public client");
    return;
  }
  if (auto refresh = state.application->refresh.find(
      digestText(parser.object->token));
      refresh && refresh->val().clientID == clientID) {
    if (auto family = state.application->families.find(refresh->val().family))
      family->val().revoked = true;
  }
  OAuthAccessClaims claims;
  if (jwtVerify(*state.application, parser.object->token, state.issuerURL,
      state.audience, Zm::now().sec(), claims) && claims.clientID == clientID) {
    if (auto access = state.application->access.find(claims.tokenID))
      access->val().revoked = true;
  }
  ++m_revoke;
  event_("revoke");
  m_finishing = m_pong >= m_options->requests;
  send_<RevokeOK>(token, parser, new Empty{}, m_finishing);
}


ZmSemaphore done;

static bool parseDuration(ZuCSpan value, uint64_t &seconds)
{
  if (value.length() < 2) return false;
  uint64_t n = 0;
  unsigned end = value.length() - 1;
  for (unsigned i = 0; i < end; ++i) {
    auto c = value[i];
    if (c < '0' || c > '9' ||
        n > (UINT64_MAX - uint64_t(c - '0')) / 10) return false;
    n = n * 10 + uint64_t(c - '0');
  }
  uint64_t scale;
  switch (value[end]) {
    case 's': scale = 1; break;
    case 'm': scale = 60; break;
    case 'h': scale = 3600; break;
    default: return false;
  }
  if (!n || n > UINT64_MAX / scale) return false;
  seconds = n * scale;
  return true;
}

static void usage(int code = 1)
{
  std::cerr <<
    "Usage: zrestd [OPTION]...\n\n"
    "Options:\n"
    "  --port=N           listen port, default 8080\n"
    "  --addr=IP          listen address, default all interfaces\n"
    "  --ipv6             listen on IPv6 address\n"
    "  --daemon           detach and run in background\n"
    "  --pidfile=NAME     write PID beneath the temporary directory\n"
    "  --maxconn=N        maximum concurrent accepted connections\n"
    "  --max-pending=N    maximum accepted REST requests, default 256\n"
    "  --state-limit=N    override each OAuth state limit for testing\n"
    "  --log=PATH         write log to PATH, '-' for stderr\n"
    "  --syslog           send log to syslog\n"
    "  -n, --requests=N   process N ping requests then exit, default 1\n"
    "  --no-keepalive     close each connection after its response\n"
    "  --no-server-id     omit server identity headers\n"
    "  --timeout=N        idle connection timeout, default 30\n"
    "  --http             enable HTTP/1.1 over TCP, default\n"
    "  --https            enable HTTP/1.1 or HTTP/2 over TLS\n"
    "  --http2=MODE       HTTP/2 mode: force, prefer, disable\n"
    "  --http3            enable HTTP/3 over QUIC\n"
    "  --cert=PATH        TLS certificate for --https/--http3\n"
    "  --key=PATH         TLS private key for --https/--http3\n"
    "  --key-log=PATH     append HTTP/3 TLS secrets\n"
    "  --quic-heartbeat=N send QUIC PING after N idle seconds\n"
#ifdef ZiMultiplex_DEBUG
    "  --debug            enable multiplex and HTTP/3 debugging\n"
    "  --frag             fragment multiplex I/O in debug builds\n"
    "  --yield            yield in multiplex in debug builds\n"
#endif
#ifdef ZiMultiplex_FILTER
    "  --quic-rx-drop=N%  randomly drop N% of received QUIC packets\n"
    "  --quic-tx-drop=N%  randomly drop N% of transmitted QUIC packets\n"
#endif
#ifdef Zquic_DEBUG
    "  --quic-diag=N      print HTTP/3 counters every N seconds\n"
#endif
    "  --mem-diag=N       print memory counters every N seconds\n"
    "  --event-fd=N       emit process-supervision events\n"
    "  --user=USER        demonstration login, default test\n"
    "  --pass=PASS        demonstration password, default test123\n"
    "  --access-token-lifetime=Ns|Nm|Nh, default 5m\n"
    "  --refresh-token-lifetime=Ns|Nm|Nh, default 24h\n"
    "  -v, --verbose      emit stable OAuth/pong events\n"
    "  -h, --help         show help\n" << std::flush;
  ::exit(code);
}

#ifdef ZiMultiplex_FILTER
static bool parseDrop(ZuCSpan s, double &drop)
{
  drop = 0.0;
  if (!s) return true;
  if (s.length() < 2 || s[s.length() - 1] != '%') return false;
  ZuBox<double> pct;
  if (pct.scan(s.data(), s.length() - 1) != s.length() - 1) return false;
  drop = pct;
  if (drop < 0.0 || drop > 100.0) return false;
  drop *= .01;
  return true;
}
#endif

static bool loadOptions(Options &options, int argc, char **argv)
{
  bool httpSet = false;
  for (int i = 1; i < argc; ++i)
    if (ZuCSpan{argv[i]} == "--http") httpSet = true;
  int argc_ = ZfCLI::load(options, argc, argv);
  if (options.help) usage(0);
  if (argc_ != 1 || !options.requests || !options.maxPending ||
      options.port > 65535 ||
      options.http2 < 0 || options.http2 >= Http2Mode::N ||
      !options.user || !options.pass ||
      !parseDuration(options.accessTokenLifetime, options.accessSecs) ||
      !parseDuration(options.refreshTokenLifetime, options.refreshSecs) ||
      options.refreshSecs <= options.accessSecs) return false;
  if (!httpSet && (options.https || options.http3)) options.http = false;
  if ((options.https || options.http3) && (!options.cert || !options.key))
    return false;
  if (options.ipv6 && options.addr == "0.0.0.0") options.addr = "::";
#ifdef ZiMultiplex_FILTER
  double drop;
  if (!parseDrop(options.quicRxDrop, drop) ||
      !parseDrop(options.quicTxDrop, drop)) return false;
#endif
  return true;
}

static void printMemDiag()
{
  ZiLOG(Info, "zrestd", ([](auto &s) {
    s << "Hash Tables:\n" << Ztc::hashCSV();
    s << "Heaps:\n" << Ztc::heapCSV();
  }));
}

static bool prepareProcess(const Options &options)
{
  Zi::Path pidName;
  if (options.pidfile) pidName = options.pidfile;
  int rc = ZiDaemon::init(nullptr, nullptr, -1, options.daemon, pidName);
  if (rc == ZiDaemon::Running)
    ZiLOG(Error, "zrestd", "PID file names a running process");
  else if (rc != ZiDaemon::OK)
    ZiLOG(Error, "zrestd", "daemon initialization failed");
  return rc == ZiDaemon::OK;
}

void App::listening(int transport, unsigned port)
{
  ZiLOG(Info, "zrestd", ([transport, port](auto &s) {
    s << "event=listening transport=";
    switch (transport) {
      case Zhttp::Transport::QUIC: s << "h3"; break;
      case Zhttp::Transport::TLS: s << "https"; break;
      default: s << "http"; break;
    }
    s << " port=" << port;
  }));
#ifndef _WIN32
  if (m_options->eventFD >= 0) {
    uint8_t value = uint8_t(transport);
    (void)::write(m_options->eventFD, &value, 1);
  }
#endif
}

void App::event_(ZuCSpan event)
{
  if (!m_options->verbose) return;
  ZeString value{event};
  ZiLOG(Info, "zrestd", ([value = ZuMv(value)](auto &s) {
    s << "event=" << value;
  }));
}

void App::fail_()
{
  ++m_errors;
  signal_(false);
}

void App::signal_(bool ok)
{
#ifndef _WIN32
  if (m_options->eventFD >= 0) {
    uint8_t value = uint8_t(ok ? 0x80 : 0x81);
    (void)::write(m_options->eventFD, &value, 1);
  }
#else
  (void)ok;
#endif
  done.post();
}

void App::pong_()
{
  ++m_pong;
  event_("pong");
}

void App::finish_()
{
  ZiLOG(Info, "zrestd", ([authorize = m_authorize, token = m_token,
      refresh = m_refresh, revoke = m_revoke, pong = m_pong](auto &s) {
    s << "summary authorize=" << authorize << " token=" << token <<
      " refresh=" << refresh << " revoke=" << revoke << " pong=" << pong;
  }));
  signal_(true);
}

static ZiMxParams mxParams(const Options &options)
{
  auto params = ZiMxParams()
    .scheduler([](auto &s) {
      s.nThreads(4)
	.thread(1, [](auto &t) { t.isolated(1); })
	.thread(2, [](auto &t) { t.isolated(1); })
	.thread(3, [](auto &t) { t.isolated(1); })
	.thread(4, [](auto &t) { t.isolated(1); }); })
    .rxThread(1).txThread(2);
#ifdef ZiMultiplex_DEBUG
  if (options.debug) params.debug(true);
  if (options.frag) params.frag(true);
  if (options.yield) params.yield(true);
#else
  (void)options;
#endif
  return params;
}

static void interrupted() { done.post(); }

int main(int argc, char **argv)
{
  ZiHeapTune::load();
  ZiHashTune::load();

  Options options;
  try {
    if (!loadOptions(options, argc, argv)) usage();
  } catch (const ZeException &e) {
    std::cerr << e << '\n';
    usage();
  }

  ZiLog::init("zrestd", options.syslog ? "daemon" : "user");
  ZiLog::level(
#ifdef ZiMultiplex_DEBUG
    options.debug ? Ze::Debug :
#endif
#ifdef Zquic_DEBUG
    options.quicDiag ? Ze::Info :
#endif
    options.memDiag ? Ze::Debug : Ze::Info);
  if (options.syslog)
    ZiLog::sink(ZiLog::sysSink());
  else if (options.logPath && options.logPath != "-")
    ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path(options.logPath)));
  else
    ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();

  ZmTrap::sigintFn(interrupted);
  ZmTrap::trap();
  if (!prepareProcess(options)) {
    ZiLog::stop();
    return 1;
  }

  ZiMultiplex mx(mxParams(options));
  if (!mx.start()) {
    ZiLOG(Error, "zrestd", "ZiMultiplex start failed");
    ZiLog::stop();
    return 1;
  }
  App app{&options};
  if (!app.init()) {
    ZiLOG(Error, "zrestd", "random source initialization failed");
    mx.stop();
    ZiLog::stop();
    return 1;
  }
  RESTServer server;
  server.txErrorFn(ZiTxErrorFn{[](ZeException &e) {
    ZiLOG(Error, "zrestd", ([e](auto &s) { s << "transmit error: " << e; }));
    return false;
  }});
  Zrest::ServerConfig config;
  config.localIP(ZiIP(options.addr)).port(options.port)
    .idleTimeout(options.timeout).maxConnections(options.maxconn)
    .retainedBodyMax(ReqBodyMax);
  Zrest::Limits limits;
  limits.maxPending = options.maxPending;
  config.limits(limits);
  if (options.http) config.tcp();
  if (options.https) {
    Zhttp::H2Policy::T policy;
    switch (options.http2) {
      case Http2Mode::force: policy = Zhttp::H2Policy::Force; break;
      case Http2Mode::disable: policy = Zhttp::H2Policy::Disable; break;
      default: policy = Zhttp::H2Policy::Prefer; break;
    }
    config.tls(Zhttp::H2Config{}.certPath(options.cert)
      .keyPath(options.key).policy(policy));
  }
#ifdef Zquic_DEBUG
  bool h3Enabled = false;
#endif
  if (options.http3) {
    double rxDrop = 0, txDrop = 0;
#ifdef ZiMultiplex_FILTER
    (void)parseDrop(options.quicRxDrop, rxDrop);
    (void)parseDrop(options.quicTxDrop, txDrop);
#endif
    config.quic(Zhttp::QUICConfig{}
      .certPath(options.cert).keyPath(options.key)
      .keyLogPath(options.keyLog)
      .heartbeat(options.quicHeartbeat ?
	ZuTime{options.quicHeartbeat} : ZuTime{})
      .rxDrop(rxDrop).txDrop(txDrop));
#ifdef Zquic_DEBUG
    h3Enabled = true;
#endif
  }
  bool serverInited = server.init(
    Zhttp::HubConfig{&mx, "3", "4"}, ZuMv(config), &app);
  bool appActive = serverInited && app.activate("ping") &&
    app.activate("other");
  bool serverStarted = appActive && server.start();
  if (!serverStarted) {
    ZiLOG(Error, "zrestd", ([serverInited, appActive](auto &s) {
      if (!serverInited) s << "HTTP server initialization failed";
      else if (!appActive) s << "application activation failed";
      else s << "HTTP server start failed";
    }));
    if (serverInited) (void)server.stop();
    server.final();
    mx.stop();
    ZiLog::stop();
    return 1;
  }

  unsigned memElapsed = 0;
#ifdef Zquic_DEBUG
  unsigned quicElapsed = 0;
#endif
  for (;;) {
    unsigned step = options.memDiag ? options.memDiag - memElapsed : 0;
#ifdef Zquic_DEBUG
    if (h3Enabled && options.quicDiag) {
      unsigned left = options.quicDiag - quicElapsed;
      if (!step || left < step) step = left;
    }
#endif
    if (!step) { done.wait(); break; }
    if (!done.timedwait(Zm::now(step))) break;
    if (options.memDiag && (memElapsed += step) >= options.memDiag) {
      memElapsed = 0;
      printMemDiag();
    }
#ifdef Zquic_DEBUG
    if (h3Enabled && options.quicDiag &&
	(quicElapsed += step) >= options.quicDiag) {
      quicElapsed = 0;
      server.printQUICDiag();
    }
#endif
  }
  if (!server.stop()) app.transportFailed();
  server.final();
  mx.stop();
  ZiLog::stop();
  return app.errors() || app.processed() < options.requests ? 1 : 0;
}
