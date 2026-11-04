//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZfixDispatch.hh>
#include <zlib/ZfixRxMsg.hh>
#include "../util/zfix_load_frame.hh"
using namespace ZuTestUtil;
using namespace ZfixLoad;
static void order(unsigned parties, unsigned subs, unsigned text) {
  ZuTestScopeRT(order);
  Model model{{parties, subs, text}, 42}; const auto &o = model.next(1001,
    ZuDateTime{Zm::now()});
  auto wire = frame<NewOrder>(o); auto parsed = Zfix::Fmt::frame(ZuCSpan{wire->cspan()});
  ZuCheckRT(parsed.state == Zfix::Fmt::FrameState::Complete);
  Zfix::RxMsg input; input.frame = parsed.view;
  ZuCheckRT(!Zfix::catalogSession<Dictionary>(input));
  unsigned calls = 0;
  auto error = Zfix::dispatch<Dictionary>(parsed.view,
    [&o, &calls, &input, parties, subs](auto *entry, const auto &value) -> Zfix::Fmt::Error {
      using E = ZuDecay<decltype(*entry)>;
      if constexpr (ZuIsSame<E, NewOrder>{}) {
	++calls;
	  ZuCheckRT(valid(value) && value.key == o.key && value.qty == o.qty
	  && value.price == o.price);
	ZuCheckRT(value.time == o.time && value.text == o.text
	  && input.seqNo == 2);
	ZuCheckRT(value.parties.length() == parties);
	if (parties) ZuCheckRT(value.parties[parties - 1].subs.length() == subs);
      } else ZuCheckRT(false);
      return {};
    });
  ZuCheckRT(!error && calls == 1);
}
static void reports() {
  ZuTestScope(reports);
  Model model{{0, 0, 20}}; const auto &o = model.next(1001, ZuDateTime{Zm::now()});
  auto ack = report(o, 17, "0"), fill = report(o, 18, "F");
  auto a = frame<Ack>(ack), f = frame<Fill>(fill);
  Zfix::Fmt::MessageHandler<Report> ad{Zfix::Fmt::frame(ZuCSpan{a->cspan()}).view},
    fd{Zfix::Fmt::frame(ZuCSpan{f->cspan()}).view};
  auto av = ad.ctor(), fv = fd.ctor();
  ZuCheck(ad.valid() && fd.valid() && matches(av, o) && matches(fv, o));
  ZuCheck(av.exec == 17 && fv.exec == 18);
}
static void changes() {
  ZuTestScope(changes);
  Model model{{0, 0, 20}};
  const auto &o = model.next(1002, ZuDateTime{Zm::now()});
  Cancel cancel{1001, o.key, "ZFIX", "1", o.time, o.qty};
  Replace replace{Order{o}, 1001};
  auto c = frame<CancelOrder>(cancel), g = frame<ReplaceOrder>(replace);
  Zfix::Fmt::MessageHandler<Cancel> cd{Zfix::Fmt::frame(ZuCSpan{c->cspan()}).view};
  Zfix::Fmt::MessageHandler<Replace> gd{Zfix::Fmt::frame(ZuCSpan{g->cspan()}).view};
  auto cv = cd.ctor(); auto gv = gd.ctor();
  ZuCheck(cd.valid() && gd.valid() && cv.key == 1002 && cv.original == 1001 &&
    gv.original == 1001 && gv.order.key == 1002 && gv.order.price == o.price);
  auto canceled = report(o, 21, "4", 1001, 1001);
  auto replaced = report(o, 22, "5", 1001, 1001);
  auto filled = report(o, 23, "F", 1001, 1001);
  auto cw = frame<Canceled>(canceled), rw = frame<Replaced>(replaced), fw = frame<Fill>(filled);
  Zfix::Fmt::MessageHandler<Report> cr{Zfix::Fmt::frame(ZuCSpan{cw->cspan()}).view};
  Zfix::Fmt::MessageHandler<Report> rr{Zfix::Fmt::frame(ZuCSpan{rw->cspan()}).view};
  Zfix::Fmt::MessageHandler<Report> fr{Zfix::Fmt::frame(ZuCSpan{fw->cspan()}).view};
  auto a = cr.ctor(), b = rr.ctor(), d = fr.ctor();
  ZuCheck(cr.valid() && rr.valid() && fr.valid() && matches(a, o) && matches(b, o) &&
    matches(d, o) && a.original == 1001 && b.original == 1001 && d.original == 1001 &&
    a.order == 1001 && b.order == 1001 && d.order == 1001 &&
    !a.leaves && b.status == "0" && b.leaves == o.qty && d.cum == o.qty);
}
static void scalarReport() {
  ZuTestScope(scalarReport);
  Model model{{}}; const auto &o = model.next(1001, ZuDateTime{Zm::now()});
  auto wire = frame<Ack>(report(o, 17, "0"));
  auto parsed = Zfix::Fmt::frame(ZuCSpan{wire->cspan()});
  Zfix::RxMsg input; input.frame = parsed.view;
  ZuCheck(parsed.state == Zfix::Fmt::FrameState::Complete &&
    !Zfix::catalogSession<Dictionary>(input));
  unsigned offset = input.sessionBytes, previous = 0, count = 0;
  bool ordered = true, original = false;
  while (offset < parsed.view.content.length()) {
    Zfix::Fmt::Token token;
    if (Zfix::Fmt::scan(parsed.view.content, offset, token)) {
      ordered = false; break;
    }
    ordered &= token.tag > previous;
    original |= token.tag == 41;
    previous = token.tag; offset = token.next; ++count;
  }
  ZuCheck(ordered && !original && count == 15);
  Zfix::Fmt::MessageHandler<Report> handler{parsed.view};
  auto value = handler.ctor();
  ZuCheck(handler.valid() && !value.original && !value.avg && !value.cum &&
    !value.lastQty && !value.lastPx && matches(value, o));
}
static void absentOriginal() {
  ZuTestScope(absentOriginal);
  Model model{{}}; const auto &o = model.next(1001, ZuDateTime{Zm::now()});
  ZmRef<ZiIOBuf> storage = new FrameAlloc{nullptr, 0};
  Zfix::Fmt::FrameWriter writer{{}, "FIX.4.4", grow, storage.ptr()};
  auto &out = writer.body();
  out.field(35); out << '8'; out.end();
  out.field(49); out << "ZFIX-S"; out.end();
  out.field(56); out << "ZFIX-C"; out.end();
  out.field(34); out << '2'; out.end();
  auto field = [&writer]<typename T>(int32_t tag, const T &value) {
    auto &body = writer.body(); body.field(tag); body << value; body.end();
  };
  field(37, 9001); field(11, o.key); field(17, 17);
  field(150, "0"); field(39, "0"); field(55, "ZFIX"); field(54, "1");
  field(38, o.qty); field(44, o.price); field(151, o.qty); field(14, 0);
  field(6, 0); field(32, 0); field(31, 0);
  auto wire = writer.finish(); ZuCheck(!wire.error && wire.data.length());
  Zfix::Fmt::MessageHandler<Report> handler{Zfix::Fmt::frame(wire.data).view};
  auto r = handler.ctor();
  ZuCheck(handler.valid() && !r.original && r.key == o.key && matches(r, o));
  Calls calls{1}; ZuCheck(calls.add(o, 100, 100));
  ZuCheck(calls.receive(r, 110) == Outcome::Ack && calls.progress().ack == 1 &&
    !calls.progress().unknown);
}
int main(int argc, char **argv) {
  parse(argc, argv); ZuTestMain();
  ZuTestCall(order, 0, 0, 0);
  ZuTestCall(order, 2, 0, 24);
  ZuTestCall(order, 0, 0, 0);
  ZuTestCall(order, 2, 2, 24);
  ZuTestCall(order, 32, 8, 24);
  ZuTestCall(reports);
  ZuTestCall(changes);
  ZuTestCall(scalarReport);
  ZuTestCall(absentOriginal);
  return 0;
}
