//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <unistd.h>

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#ifdef linux
#include <sys/syscall.h>
#endif

#include <iostream>

#include <zlib/ZuBox.hh>
#include <zlib/ZuTestUtil.hh>
#include <zlib/ZtString.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmBlock.hh>
#include <zlib/ZmFn.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZfJSON.hh>
#include <zlib/Zquic.hh>

#include "ZiTestResidue.hh"

#include "ZquicTestPorts.hh"

using namespace ZuTestUtil;

#define ZQUIC_CHECK_RT(x, ...) ZuCheckRT(x, log_(__VA_ARGS__))


#ifdef linux
// Fixture-only control for dual connected UDP sockets retaining one source
// tuple. Ordinary Zi UDP intentionally does not enable address reuse on bind.
// Publish exact immutable metadata on mx Rx before posting the candidate open;
// clear it only after both endpoint owner drains have completed.
struct ReuseTuple {
  in_addr ipv4{};
  in6_addr ipv6{};
  int originalFD = -1;
  ZmAtomic<unsigned> key = 0; // (native family << 16) | host-order port
  ZmAtomic<unsigned> binds = 0;
};
static ReuseTuple reuseTuple;

extern "C" __attribute__((visibility("default")))
int bind(int fd, const struct sockaddr *addr, socklen_t length) noexcept
{
  unsigned key = reuseTuple.key;
  bool matches = false;
  if (key && addr && unsigned(addr->sa_family) == (key >> 16)) {
    if (addr->sa_family == AF_INET && length >= sizeof(sockaddr_in)) {
      auto &a = *reinterpret_cast<const sockaddr_in *>(addr);
      matches = ntohs(a.sin_port) == (key & 0xffff) &&
	a.sin_addr.s_addr == reuseTuple.ipv4.s_addr;
    } else if (addr->sa_family == AF_INET6 && length >= sizeof(sockaddr_in6)) {
      auto &a = *reinterpret_cast<const sockaddr_in6 *>(addr);
      matches = ntohs(a.sin6_port) == (key & 0xffff) &&
	!memcmp(&a.sin6_addr, &reuseTuple.ipv6, sizeof(in6_addr));
    }
  }
  if (matches) {
    int yes = 1;
    if (setsockopt(reuseTuple.originalFD, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) ||
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes))) return -1;
    ++reuseTuple.binds;
  }
  return syscall(SYS_bind, fd, addr, length);
}
#endif

enum { TestSrvLinkCap = 20 };
enum { TestSrvCxnCount = 17 };

// Durable fixture-owned notifications: a timeout never leaves a callback
// referring to a stack waiter. Pending producers are drained with the fixture.
struct TestEvent {
  ZmSemaphore sem;

  void post() { sem.post(); }
  template <typename L>
  bool wait(L ready, ZuTime deadline) {
    while (!ready())
      if (Zm::now() >= deadline || sem.timedwait(deadline)) return ready();
    return true;
  }
};

template <typename Link>
static Zquic::LinkDiag diag(const Link &link)
{
  return ZmBlock<Zquic::LinkDiag>{}(
    [&link](auto wake) { link->diag(ZuMv(wake)); });
}

template <typename Link>
static Zquic::EndpointDiag cxnDiag(const Link &link)
{
  return ZmBlock<Zquic::EndpointDiag>{}(
    [&link](auto wake) { link->cxnDiag(ZuMv(wake)); });
}

template <typename Server>
static Zquic::EndpointDiag endpointDiag(Server &server)
{
  return ZmBlock<Zquic::EndpointDiag>{}(
    [&server](auto wake) { server.endpointDiag(ZuMv(wake)); });
}

template <typename Server>
static Zquic::AddrValidationDiag addrValidationDiag(Server &server)
{
  return ZmBlock<Zquic::AddrValidationDiag>{}(
    [&server](auto wake) { server.addrValidationDiag(ZuMv(wake)); });
}

template <typename Link>
static auto openStream(
  const Link &link,
  Zquic::StreamType::T type = Zquic::StreamType::Duplex)
{
  using StreamRef = decltype(link->stream(type));
  StreamRef stream;
  ZmBlock<>{}([&link, type, &stream](auto wake) {
    link->app()->rxRun([
      link, type, &stream, wake = ZuMv(wake)
    ]() mutable {
      stream = link->stream(type);
      wake();
    });
  });
  return stream;
}

template <typename Link>
static auto findStream(const Link &link, int64_t id)
{
  using StreamRef = decltype(link->findStream(id));
  StreamRef stream;
  ZmBlock<>{}([&link, id, &stream](auto wake) {
    link->app()->rxRun([link, id, &stream, wake = ZuMv(wake)]() mutable {
      stream = link->findStream(id);
      wake();
    });
  });
  return stream;
}

struct TestClient : public Zquic::Client<TestClient> {
  struct Link;
  struct Stream;
  using Base = Zquic::Client<TestClient>;
  using Base::rxInvoke;

  enum {
    TokenNone,
    TokenMalformed,
    TokenAuth,
    TokenExpired,
    TokenAddress,
    TokenPolicy,
    TokenKind
  };

  TestEvent rxEvent;
  ZmAtomic<unsigned> rxCallbacks = 0;

  bool resumeEarly = false;
  Zquic::TokenBytes ticket;
  Zquic::TransportParams ticketParams;
  ZmSemaphore ticketReady;
  ZmAtomic<unsigned> holdOneRTT = 0; // while checking the original 0-RTT STREAM ACK
  bool earlyData(Link *, ZuBSpan &session, const Zquic::TransportParams *&params,
      ZuBSpan &application) {
    if (!ticket) return false;
    session = {ticket.data(), ticket.length()};
    params = &ticketParams;
    application = {};
    return true;
  }
  void saveEarlyData(Link *, ZuBSpan session, const Zquic::TransportParams &params) {
    if (!resumeEarly || !session || ticket) return;
    ticket.length(session.length());
    memcpy(ticket.data(), session.data(), session.length());
    ticketParams = params;
    ticketReady.post();
  }
  bool allowEarlyStream(Link *, uint64_t, bool) { return resumeEarly; }
  void earlyDataReady(Link *);
  bool holdMigCID = false; // immutable fixture policy before init
  bool keepMigTuple = false; // immutable fixture policy before init
  bool dropMigChallenge = false; // immutable after init
  unsigned pendingMigDrop = 0; // Tx thread
  ZmAtomic<unsigned> migChallengeDrops = 0;
  Zquic::TokenSecret tokenSecret;
  uint64_t tokenNow = 0;
  ZmAtomic<unsigned> tokenRewrite = TokenNone;
  ZmAtomic<unsigned> tokenRewritesApplied = 0;
  ZmAtomic<unsigned> tokenRewriteLengthMismatch = 0;

  // Endpoint readiness and packet delivery enter through the object-bearing
  // Rx dispatcher. Notify only after that work completes. Predicate queries
  // use rxRun, so inspecting state cannot generate its own wakeup.
  template <typename O, typename L>
  void rxInvoke(O *owner, L l) {
    Base::rxInvoke(owner, [this, l = ZuMv(l)]() mutable {
      auto result = l();
      ++rxCallbacks;
      rxEvent.post();
      return result;
    });
  }

  bool sendPkt(const ZmRef<ZiIOBuf> &buf) {
    if (holdOneRTT && buf && buf->length && !Zquic::Pkt::isLong(buf->cspan())) return false;
    if (pendingMigDrop && buf && buf->length &&
	!Zquic::Pkt::isLong(buf->cspan())) {
      --pendingMigDrop;
      ++migChallengeDrops;
      return false;
    }
    unsigned rewrite = tokenRewrite;
    if (!rewrite || !buf || !buf->length) return true;
    auto packet = buf->cspan();
    if (!Zquic::Pkt::isLong(packet)) return true;
    Zquic::LongHdr h;
    if (Zquic::Pkt::parseLong(packet, h) < 0 ||
	h.type != Zquic::PktType::Initial ||
	!h.tokenLength)
      return true;

    ZiSockAddr addr{
      rewrite == TokenAddress ? ZiIP("127.0.0.2") : ZiIP("127.0.0.1"),
      12345
    };
    Zquic::TokenKind::T kind = rewrite == TokenKind ?
      Zquic::TokenKind::T(3) : Zquic::TokenKind::NewToken;
    Zquic::TokenBytes token;
    if (!Zquic::AddressToken::encode(
	  token, kind, tokenSecret, addr, h.dcid, {},
	  rewrite == TokenExpired ? tokenNow - 120 : tokenNow, false))
      return true;
    if (rewrite == TokenMalformed)
      token[0] = 'X';
    else if (rewrite == TokenAuth)
      token[token.length() - 1] ^= 1;
    if (token.length() != h.tokenLength) {
      ++tokenRewriteLengthMismatch;
      return true;
    }
    memcpy(buf->data_() + h.tokenOffset, token.data(), token.length());
    tokenRewrite = TokenNone;
    ++tokenRewritesApplied;
    return true;
  }
};
struct TestServerLink;
struct RouteCounts {
  unsigned packets = 0, probes = 0, inserts = 0, walks = 0;
};
template <> struct Zquic_::CxnRouteTrace<TestServerLink> {
  static inline thread_local RouteCounts counts;
  static void shortPacket() { ++counts.packets; }
  static void probe() { ++counts.probes; }
  static void insert() { ++counts.inserts; }
  static void bindLocals() { ++counts.walks; }
};
struct TestServerStream;
struct TestServer :
    public Zquic::Server<TestServer, TestServerLink> {
  using Link = TestServerLink;
  using Stream = TestServerStream;
  using Base = Zquic::Server<TestServer, TestServerLink>;

  Zquic::ServerAddrs addrs;
  Zquic::ServerAddrs serverAddrs() const { return addrs; }
  bool repeatInitial = false; // copy and redispatch the first accepted Initial on Rx
  Link *bootstrapCollision = nullptr; // Rx-owned sibling retaining the conflicting CID
  bool issuanceCollisions = false; // Rx-owned after start
  bool resetFixture = false; // immutable dedicated socket fixture policy
  bool resetInstalled = false; // Rx-owned test record installation
  Zquic::CxnID resetCID{"resetcid"};
  Zquic::ResetToken resetToken{"0123456789abcdef"};
  bool resumeEarly = false;
  uint32_t maxEarlyData(Link *) { return resumeEarly ? 1U<<20 : 0; }
  bool allowEarlyStream(Link *, uint64_t, bool) { return resumeEarly; }
  bool retryMode = false;
  bool retryAddrValidate() const { return retryMode || Base::retryAddrValidate(); }

  Zi::IOBufAllocator *tlsOutputOverride = nullptr;
  Zi::IOBufAllocator *tlsOutputAllocator() const {
    return tlsOutputOverride ? tlsOutputOverride : Base::tlsOutputAllocator();
  }

  ZmAtomic<unsigned> holdHandshakeDone = 0;
  ZmAtomic<unsigned> heldHandshakeDone = 0;
  TestEvent handshakeDoneEvent;
  TestEvent dropEvent;

  bool stopOnDisconnect = false; // Rx thread - one-shot stop race
  unsigned stopOnMigration = 0; // fixture policy, changed only while stopped
  ZmAtomic<unsigned> disconnectStops = 0;
  ZmRef<Link> link_;
  ZmRef<Link> links_[TestSrvLinkCap];
  TestEvent acceptedEvent;
  ZmAtomic<unsigned> acceptedCount = 0;
  ZmAtomic<unsigned> retryPkts = 0;
  ZmAtomic<unsigned> initialPkts = 0;
  ZmAtomic<unsigned> handshakePkts = 0;
  ZmAtomic<unsigned> dropNextInitial = 0;
  ZmAtomic<unsigned> droppedInitial = 0;
  ZmAtomic<unsigned> dropNextHandshake = 0;
  ZmAtomic<unsigned> droppedHandshake = 0;
  ZmAtomic<unsigned> droppedCoalescedFlight = 0;
  bool initialProbeAfterDrop = false; // Tx thread
  ZmAtomic<unsigned> firstHandshakeAfterInitialProbe = 0;
  ZmAtomic<unsigned> dropNextShort = 0;
  ZmAtomic<unsigned> droppedShort = 0;
  ZmAtomic<unsigned> shortPkts = 0;
  ZmAtomic<int64_t> dropStreamID = -1;
  ZmAtomic<unsigned> droppedStream = 0;
  ZmAtomic<unsigned> streamFrames = 0;

  void datagramDropped() { dropEvent.post(); }

  ZmRef<Link> link(unsigned i = unsigned(-1));
  ZmRef<Link> accepted(const Zquic::InitialInfo &);
  void clearLinks();
  ZmRef<Link> waitLink(unsigned, ZuTime);
  void disconnected(Link *, bool);
  ZiIP localIP() const { return ZiIP("127.0.0.1"); }
  unsigned localPort() const { return ZquicTestPort::Runtime; }
  bool sendPkt(const ZmRef<ZiIOBuf> &buf) {
    if (!buf || !buf->length) return true;
    auto packet = buf->cspan();
    if (Zquic::Pkt::isLong(packet)) {
      Zquic::LongHdr h;
      if (Zquic::Pkt::parseLong(packet, h) < 0) return true;
      if (h.type == Zquic::PktType::Retry) {
	++retryPkts;
	return true;
      }
      if (h.type == Zquic::PktType::Initial) ++initialPkts;
      if (h.type == Zquic::PktType::Handshake) ++handshakePkts;
      if (h.type == Zquic::PktType::Initial && dropNextInitial) {
	unsigned end = h.pnOffset + unsigned(h.length);
	Zquic::LongHdr next;
	if (end < packet.length() && Zquic::Pkt::parseLong(
	      {packet.data() + end, packet.length() - end}, next) >= 0 &&
	    next.type == Zquic::PktType::Handshake)
	  droppedCoalescedFlight = 1;
	--dropNextInitial;
	++droppedInitial;
	dropEvent.post();
	return false;
      }
      // ACK-only Initial packets can precede the padded PTO probe.
      if (h.type == Zquic::PktType::Initial && droppedCoalescedFlight &&
	  packet.length() >= Zquic::MinUDPPayload)
	initialProbeAfterDrop = true;
      if (h.type == Zquic::PktType::Handshake && initialProbeAfterDrop &&
	  !firstHandshakeAfterInitialProbe)
	firstHandshakeAfterInitialProbe = unsigned(h.length);
      if (h.type == Zquic::PktType::Handshake && dropNextHandshake) {
	--dropNextHandshake;
	++droppedHandshake;
	dropEvent.post();
	return false;
      }
      return true;
    }
    ++shortPkts;
    if (!dropNextShort) return true;
    dropNextShort = 0;
    ++droppedShort;
    return false;
  }
  bool sendFrame(const Zquic::SentFrameRef &ref) {
    if (holdHandshakeDone &&
	ref.kind == Zquic::SentFrameKind::Control &&
	ref.controlType() == Zquic::FrameType::HandshakeDone) {
      ++heldHandshakeDone;
      handshakeDoneEvent.post();
      return false;
    }
    if (ref.kind != Zquic::SentFrameKind::Stream) return true;
    ++streamFrames;
    if (dropStreamID < 0 || ref.streamID != uint64_t(dropStreamID))
      return true;
    dropStreamID = -1;
    ++droppedStream;
    return false;
  }
};

struct TestClient::Stream :
  public Zquic::CliStream<TestClient::Link, TestClient::Stream> {
  using Base =
    Zquic::CliStream<TestClient::Link, TestClient::Stream>;
  using Base::Base;

  TestEvent txEvent;
  unsigned processed = 0;

  int process(Zquic::RxStream &);
  void txDrained_() { txEvent.post(); }
  void txComplete(bool) { txEvent.post(); }
};

struct TestClient::Link :
  public ZmNHeap<TestClient::Link>,
  public Zquic::CliLink<TestClient, TestClient::Link,
    TestClient::Stream> {
friend inline ZuStringT<"Zquic.Runtime.CliLink"> ZmNHeapID(Link *);
  using ZmNHeap<Link>::operator new;
  using ZmNHeap<Link>::operator delete;

  using Base = Zquic::CliLink<TestClient, TestClient::Link,
    TestClient::Stream>;
  using Base::Base;
  using CryptoSnapshot = typename Base::TxCryptoSnapshot;

  using Base::ownerLayout;

  CryptoSnapshot handshakeKeys;

  Zquic::MigrationResult promotion; // Tx thread
  Zquic::MigrationResult migrationFailure;
  TestEvent migrationEvent;
  TestEvent connectedEvent;
  TestEvent disconnectedEvent;
  TestEvent streamEvent;
  TestEvent closeEvent;
  unsigned connectTarget = 0;
  unsigned failureBase = 0;
  unsigned disconnectTarget = 0;
  unsigned closeTarget = 0;
  ZmAtomic<unsigned> closeCount = 0;
  ZmAtomic<unsigned> processedStreams = 0;
  ZmAtomic<unsigned> connectedCount = 0;
  ZmAtomic<unsigned> disconnectedCount = 0;
  ZmAtomic<unsigned> earlyDisconnects = 0;
  ZmAtomic<unsigned> connectFailures = 0;
  ZmAtomic<unsigned> streamedCount = 0;

  Link(TestClient *app) : Base{app} { }

  bool receivedNewToken() {
    return ZmBlock<bool>{}([this](auto wake) {
      app()->rxRun([this, wake = ZuMv(wake)]() mutable {
        wake(Base::cachedNewToken());
      });
    });
  }

  void connected(Zquic::Connected) {
    Base::snapshotTxCrypto(handshakeKeys);
    ++connectedCount;
    connectedEvent.post();
  }
  bool handshakeRxKeysLive() {
    auto &crypto = Base::crypto_();
    return crypto.rxSecretInstalled(Zquic::PktNumSpace::Handshake) ||
      crypto.txSecretInstalled(Zquic::PktNumSpace::Handshake) ||
      crypto.rxProtState(Zquic::PktNumSpace::Handshake).valid();
  }
  bool handshakeTxKeysLive() const {
    return Base::txSecretInstalled_(Zquic::PktNumSpace::Handshake);
  }
  bool staleKeysStayRetired() {
    return Base::installTxCrypto(handshakeKeys) &&
      !handshakeTxKeysLive() &&
      Base::txSecretInstalled_(Zquic::PktNumSpace::AppData);
  }
  void disconnected(bool) {
    if (cxn()) ++earlyDisconnects;
    ++disconnectedCount;
    disconnectedEvent.post();
    connectedEvent.post();
    streamEvent.post();
  }
  void connectFailed(bool) {
    ++connectFailures;
    connectedEvent.post();
  }
  void streamed(ZmRef<Stream>) {
    ++streamedCount;
    streamEvent.post();
  }
  void processedStream() {
    ++processedStreams;
    streamEvent.post();
  }
  template <typename ...Args>
  void connect(Args &&...args) {
    // Each reconnect follows the preceding disconnect completion. Baselines
    // distinguish its callbacks from durable notifications for older attempts.
    connectTarget = connectedCount + 1;
    failureBase = connectFailures;
    disconnectTarget = disconnectedCount + 1;
    if (!waitClosed(Zm::now(10))) {
      ++connectFailures;
      connectedEvent.post();
      return;
    }
    Base::connect(ZuFwd<Args>(args)...);
  }
  bool waitConnected(ZuTime deadline) {
    return connectedEvent.wait([this]() {
      return connectedCount >= connectTarget ||
        connectFailures > failureBase || disconnectedCount >= disconnectTarget;
    }, deadline) && connectedCount >= connectTarget &&
      connectFailures == failureBase && disconnectedCount < disconnectTarget &&
      established();
  }
  bool waitStreams(unsigned n, ZuTime deadline) {
    return streamEvent.wait([this, n]() {
      return (streamedCount >= n && processedStreams >= n) ||
        disconnectedCount >= disconnectTarget;
    }, deadline) && streamedCount >= n && processedStreams >= n;
  }
  bool waitDisconnected(ZuTime deadline) {
    return disconnectedEvent.wait([this]() {
      return disconnectedCount >= disconnectTarget;
    }, deadline);
  }
  void closeAsync() {
    ++closeTarget;
    Base::disconnect([link = ZmRef<Link>{this}]() {
      ++link->closeCount;
      link->closeEvent.post();
    });
  }
  bool waitClosed(ZuTime deadline) {
    return closeEvent.wait([this]() { return closeCount >= closeTarget; }, deadline);
  }
  bool close() {
    closeAsync();
    return waitClosed(Zm::now(10));
  }
  void migrationStarted(const Zquic::MigrationResult &result) {
    if (app()->dropMigChallenge &&
	result.state == Zquic::MigrationState::Validating &&
	result.reason == Zquic::MigrationReason::Active)
      app()->pendingMigDrop = 1;
  }
  void migrationPromoted(const Zquic::MigrationResult &result) {
    promotion = result;
    migrationEvent.post();
  }
  void migrationFailed(const Zquic::MigrationResult &result) {
    migrationFailure = result;
    migrationEvent.post();
  }
  bool migCIDHeld = false; // Tx-owned fixture checkpoint
  void migrationCIDReady() {
    if (app()->holdMigCID) {
      migCIDHeld = true;
      migrationEvent.post();
      return;
    }
    startMigrationCID();
  }
  void releaseHeldMigration() {
    if (!migCIDHeld) return;
    migCIDHeld = false;
    startMigrationCID();
  }
  void startMigrationCID() {
    // Keep both source address and port fixed while changing only the server
    // destination. The two connected UDP sockets have distinct remote tuples.
    if (app()->keepMigTuple) {
      auto local = Base::activePathInfo().local;
      Base::updateMigLocal(local);
#ifdef linux
      auto target = Base::Endpoint::pktAllocTarget();
      app()->mx()->rxRun([this, local, target] {
	if (Base::Endpoint::pktAllocTarget() == target && target.physical) {
	  using Physical = Zquic_::EndpointCxn<Base::Endpoint>;
	  auto cxn = static_cast<Physical *>(target.physical);
	  reuseTuple.originalFD = cxn->info().socket;
	  auto ip = local.ip();
	  if (ip.v6()) reuseTuple.ipv6 = ip.in6Addr();
	  else reuseTuple.ipv4 = ip.inAddr();
	  reuseTuple.key = (unsigned(ip.v6() ? AF_INET6 : AF_INET) << 16) | local.port();
	}
	app()->txRun([this] { Base::migrationCIDReady(); });
      });
      return;
#endif
    }
    Base::migrationCIDReady();
  }
  Zquic::PreferredAddress peerPreferred() const {
    return Base::crypto_().peerParams().preferredAddress;
  }
  bool receivedCID(const Zquic_::LinkCID &expected) {
    return ZmBlock<bool>{}([this, expected](auto wake) {
      app()->rxRun([this, expected, wake = ZuMv(wake)]() mutable {
	auto cid = Base::peerCID_(expected.sequence);
	wake(cid && cid->id == expected.id && cid->resetToken == expected.resetToken);
      });
    });
  }
  bool spareCID() {
    return ZmBlock<bool>{}([this](auto wake) {
      app()->rxRun([link = ZmRef<Link>{this}, wake = ZuMv(wake)]() mutable {
	Zquic::CxnID id;
	uint64_t sequence;
	wake(link->Base::selectPeerCIDRx(true, id, sequence));
      });
    });
  }
  void forceCloseTimeout() {
    app()->txInvoke(this, [link = ZmRef<Link>{this}]() mutable {
      link->Base::closeTimeout();
      return link.ptr();
    });
  }
};

struct TestServerStream :
  public Zquic::SrvStream<TestServerLink, TestServerStream> {
  using Base =
    Zquic::SrvStream<TestServerLink, TestServerStream>;
  using Base::Base;

  TestEvent txEvent;
  unsigned processed = 0;

  int process(Zquic::RxStream &);
  void txDrained_() { txEvent.post(); }
  void txComplete(bool) { txEvent.post(); }
};

struct TestServerLink :
  public ZmNHeap<TestServerLink>,
  public Zquic::SrvLink<TestServer, TestServerLink,
    TestServerStream> {
friend inline ZuStringT<"Zquic.Runtime.SrvLink"> ZmNHeapID(TestServerLink *);
  using ZmNHeap<TestServerLink>::operator new;
  using ZmNHeap<TestServerLink>::operator delete;

  using Base = Zquic::SrvLink<TestServer, TestServerLink,
    TestServerStream>;
  using Base::Base;
  using CryptoSnapshot = typename Base::TxCryptoSnapshot;

  using Base::ownerLayout;

  CryptoSnapshot handshakeKeys;

  Zquic::MigrationResult migrationStop; // Tx-owned lifecycle checkpoint
  ZmFn<void(bool)> heldMigrationCommit;
  unsigned migrationPromotions = 0; // Tx-owned callback count

  TestEvent peerCloseEvent;
  TestEvent migrationEvent;
  TestEvent connectedEvent;
  TestEvent disconnectedEvent;
  TestEvent streamEvent;
  ZmAtomic<unsigned> processedStreams = 0;
  ZmAtomic<unsigned> connectedCount = 0;
  ZmAtomic<unsigned> disconnectedCount = 0;
  ZmAtomic<unsigned> streamedCount = 0;

  TestServerLink(TestServer *app) : Base{app} { }

  Zquic_::EndpointTxGate *heldGate = nullptr; // Tx-owned fixture admission seam
  Zquic_::EndpointTxWait *heldWait = nullptr;
  Zquic_::PktAllocTarget heldTarget;
  void waitPktTx(Zquic_::EndpointTxWait &wait, uint64_t epoch, Zquic_::EndpointTxFn fn) {
    if (!heldGate || Base::pktAllocTarget() != heldTarget) {
      Base::waitPktTx(wait, epoch, ZuMv(fn));
      return;
    }
    heldWait = &wait;
    wait.start(app()->mx(), app()->txThread(), epoch, ZuMv(fn));
    app()->mx()->txRun([gate = heldGate, wait = &wait] { gate->waitTx(*wait); });
  }
  bool armHeldGate(Zquic_::EndpointTxGate *gate) {
    heldGate = gate;
    heldTarget = Base::pktAllocTarget();
    // Install the real protocol LinkCapacity waiter by refusing physical
    // admission. The synthetic gate stays full throughout preferred migration.
    bool sent = Base::submitPktTx({}, Base::activePathRemote_(),
      Zquic::EcnMark::NotECT, false,
      [](auto, ZiSockAddr, Zquic::EcnMark::T, bool,
	  Zquic_::EndpointTxToken, bool *blocked) {
	*blocked = true;
	return false;
      });
    return !sent && heldWait && heldWait->busy() && Base::pktAllocationPending();
  }
  bool heldWaitBusy() const { return heldWait && heldWait->busy(); }
  Zquic::RxPkt *repeat = nullptr; // identity of the retained replay on Rx
  ZmAtomic<unsigned> repeatedInitials = 0;
  bool receivedRouted(ZmRef<Zquic::RxPkt> pkt, const Zquic::CxnID &dcid) {
    if (pkt.ptr() == repeat) {
      ++repeatedInitials;
      repeat = nullptr;
    } else if (app()->repeatInitial) {
      app()->repeatInitial = false;
      auto owner = app()->pktAllocTarget(pkt->local).owner;
      auto ep = static_cast<TestServer::Endpoint *>(static_cast<TestServer::Endpoint::Base *>(owner));
      ZmRef<Zquic::RxPkt> copy{app()->rxPktAllocator()->allocBuf(nullptr, 0)};
      if (ep && copy) {
	copy->append(pkt->cspan());
	copy->addr = pkt->addr;
	copy->local = pkt->local;
	copy->ecn = pkt->ecn;
	repeat = copy.ptr();
	app()->rxRun([ep, copy = ZuMv(copy)]() mutable { ep->endpointPacket(ZuMv(copy)); });
      }
    }
    return Base::receivedRouted(ZuMv(pkt), dcid);
  }
  Zquic::CxnRouter<TestServerLink> *router = nullptr; // fixture server owns the router
  ZmAtomic<unsigned> earlyBytes = 0;
  void streamData(ZmRef<Stream> stream, uint64_t offset, ZuBSpan data, bool fin) {
    if (stream->earlyData()) earlyBytes += data.length();
    Base::streamData(ZuMv(stream), offset, data, fin);
  }
  unsigned collisions = 0, cidReady = 0;
  void migrationCIDsReady() {
    ++cidReady;
    Base::migrationCIDsReady();
  }
  bool bindLocalCID(Base::LinkCID &candidate) {
    if (app()->issuanceCollisions && (!collisions || candidate.sequence == 2)) {
      // Force a generated ID collision while preserving the new sequence/token.
      ++collisions;
      auto conflict = candidate;
      conflict.id = Base::findLocalCID(0)->id;
      return Base::bindLocalCID(conflict);
    }
    return Base::bindLocalCID(candidate);
  }
  bool retryIssuance(Zquic_::LinkCID &second) {
    auto first = Base::findLocalCID(1);
    if (collisions != 5 || cidReady || !first || !first->associated ||
	Base::findLocalCID(2) || !routeMatches(*first)) return false;
    auto saved = *first;
    app()->issuanceCollisions = false;
    Base::scheduleMigrationCIDs();
    auto replacement = Base::findLocalCID(2);
    first = Base::findLocalCID(1);
    if (cidReady != 1 || !first || first->id != saved.id ||
	!routeMatches(saved) || !replacement || !routeMatches(*replacement)) return false;
    second = *replacement;
    return true;
  }
  bool routeMatches(const Base::LinkCID &cid) const {
    Zquic::ResetToken token;
    return router->find(cid.id) == this && router->resetToken(cid.id, token) &&
      token == cid.resetToken;
  }
  bool survivedBootstrapCollision(const Zquic::CxnID &original) {
    return !router->find(original) && routeMatches(*Base::findLocalCID(0));
  }
  bool bindBootstrapRoutes(Zquic::CxnRouter<TestServerLink> &routes) {
    router = &routes;
    if (auto sibling = app()->bootstrapCollision) {
      Base::findLocalCID(0)->id = sibling->Base::findLocalCID(0)->id;
      app()->bootstrapCollision = nullptr;
    }
    if (!Base::bindBootstrapRoutes(routes)) return false;
    if (!app()->resetFixture || app()->resetInstalled) return true;
    if (!routes.add(app()->resetCID, Zquic::U64Null, this, app()->resetToken)) return false;
    // Test-only installation of the router's existing Retired+token state.
    // Production retirement deletes/tombstones routes; its policy is unchanged.
    // The key/table are untouched and this single record lives until server stop.
    using Route = Zquic::CxnRouter<TestServerLink>::Route;
    routes.all([this](const Route &route) {
      if (route.id != app()->resetCID) return;
      auto &reset = const_cast<Route &>(route);
      reset.state = Zquic::CxnState::Retired;
      reset.link = nullptr;
      app()->resetInstalled = true;
    });
    return true;
  }
  ZiSockAddr originalPeer;
  bool retireUnusedPreferred() {
    auto original = Base::findLocalCID(0);
    auto preferred = Base::findLocalCID(1);
    if (!original || original->state != Zquic::CxnState::Active ||
	!original->associated || !preferred ||
	preferred->state != Zquic::CxnState::Active ||
	Base::findLocalCID(2)) return false;
    auto originalID = original->id;
    auto preferredID = preferred->id;
    auto preferredToken = preferred->resetToken;
    // Model a valid RETIRE_CONNECTION_ID for the unused advertised CID while
    // this stationary client continues to address the original sequence zero.
    Zquic::Frame retire;
    retire.type = Zquic::FrameType::RetireCxnID;
    retire.value = 1;
    if (!Base::receiveRetireCxnID_(retire)) return false;
    original = Base::findLocalCID(0);
    // The bounded local table may reuse the retired sequence-one slot.
    preferred = Base::findLocalCID(1);
    auto replacement = Base::findLocalCID(2);
    if (!Base::receiveRetireCxnID_(retire) || router->find(preferredID)) return false;
    return original && original->id == originalID && original->associated &&
      original->state == Zquic::CxnState::Active &&
      (!preferred || (preferred->state == Zquic::CxnState::Retired && !preferred->associated)) &&
      replacement && replacement->state == Zquic::CxnState::Active &&
      replacement->associated && replacement->id != preferredID &&
      replacement->resetToken.valid() && replacement->resetToken != preferredToken;
  }
  bool preferredRouted(const Zquic::PreferredAddress &preferred) const {
    auto cid = Base::findLocalCID(1);
    return cid && cid->associated && cid->id == preferred.cid &&
      cid->resetToken == preferred.resetToken;
  }
  bool initTLSFixture() {
    return Base::initTLS_(Zquic::CryptoConfig{
      .isServer = true,
      .alpn = app()->firstALPN(),
      .certPath = app()->certPath(),
      .keyPath = app()->keyPath()
    }) && Base::startHandshake();
  }

  bool inputTLS(size_t epoch, ZuBSpan input, ZiSockAddr addr = {}) {
    return Base::advanceTLS(epoch, input, ZuMv(addr),
      [this](ZmRef<ZiIOBuf> output, const Zquic::CryptoOffsets &, ZiSockAddr) {
	tlsFlightBytes += output->length;
	return true;
      },
      [] {}, [](ZiSockAddr) { return true; });
  }

  bool inputCryptoFrame(uint64_t offset, ZuBSpan input) {
    uint8_t frame[Zquic::BufSize];
    int n = Zquic::FrameCodec::writeCrypto(frame, sizeof(frame), offset, input);
    if (n < 0) return false;
    typename Base::RxAckMeta ack;
    return Base::consumeProtFrames(
      Zquic::PktNumSpace::Initial, 0, ZuBSpan{frame, unsigned(n)}, {}, {},
      ack, nullptr,
      [this](size_t epoch, ZuBSpan bytes, ZiSockAddr addr) {
	return inputTLS(epoch, bytes, ZuMv(addr));
      },
      [](Zquic::PktNumSpace::T, const Zquic::Frame &, ZiSockAddr) {
	return false;
      });
  }

  unsigned tlsFlightBytes = 0;

  void connected(Zquic::Connected) {
    Base::snapshotTxCrypto(handshakeKeys);
    ++connectedCount;
    connectedEvent.post();
  }
  bool handshakeRxKeysLive() {
    auto &crypto = Base::crypto_();
    return crypto.rxSecretInstalled(Zquic::PktNumSpace::Handshake) ||
      crypto.txSecretInstalled(Zquic::PktNumSpace::Handshake) ||
      crypto.rxProtState(Zquic::PktNumSpace::Handshake).valid();
  }
  bool handshakeTxKeysLive() const {
    return Base::txSecretInstalled_(Zquic::PktNumSpace::Handshake);
  }
  bool staleKeysStayRetired() {
    return Base::installTxCrypto(handshakeKeys) &&
      !handshakeTxKeysLive() &&
      Base::txSecretInstalled_(Zquic::PktNumSpace::AppData);
  }
  void streamed(ZmRef<Stream>) {
    ++streamedCount;
    streamEvent.post();
  }
  void processedStream() {
    ++processedStreams;
    streamEvent.post();
  }
  bool waitConnected(ZuTime deadline) {
    return connectedEvent.wait([this]() {
      return connectedCount || disconnectedCount;
    }, deadline) && connectedCount && !disconnectedCount && established();
  }
  bool waitStreams(unsigned n, ZuTime deadline) {
    return streamEvent.wait([this, n]() {
      return (streamedCount >= n && processedStreams >= n) || disconnectedCount;
    }, deadline) && streamedCount >= n && processedStreams >= n;
  }
  bool waitDisconnected(ZuTime deadline) {
    return disconnectedEvent.wait([this]() { return disconnectedCount; }, deadline);
  }
  void forceCloseTimeout() {
    ZmBlock<>{}([this](auto wake) {
      app()->txRun([
	link = ZmRef<TestServerLink>{this}, wake = ZuMv(wake)
      ]() mutable {
	link->Base::closeTimeout();
	wake();
      });
    });
  }

  void transportClose(Zquic::FrameType::T type, uint64_t error) {
    Base::transportClose(type, error);
    // The callback precedes enterPeerDraining_0 in the current receive turn.
    app()->rxRun([this] { peerCloseEvent.post(); });
  }

  void migrationPromoted(const Zquic::MigrationResult &result) {
    Base::migrationPromoted(result);
    ++migrationPromotions;
    migrationEvent.post();
  }
  void migrationStarted(const Zquic::MigrationResult &result) {
    Base::migrationStarted(result);
    if (app()->stopOnMigration != 1) return;
    migrationStop = result;
    // Request shutdown in this exact validation turn, before its challenge
    // can return proof. Never block an owner waiting for its own drain.
    app()->stop([](bool) { });
    migrationEvent.post();
  }
  template <typename L> void commitMigEndpoint(L l) {
    if (app()->stopOnMigration != 2) { l(true); return; }
    heldMigrationCommit = ZuMv(l);
    Base::migrationState([this](const Zquic::MigrationResult &result) {
      migrationStop = result;
    });
    app()->stop([](bool) { });
    migrationEvent.post();
  }
};

void TestServer::disconnected(Link *link, bool peer)
{
  if (stopOnDisconnect) {
    stopOnDisconnect = false;
    ++disconnectStops;
    stop({}); // queues stop enumeration behind this completion on Rx
  }
  Zquic::Server<TestServer, TestServerLink>::disconnected(link, peer);
  ++link->disconnectedCount;
  link->disconnectedEvent.post();
  link->connectedEvent.post();
  link->streamEvent.post();
}

ZmRef<TestServer::Link> TestServer::link(unsigned i)
{
  if (i != unsigned(-1))
    return i < TestSrvLinkCap ? links_[i] : nullptr;
  return link_;
}

ZmRef<TestServer::Link> TestServer::accepted(const Zquic::InitialInfo &info)
{
  unsigned i = acceptedCount;
  if (i >= TestSrvLinkCap) return nullptr;
  links_[i] = new Link{this};
  links_[i]->originalPeer = info.peer;
  link_ = links_[i];
  ++acceptedCount;
  acceptedEvent.post();
  return link_;
}

void TestClient::earlyDataReady(Link *link)
{
  auto stream = link->stream(Zquic::StreamType::Duplex);
  if (stream) link->send(stream, "resumed", true);
}

int TestClient::Stream::process(Zquic::RxStream &)
{
  if (!processed++) link()->processedStream();
  else link()->streamEvent.post();
  return 0;
}

int TestServerStream::process(Zquic::RxStream &)
{
  if (!processed++) link()->processedStream();
  else link()->streamEvent.post();
  return 0;
}

ZmRef<TestServer::Link> TestServer::waitLink(unsigned i, ZuTime deadline)
{
  if (!acceptedEvent.wait([this, i]() { return acceptedCount > i; }, deadline))
    return nullptr;
  return link(i);
}

template <typename Link> static bool ownerStorage(const Link &link)
{
  using Base = typename Link::Base;
  bool valid = !(uintptr_t(&link) % Zm::CacheLineSize);
  link.ownerLayout([&valid](const void *address, size_t size) {
    valid &= !(uintptr_t(address) % Zm::CacheLineSize) && !(size % Zm::CacheLineSize);
  });
  valid &= uintptr_t(&link.handshakeKeys) >=
    uintptr_t(static_cast<const Base *>(&link)) + sizeof(Base);
  if (!valid) log_("concrete QUIC link owner storage overlaps a cache line or derived tail");
  return valid;
}

static bool waitEstablished(
  const ZmRef<TestClient::Link> &client, TestServer &server,
  ZmRef<TestServer::Link> &peer, unsigned i = 0)
{
  ZuTime deadline = Zm::now(10);
  bool connected = client->waitConnected(deadline);
  peer = server.waitLink(i, deadline);
  return connected && peer && peer->waitConnected(deadline) &&
    ownerStorage(*client) && ownerStorage(*peer);
}

void TestServer::clearLinks()
{
  link_ = nullptr;
  for (unsigned i = 0; i < TestSrvLinkCap; ++i)
    links_[i] = nullptr;
}

static void waitThread(ZiMultiplex &mx, unsigned thread)
{
	ZmBlock<>{}([&mx, thread](auto wake) {
	  mx.invoke([wake = ZuMv(wake)]() mutable { wake(); }, thread);
	});
}

template <typename App>
static void waitDisconnect(App &app)
{
  auto &mx = *app.mx();
  waitThread(mx, app.txThread());
  waitThread(mx, app.rxThread());
  // UDP endpoint teardown hands its completion from mx Rx to mx Tx,
  // then back to the QUIC app's Tx shard.
  waitThread(mx, mx.rxThread());
  waitThread(mx, mx.txThread());
  waitThread(mx, app.txThread());
  waitThread(mx, app.rxThread());
}

#ifdef Zquic_DEBUG
static ZtString<> readFile(ZuCSpan path)
{
  ZtString<> data;
  ZiFile file{Zi::Path{path}, ZiFile::ReadOnly | ZiFile::GC};
  if (!file) return data;
  auto size = file.size();
  if (size <= 0 || size > (4<<20)) return data;
  data.length(unsigned(size));
  int n = file.read(data.data(), data.length());
  if (n <= 0)
    data.length(0);
  else
    data.length(unsigned(n));
  return data;
}

ZuTuple<unsigned, bool> challengeSizes(ZuCSpan sequence)
{
  using Node = ZfJSON::AnyNode;
  auto field = [](const Node *object, ZuCSpan key) -> const Node * {
    if (!object || !object->has<Node::Object>()) return nullptr;
    for (const auto &entry : object->data<Node::Object>())
      if (entry.p<0>() == key) return entry.p<1>().ptr();
    return nullptr;
  };
  unsigned count = 0, offset = 0;
  bool valid = true;
  while (offset < sequence.length()) {
    if (sequence[offset++] != '\x1e') return {0, false};
    unsigned start = offset;
    while (offset < sequence.length() && sequence[offset] != '\n') ++offset;
    if (offset == sequence.length()) return {0, false};
    ZtString<> json;
    json << ZuCSpan{sequence.data() + start, offset - start};
    ++offset;
    auto parsed = ZfJSON::scan(json);
    if (parsed.p<0>() != int(json.length())) return {0, false};
    auto root = parsed.p<1>().ptr();
    if (!root || !root->has<Node::Array>()) return {0, false};
    const auto &values = root->data<Node::Array>();
    if (values.length() != 1) return {0, false};
    auto event = values[0].ptr();
    auto name = field(event, "name");
    if (!name || !name->has<Node::String>() ||
	name->data<Node::String>() != "quic:packet_sent") continue;
    auto data = field(event, "data");
    auto frames = field(data, "frames");
    if (!frames || !frames->has<Node::Array>()) continue;
    bool challenge = false;
    for (const auto &frame : frames->data<Node::Array>()) {
      auto type = field(frame.ptr(), "frame_type");
      if (type && type->has<Node::String>() &&
	  type->data<Node::String>() == "path_challenge") {
	challenge = true;
	break;
      }
    }
    if (!challenge) continue;
    ++count;
    auto length = field(field(data, "raw"), "length");
    if (!length || !length->has<Node::Number>()) return {count, false};
    auto text = length->data<Node::Number>();
    ZuBox<unsigned> bytes;
    valid &= bytes.scan(text) == int(text.length()) &&
      bytes >= Zquic::MinUDPPayload;
  }
  return {count, valid};
}
#endif

static void dumpDiag(
  const char *name, const Zquic::LinkDiag &d, const Zquic::CryptoDiag &c)
{
  std::cout <<
    "# " << name <<
    " endpointReady=" << uint64_t(d.rx.endpointReady) <<
    " datagramsRx=" << uint64_t(d.rx.datagramsRx) <<
    " bytesRx=" << uint64_t(d.rx.bytesRx) <<
    " bytesTx=" << uint64_t(d.tx.bytesTx) <<
    " packetsTx=" << uint64_t(d.tx.packetsTx) <<
    " packetsRx=" << uint64_t(d.rx.packetsRx) <<
    " framesRx=" << uint64_t(d.rx.framesRx) <<
    " cryptoBytesTx=" << uint64_t(d.tx.cryptoBytesTx) <<
    " cryptoBytesRx=" << uint64_t(d.rx.cryptoBytesRx) <<
    " streamBytesTx=" << uint64_t(d.tx.streamBytesTx) <<
    " streamBytesRx=" << uint64_t(d.rx.streamBytesRx) <<
    " ptoCount=" << uint64_t(d.tx.ptoCount) <<
    " retransmittedFrames=" << uint64_t(d.tx.retransmittedFrames) <<
    " migReq=" << uint64_t(d.tx.migration.requested) <<
    " migStarted=" << uint64_t(d.tx.migration.started) <<
    " migPromoted=" << uint64_t(d.tx.migration.promoted) <<
    " migFailed=" << uint64_t(d.tx.migration.failed) <<
    " failures=" << uint64_t(d.failures()) <<
    " handshakeComplete=" << uint64_t(d.rx.handshakeComplete) <<
    " tlsHandled=" << c.tlsMessagesHandled <<
    " tlsEmitted=" << c.tlsMessagesEmitted <<
    " secrets=" << c.secretsInstalled <<
    '\n';
}

struct TempDir {
  Zi::Path	path;
  ZtString<>	certPath;
  ZtString<>	keyPath;

  bool init()
  {
    static unsigned counter;
    Zi::Name name;
    name << "case" << ZuBox<unsigned>{++counter};
    path = ZiTestResidue::dir(name);
    certPath << path << "/cert.pem";
    keyPath << path << "/key.pem";

    ZtString<> cmd;
    cmd <<
      "openssl req -x509 -newkey rsa:2048 -nodes -days 1 "
      "-subj /CN=localhost "
      "-addext basicConstraints=critical,CA:TRUE "
      "-addext keyUsage=critical,digitalSignature,keyEncipherment,keyCertSign "
      "-addext subjectAltName=DNS:localhost,IP:127.0.0.1,IP:127.0.0.2,IP:::1 "
      "-keyout " << keyPath << ' ' <<
      "-out " << certPath << " >/dev/null 2>&1";
    return systemOK(system(cmd.data()));
  }

  ZtString<> pathOf(const char *name) const
  {
    ZtString<> s;
    s << path << '/' << name;
    return s;
  }

  static bool systemOK(int status)
  {
    return status != -1 && WIFEXITED(status) && !WEXITSTATUS(status);
  }

};



template <typename Link>
static void closeClient(const Link &link)
{
  ZmBlock<>{}([&link](auto wake) {
    link->disconnect([wake = ZuMv(wake)]() mutable { wake(); });
  });
}

// Failed handshake predicates do not stop asynchronous TLS or physical I/O.
// Keep both app configurations alive until link and listener owners finish.
static void drainFailedHandshake(
    const ZmRef<TestClient::Link> &client, TestServer &server)
{
  if (client) closeClient(client);
  server.stop();
  waitDisconnect(server);
}

// Exercise the public ownership-bearing and borrowed APIs on the existing live fixture.
template <typename Sender, typename Receiver, typename Arm>
void testOwnedStreamAPI(const Sender &sender, const Receiver &receiver,
    Arm arm, bool offThread)
{
  ZuTestScopeRT(testOwnedStreamAPI);
  enum { N = 6000, Borrowed = 257 };
  uint8_t expected[N];
  for (unsigned i = 0; i < N; ++i) expected[i] = uint8_t('a' + i%23);
  auto output = openStream(sender);
  ZQUIC_CHECK_RT(output, "owned stream did not open");
  if (!output) return;
  static ZiMultiplex mx;
  static auto allocator = Zquic::plaintextAllocator<
    Zquic::StreamTxBufAlloc<8192, 8192>>(&mx);
  auto backing = allocator->allocBuf();
  backing->skip = 9;
  backing->append(ZuBSpan{"prefix!"});
  backing->append(expected, N);
  backing->advance(7);
  bool accepted;
  if (offThread) {
    accepted = sender->send(output, ZuMv(backing));
    backing = nullptr; // release caller ownership immediately after public return
  } else accepted = ZmBlock<bool>{}([sender, output, &backing, arm](auto wake) {
    sender->app()->txRun([
      sender, output, backing = ZuMv(backing), arm, wake = ZuMv(wake)
    ]() mutable {
      arm(); // unchanged fixture packet-drop mechanism, on its Tx owner
      bool ok = sender->send(output, ZuMv(backing));
      backing = nullptr; // no fixture capture survives public send return
      wake(ok);
    });
  });
  ZQUIC_CHECK_RT(accepted && !backing, "owned buffer API admission/caller release failed");
  auto checkInput = [receiver](const auto &stream, const uint8_t *bytes, unsigned size) {
    auto id = stream->id();
    return receiver->streamEvent.wait([receiver, id, bytes, size]() {
      auto incoming = findStream(receiver, id);
      if (!incoming) return false;
      return ZmBlock<bool>{}([receiver, incoming, bytes, size](auto wake) {
        receiver->app()->rxRun([incoming, bytes, size, wake = ZuMv(wake)]() mutable {
          auto &rx = incoming->rxStream(); unsigned offset = 0;
          bool ok = rx.length() == size;
          if (ok) {
            auto n = rx.each(size, [bytes, &offset](auto part) -> int64_t {
              if (memcmp(part.data(), bytes+offset, part.length())) return -1;
              offset += part.length(); return part.length();
            });
            ok = n == int64_t(size);
          }
          wake(ok);
        });
      });
    }, Zm::now(10));
  };
  ZQUIC_CHECK_RT(checkInput(output, expected, N), "owned protected bytes changed");
  bool completed = output->txEvent.wait([sender, output]() {
    return ZmBlock<bool>{}([sender, output](auto wake) {
      sender->app()->txRun([output, wake = ZuMv(wake)]() mutable {
        wake(output->finSent() && output->txDrained());
      });
    });
  }, Zm::now(10));
  ZQUIC_CHECK_RT(completed, "owned buffer FIN/ACK did not drain");
  auto borrowedStream = openStream(sender);
  ZQUIC_CHECK_RT(borrowedStream, "borrowed stream did not open");
  if (!borrowedStream) return;
  uint8_t borrowed[Borrowed], saved[Borrowed];
  for (unsigned i=0; i<Borrowed; ++i) borrowed[i] = saved[i] = uint8_t(i%251);
  ZQUIC_CHECK_RT(sender->send(borrowedStream, ZuBSpan{borrowed, Borrowed}),
    "borrowed span API admission failed");
  memset(borrowed, 0xa5, sizeof(borrowed)); // public call must have acquired ownership
  ZQUIC_CHECK_RT(checkInput(borrowedStream, saved, Borrowed),
    "caller overwrite changed borrowed-span delivery");
}

static bool connectionlessSource(const ZiSockAddr &binding, bool retry)
{
  int fd = socket(binding.ip().v6() ? AF_INET6 : AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) return false;
  timeval timeout{3, 0};
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  uint8_t packet[Zquic::MinUDPPayload] = {};
  Zquic::CxnID dcid{ZuBSpan{"original"}}, scid{ZuBSpan{"clientid"}};
  int n = Zquic::Pkt::writeInitial(packet, sizeof(packet), dcid, scid, 0, 1);
  if (n < 0) { close(fd); return false; }
  if (!retry) packet[1] = packet[2] = packet[3] = packet[4] = 0xfa;
  bool sent = sendto(fd, packet, sizeof(packet), 0, binding.sa(), binding.len()) ==
    ssize_t(sizeof(packet));
  uint8_t response[Zquic::BufSize];
  ZiSockAddr source;
  source.init(binding.ip().type());
  socklen_t length = source.len();
  int received = sent ? recvfrom(fd, response, sizeof(response), 0, source.sa(), &length) : -1;
  source.sync();
  close(fd);
  if (received <= 0 || source != binding) return false;
  if (retry) {
    Zquic::LongHdr h;
    return Zquic::Pkt::parseLong(ZuBSpan{response, unsigned(received)}, h) > 0 &&
      h.type == Zquic::PktType::Retry;
  }
  uint32_t versions[4];
  unsigned count = 0;
  return Zquic::Pkt::parseVerNeg(ZuBSpan{response, unsigned(received)},
    versions, 4, count) == 0 && count;
}

static bool resetBindingSource(const ZiSockAddr &binding,
    const Zquic::CxnID &cid, const Zquic::ResetToken &token)
{
  int fd = socket(binding.ip().v6() ? AF_INET6 : AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) return false;
  timeval timeout{3, 0};
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  uint8_t packet[64] = {0x40};
  memcpy(packet + 1, cid.data(), cid.length());
  bool sent = sendto(fd, packet, sizeof(packet), 0, binding.sa(), binding.len()) ==
    ssize_t(sizeof(packet));
  uint8_t response[Zquic::BufSize];
  ZiSockAddr source;
  source.init(binding.ip().type());
  socklen_t length = source.len();
  int received = sent ? recvfrom(fd, response, sizeof(response), 0, source.sa(), &length) : -1;
  source.sync();
  close(fd);
  return received >= 21 && received < int(sizeof(packet)) && source == binding &&
    !Zquic::Pkt::isLong(ZuBSpan{response, unsigned(received)}) &&
    !memcmp(response + received - Zquic::ResetToken::Length,
      token.data(), Zquic::ResetToken::Length);
}

void testResetBindingSource()
{
  ZuTestScopeRT(testResetBindingSource);
  TempDir temp;
  bool cert = temp.init();
  ZQUIC_CHECK_RT(cert, "stateless reset source certificate generation failed");
  if (!cert) return;
  ZiMultiplex mx{ZiMxParams().scheduler([](auto &s) { s.nThreads(5); })
    .rxThread(1).txThread(2)};
  TestServer server;
  server.resetFixture = true;
  server.addrs = {
    {ZiIP{"127.0.0.1"}, ZquicTestPort::Original4},
    {ZiIP{"127.0.0.1"}, ZquicTestPort::Preferred4},
    {ZiIP{"::1"}, ZquicTestPort::Original6},
    {ZiIP{"::1"}, ZquicTestPort::Preferred6}};
  bool serverInit = server.init(Zquic::ServerParams{&mx, "3", "4"}
    .certPath(temp.certPath.cspan()).keyPath(temp.keyPath.cspan())
    .alpn(ZuSpan<ZuCSpan>{"h3"}));
  TestClient client;
  bool clientInit = client.init(Zquic::ClientParams{&mx, "3", "4"}
    .caPath(temp.certPath.cspan()).migrationMode(Zquic::MigrationMode::Disabled)
    .alpn(ZuSpan<ZuCSpan>{"h3"}));
  bool mxStarted = mx.start();
  ZQUIC_CHECK_RT(serverInit && clientInit && mxStarted,
    "stateless reset source fixture initialization failed");
  if (!mxStarted) { client.final(); server.final(); return; }
  bool ready = serverInit && clientInit && server.start();
  ZQUIC_CHECK_RT(ready, "stateless reset source server startup failed");
  ZmRef<TestClient::Link> link;
  ZmRef<TestServer::Link> peer;
  if (ready) {
    link = new TestClient::Link{&client};
    link->connect(Zquic::Host{"127.0.0.1"}, server.local().port());
    bool established = waitEstablished(link, server, peer);
    ZQUIC_CHECK_RT(established, "stateless reset route fixture handshake failed");
    if (established) {
      bool installed = ZmBlock<bool>{}([&server](auto wake) {
	server.rxRun([&server, wake = ZuMv(wake)]() mutable {
	  wake(server.resetInstalled);
	});
      });
      ZQUIC_CHECK_RT(installed &&
	resetBindingSource(server.addrs.original4, server.resetCID, server.resetToken) &&
	resetBindingSource(server.addrs.original6, server.resetCID, server.resetToken),
	"stateless reset response used a different original source binding");
    }
  }
  if (link) closeClient(link);
  if (ready) server.stop();
  waitDisconnect(server);
  link = nullptr; peer = nullptr;
  server.clearLinks();
  client.final(); server.final();
  mx.stop();
}

void testServerBindings()
{
  ZuTestScopeRT(testServerBindings);
  using Adapter = Zquic_::ServerEP<TestServer>;
  using Physical = Zquic_::EndpointCxn<Adapter::Base>;
  log("# preferred storage: adapter=", sizeof(Adapter), " physical=", sizeof(Physical),
    " queue=", sizeof(Zi::IOQueue), " gate=", sizeof(Zquic_::EndpointTxGate),
    " reservation=", 4096*24, " four-reservations=", 4*4096*24,
    " provider-default-count=", ZiIOBuf_DefltCount,
    " builtin backing grows; finite providers impose their own shared bound");
  TempDir temp;
  bool cert = temp.init();
  ZQUIC_CHECK_RT(cert, "shared listener certificate generation failed");
  if (!cert) return;
  ZiMultiplex mx{ZiMxParams().scheduler([](auto &s) { s.nThreads(5); })
    .rxThread(1).txThread(2)};
  TestServer server;
  Zquic::TokenSecret secret;
  secret.length(Zquic::AddressToken::SecretLength);
  memset(secret.data(), 0x17, secret.length());
  bool init = server.init(Zquic::ServerParams{&mx, "3", "4"}
    .certPath(temp.certPath.cspan()).keyPath(temp.keyPath.cspan())
    .addrValidationSecret(secret).alpn(ZuSpan<ZuCSpan>{"h3"}));
  ZQUIC_CHECK_RT(init, "shared listener server initialization failed");
  if (!init) return;
  bool started = mx.start();
  ZQUIC_CHECK_RT(started, "shared listener multiplexer start failed");
  if (!started) { server.final(); return; }

  // An invalid explicit configuration opens no socket and reports typed failure.
  server.addrs.original4 = ZiSockAddr{ZiIP{"127.0.0.1"}, ZquicTestPort::Original4};
  ZQUIC_CHECK_RT(!server.start() && !server.listening(),
    "invalid explicit bindings started the server");
  server.addrs.preferred4 = ZiSockAddr{ZiIP{"127.0.0.1"}, ZquicTestPort::Preferred4};
  // A bind failure after opening the original must drain it before start fails.
  int occupied = socket(AF_INET, SOCK_DGRAM, 0);
  bool blocked = occupied >= 0 && !bind(occupied, server.addrs.preferred4.sa(),
    server.addrs.preferred4.len());
  ZQUIC_CHECK_RT(blocked, "shared listener failed to reserve conflicting fixture socket");
  if (blocked)
    ZQUIC_CHECK_RT(!server.start() && !server.connected(),
      "partial listener bind failure did not drain startup");
  if (occupied >= 0) close(occupied);

  server.addrs.original6 = ZiSockAddr{ZiIP{"::1"}, ZquicTestPort::Original6};
  server.addrs.preferred6 = ZiSockAddr{ZiIP{"::1"}, ZquicTestPort::Preferred6};
  Zquic_::PktAllocTarget previous[4];
  for (unsigned attempt = 0; attempt < 2; ++attempt) {
    bool ready = server.start();
    ZQUIC_CHECK_RT(ready && server.listening() && server.endpointCount() == 4 &&
      server.local() == server.addrs.original4,
      "four bindings did not become ready together");
    if (!ready) break;
    if (attempt) {
      bool staleRejected = ZmBlock<bool>{}([&mx, &server, &previous](auto wake) {
	mx.txRun([&server, &previous, wake = ZuMv(wake)]() mutable {
	  bool rejected = true;
	  for (unsigned slot = 0; slot < 4; ++slot) {
	    auto buf = server.allocTxPkt_(previous[slot]);
	    bool blocked = false;
	    if (!buf) { rejected = false; continue; }
	    buf->append(ZuBSpan{"stale"});
	    rejected &= !server.sendPktRaw(ZuMv(buf), server.local(),
	      Zquic::EcnMark::NotECT, false, {}, &blocked, previous[slot]) && !blocked;
	  }
	  wake(rejected);
	});
      });
      ZQUIC_CHECK_RT(staleRejected,
	"stale physical output snapshot selected a replacement server socket");
      for (unsigned slot = 0; slot < 4; ++slot) {
	Zquic_::EndpointTxWait wait;
	bool cancelled = ZmBlock<bool>{}([&server, &wait, &previous, slot](auto wake) {
	  server.txRun([&server, &wait, &previous, slot, wake = ZuMv(wake)]() mutable {
	    server.waitTx(wait, server.txThread(), 17,
	      [wake = ZuMv(wake)](int result, Zquic_::EndpointTxToken) mutable {
		wake(result == Zquic_::EndpointTxResult::Cancelled);
	      }, previous[slot]);
	  });
	});
	ZQUIC_CHECK_RT(cancelled && !wait.busy(),
	  "stale endpoint admission consumed replacement socket credit");
	auto d = ZmBlock<Zquic::EndpointDiag>{}([&server, slot](auto wake) {
	  server.endpointDiag(slot, ZuMv(wake));
	});
	ZQUIC_CHECK_RT(d.tx.txDropped == 1 && !d.tx.submittedTx,
	  "stale submission was not charged to its original physical slot");
      }
    }
    for (unsigned slot = 0; slot < 4; ++slot)
      previous[slot] = server.pktAllocTarget(slot, server.endpointGeneration());
    auto frozen = server.runtimeServerAddrs();
    auto preferred = server.addrs.preferred4;
    server.addrs.preferred4.null();
    ZQUIC_CHECK_RT(server.runtimeServerAddrs().preferred4 == preferred,
      "running address configuration changed before restart");
    server.addrs.preferred4 = preferred;
    ZQUIC_CHECK_RT(connectionlessSource(frozen.original4, false) &&
      connectionlessSource(frozen.original6, false),
      "Version Negotiation response used a different source binding");
    ZmBlock<>{}([&server](auto wake) {
      server.rxRun([&server, wake = ZuMv(wake)]() mutable {
	server.retryMode = true;
	wake();
      });
    });
    ZQUIC_CHECK_RT(connectionlessSource(frozen.original4, true) &&
      connectionlessSource(frozen.original6, true),
      "Retry response used a different source binding");
    ZmBlock<>{}([&server](auto wake) {
      server.rxRun([&server, wake = ZuMv(wake)]() mutable {
	server.retryMode = false;
	wake();
      });
    });
    ZQUIC_CHECK_RT(server.stop() && !server.listening(),
      "shared listeners remained ready after shutdown drain");
  }
  // Default mode allocates no additional adapter and still accepts ephemeral binds.
  server.addrs = {};
  ZQUIC_CHECK_RT(server.start() && server.endpointCount() == 1,
    "restart did not restore the single-endpoint default");
  server.stop();
  server.final();
  mx.stop();
}

void testEndpointOpen()
{
  ZuTestScope(testEndpointOpen);

  TempDir temp;
  ZuCHECK(temp.init(), "runtime temporary TLS certificate generation failed");
  ZtString<> qlogPath = temp.pathOf("endpoint.sqlog");
  ZtString<> clientQLogPath = temp.pathOf("client.sqlog");

  ZiMultiplex mx(
      ZiMxParams()
	.scheduler([](auto &s) {
	  s.nThreads(5)
	    .thread(1, [](auto &t) { t.isolated(1); })
	    .thread(2, [](auto &t) { t.isolated(1); })
	    .thread(3, [](auto &t) { t.isolated(1); })
	    .thread(4, [](auto &t) { t.isolated(1); }); })
	.rxThread(1).txThread(2));

  TestServer server;
  ZuCHECK(server.init(
      Zquic::ServerParams(&mx, "3", "4")
	.certPath(temp.certPath.cspan())
	.keyPath(temp.keyPath.cspan())
	.qlog(true)
	.qlogPath(qlogPath.cspan())

	.qlogRingSize(1<<16)
	.maxData(32768)
	.maxStreamData(8192)
	.maxStreamsDuplex(8)
	.maxStreamsSimplex(8)
	.alpn(ZuSpan<ZuCSpan>{"h3"})),
    "runtime server init failed");
  TestClient client;
  ZuCHECK(client.init(
      Zquic::ClientParams(&mx, "3", "4")
	.caPath(temp.certPath.cspan())
	.qlog(true)
	.qlogPath(clientQLogPath.cspan())
	.qlogRingSize(1<<16)
	.maxData(32768)
	.maxStreamData(8192)
	.maxStreamsDuplex(8)
	.maxStreamsSimplex(8)
	.alpn(ZuSpan<ZuCSpan>{"h3"})),
    "runtime client init failed");
  bool mxStarted = mx.start();
  ZuCHECK(mxStarted, "runtime multiplexer start failed");
  if (!mxStarted) { client.final(); server.final(); return; }

  ZuCHECK(server.start(),
    "runtime server listen failed");
  ZuCHECK(server.listening(),
    "runtime server endpoint did not become ready");
  ZuCHECK(server.listening() &&
      !endpointDiag(server).failures() &&
      server.local().port(),
    "runtime server endpoint diagnostics mismatch");
	  if (!server.listening()) {
	    server.stop();
	    client.final();
	    server.final();
	    mx.stop();
	    return;
	  }


  ZmRef<TestClient::Link> clientLink = new TestClient::Link{&client};
  clientLink->connect(
    Zquic::Host{"localhost"}, server.local().port(), ZiIP{"127.0.0.1"});
  ZuCHECK(client.rxEvent.wait(
    [&clientLink]() { return clientLink->ready(); }, Zm::now(10)),
    "runtime client link did not become ready");
#ifdef Zquic_DEBUG
  ZuCHECK(diag(clientLink).rx.endpointReady == 1 &&
      !diag(clientLink).failures() &&
      !cxnDiag(clientLink).failures(),
    "runtime client link diagnostics mismatch");
#endif
  ZuCHECK(clientLink->server() == "localhost" &&
      clientLink->remote().ip() == ZiIP{"127.0.0.1"},
    "explicit remote preserves TLS server name");
  if (!clientLink->ready()) {
    closeClient(clientLink);
    server.stop();
    clientLink = nullptr;
    server.clearLinks();
    client.final();
    server.final();
    mx.stop();
    return;
  }

  ZmRef<TestServer::Link> serverLink;
  bool established = waitEstablished(clientLink, server, serverLink);
  if (!established) {
    dumpDiag(
      "client", diag(clientLink), clientLink->crypto().diag());
    if (serverLink)
      dumpDiag(
	"server", diag(serverLink), serverLink->crypto().diag());
  }
  ZuCHECK(established, "runtime UDP QUIC handshake did not establish");
  if (!established || !serverLink) {
    closeClient(clientLink);
    server.stop();
    clientLink = nullptr;
    serverLink = nullptr;
    server.clearLinks();
    client.final();
    server.final();
    mx.stop();
    return;
  }
  ZuCHECK(server.acceptedCount == 1, "runtime server accept count mismatch");
  ZuCHECK(clientLink->crypto().oneRTTReady() &&
      serverLink->crypto().oneRTTReady() &&
      clientLink->crypto().negotiatedProtocol() == "h3" &&
      serverLink->crypto().negotiatedProtocol() == "h3",
    "runtime TLS/ALPN state mismatch");
  ZuCHECK(clientLink->connectedCount == 1,
    "runtime client handshake completion callback mismatch");
#ifdef Zquic_DEBUG
  ZuCHECK(diag(clientLink).rx.handshakeComplete == 1,
    "runtime client handshake completion diagnostics mismatch");
  ZuCHECK(diag(serverLink).rx.handshakeComplete == 1,
    "runtime server handshake completion diagnostics mismatch");
  ZuCHECK(diag(clientLink).tx.packetsTx &&
      diag(clientLink).rx.packetsRx &&
      diag(serverLink).tx.packetsTx &&
      diag(serverLink).rx.packetsRx,
    "runtime packet diagnostics mismatch");
  ZuCHECK(diag(clientLink).tx.cryptoBytesTx &&
      diag(clientLink).rx.cryptoBytesRx &&
      diag(serverLink).tx.cryptoBytesTx &&
      diag(serverLink).rx.cryptoBytesRx,
    "runtime CRYPTO byte diagnostics mismatch");
#endif
  auto clientDiag = diag(clientLink);
  if (clientDiag.failures()) {
    dumpDiag("client failures", clientDiag, clientLink->crypto().diag());
    std::cout << "# rx failures=" << uint64_t(clientDiag.rx.failures) <<
      " tx failures=" << uint64_t(clientDiag.tx.failures) << '\n';
  }
  ZuCHECK(!clientDiag.failures(),
    "runtime client failure diagnostics mismatch");
  ZuCHECK(!diag(serverLink).failures(),
    "runtime server failure diagnostics mismatch");

  auto routeCounts = [&server]() {
    return ZmBlock<RouteCounts>{}([&server](auto wake) {
      server.rxRun([wake = ZuMv(wake)]() mutable {
	wake(Zquic_::CxnRouteTrace<TestServerLink>::counts);
      });
    });
  };
  auto routeStart = routeCounts();
  auto clientDuplex = openStream(clientLink, Zquic::StreamType::Duplex);
  auto clientUni = openStream(clientLink, Zquic::StreamType::Simplex);
  ZuCHECK(clientLink->send(clientDuplex, "client-bidi") &&
      clientLink->send(clientUni, "client-uni"),
    "runtime client stream send failed");
  auto serverDuplex = openStream(serverLink, Zquic::StreamType::Duplex);
  auto serverUni = openStream(serverLink, Zquic::StreamType::Simplex);
  ZuCHECK(serverLink->send(serverDuplex, "server-bidi") &&
      serverLink->send(serverUni, "server-uni"),
    "runtime server stream send failed");
  bool streamsArrived = serverLink->waitStreams(2, Zm::now(10)) &&
    clientLink->waitStreams(2, Zm::now(10));
  if (!streamsArrived) {
    dumpDiag(
      "client", diag(clientLink), clientLink->crypto().diag());
    dumpDiag(
      "server", diag(serverLink), serverLink->crypto().diag());
  }
  ZuCHECK(streamsArrived, "runtime protected stream bytes did not arrive");
#ifdef Zquic_DEBUG
  ZuCHECK(diag(clientLink).tx.streamBytesTx == 21 &&
      diag(serverLink).tx.streamBytesTx == 21 &&
      diag(clientLink).tx.packetsTx >= 2 &&
      diag(serverLink).tx.packetsTx >= 2,
    "runtime stream diagnostics mismatch");
#endif
  auto serverRxDuplex = findStream(serverLink, 0);
  auto serverRxUni = findStream(serverLink, 2);
  auto clientRxDuplex = findStream(clientLink, 1);
  auto clientRxUni = findStream(clientLink, 3);
  ZuCHECK(serverRxDuplex && serverRxDuplex->link() == serverLink.ptr() &&
      serverRxDuplex->processed >= 1 &&
      serverRxUni && serverRxUni->link() == serverLink.ptr() &&
      serverRxUni->processed >= 1 &&
      clientRxDuplex && clientRxDuplex->link() == clientLink.ptr() &&
      clientRxDuplex->processed >= 1 &&
      clientRxUni && clientRxUni->link() == clientLink.ptr() &&
      clientRxUni->processed >= 1 &&
      serverLink->streamedCount == 2 &&
      clientLink->streamedCount == 2,
    "runtime stream objects did not process received STREAM frames");
  ZuTestCall(testOwnedStreamAPI, clientLink, serverLink,
    [&client]() { client.pendingMigDrop = 1; }, true);
  ZuTestCall(testOwnedStreamAPI, clientLink, serverLink,
    [&client]() { client.pendingMigDrop = 1; }, false);
  ZuTestCall(testOwnedStreamAPI, serverLink, clientLink,
    [&server]() { server.dropNextShort = 1; }, true);
  ZuTestCall(testOwnedStreamAPI, serverLink, clientLink,
    [&server]() { server.dropNextShort = 1; }, false);


  auto routeEnd = routeCounts();
  unsigned packets = routeEnd.packets - routeStart.packets;
  unsigned probes = routeEnd.probes - routeStart.probes;
  unsigned writes = routeEnd.inserts - routeStart.inserts;
  unsigned walks = routeEnd.walks - routeStart.walks;
  std::cout << "# established server short datagrams=" << packets << " probes=" << probes
    << " route inserts=" << writes << " local CID walks=" << walks << '\n';
  ZuCHECK(packets && probes == packets && !writes && !walks,
    "established ACK/STREAM dispatch must do one probe and zero CID maintenance");

  // A separate Initial binds an original DCID, then collides on its local CID.
  // Wait for the failed link's normal disconnect before checking the live sibling.
  ZmBlock<>{}([serverLink](auto wake) {
    serverLink->app()->rxRun([serverLink, wake = ZuMv(wake)]() mutable {
      serverLink->app()->bootstrapCollision = serverLink.ptr();
      wake();
    });
  });
  Zquic::CxnID original{"failinit"};
  uint8_t initial[Zquic::MinUDPPayload] = {};
  int n = Zquic::Pkt::writeInitial(initial, sizeof(initial), original,
    Zquic::CxnID{"clientid"}, 0, 1);
  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  auto dest = server.local();
  bool sent = fd >= 0 && n > 0 &&
    sendto(fd, initial, sizeof(initial), 0, dest.sa(), dest.len()) == sizeof(initial);
  if (fd >= 0) ::close(fd);
  auto failed = sent ? server.waitLink(1, Zm::now(5)) : nullptr;
  bool drained = failed && failed->waitDisconnected(Zm::now(5));
  bool rollback = ZmBlock<bool>{}([serverLink, original](auto wake) {
    serverLink->app()->rxRun([serverLink, original, wake = ZuMv(wake)]() mutable {
      wake(serverLink->survivedBootstrapCollision(original));
    });
  });
  ZuCHECK(drained && rollback, "bootstrap failure disturbed its sibling or leaked a route");


	ZuCHECK(clientLink->close(), "runtime client disconnect did not complete");
	waitDisconnect(server);
	ZuCHECK(serverLink->peerCloseEvent.wait(
	  [&serverLink]() { return serverLink->closed(); }, Zm::now(10)),
	  "runtime server did not observe client close");
	// The endpoint shards are 3/4, not the multiplexer's default 1/2.
	// Complete expiry before stop can cancel the pending close work.
	waitThread(mx, server.txThread());
	serverLink->forceCloseTimeout();
	ZuCHECK(serverLink->waitDisconnected(Zm::now(10)),
	  "runtime server drain expiry did not disconnect");
	ZuCHECK(server.stop(), "runtime server stop failed");
	ZuCHECK(!server.connected(), "server stop preceded endpoint drain");
	waitDisconnect(server);
	ZuCHECK(!clientLink->cxn() && !clientLink->earlyDisconnects &&
	    clientLink->disconnectedCount == 1 &&
	    !server.connected(),
	  "runtime endpoints remained connected after close");
  waitDisconnect(server);

  serverRxDuplex = nullptr;
  serverRxUni = nullptr;
  clientRxDuplex = nullptr;
  clientRxUni = nullptr;
  clientLink = nullptr;
  serverLink = nullptr;
  server.clearLinks();
  client.final();
  server.final();

#ifdef Zquic_DEBUG
  ZtString<> qlog = readFile(qlogPath.cspan());
  ZuCHECK(qlog, "endpoint runtime qlog output missing");
  ZuCHECK(qlog.find<"\"type\":\"server\"">() >= 0,
    "endpoint runtime qlog missing server vantage point");
  ZuCHECK(qlog.find<"common_fields">() >= 0 &&
      qlog.find<"ODCID">() >= 0 &&
      qlog.find<"group_id">() >= 0 &&
      qlog.find<"DCID">() >= 0 &&
      qlog.find<"SCID">() >= 0,
    "endpoint runtime qlog missing connection linkInfo fields");
  ZuCHECK(qlog.find<"\"ODCID\":\"\"">() < 0 &&
      qlog.find<"\"group_id\":\"\"">() < 0 &&
      qlog.find<"\"DCID\":\"\"">() < 0 &&
      qlog.find<"\"SCID\":\"\"">() < 0,
    "endpoint runtime qlog has empty connection linkInfo");
  ZuCHECK(qlog.find<"quic:stream_state_updated">() >= 0,
    "endpoint runtime qlog missing stream_state_updated");
  ZuCHECK(qlog.find<"quic:udp_datagrams_sent">() >= 0,
    "endpoint runtime qlog missing datagrams_sent");
  ZuCHECK(qlog.find<"quic:packets_acked">() >= 0,
    "endpoint runtime qlog missing packets_acked");
  ZuCHECK(qlog.find<"quic:parameters_set">() >= 0 &&
      qlog.find<"max_udp_payload_size">() >= 0 &&
      qlog.find<"initial_max_data">() >= 0,
    "endpoint runtime qlog missing transport parameters");
  ZuCHECK(qlog.find<"quic:alpn_information">() >= 0 &&
      qlog.find<"\"chosen_alpn\":{\"string_value\":\"h3\"}">() >= 0,
    "endpoint runtime qlog missing ALPN selection");
  ZuCHECK(qlog.find<"packet_number_space">() >= 0,
    "endpoint runtime qlog missing ACK packet_number_space");
  ZuCHECK(qlog.find<"packet_numbers">() >= 0,
    "endpoint runtime qlog missing ACK packet_numbers");
  ZuCHECK(qlog.find<"quic:stream_data_moved">() >= 0,
    "endpoint runtime qlog missing stream_data_moved");
  ZuCHECK(qlog.find<"stream_id">() >= 0,
    "endpoint runtime qlog missing stream_id");
  ZuCHECK(qlog.find<"application">() >= 0 &&
      qlog.find<"transport">() >= 0 &&
      qlog.find<"network">() >= 0,
    "endpoint runtime qlog missing stream data movement locations");
  ZuCHECK(qlog.find<"\"from\":\"transport\"">() >= 0 &&
      qlog.find<"\"to\":\"application\"">() >= 0,
    "endpoint runtime qlog missing transport-to-application movement");
  ZuCHECK(qlog.find<"raw">() >= 0 && qlog.find<"length">() >= 0,
    "endpoint runtime qlog missing stream data raw length");
  ZuCHECK(qlog.find<"client-bidi">() < 0 &&
      qlog.find<"client-uni">() < 0 &&
      qlog.find<"server-bidi">() < 0 &&
      qlog.find<"server-uni">() < 0,
    "endpoint runtime qlog leaked application payload");
  ZuCHECK(qlog.find<"local_open">() >= 0 || qlog.find<"peer_open">() >= 0,
    "endpoint runtime qlog missing stream open reason");
  ZuCHECK(qlog.find<"quic:connection_closed">() >= 0,
    "endpoint runtime qlog missing connection_closed");
  ZuCHECK(qlog.find<"peer_close_frame">() >= 0 ||
      qlog.find<"local_close">() >= 0,
    "endpoint runtime qlog missing close reason");
  ZuCHECK(qlog.find<"drain_expired">() >= 0 &&
      qlog.find<"aborted">() >= 0,
    "endpoint runtime qlog missing close/drain expiry");
#endif

  mx.stop();
}

void testHandshakeCryptoLoss()
{
  ZuTestScopeRT(testHandshakeCryptoLoss);

  TempDir temp;
  ZQUIC_CHECK_RT(temp.init(), "loss runtime temporary TLS certificate failed");

  for (unsigned dropHandshake = 0; dropHandshake < 2; ++dropHandshake) {
    ZiMultiplex mx(
        ZiMxParams()
	  .scheduler([](auto &s) {
	    s.nThreads(5)
	      .thread(1, [](auto &t) { t.isolated(1); })
	      .thread(2, [](auto &t) { t.isolated(1); })
	      .thread(3, [](auto &t) { t.isolated(1); })
	      .thread(4, [](auto &t) { t.isolated(1); }); })
	  .rxThread(1).txThread(2));
    TestServer server;
    server.repeatInitial = true;
    ZQUIC_CHECK_RT(server.init(
	Zquic::ServerParams(&mx, "3", "4")
	  .certPath(temp.certPath.cspan())
	  .keyPath(temp.keyPath.cspan())
	  .maxData(32768)
	  .maxStreamData(8192)
	  .maxStreamsDuplex(8)
	  .maxStreamsSimplex(8)
	  .alpn(ZuSpan<ZuCSpan>{"h3"})),
      "loss runtime server init failed");
    TestClient client;
    ZQUIC_CHECK_RT(client.init(
	Zquic::ClientParams(&mx, "3", "4")
	  .caPath(temp.certPath.cspan())
	  .maxData(32768)
	  .maxStreamData(8192)
	  .maxStreamsDuplex(8)
	  .maxStreamsSimplex(8)
	  .alpn(ZuSpan<ZuCSpan>{"h3"})),
      "loss runtime client init failed");
    bool mxStarted = mx.start();
    ZQUIC_CHECK_RT(mxStarted, "loss runtime multiplexer start failed");
    if (!mxStarted) { client.final(); server.final(); return; }

    ZQUIC_CHECK_RT(server.start(), "loss runtime server listen failed");
    bool listening = server.listening();
    ZQUIC_CHECK_RT(listening, "loss runtime server did not listen");
	    if (!server.listening()) {
	      server.stop();
	      client.final();
	      server.final();
	      mx.stop();
	      continue;
	    }
    if (dropHandshake)
      server.dropNextHandshake = 4;
    else
      server.dropNextInitial = 1;


    ZmRef<TestClient::Link> clientLink = new TestClient::Link{&client};
    clientLink->connect(Zquic::Host{"127.0.0.1"}, server.local().port());

    ZmRef<TestServer::Link> serverLink;
    bool established = waitEstablished(clientLink, server, serverLink);
    if (!established) {
      dumpDiag(
	"client", diag(clientLink), clientLink->crypto().diag());
      if (serverLink)
	dumpDiag(
	  "server", diag(serverLink), serverLink->crypto().diag());
    }
    ZQUIC_CHECK_RT(established, "loss runtime handshake did not recover");
    ZQUIC_CHECK_RT(serverLink && serverLink->repeatedInitials == 1 && server.acceptedCount == 1,
      "repeated Initial did not return the accepted link");
    ZQUIC_CHECK_RT(dropHandshake ? server.droppedHandshake == 4 :
	server.droppedInitial == 1,
      "loss runtime did not drop selected long-header packet");
    ZQUIC_CHECK_RT(dropHandshake ? server.handshakePkts > server.droppedHandshake :
	server.initialPkts > server.droppedInitial,
      "loss runtime did not send selected packet after deliberate loss");
    if (!dropHandshake) {
      ZQUIC_CHECK_RT(server.droppedCoalescedFlight,
	"loss runtime did not drop a coalesced Initial and Handshake flight");
      // The peer has sent no Handshake packet to ACK at the first probe.
      // A padded PING alone has four protected bytes preceding the tag.
      ZQUIC_CHECK_RT(server.firstHandshakeAfterInitialProbe > Zquic::InitialSecret::TagLen + 4,
	"first companion Handshake probe did not carry retained CRYPTO");
    }
    auto ready = [](const auto &link) {
      if (!link) return false;
      return ZmBlock<bool>{}([&link](auto wake) {
	link->app()->rxRun([link, wake = ZuMv(wake)]() mutable {
	  wake(link->crypto().oneRTTReady());
	});
      });
    };
    bool clientReady = ready(clientLink), serverReady = ready(serverLink);
    ZQUIC_CHECK_RT(clientReady && serverReady,
      "loss runtime did not recover application traffic keys");
#ifdef Zquic_DEBUG
    ZQUIC_CHECK_RT(serverLink && diag(serverLink).tx.retransmittedFrames,
      "loss runtime did not retransmit dropped handshake data");
#endif

    bool clientClosed = clientLink->close();
    server.stop();
    ZQUIC_CHECK_RT(clientClosed && !clientLink->cxn() && !server.connected(),
      "loss runtime endpoints remained connected after close");
    waitDisconnect(server);
    clientLink = nullptr;
    serverLink = nullptr;
    client.final();
    server.final();
    mx.stop();
  }
}

void testTLSOutputFailure()
{
  ZuTestScopeRT(testTLSOutputFailure);

  TempDir temp;
  if (!temp.init()) {
    ZQUIC_CHECK_RT(false, "TLS output failure fixture certificate failed");
    return;
  }

  ZiMultiplex mx(
    ZiMxParams().scheduler([](auto &s) { s.nThreads(5); })
      .rxThread(1).txThread(2));
  TestServer server;
  server.tlsOutputOverride = Zquic::plaintextAllocator<
    Zquic::CryptoTxBufAlloc<1, 1, "Zquic.Runtime.ShortTLS">>(&mx);
  bool initialized = server.tlsOutputOverride &&
    server.init(Zquic::ServerParams(&mx, "3", "4")
      .certPath(temp.certPath.cspan()).keyPath(temp.keyPath.cspan())
      .alpn(ZuSpan<ZuCSpan>{"h3"}));
  ZQUIC_CHECK_RT(initialized, "TLS output failure fixture init failed");
  if (!initialized) return;
  bool started = mx.start();
  ZQUIC_CHECK_RT(started, "TLS output failure multiplexer start failed");
  if (!started) { server.final(); return; }

  if (server.start() && server.listening()) {
    Zquic::Crypto client;
    auto hello = server.Base::tlsOutputAllocator()->allocBuf();
    Zquic::CryptoOffsets offsets;
    bool generated = client.initTLS(Zquic::CryptoConfig{.alpn = "h3"}) &&
      client.handleTLSMessage(hello.ptr(), offsets, 0, {}) > 1;
    ZQUIC_CHECK_RT(generated, "TLS output failure fixture ClientHello failed");
    if (generated) for (bool deferred : {false, true}) {
      ZmRef<TestServerLink> link = new TestServerLink{&server};
      bool admitted = ZmBlock<bool>{}([link, hello, deferred](auto wake) {
	link->app()->rxRun([link, hello, deferred, wake = ZuMv(wake)]() mutable {
	  if (!link->initTLSFixture()) { wake(false); return; }
	  if (deferred) {
	    // The queued tail forces the head through CRYPTO dequeue delivery.
	    wake(link->inputCryptoFrame(1, hello->cspan(1)) &&
		 link->inputCryptoFrame(0, ZuBSpan{hello->data(), 1}));
	  } else {
	    wake(!link->inputCryptoFrame(0, hello->cspan()) &&
		 !link->crypto().tlsReady() &&
		 link->crypto().tlsResult() == Ztls_ErrorRawOutput);
	  }
	});
      });
      ZQUIC_CHECK_RT(admitted, "TLS output failure fixture input failed");
      bool closed = admitted && link->waitDisconnected(Zm::now(10));
      ZQUIC_CHECK_RT(closed &&
	  link->closeError() == Zquic::TransportError::InternalError,
	"TLS output failure did not retire the failed handshake");
      bool rejected = ZmBlock<bool>{}([link, hello](auto wake) {
	link->app()->rxRun([link, hello, wake = ZuMv(wake)]() mutable {
	  const auto &crypto = link->crypto();
	  uint64_t handled = crypto.diag().tlsMessagesHandled;
	  wake(handled && !crypto.diag().tlsMessagesEmitted &&
	       !link->tlsFlightBytes && !link->inputTLS(0, hello->cspan()) &&
	       handled == crypto.diag().tlsMessagesHandled);
	});
      });
      ZQUIC_CHECK_RT(rejected && !diag(link).tx.cryptoBytesTx,
	"failed TLS output was published or TLS resumed");
      if (!closed) {
	link->disconnect();
	(void)link->waitDisconnected(Zm::now(10));
      }
      waitThread(mx, server.rxThread());
    }
  } else {
    ZQUIC_CHECK_RT(false, "TLS output failure fixture could not listen");
  }
  server.stop();
  waitDisconnect(server);
  server.final();
  mx.stop();
}

template <typename Link>
unsigned handshakeKeysLive(const ZmRef<Link> &link)
{
  return ZmBlock<unsigned>{}([&link](auto wake) {
    link->app()->rxRun([link, wake = ZuMv(wake)]() mutable {
      unsigned rx = link->handshakeRxKeysLive();
      link->app()->txRun([link, rx, wake = ZuMv(wake)]() mutable {
	wake(rx | (unsigned(link->handshakeTxKeysLive()) << 1));
      });
    });
  });
}

template <typename Link>
bool staleKeysStayRetired(const ZmRef<Link> &link)
{
  return ZmBlock<bool>{}([&link](auto wake) {
    link->app()->txRun([link, wake = ZuMv(wake)]() mutable {
      wake(link->staleKeysStayRetired());
    });
  });
}

void testHandshakeKeyRetirement()
{
  ZuTestScopeRT(testHandshakeKeyRetirement);

  TempDir temp;
  ZQUIC_CHECK_RT(temp.init(), "key retirement TLS certificate failed");
  ZiMultiplex mx(
    ZiMxParams().scheduler([](auto &s) {
      s.nThreads(5)
	.thread(1, [](auto &t) { t.isolated(1); })
	.thread(2, [](auto &t) { t.isolated(1); })
	.thread(3, [](auto &t) { t.isolated(1); })
	.thread(4, [](auto &t) { t.isolated(1); });
    }).rxThread(1).txThread(2));
  TestServer server;
  ZQUIC_CHECK_RT(server.init(
      Zquic::ServerParams(&mx, "3", "4")
	.certPath(temp.certPath.cspan())
	.keyPath(temp.keyPath.cspan())
	.maxData(32768).maxStreamData(8192).maxStreamsDuplex(8)
	.alpn(ZuSpan<ZuCSpan>{"h3"})),
    "key retirement server init failed");
  TestClient client;
  ZQUIC_CHECK_RT(client.init(
      Zquic::ClientParams(&mx, "3", "4")
	.caPath(temp.certPath.cspan())
	.maxData(32768).maxStreamData(8192).maxStreamsDuplex(8)
	.alpn(ZuSpan<ZuCSpan>{"h3"})),
    "key retirement client init failed");
  bool started = mx.start();
  ZQUIC_CHECK_RT(started, "key retirement mx start failed");
  if (!started) { client.final(); server.final(); return; }
  ZQUIC_CHECK_RT(server.start() && server.listening(),
    "key retirement server listen failed");
  if (!server.listening()) {
    server.stop(); client.final(); server.final(); mx.stop(); return;
  }

  server.holdHandshakeDone = true;
  ZmRef<TestClient::Link> clientLink = new TestClient::Link{&client};
  clientLink->connect(Zquic::Host{"127.0.0.1"}, server.local().port());
  ZmRef<TestServer::Link> serverLink;
  bool established = waitEstablished(clientLink, server, serverLink);
  ZQUIC_CHECK_RT(established, "key retirement handshake failed");
  if (established) {
    ZQUIC_CHECK_RT(server.handshakeDoneEvent.wait(
      [&server]() { return bool(server.heldHandshakeDone); }, Zm::now(10)),
      "key retirement did not hold HANDSHAKE_DONE");
    waitThread(mx, server.rxThread());
    ZQUIC_CHECK_RT(!handshakeKeysLive(serverLink),
      "server retained Handshake keys pending HANDSHAKE_DONE acknowledgment");
    ZQUIC_CHECK_RT(handshakeKeysLive(clientLink) == 3,
      "client discarded Handshake keys before confirmation");
    ZQUIC_CHECK_RT(staleKeysStayRetired(serverLink),
      "stale TLS snapshot restored retired server Handshake keys");

    server.holdHandshakeDone = false;
    auto stream = openStream(serverLink);
    ZQUIC_CHECK_RT(stream && serverLink->send(stream, "confirm"),
      "key retirement confirmation flush failed");
    ZQUIC_CHECK_RT(clientLink->waitStreams(1, Zm::now(10)),
      "key retirement protected response did not arrive");
    ZQUIC_CHECK_RT(client.rxEvent.wait([&clientLink] {
      return !handshakeKeysLive(clientLink);
    }, Zm::now(10)), "client retained Handshake keys after HANDSHAKE_DONE");
    ZQUIC_CHECK_RT(staleKeysStayRetired(clientLink),
      "stale TLS snapshot restored retired client Handshake keys");

    bool rejected = ZmBlock<bool>{}([clientLink, serverLink](auto wake) {
      serverLink->app()->rxRun([clientLink, serverLink, wake = ZuMv(wake)]() mutable {
	const uint8_t keyUpdate[] = {Ztls_HsTypeKeyUpdate, 0, 0, 1, 0};
	wake(!serverLink->inputTLS(3, keyUpdate, clientLink->local()) &&
	     !serverLink->crypto().tlsReady() && !serverLink->tlsFlightBytes);
      });
    });
    ZQUIC_CHECK_RT(rejected, "QUIC accepted a TLS KeyUpdate");
    ZQUIC_CHECK_RT(client.rxEvent.wait([clientLink] {
      return clientLink->closeError() == 0x010a;
    }, Zm::now(10)), "peer did not receive QUIC crypto error 0x010a");
  }
  bool closed = clientLink->close();
  server.stop();
  ZQUIC_CHECK_RT(closed && !server.connected(),
    "key retirement endpoints remained connected");
  waitDisconnect(server);
  clientLink = nullptr;
  serverLink = nullptr;
  server.clearLinks();
  client.final();
  server.final();
  mx.stop();
}

void testMigrationLoss()
{
  ZuTestScopeRT(testMigrationLoss);

  TempDir temp;
  bool cert = temp.init();
  ZQUIC_CHECK_RT(cert, "migration loss certificate failed");
  if (!cert) return;
  ZtString<> serverQLog = temp.pathOf("migration-server.sqlog");
  ZtString<> clientQLog = temp.pathOf("migration-client.sqlog");

  ZiMultiplex mx{
    ZiMxParams()
      .scheduler([](auto &s) { s.nThreads(5); })
      .rxThread(1).txThread(2)};
  TestServer server;
  server.issuanceCollisions = true;
  bool serverInit = server.init(
    Zquic::ServerParams(&mx, "3", "4")
      .certPath(temp.certPath.cspan()).keyPath(temp.keyPath.cspan())
      .qlog(true).qlogPath(serverQLog.cspan()).qlogRingSize(1<<16)
      .migrationMode(Zquic::MigrationMode::Active).migCIDRes(2)
      .maxData(32768).maxStreamData(8192).maxStreamsDuplex(8)
      .alpn(ZuSpan<ZuCSpan>{"h3"}));
  ZQUIC_CHECK_RT(serverInit, "migration loss server init failed");
  if (!serverInit) { mx.stop(); return; }
  TestClient client;
  client.dropMigChallenge = true; // immutable before init and publication
  bool clientInit = client.init(
    Zquic::ClientParams(&mx, "3", "4")
      .caPath(temp.certPath.cspan())
      .qlog(true).qlogPath(clientQLog.cspan()).qlogRingSize(1<<16)
      .migrationMode(Zquic::MigrationMode::Active).migCIDRes(2)
      .maxData(32768).maxStreamData(8192).maxStreamsDuplex(8)
      .alpn(ZuSpan<ZuCSpan>{"h3"}));
  ZQUIC_CHECK_RT(clientInit, "migration loss client init failed");
  bool started = mx.start();
  ZQUIC_CHECK_RT(started, "migration loss multiplexer start failed");
  if (!started) { client.final(); server.final(); return; }

  bool serverStart = server.start();
  ZQUIC_CHECK_RT(serverStart, "migration loss server start failed");


  ZmRef<TestClient::Link> clientLink;
  ZmRef<TestServer::Link> serverLink;
  auto exercise = [&client, &server, &clientLink, &serverLink, serverStart, clientInit]() {
    if (!serverStart || !clientInit) return;
    bool listening = server.listening(); // start() completed the engine startup
    ZQUIC_CHECK_RT(listening, "migration loss server did not listen");
    if (!listening) return;
    clientLink = new TestClient::Link{&client};
    clientLink->connect(Zquic::Host{"127.0.0.1"}, server.local().port());
    auto accepted = [&server]() {
      return ZmBlock<ZmRef<TestServer::Link>>{}([&server](auto wake) {
	server.rxRun([&server, wake = ZuMv(wake)]() mutable {
	  wake(server.link());
	});
      });
    };
    bool established = waitEstablished(clientLink, server, serverLink);
    ZQUIC_CHECK_RT(established, "migration loss UDP handshake failed");
    if (!established) return;
    Zquic_::LinkCID second;
    bool issued = ZmBlock<bool>{}([serverLink, &second](auto wake) {
      serverLink->app()->rxRun([serverLink, &second, wake = ZuMv(wake)]() mutable {
	wake(serverLink->retryIssuance(second));
      });
    });
    ZQUIC_CHECK_RT(issued, "failed bind changed sequence, committed routes or ready count");
    ZQUIC_CHECK_RT(issued && client.rxEvent.wait(
      [clientLink, second] { return clientLink->receivedCID(second); }, Zm::now(10)),
      "replacement CID and token did not reach the peer");
    bool spare = client.rxEvent.wait(
      [&clientLink]() { return clientLink->spareCID(); }, Zm::now(10));
    ZQUIC_CHECK_RT(spare, "migration loss peer did not supply a spare CID");
    if (!spare) return;
    auto state = [&clientLink]() {
      return ZmBlock<Zquic::MigrationResult>{}([&clientLink](auto wake) {
	clientLink->migrationState(ZuMv(wake));
      });
    };
    auto before = state();
    ZQUIC_CHECK_RT(before.active.local.port(),
      "migration loss initial UDP port is empty");
    bool requested = ZmBlock<bool>{}([&client, &clientLink](auto wake) {
      client.txRun([clientLink, wake = ZuMv(wake)]() mutable {
	wake(clientLink->migrateLocal(ZiIP{"127.0.0.1"}));
      });
    });
    ZQUIC_CHECK_RT(requested, "migration loss local rebind was rejected");
    Zquic::MigrationResult after;
    bool promoted = clientLink->migrationEvent.wait([&after, &clientLink]() {
      after = ZmBlock<Zquic::MigrationResult>{}([&clientLink](auto wake) {
	clientLink->app()->txRun([clientLink, wake = ZuMv(wake)]() mutable {
	  wake(clientLink->promotion);
	});
      });
      return after.state == Zquic::MigrationState::Promoted;
    }, Zm::now(10));
    ZQUIC_CHECK_RT(client.migChallengeDrops == 1,
      "migration loss did not withhold exactly one candidate challenge");
    ZQUIC_CHECK_RT(promoted && after.success && after.active.validated &&
	after.active.local.port() &&
	after.active.local.port() != before.active.local.port() &&
	after.active.remote == before.active.remote,
      "migration loss did not validate the rebound UDP path");
    ZQUIC_CHECK_RT(server.acceptedCount == 1 &&
	clientLink->connectedCount == 1 && accepted() == serverLink,
      "migration loss replaced the established QUIC connection");
    if (!promoted) return;
    bool serverPromoted = serverLink->migrationEvent.wait([&serverLink, &after]() {
      auto state = ZmBlock<Zquic::MigrationResult>{}([&serverLink](auto wake) {
	serverLink->migrationState(ZuMv(wake));
      });
      return state.active.validated &&
	state.active.remote.port() == after.active.local.port();
    }, Zm::now(10));
    ZQUIC_CHECK_RT(serverPromoted,
      "migration loss server did not validate its own candidate challenge");
    if (!serverPromoted) return;

    auto stream = openStream(serverLink);
    ZQUIC_CHECK_RT(stream, "migration loss stream open failed");
    if (!stream) return;
    constexpr auto payload = "migration-loss-recovered"_z;
    bool sent = ZmBlock<bool>{}([&server, stream, payload](auto wake) {
      server.txRun([&server, stream, payload, wake = ZuMv(wake)]() mutable {
	server.dropNextShort = 1;
	auto tx = stream->txStream_();
	tx << payload << Zi::flush();
	bool ok = !tx.failed();
	stream->fin();
	wake(ok);
      });
    });
    ZQUIC_CHECK_RT(sent, "migration loss direct stream serialization failed");
    auto id = stream->id();
    bool received = clientLink->streamEvent.wait([&client, &clientLink, id, payload]() {
      auto incoming = findStream(clientLink, id);
      if (!incoming) return false;
      return ZmBlock<bool>{}([&client, incoming, payload](auto wake) {
	client.rxRun([incoming, payload, wake = ZuMv(wake)]() mutable {
	  auto &rx = incoming->rxStream();
	  unsigned offset = 0;
	  bool ok = rx.length() == payload.length();
	  if (ok) {
	    auto n = rx.each(payload.length(), [&offset, payload](auto part) -> int64_t {
	      if (memcmp(part.data(), payload.data() + offset, part.length()))
		return -1;
	      offset += part.length();
	      return part.length();
	    });
	    ok = n == int64_t(payload.length());
	  }
	  wake(ok);
	});
      });
    }, Zm::now(10));
    ZQUIC_CHECK_RT(received,
      "migration loss changed or failed to deliver protected stream bytes");
    ZQUIC_CHECK_RT(server.droppedShort == 1,
      "migration loss did not exercise the additional server short drop");
  };
  exercise();

  if (clientLink) closeClient(clientLink);
  if (serverStart) {
    ZQUIC_CHECK_RT(server.stop(), "migration loss server stop failed");
    waitDisconnect(server);
  }
  ZQUIC_CHECK_RT(!clientLink || (!clientLink->cxn() &&
      !clientLink->earlyDisconnects && clientLink->disconnectedCount == 1),
    "migration loss client completion preceded physical retirement");
  clientLink = nullptr;
  serverLink = nullptr;
  server.clearLinks();
  if (clientInit) client.final();
  server.final();
#ifdef Zquic_DEBUG
  auto sizes = challengeSizes(readFile(clientQLog.cspan()));
  ZQUIC_CHECK_RT(sizes.p<0>() >= 2 && sizes.p<1>(),
    "initial and recovered active PATH_CHALLENGE packets must each reach 1200 bytes; "
    "count=", sizes.p<0>(), " valid=", sizes.p<1>());
#endif
  mx.stop();
}

void testPreferredBindings(bool v6, bool differentIP, bool keepTuple)
{
  ZuTestScopeRT(testPreferredBindings);
  TempDir temp;
  bool cert = temp.init();
  ZQUIC_CHECK_RT(cert, "preferred listener certificate generation failed");
  if (!cert) return;
  ZiMultiplex mx{ZiMxParams().scheduler([](auto &s) { s.nThreads(5); })
    .rxThread(1).txThread(2)};
  TestServer server;
  ZiSockAddr original{ZiIP{v6 ? "::1" : "127.0.0.1"},
    uint16_t(v6 ? ZquicTestPort::Original6 : ZquicTestPort::Original4)};
  ZiSockAddr preferred{ZiIP{v6 ? "::1" : differentIP ? "127.0.0.2" : "127.0.0.1"},
    uint16_t(differentIP ? original.port() :
      v6 ? ZquicTestPort::Preferred6 : ZquicTestPort::Preferred4)};
  if (v6) { server.addrs.original6 = original; server.addrs.preferred6 = preferred; }
  else { server.addrs.original4 = original; server.addrs.preferred4 = preferred; }
  bool serverInit = server.init(Zquic::ServerParams{&mx, "3", "4"}
    .certPath(temp.certPath.cspan()).keyPath(temp.keyPath.cspan())
    .migrationMode(Zquic::MigrationMode::Disabled).migCIDRes(0)
    .maxData(32768).maxStreamData(8192).maxStreamsDuplex(8)
    .alpn(ZuSpan<ZuCSpan>{"h3"}));
  ZQUIC_CHECK_RT(serverInit, "preferred server initialization failed");
  if (!serverInit) return;
  TestClient client, stationary;
#ifdef linux
  reuseTuple.key = 0;
  reuseTuple.binds = 0;
#endif
  client.keepMigTuple = keepTuple;
  client.holdMigCID = true;
  bool clientInit = client.init(Zquic::ClientParams{&mx, "3", "4"}
    .caPath(temp.certPath.cspan()).migrationMode(Zquic::MigrationMode::Active)
    .migCIDRes(2).maxData(32768).maxStreamData(8192).maxStreamsDuplex(8)
    .alpn(ZuSpan<ZuCSpan>{"h3"}));
  bool stationaryInit = stationary.init(Zquic::ClientParams{&mx, "3", "4"}
    .caPath(temp.certPath.cspan()).migrationMode(Zquic::MigrationMode::Disabled)
    .maxData(32768).maxStreamData(8192).maxStreamsDuplex(8)
    .alpn(ZuSpan<ZuCSpan>{"h3"}));
  ZQUIC_CHECK_RT(clientInit && stationaryInit, "preferred client initialization failed");
  bool mxStarted = mx.start();
  ZQUIC_CHECK_RT(mxStarted, "preferred multiplexer start failed");
  if (!mxStarted) { client.final(); stationary.final(); server.final(); return; }
  bool ready = server.start();
  ZQUIC_CHECK_RT(ready, "preferred listeners did not become ready");
  unsigned heldQueued = 1;
  auto originalTarget = server.pktAllocTarget(original);
  Zquic_::EndpointTxGate heldGate{&mx, originalTarget.physical,
    originalTarget.generation, 1, [&heldQueued] { return heldQueued; }};
  ZmRef<TestClient::Link> moving, fixed;
  ZmRef<TestServer::Link> movingPeer, fixedPeer;
  auto exercise = [&server, &client, &stationary, &moving, &fixed, &movingPeer,
      &fixedPeer, &heldGate, &mx, ready, clientInit, stationaryInit, original, preferred,
      v6, keepTuple]() {
    if (!ready || !clientInit || !stationaryInit) return;
    fixed = new TestClient::Link{&stationary};
    fixed->connect(Zquic::Host{v6 ? "::1" : "127.0.0.1"}, original.port());
    bool fixedOK = waitEstablished(fixed, server, fixedPeer);
    ZQUIC_CHECK_RT(fixedOK, "stationary client handshake failed");
    if (!fixedOK) return;
    moving = new TestClient::Link{&client};
    moving->connect(Zquic::Host{v6 ? "::1" : "127.0.0.1"}, original.port());
    bool movingOK = waitEstablished(moving, server, movingPeer, 1);
    ZQUIC_CHECK_RT(movingOK, "moving client handshake failed");
    if (!movingOK) return;
    bool heldClient = moving->migrationEvent.wait([moving]() {
      return ZmBlock<bool>{}([moving](auto wake) {
	moving->app()->txRun([moving, wake = ZuMv(wake)]() mutable {
	  wake(moving->migCIDHeld);
	});
      });
    }, Zm::now(10));
    ZQUIC_CHECK_RT(heldClient, "client preferred migration did not reach fixture checkpoint");
    if (!heldClient) return;
    bool armed = ZmBlock<bool>{}([movingPeer, &heldGate](auto wake) {
      movingPeer->app()->txRun([movingPeer, &heldGate, wake = ZuMv(wake)]() mutable {
	wake(movingPeer->armHeldGate(&heldGate));
      });
    });
    bool waiting = ZmBlock<bool>{}([&mx, &heldGate](auto wake) {
      mx.txRun([&heldGate, wake = ZuMv(wake)]() mutable {
	wake(heldGate.waiting() == 1 && !heldGate.reservations() && !heldGate.available());
      });
    });
    waitThread(mx, movingPeer->app()->txThread());
    ZQUIC_CHECK_RT(armed && waiting,
      "original endpoint did not retain the protocol capacity waiter before migration");
    if (!armed || !waiting) return;
    ZmBlock<>{}([moving](auto wake) {
      moving->app()->txRun([moving, wake = ZuMv(wake)]() mutable {
	moving->releaseHeldMigration();
	wake();
      });
    });
    auto advertisement = [](const auto &link) {
      return ZmBlock<Zquic::PreferredAddress>{}([link](auto wake) {
	link->app()->rxRun([link, wake = ZuMv(wake)]() mutable {
	  wake(link->peerPreferred());
	});
      });
    };
    auto a = advertisement(moving), b = advertisement(fixed);
    ZQUIC_CHECK_RT(a && b && a.select(original.ip()) == preferred &&
      a.cid != b.cid && a.resetToken != b.resetToken,
      "TLS preferred endpoints or per-connection CID/token isolation failed");
    bool routed = ZmBlock<bool>{}([movingPeer, a](auto wake) {
      movingPeer->app()->rxRun([movingPeer, a, wake = ZuMv(wake)]() mutable {
	wake(movingPeer->preferredRouted(a));
      });
    });
    ZQUIC_CHECK_RT(routed, "advertised sequence-1 CID was not registered on the same link");
    Zquic::MigrationResult moved;
    bool promoted = moving->migrationEvent.wait([&moved, moving]() {
      moved = ZmBlock<Zquic::MigrationResult>{}([moving](auto wake) {
	moving->app()->txRun([moving, wake = ZuMv(wake)]() mutable {
	  wake(moving->promotion);
	});
      });
      return moved.state == Zquic::MigrationState::Promoted;
    }, Zm::now(10));
    if (!promoted || moved.active.remote != preferred) {
      auto current = ZmBlock<Zquic::MigrationResult>{}([moving](auto wake) {
	moving->migrationState(ZuMv(wake));
      });
      auto failure = ZmBlock<Zquic::MigrationResult>{}([moving](auto wake) {
	moving->app()->txRun([moving, wake = ZuMv(wake)]() mutable {
	  wake(moving->migrationFailure);
	});
      });
      auto endpoints = cxnDiag(moving);
      log_("preferred fixture: v6=", v6, " differentIP=", preferred.ip() != original.ip(),
	" keepTuple=", keepTuple, " callbackState=", int(moved.state),
	" currentState=", int(current.state), " reason=", int(current.reason),
	" failureState=", int(failure.state), " failureReason=", int(failure.reason),
	" activePort=", current.active.remote.port(),
	" candidatePort=", current.candidate.remote.port(),
	" endpointFailures=", endpoints.failures());
    }
    ZQUIC_CHECK_RT(promoted && moved.active.remote == preferred,
      "client did not validate the advertised preferred endpoint");
#ifdef linux
    ZQUIC_CHECK_RT(!keepTuple || reuseTuple.binds == 1,
      "unchanged-tuple fixture did not bind its exact retained source tuple");
#endif
    if (!promoted) return;
    ZQUIC_CHECK_RT(!keepTuple || moved.active.local == movingPeer->originalPeer,
      "preferred fixture changed the client's original source tuple");
    // Non-probing traffic qualifies the server's already validated candidate.
    // Exercise retained payload/FIN with one dropped protected datagram.
    ZuTestCall(testOwnedStreamAPI, moving, movingPeer,
      [&client]() { client.pendingMigDrop = 1; }, false);
    bool serverPromoted = movingPeer->migrationEvent.wait([movingPeer, preferred]() {
      auto state = ZmBlock<Zquic::MigrationResult>{}([movingPeer](auto wake) {
	movingPeer->migrationState(ZuMv(wake));
      });
      return state.active.local == preferred && state.active.validated;
    }, Zm::now(10));
    ZQUIC_CHECK_RT(serverPromoted,
      "server did not promote after its proof and qualifying input");
    if (!serverPromoted) return;
    bool replaced = ZmBlock<bool>{}([fixedPeer](auto wake) {
      fixedPeer->app()->rxRun([fixedPeer, wake = ZuMv(wake)]() mutable {
	wake(fixedPeer->retireUnusedPreferred());
      });
    });
    ZQUIC_CHECK_RT(replaced,
      "disabled server with zero spare reserve failed to route the preferred CID replacement");
    // Retirement on the stationary connection must not affect its migrated peer.
    ZuTestCall(testOwnedStreamAPI, movingPeer, moving,
      [&server]() { server.dropNextShort = 1; }, false);
    bool released = ZmBlock<bool>{}([movingPeer](auto wake) {
      movingPeer->app()->txRun([movingPeer, wake = ZuMv(wake)]() mutable {
	wake(!movingPeer->heldWaitBusy());
      });
    });
    bool emptyGate = ZmBlock<bool>{}([&mx, &heldGate](auto wake) {
      mx.txRun([&heldGate, wake = ZuMv(wake)]() mutable {
	wake(!heldGate.waiting() && !heldGate.reservations() && !heldGate.available());
      });
    });
    ZQUIC_CHECK_RT(released && emptyGate,
      "preferred promotion did not cancel the old-target waiter with owner acknowledgement");
    // Closing a migrated link must leave shared original listeners available.
    closeClient(moving);
    waitDisconnect(server);
    ZQUIC_CHECK_RT(server.listening() && fixed->established() &&
      server.acceptedCount == 2 && moving->connectedCount == 1 && fixed->connectedCount == 1,
      "preferred migration or disconnect replaced a connection/shared listener");
    ZuTestCall(testOwnedStreamAPI, fixed, fixedPeer,
      [&stationary]() { stationary.pendingMigDrop = 1; }, false);
    auto fixedPath = ZmBlock<Zquic::MigrationResult>{}([fixedPeer](auto wake) {
      fixedPeer->migrationState(ZuMv(wake));
    });
    ZQUIC_CHECK_RT(fixedPath.active.local == original,
      "disabled stationary client left its original server binding");
  };
  exercise();
  if (moving && !moving->closed()) closeClient(moving);
  if (fixed) closeClient(fixed);
  if (ready) server.stop();
  waitDisconnect(server);
  ZmBlock<>{}([&mx, &heldGate](auto wake) {
    mx.txRun([&heldGate, wake = ZuMv(wake)]() mutable { heldGate.close(ZuMv(wake)); });
  });
  moving = nullptr; fixed = nullptr; movingPeer = nullptr; fixedPeer = nullptr;
  server.clearLinks();
  client.final(); stationary.final(); server.final();
  mx.stop();
#ifdef linux
  reuseTuple.key = 0;
  reuseTuple.originalFD = -1;
#endif
}

void testPreferredStop(bool atCommit)
{
  ZuTestScopeRT(testPreferredStop);
  TempDir temp;
  bool cert = temp.init();
  ZQUIC_CHECK_RT(cert, "preferred shutdown certificate generation failed");
  if (!cert) return;
  ZiMultiplex mx{ZiMxParams().scheduler([](auto &s) { s.nThreads(5); })
    .rxThread(1).txThread(2)};
  TestServer server;
  server.addrs = {
    {ZiIP{"127.0.0.1"}, ZquicTestPort::Original4},
    {ZiIP{"127.0.0.1"}, ZquicTestPort::Preferred4},
    {ZiIP{"::1"}, ZquicTestPort::Original6},
    {ZiIP{"::1"}, ZquicTestPort::Preferred6}};
  server.stopOnMigration = atCommit ? 2 : 1;
  TestClient movingApp, fixedApp;
  movingApp.holdMigCID = true;
  bool serverInit = server.init(Zquic::ServerParams{&mx, "3", "4"}
    .certPath(temp.certPath.cspan()).keyPath(temp.keyPath.cspan())
    .migrationMode(Zquic::MigrationMode::Disabled).migCIDRes(0)
    .alpn(ZuSpan<ZuCSpan>{"h3"}));
  bool movingInit = movingApp.init(Zquic::ClientParams{&mx, "3", "4"}
    .caPath(temp.certPath.cspan()).migrationMode(Zquic::MigrationMode::Active)
    .migCIDRes(2).alpn(ZuSpan<ZuCSpan>{"h3"}));
  bool fixedInit = fixedApp.init(Zquic::ClientParams{&mx, "3", "4"}
    .caPath(temp.certPath.cspan()).migrationMode(Zquic::MigrationMode::Disabled)
    .alpn(ZuSpan<ZuCSpan>{"h3"}));
  bool started = mx.start();
  ZQUIC_CHECK_RT(serverInit && movingInit && fixedInit && started,
    "preferred shutdown fixture initialization failed");
  ZmRef<TestClient::Link> moving, fixed, fresh4, fresh6;
  ZmRef<TestServer::Link> movingPeer, fixedPeer, freshPeer4, freshPeer6;
  auto exercise = [&server, &movingApp, &fixedApp, &moving, &fixed, &fresh4, &fresh6,
    &movingPeer, &fixedPeer, &freshPeer4, &freshPeer6,
    serverInit, movingInit, fixedInit, started, atCommit]() {
    if (!serverInit || !movingInit || !fixedInit || !started) return;
    bool ready = server.start();
    ZQUIC_CHECK_RT(ready, "preferred shutdown listeners did not start");
    if (!ready) return;
    unsigned generation = server.endpointGeneration();
    fixed = new TestClient::Link{&fixedApp};
    fixed->connect(Zquic::Host{"::1"}, server.addrs.original6.port());
    bool fixedConnected = waitEstablished(fixed, server, fixedPeer);
    ZQUIC_CHECK_RT(fixedConnected, "preferred shutdown stationary client did not establish");
    if (!fixedConnected) return;
    moving = new TestClient::Link{&movingApp};
    moving->connect(Zquic::Host{"127.0.0.1"}, server.addrs.original4.port());
    bool connected = waitEstablished(moving, server, movingPeer, 1);
    ZQUIC_CHECK_RT(connected, "preferred shutdown clients did not establish");
    if (!connected) return;
    ZuTestCall(testOwnedStreamAPI, fixed, fixedPeer, [] { }, false);
    bool held = moving->migrationEvent.wait([moving]() {
      return ZmBlock<bool>{}([moving](auto wake) {
	moving->app()->txRun([moving, wake = ZuMv(wake)]() mutable {
	  wake(moving->migCIDHeld);
	});
      });
    }, Zm::now(10));
    ZQUIC_CHECK_RT(held, "preferred shutdown did not hold client migration");
    if (!held) return;
    ZmBlock<>{}([moving](auto wake) {
      moving->app()->txRun([moving, wake = ZuMv(wake)]() mutable {
	moving->releaseHeldMigration();
	wake();
      });
    });
    if (atCommit) {
      bool promoted = moving->migrationEvent.wait([moving]() {
	return ZmBlock<bool>{}([moving](auto wake) {
	  moving->app()->txRun([moving, wake = ZuMv(wake)]() mutable {
	    wake(moving->promotion.state == Zquic::MigrationState::Promoted);
	  });
	});
      }, Zm::now(10));
      ZQUIC_CHECK_RT(promoted, "preferred shutdown client failed to validate");
      if (!promoted) return;
      // Fresh non-probing input completes the server's promotion conditions;
      // an earlier ACK-only packet may already have completed them. The
      // fixture holds the existing endpoint/CID commit continuation either way.
      auto stream = openStream(moving);
      if (stream) (void)moving->send(stream, "shutdown");
    }
    Zquic::MigrationResult checkpoint;
    bool stoppedAtPath = movingPeer->migrationEvent.wait([&checkpoint, movingPeer]() {
      checkpoint = ZmBlock<Zquic::MigrationResult>{}([movingPeer](auto wake) {
	movingPeer->app()->txRun([movingPeer, wake = ZuMv(wake)]() mutable {
	  wake(movingPeer->migrationStop);
	});
      });
      return checkpoint.state != Zquic::MigrationState::Idle;
    }, Zm::now(10));
    ZQUIC_CHECK_RT(stoppedAtPath && checkpoint.state == (atCommit ?
      Zquic::MigrationState::Promoted : Zquic::MigrationState::Validating) &&
      checkpoint.active.local == server.addrs.original4 &&
      checkpoint.candidate.local == server.addrs.preferred4,
      "server shutdown missed the outstanding preferred candidate");
    ZQUIC_CHECK_RT(server.stop(), "preferred candidate shutdown did not complete");
    waitDisconnect(server);
    bool drained = !server.listening() && !server.connected() &&
      movingPeer->disconnectedCount == 1 && fixedPeer->disconnectedCount == 1;
    for (unsigned slot = 0; slot < 4; ++slot)
      drained &= !server.pktAllocTarget(slot, generation).physical;
    ZQUIC_CHECK_RT(drained, "candidate shutdown left a shared listener or link alive");
    closeClient(moving);
    closeClient(fixed);
    server.clearLinks();
    server.stopOnMigration = 0;
    ready = server.start();
    ZQUIC_CHECK_RT(ready && server.endpointCount() == 4 &&
      server.endpointGeneration() != generation,
      "preferred candidate shutdown did not permit four-listener restart");
    if (!ready) return;
    // Replay a delayed old-runtime commit only after replacement listeners
    // are ready. The old link remains owned here until this callback drains.
    ZmBlock<>{}([movingPeer](auto wake) {
      movingPeer->app()->txRun([movingPeer, wake = ZuMv(wake)]() mutable {
	auto commit = ZuMv(movingPeer->heldMigrationCommit);
	if (commit) commit(true);
	wake();
      });
    });
    waitDisconnect(server);
    bool noPromotion = ZmBlock<bool>{}([movingPeer](auto wake) {
      movingPeer->app()->txRun([movingPeer, wake = ZuMv(wake)]() mutable {
	wake(!movingPeer->heldMigrationCommit && !movingPeer->migrationPromotions);
      });
    });
    fresh4 = new TestClient::Link{&fixedApp};
    fresh6 = new TestClient::Link{&fixedApp};
    fresh4->connect(Zquic::Host{"127.0.0.1"}, server.addrs.original4.port());
    bool connected4 = waitEstablished(fresh4, server, freshPeer4, 2);
    fresh6->connect(Zquic::Host{"::1"}, server.addrs.original6.port());
    bool connected6 = waitEstablished(fresh6, server, freshPeer6, 3);
    ZQUIC_CHECK_RT(connected4 && connected6 && server.acceptedCount == 4 &&
      server.listening() && noPromotion,
      "delayed candidate work disrupted replacement listeners or connection routing");
    if (connected4) ZuTestCall(testOwnedStreamAPI, fresh4, freshPeer4, [] { }, false);
    if (connected6) ZuTestCall(testOwnedStreamAPI, fresh6, freshPeer6, [] { }, false);
  };
  exercise();
  if (started) {
    if (moving) closeClient(moving);
    if (fixed) closeClient(fixed);
    if (fresh4) closeClient(fresh4);
    if (fresh6) closeClient(fresh6);
    if (serverInit) server.stop();
    waitDisconnect(server);
  }
  moving = nullptr; fixed = nullptr; fresh4 = nullptr; fresh6 = nullptr;
  movingPeer = nullptr; fixedPeer = nullptr; freshPeer4 = nullptr; freshPeer6 = nullptr;
  server.clearLinks();
  movingApp.final(); fixedApp.final(); server.final();
  if (started) mx.stop();
}

void testSrvMultiCxn()
{
  ZuTestScope(testSrvMultiCxn);

  TempDir temp;
  ZuCHECK(temp.init(), "multi runtime temporary TLS certificate failed");

  ZiMultiplex mx(
      ZiMxParams()
	.scheduler([](auto &s) {
	  s.nThreads(5)
	    .thread(1, [](auto &t) { t.isolated(1); })
	    .thread(2, [](auto &t) { t.isolated(1); })
	    .thread(3, [](auto &t) { t.isolated(1); })
	    .thread(4, [](auto &t) { t.isolated(1); }); })
	.rxThread(1).txThread(2));

  ZmAtomic<unsigned> serverErrors = 0;
  TestServer server;
  ZuCHECK(server.init(
      Zquic::ServerParams(&mx, "3", "4")
	.certPath(temp.certPath.cspan())
	.keyPath(temp.keyPath.cspan())
	.maxData(32768)
	.maxStreamData(8192)
	.maxStreamsDuplex(8)
	.maxStreamsSimplex(8)
	.alpn(ZuSpan<ZuCSpan>{"h3"})
	.errorFn(Zquic::ErrorFn{[&serverErrors](ZeException) {
	  ++serverErrors;
	}})),
    "multi runtime server init failed");
  TestClient client;
  ZuCHECK(client.init(
      Zquic::ClientParams(&mx, "3", "4")
	.caPath(temp.certPath.cspan())
	.maxData(32768)
	.maxStreamData(8192)
	.maxStreamsDuplex(8)
	.maxStreamsSimplex(8)
	.alpn(ZuSpan<ZuCSpan>{"h3"})),
    "multi runtime client init failed");
  bool mxStarted = mx.start();
  ZuCHECK(mxStarted, "multi runtime multiplexer start failed");
  if (!mxStarted) { client.final(); server.final(); return; }

  ZuCHECK(server.start(), "multi runtime server listen failed");
  ZuCHECK(server.listening(),
    "multi runtime server did not become ready");



  ZmRef<TestClient::Link> clients[TestSrvCxnCount];
  ZmRef<TestServer::Link> serverLinks[TestSrvCxnCount];
  bool established = true;
  for (unsigned i = 0; i < TestSrvCxnCount; ++i) {
    clients[i] = new TestClient::Link{&client};
    clients[i]->connect(Zquic::Host{"127.0.0.1"}, server.local().port());
    if (!waitEstablished(clients[i], server, serverLinks[i], i)) {
      established = false;
      break;
    }
  }
  auto &c0 = clients[0];
  auto &c1 = clients[1];
  auto &s0 = serverLinks[0];
  auto &s1 = serverLinks[1];

  if (established)
    established = server.acceptedCount == TestSrvCxnCount;
  if (!established) {
    for (unsigned i = 0; i < TestSrvCxnCount; ++i) {
      if (clients[i])
	dumpDiag(
	  "client", diag(clients[i]), clients[i]->crypto().diag());
      if (serverLinks[i])
	dumpDiag(
	  "server", diag(serverLinks[i]), serverLinks[i]->crypto().diag());
    }
  }
  ZuCHECK(established,
    "17 runtime connections did not establish");
  ZuCHECK(server.acceptedCount == TestSrvCxnCount &&
      !serverErrors,
    "server live-link table rejected connections past the old cap");
  if (!established || !s0 || !s1) {
    for (unsigned i = 0; i < TestSrvCxnCount; ++i)
      if (clients[i]) clients[i]->closeAsync();
    ZuTime closeDeadline = Zm::now(10);
    for (unsigned i = 0; i < TestSrvCxnCount; ++i)
      if (clients[i])
        ZuCHECK(clients[i]->waitClosed(closeDeadline),
          "multi runtime failed connection cleanup did not complete");
    server.stop();
    waitDisconnect(server);
    for (unsigned i = 0; i < TestSrvCxnCount; ++i)
      clients[i] = nullptr;
    for (unsigned i = 0; i < TestSrvCxnCount; ++i)
      serverLinks[i] = nullptr;
    server.clearLinks();
    client.final();
    server.final();
    mx.stop();
    return;
  }

  auto c0s = openStream(c0, Zquic::StreamType::Duplex);
  auto c1s = openStream(c1, Zquic::StreamType::Duplex);
  ZuCHECK(c0->send(c0s, "zero") && c1->send(c1s, "one"),
    "multi runtime client stream sends failed");
  ZuCHECK(s0->waitStreams(1, Zm::now(10)) && s1->waitStreams(1, Zm::now(10)),
    "multi runtime routed stream bytes did not arrive independently");

#ifdef Zquic_DEBUG
  uint64_t s0Bytes = diag(s0).rx.streamBytesRx;
  uint64_t s1Bytes = diag(s1).rx.streamBytesRx;
  uint64_t failures = endpointDiag(server).failures();
#else
  unsigned s0Streams = s0->streamedCount;
  unsigned s1Streams = s1->streamedCount;
#endif
  unsigned errors = serverErrors;
  s0->disconnect();
  waitThread(mx, server.rxThread());
  ZuCHECK(s0->closed() && !s0->established(),
    "multi runtime server link did not close logically");

  auto c0Stale = openStream(c0, Zquic::StreamType::Duplex);
  auto c1Live = openStream(c1, Zquic::StreamType::Duplex);
  ZuCHECK(c0->send(c0Stale, "drop") && c1->send(c1Live, "alive"),
    "multi runtime post-close client stream sends failed");
#ifdef Zquic_DEBUG
  ZuTime deliveryDeadline = Zm::now(10);
  bool dropped = server.dropEvent.wait([&server, failures]() {
    return endpointDiag(server).failures() > failures;
  }, deliveryDeadline);
  bool delivered = s1->streamEvent.wait([&s1, s1Bytes]() {
    return diag(s1).rx.streamBytesRx >= s1Bytes + 5;
  }, deliveryDeadline);
  ZuCHECK(dropped && delivered,
    "multi runtime stale route drop or sibling delivery did not happen");
  ZuCHECK(diag(s0).rx.streamBytesRx == s0Bytes,
    "closed server link received stale routed data");
#else
  ZuCHECK(s1->waitStreams(s1Streams + 1, Zm::now(10)),
    "multi runtime sibling delivery did not happen");
  ZuCHECK(s0->streamedCount == s0Streams,
    "closed server link received stale routed data");
#endif
  ZuCHECK(serverErrors == errors,
    "stale routed datagram used ErrorFn instead of diagnostics");

  // Enter Stopping from a completed link, before Rx counts live links.
  ZmBlock<>{}([&server, &s1](auto wake) {
    server.rxRun([&server, s1, wake = ZuMv(wake)]() mutable {
      server.stopOnDisconnect = true;
      s1->disconnect();
      wake();
    });
  });
  ZuCHECK(s1->waitDisconnected(Zm::now(10)) && server.disconnectStops == 1 &&
      s1->disconnectedCount == 1,
    "multi runtime disconnect did not initiate server stop");
  for (unsigned i = 0; i < TestSrvCxnCount; ++i)
    clients[i]->closeAsync();
  server.stop();
  ZuTime closeDeadline = Zm::now(10);
  bool clientsClosed = true;
  for (unsigned i = 0; i < TestSrvCxnCount; ++i)
    if (!clients[i]->waitClosed(closeDeadline) || clients[i]->cxn())
      clientsClosed = false;
  ZuCHECK(clientsClosed && !server.connected(),
    "multi runtime endpoints remained connected after close");
  waitDisconnect(server);

  c0s = nullptr;
  c1s = nullptr;
  c0Stale = nullptr;
  c1Live = nullptr;
  for (unsigned i = 0; i < TestSrvCxnCount; ++i)
    clients[i] = nullptr;
  for (unsigned i = 0; i < TestSrvCxnCount; ++i)
    serverLinks[i] = nullptr;
  server.clearLinks();
  client.final();
  server.final();

  mx.stop();
}

void testRetryAddrValidate(bool forceFailure = false)
{
  ZuTestScope(testRetryAddrValidate);

  TempDir temp;
  ZuCHECK(temp.init(), "retry runtime temporary TLS certificate failed");

  ZiMultiplex mx(
      ZiMxParams()
	.scheduler([](auto &s) {
	  s.nThreads(5)
	    .thread(1, [](auto &t) { t.isolated(1); })
	    .thread(2, [](auto &t) { t.isolated(1); })
	    .thread(3, [](auto &t) { t.isolated(1); })
	    .thread(4, [](auto &t) { t.isolated(1); }); })
	.rxThread(1).txThread(2));

  TestServer server;
  server.resumeEarly = !forceFailure;
  server.repeatInitial = !forceFailure;
  if (forceFailure) {
    server.dropNextInitial = UINT_MAX;
    server.dropNextHandshake = UINT_MAX;
  }
  ZtString<> qlogPath = temp.pathOf("retry.sqlog");
  unlink(qlogPath.data());
  ZuCHECK(server.init(
      Zquic::ServerParams(&mx, "3", "4")
	.certPath(temp.certPath.cspan())
	.keyPath(temp.keyPath.cspan())
	.qlog(true)
	.qlogPath(qlogPath.cspan())

	.qlogRingSize(1<<16)
	.maxData(32768)
	.maxStreamData(8192)
	.maxStreamsDuplex(8)
	.maxStreamsSimplex(8)
	.retryAddrValidate(true)
	.newTokenAddrValidate(true)
	.addrValidationLifetime(60)
	.alpn(ZuSpan<ZuCSpan>{"h3"})),
    "retry runtime server init failed");
  TestClient client;
  client.resumeEarly = !forceFailure;
  ZuCHECK(client.init(
      Zquic::ClientParams(&mx, "3", "4")
	.caPath(temp.certPath.cspan())
	.maxData(32768)
	.maxStreamData(8192)
	.maxStreamsDuplex(8)
	.maxStreamsSimplex(8)
	.alpn(ZuSpan<ZuCSpan>{"h3"})),
    "retry runtime client init failed");
  bool mxStarted = mx.start();
  ZuCHECK(mxStarted, "retry runtime multiplexer start failed");
  if (!mxStarted) { client.final(); server.final(); return; }

  ZuCHECK(server.start(), "retry runtime server listen failed");
  ZuCHECK(server.listening(),
    "retry runtime server did not listen");
	  if (!server.listening()) {
	    server.stop();
	    client.final();
	    server.final();
	    mx.stop();
	    return;
	  }


  ZmRef<TestClient::Link> clientLink = new TestClient::Link{&client};
  clientLink->connect(Zquic::Host{"127.0.0.1"}, server.local().port());

  ZmRef<TestServer::Link> serverLink;
  bool established = false;
  if (forceFailure) {
    serverLink = server.waitLink(0, Zm::now(10));
    bool ready = client.rxEvent.wait([clientLink] { return clientLink->ready(); }, Zm::now(10));
    bool withheld = server.dropEvent.wait(
      [&server] { return bool(server.droppedInitial); }, Zm::now(10));
    bool incomplete = ZmBlock<bool>{}([clientLink](auto wake) {
      clientLink->app()->rxRun([clientLink, wake = ZuMv(wake)]() mutable {
	wake(!clientLink->crypto().oneRTTReady() && !clientLink->established());
      });
    });
    ZuCHECK(ready && serverLink && withheld && incomplete && !clientLink->closed(),
      "forced handshake failure did not retain an incomplete live handshake");
  } else established = waitEstablished(clientLink, server, serverLink);
  if (!established && !forceFailure) {
    dumpDiag(
      "client", diag(clientLink), clientLink->crypto().diag());
    if (serverLink)
      dumpDiag(
	"server", diag(serverLink), serverLink->crypto().diag());
  }
  ZuCHECK(forceFailure || established, "retry runtime handshake did not establish");
	  if (!established || !serverLink) {
	    drainFailedHandshake(clientLink, server);
	    ZuCHECK(!clientLink->cxn() && !server.connected(),
	      "failed handshake cleanup preceded physical owner drains");
	    clientLink = nullptr;
	    serverLink = nullptr;
	    server.clearLinks();
	    unsigned callbacks = client.rxCallbacks;
	    unsigned rxSID = client.rxThread(), txSID = client.txThread();
	    client.final();
	    server.final();
	    if (forceFailure) {
	      waitThread(mx, mx.rxThread());
	      waitThread(mx, mx.txThread());
	      waitThread(mx, rxSID);
	      waitThread(mx, txSID);
	      ZuCHECK(client.rxCallbacks == callbacks,
		"failed handshake callback ran after app finalization");
	    }
	    mx.stop();
	    return;
	  }
  ZuCHECK(server.retryPkts >= 1,
    "retry runtime server did not emit Retry");
  Zquic::AddrValidationDiag av = addrValidationDiag(server);
  ZuCHECK(av.retrySent >= 1 && av.retryAccepted == 1 &&
      av.retryRejected >= 1,
    "retry runtime address-validation diagnostics mismatch");
  ZuCHECK(server.acceptedCount == 1 && serverLink->repeatedInitials == 1,
    "repeated validated Initial did not reuse its Retry SCID route");
  const auto &params = clientLink->crypto().peerParams();
  ZuCHECK(params.origDCID.length() >= Zquic::MinCIDLength &&
      params.initialSCID.length() >= Zquic::MinCIDLength &&
      params.retrySCID.length() >= Zquic::MinCIDLength,
    "retry runtime transport parameters missing Retry CIDs");
  ZuCHECK(!diag(clientLink).failures() &&
      !diag(serverLink).failures(),
    "retry runtime failure diagnostics mismatch");
  ZuCHECK(client.rxEvent.wait([&clientLink]() {
      return clientLink->receivedNewToken();
    }, Zm::now(10)), "retry runtime NEW_TOKEN was not exchanged");

  ZuCHECK(!client.ticketReady.timedwait(Zm::now(5)), "initial TLS session ticket missing");
  unsigned retryPkts = server.retryPkts;
  bool clientClosed = clientLink->close();
  ZuCHECK(clientClosed && !clientLink->cxn(),
    "retry runtime first connection did not close");
  waitDisconnect(server);

  client.holdOneRTT = 1;
  clientLink->connect(Zquic::Host{"127.0.0.1"}, server.local().port());
  ZmRef<TestServer::Link> serverLink2;
  bool established2 = waitEstablished(clientLink, server, serverLink2, 1);
  if (!established2) {
    dumpDiag(
      "client2", diag(clientLink), clientLink->crypto().diag());
    if (serverLink2)
      dumpDiag(
	"server2", diag(serverLink2), serverLink2->crypto().diag());
  }
  ZuCHECK(established2,
    "retry runtime NEW_TOKEN reconnect did not establish");
	  if (!established2 || !serverLink2) {
	    drainFailedHandshake(clientLink, server);
	    clientLink = nullptr;
	    serverLink = nullptr;
	    serverLink2 = nullptr;
	    server.clearLinks();
	    client.final();
	    server.final();
	    mx.stop();
	    return;
	  }
  ZuCHECK(server.acceptedCount == 2,
    "retry runtime server did not accept NEW_TOKEN reconnect");
  ZuCHECK(server.retryPkts == retryPkts,
    "retry runtime NEW_TOKEN reconnect unexpectedly used Retry");
  av = addrValidationDiag(server);
  ZuCHECK(av.newTokenAccepted == 1,
    "retry runtime NEW_TOKEN acceptance diagnostics mismatch");
  ZuCHECK(!diag(clientLink).failures() &&
      !diag(serverLink2).failures(),
    "retry runtime NEW_TOKEN reconnect diagnostics mismatch");

  auto earlyStream = findStream(clientLink, 0);
  bool acked = earlyStream && earlyStream->txEvent.wait([clientLink, earlyStream] {
    return ZmBlock<bool>{}([clientLink, earlyStream](auto wake) {
      clientLink->app()->txRun([earlyStream, wake = ZuMv(wake)]() mutable {
	wake(earlyStream->finSent() && earlyStream->txDrained());
      });
    });
  }, Zm::now(5));
  client.holdOneRTT = 0;
  ZuCHECK(acked && serverLink2->earlyBytes == 7,
    "resumed 0-RTT STREAM must arrive once and drain through a 1-RTT acknowledgment");
  earlyStream = nullptr;
  clientClosed = clientLink->close();
  server.stop();
  ZuCHECK(clientClosed && !clientLink->cxn() &&
      !server.connected(),
    "retry runtime endpoints remained connected after close");
  waitDisconnect(server);

  clientLink = nullptr;
  serverLink = nullptr;
  serverLink2 = nullptr;
  server.clearLinks();
  client.final();
  server.final();

#ifdef Zquic_DEBUG
  ZtString<> qlog = readFile(qlogPath.cspan());
  ZuCHECK(qlog, "retry runtime qlog output missing");
  ZuCHECK(qlog.find<"zquic:retry_sent">() >= 0,
    "retry runtime qlog missing retry_sent");
  ZuCHECK(qlog.find<"zquic:retry_validated">() >= 0,
    "retry runtime qlog missing retry_validated");
  ZuCHECK(qlog.find<"zquic:token_issued">() >= 0,
    "retry runtime qlog missing token_issued");
  ZuCHECK(qlog.find<"zquic:token_validated">() >= 0,
    "retry runtime qlog missing token_validated");
  ZuCHECK(qlog.find<"zquic:token_rejected">() >= 0,
    "retry runtime qlog missing token_rejected");
  ZuCHECK(qlog.find<"quic:packet_buffered">() >= 0,
    "retry runtime qlog missing packet_buffered");
  ZuCHECK(qlog.find<"address_validation">() >= 0,
    "retry runtime qlog missing address-validation reason");
  ZuCHECK(qlog.find<"missing_token">() >= 0,
    "retry runtime qlog missing missing-token rejection reason");
	  ZuCHECK(qlog.find<"new_token">() >= 0,
	    "retry runtime qlog missing NEW_TOKEN reason");
	  ZuCHECK(qlog.find<"coalescing">() < 0,
	    "retry runtime qlog leaked private coalescing reason");
#endif

  mx.stop();
}

void testRejectedTokenQLog()
{
  ZuTestScopeRT(testRejectedTokenQLog);

  TempDir temp;
  ZQUIC_CHECK_RT(temp.init(), "rejected-token runtime temporary TLS certificate failed");
  ZtString<> qlogPath = temp.pathOf("rejected-token.sqlog");
  ZtString<> policyQlogPath = temp.pathOf("policy-token.sqlog");
  unlink(qlogPath.data());
  unlink(policyQlogPath.data());

  Zquic::TokenSecret secret;
  secret.length(Zquic::AddressToken::SecretLength);
  for (unsigned i = 0, n = secret.length(); i < n; ++i) secret[i] = uint8_t(i + 1);

  ZiMultiplex mx(
      ZiMxParams()
	.scheduler([](auto &s) {
	  s.nThreads(5)
	    .thread(1, [](auto &t) { t.isolated(1); })
	    .thread(2, [](auto &t) { t.isolated(1); })
	    .thread(3, [](auto &t) { t.isolated(1); })
	    .thread(4, [](auto &t) { t.isolated(1); }); })
	.rxThread(1).txThread(2));

  TestServer server;
  ZQUIC_CHECK_RT(server.init(
      Zquic::ServerParams(&mx, "3", "4")
	.certPath(temp.certPath.cspan())
	.keyPath(temp.keyPath.cspan())
	.qlog(true)
	.qlogPath(qlogPath.cspan())

	.qlogRingSize(1<<16)
	.maxData(32768)
	.maxStreamData(8192)
	.maxStreamsDuplex(8)
	.maxStreamsSimplex(8)
	.retryAddrValidate(true)
	.newTokenAddrValidate(true)
	.addrValidationSecret(secret)
	.addrValidationLifetime(60)
	.alpn(ZuSpan<ZuCSpan>{"h3"})),
    "rejected-token runtime server init failed");
  TestClient client;
  client.tokenSecret = secret;
  client.tokenNow = uint64_t(Zm::now().sec());
  ZQUIC_CHECK_RT(client.init(
	Zquic::ClientParams(&mx, "3", "4")
	  .caPath(temp.certPath.cspan())
	  .maxData(32768)
	  .maxStreamData(8192)
	  .maxStreamsDuplex(8)
	  .maxStreamsSimplex(8)
	  .alpn(ZuSpan<ZuCSpan>{"h3"})),
    "rejected-token runtime client init failed");
  TestServer policyServer;
  bool policyInit = policyServer.init(
      Zquic::ServerParams(&mx, "3", "4")
	.certPath(temp.certPath.cspan())
	.keyPath(temp.keyPath.cspan())
	.qlog(true)
	.qlogPath(policyQlogPath.cspan())

	.qlogRingSize(1<<16)
	.maxData(32768)
	.maxStreamData(8192)
	.maxStreamsDuplex(8)
	.maxStreamsSimplex(8)
	.retryAddrValidate(true)
	.newTokenAddrValidate(false)
	.addrValidationSecret(secret)
	.addrValidationLifetime(60)
	.alpn(ZuSpan<ZuCSpan>{"h3"}));
  ZQUIC_CHECK_RT(policyInit, "rejected-token policy server init failed");
  if (!policyInit) { client.final(); server.final(); policyServer.final(); return; }
  bool mxStarted = mx.start();
  ZQUIC_CHECK_RT(mxStarted, "rejected-token runtime multiplexer start failed");
  if (!mxStarted) { client.final(); server.final(); policyServer.final(); return; }

  ZQUIC_CHECK_RT(server.start(), "rejected-token runtime server listen failed");
  ZQUIC_CHECK_RT(server.listening(),
    "rejected-token runtime server did not listen");
	  if (!server.listening()) {
	    server.stop();
	    client.final();
	    server.final();
	    policyServer.final();
	    mx.stop();
	    return;
	  }


  ZmRef<TestClient::Link> clientLink = new TestClient::Link{&client};

  clientLink->connect(Zquic::Host{"127.0.0.1"}, server.local().port());
  ZmRef<TestServer::Link> serverLink;
  bool established = waitEstablished(clientLink, server, serverLink);
  if (!established) {
    dumpDiag(
      "client", diag(clientLink), clientLink->crypto().diag());
    if (serverLink)
      dumpDiag(
	"server", diag(serverLink), serverLink->crypto().diag());
  }
  ZQUIC_CHECK_RT(established,
    "rejected-token runtime initial handshake did not establish");
	  if (!established || !serverLink) {
	    drainFailedHandshake(clientLink, server);
	    clientLink = nullptr;
	    serverLink = nullptr;
	    server.clearLinks();
	    client.final();
	    server.final();
	    policyServer.final();
	    mx.stop();
	    return;
	  }
  ZQUIC_CHECK_RT(client.rxEvent.wait([&clientLink]() {
      return clientLink->receivedNewToken();
    }, Zm::now(10)), "rejected-token runtime NEW_TOKEN was not exchanged");
  bool clientClosed = clientLink->close();
  ZQUIC_CHECK_RT(clientClosed && !clientLink->cxn(),
    "rejected-token runtime initial connection did not close");
  waitDisconnect(server);

  unsigned accepted = 1;
  unsigned modes[] = {
    TestClient::TokenMalformed,
    TestClient::TokenAuth,
    TestClient::TokenExpired,
    TestClient::TokenAddress,
    TestClient::TokenKind
  };
  for (unsigned i = 0; i < sizeof(modes) / sizeof(modes[0]); ++i) {
    client.tokenNow = uint64_t(Zm::now().sec());
    client.tokenRewrite = modes[i];
    clientLink->connect(Zquic::Host{"127.0.0.1"}, server.local().port());

    serverLink = nullptr;
    established = waitEstablished(clientLink, server, serverLink, accepted);
    if (!established) {
      dumpDiag(
	"client", diag(clientLink), clientLink->crypto().diag());
      if (serverLink)
	dumpDiag(
	  "server", diag(serverLink), serverLink->crypto().diag());
    }
    ZQUIC_CHECK_RT(established, "rejected-token runtime handshake did not establish");
	    if (!established || !serverLink) {
	      break;
    }
    ++accepted;
    bool clientClosed = clientLink->close();
    ZQUIC_CHECK_RT(clientClosed && !clientLink->cxn(),
      "rejected-token runtime connection did not close");
    waitDisconnect(server);
  }
  closeClient(clientLink);
  server.stop();
  ZQUIC_CHECK_RT(!server.connected(),
    "rejected-token runtime server remained connected");
  waitDisconnect(server);
  serverLink = nullptr;
  server.clearLinks();
  server.final();

  mx.stop();
  bool policyMxStarted = mx.start();
  ZQUIC_CHECK_RT(policyMxStarted,
    "rejected-token policy multiplexer start failed");
  if (!policyMxStarted) {
    clientLink = nullptr;
    client.final(); policyServer.final(); return;
  }
  ZQUIC_CHECK_RT(policyServer.start(), "rejected-token policy server listen failed");
  ZQUIC_CHECK_RT(policyServer.listening(),
    "rejected-token policy server did not listen");
  client.tokenNow = uint64_t(Zm::now().sec());
  client.tokenRewrite = TestClient::TokenPolicy;
  clientLink->connect(Zquic::Host{"127.0.0.1"}, policyServer.local().port());
  ZmRef<TestServer::Link> policyLink;
  established = waitEstablished(clientLink, policyServer, policyLink);
  ZQUIC_CHECK_RT(established, "rejected-token policy handshake did not establish");
  clientClosed = clientLink->close();
  ZQUIC_CHECK_RT(clientClosed && !clientLink->cxn(),
    "rejected-token policy connection did not close");
  policyServer.stop();
  waitDisconnect(policyServer);
  policyLink = nullptr;
  policyServer.clearLinks();
  policyServer.final();
  clientLink = nullptr;
  client.final();

#ifdef Zquic_DEBUG
  ZtString<> qlog = readFile(qlogPath.cspan());
  ZtString<> policyQlog = readFile(policyQlogPath.cspan());
  ZQUIC_CHECK_RT(qlog, "rejected-token runtime qlog output missing");
  ZQUIC_CHECK_RT(policyQlog, "rejected-token policy qlog output missing");
  ZQUIC_CHECK_RT(qlog.find<"zquic:token_rejected">() >= 0,
    "rejected-token runtime qlog missing token_rejected");
  ZQUIC_CHECK_RT(qlog.find<"malformed">() >= 0,
    "rejected-token runtime qlog missing malformed reason");
  ZQUIC_CHECK_RT(qlog.find<"auth">() >= 0,
    "rejected-token runtime qlog missing auth reason");
  ZQUIC_CHECK_RT(qlog.find<"expired">() >= 0,
    "rejected-token runtime qlog missing expired reason");
  ZQUIC_CHECK_RT(qlog.find<"address">() >= 0,
    "rejected-token runtime qlog missing address reason");
  ZQUIC_CHECK_RT(qlog.find<"kind">() >= 0,
    "rejected-token runtime qlog missing kind reason");
  ZQUIC_CHECK_RT(policyQlog.find<"new_token_policy">() >= 0,
    "rejected-token runtime qlog missing NEW_TOKEN policy reason");
#endif

  mx.stop();
}

void testIdleTimeoutQLog()
{
  ZuTestScope(testIdleTimeoutQLog);

  TempDir temp;
  ZuCHECK(temp.init(), "idle runtime temporary TLS certificate failed");
  ZtString<> qlogPath = temp.pathOf("idle.sqlog");
  unlink(qlogPath.data());

  ZiMultiplex mx(
      ZiMxParams()
	.scheduler([](auto &s) {
	  s.nThreads(5)
	    .thread(1, [](auto &t) { t.isolated(1); })
	    .thread(2, [](auto &t) { t.isolated(1); })
	    .thread(3, [](auto &t) { t.isolated(1); })
	    .thread(4, [](auto &t) { t.isolated(1); }); })
	.rxThread(1).txThread(2));

  TestServer server;
  ZuCHECK(server.init(
      Zquic::ServerParams(&mx, "3", "4")
	.certPath(temp.certPath.cspan())
	.keyPath(temp.keyPath.cspan())
	.qlog(true)
	.qlogPath(qlogPath.cspan())

	.qlogRingSize(1<<16)
	.maxData(32768)
	.maxStreamData(8192)
	.maxStreamsDuplex(8)
	.maxStreamsSimplex(8)
	.maxIdleTimeout(1)
	.alpn(ZuSpan<ZuCSpan>{"h3"})),
    "idle runtime server init failed");
  TestClient client;
  ZuCHECK(client.init(
      Zquic::ClientParams(&mx, "3", "4")
	.caPath(temp.certPath.cspan())
	.maxData(32768)
	.maxStreamData(8192)
	.maxStreamsDuplex(8)
	.maxStreamsSimplex(8)
	.maxIdleTimeout(1)
	.alpn(ZuSpan<ZuCSpan>{"h3"})),
    "idle runtime client init failed");
  bool mxStarted = mx.start();
  ZuCHECK(mxStarted, "idle runtime multiplexer start failed");
  if (!mxStarted) { client.final(); server.final(); return; }

  ZuCHECK(server.start(), "idle runtime server listen failed");
  ZuCHECK(server.listening(),
    "idle runtime server did not listen");
	  if (!server.listening()) {
	    server.stop();
	    client.final();
	    server.final();
	    mx.stop();
	    return;
	  }


  ZmRef<TestClient::Link> clientLink = new TestClient::Link{&client};
  clientLink->connect(Zquic::Host{"127.0.0.1"}, server.local().port());

  ZmRef<TestServer::Link> serverLink;
  bool established = waitEstablished(clientLink, server, serverLink);
  if (!established) {
    dumpDiag(
      "client", diag(clientLink), clientLink->crypto().diag());
    if (serverLink)
      dumpDiag(
	"server", diag(serverLink), serverLink->crypto().diag());
  }
  ZuCHECK(established, "idle runtime handshake did not establish");
	  if (!established || !serverLink) {
	    drainFailedHandshake(clientLink, server);
	    clientLink = nullptr;
	    serverLink = nullptr;
	    server.clearLinks();
	    client.final();
	    server.final();
	    mx.stop();
	    return;
	  }

  ZuCHECK(clientLink->waitDisconnected(Zm::now(10)) && !clientLink->cxn(),
    "idle runtime client did not close on idle timeout");
  closeClient(clientLink);
  server.stop();
  waitDisconnect(server);

  clientLink = nullptr;
  serverLink = nullptr;
  server.clearLinks();
  client.final();
  server.final();

#ifdef Zquic_DEBUG
  ZtString<> qlog = readFile(qlogPath.cspan());
  ZuCHECK(qlog, "idle runtime qlog output missing");
  ZuCHECK(qlog.find<"quic:connection_closed">() >= 0,
    "idle runtime qlog missing connection_closed");
  ZuCHECK(qlog.find<"idle_timeout">() >= 0,
    "idle runtime qlog missing idle_timeout trigger");
  ZuCHECK(qlog.find<"idle">() >= 0,
    "idle runtime qlog missing idle reason");
  ZuCHECK(qlog.find<"no_error">() >= 0,
    "idle runtime qlog missing no_error close code");
#endif

  mx.stop();
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZiTestResidue::init("ZquicRuntimeTest");
  ZuTestMain();
  ZuTestCall(testServerBindings);
  ZuTestCall(testResetBindingSource);
  ZuTestCall(testEndpointOpen);
  ZuTestCall(testHandshakeCryptoLoss);
  ZuTestCall(testTLSOutputFailure);
  ZuTestCall(testHandshakeKeyRetirement);
  ZuTestCall(testMigrationLoss);
  ZuTestCall(testPreferredBindings, false, false, true);
  ZuTestCall(testPreferredBindings, false, false, false);
  ZuTestCall(testPreferredBindings, false, true, true);
  ZuTestCall(testPreferredBindings, true, false, true);
  ZuTestCall(testPreferredStop, false);
  ZuTestCall(testPreferredStop, true);
  ZuTestCall(testSrvMultiCxn);
  ZuTestCall(testRetryAddrValidate);
  ZuTestCall(testRetryAddrValidate, true);
  ZuTestCall(testRejectedTokenQLog);
  ZuTestCall(testIdleTimeoutQLog);
}
