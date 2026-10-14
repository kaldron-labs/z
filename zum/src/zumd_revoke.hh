//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// security event delivery and revocation service helpers

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
#include <zlib/zumd_oidc.hh>
#include <zlib/zumd_key_db.hh>
#include <zlib/zumd_token.hh>
#include <zlib/zumd_request.hh>
#include <zlib/zumd_ssf_db.hh>
#include <zlib/ZuBase64URL.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmObject.hh>
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
  DB		*db = nullptr;
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

} // namespace Zum

#endif /* zumd_revoke_HH */
