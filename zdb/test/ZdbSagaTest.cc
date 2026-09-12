//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuMatcher.hh>

#include <zlib/ZfCf.hh>

#include <zlib/ZvMxParams.hh>

#include <zlib/ZiLog.hh>

#include <zlib/Zdb.hh>

#include "ZdbMockStore.hh"
#include "ZdbTest.hh"

using namespace ZuTestUtil;

namespace zdbtest {

struct KeyOrder {
  unsigned member;
  unsigned group;
};
ZfStruct(, KeyOrder,
  (((member), (Ctor<0>, (Keys<0, 1>), Group<0>)), (UInt32)),
  (((group), (Ctor<1>, (Keys<0, 1>), Group<1>)), (UInt32)));

struct Context : public ZmPolymorph { };
struct OtherContext : public ZmPolymorph { };

struct SagaA : public ZdbSagaBase<Context> {
  using Base = ZdbSagaBase<Context>;
  using Base::context;
  using Base::saga;
  using Type = ZuStringT<"sagaA">;
  enum { NSteps = 3 };

  uint64_t value = 0;

  ZdbSagaStep(0, order, Insert) {
    complete(true);
    return {};
  }
  ZdbSagaStep(1, order, Update) { return {}; }
  ZdbSagaStep(2, payment, Delete) { return {}; }
};

ZfbStruct(, SagaA,
  (((value), (Ctor<0>)), (UInt64)));

struct SagaB : public ZdbSagaBase<Context> {
  using Base = ZdbSagaBase<Context>;
  using Base::context;
  using Base::saga;
  using Type = ZuStringT<"sagaB">;
  enum { NSteps = 1 };

  uint32_t value = 0;

  ZdbSagaStep(0, payment, Update) { return {}; }
};

ZfbStruct(, SagaB,
  (((value), (Ctor<0>)), (UInt32)));

template <typename Catalog>
static int sagaMatch(ZuCSpan type)
{
  struct IDs { using Keys = Zdb_::SagaTypes<typename Catalog::List>; };
  static constexpr auto matcher = ZuMatcher<IDs>();
  return matcher.exact(type);
}

struct Sagas {
  using List = ZuTypeList<SagaA, SagaB>;
  static int match(ZuCSpan type) { return sagaMatch<Sagas>(type); }
};

struct RepeatSaga : public ZdbSagaBase<Context> {
  using Base = ZdbSagaBase<Context>;
  using Type = ZuStringT<"repeatSaga">;
  enum { NSteps = 4 };
  uint64_t first = 0;
  uint64_t second = 0;

  ZdbSagaRepeatStep(0, order, Insert, first) { return {}; }
  ZdbSagaStep(1, order, Update) { return {}; }
  ZdbSagaRepeatStep(2, payment, Delete, second) { return {}; }
  ZdbSagaStep(3, payment, Update) { return {}; }
};
ZfbStruct(, RepeatSaga,
  (((first), (Ctor<0>)), (UInt64)),
  (((second), (Ctor<1>)), (UInt64)));
struct RepeatSagas {
  using List = ZuTypeList<RepeatSaga>;
  static int match(ZuCSpan type) { return sagaMatch<RepeatSagas>(type); }
};

struct BadRepeat {
  enum { NSteps = 1 };
  ZdbSagaRepeatStep(0, order, Insert, 2) { return {}; }
};
ZuAssert((!Zdb_::SagaDefValid<BadRepeat>{}));

ZuAssert((Zdb_::SagaBasesValid_<Context, Sagas::List>{}));
ZuAssert((!Zdb_::SagaBasesValid_<OtherContext, Sagas::List>{}));

namespace Trace {
  enum { Intent, Enter, Commit, Return, Probe, Next, Read };
}

using PauseFn = ZmFn<void(bool), ZmFnHeapID<"Zdb.Saga.Test.Pause">>;

struct MoveSubmit {
  bool		*result;
  ZmSemaphore	*done;

  MoveSubmit(bool *result_, ZmSemaphore *done_) :
    result{result_}, done{done_} { }
  MoveSubmit(const MoveSubmit &) = delete;
  MoveSubmit &operator =(const MoveSubmit &) = delete;
  MoveSubmit(MoveSubmit &&) = default;
  MoveSubmit &operator =(MoveSubmit &&) = default;

  void operator ()(bool ok) {
    *result = ok;
    done->post();
  }
};

struct MoveComplete {
  bool		*result;
  unsigned	*called;
  ZmSemaphore	*done;

  MoveComplete(bool *result_, unsigned *called_, ZmSemaphore *done_) :
    result{result_}, called{called_}, done{done_} { }
  MoveComplete(const MoveComplete &) = delete;
  MoveComplete &operator =(const MoveComplete &) = delete;
  MoveComplete(MoveComplete &&) = default;
  MoveComplete &operator =(MoveComplete &&) = default;

  void operator ()(bool ok) {
    *result = ok;
    ++*called;
    done->post();
  }
};

struct LiveContext : public ZmPolymorph {
  ZmFn<void(unsigned, unsigned)> trace;
  ZdbTable<Order>	*orders = nullptr;
  ZmSemaphore	*done = nullptr;
  ZmSemaphore	*paused = nullptr;
  ZmSemaphore	*reverseFailed = nullptr;
  unsigned	runs = 0;
  unsigned	inserts = 0;
  unsigned	updates = 0;
  unsigned	deletes = 0;
  unsigned	completed = 0;
  unsigned	errors = 0;
  unsigned	reads = 0;
  ZuTime	deadline;
  bool		read = false;
  bool		secondary = false;
  uint32_t	pauseStep = UINT32_MAX;
  uint32_t	skipStep = UINT32_MAX;
  bool		pausedOnce = false;
  bool		pauseAll = false;
  uint32_t	failStep = UINT32_MAX;
  bool		absentReverse = false;
  bool		failReverse = false;
  ZtArray<int> dirs;
  ZtArray<PauseFn> pausedCompletes;

  template <typename Complete>
  void pause(Complete &&complete) {
    pausedCompletes.push(PauseFn{ZuFwd<Complete>(complete)});
    paused->post();
  }
  PauseFn resume() { return pausedCompletes.pop(); }
  void release() {
    while (pausedCompletes.length()) pausedCompletes.pop()(false);
  }
};

struct LiveSaga : public ZdbSagaBase<LiveContext> {
  using Base = ZdbSagaBase<LiveContext>;
  using Base::context;
  using Base::saga;
  using Type = ZuStringT<"liveSaga">;
  enum { NSteps = 3 };

  uint64_t orderID = 0;

  template <unsigned Step, bool Fwd, typename Complete>
  void begin(Complete &&complete) {
    context->deadline = saga->deadline();
    ++context->runs;
    context->dirs.push(Fwd ? int(Step) + 1 : -int(Step) - 1);
    if (context->trace) context->trace(Trace::Next, Step);
    if constexpr (Fwd)
      if (Step == context->skipStep) {
	saga->skip(ZuFwd<Complete>(complete));
	return;
      }
    if constexpr (Fwd)
      if (Step == context->failStep) {
	if (context->absentReverse && Step) {
	  context->orders->run(0,
	    [this, complete = ZuMv(complete)]() mutable {
	    context->orders->findDel<0>(0, ZuFwdTuple("IBM", orderID),
	      [complete = ZuMv(complete)](ZdbRow<Order> *row) mutable {
		if (row) row->commit();
		complete(false);
	      });
	  });
	  return;
	}
	complete(false);
	return;
      }
    if (Step == context->pauseStep &&
	(!context->pausedOnce || context->pauseAll)) {
      context->pausedOnce = true;
      context->pause(PauseFn{
	[this, complete = ZuMv(complete)](bool ok) mutable {
	  if (!ok) { complete(false); return; }
	  begin<Step, Fwd>(ZuMv(complete));
	}});
      return;
    }
    if (context->read) {
      context->orders->count<0>({}, [
	this, complete = ZuMv(complete)
      ](ZuUnion<void, uint64_t> result) mutable {
	  if (!result.is<uint64_t>()) {
	    ++context->errors;
	    complete(false);
	    return;
	  }
	  ++context->reads;
	  if (context->trace) context->trace(Trace::Read, Step);
	  run<Fwd>(ZuMv(complete), ZuUnsigned<Step>{});
      });
      return;
    }
    run<Fwd>(ZuFwd<Complete>(complete), ZuUnsigned<Step>{});
  }

  template <bool Fwd, typename Complete>
  void run(Complete &&complete,
      ZuUnsigned<0>) {
	context->orders->run(0, [
	  this, complete = ZuMv(complete)
	]() mutable {
	if constexpr (Fwd) {
	  ZdbRowRef<Order> order =
	    new ZdbRow<Order>{context->orders, ZdbShard{0}};
	  saga->insert(context->orders, ZuMv(order), ZuMv(complete),
	  [this](ZdbRow<Order> *row, auto &&complete) mutable {
	    ++context->inserts;
	    if (context->trace) context->trace(Trace::Enter, 0);
	    new (row->ptr()) Order{
	      "IBM", orderID, "FIX0", "saga", 0,
	      Side::Buy, {100}, {1}};
	    bool ok = row->commit();
	    if (context->trace) context->trace(Trace::Return, 0);
	    complete(ok);
	  });
	} else {
	  saga->findDel<0>(context->orders, 0, ZuFwdTuple("IBM", orderID),
	    ZuMv(complete), [this](ZdbRow<Order> *row, auto &&complete) mutable {
	      if (context->failReverse) {
		complete(false);
		if (context->reverseFailed) context->reverseFailed->post();
		return;
	      }
	      complete(bool(row->commit()));
	    });
	}
	});
  }

  template <bool Fwd, typename Complete>
  void run(Complete &&complete,
      ZuUnsigned<1>) {
	context->orders->run(0, [
	  this, complete = ZuMv(complete)
	]() mutable {
	auto fn = [this](ZdbRow<Order> *row, auto &&complete) mutable {
	  ++context->updates;
	  if (context->trace) context->trace(Trace::Enter, 1);
	  row->data().seqNo = Fwd ? 1 : 0;
	  bool ok = row->commit();
	  if (context->trace) context->trace(Trace::Return, 1);
	  complete(ok);
	};
	if (context->secondary)
	  saga->findUpd<1, ZuSeq<2>>(context->orders, 0,
	    ZuFwdTuple("FIX0", "saga"), ZuMv(complete), ZuMv(fn));
	else
	  saga->findUpd<0, ZuSeq<2>>(context->orders, 0,
	    ZuFwdTuple("IBM", orderID), ZuMv(complete), ZuMv(fn));
	});
  }

  template <bool Fwd, typename Complete>
  void run(Complete &&complete,
      ZuUnsigned<2>) {
	ZuAssert(Fwd);
	context->orders->run(0, [
	  this, complete = ZuMv(complete)
	]() mutable {
	auto complete_ = [this, complete = ZuMv(complete)](bool ok) mutable {
	  if (context->pauseStep == 3 &&
	      (!context->pausedOnce || context->pauseAll)) {
	    context->pausedOnce = true;
	    context->pause([
	      complete = ZuMv(complete), ok
	    ](bool resume) mutable { complete(resume && ok); });
	    return;
	  }
	  complete(ok);
	};
	auto fn = [this](ZdbRow<Order> *row, auto &&complete) mutable {
	  ++context->deletes;
	  if (context->trace) context->trace(Trace::Enter, 2);
	  if (!row->commit()) { complete(false); return; }
	  if (context->trace) context->trace(Trace::Return, 2);
	  complete(true);
	};
	if (context->secondary)
	  saga->findDel<1>(context->orders, 0,
	    ZuFwdTuple("FIX0", "saga"), ZuMv(complete_), ZuMv(fn));
	else
	  saga->findDel<0>(context->orders, 0,
	    ZuFwdTuple("IBM", orderID), ZuMv(complete_), ZuMv(fn));
	});
  }

  ZdbSagaStep(0, o, Insert) {
    begin<Step, Fwd>(ZuFwd<Complete>(complete));
    return {};
  }
  ZdbSagaStep(1, o, Update) {
    begin<Step, Fwd>(ZuFwd<Complete>(complete));
    return {};
  }
  ZdbSagaStep(2, o, Delete) {
    begin<Step, Fwd>(ZuFwd<Complete>(complete));
    return {};
  }
};

ZfbStruct(, LiveSaga,
  (((orderID), (Ctor<0>)), (UInt64)));

struct LiveSagas {
  using List = ZuTypeList<LiveSaga>;
  static int match(ZuCSpan type) { return sagaMatch<LiveSagas>(type); }
};

struct ShortSaga : public ZdbSagaBase<LiveContext> {
  using Base = ZdbSagaBase<LiveContext>;
  using Type = LiveSaga::Type;
  enum { NSteps = 1 };
  uint64_t orderID = 0;
  ZdbSagaStep(0, o, Insert) { return {}; }
};
ZfbStruct(, ShortSaga,
  (((orderID), (Ctor<0>)), (UInt64)));

struct ChangedSaga : public ZdbSagaBase<LiveContext> {
  using Base = ZdbSagaBase<LiveContext>;
  using Type = LiveSaga::Type;
  enum { NSteps = 3 };
  uint64_t orderID = 0;
  ZdbSagaStep(0, o, Insert) { return {}; }
  ZdbSagaStep(1, o, Delete) { return {}; }
  ZdbSagaStep(2, o, Delete) { return {}; }
};
ZfbStruct(, ChangedSaga,
  (((orderID), (Ctor<0>)), (UInt64)));

struct ShortCatalog {
  using List = ZuTypeList<ShortSaga>;
  static int match(ZuCSpan type) { return sagaMatch<ShortCatalog>(type); }
};
struct ChangedCatalog {
  using List = ZuTypeList<ChangedSaga>;
  static int match(ZuCSpan type) { return sagaMatch<ChangedCatalog>(type); }
};

struct PayloadContext : public ZmPolymorph {
  ZmSemaphore entered;
  Zdb_::SagaCompleteFn complete;
};

struct PayloadSaga : public ZdbSagaBase<PayloadContext> {
  using Base = ZdbSagaBase<PayloadContext>;
  using Base::context;
  using Base::saga;
  using Type = ZuStringT<"payloadSagaWithATypeNameThatExceedsBuiltinStringStorage">;
  enum { NSteps = 1 };

  Zdb_::SagaPayload data;

  ZdbSagaStep(0, o, Update) {
    if (context) {
      context->complete = ZuMv(complete);
      context->entered.post();
    }
    return {};
  }
};
ZfbStruct(, PayloadSaga,
  (((data), (Ctor<0>)), (Bytes)));

} // zdbtest

template <typename T>
static T roundTrip(const T &data)
{
  Zfb::IOBuilder fbb{new Zdb_::IOBufAlloc<T>{}};
  fbb.Finish(ZfbStruct::save(fbb, data));
  auto fbo = ZfbStruct::verify<T>(
    {fbb.GetBufferPointer(), unsigned(fbb.GetSize())});
  ZmAssert(fbo);
  return ZfbStruct::ctor<T>(fbo);
}

static bool data(unsigned size)
{
  Zdb_::SagaPayload payload;
  payload.length(size, false);
  for (unsigned i = 0; i < size; ++i) payload[i] = uint8_t(i);

  Zdb_::SagaData row{
    .type = "orderNew",
    .id = (uint128_t(1)<<127) | 42,
    .shard = 7,
    .data = ZuMv(payload)
  };
  auto loaded = roundTrip(row);
  if (loaded.type != row.type || loaded.id != row.id ||
      loaded.shard != row.shard || loaded.data.length() != size)
    return false;
  for (unsigned i = 0; i < size; ++i)
    if (loaded.data[i] != uint8_t(i)) return false;
  return true;
}

static void rows()
{
  ZuTestScope(rows);

  ZuCheck(data(ZdbSaga_BuiltinSize - 1));
  ZuCheck(data(ZdbSaga_BuiltinSize));
  ZuCheck(data(ZdbSaga_BuiltinSize + 1));

  Zdb_::SagaStep step{
    .type = "orderNew",
    .id = (uint128_t(1)<<96) | 7,
    .step = UINT32_C(0xf1234567),
    .shard = 63,
    .un = UINT64_C(0xf123456789abcdef)
  };
  auto loadedStep = roundTrip(step);
  ZuCheck(loadedStep.type == step.type);
  ZuCheck(loadedStep.id == step.id);
  ZuCheck(loadedStep.step == step.step);
  ZuCheck(loadedStep.shard == step.shard);
  ZuCheck(loadedStep.un == step.un);

  auto op = [](ZdbSagaOp::T op) {
    Zdb_::SagaTypeStep typeStep{
      .type = "orderNew",
      .table = "orders",
      .step = 3,
      .op = op
    };
    auto loadedTypeStep = roundTrip(typeStep);
    return loadedTypeStep.type == typeStep.type &&
      loadedTypeStep.step == typeStep.step &&
      loadedTypeStep.table == typeStep.table &&
      loadedTypeStep.op == typeStep.op;
  };
  ZuCheck(op(ZdbSagaOp::Invalid));
  ZuCheck(op(ZdbSagaOp::Insert));
  ZuCheck(op(ZdbSagaOp::Update));
  ZuCheck(op(ZdbSagaOp::Delete));
}

static bool payload(unsigned size)
{
  struct Catalog {
    using List = ZuTypeList<zdbtest::PayloadSaga>;
    static int match(ZuCSpan type) {
      return zdbtest::sagaMatch<Catalog>(type);
    }
  };
  using M = ZdbMSaga<Catalog>;
  Zdb_::SagaPayload input;
  input.length(size, false);
  for (unsigned i = 0; i < size; ++i) input[i] = uint8_t(i);
  ZmRef<M> saga = new M{};
  saga->init(zdbtest::PayloadSaga{{}, ZuMv(input)});
  Zdb_::SagaPayload saved;
  M::save(saga, saved);
  auto loaded = M::load(zdbtest::PayloadSaga::Type{}(), saved);
  return loaded->u.cdispatch([size](auto, const auto &def) {
    if (def.data.length() != size) return false;
    for (unsigned i = 0; i < size; ++i)
      if (def.data[i] != uint8_t(i)) return false;
    return true;
  });
}

static void dispatch()
{
  ZuTestScope(dispatch);

  using M = ZdbMSaga<zdbtest::Sagas>;
  using DB = ZdbSagaDB<zdbtest::Context, zdbtest::Sagas>;
  ZuAssert((ZuIsSame<typename DB::M, M>{}));

  ZmRef<M> saga = new M{};
  saga->init(zdbtest::SagaA{{}, 42});
  ZuCheck(M::match("sagaA") == 0);
  ZuCheck(M::match("sagaB") == 1);
  ZuCheck(M::match("sagaC") < 0);
  ZuCheck(M::type(saga) == "sagaA");
  ZuCheck(M::stepCount("sagaA") == 3);

  ZuCSpan table;
  ZdbSagaOp::T op = ZdbSagaOp::Invalid;
  ZuCheck(M::stepDef("sagaA", 2, table, op));
  ZuCheck(table == "payment");
  ZuCheck(op == ZdbSagaOp::Delete);

  Zdb_::SagaPayload data;
  M::save(saga, data);
  ZmRef<M> loaded = M::load("sagaA", data);
  ZuCheck(M::type(loaded) == "sagaA");
  ZuCheck(loaded->u.cdispatch([](auto, const auto &def) {
    return def.value == 42 && !def.context && !def.saga;
  }));

  ZtArray<Zdb_::SagaTypeStep> catalog;
  for (unsigned i = 0; i < zdbtest::Sagas::List::N; ++i) {
    Zdb_::SagaTypeStep row;
    for (unsigned j = 0; M::catalog(i, j, row); ++j)
      catalog.push(ZuMv(row));
  }
  ZuCheck(catalog.length() == 4);
  ZuCheck(catalog[0].type == "sagaA");
  ZuCheck(catalog[0].table == "order");
  ZuCheck(catalog[3].type == "sagaB");
  ZuCheck(catalog[3].op == ZdbSagaOp::Update);
  ZuCheck(payload(0));
  ZuCheck(payload(ZdbSaga_BuiltinSize - 1));
  ZuCheck(payload(ZdbSaga_BuiltinSize));
  ZuCheck(payload(ZdbSaga_BuiltinSize + 1));
  ZuCheck(payload(4 * ZdbSaga_BuiltinSize));
}

static void catalog()
{
  ZuTestScope(catalog);
  using M = ZdbMSaga<zdbtest::Sagas>;
  auto context = []() {
    ZmRef<Zdb_::SagaCatalog> c = new Zdb_::SagaCatalog{};
    c->seen.length(zdbtest::Sagas::List::N, false);
    memset(c->seen.data(), 0, c->seen.length());
    return c;
  };
  auto row = [](unsigned type, unsigned step) {
    Zdb_::SagaTypeStep row;
    M::catalog(type, step, row);
    return row;
  };

  auto empty = context();
  empty->end<zdbtest::Sagas>();
  ZuCheck(!empty->error);
  ZuCheck(!empty->seen[0] && !empty->seen[1]);

  auto exact = context();
  exact->load<zdbtest::Sagas>(row(0, 0));
  exact->load<zdbtest::Sagas>(row(0, 1));
  exact->load<zdbtest::Sagas>(row(0, 2));
  exact->load<zdbtest::Sagas>(row(1, 0));
  exact->end<zdbtest::Sagas>();
  ZuCheck(!exact->error);
  ZuCheck(exact->seen[0] && exact->seen[1]);

  auto historical = context();
  historical->load<zdbtest::Sagas>({"oldSaga", "oldTable", 0, ZdbSagaOp::Insert});
  historical->load<zdbtest::Sagas>(row(1, 0));
  historical->end<zdbtest::Sagas>();
  ZuCheck(!historical->error);
  ZuCheck(!historical->seen[0] && historical->seen[1]);

  auto partial = context();
  partial->load<zdbtest::Sagas>(row(0, 0));
  partial->end<zdbtest::Sagas>();
  ZuCheck(bool(partial->error));

  auto boundary = context();
  boundary->load<zdbtest::Sagas>(row(0, 0));
  boundary->load<zdbtest::Sagas>(row(1, 0));
  ZuCheck(bool(boundary->error));

  auto missing = context();
  missing->load<zdbtest::Sagas>(row(0, 1));
  ZuCheck(bool(missing->error));

  auto gap = context();
  gap->load<zdbtest::Sagas>(row(0, 0));
  gap->load<zdbtest::Sagas>(row(0, 2));
  ZuCheck(bool(gap->error));

  auto duplicate = context();
  duplicate->load<zdbtest::Sagas>(row(0, 0));
  duplicate->load<zdbtest::Sagas>(row(0, 0));
  ZuCheck(bool(duplicate->error));

  auto table = context();
  auto wrongTable = row(0, 0);
  wrongTable.table = "wrong";
  table->load<zdbtest::Sagas>(ZuMv(wrongTable));
  ZuCheck(bool(table->error));

  auto op = context();
  auto wrongOp = row(0, 0);
  wrongOp.op = ZdbSagaOp::Update;
  op->load<zdbtest::Sagas>(ZuMv(wrongOp));
  ZuCheck(bool(op->error));

  auto extra = context();
  extra->load<zdbtest::Sagas>(row(1, 0));
  auto extraStep = row(1, 0);
  extraStep.step = 1;
  extra->load<zdbtest::Sagas>(ZuMv(extraStep));
  ZuCheck(bool(extra->error));

  Zdb_::SagaTypeStep unused;
  ZuCheck(!M::catalog(2, 0, unused));
  ZuCheck(!M::catalog(0, 3, unused));
}

static void groupKeys()
{
  ZuTestScope(groupKeys);
  using Model = zdbtest::KeyOrder;
  auto primary = Zdb_::SplitKey<Model, 0>::storeFields();
  auto secondary = Zdb_::SplitKey<Model, 1>::storeFields();
  ZuCheck(primary.length() == 2);
  ZuCheck(primary[0]->id == "member");
  ZuCheck(primary[1]->id == "group");
  ZuCheck(secondary.length() == 2);
  ZuCheck(secondary[0]->id == "group");
  ZuCheck(secondary[1]->id == "member");
  Model row{17, 23};
  auto key = ZuStructKey<1>(row);
  ZuCheck(key.p<0>() == 17);
  ZuCheck(key.p<1>() == 23);
  auto plain = Zdb_::SplitKey<zdbtest::Order, 0>::storeFields();
  ZuCheck(plain[0]->id == "symbol");
  ZuCheck(plain[1]->id == "orderID");
}

static void repeatedLayout()
{
  ZuTestScope(repeatedLayout);
  using Def = zdbtest::RepeatSaga;
  using Layout = Zdb_::SagaLayout<Def>;
  using M = ZdbMSaga<zdbtest::RepeatSagas>;
  Def def{{}, 3, 2};
  ZuCheck(Layout::size(def) == 7);
  unsigned iteration;
  bool phases = true;
  bool iterations = true;
  for (unsigned step = 0; step < 7; ++step) {
    auto phase = Layout::phase(def, step, iteration);
    phases &= phase == (step < 3 ? 0 : step == 3 ? 1 : step < 6 ? 2 : 3);
    iterations &=
	iteration == (step < 3 ? step : step > 3 && step < 6 ? step - 4 : 0);
  }
  ZuCheck(phases);
  ZuCheck(iterations);
  ZuCheck(Layout::phase(def, 7, iteration) == Def::NSteps);
  ZmRef<M> saga = new M{};
  saga->init(ZuMv(def));
  Zdb_::SagaPayload payload;
  M::save(saga, payload);
  auto loaded = M::load(Def::Type{}(), payload);
  ZuCheck(M::stepCount(loaded.ptr()) == 7);
  ZuCSpan table;
  ZdbSagaOp::T op = ZdbSagaOp::Invalid;
  ZuCheck(M::stepDef(loaded.ptr(), 4, table, op));
  ZuCheck(table == "payment" && op == ZdbSagaOp::Delete);
  ZuCheck(M::stepDef(loaded.ptr(), 6, table, op));
  ZuCheck(op == ZdbSagaOp::Update);
  ZuCheck(!M::stepDef(loaded.ptr(), 7, table, op));

  Def empty;
  ZuCheck(Layout::size(empty) == 2);
  ZuCheck(Layout::phase(empty, 0, iteration) == 1 && !iteration);
  ZuCheck(Layout::phase(empty, 1, iteration) == 3 && !iteration);
  ZuCheck(Layout::size(Def{{}, UINT32_MAX - 2, 0}) == UINT32_MAX);
  ZuCheck(!Layout::size(Def{{}, UINT32_MAX - 1, 0}));
  ZuCheck(!Layout::size(Def{{}, UINT64_MAX, UINT64_MAX}));

  Zdb_::SagaTypeStep row;
  ZuCheck(M::catalog(0, 0, row) && row.repeat);
  ZmRef<Zdb_::SagaCatalog> catalog = new Zdb_::SagaCatalog{};
  catalog->seen.length(1);
  bool catalogRows = true;
  bool repeatFlags = true;
  for (unsigned phase = 0; phase < Def::NSteps; ++phase) {
    catalogRows &= M::catalog(0, phase, row);
    repeatFlags &= row.repeat == (phase == 0 || phase == 2);
    catalog->load<zdbtest::RepeatSagas>(row);
  }
  ZuCheck(catalogRows);
  ZuCheck(repeatFlags);
  catalog->end<zdbtest::RepeatSagas>();
  ZuCheck(!catalog->error);
  catalog = new Zdb_::SagaCatalog{};
  catalog->seen.length(1);
  M::catalog(0, 0, row);
  row.repeat = false;
  catalog->load<zdbtest::RepeatSagas>(row);
  ZuCheck(bool(catalog->error));
}

static void indexes()
{
  ZuTestScope(indexes);
  using namespace Zdb_;
  ZmRef<ZdbMSaga<zdbtest::Sagas>> saga = new ZdbMSaga<zdbtest::Sagas>{};
  saga->init(zdbtest::SagaA{{}, 1});
  ZmRef<SagaStepHashObj> steps = new SagaStepHashObj{};
  ZmRef<SagaUNHashObj> uns = new SagaUNHashObj{};
  ZmRef<SagaRec> effect = new SagaRec{saga, nullptr, 7, 0, 0, SagaOp::Insert};
  ZmRef<SagaRec> noop = new SagaRec{saga, nullptr, nullUN(), 1, 0, SagaOp::Delete};
  ZmRef<SagaRec> other = new SagaRec{saga, nullptr, 7, 2, 1, SagaOp::Update};
  steps->addNode(effect.ptr());
  steps->addNode(noop.ptr());
  steps->addNode(other.ptr());
  uns->addNode(static_cast<SagaUNHash::Node *>(effect.ptr()));
  uns->addNode(static_cast<SagaUNHash::Node *>(other.ptr()));
  effect = nullptr;
  noop = nullptr;
  other = nullptr;
  ZuCheck(steps->count_() == 3);
  ZuCheck(uns->count_() == 2);
  ZuCheck(bool(uns->find(SagaUNKey{nullptr, 0, 7})));
  ZuCheck(bool(uns->find(SagaUNKey{nullptr, 1, 7})));
  ZuCheck(!uns->find(SagaUNKey{nullptr, 0, nullUN()}));
  auto rec = steps->find(SagaStepKey{saga->type(), saga->id(), 0});
  ZuCheck(bool(rec));
  ZuCheck(rec->un == 7);
  uns->delNode(static_cast<SagaUNHash::Node *>(rec.ptr()));
  steps->delNode(rec.ptr());
  rec = nullptr;
  ZuCheck(!uns->find(SagaUNKey{nullptr, 0, 7}));
  ZuCheck(!steps->find(SagaStepKey{saga->type(), saga->id(), 0}));
  ZuCheck(steps->count_() == 2);
  uns->clean();
  steps->clean();
  ZuCheck(!uns->count_());
  ZuCheck(!steps->count_());
}

static ZuPtr<const ZfCf::AnyNode> cf(bool separate = false)
{
  auto scan = ZfCf::scan(ZtString<>{} <<
    "zdb: {\n"
    "  thread: zdb,\n" <<
    (separate ? "  threads: [shard],\n" : "") <<
    "  store: {thread: store},\n"
    "  hostID: self,\n"
    "  hosts: {self: {standalone: true}},\n"
    "  tables: {o: {cacheMode: All}}\n"
    "},\n"
    "mx: {\n"
    "  nThreads: " << (separate ? 5 : 4) << ",\n"
    "  threads: {\n"
    "    1: {name: rx, isolated: true},\n"
    "    2: {name: tx, isolated: true},\n"
    "    3: {name: zdb, isolated: true},\n"
    "    4: {name: store, isolated: true}\n" <<
    (separate ? "    ,5: {name: shard, isolated: true}\n" : "") <<
    "  },\n"
    "  rxThread: rx,\n"
    "  txThread: tx\n"
    "}\n");
  return ZuMv(scan.p<1>());
}

static ZmSemaphore *active_;
static ZmSemaphore *down_;
static unsigned activations;

static void tableIDs()
{
  ZuTestScopeRT(tableIDs);
  auto config = cf();
  ZiMultiplex mx{ZvMxParams{"mx", config->resolve("mx")}};
  ZuCheckRT(mx.start());
  ZmRef<zdbtest::Store> store = new zdbtest::Store{};
  ZmRef<Zdb> db = new Zdb{};
  db->init(ZdbCf{config->resolve("zdb")}, &mx, {}, store);
  for (auto id : {"saga", "saga_step", "saga_type"}) {
    bool rejected = false;
    try {
      db->initTable<zdbtest::Order>(id);
    } catch (ZeException &) {
      rejected = true;
    }
    ZuCheckRT(rejected);
  }
  for (auto id : {"saga_app", "saga_step_log", "saga_type_v2", "my_saga"}) {
    bool accepted = false;
    try {
      auto table = db->initTable<zdbtest::Order>(id);
      accepted = table && table->id() == id;
    } catch (ZeException &) { }
    ZuCheckRT(accepted);
  }
  ZuCheckRT(db->start());
  ZuCheckRT(db->stop());
  db->final();
  db = {};
  store = {};
  ZuCheckRT(mx.stop());
}

static void collisions()
{
  ZuTestScope(collisions);
  using namespace Zdb_;
  using zdbtest::Order;

  auto config = cf();
  ZiMultiplex mx{ZvMxParams{"mx", config->resolve("mx")}};
  ZuCheck(mx.start());
  ZmRef<ZdbMem::Store> store = new ZdbMem::Store{};
  auto init = store->init(config->resolve("zdb.store"), &mx, 1, {});
  ZuCheck(init.is<InitData>());
  StoreTbl *table = nullptr;
  ZmSemaphore opened;
  store->open(false, "collisions", ZfVFields<Order>(), ZfVKeyFields<Order>(),
    reflection::GetSchema(ZfbSchema<Order>::data()), Table<Order>::allocBuf,
    [&table, &opened](OpenResult result) {
      if (result.is<OpenData>()) table = result.p<OpenData>().storeTbl;
      opened.post();
    });
  opened.wait();
  ZuCheck(table);

  auto insert = [table](UN un, uint64_t seqNo) {
    Order row{"IBM", 1, "FIX0", "collision", seqNo,
      zdbtest::Side::Buy, {100}, {1}};
    Zfb::IOBuilder fbb{Table<Order>::allocBuf()};
    auto data = Zfb::Save::nest(fbb, [&row](Zfb::Builder &fbb) {
      return ZfbStruct::save(fbb, row).Union();
    });
    auto sn = ZfbTransform::UInt128::save(SN{un});
    fbb.Finish(fbs::CreateMsg(fbb, fbs::Body::Replication,
      fbs::CreateRecord(fbb, Zfb::Save::str(fbb, "collisions"),
	un, &sn, 0, 0, data).Union()));
    bool ok = false;
    ZmSemaphore done;
    table->write(saveHdr(fbb), [&ok, &done](ZmRef<IOBuf>, CommitResult result) {
      ok = result.is<void>();
      done.post();
    });
    done.wait();
    return ok;
  };
  ZuCheck(insert(0, 7));
  ZuCheck(insert(1, 99));

  bool unchanged = false;
  unsigned rows = 0;
  ZmSemaphore selected;
  Zfb::IOBuilder fbb{Table<Order>::allocBuf()};
  using GroupKey = typename SplitKey<Order, 0>::GroupKey;
  fbb.Finish(ZfbStruct::save(fbb, GroupKey{}).Union());
  table->select(true, false, false, 0, fbb.buf(), 10,
    [&unchanged, &rows, &selected](TupleResult result) {
      if (result.is<TupleData>()) {
	++rows;
	auto &data = result.p<TupleData>();
	auto fbo = ZfbStruct::verify<Order>({data.buf->data(), data.buf->length});
	unchanged = fbo && ZfbStruct::ctor<Order>(fbo).seqNo == 7;
	return;
      }
      selected.post();
    });
  selected.wait();
  ZuCheck(rows == 1);
  ZuCheck(unchanged);
  ZmSemaphore closed;
  table->close([&closed]() { closed.post(); });
  closed.wait();
  bool position = false;
  store->open(false, "collisions", ZfVFields<Order>(), ZfVKeyFields<Order>(),
    reflection::GetSchema(ZfbSchema<Order>::data()), Table<Order>::allocBuf,
    [&position, &table, &opened](OpenResult result) {
      if (result.is<OpenData>()) {
	auto &data = result.p<OpenData>();
	table = data.storeTbl;
	position = data.count == 1 && data.un[0] == 1 && data.sn == 1;
      }
      opened.post();
    });
  opened.wait();
  ZuCheck(position);
  table->close([&closed]() { closed.post(); });
  closed.wait();
  store->final();
  store = {};
  ZuCheck(mx.stop());
}

static void up(Zdb *, ZdbHost *)
{
  active_->post();
}

static void down(Zdb *, bool)
{
  down_->post();
}

static void live(unsigned race, bool separate = false, bool secondary = false)
{
  ZuTestScope(live);

  auto config = cf(separate);
  ZiMultiplex mx{ZvMxParams{"mx", config->resolve("mx")}};
  ZuCheck(mx.start());

  ZmRef<zdbtest::Store> store = new zdbtest::Store{};
  ZmRef<ZdbSagaDB<zdbtest::LiveContext, zdbtest::LiveSagas>> db =
    new ZdbSagaDB<zdbtest::LiveContext, zdbtest::LiveSagas>{};
  ZmSemaphore active;
  active_ = &active;
  db->init(ZdbCf{config->resolve("zdb")}, &mx, ZdbHandler{
    .upFn = up
  }, store);
  auto orders = db->initTable<zdbtest::Order>("o");
  ZmSemaphore completed;
  ZmRef<zdbtest::LiveContext> context = new zdbtest::LiveContext{};
  context->orders = orders;
  context->done = &completed;
  context->secondary = secondary;
  db->sagas(context);
  ZuCheck(db->start());
  active.wait();

  // The mock write hook runs on the committing shard, before Table::commit
  // returns. Queue an interloper at each intent to catch a yield before effect.
  using namespace zdbtest::Trace;
  enum { Steps = zdbtest::LiveSaga::NSteps };
  ZtArray<unsigned, ZtArrayHeapID<"Zdb.Test.Trace">> phases;
  phases.length(Steps, false);
  for (unsigned i = 0; i < Steps; ++i) phases[i] = 0;
  unsigned intents = 0, effects = 0;
  bool ordered = true;
  auto journal = ZmBlock<ZmRef<Zdb_::AnyTable>>{}([db = db.ptr()](auto wake) {
    db->run([db, wake = ZuMv(wake)]() mutable {
      wake(db->table("saga_step"));
    });
  });
  context->trace = [
    &phases, &ordered, orders = orders.ptr(), journal = journal.ptr()
  ](unsigned event, unsigned step) {
    if (event == Next) {
      return;
    }
    ordered &= step < Steps;
    if (step >= Steps) return;
    auto &phase = phases[step];
    ordered &= phase == event;
    phase = event + 1;
    ordered &= journal->nextUN(0) == step + 1;
    ordered &= orders->nextUN(0) == step + (event >= Commit);
  };
  store->writeFn([
    db = db.ptr(), context = context.ptr(), &intents, &effects
  ](ZuCSpan id) {
    if (id == "saga_step" && intents < Steps) {
      unsigned step = intents++;
      context->trace(Intent, step);
      db->shardRun(0, [context, step]() { context->trace(Probe, step); });
    } else if (id == "o")
      context->trace(Commit, effects++);
  });

  bool replaced = false;
  ZmSemaphore interloper;
  if (race) {
    store->writeFn({});
    context->trace = [
      db = db.ptr(), orders = orders.ptr(), race, secondary,
      &replaced, &interloper
    ](
        unsigned event, unsigned step) {
      if (event != Next || step != race) return;
      // The direct application shard post must precede this DB-turn
      // interloper; the old DB-control bounce let the interloper win.
      db->run([orders, secondary, &replaced, &interloper]() {
	orders->run(0, [orders, secondary, &replaced, &interloper]() {
	  if (secondary)
	    orders->findUpd<0, ZuSeq<1, 2>>(0, ZuFwdTuple("IBM", UINT64_C(42)),
	      [](ZdbRow<zdbtest::Order> *row) {
		if (!row) return;
		row->data().link = "MOVED";
		row->data().clOrdID = "gone";
		row->commit();
	      });
	  orders->findDel<0>(0, ZuFwdTuple("IBM", UINT64_C(42)),
	    [](ZdbRow<zdbtest::Order> *row) {
	      if (row) row->commit();
	    });
	  ZdbRowRef<zdbtest::Order> row =
	    new ZdbRow<zdbtest::Order>{orders, 0};
	  orders->insert(ZuMv(row), [
	    secondary, &replaced, &interloper
	  ](ZdbRow<zdbtest::Order> *row) {
	    new (row->ptr()) zdbtest::Order{
	      "IBM", 42, secondary ? "FIX0" : "FIX1",
	      secondary ? "saga" : "replacement", 0,
	      zdbtest::Side::Buy, {100}, {2}};
	    replaced = bool(row->commit());
	    interloper.post();
	  });
	});
      });
    };
  }

  bool submittedOK = false;
  ZmSemaphore submitted;
  ZmRef<ZdbMSaga<zdbtest::LiveSagas>> saga =
    new ZdbMSaga<zdbtest::LiveSagas>{};
  saga->init(zdbtest::LiveSaga{{}, 42});
  ZuCheck(db->saga(0, 1, ZuMv(saga),
    [&submittedOK, &submitted](bool ok) {
      submittedOK = ok;
      submitted.post();
    }, [context = context.ptr()](bool ok) {
      if (!ok) ++context->errors;
      context->done->post();
    }));
  submitted.wait();
  completed.wait();
  if (race) interloper.wait();
  ZuCheck(submittedOK);
  ZuCheck(!context->errors);
  ZuCheck(context->runs == 3);
  ZuCheck(context->inserts == 1);
  ZuCheck(context->updates == 1);
  ZuCheck(context->deletes == 1);
  ZuCheck(race ? replaced : ordered && intents == Steps && effects == Steps);

  bool absent = false;
  ZmSemaphore found;
  orders->run(0, [orders = orders.ptr(), &absent, &found]() {
    orders->find<0>(0, ZuFwdTuple("IBM", UINT64_C(42)),
      [&absent, &found](ZdbRowRef<zdbtest::Order> row) {
	absent = !row;
	found.post();
      });
  });
  found.wait();
  ZuCheck(race || absent);

  ZuCheck(db->stop());
  journal = {};
  orders = {};
  db->final();
  db = {};
  store = {};
  ZuCheck(mx.stop());
}

// 0: after insert; 1: update intent only; 2: after delete; 3: absent delete;
// 4: main row only; 5: insert intent only; 6: delete intent only;
// 7: after update, before delete intent.
static void recovery(
    unsigned cut, bool separate = false, bool read = false,
    uint32_t skipStep = UINT32_MAX, bool expired = false)
{
  ZuTestScope(recovery);

  bool beforeInsert = cut == 4 || cut == 5;
  bool afterDelete = cut == 2 || cut == 3;
  int intentStep = cut == 1 ? 1 : cut == 5 ? 0 : cut == 6 ? 2 : -1;
  auto config = cf(separate);
  ZiMultiplex mx{ZvMxParams{"mx", config->resolve("mx")}};
  ZuCheck(mx.start());

  ZmRef<zdbtest::Store> store = new zdbtest::Store{};
  store->preserve();
  ZmRef<ZdbSagaDB<zdbtest::LiveContext, zdbtest::LiveSagas>> db =
    new ZdbSagaDB<zdbtest::LiveContext, zdbtest::LiveSagas>{};
  ZmSemaphore active;
  ZmSemaphore inactive;
  ZmSemaphore paused;
  active_ = &active;
  down_ = &inactive;
  db->init(ZdbCf{config->resolve("zdb")}, &mx, ZdbHandler{
    .upFn = up,
    .downFn = down
  }, store);
  auto orders = db->initTable<zdbtest::Order>("o");
  ZmRef<zdbtest::LiveContext> first = new zdbtest::LiveContext{};
  first->orders = orders;
  first->paused = &paused;
  first->read = read;
  first->skipStep = skipStep;
  first->pauseStep = beforeInsert ? 0U : cut < 2 ? 1U : cut == 2 ? 3U : 2U;
  db->sagas(first);
  ZuCheck(db->start());
  active.wait();

  bool submittedOK = false;
  ZmSemaphore submitted;
  ZmRef<ZdbMSaga<zdbtest::LiveSagas>> saga =
    new ZdbMSaga<zdbtest::LiveSagas>{};
  saga->init(zdbtest::LiveSaga{{}, 43});
  ZuTime deadline = expired ? ZuTime{0} : ZuTime{};
  ZuCheck(db->saga(0, 2, ZuMv(saga),
    [&submittedOK, &submitted](bool ok) {
      submittedOK = ok;
      submitted.post();
    }, [](bool) { }, deadline));
  submitted.wait();
  paused.wait();
  // Model a crash after an intent reaches the store, before its effect.
  bool journaled = intentStep < 0;
  if (intentStep >= 0)
    ZmBlock<>{}([db = db.ptr(), orders = orders.ptr(), intentStep, &journaled](auto wake) {
      db->run([db, orders, intentStep, &journaled, wake = ZuMv(wake)]() mutable {
	using Step = Zdb_::SagaStep;
	auto table = static_cast<ZdbTable<Step> *>(
	  db->table("saga_step").ptr());
	table->run(0, [table, orders, intentStep, &journaled, wake = ZuMv(wake)]() mutable {
	  ZdbRowRef<Step> row = new ZdbRow<Step>{table, ZdbShard{0}};
	  table->insert(ZuMv(row), [orders, intentStep, &journaled](ZdbRow<Step> *o) {
	    if (!o) return;
	    new (o->ptr()) Step{
	      .type = "liveSaga", .id = 2, .step = unsigned(intentStep), .shard = 0,
	      .un = orders->nextUN(0)
	    };
	    journaled = bool(o->commit());
	  });
	  wake();
	});
      });
    });
  bool noopUN = true;
  if (cut == 3) {
    // A different actor deletes the row before the saga reaches findDel.
    ZmBlock<>{}([orders = orders.ptr()](auto wake) {
      orders->run(0, [orders, wake = ZuMv(wake)]() mutable {
	orders->findDel<0>(0, ZuFwdTuple("IBM", UINT64_C(43)),
	  [wake = ZuMv(wake)](ZdbRow<zdbtest::Order> *o) mutable {
	    if (o) o->commit();
	    wake();
	});
      });
    });
    db->run([first = first.ptr()]() {
      first->pauseStep = 3;
      first->pausedOnce = false;
      first->resume()(true);
    });
    paused.wait();
    ZmBlock<>{}([orders = orders.ptr(), &noopUN](auto wake) {
      orders->run(0, [orders, &noopUN, wake = ZuMv(wake)]() mutable {
	noopUN = orders->nextUN(0) == 3;
	wake();
      });
    });
  }
  if (afterDelete)
    ZmBlock<>{}([orders = orders.ptr()](auto wake) {
      orders->run(0, [orders, wake = ZuMv(wake)]() mutable {
	ZdbRowRef<zdbtest::Order> replacement =
	  new ZdbRow<zdbtest::Order>{orders, ZdbShard{0}};
	orders->insert(ZuMv(replacement), [](ZdbRow<zdbtest::Order> *o) {
	  if (!o) return;
	  new (o->ptr()) zdbtest::Order{
	    "IBM", 43, "FIX0", "replacement", 99,
	    zdbtest::Side::Buy, {100}, {1}};
	  o->commit();
	});
	wake();
      });
    });
  store->sync();
  ZuCheck(journaled);
  ZuCheck(noopUN);
  ZuCheck(submittedOK);
  ZuCheck(first->runs ==
    (beforeInsert ? 1U : cut < 2 ? 2U : cut == 2 ? 3U : cut == 3 ? 4U : 3U));
  ZuCheck(first->inserts == unsigned(!beforeInsert));
  ZuCheck(!read || first->reads == first->inserts + first->updates + first->deletes);
  db->run([db = db.ptr(), first = first.ptr()]() {
    db->fail();
    first->release();
  });
  inactive.wait();
  ZuCheck(db->stop());
  orders = {};
  db->final();

  active_ = &active;
  db->init(ZdbCf{config->resolve("zdb")}, &mx, ZdbHandler{
    .upFn = up
  }, store);
  orders = db->initTable<zdbtest::Order>("o");
  ZmRef<zdbtest::LiveContext> second = new zdbtest::LiveContext{};
  second->orders = orders;
  second->read = read;
  second->skipStep = skipStep;
  // A fresh DB has no cached target row. Observe lookup yielding before the
  // first new intent, then prove its intent/effect pair does not yield.
  unsigned phase = 0;
  bool ordered = true;
  bool trace = !expired && (cut == 0 || cut == 7);
  if (trace) {
    store->findFn([db = db.ptr(), &phase, &ordered](ZuCSpan id) {
      if (id != "o" || phase) return;
      phase = 1;
      db->shardRun(0, [&phase, &ordered]() {
	ordered &= phase == 1;
	phase = 2;
      });
    });
    store->writeFn([db = db.ptr(), &phase, &ordered](ZuCSpan id) {
      if (phase >= 7) return;
      if (id == "saga_step" && phase < 3) {
	ordered &= phase == 2;
	phase = 3;
	db->shardRun(0, [&phase, &ordered]() {
	  ordered &= phase == 6;
	  phase = 7;
	});
      } else if (id == "o") {
	ordered &= phase == 4;
	phase = 5;
      }
    });
    second->trace = [cut, &phase, &ordered](unsigned event, unsigned step) {
      if (step != (cut == 0 ? 1U : 2U)) return;
      switch (event) {
	case zdbtest::Trace::Enter:
	  ordered &= phase == 3;
	  phase = 4;
	  return;
	case zdbtest::Trace::Return:
	  ordered &= phase == 5;
	  phase = 6;
	  return;
      }
    };
  }
  db->sagas(second);
  bool started = db->start();
  if (started) {
    active.wait();
  }
  ZuCheck(started);
  unsigned expiredRuns = cut == 0 || cut == 1 ? 1U :
    cut == 7 || cut == 6 ? 2U : 0U;
  ZuCheck(second->runs == (expired ? expiredRuns : 3U));
  ZuCheck(!read || second->reads == second->runs);
  ZuCheck(!second->errors);
  ZuCheck(second->inserts == unsigned(!expired && beforeInsert));
  ZuCheck(second->updates == (expired ? unsigned(expiredRuns == 2) :
    unsigned(cut < 2 || beforeInsert)));
  ZuCheck(second->deletes == unsigned(!expired && !afterDelete));
  ZuCheck(!trace || (ordered && phase == 7));
  store->findFn({});
  store->writeFn({});

  bool retained = false;
  if (started)
    ZmBlock<>{}([orders = orders.ptr(), &retained, afterDelete](auto wake) {
      orders->run(0, [orders, &retained, afterDelete,
	  wake = ZuMv(wake)]() mutable {
	orders->find<0>(0, ZuFwdTuple("IBM", UINT64_C(43)),
	  [&retained, afterDelete,
	    wake = ZuMv(wake)](ZdbRowRef<zdbtest::Order> o) mutable {
	    retained = o && (!afterDelete || o->data().seqNo == 99);
	    wake();
	  });
      });
    });
  ZuCheck(!afterDelete ? !retained : retained);
  ZuCheck(!expired || !second->runs ||
    (*second->deadline && second->deadline <= Zm::now()));

  bool stopped = !started || db->stop();
  ZuCheck(stopped);
  orders = {};
  db->final();
  db = {};
  store = {};
  ZuCheck(mx.stop());
}

static bool intent(
    ZdbTable<Zdb_::SagaStep> *table, ZdbSagaID id, uint32_t step, Zdb_::UN un)
{
  using Step = Zdb_::SagaStep;
  bool ok = false;
  ZdbRowRef<Step> row = new ZdbRow<Step>{table, 0};
  table->insert(row, [id, step, un, &ok](ZdbRow<Step> *row) {
    if (!row) return;
    new (row->ptr()) Step{
      .type = "liveSaga", .id = id, .step = step, .shard = 0, .un = un};
    ok = bool(row->commit());
  });
  return ok;
}

static void recoveryQueue(bool read = false)
{
  ZuTestScopeRT(recoveryQueue);
  auto config = cf();
  ZiMultiplex mx{ZvMxParams{"mx", config->resolve("mx")}};
  ZuCheckRT(mx.start());
  ZmRef<zdbtest::Store> store = new zdbtest::Store{};
  store->preserve();
  ZmRef<ZdbSagaDB<zdbtest::LiveContext, zdbtest::LiveSagas>> db =
    new ZdbSagaDB<zdbtest::LiveContext, zdbtest::LiveSagas>{};
  db->init(ZdbCf{config->resolve("zdb")}, &mx, {}, store);
  auto orders = db->initTable<zdbtest::Order>("o");
  ZmSemaphore paused;
  ZmRef<zdbtest::LiveContext> first = new zdbtest::LiveContext{};
  first->orders = orders;
  first->paused = &paused;
  first->pauseStep = 0;
  first->pauseAll = true;
  db->sagas(first);
  ZuCheckRT(db->start());
  for (unsigned i = 0; i < 2; ++i) {
    ZmRef<ZdbMSaga<zdbtest::LiveSagas>> saga =
      new ZdbMSaga<zdbtest::LiveSagas>{};
    saga->init(zdbtest::LiveSaga{{}, 43 + i});
    ZuCheckRT(db->saga(0, i + 1, ZuMv(saga),
      [](bool) { }, [](bool) { }));
    paused.wait();
  }
  bool seeded = ZmBlock<bool>{}([db = db.ptr(), orders = orders.ptr()](auto wake) {
    db->run([db, orders, wake = ZuMv(wake)]() mutable {
      auto table = static_cast<ZdbTable<Zdb_::SagaStep> *>(db->table("saga_step").ptr());
      orders->run(0, [table, orders, wake = ZuMv(wake)]() mutable {
	bool ok = true;
	for (unsigned i = 0; i < 2; ++i) {
	  ZdbRowRef<zdbtest::Order> row = new ZdbRow<zdbtest::Order>{orders, 0};
	  orders->insert(row, [i, &ok](ZdbRow<zdbtest::Order> *row) {
	    if (!row) { ok = false; return; }
	    new (row->ptr()) zdbtest::Order{
	      "IBM", 43 + i, i ? "FIXB" : "FIXA", "queue", 0,
	      zdbtest::Side::Buy, {100}, {1}};
	    ok &= bool(row->commit());
	  });
	  ok &= intent(table, i + 1, 0, i);
	  // A sorts first but needs B's UN 2 update before its own UN 3 update.
	  ok &= intent(table, i + 1, 1, i ? 2 : 3);
	}
	wake(ok);
      });
    });
  });
  ZuCheckRT(seeded);
  db->run([db = db.ptr(), first = first.ptr()]() {
    db->fail();
    first->release();
  });
  ZuCheckRT(db->stop());
  orders = {};
  db->final();

  db->init(ZdbCf{config->resolve("zdb")}, &mx, {}, store);
  orders = db->initTable<zdbtest::Order>("o");
  ZmRef<zdbtest::LiveContext> second = new zdbtest::LiveContext{};
  second->orders = orders;
  second->read = read;
  db->sagas(second);
  bool started = db->start();
  ZuCheckRT(started);
  // A's update and B's fresh delete each defer once; the latter cannot take
  // A's reserved UN. All other body entries execute/skip exactly once.
  ZuCheckRT(second->runs == 8);
  ZuCheckRT(!read || second->reads == second->runs);
  ZuCheckRT(!second->errors);
  ZuCheckRT(second->inserts == 0);
  ZuCheckRT(second->updates == 2 && second->deletes == 2);
  ZuCheckRT(second->completed == 0);
  bool clean = ZmBlock<bool>{}([db = db.ptr(), orders = orders.ptr()](auto wake) {
    db->run([db, orders, wake = ZuMv(wake)]() mutable {
      wake(orders->nextUN(0) == 6 && orders->count() == 0 &&
	db->table("saga")->count() == 0 && db->table("saga_step")->count() == 0);
    });
  });
  ZuCheckRT(clean);
  ZuCheckRT(!started || db->stop());
  orders = {};
  db->final();
  db = {};
  store = {};
  ZuCheckRT(mx.stop());
}

// 0: no-progress NotReady; 1: missing update row; 2: bad step; 3: bad no-op.
static void recoveryError(unsigned mode)
{
  ZuTestScopeRT(recoveryError);
  auto config = cf();
  ZiMultiplex mx{ZvMxParams{"mx", config->resolve("mx")}};
  ZuCheckRT(mx.start());
  ZmRef<zdbtest::Store> store = new zdbtest::Store{};
  store->preserve();
  ZmRef<ZdbSagaDB<zdbtest::LiveContext, zdbtest::LiveSagas>> db =
    new ZdbSagaDB<zdbtest::LiveContext, zdbtest::LiveSagas>{};
  db->init(ZdbCf{config->resolve("zdb")}, &mx, {}, store);
  auto orders = db->initTable<zdbtest::Order>("o");
  ZmSemaphore paused;
  ZmRef<zdbtest::LiveContext> first = new zdbtest::LiveContext{};
  first->orders = orders;
  first->paused = &paused;
  first->pauseStep = 0;
  db->sagas(first);
  ZuCheckRT(db->start());
  ZmRef<ZdbMSaga<zdbtest::LiveSagas>> saga =
    new ZdbMSaga<zdbtest::LiveSagas>{};
  saga->init(zdbtest::LiveSaga{{}, 43});
  ZuCheckRT(db->saga(0, 2, ZuMv(saga),
    [](bool) { }, [](bool) { }));
  paused.wait();
  bool seeded = ZmBlock<bool>{}([db = db.ptr(), orders = orders.ptr(), mode](auto wake) {
    db->run([db, orders, mode, wake = ZuMv(wake)]() mutable {
      if (mode >= 6) {
	using Data = Zdb_::SagaData;
	auto table = static_cast<ZdbTable<Data> *>(db->table("saga").ptr());
	table->run(0, [
	  table, mode, invalidShard = ZdbShard(db->nShards()), wake = ZuMv(wake)
	]() mutable {
	  table->findDel<0>(0, ZuFwdTuple("liveSaga", ZdbSagaID{2}),
	    [table, mode, invalidShard, wake = ZuMv(wake)](ZdbRow<Data> *dbRow) mutable {
	      if (!dbRow) { wake(false); return; }
	      Data data = dbRow->data();
	      if (!dbRow->commit()) { wake(false); return; }
	      switch (mode) {
		case 6: data.type = "unknownSaga"; break;
		case 7: data.shard = invalidShard; break;
		case 8: data.data.length(0); break;
	      }
	      ZdbRowRef<Data> replacement = new ZdbRow<Data>{table, 0};
	      table->insert(ZuMv(replacement),
		[data = ZuMv(data), wake = ZuMv(wake)](ZdbRow<Data> *row) mutable {
		  if (!row) { wake(false); return; }
		  new (row->ptr()) Data{ZuMv(data)};
		  wake(bool(row->commit()));
		});
	    });
	});
	return;
      }
      using Step = Zdb_::SagaStep;
      auto table = static_cast<ZdbTable<Step> *>(db->table("saga_step").ptr());
      table->run(0, [table, orders, mode, wake = ZuMv(wake)]() mutable {
	bool ok = true;
	if (mode == 1) {
	  // Insert and delete advance the target stream to UN 2, but leave no
	  // row for an update at exactly that UN. Step zero replays as skipped.
	  ZdbRowRef<zdbtest::Order> row = new ZdbRow<zdbtest::Order>{orders, 0};
	  orders->insert(row, [&ok](ZdbRow<zdbtest::Order> *row) {
	    if (!row) { ok = false; return; }
	    new (row->ptr()) zdbtest::Order{
	      "IBM", 43, "FIX0", "missing", 0, zdbtest::Side::Buy, {100}, {1}};
	    ok &= bool(row->commit());
	  });
	  orders->del(row, [&ok](ZdbRow<zdbtest::Order> *row) {
	    ok &= row && row->commit();
	  });
	  ok &= intent(table, 2, 0, 0);
	  ok &= intent(table, 2, 1, 2);
	} else if (mode == 4) {
	  // Distinct logical steps cannot reserve the same target stream position.
	  ok &= intent(table, 2, 0, 0);
	  ok &= intent(table, 2, 1, 0);
	} else if (mode == 5) {
	  // A null UN is reserved for an absent-delete marker, never an update.
	  ok &= intent(table, 2, 1, Zdb_::nullUN());
	} else {
	  ok &= intent(table, 2, mode == 2 ? 99 : 0, mode == 3 ? Zdb_::nullUN() : 1);
	}
	wake(ok);
      });
    });
  });
  ZuCheckRT(seeded);
  db->run([db = db.ptr(), first = first.ptr()]() {
    db->fail();
    first->release();
  });
  ZuCheckRT(db->stop());
  orders = {};
  db->final();

  activations = 0;
  db->init(ZdbCf{config->resolve("zdb")}, &mx, ZdbHandler{
    .upFn = [](Zdb *, ZdbHost *) { ++activations; }
  }, store);
  orders = db->initTable<zdbtest::Order>("o");
  ZmRef<zdbtest::LiveContext> second = new zdbtest::LiveContext{};
  second->orders = orders;
  db->sagas(second);
  bool started = db->start();
  ZuCheckRT(!started);
  ZuCheckRT(activations == 0);
  ZuCheckRT(second->runs == (mode < 2 ? 2U : 0U));
  ZuCheckRT(second->inserts == 0 && second->updates == 0 && second->deletes == 0);
  ZuCheckRT(second->completed == 0 && second->errors == 0);
  ZuCheckRT(!started || db->stop());
  orders = {};
  db->final();
  db = {};
  store = {};
  ZuCheckRT(mx.stop());
}

static bool catalogWrite(const Zdb_::OpenData &data, unsigned mode)
{
  using namespace Zdb_;
  // Persist real catalog corruption, rather than only testing its decoder.
  bool partial = mode == 1;
  SagaTypeStep row{
    "liveSaga", partial ? "o" : "order", partial ? 2U : 1U,
    SagaOp::T(partial ? SagaOp::Delete : SagaOp::Update)};
  // Catalog columns are immutable: replace a row with delete/insert.
  for (unsigned i = 0, n = partial ? 1 : 2; i < n; ++i) {
    Zfb::IOBuilder fbb{Table<SagaTypeStep>::allocBuf()};
    auto payload = Zfb::Save::nest(fbb, [&row](Zfb::Builder &fbb) {
      return ZfbStruct::save(fbb, row).Union();
    });
    auto sn = ZfbTransform::UInt128::save(data.sn + i + 1);
    fbb.Finish(fbs::CreateMsg(fbb, fbs::Body::Replication,
      fbs::CreateRecord(fbb, Zfb::Save::str(fbb, "saga_type"),
	data.un[0] + i + 1, &sn, i ? 0 : -1, 0, payload).Union()));
    if (!ZmBlock<bool>{}([
      table = data.storeTbl, buf = saveHdr(fbb)
    ](auto wake) mutable {
      table->write(ZuMv(buf), [wake = ZuMv(wake)](
	  ZmRef<IOBuf>, CommitResult result) mutable {
	wake(result.is<void>());
      });
    })) return false;
  }
  return true;
}

template <typename Context, typename Sagas>
static bool catalogStart(
    const ZfCf::AnyNode *config, ZiMultiplex *mx, zdbtest::Store *store,
    uint64_t expectedCount, unsigned corrupt = 0)
{
  using namespace Zdb_;
  ZmRef<ZdbSagaDB<Context, Sagas>> db = new ZdbSagaDB<Context, Sagas>{};
  db->init(ZdbCf{config->resolve("zdb")}, mx, {}, store);
  auto orders = db->template initTable<zdbtest::Order>("o");
  auto otherOrders = db->template initTable<zdbtest::Order>("order");
  auto payments = db->template initTable<zdbtest::Order>("payment");
  db->sagas({});
  bool started = db->start();
  bool reconciled = false;
  if (started) {
    // Successful reconciliation must release the direct store handle.
    OpenResult result = ZmBlock<OpenResult>{}([store](auto wake) {
      store->open(true, "saga_type",
	ZfVFields<SagaTypeStep>(), ZfVKeyFields<SagaTypeStep>(),
	reflection::GetSchema(ZfbSchema<SagaTypeStep>::data()),
	Table<SagaTypeStep>::allocBuf,
	[wake = ZuMv(wake)](OpenResult result) mutable { wake(ZuMv(result)); });
    });
    if (result.is<OpenData>()) {
      const auto &data = result.p<OpenData>();
      reconciled = data.count == expectedCount;
      if (corrupt) reconciled &= catalogWrite(data, corrupt);
      ZmBlock<>{}([table = data.storeTbl](auto wake) {
	table->close([wake = ZuMv(wake)]() mutable { wake(); });
      });
    }
  }
  bool stopped = !started || db->stop();
  orders = {}; otherOrders = {}; payments = {};
  db->final();
  // Zero denotes an expected fatal startup, not an empty valid saga bundle.
  return started == bool(expectedCount) && stopped && (!started || reconciled);
}

static void catalogStartup()
{
  ZuTestScope(catalogStartup);
  auto config = cf();
  ZiMultiplex mx{ZvMxParams{"mx", config->resolve("mx")}};
  ZuCheck(mx.start());
  ZmRef<zdbtest::Store> store = new zdbtest::Store{};
  store->preserve();
  ZuCheck((catalogStart<zdbtest::LiveContext, zdbtest::LiveSagas>(
    config, &mx, store, 3)));
  ZuCheck((catalogStart<zdbtest::LiveContext,
    zdbtest::ShortCatalog>(config, &mx, store, 0)));
  ZuCheck((catalogStart<zdbtest::LiveContext,
    zdbtest::ChangedCatalog>(config, &mx, store, 0)));
  // All handles must have closed; neither failed build may alter the catalog.
  ZuCheck((catalogStart<zdbtest::LiveContext, zdbtest::LiveSagas>(
    config, &mx, store, 3)));
  // Whole new types are appended; types absent from this build are historical.
  ZuCheck((catalogStart<zdbtest::Context, zdbtest::Sagas>(
    config, &mx, store, 7)));
  ZuCheck((catalogStart<zdbtest::LiveContext, zdbtest::LiveSagas>(
    config, &mx, store, 7)));
  ZuCheck((catalogStart<zdbtest::Context, zdbtest::Sagas>(
    config, &mx, store, 7)));
  store = {};
  ZuCheck(mx.stop());
}

static void catalogCorrupt(unsigned mode)
{
  ZuTestScope(catalogCorrupt);
  auto config = cf();
  ZiMultiplex mx{ZvMxParams{"mx", config->resolve("mx")}};
  ZuCheck(mx.start());
  ZmRef<zdbtest::Store> store = new zdbtest::Store{};
  store->preserve();
  ZuCheck((catalogStart<zdbtest::LiveContext, zdbtest::LiveSagas>(
    config, &mx, store, 3, mode)));
  ZuCheck((catalogStart<zdbtest::LiveContext, zdbtest::LiveSagas>(
    config, &mx, store, 0)));
  // Failure releases its handles and must not silently repair the definition.
  ZuCheck((catalogStart<zdbtest::LiveContext, zdbtest::LiveSagas>(
    config, &mx, store, 0)));
  store = {};
  ZuCheck(mx.stop());
}

static void findFail()
{
  ZuTestScope(findFail);
  using DB = ZdbSagaDB<zdbtest::LiveContext, zdbtest::LiveSagas>;
  using M = ZdbMSaga<zdbtest::LiveSagas>;
  auto config = cf();
  ZiMultiplex mx{ZvMxParams{"mx", config->resolve("mx")}};
  ZuCheck(mx.start());
  ZmRef<zdbtest::Store> store = new zdbtest::Store{};
  store->preserve();
  ZmRef<DB> db = new DB{};
  db->init(ZdbCf{config->resolve("zdb")}, &mx, {}, store);
  auto orders = db->initTable<zdbtest::Order>("o");
  ZmSemaphore paused;
  ZmRef<zdbtest::LiveContext> first = new zdbtest::LiveContext{};
  first->orders = orders;
  first->paused = &paused;
  first->pauseStep = 1;
  db->sagas(first);
  ZuCheck(db->start());
  ZmRef<M> saga = new M{};
  saga->init(zdbtest::LiveSaga{{}, 61});
  ZuCheck(db->saga(0, 61, ZuMv(saga),
    [](bool) { }, [](bool) { }));
  paused.wait();
  db->run([db = db.ptr(), first = first.ptr()]() {
    db->fail();
    first->release();
  });
  ZuCheck(db->stop());
  orders = {};
  db->final();

  activations = 0;
  db->init(ZdbCf{config->resolve("zdb")}, &mx, {
    .upFn = [](Zdb *, ZdbHost *) { ++activations; }
  }, store);
  orders = db->initTable<zdbtest::Order>("o");
  ZmRef<zdbtest::LiveContext> next = new zdbtest::LiveContext{};
  next->orders = orders;
  db->sagas(next);
  store->findResultFn([](ZuCSpan id, Zdb_::RowResult &result) {
    if (id == "o")
      result = ZeEXCEPT(Fatal, "ZdbTest", "injected saga lookup failure");
  });
  ZuCheck(!db->start()); // failed lookup must retire before failed-start finishes
  ZuCheck(activations == 0);
  ZuCheck(next->runs == 2);
  ZuCheck(next->updates == 0 && next->deletes == 0 && next->completed == 0);

  store->findResultFn({});
  ZuCheck(db->start());
  ZuCheck(activations == 1);
  ZuCheck(next->runs == 5);
  ZuCheck(next->inserts == 0 && next->updates == 1 && next->deletes == 1);
  ZuCheck(next->completed == 0);
  ZuCheck(db->stop());
  orders = {};
  db->final();
  db = {};
  store = {};
  ZuCheck(mx.stop());
}

static void scanError(bool steps)
{
  ZuTestScopeRT(scanError);
  auto config = cf();
  ZiMultiplex mx{ZvMxParams{"mx", config->resolve("mx")}};
  ZuCheckRT(mx.start());
  ZmRef<zdbtest::Store> store = new zdbtest::Store{};
  store->preserve();
  ZmRef<ZdbSagaDB<zdbtest::LiveContext, zdbtest::LiveSagas>> db =
    new ZdbSagaDB<zdbtest::LiveContext, zdbtest::LiveSagas>{};
  activations = 0;
  db->init(ZdbCf{config->resolve("zdb")}, &mx, {
    .upFn = [](Zdb *, ZdbHost *) { ++activations; }
  }, store);
  auto orders = db->initTable<zdbtest::Order>("o");
  db->sagas({});
  store->selectResultFn([steps](ZuCSpan id, Zdb_::TupleResult &result) {
    if (id == (steps ? "saga_step" : "saga") && result.is<void>())
      result = ZeEXCEPT(Fatal, "ZdbTest", "injected saga scan failure");
  });
  bool started = db->start();
  ZuCheckRT(!started);
  ZuCheckRT(activations == 0);
  ZuCheckRT(!started || db->stop());
  store->selectResultFn({});
  ZuCheckRT(db->start());
  ZuCheckRT(activations == 1);
  ZuCheckRT(db->stop());
  orders = {};
  db->final();
  db = {};
  store = {};
  ZuCheckRT(mx.stop());
}

static void scanFail(bool steps, bool stopPending)
{
  ZuTestScopeRT(scanFail);
  auto config = cf();
  ZiMultiplex mx{ZvMxParams{"mx", config->resolve("mx")}};
  ZuCheckRT(mx.start());
  ZmRef<zdbtest::Store> store = new zdbtest::Store{};
  store->preserve();
  ZmRef<ZdbSagaDB<zdbtest::LiveContext, zdbtest::LiveSagas>> db =
    new ZdbSagaDB<zdbtest::LiveContext, zdbtest::LiveSagas>{};
  activations = 0;
  db->init(ZdbCf{config->resolve("zdb")}, &mx, ZdbHandler{
    .upFn = [](Zdb *, ZdbHost *) { ++activations; }
  }, store);
  auto orders = db->initTable<zdbtest::Order>("o");
  db->sagas({});
  ZmSemaphore selected;
  store->selectFn([store = store.ptr(), steps, &selected](ZuCSpan id) {
    if (id != (steps ? "saga_step" : "saga")) return;
    store->deferCallbacks(true);
    selected.post();
  });
  ZmSemaphore started;
  ZmSemaphore stopped;
  bool startOK = true;
  bool stopOK = false;
  bool finished = false;
  db->start([&startOK, &finished, &started](bool ok) {
    startOK = ok;
    finished = true;
    started.post();
  });
  selected.wait();
  ZmBlock<>{}([db = db.ptr(), stopPending, &stopOK, &stopped](auto wake) {
    db->run([db, stopPending, &stopOK, &stopped, wake = ZuMv(wake)]() mutable {
      db->fail();
      db->fail(); // duplicate failure must not replace failed-start completion
      if (stopPending) db->stop([&stopOK, &stopped](bool ok) {
	stopOK = ok;
	stopped.post();
      });
      wake();
    });
  });
  store->sync(); // the selected page's EOR is now in the deferred queue
  bool waiting = ZmBlock<bool>{}([db = db.ptr(), &finished](auto wake) {
    db->run([&finished, wake = ZuMv(wake)]() mutable { wake(!finished); });
  });
  ZuCheckRT(waiting);
  store->deferCallbacks(false);
  store->performCallbacks();
  started.wait();
  if (stopPending) stopped.wait();
  ZuCheckRT(!startOK);
  ZuCheckRT(!stopPending || stopOK);
  ZuCheckRT(activations == 0);

  store->selectFn({});
  bool restarted = db->start();
  ZuCheckRT(restarted);
  ZuCheckRT(activations == 1);
  ZuCheckRT(!restarted || db->stop());
  orders = {};
  db->final();
  db = {};
  store = {};
  ZuCheckRT(mx.stop());
}

static void admission()
{
  ZuTestScope(admission);
  using DB = ZdbSagaDB<zdbtest::LiveContext, zdbtest::LiveSagas>;
  using M = ZdbMSaga<zdbtest::LiveSagas>;
  auto config = cf();
  ZiMultiplex mx{ZvMxParams{"mx", config->resolve("mx")}};
  ZuCheck(mx.start());
  ZmRef<zdbtest::Store> store = new zdbtest::Store{};
  ZmRef<DB> db = new DB{};
  db->init(ZdbCf{config->resolve("zdb")}, &mx, {}, store);
  auto orders = db->initTable<zdbtest::Order>("o");
  auto make = []() {
    ZmRef<M> saga = new M{};
    saga->init(zdbtest::LiveSaga{{}, 43});
    return saga;
  };
  ZmSemaphore paused;
  ZmSemaphore done;
  ZmRef<zdbtest::LiveContext> context = new zdbtest::LiveContext{};
  context->orders = orders;
  context->done = &done;
  context->paused = &paused;
  context->pauseStep = 0;
  auto submit = [db = db.ptr(), context = context.ptr()](
      ZdbShard shard, ZmRef<M> saga, ZuTime deadline = {}) {
    return ZmBlock<bool>{}([
      db, context, shard, saga = ZuMv(saga), deadline
    ](auto wake) mutable {
      db->saga(shard, 1, ZuMv(saga),
	[wake = ZuMv(wake)](bool ok) mutable { wake(ok); },
	[context](bool ok) {
	  if (!ok) {
	    ++context->errors;
	    context->done->post();
	    return;
	  }
	  if (++context->completed == 1) {
	    ZmRef<M> saga = new M{};
	    saga->init(zdbtest::LiveSaga{{}, 43});
	    auto db = static_cast<DB *>(context->orders->db());
	    db->saga(0, 1, ZuMv(saga),
	      [context](bool ok) {
		if (!ok) { ++context->errors; context->done->post(); }
	      },
	      [context](bool ok) {
		if (!ok) ++context->errors;
		++context->completed;
		context->done->post();
	      });
	  }
	}, deadline);
    });
  };
  ZuCheck(!submit(0, make())); // registration is optional, but required here
  db->sagas(context);
  ZuCheck(!submit(0, make())); // before start
  ZuCheck(db->start());
  ZuCheck(!submit(0, {}));
  ZuCheck(!submit(0, ZmRef<M>{new M{}}));
  ZuCheck(!submit(1, make())); // this DB has one shard
  ZuCheck(submit(0, make()));
  paused.wait();
  ZuCheck(context->errors == 0);
  db->run([context = context.ptr()]() { context->resume()(true); });
  done.wait();
  ZuCheck(context->completed == 2 && context->errors == 0);
  ZuCheck(context->runs == 7); // one paused entry plus two complete executions
  ZuCheck(context->inserts == 2 && context->updates == 2 && context->deletes == 2);
  ZuCheck(db->stop());
  orders = {};
  db->final();
  db = {};
  store = {};
  ZuCheck(mx.stop());
}

static void payloadAdmission(unsigned size)
{
  ZuTestScopeRT(payloadAdmission);
  struct Sagas {
    using List = ZuTypeList<zdbtest::PayloadSaga>;
    static int match(ZuCSpan type) {
      return zdbtest::sagaMatch<Sagas>(type);
    }
  };
  using DB = ZdbSagaDB<zdbtest::PayloadContext, Sagas>;
  using M = ZdbMSaga<Sagas>;
  auto config = cf(true);
  ZiMultiplex mx{ZvMxParams{"mx", config->resolve("mx")}};
  ZuCheckRT(mx.start());
  ZmRef<zdbtest::Store> store = new zdbtest::Store{};
  ZmRef<DB> db = new DB{};
  db->init(ZdbCf{config->resolve("zdb")}, &mx, {}, store);
  auto orders = db->initTable<zdbtest::Order>("o");
  ZmRef<zdbtest::PayloadContext> context = new zdbtest::PayloadContext{};
  db->sagas(context);
  ZuCheckRT(db->start());
  Zdb_::SagaPayload data;
  data.length(size, false);
  for (unsigned i = 0; i < size; ++i) data[i] = uint8_t(i);
  ZmRef<M> saga = new M{};
  saga->init(zdbtest::PayloadSaga{{}, ZuMv(data)});
  Zdb_::SagaPayload saved;
  M::save(saga, saved);
  bool admitted = ZmBlock<bool>{}([
    db = db.ptr(), saga, context = context.ptr()
  ](auto wake) mutable {
    db->saga(0, 1, ZuMv(saga),
      [wake = ZuMv(wake)](bool ok) mutable { wake(ok); },
      [context](bool) { context->entered.post(); });
  });
  ZuCheckRT(admitted);
  if (admitted) {
    context->entered.wait();
    store->sync();
    bool stored = ZmBlock<bool>{}([db = db.ptr(), &saved](auto wake) {
      db->run([db, &saved, wake = ZuMv(wake)]() mutable {
	auto table = static_cast<ZdbTable<Zdb_::SagaData> *>(
	  db->table("saga").ptr());
	table->selectRows<0>({}, 1, [
	  &saved, wake = ZuMv(wake), matched = false
	](auto result, unsigned) mutable {
	  using Tuple = ZdbTable<Zdb_::SagaData>::Tuple;
	  if (result.template is<Tuple>()) {
	    const auto &row = result.template p<Tuple>();
	    matched = row.template p<0>() == zdbtest::PayloadSaga::Type{}() &&
	      row.template p<1>() == 1 && row.template p<3>() == saved;
	  } else
	    wake(matched);
	});
      });
    });
    ZuCheckRT(stored);
  }
  // This fixture intentionally leaves the application body suspended.
  context->complete(false);
  ZuCheckRT(db->stop());
  saga = {};
  orders = {};
  db->final();
  db = {};
  store = {};
  ZuCheckRT(mx.stop());
}

static void admissionDeactivated(bool read = false)
{
  ZuTestScope(admissionDeactivated);
  auto config = cf();
  ZiMultiplex mx{ZvMxParams{"mx", config->resolve("mx")}};
  ZuCheck(mx.start());
  ZmRef<zdbtest::Store> store = new zdbtest::Store{};
  store->preserve();
  ZmRef<ZdbSagaDB<zdbtest::LiveContext, zdbtest::LiveSagas>> db =
    new ZdbSagaDB<zdbtest::LiveContext, zdbtest::LiveSagas>{};
  db->init(ZdbCf{config->resolve("zdb")}, &mx, {}, store);
  auto orders = db->initTable<zdbtest::Order>("o");
  ZmRef<zdbtest::LiveContext> context = new zdbtest::LiveContext{};
  context->orders = orders;
  context->read = read;
  ZmSemaphore readFailed;
  if (read)
    context->trace = [db = db.ptr(), &readFailed](unsigned event, unsigned) {
      if (event != zdbtest::Trace::Read) return;
      db->run([db]() { db->fail(); });
      readFailed.post();
    };
  db->sagas(context);
  ZuCheck(db->start());

  ZmSemaphore submitted;
  bool queued = false;
  bool rejected = false;
  db->run([db = db.ptr(), read, &queued, &rejected, &submitted]() {
    ZmRef<ZdbMSaga<zdbtest::LiveSagas>> saga =
      new ZdbMSaga<zdbtest::LiveSagas>{};
    saga->init(zdbtest::LiveSaga{{}, 61});
    queued = db->saga(0, 61, ZuMv(saga),
      [&rejected, &submitted](bool ok) {
	rejected = !ok;
	submitted.post();
      }, [](bool) { });
    // Admission runs first and posts the shard commit behind this failure.
    // Its completion must reject the old epoch, without deleting the intent.
    if (!read) db->run([db]() { db->fail(); });
  });
  submitted.wait();
  if (read) readFailed.wait();
  ZuCheck(queued);
  ZuCheck(rejected == !read);
  ZuCheck(context->runs == unsigned(read));
  ZuCheck(context->reads == unsigned(read));
  ZuCheck(context->inserts <= 1);
  ZuCheck(db->stop());
  orders = {};
  db->final();

  db->init(ZdbCf{config->resolve("zdb")}, &mx, {}, store);
  orders = db->initTable<zdbtest::Order>("o");
  ZmRef<zdbtest::LiveContext> next = new zdbtest::LiveContext{};
  next->orders = orders;
  next->read = read;
  db->sagas(next);
  ZuCheck(db->start());
  ZuCheck(next->runs == 3);
  ZuCheck(!read || next->reads == next->runs);
  ZuCheck(next->inserts + context->inserts == 1);
  ZuCheck(next->updates == 1);
  ZuCheck(next->deletes == 1);
  ZuCheck(db->stop());
  orders = {};
  db->final();
  db = {};
  store = {};
  ZuCheck(mx.stop());
}

static void gracefulStop(bool failed)
{
  ZuTestScope(gracefulStop);
  auto config = cf();
  ZiMultiplex mx{ZvMxParams{"mx", config->resolve("mx")}};
  ZuCheck(mx.start());
  ZmRef<zdbtest::Store> store = new zdbtest::Store{};
  store->preserve();
  ZmRef<ZdbSagaDB<zdbtest::LiveContext, zdbtest::LiveSagas>> db =
    new ZdbSagaDB<zdbtest::LiveContext, zdbtest::LiveSagas>{};
  ZmSemaphore active;
  ZmSemaphore paused;
  active_ = &active;
  db->init(ZdbCf{config->resolve("zdb")}, &mx, ZdbHandler{
    .upFn = up
  }, store);
  auto orders = db->initTable<zdbtest::Order>("o");
  ZmRef<zdbtest::LiveContext> context = new zdbtest::LiveContext{};
  context->orders = orders;
  context->paused = &paused;
  context->pauseStep = 1;
  db->sagas(context);
  ZuCheck(db->start());
  active.wait();
  ZmRef<ZdbMSaga<zdbtest::LiveSagas>> saga =
    new ZdbMSaga<zdbtest::LiveSagas>{};
  saga->init(zdbtest::LiveSaga{{}, 51});
  ZuCheck(db->saga(0, 51, ZuMv(saga), [](bool) { },
    [context = context.ptr()](bool ok) {
      if (ok) ++context->completed;
    }));
  paused.wait();

  ZmSemaphore stopped;
  bool stopOK = false;
  db->stop([context = context.ptr(), &stopOK, &stopped, failed](bool ok) {
    stopOK = ok && context->completed == !failed && context->deletes == !failed;
    stopped.post();
  });
  bool draining = false;
  ZmBlock<>{}([db = db.ptr(), &draining](auto wake) {
    db->run([db, &draining, wake = ZuMv(wake)]() mutable {
      draining = db->active() && db->Engine::stopping();
      wake();
    });
  });
  ZuCheck(draining);

  saga = new ZdbMSaga<zdbtest::LiveSagas>{};
  saga->init(zdbtest::LiveSaga{{}, 52});
  bool rejected = false;
  ZmBlock<>{}([db = db.ptr(), saga = ZuMv(saga), &rejected](auto wake) mutable {
    db->saga(0, 52, ZuMv(saga),
      [&rejected, wake = ZuMv(wake)](bool ok) mutable {
	rejected = !ok;
	wake();
      }, [](bool) { });
  });
  ZuCheck(rejected);
  db->run([db = db.ptr(), context = context.ptr(), failed]() {
    // Queue failure before resumed shard work so the retained completion is
    // stale when that work drains.
    if (failed) db->run([db]() { db->fail(); });
    context->resume()(true);
  });
  stopped.wait();
  ZuCheck(stopOK);
  ZuCheck(context->runs == (failed ? 3 : 4));
  ZuCheck(context->updates == 1);
  orders = {};
  db->final();

  db->init(ZdbCf{config->resolve("zdb")}, &mx, ZdbHandler{
    .upFn = up
  }, store);
  orders = db->initTable<zdbtest::Order>("o");
  ZmRef<zdbtest::LiveContext> next = new zdbtest::LiveContext{};
  next->orders = orders;
  db->sagas(next);
  bool started = db->start();
  ZuCheck(started);
  if (started) active.wait();
  ZuCheck(next->runs == (failed ? 3 : 0));
  ZuCheck(next->inserts == 0);
  ZuCheck(next->updates == 0);
  ZuCheck(next->deletes == unsigned(failed));
  ZuCheck(!started || db->stop());
  orders = {};
  db->final();
  db = {};
  store = {};
  ZuCheck(mx.stop());
}

// Fail after deleting the main row (0), or after each step deletion (1..3).
static void recoveryCleanup(unsigned cut)
{
  ZuTestScope(recoveryCleanup);
  auto config = cf();
  ZiMultiplex mx{ZvMxParams{"mx", config->resolve("mx")}};
  ZuCheck(mx.start());
  ZmRef<zdbtest::Store> store = new zdbtest::Store{};
  store->preserve();
  ZmRef<ZdbSagaDB<zdbtest::LiveContext, zdbtest::LiveSagas>> db =
    new ZdbSagaDB<zdbtest::LiveContext, zdbtest::LiveSagas>{};
  ZmSemaphore active;
  ZmSemaphore inactive;
  ZmSemaphore paused;
  active_ = &active;
  down_ = &inactive;
  db->init(ZdbCf{config->resolve("zdb")}, &mx, ZdbHandler{
    .upFn = up, .downFn = down
  }, store);
  auto orders = db->initTable<zdbtest::Order>("o");
  ZmRef<zdbtest::LiveContext> first = new zdbtest::LiveContext{};
  first->orders = orders;
  first->paused = &paused;
  first->pauseStep = 3;
  db->sagas(first);
  ZuCheck(db->start());
  active.wait();
  ZmRef<ZdbMSaga<zdbtest::LiveSagas>> saga =
    new ZdbMSaga<zdbtest::LiveSagas>{};
  saga->init(zdbtest::LiveSaga{{}, 43});
  ZuCheck(db->saga(0, 2, ZuMv(saga),
    [](bool) { }, [](bool) { }));
  paused.wait();

  // The hook runs in the committing shard turn. Its posted failure precedes
  // the cleanup continuation, but the current commit still reaches the store.
  unsigned writes = 0;
  store->writeFn([db = db.ptr(), cut, &writes](ZuCSpan id) {
    if (id != "saga" && id != "saga_step") return;
    if (writes++ == cut) db->run([db]() { db->fail(); });
  });
  db->run([first = first.ptr()]() { first->resume()(true); });
  inactive.wait();
  ZuCheck(db->stop());
  store->writeFn({});
  store->sync();
  ZuCheck(writes == cut + 1);
  ZuCheck(first->inserts == 1);
  ZuCheck(first->updates == 1);
  ZuCheck(first->deletes == 1);
  bool residue = ZmBlock<bool>{}([db = db.ptr(), cut](auto wake) {
    db->run([db, cut, wake = ZuMv(wake)]() mutable {
      wake(db->table("saga")->count() == 0 &&
	db->table("saga_step")->count() == 3 - cut);
    });
  });
  ZuCheck(residue);
  orders = {};
  db->final();

  db->init(ZdbCf{config->resolve("zdb")}, &mx, ZdbHandler{
    .upFn = up
  }, store);
  orders = db->initTable<zdbtest::Order>("o");
  ZmSemaphore completed;
  ZmRef<zdbtest::LiveContext> second = new zdbtest::LiveContext{};
  second->orders = orders;
  second->done = &completed;
  db->sagas(second);
  bool started = db->start();
  ZuCheck(started);
  if (started) {
    active.wait();
    ZuCheck(second->runs == 0);
    bool cleaned = ZmBlock<bool>{}([db = db.ptr()](auto wake) {
      db->run([db, wake = ZuMv(wake)]() mutable {
	wake(db->table("saga")->count() == 0 &&
	  db->table("saga_step")->count() == 0);
      });
    });
    ZuCheck(cleaned);
    saga = new ZdbMSaga<zdbtest::LiveSagas>{};
    saga->init(zdbtest::LiveSaga{{}, 43});
    ZuCheck(db->saga(0, 2, ZuMv(saga), [](bool) { },
      [second = second.ptr()](bool) { second->done->post(); }));
    completed.wait();
    ZuCheck(second->runs == 3);
    ZuCheck(second->inserts == 1);
    ZuCheck(second->updates == 1);
    ZuCheck(second->deletes == 1);
    ZuCheck(db->stop());
  }
  orders = {};
  db->final();
  db = {};
  store = {};
  ZuCheck(mx.stop());
}

static void recoveryPages(bool orphan)
{
  ZuTestScopeRT(recoveryPages);
  enum { N = Zdb_::SagaScanSize + 1 }; // cross both main and step scan pages
  auto config = cf();
  ZiMultiplex mx{ZvMxParams{"mx", config->resolve("mx")}};
  ZuCheckRT(mx.start());
  ZmRef<zdbtest::Store> store = new zdbtest::Store{};
  store->preserve();
  ZmRef<ZdbSagaDB<zdbtest::LiveContext, zdbtest::LiveSagas>> db =
    new ZdbSagaDB<zdbtest::LiveContext, zdbtest::LiveSagas>{};
  ZmSemaphore active;
  ZmSemaphore inactive;
  ZmSemaphore paused;
  active_ = &active;
  down_ = &inactive;
  db->init(ZdbCf{config->resolve("zdb")}, &mx, ZdbHandler{
    .upFn = up, .downFn = down
  }, store);
  auto orders = db->initTable<zdbtest::Order>("o");
  ZmRef<zdbtest::LiveContext> first = new zdbtest::LiveContext{};
  first->orders = orders;
  first->paused = &paused;
  first->pauseStep = 1;
  first->pauseAll = true;
  db->sagas(first);
  ZuCheckRT(db->start());
  active.wait();
  for (unsigned i = 0; i < N; ++i) {
    ZmRef<ZdbMSaga<zdbtest::LiveSagas>> saga =
      new ZdbMSaga<zdbtest::LiveSagas>{};
    saga->init(zdbtest::LiveSaga{{}, i});
    ZuCheckRT(db->saga(0, i, ZuMv(saga),
      [](bool) { }, [](bool) { }));
  }
  for (unsigned i = 0; i < N; ++i) paused.wait();
  if (orphan) {
    bool removed = false;
    ZmBlock<>{}([db = db.ptr(), &removed](auto wake) {
      db->run([db, &removed, wake = ZuMv(wake)]() mutable {
	auto table = static_cast<ZdbTable<Zdb_::SagaData> *>(db->table("saga").ptr());
	table->run(0, [table, &removed, wake = ZuMv(wake)]() mutable {
	  typename ZdbTable<Zdb_::SagaData>::template Key<0> key{
	    "liveSaga", N - 1};
	  table->findDel<0>(0, key,
	    [&removed, wake = ZuMv(wake)](ZdbRow<Zdb_::SagaData> *o) mutable {
	      removed = o && o->commit();
	      wake();
	    });
	});
      });
    });
    ZuCheckRT(removed);
  }
  store->sync();
  ZuCheckRT(first->runs == 2 * N);
  ZuCheckRT(first->inserts == N);
  db->run([db = db.ptr(), first = first.ptr()]() {
    db->fail();
    first->release();
  });
  inactive.wait();
  ZuCheckRT(db->stop());
  orders = {};
  db->final();

  db->init(ZdbCf{config->resolve("zdb")}, &mx, ZdbHandler{
    .upFn = up
  }, store);
  orders = db->initTable<zdbtest::Order>("o");
  ZmRef<zdbtest::LiveContext> second = new zdbtest::LiveContext{};
  second->orders = orders;
  db->sagas(second);
  bool started = db->start();
  ZuCheckRT(started);
  if (started) active.wait();
  unsigned recovered = N - unsigned(orphan);
  ZuCheckRT(second->runs == 3 * recovered);
  ZuCheckRT(second->inserts == 0);
  ZuCheckRT(second->updates == recovered);
  ZuCheckRT(second->deletes == recovered);
  if (started) {
    ZuCheckRT(orders->count() == unsigned(orphan));
    bool cleaned = false;
    ZmBlock<>{}([db = db.ptr(), &cleaned](auto wake) {
      db->run([db, &cleaned, wake = ZuMv(wake)]() mutable {
	cleaned = db->table("saga")->count() == 0 &&
	  db->table("saga_step")->count() == 0;
	wake();
      });
    });
    ZuCheckRT(cleaned);
  }
  ZuCheckRT(!started || db->stop());
  orders = {};
  db->final();
  db = {};
  store = {};
  ZuCheckRT(mx.stop());
}

static void rollback(
    unsigned failStep, bool absentReverse = false, bool failReverse = false,
    uint32_t skipStep = UINT32_MAX)
{
  ZuTestScopeRT(rollback);
  using DB = ZdbSagaDB<zdbtest::LiveContext, zdbtest::LiveSagas>;
  using M = ZdbMSaga<zdbtest::LiveSagas>;
  auto config = cf();
  ZiMultiplex mx{ZvMxParams{"mx", config->resolve("mx")}};
  ZuCheckRT(mx.start());
  ZmRef<zdbtest::Store> store = new zdbtest::Store{};
  ZmRef<DB> db = new DB{};
  ZmSemaphore active;
  active_ = &active;
  db->init(ZdbCf{config->resolve("zdb")}, &mx, ZdbHandler{
    .upFn = up
  }, store);
  auto orders = db->initTable<zdbtest::Order>("o");
  ZmRef<zdbtest::LiveContext> context = new zdbtest::LiveContext{};
  context->orders = orders;
  context->failStep = failStep;
  context->skipStep = skipStep;
  context->absentReverse = absentReverse;
  context->failReverse = failReverse;
  int contextRefs = context->refCount();
  db->sagas(context);
  ZuCheckRT(context->refCount() == contextRefs + 1);
  ZuCheckRT(db->start());
  active.wait();

  bool skipped = skipStep < failStep;
  unsigned fwdWrites = failStep - skipped;
  unsigned stepWrites = 0;
  bool journalOrder = true;
  store->writeFn([context = context.ptr(), failStep, fwdWrites,
      &stepWrites, &journalOrder](ZuCSpan id) {
    if (id != "saga_step") return;
    ++stepWrites;
    if (stepWrites > fwdWrites)
      journalOrder &= context->dirs.length() == failStep + 1 +
	stepWrites - fwdWrites;
  });

  bool admitted = false, outcome = true;
  unsigned called = 0;
  ZmSemaphore submitted, done;
  ZmRef<M> saga = new M{};
  saga->init(zdbtest::LiveSaga{{}, 81});
  ZuCheckRT(db->saga(0, 81, ZuMv(saga),
    zdbtest::MoveSubmit{&admitted, &submitted},
    zdbtest::MoveComplete{&outcome, &called, &done}));
  submitted.wait();
  ZuCheckRT(admitted);
  done.wait();
  ZuCheckRT(!outcome);
  ZuCheckRT(called == 1);
  ZuCheckRT(context->refCount() == contextRefs + 1);
  ZuCheckRT(context->dirs.length() == (failStep << 1) + 1 - skipped);
  bool ordered = true;
  for (unsigned i = 0; i <= failStep; ++i)
    ordered &= context->dirs[i] == int(i) + 1;
  unsigned j = failStep + 1;
  for (unsigned i = failStep; i-- > 0; )
    if (i != skipStep) ordered &= context->dirs[j++] == -int(i) - 1;
  ZuCheckRT(ordered);
  ZuCheckRT(stepWrites == (failStep - skipped) << 1);
  ZuCheckRT(journalOrder);
  bool clean = ZmBlock<bool>{}([db = db.ptr(), orders = orders.ptr()](auto wake) {
    db->run([db, orders, wake = ZuMv(wake)]() mutable {
      wake(!orders->count() && !db->table("saga")->count() &&
	!db->table("saga_step")->count());
    });
  });
  ZuCheckRT(clean);

  context->failStep = UINT32_MAX;
  saga = new M{};
  saga->init(zdbtest::LiveSaga{{}, 82});
  ZuCheckRT(db->saga(0, 82, ZuMv(saga), [](bool) { },
    [&outcome, &done](bool ok) { outcome = ok; done.post(); }));
  done.wait();
  ZuCheckRT(outcome);
  ZuCheckRT(db->stop());
  orders = {};
  db->final();
  ZuCheckRT(context->refCount() == contextRefs);
  db = {};
  store = {};
  ZuCheckRT(mx.stop());
}

static void abandonRollback()
{
  ZuTestScopeRT(abandonRollback);
  using DB = ZdbSagaDB<zdbtest::LiveContext, zdbtest::LiveSagas>;
  using M = ZdbMSaga<zdbtest::LiveSagas>;
  auto config = cf();
  ZiMultiplex mx{ZvMxParams{"mx", config->resolve("mx")}};
  ZuCheckRT(mx.start());
  ZmRef<zdbtest::Store> store = new zdbtest::Store{};
  store->preserve();
  ZmRef<DB> db = new DB{};
  ZmSemaphore active, reverseFailed;
  active_ = &active;
  db->init(ZdbCf{config->resolve("zdb")}, &mx, ZdbHandler{
    .upFn = up
  }, store);
  auto orders = db->initTable<zdbtest::Order>("o");
  ZmRef<zdbtest::LiveContext> context = new zdbtest::LiveContext{};
  context->orders = orders;
  context->failStep = 1;
  context->failReverse = true;
  context->reverseFailed = &reverseFailed;
  db->sagas(context);
  ZuCheckRT(db->start());
  active.wait();

  bool admitted = false, outcome = true;
  unsigned called = 0;
  ZmSemaphore submitted, done;
  ZmRef<M> saga = new M{};
  saga->init(zdbtest::LiveSaga{{}, 81});
  ZuCheckRT(db->saga(0, 81, ZuMv(saga),
    zdbtest::MoveSubmit{&admitted, &submitted},
    zdbtest::MoveComplete{&outcome, &called, &done}));
  submitted.wait();
  reverseFailed.wait();
  ZuCheckRT(admitted);

  context->failStep = UINT32_MAX;
  context->failReverse = false;
  saga = new M{};
  saga->init(zdbtest::LiveSaga{{}, 82});
  ZuCheckRT(db->saga(0, 82, ZuMv(saga), [](bool) { },
    [&outcome, &done](bool ok) { outcome = ok; done.post(); }));
  done.wait();
  ZuCheckRT(outcome);
  ZuCheckRT(!called);
  bool retained = ZmBlock<bool>{}([db = db.ptr()](auto wake) {
    db->run([db, wake = ZuMv(wake)]() mutable {
      wake(db->table("saga")->count() == 1 &&
	db->table("saga_step")->count() == 1);
    });
  });
  ZuCheckRT(retained);
  ZuCheckRT(db->stop());
  orders = {};
  db->final();

  db->init(ZdbCf{config->resolve("zdb")}, &mx, ZdbHandler{
    .upFn = up
  }, store);
  orders = db->initTable<zdbtest::Order>("o");
  ZmRef<zdbtest::LiveContext> retry = new zdbtest::LiveContext{};
  retry->orders = orders;
  retry->failStep = 1;
  retry->failReverse = true;
  retry->reverseFailed = &reverseFailed;
  db->sagas(retry);
  ZuCheckRT(db->start());
  active.wait();
  reverseFailed.wait();
  ZuCheckRT(retry->runs == 3);
  retained = ZmBlock<bool>{}([db = db.ptr()](auto wake) {
    db->run([db, wake = ZuMv(wake)]() mutable {
      wake(db->table("saga")->count() == 1 &&
	db->table("saga_step")->count() == 1);
    });
  });
  ZuCheckRT(retained);
  ZuCheckRT(db->stop());
  orders = {};
  db->final();

  db->init(ZdbCf{config->resolve("zdb")}, &mx, ZdbHandler{
    .upFn = up
  }, store);
  orders = db->initTable<zdbtest::Order>("o");
  ZmRef<zdbtest::LiveContext> repaired = new zdbtest::LiveContext{};
  repaired->orders = orders;
  db->sagas(repaired);
  ZuCheckRT(db->start());
  active.wait();
  ZuCheckRT(repaired->runs == 3);
  ZuCheckRT(repaired->updates == 1);
  ZuCheckRT(repaired->deletes == 1);
  bool clean = ZmBlock<bool>{}([db = db.ptr()](auto wake) {
    db->run([db, wake = ZuMv(wake)]() mutable {
      wake(!db->table("saga")->count() &&
	!db->table("saga_step")->count());
    });
  });
  ZuCheckRT(clean);
  ZuCheckRT(!called);
  ZuCheckRT(db->stop());
  orders = {};
  db->final();
  db = {};
  store = {};
  ZuCheckRT(mx.stop());
}

int main()
{
  ZiLog::init("ZdbSagaTest");
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();
  ZuTestMain();
  ZuTestCall(rows);
  ZuTestCall(groupKeys);
  ZuTestCall(repeatedLayout);
  ZuTestCall(dispatch);
  ZuTestCall(catalog);
  ZuTestCall(catalogStartup);
  ZuTestCall(catalogCorrupt, 1);
  ZuTestCall(catalogCorrupt, 2);
  ZuTestCall(findFail);
  ZuTestCall(scanError, false);
  ZuTestCall(scanError, true);
  ZuTestCall(scanFail, false, false);
  ZuTestCall(scanFail, true, false);
  ZuTestCall(scanFail, false, true);
  ZuTestCall(scanFail, true, true);
  ZuTestCall(admission);
  ZuTestCall(payloadAdmission, 0);
  ZuTestCall(payloadAdmission, ZdbSaga_BuiltinSize);
  ZuTestCall(payloadAdmission, 2 * ZdbSaga_BuiltinSize);
  ZuTestCall(rollback, 0);
  ZuTestCall(rollback, 1);
  ZuTestCall(rollback, 2);
  ZuTestCall(rollback, 2, false, false, 1);
  ZuTestCall(rollback, 1, true);
  ZuTestCall(abandonRollback);
  ZuTestCall(admissionDeactivated);
  ZuTestCall(admissionDeactivated, true);
  ZuTestCall(indexes);
  ZuTestCall(tableIDs);
  ZuTestCall(collisions);
  ZuTestCall(live, 0);
  ZuTestCall(live, 0, true);
  ZuTestCall(live, 1);
  ZuTestCall(live, 2);
  ZuTestCall(live, 1, true);
  ZuTestCall(live, 2, true);
  ZuTestCall(live, 1, false, true);
  ZuTestCall(live, 2, false, true);
  ZuTestCall(live, 1, true, true);
  ZuTestCall(live, 2, true, true);
  ZuTestCall(gracefulStop, false);
  ZuTestCall(gracefulStop, true);
  ZuTestCall(recovery, 0);
  ZuTestCall(recovery, 1);
  ZuTestCall(recovery, 2);
  ZuTestCall(recovery, 3);
  ZuTestCall(recovery, 4);
  ZuTestCall(recovery, 5);
  ZuTestCall(recovery, 6);
  ZuTestCall(recovery, 7);
  ZuTestCall(recovery, 0, true);
  ZuTestCall(recovery, 7, true);
  ZuTestCall(recovery, 7, false, false, 1);
  ZuTestCall(recovery, 0, true, true);
  ZuTestCall(recovery, 2, true, true);
  ZuTestCall(recovery, 0, false, false, UINT32_MAX, true);
  ZuTestCall(recovery, 1, false, false, UINT32_MAX, true);
  ZuTestCall(recovery, 2, false, false, UINT32_MAX, true);
  ZuTestCall(recovery, 4, false, false, UINT32_MAX, true);
  ZuTestCall(recovery, 5, false, false, UINT32_MAX, true);
  ZuTestCall(recovery, 6, false, false, UINT32_MAX, true);
  ZuTestCall(recovery, 7, false, false, UINT32_MAX, true);
  ZuTestCall(recoveryCleanup, 0);
  ZuTestCall(recoveryCleanup, 1);
  ZuTestCall(recoveryCleanup, 2);
  ZuTestCall(recoveryCleanup, 3);
  ZuTestCall(recoveryQueue);
  ZuTestCall(recoveryQueue, true);
  ZuTestCall(recoveryError, 0);
  ZuTestCall(recoveryError, 1);
  ZuTestCall(recoveryError, 2);
  ZuTestCall(recoveryError, 3);
  ZuTestCall(recoveryError, 4);
  ZuTestCall(recoveryError, 5);
  ZuTestCall(recoveryError, 6);
  ZuTestCall(recoveryError, 7);
  ZuTestCall(recoveryError, 8);
  ZuTestCall(recoveryPages, false);
  ZuTestCall(recoveryPages, true);
  ZiLog::stop();
}
