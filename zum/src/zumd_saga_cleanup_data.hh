//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef zumd_saga_cleanup_data_HH
#define zumd_saga_cleanup_data_HH

#include <zlib/zumd.hh>
#include <zlib/ZiFBReflect.hh>

namespace Zum {

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
ZfStruct(ZumAPI, (GrantDelete, FB),
  (id,		(Ctor<0>),			Bytes),
  (digest,	(Ctor<1>),			Bytes),
  (expires,	(Ctor<2>),			Int64),
  (state,	(Ctor<3>, Enum<State::Map>),	Int8),
  (owner,	(Ctor<4>),			UInt128));
ZiFB::AsTable<"Zum.fbs.GrantDelete"> ZiFB_Fmt(GrantDelete *);
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

struct RefreshDelete {
  Bytes		id;
  int64_t	expires = 0;
  State::T	state = State::Pending;
  uint64_t	version = 0;
  uint128_t	owner = 0;
};
ZfStruct(ZumAPI, (RefreshDelete, FB),
  (id,		(Ctor<0>),			Bytes),
  (expires,	(Ctor<1>),			Int64),
  (state,	(Ctor<2>, Enum<State::Map>),	Int8),
  (version,	(Ctor<3>),			UInt64),
  (owner,	(Ctor<4>),			UInt128));
ZiFB::AsTable<"Zum.fbs.RefreshDelete"> ZiFB_Fmt(RefreshDelete *);
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
ZfStruct(ZumAPI, (EvidenceDelete, FB),
  (appID,	(Ctor<0>),	UInt64),
  (userID,	(Ctor<1>),	UInt64),
  (providerID,	(Ctor<2>),	UInt64),
  (deadline,	(Ctor<3>),	Int64),
  (version,	(Ctor<4>),	UInt64),
  (updated,	(Ctor<5>),	Int64),
  (owner,	(Ctor<6>),	UInt128));
ZiFB::AsTable<"Zum.fbs.EvidenceDelete"> ZiFB_Fmt(EvidenceDelete *);
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
ZfStruct(ZumAPI, (ConsentDelete, FB),
  (userID,	(Ctor<0>),			UInt64),
  (clientID,	(Ctor<1>),			String),
  (appID,	(Ctor<2>),			UInt64),
  (state,	(Ctor<3>, Enum<State::Map>),	Int8),
  (version,	(Ctor<4>),			UInt64),
  (updated,	(Ctor<5>),			Int64),
  (owner,	(Ctor<6>),			UInt128));
ZiFB::AsTable<"Zum.fbs.ConsentDelete"> ZiFB_Fmt(ConsentDelete *);
ZuDerive(ConsentDeleteVec, (ZtArray<ConsentDelete,
  ZtArrayHeapID<"Zum.Maintenance.Consent">>));

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

} // namespace Zum

#endif /* zumd_saga_cleanup_data_HH */
