//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// LRU cache with single index (combination of ZmList and ZmHash)

#ifndef ZmCache_HH
#define ZmCache_HH

#ifndef ZmLib_HH
#include <zlib/ZmLib.hh>
#endif

#include <zlib/ZuDerive.hh>

#include <zlib/ZmLockTraits.hh>
#include <zlib/ZmPLock.hh>
#include <zlib/ZmGuard.hh>
#include <zlib/ZmAlloc.hh>
#include <zlib/ZmList.hh>
#include <zlib/ZmHash.hh>
#include <zlib/ZmBlock.hh>
#include <zlib/ZmCacheStats.hh>

// NTP defaults
struct ZmCache_Defaults : public ZmHash_Defaults {
  struct HeapID : public ZuStringT<"ZmCache"> { };
  using ID = HeapID;
  enum { Evict = 1 };
};

// most NTP parameters are identical to ZmHash
template <auto KeyAxor, typename NTP = ZmCache_Defaults>
using ZmCacheKey = ZmHashKey<KeyAxor, NTP>;
template <auto KeyAxor, auto ValAxor, typename NTP = ZmCache_Defaults>
using ZmCacheKeyVal = ZmHashKeyVal<KeyAxor, ValAxor, NTP>;
template <template <typename> class Cmp, typename NTP = ZmCache_Defaults>
using ZmCacheCmp = ZmHashCmp<Cmp, NTP>;
template <template <typename> class ValCmp, typename NTP = ZmCache_Defaults>
using ZmCacheValCmp = ZmHashValCmp<ValCmp, NTP>;
template <template <typename> class HashFn, typename NTP = ZmCache_Defaults>
using ZmCacheHashFn = ZmHashFn<HashFn, NTP>;
template <typename Lock, typename NTP = ZmCache_Defaults>
using ZmCacheLock = ZmHashLock<Lock, NTP>;
template <bool Shadow, typename NTP = ZmCache_Defaults>
using ZmCacheShadow = ZmHashShadow<Shadow, NTP>;
template <typename HeapID, typename NTP = ZmCache_Defaults>
using ZmCacheHeapID_ = ZmHashHeapID_<HeapID, NTP>;
template <ZuString HeapID, typename NTP = ZmCache_Defaults>
using ZmCacheHeapID = ZmHashHeapID_<ZuStringT<HeapID>, NTP>;
template <bool Sharded, typename NTP = ZmCache_Defaults>
using ZmCacheSharded = ZmHashSharded<Sharded, NTP>;

// ZmCacheEvict - enable/disable eviction
template <bool Evict_, typename NTP = ZmCache_Defaults>
struct ZmCacheEvict : public NTP {
  enum { Evict = Evict_ };
};

template <typename T_, typename NTP = ZmCache_Defaults>
class ZmCache {
public:
  using T = T_;
  using Lock = typename NTP::Lock;
  enum { Evict = NTP::Evict };

private:
  using Guard = ZmGuard<Lock>;
  using ReadGuard = ZmReadGuard<Lock>;
  ZuDerive(LRUList, (ZmList<T, ZmListNode<T, ZmListShadow<true>>>));
  struct LRUDisable { // LRU list is not needed if eviction is disabled
    using Node = T;
    Node *delNode(Node *node) { return node; }
    Node *shift() { return nullptr; }
    void pushNode(Node *) { }
  };
  using LRU = ZuIf<Evict, LRUList, LRUDisable>;
  ZuDerive(Hash,
    (ZmHash<typename LRU::Node,
      ZmHashNode<typename LRU::Node,
	ZmHashLock<ZmNoLock, NTP>>>)); // overrides NTP::Lock

public:
  using Key = typename Hash::Key;
  using Cmp = typename Hash::Cmp;

  using HeapID = Hash::HeapID;
  enum { Sharded = Hash::Sharded };

  using Node = typename Hash::Node;
  using NodeRef = typename Hash::NodeRef;
  using NodeMvRef = typename Hash::NodeMvRef;

private:
  using FindFn = ZmFn<void(Node *)>;
  ZuDerive(FindFnList, (ZmList<FindFn>));
  ZuDerive(LoadHash,
    (ZmHashKV<Key, FindFnList,
      ZmHashHeapID_<HeapID, ZmHashSharded<Sharded>>>));

public:
  ZmCache() noexcept {
    m_hash = new Hash{HeapID{}()};
    m_loadHash = new LoadHash{HeapID{}()};
    m_size = m_hash->size();
  }
  template <
    typename ID,
    ZuIfT<ZuTraits<ID>::IsString, int> = 0>
  ZmCache(const ID &id) {
    m_hash = new Hash{id};
    m_loadHash = new LoadHash{id};
    m_size = m_hash->size();
  }
  template <
    typename Cmp_,
    typename ID,
    decltype(ZuIfT<
      bool(ZuIsSame<Cmp_, Cmp>{}) &&
      ZuTraits<ID>::IsString>(), int()) = 0>
  ZmCache(Cmp_ cmp, const ID &id) {
    m_hash = new Hash{cmp, id};
    m_loadHash = new LoadHash{cmp, id};
    m_size = m_hash->size();
  }
  template <
    typename Params,
    ZuIfT<ZuIsSame<Params, ZmHashParams>{}, int> = 0>
  ZmCache(const Params &params) {
    m_hash = new Hash{params};
    m_loadHash = new LoadHash{params};
    m_size = m_hash->size();
  }
  template <
    typename Cmp_,
    typename Params,
    decltype(ZuIfT<
      bool(ZuIsSame<Cmp_, Cmp>{}) &&
      bool(ZuIsSame<Params, ZmHashParams>{})>(), int()) = 0>
  ZmCache(Cmp_ cmp, const Params &params) {
    m_hash = new Hash{cmp, params};
    m_loadHash = new LoadHash{cmp, params};
    m_size = m_hash->size();
  }

  ZmCache(const ZmCache &) = delete;
  ZmCache &operator =(const ZmCache &) = delete;
  // the move operators are intentionally unlocked
  // - these are exclusively intended for use in initialization
  ZmCache(ZmCache &&c) noexcept :
    m_hash{ZuMv(c.m_hash)},
    m_lru{ZuMv(c.m_lru)},
    m_loadHash{ZuMv(c.m_loadHash)},
    m_loads{c.m_loads},
    m_misses{c.m_misses},
    m_evictions{c.m_evictions}
  {
    c.m_hash = nullptr;
    c.m_lru = LRU{};
    c.m_loadHash = nullptr;
    c.m_loads = c.m_misses = c.m_evictions = 0;
  }
  ZmCache &operator =(ZmCache &&c) noexcept {
    this->~ZmCache();
    new (this) ZmCache{ZuMv(c)};
    return *this;
  }

  ~ZmCache() = default;

  unsigned size() const { return m_size; }

  using Stats = ZmCacheStats;

private:
  void stats_(Stats &r) const {
    r.size = m_hash->size();
    r.count = m_hash->count_();
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

  template <bool UpdateLRU = Evict, typename Key>
  NodeRef find(const Key &key) {
    Guard guard{m_lock};
    ++m_loads;
    if (NodeRef node = find_<UpdateLRU>(key)) return node;
    ++m_misses;
    return nullptr;
  }

  template <
    bool UpdateLRU = Evict, bool Evict_ = Evict,
    typename Key, typename FindFn_, typename LoadFn>
  void find(const Key &key, FindFn_ findFn, LoadFn loadFn) {
    Guard guard{m_lock};
    ++m_loads;
    if (NodeRef node = find_<UpdateLRU>(key)) { findFn(ZuMv(node)); return; }
    ++m_misses;
    typename LoadHash::Node *load = m_loadHash->find(key);
    bool pending = load;
    if (!pending)
      m_loadHash->addNode(
	  load = new typename LoadHash::Node{key, FindFnList{}});
    load->val().push(FindFn{ZuMv(findFn)});
    guard.unlock();
    if (!pending)
      loadFn(key, [this, key](NodeRef node) {
	Guard guard{m_lock};
	if (node) add_(node);
	if (auto load = m_loadHash->del(key)) {
	  guard.unlock();
	  while (auto findFn = load->val().shiftVal()) findFn(node);
	}
      });
  }

  template <
    bool UpdateLRU = Evict,
    typename Key, typename FindFn_, typename LoadFn, typename EvictFn>
  void find(const Key &key, FindFn_ findFn, LoadFn loadFn, EvictFn evictFn) {
    Guard guard{m_lock};
    ++m_loads;
    if (NodeRef node = find_<UpdateLRU>(key)) { findFn(ZuMv(node)); return; }
    ++m_misses;
    typename LoadHash::Node *load = m_loadHash->find(key);
    bool pending = load;
    if (!pending)
      m_loadHash->addNode(
	  load = new typename LoadHash::Node{key, FindFnList{}});
    load->val().push(FindFn{ZuMv(findFn)});
    guard.unlock();
    if (!pending)
      loadFn(key, [this, key, evictFn = ZuMv(evictFn)](NodeRef node) {
	Guard guard{m_lock};
	if (node)
	  if (auto evicted = add_<true>(node)) evictFn(ZuMv(evicted));
	if (auto load = m_loadHash->del(key)) {
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
  ZuIfT<Evict_ && Evict, NodeRef> add(NodeRef node) {
    Guard guard{m_lock};
    return add_<true>(ZuMv(node));
  }

  template <bool Evict_ = Evict, typename EvictFn>
  ZuIfT<Evict_ && Evict, NodeRef> add(NodeRef node, EvictFn evictFn) {
    Guard guard{m_lock};
    if (auto evicted = add_<true>(ZuMv(node)))
      evictFn(ZuMv(evicted));
  }

  template <typename Key>
  NodeMvRef del(const Key &key) {
    Guard guard{m_lock};
    NodeMvRef node = m_hash->del(key);
    if constexpr (Evict) if (node) m_lru.delNode(node);
    return node;
  }

  NodeMvRef delNode(Node *node_) {
    Guard guard{m_lock};
    NodeMvRef node = m_hash->delNode(node_);
    if constexpr (Evict) if (node) m_lru.delNode(node);
    return node;
  }

private:
  template <bool UpdateLRU = Evict, typename P>
  NodeRef find_(const P &key) {
    if (auto node = m_hash->find(key)) {
      if constexpr (UpdateLRU && Evict)
	m_lru.pushNode(m_lru.delNode(node));
      return node;
    }
    return nullptr;
  }

  template <bool Evict_ = Evict>
  ZuIfT<!Evict_ || !Evict> add_(NodeRef node) {
    Node *nodePtr = node;
    m_hash->addNode(ZuMv(node));
    if constexpr (Evict) m_lru.pushNode(nodePtr);
  }

  template <bool Evict_ = Evict>
  ZuIfT<Evict_ && Evict, NodeMvRef> add_(NodeRef node) {
    Node *nodePtr = node;
    NodeMvRef evicted = nullptr;
    if (m_hash->count_() >= m_size) {
      auto evicted_ = m_lru.shift();
      if constexpr (ZuTraits<ZuDecay<decltype(evicted_)>>::IsPrimitive)
	evicted = static_cast<NodeMvRef>(evicted_);
      else
	evicted = NodeMvRef{ZuMv(evicted_)};
      if (evicted) {
	++m_evictions;
	m_hash->delNode(ZuMv(evicted));
      }
    }
    m_hash->addNode(ZuMv(node));
    m_lru.pushNode(nodePtr);
    return evicted;
  }

public:
  // all() is const by default, but all<true>() empties the cache
  template <bool Delete = false, typename L>
  ZuIfT<!Delete> all(L &&l) const {
    m_lock.lock();
    const_cast<ZmCache *>(this)->all_<Delete, false>(ZuFwd<L>(l));
  }
  template <bool Delete, typename L>
  ZuIfT<Delete> all(L &&l) {
    m_lock.lock();
    all_<Delete, false>(ZuFwd<L>(l));
  }

  // allSync() synchronously blocks
  template <bool Delete = false, typename L>
  ZuIfT<!Delete> allSync(L &&l) const {
    m_lock.lock();
    const_cast<ZmCache *>(this)->all_<Delete, true>(ZuFwd<L>(l));
  }
  template <bool Delete, typename L>
  ZuIfT<Delete> allSync(L &&l) {
    m_lock.lock();
    all_<Delete, true>(ZuFwd<L>(l));
  }

private:
  template <bool Delete, bool Sync, typename L>
  bool all_(L &&l) {
    unsigned n = m_hash->count_();
    auto buf = ZmAlloc(NodeRef, n);
    if (!buf) return false;
    using Fn = NodeRefFn<NodeRef>;
    {
      auto i = allIter<Delete>();
      unsigned j = 0;
      for (j = 0; j < n; j++) {
	Fn::ctor(&buf[j], i());
	if (ZuUnlikely(!buf[j])) { Fn::dtor(buf[j]); break; }
	if constexpr (Delete) i.del();
      }
      n = j;
    }
    m_lock.unlock();
    if constexpr (Sync)
      ZmBlock<>{}(n, [&l, &buf](unsigned j, auto wake) {
	l(ZuMv(buf[j]), ZuMv(wake));
	Fn::dtor(buf[j]);
      });
    else
      for (unsigned j = 0; j < n; j++) {
	l(ZuMv(buf[j]));
	Fn::dtor(buf[j]);
      }
    return true;
  }
  template <bool Delete>
  ZuIfT<Delete, decltype(ZuDeclVal<Hash &>().iter())>
  allIter() {
    return m_hash->iter();
  }
  template <bool Delete>
  ZuIfT<!Delete, decltype(ZuDeclVal<const Hash &>().citer())>
  allIter() {
    return m_hash->citer();
  }
  template <typename NodeRef>
  struct NodeRefFn {
    static void ctor(NodeRef *ptr, NodeRef ref) {
      new (ptr) NodeRef{ZuMv(ref)};
    }
    static void dtor(NodeRef &ref) { ref.~NodeRef(); }
  };
  template <typename Node>
  struct NodeRefFn<Node *> {
    using NodeRef = Node *;
    static void ctor(NodeRef *ptr, NodeRef ref) { *ptr = ref; }
    static constexpr void dtor(NodeRef &) { }
  };

private:
  unsigned		m_size;
 
  mutable Lock		m_lock;
    ZmRef<Hash>		  m_hash;
    LRU			  m_lru;
    ZmRef<LoadHash>	  m_loadHash;
    uint64_t		  m_loads = 0;
    uint64_t		  m_misses = 0;
    uint64_t		  m_evictions = 0;
};

template <typename P0, typename P1, typename NTP = ZmCache_Defaults>
ZuDerive(ZmCacheKV,
  (ZmCache<ZuTuple<P0, P1>,
    ZmCacheKeyVal<ZuTupleAxor<0>(), ZuTupleAxor<1>(), NTP>>));

#endif /* ZmCache_HH */
