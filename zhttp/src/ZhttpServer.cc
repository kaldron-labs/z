//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include "ZhttpServerImpl.hh"

namespace Zhttp {

ServerCore::ServerCore(void *app, const ServerAppOps &ops,
    const CatalogView &req, const CatalogView &res) :
  m_impl{new ServerImpl{this}}, m_app{app}, m_ops{&ops},
  m_reqCatalog{&req}, m_resCatalog{&res} { }

ServerCore::~ServerCore() { delete m_impl; }

bool ServerCore::init(const HubConfig &hub, ServerConfig config) {
  return m_impl->init(hub, ZuMv(config));
}
void ServerCore::stopAccepting() { m_impl->hubs().stopAccepting(); }
void ServerCore::final() { m_impl->final(); }
void ServerCore::start(CtrlFn fn) { m_impl->start(ZuMv(fn)); }
void ServerCore::stop(CtrlFn fn) { m_impl->stop(ZuMv(fn)); }
bool ServerCore::start() { return m_impl->start(); }
bool ServerCore::stop() { return m_impl->stop(); }
int ServerCore::state() const { return m_impl->state(); }
bool ServerCore::running() const { return m_impl->running(); }
bool ServerCore::stopping() const { return m_impl->stopping(); }
bool ServerCore::ok() const { return m_impl->ok(); }
void ServerCore::txErrorFn(ZiTxErrorFn fn) { m_impl->txErrorFn(ZuMv(fn)); }

ZiMultiplex *ServerCore::mx() const { return m_impl->mx(); }
unsigned ServerCore::rxThread() const { return m_impl->rxThread(); }
unsigned ServerCore::txThread() const { return m_impl->txThread(); }

unsigned ServerCore::activeConnections() const { return m_impl->activeConnections(); }
unsigned ServerCore::activeRequests() const { return m_impl->activeRequests(); }
unsigned ServerCore::queuedResponses() const { return m_impl->queuedResponses(); }
uint64_t ServerCore::retainedBytes() const { return m_impl->retainedBytes(); }
uint64_t ServerCore::serverFaults() const { return m_impl->serverFaults(); }
uint64_t ServerCore::rejectedRequests() const { return m_impl->rejectedRequests(); }
uint64_t ServerCore::parseFailures() const { return m_impl->parseFailures(); }
uint64_t ServerCore::bodyFailures() const { return m_impl->bodyFailures(); }
uint64_t ServerCore::responseBuildFailures() const { return m_impl->responseBuildFailures(); }
uint64_t ServerCore::transportFailures() const { return m_impl->transportFailures(); }
unsigned ServerCore::hubCount() const { return m_impl->hubCount(); }

void ServerCore::printQUICDiag() { m_impl->printQUICDiag(); }

bool ServerCore::backends(const HubConfig &hub) {
  for (const auto &profile: profiles())
    if (!profile.server(*this, hub, profile.versions)) return false;
  return true;
}

} // namespace Zhttp
