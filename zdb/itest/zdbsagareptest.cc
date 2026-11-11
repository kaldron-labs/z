//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmBlock.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZiLog.hh>
#include <zlib/ZfCf.hh>

#include "../../zdb/test/ZdbTestCf.hh"
#include <zlib/ZvMxParams.hh>
#include <zlib/Zdb.hh>

#include "ZdbMockStore.hh"
#include "ZdbTest.hh"
#include "../test/ZdbPolicyTable.hh"

namespace zdbtest {

struct PolicySmall {
  uint32_t id;
  ZtString<> value;
  friend ZuUnsigned<256> ZdbBufSize(PolicySmall *);
  friend ZuStringT<"Zdb.Repl.Small"> ZdbBufHeapID(PolicySmall *);
};
ZfStruct(, (PolicySmall, FB),
  (id, (Ctor<0>, Keys<0>), UInt32), (value, (Ctor<1>), String));
PolicyTable<PolicySmall> *ZdbTableType(PolicySmall *);
Zfb::AsTable<"zdbtest.PolicySmall"> Zfb_Fmt(PolicySmall *);

struct PolicyLarge {
  uint64_t id;
  ZtString<> value;
  friend ZuUnsigned<1024> ZdbBufSize(PolicyLarge *);
  friend ZuStringT<"Zdb.Repl.Large"> ZdbBufHeapID(PolicyLarge *);
};
ZfStruct(, (PolicyLarge, FB),
  (id, (Ctor<0>, Keys<0>), UInt64), (value, (Ctor<1>), String));
PolicyTable<PolicyLarge> *ZdbTableType(PolicyLarge *);
Zfb::AsTable<"zdbtest.PolicyLarge"> Zfb_Fmt(PolicyLarge *);

using PauseFn = ZmFn<void(bool), ZmFnHeapID<"Zdb.Saga.Repl.Pause">>;

struct PolicyDB : public Zdb {
  ZmSemaphore *up = nullptr;
};

struct ReplDB : public ZdbSagaDB {
  ZmSemaphore	*up = nullptr;
  unsigned	ups = 0;
  unsigned	doneAtUp = 0;

  ZdbTable<Order>	*orders = nullptr;
  ZmSemaphore	*paused = nullptr;
  unsigned	runs = 0;
  unsigned	inserts = 0;
  unsigned	updates = 0;
  unsigned	completed = 0;
  unsigned	errors = 0;
  bool		failUpdate = false;
  bool		pauseReverse = false;
  PauseFn	pausedComplete;
};

struct LiveSaga : public ZmNHeap<LiveSaga>, public ZdbSaga<LiveSaga, ReplDB> {
friend inline ZuStringT<"Zdb.Repl.LiveSaga"> ZmNHeapID(LiveSaga *);

  using Base = ZdbSaga<LiveSaga, ReplDB>;
  using Base::db;
  using Type = ZuStringT<"replSaga">;
  enum { NPhases = 2 };

  uint64_t orderID;

  ZdbSagaPhase(0, o, Insert) {
    ++db()->runs;
	db()->orders->run(0,
	  [this, complete = ZuMv(complete)]() mutable {
	if constexpr (Fwd) {
	  ZdbRowRef<Order> order = new ZdbRow<Order>{db()->orders, 0};
	  this->insert(db()->orders, ZuMv(order), ZuMv(complete),
	  [this](ZdbRow<Order> *row, auto &&complete) mutable {
	    ++db()->inserts;
	    new (row->ptr()) Order{
	      "IBM", orderID, "FIX0", "repl", 0, Side::Buy, {100}, {7}};
	    complete(bool(row->commit()));
	  });
	} else {
	  this->template findDel<0>(db()->orders, 0, ZuFwdTuple("IBM", orderID),
	    ZuMv(complete), [this](ZdbRow<Order> *row, auto &&complete) mutable {
	      if (db()->pauseReverse) {
		db()->pausedComplete = PauseFn{
		  [complete = ZuMv(complete)](bool ok) mutable { complete(ok); }};
		db()->paused->post();
		return;
	      }
	      complete(bool(row->commit()));
	    });
	}
	});
	return {};
  }

  ZdbSagaPhase(1, o, Update) {
	ZuAssert(Fwd);
	++db()->runs;
	db()->orders->run(0,
	  [this, complete = ZuMv(complete)]() mutable {
	this->template findUpd<0>(db()->orders, 0, ZuFwdTuple("IBM", orderID),
	  ZuMv(complete), [this](ZdbRow<Order> *row, auto &&complete) mutable {
	    ++db()->updates;
	    if (db()->failUpdate) { complete(false); return; }
	    ++row->data().qtys[0];
	    bool ok = row->commit();
	    if (db()->paused) {
	      db()->pausedComplete = PauseFn{
		[complete = ZuMv(complete)](bool ok) mutable { complete(ok); }};
	      db()->paused->post();
	      return;
	    }
	    complete(ok);
	  });
	});
	return {};
  }

  LiveSaga() = default;
  LiveSaga(uint64_t orderID_) : orderID{orderID_} { }

};

ZfStruct(, (LiveSaga, FB),
  (orderID, (Ctor<0>),		UInt64));
Zfb::AsTable<"zdbtest.fbs.LiveSaga"> Zfb_Fmt(LiveSaga *);

ZuAssert((Zdb_::SagaDefValid<LiveSaga>{}));

Zdb_::SagaDef liveSagaDef() { return LiveSaga::def(); }



} // zdbtest

using namespace ZuTestUtil;

namespace Mode {
  enum { Forward, RetrySuccess, RetryFailure };
}

template <typename T>
static bool empty(Zdb *db, ZuCSpan id)
{
  return ZmBlock<bool>{}([db, id](auto wake) {
    db->run([db, id, wake = ZuMv(wake)]() mutable {
      auto table = static_cast<ZdbTable<T> *>(db->table(id).ptr());
      table->template selectRows<0>({}, 1, [
	  wake = ZuMv(wake), empty = true
      ](auto result, unsigned) mutable {
	using Tuple = ZdbTable<T>::Tuple;
	if (result.template is<Tuple>())
	  empty = false;
	else
	  wake(empty);
      });
    });
  });
}

template <typename T>
static bool policyRecord(Zdb_::IOBuf *buf)
{
  auto record = Zdb_::record_(Zdb_::msg_(buf->hdr()));
  auto bytes = record.template get<Zdb_::Msg::ZfField(Record, data)>();
  if (!Zfb::verify<T>(bytes) ||
      (uintptr_t(bytes.data()) & (Zfb::BufferAlign<T>::N - 1))) return false;
  auto value = Zfb::load<T>(bytes);
  return value.id == 7 && value.value == "replicated-policy";
}

template <typename T>
static bool policyAllocated(Zdb_::IOBuf *buf, Zi::IOBufAllocator_ *allocator = nullptr)
{
  return dynamic_cast<zdbtest::PolicyStorage<T> *>(buf) &&
    (!allocator || buf->allocator() == allocator);
}

// Store completion observes the exact frame handed to the committing store.
// These references intentionally pin original payloads through mx.stop().
struct PolicyObserve {
  ZmSemaphore done;
  ZtArray<ZmRef<Zdb_::IOBuf>, ZtArrayHeapID<"Zdb.Repl.Held">> held;
  Zi::IOBufAllocator_ *smallAllocator = nullptr;
  Zi::IOBufAllocator_ *otherAllocator = nullptr;
  Zi::IOBufAllocator_ *largeAllocator = nullptr;
  bool receiver = false;
  bool fragmented = false;
  unsigned appFrames = 0;
  unsigned failures = 0;

  void operator ()(ZuCSpan id, Zdb_::IOBuf *original, Zdb_::IOBuf *buf, bool ok) {
    if (id != "small" && id != "other" && id != "large") return;
    unsigned failed = 0;
    auto frame = buf ? buf->frameSpan() : ZuBSpan{};
    if (!ok || original != buf || frame.length() < sizeof(Zdb_::Hdr))
      failed |= 1;
    if (frame.length() >= sizeof(Zdb_::Hdr)) {
      if (receiver) {
	auto view = dynamic_cast<Zdb_::RxBuf *>(buf);
	if (!view || !view->backing ||
	    frame.data() < view->backing->data_() ||
	    frame.data() + frame.length() > view->backing->data_() + view->backing->size)
	  failed |= 2;
	if (view && view->backing) {
	  auto type = view->backing->allocator()->type;
	  if (type == Zi::IOBufType::App) ++appFrames;
	  if ((type != Zi::IOBufType::App && type != Zi::IOBufType::RxTCP) ||
	      (fragmented && type != Zi::IOBufType::App)) failed |= 2;
	}
      } else {
	auto allocator = id == "small" ? smallAllocator :
	  id == "other" ? otherAllocator : largeAllocator;
	bool typed = id == "large" ? policyAllocated<zdbtest::PolicyLarge>(buf, allocator) :
	  policyAllocated<zdbtest::PolicySmall>(buf, allocator);
	if (!typed) failed |= 4;
      }
      auto record = Zdb_::record_(Zdb_::msg_(buf->hdr()));
      auto bytes = record.template get<Zdb_::Msg::ZfField(Record, data)>();
      auto check = [this, &bytes, &frame]<typename T>() {
	unsigned result = 0;
	if (!Zfb::verify<T>(bytes)) return 8U;
	// Typed builders provide absolute alignment. Network views retain the
	// original byte position; Zfb verifies alignment relative to that root.
	if ((!receiver && (uintptr_t(bytes.data()) & (Zfb::BufferAlign<T>::N - 1))) ||
	    bytes.data() < frame.data() ||
	    bytes.data() + bytes.length() > frame.data() + frame.length()) result |= 16;
	auto value = Zfb::load<T>(bytes);
	if (value.id != 7 || value.value != "replicated-policy") result |= 32;
	return result;
      };
      failed |= id == "large" ? check.template operator()<zdbtest::PolicyLarge>() :
	check.template operator()<zdbtest::PolicySmall>();
    }
    // Snapshot each table independently; a failure must not hide later frames.
    unsigned shift = id == "small" ? 0 : id == "other" ? 6 : 12;
    failures |= failed << shift;
    held.push(ZmRef<Zdb_::IOBuf>{buf});
    done.post();
  }
};

template <typename T>
static ZmRef<Zdb_::IOBuf> policySample(ZdbTable<T> *table)
{
  return ZmBlock<ZmRef<Zdb_::IOBuf>>{}([table](auto done) {
    table->run(0, [table, done = ZuMv(done)]() mutable { done(table->allocBuf(0)); });
  });
}

template <typename T>
static bool policyInsert(ZdbTable<T> *table)
{
  return ZmBlock<bool>{}([table](auto done) {
    table->run(0, [table, done = ZuMv(done)]() mutable {
      ZdbRowRef<T> row = new ZdbRow<T>{table, 0};
      table->insert(ZuMv(row), [done = ZuMv(done)](ZdbRow<T> *row) mutable {
	if (!row) { done(false); return; }
	new (row->ptr()) T{7, "replicated-policy"};
	done(row->commit());
      });
    });
  });
}

template <typename T>
static bool policyRead(ZdbTable<T> *table, Zi::IOBufAllocator_ *shardAllocator)
{
  using Key = ZuDecay<decltype(ZuDeclVal<T>().id)>;
  bool dispatched = ZmBlock<bool>{}([table](auto done) {
    table->run(0, [table, done = ZuMv(done)]() mutable {
      table->template find<0>(0, ZuFwdTuple(Key{7}),
	[done = ZuMv(done)](ZdbRowRef<T> row) mutable {
	  done(row && row->data().id == 7 && row->data().value == "replicated-policy");
	});
    });
  });
  auto result = ZmBlock<Zdb_::RowResult>{}([table](auto done) {
    table->recover(0, 0, ZuMv(done));
  });
  if (!dispatched || !result.template is<Zdb_::RowData>()) return false;
  auto buf = ZuMv(result).template p<Zdb_::RowData>().buf;
  bool typed = policyAllocated<T>(buf.ptr(), buf->allocator());
  if (buf->allocator()) typed &= buf->allocator() != shardAllocator;
  return typed && !buf->rep && policyRecord<T>(buf);
}

static void policyReplication(bool fragmented)
{
  ZuTestScopeRT(policyReplication);
  auto config = ZfCf::scan(
    "thread: zdb, store: {thread: store}, hostID: 0,\n"
    "hosts: {0: {priority: 100, ip: 127.0.0.1, port: 9955},\n"
    "        1: {priority: 80, ip: 127.0.0.1, port: 9956},\n"
    "        2: {priority: 60, ip: 127.0.0.1, port: 9957}},\n"
    "tables: {small: {}, other: {}, large: {}},\n"
    "mx: {nThreads: 4, threads: {\n"
    "1: {name: rx, isolated: true}, 2: {name: tx, isolated: true},\n"
    "3: {name: zdb, isolated: true}, 4: {name: store, isolated: true}\n"
    "}, rxThread: rx, txThread: tx}\n").p<1>();
  auto fields = ZfCf::handler<zdbtest::ConfigFields>(config).ctor();
  ZiMultiplex mx{ZvMxParams("mx", fields.mx.p<const ZfCf::AnyNode *>())};
#ifdef ZiMultiplex_DEBUG
  // Five bytes per fresh receive slot splits every replication frame, while
  // the normal pass permits complete/coalesced stream input.
  mx.frag(fragmented);
#endif
  ZmRef<zdbtest::PolicyDB> leader = new zdbtest::PolicyDB{},
    follower = new zdbtest::PolicyDB{}, terminal = new zdbtest::PolicyDB{};
  ZmRef<zdbtest::Store> first = new zdbtest::Store{},
    second = new zdbtest::Store{}, third = new zdbtest::Store{};
  ZmSemaphore leaderUp, followerUp, terminalUp, started;
  auto init = [&config, &mx](zdbtest::PolicyDB *db,
      zdbtest::Store *store, ZuCSpan id, ZmSemaphore *up) {
    db->up = up;
    ZdbCf cf{config};
    cf.hostID = id;
    db->init(ZuMv(cf), &mx, ZdbHandler{
      .upFn = [](Zdb *db, ZdbHost *) {
	static_cast<zdbtest::PolicyDB *>(db)->up->post();
      }
    }, store);
  };
  init(leader, first, "0", &leaderUp);
  init(follower, second, "1", &followerUp);
  init(terminal, third, "2", &terminalUp);
  auto small = leader->initTable<zdbtest::PolicySmall>("small");
  auto other = leader->initTable<zdbtest::PolicySmall>("other");
  auto large = leader->initTable<zdbtest::PolicyLarge>("large");
  auto rxSmall = follower->initTable<zdbtest::PolicySmall>("small");
  auto rxOther = follower->initTable<zdbtest::PolicySmall>("other");
  auto rxLarge = follower->initTable<zdbtest::PolicyLarge>("large");
  auto lastSmall = terminal->initTable<zdbtest::PolicySmall>("small");
  auto lastOther = terminal->initTable<zdbtest::PolicySmall>("other");
  auto lastLarge = terminal->initTable<zdbtest::PolicyLarge>("large");
  bool mxOK = mx.start();
  ZuCheckRT(mxOK);
  if (!mxOK) return;
  ZmRef<Zdb_::IOBuf> a, b, c, ra, rb, rc;
  PolicyObserve sent, received, forwarded;
  auto finish = [&]() {
    ZuCheckRT(leader->stop());
    ZuCheckRT(follower->stop());
    ZuCheckRT(terminal->stop());
    ZuCheckRT(mx.stop());
    for (const auto &buf : received.held) ZuCheckRT(bool(buf) && buf->size);
    for (const auto &buf : forwarded.held) ZuCheckRT(bool(buf) && buf->size);
    for (const auto &buf : sent.held) ZuCheckRT(bool(buf) && buf->size);
    sent.held.null(); received.held.null(); forwarded.held.null();
    a = {}; b = {}; c = {}; ra = {}; rb = {}; rc = {};
    small = {}; other = {}; large = {}; rxSmall = {}; rxOther = {}; rxLarge = {};
    lastSmall = {}; lastOther = {}; lastLarge = {};
    first->commitObserve({}); second->commitObserve({}); third->commitObserve({});
    leader->final(); follower->final(); terminal->final();
  };
  bool leaderOK = false, followerOK = false, terminalOK = false;
  ZuGuard cleanup{finish};
  leader->start([&leaderOK, &started](bool ok) { leaderOK = ok; started.post(); });
  follower->start([&followerOK, &started](bool ok) { followerOK = ok; started.post(); });
  terminal->start([&terminalOK, &started](bool ok) { terminalOK = ok; started.post(); });
  auto startup = Zm::now(10);
  bool ready = started.timedwait(startup) == 0 && started.timedwait(startup) == 0 &&
    started.timedwait(startup) == 0;
  ZuCheckRT(ready && leaderOK && followerOK && terminalOK);
  if (!ready || !leaderOK || !followerOK || !terminalOK) return;
  ready = leaderUp.timedwait(Zm::now(10)) == 0;
  ZuCheckRT(ready);
  if (!ready) return;
  a = policySample<zdbtest::PolicySmall>(small.ptr());
  b = policySample<zdbtest::PolicySmall>(other.ptr());
  c = policySample<zdbtest::PolicyLarge>(large.ptr());
  ra = policySample<zdbtest::PolicySmall>(rxSmall.ptr());
  rb = policySample<zdbtest::PolicySmall>(rxOther.ptr());
  rc = policySample<zdbtest::PolicyLarge>(rxLarge.ptr());
  ZuCheckRT(a->allocator() != b->allocator());
  ZuCheckRT(ra->allocator() != rb->allocator());
  sent.smallAllocator = a->allocator();
  sent.otherAllocator = b->allocator();
  sent.largeAllocator = c->allocator();
  received.receiver = forwarded.receiver = true;
  received.fragmented = forwarded.fragmented = fragmented;
  first->commitObserve([&sent](ZuCSpan id, auto original, auto buf, bool ok) {
    sent(id, original, buf, ok);
  });
  second->commitObserve([&received](ZuCSpan id, auto original, auto buf, bool ok) {
    received(id, original, buf, ok);
  });
  third->commitObserve([&forwarded](ZuCSpan id, auto original, auto buf, bool ok) {
    forwarded(id, original, buf, ok);
  });
  ZuCheckRT(policyInsert<zdbtest::PolicySmall>(small.ptr()));
  ZuCheckRT(policyInsert<zdbtest::PolicySmall>(other.ptr()));
  ZuCheckRT(policyInsert<zdbtest::PolicyLarge>(large.ptr()));
  auto deadline = Zm::now(10);
  bool replicated = true;
  for (unsigned i = 0; i < 3 && replicated; ++i)
    replicated = sent.done.timedwait(deadline) == 0 && received.done.timedwait(deadline) == 0 &&
      forwarded.done.timedwait(deadline) == 0;
  ZuCheckRT(replicated);
  if (!replicated) return;
  first->sync(); second->sync(); third->sync();
  log("policy failures sender=", sent.failures, " receiver=", received.failures);
  ZuCheckRT(!sent.failures);
  ZuCheckRT(!received.failures);
  ZuCheckRT(!forwarded.failures);
  if (fragmented) {
    ZuCheckRT(received.appFrames == 3);
    ZuCheckRT(forwarded.appFrames == 3);
  }
  ZuCheckRT(sent.held.length() == 3 && received.held.length() == 3 &&
    forwarded.held.length() == 3);
  ZuCheckRT(policyRead<zdbtest::PolicySmall>(rxSmall.ptr(), ra->allocator()));
  ZuCheckRT(policyRead<zdbtest::PolicySmall>(rxOther.ptr(), rb->allocator()));
  ZuCheckRT(policyRead<zdbtest::PolicyLarge>(rxLarge.ptr(), rc->allocator()));
  auto lastA = policySample<zdbtest::PolicySmall>(lastSmall.ptr());
  auto lastB = policySample<zdbtest::PolicySmall>(lastOther.ptr());
  auto lastC = policySample<zdbtest::PolicyLarge>(lastLarge.ptr());
  ZuCheckRT(policyRead<zdbtest::PolicySmall>(lastSmall.ptr(), lastA->allocator()));
  ZuCheckRT(policyRead<zdbtest::PolicySmall>(lastOther.ptr(), lastB->allocator()));
  ZuCheckRT(policyRead<zdbtest::PolicyLarge>(lastLarge.ptr(), lastC->allocator()));
  lastA = {}; lastB = {}; lastC = {};
  finish();
  cleanup.cancel();
}

static void recovery(unsigned mode)
{
  ZuTestScope(recovery);
  auto config = ZfCf::scan(
    "thread: zdb, store: {thread: store}, hostID: 0,\n"
    "hosts: {\n"
    "  0: {priority: 100, ip: 127.0.0.1, port: 9945},\n"
    "  1: {priority: 80, ip: 127.0.0.1, port: 9946}\n"
    "}, tables: {o: {}},\n"
    "mx: {nThreads: 4, threads: {\n"
    "  1: {name: rx, isolated: true},\n"
    "  2: {name: tx, isolated: true},\n"
    "  3: {name: zdb, isolated: true},\n"
    "  4: {name: store, isolated: true}\n"
    "}, rxThread: rx, txThread: tx}\n").p<1>();
  auto configFields = ZfCf::handler<zdbtest::ConfigFields>(config).ctor();
  ZiMultiplex mx{ZvMxParams("mx", configFields.mx.p<const ZfCf::AnyNode *>())};

  ZmRef<zdbtest::ReplDB> leader = new zdbtest::ReplDB{};
  ZmRef<zdbtest::ReplDB> follower = new zdbtest::ReplDB{};
  ZmRef<zdbtest::Store> firstStore = new zdbtest::Store{};
  ZmRef<zdbtest::Store> secondStore = new zdbtest::Store{};
  auto init = [&config, &mx](zdbtest::ReplDB *db,
      zdbtest::Store *store, ZuCSpan id) {
    ZdbCf cf{config};
    cf.hostID = id;
    db->init(ZuMv(cf), &mx, ZdbHandler{
      .upFn = [](Zdb *db_, ZdbHost *) {
	auto db = static_cast<zdbtest::ReplDB *>(db_);
	++db->ups;
	db->doneAtUp = db->completed;
	db->up->post();
      }
    }, store);
    db->sagas({zdbtest::liveSagaDef()});
  };
  init(leader, firstStore, "0");
  init(follower, secondStore, "1");
  auto firstOrders = leader->initTable<zdbtest::Order>("o");
  auto secondOrders = follower->initTable<zdbtest::Order>("o");
  leader->orders = firstOrders;
  follower->orders = secondOrders;
  if (mode != Mode::Forward) {
    leader->failUpdate = true;
    leader->pauseReverse = true;
    follower->failUpdate = mode == Mode::RetryFailure;
  }
  ZuCheck(mx.start());
  ZmSemaphore firstUp, secondUp, paused, replicated, started;
  leader->up = &firstUp;
  follower->up = &secondUp;
  leader->paused = &paused;
  unsigned writes = 0; // committing-shard-owned; observed through replicated
  secondStore->writeFn([mode, &writes, &replicated](ZuCSpan id) {
    if (mode == Mode::Forward) {
      if (id == "o" && ++writes == 2) replicated.post();
    } else if (id == "saga_step" && ++writes == 3)
      replicated.post();
  });
  bool firstOK = false, secondOK = false;
  leader->start([&firstOK, &started](bool ok) {
    firstOK = ok;
    started.post();
  });
  follower->start([&secondOK, &started](bool ok) {
    secondOK = ok;
    started.post();
  });
  started.wait();
  started.wait();
  ZuCheck(firstOK && secondOK);
  firstUp.wait();
  bool standby = ZmBlock<bool>{}([follower = follower.ptr()](auto wake) {
    follower->run([follower, wake = ZuMv(wake)]() mutable {
      wake(!follower->active() && !follower->ups && !follower->runs);
    });
  });
  ZuCheck(standby);

  ZmRef<ZdbAnySaga<zdbtest::ReplDB>> saga = new zdbtest::LiveSaga{42};
  bool admitted = ZmBlock<bool>{}([
      leader = leader.ptr(), saga = ZuMv(saga)](auto wake) mutable {
    leader->saga(0, 1, ZuMv(saga),
      [wake = ZuMv(wake)](bool ok) mutable { wake(ok); },
      [context = leader](bool ok) {
	if (ok) ++context->completed; else ++context->errors;
      });
  });
  ZuCheck(admitted);
  paused.wait();
  replicated.wait();
  // Test coordination only: finish the observed follower store write.
  secondStore->sync();
  ZuCheck(leader->inserts == 1 && leader->updates == 1);
  leader->run([leader = leader.ptr()]() {
    leader->fail();
    leader->pausedComplete(false);
  });
  ZuCheck(leader->stop());
  secondUp.wait();
  ZuCheck(follower->ups == 1 && !follower->doneAtUp);
  ZuCheck(follower->runs ==
    unsigned(mode == Mode::RetryFailure ? 3 : 2));
  ZuCheck(!follower->inserts);
  ZuCheck(follower->updates == unsigned(mode != Mode::Forward));
  ZuCheck(!leader->errors && !follower->errors);

  bool once = ZmBlock<bool>{}([
      orders = secondOrders.ptr(), mode](auto wake) {
    orders->run(0, [orders, mode, wake = ZuMv(wake)]() mutable {
      orders->find<0>(0, ZuFwdTuple("IBM", uint64_t(42)),
	[mode, wake = ZuMv(wake)](ZdbRowRef<zdbtest::Order> row) mutable {
	  wake(mode == Mode::RetryFailure ? !row :
	    row && row->data().qtys[0] == 8);
	});
    });
  });
  ZuCheck(once);
  ZuCheck(empty<Zdb_::SagaData>(follower, "saga"));
  ZuCheck(empty<Zdb_::SagaStep>(follower, "saga_step"));
  ZuCheck(follower->stop());
  firstOrders = {};
  secondOrders = {};
  ZuCheck(mx.stop());
  leader->final();
  follower->final();
  leader = {};
  follower = {};
  firstStore = {};
  secondStore = {};
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZiLog::init("zdbsagareptest");
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();
  ZuTestMain();
  ZuTestCall(policyReplication, false);
#ifdef ZiMultiplex_DEBUG
  ZuTestCall(policyReplication, true);
#endif
  ZuTestCall(recovery, Mode::Forward);
  ZuTestCall(recovery, Mode::RetrySuccess);
  ZuTestCall(recovery, Mode::RetryFailure);
  ZiLog::stop();
  return 0;
}
