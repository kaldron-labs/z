//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>

#include <openssl/core_names.h>
#include <openssl/evp.h>
#include <openssl/x509.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZtArray.hh>
#include <zlib/ZtlsKEM.hh>

using namespace ZuTestUtil;
using namespace Ztls::PK;

static void kem()
{
  ZuTestScope(kem);
  Ztls::Random rng;
  ZuCheck(rng.init());
  SK_MLKEM768 sk{rng};

  uint8_t pub[MLKEM768PublicSize];
  ZuCheck(!sk.exportPK(pub).template is<ZeException>());
  PK_MLKEM768 pk{pub};
  auto publicResult = sk.mkPK();
  ZuCheck(!publicResult.template is<ZeException>());
  if (publicResult.template is<ZeException>()) return;
  auto derived = ZuMv(publicResult).template p<0>();

  uint8_t ct[MLKEM768CiphertextSize];
  uint8_t secret[MLKEM768SecretSize];
  uint8_t recovered[MLKEM768SecretSize];
  ZuCheck(!pk.encapsulate(rng, ct, secret).template is<ZeException>());
  ZuCheck(!sk.decapsulate(ct, recovered).template is<ZeException>());
  ZuCheck(!memcmp(secret, recovered, sizeof(secret)));
  ZuCheck(!derived->encapsulate(rng, ct, secret).template is<ZeException>());
  ZuCheck(!sk.decapsulate(ct, recovered).template is<ZeException>());
  ZuCheck(!memcmp(secret, recovered, sizeof(secret)));
  uint8_t ct2[MLKEM768CiphertextSize];
  uint8_t secret2[MLKEM768SecretSize];
  ZuCheck(!pk.encapsulate(rng, ct2, secret2).template is<ZeException>());
  ZuCheck(memcmp(ct, ct2, sizeof(ct)) != 0);
  ZuCheck(memcmp(secret, secret2, sizeof(secret)) != 0);
  ct[0] ^= 1;
  auto altered = sk.decapsulate(ct, recovered);
  ZuCheck(altered.template is<ZeException>() ||
    memcmp(secret, recovered, sizeof(secret)) != 0);

  ZuCheck(pk.encapsulate(rng, {ct, sizeof(ct) - 1}, secret)
    .template is<ZeException>());
  ZuCheck(sk.decapsulate({ct, sizeof(ct) - 1}, recovered)
    .template is<ZeException>());
  ZuCheck(pk.exportPK({pub, sizeof(pub) - 1}).template is<ZeException>());
  ZuCheck(pk.encapsulate(rng, ct, {secret, sizeof(secret) - 1})
    .template is<ZeException>());
  ZuCheck(sk.decapsulate(ct2, {recovered, sizeof(recovered) - 1})
    .template is<ZeException>());
  bool rejected = false;
  try {
    PK_MLKEM768 invalid{{pub, sizeof(pub) - 1}};
  } catch (const ZeException &) {
    rejected = true;
  }
  ZuCheck(rejected);
}

static void encoding()
{
  ZuTestScope(encoding);
  Ztls::Random rng;
  ZuCheck(rng.init());
  SK_MLKEM768 sk{rng};
  ZtBArray privateDER, publicDER;
  ZuCheck(!sk.save(privateDER).template is<ZeException>());
  ZuCheck(!sk.savePK(publicDER).template is<ZeException>());
  ZuCheck(privateDER.length() < DERBufSize);
  ZuCheck(publicDER.length() < DERBufSize);

  const unsigned char *ptr = privateDER.data();
  auto osslSK = d2i_AutoPrivateKey(nullptr, &ptr, privateDER.length());
  ZuCheck(osslSK && ptr == privateDER.data() + privateDER.length());
  if (!osslSK) return;
  ptr = publicDER.data();
  auto osslPK = d2i_PUBKEY(nullptr, &ptr, publicDER.length());
  ZuCheck(osslPK && ptr == publicDER.data() + publicDER.length());
  EVP_PKEY_free(osslPK);

  auto loaded = LoadSK{rng}.load(privateDER);
  ZuCheck(!loaded.template is<ZeException>());
  if (!loaded.template is<ZeException>())
    ZuCheck(dynamic_cast<SK_MLKEM768 *>(loaded.template p<0>().ptr()));
  auto loadedPK = LoadPK{}.load(publicDER);
  ZuCheck(!loadedPK.template is<ZeException>());
  if (!loadedPK.template is<ZeException>())
    ZuCheck(dynamic_cast<PK_MLKEM768 *>(loadedPK.template p<0>().ptr()));

  uint8_t seed[MLKEM768SeedSize];
  size_t n = 0;
  ZuCheck(EVP_PKEY_get_octet_string_param(osslSK,
    OSSL_PKEY_PARAM_ML_KEM_SEED, seed, sizeof(seed), &n) == 1 &&
    n == sizeof(seed));
  bool rejected = false;
  try {
    SK_MLKEM768 invalid{ZuBSpan{seed, sizeof(seed) - 1}};
  } catch (const ZeException &) {
    rejected = true;
  }
  ZuCheck(rejected);
  SK_MLKEM768 seeded{ZuBSpan{seed, sizeof(seed)}};
  ZtBArray seededDER;
  ZuCheck(!seeded.save(seededDER).template is<ZeException>());
  ZuCheck(seededDER == privateDER);

  uint8_t expanded[MLKEM768PrivateSize];
  n = 0;
  ZuCheck(EVP_PKEY_get_octet_string_param(osslSK,
    OSSL_PKEY_PARAM_PRIV_KEY, expanded, sizeof(expanded), &n) == 1 &&
    n == sizeof(expanded));
  rejected = false;
  try {
    SK_MLKEM768 invalid{ZuBSpan{expanded, sizeof(expanded) - 1}};
  } catch (const ZeException &) {
    rejected = true;
  }
  ZuCheck(rejected);
  Data::SK_PKCS8_MLKEM_EXPANDED expandedData{
    .version = 0, .id = OIDs::MLKEM768, .key = expanded
  };
  ZtBArray expandedDER;
  ZfASN1::save(expandedDER, expandedData);
  loaded = LoadSK{rng}.load(expandedDER);
  ZuCheck(!loaded.template is<ZeException>());
  if (!loaded.template is<ZeException>()) {
    auto expandedSK = dynamic_cast<SK_MLKEM768 *>(loaded.template p<0>().ptr());
    ZuCheck(expandedSK);
    if (expandedSK) {
      ZtBArray saved;
      ZuCheck(!expandedSK->save(saved).template is<ZeException>());
      ZuCheck(saved == expandedDER);
      ZtArray<char> expandedPEM;
      ZuCheck(!savePEM(expandedPEM, expandedSK)
        .template is<ZeException>());
      ZuCheck(expandedPEM.length() < BufSize);
      auto expandedLoaded = LoadSK{rng}.loadPEM(expandedPEM);
      ZuCheck(!expandedLoaded.template is<ZeException>());
    }
  }
  ptr = expandedDER.data();
  auto osslExpanded = d2i_AutoPrivateKey(
    nullptr, &ptr, expandedDER.length());
  ZuCheck(osslExpanded && ptr == expandedDER.data() + expandedDER.length());
  EVP_PKEY_free(osslExpanded);
  Data::SK_PKCS8_MLKEM_BOTH bothData{
    .version = 0, .id = OIDs::MLKEM768,
    .both = {.seed = seed, .key = expanded}
  };
  ZtBArray bothDER;
  ZfASN1::save(bothDER, bothData);
  ptr = bothDER.data();
  auto osslBoth = d2i_AutoPrivateKey(nullptr, &ptr, bothDER.length());
  ZuCheck(osslBoth && ptr == bothDER.data() + bothDER.length());
  EVP_PKEY_free(osslBoth);
  loaded = LoadSK{rng}.load(bothDER);
  ZuCheck(!loaded.template is<ZeException>());
  if (!loaded.template is<ZeException>()) {
    auto bothSK = dynamic_cast<SK_MLKEM768 *>(loaded.template p<0>().ptr());
    ZuCheck(bothSK);
    if (bothSK) {
      uint8_t bothPub[MLKEM768PublicSize];
      ZuCheck(!bothSK->exportPK(bothPub).template is<ZeException>());
      uint8_t originalPub[MLKEM768PublicSize];
      ZuCheck(!sk.exportPK(originalPub).template is<ZeException>());
      ZuCheck(!memcmp(bothPub, originalPub, sizeof(bothPub)));
    }
  }

  ZtArray<char> privatePEM, publicPEM;
  ZuCheck(!savePEM(privatePEM, &sk).template is<ZeException>());
  uint8_t pub[MLKEM768PublicSize];
  ZuCheck(!sk.exportPK(pub).template is<ZeException>());
  PK_MLKEM768 pk{ZuBSpan{pub, sizeof(pub)}};
  ZuCheck(!savePEM(publicPEM, &pk).template is<ZeException>());
  ZuCheck(privatePEM.length() < BufSize);
  loaded = LoadSK{rng}.loadPEM(privatePEM);
  ZuCheck(!loaded.template is<ZeException>());
  loadedPK = LoadPK{}.loadPEM(publicPEM);
  ZuCheck(!loadedPK.template is<ZeException>());

  EVP_PKEY_free(osslSK);
  ZuClear(seed, sizeof(seed));
  ZuClear(expanded, sizeof(expanded));
}

static void interop(int argc, char **argv)
{
  ZuTestScopeRT(interop);
  if (argc == 1) {
    ZuCheckRT(true);
    return;
  }
  ZuCheckRT(argc == 4);
  if (argc != 4) return;
  Ztls::Random rng;
  ZuCheckRT(rng.init());
  if (!strcmp(argv[1], "--save")) {
    SK_MLKEM768 sk{rng};
    auto pk = sk.mkPK();
    ZuCheckRT(!pk.template is<ZeException>());
    if (pk.template is<ZeException>()) return;
    ZuCheckRT(!saveFile(argv[2], &sk).template is<ZeException>());
    ZuCheckRT(!saveFile(argv[3], pk.template p<0>().ptr())
      .template is<ZeException>());
    return;
  }
  ZuCheckRT(!strcmp(argv[1], "--load"));
  if (strcmp(argv[1], "--load")) return;
  auto sk = LoadSK{rng}.loadFile(argv[2]);
  auto pk = LoadPK{}.loadFile(argv[3]);
  ZuCheckRT(!sk.template is<ZeException>());
  ZuCheckRT(!pk.template is<ZeException>());
  if (sk.template is<ZeException>() || pk.template is<ZeException>()) return;
  auto privateKey = dynamic_cast<SK_MLKEM768 *>(sk.template p<0>().ptr());
  auto publicKey = dynamic_cast<PK_MLKEM768 *>(pk.template p<0>().ptr());
  ZuCheckRT(privateKey && publicKey);
  if (!privateKey || !publicKey) return;
  uint8_t ct[MLKEM768CiphertextSize];
  uint8_t secret[MLKEM768SecretSize];
  uint8_t recovered[MLKEM768SecretSize];
  ZuCheckRT(!publicKey->encapsulate(rng, ct, secret)
    .template is<ZeException>());
  ZuCheckRT(!privateKey->decapsulate(ct, recovered)
    .template is<ZeException>());
  ZuCheckRT(!memcmp(secret, recovered, sizeof(secret)));
}

int main(int argc, char **argv)
{
  ZuTestMain();
  ZuTestCall_("kem", kem);
  ZuTestCall_("encoding", encoding);
  ZuTestCall_("interop", interop, argc, argv);
}
