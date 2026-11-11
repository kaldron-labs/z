//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <unistd.h>

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

#include <iostream>

#include <zlib/ZuBox.hh>
#include <zlib/ZuTestUtil.hh>
#include <zlib/ZtString.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmBlock.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZfJSON.hh>
#include <zlib/Zquic.hh>

#include "ZiTestResidue.hh"

#include "ZquicTestPorts.hh"

using namespace ZuTestUtil;

#define ZQUIC_CHECK_RT(x, ...) ZuCheckRT(x, log_(__VA_ARGS__))

namespace {

static constexpr unsigned TestSrvLinkCap = 20;
static constexpr unsigned TestSrvCxnCount = 17;

// Durable fixture-owned notifications: a timeout never leaves a callback
// referring to a stack waiter. Pending producers are drained with the fixture.
struct TestEvent {
  ZmSemaphore sem;

  void post() { sem.post(); }
  template <typename L>
  bool wait(L ready, ZuTime deadline) {
    while (!ready())
      if (sem.timedwait(deadline)) return ready();
    return true;
  }
};

template <typename Link>
Zquic::LinkDiag diag(const Link &link)
{
  return ZmBlock<Zquic::LinkDiag>{}(
    [&link](auto wake) { link->diag(ZuMv(wake)); });
}

template <typename Link>
Zquic::EndpointDiag cxnDiag(const Link &link)
{
  return ZmBlock<Zquic::EndpointDiag>{}(
    [&link](auto wake) { link->cxnDiag(ZuMv(wake)); });
}

template <typename Server>
Zquic::EndpointDiag endpointDiag(Server &server)
{
  return ZmBlock<Zquic::EndpointDiag>{}(
    [&server](auto wake) { server.endpointDiag(ZuMv(wake)); });
}

template <typename Server>
Zquic::AddrValidationDiag addrValidationDiag(Server &server)
{
  return ZmBlock<Zquic::AddrValidationDiag>{}(
    [&server](auto wake) { server.addrValidationDiag(ZuMv(wake)); });
}

template <typename Link>
auto openStream(
  const Link &link,
  Zquic::StreamType::T type = Zquic::StreamType::Duplex)
{
  using StreamRef = decltype(link->stream(type));
  StreamRef stream;
  ZmBlock<>{}([&link, type, &stream](auto wake) {
    link->app()->txRun([
      link, type, &stream, wake = ZuMv(wake)
    ]() mutable {
      stream = link->stream(type);
      wake();
    });
  });
  return stream;
}

template <typename Link>
auto findStream(const Link &link, int64_t id)
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

  enum TokenRewrite {
    TokenNone,
    TokenMalformed,
    TokenAuth,
    TokenExpired,
    TokenAddress,
    TokenPolicy,
    TokenKind
  };

  bool sendPkt(const ZmRef<ZiIOBuf> &buf) {
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

  bool dropMigChallenge = false; // immutable after init
  unsigned pendingMigDrop = 0; // Tx thread
  ZmAtomic<unsigned> migChallengeDrops = 0;
  Zquic::TokenSecret tokenSecret;
  uint64_t tokenNow = 0;
  ZmAtomic<unsigned> tokenRewrite = TokenNone;
  ZmAtomic<unsigned> tokenRewritesApplied = 0;
  ZmAtomic<unsigned> tokenRewriteLengthMismatch = 0;
};
struct TestServerLink;
struct TestServerStream;
struct TestServer :
    public Zquic::Server<TestServer, TestServerLink> {
  using Link = TestServerLink;
  using Stream = TestServerStream;

  ZmAtomic<unsigned> holdHandshakeDone = 0;
  ZmAtomic<unsigned> heldHandshakeDone = 0;

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
	--dropNextInitial;
	++droppedInitial;
	return false;
      }
      if (h.type == Zquic::PktType::Handshake && dropNextHandshake) {
	--dropNextHandshake;
	++droppedHandshake;
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
	ref.controlType == Zquic::FrameType::HandshakeDone) {
      ++heldHandshakeDone;
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

  bool stopOnDisconnect = false; // Rx thread - one-shot stop race
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
  ZmAtomic<unsigned> dropNextShort = 0;
  ZmAtomic<unsigned> droppedShort = 0;
  ZmAtomic<unsigned> shortPkts = 0;
  ZmAtomic<int64_t> dropStreamID = -1;
  ZmAtomic<unsigned> droppedStream = 0;
  ZmAtomic<unsigned> streamFrames = 0;
};

struct TestClient::Stream :
  public Zquic::CliStream<TestClient::Link, TestClient::Stream> {
  using Base =
    Zquic::CliStream<TestClient::Link, TestClient::Stream>;
  using Base::Base;

  int process(Zquic::RxStream &);

  unsigned processed = 0;
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

  CryptoSnapshot handshakeKeys;

  Link(TestClient *app) : Base{app} { }

  void connected(Zquic::Connected) {
    Base::snapshotTxCrypto(handshakeKeys);
    ++connectedCount;
    connectedEvent.post();
  }
  bool handshakeRxKeysLive() {
    auto &crypto = Base::crypto_();
    return crypto.rxSecretInstalled(Zquic::PktNumSpace::Handshake) ||
      crypto.txSecretInstalled(Zquic::PktNumSpace::Handshake) ||
      crypto.rxProtState(Zquic::PktNumSpace::Handshake).valid() ||
      crypto.txProtState(Zquic::PktNumSpace::Handshake).valid();
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
      link->Base::closeTimeout_();
      return link.ptr();
    });
  }

  Zquic::MigrationResult promotion; // Tx thread
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
};

struct TestServerStream :
  public Zquic::SrvStream<TestServerLink, TestServerStream> {
  using Base =
    Zquic::SrvStream<TestServerLink, TestServerStream>;
  using Base::Base;

  int process(Zquic::RxStream &);

  unsigned processed = 0;
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

  CryptoSnapshot handshakeKeys;

  TestServerLink(TestServer *app) : Base{app} { }

  void connected(Zquic::Connected) {
    Base::snapshotTxCrypto(handshakeKeys);
    ++connectedCount;
    connectedEvent.post();
  }
  bool handshakeRxKeysLive() {
    auto &crypto = Base::crypto_();
    return crypto.rxSecretInstalled(Zquic::PktNumSpace::Handshake) ||
      crypto.txSecretInstalled(Zquic::PktNumSpace::Handshake) ||
      crypto.rxProtState(Zquic::PktNumSpace::Handshake).valid() ||
      crypto.txProtState(Zquic::PktNumSpace::Handshake).valid();
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
	link->Base::closeTimeout_();
	wake();
      });
    });
  }

  TestEvent connectedEvent;
  TestEvent disconnectedEvent;
  TestEvent streamEvent;
  ZmAtomic<unsigned> processedStreams = 0;
  ZmAtomic<unsigned> connectedCount = 0;
  ZmAtomic<unsigned> disconnectedCount = 0;
  ZmAtomic<unsigned> streamedCount = 0;
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

ZmRef<TestServer::Link> TestServer::accepted(const Zquic::InitialInfo &)
{
  unsigned i = acceptedCount;
  if (i >= TestSrvLinkCap) return nullptr;
  links_[i] = new Link{this};
  link_ = links_[i];
  ++acceptedCount;
  acceptedEvent.post();
  return link_;
}

int TestClient::Stream::process(Zquic::RxStream &)
{
  if (!processed++) link()->processedStream();
  return 0;
}

int TestServerStream::process(Zquic::RxStream &)
{
  if (!processed++) link()->processedStream();
  return 0;
}

ZmRef<TestServer::Link> TestServer::waitLink(unsigned i, ZuTime deadline)
{
  if (!acceptedEvent.wait([this, i]() { return acceptedCount > i; }, deadline))
    return nullptr;
  return link(i);
}

bool waitEstablished(
  const ZmRef<TestClient::Link> &client, TestServer &server,
  ZmRef<TestServer::Link> &peer, unsigned i = 0)
{
  ZuTime deadline = Zm::now(10);
  if (!client->waitConnected(deadline)) return false;
  peer = server.waitLink(i, deadline);
  return peer && peer->waitConnected(deadline);
}

void TestServer::clearLinks()
{
  link_ = nullptr;
  for (unsigned i = 0; i < TestSrvLinkCap; ++i)
    links_[i] = nullptr;
}

// Accepted harness polling for UDP readiness and intermediate/diagnostic
// states lacking an equivalent fixture completion callback at this base.
template <typename L>
bool waitUntil(L l)
{
  for (unsigned i = 0; i < 10000; ++i) {
    if (l()) return true;
    usleep(1000);
  }
  return false;
}

void waitThread(ZiMultiplex &mx, unsigned thread)
{
	ZmBlock<>{}([&mx, thread](auto wake) {
	  mx.invoke([wake = ZuMv(wake)]() mutable { wake(); }, thread);
	});
}

template <typename App>
void waitDisconnect(App &app)
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
ZtString<> readFile_(ZuCSpan path)
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

ZuTuple<unsigned, bool> challengeSizes_(ZuCSpan sequence)
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

void dumpDiag(
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
      "-addext subjectAltName=DNS:localhost,IP:127.0.0.1 "
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

} // namespace

template <typename Link>
static void closeClient(const Link &link)
{
  ZmBlock<>{}([&link](auto wake) {
    link->disconnect([wake = ZuMv(wake)]() mutable { wake(); });
  });
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
  backing->append(reinterpret_cast<const uint8_t *>("prefix!"), 7);
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
    return waitUntil([receiver, id, bytes, size]() {
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
    });
  };
  ZQUIC_CHECK_RT(checkInput(output, expected, N), "owned protected bytes changed");
  bool completed = waitUntil([sender, output]() {
    return ZmBlock<bool>{}([sender, output](auto wake) {
      sender->app()->txRun([output, wake = ZuMv(wake)]() mutable {
        wake(output->finSent() && output->txDrained());
      });
    });
  });
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
  ZuCHECK(waitUntil([&clientLink]() { return clientLink->ready(); }),
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


	ZuCHECK(clientLink->close(), "runtime client disconnect did not complete");
	waitDisconnect(server);
	ZuCHECK(waitUntil([&serverLink]() { return serverLink->closed(); }),
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
  ZtString<> qlog = readFile_(qlogPath.cspan());
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

  ZiMultiplex mx(
      ZiMxParams()
	.scheduler([](auto &s) {
	  s.nThreads(5)
	    .thread(1, [](auto &t) { t.isolated(1); })
	    .thread(2, [](auto &t) { t.isolated(1); })
	    .thread(3, [](auto &t) { t.isolated(1); })
	    .thread(4, [](auto &t) { t.isolated(1); }); })
	.rxThread(1).txThread(2));

  for (unsigned dropHandshake = 0; dropHandshake < 2; ++dropHandshake) {
    TestServer server;
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
    ZQUIC_CHECK_RT(dropHandshake ? server.droppedHandshake == 4 :
	server.droppedInitial == 1,
      "loss runtime did not drop selected long-header packet");
    ZQUIC_CHECK_RT(dropHandshake ? server.handshakePkts > server.droppedHandshake :
	server.initialPkts > server.droppedInitial,
      "loss runtime did not send selected packet after deliberate loss");
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
    ZQUIC_CHECK_RT(waitUntil([&server]() { return bool(server.heldHandshakeDone); }),
      "key retirement did not hold HANDSHAKE_DONE");
    ZQUIC_CHECK_RT(waitUntil([&serverLink]() {
      return !handshakeKeysLive(serverLink);
    }), "server retained Handshake keys pending HANDSHAKE_DONE acknowledgment");
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
    ZQUIC_CHECK_RT(waitUntil([&clientLink]() {
      return !handshakeKeysLive(clientLink);
    }), "client retained Handshake keys after HANDSHAKE_DONE");
    ZQUIC_CHECK_RT(staleKeysStayRetired(clientLink),
      "stale TLS snapshot restored retired client Handshake keys");
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
  auto exercise = [&]() {
    if (!serverStart || !clientInit) return;
    bool listening = waitUntil([&server]() { return server.listening(); });
    ZQUIC_CHECK_RT(listening, "migration loss server did not listen");
    if (!listening) return;
    clientLink = new TestClient::Link{&client};
    clientLink->connect(Zquic::Host{"127.0.0.1"}, server.local().port());
    auto ready = [](const auto &link) {
      return link && ZmBlock<bool>{}([&link](auto wake) {
	link->app()->rxRun([link, wake = ZuMv(wake)]() mutable {
	  wake(link->crypto().oneRTTReady());
	});
      });
    };
    auto accepted = [&server]() {
      return ZmBlock<ZmRef<TestServer::Link>>{}([&server](auto wake) {
	server.rxRun([&server, wake = ZuMv(wake)]() mutable {
	  wake(server.link());
	});
      });
    };
    bool established = waitUntil([&]() {
      if (!serverLink) serverLink = accepted();
      return ready(clientLink) && ready(serverLink);
    });
    ZQUIC_CHECK_RT(established, "migration loss UDP handshake failed");
    if (!established) return;
    bool spare = waitUntil([&clientLink]() { return clientLink->spareCID(); });
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
    bool requested = ZmBlock<bool>{}([&](auto wake) {
      client.txRun([clientLink, wake = ZuMv(wake)]() mutable {
	wake(clientLink->migrateLocal(ZiIP{"127.0.0.1"}));
      });
    });
    ZQUIC_CHECK_RT(requested, "migration loss local rebind was rejected");
    Zquic::MigrationResult after;
    bool promoted = waitUntil([&]() {
      after = ZmBlock<Zquic::MigrationResult>{}([&clientLink](auto wake) {
	clientLink->app()->txRun([clientLink, wake = ZuMv(wake)]() mutable {
	  wake(clientLink->promotion);
	});
      });
      return after.state == Zquic::MigrationState::Promoted;
    });
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
    bool serverPromoted = waitUntil([&serverLink]() {
      return diag(serverLink).tx.pathValidationPromoted != 0;
    });
    ZQUIC_CHECK_RT(serverPromoted,
      "migration loss server did not validate its own candidate challenge");
    if (!serverPromoted) return;

    auto stream = openStream(serverLink);
    ZQUIC_CHECK_RT(stream, "migration loss stream open failed");
    if (!stream) return;
    constexpr ZuCSpan payload{"migration-loss-recovered"};
    bool sent = ZmBlock<bool>{}([&](auto wake) {
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
    bool received = waitUntil([&]() {
      auto incoming = findStream(clientLink, id);
      if (!incoming) return false;
      return ZmBlock<bool>{}([&](auto wake) {
	client.rxRun([incoming, payload, wake = ZuMv(wake)]() mutable {
	  auto &rx = incoming->rxStream();
	  unsigned offset = 0;
	  bool ok = rx.length() == payload.length();
	  if (ok) {
	    auto n = rx.each(payload.length(), [&](auto part) -> int64_t {
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
    });
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
  auto sizes = challengeSizes_(readFile_(clientQLog.cspan()));
  ZQUIC_CHECK_RT(sizes.p<0>() >= 2 && sizes.p<1>(),
    "initial and recovered active PATH_CHALLENGE packets must each reach 1200 bytes; "
    "count=", sizes.p<0>(), " valid=", sizes.p<1>());
#endif
  mx.stop();
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
  ZuCHECK(waitUntil([&s0]() {
      return s0->closed() && !s0->established();
    }), "multi runtime server link did not close logically");

  auto c0Stale = openStream(c0, Zquic::StreamType::Duplex);
  auto c1Live = openStream(c1, Zquic::StreamType::Duplex);
  ZuCHECK(c0->send(c0Stale, "drop") && c1->send(c1Live, "alive"),
    "multi runtime post-close client stream sends failed");
#ifdef Zquic_DEBUG
  ZuCHECK(waitUntil([&server, &s1, failures, s1Bytes]() {
      return endpointDiag(server).failures() > failures &&
	diag(s1).rx.streamBytesRx >= s1Bytes + 5;
    }), "multi runtime stale route drop or sibling delivery did not happen");
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
  ZuCHECK(waitUntil([&server, &s1]() {
      return server.disconnectStops == 1 && s1->disconnectedCount == 1;
    }), "multi runtime disconnect did not initiate server stop");
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

void testRetryAddrValidate()
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
  bool established = waitEstablished(clientLink, server, serverLink);
  if (!established) {
    dumpDiag(
      "client", diag(clientLink), clientLink->crypto().diag());
    if (serverLink)
      dumpDiag(
	"server", diag(serverLink), serverLink->crypto().diag());
  }
  ZuCHECK(established, "retry runtime handshake did not establish");
	  if (!established || !serverLink) {
	    client.final();
	    server.stop();
	    client.final();
	    server.final();
	    mx.stop();
	    return;
	  }
  ZuCHECK(server.retryPkts >= 1,
    "retry runtime server did not emit Retry");
  Zquic::AddrValidationDiag av = addrValidationDiag(server);
  ZuCHECK(av.retrySent >= 1 && av.retryAccepted == 1 &&
      av.retryRejected >= 1,
    "retry runtime address-validation diagnostics mismatch");
  ZuCHECK(server.acceptedCount == 1,
    "retry runtime server accepted before validated Initial");
  const auto &params = clientLink->crypto().peerParams();
  ZuCHECK(params.origDCID.length() >= Zquic::MinCIDLength &&
      params.initialSCID.length() >= Zquic::MinCIDLength &&
      params.retrySCID.length() >= Zquic::MinCIDLength,
    "retry runtime transport parameters missing Retry CIDs");
  ZuCHECK(!diag(clientLink).failures() &&
      !diag(serverLink).failures(),
    "retry runtime failure diagnostics mismatch");
#ifdef Zquic_DEBUG
  ZuCHECK(waitUntil([&clientLink, &serverLink]() {
      return diag(clientLink).rx.newTokenRx >= 1 &&
	diag(serverLink).tx.newTokenTx >= 1;
    }), "retry runtime NEW_TOKEN was not exchanged");
#endif

  unsigned retryPkts = server.retryPkts;
  bool clientClosed = clientLink->close();
  ZuCHECK(clientClosed && !clientLink->cxn(),
    "retry runtime first connection did not close");
  waitDisconnect(server);

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
	    client.final();
	    server.stop();
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
  ZtString<> qlog = readFile_(qlogPath.cspan());
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
  bool mxStarted = mx.start();
  ZQUIC_CHECK_RT(mxStarted, "rejected-token runtime multiplexer start failed");
  if (!mxStarted) { client.final(); server.final(); return; }

  ZQUIC_CHECK_RT(server.start(), "rejected-token runtime server listen failed");
  ZQUIC_CHECK_RT(server.listening(),
    "rejected-token runtime server did not listen");
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
  ZQUIC_CHECK_RT(established,
    "rejected-token runtime initial handshake did not establish");
	  if (!established || !serverLink) {
	    client.final();
	    server.stop();
	    client.final();
	    server.final();
	    mx.stop();
	    return;
	  }
#ifdef Zquic_DEBUG
  ZQUIC_CHECK_RT(waitUntil([&clientLink, &serverLink]() {
      return diag(clientLink).rx.newTokenRx >= 1 &&
	diag(serverLink).tx.newTokenTx >= 1;
    }), "rejected-token runtime NEW_TOKEN was not exchanged");
#endif
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
  server.stop();
  ZQUIC_CHECK_RT(!server.connected(),
    "rejected-token runtime server remained connected");
  waitDisconnect(server);
  server.clearLinks();
  server.final();

  mx.stop();
  TestServer policyServer;
  ZQUIC_CHECK_RT(policyServer.init(
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
	.alpn(ZuSpan<ZuCSpan>{"h3"})),
    "rejected-token policy server init failed");
  bool policyMxStarted = mx.start();
  ZQUIC_CHECK_RT(policyMxStarted,
    "rejected-token policy multiplexer start failed");
  if (!policyMxStarted) { client.final(); policyServer.final(); return; }
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
  ZtString<> qlog = readFile_(qlogPath.cspan());
  ZtString<> policyQlog = readFile_(policyQlogPath.cspan());
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
	    client.final();
	    server.stop();
	    client.final();
	    server.final();
	    mx.stop();
	    return;
	  }

  ZuCHECK(clientLink->waitDisconnected(Zm::now(10)) && !clientLink->cxn(),
    "idle runtime client did not close on idle timeout");
  server.stop();
  waitDisconnect(server);

  clientLink = nullptr;
  serverLink = nullptr;
  server.clearLinks();
  client.final();
  server.final();

#ifdef Zquic_DEBUG
  ZtString<> qlog = readFile_(qlogPath.cspan());
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
  ZuTestCall(testEndpointOpen);
  ZuTestCall(testHandshakeCryptoLoss);
  ZuTestCall(testHandshakeKeyRetirement);
  ZuTestCall(testMigrationLoss);
  ZuTestCall(testSrvMultiCxn);
  ZuTestCall(testRetryAddrValidate);
  ZuTestCall(testRejectedTokenQLog);
  ZuTestCall(testIdleTimeoutQLog);
}
