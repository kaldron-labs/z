//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// ZvEngine - connectivity framework

#include <zlib/ZvEngine.hh>

void ZvEngine::start_()
{
  mgrAddEngine();

  ZtArray<ZmRef<ZvAnyLink>> links(m_links.count_());
  {
    auto i = m_links.citer();
    while (ZmRef<ZvAnyLink> link = i.val())
      links.push(ZuMv(link));
  }
  rxRun([this, links = ZuMv(links)]() {
    bool transitioned = false;
    links.all([&transitioned](const auto &link) {
      transitioned = transitioned || link->up_(false);
    });
    if (!transitioned) started(true);
  });
}
void ZvEngine::stop_()
{
  ZtArray<ZmRef<ZvAnyLink>> links(m_links.count_());
  {
    auto i = m_links.citer();
    while (ZmRef<ZvAnyLink> link = i.val())
      links.push(ZuMv(link));
  }
  rxRun([this, links = ZuMv(links)]() {
    bool transitioned = false;
    links.all([&transitioned](const auto &link) {
      transitioned = transitioned || link->down_(false);
    });
    if (!transitioned) stopped(true);
  });

  mgrDelEngine();
}

void ZvEngine::linkState(ZvAnyLink *link_, int prev, int next)
{
  auto link = static_cast<ZvAnyLink *>(link_);

  if (ZuUnlikely(next == prev)) return;
  switch (prev) {
    case ZvLinkState::Connecting:
    case ZvLinkState::Disconnecting:
    case ZvLinkState::ConnectPending:
    case ZvLinkState::DisconnectPending:
      switch (next) {
	case ZvLinkState::Connecting:
	case ZvLinkState::Disconnecting:
	case ZvLinkState::ConnectPending:
	case ZvLinkState::DisconnectPending:
	  return;
      }
      break;
  }

#if 0
  ZiLOG(Info, "ZvEngine", ([id = link->id(), prev, next](auto &s) {
    s << "link " << id << ' '
      << ZvLinkState::name(prev) << "->" << ZvLinkState::name(next); }));
#endif
  mgrUpdLink(link);

  {
    StateGuard stateGuard(m_stateLock);

    switch (prev) {
      case ZvLinkState::Down:
	--m_down;
	break;
      case ZvLinkState::Disabled:
	--m_disabled;
	break;
      case ZvLinkState::Connecting:
      case ZvLinkState::Disconnecting:
      case ZvLinkState::ConnectPending:
      case ZvLinkState::DisconnectPending:
	--m_transient;
	break;
      case ZvLinkState::Up:
	--m_up;
	break;
      case ZvLinkState::ReconnectPending:
      case ZvLinkState::Reconnecting:
	--m_reconn;
	break;
      case ZvLinkState::Failed:
	--m_failed;
	break;
      default:
	break;
    }

    switch (next) {
      case ZvLinkState::Down:
	++m_down;
	break;
      case ZvLinkState::Disabled:
	++m_disabled;
	break;
      case ZvLinkState::Connecting:
      case ZvLinkState::Disconnecting:
      case ZvLinkState::ConnectPending:
      case ZvLinkState::DisconnectPending:
	++m_transient;
	break;
      case ZvLinkState::Up:
	++m_up;
	break;
      case ZvLinkState::ReconnectPending:
      case ZvLinkState::Reconnecting:
	++m_reconn;
	break;
      case ZvLinkState::Failed:
	++m_failed;
	break;
      default:
	break;
    }

    switch (this->state()) {
      case ZmEngineState::Starting:
      case ZmEngineState::StopPending:
	switch (prev) {
	  case ZvLinkState::Down:
	  case ZvLinkState::Connecting:
	  case ZvLinkState::Disconnecting:
	  case ZvLinkState::ConnectPending:
	  case ZvLinkState::DisconnectPending:
	    if (!(m_down + m_transient)) started(true);
	    break;
	}
      case ZmEngineState::Stopping:
      case ZmEngineState::StartPending:
	switch (prev) {
	  case ZvLinkState::Connecting:
	  case ZvLinkState::Disconnecting:
	  case ZvLinkState::ConnectPending:
	  case ZvLinkState::DisconnectPending:
	  case ZvLinkState::Up:
	    if (!(m_up + m_transient)) stopped(true);
	    break;
	}
	break;
    }
  }
}

bool ZvEngine::final()
{
  return ZmEngine<ZvEngine>::lock(ZmEngineState::Stopped, [this]() {
    m_links.clean();
    m_txPools.clean();
    return true;
  });
}

void ZvEngine::telemetry(Telemetry &data) const
{
  data.id = m_id;
  data.mxID = m_mx->params().id();
  {
    StateReadGuard guard(m_stateLock);
    data.down = m_down;
    data.disabled = m_disabled;
    data.transient = m_transient;
    data.up = m_up;
    data.reconn = m_reconn;
    data.failed = m_failed;
    data.state = this->state();
  }
  {
    ReadGuard guard(m_lock);
    data.nLinks = m_links.count_();
  }
  data.rxThread = m_rxThread;
  data.txThread = m_txThread;
}

// connection state management

ZvAnyTx::ZvAnyTx(ZuID id) : m_id(id)
{
}

void ZvAnyTx::init(ZvEngine *engine)
{
  m_engine = engine;
  m_mx = engine->mx();
}

ZvAnyLink::ZvAnyLink(ZuID id) :
  ZvAnyTx(id),
  m_state(ZvLinkState::Down),
  m_reconnects(0)
{
}

bool ZvAnyLink::up_(bool enable)
{
  // cancel reconnect
  mx()->del(&m_reconnTimer);

  int prev, next;
  bool running = engine()->running();
  bool connect = false;

  {
    StateGuard stateGuard(m_stateLock);

    if (enable) m_enabled = true;

    // state machine
    prev = m_state;
    switch (prev) {
      case ZvLinkState::Disabled:
      case ZvLinkState::Down:
      case ZvLinkState::Failed:
	if (running) {
	  m_state.store_(ZvLinkState::Connecting);
	  connect = true;
	} else
	  m_state.store_(ZvLinkState::Down);
	break;
      case ZvLinkState::Disconnecting:
	if (running && m_enabled)
	  m_state.store_(ZvLinkState::ConnectPending);
	break;
      case ZvLinkState::DisconnectPending:
	if (m_enabled)
	  m_state.store_(ZvLinkState::Connecting);
	break;
      default:
	break;
    }
    next = m_state.load_();
  }

  if (next != prev) engine()->linkState(this, prev, next);
  if (connect) engine()->rxInvoke([this]() { this->connect(); });
  return next != prev;
}

bool ZvAnyLink::down_(bool disable)
{
  int prev, next;
  bool disconnect = false;

  // cancel reconnect
  mx()->del(&m_reconnTimer);

  {
    StateGuard stateGuard(m_stateLock);

    if (disable) m_enabled = false;

    // state machine
    prev = m_state;
    switch (prev) {
      case ZvLinkState::Down:
	if (!m_enabled) m_state.store_(ZvLinkState::Disabled);
	break;
      case ZvLinkState::Up:
      case ZvLinkState::ReconnectPending:
      case ZvLinkState::Reconnecting:
	m_state.store_(ZvLinkState::Disconnecting);
	disconnect = true;
	break;
      case ZvLinkState::Connecting:
	m_state.store_(ZvLinkState::DisconnectPending);
	break;
      case ZvLinkState::ConnectPending:
	m_state.store_(ZvLinkState::Disconnecting);
	break;
      default:
	break;
    }
    next = m_state.load_();
  }

  if (next != prev) engine()->linkState(this, prev, next);
  if (disconnect) engine()->rxInvoke([this]() { this->disconnect(); });
  return next != prev;
}

void ZvAnyLink::connected()
{
  int prev, next;
  bool disconnect = false;

  // cancel reconnect
  mx()->del(&m_reconnTimer);

  {
    StateGuard stateGuard(m_stateLock);

    // state machine
    prev = m_state;
    switch (prev) {
      case ZvLinkState::Connecting:
      case ZvLinkState::ReconnectPending:
      case ZvLinkState::Reconnecting:
	m_state.store_(ZvLinkState::Up);
      case ZvLinkState::Up:
	m_reconnects.store_(0);
	break;
      case ZvLinkState::DisconnectPending:
	m_state.store_(ZvLinkState::Disconnecting);
	disconnect = true;
	m_reconnects.store_(0);
	break;
      default:
	break;
    }
    next = m_state.load_();
  }

  if (next != prev) engine()->linkState(this, prev, next);
  if (disconnect) engine()->rxRun([this]() { this->disconnect(); });
}

void ZvAnyLink::disconnected(bool)
{
  int prev, next;
  bool connect = false;

  // cancel reconnect
  mx()->del(&m_reconnTimer);

  {
    StateGuard stateGuard(m_stateLock);

    // state machine
    prev = m_state;
    switch (prev) {
      case ZvLinkState::Connecting:
      case ZvLinkState::DisconnectPending:
      case ZvLinkState::ReconnectPending:
      case ZvLinkState::Reconnecting:
      case ZvLinkState::Up:
	m_state.store_(m_enabled ? ZvLinkState::Failed : ZvLinkState::Disabled);
	m_reconnects.store_(0);
	break;
      case ZvLinkState::Disconnecting:
	m_state.store_(m_enabled ? ZvLinkState::Down : ZvLinkState::Disabled);
	m_reconnects.store_(0);
	break;
      case ZvLinkState::ConnectPending:
	if (m_enabled) {
	  m_state.store_(ZvLinkState::Connecting);
	  connect = true;
	} else
	  m_state.store_(ZvLinkState::Disabled);
	m_reconnects.store_(0);
	break;
      default:
	break;
    }
    next = m_state.load_();
  }

  if (next != prev) engine()->linkState(this, prev, next);
  if (connect) engine()->rxRun([this]() { this->connect(); });
}

void ZvAnyLink::reconnecting()
{
  int prev, next;

  // cancel reconnect
  mx()->del(&m_reconnTimer);

  {
    StateGuard stateGuard(m_stateLock);

    // state machine
    prev = m_state;
    switch (prev) {
      case ZvLinkState::Up:
	m_state.store_(ZvLinkState::Connecting);
	break;
      default:
	break;
    }
    next = m_state.load_();
  }

  if (next != prev) engine()->linkState(this, prev, next);
}

void ZvAnyLink::reconnect(bool immediate)
{
  int prev, next;
  bool reconnect = false, disconnect = false;
  ZuTime reconnTime;

  // cancel reconnect
  mx()->del(&m_reconnTimer);

  {
    StateGuard stateGuard(m_stateLock);

    // state machine
    prev = m_state;
    switch (prev) {
      case ZvLinkState::Connecting:
      case ZvLinkState::Reconnecting:
      case ZvLinkState::Up:
	m_state.store_(ZvLinkState::ReconnectPending);
	reconnect = true;
	break;
      case ZvLinkState::DisconnectPending:
	disconnect = true;
	break;
      default:
	break;
    }
    next = m_state.load_();

    if (reconnect) {
      m_reconnects.store_(m_reconnects.load_() + 1);
      reconnTime = Zm::now(reconnInterval(m_reconnects.load_()));
    }
  }

  if (next != prev) engine()->linkState(this, prev, next);
  if (reconnect) {
    if (immediate)
      engine()->rxRun([this]() { this->reconnect_(); });
    else
      engine()->mx()->add(&m_reconnTimer, reconnTime, ZmScheduler::Update,
	  [this](auto &&arm) {
	    return arm([this]() { this->reconnect_(); });
	  }, engine()->rxThread());
  }
  if (disconnect) engine()->rxRun([this]() { this->disconnect(); });
}

void ZvAnyLink::reconnect_()
{
  int prev, next;
  bool connect = false;

  {
    StateGuard stateGuard(m_stateLock);

    // state machine
    prev = m_state;
    switch (prev) {
      case ZvLinkState::ReconnectPending:
	m_state.store_(ZvLinkState::Reconnecting);
	connect = true;
	break;
      default:
	break;
    }
    next = m_state.load_();
  }

  if (next != prev) engine()->linkState(this, prev, next);
  if (connect) engine()->rxRun([this]() { this->connect(); });
}

void ZvAnyLink::deleted_()
{
  int prev;

  {
    StateGuard stateGuard(m_stateLock);
    prev = m_state;
    m_state.store_(ZvLinkState::Deleted);
  }
  if (prev != ZvLinkState::Deleted)
    engine()->linkState(this, prev, ZvLinkState::Deleted);
}

void ZvAnyLink::telemetry(Telemetry &data) const
{
  data.id = id();
  data.rxSeqNo = rxSeqNo();
  data.txSeqNo = txSeqNo();
  data.reconnects = m_reconnects.load_();
  data.state = m_state.load_();
}
