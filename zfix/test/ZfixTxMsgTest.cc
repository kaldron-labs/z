//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuPtr.hh>
#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZtArray.hh>
#include <zlib/ZtString.hh>
#include <zlib/ZiTx.hh>
#include <zlib/ZfixTxMsg.hh>
#include <zlib/ZfixTxCapture.hh>
#include <zlib/ZfixRxMsg.hh>
#include "ZfixLabel.hh"

#include "ZfixTxImageTest.hh"

using namespace ZuTestUtil;
using namespace Zfix;
using namespace Zfix::Fmt;

namespace Zfix::TxModel_ {
template <typename Facet> struct ScalarOwn<Label, Facet> {
  Label value;
  void capture(const Label &source, Arena &arena, int32_t tag) {
    if (source.value.length() > arena.limits.fieldBytes) {
      arena.fail(Fmt::ErrorCode::Size, tag); return;
    }
    value = source; // Text owns its copy; no borrowed formatter object crosses owners.
  }
  template <typename Props, typename Out> void emit(Out &out, const Arena &) const {
    Fmt::saveScalar<Facet, ZfFieldTC::UDT, Props>(out, value);
  }
  uint64_t memory() const {
    if (!value.value.vallocd()) return 0;
    using Heap = ZtString<ZtStringHeapID<"Zfix.Test">>::VHeap;
    uint64_t bytes = value.value.size() + 1;
    auto i = Heap::cacheI(bytes);
    return i < Heap::NCaches ? Heap::blockSize(i) : bytes;
  }
};
}

struct Words : ZtArray<ZuCSpan, ZtArrayHeapID<"Zfix.Test.Words">> {
  ZuDerive_(Words, (ZtArray<ZuCSpan, ZtArrayHeapID<"Zfix.Test.Words">>));
friend AsMultiple<ZfFieldTC::String> Zfix_Fmt(Words *);
};
struct WordList { Words words; };
ZfStruct(, (WordList, FIX), (words, (FIX::ID<9010>), UDT));
ZfStructImpl(WordList); ZfStructImpl(WordList, FIX);

struct AuthExtra { ZuBSpan raw; ZuCSpan user; };
ZfStruct(, (AuthExtra, FIX), (raw, (FIX::ID<95>, FIX::Binary<96>), Bytes),
  (user, (FIX::ID<553>, Required), String));
ZfStructImpl(AuthExtra); ZfStructImpl(AuthExtra, FIX);

struct ScalarUDT { Label label; };
ZfStruct(, (ScalarUDT, FIX), (label, (FIX::ID<55>), UDT));
ZfStructImpl(ScalarUDT); ZfStructImpl(ScalarUDT, FIX);

struct Order {
  ZuCSpan	id;
  ZuCSpan	data;
};

ZfStruct(, (Order),
  (id, (FIX::ID<11>), String),
  (data, (FIX::ID<58>), String));
using OrderFIX = ZuTypeConcat<SessionFields<Order>, ZfFields_Order>;
OrderFIX ZuFields_(Order *, ZuFacet::FIX *);
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

ZfStruct(, (ArchiveOnly),
  ((value, RdFn), (FIX::ID<38>, Required, Keys<0>), Int32));
using ArchiveOnlyFIX = ZuTypeConcat<SessionFields<ArchiveOnly>, ZfFields_ArchiveOnly>;
ArchiveOnlyFIX ZuFields_(ArchiveOnly *, ZuFacet::FIX *);
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

ZfStruct(, (GroupOrder), (parties, (FIX::ID<453>), UDT));
using GroupOrderFIX = ZuTypeConcat<SessionFields<GroupOrder>, ZfFields_GroupOrder>;
GroupOrderFIX ZuFields_(GroupOrder *, ZuFacet::FIX *);
ZfStructImpl(GroupOrder);
ZfStructImpl(GroupOrder, FIX);
using GroupMsg = MT<GroupOrder, "D">;

struct GroupAuth { ZuBSpan raw; ZuCSpan user; Parties parties; };
ZfStruct(, (GroupAuth, FIX),
  (raw, (FIX::ID<95>, FIX::Binary<96>, Required), Bytes),
  (user, (FIX::ID<553>, Required), String),
  (parties, (FIX::ID<453>, Required), UDT));
ZfStructImpl(GroupAuth); ZfStructImpl(GroupAuth, FIX);

struct RequiredDecl { unsigned value; };
ZfStruct(, (RequiredDecl, FIX), (value, (FIX::ID<9001>, Required), UInt32));
ZfStructImpl(RequiredDecl); ZfStructImpl(RequiredDecl, FIX);
struct OptionalDecl { unsigned value; };
ZfStruct(, (OptionalDecl, FIX), (value, (FIX::ID<9001>, FIX::Opt), UInt32));
ZfStructImpl(OptionalDecl); ZfStructImpl(OptionalDecl, FIX);

struct BinarySigner {
  enum { SignMsg = 1 };
  template <typename S, typename Message>
  void signMsg(S &out, const TxBody *, TxContext, const Message &) const {
    out.field(93); out << 5; out.end();
    out.field(89); out << ZuCSpan{"a\1=\0b", 5}; out.end();
  }
};

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

ZfStruct(, (WithComponent), (component, , UDT));
using WithComponentFIX = ZuTypeConcat<SessionFields<WithComponent>, ZfFields_WithComponent>;
WithComponentFIX ZuFields_(WithComponent *, ZuFacet::FIX *);
ZfStructImpl(WithComponent);
ZfStructImpl(WithComponent, FIX);
using ComponentMsg = MT<WithComponent, "D">;

struct InterleavedSession {
  ZuCSpan first, sender, middle, target, last;
  uint64_t seqNo = 0;
  ZuDateTime sendingTime;
};
ZfStruct(, (InterleavedSession, FIX),
  (first, (FIX::ID<11>, Required), String),
  (sender, (FIX::ID<49>, Required), String),
  (middle, (FIX::ID<58>, Required), String),
  (target, (FIX::ID<56>, Required), String),
  (last, (FIX::ID<55>, Required), String),
  (seqNo, (FIX::ID<34>, Required), UInt64),
  (sendingTime, (FIX::ID<52>, Required), DateTime));
ZfStructImpl(InterleavedSession); ZfStructImpl(InterleavedSession, FIX);
using InterleavedMsg = MT<InterleavedSession, "D">;

static TxConfig config() {
  SessionConfig session;
  session.identity.version = Version::V4_4;
  session.identity.local = "CLIENT"; session.identity.peer = "SERVER";
  return txConfig(session);
}
static TxContext context() {
  return {.seqNo = 7,
    .sendingTime = ZuDateTime{ZuDateTimeScan::FIX{}, "20261027-12:34:56"}};
}
static RxMsg metadata(const FrameView &frame) {
  RxMsg input; input.frame = frame;
  readSession(input);
  return input;
}

static void retained()
{
  ZuTestScope(retained);
  Error error;
  auto payload = TxBody::encode<OrderMsg>(Order{"abc", ZuCSpan{"ABCD"_z}}, 91, error);
  ZuCheck(payload && !error && payload->id() == 91 && payload->msgType() == "D");
  constexpr auto Original = "11=abc\1" "58=ABCD\1"_z;
  ZuCheck(payload->body() == Original && !payload->administrative());
  auto h = context();
  auto wire = testFrame(*payload, config(), h, error);
  ZuCheck(wire && !error);
  auto parsed = frame(ZuCSpan{wire->cspan()});
  MessageHandler<Order> decoded{parsed.view};
  auto order = decoded.ctor();
  ZuCheck(decoded.valid() && order.id == "abc" && order.data == ZuCSpan{"ABCD"_z});
  ZuCheck(metadata(parsed.view).seqNo == 7 && metadata(parsed.view).sendingTime == h.sendingTime);

  // Transport mutation and partial-send cursor changes cannot touch history.
  memset(wire->data(), 'X', wire->length);
  wire->advance(9);
  ZuCheck(payload->body() == Original);
  h.possDup = true;
  h.origSendingTime = h.sendingTime;
  h.sendingTime = ZuDateTime{ZuDateTimeScan::FIX{}, "20261027-13:00:00"};
  auto replay = testFrame(*payload, config(), h, error);
  ZuCheck(replay && !error);
  MessageHandler<Order> restored{frame(ZuCSpan{replay->cspan()}).view};
  auto got = restored.ctor();
  ZuCheck(restored.valid() && got.id == "abc" && got.data == ZuCSpan{"ABCD"_z});
  ZuCheck(metadata(frame(replay->cspan()).view).seqNo == 7 && metadata(frame(replay->cspan()).view).possDup &&
    metadata(frame(replay->cspan()).view).origSendingTime == h.origSendingTime &&
    metadata(frame(replay->cspan()).view).sendingTime == h.sendingTime);
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
  auto encoded = testFrame(*heartbeat, config(), context(), error);
  ZuCheck(encoded && !error);
  Session::Msg got;
  auto input = metadata(frame(encoded->cspan()).view);

  ZuCheck(!Session::read(frame(ZuCSpan{encoded->cspan()}).view, got));
  ZuCheck(got.type() == Session::Msg::Index<Session::HeartBt>{} && input.seqNo == 7);
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
  for (unsigned i = 0; i < n; ++i) bytes[i] = 'A' + i % 26;
  Error error;
  Order order{"large", bytes};
  auto body = TxBody::encode<OrderMsg>(order, 92, error);
  ZuCheck(body && !error && body->body().length() > n && body->size > ZiIOBuf_DefltSize);
  if (!body) return;
  auto output = testFrame(*body, config(), context(), error);
  ZuCheck(output && !error && output->size > ZiIOBuf_DefltSize);
  MessageHandler<Order> decoder{frame(ZuCSpan{output->cspan()}).view};
  auto restored = decoder.ctor();
  ZuCheck(decoder.valid() && restored.data == order.data && restored.id == order.id);
  Limits limits;
  limits.frameBytes = output->length;
  auto exact = testFrame(*body, config(), context(), error, limits);
  ZuCheck(exact && !error && exact->length == output->length);
  --limits.frameBytes;
  auto shortOut = testFrame(*body, config(), context(), error, limits);
  ZuCheck(!shortOut && error.code == ErrorCode::Size);
  limits = {};
  limits.fieldBytes = n - 1;
  auto tooLong = TxBody::encode<OrderMsg>(order, 93, error, limits);
  ZuCheck(!tooLong && error.code == ErrorCode::Size && error.tag == 58);
}

static void failures()
{
  ZuTestScope(failures);
  Error error;
  auto bad = TxBody::encode<OrderMsg>(Order{"bad\1value"_z, {}}, 1, error);
  ZuCheck(bad && !error);
  auto valid = TxBody::encode<OrderMsg>(Order{"abc", {}}, 1, error);
  ZuCheck(valid && !error);
  auto h = context();
  h.seqNo = 0;
  auto noSequence = testFrame(*valid, config(), h, error);
  ZuCheck(noSequence && !error);
  h = context();
  h.possDup = true;
  auto noOriginal = testFrame(*valid, config(), h, error);
  ZuCheck(noOriginal && !error);
  h.origSendingTime = ZuDateTime{ZuDateTimeScan::FIX{}, "20261027-13:00:00"};
  auto futureOriginal = testFrame(*valid, config(), h, error);
  ZuCheck(futureOriginal && !error);
  // Long declared versions must grow the envelope headroom before body output.
  ZtString<ZtStringHeapID<"Zfix.TxMsgTest">> version;
  version.ensure(ZiIOBuf_DefltSize<<1);
  for (unsigned i = 0; i < (ZiIOBuf_DefltSize<<1); ++i) version << 'V';
  h = context(); auto longConfig = config(); longConfig.beginString = version;
  auto longVersion = testFrame(*valid, longConfig, h, error);
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
  constexpr auto Original = "38=0007\1" "9000=opaque\1"_z;
  auto body = TxBody::restore<ArchiveMsg>(Original, 83, error);
  ZuCheck(body && !error && body->id() == 83 && body->body() == Original);
  ZuCheck(!ArchiveOnly::getters && !ArchiveOnly::constructed);
  auto h = context(); h.possDup = true; h.origSendingTime = h.sendingTime;
  auto wire = testFrame(*body, config(), h, error);
  ZuCheck(wire && !error && ZuCSpan{wire->cspan()}.find(Original) >= 0);
  auto bad = TxBody::restore<ArchiveMsg>("38=bad\1"_z, 84, error);
  ZuCheck(bad && !error);
  bad = TxBody::restore<ArchiveMsg>("9000=opaque\1"_z, 84, error);
  ZuCheck(!bad && error.code == ErrorCode::Required && error.tag == 38);
  bad = TxBody::restore<ArchiveMsg>("38=7\1" "49=INJECT\1"_z, 84, error);
  ZuCheck(!bad && error.code == ErrorCode::Reserved && error.tag == 49);
  ZuCheck(!ArchiveOnly::getters && !ArchiveOnly::constructed);

  ZtString<ZtStringHeapID<"Zfix.ArchiveInput">> borrowed = Original;
  auto owned = TxBody::restore<ArchiveMsg>(borrowed, 86, error);
  borrowed.clear(); borrowed << "reused storage";
  ZuCheck(owned && !error && owned->body() == Original);

  constexpr auto Text = "11=abc\1" "58=ABCD\1"_z;
  auto data = TxBody::restore<OrderMsg>(Text, 85, error);
  ZuCheck(data && !error && data->body() == Text);
  auto output = testFrame(*data, config(), context(), error);
  MessageHandler<Order> decoded{frame(ZuCSpan{output->cspan()}).view};
  auto got = decoded.ctor();
  ZuCheck(decoded.valid() && got.id == "abc" && got.data == ZuCSpan{"ABCD"_z});
  Limits limits;
  limits.frameBytes = Text.length();
  auto oversized = TxBody::restore<OrderMsg>(Text, 85, error, limits);
  ZuCheck(!oversized && error.code == ErrorCode::Size);
  limits = {};
  limits.parseBytes = 0;
  auto zeroIndex = TxBody::restore<OrderMsg>(Text, 85, error, limits);
  ZuCheck(!zeroIndex && error.code == ErrorCode::Size);
  limits.parseBytes = Schema<Order, ZuFacet::FIX>::Leaves::N * sizeof(Position) - 1;
  auto shortIndex = TxBody::restore<OrderMsg>(Text, 85, error, limits);
  ZuCheck(!shortIndex && error.code == ErrorCode::Size);
  ++limits.parseBytes;
  auto exactIndex = TxBody::restore<OrderMsg>(Text, 85, error, limits);
  ZuCheck(exactIndex && !error && exactIndex->body() == Text);
}

static void archiveGroups()
{
  ZuTestScope(archiveGroups);
  Error error;
  constexpr auto Original = "453=2\1" "448=FIRST\1" "452=01\1" "448=SECOND\1" "452=02\1"_z;
  auto body = TxBody::restore<GroupMsg>(Original, 11, error);
  ZuCheck(body && !error && body->body() == Original);
  auto incomplete = TxBody::restore<GroupMsg>("453=1\1" "448=FIRST\1"_z, 11, error);
  ZuCheck(!incomplete && error.code == ErrorCode::Required && error.tag == 452);
  auto malformed = TxBody::restore<GroupMsg>("453=1\1" "448=FIRST\1" "452=x\1"_z, 11, error);
  ZuCheck(malformed && !error);
  auto absent = TxBody::restore<ComponentMsg>({}, 12, error);
  ZuCheck(absent && !error && !absent->body().length());
  auto present = TxBody::restore<ComponentMsg>("21=7\1"_z, 12, error);
  ZuCheck(present && !error);
}

static void archiveControls()
{
  ZuTestScope(archiveControls);
  Error error;
  constexpr auto Original = "45=0007\1" "373=05\1" "58=stored\1"_z;
  auto reject = TxBody::restoreControl("3", Original, 0, error);
  ZuCheck(reject && !error && reject->body() == Original && reject->administrative());
  auto hb = TxBody::restoreControl("0", {}, 0, error);
  ZuCheck(hb && !error && !hb->body().length() && hb->administrative());
  auto bad = TxBody::restoreControl("3", "45=0\1"_z, 0, error);
  ZuCheck(bad && !error);
  bad = TxBody::restoreControl("1", {}, 0, error);
  ZuCheck(!bad && error.code == ErrorCode::Required && error.tag == 112);
  bad = TxBody::restoreControl("D", "11=abc\1"_z, 0, error);
  ZuCheck(!bad && error.code == ErrorCode::Unresolved && error.tag == 35);
  using RejectMsg = MT<Session::Reject, "3">;
  bad = TxBody::restore<RejectMsg>(Original, 0, error);
  ZuCheck(!bad && error.code == ErrorCode::Reserved && error.tag == 35);
}

// An unmodeled root selector follows a group with distinct field tags.
struct KeyParty {
  Party		party;
  ZuCSpan	kind;
};

ZfStruct(, (KeyParty, FIX), (party, , UDT),
  (kind, (FIX::ID<9003>, Required), String));
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

static void archiveGroupKey()
{
  ZuTestScope(archiveGroupKey);
  using Selected = MT<KeyOrder, "D", ZuFacet::FIX, Discrim<150, "A">>;
  KeyOrder order;
  order.parties.push(KeyParty{{"p", 3}, "A"});
  Error error;
  auto encoded = TxBody::encode<Selected>(order, 42, error);
  constexpr auto Bytes = "453=1\1" "448=p\1" "452=3\1" "9003=A\1" "150=A\1"_z;
  ZuCheck(encoded && !error && encoded->body() == Bytes);
  auto restored = TxBody::restore<Selected>(Bytes, 42, error);
  ZuCheck(restored && encoded && !error &&
    restored->body() == encoded->body() && restored->id() == 42);
  auto missing = TxBody::restore<Selected>(
    "453=1\1" "448=p\1" "452=3\1" "9003=A\1"_z, 42, error);
  ZuCheck(!missing && error.code == ErrorCode::Required && error.tag == 150);
  auto wrong = TxBody::restore<Selected>(
    "453=1\1" "448=p\1" "452=3\1" "9003=A\1" "150=B\1"_z, 42, error);
  ZuCheck(!wrong && error.code == ErrorCode::Unresolved && error.tag == 150);
}

static void archiveSelector()
{
  ZuTestScope(archiveSelector);
  using Selected = MT<GroupOrder, "D", ZuFacet::FIX, Discrim<150, "A">>;
  Error error;
  constexpr auto Bytes = "453=1\1" "448=p\1" "452=3\1" "150=A\1"_z;
  auto saved = TxBody::restore<Selected>(Bytes, 42, error);
  ZuCheck(saved && !error && saved->body() == Bytes);
  auto missing = TxBody::restore<Selected>("453=1\1" "448=p\1" "452=3\1"_z, 42, error);
  ZuCheck(!missing && error.code == ErrorCode::Required && error.tag == 150);
  auto duplicate = TxBody::restore<Selected>(
    "150=A\1" "453=1\1" "448=p\1" "452=3\1" "150=A\1"_z, 42, error);
  ZuCheck(!duplicate && error.code == ErrorCode::Duplicate && error.tag == 150);
  auto unordered = TxBody::restore<Selected>(
    "150=A\1" "453=1\1" "452=3\1" "448=p\1"_z, 42, error);
  ZuCheck(!unordered && error.code == ErrorCode::Group);
  using NoCtor = MT<ArchiveOnly, "D", ZuFacet::FIX, Discrim<150, "A">>;
  ArchiveOnly::constructed = ArchiveOnly::getters = 0;
  auto checked = TxBody::restore<NoCtor>("38=7\1" "150=A\1"_z, 42, error);
  ZuCheck(checked && !error && !ArchiveOnly::constructed && !ArchiveOnly::getters);
}

static void destinationFrame()
{
  ZuTestScopeRT(destinationFrame);
  using Alloc = Zi::TxTCPBufAllocator<ZiTxBuf, 256, 8192,
    ZuStringT<"Zfix.FinalFrameTest">>;
  ZmRef<Zi::IOBufAllocator> allocator = new Alloc;
  for (auto length : {4U, 1024U}) {
    ZtString text;
    text.length(length);
    memset(text.data(), 'x', length);
    Error error;
    auto body = TxBody::capture<OrderMsg>(Order{"abc", text}, 91, error);
    ZuCheckRT(body && !error);
    auto destination = allocator->alloc(nullptr, 0);
    auto original = destination.ptr();
    destination->skip = 16;
    memset(destination->data_(), 0xa5, destination->skip);
    auto output = testFrame(*body, config(), context(), error,
      Limits{}, NoSign{}, destination, 24);
    ZuCheckRT(output && !error && output.ptr() == original);
    if (!output) continue;
    ZuCheckRT(output->skip >= 16);
    ZuCheckRT(output->skip + output->length + 24 <= output->size);
    if (!output->jumbo())
      for (unsigned i = 0; i < 16; ++i) ZuCheckRT(output->data_()[i] == 0xa5);
    auto parsed = frame(ZuCSpan{output->cspan()});
    ZuCheckRT(parsed.state == FrameState::Complete);
    MessageHandler<Order> decoded{parsed.view};
    auto object = decoded.ctor();
    ZuCheckRT(decoded.valid() && object.data == text);
    if (length == 1024) ZuCheckRT(output->jumbo() && !output->uring());
  }
}

static void semanticModel()
{
  ZuTestScope(semanticModel);
  Error error;
  ZtString<> id{"owned"}, text{"variable input"};
  auto model = TxBody::capture<OrderMsg>(Order{id, text}, 91, error);
  ZuCheck(model && !error && model->semantic() && model->id() == 91);
  if (!model) return;
  id = "changed"; text = "overwritten";
  using Destination = Zi::IOBufAlloc<ZiTxBuf, ZiIOBuf_DefltSize,
    ZiIOBuf_DefltMaxSize, ZuStringT<"Zfix.Model.Wire">>;
  ZmRef<ZiIOBuf> destination = new Destination{nullptr, 0};
  auto identity = destination.ptr();
  auto output = testFrame(*model, config(), context(), error, Limits{}, NoSign{}, ZuMv(destination));
  ZuCheck(output && !error && output.ptr() == identity);
  if (!output) return;
  MessageHandler<Order> parsed{frame(output->cspan()).view};
  auto got = parsed.ctor();
  ZuCheck(parsed.valid() && got.id == "owned" && got.data == "variable input");
  auto archive = model->archive(error);
  ZuCheck(archive && !error && ZuCSpan{archive.cspan()} == "11=owned\1" "58=variable input\1");
  auto replayContext = context(); replayContext.replay = replayContext.possDup = true;
  replayContext.origSendingTime = replayContext.sendingTime;
  memset(output->data(), 'X', output->length);
  auto replay = testFrame(*model, config(), replayContext, error);
  ZuCheck(replay && !error);
  if (replay) {
    MessageHandler<Order> parsed{frame(replay->cspan()).view}; auto got = parsed.ctor();
    ZuCheck(parsed.valid() && got.id == "owned" && got.data == "variable input");
  }
  LabelFormat::saves = 0;
  ScalarUDT custom{Label{"valid"}};
  auto opaque = TxBody::capture<MT<ScalarUDT, "D">>(custom, 93, error);
  custom.label.value = "changed after capture";
  ZuCheck(opaque && !error && !LabelFormat::saves);
  if (opaque) {
    auto wire = testFrame(*opaque, config(), context(), error);
    ZuCheck(wire && !error && LabelFormat::saves == 1);
    if (wire) ZuCheck(frame(wire->cspan()).view.content == "55=valid\1");
  }
  WordList list; list.words.push("first"); list.words.push("second");
  auto multiple = TxBody::capture<MT<WordList, "D">>(list, 94, error);
  list.words.length(0);
  ZuCheck(multiple && !error);
  if (multiple) {
    auto wire = testFrame(*multiple, config(), context(), error);
    ZuCheck(wire && !error && frame(wire->cspan()).view.content == "9010=first second\1");
  }

  Session::Msg control; control.p<Session::Logon>(Session::Logon{});
  auto base = TxBody::captureControl(control, error);
  ZtString<> user{"original"};
  auto extended = TxBody::extend<MT<Session::Logon, "A">>(base, AuthExtra{ZuBSpan{"binary\0value"_z}, user}, error);
  user = "changed";
  ZuCheck(extended && !error && extended->memory() > base->memory());
  if (extended) {
    auto wire = testFrame(*extended, config(), context(), error);
    ZuCheck(wire && !error);
    if (wire) {
      Indexed<AuthExtra, ZuFacet::FIX> indexed{frame(wire->cspan()).view.content};
      auto auth = indexed.ctor();
      ZuCheck(indexed.valid() && auth.user == "original" &&
	ZuCSpan{auth.raw} == "binary\0value"_z);
    }
  }

  GroupOrder groups; groups.parties.push(Party{"first", 1}); groups.parties.push(Party{"second", 2});
  auto nested = TxBody::capture<GroupMsg>(groups, 92, error);
  groups.parties.length(0);
  ZuCheck(nested && !error);
  if (!nested) return;
  auto grouped = testFrame(*nested, config(), context(), error);
  ZuCheck(grouped && !error);
  if (grouped) {
    MessageHandler<GroupOrder> parsed{frame(grouped->cspan()).view}; auto got = parsed.ctor();
    ZuCheck(parsed.valid() && got.parties.length() == 2 &&
      got.parties[0].id == "first" && got.parties[1].id == "second" && got.parties[1].role == 2);
  }
}

static void signedExtra()
{
  ZuTestScope(signedExtra);
  using Entry = MT<Session::Logon, "A">;
  Session::Msg control; control.p<Session::Logon>(Session::Logon{});
  Fmt::Error error;
  auto base = TxBody::captureControl(control, error);
  static constexpr auto Raw = "raw\1=\0auth"_z;
  GroupAuth auth{ZuBSpan{Raw}, "user"};
  auth.parties.push(Party{"first", 1}); auth.parties.push(Party{"second", 2});
  auto body = TxBody::extend<Entry>(base, auth, error);
  auth.parties.length(0);
  ZuCheck(body && !error);
  if (!body) return;
  auto wire = testFrame(*body, config(), context(), error, Limits{}, BinarySigner{});
  ZuCheck(wire && !error);
  if (!wire) return;
  auto parsed = frame(wire->cspan());
  using Joined = TxJoined<Entry, GroupAuth, ZuFacet::FIX>;
  Fmt::FieldPos pos;
  auto content = parsed.view.content;
  ZuCheck((!Fmt::inspect<Joined>(content, 89, pos) && pos.offset != UINT_MAX &&
    ZuCSpan{content.data() + pos.offset, pos.length} == ZuCSpan{"a\1=\0b", 5}));
  ZuCheck((!Fmt::inspect<Joined>(content, 96, pos) && pos.offset != UINT_MAX &&
    ZuCSpan{content.data() + pos.offset, pos.length} == "raw\1=\0auth"_z));
  ZuCheck((!Fmt::inspect<Joined>(content, 453, pos) && pos.offset != UINT_MAX &&
    ZuCSpan{content.data() + pos.offset, pos.length} == "2"));
  auto limits = Limits{}; limits.groupEntries = 1;
  auto limited = readParts<Joined, ZuFacet::FIX, TxFrame_::FrameField>(SpanView{content}, limits);
  ZuCheck(limited.code == ErrorCode::Size && limited.tag == 453);
  auto child = readParts<Joined, ZuFacet::FIX, TxFrame_::FrameField>(SpanView{
    "95=1\1" "96=x\1" "553=u\1" "453=1\1" "452=1\1"});
  ZuCheck(child.code == ErrorCode::Required && child.tag == 448);
  using BaseRequired = TxJoined<MT<RequiredDecl, "D">, OptionalDecl, ZuFacet::FIX>;
  auto baseRequired = readParts<BaseRequired, ZuFacet::FIX, TxFrame_::FrameField>(
    SpanView{ZuCSpan{}});
  ZuCheck(baseRequired.code == ErrorCode::Required && baseRequired.tag == 9001);
  using ExtraRequired = TxJoined<MT<OptionalDecl, "D">, RequiredDecl, ZuFacet::FIX>;
  auto extraRequired = readParts<ExtraRequired, ZuFacet::FIX, TxFrame_::FrameField>(
    SpanView{ZuCSpan{}});
  ZuCheck(extraRequired.code == ErrorCode::Required && extraRequired.tag == 9001);
  auto wrong = TxBody::extend<OrderMsg>(base, auth, error);
  ZuCheck(!wrong && error.code == ErrorCode::Unresolved && error.tag == 35);
}

static void retainedImage()
{
  ZuTestScope(retainedImage);
  Error error;
  auto body = TxBody::encode<OrderMsg>(Order{"original", "payload"}, 95, error);
  auto image = txFrame(*body, config(), context(), error);
  ZuCheck(image && !error);
  if (!image) return;
  ZuCheck(image.metadata.ptr() != body.ptr());
  auto expected = ZtString<>{ZuCSpan{testBytes(image)->cspan()}};
  ZiTxContext first, second;
  first.txRange(image.metadata, image.origin, image.length());
  first.txAdvance(image.length());
  auto replay = txFrame(*body, config(), context(), error);
  ZuCheck(replay && !error &&
    replay.metadata.ptr() != image.metadata.ptr() && replay.metadata.ptr() != body.ptr());
  if (replay) {
    second.txRange(replay.metadata, replay.origin, replay.length());
    ZuCheck(first.txRemaining == 0 && second.txRemaining == replay.length());
    ZuCheck(testBytes(replay)->cspan() == expected);
  }
  ZuCheck(body->msgType() == "D" && body->body() == "11=original\1" "58=payload\1");
  auto archive = body->archive(error);
  ZuCheck(archive && !error && ZuCSpan{archive.cspan()} == body->body());
}

static void repeatedSessionFlushes()
{
  ZuTestScopeRT(repeatedSessionFlushes);
  Error error;
  constexpr auto Archived = "11=first\1" "9000=between\1" "58=middle\1"
    "55=last\1" "9001=tail\1"_z;
  InterleavedSession object{"first", "ignored-sender", "middle", "ignored-target", "last"};
  TxBody::Ref bodies[] = {
    TxBody::encode<InterleavedMsg>(object, 101, error),
    TxBody::capture<InterleavedMsg>(object, 102, error),
    TxBody::restore<InterleavedMsg>(Archived, 103, error),
    TxBody::restoreRaw<InterleavedMsg>(Archived, 104, false, error)
  };
  for (unsigned i = 0; i < 4; ++i) {
    auto &body = bodies[i];
    ZuCheckRT(body && !error, (continue));
    auto archive = body->archive(error);
    ZuCheckRT(archive && !error, (continue));
    ZtString<> immutable{ZuCSpan{archive.cspan()}};
    auto one = txFrame(*body, config(), context(), error);
    ZuCheckRT(one && !error && one.metadata.ptr() != body.ptr(), (continue));
    ZtString<> original{one.spanAt(0, one.length())};
    auto changed = context(); changed.seqNo = 17;
    changed.sendingTime = ZuDateTime{ZuDateTimeScan::FIX{}, "20261027-13:00:00"};
    auto destination = config(); destination.identity.local = "SECOND";
    auto two = txFrame(*body, destination, changed, error);
    ZuCheckRT(two && !error && two.metadata != one.metadata &&
      two.metadata.ptr() != body.ptr(), (continue));
    ZuCheckRT(one.spanAt(0, one.length()) == original);
    for (unsigned attempt = 0; attempt < 2; ++attempt) {
      const auto &image = attempt ? two : one;
      auto wire = image.spanAt(0, image.length());
      auto parsed = frame(wire);
      ZuCheckRT(parsed.state == FrameState::Complete && parsed.consumed == image.length());
      unsigned counts[9] = {}, offset = 0;
      constexpr int Tags[] = {11, 49, 58, 56, 55, 34, 52, 9000, 9001};
      bool scanned = true;
      while (offset < parsed.view.content.length()) {
        Token token;
        if (scan(parsed.view.content, offset, token)) { scanned = false; break; }
        for (unsigned j = 0; j < 9; ++j) if (token.tag == Tags[j]) ++counts[j];
        offset = token.next;
      }
      ZuCheckRT(scanned && counts[0] == 1 && counts[1] == 1 && counts[2] == 1 &&
        counts[3] == 1 && counts[4] == 1 && counts[5] == 1 && counts[6] == 1);
      ZuCheckRT(counts[7] == unsigned(i >= 2) && counts[8] == unsigned(i >= 2));
      ZuCheckRT((attempt ? wire.find("49=SECOND\1"_z) : wire.find("49=CLIENT\1"_z)) >= 0 &&
        (attempt ? wire.find("34=17\1"_z) : wire.find("34=7\1"_z)) >= 0 &&
        wire.find("11=first\1"_z) >= 0 && wire.find("58=middle\1"_z) >= 0 &&
        wire.find("55=last\1"_z) >= 0);
    }
    ZiTxContext progress;
    progress.txRange(one.metadata, one.origin, one.length());
    progress.txAdvance(one.length()/2);
    ZuCheckRT(two.spanAt(0, two.length()).find("34=17\1"_z) >= 0 &&
      one.spanAt(0, one.length()) == original);
    auto unchanged = body->archive(error);
    ZuCheckRT(unchanged && !error && ZuCSpan{unchanged.cspan()} == immutable);
  }
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(semanticModel);
  ZuTestCall(signedExtra);
  ZuTestCall(retainedImage);
  ZuTestCall(repeatedSessionFlushes);
  ZuTestCall(retained);
  ZuTestCall(destinationFrame);
  ZuTestCall(controls);
  ZuTestCall(jumbo);
  ZuTestCall(failures);
  ZuTestCall(archive);
  ZuTestCall(archiveGroups);
  ZuTestCall(archiveControls);
  ZuTestCall(archiveSelector);
  ZuTestCall(archiveGroupKey);
  return 0;
}
