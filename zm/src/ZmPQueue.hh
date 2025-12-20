//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// priority queue (skip list)
// - compile-time deterministic skip list
// - optimized for message sequence reassembly
// - handles gaps, overlaps, out-of-order, etc.

// The skip list is a sequence of ordered items.
// Each item is a run-length encoded sequence of
// one or more adjacent elements, each of which is individually numbered.
// This run-length encoding allows highly efficient duplicate detection,
// enqueuing of both in- and out- of order items as well as in-order
// deletion (dequeuing) without tree re-balancing.
//
// if used for sequences of packets with bytecount sequence numbering,
// the elements are chars, the items are packet buffers, and the key
// is the bytecount; other possibilities are elements being individual
// messages within a containing buffer, as in FIX, OUCH, ITCH and similar
// protocols

#ifndef ZmPQueue_HH
#define ZmPQueue_HH

#ifndef ZmLib_HH
#include <zlib/ZmLib.hh>
#endif

#include <zlib/ZuCmp.hh>
#include <zlib/ZuTuple.hh>
#include <zlib/ZuBox.hh>

#include <zlib/ZmAssert.hh>
#include <zlib/ZmLock.hh>
#include <zlib/ZmNoLock.hh>
#include <zlib/ZmGuard.hh>
#include <zlib/ZmObject.hh>
#include <zlib/ZmRef.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmNode.hh>
#include <zlib/ZmNodeFn.hh>

// the application will normally substitute ZmPQueueDefaultFn with
// a type that is specific to the queued Item; it must conform to
// this interface:

// Key - the type of the key (the sequence number)
// key() - return the key
// length() - return the length (key element count)

// E.g. for packets/fragments using a bytecount (TCP, etc.)
// the value would be a buffer large enough to hold the packet/fragment;
// Key would be uint32_t/uint64_t; key() would return the key in the header;
// length() would return the number of bytes in the packet/fragment

constexpr auto ZmPQueueDefaultKeyAxor() {
  return []<typename T>(T &&v) -> decltype(auto) { return ZuFwd<T>(v).key(); };
}
constexpr auto ZmPQueueDefaultLenAxor() {
  return []<typename T>(const T &v) -> unsigned { return v.length(); };
}
template <
  typename Item,
  auto KeyAxor_ = ZmPQueueDefaultKeyAxor(),
  auto LenAxor_ = ZmPQueueDefaultLenAxor()>
class ZmPQueueDefaultFn {
public:
  // KeyAxor is the accessor for the key (sequence number)
  static constexpr auto KeyAxor = KeyAxor_;

  // LenAxor is the accessor for the length
  static constexpr auto LenAxor = LenAxor_;

  // Key is the type of the sequence number returned by KeyAxor()
  using Key = ZuRDecay<decltype(KeyAxor(ZuDeclVal<const Item &>()))>;

  ZmPQueueDefaultFn(Item &item) : m_item(item) { }

  ZuInline Key key() const { return KeyAxor(m_item); }
  ZuInline unsigned length() const { return LenAxor(m_item); }

  // clipHead()/clipTail() remove elements from the item's head or tail
  // to resolve overlaps
  // - if Overlap is false these functions are unused and do not need to exist
  // - clipHead()/clipTail() can just return 1 if the item length is always 1,
  //   or do nothing and return the unchanged length if items never overlap
  //   (in this case it is more performant to use ZmPQueueOverlap<false>)
  // - these functions return the length remaining in the item
  unsigned clipHead(unsigned n) { return m_item.clipHead(n); }
  unsigned clipTail(unsigned n) { return m_item.clipTail(n); }

  // write() overwrites overlapping data from item
  // - if Overlap is false this function is unused and does not need to exist
  void write(const ZmPQueueDefaultFn &item) {
    m_item.write(item.m_item);
  }

private:
  Item	&m_item;
};

// uses NTP (named template parameters)

// NTP defaults
struct ZmPQueue_Defaults {
  enum { Bits = 3, Levels = 3 };
  template <typename Item> using ZmPQueueFnT = ZmPQueueDefaultFn<Item>;
  enum { Stats = 1 };
  enum { Overlap = 1 };
  using Lock = ZmNoLock;
  using Node = ZuEmpty;
  enum { Shadow = 0 };
  struct HeapID : public ZuStringT<"ZmPQueue"> { };
  enum { Sharded = 0 };
};

// ZmPQueueBits - change skip list factor (power of 2)
template <unsigned Bits_, class NTP = ZmPQueue_Defaults>
struct ZmPQueueBits : public NTP {
  enum { Bits = Bits_ };
};

// ZmPQueueLevels - change skip list #levels
template <unsigned Levels_, class NTP = ZmPQueue_Defaults>
struct ZmPQueueLevels : public NTP {
  enum { Levels = Levels_ };
};

// ZmPQueueFn - override queuing functions for the type
template <typename Fn_, class NTP = ZmPQueue_Defaults>
struct ZmPQueueFn : public NTP {
  template <typename> using ZmPQueueFnT = Fn_;
};

// ZmPQueueStats - enable/disable statistics gathering
template <bool Stats_, class NTP = ZmPQueue_Defaults>
struct ZmPQueueStats : public NTP {
  enum { Stats = Stats_ };
};

// ZmPQueueOverlap - items may overlap each other
template <bool Overlap_, class NTP = ZmPQueue_Defaults>
struct ZmPQueueOverlap : public NTP {
  enum { Overlap = Overlap_ };
};

// ZmPQueueLock - the lock type used (ZmRWLock will permit concurrent reads)
template <class Lock_, class NTP = ZmPQueue_Defaults>
struct ZmPQueueLock : public NTP {
  using Lock = Lock_;
};

// ZmPQueueNode - the base type for nodes
template <typename Node_, typename NTP = ZmPQueue_Defaults>
struct ZmPQueueNode : public NTP {
  using Node = Node_;
};

// ZmPQueueShadow - shadow nodes, do not manage ownership
template <bool Shadow_, typename NTP = ZmPQueue_Defaults>
struct ZmPQueueShadow;
template <typename NTP>
struct ZmPQueueShadow<true, NTP> : public NTP {
  enum { Shadow = true };
  struct HeapID : public ZuStringT<""> { };
};
template <typename NTP>
struct ZmPQueueShadow<false, NTP> : public NTP {
  enum { Shadow = false };
};

// ZmPQueueHeapID - the heap ID
template <ZuString HeapID_, class NTP = ZmPQueue_Defaults>
struct ZmPQueueHeapID : public NTP {
  using HeapID = ZuStringT<HeapID_>;
};

// ZmPQueueSharded - sharded heap
template <bool Sharded_, typename NTP = ZmPQueue_Defaults>
struct ZmPQueueSharded : public NTP {
  enum { Sharded = Sharded_ };
};

// ZmPQueue node
template <typename Node, unsigned Levels>
struct ZmPQueue_NodeExt {
  ZmPQueue_NodeExt() {
    memset(m_next, 0, sizeof(Node *) * Levels);
    memset(m_prev, 0, sizeof(Node *) * Levels);
  }

  // access to Node instances is always guarded, so no need to protect
  // the returned object against concurrent deletion
  Node *next(unsigned i) { return m_next[i]; }
  Node *prev(unsigned i) { return m_prev[i]; }

  void next(unsigned i, Node *n) { m_next[i] = n; }
  void prev(unsigned i, Node *n) { m_prev[i] = n; }

  Node	*m_next[Levels];
  Node	*m_prev[Levels];
};

// utility namespace
namespace ZmPQueue_ {
  template <int, int, typename = void> struct First;
  template <int Levels, typename T_>
  struct First<0, Levels, T_> { using T = T_; };
  template <int, int, typename T_ = void> struct Next { using T = T_; };
  template <int Levels, typename T_> struct Next<0, Levels, T_> { };
  template <int Levels, typename T_> struct Next<Levels, Levels, T_> { };
  template <int, int, typename T_ = void> struct NotLast { using T = T_; };
  template <int Levels, typename T_> struct NotLast<Levels, Levels, T_> { };
  template <int, int, typename = void> struct Last;
  template <int Levels, typename T_>
  struct Last<Levels, Levels, T_> { using T = void; };
};

template <bool Stats> class ZmPQueue_Stats {
public:
  // retrieve stats
  void stats(
      uint64_t &inCount, uint64_t &inElems, 
      uint64_t &outCount, uint64_t &outElems) const {
    inCount = m_inCount;
    inElems = m_inElems;
    outCount = m_outCount;
    outElems = m_outElems;
  }

protected:
  void inCount(unsigned length) {
    ++m_inCount;
    m_inElems += length;
  }
  void outCount(unsigned length) {
    ++m_outCount;
    m_outElems += length;
  }

private:
  uint64_t	  m_inCount = 0;
  uint64_t	  m_inElems = 0;
  uint64_t	  m_outCount = 0;
  uint64_t	  m_outElems = 0;
};
template <> class ZmPQueue_Stats<false> {
protected:
  ZuInline void inCount(unsigned) { }
  ZuInline void outCount(unsigned) { }
};

template <typename Item_, class NTP = ZmPQueue_Defaults>
class ZmPQueue :
  public ZmNodeFn<NTP::Shadow, typename NTP::Node>,
  public ZmPQueue_Stats<NTP::Stats> {
public:
  using Item = Item_;
  enum { Bits = NTP::Bits };
  enum { Levels = NTP::Levels };
  using Fn = typename NTP::template ZmPQueueFnT<Item>;
  static constexpr auto KeyAxor = Fn::KeyAxor;
  using Key = typename Fn::Key;
  enum { Stats = NTP::Stats };
  enum { Overlap = NTP::Overlap };
  using Lock = typename NTP::Lock;
  using NodeBase = typename NTP::Node;
  enum { Shadow = NTP::Shadow };
  using HeapID = NTP::HeapID;
  enum { Sharded = NTP::Sharded };

  ZuDeclTuple(Gap,
      ((ZuBox0(Key)), key),
      ((ZuBox0(unsigned)), length));

private:
  using NodeFn = ZmNodeFn<Shadow, NodeBase>;

  using Guard = ZmGuard<Lock>;
  using ReadGuard = ZmReadGuard<Lock>;

public:
  struct Node;
  using Node_ = ZmNode<
    Item, KeyAxor, ZuDefaultAxor(), NodeBase, ZmPQueue_NodeExt<Node, Levels>,
    HeapID, Sharded>;
  ZuDerive(Node, Node_);
  using NodeExt = ZmPQueue_NodeExt<Node, Levels>;
  using NodeRef = typename NodeFn::template Ref<Node>;
  using NodePtr = Node *;

private:
  using NodeFn::nodeRef;
  using NodeFn::nodeDeref;
  using NodeFn::nodeDelete;
  using NodeFn::nodeRelease;

public:
  ZmPQueue() : m_headKey(Key{}), m_tailKey(Key{}) {
    memset(m_head, 0, sizeof(Node *) * Levels);
    memset(m_tail, 0, sizeof(Node *) * Levels);
  }
  ZmPQueue(Key head) : m_headKey(head), m_tailKey(head) {
    memset(m_head, 0, sizeof(Node *) * Levels);
    memset(m_tail, 0, sizeof(Node *) * Levels);
  }
  ZmPQueue(const ZmPQueue &) = delete;
  ZmPQueue &operator =(const ZmPQueue &) = delete;
  ZmPQueue(ZmPQueue &&) = delete;
  ZmPQueue &operator =(ZmPQueue &&) = delete;

  ~ZmPQueue() { clean_(); }

private:
  // add at tail
  template <int Level>
  typename ZmPQueue_::First<Level, Levels>::T
  addTail__(Node *node, unsigned addSeqNo) {
    Node *prev;
    node->NodeExt::next(0, nullptr);
    node->NodeExt::prev(0, prev = m_tail[0]);
    m_tail[0] = node;
    if (!prev)
      m_head[0] = node;
    else
      prev->next(0, node);
    addTail__<1>(node, addSeqNo);
  }
  template <int Level>
  typename ZmPQueue_::Next<Level, Levels>::T
  addTail__(Node *node, unsigned addSeqNo) {
    node->NodeExt::next(Level, nullptr);
    if (ZuUnlikely(!(addSeqNo & ((1U<<(Bits * Level)) - 1)))) {
      Node *prev;
      node->NodeExt::prev(Level, prev = m_tail[Level]);
      m_tail[Level] = node;
      if (!prev)
	m_head[Level] = node;
      else
	prev->next(Level, node);
      addTail__<Level + 1>(node, addSeqNo);
      return;
    }
    node->NodeExt::prev(Level, nullptr);
    addTailEnd_<Level + 1>(node, addSeqNo);
  }
  template <int Level>
  typename ZmPQueue_::Next<Level, Levels>::T
  addTailEnd_(Node *node, unsigned addSeqNo) {
    node->NodeExt::next(Level, nullptr);
    node->NodeExt::prev(Level, nullptr);
    addTailEnd_<Level + 1>(node, addSeqNo);
  }
  template <int Level>
  typename ZmPQueue_::Last<Level, Levels>::T
  addTail__(Node *, unsigned) { }
  template <int Level>
  typename ZmPQueue_::Last<Level, Levels>::T
  addTailEnd_(Node *, unsigned) { }

  // add at head
  template <int Level>
  typename ZmPQueue_::First<Level, Levels>::T
  addHead__(Node *node, unsigned addSeqNo) {
    Node *next;
    node->NodeExt::prev(0, nullptr);
    node->NodeExt::next(0, next = m_head[0]);
    m_head[0] = node;
    if (!next)
      m_tail[0] = node;
    else
      next->prev(0, node);
    addHead__<1>(node, addSeqNo);
  }
  template <int Level>
  typename ZmPQueue_::Next<Level, Levels>::T
  addHead__(Node *node, unsigned addSeqNo) {
    node->NodeExt::prev(Level, nullptr);
    if (ZuUnlikely(!(addSeqNo & ((1U<<(Bits * Level)) - 1)))) {
      Node *next;
      node->NodeExt::next(Level, next = m_head[Level]);
      m_head[Level] = node;
      if (!next)
	m_tail[Level] = node;
      else
	next->prev(Level, node);
      addHead__<Level + 1>(node, addSeqNo);
      return;
    }
    node->NodeExt::next(Level, nullptr);
    addHeadEnd_<Level + 1>(node, addSeqNo);
  }
  template <int Level>
  typename ZmPQueue_::Next<Level, Levels>::T
  addHeadEnd_(Node *node, unsigned addSeqNo) {
    node->NodeExt::prev(Level, nullptr);
    node->NodeExt::next(Level, nullptr);
    addHeadEnd_<Level + 1>(node, addSeqNo);
  }
  template <int Level>
  typename ZmPQueue_::Last<Level, Levels>::T
  addHead__(Node *, unsigned) { }
  template <int Level>
  typename ZmPQueue_::Last<Level, Levels>::T
  addHeadEnd_(Node *, unsigned) { }

  // add at key position, following find
  template <int Level>
  typename ZmPQueue_::First<Level, Levels>::T addAt_(
      Node *node, Node **next_, unsigned addSeqNo) {
    Node *next = next_[0];
    Node *prev = next ? next->prev(0) : m_tail[0];
    node->NodeExt::next(0, next);
    if (ZuUnlikely(!next))
      m_tail[0] = node;
    else
      next->prev(0, node);
    node->NodeExt::prev(0, prev);
    if (ZuUnlikely(!prev))
      m_head[0] = node;
    else
      prev->next(0, node);
    addAt_<1>(node, next_, addSeqNo);
  }
  template <int Level>
  typename ZmPQueue_::Next<Level, Levels>::T addAt_(
      Node *node, Node **next_, unsigned addSeqNo) {
    if (ZuUnlikely(!(addSeqNo & ((1U<<(Bits * Level)) - 1)))) {
      Node *next = next_[Level];
      Node *prev = next ? next->prev(Level) : m_tail[Level];
      node->NodeExt::next(Level, next);
      if (ZuUnlikely(!next))
	m_tail[Level] = node;
      else
	next->prev(Level, node);
      node->NodeExt::prev(Level, prev);
      if (ZuUnlikely(!prev))
	m_head[Level] = node;
      else
	prev->next(Level, node);
      addAt_<Level + 1>(node, next_, addSeqNo);
    } else {
      node->NodeExt::prev(Level, nullptr);
      node->NodeExt::next(Level, nullptr);
      addAtEnd_<Level + 1>(node, next_, addSeqNo);
    }
  }
  template <int Level>
  typename ZmPQueue_::Next<Level, Levels>::T addAtEnd_(
      Node *node, Node **next_, unsigned addSeqNo) {
    node->NodeExt::prev(Level, nullptr);
    node->NodeExt::next(Level, nullptr);
    addAtEnd_<Level + 1>(node, next_, addSeqNo);
  }
  template <int Level>
  typename ZmPQueue_::Last<Level, Levels>::T addAt_(
      Node *, Node **, unsigned) { }
  template <int Level>
  typename ZmPQueue_::Last<Level, Levels>::T addAtEnd_(
      Node *, Node **, unsigned) { }

  // delete head
  template <int Level>
  void delHead__() {
    Node *next(m_head[Level]->next(Level));
    if (!(m_head[Level] = next))
      m_tail[Level] = 0;
    else
      next->prev(Level, nullptr);
  }
  template <int Level>
  typename ZmPQueue_::First<Level, Levels>::T delHead_() {
    delHead_<1>();
    delHead__<0>();
  }
  template <int Level>
  typename ZmPQueue_::Next<Level, Levels>::T delHead_() {
    if (m_head[Level] != m_head[0]) return;
    delHead_<Level + 1>();
    delHead__<Level>();
  }
  template <int Level>
  typename ZmPQueue_::Last<Level, Levels>::T delHead_() { }

  // delete result from find
  template <int Level>
  Node *del__(Node *node) {
    Node *next(node->NodeExt::next(Level));
    Node *prev(node->NodeExt::prev(Level));
    if (ZuUnlikely(!prev))
      m_head[Level] = next;
    else
      prev->next(Level, next);
    if (ZuUnlikely(!next))
      m_tail[Level] = prev;
    else
      next->prev(Level, prev);
    return next;
  }
  template <int Level>
  typename ZmPQueue_::First<Level, Levels>::T del_(Node **next) {
    del_<1>(next);
    next[0] = del__<Level>(next[0]);
  }
  template <int Level>
  typename ZmPQueue_::Next<Level, Levels>::T del_(Node **next) {
    if (next[Level] != next[0]) return;
    del_<Level + 1>(next);
    next[Level] = del__<Level>(next[Level]);
  }
  template <int Level>
  typename ZmPQueue_::Last<Level, Levels>::T del_(Node **) { }

  // find
  bool findDir_(Key key) const {
    if (key < m_headKey) return true;
    if (key >= m_tailKey) return false;
    return key - m_headKey <= m_tailKey - key;
  }
  bool findDir_(Key key, Node *prev, Node *next) const {
    if (!prev) return true;
    if (!next) return false;
    return
      key - Fn{prev->Node::data()}.key() <=
	Fn{next->Node::data()}.key() - key;
  }
  template <int Level>
  typename ZmPQueue_::First<Level, Levels>::T findFwd_(
      Key key, Node **next) const {
    next[Levels - 1] = m_head[Levels - 1];
    findFwd__<Level>(key, next);
  }
  template <int Level>
  typename ZmPQueue_::Next<Level, Levels>::T findFwd_(
      Key key, Node **next) const {
    Node *node;
    if (!(node = next[Levels - Level]) ||
	!(node = node->NodeExt::prev(Levels - Level)))
      next[Levels - Level - 1] = m_head[Levels - Level - 1];
    else
      next[Levels - Level - 1] = node;
    findFwd__<Level>(key, next);
  }
  template <int Level>
  typename ZmPQueue_::Last<Level, Levels>::T findFwd_(
      Key , Node **) const { }

  template <int Level>
  typename ZmPQueue_::First<Level, Levels>::T findRev_(
      Key key, Node **next) const {
    next[Levels - 1] = m_tail[Levels - 1];
    findRev__<Level>(key, next);
  }
  template <int Level>
  typename ZmPQueue_::Next<Level, Levels>::T findRev_(
      Key key, Node **next) const {
    Node *node;
    if (!(node = next[Levels - Level]))
      next[Levels - Level - 1] = m_tail[Levels - Level - 1];
    else
      next[Levels - Level - 1] = node;
    findRev__<Level>(key, next);
  }
  template <int Level>
  typename ZmPQueue_::Last<Level, Levels>::T findRev_(
      Key , Node **) const { }

  template <int Level>
  typename ZmPQueue_::NotLast<Level, Levels>::T findFwd__(
      Key key, Node **next) const {
    Node *node = next[Levels - Level - 1];
    if (node) {
      do {
	Fn item{node->Node::data()};
	if (item.key() == key) goto found;
	if (item.key() > key) goto passed;
      } while (node = node->NodeExt::next(Levels - Level - 1));
      next[Levels - Level - 1] = node;
    }
    findRev_<Level + 1>(key, next);
    return;
  found:
    found_<Level>(node, next);
    return;
  passed:
    Node *prev = node->NodeExt::prev(Levels - Level - 1);
    next[Levels - Level - 1] = node;
    if (findDir_(key, prev, node))
      findFwd_<Level + 1>(key, next);
    else
      findRev_<Level + 1>(key, next);
  }
  template <int Level>
  typename ZmPQueue_::Last<Level, Levels>::T findFwd__(
      Key key, Node **next) const {
    Node *node = next[0];
    if (node) {
      do {
	Fn item{node->Node::data()};
	if (item.key() >= key) break;
      } while (node = node->NodeExt::next(0));
      next[0] = node;
    }
  }
  template <int Level>
  typename ZmPQueue_::NotLast<Level, Levels>::T findRev__(
      Key key, Node **next) const {
    Node *node = next[Levels - Level - 1];
    if (node) {
      do {
	Fn item{node->Node::data()};
	if (item.key() == key) goto found;
	if (item.key() < key) goto passed;
      } while (node = node->NodeExt::prev(Levels - Level - 1));
    }
    next[Levels - Level - 1] = m_head[Levels - Level - 1];
    findFwd_<Level + 1>(key, next);
    return;
  found:
    found_<Level>(node, next);
    return;
  passed:
    Node *prev = node;
    next[Levels - Level - 1] = node = node->NodeExt::next(Levels - Level - 1);
    if (findDir_(key, prev, node))
      findFwd_<Level + 1>(key, next);
    else
      findRev_<Level + 1>(key, next);
  }
  template <int Level>
  typename ZmPQueue_::Last<Level, Levels>::T findRev__(
      Key key, Node **next) const {
    Node *node = next[0];
    if (node) {
      do {
	Fn item{node->Node::data()};
	if (item.key() == key) { next[0] = node; return; }
	if (item.key() < key) { next[0] = node->NodeExt::next(0); return; }
      } while (node = node->NodeExt::prev(0));
    }
    next[0] = m_head[0];
  }

  template <int Level>
  typename ZmPQueue_::NotLast<Level, Levels>::T found_(
      Node *node, Node **next) const {
    next[Levels - Level - 1] = node;
    found_<Level + 1>(node, next);
  }
  template <int Level>
  typename ZmPQueue_::Last<Level, Levels>::T found_(
      Node *, Node **) const { }

  void find_(Key key, Node **next) const {
    if (findDir_(key))
      findFwd_<0>(key, next);
    else
      findRev_<0>(key, next);
  }

public:
  unsigned count_() const { return m_count; }
  unsigned length_() const { return m_length; }

  bool empty_() const { return (!m_count); }

  void reset(Key head) {
    Guard guard(m_lock);
    m_headKey = m_tailKey = head;
    clean_();
  }

  void skip() {
    Guard guard(m_lock);
    m_headKey = m_tailKey;
    clean_();
  }

  Key head() const {
    ReadGuard guard(m_lock);
    return m_headKey;
  }

  Key tail() const {
    ReadGuard guard(m_lock);
    return m_tailKey;
  }

  // returns the first gap that needs to be filled, or {0, 0} if none
  Gap gap() const {
    ReadGuard guard(m_lock);
    Node *node = m_head[0];
    Key tail = m_headKey;
    while (node) {
      Fn item{node->Node::data()};
      Key key = item.key();
      if (key > tail) return Gap(tail, key - tail);
      Key end = key + item.length();
      if (end > tail) tail = end;
      node = node->NodeExt::next(0);
    }
    if (m_tailKey > tail) return Gap(tail, m_tailKey - tail);
    return Gap();
  };

private:
  void purge(Key key) {
    while (Node *node = m_head[0]) {
      Fn item{node->Node::data()};
      Key key_ = item.key();
      if (key_ >= key) return;
      Key end_ = key_ + item.length();
      if (end_ > key) {
	if constexpr (Overlap)
	  if (unsigned length = item.clipHead(key - key_))
	    m_length -= (end_ - key_) - length;
	return;
      }
      delHead_<0>();
      nodeDeref(node);
      nodeDelete(node);
      m_length -= end_ - key_;
      --m_count;
    }
  }

public:
  // override head key (sequence number); used to manually
  // advance past an unrecoverable gap or to rewind in order to
  // force re-processing of earlier items
  void head(Key key) {
    Guard guard(m_lock);
    if (key == m_headKey) return;
    if (key < m_headKey) { // a rewind implies a reset
      clean_();
      m_headKey = m_tailKey = key;
    } else {
      purge(key);
      // key might lie within an unclippable block, evaluate the new head
      // and check that the key is not past it
      if (m_head[0]) {
	Key headKey = Fn{m_head[0]->Node::data()}.key();
	if (headKey < key) key = headKey;
      }
      m_headKey = key;
      if (key > m_tailKey) m_tailKey = key;
    }
  }

  // bypass queue, just update stats
  template <typename _ = ZuBool<Stats>>
  ZuIfT<_{}> bypass(unsigned length) {
    Guard guard(m_lock);
    this->inCount(length);
    this->outCount(length);
  }

  // adds node at key, overwriting data already in the queue
  // - idempotent - does nothing if end <= head
  // - use unshift() to prepend
  void add(NodeRef node) { add_<false>(ZuMv(node)); }

  // immediately returns node if key == head (head is incremented)
  // returns 0 if key < head or key is already present in queue
  // returns 0 and enqueues node if key > head
  NodeRef rotate(NodeRef node) { return add_<true>(ZuMv(node)); }

  // unshift node onto head
  // - idempotent - does nothing if key >= head
  // - use add() to add to body or tail of queue
  void unshift(NodeRef node) {
    Guard guard(m_lock);

    Fn item{node->Node::data()};
    Key key = item.key();
    unsigned length = item.length();
    Key end = key + length;

    if (ZuUnlikely(key >= m_headKey)) return;

    if constexpr (Overlap)
      if (ZuUnlikely(end > m_headKey)) { // clip tail
	length = item.clipTail(end - m_headKey);
	end = key + length;
      }

    if (ZuUnlikely(!length)) return;

    unsigned addSeqNo = m_addSeqNo++;

    Node *next[Levels];

    findFwd_<0>(key, next);

    addAt_<0>(nodeRelease(ZuMv(node)), next, addSeqNo);
    m_headKey = key;
    m_length += end - key;
    ++m_count;
  }

private:
  template <bool Dequeue>
  NodeRef add_(NodeRef node) {
    Guard guard(m_lock);

    Fn item{node->Node::data()};
    Key key = item.key();
    unsigned length = item.length();
    Key end = key + length;

    if (ZuUnlikely(end <= m_headKey)) return nullptr; // already processed

    if constexpr (Overlap)
      if (ZuUnlikely(key < m_headKey)) { // clip head
	length = item.clipHead(m_headKey - key);
	key = end - length;
      }

    if (ZuUnlikely(!length)) { // zero-length heartbeats etc.
      if (end > m_tailKey) m_tailKey = end;
      return nullptr;
    }

    unsigned addSeqNo = m_addSeqNo++;

    if (ZuLikely(key == m_tailKey)) // common case - append at tail
      return addTail_<Dequeue>(ZuMv(node), end, length, addSeqNo);

    if (ZuLikely(key == m_headKey)) { // common case - in-order at head
      purge(end); // remove overlapping data from queue
      return addHead_<Dequeue>(ZuMv(node), end, length, addSeqNo);
    }

    if constexpr (Stats) this->inCount(length);

    Node *next[Levels];

    // find the item immediately following the key

    find_(key, next);

    if constexpr (Overlap) {
      Node *node_ = next[0];

      // process any item immediately following the key

      if (node_) {
	Fn item_(node_->Node::data());
	Key key_ = item_.key();
	Key end_ = key_ + item_.length();

	ZmAssert(key_ >= key);

	// if the following item spans the new item, overwrite it and return
	if (key_ == key && end_ >= end) {
	  item_.write(item);
	  return nullptr;
	}

	if (key_ == key)
	  node_ = nullptr;	// don't process the same item twice
	else
	  node_ = node_->NodeExt::prev(0);
      } else
	node_ = m_tail[0];

      // process any item immediately preceding the key

      if (node_) {
	Fn item_(node_->Node::data());
	Key key_ = item_.key();
	Key end_ = key_ + item_.length();

	ZmAssert(key_ < key);

	// if the preceding item spans the new item, overwrite it and return
	if (end_ >= end) {
	  item_.write(item);
	  return nullptr;
	}

	// if the preceding item partially overlaps the new item, clip it
	if (end_ > key)
	  m_length -= (end_ - key_) - item_.clipTail(end_ - key);
      }

      // remove all items that are completely overlapped by the new item

      while (Node *node_ = next[0]) {
	Fn item_(node_->Node::data());
	Key key_ = item_.key();
	Key end_ = key_ + item_.length();

	ZmAssert(key_ >= key);

	// existing item follows new item, finish search
	if (key_ >= end) break;

	// if the following item partially overlaps, clip it and finish
	if (end_ > end) {
	  if (unsigned length = item_.clipHead(end - key_)) {
	    m_length -= (end_ - key_) - length;
	    break;
	  }
	}

	// existing item is completely overlapped by new item, delete it
	del_<0>(next);
	nodeDeref(node_);
	nodeDelete(node_);
	m_length -= end_ - key_;
	--m_count;
      }
    }

    // add new item into list before following item

    addAt_<0>(nodeRelease(ZuMv(node)), next, addSeqNo);
    if (end > m_tailKey) m_tailKey = end;
    m_length += end - key;
    ++m_count;

    return nullptr;
  }
  template <bool Dequeue>
  ZuIfT<Dequeue, NodeRef> addTail_(NodeRef node,
      Key end, unsigned length, unsigned addSeqNo) {
    m_tailKey = end;
    if constexpr (Stats) this->inCount(length);
    if (end == m_headKey + length) {
      m_headKey = end;
      if constexpr (Stats) this->outCount(length);
      return node;
    } else {
      addTail__<0>(nodeRelease(ZuMv(node)), addSeqNo);
      m_length += length;
      ++m_count;
      return nullptr;
    }
  }
  template <bool Dequeue>
  ZuIfT<!Dequeue, NodeRef> addTail_(NodeRef node,
      Key end, unsigned length, unsigned addSeqNo) {
    addTail__<0>(nodeRelease(ZuMv(node)), addSeqNo);
    m_tailKey = end;
    m_length += length;
    ++m_count;
    if constexpr (Stats) this->inCount(length);
    return nullptr;
  }
  template <bool Dequeue>
  ZuIfT<Dequeue, NodeRef> addHead_(NodeRef node,
      Key end, unsigned length, unsigned) {
    m_headKey = end;
    if (end > m_tailKey) m_tailKey = end;
    if constexpr (Stats) this->inCount(length);
    if constexpr (Stats) this->outCount(length);
    return node;
  }
  template <bool Dequeue>
  ZuIfT<!Dequeue, NodeRef> addHead_(NodeRef node,
      Key end, unsigned length, unsigned addSeqNo) {
    addHead__<0>(nodeRelease(ZuMv(node)), addSeqNo);
    if (end > m_tailKey) m_tailKey = end;
    m_length += length;
    ++m_count;
    if constexpr (Stats) this->inCount(length);
    return nullptr;
  }

  NodeRef dequeue_() {
  loop:
    NodeRef node = m_head[0];
    if (!node) return nullptr;
    Fn item{node->Node::data()};
    Key key = item.key();
    ZmAssert(key >= m_headKey);
    if (key != m_headKey) return nullptr;
    unsigned length = item.length();
    delHead_<0>();
    nodeDeref(node);
    m_length -= length;
    --m_count;
    if (!length) goto loop;
    Key end = key + length;
    m_headKey = end;
    if constexpr (Stats) this->outCount(length);
    return node;
  }
public:
  NodeRef dequeue() {
    Guard guard(m_lock);
    return dequeue_();
  }
  // dequeues up to, but not including, item containing key
  NodeRef dequeue(Key key) {
    Guard guard(m_lock);
    if (m_headKey >= key) return nullptr;
    return dequeue_();
  }

  // shift, unlike dequeue, ignores gaps
private:
  NodeRef shift_() {
  loop:
    NodeRef node = m_head[0];
    if (!node) return nullptr;
    Fn item{node->Node::data()};
    unsigned length = item.length();
    delHead_<0>();
    nodeDeref(node);
    m_length -= length;
    --m_count;
    if (!length) goto loop;
    Key end = item.key() + length;
    m_headKey = end;
    if constexpr (Stats) this->outCount(length);
    return node;
  }
public:
  NodeRef shift() {
    Guard guard(m_lock);
    return shift_();
  }
  // shifts up to but not including item containing key
  // - advances head
  NodeRef shift(Key key) {
    Guard guard(m_lock);
    if (m_headKey >= key) return nullptr;
    if (NodeRef node = shift_()) return node;
    return nullptr;
  }

  // aborts an item (leaving a gap in the queue)
  NodeRef abort(Key key) {
    Guard guard(m_lock);

    Node *next[Levels];

    find_(key, next);

    NodeRef node = next[0];

    if (!node) return nullptr;

    Fn item{node->Node::data()};

    if (item.key() != key) return nullptr;

    del_<0>(next);
    nodeDeref(node);
    m_length -= item.length();
    --m_count;

    return node;
  }

  // find item containing key
  NodeRef find(Key key) const {
    ReadGuard guard(m_lock);

    Node *next[Levels];

    find_(key, next);

    NodeRef node;

    {
      Node *node_ = next[0];

      // process any item immediately following the key (>= key)

      if (node_) {
	Fn item_(node_->Node::data());
	Key key_ = item_.key();

	ZmAssert(key_ >= key);

	if (ZuLikely(key_ == key)) return node = node_;

	node_ = node_->NodeExt::prev(0);
      } else
	node_ = m_tail[0];

      // process any item immediately preceding the key (< key)

      if (node_) {
	Fn item_(node_->Node::data());
	Key key_ = item_.key();
	Key end_ = key_ + item_.length();

	ZmAssert(key_ < key);

	if (ZuLikely(end_ > key)) return node = node_;
      }
    }

    return node;
  }

  void clean_() {
    while (Node *node = m_head[0]) {
      delHead_<0>();
      nodeDeref(node);
      nodeDelete(node);
    }
    m_length = 0;
    m_count = 0;
  }

public:
  template <typename S> void print(S &s) const {
    ReadGuard guard(m_lock);
    s << "head=" << m_headKey
      << " tail=" << m_tailKey
      << " length=" << m_length
      << " count=" << m_count;
  }
  friend ZuPrintFn ZuPrintType(ZmPQueue *);

private:
  Lock		m_lock;
  Key		  m_headKey;
  Node		  *m_head[Levels];
  Key		  m_tailKey;
  Node		  *m_tail[Levels];
  unsigned	  m_length = 0;
  unsigned	  m_count = 0;
  unsigned	  m_addSeqNo = 0;
};

// template resend-requesting receiver using ZmPQueue

// CRTP - application must conform to the following interface:
#if 0
using Queue = ZmPQueue<...>;

struct Impl : public ZmPQRx<Impl, Queue> {
  using Msg = typename Queue::Node;
  using Gap = typename Queue::Gap;

  // access queue
  Queue *rxQueue() const;
 
  // process message
  void process(Msg *msg);

  // send resend request, as protocol requires it;
  // if the protocol is TCP based and now is a subset or equal to
  // prev, then a request may not need to be (re-)sent since
  // the previous (larger) request will still be outstanding
  // and may be relied upon to be fully satisfied; UDP based protocols
  // will need to send a resend request whenever this is called
  void request(const Gap &prev, const Gap &now);

  // re-send resend request, as protocol requires it
  void reRequest(const Gap &now);

  // schedule dequeue() to be called (possibly from different thread)
  void scheduleDequeue();
  // reschedule dequeue() recursively, from within dequeue() itself
  void rescheduleDequeue();
  // dequeue is idle (nothing left in queue to process)
  void idleDequeue();

  // schedule reRequest() to be called (possibly from different thread)
  // at a configured interval later
  void scheduleReRequest();
  // reschedule reRequest() recursively, from within reRequest() itself
  void rescheduleReRequest();
  // cancel any scheduled reRequest()
  void cancelReRequest();
};
#endif

template <class Impl, class Queue, class Lock_ = ZmNoLock>
class ZmPQRx : public ZuPrintable {
  enum {
    Queuing	= 0x01,
    Dequeuing	= 0x02
  };

public:
  template <typename S> static void printFlags(S &s, unsigned v) {
    static const char *flagNames[] = { "Queuing", "Dequeuing" };
    bool first = true;
    for (unsigned i = 0; i < sizeof(flagNames) / sizeof(flagNames[0]); i++)
      if (v & (1U<<i)) {
	if (!first) s << ',';
	first = false;
	s << flagNames[i];
      }
  }
  struct PrintFlags : public ZuPrintable {
    PrintFlags(unsigned v_) : v(v_) { }
    PrintFlags(const PrintFlags &) = default;
    PrintFlags &operator =(const PrintFlags &) = default;
    PrintFlags(PrintFlags &&) = default;
    PrintFlags &operator =(PrintFlags &&) = default;
    template <typename S> void print(S &s) const { printFlags(s, v); }
    unsigned v;
  };

  using Msg = typename Queue::Node;
  using Key = typename Queue::Key;
  using Gap = typename Queue::Gap;

  using Lock = Lock_;
  using Guard = ZmGuard<Lock>;
  using ReadGuard = ZmReadGuard<Lock>;

  // reset sequence number
  void rxReset(Key key) {
    Impl *impl = static_cast<Impl *>(this);
    Guard guard(m_lock);
    impl->cancelReRequest();
    m_flags &= ~(Queuing | Dequeuing);
    impl->rxQueue()->reset(key);
    m_gap = {};
  }

  // start queueing (during snapshot recovery)
  void startQueuing() {
    Guard guard(m_lock);
    m_flags |= Queuing;
  }

  // stop queueing and begin processing messages from key onwards
  void stopQueuing(Key key) {
    Impl *impl = static_cast<Impl *>(this);
    bool scheduleDequeue;
    {
      Guard guard(m_lock);
      m_flags &= ~Queuing;
      impl->rxQueue()->head(key);
      scheduleDequeue = !(m_flags & Dequeuing) && impl->rxQueue()->count_();
      if (scheduleDequeue) m_flags |= Dequeuing;
    }
    if (scheduleDequeue) impl->scheduleDequeue();
  }

  // handle a received message (possibly out of order)
  void rcvd(ZmRef<Msg> msg) {
    Impl *impl = static_cast<Impl *>(this);
    Guard guard(m_lock);
    if (ZuUnlikely(m_flags & (Queuing | Dequeuing))) {
      impl->rxQueue()->add(ZuMv(msg));
      return;
    }
    msg = impl->rxQueue()->rotate(ZuMv(msg));
    bool scheduleDequeue = msg && impl->rxQueue()->count_();
    if (scheduleDequeue) m_flags |= Dequeuing;
    guard.unlock();
    if (ZuUnlikely(!msg)) { stalled(); return; }
    impl->process(msg);
    if (scheduleDequeue) impl->scheduleDequeue();
  }

  // dequeue a message - called via scheduleDequeue(), may reschedule itself
  void dequeue() {
    Impl *impl = static_cast<Impl *>(this);
    Guard guard(m_lock);
    ZmRef<Msg> msg = impl->rxQueue()->dequeue();
    bool scheduleDequeue = msg && impl->rxQueue()->count_();
    if (!scheduleDequeue) m_flags &= ~Dequeuing;
    guard.unlock();
    if (ZuUnlikely(!msg)) { stalled(); return; }
    impl->process(msg);
    if (scheduleDequeue)
      impl->rescheduleDequeue();
    else
      impl->idleDequeue();
  }

  void reRequest() {
    Impl *impl = static_cast<Impl *>(this);
    Gap gap;
    {
      Guard guard(m_lock);
      gap = m_gap;
    }
    impl->cancelReRequest();
    if (!gap.length()) return;
    impl->reRequest(gap);
    impl->rescheduleReRequest();
  }

  unsigned flags() const {
    ReadGuard guard(m_lock);
    return m_flags;
  }

  template <typename S> void print(S &s) const {
    ReadGuard guard(m_lock);
    s << "gap: (" << m_gap.key() << " +" << m_gap.length()
      << ")  flags: " << PrintFlags{m_flags};
  }

private:
  // receiver stalled, may schedule resend request if due to gap
  void stalled() {
    Impl *impl = static_cast<Impl *>(this);
    Gap old, gap;
    {
      Guard guard(m_lock);
      if (!impl->rxQueue()->count_()) return;
      gap = impl->rxQueue()->gap();
      if (gap == m_gap) return;
      old = m_gap;
      m_gap = gap;
    }
    impl->cancelReRequest();
    if (!gap.length()) return;
    impl->request(old, gap);
    impl->scheduleReRequest();
  }

  Lock		m_lock;
  Gap		  m_gap;
  uint8_t	  m_flags = 0;
};

// template resend-requesting sender using ZmPQueue

// CRTP - application must conform to the following interface:
#if 0
using Queue = ZmPQueue<...>;

struct Impl : public ZmPQTx<Impl, Queue> {
  using Msg = typename Queue::Node;
  using Gap = typename Queue::Gap;

  // access queue
  Queue *txQueue() const;

  // Note: *send*_()
  // - more indicates if messages are queued and will be sent immediately
  //   after this one - used to support message blocking by low-level sender
  // - the message can be aborted if it should not be delayed (in
  //   which case the function should return true as if successful); if the
  //   failure was not transient the application should call stop() and
  //   optionally shift() all messages to re-process them

  // send message (low level)
  bool send_(Msg *msg, bool more); // true on success
  bool resend_(Msg *msg, bool more); // true on success

  // send gap (can do nothing if not required)
  bool sendGap_(const MxQueue::Gap &gap, bool more); // true on success
  bool resendGap_(const MxQueue::Gap &gap, bool more); // true on success

  // archive message (low level) (once ackd by receiver(s))
  void archive_(Msg *msg);

  // retrieve message from archive containing key (key may lie within a message)
  // - can optionally call txQueue()->add() for subsequent messages <head
  //   (once key is prepended, it becomes the new head)
  ZmRef<Msg> retrieve_(Key key, Key head);

  // schedule send() to be called (possibly from different thread)
  void scheduleSend();
  // reschedule send() to be called (recursively, from within send() itself)
  void rescheduleSend();
  // sender is idle (nothing left in queue to send)
  void idleSend();

  // schedule resend() to be called (possibly from different thread)
  void scheduleResend();
  // schedule resend() to be called (recursively, from within resend() itself)
  void rescheduleResend();
  // resender is idle (nothing left in queue to resend)
  void idleResend();

  // schedule archive() to be called (possibly from different thread)
  void scheduleArchive();
  // schedule archive() to be called (recursively, from within archive() itself)
  void rescheduleArchive();
  // archiver is idle (nothing left to archive)
  void idleArchive();
};
#endif

template <class Impl, class Queue, class Lock_ = ZmNoLock>
class ZmPQTx : public ZuPrintable {
private:
  enum {
    Running		= 0x01,
    Sending		= 0x02,
    SendFailed		= 0x04,
    Archiving		= 0x08,
    Resending		= 0x10,
    ResendFailed	= 0x20
  };

public:
  template <typename S> static void printFlags(S &s, unsigned v) {
    static const char *flagNames[] = {
      "Running", "Sending", "SendFailed", "Archiving",
      "Resending", "ResendFailed"
    };
    bool comma = false;
    for (unsigned i = 0; i < sizeof(flagNames) / sizeof(flagNames[0]); i++)
      if (v & (1U<<i)) {
	if (comma) s << ',';
	comma = true;
	s << flagNames[i];
      }
  }
  struct PrintFlags : public ZuPrintable {
    PrintFlags(unsigned v_) : v(v_) { }
    PrintFlags(const PrintFlags &) = default;
    PrintFlags &operator =(const PrintFlags &) = default;
    PrintFlags(PrintFlags &&) = default;
    PrintFlags &operator =(PrintFlags &&) = default;
    template <typename S> void print(S &s) const { printFlags(s, v); }
    unsigned v;
  };

public:
  using Msg = typename Queue::Node;
  using Fn = typename Queue::Fn;
  using Key = typename Queue::Key;
  using Gap = typename Queue::Gap;

  using Lock = Lock_;
  using Guard = ZmGuard<Lock>;
  using ReadGuard = ZmReadGuard<Lock>;

  // start concurrent sending and re-sending (datagrams)
  void start() {
    Impl *impl = static_cast<Impl *>(this);
    bool scheduleSend = false;
    bool scheduleArchive = false;
    bool scheduleResend = false;
    {
      Guard guard(m_lock);
#if 0
      std::cerr << (ZuCArray<200>()
	  << "start() PRE  " << *this << "\n  " << *(impl->txQueue()) << '\n')
	<< std::flush;
#endif
      bool alreadyRunning = m_flags & Running;
      if (!alreadyRunning) m_flags |= Running;
      if (alreadyRunning && (m_flags & SendFailed))
	scheduleSend = true;
      else if (scheduleSend = !(m_flags & Sending) &&
	  m_sendKey < impl->txQueue()->tail())
	m_flags |= Sending;
      if (scheduleArchive = !(m_flags & Archiving) &&
	  m_ackdKey > m_archiveKey)
	m_flags |= Archiving;
      if (alreadyRunning && (m_flags & ResendFailed))
	scheduleResend = true;
      else if (scheduleResend = !(m_flags & Resending) && m_gap.length())
	m_flags |= Resending;
      m_flags &= ~(SendFailed | ResendFailed);
#if 0
      std::cerr << (ZuCArray<200>()
	  << "start() POST " << *this << "\n  " << *(impl->txQueue()) << '\n')
	<< std::flush;
#endif
    }
    if (scheduleSend)
      impl->scheduleSend();
    else
      impl->idleSend();
    if (scheduleArchive) impl->scheduleArchive();
    if (scheduleResend)
      impl->scheduleResend();
    else
      impl->idleResend();
  }

  // start concurrent sending and re-sending, from key onwards (streams)
  void start(Key key) {
    Impl *impl = static_cast<Impl *>(this);
    bool scheduleSend = false;
    bool scheduleArchive = false;
    bool scheduleResend = false;
    {
      Guard guard(m_lock);
#if 0
      std::cerr << (ZuCArray<200>()
	  << "start() PRE  " << *this << "\n  " << *(impl->txQueue()) << '\n')
	<< std::flush;
#endif
      bool alreadyRunning = m_flags & Running;
      if (!alreadyRunning) m_flags |= Running;
      m_sendKey = m_ackdKey = key;
      if (alreadyRunning && (m_flags & SendFailed))
	scheduleSend = true;
      else if (scheduleSend = !(m_flags & Sending) &&
	  key < impl->txQueue()->tail())
	m_flags |= Sending;
      if (scheduleArchive = !(m_flags & Archiving) &&
	  key > m_archiveKey)
	m_flags |= Archiving;
      if (alreadyRunning && (m_flags & ResendFailed))
	scheduleResend = true;
      else if (scheduleResend = !(m_flags & Resending) && m_gap.length())
	m_flags |= Resending;
      m_flags &= ~(SendFailed | ResendFailed);
#if 0
      std::cerr << (ZuCArray<200>()
	  << "start() POST " << *this << "\n  " << *(impl->txQueue()) << '\n')
	<< std::flush;
#endif
    }
    if (scheduleSend)
      impl->scheduleSend();
    else
      impl->idleSend();
    if (scheduleArchive) impl->scheduleArchive();
    if (scheduleResend)
      impl->scheduleResend();
    else
      impl->idleResend();
  }

  // stop sending
  void stop() {
    Guard guard(m_lock);
    if (!(m_flags & Running)) return;
    m_flags &= ~(Running | Sending | Resending);
  }

  // reset sequence number
  void txReset(Key key) {
    Impl *impl = static_cast<Impl *>(this);
    Guard guard(m_lock);
    m_sendKey = m_ackdKey = m_archiveKey = key;
    m_gap = Gap();
    impl->txQueue()->reset(key);
#if 0
      std::cerr << (ZuCArray<200>()
	  << "txReset() " << *this << "\n  " << *(impl->txQueue()) << '\n')
	<< std::flush;
#endif
  }

  // send message (with key already allocated)
  void send(ZmRef<Msg> msg) {
    Impl *impl = static_cast<Impl *>(this);
    bool scheduleSend = false;
    auto key = Fn{msg->data()}.key();
    {
      Guard guard(m_lock);
      if (ZuUnlikely(key < m_ackdKey)) {
#if 0
	std::cerr << (ZuCArray<200>()
	    << "send(" << key << ") outdated "
	    << *this << "\n  " << *(impl->txQueue()) << '\n')
	  << std::flush;
#endif
	return;
      }
      impl->txQueue()->add(ZuMv(msg));
      if (scheduleSend = (m_flags & (Running | Sending)) == Running &&
	  m_sendKey <= key)
	m_flags |= Sending;
#if 0
      std::cerr << (ZuCArray<200>()
	  << "send(" << key <<") "
	  << *this << "\n  " << *(impl->txQueue()) << '\n')
	<< std::flush;
#endif
    }
    if (scheduleSend) impl->scheduleSend();
  }

  // abort message
  ZmRef<Msg> abort(Key key) {
    if (key < m_sendKey) return nullptr;
    Impl *impl = static_cast<Impl *>(this);
    ZmRef<Msg> msg;
    {
      Guard guard(m_lock);
      msg = impl->txQueue()->abort(key);
    }
    return msg;
  }

  // acknowlege (archive) messages up to, but not including, key
  void ackd(Key key) {
    Impl *impl = static_cast<Impl *>(this);
    bool scheduleArchive = false;
    {
      Guard guard(m_lock);
      if (ZuUnlikely(key < m_ackdKey)) {
#if 0
	std::cerr << (ZuCArray<200>()
	    << "ackd(" << key << ") outdated "
	    << *this << "\n  " << *(impl->txQueue()) << '\n')
	  << std::flush;
#endif
	return;
      }
      m_ackdKey = key;
      if (key > m_sendKey) m_sendKey = key;
      if (scheduleArchive = !(m_flags & Archiving) && key > m_archiveKey)
	m_flags |= Archiving;
#if 0
      std::cerr << (ZuCArray<200>()
	  << "ackd(Key) " << *this << "\n  " << *(impl->txQueue()) << '\n')
	<< std::flush;
#endif
    }
    if (scheduleArchive) impl->scheduleArchive();
  }

  // resend messages (in response to a resend request)
private:
  bool resend_(const Gap &gap) {
    bool scheduleResend = false;
    if (!m_gap.length()) {
      m_gap = gap;
      scheduleResend = !(m_flags & Resending);
    } else {
      if (gap.key() < m_gap.key()) {
	m_gap.length() += (m_gap.key() - gap.key());
	m_gap.key() = gap.key();
	scheduleResend = !(m_flags & Resending);
      }
      if ((gap.key() + gap.length()) > (m_gap.key() + m_gap.length())) {
	m_gap.length() = (gap.key() - m_gap.key()) + gap.length();
	if (!scheduleResend) scheduleResend = !(m_flags & Resending);
      }
    }
    if (scheduleResend) m_flags |= Resending;
    return scheduleResend;
  }
public:
  void resend(const Gap &gap) {
    if (!gap.length()) return;
    Impl *impl = static_cast<Impl *>(this);
    bool scheduleResend = false;
    {
      Guard guard(m_lock);
      scheduleResend = resend_(gap);
    }
    if (scheduleResend) impl->scheduleResend();
  }

  // send - called via {,re}scheduleSend(), may call rescheduleSend()
  void send() {
    Impl *impl = static_cast<Impl *>(this);
    bool scheduleSend = false;
    Gap sendGap;
    Key prevKey;
    ZmRef<Msg> msg;
    {
      Guard guard(m_lock);
      auto txQueue = impl->txQueue();
#if 0
      std::cerr << (ZuCArray<200>()
	  << "send() " << *this << "\n  " << *(impl->txQueue()) << '\n')
	<< std::flush;
#endif
      if (!(m_flags & Running)) { m_flags &= ~Sending; return; }
      prevKey = m_sendKey;
      scheduleSend = prevKey < txQueue->tail();
      while (scheduleSend) {
	unsigned length;
	if (msg = txQueue->find(m_sendKey))
	  length = Fn{msg->data()}.length();
	else if (msg = impl->retrieve_(m_sendKey, txQueue->head()))
	  length = Fn{msg->data()}.length();
	else {
	  if (!sendGap.length()) sendGap.key() = m_sendKey;
	  sendGap.length() += (length = 1);
	}
	m_sendKey += length;
	scheduleSend = m_sendKey < txQueue->tail();
	if (msg) break;
      }
      if (!scheduleSend) m_flags &= ~Sending;
    }
    if (ZuUnlikely(sendGap.length())) {
      if (ZuUnlikely(!impl->sendGap_(sendGap, scheduleSend))) goto sendFailed;
      prevKey += sendGap.length();
    }
    if (ZuLikely(msg))
      if (ZuUnlikely(!impl->send_(msg, scheduleSend))) goto sendFailed;
    if (scheduleSend)
      impl->rescheduleSend();
    else
      impl->idleSend();
    return;
  sendFailed:
    {
      Guard guard(m_lock);
      m_flags |= Sending | SendFailed;
      m_sendKey = prevKey;
#if 0
      std::cerr << (ZuCArray<200>()
	  << "send() FAIL " << *this << "\n  " << *(impl->txQueue()) << '\n')
	<< std::flush;
#endif
    }
  }

  // archive - called via scheduleArchive(), may reschedule itself
  void archive() {
    Impl *impl = static_cast<Impl *>(this);
    bool scheduleArchive;
    ZmRef<Msg> msg;
    {
      Guard guard(m_lock);
      if (!(m_flags & Running)) { m_flags &= ~Archiving; return; }
      scheduleArchive = m_archiveKey < m_ackdKey;
      while (scheduleArchive) {
	msg = impl->txQueue()->find(m_archiveKey);
	m_archiveKey += msg ? (unsigned)Fn{msg->data()}.length() : 1U;
	scheduleArchive = m_archiveKey < m_ackdKey;
	if (msg) break;
      }
      if (!scheduleArchive) m_flags &= ~Archiving;
#if 0
      std::cerr << (ZuCArray<200>()
	  << "archive() " << *this << "\n  " << *(impl->txQueue()) << '\n')
	<< std::flush;
#endif
    }
    if (msg)
      impl->archive_(msg);
    if (scheduleArchive)
      impl->rescheduleArchive();
    else
      impl->idleArchive();
  }

  // completed archiving of messages up to, but not including, key
  void archived(Key key) {
    Impl *impl = static_cast<Impl *>(this);
    ZmRef<Msg> msg;
    do {
      Guard guard(m_lock);
      msg = impl->txQueue()->shift(key);
    } while (msg);
  }

  // resend - called via scheduleResend(), may reschedule itself
  void resend() {
    Impl *impl = static_cast<Impl *>(this);
    bool scheduleResend = false;
    Gap sendGap, prevGap;
    ZmRef<Msg> msg;
    {
      Guard guard(m_lock);
      if (!(m_flags & Running)) { m_flags &= ~Resending; return; }
      prevGap = m_gap;
      while (m_gap.length()) {
	unsigned length;
	if (msg = impl->txQueue()->find(m_gap.key())) {
	  Fn item{msg->data()};
	  auto end = item.key() + item.length();
	  length = end - m_gap.key();
	  if (end <= m_archiveKey)
	    while (impl->txQueue()->shift(end));
	} else if (
	    msg = impl->retrieve_(m_gap.key(), impl->txQueue()->head())) {
	  Fn item{msg->data()};
	  auto end = item.key() + item.length();
	  length = end - m_gap.key();
	} else {
	  if (!sendGap.length()) sendGap.key() = m_gap.key();
	  sendGap.length() += (length = 1);
	}
	if (m_gap.length() <= length) {
	  m_gap = {};
	  scheduleResend = false;
	} else {
	  m_gap.key() += length;
	  m_gap.length() -= length;
	  scheduleResend = true;
	}
	if (msg) break;
      }
      if (!scheduleResend) m_flags &= ~Resending;
    }
    if (ZuUnlikely(sendGap.length())) {
      if (ZuUnlikely(!impl->resendGap_(sendGap, scheduleResend)))
	goto resendFailed;
      unsigned length = sendGap.length();
      if (ZuLikely(prevGap.length() > length)) {
	prevGap.key() += length;
	prevGap.length() -= length;
      } else
	prevGap = {};
    }
    if (ZuLikely(msg))
      if (ZuUnlikely(!impl->resend_(msg, scheduleResend))) goto resendFailed;
    if (scheduleResend)
      impl->rescheduleResend();
    else
      impl->idleResend();
    return;
  resendFailed:
    {
      Guard guard(m_lock);
      m_flags |= Resending | ResendFailed;
      m_gap = prevGap;
    }
  }

  unsigned flags() const {
    ReadGuard guard(m_lock);
    return m_flags;
  }

  template <typename S> void print(S &s) const {
    ReadGuard guard(m_lock);
    s << "gap={" << m_gap.key() << ", " << m_gap.length()
      << "} flags=" << PrintFlags{m_flags}
      << " send=" << m_sendKey
      << " ackd=" << m_ackdKey
      << " archive=" << m_archiveKey;
  }

private:
  Lock		m_lock;
  Key		  m_sendKey = 0;
  Key		  m_ackdKey = 0;
  Key		  m_archiveKey = 0;
  Gap		  m_gap;
  uint8_t	  m_flags = 0;
};

#endif /* ZmPQueue_HH */
