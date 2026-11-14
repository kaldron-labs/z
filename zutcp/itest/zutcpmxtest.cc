//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZiFile.hh>
#include <zlib/ZmBlock.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/Zutcp.hh>
#include "ZiTestResidue.hh"

using namespace ZuTestUtil;

struct MxEvents {
  ZmSemaphore established, closed, sent, input;
  ZmAtomic<unsigned> destroyed = 0;
  // Fixture control reads owner state only after the corresponding handoff.
  alignas(Zm::CacheLineSize) unsigned connected = 0, disconnected = 0;
  unsigned received = 0, peerClosed = 0, bytes = 0;
  unsigned added = 0, removed = 0;
  bool observed = true, holdRx = false, valid = true;
  alignas(Zm::CacheLineSize) const uint8_t *firstTx = nullptr;
  unsigned firstTxSize = 0, firstPayloadOffset = 0, txCalls = 0;
  uint32_t localBase = 0;
  bool baseKnown = false, txValid = true, detached = true, beforeCQ = false;
};

static constexpr auto SmallMessage = "native-mx-byte-path"_z;
static ZuCSpan Message{SmallMessage};

static bool reserveOptions(Zutcp::PacketBuf *packet, const Zutcp::TCPHeader &header)
{
  int bytes = header.options.encodedLength(header.flags & 0x02);
  if (bytes < 0 || unsigned(bytes) > packet->avail()) return false;
  packet->skip += unsigned(bytes);
  return true;
}

static Zutcp::TCPOptions replayOptions(
  const Zutcp::ClosedTuple &receiver, const Zutcp::ClosedTuple &peer)
{
  Zutcp::TCPOptions options;
  if (receiver.timestamps) {
    options.flags = Zutcp::TCPOptions::TS;
    options.tsval = receiver.tsRecent;
    if (peer.timestamps && Zutcp::reuseSequence(peer.lastSentTS, options.tsval))
      options.tsval = peer.lastSentTS;
    options.tsecr = peer.tsRecent;
  }
  return options;
}

// FIFO waits belong to the fixture control caller, never an I/O owner.
static bool readGate(const Zi::Path &path)
{
  ZiFile file;
  if (file.open(path, ZiFile::ReadOnly | ZiFile::GC) != Zi::OK) return false;
  char byte;
  return file.read(&byte, 1) == 1 && byte == 'R';
}

static bool writeFile(
  const Zi::Path &path, ZuCSpan data = {}, unsigned flags = ZiFile::Write)
{
  ZiFile file;
  if (file.open(path, flags | ZiFile::GC) != Zi::OK) return false;
  bool written = file.write(data.data(), data.length()) == Zi::OK;
  return file.close() == Zi::OK && written;
}

static bool wireFaultGate()
{
  const char *directory = getenv("ZUTCP_WIRE_GATE_DIR");
  if (!directory) return true;
  ZtString<> path;
  path << directory << "/wire-release";
  return readGate(path);
}

// Direct AF_XDP interfaces precede AF_PACKET taps. Inspect this first accepted
// original Tx frame on its owner turn while recovery/CQ still pins the backing.
static bool firstDataBase(
  const uint8_t *allocation, unsigned size, unsigned payloadOffset,
  const ZiCxnInfo &info, uint32_t &base)
{
  for (unsigned shift = 0; shift < payloadOffset && shift < size; ++shift) {
    Zutcp::IPView ip;
    Zutcp::TCPView tcp;
    if (!Zutcp::readIP({allocation + shift, size - shift}, ip) ||
	!Zutcp::readTCP(ip, tcp) || ip.local != info.remote.ip() ||
	ip.remote != info.local.ip() || tcp.header.srcPort != info.local.port() ||
	tcp.header.dstPort != info.remote.port()) continue;
    if (tcp.payload.length() == Message.length() &&
	tcp.payload.data() == allocation + payloadOffset &&
	!memcmp(tcp.payload.data(), Message.data(), Message.length())) {
      base = tcp.header.seq; // independent first application byte has relative offset zero
      return true;
    }
  }
  return false;
}

struct MxTxBuf : public ZiIOBuf {
  using ZiIOBuf::ZiIOBuf;
  ~MxTxBuf() {
    auto events = static_cast<MxEvents *>(owner);
    events->detached &= !data_();
    ++events->destroyed;
  }
};

class MxCxn : public ZmNHeap<MxCxn>, public ZiConnection {
friend inline ZuStringT<"Zutcp.Test.MxCxn"> ZmNHeapID(MxCxn *);
public:
  using ZmNHeap<MxCxn>::operator new;
  using ZmNHeap<MxCxn>::operator delete;
  MxCxn(ZiMultiplex *mx, const ZiCxnInfo &info, MxEvents *events) :
    ZiConnection{mx, info}, m_events{events} { m_events->valid &= !plugin(); }
  void connected(ZiRxContext &context) override {
    m_events->valid &= mx()->invoked(mx()->rxThread()) && up() && plugin() &&
      Zi::nullSocket(info().socket) && info().mps && info().mps != ~uint32_t(0);
    if (!m_events->holdRx) armRx(context);
    ++m_events->connected;
    m_events->established.post();
  }
  void disconnected(bool peer) override {
    m_events->valid &= mx()->invoked(mx()->rxThread()) && !up() && !plugin() &&
      rxContext().completed() && txContext().completed();
    m_events->peerClosed += unsigned(peer);
    ++m_events->disconnected;
    m_events->closed.post();
  }

  void resumeRx() {
    mx()->rxRun([this] { armRx(rxContext()); mx()->plugin()->recv(this); });
  }

  void sendData(Zi::IOBufAllocator *allocator, unsigned offset, bool immediate = true) {
    mx()->txInvoke(this, [this, allocator, offset, immediate] {
      unsigned beforeCalls = m_events->txCalls;
      send_(ZiTxFn{[this, allocator, offset, immediate](ZiTxContext &context) {
	auto buf = allocator->allocBuf(m_events, Message.length() + offset, info().mps);
	m_events->txValid &= bool(buf);
	if (!buf) { context.complete(); return false; }
	if (!m_events->firstTx) {
	  m_events->firstTx = buf->data_();
	  m_events->firstTxSize = buf->size;
	  m_events->firstPayloadOffset = buf->skip + offset;
	}
	memset(buf->data(), 0x5e, offset);
	memcpy(buf->data() + offset, Message.data(), Message.length());
	context.init(ZiTxFn{[events = m_events, allocator, offset, immediate](
	    ZiTxContext &context) {
	  events->txValid &= context.length == int(Message.length()) &&
	    context.offset == offset &&
	    context.buf && (immediate ? !context.buf->data_() : bool(context.buf->data_())) &&
	    !allocator->allocBuf(events);
	  ++events->txCalls;
	  context.complete();
	  return true;
	}}, ZuMv(buf));
	context.offset = offset;
	return true;
      }});
      auto stack = static_cast<Zutcp::Stack *>(mx()->plugin());
      m_events->beforeCQ = m_events->destroyed == m_events->txCalls &&
	stack->txAdmission().outstanding > 0;
      if (immediate && !beforeCalls && m_events->txCalls == 1 &&
	  !m_events->baseKnown && m_events->beforeCQ)
	m_events->baseKnown = firstDataBase(m_events->firstTx, m_events->firstTxSize,
	  m_events->firstPayloadOffset, info(), m_events->localBase);
      m_events->sent.post();
      return this;
    });
  }

private:
  void armRx(ZiRxContext &context) {
    context.init(ZiRxFn{[events = m_events](ZiRxContext &context) {
      auto buf = context.buf;
      events->valid &= buf && buf->length == Message.length() &&
	!memcmp(buf->data(), Message.data(), Message.length());
      events->bytes += buf->length;
      context.buf = context.cxn->info().rxAllocator->allocBuf(context.cxn);
      ++events->received;
      events->input.post();
      return true;
    }}, info().rxAllocator->allocBuf(this));
  }

  MxEvents *m_events;
};

static void observe(ZiMultiplex &mx, MxEvents &events)
{
  mx.watch(Ztc::Mx::AddCxnFn{[&mx, &events](Ztc::Connection *base) {
    auto cxn = static_cast<ZiConnection *>(base);
    unsigned count = 0;
    unsigned n = mx.allCxns([&count, cxn](Ztc::Connection *found) {
      if (found == cxn) ++count;
    });
    events.observed &= mx.invoked(mx.rxThread()) && cxn->plugin() && cxn->up() &&
      n == 1 && count == 1;
    ++events.added;
  }}, Ztc::Mx::DelCxnFn{[&mx, &events](Ztc::Connection *base) {
    auto cxn = static_cast<ZiConnection *>(base);
    events.observed &= mx.invoked(mx.rxThread()) && !cxn->plugin() && !cxn->up() &&
      mx.allCxns([](Ztc::Connection *) { }) == 0;
    ++events.removed;
  }});
}

static ZiMxParams mxParams()
{
  return ZiMxParams{}.rxBufSize(128).rxThread(1).txThread(2)
    .scheduler([](auto &s) {
      s.nThreads(3)
	.thread(1, [](auto &t) { t.isolated(1); })
	.thread(2, [](auto &t) { t.isolated(1); });
    });
}

static bool wait(ZmSemaphore &event)
{
  return !event.timedwait(Zm::now() + ZuTime{10});
}

static bool start(ZiMultiplex &mx)
{
  auto deadline = Zm::now() + ZuTime{2};
  unsigned attempts = 0;
  do {
    ++attempts;
    if (mx.start()) {
      std::cout << "# AF_XDP startup attempts=" << attempts << '\n';
      return true;
    }
    usleep(1000); // fixture control caller; wait for the preceding kernel retirement
  } while (Zm::now() < deadline);
  return false;
}

static Zutcp::CxnAdmission admission(ZiMultiplex &mx, Zutcp::Stack *stack)
{
  return ZmBlock<Zutcp::CxnAdmission>{}([&mx, stack](auto done) {
    mx.rxRun([stack, done = ZuMv(done)]() mutable { done(stack->cxnAdmission()); });
  });
}

static Zutcp::IPPath route(
  ZiMultiplex &mx, Zutcp::Stack *stack, ZiIP local, ZiIP remote)
{
  return ZmBlock<Zutcp::IPPath>{}([&mx, stack, local, remote](auto done) {
    mx.rxRun([stack, local, remote, done]() mutable {
      if (!stack->resolvePath({local, remote, {}, {}, 0},
	  [done](Zutcp::IPPath path, ZtString<> error) mutable {
	    done(error ? Zutcp::IPPath{} : ZuMv(path));
	  })) done(Zutcp::IPPath{});
    });
  });
}

static bool extraInitial(
  ZiMultiplex &mx, Zutcp::Stack *stack, const Zutcp::IPPath &path,
  uint16_t port, bool)
{
  return ZmBlock<bool>{}([&mx, stack, path, port](auto done) {
    mx.txRun([stack, path, port, done = ZuMv(done)]() mutable {
      auto packet = stack->allocPacket(stack, path.local.type(), true);
      if (!packet) { done(false); return; }
      Zutcp::TCPHeader tcp{.srcPort = 18198, .dstPort = port, .seq = UINT32_MAX,
	.window = 65535, .flags = 0x02,
	.options = {.flags = Zutcp::TCPOptions::MSS,
	  .mss = uint16_t(Zutcp::tcpPayload(path.mtu, path.local.type()))}};
      if (!reserveOptions(packet.ptr(), tcp)) { done(false); return; }
      unsigned wire = Zutcp::writeTCP(packet->data_(), packet->size, path, tcp, 0);
      packet->skip = 0;
      packet->length = wire;
      done(wire && stack->sendPacket(packet));
    });
  });
}

enum { PassiveTotal = 1, PassivePending = 2 };

static void testMxConnections(
  bool v6, bool stopActive, bool loss = false, bool window = false, bool restart = false,
  bool pendingLimit = false, unsigned passiveLimit = 0)
{
  ZuTestScope(testMxConnections);
  alarm(35);
  unsigned a = ZuBox<unsigned>{getenv("ZXDP_RX_TEST_IFINDEX")};
  unsigned b = ZuBox<unsigned>{getenv("ZXDP_RX_TEST_PEER_IFINDEX")};
  ZiIP local = loss || window ? (v6 ? "2001:db8::20" : "198.18.0.20") :
    (v6 ? "2001:db8::10" : "198.18.0.10");
  ZiIP remote = loss || window ? (v6 ? "2001:db8::21" : "198.18.0.21") :
    (v6 ? "2001:db8::11" : "198.18.0.11");
  ZiMultiplex client{mxParams()}, server{mxParams()};
  ZmAtomic<unsigned> unanswered = 0, refusalResets = 0, unexpectedWire = 0;
  ZmSemaphore refusedWire, unansweredWire;
  Zutcp::PacketFn pendingPeer;
  if (passiveLimit == PassivePending)
    pendingPeer = [
      &unanswered, &refusalResets, &refusedWire, &unansweredWire, &unexpectedWire
    ](
      ZmRef<ZiIOBuf> buf) {
      Zutcp::IPView ip;
      Zutcp::TCPView tcp;
      if (!Zutcp::readIP(buf->cspan(), ip) || ip.protocol != IPPROTO_TCP) return;
      if (!Zutcp::readTCP(ip, tcp)) {
	if (++unexpectedWire == 1)
	  log_("pending raw decode failed length=", buf->length);
	return;
      }
      if (tcp.header.flags & 0x04) {
	const auto &h = tcp.header;
	bool empty = !tcp.payload.length() && !h.options.flags;
	bool refused = empty && (h.srcPort == 18196 || h.srcPort == 18197) &&
	  h.dstPort == 18198 && h.flags == 0x14 && !h.seq && !h.ack && !h.window;
	bool cancelled = empty && h.srcPort == 18193 && h.dstPort == 18194 &&
	  h.flags == 0x04 && !h.ack;
	if (refused) { ++refusalResets; refusedWire.post(); }
	else if (!cancelled && ++unexpectedWire == 1)
	  log_("pending unexpected RST src=", unsigned(h.srcPort),
	    " dst=", unsigned(h.dstPort), " flags=", unsigned(h.flags), " seq=", h.seq,
	    " ack=", h.ack, " window=", unsigned(h.window),
	    " options=", unsigned(h.options.flags), " payload=", tcp.payload.length());
	return;
      }
      if (tcp.header.dstPort == 18194 && (tcp.header.flags & 0x17) == 0x02 &&
	  !tcp.payload.length()) { ++unanswered; unansweredWire.post(); }
      else if (++unexpectedWire == 1) {
	const auto &h = tcp.header;
	log_("pending unexpected TCP src=", unsigned(h.srcPort),
	  " dst=", unsigned(h.dstPort), " flags=", unsigned(h.flags), " seq=", h.seq,
	  " ack=", h.ack, " window=", unsigned(h.window),
	  " options=", unsigned(h.options.flags), " payload=", tcp.payload.length());
      }
    };
  // This fixture deliberately leaves the active SYN to an unbound port unanswered.
  // Ordinary unbound TCP would correctly reset it before pending-cap admission.
  ZmRef<Zutcp::Stack> clientStack = new Zutcp::Stack{&client, {
    .interface = a, .packet = pendingPeer}};
  ZmRef<Zutcp::Stack> serverStack = new Zutcp::Stack{&server, {
    .interface = b, .packet = pendingPeer}};
  client.plugin(clientStack);
  server.plugin(serverStack);
  ZtString<> error;
  Zutcp::StackConfig config{.rxRetainFrames = 96, .rxRetainFramesPerCxn = 64,
    .rxReserveFrames = 16};
  auto clientRx = client.allocator<Zi::IOBufType::RxTCP>();
  auto serverRx = server.allocator<Zi::IOBufType::RxTCP>();
  auto clientTx = loss || window ? client.allocator<Zi::IOBufType::TxTCP, MxTxBuf, 1400>(1) :
    client.allocator<Zi::IOBufType::TxTCP, MxTxBuf, 128>(1);
  auto serverTx = loss || window ? server.allocator<Zi::IOBufType::TxTCP, MxTxBuf, 1400>(1) :
    server.allocator<Zi::IOBufType::TxTCP, MxTxBuf, 128>(1);
  auto message = Message;
  Zutcp::ClosedTuple restartHistory;
  for (unsigned turn = 0; turn < (restart ? 2u : 1u); ++turn) {
    alarm(35);
    if (restart) Message = {message.data(), message.length()>>turn};
    uint16_t clientPort = 18192 + turn;
    if (window) config.maxData = Message.length();
    if (stopActive || restart) {
      config.maxCxns = stopActive ? (pendingLimit ? 3 : 2) : (2u>>turn);
      config.maxPendingCxns = pendingLimit || restart ? 1 : 2;
    }
    if (passiveLimit) {
      config.maxCxns = passiveLimit == PassivePending ? 3 : 1;
      config.maxPendingCxns = 1;
    }
    bool configured = clientStack->configure(config, error) &&
      serverStack->configure(config, error);
    bool started = configured && start(client) && start(server);
    ZuCHECK(started, "both native plugin instances must start");
    if (!started) { client.stop(); server.stop(); Message = message; return; }
    MxEvents cli, srv;
    cli.holdRx = srv.holdRx = window;
    observe(client, cli);
    observe(server, srv);
    ZmRef<MxCxn> cliCxn, srvCxn;
    ZmSemaphore listening, failed;
    ZmSemaphore extraBound;
    ZmAtomic<unsigned> unexpectedPeer = 0;
    ZmAtomic<unsigned> cancelled = 0, unexpected = 0, rejected = 0;
    ZmSemaphore refusal;
    ZmAtomic<unsigned> failures = 0;
    auto fail = ZiFailFn{[&failures, &failed](bool) { ++failures; failed.post(); }};
    server.listen(ZiListenFn{[&listening](const ZiListenInfo &info) {
      if (Zi::nullSocket(info.socket) && info.port == 18191) listening.post();
    }}, fail, ZiConnectFn{[&server, &srv, &srvCxn, serverRx, clientPort](const ZiCxnInfo &info) {
      srv.valid &= info.type == ZiCxnType::TCPIn && info.rxAllocator == serverRx &&
	info.remote.port() == clientPort;
      srvCxn = new MxCxn{&server, info, &srv};
      return srvCxn.ptr();
    }}, remote, 18191, 4, serverRx, ZiCxnOptions{});
    bool bound = wait(listening);
    if (bound) client.connect(ZiConnectFn{
      [&client, &cli, &cliCxn, clientRx, clientPort](const ZiCxnInfo &info) {
	cli.valid &= info.type == ZiCxnType::TCPOut && info.rxAllocator == clientRx &&
	  info.local.port() == clientPort && info.remote.port() == 18191;
	cliCxn = new MxCxn{&client, info, &cli};
	return cliCxn.ptr();
      }}, fail, local, clientPort, remote, 18191, clientRx, ZiCxnOptions{});
    bool connected = bound && wait(cli.established) && wait(srv.established);
    ZuCHECK(connected && !unsigned(failures) && cli.valid && srv.valid &&
	cli.connected == 1 && srv.connected == 1 && !cli.received && !srv.received,
      "connect/accept must publish both pointers before callbacks with native tuples and owners");
    if (connected) {
      ZuCHECK(wireFaultGate(), "observed SYN origins must install fault selectors before output");
      auto slots = admission(client, clientStack.ptr());
      auto passive = admission(server, serverStack.ptr());
      ZuCHECK(slots.total == 1 && !slots.pending && slots.maxCxns == config.maxCxns &&
	  slots.maxPendingCxns == config.maxPendingCxns &&
	  passive.total == 1 && !passive.pending,
	"establishment must release only pending capacity and retain its total connection slot");
    }
    if (connected) {
      if (!stopActive) server.stopListening(remote, 18191);
      cliCxn->sendData(clientTx, stopActive ? 4 : 0);
      srvCxn->sendData(serverTx, stopActive ? 4 : 0);
      bool sent = wait(cli.sent) && wait(srv.sent);
      bool input = window || (wait(cli.input) && wait(srv.input));
      ZuCHECK(sent && input && cli.txValid && srv.txValid && cli.valid && srv.valid &&
	  cli.detached && srv.detached && cli.beforeCQ && srv.beforeCQ &&
	  cli.bytes == (window ? 0 : Message.length()) &&
	  srv.bytes == (window ? 0 : Message.length()) &&
	  cliCxn->txCalls() == 1 && srvCxn->txCalls() == 1,
	"Tx/Rx contexts must exchange exact bytes and release detached metadata before CQ");
    }
    if (connected) {
      auto cliPool = static_cast<Zutcp::Allocator *>(clientTx->provider());
      auto srvPool = static_cast<Zutcp::Allocator *>(serverTx->provider());
      auto deadline = Zm::now() + ZuTime{2};
      while ((cliPool->pool.used() || srvPool->pool.used()) && Zm::now() < deadline)
	usleep(1000); // fixture control caller; ACK and CQ processing stay on their owners
      bool returned = !cliPool->pool.used() && !srvPool->pool.used();
      ZuCHECK(returned && cliCxn->up() && srvCxn->up(),
	"byte ACK and independent kernel retirement must return one-slot pools while connected");
      if (returned && window) {
	cliCxn->sendData(clientTx, 4, false);
	srvCxn->sendData(serverTx, 4, false);
	bool sent = wait(cli.sent) && wait(srv.sent);
	usleep(3300000); // fixture control caller; capture must witness 1s/2s persist intervals
	auto held = [](ZiMultiplex &mx, MxEvents &events, Zutcp::Allocator *pool) {
	  return ZmBlock<bool>{}([&mx, &events, pool](auto done) {
	    mx.txRun([&events, pool, done = ZuMv(done)]() mutable {
	      done(events.txValid && events.destroyed == 1 && events.txCalls == 2 &&
		pool->pool.used() == 1);
	    });
	  });
	};
	ZuCHECK(sent && held(client, cli, cliPool) && held(server, srv, srvPool),
	  "zero-window admission must retain unsent metadata/backing through actual persist");
	cliCxn->resumeRx();
	srvCxn->resumeRx();
	bool input = wait(cli.input) && wait(srv.input) && wait(cli.input) && wait(srv.input);
	ZuCHECK(input && cli.valid && srv.valid && cli.txValid && srv.txValid &&
	    cli.bytes == (Message.length()<<1) && srv.bytes == (Message.length()<<1),
	  "reopening both receive contexts must resume exact queued DATA without another send");
	deadline = Zm::now() + ZuTime{2};
	while ((cliPool->pool.used() || srvPool->pool.used()) && Zm::now() < deadline)
	  usleep(1000);
	ZuCHECK(!cliPool->pool.used() && !srvPool->pool.used() &&
	    cli.destroyed == 2 && srv.destroyed == 2 && cli.detached && srv.detached,
	  "reopened sends must detach metadata and return original one-slot backing once");
      }
      if (returned && !window) {
	if (passiveLimit) {
	  if (passiveLimit == PassivePending) {
	    auto noConnect = ZiConnectFn{[&unexpected](const ZiCxnInfo &) -> ZiConnection * {
	      ++unexpected;
	      return nullptr;
	    }};
	    auto cancelledFn = ZiFailFn{[&cancelled](bool) { ++cancelled; }};
	    client.connect(noConnect, cancelledFn, local, 18193, remote, 18194, clientRx, {});
	    server.connect(noConnect, cancelledFn, remote, 18193, local, 18194, serverRx, {});
	    auto a = admission(client, clientStack.ptr());
	    auto b = admission(server, serverStack.ptr());
	    ZuCHECK(a.total == 2 && b.total == 2 && a.pending == 1 && b.pending == 1,
	      "one active handshake must reserve pending capacity on each mixed-role instance");
	  }
	  auto boundExtra = ZiListenFn{[&extraBound](const ZiListenInfo &) {
	    extraBound.post();
	  }};
	  auto noAccept = ZiConnectFn{[&unexpectedPeer](const ZiCxnInfo &) -> ZiConnection * {
	    ++unexpectedPeer;
	    return nullptr;
	  }};
	  server.listen(boundExtra, fail, noAccept, remote, 18196, 4, serverRx, {});
	  client.listen(boundExtra, fail, noAccept, local, 18197, 4, clientRx, {});
	  bool bound = wait(extraBound) && wait(extraBound);
	  auto forward = route(client, clientStack.ptr(), local, remote);
	  auto backward = route(server, serverStack.ptr(), remote, local);
	  bool injected = forward.mtu && backward.mtu &&
	    extraInitial(client, clientStack.ptr(), forward, 18196, false) &&
	    extraInitial(server, serverStack.ptr(), backward, 18197, true);
	  ZuCHECK(bound && injected && !unsigned(failures),
	    "valid extra Initials must precede the established DATA marker on each Tx queue");
	}
	cliCxn->sendData(clientTx, stopActive ? 0 : 4);
	srvCxn->sendData(serverTx, stopActive ? 0 : 4);
	bool sent = wait(cli.sent) && wait(srv.sent);
	bool input = wait(cli.input) && wait(srv.input);
	ZuCHECK(sent && input && cli.txValid && srv.txValid && cli.valid && srv.valid &&
  	  cli.detached && srv.detached && cli.beforeCQ && srv.beforeCQ &&
  	  cli.destroyed == 2 && srv.destroyed == 2 &&
  	  cli.bytes == (Message.length()<<1) && srv.bytes == (Message.length()<<1) &&
  	  cliCxn->txCalls() == 2 && srvCxn->txCalls() == 2,
  	"a second live send reuses the original one-slot backing after cumulative byte ACK");
	if (passiveLimit) {
	  // DATA orders receive processing after the extra Initial. A further
	  // resolve on the same cold worker orders its asynchronous admission
	  // completion before taking the snapshot; no polling or sleep.
	  auto forward = route(client, clientStack.ptr(), local, remote);
	  auto backward = route(server, serverStack.ptr(), remote, local);
	  unsigned pending = passiveLimit == PassivePending;
	  bool attempted = !pending || (wait(unansweredWire) && wait(unansweredWire));
	  bool refused = !pending || (wait(refusedWire) && wait(refusedWire));
	  auto a = admission(client, clientStack.ptr());
	  auto b = admission(server, serverStack.ptr());
	  bool shared = forward.mtu && backward.mtu && a.total == 1 + pending &&
	    b.total == 1 + pending && a.pending == pending && b.pending == pending &&
	    a.refused == 1 && b.refused == 1 &&
	    !unsigned(unexpectedPeer) && !unsigned(unexpectedWire) &&
	    attempted && refused && (!pending || (unanswered >= 2 && refusalResets == 2)) &&
	    cli.connected == 1 && srv.connected == 1;
	  if (!shared)
	    log_("passive cap v6=", unsigned(v6), " limit=", passiveLimit,
	      " mtu=", unsigned(forward.mtu), '/', unsigned(backward.mtu),
	      " total=", a.total, '/', b.total, " pending=", a.pending, '/', b.pending,
	      " refused=", a.refused, '/', b.refused, " peer=", unsigned(unexpectedPeer),
	      " wire=", unsigned(unexpectedWire), " attempted=", unsigned(attempted),
	      " joined=", unsigned(refused), " SYN=", unsigned(unanswered),
	      " RST=", unsigned(refusalResets), " connected=", cli.connected, '/', srv.connected);
	  ZuCHECK(shared,
	    "passive refusal must share the cap across listeners and connection roles");
	}
      }
    }
    if (connected) {
      unsigned connections = 0, queues = 0;
      auto count = client.allCxns([&connections, &cliCxn](Ztc::Connection *cxn) {
	if (cxn == cliCxn.ptr()) ++connections;
      });
      bool queueKeys = true;
      auto n = clientStack->allQueues([&queues, &queueKeys](Ztc::Queue *queue) {
	Ztc::QueueTelemetry data;
	queue->telemetry(data);
	auto key = queue->telKey();
	queueKeys &= data.ownerID == key.template p<0>() && data.id == key.template p<1>() &&
	  data.type == key.template p<2>();
	++queues;
      });
      bool onTx = ZmBlock<bool>{}([&client, cliCxn](auto done) {
	client.txRun([&client, cliCxn, done = ZuMv(done)]() mutable {
	  unsigned found = 0;
	  auto n = client.allCxns([&found, cliCxn](Ztc::Connection *cxn) {
	    if (cxn == cliCxn.ptr()) ++found;
	  });
	  done(n == 1 && found == 1);
	});
      });
      ZuCHECK(count == 1 && connections == 1 && n == 2 && queues == 2 && queueKeys && onTx,
	"cold enumeration must expose the live connection and its two queues on control and Tx");
    }
    if (connected && !stopActive) {
      // Fresh-stub notifications exercise the plugin-to-native pointer dispatch.
      ZmBlock<>{}([&client, cliCxn](auto done) mutable {
	client.rxRun([&client, cliCxn, done = ZuMv(done)]() mutable {
	  client.plugin()->recv(cliCxn);
	  done();
	});
      });
      server.stopListening(remote, 18191);
      bool stillConnected = ZmBlock<bool>{}([&server, srvCxn](auto done) mutable {
	server.rxRun([srvCxn, done = ZuMv(done)]() mutable { done(bool(srvCxn->plugin())); });
      });
      ZuCHECK(stillConnected && !srv.disconnected,
	"stopListening must preserve its established connection");
      unsigned snapshots = 0;
      bool snapshotValid = true, acceptedClosed = false, serverStopped = false;
      bool closed = false;
      auto count = serverStack->allQueues([
	&snapshots, &snapshotValid, &acceptedClosed, &serverStopped, &server, &srv,
	&cli, cliCxn, &closed
      ](Ztc::Queue *queue) {
	if (!snapshots++) {
	  cliCxn->close();
	  closed = wait(cli.closed);
	  acceptedClosed = wait(srv.closed);
	  serverStopped = server.stop();
	}
	Ztc::QueueTelemetry data;
	queue->telemetry(data);
	auto key = queue->telKey();
	snapshotValid &= key.p<0>() == data.ownerID && key.p<1>() == data.id &&
	  key.p<2>() == data.type && data.type >= 0 && data.type < Ztc::QueueType::N;
      });
      ZuCHECK(closed && cli.valid && cli.disconnected == 1,
	"close must drain native work and clear both pointers before Zi notification");
      ZuCHECK(acceptedClosed && serverStopped && srv.valid && srv.disconnected == 1 &&
	  count == 2 && snapshots == 2 && snapshotValid,
	"queue snapshots must survive connection and listener retirement inside the observer");
    }
    if (connected && stopActive && !passiveLimit) {
      client.connect(ZiConnectFn{[&unexpected](const ZiCxnInfo &) -> ZiConnection * {
	++unexpected;
	return nullptr;
      }}, ZiFailFn{[&cancelled](bool) { ++cancelled; }},
	local, 18193, remote, 18194, clientRx, ZiCxnOptions{});
      ZmBlock<>{}([&client](auto done) {
	client.rxRun([done = ZuMv(done)]() mutable { done(); });
      });
      auto before = admission(client, clientStack.ptr());
      client.connect(ZiConnectFn{[&unexpected](const ZiCxnInfo &) -> ZiConnection * {
	++unexpected;
	return nullptr;
      }}, ZiFailFn{[&rejected, &refusal](bool transient) {
	if (!transient) ++rejected;
	refusal.post();
      }}, local, 18195, remote, 18194, clientRx, ZiCxnOptions{});
      bool refused = wait(refusal);
      auto after = admission(client, clientStack.ptr());
      ZuCHECK(refused && rejected == 1 && before.total == 2 && before.pending == 1 &&
	  after.total == before.total && after.pending == before.pending &&
	  after.refused == before.refused + 1 && !unsigned(unexpected),
	"total or pending exhaustion must refuse once before constructing another native owner");
      ZmBlock<>{}([&client, clientStack, cliCxn](auto done) {
	client.txRun([clientStack, cliCxn, done = ZuMv(done)]() mutable {
	  clientStack->close(cliCxn, false);
	  clientStack->close(cliCxn, false);
	  done();
	});
      });
    }
    bool stopped = client.stop() && server.stop();
    unsigned cancellations = passiveLimit == PassivePending ? 2 : !passiveLimit;
    ZuCHECK(!stopActive ||
      (cancelled == cancellations && rejected == unsigned(!passiveLimit) &&
	!unsigned(unexpected) && !unsigned(unexpectedPeer) &&
	cli.disconnected == 1 && srv.disconnected == 1),
      "stop must retire active owners and cancel setup without publishing another Zi connection");
    ZuCHECK(stopped && cli.valid && srv.valid &&
	(!connected || (!cliCxn->plugin() && !srvCxn->plugin())),
      "native endpoint and Zi context lifetimes must drain before plugin stop");
    if (stopped) {
      auto a = clientStack->cxnAdmission(), b = serverStack->cxnAdmission();
      ZuCHECK(!a.total && !a.pending && !b.total && !b.pending,
	"established and cancelled handshakes must release every connection slot");
    }
    if (restart && stopped) {
      Zutcp::ClosedTuple old;
      bool found = clientStack->closedTuple(
	{ZiSockAddr{local, 18192}, ZiSockAddr{remote, 18191}}, old);
      if (!turn) restartHistory = old;
      ZuCHECK(found && old.id && old.id == restartHistory.id &&
	  old.txNext == restartHistory.txNext &&
	  old.rxNext == restartHistory.rxNext,
	"restart with a fresh client port must preserve the first tuple's retained evidence");
    }
    bool empty = client.allCxns([](Ztc::Connection *) { }) == 0 &&
      server.allCxns([](Ztc::Connection *) { }) == 0;
    ZuCHECK(empty && cli.observed && srv.observed &&
	cli.added == 1 && cli.removed == 1 && srv.added == 1 && srv.removed == 1,
      "watch callbacks and stopped enumeration must reflect exactly one retired association");
    client.unwatch();
    server.unwatch();
    cliCxn = nullptr;
    srvCxn = nullptr;
    alarm(0);
  }
  Message = message;
}

static void testConnectTimeout(bool v6)
{
  ZuTestScope(testConnectTimeout);
  alarm(35);
  unsigned a = ZuBox<unsigned>{getenv("ZXDP_RX_TEST_IFINDEX")};
  unsigned b = ZuBox<unsigned>{getenv("ZXDP_RX_TEST_PEER_IFINDEX")};
  ZiIP local{v6 ? "2001:db8::10" : "198.18.0.10"};
  ZiIP remote{v6 ? "2001:db8::11" : "198.18.0.11"};
  ZiMultiplex client{mxParams()}, server{mxParams()};
  ZmSemaphore marker, listening;
  struct PendingSyn {
    ZmSemaphore seen;
    ZmAtomic<unsigned> count = 0, errors = 0;
    uint32_t localBase = 0;
  } syn;
  ZmRef<Zutcp::Stack> clientStack = new Zutcp::Stack{&client, {.interface = a,
    .packet = [&syn](ZmRef<ZiIOBuf> buf) {
      Zutcp::IPView ip;
      Zutcp::TCPView tcp;
      if (!Zutcp::readIP(buf->cspan(), ip) || !Zutcp::readTCP(ip, tcp) ||
	  tcp.header.dstPort != 18198 || tcp.header.flags != 0x12 || tcp.header.ack ||
	  tcp.payload.length())
	return;
      auto base = uint32_t(tcp.header.seq + 1);
      if (!syn.count.xch(1)) {
	syn.localBase = base;
	syn.seen.post();
      } else if (syn.localBase != base) ++syn.errors;
    }}};
  ZmRef<Zutcp::Stack> serverStack = new Zutcp::Stack{&server, {
    .interface = b, .packet = [&marker](ZmRef<Zi::IOBuf> buf) {
      Zutcp::IPView ip;
      Zutcp::TCPView tcp;
      if (Zutcp::readIP(buf->cspan(), ip) && Zutcp::readTCP(ip, tcp) &&
	  tcp.header.dstPort == 18199) marker.post();
    }}};
  client.plugin(clientStack);
  server.plugin(serverStack);
  auto clientRx = client.allocator<Zi::IOBufType::RxTCP>();
  auto serverRx = server.allocator<Zi::IOBufType::RxTCP>();
  auto clientTx = client.allocator<Zi::IOBufType::TxTCP, MxTxBuf, 128>(1);
  auto serverTx = server.allocator<Zi::IOBufType::TxTCP, MxTxBuf, 128>(1);
  Zutcp::StackConfig config{.rxRetainFrames = 96, .rxRetainFramesPerCxn = 64,
    .rxReserveFrames = 16, .maxCxns = 1, .maxPendingCxns = 1, .connectTimeout = ZuTime{3}};
  auto serverConfig = config;
  serverConfig.connectTimeout = ZuTime{1};
  ZtString<> error;
  bool started = clientStack->configure(config, error) &&
    serverStack->configure(serverConfig, error) && start(client) && start(server);
  ZuCHECK(started, "connect policy fixture must start both constrained instances");
  if (!started) { client.stop(); server.stop(); alarm(0); return; }
  MxEvents cli, srv;
  observe(client, cli);
  observe(server, srv);
  ZmRef<MxCxn> cliCxn, srvCxn;
  ZmAtomic<unsigned> failures = 0, retryFailures = 0, listenFailures = 0;
  bool released = false, clientHistory = false;
  auto retryFail = ZiFailFn{[&retryFailures](bool) { ++retryFailures; }};
  auto retry = ZiConnectFn{[&client, &cli, &cliCxn](const ZiCxnInfo &info) -> ZiConnection * {
    cliCxn = new MxCxn{&client, info, &cli};
    return cliCxn.ptr();
  }};
  server.listen(ZiListenFn{[&listening](const ZiListenInfo &) { listening.post(); }},
    ZiFailFn{[&listenFailures](bool) { ++listenFailures; }},
    ZiConnectFn{[&server, &srv, &srvCxn](const ZiCxnInfo &info) -> ZiConnection * {
      srvCxn = new MxCxn{&server, info, &srv};
      return srvCxn.ptr();
    }}, remote, 18191, 4, serverRx, {});
  bool bound = wait(listening);
  auto path = route(client, clientStack.ptr(), local, remote);
  bool injected = bound && path.mtu &&
    extraInitial(client, clientStack.ptr(), path, 18191, false) &&
    extraInitial(client, clientStack.ptr(), path, 18199, true);
  bool seen = injected && wait(marker);
  auto barrier = route(server, serverStack.ptr(), remote, local);
  auto pending = admission(server, serverStack.ptr());
  ZuCHECK(seen && barrier.mtu && pending.total == 1 && pending.pending == 1 &&
      !srv.connected && !unsigned(listenFailures),
    "unanswered passive setup must own the server's sole pending and total slot");
  client.connect(retry,
    ZiFailFn{[&client, clientStack, &released, &clientHistory, &failures, retry, retryFail,
      local, remote, clientRx](bool transient) mutable {
      ++failures;
      auto slots = clientStack->cxnAdmission();
      released = transient && client.invoked(client.rxThread()) && !slots.total && !slots.pending;
      Zutcp::ClosedTuple record;
      clientHistory = clientStack->closedTuple({ZiSockAddr{local, 18193},
	ZiSockAddr{remote, 18194}}, record) && record.id && !record.fin;
      client.connect(retry, retryFail, local, 18192, remote, 18191, clientRx, {});
    }}, local, 18193, remote, 18194, clientRx, {});
  bool connected = wait(cli.established) && wait(srv.established);
  ZuCHECK(connected && failures == 1 && !unsigned(retryFailures) && released && clientHistory &&
      cli.connected == 1 && srv.connected == 1,
    "timeout must release its slot before the failure callback immediately retries at maxCxns=1");
  if (connected) {
    auto history = ZmBlock<Zutcp::ClosedTuple>{}([
      &server, serverStack, local, remote
    ](auto done) {
      server.rxRun([serverStack, local, remote, done]() mutable {
	Zutcp::ClosedTuple record;
	serverStack->closedTuple({ZiSockAddr{remote, 18191}, ZiSockAddr{local, 18198}}, record);
	done(record);
      });
    });
    bool synSeen = wait(syn.seen);
    ZuCHECK(synSeen && !unsigned(syn.errors) && history.id && !history.fin &&
	!history.rxNext && uint32_t(history.txNext - syn.localBase) == 0,
      "unpublished passive timeout must retain bounded native tuple evidence before retry");
    auto a = admission(client, clientStack.ptr()), b = admission(server, serverStack.ptr());
    ZuCHECK(a.total == 1 && b.total == 1 && !a.pending && !b.pending &&
	!a.refused && !b.refused,
      "both expired setups must retire before the replacement connection consumes their slots");
    cliCxn->sendData(clientTx, 0);
    srvCxn->sendData(serverTx, 0);
    bool sent = wait(cli.sent) && wait(srv.sent);
    bool input = wait(cli.input) && wait(srv.input);
    ZuCHECK(sent && input && cli.valid && srv.valid && cli.txValid && srv.txValid &&
	cli.bytes == Message.length() && srv.bytes == Message.length(),
      "successful replacement must preserve exact native application I/O");
  }
  bool stopped = client.stop() && server.stop();
  auto endClient = clientStack->cxnAdmission(), endServer = serverStack->cxnAdmission();
  ZuCHECK(stopped && !endClient.total && !endClient.pending &&
      !endServer.total && !endServer.pending && failures == 1 && !unsigned(retryFailures) &&
      cli.disconnected == unsigned(connected) && srv.disconnected == unsigned(connected),
    "timeout and replacement teardown must leave no slots or repeated failure notifications");
  client.unwatch();
  server.unwatch();
  cliCxn = nullptr;
  srvCxn = nullptr;
  alarm(0);
}

static void testUserTimeout(bool v6)
{
  ZuTestScope(testUserTimeout);
  alarm(35);
  unsigned a = ZuBox<unsigned>{getenv("ZXDP_RX_TEST_IFINDEX")};
  unsigned b = ZuBox<unsigned>{getenv("ZXDP_RX_TEST_PEER_IFINDEX")};
  ZiIP local{v6 ? "2001:db8::10" : "198.18.0.10"};
  ZiIP remote{v6 ? "2001:db8::11" : "198.18.0.11"};
  ZiMultiplex client{mxParams()}, server{mxParams()};
  ZmRef<Zutcp::Stack> clientStack = new Zutcp::Stack{&client, {.interface = a}};
  ZmRef<Zutcp::Stack> serverStack = new Zutcp::Stack{&server, {.interface = b}};
  client.plugin(clientStack);
  server.plugin(serverStack);
  auto clientRx = client.allocator<Zi::IOBufType::RxTCP>();
  auto serverRx = server.allocator<Zi::IOBufType::RxTCP>();
  auto clientTx = client.allocator<Zi::IOBufType::TxTCP, MxTxBuf, 128>(1);
  Zutcp::StackConfig config{.rxRetainFrames = 96, .rxRetainFramesPerCxn = 64,
    .rxReserveFrames = 16, .maxCxns = 1, .maxPendingCxns = 1, .userTimeout = ZuTime{2}};
  auto serverConfig = config;
  serverConfig.maxData = 0;
  serverConfig.userTimeout = ZuTime{0};
  ZtString<> error;
  bool started = clientStack->configure(config, error) &&
    serverStack->configure(serverConfig, error) && start(client) && start(server);
  ZuCHECK(started, "user timeout fixture must start a sender and zero-window receiver");
  if (!started) { client.stop(); server.stop(); alarm(0); return; }
  MxEvents cli, srv;
  observe(client, cli);
  observe(server, srv);
  ZmRef<MxCxn> cliCxn, srvCxn;
  ZmSemaphore listening;
  ZmAtomic<unsigned> failures = 0;
  auto fail = ZiFailFn{[&failures](bool) { ++failures; }};
  server.listen(ZiListenFn{[&listening](const ZiListenInfo &) { listening.post(); }}, fail,
    ZiConnectFn{[&server, &srv, &srvCxn](const ZiCxnInfo &info) -> ZiConnection * {
      srvCxn = new MxCxn{&server, info, &srv};
      return srvCxn.ptr();
    }}, remote, 18191, 4, serverRx, {});
  bool bound = wait(listening);
  if (bound) client.connect(
    ZiConnectFn{[&client, &cli, &cliCxn](const ZiCxnInfo &info) -> ZiConnection * {
      cliCxn = new MxCxn{&client, info, &cli};
      return cliCxn.ptr();
    }}, fail, local, 18192, remote, 18191, clientRx, {});
  bool connected = bound && wait(cli.established) && wait(srv.established);
  ZuCHECK(connected && !unsigned(failures), "zero advertised credit must permit establishment");
  if (connected) {
    cliCxn->sendData(clientTx, 0, false);
    bool sent = wait(cli.sent);
    ZuCHECK(sent && cli.txValid && cli.txCalls == 1 && !unsigned(cli.destroyed) &&
	!srv.bytes && !cli.bytes,
      "zero credit must retain attached backing after local send admission");
    bool expired = wait(cli.closed);
    bool peerClosed = wait(srv.closed);
    ZuCHECK(expired && peerClosed && cli.disconnected == 1 && !cliCxn->up() &&
	serverStack->ready() && clientStack->ready() && !srvCxn->up() &&
	srv.disconnected == 1 && !cli.peerClosed && srv.peerClosed == 1,
      "user expiry must reset both owners once while keeping packet service up");
  }
  bool stopped = client.stop() && server.stop();
  auto aSlots = clientStack->cxnAdmission(), bSlots = serverStack->cxnAdmission();
  auto pool = static_cast<Zutcp::Allocator *>(clientTx->provider());
  ZuCHECK(stopped && !aSlots.total && !aSlots.pending && !bSlots.total && !bSlots.pending &&
      !pool->pool.used() && (!connected || (cli.destroyed == 1 && !cli.detached &&
	cli.disconnected == 1 && srv.disconnected == 1 && !srv.bytes)) && !unsigned(failures),
    "owner teardown must return the unsent original slot with no repeated notifications");
  client.unwatch();
  server.unwatch();
  cliCxn = nullptr;
  srvCxn = nullptr;
  alarm(0);
}

static void drainOwners(ZiMultiplex &mx)
{
  ZmBlock<>{}([&mx](auto done) {
    mx.rxRun([&mx, done] { mx.txRun([&mx, done] { mx.rxRun(done); }); });
  });
}

static Zutcp::ClosedTuple closedRecord(
  ZiMultiplex &mx, Zutcp::Stack *stack, const Zutcp::ClosedTupleKey &key)
{
  return ZmBlock<Zutcp::ClosedTuple>{}([&mx, stack, key](auto done) {
    mx.rxRun([stack, key, done] () mutable {
      Zutcp::ClosedTuple record;
      stack->closedTuple(key, record);
      done(record);
    });
  });
}

static void testGracefulFin(
  bool v6, bool serverFirst, bool completedReader = false, bool lostAck = false,
  bool finWindow = false)
{
  ZuTestScope(testGracefulFin);
  alarm(35);
  unsigned a = ZuBox<unsigned>{getenv("ZXDP_RX_TEST_IFINDEX")};
  unsigned b = ZuBox<unsigned>{getenv("ZXDP_RX_TEST_PEER_IFINDEX")};
  bool bridged = getenv("ZUTCP_FIN_DIR");
  ZiIP local{v6 ? (bridged ? "2001:db8::20" : "2001:db8::10") :
    (bridged ? "198.18.0.20" : "198.18.0.10")};
  ZiIP remote{v6 ? (bridged ? "2001:db8::21" : "2001:db8::11") :
    (bridged ? "198.18.0.21" : "198.18.0.11")};
  struct LateAck {
    ZmSemaphore event;
    unsigned responses = 0;
    bool enabled = false;
    bool valid = false, timestamps = false;
    uint32_t expectedSeq = 0, expectedAck = 0, expectedEcho = 0, receivedTS = 0;
  } late;
  ZiMultiplex client{mxParams()}, server{mxParams()};
  ZmRef<Zutcp::Stack> clientStack = new Zutcp::Stack{&client, {.interface = a,
    .packet = [&late](ZmRef<ZiIOBuf> packet) {
      Zutcp::IPView ip;
      Zutcp::TCPView tcp;
      if (!late.enabled || !Zutcp::readIP(packet->cspan(), ip) ||
	  !Zutcp::readTCP(ip, tcp) || tcp.header.srcPort != 18191 ||
	  tcp.header.dstPort != 18192 || tcp.header.flags != 0x10 ||
	  tcp.payload.length()) return;
      late.receivedTS = tcp.header.options.tsval;
      late.valid = bool(tcp.header.options.flags & Zutcp::TCPOptions::TS) == late.timestamps &&
	(!late.timestamps || tcp.header.options.tsecr == late.expectedEcho) &&
	tcp.header.seq == late.expectedSeq && tcp.header.ack == late.expectedAck;
      ++late.responses;
      late.event.post();
    }}};
  ZmRef<Zutcp::Stack> serverStack = new Zutcp::Stack{&server, {.interface = b}};
  client.plugin(clientStack);
  server.plugin(serverStack);
  auto clientRx = client.allocator<Zi::IOBufType::RxTCP>();
  auto serverRx = server.allocator<Zi::IOBufType::RxTCP>();
  auto clientTx = client.allocator<Zi::IOBufType::TxTCP, MxTxBuf, 128>(1);
  auto serverTx = server.allocator<Zi::IOBufType::TxTCP, MxTxBuf, 128>(1);
  Zutcp::StackConfig config{.rxRetainFrames = 96, .rxRetainFramesPerCxn = 64,
    .rxReserveFrames = 16, .maxCxns = 1, .maxPendingCxns = 1, .closedTupleRingSize = 1};
  if (finWindow) config.maxData = Message.length();
  ZtString<> error;
  bool started = clientStack->configure(config, error) &&
    serverStack->configure(config, error) && start(client) && start(server);
  ZuCHECK(started, "graceful FIN fixture must start both native packet services");
  if (!started) { client.stop(); server.stop(); alarm(0); return; }
  MxEvents cli, srv;
  cli.holdRx = serverFirst;
  srv.holdRx = !serverFirst;
  observe(client, cli);
  observe(server, srv);
  ZmRef<MxCxn> cliCxn, srvCxn;
  ZmSemaphore listening;
  ZmAtomic<unsigned> failures = 0;
  auto fail = ZiFailFn{[&failures](bool) { ++failures; }};
  server.listen(ZiListenFn{[&listening](const ZiListenInfo &) { listening.post(); }}, fail,
    ZiConnectFn{[&server, &srv, &srvCxn](const ZiCxnInfo &info) -> ZiConnection * {
      srvCxn = new MxCxn{&server, info, &srv};
      return srvCxn.ptr();
    }}, remote, 18191, 4, serverRx, {});
  bool bound = wait(listening);
  if (bound) client.connect(
    ZiConnectFn{[&client, &cli, &cliCxn](const ZiCxnInfo &info) -> ZiConnection * {
      cliCxn = new MxCxn{&client, info, &cli};
      return cliCxn.ptr();
    }}, fail, local, 18192, remote, 18191, clientRx, {});
  bool connected = bound && wait(cli.established) && wait(srv.established);
  ZuCHECK(connected && !unsigned(failures), "both mapped roles must establish before FIN");
  if (connected) {
    ZuCHECK(wireFaultGate(), "SYN-origin fault setup must precede graceful DATA and FIN");
    auto out = serverFirst ? srvCxn.ptr() : cliCxn.ptr();
    auto in = serverFirst ? cliCxn.ptr() : srvCxn.ptr();
    auto &outEvents = serverFirst ? srv : cli;
    auto &inEvents = serverFirst ? cli : srv;
    out->sendData(serverFirst ? serverTx : clientTx, 0);
    bool admitted = wait(outEvents.sent);
    if (completedReader)
      ZmBlock<>{}([out](auto done) {
	out->mx()->rxRun([out, done]() mutable {
	  out->rxContext().complete();
	  done();
	});
      });
    out->disconnect();
    out->disconnect();
    in->sendData(serverFirst ? clientTx : serverTx, 0);
    bool reverse = wait(inEvents.sent) && (completedReader || wait(outEvents.input));
    ZuCHECK(admitted && reverse &&
	outEvents.bytes == (completedReader ? 0 : Message.length()) &&
	!cli.disconnected && !srv.disconnected,
      "local FIN must retain admitted DATA and preserve an armed reverse reader");
    if (finWindow) {
      ZtString<> gate;
      gate << getenv("ZUTCP_FIN_DIR") << "/reopen";
      bool probed = readGate(gate);
      ZuCHECK(probed && !outEvents.disconnected && !inEvents.disconnected,
	"captured zero-window persist must precede receiver reopening and FIN completion");
    }
    in->resumeRx();
    bool delivered = wait(inEvents.input);
    bool retired;
    if (lostAck) {
      bool first = wait(outEvents.closed);
      drainOwners(serverFirst ? server : client);
      auto stack = serverFirst ? serverStack.ptr() : clientStack.ptr();
      auto &mx = serverFirst ? server : client;
      Zutcp::ClosedTupleKey key = serverFirst ?
	Zutcp::ClosedTupleKey{ZiSockAddr{remote, 18191}, ZiSockAddr{local, 18192}} :
	Zutcp::ClosedTupleKey{ZiSockAddr{local, 18192}, ZiSockAddr{remote, 18191}};
      auto before = closedRecord(mx, stack, key);
      bool bounded = first && before.id && before.fin && !admission(mx, stack).total;
      ZuCHECK(bounded && !inEvents.disconnected,
	"final ACK sender must retire while the peer still awaits its FIN acknowledgment");
      ZtString<> marker;
      marker << getenv("ZUTCP_FIN_DIR") << "/retired";
      bool marked = bounded && writeFile(marker);
      ZuCHECK(marked, "fixture must release ACK loss only after native owner retirement");
      retired = first && marked && wait(inEvents.closed);
      auto after = closedRecord(mx, stack, key);
      ZuCHECK(retired && after.id == before.id &&
	!admission(mx, stack).total,
	"peer FIN retransmission must complete through retired history without a new owner");
    } else retired = wait(cli.closed) && wait(srv.closed);
    drainOwners(client);
    drainOwners(server);
    auto ca = admission(client, clientStack.ptr()), sa = admission(server, serverStack.ptr());
    auto c = closedRecord(client, clientStack.ptr(), {ZiSockAddr{local, 18192},
      ZiSockAddr{remote, 18191}});
    auto s = closedRecord(server, serverStack.ptr(), {ZiSockAddr{remote, 18191},
      ZiSockAddr{local, 18192}});
    ZuCHECK(delivered && retired && (!completedReader || !outEvents.received) &&
	cli.valid && srv.valid && cli.disconnected == 1 &&
	srv.disconnected == 1 && !cliCxn->plugin() && !srvCxn->plugin() &&
	!ca.total && !sa.total && c.id && s.id && c.fin && s.fin &&
	cli.baseKnown && srv.baseKnown &&
	uint32_t(c.txNext - cli.localBase) == Message.length() + 1 &&
	uint32_t(c.rxNext - srv.localBase) == Message.length() + 1 &&
	s.txNext == c.rxNext && s.rxNext == c.txNext,
      "FIN/ACK completion must destroy full owners and preserve only bounded closed evidence");
    if (retired && c.id && s.id) {
      ZmBlock<>{}([&client, clientStack, c, s, &late](auto done) {
	client.rxRun([clientStack, c, s, &late, done] () mutable {
	  // Evict only this fixture's client-side history so its ordinary
	  // packet observer can inspect the server's late-ACK response.
	  clientStack->rememberClosed({.key = {ZiSockAddr{ZiIP{"198.18.0.90"}, 1},
	    ZiSockAddr{ZiIP{"198.18.0.91"}, 2}}});
	  late.enabled = true;
	  late.expectedSeq = s.txNext;
	  late.expectedAck = s.rxNext;
	  late.timestamps = s.timestamps;
	  late.expectedEcho = replayOptions(s, c).tsval;
	  done();
	});
      });
      auto path = route(client, clientStack.ptr(), local, remote);
      auto replay = [&client, clientStack, path, c, s](bool data, bool pureAck) {
	return ZmBlock<bool>{}([&client, clientStack, path, c, s, data, pureAck](auto done) {
	  client.txRun([clientStack, path, c, s, data, pureAck, done]() mutable {
	    auto packet = clientStack->allocPacket(clientStack.ptr(), path.local.type(), true);
	    if (!packet) { done(false); return; }
	    uint32_t seq = s.rxNext - 1;
	    if (data || pureAck) seq -= Message.length();
	    Zutcp::TCPHeader tcp{.srcPort = 18192, .dstPort = 18191,
	      .seq = seq, .ack = s.txNext, .window = uint16_t(pureAck ? 65535 : 0),
	      .flags = uint8_t(pureAck ? 0x10 : data ? 0x18 : 0x11),
	      .options = replayOptions(s, c)};
	    if (!reserveOptions(packet.ptr(), tcp)) { done(false); return; }
	    unsigned bytes = data ? unsigned(Message.length()) : 0;
	    if (bytes) memcpy(packet->data(), Message.data(), bytes);
	    unsigned wire = Zutcp::writeTCP(packet->data_(), packet->size, path, tcp, bytes);
	    packet->skip = 0;
	    packet->length = wire;
	    done(wire && clientStack->sendPacket(packet));
	  });
	});
      };
      // The ordered pure ACK must elicit no response. The subsequent old
      // DATA must elicit the one final cumulative ACK from the owner-free ring.
      bool dataReplayed = replay(false, true) && replay(true, false);
      bool dataReply = dataReplayed && wait(late.event);
      drainOwners(server);
      auto afterData = closedRecord(server, serverStack.ptr(), s.key);
      ZuCHECK(dataReply && late.valid && afterData.id == s.id &&
	  late.responses == 1 && afterData.txNext == s.txNext &&
	  afterData.rxNext == s.rxNext && afterData.timestamps == s.timestamps &&
	  (!s.timestamps || (afterData.tsRecent == replayOptions(s, c).tsval &&
	    *afterData.tsRecentTime && afterData.lastSentTS == late.receivedTS)) &&
	  !admission(server, serverStack.ptr()).total,
	"retired history must ACK old DATA once, ignore pure ACKs and preserve wire frontiers");
      ZmBlock<>{}([&client, &late, afterData](auto done) {
	client.rxRun([&late, afterData, done]() mutable {
	  late.valid = false;
	  done();
	});
      });
      bool replayed = replay(false, false);
      bool reply = replayed && wait(late.event);
      drainOwners(server);
      auto updated = closedRecord(server, serverStack.ptr(), s.key);
      ZuCHECK(reply && late.valid && updated.id == s.id && late.responses == 2 &&
	  updated.txNext == s.txNext && updated.rxNext == s.rxNext &&
	  updated.timestamps == s.timestamps && (!s.timestamps ||
	    (updated.tsRecent == replayOptions(s, c).tsval && *updated.tsRecentTime &&
	      updated.lastSentTS == late.receivedTS)) &&
	  !admission(server, serverStack.ptr()).total && !unsigned(failures),
	"retired server must also ACK repeated FIN without resurrecting owner or FIFO position");
    }
  }
  bool stopped = client.stop() && server.stop();
  ZuCHECK(stopped && !unsigned(failures) && cli.disconnected == unsigned(connected) &&
      srv.disconnected == unsigned(connected), "graceful close and final stop must notify once");
  client.unwatch();
  server.unwatch();
  cliCxn = nullptr;
  srvCxn = nullptr;
  alarm(0);
}

static void testMappedLoss(
  bool v6, bool window = false, bool restart = false, bool lostWindow = false)
{
  ZuTestScopeRT(testMappedLoss);
  Zi::Name name;
  name << (lostWindow ? "mapped-window-loss-" :
    restart ? "mapped-restart-" : window ? "mapped-window-" : "mapped-loss-")
    << (v6 ? "v6" : "v4");
  auto directory = ZiTestResidue::dir(name);
  auto child = fork();
  if (!child) {
    execl("/bin/bash", "bash", "./zutcpmxlossfixture.sh", v6 ? "6" : "4",
      directory.data(), lostWindow ? "lost-window" : restart ? "restart" :
      window ? "window" : "loss",
      static_cast<char *>(nullptr));
    _exit(127);
  }
  int status = -1;
  if (child > 0) {
    pid_t result;
    do { result = waitpid(child, &status, 0); } while (result < 0 && errno == EINTR);
    if (result != child) status = -1;
  }
  ZuCheckRT(!status, log_("mapped IPv", v6 ? 6 : 4,
    lostWindow ? " lost opening advertisement with persist recovery; logs " :
      restart ? " restart with changed window settings; logs " :
      window ? " zero window/persist/reopening; logs " :
      " DATA loss with one-slot producer pools; logs ", directory));
}

static void testFinalAckLoss(bool v6, bool serverFirst)
{
  ZuTestScopeRT(testFinalAckLoss);
  Zi::Name name;
  name << "final-ack-" << (serverFirst ? "server-" : "client-") << (v6 ? "v6" : "v4");
  auto directory = ZiTestResidue::dir(name);
  auto child = fork();
  if (!child) {
    execl("/bin/bash", "bash", "./zutcpmxlossfixture.sh", v6 ? "6" : "4",
      directory.data(), serverFirst ? "fin-server" : "fin-client",
      static_cast<char *>(nullptr));
    _exit(127);
  }
  int status = -1;
  if (child > 0) {
    pid_t result;
    do { result = waitpid(child, &status, 0); } while (result < 0 && errno == EINTR);
    if (result != child) status = -1;
  }
  ZuCheckRT(!status, log_("lost final ACK with retired owner; logs ", directory));
}

static void testFinWindow(bool v6, bool serverFirst)
{
  ZuTestScopeRT(testFinWindow);
  Zi::Name name;
  name << "fin-window-" << (serverFirst ? "server-" : "client-") << (v6 ? "v6" : "v4");
  auto directory = ZiTestResidue::dir(name);
  auto child = fork();
  if (!child) {
    execl("/bin/bash", "bash", "./zutcpmxlossfixture.sh", v6 ? "6" : "4",
      directory.data(), serverFirst ? "fin-window-server" : "fin-window-client",
      static_cast<char *>(nullptr));
    _exit(127);
  }
  int status = -1;
  if (child > 0) {
    pid_t result;
    do { result = waitpid(child, &status, 0); } while (result < 0 && errno == EINTR);
    if (result != child) status = -1;
  }
  ZuCheckRT(!status, log_("FIN admission at exhausted window; logs ", directory));
}

static void testResetPair(bool v6, bool serverFirst)
{
  ZuTestScope(testResetPair);
  alarm(35);
  unsigned a = ZuBox<unsigned>{getenv("ZXDP_RX_TEST_IFINDEX")};
  unsigned b = ZuBox<unsigned>{getenv("ZXDP_RX_TEST_PEER_IFINDEX")};
  ZiIP local{v6 ? "2001:db8::20" : "198.18.0.20"};
  ZiIP remote{v6 ? "2001:db8::21" : "198.18.0.21"};
  ZiMultiplex client{mxParams()}, server{mxParams()};
  ZmRef<Zutcp::Stack> clientStack = new Zutcp::Stack{&client, {.interface = a}};
  ZmRef<Zutcp::Stack> serverStack = new Zutcp::Stack{&server, {.interface = b}};
  client.plugin(clientStack);
  server.plugin(serverStack);
  auto clientRx = client.allocator<Zi::IOBufType::RxTCP>();
  auto serverRx = server.allocator<Zi::IOBufType::RxTCP>();
  auto clientTx = client.allocator<Zi::IOBufType::TxTCP, MxTxBuf, 128>(1);
  auto serverTx = server.allocator<Zi::IOBufType::TxTCP, MxTxBuf, 128>(1);
  Zutcp::StackConfig config{.rxRetainFrames = 96, .rxRetainFramesPerCxn = 64,
    .rxReserveFrames = 16, .maxCxns = 1, .maxPendingCxns = 1};
  ZtString<> error;
  bool started = clientStack->configure(config, error) &&
    serverStack->configure(config, error) && start(client) && start(server);
  ZuCHECK(started, "RST fixture must start both raw packet services");
  if (!started) { client.stop(); server.stop(); alarm(0); return; }
  MxEvents cli[2], srv[2];
  ZmRef<MxCxn> cliCxn[2], srvCxn[2];
  ZmAtomic<unsigned> phase = 0, failures = 0;
  ZmSemaphore listening;
  auto fail = ZiFailFn{[&failures](bool) { ++failures; }};
  server.listen(ZiListenFn{[&listening](const ZiListenInfo &) { listening.post(); }}, fail,
    ZiConnectFn{[&server, &srv, &srvCxn, &phase](const ZiCxnInfo &info) -> ZiConnection * {
      unsigned i = phase.load_();
      srvCxn[i] = new MxCxn{&server, info, &srv[i]};
      return srvCxn[i].ptr();
    }}, remote, 18191, 4, serverRx, {});
  bool bound = wait(listening), retired = false;
  for (unsigned i = 0; bound && i < 2; ++i) {
    phase = i;
    client.connect(ZiConnectFn{[&client, &cli, &cliCxn, i](
      const ZiCxnInfo &info) -> ZiConnection * {
      cliCxn[i] = new MxCxn{&client, info, &cli[i]};
      return cliCxn[i].ptr();
    }}, fail, local, 18192 + i, remote, 18191, clientRx, {});
    bool connected = wait(cli[i].established) && wait(srv[i].established);
    ZuCHECK(connected && !unsigned(failures) && cli[i].valid && srv[i].valid,
      "the same listener must establish the original and fresh connection");
    if (!connected) break;
    if (!i) {
      auto out = serverFirst ? srvCxn[i].ptr() : cliCxn[i].ptr();
      auto &events = serverFirst ? srv[i] : cli[i];
      auto allocator = serverFirst ? serverTx : clientTx;
      auto pool = static_cast<Zutcp::Allocator *>(allocator->provider());
      out->sendData(allocator, 0);
      bool sent = wait(events.sent);
      ZtString<> gate;
      gate << getenv("ZUTCP_RST_DIR") << "/close";
      bool dropped = readGate(gate);
      bool retained = ZmBlock<bool>{}([out, pool, &events](auto done) {
	out->mx()->txRun([pool, &events, done]() mutable {
	  done(events.txValid && events.detached && events.destroyed == 1 &&
	    events.txCalls == 1 && pool->pool.used() == 1);
	});
      });
      ZuCHECK(sent && dropped && retained && !cli[i].received && !srv[i].received,
	"wire loss must leave original backing retained after admitted metadata destruction");
      out->close();
      out->close();
    } else {
      cliCxn[i]->sendData(clientTx, 0);
      srvCxn[i]->sendData(serverTx, 0);
      bool exchanged = wait(cli[i].sent) && wait(srv[i].sent) &&
	wait(cli[i].input) && wait(srv[i].input);
      ZuCHECK(exchanged && cli[i].valid && srv[i].valid && cli[i].txValid &&
	  srv[i].txValid && cli[i].bytes == Message.length() &&
	  srv[i].bytes == Message.length(),
	"fresh tuple must exchange exact bytes through reused original application pools");
      cliCxn[i]->disconnect();
    }
    retired = wait(cli[i].closed) && wait(srv[i].closed);
    drainOwners(client);
    drainOwners(server);
    ZuCHECK(retired && cli[i].disconnected == 1 && srv[i].disconnected == 1 &&
	cli[i].peerClosed == unsigned(!i && serverFirst) &&
	srv[i].peerClosed == unsigned(!i && !serverFirst) + unsigned(bool(i)) &&
	!cliCxn[i]->plugin() && !srvCxn[i]->plugin() &&
	!admission(client, clientStack.ptr()).total &&
	!admission(server, serverStack.ptr()).total,
      "RST or graceful completion must release both owners and notify each role once");
    if (!retired) break;
  }
  bool stopped = client.stop() && server.stop();
  auto cp = static_cast<Zutcp::Allocator *>(clientTx->provider());
  auto sp = static_cast<Zutcp::Allocator *>(serverTx->provider());
  ZuCHECK(stopped && retired && !unsigned(failures) && !cp->pool.used() && !sp->pool.used() &&
      cli[0].disconnected == 1 && srv[0].disconnected == 1 &&
      cli[1].disconnected == 1 && srv[1].disconnected == 1,
    "reset then reconnect must preserve listener, once-only notifications "
    "and backing retirement");
  alarm(0);
}

static void testReset(bool v6, bool serverFirst)
{
  ZuTestScopeRT(testReset);
  Zi::Name name;
  name << "rst-" << (serverFirst ? "server-" : "client-") << (v6 ? "v6" : "v4");
  auto directory = ZiTestResidue::dir(name);
  auto child = fork();
  if (!child) {
    execl("/bin/bash", "bash", "./zutcpmxlossfixture.sh", v6 ? "6" : "4",
      directory.data(), serverFirst ? "rst-server" : "rst-client",
      static_cast<char *>(nullptr));
    _exit(127);
  }
  int status = -1;
  if (child > 0) {
    pid_t result;
    do { result = waitpid(child, &status, 0); } while (result < 0 && errno == EINTR);
    if (result != child) status = -1;
  }
  ZuCheckRT(!status, log_(
    "abrupt close with outstanding backing and reconnect; logs ", directory));
}

static void testAbortPair(bool v6, bool reject, bool serverTarget)
{
  ZuTestScope(testAbortPair);
  alarm(35);
  unsigned a = ZuBox<unsigned>{getenv("ZXDP_RX_TEST_IFINDEX")};
  unsigned b = ZuBox<unsigned>{getenv("ZXDP_RX_TEST_PEER_IFINDEX")};
  ZiIP local{v6 ? "2001:db8::20" : "198.18.0.20"};
  ZiIP remote{v6 ? "2001:db8::21" : "198.18.0.21"};
  ZiMultiplex client{mxParams()}, server{mxParams()};
  ZmRef<Zutcp::Stack> clientStack = new Zutcp::Stack{&client, {.interface = a}};
  ZmRef<Zutcp::Stack> serverStack = new Zutcp::Stack{&server, {.interface = b}};
  client.plugin(clientStack);
  server.plugin(serverStack);
  auto clientRx = client.allocator<Zi::IOBufType::RxTCP>();
  auto serverRx = server.allocator<Zi::IOBufType::RxTCP>();
  auto clientTx = client.allocator<Zi::IOBufType::TxTCP, MxTxBuf, 128>(1);
  auto serverTx = server.allocator<Zi::IOBufType::TxTCP, MxTxBuf, 128>(1);
  Zutcp::StackConfig config{.rxRetainFrames = 96, .rxRetainFramesPerCxn = 64,
    .rxReserveFrames = 16, .maxCxns = 2, .maxPendingCxns = 1};
  ZtString<> error;
  bool started = clientStack->configure(config, error) &&
    serverStack->configure(config, error) && start(client) && start(server);
  ZuCHECK(started, "abort entrypoint fixture must start both raw packet services");
  if (!started) { client.stop(); server.stop(); alarm(0); return; }
  MxEvents cli[2], srv[2];
  ZmRef<MxCxn> cliCxn[2], srvCxn[2];
  ZmAtomic<unsigned> phase = 0, failures = 0, rejected = 0, setupFailed[2];
  for (auto &failed : setupFailed) failed = 0;
  ZmSemaphore listening, rejection;
  auto fail = ZiFailFn{[&failures](bool) { ++failures; }};
  server.listen(ZiListenFn{[&listening](const ZiListenInfo &) { listening.post(); }}, fail,
    ZiConnectFn{[&server, &srv, &srvCxn, &phase, &rejected, &rejection, reject](
      const ZiCxnInfo &info) -> ZiConnection * {
      unsigned i = phase.load_();
      if (reject && !i) {
	++rejected;
	rejection.post();
	return nullptr;
      }
      srvCxn[i] = new MxCxn{&server, info, &srv[i]};
      return srvCxn[i].ptr();
    }}, remote, 18191, 4, serverRx, {});
  bool bound = wait(listening), retired = false;
  for (unsigned i = 0; bound && i < 2; ++i) {
    phase = i;
    client.connect(ZiConnectFn{[&client, &cli, &cliCxn, i](
      const ZiCxnInfo &info) -> ZiConnection * {
      cliCxn[i] = new MxCxn{&client, info, &cli[i]};
      return cliCxn[i].ptr();
    }}, ZiFailFn{[&cli, &setupFailed, i](bool) {
      ++setupFailed[i];
      cli[i].closed.post();
    }}, local, 18192 + i, remote, 18191, clientRx, {});
    if (reject && !i) {
      // The wire RST may arrive before or after client publication. Both
      // outcomes must terminate through the existing callback exactly once.
      retired = wait(rejection) && wait(cli[i].closed);
      ZuCHECK(retired && rejected == 1 && !srvCxn[i] && !srv[i].connected &&
	  !srv[i].disconnected && unsigned(setupFailed[i]) + cli[i].disconnected == 1 &&
	  (!cliCxn[i] || (!cliCxn[i]->plugin() && cli[i].peerClosed == 1)),
	"NULL server publication must reset and finish setup or the published peer once");
    } else {
      bool connected = wait(cli[i].established) && wait(srv[i].established);
      ZuCHECK(connected && !unsigned(setupFailed[i]) && !unsigned(failures) &&
	  cli[i].valid && srv[i].valid,
	"fatal-input fixture and the same surviving listener must publish both roles");
      if (!connected) break;
      if (!i) {
	ZuCHECK(wireFaultGate(), "SYN-origin fault setup must precede fatal-input DATA");
	auto out = serverTarget ? cliCxn[i].ptr() : srvCxn[i].ptr();
	auto &sentEvents = serverTarget ? cli[i] : srv[i];
	auto &inputEvents = serverTarget ? srv[i] : cli[i];
	out->sendData(serverTarget ? clientTx : serverTx, 0);
	bool delivered = wait(sentEvents.sent) && wait(inputEvents.input);
	auto allocator = serverTarget ? clientTx : serverTx;
	auto pool = static_cast<Zutcp::Allocator *>(allocator->provider());
	bool retained = ZmBlock<bool>{}([out, pool, &sentEvents](auto done) {
	  out->mx()->txRun([pool, &sentEvents, done]() mutable {
	    done(sentEvents.txValid && sentEvents.detached && sentEvents.destroyed == 1 &&
	      sentEvents.txCalls == 1 && pool->pool.used() == 1);
	  });
	});
	ZtString<> path;
	path << getenv("ZUTCP_ABORT_DIR") << "/ready";
	bool ready = delivered && writeFile(path, "R", ZiFile::WriteOnly);
	path = ZtString<>{} << getenv("ZUTCP_ABORT_DIR") << "/injected";
	bool injected = ready && readGate(path);
	ZuCHECK(delivered && retained && ready && injected &&
	    inputEvents.bytes == Message.length(),
	  "injection must follow real DATA delivery, retained original output "
	  "and acknowledgment of out-of-order DATA");
      } else {
	auto ca = admission(client, clientStack.ptr()), sa = admission(server, serverStack.ptr());
	ZuCHECK(ca.total == 1 && !ca.pending && sa.total == 1 && !sa.pending,
	  "fresh establishment must find no leaked owner from rejection or fatal input");
	cliCxn[i]->sendData(clientTx, 0);
	srvCxn[i]->sendData(serverTx, 0);
	bool exchanged = wait(cli[i].sent) && wait(srv[i].sent) &&
	  wait(cli[i].input) && wait(srv[i].input);
	ZuCHECK(exchanged && cli[i].valid && srv[i].valid && cli[i].txValid &&
	    srv[i].txValid && cli[i].bytes == Message.length() &&
	    srv[i].bytes == Message.length(),
	  "fresh tuple must exchange exact bytes through the surviving listener "
	  "and reused pools");
	cliCxn[i]->disconnect();
      }
      retired = wait(cli[i].closed) && wait(srv[i].closed);
      ZuCHECK(retired && cli[i].disconnected == 1 && srv[i].disconnected == 1 &&
	  !cliCxn[i]->plugin() && !srvCxn[i]->plugin() &&
	  (i || (cli[i].peerClosed == unsigned(serverTarget) &&
		 srv[i].peerClosed == unsigned(!serverTarget))),
	"fatal input or graceful completion must clear both associations and notify once");
    }
    drainOwners(client);
    drainOwners(server);
    if (!retired) break;
  }
  bool stopped = client.stop() && server.stop();
  auto cp = static_cast<Zutcp::Allocator *>(clientTx->provider());
  auto sp = static_cast<Zutcp::Allocator *>(serverTx->provider());
  ZuCHECK(stopped && retired && !unsigned(failures) && !cp->pool.used() && !sp->pool.used() &&
      cli[1].disconnected == 1 && srv[1].disconnected == 1 &&
      !unsigned(setupFailed[1]) && (!reject || rejected == 1),
    "meaningful abort then reconnect must drain original pools without late callbacks");
  alarm(0);
}

static void testAbort(bool v6, bool reject, bool serverTarget)
{
  ZuTestScopeRT(testAbort);
  Zi::Name name;
  name << (reject ? "reject-" : "fatal-") << (serverTarget ? "server-" : "client-") <<
    (v6 ? "v6" : "v4");
  auto directory = ZiTestResidue::dir(name);
  auto child = fork();
  if (!child) {
    execl("/bin/bash", "bash", "./zutcpmxlossfixture.sh", v6 ? "6" : "4",
      directory.data(), reject ? "abort-reject" :
      serverTarget ? "abort-fatal-server" : "abort-fatal-client",
      static_cast<char *>(nullptr));
    _exit(127);
  }
  int status = -1;
  if (child > 0) {
    pid_t result;
    do { result = waitpid(child, &status, 0); } while (result < 0 && errno == EINTR);
    if (result != child) status = -1;
  }
  ZuCheckRT(!status, log_("publication/fatal-input abort and listener reuse; logs ", directory));
}

struct HistoryEvents {
  ZmSemaphore established, input, eof, held, txDone, closed, down, destroyed, progress, failed;
  ZmAtomic<unsigned> errors = 0, connected = 0, disconnected = 0, completed = 0, downs = 0;
  ZmAtomic<unsigned> destroyedCount = 0, failures = 0, peerClosed = 0;
  ZmAtomic<unsigned> expectFailure = 0, failureFree = 0, routedProbes = 0;
  const ZiIOBuf *probeInput = nullptr; // Rx-owned identity, no lifetime reference.
  Zutcp::TCPHeader initial;
  ZuCSpan expected;
  ZiSockAddr actualLocal, actualPeer;
  Zutcp::Stack *failureStack = nullptr;
  unsigned bytes = 0, windowRead = unsigned(-1), windowHeld = 0;
  bool hold = false, heldPosted = false, inputPosted = false, eofPosted = false;
  bool progressed = false;
};

// Declared before each native Cxn base, so this destructor runs after the
// complete protocol/endpoint base chain. Final application references are
// released on an I/O owner; its deferred queue turn follows the heap base
// destructor and operator delete too.
struct HistoryDtor {
  ZiMultiplex *mx;
  HistoryEvents *events;
  HistoryDtor(ZiMultiplex *mx_, HistoryEvents *events_) : mx{mx_}, events{events_} { }
  ~HistoryDtor() {
    unsigned owner = mx->invoked(mx->rxThread()) ? mx->rxThread() :
      mx->invoked(mx->txThread()) ? mx->txThread() : 0;
    ZmAssert_(owner);
    mx->run([events = events] {
      ++events->destroyedCount;
      events->destroyed.post();
    }, owner);
  }
};

template <typename C> static int historyInput(
  C *cxn, HistoryEvents &events, Zutcp::Reader &reader)
{
  auto length = reader.length();
  auto expected = events.expected.length() ? events.expected : Message;
  if (length && events.bytes >= events.windowRead) {
    unsigned next = events.bytes + length;
    if (next != events.windowHeld) {
      events.windowHeld = next;
      events.held.post();
    }
    return 0;
  }
  if (events.hold) {
    if (length && cxn->finReceived() && cxn->rxComplete() && !events.heldPosted) {
      events.heldPosted = true;
      events.held.post();
    }
    return 0;
  }
  if (length) {
    auto n = reader.each(length, [&events, expected](auto part) -> int64_t {
      if (events.bytes > expected.length() || part.length() > expected.length() - events.bytes ||
	  memcmp(part.data(), expected.data() + events.bytes, part.length())) ++events.errors;
      events.bytes += part.length();
      return part.length();
    });
    reader.clean();
    if (n != int64_t(length) || !cxn->retireRx(length)) ++events.errors;
    if (!events.progressed) {
      events.progressed = true;
      events.progress.post();
    }
    if (events.bytes == expected.length() && !events.inputPosted) {
      events.inputPosted = true;
      events.input.post();
    }
  }
  if (!reader.length() && cxn->finReceived() && cxn->rxComplete() && !events.eofPosted) {
    events.eofPosted = true;
    events.eof.post();
  }
  return length ? 1 : 0;
}

struct HistoryClient : Zutcp::Client<HistoryClient> { };
class HistorySrv;
struct HistoryServer : Zutcp::Server<HistoryServer, HistorySrv> {
  using Base = Zutcp::Server<HistoryServer, HistorySrv>;
  ZiIP bindIP;
  uint16_t bindPort;
  HistoryEvents events[3];
  ZmRef<HistorySrv> kept[3];
  unsigned acceptedCount = 0;
  ZmAtomic<unsigned> errors = 0, rejectedFactories = 0;
  bool rejectCandidates = false;
  explicit HistoryServer(bool v6, uint16_t port = 18191) :
    bindIP{v6 ? "::" : "0.0.0.0"}, bindPort{port} { }
  ZiIP localIP() const { return bindIP; }
  uint16_t localPort() const { return bindPort; }
  ZmRef<HistorySrv> accepted(const Zutcp::InitialInfo &);
  void disconnected(HistorySrv *, bool);
  void listenFailed(bool) { ++errors; }
};

class HistoryCli : public ZmNHeap<HistoryCli>, public HistoryDtor,
  public Zutcp::CliCxn<HistoryClient, HistoryCli> {
friend inline ZuStringT<"Zutcp.Test.HistoryCli"> ZmNHeapID(HistoryCli *);
  using Base = Zutcp::CliCxn<HistoryClient, HistoryCli>;
public:
  using ZmNHeap<HistoryCli>::operator new;
  using ZmNHeap<HistoryCli>::operator delete;
  HistoryCli(HistoryClient *app, HistoryEvents *events, ZiSockAddr bind) :
    HistoryDtor{app->mx(), events}, Base{app}, m_events{events}, m_bind{bind} { }
  ZiSockAddr localBind() const { return m_bind; }
  void connected(Zutcp::Connected) {
    if (!app()->rxInvoked() || mxCxn) ++m_events->errors;
    ++m_events->connected;
    m_events->established.post();
  }
  int process(Zutcp::Reader &reader) { return historyInput(this, *m_events, reader); }
  void txComplete(bool ok) {
    if (!app()->txInvoked() || (!ok && !m_events->expectFailure.load_())) ++m_events->errors;
    ++m_events->completed;
    m_events->txDone.post();
  }
  void disconnected(bool peer) {
    if (!app()->rxInvoked()) ++m_events->errors;
    m_events->peerClosed += unsigned(peer);
    ++m_events->disconnected;
    m_events->closed.post();
  }
  void endpointDown() {
    if (!app()->txInvoked()) ++m_events->errors;
    ++m_events->downs;
    m_events->down.post();
  }
  void connectFailed(bool) {
    if (!m_events->expectFailure.load_()) ++m_events->errors;
    if (auto stack = m_events->failureStack) {
      auto slots = stack->cxnAdmission();
      if (!slots.total && !slots.pending) ++m_events->failureFree;
      else ++m_events->errors;
    }
    ++m_events->failures;
    m_events->failed.post();
    m_events->established.post();
  }
  bool outstandingTx() const {
    ZiAssert(app()->txInvoked(), "Zutcp", (), "native outstanding inspection outside Tx",
      return false);
    return Base::txNext() == Message.length() && !Base::txBufferedBytes() &&
      Base::txStillUnackd(0, Message.length()) && !Base::txDrained();
  }
  bool windowTx(unsigned ack) const {
    ZiAssert(app()->txInvoked(), "Zutcp", (), "native window inspection outside Tx",
      return false);
    return Base::txNext() == 1000 && Base::txAckd() == ack &&
      Base::txCreditLimit() == ack && !Base::txCreditAvailable() &&
      !Base::txBufferedBytes() && Base::txStillUnackd(ack, 1000 - ack) &&
      Base::finSent() && !Base::finDequeued();
  }
  Zutcp::TCPHeader wireHeaderTx() const {
    ZiAssert(app()->txInvoked(), "Zutcp", (), "wire header inspection outside Tx", return {});
    return Base::tcpHeader();
  }
  bool quietTx() const {
    ZiAssert(app()->txInvoked(), "Zutcp", (),
      "native history inspection outside Tx", return false);
    return !Base::timersActive() && Base::txDrained() && Base::closed();
  }
  bool writeOpenTx() const {
    ZiAssert(app()->txInvoked(), "Zutcp", (), "native write inspection outside Tx", return false);
    return !Base::finSent();
  }
private:
  HistoryEvents *m_events;
  ZiSockAddr m_bind;
};

class HistorySrv : public ZmNHeap<HistorySrv>, public HistoryDtor,
  public Zutcp::SrvCxn<HistoryServer, HistorySrv> {
friend inline ZuStringT<"Zutcp.Test.HistorySrv"> ZmNHeapID(HistorySrv *);
  using Base = Zutcp::SrvCxn<HistoryServer, HistorySrv>;
public:
  using ZmNHeap<HistorySrv>::operator new;
  using ZmNHeap<HistorySrv>::operator delete;
  HistorySrv(HistoryServer *app, HistoryEvents *events) :
    HistoryDtor{app->mx(), events}, Base{app}, m_events{events} { }
  void connected(Zutcp::Connected) {
    if (!app()->rxInvoked() || mxCxn) ++m_events->errors;
    ++m_events->connected;
    m_events->established.post();
  }
  int process(Zutcp::Reader &reader) { return historyInput(this, *m_events, reader); }
  bool receivedRouted(Zutcp::Datagram d) {
    if (m_events->probeInput && d.buf.ptr() == m_events->probeInput) ++m_events->routedProbes;
    return Base::receivedRouted(ZuMv(d));
  }
  void txComplete(bool ok) {
    if (!app()->txInvoked() || (!ok && !m_events->expectFailure.load_())) ++m_events->errors;
    ++m_events->completed;
    m_events->txDone.post();
  }
  void retired(bool peer) {
    if (!app()->rxInvoked()) ++m_events->errors;
    m_events->peerClosed += unsigned(peer);
    ++m_events->disconnected;
    m_events->closed.post();
  }
  void releaseReaderRx() {
    m_events->hold = false;
    (void)Base::processRx();
  }
  bool windowRx(unsigned next) const {
    ZiAssert(app()->rxInvoked(), "Zutcp", (), "native window inspection outside Rx",
      return false);
    return Base::rxBytes() == next && Base::rxDataCreditUsed() == next &&
      m_events->windowHeld == next &&
      (next == 1000 || !Base::rxCreditAvailable());
  }
  void releaseWindowRx(unsigned next) {
    m_events->windowRead = next;
    (void)Base::processRx();
  }
  bool outstandingTx() const {
    ZiAssert(app()->txInvoked(), "Zutcp", (), "native outstanding inspection outside Tx",
      return false);
    return Base::txNext() == Message.length() && !Base::txBufferedBytes() &&
      Base::txStillUnackd(0, Message.length()) && !Base::txDrained();
  }
  Zutcp::TCPHeader wireHeaderTx() const {
    ZiAssert(app()->txInvoked(), "Zutcp", (), "wire header inspection outside Tx", return {});
    return Base::tcpHeader();
  }
  bool quietTx() const {
    ZiAssert(app()->txInvoked(), "Zutcp", (),
      "native history inspection outside Tx", return false);
    return !Base::timersActive() && Base::txDrained() && Base::closed();
  }
  bool writeOpenTx() const {
    ZiAssert(app()->txInvoked(), "Zutcp", (), "native write inspection outside Tx", return false);
    return !Base::finSent();
  }
private:
  HistoryEvents *m_events;
};

ZmRef<HistorySrv> HistoryServer::accepted(const Zutcp::InitialInfo &info)
{
  if (rejectCandidates) { ++rejectedFactories; return {}; }
  if (acceptedCount >= 3) { ++errors; return {}; }
  unsigned i = acceptedCount++;
  events[i].initial = info.header;
  events[i].actualLocal = info.local;
  events[i].actualPeer = info.peer;
  kept[i] = new HistorySrv{this, &events[i]};
  return kept[i];
}
void HistoryServer::disconnected(HistorySrv *cxn, bool peer)
{
  ZmRef<HistorySrv> keep{cxn};
  Base::disconnected(cxn, peer);
  cxn->retired(peer);
}

template <typename C> static Zutcp::TCPHeader historyBases(ZiMultiplex &mx, ZmRef<C> cxn)
{
  return ZmBlock<Zutcp::TCPHeader>{}([&mx, cxn](auto done) {
    mx.txRun([cxn, done]() mutable { done(cxn->wireHeaderTx()); });
  });
}

template <typename C> static bool historyQuiet(ZiMultiplex &mx, ZmRef<C> cxn)
{
  return ZmBlock<bool>{}([&mx, cxn](auto done) {
    mx.txRun([cxn, done]() mutable { done(cxn->quietTx()); });
  });
}
static bool historyUnbound(ZiMultiplex &mx, ZmRef<HistoryCli> cxn)
{
  if (!cxn) return true;
  return ZmBlock<bool>{}([&mx, cxn](auto done) {
    mx.rxRun([cxn, done]() mutable { done(!cxn->cxn()); });
  });
}
static unsigned historyLinks(HistoryServer &app)
{
  return ZmBlock<unsigned>{}([&app](auto done) {
    app.rxRun([&app, done]() mutable { done(app.allLinks_([](Ztc::Link *) {})); });
  });
}
static bool historyMarker(const char *name, const Zutcp::ClosedTuple &record)
{
  ZtString<> path;
  path << getenv("ZUTCP_NATIVE_HISTORY_DIR") << '/' << name;
  ZtString<> text;
  text << record.id << ' ' << record.txNext << ' ' << record.rxNext << '\n';
  return writeFile(path, text);
}

static void testNativeHistoryPair(bool v6, bool serverFirst)
{
  ZuTestScope(testNativeHistoryPair);
  alarm(35);
  unsigned a = ZuBox<unsigned>{getenv("ZXDP_RX_TEST_IFINDEX")};
  unsigned b = ZuBox<unsigned>{getenv("ZXDP_RX_TEST_PEER_IFINDEX")};
  const char *remoteText = v6 ? "2001:db8::21" : "198.18.0.21";
  ZiIP local{v6 ? "2001:db8::20" : "198.18.0.20"}, remote{remoteText};
  ZiMultiplex client{mxParams()}, server{mxParams()};
  ZmRef<Zutcp::Stack> clientStack = new Zutcp::Stack{&client, {.interface = a}};
  ZmRef<Zutcp::Stack> serverStack = new Zutcp::Stack{&server, {.interface = b}};
  client.plugin(clientStack);
  server.plugin(serverStack);
  HistoryEvents cli[2];
  HistoryClient clientApp;
  HistoryServer serverApp{v6};
  serverApp.events[1].hold = true;
  ZmRef<HistoryCli> cxn[2];
  bool clientInitialized = clientApp.init(Zutcp::ClientParams{&client}
    .rxRetainFrames(96).rxRetainFramesPerCxn(64).rxReserveFrames(16));
  bool serverInitialized = clientInitialized && serverApp.init(Zutcp::ServerParams{&server}
    .rxRetainFrames(96).rxRetainFramesPerCxn(64).rxReserveFrames(16));
  bool started = serverInitialized && start(client) && start(server) && serverApp.start();
  ZuCHECK(started, "direct native hubs must start on two live Stacks and a wildcard listener");
  if (!started) {
    if (serverInitialized) serverApp.stop();
    client.stop(); server.stop();
    if (clientInitialized) clientApp.final();
    if (serverInitialized) serverApp.final();
    alarm(0);
    return;
  }
  Zutcp::ClosedTuple firstClient, firstServer;
  for (unsigned i = 0; i < 2; ++i) {
    cxn[i] = new HistoryCli{&clientApp, &cli[i], ZiSockAddr{local, uint16_t(18192 + i)}};
    cxn[i]->connect(Zutcp::Host{remoteText}, 18191, remote);
    bool connected = wait(cli[i].established) && wait(serverApp.events[i].established);
    ZuCHECK(connected && cli[i].connected == 1 && serverApp.events[i].connected == 1 &&
	!unsigned(cli[i].errors) && !unsigned(serverApp.events[i].errors),
      "the same public native listener must establish two successive transport incarnations");
    if (!connected || !serverApp.kept[i]) break;
    auto clientBases = historyBases(client, cxn[i]);
    auto serverBases = historyBases(server, serverApp.kept[i]);
    if (!i) ZuCHECK(wireFaultGate(), "SYN-origin fault setup must precede native FIN output");
    auto &srv = serverApp.events[i];
    Zutcp::ClosedTupleKey ck{ZiSockAddr{local, uint16_t(18192 + i)},
      ZiSockAddr{remote, 18191}};
    Zutcp::ClosedTupleKey sk{ck.p<1>(), ck.p<0>()};
    if (!i) {
      bool sent = serverFirst ? serverApp.kept[i]->send(Message, true) :
	cxn[i]->send(Message, true);
      bool eof = sent && wait(serverFirst ? cli[i].eof : srv.eof);
      bool open = ZmBlock<bool>{}([
	&client, &server, &serverApp, &cxn, i, serverFirst
      ](auto done) {
	auto check = [&serverApp, &cxn, i, serverFirst, done]() mutable {
	  done(serverFirst ? cxn[i]->writeOpenTx() : serverApp.kept[i]->writeOpenTx());
	};
	if (serverFirst) client.txRun(ZuMv(check)); else server.txRun(ZuMv(check));
      });
      bool marked = eof && open && historyMarker("post-eof", {});
      bool replied = marked && (serverFirst ? cxn[i]->send(Message, true) :
	serverApp.kept[i]->send(Message, true));
      ZuCHECK(replied, "native output must remain possible after peer EOF before local FIN");
      bool activeGone = serverFirst ? wait(srv.closed) : wait(cli[i].down);
      auto &activeMx = serverFirst ? server : client;
      auto activeStack = serverFirst ? serverStack.ptr() : clientStack.ptr();
      auto key = serverFirst ? sk : ck;
      auto before = closedRecord(activeMx, activeStack, key);
      bool quiet = serverFirst ? historyQuiet(server, serverApp.kept[i]) :
	historyQuiet(client, cxn[i]);
      bool removed = serverFirst ? historyLinks(serverApp) == 0 : historyUnbound(client, cxn[i]);
      ZuCHECK(activeGone && quiet && removed && before.id && before.fin &&
	  uint32_t(before.txNext - (serverFirst ? serverBases.seq : clientBases.seq)) == 20 &&
	  uint32_t(before.rxNext - (serverFirst ? serverBases.ack : clientBases.ack)) == 20 &&
	  !(serverFirst ? unsigned(cli[i].disconnected) : unsigned(srv.disconnected)),
	"final ACK acceptance must retire the active transport before releasing its lost ACK");
      if (serverFirst) server.rxRun([&serverApp] { serverApp.kept[0] = nullptr; });
      else client.rxRun([&cxn] { cxn[0] = nullptr; });
      auto &activeEvents = serverFirst ? srv : cli[i];
      bool destroyed = wait(activeEvents.destroyed);
      bool retired = activeGone && quiet && removed && destroyed &&
	activeEvents.destroyedCount == 1 && before.id && historyMarker("retired", before);
      ZuCHECK(retired,
	"the entire active native owner must be destroyed before final ACK loss is released");
      if (retired) {
	ZtString<> path;
	path << getenv("ZUTCP_NATIVE_HISTORY_DIR") << "/retired-gate";
	retired &= writeFile(path, "R", ZiFile::WriteOnly);
      }
      bool passiveGone = retired && (serverFirst ? wait(cli[i].down) : wait(srv.closed));
      auto after = closedRecord(activeMx, activeStack, key);
      ZuCHECK(passiveGone && after.id == before.id &&
	  (serverFirst ? historyLinks(serverApp) == 0 : historyUnbound(client, cxn[i])) &&
	  historyMarker("recovered", after),
	"retired native history must ACK a repeated FIN without recovering a full owner");
    } else {
      bool sent = cxn[i]->send(Message, true) && serverApp.kept[i]->send(Message, true);
      bool held = sent && wait(srv.held) && wait(srv.txDone) && wait(cli[i].down);
      auto absent = closedRecord(server, serverStack.ptr(), sk);
      ZuCHECK(held && !unsigned(srv.disconnected) && historyLinks(serverApp) == 1 &&
	  !absent.id && !unsigned(srv.bytes) && historyMarker("held-reader", {}),
	"FIN acknowledgment must preserve native ownership while its application reader is held");
      server.rxRun([held = serverApp.kept[i]] { held->releaseReaderRx(); });
      bool consumed = wait(srv.eof) && wait(srv.closed);
      ZuCHECK(consumed && srv.bytes == Message.length(),
	"reader release must complete native EOF and retire without another packet or app stop");
    }
    bool clientQuiet = cxn[i] ? historyQuiet(client, cxn[i]) : cli[i].destroyedCount == 1;
    bool serverQuiet = serverApp.kept[i] ? historyQuiet(server, serverApp.kept[i]) :
      srv.destroyedCount == 1;
    bool endpoints = wait(cli[i].closed) && clientQuiet && serverQuiet;
    auto cr = closedRecord(client, clientStack.ptr(), ck);
    auto sr = closedRecord(server, serverStack.ptr(), sk);
    ZuCHECK(endpoints && historyLinks(serverApp) == 0 && serverApp.listening() &&
	clientStack->ready() && serverStack->ready() && historyUnbound(client, cxn[i]) &&
	cli[i].disconnected == 1 && cli[i].downs == 1 && cli[i].completed == 1 &&
	srv.disconnected == 1 && srv.completed == 1 &&
	!unsigned(cli[i].errors) && !unsigned(srv.errors) &&
	cli[i].bytes == Message.length() && srv.bytes == Message.length() &&
	cr.id && sr.id && cr.fin && sr.fin,
      "application references may remain after routes, timers and server ownership retire once");
    if (!i) { firstClient = cr; firstServer = sr; }
    else {
      auto oldClient = closedRecord(client, clientStack.ptr(), firstClient.key);
      auto oldServer = closedRecord(server, serverStack.ptr(), firstServer.key);
      ZuCHECK(cr.id != firstClient.id && sr.id != firstServer.id &&
	  oldClient.id == firstClient.id && oldServer.id == firstServer.id,
	"a second transport must preserve earlier closed identities on the same live Stacks");
    }
  }
  bool stopped = serverApp.stop();
  for (unsigned i = 0; i < 2; ++i) if (cxn[i]) {
    ZmBlock<>{}([held = cxn[i]](auto done) { held->abort(ZuMv(done)); });
    client.rxRun([&cxn, i] { cxn[i] = nullptr; });
    if (!wait(cli[i].destroyed)) ++cli[i].errors;
  }
  for (unsigned i = 0; i < 2; ++i) if (serverApp.kept[i]) {
    server.rxRun([&serverApp, i] { serverApp.kept[i] = nullptr; });
    if (!wait(serverApp.events[i].destroyed)) ++serverApp.events[i].errors;
  }
  clientApp.final();
  serverApp.final();
  stopped = client.stop() && server.stop() && stopped;
  bool callbacks = true;
  for (unsigned i = 0; i < 2; ++i)
    callbacks &= cli[i].connected == 1 && cli[i].disconnected == 1 && cli[i].downs == 1 &&
      cli[i].completed == 1 && cli[i].destroyedCount == 1 &&
      serverApp.events[i].connected == 1 &&
      serverApp.events[i].disconnected == 1 && serverApp.events[i].completed == 1 &&
      serverApp.events[i].destroyedCount == 1 &&
      !unsigned(cli[i].errors) && !unsigned(serverApp.events[i].errors);
  ZuCHECK(stopped && callbacks && serverApp.acceptedCount == 2 && !unsigned(serverApp.errors),
    "direct native listener and Stack teardown must drain after both completed incarnations");
  alarm(0);
}

static void testNativeHistory(bool v6, bool serverFirst)
{
  ZuTestScopeRT(testNativeHistory);
  Zi::Name name;
  name << "native-history-" << (serverFirst ? "srv-" : "cli-") << (v6 ? "v6" : "v4");
  auto directory = ZiTestResidue::dir(name);
  auto child = fork();
  if (!child) {
    execl("/bin/bash", "bash", "./zutcpmxlossfixture.sh", v6 ? "6" : "4",
      directory.data(), serverFirst ? "native-history-server" : "native-history-client",
      static_cast<char *>(nullptr));
    _exit(127);
  }
  int status = -1;
  if (child > 0) {
    pid_t result;
    do { result = waitpid(child, &status, 0); } while (result < 0 && errno == EINTR);
    if (result != child) status = -1;
  }
  ZuCheckRT(!status, log_("direct native FIN retirement and retained history; logs ", directory));
}

static bool sameHistory(const Zutcp::ClosedTuple &a, const Zutcp::ClosedTuple &b)
{
  return a.id && a.id == b.id && a.txNext == b.txNext && a.rxNext == b.rxNext &&
    a.tsRecent == b.tsRecent && a.lastSentTS == b.lastSentTS &&
    a.timestamps == b.timestamps && a.tsRecentTime == b.tsRecentTime && a.fin == b.fin;
}

static void testSharedAdmissionPair(bool v6, bool passive)
{
  ZuTestScope(testSharedAdmissionPair);
  alarm(40);
  unsigned a = ZuBox<unsigned>{getenv("ZXDP_RX_TEST_IFINDEX")};
  unsigned b = ZuBox<unsigned>{getenv("ZXDP_RX_TEST_PEER_IFINDEX")};
  const char *remoteText = v6 ? "2001:db8::21" : "198.18.0.21";
  ZiIP local{v6 ? "2001:db8::20" : "198.18.0.20"}, remote{remoteText};
  ZiMultiplex client{mxParams()}, server{mxParams()};
  ZmRef<Zutcp::Stack> cs = new Zutcp::Stack{&client, {.interface = a}};
  ZmSemaphore initial;
  ZmRef<Zutcp::Stack> ss = new Zutcp::Stack{&server, {.interface = b,
    .packet = [&initial](ZmRef<ZiIOBuf> packet) {
      Zutcp::IPView ip;
      Zutcp::TCPView tcp;
      if (Zutcp::readIP(packet->cspan(), ip) && Zutcp::readTCP(ip, tcp) &&
	  tcp.header.dstPort == 18199 && (tcp.header.flags & 2)) initial.post();
    }}};
  client.plugin(cs);
  server.plugin(ss);
  auto mappedRx = client.allocator<Zi::IOBufType::RxTCP>();
  HistoryClient firstHub, nextHub;
  HistoryServer firstListener{v6}, nextListener{v6, 18195};
  unsigned cc = 2, sc = passive ? 1 : 2;
  auto cp = [&client, cc, passive] {
    auto params = Zutcp::ClientParams{&client}.maxCxns(cc).maxPendingCxns(1);
    if (passive) params.connectTimeout(ZuTime{2});
    return params;
  };
  auto sp = [&server, sc] {
    return Zutcp::ServerParams{&server}.maxCxns(sc).maxPendingCxns(sc);
  };
  bool initialized = firstHub.init(cp()) && nextHub.init(cp()) &&
    firstListener.init(sp()) && nextListener.init(sp());
  bool started = initialized && start(client) && start(server) &&
    firstListener.start() && nextListener.start();
  ZuCHECK(started, "two native Hubs/listeners must share each Stack's configured ledger");
  if (!started) { alarm(0); return; }
  HistoryEvents first, denied, fresh;
  denied.expectFailure = passive;
  ZmRef<HistoryCli> cxn = new HistoryCli{&firstHub, &first, {local, 18192}};
  ZmRef<HistoryCli> refused = new HistoryCli{&nextHub, &denied, {local, 18193}};
  ZmRef<HistoryCli> retry;
  ZmAtomic<unsigned> factories = 0, failed = 0;
  ZmSemaphore mappedFailed;
  auto denyMapped = [
    &client, &factories, &failed, &mappedFailed, local, remote, mappedRx
  ](uint16_t port) {
    client.connect(ZiConnectFn{[&factories](const ZiCxnInfo &) -> ZiConnection * {
      ++factories;
      return nullptr;
    }}, ZiFailFn{[&failed, &mappedFailed](bool transient) {
      if (transient) failed += 100;
      ++failed;
      mappedFailed.post();
    }}, local, port, remote, 18191, mappedRx, {});
    return wait(mappedFailed);
  };
  HistoryEvents pending;
  pending.expectFailure = true;
  ZmRef<HistoryCli> setup;
  if (!passive) {
    setup = new HistoryCli{&firstHub, &pending, {local, 18197}};
    setup->connect(Zutcp::Host{remoteText}, 18199, remote);
    bool emitted = wait(initial);
    auto held = admission(client, cs.ptr());
    bool deniedPending = denyMapped(18198);
    auto fullPending = admission(client, cs.ptr());
    bool absent = !closedRecord(client, cs.ptr(),
      {ZiSockAddr{local, 18198}, ZiSockAddr{remote, 18191}}).id;
    ZuCHECK(emitted && held.total == 1 && held.pending == 1 && held.maxCxns == 2 &&
	deniedPending && !unsigned(pending.failures) && !unsigned(factories) && failed == 1 &&
	fullPending.total == 1 && fullPending.pending == 1 &&
	fullPending.refused == held.refused + 1 && absent,
      "mapped setup must share native pending capacity while total capacity remains free");
    ZmBlock<>{}([setup](auto done) { setup->abort(ZuMv(done)); });
    auto empty = admission(client, cs.ptr());
    ZuCHECK(!empty.total && !empty.pending,
      "native setup abort completion must release the shared pending and total charge");
  }
  cxn->connect(Zutcp::Host{remoteText}, 18191, remote);
  bool connected = wait(first.established) && wait(firstListener.events[0].established);
  auto &peer = firstListener.events[0];
  unsigned cut = 4;
  bool before = connected && cxn->send(ZuBSpan{Message.data(), cut}, false) &&
    firstListener.kept[0]->send(ZuBSpan{Message.data(), cut}, false) &&
    wait(first.progress) && wait(peer.progress);
  auto initialClient = admission(client, cs.ptr()), initialServer = admission(server, ss.ptr());
  ZuCHECK(before && initialClient.total == 1 && !initialClient.pending &&
      initialServer.total == 1 && !initialServer.pending,
    "the first serviced direct flow must consume one shared established slot per Stack");
  uint16_t deniedPort = passive ? 18195 : 18191;
  refused->connect(Zutcp::Host{remoteText}, deniedPort, remote);
  bool second = passive ? wait(denied.failed) :
    (wait(denied.established) && wait(firstListener.events[1].established));
  auto afterClient = admission(client, cs.ptr()), afterServer = admission(server, ss.ptr());
  auto deniedHistory = closedRecord(client, cs.ptr(),
    {ZiSockAddr{local, 18193}, ZiSockAddr{remote, deniedPort}});
  bool noPassiveHistory = !closedRecord(server, ss.ptr(),
    {ZiSockAddr{remote, deniedPort}, ZiSockAddr{local, 18193}}).id;
  ZuCHECK(second && denied.failures == unsigned(passive) &&
      denied.connected == unsigned(!passive) && !unsigned(denied.errors) &&
      firstListener.acceptedCount == (passive ? 1U : 2U) && !nextListener.acceptedCount &&
      afterClient.total == (passive ? 1U : 2U) && !afterClient.pending &&
      afterServer.total == afterClient.total && !afterServer.pending &&
      (!passive || afterServer.refused > initialServer.refused) &&
      noPassiveHistory && (passive || !deniedHistory.id),
    "two direct Hubs must share total slots and a second listener must refuse before factory");
  if (!passive) {
    bool mapped = denyMapped(18194);
    auto slots = admission(client, cs.ptr());
    auto absent = closedRecord(client, cs.ptr(),
      {ZiSockAddr{local, 18194}, ZiSockAddr{remote, 18191}});
    ZuCHECK(mapped && failed == 2 && !unsigned(factories) && !absent.id &&
	slots.total == 2 && !slots.pending && slots.refused == afterClient.refused + 1,
      "mapped active creation must share two native occupied slots without factory or history");
  }
  bool continued = cxn->send(ZuBSpan{Message.data() + cut, Message.length() - cut}, true) &&
    firstListener.kept[0]->send(ZuBSpan{Message.data() + cut, Message.length() - cut}, true) &&
    wait(first.input) && wait(peer.input) && wait(first.down) &&
    wait(first.closed) && wait(peer.closed);
  auto freeClient = admission(client, cs.ptr()), freeServer = admission(server, ss.ptr());
  Zutcp::ClosedTupleKey oldKey{ZiSockAddr{local, 18192}, ZiSockAddr{remote, 18191}};
  auto oldHistory = closedRecord(client, cs.ptr(), oldKey);
  ZuCHECK(continued && first.bytes == Message.length() && peer.bytes == Message.length() &&
      !unsigned(first.errors) && !unsigned(peer.errors) &&
      first.disconnected == 1 && first.downs == 1 && peer.disconnected == 1 &&
      freeClient.total == unsigned(!passive) && !freeClient.pending &&
      freeServer.total == unsigned(!passive) && !freeServer.pending &&
      cxn && firstListener.kept[0] && oldHistory.id,
    "denial must preserve exact I/O, then FIN retirement frees slots while app refs remain");
  retry = new HistoryCli{&nextHub, &fresh, {local, 18196}};
  uint16_t freshPort = 18195;
  auto &listener = nextListener;
  unsigned index = 0;
  retry->connect(Zutcp::Host{remoteText}, freshPort, remote);
  bool reused = wait(fresh.established) && wait(listener.events[index].established);
  ZuCHECK(reused && fresh.connected == 1 && listener.acceptedCount == index + 1 &&
      admission(client, cs.ptr()).total == (passive ? 1U : 2U) &&
      admission(server, ss.ptr()).total == (passive ? 1U : 2U),
    "the other Hub/listener must admit a fresh transport after owner slot release");
  if (!passive && reused) {
    client.rxRun([&first] { first.expectFailure = true; });
    cxn->connect(Zutcp::Host{remoteText}, 18191, remote);
    bool oldDenied = wait(first.failed);
    auto unchanged = closedRecord(client, cs.ptr(), oldKey);
    bool aborted = false;
    ZmBlock<>{}([cxn, &aborted](auto done) {
      cxn->abort([&aborted, done]() mutable { aborted = true; done(); });
    });
    auto afterAbort = closedRecord(client, cs.ptr(), oldKey);
    ZuCHECK(oldDenied && first.failures == 1 && sameHistory(oldHistory, unchanged) &&
	aborted && sameHistory(oldHistory, afterAbort) &&
	admission(client, cs.ptr()).total == 2,
      "denial and cleanup of a reused wrapper must preserve its prior closed identity");
  }
  if (!passive) {
    auto &other = firstListener.events[1];
    bool drained = refused->send(Message, true) && firstListener.kept[1]->send(Message, true) &&
      wait(denied.input) && wait(other.input) && wait(denied.down) &&
      wait(denied.closed) && wait(other.closed);
    ZuCHECK(drained && denied.bytes == Message.length() && other.bytes == Message.length() &&
	!unsigned(denied.errors) && !unsigned(other.errors) && denied.disconnected == 1 &&
	other.disconnected == 1 && admission(client, cs.ptr()).total == 1 &&
	admission(server, ss.ptr()).total == 1,
      "the other direct Hub must remain serviced while a released slot admits the fresh flow");
  }
  auto &freshPeer = listener.events[index];
  bool exact = reused && retry->send(Message, true) &&
    listener.kept[index]->send(Message, true) &&
    wait(fresh.input) && wait(freshPeer.input) && wait(fresh.down) &&
    wait(fresh.closed) && wait(freshPeer.closed);
  ZuCHECK(exact && fresh.bytes == Message.length() && freshPeer.bytes == Message.length() &&
      fresh.disconnected == 1 && fresh.downs == 1 && freshPeer.disconnected == 1 &&
      !unsigned(fresh.errors) && !unsigned(freshPeer.errors) &&
      !admission(client, cs.ptr()).total && !admission(server, ss.ptr()).total,
    "fresh direct flow must exchange exact bytes and return both shared slots again");
  bool stopped = firstListener.stop() && nextListener.stop();
  client.rxRun([&cxn, &refused, &retry, &setup] {
    cxn = nullptr; refused = nullptr; retry = nullptr; setup = nullptr;
  });
  bool owners = wait(first.destroyed) && wait(denied.destroyed) && wait(fresh.destroyed) &&
    (passive || wait(pending.destroyed));
  server.rxRun([&firstListener, &nextListener] {
    for (unsigned i = 0; i < 2; ++i) {
      firstListener.kept[i] = nullptr;
      nextListener.kept[i] = nullptr;
    }
  });
  bool peers = wait(firstListener.events[0].destroyed) && wait(freshPeer.destroyed) &&
    (passive || wait(firstListener.events[1].destroyed));
  firstHub.final(); nextHub.final(); firstListener.final(); nextListener.final();
  stopped = client.stop() && server.stop() && stopped;
  ZuCHECK(stopped && owners && peers && !unsigned(firstListener.errors) &&
      !unsigned(nextListener.errors),
    "shared admission fixtures must retain no owners after both transports and app stop");
  alarm(0);
}

static void testSharedAdmission(bool v6, bool passive)
{
  ZuTestScopeRT(testSharedAdmission);
  Zi::Name name;
  name << "shared-admission-" << (passive ? "passive-" : "active-") << (v6 ? "v6" : "v4");
  auto directory = ZiTestResidue::dir(name);
  auto child = fork();
  if (!child) {
    execl("/bin/bash", "bash", "./zutcpmxlossfixture.sh", v6 ? "6" : "4", directory.data(),
      passive ? "shared-passive" : "shared-active", static_cast<char *>(nullptr));
    _exit(127);
  }
  int status = -1;
  if (child > 0) {
    pid_t result;
    do { result = waitpid(child, &status, 0); } while (result < 0 && errno == EINTR);
    if (result != child) status = -1;
  }
  ZuCheckRT(!status, log_("shared direct/mapped Stack admission; logs ", directory));
}

static Zutcp::ClosedTupleStats historyStats(ZiMultiplex &mx, Zutcp::Stack *stack)
{
  return ZmBlock<Zutcp::ClosedTupleStats>{}([&mx, stack](auto done) {
    mx.rxRun([stack, done]() mutable { done(stack->closedTupleStats()); });
  });
}

static void testConservativeTuplePair(bool v6)
{
  ZuTestScope(testConservativeTuplePair);
  alarm(35);
  unsigned a = ZuBox<unsigned>{getenv("ZXDP_RX_TEST_IFINDEX")};
  unsigned b = ZuBox<unsigned>{getenv("ZXDP_RX_TEST_PEER_IFINDEX")};
  const char *remoteText = v6 ? "2001:db8::21" : "198.18.0.21";
  ZiIP local{v6 ? "2001:db8::20" : "198.18.0.20"}, remote{remoteText};
  ZiMultiplex client{mxParams()}, server{mxParams()};
  ZmRef<Zutcp::Stack> cs = new Zutcp::Stack{&client, {.interface = a}};
  ZmRef<Zutcp::Stack> ss = new Zutcp::Stack{&server, {.interface = b}};
  client.plugin(cs);
  server.plugin(ss);
  HistoryClient app;
  HistoryServer listener{v6};
  bool initialized = app.init(Zutcp::ClientParams{&client}.maxCxns(1).maxPendingCxns(1)) &&
    listener.init(Zutcp::ServerParams{&server}.maxCxns(1).maxPendingCxns(1));
  bool started = initialized && start(client) && start(server) && listener.start();
  ZuCHECK(started, "tuple policy fixture must start shared packet service and public listener");
  if (!started) { alarm(0); return; }
  HistoryEvents first, fresh;
  ZmRef<HistoryCli> fixed = new HistoryCli{&app, &first, {local, 49152}};
  fixed->connect(Zutcp::Host{remoteText}, 18191, remote);
  bool connected = wait(first.established) && wait(listener.events[0].established);
  auto &oldPeer = listener.events[0];
  auto fixedBases = historyBases(client, fixed);
  bool exact = connected && fixed->send(Message, true) && listener.kept[0]->send(Message, true) &&
    wait(first.input) && wait(oldPeer.input) && wait(first.down) &&
    wait(first.closed) && wait(oldPeer.closed);
  Zutcp::ClosedTupleKey oldKey{ZiSockAddr{local, 49152}, ZiSockAddr{remote, 18191}};
  auto before = closedRecord(client, cs.ptr(), oldKey);
  auto stats = historyStats(client, cs.ptr());
  ZuCHECK(exact && before.id && before.fin && uint32_t(before.txNext - fixedBases.seq) == 20 &&
      uint32_t(before.rxNext - fixedBases.ack) == 20 &&
      first.bytes == Message.length() && oldPeer.bytes == Message.length() &&
      first.disconnected == 1 && first.downs == 1 && oldPeer.disconnected == 1 &&
      !admission(client, cs.ptr()).total && !admission(server, ss.ptr()).total &&
      !unsigned(first.errors) && !unsigned(oldPeer.errors) && listener.acceptedCount == 1,
    "a fixed client tuple must finish exact I/O and retain history with its app wrapper held");
  client.rxRun([&first, cs] {
    first.expectFailure = 1;
    first.failureStack = cs.ptr();
  });
  fixed->connect(Zutcp::Host{remoteText}, 18191, remote);
  bool refused = wait(first.failed);
  auto denied = closedRecord(client, cs.ptr(), oldKey);
  auto deniedStats = historyStats(client, cs.ptr());
  ZuCHECK(refused && first.failures == 1 && first.failureFree == 1 && first.connected == 1 &&
      listener.acceptedCount == 1 && !unsigned(first.errors) && sameHistory(before, denied) &&
      deniedStats.count == stats.count && deniedStats.inserted == stats.inserted &&
      deniedStats.evicted == stats.evicted && cs->ready() && ss->ready(),
    "actual fixed bind refusal must release its slot before failure callback "
    "and preserve history");
  ZmBlock<>{}([fixed](auto done) { fixed->abort(ZuMv(done)); });
  auto aborted = closedRecord(client, cs.ptr(), oldKey);
  auto abortStats = historyStats(client, cs.ptr());
  ZuCHECK(sameHistory(before, aborted) && abortStats.count == stats.count &&
      abortStats.inserted == stats.inserted && abortStats.evicted == stats.evicted &&
      first.failures == 1 && first.disconnected == 2 && first.downs == 2 &&
      !admission(client, cs.ptr()).total && !unsigned(first.errors),
    "explicit cleanup of the rejected incarnation must preserve old tuple evidence and FIFO");
  ZmRef<HistoryCli> automatic = new HistoryCli{&app, &fresh, {local, 0}};
  automatic->connect(Zutcp::Host{remoteText}, 18191, remote);
  bool accepted = wait(fresh.established) && wait(listener.events[1].established);
  auto assigned = ZmBlock<ZiSockAddr>{}([&client, automatic](auto done) {
    client.rxRun([automatic, done]() mutable { done(automatic->local()); });
  });
  auto &newPeer = listener.events[1];
  bool delivered = accepted && automatic->send(Message, true) &&
    listener.kept[1]->send(Message, true) && wait(fresh.input) && wait(newPeer.input) &&
    wait(fresh.down) && wait(fresh.closed) && wait(newPeer.closed);
  auto retained = closedRecord(client, cs.ptr(), oldKey);
  auto freshRecord = closedRecord(client, cs.ptr(),
    {ZiSockAddr{local, 49153}, ZiSockAddr{remote, 18191}});
  ZuCHECK(delivered && assigned.ip() == local && assigned.port() == 49153 &&
      fresh.bytes == Message.length() && newPeer.bytes == Message.length() &&
      fresh.disconnected == 1 && fresh.downs == 1 && newPeer.disconnected == 1 &&
      !unsigned(fresh.errors) && !unsigned(newPeer.errors) && listener.acceptedCount == 2 &&
      sameHistory(before, retained) && freshRecord.id && freshRecord.id != before.id &&
      !admission(client, cs.ptr()).total && !admission(server, ss.ptr()).total &&
      listener.listening() && cs->ready() && ss->ready() && fixed,
    "automatic selection must skip retained 49152, exchange exact bytes on 49153 "
    "and keep service up");
  bool stopped = listener.stop();
  client.rxRun([&fixed, &automatic] { fixed = nullptr; automatic = nullptr; });
  bool owners = wait(first.destroyed) && wait(fresh.destroyed);
  server.rxRun([&listener] { listener.kept[0] = nullptr; listener.kept[1] = nullptr; });
  bool peers = wait(oldPeer.destroyed) && wait(newPeer.destroyed);
  app.final(); listener.final();
  stopped = client.stop() && server.stop() && stopped;
  ZuCHECK(stopped && owners && peers && !unsigned(listener.errors),
    "tuple refusal and fresh selection must drain all owners through ordinary teardown");
  alarm(0);
}

static void testConservativeTuple(bool v6)
{
  ZuTestScopeRT(testConservativeTuple);
  Zi::Name name;
  name << "conservative-tuple-" << (v6 ? "v6" : "v4");
  auto directory = ZiTestResidue::dir(name);
  auto child = fork();
  if (!child) {
    execl("/bin/bash", "bash", "./zutcpmxlossfixture.sh", v6 ? "6" : "4", directory.data(),
      "conservative-tuple", static_cast<char *>(nullptr));
    _exit(127);
  }
  int status = -1;
  if (child > 0) {
    pid_t result;
    do { result = waitpid(child, &status, 0); } while (result < 0 && errno == EINTR);
    if (result != child) status = -1;
  }
  ZuCheckRT(!status, log_("conservative native client tuple selection; logs ", directory));
}

static ZmRef<ZiIOBuf> lifecycleBacking(ZiMultiplex &mx, Zutcp::Stack *stack, ZiIPType::T family)
{
  return ZmBlock<ZmRef<ZiIOBuf>>{}([&mx, stack, family](auto done) {
    mx.txRun([stack, family, done]() mutable {
      auto buf = stack->allocPacket(stack, family, false);
      if (buf && buf->avail() >= Message.length()) {
	memcpy(buf->data(), Message.data(), Message.length());
	buf->length = Message.length();
      } else buf = nullptr;
      done(buf);
    });
  });
}

static void testPositiveWindowPair(bool v6)
{
  ZuTestScope(testPositiveWindowPair);
  alarm(35);
  unsigned a = ZuBox<unsigned>{getenv("ZXDP_RX_TEST_IFINDEX")};
  unsigned b = ZuBox<unsigned>{getenv("ZXDP_RX_TEST_PEER_IFINDEX")};
  const char *remoteText = v6 ? "2001:db8::21" : "198.18.0.21";
  ZiIP local{v6 ? "2001:db8::20" : "198.18.0.20"}, remote{remoteText};
  ZiMultiplex client{mxParams()}, server{mxParams()};
  ZmRef<Zutcp::Stack> cs = new Zutcp::Stack{&client, {.interface = a}};
  ZmRef<Zutcp::Stack> ss = new Zutcp::Stack{&server, {.interface = b}};
  client.plugin(cs);
  server.plugin(ss);
  HistoryClient app;
  HistoryServer listener{v6};
  listener.events[0].windowRead = 0;
  bool initialized = app.init(Zutcp::ClientParams{&client}.maxData(2000)) &&
    listener.init(Zutcp::ServerParams{&server}.maxData(400));
  bool started = initialized && start(client) && start(server) && listener.start();
  ZuCHECK(started, "positive-window fixture must start a native pair with strict Rx400");
  if (!started) { alarm(0); return; }
  HistoryEvents cli;
  auto &peer = listener.events[0];
  ZmRef<HistoryCli> cxn = new HistoryCli{&app, &cli, {local, 18192}};
  cxn->connect(Zutcp::Host{remoteText}, 18191, remote);
  bool connected = wait(cli.established) && wait(peer.established);
  // Stack producer geometry stays MSS-sized; the retained-buffer API offers
  // the original 1000 bytes even though this peer advertised only 400.
  auto packet = ZmBlock<ZmRef<ZiIOBuf>>{}([&client, &app, stack = cs.ptr()](auto done) {
    client.txRun([&app, stack, done]() mutable {
      auto buf = app.dataTxAllocator()->allocBuf(stack, 0, 1000);
      if (buf && buf->avail() >= 1000) {
	memcpy(buf->data(), Message.data(), 1000);
	buf->length = 1000;
      } else buf = nullptr;
      done(buf);
    });
  });
  auto origin = packet ? packet->data() : nullptr;
  auto provider = packet ?
    static_cast<Zutcp::Allocator *>(packet->allocator()->provider()) : nullptr;
  bool original = packet && packet->registered() && packet->length == 1000 &&
    packet->size - packet->skip >= 1000 && provider;
  bool admitted = connected && original && cxn->send(ZuMv(packet), true);
  auto held = [&server, &listener](unsigned next) {
    return ZmBlock<bool>{}([&server, &listener, next](auto done) {
      server.rxRun([&listener, next, done]() mutable {
	done(listener.kept[0]->windowRx(next));
      });
    });
  };
  auto release = [&server, &listener](unsigned next) {
    ZmBlock<>{}([&server, &listener, next](auto done) {
      server.rxRun([&listener, next, done]() mutable {
	listener.kept[0]->releaseWindowRx(next);
	done();
      });
    });
  };
  auto sender = [&client, cxn = cxn.ptr(), provider, origin](unsigned ack) {
    return ZmBlock<bool>{}([&client, cxn, provider, origin, ack](auto done) {
      client.txRun([cxn, provider, origin, ack, done]() mutable {
	done(cxn->windowTx(ack) && provider->pool.used() == 1 &&
	  !memcmp(origin, Message.data(), 1000));
      });
    });
  };
  bool first = admitted && wait(peer.held) && held(400);
  ZtString<> gate;
  gate << getenv("ZUTCP_POSITIVE_WINDOW_DIR") << "/reopen";
  bool probed = first && readGate(gate);
  bool retained = probed && sender(400);
  ZuCHECK(first && retained && !peer.bytes && !cli.disconnected && !peer.disconnected,
    "original1000 must deliver only400, retain its registered slot and wait through persist");
  if (retained) release(400);
  bool second = retained && wait(peer.held) && held(800);
  // The monitor releases the second gate after ACK800/window0 and its real
  // persist probe too, so Tx evidence has reached its owner before inspection.
  gate = ZtString<>{} << getenv("ZUTCP_POSITIVE_WINDOW_DIR") << "/reopen-second";
  bool probedSecond = second && readGate(gate);
  bool retainedSecond = probedSecond && sender(800);
  ZuCHECK(second && retainedSecond && peer.bytes == 400,
    "covering recovery must deliver only the next400 and keep the same immutable original");
  if (retainedSecond) release(800);
  bool tail = retainedSecond && wait(peer.held) && held(1000);
  if (tail) release(1000);
  bool received = tail && wait(peer.input) && wait(peer.eof);
  bool serverClose = received && listener.kept[0]->disconnect();
  bool retired = serverClose && wait(cli.closed) && wait(cli.down) && wait(peer.closed);
  ZuCHECK(retired && peer.bytes == 1000 && !unsigned(cli.errors) && !unsigned(peer.errors) &&
      cli.disconnected == 1 && cli.downs == 1 && peer.disconnected == 1 &&
      cli.completed == 1 && peer.completed == 1 && !unsigned(cli.peerClosed) &&
      !unsigned(peer.peerClosed),
    "strict400/400/200 reads must deliver exact1000 once before independent FIN retirement");
  ZmBlock<>{}([cxn](auto done) { cxn->abort(ZuMv(done)); });
  bool stopped = listener.stop();
  client.rxRun([&cxn] { cxn = nullptr; });
  server.rxRun([&listener] { listener.kept[0] = nullptr; });
  bool destroyed = wait(cli.destroyed) && wait(peer.destroyed);
  stopped = client.stop() && server.stop() && stopped;
  app.final(); listener.final();
  ZuCHECK(stopped && destroyed && listener.acceptedCount == 1 && !unsigned(listener.errors),
    "positive-window fixture must drain both native owners and packet services");
  alarm(0);
}

static void testPositiveWindow(bool v6)
{
  ZuTestScopeRT(testPositiveWindow);
  auto directory = ZiTestResidue::dir(v6 ? "positive-window-v6" : "positive-window-v4");
  auto child = fork();
  if (!child) {
    execl("/bin/bash", "bash", "./zutcpmxlossfixture.sh", v6 ? "6" : "4", directory.data(),
      "positive-window", static_cast<char *>(nullptr));
    _exit(127);
  }
  int status = -1;
  if (child > 0) {
    pid_t result;
    do { result = waitpid(child, &status, 0); } while (result < 0 && errno == EINTR);
    if (result != child) status = -1;
  }
  ZuCheckRT(!status, log_(
    "positive window sends original1000 with strict Rx400; logs ", directory));
}

static void testNativeLifecyclePair(
  bool v6, bool stopHub, bool numeric = false, bool serverError = false)
{
  ZuTestScope(testNativeLifecyclePair);
  alarm(35);
  unsigned a = ZuBox<unsigned>{getenv("ZXDP_RX_TEST_IFINDEX")};
  unsigned b = ZuBox<unsigned>{getenv("ZXDP_RX_TEST_PEER_IFINDEX")};
  const char *remoteText = v6 ? "2001:db8::21" : "198.18.0.21";
  ZiIP local{v6 ? "2001:db8::20" : "198.18.0.20"}, remote{remoteText};
  ZiMultiplex client{mxParams()}, server{mxParams()};
  ZmRef<Zutcp::Stack> cs = new Zutcp::Stack{&client, {.interface = a}};
  ZmRef<Zutcp::Stack> ss = new Zutcp::Stack{&server, {.interface = b}};
  client.plugin(cs);
  server.plugin(ss);
  HistoryClient app;
  HistoryServer firstListener{v6}, otherListener{v6, 18195};
  bool initialized = app.init(Zutcp::ClientParams{&client}.maxCxns(2).maxPendingCxns(2)) &&
    firstListener.init(Zutcp::ServerParams{&server}.maxCxns(2).maxPendingCxns(2)) &&
    otherListener.init(Zutcp::ServerParams{&server}.maxCxns(2).maxPendingCxns(2));
  bool started = initialized && start(client) && start(server) &&
    firstListener.start() && otherListener.start();
  ZuCHECK(started, "native lifecycle fixture must start two public listeners on one Stack");
  if (!started) { alarm(0); return; }
  bool abrupt = stopHub || numeric;
  bool serverInitiator = stopHub || serverError;
  HistoryEvents first, fresh;
  first.expectFailure = unsigned(abrupt);
  firstListener.events[0].expectFailure = unsigned(abrupt);
  ZmRef<HistoryCli> cxn = new HistoryCli{&app, &first, {local, 18192}};
  ZmRef<HistoryCli> retry;
  ZmRef<ZiIOBuf> held;
  cxn->connect(Zutcp::Host{remoteText}, 18191, remote);
  bool connected = wait(first.established) && wait(firstListener.events[0].established);
  auto &peer = firstListener.events[0];
  ZuCHECK(connected && first.connected == 1 && peer.connected == 1,
    "native roles must establish before explicit lifecycle operations");
  auto clientBases = historyBases(client, cxn);
  auto serverBases = historyBases(server, firstListener.kept[0]);
  bool firstStopped = false;
  if (!abrupt) {
    bool admitted = connected && cxn->send(Message, false);
    bool literal = cxn->disconnect(0);
    uint64_t zero = 0;
    bool typed = cxn->disconnect(zero);
    bool eof = wait(peer.eof);
    held = lifecycleBacking(client, cs.ptr(), local.type());
    bool refused = held && !cxn->send(Message, false) && !cxn->send(held, false);
    bool reverse = eof && firstListener.kept[0]->send(Message, false);
    bool serverClose = ZmBlock<bool>{}([&server, &firstListener](auto done) {
      server.rxRun([&firstListener, done]() mutable {
	done(firstListener.kept[0]->disconnect());
      });
    });
    bool serverRefused = !firstListener.kept[0]->send(Message, false);
    bool retired = wait(first.input) && wait(peer.input) && wait(first.down) &&
      wait(first.closed) && wait(peer.closed);
    auto cr = closedRecord(client, cs.ptr(),
      {ZiSockAddr{local, 18192}, ZiSockAddr{remote, 18191}});
    auto sr = closedRecord(server, ss.ptr(),
      {ZiSockAddr{remote, 18191}, ZiSockAddr{local, 18192}});
    ZuCHECK(admitted && literal && typed && eof && refused && reverse && serverClose &&
	serverRefused && retired && first.bytes == Message.length() &&
	peer.bytes == Message.length() && !unsigned(first.errors) && !unsigned(peer.errors) &&
	first.disconnected == 1 && first.downs == 1 && first.completed == 1 &&
	peer.disconnected == 1 && peer.completed == 1 && !unsigned(first.peerClosed) &&
	!unsigned(peer.peerClosed) && cr.id && sr.id && cr.fin && sr.fin &&
	uint32_t(cr.txNext - clientBases.seq) == 20 &&
	uint32_t(cr.rxNext - clientBases.ack) == 20 &&
	uint32_t(sr.txNext - serverBases.seq) == 20 &&
	uint32_t(sr.rxNext - serverBases.ack) == 20 &&
	!admission(client, cs.ptr()).total && !admission(server, ss.ptr()).total,
      "literal/typed zero and server default close must drain admitted bytes with FIN, "
      "refuse late offers benignly and deliver positive reverse data after local FIN");
  } else {
    held = serverInitiator ? lifecycleBacking(server, ss.ptr(), remote.type()) :
      lifecycleBacking(client, cs.ptr(), local.type());
    bool admitted = connected && (serverInitiator ? firstListener.kept[0]->send(Message, false) :
      cxn->send(Message, false));
    ZtString<> path;
    path << getenv("ZUTCP_NATIVE_LIFECYCLE_DIR") << "/stop";
    bool dropped = readGate(path);
    bool outstanding = ZmBlock<bool>{}([
      &server, &client, &firstListener, cxn, serverInitiator
    ](auto done) {
      auto query = [&firstListener, cxn, serverInitiator, done]() mutable {
	done(serverInitiator ? firstListener.kept[0]->outstandingTx() : cxn->outstandingTx());
      };
      if (serverInitiator) server.txRun(ZuMv(query)); else client.txRun(ZuMv(query));
    });
    static constexpr uint64_t ErrorCode = 0xfedcba9876543210ULL;
    bool terminated;
    if (numeric) {
      terminated = serverInitiator ? firstListener.kept[0]->disconnect(ErrorCode) :
	cxn->disconnect(ErrorCode);
    } else terminated = firstStopped = firstListener.stop();
    bool retired = wait(first.closed) && wait(first.down) && wait(peer.closed);
    auto cr = closedRecord(client, cs.ptr(),
      {ZiSockAddr{local, 18192}, ZiSockAddr{remote, 18191}});
    auto sr = closedRecord(server, ss.ptr(),
      {ZiSockAddr{remote, 18191}, ZiSockAddr{local, 18192}});
    bool errorPreserved = true;
    if (numeric) {
      // Repeat the same public close after owner drain; it must not notify or reset again.
      if (serverInitiator) firstListener.kept[0]->disconnect(ErrorCode);
      else cxn->disconnect(ErrorCode);
      drainOwners(client);
      drainOwners(server);
      errorPreserved = (serverInitiator ? firstListener.kept[0]->closeError() :
	cxn->closeError()) == ErrorCode && (serverInitiator ? cxn->closeError() :
	firstListener.kept[0]->closeError()) == 0 && firstListener.listening();
    }
    ZuCHECK(admitted && dropped && outstanding && terminated && retired && errorPreserved &&
	first.disconnected == 1 && first.downs == 1 &&
	first.peerClosed == unsigned(serverInitiator) &&
	peer.disconnected == 1 && peer.peerClosed == unsigned(!serverInitiator) &&
	!unsigned(first.errors) && !unsigned(peer.errors) && !first.bytes && !peer.bytes &&
	!admission(client, cs.ptr()).total && !admission(server, ss.ptr()).total &&
	cr.id && sr.id && !cr.fin && !sr.fin && !historyLinks(firstListener) &&
	otherListener.listening() && cs->ready() && ss->ready() && held &&
	held->length == Message.length() &&
	!memcmp(held->data(), Message.data(), Message.length()),
      "numeric close or Hub stop must reset an outstanding flow and retain local error "
      "and preserve another listener and independent application backing");
    retry = new HistoryCli{&app, &fresh, {local, 18193}};
    retry->connect(Zutcp::Host{remoteText}, 18195, remote);
    bool accepted = wait(fresh.established) && wait(otherListener.events[0].established);
    auto &other = otherListener.events[0];
    bool exact = accepted && retry->send(Message, true) &&
      otherListener.kept[0]->send(Message, true) && wait(fresh.input) && wait(other.input) &&
      wait(fresh.down) && wait(fresh.closed) && wait(other.closed);
    ZuCHECK(exact && fresh.bytes == Message.length() && other.bytes == Message.length() &&
	fresh.disconnected == 1 && fresh.downs == 1 && other.disconnected == 1 &&
	!unsigned(fresh.errors) && !unsigned(other.errors) &&
	!admission(client, cs.ptr()).total && !admission(server, ss.ptr()).total &&
	otherListener.listening() && cs->ready() && ss->ready(),
      "the other listener must accept fresh exact I/O after Hub-local abort on the shared Stack");
  }
  bool stopped = (firstStopped || firstListener.stop()) && otherListener.stop();
  client.rxRun([&cxn, &retry] { cxn = nullptr; retry = nullptr; });
  bool owners = wait(first.destroyed) && (!abrupt || wait(fresh.destroyed));
  server.rxRun([&firstListener, &otherListener] {
    firstListener.kept[0] = nullptr;
    otherListener.kept[0] = nullptr;
  });
  bool peers = wait(peer.destroyed) && (!abrupt || wait(otherListener.events[0].destroyed));
  stopped = client.stop() && server.stop() && stopped;
  bool backing = held && held->length == Message.length() &&
    !memcmp(held->data(), Message.data(), Message.length());
  held = nullptr;
  app.final(); firstListener.final(); otherListener.final();
  ZuCHECK(stopped && owners && peers && backing && first.disconnected == 1 &&
      first.downs == 1 && peer.disconnected == 1 &&
      (!abrupt || (fresh.disconnected == 1 && fresh.downs == 1 &&
	otherListener.events[0].disconnected == 1)) &&
      !unsigned(firstListener.errors) && !unsigned(otherListener.errors),
    "explicit lifecycle teardown must destroy owners and preserve application-only backing "
    "through packet-service stop");
  alarm(0);
}

static void testNativeLifecycle(bool v6, bool stopHub)
{
  ZuTestScopeRT(testNativeLifecycle);
  Zi::Name name;
  name << "native-" << (stopHub ? "hub-stop-" : "zero-close-") << (v6 ? "v6" : "v4");
  auto directory = ZiTestResidue::dir(name);
  auto child = fork();
  if (!child) {
    execl("/bin/bash", "bash", "./zutcpmxlossfixture.sh", v6 ? "6" : "4", directory.data(),
      stopHub ? "native-hub-stop" : "native-zero-close", static_cast<char *>(nullptr));
    _exit(127);
  }
  int status = -1;
  if (child > 0) {
    pid_t result;
    do { result = waitpid(child, &status, 0); } while (result < 0 && errno == EINTR);
    if (result != child) status = -1;
  }
  ZuCheckRT(!status, log_("public native zero close or active Hub stop; logs ", directory));
}

static void testNativeNumericClose(bool v6, bool serverError)
{
  ZuTestScopeRT(testNativeNumericClose);
  Zi::Name name;
  name << "native-error-" << (serverError ? "server-" : "client-") << (v6 ? "v6" : "v4");
  auto directory = ZiTestResidue::dir(name);
  auto child = fork();
  if (!child) {
    execl("/bin/bash", "bash", "./zutcpmxlossfixture.sh", v6 ? "6" : "4", directory.data(),
      serverError ? "native-error-server" : "native-error-client", static_cast<char *>(nullptr));
    _exit(127);
  }
  int status = -1;
  if (child > 0) {
    pid_t result;
    do { result = waitpid(child, &status, 0); } while (result < 0 && errno == EINTR);
    if (result != child) status = -1;
  }
  ZuCheckRT(!status, log_("public native numeric error close; logs ", directory));
}

static Zutcp::Datagram passiveInitial(
  ZiSockAddr local, ZiSockAddr peer, uint32_t seq = UINT32_MAX)
{
  auto buf = Zutcp::inlineAllocator()->allocBuf();
  Zutcp::Datagram d{ZuMv(buf), peer};
  d.local = local;
  d.tcp = {.srcPort = peer.port(), .dstPort = local.port(), .seq = seq,
    .window = 65535, .flags = 0x02,
    .options = {.flags = Zutcp::TCPOptions::MSS, .mss = 1400}};
  return d;
}

static bool passiveWire(
  ZiMultiplex &mx, Zutcp::Stack *stack, Zutcp::IPPath path, Zutcp::Datagram d)
{
  return ZmBlock<bool>{}([&mx, stack, path, d = ZuMv(d)](auto done) mutable {
    mx.txRun([stack, path, d = ZuMv(d), done]() mutable {
      auto buf = stack->allocPacket(stack, path.local.type(), true);
      if (!buf || !d.buf || !reserveOptions(buf.ptr(), d.tcp) ||
	  buf->avail() < d.buf->length) { done(false); return; }
      memcpy(buf->data(), d.buf->data(), d.buf->length);
      unsigned n = Zutcp::writeTCP(buf->data_(), buf->size, path, d.tcp, d.buf->length);
      buf->skip = 0;
      buf->length = n;
      done(n && stack->sendPacket(buf));
    });
  });
}

struct PassiveRouteGate {
  ZiMultiplex *mx;
  unsigned worker = 0;
  ZmSemaphore entered, release, completed;
  explicit PassiveRouteGate(ZiMultiplex *mx_) : mx{mx_} {
    for (unsigned i = 0; i < mx->nWorkers(); ++i) {
      auto id = mx->workerID(i);
      if (id != mx->rxThread() && id != mx->txThread()) { worker = id; break; }
    }
  }
  bool hold() {
    if (!worker) return false;
    mx->run([this] { entered.post(); release.wait(); }, worker);
    return wait(entered);
  }
  bool finish() {
    // Route work was posted before the preceding Rx snapshot. Its completion
    // reaches Rx before this same-worker sentinel reaches Rx.
    mx->run([this] { mx->rxRun([this] { completed.post(); }); }, worker);
    release.post();
    return wait(completed);
  }
};

static bool sameRing(const Zutcp::ClosedTupleStats &a, const Zutcp::ClosedTupleStats &b)
{
  return a.capacity == b.capacity && a.count == b.count &&
    a.inserted == b.inserted && a.evicted == b.evicted;
}

static Zutcp::ClosedTuple passiveRemember(
  ZiMultiplex &mx, Zutcp::Stack *stack, Zutcp::ClosedTuple record)
{
  return ZmBlock<Zutcp::ClosedTuple>{}([&mx, stack, record](auto done) {
    mx.rxRun([stack, record, done]() mutable { done(stack->rememberClosed(record)); });
  });
}

// Raw peer stamps are independently chosen wire inputs, not a Stack clock override.
struct PAWSPeer {
  ZmSemaphore syn, ack, fin;
  Zutcp::TCPHeader synHeader;
  uint32_t expectedAck = 0, expectedEcho = 0, lastTS = 0;
  unsigned dataBytes = 0, errors = 0, packets = 0, port = 49152;
  bool synSeen = false, armed = false, finSeen = false;

  void received(ZmRef<ZiIOBuf> buf) {
    Zutcp::IPView ip;
    Zutcp::TCPView tcp;
    if (!Zutcp::readIP(buf->cspan(), ip) || !Zutcp::readTCP(ip, tcp) ||
	 tcp.header.srcPort != 18191 || tcp.header.dstPort != port) return;
    ++packets;
    auto &header = tcp.header;
    if (header.flags & 4) ++errors;
    if (!(header.options.flags & Zutcp::TCPOptions::TS)) { ++errors; return; }
    if (header.flags & 2) {
      if (header.flags == 0x12 && !synSeen) {
	synSeen = true;
	synHeader = header;
	syn.post();
      }
      return;
    }
    if (tcp.payload.length()) {
      if (header.seq != uint32_t(synHeader.seq + 1) ||
	  tcp.payload.length() != Message.length() ||
	  memcmp(tcp.payload.data(), Message.data(), Message.length())) ++errors;
      dataBytes = unsigned(tcp.payload.length());
    }
    if (armed && header.ack == expectedAck && header.options.tsecr == expectedEcho) {
      armed = false;
      ack.post();
    }
    if ((header.flags & 1) && !finSeen) {
      finSeen = true;
      lastTS = header.options.tsval;
      fin.post();
    }
  }
};

static void testPAWSPair(bool v6)
{
  ZuTestScope(testPAWSPair);
  alarm(45);
  static constexpr auto Accepted = "reuse-mx-byte-path!"_z;
  unsigned a = ZuBox<unsigned>{getenv("ZXDP_RX_TEST_IFINDEX")};
  unsigned b = ZuBox<unsigned>{getenv("ZXDP_RX_TEST_PEER_IFINDEX")};
  ZiIP local{v6 ? "2001:db8::20" : "198.18.0.20"};
  ZiIP remote{v6 ? "2001:db8::21" : "198.18.0.21"};
  ZiMultiplex client{mxParams()}, server{mxParams()};
  PAWSPeer peer, resetPeer;
  resetPeer.port = 49153;
  ZmRef<Zutcp::Stack> cs = new Zutcp::Stack{&client, {.interface = a,
    .packet = [&peer, &resetPeer](ZmRef<ZiIOBuf> buf) {
      peer.received(buf);
      resetPeer.received(ZuMv(buf));
    }}};
  ZmRef<Zutcp::Stack> ss = new Zutcp::Stack{&server, {.interface = b}};
  client.plugin(cs); server.plugin(ss);
  HistoryServer listener{v6};
  listener.events[0].expected = Accepted;
  bool started = listener.init(Zutcp::ServerParams{&server}) &&
    start(client) && start(server) && listener.start();
  ZuCHECK(started, "PAWS raw peer must use a real public listener and AF_XDP Stacks");
  if (!started) { client.stop(); server.stop(); alarm(0); return; }
  auto forward = route(client, cs.ptr(), local, remote);
  constexpr uint32_t Base = 0x12345671, InitialTS = 0xfffffff0;
  auto syn = passiveInitial({remote,18191}, {local,49152});
  syn.tcp.seq = Base - 1;
  syn.tcp.options.flags |= Zutcp::TCPOptions::TS;
  syn.tcp.options.tsval = InitialTS;
  bool offered = forward.mtu && passiveWire(client, cs.ptr(), forward, ZuMv(syn)) &&
    wait(peer.syn);
  auto initial = ZmBlock<Zutcp::TCPHeader>{}([&client, &peer](auto done) {
    client.rxRun([&peer, done]() mutable { done(peer.synHeader); });
  });
  auto header = [initial](uint32_t seq, uint32_t stamp) {
    return Zutcp::TCPHeader{.srcPort = 49152, .dstPort = 18191,
      .seq = seq, .ack = uint32_t(initial.seq + 1), .window = 65535, .flags = 0x10,
      .options = {.flags = Zutcp::TCPOptions::TS, .tsval = stamp,
	.tsecr = initial.options.tsval}};
  };
  auto final = passiveInitial({remote,18191}, {local,49152});
  final.tcp = header(Base, InitialTS + 1);
  final.buf->length = 0;
  bool established = offered && initial.flags == 0x12 &&
    initial.ack == Base && initial.options.tsecr == InitialTS &&
    passiveWire(client, cs.ptr(), forward, ZuMv(final)) && wait(listener.events[0].established);
  ZuCHECK(established && listener.acceptedCount == 1,
    "actual SYN/SYNACK/final ACK must negotiate timestamps with a near-wrap peer stamp");
  auto arm = [&client, &peer](uint32_t ack, uint32_t echo) {
    ZmBlock<>{}([&client, &peer, ack, echo](auto done) {
      client.rxRun([&peer, ack, echo, done]() mutable {
	peer.expectedAck = ack; peer.expectedEcho = echo; peer.armed = true; done();
      });
    });
  };
  auto send = [&client, cs, forward, header](
    unsigned offset, ZuBSpan payload, bool fin, uint32_t stamp) {
    auto buf = Zutcp::inlineAllocator()->allocBuf();
    if (!buf) return false;
    if (payload.length()) memcpy(buf->data(), payload.data(), payload.length());
    buf->length = payload.length();
    Zutcp::Datagram d{ZuMv(buf), {forward.local,49152}};
    d.local = {forward.local,49152};
    d.tcp = header(Base + offset, stamp);
    d.tcp.flags |= uint8_t((payload.length() ? 8 : 0) | (fin ? 1 : 0));
    return passiveWire(client, cs.ptr(), forward, ZuMv(d));
  };
  arm(Base, InitialTS + 1);
  bool old = established && send(0,Message,false,InitialTS) && wait(peer.ack);
  auto bytes = ZmBlock<unsigned>{}([&server, &listener](auto done) {
    server.rxRun([&listener, done]() mutable { done(listener.events[0].bytes); });
  });
  ZuCHECK(old && !bytes,
    "in-window old-TS DATA must elicit current echo without publishing bytes");
  arm(Base + Accepted.length(), 0x10);
  bool wrapped = old && send(0,Accepted,false,0x10) &&
    wait(listener.events[0].input) && wait(peer.ack);
  arm(Base + Accepted.length(), 0x10);
  bool laterOld = wrapped && send(Accepted.length(),Message,false,InitialTS) && wait(peer.ack);
  bool ended = laterOld && send(Accepted.length(),{},true,0x11) &&
    wait(listener.events[0].eof);
  ZuCHECK(wrapped && laterOld && ended && listener.events[0].bytes == Accepted.length() &&
      !unsigned(listener.events[0].errors),
    "wrapped-new TS DATA and FIN must reuse rejected byte positions "
    "and deliver only exact admitted bytes");
  bool replied = ended && listener.kept[0]->send(Message,true) && wait(peer.fin);
  auto lastTS = ZmBlock<uint32_t>{}([&client, &peer](auto done) {
    client.rxRun([&peer, done]() mutable { done(peer.lastTS); });
  });
  auto buf = Zutcp::inlineAllocator()->allocBuf();
  Zutcp::Datagram ack{ZuMv(buf), {local,49152}};
  ack.local = {remote,18191};
  ack.tcp = header(Base + Accepted.length() + 1, 0x12);
  ack.tcp.ack += unsigned(Message.length()) + 1;
  ack.tcp.options.tsecr = lastTS;
  bool closed = replied && passiveWire(client, cs.ptr(), forward, ZuMv(ack)) &&
    wait(listener.events[0].closed);
  auto record = closedRecord(
    server, ss.ptr(), {ZiSockAddr{remote,18191}, ZiSockAddr{local,49152}});
  unsigned peerErrors = ZmBlock<unsigned>{}([&client, &peer](auto done) {
    client.rxRun([&peer, done]() mutable {
      done(peer.errors + unsigned(peer.dataBytes != Message.length()));
    });
  });
  ZuCHECK(closed && !peerErrors && record.id && record.timestamps && record.fin &&
      record.rxNext == Base + Accepted.length() + 1 &&
      record.txNext == uint32_t(initial.seq + 1 + Message.length() + 1) &&
      listener.events[0].connected == 1 && listener.events[0].disconnected == 1 &&
      listener.events[0].completed == 1 && !admission(server, ss.ptr()).total,
    "raw peer cumulative ACK must retire one actual native owner and preserve timestamp history");
  // A separate actual connection keeps the PAWS DATA/FIN exchange intact.
  // Seed matching cold evidence only after its live timestamp negotiation.
  enum { ResetBase = 0x456789ab, ResetTS = 0x23456789 };
  auto resetSyn = passiveInitial({remote,18191}, {local,49153}, ResetBase - 1);
  resetSyn.tcp.options.flags |= Zutcp::TCPOptions::TS;
  resetSyn.tcp.options.tsval = ResetTS;
  listener.events[1].expectFailure = 1;
  bool resetOffered = closed && passiveWire(client, cs.ptr(), forward, ZuMv(resetSyn)) &&
    wait(resetPeer.syn);
  auto resetInitial = ZmBlock<Zutcp::TCPHeader>{}([&client, &resetPeer](auto done) {
    client.rxRun([&resetPeer, done]() mutable { done(resetPeer.synHeader); });
  });
  auto resetFinal = passiveInitial({remote,18191}, {local,49153});
  resetFinal.tcp = {.srcPort = 49153, .dstPort = 18191, .seq = ResetBase,
    .ack = uint32_t(resetInitial.seq + 1), .window = 65535, .flags = 0x10,
    .options = {.flags = Zutcp::TCPOptions::TS, .tsval = ResetTS + 1,
      .tsecr = resetInitial.options.tsval}};
  bool resetReady = resetOffered && (resetInitial.options.flags & Zutcp::TCPOptions::TS) &&
    resetInitial.ack == ResetBase && resetInitial.options.tsecr == ResetTS &&
    passiveWire(client, cs.ptr(), forward, ZuMv(resetFinal)) &&
    wait(listener.events[1].established);
  Zutcp::ClosedTupleKey resetKey{ZiSockAddr{remote,18191}, ZiSockAddr{local,49153}};
  auto seeded = resetReady ? passiveRemember(server, ss.ptr(), {.key = resetKey,
    .txNext = uint32_t(resetInitial.seq + 1), .rxNext = ResetBase, .tsRecent = ResetTS + 1,
    .lastSentTS = resetInitial.options.tsval, .timestamps = true, .fin = true}) :
    Zutcp::ClosedTuple{};
  auto resetSlots = admission(server, ss.ptr());
  ZuCHECK(resetReady && listener.acceptedCount == 2 && seeded.id && seeded.timestamps &&
      listener.events[1].actualLocal == resetKey.p<0>() &&
      listener.events[1].actualPeer == resetKey.p<1>() &&
      resetSlots.total == 1 && !resetSlots.pending && historyLinks(listener) == 1,
    "a live timestamp-negotiated owner must coexist with seeded same-key closed history");
  auto receivedBefore = ZmBlock<unsigned>{}([&client, &resetPeer](auto done) {
    client.rxRun([&resetPeer, done]() mutable { done(resetPeer.packets); });
  });
  auto reset = passiveInitial({remote,18191}, {local,49153});
  reset.tcp = {.srcPort = 49153, .dstPort = 18191, .seq = ResetBase,
    .ack = uint32_t(resetInitial.seq + 1), .flags = 0x14}; // intentionally no TS
  bool resetSent = resetReady && seeded.id &&
    passiveWire(client, cs.ptr(), forward, ZuMv(reset));
  bool resetClosed = resetSent && wait(listener.events[1].closed);
  drainOwners(server);
  drainOwners(client);
  auto resetRecord = closedRecord(server, ss.ptr(), resetKey);
  auto receivedAfter = ZmBlock<ZuTuple<unsigned, unsigned>>{}([
    &client, &resetPeer
  ](auto done) {
    client.rxRun([&resetPeer, done]() mutable {
      done(ZuTuple{resetPeer.packets, resetPeer.errors});
    });
  });
  ZuCHECK(resetClosed && listener.events[1].connected == 1 &&
      listener.events[1].disconnected == 1 && listener.events[1].peerClosed == 1 &&
      !unsigned(listener.events[1].errors) && !listener.events[1].bytes &&
      resetRecord.id && resetRecord.id != seeded.id && !resetRecord.fin &&
      resetRecord.timestamps && !admission(server, ss.ptr()).total &&
      !historyLinks(listener) && receivedAfter.p<0>() == receivedBefore &&
      !receivedAfter.p<1>(),
    "exact-sequence RST without TS must close the live owner once, not disappear into history "
    "or emit an ACK/reset reflection");
  bool stopped = listener.stop();
  bool secondOwner = bool(listener.kept[1]);
  ZmBlock<>{}([&server, &listener](auto done) {
    server.rxRun([&listener, done]() mutable {
      listener.kept[0] = nullptr;
      listener.kept[1] = nullptr;
      done();
    });
  });
  bool destroyed = wait(listener.events[0].destroyed) &&
    (!secondOwner || wait(listener.events[1].destroyed));
  listener.final();
  stopped = client.stop() && server.stop() && stopped;
  ZuCHECK(stopped && destroyed && !unsigned(listener.errors) &&
      listener.events[0].destroyedCount == 1 && listener.events[1].destroyedCount == 1 &&
      listener.events[1].disconnected == 1 && listener.events[1].peerClosed == 1,
    "PAWS and no-TS reset owners must destruct once after listener and backing drain");
  alarm(0);
}

static void testPAWS(bool v6)
{
  ZuTestScopeRT(testPAWS);
  Zi::Name name;
  name << "paws-wrap-" << (v6 ? "v6" : "v4");
  auto directory = ZiTestResidue::dir(name);
  auto child = fork();
  if (!child) {
    execl("/bin/bash", "bash", "./zutcpmxlossfixture.sh", v6 ? "6" : "4", directory.data(),
      "paws-wrap", static_cast<char *>(nullptr));
    _exit(127);
  }
  int status = -1;
  if (child > 0) {
    pid_t result;
    do { result = waitpid(child, &status, 0); } while (result < 0 && errno == EINTR);
    if (result != child) status = -1;
  }
  ZuCheckRT(!status, log_("actual endpoint PAWS rejection and timestamp wrap; logs ", directory));
}

static void testPassiveTuplePair(bool v6)
{
  ZuTestScope(testPassiveTuplePair);
  alarm(45);
  unsigned a = ZuBox<unsigned>{getenv("ZXDP_RX_TEST_IFINDEX")};
  unsigned b = ZuBox<unsigned>{getenv("ZXDP_RX_TEST_PEER_IFINDEX")};
  const char *remoteText = v6 ? "2001:db8::21" : "198.18.0.21";
  ZiIP local{v6 ? "2001:db8::20" : "198.18.0.20"}, remote{remoteText};
  ZiMultiplex client{mxParams()}, server{mxParams()};
  ZmRef<Zutcp::Stack> cs = new Zutcp::Stack{&client, {.interface = a}};
  ZmRef<Zutcp::Stack> ss = new Zutcp::Stack{&server, {.interface = b}};
  client.plugin(cs);
  server.plugin(ss);
  HistoryClient app;
  HistoryServer listener{v6}, other{v6, 18195};
  bool initialized = app.init(Zutcp::ClientParams{&client}.maxCxns(2).maxPendingCxns(1)
      .closedTupleRingSize(2)) &&
    listener.init(Zutcp::ServerParams{&server}.maxCxns(2).maxPendingCxns(1)
      .closedTupleRingSize(2)) &&
    other.init(Zutcp::ServerParams{&server}.maxCxns(2).maxPendingCxns(1)
      .closedTupleRingSize(2));
  bool started = initialized && start(client) && start(server) &&
    listener.start() && other.start();
  ZuCHECK(started, "passive tuple fixture must start wildcard listeners and real AF_XDP Stacks");
  if (!started) { alarm(0); return; }
  HistoryEvents cli[4];
  ZmRef<HistoryCli> cxn[4];
  Zutcp::TCPHeader origins[4];
  auto establish = [
    &app, &cli, &cxn, &origins, &client, local, remote, remoteText
  ](unsigned i, uint16_t port, uint16_t localPort = 0) {
    if (!localPort) localPort = uint16_t(49152 + i);
    cxn[i] = new HistoryCli{&app, &cli[i], {local, localPort}};
    cxn[i]->connect(Zutcp::Host{remoteText}, port, remote);
    bool ready = wait(cli[i].established);
    if (ready) origins[i] = historyBases(client, cxn[i]);
    return ready;
  };
  auto exchange = [&cli, &cxn](unsigned i, HistoryServer &hub, unsigned index) {
    return cxn[i]->send(Message, true) && hub.kept[index]->send(Message, true) &&
      wait(cli[i].input) && wait(hub.events[index].input) && wait(cli[i].down) &&
      wait(cli[i].closed) && wait(hub.events[index].closed);
  };
  bool first = establish(0, 18191) && wait(listener.events[0].established) &&
    exchange(0, listener, 0);
  Zutcp::ClosedTupleKey oldKey{ZiSockAddr{remote, 18191}, ZiSockAddr{local, 49152}};
  auto old = closedRecord(server, ss.ptr(), oldKey);
  auto oldStats = historyStats(server, ss.ptr());
  ZiIP wildcard;
  if (v6) wildcard = in6addr_any; else wildcard = in_addr{};
  auto absent = closedRecord(server, ss.ptr(), {ZiSockAddr{wildcard, 18191}, oldKey.p<1>()});
  bool fresh = establish(1, 18191) && wait(listener.events[1].established);
  PassiveRouteGate gate{&server};
  bool held = gate.hold();
  auto forward = route(client, cs.ptr(), local, remote);
  auto insufficient = passiveInitial(
    oldKey.p<0>(), oldKey.p<1>());
  insufficient.tcp.seq = old.rxNext - 1;
  bool injected = forward.mtu && passiveWire(client, cs.ptr(), forward, insufficient);
  bool marker = fresh && cxn[1]->send(Message, false) && wait(listener.events[1].input);
  auto guarded = admission(server, ss.ptr());
  auto unchanged = closedRecord(server, ss.ptr(), oldKey);
  auto guardedStats = historyStats(server, ss.ptr());
  ZuCHECK(first && old.id && !absent.id && listener.localIP().wildcard() &&
      listener.events[0].actualLocal == oldKey.p<0>() &&
      listener.events[0].actualPeer == oldKey.p<1>() && held && injected && marker &&
      listener.acceptedCount == 2 && guarded.total == 1 && !guarded.pending &&
      !guarded.refused && sameHistory(old, unchanged) && sameRing(oldStats, guardedStats),
    "wire fresh SYN for a retained actual tuple must fail before slot/route/factory "
    "while a fresh live flow proves ordered ingress service on the wildcard listener");
  // A live tuple route must remain first even when an older tuple record exists.
  auto liveKey = Zutcp::ClosedTupleKey{ZiSockAddr{remote, 18191}, ZiSockAddr{local, 49153}};
  auto liveRecord = passiveRemember(server, ss.ptr(), {.key = liveKey,
    .txNext = 3, .rxNext = 4});
  auto liveDuplicate = passiveInitial(liveKey.p<0>(), liveKey.p<1>());
  auto liveHeader = historyBases(client, cxn[1]);
  liveDuplicate.tcp.seq = liveHeader.seq;
  liveDuplicate.tcp.ack = liveHeader.ack;
  liveDuplicate.tcp.flags = 0x10;
  liveDuplicate.tcp.options = liveHeader.options;
  liveDuplicate.tcp.window = liveHeader.window;
  auto serverHeader = historyBases(server, listener.kept[1]);
  bool liveQualified = ZmBlock<bool>{}([&server, &listener, liveDuplicate](auto done) {
    server.rxRun([&listener, liveDuplicate, done]() mutable {
      listener.events[1].probeInput = liveDuplicate.buf.ptr();
      done(listener.kept[1]->acceptsLiveTCP(liveDuplicate));
    });
  });
  listener.endpointDatagram(liveDuplicate);
  auto routedSlots = admission(server, ss.ptr());
  auto routedRecord = closedRecord(server, ss.ptr(), liveKey);
  bool routed = listener.events[1].routedProbes == 1;
  server.rxRun([&listener] { listener.events[1].probeInput = nullptr; });
  bool finished = gate.finish();
  if (!(routed && routedSlots.total == 1 && !routedSlots.pending &&
      listener.acceptedCount == 2 && sameHistory(liveRecord, routedRecord) && finished))
    log_("live tuple v6=", unsigned(v6), " qualified=", unsigned(liveQualified),
      " probes=", listener.events[1].routedProbes, " total=", routedSlots.total,
      " pending=", routedSlots.pending, " accepted=", listener.acceptedCount,
      " same=", unsigned(sameHistory(liveRecord, routedRecord)), " finished=", unsigned(finished),
      " payload=", liveDuplicate.buf->length, " seq=", liveDuplicate.tcp.seq,
      " ack=", liveDuplicate.tcp.ack, " TS=", unsigned(liveDuplicate.tcp.options.flags), ':',
      liveDuplicate.tcp.options.tsval, " peerNext=", serverHeader.ack,
      " localNext=", serverHeader.seq, " peerTS=", unsigned(serverHeader.options.flags), ':',
      serverHeader.options.tsecr);
  ZuCHECK(routed && routedSlots.total == 1 && !routedSlots.pending &&
      listener.acceptedCount == 2 && sameHistory(liveRecord, routedRecord) && finished,
    "an existing tuple route must receive its duplicate segment "
    "ahead of new-owner tuple admission");
  bool second = listener.kept[1]->send(Message, true);
  cxn[1]->disconnect(0);
  second &= wait(cli[1].input) && wait(cli[1].down) && wait(cli[1].closed) &&
    wait(listener.events[1].closed);
  ZuCHECK(second && !admission(server, ss.ptr()).total &&
      !unsigned(listener.errors) && !unsigned(cli[0].errors) && !unsigned(cli[1].errors),
    "both real flows must retire through FIN while their application references remain held");
  // Only the active Stack evicts its record; passive history remains authoritative.
  auto clientOld = closedRecord(client, cs.ptr(), {oldKey.p<1>(), oldKey.p<0>()});
  passiveRemember(client, cs.ptr(), {.key = {ZiSockAddr{local, 19990},
    ZiSockAddr{remote, 19991}}});
  auto activeAbsent = closedRecord(client, cs.ptr(), clientOld.key);
  auto retained = closedRecord(server, ss.ptr(), oldKey);
  static constexpr auto ReuseMessage = "reuse-mx-byte-path!"_z;
  ZuCSpan reuseMessage{ReuseMessage};
  cli[2].expected = reuseMessage;
  ZmBlock<>{}([&server, &listener, reuseMessage](auto done) {
    server.rxRun([&listener, reuseMessage, done]() mutable {
      listener.events[2].expected = reuseMessage;
      done();
    });
  });
  bool reused = !activeAbsent.id && sameHistory(old, retained) &&
    establish(2, 18191, 49152) && wait(listener.events[2].established);
  auto reuseServer = reused ? historyBases(server, listener.kept[2]) : Zutcp::TCPHeader{};
  auto after = [](uint32_t value, uint32_t prior) {
    auto distance = uint32_t(value - prior);
    return distance && distance < 0x80000000;
  };
  auto during = closedRecord(server, ss.ptr(), oldKey);
  auto duringStats = historyStats(server, ss.ptr());
  ZuCHECK(reused && clientOld.id && listener.acceptedCount == 3 &&
      listener.events[2].actualPeer == oldKey.p<1>() &&
      listener.events[2].actualLocal == oldKey.p<0>() &&
      after(origins[2].seq - 1, old.rxNext - 1) &&
      after(reuseServer.seq - 1, old.txNext - 1) && sameHistory(old, during),
    "sequence-qualified fresh SYN must establish the same actual tuple "
    "without replacing retained closed evidence during setup");
  auto claimed = passiveInitial(oldKey.p<0>(), oldKey.p<1>());
  claimed.tcp.seq = origins[2].seq - 1;
  bool claimHeld = gate.hold();
  listener.endpointDatagram(claimed);
  auto claimedSlots = admission(server, ss.ptr());
  bool claimDone = gate.finish();
  ZuCHECK(claimHeld && claimedSlots.total == 1 && !claimedSlots.pending &&
      claimDone && listener.acceptedCount == 3 &&
      sameHistory(during, closedRecord(server, ss.ptr(), oldKey)),
    "a fresh SYN cannot create a second owner for an already established reused tuple");
  auto oldOptions = replayOptions(during, clientOld);
  auto replay = [&client, cs, forward, old, clientOld, oldOptions](bool fin) {
    auto packet = Zutcp::inlineAllocator()->allocBuf();
    if (!packet) return false;
    if (!fin) memcpy(packet->data(), Message.data(), Message.length());
    packet->length = fin ? 0 : unsigned(Message.length());
    Zutcp::Datagram datagram{ZuMv(packet), old.key.p<0>()};
    datagram.local = old.key.p<1>();
    datagram.tcp = {.srcPort = 49152, .dstPort = 18191,
      .seq = uint32_t(old.rxNext - 1 - (fin ? 0 : Message.length())),
      .ack = old.txNext, .flags = uint8_t(fin ? 0x11 : 0x18), .options = oldOptions};
    return passiveWire(client, cs.ptr(), forward, ZuMv(datagram));
  };
  bool replayed = reused && replay(false) && replay(true);
  bool newInput = replayed && cxn[2]->send(reuseMessage, false) &&
    wait(listener.events[2].input);
  drainOwners(server);
  auto replied = closedRecord(server, ss.ptr(), oldKey);
  auto expectedReply = during;
  if (during.timestamps) {
    expectedReply.tsRecent = oldOptions.tsval;
    expectedReply.tsRecentTime = replied.tsRecentTime;
    expectedReply.lastSentTS = replied.lastSentTS;
  }
  bool timestampReply = !during.timestamps || (*replied.tsRecentTime &&
    int32_t(replied.lastSentTS - during.lastSentTS) >= 0);
  ZuCHECK(newInput && listener.events[2].bytes == reuseMessage.length() &&
      !unsigned(listener.events[2].errors) && !unsigned(listener.events[2].disconnected) &&
      timestampReply && sameHistory(expectedReply, replied) &&
      sameRing(duringStats, historyStats(server, ss.ptr())) &&
      admission(server, ss.ptr()).total == 1,
    "old-incarnation DATA and FIN must use retained history while the new owner receives only "
    "its distinct exact bytes and remains established");
  bool newClosed = newInput && listener.kept[2]->send(reuseMessage, true);
  if (cxn[2]) cxn[2]->disconnect(0);
  newClosed &= wait(cli[2].input) && wait(cli[2].down) && wait(cli[2].closed) &&
    wait(listener.events[2].closed);
  auto replacement = closedRecord(server, ss.ptr(), oldKey);
  ZuCHECK(newClosed && replacement.id != old.id && replacement.fin &&
      replacement.txNext == uint32_t(reuseServer.seq + reuseMessage.length() + 1) &&
      replacement.rxNext == uint32_t(origins[2].seq + reuseMessage.length() + 1) &&
      !admission(client, cs.ptr()).total && !admission(server, ss.ptr()).total &&
      cli[2].disconnected == 1 && listener.events[2].disconnected == 1 &&
      !unsigned(cli[2].errors) && !unsigned(listener.events[2].errors),
    "new sequence incarnation must complete exact DATA/FIN and replace old history "
    "only after its own owner retires");
  server.rxRun([&listener] { listener.rejectCandidates = true; });
  ZiSockAddr actual{remote, 18191}, peer{local, 19190};
  auto race = passiveInitial(actual, peer);
  uint32_t measuredISN = 0;
  bool sampled = ZmBlock<bool>{}([&server, ss, actual, peer, &measuredISN](auto done) {
    server.rxRun([ss, actual, peer, &measuredISN, done]() mutable {
      done(ss->chooseISN(actual, peer, measuredISN));
    });
  });
  auto priorRace = passiveRemember(server, ss.ptr(), {.key = {actual, peer},
    .txNext = uint32_t(measuredISN - 1024), .rxNext = race.tcp.seq,
    .fin = true});
  bool raceHeld = gate.hold();
  listener.endpointDatagram(race);
  auto pending = admission(server, ss.ptr());
  listener.endpointDatagram(race);
  auto duplicate = admission(server, ss.ptr());
  listener.endpointDatagram(passiveInitial(actual, peer, UINT32_MAX - 1));
  auto exclusive = admission(server, ss.ptr());
  auto raced = passiveRemember(server, ss.ptr(), {.key = {actual, peer},
    .txNext = 303, .rxNext = race.tcp.seq + 1, .tsRecent = 505, .lastSentTS = 606,
    .timestamps = v6, .fin = true});
  auto raceStats = historyStats(server, ss.ptr());
  bool completed = gate.finish();
  auto released = admission(server, ss.ptr());
  auto raceAfter = closedRecord(server, ss.ptr(), raced.key);
  ZuCHECK(sampled && priorRace.id && raceHeld && pending.total == 1 && pending.pending == 1 &&
      duplicate.total == 1 && duplicate.pending == 1 && duplicate.refused == pending.refused &&
      exclusive.total == 1 && exclusive.pending == 1 && exclusive.refused == pending.refused &&
      completed && !released.total && !released.pending && !listener.rejectedFactories &&
      sameHistory(raced, raceAfter) && sameRing(raceStats, historyStats(server, ss.ptr())),
    "pending SYN retries and conflicting SYN tuple claims must keep one token; "
    "replacement history during held real route work must revalidate the selected "
    "candidate, prevent factory adoption and release that token on completion");
  bool refuseHeld = gate.hold();
  listener.endpointDatagram(passiveInitial(actual, peer));
  auto refused = admission(server, ss.ptr());
  auto beforeRelease = closedRecord(server, ss.ptr(), raced.key);
  bool noWork = gate.finish();
  ZuCHECK(refuseHeld && !refused.total && !refused.pending && !refused.refused &&
      !listener.rejectedFactories && noWork && sameHistory(raced, beforeRelease) &&
      sameRing(raceStats, historyStats(server, ss.ptr())),
    "a fresh SYN on the retained tuple must remain refused without refreshing evidence or FIFO");
  // Unrelated remote port and local address each reach the actual accepted factory.
  ZiIP otherLocal{v6 ? "2001:db8::22" : "198.18.0.22"};
  for (unsigned n = 0; n < 2; ++n) {
    auto dest = n ? ZiSockAddr{otherLocal, 18191} : actual;
    auto source = n ? peer : ZiSockAddr{local, 19191};
    bool routeHeld = gate.hold();
    listener.endpointDatagram(passiveInitial(dest, source));
    auto one = admission(server, ss.ptr());
    bool done = gate.finish();
    auto zero = admission(server, ss.ptr());
    ZuCHECK(routeHeld && one.total == 1 && one.pending == 1 && done &&
	!zero.total && !zero.pending && listener.rejectedFactories == n + 1 &&
	sameHistory(raced, closedRecord(server, ss.ptr(), raced.key)),
      "an unrelated address or peer port must reach the real factory "
      "without weakening exact guard");
  }
  passiveRemember(server, ss.ptr(), {.key = {actual, ZiSockAddr{local, 19192}}});
  auto oldest = closedRecord(server, ss.ptr(), raced.key);
  auto fifo = historyStats(server, ss.ptr());
  listener.endpointDatagram(passiveInitial(actual, peer));
  auto blocked = admission(server, ss.ptr());
  auto stillOldest = closedRecord(server, ss.ptr(), raced.key);
  passiveRemember(server, ss.ptr(), {.key = {actual, ZiSockAddr{local, 19193}}});
  auto evicted = closedRecord(server, ss.ptr(), raced.key);
  bool retryHeld = gate.hold();
  listener.endpointDatagram(passiveInitial(actual, peer));
  auto retried = admission(server, ss.ptr());
  bool retriedDone = gate.finish();
  auto retriedFree = admission(server, ss.ptr());
  ZuCHECK(sameHistory(raced, oldest) && !blocked.total && !blocked.pending &&
      sameHistory(raced, stillOldest) && !evicted.id && retryHeld && retried.total == 1 &&
      retried.pending == 1 && retriedDone && !retriedFree.total && !retriedFree.pending &&
      listener.rejectedFactories == 3 &&
      historyStats(server, ss.ptr()).evicted == fifo.evicted + 1,
    "refusal must preserve oldest FIFO position and actual capacity eviction must permit retry");
  bool stopHeld = gate.hold();
  listener.endpointDatagram(passiveInitial(actual, {local, 19194}));
  auto stoppingSlot = admission(server, ss.ptr());
  ZmSemaphore stoppedEvent;
  ZmAtomic<unsigned> stopCalls = 0;
  static_cast<ZmEngine<HistoryServer> &>(listener).stop([&stoppedEvent, &stopCalls](bool ok) {
    if (ok) ++stopCalls;
    stoppedEvent.post();
  });
  drainOwners(server); // The engine wake posts the actual Server stop turn to Rx.
  auto stopping = admission(server, ss.ptr());
  bool deferredStop = !stopCalls && listener.state() == ZmEngineState::Stopping;
  passiveRemember(server, ss.ptr(), {.key = {actual, ZiSockAddr{local, 19194}}});
  bool stoppedRoute = gate.finish();
  bool stopped = wait(stoppedEvent);
  auto drained = admission(server, ss.ptr());
  ZuCHECK(stopHeld && stoppingSlot.total == 1 && stoppingSlot.pending == 1 &&
      stopping.total == 1 && deferredStop && stoppedRoute && stopped && stopCalls == 1 &&
      !drained.total && !drained.pending && listener.rejectedFactories == 3 &&
      other.listening() && ss->ready(),
    "pending stop must await real route completion, release its one slot and notify once");
  bool survivor = establish(3, 18195, 49154) && wait(other.events[0].established) &&
    exchange(3, other, 0);
  ZuCHECK(survivor && cli[3].bytes == Message.length() &&
      other.events[0].bytes == Message.length() &&
      !unsigned(cli[3].errors) && !unsigned(other.events[0].errors) && other.listening() &&
      !admission(client, cs.ptr()).total && !admission(server, ss.ptr()).total,
    "a surviving listener on the same Stack must exchange fresh exact bytes after pending stop");
  ZtString<> controlPath;
  controlPath << getenv("ZUTCP_PASSIVE_TUPLE_DIR") << "/passive-controls.json";
  ZtString<> controls;
  controls << "{\"guarded_total\":" << guarded.total << ",\"guarded_pending\":" << guarded.pending
    << ",\"race_pending\":" << pending.pending << ",\"duplicate_pending\":" << duplicate.pending
    << ",\"released_total\":" << released.total << ",\"factory_calls\":"
    << listener.rejectedFactories.load_() << ",\"stop_callbacks\":" << stopCalls.load_() << "}\n";
  bool recorded = writeFile(controlPath, controls);
  ZuCHECK(recorded, "controlled actual Server path snapshots must remain in fixture evidence");
  bool allStopped = other.stop();
  client.rxRun([&cxn] { for (unsigned i = 0; i < 4; ++i) cxn[i] = nullptr; });
  bool owners = wait(cli[0].destroyed) && wait(cli[1].destroyed) && wait(cli[2].destroyed) &&
    wait(cli[3].destroyed);
  server.rxRun([&listener, &other] {
    listener.kept[0] = nullptr; listener.kept[1] = nullptr; listener.kept[2] = nullptr;
    other.kept[0] = nullptr;
  });
  bool peers = wait(listener.events[0].destroyed) && wait(listener.events[1].destroyed) &&
    wait(listener.events[2].destroyed) && wait(other.events[0].destroyed);
  app.final(); listener.final(); other.final();
  allStopped = client.stop() && server.stop() && allStopped;
  ZuCHECK(allStopped && owners && peers && !unsigned(listener.errors) && !unsigned(other.errors),
    "passive tuple probes must return all owners and shared slots before fixture teardown");
  alarm(0);
}

static void testPassiveTuple(bool v6)
{
  ZuTestScopeRT(testPassiveTuple);
  Zi::Name name;
  name << "passive-tuple-" << (v6 ? "v6" : "v4");
  auto directory = ZiTestResidue::dir(name);
  auto child = fork();
  if (!child) {
    execl("/bin/bash", "bash", "./zutcpmxlossfixture.sh", v6 ? "6" : "4", directory.data(),
      "passive-tuple", static_cast<char *>(nullptr));
    _exit(127);
  }
  int status = -1;
  if (child > 0) {
    pid_t result;
    do { result = waitpid(child, &status, 0); } while (result < 0 && errno == EINTR);
    if (result != child) status = -1;
  }
  ZuCheckRT(!status, log_("passive retained tuple guard on actual Server paths; logs ",
    directory));
}

static void parentCases()
{
  ZuTestMain();
  ZuTestCall(testMxConnections, false, false);
  ZuTestCall(testMxConnections, true, false);
  ZuTestCall(testMxConnections, false, true);
  ZuTestCall(testMxConnections, true, true);
  ZuTestCall(testMxConnections, false, true, false, false, false, true);
  ZuTestCall(testMxConnections, true, true, false, false, false, true);
  ZuTestCall(testMxConnections, false, true, false, false, false, false, PassiveTotal);
  ZuTestCall(testMxConnections, true, true, false, false, false, false, PassiveTotal);
  ZuTestCall(testMxConnections, false, true, false, false, false, false, PassivePending);
  ZuTestCall(testMxConnections, true, true, false, false, false, false, PassivePending);
  ZuTestCall(testConnectTimeout, false);
  ZuTestCall(testConnectTimeout, true);
  ZuTestCall(testUserTimeout, false);
  ZuTestCall(testUserTimeout, true);
  ZuTestCall(testGracefulFin, false, false);
  ZuTestCall(testGracefulFin, true, false);
  ZuTestCall(testGracefulFin, false, true);
  ZuTestCall(testGracefulFin, true, true);
  ZuTestCall(testGracefulFin, false, false, true);
  ZuTestCall(testGracefulFin, true, false, true);
  ZuTestCall(testGracefulFin, false, true, true);
  ZuTestCall(testGracefulFin, true, true, true);
  ZuTestCall(testFinalAckLoss, false, false);
  ZuTestCall(testFinalAckLoss, true, false);
  ZuTestCall(testFinalAckLoss, false, true);
  ZuTestCall(testFinalAckLoss, true, true);
  ZuTestCall(testFinWindow, false, false);
  ZuTestCall(testFinWindow, true, false);
  ZuTestCall(testFinWindow, false, true);
  ZuTestCall(testFinWindow, true, true);
  ZuTestCall(testReset, false, false);
  ZuTestCall(testReset, true, false);
  ZuTestCall(testReset, false, true);
  ZuTestCall(testReset, true, true);
  ZuTestCall(testAbort, false, true, true);
  ZuTestCall(testAbort, true, true, true);
  ZuTestCall(testAbort, false, false, false);
  ZuTestCall(testAbort, true, false, false);
  ZuTestCall(testAbort, false, false, true);
  ZuTestCall(testAbort, true, false, true);
  ZuTestCall(testPositiveWindow, false);
  ZuTestCall(testPositiveWindow, true);
  ZuTestCall(testPAWS, false);
  ZuTestCall(testPAWS, true);
  ZuTestCall(testPassiveTuple, false);
  ZuTestCall(testPassiveTuple, true);
  ZuTestCall(testNativeNumericClose, false, false);
  ZuTestCall(testNativeNumericClose, true, false);
  ZuTestCall(testNativeNumericClose, false, true);
  ZuTestCall(testNativeNumericClose, true, true);
  ZuTestCall(testNativeLifecycle, false, false);
  ZuTestCall(testNativeLifecycle, true, false);
  ZuTestCall(testNativeLifecycle, false, true);
  ZuTestCall(testNativeLifecycle, true, true);
  ZuTestCall(testConservativeTuple, false);
  ZuTestCall(testConservativeTuple, true);
  ZuTestCall(testSharedAdmission, false, false);
  ZuTestCall(testSharedAdmission, true, false);
  ZuTestCall(testSharedAdmission, false, true);
  ZuTestCall(testSharedAdmission, true, true);
  ZuTestCall(testNativeHistory, false, false);
  ZuTestCall(testNativeHistory, true, false);
  ZuTestCall(testNativeHistory, false, true);
  ZuTestCall(testNativeHistory, true, true);
  ZuTestCall(testMappedLoss, false);
  ZuTestCall(testMappedLoss, true);
  ZuTestCall(testMappedLoss, false, true);
  ZuTestCall(testMappedLoss, true, true);
  ZuTestCall(testMappedLoss, false, true, true);
  ZuTestCall(testMappedLoss, true, true, true);
  ZuTestCall(testMappedLoss, false, true, false, true);
  ZuTestCall(testMappedLoss, true, true, false, true);
  ZiLog::stop();
}

static void lossCase(bool v6, bool window = false, bool restart = false)
{
  ZuTestMain();
  ZuTestCall(testMxConnections, v6, false, !window, window, restart);
  ZiLog::stop();
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  if (geteuid() || !getenv("ZUTCP_MX_FIXTURE")) {
    std::cout << "1..0 # SKIP requires private native Mx connection fixture\n";
    return 0;
  }
  ZiLog::init("zutcpmxtest");
  ZiLog::level(0);
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();
  const char *paws = getenv("ZUTCP_PAWS_WRAP");
  const char *positive = getenv("ZUTCP_POSITIVE_WINDOW");
  const char *passiveTuple = getenv("ZUTCP_PASSIVE_TUPLE");
  const char *lifecycle = getenv("ZUTCP_NATIVE_LIFECYCLE");
  const char *tuple = getenv("ZUTCP_CONSERVATIVE_TUPLE");
  const char *shared = getenv("ZUTCP_SHARED_ADMISSION");
  const char *history = getenv("ZUTCP_NATIVE_HISTORY");
  const char *abortCase = getenv("ZUTCP_MX_ABORT");
  const char *reset = getenv("ZUTCP_MX_RST");
  const char *finWindow = getenv("ZUTCP_MX_FIN_WINDOW");
  const char *fin = getenv("ZUTCP_MX_FIN");
  const char *loss = getenv("ZUTCP_MX_LOSS");
  const char *window = getenv("ZUTCP_MX_WINDOW");
  const char *restart = getenv("ZUTCP_MX_RESTART");
  if (restart) window = restart;
  char lossPayload[1024];
  if (loss || window || positive) {
    for (unsigned i = 0; i < sizeof(lossPayload); ++i) lossPayload[i] = char(i * 13 + 7);
    Message = {lossPayload, positive ? 1000 : sizeof(lossPayload)};
  }
  ZiTestResidue::init("zutcpmxtest");
  if (paws) {
    ZuTestMain();
    ZuTestCall(testPAWSPair, *paws == '6');
    ZiLog::stop();
  } else if (passiveTuple) {
    ZuTestMain();
    ZuTestCall(testPassiveTuplePair, *passiveTuple == '6');
    ZiLog::stop();
  } else if (positive) {
    ZuTestMain();
    ZuTestCall(testPositiveWindowPair, *positive == '6');
    ZiLog::stop();
  } else if (lifecycle) {
    ZuTestMain();
    ZuTestCall(testNativeLifecyclePair, *lifecycle == '6',
      bool(getenv("ZUTCP_NATIVE_HUB_STOP")), bool(getenv("ZUTCP_NATIVE_NUMERIC")),
      bool(getenv("ZUTCP_NATIVE_SERVER_ERROR")));
    ZiLog::stop();
  } else if (tuple) {
    ZuTestMain();
    ZuTestCall(testConservativeTuplePair, *tuple == '6');
    ZiLog::stop();
  } else if (shared) {
    ZuTestMain();
    ZuTestCall(testSharedAdmissionPair, *shared == '6',
      bool(getenv("ZUTCP_SHARED_PASSIVE")));
    ZiLog::stop();
  } else if (history) {
    ZuTestMain();
    ZuTestCall(testNativeHistoryPair, *history == '6',
      bool(getenv("ZUTCP_NATIVE_SERVER_FIRST")));
    ZiLog::stop();
  } else if (abortCase) {
    ZuTestMain();
    ZuTestCall(testAbortPair, *abortCase == '6', bool(getenv("ZUTCP_ABORT_REJECT")),
      bool(getenv("ZUTCP_ABORT_SERVER")));
    ZiLog::stop();
  } else if (reset) {
    ZuTestMain();
    ZuTestCall(testResetPair, *reset == '6', bool(getenv("ZUTCP_RST_SERVER")));
    ZiLog::stop();
  } else if (finWindow) {
    ZuTestMain();
    ZuTestCall(testGracefulFin, *finWindow == '6',
      bool(getenv("ZUTCP_FIN_SERVER")), false, false, true);
    ZiLog::stop();
  } else if (fin) {
    ZuTestMain();
    ZuTestCall(testGracefulFin, *fin == '6', bool(getenv("ZUTCP_FIN_SERVER")), false, true);
    ZiLog::stop();
  } else if (loss || window)
    lossCase(*(window ? window : loss) == '6', bool(window), bool(restart));
  else parentCases();
}
