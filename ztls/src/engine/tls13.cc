/*
 * Copyright (c) 2016 DeNA Co., Ltd., Kazuho Oku
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
#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <zlib/ZmTime.hh>
#include <zlib/ZmVHeap.hh>
#include <zlib/ZtArray.hh>
#include <zlib/ZtScratch.hh>
#include <zlib/ZiIP.hh>
#include <zlib/ZiIOBuf.hh>
#include <zlib/ZuDerive.hh>
#include <zlib/ZtlsCxn.hh>

#include <zlib/ZtlsSec.hh>

namespace Ztls_ {

#define Ztls_MAX_PLAINTEXT_RECORD_SIZE Ztls_RECEIVE_APPLICATION_CAPACITY
#define Ztls_MAX_ENCRYPTED_RECORD_SIZE Ztls_RECEIVE_RECORD_CAPACITY

#define Ztls_RECORD_VERSION_MAJOR 3
#define Ztls_RECORD_VERSION_MINOR 3

#define Ztls_CONTENT_TYPE_CHANGE_CIPHER_SPEC 20
#define Ztls_CONTENT_TYPE_ALERT 21
#define Ztls_CONTENT_TYPE_HANDSHAKE 22
#define Ztls_CONTENT_TYPE_APPDATA 23

#define Ztls_PSK_KE_MODE_PSK 0
#define Ztls_PSK_KE_MODE_PSK_DHE 1

#define Ztls_HANDSHAKE_HEADER_SIZE 4

#define Ztls_EXTENSION_TYPE_SERVER_NAME 0
#define Ztls_EXTENSION_TYPE_STATUS_REQUEST 5
#define Ztls_EXTENSION_TYPE_SUPPORTED_GROUPS 10
#define Ztls_EXTENSION_TYPE_SIGNATURE_ALGORITHMS 13
#define Ztls_EXTENSION_TYPE_ALPN 16
#define Ztls_EXTENSION_TYPE_SERVER_CERTIFICATE_TYPE 20
#define Ztls_EXTENSION_TYPE_COMPRESS_CERTIFICATE 27
#define Ztls_EXTENSION_TYPE_PRE_SHARED_KEY 41
#define Ztls_EXTENSION_TYPE_EARLY_DATA 42
#define Ztls_EXTENSION_TYPE_SUPPORTED_VERSIONS 43
#define Ztls_EXTENSION_TYPE_COOKIE 44
#define Ztls_EXTENSION_TYPE_PSK_KEY_EXCHANGE_MODES 45
#define Ztls_EXTENSION_TYPE_CERTIFICATE_AUTHORITIES 47
#define Ztls_EXTENSION_TYPE_KEY_SHARE 51
#define Ztls_EXTENSION_TYPE_TICKET_REQUEST 58
#define Ztls_EXTENSION_TYPE_ECH_OUTER_EXTENSIONS 0xfd00
#define Ztls_EXTENSION_TYPE_ENCRYPTED_CLIENT_HELLO 0xfe0d

#define Ztls_SERVER_NAME_TYPE_HOSTNAME 0

#define Ztls_ECH_CONFIG_VERSION 0xfe0d
#define Ztls_ECH_CLIENT_HELLO_TYPE_OUTER 0
#define Ztls_ECH_CLIENT_HELLO_TYPE_INNER 1

#define Ztls_ECH_CONFIRM_LENGTH 8

static const char ech_info_prefix[8] = "tls ech";

#define Ztls_SERVER_CERTIFICATE_VERIFY_CONTEXT_STRING "TLS 1.3, server CertificateVerify"
#define Ztls_CLIENT_CERTIFICATE_VERIFY_CONTEXT_STRING "TLS 1.3, client CertificateVerify"
#define Ztls_MAX_CERTIFICATE_VERIFY_SIGNDATA_SIZE                                                                                  \
    (64 + sizeof(Ztls_SERVER_CERTIFICATE_VERIFY_CONTEXT_STRING) + Ztls_MAX_DIGEST_SIZE * 2)

#define Ztls_EARLY_DATA_MAX_DELAY 10000 /* max. RTT (in msec) to permit early data */

#ifndef Ztls_MAX_EARLY_DATA_SKIP_SIZE
#define Ztls_MAX_EARLY_DATA_SKIP_SIZE 65536
#endif


/**
 * list of supported versions in the preferred order
 */
static const uint16_t supported_versions[] = {Ztls_PROTOCOL_VERSION_TLS13};

static const uint8_t hello_retry_random[Ztls_HELLO_RANDOM_SIZE] = {0xCF, 0x21, 0xAD, 0x74, 0xE5, 0x9A, 0x61, 0x11, 0xBE, 0x1D, 0x8C,
                                                                   0x02, 0x1E, 0x65, 0xB8, 0x91, 0xC2, 0xA2, 0x11, 0x16, 0x7A, 0xBB,
                                                                   0x8C, 0x5E, 0x07, 0x9E, 0x09, 0xE2, 0xC8, 0xA8, 0x33, 0x9C};

struct st_record_message_emitter_t {
    message_emitter_t super;
    size_t rec_start;
};

struct st_decoded_ech_config_t {
    uint8_t id;
    hpke_kem_t *kem;
    Bytes public_key;
    hpke_cipher_suite_t *cipher;
    uint8_t max_name_length;
    Bytes public_name;
    Bytes bytes;
};

struct st_record_t {
    uint8_t type;
    uint16_t version;
    size_t length;
    const uint8_t *fragment;
};

#define MAX_UNKNOWN_EXTENSIONS 16
#define MAX_CERTIFICATE_TYPES 8

struct st_client_hello_t {
    uint16_t legacy_version;
    const uint8_t *random_bytes;
    Bytes legacy_session_id;
    struct {
        const uint8_t *ids;
        size_t count;
    } compression_methods;
    uint16_t selected_version;
    Bytes cipher_suites;
    Bytes negotiated_groups;
    Bytes key_shares;
    SignatureAlgorithms signature_algorithms;
    Bytes server_name;
    struct {
        Bytes list[16];
        size_t count;
    } alpn;
    struct {
        uint16_t list[16];
        size_t count;
    } cert_compression_algos;
    struct {
        Bytes all;
        Bytes tbs;
        Bytes ch1_hash;
        Bytes signature;
        unsigned sent_key_share : 1;
    } cookie;
    struct {
        uint8_t list[MAX_CERTIFICATE_TYPES];
        size_t count;
    } server_certificate_types;
    unsigned status_request : 1;
    struct {
        uint8_t new_session_count;
        uint8_t resumption_count;
    } ticket_request;
    /**
     * ECH: payload.data() != nullptr indicates that the extension was received
     */
    struct {
        uint8_t type;
        uint8_t config_id;
        hpke_cipher_suite_id_t cipher_suite;
        Bytes enc;
        Bytes payload;
    } ech;
    struct {
        const uint8_t *hash_end;
        struct {
            client_hello_psk_identity_t list[4];
            size_t count;
        } identities;
        unsigned ke_modes;
        unsigned early_data_indication : 1;
        unsigned is_last_extension : 1;
    } psk;
    raw_extension_t *unknown_extensions;
    size_t first_extension_at;
};

struct st_server_hello_t {
    uint8_t random_[Ztls_HELLO_RANDOM_SIZE];
    Bytes legacy_session_id;
    int is_retry_request;
    Bytes peerkey;
    struct {
        uint16_t selected_group;
        Bytes cookie;
        const uint8_t *ech;
    } retry_request;
};

struct ScheduleHash {
    hash_algorithm_t *algo;
    hash_context_t *ctx;
    hash_context_t *ctx_outer;
};
ZuDerive(ScheduleHashes, (ZtArray<ScheduleHash, ZtArrayHeapID<"Ztls.Engine.ScheduleHashes">>));

struct st_key_schedule_t {
    unsigned generation; /* early secret (1), handshake secret (2), master secret (3) */
    uint8_t secret[Ztls_MAX_DIGEST_SIZE];
    size_t num_hashes;
    ScheduleHashes hashes;
};
using ScheduleHeap = ZmVHeap<"Ztls.Engine.Schedule", 64, 4096, alignof(key_schedule_t)>;

ZuDerive(CollectedExtensions, (ZtArray<raw_extension_t, ZtArrayHeapID<"Ztls.Engine.CollectedExtensions">>));
ZuDerive(DecodedClientHello, (ZtArray<st_client_hello_t, ZtArrayHeapID<"Ztls.Engine.DecodedClientHello">>));

struct st_extension_decoder_t {
    uint16_t type;
    int (*cb)(Ztls::Cxn *tls, void *arg, const uint8_t *src, const uint8_t *const end);
};

struct st_extension_bitmap_t {
    uint64_t bits;
};

static const uint8_t zeroes_of_max_digest_size[Ztls_MAX_DIGEST_SIZE] = {0};

static aead_context_t *new_aead(aead_algorithm_t *aead, hash_algorithm_t *hash, int is_enc, const void *secret,
                                     Bytes hash_value, const char *label_prefix);
static int server_finish_handshake(Ztls::Cxn *tls, message_emitter_t *emitter, int send_cert_verify,
                                   SignatureAlgorithms *signature_algorithms);

static int is_supported_version(uint16_t v)
{
    size_t i;
    for (i = 0; i != Ztls_ELEMENTSOF(supported_versions); ++i)
        if (supported_versions[i] == v)
            return 1;
    return 0;
}

static int extension_bitmap_testandset(struct st_extension_bitmap_t *bitmap, int hstype, uint16_t extid)
{
#define HSTYPE_TO_BIT(hstype) ((uint64_t)1 << ((hstype) + 1)) /* min(hstype) is -1 (PSEUDO_HRR) */
#define DEFINE_BIT(abbrev, hstype) static const uint64_t abbrev = HSTYPE_TO_BIT(Ztls_HANDSHAKE_TYPE_##hstype)
#define EXT(candext, allowed_bits)                                                                                                 \
    do {                                                                                                                           \
        if (Ztls_UNLIKELY(extid == Ztls_EXTENSION_TYPE_##candext)) {                                                               \
            allowed_hs_bits = allowed_bits;                                                                                        \
            goto Found;                                                                                                            \
        }                                                                                                                          \
        ext_bitmap_mask <<= 1;                                                                                                     \
    } while (0)

    DEFINE_BIT(CH, CLIENT_HELLO);
    DEFINE_BIT(SH, SERVER_HELLO);
    DEFINE_BIT(HRR, PSEUDO_HRR);
    DEFINE_BIT(EE, ENCRYPTED_EXTENSIONS);
    DEFINE_BIT(CR, CERTIFICATE_REQUEST);
    DEFINE_BIT(CT, CERTIFICATE);
    DEFINE_BIT(NST, NEW_SESSION_TICKET);

    uint64_t allowed_hs_bits, ext_bitmap_mask = 1;

    /* clang-format off */
    /* RFC 8446 section 4.2: "The table below indicates the messages where a given extension may appear... If an implementation
     * receives an extension which it recognizes and which is not specified for the message in which it appears, it MUST abort the
     * handshake with an "illegal_parameter" alert.
     *
     * +-------------------------+---------------+
     * +        Extension        |    Allowed    |
     * +-------------------------+---------------+ */
    EXT( SERVER_NAME             , CH + EE       );
    EXT( STATUS_REQUEST          , CH + CR + CT  );
    EXT( SUPPORTED_GROUPS        , CH + EE       );
    EXT( SIGNATURE_ALGORITHMS    , CH + CR       );
    EXT( ALPN                    , CH + EE       );
    EXT( SERVER_CERTIFICATE_TYPE , CH + EE       );
    EXT( KEY_SHARE               , CH + SH + HRR );
    EXT( PRE_SHARED_KEY          , CH + SH       );
    EXT( PSK_KEY_EXCHANGE_MODES  , CH            );
    EXT( EARLY_DATA              , CH + EE + NST );
    EXT( COOKIE                  , CH + HRR      );
    EXT( SUPPORTED_VERSIONS      , CH + SH + HRR );
    EXT( COMPRESS_CERTIFICATE    , CH + CR       ); /* from RFC 8879 */
    EXT( ENCRYPTED_CLIENT_HELLO  , CH + HRR + EE ); /* from draft-ietf-tls-esni-15 */
    EXT( ECH_OUTER_EXTENSIONS    , 0             );
    /* +-----------------------------------------+ */
    /* clang-format on */

    return 1;

Found:
    if ((allowed_hs_bits & HSTYPE_TO_BIT(hstype)) == 0)
        return 0;
    if ((bitmap->bits & ext_bitmap_mask) != 0)
        return 0;
    bitmap->bits |= ext_bitmap_mask;
    return 1;

#undef HSTYPE_TO_BIT
#undef DEFINE_ABBREV
#undef EXT
}

template <typename T>
static T load_be(const uint8_t *src)
{
    T data;
    memcpy(&data, src, sizeof(data));
    return ZuBE(data);
}

static uint32_t load_be24(const uint8_t *src)
{
    return (uint32_t(src[0]) << 16) | load_be<uint16_t>(src + 1);
}

template <typename Array>
static void assign_name(Array &dst, const char *src, size_t len)
{
    if (!src) {
        dst.null();
        return;
    }
    if (!len)
        len = strlen(src);
    Array name(len + 1, len + 1);
    memcpy(name.data(), src, len);
    name[len] = '\0';
    dst = ZuMv(name);
}

static int buffer_require_direction(buffer_t *buf, int tx)
{
    assert(tx == 0 || tx == 1);
    if (buf->tx == tx)
        return 0;
    assert(!"TLS buffer direction mismatch");
    return Ztls_ERROR_LIBRARY;
}

using RecordBufferHeap = ZmVHeap<"Ztls.RecordBuffer", 128, (1U << 20), ZiIOBuf_Align>;
static constexpr unsigned RecordBufferAlignBits = ZuIntrin::ctz(ZiIOBuf_Align);

void buffer__release_memory(buffer_t *buf)
{
    if (buf->off)
        ZuClear(buf->base, buf->off);
    if (buf->is_allocated)
        RecordBufferHeap::free(buf->base, buf->capacity);
}

int buffer_reserve_aligned(buffer_t *buf, size_t delta, uint8_t align_bits, int tx)
{
    int ret;
    if ((ret = buffer_require_direction(buf, tx)) != 0)
        return ret;

    if (buf->base == NULL)
        return Ztls_ERROR_NO_MEMORY;

    if (delta > SIZE_MAX - buf->off)
        return Ztls_ERROR_NO_MEMORY;
    size_t required = buf->off + delta;

    if (buf->capacity < required ||
        (buf->align_bits < align_bits && ((uintptr_t)buf->base & (((uintptr_t)1 << align_bits) - 1)) != 0)) {
            size_t new_capacity = buf->capacity;
        if (new_capacity < 1024)
            new_capacity = 1024;
        while (new_capacity < required) {
            if (new_capacity > SIZE_MAX / 2) {
                new_capacity = required;
                break;
            }
            new_capacity *= 2;
        }
        if (new_capacity > UINT32_MAX || align_bits > RecordBufferAlignBits)
            return Ztls_ERROR_NO_MEMORY;
        auto data = static_cast<uint8_t *>(RecordBufferHeap::alloc(new_capacity));
        if (buf->off)
            memcpy(data, buf->base, buf->off);
        buffer__release_memory(buf);
        buf->base = data;
        buf->capacity = new_capacity;
        buf->is_allocated = 1;
        buf->align_bits = align_bits;
    }

    return 0;
}

int buffer_reserve(buffer_t *buf, size_t delta, int tx)
{
    return buffer_reserve_aligned(buf, delta, 0, tx);
}

int buffer__do_pushv(buffer_t *buf, const void *src, size_t len)
{
    int ret;

    if (len == 0)
        return 0;
    if ((ret = buffer_reserve(buf, len, buf->tx)) != 0)
        return ret;
    memcpy(buf->base + buf->off, src, len);
    buf->off += len;
    return 0;
}

static void build_aad(uint8_t aad[5], size_t reclen)
{
    aad[0] = Ztls_CONTENT_TYPE_APPDATA;
    aad[1] = Ztls_RECORD_VERSION_MAJOR;
    aad[2] = Ztls_RECORD_VERSION_MINOR;
    aad[3] = (uint8_t)(reclen >> 8);
    aad[4] = (uint8_t)reclen;
}

static int aead_decrypt(TrafficProtection *ctx, void *output, size_t *outlen, const void *input, size_t inlen)
{
    uint8_t aad[5];

    build_aad(aad, inlen);
    if ((*outlen = aead_decrypt(ctx->aead, output, input, inlen, ctx->seq, aad, sizeof(aad))) == SIZE_MAX)
        return Ztls_ALERT_BAD_RECORD_MAC;
    ++ctx->seq;
    return 0;
}

static int record_serial_available(TrafficProtection *enc)
{
    return enc->aead == NULL || (enc->aead->do_enc_begin != NULL && enc->aead->do_enc_chunk != NULL &&
                                 enc->aead->do_enc_end != NULL);
}

template <typename Sink>
struct record_writer {
    Sink *sink;
    size_t remaining;
    int contiguous;
};

template <typename Sink>
static int record_write(record_writer<Sink> *writer, const void *input, size_t length, aead_context_t *aead)
{
    const uint8_t *src = static_cast<const uint8_t *>(input);
    int ret;
    if (length > writer->remaining)
        return Ztls_ERROR_LIBRARY;
    while (length != 0) {
        void *dst = NULL;
        size_t capacity = 0, written;
        if ((ret = writer->sink->next(&dst, &capacity)) != 0)
            return ret;
        if (dst == NULL || capacity == 0)
            return Ztls_ERROR_LIBRARY;
        size_t count = length < capacity ? length : capacity;
        if (aead != NULL) {
            if ((ret = aead->do_enc_chunk(aead, dst, src, count, &written)) != 0 || written != count)
                return Ztls_ERROR_LIBRARY;
        } else {
            memcpy(dst, src, count);
        }
        if ((ret = writer->sink->commit(count)) != 0)
            return ret;
        src += count;
        length -= count;
        writer->remaining -= count;
    }
    return 0;
}

static size_t record_wire_size(TrafficProtection *enc, size_t length)
{
    return length + 5 + (enc->aead == NULL ? 0 : enc->aead->algo->tag_size + 1);
}

/* A record has one nonce, one cipher stream and one tag, independent of destination span boundaries. */
template <typename Sink>
static int record_emitv(record_writer<Sink> *writer, TrafficProtection *enc,
                        uint8_t type, Bytes *input, size_t incnt, size_t length)
{
    uint8_t header[5] = {type, Ztls_RECORD_VERSION_MAJOR, Ztls_RECORD_VERSION_MINOR};
    uint8_t tag[Ztls_MAX_DIGEST_SIZE];
    size_t wire_size = record_wire_size(enc, length), written;
    aead_context_t *aead = enc->aead;
    int ret;
    uint64_t nonce = enc->seq;
    if (enc->failed || length > Ztls_MAX_PLAINTEXT_RECORD_SIZE)
        return Ztls_ERROR_LIBRARY;
    if (!writer->contiguous && !record_serial_available(enc))
        return Ztls_ERROR_NOT_AVAILABLE;
    if (aead != NULL)
        header[0] = Ztls_CONTENT_TYPE_APPDATA;
    header[3] = (uint8_t)((wire_size - 5) >> 8);
    header[4] = (uint8_t)(wire_size - 5);
    if ((ret = record_write(writer, header, sizeof(header), NULL)) != 0)
        return ret;
    if (aead == NULL) {
        for (size_t i = 0; i != incnt; ++i)
            if ((ret = record_write(writer, input[i].data(), input[i].length(), NULL)) != 0)
                return ret;
        return 0;
    }
    if (writer->contiguous) {
        /* Preserve the optimized vector operation for contiguous destinations. */
        void *dst = NULL;
        size_t capacity = 0, count = incnt + 1, size = length + aead->algo->tag_size + 1;
        input[incnt] = wire_bytes(&type, 1);
        if ((ret = writer->sink->next(&dst, &capacity)) != 0)
            return ret;
        if (dst == NULL || capacity < size || size > writer->remaining)
            return Ztls_ERROR_LIBRARY;
        if (incnt == 1 && input[0].data() == dst) {
            /* Handshake encryption aliases its formatting buffer; _v_s requires disjoint input. */
            aead_encrypt_v(aead, dst, input, count, nonce, header, sizeof(header));
            written = size;
        } else {
            written = aead_encrypt_v_s(aead, dst, input, count, nonce, header, sizeof(header), NULL);
        }
        ++enc->seq;
        if (written != size)
            return Ztls_ERROR_LIBRARY;
        if ((ret = writer->sink->commit(written)) != 0)
            return ret;
        writer->remaining -= written;
        return 0;
    }
    if (aead->do_enc_begin(aead, nonce, header, sizeof(header)) != 0)
        return Ztls_ERROR_LIBRARY;
    ++enc->seq;
    for (size_t i = 0; i != incnt; ++i)
        if ((ret = record_write(writer, input[i].data(), input[i].length(), aead)) != 0)
            return ret;
    if ((ret = record_write(writer, &type, 1, aead)) != 0)
        return ret;
    void *dst = NULL;
    size_t capacity = 0;
    if (aead->algo->tag_size > sizeof(tag) || aead->algo->tag_size > writer->remaining)
        return Ztls_ERROR_LIBRARY;
    if ((ret = writer->sink->next(&dst, &capacity)) != 0)
        return ret;
    if (dst == NULL || capacity == 0)
        return Ztls_ERROR_LIBRARY;
    if (capacity >= aead->algo->tag_size) {
        if (aead->do_enc_end(aead, dst, capacity, &written) != 0 || written != aead->algo->tag_size)
            return Ztls_ERROR_LIBRARY;
        if ((ret = writer->sink->commit(written)) != 0)
            return ret;
        writer->remaining -= written;
        return 0;
    }
    if (aead->do_enc_end(aead, tag, sizeof(tag), &written) != 0 || written != aead->algo->tag_size)
        return Ztls_ERROR_LIBRARY;
    return record_write(writer, tag, written, NULL);
}

template <typename Sink>
static int record_emit(record_writer<Sink> *writer, TrafficProtection *enc,
                       uint8_t type, const void *input, size_t length)
{
    Bytes spans[2] = {wire_bytes(input, length)};
    return record_emitv(writer, enc, type, spans, 1, length);
}

template <typename Sink>
static int record_transaction_(Sink *sink, TrafficProtection *enc,
                               uint8_t type, const void *input, size_t length, int contiguous)
{
    int ret;
    if (!sink)
        return Ztls_ERROR_LIBRARY;
    if (!contiguous && !record_serial_available(enc))
        return Ztls_ERROR_NOT_AVAILABLE;
    record_writer writer{sink, record_wire_size(enc, length), contiguous};
    if (enc->failed)
        return Ztls_ERROR_LIBRARY;
    if ((ret = sink->reserve(writer.remaining)) != 0) {
        sink->discard();
        return ret;
    }
    if ((ret = record_emit(&writer, enc, type, input, length)) == 0)
        ret = writer.remaining == 0 ? sink->finish() : Ztls_ERROR_LIBRARY;
    if (ret != 0) {
        enc->failed = 1;
        sink->discard();
    }
    return ret;
}

static int record_transaction(Ztls::RecordSink *sink, TrafficProtection *enc,
                              uint8_t type, const void *input, size_t length)
{
    return record_transaction_(sink, enc, type, input, length, 0);
}

// A contiguous record has already reserved space before the writer borrows
// any input that may alias its destination.
class ContiguousRecord {
public:
    explicit ContiguousRecord(buffer_t *buffer) : m_buffer{buffer} { }

    int reserve(size_t length) const
    {
        return length <= m_buffer->capacity - m_buffer->off ? 0 : Ztls_ERROR_LIBRARY;
    }
    int next(void **base, size_t *capacity) const
    {
        *base = m_buffer->base + m_buffer->off;
        *capacity = m_buffer->capacity - m_buffer->off;
        return 0;
    }
    int commit(size_t length)
    {
        if (length > m_buffer->capacity - m_buffer->off)
            return Ztls_ERROR_LIBRARY;
        m_buffer->off += length;
        return 0;
    }
    int finish() const { return 0; }
    void discard() const { }

private:
    buffer_t *m_buffer;
};

static int buffer_record(buffer_t *buf, TrafficProtection *enc,
                         uint8_t type, const void *input, size_t length)
{
    if (buf->record_sink != NULL)
        return record_transaction(buf->record_sink, enc, type, input, length);
    int ret = buffer_reserve_aligned(buf, record_wire_size(enc, length),
                                          enc->aead != NULL ? enc->aead->algo->align_bits : 0, 1);
    if (ret != 0)
        return ret;
    ContiguousRecord sink{buf};
    return record_transaction_(&sink, enc, type, input, length, 1);
}

static int buffer_push_encrypted_records(buffer_t *buf, uint8_t type, const uint8_t *src, size_t len,
                                         TrafficProtection *enc)
{
    while (len != 0) {
        size_t size = len < Ztls_MAX_PLAINTEXT_RECORD_SIZE ? len : Ztls_MAX_PLAINTEXT_RECORD_SIZE;
        int ret = buffer_record(buf, enc, type, src, size);
        if (ret != 0)
            return ret;
        src += size;
        len -= size;
    }
    return 0;
}

ZuDerive(RecordPlaintext, (ZtBArray<ZtArraySecret<true, ZtArrayHeapID<"Ztls.Engine.RecordPlaintext">>>));

static int buffer_encrypt_record(buffer_t *buf, size_t rec_start, TrafficProtection *enc)
{
    size_t bodylen = buf->off - rec_start - 5;
    uint8_t type = buf->base[rec_start];
    int ret;

    if (buf->record_sink != NULL) {
        for (size_t off = 0; off < bodylen;) {
            size_t len = bodylen - off;
            if (len > Ztls_MAX_PLAINTEXT_RECORD_SIZE)
                len = Ztls_MAX_PLAINTEXT_RECORD_SIZE;
            int ret = record_transaction(buf->record_sink, enc, type, buf->base + rec_start + 5 + off, len);
            if (ret != 0)
                return ret;
            off += len;
        }
        return 0;
    }

    if (bodylen <= Ztls_MAX_PLAINTEXT_RECORD_SIZE) {
        size_t extra = record_wire_size(enc, bodylen) - bodylen - 5;
        if ((ret = buffer_reserve_aligned(buf, extra, enc->aead->algo->align_bits, 1)) != 0)
            return ret;
        buf->off = rec_start;
        ContiguousRecord sink{buf};
        return record_transaction_(&sink, enc, type, buf->base + rec_start + 5, bodylen, 1);
    }

    RecordPlaintext plaintext(buf->base + rec_start + 5, bodylen);
    ZuClear(buf->base + rec_start, bodylen + 5);
    buf->off = rec_start;
    return buffer_push_encrypted_records(buf, type, plaintext.data(), bodylen, enc);
}

static int begin_record_message(message_emitter_t *_self)
{
    struct st_record_message_emitter_t *self = reinterpret_cast<struct st_record_message_emitter_t *>(_self);
    int ret;

    self->rec_start = self->super.buf->off;
    Ztls_buffer_push(self->super.buf, Ztls_CONTENT_TYPE_HANDSHAKE, Ztls_RECORD_VERSION_MAJOR, Ztls_RECORD_VERSION_MINOR, 0, 0);
    ret = 0;
Exit:
    return ret;
}

static int commit_record_message(message_emitter_t *_self)
{
    struct st_record_message_emitter_t *self = reinterpret_cast<struct st_record_message_emitter_t *>(_self);
    int ret;

    if (self->super.enc->aead != NULL) {
        ret = buffer_encrypt_record(self->super.buf, self->rec_start, self->super.enc);
    } else {
        /* TODO allow CH,SH,HRR above 16KB */
        size_t sz = self->super.buf->off - self->rec_start - 5;
        assert(sz <= Ztls_MAX_PLAINTEXT_RECORD_SIZE);
        self->super.buf->base[self->rec_start + 3] = (uint8_t)(sz >> 8);
        self->super.buf->base[self->rec_start + 4] = (uint8_t)(sz);
        TrafficProtection clear = {};
        if (self->super.buf->record_sink != NULL)
            return record_transaction(self->super.buf->record_sink, &clear, Ztls_CONTENT_TYPE_HANDSHAKE,
                                      self->super.buf->base + self->rec_start + 5, sz);
        ret = 0;
    }

    return ret;
}

#define buffer_push_extension(buf, type, block)                                                                                    \
    do {                                                                                                                           \
        Ztls_buffer_push16((buf), (type));                                                                                         \
        Ztls_buffer_push_block((buf), 2, block);                                                                                   \
    } while (0);

#define decode_open_extensions(src, end, hstype, exttype, block)                                                                   \
    do {                                                                                                                           \
        struct st_extension_bitmap_t bitmap = {0};                                                                            \
        Ztls_decode_open_block((src), end, 2, {                                                                                    \
            while ((src) != end) {                                                                                                 \
                if ((ret = decode16((exttype), &(src), end)) != 0)                                                            \
                    goto Exit;                                                                                                     \
                if (!extension_bitmap_testandset(&bitmap, (hstype), *(exttype))) {                                                 \
                    ret = Ztls_ALERT_ILLEGAL_PARAMETER;                                                                            \
                    goto Exit;                                                                                                     \
                }                                                                                                                  \
                Ztls_decode_open_block((src), end, 2, block);                                                                      \
            }                                                                                                                      \
        });                                                                                                                        \
    } while (0)

#define decode_extensions(src, end, hstype, exttype, block)                                                                        \
    do {                                                                                                                           \
        decode_open_extensions((src), end, hstype, exttype, block);                                                                \
        Ztls_decode_assert_block_close((src), end);                                                                                \
    } while (0)

int decode8(uint8_t *value, const uint8_t **src, const uint8_t *end)
{
    if (*src == end)
        return Ztls_ALERT_DECODE_ERROR;
    *value = *(*src)++;
    return 0;
}

int decode16(uint16_t *value, const uint8_t **src, const uint8_t *end)
{
    if (end - *src < 2)
        return Ztls_ALERT_DECODE_ERROR;
    *value = load_be<uint16_t>(*src);
    *src += 2;
    return 0;
}

int decode24(uint32_t *value, const uint8_t **src, const uint8_t *end)
{
    if (end - *src < 3)
        return Ztls_ALERT_DECODE_ERROR;
    *value = load_be24(*src);
    *src += 3;
    return 0;
}

int decode32(uint32_t *value, const uint8_t **src, const uint8_t *end)
{
    if (end - *src < 4)
        return Ztls_ALERT_DECODE_ERROR;
    *value = load_be<uint32_t>(*src);
    *src += 4;
    return 0;
}

int decode64(uint64_t *value, const uint8_t **src, const uint8_t *end)
{
    if (end - *src < 8)
        return Ztls_ALERT_DECODE_ERROR;
    *value = load_be<uint64_t>(*src);
    *src += 8;
    return 0;
}

/**
 * This function preserves the flags and  modes (e.g., `offered`, `accepted`, `cipher`), they can be used afterwards.
 */
static void clear_ech(Ech *ech, int is_server)
{
    if (ech->aead != NULL) {
        aead_free(ech->aead);
        ech->aead = NULL;
    }
    ZuClear(ech->inner_client_random, Ztls_HELLO_RANDOM_SIZE);
    if (!is_server) {
        ech->client.enc.null();
        ech->client.public_name.null();
        ech->client.first_ech.null();
    }
}

/**
 * Decodes one ECHConfigContents (tls-esni-15 section 4). `decoded->kem` and `cipher` may be NULL even when the function returns
 * zero, if the corresponding entries are not found.
 */
static int decode_one_ech_config(hpke_kem_t **kems, hpke_cipher_suite_t **ciphers,
                                 struct st_decoded_ech_config_t *decoded, const uint8_t **src, const uint8_t *const end)
{
    EchPublicName public_name_buf;
    int ret;

    *decoded = (struct st_decoded_ech_config_t){0};

    if ((ret = decode8(&decoded->id, src, end)) != 0)
        goto Exit;
    uint16_t kem_id;
    if ((ret = decode16(&kem_id, src, end)) != 0)
        goto Exit;
    for (size_t i = 0; kems[i] != NULL; ++i) {
        if (kems[i]->id == kem_id) {
            decoded->kem = kems[i];
            break;
        }
    }
    Ztls_decode_open_block(*src, end, 2, {
        if (*src == end) {
            ret = Ztls_ALERT_DECODE_ERROR;
            goto Exit;
        }
        decoded->public_key = wire_bytes(*src, end - *src);
        *src = end;
    });
    Ztls_decode_open_block(*src, end, 2, {
        do {
            uint16_t kdf_id;
            uint16_t aead_id;
            if ((ret = decode16(&kdf_id, src, end)) != 0)
                goto Exit;
            if ((ret = decode16(&aead_id, src, end)) != 0)
                goto Exit;
            if (decoded->cipher == NULL) {
                for (size_t i = 0; ciphers[i] != NULL; ++i) {
                    if (ciphers[i]->id.kdf == kdf_id && ciphers[i]->id.aead == aead_id) {
                        decoded->cipher = ciphers[i];
                        break;
                    }
                }
            }
        } while (*src != end);
    });
    if ((ret = decode8(&decoded->max_name_length, src, end)) != 0)
        goto Exit;

#define SKIP_DECODED()                                                                                                             \
    do {                                                                                                                           \
        decoded->kem = NULL;                                                                                                       \
        decoded->cipher = NULL;                                                                                                    \
    } while (0)

    /* Decode public_name. The specification requires clients to ignore (upon parsing ESNIConfigList) or reject (upon handshake)
     * public names that are not DNS names or IPv4 addresses. We ignore IPv4 and v6 addresses during parsing (IPv6 addresses never
     * looks like DNS names), and delegate the responsibility of rejecting non-DNS names to the certificate verify callback. */
    Ztls_decode_open_block(*src, end, 1, {
        if (*src == end) {
            ret = Ztls_ALERT_DECODE_ERROR;
            goto Exit;
        }
        assign_name(public_name_buf, reinterpret_cast<const char *>(*src), end - *src);
        if (server_name_is_ipaddr(public_name_buf.data())) {
            SKIP_DECODED();
        } else {
            decoded->public_name = wire_bytes(*src, end - *src);
        }
        *src = end;
    });

    Ztls_decode_block(*src, end, 2, {
        while (*src < end) {
            uint16_t type;
            if ((ret = decode16(&type, src, end)) != 0)
                goto Exit;
            Ztls_decode_open_block(*src, end, 2, { *src = end; });
            /* if a critital extension is found, indicate that the config cannot be used */
            if ((type & 0x8000) != 0)
                SKIP_DECODED();
        }
    });

#undef SKIP_DECODED

Exit:
    return ret;
}

static int client_decode_ech_config_list(context_t *ctx, struct st_decoded_ech_config_t *decoded, Bytes config_list)
{
    const uint8_t *src = config_list.data(), *const end = src + config_list.length();
    int match_found = 0, ret;

    *decoded = (struct st_decoded_ech_config_t){0};

    Ztls_decode_block(src, end, 2, {
        do {
            const uint8_t *config_start = src;
            uint16_t version;
            if ((ret = decode16(&version, &src, end)) != 0)
                goto Exit;
            Ztls_decode_open_block(src, end, 2, {
                /* If the block is the one that we recognize, parse it, then adopt if if possible. Otherwise, skip. */
                if (version == Ztls_ECH_CONFIG_VERSION) {
                    struct st_decoded_ech_config_t thisconf;
                    if ((ret = decode_one_ech_config(ctx->ech.client.kems, ctx->ech.client.ciphers, &thisconf, &src, end)) != 0)
                        goto Exit;
                    if (!match_found && thisconf.kem != NULL && thisconf.cipher != NULL) {
                        *decoded = thisconf;
                        decoded->bytes = wire_bytes(config_start, end - config_start);
                        match_found = 1;
                    }
                } else {
                    src = end;
                }
            });
        } while (src != end);
    });
    ret = 0;

Exit:
    if (ret != 0)
        *decoded = (struct st_decoded_ech_config_t){0};
    return ret;
}

static int client_setup_ech(Ech *ech, struct st_decoded_ech_config_t *decoded,
                            void (*random_bytes)(void *, size_t))
{
    buffer_t infobuf;
    uint8_t infobuf_smallbuf[256];
    int ret;

    /* setup `enc` and `aead` by running HPKE */
    buffer_init_tx(&infobuf, infobuf_smallbuf, sizeof(infobuf_smallbuf));
    Ztls_buffer_pushv(&infobuf, ech_info_prefix, sizeof(ech_info_prefix));
    Ztls_buffer_pushv(&infobuf, decoded->bytes.data(), decoded->bytes.length());
    if ((ret = hpke_setup_base_s(decoded->kem, decoded->cipher, &ech->client.enc, &ech->aead, decoded->public_key,
                                      wire_bytes(infobuf.base, infobuf.off))) != 0)
        goto Exit;

    /* setup the rest */
    ech->config_id = decoded->id;
    ech->kem = decoded->kem;
    ech->cipher = decoded->cipher;
    random_bytes(ech->inner_client_random, Ztls_HELLO_RANDOM_SIZE);
    ech->client.max_name_length = decoded->max_name_length;
    assign_name(ech->client.public_name, reinterpret_cast<const char *>(decoded->public_name.data()), decoded->public_name.length());

Exit:
    buffer_dispose(&infobuf);
    if (ret != 0)
        clear_ech(ech, 0);
    return ret;
}

static void client_setup_ech_grease(Ech *ech, void (*random_bytes)(void *, size_t), hpke_kem_t **kems,
                                    hpke_cipher_suite_t **ciphers, const char *sni_name)
{
    static const size_t x25519_key_size = 32;
    uint8_t random_secret[Ztls_AES128_KEY_SIZE + Ztls_AES_IV_SIZE];

    /* pick up X25519, AES-128-GCM or bail out */
    for (size_t i = 0; kems[i] != NULL; ++i) {
        if (kems[i]->id == Ztls_HPKE_KEM_X25519_SHA256) {
            ech->kem = kems[i];
            break;
        }
    }
    for (size_t i = 0; ciphers[i] != NULL; ++i) {
        if (ciphers[i]->id.kdf == Ztls_HPKE_HKDF_SHA256 && ciphers[i]->id.aead == Ztls_HPKE_AEAD_AES_128_GCM) {
            ech->cipher = ciphers[i];
            break;
        }
    }
    if (ech->kem == NULL || ech->cipher == NULL) {
        clear_ech(ech, 0);
        return;
    }

    /* aead is generated from random */
    random_bytes(random_secret, sizeof(random_secret));
    ech->aead = aead_new_direct(ech->cipher->aead, 1, random_secret, random_secret + Ztls_AES128_KEY_SIZE);
    ZuClear(random_secret, sizeof(random_secret));

    /* `enc` is random bytes */
    ech->client.enc.length(x25519_key_size);
    random_bytes(ech->client.enc.data(), ech->client.enc.length());

    /* setup the rest (inner_client_random is left zeros) */
    random_bytes(&ech->config_id, sizeof(ech->config_id));
    ech->client.max_name_length = 64;
    assign_name(ech->client.public_name, sni_name, 0);
}

#define ECH_CONFIRMATION_SERVER_HELLO "ech accept confirmation"
#define ECH_CONFIRMATION_HRR "hrr ech accept confirmation"
static int ech_calc_confirmation(key_schedule_t *sched, void *dst, const uint8_t *inner_random, const char *label,
                                 Bytes message)
{
    hash_context_t *hash = NULL;
    uint8_t secret[Ztls_MAX_DIGEST_SIZE], transcript_hash[Ztls_MAX_DIGEST_SIZE];
    int ret;

    /* calc transcript hash using the modified ServerHello / HRR */
    if ((hash = sched->hashes[0].ctx->clone_(sched->hashes[0].ctx)) == NULL) {
        ret = Ztls_ERROR_NO_MEMORY;
        goto Exit;
    }
    hash->update(hash, message.data(), message.length());
    hash->final(hash, transcript_hash, HASH_FINAL_MODE_FREE);
    hash = NULL;

    /* HKDF extract and expand */
    if ((ret = hkdf_extract(sched->hashes[0].algo, secret, wire_bytes(NULL, 0),
                                 wire_bytes(inner_random, Ztls_HELLO_RANDOM_SIZE))) != 0)
        goto Exit;
    if ((ret = hkdf_expand_label(sched->hashes[0].algo, dst, 8, wire_bytes(secret, sched->hashes[0].algo->digest_size),
                                      label, wire_bytes(transcript_hash, sched->hashes[0].algo->digest_size), NULL)) != 0)
        goto Exit;

Exit:
    ZuClear(secret, sizeof(secret));
    ZuClear(transcript_hash, sizeof(transcript_hash));
    if (hash != NULL)
        hash->final(hash, NULL, HASH_FINAL_MODE_FREE);
    return ret;
}

static void key_schedule_free(key_schedule_t *sched)
{
    size_t i;
    ZuClear(sched->secret, sizeof(sched->secret));
    for (i = 0; i != sched->num_hashes; ++i) {
        sched->hashes[i].ctx->final(sched->hashes[i].ctx, NULL, HASH_FINAL_MODE_FREE);
        if (sched->hashes[i].ctx_outer != NULL)
            sched->hashes[i].ctx_outer->final(sched->hashes[i].ctx_outer, NULL, HASH_FINAL_MODE_FREE);
    }
    sched->~key_schedule_t();
    ScheduleHeap::free(sched, sizeof(key_schedule_t));
}

static key_schedule_t *key_schedule_new(cipher_suite_t *preferred, cipher_suite_t **offered, int use_outer)
{
#define FOREACH_HASH(block)                                                                                                        \
    do {                                                                                                                           \
        cipher_suite_t *cs;                                                                                                   \
        if ((cs = preferred) != NULL) {                                                                                            \
            block                                                                                                                  \
        }                                                                                                                          \
        if (offered != NULL) {                                                                                                     \
            size_t i, j;                                                                                                           \
            for (i = 0; (cs = offered[i]) != NULL; ++i) {                                                                          \
                if (preferred == NULL || cs->hash != preferred->hash) {                                                            \
                    for (j = 0; j != i; ++j)                                                                                       \
                        if (cs->hash == offered[j]->hash)                                                                          \
                            break;                                                                                                 \
                    if (j == i) {                                                                                                  \
                        block                                                                                                      \
                    }                                                                                                              \
                }                                                                                                                  \
            }                                                                                                                      \
        }                                                                                                                          \
    } while (0)

    size_t num_hashes = 0;
    FOREACH_HASH({ ++num_hashes; });
    auto sched = new (ScheduleHeap::alloc(sizeof(key_schedule_t))) key_schedule_t{};
    sched->hashes.length(num_hashes);

    /* setup the hash algos and contexts */
    FOREACH_HASH({
        sched->hashes[sched->num_hashes].algo = cs->hash;
        if ((sched->hashes[sched->num_hashes].ctx = cs->hash->create()) == NULL)
            goto Fail;
        if (use_outer) {
            if ((sched->hashes[sched->num_hashes].ctx_outer = cs->hash->create()) == NULL) {
                auto ctx = sched->hashes[sched->num_hashes].ctx;
                ctx->final(ctx, NULL, HASH_FINAL_MODE_FREE);
                goto Fail;
            }
        } else {
            sched->hashes[sched->num_hashes].ctx_outer = NULL;
        }
        ++sched->num_hashes;
    });

    return sched;
Fail:
    key_schedule_free(sched);
    return NULL;

#undef FOREACH_HASH
}

static int key_schedule_extract(key_schedule_t *sched, Bytes ikm)
{
    int ret;

    if (ikm.data() == NULL)
        ikm = wire_bytes(zeroes_of_max_digest_size, sched->hashes[0].algo->digest_size);

    if (sched->generation != 0 &&
        (ret = hkdf_expand_label(sched->hashes[0].algo, sched->secret, sched->hashes[0].algo->digest_size,
                                      wire_bytes(sched->secret, sched->hashes[0].algo->digest_size), "derived",
                                      wire_bytes(sched->hashes[0].algo->empty_digest, sched->hashes[0].algo->digest_size),
                                      NULL)) != 0)
        return ret;

    ++sched->generation;
    ret = hkdf_extract(sched->hashes[0].algo, sched->secret,
                            wire_bytes(sched->secret, sched->hashes[0].algo->digest_size), ikm);
    return ret;
}

static int key_schedule_select_cipher(key_schedule_t *sched, cipher_suite_t *cs, int reset, Bytes reset_ikm)
{
    size_t found_slot = SIZE_MAX, i;
    int ret;

    assert(sched->generation == 1);

    /* find the one, while freeing others */
    for (i = 0; i != sched->num_hashes; ++i) {
        if (sched->hashes[i].algo == cs->hash) {
            assert(found_slot == SIZE_MAX);
            found_slot = i;
        } else {
            sched->hashes[i].ctx->final(sched->hashes[i].ctx, NULL, HASH_FINAL_MODE_FREE);
            if (sched->hashes[i].ctx_outer != NULL)
                sched->hashes[i].ctx_outer->final(sched->hashes[i].ctx_outer, NULL, HASH_FINAL_MODE_FREE);
        }
    }
    if (found_slot != 0) {
        sched->hashes[0] = sched->hashes[found_slot];
        reset = 1;
    }
    sched->num_hashes = 1;

    /* recalculate the hash if a different hash as been selected than the one we used for calculating the early secrets */
    if (reset) {
        --sched->generation;
        memset(sched->secret, 0, sizeof(sched->secret));
        if ((ret = key_schedule_extract(sched, reset_ikm)) != 0)
            goto Exit;
    }

    ret = 0;
Exit:
    return ret;
}

static void key_schedule_select_outer(key_schedule_t *sched)
{
    /* This function is called when receiving a cleartext message (Server Hello), after the cipher-suite is determined (and hence
     * the hash also), if ECH was offered */
    assert(sched->generation == 1);
    assert(sched->num_hashes == 1);
    assert(sched->hashes[0].ctx_outer != NULL);

    sched->hashes[0].ctx->final(sched->hashes[0].ctx, NULL, HASH_FINAL_MODE_FREE);
    sched->hashes[0].ctx = sched->hashes[0].ctx_outer;
    sched->hashes[0].ctx_outer = NULL;
}

void _key_schedule_update_hash(key_schedule_t *sched, const uint8_t *msg, size_t msglen, int use_outer)
{
    size_t i;

    for (i = 0; i != sched->num_hashes; ++i) {
        hash_context_t *ctx = use_outer ? sched->hashes[i].ctx_outer : sched->hashes[i].ctx;
        ctx->update(ctx, msg, msglen);
    }
}

static void key_schedule_update_ch1hash_prefix(key_schedule_t *sched)
{
    uint8_t prefix[4] = {Ztls_HANDSHAKE_TYPE_MESSAGE_HASH, 0, 0, (uint8_t)sched->hashes[0].algo->digest_size};
    _key_schedule_update_hash(sched, prefix, sizeof(prefix), 0);
}

static void key_schedule_extract_ch1hash(key_schedule_t *sched, uint8_t *hash)
{
    assert(sched->hashes[0].ctx_outer == NULL);
    sched->hashes[0].ctx->final(sched->hashes[0].ctx, hash, HASH_FINAL_MODE_RESET);
}

static void key_schedule_transform_post_ch1hash(key_schedule_t *sched)
{
    size_t digest_size = sched->hashes[0].algo->digest_size;
    hash_context_t *hashes[3] = {sched->hashes[0].ctx, sched->hashes[0].ctx_outer, NULL};
    uint8_t ch1hash[Ztls_MAX_DIGEST_SIZE];
    uint8_t prefix[4] = {Ztls_HANDSHAKE_TYPE_MESSAGE_HASH, 0, 0, (uint8_t)digest_size};

    for (size_t i = 0; hashes[i] != NULL; ++i) {
        hashes[i]->final(hashes[i], ch1hash, HASH_FINAL_MODE_RESET);
        hashes[i]->update(hashes[i], prefix, sizeof(prefix));
        hashes[i]->update(hashes[i], ch1hash, digest_size);
    }

    ZuClear(ch1hash, sizeof(ch1hash));
}

static int derive_secret_with_hash(key_schedule_t *sched, void *secret, const char *label, const uint8_t *hash)
{
    int ret = hkdf_expand_label(sched->hashes[0].algo, secret, sched->hashes[0].algo->digest_size,
                                     wire_bytes(sched->secret, sched->hashes[0].algo->digest_size), label,
                                     wire_bytes(hash, sched->hashes[0].algo->digest_size), NULL);
    return ret;
}

static int derive_secret(key_schedule_t *sched, void *secret, const char *label)
{
    uint8_t hash_value[Ztls_MAX_DIGEST_SIZE];

    sched->hashes[0].ctx->final(sched->hashes[0].ctx, hash_value, HASH_FINAL_MODE_SNAPSHOT);
    int ret = derive_secret_with_hash(sched, secret, label, hash_value);
    ZuClear(hash_value, sizeof(hash_value));
    return ret;
}

static int derive_secret_with_empty_digest(key_schedule_t *sched, void *secret, const char *label)
{
    return derive_secret_with_hash(sched, secret, label, sched->hashes[0].algo->empty_digest);
}

static int derive_exporter_secret(Ztls::Cxn *tls, int is_early)
{
    if (!tls->ctx->use_exporter)
        return 0;

    auto &slot = is_early ? tls->exporter_master_secret.early : tls->exporter_master_secret.one_rtt;
    assert(!slot.length());
    slot.length(tls->key_schedule->hashes[0].algo->digest_size);
    return derive_secret(tls->key_schedule, slot.data(), is_early ? "e exp master" : "exp master");
}

static int derive_resumption_secret(key_schedule_t *sched, uint8_t *secret, Bytes nonce)
{
    int ret;

    if ((ret = derive_secret(sched, secret, "res master")) != 0)
        goto Exit;
    if ((ret = hkdf_expand_label(sched->hashes[0].algo, secret, sched->hashes[0].algo->digest_size,
                                      wire_bytes(secret, sched->hashes[0].algo->digest_size), "resumption", nonce, NULL)) != 0)
        goto Exit;

Exit:
    if (ret != 0)
        ZuClear(secret, sched->hashes[0].algo->digest_size);
    return ret;
}

static int decode_new_session_ticket(Ztls::Cxn *tls, uint32_t *lifetime, uint32_t *age_add, Bytes *nonce, Bytes *ticket,
                                     uint32_t *max_early_data_size, const uint8_t *src, const uint8_t *const end)
{
    uint16_t exttype;
    int ret;

    if ((ret = decode32(lifetime, &src, end)) != 0)
        goto Exit;
    if ((ret = decode32(age_add, &src, end)) != 0)
        goto Exit;
    Ztls_decode_open_block(src, end, 1, {
        *nonce = wire_bytes(src, end - src);
        src = end;
    });
    Ztls_decode_open_block(src, end, 2, {
        if (src == end) {
            ret = Ztls_ALERT_DECODE_ERROR;
            goto Exit;
        }
        *ticket = wire_bytes(src, end - src);
        src = end;
    });

    *max_early_data_size = 0;
    decode_extensions(src, end, Ztls_HANDSHAKE_TYPE_NEW_SESSION_TICKET, &exttype, {
        if (tls->ctx->on_extension != NULL &&
            (ret = tls->ctx->on_extension->cb(tls->ctx->on_extension, tls, Ztls_HANDSHAKE_TYPE_NEW_SESSION_TICKET, exttype,
                                              wire_bytes(src, end - src)) != 0))
            goto Exit;
        switch (exttype) {
        case Ztls_EXTENSION_TYPE_EARLY_DATA:
            if ((ret = decode32(max_early_data_size, &src, end)) != 0)
                goto Exit;
            break;
        default:
            src = end;
            break;
        }
    });

    ret = 0;
Exit:
    return ret;
}

static int decode_stored_session_ticket(Ztls::Cxn *tls, key_exchange_algorithm_t **key_share, cipher_suite_t **cs,
                                        Bytes *secret, uint32_t *obfuscated_ticket_age, Bytes *ticket,
                                        uint32_t *max_early_data_size, const uint8_t *src, const uint8_t *const end)
{
    uint16_t kxid, csid;
    uint32_t lifetime, age_add;
    uint64_t obtained_at, now;
    Bytes nonce;
    int ret;

    /* decode */
    if ((ret = decode64(&obtained_at, &src, end)) != 0)
        goto Exit;
    if ((ret = decode16(&kxid, &src, end)) != 0)
        goto Exit;
    if ((ret = decode16(&csid, &src, end)) != 0)
        goto Exit;
    Ztls_decode_open_block(src, end, 3, {
        if ((ret = decode_new_session_ticket(tls, &lifetime, &age_add, &nonce, ticket, max_early_data_size, src, end)) != 0)
            goto Exit;
        src = end;
    });
    Ztls_decode_block(src, end, 2, {
        *secret = wire_bytes(src, end - src);
        src = end;
    });

    { /* determine the key-exchange */
        key_exchange_algorithm_t **cand;
        for (cand = tls->ctx->key_exchanges; *cand != NULL; ++cand)
            if ((*cand)->id == kxid)
                break;
        if (*cand == NULL) {
            ret = Ztls_ERROR_LIBRARY;
            goto Exit;
        }
        *key_share = *cand;
    }

    { /* determine the cipher-suite */
        cipher_suite_t **cand;
        for (cand = tls->ctx->cipher_suites; *cand != NULL; ++cand)
            if ((*cand)->id == csid)
                break;
        if (*cand == NULL) {
            ret = Ztls_ERROR_LIBRARY;
            goto Exit;
        }
        *cs = *cand;
    }

    /* calculate obfuscated_ticket_age */
    now = tls->ctx->get_time->cb(tls->ctx->get_time);
    if (!(obtained_at <= now && now - obtained_at < 7 * 86400 * 1000)) {
        ret = Ztls_ERROR_LIBRARY;
        goto Exit;
    }
    *obfuscated_ticket_age = (uint32_t)(now - obtained_at) + age_add;

    ret = 0;
Exit:
    return ret;
}

static int get_traffic_key(hash_algorithm_t *algo, void *key, size_t key_size, int is_iv, const void *secret,
                           Bytes hash_value, const char *label_prefix)
{
    return hkdf_expand_label(algo, key, key_size, wire_bytes(secret, algo->digest_size), is_iv ? "iv" : "key", hash_value,
                                  label_prefix);
}

static int get_traffic_keys(aead_algorithm_t *aead, hash_algorithm_t *hash, void *key, void *iv, const void *secret,
                            Bytes hash_value, const char *label_prefix)
{
    int ret;

    if ((ret = get_traffic_key(hash, key, aead->key_size, 0, secret, hash_value, label_prefix)) != 0 ||
        (ret = get_traffic_key(hash, iv, aead->iv_size, 1, secret, hash_value, label_prefix)) != 0) {
        ZuClear(key, aead->key_size);
        ZuClear(iv, aead->iv_size);
    }

    return ret;
}

static int setup_traffic_protection(Ztls::Cxn *tls, int is_enc, const char *secret_label, size_t epoch, uint64_t seq, int skip_notify)
{
    TrafficProtection *ctx = is_enc ? &tls->tx_traffic : &tls->rx_traffic;

    if (secret_label != NULL) {
        int ret;
        if ((ret = derive_secret(tls->key_schedule, ctx->secret, secret_label)) != 0)
            return ret;
    }

    ctx->epoch = epoch;


    /* special path for applications having their own record layer */
    if (tls->ctx->update_traffic_key != NULL) {
        if (skip_notify)
            return 0;
        return tls->ctx->update_traffic_key->cb(tls->ctx->update_traffic_key, tls, is_enc, epoch, ctx->secret);
    }

    if (ctx->aead != NULL)
        aead_free(ctx->aead);
    if ((ctx->aead = aead_new(tls->cipher_suite->aead, tls->cipher_suite->hash, is_enc, ctx->secret,
                                   tls->ctx->hkdf_label_prefix__obsolete)) == NULL)
        return Ztls_ERROR_NO_MEMORY; /* TODO obtain error from aead_new */
    ctx->seq = seq;


    return 0;
}

static int commission_handshake_secret(Ztls::Cxn *tls)
{
    int is_enc = !is_server(tls);

    assert(tls->pending_handshake_secret.data() != NULL);
    memcpy((is_enc ? &tls->tx_traffic : &tls->rx_traffic)->secret, tls->pending_handshake_secret.data(),
           Ztls_MAX_DIGEST_SIZE);
    tls->pending_handshake_secret.null();

    return setup_traffic_protection(tls, is_enc, NULL, 2, 0, 1);
}

#define SESSION_IDENTIFIER_MAGIC "ptls0001" /* the number should be changed upon incompatible format change */
#define SESSION_IDENTIFIER_MAGIC_SIZE (sizeof(SESSION_IDENTIFIER_MAGIC) - 1)

static int encode_session_identifier(context_t *ctx, buffer_t *buf, uint32_t ticket_age_add, Bytes ticket_nonce,
                                     key_schedule_t *sched, const char *server_name, uint16_t key_exchange_id, uint16_t csid,
                                     const char *negotiated_protocol)
{
    int ret = 0;

    Ztls_buffer_push_block(buf, 2, {
        /* format id */
        Ztls_buffer_pushv(buf, SESSION_IDENTIFIER_MAGIC, SESSION_IDENTIFIER_MAGIC_SIZE);
        /* date */
        Ztls_buffer_push64(buf, ctx->get_time->cb(ctx->get_time));
        /* resumption master secret */
        Ztls_buffer_push_block(buf, 2, {
            if ((ret = buffer_reserve(buf, sched->hashes[0].algo->digest_size, 1)) != 0)
                goto Exit;
            if ((ret = derive_resumption_secret(sched, buf->base + buf->off, ticket_nonce)) != 0)
                goto Exit;
            buf->off += sched->hashes[0].algo->digest_size;
        });
        /* key-exchange */
        Ztls_buffer_push16(buf, key_exchange_id);
        /* cipher-suite */
        Ztls_buffer_push16(buf, csid);
        /* ticket_age_add */
        Ztls_buffer_push32(buf, ticket_age_add);
        /* session ID context */
        Ztls_buffer_push_block(buf, 2, {
            if (ctx->ticket_context.is_set) {
                Ztls_buffer_pushv(buf, ctx->ticket_context.bytes, sizeof(ctx->ticket_context.bytes));
            } else if (server_name != NULL) {
                Ztls_buffer_pushv(buf, server_name, strlen(server_name));
            }
        });
        /* alpn */
        Ztls_buffer_push_block(buf, 1, {
            if (negotiated_protocol != NULL)
                Ztls_buffer_pushv(buf, negotiated_protocol, strlen(negotiated_protocol));
        });
    });

Exit:
    return ret;
}

int decode_session_identifier(uint64_t *issued_at, Bytes *psk, uint32_t *ticket_age_add, Bytes *ticket_ctx,
                              uint16_t *key_exchange_id, uint16_t *csid, Bytes *negotiated_protocol, const uint8_t *src,
                              const uint8_t *const end)
{
    int ret = 0;

    Ztls_decode_block(src, end, 2, {
        if (end - src < SESSION_IDENTIFIER_MAGIC_SIZE ||
            memcmp(src, SESSION_IDENTIFIER_MAGIC, SESSION_IDENTIFIER_MAGIC_SIZE) != 0) {
            ret = Ztls_ALERT_DECODE_ERROR;
            goto Exit;
        }
        src += SESSION_IDENTIFIER_MAGIC_SIZE;
        if ((ret = decode64(issued_at, &src, end)) != 0)
            goto Exit;
        Ztls_decode_open_block(src, end, 2, {
            *psk = wire_bytes(src, end - src);
            src = end;
        });
        if ((ret = decode16(key_exchange_id, &src, end)) != 0)
            goto Exit;
        if ((ret = decode16(csid, &src, end)) != 0)
            goto Exit;
        if ((ret = decode32(ticket_age_add, &src, end)) != 0)
            goto Exit;
        Ztls_decode_open_block(src, end, 2, {
            *ticket_ctx = wire_bytes(src, end - src);
            src = end;
        });
        Ztls_decode_open_block(src, end, 1, {
            *negotiated_protocol = wire_bytes(src, end - src);
            src = end;
        });
    });

Exit:
    return ret;
}

static size_t build_certificate_verify_signdata(uint8_t *data, key_schedule_t *sched, const char *context_string)
{
    size_t datalen = 0;

    memset(data + datalen, 32, 64);
    datalen += 64;
    memcpy(data + datalen, context_string, strlen(context_string) + 1);
    datalen += strlen(context_string) + 1;
    sched->hashes[0].ctx->final(sched->hashes[0].ctx, data + datalen, HASH_FINAL_MODE_SNAPSHOT);
    datalen += sched->hashes[0].algo->digest_size;
    assert(datalen <= Ztls_MAX_CERTIFICATE_VERIFY_SIGNDATA_SIZE);

    return datalen;
}

static int calc_verify_data(void *output, key_schedule_t *sched, const void *secret)
{
    hash_context_t *hmac;
    uint8_t digest[Ztls_MAX_DIGEST_SIZE];
    int ret;

    if ((ret = hkdf_expand_label(sched->hashes[0].algo, digest, sched->hashes[0].algo->digest_size,
                                      wire_bytes(secret, sched->hashes[0].algo->digest_size), "finished",
                                      wire_bytes(NULL, 0), NULL)) != 0)
        return ret;
    if ((hmac = hmac_create(sched->hashes[0].algo, digest, sched->hashes[0].algo->digest_size)) == NULL) {
        ZuClear(digest, sizeof(digest));
        return Ztls_ERROR_NO_MEMORY;
    }

    sched->hashes[0].ctx->final(sched->hashes[0].ctx, digest, HASH_FINAL_MODE_SNAPSHOT);
    hmac->update(hmac, digest, sched->hashes[0].algo->digest_size);
    ZuClear(digest, sizeof(digest));
    hmac->final(hmac, output, HASH_FINAL_MODE_FREE);

    return 0;
}

static int verify_finished(Ztls::Cxn *tls, Bytes message)
{
    uint8_t verify_data[Ztls_MAX_DIGEST_SIZE];
    int ret;

    if (Ztls_HANDSHAKE_HEADER_SIZE + tls->key_schedule->hashes[0].algo->digest_size != message.length()) {
        ret = Ztls_ALERT_DECODE_ERROR;
        goto Exit;
    }

    if ((ret = calc_verify_data(verify_data, tls->key_schedule, tls->rx_traffic.secret)) != 0)
        goto Exit;
    if (!Ztls::ctEqual({message.data() + Ztls_HANDSHAKE_HEADER_SIZE, tls->key_schedule->hashes[0].algo->digest_size},
                         {verify_data, tls->key_schedule->hashes[0].algo->digest_size})) {
        ret = Ztls_ALERT_HANDSHAKE_FAILURE;
        goto Exit;
    }

Exit:
    ZuClear(verify_data, sizeof(verify_data));
    return ret;
}

static int send_finished(Ztls::Cxn *tls, message_emitter_t *emitter)
{
    int ret;

    Ztls_push_message(emitter, tls->key_schedule, Ztls_HANDSHAKE_TYPE_FINISHED, {
        if ((ret = buffer_reserve(emitter->buf, tls->key_schedule->hashes[0].algo->digest_size, 1)) != 0)
            goto Exit;
        if ((ret = calc_verify_data(emitter->buf->base + emitter->buf->off, tls->key_schedule,
                                    tls->tx_traffic.secret)) != 0)
            goto Exit;
        emitter->buf->off += tls->key_schedule->hashes[0].algo->digest_size;
    });

Exit:
    return ret;
}

static int send_session_ticket(Ztls::Cxn *tls, message_emitter_t *emitter)
{
    hash_context_t *msghash_backup = tls->key_schedule->hashes[0].ctx->clone_(tls->key_schedule->hashes[0].ctx);
    buffer_t session_id;
    char session_id_smallbuf[128];
    uint32_t ticket_age_add;
    int ret = 0;

    assert(tls->ctx->ticket_lifetime != 0);
    assert(tls->ctx->encrypt_ticket != NULL);

    buffer_init_tx(&session_id, session_id_smallbuf, sizeof(session_id_smallbuf));

    { /* calculate verify-data that will be sent by the client */
        size_t orig_off = emitter->buf->off;
        if (tls->pending_handshake_secret.data() != NULL && !tls->ctx->omit_end_of_early_data) {
            assert(tls->state == Ztls_STATE_SERVER_EXPECT_END_OF_EARLY_DATA);
            Ztls_buffer_push_message_body(emitter->buf, tls->key_schedule, Ztls_HANDSHAKE_TYPE_END_OF_EARLY_DATA, {});
            emitter->buf->off = orig_off;
        }
        Ztls_buffer_push_message_body(emitter->buf, tls->key_schedule, Ztls_HANDSHAKE_TYPE_FINISHED, {
            if ((ret = buffer_reserve(emitter->buf, tls->key_schedule->hashes[0].algo->digest_size, 1)) != 0)
                goto Exit;
            if ((ret = calc_verify_data(emitter->buf->base + emitter->buf->off, tls->key_schedule,
                                        tls->pending_handshake_secret.data() != NULL ? tls->pending_handshake_secret.data()
                                                                              : tls->rx_traffic.secret)) != 0)
                goto Exit;
            emitter->buf->off += tls->key_schedule->hashes[0].algo->digest_size;
        });
        emitter->buf->off = orig_off;
    }

    tls->ctx->random_bytes(&ticket_age_add, sizeof(ticket_age_add));

    /* build the raw nsk */
    if (tls->key_share != NULL && (ret = encode_session_identifier(tls->ctx, &session_id, ticket_age_add, wire_bytes(NULL, 0),
                                                                   tls->key_schedule, tls->server_name.data(), tls->key_share->id,
                                                                   tls->cipher_suite->id, tls->negotiated_protocol.data())) != 0)
        goto Exit;

    /* encrypt and send */
    Ztls_push_message(emitter, tls->key_schedule, Ztls_HANDSHAKE_TYPE_NEW_SESSION_TICKET, {
        Ztls_buffer_push32(emitter->buf, tls->ctx->ticket_lifetime);
        Ztls_buffer_push32(emitter->buf, ticket_age_add);
        Ztls_buffer_push_block(emitter->buf, 1, {});
        Ztls_buffer_push_block(emitter->buf, 2, {
            if ((ret = tls->ctx->encrypt_ticket->cb(tls->ctx->encrypt_ticket, tls, 1, emitter->buf,
                                                    wire_bytes(session_id.base, session_id.off))) != 0)
                goto Exit;
        });
        Ztls_buffer_push_block(emitter->buf, 2, {
            if (tls->ctx->max_early_data_size != 0)
                buffer_push_extension(emitter->buf, Ztls_EXTENSION_TYPE_EARLY_DATA,
                                      { Ztls_buffer_push32(emitter->buf, tls->ctx->max_early_data_size); });
        });
    });

Exit:
    buffer_dispose(&session_id);

    /* restore handshake state */
    tls->key_schedule->hashes[0].ctx->final(tls->key_schedule->hashes[0].ctx, NULL, HASH_FINAL_MODE_FREE);
    tls->key_schedule->hashes[0].ctx = msghash_backup;

    return ret;
}

static int push_change_cipher_spec(Ztls::Cxn *tls, message_emitter_t *emitter)
{
    TrafficProtection clear = {};
    const uint8_t value = 1;
    int ret;

    /* check if we are requested to (or still need to) */
    if (!tls->send_change_cipher_spec) {
        ret = 0;
        goto Exit;
    }

    /* CCS is a record, can only be sent when using a record-based protocol. */
    if (emitter->begin_message != begin_record_message) {
        ret = Ztls_ALERT_UNEXPECTED_MESSAGE;
        goto Exit;
    }

    /* emit CCS */
    if ((ret = buffer_record(emitter->buf, &clear, Ztls_CONTENT_TYPE_CHANGE_CIPHER_SPEC, &value, 1)) != 0)
        goto Exit;

    tls->send_change_cipher_spec = 0;
    ret = 0;
Exit:
    return ret;
}

static int push_additional_extensions(handshake_properties_t *properties, buffer_t *sendbuf)
{
    int ret;

    if (properties != NULL && properties->additional_extensions != NULL) {
        raw_extension_t *ext;
        for (ext = properties->additional_extensions; ext->type != UINT16_MAX; ++ext) {
            buffer_push_extension(sendbuf, ext->type, { Ztls_buffer_pushv(sendbuf, ext->data.data(), ext->data.length()); });
        }
    }
    ret = 0;
Exit:
    return ret;
}

static int push_signature_algorithms(verify_certificate_t *vc, buffer_t *sendbuf)
{
    /* The list sent when verify callback is not registered */
    static const uint16_t default_algos[] = {Ztls_SIGNATURE_RSA_PSS_RSAE_SHA384, Ztls_SIGNATURE_RSA_PSS_RSAE_SHA256,
                                             Ztls_SIGNATURE_ECDSA_SECP384R1_SHA384, Ztls_SIGNATURE_ECDSA_SECP256R1_SHA256,
                                             Ztls_SIGNATURE_RSA_PKCS1_SHA256, Ztls_SIGNATURE_RSA_PKCS1_SHA1, UINT16_MAX};
    int ret;

    Ztls_buffer_push_block(sendbuf, 2, {
        for (const uint16_t *p = vc != NULL ? vc->algos : default_algos; *p != UINT16_MAX; ++p)
            Ztls_buffer_push16(sendbuf, *p);
    });

    ret = 0;
Exit:
    return ret;
}

static int decode_signature_algorithms(SignatureAlgorithms *sa, const uint8_t **src, const uint8_t *end)
{
    int ret;

    Ztls_decode_block(*src, end, 2, {
        do {
            uint16_t id;
            if ((ret = decode16(&id, src, end)) != 0)
                goto Exit;
            if (sa->count < Ztls_ELEMENTSOF(sa->list))
                sa->list[sa->count++] = id;
        } while (*src != end);
    });

    ret = 0;
Exit:
    return ret;
}

/**
 * @param hash optional argument for restricting the underlying hash algorithm
 */
static int select_cipher(cipher_suite_t **selected, cipher_suite_t **candidates, const uint8_t *src,
                         const uint8_t *const end, int server_preference, int server_chacha_priority, hash_algorithm_t *hash)
{
    size_t found_index = SIZE_MAX;
    int ret;

    while (src != end) {
        uint16_t id;
        if ((ret = decode16(&id, &src, end)) != 0)
            goto Exit;
        for (size_t i = 0; candidates[i] != NULL; ++i) {
            if (candidates[i]->id == id && (hash == NULL || candidates[i]->hash == hash)) {
                if (server_preference && !(server_chacha_priority && id == Ztls_CIPHER_SUITE_CHACHA20_POLY1305_SHA256)) {
                    /* preserve smallest matching index, and proceed to the next input */
                    if (i < found_index) {
                        found_index = i;
                        break;
                    }
                } else {
                    /* return the pointer matching to the first input that can be used */
                    *selected = candidates[i];
                    goto Exit;
                }
            }
        }
        /* first position of the server list matched (server_preference) */
        if (found_index == 0)
            break;
        /* server preference is overridden only if the first entry of client-provided list is chachapoly */
        server_chacha_priority = 0;
    }
    if (found_index != SIZE_MAX) {
        *selected = candidates[found_index];
        ret = 0;
    } else {
        ret = Ztls_ALERT_HANDSHAKE_FAILURE;
    }

Exit:
    return ret;
}

static int push_key_share_entry(buffer_t *buf, uint16_t group, Bytes pubkey)
{
    int ret;

    Ztls_buffer_push16(buf, group);
    Ztls_buffer_push_block(buf, 2, { Ztls_buffer_pushv(buf, pubkey.data(), pubkey.length()); });
    ret = 0;
Exit:
    return ret;
}

static int decode_key_share_entry(uint16_t *group, Bytes *key_exchange, const uint8_t **src, const uint8_t *const end)
{
    int ret;

    if ((ret = decode16(group, src, end)) != 0)
        goto Exit;
    Ztls_decode_open_block(*src, end, 2, {
        *key_exchange = wire_bytes(*src, end - *src);
        *src = end;
    });

Exit:
    return ret;
}

static int select_key_share(key_exchange_algorithm_t **selected, Bytes *peer_key,
                            key_exchange_algorithm_t **candidates, const uint8_t **src, const uint8_t *const end,
                            int expect_one)
{
    int ret;

    *selected = NULL;

    if (expect_one && *src == end) {
        ret = Ztls_ALERT_ILLEGAL_PARAMETER;
        goto Exit;
    }

    while (*src != end) {
        uint16_t group;
        Bytes key;
        if ((ret = decode_key_share_entry(&group, &key, src, end)) != 0)
            goto Exit;
        key_exchange_algorithm_t **c = candidates;
        for (; *c != NULL; ++c) {
            if (*selected == NULL && (*c)->id == group) {
                *selected = *c;
                *peer_key = key;
            }
        }
        if (expect_one) {
            ret = *selected != NULL ? 0 : Ztls_ALERT_ILLEGAL_PARAMETER;
            goto Exit;
        }
    }

    ret = 0;

Exit:
    return ret;
}

static int emit_server_name_extension(buffer_t *buf, const char *server_name)
{
    int ret;

    Ztls_buffer_push_block(buf, 2, {
        Ztls_buffer_push(buf, Ztls_SERVER_NAME_TYPE_HOSTNAME);
        Ztls_buffer_push_block(buf, 2, { Ztls_buffer_pushv(buf, server_name, strlen(server_name)); });
    });

    ret = 0;
Exit:
    return ret;
}

/**
 * Within the outer ECH extension, returns the number of bytes that preceeds the AEAD-encrypted payload.
 */
static inline size_t outer_ech_header_size(size_t enc_size)
{
    return 10 + enc_size;
}

/**
 * Flag to indicate which of ClientHelloInner, EncodedClientHelloInner, ClientHelloOuter is to be generated. When ECH is inactive,
 * only ClientHelloInner is used.
 */
enum encode_ch_mode { ENCODE_CH_MODE_INNER, ENCODE_CH_MODE_ENCODED_INNER, ENCODE_CH_MODE_OUTER };

static int encode_client_hello(context_t *ctx, buffer_t *sendbuf, enum encode_ch_mode mode, int is_second_flight,
                               handshake_properties_t *properties, const void *client_random,
                               key_exchange_context_t *key_share_ctx, const char *sni_name, Bytes legacy_session_id,
                               Ech *ech, size_t *ech_size_offset, Bytes ech_replay, Bytes psk_secret,
                               Bytes psk_identity, uint32_t obfuscated_ticket_age, size_t psk_binder_size,
                               Bytes *cookie, int using_early_data)
{
    int ret;

    assert(mode == ENCODE_CH_MODE_INNER || ech != NULL);

    Ztls_buffer_push_message_body(sendbuf, NULL, Ztls_HANDSHAKE_TYPE_CLIENT_HELLO, {
        /* legacy_version */
        Ztls_buffer_push16(sendbuf, 0x0303);
        /* random_bytes */
        Ztls_buffer_pushv(sendbuf, client_random, Ztls_HELLO_RANDOM_SIZE);
        /* lecagy_session_id */
        Ztls_buffer_push_block(sendbuf, 1, {
            if (mode != ENCODE_CH_MODE_ENCODED_INNER)
                Ztls_buffer_pushv(sendbuf, legacy_session_id.data(), legacy_session_id.length());
        });
        /* cipher_suites */
        Ztls_buffer_push_block(sendbuf, 2, {
            cipher_suite_t **cs = ctx->cipher_suites;
            for (; *cs != NULL; ++cs)
                Ztls_buffer_push16(sendbuf, (*cs)->id);
        });
        /* legacy_compression_methods */
        Ztls_buffer_push_block(sendbuf, 1, { Ztls_buffer_push(sendbuf, 0); });
        /* extensions */
        Ztls_buffer_push_block(sendbuf, 2, {
            if (mode == ENCODE_CH_MODE_OUTER) {
                buffer_push_extension(sendbuf, Ztls_EXTENSION_TYPE_ENCRYPTED_CLIENT_HELLO, {
                    size_t ext_payload_from = sendbuf->off;
                    Ztls_buffer_push(sendbuf, Ztls_ECH_CLIENT_HELLO_TYPE_OUTER);
                    Ztls_buffer_push16(sendbuf, ech->cipher->id.kdf);
                    Ztls_buffer_push16(sendbuf, ech->cipher->id.aead);
                    Ztls_buffer_push(sendbuf, ech->config_id);
                    Ztls_buffer_push_block(sendbuf, 2, {
                        if (!is_second_flight)
                            Ztls_buffer_pushv(sendbuf, ech->client.enc.data(), ech->client.enc.length());
                    });
                    Ztls_buffer_push_block(sendbuf, 2, {
                        assert(sendbuf->off - ext_payload_from ==
                               outer_ech_header_size(is_second_flight ? 0 : ech->client.enc.length()));
                        if ((ret = buffer_reserve(sendbuf, *ech_size_offset, 1)) != 0)
                            goto Exit;
                        memset(sendbuf->base + sendbuf->off, 0, *ech_size_offset);
                        sendbuf->off += *ech_size_offset;
                        *ech_size_offset = sendbuf->off - *ech_size_offset;
                    });
                });
            } else if (ech->aead != NULL) {
                buffer_push_extension(sendbuf, Ztls_EXTENSION_TYPE_ENCRYPTED_CLIENT_HELLO,
                                      { Ztls_buffer_push(sendbuf, Ztls_ECH_CLIENT_HELLO_TYPE_INNER); });
            } else if (ech_replay.data() != NULL) {
                buffer_push_extension(sendbuf, Ztls_EXTENSION_TYPE_ENCRYPTED_CLIENT_HELLO,
                                      { Ztls_buffer_pushv(sendbuf, ech_replay.data(), ech_replay.length()); });
            }
            if (mode == ENCODE_CH_MODE_ENCODED_INNER) {
                buffer_push_extension(sendbuf, Ztls_EXTENSION_TYPE_ECH_OUTER_EXTENSIONS, {
                    Ztls_buffer_push_block(sendbuf, 1, { Ztls_buffer_push16(sendbuf, Ztls_EXTENSION_TYPE_KEY_SHARE); });
                });
            } else {
                buffer_push_extension(sendbuf, Ztls_EXTENSION_TYPE_KEY_SHARE, {
                    Ztls_buffer_push_block(sendbuf, 2, {
                        if (key_share_ctx != NULL &&
                            (ret = push_key_share_entry(sendbuf, key_share_ctx->algo->id, key_share_ctx->pubkey)) != 0)
                            goto Exit;
                    });
                });
            }
            if (sni_name != NULL) {
                buffer_push_extension(sendbuf, Ztls_EXTENSION_TYPE_SERVER_NAME, {
                    if ((ret = emit_server_name_extension(sendbuf, sni_name)) != 0)
                        goto Exit;
                });
            }
            if (properties != NULL && properties->client.negotiated_protocols.count != 0) {
                buffer_push_extension(sendbuf, Ztls_EXTENSION_TYPE_ALPN, {
                    Ztls_buffer_push_block(sendbuf, 2, {
                        size_t i;
                        for (i = 0; i != properties->client.negotiated_protocols.count; ++i) {
                            Ztls_buffer_push_block(sendbuf, 1, {
                                Bytes p = properties->client.negotiated_protocols.list[i];
                                Ztls_buffer_pushv(sendbuf, p.data(), p.length());
                            });
                        }
                    });
                });
            }
            if (ctx->decompress_certificate != NULL) {
                buffer_push_extension(sendbuf, Ztls_EXTENSION_TYPE_COMPRESS_CERTIFICATE, {
                    Ztls_buffer_push_block(sendbuf, 1, {
                        const uint16_t *algo = ctx->decompress_certificate->supported_algorithms;
                        assert(*algo != UINT16_MAX);
                        for (; *algo != UINT16_MAX; ++algo)
                            Ztls_buffer_push16(sendbuf, *algo);
                    });
                });
            }
            buffer_push_extension(sendbuf, Ztls_EXTENSION_TYPE_SUPPORTED_VERSIONS, {
                Ztls_buffer_push_block(sendbuf, 1, {
                    size_t i;
                    for (i = 0; i != Ztls_ELEMENTSOF(supported_versions); ++i)
                        Ztls_buffer_push16(sendbuf, supported_versions[i]);
                });
            });
            buffer_push_extension(sendbuf, Ztls_EXTENSION_TYPE_SIGNATURE_ALGORITHMS, {
                if ((ret = push_signature_algorithms(ctx->verify_certificate, sendbuf)) != 0)
                    goto Exit;
            });
            if (ctx->key_exchanges != NULL) {
                buffer_push_extension(sendbuf, Ztls_EXTENSION_TYPE_SUPPORTED_GROUPS, {
                    key_exchange_algorithm_t **algo = ctx->key_exchanges;
                    Ztls_buffer_push_block(sendbuf, 2, {
                        for (; *algo != NULL; ++algo)
                            Ztls_buffer_push16(sendbuf, (*algo)->id);
                    });
                });
            }
            if (cookie != NULL && cookie->data() != NULL) {
                buffer_push_extension(sendbuf, Ztls_EXTENSION_TYPE_COOKIE, {
                    Ztls_buffer_push_block(sendbuf, 2, { Ztls_buffer_pushv(sendbuf, cookie->data(), cookie->length()); });
                });
            }
            if (ctx->use_raw_public_keys) {
                buffer_push_extension(sendbuf, Ztls_EXTENSION_TYPE_SERVER_CERTIFICATE_TYPE, {
                    Ztls_buffer_push_block(sendbuf, 1, { Ztls_buffer_push(sendbuf, Ztls_CERTIFICATE_TYPE_RAW_PUBLIC_KEY); });
                });
            }
            if (ctx->save_ticket != NULL &&
                (ctx->ticket_requests.client.new_session_count != 0 || ctx->ticket_requests.client.resumption_count != 0)) {
                buffer_push_extension(sendbuf, Ztls_EXTENSION_TYPE_TICKET_REQUEST, {
                    Ztls_buffer_push(sendbuf, ctx->ticket_requests.client.new_session_count,
                                     ctx->ticket_requests.client.resumption_count);
                });
            }
            if ((ret = push_additional_extensions(properties, sendbuf)) != 0)
                goto Exit;
            if (ctx->save_ticket != NULL || psk_secret.data() != NULL) {
                buffer_push_extension(sendbuf, Ztls_EXTENSION_TYPE_PSK_KEY_EXCHANGE_MODES, {
                    Ztls_buffer_push_block(sendbuf, 1, {
                        if (!ctx->require_dhe_on_psk)
                            Ztls_buffer_push(sendbuf, Ztls_PSK_KE_MODE_PSK);
                        Ztls_buffer_push(sendbuf, Ztls_PSK_KE_MODE_PSK_DHE);
                    });
                });
            }
            if (psk_secret.data() != NULL) {
                if (using_early_data && !is_second_flight)
                    buffer_push_extension(sendbuf, Ztls_EXTENSION_TYPE_EARLY_DATA, {});
                /* pre-shared key "MUST be the last extension in the ClientHello" (draft-17 section 4.2.6) */
                buffer_push_extension(sendbuf, Ztls_EXTENSION_TYPE_PRE_SHARED_KEY, {
                    Ztls_buffer_push_block(sendbuf, 2, {
                        Ztls_buffer_push_block(sendbuf, 2, {
                            if (mode == ENCODE_CH_MODE_OUTER && ech->state != Ztls_ECH_STATE_GREASE) {
                                if ((ret = buffer_reserve(sendbuf, psk_identity.length(), 1)) != 0)
                                    goto Exit;
                                ctx->random_bytes(sendbuf->base + sendbuf->off, psk_identity.length());
                                sendbuf->off += psk_identity.length();
                            } else {
                                Ztls_buffer_pushv(sendbuf, psk_identity.data(), psk_identity.length());
                            }
                        });
                        uint32_t age;
                        if (mode == ENCODE_CH_MODE_OUTER && ech->state != Ztls_ECH_STATE_GREASE) {
                            ctx->random_bytes(&age, sizeof(age));
                        } else {
                            age = obfuscated_ticket_age;
                        }
                        Ztls_buffer_push32(sendbuf, age);
                    });
                    /* allocate space for PSK binder. The space is filled initially filled by a random value (meeting the
                     * requirement of ClientHelloOuter), and later gets filled with the correct binder value if necessary. */
                    Ztls_buffer_push_block(sendbuf, 2, {
                        Ztls_buffer_push_block(sendbuf, 1, {
                            if ((ret = buffer_reserve(sendbuf, psk_binder_size, 1)) != 0)
                                goto Exit;
                            ctx->random_bytes(sendbuf->base + sendbuf->off, psk_binder_size);
                            sendbuf->off += psk_binder_size;
                        });
                    });
                });
            }
        });
    });

Exit:
    return ret;
}

/**
 * Feeds the CH message into the hash, computing the PSK binder if necessary. `binder_key` must be derived before calling this
 * function.
 */
static int update_ch_hash_and_binder(key_schedule_t *ks, uint8_t *ch, size_t ch_start, size_t ch_end, int has_psk,
                                     uint8_t *binder_key, int is_outer)
{
    int ret = 0;
    size_t hash_off = ch_start;

    if (has_psk) {
        size_t psk_binder_off = ch_end - (3 + ks->hashes[0].algo->digest_size);
        _key_schedule_update_hash(ks, ch + hash_off, psk_binder_off - hash_off, is_outer);
        hash_off = psk_binder_off;
        if ((ret = calc_verify_data(ch + psk_binder_off + 3, ks, binder_key)) != 0)
            return ret;
    }
    _key_schedule_update_hash(ks, ch + hash_off, ch_end - hash_off, is_outer);

    return ret;
}

static int send_client_hello(Ztls::Cxn *tls, message_emitter_t *emitter, handshake_properties_t *properties,
                             Bytes *cookie)
{
    struct {
        Bytes secret;
        Bytes identity;
        const char *label;
    } psk{};
    uint32_t obfuscated_ticket_age = 0;
    const char *sni_name = NULL;
    size_t mess_start;
    uint8_t binder_key[Ztls_MAX_DIGEST_SIZE];
    buffer_t encoded_ch_inner;
    int ret, is_second_flight = tls->key_schedule != NULL;

    buffer_init_tx(&encoded_ch_inner, const_cast<char *>(""), 0);

    if (tls->server_name.data() != NULL && !server_name_is_ipaddr(tls->server_name.data()))
        sni_name = tls->server_name.data();

    /* try to use ECH (ignore broken ECHConfigList; it is delivered insecurely) */
    if (properties != NULL) {
        if (!is_second_flight && sni_name != NULL && tls->ctx->ech.client.ciphers != NULL) {
            if (properties->client.ech.configs.length() != 0) {
                struct st_decoded_ech_config_t decoded;
                client_decode_ech_config_list(tls->ctx, &decoded, properties->client.ech.configs);
                if (decoded.kem != NULL && decoded.cipher != NULL) {
                    if ((ret = client_setup_ech(&tls->ech, &decoded, tls->ctx->random_bytes)) != 0)
                        goto Exit;
                }
            } else if (properties->client.ech.configs.data() != NULL) {
                /* zero-length config with non-NULL base indicates ECH greasing; NULL base means no ECH */
                client_setup_ech_grease(&tls->ech, tls->ctx->random_bytes, tls->ctx->ech.client.kems, tls->ctx->ech.client.ciphers,
                                        sni_name);
                tls->ech.state = Ztls_ECH_STATE_GREASE;
            }
        }
    }

    /* use external PSK if provided */
    if (tls->ctx->pre_shared_key.identity.data() != NULL) {
        if (!is_second_flight) {
            tls->offered_psk = 1;
            for (size_t i = 0; tls->ctx->cipher_suites[i] != NULL; ++i) {
                if (tls->ctx->cipher_suites[i]->hash == tls->ctx->pre_shared_key.hash) {
                    tls->cipher_suite = tls->ctx->cipher_suites[i];
                    break;
                }
            }
            assert(tls->cipher_suite != NULL && "no compatible cipher-suite provided that matches psk.hash");
            if (properties != NULL && properties->client.max_early_data_size != NULL) {
                tls->using_early_data = 1;
                *properties->client.max_early_data_size = SIZE_MAX;
            }
        } else {
            assert(tls->cipher_suite != NULL && tls->cipher_suite->hash == tls->ctx->pre_shared_key.hash);
        }
        psk.secret = tls->ctx->pre_shared_key.secret;
        psk.identity = tls->ctx->pre_shared_key.identity;
        psk.label = "ext binder";
    }

    /* try to setup resumption-related data, unless external PSK is used */
    if (psk.secret.data() == NULL && properties != NULL && properties->client.session_ticket.data() != NULL &&
        tls->ctx->key_exchanges != NULL) {
        key_exchange_algorithm_t *key_share = NULL;
        cipher_suite_t *cipher_suite = NULL;
        uint32_t max_early_data_size;
        if (decode_stored_session_ticket(tls, &key_share, &cipher_suite, &psk.secret, &obfuscated_ticket_age, &psk.identity,
                                         &max_early_data_size, properties->client.session_ticket.data(),
                                         properties->client.session_ticket.data() + properties->client.session_ticket.length()) == 0) {
            psk.label = "res binder";
            tls->offered_psk = 1;
            /* key-share selected by HRR should not be overridden */
            if (tls->key_share == NULL)
                tls->key_share = key_share;
            tls->cipher_suite = cipher_suite;
            if (!is_second_flight && max_early_data_size != 0 && properties->client.max_early_data_size != NULL) {
                tls->using_early_data = 1;
                *properties->client.max_early_data_size = max_early_data_size;
            }
        } else {
            psk.secret = wire_bytes(NULL, 0);
        }
    }

    /* send 0-RTT related signals back to the client */
    if (properties != NULL) {
        if (tls->using_early_data) {
            properties->client.early_data_acceptance = EARLY_DATA_ACCEPTANCE_UNKNOWN;
        } else {
            if (properties->client.max_early_data_size != NULL)
                *properties->client.max_early_data_size = 0;
            properties->client.early_data_acceptance = EARLY_DATA_REJECTED;
        }
    }

    /* use the default key share if still not undetermined */
    if (tls->key_share == NULL && tls->ctx->key_exchanges != NULL &&
        !(properties != NULL && properties->client.negotiate_before_key_exchange))
        tls->key_share = tls->ctx->key_exchanges[0];

    /* instantiate key share context */
    assert(tls->key_share_ctx == NULL);
    if (tls->key_share != NULL) {
        if ((ret = tls->key_share->create(tls->key_share, &tls->key_share_ctx)) != 0)
            goto Exit;
    }

    /* initialize key schedule */
    if (!is_second_flight) {
        if ((tls->key_schedule = key_schedule_new(tls->cipher_suite, tls->ctx->cipher_suites, tls->ech.aead != NULL)) == NULL) {
            ret = Ztls_ERROR_NO_MEMORY;
            goto Exit;
        }
        if ((ret = key_schedule_extract(tls->key_schedule, psk.secret)) != 0)
            goto Exit;
    }

    /* start generating CH */
    if ((ret = emitter->begin_message(emitter)) != 0)
        goto Exit;
    mess_start = emitter->buf->off;

    /* generate true (inner) CH */
    if ((ret = encode_client_hello(tls->ctx, emitter->buf, ENCODE_CH_MODE_INNER, is_second_flight, properties,
                                   tls->ech.aead != NULL ? tls->ech.inner_client_random : tls->client_random,
                                   tls->key_share_ctx, sni_name, tls->legacy_session_id, &tls->ech, NULL,
                                   wire_bytes(tls->ech.client.first_ech.data(), tls->ech.client.first_ech.length()), psk.secret, psk.identity, obfuscated_ticket_age,
                                   tls->key_schedule->hashes[0].algo->digest_size, cookie, tls->using_early_data)) != 0)
        goto Exit;

    /* update the message hash, filling in the PSK binder HMAC if necessary */
    if (psk.secret.data() != NULL) {
        if ((ret = derive_secret_with_empty_digest(tls->key_schedule, binder_key, psk.label)) != 0)
            goto Exit;
    }
    if ((ret = update_ch_hash_and_binder(tls->key_schedule, emitter->buf->base, mess_start, emitter->buf->off,
                                         psk.secret.data() != NULL, binder_key, 0)) != 0)
        goto Exit;

    /* ECH */
    if (tls->ech.aead != NULL) {
        /* build EncodedCHInner */
        if ((ret = encode_client_hello(tls->ctx, &encoded_ch_inner, ENCODE_CH_MODE_ENCODED_INNER, is_second_flight, properties,
                                       tls->ech.inner_client_random, tls->key_share_ctx, sni_name,
                                       tls->legacy_session_id, &tls->ech, NULL, wire_bytes(NULL, 0), psk.secret,
                                       psk.identity, obfuscated_ticket_age, tls->key_schedule->hashes[0].algo->digest_size, cookie,
                                       tls->using_early_data)) != 0)
            goto Exit;
        if (psk.secret.data() != NULL)
            memcpy(encoded_ch_inner.base + encoded_ch_inner.off - tls->key_schedule->hashes[0].algo->digest_size,
                   emitter->buf->base + emitter->buf->off - tls->key_schedule->hashes[0].algo->digest_size,
                   tls->key_schedule->hashes[0].algo->digest_size);
        { /* pad EncodedCHInner (following draft-ietf-tls-esni-15 6.1.3) */
            size_t padding_len;
            if (sni_name != NULL) {
                padding_len = strlen(sni_name);
                if (padding_len < tls->ech.client.max_name_length)
                    padding_len = tls->ech.client.max_name_length;
            } else {
                padding_len = tls->ech.client.max_name_length + 9;
            }
            size_t final_len = encoded_ch_inner.off - Ztls_HANDSHAKE_HEADER_SIZE + padding_len;
            final_len = (final_len + 31) / 32 * 32;
            padding_len = final_len - (encoded_ch_inner.off - Ztls_HANDSHAKE_HEADER_SIZE);
            if (padding_len != 0) {
                if ((ret = buffer_reserve(&encoded_ch_inner, padding_len, 1)) != 0)
                    goto Exit;
                memset(encoded_ch_inner.base + encoded_ch_inner.off, 0, padding_len);
                encoded_ch_inner.off += padding_len;
            }
        }
        /* flush CHInner, build CHOuterAAD */
        emitter->buf->off = mess_start;
        size_t ech_payload_size = encoded_ch_inner.off - Ztls_HANDSHAKE_HEADER_SIZE + tls->ech.aead->algo->tag_size,
               ech_size_offset = ech_payload_size;
        if ((ret = encode_client_hello(tls->ctx, emitter->buf, ENCODE_CH_MODE_OUTER, is_second_flight, properties,
                                       tls->client_random, tls->key_share_ctx, tls->ech.client.public_name.data(),
                                       tls->legacy_session_id, &tls->ech, &ech_size_offset, wire_bytes(NULL, 0),
                                       psk.secret, psk.identity, obfuscated_ticket_age,
                                       tls->key_schedule->hashes[0].algo->digest_size, cookie, tls->using_early_data)) != 0)
            goto Exit;
        /* overwrite ECH payload */
        aead_encrypt(tls->ech.aead, emitter->buf->base + ech_size_offset, encoded_ch_inner.base + Ztls_HANDSHAKE_HEADER_SIZE,
                          encoded_ch_inner.off - Ztls_HANDSHAKE_HEADER_SIZE, is_second_flight,
                          emitter->buf->base + mess_start + Ztls_HANDSHAKE_HEADER_SIZE,
                          emitter->buf->off - (mess_start + Ztls_HANDSHAKE_HEADER_SIZE));
        /* keep the copy of the 1st ECH extension so that we can send it again in 2nd CH in response to rejection with HRR */
        if (!is_second_flight) {
            size_t len = outer_ech_header_size(tls->ech.client.enc.length()) + ech_payload_size;
            tls->ech.client.first_ech.copy(
                emitter->buf->base + ech_size_offset - outer_ech_header_size(tls->ech.client.enc.length()), len);
            if (tls->ech.state != Ztls_ECH_STATE_GREASE)
                tls->ech.state = Ztls_ECH_STATE_OFFERED;
        }
        if (tls->ech.state == Ztls_ECH_STATE_GREASE) {
            /* For grease ECH, the server sees the outer CH. Discard the inner state and adopt the outer, then compute the PSK
             * binder over the outer CH using the standard flow. */
            for (size_t i = 0; i < tls->key_schedule->num_hashes; ++i) {
                tls->key_schedule->hashes[i].ctx->final(tls->key_schedule->hashes[i].ctx, NULL, HASH_FINAL_MODE_FREE);
                tls->key_schedule->hashes[i].ctx = tls->key_schedule->hashes[i].ctx_outer;
                tls->key_schedule->hashes[i].ctx_outer = NULL;
            }
            aead_free(tls->ech.aead);
            tls->ech.aead = NULL;
            if ((ret = update_ch_hash_and_binder(tls->key_schedule, emitter->buf->base, mess_start, emitter->buf->off,
                                                     psk.secret.data() != NULL, binder_key, 0)) != 0)
                goto Exit;
        } else {
            /* update outer hash */
            _key_schedule_update_hash(tls->key_schedule, emitter->buf->base + mess_start, emitter->buf->off - mess_start, 1);
        }
    }

    /* commit CH to the record layer */
    if ((ret = emitter->commit_message(emitter)) != 0)
        goto Exit;

    if (tls->using_early_data) {
        assert(!is_second_flight);
        if ((ret = setup_traffic_protection(tls, 1, "c e traffic", 1, 0, 0)) != 0)
            goto Exit;
        if ((ret = push_change_cipher_spec(tls, emitter)) != 0)
            goto Exit;
    }
    if (psk.secret.data() != NULL && !is_second_flight) {
        if ((ret = derive_exporter_secret(tls, 1)) != 0)
            goto Exit;
    }
    tls->state = cookie == NULL ? Ztls_STATE_CLIENT_EXPECT_SERVER_HELLO : Ztls_STATE_CLIENT_EXPECT_SECOND_SERVER_HELLO;
    ret = Ztls_ERROR_IN_PROGRESS;

Exit:
    buffer_dispose(&encoded_ch_inner);
    ZuClear(binder_key, sizeof(binder_key));
    return ret;
}

cipher_suite_t *find_cipher_suite(cipher_suite_t **cipher_suites, uint16_t id)
{
    cipher_suite_t **cs;
    if (cipher_suites == NULL)
        return NULL;
    for (cs = cipher_suites; *cs != NULL && (*cs)->id != id; ++cs)
        ;
    return *cs;
}

static int decode_server_hello(Ztls::Cxn *tls, struct st_server_hello_t *sh, const uint8_t *src, const uint8_t *const end)
{
    uint16_t exttype, found_version = UINT16_MAX, selected_psk_identity = UINT16_MAX;
    int ret;

    *sh = (struct st_server_hello_t){{0}};

    /* ignore legacy-version */
    if (end - src < 2) {
        ret = Ztls_ALERT_DECODE_ERROR;
        goto Exit;
    }
    src += 2;

    /* random */
    if (end - src < Ztls_HELLO_RANDOM_SIZE) {
        ret = Ztls_ALERT_DECODE_ERROR;
        goto Exit;
    }
    sh->is_retry_request = memcmp(src, hello_retry_random, Ztls_HELLO_RANDOM_SIZE) == 0;
    src += Ztls_HELLO_RANDOM_SIZE;

    /* legacy_session_id */
    Ztls_decode_open_block(src, end, 1, {
        if (end - src > 32) {
            ret = Ztls_ALERT_DECODE_ERROR;
            goto Exit;
        }
        sh->legacy_session_id = wire_bytes(src, end - src);
        src = end;
    });

    { /* select cipher_suite */
        uint16_t csid;
        if ((ret = decode16(&csid, &src, end)) != 0)
            goto Exit;
        if ((tls->cipher_suite = find_cipher_suite(tls->ctx->cipher_suites, csid)) == NULL) {
            ret = Ztls_ALERT_ILLEGAL_PARAMETER;
            goto Exit;
        }
    }

    { /* legacy_compression_method */
        uint8_t method;
        if ((ret = decode8(&method, &src, end)) != 0)
            goto Exit;
        if (method != 0) {
            ret = Ztls_ALERT_ILLEGAL_PARAMETER;
            goto Exit;
        }
    }

    if (sh->is_retry_request)
        sh->retry_request.selected_group = UINT16_MAX;

    decode_extensions(src, end, sh->is_retry_request ? Ztls_HANDSHAKE_TYPE_PSEUDO_HRR : Ztls_HANDSHAKE_TYPE_SERVER_HELLO, &exttype,
                      {
                          if (tls->ctx->on_extension != NULL &&
                              (ret = tls->ctx->on_extension->cb(tls->ctx->on_extension, tls, Ztls_HANDSHAKE_TYPE_SERVER_HELLO,
                                                                exttype, wire_bytes(src, end - src)) != 0))
                              goto Exit;
                          switch (exttype) {
                          case Ztls_EXTENSION_TYPE_SUPPORTED_VERSIONS:
                              if ((ret = decode16(&found_version, &src, end)) != 0)
                                  goto Exit;
                              break;
                          case Ztls_EXTENSION_TYPE_KEY_SHARE:
                              if (tls->ctx->key_exchanges == NULL) {
                                  ret = Ztls_ALERT_HANDSHAKE_FAILURE;
                                  goto Exit;
                              }
                              if (sh->is_retry_request) {
                                  if ((ret = decode16(&sh->retry_request.selected_group, &src, end)) != 0)
                                      goto Exit;
                              } else {
                                  uint16_t group;
                                  if ((ret = decode_key_share_entry(&group, &sh->peerkey, &src, end)) != 0)
                                      goto Exit;
                                  if (src != end) {
                                      ret = Ztls_ALERT_DECODE_ERROR;
                                      goto Exit;
                                  }
                                  if (tls->key_share == NULL || tls->key_share->id != group) {
                                      ret = Ztls_ALERT_ILLEGAL_PARAMETER;
                                      goto Exit;
                                  }
                              }
                              break;
                          case Ztls_EXTENSION_TYPE_COOKIE:
                              assert(sh->is_retry_request);
                              Ztls_decode_block(src, end, 2, {
                                  if (src == end) {
                                      ret = Ztls_ALERT_DECODE_ERROR;
                                      goto Exit;
                                  }
                                  sh->retry_request.cookie = wire_bytes(src, end - src);
                                  src = end;
                              });
                              break;
                          case Ztls_EXTENSION_TYPE_PRE_SHARED_KEY:
                              assert(!sh->is_retry_request);
                              if ((ret = decode16(&selected_psk_identity, &src, end)) != 0)
                                  goto Exit;
                              break;
                          case Ztls_EXTENSION_TYPE_ENCRYPTED_CLIENT_HELLO:
                              assert(sh->is_retry_request);
                              if (tls->ech.state == Ztls_ECH_STATE_NONE) {
                                  ret = Ztls_ALERT_UNSUPPORTED_EXTENSION;
                                  goto Exit;
                              }
                              if (end - src != Ztls_ECH_CONFIRM_LENGTH) {
                                  ret = Ztls_ALERT_DECODE_ERROR;
                                  goto Exit;
                              }
                              sh->retry_request.ech = src;
                              src = end;
                              break;
                          default:
                              src = end;
                              break;
                          }
                      });

    if (!is_supported_version(found_version)) {
        ret = Ztls_ALERT_ILLEGAL_PARAMETER;
        goto Exit;
    }
    if (!sh->is_retry_request) {
        if (selected_psk_identity != UINT16_MAX) {
            if (!tls->offered_psk) {
                ret = Ztls_ALERT_ILLEGAL_PARAMETER;
                goto Exit;
            }
            if (selected_psk_identity != 0) {
                ret = Ztls_ALERT_ILLEGAL_PARAMETER;
                goto Exit;
            }
            tls->is_psk_handshake = 1;
        }
        if (sh->peerkey.data() == NULL && !tls->is_psk_handshake) {
            ret = Ztls_ALERT_ILLEGAL_PARAMETER;
            goto Exit;
        }
    }

    ret = 0;
Exit:
    return ret;
}

static int handle_hello_retry_request(Ztls::Cxn *tls, message_emitter_t *emitter, struct st_server_hello_t *sh,
                                      Bytes message, handshake_properties_t *properties)
{
    int ret;

    if (tls->key_share_ctx != NULL) {
        tls->key_share_ctx->on_exchange(&tls->key_share_ctx, 1, NULL, wire_bytes(NULL, 0));
        tls->key_share_ctx = NULL;
    }
    if (tls->using_early_data) {
        /* release traffic encryption key so that 2nd CH goes out in cleartext, but keep the epoch at 1 since we've already
         * called derive-secret */
        if (tls->ctx->update_traffic_key == NULL) {
            assert(tls->tx_traffic.aead != NULL);
            aead_free(tls->tx_traffic.aead);
            tls->tx_traffic.aead = NULL;
        }
        tls->using_early_data = 0;
    }

    if (sh->retry_request.selected_group != UINT16_MAX) {
        /* we offer the first key_exchanges[0] as KEY_SHARE unless client.negotiate_before_key_exchange is set */
        key_exchange_algorithm_t **cand;
        for (cand = tls->ctx->key_exchanges; *cand != NULL; ++cand)
            if ((*cand)->id == sh->retry_request.selected_group)
                break;
        if (*cand == NULL) {
            ret = Ztls_ALERT_ILLEGAL_PARAMETER;
            goto Exit;
        }
        tls->key_share = *cand;
    } else if (tls->key_share != NULL) {
        /* retain the key-share using in first CH, if server does not specify one */
    } else {
        ret = Ztls_ALERT_ILLEGAL_PARAMETER;
        goto Exit;
    }

    ret = send_client_hello(tls, emitter, properties, &sh->retry_request.cookie);

Exit:
    return ret;
}

static int client_ech_select_hello(Ztls::Cxn *tls, Bytes message, size_t confirm_hash_off, const char *label)
{
    uint8_t confirm_hash_delivered[Ztls_ECH_CONFIRM_LENGTH], confirm_hash_expected[Ztls_ECH_CONFIRM_LENGTH];
    int ret = 0;

    /* Determine if ECH has been accepted by checking the confirmation hash. `confirm_hash_off` set to zero indicates that HRR was
     * received wo. ECH extension, which is an indication that ECH was rejected. */
    if (confirm_hash_off != 0) {
        memcpy(confirm_hash_delivered, message.data() + confirm_hash_off, sizeof(confirm_hash_delivered));
        memset(message.data() + confirm_hash_off, 0, sizeof(confirm_hash_delivered));
        if ((ret = ech_calc_confirmation(tls->key_schedule, confirm_hash_expected, tls->ech.inner_client_random, label, message)) !=
            0)
            goto Exit;
        int accepted = Ztls::ctEqual({confirm_hash_delivered, sizeof(confirm_hash_delivered)},
                         {confirm_hash_expected, sizeof(confirm_hash_delivered)});
        memcpy(message.data() + confirm_hash_off, confirm_hash_delivered, sizeof(confirm_hash_delivered));
        if (accepted) {
            tls->ech.state = Ztls_ECH_STATE_ACCEPTED;
            goto Exit;
        } else if (tls->ech.state == Ztls_ECH_STATE_ACCEPTED) {
            /* Per RFC 9849 Section 6.1.5: if HRR confirmed ECH acceptance, ServerHello MUST also confirm it. */
            ret = Ztls_ALERT_ILLEGAL_PARAMETER;
            goto Exit;
        }
    }

    /* dispose ECH AEAD state to indicate rejection, adopting outer CH for the rest of the handshake */
    aead_free(tls->ech.aead);
    tls->ech.aead = NULL;
    key_schedule_select_outer(tls->key_schedule);

Exit:
    ZuClear(confirm_hash_expected, sizeof(confirm_hash_expected));
    return ret;
}

static int client_handle_hello(Ztls::Cxn *tls, message_emitter_t *emitter, Bytes message,
                               handshake_properties_t *properties)
{
    struct st_server_hello_t sh;
    KeyExchangeBytes ecdh_secret;
    int ret;

    if ((ret = decode_server_hello(tls, &sh, message.data() + Ztls_HANDSHAKE_HEADER_SIZE, message.data() + message.length())) != 0)
        goto Exit;
    if (!(sh.legacy_session_id.length() == tls->legacy_session_id.length() &&
          Ztls::ctEqual({sh.legacy_session_id.data(), tls->legacy_session_id.length()},
                         {tls->legacy_session_id.data(), tls->legacy_session_id.length()}))) {
        ret = Ztls_ALERT_ILLEGAL_PARAMETER;
        goto Exit;
    }

    if (sh.is_retry_request) {
        if ((ret = key_schedule_select_cipher(tls->key_schedule, tls->cipher_suite, 0, tls->ctx->pre_shared_key.secret)) != 0)
            goto Exit;
        key_schedule_transform_post_ch1hash(tls->key_schedule);
        if (tls->ech.aead != NULL) {
            size_t confirm_hash_off = 0;
            if (tls->ech.state != Ztls_ECH_STATE_GREASE) {
                if (sh.retry_request.ech != NULL)
                    confirm_hash_off = sh.retry_request.ech - message.data();
            }
            if ((ret = client_ech_select_hello(tls, message, confirm_hash_off, ECH_CONFIRMATION_HRR)) != 0)
                goto Exit;
        }
        _key_schedule_update_hash(tls->key_schedule, message.data(), message.length(), 0);
        return handle_hello_retry_request(tls, emitter, &sh, message, properties);
    }

    if ((ret = key_schedule_select_cipher(tls->key_schedule, tls->cipher_suite, tls->offered_psk && !tls->is_psk_handshake,
                                          wire_bytes(NULL, 0))) != 0)
        goto Exit;

    /* check if ECH is accepted */
    if (tls->ech.aead != NULL) {
        size_t confirm_hash_off = 0;
        if (tls->ech.state != Ztls_ECH_STATE_GREASE) {
            confirm_hash_off =
                Ztls_HANDSHAKE_HEADER_SIZE + 2 /* legacy_version */ + Ztls_HELLO_RANDOM_SIZE - Ztls_ECH_CONFIRM_LENGTH;
        }
        if ((ret = client_ech_select_hello(tls, message, confirm_hash_off, ECH_CONFIRMATION_SERVER_HELLO)) != 0)
            goto Exit;
    }

    /* clear sensitive and space-consuming ECH state, now that are done with handling sending and decoding Hellos */
    clear_ech(&tls->ech, 0);
    if (tls->key_schedule->hashes[0].ctx_outer != NULL) {
        tls->key_schedule->hashes[0].ctx_outer->final(tls->key_schedule->hashes[0].ctx_outer, NULL, HASH_FINAL_MODE_FREE);
        tls->key_schedule->hashes[0].ctx_outer = NULL;
    }

    /* if the client offered external PSK but the server did not use that, we call it a handshake failure */
    if (tls->ctx->pre_shared_key.identity.data() != NULL && !tls->is_psk_handshake) {
        ret = Ztls_ALERT_HANDSHAKE_FAILURE;
        goto Exit;
    }

    _key_schedule_update_hash(tls->key_schedule, message.data(), message.length(), 0);

    if (sh.peerkey.data() != NULL) {
        if ((ret = tls->key_share_ctx->on_exchange(&tls->key_share_ctx, 1, &ecdh_secret, sh.peerkey)) != 0) {
            assert(!ecdh_secret.length());
            goto Exit;
        }
    }

    if ((ret = key_schedule_extract(tls->key_schedule, wire_bytes(ecdh_secret.data(), ecdh_secret.length()))) != 0)
        goto Exit;
    if ((ret = setup_traffic_protection(tls, 0, "s hs traffic", 2, 0, 0)) != 0)
        goto Exit;
    if (tls->using_early_data) {
        tls->pending_handshake_secret.length(Ztls_MAX_DIGEST_SIZE);
        if ((ret = derive_secret(tls->key_schedule, tls->pending_handshake_secret.data(), "c hs traffic")) != 0)
            goto Exit;
        if (tls->ctx->update_traffic_key != NULL &&
            (ret = tls->ctx->update_traffic_key->cb(tls->ctx->update_traffic_key, tls, 1, 2, tls->pending_handshake_secret.data())) != 0)
            goto Exit;
    } else {
        if ((ret = setup_traffic_protection(tls, 1, "c hs traffic", 2, 0, 0)) != 0)
            goto Exit;
    }

    tls->state = Ztls_STATE_CLIENT_EXPECT_ENCRYPTED_EXTENSIONS;
    ret = Ztls_ERROR_IN_PROGRESS;

Exit:
    return ret;
}

static int should_collect_unknown_extension(Ztls::Cxn *tls, handshake_properties_t *properties, uint16_t type)
{
    return properties != NULL && properties->collect_extension != NULL && properties->collect_extension(tls, properties, type);
}

static int collect_unknown_extension(Ztls::Cxn *tls, uint16_t type, const uint8_t *src, const uint8_t *const end,
                                     raw_extension_t *slots)
{
    size_t i;
    for (i = 0; slots[i].type != UINT16_MAX; ++i) {
        assert(i < MAX_UNKNOWN_EXTENSIONS);
        if (slots[i].type == type)
            return Ztls_ALERT_ILLEGAL_PARAMETER;
    }
    if (i < MAX_UNKNOWN_EXTENSIONS) {
        slots[i].type = type;
        slots[i].data = wire_bytes(src, end - src);
        slots[i + 1].type = UINT16_MAX;
    }
    return 0;
}

static int report_unknown_extensions(Ztls::Cxn *tls, handshake_properties_t *properties, raw_extension_t *slots)
{
    if (properties != NULL && properties->collect_extension != NULL) {
        assert(properties->collected_extensions != NULL);
        return properties->collected_extensions(tls, properties, slots);
    } else {
        return 0;
    }
}

static int client_handle_encrypted_extensions(Ztls::Cxn *tls, Bytes message, handshake_properties_t *properties)
{
    const uint8_t *src = message.data() + Ztls_HANDSHAKE_HEADER_SIZE, *const end = message.data() + message.length();
    uint16_t type;
    auto extensions = ZtScratch(CollectedExtensions, MAX_UNKNOWN_EXTENSIONS + 1, MAX_UNKNOWN_EXTENSIONS + 1);
    auto unknown_extensions = extensions.data();
    unknown_extensions[0] = {UINT16_MAX};
    int ret, skip_early_data = 1;
    uint8_t server_offered_cert_type = Ztls_CERTIFICATE_TYPE_X509;

    decode_extensions(src, end, Ztls_HANDSHAKE_TYPE_ENCRYPTED_EXTENSIONS, &type, {
        if (tls->ctx->on_extension != NULL &&
            (ret = tls->ctx->on_extension->cb(tls->ctx->on_extension, tls, Ztls_HANDSHAKE_TYPE_ENCRYPTED_EXTENSIONS, type,
                                              wire_bytes(src, end - src)) != 0))
            goto Exit;
        switch (type) {
        case Ztls_EXTENSION_TYPE_SERVER_NAME:
            if (src != end) {
                ret = Ztls_ALERT_DECODE_ERROR;
                goto Exit;
            }
            if (!(tls->server_name.data() != NULL && !server_name_is_ipaddr(tls->server_name.data()))) {
                ret = Ztls_ALERT_ILLEGAL_PARAMETER;
                goto Exit;
            }
            break;
        case Ztls_EXTENSION_TYPE_ALPN:
            Ztls_decode_block(src, end, 2, {
                Ztls_decode_open_block(src, end, 1, {
                    if (src == end) {
                        ret = Ztls_ALERT_DECODE_ERROR;
                        goto Exit;
                    }
                    if ((ret = set_negotiated_protocol(tls, (const char *)src, end - src)) != 0)
                        goto Exit;
                    src = end;
                });
                if (src != end) {
                    ret = Ztls_ALERT_HANDSHAKE_FAILURE;
                    goto Exit;
                }
            });
            break;
        case Ztls_EXTENSION_TYPE_EARLY_DATA:
            if (!tls->using_early_data) {
                ret = Ztls_ALERT_ILLEGAL_PARAMETER;
                goto Exit;
            }
            skip_early_data = 0;
            break;
        case Ztls_EXTENSION_TYPE_SERVER_CERTIFICATE_TYPE:
            if (end - src != 1) {
                ret = Ztls_ALERT_DECODE_ERROR;
                goto Exit;
            }
            server_offered_cert_type = *src;
            src = end;
            break;
        case Ztls_EXTENSION_TYPE_ENCRYPTED_CLIENT_HELLO: {
            /* accept retry_configs only if we offered ECH (or grease) but rejected */
            if (!(tls->ech.state == Ztls_ECH_STATE_OFFERED || tls->ech.state == Ztls_ECH_STATE_GREASE)) {
                ret = Ztls_ALERT_UNSUPPORTED_EXTENSION;
                goto Exit;
            }
            /* parse retry_config, and if it is applicable, provide that to the application (grease clients just verify syntax) */
            struct st_decoded_ech_config_t decoded;
            if ((ret = client_decode_ech_config_list(tls->ctx, &decoded, wire_bytes(src, end - src))) != 0)
                goto Exit;
            if (tls->ech.state == Ztls_ECH_STATE_GREASE) {
                /* GREASE clients ignore retry_configs after verifying the syntax */
            } else if (decoded.kem != NULL && decoded.cipher != NULL && properties != NULL &&
                       properties->client.ech.retry_configs != NULL) {
                properties->client.ech.retry_configs->copy(src, end - src);
            }
            src = end;
        } break;
        default:
            if (should_collect_unknown_extension(tls, properties, type)) {
                if ((ret = collect_unknown_extension(tls, type, src, end, unknown_extensions)) != 0)
                    goto Exit;
            }
            break;
        }
        src = end;
    });

    if (server_offered_cert_type !=
        (tls->ctx->use_raw_public_keys ? Ztls_CERTIFICATE_TYPE_RAW_PUBLIC_KEY : Ztls_CERTIFICATE_TYPE_X509)) {
        ret = Ztls_ALERT_UNSUPPORTED_CERTIFICATE;
        goto Exit;
    }

    if (tls->using_early_data) {
        if (skip_early_data)
            tls->using_early_data = 0;
        if (properties != NULL)
            properties->client.early_data_acceptance = skip_early_data ? EARLY_DATA_REJECTED : EARLY_DATA_ACCEPTED;
    }
    if ((ret = report_unknown_extensions(tls, properties, unknown_extensions)) != 0)
        goto Exit;

    _key_schedule_update_hash(tls->key_schedule, message.data(), message.length(), 0);
    tls->state =
        tls->is_psk_handshake ? Ztls_STATE_CLIENT_EXPECT_FINISHED : Ztls_STATE_CLIENT_EXPECT_CERTIFICATE_REQUEST_OR_CERTIFICATE;
    ret = Ztls_ERROR_IN_PROGRESS;

Exit:
    return ret;
}

static int decode_certificate_request(Ztls::Cxn *tls, CertificateRequest *cr, const uint8_t *src,
                                      const uint8_t *const end)
{
    int ret;
    uint16_t exttype = 0;

    /* certificate request context */
    Ztls_decode_open_block(src, end, 1, {
        size_t len = end - src;
        if (len > 255) {
            ret = Ztls_ALERT_DECODE_ERROR;
            goto Exit;
        }
        auto &context = tls->certificate_request_bytes;
        // Empty context still denotes a pending request.
        context.length(len ? len : 1);
        if (len)
            memcpy(context.data(), src, len);
        cr->context = wire_bytes(context.data(), len);
        src = end;
    });

    /* decode extensions */
    decode_extensions(src, end, Ztls_HANDSHAKE_TYPE_CERTIFICATE_REQUEST, &exttype, {
        if (tls->ctx->on_extension != NULL &&
            (ret = tls->ctx->on_extension->cb(tls->ctx->on_extension, tls, Ztls_HANDSHAKE_TYPE_CERTIFICATE_REQUEST, exttype,
                                              wire_bytes(src, end - src)) != 0))
            goto Exit;
        switch (exttype) {
        case Ztls_EXTENSION_TYPE_SIGNATURE_ALGORITHMS:
            if ((ret = decode_signature_algorithms(&cr->signature_algorithms, &src, end)) != 0)
                goto Exit;
            break;
        }
        src = end;
    });

    if (cr->signature_algorithms.count == 0) {
        ret = Ztls_ALERT_MISSING_EXTENSION;
        goto Exit;
    }

    ret = 0;
Exit:
    return ret;
}

template <typename CertAt>
static int build_certificate_message_(buffer_t *buf, Bytes context, size_t num_certificates,
                                      Bytes ocsp_status, CertAt cert_at)
{
    int ret;

    Ztls_buffer_push_block(buf, 1, { Ztls_buffer_pushv(buf, context.data(), context.length()); });
    Ztls_buffer_push_block(buf, 3, {
        size_t i;
        for (i = 0; i != num_certificates; ++i) {
            auto cert = cert_at(i);
            Ztls_buffer_push_block(buf, 3, { Ztls_buffer_pushv(buf, cert.data(), cert.length()); });
            Ztls_buffer_push_block(buf, 2, {
                if (i == 0 && ocsp_status.length() != 0) {
                    buffer_push_extension(buf, Ztls_EXTENSION_TYPE_STATUS_REQUEST, {
                        Ztls_buffer_push(buf, 1); /* status_type == ocsp */
                        Ztls_buffer_push_block(buf, 3, { Ztls_buffer_pushv(buf, ocsp_status.data(), ocsp_status.length()); });
                    });
                }
            });
        }
    });

    ret = 0;
Exit:
    return ret;
}

int build_certificate_message(buffer_t *buf, Bytes context, Bytes *certificates, size_t num_certificates,
                              Bytes ocsp_status)
{
    return build_certificate_message_(buf, context, num_certificates, ocsp_status,
                                      [certificates](size_t i) { return certificates[i]; });
}

static int default_emit_certificate_cb(emit_certificate_t *_self, Ztls::Cxn *tls, message_emitter_t *emitter,
                                       key_schedule_t *key_sched, Bytes context, int push_status_request,
                                       const uint16_t *compress_algos, size_t num_compress_algos)
{
    int ret;

    Ztls_push_message(emitter, key_sched, Ztls_HANDSHAKE_TYPE_CERTIFICATE, {
        auto &certs = tls->ctx->certificates;
        if ((ret = build_certificate_message_(emitter->buf, context, certs.length(), wire_bytes(NULL, 0),
                                              [&certs](size_t i) {
                                                  return wire_bytes(certs[i].data(), certs[i].length());
                                              })) != 0)
            goto Exit;
    });

    ret = 0;
Exit:
    return ret;
}

static int send_certificate(Ztls::Cxn *tls, message_emitter_t *emitter,
                            SignatureAlgorithms *signature_algorithms, Bytes context,
                            int push_status_request, const uint16_t *compress_algos, size_t num_compress_algos)
{
    int ret;

    if (signature_algorithms->count == 0) {
        ret = Ztls_ALERT_MISSING_EXTENSION;
        goto Exit;
    }

    { /* send Certificate (or the equivalent) */
        static emit_certificate_t default_emit_certificate = {default_emit_certificate_cb};
        emit_certificate_t *emit_certificate =
            tls->ctx->emit_certificate != NULL ? tls->ctx->emit_certificate : &default_emit_certificate;
    Redo:
        if ((ret = emit_certificate->cb(emit_certificate, tls, emitter, tls->key_schedule, context, push_status_request,
                                        compress_algos, num_compress_algos)) != 0) {
            if (ret == Ztls_ERROR_DELEGATE) {
                assert(emit_certificate != &default_emit_certificate);
                emit_certificate = &default_emit_certificate;
                goto Redo;
            }
            goto Exit;
        }
    }

Exit:
    return ret;
}

static int send_certificate_verify(Ztls::Cxn *tls, message_emitter_t *emitter,
                                   SignatureAlgorithms *signature_algorithms, const char *context_string)
{
    size_t start_off = emitter->buf->off;
    int ret;

    if (tls->ctx->sign_certificate == NULL)
        return 0;
    /* build and send CertificateVerify */
    Ztls_push_message(emitter, tls->key_schedule, Ztls_HANDSHAKE_TYPE_CERTIFICATE_VERIFY, {
        buffer_t *sendbuf = emitter->buf;
        size_t algo_off = sendbuf->off;
        Ztls_buffer_push16(sendbuf, 0); /* filled in later */
        Ztls_buffer_push_block(sendbuf, 2, {
            uint16_t algo;
            if (auto resume = tls->signature_resume) {
                ret = resume(sendbuf, &algo);
            } else {
                uint8_t data[Ztls_MAX_CERTIFICATE_VERIFY_SIGNDATA_SIZE];
                size_t datalen = build_certificate_verify_signdata(data, tls->key_schedule, context_string);
                ret = tls->ctx->sign_certificate->cb(
                    tls->ctx->sign_certificate, tls, tls->is_server, &algo, sendbuf,
                    wire_bytes(data, datalen), signature_algorithms ? signature_algorithms->list : nullptr,
                    signature_algorithms ? signature_algorithms->count : 0);
                ZuClear(data, sizeof(data));
            }
            if (ret == Ztls_ERROR_ASYNC_OPERATION) {
                assert(tls->is_server && tls->signature_resume && tls->signature_retire);
                /* CertificateVerify is rebuilt when the signature becomes available. */
                emitter->buf->off = start_off;
                goto Exit;
            }
            if (ret)
                goto Exit;
            sendbuf->base[algo_off] = (uint8_t)(algo >> 8);
            sendbuf->base[algo_off + 1] = (uint8_t)algo;
        });
    });
Exit:
    return ret;
}

static int client_handle_certificate_request(Ztls::Cxn *tls, Bytes message, handshake_properties_t *properties)
{
    const uint8_t *src = message.data() + Ztls_HANDSHAKE_HEADER_SIZE, *const end = message.data() + message.length();
    int ret = 0;

    assert(!tls->is_psk_handshake && "state machine asserts that this message is never delivered when PSK is used");

    if ((ret = decode_certificate_request(tls, &tls->certificate_request, src, end)) != 0)
        return ret;

    /* This field SHALL be zero length unless used for the post-handshake authentication exchanges (section 4.3.2) */
    if (tls->certificate_request.context.length() != 0)
        return Ztls_ALERT_ILLEGAL_PARAMETER;

    tls->state = Ztls_STATE_CLIENT_EXPECT_CERTIFICATE;
    _key_schedule_update_hash(tls->key_schedule, message.data(), message.length(), 0);

    return Ztls_ERROR_IN_PROGRESS;
}

static int handle_certificate(Ztls::Cxn *tls, const uint8_t *src, const uint8_t *end, int *got_certs)
{
    Bytes certs[16];
    size_t num_certs = 0;
    int ret = 0;

    /* certificate request context */
    Ztls_decode_open_block(src, end, 1, {
        if (src != end) {
            ret = Ztls_ALERT_ILLEGAL_PARAMETER;
            goto Exit;
        }
    });
    /* certificate_list */
    Ztls_decode_block(src, end, 3, {
        while (src != end) {
            Ztls_decode_open_block(src, end, 3, {
                if (num_certs < Ztls_ELEMENTSOF(certs))
                    certs[num_certs++] = wire_bytes(src, end - src);
                src = end;
            });
            uint16_t type;
            decode_open_extensions(src, end, Ztls_HANDSHAKE_TYPE_CERTIFICATE, &type, {
                if (tls->ctx->on_extension != NULL &&
                    (ret = tls->ctx->on_extension->cb(tls->ctx->on_extension, tls, Ztls_HANDSHAKE_TYPE_CERTIFICATE, type,
                                                      wire_bytes(src, end - src)) != 0))
                    goto Exit;
                src = end;
            });
        }
    });

    if (tls->ctx->verify_certificate != NULL) {
        const char *server_name = NULL;
        if (!is_server(tls)) {
            if (tls->ech.state == Ztls_ECH_STATE_OFFERED) {
                server_name = tls->ech.client.public_name.data();
            } else {
                server_name = tls->server_name.data();
            }
        }
        if ((ret = tls->ctx->verify_certificate->cb(tls->ctx->verify_certificate, tls, server_name, &tls->certificate_verify.cb,
                                                    &tls->certificate_verify.verify_ctx, certs, num_certs)) != 0)
            goto Exit;
    }

    *got_certs = num_certs != 0;

Exit:
    return ret;
}

static int client_do_handle_certificate(Ztls::Cxn *tls, const uint8_t *src, const uint8_t *end)
{
    int got_certs, ret;

    if ((ret = handle_certificate(tls, src, end, &got_certs)) != 0)
        return ret;
    if (!got_certs)
        return Ztls_ALERT_ILLEGAL_PARAMETER;

    return 0;
}

static int client_handle_certificate(Ztls::Cxn *tls, Bytes message)
{
    int ret;

    if ((ret = client_do_handle_certificate(tls, message.data() + Ztls_HANDSHAKE_HEADER_SIZE, message.data() + message.length())) != 0)
        return ret;

    _key_schedule_update_hash(tls->key_schedule, message.data(), message.length(), 0);

    tls->state = Ztls_STATE_CLIENT_EXPECT_CERTIFICATE_VERIFY;
    return Ztls_ERROR_IN_PROGRESS;
}

ZuDerive(UncompressedCertificate, (ZtBArray<ZtArrayHeapID<"Ztls.Engine.UncompressedCertificate">>));

static int client_handle_compressed_certificate(Ztls::Cxn *tls, Bytes message)
{
    const uint8_t *src = message.data() + Ztls_HANDSHAKE_HEADER_SIZE, *const end = message.data() + message.length();
    uint16_t algo;
    uint32_t uncompressed_size;
    UncompressedCertificate uncompressed;
    int ret;

    if (tls->ctx->decompress_certificate == NULL) {
        ret = Ztls_ALERT_UNEXPECTED_MESSAGE;
        goto Exit;
    }

    /* decode */
    if ((ret = decode16(&algo, &src, end)) != 0)
        goto Exit;
    if ((ret = decode24(&uncompressed_size, &src, end)) != 0)
        goto Exit;
    if (uncompressed_size > 65536) { /* TODO find a sensible number */
        ret = Ztls_ALERT_BAD_CERTIFICATE;
        goto Exit;
    }
    uncompressed.length(uncompressed_size ? uncompressed_size : 1);
    Ztls_decode_block(src, end, 3, {
        if ((ret = tls->ctx->decompress_certificate->cb(tls->ctx->decompress_certificate, tls, algo,
                                                        wire_bytes(uncompressed.data(), uncompressed_size),
                                                        wire_bytes(src, end - src))) != 0)
            goto Exit;
        src = end;
    });

    /* handle */
    if ((ret = client_do_handle_certificate(tls, uncompressed.data(), uncompressed.data() + uncompressed_size)) != 0)
        goto Exit;

    _key_schedule_update_hash(tls->key_schedule, message.data(), message.length(), 0);
    tls->state = Ztls_STATE_CLIENT_EXPECT_CERTIFICATE_VERIFY;
    ret = Ztls_ERROR_IN_PROGRESS;

Exit:
    return ret;
}

static int server_handle_certificate(Ztls::Cxn *tls, Bytes message)
{
    int got_certs, ret;

    if ((ret = handle_certificate(tls, message.data() + Ztls_HANDSHAKE_HEADER_SIZE, message.data() + message.length(), &got_certs)) != 0)
        return ret;

    _key_schedule_update_hash(tls->key_schedule, message.data(), message.length(), 0);

    if (got_certs) {
        tls->state = Ztls_STATE_SERVER_EXPECT_CERTIFICATE_VERIFY;
    } else {
        /* Client did not provide certificate, and the verifier says we can fail open. Therefore, the next message is Finished. */
        tls->state = Ztls_STATE_SERVER_EXPECT_FINISHED;
    }

    return Ztls_ERROR_IN_PROGRESS;
}

static int handle_certificate_verify(Ztls::Cxn *tls, Bytes message, const char *context_string)
{
    const uint8_t *src = message.data() + Ztls_HANDSHAKE_HEADER_SIZE, *const end = message.data() + message.length();
    uint16_t algo;
    Bytes signature;
    uint8_t signdata[Ztls_MAX_CERTIFICATE_VERIFY_SIGNDATA_SIZE];
    size_t signdata_size;
    int ret;

    /* decode */
    if ((ret = decode16(&algo, &src, end)) != 0)
        goto Exit;
    Ztls_decode_block(src, end, 2, {
        signature = wire_bytes(src, end - src);
        src = end;
    });

    signdata_size = build_certificate_verify_signdata(signdata, tls->key_schedule, context_string);
    if (tls->certificate_verify.cb != NULL) {
        ret = tls->certificate_verify.cb(tls->certificate_verify.verify_ctx, algo, wire_bytes(signdata, signdata_size),
                                         signature);
    } else {
        ret = 0;
    }
    ZuClear(signdata, signdata_size);
    tls->certificate_verify.cb = NULL;
    if (ret != 0) {
        goto Exit;
    }

    _key_schedule_update_hash(tls->key_schedule, message.data(), message.length(), 0);

Exit:
    return ret;
}

static int client_handle_certificate_verify(Ztls::Cxn *tls, Bytes message)
{
    int ret = handle_certificate_verify(tls, message, Ztls_SERVER_CERTIFICATE_VERIFY_CONTEXT_STRING);

    if (ret == 0) {
        tls->state = Ztls_STATE_CLIENT_EXPECT_FINISHED;
        ret = Ztls_ERROR_IN_PROGRESS;
    }

    return ret;
}

static int server_handle_certificate_verify(Ztls::Cxn *tls, Bytes message)
{
    int ret = handle_certificate_verify(tls, message, Ztls_CLIENT_CERTIFICATE_VERIFY_CONTEXT_STRING);

    if (ret == 0) {
        tls->state = Ztls_STATE_SERVER_EXPECT_FINISHED;
        ret = Ztls_ERROR_IN_PROGRESS;
    }

    return ret;
}

static int client_handle_finished(Ztls::Cxn *tls, message_emitter_t *emitter, Bytes message)
{
    uint8_t send_secret[Ztls_MAX_DIGEST_SIZE];
    int alert_ech_required = tls->ech.state == Ztls_ECH_STATE_OFFERED, ret;

    if ((ret = verify_finished(tls, message)) != 0)
        goto Exit;
    _key_schedule_update_hash(tls->key_schedule, message.data(), message.length(), 0);

    /* update traffic keys by using messages upto ServerFinished, but commission them after sending ClientFinished */
    if ((ret = key_schedule_extract(tls->key_schedule, wire_bytes(NULL, 0))) != 0)
        goto Exit;
    if ((ret = setup_traffic_protection(tls, 0, "s ap traffic", 3, 0, 0)) != 0)
        goto Exit;
    if ((ret = derive_secret(tls->key_schedule, send_secret, "c ap traffic")) != 0)
        goto Exit;
    if ((ret = derive_exporter_secret(tls, 0)) != 0)
        goto Exit;

    /* if sending early data, emit EOED and commision the client handshake traffic secret */
    if (tls->pending_handshake_secret.data() != NULL) {
        assert(tls->tx_traffic.aead != NULL || tls->ctx->update_traffic_key != NULL);
        if (tls->using_early_data && !tls->ctx->omit_end_of_early_data)
            Ztls_push_message(emitter, tls->key_schedule, Ztls_HANDSHAKE_TYPE_END_OF_EARLY_DATA, {});
        tls->using_early_data = 0;
        if ((ret = commission_handshake_secret(tls)) != 0)
            goto Exit;
    }

    if ((ret = push_change_cipher_spec(tls, emitter)) != 0)
        goto Exit;

    if (!alert_ech_required && tls->certificate_request.context.data() != NULL) {
        if ((ret = send_certificate(tls, emitter, &tls->certificate_request.signature_algorithms,
                                    tls->certificate_request.context, 0, NULL, 0)) == 0)
            ret = send_certificate_verify(tls, emitter, &tls->certificate_request.signature_algorithms,
                                          Ztls_CLIENT_CERTIFICATE_VERIFY_CONTEXT_STRING);
        tls->certificate_request_bytes.null();
        tls->certificate_request.context = wire_bytes(NULL, 0);
        if (ret != 0)
            goto Exit;
    }

    ret = send_finished(tls, emitter);

    memcpy(tls->tx_traffic.secret, send_secret, sizeof(send_secret));
    if ((ret = setup_traffic_protection(tls, 1, NULL, 3, 0, 0)) != 0)
        goto Exit;

    tls->state = Ztls_STATE_CLIENT_POST_HANDSHAKE;

    /* if ECH was rejected, close the connection with ECH_REQUIRED alert after verifying messages up to Finished */
    if (alert_ech_required)
        ret = Ztls_ALERT_ECH_REQUIRED;

Exit:
    ZuClear(send_secret, sizeof(send_secret));
    return ret;
}

static int client_handle_new_session_ticket(Ztls::Cxn *tls, Bytes message)
{
    const uint8_t *src = message.data() + Ztls_HANDSHAKE_HEADER_SIZE, *const end = message.data() + message.length();
    Bytes ticket_nonce;
    int ret;

    { /* verify the format */
        uint32_t ticket_lifetime, ticket_age_add, max_early_data_size;
        Bytes ticket;
        if ((ret = decode_new_session_ticket(tls, &ticket_lifetime, &ticket_age_add, &ticket_nonce, &ticket, &max_early_data_size,
                                             src, end)) != 0)
            return ret;
    }

    /* do nothing if use of session ticket is disabled */
    if (tls->ctx->save_ticket == NULL)
        return 0;

    /* save the extension, along with the key of myself */
    buffer_t ticket_buf;
    buffer_init_rx(&ticket_buf, const_cast<char *>(""), 0);
    Ztls_buffer_push64(&ticket_buf, tls->ctx->get_time->cb(tls->ctx->get_time));
    Ztls_buffer_push16(&ticket_buf, tls->key_share->id);
    Ztls_buffer_push16(&ticket_buf, tls->cipher_suite->id);
    Ztls_buffer_push_block(&ticket_buf, 3, { Ztls_buffer_pushv(&ticket_buf, src, end - src); });
    Ztls_buffer_push_block(&ticket_buf, 2, {
        if ((ret = buffer_reserve(&ticket_buf, tls->key_schedule->hashes[0].algo->digest_size, 0)) != 0)
            goto Exit;
        if ((ret = derive_resumption_secret(tls->key_schedule, ticket_buf.base + ticket_buf.off, ticket_nonce)) != 0)
            goto Exit;
        ticket_buf.off += tls->key_schedule->hashes[0].algo->digest_size;
    });

    if ((ret = tls->ctx->save_ticket->cb(tls->ctx->save_ticket, tls, wire_bytes(ticket_buf.base, ticket_buf.off))) != 0)
        goto Exit;

    ret = 0;
Exit:
    buffer_dispose(&ticket_buf);
    return ret;
}

static int client_hello_decode_server_name(Bytes *name, const uint8_t **src, const uint8_t *const end)
{
    int ret = 0;

    Ztls_decode_open_block(*src, end, 2, {
        do {
            uint8_t type;
            if ((ret = decode8(&type, src, end)) != 0)
                goto Exit;
            Ztls_decode_open_block(*src, end, 2, {
                switch (type) {
                case Ztls_SERVER_NAME_TYPE_HOSTNAME:
                    if (end - *src == 0) {
                        ret = Ztls_ALERT_DECODE_ERROR;
                        goto Exit;
                    }
                    if (memchr(*src, '\0', end - *src) != 0) {
                        ret = Ztls_ALERT_ILLEGAL_PARAMETER;
                        goto Exit;
                    }
                    *name = wire_bytes(*src, end - *src);
                    break;
                default:
                    break;
                }
                *src = end;
            });
        } while (*src != end);
    });

Exit:
    return ret;
}

static int select_negotiated_group(key_exchange_algorithm_t **selected, key_exchange_algorithm_t **candidates,
                                   const uint8_t *src, const uint8_t *const end)
{
    int ret;

    Ztls_decode_block(src, end, 2, {
        while (src != end) {
            uint16_t group;
            if ((ret = decode16(&group, &src, end)) != 0)
                goto Exit;
            key_exchange_algorithm_t **c = candidates;
            for (; *c != NULL; ++c) {
                if ((*c)->id == group) {
                    *selected = *c;
                    return 0;
                }
            }
        }
    });

    ret = Ztls_ALERT_HANDSHAKE_FAILURE;

Exit:
    return ret;
}

static int decode_client_hello(context_t *ctx, struct st_client_hello_t *ch, const uint8_t *src, const uint8_t *const end,
                               handshake_properties_t *properties, Ztls::Cxn *tls_cbarg)
{
    const uint8_t *start = src;
    uint16_t exttype = 0;
    int ret;

    /* decode protocol version (do not bare to decode something older than TLS 1.0) */
    if ((ret = decode16(&ch->legacy_version, &src, end)) != 0)
        goto Exit;
    if (ch->legacy_version < 0x0301) {
        ret = Ztls_ALERT_PROTOCOL_VERSION;
        goto Exit;
    }

    /* skip random */
    if (end - src < Ztls_HELLO_RANDOM_SIZE) {
        ret = Ztls_ALERT_DECODE_ERROR;
        goto Exit;
    }
    ch->random_bytes = src;
    src += Ztls_HELLO_RANDOM_SIZE;

    /* skip legacy_session_id */
    Ztls_decode_open_block(src, end, 1, {
        if (end - src > 32) {
            ret = Ztls_ALERT_DECODE_ERROR;
            goto Exit;
        }
        ch->legacy_session_id = wire_bytes(src, end - src);
        src = end;
    });

    /* decode and select from ciphersuites */
    Ztls_decode_open_block(src, end, 2, {
        if ((end - src) % 2 != 0) {
            ret = Ztls_ALERT_DECODE_ERROR;
            goto Exit;
        }
        ch->cipher_suites = wire_bytes(src, end - src);
        src = end;
    });

    /* decode legacy_compression_methods */
    Ztls_decode_open_block(src, end, 1, {
        if (src == end) {
            ret = Ztls_ALERT_DECODE_ERROR;
            goto Exit;
        }
        ch->compression_methods.ids = src;
        ch->compression_methods.count = end - src;
        src = end;
    });

    /* In TLS versions 1.2 and earlier CH might not have an extensions block (or they might, see what OpenSSL 1.0.0 sends); so bail
     * out if that is the case after parsing the main variables. Zero is returned as it is a valid ClientHello. However
     * `Ztls::Cxn::selected_version` remains zero indicating that no compatible version were found. */
    if (src == end) {
        ret = 0;
        goto Exit;
    }

    /* decode extensions */
    ch->first_extension_at = src - start + 2;
    decode_extensions(src, end, Ztls_HANDSHAKE_TYPE_CLIENT_HELLO, &exttype, {
        ch->psk.is_last_extension = 0;
        if (ctx->on_extension != NULL && tls_cbarg != NULL &&
            (ret = ctx->on_extension->cb(ctx->on_extension, tls_cbarg, Ztls_HANDSHAKE_TYPE_CLIENT_HELLO, exttype,
                                         wire_bytes(src, end - src)) != 0))
            goto Exit;
        switch (exttype) {
        case Ztls_EXTENSION_TYPE_SERVER_NAME:
            if ((ret = client_hello_decode_server_name(&ch->server_name, &src, end)) != 0)
                goto Exit;
            if (src != end) {
                ret = Ztls_ALERT_DECODE_ERROR;
                goto Exit;
            }
            break;
        case Ztls_EXTENSION_TYPE_ALPN:
            Ztls_decode_block(src, end, 2, {
                do {
                    Ztls_decode_open_block(src, end, 1, {
                        /* rfc7301 3.1: empty strings MUST NOT be included */
                        if (src == end) {
                            ret = Ztls_ALERT_DECODE_ERROR;
                            goto Exit;
                        }
                        if (ch->alpn.count < Ztls_ELEMENTSOF(ch->alpn.list))
                            ch->alpn.list[ch->alpn.count++] = wire_bytes(src, end - src);
                        src = end;
                    });
                } while (src != end);
            });
            break;
        case Ztls_EXTENSION_TYPE_SERVER_CERTIFICATE_TYPE:
            Ztls_decode_block(src, end, 1, {
                size_t list_size = end - src;

                /* RFC7250 4.1: No empty list, no list with single x509 element */
                if (list_size == 0 || (list_size == 1 && *src == Ztls_CERTIFICATE_TYPE_X509)) {
                    ret = Ztls_ALERT_DECODE_ERROR;
                    goto Exit;
                }

                do {
                    if (ch->server_certificate_types.count < Ztls_ELEMENTSOF(ch->server_certificate_types.list))
                        ch->server_certificate_types.list[ch->server_certificate_types.count++] = *src;
                    src++;
                } while (src != end);
            });
            break;
        case Ztls_EXTENSION_TYPE_COMPRESS_CERTIFICATE:
            Ztls_decode_block(src, end, 1, {
                do {
                    uint16_t id;
                    if ((ret = decode16(&id, &src, end)) != 0)
                        goto Exit;
                    if (ch->cert_compression_algos.count < Ztls_ELEMENTSOF(ch->cert_compression_algos.list))
                        ch->cert_compression_algos.list[ch->cert_compression_algos.count++] = id;
                } while (src != end);
            });
            break;
        case Ztls_EXTENSION_TYPE_SUPPORTED_GROUPS:
            ch->negotiated_groups = wire_bytes(src, end - src);
            break;
        case Ztls_EXTENSION_TYPE_SIGNATURE_ALGORITHMS:
            if ((ret = decode_signature_algorithms(&ch->signature_algorithms, &src, end)) != 0)
                goto Exit;
            break;
        case Ztls_EXTENSION_TYPE_KEY_SHARE:
            ch->key_shares = wire_bytes(src, end - src);
            break;
        case Ztls_EXTENSION_TYPE_SUPPORTED_VERSIONS:
            Ztls_decode_block(src, end, 1, {
                size_t selected_index = Ztls_ELEMENTSOF(supported_versions);
                do {
                    size_t i;
                    uint16_t v;
                    if ((ret = decode16(&v, &src, end)) != 0)
                        goto Exit;
                    for (i = 0; i != selected_index; ++i) {
                        if (supported_versions[i] == v) {
                            selected_index = i;
                            break;
                        }
                    }
                } while (src != end);
                if (selected_index != Ztls_ELEMENTSOF(supported_versions))
                    ch->selected_version = supported_versions[selected_index];
            });
            break;
        case Ztls_EXTENSION_TYPE_COOKIE:
            if (properties == NULL || properties->server.cookie.key == NULL) {
                ret = Ztls_ALERT_ILLEGAL_PARAMETER;
                goto Exit;
            }
            ch->cookie.all = wire_bytes(src, end - src);
            Ztls_decode_block(src, end, 2, {
                const auto tbs = src;
                Ztls_decode_open_block(src, end, 2, {
                    Ztls_decode_open_block(src, end, 1, {
                        ch->cookie.ch1_hash = wire_bytes(src, end - src);
                        src = end;
                    });
                    uint8_t sent_key_share;
                    if ((ret = decode8(&sent_key_share, &src, end)) != 0)
                        goto Exit;
                    switch (sent_key_share) {
                    case 0:
                        assert(!ch->cookie.sent_key_share);
                        break;
                    case 1:
                        ch->cookie.sent_key_share = 1;
                        break;
                    default:
                        ret = Ztls_ALERT_DECODE_ERROR;
                        goto Exit;
                    }
                });
                ch->cookie.tbs = wire_bytes(tbs, src - tbs);
                Ztls_decode_block(src, end, 1, {
                    ch->cookie.signature = wire_bytes(src, end - src);
                    src = end;
                });
            });
            break;
        case Ztls_EXTENSION_TYPE_PRE_SHARED_KEY: {
            size_t num_identities = 0;
            Ztls_decode_open_block(src, end, 2, {
                do {
                    client_hello_psk_identity_t psk{};
                    Ztls_decode_open_block(src, end, 2, {
                        if (end - src < 1) {
                            ret = Ztls_ALERT_DECODE_ERROR;
                            goto Exit;
                        }
                        psk.identity = wire_bytes(src, end - src);
                        src = end;
                    });
                    if ((ret = decode32(&psk.obfuscated_ticket_age, &src, end)) != 0)
                        goto Exit;
                    if (ch->psk.identities.count < Ztls_ELEMENTSOF(ch->psk.identities.list))
                        ch->psk.identities.list[ch->psk.identities.count++] = psk;
                    ++num_identities;
                } while (src != end);
            });
            ch->psk.hash_end = src;
            Ztls_decode_block(src, end, 2, {
                size_t num_binders = 0;
                do {
                    Ztls_decode_open_block(src, end, 1, {
                        if (num_binders < ch->psk.identities.count)
                            ch->psk.identities.list[num_binders].binder = wire_bytes(src, end - src);
                        src = end;
                    });
                    ++num_binders;
                } while (src != end);
                if (num_identities != num_binders) {
                    ret = Ztls_ALERT_ILLEGAL_PARAMETER;
                    goto Exit;
                }
            });
            ch->psk.is_last_extension = 1;
        } break;
        case Ztls_EXTENSION_TYPE_PSK_KEY_EXCHANGE_MODES:
            Ztls_decode_block(src, end, 1, {
                do {
                    uint8_t mode;
                    if ((ret = decode8(&mode, &src, end)) != 0)
                        goto Exit;
                    if (mode < sizeof(ch->psk.ke_modes) * 8)
                        ch->psk.ke_modes |= 1u << mode;
                } while (src != end);
            });
            break;
        case Ztls_EXTENSION_TYPE_EARLY_DATA:
            ch->psk.early_data_indication = 1;
            break;
        case Ztls_EXTENSION_TYPE_STATUS_REQUEST:
            ch->status_request = 1;
            break;
        case Ztls_EXTENSION_TYPE_TICKET_REQUEST:
            if (end - src != 2) {
                ret = Ztls_ALERT_DECODE_ERROR;
                goto Exit;
            }
            ch->ticket_request.new_session_count = *src++;
            ch->ticket_request.resumption_count = *src++;
            break;
        case Ztls_EXTENSION_TYPE_ENCRYPTED_CLIENT_HELLO:
            if ((ret = decode8(&ch->ech.type, &src, end)) != 0)
                goto Exit;
            switch (ch->ech.type) {
            case Ztls_ECH_CLIENT_HELLO_TYPE_OUTER:
                if ((ret = decode16(&ch->ech.cipher_suite.kdf, &src, end)) != 0 ||
                    (ret = decode16(&ch->ech.cipher_suite.aead, &src, end)) != 0)
                    goto Exit;
                if ((ret = decode8(&ch->ech.config_id, &src, end)) != 0)
                    goto Exit;
                Ztls_decode_open_block(src, end, 2, {
                    ch->ech.enc = wire_bytes(src, end - src);
                    src = end;
                });
                Ztls_decode_open_block(src, end, 2, {
                    if (src == end) {
                        ret = Ztls_ALERT_DECODE_ERROR;
                        goto Exit;
                    }
                    ch->ech.payload = wire_bytes(src, end - src);
                    src = end;
                });
                break;
            case Ztls_ECH_CLIENT_HELLO_TYPE_INNER:
                if (src != end) {
                    ret = Ztls_ALERT_DECODE_ERROR;
                    goto Exit;
                }
                ch->ech.payload = wire_bytes("", 0); /* non-zero base indicates that the extension was received */
                break;
            default:
                ret = Ztls_ALERT_ILLEGAL_PARAMETER;
                goto Exit;
            }
            src = end;
            break;
        default:
            if (tls_cbarg != NULL && should_collect_unknown_extension(tls_cbarg, properties, exttype)) {
                if ((ret = collect_unknown_extension(tls_cbarg, exttype, src, end, ch->unknown_extensions)) != 0)
                    goto Exit;
            }
            break;
        }
        src = end;
    });

    ret = 0;
Exit:
    return ret;
}

static int rebuild_ch_inner_extensions(buffer_t *buf, const uint8_t **src, const uint8_t *const end, const uint8_t *outer_ext,
                                       const uint8_t *outer_ext_end)
{
    int ret;

    Ztls_buffer_push_block(buf, 2, {
        Ztls_decode_open_block(*src, end, 2, {
            while (*src != end) {
                uint16_t exttype;
                if ((ret = decode16(&exttype, src, end)) != 0)
                    goto Exit;
                Ztls_decode_open_block(*src, end, 2, {
                    if (exttype == Ztls_EXTENSION_TYPE_ECH_OUTER_EXTENSIONS) {
                        Ztls_decode_open_block(*src, end, 1, {
                            do {
                                uint16_t reftype;
                                uint16_t outertype;
                                uint16_t outersize;
                                if ((ret = decode16(&reftype, src, end)) != 0)
                                    goto Exit;
                                if (reftype == Ztls_EXTENSION_TYPE_ENCRYPTED_CLIENT_HELLO) {
                                    ret = Ztls_ALERT_ILLEGAL_PARAMETER;
                                    goto Exit;
                                }
                                while (1) {
                                    if (decode16(&outertype, &outer_ext, outer_ext_end) != 0 ||
                                        decode16(&outersize, &outer_ext, outer_ext_end) != 0) {
                                        ret = Ztls_ALERT_ILLEGAL_PARAMETER;
                                        goto Exit;
                                    }
                                    assert(outer_ext_end - outer_ext >= outersize);
                                    if (outertype == reftype)
                                        break;
                                    outer_ext += outersize;
                                }
                                buffer_push_extension(buf, reftype, {
                                    Ztls_buffer_pushv(buf, outer_ext, outersize);
                                    outer_ext += outersize;
                                });
                            } while (*src != end);
                        });
                    } else {
                        buffer_push_extension(buf, exttype, {
                            Ztls_buffer_pushv(buf, *src, end - *src);
                            *src = end;
                        });
                    }
                });
            }
        });
    });

Exit:
    return ret;
}

static int rebuild_ch_inner(buffer_t *buf, const uint8_t *src, const uint8_t *const end,
                            struct st_client_hello_t *outer_ch, const uint8_t *outer_ext, const uint8_t *outer_ext_end)
{
#define COPY_BLOCK(capacity)                                                                                                       \
    do {                                                                                                                           \
        Ztls_decode_open_block(src, end, (capacity), {                                                                             \
            Ztls_buffer_push_block(buf, (capacity), { Ztls_buffer_pushv(buf, src, end - src); });                                  \
            src = end;                                                                                                             \
        });                                                                                                                        \
    } while (0)

    int ret;

    Ztls_buffer_push_message_body(buf, NULL, Ztls_HANDSHAKE_TYPE_CLIENT_HELLO, {
        { /* legacy_version */
            uint16_t legacy_version;
            if ((ret = decode16(&legacy_version, &src, end)) != 0)
                goto Exit;
            Ztls_buffer_push16(buf, legacy_version);
        }

        /* hello random */
        if (end - src < Ztls_HELLO_RANDOM_SIZE) {
            ret = Ztls_ALERT_DECODE_ERROR;
            goto Exit;
        }
        Ztls_buffer_pushv(buf, src, Ztls_HELLO_RANDOM_SIZE);
        src += Ztls_HELLO_RANDOM_SIZE;

        Ztls_decode_open_block(src, end, 1, {
            if (src != end) {
                ret = Ztls_ALERT_ILLEGAL_PARAMETER;
                goto Exit;
            }
        });
        Ztls_buffer_push_block(buf, 1,
                               { Ztls_buffer_pushv(buf, outer_ch->legacy_session_id.data(), outer_ch->legacy_session_id.length()); });

        /* cipher-suites and legacy-compression-methods */
        COPY_BLOCK(2);
        COPY_BLOCK(1);

        /* extensions */
        if ((ret = rebuild_ch_inner_extensions(buf, &src, end, outer_ext, outer_ext_end)) != 0)
            goto Exit;
    });

    /* padding must be all zero */
    for (; src != end; ++src) {
        if (*src != '\0') {
            ret = Ztls_ALERT_ILLEGAL_PARAMETER;
            goto Exit;
        }
    }

Exit:
    return ret;

#undef COPY_BLOCK
}

/* Wrapper function for invoking the on_client_hello callback, taking an exhaustive list of parameters as arguments. The intention
 * is to not miss setting them as we add new parameters to the struct. */
static inline int call_on_client_hello_cb(Ztls::Cxn *tls, Bytes server_name, Bytes raw_message,
                                          Bytes cipher_suites, Bytes *alpns, size_t num_alpns,
                                          const uint16_t *sig_algos, size_t num_sig_algos, const uint16_t *cert_comp_algos,
                                          size_t num_cert_comp_algos, const uint8_t *server_cert_types,
                                          size_t num_server_cert_types, const client_hello_psk_identity_t *psk_identities,
                                          size_t num_psk_identities, int incompatible_version)
{
    if (tls->ctx->on_client_hello == NULL)
        return 0;

    on_client_hello_parameters_t params = {server_name,
                                                raw_message,
                                                cipher_suites,
                                                {alpns, num_alpns},
                                                {sig_algos, num_sig_algos},
                                                {cert_comp_algos, num_cert_comp_algos},
                                                {server_cert_types, num_server_cert_types},
                                                {psk_identities, num_psk_identities},
                                                static_cast<unsigned>(incompatible_version)};
    return tls->ctx->on_client_hello->cb(tls->ctx->on_client_hello, tls, &params);
}

static int check_client_hello_constraints(context_t *ctx, struct st_client_hello_t *ch, const uint8_t *prev_random,
                                          int ech_is_inner_ch, Bytes raw_message, Ztls::Cxn *tls_cbarg)
{
    int is_second_flight = prev_random != 0;

    /* The following check is necessary so that we would be able to track the connection in SSLKEYLOGFILE, even though it might not
     * be for the safety of the protocol. */
    if (is_second_flight && !Ztls::ctEqual({ch->random_bytes, Ztls_HELLO_RANDOM_SIZE}, {prev_random, Ztls_HELLO_RANDOM_SIZE}))
        return Ztls_ALERT_HANDSHAKE_FAILURE;

    /* bail out if CH cannot be handled as TLS 1.3 */
    if (!is_supported_version(ch->selected_version)) {
        /* ECH: server MUST abort with an "illegal_parameter" alert if the client offers TLS 1.2 or below (draft-15 7.1) */
        if (ech_is_inner_ch)
            return Ztls_ALERT_ILLEGAL_PARAMETER;
        /* fail with PROTOCOL_VERSION alert, after providing the applications the raw CH and SNI to help them fallback */
        if (!is_second_flight) {
            int ret;
            if ((ret = call_on_client_hello_cb(tls_cbarg, ch->server_name, raw_message, ch->cipher_suites, ch->alpn.list,
                                               ch->alpn.count, NULL, 0, NULL, 0, NULL, 0, NULL, 0, 1)) != 0)
                return ret;
        }
        return Ztls_ALERT_PROTOCOL_VERSION;
    }

    /* Check TLS 1.3-specific constraints. Hereafter, we might exit without calling on_client_hello. That's fine because this CH is
     * ought to be rejected. */
    if (ch->legacy_version <= 0x0300) {
        /* RFC 8446 Appendix D.5: any endpoint receiving a Hello message with legacy_version set to 0x0300 MUST abort the handshake
         * with a "protocol_version" alert. */
        return Ztls_ALERT_PROTOCOL_VERSION;
    }
    if (!(ch->compression_methods.count == 1 && ch->compression_methods.ids[0] == 0))
        return Ztls_ALERT_ILLEGAL_PARAMETER;
    /* pre-shared key */
    if (ch->psk.hash_end != NULL) {
        /* PSK must be the last extension */
        if (!ch->psk.is_last_extension)
            return Ztls_ALERT_ILLEGAL_PARAMETER;
    } else {
        if (ch->psk.early_data_indication)
            return Ztls_ALERT_ILLEGAL_PARAMETER;
    }

    if (ech_is_inner_ch && ch->ech.payload.data() == NULL)
        return Ztls_ALERT_ILLEGAL_PARAMETER;
    if (ch->ech.payload.data() != NULL &&
        ch->ech.type != (ech_is_inner_ch ? Ztls_ECH_CLIENT_HELLO_TYPE_INNER : Ztls_ECH_CLIENT_HELLO_TYPE_OUTER))
        return Ztls_ALERT_ILLEGAL_PARAMETER;

    return 0;
}

static int vec_is_string(Bytes x, const char *y)
{
    return strncmp((const char *)x.data(), y, x.length()) == 0 && y[x.length()] == '\0';
}

/**
 * Looks for a PSK identity that can be used, and if found, updates the handshake state and returns the necessary variables. If
 * `context_t::pre_shared_key` is set, only tries handshake using those keys provided. Otherwise, tries resumption.
 */
static int try_psk_handshake(Ztls::Cxn *tls, size_t *psk_index, int *accept_early_data, struct st_client_hello_t *ch,
                             Bytes ch_trunc, int is_second_flight)
{
    buffer_t decbuf;
    Bytes secret, ticket_ctx, ticket_negotiated_protocol;
    uint64_t issue_at, now = tls->ctx->get_time->cb(tls->ctx->get_time);
    uint32_t age_add;
    uint16_t ticket_key_exchange_id, ticket_csid;
    uint8_t binder_key[Ztls_MAX_DIGEST_SIZE];
    int ret;

    buffer_init_rx(&decbuf, const_cast<char *>(""), 0);

    for (*psk_index = 0; *psk_index < ch->psk.identities.count; ++*psk_index) {
        client_hello_psk_identity_t *identity = ch->psk.identities.list + *psk_index;

        /* negotiate using fixed pre-shared key */
        if (tls->ctx->pre_shared_key.identity.data() != NULL) {
            if (identity->identity.length() == tls->ctx->pre_shared_key.identity.length() &&
                memcmp(identity->identity.data(), tls->ctx->pre_shared_key.identity.data(), identity->identity.length()) == 0) {
                *accept_early_data = ch->psk.early_data_indication && *psk_index == 0;
                tls->key_share = NULL;
                secret = tls->ctx->pre_shared_key.secret;
                goto Found;
            }
            continue;
        }

        /* decrypt ticket and decode */
        if (tls->ctx->encrypt_ticket == NULL || tls->ctx->key_exchanges == NULL)
            continue;
        int can_accept_early_data = *psk_index == 0;
        decbuf.off = 0;
        switch (tls->ctx->encrypt_ticket->cb(tls->ctx->encrypt_ticket, tls, 0, &decbuf, identity->identity)) {
        case 0: /* decrypted */
            break;
        case Ztls_ERROR_REJECT_EARLY_DATA: /* decrypted, but early data is rejected */
            can_accept_early_data = 0;
            break;
        default: /* decryption failure */
            continue;
        }
        if (decode_session_identifier(&issue_at, &secret, &age_add, &ticket_ctx, &ticket_key_exchange_id, &ticket_csid,
                                      &ticket_negotiated_protocol, decbuf.base, decbuf.base + decbuf.off) != 0)
            continue;
        /* check age */
        if (now < issue_at)
            continue;
        if (now - issue_at > (uint64_t)tls->ctx->ticket_lifetime * 1000)
            continue;
        *accept_early_data = 0;
        if (ch->psk.early_data_indication && can_accept_early_data) {
            /* accept early-data if abs(diff) between the reported age and the actual age is within += 10 seconds */
            int64_t delta = (now - issue_at) - (identity->obfuscated_ticket_age - age_add);
            if (delta < 0)
                delta = -delta;
            if (tls->ctx->max_early_data_size != 0 && delta <= Ztls_EARLY_DATA_MAX_DELAY)
                *accept_early_data = 1;
        }
        /* check ticket context */
        if (tls->ctx->ticket_context.is_set) {
            if (!(ticket_ctx.length() == sizeof(tls->ctx->ticket_context.bytes) &&
                  memcmp(ticket_ctx.data(), tls->ctx->ticket_context.bytes, ticket_ctx.length()) == 0))
                continue;
        } else {
            /* check server-name */
            if (ticket_ctx.length() != 0) {
                if (tls->server_name.data() == NULL)
                    continue;
                if (!vec_is_string(ticket_ctx, tls->server_name.data()))
                    continue;
            } else {
                if (tls->server_name.data() != NULL)
                    continue;
            }
        }
        { /* check key-exchange */
            key_exchange_algorithm_t **a;
            for (a = tls->ctx->key_exchanges; *a != NULL && (*a)->id != ticket_key_exchange_id; ++a)
                ;
            if (*a == NULL)
                continue;
            tls->key_share = *a;
        }
        /* check cipher-suite */
        if (ticket_csid != tls->cipher_suite->id)
            continue;
        /* check negotiated-protocol */
        if (ticket_negotiated_protocol.length() != 0) {
            if (tls->negotiated_protocol.data() == NULL)
                continue;
            if (!vec_is_string(ticket_negotiated_protocol, tls->negotiated_protocol.data()))
                continue;
        }
        /* check the length of the decrypted psk and the PSK binder */
        if (secret.length() != tls->key_schedule->hashes[0].algo->digest_size)
            continue;
        if (ch->psk.identities.list[*psk_index].binder.length() != tls->key_schedule->hashes[0].algo->digest_size)
            continue;

        /* found */
        goto Found;
    }

    /* not found */
    *psk_index = SIZE_MAX;
    *accept_early_data = 0;
    tls->key_share = NULL;
    ret = 0;
    goto Exit;

Found:
    if (!is_second_flight && (ret = key_schedule_extract(tls->key_schedule, secret)) != 0)
        goto Exit;
    if ((ret = derive_secret_with_empty_digest(tls->key_schedule, binder_key,
                                               tls->ctx->pre_shared_key.secret.data() != NULL ? "ext binder" : "res binder")) != 0)
        goto Exit;
    _key_schedule_update_hash(tls->key_schedule, ch_trunc.data(), ch_trunc.length(), 0);
    if ((ret = calc_verify_data(binder_key /* to conserve space, reuse binder_key for storing verify_data */, tls->key_schedule,
                                binder_key)) != 0)
        goto Exit;
    if (!Ztls::ctEqual({ch->psk.identities.list[*psk_index].binder.data(), tls->key_schedule->hashes[0].algo->digest_size},
                         {binder_key, tls->key_schedule->hashes[0].algo->digest_size})) {
        ret = Ztls_ALERT_DECRYPT_ERROR;
        goto Exit;
    }
    ret = 0;

Exit:
    buffer_dispose(&decbuf);
    ZuClear(binder_key, sizeof(binder_key));
    return ret;
}

static int calc_cookie_signature(Ztls::Cxn *tls, handshake_properties_t *properties,
                                 key_exchange_algorithm_t *negotiated_group, Bytes tbs, uint8_t *sig)
{
    hash_algorithm_t *algo = tls->ctx->cipher_suites[0]->hash;
    hash_context_t *hctx;

    if ((hctx = hmac_create(algo, properties->server.cookie.key, algo->digest_size)) == NULL)
        return Ztls_ERROR_NO_MEMORY;

#define UPDATE_BLOCK(p, _len)                                                                                                      \
    do {                                                                                                                           \
        size_t len = (_len);                                                                                                       \
        assert(len < UINT8_MAX);                                                                                                   \
        uint8_t len8 = (uint8_t)len;                                                                                               \
        hctx->update(hctx, &len8, 1);                                                                                              \
        hctx->update(hctx, (p), len);                                                                                              \
    } while (0)
#define UPDATE16(_v)                                                                                                               \
    do {                                                                                                                           \
        uint16_t v = (_v);                                                                                                         \
        uint8_t b[2] = {uint8_t(v >> 8), uint8_t(v & 0xff)};                                                                                         \
        hctx->update(hctx, b, 2);                                                                                                  \
    } while (0)

    UPDATE_BLOCK(tls->client_random, sizeof(tls->client_random));
    UPDATE_BLOCK(tls->server_name.data(), tls->server_name.data() != NULL ? strlen(tls->server_name.data()) : 0);
    UPDATE16(tls->cipher_suite->id);
    UPDATE16(negotiated_group != NULL ? negotiated_group->id : 0);
    UPDATE_BLOCK(properties->server.cookie.additional_data.data(), properties->server.cookie.additional_data.length());

    UPDATE_BLOCK(tbs.data(), tbs.length());

#undef UPDATE_BLOCK
#undef UPDATE16

    hctx->final(hctx, sig, HASH_FINAL_MODE_FREE);
    return 0;
}

static int certificate_type_exists(uint8_t *list, size_t count, uint8_t desired_type)
{
    /* empty type list means that we default to x509 */
    if (desired_type == Ztls_CERTIFICATE_TYPE_X509 && count == 0)
        return 1;
    for (size_t i = 0; i < count; i++) {
        if (list[i] == desired_type)
            return 1;
    }
    return 0;
}

static int server_handle_hello(Ztls::Cxn *tls, message_emitter_t *emitter, Bytes message,
                               handshake_properties_t *properties)
{
#define EMIT_SERVER_HELLO(sched, fill_rand, extensions, post_action)                                                               \
    do {                                                                                                                           \
        size_t sh_start_off;                                                                                                       \
        Ztls_push_message(emitter, NULL, Ztls_HANDSHAKE_TYPE_SERVER_HELLO, {                                                       \
            sh_start_off = emitter->buf->off - Ztls_HANDSHAKE_HEADER_SIZE;                                                         \
            Ztls_buffer_push16(emitter->buf, 0x0303 /* legacy version */);                                                         \
            if ((ret = buffer_reserve(emitter->buf, Ztls_HELLO_RANDOM_SIZE, 1)) != 0)                                      \
                goto Exit;                                                                                                         \
            do {                                                                                                                   \
                fill_rand                                                                                                          \
            } while (0);                                                                                                           \
            emitter->buf->off += Ztls_HELLO_RANDOM_SIZE;                                                                           \
            Ztls_buffer_push_block(emitter->buf, 1,                                                                                \
                                   { Ztls_buffer_pushv(emitter->buf, ch->legacy_session_id.data(), ch->legacy_session_id.length()); });   \
            Ztls_buffer_push16(emitter->buf, tls->cipher_suite->id);                                                               \
            Ztls_buffer_push(emitter->buf, 0);                                                                                     \
            Ztls_buffer_push_block(emitter->buf, 2, {                                                                              \
                buffer_push_extension(emitter->buf, Ztls_EXTENSION_TYPE_SUPPORTED_VERSIONS,                                        \
                                      { Ztls_buffer_push16(emitter->buf, ch->selected_version); });                                \
                do {                                                                                                               \
                    extensions                                                                                                     \
                } while (0);                                                                                                       \
            });                                                                                                                    \
        });                                                                                                                        \
        do {                                                                                                                       \
            post_action                                                                                                            \
        } while (0);                                                                                                               \
        _key_schedule_update_hash((sched), emitter->buf->base + sh_start_off, emitter->buf->off - sh_start_off, 0);           \
    } while (0)

#define EMIT_HELLO_RETRY_REQUEST(sched, negotiated_group, additional_extensions, post_action)                                      \
    EMIT_SERVER_HELLO((sched), { memcpy(emitter->buf->base + emitter->buf->off, hello_retry_random, Ztls_HELLO_RANDOM_SIZE); },    \
                      {                                                                                                            \
                          key_exchange_algorithm_t *_negotiated_group = (negotiated_group);                                   \
                          if (_negotiated_group != NULL) {                                                                         \
                              buffer_push_extension(emitter->buf, Ztls_EXTENSION_TYPE_KEY_SHARE,                                   \
                                                    { Ztls_buffer_push16(emitter->buf, _negotiated_group->id); });                 \
                          }                                                                                                        \
                          do {                                                                                                     \
                              additional_extensions                                                                                \
                          } while (0);                                                                                             \
                      },                                                                                                           \
                      post_action)
    auto decoded = ZtScratch(DecodedClientHello, 1, 1);
    auto extensions = ZtScratch(CollectedExtensions, MAX_UNKNOWN_EXTENSIONS + 1, MAX_UNKNOWN_EXTENSIONS + 1);
    auto ch = decoded.data();
    struct {
        key_exchange_algorithm_t *algorithm;
        Bytes peer_key;
    } key_share = {NULL};
    struct {
        EchBytes encoded_ch_inner;
        EchBytes ch_outer_aad;
        buffer_t ch_inner;
    } ech{};
    enum { HANDSHAKE_MODE_FULL, HANDSHAKE_MODE_PSK, HANDSHAKE_MODE_PSK_DHE } mode;
    size_t psk_index = SIZE_MAX;
    KeyExchangeBytes pubkey, ecdh_secret;
    int accept_early_data = 0, is_second_flight = tls->state == Ztls_STATE_SERVER_EXPECT_SECOND_CLIENT_HELLO, ret;

    buffer_init_rx(&ech.ch_inner, const_cast<char *>(""), 0);

    extensions[0] = {UINT16_MAX};
    *ch = st_client_hello_t{.unknown_extensions = extensions.data()};

    /* decode ClientHello */
    if ((ret = decode_client_hello(tls->ctx, ch, message.data() + Ztls_HANDSHAKE_HEADER_SIZE, message.data() + message.length(), properties,
                                   tls)) != 0)
        goto Exit;
    if ((ret = check_client_hello_constraints(tls->ctx, ch, is_second_flight ? tls->client_random : NULL, 0, message, tls)) != 0)
        goto Exit;
    if (!is_second_flight) {
        memcpy(tls->client_random, ch->random_bytes, Ztls_HELLO_RANDOM_SIZE);
    } else {
        /* consistency check for ECH extension in response to HRR */
        if (tls->ech.aead != NULL) {
            if (ch->ech.payload.data() == NULL) {
                ret = Ztls_ALERT_MISSING_EXTENSION;
                goto Exit;
            }
            if (!(ch->ech.config_id == tls->ech.config_id && ch->ech.cipher_suite.kdf == tls->ech.cipher->id.kdf &&
                  ch->ech.cipher_suite.aead == tls->ech.cipher->id.aead && ch->ech.enc.length() == 0)) {
                ret = Ztls_ALERT_ILLEGAL_PARAMETER;
                goto Exit;
            }
        }
    }

    /* ECH */
    if (ch->ech.payload.data() != NULL) {
        if (ch->ech.type != Ztls_ECH_CLIENT_HELLO_TYPE_OUTER) {
            ret = Ztls_ALERT_ILLEGAL_PARAMETER;
            goto Exit;
        }
        if (!is_second_flight)
            tls->ech.state = Ztls_ECH_STATE_OFFERED;
        /* obtain AEAD context for opening inner CH */
        if (!is_second_flight && ch->ech.payload.data() != NULL && tls->ctx->ech.server.create_opener != NULL) {
            if ((tls->ech.aead = tls->ctx->ech.server.create_opener->cb(
                     tls->ctx->ech.server.create_opener, &tls->ech.kem, &tls->ech.cipher, tls, ch->ech.config_id,
                     ch->ech.cipher_suite, ch->ech.enc, wire_bytes(ech_info_prefix, sizeof(ech_info_prefix)))) != NULL)
                tls->ech.config_id = ch->ech.config_id;
        }
        if (!is_second_flight) {
        }
        if (tls->ech.aead != NULL) {
            /* now that AEAD context is available, create AAD and decrypt inner CH */
            auto tag_size = tls->ech.aead->algo->tag_size;
            // Preserve the recoverable error for the previously impossible allocation size.
            if (ch->ech.payload.length() < tag_size) {
                ret = Ztls_ERROR_NO_MEMORY;
                goto Exit;
            }
            auto inner_length = ch->ech.payload.length() - tag_size;
            ech.encoded_ch_inner.length(inner_length ? inner_length : 1);
            ech.ch_outer_aad.copy(message.data() + Ztls_HANDSHAKE_HEADER_SIZE, message.length() - Ztls_HANDSHAKE_HEADER_SIZE);
            memset(ech.ch_outer_aad.data() + (ch->ech.payload.data() - (message.data() + Ztls_HANDSHAKE_HEADER_SIZE)), 0, ch->ech.payload.length());
            if (aead_decrypt(tls->ech.aead, ech.encoded_ch_inner.data(), ch->ech.payload.data(), ch->ech.payload.length(), is_second_flight,
                                  ech.ch_outer_aad.data(), ech.ch_outer_aad.length()) != SIZE_MAX) {
                tls->ech.state = Ztls_ECH_STATE_ACCEPTED;
                /* successfully decrypted EncodedCHInner, build CHInner */
                if ((ret = rebuild_ch_inner(&ech.ch_inner, ech.encoded_ch_inner.data(),
                                            ech.encoded_ch_inner.data() + inner_length, ch,
                                            message.data() + Ztls_HANDSHAKE_HEADER_SIZE + ch->first_extension_at,
                                            message.data() + message.length())) != 0)
                    goto Exit;
                /* treat inner ch as the message being received, re-decode it */
                message = wire_bytes(ech.ch_inner.base, ech.ch_inner.off);
                extensions[0] = {UINT16_MAX};
                *ch = st_client_hello_t{.unknown_extensions = extensions.data()};
                if ((ret = decode_client_hello(tls->ctx, ch, ech.ch_inner.base + Ztls_HANDSHAKE_HEADER_SIZE,
                                               ech.ch_inner.base + ech.ch_inner.off, properties, tls)) != 0)
                    goto Exit;
                if ((ret = check_client_hello_constraints(tls->ctx, ch, is_second_flight ? tls->ech.inner_client_random : NULL, 1,
                                                          message, tls)) != 0)
                    goto Exit;
                if (!is_second_flight)
                    memcpy(tls->ech.inner_client_random, ch->random_bytes, Ztls_HELLO_RANDOM_SIZE);
            } else if (is_second_flight) {
                /* decryption failure of inner CH in 2nd CH is fatal */
                ret = Ztls_ALERT_DECRYPT_ERROR;
                goto Exit;
            } else {
                /* decryption failure of 1st CH indicates key mismatch; dispose of AEAD context to indicate adoption of outerCH */
                aead_free(tls->ech.aead);
                tls->ech.aead = NULL;
            }
        }
    } else if (tls->ech.state != Ztls_ECH_STATE_NONE) {
        assert(is_second_flight);
        ret = Ztls_ALERT_ILLEGAL_PARAMETER;
        goto Exit;
    }

    if (tls->ctx->require_dhe_on_psk)
        ch->psk.ke_modes &= ~(1u << Ztls_PSK_KE_MODE_PSK);

    /* handle client_random, legacy_session_id, SNI, ESNI */
    if (!is_second_flight) {
        if (ch->legacy_session_id.length() != 0)
            tls->send_change_cipher_spec = 1;
        Bytes server_name = {};
        if (ch->server_name.data() != NULL)
            server_name = ch->server_name;
        if ((ret = call_on_client_hello_cb(tls, server_name, message, ch->cipher_suites, ch->alpn.list, ch->alpn.count,
                                           ch->signature_algorithms.list, ch->signature_algorithms.count,
                                           ch->cert_compression_algos.list, ch->cert_compression_algos.count,
                                           ch->server_certificate_types.list, ch->server_certificate_types.count,
                                           ch->psk.identities.list, ch->psk.identities.count, 0)) != 0)
            goto Exit;
        if (!certificate_type_exists(ch->server_certificate_types.list, ch->server_certificate_types.count,
                                     tls->ctx->use_raw_public_keys ? Ztls_CERTIFICATE_TYPE_RAW_PUBLIC_KEY
                                                                   : Ztls_CERTIFICATE_TYPE_X509)) {
            ret = Ztls_ALERT_UNSUPPORTED_CERTIFICATE;
            goto Exit;
        }
    } else {
        if (ch->psk.early_data_indication) {
            ret = Ztls_ALERT_DECODE_ERROR;
            goto Exit;
        }
        /* We compare SNI only when the value is saved by the on_client_hello callback. This should be OK because we are
         * ignoring the value unless the callback saves the server-name. */
        if (tls->server_name.data() != NULL) {
            size_t l = strlen(tls->server_name.data());
            if (!(ch->server_name.length() == l && memcmp(ch->server_name.data(), tls->server_name.data(), l) == 0)) {
                ret = Ztls_ALERT_HANDSHAKE_FAILURE;
                goto Exit;
            }
        }
    }

    { /* select (or check) cipher-suite, create key_schedule */
        cipher_suite_t *cs;
        if ((ret = select_cipher(&cs, tls->ctx->cipher_suites, ch->cipher_suites.data(),
                                 ch->cipher_suites.data() + ch->cipher_suites.length(), tls->ctx->server_cipher_preference,
                                 tls->ctx->server_cipher_chacha_priority, tls->ctx->pre_shared_key.hash)) != 0)
            goto Exit;
        if (!is_second_flight) {
            tls->cipher_suite = cs;
            if ((tls->key_schedule = key_schedule_new(cs, NULL, 0)) == NULL) {
                ret = Ztls_ERROR_NO_MEMORY;
                goto Exit;
            }
        } else {
            if (tls->cipher_suite != cs) {
                ret = Ztls_ALERT_HANDSHAKE_FAILURE;
                goto Exit;
            }
        }
    }

    /* select key_share */
    if (key_share.algorithm == NULL && ch->key_shares.data() != NULL && tls->ctx->key_exchanges != NULL) {
        const uint8_t *src = ch->key_shares.data(), *const end = src + ch->key_shares.length();
        Ztls_decode_block(src, end, 2, {
            if ((ret = select_key_share(&key_share.algorithm, &key_share.peer_key, tls->ctx->key_exchanges, &src, end, 0)) != 0)
                goto Exit;
        });
    }

    if (!is_second_flight) {
        if (ch->cookie.all.length() != 0 && key_share.algorithm != NULL) {

            { /* use cookie to check the integrity of the handshake, and update the context */
                uint8_t sig[Ztls_MAX_DIGEST_SIZE];
                size_t sigsize = tls->ctx->cipher_suites[0]->hash->digest_size;
                if ((ret = calc_cookie_signature(tls, properties, key_share.algorithm, ch->cookie.tbs, sig)) != 0)
                    goto Exit;
                if (!(ch->cookie.signature.length() == sigsize && Ztls::ctEqual({ch->cookie.signature.data(), sigsize}, {sig, sigsize}))) {
                    ret = Ztls_ALERT_HANDSHAKE_FAILURE;
                    goto Exit;
                }
            }
            /* integrity check passed; update states */
            key_schedule_update_ch1hash_prefix(tls->key_schedule);
            _key_schedule_update_hash(tls->key_schedule, ch->cookie.ch1_hash.data(), ch->cookie.ch1_hash.length(), 0);
            key_schedule_extract(tls->key_schedule,
                                 tls->ctx->pre_shared_key.secret /* this argument will be a zero-length vector unless external PSK
                                                                  is used, and that's fine; we never resume when sending HRR */);
            /* ... reusing sendbuf to rebuild HRR for hash calculation */
            size_t hrr_start = emitter->buf->off;
            EMIT_HELLO_RETRY_REQUEST(tls->key_schedule, ch->cookie.sent_key_share ? key_share.algorithm : NULL,
                                     {
                                         buffer_push_extension(emitter->buf, Ztls_EXTENSION_TYPE_COOKIE, {
                                             Ztls_buffer_pushv(emitter->buf, ch->cookie.all.data(), ch->cookie.all.length());
                                         });
                                     },
                                     {});
            emitter->buf->off = hrr_start;
            is_second_flight = 1;

        } else if (ch->key_shares.data() != NULL && tls->ctx->key_exchanges != NULL &&
                   (key_share.algorithm == NULL || (properties != NULL && properties->server.enforce_retry))) {
            /* send HelloRetryRequest, when trying to negotiate the key share but enforced by config or upon key-share mismatch */
            if (ch->negotiated_groups.data() == NULL) {
                ret = Ztls_ALERT_MISSING_EXTENSION;
                goto Exit;
            }
            key_exchange_algorithm_t *negotiated_group;
            if ((ret = select_negotiated_group(&negotiated_group, tls->ctx->key_exchanges, ch->negotiated_groups.data(),
                                               ch->negotiated_groups.data() + ch->negotiated_groups.length())) != 0)
                goto Exit;
            _key_schedule_update_hash(tls->key_schedule, message.data(), message.length(), 0);
            assert(tls->key_schedule->generation == 0);

            /* Either send a stateless retry (w. cookies) or a stateful one. When sending the latter, run the state machine. At the
             * moment, stateless retry is disabled when ECH is used (do we need to support it?). */
            int retry_uses_cookie =
                properties != NULL && properties->server.retry_uses_cookie && !is_ech_handshake(tls, NULL, NULL, NULL);
            if (!retry_uses_cookie) {
                key_schedule_transform_post_ch1hash(tls->key_schedule);
                key_schedule_extract(tls->key_schedule, tls->ctx->pre_shared_key.secret /* see comment above */);
            }
            size_t ech_confirm_off = 0;
            EMIT_HELLO_RETRY_REQUEST(
                tls->key_schedule, key_share.algorithm != NULL ? NULL : negotiated_group,
                {
                    buffer_t *sendbuf = emitter->buf;
                    if (is_ech_handshake(tls, NULL, NULL, NULL)) {
                        buffer_push_extension(sendbuf, Ztls_EXTENSION_TYPE_ENCRYPTED_CLIENT_HELLO, {
                            if ((ret = buffer_reserve(sendbuf, Ztls_ECH_CONFIRM_LENGTH, 1)) != 0)
                                goto Exit;
                            memset(sendbuf->base + sendbuf->off, 0, Ztls_ECH_CONFIRM_LENGTH);
                            ech_confirm_off = sendbuf->off;
                            sendbuf->off += Ztls_ECH_CONFIRM_LENGTH;
                        });
                    }
                    if (retry_uses_cookie) {
                        buffer_push_extension(sendbuf, Ztls_EXTENSION_TYPE_COOKIE, {
                            Ztls_buffer_push_block(sendbuf, 2, {
                                /* push to-be-signed data */
                                size_t tbs_start = sendbuf->off;
                                Ztls_buffer_push_block(sendbuf, 2, {
                                    /* first block of the cookie data is the hash(ch1) */
                                    Ztls_buffer_push_block(sendbuf, 1, {
                                        size_t sz = tls->cipher_suite->hash->digest_size;
                                        if ((ret = buffer_reserve(sendbuf, sz, 1)) != 0)
                                            goto Exit;
                                        key_schedule_extract_ch1hash(tls->key_schedule, sendbuf->base + sendbuf->off);
                                        sendbuf->off += sz;
                                    });
                                    /* second is if we have sent key_share extension */
                                    Ztls_buffer_push(sendbuf, key_share.algorithm == NULL);
                                    /* we can add more data here */
                                });
                                size_t tbs_len = sendbuf->off - tbs_start;
                                /* push the signature */
                                Ztls_buffer_push_block(sendbuf, 1, {
                                    size_t sz = tls->ctx->cipher_suites[0]->hash->digest_size;
                                    if ((ret = buffer_reserve(sendbuf, sz, 1)) != 0)
                                        goto Exit;
                                    if ((ret = calc_cookie_signature(tls, properties, negotiated_group,
                                                                     wire_bytes(sendbuf->base + tbs_start, tbs_len),
                                                                     sendbuf->base + sendbuf->off)) != 0)
                                        goto Exit;
                                    sendbuf->off += sz;
                                });
                            });
                        });
                    }
                },
                {
                    if (ech_confirm_off != 0 &&
                        (ret = ech_calc_confirmation(
                             tls->key_schedule, emitter->buf->base + ech_confirm_off, tls->ech.inner_client_random,
                             ECH_CONFIRMATION_HRR,
                             wire_bytes(emitter->buf->base + sh_start_off, emitter->buf->off - sh_start_off))) != 0)
                        goto Exit;
                });
            if (retry_uses_cookie) {
                if ((ret = push_change_cipher_spec(tls, emitter)) != 0)
                    goto Exit;
                ret = Ztls_ERROR_STATELESS_RETRY;
            } else {
                tls->state = Ztls_STATE_SERVER_EXPECT_SECOND_CLIENT_HELLO;
                if (ch->psk.early_data_indication)
                    tls->early_data_skipped_bytes = 0;
                ret = Ztls_ERROR_IN_PROGRESS;
            }
            goto Exit;
        }
    }

    /* handle unknown extensions */
    if ((ret = report_unknown_extensions(tls, properties, ch->unknown_extensions)) != 0)
        goto Exit;

    /* try psk handshake */
    if (ch->psk.hash_end != 0 && (ch->psk.ke_modes & ((1u << Ztls_PSK_KE_MODE_PSK) | (1u << Ztls_PSK_KE_MODE_PSK_DHE))) != 0 &&
        !tls->ctx->require_client_authentication &&
        ((!is_second_flight && tls->ctx->encrypt_ticket != NULL) || tls->ctx->pre_shared_key.identity.data() != NULL)) {
        if ((ret = try_psk_handshake(tls, &psk_index, &accept_early_data, ch,
                                     wire_bytes(message.data(), ch->psk.hash_end - message.data()), is_second_flight)) != 0) {
            goto Exit;
        }
    }

    /* If the server was setup to use an external PSK but failed to agree, abort the handshake. Because external PSK is a form of
     * mutual authentication, it makes sense to abort (at least as the default). */
    if (tls->ctx->pre_shared_key.identity.data() != NULL && psk_index == SIZE_MAX) {
        ret = Ztls_ALERT_UNKNOWN_PSK_IDENTITY;
        goto Exit;
    }

    /* If client authentication is enabled, we always force a full handshake.
     * TODO: Check for `post_handshake_auth` extension and if that is present, do not force full handshake!
     *       Remove also the check `!require_client_authentication` above.
     *
     * adjust key_schedule, determine handshake mode
     */
    if (psk_index == SIZE_MAX || tls->ctx->require_client_authentication) {
        _key_schedule_update_hash(tls->key_schedule, message.data(), message.length(), 0);
        if (!is_second_flight) {
            assert(tls->key_schedule->generation == 0);
            key_schedule_extract(tls->key_schedule, wire_bytes(NULL, 0));
        }
        mode = HANDSHAKE_MODE_FULL;
        if (properties != NULL)
            properties->server.selected_psk_binder.len = 0;
    } else {
        _key_schedule_update_hash(tls->key_schedule, ch->psk.hash_end, message.data() + message.length() - ch->psk.hash_end, 0);
        if ((ch->psk.ke_modes & (1u << Ztls_PSK_KE_MODE_PSK)) != 0) {
            mode = HANDSHAKE_MODE_PSK;
        } else {
            assert((ch->psk.ke_modes & (1u << Ztls_PSK_KE_MODE_PSK_DHE)) != 0);
            mode = HANDSHAKE_MODE_PSK_DHE;
        }
        tls->is_psk_handshake = 1;
        if (properties != NULL) {
            Bytes *selected = &ch->psk.identities.list[psk_index].binder;
            memcpy(properties->server.selected_psk_binder.base, selected->data(), selected->length());
            properties->server.selected_psk_binder.len = selected->length();
        }
    }

    /* determine number of tickets to send */
    if (ch->psk.ke_modes != 0 && tls->ctx->ticket_lifetime != 0) {
        if (ch->ticket_request.new_session_count != 0) {
            tls->num_tickets_to_send =
                tls->is_psk_handshake ? ch->ticket_request.resumption_count : ch->ticket_request.new_session_count;
        } else {
            tls->num_tickets_to_send = 1;
        }
        uint8_t max_tickets = tls->ctx->ticket_requests.server.max_count;
        if (max_tickets == 0)
            max_tickets = Ztls_DEFAULT_MAX_TICKETS_TO_SERVE;
        if (tls->num_tickets_to_send > max_tickets)
            tls->num_tickets_to_send = max_tickets;
    } else {
        tls->num_tickets_to_send = 0;
    }

    if (accept_early_data && tls->ctx->max_early_data_size != 0 && psk_index == 0) {
        tls->pending_handshake_secret.length(Ztls_MAX_DIGEST_SIZE);
        if ((ret = derive_exporter_secret(tls, 1)) != 0)
            goto Exit;
        if ((ret = setup_traffic_protection(tls, 0, "c e traffic", 1, 0, 0)) != 0)
            goto Exit;
    }

    /* run key-exchange, to obtain pubkey and secret */
    if (mode != HANDSHAKE_MODE_PSK) {
        if (key_share.algorithm == NULL) {
            ret = ch->key_shares.data() != NULL ? Ztls_ALERT_HANDSHAKE_FAILURE : Ztls_ALERT_MISSING_EXTENSION;
            goto Exit;
        }
        if ((ret = key_share.algorithm->exchange(key_share.algorithm, &pubkey, &ecdh_secret, key_share.peer_key)) != 0) {
            assert(!pubkey.length());
            assert(!ecdh_secret.length());
            goto Exit;
        }
        tls->key_share = key_share.algorithm;
    }

    { /* send ServerHello */
        size_t ech_confirm_off = 0;
        EMIT_SERVER_HELLO(
            tls->key_schedule,
            {
                tls->ctx->random_bytes(emitter->buf->base + emitter->buf->off, Ztls_HELLO_RANDOM_SIZE);
                /* when accepting CHInner, last 8 byte of SH.random is zero for the handshake transcript */
                if (is_ech_handshake(tls, NULL, NULL, NULL)) {
                    ech_confirm_off = emitter->buf->off + Ztls_HELLO_RANDOM_SIZE - Ztls_ECH_CONFIRM_LENGTH;
                    memset(emitter->buf->base + ech_confirm_off, 0, Ztls_ECH_CONFIRM_LENGTH);
                }
            },
            {
                buffer_t *sendbuf = emitter->buf;
                if (mode != HANDSHAKE_MODE_PSK) {
                    buffer_push_extension(sendbuf, Ztls_EXTENSION_TYPE_KEY_SHARE, {
                        Ztls_buffer_push16(sendbuf, key_share.algorithm->id);
                        Ztls_buffer_push_block(sendbuf, 2, { Ztls_buffer_pushv(sendbuf, pubkey.data(), pubkey.length()); });
                    });
                }
                if (mode != HANDSHAKE_MODE_FULL) {
                    buffer_push_extension(sendbuf, Ztls_EXTENSION_TYPE_PRE_SHARED_KEY,
                                          { Ztls_buffer_push16(sendbuf, (uint16_t)psk_index); });
                }
            },
            {
                if (ech_confirm_off != 0 &&
                    (ret = ech_calc_confirmation(
                         tls->key_schedule, emitter->buf->base + ech_confirm_off, tls->ech.inner_client_random,
                         ECH_CONFIRMATION_SERVER_HELLO,
                         wire_bytes(emitter->buf->base + sh_start_off, emitter->buf->off - sh_start_off))) != 0)
                    goto Exit;
            });
    }

    /* processing of ECH is complete; dispose state */
    clear_ech(&tls->ech, 1);

    if ((ret = push_change_cipher_spec(tls, emitter)) != 0)
        goto Exit;

    /* create protection contexts for the handshake */
    assert(tls->key_schedule->generation == 1);
    key_schedule_extract(tls->key_schedule, wire_bytes(ecdh_secret.data(), ecdh_secret.length()));
    if ((ret = setup_traffic_protection(tls, 1, "s hs traffic", 2, 0, 0)) != 0)
        goto Exit;
    if (tls->pending_handshake_secret.data() != NULL) {
        if ((ret = derive_secret(tls->key_schedule, tls->pending_handshake_secret.data(), "c hs traffic")) != 0)
            goto Exit;
        if (tls->ctx->update_traffic_key != NULL &&
            (ret = tls->ctx->update_traffic_key->cb(tls->ctx->update_traffic_key, tls, 0, 2, tls->pending_handshake_secret.data())) != 0)
            goto Exit;
    } else {
        if ((ret = setup_traffic_protection(tls, 0, "c hs traffic", 2, 0, 0)) != 0)
            goto Exit;
        if (ch->psk.early_data_indication)
            tls->early_data_skipped_bytes = 0;
    }

    /* send EncryptedExtensions */
    Ztls_push_message(emitter, tls->key_schedule, Ztls_HANDSHAKE_TYPE_ENCRYPTED_EXTENSIONS, {
        buffer_t *sendbuf = emitter->buf;
        Ztls_buffer_push_block(sendbuf, 2, {
            if (tls->server_name.data() != NULL) {
                /* In this event, the server SHALL include an extension of type "server_name" in the (extended) server hello.
                 * The "extension_data" field of this extension SHALL be empty. (RFC 6066 section 3) */
                buffer_push_extension(sendbuf, Ztls_EXTENSION_TYPE_SERVER_NAME, {});
            }
            if (tls->ctx->use_raw_public_keys) {
                buffer_push_extension(sendbuf, Ztls_EXTENSION_TYPE_SERVER_CERTIFICATE_TYPE,
                                      { Ztls_buffer_push(sendbuf, Ztls_CERTIFICATE_TYPE_RAW_PUBLIC_KEY); });
            }
            if (tls->negotiated_protocol.data() != NULL) {
                buffer_push_extension(sendbuf, Ztls_EXTENSION_TYPE_ALPN, {
                    Ztls_buffer_push_block(sendbuf, 2, {
                        Ztls_buffer_push_block(sendbuf, 1, {
                            Ztls_buffer_pushv(sendbuf, tls->negotiated_protocol.data(), strlen(tls->negotiated_protocol.data()));
                        });
                    });
                });
            }
            if (tls->pending_handshake_secret.data() != NULL)
                buffer_push_extension(sendbuf, Ztls_EXTENSION_TYPE_EARLY_DATA, {});
            /* send ECH retry_configs, if ECH was offered by rejected, even though we (the server) could have accepted ECH */
            if (tls->ech.state == Ztls_ECH_STATE_OFFERED && tls->ctx->ech.server.create_opener != NULL &&
                tls->ctx->ech.server.retry_configs.length() != 0)
                buffer_push_extension(sendbuf, Ztls_EXTENSION_TYPE_ENCRYPTED_CLIENT_HELLO, {
                    Ztls_buffer_pushv(sendbuf, tls->ctx->ech.server.retry_configs.data(), tls->ctx->ech.server.retry_configs.length());
                });
            if (ch->ticket_request.new_session_count != 0 && tls->num_tickets_to_send != 0)
                buffer_push_extension(sendbuf, Ztls_EXTENSION_TYPE_TICKET_REQUEST,
                                      { Ztls_buffer_push(sendbuf, tls->num_tickets_to_send); });
            if ((ret = push_additional_extensions(properties, sendbuf)) != 0)
                goto Exit;
        });
    });

    if (mode == HANDSHAKE_MODE_FULL) {
        /* send certificate request if client authentication is activated */
        if (tls->ctx->require_client_authentication) {
            Ztls_push_message(emitter, tls->key_schedule, Ztls_HANDSHAKE_TYPE_CERTIFICATE_REQUEST, {
                buffer_t *sendbuf = emitter->buf;
                /* certificate_request_context: this field SHALL be zero length, unless the certificate request is used for post-
                 * handshake authentication. */
                Ztls_buffer_push(sendbuf, 0);
                /* extensions */
                Ztls_buffer_push_block(sendbuf, 2, {
                    buffer_push_extension(sendbuf, Ztls_EXTENSION_TYPE_SIGNATURE_ALGORITHMS, {
                        if ((ret = push_signature_algorithms(tls->ctx->verify_certificate, sendbuf)) != 0)
                            goto Exit;
                    });
                    /* certificate authorities entension */
                    if (tls->ctx->client_ca_names.count > 0) {
                        buffer_push_extension(sendbuf, Ztls_EXTENSION_TYPE_CERTIFICATE_AUTHORITIES, {
                            Ztls_buffer_push_block(sendbuf, 2, {
                                for (size_t i = 0; i != tls->ctx->client_ca_names.count; ++i) {
                                    Ztls_buffer_push_block(sendbuf, 2, {
                                        Bytes name = tls->ctx->client_ca_names.list[i];
                                        Ztls_buffer_pushv(sendbuf, name.data(), name.length());
                                    });
                                }
                            });
                        });
                    }
                });
            });

            if (ret != 0) {
                goto Exit;
            }
        }

        /* send certificate */
        if ((ret = send_certificate(tls, emitter, &ch->signature_algorithms, wire_bytes(NULL, 0), ch->status_request,
                                    ch->cert_compression_algos.list, ch->cert_compression_algos.count)) != 0)
            goto Exit;
        /* send certificateverify, finished, and complete the handshake */
        if ((ret = server_finish_handshake(tls, emitter, 1, &ch->signature_algorithms)) != 0)
            goto Exit;
    } else {
        /* send finished, and complete the handshake */
        if ((ret = server_finish_handshake(tls, emitter, 0, NULL)) != 0)
            goto Exit;
    }

Exit:
    buffer_dispose(&ech.ch_inner);
    return ret;

#undef EMIT_SERVER_HELLO
#undef EMIT_HELLO_RETRY_REQUEST
}

static int server_finish_handshake(Ztls::Cxn *tls, message_emitter_t *emitter, int send_cert_verify,
                                   SignatureAlgorithms *signature_algorithms)
{
    int ret;

    if (send_cert_verify) {
        if ((ret = send_certificate_verify(tls, emitter, signature_algorithms, Ztls_SERVER_CERTIFICATE_VERIFY_CONTEXT_STRING)) !=
            0) {
            if (ret == Ztls_ERROR_ASYNC_OPERATION) {
                tls->state = Ztls_STATE_SERVER_GENERATING_CERTIFICATE_VERIFY;
            }
            goto Exit;
        }
    }

    if ((ret = send_finished(tls, emitter)) != 0)
        goto Exit;

    assert(tls->key_schedule->generation == 2);
    if ((ret = key_schedule_extract(tls->key_schedule, wire_bytes(NULL, 0))) != 0)
        goto Exit;
    if ((ret = setup_traffic_protection(tls, 1, "s ap traffic", 3, 0, 0)) != 0)
        goto Exit;
    if ((ret = derive_secret(tls->key_schedule, tls->pending_traffic_secret, "c ap traffic")) != 0)
        goto Exit;
    if ((ret = derive_exporter_secret(tls, 0)) != 0)
        goto Exit;

    if (tls->pending_handshake_secret.data() != NULL) {
        if (tls->ctx->omit_end_of_early_data) {
            if ((ret = commission_handshake_secret(tls)) != 0)
                goto Exit;
            tls->state = Ztls_STATE_SERVER_EXPECT_FINISHED;
        } else {
            tls->state = Ztls_STATE_SERVER_EXPECT_END_OF_EARLY_DATA;
        }
    } else if (tls->ctx->require_client_authentication) {
        tls->state = Ztls_STATE_SERVER_EXPECT_CERTIFICATE;
    } else {
        tls->state = Ztls_STATE_SERVER_EXPECT_FINISHED;
    }

    /* send session ticket if necessary */
    if (tls->num_tickets_to_send != 0) {
        assert(tls->ctx->ticket_lifetime != 0);
        for (uint8_t i = 0; i < tls->num_tickets_to_send; ++i)
            if ((ret = send_session_ticket(tls, emitter)) != 0)
                goto Exit;
    }

    if (tls->ctx->require_client_authentication) {
        ret = Ztls_ERROR_IN_PROGRESS;
    } else {
        ret = 0;
    }

Exit:
    return ret;
}

static int server_handle_end_of_early_data(Ztls::Cxn *tls, Bytes message)
{
    int ret;

    if ((ret = commission_handshake_secret(tls)) != 0)
        goto Exit;

    _key_schedule_update_hash(tls->key_schedule, message.data(), message.length(), 0);
    tls->state = Ztls_STATE_SERVER_EXPECT_FINISHED;
    ret = Ztls_ERROR_IN_PROGRESS;

Exit:
    return ret;
}

static int server_handle_finished(Ztls::Cxn *tls, Bytes message)
{
    int ret;

    if ((ret = verify_finished(tls, message)) != 0)
        return ret;

    memcpy(tls->rx_traffic.secret, tls->pending_traffic_secret, sizeof(tls->pending_traffic_secret));
    ZuClear(tls->pending_traffic_secret, sizeof(tls->pending_traffic_secret));
    if ((ret = setup_traffic_protection(tls, 0, NULL, 3, 0, 0)) != 0)
        return ret;

    _key_schedule_update_hash(tls->key_schedule, message.data(), message.length(), 0);

    tls->state = Ztls_STATE_SERVER_POST_HANDSHAKE;
    return 0;
}

static int update_traffic_key(Ztls::Cxn *tls, int is_enc)
{
    TrafficProtection *tp = is_enc ? &tls->tx_traffic : &tls->rx_traffic;
    uint8_t secret[Ztls_MAX_DIGEST_SIZE];
    int ret;

    hash_algorithm_t *hash = tls->cipher_suite->hash;
    if ((ret = hkdf_expand_label(hash, secret, hash->digest_size, wire_bytes(tp->secret, hash->digest_size),
                                      "traffic upd", wire_bytes(NULL, 0), NULL)) != 0)
        goto Exit;
    memcpy(tp->secret, secret, sizeof(secret));
    ret = setup_traffic_protection(tls, is_enc, NULL, 3, 0, 1);

Exit:
    ZuClear(secret, sizeof(secret));
    return ret;
}

static int handle_key_update(Ztls::Cxn *tls, message_emitter_t *emitter, Bytes message)
{
    const uint8_t *src = message.data() + Ztls_HANDSHAKE_HEADER_SIZE, *const end = message.data() + message.length();
    int ret;

    /* validate */
    if (end - src != 1 || *src > 1)
        return Ztls_ALERT_DECODE_ERROR;

    /* update receive key */
    if ((ret = update_traffic_key(tls, 0)) != 0)
        return ret;

    if (*src) {
        if (tls->ctx->update_traffic_key != NULL)
            return Ztls_ALERT_UNEXPECTED_MESSAGE;
        if (tls->tx_handed_off) {
            if (tls->key_update_requests == UINT32_MAX)
                return Ztls_ALERT_INTERNAL_ERROR;
            ++tls->key_update_requests;
        } else {
            tls->needs_key_update = 1;
        }
    }

    return 0;
}

static int parse_record_header(struct st_record_t *rec, const uint8_t *src)
{
    rec->type = src[0];
    rec->version = load_be<uint16_t>(src + 1);
    rec->length = load_be<uint16_t>(src + 3);

    if (rec->length >
        (size_t)(rec->type == Ztls_CONTENT_TYPE_APPDATA ? Ztls_MAX_ENCRYPTED_RECORD_SIZE : Ztls_MAX_PLAINTEXT_RECORD_SIZE))
        return Ztls_ALERT_DECODE_ERROR;

    return 0;
}

static int parse_record(Ztls::Cxn *tls, struct st_record_t *rec, const uint8_t *src, size_t *len)
{
    int ret;

    assert(*len != 0);

    /* Check if the first byte is something that we can handle, otherwise do not bother parsing / buffering the entire record as it
     * is obviously broken. SSL 2.0 handshakes fall into this path as well. */
    if (tls->recvbuf.rec.base == NULL) {
        uint8_t type = src[0];
        switch (type) {
        case Ztls_CONTENT_TYPE_CHANGE_CIPHER_SPEC:
        case Ztls_CONTENT_TYPE_ALERT:
        case Ztls_CONTENT_TYPE_HANDSHAKE:
        case Ztls_CONTENT_TYPE_APPDATA:
            break;
        default:
            return Ztls_ALERT_DECODE_ERROR;
        }
    }

    if (tls->recvbuf.rec.base == NULL && *len >= 5) {
        /* fast path */
        if ((ret = parse_record_header(rec, src)) != 0)
            return ret;
        if (5 + rec->length <= *len) {
            rec->fragment = src + 5;
            *len = rec->length + 5;
            return 0;
        }
    }

    /* slow path */
    const uint8_t *const end = src + *len;
    *rec = (struct st_record_t){0};

    if (tls->recvbuf.rec.base == NULL) {
        buffer_init_rx(&tls->recvbuf.rec, const_cast<char *>(""), 0);
        if ((ret = buffer_reserve(&tls->recvbuf.rec, 5, 0)) != 0)
            return ret;
    }

    /* fill and parse the header */
    while (tls->recvbuf.rec.off < 5) {
        if (src == end)
            return Ztls_ERROR_IN_PROGRESS;
        tls->recvbuf.rec.base[tls->recvbuf.rec.off++] = *src++;
    }
    if ((ret = parse_record_header(rec, tls->recvbuf.rec.base)) != 0)
        return ret;

    /* fill the fragment */
    size_t addlen = rec->length + 5 - tls->recvbuf.rec.off;
    if (addlen != 0) {
        if ((ret = buffer_reserve(&tls->recvbuf.rec, addlen, 0)) != 0)
            return ret;
        if (addlen > (size_t)(end - src))
            addlen = end - src;
        if (addlen != 0) {
            memcpy(tls->recvbuf.rec.base + tls->recvbuf.rec.off, src, addlen);
            tls->recvbuf.rec.off += addlen;
            src += addlen;
        }
    }

    /* set rec->fragment if a complete record has been parsed */
    if (tls->recvbuf.rec.off == rec->length + 5) {
        rec->fragment = tls->recvbuf.rec.base + 5;
        ret = 0;
    } else {
        ret = Ztls_ERROR_IN_PROGRESS;
    }

    *len -= end - src;
    return ret;
}

static void stream_discard(Ztls::Cxn *tls)
{
    StreamRecord *r = &tls->stream_rec;
    if (r->begun && tls->rx_traffic.aead != NULL &&
        tls->rx_traffic.aead->do_dec_abort != NULL)
        tls->rx_traffic.aead->do_dec_abort(tls->rx_traffic.aead);
    if (r->reserved)
        tls->plaintext.discard();
    ZuClear(r, sizeof(*r));
}

} // namespace Ztls_

namespace Ztls {

void Cxn::initialize(Ztls_::context_t *context, bool server, void *owner)
{
    using namespace Ztls_;
    retireRx();
    retireTx();
    ctx = context;
    is_server = server;
    data_ptr = owner;
    cipher_suite = nullptr;
    rx_active = true;
    is_psk_handshake = false;
    send_change_cipher_spec = ctx->send_change_cipher_spec;
    tx_handed_off = false;
    key_update_requests = 0;
    offered_psk = false;
    using_early_data = false;
    early_data_skipped_bytes = UINT32_MAX;
    num_tickets_to_send = 0;
    key_share = nullptr;
    ech = {};
    legacy_session_id = {};

    assert(ctx->get_time && "set the default or custom TLS clock");
    if (ctx->pre_shared_key.identity.data()) {
        assert(ctx->pre_shared_key.identity.length() && ctx->pre_shared_key.secret.data() &&
               ctx->pre_shared_key.secret.length() && ctx->pre_shared_key.hash);
    } else {
        assert(!ctx->pre_shared_key.identity.length() && !ctx->pre_shared_key.secret.data() &&
               !ctx->pre_shared_key.secret.length() && !ctx->pre_shared_key.hash);
    }
    if (server) {
        state = Ztls_STATE_SERVER_EXPECT_CLIENT_HELLO;
        return;
    }
    state = Ztls_STATE_CLIENT_HANDSHAKE_START;
    ctx->random_bytes(client_random, sizeof(client_random));
    if (send_change_cipher_spec) {
        legacy_session_id = wire_bytes(legacy_session_id_buf, sizeof(legacy_session_id_buf));
        ctx->random_bytes(legacy_session_id.data(), legacy_session_id.length());
    }
}

void Cxn::retireRx()
{
    using namespace Ztls_;
    if (!rx_active)
        return;
    stream_discard(this);
    plaintext.clear();
    rx_input = nullptr;
    rx_paused = false;
    buffer_dispose(&recvbuf.rec);
    buffer_dispose(&recvbuf.mess);
    if (key_schedule) {
        key_schedule_free(key_schedule);
        key_schedule = nullptr;
    }
    if (rx_traffic.aead)
        aead_free(rx_traffic.aead);
    ZuClear(&rx_traffic, sizeof(rx_traffic));
    clear_ech(&ech, is_server);
    retireSignature();
    if (key_share_ctx)
        key_share_ctx->on_exchange(&key_share_ctx, 1, nullptr, {});
    if (certificate_verify.cb)
        certificate_verify.cb(certificate_verify.verify_ctx, 0, {}, {});
    certificate_verify = {};
    certificate_request = {};
    certificate_request_bytes.null();
    pending_handshake_secret.null();
    exporter_master_secret.early.null();
    exporter_master_secret.one_rtt.null();
    server_name.null();
    negotiated_protocol.null();
    ZuClear(pending_traffic_secret, sizeof(pending_traffic_secret));
    ZuClear(client_random, sizeof(client_random));
    ZuClear(legacy_session_id_buf, sizeof(legacy_session_id_buf));
    handshake_props = {};
    rx_active = false;
}

void Cxn::retireSignature()
{
    auto retire = ZuMv(signature_retire);
    signature_resume = {};
    signature_fd = -1;
    if (retire)
        retire();
}

void Cxn::retireTx()
{
    if (tx_traffic.aead)
        Ztls_::aead_free(tx_traffic.aead);
    ZuClear(&tx_traffic, sizeof(tx_traffic));
    needs_key_update = false;
    key_update_send_request = false;
    tx_closing = false;
    tx_generation = 0;
}

Cxn::~Cxn()
{
    retireRx();
    retireTx();
}

} // namespace Ztls

namespace Ztls_ {

context_t *get_context(Ztls::Cxn *tls)
{
    return tls->ctx;
}

void set_context(Ztls::Cxn *tls, context_t *ctx)
{
    tls->ctx = ctx;
}

Bytes get_client_random(Ztls::Cxn *tls)
{
    return wire_bytes(tls->client_random, Ztls_HELLO_RANDOM_SIZE);
}

cipher_suite_t *get_cipher(Ztls::Cxn *tls)
{
    return tls->cipher_suite;
}

uint16_t get_protocol_version(Ztls::Cxn *tls)
{
    (void)tls;
    return Ztls_PROTOCOL_VERSION_TLS13;
}

int get_traffic_keys(Ztls::Cxn *tls, int is_enc, uint8_t *key, uint8_t *iv, uint64_t *seq)
{
    TrafficProtection *ctx = is_enc ? &tls->tx_traffic : &tls->rx_traffic;
    int ret;

    if (is_enc && tls->tx_handed_off)
        return Ztls_ERROR_NOT_AVAILABLE;

    if ((ret = get_traffic_keys(tls->cipher_suite->aead, tls->cipher_suite->hash, key, iv, ctx->secret, wire_bytes(NULL, 0),
                                NULL)) != 0)
        return ret;
    *seq = ctx->seq;
    return 0;
}

const char *get_server_name(Ztls::Cxn *tls)
{
    return tls->server_name.data();
}

int set_server_name(Ztls::Cxn *tls, const char *server_name, size_t server_name_len)
{
    assign_name(tls->server_name, server_name, server_name_len);
    return 0;
}

const char *get_negotiated_protocol(Ztls::Cxn *tls)
{
    return tls->negotiated_protocol.data();
}

int set_negotiated_protocol(Ztls::Cxn *tls, const char *protocol, size_t protocol_len)
{
    assign_name(tls->negotiated_protocol, protocol, protocol_len);
    return 0;
}

int handshake_is_complete(Ztls::Cxn *tls)
{
    return tls->state >= Ztls_STATE_POST_HANDSHAKE_MIN;
}

int is_psk_handshake(Ztls::Cxn *tls)
{
    return tls->is_psk_handshake;
}

int is_ech_handshake(Ztls::Cxn *tls, uint8_t *config_id, hpke_kem_t **kem, hpke_cipher_suite_t **cipher)
{
    if (tls->ech.state == Ztls_ECH_STATE_ACCEPTED) {
        if (config_id != NULL)
            *config_id = tls->ech.config_id;
        if (kem != NULL)
            *kem = tls->ech.kem;
        if (cipher != NULL)
            *cipher = tls->ech.cipher;
        return 1;
    }
    return 0;
}

void **get_data_ptr(Ztls::Cxn *tls)
{
    return &tls->data_ptr;
}

static int handle_client_handshake_message(Ztls::Cxn *tls, message_emitter_t *emitter, Bytes message, int is_end_of_record,
                                           handshake_properties_t *properties)
{
    uint8_t type = message.data()[0];
    int ret;

    switch (tls->state) {
    case Ztls_STATE_CLIENT_EXPECT_SERVER_HELLO:
    case Ztls_STATE_CLIENT_EXPECT_SECOND_SERVER_HELLO:
        if (type == Ztls_HANDSHAKE_TYPE_SERVER_HELLO && is_end_of_record) {
            ret = client_handle_hello(tls, emitter, message, properties);
        } else {
            ret = Ztls_ALERT_UNEXPECTED_MESSAGE;
        }
        break;
    case Ztls_STATE_CLIENT_EXPECT_ENCRYPTED_EXTENSIONS:
        if (type == Ztls_HANDSHAKE_TYPE_ENCRYPTED_EXTENSIONS) {
            ret = client_handle_encrypted_extensions(tls, message, properties);
        } else {
            ret = Ztls_ALERT_UNEXPECTED_MESSAGE;
        }
        break;
    case Ztls_STATE_CLIENT_EXPECT_CERTIFICATE_REQUEST_OR_CERTIFICATE:
        if (type == Ztls_HANDSHAKE_TYPE_CERTIFICATE_REQUEST) {
            ret = client_handle_certificate_request(tls, message, properties);
            break;
        }
    /* fall through */
    case Ztls_STATE_CLIENT_EXPECT_CERTIFICATE:
        switch (type) {
        case Ztls_HANDSHAKE_TYPE_CERTIFICATE:
            ret = client_handle_certificate(tls, message);
            break;
        case Ztls_HANDSHAKE_TYPE_COMPRESSED_CERTIFICATE:
            ret = client_handle_compressed_certificate(tls, message);
            break;
        default:
            ret = Ztls_ALERT_UNEXPECTED_MESSAGE;
            break;
        }
        break;
    case Ztls_STATE_CLIENT_EXPECT_CERTIFICATE_VERIFY:
        if (type == Ztls_HANDSHAKE_TYPE_CERTIFICATE_VERIFY) {
            ret = client_handle_certificate_verify(tls, message);
        } else {
            ret = Ztls_ALERT_UNEXPECTED_MESSAGE;
        }
        break;
    case Ztls_STATE_CLIENT_EXPECT_FINISHED:
        if (type == Ztls_HANDSHAKE_TYPE_FINISHED && is_end_of_record) {
            ret = client_handle_finished(tls, emitter, message);
        } else {
            ret = Ztls_ALERT_UNEXPECTED_MESSAGE;
        }
        break;
    case Ztls_STATE_CLIENT_POST_HANDSHAKE:
        switch (type) {
        case Ztls_HANDSHAKE_TYPE_NEW_SESSION_TICKET:
            ret = client_handle_new_session_ticket(tls, message);
            break;
        case Ztls_HANDSHAKE_TYPE_KEY_UPDATE:
            ret = handle_key_update(tls, emitter, message);
            break;
        default:
            ret = Ztls_ALERT_UNEXPECTED_MESSAGE;
            break;
        }
        break;
    default:
        assert(!"unexpected state");
        ret = Ztls_ALERT_INTERNAL_ERROR;
        break;
    }


    return ret;
}

static int handle_server_handshake_message(Ztls::Cxn *tls, message_emitter_t *emitter, Bytes message, int is_end_of_record,
                                           handshake_properties_t *properties)
{
    uint8_t type = message.data()[0];
    int ret;

    switch (tls->state) {
    case Ztls_STATE_SERVER_EXPECT_CLIENT_HELLO:
    case Ztls_STATE_SERVER_EXPECT_SECOND_CLIENT_HELLO:
        if (type == Ztls_HANDSHAKE_TYPE_CLIENT_HELLO && is_end_of_record) {
            ret = server_handle_hello(tls, emitter, message, properties);
        } else {
            ret = Ztls_ALERT_HANDSHAKE_FAILURE;
        }
        break;
    case Ztls_STATE_SERVER_EXPECT_CERTIFICATE:
        if (type == Ztls_HANDSHAKE_TYPE_CERTIFICATE) {
            ret = server_handle_certificate(tls, message);
        } else {
            ret = Ztls_ALERT_UNEXPECTED_MESSAGE;
        }
        break;
    case Ztls_STATE_SERVER_EXPECT_CERTIFICATE_VERIFY:
        if (type == Ztls_HANDSHAKE_TYPE_CERTIFICATE_VERIFY) {
            ret = server_handle_certificate_verify(tls, message);
        } else {
            ret = Ztls_ALERT_UNEXPECTED_MESSAGE;
        }
        break;
    case Ztls_STATE_SERVER_EXPECT_END_OF_EARLY_DATA:
        assert(!tls->ctx->omit_end_of_early_data);
        if (type == Ztls_HANDSHAKE_TYPE_END_OF_EARLY_DATA) {
            ret = server_handle_end_of_early_data(tls, message);
        } else {
            ret = Ztls_ALERT_UNEXPECTED_MESSAGE;
        }
        break;
    case Ztls_STATE_SERVER_EXPECT_FINISHED:
        if (type == Ztls_HANDSHAKE_TYPE_FINISHED && is_end_of_record) {
            ret = server_handle_finished(tls, message);
        } else {
            ret = Ztls_ALERT_HANDSHAKE_FAILURE;
        }
        break;
    case Ztls_STATE_SERVER_POST_HANDSHAKE:
        switch (type) {
        case Ztls_HANDSHAKE_TYPE_KEY_UPDATE:
            ret = handle_key_update(tls, emitter, message);
            break;
        default:
            ret = Ztls_ALERT_UNEXPECTED_MESSAGE;
            break;
        }
        break;
    default:
        assert(!"unexpected state");
        ret = Ztls_ALERT_INTERNAL_ERROR;
        break;
    }


    return ret;
}

static int handle_alert(Ztls::Cxn *tls, const uint8_t *src, size_t len)
{
    if (len != 2)
        return Ztls_ALERT_DECODE_ERROR;

    uint8_t desc = src[1];

    /* all fatal alerts and USER_CANCELLED warning tears down the connection immediately, regardless of the transmitted level */
    return Ztls_ALERT_TO_PEER_ERROR(desc);
}

static int message_buffer_is_overflow(context_t *ctx, size_t size)
{
    if (ctx->max_buffer_size == 0)
        return 0;
    if (size <= ctx->max_buffer_size)
        return 0;
    return 1;
}

static int handle_handshake_record(Ztls::Cxn *tls,
                                   int (*cb)(Ztls::Cxn *tls, message_emitter_t *emitter, Bytes message,
                                             int is_end_of_record, handshake_properties_t *properties),
                                   message_emitter_t *emitter, struct st_record_t *rec,
                                   handshake_properties_t *properties)
{
    int ret;

    /* handshake */
    if (rec->type != Ztls_CONTENT_TYPE_HANDSHAKE)
        return Ztls_ALERT_DECODE_ERROR;

    /* flatten the unhandled messages */
    const uint8_t *src, *src_end;
    if (tls->recvbuf.mess.base == NULL) {
        src = rec->fragment;
        src_end = src + rec->length;
    } else {
        if (message_buffer_is_overflow(tls->ctx, tls->recvbuf.mess.off + rec->length))
            return Ztls_ALERT_HANDSHAKE_FAILURE;
        if ((ret = buffer_reserve(&tls->recvbuf.mess, rec->length, 0)) != 0)
            return ret;
        memcpy(tls->recvbuf.mess.base + tls->recvbuf.mess.off, rec->fragment, rec->length);
        tls->recvbuf.mess.off += rec->length;
        src = tls->recvbuf.mess.base;
        src_end = src + tls->recvbuf.mess.off;
    }

    /* handle the messages */
    ret = Ztls_ERROR_IN_PROGRESS;
    while (src_end - src >= 4) {
        size_t mess_len = 4 + load_be24(src + 1);
        if (src_end - src < (int)mess_len)
            break;
        ret = cb(tls, emitter, wire_bytes(src, mess_len), src_end - src == mess_len, properties);
        switch (ret) {
        case 0:
        case Ztls_ERROR_ASYNC_OPERATION:
        case Ztls_ERROR_IN_PROGRESS:
            break;
        default:
            buffer_dispose(&tls->recvbuf.mess);
            return ret;
        }
        src += mess_len;
    }

    /* keep last partial message in buffer */
    if (src != src_end) {
        size_t new_size = src_end - src;
        if (message_buffer_is_overflow(tls->ctx, new_size))
            return Ztls_ALERT_HANDSHAKE_FAILURE;
        if (tls->recvbuf.mess.base == NULL) {
            buffer_init_rx(&tls->recvbuf.mess, const_cast<char *>(""), 0);
            if ((ret = buffer_reserve(&tls->recvbuf.mess, new_size, 0)) != 0)
                return ret;
            memcpy(tls->recvbuf.mess.base, src, new_size);
        } else {
            memmove(tls->recvbuf.mess.base, src, new_size);
        }
        tls->recvbuf.mess.off = new_size;
        ret = Ztls_ERROR_IN_PROGRESS;
    } else {
        buffer_dispose(&tls->recvbuf.mess);
    }

    return ret;
}

static int dispatch_record(Ztls::Cxn *tls, message_emitter_t *emitter, struct st_record_t *record,
                           handshake_properties_t *properties, int *application)
{
    struct st_record_t rec = *record;
    int ret;
    *application = 0;
    if (tls->recvbuf.mess.base != NULL || rec.type == Ztls_CONTENT_TYPE_HANDSHAKE) {
        /* handshake record */
        ret = handle_handshake_record(tls, tls->is_server ? handle_server_handshake_message : handle_client_handshake_message,
                                      emitter, &rec, properties);
    } else {
        /* handling of an alert or an application record */
        switch (rec.type) {
        case Ztls_CONTENT_TYPE_APPDATA:
            if (tls->state >= Ztls_STATE_POST_HANDSHAKE_MIN) {
                *application = 1;
                ret = 0;
            } else if (tls->state == Ztls_STATE_SERVER_EXPECT_END_OF_EARLY_DATA) {
                if (tls->rx_traffic.aead != NULL)
                    *application = 1;
                ret = 0;
            } else {
                ret = Ztls_ALERT_UNEXPECTED_MESSAGE;
            }
            break;
        case Ztls_CONTENT_TYPE_ALERT:
            ret = handle_alert(tls, rec.fragment, rec.length);
            break;
        default:
            ret = Ztls_ALERT_UNEXPECTED_MESSAGE;
            break;
        }
    }

    return ret;
}

static int handle_input(Ztls::Cxn *tls, message_emitter_t *emitter, buffer_t *decryptbuf, const void *input, size_t *inlen,
                        handshake_properties_t *properties)
{
    struct st_record_t rec;
    int ret;

    /* extract the record */
    if ((ret = parse_record(tls, &rec, static_cast<const uint8_t *>(input), inlen)) != 0)
        return ret;
    assert(rec.fragment != NULL);

    /* decrypt the record */
    if (rec.type == Ztls_CONTENT_TYPE_CHANGE_CIPHER_SPEC) {
        if (tls->state < Ztls_STATE_POST_HANDSHAKE_MIN) {
            if (!(rec.length == 1 && rec.fragment[0] == 0x01))
                return Ztls_ALERT_UNEXPECTED_MESSAGE;
        } else {
            return Ztls_ALERT_UNEXPECTED_MESSAGE;
        }
        ret = Ztls_ERROR_IN_PROGRESS;
        goto NextRecord;
    }
    if (tls->rx_traffic.aead != NULL && rec.type != Ztls_CONTENT_TYPE_ALERT) {
        size_t decrypted_length;
        if (rec.type != Ztls_CONTENT_TYPE_APPDATA)
            return Ztls_ALERT_HANDSHAKE_FAILURE;
        if ((ret = buffer_reserve(decryptbuf, rec.length, 0)) != 0)
            return ret;
        if ((ret = aead_decrypt(&tls->rx_traffic, decryptbuf->base + decryptbuf->off, &decrypted_length, rec.fragment,
                                rec.length)) != 0) {
            if (tls->is_server && tls->early_data_skipped_bytes != UINT32_MAX)
                goto ServerSkipEarlyData;
            return ret;
        }
        rec.length = decrypted_length;
        rec.fragment = decryptbuf->base + decryptbuf->off;
        /* skip padding */
        for (; rec.length != 0; --rec.length)
            if (rec.fragment[rec.length - 1] != 0)
                break;
        if (rec.length == 0)
            return Ztls_ALERT_UNEXPECTED_MESSAGE;
        rec.type = rec.fragment[--rec.length];
    } else if (rec.type == Ztls_CONTENT_TYPE_APPDATA && tls->is_server && tls->early_data_skipped_bytes != UINT32_MAX) {
        goto ServerSkipEarlyData;
    }

    int application;
    ret = dispatch_record(tls, emitter, &rec, properties, &application);
    if (application)
        decryptbuf->off += rec.length;

NextRecord:
    buffer_dispose(&tls->recvbuf.rec);
    return ret;

ServerSkipEarlyData:
    tls->early_data_skipped_bytes += (uint32_t)rec.length;
    if (tls->early_data_skipped_bytes > Ztls_MAX_EARLY_DATA_SKIP_SIZE)
        return Ztls_ALERT_HANDSHAKE_FAILURE;
    ret = Ztls_ERROR_IN_PROGRESS;
    goto NextRecord;
}

static void init_record_message_emitter(Ztls::Cxn *tls, struct st_record_message_emitter_t *emitter, buffer_t *sendbuf)
{
    *emitter = (struct st_record_message_emitter_t){
        {sendbuf, &tls->tx_traffic, 5, begin_record_message, commit_record_message}};
}

int handshake(Ztls::Cxn *tls, buffer_t *_sendbuf, const void *input, size_t *inlen, handshake_properties_t *properties)
{
    struct st_record_message_emitter_t emitter;
    int ret;

    assert(tls->state < Ztls_STATE_POST_HANDSHAKE_MIN);

    if (tls->tx_traffic.failed)
        return Ztls_ERROR_LIBRARY;
    if ((ret = buffer_require_direction(_sendbuf, 1)) != 0)
        return ret;
    init_record_message_emitter(tls, &emitter, _sendbuf);
    size_t sendbuf_orig_off = emitter.super.buf->off;

    /* special handlings */
    switch (tls->state) {
    case Ztls_STATE_CLIENT_HANDSHAKE_START: {
        assert(input == NULL || *inlen == 0);
        return send_client_hello(tls, &emitter.super, properties, NULL);
    }
    case Ztls_STATE_SERVER_GENERATING_CERTIFICATE_VERIFY:
        return server_finish_handshake(tls, &emitter.super, 1, NULL);
    default:
        break;
    }

    const uint8_t *src = static_cast<const uint8_t *>(input), *const src_end = src + *inlen;
    buffer_t decryptbuf;

    buffer_init_rx(&decryptbuf, const_cast<char *>(""), 0);

    /* perform handhake until completion or until all the input has been swallowed */
    ret = Ztls_ERROR_IN_PROGRESS;
    while (ret == Ztls_ERROR_IN_PROGRESS && src != src_end) {
        size_t consumed = src_end - src;
        ret = handle_input(tls, &emitter.super, &decryptbuf, src, &consumed, properties);
        src += consumed;
        assert(decryptbuf.off == 0);
    }

    buffer_dispose(&decryptbuf);

    switch (ret) {
    case 0:
    case Ztls_ERROR_IN_PROGRESS:
    case Ztls_ERROR_STATELESS_RETRY:
    case Ztls_ERROR_ASYNC_OPERATION:
        break;
    default:
        if (emitter.super.buf->record_sink != NULL &&
            (ret == Ztls_ERROR_NO_MEMORY || ret == Ztls_ERROR_LIBRARY || ret == Ztls_ERROR_NOT_AVAILABLE))
            break; /* Sink/backend errors are terminal; protocol errors still require their fatal alert. */
        /* Flush handshake messages that have been written partially. ECH_REQUIRED sticks out because it is a message sent
         * post-handshake compared to other alerts that are generating *during* the handshake. */
        if (ret != Ztls_ALERT_ECH_REQUIRED) {
            ZuClear(emitter.super.buf->base + sendbuf_orig_off, emitter.super.buf->off - sendbuf_orig_off);
            emitter.super.buf->off = sendbuf_orig_off;
        }
        /* send alert immediately */
        if (Ztls_ERROR_GET_CLASS(ret) != Ztls_ERROR_CLASS_PEER_ALERT)
            if (send_alert(tls, emitter.super.buf, Ztls_ALERT_LEVEL_FATAL,
                                Ztls_ERROR_GET_CLASS(ret) == Ztls_ERROR_CLASS_SELF_ALERT ? ret : Ztls_ALERT_INTERNAL_ERROR) != 0)
                emitter.super.buf->off = sendbuf_orig_off;
        break;
    }

    *inlen -= src_end - src;
    return ret;
}

int receive(Ztls::Cxn *tls, buffer_t *decryptbuf, const void *_input, size_t *inlen)
{
    const uint8_t *input = (const uint8_t *)_input, *const end = input + *inlen;
    size_t decryptbuf_orig_size = decryptbuf->off;
    int ret;

    assert(tls->state >= Ztls_STATE_SERVER_EXPECT_END_OF_EARLY_DATA);
    if ((ret = buffer_require_direction(decryptbuf, 0)) != 0)
        return ret;
    ret = 0;

    /* loop until we decrypt some application data (or an error) */
    while (ret == 0 && input != end && decryptbuf_orig_size == decryptbuf->off) {
        size_t consumed = end - input;
        ret = handle_input(tls, NULL, decryptbuf, input, &consumed, NULL);
        input += consumed;

        switch (ret) {
        case 0:
            break;
        case Ztls_ERROR_IN_PROGRESS:
            ret = 0;
            break;
        case Ztls_ERROR_CLASS_PEER_ALERT + Ztls_ALERT_CLOSE_NOTIFY:
            /* TODO send close alert */
            break;
        default:
            if (Ztls_ERROR_GET_CLASS(ret) == Ztls_ERROR_CLASS_SELF_ALERT) {
                /* TODO send alert */
            }
            break;
        }
    }

    *inlen -= end - input;

    return ret;
}

static int update_send_key(Ztls::Cxn *tls, buffer_t *_sendbuf, int request_update)
{
    struct st_record_message_emitter_t emitter;
    int ret;

    init_record_message_emitter(tls, &emitter, _sendbuf);
    size_t sendbuf_orig_off = emitter.super.buf->off;

    Ztls_push_message(&emitter.super, NULL, Ztls_HANDSHAKE_TYPE_KEY_UPDATE,
                      { Ztls_buffer_push(emitter.super.buf, !!request_update); });
    if ((ret = update_traffic_key(tls, 1)) != 0)
        goto Exit;
    ret = 0;

Exit:
    if (ret != 0)
        emitter.super.buf->off = sendbuf_orig_off;
    return ret;
}

int handoff_tx(Ztls::Cxn *tls)
{
    if (!tls || tls->tx_handed_off || tls->ctx->update_traffic_key ||
        (tls->state != Ztls_STATE_CLIENT_POST_HANDSHAKE && tls->state != Ztls_STATE_SERVER_POST_HANDSHAKE) ||
        !tls->tx_traffic.aead)
        return Ztls_ERROR_NOT_AVAILABLE;
    tls->tx_handed_off = true;
    return 0;
}

/* Descriptor capabilities are inspected without constructing dummy keyed contexts.
 * No cache: applications may replace suite lists before negotiating a connection. */
static int check_serial_suites(Ztls::Cxn *tls, int receive)
{
    if (tls->ctx->cipher_suites == NULL)
        return 0;
    for (cipher_suite_t **suite = tls->ctx->cipher_suites; *suite != NULL; ++suite) {
        aead_algorithm_t *algo = (*suite)->aead;
        if (receive ? !algo->serial_decrypt || algo->tag_size > 16 : !algo->serial_encrypt)
            return Ztls_ERROR_NOT_AVAILABLE;
    }
    return 0;
}

static int chunks_check(Ztls::Cxn *tls, Ztls::RecordSink *sink)
{
    if (tls == NULL)
        return Ztls_ERROR_NOT_AVAILABLE;
    if (tls->tx_traffic.failed || !sink)
        return Ztls_ERROR_LIBRARY;
    if (!record_serial_available(&tls->tx_traffic))
        return Ztls_ERROR_NOT_AVAILABLE;
    return check_serial_suites(tls, 0);
}

static int serial_receive_available(aead_context_t *aead)
{
    return aead != NULL && aead->do_dec_begin != NULL && aead->do_dec_chunk != NULL &&
           aead->do_dec_end != NULL && aead->do_dec_abort != NULL;
}

int receive_stream_check(Ztls::Cxn *tls)
{
    if (tls == NULL)
        return Ztls_ERROR_NOT_AVAILABLE;
    if (tls->rx_traffic.aead != NULL && !serial_receive_available(tls->rx_traffic.aead))
        return Ztls_ERROR_NOT_AVAILABLE;
    return check_serial_suites(tls, 1);
}

/* Copy only bounded parser carry, never encrypted record bodies. */
static size_t stream_carry(uint8_t *dest, size_t *off, size_t size, const uint8_t **src, size_t *available)
{
    size_t n = size - *off;
    if (n > *available)
        n = *available;
    if (n != 0) {
        memcpy(dest + *off, *src, n);
        *off += n;
        *src += n;
        *available -= n;
    }
    return *off;
}

int receive_stream(Ztls::Cxn *tls, Ztls::RecordSink *output,
                        const void *input, size_t *length, handshake_properties_t *properties)
{
    struct st_record_t rec;
    aead_context_t *aead;
    int ret, emit_fatal = 0;
    size_t available = length != NULL ? *length : 0, original = available;
    const uint8_t *src = static_cast<const uint8_t *>(input);
    if (length != NULL)
        *length = 0;
    if (tls == NULL)
        return Ztls_ERROR_LIBRARY;
    StreamRecord *r = &tls->stream_rec;
    if (r->failed)
        return Ztls_ERROR_LIBRARY;
    if (available != 0 && input == NULL) {
        ret = Ztls_ERROR_LIBRARY;
        goto Fail;
    }
    if ((ret = receive_stream_check(tls)) != 0)
        goto Fail;
    // Rx no longer inspects the writable Tx context after handing it off.
    if (!tls->tx_handed_off && (ret = chunks_check(tls, output)) != 0)
        goto Fail;
    if (tls->state == Ztls_STATE_CLIENT_HANDSHAKE_START || tls->state == Ztls_STATE_SERVER_GENERATING_CERTIFICATE_VERIFY) {
        size_t empty = 0;
        ret = handshake_chunks(tls, output, NULL, &empty, properties);
        if (ret == 0 || ret == Ztls_ERROR_IN_PROGRESS || ret == Ztls_ERROR_ASYNC_OPERATION)
            return ret;
        goto Fail; /* handshake_chunks already applied its alert policy. */
    }
    emit_fatal = 1;
    aead = tls->rx_traffic.aead;
    if (r->header_off != 5) {
        stream_carry(r->header, &r->header_off, 5, &src, &available);
        if (r->header_off != 5)
            goto Incomplete;
        switch (r->header[0]) {
        case Ztls_CONTENT_TYPE_CHANGE_CIPHER_SPEC:
        case Ztls_CONTENT_TYPE_ALERT:
        case Ztls_CONTENT_TYPE_HANDSHAKE:
        case Ztls_CONTENT_TYPE_APPDATA:
            break;
        default:
            ret = Ztls_ALERT_DECODE_ERROR;
            goto Fail;
        }
        if ((ret = parse_record_header(&rec, r->header)) != 0)
            goto Fail;
        r->length = rec.length;
        r->encrypted = aead != NULL && rec.type != Ztls_CONTENT_TYPE_ALERT && rec.type != Ztls_CONTENT_TYPE_CHANGE_CIPHER_SPEC;
        if (r->encrypted) {
            if (rec.type != Ztls_CONTENT_TYPE_APPDATA) {
                ret = Ztls_ALERT_HANDSHAKE_FAILURE;
                goto Fail;
            }
            r->tag_size = aead->algo->tag_size;
            if (r->tag_size > sizeof(r->tag) || r->length < r->tag_size) {
                ret = Ztls_ALERT_BAD_RECORD_MAC;
                goto Fail;
            }
        }
        r->text_length = r->length - r->tag_size;
        void *base = NULL;
        r->reserved = 1;
        ret = tls->plaintext.reserve(r->text_length, &base, &r->capacity);
        r->base = static_cast<uint8_t *>(base);
        if (ret != 0)
            goto Fail;
        if (base == NULL || r->capacity < r->text_length) {
            ret = Ztls_ERROR_NO_MEMORY;
            goto Fail;
        }
    }
    if (r->encrypted && !r->begun) {
        uint8_t aad[5];
        build_aad(aad, r->length);
        r->begun = 1;
        ret = aead->do_dec_begin(aead, tls->rx_traffic.seq, aad, sizeof(aad));
        ZuClear(aad, sizeof(aad));
        if (ret != 0)
            goto Fail;
    }
    if (r->written != r->text_length) {
        size_t n = r->text_length - r->written;
        if (n > available)
            n = available;
        if (n != 0) {
            size_t written = n;
            if (r->encrypted) {
                ret = aead->do_dec_chunk(aead, r->base + r->written, r->capacity - r->written, src, n, &written);
                /* A failed provider might have written provisional bytes; clear the entire reserved extent. */
                if (ret != 0 || written != n) {
                    r->written = r->text_length;
                    if (ret == 0)
                        ret = Ztls_ERROR_LIBRARY;
                    goto Fail;
                }
            } else {
                memcpy(r->base + r->written, src, n);
            }
            r->written += written;
            src += n;
            available -= n;
        }
        if (r->written != r->text_length)
            goto Incomplete;
    }
    if (r->tag_off != r->tag_size) {
        stream_carry(r->tag, &r->tag_off, r->tag_size, &src, &available);
        if (r->tag_off != r->tag_size)
            goto Incomplete;
    }
    rec = (struct st_record_t){r->header[0], load_be<uint16_t>(r->header + 1), r->text_length, r->base};
    if (r->encrypted) {
        ret = aead->do_dec_end(aead, r->tag, r->tag_size);
        if (ret != 0) {
            if (tls->is_server && tls->state < Ztls_STATE_POST_HANDSHAKE_MIN &&
                tls->early_data_skipped_bytes != UINT32_MAX && ret == Ztls_ALERT_BAD_RECORD_MAC) {
                r->begun = 0; /* End cleared per-record state; preserve keys while rejecting early data. */
                goto SkipEarly;
            }
            goto Fail;
        }
        r->begun = 0;
        ++tls->rx_traffic.seq;
        for (; rec.length != 0 && rec.fragment[rec.length - 1] == 0; --rec.length)
            ;
        if (rec.length == 0) {
            ret = Ztls_ALERT_UNEXPECTED_MESSAGE;
            goto Fail;
        }
        rec.type = rec.fragment[--rec.length];
    } else if (rec.type == Ztls_CONTENT_TYPE_APPDATA && tls->is_server && tls->state < Ztls_STATE_POST_HANDSHAKE_MIN &&
               tls->early_data_skipped_bytes != UINT32_MAX) {
        goto SkipEarly;
    }
    if (rec.length > Ztls_MAX_PLAINTEXT_RECORD_SIZE) {
        ret = Ztls_ALERT_RECORD_OVERFLOW;
        goto Fail;
    }
    if (rec.type == Ztls_CONTENT_TYPE_CHANGE_CIPHER_SPEC && !r->encrypted) {
        if (tls->state >= Ztls_STATE_POST_HANDSHAKE_MIN || rec.length != 1 || rec.fragment[0] != 1) {
            ret = Ztls_ALERT_UNEXPECTED_MESSAGE;
            goto Fail;
        }
        ret = 0;
    } else {
        buffer_t formatting;
        struct st_record_message_emitter_t emitter;
        buffer_init_tx(&formatting, const_cast<char *>(""), 0);
        formatting.record_sink = output;
        init_record_message_emitter(tls, &emitter, &formatting);
        int application = 0;
        ret = dispatch_record(tls, &emitter.super, &rec, properties, &application);
        buffer_dispose(&formatting);
        if (ret != 0 && ret != Ztls_ERROR_IN_PROGRESS && ret != Ztls_ERROR_ASYNC_OPERATION)
            goto Fail;
        if (application) {
            if ((ret = tls->plaintext.commit(rec.length)) != 0)
                goto Fail;
            r->base = NULL;
            r->reserved = 0;
        }
    }
    stream_discard(tls);
    if (length != NULL)
        *length = original - available;
    return ret == Ztls_ERROR_IN_PROGRESS ? 0 : ret;

SkipEarly:
    tls->early_data_skipped_bytes += (uint32_t)r->length;
    if (tls->early_data_skipped_bytes > Ztls_MAX_EARLY_DATA_SKIP_SIZE) {
        ret = Ztls_ALERT_HANDSHAKE_FAILURE;
        goto Fail;
    }
    stream_discard(tls);
    if (length != NULL)
        *length = original - available;
    return 0;
Incomplete:
    if (length != NULL)
        *length = original - available;
    return Ztls_ERROR_IN_PROGRESS;
Fail:
    /* Interpretation can fail after authentication or after dispatch replaced the receive key.
     * Abort the current provider once; discard must not require callback idempotence. */
    if (tls->rx_traffic.aead != NULL && tls->rx_traffic.aead->do_dec_abort != NULL)
        tls->rx_traffic.aead->do_dec_abort(tls->rx_traffic.aead);
    tls->stream_rec.begun = 0;
    stream_discard(tls);
    tls->stream_rec.failed = 1;
    /* Match handshake()'s fatal-alert behavior without publishing provisional input.
     * Post-handshake outbound keys may belong to a detached Tx owner. */
    if (emit_fatal && !tls->tx_handed_off && tls->state < Ztls_STATE_POST_HANDSHAKE_MIN &&
        Ztls_ERROR_GET_CLASS(ret) == Ztls_ERROR_CLASS_SELF_ALERT && output) {
        buffer_t formatting;
        buffer_init_tx(&formatting, const_cast<char *>(""), 0);
        formatting.record_sink = output;
        (void)send_alert(tls, &formatting, Ztls_ALERT_LEVEL_FATAL, (uint8_t)ret);
        buffer_dispose(&formatting);
    }
    if (length != NULL)
        *length = original - available;
    return ret;
}

int tx_send_chunks(Ztls::Cxn *tls, Ztls::RecordSink *sink, const void *input, size_t length)
{
    int ret;
    if (!tls || !tls->tx_handed_off)
        return Ztls_ERROR_NOT_AVAILABLE;
    if (length > Ztls_MAX_PLAINTEXT_RECORD_SIZE)
        return Ztls_ERROR_LIBRARY;
    TrafficProtection *enc = &tls->tx_traffic;
    if ((ret = chunks_check(tls, sink)) != 0)
        return ret;
    int update = tls->needs_key_update || enc->seq >= 16777216;
    size_t size = (length != 0 ? record_wire_size(enc, length) : 0) + (update ? record_wire_size(enc, 5) : 0);
    if (size == 0)
        return 0;
    record_writer writer{sink, size, 0};
    if ((ret = sink->reserve(size)) != 0) {
        sink->discard();
        return ret;
    }
    if (update) {
        const uint8_t message[5] = {Ztls_HANDSHAKE_TYPE_KEY_UPDATE, 0, 0, 1, !!tls->key_update_send_request};
        if ((ret = record_emit(&writer, enc, Ztls_CONTENT_TYPE_HANDSHAKE, message, sizeof(message))) != 0 ||
            (ret = update_traffic_key(tls, 1)) != 0)
            goto Fail;
        tls->needs_key_update = tls->key_update_send_request = 0;
    }
    if (length != 0 && (ret = record_emit(&writer, enc, Ztls_CONTENT_TYPE_APPDATA, input, length)) != 0)
        goto Fail;
    if (writer.remaining != 0) {
        ret = Ztls_ERROR_LIBRARY;
        goto Fail;
    }
    if ((ret = sink->finish()) == 0)
        return 0;
Fail:
    enc->failed = 1;
    sink->discard();
    return ret;
}

int tx_key_chunks(Ztls::Cxn *tls, Ztls::RecordSink *sink, int request_update)
{
    int ret;
    if (!tls || !tls->tx_handed_off)
        return Ztls_ERROR_NOT_AVAILABLE;
    if ((ret = chunks_check(tls, sink)) != 0)
        return ret;
    const uint8_t message[5] = {Ztls_HANDSHAKE_TYPE_KEY_UPDATE, 0, 0, 1, !!request_update};
    if ((ret = record_transaction(sink, &tls->tx_traffic, Ztls_CONTENT_TYPE_HANDSHAKE,
                                   message, sizeof(message))) != 0)
        return ret;
    ret = update_traffic_key(tls, 1);
    if (ret != 0)
        tls->tx_traffic.failed = 1;
    return ret;
}

int tx_alert_chunks(Ztls::Cxn *tls, Ztls::RecordSink *sink, uint8_t level, uint8_t description)
{
    int ret;
    if (!tls || !tls->tx_handed_off)
        return Ztls_ERROR_NOT_AVAILABLE;
    if ((ret = chunks_check(tls, sink)) != 0)
        return ret;
    const uint8_t message[2] = {level, description};
    return record_transaction(sink, &tls->tx_traffic, Ztls_CONTENT_TYPE_ALERT, message, sizeof(message));
}

int handshake_chunks(Ztls::Cxn *tls, Ztls::RecordSink *sink, const void *input, size_t *length,
                          handshake_properties_t *properties)
{
    int ret;
    if ((ret = chunks_check(tls, sink)) != 0)
        return ret;
    buffer_t formatting;
    buffer_init_tx(&formatting, const_cast<char *>(""), 0);
    formatting.record_sink = sink;
    ret = handshake(tls, &formatting, input, length, properties);
    if (ret != 0 && ret != Ztls_ERROR_IN_PROGRESS && ret != Ztls_ERROR_ASYNC_OPERATION)
        tls->tx_traffic.failed = 1;
    buffer_dispose(&formatting);
    return ret;
}

size_t tx_get_record_overhead(Ztls::Cxn *tls)
{
    return tls ? 6 + tls->tx_traffic.aead->algo->tag_size : 0;
}

int take_key_update_request(Ztls::Cxn *tls)
{
    if (tls == NULL || tls->key_update_requests == 0)
        return 0;
    --tls->key_update_requests;
    return 1;
}

int send(Ztls::Cxn *tls, buffer_t *sendbuf, const void *input, size_t inlen)
{
    int ret;

    if (tls->tx_handed_off)
        return Ztls_ERROR_NOT_AVAILABLE;
    if (tls->tx_traffic.failed)
        return Ztls_ERROR_LIBRARY;
    assert(tls->tx_traffic.aead != NULL);
    if ((ret = buffer_require_direction(sendbuf, 1)) != 0)
        return ret;

    /* "For AES-GCM, up to 2^24.5 full-size records (about 24 million) may be encrypted on a given connection while keeping a
     * safety margin of approximately 2^-57 for Authenticated Encryption (AE) security." (RFC 8446 section 5.5).
     *
     * Key updates do not happen with tls 1.2, check `key_schedule` to see if we are using tls/1.3
     */
    if (tls->tx_traffic.seq >= 16777216 && tls->key_schedule != NULL)
        tls->needs_key_update = 1;

    if (tls->needs_key_update) {
        int ret;
        if ((ret = update_send_key(tls, sendbuf, tls->key_update_send_request)) != 0)
            return ret;
        tls->needs_key_update = 0;
        tls->key_update_send_request = 0;
    }

    return buffer_push_encrypted_records(sendbuf, Ztls_CONTENT_TYPE_APPDATA, static_cast<const uint8_t *>(input), inlen,
                                         &tls->tx_traffic);
}

int update_key(Ztls::Cxn *tls, int request_update)
{
    if (tls->tx_handed_off)
        return Ztls_ERROR_NOT_AVAILABLE;
    if (tls->tx_traffic.failed)
        return Ztls_ERROR_LIBRARY;
    assert(tls->ctx->update_traffic_key == NULL);
    tls->needs_key_update = 1;
    tls->key_update_send_request = request_update;
    return 0;
}

size_t get_record_overhead(Ztls::Cxn *tls)
{
    if (tls->tx_handed_off)
        return 0;
    aead_algorithm_t *algo = tls->tx_traffic.aead->algo;

    return 6 + algo->tag_size;
}

int send_alert(Ztls::Cxn *tls, buffer_t *sendbuf, uint8_t level, uint8_t description)
{
    int ret;
    if (tls->tx_handed_off)
        return Ztls_ERROR_NOT_AVAILABLE;
    if (tls->tx_traffic.failed)
        return Ztls_ERROR_LIBRARY;
    if ((ret = buffer_require_direction(sendbuf, 1)) != 0)
        return ret;
    const uint8_t message[2] = {level, description};
    TrafficProtection clear = {}, *enc = &tls->tx_traffic;
    if (enc->aead == NULL || tls->state <= Ztls_STATE_CLIENT_EXPECT_FINISHED)
        enc = &clear;
    return buffer_record(sendbuf, enc, Ztls_CONTENT_TYPE_ALERT, message, sizeof(message));
}

int export_secret(Ztls::Cxn *tls, void *output, size_t outlen, const char *label, Bytes context_value, int is_early)
{
    hash_algorithm_t *algo = tls->key_schedule->hashes[0].algo;
    auto &exporter = is_early ? tls->exporter_master_secret.early : tls->exporter_master_secret.one_rtt;
    uint8_t *master_secret = exporter.data(),
            derived_secret[Ztls_MAX_DIGEST_SIZE], context_value_hash[Ztls_MAX_DIGEST_SIZE];
    int ret;

    if (master_secret == NULL) {
        if (is_early) {
            switch (tls->state) {
            case Ztls_STATE_CLIENT_HANDSHAKE_START:
            case Ztls_STATE_SERVER_EXPECT_CLIENT_HELLO:
                ret = Ztls_ERROR_IN_PROGRESS;
                break;
            default:
                ret = Ztls_ERROR_NOT_AVAILABLE;
                break;
            }
        } else {
            ret = Ztls_ERROR_IN_PROGRESS;
        }
        return ret;
    }

    if ((ret = calc_hash(algo, context_value_hash, context_value.data(), context_value.length())) != 0)
        return ret;

    if ((ret = hkdf_expand_label(algo, derived_secret, algo->digest_size, wire_bytes(master_secret, algo->digest_size),
                                      label, wire_bytes(algo->empty_digest, algo->digest_size), NULL)) != 0)
        goto Exit;
    ret = hkdf_expand_label(algo, output, outlen, wire_bytes(derived_secret, algo->digest_size), "exporter",
                                 wire_bytes(context_value_hash, algo->digest_size), NULL);

Exit:
    ZuClear(derived_secret, sizeof(derived_secret));
    ZuClear(context_value_hash, sizeof(context_value_hash));
    return ret;
}

ZuDerive(HmacKey, (ZtBArray<ZtArraySecret<true, ZtArrayHeapID<"Ztls.Engine.HMAC.Key">>>));

struct st_hmac_context_t {
    hash_context_t super;
    hash_algorithm_t *algo;
    hash_context_t *hash;
    HmacKey key;
};
using HmacHeap = ZmVHeap<"Ztls.Engine.HMAC", 64, 4096, alignof(st_hmac_context_t)>;

static void hmac_update(hash_context_t *_ctx, const void *src, size_t len)
{
    struct st_hmac_context_t *ctx = (struct st_hmac_context_t *)_ctx;
    ctx->hash->update(ctx->hash, src, len);
}

static void hmac_apply_key(struct st_hmac_context_t *ctx, uint8_t pad)
{
    size_t i;

    for (i = 0; i != ctx->algo->block_size; ++i)
        ctx->key[i] ^= pad;
    ctx->hash->update(ctx->hash, ctx->key.data(), ctx->algo->block_size);
    for (i = 0; i != ctx->algo->block_size; ++i)
        ctx->key[i] ^= pad;
}

static void hmac_final(hash_context_t *_ctx, void *md, hash_final_mode_t mode)
{
    struct st_hmac_context_t *ctx = (struct st_hmac_context_t *)_ctx;

    assert(mode != HASH_FINAL_MODE_SNAPSHOT || !"not supported");

    if (md != NULL) {
        ctx->hash->final(ctx->hash, md, HASH_FINAL_MODE_RESET);
        hmac_apply_key(ctx, 0x5c);
        ctx->hash->update(ctx->hash, md, ctx->algo->digest_size);
    }
    ctx->hash->final(ctx->hash, md, mode);

    switch (mode) {
    case HASH_FINAL_MODE_FREE:
        ctx->~st_hmac_context_t();
        HmacHeap::free(ctx, sizeof(st_hmac_context_t));
        break;
    case HASH_FINAL_MODE_RESET:
        hmac_apply_key(ctx, 0x36);
        break;
    default:
        assert(!"FIXME");
        break;
    }
}

int calc_hash(hash_algorithm_t *algo, void *output, const void *src, size_t len)
{
    hash_context_t *ctx;

    if ((ctx = algo->create()) == NULL)
        return Ztls_ERROR_NO_MEMORY;
    ctx->update(ctx, src, len);
    ctx->final(ctx, output, HASH_FINAL_MODE_FREE);
    return 0;
}

hash_context_t *hmac_create(hash_algorithm_t *algo, const void *key, size_t key_size)
{
    assert(key_size <= algo->block_size);

    hash_context_t *hash = algo->create();
    if (!hash)
        return NULL;
    auto ctx = new (HmacHeap::alloc(sizeof(st_hmac_context_t)))
        st_hmac_context_t{{hmac_update, hmac_final}, algo, hash};
    ctx->key.length(algo->block_size);
    memset(ctx->key.data(), 0, algo->block_size);
    if (key_size)
        memcpy(ctx->key.data(), key, key_size);

    hmac_apply_key(ctx, 0x36);

    return &ctx->super;
}

int hkdf_extract(hash_algorithm_t *algo, void *output, Bytes salt, Bytes ikm)
{
    hash_context_t *hash;

    if (salt.length() == 0)
        salt = wire_bytes(zeroes_of_max_digest_size, algo->digest_size);

    if ((hash = hmac_create(algo, salt.data(), salt.length())) == NULL)
        return Ztls_ERROR_NO_MEMORY;
    hash->update(hash, ikm.data(), ikm.length());
    hash->final(hash, output, HASH_FINAL_MODE_FREE);
    return 0;
}

int hkdf_expand(hash_algorithm_t *algo, void *output, size_t outlen, Bytes prk, Bytes info)
{
    hash_context_t *hmac = NULL;
    size_t i;
    uint8_t digest[Ztls_MAX_DIGEST_SIZE];

    for (i = 0; (i * algo->digest_size) < outlen; ++i) {
        if (hmac == NULL) {
            if ((hmac = hmac_create(algo, prk.data(), prk.length())) == NULL)
                return Ztls_ERROR_NO_MEMORY;
        } else {
            hmac->update(hmac, digest, algo->digest_size);
        }
        hmac->update(hmac, info.data(), info.length());
        uint8_t gen = (uint8_t)(i + 1);
        hmac->update(hmac, &gen, 1);
        hmac->final(hmac, digest, HASH_FINAL_MODE_RESET);

        size_t off_start = i * algo->digest_size, off_end = off_start + algo->digest_size;
        if (off_end > outlen)
            off_end = outlen;
        memcpy((uint8_t *)output + off_start, digest, off_end - off_start);
    }

    if (hmac != NULL)
        hmac->final(hmac, NULL, HASH_FINAL_MODE_FREE);

    ZuClear(digest, algo->digest_size);

    return 0;
}

int hkdf_expand_label(hash_algorithm_t *algo, void *output, size_t outlen, Bytes secret, const char *label,
                           Bytes hash_value, const char *label_prefix)
{
    buffer_t hkdf_label;
    uint8_t hkdf_label_buf[80];
    int ret;

    buffer_init_tx(&hkdf_label, hkdf_label_buf, sizeof(hkdf_label_buf));

    Ztls_buffer_push16(&hkdf_label, (uint16_t)outlen);
    Ztls_buffer_push_block(&hkdf_label, 1, {
        if (label_prefix == NULL)
            label_prefix = Ztls_HKDF_EXPAND_LABEL_PREFIX;
        Ztls_buffer_pushv(&hkdf_label, label_prefix, strlen(label_prefix));
        Ztls_buffer_pushv(&hkdf_label, label, strlen(label));
    });
    Ztls_buffer_push_block(&hkdf_label, 1, { Ztls_buffer_pushv(&hkdf_label, hash_value.data(), hash_value.length()); });

    ret = hkdf_expand(algo, output, outlen, secret, wire_bytes(hkdf_label.base, hkdf_label.off));

Exit:
    buffer_dispose(&hkdf_label);
    return ret;
}

// The widest retained context contains Fusion AVX key state.
using CipherHeap = ZmVHeap<"Ztls.Engine.Cipher", 64, 4096, 32>;
using AeadHeap = ZmVHeap<"Ztls.Engine.AEAD", 64, 4096, 32>;

cipher_context_t *cipher_new(cipher_algorithm_t *algo, int is_enc, const void *key)
{
    auto ctx = static_cast<cipher_context_t *>(CipherHeap::alloc(algo->context_size));
    *ctx = (cipher_context_t){algo};
    if (algo->setup_crypto(ctx, is_enc, key) != 0) {
        ZuClear(ctx, algo->context_size);
        CipherHeap::free(ctx, algo->context_size);
        ctx = NULL;
    }
    return ctx;
}

void cipher_free(cipher_context_t *ctx)
{
    size_t size = ctx->algo->context_size;
    ctx->do_dispose(ctx);
    ZuClear(ctx, size);
    CipherHeap::free(ctx, size);
}

aead_context_t *new_aead(aead_algorithm_t *aead, hash_algorithm_t *hash, int is_enc, const void *secret,
                              Bytes hash_value, const char *label_prefix)
{
    aead_context_t *ctx = NULL;
    struct {
        uint8_t key[Ztls_MAX_SECRET_SIZE];
        uint8_t iv[Ztls_MAX_IV_SIZE];
    } key_iv;
    int ret;

    if ((ret = get_traffic_keys(aead, hash, key_iv.key, key_iv.iv, secret, hash_value, label_prefix)) != 0)
        goto Exit;
    ctx = aead_new_direct(aead, is_enc, key_iv.key, key_iv.iv);
Exit:
    ZuClear(&key_iv, sizeof(key_iv));
    return ctx;
}

aead_context_t *aead_new(aead_algorithm_t *aead, hash_algorithm_t *hash, int is_enc, const void *secret,
                                   const char *label_prefix)
{
    return new_aead(aead, hash, is_enc, secret, wire_bytes(NULL, 0), label_prefix);
}

aead_context_t *aead_new_direct(aead_algorithm_t *aead, int is_enc, const void *key, const void *iv)
{
    auto ctx = static_cast<aead_context_t *>(AeadHeap::alloc(aead->context_size));

    *ctx = (aead_context_t){aead};

    if (aead->setup_crypto(ctx, is_enc, key, iv) != 0) {
        ZuClear(ctx, aead->context_size);
        AeadHeap::free(ctx, aead->context_size);
        return NULL;
    }

    return ctx;
}

void aead_free(aead_context_t *ctx)
{
    size_t size = ctx->algo->context_size;
    ctx->dispose_crypto(ctx);
    ZuClear(ctx, size);
    AeadHeap::free(ctx, size);
}

void aead_xor_iv(aead_context_t *ctx, const void *_bytes, size_t len)
{
    const uint8_t *bytes = static_cast<const uint8_t *>(_bytes);
    uint8_t iv[Ztls_MAX_IV_SIZE];

    aead_get_iv(ctx, iv);
    for (size_t i = 0; i < len; ++i)
        iv[i] ^= bytes[i];
    aead_set_iv(ctx, iv);
}

void aead__build_iv(aead_algorithm_t *algo, uint8_t *iv, const uint8_t *static_iv, uint64_t seq)
{
    size_t iv_size = algo->iv_size, i;
    const uint8_t *s = static_iv;
    uint8_t *d = iv;

    /* build iv */
    for (i = iv_size - 8; i != 0; --i)
        *d++ = *s++;
    i = 64;
    do {
        i -= 8;
        *d++ = *s++ ^ (uint8_t)(seq >> i);
    } while (i != 0);
}

static uint64_t get_time_default(get_time_t *self)
{
    return Zm::now().millisecs();
}

get_time_t get_time = {get_time_default};

int is_server(Ztls::Cxn *tls)
{
    return tls->is_server;
}

/**
 * checks if given name looks like an IP address
 */
int server_name_is_ipaddr(const char *name)
{
    ZiIP address;
    return ZiIP::parse(address, name);
}

int ech_encode_config(buffer_t *buf, uint8_t config_id, hpke_kem_t *kem, Bytes public_key,
                           hpke_cipher_suite_t **ciphers, uint8_t max_name_length, const char *public_name)
{
    int ret;
    if ((ret = buffer_require_direction(buf, 1)) != 0)
        return ret;

    Ztls_buffer_push16(buf, Ztls_ECH_CONFIG_VERSION);
    Ztls_buffer_push_block(buf, 2, {
        Ztls_buffer_push(buf, config_id);
        Ztls_buffer_push16(buf, kem->id);
        Ztls_buffer_push_block(buf, 2, { Ztls_buffer_pushv(buf, public_key.data(), public_key.length()); });
        Ztls_buffer_push_block(buf, 2, {
            for (size_t i = 0; ciphers[i] != NULL; ++i) {
                Ztls_buffer_push16(buf, ciphers[i]->id.kdf);
                Ztls_buffer_push16(buf, ciphers[i]->id.aead);
            }
        });
        Ztls_buffer_push(buf, max_name_length);
        Ztls_buffer_push_block(buf, 1, { Ztls_buffer_pushv(buf, public_name, strlen(public_name)); });
        Ztls_buffer_push_block(buf, 2, {/* extensions */});
    });

Exit:
    return ret;
}

} // Ztls_
