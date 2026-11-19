/*
 * Copyright (c) 2018 Fastly
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */
#ifndef ZtlsCertificateCompression_HH
#define ZtlsCertificateCompression_HH

#include <zlib/ZtlsEngine.hh>

namespace Ztls_ {

#define Ztls_CERTIFICATE_COMPRESSION_ALGORITHM_GZIP 1
#define Ztls_CERTIFICATE_COMPRESSION_ALGORITHM_BROTLI 2

ZuDerive(CompressedCertificate, (ZtBArray<ZtArrayHeapID<"Ztls.Engine.CompressedCertificate">>));

struct st_compressed_certificate_entry_t {
    uint32_t uncompressed_length;
    CompressedCertificate bytes;
};

typedef struct st_emit_compressed_certificate_t {
    emit_certificate_t super;
    uint16_t algo;
    st_compressed_certificate_entry_t with_ocsp_status, without_ocsp_status;
} emit_compressed_certificate_t;

extern decompress_certificate_t decompress_certificate;

/**
 * initializes a certificate emitter that precompresses a certificate chain (and ocsp status)
 */
int init_compressed_certificate(emit_compressed_certificate_t *ecc, Bytes *certificates, size_t num_certificates,
                                     Bytes ocsp_status);
/**
 *
 */
void dispose_compressed_certificate(emit_compressed_certificate_t *ecc);

} // Ztls_

#endif
