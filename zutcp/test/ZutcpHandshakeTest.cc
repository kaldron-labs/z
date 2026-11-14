//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/Zutcp.hh>

using namespace ZuTestUtil;

// Build actual Initial packets in their final buffer. Padding keeps the copied
// bootstrap's minimum datagram geometry; there is no TLS/CRYPTO or AEAD output.
static int setup(
  uint8_t *out, unsigned capacity, const Zutcp::CxnID &dcid, const Zutcp::CxnID &scid,
  uint64_t pn, const Zutcp::TransportParams *params, bool ack)
{
  enum { Frames = Zutcp::MinPktPayload };
  int offset = Zutcp::InitialPkt::write(out, capacity, dcid, scid, pn, 2, params, Frames);
  if (offset < 0) return -1;
  memset(out + offset, 0, Frames);
  if (ack && Zutcp::FrameCodec::writeAck(out + offset, Frames, 0, 0, 0) < 0) return -1;
  return offset + Frames;
}

static Zutcp::TxPkt sent(uint64_t pn, unsigned bytes)
{
  Zutcp::TxPkt packet;
  packet.pn = pn;
  packet.bytes = bytes;
  packet.space = Zutcp::PktNumSpace::Initial;
  packet.sentTime = Zutcp::timeUS(1);
  packet.ackEliciting = packet.inFlight = true;
  return packet;
}

static void testInitialTranscript()
{
  ZuTestScope(testInitialTranscript);
  Zutcp::ClientBootstrap client;
  ZuCHECK(client.start(Zutcp::CxnID{"original"}, Zutcp::CxnID{"client00"}),
    "client establishes routing identity without TLS");
  Zutcp::TransportParams clientParams;
  clientParams.initialSCID = client.initialSCID();
  clientParams.initialMaxData = 8192;

  uint8_t request[Zutcp::BufSize], response[Zutcp::BufSize], finalAck[Zutcp::BufSize];
  int n = setup(request, sizeof(request), client.initialDCID(), client.initialSCID(),
    0, &clientParams, false);
  Zutcp::InitialPkt initial;
  bool decoded = n > 0 && initial.parse({request, unsigned(n)}) == n;
  ZuCHECK(decoded && initial.number == 0 && initial.params,
    "request carries plaintext parameters in Initial context");
  if (!decoded) return;
  Zutcp::TransportParams received;
  ZuCHECK(!received.decode(initial.params) && received.initialMaxData == 8192 &&
      received.initialSCID == client.initialSCID(), "server receives client limits");
  Zutcp::ServerBootstrap server;
  ZuCHECK(server.acceptInitial(initial.header, n), "server accepts the original Initial");
  auto identity = server.localInitialSCID();
  ZuCHECK(!server.acceptInitial(initial.header, n) && server.localInitialSCID() == identity,
    "duplicate setup cannot create another accepted routing identity");
  Zutcp::PktTxSpace clientTx, serverTx;
  ZuCHECK(clientTx.add(sent(0, n)), "client setup uses retained sent-packet accounting");

  Zutcp::TransportParams serverParams;
  serverParams.initialMaxData = 16384;

  ZuCHECK(server.transportParams(serverParams),
    "server response uses bootstrap CIDs");
  int m = setup(response, sizeof(response), server.clientInitialSCID(), identity,
    0, &serverParams, true);
  Zutcp::InitialPkt reply;
  decoded = m > 0 && reply.parse({response, unsigned(m)}) == m;
  ZuCHECK(decoded && reply.header.dcid == client.initialSCID() && reply.params,
    "response identifies the client and carries server parameters");
  if (!decoded) return;
  ZuCHECK(!received.decode(reply.params) && received.initialMaxData == 16384 &&
      client.validateServerParams(received, reply.header.scid),
    "client validates response CIDs and independently configured peer limits");
  Zutcp::Frame ack;
  unsigned used = 0;
  ZuCHECK(!Zutcp::FrameCodec::parse(reply.frames, ack, used) &&
      ack.type == Zutcp::FrameType::Ack && ack.offset == 0 &&
      clientTx.ack(ack.offset) && !clientTx.bytesInFlight(),
    "ACK-bearing response retires the client's setup through existing recovery");
  ZuCHECK(serverTx.add(sent(0, m)) && serverTx.bytesInFlight() == unsigned(m),
    "server response remains in flight despite client receipt of its header");

  int k = setup(finalAck, sizeof(finalAck), identity, client.initialSCID(), 1, nullptr, true);
  Zutcp::InitialPkt confirm;
  decoded = k > 0 && confirm.parse({finalAck, unsigned(k)}) == k;
  ZuCHECK(decoded && !confirm.params && confirm.number == 1,
    "final ACK has no parameter payload and introduces no CRYPTO stream");
  if (!decoded) return;
  ZuCHECK(!Zutcp::FrameCodec::parse(confirm.frames, ack, used) &&
      ack.type == Zutcp::FrameType::Ack && serverTx.ack(ack.offset) &&
      !serverTx.bytesInFlight() && !serverTx.ack(ack.offset),
    "final ACK retires the response exactly once");
}

static void testInitialBounds()
{
  ZuTestScope(testInitialBounds);
  Zutcp::TransportParams params;
  params.initialSCID = "client00";
  params.initialMaxData = 65536;
  uint8_t packet[512];
  bool valid = true, bounded = true;
  for (unsigned pnLength = 1; pnLength <= 4; ++pnLength) {
    int n = Zutcp::InitialPkt::write(packet, sizeof(packet), Zutcp::CxnID{"original"},
      params.initialSCID, 0x1ff, pnLength, &params, 0);
    Zutcp::InitialPkt input;
    valid &= n > 0 && input.parse({packet, unsigned(n)}, 0x1fe) == n &&
      input.number == 0x1ff && !input.frames && input.params &&
      input.params.data() > packet && input.params.data() + input.params.length() == packet + n;
    if (n <= 0) continue;
    for (int length = 0; length < n; ++length)
      bounded &= input.parse({packet, unsigned(length)}, 0x1fe) < 0;
    memset(packet + n, 0xa5, sizeof(packet) - n);
    valid &= input.parse({packet, sizeof(packet)}, 0x1fe) == n && !input.frames;
  }
  ZuCHECK(valid, "plain packet numbers and parameter views need no protection samples or tag");
  ZuCHECK(bounded, "truncated header/packet/parameter bytes cannot escape the declared extent");
  auto cid = Zutcp::CxnID{"original"};
  ZuCHECK(Zutcp::InitialPkt::write(packet, 1, cid, cid, 0, 1, &params, 0) < 0 &&
      Zutcp::InitialPkt::write(packet, sizeof(packet), cid, cid, 0, 0, &params, 0) < 0 &&
      Zutcp::InitialPkt::write(packet, sizeof(packet), cid, cid, 0, 5, &params, 0) < 0 &&
      Zutcp::InitialPkt::write(packet, sizeof(packet), cid, cid, 0, 1, &params, UINT_MAX) < 0,
    "output framing rejects impossible packet and frame capacities");
}

static void testInitialContext()
{
  ZuTestScope(testInitialContext);
  uint8_t packet[512];
  auto cid = Zutcp::CxnID{"original"};
  int n = Zutcp::InitialPkt::write(packet, sizeof(packet), cid, cid, 0, 1, nullptr, 0);
  Zutcp::InitialPkt input;
  ZuCHECK(n > 0 && input.parse({packet, unsigned(n)}) == n &&
      !input.params && !input.frames &&
      n == int(input.header.payloadOffset + 1),
    "ACK-only Initial has just a zero-length prefix");
  if (n <= 0) return;
  static constexpr uint8_t expected[] = {
    0xc0,0,0,0,1,8, 'o','r','i','g','i','n','a','l',
    8, 'o','r','i','g','i','n','a','l', 2,0,0
  };
  ZuCHECK(n == sizeof(expected) && !memcmp(packet, expected, sizeof(expected)),
    "independent Initial header/packet-number/parameter-prefix wire vector");
  packet[input.header.payloadOffset] = 0x40;
  ZuCHECK(input.parse({packet, unsigned(n)}) < 0, "truncated parameter-length VarInt");
  packet[input.header.payloadOffset] = 1;
  ZuCHECK(input.parse({packet, unsigned(n)}) < 0,
    "parameter length cannot consume a next packet");
  packet[input.header.payloadOffset] = 0;
  packet[0] |= 0x20;
  ZuCHECK(input.parse({packet, unsigned(n)}) < 0, "Handshake context is not a plaintext Initial");
  packet[0] &= ~0x20;
  packet[4] = 2;
  ZuCHECK(input.parse({packet, unsigned(n)}) < 0, "unrecognized Initial version");
  Zutcp::TransportParams params;
  params.ackDelayExponent = 21;
  ZuCHECK(Zutcp::InitialPkt::write(packet, sizeof(packet), cid, cid, 0, 1, &params, 0) < 0,
    "invalid local transport parameters are not emitted");
}

// Owner queues deliberately delay the ACK's Tx->Rx publication. This exercises
// the real server codec/recovery/lifecycle without a kernel transport backend.
struct GateApp : Zutcp::Hub<GateApp> {
  Zi::IOBufAllocator *dataTxAllocator() const { return Zutcp::inlineAllocator(); }
  using Fn = ZmFn<>;
  ZmQueue<Fn> rx, tx;
  Zi::IOBufBuiltinAllocator<Zi::IOBufType::App, Zutcp::PacketBuf, Zutcp::BufSize> packets;
  ZmRef<ZiIOBuf> response;
  ZmFn<> onInvoke;
  unsigned owner = 1, shortSent = 0, longSent = 0;
  Zutcp::TCPHeader responseTCP;
  bool native = false, inlineRx = false;
  ZuTime clock;
  bool nativeTx() const { return native; }
  ZuTime transportNow() const { return *clock ? clock : Zutcp::Hub<GateApp>::transportNow(); }
  ZiMultiplex multiplex;

  GateApp() {
    ZiAssert(init(Zutcp::HubParams{&multiplex}.maxData(8192)), "Zutcp", (),
      "setup gate fixture initialization failed", abort());
  }
  ~GateApp() { response = nullptr; final(); }
  bool rxInvoked() const { return owner == 1; }
  bool txInvoked() const { return owner == 2; }
  template <typename L> void rxRun(L l) {
    if (inlineRx && rxInvoked()) (void)l();
    else rx.push([l = ZuMv(l)]() mutable { (void)l(); });
  }
  template <typename L> void txRun(L l) { tx.push([l = ZuMv(l)]() mutable { (void)l(); }); }
  template <typename L> void txInvoke(L l) {
    if (onInvoke) {
      auto fn = ZuMv(onInvoke);
      fn();
    }
    if (txInvoked()) (void)l(); else txRun(ZuMv(l));
  }
  template <typename T, typename L> void txInvoke(T *, L l) { txInvoke(ZuMv(l)); }
  template <typename L> void rxInvoke(L l) {
    if (rxInvoked()) (void)l(); else rxRun(ZuMv(l));
  }
  template <typename T, typename L> void rxInvoke(T *, L l) { rxInvoke(ZuMv(l)); }
  void runRx() { owner = 1; while (auto fn = rx.shift()) fn(); }
  void runTx() { owner = 2; while (auto fn = tx.shift()) fn(); owner = 1; }
  void drain() { do { runTx(); runRx(); } while (tx.count_() || rx.count_()); }
  void stepTx() { owner = 2; if (auto fn = tx.shift()) fn(); owner = 1; }
  void refreshLinkRoutes(void *) { }
  void retireLinkRoutes(void *) { }
  void disconnected(void *, bool) { }
  bool sendPkt(const ZmRef<ZiIOBuf> &) { return true; }
  bool sendFrame(const Zutcp::SentFrameRef &) { return true; }
  ZmRef<Zutcp::PacketBuf> allocTxPkt_(bool = false) {
    auto buf = packets.allocBuf();
    return ZmRef<Zutcp::PacketBuf>{static_cast<Zutcp::PacketBuf *>(buf.ptr())};
  }
  bool sendPktRaw(ZmRef<ZiIOBuf> buf, ZiSockAddr, Zutcp::EcnMark::T, bool,
      const Zutcp::IPPath &) {
    Zutcp::InitialPkt packet;
    if (Zutcp::Pkt::isLong(buf->cspan())) {
      ++longSent;
      if (packet.parse(buf->cspan()) > 0 && packet.params) {
	responseTCP = static_cast<Zutcp::PacketBuf *>(buf.ptr())->tcp;
	response = ZuMv(buf);
      }
    } else ++shortSent;
    return true;
  }
};

struct AbortCli : ZmNHeap<AbortCli>, Zutcp::CliCxn<GateApp, AbortCli> {
friend ZuStringT<"Zutcp.Handshake.AbortCli"> ZmNHeapID(AbortCli *);
  using Base = Zutcp::CliCxn<GateApp, AbortCli>;
  using State = Base::Base;
  unsigned disconnectedCount = 0, endpointDownCount = 0, connectFailures = 0, connects = 0;
  bool reconnectOnDisconnect = false;
  unsigned setups = 0;
  ZiSockAddr setupTarget;
  unsigned resetPackets = 0, flushRequests = 0;
  Zutcp::TCPHeader lastReset;
  void connect_1(ZiIP ip, uint16_t port) {
    ++setups;
    setupTarget = ZiSockAddr{ip, port};
    Base::connect_1(ZuMv(ip), port);
  }
  explicit AbortCli(GateApp *app) : Base{app} { Base::configLocalParams(app); }
  void prepare() {
    Base::resetLink_();
    Base::configLocalParams(app());
    (void)Base::setTCPOriginsRx_({UINT32_MAX, UINT32_MAX, true, true, false});
  }
  bool originsForTest(Zutcp::TCPOrigins origins) { return State::setTCPOriginsRx_(origins); }
  void installOriginsForTest(Zutcp::TCPOrigins origins, uint64_t gen) {
    State::installTCPOriginsTx_(origins, gen);
  }
  Zutcp::TCPHeader headerForTest() const { return State::tcpHeader(); }
  bool pendingOriginForTest(uint32_t local) {
    State::resetLink_();
    Zutcp::CxnID original{"original"}, localCID{"client00"};
    State::setRuntimeCIDs_(original, localCID, original);
    State::configLocalParams(app());
    return State::setTCPOriginsRx_({local, 0, true, false, false}) && State::startHandshake();
  }
  bool synForTest() {
    Zutcp::PktBuild payload;
    int n = Zutcp::FrameCodec::writePing(payload.scratch(), payload.scratchAvail());
    if (n <= 0 || !payload.commitScratch(unsigned(n))) return false;
    State::TxPktRefs refs;
    return State::sendInitialPkt(State::CIDSel::Initial, State::CIDSel::Local, 2, true,
      payload, {}, {}, &refs, true,
      [this] { return app()->allocTxPkt_(); }, [](auto, ZiSockAddr) { return true; });
  }
  bool receiveShortForTest(Zutcp::Datagram d) {
    return State::receiveShortPkt(d, 0, d.buf->length,
      [this](auto level, auto pn, ZuBSpan frames, auto addr, const auto &buf,
	State::RxAckMeta &ack, auto *qlog) {
	return State::consumeFrames(level, pn, frames, ZuMv(addr), buf, ack, qlog,
	  [](auto, const Zutcp::Frame &, ZiSockAddr) { return true; });
      });
  }
  bool shortSeenForTest(uint64_t pn) const {
    return State::rxPktSeen(Zutcp::PktNumSpace::AppData, pn);
  }
  void delayedReady() { Base::endpointReady_(this); }
  void gracefulForTest(Base::CloseFn fn) {
    Base::closeAfterDisconnect(true, false, {}, ZuMv(fn));
  }
  void finishForTest() { Base::nativeFinished_(); }
  bool timerWork() const { return Base::timersActive(); }
  using State::closeErrorRx_;
  using State::closeErrorTx_;
  bool appCloseTxForTest() const { return State::appCloseOnDisconnect(); }
  void queueTxFlush() { ++flushRequests; }
  void queueTxFlush(ZiSockAddr) { ++flushRequests; }
  void establishForTest() {
    Zutcp::CxnID cid{"idlecid0"};
    State::setRuntimeCIDs_(cid, cid, cid);
    State::startHandshake();
    State::establish_();
  }
  void idleForTest(ZuTime timeout) { State::startIdleTimerTx(timeout); }
  void idleTickForTest() { State::cxnTimer(Zutcp::CxnTimer::Idle); }
  bool sendTCPReset_() {
    return State::sendTCPResetPkt_([this] { return app()->allocTxPkt_(); },
      [this](auto buf) {
	++resetPackets;
	lastReset = buf->tcp;
	return app()->txInvoked();
      });
  }
  void connected(Zutcp::Connected) { ++connects; }
  int process(Zutcp::Reader &) { return 0; }
  void disconnected(bool) {
    ++disconnectedCount;
    if (reconnectOnDisconnect) {
      reconnectOnDisconnect = false;
      Base::connect(Zutcp::Host{"198.18.0.77"}, 18191);
    }
  }
  void endpointDown() { ++endpointDownCount; }
  void connectFailed(bool) { ++connectFailures; }
};

struct CloseSrv : ZmNHeap<CloseSrv>, Zutcp::SrvCxn<GateApp, CloseSrv> {
friend inline ZuStringT<"Zutcp.Handshake.CloseSrv"> ZmNHeapID(CloseSrv *);
  using Base = Zutcp::SrvCxn<GateApp, CloseSrv>;
  using State = Base::Base;
  unsigned resetPackets = 0;
  Zutcp::TCPHeader lastReset;
  explicit CloseSrv(GateApp *app) : Base{app} { State::configLocalParams(app); }
  int process(Zutcp::Reader &) { return 0; }
  void connected(Zutcp::Connected) { }
  void prepare() {
    State::resetLink_();
    State::configLocalParams(app());
    (void)State::setTCPOriginsRx_({UINT32_MAX, UINT32_MAX, true, true, true});
    Zutcp::CxnID cid{"idlecid0"};
    State::setRuntimeCIDs_(cid, cid, cid);
    State::startHandshake();
    State::establish_();
  }
  void idleForTest(ZuTime timeout) { State::startIdleTimerTx(timeout); }
  void idleTickForTest() { State::cxnTimer(Zutcp::CxnTimer::Idle); }
  bool timerWork() const { return State::timersActive(); }
  using State::closeErrorRx_;
  using State::closeErrorTx_;
  bool appCloseTxForTest() const { return State::appCloseOnDisconnect(); }
  bool sendTCPReset_() {
    return State::sendTCPResetPkt_([this] { return app()->allocTxPkt_(); },
      [this](auto buf) {
	++resetPackets;
	lastReset = buf->tcp;
	return app()->txInvoked();
      });
  }
};

static void testNativeZeroDisconnect()
{
  ZuTestScope(testNativeZeroDisconnect);
  using NumericResult = decltype(ZuDeclVal<AbortCli &>().disconnect(0));
  ZuCHECK((ZuIsSame<NumericResult, bool>{}), "literal zero must select numeric acceptance");
  GateApp app;
  app.native = true;
  ZmRef<AbortCli> client = new AbortCli{&app};
  client->prepare();
  client->establishForTest();
  ZmRef<CloseSrv> server = new CloseSrv{&app};
  server->prepare();
  app.drain();
  // No real Endpoint is installed for the client: exercise its numeric-zero
  // graceful entry through the same protected seam used by the queued close test.
  unsigned completed = 0;
  client->gracefulForTest([&completed] { ++completed; });
  client->disconnect(0);
  server->disconnect();
  app.runTx();
  app.owner = 2;
  bool waiting = client->finSent() && server->finSent() &&
    !client->txCompleted() && !server->txCompleted();
  app.owner = 1;
  ZuCHECK(waiting && !completed && !client->disconnectedCount &&
      !client->disconnecting_() && !server->disconnecting_() &&
      client->txAdmissionClosed() && server->txAdmissionClosed(),
    "zero/default close must keep both native roles alive while FIN is unacknowledged");
  auto packet = app.packets.allocBuf();
  int n = Zutcp::FrameCodec::writeData(packet->data(), packet->avail(), 0, "peer-data", false);
  Zutcp::Frame frame;
  unsigned used = 0;
  bool decoded = n > 0 && !Zutcp::FrameCodec::parse({packet->data(), unsigned(n)}, frame, used);
  packet->length = n > 0 ? unsigned(n) : 0;
  ZuCHECK(decoded && server->processFrame(frame, packet) == 0 &&
      server->reader().length() == 9 && server->reader().span().data() == frame.payload.data() &&
      !server->disconnecting_(),
    "local default FIN must preserve independent peer DATA and original Rx backing");
  client->abort();
  server->abortLocalRx_(0, [server] { server->tcpResetAbort_(false); });
  app.drain();
  ZuCHECK(completed == 1 && client->disconnectedCount == 1 && client->endpointDownCount == 1,
    "numeric zero must preserve normal client notification and queued completion");
  client = nullptr;
  server = nullptr;
  app.drain();
}

static void testNativeErrorDisconnect()
{
  ZuTestScope(testNativeErrorDisconnect);
  for (unsigned wide = 0; wide < 2; ++wide) {
    GateApp app;
    app.native = true;
    ZmRef<AbortCli> client = new AbortCli{&app};
    client->prepare();
    client->establishForTest();
    ZmRef<CloseSrv> server = new CloseSrv{&app};
    server->prepare();
    app.drain();
    uint64_t error = wide ? (uint64_t(1)<<40) | 0x41 : 7;
    bool cliAccepted = wide ? client->disconnect(error) : client->disconnect(7);
    bool srvAccepted = wide ? server->disconnect(error) : server->disconnect(7);
    client->disconnect(error);
    server->disconnect(error);
    app.runTx();
    app.owner = 2;
    bool reset = client->resetPackets == 1 && server->resetPackets == 1 &&
      client->lastReset.flags == 0x14 && server->lastReset.flags == 0x14 &&
      !client->finSent() && !server->finSent() &&
      client->closeErrorTx_() == error && server->closeErrorTx_() == error &&
      client->appCloseTxForTest() && server->appCloseTxForTest();
    app.owner = 1;
    app.drain();
    bool notified = client->disconnectedCount == 1 && client->endpointDownCount == 1 &&
      server->closed() && client->closeErrorRx_() == error &&
      server->closeErrorRx_() == error;
    unsigned completed = 0;
    client->abort([&completed] { ++completed; });
    client->disconnect(error);
    server->disconnect(error);
    app.drain();
    app.owner = 2;
    bool quiet = !client->timerWork() && !server->timerWork() &&
      client->resetPackets == 1 && server->resetPackets == 1 &&
      client->closeErrorTx_() == error && server->closeErrorTx_() == error;
    app.owner = 1;
    ZuCHECK(cliAccepted && srvAccepted && reset && notified && quiet && completed == 1 &&
	client->disconnectedCount == 1 && client->endpointDownCount == 1 &&
	server->closed() && server->disconnecting_(),
      "literal/full-width numeric errors must reset native roles, retain local application "
      "error and notify once through repeated close and late abort completion");
    client = nullptr;
    server = nullptr;
    app.drain();
  }
}

static void testPendingErrorDisconnect()
{
  ZuTestScope(testPendingErrorDisconnect);
  GateApp app;
  app.native = true;
  ZmRef<AbortCli> client = new AbortCli{&app};
  client->prepare(); // Keep the current generation's Tx reset queued.
  uint64_t error = (uint64_t(1)<<40) | 0x42;
  unsigned completed[2] = {};
  bool accepted = client->disconnect(error);
  client->abort([&completed] { ++completed[0]; });
  app.runRx();
  bool pending = !client->disconnecting_() && !completed[0];
  app.drain();
  client->abort([&completed] { ++completed[1]; });
  client->disconnect(error);
  app.drain();
  app.owner = 2;
  bool quiet = !client->timerWork() && !client->resetPackets &&
    client->closeErrorTx_() == error && client->appCloseTxForTest();
  app.owner = 1;
  ZuCHECK(accepted && pending && quiet && completed[0] == 1 && completed[1] == 1 &&
      client->disconnectedCount == 1 && client->endpointDownCount == 1 &&
      client->closeErrorRx_() == error,
    "pending numeric cancellation must preserve its error and both abort completions "
    "without inventing a peer reset or duplicating notification");
  client = nullptr;
  app.drain();
}

struct ErrorTxAllocator : Zi::IOBufAllocator {
  alignas(ZiIOBuf_Align) uint8_t bytes[512];
  mutable unsigned returned = 0;
  mutable bool valid = true;
  explicit ErrorTxAllocator(ZiMultiplex *mx) :
    IOBufAllocator{mx, Zi::IOBufType::TxTCP, 512, 256} {
    memset(bytes, 0xa5, sizeof(bytes));
  }
  uint8_t *alloc(unsigned) const override { return nullptr; }
  void free(uint8_t *p, unsigned n, bool registered) const override {
    valid &= p == bytes && n == sizeof(bytes) && registered;
    ++returned;
  }
  ZmRef<ZiIOBuf> allocBuf(void *, uint32_t, uint32_t) override { return nullptr; }
};

static void testNativeQueueError()
{
  ZuTestScope(testNativeQueueError);
  GateApp app;
  app.native = true;
  ZmRef<AbortCli> client = new AbortCli{&app};
  client->prepare();
  client->establishForTest();
  app.drain();
  client->txQueueMax(1);
  unsigned errors = 0, callbackOwner = 0;
  client->txErrorFn(ZiTxErrorFn{[&app, &errors, &callbackOwner](ZeException &) {
    ++errors;
    callbackOwner = app.owner;
    return false;
  }});
  app.owner = 3;
  auto buf = app.packets.allocBuf();
  buf->length = 4;
  bool admitted = client->enqueue(buf);
  auto held = app.tx.shift(); // Admission exists, but its backing has not reached Tx.
  bool refused = !client->enqueue(buf);
  bool deferred = !client->disconnecting_() && !client->disconnectedCount &&
    !client->endpointDownCount;
  app.runRx();
  app.runTx();
  app.owner = 2;
  bool reset = client->resetPackets == 1 && client->lastReset.flags == 0x14 &&
    !client->finSent();
  app.owner = 1;
  app.drain();
  app.tx.push(ZuMv(held));
  app.drain();
  app.owner = 2;
  bool quiet = !client->txQueueCount() && !client->txRangeCount() && !client->timerWork();
  app.owner = 1;
  unsigned completed = 0;
  client->abort([&completed] { ++completed; });
  app.drain();
  ZuCHECK(admitted && refused && deferred && reset && quiet && errors == 1 &&
      callbackOwner == 3 && client->disconnectedCount == 1 &&
      client->endpointDownCount == 1 && completed == 1,
    "a terminating native queue error from an arbitrary producer must reset and drain "
    "through the client role before a late admitted handoff can install data");
  client = nullptr;
  app.drain();
}

static void testNativeBackingError()
{
  ZuTestScope(testNativeBackingError);
  for (unsigned serverRole = 0; serverRole < 2; ++serverRole) {
    GateApp app;
    app.native = true;
    ZmRef<AbortCli> client = new AbortCli{&app};
    client->prepare();
    client->establishForTest();
    ZmRef<CloseSrv> server = new CloseSrv{&app};
    server->prepare();
    app.drain();
    ZiMultiplex foreignMx;
    ErrorTxAllocator allocator{&foreignMx};
    ZmRef<ZiIOBuf> buf = new Zi::IOBufStub<ZiIOBuf>{
      allocator.bytes, unsigned(sizeof(allocator.bytes)), &allocator, nullptr,
      unsigned(8), false, true};
    buf->skip = 256;
    memcpy(allocator.bytes + 256, "original", 8);
    unsigned errors = 0, callbackOwner = 0;
    auto error = ZiTxErrorFn{[&app, &errors, &callbackOwner](ZeException &) {
      ++errors;
      callbackOwner = app.owner;
      return false;
    }};
    if (serverRole) server->txErrorFn(ZuMv(error)); else client->txErrorFn(ZuMv(error));
    app.owner = 2;
    bool refused = serverRole ? !server->enqueue_(buf) : !client->enqueue_(buf);
    bool returned = !client->txQueueCount() && !server->txQueueCount() && !allocator.returned;
    app.owner = 1;
    bool deferred = !client->disconnecting_() && !server->disconnecting_() &&
      !client->disconnectedCount;
    app.drain();
    app.owner = 2;
    bool reset = serverRole ? server->resetPackets == 1 && server->lastReset.flags == 0x14 :
      client->resetPackets == 1 && client->lastReset.flags == 0x14;
    bool quiet = serverRole ? !server->timerWork() && !server->txRangeCount() :
      !client->timerWork() && !client->txRangeCount();
    app.owner = 1;
    bool retired = serverRole ? server->closed() && server->disconnecting_() :
      client->disconnectedCount == 1 && client->endpointDownCount == 1;
    bool original = !memcmp(allocator.bytes + 256, "original", 8) && !allocator.returned;
    buf = nullptr;
    ZuCHECK(refused && returned && deferred && reset && quiet && retired && original &&
	errors == 1 && callbackOwner == 2 && allocator.valid && allocator.returned == 1,
      "foreign registered backing errors must retire admission before a once-only native "
      "role abort while the rejected original lease stays with its caller");
    client->abort();
    server->abortLocalRx_(0, [server] { server->tcpResetAbort_(false); });
    app.drain();
    client = nullptr;
    server = nullptr;
    app.drain();
  }
}

static void testNativeErrorReplacement()
{
  ZuTestScope(testNativeErrorReplacement);
  for (unsigned queued = 0; queued < 2; ++queued) {
    GateApp app;
    app.native = true;
    ZmRef<AbortCli> client = new AbortCli{&app};
    client->prepare();
    client->establishForTest();
    app.drain();
    client->txQueueMax(1);
    unsigned errors = 0;
    client->txErrorFn(ZiTxErrorFn{[client, &app, &errors, queued](ZeException &) {
      ++errors;
      if (queued) app.rxRun([client] { client->prepare(); });
      else { client->prepare(); app.runTx(); }
      return false;
    }});
    auto generation = client->dataGen();
    app.owner = queued ? 3 : 1;
    auto buf = app.packets.allocBuf();
    buf->length = 4;
    bool admitted = client->enqueue(buf);
    auto held = app.tx.shift();
    bool refused = !client->enqueue(buf);
    app.drain();
    app.tx.push(ZuMv(held));
    app.drain();
    app.owner = 2;
    bool open = !client->txAdmissionClosed() && !client->finSent() &&
      !client->txQueueCount() && !client->txRangeCount() && !client->resetPackets;
    app.owner = 1;
    ZuCHECK(admitted && refused && open && errors == 1 &&
	client->dataGen() == generation + 1 && !client->disconnecting_() &&
	!client->disconnectedCount && !client->endpointDownCount,
      "an error callback replacement before return or before Rx dispatch must not "
      "abort or close producer admission in the replacement generation");
    client->txErrorFn({}); // Release the callback's fixture reference before owner teardown.
    client->abort();
    app.drain();
    client = nullptr;
    app.drain();
  }
}

static void testNativeErrorPolicy()
{
  ZuTestScope(testNativeErrorPolicy);
  for (unsigned policy = 0; policy < 2; ++policy) {
    GateApp app;
    app.native = true;
    ZmRef<AbortCli> client = new AbortCli{&app};
    client->prepare();
    app.drain();
    client->txQueueMax(1);
    unsigned errors = 0;
    if (policy) client->txErrorFn(ZiTxErrorFn{[&errors](ZeException &) {
      ++errors;
      return true;
    }});
    app.owner = 3;
    auto buf = app.packets.allocBuf();
    buf->length = 4;
    bool admitted = client->enqueue(buf);
    auto held = app.tx.shift();
    bool refused = !client->enqueue(buf);
    bool live = !client->disconnecting_() && !client->disconnectedCount && !app.rx.count_();
    client->closeTxAdmission();
    bool closed = !client->enqueue(buf) && errors == policy;
    client->txErrorFn(ZiTxErrorFn{[&errors](ZeException &) { ++errors; return false; }});
    bool benign = !client->enqueue(buf) && errors == policy && !app.rx.count_();
    app.owner = 1;
    ZuCHECK(admitted && refused && live && closed && benign && client->txQueueCount() == 1,
      "true or absent error policy must remain nonterminating and a closed full queue "
      "must refuse benignly without calling even a terminating error callback");
    client->abort();
    app.drain();
    app.tx.push(ZuMv(held));
    app.drain();
    client = nullptr;
    app.drain();
  }
}

static void testTCPOriginOwners()
{
  ZuTestScope(testTCPOriginOwners);
  GateApp app;
  app.native = true;
  ZmRef<AbortCli> link = new AbortCli{&app};
  // Constructor has no known origin: zero is a valid selected ISN.
  Zutcp::TCPOrigins first{0, 0xffffffff, true, true, false};
  uint64_t old = link->dataGen();
  bool chosen = link->originsForTest(first) && link->originsForTest(first) &&
    !link->originsForTest({1, 0xffffffff, true, true, false});
  app.runTx();
  app.owner = 2;
  auto initial = link->headerForTest();
  app.owner = 1;
  link->prepare(); // Reset/queue the new generation's logical-base0 fixture anchors.
  app.runTx();
  app.owner = 2;
  link->installOriginsForTest(first, old);
  auto replaced = link->headerForTest();
  app.owner = 1;
  ZuCHECK(chosen && initial.seq == 1 && !initial.ack && !replaced.seq && !replaced.ack &&
      link->dataGen() != old,
    "zero ISN stays valid, conflicts refuse and stale Tx posts cannot replace "
    "a new incarnation's selected anchors");
  link->abort();
  app.drain();
  link = nullptr;
  app.drain();
}

static void testNativeIdleReset()
{
  ZuTestScope(testNativeIdleReset);
  GateApp app;
  app.native = true;
  app.clock = Zm::now() + ZuTime{1000};
  ZmRef<AbortCli> client = new AbortCli{&app};
  client->prepare();
  client->establishForTest();
  ZmRef<CloseSrv> server = new CloseSrv{&app};
  server->prepare();
  app.drain();
  app.owner = 2;
  client->idleTickForTest();
  server->idleTickForTest();
  bool disabled = !client->resetPackets && !server->resetPackets;
  client->idleForTest(ZuTime{1});
  server->idleForTest(ZuTime{1});
  app.clock += ZuTime{100};
  client->idleTickForTest();
  server->idleTickForTest();
  bool sent = client->resetPackets == 1 && server->resetPackets == 1 &&
    client->lastReset.flags == 0x14 && server->lastReset.flags == 0x14;
  app.owner = 1;
  app.drain();
  app.owner = 2;
  bool quiet = !client->timerWork() && !server->timerWork();
  app.owner = 1;
  ZuCHECK(disabled && sent && quiet && client->disconnectedCount == 1 &&
      client->endpointDownCount == 1 && client->disconnecting_() && server->disconnecting_(),
    "configured idle expiry must submit meaningful RST before once-only native owner drain");
  client = nullptr;
  server = nullptr;
  app.drain();
}

static void testFinRequestReplacement()
{
  ZuTestScope(testFinRequestReplacement);
  for (unsigned retained = 0; retained < 2; ++retained) {
    GateApp app;
    app.native = true;
    ZmRef<AbortCli> client = new AbortCli{&app};
    client->prepare();
    app.drain();
    auto generation = client->dataGen();
    unsigned errors = 0;
    client->txErrorFn(ZiTxErrorFn{[&errors](ZeException &) { ++errors; return false; }});
    // Complete replacement after the old payload reservation, before the
    // public caller has posted its FIN request. Both sends must keep old gen.
    app.onInvoke = [client, &app] {
      client->prepare();
      app.runTx();
    };
    bool accepted;
    if (retained) {
      auto buf = app.packets.allocBuf();
      buf->length = 4;
      accepted = client->send(buf, true);
    } else accepted = client->send("data", true);
    app.drain();
    app.owner = 2;
    bool open = !client->finSent() && !client->txAdmissionClosed() && !client->txQueueCount();
    app.owner = 1;
    ZuCHECK(accepted && open && !errors && !client->disconnecting_() &&
	client->dataGen() == generation + 1,
      "an old accepted payload/FIN request must not close admission or FIN in its replacement");
    client->abort();
    app.drain();
    client = nullptr;
    app.drain();
  }
}

static void testPendingCloseCallbacks()
{
  ZuTestScope(testPendingCloseCallbacks);
  GateApp app;
  app.native = true;
  ZmRef<AbortCli> client = new AbortCli{&app};
  client->prepare(); // Deliberately hold the new generation's queued Tx reset.
  unsigned completed[3] = {};
  client->disconnect([&completed] { ++completed[0]; });
  client->abort([&completed] { ++completed[1]; });
  app.runRx();
  ZuCHECK(!client->disconnecting_() && !completed[0] && !completed[1],
    "pending graceful cancel must order reset/abort Tx work before starting the owner drain");
  app.drain();
  client->abort([&completed] { ++completed[2]; });
  app.drain();
  app.owner = 2;
  bool quiet = !client->timerWork();
  app.owner = 1;
  ZuCHECK(completed[0] == 1 && completed[1] == 1 && completed[2] == 1 &&
      client->disconnectedCount == 1 && client->endpointDownCount == 1 &&
      !client->resetPackets && quiet,
    "pending disconnect and every abort completion must finish once even after the same drain");
  client = nullptr;
  app.drain();
}

static void testRetainedAdmissionFinWake()
{
  ZuTestScope(testRetainedAdmissionFinWake);
  GateApp app;
  app.native = true;
  ZmRef<AbortCli> client = new AbortCli{&app};
  client->prepare();
  app.drain();
  auto buf = app.packets.allocBuf();
  buf->length = 4;
  ZuCHECK(client->send(buf, false) && client->txQueueCount() == 1,
    "public retained send must reserve its handoff before returning success");
  auto old = app.tx.shift(); // Keep that pre-admitted old-generation transfer delayed.
  client->prepare();
  app.runTx();
  app.owner = 2;
  client->txCredit(8);
  client->fin();
  bool blocked = !client->finReady() && client->txQueueCount() == 1;
  unsigned before = client->flushRequests;
  app.owner = 1;
  ZuCHECK(!client->send(buf, false) && client->txQueueCount() == 1,
    "a new retained offer after FIN must fail without consuming another reservation");
  app.tx.push(ZuMv(old));
  app.runTx();
  app.owner = 2;
  bool resumed = !client->txQueueCount() && client->finReady() &&
    client->flushRequests > before;
  app.owner = 1;
  ZuCHECK(blocked && resumed && !client->disconnecting_(),
    "stale handoff retirement must wake the new FIN without accepting old payload");
  client->abort();
  app.drain();
  client = nullptr;
  app.drain();
}

static void testAbortBeforeReady()
{
  ZuTestScope(testAbortBeforeReady);
  GateApp app;
  app.native = true;
  ZmRef<AbortCli> link = new AbortCli{&app};
  link->prepare();
  app.drain();
  uint64_t gen = link->dataGen();
  unsigned completed[2] = {};
  link->abort([&completed] { ++completed[0]; });
  link->abort([&completed] { ++completed[1]; });
  // Route setup can publish endpoint readiness while the abort's Tx turn is
  // still queued. It must not reset runtime generation and lose completion.
  link->delayedReady();
  ZuCHECK(!completed[0] && !completed[1] && !link->readyCount() && link->dataGen() == gen,
    "late endpoint-ready must not start a new handshake during an already requested abort");
  app.drain();
  ZuCHECK(completed[0] == 1 && completed[1] == 1 && link->disconnectedCount == 1 &&
      !app.shortSent && link->disconnecting_(),
    "each abort completion must run once while native disconnect notifies once per incarnation");
  link->prepare();
  app.drain();
  unsigned nextCompleted = 0;
  link->abort([&nextCompleted] { ++nextCompleted; });
  app.drain();
  ZuCHECK(nextCompleted == 1 && link->disconnectedCount == 2 && !app.shortSent,
    "a new native generation must get its own once-only abort notification");
  link = nullptr;
  app.drain();
}

static void testNativeGracefulCallbacks()
{
  ZuTestScope(testNativeGracefulCallbacks);
  for (unsigned finishFirst = 0; finishFirst < 2; ++finishFirst) {
    GateApp app;
    app.native = true;
    ZmRef<AbortCli> link = new AbortCli{&app};
    link->prepare();
    app.drain();
    unsigned completed[3] = {};
    if (finishFirst) {
      // FIN completion posts its Rx drain, then an application close queues
      // FIN work behind that completed Tx turn but before Rx sees retirement.
      app.owner = 2;
      link->finishForTest();
      app.owner = 1;
    }
    link->gracefulForTest([&completed] { ++completed[0]; });
    link->disconnect([&completed] { ++completed[1]; });
    if (!finishFirst) {
      app.runTx();
      app.owner = 2;
      bool waiting = link->finSent() && !link->txCompleted();
      app.owner = 1;
      ZuCHECK(waiting && !link->disconnecting_() &&
	  !completed[0] && !completed[1] && !link->disconnectedCount,
	"repeated graceful requests must retain both callbacks while local FIN is unacknowledged");
      app.owner = 2;
      link->finishForTest();
      app.owner = 1;
    } else {
      // Run Rx first: the already completed transport starts draining before
      // its later queued FIN request can run. That request must not rearm work.
      app.runRx();
      ZuCHECK(link->disconnecting_() && !completed[0] && !completed[1],
	"queued native finish must start owner drain before late Tx close work");
    }
    app.drain();
    app.owner = 2;
    bool quiet = !link->timerWork();
    app.owner = 1;
    ZuCHECK(completed[0] == 1 && completed[1] == 1 &&
	link->disconnectedCount == 1 && link->endpointDownCount == 1 && quiet,
      "automatic native retirement must preserve every graceful completion once and leave no timer");
    link->disconnect([&completed] { ++completed[2]; });
    app.drain();
    app.owner = 2;
    quiet = !link->timerWork();
    app.owner = 1;
    ZuCHECK(completed[0] == 1 && completed[1] == 1 && completed[2] == 1 &&
	link->disconnectedCount == 1 && link->endpointDownCount == 1 && quiet,
      "a graceful completion after retirement must complete without another notification or timer");
    link = nullptr;
    app.drain();
  }
}

static void testAbortReentrantConnect()
{
  ZuTestScope(testAbortReentrantConnect);
  GateApp app;
  app.native = app.inlineRx = true;
  ZmRef<AbortCli> link = new AbortCli{&app};
  link->prepare();
  app.drain();
  link->reconnectOnDisconnect = true;
  unsigned completed[2] = {};
  bool ordered = true;
  link->abort([&link, &completed, &ordered] {
    ++completed[0];
    ordered &= link->endpointDownCount == 1;
  });
  link->abort([&link, &completed, &ordered] {
    ++completed[1];
    ordered &= link->endpointDownCount == 1;
  });
  app.drain();
  // No native Stack is installed: the public reentrant connect reaches its
  // normal setup failure cleanup after the old drain and all completions.
  ZuCHECK(ordered && completed[0] == 1 && completed[1] == 1 &&
      link->disconnectedCount == 2 && link->endpointDownCount == 2 &&
      link->connectFailures == 1,
    "reentrant connect must join the old drain before the failed new attempt cleans itself up");
  link->abort();
  app.drain();
  ZuCHECK(link->disconnectedCount == 2 && link->endpointDownCount == 2,
    "repeated abort after failed setup must not repeat that generation's cleanup");
  link = nullptr;
  app.drain();
}

static void testCoalescedNativeConnect()
{
  ZuTestScope(testCoalescedNativeConnect);
  for (unsigned invalid = 0; invalid < 2; ++invalid) {
    GateApp app;
    app.native = true;
    ZmRef<AbortCli> link = new AbortCli{&app};
    link->prepare();
    app.drain();
    uint64_t generation = link->dataGen();
    link->connect(Zutcp::Host{"198.18.0.77"}, 18191);
    link->connect(Zutcp::Host{"198.18.0.88"}, 18192);
    if (invalid) link->connect(Zutcp::Host{"198.18.0.99"}, 0);
    app.drain();
    // No backend is installed, so the one adopted attempt fails Endpoint::init.
    // A second continuation must neither reset its token nor mix target fields.
    ZiSockAddr expected{ZiIP{"198.18.0.77"}, 18191};
    ZuCHECK(link->dataGen() == generation + 1 && link->setups == 1 &&
	link->setupTarget == expected && link->port() == (invalid ? 0 : 18192) &&
	link->connectFailures == invalid + 1 && link->disconnectedCount == 1 &&
	link->endpointDownCount == 1,
      "coalesced connect must claim one validated tuple and preserve later configuration");
    link = nullptr;
    app.drain();
  }
}

static void testNativeSetupFailureDrain()
{
  ZuTestScope(testNativeSetupFailureDrain);
  GateApp app;
  app.native = true;
  ZmRef<AbortCli> link = new AbortCli{&app};
  link->prepare();
  app.drain();
  // Caller validation rejection is not a new adopted attempt and must not
  // cancel an old transport or coalesce separate invalid calls by dataGen.
  link->connect({}, 0, {});
  link->connect({}, 0, {});
  app.drain();
  ZuCHECK(link->connectFailures == 2 && !link->disconnecting_() &&
      !link->disconnectedCount && !link->endpointDownCount,
    "distinct invalid connect calls must notify without claiming an adopted failure drain");
  link->endpointFailed(false);
  link->endpointFailed(false);
  ZuCHECK(link->connectFailures == 2 && !link->disconnectedCount,
    "an adopted setup failure must defer notification until its owner drain finishes");
  app.drain();
  ZuCHECK(link->connectFailures == 3 && link->disconnectedCount == 1 &&
      link->endpointDownCount == 1 && link->disconnecting_(),
    "failed route/setup must clean up once even when connectFailed only records the failure");
  link->abort();
  app.drain();
  ZuCHECK(link->connectFailures == 3 && link->disconnectedCount == 1 &&
      link->endpointDownCount == 1,
    "repeated abort must not release or notify the failed attempt twice");
  link = nullptr;
  app.drain();
}

struct GateCxn : ZmNHeap<GateCxn>, Zutcp::SrvCxn<GateApp, GateCxn> {
friend ZuStringT<"Zutcp.Handshake.Gate"> ZmNHeapID(GateCxn *);
  unsigned connections = 0, deliveries = 0;
  bool early = false;
  using Base = Zutcp::SrvCxn<GateApp, GateCxn>;

  using Base::receivedRouted;
  enum { RecoveryScanBatch = Base::RecoveryScanBatch };
  GateCxn(GateApp *app) : Base{app} { Base::configLocalParams(app); }
  void connected(Zutcp::Connected) { ++connections; }
  int process(Zutcp::Reader &reader) {
    auto n = reader.length();
    if (!n) return 0;
    ++deliveries;
    early |= !established();
    reader.clean();
    if (!retireRx(n)) return -1;
    const uint8_t reply[] = {'r'};
    return send(ZuBSpan{reply, sizeof(reply)}, false) ? 1 : -1;
  }
  void restartReady(const Zutcp::TransportParams &params) {
    Base::resetLink_();
    Base::configLocalParams(app());
    Base::startHandshake();
    Base::peerParams(params);
  }
  bool responseBatch(unsigned first, unsigned count) {
    app()->owner = 2;
    bool added = true;
    Base::setTxPN(Zutcp::PktNumSpace::Initial, first + count);
    for (unsigned i = 0; i < count; ++i)
      added &= Base::recordTxPkt(Zutcp::PktNumSpace::Initial, first + i, 10,
	Zutcp::SentFrameRef{}, true);
    app()->owner = 1;
    return added;
  }
  void reset() { Base::resetLink_(); }
  bool originForTest(uint32_t local) {
    return Base::setTCPOriginsRx_({local, 0, true, false, true});
  }
  bool initialSeenForTest(uint64_t pn) const {
    return Base::rxPktSeen(Zutcp::PktNumSpace::Initial, pn);
  }
};

static Zutcp::Datagram gateInput(GateApp &app, ZmRef<ZiIOBuf> buf)
{
  Zutcp::Datagram datagram{ZuMv(buf), ZiSockAddr{ZiIP{"192.0.2.1"}, 3000}};
  datagram.local = ZiSockAddr{ZiIP{"192.0.2.2"}, 4000};
  return datagram;
}

static ZmRef<ZiIOBuf> gateInitial(GateApp &app, const Zutcp::CxnID &dcid,
  const Zutcp::CxnID &scid, unsigned pn, const Zutcp::TransportParams *params,
  uint64_t ack = Zutcp::U64Null, unsigned firstRange = 0)
{
  auto buf = app.packets.allocBuf();
  enum { Frames = Zutcp::MinPktPayload };
  int offset = Zutcp::InitialPkt::write(buf->data(), buf->avail(), dcid, scid,
    pn, 2, params, Frames);
  if (offset < 0) return nullptr;
  memset(buf->data() + offset, 0, Frames);
  if (ack != Zutcp::U64Null)
    Zutcp::FrameCodec::writeAck(buf->data() + offset, Frames, ack, 0, firstRange);
  else Zutcp::FrameCodec::writePing(buf->data() + offset, Frames);
  buf->length = offset + Frames;
  return buf;
}

struct OriginCli : ZmNHeap<OriginCli>,
  Zutcp::Connection<GateApp, OriginCli, Zutcp::TxBufAlloc<>> {
friend inline ZuStringT<"Zutcp.Handshake.OriginCli"> ZmNHeapID(OriginCli *);
  using Base = Zutcp::Connection<GateApp, OriginCli, Zutcp::TxBufAlloc<>>;
  ZmRef<ZiIOBuf> wire;
  Zutcp::TCPHeader header;
  unsigned prepares = 0, aborts = 0, resets = 0, initialCalls = 0;
  bool peerReset = false;
  explicit OriginCli(GateApp *app) : Base{app, false} { }
  bool prepare(uint32_t local) {
    Base::resetLink_();
    Zutcp::CxnID original{"original"}, localCID{"client00"};
    Base::setRuntimeCIDs_(original, localCID, original);
    Base::configLocalParams(app());
    return Base::setTCPOriginsRx_({local, 0, true, false, false}) && Base::startHandshake();
  }
  int process(Zutcp::Reader &) { return 0; }
  bool flushTx_() { return false; }
  bool flushTx_(ZiSockAddr) { return false; }
  void pto_() { }
  void queueRetransmit() { }
  bool retransmit_(bool = false, unsigned = 2,
    Zutcp::PktNumSpace::T = Zutcp::PktNumSpace::N) { return false; }
  bool peerOriginForTest(uint32_t peer) {
    return Base::setTCPOriginsRx_({0, peer, false, true, false});
  }
  bool initial(bool params, uint64_t ack = Zutcp::U64Null, bool accept = true) {
    Zutcp::PktBuild payload;
    int n = ack == Zutcp::U64Null ?
      Zutcp::FrameCodec::writePing(payload.scratch(), payload.scratchAvail()) :
      Zutcp::FrameCodec::writeAck(payload.scratch(), payload.scratchAvail(), ack, 0, 0);
    if (n <= 0 || !payload.commitScratch(unsigned(n))) return false;
    Base::TxPktRefs refs;
    return Base::sendInitialPkt(Base::CIDSel::Peer, Base::CIDSel::Local, 2, true,
      payload, {}, {}, &refs, params,
      [this] { return app()->allocTxPkt_(); },
      [this, accept](auto buf, ZiSockAddr) {
	++initialCalls;
	header = buf->tcp;
	wire = ZuMv(buf);
	return accept;
      });
  }
  bool receive(Zutcp::Datagram d) {
    return Base::receiveLongPkt(d, 0, d.buf->length,
      [this](const Zutcp::LongHdr &h, Zutcp::Datagram &) {
	++prepares;
	Base::setPeerCIDFromHdrSCID(h);
	return true;
      },
      [this](auto level, auto pn, ZuBSpan frames, auto addr, const auto &buf,
	Base::RxAckMeta &ack, auto *qlog) {
	return Base::consumeFrames(level, pn, frames, ZuMv(addr), buf, ack, qlog,
	  [](auto, const Zutcp::Frame &, ZiSockAddr) { return true; });
      });
  }
  bool receiveShortForTest(Zutcp::Datagram d) {
    return Base::receiveShortPkt(d, 0, d.buf->length,
      [this](auto level, auto pn, ZuBSpan frames, auto addr, const auto &buf,
	Base::RxAckMeta &ack, auto *qlog) {
	return Base::consumeFrames(level, pn, frames, ZuMv(addr), buf, ack, qlog,
	  [](auto, const Zutcp::Frame &, ZiSockAddr) { return true; });
      });
  }
  void tcpResetAbort_(bool peer) { ++aborts; peerReset = peer; Base::disconnect(peer); }
  bool sendTCPReset_() {
    return Base::sendTCPResetPkt_([this] { return app()->allocTxPkt_(); },
      [this](auto) { ++resets; return true; });
  }
  bool initialSeen(uint64_t pn) const { return Base::rxPktSeen(Zutcp::PktNumSpace::Initial, pn); }
  bool shortSeen(uint64_t pn) const { return Base::rxPktSeen(Zutcp::PktNumSpace::AppData, pn); }
  void reset() { wire = nullptr; Base::resetLink_(); }
};

static void testAcceptedSYNHeader()
{
  ZuTestScope(testAcceptedSYNHeader);
  GateApp app;
  app.native = true;
  ZmRef<OriginCli> client = new OriginCli{&app};
  bool prepared = client->prepare(0) && client->peerOriginForTest(UINT32_MAX);
  app.drain();
  app.owner = 2;
  bool refused = !client->initial(true, Zutcp::U64Null, false);
  auto rejected = client->header;
  bool blocked = !client->initial(false, 0) && client->initialCalls == 1;
  bool retried = client->initial(true) && client->header.seq == rejected.seq &&
    client->header.flags == rejected.flags;
  bool ack = client->initial(false, 0) && client->header.flags == 0x10 &&
    client->header.seq == 1 && !client->header.ack && client->initialCalls == 3;
  app.owner = 1;
  ZuCHECK(prepared && refused && rejected.seq == 0 && rejected.flags == 0x02 &&
      blocked && retried && ack,
    "failed SYN submission must not authorize ACK-only output; accepted retry retains ISN0 "
    "and consumes exactly one SYN position");
  client->reset();
  app.drain();
  client = nullptr;
  app.drain();
}

static void testNativeSYNTranscript()
{
  ZuTestScope(testNativeSYNTranscript);
  GateApp app;
  app.native = true;
  ZmRef<OriginCli> client = new OriginCli{&app};
  ZmRef<GateCxn> server = new GateCxn{&app};
  enum { ClientISN = 0x12345678, ServerISN = 0x23456789 };
  ZuCHECK(client->prepare(ClientISN) && server->originForTest(ServerISN),
    "native transcript fixture must choose local origins on Rx before any output");
  app.drain();
  app.owner = 2;
  bool sent = client->initial(true);
  auto syn = client->header;
  auto request = client->wire;
  app.owner = 1;
  auto input = gateInput(app, request);
  input.tcp = syn;
  ZuCHECK(sent && syn.flags == 0x02 && syn.seq == ClientISN && !syn.ack &&
      server->receivedRouted(ZuMv(input), {}),
    "actual Initial request must carry the retained SYN origin without an invented ACK");
  app.drain();
  Zutcp::InitialPkt response;
  bool decoded = app.response && response.parse(app.response->cspan()) > 0;
  auto synack = app.responseTCP;
  ZuCHECK(decoded && app.longSent == 1 && synack.flags == 0x12 && synack.seq == ServerISN &&
      synack.ack == ClientISN + 1 && !server->connections,
    "first actual server output must be SYNACK with pending envelope ACK, never ACK before SYN");
  if (!decoded) { client->reset(); server->reset(); app.drain(); return; }
  auto reply = gateInput(app, app.response);
  reply.tcp = synack;
  ++reply.tcp.ack;
  ZuCHECK(!client->receive(ZuMv(reply)) && !client->initialSeen(response.number) &&
      !client->prepares,
    "wrong SYNACK acknowledgment must not commit the peer origin, prepare callback or PN");
  Zutcp::TransportParams corrupt;
  bool paramsDecoded = !corrupt.decode(response.params);
  corrupt.origDCID = Zutcp::CxnID{"wrongold"};
  auto malformed = gateInput(app, gateInitial(app, response.header.dcid, response.header.scid,
    88, &corrupt, 0));
  malformed.tcp = synack;
  ++malformed.tcp.seq;
  ZuCHECK(paramsDecoded && !client->receive(ZuMv(malformed)) &&
      !client->initialSeen(88) && !client->prepares,
    "wrong original CID cannot poison a valid-looking SYNACK peer origin, PN or prepare state");
  reply = gateInput(app, app.response);
  reply.tcp = synack;
  ZuCHECK(client->receive(ZuMv(reply)),
    "correct SYNACK must remain admissible after bad ACK or malformed original CID");
  app.drain();
  auto duplicate = gateInput(app, request);
  duplicate.tcp = syn;
  server->receivedRouted(ZuMv(duplicate), {});
  app.drain();
  Zutcp::InitialPkt repeated;
  bool retransmitted = app.response && repeated.parse(app.response->cspan()) > 0 &&
    repeated.number > response.number;
  ZuCHECK(retransmitted && app.responseTCP.seq == ServerISN &&
      app.responseTCP.ack == ClientISN + 1 && app.responseTCP.flags == 0x12 &&
      !server->connections,
    "repeated SYN may use another envelope PN but must retain the chosen server SYN origin");
  Zutcp::TransportParams params;
  params.initialSCID = Zutcp::CxnID{"client00"};
  params.initialMaxData = 8192;
  auto conflict = gateInput(app, gateInitial(app, Zutcp::CxnID{"original"},
    params.initialSCID, 77, &params));
  conflict.tcp = syn;
  ++conflict.tcp.seq;
  unsigned before = app.longSent;
  server->receivedRouted(ZuMv(conflict), {});
  app.drain();
  ZuCHECK(!server->initialSeenForTest(77) && app.longSent == before && !server->connections,
    "conflicting duplicate SYN origin must be rejected before PN and response effects");
  auto wrong = gateInput(app, gateInitial(app, repeated.header.scid, params.initialSCID,
    78, nullptr, repeated.number));
  wrong.tcp = {.seq = ClientISN + 1, .ack = ServerISN + 2, .window = 1, .flags = 0x10};
  server->receivedRouted(ZuMv(wrong), {});
  app.drain();
  ZuCHECK(!server->initialSeenForTest(78) && !server->connections,
    "wrong final header ACK cannot consume response PN evidence or publish establishment");
  reply = gateInput(app, app.response);
  reply.tcp = app.responseTCP;
  ZuCHECK(client->receive(ZuMv(reply)),
    "repeated matching SYNACK must retain the same peer origin");
  app.drain();
  app.owner = 2;
  bool acknowledged = client->initial(false, repeated.number);
  auto final = client->header;
  auto confirm = client->wire;
  app.owner = 1;
  auto finalInput = gateInput(app, confirm);
  finalInput.tcp = final;
  ZuCHECK(acknowledged && final.flags == 0x10 && final.seq == ClientISN + 1 &&
      final.ack == ServerISN + 1 && server->receivedRouted(ZuMv(finalInput), {}),
    "final actual Initial ACK must use both post-SYN origins and consume no second SYN");
  app.runTx();
  ZuCHECK(!server->connections, "response ACK evidence on Tx must wait for its Rx publication");
  app.drain();
  ZuCHECK(server->connections == 1 && server->established(),
    "matching final TCP and envelope ACK must establish the actual server role once");
  client->reset();
  server->reset();
  app.drain();
  client = nullptr;
  server = nullptr;
  app.drain();
}

static void testPendingSYNReset()
{
  ZuTestScope(testPendingSYNReset);
  GateApp app;
  app.native = true;
  ZmRef<AbortCli> client = new AbortCli{&app};
  enum { LocalISN = 0x3456789a };
  bool prepared = client->pendingOriginForTest(LocalISN);
  app.drain();
  app.owner = 2;
  bool sent = client->synForTest();
  app.owner = 1;
  auto reset = [&app](uint32_t ack, bool wrongCID = false) {
    auto buf = app.packets.allocBuf();
    Zutcp::CxnID cid{wrongCID ? "wrongcid" : "client00"};
    int n = Zutcp::Pkt::writeShort(buf->data(), buf->avail(), cid, 11, 4);
    if (n < 0) return Zutcp::Datagram{};
    buf->data()[n] = 0;
    buf->length = unsigned(n) + 1;
    auto d = gateInput(app, ZuMv(buf));
    d.tcp = {.seq = 0, .ack = ack, .window = 1, .flags = 0x14};
    return d;
  };
  bool ignored = client->receiveShortForTest(reset(LocalISN + 2)) &&
    client->receiveShortForTest(reset(LocalISN + 1, true));
  app.drain();
  ZuCHECK(prepared && sent && ignored && !client->shortSeenForTest(11) &&
      !client->disconnecting_() && !client->connectFailures && !client->resetPackets,
    "pending client must ignore bad RST ACK or CID before peer origin, PN or failure callback");
  bool accepted = client->receiveShortForTest(reset(LocalISN + 1));
  app.drain();
  client->receiveShortForTest(reset(LocalISN + 1));
  app.drain();
  app.owner = 2;
  bool quiet = !client->timerWork() && !client->txQueueCount();
  app.owner = 1;
  ZuCHECK(accepted && quiet && client->connectFailures == 1 && !client->connects &&
      client->disconnectedCount == 1 && client->endpointDownCount == 1 &&
      !client->resetPackets && !client->shortSeenForTest(11) && client->disconnecting_(),
    "valid SYN ACK must fail the client role once and drain without RST echo");
  client = nullptr;
  app.drain();
}

static void testServerAckGate(unsigned stale)
{
  ZuTestScopeRT(testServerAckGate);
  GateApp app;
  ZmRef<GateCxn> cxn = new GateCxn{&app};
  Zutcp::TransportParams params;
  params.initialSCID = Zutcp::CxnID{"client00"};
  params.initialMaxData = 8192;
  auto request = gateInitial(app, Zutcp::CxnID{"original"}, params.initialSCID, 0, &params);
  ZuCHECK(cxn->receivedRouted(gateInput(app, ZuMv(request)), {}), "server admits request");
  app.drain();
  Zutcp::InitialPkt response;
  bool sentResponse = app.response && response.parse(app.response->cspan()) > 0;
  ZuCHECK(sentResponse && !cxn->established() && !cxn->connections,
    "peer parameters and submitted response do not establish the server");
  if (sentResponse) {
    auto wrong = gateInitial(app, response.header.scid, params.initialSCID, 1,
      nullptr, response.number + 100);
    ZuCHECK(cxn->receivedRouted(gateInput(app, ZuMv(wrong)), {}), "unsent ACK is parsed");
    app.drain();
    ZuCHECK(!cxn->established() && !cxn->connections,
      "ACK for an unsent response cannot establish");
    auto ack = gateInitial(app, response.header.scid, params.initialSCID, 2,
      nullptr, response.number);
    ZuCHECK(cxn->receivedRouted(gateInput(app, ZuMv(ack)), {}), "response ACK admitted on Rx");
    if (stale == 2) cxn->restartReady(params);
    app.runTx(); // Valid recovery evidence exists, but Rx publication is held.
    ZuCHECK(!cxn->established() && !cxn->connections,
      "Tx evidence does not mutate Rx establishment directly");
    if (stale) {
      if (stale == 1) cxn->restartReady(params);
      app.drain();
      ZuCHECK(!cxn->established() && !cxn->connections,
	"old ACK publication cannot establish a new ready generation");
    } else {
      auto data = app.packets.allocBuf();
      int n = Zutcp::Pkt::writeShort(data->data(), data->avail(), response.header.scid, 0, 2);
      const uint8_t payload[] = {'x'};
      int m = n > 0 ? Zutcp::FrameCodec::writeData(data->data() + n, data->avail() - n,
	0, ZuBSpan{payload, sizeof(payload)}, false) : -1;
      data->length = m > 0 ? n + m : 0;
      ZuCHECK(m > 0 && cxn->receivedRouted(gateInput(app, ZuMv(data)), response.header.scid),
	"first Short data is accepted before ACK publication returns to Rx");
      app.runTx();
      ZuCHECK(cxn->deliveries == 1 && cxn->early && !app.shortSent,
	"valid early input queues its reply without opening the initial send budget");
      app.drain();
      ZuCHECK(cxn->established() && cxn->connections == 1 && app.shortSent,
	"ACK publication establishes once and resumes the queued reply");
      auto duplicate = gateInitial(app, response.header.scid, params.initialSCID, 3,
	nullptr, response.number);
      cxn->receivedRouted(gateInput(app, ZuMv(duplicate)), {});
      app.drain();
      ZuCHECK(cxn->connections == 1, "duplicate ACK does not repeat connected");
    }
  }
  cxn->reset();
  app.drain();
  cxn = nullptr;
  app.drain();
}

static void testAckBatchGate()
{
  ZuTestScope(testAckBatchGate);
  GateApp app;
  ZmRef<GateCxn> cxn = new GateCxn{&app};
  Zutcp::TransportParams params;
  params.initialSCID = Zutcp::CxnID{"client00"};
  params.initialMaxData = 8192;
  auto request = gateInitial(app, Zutcp::CxnID{"original"}, params.initialSCID, 0, &params);
  cxn->receivedRouted(gateInput(app, ZuMv(request)), {});
  app.drain();
  Zutcp::InitialPkt response;
  bool sentResponse = app.response && response.parse(app.response->cspan()) > 0;
  ZuCHECK(sentResponse, "response recorded before batched ACK");
  if (sentResponse) {
    enum { Count = GateCxn::RecoveryScanBatch * 2 + 1 };
    ZuCHECK(cxn->responseBatch(response.number + 1, Count), "retained response batch installed");
    auto ack = gateInitial(app, response.header.scid, params.initialSCID, 1,
      nullptr, response.number + Count, Count);
    cxn->receivedRouted(gateInput(app, ZuMv(ack)), {});
    app.stepTx();
    app.runRx();
    ZuCHECK(!cxn->established() && !cxn->connections,
      "partial bounded ACK batch cannot publish establishment");
    app.drain();
    ZuCHECK(cxn->established() && cxn->connections == 1,
      "completed ACK batch publishes its response evidence exactly once");
  }
  cxn->reset();
  app.drain();
  cxn = nullptr;
  app.drain();
}

static void testResponseEvidence()
{
  ZuTestScope(testResponseEvidence);
  auto packet = sent(0, 100);
  packet.packetType = Zutcp::PktType::Initial;
  Zutcp::PktTxUpdate update;
  packet.ackEliciting = false;
  for (unsigned i = 0; i < Zutcp::PktTxUpdate::AckedPNSampleMax; ++i) {
    packet.pn = i;
    update.ackd(packet, false);
  }
  ZuCHECK(!update.initialResponseAckd, "pure ACK packets cannot confirm setup");
  packet.ackEliciting = true;
  packet.lost = true;
  update.ackd(packet, false);
  ZuCHECK(update.ackedPNsTruncated && update.initialResponseAckd,
    "late ACK of a retained lost response counts beyond the sampled PN bound");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testInitialTranscript);
  ZuTestCall(testAbortBeforeReady);
  ZuTestCall(testAbortReentrantConnect);
  ZuTestCall(testNativeGracefulCallbacks);
  ZuTestCall(testNativeZeroDisconnect);
  ZuTestCall(testNativeErrorDisconnect);
  ZuTestCall(testPendingErrorDisconnect);
  ZuTestCall(testNativeQueueError);
  ZuTestCall(testNativeBackingError);
  ZuTestCall(testNativeErrorReplacement);
  ZuTestCall(testNativeErrorPolicy);
  ZuTestCall(testTCPOriginOwners);
  ZuTestCall(testNativeIdleReset);
  ZuTestCall(testRetainedAdmissionFinWake);
  ZuTestCall(testPendingCloseCallbacks);
  ZuTestCall(testFinRequestReplacement);
  ZuTestCall(testNativeSetupFailureDrain);
  ZuTestCall(testCoalescedNativeConnect);
  ZuTestCall(testInitialBounds);
  ZuTestCall(testInitialContext);
  ZuTestCall(testAcceptedSYNHeader);
  ZuTestCall(testNativeSYNTranscript);
  ZuTestCall(testPendingSYNReset);
  ZuTestCall(testServerAckGate, 0);
  ZuTestCall(testServerAckGate, 1);
  ZuTestCall(testServerAckGate, 2);
  ZuTestCall(testResponseEvidence);
  ZuTestCall(testAckBatchGate);
}
