//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZfixDispatch.hh>
#include "../util/zfix_load_frame.hh"
using namespace ZuTestUtil;
using namespace ZfixLoad;
static void order(unsigned parties, unsigned subs, unsigned binary, unsigned text) {
  ZuTestScope(order);
  Model model{{parties, subs, text, binary}, 42}; const auto &o = model.next(1001,
    ZuDateTime{Zm::now()});
  auto wire = frame<NewOrder>(o); auto parsed = Zfix::Fmt::frame(wire);
  ZuCheck(parsed.state == Zfix::Fmt::FrameState::Complete);
  unsigned calls = 0;
  auto error = Zfix::dispatch<Dictionary>(parsed.view,
    [&o, &calls, parties, subs](auto *entry, const auto &value, const auto &h,
      const auto &) -> Zfix::Fmt::Error {
      using E = ZuDecay<decltype(*entry)>;
      if constexpr (ZuIsSame<E, NewOrder>{}) {
	++calls;
	  ZuCheck(valid(value) && value.key == o.key && value.qty == o.qty
	  && value.price == o.price);
	ZuCheck(value.time == o.time && value.text == o.text && value.encoded == o.encoded
	  && h.seqNo == 2);
	ZuCheck(value.parties.length() == parties);
	if (parties) ZuCheck(value.parties[parties - 1].subs.length() == subs);
      } else ZuCheck(false);
      return {};
    });
  ZuCheck(!error && calls == 1);
}
static void reports() {
  ZuTestScope(reports);
  Model model{{0, 0, 20, 100}}; const auto &o = model.next(1001, ZuDateTime{Zm::now()});
  auto ack = report(o, 17, "0"), fill = report(o, 18, "F");
  auto a = frame<Ack>(ack), f = frame<Fill>(fill);
  Zfix::Fmt::MessageHandler<Report> ad{Zfix::Fmt::frame(a).view}, fd{Zfix::Fmt::frame(f).view};
  auto av = ad.ctor(), fv = fd.ctor();
  ZuCheck(ad.valid() && fd.valid() && matches(av, o) && matches(fv, o));
  ZuCheck(av.exec == 17 && fv.exec == 18 && fv.encoded == o.encoded);
}
static void changes() {
  ZuTestScope(changes);
  Model model{{0, 0, 20, 100}};
  const auto &o = model.next(1002, ZuDateTime{Zm::now()});
  Cancel cancel{1001, o.key, "ZFIX", "1", o.time, o.qty};
  Replace replace{Order{o}, 1001};
  auto c = frame<CancelOrder>(cancel), g = frame<ReplaceOrder>(replace);
  Zfix::Fmt::MessageHandler<Cancel> cd{Zfix::Fmt::frame(c).view};
  Zfix::Fmt::MessageHandler<Replace> gd{Zfix::Fmt::frame(g).view};
  auto cv = cd.ctor(); auto gv = gd.ctor();
  ZuCheck(cd.valid() && gd.valid() && cv.key == 1002 && cv.original == 1001 &&
    gv.original == 1001 && gv.order.key == 1002 && gv.order.price == o.price);
  auto canceled = report(o, 21, "4", 1001, 1001);
  auto replaced = report(o, 22, "5", 1001, 1001);
  auto filled = report(o, 23, "F", 1001, 1001);
  auto cw = frame<Canceled>(canceled), rw = frame<Replaced>(replaced), fw = frame<Fill>(filled);
  Zfix::Fmt::MessageHandler<Report> cr{Zfix::Fmt::frame(cw).view};
  Zfix::Fmt::MessageHandler<Report> rr{Zfix::Fmt::frame(rw).view};
  Zfix::Fmt::MessageHandler<Report> fr{Zfix::Fmt::frame(fw).view};
  auto a = cr.ctor(), b = rr.ctor(), d = fr.ctor();
  ZuCheck(cr.valid() && rr.valid() && fr.valid() && matches(a, o) && matches(b, o) &&
    matches(d, o) && a.original == 1001 && b.original == 1001 && d.original == 1001 &&
    a.order == 1001 && b.order == 1001 && d.order == 1001 &&
    !a.leaves && b.status == "0" && b.leaves == o.qty && d.cum == o.qty);
}
static void absentOriginal() {
  ZuTestScope(absentOriginal);
  Model model{{}}; const auto &o = model.next(1001, ZuDateTime{Zm::now()});
  Text storage;
  Zfix::Fmt::FrameWriter writer{{}, "FIX.4.4", grow, &storage};
  Zfix::Fmt::Header h;
  h.beginString = "FIX.4.4"; h.msgType = "8"; h.seqNo = 2;
  h.sender = "ZFIX-S"; h.target = "ZFIX-C"; h.sendingTime = o.time;
  ZuCheck(!Zfix::Fmt::writeHeader(writer.body(), h));
  auto field = [&writer]<typename T>(unsigned tag, const T &value) {
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
  ZuTestCall(order, 0, 0, 0, 0);
  ZuTestCall(order, 2, 0, 0, 24);
  ZuTestCall(order, 0, 0, 300, 0);
  ZuTestCall(order, 2, 2, 300, 24);
  ZuTestCall(order, 32, 8, 4096, 24);
  ZuTestCall(reports);
  ZuTestCall(changes);
  ZuTestCall(absentOriginal);
  return 0;
}
