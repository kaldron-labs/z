//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

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
#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <zlib/ZuAssert.hh>
#include <zlib/ZuDerive.hh>
#include <zlib/ZuTraits.hh>

#include <zlib/ZmTime.hh>
#include <zlib/ZmVHeap.hh>

#include <zlib/ZtArray.hh>
#include <zlib/ZtScratch.hh>

#include <zlib/ZiIOBuf.hh>
#include <zlib/ZiIP.hh>

#include <zlib/ZtlsCxn.hh>
#include <zlib/ZtlsSec.hh>

namespace Ztls_ {

#define Ztls_MaxPlainRecordSize Ztls_RxAppCapacity
#define Ztls_MaxEncRecordSize Ztls_RxRecordCapacity

#define Ztls_RecordVerMajor 3
#define Ztls_RecordVerMinor 3

#define Ztls_ContentChangeCipher 20
#define Ztls_ContentTypeAlert 21
#define Ztls_ContentTypeHS 22
#define Ztls_ContentTypeAppdata 23

#define Ztls_PSKKeModePSK 0
#define Ztls_PSKKeModePSKDHE 1

#define Ztls_HSHeaderSize 4

#define Ztls_ExtTypeSrvName 0
#define Ztls_ExtTypeStatusRequest 5
#define Ztls_ExtGroups 10
#define Ztls_ExtTypeSigAlgos 13
#define Ztls_ExtTypeALPN 16
#define Ztls_ExtTypeSrvCertType 20
#define Ztls_ExtTypeCompressCert 27
#define Ztls_ExtTypePreSharedKey 41
#define Ztls_ExtTypeEarly 42
#define Ztls_ExtVersions 43
#define Ztls_ExtTypeCookie 44
#define Ztls_ExtPSKKexModes 45
#define Ztls_ExtCertAuths 47
#define Ztls_ExtTypeKeyShare 51
#define Ztls_ExtTypeTicketRequest 58
#define Ztls_ExtTypeECHOuterExts 0xfd00
#define Ztls_ExtTypeEncCliHello 0xfe0d

#define Ztls_SrvNameTypeHostname 0

#define Ztls_ECHConfigVer 0xfe0d
#define Ztls_ECHCliHelloOuter 0
#define Ztls_ECHCliHelloInner 1

#define Ztls_ECHConfirmLength 8

static const char ech_info_prefix[8] = "tls ech";

// max. RTT (in msec) to permit early data
#ifndef ZtlsEarlyDataMaxDelay
#define ZtlsEarlyDataMaxDelay 10000
#endif

#ifndef ZtlsMaxEarlySkipSize
#define ZtlsMaxEarlySkipSize 65536
#endif

// list of supported versions in the preferred order
static const uint16_t supported_versions[] = {Ztls_ProtoVerTls13};

static const uint8_t hello_retry_random[Ztls_HelloRandomSize] = {0xCF, 0x21, 0xAD, 0x74, 0xE5,
  0x9A, 0x61, 0x11, 0xBE, 0x1D, 0x8C, 0x02, 0x1E, 0x65, 0xB8, 0x91, 0xC2, 0xA2, 0x11, 0x16, 0x7A,
  0xBB, 0x8C, 0x5E, 0x07, 0x9E, 0x09, 0xE2, 0xC8, 0xA8, 0x33, 0x9C};

struct RecordMessageEmitter : MessageEmitter {
  size_t	recStart = 0;

  bool recordLayer() const override { return true; }
  int beginMessage() override;
  int commitMessage() override;
};

struct DecodedEchConfig {
  uint8_t		id;
  const HpkeKem		*kem;
  Bytes			publicKey;
  const HpkeSuite	*cipher;
  uint8_t		maxNameLength;
  Bytes			publicName;
  Bytes			bytes;
};

struct Record {
  uint8_t	type;
  uint16_t	version;
  size_t	length;
  const uint8_t	*fragment;
};

#ifndef ZtlsMaxUnknownExts
#define ZtlsMaxUnknownExts 16
#endif
#ifndef ZtlsMaxCertTypes
#define ZtlsMaxCertTypes 8
#endif

ZuDerive(
  PeerCertificates,
  (ZtBuiltin<
    ZtArray<Bytes, ZtArrayHeapID<"Ztls.Engine.PeerCertificates">>, ZtlsCertChainBuiltin>));
ZuDerive(
  AlpnOffers,
  (ZtBuiltin<ZtArray<Bytes, ZtArrayHeapID<"Ztls.Engine.AlpnOffers">>, ZtlsALPNBuiltin>));
ZuDerive(
  CompressionOffers,
  (ZtBuiltin<
    ZtArray<uint16_t, ZtArrayHeapID<"Ztls.Engine.CompressionOffers">>, ZtlsCertCompBuiltin>));

struct ClientHello {
  uint16_t		legacyVersion;
  const uint8_t		*randomBytes;
  Bytes			legacySessionId;
  struct CompressionMethods {
    const uint8_t	*ids;
    size_t		count;
  } compressionMethods;
  uint16_t		selectedVersion;
  Bytes			cipherSuites;
  Bytes			negotiatedGroups;
  Bytes			keyShares;
  SignatureAlgorithms	signatureAlgorithms;
  Bytes			serverName;
  AlpnOffers		alpn;
  CompressionOffers	certCompressionAlgos;
  struct Cookie {
    Bytes	all;
    Bytes	tbs;
    Bytes	ch1Hash;
    Bytes	signature;
    unsigned	sentKeyShare:1;
  } cookie;
  struct ServerCertificateTypes {
    uint8_t	list[ZtlsMaxCertTypes];
    size_t	count;
  } serverCertificateTypes;
  unsigned		statusRequest:1;
  struct TicketRequest {
    uint8_t	newSessionCount;
    uint8_t	resumptionCount;
  } ticketRequest;

  // ECH: payload.data() != nullptr indicates that the extension was received
  struct Ech {
    uint8_t	type;
    uint8_t	configId;
    HpkeSuiteID	cipherSuite;
    Bytes	enc;
    Bytes	payload;
  } ech;
  struct Psk {
    const uint8_t	*hashEnd;
    struct Identities {
      CliHelloPskIdentity	list[4];
      size_t			count;
    } identities;
    unsigned		keModes;
    unsigned		earlyDataIndication:1;
    unsigned		isLastExtension:1;
  } psk;
  RawExtension		*unknownExtensions;
  size_t		firstExtensionAt;
};

struct ServerHello {
  uint8_t	random[Ztls_HelloRandomSize];
  Bytes		legacySessionId;
  int		isRetryRequest;
  Bytes		peerkey;
  struct RetryRequest {
    uint16_t		selectedGroup;
    Bytes		cookie;
    const uint8_t	*ech;
  } retryRequest;
};

struct ScheduleHash {
  const HashAlgo	*algo;
  HashContext		*ctx;
  HashContext		*ctxOuter;
};
ZuDerive(ScheduleHashes, (ZtArray<ScheduleHash, ZtArrayHeapID<"Ztls.Engine.ScheduleHashes">>));

struct KeySchedule : ZmNHeap<KeySchedule> {
friend inline ZuStringT<"Ztls.Engine.Schedule"> ZmNHeapID(KeySchedule *);
  unsigned		generation;
  // early secret (1), handshake secret (2), master secret (3)
  uint8_t		secret[Ztls_MaxDigestSize];
  size_t		numHashes;
  ScheduleHashes	hashes;
};

ZuDerive(
  CollectedExtensions, (ZtArray<RawExtension, ZtArrayHeapID<"Ztls.Engine.CollectedExtensions">>));
ZuDerive(
  DecodedClientHello, (ZtArray<ClientHello, ZtArrayHeapID<"Ztls.Engine.DecodedClientHello">>));

struct ExtensionBitmap {
  uint64_t	bits;
};

static const uint8_t zeroes_of_max_digest_size[Ztls_MaxDigestSize] = {0};

static AeadContext *new_aead(
  const AeadAlgo *aead, const HashAlgo *hash, int is_enc, const void *secret, Bytes hash_value,
  const char *label_prefix);
static int server_finish_handshake(
  Ztls::Cxn *tls, MessageEmitter *emitter, int send_cert_verify,
  SignatureAlgorithms *signatureAlgorithms);

static int is_supported_version(uint16_t v)
{
  size_t i;
  for (i = 0; i != ZuTraits<decltype(supported_versions)>::length(supported_versions); ++i)
    if (supported_versions[i] == v) return 1;
  return 0;
}

static int extension_bitmap_testandset(ExtensionBitmap *bitmap, int hstype, uint16_t extid)
{
//  min(hstype) is -1 (PSEUDO_HRR)
#define Ztls_HSTypeToBit(hstype) ((uint64_t)1 << ((hstype) + 1))
#define Ztls_DefineBit(abbrev, hstype) static const uint64_t abbrev = Ztls_HSTypeToBit(hstype)
#define Ztls_Ext(candext, allowed_bits) \
  do { \
    if (ZuUnlikely(extid == candext)) { \
      allowed_hs_bits = allowed_bits; \
      goto Found; \
    } \
    ext_bitmap_mask <<= 1; \
  } while (0)

  Ztls_DefineBit(CH, Ztls_HsTypeCliHello);
  Ztls_DefineBit(SH, Ztls_HsTypeSrvHello);
  Ztls_DefineBit(HRR, Ztls_HsTypePseudoHRR);
  Ztls_DefineBit(EE, Ztls_HsTypeEncExts);
  Ztls_DefineBit(CR, Ztls_HsTypeCertRequest);
  Ztls_DefineBit(CT, Ztls_HsTypeCert);
  Ztls_DefineBit(NST, Ztls_HsTypeNewTicket);

  uint64_t allowed_hs_bits, ext_bitmap_mask = 1;

  //  clang-format off
  // RFC 8446 section 4.2: "The table below indicates the messages where a given extension may
  // appear... If an implementation
  // receives an extension which it recognizes and which is not specified for the message in which
  // it appears, it MUST abort the
  // handshake with an "illegal_parameter" alert.

  // +-------------------------+---------------+
  // +        Extension        |    Allowed    |
  // +-------------------------+---------------+
  Ztls_Ext(Ztls_ExtTypeSrvName, CH + EE);
  Ztls_Ext(Ztls_ExtTypeStatusRequest, CH + CR + CT);
  Ztls_Ext(Ztls_ExtGroups, CH + EE);
  Ztls_Ext(Ztls_ExtTypeSigAlgos, CH + CR);
  Ztls_Ext(Ztls_ExtTypeALPN, CH + EE);
  Ztls_Ext(Ztls_ExtTypeSrvCertType, CH + EE);
  Ztls_Ext(Ztls_ExtTypeKeyShare, CH + SH + HRR);
  Ztls_Ext(Ztls_ExtTypePreSharedKey, CH + SH);
  Ztls_Ext(Ztls_ExtPSKKexModes, CH);
  Ztls_Ext(Ztls_ExtTypeEarly, CH + EE + NST);
  Ztls_Ext(Ztls_ExtTypeCookie, CH + HRR);
  Ztls_Ext(Ztls_ExtVersions, CH + SH + HRR);
  Ztls_Ext(Ztls_ExtTypeCompressCert, CH + CR);
  // from RFC 8879
  Ztls_Ext(Ztls_ExtTypeEncCliHello, CH + HRR + EE);
  // from draft-ietf-tls-esni-15
  Ztls_Ext(Ztls_ExtTypeECHOuterExts, 0);
  //  +-----------------------------------------+
  //  clang-format on

  return 1;

Found:
  if ((allowed_hs_bits & Ztls_HSTypeToBit(hstype)) == 0) return 0;
  if ((bitmap->bits & ext_bitmap_mask) != 0) return 0;
  bitmap->bits |= ext_bitmap_mask;
  return 1;

#undef Ztls_HSTypeToBit
#undef Ztls_DefineBit
#undef Ztls_Ext
}

template <typename T> static T load_be(const uint8_t *src)
{
  T data;
  memcpy(&data, src, sizeof(data));
  return ZuBE(data);
}

static uint32_t load_be24(const uint8_t *src)
{ return (uint32_t(src[0]) << 16) | load_be<uint16_t>(src + 1); }

template <typename Array> static void assign_name(Array &dst, ZuCSpan src)
{
  if (!src.data()) {
    dst.null();
    return;
  }
  Array name(src.length() + 1, src.length() + 1);
  memcpy(name.data(), src.data(), src.length());
  name[src.length()] = '\0';
  dst = ZuMv(name);
}

static int buffer_require_direction(Buffer *buf, int tx)
{
  assert(tx == 0 || tx == 1);
  if (buf->tx == tx) return 0;
  assert(!"TLS buffer direction mismatch");
  return Ztls_ErrorLibrary;
}

using RecordBufferHeap = ZmVHeap<"Ztls.RecordBuffer", 128, (1U << 20), ZiIOBuf_Align>;
static constexpr unsigned RecordBufferAlignBits = ZuIntrin::ctz(ZiIOBuf_Align);

void buffer__release_memory(Buffer *buf)
{
  if (buf->off) ZuClear(buf->base, buf->off);
  if (buf->isAllocated) RecordBufferHeap::free(buf->base, buf->capacity);
}

int buffer_reserve_aligned(Buffer *buf, size_t delta, uint8_t alignBits, int tx)
{
  int ret;
  if ((ret = buffer_require_direction(buf, tx)) != 0) return ret;

  if (buf->base == NULL || delta > SIZE_MAX - buf->off)
    return buf->bounded ? Ztls_ErrorRawOutput : Ztls_ErrorNoMemory;

  size_t required = buf->off + delta;
  if (buf->capacity < required ||
    (buf->alignBits < alignBits &&
      (alignBits >= (sizeof(uintptr_t) << 3) ||
	(uintptr_t(buf->base) & ((uintptr_t(1) << alignBits) - 1)) != 0))) {
    if (buf->bounded) return Ztls_ErrorRawOutput;
    size_t new_capacity = buf->capacity;
    if (new_capacity < 1024) new_capacity = 1024;
    while (new_capacity < required) {
      if (new_capacity > SIZE_MAX/2) {
	new_capacity = required;
	break;
      }
      new_capacity *= 2;
    }
    if (new_capacity > UINT32_MAX || alignBits > RecordBufferAlignBits) return Ztls_ErrorNoMemory;
    auto data = static_cast<uint8_t *>(RecordBufferHeap::alloc(new_capacity));
    if (buf->off) memcpy(data, buf->base, buf->off);
    buffer__release_memory(buf);
    buf->base = data;
    buf->capacity = new_capacity;
    buf->isAllocated = 1;
    buf->alignBits = alignBits;
  }

  return 0;
}

int buffer_reserve(Buffer *buf, size_t delta, int tx)
{ return buffer_reserve_aligned(buf, delta, 0, tx); }

int buffer__do_pushv(Buffer *buf, const void *src, size_t len)
{
  int ret;

  if (len == 0) return 0;
  if ((ret = buffer_reserve(buf, len, buf->tx)) != 0) return ret;
  memcpy(buf->base + buf->off, src, len);
  buf->off += len;
  return 0;
}

static void build_aad(uint8_t aad[5], size_t reclen)
{
  aad[0] = Ztls_ContentTypeAppdata;
  aad[1] = Ztls_RecordVerMajor;
  aad[2] = Ztls_RecordVerMinor;
  aad[3] = (uint8_t)(reclen >> 8);
  aad[4] = (uint8_t)reclen;
}

static int aead_decrypt(
  TrafficProtection *ctx, void *output, size_t *outlen, const void *input, size_t inlen)
{
  uint8_t aad[5];

  build_aad(aad, inlen);
  if ((*outlen = aead_decrypt(ctx->aead, output, input, inlen, ctx->seq, aad, sizeof(aad))) ==
    SIZE_MAX)
    return Ztls_AlertBadRecordMac;
  ++ctx->seq;
  return 0;
}

static int record_serial_available(TrafficProtection *enc)
{ return enc->aead == NULL || enc->aead->serialEncrypt(); }

template <typename Sink> struct RecordWriter {
  Sink		*sink;
  size_t	remaining;
  int		contiguous;
};

template <typename Sink>
static int
record_write(RecordWriter<Sink> *writer, const void *input, size_t length, AeadContext *aead)
{
  const uint8_t *src = static_cast<const uint8_t *>(input);
  int		 ret;
  if (length > writer->remaining) return Ztls_ErrorLibrary;
  while (length != 0) {
    void  *dst = NULL;
    size_t capacity = 0, written;
    if ((ret = writer->sink->next(&dst, &capacity)) != 0) return ret;
    if (dst == NULL || capacity == 0) return Ztls_ErrorLibrary;
    size_t count = length < capacity ? length : capacity;
    if (aead != NULL) {
      if ((ret = aead->encChunk(dst, src, count, &written)) != 0 || written != count)
	return Ztls_ErrorLibrary;
    } else {
      memcpy(dst, src, count);
    }
    if ((ret = writer->sink->commit(count)) != 0) return ret;
    src += count;
    length -= count;
    writer->remaining -= count;
  }
  return 0;
}

static size_t record_wire_size(TrafficProtection *enc, size_t length)
{ return length + 5 + (enc->aead == NULL ? 0 : enc->aead->algo->tagSize + 1); }

// A record has one nonce, one cipher stream and one tag, independent of destination span
// boundaries.
template <typename Sink>
static int record_emitv(
  RecordWriter<Sink> *writer, TrafficProtection *enc, uint8_t type, Bytes *input, size_t incnt,
  size_t length)
{
  uint8_t      header[5] = {type, Ztls_RecordVerMajor, Ztls_RecordVerMinor};
  uint8_t      tag[Ztls_MaxDigestSize];
  size_t       wire_size = record_wire_size(enc, length), written;
  AeadContext *aead = enc->aead;
  int	       ret;
  uint64_t     nonce = enc->seq;
  if (enc->failed || length > Ztls_MaxPlainRecordSize) return Ztls_ErrorLibrary;
  if (!writer->contiguous && !record_serial_available(enc)) return Ztls_ErrorNotAvailable;
  if (aead != NULL) header[0] = Ztls_ContentTypeAppdata;
  header[3] = (uint8_t)((wire_size - 5) >> 8);
  header[4] = (uint8_t)(wire_size - 5);
  if ((ret = record_write(writer, header, sizeof(header), NULL)) != 0) return ret;
  if (aead == NULL) {
    for (size_t i = 0; i != incnt; ++i)
      if ((ret = record_write(writer, input[i].data(), input[i].length(), NULL)) != 0) return ret;
    return 0;
  }
  if (writer->contiguous) {
    // Preserve the optimized vector operation for contiguous destinations.
    void  *dst = NULL;
    size_t capacity = 0, count = incnt + 1, size = length + aead->algo->tagSize + 1;
    input[incnt] = wire_bytes(&type, 1);
    if ((ret = writer->sink->next(&dst, &capacity)) != 0) return ret;
    if (dst == NULL || capacity < size || size > writer->remaining) return Ztls_ErrorLibrary;
    if (incnt == 1 && input[0].data() == dst) {
      // Handshake encryption aliases its formatting buffer; _v_s requires disjoint input.
      aead_encrypt_v(aead, dst, input, count, nonce, header, sizeof(header));
      written = size;
    } else {
      written = aead_encrypt_v_s(aead, dst, input, count, nonce, header, sizeof(header), NULL);
    }
    ++enc->seq;
    if (written != size) return Ztls_ErrorLibrary;
    if ((ret = writer->sink->commit(written)) != 0) return ret;
    writer->remaining -= written;
    return 0;
  }
  if (aead->encBegin(nonce, header, sizeof(header)) != 0) return Ztls_ErrorLibrary;
  ++enc->seq;
  for (size_t i = 0; i != incnt; ++i)
    if ((ret = record_write(writer, input[i].data(), input[i].length(), aead)) != 0) return ret;
  if ((ret = record_write(writer, &type, 1, aead)) != 0) return ret;
  void	*dst = NULL;
  size_t capacity = 0;
  if (aead->algo->tagSize > sizeof(tag) || aead->algo->tagSize > writer->remaining)
    return Ztls_ErrorLibrary;
  if ((ret = writer->sink->next(&dst, &capacity)) != 0) return ret;
  if (dst == NULL || capacity == 0) return Ztls_ErrorLibrary;
  if (capacity >= aead->algo->tagSize) {
    if (aead->encEnd(dst, capacity, &written) != 0 || written != aead->algo->tagSize)
      return Ztls_ErrorLibrary;
    if ((ret = writer->sink->commit(written)) != 0) return ret;
    writer->remaining -= written;
    return 0;
  }
  if (aead->encEnd(tag, sizeof(tag), &written) != 0 || written != aead->algo->tagSize)
    return Ztls_ErrorLibrary;
  return record_write(writer, tag, written, NULL);
}

template <typename Sink>
static int record_emit(
  RecordWriter<Sink> *writer, TrafficProtection *enc, uint8_t type, const void *input,
  size_t length)
{
  Bytes spans[2] = {wire_bytes(input, length)};
  return record_emitv(writer, enc, type, spans, 1, length);
}

template <typename Sink>
static int record_transaction_(
  Sink *sink, TrafficProtection *enc, uint8_t type, const void *input, size_t length,
  int contiguous)
{
  int ret;
  if (!sink) return Ztls_ErrorLibrary;
  if (!contiguous && !record_serial_available(enc)) return Ztls_ErrorNotAvailable;
  RecordWriter writer{sink, record_wire_size(enc, length), contiguous};
  if (enc->failed) return Ztls_ErrorLibrary;
  if ((ret = sink->reserve(writer.remaining)) != 0) {
    sink->discard();
    return ret;
  }
  if ((ret = record_emit(&writer, enc, type, input, length)) == 0)
    ret = writer.remaining == 0 ? sink->finish() : Ztls_ErrorLibrary;
  if (ret != 0) {
    enc->failed = 1;
    sink->discard();
  }
  return ret;
}

static int record_transaction(
  Ztls::RecordSink *sink, TrafficProtection *enc, uint8_t type, const void *input, size_t length)
{ return record_transaction_(sink, enc, type, input, length, 0); }

// A contiguous record has already reserved space before the writer borrows
// any input that may alias its destination.
class ContiguousRecord {
public:
  explicit ContiguousRecord(Buffer *buffer) : m_buffer{buffer} { }

  int reserve(size_t length) const
  { return length <= m_buffer->capacity - m_buffer->off ? 0 : Ztls_ErrorLibrary; }
  int next(void **base, size_t *capacity) const
  {
    *base = m_buffer->base + m_buffer->off;
    *capacity = m_buffer->capacity - m_buffer->off;
    return 0;
  }
  int commit(size_t length)
  {
    if (length > m_buffer->capacity - m_buffer->off) return Ztls_ErrorLibrary;
    m_buffer->off += length;
    return 0;
  }
  int finish() const { return 0; }
  void discard() const { }
private:
  Buffer	*m_buffer;
};

static int
buffer_record(Buffer *buf, TrafficProtection *enc, uint8_t type, const void *input, size_t length)
{
  if (buf->recordSink != NULL)
    return record_transaction(buf->recordSink, enc, type, input, length);
  int ret = buffer_reserve_aligned(
    buf, record_wire_size(enc, length), enc->aead != NULL ? enc->aead->algo->alignBits : 0, 1);
  if (ret != 0) return ret;
  ContiguousRecord sink{buf};
  return record_transaction_(&sink, enc, type, input, length, 1);
}

static int buffer_push_enc_records(
  Buffer *buf, uint8_t type, const uint8_t *src, size_t len, TrafficProtection *enc)
{
  while (len != 0) {
    size_t size = len < Ztls_MaxPlainRecordSize ? len : Ztls_MaxPlainRecordSize;
    int	   ret = buffer_record(buf, enc, type, src, size);
    if (ret != 0) return ret;
    src += size;
    len -= size;
  }
  return 0;
}

ZuDerive(
  RecordPlaintext, (ZtBArray<ZtArraySecret<true, ZtArrayHeapID<"Ztls.Engine.RecordPlaintext">>>));

static int buffer_encrypt_record(Buffer *buf, size_t recStart, TrafficProtection *enc)
{
  size_t  bodylen = buf->off - recStart - 5;
  uint8_t type = buf->base[recStart];
  int	  ret;

  if (buf->recordSink != NULL) {
    for (size_t off = 0; off < bodylen;) {
      size_t len = bodylen - off;
      if (len > Ztls_MaxPlainRecordSize) len = Ztls_MaxPlainRecordSize;
      int ret =
	record_transaction(buf->recordSink, enc, type, buf->base + recStart + 5 + off, len);
      if (ret != 0) return ret;
      off += len;
    }
    return 0;
  }

  if (bodylen <= Ztls_MaxPlainRecordSize) {
    size_t extra = record_wire_size(enc, bodylen) - bodylen - 5;
    if ((ret = buffer_reserve_aligned(buf, extra, enc->aead->algo->alignBits, 1)) != 0)
      return ret;
    buf->off = recStart;
    ContiguousRecord sink{buf};
    return record_transaction_(&sink, enc, type, buf->base + recStart + 5, bodylen, 1);
  }

  RecordPlaintext plaintext(buf->base + recStart + 5, bodylen);
  ZuClear(buf->base + recStart, bodylen + 5);
  buf->off = recStart;
  return buffer_push_enc_records(buf, type, plaintext.data(), bodylen, enc);
}

int RecordMessageEmitter::beginMessage()
{
  auto self = this;
  int  ret;

  self->recStart = self->buf->off;
  Ztls_BufferPush_1(
    self->buf, Ztls_ContentTypeHS, Ztls_RecordVerMajor, Ztls_RecordVerMinor, 0, 0);
  ret = 0;
Exit:
  return ret;
}

int RecordMessageEmitter::commitMessage()
{
  auto self = this;
  int  ret;

  if (self->enc->aead != NULL) {
    ret = buffer_encrypt_record(self->buf, self->recStart, self->enc);
  } else {
    // TODO allow CH,SH,HRR above 16KB
    size_t sz = self->buf->off - self->recStart - 5;
    assert(sz <= Ztls_MaxPlainRecordSize);
    self->buf->base[self->recStart + 3] = (uint8_t)(sz >> 8);
    self->buf->base[self->recStart + 4] = (uint8_t)(sz);
    TrafficProtection clear = {};
    if (self->buf->recordSink != NULL)
      return record_transaction(
	self->buf->recordSink, &clear, Ztls_ContentTypeHS, self->buf->base + self->recStart + 5,
	sz);
    ret = 0;
  }

  return ret;
}

#define Ztls_BufferPushExtension(buf, type, block) \
  do { \
    Ztls_BufferPush16((buf), (type)); \
    Ztls_BufferPushBlock((buf), 2, block); \
  } while (0);

#define Ztls_DecodeOpenExtensions(src, end, hstype, exttype, block) \
  do { \
    ExtensionBitmap bitmap = {0}; \
    Ztls_DecodeOpenBlock((src), end, 2, { \
      while ((src) != end) { \
	if ((ret = decode16((exttype), &(src), end)) != 0) goto Exit; \
	if (!extension_bitmap_testandset(&bitmap, (hstype), *(exttype))) { \
	  ret = Ztls_AlertIllegalParameter; \
	  goto Exit; \
	} \
	Ztls_DecodeOpenBlock((src), end, 2, block); \
      } \
    }); \
  } while (0)

#define Ztls_DecodeExtensions(src, end, hstype, exttype, block) \
  do { \
    Ztls_DecodeOpenExtensions((src), end, hstype, exttype, block); \
    Ztls_DecodeCheckBlockEnd((src), end); \
  } while (0)

int decode8(uint8_t *value, const uint8_t **src, const uint8_t *end)
{
  if (*src == end) return Ztls_AlertDecodeError;
  *value = *(*src)++;
  return 0;
}

int decode16(uint16_t *value, const uint8_t **src, const uint8_t *end)
{
  if (end - *src < 2) return Ztls_AlertDecodeError;
  *value = load_be<uint16_t>(*src);
  *src += 2;
  return 0;
}

int decode24(uint32_t *value, const uint8_t **src, const uint8_t *end)
{
  if (end - *src < 3) return Ztls_AlertDecodeError;
  *value = load_be24(*src);
  *src += 3;
  return 0;
}

int decode32(uint32_t *value, const uint8_t **src, const uint8_t *end)
{
  if (end - *src < 4) return Ztls_AlertDecodeError;
  *value = load_be<uint32_t>(*src);
  *src += 4;
  return 0;
}

int decode64(uint64_t *value, const uint8_t **src, const uint8_t *end)
{
  if (end - *src < 8) return Ztls_AlertDecodeError;
  *value = load_be<uint64_t>(*src);
  *src += 8;
  return 0;
}

// This function preserves the flags and  modes (e.g., `offered`, `accepted`, `cipher`), they can
// be used afterwards.
static void clear_ech(Ech *ech, int isServer)
{
  if (ech->aead != NULL) {
    aead_free(ech->aead);
    ech->aead = NULL;
  }
  ZuClear(ech->innerClientRandom, Ztls_HelloRandomSize);
  if (!isServer) {
    ech->client.enc.null();
    ech->client.publicName.null();
    ech->client.firstEch.null();
  }
}

// Decodes one ECHConfigContents (tls-esni-15 section 4). `decoded->kem` and `cipher` may be NULL
// even when the function returns zero, if the corresponding entries are not found.
static int decode_one_ech_config(
  const HpkeKem **kems, const HpkeSuite **ciphers, DecodedEchConfig *decoded, const uint8_t **src,
  const uint8_t *const end)
{
  EchPublicName public_name_buf;
  int		ret;

  *decoded = (DecodedEchConfig){0};

  if ((ret = decode8(&decoded->id, src, end)) != 0) goto Exit;
  uint16_t kem_id;
  if ((ret = decode16(&kem_id, src, end)) != 0) goto Exit;
  for (size_t i = 0; kems[i] != NULL; ++i) {
    if (kems[i]->id == kem_id) {
      decoded->kem = kems[i];
      break;
    }
  }
  Ztls_DecodeOpenBlock(*src, end, 2, {
    if (*src == end) {
      ret = Ztls_AlertDecodeError;
      goto Exit;
    }
    decoded->publicKey = wire_bytes(*src, end - *src);
    *src = end;
  });
  Ztls_DecodeOpenBlock(*src, end, 2, {
    do {
      uint16_t kdf_id;
      uint16_t aead_id;
      if ((ret = decode16(&kdf_id, src, end)) != 0) goto Exit;
      if ((ret = decode16(&aead_id, src, end)) != 0) goto Exit;
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
  if ((ret = decode8(&decoded->maxNameLength, src, end)) != 0) goto Exit;

#define Ztls_SkipDecoded() \
  do { \
    decoded->kem = NULL; \
    decoded->cipher = NULL; \
  } while (0)

  // Decode publicName. The specification requires clients to ignore (upon parsing ESNIConfigList)
  // or reject (upon handshake) public names that are not DNS names or IPv4 addresses. We ignore
  // IPv4 and v6 addresses during parsing (IPv6 addresses never looks like DNS names), and
  // delegate the responsibility of rejecting non-DNS names to the certificate verify callback.
  Ztls_DecodeOpenBlock(*src, end, 1, {
    if (*src == end) {
      ret = Ztls_AlertDecodeError;
      goto Exit;
    }
    assign_name(public_name_buf, {reinterpret_cast<const char *>(*src), size_t(end - *src)});
    if (server_name_is_ipaddr(public_name_buf.data())) {
      Ztls_SkipDecoded();
    } else {
      decoded->publicName = wire_bytes(*src, end - *src);
    }
    *src = end;
  });

  Ztls_DecodeBlock(*src, end, 2, {
    while (*src < end) {
      uint16_t type;
      if ((ret = decode16(&type, src, end)) != 0) goto Exit;
      Ztls_DecodeOpenBlock(*src, end, 2, { *src = end; });
      //  if a critital extension is found, indicate that the config cannot be used
      if ((type & 0x8000) != 0) Ztls_SkipDecoded();
    }
  });

#undef Ztls_SkipDecoded

Exit:
  return ret;
}

static int cli_decode_ech_config_list(Context *ctx, DecodedEchConfig *decoded, Bytes config_list)
{
  const uint8_t *src = config_list.data(), *const end = src + config_list.length();
  int match_found = 0, ret;

  *decoded = (DecodedEchConfig){0};

  Ztls_DecodeBlock(src, end, 2, {
    do {
      const uint8_t *config_start = src;
      uint16_t	     version;
      if ((ret = decode16(&version, &src, end)) != 0) goto Exit;
      Ztls_DecodeOpenBlock(src, end, 2, {
	// If the block is the one that we recognize, parse it, then adopt if if possible.
	// Otherwise, skip.
	if (version == Ztls_ECHConfigVer) {
	  DecodedEchConfig thisconf;
	  if ((ret = decode_one_ech_config(
		 ctx->ech.client.kems, ctx->ech.client.ciphers, &thisconf, &src, end)) != 0)
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
  if (ret != 0) *decoded = (DecodedEchConfig){0};
  return ret;
}

static int client_setup_ech(Ech *ech, DecodedEchConfig *decoded, RandomSource *random)
{
  Buffer  infobuf;
  uint8_t infobuf_smallbuf[256];
  int	  ret;

  // setup `enc` and `aead` by running HPKE
  buffer_init_tx(&infobuf, infobuf_smallbuf, sizeof(infobuf_smallbuf));
  Ztls_BufferPushv(&infobuf, ech_info_prefix, sizeof(ech_info_prefix));
  Ztls_BufferPushv(&infobuf, decoded->bytes.data(), decoded->bytes.length());
  if ((ret = hpke_setup_base_s(
	 decoded->kem, decoded->cipher, &ech->client.enc, &ech->aead, decoded->publicKey,
	 wire_bytes(infobuf.base, infobuf.off))) != 0)
    goto Exit;

  // setup the rest
  ech->configId = decoded->id;
  ech->kem = decoded->kem;
  ech->cipher = decoded->cipher;
  random->fill(ech->innerClientRandom, Ztls_HelloRandomSize);
  ech->client.maxNameLength = decoded->maxNameLength;
  assign_name(ech->client.publicName, ZuCSpan{decoded->publicName});

Exit:
  buffer_dispose(&infobuf);
  if (ret != 0) clear_ech(ech, 0);
  return ret;
}

static void client_setup_ech_grease(
  Ech *ech, RandomSource *random, const HpkeKem **kems, const HpkeSuite **ciphers,
  ZuCSpan sni_name)
{
  static const size_t x25519_key_size = 32;
  uint8_t	      random_secret[Ztls_AES128KeySize + Ztls_AESIVSize];

  // pick up X25519, AES-128-GCM or bail out
  for (size_t i = 0; kems[i] != NULL; ++i) {
    if (kems[i]->id == Ztls_HPKEKEMX25519SHA256) {
      ech->kem = kems[i];
      break;
    }
  }
  for (size_t i = 0; ciphers[i] != NULL; ++i) {
    if (ciphers[i]->id.kdf == Ztls_HPKEHKDFSHA256 &&
      ciphers[i]->id.aead == Ztls_HPKEAEADAES128GCM) {
      ech->cipher = ciphers[i];
      break;
    }
  }
  if (ech->kem == NULL || ech->cipher == NULL) {
    clear_ech(ech, 0);
    return;
  }

  // aead is generated from random
  random->fill(random_secret, sizeof(random_secret));
  ech->aead =
    aead_new_direct(ech->cipher->aead, 1, random_secret, random_secret + Ztls_AES128KeySize);
  ZuClear(random_secret, sizeof(random_secret));

  // `enc` is random bytes
  ech->client.enc.length(x25519_key_size);
  random->fill(ech->client.enc.data(), ech->client.enc.length());

  // setup the rest (innerClientRandom is left zeros)
  random->fill(&ech->configId, sizeof(ech->configId));
  ech->client.maxNameLength = 64;
  assign_name(ech->client.publicName, sni_name);
}

#define Ztls_ECHConfirmationSrvHello "ech accept confirmation"
#define Ztls_ECHConfirmationHRR "hrr ech accept confirmation"
static int ech_calc_confirmation(
  KeySchedule *sched, void *dst, const uint8_t *inner_random, const char *label, Bytes message)
{
  HashContext *hash = NULL;
  uint8_t      secret[Ztls_MaxDigestSize], transcript_hash[Ztls_MaxDigestSize];
  int	       ret;

  // calc transcript hash using the modified ServerHello / HRR
  if ((hash = sched->hashes[0].ctx->clone()) == NULL) {
    ret = Ztls_ErrorNoMemory;
    goto Exit;
  }
  hash->update(message.data(), message.length());
  {
    hash->final(transcript_hash);
    delete hash;
  }
  hash = NULL;

  // HKDF extract and expand
  if ((ret = hkdf_extract(
	 sched->hashes[0].algo, secret, wire_bytes(NULL, 0),
	 wire_bytes(inner_random, Ztls_HelloRandomSize))) != 0)
    goto Exit;
  if ((ret = hkdf_expand_label(
	 sched->hashes[0].algo, dst, 8, wire_bytes(secret, sched->hashes[0].algo->digestSize),
	 label, wire_bytes(transcript_hash, sched->hashes[0].algo->digestSize), NULL)) != 0)
    goto Exit;

Exit:
  ZuClear(secret, sizeof(secret));
  ZuClear(transcript_hash, sizeof(transcript_hash));
  if (hash != NULL) delete hash;
  return ret;
}

static void key_schedule_free(KeySchedule *sched)
{
  size_t i;
  ZuClear(sched->secret, sizeof(sched->secret));
  for (i = 0; i != sched->numHashes; ++i) {
    delete sched->hashes[i].ctx;
    if (sched->hashes[i].ctxOuter != NULL) delete sched->hashes[i].ctxOuter;
  }
  delete sched;
}

static KeySchedule *
key_schedule_new(const CipherSuite *preferred, SuiteSpan offered, int use_outer)
{
#define Ztls_ForeachHash(block) \
  do { \
    const CipherSuite *cs; \
    if ((cs = preferred) != NULL) { \
      block \
    } \
    if (offered.length()) { \
      size_t i, j; \
      for (i = 0; i < offered.length(); ++i) { \
	if ((cs = offered[i]) && (preferred == NULL || cs->hash != preferred->hash)) { \
	  for (j = 0; j != i; ++j) \
	    if (cs->hash == offered[j]->hash) break; \
	  if (j == i) { \
	    block \
	  } \
	} \
      } \
    } \
  } while (0)

  if (!preferred && !offered.length()) return nullptr;
  size_t numHashes = 0;
  Ztls_ForeachHash({ ++numHashes; });
  auto sched = new KeySchedule{};
  sched->hashes.length(numHashes);

  // setup the hash algos and contexts
  Ztls_ForeachHash({
    sched->hashes[sched->numHashes].algo = cs->hash;
    if ((sched->hashes[sched->numHashes].ctx = cs->hash->create()) == NULL) goto Fail;
    if (use_outer) {
      if ((sched->hashes[sched->numHashes].ctxOuter = cs->hash->create()) == NULL) {
	auto ctx = sched->hashes[sched->numHashes].ctx;
	delete ctx;
	goto Fail;
      }
    } else {
      sched->hashes[sched->numHashes].ctxOuter = NULL;
    }
    ++sched->numHashes;
  });

  return sched;
Fail:
  key_schedule_free(sched);
  return NULL;

#undef Ztls_ForeachHash
}

static int key_schedule_extract(KeySchedule *sched, Bytes ikm)
{
  int ret;

  if (ikm.data() == NULL)
    ikm = wire_bytes(zeroes_of_max_digest_size, sched->hashes[0].algo->digestSize);

  if (sched->generation != 0 &&
    (ret = hkdf_expand_label(
       sched->hashes[0].algo, sched->secret, sched->hashes[0].algo->digestSize,
       wire_bytes(sched->secret, sched->hashes[0].algo->digestSize), "derived",
       wire_bytes(sched->hashes[0].algo->emptyDigest, sched->hashes[0].algo->digestSize),
       NULL)) != 0)
    return ret;

  ++sched->generation;
  ret = hkdf_extract(
    sched->hashes[0].algo, sched->secret,
    wire_bytes(sched->secret, sched->hashes[0].algo->digestSize), ikm);
  return ret;
}

static int
key_schedule_select_cipher(KeySchedule *sched, const CipherSuite *cs, int reset, Bytes reset_ikm)
{
  size_t found_slot = SIZE_MAX, i;
  int	 ret;

  assert(sched->generation == 1);

  // find the one, while freeing others
  for (i = 0; i != sched->numHashes; ++i) {
    if (sched->hashes[i].algo == cs->hash) {
      assert(found_slot == SIZE_MAX);
      found_slot = i;
    } else {
      delete sched->hashes[i].ctx;
      if (sched->hashes[i].ctxOuter != NULL) delete sched->hashes[i].ctxOuter;
    }
  }
  if (found_slot != 0) {
    sched->hashes[0] = sched->hashes[found_slot];
    reset = 1;
  }
  sched->numHashes = 1;

  // recalculate the hash if a different hash as been selected than the one we used for
  // calculating the early secrets
  if (reset) {
    --sched->generation;
    memset(sched->secret, 0, sizeof(sched->secret));
    if ((ret = key_schedule_extract(sched, reset_ikm)) != 0) goto Exit;
  }

  ret = 0;
Exit:
  return ret;
}

static void key_schedule_select_outer(KeySchedule *sched)
{
  // This function is called when receiving a cleartext message (Server Hello), after the
  // cipher-suite is determined (and hence the hash also), if ECH was offered
  assert(sched->generation == 1);
  assert(sched->numHashes == 1);
  assert(sched->hashes[0].ctxOuter != NULL);

  delete sched->hashes[0].ctx;
  sched->hashes[0].ctx = sched->hashes[0].ctxOuter;
  sched->hashes[0].ctxOuter = NULL;
}

void _key_schedule_update_hash(
  KeySchedule *sched, const uint8_t *msg, size_t msglen, int use_outer)
{
  size_t i;

  for (i = 0; i != sched->numHashes; ++i) {
    HashContext *ctx = use_outer ? sched->hashes[i].ctxOuter : sched->hashes[i].ctx;
    ctx->update(msg, msglen);
  }
}

static void key_sched_ch1hash_prefix(KeySchedule *sched)
{
  uint8_t prefix[4] = {Ztls_HsTypeMessageHash, 0, 0, (uint8_t)sched->hashes[0].algo->digestSize};
  _key_schedule_update_hash(sched, prefix, sizeof(prefix), 0);
}

static void key_schedule_extract_ch1hash(KeySchedule *sched, uint8_t *hash)
{
  assert(sched->hashes[0].ctxOuter == NULL);
  sched->hashes[0].ctx->final(hash, HashFinalMode::Reset);
}

static void key_sched_post_ch1hash(KeySchedule *sched)
{
  size_t       digestSize = sched->hashes[0].algo->digestSize;
  HashContext *hashes[3] = {sched->hashes[0].ctx, sched->hashes[0].ctxOuter, NULL};
  uint8_t      ch1hash[Ztls_MaxDigestSize];
  uint8_t      prefix[4] = {Ztls_HsTypeMessageHash, 0, 0, (uint8_t)digestSize};

  for (size_t i = 0; hashes[i] != NULL; ++i) {
    hashes[i]->final(ch1hash, HashFinalMode::Reset);
    hashes[i]->update(prefix, sizeof(prefix));
    hashes[i]->update(ch1hash, digestSize);
  }

  ZuClear(ch1hash, sizeof(ch1hash));
}

static int
derive_secret_with_hash(KeySchedule *sched, void *secret, const char *label, const uint8_t *hash)
{
  int ret = hkdf_expand_label(
    sched->hashes[0].algo, secret, sched->hashes[0].algo->digestSize,
    wire_bytes(sched->secret, sched->hashes[0].algo->digestSize), label,
    wire_bytes(hash, sched->hashes[0].algo->digestSize), NULL);
  return ret;
}

static int derive_secret(KeySchedule *sched, void *secret, const char *label)
{
  uint8_t hash_value[Ztls_MaxDigestSize];

  sched->hashes[0].ctx->final(hash_value, HashFinalMode::Snapshot);
  int ret = derive_secret_with_hash(sched, secret, label, hash_value);
  ZuClear(hash_value, sizeof(hash_value));
  return ret;
}

static int derive_empty_secret(KeySchedule *sched, void *secret, const char *label)
{ return derive_secret_with_hash(sched, secret, label, sched->hashes[0].algo->emptyDigest); }

static int derive_exporter_secret(Ztls::Cxn *tls, int is_early)
{
  if (!tls->ctx->useExporter) return 0;

  auto &slot = is_early ? tls->exporterMasterSecret.early : tls->exporterMasterSecret.oneRtt;
  assert(!slot.length());
  slot.length(tls->keySchedule->hashes[0].algo->digestSize);
  return derive_secret(tls->keySchedule, slot.data(), is_early ? "e exp master" : "exp master");
}

static int derive_resumption_secret(KeySchedule *sched, uint8_t *secret, Bytes nonce)
{
  int ret;

  if ((ret = derive_secret(sched, secret, "res master")) != 0) goto Exit;
  if ((ret = hkdf_expand_label(
	 sched->hashes[0].algo, secret, sched->hashes[0].algo->digestSize,
	 wire_bytes(secret, sched->hashes[0].algo->digestSize), "resumption", nonce, NULL)) != 0)
    goto Exit;

Exit:
  if (ret != 0) ZuClear(secret, sched->hashes[0].algo->digestSize);
  return ret;
}

static int decode_new_session_ticket(
  Ztls::Cxn *tls, uint32_t *lifetime, uint32_t *age_add, Bytes *nonce, Bytes *ticket,
  uint32_t *maxEarlyDataSize, const uint8_t *src, const uint8_t *const end)
{
  uint16_t exttype;
  int	   ret;

  if ((ret = decode32(lifetime, &src, end)) != 0) goto Exit;
  if ((ret = decode32(age_add, &src, end)) != 0) goto Exit;
  Ztls_DecodeOpenBlock(src, end, 1, {
    *nonce = wire_bytes(src, end - src);
    src = end;
  });
  Ztls_DecodeOpenBlock(src, end, 2, {
    if (src == end) {
      ret = Ztls_AlertDecodeError;
      goto Exit;
    }
    *ticket = wire_bytes(src, end - src);
    src = end;
  });

  *maxEarlyDataSize = 0;
  Ztls_DecodeExtensions(src, end, Ztls_HsTypeNewTicket, &exttype, {
    if (tls->ctx->onExtension != NULL &&
      (ret = tls->ctx->onExtension->call(
	       tls, Ztls_HsTypeNewTicket, exttype, wire_bytes(src, end - src)) != 0))
      goto Exit;
    switch (exttype) {
    case Ztls_ExtTypeEarly:
      if ((ret = decode32(maxEarlyDataSize, &src, end)) != 0) goto Exit;
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

static int decode_stored_session_ticket(
  Ztls::Cxn *tls, const KeyExchangeAlgo **keyShare, const CipherSuite **cs, Bytes *secret,
  uint32_t *obfuscatedTicketAge, Bytes *ticket, uint32_t *maxEarlyDataSize, const uint8_t *src,
  const uint8_t *const end)
{
  uint16_t kxid, csid;
  uint32_t lifetime, age_add;
  uint64_t obtained_at, now;
  Bytes	   nonce;
  int	   ret;

  // decode
  if ((ret = decode64(&obtained_at, &src, end)) != 0) goto Exit;
  if ((ret = decode16(&kxid, &src, end)) != 0) goto Exit;
  if ((ret = decode16(&csid, &src, end)) != 0) goto Exit;
  Ztls_DecodeOpenBlock(src, end, 3, {
    if ((ret = decode_new_session_ticket(
	   tls, &lifetime, &age_add, &nonce, ticket, maxEarlyDataSize, src, end)) != 0)
      goto Exit;
    src = end;
  });
  Ztls_DecodeBlock(src, end, 2, {
    *secret = wire_bytes(src, end - src);
    src = end;
  });

  {
    // determine the key-exchange
    const KeyExchangeAlgo **cand;
    for (cand = tls->ctx->keyExchanges; *cand != NULL; ++cand)
      if ((*cand)->id == kxid) break;
    if (*cand == NULL) {
      ret = Ztls_ErrorLibrary;
      goto Exit;
    }
    *keyShare = *cand;
  }

  {
    // determine the cipher-suite
    *cs = find_cipher_suite(tls->ctx->cipherSuites, csid);
    if (!*cs) {
      ret = Ztls_ErrorLibrary;
      goto Exit;
    }
  }

  // calculate obfuscatedTicketAge
  now = tls->ctx->getTime->call();
  if (!(obtained_at <= now && now - obtained_at < 7 * 86400 * 1000)) {
    ret = Ztls_ErrorLibrary;
    goto Exit;
  }
  *obfuscatedTicketAge = (uint32_t)(now - obtained_at) + age_add;

  ret = 0;
Exit:
  return ret;
}

static int get_traffic_key(
  const HashAlgo *algo, void *key, size_t keySize, int is_iv, const void *secret,
  Bytes hash_value, const char *label_prefix)
{
  return hkdf_expand_label(
    algo, key, keySize, wire_bytes(secret, algo->digestSize), is_iv ? "iv" : "key", hash_value,
    label_prefix);
}

static int get_traffic_keys(
  const AeadAlgo *aead, const HashAlgo *hash, void *key, void *iv, const void *secret,
  Bytes hash_value, const char *label_prefix)
{
  int ret;

  if (
    (ret = get_traffic_key(hash, key, aead->keySize, 0, secret, hash_value, label_prefix)) != 0 ||
    (ret = get_traffic_key(hash, iv, aead->ivSize, 1, secret, hash_value, label_prefix)) != 0) {
    ZuClear(key, aead->keySize);
    ZuClear(iv, aead->ivSize);
  }

  return ret;
}

static int setup_traffic_protection(
  Ztls::Cxn *tls, int is_enc, const char *secret_label, size_t epoch, uint64_t seq,
  int skip_notify)
{
  TrafficProtection *ctx = is_enc ? &tls->txTraffic : &tls->rxTraffic;

  if (secret_label != NULL) {
    int ret;
    if ((ret = derive_secret(tls->keySchedule, ctx->secret, secret_label)) != 0) return ret;
  }

  ctx->epoch = epoch;

  // special path for applications having their own record layer
  if (tls->ctx->updateTrafficKey != NULL) {
    if (skip_notify) return 0;
    return tls->ctx->updateTrafficKey->call(tls, is_enc, epoch, ctx->secret);
  }

  if (ctx->aead != NULL) aead_free(ctx->aead);
  if ((ctx->aead = aead_new(
	 tls->cipherSuite->aead, tls->cipherSuite->hash, is_enc, ctx->secret,
	 tls->ctx->hkdfLabelPrefix)) == NULL)
    return Ztls_ErrorNoMemory;
  // TODO obtain error from aead_new
  ctx->seq = seq;

  return 0;
}

static int commission_handshake_secret(Ztls::Cxn *tls)
{
  int is_enc = !isServer(tls);

  assert(tls->pendingHandshakeSecret.data() != NULL);
  memcpy(
    (is_enc ? &tls->txTraffic : &tls->rxTraffic)->secret, tls->pendingHandshakeSecret.data(),
    Ztls_MaxDigestSize);
  tls->pendingHandshakeSecret.null();

  return setup_traffic_protection(tls, is_enc, NULL, 2, 0, 1);
}

// the number should be changed upon incompatible format change
#define Ztls_SessionIDMagic "ptls0001"
#define Ztls_SessionIdMagicSize (sizeof(Ztls_SessionIDMagic) - 1)

static int encode_session_identifier(
  Context *ctx, Buffer *buf, uint32_t ticket_age_add, Bytes ticket_nonce, KeySchedule *sched,
  const char *serverName, uint16_t key_exchange_id, uint16_t csid, const char *negotiatedProtocol)
{
  int ret = 0;

  Ztls_BufferPushBlock(buf, 2, {
    // format id
    Ztls_BufferPushv(buf, Ztls_SessionIDMagic, Ztls_SessionIdMagicSize);
    // date
    Ztls_BufferPush64(buf, ctx->getTime->call());
    // resumption master secret
    Ztls_BufferPushBlock(buf, 2, {
      if ((ret = buffer_reserve(buf, sched->hashes[0].algo->digestSize, 1)) != 0) goto Exit;
      if ((ret = derive_resumption_secret(sched, buf->base + buf->off, ticket_nonce)) != 0)
	goto Exit;
      buf->off += sched->hashes[0].algo->digestSize;
    });
    // key-exchange
    Ztls_BufferPush16(buf, key_exchange_id);
    // cipher-suite
    Ztls_BufferPush16(buf, csid);
    // ticket_age_add
    Ztls_BufferPush32(buf, ticket_age_add);
    // session ID context
    Ztls_BufferPushBlock(buf, 2, {
      if (ctx->ticketContext.isSet) {
	Ztls_BufferPushv(buf, ctx->ticketContext.bytes, sizeof(ctx->ticketContext.bytes));
      } else if (serverName != NULL) {
	Ztls_BufferPushv(buf, serverName, strlen(serverName));
      }
    });
    // alpn
    Ztls_BufferPushBlock(buf, 1, {
      if (negotiatedProtocol != NULL)
	Ztls_BufferPushv(buf, negotiatedProtocol, strlen(negotiatedProtocol));
    });
  });

Exit:
  return ret;
}

int decode_session_identifier(
  uint64_t *issued_at, Bytes *psk, uint32_t *ticket_age_add, Bytes *ticket_ctx,
  uint16_t *key_exchange_id, uint16_t *csid, Bytes *negotiatedProtocol, const uint8_t *src,
  const uint8_t *const end)
{
  int ret = 0;

  Ztls_DecodeBlock(src, end, 2, {
    if (end - src < Ztls_SessionIdMagicSize ||
      memcmp(src, Ztls_SessionIDMagic, Ztls_SessionIdMagicSize) != 0) {
      ret = Ztls_AlertDecodeError;
      goto Exit;
    }
    src += Ztls_SessionIdMagicSize;
    if ((ret = decode64(issued_at, &src, end)) != 0) goto Exit;
    Ztls_DecodeOpenBlock(src, end, 2, {
      *psk = wire_bytes(src, end - src);
      src = end;
    });
    if ((ret = decode16(key_exchange_id, &src, end)) != 0) goto Exit;
    if ((ret = decode16(csid, &src, end)) != 0) goto Exit;
    if ((ret = decode32(ticket_age_add, &src, end)) != 0) goto Exit;
    Ztls_DecodeOpenBlock(src, end, 2, {
      *ticket_ctx = wire_bytes(src, end - src);
      src = end;
    });
    Ztls_DecodeOpenBlock(src, end, 1, {
      *negotiatedProtocol = wire_bytes(src, end - src);
      src = end;
    });
  });

Exit:
  return ret;
}

static size_t
build_cert_verify_signdata(uint8_t *data, KeySchedule *sched, const char *context_string)
{
  size_t datalen = 0;

  memset(data + datalen, 32, 64);
  datalen += 64;
  memcpy(data + datalen, context_string, strlen(context_string) + 1);
  datalen += strlen(context_string) + 1;
  sched->hashes[0].ctx->final(data + datalen, HashFinalMode::Snapshot);
  datalen += sched->hashes[0].algo->digestSize;
  assert(datalen <= Ztls_SignInputSize);

  return datalen;
}

static int calc_verify_data(void *output, KeySchedule *sched, const void *secret)
{
  HashContext *hmac;
  uint8_t      digest[Ztls_MaxDigestSize];
  int	       ret;

  if ((ret = hkdf_expand_label(
	 sched->hashes[0].algo, digest, sched->hashes[0].algo->digestSize,
	 wire_bytes(secret, sched->hashes[0].algo->digestSize), "finished", wire_bytes(NULL, 0),
	 NULL)) != 0)
    return ret;
  if ((hmac = hmac_create(sched->hashes[0].algo, digest, sched->hashes[0].algo->digestSize)) ==
    NULL) {
    ZuClear(digest, sizeof(digest));
    return Ztls_ErrorNoMemory;
  }

  sched->hashes[0].ctx->final(digest, HashFinalMode::Snapshot);
  hmac->update(digest, sched->hashes[0].algo->digestSize);
  ZuClear(digest, sizeof(digest));
  {
    hmac->final(output);
    delete hmac;
  }

  return 0;
}

static int verify_finished(Ztls::Cxn *tls, Bytes message)
{
  uint8_t verify_data[Ztls_MaxDigestSize];
  int	  ret;

  if (Ztls_HSHeaderSize + tls->keySchedule->hashes[0].algo->digestSize != message.length()) {
    ret = Ztls_AlertDecodeError;
    goto Exit;
  }

  if ((ret = calc_verify_data(verify_data, tls->keySchedule, tls->rxTraffic.secret)) != 0)
    goto Exit;
  if (!Ztls::ctEqual(
	{message.data() + Ztls_HSHeaderSize, tls->keySchedule->hashes[0].algo->digestSize},
	{verify_data, tls->keySchedule->hashes[0].algo->digestSize})) {
    ret = Ztls_AlertHSFailure;
    goto Exit;
  }

Exit:
  ZuClear(verify_data, sizeof(verify_data));
  return ret;
}

static int send_finished(Ztls::Cxn *tls, MessageEmitter *emitter)
{
  int ret;

  Ztls_PushMessage(emitter, tls->keySchedule, Ztls_HSTypeFinished, {
    if (
      (ret = buffer_reserve(emitter->buf, tls->keySchedule->hashes[0].algo->digestSize, 1)) != 0)
      goto Exit;
    if ((ret = calc_verify_data(
	   emitter->buf->base + emitter->buf->off, tls->keySchedule, tls->txTraffic.secret)) != 0)
      goto Exit;
    emitter->buf->off += tls->keySchedule->hashes[0].algo->digestSize;
  });

Exit:
  return ret;
}

static int send_session_ticket(Ztls::Cxn *tls, MessageEmitter *emitter)
{
  HashContext *msghash_backup = tls->keySchedule->hashes[0].ctx->clone();
  Buffer       session_id;
  char	       session_id_smallbuf[128];
  uint32_t     ticket_age_add;
  int	       ret = 0;

  assert(tls->ctx->ticketLifetime != 0);
  assert(tls->ctx->encryptTicket != NULL);

  buffer_init_tx(&session_id, session_id_smallbuf, sizeof(session_id_smallbuf));

  {
    // calculate verify-data that will be sent by the client
    size_t orig_off = emitter->buf->off;
    if (tls->pendingHandshakeSecret.data() != NULL && !tls->ctx->omitEndOfEarlyData) {
      assert(tls->state == Ztls_::State::SrvWaitEarlyEnd);
      Ztls_BufferPushMsgBody(emitter->buf, tls->keySchedule, Ztls_HsTypeEndOfEarly, {});
      emitter->buf->off = orig_off;
    }
    Ztls_BufferPushMsgBody(emitter->buf, tls->keySchedule, Ztls_HSTypeFinished, {
      if ((ret = buffer_reserve(emitter->buf, tls->keySchedule->hashes[0].algo->digestSize, 1)) !=
	0)
	goto Exit;
      if ((ret = calc_verify_data(
	     emitter->buf->base + emitter->buf->off, tls->keySchedule,
	     tls->pendingHandshakeSecret.data() != NULL ? tls->pendingHandshakeSecret.data() :
							  tls->rxTraffic.secret)) != 0)
	goto Exit;
      emitter->buf->off += tls->keySchedule->hashes[0].algo->digestSize;
    });
    emitter->buf->off = orig_off;
  }

  tls->ctx->random->fill(&ticket_age_add, sizeof(ticket_age_add));

  // build the raw nsk
  if (tls->keyShare != NULL &&
    (ret = encode_session_identifier(
       tls->ctx, &session_id, ticket_age_add, wire_bytes(NULL, 0), tls->keySchedule,
       tls->serverName.data(), tls->keyShare->id, tls->cipherSuite->id,
       tls->negotiatedProtocol.data())) != 0)
    goto Exit;

  // encrypt and send
  Ztls_PushMessage(emitter, tls->keySchedule, Ztls_HsTypeNewTicket, {
    Ztls_BufferPush32(emitter->buf, tls->ctx->ticketLifetime);
    Ztls_BufferPush32(emitter->buf, ticket_age_add);
    Ztls_BufferPushBlock(emitter->buf, 1, {});
    Ztls_BufferPushBlock(emitter->buf, 2, {
      if ((ret = tls->ctx->encryptTicket->call(
	     tls, 1, emitter->buf, wire_bytes(session_id.base, session_id.off))) != 0)
	goto Exit;
    });
    Ztls_BufferPushBlock(emitter->buf, 2, {
      if (tls->ctx->maxEarlyDataSize != 0)
	Ztls_BufferPushExtension(emitter->buf, Ztls_ExtTypeEarly, {
	  Ztls_BufferPush32(emitter->buf, tls->ctx->maxEarlyDataSize);
	});
    });
  });

Exit:
  buffer_dispose(&session_id);

  // restore handshake state
  delete tls->keySchedule->hashes[0].ctx;
  tls->keySchedule->hashes[0].ctx = msghash_backup;

  return ret;
}

static int push_change_cipher_spec(Ztls::Cxn *tls, MessageEmitter *emitter)
{
  TrafficProtection clear = {};
  const uint8_t	    value = 1;
  int		    ret;

  // check if we are requested to (or still need to)
  if (!tls->sendChangeCipherSpec) {
    ret = 0;
    goto Exit;
  }

  // CCS is a record, can only be sent when using a record-based protocol.
  if (!emitter->recordLayer()) {
    ret = Ztls_AlertUnwaitedMsg;
    goto Exit;
  }

  // emit CCS
  if ((ret = buffer_record(emitter->buf, &clear, Ztls_ContentChangeCipher, &value, 1)) != 0)
    goto Exit;

  tls->sendChangeCipherSpec = 0;
  ret = 0;
Exit:
  return ret;
}

static int push_additional_extensions(HandshakeProps *properties, Buffer *sendbuf)
{
  int ret;

  if (properties != NULL && properties->additionalExtensions != NULL) {
    RawExtension *ext;
    for (ext = properties->additionalExtensions; ext->type != UINT16_MAX; ++ext) {
      Ztls_BufferPushExtension(
	sendbuf, ext->type, { Ztls_BufferPushv(sendbuf, ext->data.data(), ext->data.length()); });
    }
  }
  ret = 0;
Exit:
  return ret;
}

static int push_signature_algorithms(VerifyCertificate *vc, Buffer *sendbuf)
{
  // The list sent when verify callback is not registered
  static const uint16_t default_algos[] = {Ztls_SigRSAPSSRSAESHA384, Ztls_SigRSAPSSRSAESHA256,
    Ztls_SigECDSAP384SHA384, Ztls_SigECDSAP256SHA256, Ztls_SigRSAPKCS1SHA256,
    Ztls_SigRSAPKCS1SHA1};
  int			ret;

  Ztls_BufferPushBlock(sendbuf, 2, {
    for (auto algorithm : vc ? vc->algos : ZuSpan<const uint16_t>{default_algos})
      Ztls_BufferPush16(sendbuf, algorithm);
  });

  ret = 0;
Exit:
  return ret;
}

static int
decode_signature_algorithms(SignatureAlgorithms *sa, const uint8_t **src, const uint8_t *end)
{
  int ret;

  Ztls_DecodeBlock(*src, end, 2, {
    do {
      uint16_t id;
      if ((ret = decode16(&id, src, end)) != 0) goto Exit;
      sa->push(id);
    } while (*src != end);
  });

  ret = 0;
Exit:
  return ret;
}

// @param hash optional argument for restricting the underlying hash algorithm
static int select_cipher(
  const CipherSuite **selected, SuiteSpan candidates, const uint8_t *src,
  const uint8_t *const end, int server_preference, int server_chacha_priority,
  const HashAlgo *hash)
{
  size_t found_index = SIZE_MAX;
  int	 ret;

  while (src != end) {
    uint16_t id;
    if ((ret = decode16(&id, &src, end)) != 0) goto Exit;
    for (size_t i = 0; i < candidates.length(); ++i) {
      if (candidates[i]->id == id && (hash == NULL || candidates[i]->hash == hash)) {
	if (server_preference && !(server_chacha_priority && id == Ztls_SuiteChaChaSHA256)) {
	  // preserve smallest matching index, and proceed to the next input
	  if (i < found_index) {
	    found_index = i;
	    break;
	  }
	} else {
	  //  return the pointer matching to the first input that can be used
	  *selected = candidates[i];
	  goto Exit;
	}
      }
    }
    // first position of the server list matched (server_preference)
    if (found_index == 0) break;
    // server preference is overridden only if the first entry of client-provided list is
    // chachapoly
    server_chacha_priority = 0;
  }
  if (found_index != SIZE_MAX) {
    *selected = candidates[found_index];
    ret = 0;
  } else {
    ret = Ztls_AlertHSFailure;
  }

Exit:
  return ret;
}

static int push_key_share_entry(Buffer *buf, uint16_t group, Bytes pubkey)
{
  int ret;

  Ztls_BufferPush16(buf, group);
  Ztls_BufferPushBlock(buf, 2, { Ztls_BufferPushv(buf, pubkey.data(), pubkey.length()); });
  ret = 0;
Exit:
  return ret;
}

static int decode_key_share_entry(
  uint16_t *group, Bytes *key_exchange, const uint8_t **src, const uint8_t *const end)
{
  int ret;

  if ((ret = decode16(group, src, end)) != 0) goto Exit;
  Ztls_DecodeOpenBlock(*src, end, 2, {
    *key_exchange = wire_bytes(*src, end - *src);
    *src = end;
  });

Exit:
  return ret;
}

static int select_key_share(
  const KeyExchangeAlgo **selected, Bytes *peerKey, const KeyExchangeAlgo **candidates,
  const uint8_t **src, const uint8_t *const end, int expect_one)
{
  int ret;

  *selected = NULL;

  if (expect_one && *src == end) {
    ret = Ztls_AlertIllegalParameter;
    goto Exit;
  }

  while (*src != end) {
    uint16_t group;
    Bytes    key;
    if ((ret = decode_key_share_entry(&group, &key, src, end)) != 0) goto Exit;
    const KeyExchangeAlgo **c = candidates;
    for (; *c != NULL; ++c) {
      if (*selected == NULL && (*c)->id == group) {
	*selected = *c;
	*peerKey = key;
      }
    }
    if (expect_one) {
      ret = *selected != NULL ? 0 : Ztls_AlertIllegalParameter;
      goto Exit;
    }
  }

  ret = 0;

Exit:
  return ret;
}

static int emit_server_name_extension(Buffer *buf, ZuCSpan serverName)
{
  int ret;

  Ztls_BufferPushBlock(buf, 2, {
    Ztls_BufferPush_1(buf, Ztls_SrvNameTypeHostname);
    Ztls_BufferPushBlock(
      buf, 2, { Ztls_BufferPushv(buf, serverName.data(), serverName.length()); });
  });

  ret = 0;
Exit:
  return ret;
}

// Within the outer ECH extension, returns the number of bytes that preceeds the AEAD-encrypted
// payload.
static inline size_t outer_ech_header_size(size_t enc_size) { return 10 + enc_size; }

// Flag to indicate which of ClientHelloInner, EncodedClientHelloInner, ClientHelloOuter is to be
// generated. When ECH is inactive, only ClientHelloInner is used.
namespace EncodeCHMode {
  enum { Inner, EncodedInner, Outer };
}

static int encode_client_hello(
  Context *ctx, Buffer *sendbuf, int mode, int is_second_flight, HandshakeProps *properties,
  const void *clientRandom, KeyExchangeContext *keyShareCtx, ZuCSpan sni_name,
  Bytes legacySessionId, Ech *ech, size_t *ech_size_offset, Bytes ech_replay, Bytes psk_secret,
  Bytes psk_identity, uint32_t obfuscatedTicketAge, size_t psk_binder_size, Bytes *cookie,
  int usingEarlyData)
{
  int ret;

  assert(mode == EncodeCHMode::Inner || ech != NULL);

  Ztls_BufferPushMsgBody(sendbuf, NULL, Ztls_HsTypeCliHello, {
    // legacyVersion
    Ztls_BufferPush16(sendbuf, 0x0303);
    // randomBytes
    Ztls_BufferPushv(sendbuf, clientRandom, Ztls_HelloRandomSize);
    // lecagy_session_id
    Ztls_BufferPushBlock(sendbuf, 1, {
      if (mode != EncodeCHMode::EncodedInner)
	Ztls_BufferPushv(sendbuf, legacySessionId.data(), legacySessionId.length());
    });
    // cipherSuites
    Ztls_BufferPushBlock(sendbuf, 2, {
      for (auto cs : ctx->cipherSuites) Ztls_BufferPush16(sendbuf, cs->id);
    });
    // legacy_compression_methods
    Ztls_BufferPushBlock(sendbuf, 1, { Ztls_BufferPush_1(sendbuf, 0); });
    // extensions
    Ztls_BufferPushBlock(sendbuf, 2, {
      if (mode == EncodeCHMode::Outer) {
	Ztls_BufferPushExtension(sendbuf, Ztls_ExtTypeEncCliHello, {
	  [[maybe_unused]] size_t ext_payload_from = sendbuf->off;
	  Ztls_BufferPush_1(sendbuf, Ztls_ECHCliHelloOuter);
	  Ztls_BufferPush16(sendbuf, ech->cipher->id.kdf);
	  Ztls_BufferPush16(sendbuf, ech->cipher->id.aead);
	  Ztls_BufferPush_1(sendbuf, ech->configId);
	  Ztls_BufferPushBlock(sendbuf, 2, {
	    if (!is_second_flight)
	      Ztls_BufferPushv(sendbuf, ech->client.enc.data(), ech->client.enc.length());
	  });
	  Ztls_BufferPushBlock(sendbuf, 2, {
	    assert(
	      sendbuf->off - ext_payload_from ==
	      outer_ech_header_size(is_second_flight ? 0 : ech->client.enc.length()));
	    if ((ret = buffer_reserve(sendbuf, *ech_size_offset, 1)) != 0) goto Exit;
	    memset(sendbuf->base + sendbuf->off, 0, *ech_size_offset);
	    sendbuf->off += *ech_size_offset;
	    *ech_size_offset = sendbuf->off - *ech_size_offset;
	  });
	});
      } else if (ech->aead != NULL) {
	Ztls_BufferPushExtension(sendbuf, Ztls_ExtTypeEncCliHello, {
	  Ztls_BufferPush_1(sendbuf, Ztls_ECHCliHelloInner);
	});
      } else if (ech_replay.data() != NULL) {
	Ztls_BufferPushExtension(sendbuf, Ztls_ExtTypeEncCliHello, {
	  Ztls_BufferPushv(sendbuf, ech_replay.data(), ech_replay.length());
	});
      }
      if (mode == EncodeCHMode::EncodedInner) {
	Ztls_BufferPushExtension(sendbuf, Ztls_ExtTypeECHOuterExts, {
	  Ztls_BufferPushBlock(
	    sendbuf, 1, { Ztls_BufferPush16(sendbuf, Ztls_ExtTypeKeyShare); });
	});
      } else {
	Ztls_BufferPushExtension(sendbuf, Ztls_ExtTypeKeyShare, {
	  Ztls_BufferPushBlock(sendbuf, 2, {
	    if (keyShareCtx != NULL &&
	      (ret = push_key_share_entry(sendbuf, keyShareCtx->algo->id, keyShareCtx->pubkey)) !=
		0)
	      goto Exit;
	  });
	});
      }
      if (sni_name.data() != NULL) {
	Ztls_BufferPushExtension(sendbuf, Ztls_ExtTypeSrvName, {
	  if ((ret = emit_server_name_extension(sendbuf, sni_name)) != 0) goto Exit;
	});
      }
      if (properties != NULL && properties->client.negotiatedProtocols.length() != 0) {
	Ztls_BufferPushExtension(sendbuf, Ztls_ExtTypeALPN, {
	  Ztls_BufferPushBlock(sendbuf, 2, {
	    for (auto p : properties->client.negotiatedProtocols) {
	      Ztls_BufferPushBlock(
		sendbuf, 1, { Ztls_BufferPushv(sendbuf, p.data(), p.length()); });
	    }
	  });
	});
      }
      if (ctx->decompressCertificate != NULL) {
	Ztls_BufferPushExtension(sendbuf, Ztls_ExtTypeCompressCert, {
	  Ztls_BufferPushBlock(sendbuf, 1, {
	    assert(ctx->decompressCertificate->supportedAlgorithms.length());
	    for (auto algo : ctx->decompressCertificate->supportedAlgorithms)
	      Ztls_BufferPush16(sendbuf, algo);
	  });
	});
      }
      Ztls_BufferPushExtension(sendbuf, Ztls_ExtVersions, {
	Ztls_BufferPushBlock(sendbuf, 1, {
	  size_t i;
	  for (i = 0; i != ZuTraits<decltype(supported_versions)>::length(supported_versions);
	    ++i)
	    Ztls_BufferPush16(sendbuf, supported_versions[i]);
	});
      });
      Ztls_BufferPushExtension(sendbuf, Ztls_ExtTypeSigAlgos, {
	if ((ret = push_signature_algorithms(ctx->verifyCertificate, sendbuf)) != 0) goto Exit;
      });
      if (ctx->keyExchanges != NULL) {
	Ztls_BufferPushExtension(sendbuf, Ztls_ExtGroups, {
	  const KeyExchangeAlgo **algo = ctx->keyExchanges;
	  Ztls_BufferPushBlock(sendbuf, 2, {
	    for (; *algo != NULL; ++algo) Ztls_BufferPush16(sendbuf, (*algo)->id);
	  });
	});
      }
      if (cookie != NULL && cookie->data() != NULL) {
	Ztls_BufferPushExtension(sendbuf, Ztls_ExtTypeCookie, {
	  Ztls_BufferPushBlock(
	    sendbuf, 2, { Ztls_BufferPushv(sendbuf, cookie->data(), cookie->length()); });
	});
      }
      if (ctx->useRawPublicKeys) {
	Ztls_BufferPushExtension(sendbuf, Ztls_ExtTypeSrvCertType, {
	  Ztls_BufferPushBlock(
	    sendbuf, 1, { Ztls_BufferPush_1(sendbuf, Ztls_CertTypeRawPubkey); });
	});
      }
      if (ctx->saveTicket != NULL &&
	(ctx->ticketRequests.newSessionCount != 0 || ctx->ticketRequests.resumptionCount != 0)) {
	Ztls_BufferPushExtension(sendbuf, Ztls_ExtTypeTicketRequest, {
	  Ztls_BufferPush_1(
	    sendbuf, ctx->ticketRequests.newSessionCount, ctx->ticketRequests.resumptionCount);
	});
      }
      if ((ret = push_additional_extensions(properties, sendbuf)) != 0) goto Exit;
      if (ctx->saveTicket != NULL || psk_secret.data() != NULL) {
	Ztls_BufferPushExtension(sendbuf, Ztls_ExtPSKKexModes, {
	  Ztls_BufferPushBlock(sendbuf, 1, {
	    if (!ctx->requireDheOnPsk) Ztls_BufferPush_1(sendbuf, Ztls_PSKKeModePSK);
	    Ztls_BufferPush_1(sendbuf, Ztls_PSKKeModePSKDHE);
	  });
	});
      }
      if (psk_secret.data() != NULL) {
	if (usingEarlyData && !is_second_flight)
	  Ztls_BufferPushExtension(sendbuf, Ztls_ExtTypeEarly, {});
	// pre-shared key "MUST be the last extension in the ClientHello" (draft-17 section 4.2.6)
	Ztls_BufferPushExtension(sendbuf, Ztls_ExtTypePreSharedKey, {
	  Ztls_BufferPushBlock(sendbuf, 2, {
	    Ztls_BufferPushBlock(sendbuf, 2, {
	      if (mode == EncodeCHMode::Outer && ech->state != Ztls_::EchState::Grease) {
		if ((ret = buffer_reserve(sendbuf, psk_identity.length(), 1)) != 0) goto Exit;
		ctx->random->fill(sendbuf->base + sendbuf->off, psk_identity.length());
		sendbuf->off += psk_identity.length();
	      } else {
		Ztls_BufferPushv(sendbuf, psk_identity.data(), psk_identity.length());
	      }
	    });
	    uint32_t age;
	    if (mode == EncodeCHMode::Outer && ech->state != Ztls_::EchState::Grease) {
	      ctx->random->fill(&age, sizeof(age));
	    } else {
	      age = obfuscatedTicketAge;
	    }
	    Ztls_BufferPush32(sendbuf, age);
	  });
	  // allocate space for PSK binder. The space is filled initially filled by a random value
	  // (meeting the requirement of ClientHelloOuter), and later gets filled with the correct
	  // binder value if necessary.
	  Ztls_BufferPushBlock(sendbuf, 2, {
	    Ztls_BufferPushBlock(sendbuf, 1, {
	      if ((ret = buffer_reserve(sendbuf, psk_binder_size, 1)) != 0) goto Exit;
	      ctx->random->fill(sendbuf->base + sendbuf->off, psk_binder_size);
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

// Feeds the CH message into the hash, computing the PSK binder if necessary. `binder_key` must be
// derived before calling this function.
static int update_ch_hash_and_binder(
  KeySchedule *ks, uint8_t *ch, size_t ch_start, size_t ch_end, int has_psk, uint8_t *binder_key,
  int is_outer)
{
  int	 ret = 0;
  size_t hash_off = ch_start;

  if (has_psk) {
    size_t psk_binder_off = ch_end - (3 + ks->hashes[0].algo->digestSize);
    _key_schedule_update_hash(ks, ch + hash_off, psk_binder_off - hash_off, is_outer);
    hash_off = psk_binder_off;
    if ((ret = calc_verify_data(ch + psk_binder_off + 3, ks, binder_key)) != 0) return ret;
  }
  _key_schedule_update_hash(ks, ch + hash_off, ch_end - hash_off, is_outer);

  return ret;
}

static int send_client_hello(
  Ztls::Cxn *tls, MessageEmitter *emitter, HandshakeProps *properties, Bytes *cookie)
{
  struct Psk {
    Bytes	secret;
    Bytes	identity;
    const char	*label;
  } psk{};
  uint32_t obfuscatedTicketAge = 0;
  ZuCSpan  sni_name;
  size_t   mess_start;
  uint8_t  binder_key[Ztls_MaxDigestSize];
  Buffer   encodedChInner;
  int	   ret, is_second_flight = tls->keySchedule != NULL;

  buffer_init_tx(&encodedChInner, const_cast<char *>(""), 0);

  if (tls->serverName.data() != NULL && !server_name_is_ipaddr(tls->serverName.data()))
    sni_name = {tls->serverName.data(), tls->serverName.length() - 1};

  // try to use ECH (ignore broken ECHConfigList; it is delivered insecurely)
  if (properties != NULL) {
    if (!is_second_flight && sni_name.data() != NULL && tls->ctx->ech.client.ciphers != NULL) {
      if (properties->client.ech.configs.length() != 0) {
	DecodedEchConfig decoded;
	cli_decode_ech_config_list(tls->ctx, &decoded, properties->client.ech.configs);
	if (decoded.kem != NULL && decoded.cipher != NULL) {
	  if ((ret = client_setup_ech(&tls->ech, &decoded, tls->ctx->random)) != 0) goto Exit;
	}
      } else if (properties->client.ech.configs.data() != NULL) {
	// zero-length config with non-NULL base indicates ECH greasing; NULL base means no ECH
	client_setup_ech_grease(
	  &tls->ech, tls->ctx->random, tls->ctx->ech.client.kems, tls->ctx->ech.client.ciphers,
	  sni_name);
	tls->ech.state = Ztls_::EchState::Grease;
      }
    }
  }

  // use external PSK if provided
  if (tls->ctx->preSharedKey.identity.data() != NULL) {
    if (!is_second_flight) {
      tls->offeredPsk = 1;
      for (size_t i = 0; i < tls->ctx->cipherSuites.length(); ++i) {
	if (tls->ctx->cipherSuites[i]->hash == tls->ctx->preSharedKey.hash) {
	  tls->cipherSuite = tls->ctx->cipherSuites[i];
	  break;
	}
      }
      assert(
	tls->cipherSuite != NULL && "no compatible cipher-suite provided that matches psk.hash");
      if (properties != NULL && properties->client.maxEarlyDataSize != NULL) {
	tls->usingEarlyData = 1;
	*properties->client.maxEarlyDataSize = SIZE_MAX;
      }
    } else {
      assert(tls->cipherSuite != NULL && tls->cipherSuite->hash == tls->ctx->preSharedKey.hash);
    }
    psk.secret = tls->ctx->preSharedKey.secret;
    psk.identity = tls->ctx->preSharedKey.identity;
    psk.label = "ext binder";
  }

  // try to setup resumption-related data, unless external PSK is used
  if (psk.secret.data() == NULL && properties != NULL &&
    properties->client.sessionTicket.data() != NULL && tls->ctx->keyExchanges != NULL) {
    const KeyExchangeAlgo *keyShare = NULL;
    const CipherSuite	  *cipherSuite = NULL;
    uint32_t		   maxEarlyDataSize;
    if (decode_stored_session_ticket(
	  tls, &keyShare, &cipherSuite, &psk.secret, &obfuscatedTicketAge, &psk.identity,
	  &maxEarlyDataSize, properties->client.sessionTicket.data(),
	  properties->client.sessionTicket.data() + properties->client.sessionTicket.length()) ==
      0) {
      psk.label = "res binder";
      tls->offeredPsk = 1;
      // key-share selected by HRR should not be overridden
      if (tls->keyShare == NULL) tls->keyShare = keyShare;
      tls->cipherSuite = cipherSuite;
      if (!is_second_flight && maxEarlyDataSize != 0 &&
	properties->client.maxEarlyDataSize != NULL) {
	tls->usingEarlyData = 1;
	*properties->client.maxEarlyDataSize = maxEarlyDataSize;
      }
    } else {
      psk.secret = wire_bytes(NULL, 0);
    }
  }

  // send 0-RTT related signals back to the client
  if (properties != NULL) {
    if (tls->usingEarlyData) {
      properties->client.earlyDataAcceptance = EarlyDataAcceptance::Unknown;
    } else {
      if (properties->client.maxEarlyDataSize != NULL) *properties->client.maxEarlyDataSize = 0;
      properties->client.earlyDataAcceptance = EarlyDataAcceptance::Rejected;
    }
  }

  // use the default key share if still not undetermined
  if (tls->keyShare == NULL && tls->ctx->keyExchanges != NULL &&
    !(properties != NULL && properties->client.negotiatePreKeyex))
    tls->keyShare = tls->ctx->keyExchanges[0];

  // instantiate key share context
  assert(tls->keyShareCtx == NULL);
  if (tls->keyShare != NULL) {
    if ((ret = tls->keyShare->create(&tls->keyShareCtx)) != 0) goto Exit;
  }

  // initialize key schedule
  if (!is_second_flight) {
    if ((tls->keySchedule = key_schedule_new(
	   tls->cipherSuite, tls->ctx->cipherSuites, tls->ech.aead != NULL)) == NULL) {
      ret = Ztls_ErrorNoMemory;
      goto Exit;
    }
    if ((ret = key_schedule_extract(tls->keySchedule, psk.secret)) != 0) goto Exit;
  }

  // start generating CH
  if ((ret = emitter->beginMessage()) != 0) goto Exit;
  mess_start = emitter->buf->off;

  // generate true (inner) CH
  if ((ret = encode_client_hello(
	 tls->ctx, emitter->buf, EncodeCHMode::Inner, is_second_flight, properties,
	 tls->ech.aead != NULL ? tls->ech.innerClientRandom : tls->clientRandom, tls->keyShareCtx,
	 sni_name, tls->legacySessionId, &tls->ech, NULL,
	 wire_bytes(tls->ech.client.firstEch.data(), tls->ech.client.firstEch.length()),
	 psk.secret, psk.identity, obfuscatedTicketAge,
	 tls->keySchedule->hashes[0].algo->digestSize, cookie, tls->usingEarlyData)) != 0)
    goto Exit;

  // update the message hash, filling in the PSK binder HMAC if necessary
  if (psk.secret.data() != NULL) {
    if ((ret = derive_empty_secret(tls->keySchedule, binder_key, psk.label)) != 0) goto Exit;
  }
  if ((ret = update_ch_hash_and_binder(
	 tls->keySchedule, emitter->buf->base, mess_start, emitter->buf->off,
	 psk.secret.data() != NULL, binder_key, 0)) != 0)
    goto Exit;

  // ECH
  if (tls->ech.aead != NULL) {
    // build EncodedCHInner
    if ((ret = encode_client_hello(
	   tls->ctx, &encodedChInner, EncodeCHMode::EncodedInner, is_second_flight, properties,
	   tls->ech.innerClientRandom, tls->keyShareCtx, sni_name, tls->legacySessionId,
	   &tls->ech, NULL, wire_bytes(NULL, 0), psk.secret, psk.identity, obfuscatedTicketAge,
	   tls->keySchedule->hashes[0].algo->digestSize, cookie, tls->usingEarlyData)) != 0)
      goto Exit;
    if (psk.secret.data() != NULL)
      memcpy(
	encodedChInner.base + encodedChInner.off - tls->keySchedule->hashes[0].algo->digestSize,
	emitter->buf->base + emitter->buf->off - tls->keySchedule->hashes[0].algo->digestSize,
	tls->keySchedule->hashes[0].algo->digestSize);
    {
      // pad EncodedCHInner (following draft-ietf-tls-esni-15 6.1.3)
      size_t padding_len;
      if (sni_name.data() != NULL) {
	padding_len = sni_name.length();
	if (padding_len < tls->ech.client.maxNameLength)
	  padding_len = tls->ech.client.maxNameLength;
      } else {
	padding_len = tls->ech.client.maxNameLength + 9;
      }
      size_t final_len = encodedChInner.off - Ztls_HSHeaderSize + padding_len;
      final_len = (final_len + 31)/32 * 32;
      padding_len = final_len - (encodedChInner.off - Ztls_HSHeaderSize);
      if (padding_len != 0) {
	if ((ret = buffer_reserve(&encodedChInner, padding_len, 1)) != 0) goto Exit;
	memset(encodedChInner.base + encodedChInner.off, 0, padding_len);
	encodedChInner.off += padding_len;
      }
    }
    // flush CHInner, build CHOuterAAD
    emitter->buf->off = mess_start;
    size_t ech_payload_size =
	     encodedChInner.off - Ztls_HSHeaderSize + tls->ech.aead->algo->tagSize,
	   ech_size_offset = ech_payload_size;
    if ((ret = encode_client_hello(
	   tls->ctx, emitter->buf, EncodeCHMode::Outer, is_second_flight, properties,
	   tls->clientRandom, tls->keyShareCtx,
	   {tls->ech.client.publicName.data(), tls->ech.client.publicName.length() - 1},
	   tls->legacySessionId, &tls->ech, &ech_size_offset, wire_bytes(NULL, 0), psk.secret,
	   psk.identity, obfuscatedTicketAge, tls->keySchedule->hashes[0].algo->digestSize,
	   cookie, tls->usingEarlyData)) != 0)
      goto Exit;
    // overwrite ECH payload
    aead_encrypt(
      tls->ech.aead, emitter->buf->base + ech_size_offset,
      encodedChInner.base + Ztls_HSHeaderSize, encodedChInner.off - Ztls_HSHeaderSize,
      is_second_flight, emitter->buf->base + mess_start + Ztls_HSHeaderSize,
      emitter->buf->off - (mess_start + Ztls_HSHeaderSize));
    // keep the copy of the 1st ECH extension so that we can send it again in 2nd CH in response
    // to rejection with HRR
    if (!is_second_flight) {
      size_t len = outer_ech_header_size(tls->ech.client.enc.length()) + ech_payload_size;
      tls->ech.client.firstEch.copy(
	emitter->buf->base + ech_size_offset -
	  outer_ech_header_size(tls->ech.client.enc.length()),
	len);
      if (tls->ech.state != Ztls_::EchState::Grease) tls->ech.state = Ztls_::EchState::Offered;
    }
    if (tls->ech.state == Ztls_::EchState::Grease) {
      // For grease ECH, the server sees the outer CH. Discard the inner state and adopt the
      // outer, then compute the PSK binder over the outer CH using the standard flow.
      for (size_t i = 0; i < tls->keySchedule->numHashes; ++i) {
	delete tls->keySchedule->hashes[i].ctx;
	tls->keySchedule->hashes[i].ctx = tls->keySchedule->hashes[i].ctxOuter;
	tls->keySchedule->hashes[i].ctxOuter = NULL;
      }
      aead_free(tls->ech.aead);
      tls->ech.aead = NULL;
      if ((ret = update_ch_hash_and_binder(
	     tls->keySchedule, emitter->buf->base, mess_start, emitter->buf->off,
	     psk.secret.data() != NULL, binder_key, 0)) != 0)
	goto Exit;
    } else {
      // update outer hash
      _key_schedule_update_hash(
	tls->keySchedule, emitter->buf->base + mess_start, emitter->buf->off - mess_start, 1);
    }
  }

  // commit CH to the record layer
  if ((ret = emitter->commitMessage()) != 0) goto Exit;

  if (tls->usingEarlyData) {
    assert(!is_second_flight);
    if ((ret = setup_traffic_protection(tls, 1, "c e traffic", 1, 0, 0)) != 0) goto Exit;
    if ((ret = push_change_cipher_spec(tls, emitter)) != 0) goto Exit;
  }
  if (psk.secret.data() != NULL && !is_second_flight) {
    if ((ret = derive_exporter_secret(tls, 1)) != 0) goto Exit;
  }
  tls->state = cookie == NULL ? Ztls_::State::CliWaitSrvHello : Ztls_::State::CliWait2ndSrvHello;
  ret = Ztls_ErrorInProgress;

Exit:
  buffer_dispose(&encodedChInner);
  ZuClear(binder_key, sizeof(binder_key));
  return ret;
}

const CipherSuite *find_cipher_suite(SuiteSpan suites, uint16_t id)
{
  for (auto suite : suites)
    if (suite->id == id) return suite;
  return nullptr;
}

static int
decode_server_hello(Ztls::Cxn *tls, ServerHello *sh, const uint8_t *src, const uint8_t *const end)
{
  uint16_t exttype, found_version = UINT16_MAX, selected_psk_identity = UINT16_MAX;
  int	   ret;

  *sh = (ServerHello){{0}};

  // ignore legacy-version
  if (end - src < 2) {
    ret = Ztls_AlertDecodeError;
    goto Exit;
  }
  src += 2;

  // random
  if (end - src < Ztls_HelloRandomSize) {
    ret = Ztls_AlertDecodeError;
    goto Exit;
  }
  sh->isRetryRequest = memcmp(src, hello_retry_random, Ztls_HelloRandomSize) == 0;
  src += Ztls_HelloRandomSize;

  // legacySessionId
  Ztls_DecodeOpenBlock(src, end, 1, {
    if (end - src > 32) {
      ret = Ztls_AlertDecodeError;
      goto Exit;
    }
    sh->legacySessionId = wire_bytes(src, end - src);
    src = end;
  });

  {
    // select cipherSuite
    uint16_t csid;
    if ((ret = decode16(&csid, &src, end)) != 0) goto Exit;
    if ((tls->cipherSuite = find_cipher_suite(tls->ctx->cipherSuites, csid)) == NULL) {
      ret = Ztls_AlertIllegalParameter;
      goto Exit;
    }
  }

  {
    // legacy_compression_method
    uint8_t method;
    if ((ret = decode8(&method, &src, end)) != 0) goto Exit;
    if (method != 0) {
      ret = Ztls_AlertIllegalParameter;
      goto Exit;
    }
  }

  if (sh->isRetryRequest) sh->retryRequest.selectedGroup = UINT16_MAX;

  Ztls_DecodeExtensions(
    src, end, sh->isRetryRequest ? Ztls_HsTypePseudoHRR : Ztls_HsTypeSrvHello, &exttype, {
      if (tls->ctx->onExtension != NULL &&
	(ret = tls->ctx->onExtension->call(
		 tls, Ztls_HsTypeSrvHello, exttype, wire_bytes(src, end - src)) != 0))
	goto Exit;
      switch (exttype) {
      case Ztls_ExtVersions:
	if ((ret = decode16(&found_version, &src, end)) != 0) goto Exit;
	break;
      case Ztls_ExtTypeKeyShare:
	if (tls->ctx->keyExchanges == NULL) {
	  ret = Ztls_AlertHSFailure;
	  goto Exit;
	}
	if (sh->isRetryRequest) {
	  if ((ret = decode16(&sh->retryRequest.selectedGroup, &src, end)) != 0) goto Exit;
	} else {
	  uint16_t group;
	  if ((ret = decode_key_share_entry(&group, &sh->peerkey, &src, end)) != 0) goto Exit;
	  if (src != end) {
	    ret = Ztls_AlertDecodeError;
	    goto Exit;
	  }
	  if (tls->keyShare == NULL || tls->keyShare->id != group) {
	    ret = Ztls_AlertIllegalParameter;
	    goto Exit;
	  }
	}
	break;
      case Ztls_ExtTypeCookie:
	assert(sh->isRetryRequest);
	Ztls_DecodeBlock(src, end, 2, {
	  if (src == end) {
	    ret = Ztls_AlertDecodeError;
	    goto Exit;
	  }
	  sh->retryRequest.cookie = wire_bytes(src, end - src);
	  src = end;
	});
	break;
      case Ztls_ExtTypePreSharedKey:
	assert(!sh->isRetryRequest);
	if ((ret = decode16(&selected_psk_identity, &src, end)) != 0) goto Exit;
	break;
      case Ztls_ExtTypeEncCliHello:
	assert(sh->isRetryRequest);
	if (tls->ech.state == Ztls_::EchState::None) {
	  ret = Ztls_AlertUnsupportedExt;
	  goto Exit;
	}
	if (end - src != Ztls_ECHConfirmLength) {
	  ret = Ztls_AlertDecodeError;
	  goto Exit;
	}
	sh->retryRequest.ech = src;
	src = end;
	break;
      default:
	src = end;
	break;
      }
    });

  if (!is_supported_version(found_version)) {
    ret = Ztls_AlertIllegalParameter;
    goto Exit;
  }
  if (!sh->isRetryRequest) {
    if (selected_psk_identity != UINT16_MAX) {
      if (!tls->offeredPsk) {
	ret = Ztls_AlertIllegalParameter;
	goto Exit;
      }
      if (selected_psk_identity != 0) {
	ret = Ztls_AlertIllegalParameter;
	goto Exit;
      }
      tls->isPskHandshake = 1;
    }
    if (sh->peerkey.data() == NULL && !tls->isPskHandshake) {
      ret = Ztls_AlertIllegalParameter;
      goto Exit;
    }
  }

  ret = 0;
Exit:
  return ret;
}

static int handle_hello_retry_request(
  Ztls::Cxn *tls, MessageEmitter *emitter, ServerHello *sh, Bytes message,
  HandshakeProps *properties)
{
  int ret;

  if (tls->keyShareCtx != NULL) {
    delete tls->keyShareCtx;
    tls->keyShareCtx = NULL;
  }
  if (tls->usingEarlyData) {
    // release traffic encryption key so that 2nd CH goes out in cleartext, but keep the epoch at
    // 1 since we've already called derive-secret
    if (tls->ctx->updateTrafficKey == NULL) {
      assert(tls->txTraffic.aead != NULL);
      aead_free(tls->txTraffic.aead);
      tls->txTraffic.aead = NULL;
    }
    tls->usingEarlyData = 0;
  }

  if (sh->retryRequest.selectedGroup != UINT16_MAX) {
    // we offer the first keyExchanges[0] as KEY_SHARE unless client.negotiatePreKeyex is set
    const KeyExchangeAlgo **cand;
    for (cand = tls->ctx->keyExchanges; *cand != NULL; ++cand)
      if ((*cand)->id == sh->retryRequest.selectedGroup) break;
    if (*cand == NULL) {
      ret = Ztls_AlertIllegalParameter;
      goto Exit;
    }
    tls->keyShare = *cand;
  } else if (tls->keyShare != NULL) {
    // retain the key-share using in first CH, if server does not specify one
  } else {
    ret = Ztls_AlertIllegalParameter;
    goto Exit;
  }

  ret = send_client_hello(tls, emitter, properties, &sh->retryRequest.cookie);

Exit:
  return ret;
}

static int
client_ech_select_hello(Ztls::Cxn *tls, Bytes message, size_t confirm_hash_off, const char *label)
{
  uint8_t confirm_hash_delivered[Ztls_ECHConfirmLength],
    confirm_hash_expected[Ztls_ECHConfirmLength];
  int ret = 0;

  // Determine if ECH has been accepted by checking the confirmation hash. `confirm_hash_off` set
  // to zero indicates that HRR was received wo. ECH extension, which is an indication that ECH
  // was rejected.
  if (confirm_hash_off != 0) {
    memcpy(
      confirm_hash_delivered, message.data() + confirm_hash_off, sizeof(confirm_hash_delivered));
    memset(message.data() + confirm_hash_off, 0, sizeof(confirm_hash_delivered));
    if ((ret = ech_calc_confirmation(
	   tls->keySchedule, confirm_hash_expected, tls->ech.innerClientRandom, label,
	   message)) != 0)
      goto Exit;
    int accepted = Ztls::ctEqual(
      {confirm_hash_delivered, sizeof(confirm_hash_delivered)},
      {confirm_hash_expected, sizeof(confirm_hash_delivered)});
    memcpy(
      message.data() + confirm_hash_off, confirm_hash_delivered, sizeof(confirm_hash_delivered));
    if (accepted) {
      tls->ech.state = Ztls_::EchState::Accepted;
      goto Exit;
    } else if (tls->ech.state == Ztls_::EchState::Accepted) {
      // Per RFC 9849 Section 6.1.5: if HRR confirmed ECH acceptance, ServerHello MUST also
      // confirm it.
      ret = Ztls_AlertIllegalParameter;
      goto Exit;
    }
  }

  // dispose ECH AEAD state to indicate rejection, adopting outer CH for the rest of the handshake
  aead_free(tls->ech.aead);
  tls->ech.aead = NULL;
  key_schedule_select_outer(tls->keySchedule);

Exit:
  ZuClear(confirm_hash_expected, sizeof(confirm_hash_expected));
  return ret;
}

static int client_handle_hello(
  Ztls::Cxn *tls, MessageEmitter *emitter, Bytes message, HandshakeProps *properties)
{
  ServerHello	   sh;
  KeyExchangeBytes ecdh_secret;
  int		   ret;

  if ((ret = decode_server_hello(
	 tls, &sh, message.data() + Ztls_HSHeaderSize, message.data() + message.length())) != 0)
    goto Exit;
  if (!(sh.legacySessionId.length() == tls->legacySessionId.length() &&
	Ztls::ctEqual(
	  {sh.legacySessionId.data(), tls->legacySessionId.length()},
	  {tls->legacySessionId.data(), tls->legacySessionId.length()}))) {
    ret = Ztls_AlertIllegalParameter;
    goto Exit;
  }

  if (sh.isRetryRequest) {
    if ((ret = key_schedule_select_cipher(
	   tls->keySchedule, tls->cipherSuite, 0, tls->ctx->preSharedKey.secret)) != 0)
      goto Exit;
    key_sched_post_ch1hash(tls->keySchedule);
    if (tls->ech.aead != NULL) {
      size_t confirm_hash_off = 0;
      if (tls->ech.state != Ztls_::EchState::Grease) {
	if (sh.retryRequest.ech != NULL) confirm_hash_off = sh.retryRequest.ech - message.data();
      }
      if ((ret = client_ech_select_hello(
	     tls, message, confirm_hash_off, Ztls_ECHConfirmationHRR)) != 0)
	goto Exit;
    }
    _key_schedule_update_hash(tls->keySchedule, message.data(), message.length(), 0);
    return handle_hello_retry_request(tls, emitter, &sh, message, properties);
  }

  if ((ret = key_schedule_select_cipher(
	 tls->keySchedule, tls->cipherSuite, tls->offeredPsk && !tls->isPskHandshake,
	 wire_bytes(NULL, 0))) != 0)
    goto Exit;

  // check if ECH is accepted
  if (tls->ech.aead != NULL) {
    size_t confirm_hash_off = 0;
    if (tls->ech.state != Ztls_::EchState::Grease) {
      confirm_hash_off = Ztls_HSHeaderSize +
	2
	// legacyVersion
	+ Ztls_HelloRandomSize - Ztls_ECHConfirmLength;
    }
    if ((ret = client_ech_select_hello(
	   tls, message, confirm_hash_off, Ztls_ECHConfirmationSrvHello)) != 0)
      goto Exit;
  }

  // clear sensitive and space-consuming ECH state, now that are done with handling sending and
  // decoding Hellos
  clear_ech(&tls->ech, 0);
  if (tls->keySchedule->hashes[0].ctxOuter != NULL) {
    delete tls->keySchedule->hashes[0].ctxOuter;
    tls->keySchedule->hashes[0].ctxOuter = NULL;
  }

  // if the client offered external PSK but the server did not use that, we call it a handshake
  // failure
  if (tls->ctx->preSharedKey.identity.data() != NULL && !tls->isPskHandshake) {
    ret = Ztls_AlertHSFailure;
    goto Exit;
  }

  _key_schedule_update_hash(tls->keySchedule, message.data(), message.length(), 0);

  if (sh.peerkey.data() != NULL) {
    ret = tls->keyShareCtx->exchange(&ecdh_secret, sh.peerkey);
    delete tls->keyShareCtx;
    tls->keyShareCtx = nullptr;
    if (ret != 0) {
      assert(!ecdh_secret.length());
      goto Exit;
    }
  }

  if ((ret = key_schedule_extract(
	 tls->keySchedule, wire_bytes(ecdh_secret.data(), ecdh_secret.length()))) != 0)
    goto Exit;
  if ((ret = setup_traffic_protection(tls, 0, "s hs traffic", 2, 0, 0)) != 0) goto Exit;
  if (tls->usingEarlyData) {
    tls->pendingHandshakeSecret.length(Ztls_MaxDigestSize);
    if ((ret = derive_secret(
	   tls->keySchedule, tls->pendingHandshakeSecret.data(), "c hs traffic")) != 0)
      goto Exit;
    if (tls->ctx->updateTrafficKey != NULL &&
      (ret = tls->ctx->updateTrafficKey->call(tls, 1, 2, tls->pendingHandshakeSecret.data())) !=
	0)
      goto Exit;
  } else {
    if ((ret = setup_traffic_protection(tls, 1, "c hs traffic", 2, 0, 0)) != 0) goto Exit;
  }

  tls->state = Ztls_::State::CliWaitEncExts;
  ret = Ztls_ErrorInProgress;

Exit:
  return ret;
}

static int collect_unknown_ext(Ztls::Cxn *tls, HandshakeProps *properties, uint16_t type)
{
  return properties != NULL && properties->extensions != NULL &&
    properties->extensions->collect(tls, type);
}

static int collect_unknown_extension(
  Ztls::Cxn *tls, uint16_t type, const uint8_t *src, const uint8_t *const end,
  RawExtension *slots)
{
  size_t i;
  for (i = 0; slots[i].type != UINT16_MAX; ++i) {
    assert(i < ZtlsMaxUnknownExts);
    if (slots[i].type == type) return Ztls_AlertIllegalParameter;
  }
  if (i < ZtlsMaxUnknownExts) {
    slots[i].type = type;
    slots[i].data = wire_bytes(src, end - src);
    slots[i + 1].type = UINT16_MAX;
  }
  return 0;
}

static int
report_unknown_extensions(Ztls::Cxn *tls, HandshakeProps *properties, RawExtension *slots)
{
  if (properties != NULL && properties->extensions != NULL) {
    return properties->extensions->collected(tls, slots);
  } else {
    return 0;
  }
}

static int cli_handle_enc_exts(Ztls::Cxn *tls, Bytes message, HandshakeProps *properties)
{
  const uint8_t	      *src = message.data() + Ztls_HSHeaderSize,
		*const end = message.data() + message.length();
  uint16_t	       type;
  auto		       extensions =
    ZtScratch(CollectedExtensions, ZtlsMaxUnknownExts + 1, ZtlsMaxUnknownExts + 1);
  auto unknownExtensions = extensions.data();
  unknownExtensions[0] = {UINT16_MAX};
  int	  ret, skip_early_data = 1;
  uint8_t server_offered_cert_type = Ztls_CertTypeX509;

  Ztls_DecodeExtensions(src, end, Ztls_HsTypeEncExts, &type, {
    if (tls->ctx->onExtension != NULL &&
      (ret = tls->ctx->onExtension->call(
	       tls, Ztls_HsTypeEncExts, type, wire_bytes(src, end - src)) != 0))
      goto Exit;
    switch (type) {
    case Ztls_ExtTypeSrvName:
      if (src != end) {
	ret = Ztls_AlertDecodeError;
	goto Exit;
      }
      if (!(tls->serverName.data() != NULL && !server_name_is_ipaddr(tls->serverName.data()))) {
	ret = Ztls_AlertIllegalParameter;
	goto Exit;
      }
      break;
    case Ztls_ExtTypeALPN:
      Ztls_DecodeBlock(src, end, 2, {
	Ztls_DecodeOpenBlock(src, end, 1, {
	  if (src == end) {
	    ret = Ztls_AlertDecodeError;
	    goto Exit;
	  }
	  if ((ret = set_negotiated_protocol(
		 tls, {reinterpret_cast<const char *>(src), size_t(end - src)})) != 0)
	    goto Exit;
	  src = end;
	});
	if (src != end) {
	  ret = Ztls_AlertHSFailure;
	  goto Exit;
	}
      });
      break;
    case Ztls_ExtTypeEarly:
      if (!tls->usingEarlyData) {
	ret = Ztls_AlertIllegalParameter;
	goto Exit;
      }
      skip_early_data = 0;
      break;
    case Ztls_ExtTypeSrvCertType:
      if (end - src != 1) {
	ret = Ztls_AlertDecodeError;
	goto Exit;
      }
      server_offered_cert_type = *src;
      src = end;
      break;
    case Ztls_ExtTypeEncCliHello: {
      // accept retryConfigs only if we offered ECH (or grease) but rejected
      if (!(tls->ech.state == Ztls_::EchState::Offered ||
	    tls->ech.state == Ztls_::EchState::Grease)) {
	ret = Ztls_AlertUnsupportedExt;
	goto Exit;
      }
      // parse retry_config, and if it is applicable, provide that to the application (grease
      // clients just verify syntax)
      DecodedEchConfig decoded;
      if ((ret = cli_decode_ech_config_list(tls->ctx, &decoded, wire_bytes(src, end - src))) != 0)
	goto Exit;
      if (tls->ech.state == Ztls_::EchState::Grease) {
	// GREASE clients ignore retryConfigs after verifying the syntax
      } else if (decoded.kem != NULL && decoded.cipher != NULL && properties != NULL &&
	properties->client.ech.retryConfigs != NULL) {
	properties->client.ech.retryConfigs->copy(src, end - src);
      }
      src = end;
    } break;
    default:
      if (collect_unknown_ext(tls, properties, type)) {
	if ((ret = collect_unknown_extension(tls, type, src, end, unknownExtensions)) != 0)
	  goto Exit;
      }
      break;
    }
    src = end;
  });

  if (server_offered_cert_type !=
    (tls->ctx->useRawPublicKeys ? Ztls_CertTypeRawPubkey : Ztls_CertTypeX509)) {
    ret = Ztls_AlertUnsupportedCert;
    goto Exit;
  }

  if (tls->usingEarlyData) {
    if (skip_early_data) tls->usingEarlyData = 0;
    if (properties != NULL)
      properties->client.earlyDataAcceptance = skip_early_data ? EarlyDataAcceptance::Rejected :
								 EarlyDataAcceptance::Accepted;
  }
  if ((ret = report_unknown_extensions(tls, properties, unknownExtensions)) != 0) goto Exit;

  _key_schedule_update_hash(tls->keySchedule, message.data(), message.length(), 0);
  tls->state = tls->isPskHandshake ? Ztls_::State::CliWaitFinished : Ztls_::State::CliWaitCertReq;
  ret = Ztls_ErrorInProgress;

Exit:
  return ret;
}

static int decode_certificate_request(
  Ztls::Cxn *tls, CertificateRequest *cr, const uint8_t *src, const uint8_t *const end)
{
  int	   ret;
  uint16_t exttype = 0;

  // certificate request context
  Ztls_DecodeOpenBlock(src, end, 1, {
    size_t len = end - src;
    if (len > 255) {
      ret = Ztls_AlertDecodeError;
      goto Exit;
    }
    auto &context = tls->certificateRequestBytes;
    // Empty context still denotes a pending request.
    context.length(len ? len : 1);
    if (len) memcpy(context.data(), src, len);
    cr->context = wire_bytes(context.data(), len);
    src = end;
  });

  // decode extensions
  Ztls_DecodeExtensions(src, end, Ztls_HsTypeCertRequest, &exttype, {
    if (tls->ctx->onExtension != NULL &&
      (ret = tls->ctx->onExtension->call(
	       tls, Ztls_HsTypeCertRequest, exttype, wire_bytes(src, end - src)) != 0))
      goto Exit;
    switch (exttype) {
    case Ztls_ExtTypeSigAlgos:
      if ((ret = decode_signature_algorithms(&cr->signatureAlgorithms, &src, end)) != 0)
	goto Exit;
      break;
    }
    src = end;
  });

  if (cr->signatureAlgorithms.length() == 0) {
    ret = Ztls_AlertMissingExt;
    goto Exit;
  }

  ret = 0;
Exit:
  return ret;
}

template <typename Certificates>
static int build_certificate_message_(
  Buffer *buf, Bytes context, const Certificates &certificates, Bytes ocsp_status)
{
  int ret;

  Ztls_BufferPushBlock(buf, 1, { Ztls_BufferPushv(buf, context.data(), context.length()); });
  Ztls_BufferPushBlock(buf, 3, {
    for (size_t i = 0; i != certificates.length(); ++i) {
      const auto &cert = certificates[i];
      Ztls_BufferPushBlock(buf, 3, { Ztls_BufferPushv(buf, cert.data(), cert.length()); });
      Ztls_BufferPushBlock(buf, 2, {
	if (i == 0 && ocsp_status.length() != 0) {
	  Ztls_BufferPushExtension(buf, Ztls_ExtTypeStatusRequest, {
	    Ztls_BufferPush_1(buf, 1);
	    // status_type == ocsp
	    Ztls_BufferPushBlock(
	      buf, 3, { Ztls_BufferPushv(buf, ocsp_status.data(), ocsp_status.length()); });
	  });
	}
      });
    }
  });

  ret = 0;
Exit:
  return ret;
}

int build_certificate_message(
  Buffer *buf, Bytes context, ZuSpan<const Bytes> certificates, Bytes ocsp_status)
{ return build_certificate_message_(buf, context, certificates, ocsp_status); }

struct DefaultEmitCertificate : EmitCertificate {
  int call(
    Ztls::Cxn *tls, MessageEmitter *emitter, KeySchedule *key_sched, Bytes context,
    int push_status_request, ZuSpan<const uint16_t> compress_algos) override
  {
    int ret;

    Ztls_PushMessage(emitter, key_sched, Ztls_HsTypeCert, {
      auto &certs = tls->ctx->certificates;
      if ((ret = build_certificate_message_(emitter->buf, context, certs, {})) != 0) goto Exit;
    });

    ret = 0;
Exit:
    return ret;
  }
};

static int send_certificate(
  Ztls::Cxn *tls, MessageEmitter *emitter, SignatureAlgorithms *signatureAlgorithms,
  Bytes context, int push_status_request, ZuSpan<const uint16_t> compress_algos)
{
  int ret;

  if (!signatureAlgorithms->length()) {
    ret = Ztls_AlertMissingExt;
    goto Exit;
  }

  {
    // send Certificate (or the equivalent)
    static DefaultEmitCertificate default_emit_certificate;
    EmitCertificate		 *emitCertificate =
      tls->ctx->emitCertificate != NULL ? tls->ctx->emitCertificate : &default_emit_certificate;
Redo:
    if ((ret = emitCertificate->call(
	   tls, emitter, tls->keySchedule, context, push_status_request, compress_algos)) != 0) {
      if (ret == Ztls_ErrorDelegate) {
	assert(emitCertificate != &default_emit_certificate);
	emitCertificate = &default_emit_certificate;
	goto Redo;
      }
      goto Exit;
    }
  }

Exit:
  return ret;
}

static int send_certificate_verify(
  Ztls::Cxn *tls, MessageEmitter *emitter, SignatureAlgorithms *signatureAlgorithms,
  const char *context_string)
{
  size_t start_off = emitter->buf->off;
  int	 ret;

  if (tls->ctx->signCertificate == NULL) return 0;
  // build and send CertificateVerify
  Ztls_PushMessage(emitter, tls->keySchedule, Ztls_HsTypeCertVerify, {
    Buffer *sendbuf = emitter->buf;
    size_t  algo_off = sendbuf->off;
    Ztls_BufferPush16(sendbuf, 0);
    // filled in later
    Ztls_BufferPushBlock(sendbuf, 2, {
      uint16_t algo;
      if (auto resume = tls->signatureResume) {
	ret = resume(sendbuf, &algo);
      } else {
	uint8_t data[Ztls_SignInputSize];
	size_t	datalen = build_cert_verify_signdata(data, tls->keySchedule, context_string);
	ret = tls->ctx->signCertificate->call(
	  tls, tls->isServer, &algo, sendbuf, wire_bytes(data, datalen),
	  signatureAlgorithms ? ZuSpan<const uint16_t>{*signatureAlgorithms} :
				ZuSpan<const uint16_t>{});
	ZuClear(data, sizeof(data));
      }
      if (ret == Ztls_ErrorAsyncOperation) {
	assert(tls->isServer && tls->signatureResume && tls->signatureRetire);
	// CertificateVerify is rebuilt when the signature becomes available.
	emitter->buf->off = start_off;
	goto Exit;
      }
      if (ret) goto Exit;
      sendbuf->base[algo_off] = (uint8_t)(algo >> 8);
      sendbuf->base[algo_off + 1] = (uint8_t)algo;
    });
  });
Exit:
  return ret;
}

static int cli_handle_cert_request(Ztls::Cxn *tls, Bytes message, HandshakeProps *properties)
{
  const uint8_t	      *src = message.data() + Ztls_HSHeaderSize,
		*const end = message.data() + message.length();
  int		       ret = 0;

  assert(
    !tls->isPskHandshake &&
    "state machine asserts that this message is never delivered when PSK is used");

  if ((ret = decode_certificate_request(tls, &tls->certificateRequest, src, end)) != 0)
    return ret;

  // This field SHALL be zero length unless used for the post-handshake authentication exchanges
  // (section 4.3.2)
  if (tls->certificateRequest.context.length() != 0) return Ztls_AlertIllegalParameter;

  tls->state = Ztls_::State::CliExpectCert;
  _key_schedule_update_hash(tls->keySchedule, message.data(), message.length(), 0);

  return Ztls_ErrorInProgress;
}

static int
handle_certificate(Ztls::Cxn *tls, const uint8_t *src, const uint8_t *end, int *got_certs)
{
  PeerCertificates certs;
  int		   ret = 0;

  // certificate request context
  Ztls_DecodeOpenBlock(src, end, 1, {
    if (src != end) {
      ret = Ztls_AlertIllegalParameter;
      goto Exit;
    }
  });
  // certificate_list
  Ztls_DecodeBlock(src, end, 3, {
    while (src != end) {
      Ztls_DecodeOpenBlock(src, end, 3, {
	certs.push(wire_bytes(src, end - src));
	src = end;
      });
      uint16_t type;
      Ztls_DecodeOpenExtensions(src, end, Ztls_HsTypeCert, &type, {
	if (tls->ctx->onExtension != NULL &&
	  (ret = tls->ctx->onExtension->call(
		   tls, Ztls_HsTypeCert, type, wire_bytes(src, end - src)) != 0))
	  goto Exit;
	src = end;
      });
    }
  });

  if (tls->ctx->verifyCertificate != NULL) {
    const char *serverName = NULL;
    if (!isServer(tls)) {
      if (tls->ech.state == Ztls_::EchState::Offered) {
	serverName = tls->ech.client.publicName.data();
      } else {
	serverName = tls->serverName.data();
      }
    }
    tls->clearVerifier();
    if ((ret = tls->ctx->verifyCertificate->call(
	   tls, serverName, &tls->certificateVerify, certs)) != 0) {
      tls->clearVerifier();
      goto Exit;
    }
  }

  *got_certs = certs.length() != 0;

Exit:
  return ret;
}

static int client_do_handle_certificate(Ztls::Cxn *tls, const uint8_t *src, const uint8_t *end)
{
  int got_certs, ret;

  if ((ret = handle_certificate(tls, src, end, &got_certs)) != 0) return ret;
  if (!got_certs) return Ztls_AlertIllegalParameter;

  return 0;
}

static int client_handle_certificate(Ztls::Cxn *tls, Bytes message)
{
  int ret;

  if ((ret = client_do_handle_certificate(
	 tls, message.data() + Ztls_HSHeaderSize, message.data() + message.length())) != 0)
    return ret;

  _key_schedule_update_hash(tls->keySchedule, message.data(), message.length(), 0);

  tls->state = Ztls_::State::CliWaitCertSig;
  return Ztls_ErrorInProgress;
}

ZuDerive(
  UncompressedCertificate, (ZtBArray<ZtArrayHeapID<"Ztls.Engine.UncompressedCertificate">>));

static int cli_handle_compressed_cert(Ztls::Cxn *tls, Bytes message)
{
  const uint8_t		 *src = message.data() + Ztls_HSHeaderSize,
		*const	  end = message.data() + message.length();
  uint16_t		  algo;
  uint32_t		  uncompressed_size;
  UncompressedCertificate uncompressed;
  int			  ret;

  if (tls->ctx->decompressCertificate == NULL) {
    ret = Ztls_AlertUnwaitedMsg;
    goto Exit;
  }

  // decode
  if ((ret = decode16(&algo, &src, end)) != 0) goto Exit;
  if ((ret = decode24(&uncompressed_size, &src, end)) != 0) goto Exit;
  if (uncompressed_size > 65536) {
    // TODO find a sensible number
    ret = Ztls_AlertBadCert;
    goto Exit;
  }
  uncompressed.length(uncompressed_size ? uncompressed_size : 1);
  Ztls_DecodeBlock(src, end, 3, {
    if ((ret = tls->ctx->decompressCertificate->call(
	   tls, algo, wire_bytes(uncompressed.data(), uncompressed_size),
	   wire_bytes(src, end - src))) != 0)
      goto Exit;
    src = end;
  });

  // handle
  if ((ret = client_do_handle_certificate(
	 tls, uncompressed.data(), uncompressed.data() + uncompressed_size)) != 0)
    goto Exit;

  _key_schedule_update_hash(tls->keySchedule, message.data(), message.length(), 0);
  tls->state = Ztls_::State::CliWaitCertSig;
  ret = Ztls_ErrorInProgress;

Exit:
  return ret;
}

static int server_handle_certificate(Ztls::Cxn *tls, Bytes message)
{
  int got_certs, ret;

  if ((ret = handle_certificate(
	 tls, message.data() + Ztls_HSHeaderSize, message.data() + message.length(),
	 &got_certs)) != 0)
    return ret;

  _key_schedule_update_hash(tls->keySchedule, message.data(), message.length(), 0);

  if (got_certs) {
    tls->state = Ztls_::State::SrvWaitCertSig;
  } else {
    // Client did not provide certificate, and the verifier says we can fail open. Therefore, the
    // next message is Finished.
    tls->state = Ztls_::State::SrvWaitFinished;
  }

  return Ztls_ErrorInProgress;
}

static int handle_certificate_verify(Ztls::Cxn *tls, Bytes message, const char *context_string)
{
  const uint8_t	      *src = message.data() + Ztls_HSHeaderSize,
		*const end = message.data() + message.length();
  uint16_t	       algo;
  Bytes		       signature;
  uint8_t	       signdata[Ztls_SignInputSize];
  size_t	       signdata_size;
  int		       ret;
  auto		       verifier = tls->certificateVerify;
  tls->certificateVerify = nullptr;
  ZuGuard release{[verifier]() { delete verifier; }};

  // decode
  if ((ret = decode16(&algo, &src, end)) != 0) goto Exit;
  Ztls_DecodeBlock(src, end, 2, {
    signature = wire_bytes(src, end - src);
    src = end;
  });

  signdata_size = build_cert_verify_signdata(signdata, tls->keySchedule, context_string);
  if (verifier != nullptr) {
    ret = verifier->verify(algo, wire_bytes(signdata, signdata_size), signature);
  } else {
    ret = 0;
  }
  ZuClear(signdata, signdata_size);
  if (ret != 0) { goto Exit; }

  _key_schedule_update_hash(tls->keySchedule, message.data(), message.length(), 0);

Exit:
  return ret;
}

static int cli_handle_cert_verify(Ztls::Cxn *tls, Bytes message)
{
  int ret = handle_certificate_verify(tls, message, Ztls_SrvCertVerifyCtx);

  if (ret == 0) {
    tls->state = Ztls_::State::CliWaitFinished;
    ret = Ztls_ErrorInProgress;
  }

  return ret;
}

static int srv_handle_cert_verify(Ztls::Cxn *tls, Bytes message)
{
  int ret = handle_certificate_verify(tls, message, Ztls_CliCertVerifyCtx);

  if (ret == 0) {
    tls->state = Ztls_::State::SrvWaitFinished;
    ret = Ztls_ErrorInProgress;
  }

  return ret;
}

static int client_handle_finished(Ztls::Cxn *tls, MessageEmitter *emitter, Bytes message)
{
  uint8_t send_secret[Ztls_MaxDigestSize];
  int	  alert_ech_required = tls->ech.state == Ztls_::EchState::Offered, ret;

  if ((ret = verify_finished(tls, message)) != 0) goto Exit;
  _key_schedule_update_hash(tls->keySchedule, message.data(), message.length(), 0);

  // update traffic keys by using messages upto ServerFinished, but commission them after sending
  // ClientFinished
  if ((ret = key_schedule_extract(tls->keySchedule, wire_bytes(NULL, 0))) != 0) goto Exit;
  if ((ret = setup_traffic_protection(tls, 0, "s ap traffic", 3, 0, 0)) != 0) goto Exit;
  if ((ret = derive_secret(tls->keySchedule, send_secret, "c ap traffic")) != 0) goto Exit;
  if ((ret = derive_exporter_secret(tls, 0)) != 0) goto Exit;

  //  if sending early data, emit EOED and commision the client handshake traffic secret
  if (tls->pendingHandshakeSecret.data() != NULL) {
    assert(tls->txTraffic.aead != NULL || tls->ctx->updateTrafficKey != NULL);
    if (tls->usingEarlyData && !tls->ctx->omitEndOfEarlyData)
      Ztls_PushMessage(emitter, tls->keySchedule, Ztls_HsTypeEndOfEarly, {});
    tls->usingEarlyData = 0;
    if ((ret = commission_handshake_secret(tls)) != 0) goto Exit;
  }

  if ((ret = push_change_cipher_spec(tls, emitter)) != 0) goto Exit;

  if (!alert_ech_required && tls->certificateRequest.context.data() != NULL) {
    if ((ret = send_certificate(
	   tls, emitter, &tls->certificateRequest.signatureAlgorithms,
	   tls->certificateRequest.context, 0, {})) == 0)
      ret = send_certificate_verify(
	tls, emitter, &tls->certificateRequest.signatureAlgorithms, Ztls_CliCertVerifyCtx);
    tls->certificateRequestBytes.null();
    tls->certificateRequest.context = wire_bytes(NULL, 0);
    if (ret != 0) goto Exit;
  }

  ret = send_finished(tls, emitter);

  memcpy(tls->txTraffic.secret, send_secret, sizeof(send_secret));
  if ((ret = setup_traffic_protection(tls, 1, NULL, 3, 0, 0)) != 0) goto Exit;

  tls->state = Ztls_::State::CliPostHS;

  // if ECH was rejected, close the connection with ECH_REQUIRED alert after verifying messages up
  // to Finished
  if (alert_ech_required) ret = Ztls_AlertECHRequired;

Exit:
  ZuClear(send_secret, sizeof(send_secret));
  return ret;
}

static int cli_handle_new_ticket(Ztls::Cxn *tls, Bytes message)
{
  const uint8_t	      *src = message.data() + Ztls_HSHeaderSize,
		*const end = message.data() + message.length();
  Bytes		       ticket_nonce;
  int		       ret;

  {
    // verify the format
    uint32_t ticketLifetime, ticket_age_add, maxEarlyDataSize;
    Bytes    ticket;
    if ((ret = decode_new_session_ticket(
	   tls, &ticketLifetime, &ticket_age_add, &ticket_nonce, &ticket, &maxEarlyDataSize, src,
	   end)) != 0)
      return ret;
  }

  // do nothing if use of session ticket is disabled
  if (tls->ctx->saveTicket == NULL) return 0;

  // save the extension, along with the key of myself
  Buffer ticket_buf;
  buffer_init_rx(&ticket_buf, const_cast<char *>(""), 0);
  Ztls_BufferPush64(&ticket_buf, tls->ctx->getTime->call());
  Ztls_BufferPush16(&ticket_buf, tls->keyShare->id);
  Ztls_BufferPush16(&ticket_buf, tls->cipherSuite->id);
  Ztls_BufferPushBlock(&ticket_buf, 3, { Ztls_BufferPushv(&ticket_buf, src, end - src); });
  Ztls_BufferPushBlock(&ticket_buf, 2, {
    if ((ret = buffer_reserve(&ticket_buf, tls->keySchedule->hashes[0].algo->digestSize, 0)) != 0)
      goto Exit;
    if ((ret = derive_resumption_secret(
	   tls->keySchedule, ticket_buf.base + ticket_buf.off, ticket_nonce)) != 0)
      goto Exit;
    ticket_buf.off += tls->keySchedule->hashes[0].algo->digestSize;
  });

  if ((ret = tls->ctx->saveTicket->call(tls, wire_bytes(ticket_buf.base, ticket_buf.off))) != 0)
    goto Exit;

  ret = 0;
Exit:
  buffer_dispose(&ticket_buf);
  return ret;
}

static int cli_hello_decode_srv_name(Bytes *name, const uint8_t **src, const uint8_t *const end)
{
  int ret = 0;

  Ztls_DecodeOpenBlock(*src, end, 2, {
    do {
      uint8_t type;
      if ((ret = decode8(&type, src, end)) != 0) goto Exit;
      Ztls_DecodeOpenBlock(*src, end, 2, {
	switch (type) {
	case Ztls_SrvNameTypeHostname:
	  if (end - *src == 0) {
	    ret = Ztls_AlertDecodeError;
	    goto Exit;
	  }
	  if (memchr(*src, '\0', end - *src) != 0) {
	    ret = Ztls_AlertIllegalParameter;
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

static int select_negotiated_group(
  const KeyExchangeAlgo **selected, const KeyExchangeAlgo **candidates, const uint8_t *src,
  const uint8_t *const end)
{
  int ret;

  Ztls_DecodeBlock(src, end, 2, {
    while (src != end) {
      uint16_t group;
      if ((ret = decode16(&group, &src, end)) != 0) goto Exit;
      const KeyExchangeAlgo **c = candidates;
      for (; *c != NULL; ++c) {
	if ((*c)->id == group) {
	  *selected = *c;
	  return 0;
	}
      }
    }
  });

  ret = Ztls_AlertHSFailure;

Exit:
  return ret;
}

static int decode_client_hello(
  Context *ctx, ClientHello *ch, const uint8_t *src, const uint8_t *const end,
  HandshakeProps *properties, Ztls::Cxn *tls_cbarg)
{
  const uint8_t *start = src;
  uint16_t	 exttype = 0;
  int		 ret;

  // decode protocol version (do not bare to decode something older than TLS 1.0)
  if ((ret = decode16(&ch->legacyVersion, &src, end)) != 0) goto Exit;
  if (ch->legacyVersion < 0x0301) {
    ret = Ztls_AlertProtoVer;
    goto Exit;
  }

  // skip random
  if (end - src < Ztls_HelloRandomSize) {
    ret = Ztls_AlertDecodeError;
    goto Exit;
  }
  ch->randomBytes = src;
  src += Ztls_HelloRandomSize;

  // skip legacySessionId
  Ztls_DecodeOpenBlock(src, end, 1, {
    if (end - src > 32) {
      ret = Ztls_AlertDecodeError;
      goto Exit;
    }
    ch->legacySessionId = wire_bytes(src, end - src);
    src = end;
  });

  // decode and select from ciphersuites
  Ztls_DecodeOpenBlock(src, end, 2, {
    if ((end - src) % 2 != 0) {
      ret = Ztls_AlertDecodeError;
      goto Exit;
    }
    ch->cipherSuites = wire_bytes(src, end - src);
    src = end;
  });

  // decode legacy_compression_methods
  Ztls_DecodeOpenBlock(src, end, 1, {
    if (src == end) {
      ret = Ztls_AlertDecodeError;
      goto Exit;
    }
    ch->compressionMethods.ids = src;
    ch->compressionMethods.count = end - src;
    src = end;
  });

  // In TLS versions 1.2 and earlier CH might not have an extensions block (or they might, see
  // what OpenSSL 1.0.0 sends); so bail out if that is the case after parsing the main variables.
  // Zero is returned as it is a valid ClientHello. However `Ztls::Cxn::selectedVersion` remains
  // zero indicating that no compatible version were found.
  if (src == end) {
    ret = 0;
    goto Exit;
  }

  // decode extensions
  ch->firstExtensionAt = src - start + 2;
  Ztls_DecodeExtensions(src, end, Ztls_HsTypeCliHello, &exttype, {
    ch->psk.isLastExtension = 0;
    if (ctx->onExtension != NULL && tls_cbarg != NULL &&
      (ret = ctx->onExtension->call(
	       tls_cbarg, Ztls_HsTypeCliHello, exttype, wire_bytes(src, end - src)) != 0))
      goto Exit;
    switch (exttype) {
    case Ztls_ExtTypeSrvName:
      if ((ret = cli_hello_decode_srv_name(&ch->serverName, &src, end)) != 0) goto Exit;
      if (src != end) {
	ret = Ztls_AlertDecodeError;
	goto Exit;
      }
      break;
    case Ztls_ExtTypeALPN:
      Ztls_DecodeBlock(src, end, 2, {
	do {
	  Ztls_DecodeOpenBlock(src, end, 1, {
	    // rfc7301 3.1: empty strings MUST NOT be included
	    if (src == end) {
	      ret = Ztls_AlertDecodeError;
	      goto Exit;
	    }
	    ch->alpn.push(wire_bytes(src, end - src));
	    src = end;
	  });
	} while (src != end);
      });
      break;
    case Ztls_ExtTypeSrvCertType:
      Ztls_DecodeBlock(src, end, 1, {
	size_t list_size = end - src;

	// RFC7250 4.1: No empty list, no list with single x509 element
	if (list_size == 0 || (list_size == 1 && *src == Ztls_CertTypeX509)) {
	  ret = Ztls_AlertDecodeError;
	  goto Exit;
	}

	do {
	  if (ch->serverCertificateTypes.count <
	    ZuTraits<decltype(ch->serverCertificateTypes.list)>::length(
	      ch->serverCertificateTypes.list))
	    ch->serverCertificateTypes.list[ch->serverCertificateTypes.count++] = *src;
	  src++;
	} while (src != end);
      });
      break;
    case Ztls_ExtTypeCompressCert:
      Ztls_DecodeBlock(src, end, 1, {
	do {
	  uint16_t id;
	  if ((ret = decode16(&id, &src, end)) != 0) goto Exit;
	  ch->certCompressionAlgos.push(id);
	} while (src != end);
      });
      break;
    case Ztls_ExtGroups:
      ch->negotiatedGroups = wire_bytes(src, end - src);
      break;
    case Ztls_ExtTypeSigAlgos:
      if ((ret = decode_signature_algorithms(&ch->signatureAlgorithms, &src, end)) != 0)
	goto Exit;
      break;
    case Ztls_ExtTypeKeyShare:
      ch->keyShares = wire_bytes(src, end - src);
      break;
    case Ztls_ExtVersions:
      Ztls_DecodeBlock(src, end, 1, {
	size_t selected_index =
	  ZuTraits<decltype(supported_versions)>::length(supported_versions);
	do {
	  size_t   i;
	  uint16_t v;
	  if ((ret = decode16(&v, &src, end)) != 0) goto Exit;
	  for (i = 0; i != selected_index; ++i) {
	    if (supported_versions[i] == v) {
	      selected_index = i;
	      break;
	    }
	  }
	} while (src != end);
	if (selected_index != ZuTraits<decltype(supported_versions)>::length(supported_versions))
	  ch->selectedVersion = supported_versions[selected_index];
      });
      break;
    case Ztls_ExtTypeCookie:
      if (properties == NULL || properties->server.cookie.key == NULL) {
	ret = Ztls_AlertIllegalParameter;
	goto Exit;
      }
      ch->cookie.all = wire_bytes(src, end - src);
      Ztls_DecodeBlock(src, end, 2, {
	const auto tbs = src;
	Ztls_DecodeOpenBlock(src, end, 2, {
	  Ztls_DecodeOpenBlock(src, end, 1, {
	    ch->cookie.ch1Hash = wire_bytes(src, end - src);
	    src = end;
	  });
	  uint8_t sentKeyShare;
	  if ((ret = decode8(&sentKeyShare, &src, end)) != 0) goto Exit;
	  switch (sentKeyShare) {
	  case 0:
	    assert(!ch->cookie.sentKeyShare);
	    break;
	  case 1:
	    ch->cookie.sentKeyShare = 1;
	    break;
	  default:
	    ret = Ztls_AlertDecodeError;
	    goto Exit;
	  }
	});
	ch->cookie.tbs = wire_bytes(tbs, src - tbs);
	Ztls_DecodeBlock(src, end, 1, {
	  ch->cookie.signature = wire_bytes(src, end - src);
	  src = end;
	});
      });
      break;
    case Ztls_ExtTypePreSharedKey: {
      size_t num_identities = 0;
      Ztls_DecodeOpenBlock(src, end, 2, {
	do {
	  CliHelloPskIdentity psk{};
	  Ztls_DecodeOpenBlock(src, end, 2, {
	    if (end - src < 1) {
	      ret = Ztls_AlertDecodeError;
	      goto Exit;
	    }
	    psk.identity = wire_bytes(src, end - src);
	    src = end;
	  });
	  if ((ret = decode32(&psk.obfuscatedTicketAge, &src, end)) != 0) goto Exit;
	  if (ch->psk.identities.count <
	    ZuTraits<decltype(ch->psk.identities.list)>::length(ch->psk.identities.list))
	    ch->psk.identities.list[ch->psk.identities.count++] = psk;
	  ++num_identities;
	} while (src != end);
      });
      ch->psk.hashEnd = src;
      Ztls_DecodeBlock(src, end, 2, {
	size_t num_binders = 0;
	do {
	  Ztls_DecodeOpenBlock(src, end, 1, {
	    if (num_binders < ch->psk.identities.count)
	      ch->psk.identities.list[num_binders].binder = wire_bytes(src, end - src);
	    src = end;
	  });
	  ++num_binders;
	} while (src != end);
	if (num_identities != num_binders) {
	  ret = Ztls_AlertIllegalParameter;
	  goto Exit;
	}
      });
      ch->psk.isLastExtension = 1;
    } break;
    case Ztls_ExtPSKKexModes:
      Ztls_DecodeBlock(src, end, 1, {
	do {
	  uint8_t mode;
	  if ((ret = decode8(&mode, &src, end)) != 0) goto Exit;
	  if (mode < sizeof(ch->psk.keModes) * 8) ch->psk.keModes |= 1u << mode;
	} while (src != end);
      });
      break;
    case Ztls_ExtTypeEarly:
      ch->psk.earlyDataIndication = 1;
      break;
    case Ztls_ExtTypeStatusRequest:
      ch->statusRequest = 1;
      break;
    case Ztls_ExtTypeTicketRequest:
      if (end - src != 2) {
	ret = Ztls_AlertDecodeError;
	goto Exit;
      }
      ch->ticketRequest.newSessionCount = *src++;
      ch->ticketRequest.resumptionCount = *src++;
      break;
    case Ztls_ExtTypeEncCliHello:
      if ((ret = decode8(&ch->ech.type, &src, end)) != 0) goto Exit;
      switch (ch->ech.type) {
      case Ztls_ECHCliHelloOuter:
	if ((ret = decode16(&ch->ech.cipherSuite.kdf, &src, end)) != 0 ||
	  (ret = decode16(&ch->ech.cipherSuite.aead, &src, end)) != 0)
	  goto Exit;
	if ((ret = decode8(&ch->ech.configId, &src, end)) != 0) goto Exit;
	Ztls_DecodeOpenBlock(src, end, 2, {
	  ch->ech.enc = wire_bytes(src, end - src);
	  src = end;
	});
	Ztls_DecodeOpenBlock(src, end, 2, {
	  if (src == end) {
	    ret = Ztls_AlertDecodeError;
	    goto Exit;
	  }
	  ch->ech.payload = wire_bytes(src, end - src);
	  src = end;
	});
	break;
      case Ztls_ECHCliHelloInner:
	if (src != end) {
	  ret = Ztls_AlertDecodeError;
	  goto Exit;
	}
	ch->ech.payload = wire_bytes("", 0);
	// non-zero base indicates that the extension was received
	break;
      default:
	ret = Ztls_AlertIllegalParameter;
	goto Exit;
      }
      src = end;
      break;
    default:
      if (tls_cbarg != NULL && collect_unknown_ext(tls_cbarg, properties, exttype)) {
	if ((ret = collect_unknown_extension(
	       tls_cbarg, exttype, src, end, ch->unknownExtensions)) != 0)
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

static int rebuild_ch_inner_extensions(
  Buffer *buf, const uint8_t **src, const uint8_t *const end, const uint8_t *outer_ext,
  const uint8_t *outer_ext_end)
{
  int ret;

  Ztls_BufferPushBlock(buf, 2, {
    Ztls_DecodeOpenBlock(*src, end, 2, {
      while (*src != end) {
	uint16_t exttype;
	if ((ret = decode16(&exttype, src, end)) != 0) goto Exit;
	Ztls_DecodeOpenBlock(*src, end, 2, {
	  if (exttype == Ztls_ExtTypeECHOuterExts) {
	    Ztls_DecodeOpenBlock(*src, end, 1, {
	      do {
		uint16_t reftype;
		uint16_t outertype;
		uint16_t outersize;
		if ((ret = decode16(&reftype, src, end)) != 0) goto Exit;
		if (reftype == Ztls_ExtTypeEncCliHello) {
		  ret = Ztls_AlertIllegalParameter;
		  goto Exit;
		}
		while (1) {
		  if (decode16(&outertype, &outer_ext, outer_ext_end) != 0 ||
		    decode16(&outersize, &outer_ext, outer_ext_end) != 0) {
		    ret = Ztls_AlertIllegalParameter;
		    goto Exit;
		  }
		  assert(outer_ext_end - outer_ext >= outersize);
		  if (outertype == reftype) break;
		  outer_ext += outersize;
		}
		Ztls_BufferPushExtension(buf, reftype, {
		  Ztls_BufferPushv(buf, outer_ext, outersize);
		  outer_ext += outersize;
		});
	      } while (*src != end);
	    });
	  } else {
	    Ztls_BufferPushExtension(buf, exttype, {
	      Ztls_BufferPushv(buf, *src, end - *src);
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

static int rebuild_ch_inner(
  Buffer *buf, const uint8_t *src, const uint8_t *const end, ClientHello *outer_ch,
  const uint8_t *outer_ext, const uint8_t *outer_ext_end)
{
#define Ztls_CopyBlock(capacity) \
  do { \
    Ztls_DecodeOpenBlock(src, end, (capacity), { \
      Ztls_BufferPushBlock(buf, (capacity), { Ztls_BufferPushv(buf, src, end - src); }); \
      src = end; \
    }); \
  } while (0)

  int ret;

  Ztls_BufferPushMsgBody(buf, NULL, Ztls_HsTypeCliHello, {
    {
      // legacyVersion
      uint16_t legacyVersion;
      if ((ret = decode16(&legacyVersion, &src, end)) != 0) goto Exit;
      Ztls_BufferPush16(buf, legacyVersion);
    }

    // hello random
    if (end - src < Ztls_HelloRandomSize) {
      ret = Ztls_AlertDecodeError;
      goto Exit;
    }
    Ztls_BufferPushv(buf, src, Ztls_HelloRandomSize);
    src += Ztls_HelloRandomSize;

    Ztls_DecodeOpenBlock(src, end, 1, {
      if (src != end) {
	ret = Ztls_AlertIllegalParameter;
	goto Exit;
      }
    });
    Ztls_BufferPushBlock(buf, 1, {
      Ztls_BufferPushv(buf, outer_ch->legacySessionId.data(), outer_ch->legacySessionId.length());
    });

    // cipher-suites and legacy-compression-methods
    Ztls_CopyBlock(2);
    Ztls_CopyBlock(1);

    // extensions
    if ((ret = rebuild_ch_inner_extensions(buf, &src, end, outer_ext, outer_ext_end)) != 0)
      goto Exit;
  });

  // padding must be all zero
  for (; src != end; ++src) {
    if (*src != '\0') {
      ret = Ztls_AlertIllegalParameter;
      goto Exit;
    }
  }

Exit:
  return ret;

#undef Ztls_CopyBlock
}

// Wrapper function for invoking the onClientHello callback, taking an exhaustive list of
// parameters as arguments. The intention is to not miss setting them as we add new parameters to
// the struct.
static inline int call_on_client_hello_cb(
  Ztls::Cxn *tls, Bytes serverName, Bytes rawMessage, Bytes cipherSuites,
  ZuSpan<const Bytes> alpns, ZuSpan<const uint16_t> sig_algos,
  ZuSpan<const uint16_t> certCompAlgos, ZuSpan<const uint8_t> server_cert_types,
  ZuSpan<const CliHelloPskIdentity> pskIdentities, int incompatibleVersion)
{
  if (tls->ctx->onClientHello == NULL) return 0;

  CliHelloParams params = {serverName, rawMessage, cipherSuites, alpns, sig_algos, certCompAlgos,
    server_cert_types, pskIdentities, static_cast<unsigned>(incompatibleVersion)};
  return tls->ctx->onClientHello->call(tls, &params);
}

static int check_cli_hello_constraints(
  Context *ctx, ClientHello *ch, const uint8_t *prev_random, int ech_is_inner_ch,
  Bytes rawMessage, Ztls::Cxn *tls_cbarg)
{
  int is_second_flight = prev_random != 0;

  // The following check is necessary so that we would be able to track the connection in
  // SSLKEYLOGFILE, even though it might not be for the safety of the protocol.
  if (is_second_flight &&
    !Ztls::ctEqual({ch->randomBytes, Ztls_HelloRandomSize}, {prev_random, Ztls_HelloRandomSize}))
    return Ztls_AlertHSFailure;

  // bail out if CH cannot be handled as TLS 1.3
  if (!is_supported_version(ch->selectedVersion)) {
    // ECH: server MUST abort with an "illegal_parameter" alert if the client offers TLS 1.2 or
    // below (draft-15 7.1)
    if (ech_is_inner_ch) return Ztls_AlertIllegalParameter;
    // fail with PROTOCOL_VERSION alert, after providing the applications the raw CH and SNI to
    // help them fallback
    if (!is_second_flight) {
      int ret;
      if ((ret = call_on_client_hello_cb(
	     tls_cbarg, ch->serverName, rawMessage, ch->cipherSuites, ch->alpn, {}, {}, {}, {},
	     1)) != 0)
	return ret;
    }
    return Ztls_AlertProtoVer;
  }

  // Check TLS 1.3-specific constraints. Hereafter, we might exit without calling onClientHello.
  // That's fine because this CH is ought to be rejected.
  if (ch->legacyVersion <= 0x0300) {
    // RFC 8446 Appendix D.5: any endpoint receiving a Hello message with legacyVersion set to
    // 0x0300 MUST abort the handshake with a "protocol_version" alert.
    return Ztls_AlertProtoVer;
  }
  if (!(ch->compressionMethods.count == 1 && ch->compressionMethods.ids[0] == 0))
    return Ztls_AlertIllegalParameter;
  // pre-shared key
  if (ch->psk.hashEnd != NULL) {
    // PSK must be the last extension
    if (!ch->psk.isLastExtension) return Ztls_AlertIllegalParameter;
  } else {
    if (ch->psk.earlyDataIndication) return Ztls_AlertIllegalParameter;
  }

  if (ech_is_inner_ch && ch->ech.payload.data() == NULL) return Ztls_AlertIllegalParameter;
  if (ch->ech.payload.data() != NULL &&
    ch->ech.type != (ech_is_inner_ch ? Ztls_ECHCliHelloInner : Ztls_ECHCliHelloOuter))
    return Ztls_AlertIllegalParameter;

  return 0;
}

static int vec_is_string(Bytes x, const char *y)
{ return strncmp((const char *)x.data(), y, x.length()) == 0 && y[x.length()] == '\0'; }

// Looks for a PSK identity that can be used, and if found, updates the handshake state and
// returns the necessary variables. If `Context::preSharedKey` is set, only tries handshake using
// those keys provided. Otherwise, tries resumption.
static int try_psk_handshake(
  Ztls::Cxn *tls, size_t *psk_index, int *accept_early_data, ClientHello *ch, Bytes ch_trunc,
  int is_second_flight)
{
  Buffer   decbuf;
  Bytes	   secret, ticket_ctx, ticket_negotiated_protocol;
  uint64_t issue_at, now = tls->ctx->getTime->call();
  uint32_t age_add;
  uint16_t ticket_key_exchange_id, ticket_csid;
  uint8_t  binder_key[Ztls_MaxDigestSize];
  int	   ret;

  buffer_init_rx(&decbuf, const_cast<char *>(""), 0);

  for (*psk_index = 0; *psk_index < ch->psk.identities.count; ++*psk_index) {
    CliHelloPskIdentity *identity = ch->psk.identities.list + *psk_index;

    // negotiate using fixed pre-shared key
    if (tls->ctx->preSharedKey.identity.data() != NULL) {
      if (identity->identity.length() == tls->ctx->preSharedKey.identity.length() &&
	memcmp(
	  identity->identity.data(), tls->ctx->preSharedKey.identity.data(),
	  identity->identity.length()) == 0) {
	*accept_early_data = ch->psk.earlyDataIndication && *psk_index == 0;
	tls->keyShare = NULL;
	secret = tls->ctx->preSharedKey.secret;
	goto Found;
      }
      continue;
    }

    // decrypt ticket and decode
    if (tls->ctx->encryptTicket == NULL || tls->ctx->keyExchanges == NULL) continue;
    int can_accept_early_data = *psk_index == 0;
    decbuf.off = 0;
    switch (tls->ctx->encryptTicket->call(tls, 0, &decbuf, identity->identity)) {
    case 0:
      // decrypted
      break;
    case Ztls_ErrorRejectEarlyData:
      // decrypted, but early data is rejected
      can_accept_early_data = 0;
      break;
    default:
      // decryption failure
      continue;
    }
    if (decode_session_identifier(
	  &issue_at, &secret, &age_add, &ticket_ctx, &ticket_key_exchange_id, &ticket_csid,
	  &ticket_negotiated_protocol, decbuf.base, decbuf.base + decbuf.off) != 0)
      continue;
    // check age
    if (now < issue_at) continue;
    if (now - issue_at > (uint64_t)tls->ctx->ticketLifetime * 1000) continue;
    *accept_early_data = 0;
    if (ch->psk.earlyDataIndication && can_accept_early_data) {
      // accept early-data if abs(diff) between the reported age and the actual age is within +=
      // 10 seconds
      int64_t delta = (now - issue_at) - (identity->obfuscatedTicketAge - age_add);
      if (delta < 0) delta = -delta;
      if (tls->ctx->maxEarlyDataSize != 0 && delta <= ZtlsEarlyDataMaxDelay)
	*accept_early_data = 1;
    }
    // check ticket context
    if (tls->ctx->ticketContext.isSet) {
      if (!(ticket_ctx.length() == sizeof(tls->ctx->ticketContext.bytes) &&
	    memcmp(ticket_ctx.data(), tls->ctx->ticketContext.bytes, ticket_ctx.length()) == 0))
	continue;
    } else {
      // check server-name
      if (ticket_ctx.length() != 0) {
	if (tls->serverName.data() == NULL) continue;
	if (!vec_is_string(ticket_ctx, tls->serverName.data())) continue;
      } else {
	if (tls->serverName.data() != NULL) continue;
      }
    }
    {
      // check key-exchange
      const KeyExchangeAlgo **a;
      for (a = tls->ctx->keyExchanges; *a != NULL && (*a)->id != ticket_key_exchange_id; ++a);
      if (*a == NULL) continue;
      tls->keyShare = *a;
    }
    // check cipher-suite
    if (ticket_csid != tls->cipherSuite->id) continue;
    // check negotiated-protocol
    if (ticket_negotiated_protocol.length() != 0) {
      if (tls->negotiatedProtocol.data() == NULL) continue;
      if (!vec_is_string(ticket_negotiated_protocol, tls->negotiatedProtocol.data())) continue;
    }
    // check the length of the decrypted psk and the PSK binder
    if (secret.length() != tls->keySchedule->hashes[0].algo->digestSize) continue;
    if (ch->psk.identities.list[*psk_index].binder.length() !=
      tls->keySchedule->hashes[0].algo->digestSize)
      continue;

    // found
    goto Found;
  }

  // not found
  *psk_index = SIZE_MAX;
  *accept_early_data = 0;
  tls->keyShare = NULL;
  ret = 0;
  goto Exit;

Found:
  if (!is_second_flight && (ret = key_schedule_extract(tls->keySchedule, secret)) != 0) goto Exit;
  if ((ret = derive_empty_secret(
	 tls->keySchedule, binder_key,
	 tls->ctx->preSharedKey.secret.data() != NULL ? "ext binder" : "res binder")) != 0)
    goto Exit;
  _key_schedule_update_hash(tls->keySchedule, ch_trunc.data(), ch_trunc.length(), 0);
  if ((ret = calc_verify_data(
	 binder_key
	 // to conserve space, reuse binder_key for storing verify_data
	 ,
	 tls->keySchedule, binder_key)) != 0)
    goto Exit;
  if (!Ztls::ctEqual(
	{ch->psk.identities.list[*psk_index].binder.data(),
	  tls->keySchedule->hashes[0].algo->digestSize},
	{binder_key, tls->keySchedule->hashes[0].algo->digestSize})) {
    ret = Ztls_AlertDecryptError;
    goto Exit;
  }
  ret = 0;

Exit:
  buffer_dispose(&decbuf);
  ZuClear(binder_key, sizeof(binder_key));
  return ret;
}

static int calc_cookie_signature(
  Ztls::Cxn *tls, HandshakeProps *properties, const KeyExchangeAlgo *negotiated_group, Bytes tbs,
  uint8_t *sig)
{
  const HashAlgo *algo = tls->ctx->cipherSuites[0]->hash;
  HashContext	 *hctx;

  if ((hctx = hmac_create(algo, properties->server.cookie.key, algo->digestSize)) == NULL)
    return Ztls_ErrorNoMemory;

#define Ztls_UpdateBlock(p, _len) \
  do { \
    size_t len = (_len); \
    assert(len < UINT8_MAX); \
    uint8_t len8 = (uint8_t)len; \
    hctx->update(&len8, 1); \
    hctx->update((p), len); \
  } while (0)
#define Ztls_Update16(_v) \
  do { \
    uint16_t v = (_v); \
    uint8_t  b[2] = {uint8_t(v >> 8), uint8_t(v & 0xff)}; \
    hctx->update(b, 2); \
  } while (0)

  Ztls_UpdateBlock(tls->clientRandom, sizeof(tls->clientRandom));
  Ztls_UpdateBlock(
    tls->serverName.data(), tls->serverName.data() != NULL ? strlen(tls->serverName.data()) : 0);
  Ztls_Update16(tls->cipherSuite->id);
  Ztls_Update16(negotiated_group != NULL ? negotiated_group->id : 0);
  Ztls_UpdateBlock(
    properties->server.cookie.additionalData.data(),
    properties->server.cookie.additionalData.length());

  Ztls_UpdateBlock(tbs.data(), tbs.length());

#undef Ztls_UpdateBlock
#undef Ztls_Update16

  {
    hctx->final(sig);
    delete hctx;
  }
  return 0;
}

static int certificate_type_exists(uint8_t *list, size_t count, uint8_t desired_type)
{
  // empty type list means that we default to x509
  if (desired_type == Ztls_CertTypeX509 && count == 0) return 1;
  for (size_t i = 0; i < count; i++) {
    if (list[i] == desired_type) return 1;
  }
  return 0;
}

static int server_handle_hello(
  Ztls::Cxn *tls, MessageEmitter *emitter, Bytes message, HandshakeProps *properties)
{
// legacy version
#define Ztls_EmitServerHello(sched, fill_rand, extensions, post_action) \
  do { \
    size_t sh_start_off; \
    Ztls_PushMessage(emitter, NULL, Ztls_HsTypeSrvHello, { \
      sh_start_off = emitter->buf->off - Ztls_HSHeaderSize; \
      Ztls_BufferPush16(emitter->buf, 0x0303); \
      if ((ret = buffer_reserve(emitter->buf, Ztls_HelloRandomSize, 1)) != 0) goto Exit; \
      do { \
	fill_rand \
      } while (0); \
      emitter->buf->off += Ztls_HelloRandomSize; \
      Ztls_BufferPushBlock(emitter->buf, 1, { \
	Ztls_BufferPushv( \
	  emitter->buf, ch->legacySessionId.data(), ch->legacySessionId.length()); \
      }); \
      Ztls_BufferPush16(emitter->buf, tls->cipherSuite->id); \
      Ztls_BufferPush_1(emitter->buf, 0); \
      Ztls_BufferPushBlock(emitter->buf, 2, { \
	Ztls_BufferPushExtension(emitter->buf, Ztls_ExtVersions, { \
	  Ztls_BufferPush16(emitter->buf, ch->selectedVersion); \
	}); \
	do { \
	  extensions \
	} while (0); \
      }); \
    }); \
    do { \
      post_action \
    } while (0); \
    _key_schedule_update_hash( \
      (sched), emitter->buf->base + sh_start_off, emitter->buf->off - sh_start_off, 0); \
  } while (0)

#define Ztls_EmitHelloRetryRequest(sched, negotiated_group, additionalExtensions, post_action) \
  Ztls_EmitServerHello( \
    (sched), \
    { \
      memcpy( \
	emitter->buf->base + emitter->buf->off, hello_retry_random, Ztls_HelloRandomSize); \
    }, \
    { \
      const KeyExchangeAlgo *_negotiated_group = (negotiated_group); \
      if (_negotiated_group != NULL) { \
	Ztls_BufferPushExtension(emitter->buf, Ztls_ExtTypeKeyShare, { \
	  Ztls_BufferPush16(emitter->buf, _negotiated_group->id); \
	}); \
      } \
      do { \
	additionalExtensions \
      } while (0); \
    }, \
    post_action)
  auto decoded = ZtScratch(DecodedClientHello, 1, 1);
  auto extensions =
    ZtScratch(CollectedExtensions, ZtlsMaxUnknownExts + 1, ZtlsMaxUnknownExts + 1);
  auto ch = decoded.data();
  struct KeyShare {
    const KeyExchangeAlgo	*algorithm;
    Bytes			peerKey;
  } keyShare = {NULL};
  struct Ech {
    EchBytes	encodedChInner;
    EchBytes	chOuterAad;
    Buffer	chInner;
  } ech{};
  enum { HANDSHAKE_MODE_FULL, HANDSHAKE_MODE_PSK, HANDSHAKE_MODE_PSK_DHE } mode;
  size_t	   psk_index = SIZE_MAX;
  KeyExchangeBytes pubkey, ecdh_secret;
  int accept_early_data = 0, is_second_flight = tls->state == Ztls_::State::SrvWait2ndCliHello,
      ret;

  buffer_init_rx(&ech.chInner, const_cast<char *>(""), 0);

  extensions[0] = {UINT16_MAX};
  *ch = ClientHello{.unknownExtensions = extensions.data()};

  // decode ClientHello
  if ((ret = decode_client_hello(
	 tls->ctx, ch, message.data() + Ztls_HSHeaderSize, message.data() + message.length(),
	 properties, tls)) != 0)
    goto Exit;
  if ((ret = check_cli_hello_constraints(
	 tls->ctx, ch, is_second_flight ? tls->clientRandom : NULL, 0, message, tls)) != 0)
    goto Exit;
  if (!is_second_flight) {
    memcpy(tls->clientRandom, ch->randomBytes, Ztls_HelloRandomSize);
  } else {
    // consistency check for ECH extension in response to HRR
    if (tls->ech.aead != NULL) {
      if (ch->ech.payload.data() == NULL) {
	ret = Ztls_AlertMissingExt;
	goto Exit;
      }
      if (!(ch->ech.configId == tls->ech.configId &&
	    ch->ech.cipherSuite.kdf == tls->ech.cipher->id.kdf &&
	    ch->ech.cipherSuite.aead == tls->ech.cipher->id.aead && ch->ech.enc.length() == 0)) {
	ret = Ztls_AlertIllegalParameter;
	goto Exit;
      }
    }
  }

  // ECH
  if (ch->ech.payload.data() != NULL) {
    if (ch->ech.type != Ztls_ECHCliHelloOuter) {
      ret = Ztls_AlertIllegalParameter;
      goto Exit;
    }
    if (!is_second_flight) tls->ech.state = Ztls_::EchState::Offered;
    // obtain AEAD context for opening inner CH
    if (!is_second_flight && ch->ech.payload.data() != NULL &&
      tls->ctx->ech.server.createOpener != NULL) {
      if ((tls->ech.aead = tls->ctx->ech.server.createOpener->call(
	     &tls->ech.kem, &tls->ech.cipher, tls, ch->ech.configId, ch->ech.cipherSuite,
	     ch->ech.enc, wire_bytes(ech_info_prefix, sizeof(ech_info_prefix)))) != NULL)
	tls->ech.configId = ch->ech.configId;
    }
    if (!is_second_flight) { }
    if (tls->ech.aead != NULL) {
      // now that AEAD context is available, create AAD and decrypt inner CH
      auto tagSize = tls->ech.aead->algo->tagSize;
      // Preserve the recoverable error for the previously impossible allocation size.
      if (ch->ech.payload.length() < tagSize) {
	ret = Ztls_ErrorNoMemory;
	goto Exit;
      }
      auto inner_length = ch->ech.payload.length() - tagSize;
      ech.encodedChInner.length(inner_length ? inner_length : 1);
      ech.chOuterAad.copy(
	message.data() + Ztls_HSHeaderSize, message.length() - Ztls_HSHeaderSize);
      memset(
	ech.chOuterAad.data() + (ch->ech.payload.data() - (message.data() + Ztls_HSHeaderSize)),
	0, ch->ech.payload.length());
      if (aead_decrypt(
	    tls->ech.aead, ech.encodedChInner.data(), ch->ech.payload.data(),
	    ch->ech.payload.length(), is_second_flight, ech.chOuterAad.data(),
	    ech.chOuterAad.length()) != SIZE_MAX) {
	tls->ech.state = Ztls_::EchState::Accepted;
	// successfully decrypted EncodedCHInner, build CHInner
	if ((ret = rebuild_ch_inner(
	       &ech.chInner, ech.encodedChInner.data(), ech.encodedChInner.data() + inner_length,
	       ch, message.data() + Ztls_HSHeaderSize + ch->firstExtensionAt,
	       message.data() + message.length())) != 0)
	  goto Exit;
	// treat inner ch as the message being received, re-decode it
	message = wire_bytes(ech.chInner.base, ech.chInner.off);
	extensions[0] = {UINT16_MAX};
	*ch = ClientHello{.unknownExtensions = extensions.data()};
	if ((ret = decode_client_hello(
	       tls->ctx, ch, ech.chInner.base + Ztls_HSHeaderSize,
	       ech.chInner.base + ech.chInner.off, properties, tls)) != 0)
	  goto Exit;
	if ((ret = check_cli_hello_constraints(
	       tls->ctx, ch, is_second_flight ? tls->ech.innerClientRandom : NULL, 1, message,
	       tls)) != 0)
	  goto Exit;
	if (!is_second_flight)
	  memcpy(tls->ech.innerClientRandom, ch->randomBytes, Ztls_HelloRandomSize);
      } else if (is_second_flight) {
	// decryption failure of inner CH in 2nd CH is fatal
	ret = Ztls_AlertDecryptError;
	goto Exit;
      } else {
	// decryption failure of 1st CH indicates key mismatch; dispose of AEAD context to
	// indicate adoption of outerCH
	aead_free(tls->ech.aead);
	tls->ech.aead = NULL;
      }
    }
  } else if (tls->ech.state != Ztls_::EchState::None) {
    assert(is_second_flight);
    ret = Ztls_AlertIllegalParameter;
    goto Exit;
  }

  if (tls->ctx->requireDheOnPsk) ch->psk.keModes &= ~(1u << Ztls_PSKKeModePSK);

  // handle clientRandom, legacySessionId, SNI, ESNI
  if (!is_second_flight) {
    if (ch->legacySessionId.length() != 0) tls->sendChangeCipherSpec = 1;
    Bytes serverName = {};
    if (ch->serverName.data() != NULL) serverName = ch->serverName;
    if ((ret = call_on_client_hello_cb(
	   tls, serverName, message, ch->cipherSuites, ch->alpn, ch->signatureAlgorithms,
	   ch->certCompressionAlgos,
	   {ch->serverCertificateTypes.list, ch->serverCertificateTypes.count},
	   {ch->psk.identities.list, ch->psk.identities.count}, 0)) != 0)
      goto Exit;
    if (!certificate_type_exists(
	  ch->serverCertificateTypes.list, ch->serverCertificateTypes.count,
	  tls->ctx->useRawPublicKeys ? Ztls_CertTypeRawPubkey : Ztls_CertTypeX509)) {
      ret = Ztls_AlertUnsupportedCert;
      goto Exit;
    }
  } else {
    if (ch->psk.earlyDataIndication) {
      ret = Ztls_AlertDecodeError;
      goto Exit;
    }
    // We compare SNI only when the value is saved by the onClientHello callback. This should be
    // OK because we are ignoring the value unless the callback saves the server-name.
    if (tls->serverName.data() != NULL) {
      size_t l = strlen(tls->serverName.data());
      if (!(ch->serverName.length() == l &&
	    memcmp(ch->serverName.data(), tls->serverName.data(), l) == 0)) {
	ret = Ztls_AlertHSFailure;
	goto Exit;
      }
    }
  }

  {
    // select (or check) cipher-suite, create keySchedule
    const CipherSuite *cs;
    if ((ret = select_cipher(
	   &cs, tls->ctx->cipherSuites, ch->cipherSuites.data(),
	   ch->cipherSuites.data() + ch->cipherSuites.length(), tls->ctx->serverCipherPreference,
	   tls->ctx->srvCipherChachaPriority, tls->ctx->preSharedKey.hash)) != 0)
      goto Exit;
    if (!is_second_flight) {
      tls->cipherSuite = cs;
      if ((tls->keySchedule = key_schedule_new(cs, {}, 0)) == NULL) {
	ret = Ztls_ErrorNoMemory;
	goto Exit;
      }
    } else {
      if (tls->cipherSuite != cs) {
	ret = Ztls_AlertHSFailure;
	goto Exit;
      }
    }
  }

  // select keyShare
  if (keyShare.algorithm == NULL && ch->keyShares.data() != NULL &&
    tls->ctx->keyExchanges != NULL) {
    const uint8_t *src = ch->keyShares.data(), *const end = src + ch->keyShares.length();
    Ztls_DecodeBlock(src, end, 2, {
      if ((ret = select_key_share(
	     &keyShare.algorithm, &keyShare.peerKey, tls->ctx->keyExchanges, &src, end, 0)) != 0)
	goto Exit;
    });
  }

  if (!is_second_flight) {
    if (ch->cookie.all.length() != 0 && keyShare.algorithm != NULL) {
      {
	// use cookie to check the integrity of the handshake, and update the context
	uint8_t sig[Ztls_MaxDigestSize];
	size_t	sigsize = tls->ctx->cipherSuites[0]->hash->digestSize;
	if ((ret = calc_cookie_signature(
	       tls, properties, keyShare.algorithm, ch->cookie.tbs, sig)) != 0)
	  goto Exit;
	if (!(ch->cookie.signature.length() == sigsize &&
	      Ztls::ctEqual({ch->cookie.signature.data(), sigsize}, {sig, sigsize}))) {
	  ret = Ztls_AlertHSFailure;
	  goto Exit;
	}
      }
      // integrity check passed; update states
      key_sched_ch1hash_prefix(tls->keySchedule);
      _key_schedule_update_hash(
	tls->keySchedule, ch->cookie.ch1Hash.data(), ch->cookie.ch1Hash.length(), 0);
      key_schedule_extract(
	tls->keySchedule, tls->ctx->preSharedKey.secret
	// this argument will be a zero-length vector unless external PSK is used, and that's
	// fine; we never resume when sending HRR
      );
      // ... reusing sendbuf to rebuild HRR for hash calculation
      size_t hrr_start = emitter->buf->off;
      Ztls_EmitHelloRetryRequest(
	tls->keySchedule, ch->cookie.sentKeyShare ? keyShare.algorithm : NULL,
	{
	  Ztls_BufferPushExtension(emitter->buf, Ztls_ExtTypeCookie, {
	    Ztls_BufferPushv(emitter->buf, ch->cookie.all.data(), ch->cookie.all.length());
	  });
	},
	{});
      emitter->buf->off = hrr_start;
      is_second_flight = 1;

    } else if (ch->keyShares.data() != NULL && tls->ctx->keyExchanges != NULL &&
      (keyShare.algorithm == NULL || (properties != NULL && properties->server.enforceRetry))) {
      // send HelloRetryRequest, when trying to negotiate the key share but enforced by config or
      // upon key-share mismatch
      if (ch->negotiatedGroups.data() == NULL) {
	ret = Ztls_AlertMissingExt;
	goto Exit;
      }
      const KeyExchangeAlgo *negotiated_group;
      if ((ret = select_negotiated_group(
	     &negotiated_group, tls->ctx->keyExchanges, ch->negotiatedGroups.data(),
	     ch->negotiatedGroups.data() + ch->negotiatedGroups.length())) != 0)
	goto Exit;
      _key_schedule_update_hash(tls->keySchedule, message.data(), message.length(), 0);
      assert(tls->keySchedule->generation == 0);

      // Either send a stateless retry (w. cookies) or a stateful one. When sending the latter,
      // run the state machine. At the moment, stateless retry is disabled when ECH is used (do we
      // need to support it?).
      int retryUsesCookie = properties != NULL && properties->server.retryUsesCookie &&
	!is_ech_handshake(tls, NULL, NULL, NULL);
      if (!retryUsesCookie) {
	key_sched_post_ch1hash(tls->keySchedule);
	key_schedule_extract(
	  tls->keySchedule, tls->ctx->preSharedKey.secret
	  // see comment above
	);
      }
      size_t ech_confirm_off = 0;
      Ztls_EmitHelloRetryRequest(
	tls->keySchedule, keyShare.algorithm != NULL ? NULL : negotiated_group,
	{
	  Buffer *sendbuf = emitter->buf;
	  if (is_ech_handshake(tls, NULL, NULL, NULL)) {
	    Ztls_BufferPushExtension(sendbuf, Ztls_ExtTypeEncCliHello, {
	      if ((ret = buffer_reserve(sendbuf, Ztls_ECHConfirmLength, 1)) != 0) goto Exit;
	      memset(sendbuf->base + sendbuf->off, 0, Ztls_ECHConfirmLength);
	      ech_confirm_off = sendbuf->off;
	      sendbuf->off += Ztls_ECHConfirmLength;
	    });
	  }
	  if (retryUsesCookie) {
	    Ztls_BufferPushExtension(sendbuf, Ztls_ExtTypeCookie, {
	      Ztls_BufferPushBlock(sendbuf, 2, {
		// push to-be-signed data
		size_t tbs_start = sendbuf->off;
		Ztls_BufferPushBlock(sendbuf, 2, {
		  // first block of the cookie data is the hash(ch1)
		  Ztls_BufferPushBlock(sendbuf, 1, {
		    size_t sz = tls->cipherSuite->hash->digestSize;
		    if ((ret = buffer_reserve(sendbuf, sz, 1)) != 0) goto Exit;
		    key_schedule_extract_ch1hash(tls->keySchedule, sendbuf->base + sendbuf->off);
		    sendbuf->off += sz;
		  });
		  // second is if we have sent keyShare extension
		  Ztls_BufferPush_1(sendbuf, keyShare.algorithm == NULL);
		  // we can add more data here
		});
		size_t tbs_len = sendbuf->off - tbs_start;
		// push the signature
		Ztls_BufferPushBlock(sendbuf, 1, {
		  size_t sz = tls->ctx->cipherSuites[0]->hash->digestSize;
		  if ((ret = buffer_reserve(sendbuf, sz, 1)) != 0) goto Exit;
		  if ((ret = calc_cookie_signature(
			 tls, properties, negotiated_group,
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
	       tls->keySchedule, emitter->buf->base + ech_confirm_off, tls->ech.innerClientRandom,
	       Ztls_ECHConfirmationHRR,
	       wire_bytes(
		 emitter->buf->base + sh_start_off, emitter->buf->off - sh_start_off))) != 0)
	    goto Exit;
	});
      if (retryUsesCookie) {
	if ((ret = push_change_cipher_spec(tls, emitter)) != 0) goto Exit;
	ret = Ztls_ErrorStatelessRetry;
      } else {
	tls->state = Ztls_::State::SrvWait2ndCliHello;
	if (ch->psk.earlyDataIndication) tls->earlyDataSkippedBytes = 0;
	ret = Ztls_ErrorInProgress;
      }
      goto Exit;
    }
  }

  // handle unknown extensions
  if ((ret = report_unknown_extensions(tls, properties, ch->unknownExtensions)) != 0) goto Exit;

  // try psk handshake
  if (ch->psk.hashEnd != 0 &&
    (ch->psk.keModes & ((1u << Ztls_PSKKeModePSK) | (1u << Ztls_PSKKeModePSKDHE))) != 0 &&
    !tls->ctx->requireCliAuth &&
    ((!is_second_flight && tls->ctx->encryptTicket != NULL) ||
      tls->ctx->preSharedKey.identity.data() != NULL)) {
    if (
      (ret = try_psk_handshake(
	 tls, &psk_index, &accept_early_data, ch,
	 wire_bytes(message.data(), ch->psk.hashEnd - message.data()), is_second_flight)) != 0) {
      goto Exit;
    }
  }

  // If the server was setup to use an external PSK but failed to agree, abort the handshake.
  // Because external PSK is a form of mutual authentication, it makes sense to abort (at least as
  // the default).
  if (tls->ctx->preSharedKey.identity.data() != NULL && psk_index == SIZE_MAX) {
    ret = Ztls_AlertUnknownPSKId;
    goto Exit;
  }

  // If client authentication is enabled, we always force a full handshake. TODO: Check for
  // `post_handshake_auth` extension and if that is present, do not force full handshake! Remove
  // also the check `!requireCliAuth` above.

  // adjust keySchedule, determine handshake mode
  if (psk_index == SIZE_MAX || tls->ctx->requireCliAuth) {
    _key_schedule_update_hash(tls->keySchedule, message.data(), message.length(), 0);
    if (!is_second_flight) {
      assert(tls->keySchedule->generation == 0);
      key_schedule_extract(tls->keySchedule, wire_bytes(NULL, 0));
    }
    mode = HANDSHAKE_MODE_FULL;
    if (properties != NULL) properties->server.selectedPskBinder.len = 0;
  } else {
    _key_schedule_update_hash(
      tls->keySchedule, ch->psk.hashEnd, message.data() + message.length() - ch->psk.hashEnd, 0);
    if ((ch->psk.keModes & (1u << Ztls_PSKKeModePSK)) != 0) {
      mode = HANDSHAKE_MODE_PSK;
    } else {
      assert((ch->psk.keModes & (1u << Ztls_PSKKeModePSKDHE)) != 0);
      mode = HANDSHAKE_MODE_PSK_DHE;
    }
    tls->isPskHandshake = 1;
    if (properties != NULL) {
      Bytes *selected = &ch->psk.identities.list[psk_index].binder;
      memcpy(properties->server.selectedPskBinder.base, selected->data(), selected->length());
      properties->server.selectedPskBinder.len = selected->length();
    }
  }

  // determine number of tickets to send
  if (ch->psk.keModes != 0 && tls->ctx->ticketLifetime != 0) {
    if (ch->ticketRequest.newSessionCount != 0) {
      tls->numTicketsToSend = tls->isPskHandshake ? ch->ticketRequest.resumptionCount :
						    ch->ticketRequest.newSessionCount;
    } else {
      tls->numTicketsToSend = 1;
    }
    uint8_t max_tickets = tls->ctx->ticketRequests.maxCount;
    if (max_tickets == 0) max_tickets = ZtlsMaxTickets;
    if (tls->numTicketsToSend > max_tickets) tls->numTicketsToSend = max_tickets;
  } else {
    tls->numTicketsToSend = 0;
  }

  if (accept_early_data && tls->ctx->maxEarlyDataSize != 0 && psk_index == 0) {
    tls->pendingHandshakeSecret.length(Ztls_MaxDigestSize);
    if ((ret = derive_exporter_secret(tls, 1)) != 0) goto Exit;
    if ((ret = setup_traffic_protection(tls, 0, "c e traffic", 1, 0, 0)) != 0) goto Exit;
  }

  // run key-exchange, to obtain pubkey and secret
  if (mode != HANDSHAKE_MODE_PSK) {
    if (keyShare.algorithm == NULL) {
      ret = ch->keyShares.data() != NULL ? Ztls_AlertHSFailure : Ztls_AlertMissingExt;
      goto Exit;
    }
    if ((ret = keyShare.algorithm->exchange(&pubkey, &ecdh_secret, keyShare.peerKey)) != 0) {
      assert(!pubkey.length());
      assert(!ecdh_secret.length());
      goto Exit;
    }
    tls->keyShare = keyShare.algorithm;
  }

  {
    // send ServerHello
    size_t ech_confirm_off = 0;
    Ztls_EmitServerHello(
      tls->keySchedule,
      {
	tls->ctx->random->fill(emitter->buf->base + emitter->buf->off, Ztls_HelloRandomSize);
	// when accepting CHInner, last 8 byte of SH.random is zero for the handshake transcript
	if (is_ech_handshake(tls, NULL, NULL, NULL)) {
	  ech_confirm_off = emitter->buf->off + Ztls_HelloRandomSize - Ztls_ECHConfirmLength;
	  memset(emitter->buf->base + ech_confirm_off, 0, Ztls_ECHConfirmLength);
	}
      },
      {
	Buffer *sendbuf = emitter->buf;
	if (mode != HANDSHAKE_MODE_PSK) {
	  Ztls_BufferPushExtension(sendbuf, Ztls_ExtTypeKeyShare, {
	    Ztls_BufferPush16(sendbuf, keyShare.algorithm->id);
	    Ztls_BufferPushBlock(
	      sendbuf, 2, { Ztls_BufferPushv(sendbuf, pubkey.data(), pubkey.length()); });
	  });
	}
	if (mode != HANDSHAKE_MODE_FULL) {
	  Ztls_BufferPushExtension(sendbuf, Ztls_ExtTypePreSharedKey, {
	    Ztls_BufferPush16(sendbuf, (uint16_t)psk_index);
	  });
	}
      },
      {
	if (ech_confirm_off != 0 &&
	  (ret = ech_calc_confirmation(
	     tls->keySchedule, emitter->buf->base + ech_confirm_off, tls->ech.innerClientRandom,
	     Ztls_ECHConfirmationSrvHello,
	     wire_bytes(emitter->buf->base + sh_start_off, emitter->buf->off - sh_start_off))) !=
	    0)
	  goto Exit;
      });
  }

  // processing of ECH is complete; dispose state
  clear_ech(&tls->ech, 1);

  if ((ret = push_change_cipher_spec(tls, emitter)) != 0) goto Exit;

  // create protection contexts for the handshake
  assert(tls->keySchedule->generation == 1);
  key_schedule_extract(tls->keySchedule, wire_bytes(ecdh_secret.data(), ecdh_secret.length()));
  if ((ret = setup_traffic_protection(tls, 1, "s hs traffic", 2, 0, 0)) != 0) goto Exit;
  if (tls->pendingHandshakeSecret.data() != NULL) {
    if ((ret = derive_secret(
	   tls->keySchedule, tls->pendingHandshakeSecret.data(), "c hs traffic")) != 0)
      goto Exit;
    if (tls->ctx->updateTrafficKey != NULL &&
      (ret = tls->ctx->updateTrafficKey->call(tls, 0, 2, tls->pendingHandshakeSecret.data())) !=
	0)
      goto Exit;
  } else {
    if ((ret = setup_traffic_protection(tls, 0, "c hs traffic", 2, 0, 0)) != 0) goto Exit;
    if (ch->psk.earlyDataIndication) tls->earlyDataSkippedBytes = 0;
  }

  // send EncryptedExtensions
  Ztls_PushMessage(emitter, tls->keySchedule, Ztls_HsTypeEncExts, {
    Buffer *sendbuf = emitter->buf;
    Ztls_BufferPushBlock(sendbuf, 2, {
      if (tls->serverName.data() != NULL) {
	// In this event, the server SHALL include an extension of type "serverName" in the
	// (extended) server hello. The "extension_data" field of this extension SHALL be empty.
	// (RFC 6066 section 3)
	Ztls_BufferPushExtension(sendbuf, Ztls_ExtTypeSrvName, {});
      }
      if (tls->ctx->useRawPublicKeys) {
	Ztls_BufferPushExtension(sendbuf, Ztls_ExtTypeSrvCertType, {
	  Ztls_BufferPush_1(sendbuf, Ztls_CertTypeRawPubkey);
	});
      }
      if (tls->negotiatedProtocol.data() != NULL) {
	Ztls_BufferPushExtension(sendbuf, Ztls_ExtTypeALPN, {
	  Ztls_BufferPushBlock(sendbuf, 2, {
	    Ztls_BufferPushBlock(sendbuf, 1, {
	      Ztls_BufferPushv(
		sendbuf, tls->negotiatedProtocol.data(), strlen(tls->negotiatedProtocol.data()));
	    });
	  });
	});
      }
      if (tls->pendingHandshakeSecret.data() != NULL)
	Ztls_BufferPushExtension(sendbuf, Ztls_ExtTypeEarly, {});
      // send ECH retryConfigs, if ECH was offered by rejected, even though we (the server) could
      // have accepted ECH
      if (tls->ech.state == Ztls_::EchState::Offered &&
	tls->ctx->ech.server.createOpener != NULL &&
	tls->ctx->ech.server.retryConfigs.length() != 0)
	Ztls_BufferPushExtension(sendbuf, Ztls_ExtTypeEncCliHello, {
	  Ztls_BufferPushv(
	    sendbuf, tls->ctx->ech.server.retryConfigs.data(),
	    tls->ctx->ech.server.retryConfigs.length());
	});
      if (ch->ticketRequest.newSessionCount != 0 && tls->numTicketsToSend != 0)
	Ztls_BufferPushExtension(sendbuf, Ztls_ExtTypeTicketRequest, {
	  Ztls_BufferPush_1(sendbuf, tls->numTicketsToSend);
	});
      if ((ret = push_additional_extensions(properties, sendbuf)) != 0) goto Exit;
    });
  });

  if (mode == HANDSHAKE_MODE_FULL) {
    // send certificate request if client authentication is activated
    if (tls->ctx->requireCliAuth) {
      Ztls_PushMessage(emitter, tls->keySchedule, Ztls_HsTypeCertRequest, {
	Buffer *sendbuf = emitter->buf;
	// certificate_request_context: this field SHALL be zero length, unless the certificate
	// request is used for post- handshake authentication.
	Ztls_BufferPush_1(sendbuf, 0);
	// extensions
	Ztls_BufferPushBlock(sendbuf, 2, {
	  Ztls_BufferPushExtension(sendbuf, Ztls_ExtTypeSigAlgos, {
	    if ((ret = push_signature_algorithms(tls->ctx->verifyCertificate, sendbuf)) != 0)
	      goto Exit;
	  });
	  // certificate authorities entension
	  if (tls->ctx->clientCaNames.length() > 0) {
	    Ztls_BufferPushExtension(sendbuf, Ztls_ExtCertAuths, {
	      Ztls_BufferPushBlock(sendbuf, 2, {
		for (auto name : tls->ctx->clientCaNames) {
		  Ztls_BufferPushBlock(
		    sendbuf, 2, { Ztls_BufferPushv(sendbuf, name.data(), name.length()); });
		}
	      });
	    });
	  }
	});
      });

      if (ret != 0) { goto Exit; }
    }

    // send certificate
    if ((ret = send_certificate(
	   tls, emitter, &ch->signatureAlgorithms, wire_bytes(NULL, 0), ch->statusRequest,
	   ch->certCompressionAlgos)) != 0)
      goto Exit;
    // send certificateverify, finished, and complete the handshake
    if ((ret = server_finish_handshake(tls, emitter, 1, &ch->signatureAlgorithms)) != 0)
      goto Exit;
  } else {
    // send finished, and complete the handshake
    if ((ret = server_finish_handshake(tls, emitter, 0, NULL)) != 0) goto Exit;
  }

Exit:
  buffer_dispose(&ech.chInner);
  return ret;

#undef Ztls_EmitServerHello
#undef Ztls_EmitHelloRetryRequest
}

static int server_finish_handshake(
  Ztls::Cxn *tls, MessageEmitter *emitter, int send_cert_verify,
  SignatureAlgorithms *signatureAlgorithms)
{
  int ret;

  if (send_cert_verify) {
    if ((ret = send_certificate_verify(
	   tls, emitter, signatureAlgorithms, Ztls_SrvCertVerifyCtx)) != 0) {
      if (ret == Ztls_ErrorAsyncOperation) { tls->state = Ztls_::State::SrvBuildCertSig; }
      goto Exit;
    }
  }

  if ((ret = send_finished(tls, emitter)) != 0) goto Exit;

  assert(tls->keySchedule->generation == 2);
  if ((ret = key_schedule_extract(tls->keySchedule, wire_bytes(NULL, 0))) != 0) goto Exit;
  if ((ret = setup_traffic_protection(tls, 1, "s ap traffic", 3, 0, 0)) != 0) goto Exit;
  if ((ret = derive_secret(tls->keySchedule, tls->pendingTrafficSecret, "c ap traffic")) != 0)
    goto Exit;
  if ((ret = derive_exporter_secret(tls, 0)) != 0) goto Exit;

  if (tls->pendingHandshakeSecret.data() != NULL) {
    if (tls->ctx->omitEndOfEarlyData) {
      if ((ret = commission_handshake_secret(tls)) != 0) goto Exit;
      tls->state = Ztls_::State::SrvWaitFinished;
    } else {
      tls->state = Ztls_::State::SrvWaitEarlyEnd;
    }
  } else if (tls->ctx->requireCliAuth) {
    tls->state = Ztls_::State::SrvExpectCert;
  } else {
    tls->state = Ztls_::State::SrvWaitFinished;
  }

  // send session ticket if necessary
  if (tls->numTicketsToSend != 0) {
    assert(tls->ctx->ticketLifetime != 0);
    for (uint8_t i = 0; i < tls->numTicketsToSend; ++i)
      if ((ret = send_session_ticket(tls, emitter)) != 0) goto Exit;
  }

  if (tls->ctx->requireCliAuth) {
    ret = Ztls_ErrorInProgress;
  } else {
    ret = 0;
  }

Exit:
  return ret;
}

static int srv_handle_end_of_early_data(Ztls::Cxn *tls, Bytes message)
{
  int ret;

  if ((ret = commission_handshake_secret(tls)) != 0) goto Exit;

  _key_schedule_update_hash(tls->keySchedule, message.data(), message.length(), 0);
  tls->state = Ztls_::State::SrvWaitFinished;
  ret = Ztls_ErrorInProgress;

Exit:
  return ret;
}

static int server_handle_finished(Ztls::Cxn *tls, Bytes message)
{
  int ret;

  if ((ret = verify_finished(tls, message)) != 0) return ret;

  memcpy(tls->rxTraffic.secret, tls->pendingTrafficSecret, sizeof(tls->pendingTrafficSecret));
  ZuClear(tls->pendingTrafficSecret, sizeof(tls->pendingTrafficSecret));
  if ((ret = setup_traffic_protection(tls, 0, NULL, 3, 0, 0)) != 0) return ret;

  _key_schedule_update_hash(tls->keySchedule, message.data(), message.length(), 0);

  tls->state = Ztls_::State::SrvPostHS;
  return 0;
}

static int updateTrafficKey(Ztls::Cxn *tls, int is_enc)
{
  TrafficProtection *tp = is_enc ? &tls->txTraffic : &tls->rxTraffic;
  uint8_t	     secret[Ztls_MaxDigestSize];
  int		     ret;

  const HashAlgo *hash = tls->cipherSuite->hash;
  if ((ret = hkdf_expand_label(
	 hash, secret, hash->digestSize, wire_bytes(tp->secret, hash->digestSize), "traffic upd",
	 wire_bytes(NULL, 0), NULL)) != 0)
    goto Exit;
  memcpy(tp->secret, secret, sizeof(secret));
  ret = setup_traffic_protection(tls, is_enc, NULL, 3, 0, 1);

Exit:
  ZuClear(secret, sizeof(secret));
  return ret;
}

static int handle_key_update(Ztls::Cxn *tls, MessageEmitter *emitter, Bytes message)
{
  const uint8_t	      *src = message.data() + Ztls_HSHeaderSize,
		*const end = message.data() + message.length();
  int		       ret;

  // validate
  if (end - src != 1 || *src > 1) return Ztls_AlertDecodeError;

  // update receive key
  if ((ret = updateTrafficKey(tls, 0)) != 0) return ret;

  if (*src) {
    if (tls->ctx->updateTrafficKey != NULL) return Ztls_AlertUnwaitedMsg;
    if (tls->txHandedOff) {
      if (tls->keyUpdateRequests == UINT32_MAX) return Ztls_AlertInternalError;
      ++tls->keyUpdateRequests;
    } else {
      tls->needsKeyUpdate = 1;
    }
  }

  return 0;
}

static int parse_record_header(Record *rec, const uint8_t *src)
{
  rec->type = src[0];
  rec->version = load_be<uint16_t>(src + 1);
  rec->length = load_be<uint16_t>(src + 3);

  if (rec->length > (size_t)(rec->type == Ztls_ContentTypeAppdata ? Ztls_MaxEncRecordSize :
								    Ztls_MaxPlainRecordSize))
    return Ztls_AlertDecodeError;

  return 0;
}

static int parse_record(Ztls::Cxn *tls, Record *rec, const uint8_t *src, size_t *len)
{
  int ret;

  assert(*len != 0);

  // Check if the first byte is something that we can handle, otherwise do not bother parsing /
  // buffering the entire record as it is obviously broken. SSL 2.0 handshakes fall into this path
  // as well.
  if (tls->recvbuf.rec.base == NULL) {
    uint8_t type = src[0];
    switch (type) {
    case Ztls_ContentChangeCipher:
    case Ztls_ContentTypeAlert:
    case Ztls_ContentTypeHS:
    case Ztls_ContentTypeAppdata:
      break;
    default:
      return Ztls_AlertDecodeError;
    }
  }

  if (tls->recvbuf.rec.base == NULL && *len >= 5) {
    // fast path
    if ((ret = parse_record_header(rec, src)) != 0) return ret;
    if (5 + rec->length <= *len) {
      rec->fragment = src + 5;
      *len = rec->length + 5;
      return 0;
    }
  }

  // slow path
  const uint8_t *const end = src + *len;
  *rec = (Record){0};

  if (tls->recvbuf.rec.base == NULL) {
    buffer_init_rx(&tls->recvbuf.rec, const_cast<char *>(""), 0);
    if ((ret = buffer_reserve(&tls->recvbuf.rec, 5, 0)) != 0) return ret;
  }

  // fill and parse the header
  while (tls->recvbuf.rec.off < 5) {
    if (src == end) return Ztls_ErrorInProgress;
    tls->recvbuf.rec.base[tls->recvbuf.rec.off++] = *src++;
  }
  if ((ret = parse_record_header(rec, tls->recvbuf.rec.base)) != 0) return ret;

  // fill the fragment
  size_t addlen = rec->length + 5 - tls->recvbuf.rec.off;
  if (addlen != 0) {
    if ((ret = buffer_reserve(&tls->recvbuf.rec, addlen, 0)) != 0) return ret;
    if (addlen > (size_t)(end - src)) addlen = end - src;
    if (addlen != 0) {
      memcpy(tls->recvbuf.rec.base + tls->recvbuf.rec.off, src, addlen);
      tls->recvbuf.rec.off += addlen;
      src += addlen;
    }
  }

  // set rec->fragment if a complete record has been parsed
  if (tls->recvbuf.rec.off == rec->length + 5) {
    rec->fragment = tls->recvbuf.rec.base + 5;
    ret = 0;
  } else {
    ret = Ztls_ErrorInProgress;
  }

  *len -= end - src;
  return ret;
}

static void stream_discard(Ztls::Cxn *tls)
{
  StreamRecord *r = &tls->streamRec;
  if (r->begun && tls->rxTraffic.aead != NULL && tls->rxTraffic.aead->serialDecrypt())
    tls->rxTraffic.aead->decAbort();
  if (r->reserved) tls->plaintext.discard();
  ZuClear(r, sizeof(*r));
}

} // namespace Ztls_

namespace Ztls {

void Cxn::initialize(Ztls_::Context *context, bool server, void *owner)
{
  using namespace Ztls_;
  retireRx();
  retireTx();
  ctx = context;
  isServer = server;
  dataPtr = owner;
  cipherSuite = nullptr;
  rxActive = true;
  isPskHandshake = false;
  sendChangeCipherSpec = ctx->sendChangeCipherSpec;
  txHandedOff = false;
  keyUpdateRequests = 0;
  offeredPsk = false;
  usingEarlyData = false;
  earlyDataSkippedBytes = UINT32_MAX;
  numTicketsToSend = 0;
  keyShare = nullptr;
  ech = {};
  legacySessionId = {};

  assert(ctx->getTime && "set the default or custom TLS clock");
  if (ctx->preSharedKey.identity.data()) {
    assert(
      ctx->preSharedKey.identity.length() && ctx->preSharedKey.secret.data() &&
      ctx->preSharedKey.secret.length() && ctx->preSharedKey.hash);
  } else {
    assert(
      !ctx->preSharedKey.identity.length() && !ctx->preSharedKey.secret.data() &&
      !ctx->preSharedKey.secret.length() && !ctx->preSharedKey.hash);
  }
  if (server) {
    state = Ztls_::State::SrvWaitCliHello;
    return;
  }
  state = Ztls_::State::CliHSStart;
  ctx->random->fill(clientRandom, sizeof(clientRandom));
  if (sendChangeCipherSpec) {
    legacySessionId = wire_bytes(legacySessionIdBuf, sizeof(legacySessionIdBuf));
    ctx->random->fill(legacySessionId.data(), legacySessionId.length());
  }
}

void Cxn::retireRx()
{
  using namespace Ztls_;
  if (!rxActive) return;
  stream_discard(this);
  plaintext.clear();
  rxInput = nullptr;
  rxPaused = false;
  buffer_dispose(&recvbuf.rec);
  buffer_dispose(&recvbuf.mess);
  if (keySchedule) {
    key_schedule_free(keySchedule);
    keySchedule = nullptr;
  }
  if (rxTraffic.aead) aead_free(rxTraffic.aead);
  ZuClear(&rxTraffic, sizeof(rxTraffic));
  clear_ech(&ech, isServer);
  retireSignature();
  delete keyShareCtx;
  keyShareCtx = nullptr;
  clearVerifier();
  certificateRequest = {};
  certificateRequestBytes.null();
  pendingHandshakeSecret.null();
  exporterMasterSecret.early.null();
  exporterMasterSecret.oneRtt.null();
  serverName.null();
  negotiatedProtocol.null();
  ZuClear(pendingTrafficSecret, sizeof(pendingTrafficSecret));
  ZuClear(clientRandom, sizeof(clientRandom));
  ZuClear(legacySessionIdBuf, sizeof(legacySessionIdBuf));
  handshakeProps = {};
  rxActive = false;
}

bool Cxn::retireSignature()
{
  if (signatureRetire && !signatureRetire()) return false;
  signatureRetire = {};
  signatureResume = {};
  signatureFd = -1;
  return true;
}

void Cxn::retireTx()
{
  if (txTraffic.aead) Ztls_::aead_free(txTraffic.aead);
  ZuClear(&txTraffic, sizeof(txTraffic));
  needsKeyUpdate = false;
  keyUpdateSendRequest = false;
  txClosing = false;
  txGeneration = 0;
}

Cxn::~Cxn()
{
  retireRx();
  retireTx();
}

} // namespace Ztls

namespace Ztls_ {

Context *get_context(Ztls::Cxn *tls) { return tls->ctx; }

void set_context(Ztls::Cxn *tls, Context *ctx) { tls->ctx = ctx; }

Bytes get_client_random(Ztls::Cxn *tls)
{ return wire_bytes(tls->clientRandom, Ztls_HelloRandomSize); }

const CipherSuite *get_cipher(Ztls::Cxn *tls) { return tls->cipherSuite; }

uint16_t get_protocol_version(Ztls::Cxn *tls)
{
  (void)tls;
  return Ztls_ProtoVerTls13;
}

int get_traffic_keys(Ztls::Cxn *tls, int is_enc, uint8_t *key, uint8_t *iv, uint64_t *seq)
{
  TrafficProtection *ctx = is_enc ? &tls->txTraffic : &tls->rxTraffic;
  int		     ret;

  if (is_enc && tls->txHandedOff) return Ztls_ErrorNotAvailable;

  if ((ret = get_traffic_keys(
	 tls->cipherSuite->aead, tls->cipherSuite->hash, key, iv, ctx->secret,
	 wire_bytes(NULL, 0), NULL)) != 0)
    return ret;
  *seq = ctx->seq;
  return 0;
}

const char *get_server_name(Ztls::Cxn *tls) { return tls->serverName.data(); }

int set_server_name(Ztls::Cxn *tls, ZuCSpan serverName)
{
  assign_name(tls->serverName, serverName);
  return 0;
}

const char *get_negotiated_protocol(Ztls::Cxn *tls) { return tls->negotiatedProtocol.data(); }

int set_negotiated_protocol(Ztls::Cxn *tls, ZuCSpan protocol)
{
  assign_name(tls->negotiatedProtocol, protocol);
  return 0;
}

int handshake_is_complete(Ztls::Cxn *tls) { return tls->state >= Ztls_::State::PostHSMin; }

int isPskHandshake(Ztls::Cxn *tls) { return tls->isPskHandshake; }

int is_ech_handshake(
  Ztls::Cxn *tls, uint8_t *configId, const HpkeKem **kem, const HpkeSuite **cipher)
{
  if (tls->ech.state == Ztls_::EchState::Accepted) {
    if (configId != NULL) *configId = tls->ech.configId;
    if (kem != NULL) *kem = tls->ech.kem;
    if (cipher != NULL) *cipher = tls->ech.cipher;
    return 1;
  }
  return 0;
}

void **get_data_ptr(Ztls::Cxn *tls) { return &tls->dataPtr; }

static int handle_cli_hs_message(
  Ztls::Cxn *tls, MessageEmitter *emitter, Bytes message, int is_end_of_record,
  HandshakeProps *properties)
{
  uint8_t type = message.data()[0];
  int	  ret;

  switch (tls->state) {
  case Ztls_::State::CliWaitSrvHello:
  case Ztls_::State::CliWait2ndSrvHello:
    if (type == Ztls_HsTypeSrvHello && is_end_of_record) {
      ret = client_handle_hello(tls, emitter, message, properties);
    } else {
      ret = Ztls_AlertUnwaitedMsg;
    }
    break;
  case Ztls_::State::CliWaitEncExts:
    if (type == Ztls_HsTypeEncExts) {
      ret = cli_handle_enc_exts(tls, message, properties);
    } else {
      ret = Ztls_AlertUnwaitedMsg;
    }
    break;
  case Ztls_::State::CliWaitCertReq:
    if (type == Ztls_HsTypeCertRequest) {
      ret = cli_handle_cert_request(tls, message, properties);
      break;
    }
  // fall through
  case Ztls_::State::CliExpectCert:
    switch (type) {
    case Ztls_HsTypeCert:
      ret = client_handle_certificate(tls, message);
      break;
    case Ztls_HsTypeCompressedCert:
      ret = cli_handle_compressed_cert(tls, message);
      break;
    default:
      ret = Ztls_AlertUnwaitedMsg;
      break;
    }
    break;
  case Ztls_::State::CliWaitCertSig:
    if (type == Ztls_HsTypeCertVerify) {
      ret = cli_handle_cert_verify(tls, message);
    } else {
      ret = Ztls_AlertUnwaitedMsg;
    }
    break;
  case Ztls_::State::CliWaitFinished:
    if (type == Ztls_HSTypeFinished && is_end_of_record) {
      ret = client_handle_finished(tls, emitter, message);
    } else {
      ret = Ztls_AlertUnwaitedMsg;
    }
    break;
  case Ztls_::State::CliPostHS:
    switch (type) {
    case Ztls_HsTypeNewTicket:
      ret = cli_handle_new_ticket(tls, message);
      break;
    case Ztls_HsTypeKeyUpdate:
      ret = handle_key_update(tls, emitter, message);
      break;
    default:
      ret = Ztls_AlertUnwaitedMsg;
      break;
    }
    break;
  default:
    assert(!"unexpected state");
    ret = Ztls_AlertInternalError;
    break;
  }

  return ret;
}

static int handle_srv_hs_message(
  Ztls::Cxn *tls, MessageEmitter *emitter, Bytes message, int is_end_of_record,
  HandshakeProps *properties)
{
  uint8_t type = message.data()[0];
  int	  ret;

  switch (tls->state) {
  case Ztls_::State::SrvWaitCliHello:
  case Ztls_::State::SrvWait2ndCliHello:
    if (type == Ztls_HsTypeCliHello && is_end_of_record) {
      ret = server_handle_hello(tls, emitter, message, properties);
    } else {
      ret = Ztls_AlertHSFailure;
    }
    break;
  case Ztls_::State::SrvExpectCert:
    if (type == Ztls_HsTypeCert) {
      ret = server_handle_certificate(tls, message);
    } else {
      ret = Ztls_AlertUnwaitedMsg;
    }
    break;
  case Ztls_::State::SrvWaitCertSig:
    if (type == Ztls_HsTypeCertVerify) {
      ret = srv_handle_cert_verify(tls, message);
    } else {
      ret = Ztls_AlertUnwaitedMsg;
    }
    break;
  case Ztls_::State::SrvWaitEarlyEnd:
    assert(!tls->ctx->omitEndOfEarlyData);
    if (type == Ztls_HsTypeEndOfEarly) {
      ret = srv_handle_end_of_early_data(tls, message);
    } else {
      ret = Ztls_AlertUnwaitedMsg;
    }
    break;
  case Ztls_::State::SrvWaitFinished:
    if (type == Ztls_HSTypeFinished && is_end_of_record) {
      ret = server_handle_finished(tls, message);
    } else {
      ret = Ztls_AlertHSFailure;
    }
    break;
  case Ztls_::State::SrvPostHS:
    switch (type) {
    case Ztls_HsTypeKeyUpdate:
      ret = handle_key_update(tls, emitter, message);
      break;
    default:
      ret = Ztls_AlertUnwaitedMsg;
      break;
    }
    break;
  default:
    assert(!"unexpected state");
    ret = Ztls_AlertInternalError;
    break;
  }

  return ret;
}

static int handle_alert(Ztls::Cxn *tls, const uint8_t *src, size_t len)
{
  if (len != 2) return Ztls_AlertDecodeError;

  uint8_t desc = src[1];

  // all fatal alerts and USER_CANCELLED warning tears down the connection immediately, regardless
  // of the transmitted level
  return Ztls_AlertToPeerError(desc);
}

static int message_buffer_is_overflow(Context *ctx, size_t size)
{
  if (ctx->maxBufferSize == 0) return 0;
  if (size <= ctx->maxBufferSize) return 0;
  return 1;
}

static int handle_handshake_messages(
  Ztls::Cxn *tls, MessageEmitter *emitter, ZuBSpan input, HandshakeProps *properties,
  bool *keyUpdateRejected = nullptr)
{
  auto dispatch = tls->isServer ? handle_srv_hs_message : handle_cli_hs_message;

  // flatten the unhandled messages
  const uint8_t *src, *src_end;
  if (tls->recvbuf.mess.base == NULL) {
    src = input.data();
    src_end = src + input.length();
  } else {
    if (message_buffer_is_overflow(tls->ctx, tls->recvbuf.mess.off + input.length()))
      return Ztls_AlertHSFailure;
    if (int ret = buffer_reserve(&tls->recvbuf.mess, input.length(), 0)) return ret;
    memcpy(tls->recvbuf.mess.base + tls->recvbuf.mess.off, input.data(), input.length());
    tls->recvbuf.mess.off += input.length();
    src = tls->recvbuf.mess.base;
    src_end = src + tls->recvbuf.mess.off;
  }

  // handle the messages
  int ret = Ztls_ErrorInProgress;
  while (src_end - src >= 4) {
    size_t mess_len = 4 + load_be24(src + 1);
    if (src_end - src < (int)mess_len) break;
    if (keyUpdateRejected && *src == Ztls_HsTypeKeyUpdate) {
      *keyUpdateRejected = true;
      ret = Ztls_AlertUnwaitedMsg;
    } else {
      ret =
	dispatch(tls, emitter, wire_bytes(src, mess_len), src_end - src == mess_len, properties);
    }
    switch (ret) {
    case 0:
    case Ztls_ErrorAsyncOperation:
    case Ztls_ErrorInProgress:
      break;
    default:
      tls->clearVerifier();
      buffer_dispose(&tls->recvbuf.mess);
      return ret;
    }
    src += mess_len;
  }

  // keep last partial message in buffer
  if (src != src_end) {
    size_t new_size = src_end - src;
    if (message_buffer_is_overflow(tls->ctx, new_size)) return Ztls_AlertHSFailure;
    if (tls->recvbuf.mess.base == NULL) {
      buffer_init_rx(&tls->recvbuf.mess, const_cast<char *>(""), 0);
      if ((ret = buffer_reserve(&tls->recvbuf.mess, new_size, 0)) != 0) return ret;
      memcpy(tls->recvbuf.mess.base, src, new_size);
    } else {
      memmove(tls->recvbuf.mess.base, src, new_size);
    }
    tls->recvbuf.mess.off = new_size;
    ret = Ztls_ErrorInProgress;
  } else {
    buffer_dispose(&tls->recvbuf.mess);
  }

  return ret;
}

static int dispatch_record(
  Ztls::Cxn *tls, MessageEmitter *emitter, Record *record, HandshakeProps *properties,
  int *application)
{
  Record rec = *record;
  int	 ret;
  *application = 0;
  if (tls->recvbuf.mess.base != NULL || rec.type == Ztls_ContentTypeHS) {
    if (rec.type != Ztls_ContentTypeHS) return Ztls_AlertDecodeError;
    ret = handle_handshake_messages(tls, emitter, {rec.fragment, rec.length}, properties);
  } else {
    // handling of an alert or an application record
    switch (rec.type) {
    case Ztls_ContentTypeAppdata:
      if (tls->state >= Ztls_::State::PostHSMin) {
	*application = 1;
	ret = 0;
      } else if (tls->state == Ztls_::State::SrvWaitEarlyEnd) {
	if (tls->rxTraffic.aead != NULL) *application = 1;
	ret = 0;
      } else {
	ret = Ztls_AlertUnwaitedMsg;
      }
      break;
    case Ztls_ContentTypeAlert:
      ret = handle_alert(tls, rec.fragment, rec.length);
      break;
    default:
      ret = Ztls_AlertUnwaitedMsg;
      break;
    }
  }

  return ret;
}

static int handle_input(
  Ztls::Cxn *tls, MessageEmitter *emitter, Buffer *decryptbuf, const void *input, size_t *inlen,
  HandshakeProps *properties)
{
  Record rec;
  int	 ret;

  // extract the record
  if ((ret = parse_record(tls, &rec, static_cast<const uint8_t *>(input), inlen)) != 0)
    return ret;
  assert(rec.fragment != NULL);

  // decrypt the record
  if (rec.type == Ztls_ContentChangeCipher) {
    if (tls->state < Ztls_::State::PostHSMin) {
      if (!(rec.length == 1 && rec.fragment[0] == 0x01)) return Ztls_AlertUnwaitedMsg;
    } else {
      return Ztls_AlertUnwaitedMsg;
    }
    ret = Ztls_ErrorInProgress;
    goto NextRecord;
  }
  if (tls->rxTraffic.aead != NULL && rec.type != Ztls_ContentTypeAlert) {
    size_t decrypted_length;
    if (rec.type != Ztls_ContentTypeAppdata) return Ztls_AlertHSFailure;
    if ((ret = buffer_reserve(decryptbuf, rec.length, 0)) != 0) return ret;
    if ((ret = aead_decrypt(
	   &tls->rxTraffic, decryptbuf->base + decryptbuf->off, &decrypted_length, rec.fragment,
	   rec.length)) != 0) {
      if (tls->isServer && tls->earlyDataSkippedBytes != UINT32_MAX) goto ServerSkipEarlyData;
      return ret;
    }
    rec.length = decrypted_length;
    rec.fragment = decryptbuf->base + decryptbuf->off;
    // skip padding
    for (; rec.length != 0; --rec.length)
      if (rec.fragment[rec.length - 1] != 0) break;
    if (rec.length == 0) return Ztls_AlertUnwaitedMsg;
    rec.type = rec.fragment[--rec.length];
  } else if (rec.type == Ztls_ContentTypeAppdata && tls->isServer &&
    tls->earlyDataSkippedBytes != UINT32_MAX) {
    goto ServerSkipEarlyData;
  }

  int application;
  ret = dispatch_record(tls, emitter, &rec, properties, &application);
  if (application) decryptbuf->off += rec.length;

NextRecord:
  buffer_dispose(&tls->recvbuf.rec);
  return ret;

ServerSkipEarlyData:
  tls->earlyDataSkippedBytes += (uint32_t)rec.length;
  if (tls->earlyDataSkippedBytes > ZtlsMaxEarlySkipSize) return Ztls_AlertHSFailure;
  ret = Ztls_ErrorInProgress;
  goto NextRecord;
}

ZtEnumImplNS(RawHandshakeOp);
ZtEnumImplNS(State);
ZtEnumImplNS(EchState);
ZtEnumImplNS(HashFinalMode);
ZtEnumImplNS(EarlyDataAcceptance);

struct RawMessageEmitter : MessageEmitter {
  RawHandshakeOffsets	*offsets;
  size_t		start = 0;

  RawMessageEmitter(Buffer *buffer, TrafficProtection *traffic, RawHandshakeOffsets *offsets_)
  :
    MessageEmitter{buffer, traffic}, offsets{offsets_}
  { }

  int beginMessage() override
  {
    auto self = this;
    auto emitter = this;
    self->start = emitter->buf->off;
    return 0;
  }

  int commitMessage() override
  {
    auto   self = this;
    auto   emitter = this;
    size_t epoch = emitter->enc->epoch;
    // A second ClientHello still belongs to Initial if 0-RTT was offered.
    if (epoch == 1 && emitter->buf->base[self->start] == Ztls_HsTypeCliHello) epoch = 0;
    if (epoch == 1 || epoch + 1 >= self->offsets->length()) return Ztls_ErrorLibrary;
    for (size_t i = epoch + 1; i < self->offsets->length(); ++i) {
      if ((*self->offsets)[i] < self->start) return Ztls_ErrorLibrary;
      (*self->offsets)[i] = emitter->buf->off;
    }
    return 0;
  }
};

size_t raw_read_epoch(const Ztls::Cxn *tls)
{
  switch (tls->state) {
  case Ztls_::State::CliHSStart:
  case Ztls_::State::CliWaitSrvHello:
  case Ztls_::State::CliWait2ndSrvHello:
  case Ztls_::State::SrvWaitCliHello:
  case Ztls_::State::SrvWait2ndCliHello:
    return 0;
  case Ztls_::State::SrvWaitEarlyEnd:
    return 1;
  case Ztls_::State::CliWaitEncExts:
  case Ztls_::State::CliWaitCertReq:
  case Ztls_::State::CliExpectCert:
  case Ztls_::State::CliWaitCertSig:
  case Ztls_::State::CliWaitFinished:
  case Ztls_::State::SrvBuildCertSig:
  case Ztls_::State::SrvExpectCert:
  case Ztls_::State::SrvWaitCertSig:
  case Ztls_::State::SrvWaitFinished:
    return 2;
  case Ztls_::State::CliPostHS:
  case Ztls_::State::SrvPostHS:
    return 3;
  default:
    return SIZE_MAX;
  }
}

RawHandshakeResult raw_handshake(
  Ztls::Cxn *tls, HandshakeProps *properties, RawHandshakeOp::T op, size_t input_epoch,
  ZuBSpan input, Bytes output)
{
  Buffer buffer{};
  buffer.base = output.data();
  buffer.capacity = output.length();
  buffer.tx = 1;
  buffer.bounded = true;
  RawHandshakeResult result{Ztls_AlertUnwaitedMsg};
  RawMessageEmitter  emitter{&buffer, &tls->txTraffic, &result.offsets};

  switch (op) {
  case RawHandshakeOp::Start:
    if (!input.length() && tls->state == Ztls_::State::CliHSStart)
      result.status = send_client_hello(tls, &emitter, properties, NULL);
    break;
  case RawHandshakeOp::Input:
    if (tls->state == Ztls_::State::CliHSStart || tls->state == Ztls_::State::SrvBuildCertSig ||
      raw_read_epoch(tls) != input_epoch)
      break;
    result.status = input.length() ?
      handle_handshake_messages(tls, &emitter, input, properties, &result.keyUpdateRejected) :
      Ztls_ErrorInProgress;
    break;
  case RawHandshakeOp::ResumeSignature:
    if (!input.length() && tls->state == Ztls_::State::SrvBuildCertSig)
      result.status = server_finish_handshake(tls, &emitter, 1, NULL);
    break;
  }

  switch (result.status) {
  case 0:
  case Ztls_ErrorInProgress:
  case Ztls_ErrorAsyncOperation:
    result.length = result.offsets[4];
    return result;
  default:
    tls->clearVerifier();
    return {.status = result.status, .keyUpdateRejected = result.keyUpdateRejected};
  }
}

static void
init_record_message_emitter(Ztls::Cxn *tls, RecordMessageEmitter *emitter, Buffer *sendbuf)
{
  emitter->buf = sendbuf;
  emitter->enc = &tls->txTraffic;
  emitter->recStart = 0;
}

int handshake(
  Ztls::Cxn *tls, Buffer *_sendbuf, const void *input, size_t *inlen, HandshakeProps *properties)
{
  RecordMessageEmitter emitter;
  int		       ret;

  assert(tls->state < Ztls_::State::PostHSMin);

  if (tls->txTraffic.failed) return Ztls_ErrorLibrary;
  if ((ret = buffer_require_direction(_sendbuf, 1)) != 0) return ret;
  init_record_message_emitter(tls, &emitter, _sendbuf);
  size_t sendbuf_orig_off = emitter.buf->off;

  // special handlings
  switch (tls->state) {
  case Ztls_::State::CliHSStart: {
    assert(input == NULL || *inlen == 0);
    return send_client_hello(tls, &emitter, properties, NULL);
  }
  case Ztls_::State::SrvBuildCertSig:
    return server_finish_handshake(tls, &emitter, 1, NULL);
  default:
    break;
  }

  const uint8_t *src = static_cast<const uint8_t *>(input), *const src_end = src + *inlen;
  Buffer decryptbuf;

  buffer_init_rx(&decryptbuf, const_cast<char *>(""), 0);

  // perform handhake until completion or until all the input has been swallowed
  ret = Ztls_ErrorInProgress;
  while (ret == Ztls_ErrorInProgress && src != src_end) {
    size_t consumed = src_end - src;
    ret = handle_input(tls, &emitter, &decryptbuf, src, &consumed, properties);
    src += consumed;
    assert(decryptbuf.off == 0);
  }

  buffer_dispose(&decryptbuf);

  switch (ret) {
  case 0:
  case Ztls_ErrorInProgress:
  case Ztls_ErrorStatelessRetry:
  case Ztls_ErrorAsyncOperation:
    break;
  default:
    tls->clearVerifier();
    if (emitter.buf->recordSink != NULL &&
      (ret == Ztls_ErrorNoMemory || ret == Ztls_ErrorLibrary || ret == Ztls_ErrorNotAvailable))
      break;
    // Sink/backend errors are terminal; protocol errors still require their fatal alert. Flush
    // handshake messages that have been written partially. ECH_REQUIRED sticks out because it is
    // a message sent post-handshake compared to other alerts that are generating *during* the
    // handshake.
    if (ret != Ztls_AlertECHRequired) {
      ZuClear(emitter.buf->base + sendbuf_orig_off, emitter.buf->off - sendbuf_orig_off);
      emitter.buf->off = sendbuf_orig_off;
    }
    // send alert immediately
    if (Ztls_ErrorGetClass(ret) != Ztls_ErrorClassPeerAlert)
      if (send_alert(
	    tls, emitter.buf, Ztls_AlertLevelFatal,
	    Ztls_ErrorGetClass(ret) == Ztls_ErrorClassSelfAlert ? ret :
								  Ztls_AlertInternalError) != 0)
	emitter.buf->off = sendbuf_orig_off;
    break;
  }

  *inlen -= src_end - src;
  return ret;
}

int receive(Ztls::Cxn *tls, Buffer *decryptbuf, const void *_input, size_t *inlen)
{
  const uint8_t *input = (const uint8_t *)_input, *const end = input + *inlen;
  size_t decryptbuf_orig_size = decryptbuf->off;
  int	 ret;

  assert(tls->state >= Ztls_::State::SrvWaitEarlyEnd);
  if ((ret = buffer_require_direction(decryptbuf, 0)) != 0) return ret;
  ret = 0;

  // loop until we decrypt some application data (or an error)
  while (ret == 0 && input != end && decryptbuf_orig_size == decryptbuf->off) {
    size_t consumed = end - input;
    ret = handle_input(tls, NULL, decryptbuf, input, &consumed, NULL);
    input += consumed;

    switch (ret) {
    case 0:
      break;
    case Ztls_ErrorInProgress:
      ret = 0;
      break;
    case Ztls_ErrorClassPeerAlert + Ztls_AlertCloseNotify:
      // TODO send close alert
      break;
    default:
      if (Ztls_ErrorGetClass(ret) == Ztls_ErrorClassSelfAlert) {
	// TODO send alert
      }
      break;
    }
  }

  *inlen -= end - input;

  return ret;
}

static int update_send_key(Ztls::Cxn *tls, Buffer *_sendbuf, int request_update)
{
  RecordMessageEmitter emitter;
  int		       ret;

  init_record_message_emitter(tls, &emitter, _sendbuf);
  size_t sendbuf_orig_off = emitter.buf->off;

  Ztls_PushMessage(
    &emitter, NULL, Ztls_HsTypeKeyUpdate, { Ztls_BufferPush_1(emitter.buf, !!request_update); });
  if ((ret = updateTrafficKey(tls, 1)) != 0) goto Exit;
  ret = 0;

Exit:
  if (ret != 0) emitter.buf->off = sendbuf_orig_off;
  return ret;
}

int handoff_tx(Ztls::Cxn *tls)
{
  if (!tls || tls->txHandedOff || tls->ctx->updateTrafficKey ||
    (tls->state != Ztls_::State::CliPostHS && tls->state != Ztls_::State::SrvPostHS) ||
    !tls->txTraffic.aead)
    return Ztls_ErrorNotAvailable;
  tls->txHandedOff = true;
  return 0;
}

// Descriptor capabilities are inspected without constructing dummy keyed contexts.
// No cache: applications may replace suite lists before negotiating a connection.
static int check_serial_suites(Ztls::Cxn *tls, int receive)
{
  for (auto suite : tls->ctx->cipherSuites) {
    const AeadAlgo *algo = suite->aead;
    if (receive ? !algo->serialDecrypt || algo->tagSize > 16 : !algo->serialEncrypt)
      return Ztls_ErrorNotAvailable;
  }
  return 0;
}

static int chunks_check(Ztls::Cxn *tls, Ztls::RecordSink *sink)
{
  if (tls == NULL) return Ztls_ErrorNotAvailable;
  if (tls->txTraffic.failed || !sink) return Ztls_ErrorLibrary;
  if (!record_serial_available(&tls->txTraffic)) return Ztls_ErrorNotAvailable;
  return check_serial_suites(tls, 0);
}

static int serial_receive_available(AeadContext *aead)
{ return aead != NULL && aead->serialDecrypt(); }

int receive_stream_check(Ztls::Cxn *tls)
{
  if (tls == NULL) return Ztls_ErrorNotAvailable;
  if (tls->rxTraffic.aead != NULL && !serial_receive_available(tls->rxTraffic.aead))
    return Ztls_ErrorNotAvailable;
  return check_serial_suites(tls, 1);
}

// Copy only bounded parser carry, never encrypted record bodies.
static size_t
stream_carry(uint8_t *dest, size_t *off, size_t size, const uint8_t **src, size_t *available)
{
  size_t n = size - *off;
  if (n > *available) n = *available;
  if (n != 0) {
    memcpy(dest + *off, *src, n);
    *off += n;
    *src += n;
    *available -= n;
  }
  return *off;
}

int receive_stream(
  Ztls::Cxn *tls, Ztls::RecordSink *output, const void *input, size_t *length,
  HandshakeProps *properties)
{
  Record	 rec;
  AeadContext	*aead;
  int		 ret, emit_fatal = 0;
  size_t	 available = length != NULL ? *length : 0, original = available;
  const uint8_t *src = static_cast<const uint8_t *>(input);
  if (length != NULL) *length = 0;
  if (tls == NULL) return Ztls_ErrorLibrary;
  StreamRecord *r = &tls->streamRec;
  if (r->failed) return Ztls_ErrorLibrary;
  if (available != 0 && input == NULL) {
    ret = Ztls_ErrorLibrary;
    goto Fail;
  }
  if ((ret = receive_stream_check(tls)) != 0) goto Fail;
  // Rx no longer inspects the writable Tx context after handing it off.
  if (!tls->txHandedOff && (ret = chunks_check(tls, output)) != 0) goto Fail;
  if (tls->state == Ztls_::State::CliHSStart || tls->state == Ztls_::State::SrvBuildCertSig) {
    size_t empty = 0;
    ret = handshake_chunks(tls, output, NULL, &empty, properties);
    if (ret == 0 || ret == Ztls_ErrorInProgress || ret == Ztls_ErrorAsyncOperation) return ret;
    goto Fail;
    // handshake_chunks already applied its alert policy.
  }
  emit_fatal = 1;
  aead = tls->rxTraffic.aead;
  if (r->headerOff != 5) {
    stream_carry(r->header, &r->headerOff, 5, &src, &available);
    if (r->headerOff != 5) goto Incomplete;
    switch (r->header[0]) {
    case Ztls_ContentChangeCipher:
    case Ztls_ContentTypeAlert:
    case Ztls_ContentTypeHS:
    case Ztls_ContentTypeAppdata:
      break;
    default:
      ret = Ztls_AlertDecodeError;
      goto Fail;
    }
    if ((ret = parse_record_header(&rec, r->header)) != 0) goto Fail;
    r->length = rec.length;
    r->encrypted =
      aead != NULL && rec.type != Ztls_ContentTypeAlert && rec.type != Ztls_ContentChangeCipher;
    if (r->encrypted) {
      if (rec.type != Ztls_ContentTypeAppdata) {
	ret = Ztls_AlertHSFailure;
	goto Fail;
      }
      r->tagSize = aead->algo->tagSize;
      if (r->tagSize > sizeof(r->tag) || r->length < r->tagSize) {
	ret = Ztls_AlertBadRecordMac;
	goto Fail;
      }
    }
    r->textLength = r->length - r->tagSize;
    void *base = NULL;
    r->reserved = 1;
    ret = tls->plaintext.reserve(r->textLength, &base, &r->capacity);
    r->base = static_cast<uint8_t *>(base);
    if (ret != 0) goto Fail;
    if (base == NULL || r->capacity < r->textLength) {
      ret = Ztls_ErrorNoMemory;
      goto Fail;
    }
  }
  if (r->encrypted && !r->begun) {
    uint8_t aad[5];
    build_aad(aad, r->length);
    r->begun = 1;
    ret = aead->decBegin(tls->rxTraffic.seq, aad, sizeof(aad));
    ZuClear(aad, sizeof(aad));
    if (ret != 0) goto Fail;
  }
  if (r->written != r->textLength) {
    size_t n = r->textLength - r->written;
    if (n > available) n = available;
    if (n != 0) {
      size_t written = n;
      if (r->encrypted) {
	ret = aead->decChunk(r->base + r->written, r->capacity - r->written, src, n, &written);
	// A failed provider might have written provisional bytes; clear the entire reserved
	// extent.
	if (ret != 0 || written != n) {
	  r->written = r->textLength;
	  if (ret == 0) ret = Ztls_ErrorLibrary;
	  goto Fail;
	}
      } else {
	memcpy(r->base + r->written, src, n);
      }
      r->written += written;
      src += n;
      available -= n;
    }
    if (r->written != r->textLength) goto Incomplete;
  }
  if (r->tagOff != r->tagSize) {
    stream_carry(r->tag, &r->tagOff, r->tagSize, &src, &available);
    if (r->tagOff != r->tagSize) goto Incomplete;
  }
  rec = (Record){r->header[0], load_be<uint16_t>(r->header + 1), r->textLength, r->base};
  if (r->encrypted) {
    ret = aead->decEnd(r->tag, r->tagSize);
    if (ret != 0) {
      if (tls->isServer && tls->state < Ztls_::State::PostHSMin &&
	tls->earlyDataSkippedBytes != UINT32_MAX && ret == Ztls_AlertBadRecordMac) {
	r->begun = 0;
	// End cleared per-record state; preserve keys while rejecting early data.
	goto SkipEarly;
      }
      goto Fail;
    }
    r->begun = 0;
    ++tls->rxTraffic.seq;
    for (; rec.length != 0 && rec.fragment[rec.length - 1] == 0; --rec.length);
    if (rec.length == 0) {
      ret = Ztls_AlertUnwaitedMsg;
      goto Fail;
    }
    rec.type = rec.fragment[--rec.length];
  } else if (rec.type == Ztls_ContentTypeAppdata && tls->isServer &&
    tls->state < Ztls_::State::PostHSMin && tls->earlyDataSkippedBytes != UINT32_MAX) {
    goto SkipEarly;
  }
  if (rec.length > Ztls_MaxPlainRecordSize) {
    ret = Ztls_AlertRecordOverflow;
    goto Fail;
  }
  if (rec.type == Ztls_ContentChangeCipher && !r->encrypted) {
    if (tls->state >= Ztls_::State::PostHSMin || rec.length != 1 || rec.fragment[0] != 1) {
      ret = Ztls_AlertUnwaitedMsg;
      goto Fail;
    }
    ret = 0;
  } else {
    Buffer		 formatting;
    RecordMessageEmitter emitter;
    buffer_init_tx(&formatting, const_cast<char *>(""), 0);
    formatting.recordSink = output;
    init_record_message_emitter(tls, &emitter, &formatting);
    int application = 0;
    ret = dispatch_record(tls, &emitter, &rec, properties, &application);
    buffer_dispose(&formatting);
    if (ret != 0 && ret != Ztls_ErrorInProgress && ret != Ztls_ErrorAsyncOperation) goto Fail;
    if (application) {
      if ((ret = tls->plaintext.commit(rec.length)) != 0) goto Fail;
      r->base = NULL;
      r->reserved = 0;
    }
  }
  stream_discard(tls);
  if (length != NULL) *length = original - available;
  return ret == Ztls_ErrorInProgress ? 0 : ret;

SkipEarly:
  tls->earlyDataSkippedBytes += (uint32_t)r->length;
  if (tls->earlyDataSkippedBytes > ZtlsMaxEarlySkipSize) {
    ret = Ztls_AlertHSFailure;
    goto Fail;
  }
  stream_discard(tls);
  if (length != NULL) *length = original - available;
  return 0;
Incomplete:
  if (length != NULL) *length = original - available;
  return Ztls_ErrorInProgress;
Fail:
  // Interpretation can fail after authentication or after dispatch replaced the receive key.
  // Abort the current provider once; discard must not require callback idempotence.
  if (tls->rxTraffic.aead != NULL && tls->rxTraffic.aead->serialDecrypt())
    tls->rxTraffic.aead->decAbort();
  tls->streamRec.begun = 0;
  stream_discard(tls);
  tls->clearVerifier();
  tls->streamRec.failed = 1;
  // Match handshake()'s fatal-alert behavior without publishing provisional input.
  // Post-handshake outbound keys may belong to a detached Tx owner.
  if (emit_fatal && !tls->txHandedOff && tls->state < Ztls_::State::PostHSMin &&
    Ztls_ErrorGetClass(ret) == Ztls_ErrorClassSelfAlert && output) {
    Buffer formatting;
    buffer_init_tx(&formatting, const_cast<char *>(""), 0);
    formatting.recordSink = output;
    (void)send_alert(tls, &formatting, Ztls_AlertLevelFatal, (uint8_t)ret);
    buffer_dispose(&formatting);
  }
  if (length != NULL) *length = original - available;
  return ret;
}

int tx_send_chunks(Ztls::Cxn *tls, Ztls::RecordSink *sink, const void *input, size_t length)
{
  int ret;
  if (!tls || !tls->txHandedOff) return Ztls_ErrorNotAvailable;
  if (length > Ztls_MaxPlainRecordSize) return Ztls_ErrorLibrary;
  TrafficProtection *enc = &tls->txTraffic;
  if ((ret = chunks_check(tls, sink)) != 0) return ret;
  int	 update = tls->needsKeyUpdate || enc->seq >= 16777216;
  size_t size =
    (length != 0 ? record_wire_size(enc, length) : 0) + (update ? record_wire_size(enc, 5) : 0);
  if (size == 0) return 0;
  RecordWriter writer{sink, size, 0};
  if ((ret = sink->reserve(size)) != 0) {
    sink->discard();
    return ret;
  }
  if (update) {
    const uint8_t message[5] = {Ztls_HsTypeKeyUpdate, 0, 0, 1, !!tls->keyUpdateSendRequest};
    if ((ret = record_emit(&writer, enc, Ztls_ContentTypeHS, message, sizeof(message))) != 0 ||
      (ret = updateTrafficKey(tls, 1)) != 0)
      goto Fail;
    tls->needsKeyUpdate = tls->keyUpdateSendRequest = 0;
  }
  if (
    length != 0 && (ret = record_emit(&writer, enc, Ztls_ContentTypeAppdata, input, length)) != 0)
    goto Fail;
  if (writer.remaining != 0) {
    ret = Ztls_ErrorLibrary;
    goto Fail;
  }
  if ((ret = sink->finish()) == 0) return 0;
Fail:
  enc->failed = 1;
  sink->discard();
  return ret;
}

int tx_key_chunks(Ztls::Cxn *tls, Ztls::RecordSink *sink, int request_update)
{
  int ret;
  if (!tls || !tls->txHandedOff) return Ztls_ErrorNotAvailable;
  if ((ret = chunks_check(tls, sink)) != 0) return ret;
  const uint8_t message[5] = {Ztls_HsTypeKeyUpdate, 0, 0, 1, !!request_update};
  if ((ret = record_transaction(
	 sink, &tls->txTraffic, Ztls_ContentTypeHS, message, sizeof(message))) != 0)
    return ret;
  ret = updateTrafficKey(tls, 1);
  if (ret != 0) tls->txTraffic.failed = 1;
  return ret;
}

int tx_alert_chunks(Ztls::Cxn *tls, Ztls::RecordSink *sink, uint8_t level, uint8_t description)
{
  int ret;
  if (!tls || !tls->txHandedOff) return Ztls_ErrorNotAvailable;
  if ((ret = chunks_check(tls, sink)) != 0) return ret;
  const uint8_t message[2] = {level, description};
  return record_transaction(
    sink, &tls->txTraffic, Ztls_ContentTypeAlert, message, sizeof(message));
}

int handshake_chunks(
  Ztls::Cxn *tls, Ztls::RecordSink *sink, const void *input, size_t *length,
  HandshakeProps *properties)
{
  int ret;
  if ((ret = chunks_check(tls, sink)) != 0) return ret;
  Buffer formatting;
  buffer_init_tx(&formatting, const_cast<char *>(""), 0);
  formatting.recordSink = sink;
  ret = handshake(tls, &formatting, input, length, properties);
  if (ret != 0 && ret != Ztls_ErrorInProgress && ret != Ztls_ErrorAsyncOperation)
    tls->txTraffic.failed = 1;
  buffer_dispose(&formatting);
  return ret;
}

size_t tx_get_record_overhead(Ztls::Cxn *tls)
{ return tls ? 6 + tls->txTraffic.aead->algo->tagSize : 0; }

int take_key_update_request(Ztls::Cxn *tls)
{
  if (tls == NULL || tls->keyUpdateRequests == 0) return 0;
  --tls->keyUpdateRequests;
  return 1;
}

int send(Ztls::Cxn *tls, Buffer *sendbuf, const void *input, size_t inlen)
{
  int ret;

  if (tls->txHandedOff) return Ztls_ErrorNotAvailable;
  if (tls->txTraffic.failed) return Ztls_ErrorLibrary;
  assert(tls->txTraffic.aead != NULL);
  if ((ret = buffer_require_direction(sendbuf, 1)) != 0) return ret;

  // "For AES-GCM, up to 2^24.5 full-size records (about 24 million) may be encrypted on a given
  // connection while keeping a safety margin of approximately 2^-57 for Authenticated Encryption
  // (AE) security." (RFC 8446 section 5.5).

  // Key updates do not happen with tls 1.2, check `keySchedule` to see if we are using tls/1.3
  if (tls->txTraffic.seq >= 16777216 && tls->keySchedule != NULL) tls->needsKeyUpdate = 1;

  if (tls->needsKeyUpdate) {
    int ret;
    if ((ret = update_send_key(tls, sendbuf, tls->keyUpdateSendRequest)) != 0) return ret;
    tls->needsKeyUpdate = 0;
    tls->keyUpdateSendRequest = 0;
  }

  return buffer_push_enc_records(
    sendbuf, Ztls_ContentTypeAppdata, static_cast<const uint8_t *>(input), inlen,
    &tls->txTraffic);
}

int update_key(Ztls::Cxn *tls, int request_update)
{
  if (tls->txHandedOff) return Ztls_ErrorNotAvailable;
  if (tls->txTraffic.failed) return Ztls_ErrorLibrary;
  assert(tls->ctx->updateTrafficKey == NULL);
  tls->needsKeyUpdate = 1;
  tls->keyUpdateSendRequest = request_update;
  return 0;
}

size_t get_record_overhead(Ztls::Cxn *tls)
{
  if (tls->txHandedOff) return 0;
  const AeadAlgo *algo = tls->txTraffic.aead->algo;

  return 6 + algo->tagSize;
}

int send_alert(Ztls::Cxn *tls, Buffer *sendbuf, uint8_t level, uint8_t description)
{
  int ret;
  if (tls->txHandedOff) return Ztls_ErrorNotAvailable;
  if (tls->txTraffic.failed) return Ztls_ErrorLibrary;
  if ((ret = buffer_require_direction(sendbuf, 1)) != 0) return ret;
  const uint8_t	    message[2] = {level, description};
  TrafficProtection clear = {}, *enc = &tls->txTraffic;
  if (enc->aead == NULL || tls->state <= Ztls_::State::CliWaitFinished) enc = &clear;
  return buffer_record(sendbuf, enc, Ztls_ContentTypeAlert, message, sizeof(message));
}

int export_secret(
  Ztls::Cxn *tls, void *output, size_t outlen, const char *label, Bytes context_value,
  int is_early)
{
  const HashAlgo *algo = tls->keySchedule->hashes[0].algo;
  auto &exporter = is_early ? tls->exporterMasterSecret.early : tls->exporterMasterSecret.oneRtt;
  uint8_t *master_secret = exporter.data(), derived_secret[Ztls_MaxDigestSize],
	  context_value_hash[Ztls_MaxDigestSize];
  int ret;

  if (master_secret == NULL) {
    if (is_early) {
      switch (tls->state) {
      case Ztls_::State::CliHSStart:
      case Ztls_::State::SrvWaitCliHello:
	ret = Ztls_ErrorInProgress;
	break;
      default:
	ret = Ztls_ErrorNotAvailable;
	break;
      }
    } else {
      ret = Ztls_ErrorInProgress;
    }
    return ret;
  }

  if ((ret = calc_hash(algo, context_value_hash, context_value.data(), context_value.length())) !=
    0)
    return ret;

  if ((ret = hkdf_expand_label(
	 algo, derived_secret, algo->digestSize, wire_bytes(master_secret, algo->digestSize),
	 label, wire_bytes(algo->emptyDigest, algo->digestSize), NULL)) != 0)
    goto Exit;
  ret = hkdf_expand_label(
    algo, output, outlen, wire_bytes(derived_secret, algo->digestSize), "exporter",
    wire_bytes(context_value_hash, algo->digestSize), NULL);

Exit:
  ZuClear(derived_secret, sizeof(derived_secret));
  ZuClear(context_value_hash, sizeof(context_value_hash));
  return ret;
}

ZuDerive(HmacKey, (ZtBArray<ZtArraySecret<true, ZtArrayHeapID<"Ztls.Engine.HMAC.Key">>>));

struct HmacContext : ZmNHeap<HmacContext>, HashContext {
friend inline ZuStringT<"Ztls.Engine.HMAC"> ZmNHeapID(HmacContext *);
  const HashAlgo	*algo;
  HashContext		*hash;
  HmacKey		key;

  HmacContext(const HashAlgo *algo_, HashContext *hash_, const void *src, size_t size)
  :
    algo{algo_}, hash{hash_}
  {
    key.length(algo->blockSize);
    memset(key.data(), 0, algo->blockSize);
    if (size) memcpy(key.data(), src, size);
    applyKey(0x36);
  }
  ~HmacContext() override { delete hash; }
  void update(const void *src, size_t len) override { hash->update(src, len); }
  void applyKey(uint8_t pad)
  {
    for (size_t i = 0; i != algo->blockSize; ++i) key[i] ^= pad;
    hash->update(key.data(), algo->blockSize);
    for (size_t i = 0; i != algo->blockSize; ++i) key[i] ^= pad;
  }
  int final(void *md, HashFinalMode::T mode) override
  {
    if (mode == HashFinalMode::Snapshot) return Ztls_ErrorNotAvailable;
    if (md) {
      hash->final(md, HashFinalMode::Reset);
      applyKey(0x5c);
      hash->update(md, algo->digestSize);
    }
    hash->final(md, mode);
    if (mode == HashFinalMode::Reset) applyKey(0x36);
    return 0;
  }
  // HMAC does not support cloning or non-mutating snapshots.
  HashContext *clone() const override { return nullptr; }
};

int calc_hash(const HashAlgo *algo, void *output, const void *src, size_t len)
{
  HashContext *ctx;

  if ((ctx = algo->create()) == NULL) return Ztls_ErrorNoMemory;
  ctx->update(src, len);
  {
    ctx->final(output);
    delete ctx;
  }
  return 0;
}

HashContext *hmac_create(const HashAlgo *algo, const void *key, size_t keySize)
{
  assert(keySize <= algo->blockSize);

  HashContext *hash = algo->create();
  if (!hash) return NULL;
  return new HmacContext{algo, hash, key, keySize};
}

int hkdf_extract(const HashAlgo *algo, void *output, Bytes salt, Bytes ikm)
{
  HashContext *hash;

  if (salt.length() == 0) salt = wire_bytes(zeroes_of_max_digest_size, algo->digestSize);

  if ((hash = hmac_create(algo, salt.data(), salt.length())) == NULL) return Ztls_ErrorNoMemory;
  hash->update(ikm.data(), ikm.length());
  {
    hash->final(output);
    delete hash;
  }
  return 0;
}

int hkdf_expand(const HashAlgo *algo, void *output, size_t outlen, Bytes prk, Bytes info)
{
  HashContext *hmac = NULL;
  size_t       i;
  uint8_t      digest[Ztls_MaxDigestSize];

  for (i = 0; (i * algo->digestSize) < outlen; ++i) {
    if (hmac == NULL) {
      if ((hmac = hmac_create(algo, prk.data(), prk.length())) == NULL) return Ztls_ErrorNoMemory;
    } else {
      hmac->update(digest, algo->digestSize);
    }
    hmac->update(info.data(), info.length());
    uint8_t gen = (uint8_t)(i + 1);
    hmac->update(&gen, 1);
    hmac->final(digest, HashFinalMode::Reset);

    size_t off_start = i * algo->digestSize, off_end = off_start + algo->digestSize;
    if (off_end > outlen) off_end = outlen;
    memcpy((uint8_t *)output + off_start, digest, off_end - off_start);
  }

  if (hmac != NULL) delete hmac;

  ZuClear(digest, algo->digestSize);

  return 0;
}

int hkdf_expand_label(
  const HashAlgo *algo, void *output, size_t outlen, Bytes secret, const char *label,
  Bytes hash_value, const char *label_prefix)
{
  Buffer  hkdf_label;
  uint8_t hkdf_label_buf[80];
  int	  ret;

  buffer_init_tx(&hkdf_label, hkdf_label_buf, sizeof(hkdf_label_buf));

  Ztls_BufferPush16(&hkdf_label, (uint16_t)outlen);
  Ztls_BufferPushBlock(&hkdf_label, 1, {
    if (label_prefix == NULL) label_prefix = Ztls_HKDFLabelPrefix;
    Ztls_BufferPushv(&hkdf_label, label_prefix, strlen(label_prefix));
    Ztls_BufferPushv(&hkdf_label, label, strlen(label));
  });
  Ztls_BufferPushBlock(
    &hkdf_label, 1, { Ztls_BufferPushv(&hkdf_label, hash_value.data(), hash_value.length()); });

  ret = hkdf_expand(algo, output, outlen, secret, wire_bytes(hkdf_label.base, hkdf_label.off));

Exit:
  buffer_dispose(&hkdf_label);
  return ret;
}

CipherContext *cipher_new(const CipherAlgo *algo, int is_enc, const void *key)
{ return algo->create(is_enc, key); }

void cipher_free(CipherContext *ctx) { delete ctx; }

AeadContext *new_aead(
  const AeadAlgo *aead, const HashAlgo *hash, int is_enc, const void *secret, Bytes hash_value,
  const char *label_prefix)
{
  AeadContext *ctx = NULL;
  struct KeyIV {
    uint8_t	key[Ztls_MaxSecretSize];
    uint8_t	iv[Ztls_MaxIVSize];
  } key_iv;
  int ret;

  if ((ret = get_traffic_keys(
	 aead, hash, key_iv.key, key_iv.iv, secret, hash_value, label_prefix)) != 0)
    goto Exit;
  ctx = aead_new_direct(aead, is_enc, key_iv.key, key_iv.iv);
Exit:
  ZuClear(&key_iv, sizeof(key_iv));
  return ctx;
}

AeadContext *aead_new(
  const AeadAlgo *aead, const HashAlgo *hash, int is_enc, const void *secret,
  const char *label_prefix)
{ return new_aead(aead, hash, is_enc, secret, wire_bytes(NULL, 0), label_prefix); }

AeadContext *aead_new_direct(const AeadAlgo *aead, int is_enc, const void *key, const void *iv)
{ return aead->create(is_enc, key, iv); }

void aead_free(AeadContext *ctx) { delete ctx; }

void aead_xor_iv(AeadContext *ctx, const void *_bytes, size_t len)
{
  const uint8_t *bytes = static_cast<const uint8_t *>(_bytes);
  uint8_t	 iv[Ztls_MaxIVSize];

  aead_get_iv(ctx, iv);
  for (size_t i = 0; i < len; ++i) iv[i] ^= bytes[i];
  aead_set_iv(ctx, iv);
}

void aead__build_iv(const AeadAlgo *algo, uint8_t *iv, const uint8_t *staticIv, uint64_t seq)
{
  size_t	 ivSize = algo->ivSize, i;
  const uint8_t *s = staticIv;
  uint8_t	*d = iv;

  // build iv
  for (i = ivSize - 8; i != 0; --i) *d++ = *s++;
  i = 64;
  do {
    i -= 8;
    *d++ = *s++ ^ (uint8_t)(seq >> i);
  } while (i != 0);
}

struct SystemTime : GetTime {
  uint64_t call() override { return Zm::now().millisecs(); }
};
static SystemTime systemTime;
GetTime		 &getTime = systemTime;

int isServer(Ztls::Cxn *tls) { return tls->isServer; }

// checks if given name looks like an IP address
int server_name_is_ipaddr(const char *name)
{
  ZiIP address;
  return ZiIP::parse(address, name);
}

int ech_encode_config(
  Buffer *buf, uint8_t configId, const HpkeKem *kem, Bytes publicKey, const HpkeSuite **ciphers,
  uint8_t maxNameLength, const char *publicName)
{
  int ret;
  if ((ret = buffer_require_direction(buf, 1)) != 0) return ret;

  Ztls_BufferPush16(buf, Ztls_ECHConfigVer);
  Ztls_BufferPushBlock(buf, 2, {
    Ztls_BufferPush_1(buf, configId);
    Ztls_BufferPush16(buf, kem->id);
    Ztls_BufferPushBlock(
      buf, 2, { Ztls_BufferPushv(buf, publicKey.data(), publicKey.length()); });
    Ztls_BufferPushBlock(buf, 2, {
      for (size_t i = 0; ciphers[i] != NULL; ++i) {
	Ztls_BufferPush16(buf, ciphers[i]->id.kdf);
	Ztls_BufferPush16(buf, ciphers[i]->id.aead);
      }
    });
    Ztls_BufferPush_1(buf, maxNameLength);
    Ztls_BufferPushBlock(buf, 1, { Ztls_BufferPushv(buf, publicName, strlen(publicName)); });
    Ztls_BufferPushBlock(
      buf, 2,
      {
	// extensions
      });
  });

Exit:
  return ret;
}

} // namespace Ztls_
