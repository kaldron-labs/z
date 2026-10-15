//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/zumd_saga_external_projection.hh>
#include <zlib/zumd_identity_db.hh>
#include <zlib/zumd_app_db.hh>
#include <zlib/zumd_provider_db.hh>
#include <zlib/zumd_key_db.hh>
#include <zlib/zumd_request_db.hh>
#include <zlib/ZtlsSec.hh>
#include <zlib/ZmHeap.hh>

namespace Zum {

template <typename Heap = ZuVoid>
struct ExternalProjectionSaga_ : public Heap,
    public ZdbSaga<ExternalProjectionSaga_<Heap>, DB> {
  using Base = ZdbSaga<ExternalProjectionSaga_<Heap>, DB>;
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
  using Type = ZuStringT<"externalProjection.v1">;
  enum { NPhases = 5 };

  ProviderID	providerID = 0;
  String	issuer;
  String	subject;
  UserID	userID = 0;
  String	name;
  Bytes		handle;
  int64_t	created = 0;

  ZdbSagaPhase(0, zum.ext_identity, Insert) {
    db()->shardRun(0, [this,
        complete = ZuMv(complete)]() mutable {
      auto key = ZuFwdTuple(providerID, issuer, subject);
      if constexpr (Fwd) {
	ZdbRowRef<ExtIdentity> row = new ZdbRow<ExtIdentity>{
	  db()->extIdentities, ZdbShard{0}};
	insert(db()->extIdentities, ZuMv(row), ZuMv(complete),
	  [this](ZdbRow<ExtIdentity> *row, auto &&complete) mutable {
	    new (row->ptr()) ExtIdentity{.providerID = providerID,
	      .issuer = issuer, .subject = subject, .userID = userID,
	      .created = created, .updated = created, .owner = id()};
	    complete(row->commit());
	  });
      } else {
	this->template findDel<0>(db()->extIdentities, 0, ZuMv(key),
	  ZuMv(complete), [this](ZdbRow<ExtIdentity> *row,
	      auto &&complete) mutable {
	    if (!row || row->data().owner != id()) {
	      complete(true);
	      return;
	    }
	    complete(row->commit());
	  });
      }
    });
    return {};
  }

  ZdbSagaPhase(1, zum.user, Insert) {
    db()->shardRun(0, [this, complete = ZuMv(complete)]() mutable {
      if constexpr (Fwd) {
	ZdbRowRef<User> row = new ZdbRow<User>{db()->users, ZdbShard{0}};
	insert(db()->users, ZuMv(row), ZuMv(complete),
	  [this](ZdbRow<User> *row, auto &&complete) mutable {
	    new (row->ptr()) User{.id = userID, .source = UserSource::External,
	      .name = name, .handle = handle, .created = created, .updated = created,
	      .state = State::Pending, .owner = id()};
	    complete(row->commit());
	  });
      } else {
	this->template findDel<0>(db()->users, 0, ZuFwdTuple(userID),
	  ZuMv(complete), [this](ZdbRow<User> *row,
	      auto &&complete) mutable {
	    if (!row || row->data().owner != id() ||
		row->data().state != State::Pending) {
	      complete(true);
	      return;
	    }
	    complete(row->commit());
	  });
      }
    });
    return {};
  }

  ZdbSagaPhase(2, zum.user, Update) {
    db()->shardRun(0, [this, complete = ZuMv(complete)]() mutable {
      this->template findUpd<0>(db()->users, 0, ZuFwdTuple(userID),
	ZuMv(complete), [this](ZdbRow<User> *row,
	    auto &&complete) mutable {
	if (!row || row->data().owner != id() ||
	    row->data().state != (Fwd ? State::Pending : State::Active)) {
	  complete(!Fwd);
	  return;
	}
	row->data().state = Fwd ? State::Active : State::Pending;
	complete(row->commit());
      });
    });
    return {};
  }

  ZdbSagaPhase(3, zum.user, Update) {
    db()->shardRun(0, [this, complete = ZuMv(complete)]() mutable {
      this->template findUpd<0>(db()->users, 0, ZuFwdTuple(userID),
	ZuMv(complete), [this](ZdbRow<User> *row,
	    auto &&complete) mutable {
	if (!row || row->data().state != State::Active ||
	    row->data().owner != (Fwd ? id() : uint128_t{0})) {
	  complete(!Fwd);
	  return;
	}
	row->data().owner = Fwd ? uint128_t{0} : id();
	complete(row->commit());
      });
    });
    return {};
  }

  ZdbSagaPhase(4, zum.ext_identity, Update) {
    db()->shardRun(0, [this,
        complete = ZuMv(complete)]() mutable {
      this->template findUpd<0>(db()->extIdentities, 0,
	ZuFwdTuple(providerID, issuer, subject), ZuMv(complete),
	[this](ZdbRow<ExtIdentity> *row, auto &&complete) mutable {
	  if (!row || row->data().owner !=
	      (Fwd ? id() : uint128_t{0})) {
	    complete(!Fwd);
	    return;
	  }
	  row->data().owner = Fwd ? uint128_t{0} : id();
	  complete(row->commit());
	});
    });
    return {};
  }

  ExternalProjectionSaga_() = default;
  ExternalProjectionSaga_(
      ProviderID providerID_, String issuer_,
      String subject_, UserID userID_,
      String name_, Bytes handle_,
      int64_t created_) :
    providerID{ZuMv(providerID_)},
    issuer{ZuMv(issuer_)},
    subject{ZuMv(subject_)},
    userID{ZuMv(userID_)},
    name{ZuMv(name_)},
    handle{ZuMv(handle_)},
    created{ZuMv(created_)} { }
  ExternalProjectionSaga_(ExternalProjectionArgs args) :
    ExternalProjectionSaga_{
      ZuMv(args.providerID), ZuMv(args.issuer), ZuMv(args.subject),
      ZuMv(args.userID), ZuMv(args.name), ZuMv(args.handle),
      ZuMv(args.created)} { }
  ~ExternalProjectionSaga_() override;
};

using ExternalProjectionSaga = ExternalProjectionSaga_<
  ZmHeap<"Zum.ExternalProjectionSaga", ExternalProjectionSaga_<>>>;

ZfStruct(, (ExternalProjectionSaga, FB),
  (providerID,	(Ctor<0>),	UInt64),
  (issuer,	(Ctor<1>),	String),
  (subject,	(Ctor<2>),	String),
  (userID,	(Ctor<3>),	UInt64),
  (name,	(Ctor<4>),	String),
  (handle,	(Ctor<5>),	Bytes),
  (created,	(Ctor<6>),	Int64));
Zfb::AsTable<"Zum.fbs.ExternalProjection"> Zfb_Fmt(ExternalProjectionSaga *);

ZuAssert((Zdb_::SagaDefValid<ExternalProjectionSaga>{}));

template <typename Heap>
ExternalProjectionSaga_<Heap>::~ExternalProjectionSaga_() = default;

SagaRef externalProjection(ZuBSpan data) {
  return ExternalProjectionSaga::load(data);
}

Zdb_::SagaDef externalProjectionDef() {
  return ExternalProjectionSaga::def();
}
SagaRef externalProjection(ExternalProjectionArgs args) {
  return new ExternalProjectionSaga{ZuMv(args)};
}

} // namespace Zum
