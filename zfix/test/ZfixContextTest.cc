//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmSingleton.hh>
#include <zlib/ZmRef.hh>
#include <zlib/ZiTx.hh>
#include <zlib/ZfixContext.hh>
#include <zlib/ZfixTxMsg.hh>
#include <zlib/ZfixLinkState.hh>

static ZmRef<ZiIOBuf> frameBuf() {
  using Allocator = Zi::IOBufBuiltinAllocator<Zi::IOBufType::App, ZiTxBuf, ZiIOBuf_DefltSize>;
  return ZmSingleton<Allocator>::instance()->allocBuf();
}

using namespace ZuTestUtil;
using namespace Zfix;
struct Order {
  ZuCSpan	id;
};

ZfStruct(, (Order), (id, (FIX::ID<11>, Required), String));
using OrderFIXFields = ZuTypeConcat<SessionFields<Order>, ZfFields_Order>;
OrderFIXFields ZuFields_(Order *, ZuFacet::FIX *);
ZfStructImpl(Order); ZfStructImpl(Order, FIX);
using Entry = MT<Order, "D">;
struct Other { };

using OtherEntry = MT<Other, "8">;
using ReplyEntry = MT<Order, "8">;
using RequestEntry = MT<Order, "D", ZuFacet::FIX, NoDiscrim, ZuTypeList<ReplyEntry>>;
template <typename E = Entry> static RequestContext make(ZuCSpan &id) {
  Fmt::Error error;
  auto body = TxBody::encode<E>(Order{"retained"}, 99, error);
  auto stamp = ZuDateTime{ZuDateTimeScan::FIX{}, "20261027-12:34:56"};
  SessionConfig c; c.identity.version = Version::V4_4;
  c.identity.local = "PEER"; c.identity.peer = "LOCAL";
  TxContext tx{.epoch = 3, .connection = 7, .seqNo = 9, .replay = true,
    .sendingTime = stamp, .origSendingTime = stamp, .possDup = true};
  if (!body || error) return {};
  auto buffer = txFrame(frameBuf(), *body, txConfig(ZuMv(c)), tx, error);
  if (!buffer || error) return {};
  auto parsed = Fmt::frame(buffer->cspan());
  if (parsed.state != Fmt::FrameState::Complete) return {};
  ZmRef<RxQueue::Node> message = new RxQueue::Node;
  auto &data = message->data();
  data.buffer = ZuMv(buffer); data.frame = parsed.view;
  data.connection = 7; data.epoch = 3; data.received = ZuTime{123}; data.utc = stamp;
  if (catalogSession<MTList<E>>(data)) return {};
  Fmt::MessageHandler<Order> input{data.frame}; auto object = input.ctor();
  if (!input.valid()) return {};
  id = object.id;
  return {static_cast<E *>(nullptr), uint128_t{1234}, ZmRef<const RxQueue::Node>{ZuMv(message)}};
}
static void empty() {
  ZuTestScope(empty);
  RequestContext context;
  ZuCheck(!context && !context.context().session && !context.context().epoch);
  auto copy = context;
  ZuCheck(!copy && !copy.context().connection && !copy.context().version);
}
static void retention() {
  ZuTestScope(retention);
  ZuCSpan id; auto context = make(id);
  ZuCheck(bool(context)); if (!context) return;
  auto copy = context; auto wire = context.frame().wire;
  context = {};
  ZuCheck(!context && bool(copy));
  ZuCheck(copy.frame().wire.data() == wire.data() && copy.frame().wire.length() == wire.length());
  ZuCheck(id == "retained" && id.data() >= wire.data()
    && id.data() + id.length() <= wire.data() + wire.length());
  const auto &meta = copy.context();
  ZuCheck(meta.session == 1234 && meta.connection == 7 && meta.epoch == 3 && meta.seqNo == 9);
  ZuCheck(meta.replay && meta.possDup && meta.version == "FIX.4.4");
  ZuCheck(copy.is<Entry>() && copy.is<RequestEntry>() && !copy.is<OtherEntry>());
  ZuCheck(copy.sender() == "PEER" && copy.target() == "LOCAL");
  ZuCheck(copy.received() == ZuTime{123} && copy.utc() == copy.sendingTime());
  auto parsed = Fmt::frame(copy.frame().wire);
  ZuCheck(parsed.state == Fmt::FrameState::Complete);
}
struct NoHooks { };

struct SomeHooks {
  unsigned	called = 0, replies = 0;
  bool		permit = true;

  Fmt::Error fixReplyRx(RequestEntry *, ReplyEntry *, const RequestContext &context) {
    if (!context) return {Fmt::ErrorCode::Unresolved, 35};
    if (!permit) return {Fmt::ErrorCode::Value, 11, 37};
    if (context.context().session != 1234 || context.context().epoch != 3)
      return {Fmt::ErrorCode::Unresolved, 35};
    ++replies; return {}; // explicit fixture policy permits replay/multiple replies
  }

  Fmt::Error fixReceiveRx(Entry *, const Order &o, const RequestContext &context) {
    ++called;
    if (o.id != "retained" || context.context().seqNo != 9) return {Fmt::ErrorCode::Value, 11};
    return {};
  }
};

static void defaults() {
  ZuTestScope(defaults);
  ZuCSpan id; auto context = make(id); ZuCheck(bool(context)); if (!context) return;
  NoHooks noHooks; SomeHooks hooks;
  auto error = Context_::Receive<NoHooks, Entry>::call(&noHooks, nullptr, Order{id}, context);
  ZuCheck(error.code == Fmt::ErrorCode::Unresolved && error.tag == 35);
  error = Context_::Receive<SomeHooks, Entry>::call(&hooks, nullptr, Order{id}, context);
  ZuCheck(!error && hooks.called == 1);
  error = Context_::Receive<SomeHooks, OtherEntry>::call(&hooks, nullptr, Other{}, context);
  ZuCheck(error.code == Fmt::ErrorCode::Unresolved && error.tag == 35 && hooks.called == 1);
}
static void replyPolicy() {
  ZuTestScope(replyPolicy);
  ZuCSpan id; auto context = make(id); ZuCheck(bool(context)); if (!context) return;
  NoHooks none; SomeHooks hooks;
  auto error = Context_::Reply<NoHooks, RequestEntry, ReplyEntry, RequestContext>::call(&none,
    context);
  ZuCheck(error.code == Fmt::ErrorCode::Unresolved && error.tag == 35);
  error = Context_::Reply<SomeHooks, RequestEntry, ReplyEntry, RequestContext>::call(&hooks,
    context);
  ZuCheck(!error && hooks.replies == 1 && context.context().replay);
  error = Context_::Reply<SomeHooks, RequestEntry, ReplyEntry, RequestContext>::call(&hooks,
    context);
  ZuCheck(!error && hooks.replies == 2 && bool(context));
  hooks.permit = false;
  error = Context_::Reply<SomeHooks, RequestEntry, ReplyEntry, RequestContext>::call(&hooks,
    context);
  ZuCheck(error.code == Fmt::ErrorCode::Value && error.tag == 11 && error.offset == 37
    && hooks.replies == 2);
  error = Context_::Reply<SomeHooks, RequestEntry, OtherEntry, RequestContext>::call(&hooks,
    context);
  ZuCheck(error.code == Fmt::ErrorCode::Unresolved && error.tag == 35 && hooks.replies == 2);
}
using SelectedA = MT<Order, "D", ZuFacet::FIX, Discrim<9000, "A">>;
using SelectedB = MT<Order, "D", ZuFacet::FIX, Discrim<9000, "B">>;
static void selection() {
  ZuTestScope(selection);
  ZuCSpan id; auto context = make<SelectedA>(id);
  ZuCheck(bool(context)); if (!context) return;
  ZuCheck(context.is<SelectedA>() && !context.is<SelectedB>() && !context.is<Entry>());
  auto copy = context; context = {};
  ZuCheck(!context.is<SelectedA>() && copy.is<SelectedA>() && id == "retained");
  unsigned called = 0;
  auto error = dispatch<MTList<SelectedA, SelectedB>>(copy.frame(),
    [&called](auto *type, const Order &object) -> Fmt::Error {
      if constexpr (ZuIsSame<ZuDecay<decltype(*type)>, SelectedA>{}) {
	++called; ZuCheck(object.id == "retained");
      } else ZuCheck(false);
      return {};
    });
  ZuCheck(!error && called == 1);
}
struct BinaryRequest {
  ZuCSpan id;
  ZuBSpan xml;
  ZuCSpan replay;
  ZuBSpan signature;
};
ZfStruct(, (BinaryRequest),
  (id, (FIX::ID<11>, Required), String),
  (xml, (FIX::ID<212>, FIX::Binary<213>, Required), Bytes),
  (replay, (FIX::ID<95>, FIX::Binary<96>, Required), String),
  (signature, ((FIX::Sig<93, 89>), Required), Bytes));
using BinaryFields = ZuTypeConcat<SessionFields<BinaryRequest>, ZfFields_BinaryRequest>;
BinaryFields ZuFields_(BinaryRequest *, ZuFacet::FIX *);
using BinaryEntry = MT<BinaryRequest, "n">;
static void binaryRetention()
{
  ZuTestScope(binaryRetention);
  constexpr auto XML = "<RTRF>49=FAKE\1" "34=999\1" "52=bad\1"
    "8=FIX.4.4\1" "35=D\1" "10=000\1" "</RTRF>"_z;
  constexpr auto Replay = "8=FIX.4.4\1" "35=D\1" "10=000\1"
    "8=FIX.4.4\1" "35=8\1" "10=001\1"_z;
  constexpr auto Signature = "\0\1\xff"_z;
  Fmt::Error error;
  auto buf = frameBuf();
  TxFrame writer{buf, "FIX.4.4"};
  writer.body() << "35=n\1" "49=PEER\1" "56=LOCAL\1" "34=9\1"
    "43=Y\1" "52=20261027-12:34:56\1" "122=20261027-12:34:55\1" "11=retained\1";
  Fmt::saveBinary<ZfField(BinaryRequest, xml)>(writer.body(), XML);
  Fmt::saveBinary<ZfField(BinaryRequest, replay)>(writer.body(), Replay);
  Fmt::saveBinary<ZfField(BinaryRequest, signature)>(writer.body(), Signature);
  auto buffer = writer.finish(error);
  ZuCheck(buffer == buf && !error);
  if (!buffer) return;
  auto parsed = Fmt::frame(buffer->cspan());
  ZuCheck(parsed.state == Fmt::FrameState::Complete);
  auto original = parsed.view.wire.data();
  ZmRef<RxQueue::Node> message = new RxQueue::Node;
  auto &data = message->data();
  data.buffer = buffer; data.frame = parsed.view;
  data.epoch = 3; data.connection = 7; data.received = ZuTime{123};
  data.utc = {ZuDateTimeScan::FIX{}, "20261027-12:34:56"};
  ZuCheck(!catalogSession<MTList<BinaryEntry>>(data));
  ZuCheck(data.seqNo == 9 && data.sender == "PEER" && data.target == "LOCAL");
  Fmt::FieldPos xml, replay, signature;
  ZuCheck(!Fmt::inspect<BinaryRequest>(data.frame.content, 213, xml));
  ZuCheck(!Fmt::inspect<BinaryRequest>(data.frame.content, 96, replay));
  ZuCheck(!Fmt::inspect<BinaryRequest>(data.frame.content, 89, signature));
  unsigned base = data.frame.content.data() - original;
  BinaryRequest borrowed;
  RequestContext context;
  error = dispatch<MTList<BinaryEntry>>(data.frame,
    [&borrowed, &context, &message](auto *type, const BinaryRequest &object) -> Fmt::Error {
      borrowed = object;
      context = RequestContext{type, uint128_t{1234}, ZmRef<const RxQueue::Node>{message}};
      return {};
    });
  ZuCheck(!error && bool(context));
  auto retained = context;
  context = {}; message = nullptr; buffer = nullptr; parsed = {};
  ZuCheck(!context && retained.frame().wire.data() == original && retained.is<BinaryEntry>());
  ZuCheck(borrowed.xml.data() == reinterpret_cast<const uint8_t *>(original + base + xml.offset));
  ZuCheck(borrowed.replay.data() == original + base + replay.offset);
  ZuCheck(borrowed.signature.data() ==
    reinterpret_cast<const uint8_t *>(original + base + signature.offset));
  ZuCheck(borrowed.xml == ZuBSpan{XML} && borrowed.replay == Replay &&
    borrowed.signature == ZuBSpan{Signature} && borrowed.id == "retained");
  ZuCheck(retained.sender() == "PEER" && retained.target() == "LOCAL" &&
    retained.context().seqNo == 9 && retained.context().possDup);
  ZuCheck(retained.sender().data() ==
    original + unsigned(retained.frame().wire.find("49=PEER")) + 3);
  ZuCheck(retained.target().data() ==
    original + unsigned(retained.frame().wire.find("56=LOCAL")) + 3);
  ZuCheck((retained.sendingTime() == ZuDateTime{ZuDateTimeScan::FIX{},
    "20261027-12:34:56"} && retained.origSendingTime() == ZuDateTime{ZuDateTimeScan::FIX{},
    "20261027-12:34:55"}));
}

int main(int argc, char **argv) {
  parse(argc, argv); ZuTestMain();
  ZuTestCall(empty);
  ZuTestCall(binaryRetention);
  ZuTestCall(retention);
  ZuTestCall(defaults);
  ZuTestCall(replyPolicy);
  ZuTestCall(selection);
  return 0;
}
