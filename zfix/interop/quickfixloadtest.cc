//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <quickfix/config.h>
#include <quickfix/DataDictionary.h>
#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmRef.hh>
#include <zlib/ZiFile.hh>
#include "../../zi/test/ZiTestResidue.hh"
#include "quickfixapp.hh"
#include "../util/load_run.hh"

using namespace ZuTestUtil;
using namespace QfLoad;

static void header(FIX::Message &m) {
  auto &h = m.getHeader();
  h.setField(FIX::BeginString{"FIX.4.4"}); h.setField(FIX::SenderCompID{"C"});
  h.setField(FIX::TargetCompID{"S"}); h.setField(FIX::MsgSeqNum{1});
  h.setField(FIX::SendingTime{FIX::UtcTimeStamp::now(), 9});
}
static void orders() {
  ZuTestScopeRT(orders);
  FIX::DataDictionary dictionary{QUICKFIX_DICT_PATH};
  for (auto shape : {Shape{}, Shape{2, 0, 24, 0}, Shape{2, 2, 24, 300}}) {
    Model model{shape, 17};
    const auto &o = model.next(123,
      ZuDateTime{ZuDateTimeScan::FIX{}, "20261028-12:34:56.123456789"});
    FIX::Message message; encode(message, o); header(message);
    auto bytes = message.toString(); FIX::Message parsed{bytes, dictionary, true};
    dictionary.validate(parsed);
    ZmRef<Input<Order>> input = new Input<Order>{unsigned(parsed.calculateLength())};
    decode(parsed, *input);
    ZuCheckRT(input->value.key == o.key && input->value.qty == o.qty &&
      input->value.price == o.price && input->value.time == o.time);
    ZuCheckRT(ZfixLoad::valid(input->value) && ZfixLoad::shape(input->value, shape));
    if (shape.binary) ZuCheckRT(!input->value.encoded[0] && input->value.encoded[1] == 1);
  }
}
static void reports() {
  ZuTestScopeRT(reports);
  FIX::DataDictionary dictionary{QUICKFIX_DICT_PATH};
  Model model{{0, 0, 24, 300}, 17};
  const auto &o = model.next(42,
    ZuDateTime{ZuDateTimeScan::FIX{}, "20261028-12:34:56.123456789"});
  for (ZuCSpan kind : {ZuCSpan{"0"}, ZuCSpan{"F"}, ZuCSpan{"4"}, ZuCSpan{"5"}}) {
    auto report = ZfixLoad::report(o, 71, kind, 19, kind == "4" || kind == "5" ? 37 : 0);
    FIX::Message message; encode(message, report); header(message);
    auto bytes = message.toString(); FIX::Message parsed{bytes, dictionary, true};
    dictionary.validate(parsed);
    ZmRef<Input<Report>> input = new Input<Report>{unsigned(parsed.calculateLength())};
    decode(parsed, *input); const auto &r = input->value;
    ZuCheckRT(r.key == report.key && r.order == 19 && r.exec == 71 && r.kind == kind &&
      r.original == report.original && r.price == report.price && r.time == o.time &&
      r.text == o.text && r.encoded.length() == o.encoded.length());
    ZuCheckRT(r.status == report.status && r.qty == report.qty && r.leaves == report.leaves &&
      r.cum == report.cum && r.lastQty == report.lastQty && r.lastPx == report.lastPx &&
      r.avg == report.avg);
  }
}
static void mutations() {
  ZuTestScope(mutations);
  FIX::DataDictionary dictionary{QUICKFIX_DICT_PATH};
  Model model{{2, 2, 0, 300}, 13};
  const auto &o = model.next(7, ZuDateTime{ZuDateTimeScan::FIX{}, "20261028-12:34:56.123456789"});
  Cancel cancel{3, 7, o.symbol, o.side, o.time, o.qty};
  FIX::Message message; encode(message, cancel); header(message);
  FIX::Message parsed{message.toString(), dictionary, true}; dictionary.validate(parsed);
  ZmRef<Input<Cancel>> canceled = new Input<Cancel>{unsigned(parsed.calculateLength())};
  decode(parsed, *canceled);
  ZuCheck(canceled->value.original == 3 && canceled->value.key == 7 &&
    canceled->value.qty == o.qty && canceled->value.time == o.time);
  encode(message, Replace{o, 3}); header(message);
  FIX::Message replaced{message.toString(), dictionary, true}; dictionary.validate(replaced);
  ZmRef<Input<Replace>> input = new Input<Replace>{unsigned(replaced.calculateLength())};
  decode(replaced, *input);
  ZuCheck(input->value.original == 3 && input->value.order.key == 7 &&
    input->value.order.price == o.price && ZfixLoad::shape(input->value.order, {2, 2, 0, 300}));
}
static void storage() {
  ZuTestScope(storage);
  auto now = FIX::UtcTimeStamp::now(); Store store{now, 2, 4096};
  ZuCheck(store.set(3, "three") && store.set(1, "one"));
  std::vector<std::string> values; store.get(1, 3, values);
  ZuCheck(values.size() == 2 && values[0] == "one" && values[1] == "three");
  ZuCheck(store.misses() == 1 && store.count() == 2 && store.peak() <= 4096);
  ZuCheck(store.set(4, "four") && store.count() == 2);
  values.clear(); store.get(1, UINT64_MAX - 1, values);
  ZuCheck(values.size() == 2 && values[0] == "one" && values[1] == "four");
  store.setNextSenderMsgSeqNum(17); store.incrNextSenderMsgSeqNum();
  store.setNextTargetMsgSeqNum(31); store.incrNextTargetMsgSeqNum();
  ZuCheck(store.getNextSenderMsgSeqNum() == 18 && store.getNextTargetMsgSeqNum() == 32);
  store.reset(now);
  ZuCheck(!store.count() && store.getNextSenderMsgSeqNum() == 1 &&
    store.getNextTargetMsgSeqNum() == 1);
  Store capped{now, 2, 1}; ZuCheck(!capped.set(1, "body") && !capped.count());
}
static void checkpoint() {
  ZuTestScope(checkpoint);
  auto path = ZiTestResidue::file("checkpoint.bin");
  auto now = FIX::UtcTimeStamp::now(); FIX::SessionID id{"FIX.4.4", "C", "S"};
  Model model{{2, 2, 24, 300}, 17};
  const auto &order = model.next(4,
    ZuDateTime{ZuDateTimeScan::FIX{}, "20261028-12:34:56.123456789"});
  FIX::Message message; encode(message, order); header(message);
  auto wire = message.toString();
  Store source{now, 4, 8192};
  ZuCheck(source.set(1, wire));
  source.setNextSenderMsgSeqNum(2); source.setNextTargetMsgSeqNum(8);
  ZuCheck(source.save(path, id, 4));
  Store restored{now, 4, 8192};
  ZuCheck(restored.load(path, id, 4, QUICKFIX_DICT_PATH));
  std::vector<std::string> values; restored.get(1, 1, values);
  ZuCheck(values.size() == 1 && values[0] == wire && restored.count() == 1 &&
    restored.getNextSenderMsgSeqNum() == 2 && restored.getNextTargetMsgSeqNum() == 8 &&
    restored.getCreationTime() == source.getCreationTime());
  Store wrongKey{now, 4, 8192}, wrongID{now, 4, 8192}, small{now, 4, 32};
  ZuCheck(!wrongKey.load(path, id, 5, QUICKFIX_DICT_PATH));
  ZuCheck(!wrongID.load(path, FIX::SessionID{"FIX.4.4", "OTHER", "S"}, 4,
    QUICKFIX_DICT_PATH));
  ZuCheck(!small.load(path, id, 4, QUICKFIX_DICT_PATH));
  restored.resetPolicy(false); bool refused = false;
  try { restored.reset(now); } catch (const FIX::IOException &) { refused = true; }
  ZuCheck(refused && restored.count() == 1 && restored.getNextSenderMsgSeqNum() == 2);
  restored.resetPolicy(true); restored.reset(now);
  ZuCheck(!restored.count() && restored.getNextSenderMsgSeqNum() == 1);
  restored.setNextSenderMsgSeqNum(2); refused = false;
  try { restored.reset(now); } catch (const FIX::IOException &) { refused = true; }
  ZuCheck(refused);
  { ZiFile file{path, ZiFile::Append | ZiFile::GC}; char trailing = 0;
    ZuCheck(!!file && file.write(&trailing, 1) == Zi::OK); }
  Store trailing{now, 4, 8192};
  ZuCheck(!trailing.load(path, id, 4, QUICKFIX_DICT_PATH));
  auto damaged = wire; damaged[damaged.length() - 3] ^= 1;
  ZuCheck(source.set(1, damaged) && source.save(path, id, 4));
  Store checksum{now, 4, 8192};
  ZuCheck(!checksum.load(path, id, 4, QUICKFIX_DICT_PATH));
}
int main(int argc, char **argv) {
  ZiTestResidue::init("quickfixloadtest");
  parse(argc, argv); ZuTestMain();
  ZuTestCall(orders);
  ZuTestCall(reports);
  ZuTestCall(mutations);
  ZuTestCall(storage);
  ZuTestCall(checkpoint);
}
