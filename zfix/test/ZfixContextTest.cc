//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmRef.hh>
#include <zlib/ZfixContext.hh>
#include <zlib/ZfixTxMsg.hh>

using namespace ZuTestUtil;
using namespace Zfix;
struct Order {
  ZuCSpan	id;
};

ZfStruct(, (Order, FIX), (id, (FIX::ID<11>, Required), String));
ZfStructImpl(Order); ZfStructImpl(Order, FIX);
using Entry = MT<Order, "D">;
struct Other { };

using OtherEntry = MT<Other, "8">;
using ReplyEntry = MT<Order, "8">;
using RequestEntry = MT<Order, "D", ZuFacet::FIX, NoDiscriminator, ZuTypeList<ReplyEntry>>;
template <typename E = Entry> static RequestContext make(ZuCSpan &id) {
  Fmt::Error error;
  auto body = TxBody::encode<E>(Order{"retained"}, 99, error);
  auto stamp = ZuDateTime{ZuDateTimeScan::FIX{}, "20261027-12:34:56"};
  Fmt::Header header{.beginString = "FIX.4.4", .msgType = "D", .sender = "PEER",
    .target = "LOCAL", .seqNo = 9, .sendingTime = stamp,
    .origSendingTime = stamp, .possDup = true};
  if (!body || error) return {};
  auto buffer = txFrame(*body, header, error);
  if (!buffer || error) return {};
  auto parsed = Fmt::frame(buffer->cspan());
  if (parsed.state != Fmt::FrameState::Complete) return {};
  Fmt::MessageHandler<Order> input{parsed.view}; auto object = input.ctor();
  if (!input.valid()) return {};
  ZmRef<RxQueue::Node> message = new RxQueue::Node;
  auto &data = message->data();
  data.buffer = ZuMv(buffer); data.frame = parsed.view;
  data.header = input.header(); data.trailer = input.trailer();
  data.connection = 7; data.epoch = 3; data.received = ZuTime{123}; data.utc = stamp;
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
  ZuCheck(copy.header().sender == "PEER" && copy.header().target == "LOCAL");
  ZuCheck(copy.received() == ZuTime{123} && copy.utc() == copy.header().sendingTime);
  auto parsed = Fmt::frame(copy.frame().wire);
  ZuCheck(parsed.state == Fmt::FrameState::Complete);
}
struct NoHooks { };

struct SomeHooks {
  unsigned	called = 0, replies = 0;
  bool		permit = true;

  Fmt::Error fixReplyRx_(RequestEntry *, ReplyEntry *, const RequestContext &context) {
    if (!context) return {Fmt::ErrorCode::Unresolved, 35};
    if (!permit) return {Fmt::ErrorCode::Value, 11, 37};
    if (context.context().session != 1234 || context.context().epoch != 3)
      return {Fmt::ErrorCode::Unresolved, 35};
    ++replies; return {}; // explicit fixture policy permits replay/multiple replies
  }

  Fmt::Error fixReceiveRx_(Entry *, const Order &o, const RequestContext &context) {
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
using SelectedA = MT<Order, "D", ZuFacet::FIX, Discriminator<9000, "A">>;
using SelectedB = MT<Order, "D", ZuFacet::FIX, Discriminator<9000, "B">>;
static void selection() {
  ZuTestScope(selection);
  ZuCSpan id; auto context = make<SelectedA>(id);
  ZuCheck(bool(context)); if (!context) return;
  ZuCheck(context.is<SelectedA>() && !context.is<SelectedB>() && !context.is<Entry>());
  auto copy = context; context = {};
  ZuCheck(!context.is<SelectedA>() && copy.is<SelectedA>() && id == "retained");
  unsigned called = 0;
  auto error = dispatch<MTList<SelectedA, SelectedB>>(copy.frame(),
    [&called](auto *type, const Order &object, const auto &, const auto &) -> Fmt::Error {
      if constexpr (ZuIsSame<ZuDecay<decltype(*type)>, SelectedA>{}) {
	++called; ZuCheck(object.id == "retained");
      } else ZuCheck(false);
      return {};
    });
  ZuCheck(!error && called == 1);
}
int main(int argc, char **argv) {
  parse(argc, argv); ZuTestMain();
  ZuTestCall(empty);
  ZuTestCall(retention);
  ZuTestCall(defaults);
  ZuTestCall(replyPolicy);
  ZuTestCall(selection);
  return 0;
}
