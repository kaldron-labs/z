//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmAtomic.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmPolymorph.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZfCf.hh>
#include "../../zdb/test/ZdbTestCf.hh"
#include <zlib/ZiIOBuf.hh>
#include <zlib/ZiRxStream.hh>
#include <zlib/ZiLog.hh>
#include <zlib/ZvMxParams.hh>
#include <zlib/ZhttpNative.hh>
#include <zlib/ZdbMemStore.hh>
#include <zlib/zumd_db.hh>
#include <zlib/zumd_request.hh>
#include <zlib/zumd_token.hh>

#include "ZumTestSupport.hh"

using namespace ZuTestUtil;
using namespace zumtest;
using TestDB = RuntimeDB;

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

struct SigningCredit : ZmNHeap<SigningCredit>, ZmPolymorph {
friend inline ZuStringT<"ZumRuntimeTokenTest.Credit"> ZmNHeapID(SigningCredit *);
  ZmAtomic<unsigned> *released;
  ZmSemaphore *complete;

  SigningCredit(ZmAtomic<unsigned> *released_, ZmSemaphore *complete_) :
    released{released_}, complete{complete_} { }
  ~SigningCredit() {
    *released = 1;
    complete->post();
  }
};

static void signingCredit(TestDB *db, Ztls::Random &rng)
{
  ZuTestScopeRT(signingCredit);
  auto requests = db->requests;
  auto scheduler = requests->scheduler();
  auto sid = requests->sid();
  ZmSemaphore submitted, completed, retired, barrier;
  ZmAtomic<unsigned> released = 0;
  bool holding = true;
  int result = Zum::TokenIssue::OK;
  unsigned completedCalls = 0;
  Zum::SignatureFn pending;
  ZmRef<SigningCredit> work = new SigningCredit{&released, &retired};
  bool queued = Zum::tokenRequest(ZuMv(work), requests,
    Zm::now() + ZuTime{10}, db, rng,
    "grant_type=client_credentials", "Basic d29ya2xvYWQ6c2VjcmV0",
    Zum::TokenConfig{.issuer = "issuer/oauth2/9", .appID = 9,
      .maxKeys = 64, .now = 340, .accessExpires = 400,
      .refreshExpires = 1000, .generationLimit = 8, .spentLimit = 8},
    [requests, &holding, &pending, &submitted](
        const Zum::SignKey &, ZuBSpan, Zum::SignatureFn complete) mutable {
      ZuCheckRT(requests->scheduler()->invoked(requests->sid()));
      if (!holding) { complete(Zum::Bytes{}); return; }
      pending = ZuMv(complete);
      submitted.post();
    }, [&result, &completedCalls, &completed](int error, Zum::TokenResponse response) {
      ++completedCalls;
      result = error;
      Zum::tokenClear(response);
      completed.post();
    });
  ZuCheckRT(queued);
  bool held = queued && !submitted.timedwait(Zm::now(10));
  ZuCheckRT(held);
  // This fixture's DB callbacks and Requests owner share shard 5. Release late
  // sign attempts normally if an earlier setup check failed.
  scheduler->run([&holding, &barrier]() {
    holding = false;
    barrier.post();
  }, sid);
  barrier.wait();
  requests->deactivate([&barrier](bool) { barrier.post(); });
  barrier.wait();
  if (queued) {
    ZuCheckRT(!completed.timedwait(Zm::now(10)));
    ZuCheckRT(result == Zum::OAuthError::TemporarilyUnavailable);
  }
  // Application cancellation has returned, but the signing helper still
  // borrows caller resources and must retain the caller's graph credit.
  if (held) ZuCheckRT(!released.load_());
  if (pending) pending(Zum::Bytes{});
  bool drained = !retired.timedwait(Zm::now(10));
  ZuCheckRT(drained);
  if (!drained) {
    pending = {}; // release a retained fired callback on the regression path
    retired.wait();
  }
  ZuCheckRT(released.load_());
  // Retaining or accidentally repeating a fired signing completion cannot
  // retain or dereference the helper, nor deliver another application result.
  if (pending) pending(Zum::Bytes{});
  ZuCheckRT(completedCalls == unsigned(queued));
  pending = {};
  ZuCheckRT(ZmBlock<bool>{}([requests](auto wake) {
    requests->activate(ZuMv(wake));
  }));
}

static void runtimeToken()
{
  ZuTestScope(runtimeToken);
  ZiLog::init("ZumRuntimeTokenTest");
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
    };
  ZmRef<ZdbMem::Store> store = new ZdbMem::Store{};
  ZmRef<TestDB> db = new TestDB{};
  db->requests = new Zum::Requests{};
  ZuCheck(db->requests->init(&mx, 5, 64));
  db->init(ZdbCf{configFields.zdb.p<const ZfCf::AnyNode *>()}, &mx,
    ZdbHandler{.upFn = dbUp, .downFn = dbDown}, store);
  Zum::registerSchema(db);
  ZmRef<Zum::DB> context = db;
  ZuCheck(mx.start());
  ZuCheck(db->start());
  db->active.wait();
  Ztls::Random rng;
  ZuCheck(rng.init());
  Zum::Bytes passkeyHandle;
  ZmRef<Ztls::PK::SK_EC> passkey;
  ZuTestCall(prepareIdentity, db, context, rng, passkeyHandle, passkey);
  OIDCSetup state;
  ZuTestCall(prepareOIDC, db, context, rng, state);
  Ztls::PK::SK_EC tokenKey{rng, Ztls::PK::OIDs::EC_GRP_SECP256R1};
  uint8_t tokenPublic[Ztls::COSE::ES256::PublicKeySize];
  TokenSetup token;
  ZuTestCall(prepareToken, db, context, rng, state, passkeyHandle,
    *passkey, tokenKey, tokenPublic, token);
  ZuCheck(token.browserCode);
  ZuTestCall(signingCredit, db, rng);
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
  ZuTestCall(runtimeToken);
  return 0;
}
