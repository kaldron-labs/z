//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/zumd_saga_admin_ops.hh>

namespace Zum {

bool clientConfigValid(
    ClientProfile::T profile, uint8_t grants, bool refreshAllowed,
    const StringVec &redirects)
{
  constexpr uint8_t all = ClientGrant::AuthCode() |
    ClientGrant::ClientCredentials() | ClientGrant::Refresh();
  if (profile < 0 || profile >= ClientProfile::N || !grants || (grants & ~all))
    return false;
  if ((grants & ClientGrant::AuthCode()) && !redirects) return false;
  if (refreshAllowed && !(grants & ClientGrant::Refresh())) return false;
  if (clientType(profile) != ClientType::Confidential &&
      (grants & ClientGrant::ClientCredentials())) return false;
  return true;
}

} // namespace Zum
