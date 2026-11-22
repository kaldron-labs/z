//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmBlock.hh>
#include <zlib/Zquic.hh>

#include "ZquicFiniteProvider.hh"

#include <zlib/ZtlsEngineOpenSSL.hh>

using namespace ZuTestUtil;

struct PressureApp : Zquic::Hub<PressureApp> { };
struct FailedAllocator : Zi::IOBufBuiltinAllocator<Zi::IOBufType::TxUDP, Zquic_::TxPkt> {
  using Base = Zi::IOBufBuiltinAllocator<Zi::IOBufType::TxUDP, Zquic_::TxPkt>;
  using Base::Base;
  ZmRef<ZiIOBuf> allocBuf(void *, uint32_t, uint32_t) override { return nullptr; }
};
struct PressureLink;
struct PressureStream :
  Zquic::Stream<PressureLink, PressureStream> {
  using Base = Zquic::Stream<PressureLink, PressureStream>;
  using Base::Base;
  int process(Zquic::RxStream &) { return 0; }
};
struct PressureLink : ZmNHeap<PressureLink>,
  Zquic::Link<PressureApp, PressureLink, PressureStream> {
friend inline ZuStringT<"Zquic.Pressure.Link"> ZmNHeapID(PressureLink *);
  using ZmNHeap<PressureLink>::operator new;
  using ZmNHeap<PressureLink>::operator delete;
  using Base =
    Zquic::Link<PressureApp, PressureLink, PressureStream>;
  using Base::disconnect_;
  using Base::txPackets;
  using Base::allocPktTx;
  using Base::cancelPktWait;
  using Base::resetTx;

  Zquic_::PktAllocTarget target;
  ZmRef<ZiIOBuf> output;
  unsigned resumes = 0, failures = 0, closes = 0;
  bool pending = true;
  bool wire = false, armed = false;
  Zquic_::EndpointTxGate *gate = nullptr;
  unsigned queued = 0, ptos = 0;
  Base::TxPktRefs accepted;

  PressureLink(PressureApp *app, bool wire = false) : Base{app}, wire{wire} {
    if (wire) Base::configLocalParams(app);
  }
  void ptoTimer() { ++ptos; }

  bool setupWire() {
    uint8_t bytes[32]{};
    Zquic::TrafficSecret secret;
    Base::initClientPathTx({}, {});
    Base::setPeerStreamLimit(Zquic::StreamType::Duplex, 2);
    Base::txApplyMaxData(100);
    return Zquic::PktProt::deriveSecret(secret,
      &Ztls_::opensslAes128gcmsha256, ZuBSpan{bytes, sizeof(bytes)}) &&
      Base::txInstallSecret(Zquic::PktNumSpace::AppData, secret);
  }
  void waitPktTx(Zquic_::EndpointTxWait &wait, uint64_t epoch, Zquic_::EndpointTxFn fn) {
    wait.start(app()->mx(), app()->txThread(), epoch, ZuMv(fn));
    app()->mx()->txRun([this, wait = &wait] { gate->waitTx(*wait); });
  }
  bool sendWire(Zquic::PktBuild &build, const Base::TxPktRefs &refs) {
    bool ok = Base::sendProtShortPkt(
      Zquic::CxnID{}, 1, build, {}, {}, &refs, true,
      [this] { return Base::allocPktTx(); },
      [this](auto buf, ZiSockAddr addr, Zquic::EcnMark::T ecn) {
	return Base::submitPktTx(ZuMv(buf), ZuMv(addr), ecn, false,
	  [this](auto buf, ZiSockAddr, Zquic::EcnMark::T, bool,
	      Zquic_::EndpointTxToken token, bool *blocked) {
	    if (gate) {
	      if (token) { if (!gate->consume(token)) return false; }
	      else if (!gate->available(queued)) { *blocked = true; return false; }
	      ++queued;
	    }
	    output = ZuMv(buf);
	    return true;
	  });
      });
    if (ok) accepted = refs;
    return ok;
  }
  bool retransmit_(bool = false, unsigned max = 16,
      Zquic::PktNumSpace::T only = Zquic::PktNumSpace::N) {
    bool sent = false;
    Zquic::SentFrameRef ref;
    Zquic::PktNumSpace::T space;
    for (unsigned n = 0; n < max && Base::nextRetransmit_(space, ref, only); ++n) {
      Zquic::PktBuild build;
      Base::TxPktRefs refs;
      bool built = Base::buildRetxStream(build, ref);
      refs.add(ref);
      if (!built || !sendWire(build, refs)) {
	Base::requeueStreamRef(space, ref);
	break;
      }
      sent = true;
    }
    return sent;
  }
  void queueTxFlush(ZiSockAddr = {}) { }
  void queueRetransmit() {
    if (wire && !Base::pktAllocationPending())
      app()->txRun([this] { flushTx_(); });
  }
  void flushTx_(ZiSockAddr = {}) {
    if (!wire || !armed || Base::linkClosedTx_()) return;
    retransmit_();
    Base::flushCtlStreamsBatch({},
      [](Zquic::PktBuild &) { return true; },
      [this](Zquic::PktBuild &build, ZiSockAddr, const Base::TxPktRefs &refs) {
	return sendWire(build, refs);
      });
  }

  Zquic_::PktAllocTarget pktAllocTarget() const { return target; }
  void pktPressureResume() {
    ++resumes;
    if (wire) flushTx_();
    else if (pending) output = allocPktTx();
  }
  void pktAllocFailed() {
    ++failures;
    if (wire) Base::pktAllocFailed();
  }
  void closeExpired() { ++closes; }

};

template <typename L> void on(ZiMultiplex &mx, unsigned sid, L fn)
{
  ZmBlock<>{}([&mx, sid, fn = ZuMv(fn)](auto wake) mutable {
    mx.run([fn = ZuMv(fn), wake = ZuMv(wake)]() mutable {
      fn();
      wake();
    }, sid);
  });
}

static void drain(ZiMultiplex &mx)
{
  // Drain endpoint capacity callbacks and their bounded caller resume turns.
  for (unsigned i = 0; i < 2; ++i) {
    on(mx, 2, [] {});
    on(mx, 4, [] {});
  }
}

void testBufferReturn()
{
  ZuTestScope(testBufferReturn);
  ZiMultiplex mx{ZiMxParams{}.scheduler([](auto &s) { s.nThreads(4); })
    .rxThread(1).txThread(2)};
  PressureApp app;
  ZuCheck(app.init(Zquic::HubParams{&mx, "3", "4"}));
  ZmRef<ZquicFiniteProvider> provider = new ZquicFiniteProvider{&mx};
  Zi::IOBufPoolAllocator<Zi::IOBufType::TxUDP, Zquic_::TxPkt> allocator{&mx, provider};
  ZmRef<PressureLink> link = new PressureLink{&app};
  link->target = {&allocator, link.ptr(), provider.ptr(), 1, 1024};
  auto held = allocator.allocBuf();
  ZuCheck(bool(held));
  ZuCheck(mx.start());
  bool refused = false;
  on(mx, 4, [&link, &refused] { refused = !link->allocPktTx(); });
  drain(mx);
  ZuCheck(refused && link->failures == 1 && !link->resumes);
  held = nullptr;
  drain(mx);
  ZuCheck(link->failures == 1 && !link->resumes && !link->output);
  on(mx, 4, [&link] { link->output = link->allocPktTx(); });
  ZuCheck(link->output && link->output->registered() && link->output->owner == link.ptr() &&
    link->output->skip == ZquicFiniteProvider::HeadRoom &&
    link->output->size == 1024 + ZquicFiniteProvider::HeadRoom + ZquicFiniteProvider::TailRoom);
  on(mx, 4, [&link] { link->output = nullptr; });
  link = nullptr;
  app.final();
  ZuCheck(mx.stop());
}

template <bool capacity, bool bufferAfterGrant = false> static void finalStream()
{
  ZuTestScope(finalStream);
  ZiMultiplex mx{ZiMxParams{}.scheduler([](auto &s) { s.nThreads(4); })
    .rxThread(1).txThread(2)};
  PressureApp app;
  // Capacity refusal is synchronous only when QUIC and endpoint Tx coincide.
  unsigned sid = capacity ? 2 : 4;
  ZuCheck(app.init(Zquic::HubParams{&mx, "3", capacity ? "2" : "4"}));
  ZmRef<ZquicFiniteProvider> provider = new ZquicFiniteProvider{&mx};
  Zi::IOBufPoolAllocator<Zi::IOBufType::TxUDP, Zquic_::TxPkt> allocator{&mx, provider};
  ZmRef<PressureLink> link = new PressureLink{&app, true};
  link->target = {&allocator, link.ptr(), provider.ptr(), 1, 1024};
  Zquic_::EndpointTxGate gate{&mx, provider.ptr(), 1, 1,
    [link = link.ptr()] { return link->queued; }};
  if (capacity) { link->gate = &gate; link->queued = 1; }
  auto held = capacity ? ZmRef<ZiIOBuf>{} : allocator.allocBuf();
  ZmRef<PressureStream> stream;
  ZuCheck(mx.start());
  bool setup = false;
  on(mx, sid, [&link, &stream, &setup] {
    setup = link->setupWire();
    stream = link->stream();
    if (!stream) { setup = false; return; }
    stream->txCredit(100);
    auto tx = stream->txStream_();
    tx << "final" << Zi::flush();
    stream->fin();
    link->streamWritable(stream);
    link->armed = true;
    link->flushTx_();
  });
  drain(mx);
  ZuCheck(setup && !link->output && !link->txPackets() && !link->resumes);
  ZuCheck(stream && stream->txUnackdBytes() == 6);
  uint64_t credit = stream ? stream->txCreditAvailable() : 0;
  unsigned waiting = 0;
  on(mx, 2, [&gate, &waiting] { waiting = gate.waiting(); });
  ZuCheck(waiting == (capacity ? 1U : 0U));
  // No app write, ACK, timer, or incoming packet follows the stalled FIN.
  if constexpr (bufferAfterGrant) held = allocator.allocBuf();
  if (capacity) on(mx, 2, [&link, &gate] { link->queued = 0; gate.service(); });
  else held = nullptr;
  drain(mx);
  if constexpr (!capacity || bufferAfterGrant) {
    bool released = false;
    on(mx, 2, [&gate, &released] {
      released = !gate.reservations() && !gate.waiting();
    });
    on(mx, 3, [] {});
    ZuCheck(released && link->closed() && link->failures == 1 &&
      link->closes == 1 && !link->output && !link->txPackets());
    held = nullptr;
    drain(mx);
    ZuCheck(link->resumes == (capacity ? 1U : 0U) && !link->output);
  } else {
    enum { expectedResumes = 1 };
    bool delivered = false;
    on(mx, sid, [&link, &stream, &delivered, credit] {
      const auto &refs = link->accepted;
      delivered = link->resumes == expectedResumes && link->txPackets() == 1 && link->output &&
        link->output->registered() && link->output->length && refs.count() == 1;
      if (!delivered) log("wire resumes=", link->resumes, " packets=", link->txPackets(),
        " output=", bool(link->output), " refs=", refs.count());
      if (delivered) {
        const auto &ref = refs[0];
        delivered &= ref.kind == Zquic::SentFrameKind::Stream && ref.offset == 0 &&
	  ref.length == 5 && ref.fin && ref.range().buf &&
	  !memcmp(ref.range().buf->data_() + ref.range().offset, "final", 5) &&
	  stream->txCreditAvailable() == credit && stream->txUnackdBytes() == 6;
        if (!delivered) log("ref offset=", ref.offset, " length=", ref.length,
	  " fin=", int(ref.fin), " credit=", stream->txCreditAvailable(), "/", credit,
	  " unackd=", stream->txUnackdBytes(), " range=", bool(ref.range().buf));
      }
    });
    ZuCheck(delivered);
    drain(mx);
    ZuCheck(link->resumes == expectedResumes && link->txPackets() == 1 &&
      !link->failures && !link->ptos);
  }
  ZmBlock<>{}([&link](auto wake) { link->disconnect_(false, ZuMv(wake)); });
  drain(mx);
  on(mx, sid, [&link] { link->output = nullptr; link->accepted = {}; });
  stream = nullptr;
  link = nullptr;
  on(mx, 2, [&gate] { gate.close(); });
  drain(mx);
  app.final();
  ZuCheck(mx.stop());
}

static void testFinalStreamBuffer()
{
  ZuTestScope(testFinalStreamBuffer);
  ZuTestCall(finalStream<false>);
}
static void testFinalStreamCapacity()
{
  ZuTestScope(testFinalStreamCapacity);
  ZuTestCall(finalStream<true>);
}

static void testBufferAfterCapacity()
{
  ZuTestScope(testBufferAfterCapacity);
  ZuTestCall((finalStream<true, true>));
}

static void testProviderFailureCloses()
{
  ZuTestScope(testProviderFailureCloses);
  ZiMultiplex mx{ZiMxParams{}.scheduler([](auto &s) { s.nThreads(4); })
    .rxThread(1).txThread(2)};
  PressureApp app;
  ZuCheck(app.init(Zquic::HubParams{&mx, "3", "4"}));
  FailedAllocator unsupported{&mx};
  ZmRef<PressureLink> link = new PressureLink{&app, true};
  link->target = {&unsupported, link.ptr(), &unsupported, 1, 1024};
  ZuCheck(mx.start());
  on(mx, 4, [&link] { (void)link->allocPktTx(); });
  drain(mx);
  on(mx, 3, [] {}); // drain the public close-state publication
  ZuCheck(link->closed() && link->closeError() == Zquic::TransportError::InternalError &&
    link->closes == 1 && link->failures == 1 && !link->resumes && !link->output);
  ZmBlock<>{}([&link](auto wake) { link->disconnect_(false, ZuMv(wake)); });
  drain(mx);
  link = nullptr;
  app.final();
  ZuCheck(mx.stop());
}

void testCapacityGate()
{
  ZuTestScope(testCapacityGate);
  ZiMultiplex mx{ZiMxParams{}.scheduler([](auto &s) { s.nThreads(4); })
    .rxThread(1).txThread(2)};
  PressureApp app;
  ZuCheck(app.init(Zquic::HubParams{&mx, "3", "4"}));
  unsigned queued = 1, grants[2] = {}, retired = 0, cancelled = 0;
  Zquic_::EndpointTxToken tokens[2];
  Zquic_::EndpointTxWait first, second;
  Zquic_::EndpointTxGate gate{&mx, &queued, 7, 1, [&queued] { return queued; }};
  gate.initQueues(&app, "gate");
  ZuCheck(mx.start());
  auto arm = [&mx, &gate, &tokens, &grants, &retired, &cancelled](
    Zquic_::EndpointTxWait &wait, unsigned index) {
    on(mx, 4, [&mx, &gate, &wait, index, &tokens, &grants, &retired, &cancelled] {
      wait.start(&mx, 4, 19, [index, &tokens, &grants, &retired, &cancelled](
	int result, Zquic_::EndpointTxToken token) {
	if (result == Zquic_::EndpointTxResult::Ready) {
	  tokens[index] = token;
	  ++grants[index];
	} else if (result == Zquic_::EndpointTxResult::Retired) ++retired;
	else ++cancelled;
      });
      mx.txRun([&gate, &wait] { gate.waitTx(wait); });
    });
  };
  arm(first, 0);
  arm(second, 1);
  drain(mx);
  bool blocked = false;
  on(mx, 2, [&gate, &blocked] {
    blocked = !gate.available() && gate.waiting() == 2 && !gate.reservations();
  });
  ZuCheck(blocked && !grants[0] && !grants[1]);
  on(mx, 2, [&gate, &queued] { queued = 0; gate.service(); });
  drain(mx);
  on(mx, 2, [&gate, &blocked] {
    blocked = !gate.available() && gate.waiting() == 1 && gate.reservations() == 1;
  });
  ZuCheck(blocked && grants[0] == 1 && !grants[1]);
  auto old = tokens[0];
  bool consumed = false, duplicate = true;
  on(mx, 2, [&gate, &queued, &consumed, &duplicate, old] {
    consumed = gate.consume(old);
    if (consumed) ++queued; // endpoint admission precedes capacity service
    duplicate = gate.consume(old);
    gate.service();
  });
  drain(mx);
  ZuCheck(consumed && !duplicate && retired == 1 && !grants[1] && !first.busy());
  on(mx, 2, [&gate, &queued] { queued = 0; gate.service(); });
  drain(mx);
  ZuCheck(grants[1] == 1 && second.busy());
  bool cancelDone = false;
  on(mx, 4, [&second, &cancelDone] { second.cancel([&cancelDone] { cancelDone = true; }); });
  drain(mx);
  ZuCheck(cancelDone && cancelled == 1 && !second.busy());

  // Reusing a request with the same caller epoch still invalidates its old token.
  arm(first, 0);
  drain(mx);
  bool stale = true, wrongPhysical = true, wrongSlot = true, wrongSerial = true;
  on(mx, 2, [&gate, old, token = tokens[0], &stale, &wrongPhysical,
      &wrongSlot, &wrongSerial]() mutable {
    stale = gate.consume(old);
    auto invalid = token;
    invalid.slot = unsigned(-1);
    wrongSlot = gate.consume(invalid);
    invalid = token;
    ++invalid.reservation;
    wrongSerial = gate.consume(invalid);
    token.physical = nullptr;
    wrongPhysical = gate.consume(token);
  });
  ZuCheck(grants[0] == 2 && !stale && !wrongPhysical && !wrongSlot && !wrongSerial);
  arm(second, 1);
  drain(mx);
  bool rowsValid = true;
  unsigned rows = gate.allQueues([&rowsValid](Ztc::Queue *queue) {
    Ztc::QueueTelemetry row;
    queue->telemetry(row);
    rowsValid &= row.count == 1;
    if (row.id == "gate:reserved") rowsValid &= row.size == 1 && row.full == 1;
    else rowsValid &= row.id == "gate:wait" && !row.size && !row.full;
  });
  rowsValid &= rows == 2;
  ZuCheck(rowsValid);
  ZmSemaphore entered, release;
  mx.run([&entered, &release] { entered.post(); release.wait(); }, 4);
  entered.wait();
  bool closed = false, drained = false;
  on(mx, 2, [&gate, &closed, &drained, &cancelled] {
    gate.close([&closed, &drained, &cancelled] {
      closed = true;
      drained = cancelled == 3;
    });
  });
  ZuCheck(!closed);
  gate.allQueues([&rowsValid](Ztc::Queue *queue) {
    Ztc::QueueTelemetry row;
    queue->telemetry(row);
    rowsValid &= !row.count;
  });
  ZuCheck(rowsValid);
  release.post();
  drain(mx);
  ZuCheck(closed && drained && !first.busy() && !second.busy());
  app.final();
  ZuCheck(mx.stop());
}

void testCapacityBatch()
{
  ZuTestScope(testCapacityBatch);
  ZiMultiplex mx{ZiMxParams{}.scheduler([](auto &s) { s.nThreads(4); })
    .rxThread(1).txThread(2)};
  unsigned queued = 128, grants = 0, cancelled = 0, firstBatch = 0;
  Zquic_::EndpointTxWait waits[75];
  Zquic_::EndpointTxGate gate{&mx, &queued, 1, 128, [&queued] { return queued; }};
  ZuCheck(mx.start());
  on(mx, 4, [&mx, &gate, &waits, &grants, &cancelled] {
    for (auto &wait : waits) {
      wait.start(&mx, 4, 1, [&grants, &cancelled](int result, Zquic_::EndpointTxToken) {
	if (result == Zquic_::EndpointTxResult::Ready) ++grants;
	if (result == Zquic_::EndpointTxResult::Cancelled) ++cancelled;
      });
      mx.txRun([&gate, wait = &wait] { gate.waitTx(*wait); });
    }
  });
  on(mx, 2, [&gate, &queued, &firstBatch] {
    queued = 0;
    gate.service();
    firstBatch = gate.reservations();
  });
  drain(mx);
  ZuCheck(firstBatch == 64 && grants == 75);
  bool closed = false;
  on(mx, 2, [&gate, &closed] { gate.close([&closed] { closed = true; }); });
  drain(mx);
  ZuCheck(closed && cancelled == 75);
  ZuCheck(mx.stop());
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testBufferReturn);
  ZuTestCall(testFinalStreamBuffer);
  ZuTestCall(testFinalStreamCapacity);
  ZuTestCall(testBufferAfterCapacity);
  ZuTestCall(testProviderFailureCloses);
  ZuTestCall(testCapacityGate);
  ZuTestCall(testCapacityBatch);
}
