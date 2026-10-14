//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Persisted session cleanup input shared by callers and saga owner.

#ifndef zumd_saga_session_cleanup_data_HH
#define zumd_saga_session_cleanup_data_HH

#include <zlib/zumd.hh>
#include <zlib/ZiFBReflect.hh>

namespace Zum {

struct SessionDelete {
  Bytes		digest;
  int64_t	idleDeadline = 0;
  int64_t	absoluteDeadline = 0;
  uint64_t	version = 0;
  uint128_t	owner = 0;
};
ZfStruct(ZumAPI, (SessionDelete, FB),
  (digest,		(Ctor<0>),	Bytes),
  (idleDeadline,	(Ctor<1>),	Int64),
  (absoluteDeadline,	(Ctor<2>),	Int64),
  (version,		(Ctor<3>),	UInt64),
  (owner,		(Ctor<4>),	UInt128));
ZiFB::AsTable<"Zum.fbs.SessionDelete"> ZiFB_Fmt(SessionDelete *);
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

} // namespace Zum

#endif /* zumd_saga_session_cleanup_data_HH */
