//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/zumd_saga_assignment_change.hh>
#include <zlib/zumd_identity_db.hh>
#include <zlib/zumd_app_db.hh>
#include <zlib/zumd_provider_db.hh>
#include <zlib/zumd_key_db.hh>
#include <zlib/zumd_request_db.hh>
#include <zlib/zumd_saga_admin_ops.hh>

namespace Zum {

template <typename Heap = ZuVoid>
struct AssignmentChangeSaga_ : public Heap,
    public ZdbSaga<AssignmentChangeSaga_<Heap>, DB> {
  using Base = ZdbSaga<AssignmentChangeSaga_<Heap>, DB>;
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
  using Type = ZuStringT<"assignmentChange.v2">;
  enum { NPhases = 6 };

  AppID		appID = 0;
  UserID	userID = 0;
  IDVec		oldRoles;
  IDVec		newRoles;
  State::T	oldState = State::Pending;
  State::T	newState = State::Pending;
  uint64_t	version = 0;
  uint64_t	authVersion = 0;
  int64_t	oldUpdated = 0;
  int64_t	updated = 0;
  uint64_t	appVersion = 0;
  uint64_t	appAuthVersion = 0;
  int64_t	appUpdated = 0;

  IdemRequest request;

  bool assignRoles = false;
  String ifMatch;
  unsigned error = 0; // Live response only; not recovery state.

  ZumAPI bool appValid(const App &, bool) const;
  ZumAPI unsigned memberError(const Assignment &, bool) const;
  ZumAPI void validate(Zdb_::SagaCompleteFn);
  ZumAPI void roles(unsigned, Zdb_::SagaCompleteFn);

  bool unchanged() const { return oldRoles == newRoles && oldState == newState; }

  ZdbSagaPhase(0, zum.request, Insert) {
    requestInsert<Fwd>(this, ZuMv(complete));
    return {};
  }

  ZdbSagaPhase(1, zum.app, Update) {
    if constexpr (!Fwd)
      if (unchanged()) { this->skip(ZuMv(complete)); return {}; }
    db()->shardRun(0, [this, complete = ZuMv(complete)]() mutable {
      db()->apps->template find<0>(0, ZuFwdTuple(appID),
	[this, complete = ZuMv(complete)](ZdbRowRef<App> row) mutable {
	  if (!row) { setError(409); complete(!Fwd); return; }
	  if (unchanged()) {
	    if (appValid(row->data(), Fwd)) this->skip(ZuMv(complete));
	    else { setError(409); complete(false); }
	    return;
	  }
	  this->update(db()->apps, ZuMv(row), ZuMv(complete),
	    [this](ZdbRow<App> *row, auto &&complete) mutable {
	      if (!appValid(row->data(), Fwd)) {
		setError(409);
		complete(!Fwd);
		return;
	      }
	      row->data().owner = Fwd ? this->id() : uint128_t{0};
	      complete(row->commit());
	    });
	});
    });
    return {};
  }

  ZdbSagaPhase(2, zum.assignment, Update) {
    if constexpr (!Fwd)
      if (unchanged()) { this->skip(ZuMv(complete)); return {}; }
    auto apply = [this, complete = ZuMv(complete)](bool ok) mutable {
	if (!ok) { complete(false); return; }
      db()->shardRun(0, [this, complete = ZuMv(complete)]() mutable {
	db()->assignments->template find<0>(0, ZuFwdTuple(appID, userID),
	  [this, complete = ZuMv(complete)](ZdbRowRef<Assignment> row) mutable {
	    if (!row) { setError(404); complete(!Fwd); return; }
	    if (unchanged()) {
	      setError(memberError(row->data(), Fwd));
	      if (error) complete(false);
	      else this->skip(ZuMv(complete));
	      return;
	    }
	    this->update(db()->assignments, ZuMv(row), ZuMv(complete),
	      [this](ZdbRow<Assignment> *row, auto &&complete) mutable {
		if (auto code = memberError(row->data(), Fwd)) {
		  setError(code);
		  complete(!Fwd);
		  return;
		}
		auto &item = row->data();
		item.roleIDs = Fwd ? newRoles : oldRoles;
		item.state = Fwd ? newState : oldState;
		item.version = version + Fwd;
		item.authVersion = authVersion + Fwd;
		item.updated = Fwd ? updated : oldUpdated;
		item.owner = Fwd ? this->id() : uint128_t{0};
		complete(row->commit());
	      });
	  });
      });
    };
    if constexpr (Fwd) validate(ZuMv(apply));
    else apply(true);
    return {};
  }

  ZdbSagaPhase(3, zum.assignment, Update) {
    if (unchanged()) { this->skip(ZuMv(complete)); return {}; }
    db()->shardRun(0, [this, complete = ZuMv(complete)]() mutable {
      this->template findUpd<0>(db()->assignments, 0, ZuFwdTuple(appID, userID),
	ZuMv(complete), [this](ZdbRow<Assignment> *row, auto &&complete) mutable {
	  if (!row || row->data().owner !=
		(Fwd ? this->id() : uint128_t{0}) ||
	      row->data().version != version + 1 ||
	      row->data().authVersion != authVersion + 1 ||
	      row->data().state != newState || row->data().roleIDs != newRoles) {
	    complete(!Fwd);
	    return;
	  }
	  row->data().owner = Fwd ? uint128_t{0} : this->id();
	  complete(row->commit());
	});
    });
    return {};
  }

  ZdbSagaPhase(4, zum.app, Update) {
    if (unchanged()) { this->skip(ZuMv(complete)); return {}; }
    db()->shardRun(0, [this, complete = ZuMv(complete)]() mutable {
      this->template findUpd<0>(db()->apps, 0, ZuFwdTuple(appID), ZuMv(complete),
	[this](ZdbRow<App> *row, auto &&complete) mutable {
	  if (!row || row->data().owner != (Fwd ? this->id() : uint128_t{0}) ||
	      row->data().state != State::Active ||
	      row->data().version != appVersion + !Fwd ||
	      row->data().authVersion != appAuthVersion + !Fwd ||
	      row->data().updated != (Fwd ? appUpdated : updated)) {
	    complete(false);
	    return;
	  }
	  row->data().version = appVersion + Fwd;
	  row->data().authVersion = appAuthVersion + Fwd;
	  row->data().updated = Fwd ? updated : appUpdated;
	  row->data().owner = Fwd ? uint128_t{0} : this->id();
	  complete(row->commit());
	});
    });
    return {};
  }

  ZdbSagaPhase(5, zum.request, Update) {
    requestComplete(this, {}, updated, ZuMv(complete));
    return {};
  }

  AssignmentChangeSaga_() = default;
  AssignmentChangeSaga_(AppID appID_, UserID userID_, IDVec oldRoles_, IDVec newRoles_, State::T oldState_, State::T newState_, uint64_t version_, uint64_t authVersion_, int64_t oldUpdated_, int64_t updated_, uint64_t appVersion_, uint64_t appAuthVersion_, int64_t appUpdated_, IdemRequest request_, bool assignRoles_, String ifMatch_) :
    appID{ZuMv(appID_)},
    userID{ZuMv(userID_)},
    oldRoles{ZuMv(oldRoles_)},
    newRoles{ZuMv(newRoles_)},
    oldState{ZuMv(oldState_)},
    newState{ZuMv(newState_)},
    version{ZuMv(version_)},
    authVersion{ZuMv(authVersion_)},
    oldUpdated{ZuMv(oldUpdated_)},
    updated{ZuMv(updated_)},
    appVersion{ZuMv(appVersion_)},
    appAuthVersion{ZuMv(appAuthVersion_)},
    appUpdated{ZuMv(appUpdated_)},
    request{ZuMv(request_)},
    assignRoles{ZuMv(assignRoles_)},
    ifMatch{ZuMv(ifMatch_)} { }
  AssignmentChangeSaga_(AssignmentChangeArgs args) :
    AssignmentChangeSaga_{ZuMv(args.appID), ZuMv(args.userID), ZuMv(args.oldRoles), ZuMv(args.newRoles), ZuMv(args.oldState), ZuMv(args.newState), ZuMv(args.version), ZuMv(args.authVersion), ZuMv(args.oldUpdated), ZuMv(args.updated), ZuMv(args.appVersion), ZuMv(args.appAuthVersion), ZuMv(args.appUpdated), ZuMv(args.request), ZuMv(args.assignRoles), ZuMv(args.ifMatch)} {
    response = ZuMv(args.result);
  }
  ZmRef<AdminEditResult<Assignment>> response;
  void setError(unsigned value) { error = value; if (response) response->error = value; }
  ~AssignmentChangeSaga_() override;
};


using AssignmentChangeSaga = AssignmentChangeSaga_<
  ZmHeap<"Zum.AssignmentChangeSaga", AssignmentChangeSaga_<>>>;

ZfStruct(, (AssignmentChangeSaga, FB),
  (appID,		(Ctor<0>),			UInt64),
  (userID,		(Ctor<1>),			UInt64),
  (oldRoles,		(Ctor<2>),			UInt64Vec),
  (newRoles,		(Ctor<3>),			UInt64Vec),
  (oldState,		(Ctor<4>, Enum<State::Map>),	Int8),
  (newState,		(Ctor<5>, Enum<State::Map>),	Int8),
  (version,		(Ctor<6>),			UInt64),
  (authVersion,		(Ctor<7>),			UInt64),
  (oldUpdated,		(Ctor<8>),			Int64),
  (updated,		(Ctor<9>),			Int64),
  (appVersion,		(Ctor<10>),			UInt64),
  (appAuthVersion,	(Ctor<11>),			UInt64),
  (appUpdated,		(Ctor<12>),			Int64),
  (request,		(Ctor<13>),			UDT),
  (assignRoles,		(Ctor<14>),			Bool),
  (ifMatch,		(Ctor<15>),			String));
ZiFB::AsTable<"Zum.fbs.AssignmentChange"> ZiFB_Fmt(AssignmentChangeSaga *);

template <typename Heap>
bool AssignmentChangeSaga_<Heap>::appValid(const App &item, bool fwd) const
{
  return item.state == State::Active && item.version == appVersion &&
    item.authVersion == appAuthVersion && item.updated == appUpdated &&
    item.owner == (fwd ? uint128_t{0} : this->id()) &&
    appVersion != UINT64_MAX && appAuthVersion != UINT64_MAX;
}

template <typename Heap>
unsigned AssignmentChangeSaga_<Heap>::memberError(const Assignment &item, bool fwd) const
{
  if (fwd) {
    if (ifMatch) {
      String etag{"\"v"};
      etag << item.version << '"';
      if (ifMatch != etag) return 412;
    }
    if (item.version != version) return 412;
    if (item.state == State::Revoked) return 409;
  }
  return version == UINT64_MAX || authVersion == UINT64_MAX ||
    item.owner != (fwd ? uint128_t{0} : this->id()) ||
    item.version != version + !fwd ||
    item.authVersion != authVersion + !fwd ||
    item.updated != (fwd ? oldUpdated : updated) ||
    item.state != (fwd ? oldState : newState) ||
    item.roleIDs != (fwd ? oldRoles : newRoles) ? 409 : 0;
}

template <typename Heap>
void AssignmentChangeSaga_<Heap>::validate(Zdb_::SagaCompleteFn complete)
{
  db()->users->run(0, [this, complete = ZuMv(complete)]() mutable {
    db()->users->template find<0>(0, ZuFwdTuple(userID),
      [this, complete = ZuMv(complete)](ZdbRowRef<User> row) mutable {
	if (!row || row->data().source != UserSource::Local || row->data().owner) {
	  setError(409);
	  complete(false);
	  return;
	}
	if (!assignRoles) { complete(true); return; }
	roles(0, [this, complete = ZuMv(complete)](bool ok) mutable {
	  if (!ok) {
	    setError(400);
	    complete(false);
	    return;
	  }
	  complete(true);
	});
      });
  });
}

template <typename Heap>
void AssignmentChangeSaga_<Heap>::roles(unsigned offset, Zdb_::SagaCompleteFn complete)
{
  if (offset == newRoles.length()) { complete(true); return; }
  auto id = newRoles[offset];
  if (!id) { complete(false); return; }
  for (unsigned i = 0; i < offset; ++i)
    if (newRoles[i] == id) { complete(false); return; }
  db()->roles->run(0, [this, offset, id, complete = ZuMv(complete)]() mutable {
    db()->roles->template find<0>(0, ZuFwdTuple(appID, id),
      [this, offset, complete = ZuMv(complete)](ZdbRowRef<Role> row) mutable {
	if (!row || row->data().state != State::Active ||
	    row->data().tombstone || row->data().owner) {
	  complete(false);
	  return;
	}
	roles(offset + 1, ZuMv(complete));
      });
  });
}

ZuAssert((Zdb_::SagaDefValid<AssignmentChangeSaga>{}));

template <typename Heap>
AssignmentChangeSaga_<Heap>::~AssignmentChangeSaga_() = default;

SagaRef assignmentChange(ZuBSpan data) {
  return AssignmentChangeSaga::load(data);
}

Zdb_::SagaDef assignmentChangeDef() {
  return AssignmentChangeSaga::def();
}
SagaRef assignmentChange(AssignmentChangeArgs args) {
  return new AssignmentChangeSaga{ZuMv(args)};
}

} // namespace Zum
