//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmSingleton.hh>
#include <zlib/ZiTx.hh>
#include <zlib/ZfixDispatch.hh>
#include <zlib/ZfixRxMsg.hh>
#include "ZfixDemo.hh"
static ZmRef<ZiIOBuf> frameBuf() {
  using Allocator = Zi::AppBufAllocator<ZiTxBuf, ZiIOBuf_DefltSize>;
  return ZmSingleton<Allocator>::instance()->allocBuf();
}

using namespace ZuTestUtil;
using namespace ZfixDemo;
static void correlation() {
  ZuTestScope(correlation);
  Calls calls{2};
  ZuCheck(calls.add(1001) && calls.add(1002));
  ZuCheck(!calls.add(1001) && !calls.add(1003));
  ZuCheck(!calls.report({9999, 1, "A", 0}) && calls.stats().unknown == 1 && calls.count() == 2);
  ZuCheck(!calls.report({1002, 20, "F", 10}));
  ZuCheck(calls.report({1002, 19, "A", 0}) && calls.stats().firstComplete == 1002);
  ZuCheck(!calls.report({1002, 20, "F", 10}) && calls.stats().duplicate == 1);
  ZuCheck(!calls.report({1001, 17, "A", 0}) && calls.report({1001, 18, "F", 10}));
  ZuCheck(calls.stats().complete == 2);
  ZuCheck(!calls.report({1001, 21, "F", 10}) && calls.stats().invalid == 1);
  ZuCheck(calls.release(1002) && calls.add(1003));
  calls.clear(); ZuCheck(!calls.count());
}
static void rejection() {
  ZuTestScope(rejection);
  Calls calls{1}; ZuCheck(calls.add(1001));
  ZuCheck(calls.reject(1001) && calls.stats().complete == 1 && calls.stats().rejected == 1);
  ZuCheck(!calls.reject(1001) && calls.stats().duplicate == 1);
  ZuCheck(!calls.reject(9999) && calls.stats().unknown == 1);
  ZuCheck(!calls.report({1001, 1, "A", 0}) && calls.stats().invalid == 1);
}
static void replay() {
  ZuTestScope(replay);
  Results results{2};
  ZuCheck(results.admit(1001, 10, false) == Decision::New);
  auto result = *results.find(1001);
  ZuCheck(results.admit(1001, 10, true) == Decision::Cached);
  ZuCheck(results.find(1001)->ack == result.ack && results.stats().fresh == 1);
  ZuCheck(results.admit(1002, 10, true) == Decision::UnknownReplay && results.count() == 1);
  ZuCheck(results.admit(1001, 11, false) == Decision::Conflict);
  Results restored{2}; ZuCheck(restored.restore(1001, result));
  ZuCheck(restored.admit(1001, 10, true) == Decision::Cached && !restored.stats().fresh);
  ZuCheck(restored.admit(1002, 10,
    false) == Decision::New && restored.find(1002)->ack > result.fill);
  ZuCheck(restored.admit(1003, 10, false) == Decision::Refused);
  ZuCheck(!restored.restore(1003, {10, 100, 101}));
}
static void sequenceStore() {
  ZuTestScope(sequenceStore);
  Zfix::Fmt::Error error;
  auto body = Zfix::TxBody::encode<Req>(Request{1001, 10, "DEMO", {}}, 1001, error);
  ZuCheck(body && !error);
  Store store{2};
  ZuCheck(store.associate(body, {7, 2, 3}));
  uint64_t bytes = store.bytes();
  ZuCheck(bytes > body->memory() && store.key(7, 3) == 1001 && !store.key(8, 3));
  ZuCheck(store.associate(body, {7, 2, 3}) && store.bytes() == bytes);
  auto other = Zfix::TxBody::encode<Req>(Request{1002, 10, "DEMO", {}}, 1002, error);
  ZuCheck(!store.associate(other, {7, 2, 3}));
  ZuDateTime original{Zm::now()}, later{Zm::now(10)};
  ZuCheck(store.published({7, 2, 3}, original));
  ZuCheck(store.published({7, 2, 3, true}, later));
  auto history = store.fetch(7, 3).history;
  ZuCheck(history.body == body && history.originalTime == original);
  Store restored{2}; ZuCheck(restored.restore(history));
  ZuCheck(restored.fetch(7, 3).history.originalTime == original && restored.key(7, 3) == 1001);
  ZuCheck(restored.associate(other, {8, 1, 3}) && restored.key(8, 3) == 1002);
  ZuCheck(!restored.associate(other, {8, 1, 4}));
  Store tight{2, bytes - 1}; ZuCheck(!tight.associate(body, {7, 2, 3}) && !tight.bytes());
  ZuCheck(store.erase(7, 3) && !store.bytes() && !store.fetch(7, 3).history.body);
  restored.clear(); ZuCheck(!restored.count() && !restored.bytes());
}
static void archivePolicy() {
  ZuTestScope(archivePolicy);
  // Both stores belong to the application. Archive progress moves ownership;
  // it does not ask the session engine to retain or release a replay payload.
  Store live{2}, longTerm{2};
  Zfix::Fmt::Error error;
  auto body = Zfix::TxBody::encode<Tick>(Event{"DEMO", 100}, 1001, error);
  ZuDateTime original{ZuDateTimeScan::FIX{}, "20261029-12:00:00.123456789"};
  ZuCheck(body && !error && live.associate(body, {7, 2, 3}));
  if (!body || error) return;
  ZuCheck(live.published({7, 2, 3}, original));
  auto archived = [&live, &longTerm](uint64_t epoch, uint64_t begin, uint64_t end) {
    for (auto seq = begin; seq < end; ++seq) {
      auto record = live.fetch(epoch, seq).history;
      if (!record.body) continue;
      if (!longTerm.restore(record) || !live.erase(epoch, seq)) return false;
    }
    return true;
  };
  ZuCheck(archived(7, 1, 4) && !live.count() && !live.bytes());
  auto history = longTerm.fetch(7, 3).history;
  ZuCheck(history.body == body && history.originalTime == original && history.seqNo == 3);

  // Repricing and changing type/identity are independent application decisions.
  // Restore at the assigned sequence after replacing the app's own record.
  auto quote = Zfix::TxBody::encode<Tick>(Event{"DEMO", 125}, 2001, error);
  ZuCheck(quote && !error && longTerm.erase(7, 3));
  ZuCheck(longTerm.restore({quote, original, 7, 3}));
  history = longTerm.fetch(7, 3).history;
  Event value;
  ZuCheck(history.body && history.body == quote && history.body->id() == 2001 &&
    history.seqNo == 3);
  ZuCheck((history.body && Zfix::Fmt::read<Event, ZuFacet::FIX, Zfix::ArchiveField>(
    value, history.body->body(), error) && value.price == 125));
  auto replacement = Zfix::TxBody::encode<Req>(Request{3001, 10, "DEMO", {}}, 3001, error);
  ZuCheck(replacement && !error && longTerm.erase(7, 3));
  ZuCheck(longTerm.restore({replacement, original, 7, 3}));
  history = longTerm.fetch(7, 3).history;
  ZuCheck(history.body && history.seqNo == 3 && history.body->msgType() == Req::MsgType &&
    history.body->id() == 3001 && history.originalTime == original);
  ZuCheck(longTerm.erase(7, 3) && !longTerm.fetch(7, 3).history.body && !longTerm.bytes());
}
static Zfix::TxConfig replayConfig() {
  Zfix::SessionConfig session;
  session.identity.version = Zfix::Version::V4_4;
  session.identity.local = "DEMO-C"; session.identity.peer = "DEMO-S";
  return Zfix::txConfig(session);
}
static Zfix::TxContext replayContext() {
  return {.seqNo = 3, .sendingTime = ZuDateTime{Zm::now()},
    .origSendingTime = ZuDateTime{Zm::now(-10)}, .possDup = true};
}
static void wireReplay() {
  ZuTestScope(wireReplay);
  Results results{2}; ZuCheck(results.restore(1001, {10, 17, 18}));
  for (uint64_t key = 1001; key <= 1002; ++key) {
    Zfix::Fmt::Error error;
    auto body = Zfix::TxBody::encode<Req>(Request{key, 10, "DEMO", {}}, key, error);
    auto output = Zfix::txFrame(frameBuf(), *body, replayConfig(), replayContext(), error);
    ZuCheck(output && !error);
    auto parsed = Zfix::Fmt::frame(output->cspan());
    Zfix::RxMsg input; input.frame = parsed.view;
    ZuCheck(!Zfix::catalogSession<Dictionary>(input));
    unsigned seen = 0;
    error = Zfix::dispatch<Dictionary>(parsed.view,
      [&results, &seen, &input, key](auto *entry, const auto &object) -> Zfix::Fmt::Error {
	using E = ZuDecay<decltype(*entry)>;
	if constexpr (ZuIsSame<E, Req>{}) {
	  ++seen; ZuCheck(input.possDup && *input.origSendingTime && object.key == key);
	  auto decision = results.admit(object.key, object.qty, input.possDup);
	  ZuCheck(decision == (key == 1001 ? Decision::Cached : Decision::UnknownReplay));
	} else ZuCheck(false);
	return {};
      });
    ZuCheck(!error && seen == 1 && results.count() == 1 && !results.stats().fresh);
  }
  ZuCheck(results.stats().cached == 1 && results.stats().unknownReplay == 1);
  ZuCheck(results.find(1001)->ack == 17 && results.find(1001)->fill == 18);
}
static void authCodec() {
  ZuTestScope(authCodec);
  Zfix::Fmt::Error error;
  auto body = Zfix::TxBody::restoreControl<LoginMsg>(
    "98=0\1" "108=30\1" "553=demo\1" "554=example\1", 1, error);
  ZuCheck(body && !error);
  auto h = replayContext(); h.possDup = false; h.origSendingTime = ZuDateTime{};
  auto output = Zfix::txFrame(frameBuf(), *body, replayConfig(), h, error);
  ZuCheck(output && !error);
  Zfix::Fmt::MessageHandler<Login> decoded{Zfix::Fmt::frame(output->cspan()).view};
  auto login = decoded.ctor();
  ZuCheck(decoded.valid() && login.user == "demo" && login.password == "example" &&
    login.common.heartBtInt == 30);
}
int main(int argc, char **argv) {
  parse(argc, argv); ZuTestMain();
  ZuTestCall(correlation);
  ZuTestCall(rejection);
  ZuTestCall(replay);
  ZuTestCall(sequenceStore);
  ZuTestCall(archivePolicy);
  ZuTestCall(wireReplay);
  ZuTestCall(authCodec);
  return 0;
}
