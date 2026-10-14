//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuBase64URL.hh>
#include <zlib/ZuMatcher.hh>

#include <string.h>

#include <zlib/ZmBlock.hh>
#include <zlib/ZmSemaphore.hh>

#include <zlib/ZfCf.hh>

#include "../../zdb/test/ZdbTestCf.hh"
#include <zlib/ZiLog.hh>

#include <zlib/ZvMxParams.hh>

#include <zlib/ZdbMemStore.hh>

#include <zlib/zumd_admin.hh>
#include <zlib/zumd_authorize.hh>
#include <zlib/zumd_db.hh>
#include <zlib/zumd_db_ops.hh>
#include <zlib/zumd_key_db.hh>
#include <zlib/zumd_discovery.hh>
#include <zlib/zumd_http.hh>
#include <zlib/zumd_jwt.hh>
#include <zlib/zumd_oauth.hh>
#include <zlib/zumd_oidc.hh>
#include <zlib/zumd_passkey.hh>
#include <zlib/zumd_request.hh>
#include <zlib/zumd_session.hh>
#include <zlib/zumd_token.hh>
#include <zlib/zumd_revoke.hh>
#include <zlib/zumd_webauthn.hh>

#include <zlib/ZtlsMD.hh>
#include <zlib/ZtlsCOSE.hh>
#include <zlib/ZtlsPK.hh>
#include <zlib/ZtlsRandom.hh>
#include <zlib/ZtlsSec.hh>

#include "ZumTestSupport.hh"

using namespace ZuTestUtil;
using namespace zumtest;


static void managementCatalog()
{
  ZuTestScope(managementCatalog);
  ZuCheck(Zum::MgmtOp::N == 61);
  ZuCheck(!Zum::managementAction(-1));
  ZuCheck(!Zum::managementAction(Zum::MgmtOp::N));
  ZuCheck(Zum::managementAction(Zum::MgmtOp::userRecover) ==
    "Zum.userRecover");
  ZuCheck(Zum::managementAction(Zum::MgmtOp::assignmentAdd) ==
    "Zum.assignmentAdd");
  ZuCheck(Zum::MgmtOp::lookup("assignmentQuery") ==
    Zum::MgmtOp::assignmentQuery &&
    Zum::MgmtOp::lookup("membershipQuery") < 0);
  ZuCheck(Zum::CoreAction::N == Zum::MgmtOp::N &&
    !Zum::coreAction(Zum::CoreAction::N));
  ZuCheck(Zum::managementNeedsIdempotency(Zum::MgmtOp::appEnroll) &&
    Zum::managementNeedsIdempotency(Zum::MgmtOp::catalogPublish) &&
    !Zum::managementNeedsIdempotency(Zum::MgmtOp::appState) &&
    !Zum::managementNeedsIdempotency(Zum::MgmtOp::appQuery));
  for (unsigned i = 0; i < Zum::MgmtOp::N; ++i) {
    const auto *route = Zum::managementRoute(i);
    if (!Zum::managementAction(i)) {
      ZuCheck(!route && !Zum::managementAudited(i));
      continue;
    }
    ZuCheck(route && Zum::managementAudited(i) ==
      (route->method != Zhttp::Method::GET));
  }
  ZuCheck(!Zum::managementAudited(-1) &&
    !Zum::managementAudited(Zum::MgmtOp::N));
  auto audit = Zum::managementAuditRecord("issuer", Zum::MgmtOp::roleActions,
    "administrator", 42, "/admin/apps/42/roles/7/actions", "request-1",
    412, 123);
  ZuCheck(audit.time == 123 && audit.issuer == "issuer" &&
    audit.appID == 42 && audit.operationID == Zum::MgmtOp::roleActions &&
    audit.actor == "administrator" &&
    audit.target == "/admin/apps/42/roles/7/actions" &&
    audit.event == Zum::AuditEvent::Administration &&
    audit.outcome == Zum::AuditOutcome::Failure &&
    audit.correlationID == "request-1" && audit.detail == "roleActions");
  ZuCheck(Zum::MgmtOp::lookup("credentialAdd") < 0);
  ZuCheck(Zum::MgmtOp::lookup("auditUpdate") < 0);
  ZuCheck(Zum::MgmtOp::lookup("auditQuery") < 0 &&
    Zum::MgmtOp::lookup("auditCleanup") < 0);
  ZuCheck(Zum::managementOperation(Zhttp::Method::GET, "/admin/audit") < 0 &&
    Zum::managementOperation(Zhttp::Method::POST, "/admin/audit/cleanup") < 0);
  ZuCheck(Zum::MgmtOp::lookup("grantCreate") < 0);
  bool unique = true;
  for (unsigned i = 0; i < Zum::MgmtOp::N; ++i) {
    if (!Zum::managementAction(i)) continue;
    unique &= Zum::MgmtOp::lookup(Zum::MgmtOp::name(i)) == i;
    auto route = Zum::managementRoute(i);
    unique &= route && route->op == int(i) && route->path &&
      Zum::managementOperation(route->method, route->path) == int(i);
    for (unsigned j = 0; j < i; ++j)
      unique &= Zum::managementAction(i) != Zum::managementAction(j) &&
        (!route || !Zum::managementRoute(j) ||
         route->method != Zum::managementRoute(j)->method ||
         ZuCSpan{route->path} != Zum::managementRoute(j)->path);
  }
  unique &= !Zum::managementRoute(-1) &&
    !Zum::managementRoute(Zum::MgmtOp::N) &&
    Zum::managementOperation(Zhttp::Method::GET, "/admin/unknown") < 0;
  ZuCheck(unique);
  ZuCheck(Zum::managementAllow("/admin/apps") == "GET, POST");
  ZuCheck(Zum::managementAllow("/admin/apps/42?ignored=true") == "PATCH");
  ZuCheck(Zum::managementOperation(
    Zhttp::Method::PATCH, "/admin/apps/state") == Zum::MgmtOp::appUpdate);
  ZuCheck(Zum::managementOperation(
    Zhttp::Method::PUT, "/admin/apps/42/state") == Zum::MgmtOp::appState);
  ZuCheck(Zum::managementOperation(
    Zhttp::Method::PUT, "/apps/42/state") == Zum::MgmtOp::appState);
  ZuCheck(Zum::managementAllow("/apps") == "GET, POST");
  ZuCheck(Zum::managementAllow("/admin/apps/42/assignments") == "GET, POST");
  ZuCheck(Zum::managementOperation(Zhttp::Method::GET,
    "/admin/apps/42/assignments?user_id=7") == Zum::MgmtOp::assignmentQuery);
  ZuCheck(Zum::managementOperation(Zhttp::Method::POST,
    "/admin/apps/42/assignments") == Zum::MgmtOp::assignmentAdd);
  ZuCheck(Zum::managementOperation(Zhttp::Method::PUT,
    "/admin/apps/42/assignments/7/roles") == Zum::MgmtOp::assignmentRoles);
  ZuCheck(Zum::managementOperation(Zhttp::Method::PUT,
    "/admin/apps/42/assignments/7/state") == Zum::MgmtOp::assignmentState);
  ZuCheck(!Zum::managementAllow("/admin/apps/42/memberships"));
  ZuCheck(Zum::managementOperation(
    Zhttp::Method::GET, "/admin/apps/") < 0);
  ZuCheck(Zum::managementOperation(
    Zhttp::Method::GET, "/admin//apps") < 0);
  ZuCheck(Zum::managementOperation(
    Zhttp::Method::PUT, "/admin/apps/42/state/extra") < 0);
  ZuCheck(!Zum::managementAllow("/admin/unknown"));
}
static void jsonContract()
{
  ZuTestScope(jsonContract);
  Zum::Cred credential{.id = Zum::Bytes{ZuBSpan{"\xfb\xff"}}};
  ZtString<> credentialJSON;
  ZfJSON::AsObject::Handler<Zum::Cred, ZuFacet::JSON>::
    template save<Zum::PublicField>(credentialJSON, credential);
  ZuCheck(credentialJSON.find<"\"id\":\"-_8\"">() >= 0);
  auto encode = []<typename T>(const T &value) {
    Zum::String json;
    ZfJSON::AsObject::Handler<T, ZuFacet::JSON>::
      template save<Zum::PublicField>(json, value);
    return json;
  };
  ZuCheck(encode(Zum::App{.catalogDigest = credential.id}).
    find<"\"catalog_digest\":\"-_8\"">() >= 0);
  ZuCheck(encode(Zum::User{.handle = credential.id}).
    find<"\"handle\":\"-_8\"">() >= 0);
  auto grantJSON = encode(Zum::Grant{
    .id = credential.id, .credentialID = credential.id});
  ZuCheck(grantJSON.find<"\"id\":\"-_8\"">() >= 0 &&
    grantJSON.find<"\"credential_id\":\"-_8\"">() >= 0);
  Zum::Role role{
    .appID = UINT64_MAX - 1,
    .id = 9007199254740993ULL,
    .name = "reader",
    .catalogRevision = 9007199254740995ULL,
    .version = 9007199254740996ULL
  };
  ZtString<> json;
  ZfJSON::AsObject::Handler<Zum::Role, ZuFacet::JSON>::
    template save<Zum::PublicField>(json, role);
  ZuCheck(json.find<"\"app_id\":\"18446744073709551614\"">() >= 0 &&
    json.find<"\"id\":\"9007199254740993\"">() >= 0 &&
    json.find<"\"name\":\"reader\"">() >= 0 &&
    json.find<"\"catalog_revision\":\"9007199254740995\"">() >= 0);
  ZuCheck(json.find<"\"appID\"">() < 0 &&
    json.find<"\"catalogRevision\"">() < 0);
  auto clientJSON = encode(Zum::Client{
    .grants = uint8_t(Zum::ClientGrant::AuthCode() | Zum::ClientGrant::Refresh())});
  ZuCheck(clientJSON.find<"\"grants\":\"AuthCode,Refresh\"">() >= 0);
  ZuCheck(clientJSON.find<"\"redirect_uris\"">() >= 0 &&
    clientJSON.find<"\"redirectURIs\"">() < 0);
  auto parsed = ZfJSON::scan(json.span());
  ZuCheck(parsed.p<0>() == int(json.length()) && parsed.p<1>());
  auto &rows = parsed.p<1>()->data<ZfJSON::AnyNode::Array>();
  auto handler = ZfJSON::handler<Zum::Role>(rows[0]);
  ZuCheck(handler.valid);
  auto decoded = handler.ctor();
  ZuCheck(decoded.appID == role.appID &&
    decoded.catalogRevision == role.catalogRevision);

}
static void requests()
{
  ZuTestScope(requests);
  ZmSchedParams params;
  params.nThreads(1);
  params.thread(1).isolated(true);
  ZmScheduler scheduler{ZuMv(params)};
  ZuCheck(scheduler.start());

  ZmRef<Zum::Requests> requests = new Zum::Requests{};
  ZuCheck(requests->init(&scheduler, 1, 1));
  requests->activate();

  ZmRef<Zum::Request> pending;
  ZmSemaphore started;
  ZmSemaphore finished;
  ZmAtomic<unsigned> completed = 0;
  ZuCheck(requests->run(Zm::now() + ZuTime{60}, [
    &pending, &started
  ](ZmRef<Zum::Request> request) mutable {
    pending = ZuMv(request);
    started.post();
  }, [&completed]() { ++completed; }));
  started.wait();
  ZuCheck(requests->count() == 1);
  ZuCheck(!requests->run(Zm::now() + ZuTime{60}, [](
      ZmRef<Zum::Request>) { }, [&completed]() { ++completed; }));
  pending->complete([&completed, &finished]() {
    ++completed;
    finished.post();
  });
  finished.wait();
  ZuCheck(completed == 1);

  ZmSemaphore timedOut;
  ZuCheck(requests->run(Zm::now() + ZuTime{0.01}, [](
      ZmRef<Zum::Request>) { }, [&completed, &timedOut]() {
    ++completed;
    timedOut.post();
  }));
  timedOut.wait();
  ZuCheck(completed == 2 && !requests->count());

  ZmRef<Zum::Request> late;
  started.reset();
  ZuCheck(requests->run(Zm::now() + ZuTime{60}, [
    &late, &started
  ](ZmRef<Zum::Request> request) mutable {
    late = ZuMv(request);
    started.post();
  }, [&completed]() { ++completed; }));
  started.wait();
  ZmSemaphore deactivated;
  requests->deactivate([&deactivated]() { deactivated.post(); });
  deactivated.wait();
  ZuCheck(!requests->active() && !requests->count() && completed == 3);
  late->complete([&completed]() { ++completed; });
  ZmSemaphore drained;
  scheduler.run([&drained]() { drained.post(); }, 1);
  drained.wait();
  ZuCheck(completed == 3);
  ZuCheck(!requests->run(Zm::now() + ZuTime{60}, [](
      ZmRef<Zum::Request>) { }, [&completed]() { ++completed; }));

  late = {};
  pending = {};
  requests = {};
  ZuCheck(scheduler.stop());
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(managementCatalog);
  ZuTestCall(jsonContract);
  ZuTestCall(requests);
  return 0;
}
