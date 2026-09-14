//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/zumd_secret.hh>

using namespace ZuTestUtil;

static void secrets()
{
  ZuTestScope(secrets);
  Ztls::Random rng;
  ZuCheck(rng.init());
  Zum::Bytes oldKey, newKey, wrongKey;
  oldKey.length(32, false);
  newKey.length(32, false);
  wrongKey.length(32, false);
  ZuCheck(rng.random(oldKey) && rng.random(newKey) && rng.random(wrongKey));
  auto check = Zum::serverKeyCheck(oldKey);
  ZuCheck(check.length() == 32 && check == Zum::serverKeyCheck(oldKey));
  ZuCheck(check != Zum::serverKeyCheck(newKey));
  ZuCheck(!Zum::serverKeyCheck({}));
  ZuCheck(!Zum::serverKeyCheck(ZuBSpan{"short"}));
  Zum::Bytes envelope, plain;
  ZuCheck(Zum::serverSecretEncrypt(rng, oldKey, "issuer", "zum.provider", "7",
    "clientSecret", ZuBSpan{"test-secret"}, envelope));
  auto original = envelope;
  ZuCheck(Zum::serverSecretDecrypt(oldKey, "issuer", "zum.provider", "7",
    "clientSecret", envelope, plain) && plain == ZuBSpan{"test-secret"});
  ZuClear(plain.data(), plain.length());
  plain.null();
  Zum::Bytes second;
  ZuCheck(Zum::serverSecretEncrypt(rng, oldKey, "issuer", "zum.provider", "7",
    "clientSecret", ZuBSpan{"test-secret"}, second) && second != envelope);
  ZuCheck(!Zum::serverSecretDecrypt(wrongKey, "issuer", "zum.provider", "7",
    "clientSecret", envelope, plain) && !plain);
  ZuCheck(!Zum::serverSecretRekey(rng, wrongKey, newKey, "issuer", "zum.provider",
    "7", "clientSecret", envelope) && envelope == original);
  ZuCheck(!Zum::serverSecretRekey(rng, oldKey, newKey, "other", "zum.provider",
    "7", "clientSecret", envelope) && envelope == original);
  ZuCheck(!Zum::serverSecretRekey(rng, oldKey, newKey, "issuer", "zum.sign_key",
    "7", "clientSecret", envelope) && envelope == original);
  ZuCheck(!Zum::serverSecretRekey(rng, oldKey, newKey, "issuer", "zum.provider",
    "8", "clientSecret", envelope) && envelope == original);
  ZuCheck(!Zum::serverSecretRekey(rng, oldKey, newKey, "issuer", "zum.provider",
    "7", "other", envelope) && envelope == original);
  auto tampered = envelope;
  tampered[tampered.length() - 1] ^= 1;
  auto before = tampered;
  ZuCheck(!Zum::serverSecretRekey(rng, oldKey, newKey, "issuer", "zum.provider",
    "7", "clientSecret", tampered) && tampered == before);
  ZuCheck(Zum::serverSecretRekey(rng, oldKey, newKey, "issuer", "zum.provider",
    "7", "clientSecret", envelope) && envelope != original);
  ZuCheck(!Zum::serverSecretDecrypt(oldKey, "issuer", "zum.provider", "7",
    "clientSecret", envelope, plain) && !plain);
  ZuCheck(Zum::serverSecretDecrypt(newKey, "issuer", "zum.provider", "7",
    "clientSecret", envelope, plain) && plain == ZuBSpan{"test-secret"});
  ZuClear(plain.data(), plain.length());
  plain.null();
  auto rotated = envelope;
  ZuCheck(Zum::serverSecretRekey(rng, oldKey, newKey, "issuer", "zum.provider",
    "7", "clientSecret", envelope) && envelope == rotated);
  ZuCheck(Zum::serverSecretEncrypt(rng, oldKey, "issuer", "zum.provider", "7",
    "clientSecret", {}, envelope));
  ZuCheck(Zum::serverSecretDecrypt(oldKey, "issuer", "zum.provider", "7",
    "clientSecret", envelope, plain) && !plain);
  ZuCheck(Zum::serverSecretRekey(rng, oldKey, newKey, "issuer", "zum.provider",
    "7", "clientSecret", envelope));
  ZuCheck(Zum::serverSecretDecrypt(newKey, "issuer", "zum.provider", "7",
    "clientSecret", envelope, plain) && !plain);
  ZuClear(oldKey.data(), oldKey.length());
  ZuClear(newKey.data(), newKey.length());
  ZuClear(wrongKey.data(), wrongKey.length());
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(secrets);
  return 0;
}
