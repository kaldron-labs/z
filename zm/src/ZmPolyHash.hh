//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// tuple of multiple ZmHash tables
// - each table is an index on ZuStruct objects
// - each hash table indexes a different key of the object
// - all hash tables are intrusive
// - each hash table node overlays the next index's node, shadowing it
// - all nodes are consolidated with the object data into a single instance
//   - only one allocation is performed per object
// - the primary key's hash table owns the object (unless Shadow is specified)

#ifndef ZmPolyHash_HH
#define ZmPolyHash_HH

#ifndef ZmLib_HH
#include <zlib/ZmLib.hh>
#endif

#include <zlib/ZuAssert.hh>
#include <zlib/ZuStruct.hh>
#include <zlib/ZuUnroll.hh>

#include <zlib/ZmHash.hh>

// NTP (named template parameters) defaults
struct ZmPolyHash_Defaults {
  using Lock = ZmNoLock;
  enum { Shadow = 0 };
  struct HeapID : public ZuStringT<"ZmPolyHash"> { };
  using ID = HeapID;
  enum { Sharded = 0 };
};

// NTP parameters are a subset of those used for ZmHash
template <typename Lock, typename NTP = ZmPolyHash_Defaults>
using ZmPolyHashLock = ZmHashLock<Lock, NTP>;
template <bool Shadow, typename NTP = ZmPolyHash_Defaults>
using ZmPolyHashShadow_ = ZmHashShadow_<Shadow, NTP>;
template <typename NTP = ZmPolyHash_Defaults>
using ZmPolyHashShadow = ZmHashShadow<NTP>;
template <typename HeapID, typename NTP = ZmPolyHash_Defaults>
using ZmPolyHashHeapID_ = ZmHashHeapID_<HeapID, NTP>;
template <ZuString HeapID, typename NTP = ZmPolyHash_Defaults>
using ZmPolyHashHeapID = ZmHashHeapID_<ZuStringT<HeapID>, NTP>;
template <bool Sharded, typename NTP = ZmPolyHash_Defaults>
using ZmPolyHashSharded = ZmHashSharded<Sharded, NTP>;

template <typename Index_, unsigned KeyID_>
struct ZmPolyHash_NodeT;

template <typename T_>
struct ZmPolyHash_ResolvedT { using T = T_; };

// resolver for direct ZmPolyHash use; derive macros introduce an owner-named
// resolver so recursive index nodes have short concrete identities
template <typename T_, typename NTP_>
struct ZmPolyHash_Index {
  using Value = T_;
  using NTP = NTP_;
  template <unsigned KeyID>
  using Node = typename ZmPolyHash_NodeT<
    ZmPolyHash_Index<T_, NTP_>, KeyID>::T;
};

template <typename Index_, unsigned KeyID_, bool Last_>
struct ZmPolyHash_NextT;
template <typename Index_, unsigned KeyID_>
struct ZmPolyHash_NextT<Index_, KeyID_, false> {
  using T = typename Index_::template Node<KeyID_ + 1>;
};
template <typename Index_, unsigned KeyID_>
struct ZmPolyHash_NextT<Index_, KeyID_, true> {
  using T = typename Index_::Value;
};

// implementation detail for node resolvers
template <typename Index_, unsigned KeyID_>
class ZmPolyHash_NodeT_ {
  using Value = typename Index_::Value;
  using NTP = typename Index_::NTP;
  enum { Last = KeyID_ + 1 == ZuSeqTL<ZuStructKeyIDs<Value>>::N };
  using Next = typename ZmPolyHash_NextT<Index_, KeyID_, Last>::T;
  using HashNTP = ZmHashNode<Next,
    ZmHashKey<ZuFieldAxor<Value, KeyID_>(),
      ZmHashLock<typename NTP::Lock,
	ZmHashShadow_<NTP::Shadow || KeyID_,
	  ZmHashHeapID_<typename NTP::HeapID, // must come after Shadow
	    ZmHashSharded<NTP::Sharded>>>>>>;
public:
  using T = typename ZmHash_NodeT<Next, HashNTP>::T;
};

// The only public purpose of this trait is its resolved concrete node type.
template <typename Index_, unsigned KeyID_>
struct ZmPolyHash_NodeT {
private:
  using Base = typename ZmPolyHash_NodeT_<Index_, KeyID_>::T;

public:
  struct T : public Base {
    ZuDerive_(T, Base)
  };
};

template <typename Index_, unsigned KeyID_>
struct ZmPolyHash_HashT {
private:
  using Value = typename Index_::Value;
  using NTP = typename Index_::NTP;
  enum { Last = KeyID_ + 1 == ZuSeqTL<ZuStructKeyIDs<Value>>::N };
  using Next = typename ZmPolyHash_NextT<Index_, KeyID_, Last>::T;
  using Node = typename Index_::template Node<KeyID_>;
  using HashNTP = ZmHashNode<Next,
    ZmHashKey<ZuFieldAxor<Value, KeyID_>(),
      ZmHashLock<typename NTP::Lock,
	ZmHashShadow_<NTP::Shadow || KeyID_,
	  ZmHashHeapID_<typename NTP::HeapID, // must come after Shadow
	    ZmHashSharded<NTP::Sharded>>>>>>;

public:
  struct T : public ZmHash<Next, HashNTP, Node, T> {
    using Base = ZmHash<Next, HashNTP, Node, T>;
    using Base::Base;
  };
};

// reverse sort of key IDs
template <typename KeyID>
using ZmPolyHash_KeyIDIndex = ZuInt<-int(KeyID{})>;
template <typename KeyIDs>
using ZmPolyHash_SortKeyIDs =
  ZuTypeSort<ZmPolyHash_KeyIDIndex, ZuSeqTL<KeyIDs>>;

template <typename T_, typename NTP_ = ZmPolyHash_Defaults,
  typename Index_ = ZmPolyHash_Index<T_, NTP_>>
class ZmPolyHash {
public:
  using T = T_;
  using NTP = NTP_;
  using Index = Index_;
  using Lock = typename NTP::Lock;
  enum { Shadow = NTP::Shadow };
  using HeapID = typename NTP::HeapID;
  enum { Sharded = NTP::Sharded };

private:
  // key IDs as a type list
  using KeyIDs = ZuSeqTL<ZuStructKeyIDs<T>>;
  // number of keys
  enum { NKeys = KeyIDs::N };
  template <typename KeyID>
  using Hash = typename ZmPolyHash_HashT<Index, KeyID{}>::T;
  // list of index hash table types
  using HashTL = ZuTypeMap<Hash, KeyIDs>;
  // list of hash ref types
  using HashRefTL = ZuTypeMap<ZmRef, HashTL>;
  // tuple of hash table references
  using HashRefs_ = ZuTypeApply<ZuTuple, HashRefTL>;
  ZuDerive(HashRefs, HashRefs_);
  // primary index
  using Primary = Hash<ZuUnsigned<0>>;

public:
  // most-derived hash node type (i.e. the primary node type)
  using Node = typename Primary::Node;
  using NodeRef = typename Primary::NodeRef;
  using NodeMvRef = typename Primary::NodeMvRef;

  ZmPolyHash() = default;

  ZmPolyHash(ZuCSpan id) {
    auto params = ZmHashParams{id};
    ZuUnroll::all<KeyIDs>([this, &id, &params]<typename KeyID>() {
      m_hashes.template p<KeyID{}>(new Hash<KeyID>{id, params});
    });
  }

  ZmPolyHash(const ZmPolyHash &) = delete;
  ZmPolyHash &operator =(const ZmPolyHash &) = delete;

  ZmPolyHash(ZmPolyHash &&) = default;
  ZmPolyHash &operator =(ZmPolyHash &&) = default;

  ~ZmPolyHash() { clean(); }

  template <unsigned KeyID>
  const auto &hash() { return m_hashes.template p<KeyID>(); }

  unsigned size() const { return m_hashes.template p<0>()->size(); }
  unsigned count_() const { return m_hashes.template p<0>()->count_(); }

  void add(NodeRef node) {
    ZuUnroll::all<ZuTypeRev<ZuTypeTail<1, KeyIDs>>>(
	[this, &node]<typename KeyID>() mutable {
      m_hashes.template p<KeyID{}>()->addNode(node);
    });
    m_hashes.template p<0>()->addNode(ZuMv(node));
  }

  template <unsigned KeyID, typename Key>
  NodeRef find(const Key &key) const {
    auto node = m_hashes.template p<KeyID>()->find(key);
    if constexpr (ZuTraits<NodeRef>::IsPrimitive)
      return static_cast<Node *>(node);
    else
      return node;
  }

  template <unsigned KeyID, typename Key>
  Node *findPtr(const Key &key) const {
    return static_cast<Node *>(m_hashes.template p<KeyID>()->find(key));
  }

  // update keys lambda - l(node)
  template <typename KeyIDs_ = ZuSeq<>, typename L>
  ZuIfT<!KeyIDs_::N>
  update(Node *node, L l) const { l(node); }
  template <typename KeyIDs_ = ZuSeq<>, typename L>
  ZuIfT<KeyIDs_::N && !ZuTypeIn<ZuUnsigned<0>, ZuSeqTL<KeyIDs_>>{}>
  update(Node *node, L l) const {
    using SortedKeyIDs = ZmPolyHash_SortKeyIDs<KeyIDs_>;
    ZuUnroll::all<SortedKeyIDs>(
      [this, node]<typename KeyID>() mutable {
	m_hashes.template p<KeyID{}>()->delNode(node);
      });
    l(node);
    ZuUnroll::all<SortedKeyIDs>(
      [this, node]<typename KeyID>() mutable {
	m_hashes.template p<KeyID{}>()->addNode(node);
      });
  }
  template <typename KeyIDs_ = ZuSeq<>, typename L>
  ZuIfT<KeyIDs_::N && bool(ZuTypeIn<ZuUnsigned<0>, ZuSeqTL<KeyIDs_>>{})>
  update(Node *node, L l) const {
    using SortedKeyIDs = ZmPolyHash_SortKeyIDs<KeyIDs_>;
    if constexpr (SortedKeyIDs::N)
      ZuUnroll::all<SortedKeyIDs>(
	[this, node]<typename KeyID>() mutable {
	  m_hashes.template p<KeyID{}>()->delNode(node);
	});
    NodeMvRef node_ = m_hashes.template p<0>()->delNode(node);
    l(node);
    if constexpr (SortedKeyIDs::N)
      ZuUnroll::all<SortedKeyIDs>(
	[this, node]<typename KeyID>() mutable {
	  m_hashes.template p<KeyID{}>()->addNode(node);
	});
    m_hashes.template p<0>()->addNode(ZuMv(node_));
  }

  template <unsigned KeyID, typename Key>
  NodeMvRef del(const Key &key) {
    if (NodeRef node = find<KeyID>(key)) return delNode(node);
    return nullptr;
  }
  NodeMvRef delNode(Node *node) {
    ZuUnroll::all<ZuTypeRev<ZuTypeTail<1, KeyIDs>>>(
	[this, node]<typename KeyID>() mutable {
      m_hashes.template p<KeyID{}>()->delNode(node);
    });
    auto node_ = m_hashes.template p<0>()->delNode(node);
    if constexpr (ZuTraits<NodeMvRef>::IsPrimitive)
      return static_cast<Node *>(node_);
    else
      return node_;
  }

  // iterators are intentionally read-only, unlike ZmHash
  // - in-place deletion on multiple other hash tables while iterating
  //   over one of them would be highly complex and of dubious benefit
  // - if a grep operation is needed, the caller can use a mark/sweep
  //   with a ZmAlloc'd temporary array
  template <typename Base>
  struct Iter : public Base {
    Iter(Base &&i) : Base{ZuMv(i)} { }

    Node *operator ()() { return static_cast<Node *>(Base::operator ()()); }
  };
  auto iter() const {
    using Base =
      ZuDecay<decltype(ZuDeclVal<const Primary &>().citer())>;
    return Iter<Base>{m_hashes.template p<0>()->citer()};
  }
  template <unsigned KeyID, typename Key>
  auto iter(Key &&key) const {
    using Base =
      ZuDecay<decltype(ZuDeclVal<const Hash<ZuUnsigned<KeyID>> &>().
	citer(ZuFwd<Key>(key)))>;
    return Iter<Base>{
      m_hashes.template p<KeyID>()->citer(ZuFwd<Key>(key))};
  }

  void clean() {
    ZuUnroll::all<ZuTypeRev<KeyIDs>>([this]<typename KeyID>() {
      m_hashes.template p<KeyID{}>()->clean();
    });
  }

private:
  HashRefs	m_hashes;
};

#define ZmPolyHashDerive(Name, T_, ...) \
  ZuDerive(Name ## _NTP, (__VA_ARGS__)); \
  template <unsigned KeyID_> struct Name ## _Node; \
  template <unsigned KeyID_> struct Name ## _NodeT; \
  struct Name ## _Index { \
    using Value = ZuPP_Strip(T_); \
    using NTP = Name ## _NTP; \
    template <unsigned KeyID> \
    using Node = typename Name ## _NodeT<KeyID>::T; \
  }; \
  template <unsigned KeyID_> struct Name ## _NodeT : public \
      ZmPolyHash_ResolvedT<Name ## _Node<KeyID_>> { }; \
  template <unsigned KeyID_> struct Name ## _Node : public \
      ZmPolyHash_NodeT<Name ## _Index, KeyID_>::T { \
    ZuDerive_(Name ## _Node, \
      (typename ZmPolyHash_NodeT<Name ## _Index, KeyID_>::T)) \
  }; \
  ZuDerive(Name, \
    (ZmPolyHash<ZuPP_Strip(T_), Name ## _NTP, Name ## _Index>));

#define ZmPolyHashDeriveT_4(Args, Name, T_, NTP_) \
  ZuPP_PfxTypename(Args) ZuDerive(Name ## _NTP, NTP_); \
  ZuPP_PfxTypename((ZuPP_Strip(Args), KeyID_)) struct Name ## _Node; \
  ZuPP_PfxTypename((ZuPP_Strip(Args), KeyID_)) struct Name ## _NodeT; \
  ZuPP_PfxTypename(Args) struct Name ## _Index { \
    using Value = ZuPP_Strip(T_); \
    using NTP = Name ## _NTP<ZuPP_Strip(Args)>; \
    template <unsigned KeyID> \
    using Node = typename Name ## _NodeT< \
      ZuPP_Strip(Args), ZuUnsigned<KeyID>>::T; \
  }; \
  ZuPP_PfxTypename((ZuPP_Strip(Args), KeyID_)) struct Name ## _NodeT : public \
      ZmPolyHash_ResolvedT<Name ## _Node< \
        ZuPP_Strip(Args), KeyID_>> { }; \
  ZuPP_PfxTypename((ZuPP_Strip(Args), KeyID_)) struct Name ## _Node : public \
      ZmPolyHash_NodeT< \
        Name ## _Index<ZuPP_Strip(Args)>, KeyID_{}>::T { \
    ZuDerive_(Name ## _Node, \
      (typename ZmPolyHash_NodeT< \
        Name ## _Index<ZuPP_Strip(Args)>, KeyID_{}>::T)) \
  }; \
  ZuPP_PfxTypename(Args) ZuDerive(Name, \
    (ZmPolyHash<ZuPP_Strip(T_), Name ## _NTP<ZuPP_Strip(Args)>, \
      Name ## _Index<ZuPP_Strip(Args)>>));
#define ZmPolyHashDeriveT_5(Args, XArgs, Name, T_, NTP_) \
  ZuPP_PfxTypename(Args) ZuDerive(Name ## _NTP, NTP_); \
  ZuPP_PfxTypename((ZuPP_Strip(Args), KeyID_)) struct Name ## _Node; \
  ZuPP_PfxTypename((ZuPP_Strip(Args), KeyID_)) struct Name ## _NodeT; \
  ZuPP_PfxTypename(Args) struct Name ## _Index { \
    using Value = ZuPP_Strip(T_); \
    using NTP = Name ## _NTP<ZuPP_Strip(Args)>; \
    template <unsigned KeyID> \
    using Node = typename Name ## _NodeT< \
      ZuPP_Strip(Args), ZuUnsigned<KeyID>>::T; \
  }; \
  ZuPP_PfxTypename((ZuPP_Strip(Args), KeyID_)) struct Name ## _NodeT : public \
      ZmPolyHash_ResolvedT<Name ## _Node< \
        ZuPP_Strip(Args), KeyID_>> { }; \
  ZuPP_PfxTypename((ZuPP_Strip(Args), KeyID_)) struct Name ## _Node : public \
      ZmPolyHash_NodeT< \
        Name ## _Index<ZuPP_Strip(Args)>, KeyID_{}>::T { \
    ZuDerive_(Name ## _Node, \
      (typename ZmPolyHash_NodeT< \
        Name ## _Index<ZuPP_Strip(Args)>, KeyID_{}>::T)) \
  }; \
  ZuPP_PfxTypename((ZuPP_Strip(Args) ZuPP_StripAppend(XArgs))) ZuDerive(Name, \
    (ZmPolyHash<ZuPP_Strip(T_), Name ## _NTP<ZuPP_Strip(Args)>, \
      Name ## _Index<ZuPP_Strip(Args)>>));
#define ZmPolyHashDeriveT_N(_0, _1, _2, _3, _4, Fn, ...) Fn
#define ZmPolyHashDeriveT(...) \
  ZmPolyHashDeriveT_N(__VA_ARGS__, \
    ZmPolyHashDeriveT_5(__VA_ARGS__), \
    ZmPolyHashDeriveT_4(__VA_ARGS__))

#endif /* ZmPolyHash_HH */
