//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// key file formats
// - public and private key file formats
// - ASN.1 DER data structures
// - PKCS#1, SEC1, PKCS#8 and X.509

#ifndef ZtlsPKData_HH
#define ZtlsPKData_HH

#ifndef ZtlsLib_HH
#include <zlib/ZtlsLib.hh>
#endif

#include <zlib/ZuSpan.hh>

#include <zlib/ZfASN1.hh>
#include <zlib/ZfStruct.hh>

namespace Ztls::PK {

enum { BufSize = 4 << 10 };    // a 4096bit RSA key pem is under 4k
enum { DERBufSize = 3 << 10 }; // BufSize reduced by 3/4 for base64

using namespace ZfASN1::Encoding;

namespace OIDs {
  constexpr char PKCS1RSA[] = "\x2a\x86\x48\x86\xf7\x0d\x01\x01\x01";
  constexpr char ECUnrestricted[] = "\x2a\x86\x48\xce\x3d\x02\x01";
  constexpr char ED25519[] = "\x2b\x65\x70";
  constexpr char X25519[] = "\x2b\x65\x6e";
  constexpr char MLKEM768[] = "\x60\x86\x48\x01\x65\x03\x04\x04\x02";
  constexpr char ECGroupSecp256r1[] = "\x2a\x86\x48\xce\x3d\x03\x01\x07";
}

namespace Data {

// RSA PKCS#1 private key
struct PKCS1Private {
  uint8_t	version;
  ZuBSpan	modulus;
  ZuBSpan	pubExp;
  ZuBSpan	prvExp;
  ZuBSpan	prime1;
  ZuBSpan	prime2;
  ZuBSpan	exp1;
  ZuBSpan	exp2;
  ZuBSpan	coeff;
};
ZfStruct(
  Ztls_API, (PKCS1Private, ASN1), (version, (Ctor<0>), UInt8),
  (modulus, (Ctor<1>, ASN1::Type<Integer>), Bytes),
  (pubExp, (Ctor<2>, ASN1::Type<Integer>), Bytes),
  (prvExp, (Ctor<3>, ASN1::Type<Integer>), Bytes),
  (prime1, (Ctor<4>, ASN1::Type<Integer>), Bytes),
  (prime2, (Ctor<5>, ASN1::Type<Integer>), Bytes), (exp1, (Ctor<6>, ASN1::Type<Integer>), Bytes),
  (exp2, (Ctor<7>, ASN1::Type<Integer>), Bytes), (coeff, (Ctor<8>, ASN1::Type<Integer>), Bytes));

// EC SEC1 private key
struct SEC1Private {
  uint8_t	version;
  ZuBSpan	key;
  ZuBSpan	id;	  // e.g. OIDs::ECGroupSecp256r1
  ZuBSpan	pubKey; // optional
};
ZfStruct(
  Ztls_API, (SEC1Private, ASN1), (version, (Ctor<0>), UInt8), (key, (Ctor<1>), Bytes),
  (id, (Ctor<2>, (ASN1::Fmt<2, tag(0, OID)>), ASN1::Opt), Bytes),
  (pubKey, (Ctor<3>, (ASN1::Fmt<3, tag(1)>), ASN1::Type<BitString>, ASN1::Opt), Bytes));

// PKCS#8 private key - header
struct PKCS8PrivateHdr {
  uint8_t	version; // 0
  ZuBSpan	id;	   // e.g. OIDs::ECUnrestricted for EC
		   //      OIDs::PKCS1RSA for RSA
		   //      1.3.101.112 for ED25519
};
ZfStruct(
  Ztls_API, (PKCS8PrivateHdr, ASN1), (version, (Ctor<0>), UInt8),
  (id, (Ctor<1>, (ASN1::Fmt<1, tagU(), seq(0), tagU(OID)>)), Bytes));

// PKCS#8 private key - RSA version (PKCS#1 payload)
struct PKCS8PrivateRSA {
  uint8_t	version;
  ZuBSpan	id;
  PKCS1Private	rsa;
};
ZfStruct(
  Ztls_API, (PKCS8PrivateRSA, ASN1), (version, (Ctor<0>), UInt8),
  (id, (Ctor<1>, (ASN1::Fmt<1, tagU(), seq(0), tagU(OID)>)), Bytes),
  (rsa, (Ctor<2>, (ASN1::Fmt<2, tagU(), str(1, 0)>)), UDT));

// PKCS#8 private key - EC version (SEC1 payload)
struct PKCS8PrivateEC {
  uint8_t	version;
  ZuBSpan	id;
  ZuBSpan	id2;
  SEC1Private	ec;
};
ZfStruct(
  Ztls_API, (PKCS8PrivateEC, ASN1), (version, (Ctor<0>), UInt8),
  (id, (Ctor<1>, (ASN1::Fmt<1, tagU(), seq(0), tagU(OID)>)), Bytes),
  (id2, (Ctor<2>, (ASN1::Fmt<2, tagU(), seq(1), tagU(OID)>)), Bytes),
  (ec, (Ctor<3>, (ASN1::Fmt<3, tagU(), str(1, 0)>)), UDT));

// PKCS#8 private key - RFC 8410 25519 raw key payload
struct PKCS8Private25519 {
  uint8_t	version;
  ZuBSpan	id;
  ZuBSpan	key;
};
ZfStruct(
  Ztls_API, (PKCS8Private25519, ASN1), (version, (Ctor<0>), UInt8),
  (id, (Ctor<1>, (ASN1::Fmt<1, tagU(), seq(0), tagU(OID)>)), Bytes),
  (key, (Ctor<2>, (ASN1::Fmt<2, tagU(), str(1, 0)>)), Bytes));

struct PKCS8PrivateMLKEMSeed {
  uint8_t	version;
  ZuBSpan	id;
  ZuBSpan	seed;
};
ZfStruct(
  Ztls_API, (PKCS8PrivateMLKEMSeed, ASN1), (version, (Ctor<0>), UInt8),
  (id, (Ctor<1>, (ASN1::Fmt<1, tagU(), seq(0), tagU(OID)>)), Bytes),
  (seed, (Ctor<2>, (ASN1::Fmt<2, tagU(), str(1, 0), tagI(0)>)), Bytes));

struct PKCS8PrivateMLKEMExpanded {
  uint8_t	version;
  ZuBSpan	id;
  ZuBSpan	key;
};
ZfStruct(
  Ztls_API, (PKCS8PrivateMLKEMExpanded, ASN1), (version, (Ctor<0>), UInt8),
  (id, (Ctor<1>, (ASN1::Fmt<1, tagU(), seq(0), tagU(OID)>)), Bytes),
  (key, (Ctor<2>, (ASN1::Fmt<2, tagU(), str(1, 0)>)), Bytes));

struct MLKEMPrivateBoth {
  ZuBSpan	seed;
  ZuBSpan	key;
};
ZfStruct(Ztls_API, (MLKEMPrivateBoth, ASN1), (seed, (Ctor<0>), Bytes), (key, (Ctor<1>), Bytes));

struct PKCS8PrivateMLKEMBoth {
  uint8_t		version;
  ZuBSpan		id;
  MLKEMPrivateBoth	both;
};
ZfStruct(
  Ztls_API, (PKCS8PrivateMLKEMBoth, ASN1), (version, (Ctor<0>), UInt8),
  (id, (Ctor<1>, (ASN1::Fmt<1, tagU(), seq(0), tagU(OID)>)), Bytes),
  (both, (Ctor<2>, (ASN1::Fmt<2, tagU(), str(1, 0)>)), UDT));

struct PKCS8PrivateMLKEM {
  uint8_t	version;
  ZuBSpan	id;
  ZuBSpan	key;
};
ZfStruct(
  Ztls_API, (PKCS8PrivateMLKEM, ASN1), (version, (Ctor<0>), UInt8),
  (id, (Ctor<1>, (ASN1::Fmt<1, tagU(), seq(0), tagU(OID)>)), Bytes), (key, (Ctor<2>), Bytes));

// RSA PKCS#1 public key
struct PKCS1Public {
  ZuBSpan	modulus;
  ZuBSpan	pubExp;
};
ZfStruct(
  Ztls_API, (PKCS1Public, ASN1), (modulus, (Ctor<0>, ASN1::Type<Integer>), Bytes),
  (pubExp, (Ctor<1>, ASN1::Type<Integer>), Bytes));

// X509 public key - header (counterpart to both SEC1 and PKCS#8)
struct X509PublicHdr {
  ZuBSpan	id; // e.g. OIDs::ECUnrestricted for EC
  //      OIDs::PKCS1RSA for RSA
  //      1.3.101.112 for ED25519
  ZuBSpan	id2; // e.g. OIDs::ECGroupSecp256r1 for EC
	       // null for RSA and ED25519
};
ZfStruct(
  Ztls_API, (X509PublicHdr, ASN1),
  (id, (Ctor<0>, (ASN1::Fmt<0, tagU(), seq(0), tagU(OID)>)), Bytes),
  (id2, (Ctor<1>, (ASN1::Fmt<1, tagU(), seq(1), tagU(OID)>), ASN1::Opt), Bytes));

// X509 public key - RSA version
struct X509PublicRSA {
  ZuBSpan	id;
  PKCS1Public	rsa;
};
ZfStruct(
  Ztls_API, (X509PublicRSA, ASN1),
  (id, (Ctor<0>, (ASN1::Fmt<0, tagU(), seq(0), tagU(OID)>)), Bytes),
  (rsa, (Ctor<3>, (ASN1::Fmt<3, tagU(), bstr(1, 0)>)), UDT));

// X509 public key - EC version
struct X509PublicEC {
  ZuBSpan	id;
  ZuBSpan	id2;
  ZuBSpan	pubKey;
};
ZfStruct(
  Ztls_API, (X509PublicEC, ASN1),
  (id, (Ctor<0>, (ASN1::Fmt<0, tagU(), seq(0), tagU(OID)>)), Bytes),
  (id2, (Ctor<1>, (ASN1::Fmt<1, tagU(), seq(1), tagU(OID)>)), Bytes),
  (pubKey, (Ctor<2>, (ASN1::Type<BitString>)), Bytes));

// X509 public key - RFC 8410 25519 raw key payload
struct X509Public25519 {
  ZuBSpan	id;
  ZuBSpan	pubKey;
};
ZfStruct(
  Ztls_API, (X509Public25519, ASN1),
  (id, (Ctor<0>, (ASN1::Fmt<0, tagU(), seq(0), tagU(OID)>)), Bytes),
  (pubKey, (Ctor<2>, (ASN1::Type<BitString>)), Bytes));

struct X509PublicMLKEM {
  ZuBSpan	id;
  ZuBSpan	pubKey;
};
ZfStruct(
  Ztls_API, (X509PublicMLKEM, ASN1),
  (id, (Ctor<0>, (ASN1::Fmt<0, tagU(), seq(0), tagU(OID)>)), Bytes),
  (pubKey, (Ctor<1>, (ASN1::Type<BitString>)), Bytes));

namespace Type {
  enum {
    PKCS8Private = 0, // PKCS#8 private key
    SEC1Private,	    // SEC1 EC private key
    PKCS1Private,	    // PKCS#1 RSA private key
    X509Public,	    // X509 public key (counterpart to PKCS#8 and SEC1)
    PKCS1Public,	    // PKCS#1 RSA public key
  };
};

Ztls_Extern int pemHead(ZuCSpan, unsigned &offset, unsigned &length);
Ztls_Extern int pemTail(ZuCSpan, int type);

} // namespace Data

} // namespace Ztls::PK

// ZtlsPKData_HH
#endif
