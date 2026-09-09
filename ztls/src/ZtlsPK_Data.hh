//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// key file formats
// - public and private key file formats
// - ASN.1 DER data structures
// - PKCS#1, SEC1, PKCS#8 and X.509

#ifndef ZtlsPK_Data_HH
#define ZtlsPK_Data_HH

#ifndef ZtlsLib_HH
#include <zlib/ZtlsLib.hh>
#endif

#include <zlib/ZuSpan.hh>
#include <zlib/ZuString.hh>

#include <zlib/ZfStruct.hh>
#include <zlib/ZfASN1.hh>

namespace Ztls::PK {

enum { BufSize = 4<<10 };	// a 4096bit RSA key pem is under 4k
enum { DERBufSize = 3<<10 };	// BufSize reduced by 3/4 for base64

using namespace ZfASN1::Encoding;

namespace OIDs {
  constexpr auto PKCS1_RSA = "\x2a\x86\x48\x86\xf7\x0d\x01\x01\x01"_Zu;
  constexpr auto EC_ALG_UNRESTRICTED = "\x2a\x86\x48\xce\x3d\x02\x01"_Zu;
  constexpr auto ED25519 = "\x2b\x65\x70"_Zu;
  constexpr auto EC_GRP_SECP256R1 = "\x2a\x86\x48\xce\x3d\x03\x01\x07"_Zu;
} // OIDs

namespace Data {

// RSA PKCS#1 private key
struct SK_PKCS1 {
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
ZfStruct(ZtlsAPI, (SK_PKCS1, ASN1),
  (((version),	(Ctor<0>)),				(UInt8)),
  (((modulus),	(Ctor<1>, ASN1::Type<Integer>)),	(Bytes)),
  (((pubExp),	(Ctor<2>, ASN1::Type<Integer>)),	(Bytes)),
  (((prvExp),	(Ctor<3>, ASN1::Type<Integer>)),	(Bytes)),
  (((prime1),	(Ctor<4>, ASN1::Type<Integer>)),	(Bytes)),
  (((prime2),	(Ctor<5>, ASN1::Type<Integer>)),	(Bytes)),
  (((exp1),	(Ctor<6>, ASN1::Type<Integer>)),	(Bytes)),
  (((exp2),	(Ctor<7>, ASN1::Type<Integer>)),	(Bytes)),
  (((coeff),	(Ctor<8>, ASN1::Type<Integer>)),	(Bytes)));

// EC SEC1 private key
struct SK_SEC1 {
  uint8_t	version;
  ZuBSpan	key;
  ZuBSpan	id;		// e.g. OIDs::EC_GRP_SECP256R1
  ZuBSpan	pubKey;		// optional
};
ZfStruct(ZtlsAPI, (SK_SEC1, ASN1),
  (((version), (Ctor<0>)),					   (UInt8)),
  (((key),     (Ctor<1>)),					   (Bytes)),
  (((id),      (Ctor<2>, (ASN1::Fmt<2, tag(0, OID)>), ASN1::Opt)), (Bytes)),
  (((pubKey),  (Ctor<3>, (ASN1::Fmt<3, tag(1)>),
	        ASN1::Type<BitString>, ASN1::Opt)),		   (Bytes)));

// PKCS#8 private key - header
struct SK_PKCS8_HDR {
  uint8_t	version;	// 0
  ZuBSpan	id;		// e.g. OIDs::EC_ALG_UNRESTRICTED for EC
				//      OIDs::PKCS1_RSA for RSA
				//      1.3.101.112 for ED25519
};
ZfStruct(ZtlsAPI, (SK_PKCS8_HDR, ASN1),
  (((version), (Ctor<0>)),					      (UInt8)),
  (((id),      (Ctor<1>, (ASN1::Fmt<1, tagU(), seq(0), tagU(OID)>))), (Bytes)));

// PKCS#8 private key - RSA version (PKCS#1 payload)
struct SK_PKCS8_RSA {
  uint8_t	version;
  ZuBSpan	id;
  SK_PKCS1	rsa;
};
ZfStruct(ZtlsAPI, (SK_PKCS8_RSA, ASN1),
  (((version), (Ctor<0>)),					      (UInt8)),
  (((id),      (Ctor<1>, (ASN1::Fmt<1, tagU(), seq(0), tagU(OID)>))), (Bytes)),
  (((rsa),     (Ctor<2>, (ASN1::Fmt<2, tagU(), str(1, 0)>))),	      (UDT)));

// PKCS#8 private key - EC version (SEC1 payload)
struct SK_PKCS8_EC {
  uint8_t	version;
  ZuBSpan	id;
  ZuBSpan	id2;
  SK_SEC1	ec;
};
ZfStruct(ZtlsAPI, (SK_PKCS8_EC, ASN1),
  (((version), (Ctor<0>)),					      (UInt8)),
  (((id),      (Ctor<1>, (ASN1::Fmt<1, tagU(), seq(0), tagU(OID)>))), (Bytes)),
  (((id2),     (Ctor<2>, (ASN1::Fmt<2, tagU(), seq(1), tagU(OID)>))), (Bytes)),
  (((ec),      (Ctor<3>, (ASN1::Fmt<3, tagU(), str(1, 0)>))),	      (UDT)));

// PKCS#8 private key - ED25519 version (raw key data payload)
struct SK_PKCS8_ED25519 {
  uint8_t	version;
  ZuBSpan	id;
  ZuBSpan	key;
};
ZfStruct(ZtlsAPI, (SK_PKCS8_ED25519, ASN1),
  (((version), (Ctor<0>)),					      (UInt8)),
  (((id),      (Ctor<1>, (ASN1::Fmt<1, tagU(), seq(0), tagU(OID)>))), (Bytes)),
  (((key),     (Ctor<2>, (ASN1::Fmt<2, tagU(), str(1, 0)>))),	      (Bytes)));

// RSA PKCS#1 public key
struct PK_PKCS1 {
  ZuBSpan	modulus;
  ZuBSpan	pubExp;
};
ZfStruct(ZtlsAPI, (PK_PKCS1, ASN1),
  (((modulus),	(Ctor<0>, ASN1::Type<Integer>)),	(Bytes)),
  (((pubExp),	(Ctor<1>, ASN1::Type<Integer>)),	(Bytes)));

// X509 public key - header (counterpart to both SEC1 and PKCS#8)
struct PK_X509_HDR {
  ZuBSpan	id;		// e.g. OIDs::EC_ALG_UNRESTRICTED for EC
				//      OIDs::PKCS1_RSA for RSA
				//      1.3.101.112 for ED25519
  ZuBSpan	id2;		// e.g. OIDs::EC_GRP_SECP256R1 for EC
				//      null for RSA and ED25519
};
ZfStruct(ZtlsAPI, (PK_X509_HDR, ASN1),
  (((id),  (Ctor<0>, (ASN1::Fmt<0, tagU(), seq(0), tagU(OID)>))), (Bytes)),
  (((id2), (Ctor<1>, (ASN1::Fmt<1, tagU(), seq(1), tagU(OID)>), ASN1::Opt)),
								  (Bytes)));

// X509 public key - RSA version
struct PK_X509_RSA {
  ZuBSpan	id;
  PK_PKCS1	rsa;
};
ZfStruct(ZtlsAPI, (PK_X509_RSA, ASN1),
  (((id),  (Ctor<0>, (ASN1::Fmt<0, tagU(), seq(0), tagU(OID)>))),    (Bytes)),
  (((rsa), (Ctor<3>, (ASN1::Fmt<3, tagU(), bstr(1, 0)>))),	     (UDT)));

// X509 public key - EC version
struct PK_X509_EC {
  ZuBSpan	id;
  ZuBSpan	id2;
  ZuBSpan	pubKey;
};
ZfStruct(ZtlsAPI, (PK_X509_EC, ASN1),
  (((id),     (Ctor<0>, (ASN1::Fmt<0, tagU(), seq(0), tagU(OID)>))), (Bytes)),
  (((id2),    (Ctor<1>, (ASN1::Fmt<1, tagU(), seq(1), tagU(OID)>))), (Bytes)),
  (((pubKey), (Ctor<2>, (ASN1::Type<BitString>))),		     (Bytes)));

// X509 public key - ED25519 version
struct PK_X509_ED25519 {
  ZuBSpan	id;
  ZuBSpan	pubKey;
};
ZfStruct(ZtlsAPI, (PK_X509_ED25519, ASN1),
  (((id),     (Ctor<0>, (ASN1::Fmt<0, tagU(), seq(0), tagU(OID)>))), (Bytes)),
  (((pubKey), (Ctor<2>, (ASN1::Type<BitString>))),		     (Bytes)));

namespace Type {
  enum {
    SK_PKCS8 = 0,	// PKCS#8 private key
    SK_SEC1,		// SEC1 EC private key
    SK_PKCS1,		// PKCS#1 RSA private key
    PK_X509,		// X509 public key (counterpart to PKCS#8 and SEC1)
    PK_PKCS1,		// PKCS#1 RSA public key
  };
};

ZtlsAPI int pemHead(ZuCSpan, unsigned &offset, unsigned &length);
ZtlsAPI int pemTail(ZuCSpan, int type);

} // Data

} // Ztls::PK

#endif /* ZtlsPK_Data_HH */
