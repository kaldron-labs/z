//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmBlock.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZfCf.hh>
#include "../../zdb/test/ZdbTestCf.hh"
#include <zlib/ZiLog.hh>
#include <zlib/ZvMxParams.hh>
#include <zlib/ZdbMemStore.hh>
#include <zlib/ZtlsRandom.hh>
#include <zlib/zumd_admin.hh>
#include <zlib/zumd_db.hh>
#include <zlib/zumd_identity_db.hh>
#include <zlib/zumd_app_db.hh>
#include <zlib/zumd_request.hh>
#include <zlib/zumd_token.hh>
#include <zlib/zumd_saga_revoke.hh>
#include <zlib/zumd_saga_grant_cleanup.hh>
#include <zlib/zumd_saga_ssf_delivery_add.hh>
#include <zlib/zumd_saga_image.hh>
#include <zlib/zumd_revoke.hh>

using namespace ZuTestUtil;

struct TestDB : public Zum::DB {
  ZmSemaphore	active;
  ZmRef<Zum::Requests> requests;
};

static ZuPtr<const ZfCf::AnyNode> dbConfig()
{
  auto scan = ZfCf::scan(
    "zdb: {\n"
    "  thread: zdb, shards: 1, threads: [shard],\n"
    "  store: {thread: store},\n"
    "  hostID: self, hosts: {self: {standalone: true}}, tables: {}\n"
    "},\n"
    "mx: {\n"
    "  nThreads: 5, rxThread: rx, txThread: tx, threads: {\n"
    "    1: {name: rx, isolated: true},\n"
    "    2: {name: tx, isolated: true},\n"
    "    3: {name: zdb, isolated: true},\n"
    "    4: {name: store, isolated: true},\n"
    "    5: {name: shard, isolated: true}\n"
    "  }\n"
    "}\n");
  return ZuMv(scan.p<1>());
}

static void dbUp(Zdb *db, ZdbHost *)
{
  auto test = static_cast<TestDB *>(db);
  test->requests->activate();
  test->active.post();
}

static void dbDown(Zdb *db, bool)
{
  static_cast<TestDB *>(db)->requests->deactivate();
}

static bool insertIssuer(
    Zum::DB *context, ZuCSpan id)
{
  return ZmBlock<bool>{}([context, id](auto wake) mutable {
    context->issuers->run(0, [context, id, wake = ZuMv(wake)]() mutable {
      ZdbRowRef<Zum::Issuer> row =
	new ZdbRow<Zum::Issuer>{context->issuers, ZdbShard{0}};
      context->issuers->insert(row, [id, wake = ZuMv(wake)](
	  ZdbRow<Zum::Issuer> *row) mutable {
	if (!row) { wake(false); return; }
	new (row->ptr()) Zum::Issuer{.id = Zum::String{id}};
	wake(row->commit());
      });
    });
  });
}

template <typename T>
static bool insertRecord(ZdbTable<T> *table, T data)
{

  return ZmBlock<bool>{}([
    table, data = ZuMv(data)
  ](auto wake) mutable {
    table->run(0, [table, data = ZuMv(data), wake = ZuMv(wake)]() mutable {
      ZdbRowRef<T> row = new ZdbRow<T>{table, ZdbShard{0}};
      table->insert(row, [
	data = ZuMv(data), wake = ZuMv(wake)
      ](ZdbRow<T> *row) mutable {
	if (!row) { wake(false); return; }
	new (row->ptr()) T{ZuMv(data)};
	wake(row->commit());
      });
    });
  });
}


static bool runSaga(TestDB *db, Zum::SagaRef saga, ZdbSagaID id)
{
  ZmSemaphore completed;
  bool terminal = false;
  bool submitted = ZmBlock<bool>{}([
      db, id, saga = ZuMv(saga), &completed, &terminal
  ](auto wake) mutable {
    if (!Zum::sagaSubmit(db, id, ZuMv(saga),
	[wake = ZuMv(wake)](bool ok) mutable { wake(ok); },
	[&completed, &terminal](bool ok) {
	  terminal = ok;
	  completed.post();
	})) wake(false);
  });
  if (!submitted) return false;
  completed.wait();
  return terminal;
}


static Zum::Grant loadGrant(Zum::DB *context, ZuBSpan id)
{
  return ZmBlock<Zum::Grant>{}([context, id](auto wake) mutable {
    context->grants->run(0, [context, id, wake = ZuMv(wake)]() mutable {
      context->grants->find<0>(0, ZuFwdTuple(id), [
	wake = ZuMv(wake)
      ](ZdbRowRef<Zum::Grant> row) mutable {
	wake(row ? Zum::Grant{row->data()} : Zum::Grant{});
      });
    });
  });
}

static Zum::Refresh loadRefresh(Zum::DB *context, ZuBSpan id)
{
  return ZmBlock<Zum::Refresh>{}([context, id](auto wake) mutable {
    context->refresh->run(0, [context, id, wake = ZuMv(wake)]() mutable {
      context->refresh->find<0>(0, ZuFwdTuple(id), [
	wake = ZuMv(wake)
      ](ZdbRowRef<Zum::Refresh> row) mutable {
	wake(row ? Zum::Refresh{row->data()} : Zum::Refresh{});
      });
    });
  });
}


static Zum::StringVec logRows;

template <typename ...Args>
static bool logged(Args &&...args)
{
  Zum::StringVec needles;
  (needles.push(Zum::String{ZuFwd<Args>(args)}), ...);
  return ZmBlock<bool>{}([needles = ZuMv(needles)](auto wake) mutable {
    // The marker executes after previously enqueued events, without sleeps.
    ZiLOG(Info, "ZumTest", ([needles = ZuMv(needles),
        wake = ZuMv(wake)](auto &) mutable {
      bool found = false;
      for (const auto &row : logRows) {
        bool match = true;
        for (const auto &needle : needles)
          if (!needle || row.find(needle) < 0) { match = false; break; }
        if (match) { found = true; break; }
      }
      wake(found);
    }));
  });
}


static void runtimeRevocation()
{
  ZuTestScope(runtimeRevocation);
  ZiLog::init("ZumRuntimeRevocationTest");
  ZiLog::level(0);
  logRows.null();
  ZiLog::sink(ZiLog::lambdaSink([](
      ZeLogBuf &buf, const ZeEventInfo &info) {
    if (info.component == "Zum")
      logRows.push(Zum::String{" "} << ZuCSpan{buf.data(), buf.length()} << " ");
  }));
  ZiLog::start();
  auto config = dbConfig();
  auto configFields = ZfCf::handler<zdbtest::ConfigFields>(config).ctor();
  ZiMultiplex mx{ZvMxParams("mx", configFields.mx.p<const ZfCf::AnyNode *>())};
  ZuCheck(mx.start());
  ZmRef<ZdbMem::Store> store = new ZdbMem::Store{};
  ZmRef<TestDB> db = new TestDB{};
  db->requests = new Zum::Requests{};
  ZuCheck(db->requests->init(&mx, 5, 64));
  db->init(ZdbCf{configFields.zdb.p<const ZfCf::AnyNode *>()}, &mx,
    ZdbHandler{.upFn = dbUp, .downFn = dbDown}, store);
  Zum::registerSchema(db);
  ZmRef<Zum::DB> context = db;
  ZuCheck(db->start());
  db->active.wait();
  ZuCheck(insertIssuer(context, "issuer"));
  ZuCheck(insertRecord(context->grants, Zum::Grant{
    .id = Zum::Bytes{ZuBSpan{"revocable"}},
    .kind = Zum::GrantKind::Code,
    .state = Zum::State::Active,
    .issuer = "issuer"}));
  Zum::RevokeArgs revokeRollback{.updated = 101};
  auto revocable = loadGrant(context, ZuBSpan{"revocable"});
  revokeRollback.grants.push(Zum::SagaImage::save(revocable));
  revocable.id = Zum::Bytes{ZuBSpan{"missing-revoke-grant"}};
  revokeRollback.grants.push(Zum::SagaImage::save(revocable));
  ZuCheck(!runSaga(db, Zum::revoke(ZuMv(revokeRollback)), ZdbSagaID{12007}));
  revocable = loadGrant(context, ZuBSpan{"revocable"});
  ZuCheck(revocable.state == Zum::State::Active && !revocable.owner);
  Zum::RevokeArgs revokeGrant{.updated = 101};
  revokeGrant.grants.push(Zum::SagaImage::save(revocable));
  ZuCheck(runSaga(db, Zum::revoke(ZuMv(revokeGrant)), ZdbSagaID{12008}));
  ZuCheck(loadGrant(context, ZuBSpan{"revocable"}).state ==
    Zum::State::Revoked);
  Zum::RevokeArgs revokeAgain{.updated = 102};
  revokeAgain.grants.push(Zum::SagaImage::save(loadGrant(context, ZuBSpan{"revocable"})));
  ZuCheck(!revokeAgain.count());
  ZuCheck(runSaga(db, Zum::revoke(ZuMv(revokeAgain)), ZdbSagaID{12009}));
  Zum::GrantCleanupArgs cleanup{.updated = 2};
  Zum::GrantCleanupArgs rollback{.updated = 1};
  auto expired = loadGrant(context, ZuBSpan{"revocable"});
  rollback.grants.push(Zum::grantDelete(expired));
  expired.id = Zum::Bytes{ZuBSpan{"changed-cleanup-grant"}};
  auto changedGrant = expired;
  changedGrant.expires = 2;
  ZuCheck(insertRecord(context->grants, changedGrant));
  rollback.grants.push(Zum::grantDelete(expired));
  ZuCheck(!runSaga(db, Zum::grantCleanup(ZuMv(rollback)), ZdbSagaID{12006}));
  ZuCheck(Zum::SagaImage::save(loadGrant(context, changedGrant.id)) ==
    Zum::SagaImage::save(changedGrant));
  // Maintenance deletes are terminal: a later conflict does not resurrect
  // an already expired row during saga compensation.
  ZuCheck(!loadGrant(context, ZuBSpan{"revocable"}).id);
  cleanup.grants.push(Zum::grantDelete(changedGrant));
  // Native deletion of an absent record succeeds without an application guard.
  expired.id = Zum::Bytes{ZuBSpan{"missing-cleanup-grant"}};
  cleanup.grants.push(Zum::grantDelete(expired));
  ZuCheck(runSaga(db, Zum::grantCleanup(ZuMv(cleanup)), ZdbSagaID{12005}));
  ZuCheck(!loadGrant(context, changedGrant.id).id);
  Zum::SSFDelivery ssfDelivery{
    .eventID = "event-1", .receiverID = "receiver-1",
    .familyIssuer = "issuer/oauth2/9", .familyID = "family-1",
    .familyExpires = 1000, .set = Zum::Bytes{ZuBSpan{"signed-set"}},
    .nextDelivery = 101};
  ZuCheck(runSaga(db, Zum::ssfDeliveryAdd(ZuMv(ssfDelivery)), ZdbSagaID{12023}));
  ZuCheck(ZmBlock<bool>{}([context](auto wake) mutable {
    auto table = context->ssfDeliveries;
    table->run(0, [table, wake = ZuMv(wake)]() mutable {
      table->find<0>(0, ZuFwdTuple(Zum::String{"event-1"},
          Zum::String{"receiver-1"}),
        [wake = ZuMv(wake)](ZdbRowRef<Zum::SSFDelivery> row) mutable {
          wake(row && !row->data().owner &&
            row->data().set == ZuBSpan{"signed-set"});
        });
    });
  }));
  Ztls::Random rng;
  ZuCheck(rng.init());
  ZuCheck(insertRecord(context->apps, Zum::App{
    .id = 9, .name = "oidc-app", .state = Zum::State::Active,
    .nextActionID = 1, .authVersion = 12, .audience = "orders"}));
  Zum::Client browser;
  browser.id = "browser";
  browser.appID = 9;
  browser.redirects.push("https://app/cb");
  browser.identityScopes.push("openid");
  browser.identityScopes.push("profile");
  browser.identityScopes.push("email");
  browser.profile = Zum::ClientProfile::Browser;
  browser.grants = Zum::ClientGrant::AuthCode() | Zum::ClientGrant::Refresh();
  browser.refreshAllowed = true;
  browser.state = Zum::State::Active;
  ZuCheck(insertRecord(context->clients, ZuMv(browser)));
  Zum::OpaqueToken revokeToken;
  ZuCheck(Zum::opaqueIssue(rng, revokeToken));
  ZuCheck(insertRecord(context->refresh, Zum::Refresh{
    .id = revokeToken.id,
    .expires = 1000,
    .state = Zum::State::Active,
    .issuer = "issuer",
    .clientID = "browser",
    .digest = revokeToken.digest
  }));
  Zum::String revokeForm{"token="};
  revokeForm << revokeToken.token << "&client_id=browser";
  ZuCheck(ZmBlock<int>{}([
    &db, &context, &revokeForm
  ](auto wake) mutable {
    Zum::revokeRequest(db->requests, Zm::now() + ZuTime{10}, context,
      ZuMv(revokeForm), {}, Zum::RevokeConfig{
	.issuer = "issuer", .appID = 9, .now = 452
      }, [wake = ZuMv(wake)](int error) mutable { wake(error); });
  }) == Zum::RevokeIssue::OK);
  ZuCheck(loadRefresh(context, revokeToken.id).state == Zum::State::Revoked);
  ZuCheck(logged(
    Zum::String{} << " event=" << int(Zum::AuditEvent::Revocation) << " ",
    Zum::String{} << " outcome=" << int(Zum::AuditOutcome::Success) << " ",
    Zum::String{} << " actor=" << "browser" << " ",
    Zum::String{} << " subject=" << Zum::auditID(revokeToken.id) << " "));
  Zum::OpaqueToken unknownToken;
  ZuCheck(Zum::opaqueIssue(rng, unknownToken));
  Zum::String unknownForm{"token="};
  unknownForm << unknownToken.token << "&client_id=browser";
  ZuCheck(ZmBlock<int>{}([
    &db, &context, &unknownForm
  ](auto wake) mutable {
    Zum::revokeRequest(db->requests, Zm::now() + ZuTime{10}, context,
      ZuMv(unknownForm), {}, Zum::RevokeConfig{
	.issuer = "issuer", .appID = 9, .now = 453
      }, [wake = ZuMv(wake)](int error) mutable { wake(error); });
  }) == Zum::RevokeIssue::OK);
  ZmSemaphore requestsDown;
  db->requests->deactivate([&requestsDown]() { requestsDown.post(); });
  requestsDown.wait();
  ZuCheck(db->stop());
  context = {};
  db->final();
  db->requests = {};
  db = {};
  store = {};
  ZuCheck(mx.stop());
  ZiLog::stop();
  ZiLog::sink(ZiLog::debugSink());
  logRows.null();
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(runtimeRevocation);
  return 0;
}
