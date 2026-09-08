//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// red/black tree (compile-time policy-based)
// - intrusive
// - policy-based control of key, value, locking, heap, etc.
// - intentionally disdains range-based for() and structured binding

#ifndef ZmRBTree_HH
#define ZmRBTree_HH

#ifndef ZmLib_HH
#include <zlib/ZmLib.hh>
#endif

#include <zlib/ZuCmp.hh>
#include <zlib/ZuArray.hh>

#include <zlib/ZmAssert.hh>
#include <zlib/ZmGuard.hh>
#include <zlib/ZmNoLock.hh>
#include <zlib/ZmRef.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmNode.hh>
#include <zlib/ZmNodeFn.hh>

// uses NTP (named template parameters):
//
// ZmRBTreeKV<ZtString<>, ZtString<>,	// key, value pair of ZtString<>s
//     ZmRBTreeValCmp<ZuICmp> >		// case-insensitive comparison

// NTP defaults
struct ZmRBTree_Defaults {
  static constexpr auto KeyAxor = ZuDefaultAxor();
  static constexpr auto ValAxor = ZuDefaultAxor();
  template <typename T> using CmpT = ZuCmp<T>;
  template <typename T> using ValCmpT = ZuCmp<T>;
  enum { Unique = 0 };
  using Lock = ZmNoLock;
  using Node = ZuVoid;
  enum { Shadow = 0 };
  struct HeapID : public ZuStringT<"ZmRBTree"> { };
  enum { Sharded = 0 };
};

// ZmRBTreeKey - key accessor
template <auto KeyAxor_, typename NTP = ZmRBTree_Defaults>
struct ZmRBTreeKey : public NTP {
  static constexpr auto KeyAxor = KeyAxor_;
};

// ZmRBTreeKeyVal - key and optional value accessors
template <
  auto KeyAxor_, auto ValAxor_,
  typename NTP = ZmRBTree_Defaults>
struct ZmRBTreeKeyVal : public NTP {
  static constexpr auto KeyAxor = KeyAxor_;
  static constexpr auto ValAxor = ValAxor_;
};

// ZmRBTreeCmp - the comparator
template <
  template <typename> class Cmp_,
  typename NTP = ZmRBTree_Defaults>
struct ZmRBTreeCmp : public NTP {
  template <typename T> using CmpT = Cmp_<T>;
};

// ZmRBTreeValCmp - the optional value comparator
template <
  template <typename> class ValCmp_,
  typename NTP = ZmRBTree_Defaults>
struct ZmRBTreeValCmp : public NTP {
  template <typename T> using ValCmpT = ValCmp_<T>;
};

// ZmRBTreeUnique - key is unique
template <bool Unique_, class NTP = ZmRBTree_Defaults>
struct ZmRBTreeUnique : public NTP {
  enum { Unique = Unique_ };
};

// ZmRBTreeLock - the lock type used (ZmRWLock will permit concurrent reads)
template <class Lock_, class NTP = ZmRBTree_Defaults>
struct ZmRBTreeLock : public NTP {
  using Lock = Lock_;
};

// ZmRBTreeNode - the base type for nodes
template <typename Node_, typename NTP = ZmRBTree_Defaults>
struct ZmRBTreeNode : public NTP {
  using Node = Node_;
};

// ZmRBTreeShadow - shadow nodes, do not manage ownership
template <bool Shadow_, typename NTP = ZmRBTree_Defaults>
struct ZmRBTreeShadow_;
template <typename NTP>
struct ZmRBTreeShadow_<true, NTP> : public NTP {
  enum { Shadow = true };
  struct HeapID : public ZuStringT<""> { };
};
template <typename NTP>
struct ZmRBTreeShadow_<false, NTP> : public NTP {
  enum { Shadow = false };
};
template <typename NTP = ZmRBTree_Defaults>
using ZmRBTreeShadow = ZmRBTreeShadow_<true, NTP>;

// ZmRBTreeHeapID - the heap ID
template <typename HeapID_, class NTP = ZmRBTree_Defaults>
struct ZmRBTreeHeapID_ : public NTP {
  using HeapID = HeapID_;
};
template <ZuString HeapID, class NTP = ZmRBTree_Defaults>
using ZmRBTreeHeapID = ZmRBTreeHeapID_<ZuStringT<HeapID>, NTP>;

// ZmRBTreeSharded - heap is sharded
template <bool Sharded_, typename NTP = ZmRBTree_Defaults>
struct ZmRBTreeSharded : public NTP {
  enum { Sharded = Sharded_ };
};

// ZmRBTree node
template <typename Node>
struct ZmRBTree_NodeExt_Dup {
  ZuInline Node *dup() const { return m_dup; }
  ZuInline void dup(Node *n) { m_dup = n; }

  ZuInline void clear() { m_dup = nullptr; }

private:
  Node	*m_dup = nullptr;
};
template <typename Node>
struct ZmRBTree_NodeExt_Unique {
  ZuInline Node *dup() const { return nullptr; }
  ZuInline void dup(Node *) { }

  ZuInline void clear() { }
};
template <typename Node, bool Unique>
struct ZmRBTree_NodeExt :
    public ZuIf<Unique,
      ZmRBTree_NodeExt_Unique<Node>,
      ZmRBTree_NodeExt_Dup<Node>> {
  // 64bit pointer-packing - uses bit 63
  static constexpr uintptr_t Black() { return uintptr_t(1)<<63; }
  static constexpr uintptr_t Black(bool b) { return uintptr_t(b)<<63; }

  using Base = ZuIf<Unique,
    ZmRBTree_NodeExt_Unique<Node>,
    ZmRBTree_NodeExt_Dup<Node>>;

  ZuInline bool black() { return m_parent & Black(); }
  ZuInline void black(bool b) {
    m_parent = (m_parent & ~Black()) | Black(b);
  }
  ZuInline void black(const ZmRBTree_NodeExt *node) {
    m_parent = (m_parent & ~Black()) | (node->m_parent & Black());
  }
  ZuInline void setBlack() { m_parent |= Black(); }
  ZuInline void clrBlack() { m_parent &= ~Black(); }

  ZuInline Node *right() const { return m_right; }
  ZuInline Node *left() const { return m_left; }
  ZuInline Node *parent() const {
    return reinterpret_cast<Node *>(m_parent & ~Black());
  }

  ZuInline void right(Node *n) { m_right = n; }
  ZuInline void left(Node *n) { m_left = n; }
  ZuInline void parent(Node *n) {
    m_parent = reinterpret_cast<uintptr_t>(n) | (m_parent & Black());
  }

  void clearDup() {
    Base::clear();
    m_parent = 0;
  }
  void clear() {
    Base::clear();
    m_right = nullptr;
    m_left = nullptr;
    m_parent = 0;
  }

private:
  Node		*m_right = nullptr;
  Node		*m_left = nullptr;
  uintptr_t	m_parent = 0;
};

template <typename T_, class NTP_, typename Node_, typename Impl_>
class ZmRBTree;

// ZmRBTree_NodeT - the concrete tree node selected by an NTP
template <typename T_, typename NTP_>
struct ZmRBTree_NodeT {
private:
  static constexpr auto KeyAxor = NTP_::KeyAxor;
  static constexpr auto ValAxor = NTP_::ValAxor;
  enum { Unique = NTP_::Unique };
  using NodeBase = typename NTP_::Node;
  using HeapID = typename NTP_::HeapID;
  enum { Sharded = NTP_::Sharded };

  struct Node;
  using NodeExt = ZmRBTree_NodeExt<Node, Unique>;
  using NodeImpl = ZmNode<
    T_, KeyAxor, ValAxor, NodeBase, NodeExt, HeapID, Sharded>;

  struct Node : public NodeImpl {
    template <typename, class, typename, typename> friend class ZmRBTree;
    ZuDerive_(Node, NodeImpl)
  private:
    using NodeExt::black;
    using NodeExt::setBlack;
    using NodeExt::clrBlack;
    using NodeExt::right;
    using NodeExt::left;
    using NodeExt::parent;
    using NodeExt::dup;
    using NodeExt::clearDup;
    using NodeExt::clear;
  };

public:
  using T = Node;
};

// ZmRBTree search and iteration comparators
enum {
  ZmRBTreeEqual = 0,
  ZmRBTreeGreaterEqual = 1,
  ZmRBTreeLessEqual = -1,
  ZmRBTreeGreater = 2,
  ZmRBTreeLess = -2
};

// red/black tree iterator - base
template <typename Tree_, int Direction_>
class ZmRBTreeIter_ {
friend Tree_;
template <typename, class, typename, typename> friend class ZmRBTree;

public:
  using Tree = Tree_;
  enum { Direction = Direction_ };
  using Node = typename Tree::Node;
  using NodeRef = typename Tree::NodeRef;

protected:
  ZmRBTreeIter_(const ZmRBTreeIter_ &) = delete;
  ZmRBTreeIter_ &operator =(const ZmRBTreeIter_ &) = delete;

  ZmRBTreeIter_(ZmRBTreeIter_ &&) = default;
  ZmRBTreeIter_ &operator =(ZmRBTreeIter_ &&) = default;

  ZmRBTreeIter_(Tree &tree) : m_tree(tree) {
    tree.iterBegin(*this);
  }
  template <typename P>
  ZmRBTreeIter_(Tree &tree, const P &key) : m_tree(tree) {
    tree.iterBegin(*this, key);
  }

public:
  void reset() { m_tree.iterBegin(*this); }
  template <typename P>
  void reset(const P &key) {
    m_tree.iterBegin(*this, key);
  }

  Node *operator ()() { return m_tree.iterate(*this); }

  decltype(auto) key() { return Tree::key(m_tree.iterate(*this)); }
  decltype(auto) val() { return Tree::val(m_tree.iterate(*this)); }

  unsigned count() const { return m_tree.count_(); }

protected:
  Tree		&m_tree;
  Node		*m_node;
};

// read-write tree iterator
template <typename Tree_, int Direction_ = ZmRBTreeGreaterEqual>
class ZmRBTreeIter :
    public Tree_::Guard,
    public ZmRBTreeIter_<Tree_, Direction_> {
  using Tree = Tree_;
  enum { Direction = Direction_ };
  using Guard = typename Tree::Guard;
  using Node = typename Tree::Node;
  using NodeRef = typename Tree::NodeRef;
  using NodeMvRef = typename Tree::NodeMvRef;

public:
  ZmRBTreeIter(const ZmRBTreeIter &) = delete;
  ZmRBTreeIter &operator =(const ZmRBTreeIter &) = delete;

  ZmRBTreeIter(ZmRBTreeIter &&) = default;
  ZmRBTreeIter &operator =(ZmRBTreeIter &&) = default;

  ZmRBTreeIter(Tree &tree) :
      Guard{tree.lock()},
      ZmRBTreeIter_<Tree, Direction>{tree} { }
  template <typename P>
  ZmRBTreeIter(Tree &tree, const P &key) :
    Guard{tree.lock()},
    ZmRBTreeIter_<Tree, Direction>{tree, key} { }

  NodeMvRef del(Node *node) { return this->m_tree.iterDel(node); }
};

// read-only tree iterator
template <typename Tree_, int Direction_ = ZmRBTreeGreaterEqual>
class ZmRBTreeCIter :
    public Tree_::ReadGuard,
    public ZmRBTreeIter_<Tree_, Direction_> {
  using Tree = Tree_;
  enum { Direction = Direction_ };
  using ReadGuard = typename Tree::ReadGuard;

public:
  ZmRBTreeCIter(const ZmRBTreeCIter &) = delete;
  ZmRBTreeCIter &operator =(const ZmRBTreeCIter &) = delete;

  ZmRBTreeCIter(ZmRBTreeCIter &&) = default;
  ZmRBTreeCIter &operator =(ZmRBTreeCIter &&) = default;

  ZmRBTreeCIter(const Tree &tree) :
    ReadGuard(tree.lock()),
    ZmRBTreeIter_<Tree, Direction>(const_cast<Tree &>(tree)) { }
  template <typename P>
  ZmRBTreeCIter(const Tree &tree, const P &key) :
    ReadGuard(tree.lock()),
    ZmRBTreeIter_<Tree, Direction>(const_cast<Tree &>(tree), key) { }
};

// compile-time check if cmp is static
template <typename T, typename Cmp, typename = void>
struct ZmRBTree_IsStaticCmp : public ZuFalse { };
template <typename T, typename Cmp>
struct ZmRBTree_IsStaticCmp<T, Cmp, decltype(
  Cmp::cmp(ZuDeclVal<const T &>(), ZuDeclVal<const T &>()),
  void())> : public ZuTrue { };
// compile-time check if equals is static
template <typename T, typename Cmp, typename = void>
struct ZmRBTree_IsStaticEquals : public ZuFalse { };
template <typename T, typename Cmp>
struct ZmRBTree_IsStaticEquals<T, Cmp, decltype(
  Cmp::equals(ZuDeclVal<const T &>(), ZuDeclVal<const T &>()),
  void())> : public ZuTrue { };

// red/black tree
template <typename T_, class NTP = ZmRBTree_Defaults,
  typename Node_ = typename ZmRBTree_NodeT<T_, NTP>::T,
  typename Impl_ = void>
class ZmRBTree : public ZmNodeFn<NTP::Shadow, typename NTP::Node> {
  template <typename, int> friend class ZmRBTreeIter_;
  template <typename, int> friend class ZmRBTreeIter;
  template <typename, int> friend class ZmRBTreeCIter;

public:
  using T = T_;
  using Impl = ZuIf<ZuIsSame<Impl_, void>{}, ZmRBTree, Impl_>;
  static constexpr auto KeyAxor = NTP::KeyAxor;
  static constexpr auto ValAxor = NTP::ValAxor;
  using KeyRet = decltype(KeyAxor(ZuDeclVal<const T &>()));
  using ValRet = decltype(ValAxor(ZuDeclVal<const T &>()));
  using Key = ZuRDecay<KeyRet>;
  using Val = ZuRDecay<ValRet>;
  using Cmp = typename NTP::template CmpT<Key>;
  using ValCmp = typename NTP::template ValCmpT<Val>;
  enum { Unique = NTP::Unique };
  using Lock = typename NTP::Lock;
  using NodeBase = typename NTP::Node;
  enum { Shadow = NTP::Shadow };
  using HeapID = NTP::HeapID;
  enum { Sharded = NTP::Sharded };

private:
  using NodeFn = ZmNodeFn<Shadow, NodeBase>;

  using Guard = ZmGuard<Lock>;
  using ReadGuard = ZmReadGuard<Lock>;

public:
  template <int Direction = ZmRBTreeGreaterEqual>
  using Iter = ZmRBTreeIter<Impl, Direction>;
  template <int Direction = ZmRBTreeGreaterEqual>
  using CIter = ZmRBTreeCIter<Impl, Direction>;

  using Node = Node_;
  using NodeRef = typename NodeFn::template Ref<Node>;
  using NodeMvRef = typename NodeFn::template MvRef<Node>;
  using NodePtr = Node *;

private:
  using NodeFn::nodeRef;
  using NodeFn::nodeDeref;
  using NodeFn::nodeDelete;
  using NodeFn::nodeAcquire;

  static KeyRet key(Node *node) {
    if (ZuLikely(node)) return node->Node::key();
    return ZuNullRef<Key, Cmp>();
  }
  static ValRet val(Node *node) {
    if (ZuLikely(node)) return node->Node::val();
    return ZuNullRef<Val, ValCmp>();
  }

public:
  ZmRBTree() = default;

  ZmRBTree(const ZmRBTree &) = delete;
  ZmRBTree &operator =(const ZmRBTree &) = delete;

  ZmRBTree(ZmRBTree &&tree) noexcept {
    Guard guard(tree.m_lock);
    m_root = tree.m_root;
    m_minimum = tree.m_minimum, m_maximum = tree.m_maximum;
    m_count = tree.m_count;
    tree.m_root = tree.m_minimum = tree.m_maximum = nullptr;
    tree.m_count = 0;
  }
  ZmRBTree &operator =(ZmRBTree &&tree) noexcept {
    unsigned count;
    Node *root, *minimum, *maximum;
    {
      Guard guard(tree.m_lock);
      root = tree.m_root, minimum = tree.m_minimum, maximum = tree.m_maximum;
      count = tree.m_count;
      tree.m_root = tree.m_minimum = tree.m_maximum = nullptr;
      tree.m_count = 0;
    }
    {
      clean_();
      m_root = root, m_minimum = minimum, m_maximum = maximum;
      m_count = count;
    }
    return *this;
  }

  ZmRBTree(Cmp cmp) : m_cmp{ZuMv(cmp)} { }

  ~ZmRBTree() { clean_(); }

  Lock &lock() const { return m_lock; }

  // intentionally unlocked and non-atomic
  unsigned count_() const { return m_count; }

private:
  template <typename U, typename V = Key>
  struct IsKey : public ZuBool<ZuIsConvertible<U, V>{}> { };
  template <typename U, typename R = void>
  using MatchKey = ZuIfT<IsKey<U>{}, R>;
  template <typename U, typename V = T, bool = ZuIs_<V, NodeBase>{}>
  struct IsData : public ZuBool<!IsKey<U>{} && ZuIsConvertible<U, V>{}> { };
  template <typename U, typename V>
  struct IsData<U, V, true> : public ZuFalse { };
  template <typename U, typename R = void>
  using MatchData = ZuIfT<IsData<U>{}, R>;

public:
  template <
    typename Key_ = Key,
    typename Cmp_ = Cmp,
    ZuIfT<ZmRBTree_IsStaticCmp<Key_, Cmp_>{}, int> = 0>
  static ZuInline auto cmp(const Key &l, const Key &r) {
    return Cmp::cmp(l, r);
  }
  template <
    typename Key_ = Key,
    typename Cmp_ = Cmp,
    ZuIfT<!ZmRBTree_IsStaticCmp<Key_, Cmp_>{}, int> = 0>
  auto ZuInline cmp(const Key &l, const Key &r) const {
    return m_cmp.cmp(l, r);
  }
  template <
    typename Key_ = Key,
    typename Cmp_ = Cmp,
    ZuIfT<ZmRBTree_IsStaticEquals<Key_, Cmp_>{}, int> = 0>
  static ZuInline auto equals(const Key &l, const Key &r) {
    return Cmp::equals(l, r);
  }
  template <
    typename Key_ = Key,
    typename Cmp_ = Cmp,
    ZuIfT<!ZmRBTree_IsStaticEquals<Key_, Cmp_>{}, int> = 0>
  auto ZuInline equals(const Key &l, const Key &r) const {
    return m_cmp.equals(l, r);
  }

private:
  template <
    typename P,
    typename Key_ = Key,
    typename Cmp_ = Cmp,
    ZuIfT<ZmRBTree_IsStaticCmp<Key_, Cmp_>{}, int> = 0>
  static ZuInline auto matchKey(const P &key) {
    return [&key](const Node *node) {
      return Cmp::cmp(node->Node::key(), key);
    };
  }
  template <
    typename P,
    typename Key_ = Key,
    typename Cmp_ = Cmp,
    ZuIfT<!ZmRBTree_IsStaticCmp<Key_, Cmp_>{}, int> = 0>
  ZuInline auto matchKey(const P &key) const {
    return [this, &key](const Node *node) {
      return m_cmp.cmp(node->Node::key(), key);
    };
  }
  template <
    typename P,
    typename Key_ = Key,
    typename Cmp_ = Cmp,
    ZuIfT<ZmRBTree_IsStaticCmp<Key_, Cmp_>{}, int> = 0>
  static ZuInline auto matchData(const P &data) {
    return [&data](const Node *node) {
      return Cmp::cmp(node->Node::key(), KeyAxor(data));
    };
  }
  template <
    typename P,
    typename Key_ = Key,
    typename Cmp_ = Cmp,
    ZuIfT<!ZmRBTree_IsStaticCmp<Key_, Cmp_>{}, int> = 0>
  ZuInline auto matchData(const P &data) const {
    return [this, &data](const Node *node) {
      return m_cmp.cmp(node->Node::key(), KeyAxor(data));
    };
  }

public:
  template <typename P>
  NodeRef add(P &&data) {
    Node *node = new Node(ZuFwd<P>(data));
    addNode(node);
    return node;
  }
  template <typename P0, typename P1>
  NodeRef add(P0 &&p0, P1 &&p1) {
    return add(ZuFwdTuple(ZuFwd<P0>(p0), ZuFwd<P1>(p1)));
  }
  template <bool _ = !ZuIsSame<NodeRef, Node *>{}>
  ZuIfT<_> addNode(const NodeRef &node_) { addNode(node_.ptr()); }
  template <bool _ = !ZuIsSame<NodeRef, Node *>{}>
  ZuIfT<_> addNode(NodeRef &&node_) {
    Node *node = ZuMv(node_).release();
    Guard guard(m_lock);
    addNode_(node);
  }
  void addNode(Node *node) {
    nodeRef(node);
    Guard guard(m_lock);
    addNode_(node);
  }
private:
  void addNode_(Node *newNode) {
    if constexpr (Unique) ZmAssert(!newNode->dup());
    ZmAssert(!newNode->left());
    ZmAssert(!newNode->right());
    ZmAssert(!newNode->parent());
    ZmAssert(!newNode->black());

    Node *node;

    if (!(node = m_root)) {
      newNode->setBlack();
      m_root = m_minimum = m_maximum = newNode;
      ++m_count;
      return;
    }

    bool minimum = true, maximum = true;
    const Key &key = newNode->Node::key();

    for (;;) {
      int c = cmp(node->Node::key(), key);

      if constexpr (!Unique) {
	if (!c) {
	  Node *child;

	  newNode->dup(child = node->dup());
	  if (child) child->parent(newNode);
	  node->dup(newNode);
	  newNode->parent(node);
	  ++m_count;
	  return;
	}
      }

      if (c >= 0) {
	if (!node->left()) {
	  node->left(newNode);
	  newNode->parent(node);
	  if (minimum) m_minimum = newNode;
	  break;
	}

	node = node->left();
	maximum = false;
      } else {
	if (!node->right()) {
	  node->right(newNode);
	  newNode->parent(node);
	  if (maximum) m_maximum = newNode;
	  break;
	}

	node = node->right();
	minimum = false;
      }
    }

    rebalance(newNode);
    ++m_count;
  }

  template <int Direction, typename MatchCmp, typename MatchEquals>
  ZuIfT<Direction == ZmRBTreeEqual, Node *> find_(
    MatchCmp matchCmp, MatchEquals matchEquals) const
  {
    Node *node = m_root;
    for (;;) {
      if (!node) return nullptr;
      int c = matchCmp(node);
      if (!c) {
	if constexpr (Unique) {
	  if (matchEquals(node)) return node;
	  return nullptr;
	} else {
	  while (!matchEquals(node)) if (!(node = node->dup())) break;
	  return node;
	}
      } else if (c > 0) {
	node = node->left();
      } else {
	node = node->right();
      }
    }
  }
  template <int Direction, typename MatchCmp, typename MatchEquals>
  ZuIfT<Direction == ZmRBTreeGreaterEqual, Node *> find_(
    MatchCmp matchCmp, MatchEquals) const
  {
    Node *node = m_root, *foundNode = nullptr;
    for (;;) {
      if (!node) return foundNode;
      int c = matchCmp(node);
      if (!c) {
	return node;
      } else if (c > 0) {
	foundNode = node;
	node = node->left();
      } else {
	node = node->right();
      }
    }
  }
  template <int Direction, typename MatchCmp, typename MatchEquals>
  ZuIfT<Direction == ZmRBTreeGreater, Node *> find_(
    MatchCmp matchCmp, MatchEquals) const
  {
    Node *node = m_root, *foundNode = nullptr;
    for (;;) {
      if (!node) return foundNode;
      int c = matchCmp(node);
      if (!c) {
	node = node->right();
      } else if (c > 0) {
	foundNode = node;
	node = node->left();
      } else {
	node = node->right();
      }
    }
  }
  template <int Direction, typename MatchCmp, typename MatchEquals>
  ZuIfT<Direction == ZmRBTreeLessEqual, Node *> find_(
    MatchCmp matchCmp, MatchEquals) const
  {
    Node *node = m_root, *foundNode = nullptr;
    for (;;) {
      if (!node) return foundNode;
      int c = matchCmp(node);
      if (!c) {
	return node;
      } else if (c > 0) {
	node = node->left();
      } else {
	foundNode = node;
	node = node->right();
      }
    }
  }
  template <int Direction, typename MatchCmp, typename MatchEquals>
  ZuIfT<Direction == ZmRBTreeLess, Node *> find_(
    MatchCmp matchCmp, MatchEquals) const
  {
    Node *node = m_root, *foundNode = nullptr;
    for (;;) {
      if (!node) return foundNode;
      int c = matchCmp(node);
      if (!c) {
	node = node->left();
      } else if (c > 0) {
	node = node->left();
      } else {
	foundNode = node;
	node = node->right();
      }
    }
  }

public:
  template <int Direction = ZmRBTreeEqual, typename P>
  MatchKey<P, NodeRef> find(const P &key) const {
    ReadGuard guard(m_lock);
    return find_<Direction>(matchKey(key),
      [](const Node *) constexpr { return true; });
  }
  template <int Direction = ZmRBTreeEqual, typename P>
  MatchData<P, NodeRef> find(const P &data) const {
    ReadGuard guard(m_lock);
    return find_<Direction>(matchData(data), [&data](const Node *node) {
      return node->Node::data() == data;
    });
  }
  template <int Direction = ZmRBTreeEqual, typename P0, typename P1>
  NodeRef find(P0 &&p0, P1 &&p1) {
    return find<Direction>(ZuFwdTuple(ZuFwd<P0>(p0), ZuFwd<P1>(p1)));
  }

  template <int Direction = ZmRBTreeEqual, typename P>
  MatchKey<P, Node *> findPtr(const P &key) const {
    ReadGuard guard(m_lock);
    return find_<Direction>(matchKey(key),
      [](const Node *) constexpr { return true; });
  }
  template <int Direction = ZmRBTreeEqual, typename P>
  MatchData<P, Node *> findPtr(const P &data) const {
    ReadGuard guard(m_lock);
    return find_<Direction>(matchData(data), [&data](const Node *node) {
      return node->Node::data() == data;
    });
  }

  template <int Direction = ZmRBTreeEqual, typename P>
  MatchKey<P, Key> findKey(const P &key) const {
    ReadGuard guard(m_lock);
    return key(find_<Direction>(matchKey(key),
	[](const Node *) constexpr { return true; }));
  }
  template <int Direction = ZmRBTreeEqual, typename P>
  MatchData<P, Key> findKey(const P &data) const {
    ReadGuard guard(m_lock);
    return key(find_<Direction>(matchData(data), [&data](const Node *node) {
      return node->Node::data() == data;
    }));
  }
  template <int Direction = ZmRBTreeEqual, typename P0, typename P1>
  Key findKey(P0 &&p0, P1 &&p1) {
    return findKey<Direction>(ZuFwdTuple(ZuFwd<P0>(p0), ZuFwd<P1>(p1)));
  }

  template <int Direction = ZmRBTreeEqual, typename P>
  MatchKey<P, Val> findVal(const P &key) const {
    ReadGuard guard(m_lock);
    return val(find_<Direction>(matchKey(key),
	[](const Node *) constexpr { return true; }));
  }
  template <int Direction = ZmRBTreeEqual, typename P>
  MatchData<P, Val> findVal(const P &data) const {
    ReadGuard guard(m_lock);
    return val(find_<Direction>(matchData(data), [&data](const Node *node) {
      return node->Node::data() == data;
    }));
  }
  template <int Direction = ZmRBTreeEqual, typename P0, typename P1>
  Val findVal(P0 &&p0, P1 &&p1) {
    return findVal<Direction>(ZuFwdTuple(ZuFwd<P0>(p0), ZuFwd<P1>(p1)));
  }

public:
  NodeRef minimum() const { ReadGuard guard(m_lock); return m_minimum; }
  Node *minimumPtr() const { ReadGuard guard(m_lock); return m_minimum; }
  Key minimumKey() const { ReadGuard guard(m_lock); return key(m_minimum); }
  Val minimumVal() const { ReadGuard guard(m_lock); return val(m_minimum); }

  NodeRef maximum() const { ReadGuard guard(m_lock); return m_maximum; }
  Node *maximumPtr() const { ReadGuard guard(m_lock); return m_maximum; }
  Key maximumKey() const { ReadGuard guard(m_lock); return key(m_maximum); }
  Val maximumVal() const { ReadGuard guard(m_lock); return val(m_maximum); }

  template <int Direction = ZmRBTreeEqual, typename P>
  MatchKey<P, NodeMvRef> del(const P &key) {
    ReadGuard guard(m_lock);
    Node *node = find_<Direction>(matchKey(key),
      [](const Node *) constexpr { return true; });
    if (!node) return nullptr;
    delNode_(node);
    return nodeAcquire(node);
  }
  template <int Direction = ZmRBTreeEqual, typename P>
  MatchData<P, NodeMvRef> del(const P &data) {
    ReadGuard guard(m_lock);
    Node *node = find_<Direction>(matchData(data), [&data](const Node *node) {
      return node->Node::data() == data;
    });
    if (!node) return nullptr;
    delNode_(node);
    return nodeAcquire(node);
  }
  template <int Direction = ZmRBTreeEqual, typename P0, typename P1>
  NodeMvRef del(P0 &&p0, P1 &&p1) {
    return del<Direction>(ZuFwdTuple(ZuFwd<P0>(p0), ZuFwd<P1>(p1)));
  }

  template <int Direction = ZmRBTreeEqual, typename P>
  MatchKey<P, Key> delKey(const P &key) {
    ReadGuard guard(m_lock);
    NodeMvRef node = find_<Direction>(matchKey(key),
      [](const Node *) constexpr { return true; });
    if (!node) return ZuNullRef<Key, Cmp>();
    delNode_(node);
    return ZuMv(*node).Node::key();
  }
  template <int Direction = ZmRBTreeEqual, typename P>
  MatchData<P, Key> delKey(const P &data) {
    ReadGuard guard(m_lock);
    NodeMvRef node =
      find_<Direction>(matchData(data), [&data](const Node *node) {
	return node->Node::data() == data;
      });
    if (!node) return ZuNullRef<Key, Cmp>();
    delNode_(node);
    return ZuMv(*node).Node::key();
  }
  template <int Direction = ZmRBTreeEqual, typename P0, typename P1>
  Key delKey(P0 &&p0, P1 &&p1) {
    return delKey<Direction>(ZuFwdTuple(ZuFwd<P0>(p0), ZuFwd<P1>(p1)));
  }

  template <int Direction = ZmRBTreeEqual, typename P>
  MatchKey<P, Val> delVal(const P &key) {
    ReadGuard guard(m_lock);
    NodeMvRef node = find_<Direction>(matchKey(key),
      [](const Node *) constexpr { return true; });
    if (!node) return ZuNullRef<Val, ValCmp>();
    delNode_(node);
    return ZuMv(*node).Node::val();
  }
  template <int Direction = ZmRBTreeEqual, typename P>
  MatchData<P, Val> delVal(const P &data) {
    ReadGuard guard(m_lock);
    NodeMvRef node =
      find_<Direction>(matchData(data), [&data](const Node *node) {
	return node->Node::data() == data;
      });
    if (!node) return ZuNullRef<Val, ValCmp>();
    delNode_(node);
    return ZuMv(*node).Node::val();
  }
  template <int Direction = ZmRBTreeEqual, typename P0, typename P1>
  Val delVal(P0 &&p0, P1 &&p1) {
    return delVal<Direction>(ZuFwdTuple(ZuFwd<P0>(p0), ZuFwd<P1>(p1)));
  }

  NodeMvRef delNode(Node *node) {
    if (ZuUnlikely(!node)) return {};
    Guard guard(m_lock);
    if (!node->right() && !node->left() && !node->parent() && node != m_root)
      return {};
    delNode_(node);
    return nodeAcquire(node);
  }

private:
  void delNode_(Node *node) {
    if constexpr (!Unique) {
      Node *parent = node->parent();
      Node *dup = node->dup();

      if (parent && parent->dup() == node) {
	parent->dup(dup);
	if (dup) dup->parent(parent);
	--m_count;
	node->clearDup();
	return;
      }
      if (dup) {
	{
	  Node *child;

	  dup->left(child = node->left());
	  if (child) {
	    node->left(nullptr);
	    child->parent(dup);
	  }
	  dup->right(child = node->right());
	  if (child) {
	    node->right(nullptr);
	    child->parent(dup);
	  }
	}
	if (!parent) {
	  m_root = dup;
	  dup->parent(0);
	} else if (node == parent->right()) {
	  parent->right(dup);
	  dup->parent(parent);
	} else {
	  parent->left(dup);
	  dup->parent(parent);
	}
	dup->black(node);
	if (node == m_minimum) m_minimum = dup;
	if (node == m_maximum) m_maximum = dup;
	--m_count;
	node->clearDup();
	return;
      }
    }
    delRebalance(node);
    node->clear();
    --m_count;
  }

public:
  template <int Direction = ZmRBTreeGreaterEqual>
  auto iter() {
    return Iter<Direction>{static_cast<Impl &>(*this)};
  }
  template <int Direction = ZmRBTreeGreaterEqual, typename P>
  auto iter(P &&key) {
    return Iter<Direction>{static_cast<Impl &>(*this), ZuFwd<P>(key)};
  }
  template <int Direction = ZmRBTreeGreaterEqual>
  auto citer() const {
    return CIter<Direction>{static_cast<const Impl &>(*this)};
  }
  template <int Direction = ZmRBTreeGreaterEqual, typename P>
  auto citer(P &&key) const {
    return CIter<Direction>{static_cast<const Impl &>(*this), ZuFwd<P>(key)};
  }

// clean tree

  void clean() { clean([](auto) { }); }
  template <typename L> void clean(L &&l) {
    Guard guard(m_lock);
    clean_(ZuFwd<L>(l));
    m_minimum = m_maximum = m_root = nullptr;
    m_count = 0;
  }
private:
  void clean_() { clean_([](auto) { }); }
  template <typename L> void clean_(L &&l) {
    Node *node = m_minimum, *next;
    if (!node) return;
    do {
      if (next = node->left()) { node = next; continue; }
      if (next = node->dup()) { node = next; continue; }
      if (next = node->right()) { node = next; continue; }
      if (next = node->parent()) {
	if (node == next->left())
	  next->left(nullptr);
	else if constexpr (!Unique) {
	  if (node == next->dup())
	    next->dup(nullptr);
	  else
	    next->right(nullptr);
	} else
	  next->right(nullptr);
      }
      ZuFwd<L>(l)(NodeMvRef{nodeAcquire(node)});
      node = next;
    } while (node);
  }

  void rotateRight(Node *node, Node *parent) {
    Node *left = node->left();
    Node *mid = left->right();

  // move left to the right, under node's parent
  // (make left the root if node is the root)

    if (parent) {
      if (parent->left() == node)
	parent->left(left);
      else
	parent->right(left);
    } else
      m_root = left;
    left->parent(parent);

  // node descends to left's right

    left->right(node), node->parent(left);

  // mid switches from left's right to node's left

    node->left(mid); if (mid) mid->parent(node);
  }

  void rotateLeft(Node *node, Node *parent) {
    Node *right = node->right();
    Node *mid = right->left();

  // move right to the left, under node's parent
  // (make right the root if node is the root)

    if (parent) {
      if (parent->right() == node)
	parent->right(right);
      else
	parent->left(right);
    } else
      m_root = right;
    right->parent(parent);

  // node descends to right's left

    right->left(node), node->parent(right);

  // mid switches from right's left to node's right

    node->right(mid); if (mid) mid->parent(node);
  }

  void rebalance(Node *node) {

  // rebalance until we hit a black node (the root is always black)

    for (;;) {
      Node *parent = node->parent();

      if (!parent) { node->setBlack(); return; }// force root to black

      if (parent->black()) return;

      Node *gParent = parent->parent();

      if (parent == gParent->left()) {
	Node *uncle = gParent->right();

	if (uncle && !uncle->black()) {
	  parent->setBlack();
	  uncle->setBlack();
	  (node = gParent)->clrBlack();
	} else {
	  if (node == parent->right()) {
	    rotateLeft(node = parent, gParent);
	    gParent = (parent = node->parent())->parent();
	  }
	  parent->setBlack();
	  gParent->clrBlack();
	  rotateRight(gParent, gParent->parent());
	  m_root->setBlack();			// force root to black
	  return;
	}
      } else {
	Node *uncle = gParent->left();

	if (uncle && !uncle->black()) {
	  parent->setBlack();
	  uncle->setBlack();
	  (node = gParent)->clrBlack();
	} else {
	  if (node == parent->left()) {
	    rotateRight(node = parent, gParent);
	    gParent = (parent = node->parent())->parent();
	  }
	  parent->setBlack();
	  gParent->clrBlack();
	  rotateLeft(gParent, gParent->parent());
	  m_root->setBlack();			// force root to black
	  return;
	}
      }
    }
  }

  void delRebalance(Node *node) {
    Node *successor = node;
    Node *child, *parent;

    if (!successor->left())
      child = successor->right();
    else if (!successor->right())
      child = successor->left();
    else {
      successor = successor->right();
      while (successor->left()) successor = successor->left();
      child = successor->right();
    }

    if (successor != node) {
      node->left()->parent(successor);
      successor->left(node->left());
      if (successor != node->right()) {
	parent = successor->parent();
	if (child) child->parent(parent);
	successor->parent()->left(child);
	successor->right(node->right());
	node->right()->parent(successor);
      } else
	parent = successor;

      Node *childParent = parent;

      parent = node->parent();

      if (!parent)
	m_root = successor;
      else if (node == parent->left())
	parent->left(successor);
      else
	parent->right(successor);
      successor->parent(parent);

      bool black = node->black();

      node->black(successor);
      successor->black(black);

      successor = node;

      parent = childParent;
    } else {
      parent = node->parent();

      if (child) child->parent(parent);

      if (!parent)
	m_root = child;
      else if (node == parent->left())
	parent->left(child);
      else
	parent->right(child);

      if (node == m_minimum) {
	if (!node->right())
	  m_minimum = parent;
	else {
	  Node *minimum = child;

	  do {
	    m_minimum = minimum;
	  } while (minimum = minimum->left());
	}
      }

      if (node == m_maximum) {
	if (!node->left())
	  m_maximum = parent;
	else {
	  Node *maximum = child;

	  do {
	    m_maximum = maximum;
	  } while (maximum = maximum->right());
	}
      }
    }

    if (successor->black()) {
      Node *sibling;

      while (parent && (!child || child->black()))
	if (child == parent->left()) {
	  sibling = parent->right();
	  if (!sibling->black()) {
	    sibling->setBlack();
	    parent->clrBlack();
	    rotateLeft(parent, parent->parent());
	    sibling = parent->right();
	  }
	  if ((!sibling->left() ||
		sibling->left()->black()) &&
	      (!sibling->right() ||
		sibling->right()->black())) {
	    sibling->clrBlack();
	    child = parent;
	    parent = child->parent();
	  } else {
	    if (!sibling->right() ||
		sibling->right()->black()) {
	      if (sibling->left())
		sibling->left()->setBlack();
	      sibling->clrBlack();
	      rotateRight(sibling, parent);
	      sibling = parent->right();
	    }
	    sibling->black(parent);
	    parent->setBlack();
	    if (sibling->right())
	      sibling->right()->setBlack();
	    rotateLeft(parent, parent->parent());
	    break;
	  }
	} else {
	  sibling = parent->left();
	  if (!sibling->black()) {
	    sibling->setBlack();
	    parent->clrBlack();
	    rotateRight(parent, parent->parent());
	    sibling = parent->left();
	  }
	  if ((!sibling->right() ||
		sibling->right()->black()) &&
	      (!sibling->left() ||
		sibling->left()->black())) {
	    sibling->clrBlack();
	    child = parent;
	    parent = child->parent();
	  } else {
	    if (!sibling->left() ||
		sibling->left()->black()) {
	      if (sibling->right())
		sibling->right()->setBlack();
	      sibling->clrBlack();
	      rotateLeft(sibling, parent);
	      sibling = parent->left();
	    }
	    sibling->black(parent);
	    parent->setBlack();
	    if (sibling->left())
	      sibling->left()->setBlack();
	    rotateRight(parent, parent->parent());
	    break;
	  }
	}
      if (child) child->setBlack();
    }
  }

public:
  Node *next(Node *node) const {
    Node *next;

    if constexpr (!Unique) {
      if (next = node->dup()) return next;

      if (next = node->parent())
	while (node == next->dup()) {
	  node = next;
	  if (!(next = node->parent())) break;
	}
    }

    if (next = node->right()) {
      node = next;
      while (node = node->left()) next = node;
      return next;
    }

    if (!(next = node->parent())) return nullptr;

    while (node == next->right()) {
      node = next;
      if (!(next = node->parent())) return nullptr;
    }

    return next;
  }

  Node *prev(Node *node) const {
    Node *prev;

    if constexpr (!Unique) {
      if (prev = node->dup()) return prev;

      if (prev = node->parent())
	while (node == prev->dup()) {
	  node = prev;
	  if (!(prev = node->parent())) break;
	}
    }

    if (prev = node->left()) {
      node = prev;
      while (node = node->right()) prev = node;
      return prev;
    }

    if (!(prev = node->parent())) return nullptr;

    while (node == prev->left()) {
      node = prev;
      if (!(prev = node->parent())) return nullptr;
    }

    return prev;
  }

private:

// iterator functions

  template <int Direction>
  using Iter_ = ZmRBTreeIter_<ZmRBTree, Direction>;

  template <int Direction>
  ZuIfT<(Direction >= 0)> iterBegin(
      ZmRBTreeIter_<Impl, Direction> &iter) {
    iter.m_node = m_minimum;
  }
  template <int Direction>
  ZuIfT<(Direction < 0)> iterBegin(
      ZmRBTreeIter_<Impl, Direction> &iter) {
    iter.m_node = m_maximum;
  }
  template <int Direction, typename P>
  void iterBegin(ZmRBTreeIter_<Impl, Direction> &iter, const P &key) {
    iter.m_node = find_<Direction>(matchKey(key),
      [](const Node *) constexpr { return true; });
  }

  template <int Direction>
  ZuIfT<(Direction > 0), Node *> iterate(
      ZmRBTreeIter_<Impl, Direction> &iter) {
    Node *node = iter.m_node;
    if (!node) return nullptr;
    iter.m_node = next(node);
    return node;
  }
  template <int Direction>
  ZuIfT<(!Direction), Node *> iterate(
      ZmRBTreeIter_<Impl, Direction> &iter) {
    Node *node = iter.m_node;
    if (!node) return nullptr;
    iter.m_node = node->dup();
    return node;
  }
  template <int Direction>
  ZuIfT<(Direction < 0), Node *> iterate(
      ZmRBTreeIter_<Impl, Direction> &iter) {
    Node *node = iter.m_node;
    if (!node) return nullptr;
    iter.m_node = prev(node);
    return node;
  }

  NodeMvRef iterDel(Node *node) {
    if (ZuUnlikely(!node)) return nullptr;
    delNode_(node);
    return nodeAcquire(node);
  }

  Cmp		m_cmp;
  mutable Lock	m_lock;
    Node	  *m_root = nullptr;
    Node	  *m_minimum = nullptr;
    Node	  *m_maximum = nullptr;
    unsigned	  m_count = 0;
};

template <typename P0, typename P1, typename NTP = ZmRBTree_Defaults>
using ZmRBTreeKV =
  ZmRBTree<ZuTuple<P0, P1>,
    ZmRBTreeKeyVal<ZuTupleAxor<0>(), ZuTupleAxor<1>(), NTP>>;

#define ZmRBTreeDerive(Name, T_, ...) \
  ZuDerive(Name ## _NTP, (__VA_ARGS__)); \
  using Name ## _Node = ZmRBTree_NodeT<ZuPP_Strip(T_), Name ## _NTP>::T; \
  ZuDerive(Name, \
    (ZmRBTree<ZuPP_Strip(T_), Name ## _NTP, Name ## _Node, Name>));

#define ZmRBTreeDeriveT_4(Args, Name, T_, NTP) \
  ZuPP_PfxTypename(Args) ZuDerive(Name ## _NTP, NTP); \
  ZuPP_PfxTypename(Args) using Name ## _Node = \
    ZmRBTree_NodeT<ZuPP_Strip(T_), Name ## _NTP<ZuPP_Strip(Args)>>::T; \
  ZuPP_PfxTypename(Args) ZuDerive(Name, \
    (ZmRBTree<ZuPP_Strip(T_), Name ## _NTP<ZuPP_Strip(Args)>, \
      Name ## _Node<ZuPP_Strip(Args)>, Name<ZuPP_Strip(Args)>>));
#define ZmRBTreeDeriveT_5(Args, XArgs, Name, T_, NTP) \
  ZuPP_PfxTypename(Args) ZuDerive(Name ## _NTP, NTP); \
  ZuPP_PfxTypename(Args) using Name ## _Node = \
    ZmRBTree_NodeT<ZuPP_Strip(T_), Name ## _NTP<ZuPP_Strip(Args)>>::T; \
  ZuPP_PfxTypename((ZuPP_Strip(Args) ZuPP_StripAppend(XArgs))) ZuDerive(Name, \
    (ZmRBTree<ZuPP_Strip(T_), Name ## _NTP<ZuPP_Strip(Args)>, \
      Name ## _Node<ZuPP_Strip(Args)>, \
      Name<ZuPP_Strip(Args) ZuPP_StripAppend(XArgs)>>));
#define ZmRBTreeDeriveT_N(_0, _1, _2, _3, _4, Fn, ...) Fn
#define ZmRBTreeDeriveT(...) \
  ZmRBTreeDeriveT_N(__VA_ARGS__, \
    ZmRBTreeDeriveT_5(__VA_ARGS__), \
    ZmRBTreeDeriveT_4(__VA_ARGS__))

#define ZmRBTreeKVDerive(Name, P0_, P1_, ...) \
  ZuDerive(Name ## _NTP, \
    (ZmRBTreeKeyVal<ZuTupleAxor<0>(), ZuTupleAxor<1>(), __VA_ARGS__>)); \
  using Name ## _Node = ZmRBTree_NodeT< \
    ZuTuple<ZuPP_Strip(P0_), ZuPP_Strip(P1_)>, \
    Name ## _NTP>::T; \
  ZuDerive(Name, \
    (ZmRBTree<ZuTuple<ZuPP_Strip(P0_), ZuPP_Strip(P1_)>, \
      Name ## _NTP, Name ## _Node, Name>))

#endif /* ZmRBTree_HH */
