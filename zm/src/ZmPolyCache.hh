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

template <typename T_>
struct ZmPolyCache_LRUList_NTP : public
    ZmListNode<T_, ZmListShadow<>> {
  ZuDerive_(ZmPolyCache_LRUList_NTP, (ZmListNode<T_, ZmListShadow<>>))
};
template <typename T_>
struct ZmPolyCache_LRUList_Node : public
    ZmList_NodeT<T_, ZmPolyCache_LRUList_NTP<T_>>::T {
  ZuDerive_(ZmPolyCache_LRUList_Node,
    (typename ZmList_NodeT<T_, ZmPolyCache_LRUList_NTP<T_>>::T))
};
template <typename T_>
struct ZmPolyCache_LRUList : public
    ZmList<T_, ZmPolyCache_LRUList_NTP<T_>,
      ZmPolyCache_LRUList_Node<T_>, ZmPolyCache_LRUList<T_>> {
  using Base = ZmList<T_, ZmPolyCache_LRUList_NTP<T_>,
    ZmPolyCache_LRUList_Node<T_>, ZmPolyCache_LRUList<T_>>;
  using Base::Base;
};
template <typename T_>
struct ZmPolyCache_LRUDisable {
  using Node = T_;
  Node *delNode(Node *node) { return nullptr; }
  Node *shift() { return nullptr; }
  void pushNode(Node *) { }
};
template <typename T_, bool Evict_>
struct ZmPolyCache_LRUT;
template <typename T_>
struct ZmPolyCache_LRUT<T_, true> { using T = ZmPolyCache_LRUList<T_>; };
template <typename T_>
struct ZmPolyCache_LRUT<T_, false> { using T = ZmPolyCache_LRUDisable<T_>; };

ZmPolyHashDeriveT((NTP_, LRU_), (T_), ZmPolyCache_Hash,
  (typename LRU_::Node), (ZmPolyHashLock<ZmNoLock, NTP_>));

template <typename Node_, typename HeapID_, bool Sharded_>
struct ZmPolyCache_FindFn : public ZmFn<void(Node_ *),
    ZmFnHeapID<HeapID_{}() + ".FindFn"_Zu, ZmFnSharded<Sharded_>>> {
  ZuDerive_(ZmPolyCache_FindFn,
    (ZmFn<void(Node_ *),
      ZmFnHeapID<HeapID_{}() + ".FindFn"_Zu, ZmFnSharded<Sharded_>>>) )
};
template <typename FindFn_, typename HeapID_>
struct ZmPolyCache_FindFnList_NTP : public ZmHashHeapID_<HeapID_> {
  ZuDerive_(ZmPolyCache_FindFnList_NTP, (ZmHashHeapID_<HeapID_>))
};
template <typename FindFn_, typename HeapID_>
struct ZmPolyCache_FindFnList_Node : public ZmList_NodeT<
    FindFn_, ZmPolyCache_FindFnList_NTP<FindFn_, HeapID_>>::T {
  ZuDerive_(ZmPolyCache_FindFnList_Node,
    (typename ZmList_NodeT<
      FindFn_, ZmPolyCache_FindFnList_NTP<FindFn_, HeapID_>>::T))
};
template <typename FindFn_, typename HeapID_>
struct ZmPolyCache_FindFnList : public ZmList<
    FindFn_, ZmPolyCache_FindFnList_NTP<FindFn_, HeapID_>,
    ZmPolyCache_FindFnList_Node<FindFn_, HeapID_>,
    ZmPolyCache_FindFnList<FindFn_, HeapID_>> {
  using Base = ZmList<
    FindFn_, ZmPolyCache_FindFnList_NTP<FindFn_, HeapID_>,
    ZmPolyCache_FindFnList_Node<FindFn_, HeapID_>,
    ZmPolyCache_FindFnList<FindFn_, HeapID_>>;
  using Base::Base;
};

ZmHashDeriveT((Key_, FindFnList_, HeapID_), ZmPolyCache_LoadHash,
  (ZuTuple<Key_, FindFnList_>),
  (ZmHashKeyVal<ZuTupleAxor<0>(), ZuTupleAxor<1>(), ZmHashHeapID_<HeapID_>>));

template <typename T_, typename NTP = ZmPolyCache_Defaults>
class ZmPolyCache {
public:
  using T = T_;
  using Lock = typename NTP::Lock;
  enum { Evict = NTP::Evict };

private:
  using Guard = ZmGuard<Lock>;
  using ReadGuard = ZmReadGuard<Lock>;
  using LRU = typename ZmPolyCache_LRUT<T, Evict>::T;
  using PolyHash = ZmPolyCache_Hash<NTP, LRU, T>;

public:
  using HeapID = PolyHash::HeapID;
  enum { Sharded = PolyHash::Sharded };

  using Node = typename PolyHash::Node;
  using NodeRef = typename PolyHash::NodeRef;
  using NodeMvRef = typename PolyHash::NodeMvRef;

private:
  using FindFn = ZmPolyCache_FindFn<Node, HeapID, Sharded>;
  using FindFnList = ZmPolyCache_FindFnList<FindFn, HeapID>;
  // key IDs as a type list
  using KeyIDs = ZuSeqTL<ZuStructKeyIDs<T>>;
  // key types, each a tuple
  template <int KeyID> using Key = ZuStructKeyT<T, KeyID>;
  template <typename KeyID> using KeyT = ZuStructKeyT<T, KeyID{}>;
  using Keys = ZuTypeMap<KeyT, KeyIDs>;
  // load hash tables, mapping keys to pending find() operations for each KeyID
  template <typename KeyID>
  using LoadHash = ZmPolyCache_LoadHash<KeyT<KeyID>, FindFnList, HeapID>;
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
  template <bool Reset = false, typename = ZuIfT<!Reset>>
  void stats(Stats &r) const {
    ReadGuard guard{m_lock};
    stats_(r);
  }
  template <bool Reset = false, typename = ZuIfT<Reset>>
  void stats(Stats &r) {
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
    findMiss_<KeyID, Evict_>(
      guard, ZuMv(key), FindFn{ZuMv(findFn)}, ZuMv(loadFn));
  }

  template <
    int KeyID = 0,
    bool UpdateLRU = Evict, bool Evict_ = Evict,
    typename FindFn_, typename LoadFn, typename EvictFn>
  void find(Key<KeyID> key, FindFn_ findFn, LoadFn loadFn, EvictFn evictFn) {
    Guard guard{m_lock};
    ++m_loads;
    if (NodeRef node = find_<KeyID, UpdateLRU>(key)) {
      findFn(ZuMv(node)); return;
    }
    ++m_misses;
    findMiss_<KeyID, Evict_>(
      guard, ZuMv(key), FindFn{ZuMv(findFn)},
      ZuMv(loadFn), ZuMv(evictFn));
  }

private:
  template <int KeyID, bool Evict_, typename LoadFn>
  void findMiss_(
      Guard &guard, Key<KeyID> key, FindFn findFn, LoadFn loadFn) {
    const auto &loadHash = m_loadHashes.template p<KeyID>();
    LoadHashNode<KeyID> *load = loadHash->find(key);
    bool pending = load;
    if (!pending)
      loadHash->addNode(load = new LoadHashNode<KeyID>{key, FindFnList{}});
    load->val().push(ZuMv(findFn));
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

  template <int KeyID, bool Evict_, typename LoadFn, typename EvictFn>
  void findMiss_(
      Guard &guard, Key<KeyID> key, FindFn findFn,
      LoadFn loadFn, EvictFn evictFn) {
    const auto &loadHash = m_loadHashes.template p<KeyID>();
    LoadHashNode<KeyID> *load = loadHash->find(key);
    bool pending = load;
    if (!pending)
      loadHash->addNode(load = new LoadHashNode<KeyID>{key, FindFnList{}});
    load->val().push(ZuMv(findFn));
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

public:

  template <bool Evict_ = Evict,
    typename = void>
  void add(NodeRef node) {
    if constexpr (!Evict_ || !Evict) {
      Guard guard{m_lock};
      add_<false>(ZuMv(node));
    } else {
      Guard guard{m_lock};
      add_<true>(ZuMv(node), [](Node *) { return true; });
    }
  }

  template <bool Evict_ = Evict, typename EvictFn,
    typename = ZuIfT<Evict_ && Evict>>
  void add(NodeRef node, EvictFn evictFn) {
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

  template <bool Evict_ = Evict,
    typename = void>
  void add_(NodeRef node) {
    if constexpr (!Evict_ || !Evict) {
      Node *nodePtr = node;
      m_hash.add(ZuMv(node));
      if constexpr (Evict) m_lru.pushNode(nodePtr);
    } else {
      add_(ZuMv(node), [](Node *) { return true; });
    }
  }

  template <bool Evict_ = Evict, typename EvictFn,
    typename = ZuIfT<Evict_ && Evict>>
  void add_(NodeRef node, EvictFn evictFn) {
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

#define ZmPolyCacheDerive(Name, T_, ...) \
  ZuDerive(Name ## _NTP, (__VA_ARGS__)); \
  ZuDerive(Name, (ZmPolyCache<ZuPP_Strip(T_), Name ## _NTP>));

#define ZmPolyCacheDeriveT_4(Args, Name, T_, NTP) \
  ZuPP_PfxTypename(Args) ZuDerive(Name ## _NTP, NTP); \
  ZuPP_PfxTypename(Args) ZuDerive(Name, \
    (ZmPolyCache<ZuPP_Strip(T_), Name ## _NTP<ZuPP_Strip(Args)>>));
#define ZmPolyCacheDeriveT_5(Args, XArgs, Name, T_, NTP) \
  ZuPP_PfxTypename(Args) ZuDerive(Name ## _NTP, NTP); \
  ZuPP_PfxTypename((ZuPP_Strip(Args) ZuPP_StripAppend(XArgs))) ZuDerive(Name, \
    (ZmPolyCache<ZuPP_Strip(T_), Name ## _NTP<ZuPP_Strip(Args)>>));
#define ZmPolyCacheDeriveT_N(_0, _1, _2, _3, _4, Fn, ...) Fn
#define ZmPolyCacheDeriveT(...) \
  ZmPolyCacheDeriveT_N(__VA_ARGS__, \
    ZmPolyCacheDeriveT_5(__VA_ARGS__), \
    ZmPolyCacheDeriveT_4(__VA_ARGS__))

#endif /* ZmPolyCache_HH */
