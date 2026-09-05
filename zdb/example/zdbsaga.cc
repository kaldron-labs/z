//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Canonical Zdb saga example: transfer a balance between two accounts.

#include <stdint.h>

#include <iostream>

#include <zlib/ZuLib.hh>
#include <zlib/ZuTuple.hh>

#include <zlib/ZmBlock.hh>
#include <zlib/ZmRef.hh>
#include <zlib/ZmSemaphore.hh>

#include <zlib/ZfCf.hh>

#include <zlib/ZiLog.hh>

#include <zlib/ZvMxParams.hh>

#include <zlib/Zfb.hh>
#include <zlib/ZfbStruct.hh>

#include <zlib/Zdb.hh>
#include <zlib/ZdbMemStore.hh>

#include "zdbsaga_account_fbs.h"
#include "zdbsaga_intent_fbs.h"
#include "zdbsaga_transfer_fbs.h"

namespace zdbsaga {

struct Account {
  uint64_t	id = 0;
  int64_t	balance = 0;

  friend ZuStringT<"Zdb.Example.Account"> ZdbHeapID(Account *);
  friend ZuUnsigned<128> ZdbBufSize(Account *);
  friend ZuStringT<"Zdb.Example.Account.Buf"> ZdbBufHeapID(Account *);
};

ZfbStruct(Account,
  (((id),	(Ctor<0>, Keys<0>)),	(UInt64)),
  (((balance),	(Ctor<1>, Mutable)),	(Int64)));

ZfbRoot(Account);

ZfbEnumNS(, TransferStatus, Pending, Complete)
ZtEnumImplNS(TransferStatus);

struct Transfer {
  uint64_t	id = 0;
  uint64_t	fromID = 0;
  uint64_t	toID = 0;
  int64_t	amount = 0;
  int8_t	status = TransferStatus::Pending;

  friend ZuStringT<"Zdb.Example.Transfer"> ZdbHeapID(Transfer *);
  friend ZuUnsigned<192> ZdbBufSize(Transfer *);
  friend ZuStringT<"Zdb.Example.Transfer.Buf"> ZdbBufHeapID(Transfer *);
};

ZfbStruct(Transfer,
  (((id),	(Ctor<0>, Keys<0>)),	(UInt64)),
  (((fromID),	(Ctor<1>)),		(UInt64)),
  (((toID),	(Ctor<2>)),		(UInt64)),
  (((amount),	(Ctor<3>)),		(Int64)),
  (((status),	(Ctor<4>, Enum<TransferStatus::Map>, Mutable)), (Int8)));

ZfbRoot(Transfer);

struct Context {
  ZdbTable<Transfer>	*transfers = nullptr;
  ZdbTable<Account>	*accounts = nullptr;
  ZmSemaphore		completed;
  bool			failed = false;
};

struct BalanceTransfer {
  using Type = ZuStringT<"balanceTransfer">;
  using Steps = ZdbSagaSteps(
    (transfer, Insert), (account, Update),
    (account, Update), (transfer, Update));

  uint64_t	transferID = 0;
  uint64_t	fromID = 0;
  uint64_t	toID = 0;
  int64_t	amount = 0;
  ZdbShard	transferShard = 0;
  ZdbShard	fromShard = 0;
  ZdbShard	toShard = 0;

  void operator ()(void *context_, ZmRef<ZdbSaga> saga) {
    auto context = static_cast<Context *>(context_);
    switch (saga->step()) {
      case 0: {
	ZdbObjRef<Transfer> object =
	  new ZdbObject<Transfer>{context->transfers, transferShard};
	saga->insert(context->transfers, ZuMv(object), [
	  transferID = transferID, fromID = fromID, toID = toID,
	  amount = amount
	](ZdbObject<Transfer> *object) {
	  new (object->ptr()) Transfer{
	    transferID, fromID, toID, amount, TransferStatus::Pending};
	  object->commit();
	});
	return;
      }
      case 1:
	saga->findUpd<0>(context->accounts, fromShard, ZuFwdTuple(fromID),
	  [amount = amount](ZdbObject<Account> *object) {
	    object->data().balance -= amount;
	    object->commit();
	  });
	return;
      case 2:
	saga->findUpd<0>(context->accounts, toShard, ZuFwdTuple(toID),
	  [amount = amount](ZdbObject<Account> *object) {
	    object->data().balance += amount;
	    object->commit();
	  });
	return;
      case 3:
	saga->findUpd<0>(
	  context->transfers, transferShard, ZuFwdTuple(transferID),
	  [](ZdbObject<Transfer> *object) {
	    object->data().status = TransferStatus::Complete;
	    object->commit();
	  });
	return;
      default:
	saga->done();
	return;
    }
  }
};

ZfbStruct(BalanceTransfer,
  (((transferID),	(Ctor<0>)),	(UInt64)),
  (((fromID),		(Ctor<1>)),	(UInt64)),
  (((toID),		(Ctor<2>)),	(UInt64)),
  (((amount),		(Ctor<3>)),	(Int64)),
  (((transferShard),	(Ctor<4>)),	(UInt8)),
  (((fromShard),	(Ctor<5>)),	(UInt8)),
  (((toShard),		(Ctor<6>)),	(UInt8)));

using Sagas = ZuTypeList<BalanceTransfer>;
using Saga = ZdbMSaga<Sagas>;

struct DB : public ZdbSagaDB<Sagas> {
  ZmSemaphore active;
};

static ZuPtr<const ZfCf::AnyNode> config()
{
  auto scan = ZfCf::scan(
    "zdb: {\n"
    "  thread: zdb, shards: 2, threads: [shard0, shard1],\n"
    "  store: {thread: store},\n"
    "  hostID: self, hosts: {self: {standalone: true}},\n"
    "  tables: {transfer: {cacheMode: All}, account: {cacheMode: All}}\n"
    "},\n"
    "mx: {\n"
    "  nThreads: 6, rxThread: rx, txThread: tx, threads: {\n"
    "    1: {name: rx, isolated: true},\n"
    "    2: {name: tx, isolated: true},\n"
    "    3: {name: zdb, isolated: true},\n"
    "    4: {name: store, isolated: true},\n"
    "    5: {name: shard0, isolated: true},\n"
    "    6: {name: shard1, isolated: true}\n"
    "  }\n"
    "}\n");
  return ZuMv(scan.p<1>());
}

static bool accountInsert(
    ZdbTable<Account> *table, ZdbShard shard, uint64_t id, int64_t balance)
{
  return ZmBlock<bool>{}([
      table, shard, id, balance](auto wake) mutable {
    table->run(shard, [
	table, shard, id, balance, wake = ZuMv(wake)]() mutable {
      ZdbObjRef<Account> object = new ZdbObject<Account>{table, shard};
      table->insert(object, [
	id, balance, wake = ZuMv(wake)](ZdbObject<Account> *object) mutable {
	if (!object) { wake(false); return; }
	new (object->ptr()) Account{id, balance};
	wake(bool(object->commit()));
      });
    });
  });
}

static bool accountBalance(
    ZdbTable<Account> *table, ZdbShard shard, uint64_t id, int64_t &balance)
{
  return ZmBlock<bool>{}([
      table, shard, id, &balance](auto wake) mutable {
    table->run(shard, [
	table, shard, id, &balance, wake = ZuMv(wake)]() mutable {
      table->find<0>(shard, ZuFwdTuple(id), [
	  &balance, wake = ZuMv(wake)](ZdbObjRef<Account> object) mutable {
	if (!object) { wake(false); return; }
	balance = object->data().balance;
	wake(true);
      });
    });
  });
}

static bool transferComplete(
    ZdbTable<Transfer> *table, ZdbShard shard, uint64_t id)
{
  return ZmBlock<bool>{}([table, shard, id](auto wake) mutable {
    table->run(shard, [
	table, shard, id, wake = ZuMv(wake)]() mutable {
      table->find<0>(shard, ZuFwdTuple(id), [
	  wake = ZuMv(wake)](ZdbObjRef<Transfer> object) mutable {
	wake(object && object->data().status == TransferStatus::Complete);
      });
    });
  });
}

static int run()
{
  auto cf = config();
  ZiMultiplex mx{ZvMxParams{"mx", cf->resolve("mx")}};
  if (!mx.start()) return 1;

  ZmRef<ZdbMem::Store> store = new ZdbMem::Store{};
  ZmRef<DB> db = new DB{};
  Context context;

  db->init(ZdbCf{cf->resolve("zdb")}, &mx, ZdbHandler{
    .upFn = [](Zdb *db_, ZdbHost *) {
      static_cast<DB *>(db_)->active.post();
    }
  }, store);
  ZdbTblRef<Transfer> transfers = db->initTable<Transfer>("transfer");
  ZdbTblRef<Account> accounts = db->initTable<Account>("account");
  context.transfers = transfers;
  context.accounts = accounts;
  db->sagas(ZdbSagaHandler{
    .context = &context,
    .doneFn = [](void *context_, ZuCSpan, ZdbSagaID) {
      static_cast<Context *>(context_)->completed.post();
    },
    .errorFn = [](void *context_, ZuCSpan type, ZdbSagaID, ZeException e) {
      auto context = static_cast<Context *>(context_);
      context->failed = true;
      std::cerr << "saga " << type << " failed: " << e << '\n';
      context->completed.post();
    }
  });

  bool ok = db->start();
  if (ok) {
    db->active.wait();
    ok = accountInsert(accounts, 0, 1, 1000) &&
	accountInsert(accounts, 1, 2, 250);
  }

  if (ok) {
    ZmRef<Saga> saga = new Saga{};
    saga->init(BalanceTransfer{
      .transferID = 1, .fromID = 1, .toID = 2, .amount = 125,
      .transferShard = 0, .fromShard = 0, .toShard = 1
    });
    ok = ZmBlock<bool>{}([db = db.ptr(), saga = ZuMv(saga)](
	auto wake) mutable {
      if (!db->saga(0, ZdbSagaID{1}, ZuMv(saga), [
	  wake = ZuMv(wake)](ZdbSagaSubmitResult result) mutable {
	wake(result.template is<void>());
      })) wake(false);
    });
  }

  if (ok) {
    context.completed.wait();
    ok = !context.failed;
  }

  int64_t fromBalance = 0;
  int64_t toBalance = 0;
  if (ok)
    ok = accountBalance(accounts, 0, 1, fromBalance) &&
	accountBalance(accounts, 1, 2, toBalance) &&
	transferComplete(transfers, 0, 1);

  if (ok) {
    std::cout << "transfer 1 complete: account 1 = " << fromBalance <<
      ", account 2 = " << toBalance << '\n';
    ok = fromBalance == 875 && toBalance == 375;
  }

  if (!db->stop()) ok = false;
  transfers = {};
  accounts = {};
  db->final();
  db = {};
  store = {};
  if (!mx.stop()) ok = false;
  return ok ? 0 : 1;
}

} // namespace zdbsaga

int main()
{
  ZiLog::init("zdbsaga");
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();
  int result = 1;
  try {
    result = zdbsaga::run();
  } catch (const ZeException &e) {
    std::cerr << e << '\n';
  } catch (const ZeError &e) {
    std::cerr << e << '\n';
  } catch (...) {
    std::cerr << "unknown exception\n";
  }
  ZiLog::stop();
  return result;
}
