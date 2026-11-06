//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmSingleton.hh>
#include <zlib/ZmRef.hh>
#include <zlib/ZtArray.hh>
#include <zlib/ZiTx.hh>
#include <zlib/ZfixRaw.hh>
#include <zlib/ZfixTxMsg.hh>
#include <zlib/ZfixLinkState.hh>

static ZmRef<ZiIOBuf> frameBuf() {
  using Allocator = Zi::AppBufAllocator<ZiTxBuf, ZiIOBuf_DefltSize>;
  return ZmSingleton<Allocator>::instance()->allocBuf();
}

using namespace ZuTestUtil;
using namespace Zfix;
using namespace Zfix::Fmt;
struct Order {
  ZuCSpan	kind, id;
};

ZfStruct(, (Order), (kind, (FIX::ID<150>), String),
  (id, (FIX::ID<11>, Required), String));
using OrderFIXFields = ZuTypeConcat<SessionFields<Order>, ZfFields_Order>;
OrderFIXFields ZuFields_(Order *, ZuFacet::FIX *);
ZfStructImpl(Order); ZfStructImpl(Order, FIX);
template <int32_t Tag, ZuString Key>
using Selected = MT<Order, "X", ZuFacet::FIX, Discrim<Tag, Key>>;
static TxConfig config() {
  SessionConfig c;
  c.identity.version = Version::V4_4;
  c.identity.local = "CLIENT"; c.identity.peer = "SERVER";
  return txConfig(ZuMv(c));
}
static TxContext context() {
  return {.seqNo = 7,
    .sendingTime = ZuDateTime{ZuDateTimeScan::FIX{}, "20261027-12:34:56"}};
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
  auto wire = body ? txFrame(frameBuf(), *body, config(), context(), error) : ZmRef<ZiIOBuf>{};
  ZuCheck(wire && !error);
  unsigned called = 0;
  if (wire) {
    auto parsed = frame(wire->cspan());
    error = dispatch<MTList<Selected<9000, "A">>>(parsed.view,
      [&called](auto *, const Order &o) -> Error {
	++called; ZuCheck(o.id == "id"); return {};
      });
    ZuCheck(!error && called == 1);
  }
  auto wrong = TxBody::encode<Selected<9000, "bad\1key">>(Order{{}, "id"}, 1, error);
  ZuCheck(wrong && !error);
  Limits limits; limits.parseBytes = 0;
  wrong = TxBody::encode<Selected<9000, "A">>(Order{{}, "id"}, 1, error, limits);
  ZuCheck(wrong && !error); // selector inspection allocates no parse slots
  limits = {}; limits.frameBytes = 10;
  wrong = TxBody::encode<Selected<9000, "A">>(Order{{}, "id"}, 1, error, limits);
  ZuCheck(!wrong && error.code == ErrorCode::Size);
}
struct SignedOrder { ZuCSpan kind; };
ZfStruct(, (SignedOrder), (kind, (FIX::ID<-150>), String));
using SignedFIXFields = ZuTypeConcat<SessionFields<SignedOrder>, ZfFields_SignedOrder>;
SignedFIXFields ZuFields_(SignedOrder *, ZuFacet::FIX *);
using SignedEntry = MT<SignedOrder, "N", ZuFacet::FIX, Discrim<-150, "A">>;
static void negative() {
  ZuTestScope(negative);
  Error error;
  auto body = TxBody::encode<Selected<-9000, "A">>(Order{{}, "id"}, 42, error);
  ZuCheck(body && !error && body->body() == "150=\1" "11=id\1" "-9000=A\1");
  if (!body) return;
  auto wire = txFrame(frameBuf(), *body, config(), context(), error);
  ZuCheck(wire && !error);
  if (!wire) return;
  auto parsed = frame(wire->cspan());
  unsigned called = 0;
  error = dispatch<MTList<Selected<-9000, "A">>>(parsed.view,
    [&called](auto *, const Order &o) -> Error {
      ++called; ZuCheck(o.id == "id"); return {};
    });
  ZuCheck(!error && called == 1);
  auto known = TxBody::encode<SignedEntry>(SignedOrder{"A"}, 43, error);
  ZuCheck(known && !error);
  if (!known) return;
  wire = txFrame(frameBuf(), *known, config(), context(), error);
  ZuCheck(wire && !error);
  if (!wire) return;
  parsed = frame(wire->cspan());
  error = dispatch<MTList<SignedEntry>>(parsed.view,
    [&called](auto *, const SignedOrder &o) -> Error {
      ++called; ZuCheck(o.kind == "A"); return {};
    });
  ZuCheck(!error && called == 2);
}
static void history() {
  ZuTestScope(history);
  Error error;
  auto body = TxBody::restore<Selected<9000, "A">>("11=id\1" "9000=A\1", 12, error);
  ZuCheck(body && !error && body->body() == "11=id\1" "9000=A\1");
  Limits exact;
  exact.parseBytes = Schema<Order, ZuFacet::FIX>::Leaves::N * sizeof(Position);
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
static void sessionSelector() {
  ZuTestScope(sessionSelector);
  Error error;
  auto body = TxBody::encode<Selected<49, "CLIENT">>(Order{{}, "id"}, 42, error);
  ZuCheck(body && !error && body->validates());
  if (!body) return;
  auto cfg = config(); auto wire = txFrame(frameBuf(), *body, cfg, context(), error);
  ZuCheck(wire && !error);
  auto unqualified = TxBody::encode<MT<Order, "X">>(Order{{}, "id"}, 42, error);
  cfg.identity.local = "OTHER";
  auto otherWire = txFrame(frameBuf(), *unqualified, cfg, context(), error);
  auto raw = RawOutput::fromFrame(body, {}, otherWire, error);
  ZuCheck(!raw && error.code == ErrorCode::Unresolved && error.tag == 49);

  wire = txFrame(frameBuf(), *body, cfg, context(), error);
  ZuCheck(!wire && error.code == ErrorCode::Unresolved && error.tag == 49 && error.offset);
  auto restored = TxBody::restore<Selected<49, "CLIENT">>(body->body(), 42, error);
  ZuCheck(restored && !error && restored->validates());
  wire = restored ? txFrame(frameBuf(), *restored, cfg, context(), error) : ZmRef<ZiIOBuf>{};
  ZuCheck(!wire && error.code == ErrorCode::Unresolved && error.tag == 49);
}
struct BinaryOrder {
  ZuCSpan kind, id;
  ZuBSpan data;
};
ZfStruct(, (BinaryOrder),
  (kind, (FIX::ID<150>), String),
  (data, (FIX::ID<212>, FIX::Binary<213>, Required), Bytes),
  (id, (FIX::ID<11>, Required), String));
using BinaryFIXFields = ZuTypeConcat<SessionFields<BinaryOrder>, ZfFields_BinaryOrder>;
BinaryFIXFields ZuFields_(BinaryOrder *, ZuFacet::FIX *);
using BinarySelected = MT<BinaryOrder, "J", ZuFacet::FIX, Discrim<9000, "A">>;
using PayloadSelected = MT<BinaryOrder, "J", ZuFacet::FIX, Discrim<213, "A\1\0B">>;
using LengthSelected = MT<BinaryOrder, "J", ZuFacet::FIX, Discrim<212, "4">>;
using PaddedLength = MT<BinaryOrder, "J", ZuFacet::FIX, Discrim<212, "0004">>;
static void binaryUnmodelled()
{
  ZuTestScope(binaryUnmodelled);
  constexpr auto Payload = "9000=B\1" "150=FAKE\1" "35=Z\1" "10=000\1"_z;
  BinaryOrder object{{}, "id", ZuBSpan{Payload}};
  Error error; Limits limits; limits.parseBytes = 0;
  auto body = TxBody::encode<BinarySelected>(object, 1, error, limits);
  ZuCheck(body && !error);
  if (!body) return;
  FieldPos selector;
  ZuCheck(!inspect<BinaryOrder>(body->body(), 9000, selector, limits));
  ZuCheck(selector.offset != UINT_MAX && selector.length == 1 &&
    body->body()[selector.offset] == 'A');
  auto restored = TxBody::restore<BinarySelected>(body->body(), 1, error);
  ZuCheck(restored && !error);
  auto wire = txFrame(frameBuf(), *body, config(), context(), error);
  ZuCheck(wire && !error);
  if (!wire) return;
  unsigned called = 0;
  auto parsed = frame(wire->cspan());
  error = dispatch<MTList<BinarySelected>>(parsed.view,
    [&called, &parsed, payloadBytes = Payload.length()](
	auto *, const BinaryOrder &value) -> Error {
      ++called; ZuCheck(value.id == "id" && value.data.length() == payloadBytes);
      ZuCheck(value.data.data() == reinterpret_cast<const uint8_t *>(
	parsed.view.content.data() + unsigned(parsed.view.content.find("213=")) + 4));
      return {};
    });
  ZuCheck(!error && called == 1);
  // A key-shaped token inside the advertised payload cannot satisfy restore.
  auto storage = ZtScratch(PrintBuf, body->body().length());
  Sink onlyPair{{storage.data(), unsigned(body->body().length())}};
  saveBinary<ZfField(BinaryOrder, data)>(onlyPair, ZuBSpan{"9000=A\1"_z});
  onlyPair << "11=id\1";
  auto missing = TxBody::restore<BinarySelected>(onlyPair.data(), 1, error);
  ZuCheck(!missing && error.code == ErrorCode::Required && error.tag == 9000);
  onlyPair << "9000=B\1";
  auto wrong = TxBody::restore<BinarySelected>(onlyPair.data(), 1, error);
  ZuCheck(!wrong && error.code == ErrorCode::Unresolved && error.tag == 9000);
}
static void binaryPairKeys()
{
  ZuTestScope(binaryPairKeys);
  Error error;
  constexpr auto SelectedData = "A\1\0B"_z;
  constexpr auto OtherData = "Z\1\0B"_z;
  BinaryOrder object{{}, "id", ZuBSpan{SelectedData}};
  auto payload = TxBody::encode<PayloadSelected>(object, 1, error);
  ZuCheck(payload && !error);
  auto length = TxBody::encode<LengthSelected>(object, 1, error);
  ZuCheck(length && payload && !error && length->body() == payload->body());
  if (!payload) return;
  auto padded = TxBody::encode<PaddedLength>(object, 1, error);
  ZuCheck(!padded && error.code == ErrorCode::Unresolved && error.tag == 212);
  auto restored = TxBody::restore<PaddedLength>("212=0004\1" "213=A\1\0B\1" "11=id\1"_z,
    1, error);
  ZuCheck(restored && !error);
  auto raw = RawOutput::encode(frameBuf(), payload, config(), context(), error);
  ZuCheck(raw && !error);
  if (!raw) return;
  FieldPos selector;
  auto parsed = frame(raw->frame()->cspan());
  ZuCheck(!inspect<BinaryOrder>(parsed.view.content, 213, selector));
  ZuCheck(selector.length == 4 && parsed.view.content[selector.offset] == 'A' &&
    parsed.view.content[selector.offset + 2] == '\0');
  object.data = ZuBSpan{OtherData};
  auto invalid = TxBody::encode<PayloadSelected>(object, 1, error);
  ZuCheck(!invalid && error.code == ErrorCode::Unresolved && error.tag == 213);
  using Plain = MT<BinaryOrder, "J">;
  auto otherBody = TxBody::encode<Plain>(object, 2, error);
  auto otherWire = otherBody ?
    txFrame(frameBuf(), *otherBody, config(), context(), error) : ZmRef<ZiIOBuf>{};
  ZuCheck(otherWire && !error);
  if (!otherWire) return;
  auto mismatch = RawOutput::fromFrame(payload, context(), otherWire, error);
  ZuCheck(!mismatch && error.code == ErrorCode::Unresolved && error.tag == 213 && error.offset);
}

struct GroupItem {
  ZuCSpan id;
  ZuBSpan data;
  int role = 0;
};
ZfStruct(, (GroupItem, FIX),
  (id, (FIX::ID<448>, Required), String),
  (data, (FIX::ID<212>, FIX::Binary<213>, Required), Bytes),
  (role, (FIX::ID<452>, Required), Int32));
struct GroupItems : ZtArray<GroupItem, ZtArrayHeapID<"Zfix.SelectGroups">> {
  ZuDerive_(GroupItems, (ZtArray<GroupItem, ZtArrayHeapID<"Zfix.SelectGroups">>));
friend AsArray<> Zfix_Fmt(GroupItems *);
};
struct GroupOrder { GroupItems items; ZuCSpan id; };
ZfStruct(, (GroupOrder, FIX),
  (items, (FIX::ID<453>), UDT), (id, (FIX::ID<11>, Required), String));
using GroupSelected = MT<GroupOrder, "G", ZuFacet::FIX, Discrim<9000, "B">>;
static void binaryGroupSelector()
{
  ZuTestScope(binaryGroupSelector);
  Error error;
  // Root membership ends the group before its required tail. Neither this
  // misplaced key nor the key-like bytes inside the pair can satisfy restore.
  constexpr auto misplaced = "453=1\1" "448=p\1" "212=8\1" "213=9000=A\1X\1"
    "9000=B\1" "452=7\1" "11=end\1"_z;
  auto invalid = TxBody::restore<GroupSelected>(misplaced, 1, error);
  ZuCheck(!invalid && error.code == ErrorCode::Required && error.tag == 452);
  constexpr auto missing = "453=1\1" "448=p\1" "212=8\1" "213=9000=B\1X\1"
    "452=7\1" "11=end\1"_z;
  invalid = TxBody::restore<GroupSelected>(missing, 1, error);
  ZuCheck(!invalid && error.code == ErrorCode::Required && error.tag == 9000);
  // The real root key may follow the last entry immediately, without a footer
  // field separating it from the group. Archive bytes retain the exact pair.
  constexpr auto valid = "453=1\1" "448=p\1" "212=8\1" "213=9000=A\1X\1"
    "452=7\1" "9000=B\1" "11=end\1"_z;
  auto restored = TxBody::restore<GroupSelected>(valid, 2, error);
  ZuCheck(restored && !error && restored->body() == valid);
}

int main(int argc, char **argv) {
  parse(argc, argv); ZuTestMain();
  ZuTestCall(known);
  ZuTestCall(binaryUnmodelled);
  ZuTestCall(binaryPairKeys);
  ZuTestCall(binaryGroupSelector);
  ZuTestCall(unmodelled);
  ZuTestCall(negative);
  ZuTestCall(history);
  ZuTestCall(sessionSelector);
  return 0;
}
