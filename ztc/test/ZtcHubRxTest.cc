//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Exercise the daemon's actual callbacks without sockets or OAuth timing.
#define main ZtcHubMain
#include "../src/ztchub.cc"
#undef main

#include <zlib/ZiMultiplex.hh>
#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmFn.hh>
#include <zlib/ZtScratch.hh>

using namespace ZuTestUtil;

namespace ZtcHubRxTest_ {

static ZmRef<ZiIOBuf> allocFrame()
{
  static ZiMultiplex mx;
  static auto allocator = mx.allocator<Zi::IOBufType::App, ZiIOBuf, 1024,
    ZiIOBuf_Align, ZuStringT<"Ztc.Hub.Frame">, 1U<<30>();
  return allocator->allocBuf();
}

struct Timers {
  ZiMultiplex mx;
  Zi::IOBufAllocator *allocator{mx.allocator<Zi::IOBufType::RxTLS, ZiRxQueue::Node, 64>()};
  auto rxAllocator(bool) { return allocator; }
  template <typename ...Args> void add(Args &&...) { }
  template <typename T> void del(T *) { }
};
struct Server {
  ZtcHub_::App *application;
  Timers timers;
  ZmFn<void()> pending;

  auto app() { return application; }
  auto mx() { return &timers; }
  unsigned rxThread() { return 1; }
  template <typename L> void rxRun(L &&fn) {
    ZmAssert(!pending);
    pending = ZuFwd<L>(fn);
  }
  void run() {
    auto fn = ZuMv(pending);
    if (fn) fn();
  }
  template <typename L> void txRun(L &&fn) { fn(); }
};
struct Sink {
  unsigned headRoom() const { return 0; }
  unsigned tailRoom() const { return 0; }
  ZmRef<ZiIOBuf> allocBuf(unsigned head) {
    ZmRef<ZiIOBuf> buf = allocFrame();
    buf->skip += head;
    return buf;
  }
  template <typename P> Sink &operator <<(P &&) { return *this; }
  bool appendOwned(ZmRef<ZiIOBuf>) { return true; }
  void flush() { }
};
struct Link_ : public ZmNHeap<Link_>, public ZmObject {
friend inline ZuStringT<"Ztc.Test.HubLink"> ZmNHeapID(Link_ *);

  enum { TLS = true };
  Server *server;
  ZtcHub_::App::LinkState input;
  unsigned closeCode = 0;

  Link_(Server *server_) : server{server_} { }
  auto app() { return server; }
  auto &channel() { return *this; }
  auto &state() { return input; }
  template <typename L> void streamTx(L &&fn) {
    Sink sink;
    fn(sink);
  }
  void close(unsigned code) { closeCode = code; }
  template <typename L> void txStream_(L &&fn, Zws::Opcode::T) {
    Sink sink;
    fn(sink);
  }
};

ZuDerive(Link, Link_);
ZuDerive(Bytes, (ZtArray<uint8_t, ZtArrayHeapID<"Ztc.Test.HubBytes">>));

static Ztc::HubdCf config(unsigned frames)
{
  Ztc::HubdCf cf;
  cf.listeners.push(Ztc::ListenerCf{
    "127.0.0.1", "/ztc", "server.crt", "server.key", "/session", {}});
  cf.issuerURL = "https://zum.example/oauth2/7";
  cf.audience = "https://ztchub.example";
  cf.managementIssuer = "https://zum.example";
  cf.managementClientID = "ztchub-manager";
  cf.actions.push("Request");
  cf.actions.push("Telemetry");
  cf.roles.push("Client");
  cf.roles.push("Agent");
  cf.maxFrame = 256;
  cf.controlFrames = frames;
  cf.controlBytes = 512;
  return cf;
}

template <auto &ZuTest_scope, bool RT = false>
struct Fixture {
  Ztc::Hubd hub;
  ZtcHub_::App app{&hub};
  Server server{&app};
  ZmRef<Link> link{new Link{&server}};

  Fixture(unsigned frames = 4) {
    if constexpr (RT)
      ZuCheckRT(hub.init(config(frames)) && hub.start());
    else
      ZuCheck(hub.init(config(frames)) && hub.start());
    link->input.sessionID = 1;
    link->input.generation = 1;
  }
  ~Fixture() {
    app.disconnected(*link, false);
    hub.stop();
    hub.final();
  }
  int start() { return app.messageStart(*link, Zws::Opcode::Binary); }
  int receive(ZuSpan<uint8_t> bytes) {
    auto backing = server.timers.allocator->allocBuf();
    if (!backing || !backing->append(ZuBSpan{bytes})) return -1;
    return receive(ZuMv(backing));
  }
  int receive(ZmRef<ZiIOBuf> backing) {
    ZiRxStream<ZiRxQueue> rx;
    rx.push(ZmRef<ZiRxQueue::Node>::acquire(
      static_cast<ZiRxQueue::Node *>(ZuMv(backing).release())));
    return app.process(*link, rx);
  }
  int end() { return app.messageEnd(*link); }
  void authorize(int error = Zum::ServiceError::OK) {
    Zum::ServicePrincipal principal;
    principal.tokenID.issuerURL = hub.config().issuerURL;
    principal.tokenID.jti = "token";
    principal.subject = "agent";
    principal.audience = hub.config().audience;
    principal.expires = Zm::now().sec() + 60;
    principal.actions.push("Telemetry");
    principal.authMethod = "client_credentials";
    if (error == Zum::ServiceError::OK && !link->input.closed)
      std::cout << "# ";
    app.authorize(link, error, ZuMv(principal));
    server.run();
  }
};

static Ztc::HubFrame ack(uint64_t seqNo, ZuCSpan id = "publisher")
{
  Ztc::Builder builder{allocFrame()};
  auto nested = builder.startNested();
  auto value = Zfb::save(builder, Ztc::Ack{
    .id = ZuID{id}, .seqNo = seqNo, .status = Ztc::AckStatus::OK});
  builder.finish(Ztc::saveMsg(builder, nested, Ztc::Body::Ack, value));
  return builder.buf();
}
static ZuSpan<uint8_t> bytes(const Ztc::HubFrame &frame)
{
  return {frame->data(), frame->length};
}

static Ztc::HubFrame packetStorage()
{
  Ztc::HubFrame frame = allocFrame();
  frame->skip = 32; // composed lower framing room in this test writer
  return frame;
}

// A decoder needs a contiguous wire artifact; the real output below retains
// its two original ranges. This join exists only for schema verification.
static Bytes packetBytes(const Ztc::HubPacket &packet)
{
  Bytes result;
  result.length(unsigned(packet.length()));
  memcpy(result.data(), packet.head->cspan().data(), packet.head->length);
  if ((packet.tail ? packet.tail->length : 0))
    memcpy(result.data() + packet.head->length,
      packet.tail->cspan().data(), (packet.tail ? packet.tail->length : 0));
  return result;
}

struct PacketSink {
  ZtArray<Ztc::HubFrame, ZtArrayHeapID<"Ztc.Test.PacketParts">> parts;

  PacketSink &operator <<(ZuBSpan span) {
    // Foreign/replayed WS output is serialized into destination storage.
    auto copy = packetStorage();
    ZmAssert(copy->append(span));
    parts.push(ZuMv(copy));
    return *this;
  }
  bool appendOwned(ZmRef<ZiIOBuf> buf) {
    parts.push(ZuMv(buf));
    return true;
  }
};

// Like the real WS writer, only explicit unpublished destination output grants
// in-place framing/masking authority; foreign input is serialized first.
struct PacketWriter : public PacketSink {
  Ztc::HubFrame allocated;
  Ztc::HubFrame sent;
  unsigned adopted = 0;

  Ztc::HubFrame allocBuf(unsigned head) {
    allocated = packetStorage();
    allocated->skip += head;
    return allocated;
  }
  bool fits(const ZiIOBuf &buf) const {
    return buf.skip >= 32 && buf.avail() >= 16;
  }
  bool appendOwned(ZmRef<ZiIOBuf> buf) {
    ++adopted;
    buf->data()[0] ^= 0x5a; // observable stand-in for in-place WS masking
    sent = ZuMv(buf);
    return true;
  }
};

static void prefixAuthority()
{
  ZuTestScope(prefixAuthority);
  auto source = ack(51);
  auto physical = source->data();
  auto message = Ztc::msg(source->cspan());
  Bytes original;
  original.length(source->length);
  memcpy(original.data(), source->data(), source->length);
  Ztc::RouteInfo route;
  route.subID = 7; route.deviceID = "device-a"; route.agentGeneration = 3;
  bool valid = true;
  auto build = ZtcHub_::agentFrame(route, message, source, UINT32_MAX, &valid);
  PacketWriter writer;
  auto storage = writer.allocBuf(32);
  auto allocated = storage.ptr();
  auto packet = build(ZuMv(storage), 16);
  ZuCheck(valid && packet && packet.head.ptr() == allocated, (return));
  ZuCheck(packet.head->refCount() > 1 && writer.fits(*allocated));
  auto payload = packet.head->cspan().data();
  auto first = payload[0];
  ZuMv(packet).write(writer, true);
  ZuCheck(writer.adopted == 1 && writer.sent.ptr() == allocated &&
    writer.parts.length() == 1 && writer.parts[0] != source && writer.parts[0]->cspan() == source->cspan(), (return));
  ZuCheck(writer.sent->data() == payload && payload[0] == uint8_t(first ^ 0x5a));
  ZuCheck(!memcmp(physical, original.data(), original.length()));

  auto borrowed = build(packetStorage(), 16);
  ZuCheck(valid && borrowed, (return));
  auto expected = packetBytes(borrowed);
  auto backing = borrowed.head;
  auto offset = backing->skip, length = backing->length;
  PacketWriter lower;
  ZuCheck(backing->refCount() == 2 && lower.fits(*backing));
  ZuMv(borrowed).write(lower, false);
  ZuCheck(!lower.adopted && !lower.sent && lower.parts.length() == 2, (return));
  ZuCheck(lower.parts[0] != backing && lower.parts[1] != source &&
    lower.parts[1]->cspan() == source->cspan() &&
    backing->skip == offset && backing->length == length);
  Ztc::HubPacket retained;
  retained.head = lower.parts[0]; retained.tail = lower.parts[1];
  auto actual = packetBytes(retained);
  ZuCheck(ZuBSpan{actual} == ZuBSpan{expected} && !memcmp(physical, original.data(), original.length()));
}

static void segmentedEnvelope()
{
  ZuTestScope(segmentedEnvelope);
  Fixture<ZuTest_scope> f;
  f.authorize();
  auto first = ack(11), second = ack(22);
  ZuCheck(f.start() == 1 && f.receive(bytes(first)) == 1);
  auto sourceHold = f.link->input.frame;
  auto source = sourceHold.ptr();
  auto original = source->data_();
  auto message = Ztc::msg(source->cspan());
  auto payload = message.get<Ztc::ZfField(Msg, data)>();
  ZuCheck(f.end() == 1 && !f.link->input.frame);
  Ztc::RouteInfo a, b;
  a.subID = 7; a.deviceID = "device-a"; a.agentGeneration = 3;
  b.subID = 19; b.deviceID = "a-different-device-with-a-longer-identifier";
  b.agentGeneration = 5;
  bool validA = true, validB = true;
  auto buildA = ZtcHub_::agentFrame(a, message, ZmRef<ZiIOBuf>{source},
    UINT32_MAX, &validA);
  auto buildB = ZtcHub_::agentFrame(b, message, ZmRef<ZiIOBuf>{source},
    UINT32_MAX, &validB);
  auto storageA = packetStorage(), storageB = packetStorage();
  auto allocatedA = storageA.ptr(), allocatedB = storageB.ptr();
  auto packetA = buildA(ZuMv(storageA), 16);
  auto packetB = buildB(ZuMv(storageB), 16);
  buildA = {}; buildB = {};
  ZuCheck(validA && validB && packetA && packetB, (return));
  ZuCheck(packetA.head.ptr() == allocatedA && packetB.head.ptr() == allocatedB);
  ZuCheck(packetA.head->skip >= 32 && packetB.head->skip >= 32 &&
    allocatedA->avail() >= 16 && allocatedB->avail() >= 16);
  ZuCheck(packetA.tail.ptr() == source && packetB.tail.ptr() == source &&
    packetA.tail->cspan().data() == payload.data() &&
    packetB.tail->cspan().data() == payload.data() &&
    packetA.tail->length == payload.length() && packetB.tail->length == payload.length());
  ZuCheck(!(packetA.head->length & (Ztc::Builder::Align - 1)) &&
    !(packetB.head->length & (Ztc::Builder::Align - 1)));
  PacketSink pending;
  auto delivered = packetA;
  ZuMv(delivered).write(pending, false);
  ZuCheck(pending.parts.length() == 2 &&
    pending.parts[0] != packetA.head && pending.parts[1].ptr() != source &&
    pending.parts[1]->cspan() == payload);
  sourceHold = nullptr;
  // Draining serialized foreign output must not alter another queued delivery.
  pending.parts[0]->advance(pending.parts[0]->length);
  pending.parts[1]->advance(pending.parts[1]->length);
  ZuCheck(source->data_() == original && packetA.tail->cspan().data() == payload.data() &&
    packetA.body().get<Ztc::Body::Ack>().get<Ztc::ZfField(Ack, seqNo)>() == 11);
  ZuCheck(f.start() == 1 && f.link->input.frame.ptr() != source &&
    f.receive(bytes(second)) == 1 && f.end() == 1);
  auto wireA = packetBytes(packetA), wireB = packetBytes(packetB);
  auto msgA = Ztc::msg(wireA), msgB = Ztc::msg(wireB);
  ZuCheck(msgA && msgB, (return));
  ZuCheck(msgA.get<Ztc::ZfField(Msg, subID)>() == a.subID &&
    msgB.get<Ztc::ZfField(Msg, subID)>() == b.subID &&
    msgA.get<Ztc::ZfField(Msg, deviceID)>() == a.deviceID &&
    msgB.get<Ztc::ZfField(Msg, deviceID)>() == b.deviceID &&
    msgA.get<Ztc::ZfField(Msg, agentGen)>() == a.agentGeneration &&
    msgB.get<Ztc::ZfField(Msg, agentGen)>() == b.agentGeneration);
  ZuCheck(msgA.get<Ztc::ZfField(Msg, data)>() == payload &&
    msgB.get<Ztc::ZfField(Msg, data)>() == payload &&
    Ztc::msgBody(msgA).get<Ztc::Body::Ack>().get<Ztc::ZfField(Ack, seqNo)>() == 11 &&
    Ztc::msgBody(msgB).get<Ztc::Body::Ack>().get<Ztc::ZfField(Ack, seqNo)>() == 11);
}

static void packetBounds()
{
  ZuTestScope(packetBounds);
  auto source = ack(31);
  auto message = Ztc::msg(source->cspan());
  Ztc::RouteInfo route;
  route.subID = UINT64_MAX; route.agentGeneration = UINT64_MAX;
  bool valid = true;
  auto build = ZtcHub_::agentFrame(route, message, source, UINT32_MAX, &valid);
  auto packet = build(packetStorage(), 16);
  ZuCheck(valid && packet && !packet.message().get<Ztc::ZfField(Msg, deviceID)>(), (return));
  auto length = unsigned(packet.length());
  source->advance(source->length);
  auto exact = ZtcHub_::agentFrame(route, message, source, length, &valid);
  auto accepted = exact(packetStorage(), 16);
  ZuCheck(valid && accepted && accepted.length() == length &&
    accepted.tail->cspan().data() == packet.tail->cspan().data(), (return));
  auto bounded = ZtcHub_::agentFrame(route, message, source, length - 1, &valid);
  auto rejected = bounded(packetStorage(), 16);
  ZuCheck(!valid && !rejected);
  auto wire = packetBytes(accepted);
  auto msg = Ztc::msg(wire);
  ZuCheck(msg && msg.get<Ztc::ZfField(Msg, subID)>() == UINT64_MAX &&
    msg.get<Ztc::ZfField(Msg, agentGen)>() == UINT64_MAX &&
    Ztc::msgBody(msg).get<Ztc::Body::Ack>().get<Ztc::ZfField(Ack, seqNo)>() == 31);
}

static void packetCharges()
{
  ZuTestScope(packetCharges);
  Fixture<ZuTest_scope> f;
  auto source = ack(41);
  auto message = Ztc::msg(source->cspan());
  Ztc::RouteInfo route;
  route.subID = 7; route.deviceID = "device-a"; route.agentGeneration = 3;
  bool valid = true;
  auto build = ZtcHub_::agentFrame(route, message, source, UINT32_MAX, &valid);
  auto packet = build(packetStorage(), 16);
  ZuCheck(valid && packet, (return));
  auto max = f.hub.config().controlBytes - f.hub.config().maxFrame;
  unsigned count = max / packet.length() + 1;
  ZuCheck(valid && packet && count <= f.hub.config().controlFrames - 1 &&
    uint64_t(count) * packet.head->length <= max);
  f.link->input.sending = true; // an earlier Tx turn still owns this connection
  for (unsigned i = 0; i < count; ++i) ZtcHub_::App::enqueue(f.link, packet, false);
  auto egress = f.link->input.egresses.find(route.subID);
  ZuCheck(f.link->input.closed && f.link->closeCode == Zws::CloseCode::Policy);
  ZuCheck(egress && egress->controlBytes == uint64_t(count - 1) * packet.length() &&
    egress->control.length() == count - 1);
}

static void packetAlign()
{
  ZuTestScopeRT(packetAlign);
  ZtString<> id;
  bool partialEnd = false;
  for (unsigned i = 0; i < Ztc::Builder::Align; ++i) {
    id << 'x';
    // Omit the 64-bit field using its schema default. Zero is an explicit
    // sequence number and would force eight-byte nested-root alignment.
    auto source = ack(Ztc::ZfField(Ack, seqNo)::deflt(), id);
    auto message = Ztc::msg(source->cspan());
    ZuCheckRT(message && !Ztc::msgBody(message).get<Ztc::Body::Ack>()
      .has<Ztc::ZfField(Ack, seqNo)>(), (return));
    Ztc::RouteInfo route;
    route.subID = i;
    if (i & 1) { route.deviceID = "device-a"; route.agentGeneration = 3; }
    bool valid = true;
    auto build = ZtcHub_::agentFrame(route, message, source, UINT32_MAX, &valid);
    auto packet = build(packetStorage(), 16);
    ZuCheckRT(valid && packet, (return));
    partialEnd |= bool((packet.tail ? packet.tail->length : 0) & (Ztc::Builder::Align - 1));
    auto wire = packetBytes(packet);
    auto msg = Ztc::msg(wire);
    ZuCheckRT(msg && !(packet.head->length & (Ztc::Builder::Align - 1)), (return));
    ZuCheckRT(msg.get<Ztc::ZfField(Msg, subID)>() == i &&
      msg.get<Ztc::ZfField(Msg, deviceID)>() == route.deviceID &&
      msg.get<Ztc::ZfField(Msg, agentGen)>() == route.agentGeneration &&
      Ztc::msgBody(msg).get<Ztc::Body::Ack>().get<Ztc::ZfField(Ack, id)>() == id);
  }
  // Nested roots need not end on the outer Msg's maximum alignment. No copy
  // or synthetic trailing payload bytes are necessary for these valid roots.
  ZuCheckRT(partialEnd);
}

static void frameLimit()
{
  ZuTestScope(frameLimit);
  Fixture<ZuTest_scope> f{2};
  ZuCheck(f.start() == 1);
  ZuCheck(f.end() == 1);
  ZuCheck(f.start() == 1);
  ZuCheck(f.end() == 1);
  ZuCheck(f.link->input.pending.length() == 2);
  ZuCheck(f.app.messageStart(*f.link, Zws::Opcode::Binary) < 0);
  ZuCheck(f.link->input.closed && f.link->closeCode == Zws::CloseCode::Policy);
}

static void byteLimit()
{
  ZuTestScope(byteLimit);
  Fixture<ZuTest_scope> f;
  auto data = ZtScratch(Bytes, f.hub.config().maxFrame);
  data.length(f.hub.config().maxFrame);
  memset(data.data(), 0, data.length());
  ZuCheck(f.start() == 1);
  ZuCheck(f.receive(data) == 1);
  ZuCheck(f.end() == 1);
  ZuCheck(f.start() == 1);
  ZuCheck(f.receive(data.span().trunc(data.length() - 1)) == 1);
  ZuCheck(f.end() == 1);
  ZuCheck(f.start() == 1);
  ZuCheck(f.receive(data.span().trunc(1)) == 1);
  ZuCheck(f.receive(data.span().trunc(1)) < 0);
  ZuCheck(f.link->input.pendingBytes == 511 && (f.link->input.frame ? f.link->input.frame->length : 0) == 1);
  f.authorize();
  ZuCheck(f.link->input.closed && !f.link->input.authenticated);
}

static void invalid()
{
  ZuTestScope(invalid);
  Fixture<ZuTest_scope> f;
  ZuCheck(f.start() == 1);
  ZuCheck(f.end() == 1);
  f.authorize();
  ZuCheck(f.link->input.closed && f.link->closeCode == Zws::CloseCode::InvalidData);
}

static void partial()
{
  ZuTestScope(partial);
  Fixture<ZuTest_scope> f;
  auto first = ack(1), second = ack(2);
  ZuCheck(f.start() == 1);
  ZuCheck(f.receive(bytes(first)) == 1);
  ZuCheck(f.end() == 1);
  ZuCheck(f.start() == 1);
  auto half = second->length >> 1;
  ZuCheck(f.receive(bytes(second).trunc(half)) == 1);
  auto saved = f.link->input.frame.ptr();
  f.authorize();
  ZuCheck(f.link->input.authenticated && !f.link->input.closed);
  ZuCheck(!f.link->input.pending.length() && !f.link->input.pendingBytes);
  ZuCheck(f.link->input.frame.ptr() == saved && saved->length == half);
  ZuCheck(f.receive(bytes(second).offset(half)) == 1);
  ZuCheck(f.end() == 1);
  ZuCheck(!f.link->closeCode && !f.link->input.pending.length());
}

static void rejected()
{
  ZuTestScope(rejected);
  Fixture<ZuTest_scope> f;
  auto value = ack(1);
  ZuCheck(f.start() == 1);
  ZuCheck(f.receive(bytes(value)) == 1);
  ZuCheck(f.end() == 1);
  f.authorize(Zum::ServiceError::Unauthorized);
  ZuCheck(f.link->input.closed && !f.link->input.authenticated);
  f.authorize();
  ZuCheck(!f.link->input.authenticated);
  f.app.disconnected(*f.link, false);
  ZuCheck(!f.link->input.pending.length() && !f.link->input.pendingBytes);
}

static void reuse()
{
  ZuTestScope(reuse);
  Fixture<ZuTest_scope> f;
  auto value = ack(1);
  ZuCheck(f.start() == 1);
  ZuCheck(f.receive(bytes(value)) == 1);
  ZuCheck(f.end() == 1);
  f.authorize();
  ZuCheck(f.link->input.authenticated && !f.link->input.closed);
  ZuCheck(!f.link->input.frame);
  ZuCheck(f.start() == 1);
  ZuCheck(!f.link->input.frame);
}

static void drain()
{
  ZuTestScope(drain);
  Fixture<ZuTest_scope> f;
  auto value = ack(1);
  {
    ZuTestRepeat(queued, 3);
    for (unsigned i = 0; i < 3; ++i) {
      ZuCheck(f.start() == 1);
      ZuCheck(f.receive(bytes(value)) == 1);
      ZuCheck(f.end() == 1);
    }
  }
  f.authorize();
  ZuCheck(f.link->input.authenticated && !f.link->input.closed);
  ZuCheck(f.link->input.pending.length() == 2 && f.server.pending);
  // New arrivals remain behind the frames awaiting their scheduled turn.
  ZuCheck(f.start() == 1);
  ZuCheck(f.receive(bytes(value)) == 1);
  ZuCheck(f.end() == 1);
  ZuCheck(f.link->input.pending.length() == 3);
  f.server.run();
  ZuCheck(f.link->input.pending.length() == 2 && f.server.pending);
  f.server.run();
  ZuCheck(f.link->input.pending.length() == 1 && f.server.pending);
  f.server.run();
  ZuCheck(!f.link->input.pending.length() && !f.link->input.pendingBytes);
  ZuCheck(!f.server.pending && !f.link->closeCode);
}

static void closedDrain()
{
  ZuTestScope(closedDrain);
  Fixture<ZuTest_scope> f;
  auto value = ack(1);
  {
    ZuTestRepeat(queued, 2);
    for (unsigned i = 0; i < 2; ++i) {
      ZuCheck(f.start() == 1);
      ZuCheck(f.receive(bytes(value)) == 1);
      ZuCheck(f.end() == 1);
    }
  }
  f.authorize();
  ZuCheck(f.link->input.pending.length() == 1 && f.server.pending);
  f.app.disconnected(*f.link, false);
  ZuCheck(f.link->input.closed && !f.link->input.pending.length());
  f.server.run();
  ZuCheck(!f.server.pending && !f.link->input.pendingBytes);
  ZuCheck(!f.link->closeCode);
}

static void retainedInput()
{
  ZuTestScope(retainedInput);
  Fixture<ZuTest_scope> f;
  auto first = ack(71), second = ack(72);
  static ZiMultiplex mx;
  static auto allocator = mx.allocator<Zi::IOBufType::App, ZiRxQueue::Node, 1024, ZiIOBuf_Align,
    ZuStringT<"Ztc.Test.Input">, 1U<<20>();
  ZmRef<ZiRxQueue::Node> wire{allocator->allocBuf().ptr<ZiRxQueue::Node>()};
  const uint8_t prefix[13] = {};
  wire->append(prefix, sizeof(prefix));
  wire->append(first->cspan()); wire->append(second->cspan());
  ZiRxStream<ZiRxQueue> input;
  input.push(wire);
  input.advance(sizeof(prefix));
  Ztc::HubFrame part;
  auto alloc = [&f]() { return f.server.timers.allocator->allocBuf(); };
  ZuCheck(input.extract([&first](ZuBSpan) -> int64_t { return first->length; },
    alloc, part) == first->length);
  auto retained = part;
  ZuCheck(f.start() == 1 && f.receive(ZuMv(part)) == 1 && f.end() == 1);
  ZuCheck(f.link->input.pendingBytes == first->length && !f.link->input.frame);
  ZuCheck(input.extract([&second](ZuBSpan) -> int64_t { return second->length; },
    alloc, part) == second->length && !input);
  ZuCheck(part != retained && part->cspan() == second->cspan());
  ZuCheck(f.start() == 1 && f.receive(ZuMv(part)) == 1 && f.end() == 1);
  ZuCheck(f.link->input.pendingBytes == first->length + second->length &&
    f.link->input.pending.length() == 2);
  auto queued = f.link->input.pending.shift();
  ZuCheck(queued == retained && queued->cspan() == first->cspan() &&
    Ztc::msgBody(Ztc::msg(queued->cspan())).get<Ztc::Body::Ack>()
      .get<Ztc::ZfField(Ack, seqNo)>() == 71);
  auto next = f.link->input.pending.shift();
  ZuCheck(next != queued && next->cspan() == second->cspan() &&
    Ztc::msgBody(Ztc::msg(next->cspan())).get<Ztc::Body::Ack>()
      .get<Ztc::ZfField(Ack, seqNo)>() == 72);
  f.link->input.pendingBytes = 0;
}

static void fragmentedInput()
{
  ZuTestScopeRT(fragmentedInput);
  Fixture<ZuTest_scope, true> f;
  ZtString<> id;
  for (unsigned i = 0; i < 144; ++i) id << 'x';
  auto source = ack(81, id);
  ZuCheckRT(source->length > 128 && source->length <= f.hub.config().maxFrame, (return));
  ZuCheckRT(f.start() == 1);
  for (unsigned off = 0; off < source->length;) {
    unsigned n = source->length - off;
    if (n > 23) n = 23;
    auto backing = f.server.timers.allocator->allocBuf();
    ZuCheckRT(backing && backing->append(source->data() + off, n), (return));
    ZuCheckRT(f.receive(ZuMv(backing)) == 1, (return));
    off += n;
    auto frame = f.link->input.frame;
    ZuCheckRT(frame->length == off && !memcmp(frame->data(), source->data(), off));
  }
  auto frame = f.link->input.frame;
  ZuCheckRT(f.end() == 1 && f.link->input.pendingBytes == source->length &&
    frame->length == source->length && !frame->builtin());
}

} // namespace ZtcHubRxTest_

int main()
{
  using namespace ZtcHubRxTest_;
  ZuTestMain();
  ZiLog::init("ZtcHubRxTest");
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();
  ZuTestCall(retainedInput);
  ZuTestCall(fragmentedInput);
  ZuTestCall(frameLimit);
  ZuTestCall(byteLimit);
  ZuTestCall(invalid);
  ZuTestCall(partial);
  ZuTestCall(rejected);
  ZuTestCall(reuse);
  ZuTestCall(drain);
  ZuTestCall(closedDrain);
  ZuTestCall(segmentedEnvelope);
  ZuTestCall(packetBounds);
  ZuTestCall(packetCharges);
  ZuTestCall(packetAlign);
  ZuTestCall(prefixAuthority);
  ZiLog::stop();
}
