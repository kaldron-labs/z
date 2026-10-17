//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include "ZhttpClientImpl.hh"

namespace Zhttp {

ZtEnumImplStruct(ProtoPolicy);
ZtEnumImplStruct(ResultCode);
ZtEnumImplStruct(AttemptPhase);
ZtEnumImplStruct(FailureKind);
ZtEnumImplStruct(RedirectState);
ZtEnumImplStruct(Persistence);
ZtEnumImplStruct(AttemptEvent);

void Destination::init() {
  URLString input;
  if (ipv6Literal) input << '[';
  input << host;
  if (ipv6Literal) input << ']';
  input << ':' << ZuBoxed(port);
  AuthorityView parsed;
  ZuBSpan input_ = input;
  auto error = parseAuthority(parsed, input_, 0, 0, true);
  if (!port || !error.ok() || parsed.port != port ||
	parsed.ipv6Literal != ipv6Literal) {
    host.length(0);
    authority.length(0);
    port = 0;
    ipv6Literal = false;
    return;
  }
  if (ipv6Literal) {
    ZiIP ip;
    if (!ZiIP::parse(ip, parsed.host) || !ip.v6()) {
	host.length(0);
	authority.length(0);
	port = 0;
	ipv6Literal = false;
	return;
    }
    host.length(0);
    host << ip;
  } else {
    host = parsed.host;
    for (unsigned i = 0, n = host.length(); i < n; ++i)
	if (host[i] >= 'A' && host[i] <= 'Z')
	  host[i] += 'a' - 'A';
  }
  authority.length(0);
  if (ipv6Literal) authority << '[';
  authority << host;
  if (ipv6Literal) authority << ']';
  authority << ':' << ZuBoxed(port);
}

PoolCore::PoolCore(ClientCore *client, const PoolQueueOps &ops,
    const CatalogView &req, const CatalogView &res) :
  m_impl{new PoolImpl{client, this}}, m_queueOps{&ops},
  m_reqCatalog{&req}, m_resCatalog{&res} { }
PoolCore::~PoolCore() { delete m_impl; }
bool PoolCore::poolH1TxError(unsigned slot, unsigned generation, ZeException &error) {
  return m_impl->poolH1TxError(slot, generation, error);
}

bool PoolCore::init(unsigned slot, Destination dest, const HubConfig &hub,
    const Config &config, const TCPConfig &tcp, H2Config tls,
    const QUICConfig &quic) {
  return m_impl->init(slot, ZuMv(dest), hub, selected(config), tcp, ZuMv(tls), quic);
}
bool PoolCore::backends(const PoolInit &config) {
  for (const auto &profile: profiles())
    if (!profile.pool(*this, config, profile.versions)) return false;
  return true;
}
ZiMultiplex *PoolCore::mx() const { return m_impl->mx(); }
unsigned PoolCore::rxThread() const { return m_impl->rxThread(); }
unsigned PoolCore::txThread() const { return m_impl->txThread(); }
void *PoolCore::clientContext() const { return m_impl->client()->context(); }
const Config &PoolCore::config() const { return m_impl->config(); }
const Destination &PoolCore::destination() const { return m_impl->destination(); }
unsigned PoolCore::hubCount() const { return m_impl->hubs().count(); }

bool PoolCore::start() { return m_impl->start(); }
void PoolCore::start(DoneFn fn) { m_impl->start(ZuMv(fn)); }
void PoolCore::stop() { m_impl->stop(); }
void PoolCore::stop(DoneFn fn) { m_impl->stop(ZuMv(fn)); }
void PoolCore::final() { m_impl->final(); }
void PoolCore::txErrorFn(ZiTxErrorFn fn) { m_impl->txErrorFn(ZuMv(fn)); }
void PoolCore::discoveryResolver(const DiscoveryResolver *resolver) {
  m_impl->discoveryResolver(resolver);
}
void PoolCore::printQUICDiag() { m_impl->printQUICDiag(); }
unsigned PoolCore::completed() const { return m_impl->completed(); }
unsigned PoolCore::failed() const { return m_impl->failed(); }
unsigned PoolCore::active() const { return m_impl->active(); }
void PoolCore::send(RequestRef request) { m_impl->send(ZuMv(request)); }
void PoolCore::seal() { m_impl->seal(); }
bool PoolCore::limited(unsigned slot, bool value) { return m_impl->limited(slot, value); }
bool PoolCore::send_(RequestRecord *request, bool more) { return m_impl->send_(request, more); }
bool PoolCore::gap_() { return m_impl->gap_(); }
void PoolCore::idleTx_() { m_impl->idleTx_(); }
void PoolCore::scheduleSend() { m_impl->scheduleSend(); }
void PoolCore::scheduleResend() { m_impl->scheduleResend(); }
void PoolCore::scheduleArchive() { m_impl->scheduleArchive(); }
void PoolCore::cancelQueued_(RequestRef request) { m_impl->cancelQueued_(ZuMv(request)); }
void PoolCore::cancelActive_(unsigned slot, const void *key) { m_impl->cancelActive_(slot, key); }
ClientCore::ClientCore(void *app, const ClientAppOps &ops) :
  m_app{app}, m_ops{&ops} { }
ClientCore::~ClientCore() = default;
bool ClientCore::running() const { return m_ctl.running(); }

bool ClientCore::init(
  const HubConfig &hub, unsigned poolCount,
  const Config &config, const TCPConfig &tcp,
  const H2Config &h2, const QUICConfig &quic)
{
  if (m_inited || !hub.mx() || !poolCount)
    return false;
  m_hub = hub;
  m_config = config;
  m_tcp = tcp;
  m_h2 = h2;
  m_quic = quic;
  m_resolverOwned = !ZiResolver::instance()->initialized();
  m_idleCount = 0;
  m_completed = 0;
  m_failed = 0;
  m_active = 0;
  m_pools.size(poolCount);
  m_idle.size(poolCount);
  for (unsigned i = 0; i < poolCount; ++i) {
    m_pools.push(PoolRef{});
    m_idle.push(0);
  }
  m_inited = true;
  return true;
}

ZiMultiplex *ClientCore::mx() const { return m_hub.mx(); }
unsigned ClientCore::rxThread() const {
  return m_hub.rxThread() ? mx()->sid(m_hub.rxThread()) : mx()->rxThread();
}
unsigned ClientCore::txThread() const {
  return m_hub.txThread() ? mx()->sid(m_hub.txThread()) : mx()->txThread();
}

bool ClientCore::pool(
  unsigned slot, Destination destination,
  const Config &config)
{
  if (!m_inited || m_ctl.state() != ZmEngineState::Stopped || slot >= m_pools.length() ||
	m_pools[slot] || !destination.valid())
    return false;
  Config effective = m_config.overlay(config);
  H2Config h2{m_h2};
  QUICConfig quic{m_quic};
  h2.maxConcurrentStreams(effective.linkMax());
  h2.maxPending(effective.linkMax());
  quic.maxStreamsDuplex(effective.linkMax());
  PoolRef pool = m_ops->pool(m_app);
  if (!pool->init(
	slot, ZuMv(destination), m_hub, effective, m_tcp, h2, quic) ||
	!m_ctl.add(*pool)) {
    pool->final();
    return false;
  }
  m_pools[slot] = ZuMv(pool);
  return true;
}

bool ClientCore::start() {
  return ZmBlock<bool>{}([this](auto wake) { this->start(ZuMv(wake)); });
}
void ClientCore::start(DoneFn done) {
  DoneFn done_{ZuMv(done)};
  if (!m_inited) { done_(false); return; }
  for (unsigned i = 0, n = m_pools.length(); i < n; ++i)
    if (!m_pools[i]) { done_(false); return; }
  if (m_resolverOwned) ZiResolver::start();
  if (!ZiResolver::instance()->running()) { done_(false); return; }
  m_ctl.start(ZuMv(done_));
}

bool ClientCore::seal(unsigned slot) {
  if (!m_ctl.running()) return false;
  auto pool = pool_(slot);
  if (!pool) return false;
  pool->seal();
  return true;
}
bool ClientCore::limited(unsigned pool, unsigned link, bool value) {
  auto pool_ = this->pool_(pool);
  return pool_ && pool_->limited(link, value);
}

void ClientCore::txErrorFn(ZiTxErrorFn fn) {
  for (auto &pool: m_pools) if (pool) pool->txErrorFn(fn);
}
void ClientCore::discoveryResolver(const DiscoveryResolver *resolver) {
  for (auto &pool: m_pools) if (pool) pool->discoveryResolver(resolver);
}

void ClientCore::stop() {
  (void)ZmBlock<bool>{}(
    [this](auto wake) { stop(DoneFn{ZuMv(wake)}); });
}
void ClientCore::stop(DoneFn done) {
  m_ctl.stop(ZuMv(done));
}

void ClientCore::final() {
  m_ctl.final();
  m_pools.init();
  m_idle.init();
  if (m_resolverOwned) {
    ZiResolver::stop();
    ZiResolver::final();
    m_resolverOwned = false;
  }
  m_idleCount = 0;
  m_completed = 0;
  m_failed = 0;
  m_active = 0;
  m_inited = false;
}

unsigned ClientCore::poolCount() const { return m_pools.length(); }
unsigned ClientCore::completed() const { return m_completed; }
unsigned ClientCore::failed() const { return m_failed; }
unsigned ClientCore::active() const { return m_active; }
unsigned ClientCore::completed(unsigned slot) const {
  auto pool = pool_(slot); return pool ? pool->completed() : 0;
}
unsigned ClientCore::failed(unsigned slot) const {
  auto pool = pool_(slot); return pool ? pool->failed() : 0;
}
unsigned ClientCore::active(unsigned slot) const {
  auto pool = pool_(slot); return pool ? pool->active() : 0;
}
const Config * ClientCore::config(unsigned slot) const {
  auto pool = pool_(slot); return pool ? &pool->config() : nullptr;
}
void ClientCore::printQUICDiag() {
  for (auto &pool: m_pools) if (pool) pool->printQUICDiag();
}

void ClientCore::poolIdle_(unsigned slot) {
  if (slot >= m_idle.length() || m_idle[slot]) return;
  m_idle[slot] = 1;
  if (++m_idleCount == m_idle.length()) m_ops->idle(m_app);
}
void ClientCore::poolResult_(bool failed_) {
  ++m_completed;
  if (failed_) ++m_failed;
}
void ClientCore::poolActive_(bool active_) {
  if (active_) ++m_active;
  else --m_active;
}

PoolCore * ClientCore::pool_(unsigned slot) const {
  return slot < m_pools.length() ? m_pools[slot].ptr() : nullptr;
}


} // namespace Zhttp
