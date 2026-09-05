//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// PostgreSQL saga journaling/recovery across two distinct shard threads.

#include <stdlib.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <errno.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZfCf.hh>
#include <zlib/ZfCLI.hh>
#include <zlib/ZvMxParams.hh>
#include <zlib/ZiLog.hh>
#include <zlib/Zdb.hh>

#include "ZdbTest.hh"

using namespace ZuTestUtil;

namespace zdbtest {

namespace Fault { enum { None, NoCommit }; }

struct Context {
  ZdbTable<Order>	*orders = nullptr;
  ZdbTable<Order>	*items = nullptr;
  ZmSemaphore	*paused = nullptr;
  ZmSemaphore	*done = nullptr;
  unsigned		runs = 0;
  unsigned		inserts = 0;
  unsigned		updates = 0;
  unsigned		deletes = 0;
  unsigned		fault = Fault::None;
};

// Reuse the test intent schema; runtime context is deliberately not saved.
struct LiveSaga {
  using Type = ZuStringT<"pqSaga1">;
  using Steps = ZdbSagaSteps(
    (pq_saga_order, Insert), (pq_saga_order, Update),
    (pq_saga_item, Delete));
  uint64_t orderID = 0;

  void operator ()(void *context_, ZmRef<ZdbSaga> saga) {
    auto context = static_cast<Context *>(context_);
    ++context->runs;
    switch (saga->step()) {
      case 0: {
	ZdbObjRef<Order> object = new ZdbObject<Order>{context->orders, 0};
	saga->insert(context->orders, ZuMv(object),
	  [context, id = orderID](ZdbObject<Order> *object) {
	    ++context->inserts;
	    if (!object) return;
	    new (object->ptr()) Order{
	      "SAGA", id, "PQ", "saga", 0, Side::Buy, {100}, {7}};
	    object->commit();
	  });
	return;
      }
      case 1:
	saga->findUpd<0>(context->orders, 0, ZuFwdTuple("SAGA", orderID),
	  [context](ZdbObject<Order> *object) {
	    ++context->updates;
	    if (!object) return;
	    ++object->data().qtys[0]; // UN replay must not increment this twice
	    if (context->fault == Fault::NoCommit) return;
	    object->commit();
	  });
	return;
      case 2:
	if (context->paused) {
	  context->paused->post();
	  return;
	}
	saga->findDel<0>(context->items, 1, ZuFwdTuple("SAGA", orderID),
	  [context](ZdbObject<Order> *object) {
	    ++context->deletes;
	    if (object) object->commit();
	  });
	return;
      default:
	saga->done();
	return;
    }
  }
};

ZfbStruct(LiveSaga,
  (((orderID), (Ctor<0>)), (UInt64)));

using Sagas = ZuTypeList<LiveSaga>;

} // zdbtest

using DB = ZdbSagaDB<zdbtest::Sagas>;

namespace Mode { enum { Live, Reconnect, Crash, Replay, NoCommit }; }
enum { CrashStatus = 73 }; // distinguishes the deliberate cut from a failed child

struct Options {
  bool crash = false;
  bool uncommitted = false;
};
ZfStruct((Options, CLI),
  (((crash), (CLI::Flag<'c'>, CLI::Long<"crash">)), (Bool)),
  (((uncommitted), (CLI::Flag<'u'>, CLI::Long<"uncommitted">)), (Bool)));

static ZuPtr<const ZfCf::AnyNode> config()
{
  ZmRef<ZfCf::Defines> defines = new ZfCf::Defines{};
  defines->add(ZfCf::DefKey{"MODULE"}, ZfCf::DefVal{getenv("ZDB_MODULE")});
  defines->add(ZfCf::DefKey{"CONNECT"}, ZfCf::DefVal{getenv("ZDB_CONNECT")});
  auto scan = ZfCf::scan(
    "thread: zdb, shards: 4, threads: [saga0, saga1, saga2, saga3],\n"
    "hostID: self, hosts: {self: {standalone: true}},\n"
    "store: {thread: store, replicated: true,\n"
    "  module: ${MODULE}, connection: ${CONNECT}},\n"
    "tables: {pq_saga_order: {cacheMode: All},\n"
    "  pq_saga_item: {cacheMode: All}},\n"
    "mx: {nThreads: 8, rxThread: rx, txThread: tx, threads: {\n"
    "  1: {name: rx, isolated: true},\n"
    "  2: {name: tx, isolated: true},\n"
    "  3: {name: zdb, isolated: true},\n"
    "  4: {name: store, isolated: true},\n"
    "  5: {name: saga0, isolated: true},\n"
    "  6: {name: saga1, isolated: true},\n"
    "  7: {name: saga2, isolated: true},\n"
    "  8: {name: saga3, isolated: true}}}\n", {}, ZuMv(defines));
  return ZuMv(scan.p<1>());
}

static bool erase(ZdbTable<zdbtest::Order> *table, ZdbShard shard)
{
  return ZmBlock<bool>{}([table, shard](auto wake) {
    table->run(shard, [table, shard, wake = ZuMv(wake)]() mutable {
      table->findDel<0>(shard, ZuFwdTuple("SAGA", UINT64_C(1)),
	[wake = ZuMv(wake)](ZdbObject<zdbtest::Order> *object) mutable {
	  wake(!object || object->commit());
	});
    });
  });
}

static bool seed(ZdbTable<zdbtest::Order> *table)
{
  return ZmBlock<bool>{}([table](auto wake) {
    table->run(1, [table, wake = ZuMv(wake)]() mutable {
      ZdbObjRef<zdbtest::Order> object = new ZdbObject<zdbtest::Order>{table, 1};
      table->insert(object,
	[wake = ZuMv(wake)](ZdbObject<zdbtest::Order> *object) mutable {
	  if (!object) { wake(false); return; }
	  new (object->ptr()) zdbtest::Order{
	    "SAGA", 1, "PQ", "saga", 0, zdbtest::Side::Buy, {100}, {1}};
	  wake(object->commit());
	});
    });
  });
}

static void completed(void *context, ZuCSpan, ZdbSagaID)
{
  auto done = static_cast<zdbtest::Context *>(context)->done;
  if (done) done->post();
}

static void exercise(unsigned mode)
{
  ZuTestScopeRT(exercise);
  auto cf = config();
  ZiMultiplex mx{ZvMxParams{"mx", cf->resolve("mx")}};
  ZuCheckRT(mx.start());
  ZmRef<DB> db = new DB{};
  db->init(ZdbCf{cf}, &mx, {});
  auto orders = db->initTable<zdbtest::Order>("pq_saga_order");
  auto items = db->initTable<zdbtest::Order>("pq_saga_item");
  zdbtest::Context context{.orders = orders, .items = items};
  db->sagas(ZdbSagaHandler{.context = &context, .doneFn = completed});
  bool started = db->start();
  ZuCheckRT(started);
  if (!started) {
    orders = {}; items = {};
    db->final();
    mx.stop();
    return;
  }

  ZmSemaphore paused;
  ZmSemaphore done;
  if (mode != Mode::Replay) {
    // Remove only this fixture's rows; repeated test runs share the test DB.
    ZuCheckRT(erase(orders, 0));
    ZuCheckRT(erase(items, 1));
    ZuCheckRT(seed(items));
    context = zdbtest::Context{
      .orders = orders, .items = items,
      .paused = mode != Mode::Live ? &paused : nullptr, .done = &done,
      .fault = mode == Mode::NoCommit ?
	zdbtest::Fault::NoCommit : zdbtest::Fault::None};
    ZmRef<ZdbMSaga<zdbtest::Sagas>> saga = new ZdbMSaga<zdbtest::Sagas>{};
    saga->init(zdbtest::LiveSaga{1});
    bool admitted = ZmBlock<bool>{}([db = db.ptr(), saga = ZuMv(saga)](auto wake) mutable {
      // The main row belongs to neither of the target shards (0 and 1).
      db->saga(2, 1, ZuMv(saga),
	[wake = ZuMv(wake)](ZdbSagaSubmitResult result) mutable {
	  wake(result.is<void>());
	});
    });
    ZuCheckRT(admitted);
    if (mode != Mode::Live) {
      paused.wait();
      if (mode == Mode::NoCommit)
	_Exit(1); // reaching the next step would violate the fatal-error contract
      ZuCheckRT(context.inserts == 1);
      ZuCheckRT(context.updates == 1);
      if (mode == Mode::Crash) {
	// A backend SELECT establishes this exact durable cut. Do not call
	// stop/final or run destructors: all DB state is lost with the process.
	bool durable = false;
	ZmBlock<>{}([table = orders.ptr(), &durable](auto wake) {
	  table->selectRows<0>({}, 1,
	    [&durable, wake = ZuMv(wake)](auto result, unsigned) mutable {
	      using Tuple = ZdbTable<zdbtest::Order>::Tuple;
	      if (result.template is<Tuple>()) {
		auto &row = result.template p<Tuple>();
		durable = row.template p<1>() == 1 &&
		  row.template p<7>().length() == 1 && row.template p<7>()[0] == 8;
	      } else wake();
	    });
	});
	_Exit(admitted && durable && context.inserts == 1 &&
	  context.updates == 1 ? CrashStatus : 1);
      }
      db->run([db = db.ptr()]() { db->fail(); });
      // This intentionally interrupted saga is preserved; stop flushes the
      // driver before a fresh connection reopens it. This is not a hard-crash test.
      ZuCheckRT(db->stop());
      orders = {}; items = {};
      db->final();
      db->init(ZdbCf{cf}, &mx, {});
      orders = db->initTable<zdbtest::Order>("pq_saga_order");
      items = db->initTable<zdbtest::Order>("pq_saga_item");
      context = zdbtest::Context{.orders = orders, .items = items, .done = &done};
      db->sagas(ZdbSagaHandler{.context = &context, .doneFn = completed});
      started = db->start();
      ZuCheckRT(started);
      if (!started) {
	orders = {}; items = {};
	db->final();
	mx.stop();
	return;
      }
    }
    done.wait();
  }
  ZuCheckRT(context.runs == 4);
  ZuCheckRT(context.inserts == unsigned(mode == Mode::Live));
  ZuCheckRT(context.updates == unsigned(mode == Mode::Live));
  ZuCheckRT(context.deletes == 1);

  bool updated = ZmBlock<bool>{}([table = orders.ptr()](auto wake) {
    table->run(0, [table, wake = ZuMv(wake)]() mutable {
      table->find<0>(0, ZuFwdTuple("SAGA", UINT64_C(1)),
	[wake = ZuMv(wake)](ZdbObjRef<zdbtest::Order> object) mutable {
	  wake(object && object->data().qtys[0] == 8);
	});
    });
  });
  ZuCheckRT(updated);
  bool clean = ZmBlock<bool>{}([db = db.ptr()](auto wake) {
    db->run([db, wake = ZuMv(wake)]() mutable {
      wake(db->table("saga")->count() == 0 &&
	db->table("saga_step")->count() == 0 &&
	db->table("pq_saga_item")->count() == 0);
    });
  });
  ZuCheckRT(clean);
  ZuCheckRT(db->stop());
  orders = {}; items = {};
  db->final();
  db = {};
  ZuCheckRT(mx.stop());
}

static Zdb_::OpenResult open(
    Zdb_::Store *store, bool internal, Zdb_::IDString id)
{
  using namespace Zdb_;
  using zdbtest::Order;
  return ZmBlock<OpenResult>{}([store, internal, id = ZuMv(id)](auto wake) mutable {
    store->open(internal, ZuMv(id), ZfVFields<Order>(), ZfVKeyFields<Order>(),
      reflection::GetSchema(ZfbSchema<Order>::data()), Table<Order>::allocBuf,
      [wake = ZuMv(wake)](OpenResult result) mutable {
	if (result.is<ZeException>()) ZiLogEvent(result.p<ZeException>());
	wake(ZuMv(result));
      });
  });
}

static void close(Zdb_::StoreTbl *table)
{
  ZmBlock<>{}([table](auto wake) {
    table->close([wake = ZuMv(wake)]() mutable { wake(); });
  });
}

static bool write(Zdb_::OpenData &state, ZuCSpan id, int vn, int qty)
{
  using namespace Zdb_;
  using zdbtest::Order;
  UN un = state.un[0] == nullUN() ? 0 : state.un[0] + 1;
  SN sn = state.sn + 1;
  Order row{"NS", 1, "NS", "probe", 0, zdbtest::Side::Buy, {100}, {qty}};
  Zfb::IOBuilder fbb{Table<Order>::allocBuf()};
  auto data = Zfb::Save::nest(fbb, [&row](Zfb::Builder &fbb) {
    return ZfbStruct::save(fbb, row).Union();
  });
  auto sn_ = ZfbTransform::UInt128::save(sn);
  fbb.Finish(fbs::CreateMsg(fbb, fbs::Body::Replication,
    fbs::CreateRecord(fbb, Zfb::Save::str(fbb, id),
      un, &sn_, vn, 0, data).Union()));
  bool ok = ZmBlock<bool>{}([table = state.storeTbl, buf = saveHdr(fbb)](auto wake) mutable {
    table->write(ZuMv(buf),
      [wake = ZuMv(wake)](ZmRef<IOBuf>, CommitResult result) mutable {
	wake(result.is<void>());
      });
  });
  if (ok) { state.un[0] = un; state.sn = sn; }
  return ok;
}

static int read(Zdb_::StoreTbl *table)
{
  using namespace Zdb_;
  using zdbtest::Order;
  Table<Order>::Key<0> key{"NS", 1};
  Zfb::IOBuilder fbb{Table<Order>::allocBuf()};
  fbb.Finish(ZfbStruct::save(fbb, key));
  return ZmBlock<int>{}([table, buf = fbb.buf()](auto wake) mutable {
    table->find(0, ZuMv(buf), [wake = ZuMv(wake)](RowResult result) mutable {
      if (result.is<void>()) { wake(-1); return; }
      if (!result.is<RowData>()) { wake(-2); return; }
      auto buf = ZuMv(result).p<RowData>().buf;
      auto rec = record(msg(buf->hdr()));
      auto fbo = rec ? ZfbStruct::verify<Order>(Zfb::Load::bytes(rec->data())) : nullptr;
      if (!fbo) { wake(-2); return; }
      auto row = ZfbStruct::ctor<Order>(fbo);
      wake(row.qtys.length() == 1 ? row.qtys[0] : -2);
    });
  });
}

static void namespaces(bool maxID)
{
  ZuTestScopeRT(namespaces);
  using namespace Zdb_;
  IDString id{"saga_ns"};
  if (maxID)
    while (id.length() < IDSize_ - 1) id << 'n';
  auto cf = config();
  ZiMultiplex mx{ZvMxParams{"mx", cf->resolve("mx")}};
  ZuCheckRT(mx.start());
  ZmRef<Zdb> db = new Zdb{};
  db->init(ZdbCf{cf}, &mx, {});
  auto orders = db->initTable<zdbtest::Order>("pq_saga_order");
  bool started = db->start();
  ZuCheckRT(started);
  if (!started) {
    orders = {};
    db->final();
    db = {};
    ZuCheckRT(mx.stop());
    return;
  }
  auto app = open(db->store(), false, id);
  auto internal = open(db->store(), true, id);
  bool opened = app.is<OpenData>() && internal.is<OpenData>();
  ZuCheckRT(opened);
  if (opened) {
    auto &a = app.p<OpenData>();
    auto &b = internal.p<OpenData>();
    ZuCheckRT(a.storeTbl != b.storeTbl);
    // Clean only this fixture's key if an earlier failed run left it behind.
    if (a.count) ZuCheckRT(write(a, id, -1, 0));
    if (b.count) ZuCheckRT(write(b, id, -1, 0));
    ZuCheckRT(write(a, id, 0, 11));
    ZuCheckRT(write(b, id, 0, 22));
    ZuCheckRT(read(a.storeTbl) == 11 && read(b.storeTbl) == 22);
    ZuCheckRT(write(a, id, 1, 12));
    ZuCheckRT(write(b, id, 1, 23));
    ZuCheckRT(write(a, id, 2, 13)); // deliberately different UN/SN histories
    ZuCheckRT(read(a.storeTbl) == 13 && read(b.storeTbl) == 23);
    close(a.storeTbl);
    close(b.storeTbl);
    auto app2 = open(db->store(), false, id);
    auto internal2 = open(db->store(), true, id);
    bool reopened = app2.is<OpenData>() && internal2.is<OpenData>();
    ZuCheckRT(reopened);
    if (reopened) {
      auto &a2 = app2.p<OpenData>();
      auto &b2 = internal2.p<OpenData>();
      ZuCheckRT(a2.count == 1 && a2.un[0] == a.un[0] && a2.sn == a.sn);
      ZuCheckRT(b2.count == 1 && b2.un[0] == b.un[0] && b2.sn == b.sn);
      ZuCheckRT(read(a2.storeTbl) == 13 && read(b2.storeTbl) == 23);
      ZuCheckRT(write(a2, id, -3, 0));
      ZuCheckRT(write(b2, id, -2, 0));
      ZuCheckRT(read(a2.storeTbl) == -1 && read(b2.storeTbl) == -1);
      close(a2.storeTbl);
      close(b2.storeTbl);
      // Reconnect with retained handles, then repeat with an entirely new store.
      for (unsigned restart = 0; restart < 2; ++restart) {
	ZuCheckRT(db->stop());
	if (restart) {
	  orders = {};
	  db->final();
	  db->init(ZdbCf{cf}, &mx, {});
	  orders = db->initTable<zdbtest::Order>("pq_saga_order");
	}
	bool started = db->start();
	ZuCheckRT(started);
	if (!started) break;
	auto app3 = open(db->store(), false, id);
	auto internal3 = open(db->store(), true, id);
	bool deleted = app3.is<OpenData>() && internal3.is<OpenData>();
	ZuCheckRT(deleted);
	if (deleted) {
	  auto &a3 = app3.p<OpenData>();
	  auto &b3 = internal3.p<OpenData>();
	  // With no rows left, only MRD can restore these positions.
	  ZuCheckRT(a3.count == 0 && a3.un[0] == a2.un[0] && a3.sn == a2.sn);
	  ZuCheckRT(b3.count == 0 && b3.un[0] == b2.un[0] && b3.sn == b2.sn);
	}
	if (app3.is<OpenData>()) close(app3.p<OpenData>().storeTbl);
	if (internal3.is<OpenData>()) close(internal3.p<OpenData>().storeTbl);
      }
    } else {
      if (app2.is<OpenData>()) close(app2.p<OpenData>().storeTbl);
      if (internal2.is<OpenData>()) close(internal2.p<OpenData>().storeTbl);
    }
  } else {
    if (app.is<OpenData>()) close(app.p<OpenData>().storeTbl);
    if (internal.is<OpenData>()) close(internal.p<OpenData>().storeTbl);
  }
  ZuCheckRT(db->stop());
  orders = {};
  db->final();
  db = {};
  ZuCheckRT(mx.stop());
}

static int child(const char *self, const char *option)
{
#ifndef _WIN32
  pid_t pid = fork();
  if (pid < 0) return -1;
  if (!pid) {
    // Keep child TAP out of the parent's TAP stream. After fork, only use
    // async-signal-safe operations until exec replaces the threaded process.
    if (dup2(STDERR_FILENO, STDOUT_FILENO) < 0) _exit(127);
    execl(self, self, option, static_cast<char *>(nullptr));
    _exit(127);
  }
  int status;
  pid_t result;
  do { result = waitpid(pid, &status, 0); } while (result < 0 && errno == EINTR);
  if (result != pid) return -1;
  if (WIFSIGNALED(status)) return -WTERMSIG(status);
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#else
  ZtString<> command;
  ZfCLI::CmdQuote::quote(command, self);
  command << ' ' << option;
  STARTUPINFOA start{};
  start.cb = sizeof(start);
  start.dwFlags = STARTF_USESTDHANDLES;
  start.hStdOutput = start.hStdError = GetStdHandle(STD_ERROR_HANDLE);
  start.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  PROCESS_INFORMATION process{};
  if (!CreateProcessA(nullptr, command.data(), nullptr, nullptr, TRUE, 0,
      nullptr, nullptr, &start, &process)) return -1;
  DWORD status = DWORD(-1);
  if (WaitForSingleObject(process.hProcess, INFINITE) == WAIT_OBJECT_0)
    GetExitCodeProcess(process.hProcess, &status);
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  return int(status);
#endif
}

static void crash(const char *self)
{
  ZuTestScopeRT(crash);
  bool cut = child(self, "--crash") == CrashStatus;
  ZuCheckRT(cut);
  if (cut) ZuTestCallRT(exercise, Mode::Replay);
}

static void fatal(const char *self, const char *option)
{
  ZuTestScopeRT(fatal);
#ifdef _WIN32
  enum { AbortStatus = 3 }; // MinGW CRT abort exit status
#else
  enum { AbortStatus = -SIGABRT };
#endif
  bool aborted = child(self, option) == AbortStatus;
  ZuCheckRT(aborted);
  if (aborted) ZuTestCallRT(exercise, Mode::Live);
}

int main(int argc, char **argv)
{
  Options options;
  if (ZfCLI::load(options, argc, argv) != 1) return 1;
#ifndef _WIN32
  if (options.uncommitted) {
    // These children deliberately abort; do not leave core dumps in the tree.
    struct rlimit limit{0, 0};
    if (setrlimit(RLIMIT_CORE, &limit)) return 1;
  }
#endif
  ZiLog::init("zdbsagatest");
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();
  if (options.crash || options.uncommitted) {
    ZuTestMain();
    ZuTestCall(exercise, options.crash ? Mode::Crash : Mode::NoCommit);
    return 1; // a successful cut never returns
  }
  ZuTestMain();
  ZuTestCall(namespaces, false);
  ZuTestCall(namespaces, true);
  ZuTestCall(exercise, Mode::Live);
  ZuTestCall(exercise, Mode::Reconnect);
  ZuTestCall(crash, argv[0]);
  ZuTestCall(fatal, argv[0], "--uncommitted");
  ZiLog::stop();
}
