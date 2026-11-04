//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <iostream>
#include <stdlib.h>
#ifdef __linux__
#include <errno.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <unistd.h>
#endif

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmAtomic.hh>
#include <zlib/ZmFn.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmRef.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZtArray.hh>
#include <zlib/ZiMultiplex.hh>
#include <zlib/ZiRx.hh>
#include <zlib/ZiTx.hh>
#include <zlib/ZfixIOBuf.hh>
#include <zlib/ZtlsHMAC.hh>
#ifdef ZFIX_TEST_TLS
#include <zlib/Ztls.hh>
namespace Net = Ztls;
#else
#include <zlib/Ztcp.hh>
namespace Net = Ztcp;
#endif
#include <zlib/ZfixRxFramer.hh>
#include <zlib/ZfixTxMsg.hh>
#include <zlib/ZfixWire.hh>
#include "../test/ZfixTxImageTest.hh"

#ifdef ZFIX_TEST_TLS
using RxAllocator = Zi::RxTLSBufAllocator<ZiRxBuf, Ztls_::TxRecordCapacity,
  ZiIOBuf_DefltMaxSize, ZuStringT<"Zfix.Transport.Rx">>;
using TxAllocator = Zi::TxTLSBufAllocator<Zfix::WireBuf, Ztls_::TxRecordCapacity,
  ZiIOBuf_DefltMaxSize, ZuStringT<"Zfix.Transport.Tx">>;
#else
using RxAllocator = Zi::RxTCPBufAllocator<ZiRxBuf, ZiIOBuf_DefltSize,
  ZiIOBuf_DefltMaxSize, ZuStringT<"Zfix.Transport.Rx">>;
using TxAllocator = Zi::TxTCPBufAllocator<Zfix::WireBuf, ZiIOBuf_DefltSize,
  ZiIOBuf_DefltMaxSize, ZuStringT<"Zfix.Transport.Tx">>;
#endif

using LocalBuf = Zi::IOBufAlloc<ZiIOBuf, ZiIOBuf_DefltSize,
  ZiIOBuf_DefltMaxSize, ZiIOBuf_HeapID>;

using namespace ZuTestUtil;

constexpr auto Wire = "8=FIX.4.4\1" "9=13\1" "35=D\1" "58=ABCD\1" "10=155\1"_z;
struct Payload {
  ZuBSpan text, bytes, signature;
};
ZfStruct(, (Payload, FIX),
  (text, (FIX::ID<58>, FIX::Opt), String),
  (bytes, (FIX::ID<212>, FIX::Binary<213>, FIX::Opt), Bytes),
  (signature, ((FIX::Sig<93, 89>)), Bytes));
ZfStructImpl(Payload); ZfStructImpl(Payload, FIX);
using Packet = Zfix::MT<Payload, "D">;
using Signature = ZfField(Payload, signature);

struct PacketSigner {
  enum { SignMsg = 1 };
  template <typename S, typename Message>
  void signMsg(S &out, const Zfix::TxBody *, Zfix::TxContext, const Message &message) const {
    Ztls::HMAC<> mac;
    mac.start(ZuBSpan{"transport test"}); message.each([&mac](ZuCSpan span) { mac.update(ZuBSpan{span}); });
    uint8_t digest[Ztls::HMAC<>::Size]; // fixed by SHA-256
    mac.finish(digest); Zfix::Fmt::saveBinary<Signature>(out, digest);
  }
};
static ZmRef<ZiIOBuf> makePacket(unsigned payload) {
  if (!payload) {
    ZmRef<ZiIOBuf> buf = new LocalBuf{nullptr, 0}; buf->append(Wire); return buf;
  }
  Zfix::TxFrame frame{"FIX.4.4"}; auto &out = frame.body();
  out.field(35); out << 'D'; out.end();
  out.field(212); out << ZuBox<unsigned>{payload}; out.end();
  out.field(213);
  for (unsigned i = 0; i < payload; ++i) out << char(uint8_t(i));
  out.end();
  Zfix::Fmt::Error error;
  return testBytes(frame.finish(error, PacketSigner{}, nullptr, {}));
}

// Validate application bytes after native TCP/TLS extraction. Both borrowed
// spans must address this exact extracted allocation, including across echo.
static bool checkPacket(ZuBSpan buffer, unsigned expected, Payload &object) {
  auto parsed = Zfix::Fmt::frame(buffer);
  if (parsed.state != Zfix::Fmt::FrameState::Complete ||
      Zfix::Fmt::load(object, parsed.view.content)) return false;
  if (!expected) return object.text == ZuBSpan{"ABCD"} && !object.bytes.length();
  if (object.bytes.length() != expected || object.signature.length() != Ztls::HMAC<>::Size)
    return false;
  Zfix::Fmt::FieldPos data, signature, length;
  if (Zfix::Fmt::inspect<Payload>(parsed.view.content, 213, data) ||
      Zfix::Fmt::inspect<Payload>(parsed.view.content, 89, signature) ||
      Zfix::Fmt::inspect<Payload>(parsed.view.content, 93, length) ||
      data.offset == UINT_MAX || signature.offset == UINT_MAX || length.offset == UINT_MAX)
    return false;
  if (object.bytes.data() != reinterpret_cast<const uint8_t *>(parsed.view.content.data() +
        data.offset) || object.signature.data() != reinterpret_cast<const uint8_t *>(
        parsed.view.content.data() + signature.offset)) return false;
  auto first = parsed.view.msgType.data() - 3; // fixed 35=
  auto last = parsed.view.content.data() + length.offset - 3; // application 93=
  Ztls::HMAC<> mac;
  mac.start(ZuBSpan{"transport test"});
  mac.update(ZuBSpan{ZuCSpan{first, uint64_t(last - first)}});
  uint8_t digest[Ztls::HMAC<>::Size]; mac.finish(digest);
  return object.signature == ZuBSpan{digest};
}
struct TestState {
  ZmSemaphore		listening, done, drained, ready, blocked, held, release, peerReset;
  ZmAtomic<unsigned>	errors{0}, received{0}, closed{0}, completed{0}, failed{0};
  ZmAtomic<unsigned>	watch{0}, shortWrites{0}, wouldBlock{0};
  ZmAtomic<unsigned>	holds{0}, socketErrors{0};
  ZmAtomic<int>		holdSocket{-1};
  bool			partial = false, abort = false;
  unsigned		port = 0;
  unsigned		sessions = 1;
  unsigned		payload = 0;
  ZmAtomic<unsigned>	binaryViews{0}, retainedViews{0};
  Zi::RxRange		packet;

  void fail(unsigned line = __builtin_LINE()) {
    log_("# transport failure line=", line); errors = 1; done.post();
  }

  void disconnected() {
    if (closed.xchAdd(1) + 1 == 2 * sessions) drained.post();
  }
};

#ifdef __linux__
// Test-side ELF interposition observes real kernel results, without changing
// requested lengths or manufacturing EAGAIN. The pointer is published before
// worker creation and cleared only after all workers have stopped.
static TestState *sendState = nullptr;
extern "C" __attribute__((visibility("default")))
ssize_t send(int fd, const void *buf, size_t len, int flags)
{
  auto state = sendState;
  bool watch = state && state->watch.load_();
  bool held = state && fd == state->holdSocket.load_() && !state->holds.xch(1);
  if (held) { state->held.post(); state->release.wait(); }
  ssize_t n = syscall(SYS_sendto, fd, buf, len, flags, nullptr, 0);
  int error = errno;
  if (held && n < 0 && (error == ECONNRESET || error == EPIPE)) ++state->socketErrors;
  if (watch) {
    if (n > 0 && size_t(n) < len) ++state->shortWrites;
    if (n < 0 && (error == EAGAIN || error == EWOULDBLOCK)) {
      if (!state->wouldBlock.xchAdd(1)) state->blocked.post();
    }
  }
  errno = error; return n;
}

extern "C" __attribute__((visibility("default")))
ssize_t recv(int fd, void *buf, size_t len, int flags)
{
  ssize_t n = syscall(SYS_recvfrom, fd, buf, len, flags, nullptr, nullptr);
  int error = errno;
  auto state = sendState;
  if (state && fd == state->holdSocket.load_() && n < 0 && error == ECONNRESET)
    state->peerReset.post();
  errno = error; return n;
}
#endif

#ifdef ZFIX_TEST_TLS
inline constexpr unsigned FrameMode = Zfix::WireMode::Records;
template <typename A, typename I, typename T> using FrameCli = Net::CliLink<A, I, T>;
template <typename A, typename I, typename T> using FrameSrv = Net::SrvLink<A, I, T>;
#else
inline constexpr unsigned FrameMode = Zfix::WireMode::Pending;
template <typename A, typename I, typename T>
using FrameCli = Net::CliLink<A, I, T, Zfix::WireOps>;
template <typename A, typename I, typename T>
using FrameSrv = Net::SrvLink<A, I, T, Zfix::WireOps>;
#endif

template <typename Link>
struct Output : public Zfix::WireTx<Output<Link>, FrameMode> {
  using Base = Zfix::WireTx<Output<Link>, FrameMode>;
  Link		*link;
  uint64_t	seq = 0, finished = 0;
  bool closing = false, closed = false;

  explicit Output(Link *link_) : link{link_} { }

  void open() {
    link->app()->txRun([this] {
      Zfix::WireGeometry geometry;
#ifdef ZFIX_TEST_TLS
      geometry.size = link->txMaxSize();
      geometry.head = link->template txHeadRoom<false>();
      geometry.tail = link->template txTailRoom<false>();
      geometry.memory = link->allocWire_(geometry.head)->memory();
#endif
      if (!Base::open_(1, geometry)) link->app()->state->fail();
    });
  }

  void send_(ZmRef<ZiIOBuf> packet) {
    unsigned length = packet->length;
    send_(Zi::RxRange{ZuMv(packet), 0, length});
  }
  void send_(Zi::RxRange packet) {
    auto view = Zfix::Fmt::frame(ZuCSpan{packet.cspan()}).view;
    Zfix::Fmt::Error error;
    Payload object;
    error = Zfix::Fmt::load(object, view.content);
    auto body = error ? Zfix::TxBody::Ref{} : Zfix::TxBody::encode<Packet>(object, ++seq, error);
    // The fixture packet remains unchanged; TLS encrypts record copies.
    if (error || !Base::admit_(ZuMv(body), {1, 1, seq}, ZuMv(packet))) {
      link->app()->state->fail(); return;
    }
    // A managed endpoint posts LinkTx's Rx publication notice here.
    Base::flush_();
  }

  ZmRef<Zfix::WireBuf> recordAlloc(unsigned head) {
    auto buf = link->allocWire_(head);
    return buf;
  }

  bool recordStart(ZmRef<Zfix::WireQueue::Node> node) {
    return link->startFrame_(ZuMv(node));
  }
  void recordDoneIO(ZmRef<Zfix::WireQueue::Node> node, unsigned part, bool ok) {
    link->app()->txRun([node = ZuMv(node), part, ok]() mutable {
      auto owner = static_cast<Output *>(node->owner);
      owner->Base::record_(node->token, part, ok);
      owner->Base::ack_(node); owner->closed_();
    });
  }
  void recordAckIO(ZmRef<Zfix::WireQueue::Node> node) {
    link->app()->txRun([held = ZuMv(node)] {
	auto node = held.ptr();
      auto owner = static_cast<Output *>(node->owner);
      owner->Base::ack_(ZmRef<Zfix::WireQueue::Node>{node}); owner->closed_();
    });
  }

  bool recordSend(ZmRef<ZiIOBuf> buf) { return link->sendRecord_(ZuMv(buf)); }
  bool recordSendRange(Zi::RxRange source, ZmRef<Zfix::WireBuf> wire) {
    return link->sendWire_(ZuMv(source), ZuMv(wire));
  }
  void recordRun(uint64_t connection, uint64_t token) {
    link->app()->txRun([this, connection, token] { Base::resume_(connection, token); });
  }


  void outputDone(Zfix::TxBody::Ref, Zfix::TxContext context, bool ok) {
    link->app()->txRun([this, context, ok] {
      if (!link->app()->txInvoked() || (!ok
	&& !link->app()->state->abort)) link->app()->state->fail();
      if (context.epoch != 1 || context.connection != 1 || context.seqNo != ++finished)
	link->app()->state->fail();
      if (!ok) ++link->app()->state->failed;
      ++link->app()->state->completed;
      closed_();
    });
  }

  void sent(ZmRef<ZiTxBuf> buf, bool ok) {
    auto record = static_cast<Zfix::WireBuf *>(buf.ptr());
    if (record->token) link->app()->txRun([
      this, token = record->token, part = record->part, ok
    ] { Base::record_(token, part, ok); });
  }

  void closed_() {
    if (!closing || closed || Base::count_()) return;
    closed = true;
    link->app()->txRun([link = link] {
      link->app()->rxRun([link] { link->app()->state->disconnected(); });
    });
  }
  void close() {
    link->app()->txRun([this] {
      closing = true; Base::close_(1); closed_();
    });
  }

};

template <typename Link>
static void sendFrames(Link *link) {
  link->app()->txRun([link] {
    for (unsigned i = 0; i < 2; ++i) {
      auto packet = makePacket(link->app()->state->payload);
      unsigned length = packet->length;
      link->output.send_(Zi::RxRange{ZuMv(packet), 0, length});
    }
  });
}
struct Client : public Net::Client<Client> {
  struct Link;
  TestState	*state;

  explicit Client(TestState *s) : state{s} { }

  unsigned reconnFreq() const { return 0; }
};

struct Client::Link : public ZmNHeap<Link>, public FrameCli<Client, Link, TxAllocator> {
friend inline ZuStringT<"Zfix.transport_test.Link"> ZmNHeapID(Link *);
  using ZmNHeap<Link>::operator new;
  using ZmNHeap<Link>::operator delete;
  using Base = FrameCli<Client, Link, TxAllocator>;
  Output<Link>		output{this};
  Zfix::RxFramer	frame;

  explicit Link(Client *app) : Base{app, ZuID{"fixClient"}} { }

  void connected(Net::Connected) {
    frame.reset_(); output.open();
    if (app()->state->partial) app()->state->ready.post();
    else sendFrames(this);
  }

  void disconnected(bool) {
    if (!this->txQueue.empty_()) app()->state->fail();
    frame.reset_(); output.close();
  }

  int process(Net::RxStream &stream) {
    if (!app()->rxInvoked()) { app()->state->fail(); return -1; }
    Zi::RxRange buf;
    int n = frame.next_(stream, [this] {
	auto allocator = app()->mx()->rxAllocator(false);
	return ZmRef<ZiRxQueue::Node>::acquire(static_cast<ZiRxQueue::Node *>(
	  allocator->alloc(this).release()));
      }, buf);
    if (n < 0) { app()->state->fail(); return -1; }
    if (!n) return 0;
    if (ZuCSpan{buf.cspan()} != ZuCSpan{app()->state->packet.cspan()}) { app()->state->fail();
      return -1; }
    Payload object;
    if (!checkPacket(buf.cspan(), app()->state->payload, object)) { app()->state->fail(); return -1; }
    if (app()->state->payload) ++app()->state->binaryViews;
    if (app()->state->received.xchAdd(1) + 1 == 2 * app()->state->sessions)
      app()->state->done.post();
    return 1;
  }

#ifdef ZFIX_TEST_TLS
  bool sendRecord_(ZmRef<ZiIOBuf> buf) { return Base::send_(ZuMv(buf)); }
  bool sendWire_(Zfix::TxImage::View source, ZmRef<Zfix::WireBuf> wire) {
    return Base::protectRecord_(source, ZuMv(wire));
  }
#else
  bool startFrame_(ZmRef<Zfix::WireQueue::Node> node) {
    if (node->frame.metadata.ptr() == app()->state->packet.buf.ptr()) {
      app()->state->fail(); return false;
    }
    auto bytes = node->frame.length();
    return Base::sendPending_(ZuMv(node), bytes);
  }
#endif
  ZmRef<Zfix::WireBuf> allocWire_(unsigned head) {
#ifdef ZFIX_TEST_TLS
    auto buf = this->template txAllocBuf<false>(head);
#else
    auto buf = this->template allocTxBuf_<false>(); buf->skip = head;
#endif
    return ZmRef<Zfix::WireBuf>::acquire(
      static_cast<Zfix::WireBuf *>(ZuMv(buf).release()));
  }
  bool sendWire_(Zi::RxRange source, ZmRef<Zfix::WireBuf> wire) {
#ifdef ZFIX_TEST_TLS
    return Base::protectRecord_(ZuMv(source), ZuMv(wire));
#else
    return false;
#endif
  }


  void sent(ZmRef<ZiTxBuf> buf, bool ok) {
    output.sent(buf, ok); Base::sent(ZuMv(buf), ok);
  }
};

struct Server : public Net::Server<Server> {
  struct Link;
  using Links = ZtArray<ZmRef<Link>, ZtArrayHeapID<"Zfix.TransportTest.SrvLinks">>;
  TestState	*state;
  Links		links;

  explicit Server(TestState *s) : state{s} { links.size(s->sessions); }

  ZiConnection *accepted(const ZiCxnInfo &);

  ZiIP localIP() const { return ZiIP{"127.0.0.1"}; }
  unsigned localPort() const { return 0; }
  unsigned nAccepts() const { return 1; }

  void listening(const ZiListenInfo &info) { state->port = info.port; state->listening.post(); }

  void listenFailed(bool) { state->fail(); state->listening.post(); }
};

struct Server::Link : public ZmNHeap<Link>, public FrameSrv<Server, Link, TxAllocator> {
friend inline ZuStringT<"Zfix.transport_test.Link"> ZmNHeapID(Link *);
  using ZmNHeap<Link>::operator new;
  using ZmNHeap<Link>::operator delete;
  using Base = FrameSrv<Server, Link, TxAllocator>;
  Output<Link>		output{this};
  Zfix::RxFramer	frame;

  explicit Link(Server *app) : Base{app} { }

  void connected(Net::Connected) {
    frame.reset_(); output.open();
    if (app()->state->partial) app()->state->ready.post();
  }

  void disconnected(bool) {
    if (!this->txQueue.empty_()) app()->state->fail();
    frame.reset_(); output.close();
  }

  int process(Net::RxStream &stream) {
    if (!app()->rxInvoked()) { app()->state->fail(); return -1; }
    Zi::RxRange buf;
    int n = frame.next_(stream, [this] {
	auto allocator = app()->mx()->rxAllocator(false);
	return ZmRef<ZiRxQueue::Node>::acquire(static_cast<ZiRxQueue::Node *>(
	  allocator->alloc(this).release()));
      }, buf);
    if (n < 0) { app()->state->fail(); return -1; }
    if (!n) return 0;
    if (ZuCSpan{buf.cspan()} != ZuCSpan{app()->state->packet.cspan()}) { app()->state->fail();
      return -1; }
    Payload object;
    if (!checkPacket(buf.cspan(), app()->state->payload, object)) { app()->state->fail(); return -1; }
    if (app()->state->payload) ++app()->state->binaryViews;
    // Preserve the extracted buffer across the Rx -> Tx handoff.
    app()->txRun([this, buf = ZuMv(buf), bytes = object.bytes, signature = object.signature] {
      if (app()->state->payload) {
	Payload retained;
	if (!checkPacket(buf.cspan(), app()->state->payload, retained) ||
	    retained.bytes.data() != bytes.data() ||
	    retained.signature.data() != signature.data()) { app()->state->fail(); return; }
	++app()->state->retainedViews;
      }
      output.send_(ZuMv(buf));
    });
    return 1;
  }

#ifdef ZFIX_TEST_TLS
  bool sendRecord_(ZmRef<ZiIOBuf> buf) { return Base::send_(ZuMv(buf)); }
  bool sendWire_(Zfix::TxImage::View source, ZmRef<Zfix::WireBuf> wire) {
    return Base::protectRecord_(source, ZuMv(wire));
  }
#else
  bool startFrame_(ZmRef<Zfix::WireQueue::Node> node) {
    auto bytes = node->frame.length();
    return Base::sendPending_(ZuMv(node), bytes);
  }
#endif
  ZmRef<Zfix::WireBuf> allocWire_(unsigned head) {
#ifdef ZFIX_TEST_TLS
    auto buf = this->template txAllocBuf<false>(head);
#else
    auto buf = this->template allocTxBuf_<false>(); buf->skip = head;
#endif
    return ZmRef<Zfix::WireBuf>::acquire(
      static_cast<Zfix::WireBuf *>(ZuMv(buf).release()));
  }
  bool sendWire_(Zi::RxRange source, ZmRef<Zfix::WireBuf> wire) {
#ifdef ZFIX_TEST_TLS
    return Base::protectRecord_(ZuMv(source), ZuMv(wire));
#else
    return false;
#endif
  }


  void sent(ZmRef<ZiTxBuf> buf, bool ok) {
    output.sent(buf, ok); Base::sent(ZuMv(buf), ok);
  }
};

ZiConnection *Server::accepted(const ZiCxnInfo &ci) {
  ZmRef<Link> link = new Link{this}; links.push(link);
  return new Link::Cxn{ZuMv(link), ci};
}
static void loopback(const char *name, unsigned sessions, unsigned payload = 0,
  bool partial = false, bool abort = false, bool nativePartial = false) {
  ZuTestScopeRT(name);
  TestState state; state.sessions = sessions;
  auto packet = makePacket(payload);
  auto packetLength = packet->length;
  state.packet = {ZuMv(packet), 0, packetLength};
  state.partial = partial; state.abort = abort; state.payload = payload;
  ZuCheckRT(state.packet); if (!state.packet) return;
#ifdef __linux__
  sendState = &state;
#endif
  Server server{&state}; Client client{&state};
  // Both ends share Rx backing and each retains two complete frames while
  // echoing. Cover that burst in transport-sized slots, plus the normal reserve.
  // Native partial cases deliberately use two Rx payload slots: the kernel
  // cannot drain complete frames while the I/O Rx callback is held. Gathering
  // frames larger than the pool returns slots as the final buffer grows.
#ifdef ZFIX_TEST_TLS
  constexpr unsigned RxBytes = Ztls_::TxRecordCapacity;
#else
  constexpr unsigned RxBytes = ZiIOBuf_DefltSize;
#endif
  uint64_t slots = 4 * uint64_t(sessions) *
    ((uint64_t(packetLength) + RxBytes - 1) / RxBytes);
  ZiMxParams mxParams;
  uint64_t rxSlots = uint64_t(mxParams.rxRingSize()) + slots;
  uint64_t txSlots = uint64_t(mxParams.txRingSize()) + slots;
  if (rxSlots > ZiMxParams::MaxRingSize) rxSlots = ZiMxParams::MaxRingSize;
  if (txSlots > ZiMxParams::MaxRingSize) txSlots = ZiMxParams::MaxRingSize;
  mxParams.rxRingSize(unsigned(rxSlots)).txRingSize(unsigned(txSlots));
  // Partial cases observe libc send results through the executable's interposer.
  ZiMultiplex mx{ZuMv(mxParams)
    .backend(nativePartial ? ZiMxBackend::URing :
      partial ? ZiMxBackend::EPoll : ZiMxBackend::Auto)
    .tcpBufAllocator(new RxAllocator{nativePartial ? 2U : mxParams.rxRingSize()})
    .scheduler([](auto &s) {
    s.nThreads(4)
      .thread(1, [](auto &t) { t.isolated(1); })
      .thread(2, [](auto &t) { t.isolated(1); })
      .thread(3, [](auto &t) { t.isolated(1); })
      .thread(4, [](auto &t) { t.isolated(1); });
  }).rxThread(1).txThread(2).rxBufSize(nativePartial ? 64 * 1024 : partial ? 4096 : 0)
    .txBufSize(partial ? 4096 : 0)};
  bool started = mx.start(); ZuCheckRT(started); if (!started) return;
  if (nativePartial) {
    ZuCheckRT(mx.backend() == ZiMxBackend::URing);
    ZuCheckRT(!state.packet.buf->uring());
#if Zi_URing
    auto ring = mx.rxAllocator(false)->ring();
    ZuCheckRT(ring && ring->bufCount() == 2);
    ZuCheckRT(ring && packetLength > uint64_t(ring->bufCount()) * ring->bufSize());
#endif
  }
  Net::ServerParams sp{&mx, "3", "4"};
  Net::ClientParams cp{&mx, "3", "4"};
#ifdef ZFIX_TEST_TLS
  const char *cert = getenv("ZFIX_TEST_CERT"), *key = getenv("ZFIX_TEST_KEY");
  if (!cert || !key) { ZuCheckRT(cert && key); mx.stop(); return; }
  sp.certPath(cert).keyPath(key); cp.caPath(cert);
#endif
  bool si = server.init(ZuMv(sp)), ci = client.init(ZuMv(cp));
  ZuCheckRT(si && ci);
  bool ss = si && server.start(), cs = ci && client.start();
  ZuCheckRT(ss && cs);
  ZtArray<ZmRef<Client::Link>, ZtArrayHeapID<"Zfix.TransportTest">> links;
  if (ss && cs) {
    server.listen();
    bool listening = state.listening.timedwait(Zm::now(5)) == 0;
    ZuCheckRT(listening && state.port);
    if (listening && state.port) {
      for (unsigned i = 0; i < sessions; ++i) {
	ZmRef<Client::Link> link = new Client::Link{&client};
#ifdef ZFIX_TEST_TLS
	link->connect("localhost", state.port, ZiIP{"127.0.0.1"});
#else
	link->connect("127.0.0.1", state.port);
#endif
	links.push(ZuMv(link));
      }
      if (partial) {
	for (unsigned i = 0; i < 2 * sessions; ++i)
	  ZuCheckRT(state.ready.timedwait(Zm::now(5)) == 0);
	ZmSemaphore held, release;
	// Application Rx/Tx keep running while native receive is held. With a
	// small kernel send buffer, the actual write must become incomplete.
	mx.run([&held, &release] { held.post(); release.wait(); }, 1);
	ZuCheckRT(held.timedwait(Zm::now(5)) == 0);
	if (nativePartial) {
	  for (const auto &link : links) sendFrames(link.ptr());
	  // All App Tx submissions and their physical queue installers precede
	  // this fence. A partial cursor alone must not stand in for two
	  // actual independent message admissions per link.
	  ZmBlock<>{}([&client, &mx](auto wake) {
	    client.txRun([&mx, wake = ZuMv(wake)]() mutable {
	      mx.run([wake = ZuMv(wake)]() mutable { wake(); }, 2);
	    });
	  });
	  // Observe independent operation cursors on I/O Tx. Retained backing
	  // descriptors keep their published geometry throughout a send.
	  struct Cursor { unsigned progress, active; };
	  Cursor observed{};
	  auto deadline = Zm::now(5);
	  do {
	    observed = ZmBlock<Cursor>{}([
#ifndef ZFIX_TEST_TLS
	      &links,
#endif
	      &mx
	    ](auto wake) {
	      mx.run([
#ifndef ZFIX_TEST_TLS
		&links,
#endif
		wake = ZuMv(wake)
	      ]() mutable {
		Cursor value{};
#ifndef ZFIX_TEST_TLS
		for (const auto &link : links) {
		  auto cxn = link->cxn();
		  if (!cxn) continue;
		  cxn->send_(ZiTxFn{[&value](ZiTxContext &io) {
		    if (io.buf && io.txRemaining != UINT_MAX && io.txRemaining) {
		      ++value.active;
		      if (io.txOffset > io.buf->skip) ++value.progress;
		    }
		    return false;
		  }});
		}
#endif
		wake(value);
	      }, 2);
	    });
	  } while ((observed.progress != sessions || observed.active != sessions) &&
	    Zm::now() < deadline);
	  ZuCheckRT(observed.progress == sessions && observed.active == sessions);
	} else {
	  state.watch = 1; sendFrames(links[0].ptr());
	  ZuCheckRT(state.blocked.timedwait(Zm::now(5)) == 0);
	  ZuCheckRT(state.wouldBlock.load_());
	}
	if (nativePartial && abort) ZmBlock<>{}([&server](auto wake) {
	  // Native receives can queue complete frames while I/O Rx is held.
	  // Close the server endpoints before release so queued bytes cannot
	  // admit echoes during cancellation of the pending client writes.
	  server.rxRun([&server, wake = ZuMv(wake)]() mutable {
	    for (const auto &link : server.links) link->disconnect_(false);
	    wake();
	  });
	});
	if (abort) for (const auto &link : links) ZmBlock<>{}([&client, &link](auto wake) {
	  // This scope aborts a blocked physical write. Graceful TLS disconnect
	  // deliberately drains its queued records before close_notify.
	  client.rxRun([link, wake = ZuMv(wake)]() mutable {
	    link->disconnect_(false); wake();
	  });
	});
	release.post();
	// Keep stack-owned gate objects alive until the native callback exits.
	ZmSemaphore released; mx.run([&released] { released.post(); }, 1); released.wait();
      }
      if (!abort) {
	ZuCheckRT(state.done.timedwait(Zm::now(10)) == 0);
	ZuCheckRT(state.received.load_() == 2 * sessions);
      }
    }
  }
  // Outbound links are application-owned. Drain their physical close before
  // finalizing the client hub's TLS context; Hub::stop alone does not own them.
  for (unsigned i = 0, n = links.length(); i < n; ++i) links[i]->disconnect();
  if (links.length()) ZuCheckRT(state.drained.timedwait(Zm::now(5)) == 0);
  if (cs) { bool stopped = client.stop(); ZuCheckRT(stopped); }
  if (ss) { bool stopped = server.stop(); ZuCheckRT(stopped); }
  client.final(); server.final(); mx.stop(); links.length(0); server.links.length(0);
#ifdef __linux__
  sendState = nullptr;
#endif
  ZuCheckRT(!state.errors.load_());
  ZuCheckRT(state.closed.load_() == 2 * sessions);
  ZuCheckRT(state.completed.load_() == (abort ? 2 : 4) * sessions);
  ZuCheckRT(state.failed.load_() == (abort ? 2 : 0) * sessions);
  if (nativePartial && abort) {
    ZuCheckRT(!state.received.load_());
    ZuCheckRT(!state.binaryViews.load_() && !state.retainedViews.load_());
  }
  if (payload && !abort) {
    ZuCheckRT(state.binaryViews.load_() == 4 * sessions);
    ZuCheckRT(state.retainedViews.load_() == 2 * sessions);
  }
  if (partial && !nativePartial) {
    ZuCheckRT(state.wouldBlock.load_());
#ifdef ZFIX_TEST_TLS
    if (!abort) ZuCheckRT(state.shortWrites.load_());
#else
    ZuCheckRT(state.shortWrites.load_());
#endif
    log("# native writes: short=", state.shortWrites.load_(), " EAGAIN=",
      state.wouldBlock.load_(), " close=", unsigned(abort));
    if (abort) ZuCheckRT(!state.received.load_());
  }
}
// The predecessor's native close callback is deferred on the test owner, not
// blocked on a worker. This makes B's input precede obsolete A cleanup.
struct SwapState {
  ZmSemaphore		listening, clientReady, serverReady, oldClosed, oldRetired,
    cleanupHeld, prefix, response, clientClosed, serverClosed, clientStopped, serverStopped,
    lateChecked, associated, protectedRecord, protectRelease, txError;
  ZmAtomic<unsigned>	errors{0}, replies{0}, staleCloses{0}, closed{0};
  ZmAtomic<unsigned>	connectedClients{0}, connectedServers{0};
  ZmAtomic<unsigned>	shutdowns{0}, protectNext{0}, txErrors{0};
  ZmAtomic<int>		oldSocket{-1};
  unsigned		port = 0;
  bool			late = false, refuse = false; // immutable before workers start
  bool			checkLate = false; // immutable before workers start

  void fail(unsigned line = __builtin_LINE()) {
    log("# native replacement failure line=", line);
    ++errors; response.post();
  }
};

#ifdef __linux__
static SwapState *swapState = nullptr;
extern "C" __attribute__((visibility("default")))
int shutdown(int fd, int how) noexcept
{
  auto state = swapState;
  if (state && fd == state->oldSocket.load_()) ++state->shutdowns;
  return syscall(SYS_shutdown, fd, how);
}
#endif

struct SwapCli : Net::Client<SwapCli> {
  struct Link;
  SwapState	*state;

  explicit SwapCli(SwapState *s) : state{s} { }

  unsigned reconnFreq() const { return 0; }
};

struct SwapCli::Link : ZmNHeap<Link>, Net::CliLink<SwapCli, Link> {
friend inline ZuStringT<"Zfix.TransportSwap.Cli"> ZmNHeapID(Link *);
  using ZmNHeap<Link>::operator new;
  using ZmNHeap<Link>::operator delete;
  using Base = Net::CliLink<SwapCli, Link>;
  Zfix::RxFramer	frame;

  explicit Link(SwapCli *hub) : Base{hub} { }

  void connected(Net::Connected) {
    ++app()->state->connectedClients; app()->state->clientReady.post();
  }

  void disconnected(bool) {
    frame.reset_(); ++app()->state->closed; app()->state->clientClosed.post();
  }

  void write(ZuCSpan bytes) {
    app()->txRun([this, bytes] {
      auto output = this->txStream_(); output << bytes;
      if (!output.flush()) app()->state->fail();
    });
  }

  int process(Net::RxStream &stream) {
    Zi::RxRange bytes;
    int n = frame.next_(stream, [this] {
	auto allocator = app()->mx()->rxAllocator(false);
	return ZmRef<ZiRxQueue::Node>::acquire(static_cast<ZiRxQueue::Node *>(
	  allocator->alloc(this).release()));
      }, bytes);
    if (n < 0) { app()->state->fail(); return -1; }
    if (!n) return 0;
    if (ZuCSpan{bytes.cspan()} != ZuCSpan{Wire}) { app()->state->fail(); return -1; }
    ++app()->state->replies; app()->state->response.post(); return 1;
  }
};

struct SwapCxn;
struct SwapSrv : Net::Server<SwapSrv> {
  struct Link;
  using Base = Net::Server<SwapSrv>;
  using CloseFn = ZmFn<void(), ZmFnHeapID<"Zfix.TransportSwap.Close">>;
  SwapState		*state;
  ZmRef<Link>		link;
  ZmRef<ZiConnection>	old;
  CloseFn		cleanup;
  unsigned		acceptedCount = 0; // native I/O Rx-owned
  bool			hold = true, oldPeer = false; // App Rx-owned
  bool			holdTx = false; // App Rx-owned

  explicit SwapSrv(SwapState *s) : state{s} { }

  ZiConnection *accepted(const ZiCxnInfo &);
  template <typename Fn> void txRun(Fn &&);
  void oldClose(ZmRef<ZiConnection>, bool);
  void beginClose();
  void checkClosed(ZmRef<ZiConnection>);
  void release();
  void retired();

  ZiIP localIP() const { return ZiIP{"127.0.0.1"}; }
  unsigned localPort() const { return 0; }
  unsigned nAccepts() const { return 1; }

  void listening(const ZiListenInfo &i) { state->port = i.port; state->listening.post(); }

  void listenFailed(bool) { state->fail(); state->listening.post(); }
};

struct SwapSrv::Link : ZmNHeap<Link>, Net::SrvLink<SwapSrv, Link> {
friend inline ZuStringT<"Zfix.TransportSwap.Srv"> ZmNHeapID(Link *);
  using ZmNHeap<Link>::operator new;
  using ZmNHeap<Link>::operator delete;
  using Base = Net::SrvLink<SwapSrv, Link>;
  Zfix::RxFramer	frame;
  unsigned		sockets = 0; // App Rx-owned association count
  bool			prefixSeen = false;

  explicit Link(SwapSrv *hub) : Base{hub} { }

  bool connectSocket_(bool) {
    if (++sockets != 2) return true;
    app()->state->associated.post(); return !app()->state->refuse;
  }

#ifdef ZFIX_TEST_TLS
  void txProtected_() {
    auto s = app()->state;
    if (!s->protectNext.xch(0)) return;
    s->protectedRecord.post();
    // Hold only this application Tx producer at the actual protection
    // boundary. Native association and Rx remain free to install B.
    s->protectRelease.wait();
  }
#endif

  void write() {
    app()->txRun([this] {
      auto output = this->txStream_(); output << Wire;
#ifndef ZFIX_TEST_TLS
      auto s = app()->state;
      if (s->protectNext.xch(0)) {
	s->protectedRecord.post(); s->protectRelease.wait();
      }
#endif
      if (!output.flush()) app()->state->fail();
    });
  }

  void connected(Net::Connected) {
    frame.reset_(); ++app()->state->connectedServers; app()->state->serverReady.post();
  }

  void disconnected(bool) {
    // A displaced predecessor cannot report the still-installed B as down.
    if (this->cxn()) { ++app()->state->staleCloses; return; }
    frame.reset_(); app()->state->serverClosed.post();
  }

  int process(Net::RxStream &stream) {
    Zi::RxRange bytes;
    int n = frame.next_(stream, [this] {
	auto allocator = app()->mx()->rxAllocator(false);
	return ZmRef<ZiRxQueue::Node>::acquire(static_cast<ZiRxQueue::Node *>(
	  allocator->alloc(this).release()));
      }, bytes);
    if (n < 0) { app()->state->fail(); return -1; }
    if (!n) {
      if (!prefixSeen) { prefixSeen = true; app()->state->prefix.post(); }
      return 0;
    }
    if (ZuCSpan{bytes.cspan()} != ZuCSpan{Wire}) { app()->state->fail(); return -1; }
    app()->txRun([this] {
      auto output = this->txStream_(); output << Wire;
      if (!output.flush()) app()->state->fail();
    });
    return 1;
  }
};

struct SwapCxn : ZmNHeap<SwapCxn>, SwapSrv::Link::Cxn {
friend inline ZuStringT<"Zfix.TransportSwap.Cxn"> ZmNHeapID(SwapCxn *);
  using ZmNHeap<SwapCxn>::operator new;
  using ZmNHeap<SwapCxn>::operator delete;
  using Base = SwapSrv::Link::Cxn;
  SwapSrv		*const hub;
  const unsigned	index;

  SwapCxn(SwapSrv *h, const ZiCxnInfo &ci, unsigned i) :
    Base{h->link, ci}, hub{h}, index{i} { }

  void disconnected(bool peer) override {
    if (index != 1) { Base::disconnected(peer); return; }
    hub->state->oldSocket = -1;
    if (hub->state->late) {
      // Native Rx must observe A as current before the App-Tx task is held.
      // No retirement marker is posted until that task is actually released.
      Base::disconnected(peer); hub->state->oldClosed.post(); return;
    }
    hub->rxRun([keep = ZmRef<SwapCxn>{this}, peer]() mutable {
      auto hub = keep->hub; hub->oldClose(ZuMv(keep), peer);
    });
  }

  void finish(bool peer) { Base::disconnected(peer); hub->retired(); }
};

ZiConnection *SwapSrv::accepted(const ZiCxnInfo &ci) {
  unsigned index = ++acceptedCount;
  if (index == 1) state->oldSocket = int(ci.socket);
  return new SwapCxn{this, ci, index};
}

template <typename Fn> void SwapSrv::txRun(Fn &&fn) {
  if (this->rxInvoked() && holdTx && link && !link->cxn()) {
    // The native current-at-entry Rx close has cleared its current pointer and
    // is now dispatching its first App-Tx continuation. Retain that task only.
    holdTx = false; cleanup = ZuFwd<Fn>(fn); state->cleanupHeld.post(); return;
  }
  Base::txRun(ZuFwd<Fn>(fn));
}

void SwapSrv::oldClose(ZmRef<ZiConnection> cxn, bool peer) {
  state->oldClosed.post();
  if (hold) { old = ZuMv(cxn); oldPeer = peer; return; }
  static_cast<SwapCxn *>(cxn.ptr())->finish(peer);
}

void SwapSrv::beginClose() {
  this->rxRun([this] {
    if (!link || !link->cxn()) { state->fail(); return; }
    holdTx = true;
#ifdef ZFIX_TEST_TLS
    if (state->refuse) link->disconnect_(false);
    else
#endif
      link->cxn()->close();
  });
}

struct ClosedOutput : ZiTx<ClosedOutput> {
  ZiConnection	*socket;

  explicit ClosedOutput(ZiConnection *s) : socket{s} { }

  ZiConnection *cxn() { return socket; }
};

void SwapSrv::checkClosed(ZmRef<ZiConnection> cxn) {
  auto io = this->mx();
  io->txRun([s = state, cxn = ZuMv(cxn)] {
    if (cxn->up()) { s->fail(); return; }
    using Alloc = ZiTxBufAlloc<ZiIOBuf_DefltSize, ZiIOBuf_DefltMaxSize,
      "Zfix.TransportSwap.Late">;
    ZmRef<ZiTxBuf> buf = new Alloc{nullptr, 0};
    if (!buf->append(Wire)) { s->fail(); return; }
    // ZiConnection::send invokes its real I/O-context starter inline on this
    // socket's I/O Tx owner. A down socket must not repopulate any Tx queue.
    ClosedOutput output{cxn.ptr()}; output.send(ZuMv(buf));
    if (!output.txQueue.empty_()) s->fail();
  });
  // This marker runs after the probe closure releases its old connection ref.
  io->txRun([s = state] { s->lateChecked.post(); });
}

void SwapSrv::release() {
  this->rxRun([this] {
    hold = holdTx = false;
    if (auto task = ZuMv(cleanup)) {
      Base::txRun([task = ZuMv(task)]() mutable { task(); });
      retired();
    }
    if (auto cxn = ZuMv(old)) {
      if (state->checkLate) checkClosed(cxn);
      static_cast<SwapCxn *>(cxn.ptr())->finish(oldPeer);
    }
  });
}

void SwapSrv::retired() {
  // Base close posts App Rx -> App Tx -> I/O Tx -> App Rx cleanup. These FIFO
  // markers follow that path, including a stale path that correctly does less.
  this->rxRun([this] {
    this->txRun([this] {
      this->mx()->txRun([this] {
	this->rxRun([this] { state->oldRetired.post(); });
      });
    });
  });
}

struct SwapRun : ZmNHeap<SwapRun> {
friend inline ZuStringT<"Zfix.TransportSwap.Run"> ZmNHeapID(SwapRun *);
  SwapState		state;
  ZiMultiplex		mx;
  SwapSrv		server{&state};
  SwapCli		client{&state};
  ZmRef<SwapCli::Link>	a, b;

  explicit SwapRun(ZiMxBackend::T backend = ZiMxBackend::Auto) : mx{ZiMxParams{}
    .backend(backend)
    .tcpBufAllocator<RxAllocator>()
    .scheduler([](auto &s) {
    s.nThreads(4)
      .thread(1, [](auto &t) { t.isolated(1); })
      .thread(2, [](auto &t) { t.isolated(1); })
      .thread(3, [](auto &t) { t.isolated(1); })
      .thread(4, [](auto &t) { t.isolated(1); });
    }).rxThread(1).txThread(2)} { }
};

static bool swapUndrained = false; // main-only failure exit, never a pass condition
static bool swapWait(ZmSemaphore &s) { return s.timedwait(Zm::now(5)) == 0; }

static void replacement(bool late = false, bool refuse = false,
    bool protect = false, bool stale = false)
{
  ZuTestScopeRT(stale ? "staleTxPolicy" : protect ? "protectedReplacement" :
    late ? (refuse ? "refusedReplacement" : "closingReplacement") : "replacement");
  auto run = new SwapRun;
  auto &s = run->state;
  s.late = late; s.refuse = refuse;
  bool started = run->mx.start(); ZuCheckRT(started);
  if (!started) { delete run; return; }
#ifdef __linux__
  swapState = &s;
#endif
  Net::ServerParams sp{&run->mx, "3", "4"};
  Net::ClientParams cp{&run->mx, "3", "4"};
#ifdef ZFIX_TEST_TLS
  const char *cert = getenv("ZFIX_TEST_CERT"), *key = getenv("ZFIX_TEST_KEY");
  if (!cert || !key) {
    ZuCheckRT(cert && key); run->mx.stop(); delete run;
#ifdef __linux__
    swapState = nullptr;
#endif
    return;
  }
  sp.certPath(cert).keyPath(key); cp.caPath(cert);
#endif
  bool si = run->server.init(ZuMv(sp)), ci = run->client.init(ZuMv(cp));
  ZuCheckRT(si && ci);
  bool ss = si && run->server.start(), cs = ci && run->client.start();
  ZuCheckRT(ss && cs);
  bool serverClosed = false;
  if (ss && cs) {
    run->server.link = new SwapSrv::Link{&run->server};
    if (stale) run->server.link->txErrorFn(ZiTxErrorFn{[hub = &run->server](ZeException &) {
      if (!hub->rxInvoked()) hub->state->fail();
      ++hub->state->txErrors;
      // Observe the policy after its complete Rx turn, including any close.
      hub->rxRun([s = hub->state] { s->txError.post(); });
      return false;
    }});
    run->a = new SwapCli::Link{&run->client}; run->b = new SwapCli::Link{&run->client};
    run->server.listen();
    bool listening = swapWait(s.listening); ZuCheckRT(listening && s.port);
    auto connect = [&s](SwapCli::Link *link) {
#ifdef ZFIX_TEST_TLS
      link->connect("localhost", s.port, ZiIP{"127.0.0.1"});
#else
      link->connect("127.0.0.1", s.port);
#endif
    };
    if (listening && s.port) {
      connect(run->a.ptr());
      bool aReady = swapWait(s.clientReady) && swapWait(s.serverReady); ZuCheckRT(aReady);
      if (aReady) {
	if (late) {
	  run->server.beginClose();
	  bool held = swapWait(s.cleanupHeld); ZuCheckRT(held);
	}
	if (protect) {
	  s.protectNext = 1; run->server.link->write();
	  bool protectedRecord = swapWait(s.protectedRecord); ZuCheckRT(protectedRecord);
	}
#ifdef ZFIX_TEST_TLS
	auto oldStream = run->server.link->txStream();
	if (stale) oldStream << Wire;
#endif
	connect(run->b.ptr());
	if (protect) {
	  bool associated = swapWait(s.associated); ZuCheckRT(associated);
	  s.protectRelease.post();
	}
	bool bReady = false;
	if (refuse) {
	  serverClosed = swapWait(s.serverClosed); ZuCheckRT(serverClosed);
	  ZuCheckRT(s.connectedClients.load_() == 1 && s.connectedServers.load_() == 1);
	} else {
	  bReady = swapWait(s.clientReady) && swapWait(s.serverReady); ZuCheckRT(bReady);
	}
	bool oldClosed = swapWait(s.oldClosed); ZuCheckRT(oldClosed);
#ifdef ZFIX_TEST_TLS
	if (stale && bReady) {
	  ZuCheckRT(!oldStream.flush());
	  ZuCheckRT(swapWait(s.txError) && s.txErrors.load_() == 1);
	}
#endif
	ZuCheckRT(!s.shutdowns.load_()); // actual old server fd never used graceful shutdown
	if (refuse) {
	  run->server.release();
	  bool retired = swapWait(s.oldRetired); ZuCheckRT(retired);
	  ZuCheckRT(!s.staleCloses.load_());
	} else if (bReady && oldClosed) {
	  unsigned half = Wire.length()/2;
	  run->b->write({Wire.data(), half});
	  bool prefix = swapWait(s.prefix); ZuCheckRT(prefix);
	  run->server.release();
	  bool retired = swapWait(s.oldRetired); ZuCheckRT(retired);
	  ZuCheckRT(!s.staleCloses.load_());
	  if (prefix && retired) {
	    run->b->write({Wire.data() + half, unsigned(Wire.length()) - half});
	    bool response = swapWait(s.response); ZuCheckRT(response && s.replies.load_() == 1);
	  }
	}
      }
    }
  }
  if (protect) s.protectRelease.post();
  run->server.release();
  if (run->a) run->a->disconnect();
  if (run->b) run->b->disconnect();
  if (run->a) ZuCheckRT(swapWait(s.clientClosed));
  if (run->b) ZuCheckRT(swapWait(s.clientClosed));
  if (run->server.link && !serverClosed) ZuCheckRT(swapWait(s.serverClosed));
  if (cs) run->client.stop([&s](bool ok) { if (!ok) s.fail(); s.clientStopped.post(); });
  if (ss) run->server.stop([&s](bool ok) { if (!ok) s.fail(); s.serverStopped.post(); });
  bool clientStopped = !cs || swapWait(s.clientStopped);
  bool serverStopped = !ss || swapWait(s.serverStopped);
  ZuCheckRT(clientStopped && serverStopped);
  ZuCheckRT(!s.errors.load_() && s.replies.load_() == unsigned(!refuse) && !s.staleCloses.load_());
  ZuCheckRT(s.connectedClients.load_() == (refuse ? 1U : 2U) &&
    s.connectedServers.load_() == (refuse ? 1U : 2U));
  ZuCheckRT(s.closed.load_() == 2 && !s.shutdowns.load_());
  if (!clientStopped || !serverStopped) {
    // Do not destroy a still-running native owner to conceal broken accounting.
    // The final scope reports failure before main terminates this reproducer.
    log("# native replacement physical accounting did not drain");
    swapUndrained = true; return;
  }
  run->client.final(); run->server.final(); run->mx.stop();
  run->a = nullptr; run->b = nullptr; run->server.link = nullptr;
#ifdef __linux__
  swapState = nullptr;
#endif
  delete run;
}

#if defined(__linux__) && !defined(ZFIX_TEST_TLS)
// A raw peer deliberately does not consume A's output. B must still acquire
// an independent socket send context after an actual kernel EAGAIN on A.
static void partialReplacement()
{
  ZuTestScopeRT("partialReplacement");
  auto run = new SwapRun{ZiMxBackend::EPoll};
  auto &s = run->state;
  s.checkLate = true;
  TestState writes;
  sendState = &writes; swapState = &s;
  bool started = run->mx.start(); ZuCheckRT(started);
  if (!started) { sendState = nullptr; swapState = nullptr; delete run; return; }
  Net::ServerParams sp{&run->mx, "3", "4"};
  Net::ClientParams cp{&run->mx, "3", "4"};
  bool si = run->server.init(ZuMv(sp)), ci = run->client.init(ZuMv(cp));
  ZuCheckRT(si && ci);
  bool ss = si && run->server.start(), cs = ci && run->client.start();
  ZuCheckRT(ss && cs);
  int raw = -1;
  if (ss && cs) {
    run->server.link = new SwapSrv::Link{&run->server};
    run->b = new SwapCli::Link{&run->client};
    run->server.listen();
    bool listening = swapWait(s.listening); ZuCheckRT(listening && s.port);
    if (listening && s.port) {
      raw = socket(AF_INET, SOCK_STREAM, 0); ZuCheckRT(raw >= 0);
      int size = 4096;
      bool tuned = raw >= 0 && !setsockopt(raw, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size));
      ZuCheckRT(tuned);
      sockaddr_in addr{};
      addr.sin_family = AF_INET; addr.sin_port = htons(s.port);
      addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
      bool connected = tuned &&
	!::connect(raw, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));
      ZuCheckRT(connected);
      bool aReady = connected && swapWait(s.serverReady); ZuCheckRT(aReady);
      if (aReady) {
	run->server.rxRun([&server = run->server, &writes] {
	  int size = 4096;
	  if (setsockopt(server.link->cxn()->info().socket, SOL_SOCKET,
	      SO_SNDBUF, &size, sizeof(size))) { server.state->fail(); return; }
	  writes.watch = 1;
	  server.txRun([link = server.link, &writes] {
	    auto packet = makePacket(200 * 1024);
	    auto output = link->txStream_(); output << packet->cspan();
	    if (!output.flush()) link->app()->state->fail();
	    writes.ready.post();
	  });
	});
	bool blocked = swapWait(writes.blocked); ZuCheckRT(blocked && writes.wouldBlock.load_());
	bool submitted = swapWait(writes.ready); ZuCheckRT(submitted);
	if (blocked && submitted) {
	  run->b->connect("127.0.0.1", s.port);
	  bool bReady = swapWait(s.clientReady) && swapWait(s.serverReady); ZuCheckRT(bReady);
	  bool oldClosed = swapWait(s.oldClosed); ZuCheckRT(oldClosed);
	  run->server.release();
	  bool retired = swapWait(s.oldRetired); ZuCheckRT(retired);
	  ZuCheckRT(swapWait(s.lateChecked));
	  if (bReady && oldClosed && retired) {
	    run->b->write(Wire);
	    bool response = swapWait(s.response); ZuCheckRT(response && s.replies.load_() == 1);
	  }
	}
      }
    }
  }
  run->server.release();
  if (raw >= 0) ::close(raw);
  if (run->b) { run->b->disconnect(); ZuCheckRT(swapWait(s.clientClosed)); }
  if (run->server.link) ZuCheckRT(swapWait(s.serverClosed));
  if (cs) run->client.stop([&s](bool ok) { if (!ok) s.fail(); s.clientStopped.post(); });
  if (ss) run->server.stop([&s](bool ok) { if (!ok) s.fail(); s.serverStopped.post(); });
  bool clientStopped = !cs || swapWait(s.clientStopped);
  bool serverStopped = !ss || swapWait(s.serverStopped);
  ZuCheckRT(clientStopped && serverStopped);
  ZuCheckRT(!s.errors.load_() && !s.staleCloses.load_() && !s.shutdowns.load_());
  if (!clientStopped || !serverStopped) { swapUndrained = true; return; }
  run->client.final(); run->server.final(); run->mx.stop();
  run->b = nullptr; run->server.link = nullptr;
  sendState = nullptr; swapState = nullptr; delete run;
}

// Hold the real syscall, reset its actual peer, and associate B on independent
// I/O Rx before A's kernel error is delivered to the native send callback.
static void resetTxPolicy()
{
  ZuTestScopeRT("resetTxPolicy");
  auto run = new SwapRun{ZiMxBackend::EPoll};
  auto &s = run->state;
  TestState writes;
  sendState = &writes; swapState = &s;
  bool started = run->mx.start(); ZuCheckRT(started);
  if (!started) { sendState = nullptr; swapState = nullptr; delete run; return; }
  Net::ServerParams sp{&run->mx, "3", "4"};
  bool initialized = run->server.init(ZuMv(sp)); ZuCheckRT(initialized);
  bool active = initialized && run->server.start(); ZuCheckRT(active);
  int a = -1, b = -1;
  if (active) {
    run->server.link = new SwapSrv::Link{&run->server};
    run->server.link->txErrorFn(ZiTxErrorFn{[&s](ZeException &) {
      ++s.txErrors; s.txError.post(); return false;
    }});
    run->server.listen();
    bool listening = swapWait(s.listening); ZuCheckRT(listening && s.port);
    auto connectRaw = [&s] {
      int fd = socket(AF_INET, SOCK_STREAM, 0);
      if (fd < 0) return fd;
      sockaddr_in addr{};
      addr.sin_family = AF_INET; addr.sin_port = htons(s.port);
      addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
      if (::connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr))) {
	::close(fd); return -1;
      }
      return fd;
    };
    if (listening && s.port) {
      a = connectRaw(); ZuCheckRT(a >= 0);
      bool aReady = a >= 0 && swapWait(s.serverReady); ZuCheckRT(aReady);
      if (aReady) {
	writes.holdSocket = s.oldSocket.load_();
	run->server.link->write();
	bool held = swapWait(writes.held); ZuCheckRT(held);
	if (held) {
	  linger reset{1, 0};
	  bool lingered = !setsockopt(a, SOL_SOCKET, SO_LINGER, &reset, sizeof(reset));
	  ZuCheckRT(lingered);
	  ::close(a); a = -1;
	  // Observe the real reset on independent I/O Rx before allowing the
	  // held I/O Tx syscall to see its actual kernel error.
	  ZuCheckRT(swapWait(writes.peerReset));
	  b = connectRaw(); ZuCheckRT(b >= 0);
	  bool bReady = b >= 0 && swapWait(s.serverReady); ZuCheckRT(bReady);
	  writes.release.post();
	  bool error = swapWait(s.txError); ZuCheckRT(error && writes.socketErrors.load_() == 1);
	  bool oldClosed = swapWait(s.oldClosed); ZuCheckRT(oldClosed);
	  run->server.release();
	  bool retired = swapWait(s.oldRetired); ZuCheckRT(retired);
	  if (bReady && error && oldClosed && retired) {
	    timeval timeout{5, 0};
	    ZuCheckRT(!setsockopt(b, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)));
	    ZuCheckRT(::send(b, Wire.data(), Wire.length(), MSG_NOSIGNAL) == ssize_t(Wire.length()));
	    ZtArray<uint8_t, ZtArrayHeapID<"Zfix.TransportSwap.Response">> response;
	    response.length(Wire.length());
	    unsigned received = 0;
	    while (received < response.length()) {
	      auto n = ::recv(b, response.data() + received, response.length() - received, 0);
	      if (n < 0 && errno == EINTR) continue;
	      if (n <= 0) break;
	      received += unsigned(n);
	    }
	    ZuCheckRT((received == response.length() &&
	      ZuCSpan{response} == ZuCSpan{Wire}));
	  }
	}
      }
    }
  }
  writes.release.post(); run->server.release();
  if (a >= 0) ::close(a);
  if (b >= 0) ::close(b);
  if (run->server.link) ZuCheckRT(swapWait(s.serverClosed));
  if (active) run->server.stop([&s](bool ok) { if (!ok) s.fail(); s.serverStopped.post(); });
  bool stopped = !active || swapWait(s.serverStopped); ZuCheckRT(stopped);
  ZuCheckRT(!s.errors.load_() && !s.staleCloses.load_() && !s.shutdowns.load_() &&
    s.connectedServers.load_() == 2 && s.txErrors.load_() == 1 &&
    writes.socketErrors.load_() == 1);
  if (!stopped) { swapUndrained = true; return; }
  if (initialized) run->server.final();
  run->mx.stop(); run->server.link = nullptr;
  sendState = nullptr; swapState = nullptr; delete run;
}
#endif

int main(int argc, char **argv) {
  parse(argc, argv); ZuTestMain();
  ZuTestCall(loopback, "single", 1);
  ZuTestCall(loopback, "multiple", 3);
#if Zi_URing && !defined(ZFIX_TEST_TLS)
  // One ordinary backing is concurrently wrapped by distinct bodies/attempts
  // on three links while receive is paused and socket buffers force progress.
  ZuTestCall(loopback, "nativeSharedPartial", 3, 200 * 1024, true, false, true);
  ZuTestCall(loopback, "nativeSharedAbort", 3, 200 * 1024, true, true, true);
#endif
  // TLS plaintext records are bounded at 2^14 bytes: cross two boundaries.
  ZuTestCall(loopback, "records", 2, 2 * (1U << 14) + 1);
#ifdef __linux__
  // Bound both FIX field size and wire retention while exceeding socket space.
  ZuTestCall(loopback, "partial", 1, 200 * 1024, true);
  ZuTestCall(loopback, "partialClose", 1, 200 * 1024, true, true);
#endif
  ZuTestCall(replacement);
  ZuTestCall(replacement, true);
#ifdef ZFIX_TEST_TLS
  ZuTestCall(replacement, true, true);
  ZuTestCall(replacement, false, false, false, true);
#endif
  ZuTestCall(replacement, false, false, true);
#if defined(__linux__) && !defined(ZFIX_TEST_TLS)
  ZuTestCall(partialReplacement);
  ZuTestCall(resetTxPolicy);
#endif
  if (swapUndrained) { std::cout << std::flush; std::cerr << std::flush; _Exit(1); }
  return 0;
}
