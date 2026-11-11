//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>
#include <zpicotls/openssl.h>
#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/Ztls.hh>

using namespace ZuTestUtil;

namespace ZtlsRxStubTest_ {

enum { Head = 13, Tail = 7, Capacity = 31, Slots = 8, Payload = 257, Timeout = 10 };

struct Backing : Zi::IOBufPlugin, ZmNHeap<Backing> {
friend inline ZuStringT<"Ztls.RxStub.Backing"> ZmNHeapID(Backing *);
  uint8_t bytes[Slots][Head + Capacity + Tail];
  bool used[Slots]{};
  ZmAtomic<unsigned> acquired{0}, released{0}, errors{0};
  Backing() { size = sizeof(bytes[0]); headRoom = Head; tailRoom = Tail; }
  uint8_t *alloc() override {
    for (unsigned i = 0; i < Slots; ++i) if (!used[i]) {
      used[i] = true; ++acquired;
      memset(bytes[i], 0xa5, sizeof(bytes[i]));
      return bytes[i];
    }
    ++errors; return nullptr;
  }
  void free(uint8_t *ptr) override {
    for (unsigned i = 0; i < Slots; ++i) if (ptr == bytes[i]) {
      if (!used[i]) ++errors;
      for (unsigned j = 0; j < Head; ++j) if (ptr[j] != 0xa5) ++errors;
      for (unsigned j = Head + Capacity; j < sizeof(bytes[i]); ++j)
	if (ptr[j] != 0xa5) ++errors;
      used[i] = false; ++released; return;
    }
    ++errors;
  }
};

// A bounded in-memory TCP provider. It attaches backing only on a receive
// notification, exactly as a packet provider does; Ztls receives the same context.
struct Provider : ZiMxPlugin, ZmNHeap<Provider> {
friend inline ZuStringT<"Ztls.RxStub.Provider"> ZmNHeapID(Provider *);
  ZmRef<Backing> backing = new Backing;
  ZmRef<ZiConnection> connection;
  ZmSemaphore event;
  bool stub;
  const uint8_t *wire = nullptr;
  unsigned length = 0, offset = 0;
  ZmAtomic<unsigned> emptyInitial{0}, deliveries{0}, errors{0};
  Provider(ZiMultiplex *mx, bool stub_) : ZiMxPlugin{mx}, stub{stub_} { }
  Zi::IOBufPlugin *allocator(Zi::IOBufType::T type, unsigned, unsigned, unsigned) override {
    return stub && type == Zi::IOBufType::RxTCP ? backing.ptr() : nullptr;
  }
  void start() override { mx()->pluginStarted({}); }
  void stop() override { mx()->pluginStopped({}); }
  void rx() override { event.wait(); }
  void wake() override { event.post(); }
  void listen(ZiListenFn, ZiFailFn, ZiConnectFn, ZiIP, uint16_t, unsigned,
    Zi::IOBufAllocator *, ZiCxnOptions) override { }
  void stopListening(ZiIP, uint16_t) override { }
  void udp(ZiConnectFn, ZiFailFn, ZiIPType::T, ZiIP, uint16_t, ZiIP, uint16_t,
    Zi::IOBufAllocator *, ZiCxnOptions) override { }
  void connect(ZiConnectFn fn, ZiFailFn, ZiIPType::T, ZiIP, uint16_t, ZiIP,
    uint16_t, Zi::IOBufAllocator *allocator_, ZiCxnOptions options) override {
    ZiCxnInfo info{};
    info.type = ZiCxnType::TCPOut; info.socket = Zi::nullSocket();
    info.options = options; info.rxAllocator = allocator_;
    connection = fn(info);
    if (!connection) { ++errors; return; }
    auto &io = connection->rxContext();
    if (!io.buf) ++emptyInitial;
    connection->connected(io);
    if (io.buf && stub && (io.buf->data_() || io.buf->size)) ++errors;
  }
  void recv(ZiConnection *cxn) override {
    if (cxn != connection.ptr() || !wire || offset == length) return;
    auto &io = cxn->rxContext();
    if (io.completed() || !io.buf) { ++errors; return; }
    if (stub) {
      if (io.buf->data_()) { ++errors; return; }
      auto ptr = backing->alloc();
      if (!ptr) return;
      io.buf->data__ = uintptr_t(ptr) | ZiIOBuf::Registered;
      io.buf->size = backing->size;
      io.buf->skip = Head;
    }
    unsigned n = length - offset;
    auto capacity = io.buf->size - io.buf->skip - io.buf->allocator()->tailRoom;
    if (n > capacity) n = capacity;
    if (!n) { ++errors; return; }
    memcpy(io.buf->data(), wire + offset, n);
    io.buf->length = n; offset += n; ++deliveries;
    while (!io());
  }
  void feed(const uint8_t *data, unsigned size) {
    mx()->rxRun([this, data, size]() {
      wire = data; length = size; offset = 0;
      if (connection) recv(connection);
    });
  }
  void send(ZiConnection *cxn) override {
    auto &io = cxn->txContext();
    while (!io.completed()) {
      io.length = io.buf ? io.buf->length - io.offset : 0;
      while (!io());
    }
  }
  void disconnect(ZiConnection *cxn, bool peer) override { close(cxn, peer); }
  void close(ZiConnection *cxn, bool peer) override {
    mx()->rxRun([this, cxn = ZmRef(cxn), peer]() {
      if (connection != cxn.ptr()) return;
      wire = nullptr; length = offset = 0;
      cxn->rxContext().complete();
      connection = nullptr;
      cxn->disconnected(peer);
    });
  }
  TelKey telKey() const override { return {}; }
  void telemetry(Ztc::MxTelemetry &) const override { }
  unsigned allCxns(AllCxnsFn) const override { return 0; }
  unsigned allQueues(Ztc::QueueMgr::AllFn) const override { return 0; }
  void watch(AddCxnFn, DelCxnFn) override { }
  void unwatch() override { }
};

static ptls_t *importTLS(ptls_context_t *ctx, int server)
{
  uint8_t secret[PTLS_TLS12_MASTER_SECRET_SIZE], random[PTLS_HELLO_RANDOM_SIZE * 2], small;
  memset(secret, 0x43, sizeof(secret)); memset(random, 0x76, sizeof(random));
  ptls_buffer_t params;
  ptls_buffer_init_tx(&params, &small, 0);
  ptls_t *tls = nullptr;
  int n = ptls_build_tls12_export_params(ctx, &params, server, 0,
    &ptls_openssl_tls12_ecdhe_rsa_aes128gcmsha256, secret, random, 1,
    "localhost", ptls_iovec_init(nullptr, 0));
  if (!n) n = ptls_import(ctx, &tls, ptls_iovec_init(params.base, params.off));
  ptls_buffer_dispose(&params);
  return n ? nullptr : tls;
}

struct Client : Ztls::Client<Client> {
  using Ztls::Client<Client>::ctx;
  ZmSemaphore connected, done, received;
  ZmAtomic<unsigned> errors{0}, calls{0};
  uint8_t plaintext[Payload]{};
  ZmRef<ZiIOBuf> retained;
  struct Link : Ztls::CliLink<Client, Link>, ZmNHeap<Link> {
  friend inline ZuStringT<"Ztls.RxStub.Link"> ZmNHeapID(Link *);
    using Base = Ztls::CliLink<Client, Link>;
    using Base::Base;
    // Import an established session using the protected TLS installation API;
    // the production connected dispatch still arms transport input and Tx keys.
    void connected_() {
      ptls_free(this->tls()); this->tls(importTLS(this->app()->ctx(), 1));
      if (!this->tls() || !this->handshake_(nullptr)) this->app()->done.post();
    }
    void connected(Ztls::Connected) { this->app()->connected.post(); }
    void disconnected(bool) { this->app()->done.post(); }
    void connectFailed(bool) { ++this->app()->errors; this->app()->done.post(); }
    int process(Ztls::RxStream &rx) {
      auto app = this->app();
      ZmRef<ZiIOBuf> record;
      auto n = rx.extract(
        [](ZuBSpan span) -> int64_t { return span.length(); },
        []() -> ZmRef<ZiRxQueue::Node> { return nullptr; }, record);
      if (n != Payload || !record || record->allocator() != app->rxAllocator() ||
          record->registered()) ++app->errors;
      else {
        memcpy(app->plaintext, record->data(), Payload);
        app->retained = ZuMv(record);
      }
      ++app->calls; app->received.post(); return n > 0 ? 1 : 0;
    }
    void invalidate() { this->reset_tls(); }
  };
};

void run(bool stub, bool corrupt, bool partial, bool stale)
{
  ZuTestScopeRT(run);
  ZiMultiplex mx{ZiMxParams{}.scheduler([](auto &s) {
    s.nThreads(4)
      .thread(1, [](auto &t) { t.isolated(1); })
      .thread(2, [](auto &t) { t.isolated(1); })
      .thread(3, [](auto &t) { t.isolated(1); })
      .thread(4, [](auto &t) { t.isolated(1); });
  }).rxThread(1).txThread(2)};
  ZmRef<Provider> provider = new Provider{&mx, stub};
  Client client;
  auto plain = Client::Link::rxAllocator(&mx);
  auto txPlain = Client::Link::txAllocator(&mx);
  auto packets = Client::Link::txPacketAllocator(&mx);
  Zi::IOBufAllocator *input = nullptr;
  if (!stub) input = mx.allocator<Zi::IOBufType::RxTCP, ZiRxBuf, Capacity>();
  mx.plugin(provider);
  if (stub) input = mx.allocator<Zi::IOBufType::RxTCP, ZiRxBuf, Capacity>();
  Ztls::ClientParams params{&mx, "3", "4"};
  params.rxAllocator(plain).txAllocator(txPlain)
    .txPacketAllocator(packets).socketRxAllocator(input)
    .errorFn([&client](ZeException) { ++client.errors; });
  bool initialized = client.init(ZuMv(params));
  ptls_cipher_suite_t *tls12Suites[] = {
    &ptls_openssl_tls12_ecdhe_rsa_aes128gcmsha256, nullptr};
  client.ctx()->tls12_cipher_suites = tls12Suites;
  ZuCheckRT(initialized);
  if (!initialized) return;
  bool started = mx.start(); ZuCheckRT(started);
  if (!started) { client.final(); return; }
  ZuCheckRT(client.start());
  ZmRef<Client::Link> link = new Client::Link{&client};
  uint8_t payload[Payload];
  for (unsigned i = 0; i < Payload; ++i) payload[i] = uint8_t(i * 19 + (i >> 3));
  for (unsigned pass = 0; pass < ((partial || stale) ? 2U : 1U); ++pass) {
    link->connect("127.0.0.1", 443);
    ZuCheckRT(!client.connected.timedwait(Zm::now(Timeout)));
    auto tx = importTLS(client.ctx(), 0);
    uint8_t small;
    ptls_buffer_t wire;
    ptls_buffer_init_tx(&wire, &small, 0);
    ZuCheckRT(tx && !ptls_send(tx, &wire, payload, Payload));
    ptls_free(tx);
    if (corrupt) wire.base[wire.off - 1] ^= 1;
    bool incomplete = !pass && (partial || stale);
    unsigned length = incomplete ? 23 : unsigned(wire.off);
    if (stale && !pass) {
      // Reset is ordered ahead of the input's owner-directed handoff.
      mx.rxRun([&client, link, provider, data = wire.base, length]() {
	client.rxRun([link]() { link->invalidate(); });
	provider->wire = data; provider->length = length; provider->offset = 0;
	if (provider->connection) provider->recv(provider->connection);
      });
    } else provider->feed(wire.base, length);
    ZmSemaphore drained;
    mx.rxRun([&client, &drained]() {
      client.rxRun([&drained]() { drained.post(); });
    });
    ZuCheckRT(!drained.timedwait(Zm::now(Timeout)));
    if (incomplete) {
      ZuCheckRT(!client.calls.load_());
      if (stub) ZuCheckRT(provider->backing->acquired.load_() ==
        provider->backing->released.load_());
      client.rxRun([link]() { link->disconnect_(false); });
      ZuCheckRT(!client.done.timedwait(Zm::now(Timeout)));
    } else if (corrupt) {
      ZuCheckRT(!client.done.timedwait(Zm::now(Timeout)));
      ZuCheckRT(!client.calls.load_() && client.errors.load_() == 1);
    } else {
      ZuCheckRT(!client.received.timedwait(Zm::now(Timeout)));
      ZuCheckRT(client.calls.load_() == 1 && !memcmp(payload, client.plaintext, Payload));
      link->disconnect();
      ZuCheckRT(!client.done.timedwait(Zm::now(Timeout)));
    }
    ptls_buffer_dispose(&wire);
  }
  ZuCheckRT(client.stop());
  if (!corrupt) {
    ZuCheckRT(client.retained && !memcmp(client.retained->data(), payload, Payload));
    client.retained = nullptr;
  }
  link = nullptr;
  client.final();
  ZuCheckRT(mx.stop());
  ZuCheckRT(!provider->errors.load_() && provider->emptyInitial.load_());
  ZuCheckRT(!provider->backing->errors.load_());
  ZuCheckRT(provider->backing->acquired.load_() == provider->backing->released.load_());
  if (stub) ZuCheckRT(provider->backing->acquired.load_() == provider->deliveries.load_());
  if (!corrupt) ZuCheckRT(!client.errors.load_());
}

void builtin() { ZuTestScope(builtin); ZuTestCall(run, false, false, false, false); }
void registered() { ZuTestScope(registered); ZuTestCall(run, true, false, false, false); }
void corrupt() { ZuTestScope(corrupt); ZuTestCall(run, true, true, false, false); }
void partialReconnect() { ZuTestScope(partialReconnect); ZuTestCall(run, true, false, true, false); }
void staleGeneration() { ZuTestScope(staleGeneration); ZuTestCall(run, true, false, false, true); }

} // ZtlsRxStubTest_

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  using namespace ZtlsRxStubTest_;
  ZuTestCall(builtin);
  ZuTestCall(registered);
  ZuTestCall(corrupt);
  ZuTestCall(partialReconnect);
  ZuTestCall(staleGeneration);
}
