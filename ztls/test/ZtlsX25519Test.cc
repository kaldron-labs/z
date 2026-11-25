//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZtArray.hh>

#include <zlib/ZtlsPK.hh>

#include "ZtlsTestKeys.hh"

using namespace ZuTestUtil;
using namespace Ztls::PK;

template <typename T>
static bool checkLoaded(ZuUnion<ZmRef<AnyKey>, ZeException> &loaded, ZuBSpan expected)
{
  if (loaded.template is<ZeException>()) return false;
  auto key = dynamic_cast<T *>(loaded.template p<0>().ptr());
  if (!key) return false;
  uint8_t raw[Ztls::X25519KeySize];
  return !key->exportPK(raw).template is<ZeException>() &&
    !memcmp(raw, expected.data(), sizeof(raw));
}

static void vectors()
{
  ZuTestScope(vectors);
  uint8_t alice[32], bob[32], alicePub[32], bobPub[32], shared[32];
  ZuCheck(ZtlsTestKeys::read("alice", alice), return);
  ZuCheck(ZtlsTestKeys::read("bob", bob), return);
  ZuCheck(ZtlsTestKeys::read("alice.pub", alicePub), return);
  ZuCheck(ZtlsTestKeys::read("bob.pub", bobPub), return);
  ZuCheck(ZtlsTestKeys::read("shared", shared), return);
  X25519Private a{alice}, b{bob};
  uint8_t	out[32], seed[32];
  ZuCheck(!a.exportPK(out).template is<ZeException>());
  ZuCheck(!memcmp(out, alicePub, sizeof(out)));
  ZuCheck(!b.exportPK(out).template is<ZeException>());
  ZuCheck(!memcmp(out, bobPub, sizeof(out)));
  ZuCheck(!a.exportSK(seed).template is<ZeException>());
  ZuCheck(!memcmp(seed, alice, sizeof(seed)));
  ZuClear(seed, sizeof(seed));
  ZuCheck(!a.agree(bobPub, out).template is<ZeException>());
  ZuCheck(!memcmp(out, shared, sizeof(out)));
  ZuCheck(!b.agree(alicePub, out).template is<ZeException>());
  ZuCheck(!memcmp(out, shared, sizeof(out)));
  auto pk = a.mkPK();
  ZuCheck(!pk.template is<ZeException>());
  if (!pk.template is<ZeException>()) {
    ZuCheck(!pk.template p<0>()->exportPK(out).template is<ZeException>());
    ZuCheck(!memcmp(out, alicePub, sizeof(out)));
  }
  X25519Public imported{alicePub};
  ZuCheck(!imported.exportPK(out).template is<ZeException>());
  ZuCheck(!memcmp(out, alicePub, sizeof(out)));
  // Failed replacement must not discard the existing native key.
  ZuCheck(!x25519ImportPrivate(a.key, {alice, 31}));
  ZuCheck(!x25519ImportPublic(a.key, {alicePub, 31}));
  ZuCheck(!loadPEM(a.key, nullptr));
  ZuCheck(!a.exportPK(out).template is<ZeException>());
  ZuCheck(!memcmp(out, alicePub, sizeof(out)));
  ZuCheck(!a.exportSK(seed).template is<ZeException>());
  ZuCheck(!memcmp(seed, alice, sizeof(seed)));
  ZuClear(seed, sizeof(seed));
  a.key.reset(a.key.get());
  ZuCheck(!a.agree(bobPub, out).template is<ZeException>());
  ZuCheck(!memcmp(out, shared, sizeof(out)));
  Key empty;
  ZuCheck(!check(empty, false));
  empty.reset(nullptr);
  ZuCheck(a.agree({bobPub, 31}, out).template is<ZeException>());
  ZuCheck(a.agree(bobPub, {out, 31}).template is<ZeException>());
  uint8_t zero[32]{};
  memset(out, 0xa5, sizeof(out));
  ZuCheck(a.agree(zero, out).template is<ZeException>());
  bool cleared = true;
  for (auto c : out) cleared &= c == 0;
  ZuCheck(cleared);
  ZuCheck(a.exportSK({seed, 31}).template is<ZeException>());
  ZuCheck(imported.exportPK({out, 31}).template is<ZeException>());
  X25519Private failed{alice};
  ZuCheck(Ztls::PK::x25519ImportPublic(failed.key, alicePub));
  memset(seed, 0xa5, sizeof(seed));
  ZuCheck(failed.exportSK(seed).template is<ZeException>());
  cleared = true;
  for (auto c : seed) cleared &= c == 0;
  ZuCheck(cleared);
  ZtBArray failedDER;
  ZuCheck(failed.save(failedDER).template is<ZeException>());
  bool rejected = false;
  try {
    X25519Private invalid{
      {alice, 31}
	 };
  } catch (const ZeException &) {
    rejected = true;
  }
  ZuCheck(rejected);
  rejected = false;
  try {
    X25519Public invalid{
      {alicePub, 31}
    };
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
  X25519Private sk{rng};
  uint8_t	pub[32];
  ZuCheck(!sk.exportPK(pub).template is<ZeException>());
  X25519Public pk{pub};
  ZtBArray     skDER, pkDER;
  ZuCheck(!sk.save(skDER).template is<ZeException>());
  ZuCheck(!pk.save(pkDER).template is<ZeException>());
  auto loadedSK = LoadSK{rng}.load(skDER);
  auto loadedPK = LoadPK{}.load(pkDER);
  ZuCheck(checkLoaded<X25519Private>(loadedSK, pub));
  ZuCheck(checkLoaded<X25519Public>(loadedPK, pub));
  ZtArray<char> skPEM, pkPEM;
  ZuCheck(!savePEM(skPEM, &sk).template is<ZeException>());
  ZuCheck(!savePEM(pkPEM, &pk).template is<ZeException>());
  loadedSK = LoadSK{rng}.loadPEM(skPEM);
  loadedPK = LoadPK{}.loadPEM(pkPEM);
  ZuCheck(checkLoaded<X25519Private>(loadedSK, pub));
  ZuCheck(checkLoaded<X25519Public>(loadedPK, pub));
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
    X25519Private sk{rng};
    auto	  pk = sk.mkPK();
    ZuCheckRT(!pk.template is<ZeException>());
    if (pk.template is<ZeException>()) return;
    ZuCheckRT(!saveFile(argv[2], &sk).template is<ZeException>());
    ZuCheckRT(!saveFile(argv[3], pk.template p<0>().ptr()).template is<ZeException>());
    return;
  }
  ZuCheckRT(!strcmp(argv[1], "--load"));
  if (strcmp(argv[1], "--load")) return;
  auto sk = LoadSK{rng}.loadFile(argv[2]);
  auto pk = LoadPK{}.loadFile(argv[3]);
  ZuCheckRT(!sk.template is<ZeException>());
  ZuCheckRT(!pk.template is<ZeException>());
  if (sk.template is<ZeException>() || pk.template is<ZeException>()) return;
  auto privateKey = dynamic_cast<X25519Private *>(sk.template p<0>().ptr());
  auto publicKey = dynamic_cast<X25519Public *>(pk.template p<0>().ptr());
  ZuCheckRT(privateKey && publicKey);
  if (!privateKey || !publicKey) return;
  uint8_t pub[32], ownPub[32], shared[32];
  ZuCheckRT(!publicKey->exportPK(pub).template is<ZeException>());
  ZuCheckRT(!privateKey->exportPK(ownPub).template is<ZeException>());
  ZuCheckRT(!memcmp(ownPub, pub, sizeof(pub)));
  ZuCheckRT(!privateKey->agree(pub, shared).template is<ZeException>());
}

int main(int argc, char **argv)
{
  ZuTestMain();
  ZuCheck(ZtlsTestKeys::init(), return 1);
  ZuTestCall_("vectors", vectors);
  ZuTestCall_("encoding", encoding);
  ZuTestCall_("interop", interop, argc, argv);
}
