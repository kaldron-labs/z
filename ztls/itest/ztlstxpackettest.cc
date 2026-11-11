//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <errno.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmAtomic.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZtString.hh>
#include <zlib/Ztls.hh>
#include <zlib/ZiLog.hh>

using namespace ZuTestUtil;

namespace ztlstxpackettest_ {

enum { PlainSize = 16384, PacketSize = 113, Timeout = 15 };

struct Cert {
  char dir[64] = "/tmp/ztlstxpackettest.XXXXXX";
  ZtString<> cert, key;
  bool created = false;
  bool init() {
    if (!mkdtemp(dir)) return false;
    created = true;
    cert << static_cast<const char *>(dir) << "/cert.pem";
    key << static_cast<const char *>(dir) << "/key.pem";
    ZtString<> cmd;
    cmd << "openssl req -x509 -newkey rsa:2048 -nodes -days 1 "
      "-subj /CN=localhost -addext basicConstraints=critical,CA:TRUE "
      "-addext subjectAltName=DNS:localhost,IP:127.0.0.1 -keyout " << key <<
      " -out " << cert << " >/dev/null 2>&1";
    return !system(cmd.data());
  }
  ~Cert() {
    if (!created) return;
    unlink(cert.data()); unlink(key.data()); rmdir(dir);
  }
};

enum { Normal, Partial, Exhaustion, Overflow, Limited, FlushFail, HandshakeFail };

struct FaultControl {
  ZmAtomic<unsigned> armed{0}, attempts{0}, live{0}, acquired{0}, released{0};
  unsigned failAt = unsigned(-1), limit = unsigned(-1);
  ZmAtomic<unsigned> depleted{0}, txDepleted{0};
  ZiMultiplex *mx = nullptr;
};
static FaultControl *faultControl = nullptr;

struct PacketFailure { };
struct TrackedPacket : ZiTxBuf {
  TrackedPacket(uint8_t *data, uint32_t size, Zi::IOBufAllocator_ *allocator,
    void *owner = nullptr, uint32_t length = 0, bool builtin = false,
    bool registered = false)
  : ZiTxBuf{data, size, allocator, owner, length, builtin, registered} {
    auto ctl = faultControl;
    if (ctl->live.xchAdd(1) >= ctl->limit) {
      --ctl->live; ++ctl->depleted;
      if (ctl->mx->invoked(4)) ++ctl->txDepleted;
      throw PacketFailure{};
    }
    if (ctl->armed.load_() && ctl->attempts.xchAdd(1) == ctl->failAt) {
      --ctl->live; throw PacketFailure{};
    }
    ++ctl->acquired;
  }
  ~TrackedPacket() { --faultControl->live; ++faultControl->released; }
};

struct SendControl {
  ZmAtomic<int> socket{-1};
  ZmAtomic<unsigned> shortWrites{0}, blocked{0}, injected{0};
  unsigned mode = Normal;
};
static SendControl *sendControl = nullptr;

struct State {
  uint8_t plain[PlainSize];
  ZmSemaphore listening, done;
  ZmAtomic<unsigned> errors{0}, packets{0}, protectedRecords{0};
  ZmAtomic<unsigned> cliBytes{0}, srvBytes{0};
  unsigned port = 0;
  unsigned mode = Normal;
  FaultControl fault;
  SendControl output;
  ZmAtomic<unsigned> rejected{0}, packetized{0}, cliConnected{0}, flushFailed{0};
  ZmRef<ZiIOBuf> retainedPlain;

  State() {
    for (unsigned i = 0; i < PlainSize; ++i) plain[i] = uint8_t(i * 31 + (i >> 4));
  }
  void fail() { ++errors; done.post(); }
};

template <typename Link> void sendPayload(Link &link)
{
  auto &state = link.app()->state;
  auto tx = link.txStream();
  tx << ZuSpan<const uint8_t>{state.plain, PlainSize} << Zi::flush();
  if (tx.failed()) { state.fail(); link.disconnect_(); }
}

template <typename Link>
int receivePayload(Link &link, Ztls::RxStream &rx, ZmAtomic<unsigned> &offset)
{
  auto &state = link.app()->state;
  auto before = offset.load_();
  uint64_t need = PlainSize - before, seen = 0;
  int64_t consumed = rx.consume(
    [&seen, need](ZuSpan<uint8_t> span) -> int64_t {
      seen += span.length();
      return seen < need ? 0 : span.length() - (seen - need);
    },
    [&state, &offset](ZuSpan<uint8_t> span) {
      unsigned off = offset.load_();
      if (span.length() > PlainSize - off ||
	memcmp(span.data(), state.plain + off, span.length())) state.fail();
      else offset = off + unsigned(span.length());
    });
  return consumed < 0 ? -1 : consumed ? 1 : 0;
}

template <typename Base> struct PacketHooks : Base {
  using Base::Base;
  void txProtected() { ++this->app()->state.protectedRecords; }
  void sent(ZmRef<ZiTxBuf> buf, bool ok) {
    Base::sent(buf, ok);
    auto &state = this->app()->state;
    if (!ok && (state.mode == Overflow || state.mode == Limited)) { ++state.rejected; return; }
    if (!ok || buf->allocator() != this->txPacketAllocator() ||
	buf->allocator() == this->txAllocator() || buf->skip ||
	buf->length > this->txPacketAllocator()->capacity()) state.fail();
    ++state.packets;
  }
  void txPacketized(const ZiIOBuf *, ZmRef<ZiIOBuf>) {
    ++this->app()->state.packetized;
  }
};

struct Client : Ztls::Client<Client> {
  State &state;
  explicit Client(State &s) : state{s} { }
  struct Link : PacketHooks<Ztls::CliLink<Client, Link>> {
    using Base = PacketHooks<Ztls::CliLink<Client, Link>>;
    using Base::Base;
    void connected(Ztls::Connected) {
      auto &state = this->app()->state;
      ++state.cliConnected;
      if (state.mode == Limited) { state.fail(); this->disconnect_(); return; }
      if (!this->cxn()->info().mps) { state.fail(); return; }
      state.output.socket = this->cxn()->info().socket;
      if (state.mode == Exhaustion || state.mode == FlushFail) {
	state.fault.armed = 1;
	auto buf = this->txAllocator()->allocBuf(this, PlainSize);
	memcpy(buf->data(), state.plain, PlainSize);
	state.retainedPlain = buf;
	this->app()->txRun([this, buf = ZuMv(buf)]() mutable {
	  auto &state = this->app()->state;
	  if (state.mode == FlushFail) {
	    auto tx = this->txStream_();
	    tx << ZuSpan<const uint8_t>{buf->data(), buf->length} << Zi::flush();
	    if (tx.failed()) { state.flushFailed = 1; this->disconnect(); }
	    else state.fail();
	  } else if (this->send_(ZuMv(buf))) state.fail();
	});
      } else {
	if (state.mode != Overflow && !this->updateKey_0(true)) { state.fail(); return; }
	sendPayload(*this);
      }
    }
    unsigned maxQueueLen() { return this->app()->state.mode == Overflow ? 2 : 1024; }
    void disconnected(bool) { this->app()->state.done.post(); }
    void connectFailed(bool) { this->app()->state.fail(); }
    int process(Ztls::RxStream &rx) {
      auto &state = this->app()->state;
      int n = receivePayload(*this, rx, state.cliBytes);
      if (state.cliBytes.load_() == PlainSize) this->disconnect_();
      return n;
    }
  };
};

struct Server : Ztls::Server<Server> {
  State &state;
  explicit Server(State &s) : state{s} { }
  struct Link : PacketHooks<Ztls::SrvLink<Server, Link>> {
    using Base = PacketHooks<Ztls::SrvLink<Server, Link>>;
    using Base::Base;
    void connected(Ztls::Connected) { }
    void disconnected(bool) { this->app()->state.done.post(); }
    int process(Ztls::RxStream &rx) {
      auto &state = this->app()->state;
      int n = receivePayload(*this, rx, state.srvBytes);
      if (state.srvBytes.load_() == PlainSize) sendPayload(*this);
      return n;
    }
  };
  ZiIP localIP() const { return ZiIP{"127.0.0.1"}; }
  unsigned localPort() const { return 0; }
  Link::Cxn *accepted(const ZiCxnInfo &info) { return new Link::Cxn{new Link{this}, info}; }
  void listening(const ZiListenInfo &info) { state.port = info.port; state.listening.post(); }
  void listenFailed(bool) { state.fail(); state.listening.post(); }
};

void workingPair(unsigned mode, unsigned failAt = unsigned(-1))
{
  ZuTestScopeRT(workingPair);
  static Cert cert;
  static bool ready = cert.init();
  ZuCheckRT(ready);
  if (!ready) return;
  State state;
  state.mode = state.output.mode = mode;
  state.fault.failAt = failAt;
  if (mode == HandshakeFail) state.fault.armed = 1;
  if (mode == Limited) state.fault.limit = 32;
  faultControl = &state.fault;
  sendControl = &state.output;
  ZiMultiplex mx{ZiMxParams{}.scheduler([](auto &s) {
    s.nThreads(4)
      .thread(1, [](auto &t) { t.isolated(1); })
      .thread(2, [](auto &t) { t.isolated(1); })
      .thread(3, [](auto &t) { t.isolated(1); })
      .thread(4, [](auto &t) { t.isolated(1); });
  }).rxThread(1).txThread(2)};
  state.fault.mx = &mx;
  Client client{state};
  Server server{state};
  auto cliRx = Client::Link::rxAllocator(&mx);
  auto cliTx = Client::Link::txAllocator(&mx);
  auto srvRx = Server::Link::rxAllocator(&mx);
  auto srvTx = Server::Link::txAllocator(&mx);
  auto cliPacket = mx.allocator<Zi::IOBufType::TxTCP, TrackedPacket,
    PacketSize, ZiIOBuf_DefltAlign, ZuStringT<"Ztls.ITest.Cli.Packet">>(1024);
  auto srvPacket = mx.allocator<Zi::IOBufType::TxTCP, Ztls::TxPacket,
    PacketSize, ZiIOBuf_DefltAlign, ZuStringT<"Ztls.ITest.Srv.Packet">>(1024);
  ZuCheckRT(cliTx->capacity() >= PlainSize && srvTx->capacity() >= PlainSize);
  ZuCheckRT(cliPacket->capacity() < PlainSize && srvPacket->capacity() < PlainSize);
  Ztls::ClientParams cliParams{&mx, "3", "4"};
  cliParams.rxAllocator(cliRx).txAllocator(cliTx).txPacketAllocator(cliPacket)
    .caPath(cert.cert.cspan());
  Ztls::ServerParams srvParams{&mx, "3", "4"};
  srvParams.rxAllocator(srvRx).txAllocator(srvTx).txPacketAllocator(srvPacket)
    .certPath(cert.cert.cspan()).keyPath(cert.key.cspan());
  bool cliInit = client.init(ZuMv(cliParams));
  bool srvInit = server.init(ZuMv(srvParams));
  ZuCheckRT(cliInit && srvInit);
  if (!cliInit || !srvInit) return;
  bool mxStarted = mx.start();
  ZuCheckRT(mxStarted);
  if (!mxStarted) return;
  bool cliStarted = client.start(), srvStarted = server.start();
  ZuCheckRT(cliStarted && srvStarted);
  if (cliStarted && srvStarted) {
    server.listen();
    bool listening = !state.listening.timedwait(Zm::now(Timeout));
    ZuCheckRT(listening && state.port);
    if (listening && state.port) {
      ZmRef<Client::Link> link = new Client::Link{&client};
      link->connect("127.0.0.1", state.port);
      ZuCheckRT(!state.done.timedwait(Zm::now(Timeout)));
      ZuCheckRT(!state.done.timedwait(Zm::now(Timeout)));
      ZuCheckRT(!state.errors.load_());
      if (mode == HandshakeFail) {
	ZuCheckRT(!state.cliConnected.load_() && state.fault.attempts.load_() == 1);
	ZuCheckRT(!state.protectedRecords.load_() && !state.packetized.load_());
	ZuCheckRT(!state.cliBytes.load_() && !state.srvBytes.load_() && !state.packets.load_());
      } else if (mode == Limited) {
	ZuCheckRT(state.fault.txDepleted.load_() && !state.cliConnected.load_());
	ZuCheckRT(!state.protectedRecords.load_() && !state.packetized.load_());
	ZuCheckRT(!state.cliBytes.load_() && !state.srvBytes.load_());
      } else if (mode == Exhaustion || mode == FlushFail) {
	ZuCheckRT(!state.protectedRecords.load_() && !state.packetized.load_());
	ZuCheckRT(!state.cliBytes.load_() && !state.srvBytes.load_());
	ZuCheckRT(state.fault.attempts.load_() == failAt + 1);
	if (mode == FlushFail) ZuCheckRT(state.flushFailed.load_());
	ZuCheckRT(state.retainedPlain && state.retainedPlain->length == PlainSize);
	ZuCheckRT(!memcmp(state.retainedPlain->data(), state.plain, PlainSize));
      } else if (mode == Overflow) {
	ZuCheckRT(state.rejected.load_() == 1);
	ZuCheckRT(state.protectedRecords.load_() == 1 && state.packetized.load_() == 1);
	ZuCheckRT(state.srvBytes.load_() < PlainSize && !state.cliBytes.load_());
	ZuCheckRT(state.output.shortWrites.load_() && state.output.blocked.load_());
      } else {
	ZuCheckRT(state.cliBytes.load_() == PlainSize && state.srvBytes.load_() == PlainSize);
	ZuCheckRT(state.protectedRecords.load_() == 2 && state.packetized.load_() == 2);
	ZuCheckRT(state.packets.load_() > 2 * PlainSize / cliPacket->capacity());
	if (mode == Partial)
	  ZuCheckRT(state.output.shortWrites.load_() && state.output.blocked.load_());
      }
    }
    server.stopListening();
  }
  if (cliStarted) ZuCheckRT(client.stop());
  if (srvStarted) ZuCheckRT(server.stop());
  client.final(); server.final();
  ZuCheckRT(mx.stop());
  ZuCheckRT(!state.fault.live.load_());
  ZuCheckRT(state.fault.acquired.load_() == state.fault.released.load_());
  faultControl = nullptr; sendControl = nullptr;
}

void reservationPairs()
{
  ZuTestScopeRT(reservationPairs);
  constexpr unsigned packetCapacity = Zi::BuiltinSize<
    ztlstxpackettest_::TrackedPacket, ZiIOBuf_DefltAlign>(ztlstxpackettest_::PacketSize);
  // TLS 1.3's header, inner type and 16-byte AEAD tag add 22 wire bytes.
  unsigned positions = (ztlstxpackettest_::PlainSize + 22 + packetCapacity - 1) /
    packetCapacity;
  for (unsigned i = 0; i < positions; ++i)
    ZuTestCallRT(ztlstxpackettest_::workingPair, ztlstxpackettest_::Exhaustion, i);
}

} // ztlstxpackettest_

// Constrain actual kernel writes on the established client socket. Every
// successful prefix is sent once by the kernel; retries retain its original
// ZiTx offset. Other sockets and handshake output pass through unchanged.
extern "C" __attribute__((visibility("default")))
ssize_t send(int fd, const void *buf, size_t length, int flags)
{
  using namespace ztlstxpackettest_;
  auto ctl = sendControl;
  if (ctl && fd == ctl->socket.load_() &&
      (ctl->mode == Partial || ctl->mode == Overflow)) {
    auto call = ctl->injected.xchAdd(1);
    if ((ctl->mode == Overflow && call) || call == 1) {
      ++ctl->blocked;
      errno = EAGAIN;
      return -1;
    }
    if (length > 7) {
      ssize_t n = syscall(SYS_sendto, fd, buf, 7, flags, nullptr, 0);
      if (n > 0) ++ctl->shortWrites;
      return n;
    }
  }
  return syscall(SYS_sendto, fd, buf, length, flags, nullptr, 0);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(ztlstxpackettest_::workingPair, ztlstxpackettest_::Normal);
  ZuTestCall(ztlstxpackettest_::workingPair, ztlstxpackettest_::Partial);
  ZuTestCall(ztlstxpackettest_::reservationPairs);
  ZuTestCall(ztlstxpackettest_::workingPair, ztlstxpackettest_::Overflow);
  ZuTestCall(ztlstxpackettest_::workingPair, ztlstxpackettest_::Limited);
  ZuTestCall(ztlstxpackettest_::workingPair, ztlstxpackettest_::FlushFail, 0);
  ZuTestCall(ztlstxpackettest_::workingPair, ztlstxpackettest_::HandshakeFail, 0);
  ZiLog::stop();
}
