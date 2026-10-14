//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/zumd_saga_user_edit.hh>
#include <zlib/zumd_identity_db.hh>
#include <zlib/zumd_app_db.hh>
#include <zlib/zumd_provider_db.hh>
#include <zlib/zumd_key_db.hh>
#include <zlib/zumd_request_db.hh>
#include <zlib/zumd_saga_admin_ops.hh>

namespace Zum {

template <typename Heap = ZuVoid>
struct UserEditSaga_ : public Heap,
    public ZdbSaga<UserEditSaga_<Heap>, DB> {
  using Base = ZdbSaga<UserEditSaga_<Heap>, DB>;
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
  using Type = ZuStringT<"userEdit.v1">;
  enum { NPhases = 4 };

  User before;
  String ifMatch;
  String profile;
  int64_t updated = 0;
  IdemRequest request;
  bool stateOnly = false;
  State::T state = State::Pending;
  String email;
  uint8_t fields = 0;
  unsigned error = 0;

  bool unchanged() const { return stateOnly && state == before.state; }
  ZumAPI unsigned userError(const User &) const;

  ZdbSagaPhase(0, zum.request, Insert) {
    requestInsert<Fwd>(this, ZuMv(complete));
    return {};
  }
  ZdbSagaPhase(1, zum.user, Update) {
    if constexpr (!Fwd)
      if (unchanged()) { this->skip(ZuMv(complete)); return {}; }
    db()->shardRun(0, [this, complete = ZuMv(complete)]() mutable {
      db()->users->template find<0>(0, ZuFwdTuple(before.id),
	[this, complete = ZuMv(complete)](ZdbRowRef<User> row) mutable {
	  if (!row) { setError(404); complete(!Fwd); return; }
	  if (unchanged()) {
	    setError(userError(row->data()));
	    if (error) complete(false);
	    else this->skip(ZuMv(complete));
	    return;
	  }
	  this->update(db()->users, ZuMv(row), ZuMv(complete),
	    [this](ZdbRow<User> *row, auto &&complete) mutable {
	      if constexpr (Fwd) {
		if (auto code = userError(row->data())) {
		  setError(code); complete(false); return;
		}
		if (stateOnly) row->data().state = state;
		else {
		  if (fields & 1U) row->data().profile = profile;
		  if (fields & 2U) row->data().email = email;
		}
		row->data().version = before.version + 1;
		row->data().authVersion = before.authVersion + stateOnly;
		row->data().updated = updated;
		row->data().owner = this->id();
	      } else row->data() = before;
	      complete(row->commit());
	    });
	});
    });
    return {};
  }
  ZdbSagaPhase(2, zum.user, Update) {
    if (unchanged()) { this->skip(ZuMv(complete)); return {}; }
    db()->shardRun(0, [this, complete = ZuMv(complete)]() mutable {
      this->template findUpd<0>(db()->users, 0, ZuFwdTuple(before.id),
	ZuMv(complete), [this](ZdbRow<User> *row, auto &&complete) mutable {
	  if (!row) { complete(!Fwd); return; }
	  row->data().owner = Fwd ? uint128_t{0} : this->id();
	  complete(row->commit());
	});
    });
    return {};
  }
  ZdbSagaPhase(3, zum.request, Update) {
    requestComplete(this, {}, updated, ZuMv(complete));
    return {};
  }

  UserEditSaga_() = default;
  UserEditSaga_(User before_, String ifMatch_, String profile_, int64_t updated_, IdemRequest request_, bool stateOnly_, State::T state_, String email_, uint8_t fields_) :
    before{ZuMv(before_)},
    ifMatch{ZuMv(ifMatch_)},
    profile{ZuMv(profile_)},
    updated{ZuMv(updated_)},
    request{ZuMv(request_)},
    stateOnly{ZuMv(stateOnly_)},
    state{ZuMv(state_)},
    email{ZuMv(email_)},
    fields{ZuMv(fields_)} { }
  UserEditSaga_(UserEditArgs args) :
    UserEditSaga_{ZuMv(args.before), ZuMv(args.ifMatch), ZuMv(args.profile), ZuMv(args.updated), ZuMv(args.request), ZuMv(args.stateOnly), ZuMv(args.state), ZuMv(args.email), ZuMv(args.fields)} {
    response = ZuMv(args.result);
  }
  ZmRef<AdminEditResult<User>> response;
  void setError(unsigned value) { error = value; if (response) response->error = value; }
  ~UserEditSaga_() override;
};


using UserEditSaga = UserEditSaga_<
  ZmHeap<"Zum.UserEditSaga", UserEditSaga_<>>>;

ZfStruct(, (UserEditSaga, FB),
  (before,	(Ctor<0>),			UDT),
  (ifMatch,	(Ctor<1>),			String),
  (profile,	(Ctor<2>),			String),
  (updated,	(Ctor<3>),			Int64),
  (request,	(Ctor<4>),			UDT),
  (stateOnly,	(Ctor<5>),			Bool),
  (state,	(Ctor<6>, Enum<State::Map>),	Int8),
  (email,	(Ctor<7>),			String),
  (fields,	(Ctor<8>),			UInt8));
ZiFB::AsTable<"Zum.fbs.UserEdit"> ZiFB_Fmt(UserEditSaga *);

template <typename Heap>
unsigned UserEditSaga_<Heap>::userError(const User &item) const
{
  if (stateOnly ? state < 0 || state >= State::N : !fields || (fields & ~3U))
    return 400;
  String etag{"\"v"};
  etag << item.version << '"';
  if (ifMatch != etag || item.version != before.version) return 412;
  return !before.version || before.owner || item.owner ||
    item.authVersion != before.authVersion ||
    (stateOnly && (item.source != UserSource::Local ||
      item.state == State::Revoked ||
      (state == State::Active && (item.state == State::Pending || !item.handle)))) ||
    (!unchanged() && (before.version == UINT64_MAX ||
      (stateOnly && before.authVersion == UINT64_MAX))) ? 409 : 0;
}

ZuAssert((Zdb_::SagaDefValid<UserEditSaga>{}));

template <typename Heap>
UserEditSaga_<Heap>::~UserEditSaga_() = default;

SagaRef userEdit(ZuBSpan data) {
  return UserEditSaga::load(data);
}

Zdb_::SagaDef userEditDef() {
  return UserEditSaga::def();
}
SagaRef userEdit(UserEditArgs args) {
  return new UserEditSaga{ZuMv(args)};
}

} // namespace Zum
