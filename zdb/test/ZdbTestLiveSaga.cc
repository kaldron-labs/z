//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/Zdb.hh>
#include <zlib/ZfbReflect.hh>
#include "ZdbSagaTestDefs.hh"

namespace zdbtest {

struct LiveSaga : public ZmNHeap<LiveSaga>, public ZdbSaga<LiveSaga, LiveDB> {
friend inline ZuStringT<"Zdb.Test.LiveSaga"> ZmNHeapID(LiveSaga *);

  using Base = ZdbSaga<LiveSaga, LiveDB>;
  using Base::db;
  using Type = ZuStringT<"liveSaga">;
  enum { NPhases = 3 };

  uint64_t orderID;

  template <unsigned Phase, bool Fwd, typename Complete>
  void begin(Complete &&complete) {
    db()->deadline = this->deadline();
    ++db()->runs;
    db()->dirs.push(Fwd ? int(Phase) + 1 : -int(Phase) - 1);
    if (db()->trace) db()->trace(Trace::Next, Phase);
    if constexpr (Fwd)
      if (Phase == db()->skipStep) {
	this->skip(ZuFwd<Complete>(complete));
	return;
      }
    if constexpr (Fwd)
      if (Phase == db()->failStep) {
	if (db()->absentReverse && Phase) {
	  db()->orders->run(0,
	    [this, complete = ZuMv(complete)]() mutable {
	    db()->orders->template findDel<0>(0, ZuFwdTuple("IBM", orderID),
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
    if (Phase == db()->pauseStep &&
	(!db()->pausedOnce || db()->pauseAll)) {
      db()->pausedOnce = true;
      db()->pause(PauseFn{
	[this, complete = ZuMv(complete)](bool ok) mutable {
	  if (!ok) { complete(false); return; }
	  begin<Phase, Fwd>(ZuMv(complete));
	}});
      return;
    }
    if (db()->read) {
      db()->orders->template count<0>({}, [
	this, complete = ZuMv(complete)
      ](ZuUnion<void, uint64_t> result) mutable {
	  if (!result.is<uint64_t>()) {
	    ++db()->errors;
	    complete(false);
	    return;
	  }
	  ++db()->reads;
	  if (db()->trace) db()->trace(Trace::Read, Phase);
	  effect<Fwd>(ZuMv(complete), ZuUnsigned<Phase>{});
      });
      return;
    }
    effect<Fwd>(ZuFwd<Complete>(complete), ZuUnsigned<Phase>{});
  }

  template <bool Fwd, typename Complete>
  void effect(Complete &&complete,
      ZuUnsigned<0>) {
	db()->orders->run(0, [
	  this, complete = ZuMv(complete)
	]() mutable {
	if constexpr (Fwd) {
	  ZdbRowRef<Order> order =
	    new ZdbRow<Order>{db()->orders, ZdbShard{0}};
	  this->insert(db()->orders, ZuMv(order), ZuMv(complete),
	  [this](ZdbRow<Order> *row, auto &&complete) mutable {
	    ++db()->inserts;
	    if (db()->trace) db()->trace(Trace::Enter, 0);
	    new (row->ptr()) Order{
	      "IBM", orderID, "FIX0", "saga", 0,
	      Side::Buy, {100}, {1}};
	    bool ok = row->commit();
	    if (db()->trace) db()->trace(Trace::Return, 0);
	    complete(ok);
	  });
	} else {
	  this->template findDel<0>(db()->orders, 0, ZuFwdTuple("IBM", orderID),
	    ZuMv(complete), [this](ZdbRow<Order> *row, auto &&complete) mutable {
	      if (db()->failReverse) {
		auto failed = db()->reverseFailed;
		complete(false);
		if (failed) failed->post();
		return;
	      }
	      complete(bool(row->commit()));
	    });
	}
	});
  }

  template <bool Fwd, typename Complete>
  void effect(Complete &&complete,
      ZuUnsigned<1>) {
	db()->orders->run(0, [
	  this, complete = ZuMv(complete)
	]() mutable {
	auto fn = [this](ZdbRow<Order> *row, auto &&complete) mutable {
	  ++db()->updates;
	  if (db()->trace) db()->trace(Trace::Enter, 1);
	  row->data().seqNo = Fwd ? 1 : 0;
	  bool ok = row->commit();
	  if (db()->trace) db()->trace(Trace::Return, 1);
	  complete(ok);
	};
	if (db()->secondary)
	  this->template findUpd<1, ZuSeq<2>>(db()->orders, 0,
	    ZuFwdTuple("FIX0", "saga"), ZuMv(complete), ZuMv(fn));
	else
	  this->template findUpd<0, ZuSeq<2>>(db()->orders, 0,
	    ZuFwdTuple("IBM", orderID), ZuMv(complete), ZuMv(fn));
	});
  }

  template <bool Fwd, typename Complete>
  void effect(Complete &&complete,
      ZuUnsigned<2>) {
	ZuAssert(Fwd);
	db()->orders->run(0, [
	  this, complete = ZuMv(complete)
	]() mutable {
	auto complete_ = [this, complete = ZuMv(complete)](bool ok) mutable {
	  if (db()->pauseStep == 3 &&
	      (!db()->pausedOnce || db()->pauseAll)) {
	    db()->pausedOnce = true;
	    db()->pause([
	      complete = ZuMv(complete), ok
	    ](bool resume) mutable { complete(resume && ok); });
	    return;
	  }
	  complete(ok);
	};
	auto fn = [this](ZdbRow<Order> *row, auto &&complete) mutable {
	  ++db()->deletes;
	  if (db()->trace) db()->trace(Trace::Enter, 2);
	  if (!row->commit()) { complete(false); return; }
	  if (db()->trace) db()->trace(Trace::Return, 2);
	  complete(true);
	};
	if (db()->secondary)
	  this->template findDel<1>(db()->orders, 0,
	    ZuFwdTuple("FIX0", "saga"), ZuMv(complete_), ZuMv(fn));
	else
	  this->template findDel<0>(db()->orders, 0,
	    ZuFwdTuple("IBM", orderID), ZuMv(complete_), ZuMv(fn));
	});
  }

  ZdbSagaPhase(0, o, Insert) {
    begin<Phase, Fwd>(ZuFwd<Complete>(complete));
    return {};
  }
  ZdbSagaPhase(1, o, Update) {
    begin<Phase, Fwd>(ZuFwd<Complete>(complete));
    return {};
  }
  ZdbSagaPhase(2, o, Delete) {
    begin<Phase, Fwd>(ZuFwd<Complete>(complete));
    return {};
  }

  LiveSaga(uint64_t orderID_) : orderID{ZuMv(orderID_)} { }
};

ZfStruct(, (LiveSaga, FB),
  (orderID, (Ctor<0>),		UInt64));
Zfb::AsTable<"zdbtest.fbs.LiveSaga"> Zfb_Fmt(LiveSaga *);

ZuAssert((Zdb_::SagaDefValid<LiveSaga>{}));

ZmRef<ZdbAnySaga<LiveDB>> liveSaga(uint64_t orderID_) {
  return new LiveSaga{ZuMv(orderID_)};
}

Zdb_::SagaDef liveSagaDef() {
  return LiveSaga::def();
}

} // zdbtest
