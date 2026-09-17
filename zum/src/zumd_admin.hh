//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// typed Zum administration operations

#ifndef zumd_admin_HH
#define zumd_admin_HH

#ifndef ZumLib_HH
#include <zlib/ZumLib.hh>
#endif

#include <zlib/zumd.hh>
#include <zlib/ZumMgmt.hh>

namespace Zum {


ZumExtern String auditID(ZuBSpan);
ZumExtern void logEvent(Audit);
ZumExtern Audit managementAuditRecord(
  String issuer, int operation, String actor, AppID, String target,
  String correlationID, unsigned status, int64_t now);

} // namespace Zum

#endif /* zumd_admin_HH */
