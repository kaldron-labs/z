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
#include <zlib/ZuCmp.hh>
#include <zlib/ZuDerive.hh>
#include <zlib/ZuTraits.hh>

#include <zlib/ZmTime.hh>
#include <zlib/ZmVHeap.hh>

#include <zlib/ZtArray.hh>
#include <zlib/ZtScratch.hh>

#include <zlib/ZePlatform.hh>

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

static const char echInfoPrefix[8] = "tls ech";

// max. RTT (in msec) to permit early data
#ifndef ZtlsEarlyDataMaxDelay
#define ZtlsEarlyDataMaxDelay 10000
#endif

#ifndef ZtlsMaxEarlySkipSize
#define ZtlsMaxEarlySkipSize 65536
#endif

// list of supported versions in the preferred order
static const uint16_t supportedVersions[] = {Ztls_ProtoVerTls13};

static const uint8_t helloRetryRandom[Ztls_HelloRandomSize] = {0xCF, 0x21, 0xAD, 0x74, 0xE5, 0x9A,
  0x61, 0x11, 0xBE, 0x1D, 0x8C, 0x02, 0x1E, 0x65, 0xB8, 0x91, 0xC2, 0xA2, 0x11, 0x16, 0x7A, 0xBB,
  0x8C, 0x5E, 0x07, 0x9E, 0x09, 0xE2, 0xC8, 0xA8, 0x33, 0x9C};

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

static const uint8_t zeroesOfMaxDigestSize[Ztls_MaxDigestSize] = {0};

static int srvFinishHandshake(
  Ztls::Cxn *tls, MessageEmitter *emitter, int needCertVerify,
  SignatureAlgorithms *signatureAlgorithms);

static int isSupportedVer(uint16_t v)
{
  size_t i;
  for (i = 0; i != ZuTraits<decltype(supportedVersions)>::length(supportedVersions); ++i)
    if (supportedVersions[i] == v) return 1;
  return 0;
}

static int extBitmapTestSet(ExtensionBitmap *bitmap, int hstype, uint16_t extid)
{
//  min(hstype) is -1 (PSEUDO_HRR)
#define Ztls_HSTypeToBit(hstype) ((uint64_t)1 << ((hstype) + 1))
#define Ztls_DefineBit(abbrev, hstype) static const uint64_t abbrev = Ztls_HSTypeToBit(hstype)
#define Ztls_Ext(candext, allowedBits) \
  do { \
    if (ZuUnlikely(extid == candext)) { \
      allowedHsBits = allowedBits; \
      goto Found; \
    } \
    extBitmapMask <<= 1; \
  } while (0)

  Ztls_DefineBit(CH, Ztls_HsTypeCliHello);
  Ztls_DefineBit(SH, Ztls_HsTypeSrvHello);
  Ztls_DefineBit(HRR, Ztls_HsTypePseudoHRR);
  Ztls_DefineBit(EE, Ztls_HsTypeEncExts);
  Ztls_DefineBit(CR, Ztls_HsTypeCertRequest);
  Ztls_DefineBit(CT, Ztls_HsTypeCert);
  Ztls_DefineBit(NST, Ztls_HsTypeNewTicket);

  uint64_t allowedHsBits, extBitmapMask = 1;

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
  if ((allowedHsBits & Ztls_HSTypeToBit(hstype)) == 0) return 0;
  if ((bitmap->bits & extBitmapMask) != 0) return 0;
  bitmap->bits |= extBitmapMask;
  return 1;

#undef Ztls_HSTypeToBit
#undef Ztls_DefineBit
#undef Ztls_Ext
}

template <typename T> static T loadBe(const uint8_t *src)
{
  T data;
  memcpy(&data, src, sizeof(data));
  return ZuBE(data);
}

static uint32_t loadBe24(const uint8_t *src)
{ return (uint32_t(src[0]) << 16) | loadBe<uint16_t>(src + 1); }

template <typename Array> static void assignName(Array &dst, ZuCSpan src)
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

static int bufferRequireDirection(Buffer *buf, int tx)
{
  assert(tx == 0 || tx == 1);
  if (buf->tx == tx) return 0;
  assert(!"TLS buffer direction mismatch");
  return Ztls_ErrorLibrary;
}

using RecordBufferHeap = ZmVHeap<"Ztls.RecordBuffer", 128, (1U << 20), ZiIOBuf_Align>;
static constexpr unsigned RecordBufferAlignBits = ZuIntrin::ctz(ZiIOBuf_Align);

void bufferRelease(Buffer *buf)
{
  if (buf->off) ZuClear(buf->base, buf->off);
  if (buf->isAllocated) RecordBufferHeap::free(buf->base, buf->capacity);
}

int bufferResAligned(Buffer *buf, size_t delta, uint8_t alignBits, int tx)
{
  int ret;
  if ((ret = bufferRequireDirection(buf, tx)) != 0) return ret;

  if (buf->base == NULL || delta > SIZE_MAX - buf->off)
    return buf->bounded ? Ztls_ErrorRawOutput : Ztls_ErrorNoMemory;

  size_t required = buf->off + delta;
  if (buf->capacity < required ||
    (buf->alignBits < alignBits &&
      (alignBits >= (sizeof(uintptr_t) << 3) ||
	(uintptr_t(buf->base) & ((uintptr_t(1) << alignBits) - 1)) != 0))) {
    if (buf->bounded) return Ztls_ErrorRawOutput;
    size_t newCapacity = buf->capacity;
    if (newCapacity < 1024) newCapacity = 1024;
    while (newCapacity < required) {
      if (newCapacity > SIZE_MAX/2) {
	newCapacity = required;
	break;
      }
      newCapacity *= 2;
    }
    if (newCapacity > UINT32_MAX || alignBits > RecordBufferAlignBits) return Ztls_ErrorNoMemory;
    auto data = static_cast<uint8_t *>(RecordBufferHeap::alloc(newCapacity));
    if (buf->off) memcpy(data, buf->base, buf->off);
    bufferRelease(buf);
    buf->base = data;
    buf->capacity = newCapacity;
    buf->isAllocated = 1;
    buf->alignBits = alignBits;
  }

  return 0;
}

int bufferRes(Buffer *buf, size_t delta, int tx) { return bufferResAligned(buf, delta, 0, tx); }

int bufferPushv(Buffer *buf, const void *src, size_t len)
{
  int ret;

  if (len == 0) return 0;
  if ((ret = bufferRes(buf, len, buf->tx)) != 0) return ret;
  memcpy(buf->base + buf->off, src, len);
  buf->off += len;
  return 0;
}

static int recordSerialAvailable(TrafficProtection *enc)
{ return enc->aead == NULL || enc->aead->serialEncrypt(); }

template <bool Contiguous, typename Sink> struct RecordWriter {
  Sink		*sink;
  size_t	remaining;
};

template <bool Contiguous, typename Sink>
static int
recordWrite(
  RecordWriter<Contiguous, Sink> *writer, const void *input, size_t length, AeadContext *aead)
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

static size_t recordWireSize(TrafficProtection *enc, size_t length)
{ return length + 5 + (enc->aead == NULL ? 0 : enc->aead->algo->tagSize + 1); }

// A record has one nonce, one cipher stream and one tag, independent of destination span
// boundaries.
template <bool Contiguous, typename Sink>
static int recordEmit(
  RecordWriter<Contiguous, Sink> *writer, TrafficProtection *enc, uint8_t type,
  const void *input, size_t length)
{
  uint8_t      header[5] = {type, Ztls_RecordVerMajor, Ztls_RecordVerMinor};
  uint8_t      tag[Ztls_MaxDigestSize];
  size_t       wireSize = recordWireSize(enc, length), written;
  AeadContext *aead = enc->aead;
  int	       ret;
  uint64_t     nonce = enc->seq;
  if (enc->failed || length > Ztls_MaxPlainRecordSize) return Ztls_ErrorLibrary;
  if (aead != NULL) header[0] = Ztls_ContentTypeAppdata;
  header[3] = (uint8_t)((wireSize - 5) >> 8);
  header[4] = (uint8_t)(wireSize - 5);
  if ((ret = recordWrite(writer, header, sizeof(header), NULL)) != 0) return ret;
  if (aead == NULL) {
    if ((ret = recordWrite(writer, input, length, NULL)) != 0) return ret;
    return 0;
  }
  if constexpr (Contiguous) {
    // Preserve the optimized vector operation for contiguous destinations.
    void  *dst = NULL;
    size_t capacity = 0, size = length + aead->algo->tagSize + 1;
    Bytes spans[2] = {wireBytes(input, length), wireBytes(&type, 1)};
    if ((ret = writer->sink->next(&dst, &capacity)) != 0) return ret;
    if (dst == NULL || capacity < size || size > writer->remaining) return Ztls_ErrorLibrary;
    if (input == dst) {
      // Handshake encryption aliases its formatting buffer; _v_s requires disjoint input.
      aeadEncryptV(aead, dst, spans, 2, nonce, header, sizeof(header));
      written = size;
    } else {
      written = aeadEncryptVS(aead, dst, spans, 2, nonce, header, sizeof(header), NULL);
    }
    ++enc->seq;
    if (written != size) return Ztls_ErrorLibrary;
    if ((ret = writer->sink->commit(written)) != 0) return ret;
    writer->remaining -= written;
    return 0;
  }
  if (aead->encBegin(nonce, header, sizeof(header)) != 0) return Ztls_ErrorLibrary;
  ++enc->seq;
  if ((ret = recordWrite(writer, input, length, aead)) != 0) return ret;
  if ((ret = recordWrite(writer, &type, 1, aead)) != 0) return ret;
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
  return recordWrite(writer, tag, written, NULL);
}

template <bool Contiguous = false, typename Sink>
static int recordTransaction(
  Sink *sink, TrafficProtection *enc, uint8_t type, const void *input, size_t length)
{
  int ret;
  if (!sink) return Ztls_ErrorLibrary;
  if constexpr (!Contiguous)
    if (!recordSerialAvailable(enc)) return Ztls_ErrorNotAvailable;
  RecordWriter<Contiguous, Sink> writer{sink, recordWireSize(enc, length)};
  if (enc->failed) return Ztls_ErrorLibrary;
  if ((ret = sink->reserve(writer.remaining)) != 0) {
    sink->discard();
    return ret;
  }
  if ((ret = recordEmit(&writer, enc, type, input, length)) == 0)
    ret = writer.remaining == 0 ? sink->finish() : Ztls_ErrorLibrary;
  if (ret != 0) {
    enc->failed = 1;
    sink->discard();
  }
  return ret;
}

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
bufferRecord(Buffer *buf, TrafficProtection *enc, uint8_t type, const void *input, size_t length)
{
  if (buf->recordSink != NULL)
    return recordTransaction(buf->recordSink, enc, type, input, length);
  int ret = bufferResAligned(
    buf, recordWireSize(enc, length), enc->aead != NULL ? enc->aead->algo->alignBits : 0, 1);
  if (ret != 0) return ret;
  ContiguousRecord sink{buf};
  return recordTransaction<true>(&sink, enc, type, input, length);
}

static int bufferPushEncRecords(
  Buffer *buf, uint8_t type, const uint8_t *src, size_t len, TrafficProtection *enc)
{
  while (len != 0) {
    size_t size = len < Ztls_MaxPlainRecordSize ? len : Ztls_MaxPlainRecordSize;
    int	   ret = bufferRecord(buf, enc, type, src, size);
    if (ret != 0) return ret;
    src += size;
    len -= size;
  }
  return 0;
}

ZuDerive(
  RecordPlaintext, (ZtBArray<ZtArraySecret<true, ZtArrayHeapID<"Ztls.Engine.RecordPlaintext">>>));

static int bufferEncryptRecord(Buffer *buf, size_t recStart, TrafficProtection *enc)
{
  size_t  bodylen = buf->off - recStart - 5;
  uint8_t type = buf->base[recStart];
  int	  ret;

  if (buf->recordSink != NULL) {
    for (size_t off = 0; off < bodylen;) {
      size_t len = bodylen - off;
      if (len > Ztls_MaxPlainRecordSize) len = Ztls_MaxPlainRecordSize;
      int ret =
	recordTransaction(buf->recordSink, enc, type, buf->base + recStart + 5 + off, len);
      if (ret != 0) return ret;
      off += len;
    }
    return 0;
  }

  if (bodylen <= Ztls_MaxPlainRecordSize) {
    size_t extra = recordWireSize(enc, bodylen) - bodylen - 5;
    if ((ret = bufferResAligned(buf, extra, enc->aead->algo->alignBits, 1)) != 0) return ret;
    buf->off = recStart;
    ContiguousRecord sink{buf};
    return recordTransaction<true>(&sink, enc, type, buf->base + recStart + 5, bodylen);
  }

  RecordPlaintext plaintext(buf->base + recStart + 5, bodylen);
  ZuClear(buf->base + recStart, bodylen + 5);
  buf->off = recStart;
  return bufferPushEncRecords(buf, type, plaintext.data(), bodylen, enc);
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
    ret = bufferEncryptRecord(self->buf, self->recStart, self->enc);
  } else {
    // TODO allow CH,SH,HRR above 16KB
    size_t sz = self->buf->off - self->recStart - 5;
    assert(sz <= Ztls_MaxPlainRecordSize);
    self->buf->base[self->recStart + 3] = (uint8_t)(sz >> 8);
    self->buf->base[self->recStart + 4] = (uint8_t)(sz);
    TrafficProtection clear = {};
    if (self->buf->recordSink != NULL)
      return recordTransaction(
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
	if (!extBitmapTestSet(&bitmap, (hstype), *(exttype))) { \
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
  *value = loadBe<uint16_t>(*src);
  *src += 2;
  return 0;
}

int decode24(uint32_t *value, const uint8_t **src, const uint8_t *end)
{
  if (end - *src < 3) return Ztls_AlertDecodeError;
  *value = loadBe24(*src);
  *src += 3;
  return 0;
}

int decode32(uint32_t *value, const uint8_t **src, const uint8_t *end)
{
  if (end - *src < 4) return Ztls_AlertDecodeError;
  *value = loadBe<uint32_t>(*src);
  *src += 4;
  return 0;
}

int decode64(uint64_t *value, const uint8_t **src, const uint8_t *end)
{
  if (end - *src < 8) return Ztls_AlertDecodeError;
  *value = loadBe<uint64_t>(*src);
  *src += 8;
  return 0;
}

// This function preserves the flags and  modes (e.g., `offered`, `accepted`, `cipher`), they can
// be used afterwards.
static void clearECH(Ech *ech, int isServer)
{
  if (ech->aead != NULL) {
    aeadFree(ech->aead);
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
static int decodeOneECHConfig(
  const HpkeKem **kems, const HpkeSuites *ciphers, DecodedEchConfig *decoded, const uint8_t **src,
  const uint8_t *const end, bool select)
{
  EchPublicName publicNameBuf;
  int		ret;

  *decoded = (DecodedEchConfig){0};

  if ((ret = decode8(&decoded->id, src, end)) != 0) goto Exit;
  uint16_t kemID;
  if ((ret = decode16(&kemID, src, end)) != 0) goto Exit;
  if (select) {
    for (size_t i = 0; kems[i] != NULL; ++i) {
      if (kems[i]->id == kemID) {
	decoded->kem = kems[i];
	break;
      }
    }
  }
  Ztls_DecodeOpenBlock(*src, end, 2, {
    if (*src == end) {
      ret = Ztls_AlertDecodeError;
      goto Exit;
    }
    decoded->publicKey = wireBytes(*src, end - *src);
    *src = end;
  });
  Ztls_DecodeOpenBlock(*src, end, 2, {
    do {
      uint16_t kdfID;
      uint16_t aeadID;
      if ((ret = decode16(&kdfID, src, end)) != 0) goto Exit;
      if ((ret = decode16(&aeadID, src, end)) != 0) goto Exit;
      if (decoded->kem && decoded->cipher == NULL)
	decoded->cipher = ciphers->find({kdfID, aeadID});
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
    assignName(publicNameBuf, {reinterpret_cast<const char *>(*src), size_t(end - *src)});
    if (serverNameIsIP(publicNameBuf.data())) {
      Ztls_SkipDecoded();
    } else {
      decoded->publicName = wireBytes(*src, end - *src);
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

static int cliDecodeECHConfigList(Context *ctx, DecodedEchConfig *decoded, Bytes configList)
{
  const uint8_t *src = configList.data(), *const end = src + configList.length();
  int matchFound = 0, ret;

  *decoded = (DecodedEchConfig){0};

  Ztls_DecodeBlock(src, end, 2, {
    do {
      const uint8_t *configStart = src;
      uint16_t	     version;
      if ((ret = decode16(&version, &src, end)) != 0) goto Exit;
      Ztls_DecodeOpenBlock(src, end, 2, {
	// If the block is the one that we recognize, parse it, then adopt if if possible.
	// Otherwise, skip.
	if (version == Ztls_ECHConfigVer) {
	  DecodedEchConfig thisconf;
	  if ((ret = decodeOneECHConfig(
		 ctx->ech.client.kems, ctx->ech.client.ciphers, &thisconf,
		 &src, end, !matchFound)) != 0)
	    goto Exit;
	  if (!matchFound && thisconf.kem != NULL && thisconf.cipher != NULL) {
	    *decoded = thisconf;
	    decoded->bytes = wireBytes(configStart, end - configStart);
	    matchFound = 1;
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

static int cliSetupECH(Ech *ech, DecodedEchConfig *decoded, RandomSource *random)
{
  Buffer  infobuf;
  uint8_t infobufInline[256];
  int	  ret;

  // setup `enc` and `aead` by running HPKE
  bufferInitTx(&infobuf, infobufInline, sizeof(infobufInline));
  Ztls_BufferPushv(&infobuf, echInfoPrefix, sizeof(echInfoPrefix));
  Ztls_BufferPushv(&infobuf, decoded->bytes.data(), decoded->bytes.length());
  if ((ret = hpkeSetupBaseS(
	 decoded->kem, decoded->cipher, &ech->client.enc, &ech->aead, decoded->publicKey,
	 wireBytes(infobuf.base, infobuf.off))) != 0)
    goto Exit;

  // setup the rest
  ech->configId = decoded->id;
  ech->kem = decoded->kem;
  ech->cipher = decoded->cipher;
  random->fill(ech->innerClientRandom, Ztls_HelloRandomSize);
  ech->client.maxNameLength = decoded->maxNameLength;
  assignName(ech->client.publicName, ZuCSpan{decoded->publicName});

Exit:
  bufferDispose(&infobuf);
  if (ret != 0) clearECH(ech, 0);
  return ret;
}

static void cliSetupECHGrease(
  Ech *ech, RandomSource *random, const HpkeKem **kems, const HpkeSuites *ciphers,
  ZuCSpan sniName)
{
  static const size_t x25519KeySize = 32;
  uint8_t	      randomSecret[Ztls_AES128KeySize + Ztls_AESIVSize];

  // pick up X25519, AES-128-GCM or bail out
  for (size_t i = 0; kems[i] != NULL; ++i) {
    if (kems[i]->id == Ztls_HPKEKEMX25519SHA256) {
      ech->kem = kems[i];
      break;
    }
  }
  ech->cipher = ciphers->find({Ztls_HPKEHKDFSHA256, Ztls_HPKEAEADAES128GCM});
  if (ech->kem == NULL || ech->cipher == NULL) {
    clearECH(ech, 0);
    return;
  }

  // aead is generated from random
  random->fill(randomSecret, sizeof(randomSecret));
  ech->aead =
    aeadNew_(ech->cipher->aead, 1, randomSecret, randomSecret + Ztls_AES128KeySize);
  ZuClear(randomSecret, sizeof(randomSecret));

  // `enc` is random bytes
  ech->client.enc.length(x25519KeySize);
  random->fill(ech->client.enc.data(), ech->client.enc.length());

  // setup the rest (innerClientRandom is left zeros)
  random->fill(&ech->configId, sizeof(ech->configId));
  ech->client.maxNameLength = 64;
  assignName(ech->client.publicName, sniName);
}

#define Ztls_ECHConfirmationSrvHello "ech accept confirmation"
#define Ztls_ECHConfirmationHRR "hrr ech accept confirmation"
static int echCalcConfirmation(
  KeySchedule *sched, void *dst, const uint8_t *innerRandom, const char *label, Bytes message)
{
  HashContext *hash = NULL;
  uint8_t      secret[Ztls_MaxDigestSize], transcriptHash[Ztls_MaxDigestSize];
  int	       ret;

  // calc transcript hash using the modified ServerHello / HRR
  if ((hash = sched->hashes[0].ctx->clone()) == NULL) {
    ret = Ztls_ErrorNoMemory;
    goto Exit;
  }
  hash->update(message.data(), message.length());
  {
    hash->final(transcriptHash);
    delete hash;
  }
  hash = NULL;

  // HKDF extract and expand
  if ((ret = hkdfExtract(
	 sched->hashes[0].algo, secret, wireBytes(NULL, 0),
	 wireBytes(innerRandom, Ztls_HelloRandomSize))) != 0)
    goto Exit;
  if ((ret = hkdfExpandLabel(
	 sched->hashes[0].algo, dst, 8, wireBytes(secret, sched->hashes[0].algo->digestSize),
	 label, wireBytes(transcriptHash, sched->hashes[0].algo->digestSize), NULL)) != 0)
    goto Exit;

Exit:
  ZuClear(secret, sizeof(secret));
  ZuClear(transcriptHash, sizeof(transcriptHash));
  if (hash != NULL) delete hash;
  return ret;
}

static void keyScheduleFree(KeySchedule *sched)
{
  size_t i;
  ZuClear(sched->secret, sizeof(sched->secret));
  for (i = 0; i != sched->numHashes; ++i) {
    delete sched->hashes[i].ctx;
    if (sched->hashes[i].ctxOuter != NULL) delete sched->hashes[i].ctxOuter;
  }
  delete sched;
}

HpkeSuites::HpkeSuites(ZuSpan<const HpkeSuite *const> suites)
{
  m_suites = suites;
  for (auto suite : m_suites) {
    uint32_t key = (uint32_t(suite->id.kdf) << 16) | suite->id.aead;
    if (m_index.findVal(key)) continue;
    if (m_index.count_() == (1U << ZtlsSuiteBits))
      throw ZeEXCEPT(Error, "Ztls", "HPKE suite configuration exceeds the fixed index capacity");
    m_index.add(key, suite);
  }
}

const HpkeSuite *HpkeSuites::find(HpkeSuiteID id) const
{ return m_index.findVal((uint32_t(id.kdf) << 16) | id.aead); }

CipherSuites::CipherSuites(const CipherSuites &other)
{ *this = SuiteSpan(other); }

CipherSuites::CipherSuites(CipherSuites &&other)
:
  m_suites{ZuMv(other.m_suites)}
{
  reindex();
  other.clear();
}

CipherSuites &CipherSuites::operator=(const CipherSuites &other)
{
  if (this != &other) *this = SuiteSpan(other);
  return *this;
}

CipherSuites &CipherSuites::operator=(CipherSuites &&other)
{
  if (this != &other) {
    m_suites = ZuMv(other.m_suites);
    reindex();
    other.clear();
  }
  return *this;
}

CipherSuites &CipherSuites::operator=(SuiteSpan suites)
{
  Array copy;
  copy = suites;
  Array previous{ZuMv(m_suites)};
  m_suites = ZuMv(copy);
  try { reindex(); }
  catch (...) {
    m_suites = ZuMv(previous);
    reindex();
    throw;
  }
  return *this;
}

void CipherSuites::addIndex(unsigned i)
{
  auto suite = m_suites[i];
  bool newID = ZuCmp<unsigned>::null(m_ids.findVal(suite->id));
  bool newHash = ZuCmp<unsigned>::null(m_hashIndex.findVal(suite->hash));
  if ((newID && m_ids.count_() == (1U << ZtlsSuiteBits)) ||
    (newHash && m_hashIndex.count_() == (1U << ZtlsSuiteBits)))
    throw ZeEXCEPT(Error, "Ztls", "cipher-suite configuration exceeds the fixed index capacity");
  if (newID) m_ids.add(suite->id, i);
  if (newHash) {
    m_hashIndex.add(suite->hash, i);
    m_hashes.push(suite);
  }
  m_serialRx &= suite->aead->serialDecrypt && suite->aead->tagSize <= 16;
  m_serialTx &= suite->aead->serialEncrypt;
}

void CipherSuites::reindex()
{
  m_ids.clean();
  m_hashIndex.clean();
  m_hashes.length(0);
  m_serialRx = m_serialTx = true;
  for (unsigned i = 0, n = length(); i < n; ++i) addIndex(i);
}

void CipherSuites::push(const CipherSuite *suite)
{
  unsigned i = length();
  m_suites.push(suite);
  try { addIndex(i); }
  catch (...) { m_suites.length(i); throw; }
}

void CipherSuites::clear()
{
  m_suites.length(0);
  reindex();
}

int CipherSuites::index(uint16_t id) const
{
  unsigned i = m_ids.findVal(id);
  return ZuCmp<unsigned>::null(i) ? -1 : int(i);
}

const CipherSuite *CipherSuites::find(uint16_t id) const
{
  int i = index(id);
  return i < 0 ? nullptr : m_suites[i];
}

const CipherSuite *CipherSuites::first(const HashAlgo *hash) const
{
  unsigned i = m_hashIndex.findVal(hash);
  return ZuCmp<unsigned>::null(i) ? nullptr : m_suites[i];
}

static KeySchedule *keyScheduleNew(
  const CipherSuite *preferred, const CipherSuites *offered, int useOuter)
{
  // A preferred suite, when supplied with offers, was selected from those offers.
  auto hashes = offered ? offered->hashes() : SuiteSpan{};
  size_t numHashes = hashes.length() ? hashes.length() : preferred ? 1 : 0;
  if (!numHashes) return nullptr;
  auto sched = new KeySchedule{};
  sched->hashes.length(numHashes);
  auto add = [sched, useOuter](const HashAlgo *algo) {
    auto &slot = sched->hashes[sched->numHashes];
    slot.algo = algo;
    slot.ctxOuter = NULL;
    if ((slot.ctx = algo->create()) == NULL) return false;
    if (useOuter && (slot.ctxOuter = algo->create()) == NULL) {
      delete slot.ctx;
      return false;
    }
    ++sched->numHashes;
    return true;
  };
  if (preferred && !add(preferred->hash)) goto Fail;
  for (auto suite : hashes)
    if ((!preferred || suite->hash != preferred->hash) && !add(suite->hash)) goto Fail;
  return sched;
Fail:
  keyScheduleFree(sched);
  return NULL;
}

static int keyScheduleExtract(KeySchedule *sched, Bytes ikm)
{
  int ret;

  if (ikm.data() == NULL)
    ikm = wireBytes(zeroesOfMaxDigestSize, sched->hashes[0].algo->digestSize);

  if (sched->generation != 0 &&
    (ret = hkdfExpandLabel(
       sched->hashes[0].algo, sched->secret, sched->hashes[0].algo->digestSize,
       wireBytes(sched->secret, sched->hashes[0].algo->digestSize), "derived",
       wireBytes(sched->hashes[0].algo->emptyDigest, sched->hashes[0].algo->digestSize), NULL)) !=
      0)
    return ret;

  ++sched->generation;
  ret = hkdfExtract(
    sched->hashes[0].algo, sched->secret,
    wireBytes(sched->secret, sched->hashes[0].algo->digestSize), ikm);
  return ret;
}

static int
keyScheduleSelectCipher(KeySchedule *sched, const CipherSuite *cs, int reset, Bytes resetIkm)
{
  size_t foundSlot = SIZE_MAX, i;
  int	 ret;

  assert(sched->generation == 1);

  // Retire unused transcript contexts while retaining the negotiated hash.
  for (i = 0; i != sched->numHashes; ++i) {
    if (sched->hashes[i].algo == cs->hash) {
      assert(foundSlot == SIZE_MAX);
      foundSlot = i;
    } else {
      delete sched->hashes[i].ctx;
      if (sched->hashes[i].ctxOuter != NULL) delete sched->hashes[i].ctxOuter;
    }
  }
  if (foundSlot != 0) {
    sched->hashes[0] = sched->hashes[foundSlot];
    reset = 1;
  }
  sched->numHashes = 1;

  // recalculate the hash if a different hash as been selected than the one we used for
  // calculating the early secrets
  if (reset) {
    --sched->generation;
    memset(sched->secret, 0, sizeof(sched->secret));
    if ((ret = keyScheduleExtract(sched, resetIkm)) != 0) goto Exit;
  }

  ret = 0;
Exit:
  return ret;
}

static void keyScheduleSelectOuter(KeySchedule *sched)
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

void keyScheduleUpdateHash(KeySchedule *sched, const uint8_t *msg, size_t msglen, int useOuter)
{
  size_t i;

  for (i = 0; i != sched->numHashes; ++i) {
    HashContext *ctx = useOuter ? sched->hashes[i].ctxOuter : sched->hashes[i].ctx;
    ctx->update(msg, msglen);
  }
}

static void keySchedCh1hashPrefix(KeySchedule *sched)
{
  uint8_t prefix[4] = {Ztls_HsTypeMessageHash, 0, 0, (uint8_t)sched->hashes[0].algo->digestSize};
  keyScheduleUpdateHash(sched, prefix, sizeof(prefix), 0);
}

static void keyScheduleExtractCh1hash(KeySchedule *sched, uint8_t *hash)
{
  assert(sched->hashes[0].ctxOuter == NULL);
  sched->hashes[0].ctx->final(hash, HashFinalMode::Reset);
}

static void keySchedPostCh1hash(KeySchedule *sched)
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
deriveSecretWithHash(KeySchedule *sched, void *secret, const char *label, const uint8_t *hash)
{
  int ret = hkdfExpandLabel(
    sched->hashes[0].algo, secret, sched->hashes[0].algo->digestSize,
    wireBytes(sched->secret, sched->hashes[0].algo->digestSize), label,
    wireBytes(hash, sched->hashes[0].algo->digestSize), NULL);
  return ret;
}

static int deriveSecret(KeySchedule *sched, void *secret, const char *label)
{
  uint8_t hashValue[Ztls_MaxDigestSize];

  sched->hashes[0].ctx->final(hashValue, HashFinalMode::Snapshot);
  int ret = deriveSecretWithHash(sched, secret, label, hashValue);
  ZuClear(hashValue, sizeof(hashValue));
  return ret;
}

static int deriveEmptySecret(KeySchedule *sched, void *secret, const char *label)
{ return deriveSecretWithHash(sched, secret, label, sched->hashes[0].algo->emptyDigest); }

static int deriveExporterSecret(Ztls::Cxn *tls, int isEarly)
{
  if (!tls->ctx->useExporter) return 0;

  auto &slot = isEarly ? tls->exporterMasterSecret.early : tls->exporterMasterSecret.oneRtt;
  assert(!slot.length());
  slot.length(tls->keySchedule->hashes[0].algo->digestSize);
  return deriveSecret(tls->keySchedule, slot.data(), isEarly ? "e exp master" : "exp master");
}

static int deriveResumptionSecret(KeySchedule *sched, uint8_t *secret, Bytes nonce)
{
  int ret;

  if ((ret = deriveSecret(sched, secret, "res master")) != 0) goto Exit;
  if ((ret = hkdfExpandLabel(
	 sched->hashes[0].algo, secret, sched->hashes[0].algo->digestSize,
	 wireBytes(secret, sched->hashes[0].algo->digestSize), "resumption", nonce, NULL)) != 0)
    goto Exit;

Exit:
  if (ret != 0) ZuClear(secret, sched->hashes[0].algo->digestSize);
  return ret;
}

static int decodeNewSessionTicket(
  Ztls::Cxn *tls, uint32_t *lifetime, uint32_t *ageAdd, Bytes *nonce, Bytes *ticket,
  uint32_t *maxEarlyDataSize, const uint8_t *src, const uint8_t *const end)
{
  uint16_t exttype;
  int	   ret;

  if ((ret = decode32(lifetime, &src, end)) != 0) goto Exit;
  if ((ret = decode32(ageAdd, &src, end)) != 0) goto Exit;
  Ztls_DecodeOpenBlock(src, end, 1, {
    *nonce = wireBytes(src, end - src);
    src = end;
  });
  Ztls_DecodeOpenBlock(src, end, 2, {
    if (src == end) {
      ret = Ztls_AlertDecodeError;
      goto Exit;
    }
    *ticket = wireBytes(src, end - src);
    src = end;
  });

  *maxEarlyDataSize = 0;
  Ztls_DecodeExtensions(src, end, Ztls_HsTypeNewTicket, &exttype, {
    if (tls->ctx->onExtension != NULL &&
      (ret = tls->ctx->onExtension->call(
	       tls, Ztls_HsTypeNewTicket, exttype, wireBytes(src, end - src)) != 0))
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

static int decodeStoredSessionTicket(
  Ztls::Cxn *tls, const KeyExchangeAlgo **keyShare, const CipherSuite **cs, Bytes *secret,
  uint32_t *obfuscatedTicketAge, Bytes *ticket, uint32_t *maxEarlyDataSize, const uint8_t *src,
  const uint8_t *const end)
{
  uint16_t kxid, csid;
  uint32_t lifetime, ageAdd;
  uint64_t obtainedAt, now;
  Bytes	   nonce;
  int	   ret;

  // decode
  if ((ret = decode64(&obtainedAt, &src, end)) != 0) goto Exit;
  if ((ret = decode16(&kxid, &src, end)) != 0) goto Exit;
  if ((ret = decode16(&csid, &src, end)) != 0) goto Exit;
  Ztls_DecodeOpenBlock(src, end, 3, {
    if ((ret = decodeNewSessionTicket(
	   tls, &lifetime, &ageAdd, &nonce, ticket, maxEarlyDataSize, src, end)) != 0)
      goto Exit;
    src = end;
  });
  Ztls_DecodeBlock(src, end, 2, {
    *secret = wireBytes(src, end - src);
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
    *cs = tls->ctx->cipherSuites.find(csid);
    if (!*cs) {
      ret = Ztls_ErrorLibrary;
      goto Exit;
    }
  }

  // calculate obfuscatedTicketAge
  now = tls->ctx->getTime->call();
  if (!(obtainedAt <= now && now - obtainedAt < 7 * 86400 * 1000)) {
    ret = Ztls_ErrorLibrary;
    goto Exit;
  }
  *obfuscatedTicketAge = (uint32_t)(now - obtainedAt) + ageAdd;

  ret = 0;
Exit:
  return ret;
}

static int trafficKey(
  const HashAlgo *algo, void *key, size_t keySize, int isIV, const void *secret, Bytes hashValue,
  const char *labelPrefix)
{
  return hkdfExpandLabel(
    algo, key, keySize, wireBytes(secret, algo->digestSize), isIV ? "iv" : "key", hashValue,
    labelPrefix);
}

static int trafficKeys(
  const AeadAlgo *aead, const HashAlgo *hash, void *key, void *iv, const void *secret,
  Bytes hashValue, const char *labelPrefix)
{
  int ret;

  if ((ret = trafficKey(hash, key, aead->keySize, 0, secret, hashValue, labelPrefix)) != 0 ||
    (ret = trafficKey(hash, iv, aead->ivSize, 1, secret, hashValue, labelPrefix)) != 0) {
    ZuClear(key, aead->keySize);
    ZuClear(iv, aead->ivSize);
  }

  return ret;
}

static int setupTrafficProtection(
  Ztls::Cxn *tls, int isEnc, const char *secretLabel, size_t epoch, uint64_t seq, int skipNotify)
{
  TrafficProtection *ctx = isEnc ? &tls->txTraffic : &tls->rxTraffic;

  if (secretLabel != NULL) {
    int ret;
    if ((ret = deriveSecret(tls->keySchedule, ctx->secret, secretLabel)) != 0) return ret;
  }

  ctx->epoch = epoch;

  // special path for applications having their own record layer
  if (tls->ctx->updateTrafficKey != NULL) {
    if (skipNotify) return 0;
    return tls->ctx->updateTrafficKey->call(tls, isEnc, epoch, ctx->secret);
  }

  if (ctx->aead != NULL) aeadFree(ctx->aead);
  if ((ctx->aead = aeadNew(
	 tls->cipherSuite->aead, tls->cipherSuite->hash, isEnc, ctx->secret,
	 tls->ctx->hkdfLabelPrefix)) == NULL)
    return Ztls_ErrorNoMemory;
  // TODO obtain error from aeadNew
  ctx->seq = seq;

  return 0;
}

static int commissionHandshakeSecret(Ztls::Cxn *tls)
{
  int isEnc = !isServer(tls);

  assert(tls->pendingHandshakeSecret.data() != NULL);
  memcpy(
    (isEnc ? &tls->txTraffic : &tls->rxTraffic)->secret, tls->pendingHandshakeSecret.data(),
    Ztls_MaxDigestSize);
  tls->pendingHandshakeSecret.null();

  return setupTrafficProtection(tls, isEnc, NULL, 2, 0, 1);
}

// the number should be changed upon incompatible format change
#define Ztls_SessionIDMagic "ptls0001"
#define Ztls_SessionIdMagicSize (sizeof(Ztls_SessionIDMagic) - 1)

static int encodeSessionIdentifier(
  Context *ctx, Buffer *buf, uint32_t ticketAgeAdd, Bytes ticketNonce, KeySchedule *sched,
  const char *serverName, uint16_t keyExchangeID, uint16_t csid, const char *negotiatedProtocol)
{
  int ret = 0;

  Ztls_BufferPushBlock(buf, 2, {
    // format id
    Ztls_BufferPushv(buf, Ztls_SessionIDMagic, Ztls_SessionIdMagicSize);
    // date
    Ztls_BufferPush64(buf, ctx->getTime->call());
    // resumption master secret
    Ztls_BufferPushBlock(buf, 2, {
      if ((ret = bufferRes(buf, sched->hashes[0].algo->digestSize, 1)) != 0) goto Exit;
      if ((ret = deriveResumptionSecret(sched, buf->base + buf->off, ticketNonce)) != 0)
	goto Exit;
      buf->off += sched->hashes[0].algo->digestSize;
    });
    // key-exchange
    Ztls_BufferPush16(buf, keyExchangeID);
    // cipher-suite
    Ztls_BufferPush16(buf, csid);
    // ticketAgeAdd
    Ztls_BufferPush32(buf, ticketAgeAdd);
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

int decodeSessionIdentifier(
  uint64_t *issuedAt, Bytes *psk, uint32_t *ticketAgeAdd, Bytes *ticketCtx,
  uint16_t *keyExchangeID, uint16_t *csid, Bytes *negotiatedProtocol, const uint8_t *src,
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
    if ((ret = decode64(issuedAt, &src, end)) != 0) goto Exit;
    Ztls_DecodeOpenBlock(src, end, 2, {
      *psk = wireBytes(src, end - src);
      src = end;
    });
    if ((ret = decode16(keyExchangeID, &src, end)) != 0) goto Exit;
    if ((ret = decode16(csid, &src, end)) != 0) goto Exit;
    if ((ret = decode32(ticketAgeAdd, &src, end)) != 0) goto Exit;
    Ztls_DecodeOpenBlock(src, end, 2, {
      *ticketCtx = wireBytes(src, end - src);
      src = end;
    });
    Ztls_DecodeOpenBlock(src, end, 1, {
      *negotiatedProtocol = wireBytes(src, end - src);
      src = end;
    });
  });

Exit:
  return ret;
}

static size_t buildCertVerifySigndata(uint8_t *data, KeySchedule *sched, const char *ctxString)
{
  size_t datalen = 0;

  memset(data + datalen, 32, 64);
  datalen += 64;
  memcpy(data + datalen, ctxString, strlen(ctxString) + 1);
  datalen += strlen(ctxString) + 1;
  sched->hashes[0].ctx->final(data + datalen, HashFinalMode::Snapshot);
  datalen += sched->hashes[0].algo->digestSize;
  assert(datalen <= Ztls_SignInputSize);

  return datalen;
}

static int calcVerifyData(void *output, KeySchedule *sched, const void *secret)
{
  HashContext *hmac;
  uint8_t      digest[Ztls_MaxDigestSize];
  int	       ret;

  if ((ret = hkdfExpandLabel(
	 sched->hashes[0].algo, digest, sched->hashes[0].algo->digestSize,
	 wireBytes(secret, sched->hashes[0].algo->digestSize), "finished", wireBytes(NULL, 0),
	 NULL)) != 0)
    return ret;
  if ((hmac = hmacCreate(sched->hashes[0].algo, digest, sched->hashes[0].algo->digestSize)) ==
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

static int verifyFinished(Ztls::Cxn *tls, Bytes message)
{
  uint8_t verifyData[Ztls_MaxDigestSize];
  int	  ret;

  if (Ztls_HSHeaderSize + tls->keySchedule->hashes[0].algo->digestSize != message.length()) {
    ret = Ztls_AlertDecodeError;
    goto Exit;
  }

  if ((ret = calcVerifyData(verifyData, tls->keySchedule, tls->rxTraffic.secret)) != 0) goto Exit;
  if (!Ztls::ctEqual(
	{message.data() + Ztls_HSHeaderSize, tls->keySchedule->hashes[0].algo->digestSize},
	{verifyData, tls->keySchedule->hashes[0].algo->digestSize})) {
    ret = Ztls_AlertHSFailure;
    goto Exit;
  }

Exit:
  ZuClear(verifyData, sizeof(verifyData));
  return ret;
}

static int sendFinished(Ztls::Cxn *tls, MessageEmitter *emitter)
{
  int ret;

  Ztls_PushMessage(emitter, tls->keySchedule, Ztls_HSTypeFinished, {
    if ((ret = bufferRes(emitter->buf, tls->keySchedule->hashes[0].algo->digestSize, 1)) != 0)
      goto Exit;
    if ((ret = calcVerifyData(
	   emitter->buf->base + emitter->buf->off, tls->keySchedule, tls->txTraffic.secret)) != 0)
      goto Exit;
    emitter->buf->off += tls->keySchedule->hashes[0].algo->digestSize;
  });

Exit:
  return ret;
}

static int sendSessionTicket(Ztls::Cxn *tls, MessageEmitter *emitter)
{
  HashContext *msghashBackup = tls->keySchedule->hashes[0].ctx->clone();
  Buffer       sessionID;
  char	       sessionIDInline[128];
  uint32_t     ticketAgeAdd;
  int	       ret = 0;

  assert(tls->ctx->ticketLifetime != 0);
  assert(tls->ctx->encryptTicket != NULL);

  bufferInitTx(&sessionID, sessionIDInline, sizeof(sessionIDInline));

  {
    // calculate verify-data that will be sent by the client
    size_t origOff = emitter->buf->off;
    if (tls->pendingHandshakeSecret.data() != NULL && !tls->ctx->omitEndOfEarlyData) {
      assert(tls->state == Ztls_::State::SrvWaitEarlyEnd);
      Ztls_BufferPushMsgBody(emitter->buf, tls->keySchedule, Ztls_HsTypeEndOfEarly, {});
      emitter->buf->off = origOff;
    }
    Ztls_BufferPushMsgBody(emitter->buf, tls->keySchedule, Ztls_HSTypeFinished, {
      if ((ret = bufferRes(emitter->buf, tls->keySchedule->hashes[0].algo->digestSize, 1)) != 0)
	goto Exit;
      if ((ret = calcVerifyData(
	     emitter->buf->base + emitter->buf->off, tls->keySchedule,
	     tls->pendingHandshakeSecret.data() != NULL ? tls->pendingHandshakeSecret.data() :
							  tls->rxTraffic.secret)) != 0)
	goto Exit;
      emitter->buf->off += tls->keySchedule->hashes[0].algo->digestSize;
    });
    emitter->buf->off = origOff;
  }

  tls->ctx->random->fill(&ticketAgeAdd, sizeof(ticketAgeAdd));

  // build the raw nsk
  if (tls->keyShare != NULL &&
    (ret = encodeSessionIdentifier(
       tls->ctx, &sessionID, ticketAgeAdd, wireBytes(NULL, 0), tls->keySchedule,
       tls->serverName.data(), tls->keyShare->id, tls->cipherSuite->id,
       tls->negotiatedProtocol.data())) != 0)
    goto Exit;

  // encrypt and send
  Ztls_PushMessage(emitter, tls->keySchedule, Ztls_HsTypeNewTicket, {
    Ztls_BufferPush32(emitter->buf, tls->ctx->ticketLifetime);
    Ztls_BufferPush32(emitter->buf, ticketAgeAdd);
    Ztls_BufferPushBlock(emitter->buf, 1, {});
    Ztls_BufferPushBlock(emitter->buf, 2, {
      if ((ret = tls->ctx->encryptTicket->call(
	     tls, 1, emitter->buf, wireBytes(sessionID.base, sessionID.off))) != 0)
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
  bufferDispose(&sessionID);

  // restore handshake state
  delete tls->keySchedule->hashes[0].ctx;
  tls->keySchedule->hashes[0].ctx = msghashBackup;

  return ret;
}

static int pushChangeCipherSpec(Ztls::Cxn *tls, MessageEmitter *emitter)
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
  if ((ret = bufferRecord(emitter->buf, &clear, Ztls_ContentChangeCipher, &value, 1)) != 0)
    goto Exit;

  tls->sendChangeCipherSpec = 0;
  ret = 0;
Exit:
  return ret;
}

static int pushAdditionalExts(HandshakeProps *properties, Buffer *sendbuf)
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

static int pushSigAlgos(VerifyCertificate *vc, Buffer *sendbuf)
{
  // The list sent when verify callback is not registered
  static const uint16_t defaultAlgos[] = {Ztls_SigRSAPSSRSAESHA384, Ztls_SigRSAPSSRSAESHA256,
    Ztls_SigECDSAP384SHA384, Ztls_SigECDSAP256SHA256, Ztls_SigRSAPKCS1SHA256,
    Ztls_SigRSAPKCS1SHA1};
  int			ret;

  Ztls_BufferPushBlock(sendbuf, 2, {
    for (auto algorithm : vc ? vc->algos : ZuSpan<const uint16_t>{defaultAlgos})
      Ztls_BufferPush16(sendbuf, algorithm);
  });

  ret = 0;
Exit:
  return ret;
}

static int decodeSigAlgos(SignatureAlgorithms *sa, const uint8_t **src, const uint8_t *end)
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
static int selectCipher(
  const CipherSuite **selected, const CipherSuites &candidates, const uint8_t *src,
  const uint8_t *const end, int srvPreference, int srvChachaPriority, const HashAlgo *hash)
{
  size_t foundIndex = SIZE_MAX;
  int	 ret;

  while (src != end) {
    uint16_t id;
    if ((ret = decode16(&id, &src, end)) != 0) goto Exit;
    int i = candidates.index(id);
    auto suite = i < 0 ? nullptr : candidates[i];
    if (suite && (hash == NULL || suite->hash == hash)) {
      if (srvPreference && !(srvChachaPriority && id == Ztls_SuiteChaChaSHA256)) {
	if (unsigned(i) < foundIndex) {
	  foundIndex = unsigned(i);
	  *selected = suite;
	}
      } else {
	*selected = suite;
	ret = 0;
	goto Exit;
      }
    }
    // first position of the server list matched (srvPreference)
    if (foundIndex == 0) break;
    // server preference is overridden only if the first entry of client-provided list is
    // chachapoly
    srvChachaPriority = 0;
  }
  if (foundIndex != SIZE_MAX) {
    ret = 0;
  } else {
    ret = Ztls_AlertHSFailure;
  }

Exit:
  return ret;
}

static int pushKeyShareEntry(Buffer *buf, uint16_t group, Bytes pubkey)
{
  int ret;

  Ztls_BufferPush16(buf, group);
  Ztls_BufferPushBlock(buf, 2, { Ztls_BufferPushv(buf, pubkey.data(), pubkey.length()); });
  ret = 0;
Exit:
  return ret;
}

static int decodeKeyShareEntry(
  uint16_t *group, Bytes *keyExchange, const uint8_t **src, const uint8_t *const end)
{
  int ret;

  if ((ret = decode16(group, src, end)) != 0) goto Exit;
  Ztls_DecodeOpenBlock(*src, end, 2, {
    *keyExchange = wireBytes(*src, end - *src);
    *src = end;
  });

Exit:
  return ret;
}

static int selectKeyShare(
  const KeyExchangeAlgo **selected, Bytes *peerKey, const KeyExchangeAlgo **candidates,
  const uint8_t **src, const uint8_t *const end, int expectOne)
{
  int ret;

  *selected = NULL;

  if (expectOne && *src == end) {
    ret = Ztls_AlertIllegalParameter;
    goto Exit;
  }

  while (*src != end) {
    uint16_t group;
    Bytes    key;
    if ((ret = decodeKeyShareEntry(&group, &key, src, end)) != 0) goto Exit;
    const KeyExchangeAlgo **c = candidates;
    for (; *c != NULL; ++c) {
      if (*selected == NULL && (*c)->id == group) {
	*selected = *c;
	*peerKey = key;
      }
    }
    if (expectOne) {
      ret = *selected != NULL ? 0 : Ztls_AlertIllegalParameter;
      goto Exit;
    }
  }

  ret = 0;

Exit:
  return ret;
}

static int emitSrvNameExt(Buffer *buf, ZuCSpan serverName)
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
static inline size_t outerECHHeaderSize(size_t encSize) { return 10 + encSize; }

// Flag to indicate which of ClientHelloInner, EncodedClientHelloInner, ClientHelloOuter is to be
// generated. When ECH is inactive, only ClientHelloInner is used.
namespace EncodeCHMode {
  enum { Inner, EncodedInner, Outer };
}

static int encodeCliHello(
  Context *ctx, Buffer *sendbuf, int mode, int isSecondFlight, HandshakeProps *properties,
  const void *clientRandom, KeyExchangeContext *keyShareCtx, ZuCSpan sniName,
  Bytes legacySessionId, Ech *ech, size_t *echSizeOffset, Bytes echReplay, Bytes pskSecret,
  Bytes pskIdentity, uint32_t obfuscatedTicketAge, size_t pskBinderSize, Bytes *cookie,
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
	  [[maybe_unused]] size_t extPayloadFrom = sendbuf->off;
	  Ztls_BufferPush_1(sendbuf, Ztls_ECHCliHelloOuter);
	  Ztls_BufferPush16(sendbuf, ech->cipher->id.kdf);
	  Ztls_BufferPush16(sendbuf, ech->cipher->id.aead);
	  Ztls_BufferPush_1(sendbuf, ech->configId);
	  Ztls_BufferPushBlock(sendbuf, 2, {
	    if (!isSecondFlight)
	      Ztls_BufferPushv(sendbuf, ech->client.enc.data(), ech->client.enc.length());
	  });
	  Ztls_BufferPushBlock(sendbuf, 2, {
	    assert(
	      sendbuf->off - extPayloadFrom ==
	      outerECHHeaderSize(isSecondFlight ? 0 : ech->client.enc.length()));
	    if ((ret = bufferRes(sendbuf, *echSizeOffset, 1)) != 0) goto Exit;
	    memset(sendbuf->base + sendbuf->off, 0, *echSizeOffset);
	    sendbuf->off += *echSizeOffset;
	    *echSizeOffset = sendbuf->off - *echSizeOffset;
	  });
	});
      } else if (ech->aead != NULL) {
	Ztls_BufferPushExtension(sendbuf, Ztls_ExtTypeEncCliHello, {
	  Ztls_BufferPush_1(sendbuf, Ztls_ECHCliHelloInner);
	});
      } else if (echReplay.data() != NULL) {
	Ztls_BufferPushExtension(sendbuf, Ztls_ExtTypeEncCliHello, {
	  Ztls_BufferPushv(sendbuf, echReplay.data(), echReplay.length());
	});
      }
      if (mode == EncodeCHMode::EncodedInner) {
	Ztls_BufferPushExtension(sendbuf, Ztls_ExtTypeECHOuterExts, {
	  Ztls_BufferPushBlock(sendbuf, 1, { Ztls_BufferPush16(sendbuf, Ztls_ExtTypeKeyShare); });
	});
      } else {
	Ztls_BufferPushExtension(sendbuf, Ztls_ExtTypeKeyShare, {
	  Ztls_BufferPushBlock(sendbuf, 2, {
	    if (keyShareCtx != NULL &&
	      (ret = pushKeyShareEntry(sendbuf, keyShareCtx->algo->id, keyShareCtx->pubkey)) != 0)
	      goto Exit;
	  });
	});
      }
      if (sniName.data() != NULL) {
	Ztls_BufferPushExtension(sendbuf, Ztls_ExtTypeSrvName, {
	  if ((ret = emitSrvNameExt(sendbuf, sniName)) != 0) goto Exit;
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
	  for (i = 0; i != ZuTraits<decltype(supportedVersions)>::length(supportedVersions); ++i)
	    Ztls_BufferPush16(sendbuf, supportedVersions[i]);
	});
      });
      Ztls_BufferPushExtension(sendbuf, Ztls_ExtTypeSigAlgos, {
	if ((ret = pushSigAlgos(ctx->verifyCertificate, sendbuf)) != 0) goto Exit;
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
      if ((ret = pushAdditionalExts(properties, sendbuf)) != 0) goto Exit;
      if (ctx->saveTicket != NULL || pskSecret.data() != NULL) {
	Ztls_BufferPushExtension(sendbuf, Ztls_ExtPSKKexModes, {
	  Ztls_BufferPushBlock(sendbuf, 1, {
	    if (!ctx->requireDheOnPsk) Ztls_BufferPush_1(sendbuf, Ztls_PSKKeModePSK);
	    Ztls_BufferPush_1(sendbuf, Ztls_PSKKeModePSKDHE);
	  });
	});
      }
      if (pskSecret.data() != NULL) {
	if (usingEarlyData && !isSecondFlight)
	  Ztls_BufferPushExtension(sendbuf, Ztls_ExtTypeEarly, {});
	// pre-shared key "MUST be the last extension in the ClientHello" (draft-17 section 4.2.6)
	Ztls_BufferPushExtension(sendbuf, Ztls_ExtTypePreSharedKey, {
	  Ztls_BufferPushBlock(sendbuf, 2, {
	    Ztls_BufferPushBlock(sendbuf, 2, {
	      if (mode == EncodeCHMode::Outer && ech->state != Ztls_::EchState::Grease) {
		if ((ret = bufferRes(sendbuf, pskIdentity.length(), 1)) != 0) goto Exit;
		ctx->random->fill(sendbuf->base + sendbuf->off, pskIdentity.length());
		sendbuf->off += pskIdentity.length();
	      } else {
		Ztls_BufferPushv(sendbuf, pskIdentity.data(), pskIdentity.length());
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
	      if ((ret = bufferRes(sendbuf, pskBinderSize, 1)) != 0) goto Exit;
	      ctx->random->fill(sendbuf->base + sendbuf->off, pskBinderSize);
	      sendbuf->off += pskBinderSize;
	    });
	  });
	});
      }
    });
  });

Exit:
  return ret;
}

// Feeds the CH message into the hash, computing the PSK binder if necessary. `binderKey` must be
// derived before calling this function.
static int updateChHashAndBinder(
  KeySchedule *ks, uint8_t *ch, size_t chStart, size_t chEnd, int hasPSK, uint8_t *binderKey,
  int isOuter)
{
  int	 ret = 0;
  size_t hashOff = chStart;

  if (hasPSK) {
    size_t pskBinderOff = chEnd - (3 + ks->hashes[0].algo->digestSize);
    keyScheduleUpdateHash(ks, ch + hashOff, pskBinderOff - hashOff, isOuter);
    hashOff = pskBinderOff;
    if ((ret = calcVerifyData(ch + pskBinderOff + 3, ks, binderKey)) != 0) return ret;
  }
  keyScheduleUpdateHash(ks, ch + hashOff, chEnd - hashOff, isOuter);

  return ret;
}

static int
sendCliHello(Ztls::Cxn *tls, MessageEmitter *emitter, HandshakeProps *properties, Bytes *cookie)
{
  struct Psk {
    Bytes	secret;
    Bytes	identity;
    const char	*label;
  } psk{};
  uint32_t obfuscatedTicketAge = 0;
  ZuCSpan  sniName;
  size_t   messStart;
  uint8_t  binderKey[Ztls_MaxDigestSize];
  Buffer   encodedChInner;
  int	   ret, isSecondFlight = tls->keySchedule != NULL;

  bufferInitTx(&encodedChInner, const_cast<char *>(""), 0);

  if (tls->serverName.data() != NULL && !serverNameIsIP(tls->serverName.data()))
    sniName = {tls->serverName.data(), tls->serverName.length() - 1};

  // try to use ECH (ignore broken ECHConfigList; it is delivered insecurely)
  if (properties != NULL) {
    if (!isSecondFlight && sniName.data() != NULL && tls->ctx->ech.client.ciphers != NULL) {
      if (properties->client.ech.configs.length() != 0) {
	DecodedEchConfig decoded;
	cliDecodeECHConfigList(tls->ctx, &decoded, properties->client.ech.configs);
	if (decoded.kem != NULL && decoded.cipher != NULL) {
	  if ((ret = cliSetupECH(&tls->ech, &decoded, tls->ctx->random)) != 0) goto Exit;
	}
      } else if (properties->client.ech.configs.data() != NULL) {
	// zero-length config with non-NULL base indicates ECH greasing; NULL base means no ECH
	cliSetupECHGrease(
	  &tls->ech, tls->ctx->random, tls->ctx->ech.client.kems, tls->ctx->ech.client.ciphers,
	  sniName);
	tls->ech.state = Ztls_::EchState::Grease;
      }
    }
  }

  // use external PSK if provided
  if (tls->ctx->preSharedKey.identity.data() != NULL) {
    if (!isSecondFlight) {
      tls->offeredPsk = 1;
      tls->cipherSuite = tls->ctx->cipherSuites.first(tls->ctx->preSharedKey.hash);
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
    if (decodeStoredSessionTicket(
	  tls, &keyShare, &cipherSuite, &psk.secret, &obfuscatedTicketAge, &psk.identity,
	  &maxEarlyDataSize, properties->client.sessionTicket.data(),
	  properties->client.sessionTicket.data() + properties->client.sessionTicket.length()) ==
      0) {
      psk.label = "res binder";
      tls->offeredPsk = 1;
      // key-share selected by HRR should not be overridden
      if (tls->keyShare == NULL) tls->keyShare = keyShare;
      tls->cipherSuite = cipherSuite;
      if (
	!isSecondFlight && maxEarlyDataSize != 0 && properties->client.maxEarlyDataSize != NULL) {
	tls->usingEarlyData = 1;
	*properties->client.maxEarlyDataSize = maxEarlyDataSize;
      }
    } else {
      psk.secret = wireBytes(NULL, 0);
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
  if (!isSecondFlight) {
    if ((tls->keySchedule = keyScheduleNew(
	   tls->cipherSuite, &tls->ctx->cipherSuites, tls->ech.aead != NULL)) == NULL) {
      ret = Ztls_ErrorNoMemory;
      goto Exit;
    }
    if ((ret = keyScheduleExtract(tls->keySchedule, psk.secret)) != 0) goto Exit;
  }

  // start generating CH
  if ((ret = emitter->beginMessage()) != 0) goto Exit;
  messStart = emitter->buf->off;

  // generate true (inner) CH
  if ((ret = encodeCliHello(
	 tls->ctx, emitter->buf, EncodeCHMode::Inner, isSecondFlight, properties,
	 tls->ech.aead != NULL ? tls->ech.innerClientRandom : tls->clientRandom, tls->keyShareCtx,
	 sniName, tls->legacySessionId, &tls->ech, NULL,
	 wireBytes(tls->ech.client.firstEch.data(), tls->ech.client.firstEch.length()),
	 psk.secret, psk.identity, obfuscatedTicketAge,
	 tls->keySchedule->hashes[0].algo->digestSize, cookie, tls->usingEarlyData)) != 0)
    goto Exit;

  // update the message hash, filling in the PSK binder HMAC if necessary
  if (psk.secret.data() != NULL) {
    if ((ret = deriveEmptySecret(tls->keySchedule, binderKey, psk.label)) != 0) goto Exit;
  }
  if ((ret = updateChHashAndBinder(
	 tls->keySchedule, emitter->buf->base, messStart, emitter->buf->off,
	 psk.secret.data() != NULL, binderKey, 0)) != 0)
    goto Exit;

  // ECH
  if (tls->ech.aead != NULL) {
    // build EncodedCHInner
    if ((ret = encodeCliHello(
	   tls->ctx, &encodedChInner, EncodeCHMode::EncodedInner, isSecondFlight, properties,
	   tls->ech.innerClientRandom, tls->keyShareCtx, sniName, tls->legacySessionId, &tls->ech,
	   NULL, wireBytes(NULL, 0), psk.secret, psk.identity, obfuscatedTicketAge,
	   tls->keySchedule->hashes[0].algo->digestSize, cookie, tls->usingEarlyData)) != 0)
      goto Exit;
    if (psk.secret.data() != NULL)
      memcpy(
	encodedChInner.base + encodedChInner.off - tls->keySchedule->hashes[0].algo->digestSize,
	emitter->buf->base + emitter->buf->off - tls->keySchedule->hashes[0].algo->digestSize,
	tls->keySchedule->hashes[0].algo->digestSize);
    {
      // pad EncodedCHInner (following draft-ietf-tls-esni-15 6.1.3)
      size_t paddingLen;
      if (sniName.data() != NULL) {
	paddingLen = sniName.length();
	if (paddingLen < tls->ech.client.maxNameLength)
	  paddingLen = tls->ech.client.maxNameLength;
      } else {
	paddingLen = tls->ech.client.maxNameLength + 9;
      }
      size_t finalLen = encodedChInner.off - Ztls_HSHeaderSize + paddingLen;
      finalLen = (finalLen + 31)/32 * 32;
      paddingLen = finalLen - (encodedChInner.off - Ztls_HSHeaderSize);
      if (paddingLen != 0) {
	if ((ret = bufferRes(&encodedChInner, paddingLen, 1)) != 0) goto Exit;
	memset(encodedChInner.base + encodedChInner.off, 0, paddingLen);
	encodedChInner.off += paddingLen;
      }
    }
    // flush CHInner, build CHOuterAAD
    emitter->buf->off = messStart;
    size_t echPayloadSize = encodedChInner.off - Ztls_HSHeaderSize + tls->ech.aead->algo->tagSize,
	   echSizeOffset = echPayloadSize;
    if ((ret = encodeCliHello(
	   tls->ctx, emitter->buf, EncodeCHMode::Outer, isSecondFlight, properties,
	   tls->clientRandom, tls->keyShareCtx,
	   {tls->ech.client.publicName.data(), tls->ech.client.publicName.length() - 1},
	   tls->legacySessionId, &tls->ech, &echSizeOffset, wireBytes(NULL, 0), psk.secret,
	   psk.identity, obfuscatedTicketAge, tls->keySchedule->hashes[0].algo->digestSize,
	   cookie, tls->usingEarlyData)) != 0)
      goto Exit;
    // overwrite ECH payload
    aeadEncrypt(
      tls->ech.aead, emitter->buf->base + echSizeOffset, encodedChInner.base + Ztls_HSHeaderSize,
      encodedChInner.off - Ztls_HSHeaderSize, isSecondFlight,
      emitter->buf->base + messStart + Ztls_HSHeaderSize,
      emitter->buf->off - (messStart + Ztls_HSHeaderSize));
    // keep the copy of the 1st ECH extension so that we can send it again in 2nd CH in response
    // to rejection with HRR
    if (!isSecondFlight) {
      size_t len = outerECHHeaderSize(tls->ech.client.enc.length()) + echPayloadSize;
      tls->ech.client.firstEch.copy(
	emitter->buf->base + echSizeOffset - outerECHHeaderSize(tls->ech.client.enc.length()),
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
      aeadFree(tls->ech.aead);
      tls->ech.aead = NULL;
      if ((ret = updateChHashAndBinder(
	     tls->keySchedule, emitter->buf->base, messStart, emitter->buf->off,
	     psk.secret.data() != NULL, binderKey, 0)) != 0)
	goto Exit;
    } else {
      // update outer hash
      keyScheduleUpdateHash(
	tls->keySchedule, emitter->buf->base + messStart, emitter->buf->off - messStart, 1);
    }
  }

  // commit CH to the record layer
  if ((ret = emitter->commitMessage()) != 0) goto Exit;

  if (tls->usingEarlyData) {
    assert(!isSecondFlight);
    if ((ret = setupTrafficProtection(tls, 1, "c e traffic", 1, 0, 0)) != 0) goto Exit;
    if ((ret = pushChangeCipherSpec(tls, emitter)) != 0) goto Exit;
  }
  if (psk.secret.data() != NULL && !isSecondFlight) {
    if ((ret = deriveExporterSecret(tls, 1)) != 0) goto Exit;
  }
  tls->state = cookie == NULL ? Ztls_::State::CliWaitSrvHello : Ztls_::State::CliWait2ndSrvHello;
  ret = Ztls_ErrorInProgress;

Exit:
  bufferDispose(&encodedChInner);
  ZuClear(binderKey, sizeof(binderKey));
  return ret;
}

static int
decodeSrvHello(Ztls::Cxn *tls, ServerHello *sh, const uint8_t *src, const uint8_t *const end)
{
  uint16_t exttype, foundVer = UINT16_MAX, selectedPSKIdentity = UINT16_MAX;
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
  sh->isRetryRequest = memcmp(src, helloRetryRandom, Ztls_HelloRandomSize) == 0;
  src += Ztls_HelloRandomSize;

  // legacySessionId
  Ztls_DecodeOpenBlock(src, end, 1, {
    if (end - src > 32) {
      ret = Ztls_AlertDecodeError;
      goto Exit;
    }
    sh->legacySessionId = wireBytes(src, end - src);
    src = end;
  });

  {
    // select cipherSuite
    uint16_t csid;
    if ((ret = decode16(&csid, &src, end)) != 0) goto Exit;
    if (!tls->cipherSuite || tls->cipherSuite->id != csid) {
      if ((tls->cipherSuite = tls->ctx->cipherSuites.find(csid)) == NULL) {
	ret = Ztls_AlertIllegalParameter;
	goto Exit;
      }
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
		 tls, Ztls_HsTypeSrvHello, exttype, wireBytes(src, end - src)) != 0))
	goto Exit;
      switch (exttype) {
      case Ztls_ExtVersions:
	if ((ret = decode16(&foundVer, &src, end)) != 0) goto Exit;
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
	  if ((ret = decodeKeyShareEntry(&group, &sh->peerkey, &src, end)) != 0) goto Exit;
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
	  sh->retryRequest.cookie = wireBytes(src, end - src);
	  src = end;
	});
	break;
      case Ztls_ExtTypePreSharedKey:
	assert(!sh->isRetryRequest);
	if ((ret = decode16(&selectedPSKIdentity, &src, end)) != 0) goto Exit;
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

  if (!isSupportedVer(foundVer)) {
    ret = Ztls_AlertIllegalParameter;
    goto Exit;
  }
  if (!sh->isRetryRequest) {
    if (selectedPSKIdentity != UINT16_MAX) {
      if (!tls->offeredPsk) {
	ret = Ztls_AlertIllegalParameter;
	goto Exit;
      }
      if (selectedPSKIdentity != 0) {
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

static int handleHelloRetryReq(
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
      aeadFree(tls->txTraffic.aead);
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

  ret = sendCliHello(tls, emitter, properties, &sh->retryRequest.cookie);

Exit:
  return ret;
}

static int
cliECHSelectHello(Ztls::Cxn *tls, Bytes message, size_t confirmHashOff, const char *label)
{
  uint8_t confirmHashDelivered[Ztls_ECHConfirmLength], confirmHashExpected[Ztls_ECHConfirmLength];
  int	  ret = 0;

  // Determine if ECH has been accepted by checking the confirmation hash. `confirmHashOff` set
  // to zero indicates that HRR was received wo. ECH extension, which is an indication that ECH
  // was rejected.
  if (confirmHashOff != 0) {
    memcpy(confirmHashDelivered, message.data() + confirmHashOff, sizeof(confirmHashDelivered));
    memset(message.data() + confirmHashOff, 0, sizeof(confirmHashDelivered));
    if (
      (ret = echCalcConfirmation(
	 tls->keySchedule, confirmHashExpected, tls->ech.innerClientRandom, label, message)) != 0)
      goto Exit;
    int accepted = Ztls::ctEqual(
      {confirmHashDelivered, sizeof(confirmHashDelivered)},
      {confirmHashExpected, sizeof(confirmHashDelivered)});
    memcpy(message.data() + confirmHashOff, confirmHashDelivered, sizeof(confirmHashDelivered));
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
  aeadFree(tls->ech.aead);
  tls->ech.aead = NULL;
  keyScheduleSelectOuter(tls->keySchedule);

Exit:
  ZuClear(confirmHashExpected, sizeof(confirmHashExpected));
  return ret;
}

static int
cliHandleHello(Ztls::Cxn *tls, MessageEmitter *emitter, Bytes message, HandshakeProps *properties)
{
  ServerHello	   sh;
  KeyExchangeBytes ecdhSecret;
  int		   ret;

  if ((ret = decodeSrvHello(
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
    if ((ret = keyScheduleSelectCipher(
	   tls->keySchedule, tls->cipherSuite, 0, tls->ctx->preSharedKey.secret)) != 0)
      goto Exit;
    keySchedPostCh1hash(tls->keySchedule);
    if (tls->ech.aead != NULL) {
      size_t confirmHashOff = 0;
      if (tls->ech.state != Ztls_::EchState::Grease) {
	if (sh.retryRequest.ech != NULL) confirmHashOff = sh.retryRequest.ech - message.data();
      }
      if ((ret = cliECHSelectHello(tls, message, confirmHashOff, Ztls_ECHConfirmationHRR)) != 0)
	goto Exit;
    }
    keyScheduleUpdateHash(tls->keySchedule, message.data(), message.length(), 0);
    return handleHelloRetryReq(tls, emitter, &sh, message, properties);
  }

  if ((ret = keyScheduleSelectCipher(
	 tls->keySchedule, tls->cipherSuite, tls->offeredPsk && !tls->isPskHandshake,
	 wireBytes(NULL, 0))) != 0)
    goto Exit;

  // check if ECH is accepted
  if (tls->ech.aead != NULL) {
    size_t confirmHashOff = 0;
    if (tls->ech.state != Ztls_::EchState::Grease) {
      confirmHashOff = Ztls_HSHeaderSize +
	2
	// legacyVersion
	+ Ztls_HelloRandomSize - Ztls_ECHConfirmLength;
    }
    if (
      (ret = cliECHSelectHello(tls, message, confirmHashOff, Ztls_ECHConfirmationSrvHello)) != 0)
      goto Exit;
  }

  // clear sensitive and space-consuming ECH state, now that are done with handling sending and
  // decoding Hellos
  clearECH(&tls->ech, 0);
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

  keyScheduleUpdateHash(tls->keySchedule, message.data(), message.length(), 0);

  if (sh.peerkey.data() != NULL) {
    ret = tls->keyShareCtx->exchange(&ecdhSecret, sh.peerkey);
    delete tls->keyShareCtx;
    tls->keyShareCtx = nullptr;
    if (ret != 0) {
      assert(!ecdhSecret.length());
      goto Exit;
    }
  }

  if ((ret = keyScheduleExtract(
	 tls->keySchedule, wireBytes(ecdhSecret.data(), ecdhSecret.length()))) != 0)
    goto Exit;
  if ((ret = setupTrafficProtection(tls, 0, "s hs traffic", 2, 0, 0)) != 0) goto Exit;
  if (tls->usingEarlyData) {
    tls->pendingHandshakeSecret.length(Ztls_MaxDigestSize);
    if ((ret = deriveSecret(
	   tls->keySchedule, tls->pendingHandshakeSecret.data(), "c hs traffic")) != 0)
      goto Exit;
    if (tls->ctx->updateTrafficKey != NULL &&
      (ret = tls->ctx->updateTrafficKey->call(tls, 1, 2, tls->pendingHandshakeSecret.data())) !=
	0)
      goto Exit;
  } else {
    if ((ret = setupTrafficProtection(tls, 1, "c hs traffic", 2, 0, 0)) != 0) goto Exit;
  }

  tls->state = Ztls_::State::CliWaitEncExts;
  ret = Ztls_ErrorInProgress;

Exit:
  return ret;
}

static int collectUnknownExt(Ztls::Cxn *tls, HandshakeProps *properties, uint16_t type)
{
  return properties != NULL && properties->extensions != NULL &&
    properties->extensions->collect(tls, type);
}

static int collectUnknownExtension(
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
    slots[i].data = wireBytes(src, end - src);
    slots[i + 1].type = UINT16_MAX;
  }
  return 0;
}

static int reportUnknownExts(Ztls::Cxn *tls, HandshakeProps *properties, RawExtension *slots)
{
  if (properties != NULL && properties->extensions != NULL) {
    return properties->extensions->collected(tls, slots);
  } else {
    return 0;
  }
}

static int cliHandleEncExts(Ztls::Cxn *tls, Bytes message, HandshakeProps *properties)
{
  const uint8_t	      *src = message.data() + Ztls_HSHeaderSize,
		*const end = message.data() + message.length();
  uint16_t	       type;
  auto		       extensions =
    ZtScratch(CollectedExtensions, ZtlsMaxUnknownExts + 1, ZtlsMaxUnknownExts + 1);
  auto unknownExtensions = extensions.data();
  unknownExtensions[0] = {UINT16_MAX};
  int	  ret, skipEarlyData = 1;
  uint8_t srvOfferedCertType = Ztls_CertTypeX509;

  Ztls_DecodeExtensions(src, end, Ztls_HsTypeEncExts, &type, {
    if (tls->ctx->onExtension != NULL &&
      (ret = tls->ctx->onExtension->call(
	       tls, Ztls_HsTypeEncExts, type, wireBytes(src, end - src)) != 0))
      goto Exit;
    switch (type) {
    case Ztls_ExtTypeSrvName:
      if (src != end) {
	ret = Ztls_AlertDecodeError;
	goto Exit;
      }
      if (!(tls->serverName.data() != NULL && !serverNameIsIP(tls->serverName.data()))) {
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
	  if ((ret = negotiatedProtocol(
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
      skipEarlyData = 0;
      break;
    case Ztls_ExtTypeSrvCertType:
      if (end - src != 1) {
	ret = Ztls_AlertDecodeError;
	goto Exit;
      }
      srvOfferedCertType = *src;
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
      if ((ret = cliDecodeECHConfigList(tls->ctx, &decoded, wireBytes(src, end - src))) != 0)
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
      if (collectUnknownExt(tls, properties, type)) {
	if ((ret = collectUnknownExtension(tls, type, src, end, unknownExtensions)) != 0)
	  goto Exit;
      }
      break;
    }
    src = end;
  });

  if (srvOfferedCertType !=
    (tls->ctx->useRawPublicKeys ? Ztls_CertTypeRawPubkey : Ztls_CertTypeX509)) {
    ret = Ztls_AlertUnsupportedCert;
    goto Exit;
  }

  if (tls->usingEarlyData) {
    if (skipEarlyData) tls->usingEarlyData = 0;
    if (properties != NULL)
      properties->client.earlyDataAcceptance = skipEarlyData ? EarlyDataAcceptance::Rejected :
							       EarlyDataAcceptance::Accepted;
  }
  if ((ret = reportUnknownExts(tls, properties, unknownExtensions)) != 0) goto Exit;

  keyScheduleUpdateHash(tls->keySchedule, message.data(), message.length(), 0);
  tls->state = tls->isPskHandshake ? Ztls_::State::CliWaitFinished : Ztls_::State::CliWaitCertReq;
  ret = Ztls_ErrorInProgress;

Exit:
  return ret;
}

static int decodeCertReq(
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
    cr->context = wireBytes(context.data(), len);
    src = end;
  });

  // decode extensions
  Ztls_DecodeExtensions(src, end, Ztls_HsTypeCertRequest, &exttype, {
    if (tls->ctx->onExtension != NULL &&
      (ret = tls->ctx->onExtension->call(
	       tls, Ztls_HsTypeCertRequest, exttype, wireBytes(src, end - src)) != 0))
      goto Exit;
    switch (exttype) {
    case Ztls_ExtTypeSigAlgos:
      if ((ret = decodeSigAlgos(&cr->signatureAlgorithms, &src, end)) != 0) goto Exit;
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
static int
buildCertMessage_(Buffer *buf, Bytes context, const Certificates &certificates, Bytes ocspStatus)
{
  int ret;

  Ztls_BufferPushBlock(buf, 1, { Ztls_BufferPushv(buf, context.data(), context.length()); });
  Ztls_BufferPushBlock(buf, 3, {
    for (size_t i = 0; i != certificates.length(); ++i) {
      const auto &cert = certificates[i];
      Ztls_BufferPushBlock(buf, 3, { Ztls_BufferPushv(buf, cert.data(), cert.length()); });
      Ztls_BufferPushBlock(buf, 2, {
	if (i == 0 && ocspStatus.length() != 0) {
	  Ztls_BufferPushExtension(buf, Ztls_ExtTypeStatusRequest, {
	    Ztls_BufferPush_1(buf, 1);
	    // status_type == ocsp
	    Ztls_BufferPushBlock(
	      buf, 3, { Ztls_BufferPushv(buf, ocspStatus.data(), ocspStatus.length()); });
	  });
	}
      });
    }
  });

  ret = 0;
Exit:
  return ret;
}

int buildCertMessage(
  Buffer *buf, Bytes context, ZuSpan<const Bytes> certificates, Bytes ocspStatus)
{ return buildCertMessage_(buf, context, certificates, ocspStatus); }

struct DefaultEmitCertificate : EmitCertificate {
  int call(
    Ztls::Cxn *tls, MessageEmitter *emitter, KeySchedule *keySched, Bytes context,
    int pushStatusReq, ZuSpan<const uint16_t> compressAlgos) override
  {
    int ret;

    Ztls_PushMessage(emitter, keySched, Ztls_HsTypeCert, {
      auto &certs = tls->ctx->certificates;
      if ((ret = buildCertMessage_(emitter->buf, context, certs, {})) != 0) goto Exit;
    });

    ret = 0;
Exit:
    return ret;
  }
};

static int sendCert(
  Ztls::Cxn *tls, MessageEmitter *emitter, SignatureAlgorithms *signatureAlgorithms,
  Bytes context, int pushStatusReq, ZuSpan<const uint16_t> compressAlgos)
{
  int ret;

  if (!signatureAlgorithms->length()) {
    ret = Ztls_AlertMissingExt;
    goto Exit;
  }

  {
    // send Certificate (or the equivalent)
    static DefaultEmitCertificate defaultEmitCert;
    EmitCertificate		 *emitCertificate =
      tls->ctx->emitCertificate != NULL ? tls->ctx->emitCertificate : &defaultEmitCert;
Redo:
    if ((ret = emitCertificate->call(
	   tls, emitter, tls->keySchedule, context, pushStatusReq, compressAlgos)) != 0) {
      if (ret == Ztls_ErrorDelegate) {
	assert(emitCertificate != &defaultEmitCert);
	emitCertificate = &defaultEmitCert;
	goto Redo;
      }
      goto Exit;
    }
  }

Exit:
  return ret;
}

static int sendCertVerify(
  Ztls::Cxn *tls, MessageEmitter *emitter, SignatureAlgorithms *signatureAlgorithms,
  const char *ctxString)
{
  size_t startOff = emitter->buf->off;
  int	 ret;

  if (tls->ctx->signCertificate == NULL) return 0;
  // build and send CertificateVerify
  Ztls_PushMessage(emitter, tls->keySchedule, Ztls_HsTypeCertVerify, {
    Buffer *sendbuf = emitter->buf;
    size_t  algoOff = sendbuf->off;
    Ztls_BufferPush16(sendbuf, 0);
    // filled in later
    Ztls_BufferPushBlock(sendbuf, 2, {
      uint16_t algo;
      if (auto resume = tls->signatureResume) {
	ret = resume(sendbuf, &algo);
      } else {
	uint8_t data[Ztls_SignInputSize];
	size_t	datalen = buildCertVerifySigndata(data, tls->keySchedule, ctxString);
	ret = tls->ctx->signCertificate->call(
	  tls, tls->isServer, &algo, sendbuf, wireBytes(data, datalen),
	  signatureAlgorithms ? ZuSpan<const uint16_t>{*signatureAlgorithms} :
				ZuSpan<const uint16_t>{});
	ZuClear(data, sizeof(data));
      }
      if (ret == Ztls_ErrorAsyncOperation) {
	assert(tls->isServer && tls->signatureResume && tls->signatureRetire);
	// CertificateVerify is rebuilt when the signature becomes available.
	emitter->buf->off = startOff;
	goto Exit;
      }
      if (ret) goto Exit;
      sendbuf->base[algoOff] = (uint8_t)(algo >> 8);
      sendbuf->base[algoOff + 1] = (uint8_t)algo;
    });
  });
Exit:
  return ret;
}

static int cliHandleCertReq(Ztls::Cxn *tls, Bytes message, HandshakeProps *properties)
{
  const uint8_t	      *src = message.data() + Ztls_HSHeaderSize,
		*const end = message.data() + message.length();
  int		       ret = 0;

  assert(
    !tls->isPskHandshake &&
    "state machine asserts that this message is never delivered when PSK is used");

  if ((ret = decodeCertReq(tls, &tls->certificateRequest, src, end)) != 0) return ret;

  // This field SHALL be zero length unless used for the post-handshake authentication exchanges
  // (section 4.3.2)
  if (tls->certificateRequest.context.length() != 0) return Ztls_AlertIllegalParameter;

  tls->state = Ztls_::State::CliExpectCert;
  keyScheduleUpdateHash(tls->keySchedule, message.data(), message.length(), 0);

  return Ztls_ErrorInProgress;
}

static int handleCert(Ztls::Cxn *tls, const uint8_t *src, const uint8_t *end, int *gotCerts)
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
	certs.push(wireBytes(src, end - src));
	src = end;
      });
      uint16_t type;
      Ztls_DecodeOpenExtensions(src, end, Ztls_HsTypeCert, &type, {
	if (tls->ctx->onExtension != NULL &&
	  (ret = tls->ctx->onExtension->call(
		   tls, Ztls_HsTypeCert, type, wireBytes(src, end - src)) != 0))
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

  *gotCerts = certs.length() != 0;

Exit:
  return ret;
}

static int cliDoHandleCert(Ztls::Cxn *tls, const uint8_t *src, const uint8_t *end)
{
  int gotCerts, ret;

  if ((ret = handleCert(tls, src, end, &gotCerts)) != 0) return ret;
  if (!gotCerts) return Ztls_AlertIllegalParameter;

  return 0;
}

static int cliHandleCert(Ztls::Cxn *tls, Bytes message)
{
  int ret;

  if ((ret = cliDoHandleCert(
	 tls, message.data() + Ztls_HSHeaderSize, message.data() + message.length())) != 0)
    return ret;

  keyScheduleUpdateHash(tls->keySchedule, message.data(), message.length(), 0);

  tls->state = Ztls_::State::CliWaitCertSig;
  return Ztls_ErrorInProgress;
}

ZuDerive(
  UncompressedCertificate, (ZtBArray<ZtArrayHeapID<"Ztls.Engine.UncompressedCertificate">>));

static int cliHandleCompressedCert(Ztls::Cxn *tls, Bytes message)
{
  const uint8_t		 *src = message.data() + Ztls_HSHeaderSize,
		*const	  end = message.data() + message.length();
  uint16_t		  algo;
  uint32_t		  uncompressedSize;
  UncompressedCertificate uncompressed;
  int			  ret;

  if (tls->ctx->decompressCertificate == NULL) {
    ret = Ztls_AlertUnwaitedMsg;
    goto Exit;
  }

  // decode
  if ((ret = decode16(&algo, &src, end)) != 0) goto Exit;
  if ((ret = decode24(&uncompressedSize, &src, end)) != 0) goto Exit;
  if (uncompressedSize > 65536) {
    // TODO find a sensible number
    ret = Ztls_AlertBadCert;
    goto Exit;
  }
  uncompressed.length(uncompressedSize ? uncompressedSize : 1);
  Ztls_DecodeBlock(src, end, 3, {
    if ((ret = tls->ctx->decompressCertificate->call(
	   tls, algo, wireBytes(uncompressed.data(), uncompressedSize),
	   wireBytes(src, end - src))) != 0)
      goto Exit;
    src = end;
  });

  // handle
  if ((ret = cliDoHandleCert(tls, uncompressed.data(), uncompressed.data() + uncompressedSize)) !=
    0)
    goto Exit;

  keyScheduleUpdateHash(tls->keySchedule, message.data(), message.length(), 0);
  tls->state = Ztls_::State::CliWaitCertSig;
  ret = Ztls_ErrorInProgress;

Exit:
  return ret;
}

static int srvHandleCert(Ztls::Cxn *tls, Bytes message)
{
  int gotCerts, ret;

  if ((ret = handleCert(
	 tls, message.data() + Ztls_HSHeaderSize, message.data() + message.length(),
	 &gotCerts)) != 0)
    return ret;

  keyScheduleUpdateHash(tls->keySchedule, message.data(), message.length(), 0);

  if (gotCerts) {
    tls->state = Ztls_::State::SrvWaitCertSig;
  } else {
    // Client did not provide certificate, and the verifier says we can fail open. Therefore, the
    // next message is Finished.
    tls->state = Ztls_::State::SrvWaitFinished;
  }

  return Ztls_ErrorInProgress;
}

static int handleCertVerify(Ztls::Cxn *tls, Bytes message, const char *ctxString)
{
  const uint8_t	      *src = message.data() + Ztls_HSHeaderSize,
		*const end = message.data() + message.length();
  uint16_t	       algo;
  Bytes		       signature;
  uint8_t	       signdata[Ztls_SignInputSize];
  size_t	       signdataSize;
  int		       ret;
  auto		       verifier = tls->certificateVerify;
  tls->certificateVerify = nullptr;
  ZuGuard release{[verifier]() { delete verifier; }};

  // decode
  if ((ret = decode16(&algo, &src, end)) != 0) goto Exit;
  Ztls_DecodeBlock(src, end, 2, {
    signature = wireBytes(src, end - src);
    src = end;
  });

  signdataSize = buildCertVerifySigndata(signdata, tls->keySchedule, ctxString);
  if (verifier != nullptr) {
    ret = verifier->verify(algo, wireBytes(signdata, signdataSize), signature);
  } else {
    ret = 0;
  }
  ZuClear(signdata, signdataSize);
  if (ret != 0) { goto Exit; }

  keyScheduleUpdateHash(tls->keySchedule, message.data(), message.length(), 0);

Exit:
  return ret;
}

static int cliHandleCertVerify(Ztls::Cxn *tls, Bytes message)
{
  int ret = handleCertVerify(tls, message, Ztls_SrvCertVerifyCtx);

  if (ret == 0) {
    tls->state = Ztls_::State::CliWaitFinished;
    ret = Ztls_ErrorInProgress;
  }

  return ret;
}

static int srvHandleCertVerify(Ztls::Cxn *tls, Bytes message)
{
  int ret = handleCertVerify(tls, message, Ztls_CliCertVerifyCtx);

  if (ret == 0) {
    tls->state = Ztls_::State::SrvWaitFinished;
    ret = Ztls_ErrorInProgress;
  }

  return ret;
}

static int cliHandleFinished(Ztls::Cxn *tls, MessageEmitter *emitter, Bytes message)
{
  uint8_t sendSecret[Ztls_MaxDigestSize];
  int	  alertECHRequired = tls->ech.state == Ztls_::EchState::Offered, ret;

  if ((ret = verifyFinished(tls, message)) != 0) goto Exit;
  keyScheduleUpdateHash(tls->keySchedule, message.data(), message.length(), 0);

  // update traffic keys by using messages upto ServerFinished, but commission them after sending
  // ClientFinished
  if ((ret = keyScheduleExtract(tls->keySchedule, wireBytes(NULL, 0))) != 0) goto Exit;
  if ((ret = setupTrafficProtection(tls, 0, "s ap traffic", 3, 0, 0)) != 0) goto Exit;
  if ((ret = deriveSecret(tls->keySchedule, sendSecret, "c ap traffic")) != 0) goto Exit;
  if ((ret = deriveExporterSecret(tls, 0)) != 0) goto Exit;

  //  if sending early data, emit EOED and commision the client handshake traffic secret
  if (tls->pendingHandshakeSecret.data() != NULL) {
    assert(tls->txTraffic.aead != NULL || tls->ctx->updateTrafficKey != NULL);
    if (tls->usingEarlyData && !tls->ctx->omitEndOfEarlyData)
      Ztls_PushMessage(emitter, tls->keySchedule, Ztls_HsTypeEndOfEarly, {});
    tls->usingEarlyData = 0;
    if ((ret = commissionHandshakeSecret(tls)) != 0) goto Exit;
  }

  if ((ret = pushChangeCipherSpec(tls, emitter)) != 0) goto Exit;

  if (!alertECHRequired && tls->certificateRequest.context.data() != NULL) {
    if ((ret = sendCert(
	   tls, emitter, &tls->certificateRequest.signatureAlgorithms,
	   tls->certificateRequest.context, 0, {})) == 0)
      ret = sendCertVerify(
	tls, emitter, &tls->certificateRequest.signatureAlgorithms, Ztls_CliCertVerifyCtx);
    tls->certificateRequestBytes.null();
    tls->certificateRequest.context = wireBytes(NULL, 0);
    if (ret != 0) goto Exit;
  }

  ret = sendFinished(tls, emitter);

  memcpy(tls->txTraffic.secret, sendSecret, sizeof(sendSecret));
  if ((ret = setupTrafficProtection(tls, 1, NULL, 3, 0, 0)) != 0) goto Exit;

  tls->state = Ztls_::State::CliPostHS;

  // if ECH was rejected, close the connection with ECH_REQUIRED alert after verifying messages up
  // to Finished
  if (alertECHRequired) ret = Ztls_AlertECHRequired;

Exit:
  ZuClear(sendSecret, sizeof(sendSecret));
  return ret;
}

static int cliHandleNewTicket(Ztls::Cxn *tls, Bytes message)
{
  const uint8_t	      *src = message.data() + Ztls_HSHeaderSize,
		*const end = message.data() + message.length();
  Bytes		       ticketNonce;
  int		       ret;

  {
    // verify the format
    uint32_t ticketLifetime, ticketAgeAdd, maxEarlyDataSize;
    Bytes    ticket;
    if ((ret = decodeNewSessionTicket(
	   tls, &ticketLifetime, &ticketAgeAdd, &ticketNonce, &ticket, &maxEarlyDataSize, src,
	   end)) != 0)
      return ret;
  }

  // do nothing if use of session ticket is disabled
  if (tls->ctx->saveTicket == NULL) return 0;

  // save the extension, along with the key of myself
  Buffer ticketBuf;
  bufferInitRx(&ticketBuf, const_cast<char *>(""), 0);
  Ztls_BufferPush64(&ticketBuf, tls->ctx->getTime->call());
  Ztls_BufferPush16(&ticketBuf, tls->keyShare->id);
  Ztls_BufferPush16(&ticketBuf, tls->cipherSuite->id);
  Ztls_BufferPushBlock(&ticketBuf, 3, { Ztls_BufferPushv(&ticketBuf, src, end - src); });
  Ztls_BufferPushBlock(&ticketBuf, 2, {
    if ((ret = bufferRes(&ticketBuf, tls->keySchedule->hashes[0].algo->digestSize, 0)) != 0)
      goto Exit;
    if ((ret = deriveResumptionSecret(
	   tls->keySchedule, ticketBuf.base + ticketBuf.off, ticketNonce)) != 0)
      goto Exit;
    ticketBuf.off += tls->keySchedule->hashes[0].algo->digestSize;
  });

  if ((ret = tls->ctx->saveTicket->call(tls, wireBytes(ticketBuf.base, ticketBuf.off))) != 0)
    goto Exit;

  ret = 0;
Exit:
  bufferDispose(&ticketBuf);
  return ret;
}

static int cliHelloDecodeSrvName(Bytes *name, const uint8_t **src, const uint8_t *const end)
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
	  *name = wireBytes(*src, end - *src);
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

static int selectNegotiatedGroup(
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

static int decodeCliHello(
  Context *ctx, ClientHello *ch, const uint8_t *src, const uint8_t *const end,
  HandshakeProps *properties, Ztls::Cxn *tlsCbarg)
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
    ch->legacySessionId = wireBytes(src, end - src);
    src = end;
  });

  // decode and select from ciphersuites
  Ztls_DecodeOpenBlock(src, end, 2, {
    if ((end - src) % 2 != 0) {
      ret = Ztls_AlertDecodeError;
      goto Exit;
    }
    ch->cipherSuites = wireBytes(src, end - src);
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
    if (ctx->onExtension != NULL && tlsCbarg != NULL &&
      (ret = ctx->onExtension->call(
	       tlsCbarg, Ztls_HsTypeCliHello, exttype, wireBytes(src, end - src)) != 0))
      goto Exit;
    switch (exttype) {
    case Ztls_ExtTypeSrvName:
      if ((ret = cliHelloDecodeSrvName(&ch->serverName, &src, end)) != 0) goto Exit;
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
	    ch->alpn.push(wireBytes(src, end - src));
	    src = end;
	  });
	} while (src != end);
      });
      break;
    case Ztls_ExtTypeSrvCertType:
      Ztls_DecodeBlock(src, end, 1, {
	size_t listSize = end - src;

	// RFC7250 4.1: No empty list, no list with single x509 element
	if (listSize == 0 || (listSize == 1 && *src == Ztls_CertTypeX509)) {
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
      ch->negotiatedGroups = wireBytes(src, end - src);
      break;
    case Ztls_ExtTypeSigAlgos:
      if ((ret = decodeSigAlgos(&ch->signatureAlgorithms, &src, end)) != 0) goto Exit;
      break;
    case Ztls_ExtTypeKeyShare:
      ch->keyShares = wireBytes(src, end - src);
      break;
    case Ztls_ExtVersions:
      Ztls_DecodeBlock(src, end, 1, {
	size_t selectedIndex = ZuTraits<decltype(supportedVersions)>::length(supportedVersions);
	do {
	  size_t   i;
	  uint16_t v;
	  if ((ret = decode16(&v, &src, end)) != 0) goto Exit;
	  for (i = 0; i != selectedIndex; ++i) {
	    if (supportedVersions[i] == v) {
	      selectedIndex = i;
	      break;
	    }
	  }
	} while (src != end);
	if (selectedIndex != ZuTraits<decltype(supportedVersions)>::length(supportedVersions))
	  ch->selectedVersion = supportedVersions[selectedIndex];
      });
      break;
    case Ztls_ExtTypeCookie:
      if (properties == NULL || properties->server.cookie.key == NULL) {
	ret = Ztls_AlertIllegalParameter;
	goto Exit;
      }
      ch->cookie.all = wireBytes(src, end - src);
      Ztls_DecodeBlock(src, end, 2, {
	const auto tbs = src;
	Ztls_DecodeOpenBlock(src, end, 2, {
	  Ztls_DecodeOpenBlock(src, end, 1, {
	    ch->cookie.ch1Hash = wireBytes(src, end - src);
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
	ch->cookie.tbs = wireBytes(tbs, src - tbs);
	Ztls_DecodeBlock(src, end, 1, {
	  ch->cookie.signature = wireBytes(src, end - src);
	  src = end;
	});
      });
      break;
    case Ztls_ExtTypePreSharedKey: {
      size_t numIdentities = 0;
      Ztls_DecodeOpenBlock(src, end, 2, {
	do {
	  CliHelloPskIdentity psk{};
	  Ztls_DecodeOpenBlock(src, end, 2, {
	    if (end - src < 1) {
	      ret = Ztls_AlertDecodeError;
	      goto Exit;
	    }
	    psk.identity = wireBytes(src, end - src);
	    src = end;
	  });
	  if ((ret = decode32(&psk.obfuscatedTicketAge, &src, end)) != 0) goto Exit;
	  if (ch->psk.identities.count <
	    ZuTraits<decltype(ch->psk.identities.list)>::length(ch->psk.identities.list))
	    ch->psk.identities.list[ch->psk.identities.count++] = psk;
	  ++numIdentities;
	} while (src != end);
      });
      ch->psk.hashEnd = src;
      Ztls_DecodeBlock(src, end, 2, {
	size_t numBinders = 0;
	do {
	  Ztls_DecodeOpenBlock(src, end, 1, {
	    if (numBinders < ch->psk.identities.count)
	      ch->psk.identities.list[numBinders].binder = wireBytes(src, end - src);
	    src = end;
	  });
	  ++numBinders;
	} while (src != end);
	if (numIdentities != numBinders) {
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
	  ch->ech.enc = wireBytes(src, end - src);
	  src = end;
	});
	Ztls_DecodeOpenBlock(src, end, 2, {
	  if (src == end) {
	    ret = Ztls_AlertDecodeError;
	    goto Exit;
	  }
	  ch->ech.payload = wireBytes(src, end - src);
	  src = end;
	});
	break;
      case Ztls_ECHCliHelloInner:
	if (src != end) {
	  ret = Ztls_AlertDecodeError;
	  goto Exit;
	}
	ch->ech.payload = wireBytes("", 0);
	// non-zero base indicates that the extension was received
	break;
      default:
	ret = Ztls_AlertIllegalParameter;
	goto Exit;
      }
      src = end;
      break;
    default:
      if (tlsCbarg != NULL && collectUnknownExt(tlsCbarg, properties, exttype)) {
	if ((ret = collectUnknownExtension(tlsCbarg, exttype, src, end, ch->unknownExtensions)) !=
	  0)
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

static int rebuildChInnerExts(
  Buffer *buf, const uint8_t **src, const uint8_t *const end, const uint8_t *outerExt,
  const uint8_t *outerExtEnd)
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
		  if (decode16(&outertype, &outerExt, outerExtEnd) != 0 ||
		    decode16(&outersize, &outerExt, outerExtEnd) != 0) {
		    ret = Ztls_AlertIllegalParameter;
		    goto Exit;
		  }
		  assert(outerExtEnd - outerExt >= outersize);
		  if (outertype == reftype) break;
		  outerExt += outersize;
		}
		Ztls_BufferPushExtension(buf, reftype, {
		  Ztls_BufferPushv(buf, outerExt, outersize);
		  outerExt += outersize;
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

static int rebuildChInner(
  Buffer *buf, const uint8_t *src, const uint8_t *const end, ClientHello *outerCh,
  const uint8_t *outerExt, const uint8_t *outerExtEnd)
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
      Ztls_BufferPushv(buf, outerCh->legacySessionId.data(), outerCh->legacySessionId.length());
    });

    // cipher-suites and legacy-compression-methods
    Ztls_CopyBlock(2);
    Ztls_CopyBlock(1);

    // extensions
    if ((ret = rebuildChInnerExts(buf, &src, end, outerExt, outerExtEnd)) != 0) goto Exit;
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
static inline int callCliHello(
  Ztls::Cxn *tls, Bytes serverName, Bytes rawMessage, Bytes cipherSuites,
  ZuSpan<const Bytes> alpns, ZuSpan<const uint16_t> sigAlgos,
  ZuSpan<const uint16_t> certCompAlgos, ZuSpan<const uint8_t> srvCertTypes,
  ZuSpan<const CliHelloPskIdentity> pskIdentities, int incompatibleVersion)
{
  if (tls->ctx->onClientHello == NULL) return 0;

  CliHelloParams params = {serverName, rawMessage, cipherSuites, alpns, sigAlgos, certCompAlgos,
    srvCertTypes, pskIdentities, static_cast<unsigned>(incompatibleVersion)};
  return tls->ctx->onClientHello->call(tls, &params);
}

static int checkCliHelloConstraints(
  Context *ctx, ClientHello *ch, const uint8_t *prevRandom, int echIsInnerCh, Bytes rawMessage,
  Ztls::Cxn *tlsCbarg)
{
  int isSecondFlight = prevRandom != 0;

  // The following check is necessary so that we would be able to track the connection in
  // SSLKEYLOGFILE, even though it might not be for the safety of the protocol.
  if (isSecondFlight &&
    !Ztls::ctEqual({ch->randomBytes, Ztls_HelloRandomSize}, {prevRandom, Ztls_HelloRandomSize}))
    return Ztls_AlertHSFailure;

  // bail out if CH cannot be handled as TLS 1.3
  if (!isSupportedVer(ch->selectedVersion)) {
    // ECH: server MUST abort with an "illegal_parameter" alert if the client offers TLS 1.2 or
    // below (draft-15 7.1)
    if (echIsInnerCh) return Ztls_AlertIllegalParameter;
    // fail with PROTOCOL_VERSION alert, after providing the applications the raw CH and SNI to
    // help them fallback
    if (!isSecondFlight) {
      int ret;
      if ((ret = callCliHello(
	     tlsCbarg, ch->serverName, rawMessage, ch->cipherSuites, ch->alpn, {}, {}, {}, {},
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

  if (echIsInnerCh && ch->ech.payload.data() == NULL) return Ztls_AlertIllegalParameter;
  if (ch->ech.payload.data() != NULL &&
    ch->ech.type != (echIsInnerCh ? Ztls_ECHCliHelloInner : Ztls_ECHCliHelloOuter))
    return Ztls_AlertIllegalParameter;

  return 0;
}

static int vecIsString(Bytes x, const char *y)
{ return strncmp((const char *)x.data(), y, x.length()) == 0 && y[x.length()] == '\0'; }

// Looks for a PSK identity that can be used, and if found, updates the handshake state and
// returns the necessary variables. If `Context::preSharedKey` is set, only tries handshake using
// those keys provided. Otherwise, tries resumption.
static int tryPSKHandshake(
  Ztls::Cxn *tls, size_t *pskIndex, int *acceptEarlyData, ClientHello *ch, Bytes chTrunc,
  int isSecondFlight)
{
  Buffer   decbuf;
  Bytes	   secret, ticketCtx, ticketNegotiatedProtocol;
  uint64_t issueAt, now = tls->ctx->getTime->call();
  uint32_t ageAdd;
  uint16_t ticketKeyExchangeID, ticketCsid;
  uint8_t  binderKey[Ztls_MaxDigestSize];
  int	   ret;

  bufferInitRx(&decbuf, const_cast<char *>(""), 0);

  for (*pskIndex = 0; *pskIndex < ch->psk.identities.count; ++*pskIndex) {
    CliHelloPskIdentity *identity = ch->psk.identities.list + *pskIndex;

    // negotiate using fixed pre-shared key
    if (tls->ctx->preSharedKey.identity.data() != NULL) {
      if (identity->identity.length() == tls->ctx->preSharedKey.identity.length() &&
	memcmp(
	  identity->identity.data(), tls->ctx->preSharedKey.identity.data(),
	  identity->identity.length()) == 0) {
	*acceptEarlyData = ch->psk.earlyDataIndication && *pskIndex == 0;
	tls->keyShare = NULL;
	secret = tls->ctx->preSharedKey.secret;
	goto Found;
      }
      continue;
    }

    // decrypt ticket and decode
    if (tls->ctx->encryptTicket == NULL || tls->ctx->keyExchanges == NULL) continue;
    int canAcceptEarlyData = *pskIndex == 0;
    decbuf.off = 0;
    switch (tls->ctx->encryptTicket->call(tls, 0, &decbuf, identity->identity)) {
    case 0:
      // decrypted
      break;
    case Ztls_ErrorRejectEarlyData:
      // decrypted, but early data is rejected
      canAcceptEarlyData = 0;
      break;
    default:
      // decryption failure
      continue;
    }
    if (decodeSessionIdentifier(
	  &issueAt, &secret, &ageAdd, &ticketCtx, &ticketKeyExchangeID, &ticketCsid,
	  &ticketNegotiatedProtocol, decbuf.base, decbuf.base + decbuf.off) != 0)
      continue;
    // check age
    if (now < issueAt) continue;
    if (now - issueAt > (uint64_t)tls->ctx->ticketLifetime * 1000) continue;
    *acceptEarlyData = 0;
    if (ch->psk.earlyDataIndication && canAcceptEarlyData) {
      // accept early-data if abs(diff) between the reported age and the actual age is within +=
      // 10 seconds
      int64_t delta = (now - issueAt) - (identity->obfuscatedTicketAge - ageAdd);
      if (delta < 0) delta = -delta;
      if (tls->ctx->maxEarlyDataSize != 0 && delta <= ZtlsEarlyDataMaxDelay) *acceptEarlyData = 1;
    }
    // check ticket context
    if (tls->ctx->ticketContext.isSet) {
      if (!(ticketCtx.length() == sizeof(tls->ctx->ticketContext.bytes) &&
	    memcmp(ticketCtx.data(), tls->ctx->ticketContext.bytes, ticketCtx.length()) == 0))
	continue;
    } else {
      // check server-name
      if (ticketCtx.length() != 0) {
	if (tls->serverName.data() == NULL) continue;
	if (!vecIsString(ticketCtx, tls->serverName.data())) continue;
      } else {
	if (tls->serverName.data() != NULL) continue;
      }
    }
    {
      // check key-exchange
      const KeyExchangeAlgo **a;
      for (a = tls->ctx->keyExchanges; *a != NULL && (*a)->id != ticketKeyExchangeID; ++a);
      if (*a == NULL) continue;
      tls->keyShare = *a;
    }
    // check cipher-suite
    if (ticketCsid != tls->cipherSuite->id) continue;
    // check negotiated-protocol
    if (ticketNegotiatedProtocol.length() != 0) {
      if (tls->negotiatedProtocol.data() == NULL) continue;
      if (!vecIsString(ticketNegotiatedProtocol, tls->negotiatedProtocol.data())) continue;
    }
    // check the length of the decrypted psk and the PSK binder
    if (secret.length() != tls->keySchedule->hashes[0].algo->digestSize) continue;
    if (ch->psk.identities.list[*pskIndex].binder.length() !=
      tls->keySchedule->hashes[0].algo->digestSize)
      continue;

    // found
    goto Found;
  }

  // not found
  *pskIndex = SIZE_MAX;
  *acceptEarlyData = 0;
  tls->keyShare = NULL;
  ret = 0;
  goto Exit;

Found:
  if (isSecondFlight) {
    // HRR initialized the early secret without a ticket. Replace that secret,
    // retaining the message_hash(CH1) || HRR transcript for the CH2 binder.
    if (tls->ctx->preSharedKey.identity.data() == NULL &&
      (ret = keyScheduleSelectCipher(tls->keySchedule, tls->cipherSuite, 1, secret)) != 0)
      goto Exit;
  } else if ((ret = keyScheduleExtract(tls->keySchedule, secret)) != 0) {
    goto Exit;
  }
  if ((ret = deriveEmptySecret(
	 tls->keySchedule, binderKey,
	 tls->ctx->preSharedKey.secret.data() != NULL ? "ext binder" : "res binder")) != 0)
    goto Exit;
  keyScheduleUpdateHash(tls->keySchedule, chTrunc.data(), chTrunc.length(), 0);
  if ((ret = calcVerifyData(
	 binderKey
	 // to conserve space, reuse binderKey for storing verifyData
	 ,
	 tls->keySchedule, binderKey)) != 0)
    goto Exit;
  if (!Ztls::ctEqual(
	{ch->psk.identities.list[*pskIndex].binder.data(),
	  tls->keySchedule->hashes[0].algo->digestSize},
	{binderKey, tls->keySchedule->hashes[0].algo->digestSize})) {
    ret = Ztls_AlertDecryptError;
    goto Exit;
  }
  ret = 0;

Exit:
  bufferDispose(&decbuf);
  ZuClear(binderKey, sizeof(binderKey));
  return ret;
}

static int calcCookieSig(
  Ztls::Cxn *tls, HandshakeProps *properties, const KeyExchangeAlgo *negotiatedGroup, Bytes tbs,
  uint8_t *sig)
{
  const HashAlgo *algo = tls->ctx->cipherSuites[0]->hash;
  HashContext	 *hctx;

  if ((hctx = hmacCreate(algo, properties->server.cookie.key, algo->digestSize)) == NULL)
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
  Ztls_Update16(negotiatedGroup != NULL ? negotiatedGroup->id : 0);
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

static int certTypeExists(uint8_t *list, size_t count, uint8_t desiredType)
{
  // empty type list means that we default to x509
  if (desiredType == Ztls_CertTypeX509 && count == 0) return 1;
  for (size_t i = 0; i < count; i++) {
    if (list[i] == desiredType) return 1;
  }
  return 0;
}

static int
srvHandleHello(Ztls::Cxn *tls, MessageEmitter *emitter, Bytes message, HandshakeProps *properties)
{
// legacy version
#define Ztls_EmitServerHello(sched, fillRand, extensions, postAction) \
  do { \
    size_t shStartOff; \
    Ztls_PushMessage(emitter, NULL, Ztls_HsTypeSrvHello, { \
      shStartOff = emitter->buf->off - Ztls_HSHeaderSize; \
      Ztls_BufferPush16(emitter->buf, 0x0303); \
      if ((ret = bufferRes(emitter->buf, Ztls_HelloRandomSize, 1)) != 0) goto Exit; \
      do { \
	fillRand \
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
      postAction \
    } while (0); \
    keyScheduleUpdateHash( \
      (sched), emitter->buf->base + shStartOff, emitter->buf->off - shStartOff, 0); \
  } while (0)

#define Ztls_EmitHelloRetryRequest(sched, negotiatedGroup, additionalExtensions, postAction) \
  Ztls_EmitServerHello( \
    (sched), \
    { \
      memcpy( \
	emitter->buf->base + emitter->buf->off, helloRetryRandom, Ztls_HelloRandomSize); \
    }, \
    { \
      const KeyExchangeAlgo *_negotiated_group = (negotiatedGroup); \
      if (_negotiated_group != NULL) { \
	Ztls_BufferPushExtension(emitter->buf, Ztls_ExtTypeKeyShare, { \
	  Ztls_BufferPush16(emitter->buf, _negotiated_group->id); \
	}); \
      } \
      do { \
	additionalExtensions \
      } while (0); \
    }, \
    postAction)
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
  enum { HandshakeFull, HandshakePSK, HandshakePSKDHE } mode;
  size_t	   pskIndex = SIZE_MAX;
  KeyExchangeBytes pubkey, ecdhSecret;
  int acceptEarlyData = 0, isSecondFlight = tls->state == Ztls_::State::SrvWait2ndCliHello, ret;

  bufferInitRx(&ech.chInner, const_cast<char *>(""), 0);

  extensions[0] = {UINT16_MAX};
  *ch = ClientHello{.unknownExtensions = extensions.data()};

  // decode ClientHello
  if ((ret = decodeCliHello(
	 tls->ctx, ch, message.data() + Ztls_HSHeaderSize, message.data() + message.length(),
	 properties, tls)) != 0)
    goto Exit;
  if ((ret = checkCliHelloConstraints(
	 tls->ctx, ch, isSecondFlight ? tls->clientRandom : NULL, 0, message, tls)) != 0)
    goto Exit;
  if (!isSecondFlight) {
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
    if (!isSecondFlight) tls->ech.state = Ztls_::EchState::Offered;
    // obtain AEAD context for opening inner CH
    if (!isSecondFlight && ch->ech.payload.data() != NULL &&
      tls->ctx->ech.server.createOpener != NULL) {
      if ((tls->ech.aead = tls->ctx->ech.server.createOpener->call(
	     &tls->ech.kem, &tls->ech.cipher, tls, ch->ech.configId, ch->ech.cipherSuite,
	     ch->ech.enc, wireBytes(echInfoPrefix, sizeof(echInfoPrefix)))) != NULL)
	tls->ech.configId = ch->ech.configId;
    }
    if (!isSecondFlight) { }
    if (tls->ech.aead != NULL) {
      // now that AEAD context is available, create AAD and decrypt inner CH
      auto tagSize = tls->ech.aead->algo->tagSize;
      // Preserve the recoverable error for the previously impossible allocation size.
      if (ch->ech.payload.length() < tagSize) {
	ret = Ztls_ErrorNoMemory;
	goto Exit;
      }
      auto innerLen = ch->ech.payload.length() - tagSize;
      ech.encodedChInner.length(innerLen ? innerLen : 1);
      ech.chOuterAad.copy(
	message.data() + Ztls_HSHeaderSize, message.length() - Ztls_HSHeaderSize);
      memset(
	ech.chOuterAad.data() + (ch->ech.payload.data() - (message.data() + Ztls_HSHeaderSize)),
	0, ch->ech.payload.length());
      if (aeadDecrypt(
	    tls->ech.aead, ech.encodedChInner.data(), ch->ech.payload.data(),
	    ch->ech.payload.length(), isSecondFlight, ech.chOuterAad.data(),
	    ech.chOuterAad.length()) != SIZE_MAX) {
	tls->ech.state = Ztls_::EchState::Accepted;
	// successfully decrypted EncodedCHInner, build CHInner
	if ((ret = rebuildChInner(
	       &ech.chInner, ech.encodedChInner.data(), ech.encodedChInner.data() + innerLen, ch,
	       message.data() + Ztls_HSHeaderSize + ch->firstExtensionAt,
	       message.data() + message.length())) != 0)
	  goto Exit;
	// treat inner ch as the message being received, re-decode it
	message = wireBytes(ech.chInner.base, ech.chInner.off);
	extensions[0] = {UINT16_MAX};
	*ch = ClientHello{.unknownExtensions = extensions.data()};
	if ((ret = decodeCliHello(
	       tls->ctx, ch, ech.chInner.base + Ztls_HSHeaderSize,
	       ech.chInner.base + ech.chInner.off, properties, tls)) != 0)
	  goto Exit;
	if ((ret = checkCliHelloConstraints(
	       tls->ctx, ch, isSecondFlight ? tls->ech.innerClientRandom : NULL, 1, message,
	       tls)) != 0)
	  goto Exit;
	if (!isSecondFlight)
	  memcpy(tls->ech.innerClientRandom, ch->randomBytes, Ztls_HelloRandomSize);
      } else if (isSecondFlight) {
	// decryption failure of inner CH in 2nd CH is fatal
	ret = Ztls_AlertDecryptError;
	goto Exit;
      } else {
	// decryption failure of 1st CH indicates key mismatch; dispose of AEAD context to
	// indicate adoption of outerCH
	aeadFree(tls->ech.aead);
	tls->ech.aead = NULL;
      }
    }
  } else if (tls->ech.state != Ztls_::EchState::None) {
    assert(isSecondFlight);
    ret = Ztls_AlertIllegalParameter;
    goto Exit;
  }

  if (tls->ctx->requireDheOnPsk) ch->psk.keModes &= ~(1u << Ztls_PSKKeModePSK);

  // handle clientRandom, legacySessionId, SNI, ESNI
  if (!isSecondFlight) {
    if (ch->legacySessionId.length() != 0) tls->sendChangeCipherSpec = 1;
    Bytes serverName = {};
    if (ch->serverName.data() != NULL) serverName = ch->serverName;
    if ((ret = callCliHello(
	   tls, serverName, message, ch->cipherSuites, ch->alpn, ch->signatureAlgorithms,
	   ch->certCompressionAlgos,
	   {ch->serverCertificateTypes.list, ch->serverCertificateTypes.count},
	   {ch->psk.identities.list, ch->psk.identities.count}, 0)) != 0)
      goto Exit;
    if (!certTypeExists(
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
    if ((ret = selectCipher(
	   &cs, tls->ctx->cipherSuites, ch->cipherSuites.data(),
	   ch->cipherSuites.data() + ch->cipherSuites.length(), tls->ctx->serverCipherPreference,
	   tls->ctx->srvCipherChachaPriority, tls->ctx->preSharedKey.hash)) != 0)
      goto Exit;
    if (!isSecondFlight) {
      tls->cipherSuite = cs;
      if ((tls->keySchedule = keyScheduleNew(cs, nullptr, 0)) == NULL) {
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
      if ((ret = selectKeyShare(
	     &keyShare.algorithm, &keyShare.peerKey, tls->ctx->keyExchanges, &src, end, 0)) != 0)
	goto Exit;
    });
  }

  if (!isSecondFlight) {
    if (ch->cookie.all.length() != 0 && keyShare.algorithm != NULL) {
      {
	// use cookie to check the integrity of the handshake, and update the context
	uint8_t sig[Ztls_MaxDigestSize];
	size_t	sigsize = tls->ctx->cipherSuites[0]->hash->digestSize;
	if ((ret = calcCookieSig(tls, properties, keyShare.algorithm, ch->cookie.tbs, sig)) != 0)
	  goto Exit;
	if (!(ch->cookie.signature.length() == sigsize &&
	      Ztls::ctEqual({ch->cookie.signature.data(), sigsize}, {sig, sigsize}))) {
	  ret = Ztls_AlertHSFailure;
	  goto Exit;
	}
      }
      // integrity check passed; update states
      keySchedCh1hashPrefix(tls->keySchedule);
      keyScheduleUpdateHash(
	tls->keySchedule, ch->cookie.ch1Hash.data(), ch->cookie.ch1Hash.length(), 0);
      keyScheduleExtract(tls->keySchedule, tls->ctx->preSharedKey.secret);
      // ... reusing sendbuf to rebuild HRR for hash calculation
      size_t hrrStart = emitter->buf->off;
      Ztls_EmitHelloRetryRequest(
	tls->keySchedule, ch->cookie.sentKeyShare ? keyShare.algorithm : NULL,
	{
	  Ztls_BufferPushExtension(emitter->buf, Ztls_ExtTypeCookie, {
	    Ztls_BufferPushv(emitter->buf, ch->cookie.all.data(), ch->cookie.all.length());
	  });
	},
	{});
      emitter->buf->off = hrrStart;
      isSecondFlight = 1;

    } else if (ch->keyShares.data() != NULL && tls->ctx->keyExchanges != NULL &&
      (keyShare.algorithm == NULL || (properties != NULL && properties->server.enforceRetry))) {
      // send HelloRetryRequest, when trying to negotiate the key share but enforced by config or
      // upon key-share mismatch
      if (ch->negotiatedGroups.data() == NULL) {
	ret = Ztls_AlertMissingExt;
	goto Exit;
      }
      const KeyExchangeAlgo *negotiatedGroup;
      if ((ret = selectNegotiatedGroup(
	     &negotiatedGroup, tls->ctx->keyExchanges, ch->negotiatedGroups.data(),
	     ch->negotiatedGroups.data() + ch->negotiatedGroups.length())) != 0)
	goto Exit;
      keyScheduleUpdateHash(tls->keySchedule, message.data(), message.length(), 0);
      assert(tls->keySchedule->generation == 0);

      // Either send a stateless retry (w. cookies) or a stateful one. When sending the latter,
      // run the state machine. At the moment, stateless retry is disabled when ECH is used (do we
      // need to support it?).
      int retryUsesCookie = properties != NULL && properties->server.retryUsesCookie &&
	!isECHHandshake(tls, NULL, NULL, NULL);
      if (!retryUsesCookie) {
	keySchedPostCh1hash(tls->keySchedule);
	keyScheduleExtract(tls->keySchedule, tls->ctx->preSharedKey.secret);
      }
      size_t echConfirmOff = 0;
      Ztls_EmitHelloRetryRequest(
	tls->keySchedule, keyShare.algorithm != NULL ? NULL : negotiatedGroup,
	{
	  Buffer *sendbuf = emitter->buf;
	  if (isECHHandshake(tls, NULL, NULL, NULL)) {
	    Ztls_BufferPushExtension(sendbuf, Ztls_ExtTypeEncCliHello, {
	      if ((ret = bufferRes(sendbuf, Ztls_ECHConfirmLength, 1)) != 0) goto Exit;
	      memset(sendbuf->base + sendbuf->off, 0, Ztls_ECHConfirmLength);
	      echConfirmOff = sendbuf->off;
	      sendbuf->off += Ztls_ECHConfirmLength;
	    });
	  }
	  if (retryUsesCookie) {
	    Ztls_BufferPushExtension(sendbuf, Ztls_ExtTypeCookie, {
	      Ztls_BufferPushBlock(sendbuf, 2, {
		// push to-be-signed data
		size_t tbsStart = sendbuf->off;
		Ztls_BufferPushBlock(sendbuf, 2, {
		  // first block of the cookie data is the hash(ch1)
		  Ztls_BufferPushBlock(sendbuf, 1, {
		    size_t sz = tls->cipherSuite->hash->digestSize;
		    if ((ret = bufferRes(sendbuf, sz, 1)) != 0) goto Exit;
		    keyScheduleExtractCh1hash(tls->keySchedule, sendbuf->base + sendbuf->off);
		    sendbuf->off += sz;
		  });
		  // second is if we have sent keyShare extension
		  Ztls_BufferPush_1(sendbuf, keyShare.algorithm == NULL);
		  // we can add more data here
		});
		size_t tbsLen = sendbuf->off - tbsStart;
		// push the signature
		Ztls_BufferPushBlock(sendbuf, 1, {
		  size_t sz = tls->ctx->cipherSuites[0]->hash->digestSize;
		  if ((ret = bufferRes(sendbuf, sz, 1)) != 0) goto Exit;
		  if ((ret = calcCookieSig(
			 tls, properties, negotiatedGroup,
			 wireBytes(sendbuf->base + tbsStart, tbsLen),
			 sendbuf->base + sendbuf->off)) != 0)
		    goto Exit;
		  sendbuf->off += sz;
		});
	      });
	    });
	  }
	},
	{
	  if (echConfirmOff != 0 &&
	    (ret = echCalcConfirmation(
	       tls->keySchedule, emitter->buf->base + echConfirmOff, tls->ech.innerClientRandom,
	       Ztls_ECHConfirmationHRR,
	       wireBytes(emitter->buf->base + shStartOff, emitter->buf->off - shStartOff))) != 0)
	    goto Exit;
	});
      if (retryUsesCookie) {
	if ((ret = pushChangeCipherSpec(tls, emitter)) != 0) goto Exit;
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
  if ((ret = reportUnknownExts(tls, properties, ch->unknownExtensions)) != 0) goto Exit;

  // try psk handshake
  if (ch->psk.hashEnd != 0 &&
    (ch->psk.keModes & ((1u << Ztls_PSKKeModePSK) | (1u << Ztls_PSKKeModePSKDHE))) != 0 &&
    !tls->ctx->requireCliAuth &&
    (tls->ctx->encryptTicket != NULL ||
      tls->ctx->preSharedKey.identity.data() != NULL)) {
    if ((ret = tryPSKHandshake(
	   tls, &pskIndex, &acceptEarlyData, ch,
	   wireBytes(message.data(), ch->psk.hashEnd - message.data()), isSecondFlight)) != 0) {
      goto Exit;
    }
  }

  // If the server was setup to use an external PSK but failed to agree, abort the handshake.
  // Because external PSK is a form of mutual authentication, it makes sense to abort (at least as
  // the default).
  if (tls->ctx->preSharedKey.identity.data() != NULL && pskIndex == SIZE_MAX) {
    ret = Ztls_AlertUnknownPSKId;
    goto Exit;
  }

  // If client authentication is enabled, we always force a full handshake. TODO: Check for
  // `post_handshake_auth` extension and if that is present, do not force full handshake! Remove
  // also the check `!requireCliAuth` above.

  // adjust keySchedule, determine handshake mode
  if (pskIndex == SIZE_MAX || tls->ctx->requireCliAuth) {
    keyScheduleUpdateHash(tls->keySchedule, message.data(), message.length(), 0);
    if (!isSecondFlight) {
      assert(tls->keySchedule->generation == 0);
      keyScheduleExtract(tls->keySchedule, wireBytes(NULL, 0));
    }
    mode = HandshakeFull;
    if (properties != NULL) properties->server.selectedPskBinder.len = 0;
  } else {
    keyScheduleUpdateHash(
      tls->keySchedule, ch->psk.hashEnd, message.data() + message.length() - ch->psk.hashEnd, 0);
    if ((ch->psk.keModes & (1u << Ztls_PSKKeModePSK)) != 0) {
      mode = HandshakePSK;
    } else {
      assert((ch->psk.keModes & (1u << Ztls_PSKKeModePSKDHE)) != 0);
      mode = HandshakePSKDHE;
    }
    tls->isPskHandshake = 1;
    if (properties != NULL) {
      Bytes *selected = &ch->psk.identities.list[pskIndex].binder;
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
    uint8_t maxTickets = tls->ctx->ticketRequests.maxCount;
    if (maxTickets == 0) maxTickets = ZtlsMaxTickets;
    if (tls->numTicketsToSend > maxTickets) tls->numTicketsToSend = maxTickets;
  } else {
    tls->numTicketsToSend = 0;
  }

  if (acceptEarlyData && tls->ctx->maxEarlyDataSize != 0 && pskIndex == 0) {
    tls->pendingHandshakeSecret.length(Ztls_MaxDigestSize);
    if ((ret = deriveExporterSecret(tls, 1)) != 0) goto Exit;
    if ((ret = setupTrafficProtection(tls, 0, "c e traffic", 1, 0, 0)) != 0) goto Exit;
  }

  // run key-exchange, to obtain pubkey and secret
  if (mode != HandshakePSK) {
    if (keyShare.algorithm == NULL) {
      ret = ch->keyShares.data() != NULL ? Ztls_AlertHSFailure : Ztls_AlertMissingExt;
      goto Exit;
    }
    if ((ret = keyShare.algorithm->exchange(&pubkey, &ecdhSecret, keyShare.peerKey)) != 0) {
      assert(!pubkey.length());
      assert(!ecdhSecret.length());
      goto Exit;
    }
    tls->keyShare = keyShare.algorithm;
  }

  {
    // send ServerHello
    size_t echConfirmOff = 0;
    Ztls_EmitServerHello(
      tls->keySchedule,
      {
	tls->ctx->random->fill(emitter->buf->base + emitter->buf->off, Ztls_HelloRandomSize);
	// when accepting CHInner, last 8 byte of SH.random is zero for the handshake transcript
	if (isECHHandshake(tls, NULL, NULL, NULL)) {
	  echConfirmOff = emitter->buf->off + Ztls_HelloRandomSize - Ztls_ECHConfirmLength;
	  memset(emitter->buf->base + echConfirmOff, 0, Ztls_ECHConfirmLength);
	}
      },
      {
	Buffer *sendbuf = emitter->buf;
	if (mode != HandshakePSK) {
	  Ztls_BufferPushExtension(sendbuf, Ztls_ExtTypeKeyShare, {
	    Ztls_BufferPush16(sendbuf, keyShare.algorithm->id);
	    Ztls_BufferPushBlock(
	      sendbuf, 2, { Ztls_BufferPushv(sendbuf, pubkey.data(), pubkey.length()); });
	  });
	}
	if (mode != HandshakeFull) {
	  Ztls_BufferPushExtension(sendbuf, Ztls_ExtTypePreSharedKey, {
	    Ztls_BufferPush16(sendbuf, (uint16_t)pskIndex);
	  });
	}
      },
      {
	if (echConfirmOff != 0 &&
	  (ret = echCalcConfirmation(
	     tls->keySchedule, emitter->buf->base + echConfirmOff, tls->ech.innerClientRandom,
	     Ztls_ECHConfirmationSrvHello,
	     wireBytes(emitter->buf->base + shStartOff, emitter->buf->off - shStartOff))) != 0)
	  goto Exit;
      });
  }

  // processing of ECH is complete; dispose state
  clearECH(&tls->ech, 1);

  if ((ret = pushChangeCipherSpec(tls, emitter)) != 0) goto Exit;

  // create protection contexts for the handshake
  assert(tls->keySchedule->generation == 1);
  keyScheduleExtract(tls->keySchedule, wireBytes(ecdhSecret.data(), ecdhSecret.length()));
  if ((ret = setupTrafficProtection(tls, 1, "s hs traffic", 2, 0, 0)) != 0) goto Exit;
  if (tls->pendingHandshakeSecret.data() != NULL) {
    if ((ret = deriveSecret(
	   tls->keySchedule, tls->pendingHandshakeSecret.data(), "c hs traffic")) != 0)
      goto Exit;
    if (tls->ctx->updateTrafficKey != NULL &&
      (ret = tls->ctx->updateTrafficKey->call(tls, 0, 2, tls->pendingHandshakeSecret.data())) !=
	0)
      goto Exit;
  } else {
    if ((ret = setupTrafficProtection(tls, 0, "c hs traffic", 2, 0, 0)) != 0) goto Exit;
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
      if ((ret = pushAdditionalExts(properties, sendbuf)) != 0) goto Exit;
    });
  });

  if (mode == HandshakeFull) {
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
	    if ((ret = pushSigAlgos(tls->ctx->verifyCertificate, sendbuf)) != 0) goto Exit;
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
    if ((ret = sendCert(
	   tls, emitter, &ch->signatureAlgorithms, wireBytes(NULL, 0), ch->statusRequest,
	   ch->certCompressionAlgos)) != 0)
      goto Exit;
    // send certificateverify, finished, and complete the handshake
    if ((ret = srvFinishHandshake(tls, emitter, 1, &ch->signatureAlgorithms)) != 0) goto Exit;
  } else {
    // send finished, and complete the handshake
    if ((ret = srvFinishHandshake(tls, emitter, 0, NULL)) != 0) goto Exit;
  }

Exit:
  bufferDispose(&ech.chInner);
  return ret;

#undef Ztls_EmitServerHello
#undef Ztls_EmitHelloRetryRequest
}

static int srvFinishHandshake(
  Ztls::Cxn *tls, MessageEmitter *emitter, int needCertVerify,
  SignatureAlgorithms *signatureAlgorithms)
{
  int ret;

  if (needCertVerify) {
    if ((ret = sendCertVerify(tls, emitter, signatureAlgorithms, Ztls_SrvCertVerifyCtx)) != 0) {
      if (ret == Ztls_ErrorAsyncOperation) { tls->state = Ztls_::State::SrvBuildCertSig; }
      goto Exit;
    }
  }

  if ((ret = sendFinished(tls, emitter)) != 0) goto Exit;

  assert(tls->keySchedule->generation == 2);
  if ((ret = keyScheduleExtract(tls->keySchedule, wireBytes(NULL, 0))) != 0) goto Exit;
  if ((ret = setupTrafficProtection(tls, 1, "s ap traffic", 3, 0, 0)) != 0) goto Exit;
  if ((ret = deriveSecret(tls->keySchedule, tls->pendingTrafficSecret, "c ap traffic")) != 0)
    goto Exit;
  if ((ret = deriveExporterSecret(tls, 0)) != 0) goto Exit;

  if (tls->pendingHandshakeSecret.data() != NULL) {
    if (tls->ctx->omitEndOfEarlyData) {
      if ((ret = commissionHandshakeSecret(tls)) != 0) goto Exit;
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
      if ((ret = sendSessionTicket(tls, emitter)) != 0) goto Exit;
  }

  if (tls->ctx->requireCliAuth) {
    ret = Ztls_ErrorInProgress;
  } else {
    ret = 0;
  }

Exit:
  return ret;
}

static int srvHandleEndOfEarlyData(Ztls::Cxn *tls, Bytes message)
{
  int ret;

  if ((ret = commissionHandshakeSecret(tls)) != 0) goto Exit;

  keyScheduleUpdateHash(tls->keySchedule, message.data(), message.length(), 0);
  tls->state = Ztls_::State::SrvWaitFinished;
  ret = Ztls_ErrorInProgress;

Exit:
  return ret;
}

static int srvHandleFinished(Ztls::Cxn *tls, Bytes message)
{
  int ret;

  if ((ret = verifyFinished(tls, message)) != 0) return ret;

  memcpy(tls->rxTraffic.secret, tls->pendingTrafficSecret, sizeof(tls->pendingTrafficSecret));
  ZuClear(tls->pendingTrafficSecret, sizeof(tls->pendingTrafficSecret));
  if ((ret = setupTrafficProtection(tls, 0, NULL, 3, 0, 0)) != 0) return ret;

  keyScheduleUpdateHash(tls->keySchedule, message.data(), message.length(), 0);

  tls->state = Ztls_::State::SrvPostHS;
  return 0;
}

static int updateTrafficKey(Ztls::Cxn *tls, int isEnc)
{
  TrafficProtection *tp = isEnc ? &tls->txTraffic : &tls->rxTraffic;
  uint8_t	     secret[Ztls_MaxDigestSize];
  int		     ret;

  const HashAlgo *hash = tls->cipherSuite->hash;
  if ((ret = hkdfExpandLabel(
	 hash, secret, hash->digestSize, wireBytes(tp->secret, hash->digestSize), "traffic upd",
	 wireBytes(NULL, 0), NULL)) != 0)
    goto Exit;
  memcpy(tp->secret, secret, sizeof(secret));
  ret = setupTrafficProtection(tls, isEnc, NULL, 3, 0, 1);

Exit:
  ZuClear(secret, sizeof(secret));
  return ret;
}

static int handleKeyUpdate(Ztls::Cxn *tls, MessageEmitter *emitter, Bytes message)
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

static int parseRecordHeader(Record *rec, const uint8_t *src)
{
  rec->type = src[0];
  rec->version = loadBe<uint16_t>(src + 1);
  rec->length = loadBe<uint16_t>(src + 3);

  if (rec->length > (size_t)(rec->type == Ztls_ContentTypeAppdata ? Ztls_MaxEncRecordSize :
								    Ztls_MaxPlainRecordSize))
    return Ztls_AlertDecodeError;

  return 0;
}

static int parseRecord(Ztls::Cxn *tls, Record *rec, const uint8_t *src, size_t *len)
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
    if ((ret = parseRecordHeader(rec, src)) != 0) return ret;
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
    bufferInitRx(&tls->recvbuf.rec, const_cast<char *>(""), 0);
    if ((ret = bufferRes(&tls->recvbuf.rec, 5, 0)) != 0) return ret;
  }

  // fill and parse the header
  while (tls->recvbuf.rec.off < 5) {
    if (src == end) return Ztls_ErrorInProgress;
    tls->recvbuf.rec.base[tls->recvbuf.rec.off++] = *src++;
  }
  if ((ret = parseRecordHeader(rec, tls->recvbuf.rec.base)) != 0) return ret;

  // fill the fragment
  size_t addlen = rec->length + 5 - tls->recvbuf.rec.off;
  if (addlen != 0) {
    if ((ret = bufferRes(&tls->recvbuf.rec, addlen, 0)) != 0) return ret;
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

static void streamDiscard(Ztls::Cxn *tls)
{
  StreamRecord *r = &tls->streamRec;
  if (r->begun && tls->rxTraffic.aead != NULL && tls->rxTraffic.aead->serialDecrypt())
    tls->rxTraffic.aead->decAbort();
  tls->plaintext.discard();
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
    legacySessionId = wireBytes(legacySessionIdBuf, sizeof(legacySessionIdBuf));
    ctx->random->fill(legacySessionId.data(), legacySessionId.length());
  }
}

void Cxn::retireRx()
{
  using namespace Ztls_;
  if (!rxActive) return;
  streamDiscard(this);
  plaintext.clear();
  rxInput = nullptr;
  rxPaused = false;
  bufferDispose(&recvbuf.rec);
  bufferDispose(&recvbuf.mess);
  if (keySchedule) {
    keyScheduleFree(keySchedule);
    keySchedule = nullptr;
  }
  if (rxTraffic.aead) aeadFree(rxTraffic.aead);
  ZuClear(&rxTraffic, sizeof(rxTraffic));
  clearECH(&ech, isServer);
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
  if (txTraffic.aead) Ztls_::aeadFree(txTraffic.aead);
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

Context *context(Ztls::Cxn *tls) { return tls->ctx; }

void context(Ztls::Cxn *tls, Context *ctx) { tls->ctx = ctx; }

Bytes cliRandom(Ztls::Cxn *tls) { return wireBytes(tls->clientRandom, Ztls_HelloRandomSize); }

const CipherSuite *cipher(Ztls::Cxn *tls) { return tls->cipherSuite; }

uint16_t protocolVersion(Ztls::Cxn *tls)
{
  (void)tls;
  return Ztls_ProtoVerTls13;
}

int trafficKeys(Ztls::Cxn *tls, int isEnc, uint8_t *key, uint8_t *iv, uint64_t *seq)
{
  TrafficProtection *ctx = isEnc ? &tls->txTraffic : &tls->rxTraffic;
  int		     ret;

  if (isEnc && tls->txHandedOff) return Ztls_ErrorNotAvailable;

  if ((ret = trafficKeys(
	 tls->cipherSuite->aead, tls->cipherSuite->hash, key, iv, ctx->secret, wireBytes(NULL, 0),
	 NULL)) != 0)
    return ret;
  *seq = ctx->seq;
  return 0;
}

const char *serverName(Ztls::Cxn *tls) { return tls->serverName.data(); }

int serverName(Ztls::Cxn *tls, ZuCSpan serverName)
{
  assignName(tls->serverName, serverName);
  return 0;
}

const char *negotiatedProtocol(Ztls::Cxn *tls) { return tls->negotiatedProtocol.data(); }

int negotiatedProtocol(Ztls::Cxn *tls, ZuCSpan protocol)
{
  assignName(tls->negotiatedProtocol, protocol);
  return 0;
}

int handshakeComplete(Ztls::Cxn *tls) { return tls->state >= Ztls_::State::PostHSMin; }

int isPskHandshake(Ztls::Cxn *tls) { return tls->isPskHandshake; }

int isECHHandshake(
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

void **dataPtr(Ztls::Cxn *tls) { return &tls->dataPtr; }

static int handleCliHsMessage(
  Ztls::Cxn *tls, MessageEmitter *emitter, Bytes message, int isEndOfRecord,
  HandshakeProps *properties)
{
  uint8_t type = message.data()[0];
  int	  ret;

  switch (tls->state) {
  case Ztls_::State::CliWaitSrvHello:
  case Ztls_::State::CliWait2ndSrvHello:
    if (type == Ztls_HsTypeSrvHello && isEndOfRecord) {
      ret = cliHandleHello(tls, emitter, message, properties);
    } else {
      ret = Ztls_AlertUnwaitedMsg;
    }
    break;
  case Ztls_::State::CliWaitEncExts:
    if (type == Ztls_HsTypeEncExts) {
      ret = cliHandleEncExts(tls, message, properties);
    } else {
      ret = Ztls_AlertUnwaitedMsg;
    }
    break;
  case Ztls_::State::CliWaitCertReq:
    if (type == Ztls_HsTypeCertRequest) {
      ret = cliHandleCertReq(tls, message, properties);
      break;
    }
  // fall through
  case Ztls_::State::CliExpectCert:
    switch (type) {
    case Ztls_HsTypeCert:
      ret = cliHandleCert(tls, message);
      break;
    case Ztls_HsTypeCompressedCert:
      ret = cliHandleCompressedCert(tls, message);
      break;
    default:
      ret = Ztls_AlertUnwaitedMsg;
      break;
    }
    break;
  case Ztls_::State::CliWaitCertSig:
    if (type == Ztls_HsTypeCertVerify) {
      ret = cliHandleCertVerify(tls, message);
    } else {
      ret = Ztls_AlertUnwaitedMsg;
    }
    break;
  case Ztls_::State::CliWaitFinished:
    if (type == Ztls_HSTypeFinished && isEndOfRecord) {
      ret = cliHandleFinished(tls, emitter, message);
    } else {
      ret = Ztls_AlertUnwaitedMsg;
    }
    break;
  case Ztls_::State::CliPostHS:
    switch (type) {
    case Ztls_HsTypeNewTicket:
      ret = cliHandleNewTicket(tls, message);
      break;
    case Ztls_HsTypeKeyUpdate:
      ret = handleKeyUpdate(tls, emitter, message);
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

static int handleSrvHsMessage(
  Ztls::Cxn *tls, MessageEmitter *emitter, Bytes message, int isEndOfRecord,
  HandshakeProps *properties)
{
  uint8_t type = message.data()[0];
  int	  ret;

  switch (tls->state) {
  case Ztls_::State::SrvWaitCliHello:
  case Ztls_::State::SrvWait2ndCliHello:
    if (type == Ztls_HsTypeCliHello && isEndOfRecord) {
      ret = srvHandleHello(tls, emitter, message, properties);
    } else {
      ret = Ztls_AlertHSFailure;
    }
    break;
  case Ztls_::State::SrvExpectCert:
    if (type == Ztls_HsTypeCert) {
      ret = srvHandleCert(tls, message);
    } else {
      ret = Ztls_AlertUnwaitedMsg;
    }
    break;
  case Ztls_::State::SrvWaitCertSig:
    if (type == Ztls_HsTypeCertVerify) {
      ret = srvHandleCertVerify(tls, message);
    } else {
      ret = Ztls_AlertUnwaitedMsg;
    }
    break;
  case Ztls_::State::SrvWaitEarlyEnd:
    assert(!tls->ctx->omitEndOfEarlyData);
    if (type == Ztls_HsTypeEndOfEarly) {
      ret = srvHandleEndOfEarlyData(tls, message);
    } else {
      ret = Ztls_AlertUnwaitedMsg;
    }
    break;
  case Ztls_::State::SrvWaitFinished:
    if (type == Ztls_HSTypeFinished && isEndOfRecord) {
      ret = srvHandleFinished(tls, message);
    } else {
      ret = Ztls_AlertHSFailure;
    }
    break;
  case Ztls_::State::SrvPostHS:
    switch (type) {
    case Ztls_HsTypeKeyUpdate:
      ret = handleKeyUpdate(tls, emitter, message);
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

static int handleAlert(Ztls::Cxn *tls, const uint8_t *src, size_t len)
{
  if (len != 2) return Ztls_AlertDecodeError;

  uint8_t desc = src[1];

  // all fatal alerts and USER_CANCELLED warning tears down the connection immediately, regardless
  // of the transmitted level
  return Ztls_AlertToPeerError(desc);
}

static int hsBufOverflow(Context *ctx, size_t size)
{
  if (ctx->maxBufferSize == 0) return 0;
  if (size <= ctx->maxBufferSize) return 0;
  return 1;
}

static int handleHandshakeMessages(
  Ztls::Cxn *tls, MessageEmitter *emitter, ZuBSpan input, HandshakeProps *properties,
  bool *keyUpdateRejected = nullptr)
{
  auto dispatch = tls->isServer ? handleSrvHsMessage : handleCliHsMessage;

  // flatten the unhandled messages
  const uint8_t *src, *srcEnd;
  if (tls->recvbuf.mess.base == NULL) {
    src = input.data();
    srcEnd = src + input.length();
  } else {
    if (hsBufOverflow(tls->ctx, tls->recvbuf.mess.off + input.length()))
      return Ztls_AlertHSFailure;
    if (int ret = bufferRes(&tls->recvbuf.mess, input.length(), 0)) return ret;
    memcpy(tls->recvbuf.mess.base + tls->recvbuf.mess.off, input.data(), input.length());
    tls->recvbuf.mess.off += input.length();
    src = tls->recvbuf.mess.base;
    srcEnd = src + tls->recvbuf.mess.off;
  }

  // handle the messages
  int ret = Ztls_ErrorInProgress;
  while (srcEnd - src >= 4) {
    size_t messLen = 4 + loadBe24(src + 1);
    if (srcEnd - src < (int)messLen) break;
    if (keyUpdateRejected && *src == Ztls_HsTypeKeyUpdate) {
      *keyUpdateRejected = true;
      ret = Ztls_AlertUnwaitedMsg;
    } else {
      ret = dispatch(tls, emitter, wireBytes(src, messLen), srcEnd - src == messLen, properties);
    }
    switch (ret) {
    case 0:
    case Ztls_ErrorAsyncOperation:
    case Ztls_ErrorInProgress:
      break;
    default:
      tls->clearVerifier();
      bufferDispose(&tls->recvbuf.mess);
      return ret;
    }
    src += messLen;
  }

  // keep last partial message in buffer
  if (src != srcEnd) {
    size_t newSize = srcEnd - src;
    if (hsBufOverflow(tls->ctx, newSize)) return Ztls_AlertHSFailure;
    if (tls->recvbuf.mess.base == NULL) {
      bufferInitRx(&tls->recvbuf.mess, const_cast<char *>(""), 0);
      if ((ret = bufferRes(&tls->recvbuf.mess, newSize, 0)) != 0) return ret;
      memcpy(tls->recvbuf.mess.base, src, newSize);
    } else {
      memmove(tls->recvbuf.mess.base, src, newSize);
    }
    tls->recvbuf.mess.off = newSize;
    ret = Ztls_ErrorInProgress;
  } else {
    bufferDispose(&tls->recvbuf.mess);
  }

  return ret;
}

static int dispatchRecord(
  Ztls::Cxn *tls, MessageEmitter *emitter, Record *record, HandshakeProps *properties,
  int *application)
{
  Record rec = *record;
  int	 ret;
  *application = 0;
  if (tls->recvbuf.mess.base != NULL || rec.type == Ztls_ContentTypeHS) {
    if (rec.type != Ztls_ContentTypeHS) return Ztls_AlertDecodeError;
    ret = handleHandshakeMessages(tls, emitter, {rec.fragment, rec.length}, properties);
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
      ret = handleAlert(tls, rec.fragment, rec.length);
      break;
    default:
      ret = Ztls_AlertUnwaitedMsg;
      break;
    }
  }

  return ret;
}

static int handleInput(
  Ztls::Cxn *tls, MessageEmitter *emitter, Buffer *decryptbuf, const void *input, size_t *inlen,
  HandshakeProps *properties)
{
  Record rec;
  int	 ret;

  // extract the record
  if ((ret = parseRecord(tls, &rec, static_cast<const uint8_t *>(input), inlen)) != 0) return ret;
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
    size_t decryptedLen;
    if (rec.type != Ztls_ContentTypeAppdata) return Ztls_AlertHSFailure;
    if ((ret = bufferRes(decryptbuf, rec.length, 0)) != 0) return ret;
    decryptedLen = aeadDecrypt(
      tls->rxTraffic.aead, decryptbuf->base + decryptbuf->off, rec.fragment, rec.length,
      tls->rxTraffic.seq, rec.fragment - 5, 5);
    if (decryptedLen == SIZE_MAX) {
      ret = Ztls_AlertBadRecordMac;
      if (tls->isServer && tls->earlyDataSkippedBytes != UINT32_MAX) goto ServerSkipEarlyData;
      return ret;
    }
    ++tls->rxTraffic.seq;
    rec.length = decryptedLen;
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
  ret = dispatchRecord(tls, emitter, &rec, properties, &application);
  if (application) decryptbuf->off += rec.length;

NextRecord:
  bufferDispose(&tls->recvbuf.rec);
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

size_t rawReadEpoch(const Ztls::Cxn *tls)
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

RawHandshakeResult rawHandshake(
  Ztls::Cxn *tls, HandshakeProps *properties, RawHandshakeOp::T op, size_t inputEpoch,
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
      result.status = sendCliHello(tls, &emitter, properties, NULL);
    break;
  case RawHandshakeOp::Input:
    if (tls->state == Ztls_::State::CliHSStart || tls->state == Ztls_::State::SrvBuildCertSig ||
      rawReadEpoch(tls) != inputEpoch)
      break;
    result.status = input.length() ?
      handleHandshakeMessages(tls, &emitter, input, properties, &result.keyUpdateRejected) :
      Ztls_ErrorInProgress;
    break;
  case RawHandshakeOp::ResumeSignature:
    if (!input.length() && tls->state == Ztls_::State::SrvBuildCertSig)
      result.status = srvFinishHandshake(tls, &emitter, 1, NULL);
    break;
  }

  switch (result.status) {
  case 0:
  case Ztls_ErrorInProgress:
  case Ztls_ErrorStatelessRetry:
  case Ztls_ErrorAsyncOperation:
    result.length = result.offsets[4];
    return result;
  default:
    tls->clearVerifier();
    return {.status = result.status, .keyUpdateRejected = result.keyUpdateRejected};
  }
}

static void
initRecordMessageEmitter(Ztls::Cxn *tls, RecordMessageEmitter *emitter, Buffer *sendbuf)
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
  if ((ret = bufferRequireDirection(_sendbuf, 1)) != 0) return ret;
  initRecordMessageEmitter(tls, &emitter, _sendbuf);
  size_t sendbufOrigOff = emitter.buf->off;

  // special handlings
  switch (tls->state) {
  case Ztls_::State::CliHSStart: {
    assert(input == NULL || *inlen == 0);
    return sendCliHello(tls, &emitter, properties, NULL);
  }
  case Ztls_::State::SrvBuildCertSig:
    return srvFinishHandshake(tls, &emitter, 1, NULL);
  default:
    break;
  }

  const uint8_t *src = static_cast<const uint8_t *>(input), *const srcEnd = src + *inlen;
  Buffer decryptbuf;

  bufferInitRx(&decryptbuf, const_cast<char *>(""), 0);

  // perform handhake until completion or until all the input has been swallowed
  ret = Ztls_ErrorInProgress;
  while (ret == Ztls_ErrorInProgress && src != srcEnd) {
    size_t consumed = srcEnd - src;
    ret = handleInput(tls, &emitter, &decryptbuf, src, &consumed, properties);
    src += consumed;
    assert(decryptbuf.off == 0);
  }

  bufferDispose(&decryptbuf);

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
      ZuClear(emitter.buf->base + sendbufOrigOff, emitter.buf->off - sendbufOrigOff);
      emitter.buf->off = sendbufOrigOff;
    }
    // send alert immediately
    if (Ztls_ErrorGetClass(ret) != Ztls_ErrorClassPeerAlert)
      if (sendAlert(
	    tls, emitter.buf, Ztls_AlertLevelFatal,
	    Ztls_ErrorGetClass(ret) == Ztls_ErrorClassSelfAlert ? ret :
								  Ztls_AlertInternalError) != 0)
	emitter.buf->off = sendbufOrigOff;
    break;
  }

  *inlen -= srcEnd - src;
  return ret;
}

int receive(Ztls::Cxn *tls, Buffer *decryptbuf, const void *_input, size_t *inlen)
{
  const uint8_t *input = (const uint8_t *)_input, *const end = input + *inlen;
  size_t decryptbufOrigSize = decryptbuf->off;
  int	 ret;

  assert(tls->state >= Ztls_::State::SrvWaitEarlyEnd);
  if ((ret = bufferRequireDirection(decryptbuf, 0)) != 0) return ret;
  ret = 0;

  // loop until we decrypt some application data (or an error)
  while (ret == 0 && input != end && decryptbufOrigSize == decryptbuf->off) {
    size_t consumed = end - input;
    ret = handleInput(tls, NULL, decryptbuf, input, &consumed, NULL);
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

static int updateSendKey(Ztls::Cxn *tls, Buffer *_sendbuf, int reqUpdate)
{
  RecordMessageEmitter emitter;
  int		       ret;

  initRecordMessageEmitter(tls, &emitter, _sendbuf);
  size_t sendbufOrigOff = emitter.buf->off;

  Ztls_PushMessage(
    &emitter, NULL, Ztls_HsTypeKeyUpdate, { Ztls_BufferPush_1(emitter.buf, !!reqUpdate); });
  if ((ret = updateTrafficKey(tls, 1)) != 0) goto Exit;
  ret = 0;

Exit:
  if (ret != 0) emitter.buf->off = sendbufOrigOff;
  return ret;
}

int handoffTx(Ztls::Cxn *tls)
{
  if (!tls || tls->txHandedOff || tls->ctx->updateTrafficKey ||
    (tls->state != Ztls_::State::CliPostHS && tls->state != Ztls_::State::SrvPostHS) ||
    !tls->txTraffic.aead)
    return Ztls_ErrorNotAvailable;
  tls->txHandedOff = true;
  return 0;
}

static int chunksCheck(Ztls::Cxn *tls, Ztls::RecordSink *sink)
{
  if (tls == NULL) return Ztls_ErrorNotAvailable;
  if (tls->txTraffic.failed || !sink) return Ztls_ErrorLibrary;
  if (!recordSerialAvailable(&tls->txTraffic)) return Ztls_ErrorNotAvailable;
  return tls->txTraffic.aead || tls->ctx->cipherSuites.serialTx() ? 0 :
    Ztls_ErrorNotAvailable;
}

int rxStreamCheck(Ztls::Cxn *tls)
{
  if (tls == NULL) return Ztls_ErrorNotAvailable;
  if (auto aead = tls->rxTraffic.aead)
    return aead->serialDecrypt() && aead->algo->tagSize <= 16 ? 0 : Ztls_ErrorNotAvailable;
  return tls->ctx->cipherSuites.serialRx() ? 0 : Ztls_ErrorNotAvailable;
}

// Stash the bounded record header or authentication tag across input chunks.
static size_t
stashRecordHdr(uint8_t *dest, size_t *off, size_t size, const uint8_t **src, size_t *available)
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

int rxStream(
  Ztls::Cxn *tls, Ztls::RecordSink *output, const void *input, size_t *length,
  HandshakeProps *properties)
{
  Record	 rec;
  AeadContext	*aead;
  int		 ret, emitFatal = 0;
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
  if ((ret = rxStreamCheck(tls)) != 0) goto Fail;
  // Rx no longer inspects the writable Tx context after handing it off.
  if (!tls->txHandedOff && (ret = chunksCheck(tls, output)) != 0) goto Fail;
  if (tls->state == Ztls_::State::CliHSStart || tls->state == Ztls_::State::SrvBuildCertSig) {
    size_t empty = 0;
    ret = handshakeChunks(tls, output, NULL, &empty, properties);
    if (ret == 0 || ret == Ztls_ErrorInProgress || ret == Ztls_ErrorAsyncOperation) return ret;
    goto Fail;
    // handshakeChunks already applied its alert policy.
  }
  emitFatal = 1;
  aead = tls->rxTraffic.aead;
  if (r->headerOff != 5) {
    stashRecordHdr(r->header, &r->headerOff, 5, &src, &available);
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
    if ((ret = parseRecordHeader(&rec, r->header)) != 0) goto Fail;
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
    ret = tls->plaintext.reserve(r->textLength, &base);
    if (ret != 0) goto Fail;
    r->base = static_cast<uint8_t *>(base);
  }
  if (r->encrypted && !r->begun) {
    r->begun = 1;
    ret = aead->decBegin(tls->rxTraffic.seq, r->header, sizeof(r->header));
    if (ret != 0) goto Fail;
  }
  if (r->written != r->textLength) {
    size_t n = r->textLength - r->written;
    if (n > available) n = available;
    if (n != 0) {
      size_t written = n;
      if (r->encrypted) {
	ret = aead->decChunk(r->base + r->written, r->textLength - r->written, src, n, &written);
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
    stashRecordHdr(r->tag, &r->tagOff, r->tagSize, &src, &available);
    if (r->tagOff != r->tagSize) goto Incomplete;
  }
  rec = (Record){r->header[0], loadBe<uint16_t>(r->header + 1), r->textLength, r->base};
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
    int application = rec.type == Ztls_ContentTypeAppdata &&
      tls->state >= Ztls_::State::PostHSMin && tls->recvbuf.mess.base == NULL;
    if (!application) {
      Buffer formatting;
      RecordMessageEmitter emitter;
      bufferInitTx(&formatting, const_cast<char *>(""), 0);
      formatting.recordSink = output;
      initRecordMessageEmitter(tls, &emitter, &formatting);
      ret = dispatchRecord(tls, &emitter, &rec, properties, &application);
      bufferDispose(&formatting);
      if (ret != 0 && ret != Ztls_ErrorInProgress && ret != Ztls_ErrorAsyncOperation) goto Fail;
    }
    if (application && (ret = tls->plaintext.commit(rec.length)) != 0) goto Fail;
  }
  streamDiscard(tls);
  if (length != NULL) *length = original - available;
  return ret == Ztls_ErrorInProgress ? 0 : ret;

SkipEarly:
  tls->earlyDataSkippedBytes += (uint32_t)r->length;
  if (tls->earlyDataSkippedBytes > ZtlsMaxEarlySkipSize) {
    ret = Ztls_AlertHSFailure;
    goto Fail;
  }
  streamDiscard(tls);
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
  streamDiscard(tls);
  tls->clearVerifier();
  tls->streamRec.failed = 1;
  // Match handshake()'s fatal-alert behavior without publishing provisional input.
  // Post-handshake outbound keys may belong to a detached Tx owner.
  if (emitFatal && !tls->txHandedOff && tls->state < Ztls_::State::PostHSMin &&
    Ztls_ErrorGetClass(ret) == Ztls_ErrorClassSelfAlert && output) {
    Buffer formatting;
    bufferInitTx(&formatting, const_cast<char *>(""), 0);
    formatting.recordSink = output;
    (void)sendAlert(tls, &formatting, Ztls_AlertLevelFatal, (uint8_t)ret);
    bufferDispose(&formatting);
  }
  if (length != NULL) *length = original - available;
  return ret;
}

int txSendChunks(Ztls::Cxn *tls, Ztls::RecordSink *sink, const void *input, size_t length)
{
  int ret;
  if (!tls || !tls->txHandedOff) return Ztls_ErrorNotAvailable;
  if (length > Ztls_MaxPlainRecordSize) return Ztls_ErrorLibrary;
  TrafficProtection *enc = &tls->txTraffic;
  if ((ret = chunksCheck(tls, sink)) != 0) return ret;
  int	 update = tls->needsKeyUpdate || enc->seq >= 16777216;
  size_t size =
    (length != 0 ? recordWireSize(enc, length) : 0) + (update ? recordWireSize(enc, 5) : 0);
  if (size == 0) return 0;
  RecordWriter<false, Ztls::RecordSink> writer{sink, size};
  if ((ret = sink->reserve(size)) != 0) {
    sink->discard();
    return ret;
  }
  if (update) {
    const uint8_t message[5] = {Ztls_HsTypeKeyUpdate, 0, 0, 1, !!tls->keyUpdateSendRequest};
    if ((ret = recordEmit(&writer, enc, Ztls_ContentTypeHS, message, sizeof(message))) != 0 ||
      (ret = updateTrafficKey(tls, 1)) != 0)
      goto Fail;
    tls->needsKeyUpdate = tls->keyUpdateSendRequest = 0;
  }
  if (
    length != 0 && (ret = recordEmit(&writer, enc, Ztls_ContentTypeAppdata, input, length)) != 0)
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

int txKeyChunks(Ztls::Cxn *tls, Ztls::RecordSink *sink, int reqUpdate)
{
  int ret;
  if (!tls || !tls->txHandedOff) return Ztls_ErrorNotAvailable;
  if ((ret = chunksCheck(tls, sink)) != 0) return ret;
  const uint8_t message[5] = {Ztls_HsTypeKeyUpdate, 0, 0, 1, !!reqUpdate};
  if ((ret = recordTransaction(
	 sink, &tls->txTraffic, Ztls_ContentTypeHS, message, sizeof(message))) != 0)
    return ret;
  ret = updateTrafficKey(tls, 1);
  if (ret != 0) tls->txTraffic.failed = 1;
  return ret;
}

int txAlertChunks(Ztls::Cxn *tls, Ztls::RecordSink *sink, uint8_t level, uint8_t description)
{
  int ret;
  if (!tls || !tls->txHandedOff) return Ztls_ErrorNotAvailable;
  if ((ret = chunksCheck(tls, sink)) != 0) return ret;
  const uint8_t message[2] = {level, description};
  return recordTransaction(
    sink, &tls->txTraffic, Ztls_ContentTypeAlert, message, sizeof(message));
}

int handshakeChunks(
  Ztls::Cxn *tls, Ztls::RecordSink *sink, const void *input, size_t *length,
  HandshakeProps *properties)
{
  int ret;
  if ((ret = chunksCheck(tls, sink)) != 0) return ret;
  Buffer formatting;
  bufferInitTx(&formatting, const_cast<char *>(""), 0);
  formatting.recordSink = sink;
  ret = handshake(tls, &formatting, input, length, properties);
  if (ret != 0 && ret != Ztls_ErrorInProgress && ret != Ztls_ErrorAsyncOperation)
    tls->txTraffic.failed = 1;
  bufferDispose(&formatting);
  return ret;
}

size_t txRecordOverhead(Ztls::Cxn *tls)
{ return tls ? 6 + tls->txTraffic.aead->algo->tagSize : 0; }

int takeKeyUpdateReq(Ztls::Cxn *tls)
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
  if ((ret = bufferRequireDirection(sendbuf, 1)) != 0) return ret;

  // "For AES-GCM, up to 2^24.5 full-size records (about 24 million) may be encrypted on a given
  // connection while keeping a safety margin of approximately 2^-57 for Authenticated Encryption
  // (AE) security." (RFC 8446 section 5.5).

  // Key updates do not happen with tls 1.2, check `keySchedule` to see if we are using tls/1.3
  if (tls->txTraffic.seq >= 16777216 && tls->keySchedule != NULL) tls->needsKeyUpdate = 1;

  if (tls->needsKeyUpdate) {
    int ret;
    if ((ret = updateSendKey(tls, sendbuf, tls->keyUpdateSendRequest)) != 0) return ret;
    tls->needsKeyUpdate = 0;
    tls->keyUpdateSendRequest = 0;
  }

  return bufferPushEncRecords(
    sendbuf, Ztls_ContentTypeAppdata, static_cast<const uint8_t *>(input), inlen,
    &tls->txTraffic);
}

int updateKey(Ztls::Cxn *tls, int reqUpdate)
{
  if (tls->txHandedOff) return Ztls_ErrorNotAvailable;
  if (tls->txTraffic.failed) return Ztls_ErrorLibrary;
  assert(tls->ctx->updateTrafficKey == NULL);
  tls->needsKeyUpdate = 1;
  tls->keyUpdateSendRequest = reqUpdate;
  return 0;
}

size_t recordOverhead(Ztls::Cxn *tls)
{
  if (tls->txHandedOff) return 0;
  const AeadAlgo *algo = tls->txTraffic.aead->algo;

  return 6 + algo->tagSize;
}

int sendAlert(Ztls::Cxn *tls, Buffer *sendbuf, uint8_t level, uint8_t description)
{
  int ret;
  if (tls->txHandedOff) return Ztls_ErrorNotAvailable;
  if (tls->txTraffic.failed) return Ztls_ErrorLibrary;
  if ((ret = bufferRequireDirection(sendbuf, 1)) != 0) return ret;
  const uint8_t	    message[2] = {level, description};
  TrafficProtection clear = {}, *enc = &tls->txTraffic;
  if (enc->aead == NULL || tls->state <= Ztls_::State::CliWaitFinished) enc = &clear;
  return bufferRecord(sendbuf, enc, Ztls_ContentTypeAlert, message, sizeof(message));
}

int exportSecret(
  Ztls::Cxn *tls, void *output, size_t outlen, const char *label, Bytes ctxValue, int isEarly)
{
  const HashAlgo *algo = tls->keySchedule->hashes[0].algo;
  auto &exporter = isEarly ? tls->exporterMasterSecret.early : tls->exporterMasterSecret.oneRtt;
  uint8_t *masterSecret = exporter.data(), derivedSecret[Ztls_MaxDigestSize],
	  ctxValueHash[Ztls_MaxDigestSize];
  int ret;

  if (masterSecret == NULL) {
    if (isEarly) {
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

  if ((ret = calcHash(algo, ctxValueHash, ctxValue.data(), ctxValue.length())) != 0) return ret;

  if ((ret = hkdfExpandLabel(
	 algo, derivedSecret, algo->digestSize, wireBytes(masterSecret, algo->digestSize), label,
	 wireBytes(algo->emptyDigest, algo->digestSize), NULL)) != 0)
    goto Exit;
  ret = hkdfExpandLabel(
    algo, output, outlen, wireBytes(derivedSecret, algo->digestSize), "exporter",
    wireBytes(ctxValueHash, algo->digestSize), NULL);

Exit:
  ZuClear(derivedSecret, sizeof(derivedSecret));
  ZuClear(ctxValueHash, sizeof(ctxValueHash));
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

int calcHash(const HashAlgo *algo, void *output, const void *src, size_t len)
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

HashContext *hmacCreate(const HashAlgo *algo, const void *key, size_t keySize)
{
  assert(keySize <= algo->blockSize);

  HashContext *hash = algo->create();
  if (!hash) return NULL;
  return new HmacContext{algo, hash, key, keySize};
}

int hkdfExtract(const HashAlgo *algo, void *output, Bytes salt, Bytes ikm)
{
  HashContext *hash;

  if (salt.length() == 0) salt = wireBytes(zeroesOfMaxDigestSize, algo->digestSize);

  if ((hash = hmacCreate(algo, salt.data(), salt.length())) == NULL) return Ztls_ErrorNoMemory;
  hash->update(ikm.data(), ikm.length());
  {
    hash->final(output);
    delete hash;
  }
  return 0;
}

int hkdfExpand(const HashAlgo *algo, void *output, size_t outlen, Bytes prk, Bytes info)
{
  HashContext *hmac = NULL;
  size_t       i;
  uint8_t      digest[Ztls_MaxDigestSize];

  for (i = 0; (i * algo->digestSize) < outlen; ++i) {
    if (hmac == NULL) {
      if ((hmac = hmacCreate(algo, prk.data(), prk.length())) == NULL) return Ztls_ErrorNoMemory;
    } else {
      hmac->update(digest, algo->digestSize);
    }
    hmac->update(info.data(), info.length());
    uint8_t gen = (uint8_t)(i + 1);
    hmac->update(&gen, 1);
    hmac->final(digest, HashFinalMode::Reset);

    size_t offStart = i * algo->digestSize, offEnd = offStart + algo->digestSize;
    if (offEnd > outlen) offEnd = outlen;
    memcpy((uint8_t *)output + offStart, digest, offEnd - offStart);
  }

  if (hmac != NULL) delete hmac;

  ZuClear(digest, algo->digestSize);

  return 0;
}

int hkdfExpandLabel(
  const HashAlgo *algo, void *output, size_t outlen, Bytes secret, const char *label,
  Bytes hashValue, const char *labelPrefix)
{
  Buffer  hkdfLabel;
  uint8_t hkdfLabelBuf[80];
  int	  ret;

  bufferInitTx(&hkdfLabel, hkdfLabelBuf, sizeof(hkdfLabelBuf));

  Ztls_BufferPush16(&hkdfLabel, (uint16_t)outlen);
  Ztls_BufferPushBlock(&hkdfLabel, 1, {
    if (labelPrefix == NULL) labelPrefix = Ztls_HKDFLabelPrefix;
    Ztls_BufferPushv(&hkdfLabel, labelPrefix, strlen(labelPrefix));
    Ztls_BufferPushv(&hkdfLabel, label, strlen(label));
  });
  Ztls_BufferPushBlock(
    &hkdfLabel, 1, { Ztls_BufferPushv(&hkdfLabel, hashValue.data(), hashValue.length()); });

  ret = hkdfExpand(algo, output, outlen, secret, wireBytes(hkdfLabel.base, hkdfLabel.off));

Exit:
  bufferDispose(&hkdfLabel);
  return ret;
}

CipherContext *cipherNew(const CipherAlgo *algo, int isEnc, const void *key)
{ return algo->create(isEnc, key); }

void cipherFree(CipherContext *ctx) { delete ctx; }

AeadContext *aeadNew(
  const AeadAlgo *aead, const HashAlgo *hash, int isEnc, const void *secret,
  const char *labelPrefix)
{
  AeadContext *ctx = NULL;
  struct KeyIV {
    uint8_t	key[Ztls_MaxSecretSize];
    uint8_t	iv[Ztls_MaxIVSize];
  } keyIV;
  int ret;

  if ((ret = trafficKeys(aead, hash, keyIV.key, keyIV.iv, secret, {}, labelPrefix)) != 0)
    goto Exit;
  ctx = aeadNew_(aead, isEnc, keyIV.key, keyIV.iv);
Exit:
  ZuClear(&keyIV, sizeof(keyIV));
  return ctx;
}

AeadContext *aeadNew_(const AeadAlgo *aead, int isEnc, const void *key, const void *iv)
{ return aead->create(isEnc, key, iv); }

void aeadFree(AeadContext *ctx) { delete ctx; }

void aeadXorIV(AeadContext *ctx, const void *_bytes, size_t len)
{
  const uint8_t *bytes = static_cast<const uint8_t *>(_bytes);
  uint8_t	 iv[Ztls_MaxIVSize];

  aeadGetIV(ctx, iv);
  for (size_t i = 0; i < len; ++i) iv[i] ^= bytes[i];
  aeadSetIV(ctx, iv);
}

void aeadBuildIV(const AeadAlgo *algo, uint8_t *iv, const uint8_t *staticIv, uint64_t seq)
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
int serverNameIsIP(const char *name)
{
  ZiIP address;
  return ZiIP::parse(address, name);
}

int echEncodeConfig(
  Buffer *buf, uint8_t configId, const HpkeKem *kem, Bytes publicKey, const HpkeSuites &ciphers,
  uint8_t maxNameLength, const char *publicName)
{
  int ret;
  if ((ret = bufferRequireDirection(buf, 1)) != 0) return ret;

  Ztls_BufferPush16(buf, Ztls_ECHConfigVer);
  Ztls_BufferPushBlock(buf, 2, {
    Ztls_BufferPush_1(buf, configId);
    Ztls_BufferPush16(buf, kem->id);
    Ztls_BufferPushBlock(
      buf, 2, { Ztls_BufferPushv(buf, publicKey.data(), publicKey.length()); });
    Ztls_BufferPushBlock(buf, 2, {
      for (auto cipher : ciphers) {
	Ztls_BufferPush16(buf, cipher->id.kdf);
	Ztls_BufferPush16(buf, cipher->id.aead);
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
