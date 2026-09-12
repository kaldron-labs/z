//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// zumd REST/OIDC server

#ifndef ZumDaemon_HH
#define ZumDaemon_HH

#ifndef ZumLib_HH
#include <zlib/ZumLib.hh>
#endif

#include <zlib/ZmList.hh>

#include <zlib/ZhttpServer.hh>
#include <zlib/ZrestServer.hh>
#include <zlib/ZfJSON.hh>

#include <zlib/ZumHTTP.hh>
#include <zlib/ZumIdentityDB.hh>
#include <zlib/ZumMgmt.hh>
#include <zlib/ZumJWT.hh>
#include <zlib/ZumRequest.hh>

#include <zlib/ZtlsPK.hh>

#include "ZumBootstrap.hh"

namespace Zum {

struct DaemonConfig {
  String	addr{"127.0.0.1"};
  uint16_t	port = 8080;
  String	issuer;
  String	rpID;
  String	rpName{"Zum"};
  String	admin;
  Bytes		dbKey;
  OIDCHTTPFn	upstreamHTTP;
  uint64_t	requestTimeout = 15;
  ServerBootstrapResult bootstrap;
};

struct DaemonResponse : public HTTPResponse { String allow; };

template <typename Impl, unsigned Status_>
struct DaemonJSON : public Zrest::ResBuilder<Impl, DaemonResponse> {
  enum { Status = Status_, Body = Zrest::BodyPolicy::Raw };
  using Headers = ZhttpHeaders(
    ("content-type", "application/json"), ("cache-control", "no-store"),
    "content-length");
  const String &bodyObject(const DaemonResponse *response) const {
    return response->body;
  }
};

struct HealthOK : public DaemonJSON<HealthOK, 200> { };
struct HealthUnavailable : public DaemonJSON<HealthUnavailable, 503> { };
struct AdminBadRequest : public DaemonJSON<AdminBadRequest, 400> { };
struct AdminForbidden : public DaemonJSON<AdminForbidden, 403> { };
struct AdminNotFound : public DaemonJSON<AdminNotFound, 404> { };
struct AdminMethodNotAllowed : public Zrest::ResBuilder<
    AdminMethodNotAllowed, DaemonResponse> {
  using Base = Zrest::ResBuilder<AdminMethodNotAllowed, DaemonResponse>;
  using Base::header;
  enum { Status = 405, Body = Zrest::BodyPolicy::Raw };
  using Headers = ZhttpHeaders(("content-type", "application/json"), "allow",
    ("cache-control", "no-store"), "content-length");
  template <typename Key, typename L> void header(L &&l) const {
    if constexpr (Key{}() == "allow") l(this->object->allow);
    else Base::template header<Key>(ZuFwd<L>(l));
  }
  const String &bodyObject(const DaemonResponse *response) const {
    return response->body;
  }
};
struct AdminConflict : public DaemonJSON<AdminConflict, 409> { };
struct AdminPrecondition : public DaemonJSON<AdminPrecondition, 412> { };
struct AdminRequired : public DaemonJSON<AdminRequired, 428> { };
struct AdminLimited : public DaemonJSON<AdminLimited, 429> { };
struct AdminNotImplemented : public DaemonJSON<AdminNotImplemented, 501> { };
struct AdminCreated : public DaemonJSON<AdminCreated, 201> { };
struct AdminAccepted : public DaemonJSON<AdminAccepted, 202> { };

struct AdminUnauthorized : public Zrest::ResBuilder<
    AdminUnauthorized, DaemonResponse> {
  using Base = Zrest::ResBuilder<AdminUnauthorized, DaemonResponse>;
  using Base::header;
  enum { Status = 401, Body = Zrest::BodyPolicy::Raw };
  using Headers = ZhttpHeaders(
    ("content-type", "application/json"), "www-authenticate",
    ("cache-control", "no-store"), "content-length");
  template <typename Key, typename L> void header(L &&l) const {
    if constexpr (Key{}() == "www-authenticate") l("Bearer");
    else Base::template header<Key>(ZuFwd<L>(l));
  }
  const String &bodyObject(const DaemonResponse *response) const {
    return response->body;
  }
};

struct BootstrapPage : public Zrest::ResBuilder<BootstrapPage, DaemonResponse> {
  using Base = Zrest::ResBuilder<BootstrapPage, DaemonResponse>;
  using Base::header;
  enum { Status = 200, Body = Zrest::BodyPolicy::Raw };
  using Headers = ZhttpHeaders(
    "content-type", ("cache-control", "no-store"), "content-length");
  template <typename Key, typename L> void header(L &&l) const {
    if constexpr (Key{}() == "content-type") l("text/html; charset=utf-8");
    else Base::template header<Key>(ZuFwd<L>(l));
  }
  const String &bodyObject(const DaemonResponse *response) const {
    return response->body;
  }
};

class Daemon;

template <typename App, typename Impl, typename Path_>
struct DaemonGet : public Zrest::ReqParser<Impl, HTTPQuery> {
  enum { Exact = 1, Query = Zrest::QueryPolicy::Raw };
  static constexpr uint64_t QueryLimit = 16U<<10;
  using Path = Path_;
  App *app = nullptr;
};

struct LiveReq : public DaemonGet<Daemon, LiveReq,
    ZuStringT<"/health/live">> {
  using Responses = ZuTypeList<HealthOK>;
  template <typename Link> void complete(Link *link, bool ok);
};

struct ReadyReq : public DaemonGet<Daemon, ReadyReq,
    ZuStringT<"/health/ready">> {
  using Responses = ZuTypeList<HealthOK, HealthUnavailable>;
  template <typename Link> void complete(Link *link, bool ok);
};

struct BootstrapReq : public DaemonGet<Daemon, BootstrapReq,
    ZuStringT<"/bootstrap/enroll">> {
  using Responses = ZuTypeList<BootstrapPage, HealthUnavailable>;
  template <typename Link> void complete(Link *link, bool ok);
};

struct EnrollReq : public DaemonGet<Daemon, EnrollReq,
    ZuStringT<"/enroll">> {
  using Responses = ZuTypeList<BootstrapPage, HealthUnavailable>;
  template <typename Link> void complete(Link *link, bool ok);
};

struct AdminData : public HTTPData {
  String target;
  String query;
  String body;
};

struct AdminResult {
  String body;
  unsigned status = 500;
  StringVec resultIDs;
};
ZuDerive(AdminDoneFn, (ZmFn<void(AdminResult),
  ZmFnHeapID<"zumd.AdminDone">>));
ZuDerive(AdminAuthFn, (ZmFn<void(bool, Principal),
  ZmFnHeapID<"zumd.AdminAuth">>));
struct AdminPermit {
  IDVec roleIDs;
  AppID targetApp = 0;
  bool allowed = false;
  bool superuser = false;
};
ZuDerive(AdminAccessFn, (ZmFn<void(AdminPermit),
  ZmFnHeapID<"zumd.AdminAccess">>));
struct IdemBegin {
  IdemRequest request;
  AdminResult result;
  bool execute = false;
};
ZuDerive(IdemBeginFn, (ZmFn<void(IdemBegin),
  ZmFnHeapID<"zumd.IdemBegin">>));

template <typename Impl, Zhttp::Method::T Method_, unsigned Body_>
struct AdminReq : public Zrest::ReqParser<Impl, AdminData> {
  using Base = Zrest::ReqParser<Impl, AdminData>;
  using Base::header;
  enum { Method = Method_, Exact = 0, Query = Zrest::QueryPolicy::None,
    Body = Body_ };
  static constexpr uint64_t QueryLimit = 16U<<10;
  static constexpr uint64_t BodyLimit = 64U<<10;
  using Path = ZuStringT<"/admin">;
  using Headers = ZhttpHeaders(
    "content-type", "content-length", "authorization", "if-match",
    "if-none-match", "idempotency-key");
  using Responses = ZuTypeList<HealthOK, AdminCreated, AdminAccepted,
    AdminBadRequest, AdminUnauthorized, AdminForbidden, AdminNotFound,
    AdminMethodNotAllowed,
    AdminConflict, AdminPrecondition, AdminRequired, AdminLimited,
    HealthUnavailable, AdminNotImplemented>;
  Daemon *app = nullptr;
  String authorization;
  String ifMatch;
  String ifNoneMatch;
  String idempotencyKey;

  void init() {
    Base::init();
    authorization.null();
    ifMatch.null();
    ifNoneMatch.null();
    idempotencyKey.null();
  }
  auto &queryObject(AdminData *object) { return object->query; }
  auto &bodyObject(AdminData *object) { return object->body; }
  bool operation(Zhttp::Method::T method, Zhttp::Target &target) {
    if (target.path.length() <= Path{}().length() ||
        target.path[Path{}().length()] != '/') return false;
    if (!Base::operation(method, target)) return false;
    auto query = target.path.find<"?">();
    if (query >= 0) {
      ZuBSpan path{target.path};
      path.trunc(unsigned(query));
      this->object->target = path;
      path = target.path;
      path.offset(unsigned(query) + 1);
      this->object->query = path;
    } else {
      this->object->target = String{target.path};
      this->object->query.null();
    }
    return true;
  }
  template <typename Key>
  void header(Zhttp::FieldSection::T, ZuSpan<uint8_t> value) {
    if constexpr (Key{}() == "authorization") authorization = value;
    else if constexpr (Key{}() == "if-match") ifMatch = value;
    else if constexpr (Key{}() == "if-none-match") ifNoneMatch = value;
    else if constexpr (Key{}() == "idempotency-key") idempotencyKey = value;
  }
  template <typename Link> void complete(Link *link, bool ok);
};

struct AdminGET : public AdminReq<AdminGET, Zhttp::Method::GET,
    Zrest::BodyPolicy::None> { };
struct AdminPOST : public AdminReq<AdminPOST, Zhttp::Method::POST,
    Zrest::BodyPolicy::Raw> { };
struct AdminPUT : public AdminReq<AdminPUT, Zhttp::Method::PUT,
    Zrest::BodyPolicy::Raw> { };
struct AdminPATCH : public AdminReq<AdminPATCH, Zhttp::Method::PATCH,
    Zrest::BodyPolicy::Raw> { };
struct AdminDELETE : public AdminReq<AdminDELETE, Zhttp::Method::DELETE,
    Zrest::BodyPolicy::Zero> { };

struct ServiceData : public HTTPData { String body; };

template <typename Impl, typename Path_>
struct ServiceReq : public Zrest::ReqParser<Impl, ServiceData> {
  using Base = Zrest::ReqParser<Impl, ServiceData>;
  using Base::header;
  enum { Method = Zhttp::Method::POST, Exact = 1,
    Query = Zrest::QueryPolicy::None, Body = Zrest::BodyPolicy::Raw };
  static constexpr uint64_t BodyLimit = 16U<<10;
  using Path = Path_;
  using Headers = ZhttpHeaders(
    "content-type", "content-length", "authorization");
  using Responses = ZuTypeList<HealthOK, AdminBadRequest,
    AdminUnauthorized, AdminForbidden, HealthUnavailable>;
  Daemon *app = nullptr;
  String authorization;

  void init() { Base::init(); authorization.null(); }
  auto &bodyObject(ServiceData *object) { return object->body; }
  template <typename Key>
  void header(Zhttp::FieldSection::T, ZuSpan<uint8_t> value) {
    if constexpr (Key{}() == "authorization") authorization = value;
  }
};

struct ServiceAuthorizeReq : public ServiceReq<ServiceAuthorizeReq,
    ZuStringT<"/service/authorize">> {
  template <typename Link> void complete(Link *, bool);
};
struct ServiceTokenReq : public ServiceReq<ServiceTokenReq,
    ZuStringT<"/service/token">> {
  template <typename Link> void complete(Link *, bool);
};
struct ServiceRevokeReq : public ServiceReq<ServiceRevokeReq,
    ZuStringT<"/service/revoke">> {
  template <typename Link> void complete(Link *, bool);
};

namespace ServiceOp { enum { Authorize, Token, Revoke }; }

using DaemonRequests = ZuTypeConcat<HTTPRequests<Daemon>, ZuTypeList<
  LiveReq, ReadyReq, BootstrapReq, EnrollReq,
  AdminGET, AdminPOST, AdminPUT, AdminPATCH, AdminDELETE,
  ServiceAuthorizeReq, ServiceTokenReq, ServiceRevokeReq>>;

struct DaemonParser : public Zrest::MReqParser<DaemonRequests> {
  using Base = Zrest::MReqParser<DaemonRequests>;
  void init(Daemon &app_) { app = &app_; }
  bool operation(Zhttp::Method::T method, Zhttp::Target &target);
  Daemon *app = nullptr;
};

using DaemonBuilder = Zrest::MResBuilder<DaemonParser>;
struct DaemonBuilder_ : public ZmObject, public DaemonBuilder { };
ZmListDerive(DaemonBuilderQ, DaemonBuilder_,
  ZmListNode<DaemonBuilder_, ZmListHeapID<"zumd.Response">>);

class Daemon : public HTTP<Daemon> {
public:
  using Parser = DaemonParser;
  using ResBuilderQ = DaemonBuilderQ;

  bool init(
    DB *, DBContext *, Requests *, ZiMultiplex *, DaemonConfig);
  // Main thread, with request admission closed: prepare active-node signing
  // after bootstrap. init/start do not read an inactive replica's database.
  bool prepare(ServerBootstrapResult);
  bool start();
  void stop();
  void final();

  template <typename Link>
  void live(Link *link, const LiveReq &, bool ok)
  {
    if (ok) send_<HealthOK, LiveReq>(
      link, httpJSON(HTTPStatusWire{"live"}));
  }

  template <typename Link>
  void ready(Link *link, const ReadyReq &, bool ok)
  {
    if (!ok || !m_requests->active()) {
      send_<HealthUnavailable, ReadyReq>(
        link, httpJSON(HTTPStatusWire{"unavailable"}));
      return;
    }
    m_provider.ready([this, hold = ZmRef<Link>{link}](ServerReply reply) mutable {
      if (m_requests->active() && reply.type == ReplyType::OK)
        send_<HealthOK, ReadyReq>(hold.ptr(), ZuMv(reply.body));
      else
        send_<HealthUnavailable, ReadyReq>(
          hold.ptr(), httpJSON(HTTPStatusWire{"not-ready"}));
    });
  }

  template <typename Link>
  void bootstrap(Link *link, const BootstrapReq &request, bool ok)
  {
    if (!ok || !request.object->data) {
      send_<HealthUnavailable, BootstrapReq>(
        link, httpJSON(HTTPStatusWire{"unavailable"}));
      return;
    }
    send_<BootstrapPage, BootstrapReq>(link, bootstrapPage_());
  }

  template <typename Link>
  void enroll(Link *link, const EnrollReq &request, bool ok)
  {
    if (!ok || !request.object->data) {
      send_<HealthUnavailable, EnrollReq>(
	link, httpJSON(HTTPStatusWire{"unavailable"}));
      return;
    }
    send_<BootstrapPage, EnrollReq>(link, enrollPage_());
  }

  template <typename Link, typename Request>
  void admin(Link *link, const Request &request, bool ok)
  {
    String correlationID = correlation_();
    if (!m_requests->active()) {
      send_<HealthUnavailable, Request>(link, correlate_(
        error_("unavailable", "server is inactive"), correlationID));
      return;
    }
    if (!ok || !request.object) {
      send_<AdminBadRequest, Request>(
        link, correlate_(error_("invalid_request", "invalid request"),
	  correlationID));
      return;
    }
    int op = managementOperation(Request::Method, request.object->target);
    if (op < 0) {
      String allow = managementAllow(request.object->target);
      if (allow)
        send_<AdminMethodNotAllowed, Request>(link,
          correlate_(error_("method_not_allowed", "method is not allowed"),
	    correlationID), ZuMv(allow));
      else
        send_<AdminNotFound, Request>(
          link, correlate_(error_("not_found", "unknown administrative endpoint"),
	    correlationID));
      return;
    }
    String authorization = request.authorization;
    String target = request.object->target;
    String query = request.object->query;
    String body = request.object->body;
    String ifMatch = request.ifMatch;
    String ifNoneMatch = request.ifNoneMatch;
    String idempotencyKey = request.idempotencyKey;
    AdminDoneFn reply{[this, correlationID, hold = ZmRef<Link>{link}](
        AdminResult result) mutable {
      if (result.status >= 400)
	result.body = correlate_(ZuMv(result.body), correlationID);
      adminSend_<Request>(hold.ptr(), ZuMv(result));
    }};
    auto cancel = [reply]() mutable {
      reply(AdminResult{error_("unavailable", "request unavailable"), 503});
    };
    if (!m_requests->run(Zm::now() + ZuTime{double(m_config.requestTimeout)},
        [this, op, authorization = ZuMv(authorization),
          target = ZuMv(target), query = ZuMv(query), body = ZuMv(body),
          ifMatch = ZuMv(ifMatch), ifNoneMatch = ZuMv(ifNoneMatch),
          idempotencyKey = ZuMv(idempotencyKey),
          correlationID = ZuMv(correlationID), reply](
            ZmRef<Zum::Request> pending) mutable {
      adminRequest_(op, ZuMv(authorization), ZuMv(target), ZuMv(query),
        ZuMv(body), ZuMv(ifMatch), ZuMv(ifNoneMatch),
        ZuMv(idempotencyKey), ZuMv(correlationID),
        [pending = ZuMv(pending), reply = ZuMv(reply)](AdminResult result) mutable {
          pending->complete([reply = ZuMv(reply), result = ZuMv(result)]() mutable {
            reply(ZuMv(result));
          });
        });
    }, cancel)) cancel();
  }

  template <int Operation, typename Link, typename Request>
  void service(Link *link, const Request &request, bool ok)
  {
    if (!ok || !request.object) {
      send_<AdminBadRequest, Request>(link,
        error_("invalid_request", "invalid service request"));
      return;
    }
    serviceCall_(Operation, String{request.authorization},
      String{request.object->body}, [this, hold = ZmRef<Link>{link}](
        AdminResult result) mutable {
      switch (result.status) {
        case 200:
          send_<HealthOK, Request>(hold.ptr(), ZuMv(result.body)); return;
        case 400:
          send_<AdminBadRequest, Request>(
            hold.ptr(), ZuMv(result.body)); return;
        case 401:
          send_<AdminUnauthorized, Request>(
            hold.ptr(), ZuMv(result.body)); return;
        case 403:
          send_<AdminForbidden, Request>(
            hold.ptr(), ZuMv(result.body)); return;
        default:
          send_<HealthUnavailable, Request>(
            hold.ptr(), ZuMv(result.body)); return;
      }
    });
  }

  void listening(int, unsigned);
  void listenFailed(int, bool);
  void connected(int) { }
  void disconnected(int) { }

private:
  template <typename Request, typename Link>
  void adminSend_(Link *link, AdminResult result)
  {
    if (!m_requests->active()) {
      send_<HealthUnavailable, Request>(link, correlate_(
        error_("unavailable", "server is inactive"), correlation_()));
      return;
    }
    switch (result.status) {
      case 200: send_<HealthOK, Request>(link, ZuMv(result.body)); return;
      case 201: send_<AdminCreated, Request>(link, ZuMv(result.body)); return;
      case 202: send_<AdminAccepted, Request>(link, ZuMv(result.body)); return;
      case 400: send_<AdminBadRequest, Request>(link, ZuMv(result.body)); return;
      case 401: send_<AdminUnauthorized, Request>(link, ZuMv(result.body)); return;
      case 403: send_<AdminForbidden, Request>(link, ZuMv(result.body)); return;
      case 404: send_<AdminNotFound, Request>(link, ZuMv(result.body)); return;
      case 409: send_<AdminConflict, Request>(link, ZuMv(result.body)); return;
      case 412: send_<AdminPrecondition, Request>(link, ZuMv(result.body)); return;
      case 428: send_<AdminRequired, Request>(link, ZuMv(result.body)); return;
      case 429: send_<AdminLimited, Request>(link, ZuMv(result.body)); return;
      case 503: send_<HealthUnavailable, Request>(link, ZuMv(result.body)); return;
      default:
        send_<AdminNotImplemented, Request>(link, ZuMv(result.body)); return;
    }
  }

  template <typename Response, typename Request, typename Link>
  void send_(Link *link, String body, String allow = {})
  {
    ZmRef<DaemonResponse> object = new DaemonResponse{};
    object->body = ZuMv(body);
    object->allow = ZuMv(allow);
    ZmRef<DaemonBuilderQ::Node> response = new DaemonBuilderQ::Node{};
    response->template init<Response, Request>(object.ptr());
    link->send(ZuMv(response));
  }

  static String page_(Bytes, String);
  static String bootstrapPage_();
  static String enrollPage_();
  static String error_(ZuCSpan, ZuCSpan);
  String correlation_();
  static String correlate_(String, ZuCSpan);
  static String issuerJSON_(const Issuer &);
  static String operationsJSON_(unsigned, unsigned, ZuBSpan);
  void adminAuth_(String, AdminAuthFn);
  void adminRequest_(int, String, String, String, String,
    String, String, String, String, AdminDoneFn);
  static bool adminTargetApp(ZuCSpan, AppID &);
  void adminAccess_(int, String, bool, AppID, bool, AdminAccessFn);
  static Bytes idemDigest_(int, ZuCSpan, ZuCSpan, ZuCSpan, ZuCSpan);
  void idemBegin_(int, ActorKind::T, String, Bytes, String, IdemBeginFn);
  void adminAudit_(int, String, AppID, String, String,
    AdminResult, AdminDoneFn);
  void adminCall_(int, Principal, AdminPermit, String, String, String,
    String, String, String, IdemRequest, AdminDoneFn);
  void serviceCall_(int, String, String, AdminDoneFn);
  void serviceRequest_(int, String, String, AdminDoneFn);
  void serviceAuthed_(int, Principal, String, AdminDoneFn);
  void roleDelete_(AppID, RoleID, String, IdemRequest, AdminDoneFn);
  void authRoute_(AppID, String, AuthRouteDoneFn);
  bool loadKey_();

  DB			*m_db = nullptr;
  DBContext		*m_context = nullptr;
  Requests		*m_requests = nullptr;
  ZiMultiplex		*m_mx = nullptr;
  DaemonConfig		m_config;
  Server		m_provider;
  ZmRef<Ztls::PK::SK_EC> m_key;
  String		m_signKeyID;
  Ztls::Random		m_rng;
  Zhttp::Server<Daemon>	m_http;
  bool			m_httpInited = false;
};

template <typename Link>
void LiveReq::complete(Link *link, bool ok) { app->live(link, *this, ok); }
template <typename Link>
void ReadyReq::complete(Link *link, bool ok) { app->ready(link, *this, ok); }
template <typename Link>
void BootstrapReq::complete(Link *link, bool ok) {
  app->bootstrap(link, *this, ok);
}
template <typename Link>
void EnrollReq::complete(Link *link, bool ok) {
  app->enroll(link, *this, ok);
}
template <typename Impl, Zhttp::Method::T Method_, unsigned Body_>
template <typename Link>
void AdminReq<Impl, Method_, Body_>::complete(Link *link, bool ok) {
  app->admin(link, *static_cast<Impl *>(this), ok);
}
template <typename Link>
void ServiceAuthorizeReq::complete(Link *link, bool ok) {
  app->service<ServiceOp::Authorize>(link, *this, ok);
}
template <typename Link>
void ServiceTokenReq::complete(Link *link, bool ok) {
  app->service<ServiceOp::Token>(link, *this, ok);
}
template <typename Link>
void ServiceRevokeReq::complete(Link *link, bool ok) {
  app->service<ServiceOp::Revoke>(link, *this, ok);
}

} // namespace Zum

#endif /* ZumDaemon_HH */
