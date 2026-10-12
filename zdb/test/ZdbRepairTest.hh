//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef ZdbRepairTest_HH
#define ZdbRepairTest_HH

#include <string.h>

#include <zlib/ZuMatcher.hh>
#include <zlib/ZuTestUtil.hh>
#include <zlib/ZtString.hh>
#include <zlib/ZfCf.hh>
#include <zlib/ZiMultiplex.hh>
#include <zlib/ZvMxParams.hh>
#include <zlib/Zdb.hh>
#include <zlib/ZiFBReflect.hh>

namespace zdbtest {

struct RepairRow {
  uint64_t id;
  ZtArray<float> reals;
  ZtArray<ZtString<>> names;
  ZtString<> stamp;
  float real;
  ZiIP ip;
  double wide;
  ZtArray<ZtString<>> strings;

  const auto &cstrings() const & { return names; }
  auto &cstrings() & { return names; }
  auto &&cstrings() && { return ZuMv(names); }
  void cstrings(ZfStruct_::CStringVec v) {
    names.length(0);
    names.size(v.length());
    v.all([this](const char *s) { new (names.push()) ZtString<>{s}; });
  }
};
ZfStruct(, (RepairRow, FB),
  (id,		(Ctor<0>, (Keys<0, 2>)),		UInt64),
  (reals,	(Ctor<1>, Mutable),		FloatVec),
  ((names, AliasFn, cstrings, cstrings), (Ctor<2>, Mutable), CStringVec),
  (stamp,	(Ctor<3>, Required, Keys<2>, Group<2>), String),
  (real,	(Ctor<4>, Keys<1>, Mutable, Deflt<1.5>), Float),
  (ip,		(Ctor<5>, Mutable),		UDT),
  (wide,	(Ctor<6>, Mutable),		Float),
  (strings,	(Ctor<7>, Mutable),		StringVec));
ZiFB::AsTable<"repair.Row"> ZiFB_Fmt(RepairRow *);
template <typename F>
using RepairDefaults = ZuBool<!ZuIsSame<F, ZfField(RepairRow, real)>{}>;

// Shared supported-column fixture: the caller owns store startup/teardown.
inline void repairBindings(Zdb_::Store *store)
{
  ZuTestScopeRT(repairBindings);
  using namespace Zdb_;
  using T = RepairRow;
  SchemaBuf schema = SchemaBuf(ZiFB::SchemaOut::Schema<T>::Size);
  ZiFB::saveSchema<T>(schema);
  using KeyIDs = ZuStructKeyIDs<T>;
  ZuArray<ZfVFieldArray, KeyIDs::N> keys(KeyIDs::N, false);
  ZuUnroll::all<KeyIDs>([&keys](auto I) { keys[I] = SplitKey<T, I>::storeFields(); });
  auto open = [&store, &schema, &keys]() {
    return ZmBlock<OpenResult>{}([&store, &schema, &keys](auto done) {
      store->open(false, "repair", ZfVFields<T>(), keys,
	Schema::from<T>(schema), Table<T>::allocBuf, ZuMv(done));
    });
  };
  auto opened = open();
  ZuCheckRT(opened.is<OpenData>());
  if (opened.is<OpenData>()) {
    auto table = opened.p<OpenData>().storeTbl;
    T row{7, {-0.0f, -2.5f, __builtin_inff(), __builtin_nanf("")},
      {"short", "another long owning string"}, "immutable required field", 1.5f, {},
      -2.5, {"", "short", "another long owning string"}};
    in_addr v4;
    memset(&v4, 0x7f, sizeof(v4));
    row.ip = ZiIP{v4};
    auto write = [&table, &row]<template <typename> class Filter>(UN un, VN vn) {
      Builder fbb{Table<T>::allocBuf(), ZiFB::BufferAlign<T>::N};
      auto nested = fbb.startNested();
      auto data = fbb.endNested(nested,
	ZiFB::save<ZuFacet::FB, Filter>(fbb, row));
      fbb.finish(saveMsg(fbb, saveRecord(fbb, "repair", un, SN{un}, vn, 0, data),
	Msg::Tag::Replication));
      ZmRef<IOBuf> buf = saveHdr(fbb);
      auto record = record_(msg_(buf->hdr()));
      ZuCheckRT(bool(Zdb_::data<T>(record)));
      if (vn) {
	auto view = data_<T>(record);
	ZuCheckRT(!view.has<ZfField(RepairRow, stamp)>());
	if (vn < 0) ZuCheckRT(!view.has<ZfField(RepairRow, real)>());
      }
      auto result = ZmBlock<CommitResult>{}([&table, buf = ZuMv(buf)](auto done) mutable {
	table->write(ZuMv(buf), [done = ZuMv(done)](ZmRef<IOBuf>, CommitResult result) mutable {
	  done(ZuMv(result));
	});
      });
      ZuCheckRT(result.template is<void>());
    };
    auto find = [&table, &row]<unsigned KeyID>() {
      Builder key{Table<T>::allocBuf(), ZiFB::BufferAlign<T>::N};
      key.finish(ZiFB::save(key, ZuStructKey<KeyID>(row)));
      return ZmBlock<RowResult>{}([&table, buf = key.buf()](auto done) mutable {
	table->find(KeyID, ZuMv(buf), ZuMv(done));
      });
    };
    auto check = [&row](RowResult result) {
      ZuCheckRT(result.is<RowData>());
      if (!result.is<RowData>()) return;
      auto &buf = result.p<RowData>().buf;
      auto bytes = record_(msg_(buf->hdr())).get<Msg::ZfField(Record, data)>();
      ZuCheckRT(ZiFB::verify<T>(bytes));
      if (!ZiFB::verify<T>(bytes)) return;
      auto loaded = ZiFB::load<T>(bytes);
      ZuCheckRT(loaded.id == row.id);
      ZuCheckRT(loaded.reals.length() == row.reals.length());
      for (unsigned i = 0, n = loaded.reals.length(); i < n; ++i) {
	if (__builtin_isnan(row.reals[i])) ZuCheckRT(__builtin_isnan(loaded.reals[i]));
	else ZuCheckRT(loaded.reals[i] == row.reals[i]);
	if (!row.reals[i])
	  ZuCheckRT(__builtin_signbit(loaded.reals[i]) == __builtin_signbit(row.reals[i]));
      }
      ZuCheckRT(loaded.names.length() == row.names.length());
      for (unsigned i = 0, n = loaded.names.length(); i < n; ++i)
	ZuCheckRT(ZuCSpan{loaded.names[i]} == row.names[i]);
      ZuCheckRT(loaded.stamp == row.stamp && loaded.real == row.real);
      if (!row.real)
	ZuCheckRT(__builtin_signbit(loaded.real) == __builtin_signbit(row.real));
      ZuCheckRT(loaded.ip == row.ip);
      ZuCheckRT(loaded.wide == row.wide && loaded.strings == row.strings);
    };
    write.template operator()<RepairDefaults>(0, 0);
    check(find.template operator()<0>());
    check(find.template operator()<1>());
    check(find.template operator()<2>());
    {
      using Group = typename SplitKey<T, 2>::GroupKey;
      Builder key{Table<T>::allocBuf(), ZiFB::BufferAlign<Group>::N};
      key.finish(ZiFB::save(key, Group{row.stamp}));
      auto keyBuf = key.buf();
      auto counted = ZmBlock<CountResult>{}([&table, buf = keyBuf](auto done) mutable {
	table->count(2, ZuMv(buf), ZuMv(done));
      });
      ZuCheckRT(counted.is<CountData>());
      if (counted.is<CountData>()) ZuCheckRT(counted.p<CountData>().count == 1);
      unsigned selected = 0;
      ZmBlock<>{}([&table, &row, &selected, buf = ZuMv(keyBuf)](auto done) mutable {
	  table->select(false, false, false, 2, ZuMv(buf), 1,
	  [done = ZuMv(done), &row, &selected](TupleResult result) mutable {
	    if (result.is<TupleData>()) {
	      auto key = ZiFB::load<ZuStructKeyT<T, 2>>(result.p<TupleData>().buf->cspan());
	      if (key.p<0>() == row.id && key.p<1>() == row.stamp) ++selected;
	    } else done();
	  });
      });
      ZuCheckRT(selected == 1);
    }

    row.reals = {};
    row.names = {};
    row.real = -0.0f;
    in6_addr v6;
    memset(&v6, 0xa5, sizeof(v6));
    row.ip = ZiIP{v6};
    write.template operator()<ZfFieldFilter::Upd>(1, 1);
    check(find.template operator()<0>());
    check(find.template operator()<1>());
    check(find.template operator()<2>());
    auto recovered = ZmBlock<RowResult>{}([&table](auto done) {
      table->recover(0, 1, ZuMv(done));
    });
    check(ZuMv(recovered));

    row.reals = {1.25f};
    row.names = {"singleton"};
    write.template operator()<ZfFieldFilter::Upd>(2, 2);
    check(find.template operator()<0>());

    ZmBlock<>{}([&table](auto done) { table->close(ZuMv(done)); });
    SchemaBuf replacement = SchemaBuf(ZiFB::SchemaOut::Schema<T>::Size);
    ZiFB::saveSchema<T>(replacement);
    schema = ZuMv(replacement);
    opened = open();
    ZuCheckRT(opened.is<OpenData>());
    if (opened.is<OpenData>()) {
      table = opened.p<OpenData>().storeTbl;
      check(find.template operator()<1>());
      check(find.template operator()<2>());
      write.template operator()<ZfFieldFilter::Del>(3, -3);
      ZuCheckRT(find.template operator()<0>().is<void>());
      ZuCheckRT(find.template operator()<1>().is<void>());
      ZuCheckRT(find.template operator()<2>().is<void>());
      ZmBlock<>{}([&table](auto done) { table->close(ZuMv(done)); });

    }
  }
}



struct RepairLimit {
  uint64_t id;
  ZtArray<int8_t> vec;
};
ZfStruct(, (RepairLimit, FB),
  (id,		(Ctor<0>, Keys<0>),			UInt64),
  (vec,		(Ctor<1>, Mutable, Keys<1>, Group<1>),	Int8Vec));
ZiFB::AsTable<"repair.Limit"> ZiFB_Fmt(RepairLimit *);

template <typename Result, typename Start>
Result repairCall(unsigned &calls, Start &&start)
{
  return ZmBlock<Result>{}([&calls, &start](auto done) {
    start([&calls, done = ZuMv(done), notified = false](Result result) mutable {
      ++calls;
      if (notified) return;
      notified = true;
      done(ZuMv(result));
    });
  });
}

template <typename Result>
bool repairSizeError(const Result &result)
{
  if (!result.template is<Zdb_::Event>()) return false;
  ZtString<> text;
  text << result.template p<Zdb_::Event>();
  return strstr(text.data(), "parameter buffer exceeds");
}

// Synthetic length words prove policy rejection before allocation/traversal.
// These intentionally incomplete vectors are never submitted to the engine.
inline void repairSize(Zdb_::Store *store)
{
  ZuTestScopeRT(repairSize);
  using namespace Zdb_;
  using T = RepairLimit;
  SchemaBuf schema(ZiFB::SchemaOut::Schema<T>::Size);
  ZiFB::saveSchema<T>(schema);
  using KeyIDs = ZuStructKeyIDs<T>;
  ZuArray<ZfVFieldArray, KeyIDs::N> keys(KeyIDs::N, false);
  ZuUnroll::all<KeyIDs>([&keys](auto I) { keys[I] = SplitKey<T, I>::storeFields(); });
  auto opened = ZmBlock<OpenResult>{}([store, &schema, &keys](auto done) {
    store->open(false, "repair_limit", ZfVFields<T>(), keys,
      Schema::from<T>(schema), Table<T>::allocBuf, ZuMv(done));
  });
  ZuCheckRT(opened.is<OpenData>());
  if (!opened.is<OpenData>()) return;
  auto table = opened.p<OpenData>().storeTbl;
  T row{1, {7}};
  auto oversize = [](ZiFB::TableView data, unsigned id) {
    auto length = ZiFB::follow(data.field(id));
    ZiFB::write<uint32_t>(const_cast<uint8_t *>(length), MaxBufSize + 1);
  };
  using Key = ZuStructKeyT<T, 1>;
  using Field = ZuType<0, ZuFields<Key>>;
  Builder key{Table<T>::allocBuf(), ZiFB::BufferAlign<Key>::N};
  key.finish(ZiFB::save(key, ZuStructKey<1>(row)));
  ZmRef<IOBuf> keyBuf = key.buf();
  oversize(ZiFB::TableView{ZiFB::root(keyBuf->cspan())}, ZiFB::As<Key>::template Handler<Key>::id<Field>());
  unsigned counts = 0, selects = 0, finds = 0, writes = 0;
  auto count = repairCall<CountResult>(counts, [table, keyBuf](auto done) {
    table->count(1, keyBuf, ZuMv(done));
  });
  ZuCheckRT(repairSizeError(count));
  auto select = repairCall<TupleResult>(selects, [table, keyBuf](auto done) {
    table->select(true, true, false, 1, keyBuf, 1, ZuMv(done));
  });
  ZuCheckRT(repairSizeError(select));
  auto find = repairCall<RowResult>(finds, [table, keyBuf](auto done) {
    table->find(1, keyBuf, ZuMv(done));
  });
  ZuCheckRT(repairSizeError(find));
  for (VN vn = 0; vn <= 1; ++vn) {
    Builder builder{Table<T>::allocBuf(), ZiFB::BufferAlign<T>::N};
    auto nested = builder.startNested();
    auto data = builder.endNested(nested, ZiFB::save(builder, row));
    builder.finish(saveMsg(builder,
      saveRecord(builder, "repair_limit", UN(vn), SN{0}, vn, 0, data),
      Msg::Tag::Replication));
    ZmRef<IOBuf> buf = saveHdr(builder);
    auto record = record_(msg_(buf->hdr()));
    oversize(ZiFB::TableView{ZiFB::root(record.get<Msg::ZfField(Record, data)>())},
      ZiFB::As<T>::template Handler<T>::id<ZfField(RepairLimit, vec)>());
    auto result = repairCall<CommitResult>(writes, [table, buf](auto done) {
      table->write(buf, [done = ZuMv(done)](ZmRef<IOBuf>, CommitResult result) mutable {
	done(ZuMv(result));
      });
    });
    ZuCheckRT(repairSizeError(result));
  }
  ZmBlock<>{}([table](auto done) { table->close(ZuMv(done)); });
  ZuCheckRT(counts == 1 && selects == 1 && finds == 1 && writes == 2);
}

struct RepairContext : public ZmPolymorph {
  ZdbTable<RepairRow> *table = nullptr;
  ZmSemaphore *paused = nullptr;
  Zdb_::SagaCompleteFn resume;
  unsigned inserts = 0, updates = 0, deletes = 0;
  bool valid = true;
};

inline bool repairValues(const RepairRow &row, bool updated)
{
  return row.real == (updated ? -2.5f : 1.5f) &&
    row.reals.length() == 2 && row.reals[0] == (updated ? 3.5f : -0.0f) &&
    row.reals[1] == -7.25f && row.names.length() == 2 &&
    ZuCSpan{row.names[0]} == (updated ? "updated" : "initial") &&
    ZuCSpan{row.names[1]} == "a long CStringVec element retained in the store" &&
    row.stamp == "required immutable value";
}

// Exercise native row commits through saga intent/effect and compensation.
struct RepairSaga : public ZdbSagaBase<RepairContext> {
  using Base = ZdbSagaBase<RepairContext>;
  using Base::context;
  using Base::saga;
  using Type = ZuStringT<"repairSaga">;
  enum { NSteps = 3 };
  uint64_t id = 0;
  bool fail = false;

  ZdbSagaStep(0, repair_saga, Insert) {
    auto table = context->table;
    table->run(0, [this, table, complete = ZuMv(complete)]() mutable {
      if constexpr (Fwd) {
	ZdbRowRef<RepairRow> row = new ZdbRow<RepairRow>{table, 0};
	saga->insert(table, ZuMv(row), ZuMv(complete),
	  [this](ZdbRow<RepairRow> *row, auto &&complete) mutable {
	    ++context->inserts;
	    new (row->ptr()) RepairRow{id, {-0.0f, -7.25f},
	      {"initial", "a long CStringVec element retained in the store"},
	      "required immutable value", 1.5f, {}, 12.5, {"control"}};
	    complete(bool(row->commit()));
	  });
      } else {
	saga->findDel<0>(table, 0, ZuFwdTuple(id), ZuMv(complete),
	  [this](ZdbRow<RepairRow> *row, auto &&complete) mutable {
	    ++context->deletes;
	    context->valid &= repairValues(row->data(), false);
	    complete(bool(row->commit()));
	  });
      }
    });
    return {};
  }

  ZdbSagaStep(1, repair_saga, Update) {
    auto table = context->table;
    table->run(0, [this, table, complete = ZuMv(complete)]() mutable {
      saga->findUpd<1, ZuSeq<1>>(table, 0, ZuFwdTuple(Fwd ? 1.5f : -2.5f),
	ZuMv(complete), [this](ZdbRow<RepairRow> *row, auto &&complete) mutable {
	  ++context->updates;
	  context->valid &= repairValues(row->data(), !Fwd);
	  row->data().real = Fwd ? -2.5f : 1.5f;
	  row->data().reals[0] = Fwd ? 3.5f : -0.0f;
	  row->data().names[0] = Fwd ? "updated" : "initial";
	  complete(bool(row->commit()));
	});
    });
    return {};
  }

  ZdbSagaStep(2, repair_saga, Update) {
    ZuAssert(Fwd);
    if (context->paused) {
      context->resume = ZuMv(complete);
      context->paused->post();
      return {};
    }
    if (fail) complete(false);
    else saga->skip(ZuMv(complete));
    return {};
  }
};
ZfStruct(, (RepairSaga, FB),
  (id,		(Ctor<0>),	UInt64),
  (fail,	(Ctor<1>),	Bool));
ZiFB::AsTable<"repair.Saga"> ZiFB_Fmt(RepairSaga *);
struct RepairCatalog {
  using List = ZuTypeList<RepairSaga>;
  static int match(ZuCSpan type) {
    struct IDs { using Keys = Zdb_::SagaTypes<List>; };
    static constexpr auto matcher = ZuMatcher<IDs>();
    return matcher.exact(type);
  }
};

inline void repairSaga(
    const ZfCf::AnyNode *cf, const ZfCf::AnyNode *mxCf,
    ZmRef<Zdb_::Store> store, bool fail,
    ZdbShard sagaShard = 0, bool paramLimits = false, bool restart = false)
{
  ZuTestScopeRT(repairSaga);
  using DB = ZdbSagaDB<RepairContext, RepairCatalog>;
  using M = ZdbMSaga<RepairCatalog>;
  ZiMultiplex mx{ZvMxParams("mx", mxCf)};
  ZuCheckRT(mx.start());
  ZmRef<DB> db = new DB{};
  db->init(ZdbCf{cf}, &mx, {}, store);
  auto table = db->initTable<RepairRow>("repair_saga");
  ZmRef<RepairContext> context = new RepairContext{};
  context->table = table;
  ZmSemaphore paused;
  if (restart) context->paused = &paused;
  db->sagas(context);
  bool started = db->start();
  ZuCheckRT(started);
  if (started) {
    if (paramLimits && !fail) ZuTestCallRT(repairSize, db->store());
    ZmRef<M> saga = new M{};
    saga->init(RepairSaga{{}, uint64_t(fail) + 1, fail});
    ZmSemaphore submitted, completed;
    bool admitted = false, outcome = false;
    ZuCheckRT(db->saga(sagaShard, uint64_t(fail) + 1, ZuMv(saga),
      [&admitted, &submitted](bool ok) { admitted = ok; submitted.post(); },
      [&outcome, &completed](bool ok) { outcome = ok; completed.post(); }));
    submitted.wait();
    ZuCheckRT(admitted);
    if (admitted) {
      if (restart) {
	paused.wait();
	ZuCheckRT(context->valid && context->inserts == 1 &&
	  context->updates == 1 && !context->deletes);
	db->run([db = db.ptr(), context]() {
	  db->fail();
	  context->resume(false);
	});
	ZuCheckRT(db->stop());
	table = {};
	db->final();
	db->init(ZdbCf{cf}, &mx, {}, store);
	table = db->initTable<RepairRow>("repair_saga");
	context = new RepairContext{};
	context->table = table;
	db->sagas(context);
	started = db->start();
	ZuCheckRT(started);
	if (!started) {
	  table = {};
	  db->final();
	  db = {};
	  ZuCheckRT(mx.stop());
	  return;
	}
      } else {
	completed.wait();
	ZuCheckRT(outcome == !fail);
      }
      using Tuple = typename Zdb_::Table<RepairRow>::Tuple;
      auto found = ZmBlock<ZuTuple<unsigned, bool>>{}([
	table = table.ptr()
      ](auto wake) mutable {
	table->selectRows<0>({}, 2, [
	  wake = ZuMv(wake), rows = 0U, valid = true
	](ZuUnion<void, Tuple> result, unsigned) mutable {
	  if (!result.is<Tuple>()) { wake(ZuTuple<unsigned, bool>{rows, valid}); return; }
	  ++rows;
	  valid &= ZuTupleCall(ZuMv(result).p<Tuple>(), [](auto &&...args) {
	    return repairValues(RepairRow{ZuFwd<decltype(args)>(args)...}, true);
	  });
	});
      });
      ZuCheckRT(found.p<0>() == unsigned(!fail) && found.p<1>());
      if (!fail) {
	ZuCheckRT(ZmBlock<bool>{}([table = table.ptr()](auto wake) {
	  table->run(0, [table, wake = ZuMv(wake)]() mutable {
	    table->findDel<1>(0, ZuFwdTuple(-2.5f),
	      [wake = ZuMv(wake)](ZdbRow<RepairRow> *row) mutable {
		wake(row && row->commit());
	      });
	  });
	}));
      }
      ZuCheckRT(ZmBlock<bool>{}([db = db.ptr()](auto wake) {
	db->run([db, wake = ZuMv(wake)]() mutable {
	  wake(!db->table("saga")->count() && !db->table("saga_step")->count());
	});
      }));
    }
    ZuCheckRT(db->stop());
    ZuCheckRT(context->valid && context->inserts == unsigned(!restart) &&
      context->updates == unsigned(!restart) + unsigned(fail) &&
      context->deletes == unsigned(fail));
  }
  table = {};
  db->final();
  db = {};
  ZuCheckRT(mx.stop());
}

} // zdbtest

#endif /* ZdbRepairTest_HH */
