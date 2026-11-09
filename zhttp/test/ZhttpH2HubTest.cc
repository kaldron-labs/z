//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmSingleton.hh>
#include <zlib/ZmAtomic.hh>
#include <zlib/ZmSemaphore.hh>

#include <zlib/ZiMultiplex.hh>
#include <zlib/ZiLog.hh>

#include <zlib/ZhttpClient.hh>
#include "../src/ZhttpServerImpl.hh"
#include "../src/ZhttpClientImpl.hh"
#include <zlib/ZhttpServer.hh>

#include "ZhttpTestUtil.hh"

using namespace ZuTestUtil;

namespace ZhttpH2HubTest_ {

using ResponseHeaderList = ZhttpHeaders("content-length");
ZhttpHdrCatalogDerive(ResponseHeaders, ResponseHeaderList);
ZhttpHdrCatalogImpl(ResponseHeaders)

struct State {
  ZmSemaphore	listening;
  ZmSemaphore	response;
  ZmSemaphore	h1Connected;
  ZmSemaphore	goaway;
  ZmSemaphore	stopped;
  ZmAtomic<unsigned> errors = 0;
  ZmAtomic<unsigned> closed = 0;
  ZmAtomic<unsigned> admissions = 0;
  ZmAtomic<unsigned> releases = 0;
  unsigned	clientNative = 0;
  unsigned	maxClientNative = 0;
  uint16_t	port = 0;
};

struct FlowAppLink : public ZmObject { };

struct FlowApp {
  enum { Rx, Tx };

  ZiMultiplex *mx() { return &multiplex; }
  unsigned rxThread() const { return 0; }
  bool rxInvoked() const { return shard == Rx; }
  bool txInvoked() const { return shard == Tx; }
  using Task = ZmFn<void(), ZmFnHeapID<"Zhttp.H2.FlowTest.Task">>;
  using Tasks = ZtArray<Task, ZtArrayHeapID<"Zhttp.H2.FlowTest.Tasks">>;
  template <typename L> void rxRun(L &&l) {
    if (deferred) rxTasks.push(Task{ZuFwd<L>(l)});
    else run(Rx, ZuFwd<L>(l));
  }
  template <typename L> void rxInvoke(L &&l) {
    if (rxInvoked()) ZuFwd<L>(l)();
    else rxRun(ZuFwd<L>(l));
  }
  template <typename L> void txRun(L &&l) {
    if (deferred) txTasks.push(Task{ZuFwd<L>(l)});
    else run(Tx, ZuFwd<L>(l));
  }
  bool step(int shard_) {
    auto &tasks = shard_ == Rx ? rxTasks : txTasks;
    if (!tasks.length()) return false;
    Task task = ZuMv(tasks[0]);
    tasks.shift(1);
    run(shard_, ZuMv(task));
    return true;
  }
  template <typename L> void runTx(L &&l) { run(Tx, ZuFwd<L>(l)); }

private:
  template <typename L> void run(int shard_, L &&l) {
    int prev = shard;
    shard = shard_;
    ZuFwd<L>(l)();
    shard = prev;
  }

public:

  ZiMultiplex multiplex;
  int shard = Rx;
  bool deferred = false;
  Tasks rxTasks, txTasks;
};

struct FlowH1App : FlowApp {
  unsigned idleTimeout() const { return 0; }
  template <typename Link> void connected(Link &, Zhttp::ConnectedInfo) { }
  template <typename Link> void disconnected(Link &, bool) { }
  template <typename Link> void connectFailed(Link &, bool) { }
};

struct FlowH1Native : ZmNHeap<FlowH1Native>, ZmObject {
friend inline ZuStringT<"Zhttp.H1.FlowNative"> ZmNHeapID(FlowH1Native *);
  FlowH1App *app;
  unsigned ends = 0;
  unsigned resets = 0;

  struct Tx { };
  Tx txStream() { ZmAssert(app->rxInvoked()); return {}; }
  Tx txStream_() { ZmAssert(app->txInvoked()); return {}; }
  void streamTxEnd() { ZmAssert(app->txInvoked()); ++ends; }
  void closeAfterTx() { ZmAssert(app->txInvoked()); ++ends; }
  void disconnectNative() { ZmAssert(app->rxInvoked()); ++resets; }
};

struct FlowH1Channel {
  template <typename Rx> int frameInput(Rx &) { return 0; }
  void frameEnd() { }
  void frameError() { }
};

struct FlowH1Client : ZmNHeap<FlowH1Client>,
    Zhttp::TLS_::ClientH1AppLink<FlowH1App, FlowH1Client, FlowH1Native> {
friend inline ZuStringT<"Zhttp.H1.FlowClient"> ZmNHeapID(FlowH1Client *);
  using Base = Zhttp::TLS_::ClientH1AppLink<FlowH1App, FlowH1Client, FlowH1Native>;
  FlowH1Channel channel_;

  using Base::Base;
  auto &channel() { return channel_; }
};

struct FlowH1Session {
  template <typename Link> void connected(Link &) { }
  template <typename Link> void disconnected(Link &, bool) { }
};

struct FlowH1Server : ZmNHeap<FlowH1Server>,
    Zhttp::TLS_::ServerH1AppLink<
      FlowH1App, FlowH1Server, FlowH1Session, FlowH1Native> {
friend inline ZuStringT<"Zhttp.H1.FlowServer"> ZmNHeapID(FlowH1Server *);
  using Base = Zhttp::TLS_::ServerH1AppLink<
    FlowH1App, FlowH1Server, FlowH1Session, FlowH1Native>;
  FlowH1Channel channel_;

  using Base::Base;
  auto &channel() { return channel_; }
};

void runH1Shards()
{
  ZuTestScopeRT(runH1Shards);
  FlowH1App app;
  app.deferred = true;
  ZmRef<FlowH1Native> native = new FlowH1Native;
  native->app = &app;
  auto exercise = [&app, &native](auto link) {
    ZuCheckRT(link->streamEnable() && link->streamAccept(), log_("Rx negotiation"));
    unsigned calls = 0;
    link->streamTx([&calls](auto &) { ++calls; });
    ZuCheckRT(calls == 1, log_("Rx borrowed writer completes synchronously"));
    app.runTx([&link, &calls]() {
      link->streamTx_([&calls](auto &) { ++calls; });
    });
    ZuCheckRT(calls == 1, log_("Tx cannot read unpublished Rx admission"));
    ZuCheckRT(app.step(FlowApp::Tx) && app.step(FlowApp::Tx),
      log_("publish native handle and admission in order"));
    app.runTx([&link, &calls]() {
      link->streamTx_([&calls](auto &) { ++calls; });
      link->streamTxEnd();
    });
    ZuCheckRT(calls == 2 && app.step(FlowApp::Rx) && app.step(FlowApp::Tx) &&
        native->ends == 1, log_("Tx writer is synchronous; end crosses Rx then Tx"));
    app.runTx([&link, &calls]() {
      link->streamTx_([&calls](auto &) { ++calls; });
      link->streamTxReset();
    });
    link->streamTx([&calls](auto &) { ++calls; });
    ZuCheckRT(calls == 2 && app.step(FlowApp::Rx) && app.step(FlowApp::Tx) &&
        app.step(FlowApp::Rx) && native->resets == 1,
          log_("terminal admission is closed and reset disconnects on Rx"));
    link->disconnected_(false);
    ZuCheckRT(app.step(FlowApp::Tx) && !app.step(FlowApp::Tx) &&
        !app.step(FlowApp::Rx), log_("retire the Tx native handle before release"));
    native->ends = native->resets = 0;
  };
  ZmRef<FlowH1Client> client = new FlowH1Client{&app};
  client->native(native.ptr());
  client->connected_({});
  exercise(client);
  ZmRef<FlowH1Server> server = new FlowH1Server{&app, native.ptr(), ZiIP{}, 0};
  server->connected_({});
  exercise(server);
}

struct FlowWire : public Zhttp::H2_::Wire<FlowWire, FlowAppLink> {
  using BufAlloc = Zi::IOBufBuiltin<
    Zhttp::Transport_::TxBufNode,
    65544,
    ZiIOBuf_DefltAlign,
    ZuStringT<"Zhttp.H2.FlowTest">,
    65544>;

  struct Tx : public ZiTxStream<Tx> {
    using Base = ZiTxStream<Tx>;

    Tx(FlowWire *wire_) : Base{65544, 0, 0}, wire{wire_} { }

    ~Tx() { this->flush(); }

    ZmRef<ZiIOBuf> allocBuf_(unsigned headRoom) {
      ZmRef<ZiIOBuf> buf = new BufAlloc{ZmSingleton<typename BufAlloc::Allocator>::instance()};
      buf->skip = headRoom;
      buf->length = 0;
      return buf;
    }
    bool appendBuf(ZmRef<ZiIOBuf> buf) {
      if (!this->flush() || !buf) return false;
      if (!buf || wire->rejectControl || wire->transportClosed) return false;
      wire->lastBuf = ZuMv(buf);
      ++wire->ownedCalls;
      return true;
    }
    bool sendBuf_(ZmRef<ZiIOBuf> buf, bool) {
      if (!buf || wire->transportClosed) return false;
      return wire->frameSent(buf->cspan());
    }

    FlowWire *wire;
  };

  FlowWire(FlowApp *app_) : app_{app_} { }

  FlowApp *app() const { return app_; }
  static unsigned txMaxSize() { return BufAlloc::Size; }
  template <bool> unsigned txHeadRoom() const { return 0; }
  template <bool> unsigned txTailRoom() const { return 0; }
  template <bool> ZmRef<ZiIOBuf> txAllocBuf(unsigned headRoom) {
    ZmRef<ZiIOBuf> buf = new BufAlloc{ZmSingleton<typename BufAlloc::Allocator>::instance()};
    buf->skip = headRoom;
    buf->length = 0;
    return buf;
  }
  bool frameSent(ZuBSpan frame) {
    ++sends;
    if (rejectControl) return false;
    uint8_t type = frame.data()[3], flags = frame.data()[4];
    if (headerOpen && type != Zhttp::H2::FrameType::Continuation)
      ++interleaved;
    switch (type) {
      case Zhttp::H2::FrameType::Headers:
	++headerFrames;
	headerOpen = !(flags & Zhttp::H2::Flag::EndHeaders);
	break;
      case Zhttp::H2::FrameType::Continuation:
	++continuationFrames;
	headerOpen = !(flags & Zhttp::H2::Flag::EndHeaders);
	break;
    }
    if (frame.data()[3] == Zhttp::H2::FrameType::Data) {
      dataLength = (unsigned(frame.data()[0])<<16) |
	(unsigned(frame.data()[1])<<8) | frame.data()[2];
      dataFlags = frame.data()[4];
    }
    return true;
  }
  bool sendSource(const Zhttp::H2_::TxFrame &frame) {
    if (!frameSent(frame.header)) return false;
    if (frame.payload && frame.payload->length) {
      lastBuf = frame.payload;
      ++ownedCalls;
    }
    return true;
  }
  Tx txStream() { return Tx{this}; }
  Tx txStream_() { return Tx{this}; }
  void h2CapacityTx(bool value) {
    capacity = value;
    ++capacityCalls;
    if (value) ++saturatedCalls;
  }
  void h2LocalEnd(uint32_t) { }
  void h2SettingsReceived() { ++settingsApplied; }
  void h2Cancel(uint32_t) { }
  bool h2Closed(uint32_t id) const { return streamClosed(id); }
  void h2ResetAppLink(uint32_t, Zhttp::H2::Error::T) { }
  void disconnect_(bool = true) {
    ++protocolDisconnects;
    // TLS closes its Tx session in a queued owner turn.
    app_->txRun([this]() { transportClosed = true; });
  }
  void disconnectNative() {
    transportClosed = true;
    ++disconnects;
    disconnectShard = app_->shard;
  }

  FlowApp	*app_;
  unsigned	sends = 0;
  ZmRef<ZiIOBuf>	lastBuf;
  unsigned	ownedCalls = 0;
  unsigned	dataLength = 0;
  unsigned	dataFlags = 0;
  unsigned	capacityCalls = 0;
  unsigned	saturatedCalls = 0;
  unsigned	protocolDisconnects = 0;
  unsigned	settingsApplied = 0;
  unsigned	headerFrames = 0;
  unsigned	continuationFrames = 0;
  unsigned	interleaved = 0;
  unsigned	disconnects = 0;
  int		disconnectShard = -1;
  bool		capacity = false;
  bool		rejectControl = false;
  bool		headerOpen = false;
  bool		transportClosed = false;
};

void runConnectionOverflow()
{
  ZuTestScopeRT(runConnectionOverflow);
  enum { Count = Zhttp::H2_::FrameDrainBatch + 1 };
  FlowApp app;
  app.deferred = true;
  FlowWire wire{&app};
  ZuCheckRT(wire.initWire(false, Zhttp::H2Config().settingsTimeout(0)
    .maxQueuedFrames(Count + 1)));
  uint8_t opaque[8] = {};
  for (unsigned i = 0; i < Count; ++i) wire.h2Ping(ZuBSpan{opaque, 8});
  wire.h2WindowUpdate(0, Zhttp::H2::MaxWindow);
  bool queued = true;
  for (unsigned i = 0; i < Count; ++i) queued &= app.step(FlowApp::Tx);
  ZuCheckRT(queued && !wire.sends);
  ZuCheckRT(app.step(FlowApp::Tx) && app.step(FlowApp::Rx));
  while (app.step(FlowApp::Tx) || app.step(FlowApp::Rx)) { }
  ZuCheckRT(wire.sends == Count + 1 && !wire.dataLength &&
    wire.protocolDisconnects == 1 && !wire.disconnects && wire.transportClosed,
    log_("fatal GOAWAY drains after a bounded control batch before TLS closes"));
  bool done = false;
  wire.clearStreams([&done]() { done = true; });
  while (app.step(FlowApp::Tx) || app.step(FlowApp::Rx)) { }
  ZuCheckRT(done);
  wire.finalWire();
}

void runInitialControlFailure()
{
  ZuTestScope(runInitialControlFailure);
  FlowApp app;
  auto config = Zhttp::H2Config().settingsTimeout(0);
  FlowWire accepted{&app};
  ZuCHECK(accepted.initWire(true, config), "initialize accepted control harness");
  accepted.sendInitial();
  ZuCHECK(accepted.settingsOutstanding() == 1 &&
      !accepted.protocolDisconnects,
    "submitted SETTINGS becomes outstanding");
  accepted.finalWire();

  FlowWire rejected{&app};
  ZuCHECK(rejected.initWire(true, config), "initialize rejected control harness");
  rejected.rejectControl = true;
  rejected.sendInitial();
  ZuCHECK(!rejected.settingsOutstanding() &&
      rejected.protocolDisconnects + rejected.disconnects == 1,
    "rejected SETTINGS does not arm success state and disconnects once");
  rejected.finalWire();

  FlowWire ping{&app};
  ZuCHECK(ping.initWire(true, config), "initialize rejected PING harness");
  ping.rejectControl = true;
  uint8_t opaque[8] = {};
  ping.h2Ping(ZuBSpan{opaque, 8});
  ZuCHECK(ping.protocolDisconnects + ping.disconnects == 1,
    "rejected PING ACK disconnects the connection");
  ping.finalWire();

  FlowWire goaway{&app};
  ZuCHECK(goaway.initWire(true, config), "initialize rejected GOAWAY harness");
  goaway.rejectControl = true;
  goaway.graceful();
  ZuCHECK(goaway.disconnects == 1,
    "rejected graceful GOAWAY disconnects the connection");
  goaway.finalWire();

  FlowWire reset{&app};
  ZuCHECK(reset.initWire(true, config), "initialize rejected RESET harness");
  reset.rejectControl = true;
  ZuCHECK(!reset.resetStream(1, Zhttp::H2::Error::Cancel) &&
      reset.protocolDisconnects + reset.disconnects == 1,
    "rejected RESET reports failure and disconnects the connection");
  reset.finalWire();

  FlowWire ack{&app};
  ZuCHECK(ack.initWire(true, config), "initialize rejected SETTINGS ACK harness");
  ack.rejectControl = true;
  ack.h2Settings();
  ZuCHECK(!ack.settingsApplied && ack.protocolDisconnects + ack.disconnects == 1,
    "rejected SETTINGS ACK does not publish readiness");
  ack.finalWire();
}

ZmRef<ZiIOBuf> flowData(unsigned length, uint32_t id = 1)
{
  ZmRef<ZiIOBuf> buf = new FlowWire::BufAlloc{ZmSingleton<typename FlowWire::BufAlloc::Allocator>::instance()};
  buf->length = length + Zhttp::H2::FrameHeaderSize;
  uint8_t *data = buf->data();
  data[0] = uint8_t(length>>16);
  data[1] = uint8_t(length>>8);
  data[2] = uint8_t(length);
  data[3] = Zhttp::H2::FrameType::Data;
  data[4] = 0;
  data[5] = uint8_t(id>>24);
  data[6] = uint8_t(id>>16);
  data[7] = uint8_t(id>>8);
  data[8] = uint8_t(id);
  return buf;
}

Zhttp::H2_::HeaderFrames flowHeaders()
{
  Zhttp::H2_::HeaderFrames frames;
  auto buf = flowData(0);
  buf->data()[3] = Zhttp::H2::FrameType::Headers;
  buf->data()[4] = Zhttp::H2::Flag::EndHeaders;
  frames.push(Zhttp::H2_::TxFrame{ZuMv(buf)});
  return frames;
}

void runFlowCapacity()
{
  ZuTestScope(runFlowCapacity);

  FlowApp app;
  FlowWire wire{&app};
  auto config = Zhttp::H2Config()
    .maxConcurrentStreams(2).maxQueuedFrames(16);
  ZuCHECK(wire.initWire(false, config), "initialize H2 flow harness");
  ZmRef<FlowAppLink> first = new FlowAppLink;
  ZmRef<FlowAppLink> second = new FlowAppLink;
  auto firstStream = wire.openLocalStream(first);
  auto secondStream = wire.openLocalStream(second);
  ZuCHECK(firstStream && secondStream,
    "open two independent H2 streams");

  ZuCHECK(wire.peerInitialWindow(1),
    "reduce the peer per-stream window to one byte");
  app.runTx([&wire, id = firstStream->id]() {
    ZuCHECK(wire.dataMaxSize<false>(id) ==
        Zhttp::H2::FrameHeaderSize + Zhttp::H2::DefltFrameSize,
      "DATA buffer geometry uses the negotiated frame size");
  });
  ZuCHECK(wire.peerInitialWindow(0),
    "reduce the peer per-stream window to zero");
  app.runTx([&wire, id = firstStream->id]() {
    ZuCHECK(wire.dataMaxSize<false>(id) ==
        Zhttp::H2::FrameHeaderSize + Zhttp::H2::DefltFrameSize,
      "DATA buffer geometry stays bounded at zero stream credit");
  });
  wire.capacityCalls = wire.saturatedCalls = 0;
  wire.capacity = false;
  ZuCHECK(wire.sendData(firstStream->id, flowData(1)) &&
      !wire.capacity && !wire.saturatedCalls,
    "one blocked H2 stream does not saturate its connection");

  wire.h2WindowUpdate(firstStream->id, 1);
  wire.h2WindowUpdate(secondStream->id, 65535);
  wire.capacityCalls = wire.saturatedCalls = 0;
  wire.capacity = false;
  ZuCHECK(wire.sendData(secondStream->id, flowData(16384)) &&
      wire.sendData(secondStream->id, flowData(16384)) &&
      wire.sendData(secondStream->id, flowData(16384)) &&
      wire.sendData(secondStream->id, flowData(16382)),
    "consume the H2 connection window");
  ZuCHECK(wire.sendData(secondStream->id, flowData(1)) &&
      wire.capacity && wire.saturatedCalls == 1,
    "connection-level H2 flow control saturates the link (capacity=",
    wire.capacity, ", calls=", wire.capacityCalls,
    ", saturated=", wire.saturatedCalls, ", sends=", wire.sends, ')');
  app.runTx([&wire, id = secondStream->id]() {
    ZuCHECK(wire.dataMaxSize<false>(id) ==
        Zhttp::H2::FrameHeaderSize + Zhttp::H2::DefltFrameSize,
      "DATA buffer geometry stays bounded at zero connection credit");
  });

  wire.capacityCalls = wire.saturatedCalls = 0;
  wire.h2WindowUpdate(0, 1);
  ZuCHECK(!wire.capacity && wire.capacityCalls >= 1 &&
      !wire.saturatedCalls,
    "connection WINDOW_UPDATE restores H2 admission capacity (calls=",
    wire.capacityCalls, ')');

  bool cleared = false;
  wire.clearStreams([&cleared]() { cleared = true; });
  ZuCHECK(cleared, "clear H2 flow harness streams");
  wire.finalWire();
}

void runBlockedRequeue()
{
  ZuTestScope(runBlockedRequeue);
  FlowApp app;
  FlowWire wire{&app};
  enum { Count = Zhttp::H2_::FrameDrainBatch + 1 };
  auto config = Zhttp::H2Config().maxConcurrentStreams(Count + 1)
    .maxQueuedFrames(Count);
  ZuCHECK(wire.initWire(false, config), "initialize bounded requeue harness");
  ZmRef<FlowAppLink> first = new FlowAppLink;
  uint32_t id = wire.openLocalStream(first)->id;
  ZuCHECK(wire.sendData(id, flowData(16384, id)) &&
      wire.sendData(id, flowData(16384, id)) &&
      wire.sendData(id, flowData(16384, id)) &&
      wire.sendData(id, flowData(16383, id)), "consume connection credit");
  bool queued = true;
  for (unsigned i = 0; i < Count; ++i) {
    ZmRef<FlowAppLink> link = new FlowAppLink;
    uint32_t stream = wire.openLocalStream(link)->id;
    queued &= wire.sendData(stream, flowData(1, stream));
  }
  ZuCHECK(queued && wire.capacity, "queue more blocked streams than one turn");
  unsigned sends = wire.sends;
  app.deferred = true;
  wire.h2WindowUpdate(0, Count);
  ZuCHECK(app.step(FlowApp::Tx) && wire.capacity && wire.sends == sends,
    "first requeue turn yields with blocked streams remaining");
  ZuCHECK(app.step(FlowApp::Tx) && !wire.capacity && wire.sends == sends,
    "requeue continuation schedules the remaining stream without output");
  ZuCHECK(app.step(FlowApp::Tx) && wire.sends == sends + Count - 1 &&
      app.step(FlowApp::Tx) && wire.sends == sends + Count,
    "bounded output turns consume all newly granted credit");
  bool done = false;
  wire.clearStreams([&done]() { done = true; });
  while (app.step(FlowApp::Rx) || app.step(FlowApp::Tx)) { }
  ZuCHECK(done, "teardown drains requeue and output continuations");
  wire.finalWire();
}

void runWindowChanges()
{
  ZuTestScopeRT(runWindowChanges);
  enum { Count = Zhttp::H2_::FrameDrainBatch + 1 };
  for (unsigned mode = 0; mode < 4; ++mode) {
    FlowApp app;
    FlowWire wire{&app};
    auto config = Zhttp::H2Config().maxConcurrentStreams(Count + 1)
      .maxQueuedFrames(Count + 1);
    ZuCheckRT(wire.initWire(false, config), log_("initialize window-change harness"));
    ZuCheckRT(wire.peerInitialWindow(0), log_("start streams without DATA credit"));
    uint32_t last = 0;
    bool queued = true;
    for (unsigned i = 0; i < Count; ++i) {
      ZmRef<FlowAppLink> link = new FlowAppLink;
      last = wire.openLocalStream(link)->id;
      if (mode < 2) queued &= wire.sendData(last, flowData(1, last));
    }
    ZuCheckRT(queued && !wire.sends, log_("prepare more windows than one turn"));
    if (mode == 2) wire.h2WindowUpdate(last, Zhttp::H2::MaxWindow);
    app.deferred = true;
    ZuCheckRT(wire.peerInitialWindow(1), log_("begin a bounded initial-window change"));
    bool removed = false;
    if (!mode) {
      wire.h2WindowUpdate(last, 1);
      ZuCheckRT(wire.peerInitialWindow(0), log_("queue a second SETTINGS value"));
      wire.h2Settings();
    } else if (mode == 1) {
      ZmRef<FlowAppLink> link = new FlowAppLink;
      uint32_t added = wire.openLocalStream(link)->id;
      queued = wire.sendData(added, flowData(1, added));
      wire.removeStream(last, [&removed]() { removed = true; });
      ZuCheckRT(queued && app.step(FlowApp::Rx),
        log_("publish new stream and removal behind the initial-window request"));
    }
    ZuCheckRT(app.step(FlowApp::Tx) && !wire.sends && !wire.protocolDisconnects,
      log_("validation yields before the captured tail without transmitting DATA"));
    bool done = false;
    if (mode == 3) {
      wire.h2WindowUpdate(last, 1);
      ZuCheckRT(wire.peerInitialWindow(0), log_("queue window changes before stop"));
      wire.clearStreams([&done]() { done = true; });
    }
    while (app.step(FlowApp::Tx) || app.step(FlowApp::Rx)) { }
    switch (mode) {
      case 0:
	ZuCheckRT(wire.sends == 2 && wire.dataLength == 1 &&
	    wire.settingsApplied == 1 && !wire.protocolDisconnects,
	      log_("window changes apply in arrival order before SETTINGS acknowledgment"));
	break;
      case 1:
	ZuCheckRT(removed && wire.sends == Count && !wire.protocolDisconnects,
	  log_("removal repairs the cursor and new streams use the target window"));
	break;
      case 2:
	ZuCheckRT(wire.protocolDisconnects == 1 && wire.sends == 1 && !wire.dataLength,
	  log_("overflow at the captured tail disconnects with GOAWAY and no DATA"));
	break;
      case 3:
	ZuCheckRT(done && !wire.protocolDisconnects,
	  log_("stop waits for active traversal and queued window-change retirement"));
	break;
    }
    if (mode != 3) wire.clearStreams([&done]() { done = true; });
    while (app.step(FlowApp::Rx) || app.step(FlowApp::Tx)) { }
    ZuCheckRT(done, log_("shutdown drains pending window changes"));
    wire.finalWire();
  }
}

void runHeaderTurns()
{
  ZuTestScopeRT(runHeaderTurns);
  enum { Batch = Zhttp::H2_::FrameDrainBatch, Count = 3 * Batch + 1 };
  for (unsigned mode = 0; mode < 2; ++mode) {
    FlowApp app;
    FlowWire wire{&app};
    auto config = Zhttp::H2Config().maxConcurrentStreams(2)
      .maxQueuedFrames(Count + 1);
    ZuCheckRT(wire.initWire(false, config), log_("initialize header-turn harness"));
    ZmRef<FlowAppLink> first = new FlowAppLink, second = new FlowAppLink;
    uint32_t id = wire.openLocalStream(first)->id;
    uint32_t other = wire.openLocalStream(second)->id;
    Zhttp::H2_::HeaderFrames frames;
    for (unsigned i = 0; i < Count; ++i) {
      auto buf = flowData(0, id);
      buf->data()[3] = i ? Zhttp::H2::FrameType::Continuation :
	Zhttp::H2::FrameType::Headers;
      buf->data()[4] = i + 1 == Count ? Zhttp::H2::Flag::EndHeaders : 0;
      frames.push(Zhttp::H2_::TxFrame{ZuMv(buf)});
    }
    app.deferred = true;
    ZuCheckRT(wire.sendHeaders(id, ZuMv(frames), false) &&
	wire.sendData(other, flowData(1, other)),
      log_("admit a long header chain followed by another stream's DATA"));
    ZuCheckRT(app.step(FlowApp::Tx) && app.step(FlowApp::Tx) && !wire.sends,
      log_("splice the complete header chain before scheduling output"));
    uint8_t opaque[8] = {};
    wire.h2Ping({opaque, 8});
    wire.h2Settings();
    ZuCheckRT(wire.resetStream(id, Zhttp::H2::Error::Cancel),
      log_("admit control frames behind the first output turn"));
    bool removed = false;
    if (mode) {
      wire.removeStream(id, [&removed]() { removed = true; });
      ZuCheckRT(app.step(FlowApp::Rx), log_("publish stream removal to Tx"));
    }
    ZuCheckRT(app.step(FlowApp::Tx) && wire.sends == Batch && wire.headerOpen,
      log_("header emission yields after exactly one frame budget"));
    ZuCheckRT(app.step(FlowApp::Tx) && app.step(FlowApp::Tx) &&
	app.step(FlowApp::Tx) && wire.sends == Batch && !wire.settingsApplied,
      log_("PING, SETTINGS ACK, and RESET wait for END_HEADERS"));
    if (mode)
      ZuCheckRT(app.step(FlowApp::Tx) &&
	  wire.sends == (Batch<<1) && wire.headerOpen && !removed,
	log_("stream removal emits another header batch instead of polling readiness"));
    while (app.step(FlowApp::Tx) || app.step(FlowApp::Rx)) { }
    ZuCheckRT(wire.sends == Count + 4 && wire.headerFrames == 1 &&
	wire.continuationFrames == Count - 1 && !wire.headerOpen &&
	!wire.interleaved && wire.settingsApplied == 1 && (!mode || removed),
      log_("finish the header chain before controls, DATA, and stream retirement"));
    bool done = false;
    wire.clearStreams([&done]() { done = true; });
    while (app.step(FlowApp::Rx) || app.step(FlowApp::Tx)) { }
    ZuCheckRT(done, log_("stop drains serialized control and header output"));
    wire.finalWire();
  }
}

void runBoundedFrameCleanup()
{
  ZuTestScopeRT(runBoundedFrameCleanup);
  for (unsigned mode = 0; mode < 2; ++mode) {
    FlowApp app;
    FlowWire wire{&app};
    enum { Count = Zhttp::H2_::FrameDrainBatch + 1 };
    auto config = Zhttp::H2Config().maxConcurrentStreams(1).maxQueuedFrames(Count);
    ZuCheckRT(wire.initWire(false, config), log_("initialize bounded cleanup harness"));
    ZmRef<FlowAppLink> link = new FlowAppLink;
    uint32_t id = wire.openLocalStream(link)->id;
    ZuCheckRT(wire.peerInitialWindow(0), log_("stall one stream's output"));
    bool queued = true;
    for (unsigned i = 0; i < Count; ++i)
      queued &= wire.sendData(id, flowData(1, id));
    ZuCheckRT(queued && !wire.sends, log_("one stream owns more frames than a turn"));
    app.deferred = true;
    bool done = false;
    if (mode)
      wire.clearStreams([&done]() { done = true; });
    else
      wire.removeStream(id, [&done]() { done = true; });
    ZuCheckRT(app.step(FlowApp::Rx) && app.step(FlowApp::Tx) && !done,
      log_("frame cleanup yields before completing a large single-stream queue"));
    app.runTx([&wire, id]() {
      ZuCheckRT(!wire.sendData<false>(id, flowData(1, id)),
        log_("retiring windows reject late Tx admission"));
    });
    ZuCheckRT(app.step(FlowApp::Tx) && !done && app.step(FlowApp::Tx) && !done &&
        app.step(FlowApp::Rx) && done && !app.step(FlowApp::Tx),
          log_("completion waits for the last cleanup turn and both shard barriers"));
    wire.finalWire();
  }
}

void runRemovalTeardown()
{
  ZuTestScopeRT(runRemovalTeardown);
  enum { Streams = 2, Count = Zhttp::H2_::FrameDrainBatch + 1 };
  FlowApp app;
  FlowWire wire{&app};
  auto config = Zhttp::H2Config().maxConcurrentStreams(Streams)
    .maxQueuedFrames(Streams * Count);
  ZuCheckRT(wire.initWire(false, config), log_("initialize pending-removal teardown"));
  ZuCheckRT(wire.peerInitialWindow(0), log_("stall stream output before removal"));
  ZuArray<uint32_t, Streams> ids;
  bool queued = true;
  for (unsigned i = 0; i < Streams; ++i) {
    ZmRef<FlowAppLink> link = new FlowAppLink;
    uint32_t id = wire.openLocalStream(link)->id;
    ids.push(id);
    for (unsigned n = 0; n < Count; ++n)
      queued &= wire.sendData(id, flowData(1, id));
  }
  ZuCheckRT(queued && !wire.sends, log_("each stream needs bounded removal turns"));
  app.deferred = true;
  unsigned removed = 0, retired = 0;
  bool complete = false, ordered = false, drained = false;
  for (auto id : ids)
    wire.removeStream(id, [&app, &removed, &retired]() {
      ++removed;
      app.txRun([&retired]() { ++retired; });
    });
  wire.clearStreams([&removed, &retired, &complete, &ordered, &drained]() {
    ordered = removed == Streams;
    drained = retired == Streams;
    complete = true;
  });
  while (app.step(FlowApp::Rx) || app.step(FlowApp::Tx)) { }
  ZuCheckRT(complete && removed == Streams && retired == Streams && !wire.streamCount(),
    log_("teardown and both pending removals finish"));
  ZuCheckRT(ordered,
    log_("physical teardown must follow all pending stream-removal callbacks"));
  ZuCheckRT(drained,
    log_("physical teardown must drain Tx work posted by removal callbacks"));
  wire.finalWire();
}

struct FlowFinal : public ZmObject, public ZmNHeap<FlowFinal> {
  friend inline ZuStringT<"Zhttp.H2.FlowFinal"> ZmNHeapID(FlowFinal *);

  Zhttp::Native_::FinalCmd command;
  FlowWire *wire;
  unsigned *emitted, *cancelled, *destroyed;
  uint32_t id;

  FlowFinal(FlowWire *wire_, uint32_t id_,
      unsigned *emitted_, unsigned *cancelled_, unsigned *destroyed_) :
    wire{wire_}, emitted{emitted_}, cancelled{cancelled_},
    destroyed{destroyed_}, id{id_} {
    command.bind(this);
    command.emit = [](void *ctx) {
      auto self = static_cast<FlowFinal *>(ctx);
      auto headers = flowHeaders();
      headers.head().buf->data()[4] = Zhttp::H2::Flag::EndStream |
	Zhttp::H2::Flag::EndHeaders;
      if (self->wire->sendHeaders<false>(self->id, ZuMv(headers), true))
	++*self->emitted;
      else
	++*self->cancelled;
    };
    command.cancel = [](void *ctx) {
      ++*static_cast<FlowFinal *>(ctx)->cancelled;
    };
  }
  ~FlowFinal() { ++*destroyed; }
};

void runFinalQueue()
{
  ZuTestScope(runFinalQueue);
  FlowApp app;
  FlowWire wire{&app};
  ZuCHECK(wire.initWire(false, Zhttp::H2Config()),
    "initialize final section queue harness");
  ZmRef<FlowAppLink> link = new FlowAppLink;
  uint32_t id = wire.openLocalStream(link)->id;
  ZuCHECK(wire.peerInitialWindow(1), "set limited DATA credit");
  ZuCHECK(wire.sendData(id, flowData(3, id)) && wire.dataLength == 1,
    "accepted DATA prefix stalls after a partial send");
  unsigned emitted = 0, cancelled = 0, destroyed = 0;
  ZmRef<FlowFinal> final = new FlowFinal{
    &wire, id, &emitted, &cancelled, &destroyed};
  app.runTx([&]() {
    ZuCHECK(wire.queueFinal(id, &final->command) && !emitted,
      "final command waits behind flow-stalled DATA");
    ZuCHECK(!wire.sendData<false>(id, flowData(1, id)) &&
	!wire.endData<false>(id) &&
	!wire.queueFinal(id, &final->command),
      "terminal admission rejects later DATA, end and final commands");
  });
  final = nullptr;
  ZuCHECK(!destroyed, "native queue retains embedded command allocation");
  unsigned sends = wire.sends;
  wire.h2WindowUpdate(id, 2);
  ZuCHECK(emitted == 1 && !cancelled && destroyed == 1 &&
      wire.sends == sends + 2 && wire.dataLength == 2 &&
      !(wire.dataFlags & Zhttp::H2::Flag::EndStream),
    "DATA suffix precedes final HEADERS without manufactured terminal DATA");

  id = wire.openLocalStream(link)->id;
  ZuCHECK(wire.peerInitialWindow(0) && wire.sendData(id, flowData(1, id)),
    "second stream stalls DATA for cancellation");
  final = new FlowFinal{&wire, id, &emitted, &cancelled, &destroyed};
  app.runTx([&]() {
    ZuCHECK(wire.queueFinal(id, &final->command),
      "second stream admits pending final command");
  });
  final = nullptr;
  wire.clearStreams([]() { });
  ZuCHECK(emitted == 1 && cancelled == 1 && destroyed == 2,
    "teardown cancels and releases a pending embedded command once");
  app.runTx([&]() {
    final = new FlowFinal{&wire, id, &emitted, &cancelled, &destroyed};
    ZuCHECK(!wire.queueFinal(id, &final->command),
      "late final command cannot recreate retired stream state");
    final = nullptr;
  });
  wire.finalWire();
}

void runNegativeWindow()
{
  ZuTestScope(runNegativeWindow);
  FlowApp app;
  FlowWire wire{&app};
  ZuCHECK(wire.initWire(false, Zhttp::H2Config()),
    "initialize negative-window harness");
  ZmRef<FlowAppLink> link = new FlowAppLink;
  auto stream = wire.openLocalStream(link);
  uint32_t id = stream->id;
  ZuCHECK(wire.peerInitialWindow(3), "set initial stream credit");
  auto buf = flowData(7);
  buf->data()[4] = Zhttp::H2::Flag::EndStream;
  memcpy(buf->data() + Zhttp::H2::FrameHeaderSize, "payload", 7);
  auto original = buf;
  ZuCHECK(wire.sendData(id, ZuMv(buf)) && wire.dataLength == 3 &&
      !(wire.dataFlags & Zhttp::H2::Flag::EndStream),
    "queued DATA is split to available credit without ending its prefix");
  ZuCheck(wire.lastBuf && wire.lastBuf->cspan() == "pay");
  unsigned sends = wire.sends;
  ZuCHECK(wire.peerInitialWindow(2) && wire.sends == sends,
    "negative stream window blocks remaining DATA");
  wire.h2WindowUpdate(id, 2);
  ZuCHECK(wire.sends == sends + 1 && wire.dataLength == 1 &&
      !(wire.dataFlags & Zhttp::H2::Flag::EndStream),
    "WINDOW_UPDATE resumes a single byte after repaying negative credit");
  wire.h2WindowUpdate(id, 3);
  ZuCHECK(wire.dataLength == 3 &&
      (wire.dataFlags & Zhttp::H2::Flag::EndStream),
    "final suffix retains END_STREAM after multiple DATA splits");
  ZuCheck(wire.ownedCalls == 3 && wire.lastBuf == original &&
    wire.lastBuf->cspan() == "oad");
  ZuCheck(original->skip == Zhttp::H2::FrameHeaderSize + 4 && original->length == 3);
  wire.clearStreams([]() { });
  app.runTx([&wire, id]() {
    ZuCHECK(!wire.sendData<false>(id, flowData(1)),
      "late Tx work cannot recreate a retired stream window");
  });
  wire.finalWire();
}

void runReadyCredit()
{
  ZuTestScope(runReadyCredit);
  FlowApp app;
  FlowWire wire{&app};
  ZuCHECK(wire.initWire(false, Zhttp::H2Config()),
    "initialize ready-queue flow harness");
  ZmRef<FlowAppLink> first = new FlowAppLink, second = new FlowAppLink;
  uint32_t a = wire.openLocalStream(first)->id;
  uint32_t b = wire.openLocalStream(second)->id;
  ZuCHECK(wire.sendData(a, flowData(16384)) &&
      wire.sendData(a, flowData(16384)) &&
      wire.sendData(a, flowData(16384)) &&
      wire.sendData(a, flowData(16382)),
    "leave one connection-credit byte for two ready streams");
  unsigned sends = wire.sends;
  app.deferred = true;
  ZuCHECK(wire.sendData(a, flowData(1)) && wire.sendData(b, flowData(1, b)) &&
      app.step(FlowApp::Tx) && app.step(FlowApp::Tx) &&
      app.step(FlowApp::Tx) && wire.sends == sends + 1 && wire.capacity,
    "drain rechecks connection credit after another ready stream sends");
  wire.h2WindowUpdate(0, 1);
  ZuCHECK(app.step(FlowApp::Tx) && app.step(FlowApp::Tx) &&
      wire.sends == sends + 2,
    "connection WINDOW_UPDATE resumes the other queued stream");
  app.deferred = false;
  wire.clearStreams([]() { });
  wire.finalWire();
}

void runTeardownDrain()
{
  ZuTestScope(runTeardownDrain);
  FlowApp app;
  FlowWire wire{&app};
  ZuCHECK(wire.initWire(false, Zhttp::H2Config()),
    "initialize asynchronous teardown harness");
  ZmRef<FlowAppLink> link = new FlowAppLink;
  auto stream = wire.openLocalStream(link);
  uint32_t id = stream->id;
  app.deferred = true;
  bool lateRan = false, admitted = true, complete = false;
  app.txRun([&app, &wire, id, &lateRan, &admitted]() {
    app.txRun([&wire, id, &lateRan, &admitted]() {
      lateRan = true;
      admitted = wire.sendData<false>(id, flowData(1));
    });
  });
  wire.clearStreams([&complete]() { complete = true; });
  ZuCHECK(!complete && !wire.canOpenLocalStream(),
    "teardown closes ingress before posting the Rx drain");
  ZuCHECK(app.step(FlowApp::Rx) && !wire.streamCount() && !complete,
    "Rx drain retires stream ownership before Tx cleanup");
  ZuCHECK(app.step(FlowApp::Tx) && app.step(FlowApp::Tx) && !complete,
    "Tx cleanup precedes owner completion");
  ZuCHECK(app.step(FlowApp::Tx) && lateRan && !admitted && !complete,
    "late Tx callback is drained without recreating retired state");
  ZuCHECK(app.step(FlowApp::Tx) && !complete &&
      app.step(FlowApp::Rx) && complete &&
      !app.step(FlowApp::Tx) && !app.step(FlowApp::Rx),
    "teardown acknowledges completion only after dependent callbacks drain");
  wire.finalWire();
}

void runAdmissionErrors()
{
  ZuTestScope(runAdmissionErrors);

  FlowApp app;
  FlowWire wire{&app};
  auto config = Zhttp::H2Config()
    .maxConcurrentStreams(1).maxQueuedFrames(0);
  ZuCHECK(wire.initWire(false, config),
    "initialize strict-shard H2 admission harness");
  ZmRef<FlowAppLink> appLink = new FlowAppLink;
  auto stream = wire.openLocalStream(appLink);
  ZuCHECK(stream, "open strict-shard H2 stream");
  if (!stream) {
    wire.finalWire();
    return;
  }
  uint32_t id = stream->id;

  unsigned handled = 0;
  wire.appTxErrorFn(id, ZiTxErrorFn{
    [&app, &handled](ZeException &) {
      ZuCHECK(app.txInvoked(), "direct-Tx error callback runs on Tx shard");
      ++handled;
      return true;
    }});
  app.runTx([&wire, id]() {
    ZuCHECK(!wire.sendFrame<false>(id, flowData(0)),
      "direct-Tx admission rejects a frame at zero capacity");
  });
  ZuCHECK(handled == 1 && !wire.disconnects,
    "handled direct-Tx admission error does not disconnect");

  unsigned replaced = 0;
  wire.appTxErrorFn(id, ZiTxErrorFn{
    [&app, &replaced](ZeException &) {
      ZuCHECK(app.txInvoked(), "replacement callback runs on Tx shard");
      ++replaced;
      return false;
    }});
  app.runTx([&wire, id]() {
    wire.sendHeaders<false>(id, flowHeaders(), false);
  });
  ZuCHECK(replaced == 1 && wire.disconnects == 1 &&
      wire.disconnectShard == FlowApp::Tx,
    "unhandled direct-Tx header rejection disconnects on Tx shard");

  wire.appTxErrorFn(id, {});
  app.runTx([&wire, id]() {
    ZuCHECK(!wire.sendFrame<false>(id, flowData(0)),
      "cleared callback leaves direct-Tx rejection unhandled");
  });
  ZuCHECK(replaced == 1 && wire.disconnects == 1,
    "callback clearing reaches the existing Tx window");

  unsigned rxHandled = 0;
  wire.appTxErrorFn(id, ZiTxErrorFn{
    [&app, &rxHandled](ZeException &) {
      ZuCHECK(app.rxInvoked(), "Rx admission callback runs on Rx shard");
      ++rxHandled;
      return true;
    }});
  ZuCHECK(!wire.sendFrame(id, flowData(0)),
    "Rx admission rejects a frame at zero capacity");
  ZuCHECK(rxHandled == 1 && wire.disconnects == 1,
    "Rx rejection uses the Rx-owned callback copy");

  bool cleared = false;
  wire.clearStreams([&cleared]() { cleared = true; });
  ZuCHECK(cleared, "clear strict-shard H2 admission harness");
  wire.finalWire();
}

struct RequestBuilder :
  public Zhttp::Builder,
  public Zhttp::H2::Request<RequestBuilder> {
  void fields(Zhttp::FieldOutput &out) {
    using B = ZuDecay<decltype(*this)>;
    Zhttp::BuilderFields<B> adapter{this, fieldCatalog};
    adapter.fields(out);
  }
  const Zhttp::CatalogView *fieldCatalog =
    &Zhttp::CatalogData<HdrCatalog>::configure();
  template <typename Key> void headerOffset(uint64_t, unsigned) { }
  bool streamResponse() const { return false; }

  using HdrCatalog = Zhttp::DefltHdrCatalog;
  template <typename L>
  void operation(L &&l) {
    l(Zhttp::Method::GET, [](auto &&emit) {
      emit([](auto &tx) { tx << '/'; });
    });
  }
  template <typename L>
  void host(L &&l) { l("127.0.0.1"); }
};

struct StreamRequestBuilder :
  public Zhttp::Builder,
  public Zhttp::H2::Request<StreamRequestBuilder, true> {
  void fields(Zhttp::FieldOutput &out) {
    using B = ZuDecay<decltype(*this)>;
    Zhttp::BuilderFields<B> adapter{this, fieldCatalog};
    adapter.fields(out);
  }
  const Zhttp::CatalogView *fieldCatalog =
    &Zhttp::CatalogData<HdrCatalog>::configure();
  template <typename Key> void headerOffset(uint64_t, unsigned) { }
  bool streamResponse() const { return false; }

  using HdrCatalog = Zhttp::DefltHdrCatalog;
  template <typename L>
  void operation(L &&l) {
    l(Zhttp::Method::CONNECT, [](auto &&emit) {
      emit([](auto &tx) { tx << "/stream"; });
    });
  }
  template <typename L>
  void host(L &&l) { l("127.0.0.1"); }
  template <typename L>
  void protocol(L &&l) { l("opaque"); }
};

struct ResponseBuilder :
  public Zhttp::Builder,
  public Zhttp::H2::Response<
    ResponseBuilder, true> {
  void fields(Zhttp::FieldOutput &out) {
    using B = ZuDecay<decltype(*this)>;
    Zhttp::BuilderFields<B> adapter{this, fieldCatalog};
    adapter.fields(out);
  }
  const Zhttp::CatalogView *fieldCatalog =
    &Zhttp::CatalogData<HdrCatalog>::configure();
  template <typename Key> void headerOffset(uint64_t, unsigned) { }
  bool streamResponse() const { return false; }

  using HdrCatalog = ResponseHeaders;
  using Base = Zhttp::H2::Response<ResponseBuilder, true>;
  using Base::body;
  unsigned status() { return 200; }
  template <typename Key, typename L>
  void header(L &&l) {
    if constexpr (Key{}() == "content-length") l("4");
  }
  template <typename L> void header(L &&) { }
};

struct StreamResponseBuilder :
  public Zhttp::Builder,
  public Zhttp::H2::Response<StreamResponseBuilder> {
  void fields(Zhttp::FieldOutput &out) {
    using B = ZuDecay<decltype(*this)>;
    Zhttp::BuilderFields<B> adapter{this, fieldCatalog};
    adapter.fields(out);
  }
  const Zhttp::CatalogView *fieldCatalog =
    &Zhttp::CatalogData<HdrCatalog>::configure();
  template <typename Key> void headerOffset(uint64_t, unsigned) { }

  using HdrCatalog = Zhttp::DefltHdrCatalog;
  using Base = Zhttp::H2::Response<StreamResponseBuilder>;
  using Base::body;
  unsigned status() { return 200; }
  bool streamResponse() { return true; }
};

struct Client;
struct ClientLink;
struct ClientParser;

struct ClientStream {
  template <typename Stream, typename Rx>
  int process(Stream stream, Rx &rx);

  ClientParser	*parser = nullptr;
};

struct ClientParser :
  public Zhttp::Parser,
  public Zhttp::H2::Parser<ClientParser, false> {
  const Zhttp::CatalogView &catalog() const { return *fieldCatalog; }
  void header(Zhttp::FieldSection::T section, int id,
      ZuBSpan name, ZuSpan<uint8_t> value) {
    int fixed = id < 0 ? -1 : fieldCatalog->valueMatch(id, value);
    Zhttp::parserHeader(*this, section, id, fixed, name, value);
  }
  const Zhttp::CatalogView *fieldCatalog =
    &Zhttp::CatalogData<HdrCatalog>::configure();

  using HdrCatalog = Zhttp::DefltHdrCatalog;
  ClientParser() : consumer{this} { }

  void bind(ClientLink &link) { dispatch.init(link, consumer); }
  void final() {
    dispatch.disable_();
    dispatch.final_();
  }
  bool operation(Zhttp::Method::T, Zhttp::Target &) { return true; }
  void status(unsigned value) { status_ = value; }
  template <typename Key>
  void header(Zhttp::FieldSection::T, ZuSpan<uint8_t>) { }
  template <typename Rx>
  bool body(Rx &rx) {
    ++bodyCalls;
    return Zhttp::bodyEach(
      rx, [this](ZuSpan<uint8_t> value) { body_ << value; });
  }
  void complete(Zhttp::H2::ParserState::T value) { complete_ = value; }
  bool bodyInfo(Zhttp::BodyType::T type, uint64_t length_) {
    if (type == Zhttp::BodyType::Fixed) length = length_;
    if (streamExpected && status_ >= 200 && status_ < 300 &&
	type != Zhttp::BodyType::None) {
      stream();
      ++streamEstablished;
      ++streamStarts;
    }
    return true;
  }
  template <typename Rx>
  void streamRx(Rx &rx) { dispatch.process(rx); }
  void streamPeerEnd() { ++streamEnds; dispatch.peerEnd(); }
  void streamError_() { ++streamResets; dispatch.error(); }
  template <typename Rx>
  void processStream(Rx &rx) {
    while (rx) {
      const uint8_t *offered = nullptr;
      int64_t n = rx.consume(
	  [&offered](ZuSpan<uint8_t> span) -> int64_t {
	    offered = span.data();
	    return 1;
	  },
	  [this, &offered](ZuSpan<uint8_t> span) {
	    streamNoCopy &= span.data() == offered;
	    streamBody << span;
	  });
      if (n <= 0) break;
    }
  }

  ZtString<>			body_;
  ZtString<>			streamBody;
  Zhttp::StreamDispatch<ClientLink, ClientStream> dispatch;
  ClientStream			consumer;
  uint64_t			length = 0;
  unsigned			status_ = 0;
  unsigned			bodyCalls = 0;
  unsigned			streamEstablished = 0;
  unsigned			streamStarts = 0;
  unsigned			streamEnds = 0;
  unsigned			streamResets = 0;
  bool				streamNoCopy = true;
  bool				streamExpected = false;
  Zhttp::H2::ParserState::T	complete_ =
    Zhttp::H2::ParserState::Initial;
};

template <typename Stream, typename Rx>
int ClientStream::process(Stream, Rx &rx)
{
  parser->processStream(rx);
  return 1;
}

struct Client : public Zhttp::ClientHub<Client, Zhttp::H2TLS> {
  using Link = ClientLink;
  State *state = nullptr;

  Client(State *state_) : state{state_} { }

  void connected(Link &link, Zhttp::ConnectedInfo info);
  void disconnected(Link &link, bool);
  void connectFailed(Link &, bool) {
    ++state->errors;
    state->response.post();
  }
  int process(Link &link, Zhttp::H2_::EventRx &rx);
};

struct ClientLink :
  public Zhttp::ClientLink<Client, ClientLink, Zhttp::H2TLS> {
  using Base =
    Zhttp::ClientLink<Client, ClientLink, Zhttp::H2TLS>;
  using Base::Base;

  ClientParser parser;
  unsigned	id = 0;
  unsigned	disconnects = 0;
  bool		streamSent = false;
  bool		holdOpen = false;

  template <typename Stream, typename Rx>
  int process(Stream stream, Rx &rx) {
    return Zhttp::Link_::process(*this->app(), ZuMv(stream), rx, 0);
  }
  template <typename Stream> void peerEnd(Stream stream) {
    Zhttp::Link_::peerEnd(*this->app(), ZuMv(stream), 0);
  }
  template <typename Stream> void error(Stream stream) {
    Zhttp::Link_::error(*this->app(), ZuMv(stream), 0);
  }
};

void Client::disconnected(Link &link, bool)
{
  link.parser.final();
  ++link.disconnects;
  ++state->closed;
}

void Client::connected(Link &link, Zhttp::ConnectedInfo info)
{
  if (info.httpVersion != Zhttp::Version::H2 ||
      info.transport != Zhttp::Transport::TLS ||
      info.alpn != "h2") {
    ++state->errors;
    state->response.post();
    return;
  }
  link.parser.bind(link);
  this->txRun([
    link = &link, state = state, stream = link.parser.streamExpected]() {
    auto tx = link->txStream_();
    if (stream) {
      StreamRequestBuilder builder;
      if (!builder.begin(tx)) {
	++state->errors;
	state->response.post();
	link->app()->rxRun([link]() { link->disconnect(); });
      }
    } else {
      RequestBuilder builder;
      builder.begin(tx);
      builder.finish(tx);
    }
  });
}

int Client::process(Link &link, Zhttp::H2_::EventRx &rx)
{
  auto state_ = link.receive(link.parser, rx);
  if (state_ == Zhttp::H2::ParserState::Error) {
    ++state->errors;
    state->response.post();
    return -1;
  }
  if (link.parser.streamExpected) {
    if (state_ == Zhttp::H2::ParserState::Stream &&
	!link.streamSent) {
      link.streamSent = true;
      if (link.holdOpen) {
	state->response.post();
	return 0;
      }
      Zhttp::Stream stream{link};
      stream.txStream([](auto &body) {
	body << ZuCSpan{"ping"};
	body.flush();
      });
      stream.end();
    }
    if (state_ == Zhttp::H2::ParserState::RemoteClosed) {
      if (link.parser.streamEstablished != 1 ||
	  link.parser.streamStarts != 1 ||
	  link.parser.streamBody != "ping" ||
	  !link.parser.streamNoCopy ||
	  link.parser.streamEnds != 1 || link.parser.streamResets)
	++state->errors;
      state->response.post();
    }
    return 0;
  }
  if (state_ != Zhttp::H2::ParserState::Complete) return 0;
  if (link.parser.status_ != 200 || link.parser.length != 4 ||
      link.parser.body_ != "pong" || link.parser.bodyCalls != 4)
    ++state->errors;
  state->response.post();
  return 1;
}

struct Server;
struct ServerLink;

struct ServerParser;

struct ServerStream {
  template <typename Stream, typename Rx>
  int process(Stream stream, Rx &rx);

  ServerParser	*parser = nullptr;
};

struct ServerParser :
  public Zhttp::Parser,
  public Zhttp::H2::Parser<ServerParser, true> {
  const Zhttp::CatalogView &catalog() const { return *fieldCatalog; }
  void header(Zhttp::FieldSection::T section, int id,
      ZuBSpan name, ZuSpan<uint8_t> value) {
    int fixed = id < 0 ? -1 : fieldCatalog->valueMatch(id, value);
    Zhttp::parserHeader(*this, section, id, fixed, name, value);
  }
  const Zhttp::CatalogView *fieldCatalog =
    &Zhttp::CatalogData<HdrCatalog>::configure();

  using HdrCatalog = Zhttp::DefltHdrCatalog;
  ServerParser() : consumer{this} { }

  void bind(ServerLink &link) { dispatch.init(link, consumer); }
  void final() {
    dispatch.disable_();
    dispatch.final_();
  }
  bool operation(
    Zhttp::Method::T method_, Zhttp::Target &target) {
    method = method_;
    path = target.path;
    protocol_ = target.protocol;
    return true;
  }
  bool bodyInfo(Zhttp::BodyType::T type, uint64_t) {
    if (method == Zhttp::Method::CONNECT && protocol_ &&
	type != Zhttp::BodyType::None) {
      stream();
      ++streamEstablished;
      ++streamStarts;
    }
    return true;
  }
  void status(unsigned) { }
  template <typename Key>
  void header(Zhttp::FieldSection::T, ZuSpan<uint8_t>) { }
  template <typename Rx>
  bool body(Rx &rx) { return Zhttp::bodyDrain(rx); }
  template <typename Rx>
  void streamRx(Rx &rx) { dispatch.process(rx); }
  void streamPeerEnd() { remoteEnded = true; dispatch.peerEnd(); }
  void streamError_() { ++streamResets; dispatch.error(); }
  template <typename Rx>
  void processStream(Rx &rx) {
    while (rx) {
      const uint8_t *offered = nullptr;
      int64_t n = rx.consume(
	  [&offered](ZuSpan<uint8_t> span) -> int64_t {
	    offered = span.data();
	    return 1;
	  },
	  [this, &offered](ZuSpan<uint8_t> span) {
	    streamNoCopy &= span.data() == offered;
	    streamData << span;
	  });
      if (n <= 0) break;
    }
  }
  void complete(Zhttp::H2::ParserState::T value) { complete_ = value; }

  ZtString<>			path;
  ZtString<>			protocol_;
  ZtString<>			streamData;
  Zhttp::StreamDispatch<ServerLink, ServerStream> dispatch;
  ServerStream			consumer;
  Zhttp::Method::T		method = -1;
  Zhttp::H2::ParserState::T	complete_ =
    Zhttp::H2::ParserState::Initial;
  unsigned			streamEstablished = 0;
  unsigned			streamStarts = 0;
  unsigned			streamResets = 0;
  bool				remoteEnded = false;
  bool				streamNoCopy = true;
};

template <typename Stream, typename Rx>
int ServerStream::process(Stream, Rx &rx)
{
  parser->processStream(rx);
  return 1;
}

struct ServerSession {
  ServerParser parser;

  template <typename Link>
  void connected(Link &link) {
    connected_ = true;
    if constexpr (ZuIsSame<ZuDecay<Link>, ServerLink>{})
      parser.bind(link);
    parser.extendedConnect(Zhttp::Stream{link}.localCap());
  }
  template <typename Link>
  void disconnected(Link &, bool) {
    if constexpr (ZuIsSame<ZuDecay<Link>, ServerLink>{})
      parser.final();
  }
  template <typename Link>
  int process(Link &link, Zhttp::H2_::EventRx &rx) {
    if (!connected_) ++link.app()->state->errors;
    parser.streamData.null();
    parser.remoteEnded = false;
    auto state = link.receive(parser, rx);
    if (state == Zhttp::H2::ParserState::Error) return -1;
    if (state == Zhttp::H2::ParserState::Stream ||
	state == Zhttp::H2::ParserState::RemoteClosed) {
      if (!parser.streamNoCopy || parser.streamStarts != 1)
	++link.app()->state->errors;
      if (!streamResponse_) {
	streamResponse_ = true;
	link.app()->txRun([link = &link]() {
	  auto tx = link->txStream_();
	  StreamResponseBuilder builder;
	  builder.begin(tx);
	});
      }
      if (parser.streamData) {
	Zhttp::Stream{link}.txStream([this](auto &body) {
	  body << parser.streamData;
	  body.flush();
	});
      }
      if (parser.remoteEnded) Zhttp::Stream{link}.end();
      return 0;
    }
    if (state != Zhttp::H2::ParserState::Complete) return 0;
    if (parser.method != Zhttp::Method::GET || parser.path != "/")
      ++link.app()->state->errors;
    link.app()->txRun([link = &link]() {
      auto tx = link->txStream_();
      ResponseBuilder builder;
      builder.begin(tx);
      auto body = builder.body(tx);
      body << ZuCSpan{"pong"};
      body.flush();
      builder.finish(tx);
    });
    return 1;
  }

  bool connected_ = false;
  bool streamResponse_ = false;
};

struct Server : public Zhttp::ProtocolServer<Server, Zhttp::H2TLS, ServerLink> {
  using Link = ServerLink;
  State *state = nullptr;

  Server(State *state_) : state{state_} { }

  ZiIP localIP() const { return ZiIP{"127.0.0.1"}; }
  unsigned localPort() const { return state->port; }
  ZuTime idleTimeout() const { return ZuTime{0}; }
  void listening(const ZiListenInfo &) { state->listening.post(); }
  void listenFailed(bool) {
    ++state->errors;
    state->listening.post();
  }
  bool admit(const ZiCxnInfo &) {
    ++state->admissions;
    return true;
  }
  void release() { ++state->releases; }
  void disconnected(Link &, bool) { ++state->closed; }
};

struct ServerLink :
  public Zhttp::ServerLink<
    Server, ServerLink, Zhttp::H2TLS, ServerSession> {
  using Base = Zhttp::ServerLink<
    Server, ServerLink, Zhttp::H2TLS, ServerSession>;
  using Base::Base;

  template <typename Stream, typename Rx>
  int process(Stream stream, Rx &rx) {
    return Zhttp::Link_::process(this->rxState(), ZuMv(stream), rx, 0);
  }
  template <typename Stream> void peerEnd(Stream stream) {
    Zhttp::Link_::peerEnd(this->rxState(), ZuMv(stream), 0);
  }
  template <typename Stream> void error(Stream stream) {
    Zhttp::Link_::error(this->rxState(), ZuMv(stream), 0);
  }
};

struct H1Client;
struct H1ClientLink;

struct H1Client : public Zhttp::ClientHub<H1Client, Zhttp::H1TLS> {
  using Link = H1ClientLink;
  State *state = nullptr;

  H1Client(State *state_) : state{state_} { }

  void connected(Link &link, Zhttp::ConnectedInfo info);
  void disconnected(Link &, bool) { ++state->closed; }
  void connectFailed(Link &, bool) {
    ++state->errors;
    state->response.post();
  }
  int process(Link &, Ztls::RxStream &) { return -1; }
};

struct H1ClientLink :
  public Zhttp::ClientLink<H1Client, H1ClientLink, Zhttp::H1TLS> {
  using Base =
    Zhttp::ClientLink<H1Client, H1ClientLink, Zhttp::H1TLS>;
  using Base::Base;

  template <typename Stream, typename Rx>
  int process(Stream stream, Rx &rx) {
    return Zhttp::Link_::process(*this->app(), ZuMv(stream), rx, 0);
  }
  template <typename Stream> void peerEnd(Stream stream) {
    Zhttp::Link_::peerEnd(*this->app(), ZuMv(stream), 0);
  }
  template <typename Stream> void error(Stream stream) {
    Zhttp::Link_::error(*this->app(), ZuMv(stream), 0);
  }
  template <typename Rx> int process(Rx &rx) { return Base::process(rx); }
};

struct MismatchClient;
struct MismatchLink;

struct MismatchClient :
  public Zhttp::ClientHub<MismatchClient, Zhttp::H2TLS> {
  using Link = MismatchLink;
  State *state = nullptr;

  MismatchClient(State *state_) : state{state_} { }

  void connected(Link &, Zhttp::ConnectedInfo) {
    ++state->errors;
    state->response.post();
  }
  void disconnected(Link &, bool) { ++state->errors; }
  void connectFailed(Link &link, bool);
  int process(Link &, Zhttp::H2_::EventRx &) {
    ++state->errors;
    return -1;
  }
};

struct MismatchLink :
  public Zhttp::ClientLink<
    MismatchClient, MismatchLink, Zhttp::H2TLS> {
  using Base = Zhttp::ClientLink<
    MismatchClient, MismatchLink, Zhttp::H2TLS>;
  using Base::Base;

  unsigned id = 0;

  template <typename Stream, typename Rx>
  int process(Stream stream, Rx &rx) {
    return Zhttp::Link_::process(*this->app(), ZuMv(stream), rx, 0);
  }
  template <typename Stream> void peerEnd(Stream stream) {
    Zhttp::Link_::peerEnd(*this->app(), ZuMv(stream), 0);
  }
  template <typename Stream> void error(Stream stream) {
    Zhttp::Link_::error(*this->app(), ZuMv(stream), 0);
  }
};

void MismatchClient::connectFailed(Link &link, bool)
{
  if (link.result() != Zhttp::ResultCode::Unprocessed)
    ++state->errors;
  state->response.post();
}

void H1Client::connected(Link &link, Zhttp::ConnectedInfo info)
{
  if (info.httpVersion != Zhttp::Version::H1 ||
      info.transport != Zhttp::Transport::TLS ||
      info.alpn != "http/1.1")
    ++state->errors;
  state->response.post();
  link.disconnect();
}

struct SharedServer;
struct SharedH1Link;
struct SharedH2Link;
struct SharedClient;
struct SharedClientH1Link;
struct SharedClientH2Link;

struct SharedH1Session {
  template <typename Link> void connected(Link &) { }
  template <typename Link> void disconnected(Link &, bool) { }
  template <typename Link>
  int process(Link &, Ztls::RxStream &) { return -1; }
};

struct SharedServer : public Zhttp::TLS_::ServerHub<SharedServer> {
  using H1Link = SharedH1Link;
  using H2Link = SharedH2Link;
  State *state = nullptr;

  SharedServer(State *state_) : state{state_} { }

  ZiIP localIP() const { return ZiIP{"127.0.0.1"}; }
  unsigned localPort() const { return state->port; }
  ZuTime idleTimeout() const { return ZuTime{0}; }
  void listening(const ZiListenInfo &) { state->listening.post(); }
  void listenFailed(bool) {
    ++state->errors;
    state->listening.post();
  }
  bool admit(const ZiCxnInfo &) {
    ++state->admissions;
    return true;
  }
  void release() { ++state->releases; }
  template <typename Link>
  void connected(Link &, Zhttp::ConnectedInfo info) {
    if (info.transport != Zhttp::Transport::TLS ||
	(info.httpVersion != Zhttp::Version::H1 &&
	 info.httpVersion != Zhttp::Version::H2))
      ++state->errors;
    if constexpr (ZuIsSame<Link, H1Link>{}) state->h1Connected.post();
  }
  template <typename Link>
  void disconnected(Link &, bool) { ++state->closed; }
};

struct SharedH1Link :
  public Zhttp::TLS_::ServerH1AppLink<
    SharedServer, SharedH1Link, SharedH1Session,
    Zhttp::TLS_::SrvLink<SharedServer>> {
  using Base = Zhttp::TLS_::ServerH1AppLink<
    SharedServer, SharedH1Link, SharedH1Session,
    Zhttp::TLS_::SrvLink<SharedServer>>;
  using Base::Base;

  template <typename Stream, typename Rx>
  int process(Stream stream, Rx &rx) {
    return Zhttp::Link_::process(this->rxState(), ZuMv(stream), rx, 0);
  }
  template <typename Stream> void peerEnd(Stream stream) {
    Zhttp::Link_::peerEnd(this->rxState(), ZuMv(stream), 0);
  }
  template <typename Stream> void error(Stream stream) {
    Zhttp::Link_::error(this->rxState(), ZuMv(stream), 0);
  }
};

struct SharedH2Link :
  public Zhttp::H2_::ServerAppLink<
    SharedServer, SharedH2Link, ServerSession,
    Zhttp::TLS_::SrvLink<SharedServer>> {
  using Base = Zhttp::H2_::ServerAppLink<
    SharedServer, SharedH2Link, ServerSession,
    Zhttp::TLS_::SrvLink<SharedServer>>;
  using Base::Base;
};

template <typename H1, typename H2>
auto sharedChoice(ZmRef<H1> h1, ZmRef<H2> h2)
{
  return [h1 = ZuMv(h1), h2 = ZuMv(h2)](int8_t) mutable {
    return Zhttp::TLS_::ClientChoice<H1, H2>{ZuMv(h1), ZuMv(h2)};
  };
}

struct SharedClient :
  public Zhttp::TLS_::ClientHub<
    SharedClient, SharedClientH1Link, SharedClientH2Link> {
  using Base = Zhttp::TLS_::ClientHub<
    SharedClient, SharedClientH1Link, SharedClientH2Link>;
  using NativeLink = Base::Link;
  using H1Link = SharedClientH1Link;
  using H2Link = SharedClientH2Link;
  State *state = nullptr;

  SharedClient(State *state_) : state{state_} { }

  void connected(H1Link &link, Zhttp::ConnectedInfo info);
  void connected(H2Link &link, Zhttp::ConnectedInfo info);
  template <typename Link>
  void disconnected(Link &, bool) { ++state->closed; }
  template <typename Link>
  void connectFailed(Link &, bool) {
    ++state->errors;
    state->response.post();
  }
  template <typename Native>
  void nativeUp(
      unsigned, uint64_t, const ZmRef<Native> &) {
    if (++state->clientNative > state->maxClientNative)
      state->maxClientNative = state->clientNative;
  }
  void nativeDown(unsigned, uint64_t) {
    ZmAssert(state->clientNative);
    --state->clientNative;
  }
  void goaway(uint32_t) { state->goaway.post(); }
  int process(H1Link &, Ztls::RxStream &) { return -1; }
  int process(H2Link &link, Zhttp::H2_::EventRx &rx);
};

struct SharedClientH1Link :
  public Zhttp::TLS_::ClientH1AppLink<
    SharedClient, SharedClientH1Link, SharedClient::NativeLink> {
  using Base = Zhttp::TLS_::ClientH1AppLink<
    SharedClient, SharedClientH1Link, SharedClient::NativeLink>;
  using Base::Base;

  template <typename Stream, typename Rx>
  int process(Stream stream, Rx &rx) {
    return Zhttp::Link_::process(*this->app(), ZuMv(stream), rx, 0);
  }
  template <typename Stream> void peerEnd(Stream stream) {
    Zhttp::Link_::peerEnd(*this->app(), ZuMv(stream), 0);
  }
  template <typename Stream> void error(Stream stream) {
    Zhttp::Link_::error(*this->app(), ZuMv(stream), 0);
  }
};

struct SharedClientH2Link :
  public Zhttp::H2_::ClientAppLink<
    SharedClient, SharedClientH2Link, SharedClient::NativeLink> {
  using Base = Zhttp::H2_::ClientAppLink<
    SharedClient, SharedClientH2Link, SharedClient::NativeLink>;
  using Base::Base;

  ClientParser parser;
};

void SharedClient::connected(
  H1Link &link, Zhttp::ConnectedInfo info)
{
  if (info.httpVersion != Zhttp::Version::H1 ||
      info.transport != Zhttp::Transport::TLS ||
      info.alpn != "http/1.1")
    ++state->errors;
  link.complete([state = state]() { state->response.post(); });
}

void SharedClient::connected(
  H2Link &link, Zhttp::ConnectedInfo info)
{
  if (info.httpVersion != Zhttp::Version::H2 ||
      info.transport != Zhttp::Transport::TLS ||
      info.alpn != "h2") {
    ++state->errors;
    state->response.post();
    return;
  }
  this->txRun([link = &link]() {
    auto tx = link->txStream_();
    RequestBuilder builder;
    builder.begin(tx);
    builder.finish(tx);
  });
}

int SharedClient::process(
  H2Link &link, Zhttp::H2_::EventRx &rx)
{
  auto state_ = rx.process(link.parser);
  if (state_ == Zhttp::H2::ParserState::Error) {
    ++state->errors;
    state->response.post();
    return -1;
  }
  if (state_ != Zhttp::H2::ParserState::Complete) return 0;
  if (link.parser.status_ != 200 || link.parser.length != 4 ||
      link.parser.body_ != "pong" || link.parser.bodyCalls != 4)
    ++state->errors;
  state->response.post();
  return 1;
}

ZiMxParams mxParams()
{
  return ZiMxParams()
    .scheduler([](auto &s) {
      s.nThreads(4)
	.thread(1, [](auto &t) { t.isolated(1); })
	.thread(2, [](auto &t) { t.isolated(1); })
	.thread(3, [](auto &t) { t.isolated(1); })
	.thread(4, [](auto &t) { t.isolated(1); });
    })
    .rxThread(1).txThread(2);
}

void run()
{
  ZuTestScope(run);

  Zhttp::Test::TempDir cert;
  bool certOK = cert.init();
  ZuCHECK(certOK, "certificate fixture failed");
  if (!certOK) return;
  State state;
  state.port = Zhttp::Test::loopbackPort(ZhttpTestPort::H2Hub);
  ZuCHECK(state.port, "port allocation failed");
  if (!state.port) return;

  ZiMultiplex mx{mxParams()};

  Zhttp::HubConfig hub{&mx, "3", "4"};
  auto serverConfig = Zhttp::H2Config().tls([&cert](auto &tls) {
    tls.certPath(cert.certPath).keyPath(cert.keyPath);
  })
    .maxConcurrentStreams(1).maxQueuedFrames(16);
  auto clientConfig = Zhttp::H2Config().tls([&cert](auto &tls) {
    tls.caPath(cert.certPath);
  }).initialWindowSize(1)
    .maxConcurrentStreams(1);
  Server server{&state};
  Client client{&state};
  bool initialized =
    server.init(hub, serverConfig) && client.init(hub, clientConfig);
  ZuCHECK(initialized, "H2 hubs initialized");
  if (!initialized) {
    client.final();
    server.final();
    mx.stop();
    return;
  }
  ZuCHECK(mx.start(), "multiplexer start failed");
  if (!mx.running()) return;
  bool started = server.start() && client.start();
  ZuCHECK(started, "H2 hubs started");
  bool listening =
    state.listening.timedwait(Zm::now(10)) == 0;
  ZuCHECK(listening, "H2 server listening");

  ZmRef<ClientLink> link1 = new ClientLink{&client};
  ZmRef<ClientLink> link2 = new ClientLink{&client};
  if (listening) {
    link1->connect("127.0.0.1", state.port);
    link2->connect("127.0.0.1", state.port);
  }
  bool response =
    state.response.timedwait(Zm::now(10)) == 0 &&
    state.response.timedwait(Zm::now(10)) == 0;
  ZuCHECK(response && !state.errors && state.admissions == 1,
    "two logical requests shared one H2 TLS session");

  client.stop([&state](bool ok) {
    if (!ok) ++state.errors;
    state.stopped.post();
  });
  client.stop([&state](bool ok) {
    if (!ok) ++state.errors;
    state.stopped.post();
  });
  server.stop([&state](bool ok) {
    if (!ok || state.releases != 1) ++state.errors;
    state.stopped.post();
  });
  server.stop([&state](bool ok) {
    if (!ok || state.releases != 1) ++state.errors;
    state.stopped.post();
  });
  bool stopped =
    state.stopped.timedwait(Zm::now(10)) == 0 &&
    state.stopped.timedwait(Zm::now(10)) == 0 &&
    state.stopped.timedwait(Zm::now(10)) == 0 &&
    state.stopped.timedwait(Zm::now(10)) == 0;
  ZuCHECK(stopped && !state.errors && state.closed == 4 &&
      state.releases == 1,
    "H2 asynchronous stop follows logical disconnect and admission release");
  client.final();
  server.final();
  mx.stop();
}

void runStream()
{
  ZuTestScope(runStream);

  Zhttp::Test::TempDir cert;
  bool certOK = cert.init();
  ZuCHECK(certOK, "certificate fixture failed");
  if (!certOK) return;
  State state;
  state.port = Zhttp::Test::loopbackPort(ZhttpTestPort::H2Hub + 1);
  ZuCHECK(state.port, "port allocation failed");
  if (!state.port) return;

  ZiMultiplex mx{mxParams()};

  Zhttp::HubConfig hub{&mx, "3", "4"};
  auto serverConfig = Zhttp::H2Config().tls([&cert](auto &tls) {
    tls.certPath(cert.certPath).keyPath(cert.keyPath);
  })
    .extendedConnect(true).maxConcurrentStreams(4)
    .initialWindowSize(1);
  auto clientConfig = Zhttp::H2Config().tls([&cert](auto &tls) {
    tls.caPath(cert.certPath);
  })
    .extendedConnect(true).maxConcurrentStreams(4)
    .initialWindowSize(1);
  Server server{&state};
  Client client{&state};
  bool initialized =
    server.init(hub, serverConfig) && client.init(hub, clientConfig);
  ZuCHECK(initialized, "Extended CONNECT hubs initialized");
  if (!initialized) {
    client.final();
    server.final();
    mx.stop();
    return;
  }
  ZuCHECK(mx.start(), "multiplexer start failed");
  if (!mx.running()) return;
  bool started = server.start() && client.start();
  ZuCHECK(started, "Extended CONNECT hubs started");
  bool listening =
    state.listening.timedwait(Zm::now(10)) == 0;
  ZuCHECK(listening, "Extended CONNECT server listening");

  ZmRef<ClientLink> rest = new ClientLink{&client};
  ZmRef<ClientLink> stream1 = new ClientLink{&client};
  ZmRef<ClientLink> stream2 = new ClientLink{&client};
  ZmRef<ClientLink> active = new ClientLink{&client};
  stream1->parser.streamExpected = true;
  stream2->parser.streamExpected = true;
  active->parser.streamExpected = true;
  active->holdOpen = true;
  if (listening) {
    rest->connect("127.0.0.1", state.port);
    stream1->connect("127.0.0.1", state.port);
    stream2->connect("127.0.0.1", state.port);
    active->connect("127.0.0.1", state.port);
  }
  bool responses =
    state.response.timedwait(Zm::now(10)) == 0 &&
    state.response.timedwait(Zm::now(10)) == 0 &&
    state.response.timedwait(Zm::now(10)) == 0 &&
    state.response.timedwait(Zm::now(10)) == 0;
  ZuCHECK(responses && !state.errors && state.admissions == 1,
    "REST and three flow-controlled streams share one H2 connection");

  client.stop([&state](bool ok) {
    if (!ok) ++state.errors;
    state.stopped.post();
  });
  server.stop([&state](bool ok) {
    if (!ok || state.releases != 1) ++state.errors;
    state.stopped.post();
  });
  bool stopped =
    state.stopped.timedwait(Zm::now(10)) == 0 &&
    state.stopped.timedwait(Zm::now(10)) == 0;
  ZuCHECK(stopped && !state.errors && state.releases == 1 &&
      active->disconnects == 1,
    "active stream disconnects once before physical stop completion");
  client.final();
  server.final();
  mx.stop();
}

void runSharedTLS()
{
  ZuTestScope(runSharedTLS);

  Zhttp::Test::TempDir cert;
  bool certOK = cert.init();
  ZuCHECK(certOK, "certificate fixture failed");
  if (!certOK) return;
  State state;
  state.port = Zhttp::Test::loopbackPort(ZhttpTestPort::H2Hub + 2);
  ZuCHECK(state.port, "port allocation failed");
  if (!state.port) return;

  ZiMultiplex mx{mxParams()};

  Zhttp::HubConfig hub{&mx, "3", "4"};
  auto serverConfig = Zhttp::H2Config().tls([&cert](auto &tls) {
    tls.certPath(cert.certPath).keyPath(cert.keyPath);
  })
    .policy(Zhttp::H2Policy::Prefer);
  auto h2Config = Zhttp::H2Config().tls([&cert](auto &tls) {
    tls.caPath(cert.certPath);
  }).initialWindowSize(1)
    .maxStreamID(3)
    .policy(Zhttp::H2Policy::Prefer);
  auto h1Config = Zhttp::H2Config().tls([&cert](auto &tls) {
    tls.caPath(cert.certPath);
  })
    .policy(Zhttp::H2Policy::Disable);
  SharedServer server{&state};
  SharedClient h2Client{&state};
  SharedClient h1Client{&state};
  bool initialized =
    server.init(hub, serverConfig) &&
    h2Client.init(hub, h2Config) &&
    h1Client.init(hub, h1Config);
  ZuCHECK(initialized, "shared TLS hubs initialized");
  if (!initialized) {
    h1Client.final();
    h2Client.final();
    server.final();
    mx.stop();
    return;
  }
  ZuCHECK(mx.start(), "multiplexer start failed");
  if (!mx.running()) return;
  bool started =
    server.start() && h2Client.start() && h1Client.start();
  ZuCHECK(started, "shared TLS hubs started");
  bool listening =
    state.listening.timedwait(Zm::now(10)) == 0;
  ZuCHECK(listening, "shared TLS server listening");

  ZmRef<SharedClientH1Link> h2H1 =
    new SharedClientH1Link{&h2Client};
  ZmRef<SharedClientH2Link> h2H2 =
    new SharedClientH2Link{&h2Client};
  ZmRef<SharedClientH1Link> h2H1_2 =
    new SharedClientH1Link{&h2Client};
  ZmRef<SharedClientH2Link> h2H2_2 =
    new SharedClientH2Link{&h2Client};
  ZmRef<SharedClientH1Link> h2H1_3 =
    new SharedClientH1Link{&h2Client};
  ZmRef<SharedClientH2Link> h2H2_3 =
    new SharedClientH2Link{&h2Client};
  ZmRef<SharedClientH1Link> h2H1_4 =
    new SharedClientH1Link{&h2Client};
  ZmRef<SharedClientH2Link> h2H2_4 =
    new SharedClientH2Link{&h2Client};
  ZmRef<SharedClientH1Link> h1H1 =
    new SharedClientH1Link{&h1Client};
  ZmRef<SharedClientH2Link> h1H2 =
    new SharedClientH2Link{&h1Client};
  ZmRef<SharedClientH1Link> h1H1_2 =
    new SharedClientH1Link{&h1Client};
  ZmRef<SharedClientH2Link> h1H2_2 =
    new SharedClientH2Link{&h1Client};
  if (listening)
    h2Client.connect(
      sharedChoice(h2H1, h2H2), Ztls::Host{"127.0.0.1"}, state.port, 0);
  bool firstH2 =
    state.response.timedwait(Zm::now(10)) == 0;
  ZuCHECK(firstH2 && !state.errors,
    "prefer-H2 client completed its first logical request");
  if (firstH2)
    server.drain([&state]() { state.goaway.post(); });
  bool drained =
    state.goaway.timedwait(Zm::now(10)) == 0 &&
    state.goaway.timedwait(Zm::now(10)) == 0;
  ZuCHECK(drained, "client observed server GOAWAY drain");
  if (drained)
    h2Client.connect(
      sharedChoice(h2H1_2, h2H2_2), Ztls::Host{"127.0.0.1"}, state.port, 0);
  bool secondH2 =
    state.response.timedwait(Zm::now(10)) == 0;
  ZuCHECK(secondH2 && !state.errors && state.admissions == 2,
    "post-GOAWAY H2 request used a replacement TLS connection");
  ZuCHECK(state.maxClientNative == 1,
    "post-GOAWAY replacement waits for the prior connection to close");
  if (secondH2)
    h2Client.connect(
      sharedChoice(h2H1_3, h2H2_3), Ztls::Host{"127.0.0.1"}, state.port, 0);
  // Keep the first connection below exhaustion until GOAWAY is observed.
  // The replacement then consumes IDs1 and3 before the next request rotates it.
  bool exhaustedH2 =
    state.response.timedwait(Zm::now(10)) == 0;
  ZuCHECK(exhaustedH2 && !state.errors && state.admissions == 2,
    "replacement H2 connection accepts its final available stream ID");
  if (exhaustedH2)
    h2Client.connect(
      sharedChoice(h2H1_4, h2H2_4), Ztls::Host{"127.0.0.1"}, state.port, 0);
  bool thirdH2 =
    state.response.timedwait(Zm::now(10)) == 0;
  ZuCHECK(thirdH2 && !state.errors && state.admissions == 3,
    "stream-ID exhaustion rotated the H2 TLS connection");
  ZuCHECK(state.maxClientNative == 1,
    "stream-ID replacement preserves one live connection per link");
  if (thirdH2)
    h1Client.connect(
      sharedChoice(h1H1, h1H2), Ztls::Host{"127.0.0.1"}, state.port, 0);
  if (thirdH2)
    h1Client.connect(
      sharedChoice(h1H1_2, h1H2_2), Ztls::Host{"127.0.0.1"}, state.port, 0);
  bool h1Responses =
    state.response.timedwait(Zm::now(10)) == 0 &&
    state.response.timedwait(Zm::now(10)) == 0;
  bool h1Connected = state.h1Connected.timedwait(Zm::now(10)) == 0;
  ZuCHECK(h1Responses && h1Connected,
    "overlapping H1 operations and server TLS setup completed before shutdown");
  ZuCHECK(!state.errors, "shared TLS message paths reported no errors");
  ZuCHECK(state.admissions == 4,
    "shared TLS listener admitted rotated H2 plus H1 connections");
  ZuCHECK(!h1H1->active() && !h1H1_2->active() &&
      !state.errors && state.admissions == 4,
    "overlapping H1 operations retire without closing their TLS link");

  h1Client.stop([&state](bool ok) {
    if (!ok) ++state.errors;
    state.stopped.post();
  });
  h2Client.stop([&state](bool ok) {
    if (!ok) ++state.errors;
    state.stopped.post();
  });
  server.stop([&state](bool ok) {
    if (!ok || state.releases != 4) ++state.errors;
    state.stopped.post();
  });
  bool stopped =
    state.stopped.timedwait(Zm::now(10)) == 0 &&
    state.stopped.timedwait(Zm::now(10)) == 0 &&
    state.stopped.timedwait(Zm::now(10)) == 0;
  ZuCHECK(stopped, "shared TLS stop continuations completed");
  ZuCHECK(!state.errors, "shared TLS stop reported no errors");
  ZuCHECK(state.closed == 9 &&
      !h1H1->active() && !h1H1_2->active(),
    "shared TLS stop disconnected H2 links and released H1 operations (closed=",
    state.closed.load_(), ", active=", h1H1->active(), ',',
    h1H1_2->active(), ')');
  ZuCHECK(state.releases == 4,
    "shared TLS stop released every physical admission");
  h1Client.final();
  h2Client.final();
  server.final();
  mx.stop();
}

void runForceMismatch()
{
  ZuTestScope(runForceMismatch);

  Zhttp::Test::TempDir cert;
  bool certOK = cert.init();
  ZuCHECK(certOK, "certificate fixture failed");
  if (!certOK) return;
  State state;
  state.port = Zhttp::Test::loopbackPort(ZhttpTestPort::H2Hub + 3);
  ZuCHECK(state.port, "port allocation failed");
  if (!state.port) return;

  ZiMultiplex mx{mxParams()};

  Zhttp::HubConfig hub{&mx, "3", "4"};
  auto serverConfig = Zhttp::H2Config().tls([&cert](auto &tls) {
    tls.certPath(cert.certPath).keyPath(cert.keyPath);
  })
    .policy(Zhttp::H2Policy::Disable);
  auto clientConfig = Zhttp::H2Config().tls([&cert](auto &tls) {
    tls.caPath(cert.certPath);
  });
  SharedServer server{&state};
  MismatchClient client{&state};
  bool initialized =
    server.init(hub, serverConfig) &&
    client.init(hub, clientConfig);
  ZuCHECK(initialized, "mismatch hubs initialized");
  if (!initialized) {
    client.final();
    server.final();
    mx.stop();
    return;
  }
  ZuCHECK(mx.start(), "multiplexer start failed");
  if (!mx.running()) return;
  bool started = server.start() && client.start();
  ZuCHECK(started, "mismatch hubs started");
  bool listening =
    state.listening.timedwait(Zm::now(10)) == 0;
  ZuCHECK(listening, "H1-only TLS server listening");

  ZmRef<MismatchLink> link = new MismatchLink{&client};
  if (listening) link->connect("127.0.0.1", state.port);
  bool failed = state.response.timedwait(Zm::now(10)) == 0;
  ZuCHECK(failed && !state.errors,
    "force-H2 mismatch failed before an HTTP logical link opened");

  client.stop([&state](bool ok) {
    if (!ok) ++state.errors;
    state.stopped.post();
  });
  server.stop([&state](bool ok) {
    if (!ok || state.releases != 1) ++state.errors;
    state.stopped.post();
  });
  bool stopped =
    state.stopped.timedwait(Zm::now(10)) == 0 &&
    state.stopped.timedwait(Zm::now(10)) == 0;
  ZuCHECK(stopped && !state.errors && !state.closed &&
      state.admissions == 1 && state.releases == 1,
    "failed ALPN drained one physical admission without logical callbacks");
  client.final();
  server.final();
  mx.stop();
}

} // namespace ZhttpH2HubTest_

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZiTestResidue::init("ZhttpH2HubTest");
  ZiLog::init("ZhttpH2HubTest");
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();
  ZuTestMain();
  ZuTestCall(ZhttpH2HubTest_::runH1Shards);
  ZuTestCall(ZhttpH2HubTest_::runInitialControlFailure);
  ZuTestCall(ZhttpH2HubTest_::runFlowCapacity);
  ZuTestCall(ZhttpH2HubTest_::runBlockedRequeue);
  ZuTestCall(ZhttpH2HubTest_::runWindowChanges);
  ZuTestCall(ZhttpH2HubTest_::runConnectionOverflow);
  ZuTestCall(ZhttpH2HubTest_::runHeaderTurns);
  ZuTestCall(ZhttpH2HubTest_::runBoundedFrameCleanup);
  ZuTestCall(ZhttpH2HubTest_::runRemovalTeardown);
  ZuTestCall(ZhttpH2HubTest_::runNegativeWindow);
  ZuTestCall(ZhttpH2HubTest_::runFinalQueue);
  ZuTestCall(ZhttpH2HubTest_::runTeardownDrain);
  ZuTestCall(ZhttpH2HubTest_::runReadyCredit);
  ZuTestCall(ZhttpH2HubTest_::runAdmissionErrors);
  ZuTestCall(ZhttpH2HubTest_::run);
  ZuTestCall(ZhttpH2HubTest_::runStream);
  ZuTestCall(ZhttpH2HubTest_::runSharedTLS);
  ZuTestCall(ZhttpH2HubTest_::runForceMismatch);
  ZiLog::stop();
}
