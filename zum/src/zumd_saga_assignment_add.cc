//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/zumd_saga_assignment_add.hh>
#include <zlib/zumd_identity_db.hh>
#include <zlib/zumd_app_db.hh>
#include <zlib/zumd_request_db.hh>

namespace Zum {

template <typename Heap = ZuVoid>
struct AssignmentAddSaga_ : public Heap,
    public ZdbSaga<AssignmentAddSaga_<Heap>, DB> {
  using Base = ZdbSaga<AssignmentAddSaga_<Heap>, DB>;
  using Base::db;
  using Base::id;
  using Base::iteration;
  using Base::skip;
  using Base::insert;
  using Base::update;
  using Base::findUpd;
  using Base::findDel;
  using Base::result_;
  using Base::shard;
  using Type = ZuStringT<"assignmentAdd.v1">;
  enum { NPhases = 4 };

  AppID appID = 0;
  UserID userID = 0;
  int64_t created = 0;
  IdemRequest request;
  unsigned error = 503; // Live response only; not recovery state.
  ZmRef<AssignmentAddResult> result;

  void setError(unsigned value) {
    error = value;
    if (result) result->error = value;
  }

  ZdbSagaPhase(0, zum.request, Insert) {
    requestInsert<Fwd>(this, ZuMv(complete));
    return {};
  }

  ZdbSagaPhase(1, zum.assignment, Insert) {
    if constexpr (!Fwd) {
      db()->shardRun(0, [this, complete = ZuMv(complete)]() mutable {
	this->template findDel<0>(db()->assignments, 0, ZuFwdTuple(appID, userID),
	  ZuMv(complete), [](ZdbRow<Assignment> *row, auto &&complete) mutable {
	    complete(!row || row->commit());
	  });
      });
      return {};
    }
    db()->shardRun(0, [this, complete = ZuMv(complete)]() mutable {
      db()->apps->template find<0>(0, ZuFwdTuple(appID),
	[this, complete = ZuMv(complete)](ZdbRowRef<App> app) mutable {
	  if (!app || app->data().state != State::Active || app->data().owner) {
	    complete(false);
	    return;
	  }
	  db()->users->template find<0>(0, ZuFwdTuple(userID),
	    [this, complete = ZuMv(complete)](ZdbRowRef<User> user) mutable {
		if (!user || user->data().source != UserSource::Local ||
		    user->data().state == State::Revoked || user->data().owner) {
		  complete(false);
		  return;
		}
		db()->assignments->template find<0>(0, ZuFwdTuple(appID, userID),
		  [this, complete = ZuMv(complete)](
		      ZdbRowRef<Assignment> existing) mutable {
		      if (existing) { setError(409); complete(false); return; }
		      ZdbRowRef<Assignment> row =
			new ZdbRow<Assignment>{db()->assignments, ZdbShard{0}};
		      insert(db()->assignments, ZuMv(row), ZuMv(complete),
			[this](
			    ZdbRow<Assignment> *row, auto &&complete) mutable {
			  new (row->ptr()) Assignment{.appID = appID, .userID = userID,
			    .state = State::Active, .version = 1, .created = created,
			    .updated = created, .owner = id()};
			  complete(row->commit());
			});
		  });
	    });
	});
    });
    return {};
  }

  ZdbSagaPhase(2, zum.assignment, Update) {
    db()->shardRun(0, [this, complete = ZuMv(complete)]() mutable {
      this->template findUpd<0>(db()->assignments, 0, ZuFwdTuple(appID, userID),
	ZuMv(complete), [this](ZdbRow<Assignment> *row, auto &&complete) mutable {
	  if (!row) { complete(!Fwd); return; }
	  row->data().owner = Fwd ? uint128_t{0} : id();
	  complete(row->commit());
	});
    });
    return {};
  }

  ZdbSagaPhase(3, zum.request, Update) {
    StringVec ids;
    ids.push(String{} << appID << ':' << userID);
    requestComplete(this, ZuMv(ids), created, ZuMv(complete));
    return {};
  }

  AssignmentAddSaga_() = default;
  AssignmentAddSaga_(AppID appID_, UserID userID_, int64_t created_,
      IdemRequest request_, ZmRef<AssignmentAddResult> result_ = {}) :
    appID{appID_}, userID{userID_}, created{created_},
    request{ZuMv(request_)}, result{ZuMv(result_)} { }
  AssignmentAddSaga_(AssignmentAddArgs args) :
    AssignmentAddSaga_{args.appID, args.userID, args.created,
      ZuMv(args.request), ZuMv(args.result)} { }
  ~AssignmentAddSaga_() override;
};

using AssignmentAddSaga = AssignmentAddSaga_<
  ZmHeap<"Zum.AssignmentAddSaga", AssignmentAddSaga_<>>>;

ZfStruct(, (AssignmentAddSaga, FB),
  (appID,	(Ctor<0>),	UInt64),
  (userID,	(Ctor<1>),	UInt64),
  (created,	(Ctor<2>),	Int64),
  (request,	(Ctor<3>),	UDT));
Zfb::AsTable<"Zum.fbs.AssignmentAdd"> Zfb_Fmt(AssignmentAddSaga *);


ZuAssert((Zdb_::SagaDefValid<AssignmentAddSaga>{}));

template <typename Heap>
AssignmentAddSaga_<Heap>::~AssignmentAddSaga_() = default;

SagaRef assignmentAdd(ZuBSpan data) {
  return AssignmentAddSaga::load(data);
}

Zdb_::SagaDef assignmentAddDef() {
  return AssignmentAddSaga::def();
}
SagaRef assignmentAdd(AssignmentAddArgs args) {
  return new AssignmentAddSaga{ZuMv(args)};
}

} // namespace Zum
