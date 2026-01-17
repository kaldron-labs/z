//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuLib.hh>

#include <stdlib.h>

#include <iostream>

#include <zlib/ZtArray.hh>

#include <zlib/ZiFile.hh>

#include <zlib/ZtlsPK.hh>

void usage()
{
  std::cerr << "Usage: ZtlsPKTest [-s|--save]\n" << std::flush;
  ::exit(1);
}

void gtfo()
{
  ZiLog::stop();
  exit(1);
}

inline void out(bool ok, ZuCSpan t, ZuCSpan s, ZuCSpan e) {
  std::cout
    << (ok ? "OK  " : "NOK ")
    << t << ' ' << s << ' ' << e << '\n' << std::flush;
  if (!ok) gtfo();
}

#define CHECK(t, s, x) (out((x), t, s, #x))

static bool save = false;

template <typename T>
void check(Ztls::Random &rng, ZuCSpan type)
{
  using namespace Ztls::PK;

  ZmRef<T> sk;

  if constexpr (ZuIsSame<T, SK_RSA>{}) {
    sk = new T{rng, 2048};
  } else if constexpr (ZuIsSame<T, SK_EC>{}) {
    sk = new T{rng, OID::ec_grp_secp256r1()};
  } else if constexpr (ZuIsSame<T, SK_ED25519>{}) {
    sk = new T{rng};
  }

  ZtBytes asn;
  {
    auto r = sk->save(asn);
    if (r.template is<ZeException>()) {
      ZiLog::log(ZuMv(r.template p<ZeException>()));
      gtfo();
    }
  }
  if (save) {
    ZiFile file;
    if (file.open(ZtString<>{} << type << ".der", ZiFile::Write) == Zi::OK) {
      file.write(&asn[0], asn.length());
      system(ZtString<>{}
	<< "openssl pkey -inform DER -in " << type << ".der -text -noout");
    }
  }
  ZmRef<AnyKey> sk2_;
  LoadSK loader{rng};
  {
    auto r = loader.load(asn);
    if (r.template is<ZeException>()) {
      ZiLog::log(ZuMv(r.template p<ZeException>()));
      gtfo();
    }
    sk2_ = ZuMv(r).template p<ZmRef<AnyKey>>();
  }
  if (!dynamic_cast<T *>(sk2_.ptr())) {
    ZiLOG(Error, "ZtlsPKTest", "wrong key type loaded");
    gtfo();
  }
  ZmRef<T> sk2{ZuMv(sk2_)};
  ZtBytes asn2;
  {
    auto r = sk2->save(asn2);
    if (r.template is<ZeException>()) {
      ZiLog::log(ZuMv(r.template p<ZeException>()));
      gtfo();
    }
  }
  CHECK(type, "SK ASN.1 DER match", asn == asn2);
  {
    using PK = typename T::PK;
    auto r = sk2->mkPK();
    if (r.template is<ZeException>()) {
      ZiLog::log(ZuMv(r.template p<ZeException>()));
      gtfo();
    }
    auto pk = ZuMv(r).template p<0>();
    ZtBytes asn3;
    pk->save(asn3);
    if (save) {
      ZiFile file;
      if (file.open(
	  ZtString<>{} << type << "_public.der", ZiFile::Write) == Zi::OK) {
	file.write(&asn3[0], asn3.length());
	system(ZtString<>{}
	  << "openssl pkey -pubin -inform DER -in "
	  << type << "_public.der -text -noout");
      }
    }

    LoadPK loader;
    ZmRef<AnyKey> pk2_;
    {
      auto r = loader.load(asn3);
      if (r.template is<ZeException>()) {
	ZiLog::log(ZuMv(r.template p<ZeException>()));
	gtfo();
      }
      pk2_ = ZuMv(r).template p<ZmRef<AnyKey>>();
    }
    if (!dynamic_cast<PK *>(pk2_.ptr())) {
      ZiLOG(Error, "ZtlsPKTest", "wrong key type loaded");
      gtfo();
    }
    ZmRef<PK> pk2{ZuMv(pk2_)};
    ZtBytes asn4;
    {
      auto r = pk2->save(asn4);
      if (r.template is<ZeException>()) {
	ZiLog::log(ZuMv(r.template p<ZeException>()));
	gtfo();
      }
    }
    CHECK(type, "PK ASN.1 DER match", asn3 == asn4);

    {
      auto data = ZuBSpan{"foobarbaz"};
      uint8_t digest[Ztls::MD<>::Size];
      { Ztls::MD<> md; md.update(data); md.finish(digest); }
      auto r = sk->sign(rng, digest, [&type, &pk, &digest](ZuBSpan signature) {
	auto r = pk->verify(digest, signature);
	if (r.template is<ZeException>()) {
	  ZiLog::log(ZuMv(r.template p<ZeException>()));
	  gtfo();
	}
	CHECK(type, "signature verification", r.template p<0>());
      });
      if (r.template is<ZeException>()) {
	ZiLog::log(ZuMv(r.template p<ZeException>()));
	gtfo();
      }
    }
  }
}

int main(int argc, char **argv)
{
  if (argc > 2) usage();
  if (argc == 2) {
    ZuCSpan arg(argv[1]);
    if (arg != "-s" && arg != "--save") usage();
    save = true;
  }

  ZiLog::init("ZtlsPKTest");
  ZiLog::level(0);
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();

  Ztls::Random rng;
  rng.init();

  using namespace Ztls::PK;

  check<SK_RSA>(rng, "rsa");
  check<SK_EC>(rng, "ec");
  check<SK_ED25519>(rng, "ed25519");

  ZiLog::stop();

  return 0;
}
