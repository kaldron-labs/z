//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Application HMAC policy; the codec imposes no cryptographic algorithm.
#include "ZfixTestAllocator.hh"

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuHex.hh>
#include <zlib/ZuPtr.hh>
#include <zlib/ZmSingleton.hh>
#include <zlib/ZmList.hh>
#include <zlib/ZmFn.hh>
#include <zlib/ZtArray.hh>
#include <zlib/ZtString.hh>
#include <zlib/ZtlsHMAC.hh>
#include <zlib/ZiTx.hh>
#include <zlib/ZfixRaw.hh>
#include <zlib/ZfixTx.hh>

static ZmRef<ZiIOBuf> frameBuf() {
  using Allocator = Zi::IOBufBuiltinAllocator<Zi::IOBufType::App, ZiTxBuf, ZiIOBuf_DefltSize>;
  return ZfixTest::Allocator<Allocator>::get()->allocBuf();
}

using namespace ZuTestUtil;
using namespace Zfix;
using namespace Zfix::Fmt;
using Text = ZtString<ZtStringHeapID<"Zfix.SignTest">>;

struct Signed {
  ZuCSpan id;
  ZuBSpan data, signature;
  uint64_t seqNo = 0;
  bool possDup = false;
  ZuDateTime sendingTime, origSendingTime;
};
ZfStruct(, (Signed),
  (id, (FIX::ID<11>, Required), String),
  (data, (FIX::ID<212>, FIX::Binary<213>, Required), Bytes),
  (signature, ((FIX::Sig<93, 89>), Required), Bytes));
using SignedFIXFields = ZuTypeConcat<SessionFields<Signed>, ZfFields_Signed>;
SignedFIXFields ZuFields_(Signed *, ZuFacet::FIX *);
using Entry = MT<Signed, "D">;
using SigField = ZfField(Signed, signature);

static constexpr auto Payload = "A\1\0=B"_z;
static constexpr auto Body = "11=abc\1" "212=5\1" "213=A\1\0=B\1"_z;
static constexpr auto Covered = "35=D\1" "49=CLIENT\1" "56=SERVER\1" "34=7\1"
  "52=20261027-12:34:56\1" "11=abc\1" "212=5\1" "213=A\1\0=B\1"_z;
// Independently calculated with Python hmac.new(b"secret", Covered, sha256).
static constexpr auto Known =
  "fab44f856331f9c892d2a748799191a98746b67514cb44652676429d7c3684c3"_z;

static TxConfig config()
{
  SessionConfig session;
  session.identity.version = Version::V4_4;
  session.identity.local = "CLIENT"; session.identity.peer = "SERVER";
  return txConfig(ZuMv(session));
}
static TxContext context()
{
  TxContext c{3, 4, 7, false};
  c.sendingTime = {ZuDateTimeScan::FIX{}, "20261027-12:34:56"};
  return c;
}
static void mac(ZuCSpan input, ZuSpan<uint8_t> digest)
{
  Ztls::HMAC<> hmac;
  hmac.start(ZuBSpan{"secret"}); hmac.update(ZuBSpan{input}); hmac.finish(digest);
}
struct HmacSign {
  enum { SignMsg = 1 };
  mutable unsigned calls = 0;
  mutable Text covered;
  mutable const TxBody *body = nullptr;
  mutable TxContext context;

  template <typename S>
  void signMsg(S &out, const TxBody *b, TxContext c, ZuCSpan input) const {
    ++calls; covered = input; body = b; context = c;
    uint8_t digest[Ztls::HMAC<>::Size]; // SHA-256's fixed digest extent
    mac(input, digest);
    saveBinary<SigField>(out, digest);
  }
};
struct Disabled : HmacSign { enum { SignMsg = 0 }; };
struct FailSign : NoSign {
  enum { SignMsg = 1 };
  template <typename S>
  void signMsg(S &out, const TxBody *, TxContext, ZuCSpan) const {
    out.fail(ErrorCode::Output); out << "ignored";
  }
};

// Parsing and signature verification are intentionally independent operations.
static bool verify(const FrameView &frame)
{
  MessageHandler<Signed> input{frame};
  auto object = input.ctor();
  if (!input.valid() || object.signature.length() != Ztls::HMAC<>::Size) return false;
  FieldPos length, signature;
  if (inspect<Signed>(frame.content, 93, length) || length.offset == UINT_MAX ||
      inspect<Signed>(frame.content, 89, signature) || signature.offset == UINT_MAX)
    return false;
  if (signature.offset + signature.length + 1 != frame.content.length()) return false;
  auto first = frame.msgType.data() - 3; // the fixed 35= envelope prefix
  auto last = frame.content.data() + length.offset - 3; // declared 93=
  uint8_t digest[Ztls::HMAC<>::Size];
  mac({first, uint64_t(last - first)}, digest);
  return object.signature == ZuBSpan{digest};
}

static void signing()
{
  ZuTestScope(signing);
  uint8_t digest[Ztls::HMAC<>::Size]; char hex[ZuHex::enclen(sizeof(digest))];
  mac(Covered, digest); ZuHex::encode<false>(hex, digest);
  ZuCheck((ZuCSpan{hex, sizeof(hex)} == Known));
  Error error;
  Signed object{"abc", ZuBSpan{Payload}, ZuBSpan{"stale signature"}};
  auto body = TxBody::encode<Entry>(object, 42, error);
  ZuCheck(body && !error && body->body() == Body);
  ZuCheck(object.signature == ZuBSpan{"stale signature"});
  auto restored = TxBody::restore<Entry>(body->body(), 42, error);
  ZuCheck(restored && !error && restored->body() == body->body());
  HmacSign signer;
  TxContext finalContext = context();
  auto wire = txFrame(frameBuf(), *restored, config(), finalContext, error, {}, signer);
  ZuCheck(wire && !error && signer.calls == 1 && signer.covered == Covered);
  ZuCheck(signer.body == restored.ptr() && signer.context.epoch == 3 &&
    signer.context.connection == 4 && signer.context.seqNo == 7 && !signer.context.replay);
  if (!wire) return;
  auto parsed = frame(wire->cspan());
  ZuCheck(parsed.state == FrameState::Complete && parsed.consumed == wire->length);
  ZuCheck(verify(parsed.view));
  MessageHandler<Signed> input{parsed.view};
  auto received = input.ctor();
  FieldPos pos;
  ZuCheck(!inspect<Signed>(parsed.view.content, 89, pos));
  ZuCheck(input.valid() && received.signature.data() ==
    reinterpret_cast<const uint8_t *>(parsed.view.content.data() + pos.offset));
  IndexedMessage<Signed, ZuFacet::FIX> indexed{parsed.view};
  auto copy = indexed.ctor();
  ZuCheck(indexed.valid() && copy.signature.data() == received.signature.data());
  auto raw = RawOutput::encode(frameBuf(), body, config(), finalContext, error, {}, signer);
  ZuCheck(raw && !error && signer.calls == 2 && verify(frame(raw->frame()->cspan()).view));
  if (!raw) return;
  auto direct = RawOutput::fromFrame(body, finalContext, raw->frame(), error);
  ZuCheck(direct && signer.calls == 2 && direct->frame().ptr() == raw->frame().ptr());

  Text supplied{Body};
  auto scratch = ZtScratch(PrintBuf, wire->length);
  Sink pair{{scratch.data(), unsigned(wire->length)}};
  pair << supplied; saveBinary<SigField>(pair, received.signature);
  error = {};
  auto conflict = TxBody::restore<Entry>(pair.data(), 42, error);
  ZuCheck(!conflict && error.code == ErrorCode::Reserved && error.tag == 93);
  error = {};
  auto unsignedWire = txFrame(frameBuf(), *body, config(), context(), error);
  ZuCheck(!unsignedWire && error.code == ErrorCode::Required && error.tag == 93);
  error = {};
  auto failed = txFrame(frameBuf(), *body, config(), context(), error, {}, FailSign{});
  ZuCheck(!failed && error.code == ErrorCode::Output);
  error = {};
  Limits limits; limits.fieldBytes = 20;
  failed = txFrame(frameBuf(), *body, config(), context(), error, limits, signer);
  ZuCheck(!failed && error.code == ErrorCode::Size && error.tag == 89);

  // A standalone writer invokes the identical hook before 8/9/10 finalization.
  auto storage = ZtScratch(PrintBuf, wire->length);
  FrameWriter standalone{{storage.data(), unsigned(wire->length)}, "FIX.4.4"};
  standalone.body() << Covered;
  auto result = standalone.finish(signer, body.ptr(), finalContext);
  ZuCheck(result && result.data == wire->cspan());
  auto calls = signer.calls;
  auto repeated = standalone.finish(signer, body.ptr(), finalContext);
  ZuCheck(!repeated && signer.calls == calls);
}

static void tampering()
{
  ZuTestScope(tampering);
  uint8_t digest[Ztls::HMAC<>::Size]; mac(Covered, digest);
  // Recompute outer BodyLength/CheckSum, so every altered frame still parses.
  auto check = [](ZuCSpan covered, ZuBSpan signature) {
    constexpr auto Envelope = "8=FIX.4.4\1" "9=4294967295\1" "10=255\1"_z;
    constexpr auto Pair = "93=4294967295\1" "89=\1"_z;
    auto capacity = covered.length() + signature.length() + Envelope.length() + Pair.length();
    auto bytes = ZtScratch(PrintBuf, capacity);
    FrameWriter writer{{bytes.data(), unsigned(capacity)}, "FIX.4.4"};
    writer.body() << covered; saveBinary<SigField>(writer.body(), signature);
    auto output = writer.finish();
    if (!output) return false;
    auto parsed = frame(output.data);
    return parsed.state == FrameState::Complete && !verify(parsed.view);
  };
  Text altered{Covered}; altered[altered.length() - 2] ^= 1;
  ZuCheck(check(altered, digest)); // binary payload
  altered = Covered; altered[unsigned(altered.find("CLIENT"))] = 'X';
  ZuCheck(check(altered, digest)); // covered peer identity
  digest[0] ^= 1;
  ZuCheck(check(Covered, digest)); // signature
  ZuCheck(check(Covered, ZuBSpan{}));
  ZuCheck((check(Covered, ZuBSpan{digest, sizeof(digest) - 1})));
}

struct OptionalSigned {
  ZuCSpan id;
  ZuBSpan signature;
};
ZfStruct(, (OptionalSigned),
  (id, (FIX::ID<11>), String),
  (signature, ((FIX::Sig<6100, 6090>)), Bytes));
using OptionalFIXFields = ZuTypeConcat<SessionFields<OptionalSigned>, ZfFields_OptionalSigned>;
OptionalFIXFields ZuFields_(OptionalSigned *, ZuFacet::FIX *);
using OptEntry = MT<OptionalSigned, "X">;
using OptSig = ZfField(OptionalSigned, signature);
struct VariableSign {
  enum { SignMsg = 1 };
  ZuBSpan value;
  template <typename S>
  void signMsg(S &out, const TxBody *, TxContext, ZuCSpan) const { saveBinary<OptSig>(out, value); }
};
static void policies()
{
  ZuTestScope(policies);
  Error error;
  OptionalSigned object{"abc", ZuBSpan{"stale"}};
  auto body = TxBody::encode<OptEntry>(object, 1, error);
  ZuCheck(body && body->body() == "11=abc\1");
  Disabled disabled;
  auto wire = txFrame(frameBuf(), *body, config(), context(), error, {}, disabled);
  ZuCheck(wire && !error && !disabled.calls && object.signature == ZuBSpan{"stale"});
  MessageHandler<OptionalSigned> input{frame(wire->cspan()).view};
  auto received = input.ctor();
  ZuCheck(input.valid() && !received.signature.length());
  for (auto payload : {ZuCSpan{""_z}, ZuCSpan{"\0\1\xff"_z},
      ZuCSpan{"variable signature bytes"_z}}) {
    VariableSign signer{ZuBSpan{payload}};
    wire = txFrame(frameBuf(), *body, config(), context(), error, {}, signer);
    ZuCheck(wire && !error);
    if (!wire) continue;
    auto parsed = frame(wire->cspan());
    MessageHandler<OptionalSigned> signedInput{parsed.view};
    auto value = signedInput.ctor();
    ZuCheck(signedInput.valid() && value.signature == ZuBSpan{payload});
    FieldPos pos;
    ZuCheck(!inspect<OptionalSigned>(parsed.view.content, 6090, pos));
    ZuCheck(value.signature.data() == reinterpret_cast<const uint8_t *>(
      parsed.view.content.data() + pos.offset));
  }
  // A signature larger than the initial pooled output forces sink relocation.
  ZtArray<uint8_t, ZtArrayHeapID<"Zfix.SignGrowth">> large;
  large.length(Zi::IOBufBuiltinAllocator<Zi::IOBufType::App, ZiIOBuf, ZiIOBuf_DefltSize,
    ZiIOBuf_Align, ZiIOBuf_HeapID, ZiIOBuf_DefltVHMaxSize>::Size + 1);
  memset(large.data(), 0x01, large.length());
  VariableSign signer{ZuBSpan{large}};
  wire = txFrame(frameBuf(), *body, config(), context(), error, {}, signer);
  ZuCheck(wire && !error);
  if (wire) {
    auto parsed = frame(wire->cspan());
    MessageHandler<OptionalSigned> input{parsed.view};
    auto value = input.ctor();
    FieldPos pos;
    ZuCheck(input.valid() && value.signature == ZuBSpan{large});
    ZuCheck(!inspect<OptionalSigned>(parsed.view.content, 6090, pos));
    ZuCheck(value.signature.data() == reinterpret_cast<const uint8_t *>(
      parsed.view.content.data() + pos.offset));
    Limits limit; limit.frameBytes = wire->length - 1;
    error = {};
    ZuCheck(!txFrame(frameBuf(), *body, config(), context(), error, limit, signer)
      && bool(error));
  }
}

template <typename T> using Values = ZtArray<T, ZtArrayHeapID<"Zfix.SignEngine">>;
struct Task {
  unsigned kind;
  uint64_t ticket;
};
struct Published {
  TxBody::Ref body;
  TxContext context;
  ZmRef<ZiIOBuf> wire;
  ZuDateTime original;
};
struct Engine : Zfix::Tx<Engine>, HmacSign {
  enum { SignMsg = 1 };
  Values<Task> tasks;
  Values<Published> output;
  Values<TxHistory> history;
  unsigned task = 0;
  ZuDateTime now{ZuDateTimeScan::FIX{}, "20261027-12:34:56"};
  bool fail = false;
  Error error;

  static SessionConfig config() {
    SessionConfig c;
    c.identity.local = "CLIENT"; c.identity.peer = "SERVER";
    c.identity.version = Version::V4_4;
    return c;
  }
  Engine() : Zfix::Tx<Engine>{config()} { }
  template <typename S>
  void signMsg(S &out, const TxBody *body, TxContext c, ZuCSpan message) const {
    HmacSign::signMsg(out, body, c, message);
    if (fail) out.fail(ErrorCode::Output);
  }
  void txPost(unsigned kind, uint64_t ticket) { tasks.push(Task{kind, ticket}); }
  ZuDateTime utcTx() { return now; }
  unsigned loadTx(TxBody::Ref, TxContext, uint64_t) { return TxLoad::Accepted; }
  bool unloadTx(TxBody::Ref, TxContext, uint64_t) { return true; }
  ZmRef<ZiIOBuf> allocTx() { return frameBuf(); }

  bool frameTx(TxBody::Ref body, TxContext c, ZmRef<ZiIOBuf> wire, ZuDateTime original) {
    if (!c.replay) history.push(TxHistory{body, original, c.epoch, c.seqNo});
    output.push(Published{ZuMv(body), c, ZuMv(wire), original});
    return true;
  }
  TxReload historyTx(TxContext c, uint64_t, uint64_t) {
    for (const auto &h : history) if (h.seqNo == c.seqNo) return {h};
    return {};
  }
  void historyArchive(uint64_t, uint64_t, uint64_t) { }
  void errorTx(TxContext, Error e) { error = e; }
  void drain() {
    while (task < tasks.length()) {
      auto t = tasks[task++]; run_(t.kind, t.ticket);
    }
  }
  void start() { attach_(1); ready_(1, true); resume_(); }
};
static void managed()
{
  ZuTestScope(managed);
  Engine engine; engine.start();
  Error error;
  auto body = TxBody::encode<Entry>(Signed{"abc", ZuBSpan{Payload}, {}}, 12, error);
  ZuCheck(body && engine.select_(body) == TxSelect::Accepted);
  engine.drain();
  ZuCheck(engine.output.length() == 1 && engine.calls == 1 && verify(frame(
    engine.output[0].wire->cspan()).view));
  auto original = engine.output[0].wire;
  engine.now += ZuTime{2};
  ZuCheck(engine.replay_(1, 1)); engine.drain();
  ZuCheck(engine.output.length() == 2 && engine.calls == 2 && engine.context.replay);
  auto replay = engine.output[1].wire;
  ZuCheck(replay->cspan() != original->cspan() && verify(frame(replay->cspan()).view));
  MessageHandler<Signed> decoded{frame(replay->cspan()).view};
  auto value = decoded.ctor();
  ZuCheck(decoded.valid() && value.id == "abc" && value.possDup && value.sendingTime == engine.now &&
    value.origSendingTime == engine.output[0].original);
  // A signing failure keeps the assigned body and sequence, without publication.
  engine.fail = true;
  ZuCheck(engine.select_(body) == TxSelect::Accepted); engine.drain();
  ZuCheck(engine.output.length() == 2 && engine.failed_() && engine.error.code == ErrorCode::Output);
  auto next = engine.next_();
  engine.fail = false; engine.resume_(); engine.drain();
  ZuCheck(engine.output.length() == 3 && engine.next_() == next &&
    verify(frame(engine.output[2].wire->cspan()).view));
  Session::Msg control; control.p<Session::HeartBt>(Session::HeartBt{});
  auto admin = TxBody::control(control, error);
  ZuCheck(admin && admin->administrative() && !admin->body().length());
  ZuCheck(engine.select_(admin) == TxSelect::Accepted); engine.drain();
  ZuCheck(engine.output.length() == 4);
  auto parsed = frame(engine.output[3].wire->cspan());
  Session::Msg received;
  ZuCheck(!Session::read(parsed.view, received));
  auto signature = received.p<Session::HeartBt>().signature;
  ZuCheck(signature.length() == Ztls::HMAC<>::Size);
  FieldPos pos;
  ZuCheck(!inspect<Session::HeartBt>(parsed.view.content, 89, pos));
  ZuCheck(signature.data() == reinterpret_cast<const uint8_t *>(
    parsed.view.content.data() + pos.offset));
}

struct PostedTask {
  ZmFn<> fn;
  bool rx;
};
struct Owner {
  using Queue = ZmList<PostedTask, ZmListHeapID<"Zfix.SignTasks">>;
  Queue queue;
  bool rx = true;

  bool rxInvoked() const { return rx; }
  bool txInvoked() const { return !rx; }
  template <typename Fn> void txRun(Fn &&fn) {
    queue.push(PostedTask{ZmFn<>{ZuFwd<Fn>(fn)}, false});
  }
  template <typename Fn> void rxRun(Fn &&fn) {
    queue.push(PostedTask{ZmFn<>{ZuFwd<Fn>(fn)}, true});
  }
  void drain() {
    while (auto task = queue.shift()) { rx = task->data().rx; task->data().fn(); }
    rx = true;
  }
};
struct RawEndpoint : ZmObject, ZmNHeap<RawEndpoint>, Raw<RawEndpoint>, HmacSign {
friend ZuStringT<"Zfix.SignRaw"> ZmNHeapID(RawEndpoint *);
  enum { SignMsg = 1 };
  using MessageCatalog = MTList<Entry>;
  Owner *owner;
  PostedState posted;
  Values<ZmRef<ZiIOBuf>> records;
  Values<RawOutput::Ref> outputs;
  unsigned faults = 0;
  mutable bool onTx = true;

  static SessionConfig config() { SessionConfig c; c.mode = Transport::Raw; return c; }
  explicit RawEndpoint(Owner *o) : Raw{config()}, owner{o} { }
  Owner *app() const { return owner; }
  PostedState txPosted() const { return posted; }
  WorkState workState() const { return {}; }
  bool txPost_(uint64_t bytes) { posted.bytes += bytes; ++posted.count; return true; }
  void txUnpost(uint64_t bytes) { posted.bytes -= bytes; --posted.count; }
  template <typename S>
  void signMsg(S &out, const TxBody *body, TxContext c, ZuCSpan message) const {
    onTx &= owner->txInvoked();
    HmacSign::signMsg(out, body, c, message);
  }
  WireGeometry fixGeometryTx() const { return {}; }
  ZmRef<ZiIOBuf> fixAllocTx(unsigned head) {
    ZmRef<ZiIOBuf> b = ZfixTest::Allocator<Zi::IOBufBuiltinAllocator<Zi::IOBufType::App,
      ZiTxBuf, ZiIOBuf_DefltSize>>::get()->allocBuf();
    b->skip += head; return b;
  }
  bool fixSendTx(ZmRef<ZiIOBuf> b) {
    records.push(ZuMv(b)); return true;
  }
  void fixRawSubmittedTx(RawOutput::Ref out, unsigned) { outputs.push(ZuMv(out)); }
  void fixRawFaultTx(uint64_t, Error) { ++faults; }
  void disconnect_(bool = true) { }
  void resumeRx() { }
};
static void rawOwner()
{
  ZuTestScope(rawOwner);
  Owner owner; ZmRef<RawEndpoint> link = new RawEndpoint{&owner};
  ZuCheck(link->transportBind(false)); link->transportUp_0(); owner.drain();
  Error error;
  Text peer{"SERVER"}, local{"CLIENT"}, version{"FIX.4.4"};
  auto route = config(); route.identity.local = local; route.identity.peer = peer;
  route.beginString = version;
  Signed object{"abc", ZuBSpan{Payload}, ZuBSpan{"stale"}};
  ZuCheck(link->send<Entry>(object, 9, route,
    TxContext{.connection = 1, .seqNo = 7, .sendingTime = context().sendingTime}, false, error));
  ZuCheck(!link->calls && link->posted.count == 1 && !link->outputs.length());
  // The asynchronous request must not borrow these caller-owned strings.
  peer = "CHANGED"; local = "CHANGED"; version = "CHANGED";
  route.identity.local = "CHANGED"; route.identity.peer = "CHANGED";
  route.beginString = "CHANGED";
  route = {}; object = {};
  owner.drain();
  ZuCheck(link->onTx && link->calls == 1 && !link->posted.count && !link->posted.bytes);
  ZuCheck(link->outputs.length() == 1 && !link->faults);
  auto output = link->outputs[0];
  ZuCheck(verify(frame(output->frame()->cspan()).view) && link->covered == Covered);
  owner.rx = false;
  // Transport cursor retries reuse the constructed frame and its signature.
  for (const auto &record : link->records) {
    auto bytes = record->cspan();
    auto n = record->length / 2;
    record->advance(n);
    ZuCheck(record.ptr() == output->frame().ptr());
    ZuCheck((record->cspan() == bytes.offset(n)));
    ZuCheck(link->calls == 1 && link->outputs[0]->frame().ptr() == output->frame().ptr());
    link->transportSent(record, true);
  }
  owner.rx = true; owner.drain();
  ZuCheck(link->calls == 1);
  link->transportDown_0(false); owner.drain();
  link->records.clear(); link->outputs.clear(); link = nullptr;
}

struct SigPart : ZmNHeap<SigPart> {
friend ZuStringT<"Zfix.SigPart"> ZmNHeapID(SigPart *);
  ZuBSpan signature;
};
ZfStruct(, (SigPart, FIX), (signature, ((FIX::Sig<93, 89>), Required), Bytes));
struct ComponentSigned {
  ZuCSpan id;
  ZuPtr<SigPart> part;
};
ZfStruct(, (ComponentSigned),
  (id, (FIX::ID<11>, Required), String), (part, (Required), UDT));
using ComponentFIXFields = ZuTypeConcat<SessionFields<ComponentSigned>, ZfFields_ComponentSigned>;
ComponentFIXFields ZuFields_(ComponentSigned *, ZuFacet::FIX *);
using ComponentEntry = MT<ComponentSigned, "Y">;
static void componentSignature()
{
  ZuTestScope(componentSignature);
  Error error;
  ComponentSigned object{"abc", {}};
  auto body = TxBody::encode<ComponentEntry>(object, 1, error);
  ZuCheck(body && !error && body->body() == "11=abc\1");
  if (!body) return;
  auto archive = TxBody::restore<ComponentEntry>(body->body(), 1, error);
  ZuCheck(archive && !error);
  Indexed<ComponentSigned, ZuFacet::FIX> indexed{body->body()};
  ZuCheck(indexed.template check<ArchiveField>());
  HmacSign signer;
  auto wire = txFrame(frameBuf(), *body, config(), context(), error, {}, signer);
  ZuCheck(wire && !error);
  if (!wire) return;
  auto parsed = frame(wire->cspan());
  MessageHandler<ComponentSigned> input{parsed.view};
  auto value = input.ctor();
  ZuCheck(input.valid() && value.part && value.part->signature.length() == Ztls::HMAC<>::Size);
  FieldPos pos;
  ZuCheck(!inspect<ComponentSigned>(parsed.view.content, 89, pos));
  ZuCheck(value.part->signature.data() == reinterpret_cast<const uint8_t *>(
    parsed.view.content.data() + pos.offset));
  error = {};
  auto unsignedFrame = txFrame(frameBuf(), *body, config(), context(), error);
  ZuCheck(!unsignedFrame && error.code == ErrorCode::Required);
}

struct RequiredPart : ZmNHeap<RequiredPart> {
friend ZuStringT<"Zfix.SignRequiredPart"> ZmNHeapID(RequiredPart *);
  ZuCSpan id, note;
};
ZfStruct(, (RequiredPart, FIX),
  (id, (FIX::ID<21>, Required), String),
  (note, (FIX::ID<22>, FIX::Opt), String));
struct OptionalPartSigned {
  ZuCSpan id;
  ZuPtr<RequiredPart> part;
  ZuBSpan signature;
};
ZfStruct(, (OptionalPartSigned),
  (id, (FIX::ID<11>, Required), String),
  (part, , UDT),
  (signature, ((FIX::Sig<93, 89>)), Bytes));
using OptionalPartFields = ZuTypeConcat<SessionFields<OptionalPartSigned>,
  ZfFields_OptionalPartSigned>;
OptionalPartFields ZuFields_(OptionalPartSigned *, ZuFacet::FIX *);
using OptionalPartEntry = MT<OptionalPartSigned, "D">;
struct PartialPartSign : NoSign {
  enum { SignMsg = 1 };
  template <typename S>
  void signMsg(S &out, const TxBody *, TxContext, ZuCSpan) const {
    out.field(22); out << "seen"; out.end();
    saveBinary<SigField>(out, ZuBSpan{"sig"});
  }
};
static void optionalComponent()
{
  ZuTestScope(optionalComponent);
  Error error;
  OptionalPartSigned object{"abc", {}, {}};
  auto body = TxBody::encode<OptionalPartEntry>(object, 1, error);
  ZuCheck(body && !error);
  if (!body) return;
  auto archive = TxBody::restore<OptionalPartEntry>(body->body(), 1, error);
  ZuCheck(archive && !error);
  auto unsignedWire = txFrame(frameBuf(), *body, config(), context(), error);
  ZuCheck(unsignedWire && !error);
  HmacSign signer;
  auto wire = txFrame(frameBuf(), *body, config(), context(), error, {}, signer);
  ZuCheck(wire && !error);
  if (!wire) return;
  auto parsed = frame(wire->cspan());
  MessageHandler<OptionalPartSigned> input{parsed.view};
  auto value = input.ctor();
  FieldPos length, signature;
  ZuCheck(input.valid() && !value.part &&
    !inspect<OptionalPartSigned>(parsed.view.content, 93, length) &&
    !inspect<OptionalPartSigned>(parsed.view.content, 89, signature));
  if (!input.valid() || length.offset == UINT_MAX || signature.offset == UINT_MAX) return;
  uint8_t digest[Ztls::HMAC<>::Size];
  auto first = parsed.view.msgType.data() - 3;
  auto last = parsed.view.content.data() + length.offset - 3;
  mac({first, uint64_t(last - first)}, digest);
  ZuCheck(value.signature == ZuBSpan{digest} && value.signature.data() ==
    reinterpret_cast<const uint8_t *>(parsed.view.content.data() + signature.offset));
  auto partial = txFrame(frameBuf(), *body, config(), context(), error, {}, PartialPartSign{});
  ZuCheck(!partial && error.code == ErrorCode::Required && error.tag == 21);
  object.part = new RequiredPart;
  object.part->id = "part";
  body = TxBody::encode<OptionalPartEntry>(object, 2, error);
  ZuCheck(body && !error);
  if (!body) return;
  wire = txFrame(frameBuf(), *body, config(), context(), error, {}, signer);
  ZuCheck(wire && !error);
  if (!wire) return;
  MessageHandler<OptionalPartSigned> present{frame(wire->cspan()).view};
  auto part = present.ctor();
  ZuCheck(present.valid() && part.part && part.part->id == "part");
}

struct CtorSigned {
  ZuCSpan id;
  ZuBSpan signature;
  explicit CtorSigned(ZuCSpan value) : id{value} { }
};
ZfStruct(, (CtorSigned),
  (id, (FIX::ID<21>, Ctor<0>, Required), String),
  (signature, ((FIX::Sig<93, 89>)), Bytes));
using CtorSignFields = ZuTypeConcat<SessionFields<CtorSigned>, ZfFields_CtorSigned>;
CtorSignFields ZuFields_(CtorSigned *, ZuFacet::FIX *);
using CtorSignEntry = MT<CtorSigned, "X">;
static void constructorValidation()
{
  ZuTestScope(constructorValidation);
  Error error;
  CtorSigned object{"ctor"};
  auto body = TxBody::encode<CtorSignEntry>(object, 1, error);
  ZuCheck(body && !error);
  if (!body) return;
  HmacSign signer;
  auto wire = txFrame(frameBuf(), *body, config(), context(), error, {}, signer);
  ZuCheck(wire && !error);
  if (!wire) return;
  MessageHandler<CtorSigned> input{frame(wire->cspan()).view};
  auto value = input.ctor();
  ZuCheck(input.valid() && value.id == "ctor" && value.signature.length() == Ztls::HMAC<>::Size);
  TxFrame missing{frameBuf(), "FIX.4.4"};
  missing.body() << "35=X\1" "49=CLIENT\1" "56=SERVER\1" "34=7\1";
  auto malformed = missing.finish(error, signer, body.ptr(), context());
  ZuCheck(malformed && !error);
  if (!malformed) return;
  auto parsed = frame(malformed->cspan());
  MessageHandler<CtorSigned> rejected{parsed.view};
  auto absent = rejected.ctor();
  (void)absent;
  ZuCheck(!rejected.valid() && rejected.error().code == ErrorCode::Required &&
    rejected.error().tag == 21);
  auto check = body->validate(parsed.view, {});
  ZuCheck(check.code == ErrorCode::Required && check.tag == 21);
  auto output = RawOutput::fromFrame(body, context(), malformed, error);
  ZuCheck(!output && error.code == ErrorCode::Required && error.tag == 21);
}

struct SessionRow {
  ZuCSpan sender;
  uint64_t seqNo = 0;
  ZuBSpan data;
};
ZfStruct(, (SessionRow, FIX),
  (sender, (FIX::ID<49>, Required), String),
  (seqNo, (FIX::ID<34>, Required), UInt64),
  (data, (FIX::ID<95>, FIX::Binary<96>, Required), Bytes));
struct SessionRows : ZtArray<SessionRow, ZtArrayHeapID<"Zfix.SignRows">> {
friend AsArray<> Zfix_Fmt(SessionRows *);
};
struct GroupedSigned {
  SessionRows rows;
  ZuBSpan signature;
};
ZfStruct(, (GroupedSigned),
  (rows, (FIX::ID<453>, Required), UDT),
  (signature, ((FIX::Sig<93, 89>), Required), Bytes));
using GroupedFIXFields = ZuTypeConcat<SessionFields<GroupedSigned>, ZfFields_GroupedSigned>;
GroupedFIXFields ZuFields_(GroupedSigned *, ZuFacet::FIX *);
using GroupedEntry = MT<GroupedSigned, "G">;
static void configuredGroups()
{
  ZuTestScope(configuredGroups);
  GroupedSigned object;
  object.rows.push(SessionRow{"GROUP", 99, ZuBSpan{Payload}});
  Error error;
  auto body = TxBody::encode<GroupedEntry>(object, 1, error);
  ZuCheck(body && !error && body->body().find("49=GROUP\1"_z) >= 0 &&
    body->body().find("34=99\1"_z) >= 0);
  if (!body) return;
  HmacSign signer;
  auto wire = txFrame(frameBuf(), *body, config(), context(), error, {}, signer);
  ZuCheck(wire && !error);
  if (!wire) return;
  auto storage = ZtScratch(PrintBuf, wire->length);
  auto direct = Zfix::writeFrame<GroupedEntry>(
    {storage.data(), wire->length}, config(), context(), object, {}, signer, body.ptr());
  ZuCheck(direct && direct.data == wire->cspan());
  auto parsed = frame(wire->cspan());
  MessageHandler<GroupedSigned> input{parsed.view};
  auto value = input.ctor();
  ZuCheck(input.valid() && value.rows.length() == 1 && value.rows[0].sender == "GROUP" &&
    value.rows[0].seqNo == 99 && value.rows[0].data == ZuBSpan{Payload});
  FieldPos length, signature;
  ZuCheck(!inspect<GroupedSigned>(parsed.view.content, 93, length));
  ZuCheck(!inspect<GroupedSigned>(parsed.view.content, 89, signature));
  uint8_t digest[Ztls::HMAC<>::Size];
  auto first = parsed.view.msgType.data() - 3;
  auto last = parsed.view.content.data() + length.offset - 3;
  mac({first, uint64_t(last - first)}, digest);
  ZuCheck(value.signature == ZuBSpan{digest} && value.signature.data() ==
    reinterpret_cast<const uint8_t *>(parsed.view.content.data() + signature.offset));
}

int main(int argc, char **argv)
{
  parse(argc, argv); ZuTestMain();
  ZuTestCall(signing);
  ZuTestCall(componentSignature);
  ZuTestCall(optionalComponent);
  ZuTestCall(constructorValidation);
  ZuTestCall(configuredGroups);
  ZuTestCall(rawOwner);
  ZuTestCall(managed);
  ZuTestCall(tampering);
  ZuTestCall(policies);
  return 0;
}
