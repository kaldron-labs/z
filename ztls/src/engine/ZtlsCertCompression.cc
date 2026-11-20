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
    size_t decoded_size = output.length();
    if (algorithm != Ztls_CertCompAlgoBrotli) goto Fail;
    if (BrotliDecoderDecompress(input.length(), input.data(), &decoded_size, output.data()) !=
      BROTLI_DECODER_RESULT_SUCCESS)
      goto Fail;

    if (decoded_size != output.length()) goto Fail;

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
  Ztls::Cxn *tls, MessageEmitter *emitter, KeySchedule *key_sched, Bytes context,
  int push_status_request, ZuSpan<const uint16_t> compress_algos)
{
  auto		       self = this;
  CompressedCertEntry *entry;
  int		       ret;

  assert(context.length() == 0 || !"precompressed mode can only be used for server certificates");

  for (auto algorithm : compress_algos) {
    if (algorithm == Ztls_CertCompAlgoBrotli) goto FoundBrotli;
  }
  // brotli not found, delegate to the core
  ret = Ztls_ErrorDelegate;
  goto Exit;

FoundBrotli:
  entry = &self->withoutOcsp;
  if (push_status_request && self->withOcsp.uncompressedLength != 0) entry = &self->withOcsp;

  Ztls_PushMessage(emitter, key_sched, Ztls_HsTypeCompressedCert, {
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
build_compressed(CompressedCertEntry *entry, ZuSpan<const Bytes> certificates, Bytes ocsp_status)
{
  Buffer uncompressed;
  size_t size;
  int	 ret;

  buffer_init_tx(&uncompressed, const_cast<char *>(""), 0);

  // build uncompressed
  if ((ret = build_certificate_message(
	 &uncompressed, wire_bytes(NULL, 0), certificates, ocsp_status)) != 0)
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
  buffer_dispose(&uncompressed);
  return ret;
}

int init_compressed_certificate(
  EmitCompressedCert *self, ZuSpan<const Bytes> certificates, Bytes ocsp_status)
{
  int ret;

  dispose_compressed_certificate(self);
  self->algo = Ztls_CertCompAlgoBrotli;

  // build entries
  if ((ret = build_compressed(&self->withoutOcsp, certificates, wire_bytes(NULL, 0))) != 0)
    goto Exit;
  if (ocsp_status.length() != 0) {
    if ((ret = build_compressed(&self->withOcsp, certificates, ocsp_status)) != 0) goto Exit;
  }

  ret = 0;

Exit:
  if (ret != 0) dispose_compressed_certificate(self);
  return ret;
}

void dispose_compressed_certificate(EmitCompressedCert *self)
{
  self->withOcsp = {};
  self->withoutOcsp = {};
}

} // namespace Ztls_

#endif
