//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Run in the fixture generator's private staging directory.
#include <zlib/ZtlsAge.hh>

static bool writeFile(const char *name, ZuBSpan data)
{
  ZiFile file{name, ZiFile::Write | ZiFile::Exclusive | ZiFile::GC, 0600};
  return file && file.write(data.data(), data.length()) == Zi::OK;
}

int main()
{
  Ztls::Random rng;
  if (!rng.init()) return 1;
  uint8_t seed[32];
  char	  text[128];
  ZuGuard clear{[&]()
  {
    ZuClear(seed, sizeof(seed));
    ZuClear(text, sizeof(text));
  }};
  for (const char *name : {"alice", "bob", "hybrid"}) {
    if (!rng.random(seed) || !writeFile(name, seed)) return 1;
    bool hybrid = !strcmp(name, "hybrid");
    auto encoded = ZtlsAge::encodeIdentity(
      hybrid ? ZtlsAgeKeyType::Hybrid : ZtlsAgeKeyType::X25519, seed, {text, sizeof(text)});
    if (encoded.is<ZeException>()) return 1;
    Zi::Path path;
    path << name << ".identity";
    if (!writeFile(path, {text, encoded.p<size_t>()})) return 1;
  }

  Ztls::PK::RSAPrivate	   rsa{rng, 2048};
  Ztls::PK::Ed25519Private ed{rng};
  return Ztls::PK::saveFile("rsa.pem", &rsa).is<ZeException>() ||
    Ztls::PK::saveFile("ed.pem", &ed).is<ZeException>();
}
