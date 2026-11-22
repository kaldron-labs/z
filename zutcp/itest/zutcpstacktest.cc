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
#include <zlib/ZuMatcher.hh>
#include <zlib/ZtcHeap.hh>

#include <zlib/ZmAssert.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmThread.hh>
#include <zlib/ZmTime.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmFn.hh>
#include <zlib/ZmRef.hh>

#include <zlib/ZePlatform.hh>

#include <zlib/ZiLog.hh>

#include <zlib/Zutcp.hh>

#include "../test/ZutcpTestInstance.hh"
#include <zlib/ZutcpCxn.hh>

using namespace ZuTestUtil;

enum { RxCount = 4, Head = 14 + 40 + 60, Payload = 32,
  WireSize = Head + Payload, Repeats = 1 }; // exact fixture workload

struct MetaTotals {
  enum { Count = 16 };
  using Keys = ZuStringTL<
    "Zutcp.Data.Tx", "Zutcp.Connection.TxRanges", "Zutcp.Pkt.TxSentNode",
    "Zutcp.Pkt.OutstandingRange", "Zutcp.Pkt.OutstandingFlow", "Zutcp.OutstandingFlows",
    "Zutcp.Pkt.OutstandingExact", "Zutcp.OutstandingExact", "Zutcp.Pkt.RetransmitQueue",
    "Zutcp.Pkt.AckdFrame", "Zutcp.AckdFrameHash", "Zutcp.TxUnackdRange",
    "Zutcp.Connection.TxUnackdRanges", "Zutcp.Connection.TxUnackdIndex",
    "Zutcp.Pkt.TxLostPNs", "Zutcp.Recovery.FrameUpdates">;
  uint64_t requests[Count]{}, returns[Count]{}, bytes[Count]{}, returnedBytes[Count]{};
};

static MetaTotals metaTotals()
{
  MetaTotals result;
  Ztc::HeapMgr::all([&result](Ztc::Heap *heap) {
    static constexpr auto matcher = ZuMatcher<MetaTotals>();
    int i = matcher.exact(heap->telKey().p<0>());
    if (i < 0) return;
    Ztc::HeapTelemetry data;
    heap->telemetry(data);
    uint64_t allocated = data.cacheAllocs + data.heapAllocs;
    uint64_t freed = data.cacheFrees + data.heapFrees;
    result.requests[i] += allocated;
    result.returns[i] += freed;
    result.bytes[i] += allocated * data.size;
    result.returnedBytes[i] += freed * data.size;
  });
  return result;
}

static void reportMeta(unsigned mode, const char *phase,
    const MetaTotals &before, const MetaTotals &after)
{
  static constexpr const char *ids[] = {
    "Zutcp.Data.Tx", "Zutcp.Connection.TxRanges", "Zutcp.Pkt.TxSentNode",
    "Zutcp.Pkt.OutstandingRange", "Zutcp.Pkt.OutstandingFlow", "Zutcp.OutstandingFlows",
    "Zutcp.Pkt.OutstandingExact", "Zutcp.OutstandingExact", "Zutcp.Pkt.RetransmitQueue",
    "Zutcp.Pkt.AckdFrame", "Zutcp.AckdFrameHash", "Zutcp.TxUnackdRange",
    "Zutcp.Connection.TxUnackdRanges", "Zutcp.Connection.TxUnackdIndex",
    "Zutcp.Pkt.TxLostPNs", "Zutcp.Recovery.FrameUpdates"};
  for (unsigned i = 0; i < MetaTotals::Count; ++i) {
    auto allocations = after.requests[i] - before.requests[i];
    auto frees = after.returns[i] - before.returns[i];
    if (!allocations && !frees && (i < 11 || i > 13)) continue;
    std::cout << "# metadata mode=" << mode << " phase=" << phase << " id=" << ids[i]
      << " requests=" << allocations << " returns=" << frees
      << " bytes=" << after.bytes[i] - before.bytes[i]
      << " returnedBytes=" << after.returnedBytes[i] - before.returnedBytes[i] << '\n';
  }
}

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

using WriterApp = Zutcp::Instance;

struct WireEndpoint : Zutcp_::Endpoint_<WireEndpoint> {
  ZmSemaphore ready;
  enum { EndpointRef = false };
  void endpointReady(Zutcp_::Endpoint_<WireEndpoint> *) { ready.post(); }
  void endpointFailed(bool) { ready.post(); }
  void endpointDown_(Zutcp_::Endpoint_<WireEndpoint> *) { }
  void endpointDatagram(Zutcp::Datagram) { }
};

struct WriterCxn : public ZmNHeap<WriterCxn>,
  public Zutcp::Cxn_<WriterApp, WriterCxn, Zutcp::TxBufAlloc<>> {
friend inline ZuStringT<"Zutcp.StackTest.Writer"> ZmNHeapID(WriterCxn *);
  using Base = Zutcp::Cxn_<WriterApp, WriterCxn, Zutcp::TxBufAlloc<>>;
  using Base::txNext;
  using ZmNHeap<WriterCxn>::operator new;
  using ZmNHeap<WriterCxn>::operator delete;

  Zutcp::SentFrameRef retry;
  unsigned extraAlloc = 0, submitted = 0, controls = 0, refused = 0;
  bool offerAck = true, recovering = false, resuming = false;
  unsigned resumedRecovery = 0, resumedFinalACK = 0, finishes = 0, allocRefused = 0;
  unsigned sourceEncodes = 0;
  ZmSemaphore finalClosed;
  bool original = true, detached = true, bounded = true, reused = true, preserved = true;

  explicit WriterCxn(WriterApp *app) : Base{app} { }
  ZmFn<bool()> resume;
  bool flushTx() { return resume ? resume() : false; }
  bool recoveryPending() const { return Base::txDiag().retransmitPending; }
  void resumeTx() override {
    resuming = true;
    Base::resumeTx();
    resuming = false;
  }
  void queueTxFlush(ZiSockAddr = {}) { }
  void queueRetransmit() { }
  void pto() { }
  bool sendSyn() { return false; }
  bool retransmit(bool = false, unsigned budget = 2) {
    if (!recovering) return false;
    bool sent = false;
    for (unsigned i = 0; i < budget; ++i) {
      Zutcp::SentFrameRef ref;
      if (!Base::nextRetransmit(ref)) break;
      bool accepted = Base::sendRecovery(ref, [this, &ref] {
	return Base::sendTCP(ref, false, 0,
	  [this]() { ++extraAlloc; return app()->allocPacket(this, ZiIPType::V4); },
	  [this](auto packet, auto) { return submit(app(), ZuMv(packet)); });
      });
      if (!accepted) { Base::requeueRetransmit(ref); break; }
      if (resuming) ++resumedRecovery;
      sent = true;
    }
    return sent;
  }
  void nativeFinished() {
    ++finishes;
    Base::disconnect_(false, [this] { finalClosed.post(); });
  }
  void prepareFINRx() {
    Base::configLocalParams(app());
    prepareRx();
  }
  bool peerFIN(bool v6) {
    Zutcp::Datagram packet{{}, {ZiIP{v6 ? "2001:db8::2" : "198.18.0.2"}, 18181}};
    packet.local = {ZiIP{v6 ? "2001:db8::1" : "198.18.0.1"}, 18181};
    packet.tcp = {.seq = 0, .ack = 1, .window = 8, .flags = 0x11};
    return Base::receiveSegment(ZuMv(packet)) && Base::finReceived() &&
      Base::rxComplete() && Base::retireRx(0);
  }
  void beginFIN() {
    Base::extendTxCredit(1);
    Base::fin(); // notifyTx() drives the real packetizer synchronously.
  }
  bool wireFlush(WireEndpoint &endpoint, const Zutcp::IPPath &path) {
    return Base::flushTCP([this, &endpoint, &path](
	const Zutcp::SentFrameRef &ref, bool syn, uint8_t control, bool persist) {
      return Base::sendTCP(ref, syn, control,
	[this, &path]() {
	  auto packet = app()->allocPacket(this, path.local.type(), true);
	  if (!packet) ++allocRefused;
	  return packet;
	}, [this, &endpoint, &path, &ref](auto packet, auto ecn) {
	  bool sent = endpoint.send(ZuMv(packet), {path.remote, 18181}, ecn, true, &path);
	  if (sent) {
	    ++controls;
	    if (!ref.fin && resuming) ++resumedFinalACK;
	  }
	  return sent;
	}, persist);
    });
  }
  int process(Zutcp::Reader &) { return 0; }
  void prepareRx() {
    Base::startHandshake();
    // This synthetic writer bypasses setup; explicitly keep logical data bases at zero.
    Base::setTCPOriginsRx({.local = UINT32_MAX, .peer = UINT32_MAX,
      .localKnown = true, .peerKnown = true});
    Base::establish();
  }
  void prepareTx(bool v6 = false) {
    Base::initClientPathTx(
      ZiSockAddr{ZiIP{v6 ? "2001:db8::1" : "198.18.0.1"}, 18181},
      ZiSockAddr{ZiIP{v6 ? "2001:db8::2" : "198.18.0.2"}, 18181});
  }
  void clearRx() { Base::resetLink(); }
  bool sendNext(Zutcp::Instance *stack) {
    if (offerAck) {
      bool sent = Base::sendTCP({}, false, 0x10,
	[this, stack]() { return stack->allocPacket(this, ZiIPType::V4, true); },
	[this, stack](auto packet, auto) {
	  return submit(stack, ZuMv(packet), true);
	});
      if (!sent) return false;
      offerAck = false;
    }
    Zutcp::TxRange range;
    if (!Base::txRange(0, range)) return false;
    auto ref = Zutcp::SentFrameRef::data(range, false);
    if (submitted == 1 || recovering) retry = ref;
    auto payload = range.storage->data + range.offset;
    return Base::sendTCP(ref, false, 0,
      [this, stack]() { ++extraAlloc; return stack->allocPacket(this, ZiIPType::V4); },
      [this, stack, payload](auto packet, auto) {
	original &= packet->data() == payload;
	bounded &= packet->length == 8;
	return submit(stack, ZuMv(packet));
      });
  }
  bool submit(Zutcp::Instance *stack, ZmRef<ZiIOBuf> packet,
      bool control = false) {
    memset(packet->data_(), 0, packet->skip);
    memset(packet->data_(), 0xff, 6);
    packet->data_()[6] = 2;
    packet->data_()[12] = 0x88; packet->data_()[13] = 0xb5;
    packet->rewind(packet->skip);
    auto backing = packet->data_();
    auto size = packet->size, length = packet->length;
    bool sent = stack->sendPacket(packet, nullptr);
    if (sent) {
      detached &= !packet->data_() && !packet->size;
      if (control) ++controls; else ++submitted;
    } else {
      ++refused;
      preserved &= packet->data_() == backing && packet->size == size &&
	!packet->skip && packet->length == length;
    }
    return sent;
  }
  void loseForRecovery() {
    for (unsigned i = 0; i < 4; ++i)
      Base::applyTCP({.ack = 0, .window = 8, .flags = 0x10});
  }
  bool sourceOutput(WireEndpoint &endpoint, const Zutcp::IPPath &path,
      bool fin, bool replay = false) {
    if (!replay) {
      Zutcp::TxRange range;
      if (!Base::txRange(0, range)) return false;
      retry = Zutcp::SentFrameRef::data(range, fin);
      if (fin) { Base::extendTxCredit(9); Base::fin(); }
    }
    return Base::sendTCP(retry, false, 0,
      [this, &path]() { return app()->allocPacket(this, path.local.type()); },
      [this, &endpoint, &path, replay](auto packet, auto ecn) {
	++sourceEncodes;
	if (replay) { packet->tcp.ack += 100; packet->tcp.window = 1; }
	return endpoint.send(ZuMv(packet), {path.remote, 18187}, ecn, true, &path);
      });
  }
  void ackOriginal(bool fin = false) {
    Base::applyTCP({.ack = unsigned(8 + fin), .window = 8, .flags = 0x10});
  }
  void ackFirst() {
    Zutcp::TCPHeader header;
    header.flags = 0x10;
    header.ack = 8;
    header.window = 4; // retain the original right edge at byte 12
    Base::applyTCP(header);
  }
  bool replay(Zutcp::Instance *stack) {
    return Base::sendTCP(retry, false, 0,
      [this, stack]() { ++extraAlloc; return stack->allocPacket(this, ZiIPType::V4); },
      [this, stack](auto packet, auto) {
	auto payload = packet->data();
	reused &= payload == retry.range.storage->data + retry.range.offset &&
	  packet->length == 8 && !memcmp(payload, "second--", 8);
	return submit(stack, ZuMv(packet));
      });
  }
  bool wireReplay(WireEndpoint &endpoint, const Zutcp::IPPath &path,
      uint32_t ack, uint8_t *&frame, unsigned &length, unsigned mode = 0) {
    return Base::sendTCP(retry, false, 0,
      [this]() { return app()->allocPacket(this, ZiIPType::V4); },
      [this, &endpoint, &path, ack, &frame, &length, mode](auto packet, auto ecn) {
	packet->tcp.ack = ack;
	packet->tcp.window = uint16_t(ack);
	// Exercise the endpoint with changed wire options/FIN without changing
	// this writer's synthetic recovery history. Keep the payload in place.
	packet->tcp.options = {};
	if (mode & 1) {
	  packet->tcp.options.flags = Zutcp::TCPOptions::TS;
	  packet->tcp.options.tsval = ack;
	  packet->tcp.options.tsecr = ack - 1;
	  packet->tcp.options.sackCount = ack == 100 ? 1 : 2;
	  packet->tcp.options.sack[0] = {ack + 10, ack + 20};
	  packet->tcp.options.sack[1] = {ack + 30, ack + 40};
	}
	if (mode & 2) packet->tcp.flags |= 0x01;
	auto head = Zutcp::tcpHead(path.local.type(), packet->tcp);
	auto start = packet->data() - head;
	auto &source = *retry.range.storage;
	packet->data__ = uintptr_t(start) | ZiIOBuf::Builtin | ZiIOBuf::Registered;
	packet->size = source.size - unsigned(start - source.data);
	packet->skip = head;
	frame = packet->data_();
	length = Zutcp::tcpHead(path.local.type(), packet->tcp) + packet->length;
	if (length < 60) length = 60; // Ethernet minimum, excluding FCS
	return endpoint.send(ZuMv(packet), {path.remote, 18184}, ecn, true, &path);
      });
  }
  bool finControl(Zutcp::Instance *stack) {
    Base::extendTxCredit(17);
    Base::fin();
    Zutcp::SentFrameRef ref;
    ref.kind = Zutcp::SentFrameKind::Data;
    ref.offset = Base::txNext();
    ref.fin = true;
    return Base::sendTCP(ref, false, 0,
      [this, stack]() { return stack->allocPacket(this, ZiIPType::V4, true); },
      [this, stack](auto packet, auto) {
	bounded &= !packet->length;
	return submit(stack, ZuMv(packet), true);
      });
  }

};

// Independent bytewise reference; do not use the production checksum dispatch.
static unsigned wireSum(const uint8_t *data, unsigned length)
{
  unsigned sum = 0;
  for (; length > 1; data += 2, length -= 2) sum += (unsigned(data[0])<<8) | data[1];
  if (length) sum += unsigned(*data)<<8;
  while (sum>>16) sum = (sum & 0xffff) + (sum>>16);
  return sum;
}

static void checkHeldHeaders(
  ZiMultiplex &mx, Zutcp::Instance *stack, WriterCxn *cxn, int wire)
{
  WireEndpoint endpoint;
  ZmSemaphore owner, entered, release;
  bool opened = false;
  mx.rxRun([&endpoint, stack, &opened, &owner] {
    opened = endpoint.init(stack) && endpoint.open(
      Zutcp::PathMode::ServerUnconnected, ZiIP{"198.18.0.1"}, 18183);
    owner.post();
  });
  owner.wait();
  bool ready = opened && !endpoint.ready.timedwait(Zm::now(2));
  ZuCheckRT(ready, log_("open real encoder endpoint for the held-submission check"));
  for (unsigned mode = 0; ready && mode < 8; ++mode) {
    bool retired = false;
    auto deadline = Zm::now(2);
    do {
      mx.txRun([stack, &retired, &owner] {
	retired = !stack->txAdmission().outstanding;
	owner.post();
      });
      owner.wait();
    } while (!retired && Zm::now() < deadline);
    ZuCheckRT(retired, log_("observe prior device retirement before gating the sender"));
    mx.run([&entered, &release] { entered.post(); release.wait(); }, 3);
    entered.wait();
    uint8_t snapshot[1600];
    bool accepted = false, refused = false, unchanged = false;
    unsigned length = 0;
    mx.txRun([&endpoint, stack, cxn, &snapshot, &accepted, &refused,
	&unchanged, &length, &owner, mode] {
      Zutcp::IPPath path{
	.local = ZiIP{mode & 4 ? "2001:db8::1" : "198.18.0.1"},
	.remote = ZiIP{mode & 4 ? "2001:db8::2" : "198.18.0.2"}, .mtu = 1500};
      memset(path.remoteMAC, 0xff, sizeof(path.remoteMAC));
      path.localMAC[0] = 2;
      uint8_t *frame = nullptr;
      accepted = cxn->wireReplay(endpoint, path, 100, frame, length, mode);
      if (accepted && length <= sizeof(snapshot)) {
	memcpy(snapshot, frame, length);
	auto before = stack->txAdmission().outstanding;
	refused = true;
	unchanged = true;
	auto held = frame;
	auto heldLength = length;
	for (unsigned attempt = 1; attempt <= 3; ++attempt) {
	  refused &= !cxn->wireReplay(endpoint, path, 100 + 100 * attempt,
	    frame, length, mode) && stack->txAdmission().outstanding == before;
	  unchanged &= !memcmp(held, snapshot, heldLength);
	}
	length = heldLength;
      }
      owner.post();
    });
    owner.wait();
    release.post();
    bool received = false;
    uint8_t observed[1600];
    for (unsigned attempt = 0; accepted && attempt < 16 && !received; ++attempt) {
      pollfd readable{wire, POLLIN, 0};
      if (poll(&readable, 1, 1000) <= 0) break;
      auto n = recv(wire, observed, sizeof(observed), 0);
      if (n != int(length) || memcmp(observed, snapshot, length)) continue;
      auto ip = observed + 14;
      unsigned l3 = mode & 4 ? 40 : 20;
      unsigned segment = mode & 4 ? (unsigned(ip[4])<<8) | ip[5] :
	((unsigned(ip[2])<<8) | ip[3]) - l3;
      unsigned pseudo = wireSum(ip + (mode & 4 ? 8 : 12), mode & 4 ? 32 : 8) +
	segment + 6;
      unsigned sum = pseudo + wireSum(ip + l3, segment);
      while (sum>>16) sum = (sum & 0xffff) + (sum>>16);
      received = sum == 0xffff && (mode & 4 || wireSum(ip, l3) == 0xffff);
    }
    std::cout << "# held header mode=" << mode << " accepted=" << accepted
      << " refused=" << refused << " unchanged=" << unchanged
      << " peer=" << received << " wireBytes=" << length << '\n';
    ZuCheckRT(accepted && refused && unchanged && received,
      log_("refused recovery preserves the held frame; peer receives its exact bytes/checksum"));
  }
  endpoint.disconnect([&owner] { owner.post(); });
  owner.wait();
}

struct RetxProbe {
  ZiMultiplex *mx;
  WriterCxn *cxn;
  Zutcp::Instance *stack;
  ZmSemaphore done;
  ZmScheduler::Timer timer;
  ZuTime deadline;
  bool sent = false, refused = false;

  void run() {
    if ((sent = cxn->replay(stack))) {
      // A duplicate replay can be refused by pacing or kernel-held backing.
      // Either refusal must preserve the retained payload and send count.
      auto previous = cxn->submitted;
      auto backing = cxn->retry.range.storage->data;
      refused = !cxn->replay(stack) && cxn->submitted == previous &&
	cxn->retry.range.storage->data == backing &&
	!memcmp(backing + cxn->retry.range.offset, "second--", 8);
      done.post();
      return;
    }
    if (Zm::now() >= deadline) { done.post(); return; }
    mx->add(&timer, Zm::now() + ZuTime{0, 1000000}, ZmScheduler::Update,
      [this](auto &&arm) { return arm([this] { run(); }); }, mx->txThread());
  }
};

// Two internal slots: recovery holds one, and an unretained marker proves CQ
// progress before ACK. All queue/pool operations run on the same Tx owner.
struct LeaseProbe {
  ZiMultiplex *mx;
  Zutcp::Instance *stack;
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
    Zutcp::TxRange range;
    range.length = 1;
    p.addFrame(Zutcp::SentFrameRef::data(range, false));
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
    Zutcp::PktAckBatch ack;
    bool ackd = recovery.ackBytesBatch(1, ack, 8) && !recovery.count();
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
    dataPacket.frames.clear();
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
  Zutcp::Instance *stack;
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
      lease && **lease;
    done.post();
  }
};

struct IdleProgress {
  ZiMultiplex *mx;
  Zutcp::Instance *stack;
  WriterCxn *cxn;
  unsigned wanted = 1;
  bool closed = false;
  ZmScheduler::Timer timer;
  ZmSemaphore done;
  ZuTime deadline;
  bool resumed = false;

  void run() {
    resumed = cxn->submitted == wanted && (closed || !cxn->txQueueCount()) &&
      (!cxn->recovering || (!cxn->recoveryPending() && cxn->resumedRecovery == 1)) &&
      !stack->txAdmission().outstanding;
    if (resumed || Zm::now() >= deadline) { done.post(); return; }
    mx->add(&timer, Zm::now() + ZuTime{0, 1000000}, ZmScheduler::Update,
      [this](auto &&arm) { return arm([this] { run(); }); }, mx->txThread());
  }
};

// Fill instance DATA admission with the sender gated, then refuse the first
// original on another connection. Its producer never submits another byte.
static void checkIdleRefusal(ZiMultiplex &mx, Zutcp::Instance *stack,
    Zi::IOBufAllocator *tx, Zi::IOBufAllocator *small, int wire, unsigned scenario,
    MetaTotals *samples = nullptr)
{
  ZmRef<WriterCxn> cxn = new WriterCxn{stack};
  ZmSemaphore owner, entered, release;
  mx.rxRun([cxn, &owner] { cxn->prepareRx(); owner.post(); });
  owner.wait();
  mx.run([&entered, &release] { entered.post(); release.wait(); }, 3);
  entered.wait();
  bool refused = false;
  mx.txRun([&mx, stack, tx, small, cxn, scenario, samples, &refused, &owner] {
    cxn->prepareTx();
    cxn->offerAck = scenario == 3;
    cxn->extendTxCredit(8);
    if (samples) samples[0] = metaTotals();
    auto writer = cxn->writer_();
    auto original = writer.allocBuf(writer.headRoom());
    if (!original) { owner.post(); return; }
    original->append(reinterpret_cast<const uint8_t *>("resume--"), 8);
    bool queued = writer.appendBuf(ZuMv(original));
    bool recovery = scenario == 4;
    bool primed = true;
    if (recovery) {
      cxn->recovering = true;
      primed = cxn->sendNext(stack);
    }
    if (samples) samples[1] = metaTotals();
    ZmRef<ZiIOBuf> fill[5];
    for (unsigned i = 0; i < 3; ++i) fill[i] = tx->allocBuf(&mx, Payload);
    fill[3] = small->allocBuf(&mx, Payload);
    if (!recovery) fill[4] = stack->allocPacket(&mx, ZiIPType::V4);
    bool full = true;
    for (unsigned i = 0; i < (recovery ? 4 : 5); ++i) {
      auto &packet = fill[i];
      if (!packet) { full = false; continue; }
      ControlProbe::frame(packet, 0xa1);
      full &= stack->sendPacket(packet);
    }
    if (scenario == 3) {
      // The prior ownership fixture retains one control slot. Hold the other
      // under a kernel lease so ACK allocation must wait for this CQ.
      auto control = stack->allocPacket(&mx, ZiIPType::V4, true);
      if (control) {
	ControlProbe::frame(control, 0xa2);
	full &= stack->sendPacket(control);
      } else full = false;
    }
    cxn->resume = [link = cxn.ptr(), stack] { return link->sendNext(stack); };
    if (recovery) cxn->loseForRecovery();
    bool attempted = recovery ? cxn->retransmit() : cxn->flushTx();
    refused = queued && primed && full && !attempted &&
      (scenario == 3 ? !cxn->controls : recovery ? cxn->refused >= 1 : cxn->refused == 1) &&
      cxn->txQueueCount() == unsigned(!recovery) &&
      cxn->submitted == unsigned(recovery) &&
      (!recovery || cxn->recoveryPending()) &&
      stack->txAdmission().outstanding == (scenario == 3 ? 6 : 5);
    if (samples) samples[2] = metaTotals();
    if (scenario == 4) std::cout << "# recovery queued=" << queued << " primed=" << primed
      << " full=" << full << " attempted=" << attempted << " refused=" << cxn->refused
      << " queuedCount=" << cxn->txQueueCount() << " sent=" << cxn->submitted
      << " recovery=" << cxn->recoveryPending() << " ref=" << bool(cxn->retry)
      << " refBytes=" << cxn->retry.length
      << " outstanding=" << stack->txAdmission().outstanding << '\n';
    owner.post();
  });
  owner.wait();
  if (scenario == 1) {
    mx.rxRun([cxn, &mx, &owner] {
      cxn->clearRx();
      mx.txRun([&mx, &owner] { mx.rxRun([&owner] { owner.post(); }); });
    });
    owner.wait();
  } else if (scenario == 2) {
    mx.rxRun([cxn, &owner] { cxn->disconnect_(false, [&owner] { owner.post(); }); });
    owner.wait();
  }
  release.post();
  ZuCheckRT(refused, log_("output is refused under held admission; scenario ", scenario));
  IdleProgress progress{&mx, stack, cxn};
  bool cancelled = scenario == 1 || scenario == 2;
  if (cancelled) progress.wanted = 0;
  if (scenario == 4) progress.wanted = 2;
  progress.closed = scenario == 2;
  progress.deadline = Zm::now() + ZuTime{2};
  mx.txRun([&progress] { progress.run(); });
  progress.done.wait();
  unsigned matches = 0, wanted = cancelled ? 1 : scenario == 4 ? 2 : 1;
  uint8_t bytes[2048];
  for (unsigned attempt = 0; attempt < 16 && matches < wanted; ++attempt) {
    pollfd ready{wire, POLLIN, 0};
    if (poll(&ready, 1, cancelled ? 0 : 1000) <= 0) break;
    auto n = recv(wire, bytes, sizeof(bytes), 0);
    if (n >= 8 && !memcmp(bytes + n - 8, "resume--", 8)) ++matches;
  }
  bool exact = matches == (cancelled ? 0 : wanted);
  ZuCheckRT(progress.resumed && exact,
    log_("CQ resumes idle refused output, while reset/close cancels old work; scenario ",
      scenario, " completed ", progress.resumed, " peer bytes ", exact));
  mx.txRun([cxn, samples, &owner] {
    cxn->resume = {};
    if (samples) samples[3] = metaTotals();
    owner.post();
  });
  owner.wait();
  mx.rxRun([cxn, &mx, &owner] {
    cxn->clearRx();
    mx.txRun([&mx, &owner] { mx.rxRun([&owner] { owner.post(); }); });
  });
  owner.wait();
}

// One control slot is retained by ControlProbe; the other carries the local
// FIN behind the gated sender. Acknowledging that FIN releases its caller lease,
// but the peer-FIN ACK must wait for CQ before it can allocate the same slot.
static void checkFinalACK(ZiMultiplex &mx, Zutcp::Instance *stack, int wire, bool v6)
{
  ZmRef<WriterCxn> cxn = new WriterCxn{stack};
  WireEndpoint endpoint;
  Zutcp::IPPath path{.local = ZiIP{v6 ? "2001:db8::1" : "198.18.0.1"},
    .remote = ZiIP{v6 ? "2001:db8::2" : "198.18.0.2"}, .mtu = 1500};
  memset(path.remoteMAC, 0xff, sizeof(path.remoteMAC));
  path.localMAC[0] = 2;
  ZmSemaphore owner, entered, release;
  bool opened = false;
  mx.rxRun([stack, cxn, &endpoint, &opened, &owner, &path] {
    cxn->prepareFINRx();
    opened = endpoint.init(stack) && endpoint.open(
      Zutcp::PathMode::ServerUnconnected, path.local, 18185);
    owner.post();
  });
  owner.wait();
  bool ready = opened && !endpoint.ready.timedwait(Zm::now(2));
  ZuCheckRT(ready, log_("open real encoder for refused final FIN ACK"));
  if (!ready) return;
  mx.run([&entered, &release] { entered.post(); release.wait(); }, 3);
  entered.wait();
  bool local = false;
  mx.txRun([cxn, stack, &endpoint, &path, &local, &owner, v6] {
    cxn->prepareTx(v6);
    cxn->resume = [link = cxn.ptr(), &endpoint, &path] {
      return link->wireFlush(endpoint, path);
    };
    cxn->beginFIN();
    local = cxn->controls == 1 && stack->txAdmission().outstanding == 1;
    owner.post();
  });
  owner.wait();
  bool peer = false;
  mx.rxRun([cxn, &mx, &peer, &owner, v6] {
    peer = cxn->peerFIN(v6);
    mx.txRun([&mx, &owner] {
      mx.rxRun([&mx, &owner] { mx.txRun([&owner] { owner.post(); }); });
    });
  });
  owner.wait();
  bool blocked = false;
  mx.txRun([cxn, stack, &blocked, &owner] {
    blocked = cxn->controls == 1 && cxn->allocRefused && !cxn->finishes &&
      stack->txAdmission().outstanding == 1;
    std::cout << "# final ACK before CQ controls=" << cxn->controls
      << " allocRefused=" << cxn->allocRefused << " finishes=" << cxn->finishes
      << " pending=" << stack->txAdmission().outstanding << '\n';
    owner.post();
  });
  owner.wait();
  std::cout << "# final ACK local=" << local << " peer=" << peer << '\n';
  ZuCheckRT(local && peer && blocked,
    log_("peer FIN is retired but its refused ACK cannot finish graceful close"));
  release.post();
  bool closed = !cxn->finalClosed.timedwait(Zm::now(2));
  bool drained = false, completed = false, returned = false;
  auto deadline = Zm::now(2);
  do {
    mx.txRun([cxn, stack, &completed, &drained, &returned, &owner] {
      completed = cxn->controls == 2 && cxn->resumedFinalACK == 1 && cxn->finishes == 1;
      drained = !stack->txAdmission().outstanding;
      if (drained) {
	auto slot = stack->allocPacket(cxn.ptr(), ZiIPType::V4, true);
	returned = slot && !stack->allocPacket(cxn.ptr(), ZiIPType::V4, true);
	std::cout << "# final ACK after CQ controls=" << cxn->controls
	  << " resume=" << cxn->resumedFinalACK << " finishes=" << cxn->finishes
	  << " returned=" << returned << '\n';
      }
      owner.post();
    });
    owner.wait();
  } while (!drained && Zm::now() < deadline);
  unsigned seen = 0;
  for (unsigned attempt = 0; attempt < 16 && seen != 3; ++attempt) {
    pollfd readable{wire, POLLIN, 0};
    if (poll(&readable, 1, 1000) <= 0) break;
    uint8_t bytes[2048];
    auto n = recv(wire, bytes, sizeof(bytes), 0);
    if (n != (v6 ? 74 : 60) || bytes[12] != (v6 ? 0x86 : 8) ||
	bytes[13] != (v6 ? 0xdd : 0) || bytes[14] != (v6 ? 0x60 : 0x45)) continue;
    auto tcp = bytes + (v6 ? 54 : 34);
    if (tcp[0] != (18185>>8) || tcp[1] != (18185 & 255)) continue;
    unsigned sum = wireSum(bytes + (v6 ? 22 : 26), v6 ? 32 : 8) +
      20 + 6 + wireSum(tcp, 20);
    while (sum>>16) sum = (sum & 0xffff) + (sum>>16);
    bool exact = sum == 0xffff && (v6 || wireSum(bytes + 14, 20) == 0xffff);
    if (!v6) for (unsigned i = 54; i < 60; ++i) exact &= !bytes[i];
    if (exact && tcp[13] == 0x11 && !tcp[7] && !tcp[11]) seen |= 1;
    if (exact && tcp[13] == 0x10 && tcp[7] == 1 && tcp[11] == 1) seen |= 2;
  }
  ZuCheckRT(closed && completed && drained && returned && seen == 3,
    log_("CQ resumes final ACK exactly once, completes close and sends valid FIN/ACK; "
      "IPv6 ", v6));
  if (!closed) {
    mx.rxRun([cxn, &owner] { cxn->disconnect_(false, [&owner] { owner.post(); }); });
    owner.wait();
  }
  mx.txRun([cxn, &owner] { cxn->resume = {}; owner.post(); });
  owner.wait();
  endpoint.disconnect([&owner] { owner.post(); });
  owner.wait();
}

// Hold the other internal DATA slot so allocation identifies this source's
// exact final return, independently of the retained range metadata lifetime.
static void checkSourceRetirement(ZiMultiplex &mx, Zutcp::Instance *stack, int wire)
{
  for (unsigned mode = 0; mode < 8; ++mode) {
    bool v6 = mode & 4, ackFirst = mode & 2, fin = mode & 1;
    ZmRef<WriterCxn> cxn = new WriterCxn{stack};
    WireEndpoint endpoint;
    Zutcp::IPPath path{.local = ZiIP{v6 ? "2001:db8::1" : "198.18.0.1"},
      .remote = ZiIP{v6 ? "2001:db8::2" : "198.18.0.2"}, .mtu = 1500};
    memset(path.remoteMAC, 0xff, 6);
    path.localMAC[0] = 2;
    ZmSemaphore owner, entered, release;
    bool ready = false;
    mx.rxRun([cxn, stack, &endpoint, &path, &ready, &owner] {
      cxn->prepareRx();
      ready = endpoint.init(stack) && endpoint.open(
	Zutcp::PathMode::ServerUnconnected, path.local, 18186);
      owner.post();
    });
    owner.wait();
    ready &= !endpoint.ready.timedwait(Zm::now(2));
    ZuCheckRT(ready, log_("open retained DATA/FIN endpoint"));
    mx.run([&entered, &release] { entered.post(); release.wait(); }, 3);
    entered.wait();
    MetaTotals baseline, admitted, attempted, drained;
    ZmRef<Zutcp::PacketBuf> other, returned;
    uint8_t *origin = nullptr;
    bool submitted = false, blocked = false, preserved = false;
    auto family = v6 ? ZiIPType::V6 : ZiIPType::V4;
    mx.txRun([cxn, stack, v6, family, ackFirst, fin, &endpoint, &path, &other, &origin,
	&submitted, &blocked, &preserved, &baseline, &admitted, &attempted, &owner] {
      cxn->prepareTx(v6);
      cxn->offerAck = false;
      cxn->extendTxCredit(8);
      baseline = metaTotals();
      auto writer = cxn->writer_();
      auto packet = writer.allocBuf(writer.headRoom());
      if (packet) {
	packet->append(reinterpret_cast<const uint8_t *>("retire--"), 8);
	submitted = writer.appendBuf(ZuMv(packet)) && cxn->sourceOutput(endpoint, path, fin);
      }
      admitted = metaTotals();
      if (submitted) {
	auto &source = *cxn->retry.range.storage;
	origin = source.data;
	auto head = Zutcp::tcpHead(family, source.tcp);
	auto frame = origin + source.payloadOffset - head;
	unsigned length = head + 8;
	uint8_t snapshot[128];
	memcpy(snapshot, frame, length);
	auto before = cxn->sourceEncodes;
	preserved = true;
	for (unsigned attempt = 0; attempt < 3; ++attempt)
	  preserved &= !cxn->sourceOutput(endpoint, path, fin, true) &&
	    !memcmp(frame, snapshot, length);
	preserved &= cxn->sourceEncodes == before + 3 &&
	  stack->txAdmission().outstanding == 1;
	attempted = metaTotals();
	other = stack->allocPacket(cxn.ptr(), family);
	if (other) {
	  auto a = uintptr_t(origin), b = uintptr_t(other->data_());
	  std::cout << "# retained-source payload=8 logical=" << source.size
	    << " poolCapacity=" << source.pool->size << " slotSpacing=" << (a > b ? a - b : b - a)
	    << " fin=" << fin << '\n';
	}
	submitted &= other && source.fin == fin && cxn->txNext() == unsigned(8 + fin) &&
	  stack->txAdmission().outstanding == 1;
	if (ackFirst) {
	  cxn->ackOriginal(fin);
	  blocked = !source.data && !*source.backing &&
	    !stack->allocPacket(cxn.ptr(), family) && stack->txAdmission().outstanding == 1;
	}
      }
      owner.post();
    });
    owner.wait();
    release.post();
    bool retired = false;
    auto deadline = Zm::now(2);
    do {
      mx.txRun([stack, &retired, &owner] {
	retired = !stack->txAdmission().outstanding;
	owner.post();
      });
      owner.wait();
    } while (!retired && Zm::now() < deadline);
    bool exact = false;
    mx.txRun([cxn, stack, family, ackFirst, fin, origin, &blocked, &returned, &exact, &owner] {
      if (cxn->retry) {
	auto &source = *cxn->retry.range.storage;
	if (!ackFirst) {
	  blocked = source.data == origin && *source.backing &&
	    !stack->allocPacket(cxn.ptr(), family);
	  cxn->ackOriginal(fin);
	}
	returned = stack->allocPacket(cxn.ptr(), family);
	cxn->ackOriginal(fin); // Duplicate cumulative ACK cannot release this slot again.
	exact = !source.data && !*source.backing && returned &&
	  returned->data_() == origin && !stack->allocPacket(cxn.ptr(), family);
      }
      owner.post();
    });
    owner.wait();
    bool received = false;
    for (unsigned attempt = 0; submitted && attempt < 16 && !received; ++attempt) {
      pollfd readable{wire, POLLIN, 0};
      if (poll(&readable, 1, 1000) <= 0) break;
      uint8_t bytes[2048];
      auto n = recv(wire, bytes, sizeof(bytes), 0);
      if (n != (v6 ? 82 : 62) || memcmp(bytes + n - 8, "retire--", 8)) continue;
      auto ip = bytes + 14;
      auto tcp = ip + (v6 ? 40 : 20);
      unsigned sum = wireSum(ip + (v6 ? 8 : 12), v6 ? 32 : 8) + 28 + 6 + wireSum(tcp, 28);
      while (sum>>16) sum = (sum & 0xffff) + (sum>>16);
      received = sum == 0xffff && (v6 || wireSum(ip, 20) == 0xffff) &&
	bool(tcp[13] & 1) == fin;
    }
    ZuCheckRT(ready && submitted && preserved && blocked && retired && exact && received,
      log_("retained DATA returns its exact slot once after ACK and CQ; IPv6 ", v6,
	" ACK before CQ ", ackFirst, " FIN ", fin));
    mx.txRun([&other, &returned, &owner] { other = returned = nullptr; owner.post(); });
    owner.wait();
    mx.rxRun([cxn, &mx, &owner] {
      cxn->clearRx();
      mx.txRun([&mx, &owner] { mx.rxRun([&owner] { owner.post(); }); });
    });
    owner.wait();
    endpoint.disconnect([&owner] { owner.post(); });
    owner.wait();
    cxn = nullptr;
    mx.txRun([&drained, &owner] { drained = metaTotals(); owner.post(); });
    owner.wait();
    reportMeta(mode, "admission", baseline, admitted);
    reportMeta(mode, "three-refusals", admitted, attempted);
    reportMeta(mode, "teardown", attempted, drained);
  }
}

static void testStack()
{
  ZuTestScopeRT(testStack);
  alarm(30); // a lost wake/lifecycle transition must fail the fixture
  auto index = unsigned(strtoul(getenv("ZXDP_RX_TEST_IFINDEX"), nullptr, 10));
  auto peer = unsigned(strtoul(getenv("ZXDP_RX_TEST_PEER_IFINDEX"), nullptr, 10));
  ZiMultiplex mx{ZiMxParams{}.rxBufSize(RxCount).rxQuantum(1)
    .scheduler([](auto &p) { p.nThreads(3).thread(3, [](auto &t) { t.isolated(true); }); })};
  ZmSemaphore received;
  ZmRef<Zi::IOBuf> input[RxCount], extra;
  unsigned packets = 0;
  bool onRx = true;
  auto instance = ZutcpTest::makeInstance(&mx, index, 3, {
    .packet = [&mx, &input, &extra, &packets, &onRx, &received](ZmRef<Zi::IOBuf> buf) {
      if (buf->length < 14 || buf->data()[12] != 0x88 || buf->data()[13] != 0xb5) return;
      onRx &= mx.invoked(mx.rxThread()) && buf->registered();
      if (packets < RxCount) input[packets] = ZuMv(buf); else extra = ZuMv(buf);
      ++packets;
      received.post();
    }, .packetCount = 2, .controlBufCount = 2, .txControlCredits = 3,
    .rxRetainFramesPerCxn = 1, .maxData = 8192, .closedTupleRingSize = 3,
    .tfoCookieCacheSize = 3}, 0);
  auto stack = instance.ptr();
  mx.plugin(ZuMv(instance));
  const auto initializedParams = ZutcpTest::Instance::parameters(*stack);
  ZuCHECK(initializedParams.packetCount == 2 &&
      stack->txAdmission().controlCount == 2 && stack->txAdmission().reserved == 3 &&
      stack->maxData() == 8192,
    "initialization parameters must apply native setup and transport policy together");
  const auto provisioned = stack->txAdmission();
  bool invalidCount = ZutcpTest::Instance::apply(*stack,
    {.controlBufCount = 0, .txControlCredits = 1}).is<ZeException>();
  bool invalidCredits = ZutcpTest::Instance::apply(*stack,
    {.controlBufCount = 2, .txControlCredits = 4}).is<ZeException>();
  bool invalidRx = ZutcpTest::Instance::apply(*stack,
    {.controlBufCount = 3, .txControlCredits = 3,
    .rxRetainFramesPerCxn = 0}).is<ZeException>();
  ZuCheckRT(invalidCount && invalidCredits &&
      invalidRx &&
      stack->txAdmission().controlCount == provisioned.controlCount,
    log_("invalid control configuration preserves provisioned pool counts"));
  for (unsigned i = 0; i < 3; ++i) {
    Zutcp::InstanceParams config;
    config.maxCxns = i ? 2 : 0;
    config.maxPendingCxns = i == 1 ? 0 : 3;
    bool rejected = ZutcpTest::Instance::apply(*stack, config).is<ZeException>();
    auto slots = stack->cxnAdmission();
    ZuCheckRT(rejected &&
	!slots.total && !slots.pending && slots.maxCxns == Zutcp::DefaultMaxCxns &&
	slots.maxPendingCxns == Zutcp::DefaultMaxPendingCxns,
      log_("invalid connection caps must preserve stopped admission configuration"));
  }
  for (auto timeout : {ZuTime{}, ZuTime{0}, ZuTime{-1}}) {
    Zutcp::InstanceParams config;
    config.connectTimeout = timeout;
    bool rejected = ZutcpTest::Instance::apply(*stack, config).is<ZeException>();
    ZuCheckRT(rejected &&
	stack->txAdmission().controlCount == provisioned.controlCount,
      log_("invalid connect deadlines must preserve stopped configuration"));
  }
  for (auto timeout : {ZuTime{}, ZuTime{-1}}) {
    Zutcp::InstanceParams config;
    config.userTimeout = timeout;
    bool rejected = ZutcpTest::Instance::apply(*stack, config).is<ZeException>();
    ZuCheckRT(rejected &&
	stack->txAdmission().controlCount == provisioned.controlCount,
      log_("invalid user deadlines must preserve stopped configuration"));
  }
  for (bool finWait : {false, true}) for (auto timeout : {ZuTime{}, ZuTime{0}, ZuTime{-1}}) {
    Zutcp::InstanceParams config;
    if (finWait) config.finWaitTimeout = timeout;
    else config.closeDrainTimeout = timeout;
    bool rejected = ZutcpTest::Instance::apply(*stack, config).is<ZeException>();
    ZuCheckRT(rejected &&
	stack->txAdmission().controlCount == provisioned.controlCount,
      log_("invalid close deadlines must preserve stopped configuration"));
  }
  {
    Zutcp::InstanceParams config;
    config.closedTupleRingSize = 0;
    bool rejected = ZutcpTest::Instance::apply(*stack, config).is<ZeException>();
    ZuCheckRT(rejected &&
	stack->closedTupleStats().capacity == initializedParams.closedTupleRingSize,
      log_("zero closed tuple capacity must preserve stopped configuration"));
  }
  auto &app = *stack;
  Zutcp::InstanceParams badConfig;
  badConfig.controlBufCount = 2;
  badConfig.txControlCredits = 4;
  bool rejected = ZutcpTest::Instance::apply(app, badConfig).is<ZeException>();
  ZuCheckRT(rejected &&
      stack->txAdmission().controlCount == provisioned.controlCount,
    log_("failed Instance control configuration preserves the original pool"));
  bool appReady = ZutcpTest::Instance::apply(app, initializedParams).is<void>();
  ZuCheckRT(appReady && app.dataTxAllocator() == stack->dataTxAllocator(),
    log_("protocol owners use their Instance's native producer adapter"));
  auto tx = mx.allocator<Zi::IOBufType::TxTCP, Zi::IOBuf, 128>(3);
  auto small = mx.allocator<Zi::IOBufType::TxTCP, Zi::IOBuf, 64>(1);
  auto rx = mx.allocator<Zi::IOBufType::Rx>(77);
  auto rxHeap = mx.allocator<Zi::IOBufType::App>();
  auto otherRx = mx.allocator<Zi::IOBufType::Rx, Zi::IOBuf, 512>(55);
  auto stub = rx->allocBuf(&mx), otherStub = otherRx->allocBuf(&mx);
  ZuCheckRT(!stack->ready() && !tx->allocBuf(&mx, Payload) && stub && otherStub &&
      !stub->data_() && !otherStub->data_() && !stub->size && !otherStub->size &&
      tx->capacity() == 128 && small->capacity() == 64,
    log_("stopped factory publishes stable Tx geometry and backingless Rx stubs"));
  stub = otherStub = nullptr;
  bool started = mx.start();
  ZuCheckRT(started && stack->ready(), log_("registered packet Instance starts on mx owners"));
  if (!started) { alarm(0); return; }
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

  const ZiIP tfoClients[] = {ZiIP{"198.18.0.1"}, ZiIP{"2001:db8::1"}};
  const ZiSockAddr tfoServers[] = {
    ZiSockAddr{ZiIP{"198.18.0.2"},443}, ZiSockAddr{ZiIP{"2001:db8::2"},443}
  };
  Zutcp::TFOCookie tfoIssued[2];
  bool tfoStored = true;
  mx.rxRun([&stack, &tfoClients, &tfoServers, &tfoIssued, &tfoStored, &owner] {
    for (unsigned i = 0; i < 2; ++i) {
      tfoStored &= stack->issueTFO(tfoServers[i],tfoClients[i],tfoIssued[i]) &&
	stack->validTFO(tfoServers[i],tfoClients[i],tfoIssued[i]);
      auto submitted = tfoIssued[i];
      stack->saveTFOCookie(tfoClients[i],tfoServers[i],submitted);
      submitted.bytes[0] ^= 1;
      Zutcp::TFOCookie snapshot, retained;
      tfoStored &= stack->tfoCookie(tfoClients[i],tfoServers[i],snapshot) &&
	snapshot.length == tfoIssued[i].length &&
	!memcmp(snapshot.bytes,tfoIssued[i].bytes,sizeof(snapshot.bytes));
      snapshot.bytes[0] ^= 1;
      tfoStored &= stack->tfoCookie(tfoClients[i],tfoServers[i],retained) &&
	retained.length == tfoIssued[i].length &&
	!memcmp(retained.bytes,tfoIssued[i].bytes,sizeof(retained.bytes));
    }
    owner.post();
  });
  owner.wait();
  ZuCheckRT(tfoStored,
    log_("actual Instance Rx owns TFO keys/cache and copies both input cookies "
      "and output snapshots"));


  int wire = socket(AF_PACKET, SOCK_RAW | SOCK_NONBLOCK | SOCK_CLOEXEC, htons(ETH_P_ALL));
  sockaddr_ll address{};
  address.sll_family = AF_PACKET;
  address.sll_protocol = htons(ETH_P_ALL);
  address.sll_ifindex = peer;
  bool bound = wire >= 0 && !bind(wire, reinterpret_cast<sockaddr *>(&address), sizeof(address));
  ZuCheckRT(bound, log_("independent packet peer bound"));
  if (!bound) { if (wire >= 0) close(wire); ZmAssert_(mx.stop()); alarm(0); return; }

  ControlProbe pressure{&mx, stack};
  ZmSemaphore pressureSenderEntered, pressureSenderRelease;
  mx.run([&pressureSenderEntered, &pressureSenderRelease] {
    pressureSenderEntered.post();
    pressureSenderRelease.wait();
  }, 3);
  pressureSenderEntered.wait();
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
  pressureSenderRelease.post();
  ZuCheckRT(pressure.submitted && pressure.during.capacity == 8 &&
      pressure.during.reserved == 3 && pressure.during.outstanding == 7 &&
      pressure.during.dataStalls == 1 &&
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

  MetaTotals recoverySamples[5];
  for (unsigned scenario = 0; scenario < 5; ++scenario)
    checkIdleRefusal(mx, stack, tx, small, wire, scenario,
      scenario == 4 ? recoverySamples : nullptr);
  mx.txRun([&recoverySamples, &owner] { recoverySamples[4] = metaTotals(); owner.post(); });
  owner.wait();
  reportMeta(8, "recovery-admission", recoverySamples[0], recoverySamples[1]);
  reportMeta(8, "loss-and-refusals", recoverySamples[1], recoverySamples[2]);
  reportMeta(8, "completion-retry", recoverySamples[2], recoverySamples[3]);
  reportMeta(8, "recovery-teardown", recoverySamples[3], recoverySamples[4]);
  checkFinalACK(mx, stack, wire, false);
  checkFinalACK(mx, stack, wire, true);
  checkSourceRetirement(mx, stack, wire);

  ZmRef<Zi::IOBuf> reserves[2]; // remaining two slots in the three-packet Tx pool
  uint8_t *sentOrigin = nullptr;
  bool accepted = false, isolated = false;
  mx.txRun([&mx, &tx, &stack, &reserves, &sentOrigin, &accepted, &isolated, &owner] {
    auto packet = tx->allocBuf(&mx, Payload);
    reserves[0] = tx->allocBuf(&mx, Payload);
    reserves[1] = tx->allocBuf(&mx, Payload);
    // Acquire the control provider through its native raw pool, independently.
    auto ctl = stack->control()->alloc();
    isolated = packet && reserves[0] && reserves[1] && !tx->allocBuf(&mx, 1) && ctl;
    if (ctl) stack->control()->free(ctl);
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
    log_("positive credit sends both original packets "
      "and charges every byte without allocation"));
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
  checkHeldHeaders(mx, stack, cxn, wire);
  RetxProbe retransmit{&mx, cxn, stack};
  retransmit.deadline = Zm::now() + ZuTime{2};
  mx.txRun([&cxn, &retransmit] { cxn->ackFirst(); retransmit.run(); });
  retransmit.done.wait();
  ZuCheckRT(retransmit.sent && retransmit.refused && cxn->preserved &&
      cxn->reused && !cxn->extraAlloc &&
      cxn->submitted == 3 && cxn->detached,
    log_("retransmission reuses the retained raw payload address without packet allocation; ",
      "sent ", retransmit.sent, " reused ", cxn->reused, " extra allocations ", cxn->extraAlloc,
      " submitted ", cxn->submitted, " detached ", cxn->detached, " refused ", cxn->refused));
  bool replaySeen = false;
  for (unsigned tries = 0; tries < 16 && !replaySeen; ++tries) {
    pollfd ready{wire, POLLIN, 0};
    if (poll(&ready, 1, 1000) <= 0) break;
    auto n = recv(wire, observed, sizeof(observed), 0);
    replaySeen = n >= 8 && !memcmp(observed + n - 8, "second--", 8);
  }
  ZuCheckRT(replaySeen, log_("independent peer verifies retransmitted payload bytes"));
  bool finControl = false;
  mx.txRun([&cxn, &stack, &owner, &finControl] {
    finControl = cxn->finControl(stack) && cxn->controls == 2 && !cxn->extraAlloc &&
      cxn->bounded && cxn->detached;
    owner.post();
  });
  owner.wait();
  ZuCheckRT(finControl,
    log_("FIN-only TCP construction uses control backing without DATA allocation"));
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
  uint8_t shutdownFrame[64]{}, shutdownWire[2048];
  while (recv(wire, shutdownWire, sizeof(shutdownWire), 0) >= 0) { }
  ZmSemaphore senderEntered, senderRelease;
  mx.run([&senderEntered, &senderRelease] {
    senderEntered.post();
    senderRelease.wait(); // Deliberately hold kernel service behind admission.
  }, 3);
  senderEntered.wait();
  bool shutdownSubmitted = false;
  mx.txRun([&stack, &shutdownFrame, &shutdownSubmitted, &owner] {
    auto packet = stack->allocPacket(&stack, ZiIPType::V4, true);
    if (packet) {
      ControlProbe::frame(packet, 0xfe);
      for (unsigned i = 15; i < sizeof(shutdownFrame); ++i)
	packet->data()[i] = uint8_t(i ^ 0xa7);
      memcpy(shutdownFrame, packet->data(), sizeof(shutdownFrame));
      shutdownSubmitted = stack->sendPacket(packet) && !packet->data_();
    }
    owner.post();
  });
  owner.wait();
  ZuCheckRT(shutdownSubmitted,
    log_("final frame is accepted while the sender is gated"));
  ZmThread releaseSender{[&mx, &stack, &senderRelease] {
    while (stack->ready()) Zm::yield();
    // Stopping is published on Rx. Its FIFO successor posts behind stopTx,
    // so the sender remains gated until shutdown has observed the admission.
    mx.rxRun([&mx, &senderRelease] {
      mx.txRun([&senderRelease] { senderRelease.post(); });
    });
  }};
  bool txStopped = mx.stop();
  releaseSender.join();
  ZuCheckRT(txStopped && !stack->ready(),
    log_("asynchronous Rx then Tx drain retires the XDP service"));
  pollfd shutdownReady{wire, POLLIN, 0};
  bool shutdownDelivered = false;
  auto shutdownDeadline = Zm::now() + ZuTime{2};
  while (!shutdownDelivered) {
    auto remaining = (shutdownDeadline - Zm::now()).millisecs();
    if (remaining <= 0) break;
    if (poll(&shutdownReady, 1, int(remaining)) <= 0) continue;
    ssize_t n;
    while ((n = recv(wire, shutdownWire, sizeof(shutdownWire), 0)) >= 0)
      if (n == sizeof(shutdownFrame) &&
	  !memcmp(shutdownWire, shutdownFrame, sizeof(shutdownFrame)))
	shutdownDelivered = true;
  }
  ZuCheckRT(shutdownSubmitted && shutdownDelivered,
    log_("shutdown sends an accepted frame before stopping the gated kernel-service owner"));
  pressure.retained.clear();
  ZuCheckRT(extra && extra->data()[14] == 0xa5 && probe.packet,
    log_("stopped Instance preserves application-held Rx and Tx backing"));
  ZuCheckRT(leases.shutdownLease && **leases.shutdownLease &&
      leases.shutdownLease->data() == leases.origin && leases.origin[14] == 0x5a,
    log_("XDP stop preserves recovery's caller lease after Zi metadata destruction"));
  // Owners are stopped. Clearing recovery returns its lease through the still
  // live provider; the subsequent restart also verifies pool drain.
  leases.recovery.clear();
  const auto &retained = leases.heldRange.range;
  ZuCheckRT(retained.storage && !retained.storage->pending && retained.length == 1 &&
      retained.storage->data[retained.offset] == 0x5a && **leases.shutdownLease,
    log_("retained raw range outlives Zi metadata, queue and sent-record retirement"));
  leases.heldRange = {};
  ZuCheckRT(input[1] && input[1]->data()[14] == 1,
    log_("stopping preserves application-held ingress backing"));
  extra = nullptr;
  for (auto &p : input) p = nullptr;
  for (auto &p : reserves) p = nullptr;
  probe.packet = nullptr;
  auto freshStack = ZutcpTest::makeInstance(&mx, index, 3, {.packetCount = 2}, 0);
  auto conflicting = initializedParams;
  conflicting.controlBufCount = 3;
  conflicting.closedTupleRingSize = 2;
  bool configured = ZutcpTest::Instance::apply(app, initializedParams).is<void>();
  ZuCheckRT(configured && ZutcpTest::Instance::apply(app, conflicting).is<ZeException>() &&
      app.controlBufCount() == 2 && app.txControlCredits() == 3 &&
      stack->txAdmission().controlCount == 2 && stack->txAdmission().capacity == 8,
    log_("conflicting stopped configuration preserves allocator geometry and counts"));
  ZuCheckRT(app.rxRetainFramesPerCxn() == 1,
    log_("nondefault per-connection retention limit survives restart"));

  bool restarted = mx.start();
  ZuCheckRT(restarted, log_("restart activates the retained packet service"));
  if (!restarted) { close(wire); alarm(0); return; }
  bool running = stack->ready();
  bool historyKept = false;
  mx.rxRun([&stack, &history, &historyKept, &owner] {
    Zutcp::ClosedTuple found;
    auto stats = stack->closedTupleStats();
    historyKept = stats.capacity == 3 && stats.count == 3 &&
      stack->closedTuple(history[0].key, found) && found.id == history[0].id &&
      stack->closedTuple(history[1].key, found) &&
      found.id == history[1].id && found.rxNext == history[1].rxNext &&
      stack->closedTuple(history[2].key, found) && found.id == history[2].id;
    owner.post();
  });
  owner.wait();
  ZuCheckRT(historyKept, log_("restart preserves established tuple history"));

  bool tfoKept = true, tfoNewInstance = true;
  mx.rxRun([&stack, &freshStack, &tfoClients, &tfoServers, &tfoIssued,
    &tfoKept, &tfoNewInstance, &owner] {
    for (unsigned i = 0; i < 2; ++i) {
      Zutcp::TFOCookie repeated, cached, replacement;
      tfoKept &= stack->issueTFO(tfoServers[i],tfoClients[i],repeated) &&
	repeated.length == tfoIssued[i].length &&
	!memcmp(repeated.bytes,tfoIssued[i].bytes,sizeof(repeated.bytes)) &&
	stack->validTFO(tfoServers[i],tfoClients[i],tfoIssued[i]) &&
	stack->tfoCookie(tfoClients[i],tfoServers[i],cached) &&
	cached.length == tfoIssued[i].length &&
	!memcmp(cached.bytes,tfoIssued[i].bytes,sizeof(cached.bytes));
      tfoNewInstance &= !freshStack->tfoCookie(tfoClients[i],tfoServers[i],cached) &&
	!freshStack->validTFO(tfoServers[i],tfoClients[i],tfoIssued[i]) &&
	freshStack->issueTFO(tfoServers[i],tfoClients[i],replacement) &&
	freshStack->validTFO(tfoServers[i],tfoClients[i],replacement) &&
	!stack->validTFO(tfoServers[i],tfoClients[i],replacement);
    }
    owner.post();
  });
  owner.wait();
  ZuCheckRT(tfoKept && configured && app.tfoCookieCacheSize() == 3,
    log_("same-size stopped configuration and actual restart retain TFO key "
      "and cookie evidence"));
  ZuCheckRT(tfoNewInstance && mx.plugin() == stack && !freshStack->ready(),
    log_("a new unstarted Instance has an independent key/cache "
      "alongside the permanently attached plugin"));

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
    mx.rxRun([&mx, &app, &pressureRx, &pressurePassed, &owner] {
      ZmRef<WriterCxn> a = new WriterCxn{&app};
      ZmRef<WriterCxn> b = new WriterCxn{&app};
      ZmRef<WriterCxn> c = new WriterCxn{&app};
      a->rxCredit(16); b->rxCredit(16); c->rxCredit(16);
      auto frame = [](ZiIOBuf *buf, uint64_t offset, unsigned length = 1) {
	Zutcp::ByteData value;
	value.offset = offset;
	value.length = length;
	value.payload = {buf->data() + 14, length};
	return value;
      };
      auto receive = [](WriterCxn *cxn, const Zutcp::ByteData &data,
	  ZmRef<ZiIOBuf> packet) {
	return cxn->processFrame(data, ZuMv(packet));
      };
      bool first = receive(a.ptr(), frame(pressureRx[0], 0), pressureRx[0]) >= 0;
      bool second = receive(b.ptr(), frame(pressureRx[1], 0), pressureRx[1]) >= 0;
      bool third = receive(c.ptr(), frame(pressureRx[2], 0), pressureRx[2]) >= 0;
      pressurePassed = first && second && third &&
	a->rxError() == Zutcp::DataError::None && c->rxError() == Zutcp::DataError::None;
      mx.rxRun([a = ZuMv(a), b = ZuMv(b), c = ZuMv(c),
	&mx, &pressurePassed, &owner] {
	// Normal queued Rx dequeue work precedes this observation.
	pressurePassed &= a->rxBytes() == 1 && !a->rxPending() &&
	  b->rxBytes() == 1 && c->rxBytes() == 1;
	a->clearRx(); b->clearRx(); c->clearRx();
	mx.txRun([&mx, &owner] { mx.rxRun([&owner] { owner.post(); }); });
      });
    });
    owner.wait();
    ZuCheckRT(pressurePassed,
      log_("connections independently retain and deliver their native DATA"));
    for (auto &p : pressureRx) p = nullptr;

  }
  bool stopped = restarted && mx.stop();
  ZuCheckRT(restarted && running && stopped,
    log_("released pools restart with stable providers: start ", restarted,
	" ready ", running, " stop ", stopped));
  freshStack = nullptr;
  close(wire);
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
