//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZquicCrypto.hh>

#include "ZquicOpenSSL.hh"

#include <zlib/ZtlsBackend.hh>

#include <zlib/ZtLocalArray.hh>

#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <zpicotls/openssl.h>

namespace Zquic {

namespace {

static constexpr uint8_t InitialSaltV1_[] = {
  0x38, 0x76, 0x2c, 0xf7, 0xf5, 0x59, 0x34, 0xb3, 0x4d, 0x17,
  0x9a, 0xe6, 0xa4, 0xc8, 0x0c, 0xad, 0xcc, 0xbb, 0x7f, 0x0a
};

static bool hmacSHA256_(
  const uint8_t *key, unsigned keyLen, const uint8_t *data, unsigned dataLen,
  uint8_t *out, unsigned outLen)
{
  unsigned n = 0;
  if (outLen < InitialSecret::SecretLen) return false;
  return HMAC(EVP_sha256(), key, int(keyLen), data, dataLen, out, &n) &&
    n == InitialSecret::SecretLen;
}

static bool hkdfExtract_(
  uint8_t *out, const uint8_t *salt, unsigned saltLen, ZuCSpan ikm)
{
  return hmacSHA256_(
    salt, saltLen, reinterpret_cast<const uint8_t *>(ikm.data()), ikm.length(),
    out, InitialSecret::SecretLen);
}

static bool hkdfExpand_(
  uint8_t *out, unsigned outLen, const uint8_t *secret, unsigned secretLen,
  const uint8_t *info, unsigned infoLen)
{
  uint8_t t[InitialSecret::SecretLen];
  uint8_t msg[InitialSecret::SecretLen + 256 + 1];
  unsigned tLen = 0, off = 0;
  for (uint8_t block = 1; off < outLen; ++block) {
    if (tLen + infoLen + 1 > sizeof(msg)) return false;
    memcpy(msg, t, tLen);
    memcpy(msg + tLen, info, infoLen);
    msg[tLen + infoLen] = block;
    if (!hmacSHA256_(secret, secretLen, msg, tLen + infoLen + 1,
	  t, sizeof(t)))
      return false;
    unsigned n = outLen - off;
    if (n > sizeof(t)) n = sizeof(t);
    memcpy(out + off, t, n);
    off += n;
    tLen = sizeof(t);
  }
  return true;
}

static bool hkdfExpandLabel_(
  uint8_t *out, unsigned outLen, const uint8_t *secret, unsigned secretLen,
  ZuCSpan label)
{
  static constexpr char Prefix[] = "tls13 ";
  uint8_t info[64];
  unsigned o = 0;
  unsigned fullLen = (sizeof(Prefix) - 1) + label.length();
  if (fullLen > 255 || outLen > 0xffff) return false;
  info[o++] = uint8_t(outLen >> 8);
  info[o++] = uint8_t(outLen);
  info[o++] = uint8_t(fullLen);
  memcpy(info + o, Prefix, sizeof(Prefix) - 1);
  o += sizeof(Prefix) - 1;
  memcpy(info + o, label.data(), label.length());
  o += label.length();
  info[o++] = 0;
  return hkdfExpand_(out, outLen, secret, secretLen, info, o);
}

static bool deriveSecret_(InitialSecret &out, const uint8_t *initial,
    ZuCSpan label)
{
  return hkdfExpandLabel_(
      out.secret, sizeof(out.secret), initial, InitialSecret::SecretLen, label) &&
    hkdfExpandLabel_(
      out.key, sizeof(out.key), out.secret, sizeof(out.secret), "quic key") &&
    hkdfExpandLabel_(
      out.iv, sizeof(out.iv), out.secret, sizeof(out.secret), "quic iv") &&
    hkdfExpandLabel_(
      out.hp, sizeof(out.hp), out.secret, sizeof(out.secret), "quic hp");
}

static void nonce_(uint8_t *out, const InitialSecret &secret, uint64_t pn)
{
  memcpy(out, secret.iv, InitialSecret::IVLen);
  for (unsigned i = 0; i < 8; ++i)
    out[InitialSecret::IVLen - 1 - i] ^= uint8_t(pn >> (i * 8));
}

static bool initCipherSuites_(ptls_cipher_suite_t **suites, unsigned max)
{
  unsigned n = 0;
  for (auto p = ptls_openssl_cipher_suites; *p; ++p) {
    if ((*p)->aead && (*p)->aead->ecb_cipher && (*p)->aead->ctr_cipher) {
      if (n >= max) return false;
      suites[n++] = *p;
    }
  }
  suites[n] = nullptr;
  return true;
}

static void freeCertificates_(ptls_context_t &ctx)
{
  if (ctx.certificates.list) {
    for (unsigned i = 0; i < ctx.certificates.count; ++i)
      ::free(ctx.certificates.list[i].base);
    ::free(ctx.certificates.list);
  }
  ctx.certificates.count = 0;
  ctx.certificates.list = nullptr;
}

static bool isDir_(ZuCSpan path)
{
  if (!path) return false;
  struct stat st;
  return !stat(path.data(), &st) && S_ISDIR(st.st_mode);
}

} // namespace

void CryptoStream::reset()
{
  m_txOffset = 0;
  m_rxOffset = 0;
  Rx::rxReset(0);
  m_delivery.length(0);
  m_txData.length(0);
}

bool CryptoStream::sent(uint64_t offset, ZuCSpan payload)
{
  uint64_t end = offset + payload.length();
  if (end < offset || end > MaxBufSize) return false;
  if (offset < m_txData.length()) {
    if (end > m_txData.length()) return false;
    return !memcmp(m_txData.data() + offset, payload.data(), payload.length());
  }
  if (offset != m_txData.length()) return false;
  m_txData.append(
    reinterpret_cast<const uint8_t *>(payload.data()), payload.length());
  return true;
}

bool CryptoStream::txPayload(
  uint64_t offset, uint64_t length, ZuCSpan &payload) const
{
  uint64_t end = offset + length;
  if (end < offset || end > m_txData.length()) return false;
  payload = ZuCSpan{
    reinterpret_cast<const char *>(m_txData.data() + offset),
    unsigned(length)};
  return true;
}

int CryptoStream::writeFrame(
  uint8_t *out, unsigned len, ZuCSpan payload, CryptoDiag *diag)
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
  const Frame &frame, ZuCSpan &contiguous, CryptoDiag *diag)
{
  if (frame.type != FrameType::Crypto) return -1;
  return receive(frame.offset, frame.payload, contiguous, diag);
}

int CryptoStream::receive(
  uint64_t offset, ZuCSpan payload, ZuCSpan &contiguous, CryptoDiag *diag)
{
  contiguous = {};
  m_delivery.length(0);
  uint64_t end = offset + payload.length();
  if (end < offset || end > MaxBufSize) return -1;
  if (!payload.length() || end <= m_rxOffset) return 0;
  uint64_t first = offset < m_rxOffset ? m_rxOffset : offset;
  if (first == m_rxOffset && !m_rxQueue.count_()) {
    unsigned payloadOffset = unsigned(first - offset);
    unsigned bytes = unsigned(end - first);
    contiguous = ZuCSpan{payload.data() + payloadOffset, bytes};
    m_rxOffset = end;
    m_rxQueue.head(end);
    if (diag) diag->cryptoBytesRx += bytes;
    return 0;
  }

  auto spans = ZtLocalArray(RxSpans, m_rxQueue.count_() + 1);
  if (!rxNovelSpans(m_rxQueue, first, end, spans)) return -1;
  uint64_t bytes = rxSpanBytes(spans);
  if (!queueRxSpans(spans, offset, payload,
	[](unsigned) -> ZmRef<ZiIOBuf> {
	  return new CryptoRxBufAlloc<BufSize, MaxBufSize>{nullptr};
	},
	[this](ZmRef<ZiIOBuf> buf, uint64_t offset, unsigned length) {
	  Rx::rcvd(new CryptoRxPQueue::Node{
	    RxData{ZuMv(buf), offset, 0, length}});
	}))
    return -1;

  if (m_asyncDelivery) resumeReadyDequeue_();
  if (diag) diag->cryptoBytesRx += bytes;
  if (!m_asyncDelivery && m_delivery.length())
    contiguous = ZuCSpan{m_delivery.data(), m_delivery.length()};
  return 0;
}

void CryptoStream::resumeReadyDequeue_()
{
  if (!m_rxQueue.count_()) return;
  if (!m_rxQueue.has(m_rxQueue.head())) return;
  Rx::resumeDequeue();
}

void CryptoStream::appendDelivery_(const uint8_t *p, uint64_t length)
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
  RxData &data = node->data();
  if (!data.buf || !data.bytes) return;
  const uint8_t *p = data.buf->data_() + data.bufOffset;
  appendDelivery_(p, data.bytes);
  if (m_asyncDelivery)
    m_deliveryFn(ZuCSpan{p, unsigned(data.bytes)});
  m_rxOffset += data.bytes;
}

bool InitialCrypto::derive(InitialKeyMaterial &out, const CxnID &dcid)
{
  out = {};
  if (!hkdfExtract_(out.initial, InitialSaltV1_, sizeof(InitialSaltV1_),
	dcid))
    return false;
  return deriveSecret_(out.client, out.initial, "client in") &&
    deriveSecret_(out.server, out.initial, "server in");
}

int InitialCrypto::encrypt(
  uint8_t *out, unsigned len, const InitialSecret &secret, uint64_t pn,
  ZuCSpan aad, ZuCSpan plaintext)
{
  ptls_iovec_t plain =
    ptls_iovec_init(plaintext.data(), plaintext.length());
  return encryptV(out, len, secret, pn, aad, &plain, 1);
}

int InitialCrypto::encryptV(
  uint8_t *out, unsigned len, const InitialSecret &secret, uint64_t pn,
  ZuCSpan aad, const ptls_iovec_t *plain, unsigned plainCount)
{
  if (!plain && plainCount) return -1;
  size_t plainLen = 0;
  for (unsigned i = 0; i < plainCount; ++i) {
    if (plain[i].len > UINT_MAX || plainLen > UINT_MAX - plain[i].len)
      return -1;
    plainLen += plain[i].len;
  }
  if (len < plainLen + InitialSecret::TagLen) return -1;
  uint8_t nonce[InitialSecret::IVLen];
  nonce_(nonce, secret, pn);

  EVP_CIPHER_CTX *ctx = opensslCipherCtx();
  if (!ctx) return -1;
  int n = 0, off = 0;
  bool ok =
    EVP_EncryptInit_ex(ctx, EVP_aes_128_gcm(), nullptr, nullptr, nullptr) == 1 &&
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, sizeof(nonce), nullptr) == 1 &&
    EVP_EncryptInit_ex(ctx, nullptr, nullptr, secret.key, nonce) == 1 &&
    (!aad.length() || EVP_EncryptUpdate(ctx, nullptr, &n,
      reinterpret_cast<const uint8_t *>(aad.data()), aad.length()) == 1);
  for (unsigned i = 0; ok && i < plainCount; ++i) {
    if (!plain[i].len) continue;
    ok = EVP_EncryptUpdate(
      ctx, out + off, &n, plain[i].base, plain[i].len) == 1;
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
  ZuCSpan aad, ZuCSpan ciphertext)
{
  if (ciphertext.length() < InitialSecret::TagLen) return -1;
  unsigned plainLen = ciphertext.length() - InitialSecret::TagLen;
  if (len < plainLen) return -1;
  uint8_t nonce[InitialSecret::IVLen];
  nonce_(nonce, secret, pn);

  EVP_CIPHER_CTX *ctx = opensslCipherCtx();
  if (!ctx) return -1;
  int n = 0, off = 0;
  bool ok =
    EVP_DecryptInit_ex(ctx, EVP_aes_128_gcm(), nullptr, nullptr, nullptr) == 1 &&
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, sizeof(nonce), nullptr) == 1 &&
    EVP_DecryptInit_ex(ctx, nullptr, nullptr, secret.key, nonce) == 1 &&
    (!aad.length() || EVP_DecryptUpdate(ctx, nullptr, &n,
      reinterpret_cast<const uint8_t *>(aad.data()), aad.length()) == 1) &&
    (!plainLen || EVP_DecryptUpdate(ctx, out, &n,
      reinterpret_cast<const uint8_t *>(ciphertext.data()), plainLen) == 1);
  if (ok) off = n;
  ok = ok &&
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, InitialSecret::TagLen,
      const_cast<char *>(ciphertext.data()) + plainLen) == 1 &&
    EVP_DecryptFinal_ex(ctx, out + off, &n) == 1;
  if (ok) off += n;
  return ok ? off : -1;
}

bool InitialCrypto::headerMask(
  uint8_t *out, unsigned len, const InitialSecret &secret, ZuCSpan sample)
{
  if (len < InitialSecret::HPMaskLen || sample.length() < 16) return false;
  uint8_t block[32];
  EVP_CIPHER_CTX *ctx = opensslCipherCtx();
  if (!ctx) return false;
  int n = 0, total = 0;
  bool ok =
    EVP_EncryptInit_ex(ctx, EVP_aes_128_ecb(), nullptr, secret.hp, nullptr) == 1 &&
    EVP_CIPHER_CTX_set_padding(ctx, 0) == 1 &&
    EVP_EncryptUpdate(ctx, block, &n,
      reinterpret_cast<const uint8_t *>(sample.data()), 16) == 1;
  if (ok) total = n;
  ok = ok && EVP_EncryptFinal_ex(ctx, block + total, &n) == 1;
  if (ok) memcpy(out, block, InitialSecret::HPMaskLen);
  return ok;
}

int InitialPktProt::protectLong(
  uint8_t *out, unsigned len, const InitialSecret &secret, uint64_t pn,
  ZuCSpan header, ZuCSpan plaintext, unsigned pnOffset, unsigned pnLength)
{
  ptls_iovec_t plain =
    ptls_iovec_init(plaintext.data(), plaintext.length());
  return protectLongV(out, len, secret, pn, header, &plain, 1,
    pnOffset, pnLength);
}

int InitialPktProt::protectLongV(
  uint8_t *out, unsigned len, const InitialSecret &secret, uint64_t pn,
  ZuCSpan header, const ptls_iovec_t *plain, unsigned plainCount,
  unsigned pnOffset, unsigned pnLength)
{
  if (pnLength < 1 || pnLength > 4 ||
      header.length() != pnOffset + pnLength)
    return -1;
  if (!plain && plainCount) return -1;
  size_t plainLen = 0;
  for (unsigned i = 0; i < plainCount; ++i) {
    if (plain[i].len > UINT_MAX || plainLen > UINT_MAX - plain[i].len)
      return -1;
    plainLen += plain[i].len;
  }
  unsigned packetLen = header.length() + unsigned(plainLen) +
    InitialSecret::TagLen;
  if (len < packetLen || packetLen < pnOffset + 4 + 16) return -1;
  if (reinterpret_cast<const char *>(out) != header.data())
    memcpy(out, header.data(), header.length());
  int n = InitialCrypto::encryptV(
    out + header.length(), len - header.length(), secret, pn,
    ZuCSpan{reinterpret_cast<const char *>(out), header.length()},
    plain, plainCount);
  if (n != int(plainLen + InitialSecret::TagLen)) return -1;

  uint8_t mask[InitialSecret::HPMaskLen];
  if (!InitialCrypto::headerMask(
	mask, sizeof(mask), secret,
	ZuCSpan{reinterpret_cast<const char *>(out + pnOffset + 4), 16}))
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
  uint8_t mask[InitialSecret::HPMaskLen];
  if (!InitialCrypto::headerMask(
	mask, sizeof(mask), secret,
	ZuCSpan{reinterpret_cast<const char *>(packet + pnOffset + 4), 16}))
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
    ZuCSpan{reinterpret_cast<const char *>(packet), payloadOffset},
    ZuCSpan{reinterpret_cast<const char *>(packet + payloadOffset),
      len - payloadOffset});
  return n;
}

void TrafficSecret::clear()
{
  memset(this, 0, sizeof(*this));
}

bool PktProtState::init(
  const TrafficSecret &secret_, CryptoLevel::T level_, bool tx_)
{
  clear();
  if (!secret_.valid() || !secret_.aead || !secret_.hpCipher ||
      !secret_.hpSuppCipher)
    return false;
  if (!aead.init(secret_.aead, tx_, secret_.key, secret_.iv)) return false;
  if (!hp.init(secret_.hpCipher, true, secret_.hp)) {
    clear();
    return false;
  }
  if (tx_ && !hpSupp.init(secret_.hpSuppCipher, true, secret_.hp)) {
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
  aead.clear();
  hp.clear();
  hpSupp.clear();
  secret.clear();
  level = CryptoLevel::Initial;
  tx = false;
  installed = false;
}

static bool hkdfExpandLabelPTLS_(
  ptls_hash_algorithm_t *hash, uint8_t *out, unsigned outLen,
  ZuCSpan secret, const char *label)
{
  return hash && out && outLen &&
    !ptls_hkdf_expand_label(
      hash, out, outLen,
      ptls_iovec_init(secret.data(), secret.length()),
      label, ptls_iovec_init(nullptr, 0), "tls13 ");
}

bool PktProt::deriveTrafficSecret(
  TrafficSecret &out, ptls_cipher_suite_t *cipher, ZuCSpan secret)
{
  out.clear();
  if (!cipher || !cipher->aead || !cipher->hash ||
      !cipher->aead->ecb_cipher || !cipher->aead->ctr_cipher || !secret)
    return false;

  unsigned secretLen = cipher->hash->digest_size;
  unsigned keyLen = cipher->aead->key_size;
  unsigned ivLen = cipher->aead->iv_size;
  if (ivLen < 8) ivLen = 8;
  unsigned hpLen = cipher->aead->ecb_cipher->key_size;
  unsigned hpSuppLen = cipher->aead->ctr_cipher->key_size;
  unsigned tagLen = cipher->aead->tag_size;
  if (secret.length() != secretLen ||
      secretLen > TrafficSecret::MaxSecretLen ||
      keyLen > TrafficSecret::MaxKeyLen ||
      ivLen > TrafficSecret::MaxIVLen ||
      hpLen > TrafficSecret::MaxHPLen ||
      hpSuppLen > TrafficSecret::MaxHPLen ||
      hpSuppLen != hpLen ||
      !tagLen)
    return false;

  if (!hkdfExpandLabelPTLS_(cipher->hash, out.key, keyLen,
	secret, "quic key") ||
      !hkdfExpandLabelPTLS_(cipher->hash, out.iv, ivLen,
	secret, "quic iv") ||
      !hkdfExpandLabelPTLS_(cipher->hash, out.hp, hpLen,
	secret, "quic hp"))
    return false;

  memcpy(out.secret, secret.data(), secret.length());
  out.secretLen = secretLen;
  out.keyLen = keyLen;
  out.ivLen = ivLen;
  out.hpLen = hpLen;
  out.tagLen = tagLen;
  out.aead = cipher->aead;
  out.hash = cipher->hash;
  out.hpCipher = cipher->aead->ecb_cipher;
  out.hpSuppCipher = cipher->aead->ctr_cipher;
  out.installed = true;
  return true;
}

bool PktProt::deriveNextTrafficSecret(
  TrafficSecret &out, const TrafficSecret &current)
{
  out.clear();
  if (!current.valid() || !current.hash || !current.aead ||
      !current.hpCipher || !current.secretLen ||
      current.secretLen > TrafficSecret::MaxSecretLen)
    return false;

  out.secretLen = current.secretLen;
  out.keyLen = current.keyLen;
  out.ivLen = current.ivLen;
  out.hpLen = current.hpLen;
  out.tagLen = current.tagLen;
  out.aead = current.aead;
  out.hash = current.hash;
  out.hpCipher = current.hpCipher;
  out.hpSuppCipher = current.hpSuppCipher;

  ZuCSpan secret{current.secret};
  secret.trunc(current.secretLen);
  if (!hkdfExpandLabelPTLS_(current.hash, out.secret, out.secretLen,
	secret, "quic ku")) {
    out.clear();
    return false;
  }
  ZuCSpan next{out.secret};
  next.trunc(out.secretLen);
  if (!hkdfExpandLabelPTLS_(current.hash, out.key, out.keyLen,
	next, "quic key") ||
      !hkdfExpandLabelPTLS_(current.hash, out.iv, out.ivLen,
	next, "quic iv")) {
    out.clear();
    return false;
  }
  memcpy(out.hp, current.hp, out.hpLen);
  out.installed = true;
  return true;
}

bool Crypto::updateTxTrafficSecret(
  CryptoLevel::T level, const TrafficSecret &secret)
{
  if (level < 0 || level >= 3 || !secret.valid()) return false;
  m_txTrafficSecrets[level] = secret;
  return m_txProt[level].init(m_txTrafficSecrets[level], level, true);
}

bool Crypto::updateRxTrafficSecret(
  CryptoLevel::T level, const TrafficSecret &secret)
{
  if (level < 0 || level >= 3 || !secret.valid()) return false;
  m_rxTrafficSecrets[level] = secret;
  return m_rxProt[level].init(m_rxTrafficSecrets[level], level, false);
}

static bool trafficMask_(
  uint8_t *mask, unsigned len, PktProtState &state, ZuCSpan sample)
{
  const TrafficSecret &secret = state.secret;
  if (len < InitialSecret::HPMaskLen ||
      !state.valid() || !secret.valid() || !state.hp.valid() ||
      !secret.hpCipher || sample.length() < 16)
    return false;
  ptls_cipher_context_t *ctx = state.hp.get();
  uint8_t block[32];
  ptls_cipher_init(ctx, nullptr);
  ptls_cipher_encrypt(ctx, block, sample.data(), 16);
  memcpy(mask, block, InitialSecret::HPMaskLen);
  return true;
}

static int protect_(
  uint8_t *out, unsigned len, PktProtState &state, uint64_t pn,
  ZuCSpan header, const ptls_iovec_t *plain, unsigned plainCount,
  unsigned pnOffset, unsigned pnLength,
  uint8_t firstMask)
{
  const TrafficSecret &secret = state.secret;
  if (!state.valid() || !secret.valid() || !state.aead.valid() ||
      !state.hpSupp.valid() || !secret.aead || !secret.hpSuppCipher ||
      (!plain && plainCount) ||
      pnLength < 1 || pnLength > 4 ||
      header.length() != pnOffset + pnLength)
    return -1;
  size_t plainLen = 0;
  for (unsigned i = 0; i < plainCount; ++i) {
    if (plain[i].len > UINT_MAX || plainLen > UINT_MAX - plain[i].len)
      return -1;
    plainLen += plain[i].len;
  }
  unsigned packetLen = header.length() + unsigned(plainLen) + secret.tagLen;
  if (len < packetLen || packetLen < pnOffset + 4 + 16) return -1;
  if (reinterpret_cast<const char *>(out) != header.data())
    memcpy(out, header.data(), header.length());

  ptls_aead_context_t *ctx = state.aead.get();
  ptls_aead_encrypt_v(
    ctx, out + header.length(), plain, plainCount, pn,
    out, header.length());
  size_t n = plainLen + secret.tagLen;
  if (n != plainLen + secret.tagLen) return -1;

  uint8_t mask[InitialSecret::HPMaskLen];
  if (!trafficMask_(
	mask, sizeof(mask), state,
	ZuCSpan{reinterpret_cast<const char *>(out + pnOffset + 4), 16}))
    return -1;
  out[0] ^= mask[0] & firstMask;
  for (unsigned i = 0; i < pnLength; ++i)
    out[pnOffset + i] ^= mask[1 + i];
  return int(packetLen);
}

static int unprotect_(
  uint8_t *packet, unsigned len, PktProtState &state,
  uint64_t largestPN, unsigned pnOffset, uint64_t &pn,
  unsigned &payloadOffset, uint8_t firstMask)
{
  const TrafficSecret &secret = state.secret;
  if (!state.valid() || !secret.valid() || !state.aead.valid() ||
      !secret.aead || pnOffset + 4 + 16 > len)
    return -1;
  if (len > BufSize) return -1;
  uint8_t first = packet[0];
  uint8_t pnBytes[4];
  memcpy(pnBytes, packet + pnOffset, sizeof(pnBytes));
  auto fail = [&]() -> int {
    packet[0] = first;
    memcpy(packet + pnOffset, pnBytes, sizeof(pnBytes));
    return -1;
  };
  uint8_t mask[InitialSecret::HPMaskLen];
  if (!trafficMask_(
	mask, sizeof(mask), state,
	ZuCSpan{reinterpret_cast<const char *>(packet + pnOffset + 4), 16}))
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

  ptls_aead_context_t *ctx = state.aead.get();
  using Plain = ZtArray<
    uint8_t, ZtArrayHeapID<"Zquic.PktProt.Plain">>;
  unsigned cipherLen = len - payloadOffset;
  auto plain = ZtLocalArray(Plain, cipherLen, cipherLen);
  if (cipherLen && !plain) return fail();
  size_t n = ptls_aead_decrypt(
    ctx, plain.data(), packet + payloadOffset,
    len - payloadOffset, pn, packet, payloadOffset);
  if (n == SIZE_MAX) return fail();
  if (n > cipherLen) return fail();
  if (n) memcpy(packet + payloadOffset, plain.data(), n);
  return int(n);
}

int PktProt::protectLongV(
  uint8_t *out, unsigned len, PktProtState &state, uint64_t pn,
  ZuCSpan header, const ptls_iovec_t *plain, unsigned plainCount,
  unsigned pnOffset, unsigned pnLength)
{
  return protect_(out, len, state, pn, header, plain, plainCount,
    pnOffset, pnLength, 0x0f);
}

int PktProt::protectLong(
  uint8_t *out, unsigned len, PktProtState &state, uint64_t pn,
  ZuCSpan header, ZuCSpan plaintext, unsigned pnOffset, unsigned pnLength)
{
  ptls_iovec_t plain =
    ptls_iovec_init(plaintext.data(), plaintext.length());
  return protectLongV(out, len, state, pn, header, &plain, 1,
    pnOffset, pnLength);
}

int PktProt::protectLong(
  uint8_t *out, unsigned len, const TrafficSecret &secret, uint64_t pn,
  ZuCSpan header, ZuCSpan plaintext, unsigned pnOffset, unsigned pnLength)
{
  PktProtState state;
  if (!state.init(secret, CryptoLevel::Handshake, true)) return -1;
  return protectLong(out, len, state, pn, header, plaintext,
    pnOffset, pnLength);
}

int PktProt::unprotectLong(
  uint8_t *packet, unsigned len, PktProtState &state,
  uint64_t largestPN, unsigned pnOffset, uint64_t &pn,
  unsigned &payloadOffset)
{
  return unprotect_(packet, len, state, largestPN, pnOffset, pn,
    payloadOffset, 0x0f);
}

int PktProt::unprotectLong(
  uint8_t *packet, unsigned len, const TrafficSecret &secret,
  uint64_t largestPN, unsigned pnOffset, uint64_t &pn,
  unsigned &payloadOffset)
{
  PktProtState state;
  if (!state.init(secret, CryptoLevel::Handshake, false)) return -1;
  return unprotectLong(packet, len, state, largestPN, pnOffset, pn,
    payloadOffset);
}

int PktProt::protectShortV(
  uint8_t *out, unsigned len, PktProtState &state, uint64_t pn,
  ZuCSpan header, const ptls_iovec_t *plain, unsigned plainCount,
  unsigned pnOffset, unsigned pnLength)
{
  return protect_(out, len, state, pn, header, plain, plainCount,
    pnOffset, pnLength, 0x1f);
}

int PktProt::protectShort(
  uint8_t *out, unsigned len, PktProtState &state, uint64_t pn,
  ZuCSpan header, ZuCSpan plaintext, unsigned pnOffset, unsigned pnLength)
{
  ptls_iovec_t plain =
    ptls_iovec_init(plaintext.data(), plaintext.length());
  return protectShortV(out, len, state, pn, header, &plain, 1,
    pnOffset, pnLength);
}

int PktProt::protectShort(
  uint8_t *out, unsigned len, const TrafficSecret &secret, uint64_t pn,
  ZuCSpan header, ZuCSpan plaintext, unsigned pnOffset, unsigned pnLength)
{
  PktProtState state;
  if (!state.init(secret, CryptoLevel::OneRTT, true)) return -1;
  return protectShort(out, len, state, pn, header, plaintext,
    pnOffset, pnLength);
}

int PktProt::unprotectShort(
  uint8_t *packet, unsigned len, PktProtState &state,
  uint64_t largestPN, unsigned pnOffset, uint64_t &pn,
  unsigned &payloadOffset)
{
  return unprotect_(packet, len, state, largestPN, pnOffset, pn,
    payloadOffset, 0x1f);
}

int PktProt::unprotectShort(
  uint8_t *packet, unsigned len, const TrafficSecret &secret,
  uint64_t largestPN, unsigned pnOffset, uint64_t &pn,
  unsigned &payloadOffset)
{
  PktProtState state;
  if (!state.init(secret, CryptoLevel::OneRTT, false)) return -1;
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
  m_earlyDataEnabled = false;
  m_oneRTTReady = false;
  memset(m_secretInstalled, 0, sizeof(m_secretInstalled));
  for (auto &secret : m_txTrafficSecrets) secret.clear();
  for (auto &secret : m_rxTrafficSecrets) secret.clear();
  for (auto &state : m_txProt) state.clear();
  for (auto &state : m_rxProt) state.clear();
  m_alpn = ParamString{config.alpn};
  m_serverName = Host{config.serverName};
  m_localTransportParams = config.localTransportParams ?
    *config.localTransportParams : TransportParams{};
  m_peerTransportParams = {};
  m_peerTransportParamsReceived = false;
  if (config.enable0RTT) ++m_diag.zeroRTTRejected;
  return true;
}

bool Crypto::initTLS(const CryptoConfig &config)
{
  if (!init(config)) return false;
  if (!initTLSContext_(config)) {
    resetTLS_();
    return false;
  }
  if (!initTLSProperties_(config)) {
    resetTLS_();
    return false;
  }
  return true;
}

bool Crypto::initTLSContext_(const CryptoConfig &config)
{
  if (!Ztls::Backend::init()) return false;

  memset(&m_tlsCtx, 0, sizeof(m_tlsCtx));
  m_tlsCtx.random_bytes = ptls_openssl_random_bytes;
  m_tlsCtx.get_time = &ptls_get_time;
  m_tlsCtx.key_exchanges = ptls_openssl_key_exchanges;
  if (!initCipherSuites_(m_tlsCipherSuites, TLSMaxCiphers)) return false;
  m_tlsCtx.cipher_suites = m_tlsCipherSuites;
  m_tlsCtx.server_cipher_preference = 1;
  m_tlsCtx.max_early_data_size = 0;
  m_tlsCtx.omit_end_of_early_data = 1;

  static ptls_update_traffic_key_t updateTrafficKey{
    .cb = &Crypto::updateTrafficKeyCB_
  };
  m_tlsCtx.update_traffic_key = &updateTrafficKey;

  if (m_isServer) {
    if (!config.certPath || !config.keyPath) return false;
    if (!Ztls::Backend::load_certificates(&m_tlsCtx, config.certPath.data()))
      return false;
    m_key = Ztls::Backend::pkey_load_pem(config.keyPath.data());
    if (!m_key) return false;
    m_sign = Ztls::Backend::sign_cert_new(m_key);
    if (!m_sign) return false;
    m_tlsCtx.sign_certificate = Ztls::Backend::sign_cert_cb(m_sign);
    static ptls_on_client_hello_t onClientHello{
      .cb = &Crypto::onClientHelloCB_
    };
    m_tlsCtx.on_client_hello = &onClientHello;
  } else if (config.caPath) {
    m_certStore = Ztls::Backend::cert_store_new();
    if (!m_certStore) return false;
    bool ok = isDir_(config.caPath) ?
      Ztls::Backend::cert_store_load_path(
	m_certStore, config.caPath.data()) :
      Ztls::Backend::cert_store_load_file(
	m_certStore, config.caPath.data());
    if (!ok) return false;
    m_verify = Ztls::Backend::verify_cert_new(m_certStore);
    if (!m_verify) return false;
    m_tlsCtx.verify_certificate = Ztls::Backend::verify_cert_cb(m_verify);
  }

  m_tls = ptls_new(&m_tlsCtx, m_isServer ? 1 : 0);
  if (!m_tls) return false;
  *ptls_get_data_ptr(m_tls) = this;
  if (!m_isServer && m_serverName)
    if (ptls_set_server_name(m_tls, m_serverName.data(), m_serverName.length()))
      return false;
  return true;
}

bool Crypto::initTLSProperties_(const CryptoConfig &)
{
  memset(&m_tlsProps, 0, sizeof(m_tlsProps));
  unsigned transportParamsLen =
    m_localTransportParams.encodedLength();
  m_tlsTransportParams.length(transportParamsLen);
  int n = encodeTransportParams(
    m_tlsTransportParams.data(), m_tlsTransportParams.length(),
    m_localTransportParams);
  if (n < 0 || unsigned(n) != m_tlsTransportParams.length()) return false;
  m_tlsExtensions[0].type = TLSExtQUICTransportParamsV1;
  m_tlsExtensions[0].data =
    ptls_iovec_init(m_tlsTransportParams.data(), m_tlsTransportParams.length());
  m_tlsExtensions[1].type = UINT16_MAX;
  m_tlsExtensions[1].data = {};
  m_tlsProps.additional_extensions = m_tlsExtensions;
  m_tlsProps.collect_extension = &Crypto::collectExtensionCB_;
  m_tlsProps.collected_extensions = &Crypto::collectedExtensionsCB_;

  if (!m_isServer && m_alpn) {
    m_alpnVec = ptls_iovec_init(m_alpn.data(), m_alpn.length());
    m_tlsProps.client.negotiated_protocols.list = &m_alpnVec;
    m_tlsProps.client.negotiated_protocols.count = 1;
  }
  if (!m_isServer) {
    m_maxEarlyData = 0;
    m_tlsProps.client.max_early_data_size = &m_maxEarlyData;
    m_tlsProps.client.early_data_acceptance =
      PTLS_EARLY_DATA_ACCEPTANCE_UNKNOWN;
  }
  return true;
}

void Crypto::resetTLS_()
{
  if (m_tls) {
    ptls_free(m_tls);
    m_tls = nullptr;
  }
  freeCertificates_(m_tlsCtx);
  if (m_sign) {
    Ztls::Backend::sign_cert_free(m_sign);
    m_sign = nullptr;
  }
  if (m_key) {
    Ztls::Backend::pkey_free(m_key);
    m_key = nullptr;
  }
  if (m_verify) {
    Ztls::Backend::verify_cert_free(m_verify);
    m_verify = nullptr;
  }
  if (m_certStore) {
    Ztls::Backend::cert_store_free(m_certStore);
    m_certStore = nullptr;
  }
  memset(&m_tlsCtx, 0, sizeof(m_tlsCtx));
  memset(m_tlsCipherSuites, 0, sizeof(m_tlsCipherSuites));
  memset(&m_tlsProps, 0, sizeof(m_tlsProps));
  memset(m_tlsExtensions, 0, sizeof(m_tlsExtensions));
  m_tlsTransportParams.length(0);
  m_alpnVec = {};
  m_maxEarlyData = 0;
  m_tlsResult = PTLS_ERROR_IN_PROGRESS;
}

int Crypto::updateTrafficKeyCB_(
  ptls_update_traffic_key_t *, ptls_t *tls, int isEnc, size_t epoch,
  const void *secret)
{
  auto crypto = static_cast<Crypto *>(*ptls_get_data_ptr(tls));
  return crypto ? crypto->updateTrafficKey_(isEnc, epoch, secret) : 0;
}

int Crypto::onClientHelloCB_(
  ptls_on_client_hello_t *, ptls_t *tls,
  ptls_on_client_hello_parameters_t *params)
{
  auto crypto = static_cast<Crypto *>(*ptls_get_data_ptr(tls));
  return crypto ? crypto->onClientHello_(params) :
    PTLS_ALERT_TO_PEER_ERROR(PTLS_ALERT_INTERNAL_ERROR);
}

int Crypto::updateTrafficKey_(int isEnc, size_t epoch, const void *secret)
{
  if (!secret) return 0;
  CryptoLevel::T level =
    epoch == 2 ? CryptoLevel::Handshake :
    epoch >= 3 ? CryptoLevel::OneRTT : CryptoLevel::Initial;
  if (epoch < 2) return 0;

  ptls_cipher_suite_t *cipher = ptls_get_cipher(m_tls);
  if (!cipher || !cipher->hash) return -1;
  ZuCSpan secretSpan{
    reinterpret_cast<const char *>(secret),
    unsigned(cipher->hash->digest_size)};
  TrafficSecret &traffic =
    isEnc ? m_txTrafficSecrets[level] : m_rxTrafficSecrets[level];
  if (!PktProt::deriveTrafficSecret(traffic, cipher, secretSpan))
    return -1;
  PktProtState &state =
    isEnc ? m_txProt[level] : m_rxProt[level];
  if (!state.init(traffic, level, isEnc)) {
    traffic.clear();
    return -1;
  }
  installSecret(level, secretSpan);
  return 0;
}

int Crypto::onClientHello_(ptls_on_client_hello_parameters_t *params)
{
  if (!params) return PTLS_ALERT_TO_PEER_ERROR(PTLS_ALERT_INTERNAL_ERROR);
  if (params->server_name.base && params->server_name.len)
    ptls_set_server_name(
      m_tls, reinterpret_cast<const char *>(params->server_name.base),
      params->server_name.len);
  if (!m_alpn) return 0;
  for (size_t i = 0; i < params->negotiated_protocols.count; ++i) {
    auto protocol = params->negotiated_protocols.list[i];
    if (protocol.len != m_alpn.length() ||
	memcmp(protocol.base, m_alpn.data(), m_alpn.length()))
      continue;
    return ptls_set_negotiated_protocol(
      m_tls, m_alpn.data(), m_alpn.length());
  }
  return PTLS_ALERT_TO_PEER_ERROR(PTLS_ALERT_NO_APPLICATION_PROTOCOL);
}

int Crypto::collectExtensionCB_(
  ptls_t *, ptls_handshake_properties_t *, uint16_t type)
{
  return type == TLSExtQUICTransportParamsV1;
}

int Crypto::collectedExtensionsCB_(
  ptls_t *tls, ptls_handshake_properties_t *, ptls_raw_extension_t *extensions)
{
  auto crypto = static_cast<Crypto *>(*ptls_get_data_ptr(tls));
  return crypto ? crypto->collectedExtensions_(extensions) :
    PTLS_ALERT_TO_PEER_ERROR(PTLS_ALERT_INTERNAL_ERROR);
}

int Crypto::collectedExtensions_(ptls_raw_extension_t *extensions)
{
  if (!extensions)
    return PTLS_ALERT_TO_PEER_ERROR(PTLS_ALERT_MISSING_EXTENSION);
  for (; extensions->type != UINT16_MAX; ++extensions) {
    if (extensions->type != TLSExtQUICTransportParamsV1) continue;
    ZuCSpan in{
      reinterpret_cast<const char *>(extensions->data.base),
      unsigned(extensions->data.len)};
    if (decodeTransportParams(in, m_peerTransportParams) < 0)
      return PTLS_ALERT_TO_PEER_ERROR(PTLS_ALERT_DECODE_ERROR);
    m_peerTransportParamsReceived = true;
    return 0;
  }
  return PTLS_ALERT_TO_PEER_ERROR(PTLS_ALERT_MISSING_EXTENSION);
}

ZuCSpan Crypto::negotiatedProtocol() const
{
  if (!m_tls) return {};
  auto protocol = ptls_get_negotiated_protocol(m_tls);
  if (!protocol) return {};
  return ZuCSpan{protocol, unsigned(strlen(protocol))};
}

size_t Crypto::tlsReadEpoch() const
{
  return m_tls ? ptls_get_read_epoch(m_tls) : 0;
}

int Crypto::handleTLSMessage(
  ZiIOBuf *out, size_t epochOffsets[5], size_t inEpoch, ZuCSpan input)
{
  if (!m_tls || !out || !out->size || !epochOffsets) return -1;
  memset(epochOffsets, 0, sizeof(size_t) * 5);
  out->clear();
  ptls_buffer_t pbuf;
  ptls_buffer_init_tx(&pbuf, out->data_(), out->size);
  pbuf.origin = out;
  const void *inputData = input.length() ? input.data() : nullptr;
  int n = m_isServer ?
    ptls_server_handle_message(
      m_tls, &pbuf, epochOffsets, inEpoch,
      inputData, input.length(), &m_tlsProps) :
    ptls_client_handle_message(
      m_tls, &pbuf, epochOffsets, inEpoch,
      inputData, input.length(), &m_tlsProps);
  m_tlsResult = n;
  if (input.length()) ++m_diag.tlsMessagesHandled;
  if (pbuf.off) ++m_diag.tlsMessagesEmitted;
  bool ok =
    pbuf.base == out->data_() && pbuf.off <= out->size && !pbuf.is_allocated;
  unsigned outLen = ok ? unsigned(pbuf.off) : 0;
  if (ok) out->length = outLen;
  if (pbuf.is_allocated)
    ptls_buffer_dispose(&pbuf);
  if (!ok) return -1;
  if (!n && ptls_handshake_is_complete(m_tls)) m_oneRTTReady = true;
  if (n == PTLS_ERROR_IN_PROGRESS || n == PTLS_ERROR_ASYNC_OPERATION || !n)
    return int(outLen);
  return -1;
}

int Crypto::encodeTransportParams(
  uint8_t *out, unsigned len, const TransportParams &params)
{
  int n = params.encode(out, len);
  if (n >= 0) ++m_diag.transportParamsEncoded;
  return n;
}

int Crypto::decodeTransportParams(ZuCSpan in, TransportParams &params)
{
  int n = params.decode(in);
  if (!n) ++m_diag.transportParamsDecoded;
  return n;
}

void Crypto::installSecret(CryptoLevel::T level, ZuCSpan secret)
{
  ZiAssert(level >= CryptoLevel::Initial && level <= CryptoLevel::OneRTT,
    "Zquic", (), "invalid crypto level", return);
  ZiAssert(secret.length(), "Zquic", (),
    "empty traffic secret", return);
  if (!m_secretInstalled[level]) ++m_diag.secretsInstalled;
  m_secretInstalled[level] = true;
}

bool Crypto::discardSecret(CryptoLevel::T level)
{
  ZiAssert(level >= CryptoLevel::Initial && level <= CryptoLevel::OneRTT,
    "Zquic", (), "invalid crypto level", return false);
  if (!m_secretInstalled[level]) return false;
  m_secretInstalled[level] = false;
  if (level == CryptoLevel::Initial)
    memset(&m_initialKeys, 0, sizeof(m_initialKeys));
  m_txTrafficSecrets[level].clear();
  m_rxTrafficSecrets[level].clear();
  m_txProt[level].clear();
  m_rxProt[level].clear();
  ++m_diag.secretsDiscarded;
  return true;
}

bool Crypto::deriveInitial(const CxnID &dcid)
{
  if (!InitialCrypto::derive(m_initialKeys, dcid)) return false;
  ++m_diag.initialKeysDerived;
  return true;
}

bool Crypto::rejectZeroRTT()
{
  ++m_diag.zeroRTTRejected;
  return true;
}

bool Crypto::completeHandshake()
{
  m_oneRTTReady = m_secretInstalled[CryptoLevel::OneRTT];
  if (m_oneRTTReady) {
    discardSecret(CryptoLevel::Initial);
    discardSecret(CryptoLevel::Handshake);
  }
  return m_oneRTTReady;
}

} // namespace Zquic
