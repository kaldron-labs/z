//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include "../util/load.hh"
#ifdef ZfixFmtLib_HH
#error Shared workloads must not include the native FIX codec
#endif
#ifdef ZfixLib_HH
#error Shared workloads must not include the native session engine
#endif
using namespace ZuTestUtil;
using namespace ZfixLoad;
static void model() {
  ZuTestScope(model);
  Model first{{2, 3, 24}, 42}, second{{2, 3, 24}, 42}, different{{}, 43};
  ZuDateTime time{Zm::now()};
  const auto &o = first.next(1001, time);
  const auto &same = second.next(1001, time);
  ZuCheck(valid(o) && o.qty == same.qty && o.price == same.price && o.time == same.time);
  auto price = o.price;
  ZuCheck(o.price != different.next(1001, time).price);
  ZuCheck(o.parties.length() == 2 && o.parties[1].subs.length() == 3
    && o.parties[1].subs[2].type == 1);
  ZuCheck(o.text.length() == 24);
  auto text = o.text.data(); auto parties = o.parties.data();
  first.next(1002, time);
  ZuCheck(o.text.data() == text && o.parties.data() == parties
    && o.price != price);
  auto ack = report(o, 17, "0"), fill = report(o, 18, "F");
  ZuCheck(matches(ack, o) && matches(fill, o));
  fill.cum = 0; ZuCheck(!matches(fill, o));
}
static void accounting() {
  ZuTestScope(accounting);
  Model model{{}}; const auto &o = model.next(1001, ZuDateTime{Zm::now()});
  Calls calls{1}; Samples acks{1}, terms{1};
  ZuCheck(calls.add(o, 100, 100) && !calls.add(o, 100, 100));
  auto fill = report(o, 18, "F"), ack = report(o, 17, "0");
  fill.order = ack.order = 9001;
  ZuCheck(calls.receive(fill, 110, &acks, &terms) == Outcome::Terminal);
  ZuCheck(!calls.refused(o.key) && calls.count() == 1 && !calls.release(o.key));
  ZuCheck(calls.receive(fill, 111, &acks, &terms) == Outcome::Duplicate);
  auto wrong = ack; wrong.order = 9002;
  ZuCheck(calls.receive(wrong, 119) == Outcome::Invalid);
  ZuCheck(calls.receive(ack, 120, &acks, &terms) == Outcome::Complete);
  ZuCheck(calls.receive(ack, 121, &acks, &terms) == Outcome::Duplicate);
  auto stats = calls.progress();
  ZuCheck(stats.complete == 1 && stats.ack == 1 && !stats.inFlight && stats.peak == 1
    && stats.duplicate == 2);
  ZuCheck(acks.finish().p99 == 20 && terms.finish().p99 == 10);
  ZuCheck(calls.release(o.key) && !calls.count());
  ZuCheck(calls.receive(ack, 122) == Outcome::Unknown && !calls.count());
  ZuCheck(calls.add(o, 200, 200, true));
  ZuCheck(calls.receive(ack, 220, &acks, &terms) == Outcome::Ack);
  fill.exec = ack.exec; ZuCheck(calls.receive(fill, 221) == Outcome::Invalid);
  fill.exec = 18; ZuCheck(calls.receive(fill, 230, &acks, &terms) == Outcome::Complete);
  ZuCheck(acks.count() == 1 && terms.count() == 1);
}
static void refusal() {
  ZuTestScope(refusal);
  Model model{{}}; const auto &o = model.next(1, ZuDateTime{Zm::now()});
  Calls calls{1};
  ZuCheck(calls.add(o, 100, 100) && calls.refused(1) && !calls.count());
  ZuCheck(!calls.progress().admitted && !calls.progress().inFlight
    && calls.progress().refused == 1);
  ZuCheck(calls.add(o, 100, 100));
  auto ack = report(o, 1, "0");
  ZuCheck(calls.receive(ack, 99) == Outcome::Invalid && calls.progress().inFlight == 1);
  ack.qty = 0; ZuCheck(calls.receive(ack, 101) == Outcome::Invalid);
  calls.clear(); ZuCheck(!calls.count() && calls.progress().inFlight == 1);
    // unfinished stays visible
}
static void samples() {
  ZuTestScope(samples);
  Samples samples{5}, empty{0};
  ZuCheck(!empty.add(1) && !empty.finish().count);
  ZuCheck(samples.add(50) && samples.add(10) && samples.add(30) && samples.add(20)
    && samples.add(40));
  ZuCheck(!samples.add(60)); auto q = samples.finish();
  ZuCheck(q.count == 5 && q.p50 == 30 && q.p95 == 50 && q.p99 == 50 && q.p999 == 50
    && q.max == 50);
  auto now = monotonic(); ZuCheck(now && monotonic() >= now);
  Text json; ZfJSON::save(json, q); ZuCheck(json.length() && json[0] == '{');
}
static void workflowCalls(unsigned flow) {
  ZuTestScopeRT(workflowCalls);
  Model model{{}}; auto original = model.next(1, ZuDateTime{Zm::now()});
  Calls calls{2, flow}; Samples acks{1}, terms{1};
  ZuCheckRT(calls.add(original, 100, 100));
  auto ack = report(original, 10, "0", 99);
  ZuCheckRT(calls.receive(ack, 110, &acks, &terms) == Outcome::Ack);
  Order next{original}; next.key = 2;
  if (flow == Workflow::Replace) { ++next.qty; next.price += ZuDecimal{1, 2}; }
  ZuCheckRT(calls.follow(1, next) && !calls.follow(1, next));
  ZuCheckRT(calls.receive(ack, 111) == Outcome::Duplicate);
  auto terminal = report(next, 12, flow == Workflow::Cancel ? "4" : "F", 99, 1);
  auto wrong = terminal; wrong.original = 3;
  ZuCheckRT(calls.receive(wrong, 115) == Outcome::Unknown);
  wrong = terminal; wrong.order = 100;
  ZuCheckRT(calls.receive(wrong, 115) == Outcome::Invalid);
  if (flow == Workflow::Replace) {
    ZuCheckRT(calls.receive(terminal, 115) == Outcome::Invalid);
    auto changed = report(next, 11, "5", 99, 1);
    ZuCheckRT(calls.receive(changed, 120) == Outcome::Changed);
    ZuCheckRT(calls.receive(changed, 121) == Outcome::Duplicate);
    wrong = terminal; wrong.exec = 11;
    ZuCheckRT(calls.receive(wrong, 125) == Outcome::Invalid);
  }
  ZuCheckRT(calls.receive(terminal, 130, &acks, &terms) == Outcome::Complete);
  ZuCheckRT(calls.receive(terminal, 131) == Outcome::Duplicate && calls.release(1));
  const auto &p = calls.progress();
  ZuCheckRT(p.offered == 1 && p.admitted == 1 && p.ack == 1 && p.complete == 1 && !p.inFlight);
  ZuCheckRT(acks.finish().max == 10 && terms.finish().max == 30);
}
int main(int argc, char **argv) {
  parse(argc, argv); ZuTestMain();
  ZuTestCall(model);
  ZuTestCall(accounting);
  ZuTestCall(refusal);
  ZuTestCall(samples);
  ZuTestCall(workflowCalls, Workflow::Cancel);
  ZuTestCall(workflowCalls, Workflow::Replace);
  return 0;
}
