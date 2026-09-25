//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// credential vault backend module contract

#ifndef ZtlsVaultStore_HH
#define ZtlsVaultStore_HH

#ifndef ZtlsLib_HH
#include <zlib/ZtlsLib.hh>
#endif

#include <zlib/ZmPolymorph.hh>

#include <zlib/ZtlsVault.hh>

namespace Ztls_ {

class VaultStore : public ZmPolymorph {
public:
  virtual Ztls::VaultResult init(const Ztls::VaultConfig &) = 0;
  virtual void final() = 0;
  virtual Ztls::VaultResult load(ZuCSpan key, Ztls::VaultLoadFn) = 0;
  virtual Ztls::VaultResult save(ZuCSpan key, ZuBSpan) = 0;
};

typedef VaultStore *(*VaultStoreFn)();

}

extern "C" {
  typedef Ztls_::VaultStoreFn ZtlsVaultStoreFn;
  ZtlsExtern Ztls_::VaultStore *ZtlsVaultStore();
}
#define ZtlsVaultStoreFnSym "ZtlsVaultStore"

#endif /* ZtlsVaultStore_HH */
