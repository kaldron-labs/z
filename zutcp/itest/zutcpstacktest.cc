//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <arpa/inet.h>
#include <iostream>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmAssert.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmTime.hh>
#include <zlib/ZiLog.hh>
#include <zlib/Zutcp.hh>
#include <zlib/ZutcpMx.hh>

using namespace ZuTestUtil;

enum { RxCount = 4, Head = 14 + 40 + 60 + Zutcp::TxDataPktReserve, Payload = 32,
  WireSize = Head + Payload, Repeats = 1 }; // exact fixture workload

struct Probe {
  ZiMultiplex *mx;
  Zi::IOBufAllocator *allocator;
  ZmSemaphore done;
  ZmRef<Zi::IOBuf> packet;
  ZmScheduler::Timer timer;
  ZuTime deadline;

  void run() {
    ZmAssert_(mx->invoked(mx->txThread()));
    packet = allocator->allocBuf(this, Payload);
    if (packet || Zm::now() >= deadline) { done.post(); return; }
    mx->add(&timer, Zm::now() + ZuTime{0, 1000000}, ZmScheduler::Update,
      [this](auto &&arm) { return arm([this] { run(); }); }, mx->txThread());
  }
};

struct WriterApp : Zutcp::Hub<WriterApp> { };

struct WriterCxn : public ZmNHeap<WriterCxn>,
  public Zutcp::Connection<WriterApp, WriterCxn, Zutcp::TxBufAlloc<>> {
friend inline ZuStringT<"Zutcp.StackTest.Writer"> ZmNHeapID(WriterCxn *);
  using Base = Zutcp::Connection<WriterApp, WriterCxn, Zutcp::TxBufAlloc<>>;
  using Base::txNext;
  using ZmNHeap<WriterCxn>::operator new;
  using ZmNHeap<WriterCxn>::operator delete;

  Zutcp::SentFrameRef retry;
  unsigned extraAlloc = 0, submitted = 0, controls = 0;
  bool offerAck = true;
  bool original = true, detached = true, bounded = true, reused = true;

  static bool controlSelection() {
    uint8_t frame[32]; // FIN and one-byte DATA fixture encodings
    int n = Zutcp::FrameCodec::writeData(frame, sizeof(frame), 0, {}, true);
    bool fin = n > 0 && Base::txControlPacket({frame, unsigned(n)}, nullptr);
    uint8_t byte = 1;
    n = Zutcp::FrameCodec::writeData(frame, sizeof(frame), 0, {&byte, 1}, false);
    return fin && n > 0 && !Base::txControlPacket({frame, unsigned(n)}, nullptr);
  }
  explicit WriterCxn(WriterApp *app) : Base{app} { }
  bool flushTx_() { return false; } // this fixture explicitly steps packetization
  void queueTxFlush(ZiSockAddr = {}) { }
  void queueRetransmit() { }
  void pto_() { }
  bool retransmit_(bool = false, unsigned = 2,
      Zutcp::PktNumSpace::T = Zutcp::PktNumSpace::N) { return false; }
  int process(Zutcp::Reader &) { return 0; }
  void prepareRx() {
    Base::configLocalParams(app());
    Zutcp::TransportParams params;
    params.initialMaxData = 0;
    Base::peerParams(params);
    Base::startHandshake();
    // This synthetic writer bypasses setup; explicitly keep logical data bases at zero.
    Base::setTCPOriginsRx_({.local = UINT32_MAX, .peer = UINT32_MAX,
      .localKnown = true, .peerKnown = true});
    Base::establish_();
  }
  void prepareTx() {
    Base::initClientPathTx_(
      ZiSockAddr{ZiIP{"198.18.0.1"}, 18181},
      ZiSockAddr{ZiIP{"198.18.0.2"}, 18181});
  }
  void clearRx() { Base::resetLink_(); }
  bool sendNext(Zutcp::Stack *stack) {
    return Base::flushData({},
      [this](auto &build) {
	if (!offerAck) return true;
	enum { Ranges = 32 }; // exceeds the reserved piggyback prefix
	Zutcp::AckRange ranges[Ranges];
	for (unsigned i = 0; i < Ranges; ++i) ranges[i] = {3 * i, 3 * i};
	int n = Zutcp::FrameCodec::writeAckRanges(
	  build.scratch(), build.scratchAvail(), ranges, Ranges, 0);
	return n > int(Zutcp::TxDataPktReserve) && build.commitScratch(unsigned(n));
      },
      [this, stack](auto &build, auto addr, const auto &refs) {
	if (!refs.count()) {
	  return Base::sendShortPkt_(Zutcp::CxnID{}, 1, build, addr, {}, &refs, false,
	    [this, stack]() { return stack->allocPacket(this, ZiIPType::V4, true); },
	    [this, stack](auto packet, auto, auto) {
	      bool sent = submit(stack, ZuMv(packet), true);
	      if (sent) offerAck = false;
	      return sent;
	    });
	}
	const auto &range = refs[refs.count() - 1].range;
	if (submitted == 1) retry = refs[refs.count() - 1];
	auto summary = refs.summary();
	original &= !summary[summary.count() - 1].range.storage;
	auto payload = range.storage->data + range.offset;
	return Base::sendShortPkt_(Zutcp::CxnID{}, 1, build, addr, {}, &refs, true,
	  [this, stack]() { ++extraAlloc; return stack->allocPacket(this, ZiIPType::V4); },
	  [this, stack, payload](auto packet, auto, auto) {
	    original &= packet->data() + packet->length - 8 == payload;
	    bounded &= packet->length < Zutcp::TxDataPktReserve;
	    return submit(stack, ZuMv(packet));
	  });
      });
  }
  bool submit(Zutcp::Stack *stack, ZmRef<ZiIOBuf> packet, bool control = false) {
    memset(packet->data_(), 0, packet->skip);
    memset(packet->data_(), 0xff, 6);
    packet->data_()[6] = 2;
    packet->data_()[12] = 0x88; packet->data_()[13] = 0xb5;
    packet->rewind(packet->skip);
    bool sent = stack->sendPacket(packet);
    detached &= sent && !packet->data_() && !packet->size;
    if (sent) { if (control) ++controls; else ++submitted; }
    return sent;
  }
  void ackFirst() {
    Base::AckSnapshot ack;
    ack.level = Zutcp::PktNumSpace::AppData;
    ack.byteNext = 8;
    new (ack.ranges.push()) Zutcp::AckRange{1, 1}; // preceding ACK-only packet uses PN 0
    Base::processAckFrameTx_(ack);
  }
  bool replay(Zutcp::Stack *stack) {
    auto ref = retry;
    Zutcp::PktBuild build;
    if (!Base::buildRetxData(build, ref)) return false;
    Base::TxPktRefs refs;
    refs.add(ref);
    return Base::sendShortPkt_(Zutcp::CxnID{}, 1, build, {}, {}, &refs, true,
      [this, stack]() { ++extraAlloc; return stack->allocPacket(this, ZiIPType::V4); },
      [this, stack](auto packet, auto, auto) {
	auto payload = packet->data() + packet->length - 8;
	reused &= payload == retry.range.storage->data + retry.range.offset &&
	  !memcmp(payload, "second--", 8);
	return submit(stack, ZuMv(packet));
      });
  }
};

struct RetxProbe {
  ZiMultiplex *mx;
  WriterCxn *cxn;
  Zutcp::Stack *stack;
  ZmSemaphore done;
  ZmScheduler::Timer timer;
  ZuTime deadline;
  bool sent = false;

  void run() {
    if ((sent = cxn->replay(stack)) || Zm::now() >= deadline) { done.post(); return; }
    mx->add(&timer, Zm::now() + ZuTime{0, 1000000}, ZmScheduler::Update,
      [this](auto &&arm) { return arm([this] { run(); }); }, mx->txThread());
  }
};

// Two internal slots: recovery holds one, and an unretained marker proves CQ
// progress before ACK. All queue/pool operations run on the same Tx owner.
struct LeaseProbe {
  ZiMultiplex *mx;
  Zutcp::Stack *stack;
  Zutcp::PktTxSpace recovery;
  ZmSemaphore done;
  ZmScheduler::Timer timer;
  ZuTime deadline;
  Zutcp::PacketLease *shutdownLease = nullptr;
  Zutcp::SentFrameRef heldRange;
  uint8_t *origin = nullptr;
  bool submitted = false, retired = false, refused = false, held = false;

  static Zutcp::SentPkt record(uint64_t pn) {
    Zutcp::SentPkt p;
    p.pn = pn;
    p.bytes = 64;
    p.ackEliciting = p.inFlight = true;
    return p;
  }
  static void frame(Zutcp::PacketBuf *packet) {
    packet->skip = 0;
    packet->length = 64;
    memset(packet->data(), 0, 64);
    memset(packet->data(), 0xff, 6);
    packet->data()[6] = 2;
    packet->data()[12] = 0x88; packet->data()[13] = 0xb5;
    packet->data()[14] = 0x5a;
  }
  void start() {
    auto packet = stack->allocPacket(this, ZiIPType::V4);
    auto marker = stack->allocPacket(this, ZiIPType::V4);
    if (!packet || !marker) {
      if (Zm::now() >= deadline) { done.post(); return; }
      packet = marker = nullptr;
      mx->add(&timer, Zm::now() + ZuTime{0, 1000000}, ZmScheduler::Update,
        [this](auto &&arm) { return arm([this] { start(); }); }, mx->txThread());
      return;
    }
    origin = packet->data_();
    Zutcp::PacketLease *lease = nullptr;
    if (!recovery.add(record(1), &lease)) { done.post(); return; }
    frame(packet);
    packet->retained = lease;
    submitted = stack->sendPacket(packet) && !packet->data_() && **lease;
    packet->retained = nullptr;
    frame(marker);
    submitted &= stack->sendPacket(marker) && !marker->data_();
    // Dropping both submitted metadata objects cannot release recovery backing.
    packet = marker = nullptr;
    run();
  }
  void run() {
    auto marker = stack->allocPacket(this, ZiIPType::V4);
    if (!marker) {
      if (Zm::now() >= deadline) { done.post(); return; }
      mx->add(&timer, Zm::now() + ZuTime{0, 1000000}, ZmScheduler::Update,
        [this](auto &&arm) { return arm([this] { run(); }); }, mx->txThread());
      return;
    }
    // The later marker's CQ has returned; the earlier recovery slot stays held.
    bool exhausted = !stack->allocPacket(this, ZiIPType::V4);
    Zutcp::SentPkt snapshot = recovery.txQueue()->find(1)->data();
    bool lost = recovery.lose(1) &&
      !stack->allocPacket(this, ZiIPType::V4);
    bool ackd = recovery.ack(1);
    auto packet = stack->allocPacket(this, ZiIPType::V4);
    retired = submitted && exhausted && marker->data_() != origin && ackd &&
      snapshot.pn == 1 && lost && packet && packet->data_() == origin;
    if (!packet) { done.post(); return; }

    Zutcp::PacketLease *lease = nullptr;
    if (!recovery.add(record(2), &lease)) { done.post(); return; }
    packet->retained = lease;
    auto originalSize = packet->size, originalSkip = packet->skip;
    {
      Zutcp::TxPacketView view{packet, 1, 20};
      packet->length = packet->size + 1;
      refused = !stack->sendPacket(packet) && !**lease;
    }
    refused &= packet->data_() == origin && packet->size == originalSize &&
      packet->skip == originalSkip && !packet->length && packet->registered();
    packet->retained = nullptr;
    refused &= recovery.discard(2) && packet->data_() == origin &&
      !stack->allocPacket(this, ZiIPType::V4);

    frame(packet);
    ZmRef<Zutcp::TxRanges::Node> queued = new Zutcp::TxRanges::Node{packet, 0};
    heldRange = Zutcp::SentFrameRef::data(Zutcp::TxRange{queued, 14, 1}, false);
    auto dataPacket = record(3);
    dataPacket.addFrame(heldRange);
    if (!recovery.add(dataPacket)) { done.post(); return; }
    shutdownLease = &queued->backing;
    packet->retained = shutdownLease;
    held = stack->sendPacket(packet) && !packet->data_() && **shutdownLease;
    packet->retained = nullptr;
    queued->pending = nullptr;
    // Raw coordinates survive destruction of the submitted Zi metadata.
    held &= heldRange.range.storage->data == origin && queued->bytes == 64;
    done.post();
  }
};

struct ControlProbe {
  ZiMultiplex *mx;
  Zutcp::Stack *stack;
  Zutcp::PktTxSpace retained;
  Zutcp::PacketLease *lease = nullptr;
  Zutcp::TxAdmission during;
  ZmSemaphore done;
  ZmScheduler::Timer timer;
  ZuTime deadline;
  bool submitted = false, kernelOnly = false;

  static void frame(ZiIOBuf *packet, unsigned tag) {
    packet->skip = 0;
    packet->length = 64;
    memset(packet->data(), 0, 64);
    memset(packet->data(), 0xff, 6);
    packet->data()[6] = 2;
    packet->data()[12] = 0x88; packet->data()[13] = 0xb5;
    packet->data()[14] = tag;
  }
  void run() {
    auto current = stack->txAdmission();
    if (current.outstanding && Zm::now() < deadline) {
      mx->add(&timer, Zm::now() + ZuTime{0, 1000000}, ZmScheduler::Update,
        [this](auto &&arm) { return arm([this] { run(); }); }, mx->txThread());
      return;
    }
    kernelOnly = !current.outstanding && current.controlCount == 2 &&
      current.controlFree == 1 && lease && **lease;
    done.post();
  }
};

static void testStack()
{
  ZuTestScopeRT(testStack);
  alarm(30); // a lost wake/lifecycle transition must fail the fixture
  auto index = unsigned(strtoul(getenv("ZXDP_RX_TEST_IFINDEX"), nullptr, 10));
  auto peer = unsigned(strtoul(getenv("ZXDP_RX_TEST_PEER_IFINDEX"), nullptr, 10));
  ZiMultiplex mx{ZiMxParams{}.rxBufSize(RxCount).rxQuantum(1)
    .scheduler([](auto &p) { p.nThreads(2); })};
  ZmSemaphore received;
  ZmRef<Zi::IOBuf> input[RxCount], extra;
  unsigned packets = 0;
  bool onRx = true;
  ZmRef<Zutcp::Stack> stack = new Zutcp::Stack{&mx, {.interface = index,
    .packet = [&mx, &input, &extra, &packets, &onRx, &received](ZmRef<Zi::IOBuf> buf) {
      if (buf->length < 14 || buf->data()[12] != 0x88 || buf->data()[13] != 0xb5) return;
      onRx &= mx.invoked(mx.rxThread()) && buf->registered();
      if (packets < RxCount) input[packets] = ZuMv(buf); else extra = ZuMv(buf);
      ++packets;
      received.post();
    }, .spin = 0, .packetCount = 2}};
  mx.plugin(stack);
  ZtString<> configError;
  bool invalidCount = !stack->configureControl(0, 1, configError);
  bool invalidCredits = !stack->configureControl(2, 4, configError);
  bool invalidRx = !stack->configure({.controlBufCount = 3, .txControlCredits = 3,
    .rxReserveFrames = RxCount}, configError);
  ZuCheckRT(invalidCount && invalidCredits &&
      invalidRx && strstr(configError.data(), "physical frames=4") &&
      stack->txAdmission().controlCount == Zutcp::DefaultControlBufCount,
    log_("invalid control configuration preserves provisioned pool counts"));
  for (unsigned i = 0; i < 3; ++i) {
    Zutcp::StackConfig config;
    config.maxCxns = i ? 2 : 0;
    config.maxPendingCxns = i == 1 ? 0 : 3;
    bool rejected = !stack->configure(config, configError);
    auto slots = stack->cxnAdmission();
    ZuCheckRT(rejected && strstr(configError.data(), "connection admission") &&
	!slots.total && !slots.pending && slots.maxCxns == Zutcp::DefaultMaxCxns &&
	slots.maxPendingCxns == Zutcp::DefaultMaxPendingCxns,
      log_("invalid connection caps must preserve stopped admission configuration"));
  }
  for (auto timeout : {ZuTime{}, ZuTime{0}, ZuTime{-1}}) {
    Zutcp::StackConfig config;
    config.connectTimeout = timeout;
    bool rejected = !stack->configure(config, configError);
    ZuCheckRT(rejected && strstr(configError.data(), "connect timeout") &&
	stack->txAdmission().controlCount == Zutcp::DefaultControlBufCount,
      log_("invalid connect deadlines must preserve stopped configuration"));
  }
  for (auto timeout : {ZuTime{}, ZuTime{-1}}) {
    Zutcp::StackConfig config;
    config.userTimeout = timeout;
    bool rejected = !stack->configure(config, configError);
    ZuCheckRT(rejected && strstr(configError.data(), "user timeout") &&
	stack->txAdmission().controlCount == Zutcp::DefaultControlBufCount,
      log_("invalid user deadlines must preserve stopped configuration"));
  }
  for (bool finWait : {false, true}) for (auto timeout : {ZuTime{}, ZuTime{0}, ZuTime{-1}}) {
    Zutcp::StackConfig config;
    if (finWait) config.finWaitTimeout = timeout;
    else config.closeDrainTimeout = timeout;
    bool rejected = !stack->configure(config, configError);
    ZuCheckRT(rejected && strstr(configError.data(), "close timeout") &&
	stack->txAdmission().controlCount == Zutcp::DefaultControlBufCount,
      log_("invalid close deadlines must preserve stopped configuration"));
  }
  {
    Zutcp::StackConfig config;
    config.closedTupleRingSize = 0;
    bool rejected = !stack->configure(config, configError);
    ZuCheckRT(rejected && strstr(configError.data(), "closed tuple") &&
	stack->closedTupleStats().capacity == Zutcp::DefaultClosedTupleRingSize,
      log_("zero closed tuple capacity must preserve stopped configuration"));
  }
  WriterApp app;
  unsigned configErrors = 0;
  auto badHub = Zutcp::HubParams{&mx}.controlBufCount(2).txControlCredits(4)
    .rxReserveFrames(1)
    .errorFn([&configErrors](ZeException) { ++configErrors; });
  bool hubRejected = !app.init(ZuMv(badHub));
  ZuCheckRT(hubRejected && configErrors == 1 && !app.mx() &&
      stack->txAdmission().controlCount == Zutcp::DefaultControlBufCount,
    log_("failed Hub control configuration publishes no owner or changed pool"));
  bool appReady = app.init(Zutcp::HubParams{&mx}.controlBufCount(2).txControlCredits(3)
    .rxReserveFrames(1).closedTupleRingSize(3));
  ZuCheckRT(appReady && app.dataTxAllocator() == stack->dataTxAllocator(),
    log_("Hub obtains its producer adapter directly from its owned native service"));
  auto rxLimits = stack->rxAdmission();
  ZuCheckRT(rxLimits.capacity == RxCount && rxLimits.reserve == 1 &&
      rxLimits.configured == 1024 && rxLimits.configuredPerCxn == 256 &&
      rxLimits.limit == RxCount - 1 && rxLimits.perCxnLimit == RxCount - 1,
    log_("effective retention uses actual ingress capacity without growing a small pool"));
  auto tx = mx.allocator<Zi::IOBufType::TxTCP, Zi::IOBuf, 128>(3);
  auto small = mx.allocator<Zi::IOBufType::TxTCP, Zi::IOBuf, 64>(1);
  auto rx = mx.allocator<Zi::IOBufType::RxTCP>(77);
  auto rxHeap = mx.allocator<Zi::IOBufType::App>();
  auto otherRx = mx.allocator<Zi::IOBufType::RxTCP, Zi::IOBuf, 512>(55);
  auto stub = rx->allocBuf(&mx), otherStub = otherRx->allocBuf(&mx);
  ZuCheckRT(!stack->ready() && !tx->allocBuf(&mx, Payload) && stub && otherStub &&
      !stub->data_() && !otherStub->data_() && !stub->size && !otherStub->size &&
      tx->capacity() == 128 && small->capacity() == 64,
    log_("stopped factory publishes stable Tx geometry and backingless Rx stubs"));
  stub = otherStub = nullptr;
  bool started = mx.start();
  ZuCheckRT(started && stack->ready(), log_("registered packet Stack starts on mx owners"));
  if (!started) { app.final(); alarm(0); return; }
  ZmSemaphore owner;
  bool scheduler = false;
  mx.rxRun([&mx, &scheduler, &owner] {
    scheduler = mx.invoked(mx.rxThread()); owner.post();
  });
  owner.wait();
  ZuCheckRT(scheduler, log_("mx wake path interrupts the delegated blocking receive call"));
  Zutcp::ClosedTuple history[3];
  bool remembered = false;
  mx.rxRun([&stack, &history, &remembered, &owner] {
    for (unsigned i = 0; i < 3; ++i)
      history[i] = stack->rememberClosed({.key = {
	ZiSockAddr{ZiIP{"198.18.0.1"}, uint16_t(18000 + i)},
	ZiSockAddr{ZiIP{"198.18.0.2"}, 443}}, .txNext = 100 + i, .rxNext = 200 + i});
    auto stats = stack->closedTupleStats();
    remembered = stats.capacity == 3 && stats.count == 3;
    owner.post();
  });
  owner.wait();
  ZuCheckRT(remembered, log_("Rx inserts fixed records without application or packet owners"));


  int wire = socket(AF_PACKET, SOCK_RAW | SOCK_NONBLOCK | SOCK_CLOEXEC, htons(ETH_P_ALL));
  sockaddr_ll address{};
  address.sll_family = AF_PACKET;
  address.sll_protocol = htons(ETH_P_ALL);
  address.sll_ifindex = peer;
  bool bound = wire >= 0 && !bind(wire, reinterpret_cast<sockaddr *>(&address), sizeof(address));
  ZuCheckRT(bound, log_("independent packet peer bound"));
  if (!bound) { if (wire >= 0) close(wire); ZmAssert_(mx.stop()); app.final(); alarm(0); return; }

  ControlProbe pressure{&mx, stack};
  mx.txRun([&mx, &tx, &small, &stack, &pressure, &owner] {
    enum { DataFrames = 6, Accepted = 5 }; // fixture pools: 3 + 1 + 2 slots
    ZmRef<ZiIOBuf> data[DataFrames];
    for (unsigned i = 0; i < 3; ++i) data[i] = tx->allocBuf(&mx, Payload);
    data[3] = small->allocBuf(&mx, Payload);
    data[4] = stack->allocPacket(&mx, ZiIPType::V4);
    data[5] = stack->allocPacket(&mx, ZiIPType::V4);
    bool allocated = true;
    for (auto &p : data) allocated &= bool(p);
    if (!allocated) { owner.post(); return; }
    bool admitted = true;
    for (unsigned i = 0; i < DataFrames; ++i) {
      ControlProbe::frame(data[i], 0x60 + i);
      bool sent = stack->sendPacket(data[i]);
      admitted &= i < Accepted ? sent && !data[i]->data_() :
        !sent && data[i]->data_() && data[i]->length == 64;
    }
    auto control = stack->allocPacket(&mx, ZiIPType::V4, true);
    auto next = stack->allocPacket(&mx, ZiIPType::V4, true);
    bool retained = false, second = false;
    if (control && next) {
      Zutcp::SentPkt record;
      record.pn = 90;
      record.bytes = 64;
      record.ackEliciting = record.inFlight = true;
      if (pressure.retained.add(record, &pressure.lease)) {
        ControlProbe::frame(control, 0x66);
        control->retained = pressure.lease;
        retained = stack->sendPacket(control) && !control->data_() && **pressure.lease;
        control->retained = nullptr;
      }
      ControlProbe::frame(next, 0x67);
      second = stack->sendPacket(next) && !next->data_();
    }
    bool exhausted = !stack->allocPacket(&mx, ZiIPType::V4, true);
    pressure.during = stack->txAdmission();
    pressure.submitted = admitted && retained && second && exhausted;
    owner.post();
  });
  owner.wait();
  ZuCheckRT(pressure.submitted && pressure.during.capacity == 8 &&
      pressure.during.reserved == 3 && pressure.during.outstanding == 7 &&
      !pressure.during.controlFree && pressure.during.dataStalls == 1 &&
      pressure.during.controlStalls == 1,
    log_("reserved admission permits control while all application pools are exhausted"));
  unsigned seen = 0;
  uint8_t pressureWire[2048];
  for (unsigned tries = 0; tries < 32 && seen != 0xdf; ++tries) {
    pollfd event{wire, POLLIN, 0};
    if (poll(&event, 1, 1000) <= 0) break;
    auto n = recv(wire, pressureWire, sizeof(pressureWire), 0);
    if (n == 64 && pressureWire[12] == 0x88 && pressureWire[13] == 0xb5 &&
        pressureWire[14] >= 0x60 && pressureWire[14] < 0x68)
      seen |= 1U << (pressureWire[14] - 0x60);
  }
  ZuCheckRT(seen == 0xdf, log_("peer sees admitted data/control and no refused data"));
  pressure.deadline = Zm::now() + ZuTime{2};
  mx.txRun([&pressure] { pressure.run(); });
  pressure.done.wait();
  ZuCheckRT(pressure.kernelOnly,
    log_("CQ restores admission while transport recovery still owns control backing"));

  ZmRef<Zi::IOBuf> reserves[2]; // remaining two slots in the three-packet Tx pool
  uint8_t *sentOrigin = nullptr;
  bool accepted = false, isolated = false;
  mx.txRun([&mx, &tx, &stack, &reserves, &sentOrigin, &accepted, &isolated, &owner] {
    auto packet = tx->allocBuf(&mx, Payload);
    reserves[0] = tx->allocBuf(&mx, Payload);
    reserves[1] = tx->allocBuf(&mx, Payload);
    // Acquire the control provider through its native raw pool, independently.
    auto ctl = stack->control()->pool.alloc();
    isolated = packet && reserves[0] && reserves[1] && !tx->allocBuf(&mx, 1) && ctl;
    if (ctl) stack->control()->pool.free(ctl);
    if (packet) {
      sentOrigin = packet->data_();
      memset(sentOrigin, 0, packet->skip + packet->length);
      memset(sentOrigin, 0xff, 6);
      sentOrigin[6] = 2;
      sentOrigin[12] = 0x88; sentOrigin[13] = 0xb5;
      for (unsigned i = 0; i < Payload; ++i) packet->data()[i] = i ^ 0x7d;
      packet->rewind(packet->skip);
      auto length = packet->length;
      packet->length = packet->size + 1; // exceeds the advertised packet extent
      auto refused = !stack->sendPacket(packet) && packet->data_() == sentOrigin;
      packet->length = length;
      accepted = refused && stack->sendPacket(packet) && !packet->data_() &&
        !packet->size && !packet->skip && !packet->length &&
        !stack->sendPacket(packet);
    }
    owner.post();
  });
  owner.wait();
  ZuCheckRT(accepted && isolated,
    log_("Tx owner submits original backing under application pool pressure"));
  unsigned captured = 0;
  uint8_t observed[2048]; // fixture MTU 1500
  auto limit = Zm::now() + ZuTime{10};
  while (captured < Repeats && Zm::now() < limit) {
    pollfd event{wire, POLLIN, 0};
    poll(&event, 1, 100);
    auto n = recv(wire, observed, sizeof(observed), MSG_DONTWAIT);
    if (n == WireSize && observed[12] == 0x88 && observed[13] == 0xb5) {
      bool valid = true;
      for (unsigned i = 0; i < Payload; ++i) valid &= observed[Head + i] == (i ^ 0x7d);
      if (valid) ++captured;
    }
  }
  ZuCheckRT(captured == Repeats,
    log_("peer receives the exact bytes from transferred backing"));
  Probe probe{&mx, tx};
  probe.deadline = Zm::now() + ZuTime{10};
  mx.txRun([&probe] { probe.run(); });
  probe.done.wait();
  ZuCheckRT(probe.packet && probe.packet->data_() == sentOrigin,
    log_("CQ restores the exact pool slot after submitted metadata has destructed"));

  ZuCheckRT(WriterCxn::controlSelection(),
    log_("FIN-only encoding selects control backing; positive DATA selects application backing"));
  ZmRef<WriterCxn> cxn = new WriterCxn{&app};
  mx.rxRun([&cxn, &owner] { cxn->prepareRx(); owner.post(); });
  owner.wait();
  bool produced = false, whole = false, exhausted = false;
  mx.txRun([&cxn, &stack, &owner, &produced, &whole, &exhausted] {
    cxn->prepareTx();
    auto closed = cxn->writer_();
    bool noCredit = closed.failed();
    cxn->extendTxCredit(12);
    auto writer = cxn->writer_();
    auto first = writer.allocBuf(writer.headRoom());
    auto second = writer.allocBuf(writer.headRoom());
    produced = noCredit && first && second && first->registered() && second->registered() &&
      first->size - first->skip == 12 && !stack->allocPacket(cxn, ZiIPType::V4);
    if (first && second) {
      first->append(reinterpret_cast<const uint8_t *>("first---"), 8);
      second->append(reinterpret_cast<const uint8_t *>("second--"), 8);
      produced &= writer.appendBuf(ZuMv(first)) && writer.appendBuf(ZuMv(second));
      produced &= cxn->txQueueCount() == 2 && cxn->sendNext(stack);
      whole = cxn->sendNext(stack) && !cxn->txQueueCount() &&
	!cxn->txBufferedBytes() && !cxn->txCreditAvailable() &&
	cxn->txCreditLimit() == 12 && cxn->txNext() == 16;
      exhausted = !cxn->sendNext(stack) && !cxn->extraAlloc && cxn->submitted == 2 &&
	cxn->controls == 1 && cxn->original && cxn->detached && cxn->bounded;
    }
    owner.post();
  });
  owner.wait();
  ZuCheckRT(produced && whole && exhausted,
    log_("positive credit sends both original packets and charges every byte without allocation"));
  unsigned dataSeen = 0;
  for (unsigned tries = 0; tries < 16 && dataSeen != 3; ++tries) {
    pollfd ready{wire, POLLIN, 0};
    if (poll(&ready, 1, 1000) <= 0) break;
    auto n = recv(wire, observed, sizeof(observed), 0);
    if (n >= 8) {
      if (!memcmp(observed + n - 8, "first---", 8)) dataSeen |= 1;
      if (!memcmp(observed + n - 8, "second--", 8)) dataSeen |= 2;
    }
  }
  ZuCheckRT(dataSeen == 3, log_("independent peer consumes both original producer payloads"));
  RetxProbe retransmit{&mx, cxn, stack};
  retransmit.deadline = Zm::now() + ZuTime{2};
  mx.txRun([&cxn, &retransmit] { cxn->ackFirst(); retransmit.run(); });
  retransmit.done.wait();
  ZuCheckRT(retransmit.sent && cxn->reused && !cxn->extraAlloc &&
      cxn->submitted == 3 && cxn->detached,
    log_("retransmission reuses the retained raw payload address without packet allocation"));
  bool replaySeen = false;
  for (unsigned tries = 0; tries < 16 && !replaySeen; ++tries) {
    pollfd ready{wire, POLLIN, 0};
    if (poll(&ready, 1, 1000) <= 0) break;
    auto n = recv(wire, observed, sizeof(observed), 0);
    replaySeen = n >= 8 && !memcmp(observed + n - 8, "second--", 8);
  }
  ZuCheckRT(replaySeen, log_("independent peer verifies retransmitted payload bytes"));
  mx.rxRun([&mx, &cxn, &owner] {
    cxn->clearRx();
    mx.txRun([&mx, &owner] { mx.rxRun([&owner] { owner.post(); }); });
  });
  owner.wait();
  cxn = nullptr;

  LeaseProbe leases{&mx, stack};
  leases.deadline = Zm::now() + ZuTime{2};
  mx.txRun([&leases] { leases.start(); });
  leases.done.wait();
  if (!leases.retired || !leases.refused || !leases.held)
    std::cout << "# lease submitted=" << leases.submitted << " retired=" << leases.retired
      << " refused=" << leases.refused << " held=" << leases.held << '\n';
  ZuCheckRT(leases.retired && leases.refused && leases.held,
    log_("CQ/loss hold backing; late ACK returns its slot; refusal preserves metadata"));

  uint8_t bytes[64]{}; // independent Ethernet receive fixture
  memset(bytes, 0xff, 6);
  bytes[6] = 2; bytes[12] = 0x88; bytes[13] = 0xb5;
  bool injected = true;
  for (unsigned i = 0; i < RxCount; ++i) {
    bytes[14] = i;
    injected &= sendto(wire, bytes, sizeof(bytes), 0,
      reinterpret_cast<sockaddr *>(&address), sizeof(address)) == sizeof(bytes);
    if (injected) received.wait();
  }
  ZuCheckRT(injected && packets == RxCount && onRx,
    log_("one physical ingress reserve feeds original registered buffers on mx Rx"));
  ZmRef<Zutcp::RxAccount> rxAccount = new Zutcp::RxAccount{1, 17};
  bool counted = false;
  mx.rxRun([&stack, &input, &rxAccount, &counted, &owner] {
    counted = stack->retainRx(input[0], rxAccount) &&
      stack->retainRx(input[0], rxAccount) && rxAccount->frames == 1 &&
      stack->rxAdmission().retained == 1;
    owner.post();
  });
  owner.wait();
  ZuCheckRT(counted, log_("aliases charge the original ingress slot once on Rx"));
  {
    Zutcp::DataRx first{input[0], input[0]->data(), 32, rxHeap, nullptr, 0};
    Zutcp::DataRx second{input[0], input[0]->data() + 32, 32, rxHeap, nullptr, 32};
    input[0] = nullptr;
    bool copied = first.ensure(64) && first.span()[12] == 0x88 &&
      first.span()[13] == 0xb5 && second.registered();
    copied = second.ensure(64) && !second.registered() && copied;
    ZuCheckRT(copied,
      log_("off-owner heap replacement releases both aliases of a physical ingress frame"));
    // Both heap-backed metadata objects remain alive while this final packet
    // reference returns. Normal provider notification must restart empty FILL.
    bytes[14] = 0xa5;
    bool resumed = false;
    auto limit = Zm::now() + ZuTime{2};
    do {
      if (sendto(wire, bytes, sizeof(bytes), 0,
	  reinterpret_cast<sockaddr *>(&address), sizeof(address)) != sizeof(bytes)) break;
      if (!received.timedwait(Zm::now() + ZuTime{0, 1000000})) {
	resumed = true;
	break;
      }
    } while (Zm::now() < limit);
    ZuCheckRT(resumed && extra && extra->data()[14] == 0xa5 &&
	first.span()[12] == 0x88 && !first.registered() && !second.registered(),
      log_("heap-backed metadata does not pin ingress; off-owner replacement resumes Rx"));
    bool retired = false;
    mx.rxRun([&stack, &rxAccount, &retired, &owner] {
      auto stats = stack->rxAdmission();
      retired = !rxAccount->frames && !stats.retained &&
	stats.admitted == 1 && stats.retired == 1;
      owner.post();
    });
    owner.wait();
    ZuCheckRT(retired,
      log_("replacement reconciles the old slot charge before a new ingress incarnation"));
    extra = nullptr;
  }
  bytes[14] = 0xa5;
  // A packet can reach the empty FILL ring before the asynchronous return
  // notification runs. Retry peer traffic, without another mx task/wake, until
  // the one released frame is delivered. Time bounds failure, not success.
  bool reinjected = false;
  auto retryLimit = Zm::now() + ZuTime{2};
  do {
    if (sendto(wire, bytes, sizeof(bytes), 0,
	reinterpret_cast<sockaddr *>(&address), sizeof(address)) != sizeof(bytes)) break;
    if (!received.timedwait(Zm::now() + ZuTime{0, 1000000})) {
      reinjected = true;
      break;
    }
  } while (Zm::now() < retryLimit);
  ZuCheckRT(reinjected && packets == RxCount + 2 && extra && extra->data()[14] == 0xa5,
    log_("off-owner pool return wakes/re-enters receive service without packet readiness"));
  bool lateCharge = false;
  mx.rxRun([&stack, &input, &rxAccount, &lateCharge, &owner] {
    lateCharge = stack->retainRx(input[1], rxAccount) &&
      rxAccount->frames == 1 && stack->rxAdmission().retained == 1;
    owner.post();
  });
  owner.wait();
  ZuCheckRT(lateCharge, log_("a retained ingress charge survives backend shutdown"));
  ZuCheckRT(mx.stop() && !stack->ready(),
    log_("asynchronous Rx then Tx drain retires the raw backend"));
  ZtString<> controlError;
  bool busyControl = !stack->configureControl(3, 3, controlError);
  ZuCheckRT(busyControl && !!controlError && stack->txAdmission().controlCount == 2 &&
      pressure.lease && **pressure.lease && pressure.lease->data()[14] == 0x66,
    log_("stopped reconfiguration preserves retained control backing and reports busy"));
  pressure.retained.clear();
  ZuCheckRT(extra && extra->data()[14] == 0xa5 && probe.packet,
    log_("stopped Stack preserves application-held Rx and Tx backing"));
  ZuCheckRT(leases.shutdownLease && **leases.shutdownLease &&
      leases.shutdownLease->data() == leases.origin && leases.origin[14] == 0x5a,
    log_("backend close preserves recovery's caller lease after Zi metadata destruction"));
  // Owners are stopped. Clearing recovery returns its lease through the still
  // live provider; the subsequent restart also verifies pool drain.
  leases.recovery.clear();
  Zutcp::PktBuild retained;
  ZuCheckRT(leases.heldRange.range.storage &&
      !leases.heldRange.range.storage->pending &&
      retained.add(leases.heldRange.range) && retained.bytes() == 1 &&
      retained.data()[0][0] == 0x5a && **leases.shutdownLease,
    log_("retained raw range outlives Zi metadata, queue and sent-record retirement"));
  retained.reset();
  leases.heldRange = {};
  auto stoppedRx = stack->rxAdmission();
  ZuCheckRT(stoppedRx.retained == 1 && rxAccount->frames == 1 &&
      input[1]->data()[14] == 1,
    log_("stopping preserves accounted application backing without accessing an owner"));
  rxAccount = nullptr; // retire the owner before its final application return
  extra = nullptr;
  for (auto &p : input) p = nullptr;
  auto returnedRx = stack->rxAdmission();
  ZuCheckRT(!returnedRx.retained && returnedRx.admitted == 2 && returnedRx.retired == 2,
    log_("stopped control reconciliation retires a late return after account-owner release"));
  for (auto &p : reserves) p = nullptr;
  probe.packet = nullptr;
  app.final();
  bool configured = app.init(Zutcp::HubParams{&mx}.controlBufCount(3).txControlCredits(3)
    .rxReserveFrames(1).rxRetainFrames(2).rxRetainFramesPerCxn(1).closedTupleRingSize(2));
  ZuCheckRT(configured && app.controlBufCount() == 3 && app.txControlCredits() == 3 &&
      stack->txAdmission().controlCount == 3 && stack->txAdmission().capacity == 9,
    log_("stopped Hub reinitialization changes control count and preserves application counts"));
  auto resizedRx = stack->rxAdmission();
  ZuCheckRT(configured && resizedRx.capacity == RxCount && resizedRx.limit == 2 &&
      resizedRx.perCxnLimit == 1 && resizedRx.reserve == 1 &&
      app.rxRetainFrames() == 2 && app.rxRetainFramesPerCxn() == 1,
    log_("nondefault retention and connection limits preserve physical geometry"));
  // Kernel queue release may outlast close; retry from this control caller,
  // with the original providers and a finite deadline. No owner thread waits.
  bool restarted = false;
  unsigned attempts = 0;
  auto restartDeadline = Zm::now() + ZuTime{2};
  do {
    ++attempts;
    if ((restarted = mx.start())) break;
    usleep(1000);
  } while (Zm::now() < restartDeadline);
  std::cout << "# raw Stack restart attempts=" << attempts << '\n';
  bool running = stack->ready();
  bool historyKept = false;
  mx.rxRun([&stack, &history, &historyKept, &owner] {
    Zutcp::ClosedTuple found;
    auto stats = stack->closedTupleStats();
    historyKept = stats.capacity == 2 && stats.count == 2 &&
      !stack->closedTuple(history[0].key, found) && stack->closedTuple(history[1].key, found) &&
      found.id == history[1].id && found.rxNext == history[1].rxNext &&
      stack->closedTuple(history[2].key, found) && found.id == history[2].id;
    owner.post();
  });
  owner.wait();
  ZuCheckRT(historyKept, log_("restart and stopped shrink preserve newest tuple identities"));

  ZmRef<ZiIOBuf> pressureRx[3];
  bool arrived = restarted;
  for (unsigned i = 0; arrived && i < 3; ++i) {
    bytes[14] = 0x80 + i;
    arrived &= sendto(wire, bytes, sizeof(bytes), 0,
      reinterpret_cast<sockaddr *>(&address), sizeof(address)) == sizeof(bytes);
    if (arrived) {
      arrived = !received.timedwait(Zm::now() + ZuTime{2});
      if (arrived) pressureRx[i] = ZuMv(extra);
    }
  }
  ZuCheckRT(arrived, log_("native admission pressure has three independent ingress frames"));
  if (arrived) {
    bool pressurePassed = false;
    mx.rxRun([&mx, &app, &stack, &pressureRx, &pressurePassed, &owner] {
      ZmRef<WriterCxn> a = new WriterCxn{&app};
      ZmRef<WriterCxn> b = new WriterCxn{&app};
      ZmRef<WriterCxn> c = new WriterCxn{&app};
      a->rxCredit(16); b->rxCredit(16); c->rxCredit(16);
      auto frame = [](ZiIOBuf *buf, uint64_t offset, unsigned length = 1) {
	Zutcp::Frame value;
	value.type = Zutcp::FrameType::Data;
	value.offset = offset;
	value.length = length;
	value.payload = {buf->data() + 14, length};
	return value;
      };
      bool admitted = false;
      bool first = a->receiveFrame(frame(pressureRx[0], 2), pressureRx[0],
	nullptr, nullptr, &admitted) == 0 && admitted;
      bool perCxn = a->receiveFrame(frame(pressureRx[1], 4), pressureRx[1],
	nullptr, nullptr, &admitted) == 0 && !admitted && a->rxCreditUsed() == 3;
      bool second = b->receiveFrame(frame(pressureRx[1], 2), pressureRx[1],
	nullptr, nullptr, &admitted) == 0 && admitted;
      bool shared = c->receiveFrame(frame(pressureRx[2], 4), pressureRx[2],
	nullptr, nullptr, &admitted) == 0 && !admitted && !c->rxCreditUsed();
      auto before = pressureRx[2]->refCount();
      bool service = c->receiveFrame(frame(pressureRx[2], 0), pressureRx[2],
	nullptr, nullptr, &admitted) == 0 && admitted && c->rxBytes() == 1 &&
	pressureRx[2]->refCount() == before;
      bool gap = a->receiveFrame(frame(pressureRx[2], 0, 2), pressureRx[2],
	nullptr, nullptr, &admitted) == 0 && admitted &&
	pressureRx[2]->refCount() == before;
      auto stats = stack->rxAdmission();
      pressurePassed = first && perCxn && second && shared && service && gap &&
	stats.retained == 2 && stats.limit == 2 && stats.perCxnLimit == 1 &&
	a->rxError() == Zutcp::DataError::None && c->rxError() == Zutcp::DataError::None;
      mx.rxRun([a = ZuMv(a), b = ZuMv(b), c = ZuMv(c),
	&mx, &pressurePassed, &owner] {
	// Normal queued Rx dequeue work precedes this observation.
	pressurePassed &= a->rxBytes() == 3 && !a->rxPending();
	a->clearRx(); b->clearRx(); c->clearRx();
	mx.txRun([&mx, &owner] { mx.rxRun([&owner] { owner.post(); }); });
      });
    });
    owner.wait();
    ZuCheckRT(pressurePassed,
      log_("native DATA enforces both caps while gap service retains no reserve"));
    for (auto &p : pressureRx) p = nullptr;
    bool restored = false;
    mx.rxRun([&stack, &restored, &owner] {
      auto stats = stack->rxAdmission();
      restored = !stats.retained && stats.admitted == stats.retired;
      owner.post();
    });
    owner.wait();
    ZuCheckRT(restored, log_("connection teardown and final returns restore shared admission"));
  }
  bool stopped = restarted && mx.stop();
  ZuCheckRT(restarted && running && stopped,
    log_("released pools restart with stable providers: start ", restarted,
	" ready ", running, " stop ", stopped));
  close(wire);
  app.final();
  alarm(0);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  if (geteuid() || !getenv("ZXDP_RX_TEST_IFINDEX") || !getenv("ZXDP_RX_TEST_PEER_IFINDEX")) {
    std::cout << "1..0 # SKIP requires privileged fixture-owned single-queue pair\n";
    return 0;
  }
  ZiLog::init("zutcpstacktest");
  ZiLog::level(0);
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();
  ZuTestMain();
  ZuTestCall(testStack);
  ZiLog::stop();
}
