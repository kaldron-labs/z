//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Loadable vault store fixture: one record suffices for Direct and Indirect.

#include <zlib/ZuDerive.hh>
#include <string.h>

#include <zlib/ZmHeap.hh>

#include <zlib/ZtArray.hh>

#include <zlib/ZtlsVaultStore.hh>

namespace ZtlsVaultFixture_ {

using Value = ZtBArray<ZtArraySecret<true,
  ZtArrayHeapID<"Ztls.Vault.FixtureValue">>>;

template <typename Heap = ZuVoid>
class Store_ : public Heap, public Ztls_::VaultStore {
public:
  Ztls::VaultResult init(const Ztls::VaultConfig &) override { return {}; }
  void final() override {
    m_value.clear();
    m_key.clear();
  }
  Ztls::VaultResult load(ZuCSpan key, Ztls::VaultLoadFn fn) override {
    if (key != m_key)
      return ZeEXCEPT(Error, "ZtlsVaultFixture", "credential missing");
    fn(m_value);
    return {};
  }
  Ztls::VaultResult save(ZuCSpan key, ZuBSpan value) override {
    m_key = key;
    m_value = value;
    return {};
  }

private:
  Ztls::VaultString m_key;
  Value m_value;
};

ZuDerive(StoreHeap, (ZmHeap<"Ztls.Vault.Fixture", Store_<>>));
ZuDerive(Store, Store_<StoreHeap>);

} // ZtlsVaultFixture_

extern "C" Ztls_::VaultStore *ZtlsVaultStore()
{
  return new ZtlsVaultFixture_::Store;
}
