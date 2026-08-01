//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// LRU cache of ZuStruct objects (combination of ZmList and ZmPolyHash)

#ifndef ZmPolyCache_HH
#define ZmPolyCache_HH

#ifndef ZmLib_HH
#include <zlib/ZmLib.hh>
#endif

#include <zlib/ZuID.hh>
#include <zlib/ZuStruct.hh>

#include <zlib/ZmLockTraits.hh>
#include <zlib/ZmPLock.hh>
#include <zlib/ZmGuard.hh>
#include <zlib/ZmScratch.hh>
#include <zlib/ZmList.hh>
#include <zlib/ZmPolyHash.hh>
#include <zlib/ZmBlock.hh>
#include <zlib/ZmCacheStats.hh>

// NTP defaults
struct ZmPolyCache_Defaults : public ZmPolyHash_Defaults {
  struct HeapID : public ZuStringT<"ZmPolyCache"> { };
  using ID = HeapID;
  enum { Evict = 1 };
};

// most NTP parameters are identical to ZmPolyHash
template <typename Lock, typename NTP = ZmPolyCache_Defaults>
using ZmPolyCacheLock = ZmPolyHashLock<Lock, NTP>;
template <bool Shadow, typename NTP = ZmPolyCache_Defaults>
using ZmPolyCacheShadow_ = ZmPolyHashShadow_<Shadow, NTP>;
template <typename NTP = ZmPolyCache_Defaults>
using ZmPolyCacheShadow = ZmPolyHashShadow<NTP>;
template <typename HeapID, typename NTP = ZmPolyCache_Defaults>
using ZmPolyCacheHeapID_ = ZmPolyHashHeapID_<HeapID, NTP>;
template <ZuString HeapID, typename NTP = ZmPolyCache_Defaults>
using ZmPolyCacheHeapID = ZmPolyHashHeapID_<ZuStringT<HeapID>, NTP>;
template <bool Sharded, typename NTP = ZmPolyCache_Defaults>
using ZmPolyCacheSharded = ZmPolyHashSharded<Sharded, NTP>;

// ZmPolyCacheEvict - enable/disable eviction
template <bool Evict_, typename NTP = ZmPolyCache_Defaults>
struct ZmPolyCacheEvict : public NTP {
  enum { Evict = Evict_ };
};

template <typename T_, typename NTP = ZmPolyCache_Defaults>
class ZmPolyCache {
public:
  using T = T_;
  using Lock = typename NTP::Lock;
  enum { Evict = NTP::Evict };

private:
  using Guard = ZmGuard<Lock>;
  using ReadGuard = ZmReadGuard<Lock>;
  using LRUList = ZmList<T, ZmListNode<T, ZmListShadow<>>>;
  struct LRUDisable { // LRU list is not needed if eviction is disabled
    using Node = T;
    Node *delNode(Node *node) { return nullptr; }
    Node *shift() { return nullptr; }
    void pushNode(Node *) { }
  };
  using LRU = ZuIf<Evict, LRUList, LRUDisable>;
  using PolyHash =
    ZmPolyHash<typename LRU::Node,
      ZmPolyHashLock<ZmNoLock, NTP>>; // overrides NTP::Lock

public:
  using HeapID = PolyHash::HeapID;
  enum { Sharded = PolyHash::Sharded };

  using Node = typename PolyHash::Node;
  using NodeRef = typename PolyHash::NodeRef;
  using NodeMvRef = typename PolyHash::NodeMvRef;

private:
  using FindFn = ZmFn<void(Node *),
    ZmFnHeapID<HeapID{}() + ".FindFn"_Zu, ZmFnSharded<Sharded>>>;
  using FindFnList = ZmList<FindFn, ZmHashHeapID_<HeapID>>;
  // key IDs as a type list
  using KeyIDs = ZuSeqTL<ZuStructKeyIDs<T>>;
  // key types, each a tuple
  template <int KeyID> using Key = ZuStructKeyT<T, KeyID>;
  template <typename KeyID> using KeyT = ZuStructKeyT<T, KeyID{}>;
  using Keys = ZuTypeMap<KeyT, KeyIDs>;
  // load hash tables, mapping keys to pending find() operations for each KeyID
  template <typename KeyID>
  using LoadHash = ZmHashKV<KeyT<KeyID>, FindFnList, ZmHashHeapID_<HeapID>>;
  // hash table node type
  template <int KeyID>
  using LoadHashNode = typename LoadHash<ZuUnsigned<KeyID>>::Node;
  // type list of load hash tables
  using LoadHashTL = ZuTypeMap<LoadHash, KeyIDs>;
  // tuple of load hash tables
  using LoadHashes = ZuTypeApply<ZuTuple, ZuTypeMap<ZmRef, LoadHashTL>>;

public:
  ZmPolyCache() : m_size(0) { }

  ZmPolyCache(ZuCSpan id) : m_hash{id} {
    if constexpr (KeyIDs::N) {
      ZmAssert(id.length() + 5 +
	ZuBoxed(unsigned(ZuType<KeyIDs::N - 1, KeyIDs>{})).length() <
	ZuIDSize);
    }
    m_size = m_hash.size();
    auto params = ZmHashParams{id};
    ZuUnroll::all<KeyIDs>([this, &id, &params]<typename KeyID>() {
      ZuID loadHashID = id;
      loadHashID << ".ld." << ZuBoxed(KeyID{}());
      m_loadHashes.template p<KeyID{}>(
	new ZuType<KeyID{}, LoadHashTL>{loadHashID, params});
    });
  }

  ZmPolyCache(ZmPolyCache &) = delete;
  ZmPolyCache &operator =(ZmPolyCache &) = delete;
  // the move operators are intentionally unlocked
  // - these are exclusively intended for use in initialization
  ZmPolyCache(ZmPolyCache &&c) :
    m_size{c.m_size},
    m_hash{ZuMv(c.m_hash)},
    m_lru{ZuMv(c.m_lru)},
    m_loadHashes{ZuMv(c.m_loadHashes)},
    m_loads{c.m_loads},
    m_misses{c.m_misses},
    m_evictions{c.m_evictions}
  {
    c.m_size = 0;
    c.m_loads = c.m_misses = c.m_evictions = 0;
  }
  ZmPolyCache &operator =(ZmPolyCache &&c) {
    this->~ZmPolyCache();
    new (this) ZmPolyCache{ZuMv(c)};
    return *this;
  }

  unsigned size() const { return m_size; }

  using Stats = ZmCacheStats;

private:
  void stats_(Stats &r) const {
    r.size = m_hash.size();
    r.count = m_hash.count_();
    r.loads = m_loads;
    r.misses = m_misses;
    r.evictions = m_evictions;
  }
public:
  template <bool Reset = false>
  ZuIfT<!Reset> stats(Stats &r) const {
    ReadGuard guard{m_lock};
    stats_(r);
  }
  template <bool Reset = false>
  ZuIfT<Reset> stats(Stats &r) {
    Guard guard{m_lock};
    stats_(r);
    m_loads = m_misses = m_evictions = 0;
  }

  template <int KeyID = 0, bool UpdateLRU = Evict, typename Key>
  NodeRef find(const Key &key) {
    Guard guard{m_lock};
    ++m_loads;
    if (NodeRef node = find_<KeyID, UpdateLRU>(key)) return node;
    ++m_misses;
    return nullptr;
  }

  template <
    int KeyID = 0,
    bool UpdateLRU = Evict, bool Evict_ = Evict,
    typename FindFn_, typename LoadFn>
  void find(Key<KeyID> key, FindFn_ findFn, LoadFn loadFn) {
    Guard guard{m_lock};
    ++m_loads;
    if (NodeRef node = find_<KeyID, UpdateLRU>(key)) {
      findFn(ZuMv(node));
      return;
    }
    ++m_misses;
    const auto &loadHash = m_loadHashes.template p<KeyID>();
    LoadHashNode<KeyID> *load = loadHash->find(key);
    bool pending = load;
    if (!pending)
      loadHash->addNode(load = new LoadHashNode<KeyID>{key, FindFnList{}});
    load->val().push(FindFn{ZuMv(findFn)});
    guard.unlock();
    if (!pending)
      loadFn(ZuMv(key), [this, key](NodeRef node) {
	Guard guard{m_lock};
	if (node) add_<Evict_>(node);
	const auto &loadHash = m_loadHashes.template p<KeyID>();
	if (auto load = loadHash->del(key)) {
	  guard.unlock();
	  while (auto findFn = load->val().shiftVal()) findFn(node);
	}
      });
  }

  template <
    int KeyID = 0,
    bool UpdateLRU = Evict,
    typename FindFn_, typename LoadFn, typename EvictFn>
  void find(Key<KeyID> key, FindFn_ findFn, LoadFn loadFn, EvictFn evictFn) {
    Guard guard{m_lock};
    ++m_loads;
    if (NodeRef node = find_<KeyID, UpdateLRU>(key)) {
      findFn(ZuMv(node)); return;
    }
    ++m_misses;
    const auto &loadHash = m_loadHashes.template p<KeyID>();
    LoadHashNode<KeyID> *load = loadHash->find(key);
    bool pending = load;
    if (!pending)
      loadHash->addNode(load = new LoadHashNode<KeyID>{key, FindFnList{}});
    load->val().push(FindFn{ZuMv(findFn)});
    guard.unlock();
    if (!pending)
      loadFn(key, [this, key, evictFn = ZuMv(evictFn)](NodeRef node) {
	Guard guard{m_lock};
	if (node) add_<true>(node, ZuMv(evictFn));
	const auto &loadHash = m_loadHashes.template p<KeyID>();
	if (auto load = loadHash->del(key)) {
	  guard.unlock();
	  while (auto findFn = load->val().shiftVal()) findFn(node);
	}
      });
  }

  template <bool Evict_ = Evict>
  ZuIfT<!Evict_ || !Evict> add(NodeRef node) {
    Guard guard{m_lock};
    add_<false>(ZuMv(node));
  }

  template <bool Evict_ = Evict>
  ZuIfT<Evict_ && Evict> add(NodeRef node) {
    Guard guard{m_lock};
    add_<true>(ZuMv(node), [](Node *) { return true; });
  }

  template <bool Evict_ = Evict, typename EvictFn>
  ZuIfT<Evict_ && Evict> add(NodeRef node, EvictFn evictFn) {
    Guard guard{m_lock};
    add_<true>(ZuMv(node), ZuMv(evictFn));
  }

  // update keys lambda - l(node)
  template <typename KeyIDs_ = ZuSeq<>, typename L>
  void update(Node *node, L l) const {
    Guard guard{m_lock};
    m_hash.template update<KeyIDs_>(node,
      [this, &guard, l = ZuMv(l)](Node *node) mutable {
	guard = Guard{};
	l(node);
	guard = Guard{m_lock};
      });
  }

  template <int KeyID, typename Key>
  NodeMvRef del(const Key &key) {
    Guard guard{m_lock};
    NodeMvRef node = m_hash.template del<KeyID>(key);
    if constexpr (Evict) if (node) m_lru.delNode(node);
    return node;
  }

  NodeMvRef delNode(Node *node_) {
    Guard guard{m_lock};
    NodeMvRef node = m_hash.delNode(node_);
    if constexpr (Evict) if (node) m_lru.delNode(node);
    return node;
  }

private:
  template <int KeyID, bool UpdateLRU = Evict, typename Key>
  NodeRef find_(const Key &key) {
    if (auto node = m_hash.template find<KeyID>(key)) {
      if constexpr (UpdateLRU && Evict) {
	if (auto node_ = m_lru.delNode(node)) m_lru.pushNode(ZuMv(node_));
      }
      return node;
    }
    return nullptr;
  }

  template <bool Evict_ = Evict>
  ZuIfT<!Evict_ || !Evict> add_(NodeRef node) {
    Node *nodePtr = node;
    m_hash.add(ZuMv(node));
    if constexpr (Evict) m_lru.pushNode(nodePtr);
  }

  template <bool Evict_ = Evict>
  ZuIfT<Evict_ && Evict> add_(NodeRef node) {
    add_(ZuMv(node), [](Node *) { return true; });
  }

  template <bool Evict_ = Evict, typename EvictFn>
  ZuIfT<Evict_ && Evict> add_(NodeRef node, EvictFn evictFn) {
    Node *nodePtr = node;
    if (m_hash.count_() >= m_size) {
      if (NodeMvRef evicted = m_lru.shift()) {
	if (evictFn(evicted)) {
	  ++m_evictions;
	  m_hash.delNode(evicted);
	} else { // pinned
	  if constexpr (ZuIsSame<ZuPtr<Node>, NodeMvRef>{})
	    m_lru.pushNode(ZuMv(evicted).release());
	  else
	    m_lru.pushNode(ZuMv(evicted));
	}
      }
    }
    m_hash.add(ZuMv(node));
    m_lru.pushNode(nodePtr);
  }

public:
  // all() iterates over the cache asynchronously
  template <typename L>
  void all(L l) const {
    m_lock.lock();
    const_cast<ZmPolyCache *>(this)->all_<false>(ZuMv(l));
  }

  // allSync() synchronously blocks
  template <typename L>
  void allSync(L l) const {
    m_lock.lock();
    const_cast<ZmPolyCache *>(this)->all_<true>(ZuMv(l));
  }

private:
  template <bool Sync, typename L>
  bool all_(L l) {
    unsigned n = m_hash.count_();
    auto buf = ZmScratch(NodeRef, n);
    if (!buf.data()) return false;
    {
      auto i = m_hash.iter();
      for (unsigned j = 0; j < n; j++) {
	auto ref = i();
	if (ZuUnlikely(!ref)) break;
	buf.push(ZuMv(ref));
      }
    }
    m_lock.unlock();
    n = buf.length();
    if constexpr (Sync)
      ZmBlock<>{}(n, [&l, &buf](unsigned j, auto wake) {
	l(ZuMv(buf[j]), ZuMv(wake));
      });
    else
      for (unsigned j = 0; j < n; j++)
	l(ZuMv(buf[j]));
    return true;
  }

private:
  unsigned		m_size;
 
  mutable Lock		m_lock;
    PolyHash		  m_hash;
    LRU			  m_lru;
    LoadHashes		  m_loadHashes;
    uint64_t		  m_loads = 0;
    uint64_t		  m_misses = 0;
    uint64_t		  m_evictions = 0;
};

#endif /* ZmPolyCache_HH */
