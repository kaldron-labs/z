//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Short-lived Vault access shared by Zum credential stores.

#ifndef ZumVaultUtil_HH
#define ZumVaultUtil_HH

#include <zlib/ZumLib.hh>

#include <zlib/ZtlsVault.hh>

namespace ZumVaultUtil {

template <typename Fn>
ZuUnion<void, ZeException> withVault(
    ZuCSpan program, ZuCSpan account, Fn &&fn)
{
  Ztls::VaultConfig cf;
  cf.program = program;
  cf.account = account;
  Ztls::Vault vault;
  auto result = vault.init(cf);
  if (result.is<ZeException>()) return result;
  result = vault.open();
  if (result.is<ZeException>()) {
    vault.final();
    return result;
  }
  result = ZuFwd<Fn>(fn)(vault);
  vault.close();
  vault.final();
  return result;
}

} // ZumVaultUtil

#endif /* ZumVaultUtil_HH */
