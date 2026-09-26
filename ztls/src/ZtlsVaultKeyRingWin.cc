//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Windows Credential Manager backing for ZtlsVault.

#include <string.h>
#include <wincred.h>

#include <zlib/ZuDerive.hh>
#include <zlib/ZuHex.hh>

#include <zlib/ZmHeap.hh>

#include <zlib/ZtScratch.hh>

#include <zlib/ZiPlatform.hh>

#include <zlib/ZtlsVaultStore.hh>

namespace Ztls_ {

namespace VaultKeyRingWin_ {

constexpr auto TargetPrefix = "ZtlsVault:"_Zu;

template <typename Heap = ZuVoid>
class KeyRing_ : public Heap, public VaultStore {
public:
  Ztls::VaultResult init(const Ztls::VaultConfig &cf) override {
    m_program = cf.program;
    m_account = cf.account;
    return {};
  }
  void final() override {
    m_program.clear();
    m_account.clear();
  }

  Ztls::VaultResult load(ZuCSpan key, Ztls::VaultLoadFn fn) override {
    Zi::Path target;
    auto result = target_(key, target);
    if (result.template is<ZeException>()) return result;
    PCREDENTIAL credential = nullptr;
    if (!CredRead(target.data(), CRED_TYPE_GENERIC, 0, &credential)) {
      if (GetLastError() == ERROR_NOT_FOUND)
	return ZeEXCEPT(Error, "ZtlsVault", "credential missing");
      return ZeEXCEPT(Error, "ZtlsVault", "native credential read failed");
    }
    ZuGuard release{[&credential]() { CredFree(credential); }};
    fn(ZuSpan<uint8_t>{credential->CredentialBlob,
      credential->CredentialBlobSize});
    return {};
  }

  Ztls::VaultResult save(ZuCSpan key, ZuBSpan value) override {
    if (value.length() > CRED_MAX_CREDENTIAL_BLOB_SIZE)
      return ZeEXCEPT(Error, "ZtlsVault", "native credential too large");
    Zi::Path target;
    auto result = target_(key, target);
    if (result.template is<ZeException>()) return result;
    CREDENTIAL credential{};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = target.data();
    credential.CredentialBlobSize = DWORD(value.length());
    credential.CredentialBlob = value.length() ?
      const_cast<BYTE *>(value.data()) : nullptr;
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    if (!CredWrite(&credential, 0))
      return ZeEXCEPT(Error, "ZtlsVault", "native credential write failed");
    return {};
  }

private:
  Ztls::VaultResult target_(ZuCSpan key, Zi::Path &target) const {
    // Credential Manager compares targets without case. Canonical uppercase
    // hex preserves each UTF-8 byte without case-folding collisions.
    constexpr uint64_t limit = CRED_MAX_GENERIC_TARGET_NAME_LENGTH;
    if (m_program.length() > limit / 2 ||
	m_account.length() > limit / 2 || key.length() > limit / 2)
      return ZeEXCEPT(Error, "ZtlsVault", "native target too long");
    uint64_t length = TargetPrefix.length() + 2 +
      ZuHex::enclen(m_program.length()) +
      ZuHex::enclen(m_account.length()) + ZuHex::enclen(key.length());
    if (length > limit)
      return ZeEXCEPT(Error, "ZtlsVault", "native target too long");
    auto text = ZtScratch(Ztls::VaultString, length);
    auto out = text.data();
    uint64_t pos = TargetPrefix.length();
    ::memcpy(out, TargetPrefix.data(), pos);
    pos += ZuHex::encode({out + pos, length - pos}, m_program);
    out[pos++] = ':';
    pos += ZuHex::encode({out + pos, length - pos}, m_account);
    out[pos++] = ':';
    pos += ZuHex::encode({out + pos, length - pos}, key);
    text.length(pos);
    target = ZuCSpan{text};
    return {};
  }

  Ztls::VaultString m_program;
  Ztls::VaultString m_account;
};

ZuDerive(KeyRingHeap, (ZmHeap<"Ztls.Vault.KeyRing", KeyRing_<>>));
ZuDerive(KeyRing, KeyRing_<KeyRingHeap>);

} // VaultKeyRingWin_

VaultStore *vaultKeyRing() { return new VaultKeyRingWin_::KeyRing; }

} // Ztls_
