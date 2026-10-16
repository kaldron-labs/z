//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/zumd_saga_key_retire.hh>
#include <zlib/zumd_identity_db.hh>
#include <zlib/zumd_app_db.hh>
#include <zlib/zumd_provider_db.hh>
#include <zlib/zumd_key_db.hh>
#include <zlib/zumd_request_db.hh>
#include <zlib/zumd_saga_admin_ops.hh>

namespace Zum {

struct KeyRetireSaga : public ZmNHeap<KeyRetireSaga>,
    public ZdbSaga<KeyRetireSaga, DB> {
friend inline ZuStringT<"Zum.KeyRetireSaga"> ZmNHeapID(KeyRetireSaga *);

  using Base = ZdbSaga<KeyRetireSaga, DB>;
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
  using Type = ZuStringT<"keyRetire.v1">;
  enum { NPhases = 3 };

  SignKey before;
  String ifMatch;
  int64_t retireAfter = 0;
  int64_t updated = 0;
  IdemRequest request;
  unsigned error = 0;

  bool unchanged() const { return false; }
  ZumAPI unsigned recordError(const SignKey &) const;

  ZdbSagaPhase(0, zum.request, Insert) {
    requestInsert<Fwd>(this, ZuMv(complete));
    return {};
  }
  ZdbSagaPhase(1, zum.sign_key, Update) {
    db()->shardRun(0, [this, complete = ZuMv(complete)]() mutable {
      this->template findUpd<0, ZuSeq<1, 2>>(db()->signKeys, 0, ZuFwdTuple(before.id),
	ZuMv(complete), [this](ZdbRow<SignKey> *row, auto &&complete) mutable {
	  if (!row) { setError(404); complete(!Fwd); return; }
	  if constexpr (Fwd) {
	    if (auto code = recordError(row->data())) {
	      setError(code); complete(false); return;
	    }
	    row->data().retireAfter = retireAfter;
	    row->data().state = State::Suspended;
	    row->data().version = before.version + 1;
	    row->data().updated = updated;
	  } else {
	    row->data().retireAfter = before.retireAfter;
	    row->data().state = before.state;
	    row->data().version = before.version;
	    row->data().updated = before.updated;
	  }
	  complete(row->commit());
	});
    });
    return {};
  }
  ZdbSagaPhase(2, zum.request, Update) {
    requestComplete(this, {}, updated, ZuMv(complete));
    return {};
  }

  KeyRetireSaga() = default;
  KeyRetireSaga(SignKey before_, String ifMatch_, int64_t retireAfter_, int64_t updated_, IdemRequest request_) :
    before{ZuMv(before_)},
    ifMatch{ZuMv(ifMatch_)},
    retireAfter{ZuMv(retireAfter_)},
    updated{ZuMv(updated_)},
    request{ZuMv(request_)} { }
  KeyRetireSaga(KeyRetireArgs args) :
    KeyRetireSaga{ZuMv(args.before), ZuMv(args.ifMatch), ZuMv(args.retireAfter), ZuMv(args.updated), ZuMv(args.request)} {
    response = ZuMv(args.result);
  }
  ZmRef<AdminEditResult<SignKey>> response;
  void setError(unsigned value) { error = value; if (response) response->error = value; }
  ~KeyRetireSaga() override;
};

ZfStruct(, (KeyRetireSaga, FB),
  (before,	(Ctor<0>),	UDT),
  (ifMatch,	(Ctor<1>),	String),
  (retireAfter,	(Ctor<2>),	Int64),
  (updated,	(Ctor<3>),	Int64),
  (request,	(Ctor<4>),	UDT));
Zfb::AsTable<"Zum.fbs.KeyRetire"> Zfb_Fmt(KeyRetireSaga *);

unsigned KeyRetireSaga::recordError(const SignKey &item) const
{
  if (retireAfter <= updated) return 400;
  String etag{"\"v"};
  etag << item.version << '"';
  if (ifMatch != etag || item.version != before.version) return 412;
  return !before.version || before.version == UINT64_MAX ||
    item.state == State::Revoked || item.state == State::Consumed ? 409 : 0;
}

ZuAssert((Zdb_::SagaDefValid<KeyRetireSaga>{}));

KeyRetireSaga::~KeyRetireSaga() = default;

SagaRef keyRetire(ZuBSpan data) {
  return KeyRetireSaga::load(data);
}

Zdb_::SagaDef keyRetireDef() {
  return KeyRetireSaga::def();
}
SagaRef keyRetire(KeyRetireArgs args) {
  return new KeyRetireSaga{ZuMv(args)};
}

} // namespace Zum
