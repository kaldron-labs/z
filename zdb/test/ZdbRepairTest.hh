//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef ZdbRepairTest_HH
#define ZdbRepairTest_HH

#include "ZdbRepairDefs.hh"

#include <string.h>

#include <zlib/ZuMatcher.hh>
#include <zlib/ZuTestUtil.hh>
#include <zlib/ZtString.hh>
#include <zlib/ZfCf.hh>
#include <zlib/ZiMultiplex.hh>
#include <zlib/ZvMxParams.hh>
#include <zlib/Zdb.hh>
#include <zlib/ZfbReflect.hh>

namespace zdbtest {

template <typename F>
using RepairDefaults = ZuBool<!ZuIsSame<F, ZfField(RepairRow, real)>{}>;

// Shared supported-column fixture: the caller owns store startup/teardown.
inline void repairBindings(Zdb_::Store *store)
{
  ZuTestScopeRT(repairBindings);
  using namespace Zdb_;
  using T = RepairRow;
  SchemaBuf schema = SchemaBuf(Zfb::SchemaOut::Schema<T>::Size);
  Zfb::saveSchema<T>(schema);
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
      Builder fbb{Table<T>::allocBuf(), Zfb::BufferAlign<T>::N};
      auto nested = fbb.startNested();
      auto data = fbb.endNested(nested,
	Zfb::save<ZuFacet::FB, Filter>(fbb, row));
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
      Builder key{Table<T>::allocBuf(), Zfb::BufferAlign<T>::N};
      key.finish(Zfb::save(key, ZuStructKey<KeyID>(row)));
      return ZmBlock<RowResult>{}([&table, buf = key.buf()](auto done) mutable {
	table->find(KeyID, ZuMv(buf), ZuMv(done));
      });
    };
    auto check = [&row](RowResult result) {
      ZuCheckRT(result.is<RowData>());
      if (!result.is<RowData>()) return;
      auto &buf = result.p<RowData>().buf;
      auto bytes = record_(msg_(buf->hdr())).get<Msg::ZfField(Record, data)>();
      ZuCheckRT(Zfb::verify<T>(bytes));
      if (!Zfb::verify<T>(bytes)) return;
      auto loaded = Zfb::load<T>(bytes);
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
      Builder key{Table<T>::allocBuf(), Zfb::BufferAlign<Group>::N};
      key.finish(Zfb::save(key, Group{row.stamp}));
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
	      auto key = Zfb::load<ZuStructKeyT<T, 2>>(result.p<TupleData>().buf->cspan());
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
    SchemaBuf replacement = SchemaBuf(Zfb::SchemaOut::Schema<T>::Size);
    Zfb::saveSchema<T>(replacement);
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
Zfb::AsTable<"repair.Limit"> Zfb_Fmt(RepairLimit *);

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
  SchemaBuf schema(Zfb::SchemaOut::Schema<T>::Size);
  Zfb::saveSchema<T>(schema);
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
  auto oversize = [](Zfb::TableView data, unsigned id) {
    auto length = Zfb::follow(data.field(id));
    Zfb::write<uint32_t>(const_cast<uint8_t *>(length), MaxBufSize + 1);
  };
  using Key = ZuStructKeyT<T, 1>;
  using Field = ZuType<0, ZuFields<Key>>;
  Builder key{Table<T>::allocBuf(), Zfb::BufferAlign<Key>::N};
  key.finish(Zfb::save(key, ZuStructKey<1>(row)));
  ZmRef<IOBuf> keyBuf = key.buf();
  oversize(Zfb::TableView{Zfb::root(keyBuf->cspan())}, Zfb::As<Key>::template Handler<Key>::id<Field>());
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
    Builder builder{Table<T>::allocBuf(), Zfb::BufferAlign<T>::N};
    auto nested = builder.startNested();
    auto data = builder.endNested(nested, Zfb::save(builder, row));
    builder.finish(saveMsg(builder,
      saveRecord(builder, "repair_limit", UN(vn), SN{0}, vn, 0, data),
      Msg::Tag::Replication));
    ZmRef<IOBuf> buf = saveHdr(builder);
    auto record = record_(msg_(buf->hdr()));
    oversize(Zfb::TableView{Zfb::root(record.get<Msg::ZfField(Record, data)>())},
      Zfb::As<T>::template Handler<T>::id<ZfField(RepairLimit, vec)>());
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

inline void repairSaga(
    const ZfCf::AnyNode *cf, const ZfCf::AnyNode *mxCf,
    ZmRef<Zdb_::Store> store, bool fail,
    ZdbShard sagaShard = 0, bool paramLimits = false, bool restart = false)
{
  ZuTestScopeRT(repairSaga);
  using DB = RepairDB;
  using M = ZdbAnySaga<RepairDB>;
  ZiMultiplex mx{ZvMxParams("mx", mxCf)};
  ZuCheckRT(mx.start());
  ZmRef<DB> db = new DB{};
  db->init(ZdbCf{cf}, &mx, {}, store);
  auto table = db->initTable<RepairRow>("repair_saga");
  ZmRef<RepairDB> context = db;
  context->rows = table;
  ZmSemaphore paused;
  if (restart) context->paused = &paused;
  db->sagas({repairSagaDef()});
  bool started = db->start();
  ZuCheckRT(started);
  if (started) {
    if (paramLimits && !fail) ZuTestCallRT(repairSize, db->store());
    ZmRef<M> saga = repairDef(uint64_t(fail) + 1, fail);
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
	db = new RepairDB{};
	db->init(ZdbCf{cf}, &mx, {}, store);
	table = db->initTable<RepairRow>("repair_saga");
	context = db;
	context->rows = table;
	db->sagas({repairSagaDef()});
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
