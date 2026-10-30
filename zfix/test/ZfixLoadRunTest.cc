//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include "../util/load_run.hh"
#include "../util/load_cache.hh"
#ifdef ZfixFmtLib_HH
#error Workload driver must not include the native FIX codec
#endif
using namespace ZuTestUtil;
using namespace ZfixLoad;
static void cachedBytes() {
  ZuTestScope(cachedBytes);
  Options options; options.count = 1; options.warmup = 0;
  options.text = 7; options.binary = 300;
  Model model{options.shape()}; Run run{options};
  const auto &o = model.next(3, ZuDateTime{Zm::now()}, 42, ZuDecimal{12345, 2});
  ZuCheck((o.key == 3 && o.qty == 42 && o.price == ZuDecimal{12345, 2}));
  Text text{o.text}, binary{ZuCSpan{o.encoded}};
  auto check = [&model, &run, &options](ZuCSpan text, ZuBSpan binary, bool expected) {
    ZuCheck(model.shape(text, binary) == expected && run.shape(text, binary) == expected &&
      shape(text, binary, options.text, options.binary) == expected);
  };
  check(text, ZuBSpan{binary}, true);
  check({text.data(), text.length() - 1}, ZuBSpan{binary}, false);
  check(text, {binary.data(), binary.length() - 1}, false);
  auto tail = text.length() - 1; text[tail] ^= 1;
  check(text, ZuBSpan{binary}, false); text[tail] ^= 1;
  tail = binary.length() - 1; binary[tail] ^= 1;
  check(text, ZuBSpan{binary}, false); binary[tail] ^= 1;
  check(text, ZuBSpan{binary}, true);
}
static void phases() {
  ZuTestScope(phases);
  Options options; options.count = 2; options.warmup = 1; options.inFlight = 1;
  Run run{options}; uint64_t now = 100; Rows<Order> sent;
  auto send = [&sent](const Order &o) { sent.push(o); return true; };
  auto clock = [&now] { return now; };
  ZuCheck(run.start(now) && !run.start(now));
  for (unsigned i = 0; i < 3; ++i) {
    run.pump(now, send, clock); ZuCheck(sent.length() == i + 1);
    const auto &o = sent[i];
    ZuCheck(run.receive(report(o, 2 * i + 1, "0"), now + 10) == Outcome::Ack);
    run.pump(now + 10, send, clock); ZuCheck(sent.length() == i + 1);
    ZuCheck(run.receive(report(o, 2 * i + 2, "F"), now + 20) == Outcome::Complete);
    now += 20;
  }
  ZuCheck(run.phase() == Phase::Complete && !run.nextDue(now));
  Result result; run.finish(result, now);
  ZuCheck(!result.errors && result.warmup == 1 && result.progress.complete == 2 &&
    result.progress.admitted == 2 && result.progress.reports == 4 && !result.progress.inFlight);
  ZuCheck(result.ackNs.count == 2 && result.ackNs.max == 10 && result.terminalNs.max == 20
    && result.elapsedNs == 40);
  ZuCheck(result.intendedAckNs.count == 2 && result.intendedAckNs.max == result.ackNs.max &&
    result.intendedTerminalNs.max == result.terminalNs.max);
}
static void pacing() {
  ZuTestScope(pacing);
  Options options; options.count = 100; options.warmup = 0; options.inFlight = 4;
  options.rate = 2; options.duration = 1;
  Run run{options}; uint64_t now = 1000000000; Rows<Order> sent;
  auto send = [&sent](const Order &o) { sent.push(o); return true; };
  auto clock = [&now] { return now; };
  ZuCheck(run.start(now)); run.pump(now, send, clock);
  ZuCheck(sent.length() == 1 && run.nextDue(now) == 1500000000);
  now = 1499999999; run.pump(now, send, clock); ZuCheck(sent.length() == 1);
  now = 1500000010; run.pump(now, send, clock); ZuCheck(sent.length() == 2);
  for (unsigned i = 0; i < 2; ++i) {
    run.receive(report(sent[i], i * 2 + 1, "0"), now + 10);
    run.receive(report(sent[i], i * 2 + 2, "F"), now + 20);
  }
  now = 2000000000; run.pump(now, send, clock);
  ZuCheck(run.phase() == Phase::Complete && sent.length() == 2);
  Result result; run.finish(result, now);
  ZuCheck(!result.errors && result.progress.complete == 2 && result.scheduleNs.count == 2
    && result.scheduleNs.max == 10);
}
static void refusal() {
  ZuTestScope(refusal);
  Options options; options.count = 1; options.warmup = 0;
  Run run{options}; ZuCheck(run.start(100));
  run.pump(100, [](const Order &) { return false; });
  ZuCheck(run.phase() == Phase::Failed && !run.nextDue(100));
  Result result; run.finish(result, 101);
  ZuCheck(result.errors == 1 && result.progress.offered == 1 && result.progress.refused == 1 &&
    !result.progress.admitted && !result.progress.inFlight);
}
static void cache() {
  ZuTestScope(cache);
  Cache<uint64_t, uint64_t> cache{2, 1U<<20};
  ZuCheck(cache.put(1, 10) && cache.put(2, 20)); auto bytes = cache.bytes();
  ZuCheck(!cache.put(2, 30) && !cache.put(3, 30, UINT64_MAX) && cache.bytes() == bytes);
  ZuCheck(cache.put(3, 30) && !cache.get(1) && *cache.get(3) == 30 && cache.count() == 2);
  ZuCheck(cache.erase(2) && cache.bytes() == bytes / 2 && !cache.erase(2));
  Cache<uint64_t, uint64_t> tight{10, bytes / 2};
  ZuCheck(tight.put(1, 1) && tight.put(2, 2) && tight.count() == 1 && !tight.get(1));
  cache.clear(); ZuCheck(!cache.count() && !cache.bytes() && cache.peak() == bytes);
  ZuCheck(cache.put(4, 40) && cache.put(5, 50) && *cache.get(4) == 40);
  unsigned visited = 0;
  cache.each([&visited](uint64_t key, uint64_t value) {
    ZuCheck(key == 4 + visited && value == 10 * key); ++visited;
  });
  ZuCheck(visited == 2 && cache.put(6, 60) && !cache.get(4));
  ZuCheck(cache.erase(5)); visited = 0;
  cache.each([&visited](uint64_t key, uint64_t value) {
    ZuCheck(key == 6 && value == 60); ++visited;
  });
  ZuCheck(visited == 1); cache.clear();
  cache.each([&visited](uint64_t, uint64_t) { ++visited; });
  ZuCheck(visited == 1 && !cache.bytes() && cache.put(7, 70));
}
static void deferred() {
  ZuTestScope(deferred);
  Options options; options.count = 1; options.warmup = 0;
  Result result; Measurements samples;
  {
    Run run{options}; Order sent;
    ZuCheck(run.start(100));
    run.pump(100, [&sent](const Order &o) { sent = o; return true; }, [] { return uint64_t(100);
      });
    run.receive(report(sent, 1, "0"), 110); run.receive(report(sent, 2, "F"), 120);
    run.finish(result, 120, &samples);
    ZuCheck(!result.errors && !result.ackNs.count && samples.ack.count() == 1);
  }
  samples.finish(result);
  ZuCheck(result.ackNs.max == 10 && result.terminalNs.max == 20 && result.scheduleNs.count == 1);
  ZuCheck(result.intendedAckNs.max == 10 && result.intendedTerminalNs.max == 20);
}
static void identities() {
  ZuTestScope(identities);
  Options options; options.count = 1; options.warmup = 0;
  Run first{options, 0}, second{options, 1}; Order a, b;
  first.start(100); second.start(100);
  first.pump(100, [&a](const Order &o) { a = o; return true; }, [] { return uint64_t(100); });
  second.pump(100, [&b](const Order &o) { b = o; return true; }, [] { return uint64_t(100); });
  ZuCheck(a.key == 1 && b.key == (uint64_t(1)<<48) + 1 && a.key != b.key);
}
static void cli() {
  ZuTestScope(cli);
  Rows<Text> strings;
  strings.push("tool"); strings.push("--tcp"); strings.push("--sessions"); strings.push("2");
  strings.push("--ports"); strings.push("20123,20124"); strings.push("--count");
    strings.push("3");
  strings.push("--mode"); strings.push("44A");
  strings.push("--version"); strings.push("FIX.4.2");
  strings.push("--scenario"); strings.push("cancel");
  strings.push("--id"); strings.push("APPLICATION-KEY");
  strings.push("--sender"); strings.push("WIRE-C");
  strings.push("--target"); strings.push("WIRE-S");
  Rows<char *> args; for (auto &s : strings) args.push(s.data()); args.push(nullptr);
  Options options; Ports ports;
  ZuCheck(ZfixLoad::options(options, false, args.length() - 1, args.data(), ports));
  ZuCheck(options.tcp && options.count == 3 && ports.length() == 2 && ports[0] == 20123
    && ports[1] == 20124);
  ZuCheck(options.mode == Mode::FIX44A && options.version == Version::V4_2 &&
    options.scenario == Scenario::Cancel);
  ZuCheck(options.id == "APPLICATION-KEY" && options.sender == "WIRE-C" &&
    options.target == "WIRE-S");
  auto invalid = options; invalid.mode = Mode::N;
  ZuCheck(!ZfixLoad::options(invalid, false, 1, args.data(), ports));
  invalid = options; invalid.version = Version::N;
  ZuCheck(!ZfixLoad::options(invalid, false, 1, args.data(), ports));
  invalid = options; invalid.scenario = Scenario::N;
  ZuCheck(!ZfixLoad::options(invalid, false, 1, args.data(), ports));
  invalid = options; invalid.id = "";
  ZuCheck(!ZfixLoad::options(invalid, false, 1, args.data(), ports));
  options.duration = options.deadline;
  ZuCheck(!ZfixLoad::options(options, false, 1, args.data(), ports));
}
static void workflows(Scenario::T scenario) {
  ZuTestScope(workflows);
  Options options; options.scenario = scenario;
  options.count = 3; options.warmup = 1; options.inFlight = 2;
  options.text = 8; options.binary = 12;
  options.parties = options.subs = 2;
  Run run{options}; Broker broker{options, 0};
  Rows<Report> replies; unsigned next = 0;
  const Party *parties = nullptr;
  unsigned changes = 0;
  uint64_t now = 100;
  auto send = [&broker, &replies, &options, &parties, &changes]<typename O>(const O &o) {
    if constexpr (ZuIsSame<O, Order>{}) parties = o.parties.data();
    else if constexpr (ZuIsSame<O, Replace>{}) {
      // Follow-up key/price changes must keep borrowing the live model groups.
      ZuCheck(o.order.parties.data() == parties && shape(o.order, options.shape()));
      ++changes;
    }
    return broker.receive(o, false, [&replies](const Report &r) {
      replies.push(r); return true;
    });
  };
  ZuCheck(run.start(now));
  for (unsigned turns = 0; turns < 16 && run.phase() != Phase::Complete; ++turns) {
    run.pump(now, send, [&now] { return now; });
    while (next < replies.length()) {
      auto r = replies[next++];
      ZuCheck(run.receive(r, ++now, send) != Outcome::Invalid);
    }
  }
  Result result; run.finish(result, now);
  unsigned reports = scenario == Scenario::Replace ? 3 : 2;
  ZuCheck(run.phase() == Phase::Complete && !result.errors && result.warmup == 1 &&
    result.progress.offered == 3 && result.progress.admitted == 3 && result.progress.ack == 3 &&
    result.progress.complete == 3 && !result.progress.inFlight &&
    result.progress.reports == 3 * reports);
  ZuCheck(result.ackNs.count == 3 && result.terminalNs.count == 3 &&
    result.intendedAckNs.count == 3 && result.intendedTerminalNs.count == 3 &&
    result.intendedAckNs.max == result.ackNs.max &&
    result.intendedTerminalNs.max == result.terminalNs.max &&
    broker.progress.admitted == 4 && broker.progress.complete == 4 &&
    broker.progress.reports == 4 * reports);
  if (scenario == Scenario::Replace) ZuCheck(changes == 4);
  for (unsigned i = 0, n = replies.length(); i < n; ++i) {
    const auto &r = replies[i];
    ZuCheck(r.text.length() == options.text && r.encoded.length() == options.binary);
    if (r.kind != "0") ZuCheck(r.original && r.original == r.order && r.key != r.original);
  }
}
static void businessDuplicates(Scenario::T scenario) {
  ZuTestScope(businessDuplicates);
  Options options; options.scenario = scenario; options.retain = 3;
  Broker broker{options, 0}; Model model{{}};
  auto original = model.next(1, ZuDateTime{Zm::now()});
  Rows<Report> replies;
  auto emit = [&replies](const Report &r) { replies.push(r); return true; };
  ZuCheck(broker.receive(original, false, emit));
  ZuCheck(replies.length() == 1 && replies[0].kind == "0");
  auto ack = replies[0];
  ZuCheck(broker.receive(original, true, emit));
  ZuCheck(replies.length() == 2 && replies[1].exec == ack.exec);
  Order next{original}; next.key = 2;
  if (scenario == Scenario::Replace) { ++next.qty; next.price += ZuDecimal{1, 2}; }
  Cancel cancel{1, 2, "ZFIX", "1", next.time, next.qty};
  Replace replace{next, 1};
  auto change = [&broker, &emit, &cancel, &replace, scenario](bool replay) {
    return scenario == Scenario::Cancel ? broker.receive(cancel, replay, emit) :
      broker.receive(replace, replay, emit);
  };
  ZuCheck(change(false));
  auto first = replies[2];
  ZuCheck(first.order == ack.order && first.original == 1 && first.key == 2 &&
    first.exec != ack.exec && first.kind == (scenario == Scenario::Cancel ? "4" : "5"));
  ZuCheck(change(true));
  unsigned count = scenario == Scenario::Cancel ? 1 : 2;
  ZuCheck(replies.length() == 2 + 2 * count && replies[2 + count].exec == first.exec);
  ZuCheck(broker.progress.admitted == 1 && broker.progress.ack == 1 &&
    broker.progress.complete == 1 && broker.cached == 2);
  // Retention can drop the old request while its newer cached result survives.
  ZuCheck(broker.results.erase(1) && change(true));
  ZuCheck(!broker.receive(original, true, emit) && broker.replayRefused == 1);
  ZuCheck(!broker.receive(original, false, emit));
  auto missing = model.next(9, original.time);
  ZuCheck(broker.receive(missing, true, emit) && broker.progress.admitted == 2);
  broker.results.clear();
  ZuCheck(!broker.results.count() && !broker.results.bytes());
}
static void firstDeliveryReplay() {
  ZuTestScope(firstDeliveryReplay);
  Options options; options.retain = 1;
  Broker broker{options, 0}; Model model{{}};
  Rows<Report> replies;
  auto emit = [&replies](const Report &r) { replies.push(r); return true; };
  auto first = model.next(1, ZuDateTime{Zm::now()});
  // A missing wire sequence blocks later business input. A higher business ID
  // can therefore be a first delivery even when FIX marks it PossDup.
  ZuCheck(broker.receive(first, true, emit) && broker.progress.admitted == 1 &&
    broker.progress.complete == 1 && !broker.replayRefused);
  auto exec = replies[0].exec;
  ZuCheck(broker.receive(first, true, emit) && replies[2].exec == exec &&
    broker.progress.admitted == 1);
  auto second = model.next(2, first.time);
  ZuCheck(broker.receive(second, false, emit) && !broker.results.get(1));
  ZuCheck(!broker.receive(first, true, emit) && broker.replayRefused == 1 &&
    broker.progress.admitted == 2 && broker.progress.complete == 2);
  ZuCheck(!broker.receive(first, false, emit));
  // Refused result-cache admission does not consume the business key.
  Options tight; tight.cacheBytes = 1;
  Broker refused{tight, 0};
  ZuCheck(!refused.receive(first, true, emit) && !refused.progress.admitted);
  ZuCheck(!refused.receive(first, true, emit) && !refused.replayRefused);
}
static void continuedRun() {
  ZuTestScope(continuedRun);
  Options options; options.count = 1; options.warmup = 0; options.keyOffset = 4;
  Run run{options}; Model old{{}}; Order sent;
  ZuCheck(run.start(100));
  run.pump(100, [&sent](const Order &o) { sent = o; return true; },
    [] { return uint64_t(100); });
  ZuCheck(sent.key == 5);
  const auto &past = old.next(1, sent.time);
  ZuCheck(run.receive(report(past, 1, "0"), 110) == Outcome::Duplicate);
  ZuCheck(run.receive(report(past, 2, "F"), 111) == Outcome::Duplicate);
  ZuCheck(run.phase() == Phase::Measured);
  ZuCheck(run.receive(report(sent, 3, "0"), 120) == Outcome::Ack);
  ZuCheck(run.receive(report(sent, 4, "F"), 130) == Outcome::Complete);
  Result result; run.finish(result, 130);
  ZuCheck(!result.errors && result.oldReports == 2 && result.progress.complete == 1 &&
    result.progress.reports == 2 && !result.progress.unknown && !result.progress.invalid);
}
static void lifecycleOptions() {
  ZuTestScope(lifecycleOptions);
  Text name{"tool"}; char *args[] = {name.data(), nullptr};
  Options o; o.tcp = true; o.port = 12345; o.warmup = 0;
  o.count = 4; o.stateIn = "checkpoint"; o.keyOffset = 4;
  Ports ports;
  ZuCheck(ZfixLoad::options(o, false, 1, args, ports));
  o.resetLogon = true;
  ZuCheck(!ZfixLoad::options(o, false, 1, args, ports));
  o.stateIn.length(0); o.keyOffset = 0; o.connections = 2;
  ZuCheck(ZfixLoad::options(o, true, 1, args, ports));
  o.scenario = Scenario::Replace;
  ZuCheck(!ZfixLoad::options(o, true, 1, args, ports));
  o.scenario = Scenario::Orders; o.warmup = 1;
  ZuCheck(!ZfixLoad::options(o, true, 1, args, ports));
  o.warmup = 0; o.resetLogon = false; o.recoverRx = 1;
  ZuCheck(ZfixLoad::options(o, true, 1, args, ports));
  ZuCheck(!ZfixLoad::options(o, false, 1, args, ports));
  o.resetLogon = true;
  ZuCheck(!ZfixLoad::options(o, true, 1, args, ports));
}
static void intendedSamples() {
  ZuTestScope(intendedSamples);
  Options options; options.warmup = 0; options.count = options.inFlight = 2;
  options.rate = 1000000000;
  Run run{options}; Rows<Order> sent;
  ZuCheck(run.start(100));
  run.pump(200, [&sent](const Order &o) { sent.push(o); return true; },
    [] { return uint64_t(300); });
  ZuCheck(sent.length() == 2);
  if (sent.length() != 2) return;
  // Queue lateness and response latency are anticorrelated across the calls.
  // Measuring each complete interval differs from adding two percentiles.
  ZuCheck(run.receive(report(sent[1], 3, "0"), 310) == Outcome::Ack);
  ZuCheck(run.receive(report(sent[1], 4, "F"), 320) == Outcome::Complete);
  ZuCheck(run.receive(report(sent[0], 1, "0"), 500) == Outcome::Ack);
  ZuCheck(run.receive(report(sent[0], 2, "F"), 510) == Outcome::Complete);
  Result result; run.finish(result, 510);
  ZuCheck(!result.errors && result.scheduleNs.p99 == 199 && result.ackNs.p99 == 300 &&
    result.terminalNs.p99 == 310 && result.intendedAckNs.p99 == 400 &&
    result.intendedTerminalNs.p99 == 410);
  ZuCheck(result.intendedAckNs.p99 != result.ackNs.p99 + result.scheduleNs.p99 &&
    result.intendedTerminalNs.p99 != result.terminalNs.p99 + result.scheduleNs.p99);
}
int main(int argc, char **argv) {
  parse(argc, argv); ZuTestMain();
  ZuTestCall(cachedBytes);
  ZuTestCall(phases);
  ZuTestCall(pacing);
  ZuTestCall(refusal);
  ZuTestCall(cache);
  ZuTestCall(cli);
  ZuTestCall(deferred);
  ZuTestCall(identities);
  ZuTestCall(workflows, Scenario::Cancel);
  ZuTestCall(workflows, Scenario::Replace);
  ZuTestCall(businessDuplicates, Scenario::Cancel);
  ZuTestCall(businessDuplicates, Scenario::Replace);
  ZuTestCall(firstDeliveryReplay);
  ZuTestCall(continuedRun);
  ZuTestCall(lifecycleOptions);
  ZuTestCall(intendedSamples);
  return 0;
}
