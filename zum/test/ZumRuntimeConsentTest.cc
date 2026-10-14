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
#include <zlib/zumd_db.hh>
#include <zlib/zumd_identity_db.hh>
#include <zlib/zumd_app_db.hh>
#include <zlib/zumd_request.hh>
#include <zlib/zumd_saga_consent_code.hh>
#include <zlib/zumd_saga_image.hh>

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


static void runtimeConsent()
{
  ZuTestScope(runtimeConsent);
  ZiLog::init("ZumRuntimeConsentTest");
  ZiLog::level(0);
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
  // Explicit consent and code issuance are one native operation.
  ZuCheck(insertRecord(context->apps, Zum::App{
    .id = 40000, .name = "consent-saga", .state = Zum::State::Active,
    .authVersion = 1, .audience = "https://consent-saga.example"}));
  ZuCheck(insertRecord(context->users, Zum::User{
    .id = 40000, .name = "consent-saga-user",
    .handle = Zum::Bytes{ZuBSpan{"consent-saga-user"}}, .state = Zum::State::Active}));
  Zum::Client consentClient;
  consentClient.id = "consent-saga-client";
  consentClient.appID = 40000;
  consentClient.state = Zum::State::Active;
  ZuCheck(insertRecord(context->clients, consentClient));
  Zum::ConsentCodeArgs consentChange;
  auto &ceremony = consentChange.beforeGrant;
  ceremony.id = Zum::Bytes{ZuBSpan{"consent-code-001"}};
  ceremony.issuer = "issuer";
  ceremony.appID = 40000;
  ceremony.userID = 40000;
  ceremony.clientID = consentClient.id;
  ceremony.requestedRoleIDs = {7};
  ceremony.bindingDigest = Zum::Bytes{ZuBSpan{"consent binding"}};
  ceremony.authVersion = 1;
  ceremony.userVersion = 1;
  ceremony.authTime = 100;
  ceremony.created = 100;
  ceremony.expires = 200;
  ceremony.state = Zum::State::Pending;
  consentChange.afterGrant = ceremony;
  consentChange.afterGrant.kind = Zum::GrantKind::Code;
  consentChange.afterGrant.state = Zum::State::Active;
  consentChange.afterGrant.digest = Zum::Bytes{ZuBSpan{"consent code digest"}};
  consentChange.afterGrant.bindingDigest.null();
  consentChange.beforeConsent.version = 0;
  consentChange.roleIDs = {7};
  consentChange.now = 101;
  auto readConsent = [context]() {
    return ZmBlock<Zum::Consent>{}([context](auto wake) mutable {
      context->consents->run(0, [context, wake = ZuMv(wake)]() mutable {
	context->consents->find<0>(0, ZuFwdTuple(Zum::UserID{40000},
	  Zum::String{"consent-saga-client"}, Zum::AppID{40000}),
	  [wake = ZuMv(wake)](ZdbRowRef<Zum::Consent> row) mutable {
	    wake(row ? Zum::Consent{row->data()} : Zum::Consent{});
	  });
      });
    });
  };
  ZuCheck(insertRecord(context->grants, ceremony));
  ZuCheck(runSaga(db, Zum::consentCode(consentChange), ZdbSagaID{12019}));
  auto consent = readConsent();
  ZuCheck(consent.userID == 40000 && !consent.owner && consent.version == 1 &&
    consent.roleIDs == Zum::IDVec{7} && consent.created == 101 && consent.updated == 101);
  auto consentCode = loadGrant(context, ceremony.id);
  ZuCheck(consentCode.kind == Zum::GrantKind::Code && !consentCode.owner &&
    consentCode.digest == consentChange.afterGrant.digest && !consentCode.bindingDigest);

  ceremony.id = Zum::Bytes{ZuBSpan{"consent-code-002"}};
  consentChange.afterGrant.id = ceremony.id;
  consentChange.beforeConsent = consent;
  consentChange.beforeConsent.version = 2; // stale, row is still version one
  consentChange.roleIDs.push(8);
  consentChange.now = 102;
  ZuCheck(insertRecord(context->grants, ceremony));
  ZuCheck(!runSaga(db, Zum::consentCode(consentChange), ZdbSagaID{12020}));
  ZuCheck(Zum::SagaImage::save(loadGrant(context, ceremony.id)) ==
    Zum::SagaImage::save(ceremony));
  ZuCheck(Zum::SagaImage::save(readConsent()) == Zum::SagaImage::save(consent));

  consentChange.beforeConsent = consent;
  ZuCheck(runSaga(db, Zum::consentCode(consentChange), ZdbSagaID{12021}));
  consent = readConsent();
  ZuCheck((!consent.owner && consent.version == 2 && consent.roleIDs == Zum::IDVec{7, 8} &&
    consent.created == 101 && consent.updated == 102));
  consentCode = loadGrant(context, ceremony.id);
  ZuCheck(!consentCode.owner && consentCode.kind == Zum::GrantKind::Code);

  ceremony.id = Zum::Bytes{ZuBSpan{"consent-code-003"}};
  consentChange.afterGrant.id = ceremony.id;
  consentChange.beforeConsent = {};
  consentChange.beforeConsent.version = 0; // race: consent was inserted meanwhile
  ZuCheck(insertRecord(context->grants, ceremony));
  ZuCheck(!runSaga(db, Zum::consentCode(consentChange), ZdbSagaID{12022}));
  ZuCheck(Zum::SagaImage::save(loadGrant(context, ceremony.id)) ==
    Zum::SagaImage::save(ceremony));
  ZuCheck(Zum::SagaImage::save(readConsent()) == Zum::SagaImage::save(consent));

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
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(runtimeConsent);
  return 0;
}
