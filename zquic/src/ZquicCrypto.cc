//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/Zquic.hh>

#include "ZquicOpenSSL.hh"

#include <zlib/ZuDerive.hh>

#include <zlib/ZmScratch.hh>

#include <zlib/ZtPlatform.hh>
#include <zlib/ZtString.hh>

#include <zlib/ZiFile.hh>

#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <zlib/ZtlsEngineOpenSSL.hh>

namespace Zquic {

using namespace ZquicLog_;

static constexpr ZuBArray<20> InitialSaltV1_ = {
  0x38, 0x76, 0x2c, 0xf7, 0xf5, 0x59, 0x34, 0xb3, 0x4d, 0x17,
  0x9a, 0xe6, 0xa4, 0xc8, 0x0c, 0xad, 0xcc, 0xbb, 0x7f, 0x0a
};

static bool hmacSHA256(
  const uint8_t *key, unsigned keyLen, const uint8_t *data, unsigned dataLen,
  uint8_t *out, unsigned outLen)
{
  unsigned n = 0;
  if (outLen < InitialSecret::SecretLen) return false;
  return HMAC(EVP_sha256(), key, int(keyLen), data, dataLen, out, &n) &&
    n == InitialSecret::SecretLen;
}

static bool hkdfExtract(
  uint8_t *out, const uint8_t *salt, unsigned saltLen, ZuBSpan ikm)
{
  return hmacSHA256(
    salt, saltLen, ikm.data(), ikm.length(), out, InitialSecret::SecretLen);
}

static char hexDigit(unsigned v)
{
  return v < 10 ? char('0' + v) : char('a' + v - 10);
}

ZuDerive(KeyLogString, (ZtString<ZtStringHeapID<"Zquic.KeyLog">>));

static void hexAppend(KeyLogString &s, ZuBSpan data)
{
  for (unsigned i = 0, n = data.length(); i < n; ++i) {
    unsigned c = data[i];
    s << hexDigit(c >> 4) << hexDigit(c & 0x0f);
  }
}

static bool hkdfExpand(
  uint8_t *out, unsigned outLen, const uint8_t *secret, unsigned secretLen,
  const uint8_t *info, unsigned infoLen)
{
  ZuBArray<InitialSecret::SecretLen>
    t(InitialSecret::SecretLen, false);
  ZuBArray<InitialSecret::SecretLen + 256 + 1>
    msg(InitialSecret::SecretLen + 256 + 1, false);
  unsigned tLen = 0, off = 0;
  for (uint8_t block = 1; off < outLen; ++block) {
    if (tLen + infoLen + 1 > msg.length()) return false;
    memcpy(msg.data(), t.data(), tLen);
    memcpy(msg.data() + tLen, info, infoLen);
    msg[tLen + infoLen] = block;
    if (!hmacSHA256(
	  secret, secretLen, msg.data(), tLen + infoLen + 1,
	  t.data(), t.length()))
      return false;
    unsigned n = outLen - off;
    if (n > t.length()) n = t.length();
    memcpy(out + off, t.data(), n);
    off += n;
    tLen = t.length();
  }
  return true;
}

static bool hkdfExpandLabel(
  uint8_t *out, unsigned outLen, const uint8_t *secret, unsigned secretLen,
  ZuCSpan label)
{
  static constexpr auto Prefix = "tls13 "_z;
  ZuBArray<64> info(64, false);
  unsigned o = 0;
  unsigned fullLen = Prefix.length() + label.length();
  if (fullLen > 255 || outLen > 0xffff) return false;
  info[o++] = uint8_t(outLen >> 8);
  info[o++] = uint8_t(outLen);
  info[o++] = uint8_t(fullLen);
  memcpy(info.data() + o, Prefix.data(), Prefix.length());
  o += Prefix.length();
  memcpy(info.data() + o, label.data(), label.length());
  o += label.length();
  info[o++] = 0;
  return hkdfExpand(out, outLen, secret, secretLen, info.data(), o);
}

static bool deriveInitialSecret(InitialSecret &out, const uint8_t *initial,
    ZuCSpan label)
{
  return hkdfExpandLabel(
      out.secret.data(), out.secret.length(),
      initial, InitialSecret::SecretLen, label) &&
    hkdfExpandLabel(
      out.key.data(), out.key.length(),
      out.secret.data(), out.secret.length(), "quic key") &&
    hkdfExpandLabel(
      out.iv.data(), out.iv.length(),
      out.secret.data(), out.secret.length(), "quic iv") &&
    hkdfExpandLabel(
      out.hp.data(), out.hp.length(),
      out.secret.data(), out.secret.length(), "quic hp");
}

static void makeNonce(uint8_t *out, const InitialSecret &secret, uint64_t pn)
{
  memcpy(out, secret.iv.data(), InitialSecret::IVLen);
  for (unsigned i = 0; i < 8; ++i)
    out[InitialSecret::IVLen - 1 - i] ^= uint8_t(pn >> (i * 8));
}

static bool isDir(ZuCSpan path)
{
  if (!path) return false;
  struct stat st;
  return !stat(path.data(), &st) && S_ISDIR(st.st_mode);
}

void CryptoStream::reset()
{
  m_txOffset = 0;
  m_rxOffset = 0;
  Rx::rxReset(0);
  m_delivery.length(0);
  m_txRanges.length(0); m_txBytes = 0;
}

unsigned CryptoStream::txRangeIndex(uint64_t offset) const
{
  unsigned lo = 0, hi = m_txRanges.length();
  while (lo < hi) {
    unsigned mid = lo + ((hi - lo)>>1);
    if (m_txRanges[mid].offset <= offset) lo = mid + 1;
    else hi = mid;
  }
  return lo ? lo - 1 : unsigned(-1);
}

bool CryptoStream::matches_(uint64_t offset, ZuBSpan payload) const
{
  uint64_t end = offset + payload.length();
  if (end < offset || end > m_txBytes) return false;
  unsigned index = txRangeIndex(offset), used = 0;
  while (used < payload.length()) {
    if (index >= m_txRanges.length()) return false;
    const auto &range = m_txRanges[index++];
    uint64_t position = offset + used;
    if (position < range.offset || position - range.offset >= range.length) return false;
    unsigned begin = unsigned(position - range.offset);
    unsigned n = range.length - begin;
    if (n > payload.length() - used) n = payload.length() - used;
    if (memcmp(range.data->data_() + range.begin + begin, payload.data() + used, n))
      return false;
    used += n;
  }
  return true;
}

bool CryptoStream::sent(uint64_t offset, ZuBSpan payload)
{
  uint64_t end = offset + payload.length();
  if (end < offset || end > MaxBufSize) return false;
  if (offset < m_txBytes) return matches_(offset, payload);
  if (offset != m_txBytes) return false;
  if (!payload.length()) return true;
  // Public borrowed ingress acquires final retained plaintext storage once.
  if (ZuUnlikely(!m_txAllocator)) return false;
  auto buf = m_txAllocator->allocBuf();
  if (!buf->append(payload)) return false;
  return sent(offset, ZuMv(buf));
}

bool CryptoStream::sent(uint64_t offset, ZmRef<ZiIOBuf> payload)
{
  if (!payload) return false;
  unsigned begin = payload->skip, length = payload->length;
  return sent(offset, ZuMv(payload), begin, length);
}

bool CryptoStream::sent(
  uint64_t offset, ZmRef<ZiIOBuf> payload, unsigned begin, unsigned length)
{
  if (!payload || payload->failed() || begin > payload->size ||
      length > payload->size - begin) return false;
  uint64_t end = offset + length;
  if (end < offset || end > MaxBufSize) return false;
  if (offset < m_txBytes)
    return matches_(offset, byteSpan(payload->data_() + begin, length));
  if (offset != m_txBytes) return false;
  if (!length) return true;
  m_txRanges.push(TxRange{ZuMv(payload), offset, begin, length});
  m_txBytes = end;
  return true;
}

bool CryptoStream::txPayload(
  uint64_t offset, uint64_t length, ZuBSpan &payload) const
{
  payload = {};
  uint64_t end = offset + length;
  if (end < offset || end > m_txBytes) return false;
  if (!length) return true;
  unsigned index = txRangeIndex(offset);
  if (index >= m_txRanges.length()) return false;
  const auto &range = m_txRanges[index];
  unsigned begin = unsigned(offset - range.offset);
  if (begin >= range.length) return false;
  unsigned n = range.length - begin;
  if (length < n) n = unsigned(length);
  payload = byteSpan(range.data->data_() + range.begin + begin, n);
  return true;
}

int CryptoStream::writeFrame(
  uint8_t *out, unsigned len, ZuBSpan payload, CryptoDiag *diag)
{
  int n = FrameCodec::writeCryptoPrefix(out, len, m_txOffset, payload.length());
  if (n < 0 || len - unsigned(n) < payload.length()) return -1;
  if (!sent(m_txOffset, payload)) return -1;
  memcpy(out + n, payload.data(), payload.length());
  m_txOffset += payload.length();
  if (diag) {
    diag->cryptoBytesTx += payload.length();
  }
  return n + int(payload.length());
}

int CryptoStream::writeFramePrefix(
  uint8_t *out, unsigned len, unsigned payloadLen, CryptoDiag *diag)
{
  int n = FrameCodec::writeCryptoPrefix(out, len, m_txOffset, payloadLen);
  if (n < 0) return -1;
  m_txOffset += payloadLen;
  if (diag) {
    diag->cryptoBytesTx += payloadLen;
  }
  return int(n);
}

int CryptoStream::receiveFrame(
  const Frame &frame, ZuBSpan &contiguous, CryptoDiag *diag)
{
  if (frame.type != FrameType::Crypto) return -1;
  return receive(frame.offset, frame.payload, contiguous, diag);
}

int CryptoStream::receive(
  uint64_t offset, ZuBSpan payload, ZuBSpan &contiguous, CryptoDiag *diag)
{
  contiguous = {};
  m_delivery.length(0);
  uint64_t end = offset + payload.length();
  if (end < offset || end > MaxBufSize) return -1;
  if (!payload || end <= m_rxOffset) return 0;
  uint64_t first = offset < m_rxOffset ? m_rxOffset : offset;
  if (first == m_rxOffset && !m_rxQueue.count_()) {
    unsigned payloadOffset = unsigned(first - offset);
    unsigned bytes = unsigned(end - first);
    contiguous = ZuBSpan{payload.data() + payloadOffset, bytes};
    m_rxOffset = end;
    m_rxQueue.head(end);
    if (diag) diag->cryptoBytesRx += bytes;
    return 0;
  }

  auto spans = ZmScratch(
    RxSpan, m_rxQueue.count_() + 1, RxSpans::VHeap);
  if (!rxNovelSpans(m_rxQueue, first, end, spans)) return -1;
  uint64_t bytes = rxSpanBytes(spans);
  if (ZuUnlikely(!m_rxAllocator)) return -1;
  if (!queueRxSpans(spans, offset, payload,
	[this](unsigned) -> ZmRef<CryptoRxPQueue::Node> {
	  return m_rxAllocator->allocBuf();
	},
	[this](ZmRef<CryptoRxPQueue::Node> buf, uint64_t offset, unsigned) {
	  buf->offset = offset;
	  Rx::rcvd(ZuMv(buf));
	}))
    return -1;

  if (m_deliveryFn) resumeReadyDequeue();
  if (diag) diag->cryptoBytesRx += bytes;
  if (!m_deliveryFn && m_delivery)
    contiguous = m_delivery.cspan();
  return 0;
}

void CryptoStream::resumeReadyDequeue()
{
  if (!m_rxQueue.count_()) return;
  if (!m_rxQueue.has(m_rxQueue.head())) return;
  Rx::resumeDequeue();
}

void CryptoStream::appendDelivery(const uint8_t *p, uint64_t length)
{
  if (!p || !length) return;
  uint64_t n = m_delivery.length();
  ZiAssert(n + length <= MaxBufSize, "Zquic", (),
	    "CRYPTO delivery overflow", return);
  m_delivery.append(p, length);
}

void CryptoStream::process(Msg *node)
{
  if (!node) return;
  CryptoRxData &data = node->data();
  if (!data.length) return;
  const uint8_t *p = data.data();
  if (m_deliveryFn)
    m_deliveryFn(ZuBSpan{p, data.length});
  else
    appendDelivery(p, data.length);
  m_rxOffset += data.length;
}

bool InitialCrypto::derive(InitialKeyMaterial &out, const CxnID &dcid)
{
  out = {};
  if (!hkdfExtract(
	out.initial.data(), InitialSaltV1_.data(), InitialSaltV1_.length(), dcid))
    return false;
  return deriveInitialSecret(out.client, out.initial.data(), "client in") &&
    deriveInitialSecret(out.server, out.initial.data(), "server in");
}

int InitialCrypto::encrypt(
  uint8_t *out, unsigned len, const InitialSecret &secret, uint64_t pn,
  ZuBSpan aad, ZuBSpan plaintext)
{
  Ztls_::Bytes plain =
    Ztls_::wireBytes(plaintext.data(), plaintext.length());
  return encryptV_(out, len, secret, pn, aad, &plain, 1, plaintext.length());
}

static bool plainLength(
  const Ztls_::Bytes *plain, unsigned plainCount, unsigned &length)
{
  if (!plain && plainCount) return false;
  uint64_t plainLen = 0;
  for (unsigned i = 0; i < plainCount; ++i) {
    if (plain[i].length() > UINT_MAX || plainLen > UINT_MAX - plain[i].length())
      return false;
    plainLen += plain[i].length();
  }
  length = unsigned(plainLen);
  return true;
}

int InitialCrypto::encryptV(
  uint8_t *out, unsigned len, const InitialSecret &secret, uint64_t pn,
  ZuBSpan aad, const Ztls_::Bytes *plain, unsigned plainCount)
{
  unsigned plainLen;
  if (!plainLength(plain, plainCount, plainLen)) return -1;
  return encryptV_(out, len, secret, pn, aad, plain, plainCount, plainLen);
}

int InitialCrypto::encryptV_(
  uint8_t *out, unsigned len, const InitialSecret &secret, uint64_t pn,
  ZuBSpan aad, const Ztls_::Bytes *plain, unsigned plainCount, unsigned plainLen)
{
  if (len < uint64_t(plainLen) + InitialSecret::TagLen) return -1;
  ZuBArray<InitialSecret::IVLen> nonce(InitialSecret::IVLen, false);
  makeNonce(nonce.data(), secret, pn);

  EVP_CIPHER_CTX *ctx = opensslCipherCtx();
  if (!ctx) return -1;
  int n = 0, off = 0;
  bool ok =
    EVP_EncryptInit_ex(ctx, EVP_aes_128_gcm(), nullptr, nullptr, nullptr) == 1 &&
    EVP_CIPHER_CTX_ctrl(
      ctx, EVP_CTRL_GCM_SET_IVLEN, nonce.length(), nullptr) == 1 &&
    EVP_EncryptInit_ex(
      ctx, nullptr, nullptr, secret.key.data(), nonce.data()) == 1 &&
    (!aad || EVP_EncryptUpdate(ctx, nullptr, &n,
      aad.data(), aad.length()) == 1);
  for (unsigned i = 0; ok && i < plainCount; ++i) {
    if (!plain[i].length()) continue;
    ok = EVP_EncryptUpdate(
      ctx, out + off, &n, plain[i].data(), plain[i].length()) == 1;
    if (ok) off += n;
  }
  ok = ok && EVP_EncryptFinal_ex(ctx, out + off, &n) == 1;
  if (ok) off += n;
  ok = ok && EVP_CIPHER_CTX_ctrl(
    ctx, EVP_CTRL_GCM_GET_TAG, InitialSecret::TagLen, out + off) == 1;
  return ok ? off + int(InitialSecret::TagLen) : -1;
}

int InitialCrypto::decrypt(
  uint8_t *out, unsigned len, const InitialSecret &secret, uint64_t pn,
  ZuBSpan aad, ZuBSpan ciphertext)
{
  if (ciphertext.length() < InitialSecret::TagLen) return -1;
  unsigned plainLen = ciphertext.length() - InitialSecret::TagLen;
  if (len < plainLen) return -1;
  ZuBArray<InitialSecret::IVLen> nonce(InitialSecret::IVLen, false);
  makeNonce(nonce.data(), secret, pn);

  EVP_CIPHER_CTX *ctx = opensslCipherCtx();
  if (!ctx) return -1;
  int n = 0, off = 0;
  bool ok =
    EVP_DecryptInit_ex(ctx, EVP_aes_128_gcm(), nullptr, nullptr, nullptr) == 1 &&
    EVP_CIPHER_CTX_ctrl(
      ctx, EVP_CTRL_GCM_SET_IVLEN, nonce.length(), nullptr) == 1 &&
    EVP_DecryptInit_ex(
      ctx, nullptr, nullptr, secret.key.data(), nonce.data()) == 1 &&
    (!aad || EVP_DecryptUpdate(ctx, nullptr, &n,
      aad.data(), aad.length()) == 1) &&
    (!plainLen || EVP_DecryptUpdate(ctx, out, &n,
      ciphertext.data(), plainLen) == 1);
  if (ok) off = n;
  ok = ok &&
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, InitialSecret::TagLen,
      const_cast<uint8_t *>(ciphertext.data()) + plainLen) == 1 &&
    EVP_DecryptFinal_ex(ctx, out + off, &n) == 1;
  if (ok) off += n;
  return ok ? off : -1;
}

bool InitialCrypto::headerMask(
  uint8_t *out, unsigned len, const InitialSecret &secret, ZuBSpan sample)
{
  if (len < InitialSecret::HPMaskLen || sample.length() < 16) return false;
  ZuBArray<32> block(32, false);
  EVP_CIPHER_CTX *ctx = opensslCipherCtx();
  if (!ctx) return false;
  int n = 0, total = 0;
  bool ok =
    EVP_EncryptInit_ex(
      ctx, EVP_aes_128_ecb(), nullptr, secret.hp.data(), nullptr) == 1 &&
    EVP_CIPHER_CTX_set_padding(ctx, 0) == 1 &&
    EVP_EncryptUpdate(ctx, block.data(), &n,
      sample.data(), 16) == 1;
  if (ok) total = n;
  ok = ok && EVP_EncryptFinal_ex(ctx, block.data() + total, &n) == 1;
  if (ok) memcpy(out, block.data(), InitialSecret::HPMaskLen);
  return ok;
}

int InitialPktProt::protectLong(
  uint8_t *out, unsigned len, const InitialSecret &secret, uint64_t pn,
  ZuBSpan header, ZuBSpan plaintext, unsigned pnOffset, unsigned pnLength)
{
  Ztls_::Bytes plain =
    Ztls_::wireBytes(plaintext.data(), plaintext.length());
  return protectLongV_(out, len, secret, pn, header, &plain, 1,
    plaintext.length(), pnOffset, pnLength);
}

int InitialPktProt::protectLongV(
  uint8_t *out, unsigned len, const InitialSecret &secret, uint64_t pn,
  ZuBSpan header, const Ztls_::Bytes *plain, unsigned plainCount,
  unsigned pnOffset, unsigned pnLength)
{
  unsigned plainLen;
  if (!plainLength(plain, plainCount, plainLen)) return -1;
  return protectLongV_(out, len, secret, pn, header, plain, plainCount,
    plainLen, pnOffset, pnLength);
}

int InitialPktProt::protectLongV_(
  uint8_t *out, unsigned len, const InitialSecret &secret, uint64_t pn,
  ZuBSpan header, const Ztls_::Bytes *plain, unsigned plainCount,
  unsigned plainLen, unsigned pnOffset, unsigned pnLength)
{
  if (pnLength < 1 || pnLength > 4 ||
      header.length() != pnOffset + pnLength)
    return -1;
  uint64_t packetLen = uint64_t(header.length()) + plainLen +
    InitialSecret::TagLen;
  if (len < packetLen || packetLen < pnOffset + 4 + 16) return -1;
  if (out != header.data())
    memcpy(out, header.data(), header.length());
  int n = InitialCrypto::encryptV_(
    out + header.length(), len - header.length(), secret, pn,
    ZuBSpan{out, header.length()},
    plain, plainCount, plainLen);
  if (n != int(plainLen + InitialSecret::TagLen)) return -1;

  ZuBArray<InitialSecret::HPMaskLen>
    mask(InitialSecret::HPMaskLen, false);
  if (!InitialCrypto::headerMask(
	mask.data(), mask.length(), secret,
	ZuBSpan{out + pnOffset + 4, 16}))
    return -1;
  out[0] ^= mask[0] & 0x0f;
  for (unsigned i = 0; i < pnLength; ++i)
    out[pnOffset + i] ^= mask[1 + i];
  return int(packetLen);
}

int InitialPktProt::unprotectLong(
  uint8_t *packet, unsigned len, const InitialSecret &secret,
  uint64_t largestPN, unsigned pnOffset, uint64_t &pn, unsigned &payloadOffset)
{
  if (pnOffset + 4 + 16 > len) return -1;
  ZuBArray<InitialSecret::HPMaskLen>
    mask(InitialSecret::HPMaskLen, false);
  if (!InitialCrypto::headerMask(
	mask.data(), mask.length(), secret,
	ZuBSpan{packet + pnOffset + 4, 16}))
    return -1;
  packet[0] ^= mask[0] & 0x0f;
  unsigned pnLength = (packet[0] & 0x03) + 1;
  if (pnLength < 1 || pnLength > 4 || pnOffset + pnLength > len)
    return -1;
  uint64_t truncated = 0;
  for (unsigned i = 0; i < pnLength; ++i) {
    packet[pnOffset + i] ^= mask[1 + i];
    truncated = (truncated << 8) | packet[pnOffset + i];
  }
  pn = PktNumber::decode(largestPN, truncated, pnLength * 8);
  payloadOffset = pnOffset + pnLength;
  int n = InitialCrypto::decrypt(
    packet + payloadOffset, len - payloadOffset, secret, pn,
    ZuBSpan{packet, payloadOffset},
    ZuBSpan{packet + payloadOffset, len - payloadOffset});
  return n;
}

void TrafficSecret::clear()
{
  secret.clear();
  key.clear();
  iv.clear();
  hp.clear();
  tagLen = 0;
  aead = nullptr;
  hash = nullptr;
  hpCipher = nullptr;
  installed = false;
}

bool PktProtState::init(
  const TrafficSecret &secret_, PktNumSpace::T level_, bool tx_)
{
  clear();
  if (!secret_.valid() || !secret_.aead || !secret_.hpCipher)
    return false;
  aead = Ztls_::aeadNew_(
    secret_.aead, tx_, secret_.key.data(), secret_.iv.data());
  if (!aead) return false;
  hp = Ztls_::cipherNew(secret_.hpCipher, true, secret_.hp.data());
  if (!hp) {
    clear();
    return false;
  }
  secret = secret_;
  level = level_;
  tx = tx_;
  installed = true;
  return true;
}

void PktProtState::clear()
{
  if (aead) Ztls_::aeadFree(aead);
  if (hp) Ztls_::cipherFree(hp);
  aead = nullptr;
  hp = nullptr;
  secret.clear();
  level = PktNumSpace::Initial;
  tx = false;
  installed = false;
}

static bool hkdfExpandTrafficLabel(
  const Ztls_::HashAlgo *hash, uint8_t *out, unsigned outLen,
  ZuBSpan secret, const char *label)
{
  return hash && out && outLen &&
    !Ztls_::hkdfExpandLabel(
      hash, out, outLen,
      Ztls_::wireBytes(secret.data(), secret.length()),
      label, Ztls_::wireBytes(nullptr, 0), "tls13 ");
}

static const Ztls_::CipherAlgo *trafficHPCipher(const Ztls_::CipherSuite *suite)
{
  if (!suite || !suite->aead) return nullptr;
  // RFC 9001 sections 5.4.3/5.4.4 select AES-ECB or ChaCha20, respectively.
  return suite->id == Ztls_SuiteChaChaSHA256 ?
    suite->aead->ctrCipher : suite->aead->ecbCipher;
}

bool PktProt::deriveSecret(
  TrafficSecret &out, const Ztls_::CipherSuite *cipher, ZuBSpan secret)
{
  out.clear();
  const auto hpCipher = trafficHPCipher(cipher);
  if (!hpCipher || !cipher->hash || !secret) return false;

  unsigned secretLen = cipher->hash->digestSize;
  unsigned keyLen = cipher->aead->keySize;
  unsigned ivLen = cipher->aead->ivSize;
  if (ivLen < 8) ivLen = 8;
  unsigned hpLen = hpCipher->keySize;
  unsigned tagLen = cipher->aead->tagSize;
  if (secret.length() != secretLen ||
      secretLen > TrafficSecret::MaxSecretLen ||
      keyLen > TrafficSecret::MaxKeyLen ||
      ivLen > TrafficSecret::MaxIVLen ||
      hpLen > TrafficSecret::MaxHPLen ||
      !tagLen)
    return false;

  if (!out.secret.length(secretLen) ||
      !out.key.length(keyLen) ||
      !out.iv.length(ivLen) ||
      !out.hp.length(hpLen)) {
    out.clear();
    return false;
  }

  if (!hkdfExpandTrafficLabel(cipher->hash, out.key.data(), keyLen,
	secret, "quic key") ||
      !hkdfExpandTrafficLabel(cipher->hash, out.iv.data(), ivLen,
	secret, "quic iv") ||
      !hkdfExpandTrafficLabel(cipher->hash, out.hp.data(), hpLen,
	secret, "quic hp"))
    {
      out.clear();
      return false;
    }

  memcpy(out.secret.data(), secret.data(), secret.length());
  out.tagLen = tagLen;
  out.aead = cipher->aead;
  out.hash = cipher->hash;
  out.hpCipher = hpCipher;
  out.installed = true;
  return true;
}

bool PktProt::deriveNextSecret(
  TrafficSecret &out, const TrafficSecret &current)
{
  out.clear();
  if (!current.valid() || !current.hash || !current.aead ||
      !current.hpCipher || !current.secret.length() ||
      current.secret.length() > TrafficSecret::MaxSecretLen)
    return false;

  if (!out.secret.length(current.secret.length()) ||
      !out.key.length(current.key.length()) ||
      !out.iv.length(current.iv.length()) ||
      !out.hp.length(current.hp.length()))
    return false;
  out.tagLen = current.tagLen;
  out.aead = current.aead;
  out.hash = current.hash;
  out.hpCipher = current.hpCipher;

  if (!hkdfExpandTrafficLabel(
	current.hash, out.secret.data(), out.secret.length(),
	current.secret.bspan(), "quic ku")) {
    out.clear();
    return false;
  }
  if (!hkdfExpandTrafficLabel(
	current.hash, out.key.data(), out.key.length(),
	out.secret.bspan(), "quic key") ||
      !hkdfExpandTrafficLabel(
	current.hash, out.iv.data(), out.iv.length(),
	out.secret.bspan(), "quic iv")) {
    out.clear();
    return false;
  }
  memcpy(out.hp.data(), current.hp.data(), out.hp.length());
  out.installed = true;
  return true;
}

bool Crypto::txSecret(
  PktNumSpace::T level, const TrafficSecret &secret)
{
  if (level < 0 || level >= PktNumSpace::N || !secret.valid()) return false;
  m_txTrafficSecrets[level] = secret;
  auto keyLevel = level == PktNumSpace::Initial ? PktKeyLevel::Initial :
    level == PktNumSpace::Handshake ? PktKeyLevel::Handshake : PktKeyLevel::OneRTT;
  ++m_txKeyGen[keyLevel];
  return true;
}

bool Crypto::rxSecret(
  PktNumSpace::T level, const TrafficSecret &secret)
{
  if (level < 0 || level >= PktNumSpace::N || !secret.valid()) return false;
  m_rxTrafficSecrets[level] = secret;
  return m_rxProt[level].init(m_rxTrafficSecrets[level], level, false);
}

bool Crypto::txKeySecret(
  PktKeyLevel::T level, const TrafficSecret &secret)
{
  PktNumSpace::T space = spaceFromKeyLevel(level);
  if (space == PktNumSpace::N || !secret.valid()) return false;
  if (level != PktKeyLevel::ZeroRTT)
    return txSecret(space, secret);
  m_txEarlySecret = secret;
  m_earlyDataEnabled = true;
  if (m_earlyDataState == EarlyDataState::Disabled ||
      m_earlyDataState == EarlyDataState::Enabled)
    m_earlyDataState = EarlyDataState::Offered;
  ++m_txKeyGen[PktKeyLevel::ZeroRTT];
  return true;
}

bool Crypto::rxKeySecret(
  PktKeyLevel::T level, const TrafficSecret &secret)
{
  PktNumSpace::T space = spaceFromKeyLevel(level);
  if (space == PktNumSpace::N || !secret.valid()) return false;
  if (level != PktKeyLevel::ZeroRTT)
    return rxSecret(space, secret);
  m_rxEarlySecret = secret;
  m_earlyDataEnabled = true;
  if (m_isServer)
    m_earlyDataState = EarlyDataState::Accepted;
  else if (m_earlyDataState == EarlyDataState::Disabled ||
      m_earlyDataState == EarlyDataState::Enabled)
    m_earlyDataState = EarlyDataState::Offered;
  return m_rxEarlyProt.init(m_rxEarlySecret, space, false);
}

static bool trafficMask(
  uint8_t *mask, unsigned len, PktProtState &state, ZuBSpan sample)
{
  const TrafficSecret &secret = state.secret;
  if (len < InitialSecret::HPMaskLen ||
      !state.valid() || !secret.valid() || !state.hp ||
      !secret.hpCipher || sample.length() < 16)
    return false;
  Ztls_::CipherContext *ctx = state.hp;
  ZuBArray<32> block(32, false);
  if (secret.hpCipher->ivSize) {
    // ChaCha20 uses the sample as its counter/nonce and encrypts zeroes.
    Ztls_::cipherInit(ctx, sample.data());
    memset(block.data(), 0, InitialSecret::HPMaskLen);
    Ztls_::cipherEncrypt(ctx, block.data(), block.data(), InitialSecret::HPMaskLen);
  } else {
    Ztls_::cipherInit(ctx, nullptr);
    Ztls_::cipherEncrypt(ctx, block.data(), sample.data(), 16);
  }
  memcpy(mask, block.data(), InitialSecret::HPMaskLen);
  return true;
}

static int protect_(
  uint8_t *out, unsigned len, PktProtState &state, uint64_t pn,
  ZuBSpan header, const Ztls_::Bytes *plain, unsigned plainCount,
  unsigned plainLen, unsigned pnOffset, unsigned pnLength,
  uint8_t firstMask)
{
  const TrafficSecret &secret = state.secret;
  if (!state.valid() || !secret.valid() || !state.aead ||
      !state.hp || !secret.aead || !secret.hpCipher ||
      (!plain && plainCount) ||
      pnLength < 1 || pnLength > 4 ||
      header.length() != pnOffset + pnLength)
    return -1;
  uint64_t packetLen = uint64_t(header.length()) + plainLen + secret.tagLen;
  if (len < packetLen || packetLen < pnOffset + 4 + 16) return -1;
  if (out != header.data())
    memcpy(out, header.data(), header.length());

  Ztls_::AeadContext *ctx = state.aead;
  Ztls_::aeadEncryptV(
    ctx, out + header.length(), plain, plainCount, pn,
    out, header.length());

  ZuBArray<InitialSecret::HPMaskLen>
    mask(InitialSecret::HPMaskLen, false);
  if (!trafficMask(
	mask.data(), mask.length(), state,
	ZuBSpan{out + pnOffset + 4, 16}))
    return -1;
  out[0] ^= mask[0] & firstMask;
  for (unsigned i = 0; i < pnLength; ++i)
    out[pnOffset + i] ^= mask[1 + i];
  return int(packetLen);
}

template <bool PreserveCiphertext>
static int unprotect_(
  uint8_t *packet, unsigned len, PktProtState &state,
  uint64_t largestPN, unsigned pnOffset, uint64_t &pn,
  unsigned &payloadOffset, uint8_t firstMask)
{
  const TrafficSecret &secret = state.secret;
  if (!state.valid() || !secret.valid() || !state.aead ||
      !secret.aead || pnOffset + 4 + 16 > len)
    return -1;
  if (len > BufSize) return -1;
  uint8_t first = packet[0];
  ZuBArray<4> pnBytes(4, false);
  memcpy(pnBytes.data(), packet + pnOffset, pnBytes.length());
  auto fail = [packet, pnOffset, first, &pnBytes]() -> int {
    packet[0] = first;
    memcpy(packet + pnOffset, pnBytes.data(), pnBytes.length());
    return -1;
  };
  ZuBArray<InitialSecret::HPMaskLen>
    mask(InitialSecret::HPMaskLen, false);
  if (!trafficMask(
	mask.data(), mask.length(), state,
	ZuBSpan{packet + pnOffset + 4, 16}))
    return fail();
  packet[0] ^= mask[0] & firstMask;
  unsigned pnLength = (packet[0] & 0x03) + 1;
  if (pnLength < 1 || pnLength > 4 || pnOffset + pnLength > len)
    return fail();
  uint64_t truncated = 0;
  for (unsigned i = 0; i < pnLength; ++i) {
    packet[pnOffset + i] ^= mask[1 + i];
    truncated = (truncated << 8) | packet[pnOffset + i];
  }
  pn = PktNumber::decode(largestPN, truncated, pnLength * 8);
  payloadOffset = pnOffset + pnLength;

  Ztls_::AeadContext *ctx = state.aead;
  unsigned cipherLen = len - payloadOffset;
  size_t n = 0;
  if constexpr (PreserveCiphertext) {
    using Plain = ZtArray<
      uint8_t, ZtArrayHeapID<"Zquic.PktProt.Plain">>;
    auto plain = ZmScratch(uint8_t, cipherLen, Plain::VHeap);
    plain.length(cipherLen);
    n = Ztls_::aeadDecrypt(
      ctx, plain.data(), packet + payloadOffset,
      cipherLen, pn, packet, payloadOffset);
    if (n == SIZE_MAX) return fail();
    if (n > cipherLen) return fail();
    if (n) memcpy(packet + payloadOffset, plain.data(), n);
  } else {
    n = Ztls_::aeadDecrypt(
      ctx, packet + payloadOffset, packet + payloadOffset,
      cipherLen, pn, packet, payloadOffset);
    if (n == SIZE_MAX) return fail();
    if (n > cipherLen) return fail();
  }
  return int(n);
}

int PktProt::protectLongV(
  uint8_t *out, unsigned len, PktProtState &state, uint64_t pn,
  ZuBSpan header, const Ztls_::Bytes *plain, unsigned plainCount,
  unsigned pnOffset, unsigned pnLength)
{
  unsigned plainLen;
  if (!plainLength(plain, plainCount, plainLen)) return -1;
  return protectLongV_(out, len, state, pn, header, plain, plainCount,
    plainLen, pnOffset, pnLength);
}

int PktProt::protectLongV_(
  uint8_t *out, unsigned len, PktProtState &state, uint64_t pn,
  ZuBSpan header, const Ztls_::Bytes *plain, unsigned plainCount,
  unsigned plainLen, unsigned pnOffset, unsigned pnLength)
{
  return protect_(out, len, state, pn, header, plain, plainCount,
    plainLen, pnOffset, pnLength, 0x0f);
}

int PktProt::protectLong(
  uint8_t *out, unsigned len, PktProtState &state, uint64_t pn,
  ZuBSpan header, ZuBSpan plaintext, unsigned pnOffset, unsigned pnLength)
{
  Ztls_::Bytes plain =
    Ztls_::wireBytes(plaintext.data(), plaintext.length());
  return protectLongV_(out, len, state, pn, header, &plain, 1,
    plaintext.length(), pnOffset, pnLength);
}

int PktProt::protectLong(
  uint8_t *out, unsigned len, const TrafficSecret &secret, uint64_t pn,
  ZuBSpan header, ZuBSpan plaintext, unsigned pnOffset, unsigned pnLength)
{
  PktProtState state;
  if (!state.init(secret, PktNumSpace::Handshake, true)) return -1;
  return protectLong(out, len, state, pn, header, plaintext,
    pnOffset, pnLength);
}

int PktProt::unprotectLong(
  uint8_t *packet, unsigned len, PktProtState &state,
  uint64_t largestPN, unsigned pnOffset, uint64_t &pn,
  unsigned &payloadOffset)
{
  return unprotect_<false>(packet, len, state, largestPN, pnOffset, pn,
    payloadOffset, 0x0f);
}

int PktProt::unprotectLong(
  uint8_t *packet, unsigned len, const TrafficSecret &secret,
  uint64_t largestPN, unsigned pnOffset, uint64_t &pn,
  unsigned &payloadOffset)
{
  PktProtState state;
  if (!state.init(secret, PktNumSpace::Handshake, false)) return -1;
  return unprotectLong(packet, len, state, largestPN, pnOffset, pn,
    payloadOffset);
}

int PktProt::protectShortV(
  uint8_t *out, unsigned len, PktProtState &state, uint64_t pn,
  ZuBSpan header, const Ztls_::Bytes *plain, unsigned plainCount,
  unsigned pnOffset, unsigned pnLength)
{
  unsigned plainLen;
  if (!plainLength(plain, plainCount, plainLen)) return -1;
  return protectShortV_(out, len, state, pn, header, plain, plainCount,
    plainLen, pnOffset, pnLength);
}

int PktProt::protectShortV_(
  uint8_t *out, unsigned len, PktProtState &state, uint64_t pn,
  ZuBSpan header, const Ztls_::Bytes *plain, unsigned plainCount,
  unsigned plainLen, unsigned pnOffset, unsigned pnLength)
{
  return protect_(out, len, state, pn, header, plain, plainCount,
    plainLen, pnOffset, pnLength, 0x1f);
}

int PktProt::protectShort(
  uint8_t *out, unsigned len, PktProtState &state, uint64_t pn,
  ZuBSpan header, ZuBSpan plaintext, unsigned pnOffset, unsigned pnLength)
{
  Ztls_::Bytes plain =
    Ztls_::wireBytes(plaintext.data(), plaintext.length());
  return protectShortV_(out, len, state, pn, header, &plain, 1,
    plaintext.length(), pnOffset, pnLength);
}

int PktProt::protectShort(
  uint8_t *out, unsigned len, const TrafficSecret &secret, uint64_t pn,
  ZuBSpan header, ZuBSpan plaintext, unsigned pnOffset, unsigned pnLength)
{
  PktProtState state;
  if (!state.init(secret, PktNumSpace::AppData, true)) return -1;
  return protectShort(out, len, state, pn, header, plaintext,
    pnOffset, pnLength);
}

int PktProt::unprotectShort(
  uint8_t *packet, unsigned len, PktProtState &state,
  uint64_t largestPN, unsigned pnOffset, uint64_t &pn,
  unsigned &payloadOffset)
{
  return unprotect_<true>(packet, len, state, largestPN, pnOffset, pn,
    payloadOffset, 0x1f);
}

int PktProt::unprotectShort(
  uint8_t *packet, unsigned len, const TrafficSecret &secret,
  uint64_t largestPN, unsigned pnOffset, uint64_t &pn,
  unsigned &payloadOffset)
{
  PktProtState state;
  if (!state.init(secret, PktNumSpace::AppData, false)) return -1;
  return unprotectShort(packet, len, state, largestPN, pnOffset, pn,
    payloadOffset);
}

Crypto::~Crypto()
{
  resetTLS_();
}

bool Crypto::init(const CryptoConfig &config)
{
  resetTLS_();
  if (config.alpn.length() > 255) return false;
  m_isServer = config.isServer;
  m_earlyDataEnabled = config.enable0RTT;
  m_earlyDataState =
    config.enable0RTT ? EarlyDataState::Enabled : EarlyDataState::Disabled;
  m_oneRTTReady = false;
  for (auto &installed : m_secretInstalled) installed = false;
  for (auto &secret : m_txTrafficSecrets) secret.clear();
  for (auto &secret : m_rxTrafficSecrets) secret.clear();
  m_txEarlySecret.clear();
  m_rxEarlySecret.clear();
  for (auto &gen : m_txKeyGen) gen = 0;
  for (auto &state : m_rxProt) state.clear();
  m_rxEarlyProt.clear();
  m_alpn = ParamString{config.alpn};
  m_serverName = Host{config.serverName};
  m_keyLogPath = ParamString{config.keyLogPath};
  m_qlogTrace = config.qlogTrace;
  m_saveSessionTicketArg = config.saveSessionTicketArg;
  m_saveSessionTicket = config.saveSessionTicket;
  if (!m_keyLogPath)
    if (auto path = Zt::getpath("SSLKEYLOGFILE"))
      m_keyLogPath = ParamString{path};
  m_localParams = config.localParams ?
    *config.localParams : TransportParams{};
  m_peerParams = {};
  m_peerParamsSet = false;
  m_peerParamsGen = 0;
  return true;
}

bool Crypto::initTLS(const CryptoConfig &config)
{
  if (!init(config)) return false;
  if (!initTLSContext(config)) {
    resetTLS_();
    return false;
  }
  if (!initTLSProperties(config)) {
    resetTLS_();
    return false;
  }
  return true;
}

bool Crypto::initTLSContext(const CryptoConfig &config)
{
  if (!Ztls::cryptoInit()) return false;
  m_tlsCtx.random = &Ztls_::opensslRandom;
  m_tlsCtx.getTime = &Ztls_::getTime;
  m_tlsCtx.keyExchanges = Ztls_::opensslKeyExchanges;
  for (auto suite : Ztls_::opensslCipherSuites)
    if (trafficHPCipher(suite))
      m_tlsCtx.cipherSuites.push(suite);
  m_tlsCtx.serverCipherPreference = 1;
  m_tlsCtx.maxEarlyDataSize = config.enable0RTT ? UINT32_MAX : 0;
  m_tlsCtx.omitEndOfEarlyData = 1;

  struct TrafficKeyHook : Ztls_::UpdateTrafficKey {
      int call(Ztls::Cxn *tls, int isEnc, size_t epoch, const void *secret) override { return Crypto::updateTrafficKeyCB(tls, isEnc, epoch, secret); }
    };
    static TrafficKeyHook updateTrafficKey;
  m_tlsCtx.updateTrafficKey = &updateTrafficKey;

  if (m_isServer) {
    if (!config.certPath || !config.keyPath) return false;
    if (!Ztls::loadCerts(&m_tlsCtx, config.certPath.data()))
      return false;
    if (!Ztls::PK::loadPEM(m_key, config.keyPath.data())) return false;
    m_sign = Ztls::signCertNew(m_key);
    if (!m_sign) return false;
    m_tlsCtx.signCertificate = Ztls::signCertHook(m_sign);
    struct ClientHelloHook : Ztls_::OnClientHello {
      int call(Ztls::Cxn *tls, Ztls_::CliHelloParams *params) override { return Crypto::onClientHelloCB(tls, params); }
    };
    static ClientHelloHook onClientHello;
    m_tlsCtx.onClientHello = &onClientHello;
    if (config.encryptTicket) {
      m_tlsCtx.ticketLifetime = 86400;
      m_tlsCtx.encryptTicket = config.encryptTicket;
    }
  } else {
    if (m_saveSessionTicket) {
      struct SessionTicketHook : Ztls_::SaveTicket {
      int call(Ztls::Cxn *tls, Ztls_::Bytes input) override { return Crypto::saveSessionTicketCB(tls, input); }
    };
    static SessionTicketHook saveSessionTicket;
      m_tlsCtx.saveTicket = &saveSessionTicket;
    }
    if (config.caPath) {
      m_certStore = Ztls::certStoreNew();
      if (!m_certStore) return false;
      bool ok = isDir(config.caPath) ?
	Ztls::certStoreLoadPath(
	  m_certStore, config.caPath.data()) :
	Ztls::certStoreLoadFile(
	  m_certStore, config.caPath.data());
      if (!ok) return false;
      m_verify = Ztls::verifyCertNew(m_certStore);
      if (!m_verify) return false;
      m_tlsCtx.verifyCertificate = Ztls::verifyCertHook(m_verify);
    }
  }

  m_tls.initialize(&m_tlsCtx, m_isServer, this);
  if (!m_isServer && m_serverName)
    if (Ztls_::serverName(&m_tls, m_serverName))
      return false;
  return true;
}

bool Crypto::initTLSProperties(const CryptoConfig &config)
{
  unsigned transportParamsLen =
    m_localParams.encodedLength();
  m_tlsParams.length(transportParamsLen);
  int n = encodeParams(
    m_tlsParams.data(), m_tlsParams.length(),
    m_localParams);
  if (n < 0 || unsigned(n) != m_tlsParams.length()) return false;
  m_tlsExtensions[0].type = TLSExtQUICParamsV1;
  m_tlsExtensions[0].data =
    Ztls_::wireBytes(m_tlsParams.data(), m_tlsParams.length());
  m_tlsExtensions[1].type = UINT16_MAX;
  m_tlsExtensions[1].data = {};
  m_tls.handshakeProps.additionalExtensions = m_tlsExtensions;
  struct ExtensionHooks : Ztls_::ExtensionCollector {
    bool collect(Ztls::Cxn *tls, uint16_t type) override
    { return Crypto::collectExtensionCB(tls, type); }
    int collected(Ztls::Cxn *tls, Ztls_::RawExtension *extensions) override
    { return Crypto::collectedExtensionsCB(tls, extensions); }
  };
  static ExtensionHooks extensionHooks;
  m_tls.handshakeProps.extensions = &extensionHooks;

  if (!m_isServer && m_alpn) {
    m_alpnVec = Ztls_::wireBytes(m_alpn.data(), m_alpn.length());
    m_tls.handshakeProps.client.negotiatedProtocols = {&m_alpnVec, 1};
  }
  if (!m_isServer) {
    m_maxEarlyData = 0;
    if (config.sessionTicket)
      m_tls.handshakeProps.client.sessionTicket =
	Ztls_::wireBytes(config.sessionTicket.data(), config.sessionTicket.length());
    if (config.enable0RTT) {
      m_tls.handshakeProps.client.maxEarlyDataSize = &m_maxEarlyData;
      m_tls.handshakeProps.client.earlyDataAcceptance =
	Ztls_::EarlyDataAcceptance::Unknown;
      if (config.sessionTicket)
	m_earlyDataState = EarlyDataState::Offered;
    }
  }
  return true;
}

void Crypto::resetTLS_()
{
  m_tls.retireRx();
  m_tls.retireTx();
  if (m_sign) {
    Ztls::signCertFree(m_sign);
    m_sign = nullptr;
  }
  m_key.reset(nullptr);
  if (m_verify) {
    Ztls::verifyCertFree(m_verify);
    m_verify = nullptr;
  }
  if (m_certStore) {
    Ztls::certStoreFree(m_certStore);
    m_certStore = nullptr;
  }
  m_tlsCtx = {};
  for (auto &extension : m_tlsExtensions) extension = {};
  m_tlsFailed = false;
  m_tlsKeyUpdateRejected = false;
  m_tlsParams = {};
  m_alpnVec = {};
  m_maxEarlyData = 0;
  m_tlsResult = Ztls_ErrorInProgress;
}

int Crypto::updateTrafficKeyCB(
  Ztls::Cxn *tls, int isEnc, size_t epoch,
  const void *secret)
{
  auto crypto = static_cast<Crypto *>(tls->dataPtr);
  return crypto ? crypto->updateTrafficKey_(isEnc, epoch, secret) : 0;
}

int Crypto::onClientHelloCB(
  Ztls::Cxn *tls,
  Ztls_::CliHelloParams *params)
{
  auto crypto = static_cast<Crypto *>(tls->dataPtr);
  return crypto ? crypto->onClientHello_(params) :
    Ztls_AlertToPeerError(Ztls_AlertInternalError);
}

int Crypto::saveSessionTicketCB(
  Ztls::Cxn *tls, Ztls_::Bytes input)
{
  auto crypto = static_cast<Crypto *>(tls->dataPtr);
  return crypto ? crypto->saveSessionTicket_(input) : 0;
}

void Crypto::keyLog(int isEnc, PktKeyLevel::T level, ZuBSpan secret)
{
  if (!m_keyLogPath || !tlsReady() || !secret) return;
  if (level != PktKeyLevel::ZeroRTT &&
      level != PktKeyLevel::Handshake &&
      level != PktKeyLevel::OneRTT)
    return;
  Ztls_::Bytes random = Ztls_::cliRandom(&m_tls);
  if (!random.data() || random.length() != 32) return;
  bool clientSecret = isEnc ? !m_isServer : m_isServer;
  const char *label = nullptr;
  switch (level) {
    case PktKeyLevel::ZeroRTT:
      label = "CLIENT_EARLY_TRAFFIC_SECRET";
      break;
    case PktKeyLevel::Handshake:
      label = clientSecret ?
	"CLIENT_HANDSHAKE_TRAFFIC_SECRET" :
	"SERVER_HANDSHAKE_TRAFFIC_SECRET";
      break;
    case PktKeyLevel::OneRTT:
      label = clientSecret ?
	"CLIENT_TRAFFIC_SECRET_0" : "SERVER_TRAFFIC_SECRET_0";
      break;
    default:
      return;
  }
  KeyLogString line;
  line << label << ' ';
  hexAppend(line, ZuBSpan{random.data(), unsigned(random.length())});
  line << ' ';
  hexAppend(line, secret);
  line << '\n';
  ZiFile file;
  if (file.open(m_keyLogPath, ZiFile::Append | ZiFile::GC) != Zi::OK) return;
  (void)file.write(line.data(), line.length());
  file.close();
}

int Crypto::updateTrafficKey_(int isEnc, size_t epoch, const void *secret)
{
  if (!secret) return 0;
  PktKeyLevel::T keyLevel = PktKeyLevel::Initial;
  if (!keyLevelFromTLSEpoch(epoch, keyLevel) ||
      keyLevel == PktKeyLevel::Initial)
    return 0;
  PktNumSpace::T space = spaceFromKeyLevel(keyLevel);
  if (space == PktNumSpace::N) return 0;

  const Ztls_::CipherSuite *cipher = Ztls_::cipher(&m_tls);
  if (!cipher || !cipher->hash) return -1;
  ZuBSpan secretSpan{
    static_cast<const uint8_t *>(secret),
    unsigned(cipher->hash->digestSize)};
  TrafficSecret &traffic = keyLevel == PktKeyLevel::ZeroRTT ?
    (isEnc ? m_txEarlySecret : m_rxEarlySecret) :
    (isEnc ? m_txTrafficSecrets[space] : m_rxTrafficSecrets[space]);
  bool installed = traffic.valid();
  if (!PktProt::deriveSecret(traffic, cipher, secretSpan))
    return -1;
  if (isEnc)
    ++m_txKeyGen[keyLevel];
  else {
    PktProtState &state = keyLevel == PktKeyLevel::ZeroRTT ?
      m_rxEarlyProt : m_rxProt[space];
    if (!state.init(traffic, space, false)) {
      traffic.clear();
      return -1;
    }
  }
  keyLog(isEnc, keyLevel, secretSpan);
  if (keyLevel == PktKeyLevel::ZeroRTT) {
    m_earlyDataEnabled = true;
    if (m_isServer)
      m_earlyDataState = EarlyDataState::Accepted;
    else if (m_earlyDataState == EarlyDataState::Disabled ||
	m_earlyDataState == EarlyDataState::Enabled)
      m_earlyDataState = EarlyDataState::Offered;
    if (!installed) ++m_diag.secretsInstalled;
  } else {
    installSecret(space, secretSpan);
  }
  return 0;
}

int Crypto::onClientHello_(Ztls_::CliHelloParams *params)
{
  if (!params) return Ztls_AlertToPeerError(Ztls_AlertInternalError);
  if (params->serverName.data() && params->serverName.length())
    Ztls_::serverName(&m_tls, ZuCSpan{params->serverName});
  if (!m_alpn) return 0;
  for (auto protocol : params->negotiatedProtocols) {
    if (protocol.length() != m_alpn.length() ||
	memcmp(protocol.data(), m_alpn.data(), m_alpn.length()))
      continue;
    return Ztls_::negotiatedProtocol(&m_tls, m_alpn);
  }
  return Ztls_AlertToPeerError(Ztls_AlertNoAppProto);
}

int Crypto::saveSessionTicket_(Ztls_::Bytes input)
{
  if (m_saveSessionTicket) {
    ZuBSpan ticket;
    if (input.data() && input.length() <= UINT_MAX)
      ticket = ZuBSpan{input.data(), unsigned(input.length())};
    m_saveSessionTicket(m_saveSessionTicketArg, ticket);
  }
  return 0;
}

int Crypto::collectExtensionCB(
  Ztls::Cxn *, uint16_t type)
{
  return type == TLSExtQUICParamsV1;
}

int Crypto::collectedExtensionsCB(
  Ztls::Cxn *tls, Ztls_::RawExtension *extensions)
{
  auto crypto = static_cast<Crypto *>(tls->dataPtr);
  return crypto ? crypto->collectedExtensions(extensions) :
    Ztls_AlertToPeerError(Ztls_AlertInternalError);
}

int Crypto::collectedExtensions(Ztls_::RawExtension *extensions)
{
  if (!extensions)
    return Ztls_AlertToPeerError(Ztls_AlertMissingExt);
  for (; extensions->type != UINT16_MAX; ++extensions) {
    if (extensions->type != TLSExtQUICParamsV1) continue;
    ZuBSpan in{extensions->data.data(), unsigned(extensions->data.length())};
    if (decodeParams(in, m_peerParams) < 0)
      return Ztls_AlertToPeerError(Ztls_AlertDecodeError);
    m_peerParamsSet = true;
    ++m_peerParamsGen;
    return 0;
  }
  return Ztls_AlertToPeerError(Ztls_AlertMissingExt);
}

ZuCSpan Crypto::negotiatedProtocol() const
{
  if (!tlsReady()) return {};
  auto protocol = m_tls.negotiatedProtocol.data();
  if (!protocol) return {};
  return ZuCSpan{protocol, unsigned(strlen(protocol))};
}

size_t Crypto::tlsReadEpoch() const
{
  return tlsReady() ? Ztls_::rawReadEpoch(&m_tls) : 0;
}

int Crypto::handleTLSMessage(
  ZiIOBuf *out, CryptoOffsets &epochOffsets, size_t inEpoch, ZuBSpan input)
{
  using namespace Ztls_;
  m_tlsKeyUpdateRejected = false;
  if (!tlsReady()) return -1;
  if (!out || !out->size) {
    m_tlsResult = Ztls_ErrorRawOutput;
    return -1;
  }
  out->clear();
  auto operation = RawHandshakeOp::T{RawHandshakeOp::Input};
  if (!input.length()) {
    if (!m_isServer && m_tls.state == Ztls_::State::CliHSStart)
      operation = RawHandshakeOp::Start;
    else if (m_tls.state == Ztls_::State::SrvBuildCertSig)
      operation = RawHandshakeOp::ResumeSignature;
  }
  auto result = rawHandshake(
    &m_tls, &m_tls.handshakeProps, operation, inEpoch, input,
    {out->data_(), out->size});
  m_tlsResult = result.status;
  m_tlsKeyUpdateRejected = result.keyUpdateRejected;
  if (input) ++m_diag.tlsMessagesHandled;
  if (result.status && result.status != Ztls_ErrorInProgress &&
      result.status != Ztls_ErrorAsyncOperation)
    return -1;
  if (result.length > out->size || !validCryptoOffsets(result.offsets, result.length)) {
    m_tlsResult = Ztls_ErrorRawOutput;
    return -1;
  }
  epochOffsets = result.offsets;
  out->length = result.length;
  if (result.length) ++m_diag.tlsMessagesEmitted;
  syncEarlyDataState();
  if (!result.status && handshakeComplete(&m_tls)) m_oneRTTReady = true;
  return int(result.length);
}

void Crypto::syncEarlyDataState()
{
  if (m_isServer || !m_earlyDataEnabled) return;
  switch (m_tls.handshakeProps.client.earlyDataAcceptance) {
    case Ztls_::EarlyDataAcceptance::Accepted:
      m_earlyDataState = EarlyDataState::Accepted;
      break;
    case Ztls_::EarlyDataAcceptance::Rejected:
      if (m_earlyDataState != EarlyDataState::Rejected) {
	if (m_txEarlySecret.valid()) ++m_txKeyGen[PktKeyLevel::ZeroRTT];
	m_txEarlySecret.clear();
	m_rxEarlySecret.clear();
	m_rxEarlyProt.clear();
	m_earlyDataEnabled = false;
	m_earlyDataState = EarlyDataState::Rejected;
	++m_diag.zeroRTTRejected;
      }
      break;
    default:
      if (m_earlyDataState == EarlyDataState::Enabled && m_maxEarlyData)
	m_earlyDataState = EarlyDataState::Offered;
      break;
  }
}

int Crypto::encodeParams(
  uint8_t *out, unsigned len, const TransportParams &params)
{
  int n = params.encode(out, len);
  if (n >= 0) ++m_diag.transportParamsEncoded;
  return n;
}

int Crypto::decodeParams(ZuBSpan in, TransportParams &params)
{
  int n = params.decode(in);
  if (!n) ++m_diag.transportParamsDecoded;
  return n;
}

void Crypto::installSecret(PktNumSpace::T level, ZuBSpan secret)
{
  ZiAssert(level >= PktNumSpace::Initial && level <= PktNumSpace::AppData,
    "Zquic", (), "invalid packet space", return);
  ZiAssert(secret.length(), "Zquic", (),
    "empty traffic secret", return);
  if (!m_secretInstalled[level]) ++m_diag.secretsInstalled;
  m_secretInstalled[level] = true;
}

bool Crypto::discardSecret(PktNumSpace::T level)
{
  ZiAssert(level >= PktNumSpace::Initial && level <= PktNumSpace::AppData,
    "Zquic", (), "invalid packet space", return false);
  bool installed = m_secretInstalled[level] ||
    m_txTrafficSecrets[level].valid() || m_rxTrafficSecrets[level].valid();
  m_secretInstalled[level] = false;
  if (level == PktNumSpace::Initial)
    m_initialKeys.clear();
  if (m_txTrafficSecrets[level].valid()) {
    auto keyLevel = level == PktNumSpace::Initial ? PktKeyLevel::Initial :
      level == PktNumSpace::Handshake ? PktKeyLevel::Handshake : PktKeyLevel::OneRTT;
    ++m_txKeyGen[keyLevel];
  }
  m_txTrafficSecrets[level].clear();
  m_rxTrafficSecrets[level].clear();
  m_rxProt[level].clear();
  if (installed) ++m_diag.secretsDiscarded;
  return installed;
}

bool Crypto::deriveInitial(const CxnID &dcid)
{
  if (!InitialCrypto::derive(m_initialKeys, dcid)) return false;
  ++m_diag.initialKeysDerived;
  return true;
}

bool Crypto::rejectZeroRTT()
{
  bool wasRejected = m_earlyDataState == EarlyDataState::Rejected;
  bool hadEarly =
    m_txEarlySecret.valid() || m_rxEarlySecret.valid();
  if (m_txEarlySecret.valid()) ++m_txKeyGen[PktKeyLevel::ZeroRTT];
  m_txEarlySecret.clear();
  m_rxEarlySecret.clear();
  m_rxEarlyProt.clear();
  m_earlyDataEnabled = false;
  m_earlyDataState = EarlyDataState::Rejected;
  if (hadEarly) ++m_diag.secretsDiscarded;
  if (!wasRejected) ++m_diag.zeroRTTRejected;
  if (m_qlogTrace)
    ZquicLOG(*m_qlogTrace, ([](auto &o, ZuTime time) {
      SecEvt event{
	.kind = SecKind::TLS,
	.trigger = SecTrigger::Received,
	.reason = zeroRTTSecReason(ZeroRTTReason::TLSRejected),
	.success = false
      };
      o.logSecEvt(EvtName::ZeroRTTReject, event, time);
    }));
  return true;
}

bool Crypto::completeHandshake()
{
  m_oneRTTReady = m_secretInstalled[PktNumSpace::AppData];
  if (m_oneRTTReady) {
    discardSecret(PktNumSpace::Initial);
    discardSecret(PktNumSpace::Handshake);
  }
  return m_oneRTTReady;
}

} // namespace Zquic
