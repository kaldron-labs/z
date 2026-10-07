//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Short-lived Vault access for Zum service credentials.

#ifndef ZumVaultService_HH
#define ZumVaultService_HH

#include <zlib/ZumLib.hh>

#include <zlib/ZtString.hh>
#include <zlib/ZtScratch.hh>

#include <zlib/ZfJSON.hh>

#include <zlib/ZumVaultUtil.hh>

namespace ZumVaultService {

using Text = ZtString<ZtStringHeapID<"Zum.Vault.Service">>;
using SecretText = ZtString<ZtStringSecret<true,
  ZtStringHeapID<"Zum.Vault.Service">>>;
ZuDerive(SecretScratch, (ZtString<ZtStringSharded<true,
  ZtStringSecret<true, ZtStringHeapID<"Zum.Vault.Service">>>>));

struct Credential {
  Text issuerURL;
  Text clientID;
  SecretText clientSecret;
  SecretText callbackAuth;
};
ZfStruct(, (Credential, JSON),
  (issuerURL, (Required),		String),
  (clientID, (Required),		String),
  (clientSecret, (Required),		String),
  (callbackAuth, (Required),		String));

inline Ztls::VaultResult save(
    const Credential &credential, ZuCSpan account = {}, ZuCSpan program = {})
{
  return ZumVaultUtil::withVault(program, account,
    [&credential](Ztls::Vault &vault) {
      auto text = ZtScratch(SecretScratch, 512);
      ZfJSON::save(text, credential);
      return vault.save(Ztls::Scopes::Global{}, "serviceCredentials", text);
    });
}

inline Ztls::VaultResult load(
    Credential &credential, ZuCSpan account = {}, ZuCSpan program = {})
{
  return ZumVaultUtil::withVault(program, account,
    [&credential](Ztls::Vault &vault) {
      bool valid = false;
      auto result = vault.load(Ztls::Scopes::Global{}, "serviceCredentials",
	[&credential, &valid](ZuSpan<uint8_t> value) {
          auto parsed = ZfJSON::scan(ZuSpan<char>{value});
          if (parsed.p<0>() != int(value.length()) || !parsed.p<1>() ||
              !parsed.p<1>()->has<ZfJSON::AnyNode::Array>()) return;
          auto &roots = parsed.p<1>()->data<ZfJSON::AnyNode::Array>();
          if (roots.length() != 1) return;
          auto handler = ZfJSON::handler<Credential>(roots[0]);
          if (!handler.valid) return;
          Text issuerURL{ZuMv(credential.issuerURL)};
          Text clientID{ZuMv(credential.clientID)};
          handler.load(credential);
          valid = credential.issuerURL == issuerURL &&
            credential.clientID == clientID && credential.clientSecret &&
            credential.callbackAuth;
          if (!valid) {
            credential = Credential{ZuMv(issuerURL), ZuMv(clientID)};
          }
        });
      if (result.is<ZeException>()) return result;
      if (!valid)
        return Ztls::VaultResult{ZeEXCEPT(Error, "ZumVaultService",
          "invalid credential")};
      return result;
    });
}

} // ZumVaultService

#endif /* ZumVaultService_HH */
