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
  ZuTestScopeRT(managementCatalog);
  ZuCheckRT(Zum::MgmtOp::N == 61);
  ZuCheckRT(!Zum::managementAction(-1));
  ZuCheckRT(!Zum::managementAction(Zum::MgmtOp::N));
  ZuCheckRT(Zum::managementAction(Zum::MgmtOp::userRecover) ==
    "Zum.userRecover");
  ZuCheckRT(Zum::managementAction(Zum::MgmtOp::assignmentAdd) ==
    "Zum.assignmentAdd");
  ZuCheckRT(Zum::MgmtOp::lookup("assignmentQuery") ==
    Zum::MgmtOp::assignmentQuery &&
    Zum::MgmtOp::lookup("membershipQuery") < 0);
  ZuCheckRT(Zum::CoreAction::N == Zum::MgmtOp::N &&
    !Zum::coreAction(Zum::CoreAction::N));
  ZuCheckRT(Zum::managementNeedsIdempotency(Zum::MgmtOp::appEnroll) &&
    Zum::managementNeedsIdempotency(Zum::MgmtOp::catalogPublish) &&
    !Zum::managementNeedsIdempotency(Zum::MgmtOp::appState) &&
    !Zum::managementNeedsIdempotency(Zum::MgmtOp::appQuery));
  for (unsigned i = 0; i < Zum::MgmtOp::N; ++i) {
    const auto *route = Zum::managementRoute(i);
    if (!Zum::managementAction(i)) {
      ZuCheckRT(!route && !Zum::managementAudited(i));
      continue;
    }
    ZuCheckRT(route && Zum::managementAudited(i) ==
      (route->method != Zhttp::Method::GET));
  }
  ZuCheckRT(!Zum::managementAudited(-1) &&
    !Zum::managementAudited(Zum::MgmtOp::N));
  auto audit = Zum::managementAuditRecord("issuer", Zum::MgmtOp::roleActions,
    "administrator", 42, "/admin/apps/42/roles/7/actions", "request-1",
    412, 123);
  ZuCheckRT(audit.time == 123 && audit.issuer == "issuer" &&
    audit.appID == 42 && audit.operationID == Zum::MgmtOp::roleActions &&
    audit.actor == "administrator" &&
    audit.target == "/admin/apps/42/roles/7/actions" &&
    audit.event == Zum::AuditEvent::Administration &&
    audit.outcome == Zum::AuditOutcome::Failure &&
    audit.correlationID == "request-1" && audit.detail == "roleActions");
  ZuCheckRT(Zum::MgmtOp::lookup("credentialAdd") < 0);
  ZuCheckRT(Zum::MgmtOp::lookup("auditUpdate") < 0);
  ZuCheckRT(Zum::MgmtOp::lookup("auditQuery") < 0 &&
    Zum::MgmtOp::lookup("auditCleanup") < 0);
  ZuCheckRT(Zum::managementOperation(Zhttp::Method::GET, "/admin/audit") < 0 &&
    Zum::managementOperation(Zhttp::Method::POST, "/admin/audit/cleanup") < 0);
  ZuCheckRT(Zum::MgmtOp::lookup("grantCreate") < 0);
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
  ZuCheckRT(unique);
  ZuCheckRT(Zum::managementAllow("/admin/apps") == "GET, POST");
  ZuCheckRT(Zum::managementAllow("/admin/apps/42?ignored=true") == "PATCH");
  ZuCheckRT(Zum::managementOperation(
    Zhttp::Method::PATCH, "/admin/apps/state") == Zum::MgmtOp::appUpdate);
  ZuCheckRT(Zum::managementOperation(
    Zhttp::Method::PUT, "/admin/apps/42/state") == Zum::MgmtOp::appState);
  ZuCheckRT(Zum::managementOperation(
    Zhttp::Method::PUT, "/apps/42/state") == Zum::MgmtOp::appState);
  ZuCheckRT(Zum::managementAllow("/apps") == "GET, POST");
  ZuCheckRT(Zum::managementAllow("/admin/apps/42/assignments") == "GET, POST");
  ZuCheckRT(Zum::managementOperation(Zhttp::Method::GET,
    "/admin/apps/42/assignments?user_id=7") == Zum::MgmtOp::assignmentQuery);
  ZuCheckRT(Zum::managementOperation(Zhttp::Method::POST,
    "/admin/apps/42/assignments") == Zum::MgmtOp::assignmentAdd);
  ZuCheckRT(Zum::managementOperation(Zhttp::Method::PUT,
    "/admin/apps/42/assignments/7/roles") == Zum::MgmtOp::assignmentRoles);
  ZuCheckRT(Zum::managementOperation(Zhttp::Method::PUT,
    "/admin/apps/42/assignments/7/state") == Zum::MgmtOp::assignmentState);
  ZuCheckRT(!Zum::managementAllow("/admin/apps/42/memberships"));
  ZuCheckRT(Zum::managementOperation(
    Zhttp::Method::GET, "/admin/apps/") < 0);
  ZuCheckRT(Zum::managementOperation(
    Zhttp::Method::GET, "/admin//apps") < 0);
  ZuCheckRT(Zum::managementOperation(
    Zhttp::Method::PUT, "/admin/apps/42/state/extra") < 0);
  ZuCheckRT(!Zum::managementAllow("/admin/unknown"));
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
  ZuTestScopeRT(requests);
  ZmSchedParams params;
  params.nThreads(1);
  params.thread(1).isolated(true);
  ZmScheduler scheduler{ZuMv(params)};
  ZuCheckRT(scheduler.start());

  ZmRef<Zum::Requests> requests = new Zum::Requests{};
  ZuCheckRT(requests->init(&scheduler, 1, 1));
  ZuCheckRT(ZmBlock<bool>{}([requests](auto wake) {
    requests->activate(ZuMv(wake));
  }));

  ZmRef<Zum::Request> pending;
  ZmSemaphore started;
  ZmSemaphore finished, retired;
  ZmAtomic<unsigned> completed = 0;
  ZuCheckRT(requests->run(Zm::now() + ZuTime{60}, [
    &pending, &started
  ](ZmRef<Zum::Request> request) mutable {
    pending = ZuMv(request);
    started.post();
  }, [&completed]() { ++completed; }));
  started.wait();
  ZuCheckRT(requests->count() == 1);
  ZuCheckRT(!requests->run(Zm::now() + ZuTime{60}, [](
      ZmRef<Zum::Request>) { }, [&completed]() { ++completed; }));
  pending->complete([&requests, &completed, &finished]() {
    // The slot still owns a cancelled deadline until its retirement turn.
    ZuCheckRT(!requests->run(Zm::now(60), [](ZmRef<Zum::Request>) { }, []() { }));
    ++completed;
    finished.post();
  });
  finished.wait();
  scheduler.run([&retired]() { retired.post(); }, 1);
  retired.wait();
  ZuCheckRT(completed == 1);

  ZmSemaphore timedOut;
  ZuCheckRT(requests->run(Zm::now() + ZuTime{0.01}, [](
      ZmRef<Zum::Request>) { }, [&completed, &timedOut]() {
    ++completed;
    timedOut.post();
  }));
  timedOut.wait();
  scheduler.run([&retired]() { retired.post(); }, 1);
  retired.wait();
  ZuCheckRT(completed == 2 && !requests->count());

  ZmRef<Zum::Request> late;
  started.reset();
  ZuCheckRT(requests->run(Zm::now() + ZuTime{60}, [
    &late, &started
  ](ZmRef<Zum::Request> request) mutable {
    late = ZuMv(request);
    started.post();
  }, [&completed]() { ++completed; }));
  started.wait();
  ZmSemaphore deactivated;
  requests->deactivate([&deactivated](bool) { deactivated.post(); });
  deactivated.wait();
  ZuCheckRT(!requests->active() && !requests->count() && completed == 3);
  late->complete([&completed]() { ++completed; });
  ZmSemaphore drained;
  scheduler.run([&drained]() { drained.post(); }, 1);
  drained.wait();
  ZuCheckRT(completed == 3);
  ZuCheckRT(!requests->run(Zm::now() + ZuTime{60}, [](
      ZmRef<Zum::Request>) { }, [&completed]() { ++completed; }));

  // Deactivation requested from cancellation must wait for that callback
  // and the same retirement barrier as the first caller.
  ZuCheckRT(ZmBlock<bool>{}([requests](auto wake) {
    requests->activate(ZuMv(wake));
  }));
  ZuCheckRT(ZmBlock<bool>{}([requests](auto wake) {
    requests->activate(ZuMv(wake));
  }));
  bool cancelling = false;
  ZmSemaphore callbacks;
  started.reset();
  bool admitted = requests->run(Zm::now() + ZuTime{60},
    [&started](ZmRef<Zum::Request>) { started.post(); },
    [&requests, &cancelling, &callbacks]() {
      cancelling = true;
      requests->deactivate([&cancelling, &callbacks](bool) {
	ZuCheckRT(!cancelling);
	callbacks.post();
      });
      cancelling = false;
    });
  ZuCheckRT(admitted);
  started.wait();
  requests->deactivate([&cancelling, &callbacks](bool) {
    ZuCheckRT(!cancelling);
    callbacks.post();
  });
  callbacks.wait();
  callbacks.wait();
  ZuCheckRT(!requests->active() && !requests->count());

  // Activation during cancellation is a pending opposite operation.
  ZmSemaphore activated;
  ZuCheckRT(ZmBlock<bool>{}([requests](auto wake) {
    requests->activate(ZuMv(wake));
  }));
  started.reset();
  ZuCheckRT(requests->run(Zm::now() + ZuTime{60},
    [&started](ZmRef<Zum::Request>) { started.post(); },
    [&requests, &activated]() {
      requests->activate([&activated](bool) { activated.post(); });
    }));
  started.wait();
  requests->deactivate([&deactivated](bool) { deactivated.post(); });
  deactivated.wait();
  activated.wait();
  ZuCheckRT(requests->active());
  requests->deactivate([&deactivated](bool) { deactivated.post(); });
  deactivated.wait();

  // Latest-intent cancellation of a queued opposite operation is distinct
  // from completing an actual stop with a diagnostic failure. All calls here
  // precede the startup owner continuation without blocking that owner.
  bool superseded = false;
  scheduler.run([requests, &superseded, &callbacks]() {
    requests->activate();
    requests->deactivate([&superseded, &callbacks](bool ok) {
      superseded = !ok;
      callbacks.post();
    });
    requests->activate();
  }, 1);
  callbacks.wait();
  ZuCheckRT(superseded && requests->state() == ZmEngineState::Running);
  ZuCheckRT(ZmBlock<bool>{}([requests](auto wake) {
    requests->deactivate(ZuMv(wake));
  }));

  requests = {};
  // A cancelled completion token may outlive its admission controller.
  late->complete([&completed]() { ++completed; });
  pending->complete([&completed]() { ++completed; });
  ZuCheckRT(completed == 3);
  late = {};
  pending = {};

  // Reserve enough admissions in one owner turn to leave a second retirement
  // batch behind the initial stop barrier. Its completion must notify stop;
  // no repeated readiness check may be needed while those credits remain.
  constexpr unsigned batchCount = Zum::RequestWork::Batch + 1;
  requests = new Zum::Requests{};
  ZuCheckRT(requests->init(&scheduler, 1, batchCount));
  ZuCheckRT(ZmBlock<bool>{}([requests](auto wake) {
    requests->activate(ZuMv(wake));
  }));
  unsigned cancelled = 0;
  bool batchOK = false;
  scheduler.run([&requests, &cancelled, &batchOK, &deactivated]() {
    for (unsigned i = 0; i < batchCount; ++i) {
      bool accepted = requests->run(Zm::now(60),
        [](ZmRef<Zum::Request>) { }, [&cancelled]() { ++cancelled; });
      ZuCheckRT(accepted);
    }
    requests->deactivate([&requests, &cancelled, &batchOK, &deactivated](bool ok) {
      batchOK = ok && cancelled == batchCount && !requests->count();
      requests = {}; // callback-driven destruction after the actual last batch
      deactivated.post();
    });
  }, 1);
  deactivated.wait();
  ZuCheckRT(batchOK && !requests);
  ZuCheckRT(scheduler.stop());
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
