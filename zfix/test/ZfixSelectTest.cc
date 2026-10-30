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
  ZuBSpan	data;
};

ZfStruct(, (Order, FIX), (kind, (FIX::ID<150>), String),
  (id, (FIX::ID<11>, Required), String), (data, (FIX::ID<96>, FIX::Len<95>), Bytes));
ZfStructImpl(Order); ZfStructImpl(Order, FIX);
template <unsigned Tag, ZuString Key>
using Selected = MT<Order, "X", ZuFacet::FIX, Discriminator<Tag, Key>>;
static Header header() {
  return {.beginString = "FIX.4.4", .sender = "CLIENT", .target = "SERVER",
    .seqNo = 7, .sendingTime = ZuDateTime{ZuDateTimeScan::FIX{}, "20261027-12:34:56"}};
}
static void known() {
  ZuTestScope(known);
  Error error;
  auto body = TxBody::encode<Selected<150, "A">>(Order{"A", "id", {}}, 42, error);
  ZuCheck(body && !error && body->id() == 42
    && body->body() == "150=A\1" "11=id\1" "95=0\1" "96=\1");
  auto wrong = TxBody::encode<Selected<150, "A">>(Order{"B", "id", {}}, 43, error);
  ZuCheck(!wrong && error.code == ErrorCode::Unresolved && error.tag == 150);
  auto binary = TxBody::encode<Selected<96, "A\1B">>(Order{{}, "id", ZuBSpan{"A\1B"_z}}, 44,
    error);
  ZuCheck(binary && !error);
  auto length = TxBody::encode<Selected<95, "3">>(Order{{}, "id", ZuBSpan{"A\1B"_z}}, 45, error);
  ZuCheck(length && !error);
  wrong = TxBody::encode<Selected<95, "2">>(Order{{}, "id", ZuBSpan{"A\1B"_z}}, 46, error);
  ZuCheck(!wrong && error.code == ErrorCode::Unresolved && error.tag == 95);
  auto empty = TxBody::encode<Selected<150, "">>(Order{"", "id", {}}, 47, error);
  ZuCheck(empty && !error && empty->body() == "150=\1" "11=id\1" "95=0\1" "96=\1");
}
static void unmodelled() {
  ZuTestScope(unmodelled);
  Error error;
  auto body = TxBody::encode<Selected<9000, "A">>(Order{{}, "id", ZuBSpan{"9000=B\1"_z}}, 42,
    error);
  ZuCheck(body && !error
    && body->body() == "150=\1" "11=id\1" "95=7\1" "96=9000=B\1\1" "9000=A\1");
  auto wire = body ? txFrame(*body, header(), error) : ZmRef<ZiIOBuf>{};
  ZuCheck(wire && !error);
  unsigned called = 0;
  if (wire) {
    auto parsed = frame(wire->cspan());
    error = dispatch<MTList<Selected<9000, "A">>>(parsed.view,
      [&called](auto *, const Order &o, const auto &, const auto &) -> Error {
	++called; ZuCheck(o.id == "id" && o.data == ZuBSpan{"9000=B\1"_z}); return {};
      });
    ZuCheck(!error && called == 1);
  }
  auto wrong = TxBody::encode<Selected<9000, "bad\1key">>(Order{{}, "id", {}}, 1, error);
  ZuCheck(!wrong && error.code == ErrorCode::Value);
  Limits limits; limits.parseBytes = 0;
  wrong = TxBody::encode<Selected<9000, "A">>(Order{{}, "id", {}}, 1, error, limits);
  ZuCheck(!wrong && error.code == ErrorCode::Size);
  limits = {}; limits.frameBytes = 10;
  wrong = TxBody::encode<Selected<9000, "A">>(Order{{}, "id", {}}, 1, error, limits);
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
  auto binary = TxBody::restoreRaw<Selected<96, "A\1B">>("11=id\1" "95=3\1" "96=A\1B\1", 14,
    false, error);
  ZuCheck(binary && !error);
  auto raw = TxBody::encodeRaw<Selected<150, "A">>(Order{"B", "id", {}}, 15, false, error);
  ZuCheck(!raw && error.code == ErrorCode::Unresolved && error.tag == 150);
}
static void envelope() {
  ZuTestScope(envelope);
  Error error;
  auto body = TxBody::encode<Selected<49, "CLIENT">>(Order{{}, "id", {}}, 42, error);
  ZuCheck(body && !error && body->validates());
  if (!body) return;
  auto h = header(); auto wire = txFrame(*body, h, error);
  ZuCheck(wire && !error);
  auto unqualified = TxBody::encode<MT<Order, "X">>(Order{{}, "id", {}}, 42, error);
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
  auto trailer = TxBody::encode<Selected<89, "sig\1data">>(Order{{}, "id", {}}, 43, error);
  wire = trailer ? txFrame(*trailer, header(), error, {},
    Trailer{.signature = ZuBSpan{"sig\1data"_z}}) : ZmRef<ZiIOBuf>{};
  ZuCheck(wire && !error);
  wire = trailer ? txFrame(*trailer, header(), error) : ZmRef<ZiIOBuf>{};
  ZuCheck(!wire && error.code == ErrorCode::Required && error.tag == 89);
}
int main(int argc, char **argv) {
  parse(argc, argv); ZuTestMain();
  ZuTestCall(known);
  ZuTestCall(unmodelled);
  ZuTestCall(history);
  ZuTestCall(envelope);
  return 0;
}
