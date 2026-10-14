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

#include <zlib/ZiFB.hh>

#include <zlib/Zdb.hh>
#include <zlib/ZdbMemStore.hh>


namespace zdbsaga {

struct ConfigFields {
  ZfCf::Union<> mx;
  ZfCf::Union<> zdb;
};
ZfStruct(, (ConfigFields, Cf),
  (mx,		(Ctor<0>),	UDT),
  (zdb,		(Ctor<1>),	UDT));

struct Account {
  uint64_t	id = 0;
  int64_t	balance = 0;

  friend ZuStringT<"Zdb.Example.Account"> ZdbHeapID(Account *);
  friend ZuUnsigned<128> ZdbBufSize(Account *);
  friend ZuStringT<"Zdb.Example.Account.Buf"> ZdbBufHeapID(Account *);
};

ZfStruct(, (Account, FB),
  (id,		(Ctor<0>, Keys<0>),	UInt64),
  (balance,	(Ctor<1>, Mutable),	Int64));

ZiFB::AsTable<"zdbsaga.fbs.Account"> ZiFB_Fmt(Account *);
ZfStructImpl(Account);
ZfStructImpl(Account, FB);

ZtEnumNS(, TransferStatus, int8_t, Pending, Complete);
namespace TransferStatus {
  ZiFB::AsEnum<"zdbsaga.fbs.TransferStatus"> ZiFB_Fmt(Map *);
}
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

ZfStruct(, (Transfer, FB),
  (id,		(Ctor<0>, Keys<0>),				UInt64),
  (fromID,	(Ctor<1>),					UInt64),
  (toID,	(Ctor<2>),					UInt64),
  (amount,	(Ctor<3>),					Int64),
  (status,	(Ctor<4>, Enum<TransferStatus::Map>, Mutable),	Int8));

ZiFB::AsTable<"zdbsaga.fbs.Transfer"> ZiFB_Fmt(Transfer *);
ZfStructImpl(Transfer);
ZfStructImpl(Transfer, FB);

ZdbTableDerive(AccountTable, Account);
ZdbTableDerive(TransferTable, Transfer);

struct DB : public ZdbSagaDB {
  ZmSemaphore active;
  ZdbTable<Transfer>	*transfers = nullptr;
  ZdbTable<Account>	*accounts = nullptr;
  ZmSemaphore		completed;
  bool			failed = false;
};

static ZdbShard transferShard(uint64_t id) { return (id - 1) & 1; }
static ZdbShard accountShard(uint64_t id) { return (id - 1) & 1; }

template <typename Heap = ZuVoid>
struct BalanceTransfer_ : public Heap,
    public ZdbSaga<BalanceTransfer_<Heap>, DB> {
  using Base = ZdbSaga<BalanceTransfer_<Heap>, DB>;
  using Base::db;
  using Type = ZuStringT<"balanceTransfer">;
  enum { NPhases = 4 };

  uint64_t	transferID = 0;
  uint64_t	fromID = 0;
  uint64_t	toID = 0;
  int64_t	amount = 0;

  // The moved completion owns both this definition and the raw saga pointer
  // through asynchronous shard work.  Invoke it only after commit(), as the
  // final use of both this and saga in that callback.
  ZdbSagaPhase(0, transfer, Insert) {
    db()->transfers->run(transferShard(transferID),
      [this, complete = ZuMv(complete)]() mutable {
	auto shard = transferShard(transferID);
	if constexpr (Fwd) {
	  ZdbRowRef<Transfer> row =
	    new ZdbRow<Transfer>{db()->transfers, shard};
	  this->insert(db()->transfers, ZuMv(row), ZuMv(complete),
	    [this](ZdbRow<Transfer> *row, auto &&complete) mutable {
	      new (row->ptr()) Transfer{
		transferID, fromID, toID, amount, TransferStatus::Pending};
	      complete(bool(row->commit()));
	    });
	} else {
	  this->template findDel<0>(db()->transfers, shard, ZuFwdTuple(transferID),
	    ZuMv(complete), [](ZdbRow<Transfer> *row, auto &&complete) mutable {
	      complete(bool(row->commit()));
	    });
	}
      });
    return {};
  }

  ZdbSagaPhase(1, account, Update) {
    db()->accounts->run(accountShard(fromID),
      [this, complete = ZuMv(complete)]() mutable {
	auto shard = accountShard(fromID);
	this->template findUpd<0>(db()->accounts, shard, ZuFwdTuple(fromID),
	  ZuMv(complete), [this](ZdbRow<Account> *row, auto &&complete) mutable {
	    if constexpr (Fwd)
	      row->data().balance -= amount;
	    else
	      row->data().balance += amount;
	    complete(bool(row->commit()));
	  });
      });
    return {};
  }

  ZdbSagaPhase(2, account, Update) {
    db()->accounts->run(accountShard(toID),
      [this, complete = ZuMv(complete)]() mutable {
	auto shard = accountShard(toID);
	this->template findUpd<0>(db()->accounts, shard, ZuFwdTuple(toID),
	  ZuMv(complete), [this](ZdbRow<Account> *row, auto &&complete) mutable {
	    if constexpr (Fwd)
	      row->data().balance += amount;
	    else
	      row->data().balance -= amount;
	    complete(bool(row->commit()));
	  });
      });
    return {};
  }

  ZdbSagaPhase(3, transfer, Update) {
    ZuAssert(Fwd);
    db()->transfers->run(transferShard(transferID),
      [this, complete = ZuMv(complete)]() mutable {
	auto shard = transferShard(transferID);
	this->template findUpd<0>(
	  db()->transfers, shard, ZuFwdTuple(transferID),
	  ZuMv(complete), [](ZdbRow<Transfer> *row, auto &&complete) mutable {
	    row->data().status = TransferStatus::Complete;
	    complete(bool(row->commit()));
	  });
      });
    return {};
  }

  BalanceTransfer_() = default;
  BalanceTransfer_(uint64_t transferID_, uint64_t fromID_,
      uint64_t toID_, int64_t amount_) :
    transferID{transferID_}, fromID{fromID_}, toID{toID_}, amount{amount_} { }
};

using BalanceTransfer = BalanceTransfer_<
  ZmHeap<"Zdb.Example.BalanceTransfer", BalanceTransfer_<>>>;

ZuAssert((Zdb_::SagaDefValid<BalanceTransfer>{}));

ZfStruct(, (BalanceTransfer, FB),
  (transferID,	(Ctor<0>),	UInt64),
  (fromID,	(Ctor<1>),	UInt64),
  (toID,	(Ctor<2>),	UInt64),
  (amount,	(Ctor<3>),	Int64));

ZiFB::AsTable<"zdbsaga.fbs.BalanceTransfer"> ZiFB_Fmt(BalanceTransfer *);
ZfStructImpl(BalanceTransfer);
ZfStructImpl(BalanceTransfer, FB);

Zdb_::SagaDef balanceTransferDef() {
  return BalanceTransfer::def();
}


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
      ZdbRowRef<Account> row = new ZdbRow<Account>{table, shard};
      table->insert(row, [
	id, balance, wake = ZuMv(wake)](ZdbRow<Account> *row) mutable {
	if (!row) { wake(false); return; }
	new (row->ptr()) Account{id, balance};
	wake(bool(row->commit()));
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
	  &balance, wake = ZuMv(wake)](ZdbRowRef<Account> row) mutable {
	if (!row) { wake(false); return; }
	balance = row->data().balance;
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
	  wake = ZuMv(wake)](ZdbRowRef<Transfer> row) mutable {
	wake(row && row->data().status == TransferStatus::Complete);
      });
    });
  });
}

static int run()
{
  auto cf = config();
  auto cfFields = ZfCf::handler<zdbsaga::ConfigFields>(cf).ctor();
  ZiMultiplex mx{ZvMxParams("mx", cfFields.mx.p<const ZfCf::AnyNode *>())};
  if (!mx.start()) return 1;

  ZmRef<ZdbMem::Store> store = new ZdbMem::Store{};
  ZmRef<DB> db = new DB{};

  db->init(ZdbCf{cfFields.zdb.p<const ZfCf::AnyNode *>()}, &mx, ZdbHandler{
    .upFn = [](Zdb *db_, ZdbHost *) {
      static_cast<DB *>(db_)->active.post();
    }
  }, store);
  ZdbTblRef<Transfer> transfers = db->initTable<Transfer>("transfer");
  ZdbTblRef<Account> accounts = db->initTable<Account>("account");
  db->transfers = transfers;
  db->accounts = accounts;
  db->sagas({balanceTransferDef()});

  bool ok = db->start();
  if (ok) {
    db->active.wait();
    ok = accountInsert(accounts, 0, 1, 1000) &&
	accountInsert(accounts, 1, 2, 250);
  }

  if (ok) {
    ZmRef<ZdbAnySaga<DB>> saga =
      new BalanceTransfer{1, 1, 2, 125};
    ok = ZmBlock<bool>{}([
      db = db.ptr(), saga = ZuMv(saga)
    ](auto wake) mutable {
      if (!db->saga(transferShard(1), ZdbSagaID{1}, ZuMv(saga),
	[wake = ZuMv(wake)](bool ok) mutable { wake(ok); },
	[db](bool ok) {
	  db->failed = !ok;
	  db->completed.post();
	})) wake(false);
    });
  }

  if (ok) {
    db->completed.wait();
    ok = !db->failed;
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
