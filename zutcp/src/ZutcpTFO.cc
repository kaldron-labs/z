//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>

#include <zlib/ZmAssert.hh>
#include <zlib/ZutcpTFO.hh>

namespace Zutcp {

bool TFOCookie::assign(ZuBSpan input)
{
  if (!validLength(input.length())) return false;
  TFOCookie next;
  if (input.length()) memcpy(next.bytes, input.data(), input.length());
  next.length = uint8_t(input.length());
  *this = next;
  return true;
}

bool tfoCookieKeyValid(const TFOCookieKey &key)
{
  auto family = key.p<0>().type();
  return (family == ZiIPType::V4 || family == ZiIPType::V6) &&
    key.p<1>().type() == family && key.p<1>().port();
}

TFOKey::~TFOKey()
{
  OPENSSL_cleanse(m_secret, sizeof(m_secret));
}

bool TFOKey::init()
{
  return initRandom(RAND_priv_bytes);
}

bool TFOKey::initRandom(int (*random)(uint8_t *, int))
{
  if (m_initialized) return false;
  uint8_t secret[SecretSize];
  bool ok = random(secret, sizeof(secret)) == 1 && init({secret, sizeof(secret)});
  OPENSSL_cleanse(secret, sizeof(secret));
  return ok;
}

bool TFOKey::init(ZuBSpan secret)
{
  if (m_initialized || secret.length() != SecretSize) return false;
  memcpy(m_secret, secret.data(), SecretSize);
  m_initialized = true;
  return true;
}

bool TFOKey::encode(const TFOCookieKey &key, uint8_t *out)
{
  if (!tfoCookieKeyValid(key)) return false;
  const auto &client = key.p<0>();
  const auto &server = key.p<1>();
  memcpy(out, "Zutcp.TFO.v1", 12);
  out[12] = client.type() == ZiIPType::V4 ? 4 : 6;
  if (client.type() == ZiIPType::V4) {
    memset(out + 13, 0, 12);
    memcpy(out + 25, &client.inAddr(), 4);
    memset(out + 29, 0, 12);
    memcpy(out + 41, &server.sin().sin_addr, 4);
  } else {
    memcpy(out + 13, &client.in6Addr(), 16);
    memcpy(out + 29, &server.sin6().sin6_addr, 16);
  }
  out[45] = uint8_t(server.port()>>8);
  out[46] = uint8_t(server.port());
  return true;
}

bool TFOKey::issue(const TFOCookieKey &key, TFOCookie &out) const
{
  if (!m_initialized) return false;
  uint8_t input[InputSize], digest[SecretSize];
  if (!encode(key, input)) return false;
  unsigned length = 0;
  bool ok = HMAC(EVP_sha256(), m_secret, SecretSize, input, sizeof(input), digest, &length) &&
    length == sizeof(digest);
  if (ok) {
    TFOCookie next;
    memcpy(next.bytes, digest, CookieSize);
    next.length = CookieSize;
    out = next;
  }
  OPENSSL_cleanse(digest, sizeof(digest));
  return ok;
}

bool TFOKey::validate(const TFOCookieKey &key, ZuBSpan cookie) const
{
  if (cookie.length() != CookieSize) return false;
  TFOCookie expected;
  return issue(key, expected) && !CRYPTO_memcmp(expected.bytes, cookie.data(), CookieSize);
}

TFOCache::TFOCache(unsigned capacity)
: m_index{new Index{ZmHashParams{capacity}.cBits(0).loadFactor(1)}}
{
  ZmAssert_(capacity);
  m_slots.length(capacity);
  for (unsigned i = 0; i < capacity; ++i)
    m_slots[i].freeNext = i + 1 < capacity ? i + 1 : unsigned(-1);
  m_free = 0;
}

bool TFOCache::store(const TFOCookieKey &key, ZuBSpan input)
{
  TFOCookie cookie;
  if (!input.length() || !tfoCookieKeyValid(key) || !cookie.assign(input)) return false;
  if (auto found = m_index->findPtr(key)) {
    found->data().cookie = cookie;
    return true; // Updates do not refresh insertion FIFO order.
  }
  Order::Node *node;
  if (m_free != unsigned(-1)) {
    node = &m_slots[m_free];
    m_free = node->freeNext;
  } else {
    node = m_order.shift();
    m_index->delNode(node);
  }
  static_cast<TFOCacheEntry &>(*node) = {key, cookie};
  m_index->addNode(node);
  m_order.pushNode(node);
  return true;
}

bool TFOCache::find(const TFOCookieKey &key, TFOCookie &cookie) const
{
  if (!tfoCookieKeyValid(key)) return false;
  auto found = m_index->findPtr(key);
  if (!found) return false;
  cookie = found->data().cookie;
  return true;
}

bool TFOCache::erase(const TFOCookieKey &key)
{
  auto found = m_index->findPtr(key);
  if (!found) return false;
  auto node = static_cast<Order::Node *>(found);
  m_order.delNode(node);
  m_index->delNode(node);
  node->cookie = {};
  node->freeNext = m_free;
  m_free = unsigned(node - m_slots.data());
  return true;
}

bool TFOCache::resize(unsigned capacity)
{
  if (!capacity) return false;
  if (capacity == m_slots.length()) return true;
  TFOCache next{capacity};
  unsigned count = m_order.count_();
  unsigned skip = count > capacity ? count - capacity : 0;
  auto iter = m_order.citer();
  while (auto node = iter()) {
    if (skip) { --skip; continue; }
    const auto &entry = static_cast<const TFOCacheEntry &>(*node);
    next.store(entry.key, entry.cookie.span());
  }
  m_order.clean();
  m_index->clean();
  m_order = ZuMv(next.m_order);
  m_index = ZuMv(next.m_index);
  m_slots = ZuMv(next.m_slots);
  m_free = next.m_free;
  return true;
}

} // namespace Zutcp
