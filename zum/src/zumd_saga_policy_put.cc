//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/zumd_saga_policy_put.hh>
#include <zlib/zumd_identity_db.hh>
#include <zlib/zumd_app_db.hh>
#include <zlib/zumd_provider_db.hh>
#include <zlib/zumd_key_db.hh>
#include <zlib/zumd_request_db.hh>
#include <zlib/zumd_saga_admin_ops.hh>

namespace Zum {

template <typename Heap = ZuVoid>
struct PolicyPutSaga_ : public Heap,
    public ZdbSaga<PolicyPutSaga_<Heap>, DB> {
  using Base = ZdbSaga<PolicyPutSaga_<Heap>, DB>;
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
  using Type = ZuStringT<"policyPut.v1">;
  enum { NPhases = 7 };

  App app;
  AuthPolicy before;
  AuthPolicy values;
  String ifMatch;
  String ifNoneMatch;
  int64_t updated = 0;
  IdemRequest request;
  unsigned error = 0;

  bool unchanged() const { return false; }
  ZumAPI unsigned appError(const App &) const;
  ZumAPI unsigned recordError(const AuthPolicy &) const;
  ZumAPI void validate(Zdb_::SagaCompleteFn);
  ZumAPI AuthPolicy result() const;

  ZdbSagaPhase(0, zum.request, Insert) {
    requestInsert<Fwd>(this, ZuMv(complete));
    return {};
  }
  ZdbSagaPhase(1, zum.app, Update) {
    appEditStart<Fwd>(this, ZuMv(complete));
    return {};
  }
  ZdbSagaPhase(2, zum.auth_policy, Insert) {
    recordPut<Fwd, true>(this, db()->authPolicies, ZuMv(complete));
    return {};
  }
  ZdbSagaPhase(3, zum.auth_policy, Update) {
    recordPut<Fwd, false>(this, db()->authPolicies, ZuMv(complete));
    return {};
  }
  ZdbSagaPhase(4, zum.auth_policy, Update) {
    appEditRelease<Fwd>(this, db()->authPolicies, ZuMv(complete));
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

  PolicyPutSaga_() = default;
  PolicyPutSaga_(App app_, AuthPolicy before_, AuthPolicy values_, String ifMatch_, String ifNoneMatch_, int64_t updated_, IdemRequest request_) :
    app{ZuMv(app_)},
    before{ZuMv(before_)},
    values{ZuMv(values_)},
    ifMatch{ZuMv(ifMatch_)},
    ifNoneMatch{ZuMv(ifNoneMatch_)},
    updated{ZuMv(updated_)},
    request{ZuMv(request_)} { }
  PolicyPutSaga_(PolicyPutArgs args) :
    PolicyPutSaga_{ZuMv(args.app), ZuMv(args.before), ZuMv(args.values), ZuMv(args.ifMatch), ZuMv(args.ifNoneMatch), ZuMv(args.updated), ZuMv(args.request)} {
    response = ZuMv(args.result);
    if (response) response->item = result();
  }
  ZmRef<AdminEditResult<AuthPolicy>> response;
  void setError(unsigned value) { error = value; if (response) response->error = value; }
  ~PolicyPutSaga_() override;
};


using PolicyPutSaga = PolicyPutSaga_<
  ZmHeap<"Zum.PolicyPutSaga", PolicyPutSaga_<>>>;

ZfStruct(, (PolicyPutSaga, FB),
  (app,		(Ctor<0>),	UDT),
  (before,	(Ctor<1>),	UDT),
  (values,	(Ctor<2>),	UDT),
  (ifMatch,	(Ctor<3>),	String),
  (ifNoneMatch,	(Ctor<4>),	String),
  (updated,	(Ctor<5>),	Int64),
  (request,	(Ctor<6>),	UDT));
Zfb::AsTable<"Zum.fbs.PolicyPut"> Zfb_Fmt(PolicyPutSaga *);

template <typename Heap>
unsigned PolicyPutSaga_<Heap>::appError(const App &item) const
{
  return appVersionError(app, item);
}

template <typename Heap>
unsigned PolicyPutSaga_<Heap>::recordError(const AuthPolicy &item) const
{
  return putError(*this, item);
}

template <typename Heap>
AuthPolicy PolicyPutSaga_<Heap>::result() const
{
  AuthPolicy item = values;
  item.version = before.version + 1;
  item.created = before.version ? before.created : updated;
  item.updated = updated;
  item.owner = 0;
  return item;
}

template <typename Heap>
void PolicyPutSaga_<Heap>::validate(Zdb_::SagaCompleteFn complete)
{
  if (!values.appID || values.appID != before.appID || values.appID != app.id ||
      !values.localFirst || values.eligibilityMode < 0 || values.eligibilityMode >= EligibilityMode::N ||
      values.consentPolicy < 0 || values.consentPolicy >= ConsentPolicy::N ||
      values.state < 0 || values.state >= State::N || !values.assignmentMaxAge ||
      !values.sessionIdle || !values.sessionAbsolute || !values.tokenLifetime ||
      values.sessionIdle > values.sessionAbsolute ||
      (values.providerID && values.eligibilityMode == EligibilityMode::ClaimValues &&
	(!values.eligibilityClaim || !values.eligibilityValues))) {
    setError(400); complete(false); return;
  }
  if (!before.version && (ifMatch || ifNoneMatch != "*")) {
    setError(ifMatch ? 412 : 428); complete(false); return;
  }
  if (!values.providerID) { complete(true); return; }
  auto providers = db()->providers;
  providers->run(0, [this, providers, complete = ZuMv(complete)]() mutable {
    providers->template find<0>(0, ZuFwdTuple(values.providerID),
      [this, complete = ZuMv(complete)](ZdbRowRef<Provider> row) mutable {
	if (!row || row->data().owner || row->data().state != State::Active) {
	  setError(400); complete(false); return;
	}
	complete(true);
      });
  });
}

ZuAssert((Zdb_::SagaDefValid<PolicyPutSaga>{}));

template <typename Heap>
PolicyPutSaga_<Heap>::~PolicyPutSaga_() = default;

SagaRef policyPut(ZuBSpan data) {
  return PolicyPutSaga::load(data);
}

Zdb_::SagaDef policyPutDef() {
  return PolicyPutSaga::def();
}
SagaRef policyPut(PolicyPutArgs args) {
  return new PolicyPutSaga{ZuMv(args)};
}

} // namespace Zum
