//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/zumd_saga_client_access_put.hh>
#include <zlib/zumd_identity_db.hh>
#include <zlib/zumd_app_db.hh>
#include <zlib/zumd_provider_db.hh>
#include <zlib/zumd_key_db.hh>
#include <zlib/zumd_request_db.hh>
#include <zlib/zumd_saga_admin_ops.hh>

namespace Zum {

struct ClientAccessPutSaga : public ZmNHeap<ClientAccessPutSaga>,
    public ZdbSaga<ClientAccessPutSaga, DB> {
friend inline ZuStringT<"Zum.ClientAccessPutSaga"> ZmNHeapID(ClientAccessPutSaga *);

  using Base = ZdbSaga<ClientAccessPutSaga, DB>;
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
  using Type = ZuStringT<"clientAccessPut.v1">;
  enum { NPhases = 7 };

  App app;
  ClientAccess before;
  ClientAccess values;
  String ifMatch;
  String ifNoneMatch;
  int64_t updated = 0;
  IdemRequest request;
  unsigned error = 0;

  bool unchanged() const { return false; }
  ZumAPI unsigned appError(const App &) const;
  ZumAPI unsigned recordError(const ClientAccess &) const;
  ZumAPI void validate(Zdb_::SagaCompleteFn);
  ZumAPI ClientAccess result() const;

  ZdbSagaPhase(0, zum.request, Insert) {
    requestInsert<Fwd>(this, ZuMv(complete));
    return {};
  }
  ZdbSagaPhase(1, zum.app, Update) {
    appEditStart<Fwd>(this, ZuMv(complete));
    return {};
  }
  ZdbSagaPhase(2, zum.client_access, Insert) {
    recordPut<Fwd, true>(this, db()->clientAccess, ZuMv(complete));
    return {};
  }
  ZdbSagaPhase(3, zum.client_access, Update) {
    recordPut<Fwd, false>(this, db()->clientAccess, ZuMv(complete));
    return {};
  }
  ZdbSagaPhase(4, zum.client_access, Update) {
    appEditRelease<Fwd>(this, db()->clientAccess, ZuMv(complete));
    return {};
  }
  ZdbSagaPhase(5, zum.app, Update) {
    appEditPublish<Fwd>(this, true, ZuMv(complete));
    return {};
  }
  ZdbSagaPhase(6, zum.request, Update) {
    requestComplete(this, {}, updated, ZuMv(complete));
    return {};
  }

  ClientAccessPutSaga() = default;
  ClientAccessPutSaga(App app_, ClientAccess before_, ClientAccess values_, String ifMatch_, String ifNoneMatch_, int64_t updated_, IdemRequest request_) :
    app{ZuMv(app_)},
    before{ZuMv(before_)},
    values{ZuMv(values_)},
    ifMatch{ZuMv(ifMatch_)},
    ifNoneMatch{ZuMv(ifNoneMatch_)},
    updated{ZuMv(updated_)},
    request{ZuMv(request_)} { }
  ClientAccessPutSaga(ClientAccessPutArgs args) :
    ClientAccessPutSaga{ZuMv(args.app), ZuMv(args.before), ZuMv(args.values), ZuMv(args.ifMatch), ZuMv(args.ifNoneMatch), ZuMv(args.updated), ZuMv(args.request)} {
    response = ZuMv(args.result);
    if (response) response->item = result();
  }
  ZmRef<AdminEditResult<ClientAccess>> response;
  void setError(unsigned value) { error = value; if (response) response->error = value; }
  ~ClientAccessPutSaga() override;
};

ZfStruct(, (ClientAccessPutSaga, FB),
  (app,		(Ctor<0>),	UDT),
  (before,	(Ctor<1>),	UDT),
  (values,	(Ctor<2>),	UDT),
  (ifMatch,	(Ctor<3>),	String),
  (ifNoneMatch,	(Ctor<4>),	String),
  (updated,	(Ctor<5>),	Int64),
  (request,	(Ctor<6>),	UDT));
Zfb::AsTable<"Zum.fbs.ClientAccessPut"> Zfb_Fmt(ClientAccessPutSaga *);

unsigned ClientAccessPutSaga::appError(const App &item) const
{
  return appVersionError(app, item);
}

unsigned ClientAccessPutSaga::recordError(const ClientAccess &item) const
{
  if (auto code = putError(*this, item)) return code;
  return item.authVersion != before.authVersion || before.authVersion == UINT64_MAX ? 409 : 0;
}

ClientAccess ClientAccessPutSaga::result() const { return accessResult(*this); }

void ClientAccessPutSaga::validate(Zdb_::SagaCompleteFn complete)
{
  if (!values.appID || values.appID != app.id || values.appID != before.appID ||
      !values.clientID || values.clientID != before.clientID ||
      !uniqueRefs(values.roleIDs)) {
    setError(400); complete(false); return;
  }
  if (!before.version && (ifMatch || ifNoneMatch != "*")) {
    setError(ifMatch ? 412 : 428); complete(false); return;
  }
  auto clients = db()->clients;
  clients->run(0, [this, clients, complete = ZuMv(complete)]() mutable {
    clients->template find<0>(0, ZuFwdTuple(values.clientID),
      [this, complete = ZuMv(complete)](ZdbRowRef<Client> row) mutable {
	if (!row || row->data().owner || row->data().state != State::Active) {
	  setError(400); complete(false); return;
	}
	accessRefs(this, 0, ZuMv(complete));
      });
  });
}

ZuAssert((Zdb_::SagaDefValid<ClientAccessPutSaga>{}));

ClientAccessPutSaga::~ClientAccessPutSaga() = default;

SagaRef clientAccessPut(ZuBSpan data) {
  return ClientAccessPutSaga::load(data);
}

Zdb_::SagaDef clientAccessPutDef() {
  return ClientAccessPutSaga::def();
}
SagaRef clientAccessPut(ClientAccessPutArgs args) {
  return new ClientAccessPutSaga{ZuMv(args)};
}

} // namespace Zum
