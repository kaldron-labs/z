//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details).

// Native Stack peer for independent kernel interoperability. No kernel TCP socket.
#include <stdlib.h>
#include <string.h>
#include <net/if.h>

#include <iostream>

#include <zlib/ZiFile.hh>
#include <zlib/Zutcp.hh>
#include <zlib/ZtString.hh>

ZuDerive(PeerBytes, (ZtString<ZtStringHeapID<"Zutcp.NativePeer.Bytes">>));
// The fixture's bounded request/prefix and cold file-read chunk.
enum { MaxRequest = 1<<20, PrefixBytes = 260, ReadSize = 4096 };
static constexpr uint64_t ResetError = 0x123456789abcdef0ULL;

static bool waitGate(const char *name)
{
  const char *path = getenv(name);
  if (!path) return true;
  ZiFile gate{path, ZiFile::ReadOnly | ZiFile::GC};
  if (!gate) {
    ZiLOG(Error, "zutcppair", ([error = gate.error()](auto &out) {
      out << "fixture gate: " << error;
    }));
    return false;
  }
  char value;
  return gate.read(&value, 1) == 1 && value == 'R';
}

class Result {
public:
  void fail(const char *message) {
    if (!m_failed.xch(1))
      ZiLOG(Error, "zutcppair", ([message = ZeString{message}](auto &out) {
	out << "error: " << message;
      }));
  }
  bool failed() const { return m_failed.load_(); }
  void done() { m_done.post(); }
  void wait() { m_done.wait(); }
  void connected() { m_connected.post(); }
  void waitConnected() { m_connected.wait(); }
private:
  using Flag = ZmAtomic<unsigned>;
  Flag		m_failed = 0;
  ZmSemaphore	m_done, m_connected;
};

class ClientApp : public Zutcp::Client<ClientApp> {
public:
  class Cxn;
  ClientApp(Result &result, const PeerBytes &request, unsigned windowChunk = 0) :
    m_result{result}, m_request{request}, m_windowChunk{windowChunk} { }
  Result &result() { return m_result; }
  ZuCSpan request() const { return {m_request.data(), m_request.length()}; }
  unsigned windowChunk() const { return m_windowChunk; }
  bool tfo() const { return m_tfo; }
  bool earlyData(Cxn *);
  bool allowEarlyStream(Cxn *, uint64_t, bool);
  void earlyDataAccepted(Cxn *);
  void earlyDataRejected(Cxn *);
private:
  Result		&m_result;
  const PeerBytes	&m_request;
  const unsigned	m_windowChunk;
  const bool	m_tfo = getenv("ZUTCP_KERNEL_TFO");
  const bool	m_denyEarly = getenv("ZUTCP_KERNEL_TFO_DENY");
};

class ClientApp::Cxn : public ZmNHeap<ClientApp::Cxn>,
  public Zutcp::CliCxn<ClientApp, ClientApp::Cxn> {
friend inline ZuStringT<"Zutcp.KernelInterop.Client"> ZmNHeapID(Cxn *);
  using Base = Zutcp::CliCxn<ClientApp, Cxn>;
public:
  using ZmNHeap<Cxn>::operator new;
  using ZmNHeap<Cxn>::operator delete;
  explicit Cxn(ClientApp *app, bool reset, unsigned id) :
    Base{app}, m_reset{reset}, m_id{id} { }
  void connected(Zutcp::Connected) {
    std::cerr << "backend=AF_XDP role=client connected=1\n";
    std::cerr << std::flush;
    if (app()->tfo()) std::cerr << "tfo native_client connected=" << m_id << "\n";
    if (m_reset) { startRequest(); app()->result().connected(); }
    else if (getenv("ZUTCP_KERNEL_START_GATE")) app()->result().connected();
    else startRequest();
  }
  void startRequest() {
    auto request = app()->request();
    if (m_reset) {
      if (!send(ZuCSpan{request.data(), PrefixBytes}, false)) complete(false);
      return;
    }
    if (app()->tfo()) {
      if (!queueEarly()) return;
      if (!send(ZuCSpan{request.data() + PrefixBytes, request.length() - PrefixBytes}, true))
	complete(false);
      return;
    }
    unsigned chunk = app()->windowChunk();
    if (!chunk) { if (!send(request, true)) complete(false); return; }
    {
      auto tx = writer();
      std::cerr << "window writer max=" << tx.maxSize() << " head=" << tx.headRoom() << " tail="
	<< tx.tailRoom() << " capacity=" << tx.maxSize() - tx.headRoom() - tx.tailRoom()
	<< " chunk=" << chunk << "\n";
    }
    for (unsigned offset = 0; offset < request.length();) {
      unsigned length = request.length() - offset;
      if (length > chunk) length = chunk;
      if (!send(ZuCSpan{request.data() + offset, length},
	  offset + length == request.length())) { complete(false); return; }
      offset += length;
    }
  }
  unsigned id() const { return m_id; }
  bool queueEarly() {
    if (m_earlyQueued) return true;
		m_earlyQueued = true;
    std::cerr << "tfo native_client queued=" <<	m_id << "\n";
    if (send(ZuCSpan{app()->request().data(), PrefixBytes}, false)) return true;
    complete(false);
    return false;
  }
  void resetRequest() {
    if (!disconnect(ResetError)) complete(false);
  }
  int process(Zutcp::Reader &reader) {
    auto length = reader.length();
    if (length) {
      auto n = reader.each(length, [](auto part) -> int64_t {
	std::cout << ZuCSpan{part.data(), part.length()};
	return std::cout ? int64_t(part.length()) : 0;
      });
      if (n != int64_t(length)) { complete(false); return -1; }
      reader.clean();
      if (!retireRx(length)) { complete(false); return -1; }
    }
    if (finReceived() && rxComplete()) {
      std::cout << std::flush;
      complete(bool(std::cout));
    }
    return length ? 1 : 0;
  }
  void connectFailed(bool) { complete(false); }
  void disconnected(bool peer) {
    if (m_reset) {
      std::cerr << "reset native_client peer=" << unsigned(peer) << " local_error="
	<< closeError() << "\n";
      complete(!peer && closeError() == ResetError);
    } else if (!m_completed) complete(false);
  }
private:
  void complete(bool ok) {
    if (m_completed) return;
		m_completed = true;
    if (!ok) {
      app()->result().fail("native client did not complete the exact EOF exchange");
      app()->result().connected();
    }
    app()->result().done();
  }
  bool		m_completed = false; // Rx
  const bool	m_reset;
  const unsigned	m_id;
  bool		m_earlyQueued = false; // Rx
};

bool ClientApp::earlyData(Cxn *link)
{
  return m_tfo && link->queueEarly() && allowEarlyStream(link, 0, true);
}

bool ClientApp::allowEarlyStream(Cxn *link, uint64_t, bool)
{
  return m_tfo && (!m_denyEarly || link->id() == 1);
}

void ClientApp::earlyDataAccepted(Cxn *link)
{
  if (m_tfo) std::cerr << "tfo native_client accepted=" << link->id() << "\n";
}

void ClientApp::earlyDataRejected(Cxn *link)
{
  if (m_tfo) std::cerr << "tfo native_client rejected=" << link->id() << "\n";
}

class ServerCxn;
class ServerApp : public Zutcp::Server<ServerApp, ServerCxn> {
  using Base = Zutcp::Server<ServerApp, ServerCxn>;
public:
  ServerApp(Result &result, ZiIP address, unsigned port, unsigned count) :
    m_result{result}, m_address{address}, m_port{port}, m_target{count} { }
  ZiIP localIP() const { return m_address; }
  unsigned localPort() const { return m_port; }
  ZmRef<ServerCxn> accepted(const Zutcp::InitialInfo &);
  void disconnected(ServerCxn *, bool);
  bool tfo() const { return m_tfo; }
  uint32_t maxEarlyData(ServerCxn *) { return m_tfo ? 400 : 0; }
  bool allowEarlyStream(ServerCxn *, uint64_t, bool);
  void holdReader(ServerCxn *);
  void releaseReader();
  void listening() {
    auto stack = nativeStack();
    std::cerr << "backend=AF_XDP role=server ready=" << unsigned(stack && stack->ready()) << "\n";
    std::cerr << std::flush;
  }
  void listenFailed(bool) { m_result.fail("native listen failed"); m_result.done(); }
  void completed(bool ok) {
    if (!ok) m_result.fail("native server did not complete the exact EOF exchange");
    if (++m_completed >= m_target || !ok) m_result.done();
  }
private:
  using Flag = ZmAtomic<unsigned>;
  Result		&m_result;
  ZiIP		m_address;
  unsigned	m_port = 0, m_target = 1, m_accepted = 0; // Rx accepted count
  Flag		m_completed = 0;
  ZmRef<ServerCxn>	m_held; // Rx, fixture hold through the existing Reader only
  const bool	m_tfo = getenv("ZUTCP_KERNEL_TFO");
  const bool	m_denyEarly = getenv("ZUTCP_KERNEL_TFO_DENY");
};

class ServerCxn : public ZmNHeap<ServerCxn>, public Zutcp::SrvCxn<ServerApp, ServerCxn> {
friend inline ZuStringT<"Zutcp.KernelInterop.Server"> ZmNHeapID(ServerCxn *);
  using Base = Zutcp::SrvCxn<ServerApp, ServerCxn>;
public:
  using ZmNHeap<ServerCxn>::operator new;
  using ZmNHeap<ServerCxn>::operator delete;
  explicit ServerCxn(ServerApp *app, bool reset, unsigned id) :
    Base{app}, m_reset{reset}, m_id{id} { }
  void connected(Zutcp::Connected) {
		m_connected = true;
    if (app()->tfo()) std::cerr << "tfo native_server connected=" << m_id << "\n";
    std::cerr << "backend=AF_XDP role=server connected=1\n";
    std::cerr << std::flush;
    if (m_reset) {
      auto stack = app()->nativeStack();
      auto record = Base::closedSnapshotRx(Base::closedKeyRx());
      if (!record.timestamps || !Base::hasCxnSlotRx()) { complete(false); return; }
		m_fixtureHistory = stack->rememberClosed(record);
      Zutcp::ClosedTuple indexed;
      bool seeded = m_fixtureHistory.id && stack->closedTuple(record.key, indexed) &&
	indexed.id ==	m_fixtureHistory.id;
      std::cerr << "reset fixture_history_seed id="
	<< m_fixtureHistory.id << " timestamps="
	<< unsigned(record.timestamps) << " live_slot=" << unsigned(Base::hasCxnSlotRx())
	<< "\n";
      if (!seeded) complete(false);
    }
  }
  unsigned id() const { return m_id; }
  int process(Zutcp::Reader &reader) {
    if (m_replied) return 0;
    auto length = reader.length();
    if (length && app()->tfo())
      std::cerr << "tfo native_server process=" << m_id << " bytes=" << unsigned(length)
	<< " connected=" << unsigned(m_connected) << "\n";
    if (length && m_holdReader) { app()->holdReader(this); return 0; }
    if (m_request.length() + length > MaxRequest) { complete(false); return -1; }
    if (length) {
      auto n = reader.each(length, [this](auto part) -> int64_t {
	m_request << ZuCSpan{part.data(), part.length()};
	return part.length();
      });
      if (n != int64_t(length)) { complete(false); return -1; }
      reader.clean();
      if (!retireRx(length)) { complete(false); return -1; }
    }
    if (finReceived() && rxComplete()) {
		m_replied = true;
      if (!send("echo: ", false) || !send(ZuCSpan{m_request.data(), m_request.length()}, true)) {
	complete(false);
	return -1;
      }
    }
    return length ? 1 : 0;
  }
  void releaseReader() {
		m_holdReader = false;
    std::cerr << "window reader=released\n";
    std::cerr << std::flush;
    Base::resumeApplicationRx(Base::dataGen());
  }
  void txComplete(bool ok) { if (!m_reset) complete(ok); }
  void disconnected(bool peer) {
    if (m_reset) {
      std::cerr << "reset native_server peer=" << unsigned(peer) << " bytes="
	<< unsigned(m_request.length()) << "\n";
      Zutcp::ClosedTuple current;
      bool found = app()->nativeStack()->closedTuple(m_fixtureHistory.key, current);
      bool slot = Base::hasCxnSlotRx();
      std::cerr << "reset fixture_history_retired seed="
	<< m_fixtureHistory.id << " current="
	<< current.id << " fin=" << unsigned(current.fin)
	<< " slot=" << unsigned(slot) << "\n";
      bool retired = found && m_fixtureHistory.id && current.id != m_fixtureHistory.id &&
	!current.fin && current.timestamps && !slot;
      complete(peer && m_request.length() == PrefixBytes && !m_replied && retired);
    } else complete(false);
  }
private:
  void complete(bool ok) {
    if (m_completed.xch(1)) return;
    app()->completed(ok);
  }
  using Closed = Zutcp::ClosedTuple;
  using Flag = ZmAtomic<unsigned>;
  PeerBytes	m_request; // application request, retired from transport as consumed
  const bool	m_reset;
  const unsigned	m_id;
  bool		m_connected = false; // Rx
  Closed		m_fixtureHistory; // Rx, explicitly seeded while this owner is live
  bool		m_replied = false; // Rx
  bool		m_holdReader = getenv("ZUTCP_KERNEL_READ_GATE"); // Rx
  Flag		m_completed = 0; // Rx/Tx final publication
};

void ServerApp::disconnected(ServerCxn *link, bool peer)
{
  ZmRef<ServerCxn> keep{link};
  Base::disconnected(link, peer);
  keep->disconnected(peer);
}

void ServerApp::holdReader(ServerCxn *link)
{
  if (m_held) return;
  m_held = link;
  std::cerr << "window reader=held\n";
  std::cerr << std::flush;
}

void ServerApp::releaseReader()
{
  rxRun([this] {
    auto link = ZuMv(m_held);
    if (!link) {
		m_result.fail("fixture released a Reader that was never held");
		m_result.done();
    }
    else link->releaseReader();
  });
}

ZmRef<ServerCxn> ServerApp::accepted(const Zutcp::InitialInfo &)
{
  unsigned id = ++m_accepted;
  return new ServerCxn{this, id == 1 && getenv("ZUTCP_KERNEL_EXPECT_RESET"), id};
}

bool ServerApp::allowEarlyStream(ServerCxn *link, uint64_t, bool)
{
  return m_tfo && (!m_denyEarly || link->id() == 1);
}

static bool readFile(const char *name, PeerBytes &bytes)
{
  ZiFile file{name, ZiFile::ReadOnly | ZiFile::GC};
  if (!file) {
    ZiLOG(Error, "zutcppair", ([error = file.error()](auto &out) {
      out << "request file: " << error;
    }));
    return false;
  }
  char buf[ReadSize]; // Cold bounded utility input in4K chunks.
  while (true) {
    int n = file.read(buf, sizeof(buf), false);
    if (n == Zi::EndOfFile) return true;
    if (n < 0 || bytes.length() + unsigned(n) > MaxRequest) return false;
    bytes << ZuCSpan{buf, unsigned(n)};
  }
}

template <typename Params>
static void options(Params &params)
{
  if (const char *value = getenv("ZUTCP_PEER_OPTIONS")) {
    unsigned mask = unsigned(ZuBox<unsigned>(value));
    params.sackPermitted(mask & 1).windowScale(mask & 2).timestamps(mask & 4).ecn(mask & 8);
  }
}

static void usage()
{
  std::cerr << "Usage: zutcppair server INTERFACE ADDRESS PORT COUNT\n"
    "       zutcppair client INTERFACE ADDRESS PORT REQUEST_FILE [COUNT]\n";
  exit(2);
}

int main(int argc, char **argv)
{
  if (argc != 6 && argc != 7) usage();
  bool server = !strcmp(argv[1], "server");
  if (!server && strcmp(argv[1], "client")) usage();
  if (server && argc != 6) usage();
  unsigned interface = if_nametoindex(argv[2]);
  ZiIP address{argv[3]};
  unsigned port = unsigned(ZuBox<unsigned>(argv[4]));
  unsigned count = server ? unsigned(ZuBox<unsigned>(argv[5])) :
    (argc == 7 ? unsigned(ZuBox<unsigned>(argv[6])) : 1);
  if (!interface || !port || port > 65535 || !count) usage();
  ZiLog::init("zutcppair");
  ZiLog::level(0);
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();
  ZuGuard logStop{[] { ZiLog::stop(); }};
  PeerBytes request;
  if (!server && !readFile(argv[5], request)) return 1;
  if (!server && (getenv("ZUTCP_KERNEL_TFO") || getenv("ZUTCP_KERNEL_RESET_GATE")) &&
      request.length() < PrefixBytes) {
    ZiLOG(Error, "zutcppair", ([](auto &out) {
      out << "fixture request is shorter than its bounded prefix";
    }));
    return 1;
  }
  Result result;
  ZiMultiplex mx{ZiMxParams().scheduler([](auto &s) {
    s.nThreads(3).thread(1, [](auto &t) { t.isolated(1); })
      .thread(2, [](auto &t) { t.isolated(1); });
  }).rxThread(1).txThread(2)};
  auto stack = new Zutcp::Stack{&mx, {.interface = interface}};
  mx.plugin(stack);
  if (server) {
    ServerApp app{result, address, port, count};
    auto params = Zutcp::ServerParams{&mx};
    options(params);
    if (getenv("ZUTCP_KERNEL_READ_GATE")) params.maxData(4096);
    bool initialized = app.init(params);
    bool listening = initialized && mx.start() && app.start();
    if (!listening) result.fail("native server start");
    else {
      if (getenv("ZUTCP_KERNEL_READ_GATE")) {
	if (!waitGate("ZUTCP_KERNEL_READ_GATE")) result.fail("native Reader release gate");
	else app.releaseReader();
      }
      if (!result.failed()) result.wait();
    }
    if (listening) app.stop();
    if (initialized) app.final();
    mx.stop();
  } else {
    unsigned windowChunk = getenv("ZUTCP_KERNEL_WINDOW_CLIENT") ?
      (Zutcp::tcpPayload(stack->mtu(), address.type()) - 12)>>1 : 0;
    ClientApp app{result, request, windowChunk};
    auto params = Zutcp::ClientParams{&mx};
    options(params);
    if (getenv("ZUTCP_KERNEL_WINDOW_CLIENT"))
      params.maxPayload(Zutcp::tcpPayload(stack->mtu(), address.type()))
	.sackPermitted(false).timestamps(true);
    bool initialized = app.init(params);
    if (!initialized || !mx.start()) result.fail("native client start");
    else {
      std::cerr << "backend=AF_XDP role=client ready=" << unsigned(stack->ready()) << "\n";
      for (unsigned i = 0; i < count && !result.failed(); ++i) {
	bool reset = !i && getenv("ZUTCP_KERNEL_RESET_GATE");
	ZmRef<ClientApp::Cxn> link = new ClientApp::Cxn{&app, reset, i + 1};
	link->connect(Zutcp::Host{argv[3]}, port);
	if (reset) {
	  result.waitConnected();
	  if (!result.failed()) {
	    if (!waitGate("ZUTCP_KERNEL_RESET_GATE")) result.fail("native reset gate");
	    else app.rxRun([link] { link->resetRequest(); });
	  }
	}
	if (getenv("ZUTCP_KERNEL_START_GATE")) {
	  result.waitConnected();
	  if (!result.failed()) {
	    if (!waitGate("ZUTCP_KERNEL_START_GATE")) result.fail("native request start gate");
	    else app.rxRun([link] { link->startRequest(); });
	  }
	}
	if (!result.failed()) result.wait();
	ZmBlock<>{}([&link](auto wake) {
	  link->disconnect([wake = ZuMv(wake)]() mutable { wake(); });
	});
	std::cerr << "native disconnect_complete=" << i + 1 << "\n";
      }
    }
    if (initialized) app.final();
    mx.stop();
  }
  auto admission = stack->cxnAdmission();
  auto tx = stack->txAdmission();
  bool drained = !stack->ready() && !admission.total && !admission.pending &&
    !tx.outstanding && tx.controlFree == tx.controlCount;
  std::cerr << "backend=AF_XDP stopped=" << unsigned(!stack->ready()) << " cxns="
    << admission.total << " pending=" << admission.pending << " tx=" << tx.outstanding
    << " control=" << tx.controlFree << "/" << tx.controlCount << "\n";
  if (!drained) result.fail("native owner/provider drain incomplete");
  return result.failed() ? 1 : 0;
}
