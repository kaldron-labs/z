//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// zumd REST/OIDC server

#ifndef zumd_daemon_HH
#define zumd_daemon_HH

#ifndef ZumLib_HH
#include <zlib/ZuDerive.hh>
#include <zlib/ZumLib.hh>
#endif

#include <zlib/ZmList.hh>
#include <zlib/ZmSemaphore.hh>

#include <zlib/ZhttpServer.hh>
#include <zlib/ZrestServer.hh>
#include <zlib/ZfJSON.hh>

#include <zlib/zumd_http.hh>
#include <zlib/zumd_identity_db.hh>
#include <zlib/ZumMgmt.hh>
#include <zlib/zumd_jwt.hh>
#include <zlib/zumd_request.hh>

#include <zlib/ZtlsPK.hh>

#include "zumd_bootstrap.hh"
#include "zumd_revoke.hh"

namespace Zum {

struct DaemonConfig {
  String	addr{"127.0.0.1"};
  uint16_t	port = 8080;
  String	issuer;
  // Issuer used in SSF SETs; defaults to the public authorization issuer.
  String	ssfIssuer;
  String	rpID;
  String	rpName{"Zum"};
  String	admin;
  Bytes		dbKey;
  OIDCHTTPFn	oidcHTTP;
	RefreshRevokeFn	refreshRevoke;
  SSFSecretFn	ssfSecret;
  SSFReceiverVec	ssfReceivers;
  uint64_t	requestTimeout = 15;
  uint64_t	cleanupInterval = 300;
  ServerBootstrapResult bootstrap;
};

struct MaintenanceResult {
  unsigned selected = 0;
  unsigned deleted = 0;
  unsigned skipped = 0;
  unsigned conflicted = 0;
  unsigned failed = 0;
  unsigned rescheduled = 0;
  bool ok = false;
};
ZuDerive(MaintenanceFn,
  (ZmFn<void(MaintenanceResult), ZmFnHeapID<"Zum.MaintenanceFn">>));
ZumExtern void daemonGrantCleanup(
  DB *, DBContext *, Ztls::Random *, unsigned, MaintenanceFn);
ZumExtern void daemonRefreshCleanup(
  DB *, DBContext *, Ztls::Random *, unsigned, MaintenanceFn);
ZumExtern void daemonSessionCleanup(
  DB *, DBContext *, Ztls::Random *, unsigned, MaintenanceFn);
ZumExtern void daemonAppCleanup(
  DB *, DBContext *, Ztls::Random *, AppID, unsigned, MaintenanceFn);
// Lower than interactive query admission to keep periodic work within one
// scheduler turn and prevent cleanup payloads from competing with queries.
namespace DaemonCleanupLimit { enum { Rows = 512 }; }
namespace DaemonCleanupState { enum { Idle, Running, Stopping }; }

template <typename Heap = ZuVoid>
struct DaemonResponse_ : public Heap, public HTTPResponse {
  static void *operator new(size_t) { return Heap::operator new(0); }
  static void operator delete(void *p) noexcept { Heap::operator delete(p); }

  String	allow;
};
ZuDerive(DaemonResponseHeap,
  (ZmHeap<"zumd.DaemonResponse", DaemonResponse_<>>));
ZuDerive(DaemonResponse, (DaemonResponse_<DaemonResponseHeap>));

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
  static constexpr uint64_t QueryLimit = ServerLimitMax::Query;
  using Path = Path_;
  App *app = nullptr;
};

struct LiveReq : public DaemonGet<Daemon, LiveReq,
    ZuStringT<"/live">> {
  using Responses = ZuTypeList<HealthOK>;
  template <typename Link> void complete(Link *link, bool ok);
};

struct ReadyReq : public DaemonGet<Daemon, ReadyReq,
    ZuStringT<"/ready">> {
  using Responses = ZuTypeList<HealthOK, HealthUnavailable>;
  template <typename Link> void complete(Link *link, bool ok);
};

struct BootstrapReq : public DaemonGet<Daemon, BootstrapReq,
    ZuStringT<"/enroll">> {
  using Responses = ZuTypeList<BootstrapPage, HealthUnavailable>;
  template <typename Link> void complete(Link *link, bool ok);
};

struct EnrollReq : public DaemonGet<Daemon, EnrollReq,
    ZuStringT<"">> {
  using Responses = ZuTypeList<BootstrapPage, HealthUnavailable>;
  template <typename Link> void complete(Link *link, bool ok);
};

struct OAuthMetadataReq : public Zrest::ReqParser<OAuthMetadataReq, HTTPData> {
  enum { Exact = 0 };
  using Path =
    ZuStringT<"/oauth-authorization-server/oauth2">;
  using Responses = ZuTypeList<DiscoveryOK, ServerError>;
  Daemon *app = nullptr;
  bool operation(Zhttp::Method::T method, Zhttp::Target &target) {
    if (!Zrest::ReqParser<OAuthMetadataReq, HTTPData>::operation(
        method, target)) return false;
    auto path = target.path;
    path.offset(Path{}().length());
    AppIDPath route;
    if (!ZfURI::loadPath(route, path) || !route.appID) return false;
    object->appID = route.appID;
    return true;
  }
  template <typename Link> void complete(Link *link, bool ok);
};

template <typename Heap = ZuVoid>
struct AdminData_ : public Heap, public HTTPDataFields {
  String target;
  String query;
  String body;
  String allow;
  int operation = -1;
};
ZuDerive(AdminDataHeap, (ZmHeap<"zumd.AdminData", AdminData_<>>));
ZuDerive(AdminData, (AdminData_<AdminDataHeap>));

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
  static constexpr uint64_t QueryLimit = ServerLimitMax::Query;
  static constexpr uint64_t BodyLimit = ServerLimitMax::JSON;
  using Path = ZuStringT<"/">;
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
    if (!target.path || target.path[0] != '/') return false;
    if (!Base::operation(method, target)) return false;
    auto query = target.path.find<"?">();
    ZuBSpan path{target.path};
    if (query >= 0) {
      path.trunc(unsigned(query));
      ZuBSpan querySpan{target.path};
      querySpan.offset(unsigned(query) + 1);
      this->object->query = querySpan;
    }
    this->object->operation = managementOperation(method, path);
    if (this->object->operation < 0)
      this->object->allow = managementAllow(path);
    else {
      this->object->target = "/admin";
      this->object->target << path;
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

using PublicRequests = HTTPRequests<Daemon>;
ZrestCatalogDerive(PublicCatalog, PublicRequests);
using HealthRequests = ZuTypeList<LiveReq, ReadyReq>;
ZrestCatalogDerive(HealthCatalog, HealthRequests);
using BootstrapRequests = ZuTypeList<BootstrapReq>;
ZrestCatalogDerive(BootstrapCatalog, BootstrapRequests);
using EnrollRequests = ZuTypeList<EnrollReq>;
ZrestCatalogDerive(EnrollCatalog, EnrollRequests);
using WellKnownRequests = ZuTypeList<OAuthMetadataReq>;
ZrestCatalogDerive(WellKnownCatalog, WellKnownRequests);
using AdminRequests = ZuTypeList<
  AdminGET, AdminPOST, AdminPUT, AdminPATCH, AdminDELETE>;
ZrestCatalogDerive(AdminCatalog, AdminRequests);
using ControlRequests = ZuTypeConcat<HealthRequests, BootstrapRequests,
  EnrollRequests, WellKnownRequests, AdminRequests>;
using DaemonRequests = ZuTypeConcat<PublicRequests, ControlRequests>;
ZrestCatalogDerive(DaemonCatalog, DaemonRequests);

template <typename Catalog>
struct DaemonReqParser : public Zrest::MReqParser<Catalog> {
  using Base = Zrest::MReqParser<Catalog>;
  void init(Daemon &app_) { app = &app_; }
  bool operation(Zhttp::Method::T method, Zhttp::Target &target) {
    if (!Base::operation(method, target)) return false;
    this->u.dispatch([this](auto, auto &request) { request.app = app; });
    return true;
  }
  Daemon *app = nullptr;
};

using PublicParser = HTTPParser<Daemon, PublicCatalog>;
using HealthParser = DaemonReqParser<HealthCatalog>;
using BootstrapParser = DaemonReqParser<BootstrapCatalog>;
using EnrollParser = DaemonReqParser<EnrollCatalog>;
using WellKnownParser = DaemonReqParser<WellKnownCatalog>;
using AdminParser = DaemonReqParser<AdminCatalog>;
using DaemonRoots = ZuTypeList<
  Zrest::ReqRoot<ZuStringT<"oauth2">, PublicParser>,
  Zrest::ReqRoot<ZuStringT<".well-known">, WellKnownParser>,
  Zrest::ReqRoot<ZuStringT<"health">, HealthParser>,
  Zrest::ReqRoot<ZuStringT<"bootstrap">, BootstrapParser>,
  Zrest::ReqRoot<ZuStringT<"enroll">, EnrollParser>,
  Zrest::ReqRoot<ZuStringT<"admin">, AdminParser>>;
ZrestRootCatalogDerive(DaemonRootCatalog, DaemonRoots);

struct DaemonParser : public Zrest::MReqParser<DaemonRootCatalog,
    Zrest::MReqRootPolicy<DaemonRootCatalog, Daemon>> { };

using DaemonBuilder = Zrest::MResBuilder<DaemonCatalog>;
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
    if (ok)
      send_<HealthOK, LiveReq>(link, httpJSON(HTTPStatus{"live"}));
  }

  template <typename Link>
  void ready(Link *link, const ReadyReq &, bool ok)
  {
    if (!ok || !m_requests->active()) {
      send_<HealthUnavailable, ReadyReq>(
        link, httpJSON(HTTPStatus{"unavailable"}));
      return;
    }
    m_provider.ready([this, hold = ZmRef<Link>{link}](ServerReply reply) mutable {
      if (m_requests->active() && reply.type == ReplyType::OK)
        send_<HealthOK, ReadyReq>(hold.ptr(), ZuMv(reply.body));
      else
        send_<HealthUnavailable, ReadyReq>(
          hold.ptr(), httpJSON(HTTPStatus{"not-ready"}));
    });
  }

  template <typename Link>
  void bootstrap(Link *link, const BootstrapReq &request, bool ok)
  {
    if (!ok || !request.object->data) {
      send_<HealthUnavailable, BootstrapReq>(
        link, httpJSON(HTTPStatus{"unavailable"}));
      return;
    }
    send_<BootstrapPage, BootstrapReq>(
      link, bootstrapPage_(m_config.bootstrap.coreAppID));
  }

  template <typename Link>
  void enroll(Link *link, const EnrollReq &request, bool ok)
  {
    if (!ok || !request.object->data) {
      send_<HealthUnavailable, EnrollReq>(
	link, httpJSON(HTTPStatus{"unavailable"}));
      return;
    }
    send_<BootstrapPage, EnrollReq>(
      link, enrollPage_(m_config.bootstrap.coreAppID));
  }

  template <typename Link>
  void oauthMetadata(Link *link, const OAuthMetadataReq &request, bool ok)
  {
    if (!ok || !request.object || !request.object->appID) {
      send_<ServerError, OAuthMetadataReq>(link, httpJSON(HTTPEmpty{}));
      return;
    }
    m_provider.metadata(request.object->appID, [this,
        hold = ZmRef<Link>{link}](ServerReply reply) mutable {
      if (reply.type == ReplyType::Discovery)
        send_<DiscoveryOK, OAuthMetadataReq>(hold.ptr(), ZuMv(reply.body));
      else
        send_<ServerError, OAuthMetadataReq>(
          hold.ptr(), httpJSON(HTTPEmpty{}));
    });
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
    int op = request.object->operation;
    if (op < 0) {
      String allow = request.object->allow;
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

  static String page_(AppID, Bytes, String);
  static String bootstrapPage_(AppID);
  static String enrollPage_(AppID);
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
  void roleDelete_(AppID, RoleID, String, IdemRequest, AdminDoneFn);
  void authRoute_(AppID, String, AuthRouteDoneFn);
  bool loadKey_();
  void cleanup_();
  void cleanupStart_();
  bool cleanupStop_();
  void cleanupDone_(MaintenanceResult);

  using Key = ZmRef<Ztls::PK::SK_EC>;
  using SSF = ZmRef<SSFTransmitter>;
  using HTTPServer = Zhttp::Server<Daemon>;
  using CleanupRun = ZmRef<ZmPolymorph>;

  DB			*m_db = nullptr;
  DBContext		*m_context = nullptr;
  Requests		*m_requests = nullptr;
  ZiMultiplex		*m_mx = nullptr;
  DaemonConfig		m_config;
  Server		m_provider;
  Key			m_key;
  SSF			m_ssf;
  SignFn		m_sign;
  String		m_signKeyID;
  Ztls::Random		m_rng;
  HTTPServer		m_http;
  ZmScheduler::Timer	m_cleanupTimer;
  CleanupRun		m_cleanupRun;
  bool			m_started = false;
  int			m_cleanupState = DaemonCleanupState::Idle;
  uint64_t		m_cleanupBackoff = 0;
  bool			m_httpInited = false;
  bool			m_finalized = false;
  ZmSemaphore		m_cleanupStopped;
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
template <typename Link>
void OAuthMetadataReq::complete(Link *link, bool ok) {
  app->oauthMetadata(link, *this, ok);
}
template <typename Impl, Zhttp::Method::T Method_, unsigned Body_>
template <typename Link>
void AdminReq<Impl, Method_, Body_>::complete(Link *link, bool ok) {
  app->admin(link, *static_cast<Impl *>(this), ok);
}
} // namespace Zum

#endif /* zumd_daemon_HH */
