//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Zrest routes for Zum's restricted HTTP profile

#ifndef ZumHTTP_HH
#define ZumHTTP_HH

#ifndef ZumLib_HH
#include <zlib/ZumLib.hh>
#endif

#include <zlib/ZfJSON.hh>

#include <zlib/ZrestServer.hh>

#include <zlib/ZumServer.hh>

namespace Zum {

struct HTTPRedirectWire { String redirectURI; };
ZfStruct(, (HTTPRedirectWire, JSON),
  (((redirectURI),	(Required)),	(String)));
struct HTTPStatusWire { String status; };
ZfStruct(, (HTTPStatusWire, JSON),
  (((status),		(Required)),	(String)));
struct HTTPErrorWire { String error; };
ZfStruct(, (HTTPErrorWire, JSON),
  (((error),		(Required)),	(String)));
struct HTTPEmptyWire { String unused; };
ZfStruct(, (HTTPEmptyWire, JSON),
  (((unused),		(JSON::Opt)),	(String)));

template <typename T>
inline String httpJSON(T value)
{
  String body;
  ZfJSON::save(body, value);
  return body;
}

// WebAuthn completion is fetched by the provider page, not a navigation.
// A browser's manual fetch redirect hides both Location and status. Return
// the already-validated OAuth destination as JSON; keep session cookies.
inline void passkeyReply(ServerReply &reply)
{
  if (reply.type != ReplyType::Redirect) return;
  reply.body = httpJSON(HTTPRedirectWire{reply.location});
  reply.location.null();
  reply.type = ReplyType::OK;
}

struct HTTPResponse : public ZumObject {
  String	body;
  String	location;
  String	setCookie;
};

template <typename Impl, unsigned Status_>
struct NoStoreJSON : public Zrest::ResBuilder<Impl, HTTPResponse> {
  using Base = Zrest::ResBuilder<Impl, HTTPResponse>;
  using Base::header;
  enum { Status = Status_, Body = Zrest::BodyPolicy::Raw };
  using Headers = ZhttpHeaders(
    ("content-type", "application/json"),
    ("cache-control", "no-store"), ("pragma", "no-cache"),
    "set-cookie", "content-length");
  template <typename Key, typename L> void header(L &&l) const {
    if constexpr (Key{}() == "set-cookie") {
      if (this->object->setCookie) l(this->object->setCookie);
    } else Base::template header<Key>(ZuFwd<L>(l));
  }
  const String &bodyObject(const HTTPResponse *response) const {
    return response->body;
  }
};

struct TokenOK : public NoStoreJSON<TokenOK, 200> { };
struct PasskeyOK : public NoStoreJSON<PasskeyOK, 200> { };
struct OAuthErrorRes : public NoStoreJSON<OAuthErrorRes, 400> { };
struct ClientError : public NoStoreJSON<ClientError, 401> {
  using Headers = ZhttpHeaders(
    ("content-type", "application/json"),
    ("cache-control", "no-store"), ("pragma", "no-cache"),
    ("www-authenticate", "Basic"), "content-length");
};
struct BearerError : public NoStoreJSON<BearerError, 401> {
  using Base = NoStoreJSON<BearerError, 401>;
  using Base::header;
  using Headers = ZhttpHeaders(
    ("content-type", "application/json"),
    ("cache-control", "no-store"), ("pragma", "no-cache"),
    "www-authenticate", "content-length");
  template <typename Key, typename L> void header(L &&l) const {
    if constexpr (Key{}() == "www-authenticate")
      l("Bearer error=\"invalid_token\"");
    else Base::template header<Key>(ZuFwd<L>(l));
  }
};
struct ServerError : public NoStoreJSON<ServerError, 500> { };

struct DiscoveryOK : public Zrest::ResBuilder<DiscoveryOK, HTTPResponse> {
  enum { Body = Zrest::BodyPolicy::Raw };
  using Headers = ZhttpHeaders(
    ("content-type", "application/json"), "content-length");
  const String &bodyObject(const HTTPResponse *response) const {
    return response->body;
  }
};

struct PageOK : public Zrest::ResBuilder<PageOK, HTTPResponse> {
  using Base = Zrest::ResBuilder<PageOK, HTTPResponse>;
  using Base::header;
  enum { Body = Zrest::BodyPolicy::Raw };
  using Headers = ZhttpHeaders(
    "content-type",
    ("cache-control", "no-store"), "set-cookie", "content-length");
  template <typename Key, typename L> void header(L &&l) const {
    if constexpr (Key{}() == "content-type") l("text/html; charset=utf-8");
    else if constexpr (Key{}() == "set-cookie") {
      if (this->object->setCookie) l(this->object->setCookie);
    }
    else Base::template header<Key>(ZuFwd<L>(l));
  }
  const String &bodyObject(const HTTPResponse *response) const {
    return response->body;
  }
};

struct Redirect : public Zrest::ResBuilder<Redirect, HTTPResponse> {
  using Base = Zrest::ResBuilder<Redirect, HTTPResponse>;
  using Base::header;
  enum { Status = 302, Body = Zrest::BodyPolicy::Zero };
  using Headers = ZhttpHeaders(
    ("cache-control", "no-store"), "location", "set-cookie",
    "content-length");
  template <typename Key, typename L> void header(L &&l) const {
    if constexpr (Key{}() == "location") l(this->object->location);
    else if constexpr (Key{}() == "set-cookie") {
      if (this->object->setCookie) l(this->object->setCookie);
    }
    else Base::template header<Key>(ZuFwd<L>(l));
  }
};

struct RevokeOK : public Zrest::ResBuilder<RevokeOK, HTTPResponse> {
  enum { Status = 200, Body = Zrest::BodyPolicy::Zero };
  using Headers = ZhttpHeaders(
    ("cache-control", "no-store"), ("pragma", "no-cache"),
    "content-length");
};

struct HTTPData : public ZumObject {
  // Zhttp parser spans are callback-scoped; completion runs after consume.
  String data;

  HTTPData &operator =(ZuSpan<uint8_t> data_) {
    data = data_;
    return *this;
  }
};

struct HTTPQuery : public HTTPData {
  HTTPQuery &operator =(ZuSpan<uint8_t> data_) {
    if (data_ && data_[0] == '?') data_.offset(1);
    data = data_;
    return *this;
  }
};

template <typename App> struct AuthorizeReq;
template <typename App> struct TokenReq;
template <typename App> struct PasskeyBeginReq;
template <typename App> struct PasskeyFinishReq;
template <typename App> struct MetadataReq;
template <typename App> struct OpenIDMetadataReq;
template <typename App> struct JWKSReq;
template <typename App> struct UserInfoReq;
template <typename App> struct UserInfoPostReq;
template <typename App> struct RevokeReq;
template <typename App> struct OIDCCallbackReq;
template <typename App> struct LoginReq;
template <typename App> struct LoginPostReq;
template <typename App> struct ConsentReq;
template <typename App> struct LogoutReq;

template <typename App>
using HTTPRequests = ZuTypeList<AuthorizeReq<App>, TokenReq<App>,
  PasskeyBeginReq<App>, PasskeyFinishReq<App>, MetadataReq<App>, JWKSReq<App>,
  OpenIDMetadataReq<App>, UserInfoReq<App>, UserInfoPostReq<App>, RevokeReq<App>,
  OIDCCallbackReq<App>, LoginReq<App>, LoginPostReq<App>, ConsentReq<App>,
  LogoutReq<App>>;

template <typename App>
struct AuthorizeReq : public Zrest::ReqParser<AuthorizeReq<App>, HTTPQuery> {
  using Base = Zrest::ReqParser<AuthorizeReq<App>, HTTPQuery>;
  using Base::header;
  enum { Exact = 1, Query = Zrest::QueryPolicy::Raw };
  static constexpr uint64_t QueryLimit = 16U<<10;
  using Path = ZuStringT<"/authorize">;
  using Headers = ZhttpHeaders("cookie");
  using Responses = ZuTypeList<PageOK, Redirect, OAuthErrorRes, ServerError>;
  App *app = nullptr;
  String cookie;
  void init() { Base::init(); cookie.null(); }
  template <typename Key>
  void header(Zhttp::FieldSection::T, ZuSpan<uint8_t> value) {
    if constexpr (Key{}() == "cookie") cookie = value;
  }
  template <typename Link> void complete(Link *link, bool ok) {
    app->authorize(link, *this, ok);
  }
};

template <typename App>
struct TokenReq : public Zrest::ReqParser<TokenReq<App>, HTTPData> {
  using Base = Zrest::ReqParser<TokenReq, HTTPData>;
  using Base::header;
  enum { Method = Zhttp::Method::POST, Exact = 1,
    Body = Zrest::BodyPolicy::Raw };
  static constexpr uint64_t BodyLimit = 16U<<10;
  using Path = ZuStringT<"/token">;
  using Headers = ZhttpHeaders(
    ("content-type", "application/x-www-form-urlencoded"),
    "content-length", "authorization");
  using Responses = ZuTypeList<TokenOK, OAuthErrorRes, ClientError, ServerError>;
  App *app = nullptr;
  String authorization;

  void init() { Base::init(); authorization.null(); }
  template <typename Key>
  void header(Zhttp::FieldSection::T, ZuSpan<uint8_t> value) {
    if constexpr (Key{}() == "authorization") authorization = value;
  }
  template <typename Link> void complete(Link *link, bool ok) {
    app->token(link, *this, ok);
  }
};

template <typename App, typename Impl>
struct PasskeyReq : public Zrest::ReqParser<Impl, HTTPData> {
  using Base = Zrest::ReqParser<Impl, HTTPData>;
  using Base::header;
  enum { Method = Zhttp::Method::POST, Exact = 1,
    Body = Zrest::BodyPolicy::Raw };
  static constexpr uint64_t BodyLimit = 64U<<10;
  using Headers = ZhttpHeaders(
    ("content-type", "application/json"), "content-length", "cookie");
  App *app = nullptr;
  String cookie;

  void init() { Base::init(); cookie.null(); }
  template <typename Key>
  void header(Zhttp::FieldSection::T, ZuSpan<uint8_t> value) {
    if constexpr (Key{}() == "cookie") cookie = value;
  }
};

template <typename App>
struct PasskeyBeginReq : public PasskeyReq<App, PasskeyBeginReq<App>> {
  using Path = ZuStringT<"/passkey/begin">;
  using Responses = ZuTypeList<PasskeyOK, OAuthErrorRes, ServerError>;
  template <typename Link> void complete(Link *link, bool ok) {
    this->app->passkeyBegin(link, *this, ok);
  }
};

template <typename App>
struct PasskeyFinishReq : public PasskeyReq<App, PasskeyFinishReq<App>> {
  enum { Query = Zrest::QueryPolicy::Raw };
  static constexpr uint64_t QueryLimit = 256;
  using Path = ZuStringT<"/passkey/finish">;
  using Responses = ZuTypeList<PasskeyOK, PageOK, Redirect, OAuthErrorRes,
    ServerError>;
  String query;
  auto &queryObject(HTTPData *) { return query; }
  template <typename Link> void complete(Link *link, bool ok) {
    this->app->passkeyFinish(link, *this, ok);
  }
};

template <typename App>
struct MetadataReq : public Zrest::ReqParser<MetadataReq<App>, HTTPData> {
  enum { Exact = 1 };
  using Path = ZuStringT<"/.well-known/oauth-authorization-server">;
  using Responses = ZuTypeList<DiscoveryOK, ServerError>;
  App *app = nullptr;
  template <typename Link> void complete(Link *link, bool ok) {
    app->metadata(link, *this, ok);
  }
};

template <typename App>
struct OpenIDMetadataReq : public Zrest::ReqParser<OpenIDMetadataReq<App>,
    HTTPData> {
  enum { Exact = 1 };
  using Path = ZuStringT<"/.well-known/openid-configuration">;
  using Responses = ZuTypeList<DiscoveryOK, ServerError>;
  App *app = nullptr;
  template <typename Link> void complete(Link *link, bool ok) {
    app->openidMetadata(link, *this, ok);
  }
};

template <typename App>
struct JWKSReq : public Zrest::ReqParser<JWKSReq<App>, HTTPData> {
  enum { Exact = 1 };
  using Path = ZuStringT<"/jwks">;
  using Responses = ZuTypeList<DiscoveryOK, ServerError>;
  App *app = nullptr;
  template <typename Link> void complete(Link *link, bool ok) {
    app->jwks(link, *this, ok);
  }
};

template <typename App>
struct UserInfoReq : public Zrest::ReqParser<UserInfoReq<App>, HTTPData> {
  using Base = Zrest::ReqParser<UserInfoReq, HTTPData>;
  using Base::header;
  enum { Exact = 1 };
  using Path = ZuStringT<"/userinfo">;
  using Headers = ZhttpHeaders("authorization");
  using Responses = ZuTypeList<TokenOK, BearerError, ServerError>;
  App *app = nullptr;
  String authorization;

  void init() { Base::init(); authorization.null(); }
  template <typename Key>
  void header(Zhttp::FieldSection::T, ZuSpan<uint8_t> value) {
    if constexpr (Key{}() == "authorization") authorization = value;
  }
  template <typename Link> void complete(Link *link, bool ok) {
    app->userInfo(link, *this, ok);
  }
};

template <typename App>
struct UserInfoPostReq : public Zrest::ReqParser<UserInfoPostReq<App>,
    HTTPData> {
  using Base = Zrest::ReqParser<UserInfoPostReq, HTTPData>;
  using Base::header;
  enum { Method = Zhttp::Method::POST, Exact = 1,
    Body = Zrest::BodyPolicy::Raw };
  static constexpr uint64_t BodyLimit = 0;
  using Path = ZuStringT<"/userinfo">;
  using Headers = ZhttpHeaders("content-length", "authorization");
  using Responses = ZuTypeList<TokenOK, BearerError, ServerError>;
  App *app = nullptr;
  String authorization;

  void init() { Base::init(); authorization.null(); }
  template <typename Key>
  void header(Zhttp::FieldSection::T, ZuSpan<uint8_t> value) {
    if constexpr (Key{}() == "authorization") authorization = value;
  }
  template <typename Link> void complete(Link *link, bool ok) {
    app->userInfo(link, *this, ok);
  }
};

template <typename App>
struct RevokeReq : public Zrest::ReqParser<RevokeReq<App>, HTTPData> {
  using Base = Zrest::ReqParser<RevokeReq, HTTPData>;
  using Base::header;
  enum { Method = Zhttp::Method::POST, Exact = 1,
    Body = Zrest::BodyPolicy::Raw };
  static constexpr uint64_t BodyLimit = 16U<<10;
  using Path = ZuStringT<"/revoke">;
  using Headers = ZhttpHeaders(
    ("content-type", "application/x-www-form-urlencoded"),
    "content-length", "authorization");
  using Responses = ZuTypeList<RevokeOK, OAuthErrorRes, ClientError,
    ServerError>;
  App *app = nullptr;
  String authorization;

  void init() { Base::init(); authorization.null(); }
  template <typename Key>
  void header(Zhttp::FieldSection::T, ZuSpan<uint8_t> value) {
    if constexpr (Key{}() == "authorization") authorization = value;
  }
  template <typename Link> void complete(Link *link, bool ok) {
    app->revoke(link, *this, ok);
  }
};

template <typename App>
struct OIDCCallbackReq : public Zrest::ReqParser<OIDCCallbackReq<App>,
    HTTPQuery> {
  using Base = Zrest::ReqParser<OIDCCallbackReq, HTTPQuery>;
  using Base::header;
  enum { Exact = 1, Query = Zrest::QueryPolicy::Raw };
  static constexpr uint64_t QueryLimit = 16U<<10;
  using Path = ZuStringT<"/oidc/callback">;
  using Headers = ZhttpHeaders("cookie");
  using Responses = ZuTypeList<PageOK, Redirect, OAuthErrorRes, ServerError>;
  App *app = nullptr;
  String cookie;

  void init() { Base::init(); cookie.null(); }
  template <typename Key>
  void header(Zhttp::FieldSection::T, ZuSpan<uint8_t> value) {
    if constexpr (Key{}() == "cookie") cookie = value;
  }
  template <typename Link> void complete(Link *link, bool ok) {
    app->oidcCallback(link, *this, ok);
  }
};

template <typename App>
struct LoginReq : public Zrest::ReqParser<LoginReq<App>, HTTPData> {
  using Base = Zrest::ReqParser<LoginReq, HTTPData>;
  using Base::header;
  enum { Exact = 1 };
  using Path = ZuStringT<"/login">;
  using Headers = ZhttpHeaders("cookie");
  using Responses = ZuTypeList<PageOK, ServerError>;
  App *app = nullptr;
  String cookie;

  void init() { Base::init(); cookie.null(); }
  template <typename Key>
  void header(Zhttp::FieldSection::T, ZuSpan<uint8_t> value) {
    if constexpr (Key{}() == "cookie") cookie = value;
  }
  template <typename Link> void complete(Link *link, bool ok) {
    app->login(link, *this, ok);
  }
};

template <typename App>
struct LoginPostReq : public Zrest::ReqParser<LoginPostReq<App>, HTTPData> {
  using Base = Zrest::ReqParser<LoginPostReq, HTTPData>;
  using Base::header;
  enum { Method = Zhttp::Method::POST, Exact = 1,
    Body = Zrest::BodyPolicy::Raw };
  static constexpr uint64_t BodyLimit = 16U<<10;
  using Path = ZuStringT<"/login">;
  using Headers = ZhttpHeaders(
    ("content-type", "application/x-www-form-urlencoded"),
    "content-length", "cookie");
  using Responses = ZuTypeList<PageOK, Redirect, OAuthErrorRes, ServerError>;
  App *app = nullptr;
  String cookie;

  void init() { Base::init(); cookie.null(); }
  template <typename Key>
  void header(Zhttp::FieldSection::T, ZuSpan<uint8_t> value) {
    if constexpr (Key{}() == "cookie") cookie = value;
  }
  template <typename Link> void complete(Link *link, bool ok) {
    app->login(link, *this, ok);
  }
};

template <typename App>
struct LogoutReq : public Zrest::ReqParser<LogoutReq<App>, HTTPData> {
  using Base = Zrest::ReqParser<LogoutReq, HTTPData>;
  using Base::header;
  enum { Method = Zhttp::Method::POST, Exact = 1,
    Body = Zrest::BodyPolicy::Raw };
  static constexpr uint64_t BodyLimit = 16U<<10;
  using Path = ZuStringT<"/logout">;
  using Headers = ZhttpHeaders(
    ("content-type", "application/x-www-form-urlencoded"),
    "content-length", "cookie");
  using Responses = ZuTypeList<PageOK, OAuthErrorRes, ServerError>;
  App *app = nullptr;
  String cookie;

  void init() { Base::init(); cookie.null(); }
  template <typename Key>
  void header(Zhttp::FieldSection::T, ZuSpan<uint8_t> value) {
    if constexpr (Key{}() == "cookie") cookie = value;
  }
  template <typename Link> void complete(Link *link, bool ok) {
    app->logout(link, *this, ok);
  }
};

template <typename App>
struct ConsentReq : public Zrest::ReqParser<ConsentReq<App>, HTTPData> {
  using Base = Zrest::ReqParser<ConsentReq, HTTPData>;
  using Base::header;
  enum { Method = Zhttp::Method::POST, Exact = 1,
    Body = Zrest::BodyPolicy::Raw };
  static constexpr uint64_t BodyLimit = 16U<<10;
  using Path = ZuStringT<"/consent">;
  using Headers = ZhttpHeaders(
    ("content-type", "application/x-www-form-urlencoded"),
    "content-length", "cookie");
  using Responses = ZuTypeList<Redirect, OAuthErrorRes, ServerError>;
  App *app = nullptr;
  String cookie;

  void init() { Base::init(); cookie.null(); }
  template <typename Key>
  void header(Zhttp::FieldSection::T, ZuSpan<uint8_t> value) {
    if constexpr (Key{}() == "cookie") cookie = value;
  }
  template <typename Link> void complete(Link *link, bool ok) {
    app->consent(link, *this, ok);
  }
};

template <typename App>
struct HTTPParser : public Zrest::MReqParser<HTTPRequests<App>> {
  using Base = Zrest::MReqParser<HTTPRequests<App>>;
  void init(App &app_) { app = &app_; }
  bool operation(Zhttp::Method::T method, Zhttp::Target &target) {
    if (!Base::operation(method, target)) return false;
    this->u.dispatch([this](auto, auto &request) { request.app = app; });
    return true;
  }
  App *app = nullptr;
};

template <typename App>
using HTTPBuilder = Zrest::MResBuilder<HTTPParser<App>>;

template <typename App>
class HTTP {
public:
  void init(Server &server) { m_server = &server; }

  Server &server() { return *m_server; }
  const Server &server() const { return *m_server; }

  template <typename Link>
  void authorize(Link *link, const AuthorizeReq<App> &request, bool ok)
  {
    if (!ok) {
      respond_<AuthorizeReq<App>>(ZmRef<Link>{link}, ServerReply{
        .body = oauthErrorJSON(OAuthError::InvalidRequest),
        .type = ReplyType::OAuthError});
      return;
    }
    auto query = String{request.object->data};
    String cookie{request.cookie};
    m_server->authorize(ZuMv(query), ZuMv(cookie),
      [this, hold = ZmRef<Link>{link}](ServerReply reply) mutable {
      respond_<AuthorizeReq<App>>(ZuMv(hold), ZuMv(reply));
    });
  }

  template <typename Link>
  void token(Link *link, const TokenReq<App> &request, bool ok)
  {
    if (!ok) {
      respond_<TokenReq<App>>(ZmRef<Link>{link}, ServerReply{
        .body = oauthErrorJSON(OAuthError::InvalidRequest),
        .type = ReplyType::OAuthError});
      return;
    }
    String form{request.object->data};
    String authorization{request.authorization};
    m_server->token(ZuMv(form), ZuMv(authorization), [
      this, hold = ZmRef<Link>{link}
    ](ServerReply reply) mutable {
      respond_<TokenReq<App>>(ZuMv(hold), ZuMv(reply));
    });
  }

  template <typename Link>
  void revoke(Link *link, const RevokeReq<App> &request, bool ok)
  {
    if (!ok) {
      respond_<RevokeReq<App>>(ZmRef<Link>{link}, ServerReply{
        .body = oauthErrorJSON(OAuthError::InvalidRequest),
        .type = ReplyType::OAuthError});
      return;
    }
    String form{request.object->data};
    String authorization{request.authorization};
    m_server->revoke(ZuMv(form), ZuMv(authorization), [
      this, hold = ZmRef<Link>{link}
    ](ServerReply reply) mutable {
      respond_<RevokeReq<App>>(ZuMv(hold), ZuMv(reply));
    });
  }

  template <typename Link>
  void login(Link *link, const LoginReq<App> &request, bool ok)
  {
    if (!ok) {
      respond_<LoginReq<App>>(ZmRef<Link>{link}, ServerReply{
        .body = httpJSON(HTTPEmptyWire{}), .type = ReplyType::ServerError});
      return;
    }
    String cookie{request.cookie};
    m_server->login(ZuMv(cookie), [this, hold = ZmRef<Link>{link}](
	ServerReply reply) mutable {
      respond_<LoginReq<App>>(ZuMv(hold), ZuMv(reply));
    });
  }

  template <typename Link>
  void login(Link *link, const LoginPostReq<App> &request, bool ok)
  {
    if (!ok) {
      respond_<LoginPostReq<App>>(ZmRef<Link>{link}, ServerReply{
        .body = oauthErrorJSON(OAuthError::InvalidRequest),
        .type = ReplyType::OAuthError});
      return;
    }
    String form{request.object->data};
    String cookie{request.cookie};
    m_server->login(ZuMv(form), ZuMv(cookie), [
      this, hold = ZmRef<Link>{link}](ServerReply reply) mutable {
      respond_<LoginPostReq<App>>(ZuMv(hold), ZuMv(reply));
    });
  }

  template <typename Link>
  void logout(Link *link, const LogoutReq<App> &request, bool ok)
  {
    if (!ok) {
      respond_<LogoutReq<App>>(ZmRef<Link>{link}, ServerReply{
        .body = oauthErrorJSON(OAuthError::InvalidRequest),
        .type = ReplyType::OAuthError});
      return;
    }
    String form{request.object->data};
    String cookie{request.cookie};
    m_server->logout(ZuMv(form), ZuMv(cookie), [
      this, hold = ZmRef<Link>{link}](ServerReply reply) mutable {
      respond_<LogoutReq<App>>(ZuMv(hold), ZuMv(reply));
    });
  }

  template <typename Link>
  void consent(Link *link, const ConsentReq<App> &request, bool ok)
  {
    if (!ok) {
      respond_<ConsentReq<App>>(ZmRef<Link>{link}, ServerReply{
        .body = oauthErrorJSON(OAuthError::InvalidRequest),
        .type = ReplyType::OAuthError});
      return;
    }
    String form{request.object->data};
    String cookie{request.cookie};
    m_server->consent(ZuMv(form), ZuMv(cookie), [
      this, hold = ZmRef<Link>{link}](ServerReply reply) mutable {
      respond_<ConsentReq<App>>(ZuMv(hold), ZuMv(reply));
    });
  }

  template <typename Link>
  void passkeyBegin(
      Link *link, const PasskeyBeginReq<App> &request, bool ok)
  {
    if (!ok) {
      respond_<PasskeyBeginReq<App>>(ZmRef<Link>{link}, ServerReply{
        .body = oauthErrorJSON(OAuthError::InvalidRequest),
        .type = ReplyType::OAuthError});
      return;
    }
    String json{request.object->data};
    m_server->passkeyBegin(ZuMv(json), [
      this, hold = ZmRef<Link>{link}
    ](ServerReply reply) mutable {
      respond_<PasskeyBeginReq<App>>(ZuMv(hold), ZuMv(reply));
    });
  }

  template <typename Link>
  void passkeyFinish(
      Link *link, const PasskeyFinishReq<App> &request, bool ok)
  {
    if (!ok) {
      respond_<PasskeyFinishReq<App>>(ZmRef<Link>{link}, ServerReply{
        .body = oauthErrorJSON(OAuthError::InvalidRequest),
        .type = ReplyType::OAuthError});
      return;
    }
    String json{request.object->data};
    String query{request.query};
    String cookie{request.cookie};
    m_server->passkeyFinish(ZuMv(query), ZuMv(cookie), ZuMv(json), [
      this, hold = ZmRef<Link>{link}
    ](ServerReply reply) mutable {
      passkeyReply(reply);
      respond_<PasskeyFinishReq<App>>(ZuMv(hold), ZuMv(reply));
    });
  }

  template <typename Link>
  void metadata(Link *link, const MetadataReq<App> &, bool ok)
  {
    if (!ok) {
      respond_<MetadataReq<App>>(ZmRef<Link>{link}, ServerReply{
        .body = httpJSON(HTTPEmptyWire{}), .type = ReplyType::ServerError});
      return;
    }
    m_server->metadata([this, hold = ZmRef<Link>{link}](
        ServerReply reply) mutable {
      respond_<MetadataReq<App>>(ZuMv(hold), ZuMv(reply));
    });
  }

  template <typename Link>
  void openidMetadata(Link *link, const OpenIDMetadataReq<App> &, bool ok)
  {
    if (!ok) {
      respond_<OpenIDMetadataReq<App>>(ZmRef<Link>{link}, ServerReply{
        .body = httpJSON(HTTPEmptyWire{}), .type = ReplyType::ServerError});
      return;
    }
    m_server->metadata([this, hold = ZmRef<Link>{link}](
        ServerReply reply) mutable {
      respond_<OpenIDMetadataReq<App>>(ZuMv(hold), ZuMv(reply));
    });
  }

  template <typename Link>
  void jwks(Link *link, const JWKSReq<App> &, bool ok)
  {
    if (!ok) {
      respond_<JWKSReq<App>>(ZmRef<Link>{link}, ServerReply{
        .body = httpJSON(HTTPEmptyWire{}), .type = ReplyType::ServerError});
      return;
    }
    m_server->jwks([this, hold = ZmRef<Link>{link}](
        ServerReply reply) mutable {
      respond_<JWKSReq<App>>(ZuMv(hold), ZuMv(reply));
    });
  }

  template <typename Link>
  void userInfo(Link *link, const UserInfoReq<App> &request, bool ok)
  {
    if (!ok) {
      respond_<UserInfoReq<App>>(ZmRef<Link>{link}, ServerReply{
        .body = httpJSON(HTTPErrorWire{"invalid_token"}),
        .type = ReplyType::BearerError});
      return;
    }
    String authorization{request.authorization};
    m_server->userInfo(ZuMv(authorization), [
      this, hold = ZmRef<Link>{link}
    ](ServerReply reply) mutable {
      respond_<UserInfoReq<App>>(ZuMv(hold), ZuMv(reply));
    });
  }

  template <typename Link>
  void userInfo(Link *link, const UserInfoPostReq<App> &request, bool ok)
  {
    if (!ok) {
      respond_<UserInfoPostReq<App>>(ZmRef<Link>{link}, ServerReply{
        .body = httpJSON(HTTPErrorWire{"invalid_token"}),
        .type = ReplyType::BearerError});
      return;
    }
    String authorization{request.authorization};
    m_server->userInfo(ZuMv(authorization), [
      this, hold = ZmRef<Link>{link}
    ](ServerReply reply) mutable {
      respond_<UserInfoPostReq<App>>(ZuMv(hold), ZuMv(reply));
    });
  }

  template <typename Link>
  void oidcCallback(
      Link *link, const OIDCCallbackReq<App> &request, bool ok)
  {
    if (!ok) {
      respond_<OIDCCallbackReq<App>>(ZmRef<Link>{link}, ServerReply{
        .body = oauthErrorJSON(OAuthError::InvalidRequest),
        .type = ReplyType::OAuthError});
      return;
    }
    String query{request.object->data};
    String cookie{request.cookie};
    m_server->oidcCallback(ZuMv(query), ZuMv(cookie), [
      this, hold = ZmRef<Link>{link}
    ](ServerReply reply) mutable {
      respond_<OIDCCallbackReq<App>>(ZuMv(hold), ZuMv(reply));
    });
  }

private:
  template <typename Response, typename Request, typename Link>
  void send_(ZmRef<Link> link, ZmRef<HTTPResponse> object)
  {
    using ResBuilder = typename App::ResBuilderQ::Node;
    ZmRef<ResBuilder> response = new ResBuilder{};
    response->template init<Response, Request>(object.ptr());
    link->send(ZuMv(response));
  }

  template <typename Request, typename Link>
  void respond_(ZmRef<Link> link, ServerReply reply)
  {
    ZmRef<HTTPResponse> object = new HTTPResponse{};
    object->body = ZuMv(reply.body);
    object->location = ZuMv(reply.location);
    object->setCookie = ZuMv(reply.setCookie);
    switch (reply.type) {
      case ReplyType::Page:
        if constexpr (ZuIsSame<Request, AuthorizeReq<App>>{} ||
            ZuIsSame<Request, PasskeyFinishReq<App>>{} ||
            ZuIsSame<Request, OIDCCallbackReq<App>>{})
          return send_<PageOK, Request>(ZuMv(link), ZuMv(object));
        else if constexpr (ZuIsSame<Request, LoginReq<App>>{} ||
            ZuIsSame<Request, LoginPostReq<App>>{} ||
            ZuIsSame<Request, LogoutReq<App>>{})
          return send_<PageOK, Request>(ZuMv(link), ZuMv(object));
        break;
      case ReplyType::Redirect:
        if constexpr (ZuIsSame<Request, AuthorizeReq<App>>{} ||
            ZuIsSame<Request, PasskeyFinishReq<App>>{} ||
            ZuIsSame<Request, OIDCCallbackReq<App>>{} ||
            ZuIsSame<Request, LoginPostReq<App>>{} ||
            ZuIsSame<Request, ConsentReq<App>>{})
          return send_<Redirect, Request>(ZuMv(link), ZuMv(object));
        break;
      case ReplyType::OK:
        if constexpr (ZuIsSame<Request, TokenReq<App>>{} ||
            ZuIsSame<Request, UserInfoReq<App>>{} ||
            ZuIsSame<Request, UserInfoPostReq<App>>{})
          return send_<TokenOK, Request>(ZuMv(link), ZuMv(object));
        else if constexpr (ZuIsSame<Request, PasskeyBeginReq<App>>{} ||
            ZuIsSame<Request, PasskeyFinishReq<App>>{})
          return send_<PasskeyOK, Request>(ZuMv(link), ZuMv(object));
        break;
      case ReplyType::OAuthError:
	if constexpr (ZuIsSame<Request, AuthorizeReq<App>>{} ||
	    ZuIsSame<Request, TokenReq<App>>{} ||
	    ZuIsSame<Request, PasskeyBeginReq<App>>{} ||
	    ZuIsSame<Request, PasskeyFinishReq<App>>{} ||
	    ZuIsSame<Request, RevokeReq<App>>{} ||
	    ZuIsSame<Request, LoginPostReq<App>>{} ||
	    ZuIsSame<Request, ConsentReq<App>>{} ||
	    ZuIsSame<Request, LogoutReq<App>>{} ||
	    ZuIsSame<Request, OIDCCallbackReq<App>>{})
	  return send_<OAuthErrorRes, Request>(ZuMv(link), ZuMv(object));
        break;
      case ReplyType::ClientError:
        if constexpr (ZuIsSame<Request, TokenReq<App>>{} ||
            ZuIsSame<Request, RevokeReq<App>>{})
          return send_<ClientError, Request>(ZuMv(link), ZuMv(object));
        break;
      case ReplyType::BearerError:
        if constexpr (ZuIsSame<Request, UserInfoReq<App>>{} ||
            ZuIsSame<Request, UserInfoPostReq<App>>{})
          return send_<BearerError, Request>(ZuMv(link), ZuMv(object));
        break;
      case ReplyType::Empty:
        if constexpr (ZuIsSame<Request, RevokeReq<App>>{})
          return send_<RevokeOK, Request>(ZuMv(link), ZuMv(object));
        break;
      case ReplyType::Discovery:
        if constexpr (ZuIsSame<Request, MetadataReq<App>>{} ||
            ZuIsSame<Request, OpenIDMetadataReq<App>>{} ||
            ZuIsSame<Request, JWKSReq<App>>{})
          return send_<DiscoveryOK, Request>(ZuMv(link), ZuMv(object));
        break;
      default:
        break;
    }
    send_<ServerError, Request>(ZuMv(link), ZuMv(object));
  }

  Server	*m_server = nullptr;
};

} // namespace Zum

#endif /* ZumHTTP_HH */
