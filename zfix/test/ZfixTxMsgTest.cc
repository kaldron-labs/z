//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuPtr.hh>
#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZtArray.hh>
#include <zlib/ZtString.hh>
#include <zlib/ZfixTxMsg.hh>

using namespace ZuTestUtil;
using namespace Zfix;
using namespace Zfix::Fmt;

struct Order {
  ZuCSpan	id;
  ZuBSpan	data;
};

ZfStruct(, (Order, FIX),
  (id, (FIX::ID<11>), String),
  (data, (FIX::ID<96>, FIX::Len<95>), Bytes));
ZfStructImpl(Order);
ZfStructImpl(Order, FIX);
using OrderMsg = MT<Order, "D">;

class ArchiveOnly {
public:
  inline static unsigned	getters = 0;
  inline static unsigned	constructed = 0;

  ArchiveOnly(void *, int value) : m_value{value} { ++constructed; }

  int value() const { ++getters; return m_value; }
private:
  int				m_value;
};

ZfStruct(, (ArchiveOnly, FIX),
  ((value, RdFn), (FIX::ID<38>, Required, Keys<0>), Int32));
ZfStructImpl(ArchiveOnly);
ZfStructImpl(ArchiveOnly, FIX);
using ArchiveMsg = MT<ArchiveOnly, "D">;

struct Party {
  ZuCSpan	id;
  int		role = 0;
};

ZfStruct(, (Party, FIX),
  (id, (FIX::ID<448>, Required), String),
  (role, (FIX::ID<452>, Required), Int32));
ZfStructImpl(Party);
ZfStructImpl(Party, FIX);
struct Parties : ZtArray<Party, ZtArrayHeapID<"Zfix.ArchiveParties">> {
  ZuDerive_(Parties, (ZtArray<Party, ZtArrayHeapID<"Zfix.ArchiveParties">>));
friend AsArray<> Zfix_Fmt(Parties *);
};

struct GroupOrder {
  Parties	parties;
};

ZfStruct(, (GroupOrder, FIX), (parties, (FIX::ID<453>), UDT));
ZfStructImpl(GroupOrder);
ZfStructImpl(GroupOrder, FIX);
using GroupMsg = MT<GroupOrder, "D">;

struct ArchiveComponent : ZmNHeap<ArchiveComponent> {
friend ZuStringT<"Zfix.ArchiveComponent"> ZmNHeapID(ArchiveComponent *);
  int	value = 0;
};

ZfStruct(, (ArchiveComponent, FIX), (value, (FIX::ID<21>, Required), Int32));
ZfStructImpl(ArchiveComponent);
ZfStructImpl(ArchiveComponent, FIX);
struct WithComponent {
  ZuPtr<ArchiveComponent>	component;
};

ZfStruct(, (WithComponent, FIX), (component, , UDT));
ZfStructImpl(WithComponent);
ZfStructImpl(WithComponent, FIX);
using ComponentMsg = MT<WithComponent, "D">;

static Header header()
{
  return {.beginString = "FIX.4.4", .sender = "CLIENT", .target = "SERVER",
    .seqNo = 7, .sendingTime = ZuDateTime{ZuDateTimeScan::FIX{}, "20261027-12:34:56"}};
}

static void retained()
{
  ZuTestScope(retained);
  Error error;
  auto payload = TxBody::encode<OrderMsg>(Order{"abc", ZuBSpan{"A\1\0="_z}}, 91, error);
  ZuCheck(payload && !error && payload->id() == 91 && payload->msgType() == "D");
  constexpr auto original = "11=abc\1" "95=4\1" "96=A\1\0=\1"_z;
  ZuCheck(payload->body() == original && !payload->administrative());
  auto h = header();
  auto wire = txFrame(*payload, h, error);
  ZuCheck(wire && !error);
  auto parsed = frame(ZuCSpan{wire->cspan()});
  MessageHandler<Order> decoded{parsed.view};
  auto order = decoded.ctor();
  ZuCheck(decoded.valid() && order.id == "abc" && order.data == ZuBSpan{"A\1\0="_z});
  ZuCheck(decoded.header().seqNo == 7 && decoded.header().sendingTime == h.sendingTime);

  // Transport mutation and partial-send cursor changes cannot touch history.
  memset(wire->data(), 'X', wire->length);
  wire->advance(9);
  ZuCheck(payload->body() == original);
  h.possDup = true;
  h.origSendingTime = h.sendingTime;
  h.sendingTime = ZuDateTime{ZuDateTimeScan::FIX{}, "20261027-13:00:00"};
  auto replay = txFrame(*payload, h, error);
  ZuCheck(replay && !error);
  MessageHandler<Order> restored{frame(ZuCSpan{replay->cspan()}).view};
  auto got = restored.ctor();
  ZuCheck(restored.valid() && got.id == "abc" && got.data == ZuBSpan{"A\1\0="_z});
  ZuCheck(restored.header().seqNo == 7 && restored.header().possDup &&
    restored.header().origSendingTime == h.origSendingTime &&
    restored.header().sendingTime == h.sendingTime);
}

static void controls()
{
  ZuTestScope(controls);
  Error error;
  Session::Msg value;
  value.p<Session::HeartBt>(Session::HeartBt{});
  auto heartbeat = TxBody::control(value, error);
  ZuCheck(heartbeat && !error && !heartbeat->body().length());
  ZuCheck(heartbeat->administrative());
  auto encoded = txFrame(*heartbeat, header(), error);
  ZuCheck(encoded && !error);
  Session::Msg got;
  Header h;
  Trailer t;
  ZuCheck(!Session::read(frame(ZuCSpan{encoded->cspan()}).view, got, h, t));
  ZuCheck(got.type() == Session::Msg::Index<Session::HeartBt>{} && h.seqNo == 7);
  value.p<Session::Reject>(Session::Reject{});
  auto &reject = value.p<Session::Reject>();
  reject.refSeqNo = 4;
  reject.reason = 5;
  auto body = TxBody::control(value, error);
  ZuCheck(body && !error && body->administrative());
  ZuCheck(body->body() == "45=4\1" "373=5\1"_z);
  value.p<Session::Logon>(Session::Logon{});
  auto invalid = TxBody::control(value, error, Transport::FIXT11);
  ZuCheck(!invalid && error.code == ErrorCode::Required && error.tag == 1137);
  using ReservedMsg = MT<Order, "0">;
  auto reserved = TxBody::encode<ReservedMsg>(Order{}, 1, error);
  ZuCheck(!reserved && error.code == ErrorCode::Reserved && error.tag == 35);
}

static void jumbo()
{
  ZuTestScope(jumbo);
  using Bytes = ZtArray<char, ZtArrayHeapID<"Zfix.TxMsgTest">>;
  Bytes bytes;
  unsigned n = ZiIOBuf_DefltSize<<3; // cross built-in pool capacity repeatedly
  bytes.length(n, false);
  for (unsigned i = 0; i < n; ++i) bytes[i] = char(i);
  Error error;
  Order order{"large", ZuBSpan{bytes}};
  auto body = TxBody::encode<OrderMsg>(order, 92, error);
  ZuCheck(body && !error && body->body().length() > n && body->size > ZiIOBuf_DefltSize);
  auto output = txFrame(*body, header(), error);
  ZuCheck(output && !error && output->size > ZiIOBuf_DefltSize);
  MessageHandler<Order> decoder{frame(ZuCSpan{output->cspan()}).view};
  auto restored = decoder.ctor();
  ZuCheck(decoder.valid() && restored.data == order.data && restored.id == order.id);
  Limits limits;
  limits.frameBytes = output->length;
  auto exact = txFrame(*body, header(), error, limits);
  ZuCheck(exact && !error && exact->length == output->length);
  --limits.frameBytes;
  auto shortOut = txFrame(*body, header(), error, limits);
  ZuCheck(!shortOut && error.code == ErrorCode::Size);
  limits = {};
  limits.fieldBytes = n - 1;
  auto tooLong = TxBody::encode<OrderMsg>(order, 93, error, limits);
  ZuCheck(!tooLong && error.code == ErrorCode::Size && error.tag == 96);
}

static void failures()
{
  ZuTestScope(failures);
  Error error;
  auto bad = TxBody::encode<OrderMsg>(Order{"bad\1value"_z, {}}, 1, error);
  ZuCheck(!bad && error.code == ErrorCode::Value && error.tag == 11);
  auto valid = TxBody::encode<OrderMsg>(Order{"abc", {}}, 1, error);
  ZuCheck(valid && !error);
  auto h = header();
  h.seqNo = 0;
  auto noSequence = txFrame(*valid, h, error);
  ZuCheck(!noSequence && error.code == ErrorCode::Value && error.tag == 34);
  h = header();
  h.possDup = true;
  auto noOriginal = txFrame(*valid, h, error);
  ZuCheck(!noOriginal && error.code == ErrorCode::Required && error.tag == 122);
  h.origSendingTime = ZuDateTime{ZuDateTimeScan::FIX{}, "20261027-13:00:00"};
  auto futureOriginal = txFrame(*valid, h, error);
  ZuCheck(!futureOriginal && error.code == ErrorCode::Value && error.tag == 122);
  // Long declared versions must grow the envelope headroom before body output.
  ZtString<ZtStringHeapID<"Zfix.TxMsgTest">> version;
  version.ensure(ZiIOBuf_DefltSize<<1);
  for (unsigned i = 0; i < (ZiIOBuf_DefltSize<<1); ++i) version << 'V';
  h = header(); h.beginString = version;
  auto longVersion = txFrame(*valid, h, error);
  ZuCheck(longVersion && !error);
  auto parsed = frame(ZuCSpan{longVersion->cspan()});
  ZuCheck(parsed.state == FrameState::Complete && parsed.view.beginString == version);
}

static void archive()
{
  ZuTestScope(archive);
  Error error;
  ArchiveOnly::getters = ArchiveOnly::constructed = 0;
  // Leading zeros and unknown extensions must survive without normalization.
  constexpr auto original = "38=0007\1" "9000=opaque\1"_z;
  auto body = TxBody::restore<ArchiveMsg>(original, 83, error);
  ZuCheck(body && !error && body->id() == 83 && body->body() == original);
  ZuCheck(!ArchiveOnly::getters && !ArchiveOnly::constructed);
  auto h = header(); h.possDup = true; h.origSendingTime = h.sendingTime;
  auto wire = txFrame(*body, h, error);
  ZuCheck(wire && !error && ZuCSpan{wire->cspan()}.find(original) >= 0);
  auto bad = TxBody::restore<ArchiveMsg>("38=bad\1"_z, 84, error);
  ZuCheck(!bad && error.code == ErrorCode::Value && error.tag == 38);
  bad = TxBody::restore<ArchiveMsg>("9000=opaque\1"_z, 84, error);
  ZuCheck(!bad && error.code == ErrorCode::Required && error.tag == 38);
  bad = TxBody::restore<ArchiveMsg>("38=7\1" "49=INJECT\1"_z, 84, error);
  ZuCheck(!bad && error.code == ErrorCode::Reserved && error.tag == 49);
  ZuCheck(!ArchiveOnly::getters && !ArchiveOnly::constructed);

  ZtString<ZtStringHeapID<"Zfix.ArchiveInput">> borrowed = original;
  auto owned = TxBody::restore<ArchiveMsg>(borrowed, 86, error);
  borrowed.clear(); borrowed << "reused storage";
  ZuCheck(owned && !error && owned->body() == original);

  constexpr auto binary = "11=abc\1" "95=4\1" "96=A\1\0=\1"_z;
  auto data = TxBody::restore<OrderMsg>(binary, 85, error);
  ZuCheck(data && !error && data->body() == binary);
  auto output = txFrame(*data, header(), error);
  MessageHandler<Order> decoded{frame(ZuCSpan{output->cspan()}).view};
  auto got = decoded.ctor();
  ZuCheck(decoded.valid() && got.id == "abc" && got.data == ZuBSpan{"A\1\0="_z});
  auto shortData = TxBody::restore<OrderMsg>("11=abc\1" "95=5\1" "96=A\1\0=\1"_z, 85, error);
  ZuCheck(!shortData && error.code == ErrorCode::Truncated);
  Limits limits;
  limits.frameBytes = binary.length();
  auto oversized = TxBody::restore<OrderMsg>(binary, 85, error, limits);
  ZuCheck(!oversized && error.code == ErrorCode::Size);
}

static void archiveGroups()
{
  ZuTestScope(archiveGroups);
  Error error;
  constexpr auto original = "453=2\1" "448=FIRST\1" "452=01\1" "448=SECOND\1" "452=02\1"_z;
  auto body = TxBody::restore<GroupMsg>(original, 11, error);
  ZuCheck(body && !error && body->body() == original);
  auto incomplete = TxBody::restore<GroupMsg>("453=1\1" "448=FIRST\1"_z, 11, error);
  ZuCheck(!incomplete && error.code == ErrorCode::Required && error.tag == 452);
  auto malformed = TxBody::restore<GroupMsg>("453=1\1" "448=FIRST\1" "452=x\1"_z, 11, error);
  ZuCheck(!malformed && error.code == ErrorCode::Value && error.tag == 452);
  auto absent = TxBody::restore<ComponentMsg>({}, 12, error);
  ZuCheck(absent && !error && !absent->body().length());
  auto present = TxBody::restore<ComponentMsg>("21=7\1"_z, 12, error);
  ZuCheck(present && !error);
}

static void archiveControls()
{
  ZuTestScope(archiveControls);
  Error error;
  constexpr auto original = "45=0007\1" "373=05\1" "58=stored\1"_z;
  auto reject = TxBody::restoreControl("3", original, 0, error);
  ZuCheck(reject && !error && reject->body() == original && reject->administrative());
  auto hb = TxBody::restoreControl("0", {}, 0, error);
  ZuCheck(hb && !error && !hb->body().length() && hb->administrative());
  auto bad = TxBody::restoreControl("3", "45=0\1"_z, 0, error);
  ZuCheck(!bad && error.code == ErrorCode::Value && error.tag == 45);
  bad = TxBody::restoreControl("1", {}, 0, error);
  ZuCheck(!bad && error.code == ErrorCode::Required && error.tag == 112);
  bad = TxBody::restoreControl("D", "11=abc\1"_z, 0, error);
  ZuCheck(!bad && error.code == ErrorCode::Unresolved && error.tag == 35);
  using RejectMsg = MT<Session::Reject, "3">;
  bad = TxBody::restore<RejectMsg>(original, 0, error);
  ZuCheck(!bad && error.code == ErrorCode::Reserved && error.tag == 35);
}

// Root selector 150 is absent from KeyOrder's fields but present in a group.
struct KeyParty {
  Party		party;
  ZuCSpan	kind;
};

ZfStruct(, (KeyParty, FIX), (party, , UDT),
  (kind, (FIX::ID<150>, Required), String));
ZfStructImpl(KeyParty);
ZfStructImpl(KeyParty, FIX);
struct KeyParties : ZtArray<KeyParty, ZtArrayHeapID<"Zfix.ArchiveKeys">> {
  ZuDerive_(KeyParties, (ZtArray<KeyParty, ZtArrayHeapID<"Zfix.ArchiveKeys">>));
friend AsArray<> Zfix_Fmt(KeyParties *);
};

struct KeyOrder {
  KeyParties	parties;
};

ZfStruct(, (KeyOrder, FIX), (parties, (FIX::ID<453>), UDT));
ZfStructImpl(KeyOrder);
ZfStructImpl(KeyOrder, FIX);

static void archiveNestedKey()
{
  ZuTestScope(archiveNestedKey);
  using Selected = MT<KeyOrder, "D", ZuFacet::FIX, Discriminator<150, "A">>;
  KeyOrder order;
  order.parties.push(KeyParty{{"p", 3}, "A"});
  Error error;
  auto encoded = TxBody::encode<Selected>(order, 42, error);
  constexpr auto bytes = "453=1\1" "448=p\1" "452=3\1" "150=A\1" "150=A\1"_z;
  ZuCheck(encoded && !error && encoded->body() == bytes);
  auto restored = TxBody::restore<Selected>(bytes, 42, error);
  ZuCheck(restored && encoded && !error &&
    restored->body() == encoded->body() && restored->id() == 42);
  auto missing = TxBody::restore<Selected>(
    "453=1\1" "448=p\1" "452=3\1" "150=A\1"_z, 42, error);
  ZuCheck(!missing && error.code == ErrorCode::Required && error.tag == 150);
  auto wrong = TxBody::restore<Selected>(
    "453=1\1" "448=p\1" "452=3\1" "150=A\1" "150=B\1"_z, 42, error);
  ZuCheck(!wrong && error.code == ErrorCode::Unresolved && error.tag == 150);
}

static void archiveSelector()
{
  ZuTestScope(archiveSelector);
  using Selected = MT<GroupOrder, "D", ZuFacet::FIX, Discriminator<150, "A">>;
  Error error;
  constexpr auto bytes = "453=1\1" "448=p\1" "452=3\1" "150=A\1"_z;
  auto saved = TxBody::restore<Selected>(bytes, 42, error);
  ZuCheck(saved && !error && saved->body() == bytes);
  auto missing = TxBody::restore<Selected>("453=1\1" "448=p\1" "452=3\1"_z, 42, error);
  ZuCheck(!missing && error.code == ErrorCode::Required && error.tag == 150);
  auto duplicate = TxBody::restore<Selected>(
    "150=A\1" "453=1\1" "448=p\1" "452=3\1" "150=A\1"_z, 42, error);
  ZuCheck(!duplicate && error.code == ErrorCode::Duplicate && error.tag == 150);
  auto unordered = TxBody::restore<Selected>(
    "150=A\1" "453=1\1" "452=3\1" "448=p\1"_z, 42, error);
  ZuCheck(!unordered && error.code == ErrorCode::Group);
  using BinaryKey = MT<Order, "D", ZuFacet::FIX, Discriminator<150, "A">>;
  auto binary = TxBody::restore<BinaryKey>(
    "11=x\1" "95=6\1" "96=150=A\1\1"_z, 42, error);
  ZuCheck(!binary && error.code == ErrorCode::Required && error.tag == 150);
  using NoCtor = MT<ArchiveOnly, "D", ZuFacet::FIX, Discriminator<150, "A">>;
  ArchiveOnly::constructed = ArchiveOnly::getters = 0;
  auto checked = TxBody::restore<NoCtor>("38=7\1" "150=A\1"_z, 42, error);
  ZuCheck(checked && !error && !ArchiveOnly::constructed && !ArchiveOnly::getters);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(retained);
  ZuTestCall(controls);
  ZuTestCall(jumbo);
  ZuTestCall(failures);
  ZuTestCall(archive);
  ZuTestCall(archiveGroups);
  ZuTestCall(archiveControls);
  ZuTestCall(archiveSelector);
  ZuTestCall(archiveNestedKey);
  return 0;
}
