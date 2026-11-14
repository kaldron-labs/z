//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Instance-owned Fast Open cookie authenticator and Rx-owned bounded cache.

#ifndef ZutcpTFO_HH
#define ZutcpTFO_HH

#ifndef ZutcpLib_HH
#include <zlib/ZutcpLib.hh>
#endif

#include <stdint.h>

#include <zlib/ZuSpan.hh>
#include <zlib/ZuTuple.hh>
#include <zlib/ZmHash.hh>
#include <zlib/ZmList.hh>
#include <zlib/ZmRef.hh>
#include <zlib/ZtArray.hh>
#include <zlib/ZiIP.hh>

namespace Zutcp {

enum { DefaultTFOCookieCacheSize = 1024 };

// Zero bytes represents a cookie request; the cache stores only actual cookies.
// Peer cookies are opaque; their size need not match our server's chosen size.
struct ZutcpAPI TFOCookie {
  uint8_t bytes[16] = {};
  uint8_t length = 0;

  static bool validLength(unsigned n) { return !n || (n >= 4 && n <= 16 && !(n & 1)); }
  bool assign(ZuBSpan);
  ZuBSpan span() const { return {bytes, length}; }
};

// Both address values carry their family. The client ephemeral port is absent.
using TFOCookieKey = ZuTuple<ZiIP, ZiSockAddr>; // selected client IP, server endpoint
ZutcpAPI bool tfoCookieKeyValid(const TFOCookieKey &);

class ZutcpAPI TFOKey {
friend struct TFOTest;
public:
  enum { SecretSize = 32, InputSize = 47, CookieSize = 16 };
  TFOKey() = default;
  TFOKey(const TFOKey &) = delete;
  TFOKey &operator =(const TFOKey &) = delete;
  ~TFOKey();

  // Initialize only once. The containing instance retains this key across stop/start.
  bool init();
  bool issue(const TFOCookieKey &, TFOCookie &) const;
  bool validate(const TFOCookieKey &, ZuBSpan) const;

private:
  bool initRandom(int (*random)(uint8_t *, int));
  bool init(ZuBSpan);
  static bool encode(const TFOCookieKey &, uint8_t *);

  uint8_t	m_secret[SecretSize] = {};
  bool		m_initialized = false;
};

struct TFOCacheEntry {
  TFOCookieKey key;
  TFOCookie cookie;
  unsigned freeNext = unsigned(-1);
};
inline const TFOCookieKey &tfoCacheKey(const TFOCacheEntry &entry) { return entry.key; }

// Rx-owned, including stopped-state resize. Hash and FIFO share preallocated slots.
// No cookie/connection references or timers are retained; find copies a Tx snapshot.
class ZutcpAPI TFOCache {
  ZmHashDerive(Index, TFOCacheEntry,
    (ZmHashNode<TFOCacheEntry,
      ZmHashKey<tfoCacheKey,
	ZmHashShadow<ZmHashLock<ZmNoLock, ZmHashHeapID<"">>>>>));
  ZmListDerive(Order, Index::Node,
    ZmListNode<Index::Node, ZmListKey<tfoCacheKey, ZmListShadow<>>>);
  using Slots = ZtArray<Order::Node, ZtArrayHeapID<"Zutcp.TFO.Slots">>;

public:
  explicit TFOCache(unsigned capacity = DefaultTFOCookieCacheSize);
  TFOCache(const TFOCache &) = delete;
  TFOCache &operator =(const TFOCache &) = delete;
  unsigned capacity() const { return unsigned(m_slots.length()); }
  unsigned count() const { return m_order.count_(); }
  bool store(const TFOCookieKey &, ZuBSpan);
  bool find(const TFOCookieKey &, TFOCookie &) const;
  bool erase(const TFOCookieKey &);
  bool resize(unsigned); // stopped; retain newest entries without changing order

private:
  Slots m_slots;
  ZmRef<Index> m_index;
  Order m_order;
  unsigned	m_free = unsigned(-1);
};

} // namespace Zutcp

#endif /* ZutcpTFO_HH */
