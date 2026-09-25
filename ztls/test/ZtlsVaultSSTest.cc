//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>

#include "../src/ZtlsVaultSS.hh"

using namespace ZuTestUtil;

static void signatures()
{
  ZuTestScope(signatures);
  using namespace Ztls_::SS;
  ZuCheck(ZfDBUS::signature<Empty>() == "");
  ZuCheck(ZfDBUS::signature<OpenSessionArg>() == "sv");
  ZuCheck(ZfDBUS::signature<OpenSessionRes>() == "vo");
  ZuCheck(ZfDBUS::signature<SearchArg>() == "a{ss}");
  ZuCheck(ZfDBUS::signature<SearchRes>() == "aoao");
  ZuCheck(ZfDBUS::signature<UnlockArg>() == "ao");
  ZuCheck(ZfDBUS::signature<UnlockRes>() == "aoo");
  ZuCheck(ZfDBUS::signature<ReadAliasArg>() == "s");
  ZuCheck(ZfDBUS::signature<ReadAliasRes>() == "o");
  ZuCheck(ZfDBUS::signature<Secret>() == "oayays");
  ZuCheck(ZfDBUS::signature<GetSecretArg>() == "o");
  ZuCheck(ZfDBUS::signature<GetSecretRes>() == "(oayays)");
  ZuCheck(ZfDBUS::signature<CreateCollectionArg>() == "a{sv}s");
  ZuCheck(ZfDBUS::signature<CreateCollectionRes>() == "oo");
  ZuCheck(ZfDBUS::signature<CreateItemArg>() == "a{sv}(oayays)b");
  ZuCheck(ZfDBUS::signature<CreateItemRes>() == "oo");
  ZuCheck(ZfDBUS::signature<PromptArg>() == "s");
  ZuCheck(ZfDBUS::signature<PromptCompleted>() == "bv");
  ZuCheck(ZfDBUS::signature<PromptPath>() == "o");
  ZuCheck(ZfDBUS::signature<PromptPaths>() == "ao");
}

static void bodies()
{
  ZuTestScope(bodies);
  using namespace Ztls_::SS;
  using Buffer = ZtBArray<ZtArrayHeapID<"Ztls.Vault.SS.Test">>;
  Buffer wire;

  OpenSessionArg open{"plain", Variant{Text{""}}};
  ZfDBUS::save(wire, open);
  auto openHandler = ZfDBUS::handler<OpenSessionArg>({wire, "sv"});
  ZuCheck(bool(openHandler));
  if (openHandler) {
    auto decoded = openHandler.ctor();
    ZuCheck(decoded.algorithm == "plain");
    ZuCheck(decoded.input.is<Text>());
  }

  Attrs attrs;
  attrs.add("service", "example");
  attrs.add("account", "user");
  attrs.add("key", "global/token");
  SearchArg search{{&attrs}};
  wire.length(0);
  ZfDBUS::save(wire, search);
  auto searchHandler = ZfDBUS::handler<SearchArg>({wire, "a{ss}"});
  ZuCheck(bool(searchHandler));
  if (searchHandler) {
    SearchArg decoded;
    searchHandler.load(decoded);
    ZuCheck(decoded.attributes.attrs->count_() == 3);
    ZuCheck(decoded.attributes.attrs->findVal("key") == "global/token");
  }

  uint8_t value[] = {0, 0xff, 1};
  CreateItemArg item;
  item.properties.add("org.freedesktop.Secret.Item.Label",
    Variant{Text{"token"}});
  item.properties.add("org.freedesktop.Secret.Item.Attributes",
    Variant{AttrsView{&attrs}});
  item.secret = Secret{Text{"/org/freedesktop/secrets/session/1"}, {}, value,
    Text{"application/octet-stream"}};
  item.replace = true;
  wire.length(0);
  ZfDBUS::save(wire, item);
  auto itemHandler = ZfDBUS::handler<CreateItemArg>({wire,
    "a{sv}(oayays)b"});
  ZuCheck(bool(itemHandler));
  if (itemHandler) {
    CreateItemArg decoded;
    itemHandler.load(decoded);
    ZuCheck(decoded.properties.count_() == 2);
    ZuCheck(decoded.secret.value == ZuBSpan{value});
    ZuCheck(decoded.replace);
  }
}

int main()
{
  ZuTestMain();
  ZuTestCall_("signatures", signatures);
  ZuTestCall_("bodies", bodies);
}
