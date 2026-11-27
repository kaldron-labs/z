//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// Copyright (c) 2022 Fastly, Kazuho Oku
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
#include <stdlib.h>


#include <zlib/ZtlsEngine.hh>

namespace Ztls_ {

static constexpr char hpkeV1Label[] = "HPKE-v1";

static int buildSuiteID(Buffer *buf, const HpkeKem *kem, const HpkeSuite *cipher)
{
  int ret;

  if (cipher == NULL) {
    Ztls_BufferPushv(buf, "KEM", 3);
    Ztls_BufferPush16(buf, kem->id);
  } else {
    Ztls_BufferPushv(buf, "HPKE", 4);
    Ztls_BufferPush16(buf, kem->id);
    Ztls_BufferPush16(buf, cipher->id.kdf);
    Ztls_BufferPush16(buf, cipher->id.aead);
  }

  ret = 0;

Exit:
  return ret;
}

static int labeledExtract(
  const HpkeKem *kem, const HpkeSuite *cipher, void *output, Bytes salt, const char *label,
  Bytes ikm)
{
  Buffer  labeledIkm;
  uint8_t labeledIkmInline[64];
  int	  ret;

  bufferInitTx(&labeledIkm, labeledIkmInline, sizeof(labeledIkmInline));

  Ztls_BufferPushv(&labeledIkm, hpkeV1Label, (sizeof(hpkeV1Label) - 1));
  if ((ret = buildSuiteID(&labeledIkm, kem, cipher)) != 0) goto Exit;
  Ztls_BufferPushv(&labeledIkm, label, strlen(label));
  Ztls_BufferPushv(&labeledIkm, ikm.data(), ikm.length());

  ret = hkdfExtract(
    cipher != NULL ? cipher->hash : kem->hash, output, salt,
    wireBytes(labeledIkm.base, labeledIkm.off));

Exit:
  bufferDispose(&labeledIkm);
  return ret;
}

static int labeledExpand(
  const HpkeKem *kem, const HpkeSuite *cipher, void *output, size_t outlen, Bytes prk,
  const char *label, Bytes info)
{
  Buffer  labeledInfo;
  uint8_t labeledInfoInline[64];
  int	  ret;

  assert(outlen < UINT16_MAX);

  bufferInitTx(&labeledInfo, labeledInfoInline, sizeof(labeledInfoInline));

  Ztls_BufferPush16(&labeledInfo, (uint16_t)outlen);
  Ztls_BufferPushv(&labeledInfo, hpkeV1Label, (sizeof(hpkeV1Label) - 1));
  if ((ret = buildSuiteID(&labeledInfo, kem, cipher)) != 0) goto Exit;
  Ztls_BufferPushv(&labeledInfo, label, strlen(label));
  Ztls_BufferPushv(&labeledInfo, info.data(), info.length());

  ret = hkdfExpand(
    cipher != NULL ? cipher->hash : kem->hash, output, outlen, prk,
    wireBytes(labeledInfo.base, labeledInfo.off));

Exit:
  bufferDispose(&labeledInfo);
  return ret;
}

static int extractAndExpand(
  const HpkeKem *kem, void *secret, size_t secretLen, Bytes pkS, Bytes pkR, Bytes dh)
{
  Buffer  kemCtx;
  uint8_t kemCtxInline[128], eaePrk[Ztls_MaxDigestSize];
  int	  ret;

  bufferInitTx(&kemCtx, kemCtxInline, sizeof(kemCtxInline));

  Ztls_BufferPushv(&kemCtx, pkS.data(), pkS.length());
  Ztls_BufferPushv(&kemCtx, pkR.data(), pkR.length());

  if ((ret = labeledExtract(kem, NULL, eaePrk, wireBytes("", 0), "eae_prk", dh)) != 0) goto Exit;
  if ((ret = labeledExpand(
	 kem, NULL, secret, secretLen, wireBytes(eaePrk, kem->hash->digestSize), "shared_secret",
	 wireBytes(kemCtx.base, kemCtx.off))) != 0)
    goto Exit;

Exit:
  bufferDispose(&kemCtx);
  ZuClear(eaePrk, sizeof(eaePrk));
  return ret;
}

static int dhDerive(const HpkeKem *kem, void *secret, Bytes pkS, Bytes pkR, Bytes dh)
{ return extractAndExpand(kem, secret, kem->hash->digestSize, pkS, pkR, dh); }

static int dhEncap(const HpkeKem *kem, void *secret, KeyExchangeBytes *pkS, Bytes pkR)
{
  KeyExchangeBytes dh;
  int		   ret;

  pkS->null();

  if ((ret = kem->keyex->exchange(pkS, &dh, pkR)) != 0) {
    assert(!pkS->length());
    assert(!dh.length());
    goto Exit;
  }

  if ((ret = dhDerive(
	 kem, secret, wireBytes(pkS->data(), pkS->length()), pkR,
	 wireBytes(dh.data(), dh.length()))) != 0)
    goto Exit;

Exit:
  if (ret != 0) pkS->null();
  return ret;
}

static int
dhDecap(const HpkeKem *kem, void *secret, KeyExchangeContext *keyex, Bytes pkS, Bytes pkR)
{
  KeyExchangeBytes dh;
  int		   ret;

  if ((ret = keyex->exchange(&dh, pkS)) != 0) {
    assert(!dh.length());
    goto Exit;
  }

  if ((ret = dhDerive(kem, secret, pkS, pkR, wireBytes(dh.data(), dh.length()))) != 0) goto Exit;

Exit:
  return ret;
}

static int keySchedule(
  const HpkeKem *kem, const HpkeSuite *cipher, AeadContext **ctx, int isEnc,
  const void *sharedSecret, Bytes info)
{
  Buffer  keyScheduleCtx;
  uint8_t keySchedCtxInline[128], secret[Ztls_MaxDigestSize], key[Ztls_MaxSecretSize],
    baseNonce[Ztls_MaxIVSize];
  int ret;

  *ctx = NULL;

  bufferInit(&keyScheduleCtx, keySchedCtxInline, sizeof(keySchedCtxInline), isEnc != 0);

  // keyScheduleCtx = concat(mode, LabeledExtract("", "psk_id_hash", psk_id),
  // LabeledExtract("", "info_hash", info))
  Ztls_BufferPush_1(&keyScheduleCtx, Ztls_HPKEModeBase);
  if ((ret = bufferRes(&keyScheduleCtx, cipher->hash->digestSize, isEnc != 0)) != 0 ||
    (ret = labeledExtract(
       kem, cipher, keyScheduleCtx.base + keyScheduleCtx.off, wireBytes(NULL, 0), "psk_id_hash",
       wireBytes(NULL, 0))) != 0)
    goto Exit;
  keyScheduleCtx.off += cipher->hash->digestSize;
  if ((ret = bufferRes(&keyScheduleCtx, cipher->hash->digestSize, isEnc != 0)) != 0 ||
    (ret = labeledExtract(
       kem, cipher, keyScheduleCtx.base + keyScheduleCtx.off, wireBytes(NULL, 0), "info_hash",
       info)) != 0)
    goto Exit;
  keyScheduleCtx.off += cipher->hash->digestSize;

  //  secret = LabeledExtract(sharedSecret, "secret", psk)
  if ((ret = labeledExtract(
	 kem, cipher, secret, wireBytes(sharedSecret, kem->hash->digestSize), "secret",
	 wireBytes("", 0))) != 0)
    goto Exit;

  // key, baseNonce
  if ((ret = labeledExpand(
	 kem, cipher, key, cipher->aead->keySize, wireBytes(secret, cipher->hash->digestSize),
	 "key", wireBytes(keyScheduleCtx.base, keyScheduleCtx.off))) != 0)
    goto Exit;
  if (
    (ret = labeledExpand(
       kem, cipher, baseNonce, cipher->aead->ivSize, wireBytes(secret, cipher->hash->digestSize),
       "base_nonce", wireBytes(keyScheduleCtx.base, keyScheduleCtx.off))) != 0)
    goto Exit;

  *ctx = aeadNew_(cipher->aead, isEnc, key, baseNonce);

Exit:
  bufferDispose(&keyScheduleCtx);
  ZuClear(secret, sizeof(secret));
  ZuClear(key, sizeof(key));
  ZuClear(baseNonce, sizeof(baseNonce));
  return ret;
}

int hpkeSetupBaseS(
  const HpkeKem *kem, const HpkeSuite *cipher, KeyExchangeBytes *pkS, AeadContext **ctx,
  Bytes pkR, Bytes info)
{
  uint8_t secret[Ztls_MaxDigestSize];
  int	  ret;

  pkS->null();

  if ((ret = dhEncap(kem, secret, pkS, pkR)) != 0) goto Exit;

  if ((ret = keySchedule(kem, cipher, ctx, 1, secret, info)) != 0) goto Exit;

Exit:
  if (ret != 0) pkS->null();
  ZuClear(secret, sizeof(secret));
  return ret;
}

int hpkeSetupBaseR(
  const HpkeKem *kem, const HpkeSuite *cipher, KeyExchangeContext *keyex, AeadContext **ctx,
  Bytes pkS, Bytes info)
{
  uint8_t secret[Ztls_MaxDigestSize];
  int	  ret;

  if ((ret = dhDecap(kem, secret, keyex, pkS, keyex->pubkey)) != 0) goto Exit;

  if ((ret = keySchedule(kem, cipher, ctx, 0, secret, info)) != 0) goto Exit;

Exit:
  ZuClear(secret, sizeof(secret));
  return ret;
}

} // namespace Ztls_
