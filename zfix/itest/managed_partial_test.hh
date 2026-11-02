//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef managed_partial_test_HH
#define managed_partial_test_HH

#if defined(__linux__) && !defined(ZFIX_TEST_TLS)
#include <arpa/inet.h>
#include <errno.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <unistd.h>

// Real kernel backpressure is the only injected output condition. The native
// close callback is retained separately, after displacement physically closes A.
struct PartialState : TestState {
  ZmSemaphore		initialReady, peerReady, selected, blocked, oldClose, oldRetired,
    archived, serverStopped;
  ZmAtomic<unsigned>	accepts{0}, serverStops{0}, bStops{0}, delivered{0};
  ZmAtomic<unsigned>	watch{0}, shortWrites{0}, wouldBlock{0}, published{0}, completed{0};
  ZmAtomic<unsigned>	loads{0}, replacements{0};
  ZmAtomic<int>		socket{-1};
  // Native Rx publishes this handle via oldClose; main consumes it on native Rx.
  alignas(Zm::CacheLineSize)
  ZmFn<void()>		retire;
  // Immutable after main publishes the initial server enqueue task.
  TxBody::Ref		replacement;
  // AppTx; main reads after the final owner drain.
  alignas(Zm::CacheLineSize)
  uint64_t		probeTarget = 0;
};

// Published before worker creation, cleared after all workers stop. This ELF
// observer passes through actual syscall results and observes only A's socket.
static PartialState *partialSend = nullptr;
extern "C" __attribute__((visibility("default")))
ssize_t send(int fd, const void *buf, size_t len, int flags)
{
  ssize_t n = syscall(SYS_sendto, fd, buf, len, flags, nullptr, 0);
  int error = errno;
  auto state = partialSend;
  if (state && state->watch.load_() && fd == state->socket.load_()) {
    if (n > 0 && size_t(n) < len) ++state->shortWrites;
    if (n < 0 && (error == EAGAIN || error == EWOULDBLOCK) &&
	!state->wouldBlock.xchAdd(1)) state->blocked.post();
  }
  errno = error; return n;
}

struct PartialServer : Zfix::Server<PartialServer, Dictionary, Network> {
  struct Link;
  struct Cxn;
  PartialState	*state;
  ZmRef<Link>		session; // native Rx application route

  explicit PartialServer(PartialState *s) : state{s} { }

  ZiIP localIP() const { return ZiIP{"127.0.0.1"}; }
  unsigned localPort() const { return 0; }
  unsigned nAccepts() const { return 1; }

  void listening(const ZiListenInfo &i) { state->port = i.port; state->listening.post(); }
  void listenFailed(bool) { state->fail(); state->listening.post(); }
  ZiConnection *accepted(const ZiCxnInfo &);
};

struct PartialClient : Zfix::Client<PartialClient, Dictionary, Network> {
  struct Link;
  PartialState	*state;

  explicit PartialClient(PartialState *s) : state{s} { }
};

struct PartialServer::Link : ZmNHeap<Link>,
    Peer<Link, Zfix::SrvLink<PartialServer, Link, Dictionary, Network>, false> {
friend inline ZuStringT<"Zfix.ManagedPartial.Server"> ZmNHeapID(Link *);
  using ZmNHeap<Link>::operator new;
  using ZmNHeap<Link>::operator delete;
  using Parent = Peer<Link, Zfix::SrvLink<PartialServer, Link, Dictionary, Network>, false>;
  using Parent::Peer;

  void fixEventRx(SessionEvent e, ZmRef<Msg>) {
    if (e.actions & SessionAction::Failure) state()->fail();
    if (!(e.actions & SessionAction::Ready) || readyConnection == e.connection) return;
    readyConnection = e.connection;
    (e.connection == 1 ? state()->initialReady : state()->peerReady).post();
  }

  void fixPublishedTx(TxBody::Ref body, TxContext c, ZuDateTime original) {
    if (!app()->txInvoked() || c.epoch != 1) state()->fail();
    if (!c.replay) {
      auto record = store->find(c.epoch, c.seqNo);
      if (!record || record->body != body) { state()->fail(); return; }
      record->originalTime = original;
      if (body->id() == 301) {
	if (c.connection != 1 || c.seqNo != 2) state()->fail();
	state()->oldSeq = c.seqNo; state()->oldTime = original; ++state()->published;
      }
      if (body->msgType() == "1") {
	if (c.connection != 2 || state()->probeTarget) state()->fail();
	state()->probeTarget = c.seqNo + 1;
      }
    } else if (body->msgType() == "D") {
      if (c.connection != 2 || c.seqNo != 2 || body != state()->replacement ||
	  original != state()->oldTime) state()->fail();
      ++state()->replacements;
    }
  }

  TxReload fixRetrieveTx(TxContext c, uint64_t, uint64_t) {
    if (!app()->txInvoked() || !c.replay || c.epoch != 1) state()->fail();
    if (c.seqNo != 2) return {}; // application suppresses old operational controls
    ++state()->loads;
    return {TxHistory{state()->replacement, state()->oldTime, 1, 2}};
  }

  void fixCompleteTx(TxBody::Ref body, TxContext c, bool ok) {
    if (!app()->txInvoked()) state()->fail();
    if (body->id() == 301) {
      if (c.connection != 1 || c.seqNo != 2 || c.replay || ok) state()->fail();
      ++state()->completed;
    }
  }

  void fixArchiveTx(uint64_t epoch, uint64_t begin, uint64_t end) {
    if (!app()->txInvoked() || epoch != 1 || begin >= end) state()->fail();
    if (state()->probeTarget && end >= state()->probeTarget) {
      state()->probeTarget = 0; state()->archived.post();
    }
  }

  void fixStopTx(uint64_t) {
    store->tx = this->txProgress_();
    queue.clean();
  }

  void fixStoppedRx(bool) { ++state()->serverStops; state()->serverStopped.post(); }
};

struct PartialServer::Cxn : ZmNHeap<Cxn>, Link::Cxn {
friend inline ZuStringT<"Zfix.ManagedPartial.Cxn"> ZmNHeapID(Cxn *);
  using ZmNHeap<Cxn>::operator new;
  using ZmNHeap<Cxn>::operator delete;
  using Base = Link::Cxn;
  PartialState	*const state;
  PartialServer	*const hub;
  const bool	first;
  bool		held = false; // native Rx

  Cxn(Link *link, const ZiCxnInfo &info, bool initial) : Base{ZmRef<Link>{link}, info},
    state{link->state()}, hub{link->app()}, first{initial} { }

  void disconnected(bool peer) override {
    if (held) return;
    if (!first) { Base::disconnected(peer); return; }
    held = true;
    state->retire = [self = ZmRef(this), peer] {
      self->Base::disconnected(peer);
      // Marker follows actual native cleanup through each owner FIFO.
      self->hub->rxRun([self] {
	self->hub->txRun([self] {
	  self->mx()->txRun([self] {
	    self->hub->rxRun([self] { self->state->oldRetired.post(); });
	  });
	});
      });
    };
    state->oldClose.post();
  }
};

ZiConnection *PartialServer::accepted(const ZiCxnInfo &info)
{
  return new Cxn{session.ptr(), info, state->accepts.xchAdd(1) == 0};
}

struct PartialClient::Link : ZmNHeap<Link>,
    Peer<Link, Zfix::CliLink<PartialClient, Link, Dictionary, Network>, true> {
friend inline ZuStringT<"Zfix.ManagedPartial.Client"> ZmNHeapID(Link *);
  using ZmNHeap<Link>::operator new;
  using ZmNHeap<Link>::operator delete;
  using Parent = Peer<Link, Zfix::CliLink<PartialClient, Link, Dictionary, Network>, true>;
  using Parent::Peer;

  void fixEventRx(SessionEvent e, ZmRef<Msg>) {
    if (e.actions & SessionAction::Failure) state()->fail();
    if (!(e.actions & SessionAction::Ready) || readyConnection == e.connection) return;
    readyConnection = e.connection; state()->done.post();
  }

  Fmt::Error fixReceiveRx(OrderMsg *, const Order &o, const RequestContext &context) {
    if (!app()->rxInvoked() || o.id != "repriced" || context.context().epoch != 1 ||
	context.seqNo() != 2 || !context.possDup() ||
	context.origSendingTime() != state()->oldTime) state()->fail();
    ++state()->delivered; state()->selected.post(); return {};
  }

  void fixStoppedRx(bool) { ++state()->bStops; state()->cliStopped.post(); }
};

// Main is the raw peer: no worker is blocked and no callback changes native
// socket lifecycle. poll bounds the real socket operations without sleep loops.
static bool partialWrite(int fd, ZuCSpan bytes)
{
  while (bytes.length()) {
    pollfd p{fd, POLLOUT, 0};
    if (poll(&p, 1, 10000) != 1 || !(p.revents & POLLOUT)) return false;
    auto n = ::send(fd, bytes.data(), bytes.length(), MSG_NOSIGNAL);
    if (n <= 0) return false;
    bytes.offset(n);
  }
  return true;
}

static bool partialLogon(int fd)
{
  Session::Msg msg; msg.p<Session::Logon>(Session::Logon{});
  Fmt::Error error;
  auto body = TxBody::control(msg, error);
  auto frame = body ? txFrame(*body, txConfig(config(true,
    Transport::FIX4, Version::V4_4)), TxContext{.seqNo = 1,
      .sendingTime = ZuDateTime{Zm::now()}}, error) : ZmRef<ZiIOBuf>{};
  if (error || !frame || !partialWrite(fd, frame->cspan())) return false;
  ZmRef<ZiIOBuf> input = new ZiIOBufAlloc<>;
  for (;;) {
    auto r = Fmt::frame(input->cspan());
    if (r.state == Fmt::FrameState::Complete) {
      RxMsg msg; msg.frame = r.view;
      return !readSession(msg) && msg.seqNo == 1 && r.view.msgType == "A";
    }
    if (r.state != Fmt::FrameState::Incomplete || input->length == input->size) return false;
    pollfd p{fd, POLLIN, 0};
    if (poll(&p, 1, 10000) != 1 || !(p.revents & POLLIN)) return false;
    auto n = ::recv(fd, input->data() + input->length, input->size - input->length, 0);
    if (n <= 0) return false;
    input->length += n;
  }
}

static void managedPartial()
{
  ZuTestScope(managedPartial);
  // Below the field limit, above the deliberately small real TCP window.
  enum { PayloadBytes = 200<<10 };
  PartialState state;
  PartialServer server{&state}; PartialClient client{&state};
  ZiMultiplex mx{ZiMxParams{}.scheduler([](auto &s) {
    s.nThreads(5)
      .thread(1, [](auto &t) { t.isolated(1); }).thread(2, [](auto &t) { t.isolated(1); })
      .thread(3, [](auto &t) { t.isolated(1); }).thread(4, [](auto &t) { t.isolated(1); })
      .thread(5, [](auto &t) { t.isolated(1); });
  }).rxThread(1).txThread(2)};
  partialSend = &state;
  bool started = mx.start(); ZuCheck(started);
  if (!started) { partialSend = nullptr; return; }
  bool si = server.init(Network::ServerParams{&mx, "3", "4"});
  bool ci = client.init(Network::ClientParams{&mx, "3", "4"}); ZuCheck(si && ci);
  bool ss = si && server.start(), cs = ci && client.start(); ZuCheck(ss && cs);
  ZmRef<PartialClient::Link> b;
  int raw = -1; bool attemptedB = false, success = false;
  if (ss && cs) do {
    server.session = new PartialServer::Link{&server};
    server.listen(); bool listening = wait(state.listening); ZuCheck(listening);
    if (!listening) break;
    raw = socket(AF_INET, SOCK_STREAM, 0); ZuCheck(raw >= 0);
    int size = 4096; // small actual TCP windows expose a partial 200 KiB FIX frame
    bool tuned = raw >= 0 && !setsockopt(raw, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size));
    sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_port = htons(state.port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bool connected = tuned && !::connect(raw, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));
    ZuCheck(connected); if (!connected) break;
    bool logon = partialLogon(raw) && wait(state.initialReady); ZuCheck(logon);
    if (!logon) break;
    Fmt::Error error;
    state.replacement = TxBody::encode<OrderMsg>(Order{"repriced"}, 401, error);
    ZuCheck(!error && state.replacement); if (error || !state.replacement) break;
    server.rxRun([link = server.session] {
      int size = 4096; auto s = link->state();
      int fd = link->cxn()->info().socket;
      if (setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &size, sizeof(size))) { s->fail(); return; }
      s->socket = fd; s->watch = 1;
      link->app()->txRun([link] {
	ZmRef<ZiIOBuf> data = new ZiIOBufAlloc<>; data->ensure(PayloadBytes);
	memset(data->data(), 'X', PayloadBytes); data->length = PayloadBytes;
	Fmt::Error error;
	auto body = TxBody::encode<OrderMsg>(Order{data->cspan()}, 301, error);
	if (error || !body) { link->state()->fail(); return; }
	link->enqueue(ZuMv(body));
      });
    });
    bool blocked = wait(state.blocked); state.watch = 0;
    ZuCheck(blocked && state.shortWrites.load_() && state.wouldBlock.load_());
    if (!blocked) break;
    b = new PartialClient::Link{&client};
    Progress checkpoint;
    checkpoint.epoch = 1;
    checkpoint.rx = {2, 2, 0, 0}; checkpoint.tx = {2, 2, 1, 0, 0};
    bool restored = ZmBlock<bool>{}([&client, &b, &checkpoint](auto wake) {
      client.rxRun([&client, &b, &checkpoint, wake = ZuMv(wake)]() mutable {
	if (!b->restoreRx_(checkpoint.epoch, checkpoint.rx)) { wake(false); return; }
	client.txRun([&b, &checkpoint, wake = ZuMv(wake)]() mutable {
	  if (!b->restoreTx_(checkpoint.epoch, checkpoint.tx)) { wake(false); return; }
	  b->store->lastLoad = 1; wake(true);
	});
      });
    });
    ZuCheck(restored); if (!restored) break;
    attemptedB = true; b->connect("127.0.0.1", state.port);
    bool recovered = wait(state.selected) && wait(state.done) && wait(state.peerReady) &&
      wait(state.oldClose);
    ZuCheck(recovered); if (!recovered) break;
    ZuCheck(state.delivered.load_() == 1 && state.published.load_() == 1 &&
      state.loads.load_() &&
      !state.serverStops.load_() && state.retire);
    mx.rxRun([&state] { auto retire = ZuMv(state.retire); if (retire) retire(); });
    bool retired = wait(state.oldRetired); ZuCheck(retired);
    server.session->probe();
    bool usable = retired && wait(state.archived); ZuCheck(usable);
    success = usable;
  } while (false);
  state.watch = 0;
  mx.rxRun([&state] { auto retire = ZuMv(state.retire); if (retire) retire(); });
  if (attemptedB) { if (success) b->logout(); else b->disconnect(); }
  if (raw >= 0) { ::close(raw); raw = -1; }
  bool stoppedB = !attemptedB || wait(state.cliStopped);
  bool stoppedServer = !state.accepts.load_() || wait(state.serverStopped);
  ZuCheck(stoppedB && stoppedServer);
  if (!stoppedB || !stoppedServer) abort();
  bool cstop = !cs || client.stop(), sstop = !ss || server.stop(); ZuCheck(cstop && sstop);
  if (ci) client.final();
  if (si) server.final();
  mx.stop(); b = nullptr; server.session = nullptr; partialSend = nullptr;
  ZuCheck(success && !state.errors.load_() && state.accepts.load_() == 2 &&
    state.serverStops.load_() == 1 && state.bStops.load_() == 1 &&
    state.completed.load_() == 1 && state.published.load_() == 1 &&
    state.delivered.load_() == 1 && state.loads.load_() == state.replacements.load_() &&
    !state.retire && !state.probeTarget);
}
#endif // Linux TCP

#endif /* managed_partial_test_HH */
