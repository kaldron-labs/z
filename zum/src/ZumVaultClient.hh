//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Short-lived Vault access for public OAuth client tokens; this does not
// make the OAuth client confidential.

#ifndef ZumVaultClient_HH
#define ZumVaultClient_HH

#include <zlib/ZumLib.hh>

#include <zlib/ZtString.hh>
#include <zlib/ZtScratch.hh>

#include <zlib/ZfJSON.hh>

#include <zlib/ZumVaultUtil.hh>

namespace ZumVaultClient {

using Text = ZtString<ZtStringHeapID<"Zum.Vault.Client">>;
using SecretText = ZtString<ZtStringSecret<true,
  ZtStringHeapID<"Zum.Vault.Client">>>;

struct Credential {
  Text issuerURL;
  Text audience;
  Text clientID;
  SecretText accessToken;
  SecretText refreshToken;
  Text scope;
};
ZfStruct(, (Credential, JSON),
  (((issuerURL), (JSON::ID<"issuer">, Required)), (String)),
  (((audience), (Required)), (String)),
  (((clientID), (Required)), (String)),
  (((accessToken), (Required)), (String)),
  (((refreshToken), (Required)), (String)),
  (((scope), (JSON::Opt)), (String)));

inline Ztls::VaultResult save(
    const Credential &credential, ZuCSpan account = {}, ZuCSpan program = {})
{
  if (!credential.accessToken || !credential.refreshToken)
    return ZeEXCEPT(Error, "ZumVaultClient", "missing token");
  return ZumVaultUtil::withVault(program, account,
    [&credential](Ztls::Vault &vault) {
      auto text = ZtScratch(SecretText, 512);
      ZfJSON::save(text, credential);
      return vault.save(Ztls::Scopes::Global{}, "oauth", text);
    });
}

inline Ztls::VaultResult load(
    Credential &credential, ZuCSpan account = {}, ZuCSpan program = {})
{
  return ZumVaultUtil::withVault(program, account,
    [&credential](Ztls::Vault &vault) {
      bool valid = false;
      auto result = vault.load(Ztls::Scopes::Global{}, "oauth",
	[&credential, &valid](ZuSpan<uint8_t> value) {
          auto parsed = ZfJSON::scan(ZuSpan<char>{value});
          if (parsed.p<0>() != int(value.length()) || !parsed.p<1>() ||
              !parsed.p<1>()->has<ZfJSON::AnyNode::Array>()) return;
          auto &roots = parsed.p<1>()->data<ZfJSON::AnyNode::Array>();
          if (roots.length() != 1) return;
          auto handler = ZfJSON::handler<Credential>(roots[0]);
          if (!handler.valid) return;
          Text issuerURL{ZuMv(credential.issuerURL)};
          Text audience{ZuMv(credential.audience)};
          Text clientID{ZuMv(credential.clientID)};
          handler.load(credential);
          valid = credential.issuerURL == issuerURL &&
            credential.audience == audience &&
            credential.clientID == clientID && credential.accessToken &&
            credential.refreshToken;
          if (!valid) {
            credential = Credential{ZuMv(issuerURL), ZuMv(audience),
              ZuMv(clientID)};
          }
        });
      if (result.is<ZeException>()) return result;
      if (!valid)
        return Ztls::VaultResult{ZeEXCEPT(Error, "ZumVaultClient",
          "invalid credential")};
      return result;
    });
}

} // ZumVaultClient

#endif /* ZumVaultClient_HH */
