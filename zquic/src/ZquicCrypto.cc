//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZquicCrypto.hh>

#include <zlib/ZtlsBackend.hh>

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

static void initCipherSuites_(ptls_cipher_suite_t **suites)
{
  unsigned n = 0;
  for (auto p = ptls_openssl_cipher_suites; *p && n < 15; ++p)
    if ((*p)->aead && (*p)->aead->ecb_cipher)
      suites[n++] = *p;
  suites[n] = nullptr;
}

static void freeCertificates_(ptls_context_t &ctx)
{
  // ztls currently leaves this ownership to picotls process lifetime; this
  // zpicotls build does not expose a stable per-certificate free contract.
  ctx.certificates.list = nullptr;
  ctx.certificates.count = 0;
}

static bool isDir_(ZuCSpan path)
{
  if (!path) return false;
  struct stat st;
  return !stat(path.data(), &st) && S_ISDIR(st.st_mode);
}

} // namespace

int CryptoStream::writeFrame(
  uint8_t *out, unsigned len, ZuCSpan payload, CryptoDiag *diag)
{
  int n = FrameCodec::writeCrypto(out, len, m_txOffset, payload);
  if (n < 0) return -1;
  m_txOffset += payload.length();
  if (diag) {
    ++diag->cryptoFramesTx;
    diag->cryptoBytesTx += payload.length();
  }
  return n;
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
  uint64_t end = offset + payload.length();
  if (end < offset || end > MaxBuffered) return -1;
  if (diag) ++diag->cryptoFramesRx;
  if (!payload.length() || end <= m_rxOffset) return 0;
  memcpy(m_rx + offset, payload.data(), payload.length());
  uint64_t first = offset < m_rxOffset ? m_rxOffset : offset;
  if (!insert_(first, end)) return -1;
  if (diag) diag->cryptoBytesRx += end - first;
  deliver_(contiguous);
  return 0;
}

bool CryptoStream::insert_(uint64_t first, uint64_t last)
{
  if (last <= m_rxOffset) return true;
  if (first < m_rxOffset) first = m_rxOffset;
  for (unsigned i = 0; i < m_rangeCount; ++i)
    if (first >= m_ranges[i].first && last <= m_ranges[i].last)
      return true;
  if (m_rangeCount >= MaxRanges) return false;
  m_ranges[m_rangeCount++] = Range{first, last};
  for (unsigned i = 1; i < m_rangeCount; ++i) {
    Range r = m_ranges[i];
    unsigned j = i;
    while (j && r.first < m_ranges[j - 1].first) {
      m_ranges[j] = m_ranges[j - 1];
      --j;
    }
    m_ranges[j] = r;
  }
  unsigned out = 0;
  for (unsigned i = 0; i < m_rangeCount; ++i) {
    if (out && m_ranges[i].first <= m_ranges[out - 1].last) {
      if (m_ranges[i].last > m_ranges[out - 1].last)
	m_ranges[out - 1].last = m_ranges[i].last;
    } else
      m_ranges[out++] = m_ranges[i];
  }
  m_rangeCount = out;
  return true;
}

void CryptoStream::deliver_(ZuCSpan &contiguous)
{
  contiguous = {};
  if (!m_rangeCount || m_ranges[0].first > m_rxOffset) return;
  uint64_t first = m_rxOffset;
  uint64_t last = m_ranges[0].last;
  if (last <= first) return;
  contiguous = ZuCSpan{
    reinterpret_cast<const char *>(m_rx + first), unsigned(last - first)};
  m_rxOffset = last;
  for (unsigned i = 1; i < m_rangeCount; ++i) m_ranges[i - 1] = m_ranges[i];
  --m_rangeCount;
}

bool InitialCrypto::derive(InitialKeyMaterial &out, const ConnectionID &dcid)
{
  out = {};
  if (!hkdfExtract_(out.initial, InitialSaltV1_, sizeof(InitialSaltV1_),
	dcid.cspan()))
    return false;
  return deriveSecret_(out.client, out.initial, "client in") &&
    deriveSecret_(out.server, out.initial, "server in");
}

int InitialCrypto::encrypt(
  uint8_t *out, unsigned len, const InitialSecret &secret, uint64_t pn,
  ZuCSpan aad, ZuCSpan plaintext)
{
  if (len < plaintext.length() + InitialSecret::TagLen) return -1;
  uint8_t nonce[InitialSecret::IVLen];
  nonce_(nonce, secret, pn);

  EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
  if (!ctx) return -1;
  int n = 0, off = 0;
  bool ok =
    EVP_EncryptInit_ex(ctx, EVP_aes_128_gcm(), nullptr, nullptr, nullptr) == 1 &&
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, sizeof(nonce), nullptr) == 1 &&
    EVP_EncryptInit_ex(ctx, nullptr, nullptr, secret.key, nonce) == 1 &&
    (!aad.length() || EVP_EncryptUpdate(ctx, nullptr, &n,
      reinterpret_cast<const uint8_t *>(aad.data()), aad.length()) == 1) &&
    (!plaintext.length() || EVP_EncryptUpdate(ctx, out, &n,
      reinterpret_cast<const uint8_t *>(plaintext.data()), plaintext.length()) == 1);
  if (ok) off = n;
  ok = ok && EVP_EncryptFinal_ex(ctx, out + off, &n) == 1;
  if (ok) off += n;
  ok = ok && EVP_CIPHER_CTX_ctrl(
    ctx, EVP_CTRL_GCM_GET_TAG, InitialSecret::TagLen, out + off) == 1;
  EVP_CIPHER_CTX_free(ctx);
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

  EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
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
  EVP_CIPHER_CTX_free(ctx);
  return ok ? off : -1;
}

bool InitialCrypto::headerMask(
  uint8_t *out, unsigned len, const InitialSecret &secret, ZuCSpan sample)
{
  if (len < InitialSecret::HPMaskLen || sample.length() < 16) return false;
  uint8_t block[32];
  EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
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
  EVP_CIPHER_CTX_free(ctx);
  return ok;
}

int InitialPacketProtection::protectLong(
  uint8_t *out, unsigned len, const InitialSecret &secret, uint64_t pn,
  ZuCSpan header, ZuCSpan plaintext, unsigned pnOffset, unsigned pnLength)
{
  if (pnLength < 1 || pnLength > 4 ||
      header.length() != pnOffset + pnLength)
    return -1;
  unsigned packetLen = header.length() +
    plaintext.length() + InitialSecret::TagLen;
  if (len < packetLen || packetLen < pnOffset + 4 + 16) return -1;
  memcpy(out, header.data(), header.length());
  int n = InitialCrypto::encrypt(
    out + header.length(), len - header.length(), secret, pn,
    ZuCSpan{reinterpret_cast<const char *>(out), header.length()}, plaintext);
  if (n != int(plaintext.length() + InitialSecret::TagLen)) return -1;

  uint8_t mask[InitialSecret::HPMaskLen];
  if (!InitialCrypto::headerMask(
	mask, sizeof(mask), secret,
	ZuCSpan{reinterpret_cast<const char *>(out + pnOffset + 4), 16}))
    return -1;
  out[0] ^= mask[0] & 0x0f;
  for (unsigned i = 0; i < pnLength; ++i) out[pnOffset + i] ^= mask[1 + i];
  return int(packetLen);
}

int InitialPacketProtection::unprotectLong(
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
  pn = PacketNumber::decode(largestPN, truncated, pnLength * 8);
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

bool PacketProtection::deriveTrafficSecret(
  TrafficSecret &out, ptls_cipher_suite_t *cipher, ZuCSpan secret)
{
  out.clear();
  if (!cipher || !cipher->aead || !cipher->hash ||
      !cipher->aead->ecb_cipher || !secret)
    return false;

  unsigned secretLen = cipher->hash->digest_size;
  unsigned keyLen = cipher->aead->key_size;
  unsigned ivLen = cipher->aead->iv_size;
  if (ivLen < 8) ivLen = 8;
  unsigned hpLen = cipher->aead->ecb_cipher->key_size;
  unsigned tagLen = cipher->aead->tag_size;
  if (secret.length() != secretLen ||
      secretLen > TrafficSecret::MaxSecretLen ||
      keyLen > TrafficSecret::MaxKeyLen ||
      ivLen > TrafficSecret::MaxIVLen ||
      hpLen > TrafficSecret::MaxHPLen ||
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
  out.installed = true;
  return true;
}

static bool trafficMask_(
  uint8_t *mask, unsigned len, const TrafficSecret &secret, ZuCSpan sample)
{
  if (len < InitialSecret::HPMaskLen ||
      !secret.valid() || !secret.hpCipher || sample.length() < 16)
    return false;
  ptls_cipher_context_t *ctx =
    ptls_cipher_new(secret.hpCipher, 1, secret.hp);
  if (!ctx) return false;
  uint8_t block[32];
  ptls_cipher_init(ctx, nullptr);
  ptls_cipher_encrypt(ctx, block, sample.data(), 16);
  ptls_cipher_free(ctx);
  memcpy(mask, block, InitialSecret::HPMaskLen);
  return true;
}

static int protect_(
  uint8_t *out, unsigned len, const TrafficSecret &secret, uint64_t pn,
  ZuCSpan header, ZuCSpan plaintext, unsigned pnOffset, unsigned pnLength,
  uint8_t firstMask)
{
  if (!secret.valid() || !secret.aead ||
      pnLength < 1 || pnLength > 4 ||
      header.length() != pnOffset + pnLength)
    return -1;
  unsigned packetLen = header.length() + plaintext.length() + secret.tagLen;
  if (len < packetLen || packetLen < pnOffset + 4 + 16) return -1;
  memcpy(out, header.data(), header.length());

  ptls_aead_context_t *ctx =
    ptls_aead_new_direct(secret.aead, 1, secret.key, secret.iv);
  if (!ctx) return -1;
  size_t n = ptls_aead_encrypt(
    ctx, out + header.length(), plaintext.data(), plaintext.length(), pn,
    out, header.length());
  ptls_aead_free(ctx);
  if (n != plaintext.length() + secret.tagLen) return -1;

  uint8_t mask[InitialSecret::HPMaskLen];
  if (!trafficMask_(
	mask, sizeof(mask), secret,
	ZuCSpan{reinterpret_cast<const char *>(out + pnOffset + 4), 16}))
    return -1;
  out[0] ^= mask[0] & firstMask;
  for (unsigned i = 0; i < pnLength; ++i) out[pnOffset + i] ^= mask[1 + i];
  return int(packetLen);
}

static int unprotect_(
  uint8_t *packet, unsigned len, const TrafficSecret &secret,
  uint64_t largestPN, unsigned pnOffset, uint64_t &pn,
  unsigned &payloadOffset, uint8_t firstMask)
{
  if (!secret.valid() || !secret.aead || pnOffset + 4 + 16 > len) return -1;
  uint8_t mask[InitialSecret::HPMaskLen];
  if (!trafficMask_(
	mask, sizeof(mask), secret,
	ZuCSpan{reinterpret_cast<const char *>(packet + pnOffset + 4), 16}))
    return -1;
  packet[0] ^= mask[0] & firstMask;
  unsigned pnLength = (packet[0] & 0x03) + 1;
  if (pnLength < 1 || pnLength > 4 || pnOffset + pnLength > len)
    return -1;
  uint64_t truncated = 0;
  for (unsigned i = 0; i < pnLength; ++i) {
    packet[pnOffset + i] ^= mask[1 + i];
    truncated = (truncated << 8) | packet[pnOffset + i];
  }
  pn = PacketNumber::decode(largestPN, truncated, pnLength * 8);
  payloadOffset = pnOffset + pnLength;

  ptls_aead_context_t *ctx =
    ptls_aead_new_direct(secret.aead, 0, secret.key, secret.iv);
  if (!ctx) return -1;
  size_t n = ptls_aead_decrypt(
    ctx, packet + payloadOffset, packet + payloadOffset,
    len - payloadOffset, pn, packet, payloadOffset);
  ptls_aead_free(ctx);
  return n == SIZE_MAX ? -1 : int(n);
}

int PacketProtection::protectLong(
  uint8_t *out, unsigned len, const TrafficSecret &secret, uint64_t pn,
  ZuCSpan header, ZuCSpan plaintext, unsigned pnOffset, unsigned pnLength)
{
  return protect_(out, len, secret, pn, header, plaintext,
    pnOffset, pnLength, 0x0f);
}

int PacketProtection::unprotectLong(
  uint8_t *packet, unsigned len, const TrafficSecret &secret,
  uint64_t largestPN, unsigned pnOffset, uint64_t &pn,
  unsigned &payloadOffset)
{
  return unprotect_(packet, len, secret, largestPN, pnOffset, pn,
    payloadOffset, 0x0f);
}

int PacketProtection::protectShort(
  uint8_t *out, unsigned len, const TrafficSecret &secret, uint64_t pn,
  ZuCSpan header, ZuCSpan plaintext, unsigned pnOffset, unsigned pnLength)
{
  return protect_(out, len, secret, pn, header, plaintext,
    pnOffset, pnLength, 0x1f);
}

int PacketProtection::unprotectShort(
  uint8_t *packet, unsigned len, const TrafficSecret &secret,
  uint64_t largestPN, unsigned pnOffset, uint64_t &pn,
  unsigned &payloadOffset)
{
  return unprotect_(packet, len, secret, largestPN, pnOffset, pn,
    payloadOffset, 0x1f);
}

Crypto::~Crypto()
{
  resetTLS_();
}

bool Crypto::init(const CryptoConfig &config)
{
  resetTLS_();
  if (config.alpn.length() > sizeof(m_alpn)) return false;
  if (config.serverName.length() > sizeof(m_serverName)) return false;
  m_isServer = config.isServer;
  m_earlyDataEnabled = false;
  m_oneRTTReady = false;
  memset(m_secretInstalled, 0, sizeof(m_secretInstalled));
  for (auto &secret : m_txTrafficSecrets) secret.clear();
  for (auto &secret : m_rxTrafficSecrets) secret.clear();
  m_alpnLength = config.alpn.length();
  if (m_alpnLength) memcpy(m_alpn, config.alpn.data(), m_alpnLength);
  m_serverNameLength = config.serverName.length();
  if (m_serverNameLength)
    memcpy(m_serverName, config.serverName.data(), m_serverNameLength);
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
  initCipherSuites_(m_tlsCipherSuites);
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
  if (!m_isServer && m_serverNameLength)
    if (ptls_set_server_name(
	  m_tls, reinterpret_cast<const char *>(m_serverName),
	  m_serverNameLength))
      return false;
  return true;
}

bool Crypto::initTLSProperties_(const CryptoConfig &)
{
  memset(&m_tlsProps, 0, sizeof(m_tlsProps));
  int n = encodeTransportParams(
    m_tlsTransportParams, sizeof(m_tlsTransportParams), m_localTransportParams);
  if (n < 0) return false;
  m_tlsTransportParamsLen = unsigned(n);
  m_tlsExtensions[0].type = TLSExtQUICTransportParamsV1;
  m_tlsExtensions[0].data =
    ptls_iovec_init(m_tlsTransportParams, m_tlsTransportParamsLen);
  m_tlsExtensions[1].type = UINT16_MAX;
  m_tlsExtensions[1].data = {};
  m_tlsProps.additional_extensions = m_tlsExtensions;
  m_tlsProps.collect_extension = &Crypto::collectExtensionCB_;
  m_tlsProps.collected_extensions = &Crypto::collectedExtensionsCB_;

  if (!m_isServer && m_alpnLength) {
    m_alpnVec = ptls_iovec_init(m_alpn, m_alpnLength);
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
  memset(m_tlsTransportParams, 0, sizeof(m_tlsTransportParams));
  m_tlsTransportParamsLen = 0;
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
  if (!PacketProtection::deriveTrafficSecret(traffic, cipher, secretSpan))
    return -1;
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
  if (!m_alpnLength) return 0;
  for (size_t i = 0; i < params->negotiated_protocols.count; ++i) {
    auto protocol = params->negotiated_protocols.list[i];
    if (protocol.len != m_alpnLength ||
	memcmp(protocol.base, m_alpn, m_alpnLength))
      continue;
    return ptls_set_negotiated_protocol(
      m_tls, reinterpret_cast<const char *>(m_alpn), m_alpnLength);
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
  uint8_t *out, unsigned len, size_t epochOffsets[5],
  size_t inEpoch, ZuCSpan input)
{
  if (!m_tls || !out || !len || !epochOffsets) return -1;
  memset(epochOffsets, 0, sizeof(size_t) * 5);
  ptls_buffer_t pbuf;
  ptls_buffer_init_tx(&pbuf, out, len);
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
  bool ok = pbuf.off <= len;
  unsigned outLen = ok ? unsigned(pbuf.off) : 0;
  if (ok && pbuf.base != out && pbuf.off)
    memcpy(out, pbuf.base, pbuf.off);
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
  int n = TransportParamsCodec::encode(out, len, params);
  if (n >= 0) ++m_diag.transportParamsEncoded;
  return n;
}

int Crypto::decodeTransportParams(ZuCSpan in, TransportParams &params)
{
  int n = TransportParamsCodec::decode(in, params);
  if (!n) ++m_diag.transportParamsDecoded;
  return n;
}

void Crypto::installSecret(CryptoLevel::T level, ZuCSpan secret)
{
  ZiAssert(level >= CryptoLevel::Initial && level <= CryptoLevel::OneRTT,
    "Zquic", (level), "invalid crypto level", return);
  ZiAssert(secret.length(), "Zquic", (level),
    "empty traffic secret", return);
  if (!m_secretInstalled[level]) ++m_diag.secretsInstalled;
  m_secretInstalled[level] = true;
}

bool Crypto::discardSecret(CryptoLevel::T level)
{
  ZiAssert(level >= CryptoLevel::Initial && level <= CryptoLevel::OneRTT,
    "Zquic", (level), "invalid crypto level", return false);
  if (!m_secretInstalled[level]) return false;
  m_secretInstalled[level] = false;
  if (level == CryptoLevel::Initial)
    memset(&m_initialKeys, 0, sizeof(m_initialKeys));
  m_txTrafficSecrets[level].clear();
  m_rxTrafficSecrets[level].clear();
  ++m_diag.secretsDiscarded;
  return true;
}

bool Crypto::deriveInitial(const ConnectionID &dcid)
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
