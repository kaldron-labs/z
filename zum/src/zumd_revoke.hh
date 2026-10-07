//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// bounded bulk revocation sagas

#ifndef zumd_revoke_HH
#define zumd_revoke_HH

#ifndef ZumLib_HH
#include <zlib/ZuDerive.hh>
#include <zlib/ZumLib.hh>
#endif

#include <zlib/ZumMgmt.hh>
#include <zlib/zumd_secret.hh>
#include <zlib/zumd_identity_db.hh>
#include <zlib/zumd_provider_db.hh>
#include <zlib/zumd_request_db.hh>
#include <zlib/zumd_saga_image.hh>
#include <zlib/zumd_oidc.hh>
#include <zlib/zumd_key_db.hh>
#include <zlib/zumd_token.hh>
#include <zlib/zumd_request.hh>
#include <zlib/zumd_ssf_db.hh>
#include <zlib/zum_saga_fbs.h>
#include <zlib/zum_maintenance_fbs.h>
#include <zlib/ZuBase64URL.hh>
#include <zlib/ZtlsCOSE.hh>
#include <zlib/ZtlsMD.hh>

namespace Zum {

struct DB;

ZuDerive(SSFSETFn, (ZmFn<void(String),
  ZmFnHeapID<"Zum.SSFSETFn">>));

struct SSFSubject {
  String format;
  String issuer;
  String familyID;
  int64_t expires = 0;
};
ZfStruct(, (SSFSubject, JSON),
  (format, (Required),					String),
  (issuer, (JSON::ID<"iss">, Required),			String),
  (familyID, (JSON::ID<"family_id">, Required),		String),
  (expires, (JSON::ID<"exp">, Required),		Int64));

struct SSFEvent {
  SSFSubject subject;
};
ZfStruct(, (SSFEvent, JSON),
  (subject, (Required),		UDT));
struct SSFEvents {
  SSFEvent revoked;
};
ZfStruct(, (SSFEvents, JSON),
  (revoked, (JSON::ID<"urn:zum:events:refresh-token-revoked">, Required),	UDT));
struct SSFClaims {
  String issuer;
  String audience;
  String id;
  int64_t issued = 0;
  SSFEvents events;
};
ZfStruct(, (SSFClaims, JSON),
  (issuer, (JSON::ID<"iss">, Required),		String),
  (audience, (JSON::ID<"aud">, Required),	String),
  (id, (JSON::ID<"jti">, Required),		String),
  (issued, (JSON::ID<"iat">, Required),		Int64),
  (events, (Required),				UDT));

struct SSFHeader {
  String algorithm;
  String type;
  String keyID;
};
ZfStruct(, (SSFHeader, JSON),
  (algorithm, (JSON::ID<"alg">, Required),	String),
  (type, (JSON::ID<"typ">, Required),		String),
  (keyID, (JSON::ID<"kid">, Required),		String));

inline void makeSSF(
    const SignKey &key, ZuCSpan audience, const RefreshID &refreshID,
    int64_t expires, int64_t now, SignFn sign, SSFSETFn complete)
{
  if (!key.id || !key.issuer || !audience || !refreshID.issuerURL ||
      !refreshID.familyID || expires <= now || now <= 0 || !sign || !complete) {
    if (complete) complete(String{});
    return;
  }
  String eventID{refreshID.issuerURL};
  eventID << ':' << refreshID.familyID;
  eventID << ':' << now;
  String header;
  ZfJSON::save(header, SSFHeader{"ES256", "secevent+jwt", key.id});
  String header64;
  header64.length(ZuBase64URL::enclen(header.length()));
  if (ZuBase64URL::encode(header64.span(), ZuBSpan{header}) != header64.length()) {
    complete(String{});
    return;
  }
  String claims;
  ZfJSON::save(claims, SSFClaims{
    key.issuer, audience, ZuMv(eventID), now,
    {.revoked = SSFEvent{SSFSubject{
      "opaque", refreshID.issuerURL, refreshID.familyID, expires}}}});
  String input{header64};
  input << '.';
  unsigned claimsOffset = input.length();
  unsigned claimsLength = ZuBase64URL::enclen(claims.length());
  input.length(claimsOffset + claimsLength);
  if (ZuBase64URL::encode(input.span().offset(claimsOffset),
      ZuBSpan{claims}) != claimsLength) {
    complete(String{});
    return;
  }
  ZuBArray<Ztls::MD<>::Size> digest(Ztls::MD<>::Size, false);
  Ztls::MD<> md;
  md.update(ZuBSpan{input});
  md.finish(digest);
  sign(key, digest, [input = ZuMv(input), complete = ZuMv(complete)](
      Bytes signature) mutable {
    ZuBArray<Ztls::COSE::ES256::SignatureSize> raw(
      Ztls::COSE::ES256::SignatureSize, false);
    if (!signature || !Ztls::COSE::ES256::derToRaw(signature, raw)) {
      complete(String{});
      return;
    }
    String output{ZuMv(input)};
    output << '.';
    unsigned offset = output.length();
    output.length(offset + ZuBase64URL::enclen(raw.length()));
    if (ZuBase64URL::encode(output.span().offset(offset),
        raw) != output.length() - offset) {
      complete(String{});
      return;
    }
    complete(ZuMv(output));
  });
}

ZuDerive(SSFDeliveryDoneFn, (ZmFn<void(int),
  ZmFnHeapID<"Zum.SSFDeliveryDone">>));

ZuDerive(SSFRegisterFn, (ZmFn<void(unsigned, SSFLease),
  ZmFnHeapID<"Zum.SSFRegisterFn">>));

ZuDerive(SSFKeyFn, (ZmFn<void(String, SignKeyFn),
  ZmFnHeapID<"Zum.SSFKeyFn">>));

struct SSFTransmitterConfig {
  DB			*db = nullptr;
  DBContext		*context = nullptr;
  Requests		*requests = nullptr;
  String		issuer;
  String		secretIssuer;
  Bytes			dbKey;
  Ztls::Random		*rng = nullptr;
  SignKey		key;
  SSFKeyFn		keyLoad;
  SignFn		sign;
  OIDCHTTPFn		http;
  unsigned		receiverMax = 1024;
  uint32_t		leaseMax = 300;
  unsigned		errorMax = 5;
  uint32_t		retryBase = 1;
  uint32_t		retryMax = 60;
};

template <typename Heap = ZuVoid>
class SSFTransmitter_ : public Heap, public ZmObject  {
public:
  ~SSFTransmitter_() { clear_(); }
  bool init(SSFTransmitterConfig);
  void start();
  void stop();
  void revoke(AppID, RefreshID, int64_t);
  void add(AppID, String, SSFRegistration, SSFRegisterFn);

private:
  void revoke_(AppID, RefreshID, int64_t);
  void gc_();
  void gcArm_(int64_t);
  void result_(String, uint64_t, int);
  void receivers_(AppID, RefreshID, int64_t);
  void issue_(SSFRx, RefreshID, int64_t, int64_t);
  void persist_(SSFRx, SSFDelivery, String);
  void deliver_(SSFRx, SSFDelivery, String);
  void send_(SSFRx, SSFDelivery, String);
  String authorization_(const SSFRx &);
  void retry_();
  void retryPage_(SSFDeliveryTable::Key<0>, bool);
  void arm_(int64_t);
  void armOn_(int64_t);
  void clear_();

  SSFTransmitterConfig	m_config;
  ZmScheduler::Timer	m_timer;
  ZmScheduler::Timer	m_gcTimer;
  bool			m_gcArmed = false;
  bool			m_armed = false;
  int64_t		m_nextDelivery = 0;
  bool			m_started = false;
};
ZuDerive(SSFTransmitterHeap,
  (ZmHeap<"Zum.zumd.revoke.SSFTransmitter", SSFTransmitter_<>>));
ZuDerive(SSFTransmitter, (SSFTransmitter_<SSFTransmitterHeap>));

// Send one already-signed SET. Durable callers retain the SSFDelivery row and
// invoke this helper again using their retry schedule until acknowledgement.
inline void sendSSF(
    SSFRx receiver, SSFDelivery delivery, String authorization,
    OIDCHTTPFn http, SSFDeliveryDoneFn complete)
{
  if (!http || !receiver.deliveryURL || !delivery.set || !complete) {
    if (complete) complete(OAuthError::InvalidRequest);
    return;
  }
  http(OIDCHTTPRequest{
    .url = ZuMv(receiver.deliveryURL),
    .contentType = "application/secevent+jwt",
    .authorization = ZuMv(authorization),
    .body = ZuMv(delivery.set),
    .method = OIDCHTTPMethod::POST},
    [complete = ZuMv(complete)](unsigned status, String body) mutable {
      if (body.mutable_()) ZuClear(body);
      // Delivery status is deliberately reduced to terminal/retry classes;
      // callers retain the durable row for the actual retry decision.
      complete(status == 202 ? RevokeIssue::OK :
        OAuthError::TemporarilyUnavailable);
    });
}

struct Revoke : public ZdbSagaBase<DBContext> {
  using Base = ZdbSagaBase<DBContext>;
  using Base::context;
  using Base::saga;
  using Type = ZuStringT<"revoke.v2">;
  enum { NSteps = 8 };

  BytesVec sessions;
  BytesVec consents;
  BytesVec grants;
  int64_t updated = 0;
  IdemRequest request;
  unsigned error = 0;

  template <typename T> static bool eligible(const T &item)
  {
    return !item.owner && item.state != State::Revoked && item.state != State::Consumed;
  }

  template <bool Fwd, bool Release, typename Table, typename Complete>
  void change(Table *table, const BytesVec &images, Complete complete)
  {
    using T = typename Table::T;
    T before;
    if (!SagaImage::load(images[saga->iteration()], before)) {
      error = 400; complete(false); return;
    }
    if (!eligible(before)) { saga->skip(ZuMv(complete)); return; }
    table->run(0, [this, table, before = ZuMv(before), complete = ZuMv(complete)]() mutable {
      ZuStructKeyT<T, 0> key{ZuStructKey<0>(before)};
      table->template find<0>(0, ZuMv(key),
	[this, table, before = ZuMv(before), complete = ZuMv(complete)](
	    ZdbRowRef<T> row) mutable {
	  if (!row) { error = 409; complete(!Fwd); return; }
	  saga->update(table, ZuMv(row), ZuMv(complete),
	    [this, before = ZuMv(before)](ZdbRow<T> *row, auto &&complete) mutable {
	      auto &item = row->data();
	      if constexpr (Release) {
		item.owner = Fwd ? uint128_t{0} : saga->id();
	      } else {
		if constexpr (Fwd) {
		  bool valid = !item.owner && item.state == before.state;
		  if constexpr (ZuIsSame<T, Grant>{})
		    valid &= item.expires == before.expires && item.digest == before.digest;
		  else valid &= item.version == before.version && before.version != UINT64_MAX;
		  if (!valid) { error = 409; complete(false); return; }
		}
		item.state = Fwd ? State::Revoked : before.state;
		item.owner = Fwd ? saga->id() : before.owner;
		if constexpr (!ZuIsSame<T, Grant>{}) {
		  item.version = before.version + Fwd;
		  item.updated = Fwd ? updated : before.updated;
		}
	      }
	      complete(row->commit());
	    });
	});
    });
  }

  template <typename T> static unsigned count(const BytesVec &images)
  {
    unsigned n = 0;
    for (const auto &image: images) {
      T item;
      if (SagaImage::load(image, item) && eligible(item)) ++n;
    }
    return n;
  }
  unsigned count() const
  {
    return count<Session>(sessions) + count<Consent>(consents) + count<Grant>(grants);
  }

  ZdbSagaStep(0, zum.request, Insert) {
    requestInsert<Fwd>(this, ZuMv(complete)); return {};
  }
  ZdbSagaRepeatStep(1, zum.session, Update, sessions.length()) {
    change<Fwd, false>(context->sessions, sessions, ZuMv(complete)); return {};
  }
  ZdbSagaRepeatStep(2, zum.consent, Update, consents.length()) {
    change<Fwd, false>(context->consents, consents, ZuMv(complete)); return {};
  }
  ZdbSagaRepeatStep(3, zum.grant, Update, grants.length()) {
    change<Fwd, false>(context->grants, grants, ZuMv(complete)); return {};
  }
  ZdbSagaRepeatStep(4, zum.session, Update, sessions.length()) {
    change<Fwd, true>(context->sessions, sessions, ZuMv(complete)); return {};
  }
  ZdbSagaRepeatStep(5, zum.consent, Update, consents.length()) {
    change<Fwd, true>(context->consents, consents, ZuMv(complete)); return {};
  }
  ZdbSagaRepeatStep(6, zum.grant, Update, grants.length()) {
    change<Fwd, true>(context->grants, grants, ZuMv(complete)); return {};
  }
  ZdbSagaStep(7, zum.request, Update) {
    requestComplete(this, {}, updated, ZuMv(complete)); return {};
  }
};
ZfbStruct(ZumAPI, Revoke,
  (sessions, (Ctor<0>),		BytesVec),
  (consents, (Ctor<1>),		BytesVec),
  (grants, (Ctor<2>),		BytesVec),
  (updated, (Ctor<3>),		Int64),
  (request, (Ctor<4>),		UDT));

struct SSFDeliveryAdd : public ZdbSagaBase<DBContext> {
  using Base = ZdbSagaBase<DBContext>;
  using Base::context;
  using Base::saga;
  using Type = ZuStringT<"ssfDeliveryAdd.v1">;
  enum { NSteps = 2 };

  SSFDelivery delivery;

  ZdbSagaStep(0, zum.ssf_delivery, Insert) {
    auto table = context->ssfDeliveries;
    table->run(0, [this, table, complete = ZuMv(complete)]() mutable {
      if constexpr (Fwd) {
        ZdbRowRef<SSFDelivery> row =
          new ZdbRow<SSFDelivery>{table, ZdbShard{0}};
        saga->insert(table, ZuMv(row), ZuMv(complete),
          [this](ZdbRow<SSFDelivery> *row, auto &&complete) mutable {
            if (!row) { complete(false); return; }
            new (row->ptr()) SSFDelivery{delivery};
            row->data().owner = saga->id();
            complete(row->commit());
          });
      } else {
        saga->findDel<0>(table, 0,
          ZuFwdTuple(delivery.eventID, delivery.receiverID),
          ZuMv(complete), [this](ZdbRow<SSFDelivery> *row,
              auto &&complete) mutable {
            if (!row || row->data().owner != saga->id()) {
              complete(true);
              return;
            }
            complete(row->commit());
          });
      }
    });
    return {};
  }
  ZdbSagaStep(1, zum.ssf_delivery, Update) {
    auto table = context->ssfDeliveries;
    table->run(0, [this, table, complete = ZuMv(complete)]() mutable {
      saga->findUpd<0>(table, 0,
        ZuFwdTuple(delivery.eventID, delivery.receiverID), ZuMv(complete),
        [this](ZdbRow<SSFDelivery> *row, auto &&complete) mutable {
          if (!row) { complete(false); return; }
          auto &item = row->data();
          if constexpr (Fwd) {
            if (item.owner != saga->id()) { complete(false); return; }
            item.owner = 0;
          } else {
            if (item.owner) { complete(false); return; }
            item.owner = saga->id();
          }
          complete(row->commit());
        });
    });
    return {};
  }
};
ZfbStruct(ZumAPI, SSFDeliveryAdd,
  (delivery, (Ctor<0>),		UDT));

// Maintenance deletes are terminal: their reverse saga steps are idempotent
// no-ops.  These images retain only the key and fields used to reject a row
// that changed after selection; they never retain credentials or token data.
struct GrantDelete {
  Bytes		id;
  Bytes		digest;
  int64_t	expires = 0;
  State::T	state = State::Pending;
  uint128_t	owner = 0;
};
ZfbStruct(ZumAPI, GrantDelete,
  (id, (Ctor<0>),				Bytes),
  (digest, (Ctor<1>),				Bytes),
  (expires, (Ctor<2>),				Int64),
  (state, (Ctor<3>, Enum<State::Map>),		Int8),
  (owner, (Ctor<4>),				UInt128));
ZuDerive(GrantDeleteVec, (ZtArray<GrantDelete,
  ZtArrayHeapID<"Zum.Maintenance.Grant">>));
inline GrantDelete grantDelete(const Grant &item) {
  return {item.id, item.digest, item.expires, item.state, item.owner};
}
template <typename Tuple> inline GrantDelete grantDelete(const Tuple &item) {
  constexpr unsigned id = ZuTypeIndex<ZuStringT<"id">, ZuFieldIDs<Grant>>{};
  constexpr unsigned digest = ZuTypeIndex<ZuStringT<"digest">, ZuFieldIDs<Grant>>{};
  constexpr unsigned expires = ZuTypeIndex<ZuStringT<"expires">, ZuFieldIDs<Grant>>{};
  constexpr unsigned state = ZuTypeIndex<ZuStringT<"state">, ZuFieldIDs<Grant>>{};
  constexpr unsigned owner = ZuTypeIndex<ZuStringT<"owner">, ZuFieldIDs<Grant>>{};
  return {item.template p<id>(), item.template p<digest>(),
    item.template p<expires>(), item.template p<state>(), item.template p<owner>()};
}

struct SessionDelete {
  Bytes		digest;
  int64_t	idleDeadline = 0;
  int64_t	absoluteDeadline = 0;
  uint64_t	version = 0;
  uint128_t	owner = 0;
};
ZfbStruct(ZumAPI, SessionDelete,
  (digest, (Ctor<0>),			Bytes),
  (idleDeadline, (Ctor<1>),		Int64),
  (absoluteDeadline, (Ctor<2>),		Int64),
  (version, (Ctor<3>),			UInt64),
  (owner, (Ctor<4>),			UInt128));
ZuDerive(SessionDeleteVec, (ZtArray<SessionDelete,
  ZtArrayHeapID<"Zum.Maintenance.Session">>));
template <typename Tuple> inline SessionDelete sessionDelete(const Tuple &item) {
  constexpr unsigned digest = ZuTypeIndex<ZuStringT<"digest">, ZuFieldIDs<Session>>{};
  constexpr unsigned idle = ZuTypeIndex<ZuStringT<"idleDeadline">, ZuFieldIDs<Session>>{};
  constexpr unsigned absolute = ZuTypeIndex<ZuStringT<"absoluteDeadline">, ZuFieldIDs<Session>>{};
  constexpr unsigned version = ZuTypeIndex<ZuStringT<"version">, ZuFieldIDs<Session>>{};
  constexpr unsigned owner = ZuTypeIndex<ZuStringT<"owner">, ZuFieldIDs<Session>>{};
  return {item.template p<digest>(), item.template p<idle>(),
    item.template p<absolute>(), item.template p<version>(), item.template p<owner>()};
}

struct RefreshDelete {
  Bytes		id;
  int64_t	expires = 0;
  State::T	state = State::Pending;
  uint64_t	version = 0;
  uint128_t	owner = 0;
};
ZfbStruct(ZumAPI, RefreshDelete,
  (id, (Ctor<0>),				Bytes),
  (expires, (Ctor<1>),				Int64),
  (state, (Ctor<2>, Enum<State::Map>),		Int8),
  (version, (Ctor<3>),				UInt64),
  (owner, (Ctor<4>),				UInt128));
ZuDerive(RefreshDeleteVec, (ZtArray<RefreshDelete,
  ZtArrayHeapID<"Zum.Maintenance.Refresh">>));
template <typename Tuple> inline RefreshDelete refreshDelete(const Tuple &item) {
  constexpr unsigned id = ZuTypeIndex<ZuStringT<"id">, ZuFieldIDs<Refresh>>{};
  constexpr unsigned expires = ZuTypeIndex<ZuStringT<"expires">, ZuFieldIDs<Refresh>>{};
  constexpr unsigned state = ZuTypeIndex<ZuStringT<"state">, ZuFieldIDs<Refresh>>{};
  constexpr unsigned version = ZuTypeIndex<ZuStringT<"version">, ZuFieldIDs<Refresh>>{};
  constexpr unsigned owner = ZuTypeIndex<ZuStringT<"owner">, ZuFieldIDs<Refresh>>{};
  return {item.template p<id>(), item.template p<expires>(),
    item.template p<state>(), item.template p<version>(), item.template p<owner>()};
}

struct EvidenceDelete {
  AppID		appID = 0;
  UserID		userID = 0;
  ProviderID	providerID = 0;
  int64_t	deadline = 0;
  uint64_t	version = 0;
  int64_t	updated = 0;
  uint128_t	owner = 0;
};
ZfbStruct(ZumAPI, EvidenceDelete,
  (appID, (Ctor<0>),		UInt64),
  (userID, (Ctor<1>),		UInt64),
  (providerID, (Ctor<2>),	UInt64),
  (deadline, (Ctor<3>),		Int64),
  (version, (Ctor<4>),		UInt64),
  (updated, (Ctor<5>),		Int64),
  (owner, (Ctor<6>),		UInt128));
ZuDerive(EvidenceDeleteVec, (ZtArray<EvidenceDelete,
  ZtArrayHeapID<"Zum.Maintenance.Evidence">>));
template <typename Tuple> inline EvidenceDelete evidenceDelete(const Tuple &item) {
  constexpr unsigned app = ZuTypeIndex<ZuStringT<"appID">, ZuFieldIDs<Evidence>>{};
  constexpr unsigned user = ZuTypeIndex<ZuStringT<"userID">, ZuFieldIDs<Evidence>>{};
  constexpr unsigned provider = ZuTypeIndex<ZuStringT<"providerID">, ZuFieldIDs<Evidence>>{};
  constexpr unsigned deadline = ZuTypeIndex<ZuStringT<"deadline">, ZuFieldIDs<Evidence>>{};
  constexpr unsigned version = ZuTypeIndex<ZuStringT<"version">, ZuFieldIDs<Evidence>>{};
  constexpr unsigned updated = ZuTypeIndex<ZuStringT<"updated">, ZuFieldIDs<Evidence>>{};
  constexpr unsigned owner = ZuTypeIndex<ZuStringT<"owner">, ZuFieldIDs<Evidence>>{};
  return {item.template p<app>(), item.template p<user>(), item.template p<provider>(),
    item.template p<deadline>(), item.template p<version>(), item.template p<updated>(),
    item.template p<owner>()};
}

struct ConsentDelete {
  UserID		userID = 0;
  String		clientID;
  AppID		appID = 0;
  State::T	state = State::Pending;
  uint64_t	version = 0;
  int64_t	updated = 0;
  uint128_t	owner = 0;
};
ZfbStruct(ZumAPI, ConsentDelete,
  (userID, (Ctor<0>),				UInt64),
  (clientID, (Ctor<1>),				String),
  (appID, (Ctor<2>),				UInt64),
  (state, (Ctor<3>, Enum<State::Map>),		Int8),
  (version, (Ctor<4>),				UInt64),
  (updated, (Ctor<5>),				Int64),
  (owner, (Ctor<6>),				UInt128));
ZuDerive(ConsentDeleteVec, (ZtArray<ConsentDelete,
  ZtArrayHeapID<"Zum.Maintenance.Consent">>));

} // namespace Zum

namespace ZfbTransform {

template <typename C, typename F>
struct UDTArray {
  enum { IsInline = 0 };
  using Vec = Zfb::Vector<Zfb::Offset<F>>;

  template <
    typename = void, template <typename> class = ZuAlwaysTrue,
    typename Builder, typename Array>
  static auto save(Builder &fbb, const Array &a) {
    return Zfb::Save::vectorIter<F>(fbb, a.length(),
      [&a](Builder &fbb, uint64_t i) mutable {
	return ZfbStruct::save(fbb, a[i]);
      });
  }

  template <typename O>
  static O load(const Vec *v) {
    O a;
    if (!v) return a;
    for (uint64_t i = 0; i < v->size(); i++)
      a.push(ZfbStruct::ctor<C>(v->Get(i)));
    return a;
  }
};

struct GrantDeleteVec : UDTArray<Zum::GrantDelete, Zum::fbs::GrantDelete> {};
struct SessionDeleteVec : UDTArray<Zum::SessionDelete, Zum::fbs::SessionDelete> {};
struct RefreshDeleteVec : UDTArray<Zum::RefreshDelete, Zum::fbs::RefreshDelete> {};
struct EvidenceDeleteVec : UDTArray<Zum::EvidenceDelete, Zum::fbs::EvidenceDelete> {};
struct ConsentDeleteVec : UDTArray<Zum::ConsentDelete, Zum::fbs::ConsentDelete> {};

} // namespace ZfbTransform

namespace Zum {

ZfbTransform::GrantDeleteVec ZfbTransformer_(GrantDeleteVec *);
ZfbTransform::SessionDeleteVec ZfbTransformer_(SessionDeleteVec *);
ZfbTransform::RefreshDeleteVec ZfbTransformer_(RefreshDeleteVec *);
ZfbTransform::EvidenceDeleteVec ZfbTransformer_(EvidenceDeleteVec *);
ZfbTransform::ConsentDeleteVec ZfbTransformer_(ConsentDeleteVec *);
template <typename Tuple> inline ConsentDelete consentDelete(const Tuple &item) {
  constexpr unsigned user = ZuTypeIndex<ZuStringT<"userID">, ZuFieldIDs<Consent>>{};
  constexpr unsigned client = ZuTypeIndex<ZuStringT<"clientID">, ZuFieldIDs<Consent>>{};
  constexpr unsigned app = ZuTypeIndex<ZuStringT<"appID">, ZuFieldIDs<Consent>>{};
  constexpr unsigned state = ZuTypeIndex<ZuStringT<"state">, ZuFieldIDs<Consent>>{};
  constexpr unsigned version = ZuTypeIndex<ZuStringT<"version">, ZuFieldIDs<Consent>>{};
  constexpr unsigned updated = ZuTypeIndex<ZuStringT<"updated">, ZuFieldIDs<Consent>>{};
  constexpr unsigned owner = ZuTypeIndex<ZuStringT<"owner">, ZuFieldIDs<Consent>>{};
  return {item.template p<user>(), item.template p<client>(), item.template p<app>(),
    item.template p<state>(), item.template p<version>(), item.template p<updated>(),
    item.template p<owner>()};
}

struct GrantCleanup : public ZdbSagaBase<DBContext> {
  using Base = ZdbSagaBase<DBContext>;
  using Base::context;
  using Base::saga;
  using Type = ZuStringT<"grantCleanup.v3">;
  enum { NSteps = 3 };

  GrantDeleteVec grants;
  int64_t updated = 0;
  IdemRequest request;
  unsigned error = 0;

  ZdbSagaStep(0, zum.request, Insert) {
    requestInsert<Fwd>(this, ZuMv(complete)); return {};
  }
  ZdbSagaRepeatStep(1, zum.grant, Delete, grants.length()) {
    if constexpr (!Fwd) { complete(true); return {}; }
    const auto &before = grants[saga->iteration()];
    if (before.owner || before.state == State::Pending ||
        before.expires > updated || updated <= 0) {
      error = 400; complete(false); return {};
    }
    auto table = context->grants;
    table->run(0, [this, table, before, complete = ZuMv(complete)]() mutable {
      saga->template findDel<0>(table, 0, ZuFwdTuple(before.id),
        ZuMv(complete), [this, before = ZuMv(before)](
            ZdbRow<Grant> *row, auto &&complete) mutable {
          if (!row) { complete(true); return; }
          const auto &item = row->data();
          if (item.owner != before.owner || item.expires != before.expires ||
              item.state != before.state || item.digest != before.digest) {
            error = 409; complete(false); return;
          }
          complete(row->commit());
        });
    });
    return {};
  }
  ZdbSagaStep(2, zum.request, Update) {
    requestComplete(this, {}, updated, ZuMv(complete)); return {};
  }
};
ZfbStruct(ZumAPI, GrantCleanup,
  (grants, (Ctor<0>),		UDT),
  (updated, (Ctor<1>),		Int64),
  (request, (Ctor<2>),		UDT));

struct SessionCleanup : public ZdbSagaBase<DBContext> {
  using Base = ZdbSagaBase<DBContext>;
  using Base::context;
  using Base::saga;
  using Type = ZuStringT<"sessionCleanup.v2">;
  enum { NSteps = 2 };

  SessionDeleteVec sessions;
  int64_t updated = 0;
  unsigned error = 0;

  ZdbSagaRepeatStep(0, zum.session, Delete, sessions.length()) {
    if constexpr (!Fwd) { complete(true); return {}; }
    const auto &before = sessions[saga->iteration()];
    if (before.owner || updated <= 0 ||
        (before.idleDeadline > updated && before.absoluteDeadline > updated)) {
      error = 400; complete(false); return {};
    }
    auto table = context->sessions;
    table->run(0, [this, table, before, complete = ZuMv(complete)]() mutable {
      saga->template findDel<0>(table, 0, ZuFwdTuple(before.digest),
        ZuMv(complete), [this, before = ZuMv(before), updated = this->updated](
            ZdbRow<Session> *row, auto &&complete) mutable {
          if (!row) { complete(true); return; }
          const auto &item = row->data();
          if (item.owner != before.owner || item.version != before.version ||
              item.idleDeadline != before.idleDeadline ||
              item.absoluteDeadline != before.absoluteDeadline ||
              (item.idleDeadline > updated && item.absoluteDeadline > updated)) {
            error = 409; complete(false); return;
          }
          complete(row->commit());
        });
    });
    return {};
  }
  ZdbSagaStep(1, zum.session, Update) { complete(true); return {}; }
};
ZfbStruct(ZumAPI, SessionCleanup,
  (sessions, (Ctor<0>),		UDT),
  (updated, (Ctor<1>),		Int64));

struct RefreshCleanup : public ZdbSagaBase<DBContext> {
  using Base = ZdbSagaBase<DBContext>;
  using Base::context;
  using Base::saga;
  using Type = ZuStringT<"refreshCleanup.v2">;
  enum { NSteps = 2 };

  RefreshDeleteVec refreshes;
  int64_t updated = 0;
  unsigned error = 0;

  ZdbSagaRepeatStep(0, zum.refresh, Delete, refreshes.length()) {
    if constexpr (!Fwd) { complete(true); return {}; }
    const auto &before = refreshes[saga->iteration()];
    if (before.owner || updated <= 0 || before.expires > updated) {
      error = 400; complete(false); return {};
    }
    auto table = context->refresh;
    table->run(0, [this, table, before, complete = ZuMv(complete)]() mutable {
      saga->template findDel<0>(table, 0, ZuFwdTuple(before.id),
        ZuMv(complete), [this, before = ZuMv(before)](
            ZdbRow<Refresh> *row, auto &&complete) mutable {
          if (!row) { complete(true); return; }
          const auto &item = row->data();
          if (item.owner != before.owner || item.version != before.version ||
              item.expires != before.expires || item.state != before.state) {
            error = 409; complete(false); return;
          }
          complete(row->commit());
        });
    });
    return {};
  }
  ZdbSagaStep(1, zum.refresh, Update) { complete(true); return {}; }
};
ZfbStruct(ZumAPI, RefreshCleanup,
  (refreshes, (Ctor<0>),	UDT),
  (updated, (Ctor<1>),		Int64));

struct AppCleanup : public ZdbSagaBase<DBContext> {
  using Base = ZdbSagaBase<DBContext>;
  using Base::context;
  using Base::saga;
  using Type = ZuStringT<"appCleanup.v2">;
  enum { NSteps = 3 };

  AppID appID = 0;
  EvidenceDeleteVec evidence;
  ConsentDeleteVec consents;
  int64_t updated = 0;
  unsigned error = 0;

  ZdbSagaRepeatStep(0, zum.evidence, Delete, evidence.length()) {
    if constexpr (!Fwd) { complete(true); return {}; }
    const auto &before = evidence[saga->iteration()];
    if (!before.appID || before.appID != appID || before.owner || updated <= 0) {
      error = 400; complete(false); return {};
    }
    auto table = context->evidence;
    table->run(0, [this, table, before, complete = ZuMv(complete)]() mutable {
      saga->template findDel<0>(table, 0,
        ZuFwdTuple(before.appID, before.userID, before.providerID),
        ZuMv(complete), [this, before = ZuMv(before)](
            ZdbRow<Evidence> *row, auto &&complete) mutable {
          if (!row) { complete(true); return; }
          const auto &item = row->data();
          if (item.owner != before.owner || item.appID != before.appID ||
              item.version != before.version || item.updated != before.updated ||
              item.deadline != before.deadline) {
            error = 409; complete(false); return;
          }
          complete(row->commit());
        });
    });
    return {};
  }
  ZdbSagaRepeatStep(1, zum.consent, Delete, consents.length()) {
    if constexpr (!Fwd) { complete(true); return {}; }
    const auto &before = consents[saga->iteration()];
    if (!before.appID || before.appID != appID || before.owner || updated <= 0) {
      error = 400; complete(false); return {};
    }
    auto table = context->consents;
    table->run(0, [this, table, before, complete = ZuMv(complete)]() mutable {
      saga->template findDel<0>(table, 0,
        ZuFwdTuple(before.userID, before.clientID, before.appID),
        ZuMv(complete), [this, before = ZuMv(before)](
            ZdbRow<Consent> *row, auto &&complete) mutable {
          if (!row) { complete(true); return; }
          const auto &item = row->data();
          if (item.owner != before.owner || item.appID != before.appID ||
              item.version != before.version || item.updated != before.updated ||
              item.state != before.state) {
            error = 409; complete(false); return;
          }
          complete(row->commit());
        });
    });
    return {};
  }
  ZdbSagaStep(2, zum.app, Update) { complete(true); return {}; }
};
ZfbStruct(ZumAPI, AppCleanup,
  (appID, (Ctor<0>),		UInt64),
  (evidence, (Ctor<1>),		UDT),
  (consents, (Ctor<2>),		UDT),
  (updated, (Ctor<3>),		Int64));


// The compact payloads above deliberately use new saga type identifiers.
// Retain the previous decoders so an interrupted deployment reports its old
// maintenance saga as failed instead of making database recovery fatal.
struct GrantCleanupV2 : public ZdbSagaBase<DBContext> {
  using Base = ZdbSagaBase<DBContext>;
  using Type = ZuStringT<"grantCleanup.v2">;
  enum { NSteps = 3 };
  BytesVec grants;
  int64_t updated = 0;
  IdemRequest request;
  ZdbSagaStep(0, zum.request, Insert) { complete(false); return {}; }
  ZdbSagaRepeatStep(1, zum.grant, Delete, grants.length()) {
    complete(false); return {};
  }
  ZdbSagaStep(2, zum.request, Update) { complete(false); return {}; }
};
ZfbStruct(ZumAPI, GrantCleanupV2,
  (grants, (Ctor<0>),		BytesVec),
  (updated, (Ctor<1>),		Int64),
  (request, (Ctor<2>),		UDT));

struct SessionCleanupV1 : public ZdbSagaBase<DBContext> {
  using Base = ZdbSagaBase<DBContext>;
  using Type = ZuStringT<"sessionCleanup.v1">;
  enum { NSteps = 2 };
  BytesVec sessions;
  int64_t updated = 0;
  ZdbSagaRepeatStep(0, zum.session, Delete, sessions.length()) {
    complete(false); return {};
  }
  ZdbSagaStep(1, zum.session, Update) { complete(false); return {}; }
};
ZfbStruct(ZumAPI, SessionCleanupV1,
  (sessions, (Ctor<0>),		BytesVec),
  (updated, (Ctor<1>),		Int64));

struct RefreshCleanupV1 : public ZdbSagaBase<DBContext> {
  using Base = ZdbSagaBase<DBContext>;
  using Type = ZuStringT<"refreshCleanup.v1">;
  enum { NSteps = 2 };
  BytesVec refreshes;
  int64_t updated = 0;
  ZdbSagaRepeatStep(0, zum.refresh, Delete, refreshes.length()) {
    complete(false); return {};
  }
  ZdbSagaStep(1, zum.refresh, Update) { complete(false); return {}; }
};
ZfbStruct(ZumAPI, RefreshCleanupV1,
  (refreshes, (Ctor<0>),	BytesVec),
  (updated, (Ctor<1>),		Int64));

struct AppCleanupV1 : public ZdbSagaBase<DBContext> {
  using Base = ZdbSagaBase<DBContext>;
  using Type = ZuStringT<"appCleanup.v1">;
  enum { NSteps = 3 };
  AppID appID = 0;
  BytesVec evidence;
  BytesVec consents;
  int64_t updated = 0;
  ZdbSagaRepeatStep(0, zum.evidence, Delete, evidence.length()) {
    complete(false); return {};
  }
  ZdbSagaRepeatStep(1, zum.consent, Delete, consents.length()) {
    complete(false); return {};
  }
  ZdbSagaStep(2, zum.app, Update) { complete(false); return {}; }
};
ZfbStruct(ZumAPI, AppCleanupV1,
  (appID, (Ctor<0>),		UInt64),
  (evidence, (Ctor<1>),		BytesVec),
  (consents, (Ctor<2>),		BytesVec),
  (updated, (Ctor<3>),		Int64));

} // namespace Zum

#endif /* zumd_revoke_HH */
