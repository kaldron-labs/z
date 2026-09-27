//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuHex.hh>
#include <zlib/ZuTestUtil.hh>
#include <zlib/ZtPlatform.hh>
#include "ZiTestResidue.hh"
#include <zlib/ZiFile.hh>
#include <zlib/ZtlsRandom.hh>
#include <zlib/ZumTypes.hh>
#include <zlib/ZumVaultClient.hh>
#include <zlib/ZumVaultService.hh>

using namespace ZuTestUtil;

static void vaultCredentials()
{
  ZuTestScopeRT(vaultCredentials);
  Zi::Path home = ZiTestResidue::tmpDir("zum-vault-client");
  Zt::setenv("ZUMVAULTTEST_HOME", home);
  Zt::setenv("DBUS_SESSION_BUS_ADDRESS", "unsupported:address");
  constexpr auto Issuer = "https://issuer.example/oauth2/9"_Zu;
  constexpr auto Audience = "https://resource.example"_Zu;
  ZumVaultClient::Credential input{Issuer, Audience, "client",
    "access-token", "refresh-token", "read offline_access"};
  ZuCheckRT(!ZumVaultClient::save(input).is<ZeException>());
  ZumVaultClient::Credential loaded{Issuer, Audience, "client"};
  ZuCheckRT(!ZumVaultClient::load(loaded).is<ZeException>());
  ZuCheckRT(loaded.accessToken == input.accessToken &&
    loaded.refreshToken == input.refreshToken && loaded.scope == input.scope);
  ZuCheckRT(loaded.accessToken.mutable_() &&
    loaded.refreshToken.mutable_());
  ZumVaultClient::Credential separate{Issuer, Audience, "other-client"};
  ZuCheckRT(ZumVaultClient::load(separate).is<ZeException>());
  ZumVaultClient::Credential otherAudience{Issuer,
    "https://other.example", "client"};
  ZuCheckRT(ZumVaultClient::load(otherAudience).is<ZeException>());
  ZuCheckRT(!ZumVaultClient::save(input, "second@localhost")
    .is<ZeException>());
  ZumVaultClient::Credential otherAccount{Issuer, Audience, "client"};
  ZuCheckRT(!ZumVaultClient::load(otherAccount, "second@localhost")
    .is<ZeException>());
  ZuCheckRT(otherAccount.accessToken == input.accessToken);
  ZuCheckRT(ZumVaultClient::load(otherAccount, "third@localhost")
    .is<ZeException>());
  Zt::setenv("ZUMVAULTALT_HOME", ZiFile::append(home, "alt"));
  ZuCheckRT(!ZumVaultClient::save(input, "fourth@localhost", "zumvaultalt")
    .is<ZeException>());
  ZumVaultClient::Credential otherServiceAccount{Issuer, Audience, "client"};
  ZuCheckRT(!ZumVaultClient::load(otherServiceAccount,
    "fourth@localhost", "zumvaultalt").is<ZeException>());
  ZuCheckRT(ZumVaultClient::load(otherServiceAccount,
    "fourth@localhost").is<ZeException>());
  ZumVaultService::Credential serviceInput{
    Issuer, "service", "client-secret", "Bearer callback-token"};
  ZuCheckRT(!ZumVaultService::save(serviceInput)
    .is<ZeException>());
  ZumVaultService::Credential serviceLoaded{Issuer, "service"};
  ZuCheckRT(!ZumVaultService::load(serviceLoaded)
    .is<ZeException>());
  ZuCheckRT(serviceLoaded.clientSecret == serviceInput.clientSecret &&
    serviceLoaded.callbackAuth == serviceInput.callbackAuth);
  ZumVaultService::Credential otherService{Issuer, "other-service"};
  ZuCheckRT(ZumVaultService::load(otherService)
    .is<ZeException>());
  ZuCheckRT(!otherService.clientSecret && !otherService.callbackAuth);
  ZuCheckRT(!ZumVaultService::save(serviceInput,
    "second@localhost", "zumvaultalt").is<ZeException>());
  ZumVaultService::Credential serviceOtherAccount{Issuer, "service"};
  ZuCheckRT(!ZumVaultService::load(serviceOtherAccount,
    "second@localhost", "zumvaultalt").is<ZeException>());
  ZuCheckRT(serviceOtherAccount.clientSecret == serviceInput.clientSecret);
  ZuCheckRT(ZumVaultService::load(serviceOtherAccount,
    "third@localhost", "zumvaultalt").is<ZeException>());
  Ztls::VaultConfig cf;
  cf.store = Ztls::VaultStore::File;
  Ztls::Vault vault;
  ZuCheckRT(!vault.init(cf).is<ZeException>());
  ZuCheckRT(!vault.open().is<ZeException>());
  bool found = false;
  ZuCheckRT(!vault.load(Ztls::Scopes::Global{}, "oauth",
    [&found](ZuSpan<uint8_t>) { found = true; }).is<ZeException>());
  ZuCheckRT(found);
  ZuCheckRT(vault.load(Ztls::Scopes::Global{}, "zumvaultclienttest",
    [](ZuSpan<uint8_t>) {}).is<ZeException>());
  vault.close();
  vault.final();
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZiTestResidue::init("zumvaulttest");
  ZuTestMain();
  ZuTestCall(vaultCredentials);
  return 0;
}
