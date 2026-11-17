//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmBlock.hh>
#include <zlib/Zquic.hh>

using namespace ZuTestUtil;

struct QueueApp : Zquic::Hub<QueueApp> {
  ZuArray<Ztc::Link *, 2> links{nullptr, nullptr};
  unsigned allLinks_(Ztc::Hub::AllLinksFn fn) const {
    unsigned n = 0;
    for (auto link : links) if (link) { fn(link); ++n; }
    return n;
  }
};

template <typename Owner, typename L> static void capture(Owner &owner, L fn)
{
  ZtArray<Ztc::QueueTelemetry> rows;
  owner.allQueues([&rows](Ztc::Queue *queue) {
    Ztc::QueueTelemetry row;
    queue->telemetry(row);
    rows.push(ZuMv(row));
  });
  fn(rows);
}
struct QueueLink;
struct QueueStream : Zquic::Stream<QueueLink, QueueStream, Zquic::StreamTxBufAlloc<>> {
  using Base = Zquic::Stream<QueueLink, QueueStream, Zquic::StreamTxBufAlloc<>>;
  using Base::Base;
  int process(Zquic::RxStream &) { return 0; }
};
struct QueueLink : ZmNHeap<QueueLink>,
    Zquic::Link<QueueApp, QueueLink, Zquic::StreamTxBufAlloc<>, QueueStream> {
friend inline ZuStringT<"Zquic.QueueTest.Link"> ZmNHeapID(QueueLink *);
  using ZmNHeap<QueueLink>::operator new;
  using ZmNHeap<QueueLink>::operator delete;
  using Base = Zquic::Link<QueueApp, QueueLink, Zquic::StreamTxBufAlloc<>, QueueStream>;
  using Base::Base;
  using Base::resetLink;
  using Base::disconnect_;
  using Base::reapStreamByIDTx;
  using Base::clearWritableStreams;
  enum {
    Responses = Base::PathResponseMax,
    StaticQueues = 9 + 8*Zquic::PktNumSpace::N
  };

  bool produce() {
    for (unsigned i = 0; i < Responses + 2; ++i)
      if (!Base::txQueueControl(Zquic::ControlFrame::pathResponse(ZuBSpan{"12345678"})))
	return false;
    return Base::recordTxPkt<false>(Zquic::PktNumSpace::AppData, 1, 64,
      Zquic::SentFrameRef{}, true);
  }
  void flushTx_(ZiSockAddr = {}) { }
};

template <typename L> static void on(ZiMultiplex &mx, unsigned sid, L fn)
{
  ZmBlock<>{}([&mx, sid, fn = ZuMv(fn)](auto wake) mutable {
    mx.run([fn = ZuMv(fn), wake = ZuMv(wake)]() mutable { fn(); wake(); }, sid);
  });
}

static void testQueueLifetime()
{
  ZuTestScope(testQueueLifetime);
  ZiMultiplex mx{ZiMxParams{}.scheduler([](auto &s) { s.nThreads(4); })
    .rxThread(1).txThread(2)};
  QueueApp app;
  ZuCheck(app.init(Zquic::HubParams{&mx, "3", "4"}));
  ZuCheck(mx.start());
  unsigned adds = 0, dels = 0;
  bool valid = true;
  Ztc::HubMgr::watch({}, {}, {}, {}, {}, {},
    [&adds, &valid](Ztc::Queue *q) { ++adds; valid &= bool(q->telKey().p<1>()); },
    [&dels, &valid](Ztc::Queue *q) {
      ++dels;
      Ztc::QueueTelemetry data;
      q->telemetry(data); // owner destruction has not released its queues yet
      valid &= data.id == q->telKey().p<1>();
    });
  ZmRef<QueueLink> link = new QueueLink{&app};
  app.links[0] = link.ptr();
  enum { Queues = QueueLink::StaticQueues };
  ZuCheck(valid && adds == Queues && !dels);
  bool produced = false;
  on(mx, 4, [&link, &produced] { produced = link->produce(); });
  unsigned rows = 0, paths = 0, packets = 0;
  capture(app, [&rows, &paths, &packets, &valid](auto data) {
    rows = data.length();
    for (const auto &row : data) {
      ZtString<> id{row.id};
      if (id.template find<":path-response">() >= 0) {
	paths += row.count;
	valid &= row.size == QueueLink::Responses && row.full == 2;
      }
      if (id.template find<":AppData:sent">() >= 0) {
	packets += row.count;
	valid &= row.inCount == 1 && !row.outCount;
      }
    }
  });
  ZuCheck(produced && valid && rows == Queues && paths == QueueLink::Responses && packets == 1);
  unsigned listed = link->allQueues([](Ztc::Queue *) {});
  ZuCheck(listed == rows);

  // Copied snapshots contain no borrowed queue pointers.
  // Initiate shutdown there, then wait only after the observer has returned.
  ZmSemaphore done;
  capture(app, [&link, &done](auto) {
    link->disconnect_(false, [&done] { done.post(); });
  });
  ZuCheck(done.timedwait(Zm::now(3)) == 0);
  capture(app, [&rows](auto data) { rows = data.length(); });
  ZuCheck(valid && rows == Queues && !dels);
  ZmBlock<>{}([&link, &mx](auto wake) mutable {
    mx.run([&link, wake = ZuMv(wake)]() mutable {
      link->resetLink(ZuMv(wake));
    }, 3);
  });
  capture(app, [&rows, &valid](auto data) {
    rows = data.length();
    for (const auto &row : data) valid &= !row.count;
  });
  ZuCheck(valid && rows == Queues && adds == Queues);
  app.links[0] = nullptr;
  link = nullptr;
  capture(app, [&rows](auto data) { rows = data.length(); });
  ZuCheck(valid && !rows && dels == adds);
  Ztc::HubMgr::unwatch();
  app.final();
  ZuCheck(mx.stop());
}

static void testQueueIdentity()
{
  ZuTestScope(testQueueIdentity);
  ZiMultiplex mx{ZiMxParams{}.scheduler([](auto &s) { s.nThreads(4); })
    .rxThread(1).txThread(2)};
  QueueApp app;
  ZuCheck(app.init(Zquic::HubParams{&mx, "3", "4"}));
  // The same long application ID must not truncate distinct queue identities.
  ZuID id{"an-application-link-name-that-needs-a-short-queue-prefix"};
  ZmRef<QueueLink> first = new QueueLink{&app, false, id};
  ZmRef<QueueLink> second = new QueueLink{&app, false, id};
  app.links[0] = first.ptr();
  app.links[1] = second.ptr();
  bool unique = true;
  capture(app, [&unique](auto rows) {
    for (unsigned i = 0, n = rows.length(); i < n; ++i)
      for (unsigned j = i + 1; j < n; ++j)
	if (rows[i].id != "an-application-link-name-that-needs-a-short-queue-prefix")
	  unique &= rows[i].id != rows[j].id || rows[i].type != rows[j].type;
  });
  ZuCheck(unique);
  ZuCheck(first->allQueues([](Ztc::Queue *) {}) == QueueLink::StaticQueues &&
    second->allQueues([](Ztc::Queue *) {}) == QueueLink::StaticQueues);
  app.links[0] = app.links[1] = nullptr;
  first = nullptr;
  second = nullptr;
  using Storage = ZmQueue<unsigned, ZmQueueHeapID<"Zquic.QueueTest.Storage">>;
  {
    Zquic_::Queue<Storage> source, target;
    source.initQueue(&app, "source", "test", Ztc::QueueType::Tx);
    target.initQueue(&app, "target", "test", Ztc::QueueType::Tx);
    source.push(7);
    target = ZuMv(source);
    Ztc::QueueTelemetry before, after;
    source.telemetry(before);
    target.telemetry(after);
    ZuCheck(before.id == "source:test" && !before.count && after.id == "target:test" &&
      after.count == 1 && target.shift() == 7);
    bool rejected = false;
    Zquic_::Queue<Storage> overflow;
    try { overflow.initQueue(&app, id, id, Ztc::QueueType::Tx); }
    catch (const ZeException &) { rejected = true; }
    ZuCheck(rejected && !overflow.telKey().p<1>());
    ZuCheck(!(uintptr_t(static_cast<Storage *>(&source)) % Zm::CacheLineSize) &&
      uintptr_t(static_cast<Storage *>(&source)) >=
	uintptr_t(static_cast<Zquic_::QueueInfo *>(&source)) + sizeof(Zquic_::QueueInfo));
  }
  for (unsigned count : {1U, 9U, 16U}) {
    Zquic::StreamTxPQueue source{0};
    if (count == 9) {
      // Splitting the eight inline ranges can grow their backing without
      // installing the optional index; its move must also empty the source.
      source.add(Zquic::TxUnackdRange{0, 3});
      for (unsigned i = 1; i < 8; ++i)
	source.add(Zquic::TxUnackdRange{(i + 1) << 1, 1});
      source.clear(1, 1);
    } else {
      for (unsigned i = 0; i < count; ++i)
	source.add(Zquic::TxUnackdRange{i << 1, 1});
    }
    Zquic::StreamTxPQueue moved{ZuMv(source)}, target{0};
    target = ZuMv(moved);
    ZuCheck(!source.count_() && !source.length_() && !moved.count_() &&
      !moved.length_() && target.count_() == count && target.length_() == count &&
      source.verify() && moved.verify() && target.verify());
    target.clear(0, count << 1);
    ZuCheck(!target.count_() && !target.length_() && target.verify());
  }
  app.final();
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
  app.links[0] = link.ptr();
  QueueLink::StreamRef stream;
  on(mx, 4, [&link, &stream] {
    link->setPeerStreamLimit(Zquic::StreamType::Duplex, 1);
    stream = link->stream();
  });
  on(mx, 3, [] {}); // complete the other owner's stream publication
  ZuCheck(stream && link->allQueues([](Ztc::Queue *) {}) == QueueLink::StaticQueues + 4);
  unsigned streamRows = 0;
  bool valid = true;
  link->allQueues([&streamRows, &valid](Ztc::Queue *queue) {
    ZtString<> id{queue->telKey().p<1>()};
    if (id.template find<":stream:0:">() < 0) return;
    ++streamRows;
    Ztc::QueueTelemetry row;
    queue->telemetry(row);
    valid &= !row.count && !row.size;
  });
  ZuCheck(valid && streamRows == 4);

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
  });
  on(mx, 4, [&stream] {
    auto writer = stream->txStream_();
    writer << "queued" << Zi::flush();
  });
  unsigned input = 0, reasm = 0, unsent = 0;
  link->allQueues([&input, &reasm, &unsent](Ztc::Queue *queue) {
    ZtString<> id{queue->telKey().p<1>()};
    Ztc::QueueTelemetry row;
    queue->telemetry(row);
    if (id.template find<":stream:0:input">() >= 0) input = row.count;
    if (id.template find<":stream:0:reasm">() >= 0) reasm = row.count;
    if (id.template find<":stream:0:unsent">() >= 0) unsent = row.count;
  });
  ZuCheck(received && input == 1 && reasm == 1 && unsent == 1);

  // Exercise both inline and indexed retained ranges as one telemetry queue.
  on(mx, 4, [&stream] {
    for (unsigned i = 0; i < 16; ++i) stream->recordTxUnackd(i << 1, 1, false);
  });
  uint64_t retained = 0;
  link->allQueues([&retained](Ztc::Queue *queue) {
    ZtString<> id{queue->telKey().p<1>()};
    if (id.template find<":stream:0:unackd">() < 0) return;
    Ztc::QueueTelemetry row;
    queue->telemetry(row);
    retained += row.count;
  });
  ZuCheck(retained == 16);
  on(mx, 4, [&stream] { stream->txUnackdQueue()->clean(); });

  ZmSemaphore indexedDone;
  app.txRun([stream, &indexedDone] {
    for (unsigned n = 0; n < 256; ++n) {
      for (unsigned i = 0; i < 16; ++i) stream->recordTxUnackd(i << 1, 1, false);
      stream->txUnackdQueue()->clean();
    }
    indexedDone.post();
  });
  for (unsigned i = 0; i < 256; ++i)
    capture(app, [](auto) {});
  ZuCheck(indexedDone.timedwait(Zm::now(3)) == 0);

  // A completed local unidirectional stream can retire with an external ref.
  QueueLink::StreamRef retired;
  bool reaped = false;
  on(mx, 4, [&link, &retired] {
    link->setPeerStreamLimit(Zquic::StreamType::Simplex, 1);
    retired = link->stream(Zquic::StreamType::Simplex);
    retired->fin();
    uint64_t offset = 0;
    retired->dequeueFin(offset);
  });
  on(mx, 3, [] {});
  // Drain the writable notification before requesting owner retirement.
  on(mx, 4, [&link, &retired, &reaped] {
    link->clearWritableStreams();
    reaped = link->reapStreamByIDTx(retired->id());
  });
  on(mx, 3, [] {});
  on(mx, 4, [] {});
  ZuCheck(reaped && retired &&
    link->allQueues([](Ztc::Queue *) {}) == QueueLink::StaticQueues + 4);

  ZmSemaphore done;
  link->disconnect_(false, [&done] { done.post(); });
  ZuCheck(done.timedwait(Zm::now(3)) == 0);
  ZuCheck(link->allQueues([](Ztc::Queue *) {}) == QueueLink::StaticQueues);
  app.links[0] = nullptr;
  link = nullptr;
  unsigned rows = 1;
  capture(app, [&rows](auto data) { rows = data.length(); });
  ZuCheck(!rows && stream && retired);
  app.final();
  stream = nullptr;
  retired = nullptr; // queue destruction must not access the retired hub/link
  ZuCheck(mx.stop());
}

template <typename Owner>
static Ztc::QueueTelemetry queueRow(Owner &owner, ZuCSpan id)
{
  Ztc::QueueTelemetry row;
  capture(owner, [&row, id](auto rows) {
    for (const auto &entry : rows) if (entry.id == id) { row = entry; break; }
  });
  return row;
}

static void testRouterQueue()
{
  ZuTestScope(testRouterQueue);
  ZiMultiplex mx;
  QueueApp app;
  ZuCheck(app.init(Zquic::HubParams{&mx}));
  Zquic::CxnRouter<unsigned> router;
  router.initQueue(&app);
  enum { Capacity = Zquic::CxnRouter<unsigned>::TombstoneMax };
  unsigned target = 0;
  bool valid = true;
  for (unsigned i = 0; i < Capacity + 4; ++i) {
    uint8_t bytes[8] = {uint8_t(i)};
    Zquic::CxnID id{ZuBSpan{bytes, sizeof(bytes)}};
    valid &= router.tombstone(id) && router.tombstone(id);
  }
  auto row = queueRow(router, "quic:router:tombstones");
  ZuCheck(valid && row.count == Capacity && row.size == Capacity &&
    row.full == 4 && row.type == Ztc::QueueType::Rx);
  uint8_t first[8] = {}, last[8] = {Capacity + 3};
  ZuCheck(router.add(Zquic::CxnID{ZuBSpan{first, sizeof(first)}}, 0, &target) &&
    !router.add(Zquic::CxnID{ZuBSpan{last, sizeof(last)}}, 0, &target));
  router.clear();
  row = queueRow(router, "quic:router:tombstones");
  ZuCheck(!row.count && row.full == 4);
  router.final();
  ZuCheck(!queueRow(router, "quic:router:tombstones").count);
  router.init();
  router.initQueue(&app);
  ZuCheck(queueRow(router, "quic:router:tombstones").id);
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
    scheduler.initQueues(&app, "scheduler");
    ZuCheck(scheduler.addControl(1) && scheduler.addStream(2) && scheduler.addStream(3));
    ZuCheck(queueRow(scheduler, "scheduler:control").count == 1 &&
      queueRow(scheduler, "scheduler:stream").count == 2);
    ZuCheck(scheduler.next() == 1);
    scheduler.remove(1);
    ZuCheck(scheduler.next() == 2 && scheduler.next() == 3 && scheduler.next() == 2);
    scheduler.remove(2);
    scheduler.remove(3);
    ZuCheck(scheduler.empty() && !queueRow(scheduler, "scheduler:control").count &&
      !queueRow(scheduler, "scheduler:stream").count);
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
    rx.initRxQueue(&app, "crypto:Handshake");
    tx.initTxQueue(&app, "crypto:Handshake");
    unackd.initQueue(&app, "crypto:Handshake", "unackd", Ztc::QueueType::Tx);
    struct CryptoQueues {
      Zquic::CryptoStream &rx, &tx;
      Zquic::CryptoTxPQueue &unackd;
      unsigned allQueues(Ztc::QueueMgr::AllFn fn) const {
        fn(rx.rxQueue()); fn(tx.txQueue()); fn(&unackd);
        return 3;
      }
    } queues{rx, tx, unackd};
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
    ZuCheck(received && retained &&
      queueRow(queues, "crypto:Handshake:reasm").count == 1 &&
      queueRow(queues, "crypto:Handshake:retained").count == 1 &&
      queueRow(queues, "crypto:Handshake:unackd").count == 1);
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
    ZuCheck(received && retained &&
      !queueRow(queues, "crypto:Handshake:reasm").count &&
      !queueRow(queues, "crypto:Handshake:retained").count &&
      !queueRow(queues, "crypto:Handshake:unackd").count);
  }
  unsigned rows = 1;
  capture(app, [&rows](auto data) { rows = data.length(); });
  ZuCheck(!rows);
  app.final();
  ZuCheck(mx.stop());
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testQueueLifetime);
  ZuTestCall(testQueueIdentity);
  ZuTestCall(testStreamQueues);
  ZuTestCall(testRouterQueue);
  ZuTestCall(testSchedulerQueues);
  ZuTestCall(testCryptoQueues);
}
