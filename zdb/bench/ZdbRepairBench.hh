//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Included by the HEAD and native ZdbSagaTest fixtures after their shared
// Order/LiveSaga declarations. No production implementation is substituted.
#ifndef ZdbRepairBench_HH
#define ZdbRepairBench_HH

#include <time.h>
#include <stdio.h>
#include <stdlib.h>

#include <zlib/ZiModule.hh>

#include "../../zdb/test/ZdbTestCf.hh"

namespace RepairBench {

static void check(bool ok)
{
  if (!ok) { fputs("repair benchmark invariant failed\n", stderr); Zm::exit(1); }
}

// Optional configured store for the same matched workload. Setup is untimed.
static ZuPtr<const ZfCf::AnyNode> config()
{
  if (auto text = getenv("ZDB_REPAIR_CONFIG")) {
    auto scan = ZfCf::scan(text);
    return ZuMv(scan.p<1>());
  }
  return cf();
}

static ZmRef<Zdb_::Store> newStore()
{
  auto path = getenv("ZDB_MODULE");
  if (!path) return new ZdbMem::Store{};
  ZiModule module;
  ZeString error;
  int result = module.load(path, 0, &error);
  if (result < 0) std::cerr << error << '\n';
  check(result >= 0);
  auto create = reinterpret_cast<Zdb_::StoreFn>(module.resolve(ZdbStoreFnSym, &error));
  if (!create) std::cerr << error << '\n';
  check(bool(create));
  return (*create)();
}

static uint64_t now()
{
  timespec value;
  check(!clock_gettime(CLOCK_MONOTONIC_RAW, &value));
  return uint64_t(value.tv_sec)*1000000000 + value.tv_nsec;
}

struct Count {
  uint64_t allocs = 0;
  uint64_t frees = 0;
};

static bool heapCounts = true;

static Count heap()
{
  if (!heapCounts) return {};
  // Only the benchmark's main thread takes snapshots. A noncapturing
  // observer avoids adding its own closure allocation to every sample.
  static Count count;
  count = {};
  Ztc::HeapMgr::all([](Ztc::Heap *heap) {
    Ztc::HeapTelemetry data;
    heap->telemetry(data);
    count.allocs += data.cacheAllocs + data.heapAllocs;
    count.frees += data.cacheFrees + data.heapFrees;
  });
  return count;
}

struct Sample {
  ZuCSpan op;
  uint64_t ns;
  uint64_t bytes;
  uint64_t allocs;
  uint64_t frees;
};
using Samples = ZtArray<Sample, ZtArrayHeapID<"Zdb.Repair.Samples">>;

template <typename Fn>
static void measure(Samples &samples, ZuCSpan op, Fn fn)
{
  auto before = heap();
  auto start = now();
  uint64_t bytes = fn();
  auto ns = now() - start;
  auto after = heap();
  samples.push(Sample{op, ns, bytes,
    after.allocs - before.allocs, after.frees - before.frees});
}

static void output(const Samples &samples, unsigned warm, unsigned round)
{
  for (unsigned i = warm, n = samples.length(); i < n; ++i) {
    const auto &s = samples[i];
    std::cout << s.op << ',' << round << ',' << i - warm << ',' << s.ns
	<< ',' << s.bytes << ',';
    if (heapCounts) std::cout << s.allocs << ',' << s.frees;
    else std::cout << ','; // unmeasured, not zero allocations
    std::cout << '\n';
  }
}

template <typename T>
static auto key(const T &value)
{
#ifdef Zfb_HH
  Zdb_::Builder b{Zdb_::Table<zdbtest::Order>::allocBuf(), Zfb::BufferAlign<T>::N};
  b.finish(Zfb::save(b, value));
#else
  Zfb::IOBuilder b{Zdb_::Table<zdbtest::Order>::allocBuf()};
  b.Finish(ZfbStruct::save(b, value));
#endif
  return b.buf();
}

template <template <typename> class Filter>
static auto record(const zdbtest::Order &row, Zdb_::UN un, Zdb_::VN vn)
{
  using namespace Zdb_;
#ifdef Zfb_HH
  Builder b{Table<zdbtest::Order>::allocBuf(), Zfb::BufferAlign<zdbtest::Order>::N};
  auto nested = b.startNested();
  auto data = b.endNested(nested,
    Zfb::save<ZuFacet::FB, Filter>(b, row));
  b.finish(saveMsg(b, saveRecord(b, "repairbench", un, SN{un}, vn, 0, data),
    Msg::Tag::Replication));
#else
  Zfb::IOBuilder b{Table<zdbtest::Order>::allocBuf()};
  auto data = Zfb::Save::nest(b, [&row](Zfb::Builder &b) {
    return ZfbStruct::save<ZuFacet::Core, Filter>(b, row).Union();
  });
  auto sn = ZfbTransform::UInt128::save(SN{un});
  b.Finish(fbs::CreateMsg(b, fbs::Body::Replication,
    fbs::CreateRecord(b, Zfb::Save::str(b, "repairbench"), un, &sn, vn, 0, data).Union()));
#endif
  return saveHdr(b);
}

template <template <typename> class Filter>
consteval uint64_t fieldMask()
{
  using Fields = ZuFields<zdbtest::Order>;
  uint64_t mask = 0;
  ZuUnroll::all<Fields>([&mask]<typename F>() {
    if constexpr (Filter<F>{}) mask |= uint64_t(1)<<ZuTypeIndex<F, Fields>{};
  });
  return mask;
}

static void shape(ZuCSpan op, const ZmRef<Zdb_::IOBuf> &buf, uint64_t expected)
{
  uint64_t mask = 0;
#ifdef Zfb_HH
  auto data = Zdb_::record_(Zdb_::msg_(buf->hdr())).get<Zdb_::Msg::ZfField(Record, data)>();
  auto table = Zfb::TableView{Zfb::root(data)};
  for (unsigned i = 0; i < ZuFields<zdbtest::Order>::N; ++i)
    if (table.field(i)) mask |= uint64_t(1)<<i;
#else
  auto record = Zdb_::record_(Zdb_::msg_(buf->hdr()));
  auto data = Zfb::Load::bytes(record->data());
  auto table = Zfb::GetAnyRoot(data.data());
  for (unsigned i = 0; i < ZuFields<zdbtest::Order>::N; ++i)
    if (table->GetOptionalFieldOffset(4 + (i<<1))) mask |= uint64_t(1)<<i;
#endif
  check(mask == expected);
  std::cout << "#shape," << op << ',' << buf->length << ',' << mask << '\n';
}

static void store(unsigned count, unsigned rounds, unsigned width)
{
  using namespace Zdb_;
  using T = zdbtest::Order;
  auto config = RepairBench::config();
  auto configFields = ZfCf::handler<zdbtest::ConfigFields>(config).ctor();
  ZiMultiplex mx{ZvMxParams("mx", configFields.mx.p<const ZfCf::AnyNode *>())};
  check(mx.start());
  auto store = newStore();
  auto storeFields = ZfCf::handler<zdbtest::ConfigFields>(
    configFields.zdb.p<const ZfCf::AnyNode *>()).ctor();
  check(store->init(storeFields.store.p<const ZfCf::AnyNode *>(), &mx, 1, {}).is<InitData>());
  auto started = ZmBlock<StartResult>{}([store](auto done) { store->start(ZuMv(done)); });
  check(started.is<void>());
#ifdef Zfb_HH
  SchemaBuf schema(Zfb::SchemaOut::Schema<T>::Size);
  Zfb::saveSchema<T>(schema);
  auto schemaView = Schema::from<T>(schema);
#else
  auto schemaView = reflection::GetSchema(ZfbSchema<T>::data());
#endif
  auto opened = ZmBlock<OpenResult>{}([&store, schemaView](auto done) {
    store->open(false, "repairbench", ZfVFields<T>(), ZfVKeyFields<T>(),
	schemaView, Table<T>::allocBuf, ZuMv(done));
  });
  check(opened.is<OpenData>());
  auto table = opened.p<OpenData>().storeTbl;
  T row{"IBM", 42, "FIX0", "repair", 1, zdbtest::Side::Buy, {}, {},
    ZtBitmap{"0,63,64,130"}};
  row.prices.length(width, false);
  row.qtys.length(width, false);
  for (unsigned i = 0; i < width; ++i) row.prices[i] = i + 100, row.qtys[i] = i + 1;
  auto keyBuf = key(ZuStructKey<0>(row));
  using Group = SplitKey<T, 0>::GroupKey;
  auto groupBuf = key(Group{});
  Samples samples((count + 1)*11);
  UN un = 0;
  for (unsigned round = 0; round < rounds; ++round) {
    samples.length(0);
    for (unsigned i = 0; i <= count; ++i) {
      ZmRef<IOBuf> insert, update, vectorUpdate, del;
      auto insertUN = un;
      measure(samples, "codec.insert", [&row, &un, &insert]() -> uint64_t {
	insert = record<ZfFieldFilter::Save>(row, un++, 0); return insert->length;
      });
      if (!round && !i) shape("insert", insert, fieldMask<ZfFieldFilter::Save>());
      auto write = [table](ZmRef<IOBuf> buf) -> uint64_t {
	auto bytes = buf->length;
	auto result = ZmBlock<CommitResult>{}([table, buf = ZuMv(buf)](auto done) mutable {
	  table->write(ZuMv(buf), [done = ZuMv(done)](ZmRef<IOBuf>, CommitResult r) mutable {
	    done(ZuMv(r));
	  });
	});
	check(result.is<void>()); return bytes;
      };
      measure(samples, "store.insert", [&write, &insert]() { return write(ZuMv(insert)); });
      measure(samples, "store.find", [table, &keyBuf]() -> uint64_t {
	auto result = ZmBlock<RowResult>{}([table, &keyBuf](auto done) {
	  table->find(0, keyBuf, ZuMv(done));
	});
	check(result.is<RowData>()); return result.p<RowData>().buf->length;
      });
      measure(samples, "store.recover", [table, insertUN]() -> uint64_t {
	auto result = ZmBlock<RowResult>{}([table, insertUN](auto done) {
	  table->recover(0, insertUN, ZuMv(done));
	});
	check(result.is<RowData>()); return result.p<RowData>().buf->length;
      });
      measure(samples, "store.select", [table, &groupBuf]() -> uint64_t {
	uint64_t bytes = 0; unsigned rows = 0;
	ZmBlock<>{}([table, &groupBuf, &bytes, &rows](auto done) {
	  table->select(true, false, false, 0, groupBuf, 1,
	    [&bytes, &rows, done = ZuMv(done)](TupleResult r) mutable {
	      if (r.is<TupleData>()) { bytes += r.p<TupleData>().buf->length; ++rows; }
	      else { check(r.is<void>()); done(); }
	    });
	});
	check(rows == 1); return bytes;
      });
      ++row.seqNo;
      measure(samples, "codec.update", [&row, &un, &update]() -> uint64_t {
	update = record<ZfFieldFilter::Upd>(row, un++, 1); return update->length;
      });
      if (!round && !i) shape("update", update, fieldMask<ZfFieldFilter::Upd>());
      measure(samples, "store.update", [&write, &update]() { return write(ZuMv(update)); });
      row.qtys[width/2] ^= 1;
      measure(samples, "codec.update-one", [&row, &un, &vectorUpdate]() -> uint64_t {
	vectorUpdate = record<ZfFieldFilter::Upd>(row, un++, 2);
	return vectorUpdate->length;
      });
      measure(samples, "store.update-one", [&write, &vectorUpdate]() {
	return write(ZuMv(vectorUpdate));
      });
      if (!i) {
	// Untimed semantic check: both codecs must reconstruct identical data.
	auto found = ZmBlock<RowResult>{}([table, &keyBuf](auto done) {
	  table->find(0, keyBuf, ZuMv(done));
	});
	check(found.is<RowData>());
	auto buf = found.p<RowData>().buf;
#ifdef Zfb_HH
	auto bytes = record_(msg_(buf->hdr())).get<Msg::ZfField(Record, data)>();
	check(Zfb::verify<T>(bytes));
	auto loaded = Zfb::load<T>(bytes);
#else
	auto fbo = buf->fbo<T>();
	check(bool(fbo));
	auto loaded = ZfbStruct::ctor<T>(fbo);
#endif
	check(loaded.symbol == row.symbol && loaded.orderID == row.orderID &&
	  loaded.link == row.link && loaded.clOrdID == row.clOrdID &&
	  loaded.seqNo == row.seqNo && loaded.side == row.side &&
	  loaded.prices == row.prices && loaded.qtys == row.qtys && loaded.flags == row.flags);
      }
      // Both implementations receive the repaired primary-key-only contract.
      // Native core deletion and the inherited HEAD producer are measured
      // separately by the complete saga workload below.
      measure(samples, "codec.delete", [&row, &un, &del]() -> uint64_t {
	del = record<ZfFieldFilter::Del>(row, un++, -3); return del->length;
      });
      if (!round && !i)
	shape("delete", del, fieldMask<ZfFieldFilter::Del>());
      measure(samples, "store.delete", [&write, &del]() { return write(ZuMv(del)); });
    }
    output(samples, 11, round);
  }
  ZmBlock<>{}([table](auto done) { table->close(ZuMv(done)); });
  auto stopped = ZmBlock<StopResult>{}([store](auto done) { store->stop(ZuMv(done)); });
  check(stopped.is<void>());
  store->final(); store = {};
  check(mx.stop());
}

static void sagas(unsigned count, unsigned rounds, unsigned width)
{
  using SagaDB = zdbtest::LiveDB;
  using M = ZdbAnySaga<SagaDB>;
  auto config = RepairBench::config();
  auto configFields = ZfCf::handler<zdbtest::ConfigFields>(config).ctor();
  ZiMultiplex mx{ZvMxParams("mx", configFields.mx.p<const ZfCf::AnyNode *>())};
  check(mx.start());
  auto store = newStore();
  ZmRef<SagaDB> db = new SagaDB{};
  ZmSemaphore active, completed;
  active_ = &active;
  db->init(ZdbCf{configFields.zdb.p<const ZfCf::AnyNode *>()}, &mx, ZdbHandler{.upFn = up}, store);
  auto orders = db->initTable<zdbtest::Order>("o");
  ZmRef<SagaDB> context = db;
  context->orders = orders; context->done = &completed;
  db->sagas({zdbtest::liveSagaDef()});
  check(db->start()); active.wait();
  Samples samples((count + 1)*2);
  using Payload = ZdbAnySaga<zdbtest::PayloadDB>;
  Zdb_::SagaPayload data;
  data.length(width, false);
  for (unsigned i = 0; i < width; ++i) data[i] = uint8_t(i);
  ZmRef<Payload> payload = zdbtest::payloadSaga(ZuMv(data));
  uint64_t id = 0;
  for (unsigned round = 0; round < rounds; ++round) {
    samples.length(0);
    for (unsigned i = 0; i <= count; ++i) {
      ZmBlock<>{}([&db, context](auto done) {
	db->run([context, done = ZuMv(done)]() mutable {
	  context->dirs.length(0); done();
	});
      });
      measure(samples, "codec.saga-save", [&payload]() -> uint64_t {
	Zdb_::SagaPayload saved;
#ifdef Zfb_HH
	check(payload->save(saved) == Zfb::Error::OK);
#else
	payload->save(saved);
#endif
	return saved.length();
      });
      measure(samples, "saga.insert-update-delete", [&db, &completed, &id]() -> uint64_t {
	ZmRef<M> saga = zdbtest::liveSaga(42);
	ZmSemaphore submitted; bool accepted = false, success = false;
	check(db->saga(0, ++id, ZuMv(saga), [&submitted, &accepted](bool ok) {
	  accepted = ok; submitted.post();
	}, [&completed, &success](bool ok) { success = ok; completed.post(); }));
	submitted.wait(); completed.wait(); check(accepted && success);
	return 0; // Payload sizes are reported by the separate codec/store path.
      });
    }
    output(samples, 2, round);
  }
  check(!context->errors && context->inserts == id && context->updates == id &&
    context->deletes == id);
  check(db->stop()); orders = {}; db->final(); db = {}; store = {};
  check(mx.stop());
}

template <typename L>
static void schedHop(ZiMultiplex *mx, unsigned left, L &&l)
{
  if (!left) { l(); return; }
  mx->run([mx, left, l = ZuFwd<L>(l)]() mutable {
    schedHop(mx, left - 1, ZuMv(l));
  }, left & 1 ? 3 : 4);
}

// The same configured threads and callback mechanism, without a codec/store.
// This is a scheduling control, not a substitute for complete-saga latency.
static void scheduler(unsigned count, unsigned rounds)
{
  auto config = RepairBench::config();
  auto configFields = ZfCf::handler<zdbtest::ConfigFields>(config).ctor();
  ZiMultiplex mx{ZvMxParams("mx", configFields.mx.p<const ZfCf::AnyNode *>())};
  check(mx.start());
  Samples samples((count + 1)*3);
  for (unsigned round = 0; round < rounds; ++round) {
    samples.length(0);
    for (unsigned i = 0; i <= count; ++i) {
      for (unsigned hops = 2; hops <= 32; hops <<= 2) {
	ZuCSpan op = hops == 2 ? "scheduler.2-posts" :
	  hops == 8 ? "scheduler.8-posts" : "scheduler.32-posts";
	measure(samples, op, [&mx, hops]() -> uint64_t {
	  ZmBlock<>{}([&mx, hops](auto done) {
	    schedHop(&mx, hops, ZuMv(done));
	  });
	  return 0;
	});
      }
    }
    output(samples, 3, round);
  }
  check(mx.stop());
}

static int run(unsigned count, unsigned rounds, unsigned width, bool heaps = true)
{
#ifndef NDEBUG
  fputs("repair benchmark requires the retained release configuration\n", stderr);
  return 1;
#endif
  if (!count || !rounds || !width) return 1;
  heapCounts = heaps;
  std::cout << "operation,round,sample,ns,bytes,z_heap_allocs,z_heap_frees\n";
  std::cout << "#mode," << (heaps ? "heap-snapshots" : "latency-only") << '\n';
  store(count, rounds, width);
  if (getenv("ZDB_REPAIR_STORE_ONLY")) return 0;
  sagas(count, rounds, width);
  scheduler(count, rounds);
  return 0;
}

} // RepairBench

#endif /* ZdbRepairBench_HH */
