//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmRef.hh>
#include <zlib/ZfixRaw.hh>
#include <zlib/ZfixTxMsg.hh>

using namespace ZuTestUtil;
using namespace Zfix;
using namespace Zfix::Fmt;
struct Order {
  ZuCSpan	kind, id;
};

ZfStruct(, (Order, FIX), (kind, (FIX::ID<150>), String),
  (id, (FIX::ID<11>, Required), String));
ZfStructImpl(Order); ZfStructImpl(Order, FIX);
template <unsigned Tag, ZuString Key>
using Selected = MT<Order, "X", ZuFacet::FIX, Discrim<Tag, Key>>;
static Header header() {
  return {.beginString = "FIX.4.4", .sender = "CLIENT", .target = "SERVER",
    .seqNo = 7, .sendingTime = ZuDateTime{ZuDateTimeScan::FIX{}, "20261027-12:34:56"}};
}
static void known() {
  ZuTestScope(known);
  Error error;
  auto body = TxBody::encode<Selected<150, "A">>(Order{"A", "id"}, 42, error);
  ZuCheck(body && !error && body->id() == 42
    && body->body() == "150=A\1" "11=id\1");
  auto wrong = TxBody::encode<Selected<150, "A">>(Order{"B", "id"}, 43, error);
  ZuCheck(!wrong && error.code == ErrorCode::Unresolved && error.tag == 150);
  auto empty = TxBody::encode<Selected<150, "">>(Order{"", "id"}, 47, error);
  ZuCheck(empty && !error && empty->body() == "150=\1" "11=id\1");
}
static void unmodelled() {
  ZuTestScope(unmodelled);
  Error error;
  auto body = TxBody::encode<Selected<9000, "A">>(Order{{}, "id"}, 42,
    error);
  ZuCheck(body && !error
    && body->body() == "150=\1" "11=id\1" "9000=A\1");
  auto wire = body ? txFrame(*body, header(), error) : ZmRef<ZiIOBuf>{};
  ZuCheck(wire && !error);
  unsigned called = 0;
  if (wire) {
    auto parsed = frame(wire->cspan());
    error = dispatch<MTList<Selected<9000, "A">>>(parsed.view,
      [&called](auto *, const Order &o, const auto &, const auto &) -> Error {
	++called; ZuCheck(o.id == "id"); return {};
      });
    ZuCheck(!error && called == 1);
  }
  auto wrong = TxBody::encode<Selected<9000, "bad\1key">>(Order{{}, "id"}, 1, error);
  ZuCheck(!wrong && error.code == ErrorCode::Value);
  Limits limits; limits.parseBytes = 0;
  wrong = TxBody::encode<Selected<9000, "A">>(Order{{}, "id"}, 1, error, limits);
  ZuCheck(wrong && !error); // selector inspection allocates no parse slots
  limits = {}; limits.frameBytes = 10;
  wrong = TxBody::encode<Selected<9000, "A">>(Order{{}, "id"}, 1, error, limits);
  ZuCheck(!wrong && error.code == ErrorCode::Size);
}
static void history() {
  ZuTestScope(history);
  Error error;
  auto body = TxBody::restore<Selected<9000, "A">>("11=id\1" "9000=A\1", 12, error);
  ZuCheck(body && !error && body->body() == "11=id\1" "9000=A\1");
  Limits exact; exact.parseBytes = Schema<Order, ZuFacet::FIX>::Leaves::N * sizeof(Position);
  auto bounded = TxBody::restore<Selected<9000, "A">>("11=id\1" "9000=A\1", 12, error, exact);
  ZuCheck(bounded && !error);
  --exact.parseBytes;
  bounded = TxBody::restore<Selected<9000, "A">>("11=id\1" "9000=A\1", 12, error, exact);
  ZuCheck(!bounded && error.code == ErrorCode::Size);
  auto missing = TxBody::restore<Selected<9000, "A">>("11=id\1", 13, error);
  ZuCheck(!missing && error.code == ErrorCode::Required && error.tag == 9000);
  auto wrong = TxBody::restore<Selected<9000, "A">>("11=id\1" "9000=B\1", 13, error);
  ZuCheck(!wrong && error.code == ErrorCode::Unresolved && error.tag == 9000);
  auto duplicate = TxBody::restore<Selected<9000, "A">>("11=id\1" "9000=A\1" "9000=A\1", 13,
    error);
  ZuCheck(!duplicate && error.code == ErrorCode::Duplicate && error.tag == 9000);
  auto raw = TxBody::encodeRaw<Selected<150, "A">>(Order{"B", "id"}, 15, false, error);
  ZuCheck(!raw && error.code == ErrorCode::Unresolved && error.tag == 150);
}
static void envelope() {
  ZuTestScope(envelope);
  Error error;
  auto body = TxBody::encode<Selected<49, "CLIENT">>(Order{{}, "id"}, 42, error);
  ZuCheck(body && !error && body->validates());
  if (!body) return;
  auto h = header(); auto wire = txFrame(*body, h, error);
  ZuCheck(wire && !error);
  auto unqualified = TxBody::encode<MT<Order, "X">>(Order{{}, "id"}, 42, error);
  h.sender = "OTHER";
  auto otherWire = txFrame(*unqualified, h, error);
  auto raw = RawOutput::fromFrame(body, {}, otherWire, error);
  ZuCheck(!raw && error.code == ErrorCode::Unresolved && error.tag == 49);

  wire = txFrame(*body, h, error);
  ZuCheck(!wire && error.code == ErrorCode::Unresolved && error.tag == 49 && error.offset);
  auto restored = TxBody::restore<Selected<49, "CLIENT">>(body->body(), 42, error);
  ZuCheck(restored && !error && restored->validates());
  wire = restored ? txFrame(*restored, h, error) : ZmRef<ZiIOBuf>{};
  ZuCheck(!wire && error.code == ErrorCode::Unresolved && error.tag == 49);
}
int main(int argc, char **argv) {
  parse(argc, argv); ZuTestMain();
  ZuTestCall(known);
  ZuTestCall(unmodelled);
  ZuTestCall(history);
  ZuTestCall(envelope);
  return 0;
}
