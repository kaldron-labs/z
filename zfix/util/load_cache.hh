//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef ZfixLoadCache_HH
#define ZfixLoadCache_HH

#include <zlib/ZmHash.hh>
#include <zlib/ZmList.hh>
#include <zlib/ZmObject.hh>

namespace ZfixLoad {

// Owner-local FIFO retention. One hash allocation carries the shadow FIFO
// links. Caller charges variable backing separately; lookup never moves a node.
template <typename K, typename V> class Cache {
  struct Record {
    K		key;
    V		value;
    uint64_t	bytes;
  };

  using FIFO = ZmList<Record, ZmListShadow<>>;

  static const K &key(const typename FIFO::Node &node) { return node.data().key; }

  using Map = ZmHash<typename FIFO::Node,
    ZmHashNode<typename FIFO::Node, ZmHashKey<key,
      ZmHashLock<ZmNoLock, ZmHashHeapID<"Zfix.Load.Cache.Map">>>>>;
  enum { NodeBytes = ZmHeapAllocSize<sizeof(typename Map::Node)>{} };
public:
  Cache(unsigned count, uint64_t bytes) : m_limit{count}, m_max{bytes} { }

  bool fits(uint64_t bytes) const {
    return m_limit && bytes <= m_max && NodeBytes <= m_max - bytes;
  }

  V *get(const K &key) {
    auto node = m_map.findPtr(key);
    return node ? &node->data().data().value : nullptr;
  }

  template <typename Fn> void each(Fn fn) const {
    typename FIFO::CIter i{m_fifo};
    while (auto node = i()) {
      const auto &record = node->data();
      fn(record.key, record.value);
    }
  }

  bool put(K key, V value, uint64_t bytes = 0) {
    if (!m_limit || m_map.findPtr(key) || bytes > UINT64_MAX - NodeBytes) return false;
    bytes += NodeBytes;
    if (bytes > m_max) return false;
    while (m_map.count_() >= m_limit || bytes > m_max - m_bytes) {
      auto first = m_fifo.headNode();
      erase(first->data().key);
    }
    auto node = m_map.add(Record{ZuMv(key), ZuMv(value), bytes});
    m_fifo.pushNode(node);
    m_bytes += bytes;
    if (m_bytes > m_peak) m_peak = m_bytes;
    return true;
  }

  bool erase(const K &key) {
    auto node = m_map.del(key);
    if (!node) return false;
    m_fifo.delNode(node.ptr());
    m_bytes -= node->data().data().bytes;
    return true;
  }

  void clear() { m_fifo.clean(); m_map.clean(); m_bytes = 0; }

  unsigned count() const { return m_map.count_(); }
  uint64_t bytes() const { return m_bytes; }
  uint64_t peak() const { return m_peak; }
private:
  const unsigned	m_limit;
  const uint64_t	m_max;
  Map			m_map;
  FIFO			m_fifo;
  uint64_t		m_bytes = 0, m_peak = 0;
};

} // ZfixLoad

#endif /* ZfixLoadCache_HH */
