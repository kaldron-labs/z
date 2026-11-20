//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// The fixture uses an RSA method to pause within a real EVP async job.
#define OPENSSL_SUPPRESS_DEPRECATED
#include <fcntl.h>
#include <openssl/pem.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmAtomic.hh>
#include <zlib/ZmSemaphore.hh>

#include <zlib/Ztls.hh>
#include <zlib/ZtlsEngineOpenSSL.hh>

#include "ZtlsPacketCert.hh"

using namespace ZuTestUtil;

namespace ztlsrxpackettest_ {

enum { PayloadSize = (32 << 10) + 37, Timeout = 15 };

struct InputStats {
  ZmAtomic<unsigned>	acquired{0}, released{0}, errors{0};
};
static InputStats *inputStats;

struct Input : ZiRxQueue::Node {
  unsigned	originalSize;
  Input(
    uint8_t *data, uint32_t size, Zi::IOBufAllocator_ *allocator, void *owner = nullptr,
    uint32_t length = 0, bool builtin = false, bool registered = false)
  :
    ZiRxQueue::Node{data, size, allocator, owner, length, builtin, registered}, originalSize{size}
  { ++inputStats->acquired; }
  ~Input()
  {
    if (size != originalSize || registered()) ++inputStats->errors;
    ++inputStats->released;
  }
};

struct State {
  uint8_t		payload[PayloadSize];
  ZmSemaphore		listening, done;
  ZmAtomic<unsigned>	errors{0}, cliBytes{0}, srvBytes{0}, records{0};
  InputStats		input;
  Zi::IOBufAllocator	*cliRx = nullptr, *srvRx = nullptr;
  ZmRef<ZiIOBuf>	retained;
  Ztls::RxStream	cliFrames, srvFrames;
  unsigned		port = 0;
  bool			cancelAsync = false;
  ZmAtomic<unsigned>	asyncStarted{0}, asyncReleased{0}, connected{0};
  State()
  {
    for (unsigned i = 0; i < PayloadSize; ++i) payload[i] = uint8_t(i * 31 + (i >> 8));
  }
  void fail()
  {
    ++errors;
    done.post();
  }
};

template <typename Link> void sendPayload(Link &link)
{
  auto tx = link.txStream();
  tx << ZuSpan<const uint8_t>{link.app()->state.payload, PayloadSize} << Zi::flush();
  if (tx.failed()) {
    link.app()->state.fail();
    link.disconnect_();
  }
}

template <typename Link> int receivePayload(Link &link, Ztls::RxStream &rx, bool client)
{
  auto &state = link.app()->state;
  auto &bytes = client ? state.cliBytes : state.srvBytes;
  auto	expected = client ? state.cliRx : state.srvRx;
  auto &frames = client ? state.cliFrames : state.srvFrames;
  while (rx.length()) {
    ZmRef<ZiIOBuf> record;
    auto	   n = rx.extract([](ZuBSpan span) -> int64_t {
      return span.length();
    }, []() -> ZmRef<ZiRxQueue::Node> { return nullptr; }, record);
    if (n <= 0 || !record || record->allocator() != expected || record->registered() ||
      record->length > Ztls_RxAppCapacity ||
      record->size - record->skip - record->allocator()->tailRoom < Ztls_::RxRecordCapacity) {
      state.fail();
      return -1;
    }
    if (!client && !state.retained) state.retained = record;
    ++state.records;
    frames.push(ZmRef<ZiRxQueue::Node>{record.template ptr<ZiRxQueue::Node>()});
  }
  // Application framing deliberately spans several authenticated TLS records.
  uint64_t seen = 0;
  int64_t  consumed = frames.consume(
    [&seen](ZuSpan<uint8_t> span) -> int64_t
  {
    seen += span.length();
    return seen < PayloadSize ? 0 : span.length() - (seen - PayloadSize);
  }, [&state, &bytes](ZuSpan<uint8_t> span)
  {
    if (span.length() != PayloadSize || memcmp(span.data(), state.payload, PayloadSize))
      state.fail();
    else
      bytes = PayloadSize;
  });
  return consumed < 0 ? -1 : consumed ? 1 : 0;
}

struct Client : Ztls::Client<Client> {
  State	&state;
  explicit Client(State &s) : state{s} { }
  struct Link : ZmNHeap<Link>, Ztls::CliLink<Client, Link> {
  friend inline ZuStringT<"Ztls.RxPair.CliLink"> ZmNHeapID(Link *);
    using Base = Ztls::CliLink<Client, Link>;
    using Base::Base;
    void connected(Ztls::Connected)
    {
      ++this->app()->state.connected;
      if (!this->updateKey_0(true)) {
	this->app()->state.fail();
	return;
      }
      sendPayload(*this);
    }
    void disconnected(bool) { this->app()->state.done.post(); }
    void connectFailed(bool) { this->app()->state.fail(); }
    int process(Ztls::RxStream &rx)
    {
      int n = receivePayload(*this, rx, true);
      if (this->app()->state.cliBytes.load_() == PayloadSize) this->disconnect_();
      return n;
    }
  };
};

struct Server : Ztls::Server<Server> {
  State	&state;
  explicit Server(State &s) : state{s} { }
  struct Link : ZmNHeap<Link>, Ztls::SrvLink<Server, Link> {
  friend inline ZuStringT<"Ztls.RxPair.SrvLink"> ZmNHeapID(Link *);
    using Base = Ztls::SrvLink<Server, Link>;
    using Base::Base;
    void connected(Ztls::Connected) { }
    void disconnected(bool) { this->app()->state.done.post(); }
    int process(Ztls::RxStream &rx)
    {
      int n = receivePayload(*this, rx, false);
      if (this->app()->state.srvBytes.load_() == PayloadSize) sendPayload(*this);
      return n;
    }
  };
  // Software OpenSSL keys may finish inside ASYNC_start_job. This signer
  // deliberately suspends once on an fd, then delegates the actual signature
  // to the configured key callback, making owner resume/teardown observable.
  struct SignJob : ZmNHeap<SignJob> {
  friend inline ZuStringT<"Ztls.RxPair.SignJob"> ZmNHeapID(SignJob *);
    State			*state;
    Ztls_::SignCertificate	*original;
    int				fd[2];
    ZuDerive(
      Input,
      (ZtBuiltin<ZtArray<uint8_t, ZtArrayHeapID<"Ztls.RxPair.SignInput">>, Ztls_SignInputSize>));
    Input			input;
    Ztls_::SignatureAlgorithms	algorithms;
    SignJob(
      State *state_, Ztls_::SignCertificate *original_, Ztls_::Bytes bytes,
      ZuSpan<const uint16_t> algs)
    :
      state{state_}, original{original_}, fd{-1, -1}
    {
      input.append(bytes.data(), bytes.length());
      algorithms.append(algs.data(), algs.length());
    }
    ~SignJob()
    {
      if (fd[0] >= 0) ::close(fd[0]);
      if (fd[1] >= 0) ::close(fd[1]);
      ++state->asyncReleased;
    }
  };
  struct Sign : Ztls_::SignCertificate {
    Server			*owner = nullptr;
    Ztls_::SignCertificate	*original = nullptr;
    int call(
      Ztls::Cxn *tls, bool allowAsync, uint16_t *algorithm, Ztls_::Buffer *output,
      Ztls_::Bytes input, ZuSpan<const uint16_t> algorithms) override
    {
      auto self = this;
      if (!allowAsync) return Ztls_ErrorLibrary;
      SignJob *job = nullptr;
      try {
	job = new SignJob{&self->owner->state, self->original, input, algorithms};
      } catch (...) {
	return Ztls_ErrorNoMemory;
      }
      if (pipe(job->fd)) {
	delete job;
	return Ztls_ErrorLibrary;
      }
      tls->signatureFd = job->fd[0];
      tls->signatureRetire = [job]()
      {
	delete job;
	return true;
      };
      tls->signatureResume = [tls, job](Ztls_::Buffer *output, uint16_t *algorithm)
      {
	auto original = job->original;
	int  n = original->call(tls, false, algorithm, output, job->input, job->algorithms);
	tls->retireSignature();
	return n;
      };
      ++self->owner->state.asyncStarted;
      if (self->owner->state.cancelAsync) {
	self->owner->state.cancelAsync = false;
	auto link = static_cast<Link *>(*Ztls_::dataPtr(tls));
	// Queue close after the suspended operation returns to its owner.
	self->owner->rxRun([link]() { link->disconnect_(); });
      }
      uint8_t ready = 1;
      if (::write(job->fd[1], &ready, 1) != 1) return Ztls_ErrorLibrary;
      return Ztls_ErrorAsyncOperation;
    }
  } sign;
  void signer(Ztls::SignCert *sign) { ctx()->signCertificate = Ztls::signCertHook(sign); }
  void observeAsync()
  {
    sign.owner = this;
    sign.original = this->ctx()->signCertificate;
    this->ctx()->signCertificate = &sign;
  }
  ZiIP localIP() const { return ZiIP{"127.0.0.1"}; }
  unsigned localPort() const { return 0; }
  Link::Cxn *accepted(const ZiCxnInfo &info) { return new Link::Cxn{new Link{this}, info}; }
  void listening(const ZiListenInfo &info)
  {
    state.port = info.port;
    state.listening.post();
  }
  void listenFailed(bool)
  {
    state.fail();
    state.listening.post();
  }
};

#if ZtlsOpenSSLHaveAsync
// Use a test RSA method to pause inside the EVP_DigestSignFinal invoked by
// the production OpenSSL signer. Each pause publishes a different read fd.
struct NativePauses {
  ZmSemaphore		paused, allowRegistration, rxProgress, stopped;
  ZmAtomic<unsigned>	released{0};
  int		     pipes[2][2]{
    {-1, -1},
    {-1, -1}
  };
  RSA_METHOD		*method = nullptr;
  Ztls::SignCert	*sign = nullptr;

  static int index()
  {
    static int value = RSA_get_ex_new_index(0, nullptr, nullptr, nullptr, nullptr);
    return value;
  }

  static int encrypt(int len, const unsigned char *from, unsigned char *to, RSA *rsa, int padding)
  {
    auto self = static_cast<NativePauses *>(RSA_get_ex_data(rsa, index()));
    auto job = ASYNC_get_current_job();
    if (!job || !self) return -1;
    auto ctx = ASYNC_get_wait_ctx(job);
    for (unsigned i = 0; i < 2; ++i) {
      auto &pair = self->pipes[i];
      if (!ASYNC_WAIT_CTX_set_wait_fd(
	    ctx, &pair, pair[0], self, [](ASYNC_WAIT_CTX *, const void *, int, void *data)
      { ++static_cast<NativePauses *>(data)->released; }))
	return -1;
      self->paused.post();
      if (!ASYNC_pause_job()) return -1;
      uint8_t ready;
      if (::read(pair[0], &ready, 1) != 1) return -1;
      if (!i && !ASYNC_WAIT_CTX_clear_fd(ctx, &pair)) return -1;
    }
    return RSA_meth_get_priv_enc(RSA_PKCS1_OpenSSL())(len, from, to, rsa, padding);
  }

  bool init(const char *path, Server &server)
  {
    for (auto &pair : pipes)
      if (::pipe2(pair, O_CLOEXEC | O_NONBLOCK)) return false;
    auto pem = BIO_new_file(path, "r");
    if (!pem) return false;
    auto rsa = PEM_read_bio_RSAPrivateKey(pem, nullptr, nullptr, nullptr);
    BIO_free(pem);
    if (!rsa) return false;
    ZuGuard freeRSA{[rsa]() { RSA_free(rsa); }};
    method = RSA_meth_dup(RSA_PKCS1_OpenSSL());
    if (!method || !RSA_meth_set_priv_enc(method, encrypt) || !RSA_set_method(rsa, method) ||
      !RSA_set_ex_data(rsa, index(), this))
      return false;
    Ztls::PK::Key key;
    key.reset(EVP_PKEY_new());
    if (!key.get() || !EVP_PKEY_set1_RSA(key.get(), rsa)) return false;
    sign = Ztls::signCertNew(key);
    if (!sign || !Ztls::signCertAsync(sign, true)) return false;
    server.signer(sign);
    return true;
  }
  ~NativePauses()
  {
    Ztls::signCertFree(sign);
    RSA_meth_free(method);
    for (auto &pair : pipes)
      for (auto fd : pair)
	if (fd >= 0) ::close(fd);
  }
  void signal(unsigned i)
  {
    uint8_t ready = 1;
    ZuCheckRT(::write(pipes[i][1], &ready, 1) == 1);
  }
};
#endif

enum { Synchronous, NativeAsync, PausedAsync, CancelAsync, RetireNative };

void workingPair(bool separateOwner, unsigned signing = Synchronous)
{
  bool async = signing != Synchronous;
  bool mocked = signing == PausedAsync || signing == CancelAsync;
  bool cancel = signing == CancelAsync;
  ZuTestScopeRT(workingPair);
  ZtlsPacketCert cert;
  bool		 ready = cert.init();
  ZuCheckRT(ready);
  if (!ready) return;
  State state;
  inputStats = &state.input;
  state.cancelAsync = cancel;
  ZiMultiplex mx{ZiMxParams{}
      .scheduler(
	[](auto &s)
  {
    s.nThreads(5).thread(1, [](auto &t) {
      t.isolated(1);
    }).thread(2, [](auto &t) {
      t.isolated(1);
    }).thread(3, [](auto &t) {
      t.isolated(1);
    }).thread(4, [](auto &t) {
      t.isolated(1);
    }).thread(5, [](auto &t) { t.isolated(1); });
  })
      .rxThread(1)
      .txThread(2)};
  Client      client{state};
  Server      server{state};
  state.cliRx = Client::Link::rxAllocator(&mx);
  state.srvRx = Server::Link::rxAllocator(&mx);
  auto cliInput = mx.allocator<
    Zi::IOBufType::Rx, Input, 23, ZiIOBuf_DefltAlign, ZuStringT<"Ztls.RxPair.Cli">>();
  auto srvInput = mx.allocator<
    Zi::IOBufType::Rx, Input, 97, ZiIOBuf_DefltAlign, ZuStringT<"Ztls.RxPair.Srv">>();
  auto		     packets = mx.allocator<Zi::IOBufType::TxTCP, Ztls::TxPacket, 71>();
  auto		     owner = separateOwner ? "3" : "1";
  Ztls::ClientParams cliParams{&mx, owner, "4"};
  cliParams.rxAllocator(state.cliRx)
    .txAllocator(Client::Link::txAllocator(&mx))
    .txPacketAllocator(packets)
    .socketRxAllocator(cliInput)
    .caPath(cert.cert.cspan());
  Ztls::ServerParams srvParams{&mx, owner, "4"};
  srvParams.rxAllocator(state.srvRx)
    .txAllocator(Server::Link::txAllocator(&mx))
    .txPacketAllocator(packets)
    .socketRxAllocator(srvInput)
    .certPath(cert.cert.cspan())
    .keyPath(cert.key.cspan());
  if (async) srvParams.asyncThread("5");
  bool cliInit = client.init(ZuMv(cliParams)), srvInit = server.init(ZuMv(srvParams));
  ZuCheckRT(cliInit && srvInit);
  if (!cliInit || !srvInit) return;
  if (mocked) server.observeAsync();
#if ZtlsOpenSSLHaveAsync
  NativePauses native;
  if (signing == RetireNative) {
    bool installed = native.init(cert.key.data(), server);
    ZuCheckRT(installed);
    if (!installed) return;
  }
#endif
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
#if ZtlsOpenSSLHaveAsync
	if (signing == RetireNative) {
	  mx.run([&native]() { native.allowRegistration.wait(); }, 5);
	  link->connect("127.0.0.1", state.port);
	  ZuCheckRT(!native.paused.timedwait(Zm::now(Timeout)));
	  server.stop([&state, &native](bool ok)
	  {
	    if (!ok) ++state.errors;
	    native.stopped.post();
	  });
	  ZuCheckRT(!state.done.timedwait(Zm::now(Timeout)));
	  native.allowRegistration.post();
	  native.signal(0);
	  ZuCheckRT(!native.paused.timedwait(Zm::now(Timeout)));
	  server.rxRun([&native]() { native.rxProgress.post(); });
	  ZuCheckRT(!native.rxProgress.timedwait(Zm::now(Timeout)));
	  ZuCheckRT(native.stopped.trywait() != 0 && !native.released.load_());
	  native.signal(1);
	  ZuCheckRT(!native.stopped.timedwait(Zm::now(Timeout)));
	  ZuCheckRT(native.released.load_() == 1 && !state.errors.load_());
	  ZuCheckRT(!state.done.timedwait(Zm::now(Timeout)));
	  ZuCheckRT(client.stop());
	  client.final();
	  server.final();
	  ZuCheckRT(mx.stop());
	  inputStats = nullptr;
	  return;
	}
#endif
	link->connect("127.0.0.1", state.port);
	ZuCheckRT(!state.done.timedwait(Zm::now(Timeout)));
	ZuCheckRT(!state.done.timedwait(Zm::now(Timeout)));
	ZuCheckRT(!state.errors.load_());
	if (cancel && !pass) {
	  ZuCheckRT(state.asyncStarted.load_() && !state.connected.load_());
	  ZuCheckRT(!state.cliBytes.load_() && !state.srvBytes.load_());
	} else {
	  ZuCheckRT(
	    state.cliBytes.load_() == PayloadSize && state.srvBytes.load_() == PayloadSize);
	  ZuCheckRT(state.records.load_() >= 6);
	}
      }
      server.stopListening();
    }
    if (cliStarted) ZuCheckRT(client.stop());
    if (srvStarted) ZuCheckRT(server.stop());
  }
  if (mocked) ZuCheckRT(state.asyncStarted.load_() >= (cancel ? 2U : 1U));
  client.final();
  server.final();
  ZuCheckRT(mx.stop());
  ZuCheckRT(state.asyncStarted.load_() == state.asyncReleased.load_());
  ZuCheckRT(state.retained && state.retained->allocator() == state.srvRx);
  if (state.retained)
    ZuCheckRT(!memcmp(state.retained->data(), state.payload, state.retained->length));
  state.retained = nullptr;
  ZuCheckRT(state.input.acquired.load_() > 6 && !state.input.errors.load_());
  ZuCheckRT(state.input.acquired.load_() == state.input.released.load_());
  ZuCheckRT(
    cliInput->capacity() < Ztls_RxAppCapacity && srvInput->capacity() < Ztls_RxAppCapacity);
  inputStats = nullptr;
}

} // namespace ztlsrxpackettest_

int main(int argc, char **argv)
{
  ZuTestUtil::parse(argc, argv);
  ZuTestMain();
  ZuTestCall(ztlsrxpackettest_::workingPair, false);
  ZuTestCall(ztlsrxpackettest_::workingPair, true);
#if defined(ZtlsOpenSSLHaveAsync) && ZtlsOpenSSLHaveAsync
  using namespace ztlsrxpackettest_;
  ZuTestCall(workingPair, true, NativeAsync);
  ZuTestCall(workingPair, false, PausedAsync);
  ZuTestCall(workingPair, true, PausedAsync);
  ZuTestCall(workingPair, true, CancelAsync);
  ZuTestCall(workingPair, true, RetireNative);
#endif
}
