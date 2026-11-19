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
#if Ztls_USE_BROTLI

#include <assert.h>
#include <stdlib.h>
#include "brotli/decode.h"
#include "brotli/encode.h"
#include "certificate_compression.h"

namespace Ztls_ {

static inline int decompress_certificate_impl(decompress_certificate_t *self, Ztls::Cxn *tls, uint16_t algorithm, Bytes output,
                                         Bytes input)
{
    size_t decoded_size = output.length();
    if (algorithm != Ztls_CERTIFICATE_COMPRESSION_ALGORITHM_BROTLI)
        goto Fail;
    if (BrotliDecoderDecompress(input.length(), input.data(), &decoded_size, output.data()) != BROTLI_DECODER_RESULT_SUCCESS)
        goto Fail;

    if (decoded_size != output.length())
        goto Fail;

    return 0;
Fail:
    return Ztls_ALERT_BAD_CERTIFICATE;
}

static const uint16_t algorithms[] = {Ztls_CERTIFICATE_COMPRESSION_ALGORITHM_BROTLI, UINT16_MAX};

decompress_certificate_t decompress_certificate = {algorithms, decompress_certificate_impl};

static int emit_compressed_certificate(emit_certificate_t *_self, Ztls::Cxn *tls, message_emitter_t *emitter,
                                       key_schedule_t *key_sched, Bytes context, int push_status_request,
                                       const uint16_t *compress_algos, size_t num_compress_algos)
{
    emit_compressed_certificate_t *self = reinterpret_cast<emit_compressed_certificate_t *>(_self);
    struct st_compressed_certificate_entry_t *entry;
    int ret;

    assert(context.length() == 0 || !"precompressed mode can only be used for server certificates");

    for (size_t i = 0; i != num_compress_algos; ++i) {
        if (compress_algos[i] == Ztls_CERTIFICATE_COMPRESSION_ALGORITHM_BROTLI)
            goto FoundBrotli;
    }
    /* brotli not found, delegate to the core */
    ret = Ztls_ERROR_DELEGATE;
    goto Exit;

FoundBrotli:
    entry = &self->without_ocsp_status;
    if (push_status_request && self->with_ocsp_status.uncompressed_length != 0)
        entry = &self->with_ocsp_status;

    Ztls_push_message(emitter, key_sched, Ztls_HANDSHAKE_TYPE_COMPRESSED_CERTIFICATE, {
        Ztls_buffer_push16(emitter->buf, Ztls_CERTIFICATE_COMPRESSION_ALGORITHM_BROTLI);
        Ztls_buffer_push24(emitter->buf, entry->uncompressed_length);
        Ztls_buffer_push_block(emitter->buf, 3, { Ztls_buffer_pushv(emitter->buf, entry->bytes.data(), entry->bytes.length()); });
    });

    ret = 0;

Exit:
    return ret;
}

static int build_compressed(struct st_compressed_certificate_entry_t *entry, Bytes *certificates,
                            size_t num_certificates, Bytes ocsp_status)
{
    buffer_t uncompressed;
    size_t size;
    int ret;

    buffer_init_tx(&uncompressed, const_cast<char *>(""), 0);

    /* build uncompressed */
    if ((ret = build_certificate_message(&uncompressed, wire_bytes(NULL, 0), certificates, num_certificates,
                                              ocsp_status)) != 0)
        goto Exit;
    entry->uncompressed_length = (uint32_t)uncompressed.off;

    /* compress */
    size = uncompressed.off - 1;
    entry->bytes.length(size ? size : 1);
    if (BrotliEncoderCompress(BROTLI_MAX_QUALITY, BROTLI_DEFAULT_WINDOW, BROTLI_MODE_GENERIC, uncompressed.off, uncompressed.base,
                              &size, entry->bytes.data()) != BROTLI_TRUE) {
        ret = Ztls_ERROR_COMPRESSION_FAILURE;
        goto Exit;
    }

    entry->bytes.length(size);
    ret = 0;

Exit:
    if (ret != 0)
        *entry = {};
    buffer_dispose(&uncompressed);
    return ret;
}

int init_compressed_certificate(emit_compressed_certificate_t *self, Bytes *certificates, size_t num_certificates,
                                     Bytes ocsp_status)
{
    int ret;

    *self = (emit_compressed_certificate_t){{emit_compressed_certificate}, Ztls_CERTIFICATE_COMPRESSION_ALGORITHM_BROTLI};

    /* build entries */
    if ((ret = build_compressed(&self->without_ocsp_status, certificates, num_certificates, wire_bytes(NULL, 0))) != 0)
        goto Exit;
    if (ocsp_status.length() != 0) {
        if ((ret = build_compressed(&self->with_ocsp_status, certificates, num_certificates, ocsp_status)) != 0)
            goto Exit;
    }

    ret = 0;

Exit:
    if (ret != 0)
        dispose_compressed_certificate(self);
    return ret;
}

void dispose_compressed_certificate(emit_compressed_certificate_t *self)
{
    self->with_ocsp_status = {};
    self->without_ocsp_status = {};
}

} // Ztls_

#endif
