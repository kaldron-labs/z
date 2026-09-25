//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// application-facing credential vault

#ifndef ZtlsVault_HH
#define ZtlsVault_HH

#ifndef ZtlsLib_HH
#include <zlib/ZtlsLib.hh>
#endif

#include <zlib/ZuDerive.hh>
#include <zlib/ZuSpan.hh>
#include <zlib/ZuUnion.hh>

#include <zlib/ZmFn.hh>

#include <zlib/ZtEnum.hh>
#include <zlib/ZtString.hh>

#include <zlib/ZePlatform.hh>

namespace Ztls {

ZtEnumNS(ZtlsAPI, VaultStore, int8_t, Ephemeral, File, KeyRing, Module, Auto);
ZtEnumNS(ZtlsAPI, VaultVariant, int8_t, Default, Direct, Secrets);

using VaultResult = ZuUnion<void, ZeException>;
using VaultLoadFn = ZmFn<void(ZuBSpan), ZmFnHeapID<"Ztls.Vault.LoadFn">>;
using VaultString = ZtString<ZtStringHeapID<"Ztls.Vault.Config">>;

namespace Scopes {
struct Global { };
struct Environment { ZuCSpan name; };
using Union = ZuUnion<Global, Environment>;
}
ZuDerive(Scope, Scopes::Union);

struct VaultConfig {
  VaultString service;
  VaultString account;
  VaultString envPrefix;
  VaultString module;
  int store = VaultStore::Auto;
  int variant = VaultVariant::Default;
};

class ZtlsAPI Vault {
public:
  Vault();
  ~Vault();

  Vault(const Vault &) = delete;
  Vault &operator =(const Vault &) = delete;

  VaultResult init(const VaultConfig &);
  VaultResult start();
  void stop();
  void final();

  VaultResult load(Scope, ZuCSpan name, VaultLoadFn);
  VaultResult save(Scope, ZuCSpan name, ZuBSpan);

private:
  struct State;
  State *m_state = nullptr;
};

}

#endif /* ZtlsVault_HH */
