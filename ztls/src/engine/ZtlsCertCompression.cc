//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// Copyright (c) 2018 Fastly
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
#include "ZtlsCertCompression.hh"

#if ZtlsUseBrotli

#include <assert.h>
#include <stdlib.h>

#include "brotli/decode.h"
#include "brotli/encode.h"

namespace Ztls_ {

struct BrotliDecompress : DecompressCertificate {
  BrotliDecompress();
  int call(Ztls::Cxn *tls, uint16_t algorithm, Bytes output, Bytes input) override
  {
    size_t decodedSize = output.length();
    if (algorithm != Ztls_CertCompAlgoBrotli) goto Fail;
    if (BrotliDecoderDecompress(input.length(), input.data(), &decodedSize, output.data()) !=
      BROTLI_DECODER_RESULT_SUCCESS)
      goto Fail;

    if (decodedSize != output.length()) goto Fail;

    return 0;
Fail:
    return Ztls_AlertBadCert;
  }
};

static const uint16_t algorithms[] = {Ztls_CertCompAlgoBrotli};

BrotliDecompress::BrotliDecompress() { supportedAlgorithms = algorithms; }
static BrotliDecompress brotliDecompress;
DecompressCertificate  &decompressCertificate = brotliDecompress;

int EmitCompressedCert::call(
  Ztls::Cxn *tls, MessageEmitter *emitter, KeySchedule *keySched, Bytes context,
  int pushStatusReq, ZuSpan<const uint16_t> compressAlgos)
{
  auto		       self = this;
  CompressedCertEntry *entry;
  int		       ret;

  assert(context.length() == 0 || !"precompressed mode can only be used for server certificates");

  for (auto algorithm : compressAlgos) {
    if (algorithm == Ztls_CertCompAlgoBrotli) goto FoundBrotli;
  }
  // brotli not found, delegate to the core
  ret = Ztls_ErrorDelegate;
  goto Exit;

FoundBrotli:
  entry = &self->withoutOcsp;
  if (pushStatusReq && self->withOcsp.uncompressedLength != 0) entry = &self->withOcsp;

  Ztls_PushMessage(emitter, keySched, Ztls_HsTypeCompressedCert, {
    Ztls_BufferPush16(emitter->buf, Ztls_CertCompAlgoBrotli);
    Ztls_BufferPush24(emitter->buf, entry->uncompressedLength);
    Ztls_BufferPushBlock(emitter->buf, 3, {
      Ztls_BufferPushv(emitter->buf, entry->bytes.data(), entry->bytes.length());
    });
  });

  ret = 0;

Exit:
  return ret;
}

static int
buildCompressed(CompressedCertEntry *entry, ZuSpan<const Bytes> certificates, Bytes ocspStatus)
{
  Buffer uncompressed;
  size_t size;
  int	 ret;

  bufferInitTx(&uncompressed, const_cast<char *>(""), 0);

  // build uncompressed
  if ((ret = buildCertMessage(&uncompressed, wireBytes(NULL, 0), certificates, ocspStatus)) != 0)
    goto Exit;
  entry->uncompressedLength = (uint32_t)uncompressed.off;

  // compress
  size = uncompressed.off - 1;
  entry->bytes.length(size ? size : 1);
  if (BrotliEncoderCompress(
	BROTLI_MAX_QUALITY, BROTLI_DEFAULT_WINDOW, BROTLI_MODE_GENERIC, uncompressed.off,
	uncompressed.base, &size, entry->bytes.data()) != BROTLI_TRUE) {
    ret = Ztls_ErrorCompFailure;
    goto Exit;
  }

  entry->bytes.length(size);
  ret = 0;

Exit:
  if (ret != 0) *entry = {};
  bufferDispose(&uncompressed);
  return ret;
}

int initCompressedCert(
  EmitCompressedCert *self, ZuSpan<const Bytes> certificates, Bytes ocspStatus)
{
  int ret;

  disposeCompressedCert(self);
  self->algo = Ztls_CertCompAlgoBrotli;

  // build entries
  if ((ret = buildCompressed(&self->withoutOcsp, certificates, wireBytes(NULL, 0))) != 0)
    goto Exit;
  if (ocspStatus.length() != 0) {
    if ((ret = buildCompressed(&self->withOcsp, certificates, ocspStatus)) != 0) goto Exit;
  }

  ret = 0;

Exit:
  if (ret != 0) disposeCompressedCert(self);
  return ret;
}

void disposeCompressedCert(EmitCompressedCert *self)
{
  self->withOcsp = {};
  self->withoutOcsp = {};
}

} // namespace Ztls_

#endif
