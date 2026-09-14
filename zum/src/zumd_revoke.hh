//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// bounded bulk revocation sagas

#ifndef zumd_revoke_HH
#define zumd_revoke_HH

#ifndef ZumLib_HH
#include <zlib/ZumLib.hh>
#endif

#include <zlib/zumd_identity_db.hh>
#include <zlib/zumd_request_db.hh>
#include <zlib/zumd_saga_image.hh>
#include <zlib/zumd_oidc.hh>
#include <zlib/zumd_key_db.hh>
#include <zlib/zumd_token.hh>
#include <zlib/zumd_request.hh>
#include <zlib/zumd_ssf_db.hh>
#include <zlib/zum_saga_fbs.h>
#include <zlib/ZuBase64URL.hh>
#include <zlib/ZtlsCOSE.hh>
#include <zlib/ZtlsMD.hh>

namespace Zum {

struct DB;

ZuDerive(SSFSETFn, (ZmFn<void(String),
  ZmFnHeapID<"Zum.SSFSETFn">>));

struct SSFSubjectWire {
  String format;
  String issuer;
  String familyID;
  int64_t expires = 0;
};
ZfStruct(, (SSFSubjectWire, JSON),
  (((format), (Required)), (String)),
  (((issuer), (JSON::ID<"iss">, Required)), (String)),
  (((familyID), (JSON::ID<"family_id">, Required)), (String)),
  (((expires), (JSON::ID<"exp">, Required)), (Int64)));

struct SSFEventWire {
  SSFSubjectWire subject;
};
ZfStruct(, (SSFEventWire, JSON),
  (((subject), (Required)), (UDT)));
struct SSFEventsWire {
  SSFEventWire revoked;
};
ZfStruct(, (SSFEventsWire, JSON),
  (((revoked), (JSON::ID<"urn:zum:events:refresh-token-revoked">, Required)),
    (UDT)));
struct SSFClaimsWire {
  String issuer;
  String audience;
  String id;
  int64_t issued = 0;
  SSFEventsWire events;
};
ZfStruct(, (SSFClaimsWire, JSON),
  (((issuer), (JSON::ID<"iss">, Required)), (String)),
  (((audience), (JSON::ID<"aud">, Required)), (String)),
  (((id), (JSON::ID<"jti">, Required)), (String)),
  (((issued), (JSON::ID<"iat">, Required)), (Int64)),
  (((events), (Required)), (UDT)));

struct SSFHeaderWire {
  String algorithm;
  String type;
  String keyID;
};
ZfStruct(, (SSFHeaderWire, JSON),
  (((algorithm), (JSON::ID<"alg">, Required)), (String)),
  (((type), (JSON::ID<"typ">, Required)), (String)),
  (((keyID), (JSON::ID<"kid">, Required)), (String)));

inline void makeSSF(
    const SignKey &key, ZuCSpan audience, const RefreshID &refreshID,
    int64_t expires, int64_t now, SignFn sign, SSFSETFn complete)
{
  if (!key.id || !key.issuer || !audience || !refreshID.issuer ||
      !refreshID.familyID || expires <= now || now <= 0 || !sign || !complete) {
    if (complete) complete(String{});
    return;
  }
  String eventID{refreshID.issuer};
  eventID << ':' << refreshID.familyID;
  eventID << ':' << now;
  String header;
  ZfJSON::save(header, SSFHeaderWire{"ES256", "secevent+jwt", key.id});
  String header64;
  header64.length(ZuBase64URL::enclen(header.length()));
  if (ZuBase64URL::encode(header64.span(), ZuBSpan{header}) != header64.length()) {
    complete(String{});
    return;
  }
  String claims;
  ZfJSON::save(claims, SSFClaimsWire{
    key.issuer, audience, ZuMv(eventID), now,
    {.revoked = SSFEventWire{SSFSubjectWire{
      "opaque", refreshID.issuer, refreshID.familyID, expires}}}});
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

ZuDerive(SSFReceiverVec, (ZtArray<SSFRx,
  ZtArrayHeapID<"Zum.SSFReceiverVec">>));

// Resolve a configured secret reference to the complete Authorization value
// required by the receiver.  The clear value is transient and is never put in
// an SSFDelivery row.
ZuDerive(SSFSecretFn, (ZmFn<String(String),
  ZmFnHeapID<"Zum.SSFSecretFn">>));
ZuDerive(SSFKeyFn, (ZmFn<void(String, SignKeyFn),
  ZmFnHeapID<"Zum.SSFKeyFn">>));

struct SSFTransmitterConfig {
  DB			*db = nullptr;
  DBContext		*context = nullptr;
  Requests		*requests = nullptr;
  String		issuer;
  SignKey		key;
  SSFKeyFn		keyLoad;
  SignFn		sign;
  OIDCHTTPFn		http;
  SSFSecretFn		secret;
  SSFReceiverVec	receivers;
  unsigned		receiverMax = 1024;
  uint32_t		retryBase = 1;
  uint32_t		retryMax = 60;
};

class SSFTransmitter : public ZumObject {
public:
  ~SSFTransmitter() { clear_(); }
  bool init(SSFTransmitterConfig);
  void start();
  void stop();
  void revoke(AppID, RefreshID, int64_t);

private:
  void revoke_(AppID, RefreshID, int64_t);
  void configure_(unsigned);
  void receivers_(AppID, RefreshID, int64_t);
  void issue_(SSFRx, RefreshID, int64_t, int64_t);
  void persist_(SSFRx, SSFDelivery, String);
  void deliver_(SSFRx, SSFDelivery, String);
  void retry_();
  void retryPage_(SSFDeliveryTable::Key<0>, bool);
  void arm_(int64_t);
  void armOn_(int64_t);
  void clear_();

  SSFTransmitterConfig	m_config;
  ZmScheduler::Timer	m_timer;
  bool			m_armed = false;
  bool			m_started = false;
};

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
      if (body.mutable_()) ZuClear(body.data(), body.length());
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
  using Type = ZuStringT<"revoke.v1">;
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
		    valid &= item.generation == before.generation && item.digest == before.digest;
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
  (((sessions), (Ctor<0>)), (BytesVec)),
  (((consents), (Ctor<1>)), (BytesVec)),
  (((grants), (Ctor<2>)), (BytesVec)),
  (((updated), (Ctor<3>)), (Int64)),
  (((request), (Ctor<4>)), (UDT)));

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
  (((delivery), (Ctor<0>)), (UDT)));

struct GrantCleanup : public ZdbSagaBase<DBContext> {
  using Base = ZdbSagaBase<DBContext>;
  using Base::context;
  using Base::saga;
  using Type = ZuStringT<"grantCleanup.v1">;
  enum { NSteps = 3 };

  BytesVec grants;
  int64_t updated = 0;
  IdemRequest request;
  unsigned error = 0;

  ZdbSagaStep(0, zum.request, Insert) {
    requestInsert<Fwd>(this, ZuMv(complete)); return {};
  }
  ZdbSagaRepeatStep(1, zum.grant, Delete, grants.length()) {
    Grant before;
    if (!SagaImage::load(grants[saga->iteration()], before) ||
	before.owner || before.expires > updated || updated <= 0) {
      error = 400; complete(false); return {};
    }
    auto table = context->grants;
    table->run(0, [this, table, before = ZuMv(before), complete = ZuMv(complete)]() mutable {
      if constexpr (Fwd) {
	auto id = before.id;
	saga->template findDel<0>(table, 0, ZuFwdTuple(ZuMv(id)), ZuMv(complete),
	  [this, before = ZuMv(before)](ZdbRow<Grant> *row, auto &&complete) mutable {
	    if (!row || row->data().owner || row->data().expires != before.expires ||
		row->data().generation != before.generation ||
		row->data().state != before.state || row->data().digest != before.digest) {
	      error = 409; complete(false); return;
	    }
	    complete(row->commit());
	  });
      } else {
	ZdbRowRef<Grant> row = new ZdbRow<Grant>{table, ZdbShard{0}};
	saga->insert(table, ZuMv(row), ZuMv(complete),
	  [before = ZuMv(before)](ZdbRow<Grant> *row, auto &&complete) mutable {
	    new (row->ptr()) Grant{ZuMv(before)};
	    complete(row->commit());
	  });
      }
    });
    return {};
  }
  ZdbSagaStep(2, zum.request, Update) {
    requestComplete(this, {}, updated, ZuMv(complete)); return {};
  }
};
ZfbStruct(ZumAPI, GrantCleanup,
  (((grants), (Ctor<0>)), (BytesVec)),
  (((updated), (Ctor<1>)), (Int64)),
  (((request), (Ctor<2>)), (UDT)));

} // namespace Zum

#endif /* zumd_revoke_HH */
