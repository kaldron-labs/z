//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/zumd_saga_user_invite.hh>
#include <zlib/zumd_identity_db.hh>
#include <zlib/zumd_app_db.hh>
#include <zlib/zumd_provider_db.hh>
#include <zlib/zumd_key_db.hh>
#include <zlib/zumd_request_db.hh>
#include <zlib/zumd_saga_admin_ops.hh>

namespace Zum {

template <typename Heap = ZuVoid>
struct UserInviteSaga_ : public Heap,
    public ZdbSaga<UserInviteSaga_<Heap>, DB> {
  using Base = ZdbSaga<UserInviteSaga_<Heap>, DB>;
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
  using Type = ZuStringT<"userInvite.v2">;
  enum { NPhases = 8 };

  User before;
  User values;
  Grant grant;
  int64_t updated = 0;
  IdemRequest request;
  User external;
  unsigned error = 0;

  bool unchanged() const { return false; }
  ZumAPI void validate(Zdb_::SagaCompleteFn);
  ZumAPI User result() const;

  template <bool Fwd, bool Release> void invalidate(Zdb_::SagaCompleteFn complete)
  {
    if (!external.id) { this->skip(ZuMv(complete)); return; }
    auto users = db()->users;
    users->run(0, [this, users, complete = ZuMv(complete)]() mutable {
      this->template findUpd<0>(users, 0, ZuFwdTuple(external.id), ZuMv(complete),
        [this](ZdbRow<User> *row, auto &&complete) mutable {
          if (!row) { setError(409); complete(!Fwd); return; }
          auto &item = row->data();
          if constexpr (Release) {
            item.owner = Fwd ? uint128_t{0} : this->id();
          } else if constexpr (Fwd) {
            if (item.owner || item.source != UserSource::External ||
                item.name != values.name || item.version != external.version ||
                item.authVersion != external.authVersion ||
                !item.authVersion || item.authVersion == UINT64_MAX ||
                !item.version || item.version == UINT64_MAX) {
              setError(409); complete(false); return;
            }
            ++item.authVersion;
            ++item.version;
            item.updated = updated;
            item.owner = this->id();
          } else item = external;
          complete(row->commit());
        });
    });
  }

  ZdbSagaPhase(0, zum.request, Insert) {
    requestInsert<Fwd>(this, ZuMv(complete));
    return {};
  }
  ZdbSagaPhase(1, zum.user, Insert) {
    recordPut<Fwd, true>(this, db()->users, ZuMv(complete));
    return {};
  }
  ZdbSagaPhase(2, zum.grant, Insert) {
    auto table = db()->grants;
    table->run(0, [this, table, complete = ZuMv(complete)]() mutable {
      if constexpr (Fwd) {
	table->template find<0>(0, ZuFwdTuple(grant.id),
	  [this, table, complete = ZuMv(complete)](ZdbRowRef<Grant> existing) mutable {
	    ZdbRowRef<Grant> row = new ZdbRow<Grant>{table, ZdbShard{0}};
	    this->insert(table, ZuMv(row), ZuMv(complete),
	      [this, duplicate = bool(existing)](ZdbRow<Grant> *row, auto &&complete) mutable {
		new (row->ptr()) Grant{grant};
		if (duplicate) { setError(409); complete(false); return; }
		row->data().owner = this->id();
		complete(row->commit());
	      });
	  });
      } else {
	this->template findDel<0>(table, 0, ZuFwdTuple(grant.id), ZuMv(complete),
	  [](ZdbRow<Grant> *row, auto &&complete) mutable {
	    complete(!row || row->commit());
	  });
      }
    });
    return {};
  }
  ZdbSagaPhase(3, zum.user, Update) {
    invalidate<Fwd, false>(ZuMv(complete));
    return {};
  }
  ZdbSagaPhase(4, zum.user, Update) {
    invalidate<Fwd, true>(ZuMv(complete));
    return {};
  }
  ZdbSagaPhase(5, zum.user, Update) {
    appEditRelease<Fwd>(this, db()->users, ZuMv(complete));
    return {};
  }
  ZdbSagaPhase(6, zum.grant, Update) {
    auto table = db()->grants;
    table->run(0, [this, table, complete = ZuMv(complete)]() mutable {
      this->template findUpd<0>(table, 0, ZuFwdTuple(grant.id), ZuMv(complete),
	[this](ZdbRow<Grant> *row, auto &&complete) mutable {
	  if (!row) { complete(!Fwd); return; }
	  row->data().owner = Fwd ? uint128_t{0} : this->id();
	  complete(row->commit());
	});
    });
    return {};
  }
  ZdbSagaPhase(7, zum.request, Update) {
    StringVec ids;
    ids.push(String{} << values.id);
    requestComplete(this, ZuMv(ids), updated, ZuMv(complete));
    return {};
  }

  UserInviteSaga_() = default;
  UserInviteSaga_(User before_, User values_, Grant grant_, int64_t updated_, IdemRequest request_, User external_) :
    before{ZuMv(before_)},
    values{ZuMv(values_)},
    grant{ZuMv(grant_)},
    updated{ZuMv(updated_)},
    request{ZuMv(request_)},
    external{ZuMv(external_)} { }
  UserInviteSaga_(UserInviteArgs args) :
    UserInviteSaga_{ZuMv(args.before), ZuMv(args.values), ZuMv(args.grant), ZuMv(args.updated), ZuMv(args.request), ZuMv(args.external)} {
    response = ZuMv(args.result);
    if (response) response->item = result();
  }
  ZmRef<AdminEditResult<User>> response;
  void setError(unsigned value) { error = value; if (response) response->error = value; }
  ~UserInviteSaga_() override;
};


using UserInviteSaga = UserInviteSaga_<
  ZmHeap<"Zum.UserInviteSaga", UserInviteSaga_<>>>;

ZfStruct(, (UserInviteSaga, FB),
  (before,	(Ctor<0>),	UDT),
  (values,	(Ctor<1>),	UDT),
  (updated,	(Ctor<3>),	Int64),
  (request,	(Ctor<4>),	UDT),
  (grant,	(Ctor<2>),	UDT),
  (external,	(Ctor<5>),	UDT));
ZiFB::AsTable<"Zum.fbs.UserInvite"> ZiFB_Fmt(UserInviteSaga *);

template <typename Heap>
User UserInviteSaga_<Heap>::result() const
{
  return User{.id = values.id, .source = UserSource::Local, .name = values.name,
    .profile = values.profile, .email = values.email, .created = updated,
    .updated = updated, .state = State::Pending, .version = 1};
}

template <typename Heap>
void UserInviteSaga_<Heap>::validate(Zdb_::SagaCompleteFn complete)
{
  if (!values.id || values.id == UINT64_MAX || values.id != before.id || !values.name ||
      !grant.id || !grant.digest || !grant.issuer || grant.userID != values.id ||
      grant.userName != values.name || grant.kind != GrantKind::Capability ||
      grant.purpose != GrantPurpose::Enrollment || grant.state != State::Active ||
      grant.expires <= updated || grant.actor != "precreated") {
    setError(400); complete(false); return;
  }
  if (before.version) { setError(409); complete(false); return; }
  complete(true);
}

ZuAssert((Zdb_::SagaDefValid<UserInviteSaga>{}));

template <typename Heap>
UserInviteSaga_<Heap>::~UserInviteSaga_() = default;

SagaRef userInvite(ZuBSpan data) {
  return UserInviteSaga::load(data);
}

Zdb_::SagaDef userInviteDef() {
  return UserInviteSaga::def();
}
SagaRef userInvite(UserInviteArgs args) {
  return new UserInviteSaga{ZuMv(args)};
}

} // namespace Zum
