//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef ZutcpTestMxPlugin_HH
#define ZutcpTestMxPlugin_HH

#include <iostream>
#include <string.h>
#include <net/if.h>

#include "ZpktTestCf.hh"

#include <zlib/ZmAtomic.hh>
#include <zlib/ZmBlock.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmRef.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmVHeap.hh>

#include <zlib/Zutcp.hh>
#include <zlib/ZutcpCxn.hh>
#include <zlib/ZutcpCliCxn.hh>
#include <zlib/ZutcpSrvCxn.hh>
#include <zlib/ZutcpUDP.hh>

namespace ZutcpTest {

// Heap-backed XDP fixture for deterministic protocol tests. It records the
// pools supplied to allocation without opening a native socket or link.
struct Pool : ZmNHeap<Pool>, Zi::IOBufPool {
friend inline ZuStringT<"Zutcp.Test.Pool"> ZmNHeapID(Pool *);
  using Heap = ZmVHeap<"Zutcp.Test.Backing", 2048, 2048, ZiIOBuf_Align>;
  using Counter = ZmAtomic<unsigned>;
  bool		available;
  Counter		allocCalls = 0, returns = 0;

  Pool(Zi::IOBufType::T type, unsigned n, unsigned bytes, unsigned head, unsigned tail,
      unsigned alignment, bool available_) :
    Zi::IOBufPool{type}, available{available_}
  {
    count = n;
    size = bytes;
    headRoom = head;
    tailRoom = tail;
    align = alignment;
  }
  uint8_t *alloc() override {
    ++allocCalls;
    return available ? static_cast<uint8_t *>(Heap::alloc(size)) : nullptr;
  }
  void free(uint8_t *ptr) override { ++returns; Heap::free(ptr, size); }
};

class Xdp : public ZmNHeap<Xdp>, public Zpkt::Nif {
friend inline ZuStringT<"Zutcp.Test.Xdp"> ZmNHeapID(Xdp *);
public:
  explicit Xdp(ZiMultiplex *mx, bool deferred = false)
  : m_mx{mx}, m_deferred{deferred} { ++opens; }
  ZmRef<Zi::IOBufPool> bufPool(Zi::IOBufType::T type, unsigned count, unsigned size,
    unsigned head, unsigned tail, unsigned align) override
  {
    return new Pool{type, count, size, head, tail, align, !m_deferred};
  }
  void reserveTxPools(ZuSpan<Zi::IOBufPool *const> pools) override {
    ZmAssert_(pools.length() == 2);
    reserves[0] = pools[0];
    reserves[1] = pools[1];
  }
  ZuUnion<void, ZeException> alloc(
    Zi::IOBufPool *rx, const Zi::IOBufPoolList &tx) override
  {
    ++allocCalls;
    rxPool = rx;
    txPools.length(0);
    auto iter = tx.citer();
    while (auto pool = iter()) txPools.push(static_cast<Zi::IOBufPool *>(pool));
    if (failAlloc) return ZeEXCEPT(Error, "ZutcpTest", "fixture allocation failed");
    if (rx) static_cast<Pool *>(rx)->available = true;
    for (auto pool : txPools) static_cast<Pool *>(pool)->available = true;
    for (auto pool : reserves) if (pool) static_cast<Pool *>(pool)->available = true;
    m_allocated = true;
    return {};
  }
  bool start(Zpkt::RecvFn rx, Zpkt::StopFn tx) override {
    return ZmBlock<bool>{}([this, rx = ZuMv(rx), tx = ZuMv(tx)](auto done) mutable {
      start(ZuMv(rx), ZuMv(tx), ZuMv(done));
    });
  }
  void start(Zpkt::RecvFn rx, Zpkt::StopFn tx, Zpkt::StartFn done) override {
    m_receive = ZuMv(rx);
    m_txStopped = ZuMv(tx);
    Engine::start([done = ZuMv(done)](bool ok) { if (done) done(ok); });
  }
  void final() override {
    if (!m_mx) return;
    ++finals;
    m_mx = nullptr;
    m_allocated = m_started = false;
    rxPool = nullptr;
    txPools.null();
    reserves[0] = reserves[1] = nullptr;
    retireNif();
  }
  ZeException error() const override { return {}; }
  ZiMultiplex *mx() const override { return m_mx; }
  unsigned ifindex() const override { return m_mx ? 1 : 0; }
  unsigned rxQueues() const override { return 1; }
  bool allocated() const override { return m_allocated; }
  bool everStarted() const override { return m_started; }
  bool ready() const override { return state() == ZmEngineState::Running; }
  unsigned mtu() const override { return 1500; }
  bool zeroCopy() const override { return false; }
  ZuBSpan mac() const override { return {}; }
  void receive(ZuBSpan bytes, ZiRxBurst *burst = nullptr) {
    auto packet = rxPool->alloc();
    ZmAssert_(packet && bytes.length() <= rxPool->size);
    memcpy(packet, bytes.data(), bytes.length());
    m_receive(packet, bytes.length(), burst);
  }
  bool txReady(uint8_t *) const override { return ready(); }
  bool send(uint8_t *, unsigned) override { return false; }
  bool tcpChksumOffload() override { return false; }
  void rx() override { m_event.wait(); }
  void wake() override {
    m_event.post();
    if (Engine::stopping()) Engine::stopped();
  }
  unsigned pollTx() override { return 0; }
  void status() override { }

  Zi::IOBufPool *reserves[2]{}, *rxPool = nullptr;
  ZtArray<Zi::IOBufPool *> txPools;
  unsigned opens = 0, allocCalls = 0, starts = 0, stops = 0, finals = 0;
  bool failAlloc = false, failStart = false;

private:
  void start_() override {
    ++starts;
    m_started = true;
    if (failStart) { m_receive = {}; m_txStopped = {}; }
    Engine::started(!failStart);
  }
  void stop_() override {
    ++stops;
    m_receive = {};
    if (auto stopped = ZuMv(m_txStopped)) stopped();
    Engine::stopped(true);
  }

  ZiMultiplex *m_mx = nullptr;
  Zpkt::RecvFn m_receive;
  Zpkt::StopFn m_txStopped;
  ZmSemaphore m_event;
  bool m_allocated = false, m_started = false, m_deferred;
};

inline ZmRef<Zpkt::Nif> fixtureXdp(ZiMultiplex *mx, bool deferred = false)
{
  return new Xdp{mx, deferred};
}

// Fluent setup is test syntax over the actual policy value. It has no transport
// state or application owner engine.
class Params {
public:
  explicit Params(ZiMultiplex *mx = nullptr) : m_mx{mx} { }
  ZiMultiplex *mx() const { return m_mx; }
  const Zutcp::MxParams &config() const { return m_config; }
  Params &&sackPermitted(bool value) { m_config.sackPermitted = value; return ZuMv(*this); }
  bool sackPermitted() const { return m_config.sackPermitted; }
  Params &&windowScale(bool value) { m_config.windowScale = value; return ZuMv(*this); }
  bool windowScale() const { return m_config.windowScale; }
  Params &&timestamps(bool value) { m_config.timestamps = value; return ZuMv(*this); }
  bool timestamps() const { return m_config.timestamps; }
  Params &&ecn(bool value) { m_config.ecn = value; return ZuMv(*this); }
  bool ecn() const { return m_config.ecn; }
  Params &&congestionControl(bool value) {
    m_config.congestionControl = value; return ZuMv(*this);
  }
  bool congestionControl() const { return m_config.congestionControl; }
  Params &&maxData(uint64_t value) { m_config.maxData = value; return ZuMv(*this); }
  uint64_t maxData() const { return m_config.maxData; }
  Params &&maxCxns(unsigned value) { m_config.maxCxns = value; return ZuMv(*this); }
  unsigned maxCxns() const { return m_config.maxCxns; }
  Params &&maxPendingCxns(unsigned value) { m_config.maxPendingCxns = value; return ZuMv(*this); }
  unsigned maxPendingCxns() const { return m_config.maxPendingCxns; }
  Params &&closedTupleRingSize(unsigned value) {
    m_config.closedTupleRingSize = value; return ZuMv(*this);
  }
  unsigned closedTupleRingSize() const { return m_config.closedTupleRingSize; }
  Params &&tfoCookieCacheSize(unsigned value) {
    m_config.tfoCookieCacheSize = value; return ZuMv(*this);
  }
  unsigned tfoCookieCacheSize() const { return m_config.tfoCookieCacheSize; }
  Params &&connectTimeout(ZuTime value) { m_config.connectTimeout = value; return ZuMv(*this); }
  ZuTime connectTimeout() const { return m_config.connectTimeout; }
  Params &&userTimeout(ZuTime value) { m_config.userTimeout = value; return ZuMv(*this); }
  ZuTime userTimeout() const { return m_config.userTimeout; }
  Params &&closeDrainTimeout(ZuTime value) {
    m_config.closeDrainTimeout = value; return ZuMv(*this);
  }
  ZuTime closeDrainTimeout() const { return m_config.closeDrainTimeout; }
  Params &&finWaitTimeout(ZuTime value) { m_config.finWaitTimeout = value; return ZuMv(*this); }
  ZuTime finWaitTimeout() const { return m_config.finWaitTimeout; }
  Params &&maxIdleTimeout(uint64_t value) { m_config.maxIdleTimeout = value; return ZuMv(*this); }
  uint64_t maxIdleTimeout() const { return m_config.maxIdleTimeout; }
  Params &&heartBeat(ZuTime value) { m_config.heartBeat = value; return ZuMv(*this); }
  ZuTime heartBeat() const { return m_config.heartBeat; }
  Params &&maxPayload(unsigned value) { m_config.maxPayload = value; return ZuMv(*this); }
  unsigned maxPayload() const { return m_config.maxPayload; }
  Params &&controlBufCount(unsigned value) {
    m_config.controlBufCount = value; return ZuMv(*this);
  }
  unsigned controlBufCount() const { return m_config.controlBufCount; }
  Params &&txControlCredits(unsigned value) {
    m_config.txControlCredits = value; return ZuMv(*this);
  }
  unsigned txControlCredits() const { return m_config.txControlCredits; }
  Params &&rxRetainFramesPerCxn(unsigned value) {
    m_config.rxRetainFramesPerCxn = value; return ZuMv(*this);
  }
  unsigned rxRetainFramesPerCxn() const { return m_config.rxRetainFramesPerCxn; }
  Params &&tlpPeerAckDelay(ZuTime value) { m_config.tlpPeerAckDelay = value; return ZuMv(*this); }
  ZuTime tlpPeerAckDelay() const { return m_config.tlpPeerAckDelay; }
  Params &&ackHoldTimeout(ZuTime value) { m_config.ackHoldTimeout = value; return ZuMv(*this); }
  ZuTime ackHoldTimeout() const { return m_config.ackHoldTimeout; }
  Params &&initialRto(ZuTime value) { m_config.rtoParams.initial = value; return ZuMv(*this); }
  Params &&minRto(ZuTime value) { m_config.rtoParams.min = value; return ZuMv(*this); }
  Params &&maxRto(ZuTime value) { m_config.rtoParams.max = value; return ZuMv(*this); }
  Params &&rtoGranularity(ZuTime value) {
    m_config.rtoParams.granularity = value; return ZuMv(*this);
  }
  const Zutcp::RtoParams &rtoParams() const { return m_config.rtoParams; }
  using ErrorFn = ZmFn<void(ZeException)>;
  Params &&errorFn(ErrorFn fn) { m_error = ZuMv(fn); return ZuMv(*this); }
  const ErrorFn &errorFn() const { return m_error; }

private:
  ZiMultiplex *m_mx;
  Zutcp::MxParams m_config;
  ErrorFn m_error;
};

inline ZmRef<Zpkt::Env> fixtureEnv()
{
  ZmRef<Zpkt::Env> env = new Zpkt::Env;
  auto cf = ZpktTest::config(ZUTCP_TEST_PLUGIN);
  env->init(cf.ptr());
  return env;
}

// Use the production handoff for deterministic fixtures and native drivers.
class MxPlugin : public Zutcp::MxPlugin {
public:
  explicit MxPlugin(ZiMultiplex *mx) : m_fixtureEnv{fixtureEnv()}, m_fixtureMx{mx} { }
  ~MxPlugin() override { final(); }
  static bool multicast(MxPlugin &, unsigned, bool, unsigned &);
  ZiMultiplex *mx() const { return m_fixtureMx; }
  using Zutcp::MxPlugin::final;

  static void udpSend(Zutcp::MxPlugin &instance, ZiConnection *cxn, Zutcp::IPPath path) {
    Zutcp::MxPlugin::UdpCxn endpoint{&instance, cxn->info(), ZuMv(path)};
    endpoint.send(cxn);
  }
  static void udpRouteThenClose(Zutcp::MxPlugin &instance, ZiConnection *cxn) {
    ZmRef<Zutcp::MxPlugin::UdpCxn> endpoint =
      new Zutcp::MxPlugin::UdpCxn{&instance, cxn->info(), {}};
    endpoint->send(cxn); // resolution returns on a subsequent Tx turn
    cxn->txContext().close();
    endpoint->send(cxn);
  }
  static Zutcp::MxParams parameters(const Zutcp::MxPlugin &instance) {
    return instance.m_params;
  }
  static void apply(
    Zutcp::MxPlugin &instance, Zutcp::MxParams params)
  {
    instance.init(instance.mx(), instance.m_xdpEnv, {}, ZuMv(params));
  }
  static void apply(MxPlugin &instance, Zutcp::MxParams params)
  {
    if (instance.m_xdpEnv) {
      apply(static_cast<Zutcp::MxPlugin &>(instance), ZuMv(params));
      return;
    }
    auto xdp = fixtureXdp(instance.mx());
    instance.Zutcp::MxPlugin::init(
      instance.mx(), instance.m_fixtureEnv, ZuMv(xdp), ZuMv(params));
  }
  bool init(const ZutcpTest::Params &params) {
    try {
      if (params.mx() != mx()) throw ZeEXCEPT(Error, "ZutcpTest", "fixture mx mismatch");
      apply(*this, params.config());
    } catch (ZeException &e) {
      if (params.errorFn()) params.errorFn()(ZuMv(e));
      else std::cerr << e << '\n' << std::flush;
      return false;
    }
    return true;
  }

private:
  ZmRef<Zpkt::Env> m_fixtureEnv;
  ZiMultiplex *m_fixtureMx;
};

inline ZmRef<Zpkt::Nif> openXdp(
  Zpkt::Env *env, ZiMultiplex *mx, unsigned interface, unsigned sender, int64_t spin = -1)
{
  char name[IF_NAMESIZE];
  if (!if_indextoname(interface, name))
    throw ZeEXCEPT(Error, "ZutcpTest", "missing Linux interface");
  auto cf = ZpktTest::config(ZUTCP_XDP_MODULE, static_cast<const char *>(name), sender, spin);
  env->init(cf.ptr());
  auto opened = env->nif(mx, static_cast<const char *>(name));
  if (opened.is<ZeException>()) throw ZuMv(opened).p<ZeException>();
  return opened.p<Zpkt::Nif *>();
}

inline ZmRef<Zutcp::MxPlugin> initMxPlugin(
  ZmRef<Zutcp::MxPlugin> instance, Zpkt::Env *env, ZiMultiplex *mx,
  ZmRef<Zpkt::Nif> &&xdp, Zutcp::MxParams params)
{
  try { instance->init(mx, env, ZuMv(xdp), ZuMv(params)); }
  catch (ZeException &e) {
    xdp->final();
    auto message = e;
    std::cerr << message << '\n' << std::flush;
    throw;
  }
  return instance;
}

inline ZmRef<Zutcp::MxPlugin> initMxPlugin(
  Zpkt::Env *env, ZiMultiplex *mx, unsigned interface, unsigned sender,
  Zutcp::MxParams params = {}, int64_t spin = -1)
{
  return initMxPlugin(ZmRef{static_cast<Zutcp::MxPlugin *>(mx->plugin())},
    env, mx, openXdp(env, mx, interface, sender, spin), ZuMv(params));
}

template <typename M = ZiMultiplex> struct MxStorage {
  M multiplex;
  explicit MxStorage(ZiMxParams params) : multiplex{ZuMv(params)} { }
};

template <typename M = ZiMultiplex>
class Context : public MxStorage<M>, public MxPlugin {
public:
  explicit Context(ZiMxParams params = ZiMxParams{}.scheduler(
      [](auto &s) { s.nThreads(4); }).rxThread(1).txThread(2))
  : MxStorage<M>{ZuMv(params)}, MxPlugin{&this->multiplex} { }
  M *mx() const { return static_cast<M *>(MxPlugin::mx()); }
};

// Test-only local passive listener record over the same protocol primitive as
// the native transport. Its MxPlugin owns all native resources and protocol policy.
template <typename Impl, typename Link>
class Listener : public ZmPolymorph, public Zutcp_::Listener_<Impl, Link> {
  using Passive = Zutcp_::Listener_<Impl, Link>;
  using Endpoint = Zutcp_::Endpoint_<Impl>;
public:
  using Done = ZmFn<void(bool)>;
  enum { EndpointRef = true };

  Listener(Zutcp::MxPlugin *instance, ZiIP local, uint16_t port)
  : m_instance{instance}, m_local{local, port} { }
  Zutcp::MxPlugin *stack() const { return m_instance; }
  ZiMultiplex *mx() const { return m_instance->mx(); }
  ZiIP localIP() const { return m_local.ip(); }
  uint16_t localPort() const { return m_local.port(); }
  unsigned state() const { return m_state; }
  bool init(const ZutcpTest::Params &params) {
    if (params.mx() != mx()) return false;
    try { MxPlugin::apply(*m_instance, params.config()); }
    catch (ZeException &e) {
      std::cerr << e << '\n' << std::flush;
      return false;
    }
    return true;
  }
  void final() { ZmAssert_(!m_members->count_() && state() == ZmEngineState::Stopped); }
  template <typename F> void rxRun(F &&fn) { m_instance->rxRun(ZuFwd<F>(fn)); }
  bool inputActive() const { return state() == ZmEngineState::Running; }
  bool accepting() const { return state() == ZmEngineState::Running; }
  bool tupleClaimedLive(const Zutcp::ClosedTupleKey &tuple) const {
    auto iter = m_members->citer();
    while (auto node = iter())
      if (static_cast<Link *>(node)->closedKeyRx() == tuple) return true;
    return false;
  }
  ZmRef<Link> acceptInitial(const Zutcp::InitialInfo &info,
      const Zutcp_::PendingInitial &claim) {
    auto link = impl()->accepted(info);
    if (!link || !accepting() || link->admittedRx()) return {};
    if (claim.evidenceID && !link->stageReuseISNRx(claim.localISN, claim.evidenceID)) {
      link->discardUnadoptedRx();
      return {};
    }
    m_members->addNode(ZmRef<Zutcp_::ServerLinks::Node>{link});
    return link;
  }
  void disconnected(Link *link, bool) {
    ZmRef<Link> held{link};
    Passive::retireLinkRoutes(link);
    m_members->del(link);
    link->releaseCxnRx();
    drained();
  }
  void stop(Done done) {
    m_instance->rxInvoke([this, done = ZuMv(done)]() mutable {
      if (state() == ZmEngineState::Stopped) { done(true); return; }
      m_stopped = ZuMv(done);
      m_state = ZmEngineState::Stopping;
      {
        auto iter = m_members->citer();
        while (auto node = iter()) static_cast<Link *>(node)->abortRx();
      }
      Passive::clearLinkRoutes();
      drained();
    });
  }
  bool stop() {
    return ZmBlock<bool>{}([this](auto done) { stop(Done{ZuMv(done)}); });
  }
  bool start() {
    return ZmBlock<bool>{}([this](auto done) {
      m_instance->rxRun([this, done = ZuMv(done)]() mutable {
        m_started = Done{ZuMv(done)};
        m_state = ZmEngineState::Running;
        m_endpointStopped = false;
        if (!Endpoint::init(m_instance) ||
            !Endpoint::open(Zutcp::PathMode::ServerUnconnected,
              m_local.ip(), m_local.port())) endpointFailed(false);
      });
    });
  }
  void endpointReady(Endpoint *) { if (auto done = ZuMv(m_started)) done(true); }
  void endpointFailed(bool transient) {
    m_state = ZmEngineState::Stopped;
    m_endpointStopped = true;
    impl()->listenFailed(transient);
    if (auto done = ZuMv(m_started)) done(false);
  }
  void listenFailed(bool) { }
  void endpointDown_(Endpoint *) { }
  void initialDone() { drained(); }
  void endpointDatagram(Zutcp::Datagram data) {
    if (m_instance->rxInvoked()) Passive::endpointDatagram(ZuMv(data));
    else m_instance->rxRun([this, data = ZuMv(data)]() mutable {
      Passive::endpointDatagram(ZuMv(data));
    });
  }
  unsigned linkCount_() const { return m_members->count_(); }
  ZmRef<Zutcp::PacketBuf> allocTxPkt_(bool control = false) {
    return Endpoint::allocTxPkt(control);
  }
  bool sendPktRaw(ZmRef<ZiIOBuf> packet, ZiSockAddr peer, Zutcp::EcnMark::T ecn,
      bool priority, const Zutcp::IPPath &path) {
    return Endpoint::send(ZuMv(packet), peer, ecn, priority, &path);
  }

private:
  Impl *impl() { return static_cast<Impl *>(this); }
  void drained() {
    if (state() != ZmEngineState::Stopping || m_members->count_() ||
        Passive::initialPending()) return;
    if (!m_endpointStopped) {
      if (m_endpointStopping) return;
      // Keep output available until the member aborts have sent their resets.
      m_endpointStopping = true;
      Endpoint::disconnect([this] {
        m_instance->rxRun([this] {
          m_endpointStopping = false;
          m_endpointStopped = true;
          drained();
        });
      });
      return;
    }
    m_state = ZmEngineState::Stopped;
    if (auto done = ZuMv(m_stopped)) done(true);
  }

  Zutcp::MxPlugin *m_instance;
  ZiSockAddr m_local;
  ZmRef<Zutcp_::ServerLinkTable> m_members = new Zutcp_::ServerLinkTable{
    ZmHashParams{}.bits(5).loadFactor(1).cBits(3).init("Zutcp.ServerLinks")};
  ZmAtomic<unsigned> m_state{ZmEngineState::Stopped};
  bool m_endpointStopped = true;
  bool m_endpointStopping = false;
  Done m_started, m_stopped;
};

} // namespace ZutcpTest

#endif /* ZutcpTestMxPlugin_HH */
