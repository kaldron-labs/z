//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z Database

// Notes on replication and failover:

// voted (connected, associated and heartbeated) hosts are sorted in
// priority order (i.e. SN then priority):
//   first-ranked is leader
//   second-ranked is leader's next
//   third-ranked is second-ranked's next
//   etc.

// a new next is selected and recovery/replication restarts when
// - an election ends
// - a new host heartbeats for first time after election completes
// - an existing host disconnects

// a new leader is selected (the local instance may activate/deactivate) when:
// - an election ends
// - a new host heartbeats for first time after election completes
//   - possible deactivation of local instance only -
//   - if self is leader and the new host < this one, we just heartbeat it
// - an existing host disconnects (if that is leader, a new election begins)

// if replicating from primary to DR and a down secondary comes back up,
// then primary's m_next will be DR and DR's m_next will be secondary

// if leader and not replicating, then no host is a replica, so leader runs
// as standalone until peers have recovered

#include <zlib/Zdb.hh>

#include <zlib/ZuMatcher.hh>

#include <zlib/ZtBitWindow.hh>
#include <zlib/ZtHexDump.hh>

#include <zlib/ZiDir.hh>
#include <zlib/ZiModule.hh>

#include <assert.h>
#include <errno.h>

namespace Zdb_ {

static bool reservedTableID(ZuCSpan id)
{
  constexpr auto matcher = ZuMatcher<"saga", "saga_step", "saga_type">();
  return matcher.exact(id) >= 0;
}

void DB::init(
  DBCf config,
  ZiMultiplex *mx,
  DBHandler handler,
  ZmRef<Store> store)
{
  if (!ZmEngine<DB>::lock(ZmEngineState::Stopped,
	[this, &config, mx, &handler, store = ZuMv(store)]() mutable {
    if (state() != HostState::Instantiated) return false;

    static auto invalidSID = [](ZiMultiplex *mx, unsigned sid) -> bool {
      return !sid ||
	  sid > mx->params().nThreads() ||
	  sid == mx->rxThread() ||
	  sid == mx->txThread();
    };

    unsigned nShards = config.nShards;
    if (!nShards || nShards > 64 || (nShards & (nShards - 1)))
      throw ZeEXCEPT(Fatal, "Zdb", ([nShards](auto &s) {
	s << "invalid DB shard count " << nShards;
      }));
    unsigned nThreads = config.threads.length();
    if (nThreads &&
	((nThreads & (nThreads - 1)) || nThreads > nShards))
      throw ZeEXCEPT(Fatal, "Zdb", ([nThreads, nShards](auto &s) {
	s << "invalid DB shard thread count " << nThreads
	  << " (" << nShards << " shards)";
      }));

    config.sid = mx->sid(config.thread);
    if (invalidSID(mx, config.sid))
      throw ZeEXCEPT(Fatal, "Zdb", ([thread = config.thread](auto &s) {
	s << "Zdb thread misconfigured: " << thread; }));

    config.sids.length(0);
    if (!config.threads)
      config.sids.push(config.sid);
    else {
      config.sids.size(config.threads.length());
      config.threads.all([mx, &config](const ZtString<> &thread) {
	auto sid = mx->sid(thread);
	if (invalidSID(mx, sid))
	  throw ZeEXCEPT(Fatal, "Zdb",
	      ([thread = ZeString{thread}](auto &s) {
		s << "Zdb shard thread misconfigured: " << thread; }));
	config.sids.push(sid);
      });
    }

    m_cf = ZuMv(config);
    m_mx = mx;
    m_handler = ZuMv(handler);
    {
      if (!m_cf.storeCf)
	throw ZeEXCEPT(Fatal, "Zdb", ([](auto &s) {
	  s << "no data store configured"; }));
      if (store)
	m_store = ZuMv(store);
      else {
	auto storeCf = ZfCf::handler<StoreLoadCf>(m_cf.storeCf).ctor();
	ZiModule module_;
	auto &path = storeCf.module;
	ZeString e; // dlerror() returns a string
	if (module_.load(path, storeCf.preload ? ZiModule::Pre : 0, &e) < 0)
	  throw ZeEXCEPT(Fatal, "Zdb", ([path = ZeString{path}, e](auto &s) {
	    s << "failed to load \"" << path << "\": " << e; }));
	auto storeFn =
	  reinterpret_cast<StoreFn>(module_.resolve(ZdbStoreFnSym, &e));
	if (!storeFn) {
	  module_.unload();
	  throw ZeEXCEPT(Fatal, "Zdb", ([path = ZeString{path}, e](auto &s) {
	    s << "failed to resolve \"" ZdbStoreFnSym "\" in \""
	      << path << "\": " << e; }));
	}
	m_store = (*storeFn)();
      }
      if (!m_store) throw ZeEXCEPT(Fatal, "Zdb", "null data store");
      InitResult result = m_store->init(
	  m_cf.storeCf, m_mx, m_cf.nShards,
	  FailFn{this, ZmFnPtr<&DB::storeFailed>{}});
      m_cf.storeCf = nullptr;
      if (result.is<Event>()) throw ZuMv(result).p<Event>();
      m_repStore = result.p<InitData>().replicated;
    }

    m_hostIndex.clean();
    m_hosts = new Hosts{};
    bool standalone = false;
    {
      unsigned tblCount = m_tables.count_();
      auto i = m_cf.hostCfs.citer();
      while (auto node = i()) {
	auto host = new Hosts::Node{this, &(node->data()), tblCount};
	if (host->standalone()) standalone = true;
	m_hosts->addNode(host);
	m_hostIndex.addNode(host);
      }
    }
    if (standalone && m_hosts->count_() > 1)
      throw ZeEXCEPT(Fatal, "Zdb", ([id = m_cf.hostID](auto &s) {
	s << "Zdb multiple hosts defined but one or more is standalone"; }));

    m_self = m_hosts->findPtr(m_cf.hostID);
    if (!m_self)
      throw ZeEXCEPT(Fatal, "Zdb", ([id = m_cf.hostID](auto &s) {
	s << "Zdb own host ID " << id << " not in hosts table"; }));
    state(HostState::Initialized);

    return true;
  }))
    throw ZeEXCEPT(Fatal, "Zdb", "Zdb::init called out of order");
  Ztc::DBMgr::add(this);
}

ZmRef<AnyTable> DB::initTable_(
  ZuCSpan id, InitTableFn fn)
{
  if (reservedTableID(id))
    throw ZeEXCEPT(Error, "Zdb", ([id](auto &s) {
      s << "Zdb::initTable(\"" << id << "\") - reserved identifier";
    }));
  if (id.length() >= IDSize_)
    throw ZeEXCEPT(Error, "Zdb", ([id](auto &s) {
      s << "Zdb::initTable(\"" << id << "\") - identifier too long (>"
	<< IDSize_ << " bytes)"; }));
  ZmRef<AnyTable> table;
  if (!ZmEngine<DB>::lock(ZmEngineState::Stopped,
	[this, &table, id, fn = ZuMv(fn)]() {
    if (state() != HostState::Initialized) return false;
    if (m_tables.findVal(id)) return false;
    auto cf = m_cf.tableCfs.find(id);
    if (!cf) m_cf.tableCfs.addNode(cf = new TableCfs::Node{id});
    table = fn(this, &(cf->val()));
    m_tables.add(table);
    tableAdded_(table);
    return true;
  }))
    throw ZeEXCEPT(Fatal, "Zdb", "Zdb::initTable called out of order");
  return table;
}

void DB::sagas_(
    SagaRecoveryFn catalogFn, SagaRecoveryFn scanFn, SagaRunFn runFn)
{
  if (!ZmEngine<DB>::lock(ZmEngineState::Stopped,
	[
	  this, catalogFn = ZuMv(catalogFn),
	  scanFn = ZuMv(scanFn), runFn = ZuMv(runFn)
	]() mutable {
    if (state() != HostState::Initialized || m_sagaCatalogFn) return false;
    m_sagaCatalogFn = ZuMv(catalogFn);
    m_sagaScanFn = ZuMv(scanFn);
    m_sagaRunFn = ZuMv(runFn);
    m_sagaTable = new Table<SagaData>{this, &m_sagaCf, true};
    m_sagaStepTable = new Table<SagaStep>{this, &m_sagaStepCf, true};
    m_tables.add(m_sagaTable);
    m_tables.add(m_sagaStepTable);
    tableAdded_(m_sagaTable);
    tableAdded_(m_sagaStepTable);
    return true;
  }))
    throw ZeEXCEPT(Fatal, "Zdb", "Zdb::sagas called out of order");
}

void DB::sagaRun(ZmRef<Saga> saga)
{
  ZmAssert(invoked());
  if (ZuUnlikely(!saga || saga->m_epoch != m_sagaEpoch ||
	m_sagaState == SagaState::Inactive ||
	(m_sagaState == SagaState::Active && !m_appActive))) return;
  saga->m_rec = m_sagaState == SagaState::Rebuilding ?
    sagaRec(saga, saga->m_step) : nullptr;
  saga->m_uns = m_sagaState == SagaState::Rebuilding ?
    m_sagaUNHash.ptr() : nullptr;
  ++m_sagaPending;
  m_sagaRunFn(ZuMv(saga));
}

void DB::sagaDrain(DrainFn fn)
{
  ZmAssert(invoked());
  m_sagaDrainFn = ZuMv(fn);
  if (!m_sagaPending) run([this]() { sagaDrained(); });
}

void DB::sagaRetire()
{
  ZmAssert(invoked() && m_sagaPending);
  if (!--m_sagaPending && m_sagaDrainFn)
    run([this]() { sagaDrained(); });
}

void DB::sagaDrained()
{
  ZmAssert(invoked());
  if (m_sagaPending || !m_sagaDrainFn) return;
  auto fn = ZuMv(m_sagaDrainFn);
  fn();
}

void DB::sagaClean()
{
  ZmAssert(invoked() && !m_sagaPending);
  m_sagaBlocked = nullptr;
  m_sagaUNHash = nullptr;
  m_sagaStepHash = nullptr;
  m_sagaQueue.clean();
  m_sagaHash = nullptr;
  m_sagaScan = nullptr;
  m_sagaLive = 0;
}

void DB::sagaDeactivate()
{
  ZmAssert(invoked());
  if (!m_sagaCatalogFn) return;
  ++m_sagaEpoch;
  m_sagaState = SagaState::Inactive;
  sagaDrain([this]() {
    sagaClean();
    if (Engine::stopping()) stop_0();
  });
}

bool DB::sagaPrepare(
    Saga *saga, AnyTable *table, Shard shard, SagaOp::T op,
    bool effect, UN &un, bool &saved)
{
  if (ZuUnlikely(!table || table->db() != this || shard >= nShards() ||
      saga->m_step >= saga->m_locs.length())) {
    saga->result_(OpResult::Invalid, shard);
    return false;
  }
  if (!saga->m_uns) return true;
  if (auto rec = saga->m_rec) {
    if (ZuUnlikely(rec->table != table || rec->shard != shard ||
	rec->op != op)) {
      saga->result_(OpResult::Invalid, shard);
      return false;
    }
    un = rec->un;
    saved = true;
    return true;
  }
  if (effect && saga->m_uns->find(
	SagaUNKey{table, shard, table->nextUN(shard)})) {
    saga->result_(OpResult::NotReady, shard);
    return false;
  }
  return true;
}

void DB::sagaResult(
    ZmRef<Saga> saga, uint64_t epoch, OpResult::T result, Shard shard)
{
  ZmAssert(invoked());
  if (ZuUnlikely(!saga)) return;
  if (ZuUnlikely(epoch != m_sagaEpoch || saga->m_epoch != epoch)) {
    sagaRetire();
    return;
  }
  sagaRetire();
  switch (result) {
    case OpResult::NotReady: {
      auto head = m_sagaQueue.headPtr();
      if (ZuUnlikely(m_sagaState != SagaState::Rebuilding ||
	  !head || static_cast<SagaNode__ *>(head)->saga != saga)) {
	sagaFail(saga, epoch, ZeEXCEPT(Fatal, "Zdb", "invalid saga deferral"));
	return;
      }
      if (m_sagaBlocked == saga) {
	sagaActivateFail(ZeEXCEPT(Fatal, "Zdb", "recovered sagas made no progress"));
	return;
      }
      if (!m_sagaBlocked) m_sagaBlocked = saga;
      m_sagaQueue.rshift();
      sagaReplay();
      return;
    }
    case OpResult::Missing:
      sagaFail(saga, epoch, ZeEXCEPT(Fatal, "Zdb", "saga mutation row is missing"));
      return;
    default:
      sagaFail(saga, epoch, ZeEXCEPT(Fatal, "Zdb", "invalid saga mutation"));
      return;
  }
}

void DB::sagaStepRecovered(
    ZmRef<Saga> saga, uint64_t epoch, uint32_t step, Shard shard)
{
  ZmAssert(invoked());
  if (ZuUnlikely(!saga || epoch != m_sagaEpoch || saga->m_epoch != epoch)) {
    sagaRetire();
    return;
  }
  if (ZuUnlikely(saga->m_step != step || step >= saga->m_locs.length())) {
    sagaRetire();
    sagaFail(ZuMv(saga), epoch,
	ZeEXCEPT(Fatal, "Zdb", "invalid saga step completion"));
    return;
  }
  if (auto rec = sagaRec(saga, step)) {
    if (rec->un != nullUN())
      m_sagaUNHash->delNode(static_cast<SagaUNHash::Node *>(rec));
    m_sagaStepHash->delNode(rec);
  }
  saga->m_locs[step] = shard;
  m_sagaBlocked = nullptr;
  ++saga->m_step;
  sagaRetire();
  sagaRun(ZuMv(saga));
}

void DB::sagaFail(ZmRef<Saga> saga, uint64_t epoch, ZeException e)
{
  ZmAssert(invoked());
  if (ZuUnlikely(!saga || epoch != m_sagaEpoch ||
	saga->m_epoch != epoch)) return;
  if (m_sagaState == SagaState::Rebuilding) {
    sagaActivateFail(ZuMv(e));
    return;
  }
  ZiLogEvent(ZuMv(e));
  fail();
}

void DB::final()
{
  ZdbDEBUG(this, ([hostID = m_cf.hostID, state = this->state()](auto &s) {
    s << hostID << " state=" << HostState::name(state);
  }));

  if (!ZmEngine<DB>::lock(ZmEngineState::Stopped, [this]() {
    if (state() != HostState::Initialized) return false;
    ZmAssert(!m_sagaPending && !m_sagaDrainFn);
    ZmAssert(!m_sagaLive);
    // reset recovery
    m_recovering = 0; m_recover.reset(); m_recoverEnd.reset();
    // reset replication (clearing m_self also sets state to Instantiated)
    m_self = m_leader = m_prev = m_next = nullptr;
    m_nPeers = 0; m_standalone = false;
    {
      auto i = m_tables.citer<ZmRBTreeLess>();
      while (auto table = i.val()) tableDeleted_(table);
    }
    {
      auto i = m_hosts->citer();
      while (auto host = i()) hostDeleted_(host);
    }
    m_cxns.clean(); m_hostIndex.clean(); m_hosts->clean(); m_hosts = {};
    // reset tables
    m_nextSN = 0;
    m_tables.clean();
    m_sagaTable = nullptr;
    m_sagaStepTable = nullptr;
    m_sagaUNHash = nullptr;
    m_sagaStepHash = nullptr;
    m_sagaQueue.clean();
    m_sagaScan = nullptr;
    m_sagaHash = nullptr;
    m_sagaCatalogFn = {};
    m_sagaScanFn = {};
    m_sagaRunFn = {};
    m_sagaState = SagaState::Inactive;
    m_sagaEpoch = 0;
    m_sagaLive = 0;
    m_sagaBlocked = nullptr;
    // reset handler
    m_handler = {};
    // reset backing data store
    if (m_store) {
      m_store->final();
      m_store = nullptr;
    }
    return true;
  }))
    throw ZeEXCEPT(Fatal, "Zdb", "Zdb::final called out of order");
  Ztc::DBMgr::del(this);
}

unsigned DB::allDBHosts(Ztc::DB::AllDBHostsFn fn) const
{
  ZmRef<Hosts> hosts = m_hosts;
  if (!hosts) return 0;
  unsigned n = 0;
  auto i = hosts->citer();
  while (auto host = i()) {
    ++n;
    fn(host);
  }
  return n;
}

unsigned DB::allDBTables(Ztc::DB::AllDBTablesFn fn) const
{
  unsigned n = 0;
  auto i = m_tables.citer();
  while (auto table = i.val()) {
    ++n;
    fn(table);
  }
  return n;
}

void DB::wake()
{
  run([this]() { stopped(); });	// polling stopped(), may call stop_()
}

void DB::start_()
{
  ZdbDEBUG(this, ([hostID = m_cf.hostID, state = this->state()](auto &s) {
    s << hostID << " state=" << HostState::name(state);
  }));

  ZmAssert(invoked());

  using namespace HostState;

  if (state() != Initialized) {
    ZiLOG(Fatal, "Zdb", "DB::start_ called out of order");
    started(false);
    return;
  }

  ZiLOG(Info, "Zdb", "starting");

  // start backing data store
  m_store->start([this](StartResult result) {
    if (ZuUnlikely(result.is<Event>())) {
      ZiLogEvent(ZuMv(result).p<Event>());
      ZiLOG(Fatal, "Zdb", ([](auto &s) {
	s << "data store start failed";
      }));
      run([this]() { started(false); });
      return;
    }
    run([this]() { start_1(); });
  });
}

void DB::start_1()
{
  ZdbDEBUG(this, "opening all tables");

  // open and recover all tables
  all([](AnyTable *table, DB::AllTableFn done) {
    table->open([done = ZuMv(done)](bool ok) mutable { done(ok); });
  }, [](DB *db, bool ok) {
    if (!ok) {
      db->startFailed();
      return;
    }
    db->m_sagaCatalogFn ? db->m_sagaCatalogFn() : db->start_2();
  });
}

void DB::startFailed()
{
  ZmAssert(invoked());
  m_appActive = false;
  all([](AnyTable *table, DB::AllTableFn done) {
    table->close([done = ZuMv(done)]() mutable { done(true); });
  }, [](DB *db, bool) {
    db->m_store->stop([db](StopResult result) {
      if (result.is<Event>()) ZiLogEvent(ZuMv(result).p<Event>());
      db->run([db]() {
	db->drainShards(0, [db]() {
	  db->state(HostState::Initialized);
	  db->started(false);
	});
      });
    });
  });
}

void DB::sagaActivate(Host *oldMaster)
{
  ZmAssert(invoked());
  ++m_sagaEpoch;
  m_sagaState = SagaState::Rebuilding;
  m_appActive = false;
  sagaDrain([this, oldMaster]() {
    sagaClean();
    m_sagaHash = new SagaHashObj{};
    m_sagaStepHash = new SagaStepHashObj{};
    m_sagaUNHash = new SagaUNHashObj{};
    m_sagaScan = new SagaScan{};
    m_sagaScan->oldMaster = oldMaster;
    m_sagaScan->epoch = m_sagaEpoch;
    m_sagaScanFn();
  });
}

SagaRec *DB::sagaRec(Saga *saga, unsigned step)
{
  ZmAssert(invoked());
  if (!m_sagaStepHash) return nullptr;
  auto rec = m_sagaStepHash->findPtr(SagaStepKey{saga->type(), saga->id(), step});
  return static_cast<SagaRec *>(rec);
}

bool DB::sagaReserved(AnyTable *table, Shard shard, UN un)
{
  ZmAssert(invoked());
  return m_sagaUNHash && m_sagaUNHash->find(SagaUNKey{table, shard, un});
}

void DB::sagaReplay()
{
  ZmAssert(invoked());
  if (ZuUnlikely(m_sagaState != SagaState::Rebuilding ||
	!m_sagaScan || m_sagaScan->epoch != m_sagaEpoch)) return;
  auto node = m_sagaQueue.headPtr();
  if (!node) {
    sagaActivateDone();
    return;
  }
  sagaRun(static_cast<SagaNode__ *>(node)->saga);
}

void DB::sagaActivateDone()
{
  ZmAssert(invoked());
  Host *oldMaster = m_sagaScan->oldMaster;
  ZmAssert(!m_sagaUNHash->count_() && !m_sagaStepHash->count_());
  ZmAssert(!m_sagaHash->count_() && !m_sagaQueue.headPtr());
  m_sagaHash = nullptr;
  m_sagaUNHash = nullptr;
  m_sagaStepHash = nullptr;
  m_sagaScan = nullptr;
  m_sagaBlocked = nullptr;
  m_sagaState = SagaState::Active;
  m_appActive = true;
  up_(oldMaster);
  electionDone();
}

void DB::sagaActivateFail(ZeException e)
{
  ZmAssert(invoked());
  if (m_sagaState != SagaState::Rebuilding) return;
  ZiLogEvent(ZuMv(e));
  ZiLOG(Fatal, "Zdb", "saga activation failed");
  ++m_sagaEpoch;
  m_sagaState = SagaState::Inactive;
  sagaDrain([this]() {
    sagaClean();
    switch (ZmEngine::state()) {
    case ZmEngineState::Starting:
    case ZmEngineState::StopPending:
      startFailed();
      break;
    default:
      fail();
      break;
    }
  });
}

void DB::start_2()
{
  ZdbDEBUG(this, ([hostID = m_cf.hostID, state = this->state()](auto &s) {
    s << hostID << " state=" << HostState::name(state);
  }));

  ZmAssert(invoked());

  using namespace HostState;

  // refresh table state vector, begin election
  dbStateRefresh();
  repStop();
  state(Electing);

  if (!(m_nPeers = m_hosts->count_() - 1)) { // standalone
    holdElection();
    return;
  }

  mx()->add(&m_hbSendTimer, m_hbSendTime = Zm::now(), ZmScheduler::Update,
      [this](auto &&arm) { return arm([this]() { hbSend(); }); },
      sid());
  mx()->add(&m_electTimer, Zm::now(int(m_cf.electionTimeout)),
      ZmScheduler::Update,
      [this](auto &&arm) { return arm([this]() { holdElection(); }); },
      sid());

  listen();

  {
    auto i = m_hostIndex.citer<ZmRBTreeLess>(Host::IndexAxor(*m_self));
    while (Host *host = i()) host->connect();
  }
}

void DB::stop_()
{
  ZdbDEBUG(this, ([hostID = m_cf.hostID, state = this->state()](auto &s) {
    s << hostID << " state=" << HostState::name(state);
  }));

  ZmAssert(invoked());

  using namespace HostState;

  switch (state()) {
    case Active:
    case Inactive:
      break;
    case Electing:	// holdElection will resume stop_0() at completion
      return;
    case Stopping:	// saga drain may precede the engine's queued stop wake
    case Initialized:	// tables closed; store stop/shard drain still in flight
      return;
    default:
      ZiLOG(Fatal, "Zdb", "DB::stop_ called out of order");
      stopped(false);
      return;
  }

  ZiLOG(Info, "Zdb", "stopping");

  stop_0();
}

void DB::stop_0()
{
  ZdbDEBUG(this, ([hostID = m_cf.hostID, state = this->state()](auto &s) {
    s << hostID << " state=" << HostState::name(state);
  }));

  ZmAssert(invoked());

  // re-check state, stop_0() is resumed via holdElection()

  using namespace HostState;

  switch (state()) {
    case Active:
    case Inactive:
      break;
    default:
      return;
  }

  // Keep admitted sagas running, including their row cleanup and done callback.
  // The engine state already rejects new admission. Completion resumes here.
  if (m_sagaPending || m_sagaDrainFn ||
      m_sagaState == SagaState::Rebuilding ||
      (m_sagaState == SagaState::Active && m_sagaLive)) return;
  m_sagaState = SagaState::Inactive;
  m_appActive = false;
  state(Stopping);
  repStop();
  m_mx->del(&m_hbSendTimer);
  m_mx->del(&m_electTimer);

  // cancel reconnects
  {
    auto i = m_hostIndex.citer<ZmRBTreeLess>(Host::IndexAxor(*m_self));
    while (Host *host = i()) host->cancelConnect();
  }

  stopListening();

  // close all connections (and wait for them to be disconnected)
  if (!disconnectAll()) stop_1();
}

void DB::stop_1()
{
  ZdbDEBUG(this, ([hostID = m_cf.hostID, state = this->state()](auto &s) {
    s << hostID << " state=" << HostState::name(state);
  }));

  ZmAssert(invoked());

  // close all tables
  all([](AnyTable *table, DB::AllTableFn done) {
    table->close([done = ZuMv(done)]() mutable { done(true); });
  }, [](DB *db, bool) {
    db->stop_2();
  });
}

void DB::stop_2()
{
  ZdbDEBUG(this, ([hostID = m_cf.hostID, state = this->state()](auto &s) {
    s << hostID << " state=" << HostState::name(state);
  }));

  ZmAssert(invoked());

  state(HostState::Initialized);

  // stop backing data store
  m_store->stop([this](StopResult result) {
    if (ZuUnlikely(result.is<Event>())) {
      ZiLogEvent(ZuMv(result).p<Event>());
      ZiLOG(Fatal, "Zdb", "data store stop failed");
    }
    run([this]() { drainShards(0, [this]() { stopped(true); }); });
  });
}

void DB::drainShards(unsigned shard, DrainFn fn)
{
  ZmAssert(invoked());
  if (shard == nShards()) {
    fn();
    return;
  }
  // Store close/stop has emitted all write callbacks. Drain their shard work
  // before final() may release the table objects referenced by those callbacks.
  shardRun(Shard(shard), [this, shard, fn = ZuMv(fn)]() mutable {
    run([this, shard, fn = ZuMv(fn)]() mutable {
      drainShards(shard + 1, ZuMv(fn));
    });
  });
}

bool DB::disconnectAll()
{
  ZmAssert(invoked());

  bool disconnected = false;
  auto i = m_cxns.citer();
  while (auto cxn = i())
    if (cxn->up()) {
      cxn->sync(); // drain tx
      disconnected = true;
      cxn->disconnect();
    }
  return disconnected;
}

void DB::listen()
{
  ZmAssert(invoked());

  if (!m_self->standalone())
    m_mx->listen(
	ZiListenFn{this, ZmFnPtr<&DB::listening>{}},
	ZiFailFn{this, ZmFnPtr<&DB::listenFailed>{}},
	ZiConnectFn{this, ZmFnPtr<&DB::accepted>{}},
	m_self->ip(), m_self->port(), m_cf.nAccepts);
}

void DB::listening(const ZiListenInfo &)
{
  ZiLOG(Info, "Zdb", ([ip = m_self->ip(), port = m_self->port()](auto &s) {
    s << "listening on (" << ip << ':' << port << ')';
  }));
}

void DB::listenFailed(bool transient)
{
  bool retry = transient && running();
  if (retry)
    m_mx->add(&m_listenTimer, Zm::now(int(m_cf.reconnectFreq)),
	ZmScheduler::Update,
	[this](auto &&arm) { return arm([this]() { listen(); }); },
	m_cf.sid);
  ZiLOG(Warning, "Zdb", ([ip = m_self->ip(), port = m_self->port(), retry](auto &s) {
    s << "listen failed on (" << ip << ':' << port << ')';
    if (retry) s << " - retrying...";
  }));
}

void DB::stopListening()
{
  m_mx->del(&m_listenTimer);
  if (!m_self->standalone()) {
    ZiLOG(Info, "Zdb", "stop listening");
    m_mx->stopListening(m_self->ip(), m_self->port());
  }
}

void DB::holdElection()
{
  ZdbDEBUG(this, ([hostID = m_cf.hostID, state = this->state()](auto &s) {
    s << hostID << " state=" << HostState::name(state);
  }));

  ZmAssert(invoked());

  bool won, appActive;
  Host *oldMaster;

  m_mx->del(&m_electTimer);

  using namespace HostState;

  if (state() != Electing) return;

  appActive = m_appActive;

  oldMaster = setMaster();

  if (won = m_leader == m_self) {
    m_prev = nullptr;
    if (!m_nPeers)
      ZiLOG(Warning, "Zdb", "activating standalone");
    else
      hbSend_(); // announce new leader
  } else {
    m_appActive = false;
  }

  if (won) {
    if (!appActive && m_sagaCatalogFn) {
      state(Active);
      setNext();
      sagaActivate(oldMaster);
      return;
    }
    m_appActive = true;
    if (!appActive) up_(oldMaster);
  } else {
    sagaDeactivate();
    if (appActive) down_(false);
  }

  state(won ? Active : Inactive);
  setNext();

  electionDone();
}

void DB::electionDone()
{
  switch (ZmEngine::state()) {
    case ZmEngineState::Starting:
    case ZmEngineState::StopPending:
      started(true);
      break;
    case ZmEngineState::Stopping:
    case ZmEngineState::StartPending:
      run([this]() { stop_0(); });
      break;
  }
}

void DB::fail()
{
  ZmAssert(invoked());

  // Do not replace an already-failing activation's startup completion.
  if (m_sagaState == SagaState::Inactive && m_sagaDrainFn) return;
  if (m_sagaState == SagaState::Rebuilding) {
    sagaActivateFail(ZeEXCEPT(Fatal, "Zdb", "DB failure during saga recovery"));
    return;
  }

  if (!m_self) {
    ZiLOG(Fatal, "Zdb", "DB::fail called out of order");
    return;
  }

  deactivate(true);
}

void DB::deactivate(bool failed)
{
  ZmAssert(invoked());

  if (!m_self) {
badorder:
    ZiLOG(Fatal, "Zdb", "DB::deactivate called out of order");
    return;
  }

  using namespace HostState;

  switch (state()) {
    case Instantiated:
    case Initialized:
    case Stopping:
      goto badorder;
    case Inactive:
      return;
    default:
      break;
  }

  bool appActive = m_appActive;
  m_self->voted(false);
  setMaster();
  m_self->voted(true);
  m_appActive = false;
  sagaDeactivate();

  if (appActive) down_(failed);

  state(Inactive);
  setNext();
}

void Host::reactivate()
{
  m_db->reactivate(static_cast<Host *>(this));
}

void DB::reactivate(Host *host)
{
  ZmAssert(invoked());

  if (ZmRef<Cxn> cxn = host->cxn()) cxn->hbSend();

  bool appActive = m_appActive;
  if (!appActive && m_sagaCatalogFn) {
    sagaActivate(nullptr);
    return;
  }
  m_appActive = true;
  if (!appActive) up_(nullptr);
}

void DB::up_(Host *oldMaster)
{
  ZiLOG(Info, "Zdb", "ACTIVE");
  m_handler.upFn(this, oldMaster);
}

void DB::down_(bool failed)
{
  ZiLOG(Info, "Zdb", "INACTIVE");
  m_handler.downFn(this, failed);
}

void DB::all(AllFn fn, AllDoneFn doneFn)
{
  ZmAssert(invoked());

  if (ZuUnlikely(m_allCount)) {
    ZiLOG(Fatal, "Zdb", ([](auto &s) {
      s << "multiple overlapping calls to all()";
    }));
    doneFn(this, false);
    return;
  }
  auto i = m_tables.citer();
  m_allCount = m_allNotOK = m_tables.count_();
  if (ZuUnlikely(!m_allCount)) {
    ZiLOG(Fatal, "Zdb", ([](auto &s) { s << "Zdb - no tables"; }));
    doneFn(this, false);
    return;
  }
  m_allFn = ZuMv(fn);
  m_allDoneFn = ZuMv(doneFn);
  while (auto table = i.val().ptr())
    table->invoke(0, [table]() {
      auto db = table->db();
      db->m_allFn(table, AllTableFn{db, [](DB *db, bool ok) {
	db->invoke([db, ok]() { db->allDone(ok); });
      }});
    });
}

void DB::allDone(bool ok)
{
  ZmAssert(invoked());

  if (ZuUnlikely(!m_allCount)) return;
  if (ok) --m_allNotOK;
  if (!--m_allCount) {
    m_allDoneFn(this, !m_allNotOK);
    m_allFn = AllFn{};
    m_allDoneFn = AllDoneFn{};
    m_allCount = m_allNotOK = 0;
  }
}

void DB::telemetry(Ztc::DBTelemetry &data) const
{
  data.thread = m_cf.thread;
  data.threads = m_cf.threads;
  data.nShards = m_cf.nShards;
  data.self = telKey();
  data.leader = m_leader ? m_leader->id() : ZuCSpan{};
  data.prev = m_prev ? m_prev->id() : ZuCSpan{};
  data.next = m_next ? m_next->id() : ZuCSpan{};
  data.nCxns = m_cxns.count_();
  data.heartbeatFreq = m_cf.heartbeatFreq;
  data.heartbeatTimeout = m_cf.heartbeatTimeout;
  data.reconnectFreq = m_cf.reconnectFreq;
  data.electionTimeout = m_cf.electionTimeout;
  data.nTables = m_tables.count_();
  data.nHosts = m_hosts ? m_hosts->count_() : 0;
  data.nPeers = m_nPeers;
  data.state = state();
  data.active = state() == HostState::Active;
  data.recovering = m_recovering;
  data.replicating = Host::replicating(m_next);
}

Ztc::DBHostKey Host::telKey() const
{
  return {m_db->telKey(), m_cf->id};
}

void Host::telemetry(Ztc::DBHostTelemetry &data) const
{
  data.ip = m_cf->ip;
  data.dbID = m_db->telKey();
  data.id = m_cf->id;
  data.priority = m_cf->priority;
  data.port = m_cf->port;
  data.state = m_state;
  data.voted = m_voted;
}

Host::Host(DB *db, const HostCf *cf, unsigned tblCount) :
  m_db{db},
  m_cf{cf},
  m_mx{db->mx()},
  m_dbState{tblCount}
{
}

void Host::connect()
{
  if (m_cxn) return;

  ZiLOG(Info, "Zdb",
      ([id = ZeString{this->id()}, ip = config().ip, port = config().port](auto &s) {
	s << "Zdb connecting to host " << id
	  << " (" << ip << ':' << port << ')';
      }));

  m_mx->connect(
      ZiConnectFn{this, ZmFnPtr<&Host::connected>{}},
      ZiFailFn{this, ZmFnPtr<&Host::connectFailed>{}},
      ZiIP{}, 0, config().ip, config().port);
}

void Host::connectFailed(bool transient)
{
  bool retry = transient && m_db->running();
  if (retry) reconnect();
  ZiLOG(Warning, "Zdb",
      ([id = ZeString{this->id()},
	ip = config().ip,
	port = config().port,
	retry](auto &s) {
    s << "failed to connect to host " << id
      << " (" << ip << ':' << port << ')';
    if (retry) s << " - retrying...";
  }));
}

ZiConnection *Host::connected(const ZiCxnInfo &ci)
{
  ZiLOG(Info, "Zdb",
      ([id = ZeString{this->id()},
	remoteIP = ci.remoteIP, remotePort = ci.remotePort,
	localIP = ci.localIP, localPort = ci.localPort](auto &s) {
    s << "connected to host " << id << " ("
      << remoteIP << ':' << remotePort << "): "
      << localIP << ':' << localPort;
  }));

  if (!m_db->running()) return nullptr;

  return new Cxn{m_db, this, ci};
}

ZiConnection *DB::accepted(const ZiCxnInfo &ci)
{
  ZiLOG(Info, "Zdb",
      ([remoteIP = ci.remoteIP, remotePort = ci.remotePort,
	localIP = ci.localIP, localPort = ci.localPort](auto &s) {
    s << "accepted cxn on ("
      << remoteIP << ':' << remotePort << "): "
      << localIP << ':' << localPort;
  }));

  if (!running()) return nullptr;

  return new Cxn{this, nullptr, ci};
}

Cxn_::Cxn_(DB *db, Host *host, const ZiCxnInfo &ci) :
  ZiConnection{db->mx(), ci},
  m_db{db},
  m_host{host}
{
}

void Cxn_::connected(ZiIOContext &io)
{
  if (!m_db->running()) { io.disconnect(); return; }

  m_db->run([self = ZmRef(this)]() mutable {
    auto db = self->db();
    db->connected(ZuMv(self));
  });

  m_db->mx()->add(&m_hbTimer, Zm::now(int(m_db->config().heartbeatTimeout)),
      ZmScheduler::Defer,
      [this](auto &&arm) {
	return arm([self = ZmRef(this)]() mutable { self->hbTimeout(); });
      }, m_db->sid());

  msgRead(io);
}

void DB::connected(ZmRef<Cxn> cxn)
{
  ZmAssert(invoked());

  if (!cxn->up()) return;

  if (Host *host = cxn->host()) associate(cxn, host);

  hbSend_(cxn);

  m_cxns.addNode(ZuMv(cxn));
}

void DB::associate(Cxn *cxn, ZuCSpan hostID)
{
  ZmAssert(invoked());

  Host *host = m_hosts->find(hostID);

  if (!host) {
    ZiLOG(Error, "Zdb", ([hostID = ZeString{hostID}](auto &s) {
      s << "cannot associate incoming cxn: host ID "
	<< hostID << " not found";
    }));
    return;
  }

  if (host == m_self) {
    ZiLOG(Error, "Zdb", ([hostID = ZeString{hostID}](auto &s) {
      s << "cannot associate incoming cxn: host ID "
	<< hostID << " is same as self";
    }));
    return;
  }

  if (cxn->host() == host) return;

  associate(cxn, host);
}

void DB::associate(Cxn *cxn, Host *host)
{
  ZmAssert(invoked());

  ZiLOG(Info, "Zdb", ([hostID = ZeString{host->id()}](auto &s) {
    s << "host " << hostID << " CONNECTED";
  }));

  cxn->host(host);

  host->associate(cxn);

  host->voted(false);
}

void Host::associate(Cxn *cxn)
{
  ZmAssert(m_db->invoked());

  if (ZuUnlikely(m_cxn && m_cxn.ptr() != cxn)) {
    m_cxn->host(nullptr);
    m_cxn->disconnect();
  }
  m_cxn = cxn;
}

void Host::reconnect()
{
  m_mx->add(&m_connectTimer, Zm::now(int(m_db->config().reconnectFreq)),
      ZmScheduler::Defer,
      [this](auto &&arm) { return arm([this]() { connect(); }); },
      m_db->sid());
}

void Host::cancelConnect()
{
  m_mx->del(&m_connectTimer);
}

void Cxn_::hbTimeout()
{
  ZiLOG(Info, "Zdb",
      ([id = m_host ? ZuID{m_host->id()} : ZuID{"unknown"},
	ip = info().remoteIP, port = info().remotePort](auto &s) {
    s << "heartbeat timeout on host "
      << id << " (" << ip << ':' << port << ')';
  }));

  disconnect();
}

void Cxn_::disconnected(bool)
{
  ZiLOG(Info, "Zdb",
      ([id = m_host ? ZuID{m_host->id()} : ZuID{"unknown"},
	ip = info().remoteIP, port = info().remotePort](auto &s) {
    s << "disconnected from host "
      << id << " (" << ip << ':' << port << ')';
  }));

  mx()->del(&m_hbTimer);

  m_db->run([self = ZmRef(this)]() mutable {
    auto db = self->db();
    db->disconnected(ZuMv(self));
  });
}

void DB::disconnected(ZmRef<Cxn> cxn)
{
  ZmAssert(invoked());

  m_cxns.delNode(cxn);

  Host *host = cxn->host();

  if (!host || host->cxn() != cxn) return;

  ZiLOG(Info, "Zdb", ([id = ZeString{host->id()}](auto &s) {
    s << "host " << id << " DISCONNECTED";
  }));

  host->disconnected();

  switch (ZmEngine::state()) {
    case ZmEngineState::Stopping:
    case ZmEngineState::StartPending:
      if (--m_nPeers <= 0) run([this]() { stop_1(); });
      break;
  }

  using namespace HostState;

  host->state(Instantiated);
  host->voted(false);

  switch (state()) {
    case Active:
    case Inactive:
      break;
    default:
      goto ret;
  }

  if (host == m_prev) m_prev = nullptr;

  if (host == m_leader) {
    switch (state()) {
      case Inactive:
	state(Electing);
	holdElection();
	break;
    }
    goto ret;
  }

  if (host == m_next) setNext();

ret:
  if (running() && Host::IndexAxor(*host) < Host::IndexAxor(*m_self))
    host->reconnect();
}

void Host::disconnected()
{
  m_cxn = nullptr;
}

Host *DB::setMaster()
{
  ZmAssert(invoked());

  Host *oldMaster = m_leader;

  dbStateRefresh();

  m_leader = nullptr;
  m_nPeers = 0;

  {
    auto i = m_hostIndex.citer();

    ZdbDEBUG(this, ZeString{} << "setMaster()\n"
      << " self=" << ZuPrintPtr{m_self} << '\n'
      << " prev=" << ZuPrintPtr{m_prev} << '\n'
      << " next=" << ZuPrintPtr{m_next} << '\n'
      << " recovering=" << m_recovering
      << " replicating=" << Host::replicating(m_next));

    while (Host *host = i()) {
      ZdbDEBUG(this, ZeString{}
	<< " host=" << ZuPrintPtr{host} << '\n'
	<< " leader=" << ZuPrintPtr{m_leader});

      if (host->voted()) {
	if (host != m_self) ++m_nPeers;
	if (!m_leader) { m_leader = host; continue; }
	int diff = host->cmp(m_leader);
	if (ZuNull(diff)) {
	  m_leader = nullptr;
	  break;
	} else if (diff > 0)
	  m_leader = host;
      }
    }
  }

  if (m_leader) {
    ZiLOG(Info, "Zdb", ([id = ZeString{m_leader->id()}](auto &s) {
      s << "host " << id << " is leader";
    }));
  } else
    ZiLOG(Fatal, "Zdb", "leader election failed");

  return oldMaster;
}

void DB::setNext(Host *host)
{
  ZmAssert(invoked());

  m_next = host;
  m_recovering = false;

  if (m_next) {
    m_standalone = false;
    repStart();
  } else {
    m_standalone = true;
  }
}

void DB::setNext()
{
  ZmAssert(invoked());

  Host *next = nullptr;

  {
    auto i = m_hostIndex.citer();

    ZdbDEBUG(this, ZeString{} << "setNext()\n"
      << " self=" << ZuPrintPtr{m_self} << '\n'
      << " leader=" << ZuPrintPtr{m_leader} << '\n'
      << " prev=" << ZuPrintPtr{m_prev} << '\n'
      << " next=" << ZuPrintPtr{m_next} << '\n'
      << " recovering=" << m_recovering
      << " replicating=" << Host::replicating(m_next));

    while (Host *host = i()) {
      if (host != m_self && host != m_prev && host->voted() &&
	  m_self->cmp(host) >= 0 && (!next || host->cmp(next) > 0))
	next = host;

      ZdbDEBUG(this, ZeString{}
	<< " host=" << ZuPrintPtr{host} << '\n'
	<< " next=" << ZuPrintPtr{next});
    }
  }

  setNext(next);
}

void DB::repStart()
{
  ZmAssert(invoked());

  ZiLOG(Info, "Zdb", ([id = ZeString{m_next->id()}](auto &s) {
    s << "host " << id << " is next in line";
  }));

  dbStateRefresh();

  ZdbDEBUG(this, ZeString{} << "repStart()\n"
    << " self=" << ZuPrintPtr{m_self} << '\n'
    << " leader=" << ZuPrintPtr{m_leader} << '\n'
    << " prev=" << ZuPrintPtr{m_prev} << '\n'
    << " next=" << ZuPrintPtr{m_next} << '\n'
    << " recovering=" << m_recovering
    << " replicating=" << Host::replicating(m_next));

  if (m_self->dbState().cmp(m_next->dbState()) < 0 ||
      m_recovering ||			// already recovering
      m_repStore)			// backing data store is replicated
    return;

  // ZiLOG(Info, "Zdb", "repStart() initiating recovery");

  m_recover = m_next->dbState();
  m_recoverEnd = m_self->dbState();
  if (ZmRef<Cxn> cxn = m_next->cxn()) {
    auto i = m_recover.citer();
    while (auto state = i()) {
      auto key = state->p<0>();
      if (auto endState = m_recoverEnd.find(key))
	if (auto table = m_tables.findVal(key.p<0>())) {
	  ++m_recovering;
	  auto shard = key.p<1>();
	  auto un = state->p<1>();
	  auto endUN = endState->p<1>();
	  if (endUN <= un) continue;
	  table->run(shard, [table, cxn, shard, un, endUN]() mutable {
	    table->recSend(ZuMv(cxn), shard, un, endUN);
	  });
	}
    }
  }
}

// send recovery record
void AnyTable::recSend(ZmRef<Cxn> cxn, Shard shard, UN un, UN endUN)
{
  ZmAssert(invoked(shard));

  if (!m_open) return;

  if (!cxn->up()) return;

  if (auto buf = mkBuf(shard, un)) {	// attempts copy from buffer cache
    recSend_(ZuMv(cxn), shard, un, endUN, ZuMv(buf));
    return;
  }

  m_storeTbl->recover(shard, un, [
    this, cxn = ZuMv(cxn), shard, un, endUN
  ](RowResult result) mutable {
    if (ZuLikely(result.is<RowData>())) {
      ZmRef<IOBuf> buf = result.p<RowData>().buf;
      run(shard, [
	this, cxn = ZuMv(cxn), shard, un, endUN, buf = ZuMv(buf)
      ]() mutable {
	recSend_(ZuMv(cxn), shard, un, endUN, ZuMv(buf));
      });
      return;
    }
    if (ZuUnlikely(result.is<Event>())) {
      ZiLogEvent(ZuMv(result).p<Event>());
      ZiLOG(Error, "Zdb", ([id = this->id(), shard, un](auto &s) {
	s << "recovery of " << id << '/' << shard << '/' << un << " failed";
      }));
    }
    // missing is not an error, skip over updated/deleted records
    run(shard, [this, cxn = ZuMv(cxn), shard, un, endUN]() mutable {
      recNext(ZuMv(cxn), shard, un, endUN);
    });
  });
}

void AnyTable::recSend_(
  ZmRef<Cxn> cxn, Shard shard, UN un, UN endUN, ZmRef<IOBuf> buf)
{
  cxn->send(ZuMv(buf));
  recNext(ZuMv(cxn), shard, un, endUN);
}

void AnyTable::recNext(ZmRef<Cxn> cxn, Shard shard, UN un, UN endUN)
{
  if (++un < endUN)
    run(shard, [this, cxn = ZuMv(cxn), shard, un, endUN]() mutable {
      recSend(ZuMv(cxn), shard, un, endUN);
    });
  else
    m_db->invoke([db = m_db]() { db->recEnd(); });
}

void DB::recEnd()
{
  if (m_recovering) --m_recovering;
}

// build replication buffer
// - first looks in buffer cache for a buffer to copy
// - falls back to row cache
ZmRef<IOBuf> AnyTable::mkBuf(Shard shard, UN un)
{
  ZmAssert(invoked(shard));

  // build from outbound replication buffer cache
  if (auto buf = findBufUN(shard, un)) {
    auto record = record_(msg_(buf->hdr()));
    auto repData = Zfb::Load::bytes(record->data());
    Zfb::IOBuilder fbb{allocBuf()};
    Zfb::Offset<Zfb::Vector<uint8_t>> data;
    if (repData) {
      uint8_t *ptr;
      data = Zfb::Save::pvector_(fbb, repData.length(), ptr);
      if (!data.IsNull() && ptr)
	memcpy(ptr, repData.data(), repData.length());
    }
    ZmAssert(record->shard() == shard);
    auto msg = fbs::CreateMsg(fbb, fbs::Body::Recovery,
	fbs::CreateRecord(
	  fbb, Zfb::Save::str(fbb, Zfb::Load::str(record->table())),
	  record->un(), record->sn(), record->vn(), shard, data).Union());
    fbb.Finish(msg);
    return saveHdr(fbb, this);
  }
  // build from row cache (without falling through to reading from disk)
  if (auto row = findUN(shard, un))
    return row->replicate(int(fbs::Body::Recovery));
  return nullptr;
}

// send commit to replica
void AnyTable::commitSend(Shard shard, UN un)
{
  Zfb::IOBuilder fbb{allocBuf()};
  {
    auto id = Zfb::Save::str(fbb, config().id);
    auto msg = fbs::CreateMsg(
      fbb, fbs::Body::Commit, fbs::CreateCommit(fbb, id, un, shard).Union());
    fbb.Finish(msg);
  }
  m_db->replicate(saveHdr(fbb, this));
}

// prepare replication data
ZmRef<IOBuf> AnyRow::replicate(int type)
{
  ZmAssert(state() == RowState::Committed || state() == RowState::Deleted);

  ZdbDEBUG(m_table->db(), ZeString{}
    << "AnyRow::replicate(" << type << ')');

  Zfb::IOBuilder fbb{m_table->allocBuf()};
  auto data = Zfb::Save::nest(fbb, [this](Zfb::Builder &fbb) {
    if (!m_vn) return m_table->rowSave(fbb, ptr_());
    if (m_vn > 0) return m_table->rowSaveUpd(fbb, ptr_());
    return m_table->rowSaveDel(fbb, ptr_());
  });
  {
    auto id = Zfb::Save::str(fbb, m_table->config().id);
    auto sn = ZfbTransform::UInt128::save(m_sn);
    auto msg = fbs::CreateMsg(fbb, static_cast<fbs::Body>(type),
      fbs::CreateRecord(fbb, id, m_un, &sn, m_vn, m_shard, data).Union());
    fbb.Finish(msg);
  }
  return saveHdr(fbb, m_table);
}

void DB::repStop()
{
  ZmAssert(invoked());

  m_leader = nullptr;
  m_prev = nullptr;
  m_next = nullptr;
  m_recovering = false;
  {
    auto i = m_hostIndex.citer();
    while (Host *host = i()) host->voted(false);
  }
  m_self->voted(true);
  m_nPeers = 1;
}

void Cxn_::msgRead(ZiIOContext &io)
{
  recv<
    [](const ZiIOContext &, ZiIOBuf *buf) -> int {
      return loadHdr(buf);
    },
    [](Cxn_ *cxn, const ZiIOContext &, ZmRef<ZiIOBuf> buf) -> int {
      return cxn->msgRead2(ZuMv(buf));
    }>(io);
}
int Cxn_::msgRead2(ZmRef<IOBuf> buf)
{
  return verifyHdr(ZuMv(buf), [this](const Hdr *hdr, ZmRef<IOBuf> buf) -> int {
    auto msg = Zdb_::msg(hdr);
    ZmAssert(msg);
    if (ZuUnlikely(!msg)) return -1;

    auto length = uint32_t(hdr->length);

    switch (msg->body_type()) {
      case fbs::Body::Heartbeat:
      case fbs::Body::Replication:
      case fbs::Body::Recovery:
      case fbs::Body::Commit:
	if (ZuLikely(buf->length))
	  m_db->run([cxn = ZmRef(this), buf = ZuMv(buf)]() mutable {
	    cxn->msgRead3(ZuMv(buf));
	  });
	break;
      default:
	break;
    }

    m_db->mx()->add(&m_hbTimer,
	Zm::now(int(m_db->config().heartbeatTimeout)),
	ZmScheduler::Defer,
	[this](auto &&arm) { return arm([this]() { hbTimeout(); }); },
	m_db->sid());

    return length;
  });
}
void Cxn_::msgRead3(ZmRef<IOBuf> buf)
{
  ZmAssert(m_db->invoked());

  if (!up()) return;

  auto msg = Zdb_::msg(buf->hdr());
  if (!msg) return;
  switch (msg->body_type()) {
    case fbs::Body::Heartbeat:
      hbRcvd(hb(msg));
      break;
    case fbs::Body::Replication:
    case fbs::Body::Recovery:
      repRecordRcvd(ZuMv(buf));
      break;
    case fbs::Body::Commit:
      repCommitRcvd(ZuMv(buf));
      break;
    default:
      break;
  }
}

void Cxn_::hbRcvd(const fbs::Heartbeat *hb)
{
  if (ZuUnlikely(hb->nShards() != m_db->nShards())) {
    ZiLOG(Error, "Zdb", ([
      host = ZeString{Zfb::Load::str(hb->host())},
      peerNShards = hb->nShards(), nShards = m_db->nShards()
    ](auto &s) {
      s << "peer " << host << " has " << peerNShards
	<< " shards; local DB has " << nShards;
    }));
    disconnect();
    return;
  }
  if (auto dbState = hb->dbState())
    if (auto tableStates = dbState->tableStates())
      for (auto tableState: *tableStates)
	if (ZuUnlikely(tableState->shard() >= m_db->nShards())) {
	  disconnect();
	  return;
	}

  if (!m_host)
    m_db->associate(
      static_cast<Cxn *>(this), Zfb::Load::str(hb->host()));

  if (!m_host) { disconnect(); return; }

  m_db->hbRcvd(m_host, hb);
}

// process received heartbeat
void DB::hbRcvd(Host *host, const fbs::Heartbeat *hb)
{
  ZdbDEBUG(this, ZeString{} << "hbDataRcvd()\n"
    << " host=" << ZuPrintPtr{host} << '\n'
    << " self=" << ZuPrintPtr{m_self} << '\n'
    << " leader=" << ZuPrintPtr{m_leader} << '\n'
    << " prev=" << ZuPrintPtr{m_prev} << '\n'
    << " next=" << ZuPrintPtr{m_next} << '\n'
    << " recovering=" << m_recovering
    << " replicating=" << Host::replicating(m_next));

  host->state(hb->state());
  host->dbState().load(hb->dbState());

  using namespace HostState;

  int state = this->state();

  switch (state) {
    case Electing:
      if (!host->voted()) {
	host->voted(true);
	if (--m_nPeers <= 0) holdElection();
      }
      return;
    case Active:
    case Inactive:
      break;
    default:
      return;
  }

  // check for duplicate leader (dual active)
  switch (state) {
    case Active:
      switch (host->state()) {
	case Active:
	  vote(host);
	  if (host->cmp(m_self) > 0)
	    deactivate(false);
	  else
	    reactivate(host);
	  return;
      }
  }

  // check for new host joining after election
  if (!host->voted()) {
    ++m_nPeers;
    vote(host);
  }
}

// check if new host should be our next in line
void DB::vote(Host *host)
{
  host->voted(true);
  dbStateRefresh();
  if (host != m_next && host != m_prev &&
      m_self->cmp(host) >= 0 && (!m_next || host->cmp(m_next) > 0))
    setNext(host);
}

// send replication message to next-in-line
bool DB::replicate(ZmRef<IOBuf> buf)
{
  if (m_next)
    if (ZmRef<Cxn> cxn = m_next->cxn()) {
      cxn->send(ZuMv(buf));
      return true;
    }
  return false;
}

// broadcast heartbeat
void DB::hbSend()
{
  ZmAssert(invoked());

  hbSend_();

  mx()->add(&m_hbSendTimer, m_hbSendTime += (time_t)m_cf.heartbeatFreq,
    ZmScheduler::Defer,
    [this](auto &&arm) { return arm([this]() { hbSend(); }); },
    sid());
}

// send heartbeat (broadcast)
void DB::hbSend_()
{
  ZmAssert(invoked());

  dbStateRefresh();
  auto i = m_cxns.citer();
  while (auto cxn = i()) cxn->hbSend();
}

// send heartbeat (directed)
void DB::hbSend_(Cxn *cxn)
{
  ZmAssert(invoked());

  dbStateRefresh();
  cxn->hbSend();
}

// send heartbeat on a specific connection
void Cxn_::hbSend()
{
  ZmAssert(m_db->invoked());

  Host *self = m_db->self();
  Zfb::IOBuilder fbb{new ZiTxBufAlloc<HBBufSize, MaxBufSize, "Zdb.TxBuf">{}};
  {
    const auto &dbState = self->dbState();
    auto id = Zfb::Save::str(fbb, self->id());
    auto msg = fbs::CreateMsg(fbb, fbs::Body::Heartbeat,
	fbs::CreateHeartbeat(fbb, id,
	  m_db->state(), m_db->nShards(), dbState.save(fbb)).Union());
    fbb.Finish(msg);
  }

  send(saveHdr(fbb, this));

  ZdbDEBUG(m_db, ZeString{}
    << "hbSend() self{id=" << self->id()
    << ", state=" << m_db->state()
    << ", dbState=" << self->dbState() << '}');
}

// refresh table state vector
void DB::dbStateRefresh()
{
  ZmAssert(invoked());

  DBState &dbState = m_self->dbState();
  dbState.updateSN(m_nextSN);
  all_([this, &dbState](AnyTable *table) {
    for (Shard i = 0, n = nShards(); i < n; i++)
      dbState.update(table->config().id, i, table->nextUN(i));
  });
}

// process received replicated record
void Cxn_::repRecordRcvd(ZmRef<IOBuf> buf)
{
  ZmAssert(m_db->invoked());

  if (!m_host) return;
  if (m_db->repStore()) return; // backing data store is replicated
  auto record = Zdb_::record(msg_(buf->hdr())); // caller verified msg
  if (!record) return;
  ZuCSpan id = Zfb::Load::str(record->table());
  AnyTable *table = m_db->table(id);
  if (ZuUnlikely(!table)) return;

  ZdbDEBUG(m_db, (ZeString{}
    << "repRecordRcvd(host=" << m_host->id() << ", "
    << Record_Print{record, table}));

  auto shard = record->shard();
  if (ZuUnlikely(shard >= m_db->nShards())) return;

  m_db->replicated(
    m_host, id, shard, record->un(),
    ZfbTransform::UInt128::load(record->sn()));
  table->invoke(shard, [table, shard, buf = ZuMv(buf)]() mutable {
    table->repRecordRcvd(shard, ZuMv(buf));
  });
}

// process received replication commit
void Cxn_::repCommitRcvd(ZmRef<IOBuf> buf)
{
  ZmAssert(m_db->invoked());

  if (!m_host) return;
  auto commit = Zdb_::commit(msg_(buf->hdr())); // caller verified msg
  auto id = Zfb::Load::str(commit->table());
  AnyTable *table = m_db->table(id);
  if (ZuUnlikely(!table)) return;

  ZdbDEBUG(m_db, ZeString{}
    << "repCommitRcvd(host=" << m_host->id() << ", " << commit->un() << ')');

  auto shard = commit->shard();
  if (ZuUnlikely(shard >= m_db->nShards())) return;
  table->invoke(shard, [table, shard, un = commit->un()]() mutable {
    table->repCommitRcvd(shard, un);
  });
}

void DB::replicated(Host *host, ZuCSpan tblID, Shard shard, UN un, SN sn)
{
  ZmAssert(invoked());

  bool updated = host->dbState().updateSN(sn + 1);
  updated = host->dbState().update(tblID, shard, un + 1) || updated;
  if ((active() || host == m_next) && !updated) return;
  if (!m_prev) {
    m_prev = host;
    ZiLOG(Info, "Zdb", ([id = ZeString{m_prev->id()}](auto &s) {
      s << "host " << id << " is previous in line";
    }));
  }
}

AnyTable::AnyTable(
  DB *db, TableCf *cf, bool internal, IOBufAllocFn fn) :
  m_db{db}, m_cf{cf}, m_mx{db->mx()}, m_internal{internal},
  m_bufAllocFn{ZuMv(fn)}
{
  unsigned n = db->nShards();
  m_nextUN.length(n);
  m_cacheUN.length(n);
  m_bufCacheUN.length(n);
  ZuID cacheID = "Zdb.CacheUN."; cacheID << cf->id;
  ZuID bufCacheID = "Zdb.BufCacheUN."; bufCacheID << cf->id;
  for (unsigned i = 0; i < n; i++) {
    m_nextUN[i] = 0;
    m_cacheUN[i] = new CacheUN{cacheID};
    m_bufCacheUN[i] = new BufCacheUN{bufCacheID};
  }
}

AnyTable::~AnyTable() noexcept
{
  // close(); // must be called while running
}

Ztc::DBTableKey AnyTable::telKey() const
{
  return {m_db->telKey(), m_cf->id};
}

void AnyTable::telemetry(Ztc::DBTableTelemetry &data) const
{
  data.dbID = m_db->telKey();
  data.id = m_cf->id;
  data.count = count();
  data.cacheLoads = 0;
  data.cacheMisses = 0;
  data.cacheEvictions = 0;
  data.cacheSize = 0;
  for (unsigned i = 0, n = m_db->nShards(); i < n; ++i) {
    ZmCacheStats stats;
    cacheStats(i, stats);
    data.cacheSize += stats.size;
    data.cacheLoads += stats.loads;
    data.cacheMisses += stats.misses;
    data.cacheEvictions += stats.evictions;
  }
  data.cacheMode = m_cf->cacheMode;
}

// process inbound replication - record
void AnyTable::repRecordRcvd(Shard shard, ZmRef<IOBuf> buf)
{
  ZmAssert(invoked(shard));

  if (!m_open) return;

  recover(shard, record_(msg_(buf->hdr())));
  write(shard, buf, false);
}

// process inbound replication - committed
void AnyTable::repCommitRcvd(Shard shard, UN un)
{
  ZmAssert(invoked(shard));

  if (!m_open) return;

  commitSend(shard, un);
  evictBuf(shard, un);
}

// recover record
void AnyTable::recover(Shard shard, const fbs::Record *record)
{
  m_db->recoveredSN(ZfbTransform::UInt128::load(record->sn()));
  recoveredUN(shard, record->un());
  rowRecover(record);
}

// outbound replication + persistency
void AnyTable::write(Shard shard, ZmRef<IOBuf> buf, bool active)
{
  ZmAssert(invoked(shard));

  cacheBuf(shard, buf);
  auto db = this->db();
  if (ZuLikely(active) || !db->repStore()) {
    // leader, or follower without replicated data store - will
    // evict buf when write to data store is committed
    db->invoke([db, buf]() mutable { db->replicate(buf); });
    store(shard, ZuMv(buf));
  } else {
    // follower with replicated data store - will evict buf
    // when leader subsequently sends commit, unless message is recovery
    auto msg = msg_(buf->hdr());
    auto un = record_(msg)->un();
    bool recovery = msg->body_type() == fbs::Body::Recovery;
    db->invoke([db, buf = ZuMv(buf)]() mutable { db->replicate(buf); });
    if (recovery)
      invoke(shard, [this, shard, un]() { evictBuf(shard, un); });
  }
}

// low-level internal write to backing data store
void AnyTable::store(Shard shard, ZmRef<IOBuf> buf)
{
  ZmAssert(invoked(shard));

  if (ZuUnlikely(!m_open)) return; // table is closing

  store_(shard, ZuMv(buf));
}
void AnyTable::store_(Shard shard, ZmRef<IOBuf> buf)
{
  m_storeTbl->write(ZuMv(buf), CommitFn{this,
    [](AnyTable *this_, ZmRef<IOBuf> buf, CommitResult result) {
      this_->committed(ZuMv(buf), ZuMv(result));
    }});
}
void AnyTable::committed(ZmRef<IOBuf> buf, CommitResult result)
{
  auto msg = msg_(buf->hdr());
  auto record = record_(msg);
  auto shard = record->shard();
  auto un = record->un();
  if (ZuUnlikely(result.is<Event>())) {
    ZiLogEvent(ZuMv(result).p<Event>());
    ZiLOG(Fatal, "Zdb", ([id = this->id(), shard, un](auto &s) {
      s << "store of " << id << '/' << shard << '/' << un << " failed";
    }));
    auto db = this->db();
    db->run([db]() { db->fail(); }); // trigger failover
    return;
  }
  bool recovery = msg->body_type() == fbs::Body::Recovery;
  run(shard, [this, shard, un, recovery]() {
    evictBuf(shard, un);
    if (!recovery) commitSend(shard, un);
  });
}

// cache buffer
void AnyTable::cacheBuf(Shard shard, ZmRef<IOBuf> buf)
{
  cacheBufUN(shard, buf.mutablePtr());
  cacheBuf_(shard, ZuMv(buf));
}

// evict buffer
void AnyTable::evictBuf(Shard shard, UN un)
{
  if (auto buf = evictBufUN(shard, un))
    evictBuf_(shard, static_cast<IOBuf *>(buf));
}

// DB::open() iterates over tables, calling open()
// - each store table open calls table->opened(OpenResult) on success
template <typename L>
void AnyTable::open(L &&l)
{
  /* ZiLOG(Debug, "Zdb",
    ([hostID = db()->config().hostID, open = unsigned(m_open)](auto &s) {
      s << hostID << " m_open=" << open;
    })); */

  ZmAssert(invoked(0));
  ZmAssert(!m_open);

  if (m_open) {
    l(true);
    return;
  }

  db()->store()->open(
    m_internal,
    id(),
    rowFields(), rowKeyFields(), rowSchema(), m_bufAllocFn,
    [this, l = ZuFwd<L>(l)](OpenResult result) mutable {
      invoke(0, [this, l = ZuMv(l), result = ZuMv(result)]() mutable {
	l(opened(ZuMv(result)));
      });
    });
}

bool AnyTable::opened(OpenResult result)
{
  ZdbDEBUG(m_db, ([
    hostID = m_db->config().hostID, open = unsigned(m_open)
  ](auto &s) {
    s << hostID << " m_open=" << open;
  }));

  ZmAssert(invoked(0));
  ZmAssert(!m_open);

  if (m_open) return true;

  if (!result.is<OpenData>()) {
    if (result.is<Event>())
      ZiLogEvent(ZuMv(result).p<Event>());
    return false;
  }

  const auto &data = result.p<OpenData>();
  m_storeTbl = data.storeTbl;
  m_count = data.count;
  m_db->recoveredSN(data.sn);
  for (unsigned i = 0, n = m_db->nShards(); i < n; i++)
    recoveredUN(i, data.un[i]);

  m_open = 1;
  return true;
}

template <typename L>
void AnyTable::close(L &&l)
{
  /* ZiLOG(Debug, "Zdb",
    ([hostID = db()->config().hostID, open = unsigned(m_open)](auto &s) {
      s << hostID << " m_open=" << open;
    })); */

  ZmAssert(invoked(0));

  // ensure idempotence

  if (!m_open) {
    l();
    return;
  }

  if (!m_storeTbl) {
    l();
    m_open = 0;
    return;
  }

  m_storeTbl->close([this, l = ZuFwd<L>(l)]() mutable {
    invoke(0, [this, l = ZuMv(l)]() mutable {
      m_storeTbl = nullptr;
      l();
      m_open = 0;
    });
  });
}

bool AnyRow::insert_(UN un)
{
  if (m_state != RowState::Undefined) return false;
  m_state = RowState::Insert;
  m_un = un;
  return true;
}
bool AnyRow::update_(UN un)
{
  if (m_state != RowState::Committed) return false;
  m_state = RowState::Update;
  m_origUN = m_un;
  m_un = un;
  return true;
}
bool AnyRow::del_(UN un)
{
  if (m_state != RowState::Committed) return false;
  m_state = RowState::Delete;
  m_origUN = m_un;
  m_un = un;
  return true;
}

bool AnyRow::commit_()
{
  switch (m_state) {
    default: return false;
    case RowState::Insert:
    case RowState::Update:
    case RowState::Delete: break;
  }
  if (ZuUnlikely(!m_table->allocUN(m_shard, m_un))) {
    abort_();
    return false;
  }
  m_sn = m_table->db()->allocSN();
  switch (m_state) {
    case RowState::Insert:
      m_state = RowState::Committed;
      break;
    case RowState::Update:
      m_state = RowState::Committed;
      m_origUN = nullUN();
      ++m_vn;
      break;
    case RowState::Delete:
      m_state = RowState::Deleted;
      m_origUN = nullUN();
      m_vn = -m_vn - 1;
      break;
  }
  return true;
}

bool AnyRow::abort_()
{
  switch (m_state) {
    default: return false;
    case RowState::Insert:
      m_state = RowState::Undefined;
      m_un = nullUN();
      break;
    case RowState::Update:
    case RowState::Delete:
      m_state = RowState::Committed;
      m_un = m_origUN;
      m_origUN = nullUN();
      break;
  }
  return true;
}

} // namespace Zdb_
