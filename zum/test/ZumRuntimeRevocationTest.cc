//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmBlock.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZfCf.hh>
#include "../../zdb/test/ZdbTestCf.hh"
#include <zlib/ZiIOBuf.hh>
#include <zlib/ZiRxStream.hh>
#include <zlib/ZiLog.hh>
#include <zlib/ZvMxParams.hh>
#include <zlib/ZhttpNative.hh>
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
  test->requests->activate([test](bool) { test->active.post(); });
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


// The durable row and real DB callbacks keep the delivery chain meaningful;
// only the remote HTTP peer is held by this fixture.
static void transmitterLifecycle(TestDB *db, Ztls::Random &rng)
{
  ZuTestScopeRT(transmitterLifecycle);
  auto scheduler = db->requests->scheduler();
  auto sid = db->requests->sid();
  Zum::Bytes key{ZuBSpan{"0123456789abcdef0123456789abcdef"}};
  Zum::Bytes auth;
  ZuCheckRT(Zum::serverSecretEncrypt(rng, key, "issuer", "zum.ssf_rx",
    "receiver-held", "callbackAuth", ZuBSpan{"Bearer callback"}, auth));
  int64_t now = Zm::now().sec();
  ZuCheckRT(insertRecord(db->ssfRx, Zum::SSFRx{
    .expires = now + 300, .appID = 9, .receiverID = "receiver-held",
    .audience = "ping", .deliveryURL = "https://receiver/ssf",
    .callbackAuth = ZuMv(auth), .revision = 17, .updated = now}));
  ZuCheckRT(insertRecord(db->ssfDeliveries, Zum::SSFDelivery{
    .eventID = "event-held", .receiverID = "receiver-held",
    .familyIssuer = "issuer/oauth2/9", .familyID = "family-held",
    .familyExpires = now + 300, .set = Zum::Bytes{ZuBSpan{"held-set"}},
    .nextDelivery = now, .receiverRevision = 17}));

  Zum::OIDCHTTPDoneFn pending;
  ZmSemaphore submitted, stopped, barrier;
  ZmAtomic<unsigned> completions = 0;
  Zum::SSFTransmitterConfig config{
    .db = db, .requests = db->requests.ptr(), .issuer = "issuer",
    .secretIssuer = "issuer", .dbKey = key, .rng = &rng,
    .key = Zum::SignKey{.id = "key", .issuer = "issuer"},
    .sign = Zum::SignFn{[](const Zum::SignKey &, ZuBSpan,
	Zum::SignatureFn complete) { complete(Zum::Bytes{}); }},
    .http = Zum::OIDCHTTPFn{[&pending, &submitted](
	Zum::OIDCHTTPRequest request, Zum::OIDCHTTPDoneFn complete) {
      ZuCheckRT(request.authorization == "Bearer callback" && request.body == "held-set");
      pending = ZuMv(complete);
      submitted.post();
    }}};
  ZmRef<Zum::SSFTransmitter> transmitter = new Zum::SSFTransmitter{};
  ZuCheckRT(transmitter->init(config));
  transmitter->final(); // no loop or DB query was submitted by init
  ZuCheckRT(transmitter->init(ZuMv(config)));
  bool started = transmitter->start();
  ZuCheckRT(started);
  bool held = started && !submitted.timedwait(Zm::now(10));
  ZuCheckRT(held && pending);
  transmitter->stop([&completions, &stopped](bool ok) {
    ZuCheckRT(ok);
    ++completions;
    stopped.post();
  });
  transmitter->stop([&completions, &stopped](bool ok) {
    ZuCheckRT(ok);
    ++completions;
    stopped.post();
  });
  scheduler->run([&barrier]() { barrier.post(); }, sid);
  barrier.wait();
  if (pending) {
    ZuCheckRT(!completions.load_());
    // Keep the fired completion object: its invocation, rather than its
    // destruction by the HTTP adapter, must release the active work credit.
    pending(202, Zum::String{});
  }
  stopped.wait();
  stopped.wait();
  ZuCheckRT(completions == 2 && transmitter->state() == ZmEngineState::Stopped);
  bool retired = ZmBlock<bool>{}([db](auto wake) mutable {
    auto table = db->ssfDeliveries;
    table->run(0, [table, wake = ZuMv(wake)]() mutable {
      table->find<0>(0, ZuFwdTuple(Zum::String{"event-held"},
	  Zum::String{"receiver-held"}),
	[wake = ZuMv(wake)](ZdbRowRef<Zum::SSFDelivery> row) mutable {
	  wake(!row);
	});
    });
  });
  ZuCheckRT(retired);
  if (retired) {
    ZuCheckRT(transmitter->start());
    ZuCheckRT(transmitter->start());
    ZuCheckRT(transmitter->stop());
    ZuCheckRT(transmitter->stop());
  }
  transmitter->final();
  transmitter = nullptr;
  pending = {}; // a consumed external callback no longer accesses its owner
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
      logRows.push(Zum::String{" "} << ZuCSpan{buf} << " ");
  }));
  ZiLog::start();
  auto config = dbConfig();
  auto configFields = ZfCf::handler<zdbtest::ConfigFields>(config).ctor();
  ZiMultiplex mx{ZvMxParams("mx", configFields.mx.p<const ZfCf::AnyNode *>())
    .tcpBufAllocator<Zi::RxTLSBufAllocator<ZiRxQueue::Node, 8<<10, 100<<20,
      ZuStringT<"Zhttp.TLS.Rx">>>()};
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
    Zum::revokeRequest({}, db->requests, Zm::now() + ZuTime{10}, context,
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
    Zum::revokeRequest({}, db->requests, Zm::now() + ZuTime{10}, context,
      ZuMv(unknownForm), {}, Zum::RevokeConfig{
	.issuer = "issuer", .appID = 9, .now = 453
      }, [wake = ZuMv(wake)](int error) mutable { wake(error); });
  }) == Zum::RevokeIssue::OK);
  ZuTestCall(transmitterLifecycle, db.ptr(), rng);
  ZmSemaphore requestsDown;
  db->requests->deactivate([&requestsDown](bool) { requestsDown.post(); });
  requestsDown.wait();
  ZuCheck(db->stop());
  context = {};
  ZuCheck(mx.stop());
  db->final();
  db->requests = {};
  db = {};
  store = {};
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
