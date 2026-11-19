/*
 * Copyright (c) 2022 Fastly, Kazuho Oku
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
#include <stdlib.h>
#include <zlib/ZtlsEngine.hh>

namespace Ztls_ {

#define HPKE_V1_LABEL "HPKE-v1"

static int build_suite_id(buffer_t *buf, hpke_kem_t *kem, hpke_cipher_suite_t *cipher)
{
    int ret;

    if (cipher == NULL) {
        Ztls_buffer_pushv(buf, "KEM", 3);
        Ztls_buffer_push16(buf, kem->id);
    } else {
        Ztls_buffer_pushv(buf, "HPKE", 4);
        Ztls_buffer_push16(buf, kem->id);
        Ztls_buffer_push16(buf, cipher->id.kdf);
        Ztls_buffer_push16(buf, cipher->id.aead);
    }

    ret = 0;

Exit:
    return ret;
}

static int labeled_extract(hpke_kem_t *kem, hpke_cipher_suite_t *cipher, void *output, Bytes salt,
                           const char *label, Bytes ikm)
{
    buffer_t labeled_ikm;
    uint8_t labeled_ikm_smallbuf[64];
    int ret;

    buffer_init_tx(&labeled_ikm, labeled_ikm_smallbuf, sizeof(labeled_ikm_smallbuf));

    Ztls_buffer_pushv(&labeled_ikm, HPKE_V1_LABEL, strlen(HPKE_V1_LABEL));
    if ((ret = build_suite_id(&labeled_ikm, kem, cipher)) != 0)
        goto Exit;
    Ztls_buffer_pushv(&labeled_ikm, label, strlen(label));
    Ztls_buffer_pushv(&labeled_ikm, ikm.data(), ikm.length());

    ret = hkdf_extract(cipher != NULL ? cipher->hash : kem->hash, output, salt,
                            wire_bytes(labeled_ikm.base, labeled_ikm.off));

Exit:
    buffer_dispose(&labeled_ikm);
    return ret;
}

static int labeled_expand(hpke_kem_t *kem, hpke_cipher_suite_t *cipher, void *output, size_t outlen, Bytes prk,
                          const char *label, Bytes info)
{
    buffer_t labeled_info;
    uint8_t labeled_info_smallbuf[64];
    int ret;

    assert(outlen < UINT16_MAX);

    buffer_init_tx(&labeled_info, labeled_info_smallbuf, sizeof(labeled_info_smallbuf));

    Ztls_buffer_push16(&labeled_info, (uint16_t)outlen);
    Ztls_buffer_pushv(&labeled_info, HPKE_V1_LABEL, strlen(HPKE_V1_LABEL));
    if ((ret = build_suite_id(&labeled_info, kem, cipher)) != 0)
        goto Exit;
    Ztls_buffer_pushv(&labeled_info, label, strlen(label));
    Ztls_buffer_pushv(&labeled_info, info.data(), info.length());

    ret = hkdf_expand(cipher != NULL ? cipher->hash : kem->hash, output, outlen, prk,
                           wire_bytes(labeled_info.base, labeled_info.off));

Exit:
    buffer_dispose(&labeled_info);
    return ret;
}

static int extract_and_expand(hpke_kem_t *kem, void *secret, size_t secret_len, Bytes pk_s, Bytes pk_r,
                              Bytes dh)
{
    buffer_t kem_context;
    uint8_t kem_context_smallbuf[128], eae_prk[Ztls_MAX_DIGEST_SIZE];
    int ret;

    buffer_init_tx(&kem_context, kem_context_smallbuf, sizeof(kem_context_smallbuf));

    Ztls_buffer_pushv(&kem_context, pk_s.data(), pk_s.length());
    Ztls_buffer_pushv(&kem_context, pk_r.data(), pk_r.length());

    if ((ret = labeled_extract(kem, NULL, eae_prk, wire_bytes("", 0), "eae_prk", dh)) != 0)
        goto Exit;
    if ((ret = labeled_expand(kem, NULL, secret, secret_len, wire_bytes(eae_prk, kem->hash->digest_size), "shared_secret",
                              wire_bytes(kem_context.base, kem_context.off))) != 0)
        goto Exit;

Exit:
    buffer_dispose(&kem_context);
    ZuClear(eae_prk, sizeof(eae_prk));
    return ret;
}

static int dh_derive(hpke_kem_t *kem, void *secret, Bytes pk_s, Bytes pk_r, Bytes dh)
{
    return extract_and_expand(kem, secret, kem->hash->digest_size, pk_s, pk_r, dh);
}

static int dh_encap(hpke_kem_t *kem, void *secret, KeyExchangeBytes *pk_s, Bytes pk_r)
{
    KeyExchangeBytes dh;
    int ret;

    pk_s->null();

    if ((ret = kem->keyex->exchange(kem->keyex, pk_s, &dh, pk_r)) != 0) {
        assert(!pk_s->length());
        assert(!dh.length());
        goto Exit;
    }

    if ((ret = dh_derive(kem, secret, wire_bytes(pk_s->data(), pk_s->length()), pk_r, wire_bytes(dh.data(), dh.length()))) != 0)
        goto Exit;

Exit:
    if (ret != 0)
        pk_s->null();
    return ret;
}

static int dh_decap(hpke_kem_t *kem, void *secret, key_exchange_context_t *keyex, Bytes pk_s, Bytes pk_r)
{
    KeyExchangeBytes dh;
    int ret;

    if ((ret = keyex->on_exchange(&keyex, 0, &dh, pk_s)) != 0) {
        assert(!dh.length());
        goto Exit;
    }

    if ((ret = dh_derive(kem, secret, pk_s, pk_r, wire_bytes(dh.data(), dh.length()))) != 0)
        goto Exit;

Exit:
    return ret;
}

static int key_schedule(hpke_kem_t *kem, hpke_cipher_suite_t *cipher, aead_context_t **ctx, int is_enc,
                        const void *shared_secret, Bytes info)
{
    buffer_t key_schedule_context;
    uint8_t key_schedule_context_smallbuf[128], secret[Ztls_MAX_DIGEST_SIZE], key[Ztls_MAX_SECRET_SIZE],
        base_nonce[Ztls_MAX_IV_SIZE];
    int ret;

    *ctx = NULL;

    buffer_init(&key_schedule_context, key_schedule_context_smallbuf, sizeof(key_schedule_context_smallbuf), is_enc != 0);

    /* key_schedule_context = concat(mode, LabeledExtract("", "psk_id_hash", psk_id), LabeledExtract("", "info_hash", info)) */
    Ztls_buffer_push(&key_schedule_context, Ztls_HPKE_MODE_BASE);
    if ((ret = buffer_reserve(&key_schedule_context, cipher->hash->digest_size, is_enc != 0)) != 0 ||
        (ret = labeled_extract(kem, cipher, key_schedule_context.base + key_schedule_context.off, wire_bytes(NULL, 0),
                               "psk_id_hash", wire_bytes(NULL, 0))) != 0)
        goto Exit;
    key_schedule_context.off += cipher->hash->digest_size;
    if ((ret = buffer_reserve(&key_schedule_context, cipher->hash->digest_size, is_enc != 0)) != 0 ||
        (ret = labeled_extract(kem, cipher, key_schedule_context.base + key_schedule_context.off, wire_bytes(NULL, 0),
                               "info_hash", info)) != 0)
        goto Exit;
    key_schedule_context.off += cipher->hash->digest_size;

    /* secret = LabeledExtract(shared_secret, "secret", psk) */
    if ((ret = labeled_extract(kem, cipher, secret, wire_bytes(shared_secret, kem->hash->digest_size), "secret",
                               wire_bytes("", 0))) != 0)
        goto Exit;

    /* key, base_nonce */
    if ((ret = labeled_expand(kem, cipher, key, cipher->aead->key_size, wire_bytes(secret, cipher->hash->digest_size), "key",
                              wire_bytes(key_schedule_context.base, key_schedule_context.off))) != 0)
        goto Exit;
    if ((ret = labeled_expand(kem, cipher, base_nonce, cipher->aead->iv_size, wire_bytes(secret, cipher->hash->digest_size),
                              "base_nonce", wire_bytes(key_schedule_context.base, key_schedule_context.off))) != 0)
        goto Exit;

    *ctx = aead_new_direct(cipher->aead, is_enc, key, base_nonce);

Exit:
    buffer_dispose(&key_schedule_context);
    ZuClear(secret, sizeof(secret));
    ZuClear(key, sizeof(key));
    ZuClear(base_nonce, sizeof(base_nonce));
    return ret;
}

int hpke_setup_base_s(hpke_kem_t *kem, hpke_cipher_suite_t *cipher, KeyExchangeBytes *pk_s, aead_context_t **ctx,
                           Bytes pk_r, Bytes info)
{
    uint8_t secret[Ztls_MAX_DIGEST_SIZE];
    int ret;

    pk_s->null();

    if ((ret = dh_encap(kem, secret, pk_s, pk_r)) != 0)
        goto Exit;

    if ((ret = key_schedule(kem, cipher, ctx, 1, secret, info)) != 0)
        goto Exit;

Exit:
    if (ret != 0)
        pk_s->null();
    ZuClear(secret, sizeof(secret));
    return ret;
}

int hpke_setup_base_r(hpke_kem_t *kem, hpke_cipher_suite_t *cipher, key_exchange_context_t *keyex,
                           aead_context_t **ctx, Bytes pk_s, Bytes info)
{
    uint8_t secret[Ztls_MAX_DIGEST_SIZE];
    int ret;

    if ((ret = dh_decap(kem, secret, keyex, pk_s, keyex->pubkey)) != 0)
        goto Exit;

    if ((ret = key_schedule(kem, cipher, ctx, 0, secret, info)) != 0)
        goto Exit;

Exit:
    ZuClear(secret, sizeof(secret));
    return ret;
}

} // Ztls_
