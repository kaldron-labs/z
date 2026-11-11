//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zpicotls/openssl.h>
#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmAtomic.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/Ztls.hh>

using namespace ZuTestUtil;

namespace ztlsrxpackettest_ {

enum { PayloadSize = (32 << 10) + 37, Timeout = 15 };

struct InputStats {
  ZmAtomic<unsigned> acquired{0}, released{0}, errors{0};
};
static InputStats *inputStats;

struct Input : ZiRxQueue::Node {
  unsigned originalSize;
  Input(uint8_t *data, uint32_t size, Zi::IOBufAllocator_ *allocator,
    void *owner = nullptr, uint32_t length = 0, bool builtin = false,
    bool registered = false)
  : ZiRxQueue::Node{data, size, allocator, owner, length, builtin, registered},
    originalSize{size} {
    ++inputStats->acquired;
  }
  ~Input() {
    if (size != originalSize || registered()) ++inputStats->errors;
    ++inputStats->released;
  }
};

struct Cert {
  char dir[64] = "/tmp/ztlsrxpackettest.XXXXXX";
  ZtString<> cert, key;
  bool created = false;
  bool init() {
    if (!mkdtemp(dir)) return false;
    created = true;
    cert << static_cast<const char *>(dir) << "/cert.pem";
    key << static_cast<const char *>(dir) << "/key.pem";
    ZtString<> command;
    command << "openssl req -x509 -newkey rsa:2048 -nodes -days 1 "
      "-subj /CN=localhost -addext basicConstraints=critical,CA:TRUE "
      "-addext subjectAltName=DNS:localhost,IP:127.0.0.1 -keyout " << key <<
      " -out " << cert << " >/dev/null 2>&1";
    return !system(command.data());
  }
  ~Cert() {
    if (!created) return;
    unlink(cert.data()); unlink(key.data()); rmdir(dir);
  }
};

struct State {
  uint8_t payload[PayloadSize];
  ZmSemaphore listening, done;
  ZmAtomic<unsigned> errors{0}, cliBytes{0}, srvBytes{0}, records{0};
  InputStats input;
  Zi::IOBufAllocator *cliRx = nullptr, *srvRx = nullptr;
  ZmRef<ZiIOBuf> retained;
  Ztls::RxStream cliFrames, srvFrames;
  unsigned port = 0;
  bool cancelAsync = false;
  ZmAtomic<unsigned> asyncStarted{0}, asyncReleased{0}, connected{0};
  State() {
    for (unsigned i = 0; i < PayloadSize; ++i) payload[i] = uint8_t(i * 31 + (i >> 8));
  }
  void fail() { ++errors; done.post(); }
};

template <typename Link> void sendPayload(Link &link)
{
  auto tx = link.txStream();
  tx << ZuSpan<const uint8_t>{link.app()->state.payload, PayloadSize} << Zi::flush();
  if (tx.failed()) { link.app()->state.fail(); link.disconnect_(); }
}

template <typename Link>
int receivePayload(Link &link, Ztls::RxStream &rx, bool client)
{
  auto &state = link.app()->state;
  auto &bytes = client ? state.cliBytes : state.srvBytes;
  auto expected = client ? state.cliRx : state.srvRx;
  auto &frames = client ? state.cliFrames : state.srvFrames;
  while (rx.length()) {
    ZmRef<ZiIOBuf> record;
    auto n = rx.extract(
      [](ZuBSpan span) -> int64_t { return span.length(); },
      []() -> ZmRef<ZiRxQueue::Node> { return nullptr; }, record);
    if (n <= 0 || !record || record->allocator() != expected || record->registered() ||
	record->length > PTLS_RECEIVE_APPLICATION_CAPACITY ||
	record->size - record->skip - record->allocator()->tailRoom < Ztls_::RxRecordCapacity) {
      state.fail(); return -1;
    }
    if (!client && !state.retained) state.retained = record;
    ++state.records;
    frames.push(ZmRef<ZiRxQueue::Node>{record.template ptr<ZiRxQueue::Node>()});
  }
  // Application framing deliberately spans several authenticated TLS records.
  uint64_t seen = 0;
  int64_t consumed = frames.consume(
    [&seen](ZuSpan<uint8_t> span) -> int64_t {
      seen += span.length();
      return seen < PayloadSize ? 0 : span.length() - (seen - PayloadSize);
    },
    [&state, &bytes](ZuSpan<uint8_t> span) {
      if (span.length() != PayloadSize || memcmp(span.data(), state.payload, PayloadSize))
	state.fail();
      else bytes = PayloadSize;
    });
  return consumed < 0 ? -1 : consumed ? 1 : 0;
}

struct Client : Ztls::Client<Client> {
  State &state;
  explicit Client(State &s) : state{s} { }
  struct Link : Ztls::CliLink<Client, Link> {
    using Base = Ztls::CliLink<Client, Link>;
    using Base::Base;
    void connected(Ztls::Connected) {
      ++this->app()->state.connected;
      if (!this->updateKey_0(true)) { this->app()->state.fail(); return; }
      sendPayload(*this);
    }
    void disconnected(bool) { this->app()->state.done.post(); }
    void connectFailed(bool) { this->app()->state.fail(); }
    int process(Ztls::RxStream &rx) {
      int n = receivePayload(*this, rx, true);
      if (this->app()->state.cliBytes.load_() == PayloadSize) this->disconnect_();
      return n;
    }
  };
};

struct Server : Ztls::Server<Server> {
  State &state;
  explicit Server(State &s) : state{s} { }
  struct Link : Ztls::SrvLink<Server, Link> {
    using Base = Ztls::SrvLink<Server, Link>;
    using Base::Base;
    void connected(Ztls::Connected) { }
    void disconnected(bool) { this->app()->state.done.post(); }
    int process(Ztls::RxStream &rx) {
      int n = receivePayload(*this, rx, false);
      if (this->app()->state.srvBytes.load_() == PayloadSize) sendPayload(*this);
      return n;
    }
  };
  // Software OpenSSL keys may finish inside ASYNC_start_job. This signer
  // deliberately suspends once on an fd, then delegates the actual signature
  // to the configured key callback, making owner resume/teardown observable.
  struct SignJob : ptls_async_job_t, ZmNHeap<SignJob> {
  friend inline ZuStringT<"Ztls.RxPair.SignJob"> ZmNHeapID(SignJob *);
    State *state;
    int fd[2];
    uint8_t input[256];
    uint16_t algorithms[64];
    size_t length, count;
    SignJob(State *state_, ptls_iovec_t bytes, const uint16_t *algs, size_t n)
    : ptls_async_job_t{}, state{state_}, length{bytes.len}, count{n} {
      fd[0] = fd[1] = -1;
      destroy_ = [](ptls_async_job_t *base) { delete static_cast<SignJob *>(base); };
      get_fd = [](ptls_async_job_t *base) { return static_cast<SignJob *>(base)->fd[0]; };
      memcpy(input, bytes.base, bytes.len);
      memcpy(algorithms, algs, n * sizeof(uint16_t));
    }
    ~SignJob() {
      if (fd[0] >= 0) ::close(fd[0]);
      if (fd[1] >= 0) ::close(fd[1]);
      ++state->asyncReleased;
    }
  };
  struct Sign : ptls_sign_certificate_t {
    Server *owner = nullptr;
    ptls_sign_certificate_t *original = nullptr;
    Sign() {
      cb = [](ptls_sign_certificate_t *base, ptls_t *tls, ptls_async_job_t **async,
	uint16_t *algorithm, ptls_buffer_t *output, ptls_iovec_t input,
	const uint16_t *algorithms, size_t count) -> int {
	auto self = static_cast<Sign *>(base);
	if (!async) return PTLS_ERROR_LIBRARY;
	if (*async) {
	  auto job = static_cast<SignJob *>(*async);
	  int n = self->original->cb(self->original, tls, nullptr, algorithm,
	    output, ptls_iovec_init(job->input, job->length), job->algorithms, job->count);
	  delete job; *async = nullptr;
	  return n;
	}
	if (input.len > sizeof(SignJob::input) || count > 64) return PTLS_ERROR_LIBRARY;
	SignJob *job = nullptr;
	try { job = new SignJob{&self->owner->state, input, algorithms, count}; }
	catch (...) { return PTLS_ERROR_NO_MEMORY; }
	if (pipe(job->fd)) { delete job; return PTLS_ERROR_LIBRARY; }
	*async = job;
	++self->owner->state.asyncStarted;
	if (self->owner->state.cancelAsync) {
	  self->owner->state.cancelAsync = false;
	  auto link = static_cast<Link *>(*ptls_get_data_ptr(tls));
	  // Queue close after the suspended operation returns to its owner.
	  self->owner->rxRun([link]() { link->disconnect_(); });
	}
	uint8_t ready = 1;
	if (::write(job->fd[1], &ready, 1) != 1) return PTLS_ERROR_LIBRARY;
	return PTLS_ERROR_ASYNC_OPERATION;
      };
    }
  } sign;
  void observeAsync() {
    sign.owner = this;
    sign.original = this->ctx()->sign_certificate;
    this->ctx()->sign_certificate = &sign;
  }
  ZiIP localIP() const { return ZiIP{"127.0.0.1"}; }
  unsigned localPort() const { return 0; }
  Link::Cxn *accepted(const ZiCxnInfo &info) { return new Link::Cxn{new Link{this}, info}; }
  void listening(const ZiListenInfo &info) { state.port = info.port; state.listening.post(); }
  void listenFailed(bool) { state.fail(); state.listening.post(); }
};

void workingPair(bool separateOwner, bool async = false, bool cancel = false)
{
  ZuTestScopeRT(workingPair);
  Cert cert;
  bool ready = cert.init();
  ZuCheckRT(ready);
  if (!ready) return;
  State state;
  inputStats = &state.input;
  state.cancelAsync = cancel;
  ZiMultiplex mx{ZiMxParams{}.scheduler([](auto &s) {
    s.nThreads(5)
      .thread(1, [](auto &t) { t.isolated(1); })
      .thread(2, [](auto &t) { t.isolated(1); })
      .thread(3, [](auto &t) { t.isolated(1); })
      .thread(4, [](auto &t) { t.isolated(1); })
      .thread(5, [](auto &t) { t.isolated(1); });
  }).rxThread(1).txThread(2)};
  Client client{state};
  Server server{state};
  state.cliRx = Client::Link::rxAllocator(&mx);
  state.srvRx = Server::Link::rxAllocator(&mx);
  auto cliInput = mx.allocator<Zi::IOBufType::RxTCP, Input, 23,
    ZiIOBuf_DefltAlign, ZuStringT<"Ztls.RxPair.Cli">>();
  auto srvInput = mx.allocator<Zi::IOBufType::RxTCP, Input, 97,
    ZiIOBuf_DefltAlign, ZuStringT<"Ztls.RxPair.Srv">>();
  auto packets = mx.allocator<Zi::IOBufType::TxTCP, Ztls::TxPacket, 71>();
  auto owner = separateOwner ? "3" : "1";
  Ztls::ClientParams cliParams{&mx, owner, "4"};
  cliParams.rxAllocator(state.cliRx).txAllocator(Client::Link::txAllocator(&mx))
    .txPacketAllocator(packets).socketRxAllocator(cliInput).caPath(cert.cert.cspan());
  Ztls::ServerParams srvParams{&mx, owner, "4"};
  srvParams.rxAllocator(state.srvRx).txAllocator(Server::Link::txAllocator(&mx))
    .txPacketAllocator(packets).socketRxAllocator(srvInput)
    .certPath(cert.cert.cspan()).keyPath(cert.key.cspan());
  if (async) srvParams.asyncThread("5");
  bool cliInit = client.init(ZuMv(cliParams)), srvInit = server.init(ZuMv(srvParams));
  ZuCheckRT(cliInit && srvInit);
  if (!cliInit || !srvInit) return;
  if (async) server.observeAsync();
  bool started = mx.start();
  ZuCheckRT(started);
  if (!started) return;
  for (unsigned pass = 0; pass < (cancel ? 2U : 1U); ++pass) {
    bool cliStarted = client.start(), srvStarted = server.start();
    ZuCheckRT(cliStarted && srvStarted);
    if (cliStarted && srvStarted) {
      state.port = 0;
      server.listen();
      bool listening = !state.listening.timedwait(Zm::now(Timeout));
      ZuCheckRT(listening && state.port);
      if (listening && state.port) {
	ZmRef<Client::Link> link = new Client::Link{&client};
	link->connect("127.0.0.1", state.port);
	ZuCheckRT(!state.done.timedwait(Zm::now(Timeout)));
	ZuCheckRT(!state.done.timedwait(Zm::now(Timeout)));
	ZuCheckRT(!state.errors.load_());
	if (cancel && !pass) {
	  ZuCheckRT(state.asyncStarted.load_() && !state.connected.load_());
	  ZuCheckRT(!state.cliBytes.load_() && !state.srvBytes.load_());
	} else {
	  ZuCheckRT(state.cliBytes.load_() == PayloadSize && state.srvBytes.load_() == PayloadSize);
	  ZuCheckRT(state.records.load_() >= 6);
	}
      }
      server.stopListening();
    }
    if (cliStarted) ZuCheckRT(client.stop());
    if (srvStarted) ZuCheckRT(server.stop());
  }
  if (async) ZuCheckRT(state.asyncStarted.load_() >= (cancel ? 2U : 1U));
  client.final(); server.final();
  ZuCheckRT(mx.stop());
  ZuCheckRT(state.asyncStarted.load_() == state.asyncReleased.load_());
  ZuCheckRT(state.retained && state.retained->allocator() == state.srvRx);
  if (state.retained)
    ZuCheckRT(!memcmp(state.retained->data(), state.payload, state.retained->length));
  state.retained = nullptr;
  ZuCheckRT(state.input.acquired.load_() > 6 && !state.input.errors.load_());
  ZuCheckRT(state.input.acquired.load_() == state.input.released.load_());
  ZuCheckRT(cliInput->capacity() < PTLS_RECEIVE_APPLICATION_CAPACITY &&
    srvInput->capacity() < PTLS_RECEIVE_APPLICATION_CAPACITY);
  inputStats = nullptr;
}

} // ztlsrxpackettest_

int main(int argc, char **argv)
{
  ZuTestUtil::parse(argc, argv);
  ZuTestMain();
  ZuTestCall(ztlsrxpackettest_::workingPair, false);
  ZuTestCall(ztlsrxpackettest_::workingPair, true);
#if defined(PTLS_OPENSSL_HAVE_ASYNC) && PTLS_OPENSSL_HAVE_ASYNC
  ZuTestCall(ztlsrxpackettest_::workingPair, false, true);
  ZuTestCall(ztlsrxpackettest_::workingPair, true, true);
  ZuTestCall(ztlsrxpackettest_::workingPair, true, true, true);
#endif
}
