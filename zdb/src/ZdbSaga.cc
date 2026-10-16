//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z Database sagas

#include <zlib/Zdb.hh>

namespace Zdb_ {

ZtEnumImplNS(SagaOp);
ZfStructImpl(SagaData);
ZfStructImpl(SagaData, FB);
ZfStructImpl(SagaStep);
ZfStructImpl(SagaStep, FB);
ZfStructImpl(SagaTypeStep);
ZfStructImpl(SagaTypeStep, FB);

void SagaCatalog__::end(unsigned expected)
{
  if (error || typeIndex < 0) return;
  if (step != expected)
    error = ZeEXCEPT(Fatal, "Zdb", ([
      type = ZeString{type}, step = step, expected
    ](auto &s) {
      s << "saga type \"" << type << "\" has " << step
	<< " stored steps, expected " << expected;
    }));
}

void SagaCatalog__::load(const SagaDefs &sagaDefs,
    const SagaDefHash_ &sagaDefHash, SagaTypeStep row)
{
  if (error) return;
  if (row.type != type) {
    end(typeIndex >= 0 ? sagaDefs[typeIndex].nPhases : 0);
    if (error) return;
    type = ZuMv(row.type);
    step = 0;
    auto entry = sagaDefHash.find(ZuCSpan{type});
    typeIndex = entry ? int(entry->template p<1>()) : -1;
    if (typeIndex >= 0) seen[typeIndex] = 1;
  }
  if (typeIndex >= 0) {
    SagaTypeStep expected;
    if (row.step != step ||
	!sagaDefs[typeIndex].phaseDef(row.step, expected) ||
	row.table != expected.table || row.op != expected.op ||
	row.repeat != expected.repeat) {
      error = ZeEXCEPT(Fatal, "Zdb", ([
	type = ZeString{type}, step = row.step
      ](auto &s) {
	s << "saga type \"" << type << "\" step " << step
	  << " conflicts with the stored definition";
      }));
      return;
    }
  }
  step = uint64_t(row.step) + 1;
}

bool AnySaga__::prepare_(
    AnyTable *table, Shard shard, SagaOp::T op,
    bool effect, UN &un, bool &saved)
{
  return db()->sagaPrepare(this, table, shard, op, effect, un, saved);
}

void AnySaga__::intent_(Shard shard, UN un)
{
  auto db = this->db();
  SagaStep step{
    .type = this->type(), .id = this->id(), .step = this->step(),
    .shard = shard, .un = un
  };
  ZmRef<Row<SagaStep>> intent =
    new Row<SagaStep>{db->sagaStepTable(), shard};
  bool committed = false;
  db->sagaStepTable()->insert(intent,
    [&step, &committed](Row<SagaStep> *row) {
      if (ZuUnlikely(!row)) return;
      new (row->ptr()) SagaStep{ZuMv(step)};
      committed = bool(row->commit());
    });
  ZiAssert(committed, "Zdb", (), "saga intent did not commit", ::abort());
  stepShard(m_step, shard);
}

void AnySaga__::replay_(OpResult::T result, Shard shard)
{
  if (result == OpResult::Skipped) {
    stepShard(m_step, shard);
    stepRecovered_(shard);
  } else
    result_(result, shard);
}

void AnySaga__::stepRecovered_(Shard shard)
{
  stepShard(m_step, shard);
  ZmRef<AnySaga_> saga = static_cast<AnySaga_ *>(this);
  auto db = this->db();
  auto epoch = this->epoch();
  auto step = this->step();
  db->invoke([db, saga = ZuMv(saga), epoch, step, shard]() mutable {
    db->sagaStepRecovered(ZuMv(saga), epoch, step, shard);
  });
}

void AnySaga__::result_(OpResult::T result, Shard shard)
{
  ZmRef<AnySaga_> saga = static_cast<AnySaga_ *>(this);
  auto db = this->db();
  auto epoch = this->epoch();
  db->invoke([db, saga = ZuMv(saga), epoch, result, shard]() mutable {
    db->sagaResult(ZuMv(saga), epoch, result, shard);
  });
}

void SagaDriver::run_(
    DB *db, ZmRef<AnySaga_> saga, SagaCompleteFn complete)
{
  if (ZuUnlikely(!saga || saga->epoch() != db->sagaEpoch() ||
      db->sagaState() == SagaState::Inactive ||
      (db->sagaState() == SagaState::Active && !db->appActive()))) return;
  saga->rec(db->sagaState() == SagaState::Rebuilding ?
    db->sagaRec(saga, saga->step()) : nullptr);
  db->sagaPending();
  auto ptr = saga.ptr();
  auto step = ptr->step();
  SagaDriver driver{ZuMv(saga), ptr->epoch(), step, ptr->fwd(), ZuMv(complete)};
  if (ZuUnlikely(!ptr->fwd() && ptr->rec() &&
      (ptr->rec()->un == nullUN() ||
       ZuCmp<UN>::cmp(ptr->rec()->table->nextUN(ptr->rec()->shard),
	 ptr->rec()->un) <= 0))) {
    driver(true);
    return;
  }
  if (ZuLikely(step < ptr->shards().length())) {
    if (!ptr->fwd() && ptr->stepShard(step) == Shard(-1)) {
      driver(true);
      return;
    }
    ptr->run(ZuMv(driver));
    return;
  }
  if (ZuLikely(step == ptr->shards().length())) {
    driver.finish(ptr->fwd());
    return;
  }
  ptr->result_(OpResult::Invalid, ptr->shard());
}

void SagaDriver::operator ()(bool ok)
{
  auto saga_ = ZuMv(saga);
  if (ZuUnlikely(!saga_)) return;
  auto db = saga_->db();
  db->invoke([
    db, saga = ZuMv(saga_), complete = ZuMv(complete),
    epoch = epoch, step = step, fwd = fwd, ok
  ]() mutable {
    db->sagaRetire();
    if (ZuUnlikely(!saga || epoch != db->sagaEpoch() ||
	saga->epoch() != epoch)) return;
    if (ZuUnlikely(saga->step() != step || saga->fwd() != fwd ||
	step >= saga->shards().length())) {
      db->sagaFail(ZuMv(saga), epoch,
	ZeEXCEPT(Fatal, "Zdb", "invalid saga step completion"));
      return;
    }
    if (!fwd && ZuUnlikely(!ok)) {
      abandon_(db, ZuMv(saga));
      return;
    }
    if (fwd && ok) {
      if (auto rec = db->sagaRec(saga, step)) db->sagaDelRec(rec);
      db->sagaBlocked(nullptr);
      saga->stepInc();
      if (saga->step() < saga->shards().length()) {
	run_(db, ZuMv(saga), ZuMv(complete));
	return;
      }
      finish_(db, ZuMv(saga), ZuMv(complete), true);
      return;
    }
    if (fwd) saga->fwd(false);
    del_(db, ZuMv(saga), ZuMv(complete), step, false);
  });
}

void SagaDriver::finish(bool success)
{
  auto saga_ = ZuMv(saga);
  if (ZuUnlikely(!saga_)) return;
  auto db = saga_->db();
  db->invoke([
    db, saga = ZuMv(saga_), complete = ZuMv(complete),
    epoch = epoch, success
  ]() mutable {
    db->sagaRetire();
    if (ZuUnlikely(!saga || epoch != db->sagaEpoch() ||
	saga->epoch() != epoch)) return;
    finish_(db, ZuMv(saga), ZuMv(complete), success);
  });
}

void SagaDriver::del_(
    DB *db, ZmRef<AnySaga_> saga, SagaCompleteFn complete, unsigned step, bool success)
{
  auto shard = saga->stepShard(step);
  if (shard >= db->nShards()) {
    if (success) {
      cleanup_(db, ZuMv(saga), ZuMv(complete), step + 1);
      return;
    }
    if (!step) {
      finish_(db, ZuMv(saga), ZuMv(complete), false);
      return;
    }
    saga->stepDec();
    run_(db, ZuMv(saga), ZuMv(complete));
    return;
  }
  auto epoch = saga->epoch();
  db->sagaPending();
  db->sagaStepTable()->run(shard, [
    db, saga = ZuMv(saga), complete = ZuMv(complete),
    epoch, step, shard, success
  ]() mutable {
    const auto &key = saga->key();
    db->sagaStepTable()->findDel<0>(shard,
	ZuFwdTuple(key.template p<0>(), key.template p<1>(), uint32_t(step)),
	[db, saga = ZuMv(saga), complete = ZuMv(complete),
	 epoch, step, success](Row<SagaStep> *row) mutable {
	  bool ok = !row;
	  if (row) ok = row->commit();
	  db->run([db, saga = ZuMv(saga), complete = ZuMv(complete),
	    epoch, step, success, ok]() mutable {
	    db->sagaRetire();
	    if (ZuUnlikely(!saga || epoch != db->sagaEpoch() ||
		saga->epoch() != epoch)) return;
	    if (ZuUnlikely(!ok)) {
	      db->sagaFail(ZuMv(saga), epoch,
		ZeEXCEPT(Fatal, "Zdb", "failed to delete saga step intent"));
	      return;
	    }
	    if (auto rec = db->sagaRec(saga, step)) db->sagaDelRec(rec);
	    if (success) {
	      cleanup_(db, ZuMv(saga), ZuMv(complete), step + 1);
	      return;
	    }
	    if (!step) {
	      finish_(db, ZuMv(saga), ZuMv(complete), false);
	      return;
	    }
	    saga->stepDec();
	    run_(db, ZuMv(saga), ZuMv(complete));
	  });
	});
  });
}

void SagaDriver::finish_(
    DB *db, ZmRef<AnySaga_> saga, SagaCompleteFn complete, bool success)
{
  auto epoch = saga->epoch();
  auto shard = saga->shard();
  db->sagaPending();
  db->sagaTable()->run(shard, [
    db, saga = ZuMv(saga), complete = ZuMv(complete), epoch, shard, success
  ]() mutable {
    const auto &key = saga->key();
    db->sagaTable()->findDel<0>(shard,
	ZuFwdTuple(key.template p<0>(), key.template p<1>()),
	[db, saga = ZuMv(saga), complete = ZuMv(complete),
	 epoch, success](Row<SagaData> *row) mutable {
	  bool ok = !row;
	  if (row) ok = row->commit();
	  db->run([db, saga = ZuMv(saga), complete = ZuMv(complete),
	    epoch, success, ok]() mutable {
	    db->sagaRetire();
	    if (ZuUnlikely(!saga || epoch != db->sagaEpoch() ||
		saga->epoch() != epoch)) return;
	    if (ZuUnlikely(!ok)) {
	      db->sagaFail(ZuMv(saga), epoch,
		ZeEXCEPT(Fatal, "Zdb", "failed to delete saga intent"));
	      return;
	    }
	    if (success)
	      cleanup_(db, ZuMv(saga), ZuMv(complete), 0);
	    else
	      terminal_(db, ZuMv(saga), ZuMv(complete), false);
	  });
	});
  });
}

void SagaDriver::cleanup_(
    DB *db, ZmRef<AnySaga_> saga, SagaCompleteFn complete, unsigned step)
{
  if (step < saga->shards().length()) {
    del_(db, ZuMv(saga), ZuMv(complete), step, true);
    return;
  }
  terminal_(db, ZuMv(saga), ZuMv(complete), true);
}

void SagaDriver::abandon_(DB *db, ZmRef<AnySaga_> saga)
{
  ZiLOG(Error, "Zdb", ([
    type = ZeString{saga->type()}, id = saga->id(), step = saga->step()
  ](auto &s) {
    s << "saga rollback abandoned: " << type << '/' << id << '/' << step;
  }));
  if (db->sagaState() == SagaState::Rebuilding) {
    for (unsigned step = 0; step < saga->shards().length(); ++step) {
      auto rec = db->sagaRec(saga, step);
      if (!rec) continue;
      db->sagaDelRec(rec);
    }
  }
  end_(db, saga);
}

void SagaDriver::end_(DB *db, AnySaga_ *saga)
{
  db->sagaEnd(saga);
}

void SagaDriver::terminal_(
    DB *db, ZmRef<AnySaga_> saga, SagaCompleteFn complete, bool success)
{
  end_(db, saga);
  auto callback = ZuMv(complete);
  callback(success);
}
void SagaDB::sagas(SagaDefs sagaDefs)
{
  if (!ZmEngine<DB>::lock(ZmEngineState::Stopped,
      [this, sagaDefs = ZuMv(sagaDefs)]() mutable {
    if (state() != HostState::Initialized || m_sagaCatalogFn) return false;
    if (!sagaDefs.length()) return false;
    for (unsigned i = 0, n = sagaDefs.length(); i < n; ++i) {
      if (!sagaDefs[i].type || !sagaDefs[i].nPhases ||
          !sagaDefs[i].phaseDef || !sagaDefs[i].load) return false;
    }
    m_sagaDefs = ZuMv(sagaDefs);
    ZmRef<SagaDefHash> sagaDefHash =
      new SagaDefHash{unsigned(m_sagaDefs.length())};
    for (unsigned i = 0, n = m_sagaDefs.length(); i < n; ++i) {
      ZuCSpan type = m_sagaDefs[i].type;
      if (sagaDefHash->find(type) || !sagaDefHash->add(type, i)) {
        sagaDefHash = nullptr;
        m_sagaDefs.null();
        return false;
      }
    }
    m_sagaDefHash = ZuMv(sagaDefHash);
    m_sagaCatalogFn = [this]() { sagaCatalog(); };
    m_sagaScanFn = [this]() { sagaScanData(m_sagaScan, false); };
    m_sagaRunFn = [this](ZmRef<AnySaga_> saga) {
      if (ZuUnlikely(!saga)) return;
      SagaDriver::run_(this, ZuMv(saga), {});
    };
    m_sagaTable = new TableT<SagaData>{this, &m_sagaCf, true};
    m_sagaStepTable = new TableT<SagaStep>{this, &m_sagaStepCf, true};
    m_tables.add(m_sagaTable);
    m_tables.add(m_sagaStepTable);
    tableAdded_(m_sagaTable);
    tableAdded_(m_sagaStepTable);
    return true;
  }))
    throw ZeEXCEPT(Fatal, "Zdb", "invalid or out of order saga registration");
}

void SagaDB::final()
{
  DB::final();
  m_sagaDefHash = nullptr;
  m_sagaDefs.null();
}

int SagaDB::match(ZuCSpan type) const
{
  if (!m_sagaDefHash) return -1;
  auto entry = m_sagaDefHash->find(type);
  return entry ? int(entry->template p<1>()) : -1;
}

bool SagaDB::saga_(
    Shard shard, SagaID id, ZmRef<AnySaga_> saga,
    SagaCompleteFn submit, SagaCompleteFn complete, ZuTime deadline)
{
  if (!m_mx || !m_mx->running()) return false;
  return spawn([
    this, shard, id, saga = ZuMv(saga),
    submit = ZuMv(submit),
    complete = ZuMv(complete), deadline
  ]() mutable {
    if (ZuUnlikely(!saga || !m_sagaCatalogFn)) {
      submit(false);
      return;
    }
    if (ZuUnlikely(!Engine::running() || !m_appActive ||
	m_sagaState != SagaState::Active)) {
      submit(false);
      return;
    }
    ZmAssert(!m_sagaHash);
    if (ZuUnlikely(shard >= nShards())) {
      submit(false);
      return;
    }
    ZuCSpan type = saga->type();
    auto typeIndex = match(type);
    if (ZuUnlikely(!type || typeIndex < 0)) {
      submit(false);
      return;
    }
    auto stepCount = saga->nSteps();
    if (ZuUnlikely(!stepCount)) {
      submit(false);
      return;
    }
    ZmRef<Row<SagaData>> row;
    try {
      row = new Row<SagaData>{m_sagaTable, shard};
      new (row->ptr()) SagaData{
	.type = type, .id = id, .shard = shard, .deadline = deadline
      };
      if (auto error = saga->save(row->data().data)) {
	ZiLOG(Error, "Zdb", ([error](auto &s) {
	  s << "saving saga definition failed - codec error " << error;
	}));
	submit(false);
	return;
      }
      saga->init_(this, SagaKey{type, id}, shard, m_sagaEpoch, stepCount,
	deadline);
    } catch (ZeException &e) {
      ZiLogEvent(ZuMv(e));
      submit(false);
      return;
    } catch (...) {
      submit(false);
      return;
    }
    ++m_sagaPending;
    m_sagaTable->run(shard, [
      this, saga = ZuMv(saga), row = ZuMv(row),
      submit = ZuMv(submit), complete = ZuMv(complete)
    ]() mutable {
      bool committed = false;
      m_sagaTable->insert(row,
	[&committed](Row<SagaData> *row_) {
	  if (ZuUnlikely(!row_)) return;
	  committed = row_->commit();
	});
      run([
	this, saga = ZuMv(saga), submit = ZuMv(submit),
	complete = ZuMv(complete), committed
      ]() mutable {
	sagaRetire();
	if (ZuUnlikely(saga->epoch() != m_sagaEpoch)) {
	  // The intent may already be committed. Leave it for activation replay.
	  submit(false);
	  return;
	}
	if (ZuUnlikely(!committed)) {
	  submit(false);
	  return;
	}
	++m_sagaLive;
	submit(true);
	if (ZuUnlikely(saga->epoch() != m_sagaEpoch ||
	    m_sagaState != SagaState::Active || !m_appActive)) return;
	SagaDriver::run_(this, ZuMv(saga), ZuMv(complete));
      });
    });
  });
}

void SagaDB::sagaCatalog()
{
  ZmAssert(invoked());

  ZmRef<SagaCatalog> context = new SagaCatalog{};
  auto n = m_sagaDefs.length();
  context->seen.length(n, false);
  memset(context->seen.data(), 0, n);
  for (unsigned i = 0; i < n; ++i) {
    SagaTypeStep row;
    for (unsigned j = 0; m_sagaDefs[i].phaseDef(j, row); ++j) {
      if (ZuLikely(table(row.table))) continue;
      context->error = ZeEXCEPT(Fatal, "Zdb", ([
	type = ZeString{row.type}, step = row.step,
	table = ZeString{row.table}
      ](auto &s) {
	s << "saga " << type << '/' << step
	  << " references unknown table \"" << table << '"';
      }));
      sagaCatalogClose(ZuMv(context));
      return;
    }
  }
  m_store->open(
    true, "saga_type", ZfVFields<SagaTypeStep>(),
    ZfVKeyFields<SagaTypeStep>(),
    Schema::from<SagaTypeStep>(context->schema),
    TableT<SagaTypeStep>::allocBuf,
    [this, context = ZuMv(context)](OpenResult result) mutable {
      run([this, context = ZuMv(context), result = ZuMv(result)]() mutable {
	sagaCatalogOpen(ZuMv(context), ZuMv(result));
      });
    });
}

void SagaDB::sagaCatalogOpen(
    ZmRef<SagaCatalog> context, OpenResult result)
{
  ZmAssert(invoked());
  if (ZuUnlikely(!result.is<OpenData>())) {
    if (result.is<Event>()) context->error = ZuMv(result).p<Event>();
    else context->error = ZeEXCEPT(Fatal, "Zdb", "saga_type open failed");
    sagaCatalogClose(ZuMv(context));
    return;
  }
  auto data = ZuMv(result).p<OpenData>();
  context->table = data.storeTbl;
  if (ZuUnlikely(!context->table || data.un.length() != nShards())) {
    context->error = ZeEXCEPT(Fatal, "Zdb", "invalid saga_type open result");
    sagaCatalogClose(ZuMv(context));
    return;
  }
  recoveredSN(data.sn);
  UN un = data.un[0];
  context->nextUN = un == nullUN() ? 0 : un + 1;
  sagaCatalogScan(ZuMv(context), false);
}

void SagaDB::sagaCatalogScan(ZmRef<SagaCatalog> context, bool next)
{
  ZmAssert(invoked());
  context->pageCount = 0;

  auto fbb = RowBuilder<SagaTypeStep>();
  if (next) {
    using Key = ZuStructKeyT<SagaTypeStep, 0>;
    Key key{context->type, uint32_t(context->step - 1)};
    fbb.finish(Zfb::save(fbb, key));
  } else {
    using GroupKey = SplitKey<SagaTypeStep, 0>::GroupKey;
    GroupKey key;
    fbb.finish(Zfb::save(fbb, key));
  }
  auto keyBuf = fbb.buf();
  context->table->select(
    true, next, false, 0, ZuMv(keyBuf), SagaScanSize,
    TupleFn{[this, context](TupleResult result) mutable {
      if (ZuUnlikely(result.is<Event>())) {
	if (!context->error) context->error = ZuMv(result).p<Event>();
	this->run([this, context = ZmRef(context)]() mutable {
	  this->sagaCatalogClose(ZuMv(context));
	});
	return;
      }
      if (result.is<TupleData>()) {
	if (context->error) return;
	auto data = ZuMv(result).p<TupleData>();
	context->load(m_sagaDefs, *m_sagaDefHash,
	  Zfb::load<SagaTypeStep>(data.buf->cspan()));
	context->pageCount = data.count;
	return;
      }
      this->run([this, context = ZmRef(context)]() mutable {
	if (context->error) {
	  this->sagaCatalogClose(ZuMv(context));
	  return;
	}
	if (context->pageCount == SagaScanSize) {
	  this->sagaCatalogScan(ZuMv(context), true);
	  return;
	}
	context->end(context->typeIndex >= 0 ?
	  m_sagaDefs[context->typeIndex].nPhases : 0);
	if (context->error)
	  this->sagaCatalogClose(ZuMv(context));
	else
	  this->sagaCatalogWrite(ZuMv(context));
      });
    }});
}

void SagaDB::sagaCatalogWrite(ZmRef<SagaCatalog> context)
{
  ZmAssert(invoked());
  SagaTypeStep row;
  auto n = m_sagaDefs.length();
  while (context->writeType < n) {
    if (!context->seen[context->writeType] &&
	m_sagaDefs[context->writeType].phaseDef(context->writeStep, row))
      break;
    ++context->writeType;
    context->writeStep = 0;
  }
  if (context->writeType == n) {
    sagaCatalogClose(ZuMv(context));
    return;
  }
  ++context->writeStep;
  UN un = context->nextUN++;
  SN sn = allocSN();
  auto fbb = RowBuilder<SagaTypeStep>();
  auto nested = fbb.startNested();
  auto root = Zfb::save(fbb, row);
  auto data = fbb.endNested(nested, root);
  auto record = saveRecord(fbb, "saga_type", un, sn, 0, 0, data);
  fbb.finish(saveMsg(fbb, record, Msg::Tag::Replication));
  context->table->write(saveHdr(fbb), CommitFn{[this, context](ZmRef<IOBuf>, CommitResult result) mutable {
      if (ZuUnlikely(result.is<Event>()))
	context->error = ZuMv(result).p<Event>();
      this->run([this, context = ZmRef(context)]() mutable {
	if (context->error)
	  this->sagaCatalogClose(ZuMv(context));
	else
	  this->sagaCatalogWrite(ZuMv(context));
      });
    }});
}

void SagaDB::sagaCatalogClose(ZmRef<SagaCatalog> context)
{
  ZmAssert(invoked());
  if (!context->table) {
    sagaCatalogFinish(ZuMv(context));
    return;
  }
  context->table->close([this, context = ZuMv(context)]() mutable {
    run([this, context = ZuMv(context)]() mutable {
      context->table = nullptr;
      sagaCatalogFinish(ZuMv(context));
    });
  });
}

void SagaDB::sagaCatalogFinish(ZmRef<SagaCatalog> context)
{
  ZmAssert(invoked());
  if (context->error) {
    ZiLogEvent(ZuMv(context->error));
    ZiLOG(Fatal, "Zdb", "saga_type reconciliation failed");
    startFailed();
    return;
  }
  start_2();
}

void SagaDB::sagaScanData(ZmRef<SagaScan> context, bool next)
{
  ZmAssert(invoked());
  ++m_sagaPending;
  context->pageCount = 0;
  using Tuple = TableT<SagaData>::Tuple;
  auto receive = [this, context](ZuUnion<void, Tuple> result, unsigned count) mutable {
    if (result.template is<Tuple>()) {
      ZuTupleCall(ZuMv(result).template p<Tuple>(), [context](auto &&...args) {
	new (context->dataRows.push()) SagaData{ZuFwd<decltype(args)>(args)...};
      });
      context->pageCount = count;
      return;
    }
    run([this, context = ZuMv(context)]() mutable {
      sagaRetire();
      if (context->epoch != m_sagaEpoch ||
	  m_sagaState != SagaState::Rebuilding) return;
      sagaLoadData(ZuMv(context));
    });
  };
  if (next) {
    m_sagaTable->nextRows<0>(
      context->cursor, false, SagaScanSize, ZuMv(receive));
  } else {
    m_sagaTable->selectRows<0>({}, SagaScanSize, ZuMv(receive));
  }
}

void SagaDB::sagaLoadData(ZmRef<SagaScan> context)
{
  ZmAssert(invoked());
  if (ZuUnlikely(context->epoch != m_sagaEpoch ||
	m_sagaState != SagaState::Rebuilding)) return;
  try {
    for (unsigned i = 0, n = context->dataRows.length(); i < n; ++i) {
      auto &row = context->dataRows[i];
      auto typeIndex = match(row.type);
      if (ZuUnlikely(row.shard >= nShards() ||
	  typeIndex < 0))
	throw ZeEXCEPT(Fatal, "Zdb", ([type = ZeString{row.type}](auto &s) {
	  s << "invalid recovered saga \"" << type << '"';
	}));
      if (ZuUnlikely(m_sagaHash->find(
	    ZuFwdTuple(row.type, row.id))))
	throw ZeEXCEPT(Fatal, "Zdb", "duplicate recovered saga");
      ZmRef<AnySaga_> saga = m_sagaDefs[typeIndex].load(row.data);
      if (ZuUnlikely(!saga || saga->type() != row.type))
	throw ZeEXCEPT(Fatal, "Zdb", "invalid recovered saga type");
      ZuCSpan type = saga->type();
      auto stepCount = saga->nSteps();
      if (ZuUnlikely(!stepCount))
	throw ZeEXCEPT(Fatal, "Zdb", "invalid recovered saga length");
      saga->init_(this, SagaKey{type, row.id}, row.shard, m_sagaEpoch,
	stepCount, row.deadline);
      m_sagaHash->addNode(saga.ptr());
      m_sagaQueue.pushNode(saga.ptr());
    }
  } catch (ZeException &e) {
    sagaActivateFail(ZuMv(e));
    return;
  } catch (...) {
    sagaActivateFail(ZeEXCEPT(Fatal, "Zdb", "failed to load recovered saga"));
    return;
  }
  if (context->pageCount == SagaScanSize) {
    auto &last = context->dataRows[context->dataRows.length() - 1];
    context->cursor = SagaCursor{ZuMv(last.type), last.id};
    context->dataRows.length(0);
    sagaScanData(ZuMv(context), true);
    return;
  }
  context->dataRows.null();
  // The scan itself owns one pending completion until authoritative EOR.
  context->pending = 1;
  sagaScanSteps(ZuMv(context), false);
}

void SagaDB::sagaScanSteps(ZmRef<SagaScan> context, bool next)
{
  ZmAssert(invoked());
  ++m_sagaPending;
  context->pageCount = 0;
  using Tuple = TableT<SagaStep>::Tuple;
  auto receive = [this, context](ZuUnion<void, Tuple> result, unsigned count) mutable {
    if (result.template is<Tuple>()) {
      ZuTupleCall(ZuMv(result).template p<Tuple>(), [context](auto &&...args) {
	new (context->stepRows.push()) SagaStep{ZuFwd<decltype(args)>(args)...};
      });
      context->pageCount = count;
      return;
    }
    run([this, context = ZuMv(context)]() mutable {
      sagaRetire();
      if (context->epoch != m_sagaEpoch ||
	  m_sagaState != SagaState::Rebuilding) return;
      sagaLoadSteps(ZuMv(context));
    });
  };
  if (next) {
    m_sagaStepTable->nextRows<0>(
      {context->cursor.p<0>(), context->cursor.p<1>(), context->step},
      false, SagaScanSize, ZuMv(receive));
  } else {
    m_sagaStepTable->selectRows<0>({}, SagaScanSize, ZuMv(receive));
  }
}

void SagaDB::sagaLoadSteps(ZmRef<SagaScan> context)
{
  ZmAssert(invoked());
  if (ZuUnlikely(context->epoch != m_sagaEpoch ||
	m_sagaState != SagaState::Rebuilding)) return;

  for (unsigned i = 0, n = context->stepRows.length(); i < n; ++i) {
    const auto &row = context->stepRows[i];
    auto node = m_sagaHash->findPtr(ZuFwdTuple(row.type, row.id));
    if (!node) continue;
    auto saga = static_cast<AnySaga_ *>(node);
    ZuCSpan tableID;
    SagaOp::T op = SagaOp::Invalid;
    if (ZuUnlikely(row.shard >= nShards() ||
	!saga->stepDef(row.step, tableID, op) ||
	(row.un == nullUN() && op != SagaOp::Delete))) {
      sagaActivateFail(ZeEXCEPT(
	Fatal, "Zdb", ([type = ZeString{row.type}, step = row.step,
		shard = row.shard, un = row.un](auto &s) {
	  s << "invalid recovered saga step type=" << type
	    << " step=" << step << " shard=" << shard << " un=" << un;
	})));
      return;
    }
    ZmRef<AnyTable> table_ = table(tableID);
    if (ZuUnlikely(!table_)) {
      sagaActivateFail(ZeEXCEPT(Fatal, "Zdb", "unknown recovered saga table"));
      return;
    }
    if (ZuUnlikely(sagaRec(saga, row.step) ||
	(row.un != nullUN() && sagaReserved(table_, row.shard, row.un)))) {
      sagaActivateFail(ZeEXCEPT(
	Fatal, "Zdb", "duplicate recovered saga reservation"));
      return;
    }
    saga->stepShard(row.step, row.shard);
    ZmRef<SagaRec> rec = new SagaRec{saga, table_, row.un, row.step, row.shard, op};
    m_sagaStepHash->addNode(rec.ptr());
    if (row.un != nullUN())
      m_sagaUNHash->addNode(static_cast<SagaUNHash::Node *>(rec.ptr()));
  }

  for (unsigned i = 0, n = context->stepRows.length(); i < n; ++i) {
    const auto &row = context->stepRows[i];
    if (m_sagaHash->findPtr(ZuFwdTuple(row.type, row.id))) continue;
    if (ZuUnlikely(row.shard >= nShards())) {
      sagaActivateFail(ZeEXCEPT(Fatal, "Zdb", "invalid orphan saga step"));
      return;
    }
    ++context->pending;
    ++m_sagaPending;
    auto shard = row.shard;
    TableT<SagaStep>::template Key<0> stepKey{
      row.type, row.id, row.step};
    m_sagaStepTable->run(shard, [
      this, context, shard, stepKey = ZuMv(stepKey)
    ]() mutable {
      m_sagaStepTable->findDel<0>(shard, stepKey,
	[this, context = ZuMv(context)](Row<SagaStep> *row) mutable {
	  bool ok = !row;
	  if (row) ok = row->commit();
	  run([this, context = ZuMv(context), ok]() mutable {
	    sagaRetire();
	    sagaOrphanDone(ZuMv(context), ok);
	  });
	});
    });
  }
  if (context->pageCount == SagaScanSize) {
    auto &last = context->stepRows[context->stepRows.length() - 1];
    context->cursor = SagaCursor{ZuMv(last.type), last.id};
    context->step = last.step;
    context->stepRows.length(0);
    sagaScanSteps(ZuMv(context), true);
    return;
  }
  context->stepRows.null();
  sagaOrphanDone(ZuMv(context), true);
}

void SagaDB::sagaOrphanDone(ZmRef<SagaScan> context, bool ok)
{
  ZmAssert(invoked());
  if (ZuUnlikely(context->epoch != m_sagaEpoch ||
	m_sagaState != SagaState::Rebuilding)) return;
  if (ZuUnlikely(!ok)) {
    sagaActivateFail(ZeEXCEPT(Fatal, "Zdb", "orphan saga step delete failed"));
    return;
  }
  if (!--context->pending) sagaReplay();
}

} // Zdb_
