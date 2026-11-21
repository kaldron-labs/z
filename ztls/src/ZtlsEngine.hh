//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Copyright (c) 2016 DeNA Co., Ltd., Kazuho Oku
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to
// deal in the Software without restriction, including without limitation the
// rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.

#ifndef ZtlsEngine_HH
#define ZtlsEngine_HH

#ifndef ZtlsLib_HH
#include <zlib/ZtlsLib.hh>
#endif

#include <assert.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include <zlib/ZuArray.hh>
#include <zlib/ZuAssert.hh>
#include <zlib/ZuByteSwap.hh>
#include <zlib/ZuDerive.hh>
#include <zlib/ZuLib.hh>

#include <zlib/ZmLHash.hh>

#include <zlib/ZtArray.hh>
#include <zlib/ZtBuiltin.hh>
#include <zlib/ZtEnum.hh>

#ifndef ZtlsFusion
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#define ZtlsFusion 1
#else
#define ZtlsFusion 0
#endif
#endif

#define Ztls_SrvCertVerifyCtx "TLS 1.3, server CertificateVerify"
#define Ztls_CliCertVerifyCtx "TLS 1.3, client CertificateVerify"
#define Ztls_SignInputSize (64 + sizeof(Ztls_SrvCertVerifyCtx) + Ztls_MaxDigestSize * 2)

// Common chains and offers fit inline; longer valid lists grow on named heaps.
#ifndef ZtlsCertChainBuiltin
#define ZtlsCertChainBuiltin 16
#endif
#ifndef ZtlsSuiteBuiltin
#define ZtlsSuiteBuiltin 16
#endif
#ifndef ZtlsSuiteBits
#define ZtlsSuiteBits 6
#endif
ZuAssert(ZtlsSuiteBits >= 2);
#ifndef ZtlsALPNBuiltin
#define ZtlsALPNBuiltin 16
#endif
#ifndef ZtlsCertCompBuiltin
#define ZtlsCertCompBuiltin 16
#endif

namespace Ztls {
  struct Cxn;
  class RecordSink;
}

namespace Ztls_ {

ZuDerive(CertificateDER, (ZtBArray<ZtArrayHeapID<"Ztls.Engine.CertificateDER">>));
ZuDerive(
  CertificateChain,
  (ZtBuiltin<
    ZtArray<CertificateDER, ZtArrayHeapID<"Ztls.Engine.CertificateChain">>,
    ZtlsCertChainBuiltin>));

ZuDerive(
  KeyExchangeBytes, (ZtBArray<ZtArraySecret<true, ZtArrayHeapID<"Ztls.Engine.KeyExchange">>>));
ZuDerive(EchBytes, (ZtBArray<ZtArraySecret<true, ZtArrayHeapID<"Ztls.Engine.ECH">>>));

#define Ztls_HelloRandomSize 32

#define Ztls_AES128KeySize 16
#define Ztls_AES256KeySize 32
#define Ztls_AESBlockSize 16
#define Ztls_AESIVSize 16
#define Ztls_AESGCMIVSize 12
#define Ztls_AESGCMTagSize 16
// 2^25
#define Ztls_AESGCMConfLimit 0x2000000
// 2^54
#define Ztls_AESGCMIntegrityLimit uint64_t(0x40000000000000)
// 2^23.5
#define Ztls_AESCCMConfLimit 0xB504F3
// 2^23.5
#define Ztls_AESCCMIntegrityLimit 0xB504F3

#define Ztls_ChaCha20KeySize 32
// contrary to RFC 7539, follow OpenSSL way of using first 32 bits as ctr and latter 96 as IV
#define Ztls_ChaCha20IVSize 16
#define Ztls_ChaChaIVSize 12
#define Ztls_ChaChaTagSize 16
// at least 2^64
#define Ztls_ChaChaConfLimit UINT64_MAX
// 2^36
#define Ztls_ChaChaIntLimit uint64_t(0x1000000000)

#define Ztls_AEGIS128LKeySize 16
#define Ztls_AEGIS128LIVSize 16
#define Ztls_AEGIS128LTagSize 16
// at least 2^64
#define Ztls_AEGIS128LConfLimit UINT64_MAX
// 2^48
#define Ztls_AEGIS128LIntLimit uint64_t(0x1000000000000)

#define Ztls_AEGIS256KeySize 32
#define Ztls_AEGIS256IVSize 32
#define Ztls_AEGIS256TagSize 16
// at least 2^64
#define Ztls_AEGIS256ConfLimit UINT64_MAX
// 2^48
#define Ztls_AEGIS256IntLimit uint64_t(0x1000000000000)

#define Ztls_BlowfishKeySize 16
#define Ztls_BlowfishBlockSize 8

#define Ztls_SHA256BlockSize 64
#define Ztls_SHA256DigestSize 32

#define Ztls_SHA384BlockSize 128
#define Ztls_SHA384DigestSize 48

#define Ztls_SHA512BlockSize 128
#define Ztls_SHA512DigestSize 64

#define Ztls_MaxSecretSize 32
#define Ztls_MaxIVSize 32
#define Ztls_MaxDigestSize 64
#ifndef ZtlsMaxSigAlgos
#define ZtlsMaxSigAlgos 64
#endif

// versions
#define Ztls_ProtoVerTls13 0x0304

// cipher-suites
#define Ztls_SuiteAes128gcmSHA256 0x1301
#define Ztls_SuiteStrAes128gcm "TLS_AES_128_GCM_SHA256"
#define Ztls_SuiteAes256gcmSHA384 0x1302
#define Ztls_SuiteStrAes256gcm "TLS_AES_256_GCM_SHA384"
#define Ztls_SuiteChaChaSHA256 0x1303
#define Ztls_SuiteStrChaChaSHA256 "TLS_CHACHA20_POLY1305_SHA256"
#define Ztls_SuiteAEGIS256SHA512 0x1306
#define Ztls_SuiteStrAEGIS256 "TLS_AEGIS_256_SHA512"
#define Ztls_SuiteAEGIS128LSHA256 0x1307
#define Ztls_SuiteStrAEGIS128L "TLS_AEGIS_128L_SHA256"

// negotiatedGroups
#define Ztls_GroupSecp256r1 23
#define Ztls_GroupNameSecp256r1 "secp256r1"
#define Ztls_GroupSecp384r1 24
#define Ztls_GroupNameSecp384r1 "secp384r1"
#define Ztls_GroupSecp521r1 25
#define Ztls_GroupNameSecp521r1 "secp521r1"
#define Ztls_GroupX25519 29
#define Ztls_GroupNameX25519 "x25519"
#define Ztls_GroupX448 30
#define Ztls_GroupNameX448 "x448"
#define Ztls_GroupSecp256r1mlkem768 4587
#define Ztls_GroupStrP256mlkem768 "SecP256r1MLKEM768"
#define Ztls_GroupX25519mlkem768 4588
#define Ztls_GrpStrX25519mlkem768 "X25519MLKEM768"
#define Ztls_GroupP384mlkem1024 4589
#define Ztls_GroupStrP384mlkem1024 "SecP384r1MLKEM1024"
#define Ztls_GroupMLKEM512 512
#define Ztls_GroupNameMLKEM512 "MLKEM512"
#define Ztls_GroupMLKEM768 513
#define Ztls_GroupNameMLKEM768 "MLKEM768"
#define Ztls_GroupMLKEM1024 514
#define Ztls_GroupNameMLKEM1024 "MLKEM1024"

// signature algorithms
#define Ztls_SigRSAPKCS1SHA1 0x0201
#define Ztls_SigRSAPKCS1SHA256 0x0401
#define Ztls_SigECDSAP256SHA256 0x0403
#define Ztls_SigECDSAP384SHA384 0x0503
#define Ztls_SigECDSAP521SHA512 0x0603
#define Ztls_SigRSAPSSRSAESHA256 0x0804
#define Ztls_SigRSAPSSRSAESHA384 0x0805
#define Ztls_SigRSAPSSRSAESHA512 0x0806
#define Ztls_SigEd25519 0x0807

// HPKE
#define Ztls_HPKEModeBase 0
#define Ztls_HPKEModePSK 1
#define Ztls_HPKEModeAuth 2
#define Ztls_HPKEModeAuthPSK 3
#define Ztls_HPKEKEMP256SHA256 16
#define Ztls_HPKEKEMP384SHA384 17
#define Ztls_HPKEKEMX25519SHA256 32
#define Ztls_HPKEHKDFSHA256 1
#define Ztls_HPKEHKDFSHA384 2
#define Ztls_HPKEHKDFSHA512 3
#define Ztls_HPKEAEADAES128GCM 1
#define Ztls_HPKEAEADAES256GCM 2
#define Ztls_HPKEAEADChaCha 3

// error classes and macros
#define Ztls_ErrorClassSelfAlert 0
#define Ztls_ErrorClassPeerAlert 0x100
#define Ztls_ErrorClassInternal 0x200

#define Ztls_ErrorGetClass(e) ((e) & ~0xff)
#define Ztls_AlertToSelfError(e) ((e) + Ztls_ErrorClassSelfAlert)
#define Ztls_AlertToPeerError(e) ((e) + Ztls_ErrorClassPeerAlert)
#define Ztls_ErrorToAlert(e) ((e) & 0xff)

// the HKDF prefix
#define Ztls_HKDFLabelPrefix "tls13 "

// alerts
#define Ztls_AlertLevelWarning 1
#define Ztls_AlertLevelFatal 2

#define Ztls_AlertCloseNotify 0
#define Ztls_AlertUnwaitedMsg 10
#define Ztls_AlertBadRecordMac 20
#define Ztls_AlertRecordOverflow 22
#define Ztls_AlertHSFailure 40
#define Ztls_AlertBadCert 42
#define Ztls_AlertUnsupportedCert 43
#define Ztls_AlertCertRevoked 44
#define Ztls_AlertCertExpired 45
#define Ztls_AlertCertUnknown 46
#define Ztls_AlertIllegalParameter 47
#define Ztls_AlertUnknownCa 48
#define Ztls_AlertAccessDenied 49
#define Ztls_AlertDecodeError 50
#define Ztls_AlertDecryptError 51
#define Ztls_AlertProtoVer 70
#define Ztls_AlertInternalError 80
#define Ztls_AlertUserCanceled 90
#define Ztls_AlertMissingExt 109
#define Ztls_AlertUnsupportedExt 110
#define Ztls_AlertUnrecognizedName 112
#define Ztls_AlertUnknownPSKId 115
#define Ztls_AlertCertRequired 116
#define Ztls_AlertNoAppProto 120
#define Ztls_AlertECHRequired 121

// internal errors
#define Ztls_ErrorNoMemory (Ztls_ErrorClassInternal + 1)
#define Ztls_ErrorInProgress (Ztls_ErrorClassInternal + 2)
#define Ztls_ErrorLibrary (Ztls_ErrorClassInternal + 3)
#define Ztls_ErrorIncompatibleKey (Ztls_ErrorClassInternal + 4)
#define Ztls_ErrorSessionNotFound (Ztls_ErrorClassInternal + 5)
#define Ztls_ErrorStatelessRetry (Ztls_ErrorClassInternal + 6)
#define Ztls_ErrorNotAvailable (Ztls_ErrorClassInternal + 7)
#define Ztls_ErrorCompFailure (Ztls_ErrorClassInternal + 8)
#define Ztls_ErrorRejectEarlyData (Ztls_ErrorClassInternal + 9)
#define Ztls_ErrorDelegate (Ztls_ErrorClassInternal + 10)
#define Ztls_ErrorAsyncOperation (Ztls_ErrorClassInternal + 11)
#define Ztls_ErrorBlockOverflow (Ztls_ErrorClassInternal + 12)
#define Ztls_ErrorRawOutput (Ztls_ErrorClassInternal + 13)

#define Ztls_ErrBadPEMKeyVer (Ztls_ErrorClassInternal + 60)
#define Ztls_ErrBadPEMECDSAVer (Ztls_ErrorClassInternal + 61)
#define Ztls_ErrBadPEMECDSACurve (Ztls_ErrorClassInternal + 62)
#define Ztls_ErrBadPEMECDSASize (Ztls_ErrorClassInternal + 63)
#define Ztls_ErrBadECDSAAsn1 (Ztls_ErrorClassInternal + 64)

#define Ztls_HsTypeCliHello 1
#define Ztls_HsTypeSrvHello 2
#define Ztls_HsTypeNewTicket 4
#define Ztls_HsTypeEndOfEarly 5
#define Ztls_HsTypeEncExts 8
#define Ztls_HsTypeCert 11
#define Ztls_HsTypeCertRequest 13
#define Ztls_HsTypeCertVerify 15
#define Ztls_HSTypeFinished 20
#define Ztls_HsTypeKeyUpdate 24
#define Ztls_HsTypeCompressedCert 25
#define Ztls_HsTypeMessageHash 254
#define Ztls_HsTypePseudoHRR -1

#define Ztls_CertTypeX509 0
#define Ztls_CertTypeRawPubkey 2

#define Ztls_ZeroDigestSHA256 \
  { \
    0xe3, 0xb0, 0xc4, 0x42, 0x98, 0xfc, 0x1c, 0x14, 0x9a, 0xfb, 0xf4, \
    0xc8, 0x99, 0x6f, 0xb9, 0x24, 0x27, 0xae, 0x41, 0xe4, 0x64, 0x9b, \
    0x93, 0x4c, 0xa4, 0x95, 0x99, 0x1b, 0x78, 0x52, 0xb8, 0x55 \
  }

#define Ztls_ZeroDigestSHA384 \
  { \
    0x38, 0xb0, 0x60, 0xa7, 0x51, 0xac, 0x96, 0x38, 0x4c, 0xd9, 0x32, 0x7e, \
    0xb1, 0xb1, 0xe3, 0x6a, 0x21, 0xfd, 0xb7, 0x11, 0x14, 0xbe, 0x07, 0x43, \
    0x4c, 0x0c, 0xc7, 0xbf, 0x63, 0xf6, 0xe1, 0xda, 0x27, 0x4e, 0xde, 0xbf, \
    0xe7, 0x6f, 0x65, 0xfb, 0xd5, 0x1a, 0xd2, 0xf1, 0x48, 0x98, 0xb9, 0x5b \
  }

#define Ztls_ZeroDigestSHA512 \
  { \
    0xcf, 0x83, 0xe1, 0x35, 0x7e, 0xef, 0xb8, 0xbd, 0xf1, 0x54, 0x28, 0x50, 0xd6, \
    0x6d, 0x80, 0x07, 0xd6, 0x20, 0xe4, 0x05, 0x0b, 0x57, 0x15, 0xdc, 0x83, 0xf4, \
    0xa9, 0x21, 0xd3, 0x6c, 0xe9, 0xce, 0x47, 0xd0, 0xd1, 0x3c, 0x5d, 0x85, 0xf2, \
    0xb0, 0xff, 0x83, 0x18, 0xd2, 0x87, 0x7e, 0xec, 0x2f, 0x63, 0xb9, 0x31, 0xbd, \
    0x47, 0x41, 0x7a, 0x81, 0xa5, 0x38, 0x32, 0x7a, 0xf9, 0x27, 0xda, 0x3e \
  }

// default maximum of tickets to send (see Context::ticketRequests.maxCount)
#ifndef ZtlsMaxTickets
#define ZtlsMaxTickets 4
#endif

struct Context;
struct KeySchedule;
struct KeyExchangeAlgo;
struct CipherAlgo;
struct AeadAlgo;

// represents a sequence of octets
using Bytes = ZuSpan<uint8_t>;

// used for storing output

struct Buffer {
  uint8_t		*base;
  size_t		capacity;
  size_t		off;
  uint8_t		isAllocated;
  // boolean
  uint8_t		alignBits;
  //  if particular alignment is required, set to log2(alignment); otherwize zero
  uint8_t		tx;
  // direction: 0 for receive-side, 1 for transmit-side
  bool			bounded;
  // borrowed output may not grow or relocate
  Ztls::RecordSink	*recordSink;
  // internal synchronous record destination, otherwise NULL
};

// Key-exchange contexts own their ephemeral private key and public-key backing.
struct KeyExchangeContext {
  const KeyExchangeAlgo	*algo;
  Bytes			pubkey;

  explicit KeyExchangeContext(const KeyExchangeAlgo *algo_) : algo{algo_} { }
  KeyExchangeContext(const KeyExchangeContext &) = delete;
  KeyExchangeContext &operator=(const KeyExchangeContext &) = delete;
  virtual ~KeyExchangeContext() = default;
  virtual int exchange(KeyExchangeBytes *secret, Bytes peerkey) = 0;
};

struct KeyExchangeAlgo {
  uint16_t	id;
  intptr_t	data;
  const char	*name;

  KeyExchangeAlgo(uint16_t id_, intptr_t data_, const char *name_)
  :
    id{id_}, data{data_}, name{name_}
  { }
  virtual ~KeyExchangeAlgo() = default;
  virtual int create(KeyExchangeContext **ctx) const = 0;
  virtual int
  exchange(KeyExchangeBytes *pubkey, KeyExchangeBytes *secret, Bytes peerkey) const = 0;
};

struct CipherContext {
  const CipherAlgo	*algo;

  explicit CipherContext(const CipherAlgo *algo_) : algo{algo_} { }
  CipherContext(const CipherContext &) = delete;
  CipherContext &operator=(const CipherContext &) = delete;
  virtual ~CipherContext() = default;
  virtual void init(const void *iv) = 0;
  virtual void transform(void *output, const void *input, size_t len) = 0;
};

struct CipherAlgo {
  const char	*name;
  size_t	keySize;
  size_t	blockSize;
  size_t	ivSize;

  CipherAlgo(const char *name_, size_t keySize, size_t blockSize, size_t ivSize)
  :
    name{name_}, keySize{keySize}, blockSize{blockSize}, ivSize{ivSize}
  { }
  virtual ~CipherAlgo() = default;
  virtual CipherContext *create(int isEnc, const void *key) const = 0;
};

// This object specifies a 16-byte supplementary cipher operation calculated as part of AEAD
// encryption. QUIC and DTLS stacks can use this object to derive a header-protection or
// packet-number-protection mask while protecting the packet payload.
struct AeadSupplement {
  // Cipher context to be used.
  CipherContext	*ctx;

  // Input to the supplementary cipher. This field may point into the AEAD output buffer,
  // including ciphertext or tag bytes. AEAD callbacks must read it only after the referenced
  // output bytes have been written; `aeadEncryptVS` reads it after the entire
  // ciphertext-plus-tag output has been written.
  const void	*input;

  // Output.
  uint8_t	output[16];
};

// Complete AEAD contexts own their native state. Plaintext from serial decryption
// remains provisional until decEnd authenticates its tag.
struct AeadContext {
  const AeadAlgo	*algo;
  bool			isEnc;

  AeadContext(const AeadAlgo *algo_, bool isEnc_) : algo{algo_}, isEnc{isEnc_} { }
  AeadContext(const AeadContext &) = delete;
  AeadContext &operator=(const AeadContext &) = delete;
  virtual ~AeadContext() = default;
  virtual void getIV(void *iv) = 0;
  virtual void setIV(const void *iv) = 0;
  virtual bool serialEncrypt() const { return false; }
  virtual bool serialDecrypt() const { return false; }
  virtual int encBegin(uint64_t, const void *, size_t) { return Ztls_ErrorNotAvailable; }
  virtual int encChunk(void *, const void *, size_t, size_t *written)
  {
    *written = 0;
    return Ztls_ErrorNotAvailable;
  }
  virtual int encEnd(void *, size_t, size_t *written)
  {
    *written = 0;
    return Ztls_ErrorNotAvailable;
  }
  virtual int decBegin(uint64_t, const void *, size_t) { return Ztls_ErrorNotAvailable; }
  virtual int decChunk(void *, size_t, const void *, size_t, size_t *written)
  {
    *written = 0;
    return Ztls_ErrorNotAvailable;
  }
  virtual int decEnd(const void *, size_t) { return Ztls_ErrorNotAvailable; }
  virtual void decAbort() { }
  virtual void encrypt(
    void *output, const void *input, size_t inlen, uint64_t seq, const void *aad, size_t aadlen,
    AeadSupplement *supp);
  virtual void encryptV(
    void *output, const Bytes *input, size_t incnt, uint64_t seq, const void *aad,
    size_t aadlen) = 0;
  // Supplementary input can point into output; read it after all ciphertext and tag bytes exist.
  virtual void encryptVS(
    void *output, const Bytes *input, size_t incnt, uint64_t seq, const void *aad, size_t aadlen,
    AeadSupplement *supp);
  virtual size_t decrypt(
    void *output, const void *input, size_t inlen, uint64_t seq, const void *aad,
    size_t aadlen) = 0;
};

struct AeadAlgo {
  const char		*name;
  uint64_t		confidentialityLimit;
  uint64_t		integrityLimit;
  const CipherAlgo	*ctrCipher;
  const CipherAlgo	*ecbCipher;
  size_t		keySize;
  size_t		ivSize;
  size_t		tagSize;
  unsigned		nonTemporal:1;
  uint8_t		alignBits;
  unsigned		serialEncrypt:1;
  unsigned		serialDecrypt:1;

  AeadAlgo(
    const char *name_, uint64_t confidentiality, uint64_t integrity, const CipherAlgo *ctr,
    const CipherAlgo *ecb, size_t keySize, size_t ivSize, size_t tagSize, bool nonTemporal,
    uint8_t alignBits, bool serialEnc, bool serialDec)
  :
    name{name_}, confidentialityLimit{confidentiality}, integrityLimit{integrity}, ctrCipher{ctr},
    ecbCipher{ecb}, keySize{keySize}, ivSize{ivSize}, tagSize{tagSize}, nonTemporal{nonTemporal},
    alignBits{alignBits}, serialEncrypt{serialEnc}, serialDecrypt{serialDec}
  { }
  virtual ~AeadAlgo() = default;
  virtual AeadContext *create(bool isEnc, const void *key, const void *iv) const = 0;
};

// Final leaves ownership with the caller; Reset reinitializes; Snapshot does not mutate.
ZtEnumNS(Ztls_API, HashFinalMode, int8_t, Final, Reset, Snapshot);

// A hash context.
struct HashContext {
  HashContext() = default;
  HashContext(const HashContext &) = delete;
  HashContext &operator=(const HashContext &) = delete;
  virtual ~HashContext() = default;
  virtual void update(const void *src, size_t len) = 0;
  // Final obtains a digest; lifetime always belongs to the caller.
  virtual int final(void *md, HashFinalMode::T mode = HashFinalMode::Final) = 0;
  virtual HashContext *clone() const = 0;
};

// A hash algorithm and its properties.
struct HashAlgo {
  // name of the hash algorithm
  const char	*name;

  // block size
  size_t	blockSize;

  // digest size
  size_t	digestSize;

  // constructor that creates the hash context

  // digest of zero-length octets
  uint8_t	emptyDigest[Ztls_MaxDigestSize];
  HashAlgo(
    const char *name_, size_t blockSize, size_t digestSize,
    const uint8_t (&empty)[Ztls_MaxDigestSize])
  :
    name{name_}, blockSize{blockSize}, digestSize{digestSize}
  { memcpy(emptyDigest, empty, sizeof(emptyDigest)); }
  virtual ~HashAlgo() = default;
  virtual HashContext *create() const = 0;
};

struct CipherSuite {
  // ID as defined by the TLS Cipher Suites registry
  uint16_t		id;

  // underlying AEAD algorithm
  const AeadAlgo	*aead;

  // underlying hash algorithm
  const HashAlgo	*hash;

  // value of the "Description" field of the TLS Cipher Suites registry
  const char		*name;
};

using SuiteSpan = ZuSpan<const CipherSuite *const>;

// Configured order plus fixed indexes. Mutations are setup-only; active
// connections borrow the immutable configuration and retain the selected suite.
class Ztls_API CipherSuites {
public:
  CipherSuites() = default;
  CipherSuites(const CipherSuites &);
  CipherSuites(CipherSuites &&);
  CipherSuites &operator=(const CipherSuites &);
  CipherSuites &operator=(CipherSuites &&);
  CipherSuites &operator=(SuiteSpan);

  void push(const CipherSuite *suite);
  void clear();
  unsigned length() const { return unsigned(m_suites.length()); }
  const CipherSuite *const &operator[](unsigned i) const { return m_suites[i]; }
  auto begin() const { return m_suites.data(); }
  auto end() const { return m_suites.data() + m_suites.length(); }
  operator SuiteSpan() const { return {m_suites.data(), length()}; }

  int index(uint16_t id) const;
  const CipherSuite *find(uint16_t id) const;
  const CipherSuite *first(const HashAlgo *hash) const;
  SuiteSpan hashes() const { return {m_hashes.data(), unsigned(m_hashes.length())}; }
  bool serialRx() const { return m_serialRx; }
  bool serialTx() const { return m_serialTx; }

private:
  void addIndex(unsigned i);
  void reindex();

  using Array = ZtBuiltin<
    ZtArray<const CipherSuite *, ZtArrayHeapID<"Ztls.Engine.CipherSuites">>, ZtlsSuiteBuiltin>;
  using IDIndex = ZmLHashKV<uint16_t, unsigned,
    ZmLHashStatic<ZtlsSuiteBits, ZmLHashLocal<>>>;
  using HashIndex = ZmLHashKV<const HashAlgo *, unsigned,
    ZmLHashStatic<ZtlsSuiteBits, ZmLHashLocal<>>>;
  Array		m_suites, m_hashes;
  IDIndex	m_ids;
  HashIndex	m_hashIndex;
  bool		m_serialRx = true, m_serialTx = true;
};

struct TrafficProtection;

struct MessageEmitter {
  Buffer		*buf = nullptr;
  TrafficProtection	*enc = nullptr;

  MessageEmitter() = default;
  MessageEmitter(Buffer *buf_, TrafficProtection *enc_) : buf{buf_}, enc{enc_} { }
  virtual ~MessageEmitter() = default;
  virtual bool recordLayer() const { return false; }
  virtual int beginMessage() = 0;
  virtual int commitMessage() = 0;
};

// HPKE KEM
struct HpkeKem {
  uint16_t		id;
  const KeyExchangeAlgo	*keyex;
  const HashAlgo	*hash;
};

struct HpkeSuiteID {
  uint16_t	kdf;
  uint16_t	aead;
};

struct HpkeSuite {
  HpkeSuiteID		id;
  const char		*name;
  // in form of "<kdf>/<aead>" using the sames specified in IANA HPKE registry
  const HashAlgo	*hash;
  const AeadAlgo	*aead;
};

class Ztls_API HpkeSuites {
public:
  explicit HpkeSuites(ZuSpan<const HpkeSuite *const> suites);
  HpkeSuites(const HpkeSuites &) = delete;
  HpkeSuites &operator=(const HpkeSuites &) = delete;

  const HpkeSuite *find(HpkeSuiteID id) const;
  auto begin() const { return m_suites.data(); }
  auto end() const { return m_suites.data() + m_suites.length(); }

private:
  using Array = ZtBuiltin<
    ZtArray<const HpkeSuite *, ZtArrayHeapID<"Ztls.Engine.HpkeSuites">>, ZtlsSuiteBuiltin>;
  using Index = ZmLHashKV<uint32_t, const HpkeSuite *,
    ZmLHashStatic<ZtlsSuiteBits, ZmLHashLocal<>>>;
  Array	m_suites;
  Index	m_index;
};

struct CliHelloPskIdentity {
  Bytes		identity;
  uint32_t	obfuscatedTicketAge;
  Bytes		binder;
};

// arguments passsed to the onClientHello callback
struct CliHelloParams {
  // SNI value received from the client. The value is {NULL, 0} if the extension was absent.
  Bytes					serverName;

  // Raw value of the client_hello message.
  Bytes					rawMessage;

  // points to the cipher-suites section of the rawMessage (see above)
  Bytes					cipherSuites;
  ZuSpan<const Bytes>			negotiatedProtocols;
  ZuSpan<const uint16_t>		signatureAlgorithms;
  ZuSpan<const uint16_t>		certCompAlgos;
  ZuSpan<const uint8_t>			serverCertificateTypes;
  ZuSpan<const CliHelloPskIdentity>	pskIdentities;

  // set to 1 if ClientHello is too old (or too new) to be handled by this TLS implementation
  unsigned				incompatibleVersion:1;
};

// returns current time in milliseconds (getTime can be used to return the physical time)
struct GetTime {
  virtual ~GetTime() = default;
  virtual uint64_t call() = 0;
};

// after receiving ClientHello, the core calls the optional callback to give a chance to the swap
// the context depending on the input values. The callback is required to call `serverName`
// if an SNI extension needs to be sent to the client.
struct OnClientHello {
  virtual ~OnClientHello() = default;
  virtual int call(Ztls::Cxn *tls, CliHelloParams *params) = 0;
};

// callback to generate the certificate message. Context::certificates are set when the callback
// is set to NULL.
struct EmitCertificate {
  virtual ~EmitCertificate() = default;
  virtual int call(
    Ztls::Cxn *tls, MessageEmitter *emitter, KeySchedule *keySched, Bytes context,
    int pushStatusReq, ZuSpan<const uint16_t> compressAlgos) = 0;
};

// Sign CertificateVerify. A server signer may suspend by installing a
// completion and cleanup function on the connection and returning
// Ztls_ErrorAsyncOperation. The callbacks must not retain the connection.
struct SignCertificate {
  virtual ~SignCertificate() = default;
  virtual int call(
    Ztls::Cxn *tls, bool allowAsync, uint16_t *selectedAlgo, Buffer *output, Bytes input,
    ZuSpan<const uint16_t> algorithms) = 0;
};

// Certificate verification returns an owned signature verifier. Its destructor runs
// after CertificateVerify or any intervening failure, reset or teardown.
struct VerifySignature {
  VerifySignature() = default;
  VerifySignature(const VerifySignature &) = delete;
  VerifySignature &operator=(const VerifySignature &) = delete;
  virtual ~VerifySignature() = default;
  virtual int verify(uint16_t algorithm, Bytes data, Bytes signature) = 0;
};

struct VerifyCertificate {
  ZuSpan<const uint16_t>	algos;

  virtual ~VerifyCertificate() = default;
  virtual int call(
    Ztls::Cxn *tls, const char *serverName, VerifySignature **signature,
    ZuSpan<const Bytes> certs) = 0;
};

// Encrypt-and-signs (or verify-and-decrypts) a ticket (server-only). When used for encryption
// (i.e., isEncrypt being set), the function should return 0 if successful, or else a non-zero
// value. When used for decryption, the function should return 0 (successful),
// Ztls_ErrorRejectEarlyData (successful, but 0-RTT is forbidden), or any other value to indicate
// failure.
struct EncryptTicket {
  virtual ~EncryptTicket() = default;
  virtual int call(Ztls::Cxn *tls, int isEncrypt, Buffer *dst, Bytes src) = 0;
};

// saves a ticket (client-only)
struct SaveTicket {
  virtual ~SaveTicket() = default;
  virtual int call(Ztls::Cxn *tls, Bytes input) = 0;
};

// event logging (incl. secret logging)

// reference counting

// applications that have their own record layer can set this function to derive their own traffic
// keys from the traffic secret. The cipher-suite that is being associated to the connection can
// be obtained by calling the cipher function.
struct UpdateTrafficKey {
  virtual ~UpdateTrafficKey() = default;
  virtual int call(Ztls::Cxn *tls, int isEnc, size_t epoch, const void *secret) = 0;
};

// callback for every extension detected during decoding
struct OnExtension {
  virtual ~OnExtension() = default;
  virtual int call(Ztls::Cxn *tls, uint8_t hstype, uint16_t exttype, Bytes extdata) = 0;
};

struct DecompressCertificate {
  ZuSpan<const uint16_t>	supportedAlgorithms;

  virtual ~DecompressCertificate() = default;
  virtual int call(Ztls::Cxn *tls, uint16_t algorithm, Bytes output, Bytes input) = 0;
};

// ECH: creates the AEAD context to be used for "Open"-ing inner CH. Given `configId`, the
// callback looks up the ECH config and the corresponding private key, invokes `hpkeSetupBaseR`
// with provided `cipher`, `enc`, and `infoPrefix` (which will be "tls ech" || 00).
struct EchCreateOpener {
  virtual ~EchCreateOpener() = default;
  virtual AeadContext *call(
    const HpkeKem **kem, const HpkeSuite **cipher, Ztls::Cxn *tls, uint8_t configId,
    HpkeSuiteID cipherID, Bytes enc, Bytes infoPrefix) = 0;
};

// the configuration
struct RandomSource {
  virtual ~RandomSource() = default;
  virtual void fill(void *output, size_t length) = 0;
};

struct Context {
  // PRNG to be used
  RandomSource		*random;
  GetTime		*getTime;

  // list of supported key-exchange algorithms terminated by NULL
  const KeyExchangeAlgo	**keyExchanges;

  // supported cipher suites in preference order, indexed by wire ID and hash
  CipherSuites		cipherSuites;

  // list of certificates
  CertificateChain	certificates;

  // External pre-shared key used for mutual authentication. Unless when using PSK, all the fields
  // must be set to NULL / 0.
  struct PreSharedKey {
    Bytes		identity;
    Bytes		secret;

    // (mandatory) hash algorithm associated to the PSK; cipher-suites not sharing the same `const
    // HashAlgo` will be ignored
    const HashAlgo	*hash;
  } preSharedKey;

  // ECH
  struct Ech {
    struct Client {
      // list of HPKE symmetric cipher-suites (set to NULL to disable ECH altogether)
      const HpkeSuites	*ciphers;

      // KEMs being supported
      const HpkeKem	**kems;
    } client;
    struct Server {
      // callback that does ECDH key exchange and returns the AEAD context
      EchCreateOpener	*createOpener;

      // ECHConfigList to be sent to the client when there is mismatch (or when the client sends a
      // grease)
      Bytes		retryConfigs;
    } server;
  } ech;

  OnClientHello		*onClientHello;
  EmitCertificate	*emitCertificate;
  SignCertificate	*signCertificate;
  VerifyCertificate	*verifyCertificate;

  // lifetime of a session ticket (server-only)
  uint32_t		ticketLifetime;

  // maximum permitted size of early data (server-only)
  uint32_t		maxEarlyDataSize;

  // maximum size of the message buffer (default: 0 = unlimited = 3 + 2^24 bytes)
  size_t		maxBufferSize;

  // this field is obsolete and ignored
  const char		*hkdfLabelPrefix;

  // if set, psk handshakes use (ec)dhe
  unsigned		requireDheOnPsk:1;

  // if exporter master secrets should be recorded
  unsigned		useExporter:1;

  // if ChangeCipherSpec record should be sent during handshake. If the client sends CCS, the
  // server sends one in response
  // regardless of the value of this flag. See RFC 8446 Appendix D.3.
  unsigned		sendChangeCipherSpec:1;

  // if set, the server requests client certificates to authenticate the client
  unsigned		requireCliAuth:1;

  // if set, EOED will not be emitted or accepted
  unsigned		omitEndOfEarlyData:1;

  // This option turns on support for Raw Public Keys (RFC 7250).

  // When running as a client, this option instructs the client to request the server to send raw
  // public keys in place of X.509 certificate chain. The client should set its verifyCertificate
  // callback to one that is capable of validating the raw public key that will be sent by the
  // server.

  // When running as a server, this option instructs the server to only handle clients requesting
  // the use of raw public keys. If the client does not, the handshake is rejected. Note however
  // that the rejection happens only after the `onClientHello` callback is being called.
  // Therefore, applications can support both X.509 and raw public keys by swapping `Context` to
  // the correct one when that callback is being called (like handling swapping the contexts based
  // on the value of SNI).
  unsigned		useRawPublicKeys:1;

  // boolean indicating if the cipher-suite should be chosen based on server's preference
  unsigned		serverCipherPreference:1;

  // boolean indicating if ChaCha20-Poly1305 should be reprioritized to the top of the server
  // cipher list if a ChaCha20-Poly1305 cipher is at the top of the client cipher list
  unsigned		srvCipherChachaPriority:1;
  EncryptTicket		*encryptTicket;
  SaveTicket		*saveTicket;
  UpdateTrafficKey	*updateTrafficKey;
  DecompressCertificate	*decompressCertificate;
  OnExtension		*onExtension;

  // (optional) session ID Context to segment resumption
  struct TicketContext {
    uint8_t	bytes[Ztls_SHA256DigestSize];
    unsigned	isSet:1;
  } ticketContext;

  // (optional) list of CAs advertised to clients as supported in the CertificateRequest message;
  // each item must be DNs in DER format. The values are sent to the client only when
  // `Context::requireCliAuth` is set to true.
  ZuSpan<const Bytes>	clientCaNames;

  // (optional)
  struct TicketRequests {
    // Client: counts requested when saveTicket is installed.
    uint8_t	newSessionCount;
    uint8_t	resumptionCount;
    // Server: zero selects ZtlsMaxTickets.
    uint8_t	maxCount;
  } ticketRequests;
};

struct RawExtension {
  uint16_t	type;
  Bytes		data;
};

ZtEnumNS(Ztls_API, EarlyDataAcceptance, int8_t, Unknown, Rejected, Accepted);

// optional arguments to client-driven handshake
struct ExtensionCollector {
  virtual ~ExtensionCollector() = default;
  virtual bool collect(Ztls::Cxn *tls, uint16_t type) = 0;
  virtual int collected(Ztls::Cxn *tls, RawExtension *extensions) = 0;
};

struct HandshakeProps {
  struct Client {
    // list of protocols offered through ALPN
    ZuSpan<const Bytes>		negotiatedProtocols;

    // session ticket sent to the application via saveTicket callback
    Bytes			sessionTicket;

    // pointer to store the maximum size of early-data that can be sent immediately. If set to
    // non-NULL, the first call to handshake (or handle_message) will set *maxEarlyDataSize to the
    // value obtained from the session ticket, or to zero if early-data cannot be sent. If NULL,
    // early data will not be used.
    size_t			*maxEarlyDataSize;

    // If early-data has been accepted by peer, or if the state is still unknown. The state
    // changes anytime after handshake keys become available. Applications can peek the tri-state
    // variable every time it calls handshake or `handle_message` to determine the result at the
    // earliest moment. This is an output parameter.
    EarlyDataAcceptance::T	earlyDataAcceptance;

    // negotiate the key exchange method before sending keyShare
    unsigned			negotiatePreKeyex:1;

    // ECH
    struct Ech {
      // An ECH config offered by server e.g., by HTTPS RR. An empty config with a non-null data
      // pointer, a grease ECH will be sent, assuming that X25519-SHA256 KEM and
      // SHA256-AES-128-GCM HPKE cipher is available. If the data pointer is also null, ECH will
      // not be used at all, even if the context provided the ECH ciphers.
      Bytes	configs;

      // Receives the server retry configuration list on mismatch.
      EchBytes	*retryConfigs;
    } ech;
  } client;
  struct Server {
    // psk binder being selected (len is set to zero if none)
    struct SelectedPskBinder {
      uint8_t	base[Ztls_MaxDigestSize];
      size_t	len;
    } selectedPskBinder;

    // parameters related to use of the Cookie extension
    struct Cookie {
      // HMAC key to protect the integrity of the cookie. The key should be as long as the digest
      // size of the first ciphersuite specified in Context (i.e. the hash algorithm of the best
      // ciphersuite that can be chosen).
      const void	*key;

      // additional data to be used for verifying the cookie
      Bytes		additionalData;
    } cookie;

    // if HRR should always be sent
    unsigned	enforceRetry:1;

    // if retry should be stateless (cookie.key MUST be set when this option is used)
    unsigned	retryUsesCookie:1;
  } server;

  // an optional list of additional extensions to send either in CH or EE, terminated by type ==
  // UINT16_MAX
  RawExtension		*additionalExtensions;

  // an optional callback that returns a boolean value indicating if a particular extension should
  // be collected
  ExtensionCollector	*extensions;

  // an optional callback that reports the extensions being collected
};

// builds a new Bytes instance using the supplied parameters
static Bytes wireBytes(const void *p, size_t len);

// initializes a buffer, setting the default destination to the small buffer provided as the
// argument. `tx` is direction (0: receive-side, 1: transmit-side)
static void bufferInit(Buffer *buf, void *smallbuf, size_t inlineSize, int tx);

// initializes a receive-side buffer
static void bufferInitRx(Buffer *buf, void *smallbuf, size_t inlineSize);

// initializes a transmit-side buffer
static void bufferInitTx(Buffer *buf, void *smallbuf, size_t inlineSize);

// disposes a buffer, freeing resources allocated by the buffer itself (if any)
static void bufferDispose(Buffer *buf);

// internal
Ztls_Extern void bufferRelease(Buffer *buf);

// reserves space for additional amount of memory
Ztls_Extern int bufferRes(Buffer *buf, size_t delta, int tx);

// reserves space for additional amount of memory, requiring `buf->base` to follow specified
// alignment
Ztls_Extern int bufferResAligned(Buffer *buf, size_t delta, uint8_t alignBits, int tx);

// internal
Ztls_Extern int bufferPushv(Buffer *buf, const void *src, size_t len);
#define Ztls_BufferPushv(buf, src, len) \
  do { \
    if ((ret = bufferPushv((buf), (src), (len))) != 0) goto Exit; \
  } while (0)

#define Ztls_BufferPush_1(buf, ...) \
  do { \
    if ( \
      (ret = bufferPushv( \
	 (buf), (uint8_t[]){__VA_ARGS__}, sizeof((uint8_t[]){__VA_ARGS__}))) != 0 \
    ) \
      goto Exit; \
  } while (0)

#define Ztls_BufferPush16(buf, v) \
  do { \
    auto _v = ZuBE(uint16_t(v)); \
    Ztls_BufferPushv((buf), &_v, sizeof(_v)); \
  } while (0)

#define Ztls_BufferPush24(buf, v) \
  do { \
    uint32_t _v = (v); \
    Ztls_BufferPush_1(buf, (uint8_t)(_v >> 16), (uint8_t)(_v >> 8), (uint8_t)_v); \
  } while (0)

#define Ztls_BufferPush32(buf, v) \
  do { \
    auto _v = ZuBE(uint32_t(v)); \
    Ztls_BufferPushv((buf), &_v, sizeof(_v)); \
  } while (0)

#define Ztls_BufferPush64(buf, v) \
  do { \
    auto _v = ZuBE(uint64_t(v)); \
    Ztls_BufferPushv((buf), &_v, sizeof(_v)); \
  } while (0)

#define Ztls_BufferPushBlock(buf, _capacity, block) \
  do { \
    size_t capacity = (_capacity); \
    Ztls_BufferPushv((buf), "\0\0\0\0\0\0\0", capacity); \
    size_t bodyStart = (buf)->off; \
    do { \
      block \
    } while (0); \
    size_t bodySize = (buf)->off - bodyStart; \
    if (capacity < sizeof(size_t) && bodySize >= (size_t)1 << (capacity * 8)) { \
      ret = Ztls_ErrorBlockOverflow; \
      goto Exit; \
    } \
    for (; capacity != 0; --capacity) \
      (buf)->base[bodyStart - capacity] = (uint8_t)(bodySize >> (8 * (capacity - 1))); \
  } while (0)

#define Ztls_BufferPushMsgBody(buf, keySched, type, block) \
  do { \
    Buffer	*_buf = (buf); \
    KeySchedule *_key_sched = (keySched); \
    size_t	 messStart = _buf->off; \
    Ztls_BufferPush_1(_buf, (type)); \
    Ztls_BufferPushBlock(_buf, 3, block); \
    if (_key_sched != NULL) \
      keyScheduleUpdateHash(_key_sched, _buf->base + messStart, _buf->off - messStart, 0); \
  } while (0)

#define Ztls_PushMessage(emitter, keySched, type, block) \
  do { \
    MessageEmitter *_emitter = (emitter); \
    if ((ret = _emitter->beginMessage()) != 0) goto Exit; \
    Ztls_BufferPushMsgBody(_emitter->buf, (keySched), (type), block); \
    if ((ret = _emitter->commitMessage()) != 0) goto Exit; \
  } while (0)

Ztls_Extern int decode8(uint8_t *value, const uint8_t **src, const uint8_t *end);
Ztls_Extern int decode16(uint16_t *value, const uint8_t **src, const uint8_t *end);
Ztls_Extern int decode24(uint32_t *value, const uint8_t **src, const uint8_t *end);
Ztls_Extern int decode32(uint32_t *value, const uint8_t **src, const uint8_t *end);
Ztls_Extern int decode64(uint64_t *value, const uint8_t **src, const uint8_t *end);

#define Ztls_DecodeOpenBlock(src, end, capacity, block) \
  do { \
    size_t _capacity = (capacity); \
    if (_capacity > (size_t)(end - (src))) { \
      ret = Ztls_AlertDecodeError; \
      goto Exit; \
    } \
    size_t _block_size = 0; \
    do { \
      _block_size = _block_size << 8 | *(src)++; \
    } while (--_capacity != 0); \
    if (_block_size > (size_t)(end - (src))) { \
      ret = Ztls_AlertDecodeError; \
      goto Exit; \
    } \
    do { \
      const uint8_t *const end = (src) + _block_size; \
      do { \
	block \
      } while (0); \
      if ((src) != end) { \
	ret = Ztls_AlertDecodeError; \
	goto Exit; \
      } \
    } while (0); \
  } while (0)

#define Ztls_DecodeCheckBlockEnd(src, end) \
  do { \
    if ((src) != end) { \
      ret = Ztls_AlertDecodeError; \
      goto Exit; \
    } \
  } while (0);

#define Ztls_DecodeBlock(src, end, capacity, block) \
  do { \
    Ztls_DecodeOpenBlock((src), end, capacity, block); \
    Ztls_DecodeCheckBlockEnd((src), end); \
  } while (0)

// returns address of the crypto callbacks that the connection is using
Ztls_Extern Context *context(Ztls::Cxn *tls);

// updates the context of a connection. Can be called from `onClientHello` callback.
Ztls_Extern void context(Ztls::Cxn *tls, Context *ctx);

// returns the client-random
Ztls_Extern Bytes cliRandom(Ztls::Cxn *tls);

// returns the cipher-suite being used
Ztls_Extern const CipherSuite *cipher(Ztls::Cxn *tls);

// Returns the negotiated TLS 1.3 protocol identifier, 0x0304.
Ztls_Extern uint16_t protocolVersion(Ztls::Cxn *tls);

// Returns current state of traffic keys. The cipher-suite being used, as well as the length of
// the traffic keys, can be obtained via `cipher`. TODO: Even in case of offloading just the
// TX side, there should be API for handling key updates, sending Close aleart.
Ztls_Extern int trafficKeys(Ztls::Cxn *tls, int isEnc, uint8_t *key, uint8_t *iv, uint64_t *seq);

// returns the server-name (NULL if SNI is not used or failed to negotiate)
Ztls_Extern const char *serverName(Ztls::Cxn *tls);

// sets the server name associated with the TLS connection. On the client-side, the value is used
// for certificate validation. The value will be also sent as an SNI extension, if it looks like a
// DNS name. On the server-side, it can be called from onClientHello to indicate the acceptance of
// the SNI extension to the client.
Ztls_Extern int serverName(Ztls::Cxn *tls, ZuCSpan serverName);

// returns the negotiated protocol (or NULL)
Ztls_Extern const char *negotiatedProtocol(Ztls::Cxn *tls);

// sets the negotiated protocol.
Ztls_Extern int negotiatedProtocol(Ztls::Cxn *tls, ZuCSpan protocol);

// returns if the handshake has been completed
Ztls_Extern int handshakeComplete(Ztls::Cxn *tls);

// returns if a PSK (or PSK-DHE) handshake was performed
Ztls_Extern int isPskHandshake(Ztls::Cxn *tls);

// return if a ECH handshake was performed, as well as optionally the kem and cipher-suite being
// used
// FIXME: this function always return false when the TLS session is exported and imported
Ztls_Extern int isECHHandshake(
  Ztls::Cxn *tls, uint8_t *configId, const HpkeKem **kem, const HpkeSuite **cipher);

// returns a pointer to user data pointer (client is reponsible for freeing the associated data
// prior to calling free)
Ztls_Extern void **dataPtr(Ztls::Cxn *tls);

// proceeds with the handshake, optionally taking some input from peer. The function returns zero
// in case the handshake completed successfully. Ztls_ErrorInProgress is returned in case the
// handshake is incomplete. Otherwise, an error value is returned. The contents of sendbuf should
// be sent to the client, regardless of whether if an error is returned. inlen is an argument used
// for both input and output. As an input, the arguments takes the size of the data available as
// input. Upon return the value is updated to the number of bytes consumed by the handshake. In
// case the returned value is Ztls_ErrorInProgress there is a guarantee that all the input are
// consumed (i.e. the value of inlen does not change).
Ztls_Extern int handshake(
  Ztls::Cxn *tls, Buffer *sendbuf, const void *input, size_t *inlen, HandshakeProps *args);

ZtEnumNS(Ztls_API, RawHandshakeOp, int8_t, Start, Input, ResumeSignature);

using RawHandshakeOffsets = ZuArray<size_t, 5>;

struct RawHandshakeResult {
  int			status;
  size_t		length = 0;
  RawHandshakeOffsets	offsets{0, 0, 0, 0, 0};
  bool			keyUpdateRejected = false;
};

// The caller supplies ordered CRYPTO bytes at one epoch and owns the output span.
// Offsets delimit Initial, 0-RTT, Handshake, and Application, in that order.
// In-progress or suspended signing can still produce complete output ranges.
Ztls_Extern RawHandshakeResult rawHandshake(
  Ztls::Cxn *, HandshakeProps *, RawHandshakeOp::T, size_t inputEpoch, ZuBSpan input,
  Bytes output);
Ztls_Extern size_t rawReadEpoch(const Ztls::Cxn *);

// decrypts the first record within given buffer
Ztls_Extern int receive(Ztls::Cxn *tls, Buffer *plaintextbuf, const void *input, size_t *len);

// encrypts given buffer into multiple TLS records
Ztls_Extern int send(Ztls::Cxn *tls, Buffer *sendbuf, const void *input, size_t inlen);

// updates the send traffic key (as well as asks the peer to update)
Ztls_Extern int updateKey(Ztls::Cxn *tls, int reqUpdate);
// Mark the point where the Tx thread assumes ownership of the outbound members.
Ztls_Extern int handoffTx(Ztls::Cxn *tls);

// encrypts application data with the connection's outbound record protection
// All chunks output APIs: reserve failure leaves outbound state retryable. After a successful
// reserve, any emission or finish failure makes outbound state terminal: subsequent chunks and
// contiguous sends fail with Ztls_ErrorLibrary. Discard must retract the entire reservation;
// commit only advances private output, and finish publishes it. Never retry a terminal Tx.
// Handshake/receive callers must tear down the connection on errors other than their documented
// IN_PROGRESS / ASYNC_OPERATION continuations; prior records may already have been published.
Ztls_Extern int
txSendChunks(Ztls::Cxn *tls, Ztls::RecordSink *sink, const void *input, size_t length);
Ztls_Extern int txKeyChunks(Ztls::Cxn *tls, Ztls::RecordSink *sink, int reqUpdate);
Ztls_Extern int
txAlertChunks(Ztls::Cxn *tls, Ztls::RecordSink *sink, uint8_t level, uint8_t description);
Ztls_Extern int handshakeChunks(
  Ztls::Cxn *tls, Ztls::RecordSink *sink, const void *input, size_t *length,
  HandshakeProps *properties);

// Maximum private record output: the supported encrypted record bound minus its tag.
// Reserve the full encrypted bound to cover every supported tag size and TLS 1.3 inner padding.
// The application-visible extent is always limited to Ztls_RxAppCapacity.
#define Ztls_RxAppCapacity 16384
#define Ztls_RxRecordCapacity (16384 + 256)
// Receive one record incrementally, retaining provisional plaintext privately
// across fragments until authentication. The allocator remains live through
// commit or discard. The input length reports consumed bytes on return.
Ztls_Extern int rxStream(
  Ztls::Cxn *tls, Ztls::RecordSink *output, const void *input, size_t *length,
  HandshakeProps *properties);
Ztls_Extern int rxStreamCheck(Ztls::Cxn *tls);

// Return overhead for the connection's negotiated Tx record protection.
Ztls_Extern size_t txRecordOverhead(Ztls::Cxn *tls);

// consumes one pending peer request for a reciprocal KeyUpdate; returns one when consumed,
// otherwise zero
Ztls_Extern int takeKeyUpdateReq(Ztls::Cxn *tls);

// Returns if the context is a server context.
Ztls_Extern int isServer(Ztls::Cxn *tls);

// returns per-record overhead
Ztls_Extern size_t recordOverhead(Ztls::Cxn *tls);

// sends an alert
Ztls_Extern int sendAlert(Ztls::Cxn *tls, Buffer *sendbuf, uint8_t level, uint8_t description);

Ztls_Extern int exportSecret(
  Ztls::Cxn *tls, void *output, size_t outlen, const char *label, Bytes ctxValue, int isEarly);

// build the body of a Certificate message. Can be called with tls set to NULL in order to create
// a precompressed message.
Ztls_Extern int buildCertMessage(
  Buffer *buf, Bytes reqCtx, ZuSpan<const Bytes> certificates, Bytes ocspStatus);

Ztls_Extern int calcHash(const HashAlgo *algo, void *output, const void *src, size_t len);

Ztls_Extern HashContext *hmacCreate(const HashAlgo *algo, const void *key, size_t keySize);

Ztls_Extern int hkdfExtract(const HashAlgo *hash, void *output, Bytes salt, Bytes ikm);

Ztls_Extern int hkdfExpand(
  const HashAlgo *hash, void *output, size_t outlen, Bytes prk, Bytes info);

Ztls_Extern int hkdfExpandLabel(
  const HashAlgo *algo, void *output, size_t outlen, Bytes secret, const char *label,
  Bytes hashValue, const char *labelPrefix);

// instantiates a symmetric cipher
Ztls_Extern CipherContext *cipherNew(const CipherAlgo *algo, int isEnc, const void *key);

// destroys a symmetric cipher
Ztls_Extern void cipherFree(CipherContext *ctx);

// initializes the IV; this function must be called prior to calling cipherEncrypt
static void cipherInit(CipherContext *ctx, const void *iv);

// Encrypts given text. The function must be used in a way that the output length would be equal
// to the input length. For example, when using a block cipher in ECB mode, `len` must be a
// multiple of the block size when using a block cipher. The length can be of any value when using
// a stream cipher or a block cipher in CTR mode.
static void cipherEncrypt(CipherContext *ctx, void *output, const void *input, size_t len);

// instantiates an AEAD cipher given a secret, which is expanded using hkdf to a set of key and iv
// @param aead
// @param hash
// @param isEnc 1 if creating a context for encryption, 0 if creating a context for decryption
// @param secret the secret. The size must be the digest length of the hash algorithm
// @return pointer to an AEAD context if successful, otherwise NULL
Ztls_Extern AeadContext *aeadNew(
  const AeadAlgo *aead, const HashAlgo *hash, int isEnc, const void *secret,
  const char *labelPrefix);

// instantiates an AEAD cipher given key and iv
// @param aead
// @param isEnc 1 if creating a context for encryption, 0 if creating a context for decryption
// @return pointer to an AEAD context if successful, otherwise NULL
Ztls_Extern AeadContext *
aeadNew_(const AeadAlgo *aead, int isEnc, const void *key, const void *iv);

// destroys an AEAD cipher context
Ztls_Extern void aeadFree(AeadContext *ctx);

// Permutes the static IV by applying given bytes using bit-wise XOR. This API can be used for
// supplying nonces longer than 64- bits.
Ztls_Extern void aeadXorIV(AeadContext *ctx, const void *bytes, size_t len);
static void aeadGetIV(AeadContext *ctx, void *iv);
static void aeadSetIV(AeadContext *ctx, const void *iv);

// Encrypts one AEAD block, writing ciphertext followed by the AEAD tag. `output` must have room
// for `inlen + tagSize` bytes.
static size_t aeadEncrypt(
  AeadContext *ctx, void *output, const void *input, size_t inlen, uint64_t seq, const void *aad,
  size_t aadlen);

// Encrypts one AEAD block, as well as one block of ECB (for QUIC / DTLS packet number
// encryption). Depending on the AEAD engine being used, the two operations might run
// simultaneously.
static void aeadEncryptS(
  AeadContext *ctx, void *output, const void *input, size_t inlen, uint64_t seq, const void *aad,
  size_t aadlen, AeadSupplement *supp);

// Encrypts one AEAD block from read-only plaintext vectors, writing the logically concatenated
// ciphertext followed by the AEAD tag. `output` must have room for the sum of vector lengths plus
// `tagSize` bytes.
static void aeadEncryptV(
  AeadContext *ctx, void *output, const Bytes *input, size_t incnt, uint64_t seq, const void *aad,
  size_t aadlen);

// Encrypts one AEAD block from read-only plaintext vectors, and optionally computes a
// supplementary cipher block after AEAD output exists. Returns the sum of plaintext vector
// lengths plus the AEAD tag size. `output` must have room for that many bytes. `supp == NULL` is
// valid and skips supplementary encryption. `input` may be NULL when `incnt` is zero; zero-length
// vectors are accepted. For direct retained-buffer use, plaintext vector ranges must not overlap
// `output`. `supp->input` may point into `output`, including the tag, and is read after the
// ciphertext-plus-tag output has been written.
static size_t aeadEncryptVS(
  AeadContext *ctx, void *output, const Bytes *input, size_t incnt, uint64_t seq, const void *aad,
  size_t aadlen, AeadSupplement *supp);

// decrypts an AEAD record @return number of bytes emitted to output if successful, or SIZE_MAX if
// the input is invalid (e.g. broken MAC)
static size_t aeadDecrypt(
  AeadContext *ctx, void *output, const void *input, size_t inlen, uint64_t seq, const void *aad,
  size_t aadlen);

// internal
Ztls_Extern void aeadBuildIV(
  const AeadAlgo *algo, uint8_t *iv, const uint8_t *staticIv, uint64_t seq);

// internal; suitable only for backends whose `do_encrypt_v` consumes vector input without full
// plaintext flattening and returns after any required output-store visibility guarantees have
// been satisfied.

// internal
Ztls_Extern void keyScheduleUpdateHash(
  KeySchedule *sched, const uint8_t *msg, size_t msglen, int useOuter);

// checks if a server name is an IP address.
Ztls_Extern int serverNameIsIP(const char *name);

// encodes one ECH Config
Ztls_Extern int echEncodeConfig(
  Buffer *buf, uint8_t configId, const HpkeKem *kem, Bytes publicKey, const HpkeSuites &ciphers,
  uint8_t maxNameLength, const char *publicName);

// SetupBaseS function of RFC 9180. Given `kem`, `algo`, `info`, and receiver's public key,
// returns an ephemeral public key and an AEAD context used for encrypting data.
Ztls_Extern int hpkeSetupBaseS(
  const HpkeKem *kem, const HpkeSuite *cipher, KeyExchangeBytes *pkS, AeadContext **ctx,
  Bytes pkR, Bytes info);

// SetupBaseR function of RFC 9180. Given `kem`, `algo`, `info`, receiver's private key (`keyex`),
// and the esnder's public key, returns the AEAD context to be used for decrypting data.
Ztls_Extern int hpkeSetupBaseR(
  const HpkeKem *kem, const HpkeSuite *cipher, KeyExchangeContext *keyex, AeadContext **ctx,
  Bytes pkS, Bytes info);

// the default getTime callback
Ztls_Extern GetTime &getTime;

inline Bytes wireBytes(const void *data, size_t length)
{ return {const_cast<uint8_t *>(static_cast<const uint8_t *>(data)), length}; }

inline void bufferInit(Buffer *buf, void *smallbuf, size_t inlineSize, int tx)
{
  assert(tx == 0 || tx == 1);
  assert(smallbuf != NULL);
  buf->base = (uint8_t *)smallbuf;
  buf->capacity = inlineSize;
  buf->off = 0;
  buf->isAllocated = 0;
  buf->alignBits = 0;
  buf->tx = (uint8_t)tx;
  buf->bounded = false;
  buf->recordSink = NULL;
}

inline void bufferInitRx(Buffer *buf, void *smallbuf, size_t inlineSize)
{ bufferInit(buf, smallbuf, inlineSize, 0); }

inline void bufferInitTx(Buffer *buf, void *smallbuf, size_t inlineSize)
{ bufferInit(buf, smallbuf, inlineSize, 1); }

inline void bufferDispose(Buffer *buf)
{
  bufferRelease(buf);
  *buf = Buffer{};
}

inline void cipherInit(CipherContext *ctx, const void *iv) { ctx->init(iv); }

inline void cipherEncrypt(CipherContext *ctx, void *output, const void *input, size_t len)
{ ctx->transform(output, input, len); }

inline void aeadGetIV(AeadContext *ctx, void *iv) { ctx->getIV(iv); }

inline void aeadSetIV(AeadContext *ctx, const void *iv) { ctx->setIV(iv); }

inline size_t aeadEncrypt(
  AeadContext *ctx, void *output, const void *input, size_t inlen, uint64_t seq, const void *aad,
  size_t aadlen)
{
  ctx->encrypt(output, input, inlen, seq, aad, aadlen, NULL);
  return inlen + ctx->algo->tagSize;
}

inline void aeadEncryptS(
  AeadContext *ctx, void *output, const void *input, size_t inlen, uint64_t seq, const void *aad,
  size_t aadlen, AeadSupplement *supp)
{ ctx->encrypt(output, input, inlen, seq, aad, aadlen, supp); }

inline void aeadEncryptV(
  AeadContext *ctx, void *output, const Bytes *input, size_t incnt, uint64_t seq, const void *aad,
  size_t aadlen)
{ ctx->encryptV(output, input, incnt, seq, aad, aadlen); }

static inline size_t aeadVecSize(const Bytes *input, size_t incnt)
{
  size_t inlen = 0;

  for (size_t i = 0; i != incnt; ++i) {
    if (input[i].length() > SIZE_MAX - inlen) abort();
    inlen += input[i].length();
  }
  return inlen;
}

inline size_t aeadEncryptVS(
  AeadContext *ctx, void *output, const Bytes *input, size_t incnt, uint64_t seq, const void *aad,
  size_t aadlen, AeadSupplement *supp)
{
  size_t inlen = aeadVecSize(input, incnt);

  if (ctx->algo->tagSize > SIZE_MAX - inlen) abort();
  ctx->encryptVS(output, input, incnt, seq, aad, aadlen, supp);
  return inlen + ctx->algo->tagSize;
}

static inline void aeadSupplement(AeadSupplement *supp)
{
  if (supp != NULL) {
    cipherInit(supp->ctx, supp->input);
    memset(supp->output, 0, sizeof(supp->output));
    cipherEncrypt(supp->ctx, supp->output, supp->output, sizeof(supp->output));
  }
}

inline void AeadContext::encrypt(
  void *output, const void *input, size_t inlen, uint64_t seq, const void *aad, size_t aadlen,
  AeadSupplement *supp)
{
  assert(isEnc);
  Bytes vector = wireBytes(input, inlen);
  encryptV(output, &vector, 1, seq, aad, aadlen);
  aeadSupplement(supp);
}

inline void AeadContext::encryptVS(
  void *output, const Bytes *input, size_t incnt, uint64_t seq, const void *aad, size_t aadlen,
  AeadSupplement *supp)
{
  assert(isEnc);
  encryptV(output, input, incnt, seq, aad, aadlen);
  aeadSupplement(supp);
}

inline size_t aeadDecrypt(
  AeadContext *ctx, void *output, const void *input, size_t inlen, uint64_t seq, const void *aad,
  size_t aadlen)
{ return ctx->decrypt(output, input, inlen, seq, aad, aadlen); }

} // namespace Ztls_

#endif
