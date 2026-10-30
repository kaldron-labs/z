//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdio.h>
#include <string.h>
#include <zlib/ZfixDispatch.hh>
#include "../util/zfix_load_frame.hh"
using namespace ZfixLoad;
static bool write(ZuCSpan directory, ZuCSpan name, const Text &wire) {
  Text path; path << directory << '/' << name << ".fix";
  auto file = fopen(path, "wb"); if (!file) return false;
  bool ok = fwrite(wire.data(), 1, wire.length(), file) == wire.length();
  return !fclose(file) && ok;
}
static int dump(const char *directory) {
  Model scalar{{}}, heavy{{2, 2, 24, 300}}, binary{{0, 0, 0, 300}}, text{{2, 0, 24, 0}},
    heavyGroup{{32, 8, 24, 4096}};
  auto time = ZuDateTime{Zm::now()}; const auto &o = scalar.next(1001, time),
    &g = heavy.next(1002, time);
  auto ack = report(g, 17, "0"), fill = report(g, 18, "F");
  Cancel cancel{1001, 1003, "ZFIX", "1", time, o.qty};
  Replace replace{Order{o}, 1001}; replace.order.key = 1004;
  return !(write(directory, "order", frame<NewOrder>(o)) &&
    write(directory, "groups", frame<NewOrder>(g)) &&
    write(directory, "binary", frame<NewOrder>(binary.next(1005, time))) &&
    write(directory, "text", frame<NewOrder>(text.next(1006, time))) &&
    write(directory, "heavy", frame<NewOrder>(heavyGroup.next(1007, time))) && write(directory,
      "ack", frame<Ack>(ack)) &&
    write(directory, "fill", frame<Fill>(fill)) && write(directory, "cancel",
      frame<CancelOrder>(cancel)) &&
    write(directory, "replace", frame<ReplaceOrder>(replace)));
}
static Text read(ZuCSpan directory, ZuCSpan name, unsigned variant) {
  Text path; path << directory << '/' << name << ".fix";
  switch (variant) { case 1: path << ".peer"; break; case 2: path << ".unknown"; break; }
  auto file = fopen(path, "rb"); if (!file) return {};
  if (fseek(file, 0, SEEK_END)) { fclose(file); return {}; }
  auto length = ftell(file);
  if (length <= 0 || uint64_t(length) > UINT_MAX || fseek(file, 0, SEEK_SET)) { fclose(file);
    return {}; }
  Text bytes{uint64_t(length), uint64_t(length) + 1};
  bool ok = fread(bytes.data(), 1, bytes.length(), file) == bytes.length();
  fclose(file); return ok ? ZuMv(bytes) : Text{};
}
static bool sameOrder(const Order &a, const Order &b) {
  if (!valid(a) || !valid(b) || a.key != b.key || a.qty != b.qty || a.price != b.price
    || a.time != b.time ||
      a.text != b.text || a.encoded != b.encoded
	|| a.parties.length() != b.parties.length()) return false;
  for (unsigned i = 0, n = a.parties.length(); i < n; ++i) {
    const auto &x = a.parties[i], &y = b.parties[i];
    if (x.id != y.id || x.source != y.source || x.role != y.role
      || x.subs.length() != y.subs.length()) return false;
    for (unsigned j = 0, n = x.subs.length(); j < n; ++j) {
      const auto &a = x.subs[j], &b = y.subs[j];
      if (a.id != b.id || a.type != b.type) return false;
    }
  }
  return true;
}
static bool sameReport(const Report &a, const Report &b) {
  return a.key == b.key && a.order == b.order && a.exec == b.exec &&
    a.original == b.original && a.kind == b.kind && a.status == b.status &&
    a.symbol == b.symbol && a.side == b.side && a.qty == b.qty && a.price == b.price
      && a.leaves == b.leaves &&
    a.cum == b.cum && a.avg == b.avg && a.lastQty == b.lastQty && a.lastPx == b.lastPx
      && a.time == b.time &&
    a.text == b.text && a.encoded == b.encoded;
}
template <typename Entry, typename Fn> static bool check(ZuCSpan directory, ZuCSpan name, Fn fn,
  unsigned variant = 1) {
  auto native = read(directory, name, 0), peer = read(directory, name, variant);
  auto a = Zfix::Fmt::frame(native), b = Zfix::Fmt::frame(peer);
  if (a.state != Zfix::Fmt::FrameState::Complete
    || b.state != Zfix::Fmt::FrameState::Complete) return false;
  using O = typename Entry::Object;
  Zfix::Fmt::MessageHandler<O> ah{a.view}, bh{b.view};
  auto av = ah.ctor(), bv = bh.ctor();
  return ah.valid() && bh.valid() && ah.header().seqNo == bh.header().seqNo &&
    ah.header().sendingTime == bh.header().sendingTime && fn(av, bv);
}
static int checkFrames(const char *directory) {
  bool ok = check<NewOrder>(directory, "order", sameOrder, 2) &&
    check<NewOrder>(directory, "order", sameOrder) && check<NewOrder>(directory, "groups",
      sameOrder) &&
    check<NewOrder>(directory, "binary", sameOrder) && check<NewOrder>(directory, "text",
      sameOrder) &&
    check<NewOrder>(directory, "heavy", sameOrder) &&
    check<Ack>(directory, "ack", sameReport) && check<Fill>(directory, "fill", sameReport) &&
    check<CancelOrder>(directory, "cancel", [](const auto &a, const auto &b) {
      return a.original == b.original && a.key == b.key && a.symbol == b.symbol
	&& a.side == b.side &&
	a.time == b.time && a.qty == b.qty;
    }) && check<ReplaceOrder>(directory, "replace", [](const auto &a, const auto &b) {
      return a.original == b.original && sameOrder(a.order, b.order);
    });
  if (!ok) fputs("independent peer frame mismatch\n", stderr);
  else puts("independent peer frames: scalar/group/binary D, ack/fill 8, F/G values preserved");
  return ok ? 0 : 1;
}
int main(int argc, char **argv) {
  if (argc == 3 && !strcmp(argv[1], "--frames")) return dump(argv[2]);
  if (argc == 3 && !strcmp(argv[1], "--check-frames")) return checkFrames(argv[2]);
  fputs("usage: nativecodec --frames|--check-frames DIRECTORY\n", stderr);
  return 2;
}
