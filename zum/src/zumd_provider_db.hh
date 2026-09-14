//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// typed server tables

#ifndef zumd_provider_db_HH
#define zumd_provider_db_HH

#ifndef ZumLib_HH
#include <zlib/ZumLib.hh>
#endif

#include <zlib/Zdb.hh>
#include <zlib/zumd.hh>
#include <zlib/zumd_db_context.hh>

namespace Zum {

ZdbTableDerive(ProviderTable, Provider);
ZdbTableDerive(ExtIdentityTable, ExtIdentity);
ZdbTableDerive(RoleMapTable, RoleMap);
ZdbTableDerive(EvidenceTable, Evidence);

} // namespace Zum

#endif /* zumd_provider_db_HH */
