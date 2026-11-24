//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmBlock.hh>
#include <zlib/Zquic.hh>

#include "ZquicFiniteProvider.hh"

using namespace ZuTestUtil;

struct QueueApp : Zquic::Hub<QueueApp> { };

struct QueueLink;
struct QueueStream : Zquic::Stream<QueueLink, QueueStream> {
  using Base = Zquic::Stream<QueueLink, QueueStream>;
  using Base::Base;
  int process(Zquic::RxStream &) { return 0; }
};
struct QueueLink : ZmNHeap<QueueLink>,
    Zquic::Link<QueueApp, QueueLink, QueueStream> {
friend inline ZuStringT<"Zquic.QueueTest.Link"> ZmNHeapID(QueueLink *);
  using ZmNHeap<QueueLink>::operator new;
  using ZmNHeap<QueueLink>::operator delete;
  using Base = Zquic::Link<QueueApp, QueueLink, QueueStream>;
  using Base::Base;
  using Base::resetLink;
  using Base::disconnect_;
  using Base::reapStreamByIDTx;
  using Base::clearWritableStreams;
  unsigned flushes = 0;
  void flushTx_(ZiSockAddr = {}) { ++flushes; }
};

template <typename L> static void on(ZiMultiplex &mx, unsigned sid, L fn)
{
  ZmBlock<>{}([&mx, sid, fn = ZuMv(fn)](auto wake) mutable {
    mx.run([fn = ZuMv(fn), wake = ZuMv(wake)]() mutable { fn(); wake(); }, sid);
  });
}

static void testStreamQueues()
{
  ZuTestScope(testStreamQueues);
  ZiMultiplex mx{ZiMxParams{}.scheduler([](auto &s) { s.nThreads(4); })
    .rxThread(1).txThread(2)};
  QueueApp app;
  ZuCheck(app.init(Zquic::HubParams{&mx, "3", "4"}));
  ZuCheck(mx.start());
  ZmRef<QueueLink> link = new QueueLink{&app};
  QueueLink::StreamRef stream;
  on(mx, 3, [&link, &stream] {
    link->setPeerStreamLimit(Zquic::StreamType::Duplex, 1);
    stream = link->stream();
  });
  on(mx, 4, [] {}); // complete the other owner's stream publication
  ZuCheck(stream);

  bool received = true;
  on(mx, 3, [&app, &stream, &received] {
    stream->rxCredit(16);
    for (unsigned offset : {0U, 2U}) {
      auto packet = app.rxPktAllocator()->allocBuf();
      int n = Zquic::FrameCodec::writeStream(
	packet->data_(), packet->size, uint64_t(stream->id()), offset, "x", false);
      Zquic::Frame frame;
      unsigned used = 0;
      if (n <= 0) { received = false; break; }
      packet->length = unsigned(n);
      received &= !Zquic::FrameCodec::parse(packet->cspan(), frame, used) &&
	stream->receiveFrame(frame, packet);
    }
    received &= stream->rxStream().count_() == 1 && stream->rxPending() == 1;
  });
  bool queued = false;
  unsigned before = 0;
  on(mx, 4, [&link, &stream, &queued, &before] {
    before = link->flushes;
    auto writer = stream->txStream_();
    writer << "queued" << Zi::flush();
    auto head = writer.allocBuf(writer.headRoom());
    auto body = writer.allocBuf(writer.headRoom());
    if (!head || !body || !head->append("head") || !body->append("body")) return;
    writer << ZuMv(head) << ZuMv(body);
    if (stream->txRangeCount() != 1 || stream->finSent()) return;
    writer << "again" << Zi::finish();
    for (unsigned i = 0; i < 16; ++i) stream->recordTxUnackd(i << 1, 1, false);
    queued = stream->txRangeCount() == 4 && stream->txUnackdCount() == 16 &&
      stream->finSent() && link->flushes == before;
  });
  ZuCheck(received && queued);
  bool flushed = false;
  on(mx, 4, [&link, before, &flushed] { flushed = link->flushes == before + 1; });
  ZuCheck(flushed);
  // A completed local unidirectional stream can retire with an external ref.
  QueueLink::StreamRef retired;
  bool reaped = false;
  on(mx, 3, [&link, &retired] {
    link->setPeerStreamLimit(Zquic::StreamType::Simplex, 1);
    retired = link->stream(Zquic::StreamType::Simplex);
  });
  on(mx, 4, [&retired] {
    retired->fin();
    uint64_t offset = 0;
    retired->dequeueFin(offset);
    retired->recordTxUnackd(offset, 0, true);
    retired->ackTxUnackd(offset, 0, true);
  });
  on(mx, 3, [] {});
  // Drain the writable notification before requesting owner retirement.
  on(mx, 4, [&link, &retired, &reaped] {
    link->clearWritableStreams();
    reaped = link->reapStreamByIDTx(retired->id());
  });
  on(mx, 3, [] {});
  on(mx, 4, [] {});
  ZuCheck(reaped && retired && retired->retiredRx() && retired->retiredTx());

  ZmSemaphore done;
  link->disconnect_(false, [&done] { done.post(); });
  ZuCheck(done.timedwait(Zm::now(3)) == 0);
  link = nullptr;
  ZuCheck(stream && retired);
  app.final();
  stream = nullptr;
  retired = nullptr; // queue destruction must not access the retired hub/link
  ZuCheck(mx.stop());
}

static void testRouterQueue()
{
  ZuTestScope(testRouterQueue);
  ZiMultiplex mx;
  QueueApp app;
  ZuCheck(app.init(Zquic::HubParams{&mx}));
  Zquic::CxnRouter<unsigned> router;
  enum { Capacity = Zquic::CxnRouter<unsigned>::TombstoneMax };
  unsigned target = 0;
  bool valid = true;
  for (unsigned i = 0; i < Capacity + 4; ++i) {
    uint8_t bytes[8] = {uint8_t(i)};
    Zquic::CxnID id{ZuBSpan{bytes, sizeof(bytes)}};
    valid &= router.tombstone(id) && router.tombstone(id);
  }
  ZuCheck(valid && router.tombstoneCount() == Capacity);
  uint8_t first[8] = {}, last[8] = {Capacity + 3};
  ZuCheck(router.add(Zquic::CxnID{ZuBSpan{first, sizeof(first)}}, 0, &target) &&
    !router.add(Zquic::CxnID{ZuBSpan{last, sizeof(last)}}, 0, &target));
  router.clear();
  ZuCheck(!router.tombstoneCount());
  router.final();
  app.final();
}

static void testSchedulerQueues()
{
  ZuTestScope(testSchedulerQueues);
  ZiMultiplex mx;
  QueueApp app;
  ZuCheck(app.init(Zquic::HubParams{&mx}));
  {
    Zquic::TxScheduler scheduler;
    ZuCheck(scheduler.addControl(1) && scheduler.addStream(2) && scheduler.addStream(3));
    ZuCheck(scheduler.controlCount() == 1 &&
      scheduler.streamCount() == 2);
    ZuCheck(scheduler.next() == 1);
    scheduler.remove(1);
    ZuCheck(scheduler.next() == 2 && scheduler.next() == 3 && scheduler.next() == 2);
    scheduler.remove(2);
    scheduler.remove(3);
    ZuCheck(scheduler.empty() && !scheduler.controlCount() &&
      !scheduler.streamCount());
  }
  app.final();
}

static void testCryptoQueues()
{
  ZuTestScope(testCryptoQueues);
  ZiMultiplex mx{ZiMxParams{}.scheduler([](auto &s) { s.nThreads(4); })
    .rxThread(1).txThread(2)};
  QueueApp app;
  ZuCheck(app.init(Zquic::HubParams{&mx, "3", "4"}));
  ZuCheck(mx.start());
  {
    Zquic::CryptoStream rx{app.cryptoRxAllocator(), nullptr};
    Zquic::CryptoStream tx{nullptr, app.cryptoTxAllocator()};
    Zquic::CryptoTxPQueue unackd{0};
    bool received = false, retained = false;
    on(mx, 3, [&rx, &received] {
      ZuBSpan span;
      received = !rx.receive(4, "efgh", span) && !span && rx.rxOffset() == 0;
    });
    on(mx, 4, [&app, &tx, &unackd, &retained] {
      auto data = app.cryptoTxAllocator()->allocBuf();
      data->append(ZuBSpan{"abcdefgh"});
      auto original = data->data();
      retained = tx.sent(0, ZuMv(data));
      ZuBSpan payload;
      retained &= tx.txPayload(0, 8, payload) && payload.data() == original &&
	payload == ZuBSpan{"abcdefgh"};
      unackd.add(Zquic::TxUnackdRange{0, 8});
    });
    ZuCheck(received && retained);
    on(mx, 3, [&rx, &received] {
      ZuBSpan span;
      received = !rx.receive(0, "abcd", span) && rx.rxOffset() == 8 &&
	span == ZuBSpan{"abcdefgh"};
      rx.reset();
    });
    on(mx, 4, [&tx, &unackd, &retained] {
      retained = unackd.clear(0, 8);
      tx.reset();
      ZuBSpan payload;
      retained &= !tx.txPayload(0, 8, payload);
    });
    ZuCheck(received && retained);
  }
  app.final();
  ZuCheck(mx.stop());
}

static void testFourEndpointGates()
{
  ZuTestScope(testFourEndpointGates);
  ZiMultiplex mx{ZiMxParams{}.scheduler([](auto &s) { s.nThreads(4); })
    .rxThread(1).txThread(2)};
  QueueApp app;
  ZuCheck(app.init(Zquic::HubParams{&mx, "3", "4"}));
  unsigned queued[4] = {4096, 4096, 4096, 4096};
  using Gate = Zquic_::EndpointTxGate;
  Gate gates[4] = {
    {&mx, &queued[0], 1, 4096, [&queued] { return queued[0]; }},
    {&mx, &queued[1], 1, 4096, [&queued] { return queued[1]; }},
    {&mx, &queued[2], 1, 4096, [&queued] { return queued[2]; }},
    {&mx, &queued[3], 1, 4096, [&queued] { return queued[3]; }}
  };
  ZuCheck(mx.start());
  Zquic_::EndpointTxWait waits[4];
  Zquic_::EndpointTxToken tokens[4];
  unsigned grants[4] = {}, retired[4] = {};
  on(mx, 4, [&mx, &gates, &waits, &tokens, &grants, &retired] {
    for (unsigned i = 0; i < 4; ++i) {
      waits[i].start(&mx, 4, 1, [i, &tokens, &grants, &retired](
	  int result, Zquic_::EndpointTxToken token) {
	if (result == Zquic_::EndpointTxResult::Ready) {
	  tokens[i] = token;
	  ++grants[i];
	} else if (result == Zquic_::EndpointTxResult::Retired) ++retired[i];
      });
      mx.txRun([&gates, &waits, i] { gates[i].waitTx(waits[i]); });
    }
  });
  bool isolated = true;
  on(mx, 2, [&gates, &queued, &isolated] {
    for (auto &gate : gates)
      isolated &= gate.waiting() == 1 && !gate.reservations() && !gate.available();
    --queued[0];
    gates[0].service();
  });
  on(mx, 4, [] {});
  ZuCheck(isolated && grants[0] == 1 && !grants[1] && !grants[2] && !grants[3]);
  on(mx, 2, [&gates, &tokens, &isolated] {
    for (unsigned i = 1; i < 4; ++i)
      isolated &= !gates[i].consume(tokens[0]) && gates[i].waiting() == 1 &&
	!gates[i].reservations();
    isolated &= gates[0].consume(tokens[0]) && !gates[0].consume(tokens[0]);
  });
  on(mx, 4, [] {});
  ZuCheck(isolated && retired[0] == 1 && !waits[0].busy());
  on(mx, 2, [&gates, &queued] {
    for (unsigned i = 1; i < 4; ++i) { --queued[i]; gates[i].service(); }
  });
  on(mx, 4, [] {});
  on(mx, 2, [&gates, &tokens, &isolated] {
    for (unsigned i = 1; i < 4; ++i) {
      for (unsigned j = 0; j < 4; ++j)
	if (i != j) isolated &= !gates[j].consume(tokens[i]);
      isolated &= gates[i].consume(tokens[i]);
    }
  });
  on(mx, 4, [] {});
  for (unsigned i = 0; i < 4; ++i)
    ZuCheck(isolated && grants[i] == 1 && retired[i] == 1 && !waits[i].busy());
  for (auto &gate : gates) ZmBlock<>{}([&mx, &gate](auto wake) {
    mx.txRun([&gate, wake = ZuMv(wake)]() mutable { gate.close(ZuMv(wake)); });
  });
  // All four gates share one finite provider. Its eight slots, rather than
  // 16,384 queue credits, bound aggregate packet backing in this fixture.
  ZmRef<ZquicFiniteProvider> provider = new ZquicFiniteProvider{&mx, 8};
  Zi::IOBufPoolAllocator<Zi::IOBufType::TxUDP, Zquic_::TxPkt> allocator{&mx, provider};
  ZmRef<ZiIOBuf> backing[8];
  bool geometry = true;
  for (unsigned i = 0; i < 8; ++i) {
    backing[i] = allocator.allocBuf(&gates[i >> 1], 0, 1024);
    geometry &= backing[i] && backing[i]->registered() &&
      backing[i]->owner == &gates[i >> 1] &&
      backing[i]->skip == ZquicFiniteProvider::HeadRoom;
  }
  for (auto &gate : gates) geometry &= !allocator.allocBuf(&gate, 0, 1024);
  backing[7] = nullptr;
  backing[7] = allocator.allocBuf(&gates[0], 0, 1024);
  geometry &= backing[7] && backing[7]->owner == &gates[0];
  for (auto &buf : backing) buf = nullptr;
  ZuCheck(geometry);
  log("# four gates: reservation bytes=", 4*4096*24,
    " shared finite backing slots=8 bytes=", 8*ZquicFiniteProvider::SlotSize);
  app.final();
  ZuCheck(mx.stop());
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testFourEndpointGates);
  ZuTestCall(testStreamQueues);
  ZuTestCall(testRouterQueue);
  ZuTestCall(testSchedulerQueues);
  ZuTestCall(testCryptoQueues);
}
