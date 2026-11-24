//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmGuard.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmPLock.hh>
#include <zlib/ZmSemaphore.hh>

#include <zlib/Ztls.hh>
#include <zlib/ZtlsEngineOpenSSL.hh>

#include "ZtlsTestHandshake.hh"

using namespace ZuTestUtil;

namespace ZtlsRxStubTest_ {

enum { Head = 13, Tail = 7, Capacity = 31, Slots = 8, Payload = 257, Timeout = 10 };

template <unsigned Size> struct Backing : ZmNHeap<Backing<Size>>, Zi::IOBufPool {
friend inline ZuStringT<"Ztls.RxStub.Backing"> ZmNHeapID(Backing *);
  struct Slot {
    alignas(ZiIOBuf_DefltAlign)
    uint8_t	bytes[Head + Size + Tail];
    bool	used = false;
  };
  Slot			slots[Slots];
  ZmPLock		lock;
  ZmAtomic<unsigned>	acquired{0}, released{0}, errors{0};

  explicit Backing(Zi::IOBufType::T type) : Zi::IOBufPool{type}
  {
    count = Slots;
    size = Head + Size + Tail;
    headRoom = Head;
    tailRoom = Tail;
  }
  uint8_t *alloc() override
  {
    ZmGuard guard{lock};
    for (auto &slot : slots)
      if (!slot.used) {
	slot.used = true;
	++acquired;
	memset(slot.bytes, 0xa5, sizeof(slot.bytes));
	return slot.bytes;
      }
    ++errors;
    return nullptr;
  }
  void free(uint8_t *ptr) override
  {
    ZmGuard guard{lock};
    for (auto &slot : slots)
      if (ptr == slot.bytes) {
	if (!slot.used) ++errors;
	for (unsigned j = 0; j < Head; ++j)
	  if (ptr[j] != 0xa5) ++errors;
	for (unsigned j = Head + Size; j < sizeof(slot.bytes); ++j)
	  if (ptr[j] != 0xa5) ++errors;
	slot.used = false;
	++released;
	return;
      }
    ++errors;
  }
};

// A bounded in-memory TCP provider. It attaches backing only on a receive
// notification, exactly as a packet provider does; Ztls receives the same context.
struct Provider : ZmNHeap<Provider>, ZiMxPlugin {
friend inline ZuStringT<"Ztls.RxStub.Provider"> ZmNHeapID(Provider *);
  ZmRef<Backing<Capacity>>	backing = new Backing<Capacity>{Zi::IOBufType::Rx};
  ZmRef<Backing<Ztls_::TxRecordCapacity>> txPool =
    new Backing<Ztls_::TxRecordCapacity>{Zi::IOBufType::TxTCP};
  ZmRef<ZiConnection>		connection;
  ZmSemaphore			event;
  bool				stub;
  const uint8_t			*wire = nullptr;
  unsigned			length = 0, offset = 0;
  ZmAtomic<unsigned>		emptyInitial{0}, deliveries{0}, errors{0};
  Provider(ZiMultiplex *mx, bool stub_) : ZiMxPlugin{mx}, stub{stub_} { }
  void attach(ZiMultiplex *mx_) { mx(mx_); }
  Zi::IOBufPool *allocator(Zi::IOBufType::T type, unsigned, unsigned, unsigned) override
  {
    if (type == Zi::IOBufType::TxTCP) return txPool;
    ZmAssert_(stub && type == Zi::IOBufType::Rx);
    return backing;
  }
  void start() override { mx()->pluginStarted({}); }
  void stop() override { mx()->pluginStopped({}); }
  void rx() override { event.wait(); }
  void wake() override { event.post(); }
  void listen(
    ZiListenFn, ZiFailFn, ZiConnectFn, ZiIP, uint16_t, unsigned, Zi::IOBufAllocator *,
    ZiCxnOptions) override
  { }
  void stopListening(ZiIP, uint16_t) override { }
  void udp(
    ZiConnectFn, ZiFailFn, ZiIPType::T, ZiIP, uint16_t, ZiIP, uint16_t, Zi::IOBufAllocator *,
    ZiCxnOptions) override
  { }
  void connect(
    ZiConnectFn		fn, ZiFailFn, ZiIPType::T, ZiIP, uint16_t, ZiIP, uint16_t,
    Zi::IOBufAllocator *allocator_, ZiCxnOptions options) override
  {
    ZiCxnInfo info{};
    info.type = ZiCxnType::TCPOut;
    info.socket = Zi::nullSocket();
    info.options = options;
    info.rxAllocator = allocator_;
    connection = fn(info);
    if (!connection) {
      ++errors;
      return;
    }
    auto &io = connection->rxContext();
    if (!io.buf) ++emptyInitial;
    connection->connected(io);
    if (io.buf && stub && (io.buf->data_() || io.buf->size)) ++errors;
  }
  void recv(ZiConnection *cxn) override
  {
    if (cxn != connection.ptr() || !wire || offset == length) return;
    auto &io = cxn->rxContext();
    if (io.completed() || !io.buf) {
      ++errors;
      return;
    }
    if (io.buf->allocator() != cxn->info().rxAllocator || io.buf->builtin() == stub) {
      ++errors;
      return;
    }
    if (stub) {
      if (io.buf->data_() || io.buf->allocator()->pool != backing.ptr()) {
	++errors;
	return;
      }
      auto ptr = backing->alloc();
      if (!ptr) return;
      io.buf->data__ = uintptr_t(ptr) | ZiIOBuf::Registered;
      io.buf->size = backing->size;
      io.buf->skip = Head;
    }
    unsigned n = length - offset;
    auto     capacity = io.buf->size - io.buf->skip - io.buf->allocator()->tailRoom;
    if (n > capacity) n = capacity;
    if (!n) {
      ++errors;
      return;
    }
    memcpy(io.buf->data(), wire + offset, n);
    io.buf->length = n;
    offset += n;
    ++deliveries;
    while (!io());
  }
  void feed(const uint8_t *data, unsigned size)
  {
    mx()->rxRun([this, data, size]()
    {
      wire = data;
      length = size;
      offset = 0;
      if (connection) recv(connection);
    });
  }
  void send(ZiConnection *cxn) override
  {
    auto &io = cxn->txContext();
    while (!io.completed()) {
      io.length = io.buf ? io.buf->length - io.offset : 0;
      while (!io());
    }
  }
  void disconnect(ZiConnection *cxn, bool peer) override { close(cxn, peer); }
  void close(ZiConnection *cxn, bool peer) override
  {
    mx()->rxRun([this, cxn = ZmRef(cxn), peer]()
    {
      if (connection != cxn.ptr()) return;
      wire = nullptr;
      length = offset = 0;
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

struct Client : Ztls::Client<Client> {
  Zi::IOBufAllocator	*txBufAllocator = nullptr;
  using Ztls::Client<Client>::ctx;
  ZmSemaphore		connected, done, received, packetized;
  ZmAtomic<unsigned>	errors{0}, calls{0};
  uint8_t		plaintext[Payload]{};
  ZmRef<ZiIOBuf>	retained;
  // Published by the connected notification, then owned by the test driver.
  Ztls::Cxn		peer;
  bool			peerReady = false;
  struct Link : ZmNHeap<Link>, Ztls::CliLink<Client, Link> {
  friend inline ZuStringT<"Ztls.RxStub.Link"> ZmNHeapID(Link *);
    using Base = Ztls::CliLink<Client, Link>;
    using Base::Base;
    // Establish the peer in memory before testing packet provider delivery.
    void connected_()
    {
      auto app = this->app();
      this->tls()->initialize(app->ctx(), true, this);
      this->tls()->plaintext.reset(app->rxAllocator(), this);
      app->peer.initialize(app->ctx(), false);
      app->peerReady = ZtlsTest::handshake(&app->peer, this->tls());
      if (!app->peerReady || !this->processRx_()) app->done.post();
    }
    void connected(Ztls::Connected) { this->app()->connected.post(); }
    void disconnected(bool) { this->app()->done.post(); }
    void connectFailed(bool)
    {
      ++this->app()->errors;
      this->app()->done.post();
    }
    int process(Ztls::RxStream &rx)
    {
      auto	     app = this->app();
      ZmRef<ZiIOBuf> record;
      auto	     n = rx.extract([](ZuBSpan span) -> int64_t {
	return span.length();
      }, []() -> ZmRef<ZiRxQueue::Node> { return nullptr; }, record);
      if (n != Payload || !record || record->allocator() != app->rxAllocator() ||
	record->registered())
	++app->errors;
      else {
	memcpy(app->plaintext, record->data(), Payload);
	app->retained = ZuMv(record);
      }
      ++app->calls;
      app->received.post();
      return n > 0 ? 1 : 0;
    }
    void invalidate() { this->resetTLS(); }
    bool sendFor(ZmRef<ZiIOBuf> buf, uint64_t gen) { return this->send(ZuMv(buf), gen); }
    void txPacketized(const ZiIOBuf *, ZmRef<ZiIOBuf>) { this->app()->packetized.post(); }
    void failCurrent()
    { // Rx, after handoff; mutate cipher state only on Tx.
      auto cxn = ZmRef(this->cxn());
      this->app()->txRun([this, cxn = ZuMv(cxn)]()
      {
	cxn->txTraffic.failed = true;
	auto buf = this->app()->txBufAllocator->allocBuf(this, 1);
	buf->data()[0] = 42;
	this->send_(ZuMv(buf));
      });
    }
  };
};

// A rejected send for A must report its error without closing replacement B.
// An engine failure on B must report its own event and disconnect B.
void txGenerations(Client &client, ZmRef<Client::Link> link)
{
  ZuTestScopeRT(txGenerations);
  auto generation = [&client, link]()
  {
    return ZmBlock<uint64_t>{}([&client, link](auto done)
    {
      client.rxRun([&client, link, done = ZuMv(done)]() mutable
      {
	auto cxn = ZmRef(link->cxn());
	client.txRun([cxn = ZuMv(cxn), done = ZuMv(done)]() mutable { done(cxn->txGeneration); });
      });
    });
  };
  auto buffer = [link]()
  {
    auto buf = link->app()->txBufAllocator->allocBuf(link.ptr(), 1);
    buf->data()[0] = 42;
    return buf;
  };
  ZmSemaphore reported;
  unsigned    errors = 0;
  bool	      onRx = true;
  link->txErrorFn([&client, &reported, &errors, &onRx](ZeException &)
  {
    onRx &= client.rxInvoked();
    ++errors;
    reported.post();
    return true;
  });
  link->connect("127.0.0.1", 443);
  ZuCheckRT(!client.connected.timedwait(Zm::now(Timeout)));
  auto first = generation();
  link->disconnect();
  ZuCheckRT(!client.done.timedwait(Zm::now(Timeout)));
  link->connect("127.0.0.1", 443);
  ZuCheckRT(!client.connected.timedwait(Zm::now(Timeout)));
  auto second = generation();
  ZuCheckRT(first != second);
  ZuCheckRT(!link->sendFor(buffer(), first));
  ZuCheckRT(!reported.timedwait(Zm::now(Timeout)));
  ZuCheckRT(errors == 1 && client.done.trywait() != 0);
  ZuCheckRT(link->sendFor(buffer(), second));
  ZuCheckRT(!client.packetized.timedwait(Zm::now(Timeout)));
  ZuCheckRT(client.done.trywait() != 0);
  client.rxRun([link]() { link->failCurrent(); });
  ZuCheckRT(!reported.timedwait(Zm::now(Timeout)));
  ZuCheckRT(!client.done.timedwait(Zm::now(Timeout)));
  ZuCheckRT(errors == 2 && onRx);
  link->txErrorFn({});
}

void run(bool stub, bool corrupt, bool partial, bool stale, bool txErrors = false)
{
  ZuTestScopeRT(run);
  ZmRef<Provider> transport = new Provider{nullptr, stub};
  auto		  provider = transport.ptr();
  ZiMultiplex	  mx{ZiMxParams{}
      .scheduler(
	[](auto &s)
  {
    s.nThreads(4).thread(1, [](auto &t) {
      t.isolated(1);
    }).thread(2, [](auto &t) {
      t.isolated(1);
    }).thread(3, [](auto &t) {
      t.isolated(1);
    }).thread(4, [](auto &t) { t.isolated(1); });
  })
      .rxThread(1)
      .txThread(2), ZuMv(transport)};
  provider->attach(&mx);
  ZuCheckRT(!transport && mx.plugin() == provider);
  Client client;
  auto	 plain = Client::Link::rxAllocator(&mx);
  client.txBufAllocator = Ztls::allocator<Ztls::TxBufAlloc<>>(&mx);
  // The builtin case injects a populated input allocator into the in-memory transport.
  // The registered case exercises the mx factory and transport-owned Rx stub backing.
  ZuPtr<Zi::IOBufAllocator> builtin;
  if (!stub) builtin = new Zi::IOBufBuiltinAllocator<Zi::IOBufType::Rx, ZiRxBuf, Capacity>{&mx};
  auto input = stub ? mx.allocator<Zi::IOBufType::Rx, ZiRxBuf, Capacity>() : builtin.ptr();
  ZuCheckRT(input);
  if (!input) return;
  Ztls::ClientParams params{&mx, "3", "4"};
  params.rxAllocator(plain)
    .rxPktAllocator(input)
    .errorFn([&client](ZeException) { ++client.errors; });
  bool initialized = client.init(ZuMv(params));
  ZuCheckRT(initialized);
  if (!initialized) return;
  auto packets = mx.findAllocator<
    Zi::IOBufPoolAllocator<Zi::IOBufType::TxTCP, Ztls::TxPacket>>();
  ZuCheckRT(packets);
  if (!packets) return;
  ZuCheckRT(
    packets->pool == provider->txPool.ptr() &&
    input->pool.ptr() == (stub ? provider->backing.ptr() : nullptr) &&
    input->pool.ptr() != packets->pool.ptr());
  auto suite = &Ztls_::opensslAes128gcmsha256;
  client.ctx()->cipherSuites.clear();
  client.ctx()->cipherSuites.push(suite);
  ZtlsTest::useTestPSK(*client.ctx(), suite);
  bool started = mx.start();
  ZuCheckRT(started);
  if (!started) {
    client.final();
    return;
  }
  ZuCheckRT(client.start());
  ZmRef<Client::Link> link = new Client::Link{&client};
  uint8_t	      payload[Payload];
  for (unsigned i = 0; i < Payload; ++i) payload[i] = uint8_t(i * 19 + (i >> 3));
  if (txErrors) ZuTestCallRT(txGenerations, client, link);
  for (unsigned pass = 0; !txErrors && pass < ((partial || stale) ? 2U : 1U); ++pass) {
    link->connect("127.0.0.1", 443);
    ZuCheckRT(!client.connected.timedwait(Zm::now(Timeout)));
    auto tx = client.peerReady ? &client.peer : nullptr;
    client.peerReady = false;
    uint8_t	  small;
    Ztls_::Buffer wire;
    Ztls_::bufferInitTx(&wire, &small, 0);
    ZuCheckRT(tx && !Ztls_::send(tx, &wire, payload, Payload));
    if (tx) {
      tx->retireRx();
      tx->retireTx();
    }
    if (corrupt) wire.base[wire.off - 1] ^= 1;
    bool     incomplete = !pass && (partial || stale);
    unsigned length = incomplete ? 23 : unsigned(wire.off);
    if (stale && !pass) {
      // Reset is ordered ahead of the input's owner-directed handoff.
      mx.rxRun([&client, link, provider, data = wire.base, length]()
      {
	client.rxRun([link]() { link->invalidate(); });
	provider->wire = data;
	provider->length = length;
	provider->offset = 0;
	if (provider->connection) provider->recv(provider->connection);
      });
    } else
      provider->feed(wire.base, length);
    ZmSemaphore drained;
    mx.rxRun([&client, &drained]() { client.rxRun([&drained]() { drained.post(); }); });
    ZuCheckRT(!drained.timedwait(Zm::now(Timeout)));
    if (incomplete) {
      ZuCheckRT(!client.calls.load_());
      if (stub)
	ZuCheckRT(provider->backing->acquired.load_() == provider->backing->released.load_());
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
    Ztls_::bufferDispose(&wire);
  }
  ZuCheckRT(client.stop());
  if (!corrupt && !txErrors) {
    ZuCheckRT(client.retained && !memcmp(client.retained->data(), payload, Payload));
    client.retained = nullptr;
  }
  link = nullptr;
  client.final();
  ZuCheckRT(mx.stop());
  ZuCheckRT(!provider->errors.load_() && provider->emptyInitial.load_());
  ZuCheckRT(!provider->backing->errors.load_());
  ZuCheckRT(provider->backing->acquired.load_() == provider->backing->released.load_());
  ZuCheckRT(
    !provider->txPool->errors.load_() &&
    provider->txPool->acquired.load_() == provider->txPool->released.load_());
  if (stub) ZuCheckRT(provider->backing->acquired.load_() == provider->deliveries.load_());
  if (!corrupt) ZuCheckRT(!client.errors.load_());
}

void builtin()
{
  ZuTestScope(builtin);
  ZuTestCall(run, false, false, false, false);
}
void registered()
{
  ZuTestScope(registered);
  ZuTestCall(run, true, false, false, false);
}
void corrupt()
{
  ZuTestScope(corrupt);
  ZuTestCall(run, true, true, false, false);
}
void partialReconnect()
{
  ZuTestScope(partialReconnect);
  ZuTestCall(run, true, false, true, false);
}
void staleGeneration()
{
  ZuTestScope(staleGeneration);
  ZuTestCall(run, true, false, false, true);
}

} // namespace ZtlsRxStubTest_

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
  ZuTestCall(run, true, false, false, false, true);
}
