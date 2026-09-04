//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuLib.hh>

#include <stdlib.h>
#include <string.h>

#include <iostream>

#include <openssl/evp.h>
#include <openssl/x509.h>

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZtArray.hh>

#include <zlib/ZiFile.hh>

#include <zlib/ZtlsPK.hh>

using namespace ZuTestUtil;

#define ZTLS_CHECK_RT(x, ...) ZuCheckRT(x, log_(__VA_ARGS__))

namespace {

void usage(ZuCSpan name)
{
  std::cerr <<
    "Usage: " << name << " [-q] [-s|--save]\n\n"
    "Options:\n"
    "  -q\tquiet output (default when test-harnessed)\n"
    "  -s\tsave generated keys and display them with openssl\n";
  ::exit(1);
}

ZuCSpan progName(char **argv)
{
  ZuCSpan name{argv[0]};
  for (int i = name.length(); --i >= 0; ) {
    auto c = name[i];
    if (c == '/'
#ifdef _WIN32
	|| c == '\\'
#endif
	) {
      name.offset(i + 1);
      break;
    }
  }
  return name;
}

static bool save = false;

void parseArgs(int argc, char **argv)
{
  verbose = !::getenv("HARNESS_ACTIVE");
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "-q")) {
      verbose = false;
      continue;
    }
    if (!strcmp(argv[i], "-s") || !strcmp(argv[i], "--save")) {
      save = true;
      continue;
    }
    usage(progName(argv));
  }
}

bool acceptCSR(void *, ZuBSpan) { return true; }
bool rejectCSR(void *, ZuBSpan) { return false; }

template <typename T>
void checkCSR(const ZmRef<T> &sk, ZuCSpan type)
{
  ZtBArray csr;
  auto r = sk->saveCSR(csr);
  if (r.template is<ZeException>()) {
    ZiLog::log(ZuMv(r.template p<ZeException>()));
    ZTLS_CHECK_RT(false, type, " CSR generation failed");
    return;
  }

  const unsigned char *ptr = csr.data();
  const unsigned char *end = ptr + csr.length();
  X509_REQ *req = d2i_X509_REQ(nullptr, &ptr, csr.length());
  ZTLS_CHECK_RT(req && ptr == end, type, " CSR DER parse");
  if (!req || ptr != end) {
    X509_REQ_free(req);
    return;
  }

  ZTLS_CHECK_RT(X509_NAME_entry_count(X509_REQ_get_subject_name(req)) == 0,
    type, " CSR empty subject");
  STACK_OF(X509_EXTENSION) *exts = X509_REQ_get_extensions(req);
  ZTLS_CHECK_RT(!exts || sk_X509_EXTENSION_num(exts) == 0,
    type, " CSR has no extensions");
  sk_X509_EXTENSION_pop_free(exts, X509_EXTENSION_free);

  ZtBArray pubDER;
  r = sk->savePK(pubDER);
  ZTLS_CHECK_RT(!r.template is<ZeException>(), type, " CSR public key save");
  if (r.template is<ZeException>()) {
    X509_REQ_free(req);
    return;
  }
  ptr = pubDER.data();
  end = ptr + pubDER.length();
  EVP_PKEY *source = d2i_PUBKEY(nullptr, &ptr, pubDER.length());
  EVP_PKEY *requested = X509_REQ_get_pubkey(req);
  ZTLS_CHECK_RT(source && ptr == end && requested &&
      EVP_PKEY_eq(source, requested) == 1,
    type, " CSR public key match");
  ZTLS_CHECK_RT(requested && X509_REQ_verify(req, requested) == 1,
    type, " CSR signature verification");
  EVP_PKEY_free(requested);
  EVP_PKEY_free(source);
  X509_REQ_free(req);
}

template <typename T>
void checkKey(Ztls::Random &rng, ZuCSpan type)
{
  using namespace Ztls::PK;

  ZmRef<T> sk;

  if constexpr (ZuIsSame<T, SK_RSA>{}) {
    sk = new T{rng, 2048};
  } else if constexpr (ZuIsSame<T, SK_EC>{}) {
    sk = new T{rng, OIDs::EC_GRP_SECP256R1};
  } else if constexpr (ZuIsSame<T, SK_ED25519>{}) {
    sk = new T{rng};
  }

  checkCSR(sk, type);
  ZTLS_CHECK_RT(!Ztls::Backend::pkey_csr(sk->key, nullptr, rejectCSR),
    type, " CSR sink failure ignored");

  ZtBArray asn;
  {
    auto r = sk->save(asn);
    if (r.template is<ZeException>()) {
      ZiLog::log(ZuMv(r.template p<ZeException>()));
      ZTLS_CHECK_RT(false, type, " SK save failed");
      return;
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
      ZTLS_CHECK_RT(false, type, " SK load failed");
      return;
    }
    sk2_ = ZuMv(r).template p<ZmRef<AnyKey>>();
  }
  bool skTypeOK = dynamic_cast<T *>(sk2_.ptr());
  ZTLS_CHECK_RT(skTypeOK, type, " wrong SK type loaded");
  if (!skTypeOK) return;
  ZmRef<T> sk2{ZuMv(sk2_)};
  ZtBArray asn2;
  {
    auto r = sk2->save(asn2);
    if (r.template is<ZeException>()) {
      ZiLog::log(ZuMv(r.template p<ZeException>()));
      ZTLS_CHECK_RT(false, type, " re-saved SK failed");
      return;
    }
  }
  ZTLS_CHECK_RT(asn == asn2, type, " SK ASN.1 DER match");
  {
    using PK = typename T::PK;
    auto r = sk2->mkPK();
    if (r.template is<ZeException>()) {
      ZiLog::log(ZuMv(r.template p<ZeException>()));
      ZTLS_CHECK_RT(false, type, " public key generation failed");
      return;
    }
    auto pk = ZuMv(r).template p<0>();
    ZTLS_CHECK_RT(!Ztls::Backend::pkey_csr(pk->key, nullptr, acceptCSR),
      type, " public-only CSR generation succeeded");
    ZtBArray asn3;
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
	ZTLS_CHECK_RT(false, type, " PK load failed");
	return;
      }
      pk2_ = ZuMv(r).template p<ZmRef<AnyKey>>();
    }
    bool pkTypeOK = dynamic_cast<PK *>(pk2_.ptr());
    ZTLS_CHECK_RT(pkTypeOK, type, " wrong PK type loaded");
    if (!pkTypeOK) return;
    ZmRef<PK> pk2{ZuMv(pk2_)};
    ZtBArray asn4;
    {
      auto r = pk2->save(asn4);
      if (r.template is<ZeException>()) {
	ZiLog::log(ZuMv(r.template p<ZeException>()));
	ZTLS_CHECK_RT(false, type, " re-saved PK failed");
	return;
      }
    }
    ZTLS_CHECK_RT(asn3 == asn4, type, " PK ASN.1 DER match");

    {
      auto data = ZuBSpan{"foobarbaz"};
      uint8_t digest[Ztls::MD<>::Size];
      { Ztls::MD<> md; md.update(data); md.finish(digest); }
      bool verified = false;
      bool verifyReturned = false;
      auto r = sk->sign(rng, digest,
	  [&type, &pk, &digest, &verified, &verifyReturned](
	    ZuBSpan signature) {
	verifyReturned = true;
	auto r = pk->verify(digest, signature);
	if (r.template is<ZeException>()) {
	  ZiLog::log(ZuMv(r.template p<ZeException>()));
	  ZTLS_CHECK_RT(false, type, " signature verification failed");
	  return;
	}
	verified = r.template p<0>();
	ZTLS_CHECK_RT(verified, type, " signature verification");
      });
      if (r.template is<ZeException>()) {
	ZiLog::log(ZuMv(r.template p<ZeException>()));
	ZTLS_CHECK_RT(false, type, " signing failed");
	return;
      }
      ZTLS_CHECK_RT(verifyReturned, type, " signature callback");
    }
  }
}

void testRSA(Ztls::Random &rng)
{
  ZuTestScopeRT(rsa);
  checkKey<Ztls::PK::SK_RSA>(rng, "rsa");
}

void testEC(Ztls::Random &rng)
{
  ZuTestScopeRT(ec);
  checkKey<Ztls::PK::SK_EC>(rng, "ec");
}

void testED25519(Ztls::Random &rng)
{
  ZuTestScopeRT(ed25519);
  checkKey<Ztls::PK::SK_ED25519>(rng, "ed25519");
}

} // namespace

int main(int argc, char **argv)
{
  parseArgs(argc, argv);

  ZiLog::init("ZtlsPKTest");
  ZiLog::level(0);
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();

  Ztls::Random rng;
  rng.init();

  ZuTestMain();
  ZuTestCall_("rsa", testRSA, rng);
  ZuTestCall_("ec", testEC, rng);
  ZuTestCall_("ed25519", testED25519, rng);

  ZiLog::stop();
}
