//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdio.h>
#include <iostream>
#include <string.h>
#include <zlib/ZfixDispatch.hh>
#include "../util/zfix_load_frame.hh"
using namespace ZfixLoad;
// Ordinary metadata covers fields that QuickFIX classifies as header/body/
// trailer internally. The native object has one flattened message schema.
struct XMLMessage {
  ZuCSpan	sender{"ZFIX-C"}, target{"ZFIX-S"};
  uint64_t	seqNo = 2;
  ZuDateTime	sendingTime{ZuDateTimeScan::FIX{}, "20261028-12:34:56"};
  ZuCSpan	xml;
};
ZfStruct(, (XMLMessage, FIX),
  (sender, (FIX::ID<49>, Required), String), (target, (FIX::ID<56>, Required), String),
  (seqNo, (FIX::ID<34>, Required), UInt64), (sendingTime, (FIX::ID<52>), DateTime),
  (xml, (FIX::ID<212>, FIX::Binary<213>, Required), String));
using XMLMsg = Zfix::MT<XMLMessage, "n">;
struct XMLBytes {
  ZuCSpan	sender{"ZFIX-C"}, target{"ZFIX-S"};
  uint64_t	seqNo = 2;
  ZuDateTime	sendingTime{ZuDateTimeScan::FIX{}, "20261028-12:34:56"};
  ZuBSpan	xml;
};
ZfStruct(, (XMLBytes, FIX),
  (sender, (FIX::ID<49>, Required), String), (target, (FIX::ID<56>, Required), String),
  (seqNo, (FIX::ID<34>, Required), UInt64), (sendingTime, (FIX::ID<52>), DateTime),
  (xml, (FIX::ID<212>, FIX::Binary<213>, Required), Bytes));
using XMLBytesMsg = Zfix::MT<XMLBytes, "n">;
struct AuthMessage {
  ZuCSpan	sender{"ZFIX-C"}, target{"ZFIX-S"};
  uint64_t	seqNo = 2;
  ZuDateTime	sendingTime{ZuDateTimeScan::FIX{}, "20261028-12:34:56"};
  int32_t	encryptMethod = 0;
  uint32_t	heartBtInt = 30;
  ZuBSpan	raw;
};
ZfStruct(, (AuthMessage, FIX),
  (sender, (FIX::ID<49>, Required), String), (target, (FIX::ID<56>, Required), String),
  (seqNo, (FIX::ID<34>, Required), UInt64), (sendingTime, (FIX::ID<52>), DateTime),
  (encryptMethod, (FIX::ID<98>, Required), Int32), (heartBtInt, (FIX::ID<108>, Required), UInt32),
  (raw, (FIX::ID<95>, FIX::Binary<96>, Required), Bytes));
using AuthMsg = Zfix::MT<AuthMessage, "A">;
struct SignedOrder {
  Order order;
  ZuBSpan	signature;
};
ZfStruct(, (SignedOrder, FIX), (order, , UDT),
  (signature, ((FIX::Sig<93, 89>), Required), Bytes));
using SignedMsg = Zfix::MT<SignedOrder, "D">;
static constexpr auto XMLPayload = "RTRF<8=FIX.4.4\1" "9=5\1" "35=D\1" "10=999\1\0=\xff>"_z;
static constexpr auto AuthPayload = "user\0credential\1" "35=A\1" "10=999\1=\xff"_z;
static constexpr auto SigPayload = "S\0\1=\xff"_z;

template <typename Entry> static ZmRef<ZiIOBuf> binaryFrame(const typename Entry::Object &object)
{
  ZmRef<ZiIOBuf> storage = new FrameAlloc{nullptr, 0};
  Zfix::Fmt::FrameWriter writer{{}, "FIX.4.4", grow, storage.ptr()};
  writer.body().field(35); writer.body() << Entry::MsgType; writer.body().end();
  if (!Zfix::Fmt::save<typename Entry::Mapping>(writer.body(), object)) return {};
  auto out = writer.finish();
  return frameOutput(ZuMv(storage), out);
}
static ZmRef<ZiIOBuf> signatureFrame(const ZmRef<ZiIOBuf> &original)
{
  if (!original) return {};
  auto parsed = Zfix::Fmt::frame(ZuCSpan{original->cspan()});
  if (parsed.state != Zfix::Fmt::FrameState::Complete) return {};
  auto first = parsed.view.msgType.data() - 3; // fixed 35= prefix
  auto end = parsed.view.content.data() + parsed.view.content.length();
  ZmRef<ZiIOBuf> storage = new FrameAlloc{nullptr, 0};
  Zfix::Fmt::FrameWriter writer{{}, "FIX.4.4", grow, storage.ptr()};
  writer.body() << ZuCSpan{first, uint64_t(end - first)};
  Zfix::Fmt::saveBinary<ZfField(SignedOrder, signature)>(writer.body(), SigPayload);
  auto out = writer.finish();
  return frameOutput(ZuMv(storage), out);
}

static bool write(
  ZuCSpan directory, ZuCSpan name, const ZmRef<ZiIOBuf> &wire)
{
  if (!wire || !wire->length) return false;
  Text path; path << directory << '/' << name << ".fix";
  auto file = fopen(path, "wb"); if (!file) return false;
  bool ok = fwrite(wire->data(), 1, wire->length, file) == wire->length;
  return !fclose(file) && ok;
}
static int dump(const char *directory) {
  Model scalar{{}}, heavy{{2, 2, 24}}, text{{2, 0, 24}},
    heavyGroup{{32, 8, 24}};
  auto time = ZuDateTime{Zm::now()}; const auto &o = scalar.next(1001, time),
    &g = heavy.next(1002, time);
  auto ack = report(g, 17, "0"), fill = report(g, 18, "F");
  Cancel cancel{1001, 1003, "ZFIX", "1", time, o.qty};
  Replace replace{Order{o}, 1001}; replace.order.key = 1004;
  auto orderWire = frame<NewOrder>(o);
  XMLMessage xml; xml.xml = XMLPayload;
  XMLBytes xmlBytes; xmlBytes.xml = ZuBSpan{XMLPayload};
  AuthMessage auth; auth.raw = ZuBSpan{AuthPayload};
  return !(write(directory, "xml", binaryFrame<XMLMsg>(xml)) &&
    write(directory, "xmlbytes", binaryFrame<XMLBytesMsg>(xmlBytes)) &&
    write(directory, "auth", binaryFrame<AuthMsg>(auth)) &&
    write(directory, "signed", signatureFrame(orderWire)) &&
    write(directory, "order", orderWire) &&
    write(directory, "groups", frame<NewOrder>(g)) &&
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
      a.text != b.text
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
    a.text == b.text;
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
  if (!ah.valid() || !bh.valid() || !fn(av, bv)) {
    std::cerr << name << ": decoded values differ\n"; return false;
  }
  bool borrowed = true;
  using Schema = Zfix::Fmt::Schema<O, typename Entry::Mapping>;
  ZuUnroll::all<typename Schema::Leaves>([&]<typename L>() {
    using F = L::Field;
    if constexpr (Zfix::Fmt::Binary<F>{}) {
      Zfix::Fmt::FieldPos ap, bp;
      if (Zfix::Fmt::inspect<O, typename Entry::Mapping>(a.view.content,
	  Zfix::Fmt::payloadTag<F>(), ap) ||
	  Zfix::Fmt::inspect<O, typename Entry::Mapping>(b.view.content,
	  Zfix::Fmt::payloadTag<F>(), bp) || ap.offset == UINT_MAX || bp.offset == UINT_MAX) {
	borrowed = false; return;
      }
      auto x = ZuCSpan{F::get(Zfix::Fmt::ReadPath<typename L::Path>::get(av))};
      auto y = ZuCSpan{F::get(Zfix::Fmt::ReadPath<typename L::Path>::get(bv))};
      borrowed &= x.data() == a.view.content.data() + ap.offset && x.length() == ap.length &&
	y.data() == b.view.content.data() + bp.offset && y.length() == bp.length;
    }
  });
  if (!borrowed) { std::cerr << name << ": borrowed payload range differs\n"; return false; }
  for (int32_t tag : {34, 52}) {
    Zfix::Fmt::FieldPos ap, bp;
    if (Zfix::Fmt::inspect<O, typename Entry::Mapping>(a.view.content, tag, ap) ||
	Zfix::Fmt::inspect<O, typename Entry::Mapping>(b.view.content, tag, bp) ||
	ap.offset == UINT_MAX || bp.offset == UINT_MAX) {
      std::cerr << name << ": missing session field " << tag << '\n'; return false;
    }
    if (ZuCSpan{a.view.content.data() + ap.offset, ap.length} !=
	ZuCSpan{b.view.content.data() + bp.offset, bp.length}) {
      std::cerr << name << ": session field differs " << tag << '\n'; return false;
    }
  }
  return true;
}
static int checkFrames(const char *directory) {
  bool ok = check<XMLMsg>(directory, "xml", [](const auto &a, const auto &b) {
      return a.xml == XMLPayload && a.xml == b.xml;
    }) && check<XMLBytesMsg>(directory, "xmlbytes", [](const auto &a, const auto &b) {
      return a.xml == ZuBSpan{XMLPayload} && a.xml == b.xml;
    }) && check<AuthMsg>(directory, "auth", [](const auto &a, const auto &b) {
      return a.raw == ZuBSpan{AuthPayload} && a.raw == b.raw &&
	a.encryptMethod == 0 && b.encryptMethod == 0 && a.heartBtInt == b.heartBtInt;
    }) && check<SignedMsg>(directory, "signed", [](const auto &a, const auto &b) {
      return sameOrder(a.order, b.order) && a.signature == ZuBSpan{SigPayload} &&
	a.signature == b.signature;
    }) && check<NewOrder>(directory, "order", sameOrder, 2) &&
    check<NewOrder>(directory, "order", sameOrder) && check<NewOrder>(directory, "groups",
      sameOrder) &&
    check<NewOrder>(directory, "text", sameOrder) &&
    check<NewOrder>(directory, "heavy", sameOrder) &&
    check<Ack>(directory, "ack", sameReport) && check<Fill>(directory, "fill", sameReport) &&
    check<CancelOrder>(directory, "cancel", [](const auto &a, const auto &b) {
      return a.original == b.original && a.key == b.key && a.symbol == b.symbol
	&& a.side == b.side &&
	a.time == b.time && a.qty == b.qty;
    }) && check<ReplaceOrder>(directory, "replace", [](const auto &a, const auto &b) {
      return a.original == b.original && sameOrder(a.order, b.order);
    });
  if (!ok) std::cerr << "independent peer frame mismatch\n";
  else std::cout <<
    "independent peer frames: scalar/group/text D, ack/fill 8, F/G and "
    "binary212/213,95/96,93/89 preserved\n";
  return ok ? 0 : 1;
}
int main(int argc, char **argv) {
  if (argc == 3 && !strcmp(argv[1], "--frames")) return dump(argv[2]);
  if (argc == 3 && !strcmp(argv[1], "--check-frames")) return checkFrames(argv[2]);
  std::cerr << "usage: nativecodec --frames|--check-frames DIRECTORY\n";
  return 2;
}
