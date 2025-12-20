//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// linear hash table (policy-based)
//
// open addressing, linear probing, fast chained lookup, optionally locked
//
// nodes are stored by value - avoids run-time heap usage and improves
// cache coherence (except during initialization/resizing)
//
// use ZmHash for high-contention high-throughput read/write data
// use ZmLHash for unlocked or mostly uncontended reference data

#ifndef ZmLHash_HH
#define ZmLHash_HH

#ifndef ZmLib_HH
#include <zlib/ZmLib.hh>
#endif

#include <zlib/ZuCmp.hh>
#include <zlib/ZuHash.hh>
#include <zlib/ZuTuple.hh>
#include <zlib/ZuArrayFn.hh>

#include <zlib/ZmNoLock.hh>
#include <zlib/ZmAtomic.hh>
#include <zlib/ZmLockTraits.hh>
#include <zlib/ZmGuard.hh>
#include <zlib/ZmRef.hh>
#include <zlib/ZmAssert.hh>

#include <zlib/ZmHashMgr.hh>

// NTP (named template parameters):
//
// ZmLHashKV<ZtString<>, ZtString<>,	// key, value pair of ZtString<>s
//   ZmLHashCmp<ZtCmp> >		// case-insensitive comparison

// NTP defaults
struct ZmLHash_Defaults {
  static constexpr auto KeyAxor = ZuDefaultAxor();
  static constexpr auto ValAxor = ZuDefaultAxor();
  template <typename T> using CmpT = ZuCmp<T>;
  template <typename T> using ValCmpT = ZuCmp<T>;
  template <typename T> using HashFnT = ZuHash<T>;
  using Lock = ZmNoLock;
  static const char *ID() { return "ZmHash"; }
  enum { Static = 0 };
  enum { Local = 0 };
};

// ZmLHashKey - key accessor
template <auto KeyAxor_, typename NTP = ZmLHash_Defaults>
struct ZmLHashKey : public NTP {
  static constexpr auto KeyAxor = KeyAxor_;
};

// ZmLHashKeyVal - key and optional value accessors
template <auto KeyAxor_, auto ValAxor_, typename NTP = ZmLHash_Defaults>
struct ZmLHashKeyVal : public NTP {
  static constexpr auto KeyAxor = KeyAxor_;
  static constexpr auto ValAxor = ValAxor_;
};

// ZmLHashCmp - the comparator
template <template <typename> class Cmp_, typename NTP = ZmLHash_Defaults>
struct ZmLHashCmp : public NTP {
  template <typename T> using CmpT = Cmp_<T>;
};

// ZmLHashValCmp - the optional value comparator
template <template <typename> class ValCmp_, typename NTP = ZmLHash_Defaults>
struct ZmLHashValCmp : public NTP {
  template <typename T> using ValCmpT = ValCmp_<T>;
};

// ZmLHashFn - the hash function
template <template <typename> class HashFn_, typename NTP = ZmLHash_Defaults>
struct ZmLHashFn : public NTP {
  template <typename T> using HashFnT = HashFn_<T>;
};

// ZmLHashLock - the lock type used (ZmRWLock will permit concurrent reads)
template <class Lock_, typename NTP = ZmLHash_Defaults>
struct ZmLHashLock : public NTP {
  using Lock = Lock_;
};

// ZmLHashID - the hash ID
template <auto ID_, typename NTP = ZmLHash_Defaults>
struct ZmLHashID : public NTP {
  static constexpr auto ID = ID_;
};

// ZmLHashStatic<Bits> - static/non-resizable vs dynamic/resizable allocation
template <unsigned Static_, typename NTP = ZmLHash_Defaults>
struct ZmLHashStatic : public NTP {
  enum { Static = Static_ };
};

// ZmLHashLocal<> - local hash table
// - not registered with hash mgr
// - no telemetry
// - can be on stack
// - does not derive from ZmAnyHash
template <typename NTP = ZmLHash_Defaults>
struct ZmLHashLocal : public NTP {
  enum { Local = 1 };
};

template <typename T>
struct ZmLHash_Ops : public ZuArrayFn<T, ZuCmp<T> > {
  static T *alloc(unsigned size) {
    auto ptr = static_cast<T *>(
      Zm::alignedAlloc<Zm::CacheLineSize>(size * sizeof(T)));
    if (!ptr) throw std::bad_alloc{};
    return ptr;
  }
  static void free(T *ptr) {
    Zm::alignedFree(ptr);
  }
};

template <typename T>
struct alignas(T) ZmLHash_Data {
  ZuInline constexpr ZmLHash_Data() noexcept { }
  ZuInline constexpr ~ZmLHash_Data() noexcept { }
  union { T v; };
};
template <typename T_, auto KeyAxor, auto ValAxor>
class ZmLHash_Node {
template <typename, typename> friend class ZmLHash;
template <typename, typename, typename, unsigned> friend class ZmLHash_;

public:
  using T = T_;
  using Data = ZmLHash_Data<T>;

  ZmLHash_Node() noexcept { }
  ~ZmLHash_Node() noexcept { if (m_u) data().~T(); }

  ZmLHash_Node(const ZmLHash_Node &n) noexcept {
    if (m_u = n.m_u) new (&m_data.v) T(n.data());
  }
  ZmLHash_Node &operator =(const ZmLHash_Node &n) noexcept {
    if (ZuLikely(this != &n)) {
      if (m_u) data().~T();
      if (m_u = n.m_u) new (&m_data.v) T(n.data());
    }
    return *this;
  }

  ZmLHash_Node(ZmLHash_Node &&n) noexcept {
    if (m_u = n.m_u) new (&m_data.v) T(ZuMv(n.data()));
  }
  ZmLHash_Node &operator =(ZmLHash_Node &&n) noexcept {
    if (ZuLikely(this != &n)) {
      if (m_u) data().~T();
      if (m_u = n.m_u) new (&m_data.v) T(ZuMv(n.data()));
    }
    return *this;
  }

private:
  template <typename P>
  void init(unsigned head, unsigned tail, unsigned next, P &&v) {
    if (!m_u)
      new (&m_data.v) T{ZuFwd<P>(v)};
    else
      data() = ZuFwd<P>(v);
    m_u = (next<<3) | (head<<2) | (tail<<1) | 1U;
  }

  void null() {
    if (m_u) {
      data().~T();
      m_u = 0;
    }
  }

public:
  bool operator !() const { return !m_u; }
  ZuOpBool

  bool equals(const ZmLHash_Node &n) const {
    if (!n.m_u) return !m_u;
    if (!m_u) return false;
    return data().equals(n.data());
  }
  int cmp(const ZmLHash_Node &n) const {
    if (!n.m_u) return !m_u ? 0 : 1;
    if (!m_u) return -1;
    return data().cmp(n.data());
  }
  friend inline bool operator ==(const ZmLHash_Node &l, const ZmLHash_Node &r) {
    return l.equals(r);
  }
  friend inline int operator <=>(const ZmLHash_Node &l, const ZmLHash_Node &r) {
    return l.cmp(r);
  }

private:
  bool head() const { return m_u & 4U; }
  void setHead() { m_u |= 4U; }
  void clrHead() { m_u &= ~4U; }
  bool tail() const { return m_u & 2U; }
  void setTail() { m_u |= 2U; }
  void clrTail() { m_u &= ~2U; }
  unsigned next() const { return m_u>>3U; }
  void next(unsigned n) { m_u = (n<<3U) | (m_u & 7U); }

  ZuInline decltype(auto) key(this auto &&self) {
    return KeyAxor(ZuFwdLike<decltype(self)>(self.data()));
  }
  ZuInline decltype(auto) val(this auto &&self) {
    return ValAxor(ZuFwdLike<decltype(self)>(self.data()));
  }
  ZuInline constexpr auto &&data(this auto &&self) {
    ZmAssert(self.m_u);
    return ZuFwdLike<decltype(self)>(self.m_data.v);
  }

public:
  struct Traits : public ZuBaseTraits<ZmLHash_Node> {
    enum { IsPOD = ZuTraits<T>::IsPOD };
  };
  friend Traits ZuTraitsType(ZmLHash_Node *);

private:
  Data		m_data;
  uint32_t	m_u = 0;
};

template <
  typename Hash, typename NTP,
  bool Local = NTP::Local, bool Static = bool(NTP::Static)>
class ZmLHash__;

// local, static
template <typename Hash, typename NTP>
class ZmLHash__<Hash, NTP, true, true> {
  using Lock = typename NTP::Lock;

public:
  static unsigned loadFactor_() { return 16; }
  static double loadFactor() { return 1.0; }

  unsigned count_() const { return m_count.load_(); }

protected:
  ZmAtomic<unsigned>	m_count = 0;
  Lock			m_lock;
};

// global, static
template <typename Hash, typename NTP>
class ZmLHash__<Hash, NTP, false, true> :
  public ZmLHash__<Hash, NTP, true, true> { };

// local, dynamic
template <typename Hash, typename NTP>
class ZmLHash__<Hash, NTP, true, false> {
  using Lock = typename NTP::Lock;

public:
  unsigned loadFactor_() const { return m_loadFactor; }
  double loadFactor() const { return double(m_loadFactor) / 16.0; }

  unsigned count_() const { return m_count.load_(); }

protected:
  ZmLHash__(const ZmHashParams &params) {
    double loadFactor = params.loadFactor();
    if (loadFactor < 0.5) loadFactor = 0.5;
    else if (loadFactor > 1.0) loadFactor = 1.0;
    m_loadFactor = unsigned(loadFactor * 16.0);
  }

  unsigned		m_loadFactor = 0;
  ZmAtomic<unsigned>	m_count = 0;
  Lock			m_lock;
};

// global, dynamic
template <typename Hash, typename NTP>
class ZmLHash__<Hash, NTP, false, false> :
  public ZmAnyHash, public ZmLHash__<Hash, NTP, true, false> {
protected:
  ZmLHash__(ZuCSpan id, const ZmHashParams &params) :
    ZmLHash__<Hash, NTP, true, false>{params}, m_id{id} { }

  void init() { ZmHashMgr::add(this); }
  void final() { ZmHashMgr::del(this); }

  void telemetry(ZmHashTelemetry &data) const {
    data.id = m_id;
    static_cast<const Hash *>(this)->telemetry_(data);
  }

private:
  ZmIDString		m_id;
};

// statically allocated hash table base class
template <typename Hash, typename T, typename NTP, unsigned Static>
class ZmLHash_ : public ZmLHash__<Hash, NTP> {
  using Base = ZmLHash__<Hash, NTP>;
  static constexpr auto KeyAxor = NTP::KeyAxor;
  static constexpr auto ValAxor = NTP::ValAxor;
  using Key = ZuRDecay<decltype(KeyAxor(ZuDeclVal<const T &>()))>;
  using Val = ZuRDecay<decltype(ValAxor(ZuDeclVal<const T &>()))>;
  using Cmp = typename NTP::template CmpT<Key>;
  using ValCmp = typename NTP::template ValCmpT<Val>;
  using HashFn = typename NTP::template HashFnT<Key>;
  using Node = ZmLHash_Node<T, KeyAxor, ValAxor>;
  using Ops = ZmLHash_Ops<Node>;

public:
  static constexpr unsigned bits() { return Static; }

protected:
  ZmLHash_() = default;

  void init() { Ops::initElems(m_table, 1U<<Static); }
  void final() { Ops::destroyElems(m_table, 1U<<Static); }
  void resize() { }
  static constexpr unsigned resized() { return 0; }

  Node		m_table[1U<<Static];
};

// dynamically allocated hash table base class
template <class Hash, typename T, typename NTP>
class ZmLHash_<Hash, T, NTP, 0> : public ZmLHash__<Hash, NTP> {
  using Base = ZmLHash__<Hash, NTP>;
  static constexpr auto KeyAxor = NTP::KeyAxor;
  static constexpr auto ValAxor = NTP::ValAxor;
  using Key = ZuRDecay<decltype(KeyAxor(ZuDeclVal<const T &>()))>;
  using Val = ZuRDecay<decltype(ValAxor(ZuDeclVal<const T &>()))>;
  using Cmp = typename NTP::template CmpT<Key>;
  using ValCmp = typename NTP::template ValCmpT<Val>;
  using HashFn = typename NTP::template HashFnT<Key>;
  using Node = ZmLHash_Node<T, KeyAxor, ValAxor>;
  using Ops = ZmLHash_Ops<Node>;
  enum { Local = NTP::Local };

public:
  unsigned bits() const { return m_bits; }

protected:
  template <typename _ = NTP, ZuIfT<_::Local, int> = 0>
  ZmLHash_(const ZmHashParams &params) :
    Base{params}, m_bits{params.bits()} { }
  template <typename _ = NTP, ZuIfT<!_::Local, int> = 0>
  ZmLHash_(ZuCSpan id, const ZmHashParams &params) :
    Base{id, params}, m_bits{params.bits()} { }

  void init() {
    unsigned size = 1U<<m_bits;
    m_table = Ops::alloc(size);
    Ops::initElems(m_table, size);
    if constexpr (!Local) Base::init();
  }

  void final() {
    if constexpr (!Local) Base::final();
    Ops::destroyElems(m_table, 1U<<m_bits);
    Ops::free(m_table);
  }

  void resize() {
    ++m_resized;
    ++m_bits;
    unsigned size = 1U<<m_bits;
    Node *oldTable = m_table;
    m_table = Ops::alloc(size);
    Ops::initElems(m_table, size);
    for (unsigned i = 0; i < (size>>1U); i++)
      if (!!oldTable[i]) {
	auto code = HashFn::hash(oldTable[i].key());
	static_cast<Hash *>(this)->add__(ZuMv(oldTable[i].data()), code);
      }
    Ops::destroyElems(oldTable, (size>>1U));
    Ops::free(oldTable);
  }

  unsigned resized() const { return m_resized.load_(); }

  ZmAtomic<unsigned>	m_resized = 0;
  unsigned		m_bits;
  Node	 		*m_table = nullptr;
};

template <typename T_, typename NTP = ZmLHash_Defaults>
class ZmLHash : public ZmLHash_<ZmLHash<T_, NTP>, T_, NTP, NTP::Static> {
template <typename, typename, typename, unsigned> friend class ZmLHash_;

friend class ZmLHash__<ZmLHash<T_, NTP>, NTP>;

  using Base = ZmLHash_<ZmLHash<T_, NTP>, T_, NTP, NTP::Static>;

public:
  using T = T_;
  static constexpr auto KeyAxor = NTP::KeyAxor;
  static constexpr auto ValAxor = NTP::ValAxor;
  using KeyRet = decltype(KeyAxor(ZuDeclVal<const T &>()));
  using ValRet = decltype(ValAxor(ZuDeclVal<const T &>()));
  using Key = ZuRDecay<KeyRet>;
  using Val = ZuRDecay<ValRet>;
  using Cmp = typename NTP::template CmpT<Key>;
  using ValCmp = typename NTP::template ValCmpT<Val>;
  using HashFn = typename NTP::template HashFnT<Key>;
  using Lock = typename NTP::Lock;
  static constexpr auto ID = NTP::ID;
  enum { Static = NTP::Static };

  using LockTraits = ZmLockTraits<Lock>;
  using Guard = ZmGuard<Lock>;
  using ReadGuard = ZmReadGuard<Lock>;

  using Node = ZmLHash_Node<T, KeyAxor, ValAxor>;
  using Ops = ZmLHash_Ops<Node>;
 
private:
  // CheckHashFn ensures that legacy hash functions returning int
  // trigger a compile-time assertion failure; hash() must return uint32_t
  class CheckHashFn {
    using Small = char;
    struct Big { char _[2]; };
    static Small test(const uint32_t &_); // named parameter due to VS2010 bug
    static Big	 test(const int &_);
    static Big	 test(...);
  public:
    CheckHashFn(); // keep gcc quiet
    enum {
      IsUInt32 = sizeof(test(HashFn::hash(ZuDeclVal<Key>()))) == sizeof(Small)
    };
  };
  ZuAssert(CheckHashFn::IsUInt32);

  using Base::m_count;
  using Base::m_lock;
  using Base::m_table;

public:
  using Base::bits;
  using Base::loadFactor_;
  using Base::loadFactor;
  using Base::resized;

private:
  const T *ptr_(int slot) const {
    if (ZuLikely(slot >= 0)) return &m_table[slot].data();
    return nullptr;
  }
  KeyRet key_(int slot) const {
    if (ZuLikely(slot >= 0)) return m_table[slot].key();
    return ZuNullRef<Key, Cmp>();
  }
  ValRet val_(int slot) const {
    if (ZuLikely(slot >= 0)) return m_table[slot].val();
    return ZuNullRef<Val, ValCmp>();
  }

protected:
  class Iter_;
friend Iter_;
  class Iter_ {			// hash iterator
    using Hash = ZmLHash<T, NTP>;
  friend Hash;

    Iter_(const Iter_ &) = delete;
    Iter_ &operator =(const Iter_ &) = delete;

  protected:
    Hash	&hash;
    int		slot = -1;
    int		next = -1;

  protected:
    Iter_(Iter_ &&) = default;
    Iter_ &operator =(Iter_ &&) = default;

    Iter_(Hash &hash_) : hash{hash_} { }

    virtual void lock(Lock &l) = 0;
    virtual void unlock(Lock &l) = 0;

  public:
    void reset() { hash.iterBegin(*this); }
    const T *operator ()() { return hash.iterate(*this); }

    decltype(auto) key() { return hash.key(*this); }
    decltype(auto) val() { return hash.val(*this); }

    unsigned count() const { return hash.count_(); }

    bool operator !() const { return slot < 0; }
    ZuOpBool

  };

  template <typename> class KeyIter_;
template <typename> friend class KeyIter_;
  template <typename IKey_>
  class KeyIter_ : public Iter_ {
    KeyIter_(const KeyIter_ &) = delete;
    KeyIter_ &operator =(const KeyIter_ &) = delete;

    using Hash = ZmLHash<T, NTP>;
  friend Hash;

    using Iter_::hash;

  public:
    using IKey = IKey_;

  protected:
    IKey	key_;
    int		prev;

  protected:
    KeyIter_(KeyIter_ &&) = default;
    KeyIter_ &operator =(KeyIter_ &&) = default;

    template <typename IKey__>
    KeyIter_(Hash &hash_, IKey__ &&key__) :
	Iter_{hash_}, key_{ZuFwd<IKey__>(key__)}, prev{-1} { }

  public:
    void reset() { hash.iterBegin(*this); }
    const T *operator ()() { return hash.iterate(*this); }

    decltype(auto) key() { return hash.key(*this); }
    decltype(auto) val() { return hash.val(*this); }
  };

public:
  class Iter : public Iter_ {
    Iter(const Iter &) = delete;
    Iter &operator =(const Iter &) = delete;

    using Hash = ZmLHash<T, NTP>;
  friend Hash;

    using Base = Iter_;
    using Base::hash;

    void lock(Lock &l) { LockTraits::lock(l); }
    void unlock(Lock &l) { LockTraits::unlock(l); }

  public:
    Iter(Iter &&) = default;
    Iter &operator =(Iter &&) = default;

    Iter(Hash &hash_) : Base{hash_} { hash.iterBegin(*this); }
    ~Iter() { hash.iterEnd(*this); }
    void del() { hash.iterDel(*this); }
  };

  class CIter : public Iter_ {
    CIter(const CIter &) = delete;
    CIter &operator =(const CIter &) = delete;

    using Hash = ZmLHash<T, NTP>;
  friend Hash;

    using Base = Iter_;
    using Base::hash;
 
    void lock(Lock &l) { LockTraits::readlock(l); }
    void unlock(Lock &l) { LockTraits::readunlock(l); }

  public:
    CIter(CIter &&) = default;
    CIter &operator =(CIter &&) = default;

    CIter(const Hash &hash_) : Base{const_cast<Hash &>(hash_)} {
      const_cast<Hash &>(hash).iterBegin(*this);
    }
    ~CIter() { hash.iterEnd(*this); }
  };

  template <typename IKey_>
  class KeyIter : public KeyIter_<IKey_> {
    KeyIter(const KeyIter &) = delete;
    KeyIter &operator =(const KeyIter &) = delete;

    using Hash = ZmLHash<T, NTP>;
  friend Hash;

    using Base = KeyIter_<IKey_>;
    using typename Base::IKey;
    using Base::key;
    using Base::hash;

    void lock(Lock &l) { LockTraits::lock(l); }
    void unlock(Lock &l) { LockTraits::unlock(l); }

  public:
    KeyIter(KeyIter &&) = default;
    KeyIter &operator =(KeyIter &&) = default;

    template <typename IKey__>
    KeyIter(Hash &hash_, IKey__ &&key_) :
      Base{hash_, ZuFwd<IKey__>(key_)} { hash.iterBegin(*this); }
    ~KeyIter() { hash.iterEnd(*this); }
    void del() { hash.iterDel(*this); }
  };

  template <typename IKey_>
  class ReadKeyIter : public KeyIter_<IKey_> {
    ReadKeyIter(const ReadKeyIter &) = delete;
    ReadKeyIter &operator =(const ReadKeyIter &) = delete;

    using Hash = ZmLHash<T, NTP>;
  friend Hash;

    using Base = KeyIter_<IKey_>;
    using typename Base::IKey;
    using Base::key;
    using Base::hash;

    void lock(Lock &l) { LockTraits::readlock(l); }
    void unlock(Lock &l) { LockTraits::readunlock(l); }

  public:
    ReadKeyIter(ReadKeyIter &&) = default;
    ReadKeyIter &operator =(ReadKeyIter &&) = default;

    template <typename IKey__>
    ReadKeyIter(const Hash &hash_, IKey__ &&key_) :
	Base{const_cast<Hash &>(hash_), ZuFwd<IKey__>(key_)}
      { const_cast<Hash &>(hash).iterBegin(*this); }
    ~ReadKeyIter() { hash.iterEnd(*this); }
  };

  // static - no ID, no params
  template <typename _ = NTP, ZuIfT<bool(_::Static), int> = 0>
  ZmLHash() : Base{} { }

  // local - no ID, optional params
  template <
    typename _ = NTP,
    ZuIfT<_::Local && !_::Static, int> = 0>
  ZmLHash() : Base{ZmHashParams{ID()}} { Base::init(); }
  template <
    typename _ = NTP,
    ZuIfT<_::Local && !_::Static, int> = 0>
  ZmLHash(const ZmHashParams &params) : Base{params} { Base::init(); }

  // global - any combination of ID, params
  template <
    typename _ = NTP,
    ZuIfT<!_::Local && !_::Static, int> = 0>
  ZmLHash() : Base{ID(), ZmHashParams{ID()}} { Base::init(); }
  // ID
  template <
    typename _ = NTP,
    ZuIfT<!_::Local && !_::Static, int> = 0>
  ZmLHash(ZuCSpan id) : Base{id, ZmHashParams{id}} { Base::init(); }
  // params
  template <
    typename _ = NTP,
    ZuIfT<!_::Local && !_::Static, int> = 0>
  ZmLHash(const ZmHashParams &params) : Base{ID(), params} { Base::init(); }
  // ID, params
  template <
    typename _ = NTP,
    ZuIfT<!_::Local && !_::Static, int> = 0>
  ZmLHash(ZuCSpan id, const ZmHashParams &params) : Base{id, params} {
    Base::init();
  }

  ZmLHash(const ZmLHash &) = delete;
  ZmLHash &operator =(const ZmLHash &) = delete;
  ZmLHash(ZmLHash &&) = delete;
  ZmLHash &operator =(ZmLHash &&) = delete;

  ~ZmLHash() { Base::final(); }

  unsigned size() const {
    return double(uint64_t(1)<<bits()) * loadFactor();
  }

  template <typename P>
  const T *add(P &&data) {
    uint32_t code = HashFn::hash(KeyAxor(data));
    Guard guard(m_lock);
    return ptr_(add_(ZuFwd<P>(data), code));
  }

  template <typename P0, typename P1>
  const T *add(P0 &&p0, P1 &&p1) {
    uint32_t code = HashFn::hash(p0);
    Guard guard(m_lock);
    return ptr_(add_(ZuFwdTuple(ZuFwd<P0>(p0), ZuFwd<P1>(p1)), code));
  }

private:
  int alloc(unsigned slot) {
    unsigned size = 1U<<bits();
    for (unsigned i = 1; i < size; i++) {
      unsigned probe = (slot + i) & (size - 1);
      if (!m_table[probe]) return probe;
    }
    return -1;
  }
  int prev(unsigned slot) {
    unsigned size = 1U<<bits();
    for (unsigned i = 1; i < size; i++) {
      unsigned prev = (slot + size - i) & (size - 1);
      if (m_table[prev] &&
	  !m_table[prev].tail() &&
	  m_table[prev].next() == slot)
	return prev;
    }
    return -1;
  }
  template <typename P>
  int add_(P &&data, uint32_t code) {
    // std::cout << (ZuCArray<80>{} << ZuBoxPtr(this).hex<false, ZuFmt::Alt<>>() << " ZmLHash::add_() code=" << code << '\n');
    unsigned size = 1U<<bits();

    unsigned count = m_count.load_();
    if (count < (1U<<28) && ((count<<4)>>bits()) >= loadFactor_()) {
      Base::resize();
      size = 1U<<bits();
    }

    if (count >= size) return -1;

    m_count.store_(count + 1);
    // std::cout << (ZuCArray<80>{} << ZuBoxPtr(this).hex<false, ZuFmt::Alt<>>() << " ZmLHash::add_() count=" << m_count.load_() << '\n');

    return add__(ZuFwd<P>(data), code);
  }
  template <typename P>
  int add__(P &&data, uint32_t code) {
    // std::cout << (ZuCArray<80>{} << ZuBoxPtr(this).hex<false, ZuFmt::Alt<>>() << " ZmLHash::add__() code=" << code << '\n');
    unsigned size = 1U<<bits();
    unsigned slot = code & (size - 1);
    // std::cout << (ZuCArray<80>{} << ZuBoxPtr(this).hex<false, ZuFmt::Alt<>>() << " ZmLHash::add__() size=" << size << " slot=" << slot << '\n');

    if (!m_table[slot]) {
      m_table[slot].init(1, 1, 0, ZuFwd<P>(data));
      return slot;
    }

    int vacant = alloc(slot);
    if (vacant < 0) return -1;
    // std::cout << (ZuCArray<80>{} << ZuBoxPtr(this).hex<false, ZuFmt::Alt<>>() << " ZmLHash::add__() vacant slot=" << vacant << '\n');

    if (m_table[slot].head()) {
      (m_table[vacant] = ZuMv(m_table[slot])).clrHead();
      m_table[slot].init(1, 0, vacant, ZuFwd<P>(data));
      // std::cout << (ZuCArray<80>{} << ZuBoxPtr(this).hex<false, ZuFmt::Alt<>>() << " ZmLHash::add__() head slot=" << slot << '\n');
      return slot;
    }

    int prev = this->prev(slot);
    if (prev < 0) return -1;
    // std::cout << (ZuCArray<80>{} << ZuBoxPtr(this).hex<false, ZuFmt::Alt<>>() << " ZmLHash::add__() prev slot=" << prev << '\n');

    m_table[vacant] = ZuMv(m_table[slot]);
    m_table[prev].next(vacant);
    m_table[slot].init(1, 1, 0, ZuFwd<P>(data));
    // std::cout << (ZuCArray<80>{} << ZuBoxPtr(this).hex<false, ZuFmt::Alt<>>() << " ZmLHash::add__() add slot=" << slot << '\n');
    return slot;
  }

  template <typename U, typename V = Key>
  struct IsKey : public ZuBool<ZuIsConvertible<U, V>{}> { };
  template <typename U, typename R = void>
  using MatchKey = ZuIfT<IsKey<U>{}, R>;
  template <typename U, typename V = T>
  struct IsData : public ZuBool<!IsKey<U>{} && ZuIsConvertible<U, V>{}> { };
  template <typename U, typename R = void>
  using MatchData = ZuIfT<IsData<U>{}, R>;

  template <typename P>
  static ZuInline auto matchKey(const P &key) {
    return [&key](const Node *node) -> bool {
      // std::cout << (ZuCArray<80>{} << "matchKey() node key=" << node->Node::key() << " val=" << node->Node::val() << " key=" << key << '\n');
      return Cmp::equals(node->Node::key(), key);
    };
  }
  template <typename P>
  static ZuInline auto matchData(const P &data) {
    return [&data](const Node *node) -> bool {
      return node->Node::data() == data;
    };
  }

public:
  template <typename P>
  MatchKey<P, bool> exists(const P &key) const {
    uint32_t code = HashFn::hash(key);
    ReadGuard guard(const_cast<Lock &>(m_lock));
    return find_(matchKey(key), code) >= 0;
  }
  template <typename P>
  MatchData<P, bool> exists(const P &data) const {
    uint32_t code = HashFn::hash(KeyAxor(data));
    ReadGuard guard(const_cast<Lock &>(m_lock));
    return find_(matchData(data), code) >= 0;
  }

  template <typename P>
  MatchKey<P, const T *> find(const P &key) const {
    uint32_t code = HashFn::hash(key);
    ReadGuard guard(const_cast<Lock &>(m_lock));
    return ptr_(find_(matchKey(key), code));
  }
  template <typename P>
  MatchData<P, const T *> find(const P &data) const {
    uint32_t code = HashFn::hash(KeyAxor(data));
    ReadGuard guard(const_cast<Lock &>(m_lock));
    return ptr_(find_(matchData(data), code));
  }
  template <typename P0, typename P1>
  const T *find(P0 &&p0, P1 &&p1) {
    return find(ZuFwdTuple(ZuFwd<P0>(p0), ZuFwd<P1>(p1)));
  }

  template <typename P>
  MatchKey<P, Key> findKey(const P &key) const {
    uint32_t code = HashFn::hash(key);
    ReadGuard guard(const_cast<Lock &>(m_lock));
    return key_(find_(matchKey(key), code));
  }
  template <typename P>
  MatchData<P, Key> findKey(const P &data) const {
    uint32_t code = HashFn::hash(KeyAxor(data));
    ReadGuard guard(const_cast<Lock &>(m_lock));
    return key_(find_(matchData(data), code));
  }
  template <typename P0, typename P1>
  Key findKey(P0 &&p0, P1 &&p1) {
    return findKey(ZuFwdTuple(ZuFwd<P0>(p0), ZuFwd<P1>(p1)));
  }

  template <typename P>
  MatchKey<P, Val> findVal(const P &key) const {
    uint32_t code = HashFn::hash(key);
    ReadGuard guard(const_cast<Lock &>(m_lock));
    return val_(find_(matchKey(key), code));
  }
  template <typename P>
  MatchData<P, Val> findVal(const P &data) const {
    uint32_t code = HashFn::hash(KeyAxor(data));
    ReadGuard guard(const_cast<Lock &>(m_lock));
    return val_(find_(matchData(data), code));
  }
  template <typename P0, typename P1>
  Val findVal(P0 &&p0, P1 &&p1) {
    return findVal(ZuFwdTuple(ZuFwd<P0>(p0), ZuFwd<P1>(p1)));
  }

private:
  template <typename Match>
  int find_(Match match, uint32_t code) const {
    // std::cout << (ZuCArray<80>{} << ZuBoxPtr(this).hex<false, ZuFmt::Alt<>>() << " ZmLHash::find_() code=" << code << '\n');
    unsigned size = 1U<<bits();
    unsigned slot = code & (size - 1);
    // std::cout << (ZuCArray<80>{} << ZuBoxPtr(this).hex<false, ZuFmt::Alt<>>() << " ZmLHash::find_() slot=" << slot << '\n');
    if (!m_table[slot] || !m_table[slot].head()) {
      // std::cout << (ZuCArray<80>{} << ZuBoxPtr(this).hex<false, ZuFmt::Alt<>>() << " ZmLHash::find_() empty slot=" << slot << '\n');
      return -1;
    }
    for (;;) {
      if (match(&m_table[slot])) {
	// std::cout << (ZuCArray<80>{} << ZuBoxPtr(this).hex<false, ZuFmt::Alt<>>() << " ZmLHash::find_() found slot=" << slot << '\n');
	return slot;
      }
      if (m_table[slot].tail()) {
	// std::cout << (ZuCArray<80>{} << ZuBoxPtr(this).hex<false, ZuFmt::Alt<>>() << " ZmLHash::find_() tail slot=" << slot << '\n');
	return -1;
      }
      slot = m_table[slot].next();
      // std::cout << (ZuCArray<80>{} << ZuBoxPtr(this).hex<false, ZuFmt::Alt<>>() << " ZmLHash::find_() next slot=" << slot << '\n');
    }
  }
  template <typename Match>
  int findPrev_(Match match, uint32_t code) const {
    // std::cout << (ZuCArray<80>{} << ZuBoxPtr(this).hex<false, ZuFmt::Alt<>>() << " ZmLHash::findPrev_() code=" << code << '\n');
    unsigned size = 1U<<bits();
    unsigned slot = code & (size - 1);
    // std::cout << (ZuCArray<80>{} << ZuBoxPtr(this).hex<false, ZuFmt::Alt<>>() << " ZmLHash::findPrev_() slot=" << slot << '\n');
    if (!m_table[slot] || !m_table[slot].head()) {
      // std::cout << (ZuCArray<80>{} << ZuBoxPtr(this).hex<false, ZuFmt::Alt<>>() << " ZmLHash::findPrev_() empty slot=" << slot << '\n');
      return -1;
    }
    int prev = -1;
    for (;;) {
      if (match(&m_table[slot])) {
	// std::cout << (ZuCArray<80>{} << ZuBoxPtr(this).hex<false, ZuFmt::Alt<>>() << " ZmLHash::findPrev_() found slot=" << slot << '\n');
	prev = prev < 0 ? (-int(slot) - 2) : prev;
	// std::cout << (ZuCArray<80>{} << ZuBoxPtr(this).hex<false, ZuFmt::Alt<>>() << " ZmLHash::findPrev_() found prev=" << prev << '\n');
	return prev;
      }
      if (m_table[slot].tail()) {
	// std::cout << (ZuCArray<80>{} << ZuBoxPtr(this).hex<false, ZuFmt::Alt<>>() << " ZmLHash::findPrev_() tail slot=" << slot << '\n');
	return -1;
      }
      prev = slot, slot = m_table[slot].next();
      // std::cout << (ZuCArray<80>{} << ZuBoxPtr(this).hex<false, ZuFmt::Alt<>>() << " ZmLHash::findPrev_() next slot=" << slot << '\n');
    }
  }

public:
  template <typename P>
  const T *findAdd(P &&data) {
    uint32_t code = HashFn::hash(KeyAxor(data));
    Guard guard(m_lock);
    return this->ptr_(findAdd__(ZuFwd<P>(data), code));
  }
  template <typename P0, typename P1>
  const T *findAdd(P0 &&p0, P1 &&p1) {
    return findAdd(ZuFwdTuple(ZuFwd<P0>(p0), ZuFwd<P1>(p1)));
  }

private:
  template <typename P>
  int findAdd__(P &&data, uint32_t code) {
    // std::cout << (ZuCArray<80>{} << ZuBoxPtr(this).hex<false, ZuFmt::Alt<>>() << " ZmLHash::findAdd__() code=" << code << '\n');
    unsigned size = 1U<<bits();
    unsigned slot = code & (size - 1);
    // std::cout << (ZuCArray<80>{} << ZuBoxPtr(this).hex<false, ZuFmt::Alt<>>() << " ZmLHash::findAdd__() slot=" << slot << '\n');
    if (!!m_table[slot] && m_table[slot].head())
      for (;;) {
	if (m_table[slot].data() == data) return slot;
	if (m_table[slot].tail()) break;
	slot = m_table[slot].next();
	// std::cout << (ZuCArray<80>{} << ZuBoxPtr(this).hex<false, ZuFmt::Alt<>>() << " ZmLHash::findAdd__() next=" << slot << '\n');
      }
    return add_(ZuFwd<P>(data), code);
  }

public:
  template <typename P>
  MatchKey<P> del(const P &key) {
    uint32_t code = HashFn::hash(key);
    Guard guard(m_lock);
    del_(findPrev_(matchKey(key), code));
  }
  template <typename P>
  MatchData<P> del(const P &data) {
    uint32_t code = HashFn::hash(KeyAxor(data));
    Guard guard(m_lock);
    del_(findPrev_(matchData(data), code));
  }
  template <typename P0, typename P1>
  void del(P0 &&p0, P1 &&p1) {
    auto data = ZuFwdTuple(ZuFwd<P0>(p0), ZuFwd<P1>(p1));
    uint32_t code = HashFn::hash(KeyAxor(data));
    Guard guard(m_lock);
    del_(findPrev_(matchData(data), code));
  }

  template <typename P>
  MatchKey<P, Key> delKey(const P &key) {
    uint32_t code = HashFn::hash(key);
    Guard guard(m_lock);
    return delKey_(findPrev_(matchKey(key), code));
  }
  template <typename P>
  MatchData<P, Key> delKey(const P &data) {
    uint32_t code = HashFn::hash(KeyAxor(data));
    Guard guard(m_lock);
    return delKey_(findPrev_(matchData(data), code));
  }
  template <typename P0, typename P1>
  Key delKey(P0 &&p0, P1 &&p1) {
    return delKey(ZuFwdTuple(ZuFwd<P0>(p0), ZuFwd<P1>(p1)));
  }

  template <typename P>
  MatchKey<P, Val> delVal(const P &key) {
    uint32_t code = HashFn::hash(key);
    Guard guard(m_lock);
    return delVal_(findPrev_(matchKey(key), code));
  }
  template <typename P>
  MatchData<P, Val> delVal(const P &data) {
    uint32_t code = HashFn::hash(KeyAxor(data));
    Guard guard(m_lock);
    return delVal_(findPrev_(matchData(data), code));
  }
  template <typename P0, typename P1>
  Val delVal(P0 &&p0, P1 &&p1) {
    return delVal(ZuFwdTuple(ZuFwd<P0>(p0), ZuFwd<P1>(p1)));
  }

private:
  void del__(int prev) {
    int slot;

    // std::cout << (ZuCArray<80>{} << ZuBoxPtr(this).hex<false, ZuFmt::Alt<>>() << " ZmLHash::del__() prev=" << prev << '\n');

    if (prev < 0)
      slot = (-prev - 2), prev = -1;
    else
      slot = m_table[prev].next();

    // std::cout << (ZuCArray<80>{} << ZuBoxPtr(this).hex<false, ZuFmt::Alt<>>() << " ZmLHash::del__() slot=" << slot << '\n');

    if (!m_table[slot]) {
      // std::cout << (ZuCArray<80>{} << ZuBoxPtr(this).hex<false, ZuFmt::Alt<>>() << " ZmLHash::del__() empty slot=" << slot << '\n');
      return;
    }

    if (unsigned count = m_count.load_())
      m_count.store_(count - 1);

    if (m_table[slot].head()) {
      ZmAssert(prev < 0);
      if (m_table[slot].tail()) {
	// std::cout << (ZuCArray<80>{} << ZuBoxPtr(this).hex<false, ZuFmt::Alt<>>() << " ZmLHash::del__() head+tail slot=" << slot << '\n');
	m_table[slot].null();
	return;
      }
      unsigned next = m_table[slot].next();
      (m_table[slot] = ZuMv(m_table[next])).setHead();
      // std::cout << (ZuCArray<80>{} << ZuBoxPtr(this).hex<false, ZuFmt::Alt<>>() << " ZmLHash::del__() new head slot=" << slot << '\n');
      m_table[next].null();
      // std::cout << (ZuCArray<80>{} << ZuBoxPtr(this).hex<false, ZuFmt::Alt<>>() << " ZmLHash::del__() cleared slot=" << next << '\n');
      return;
    }

    if (m_table[slot].tail()) {
      ZmAssert(prev >= 0);
      if (prev >= 0) m_table[prev].setTail();
      // std::cout << (ZuCArray<80>{} << ZuBoxPtr(this).hex<false, ZuFmt::Alt<>>() << " ZmLHash::del__() new tail slot=" << prev << '\n');
      m_table[slot].null();
      // std::cout << (ZuCArray<80>{} << ZuBoxPtr(this).hex<false, ZuFmt::Alt<>>() << " ZmLHash::del__() cleared slot=" << slot << '\n');
      return;
    }

    unsigned next = m_table[slot].next();
    m_table[slot] = ZuMv(m_table[next]);
    // std::cout << (ZuCArray<80>{} << ZuBoxPtr(this).hex<false, ZuFmt::Alt<>>() << " ZmLHash::del__() moved slot=" << slot << " from next=" << next << '\n');
    m_table[next].null();
    // std::cout << (ZuCArray<80>{} << ZuBoxPtr(this).hex<false, ZuFmt::Alt<>>() << " ZmLHash::del__() cleared slot=" << next << '\n');
  }

  void del_(int prev) {
    if (prev == -1) return;
    del__(prev);
  }
  Key delKey_(int prev) {
    if (prev == -1) return ZuNullRef<Key, Cmp>();
    int slot = prev < 0 ? (-prev - 2) : m_table[prev].next();
    Key key{ZuMv(m_table[slot]).key()};
    del__(prev);
    return key;
  }
  Key delVal_(int prev) {
    if (prev == -1) return ZuNullRef<Key, Cmp>();
    int slot = prev < 0 ? (-prev - 2) : m_table[prev].next();
    Val val{ZuMv(m_table[slot]).val()};
    del__(prev);
    return val;
  }

public:
  void clean() {
    unsigned size = 1U<<bits();
    Guard guard(m_lock);

    for (unsigned i = 0; i < size; i++) m_table[i].null();
    m_count = 0;
  }

  auto iter() { return Iter{*this}; }
  template <typename P>
  auto iter(P key) {
    return KeyIter<P>{*this, ZuMv(key)};
  }

  auto citer() const { return CIter{*this}; }
  template <typename P>
  auto citer(P key) const {
    return ReadKeyIter<P>{*this, ZuMv(key)};
  }

private:
  void iterBegin(Iter_ &iter) {
    iter.lock(m_lock);
    iter.slot = -1;
    int next = -1;
    int size = 1<<bits();
    while (++next < size)
      if (!!m_table[next]) {
	iter.next = next;
	return;
      }
    iter.next = -1;
  }
  template <typename IKey>
  void iterBegin(KeyIter_<IKey> &iter) {
    iter.lock(m_lock);
    iter.slot = -1;
    int prev =
      findPrev_(matchKey(iter.key_), HashFn::hash(iter.key_));
    if (prev == -1) {
      iter.next = iter.prev = -1;
      // std::cout << (ZuCArray<80>{} << ZuBoxPtr(this).hex<false, ZuFmt::Alt<>>() << " ZmLHash::iterBegin(" << iter.key_ << ") not found\n");
      return;
    }
    if (prev < 0)
      iter.next = -prev - 2, iter.prev = -1;
    else
      iter.next = m_table[iter.prev = prev].next();
    // std::cout << (ZuCArray<80>{} << ZuBoxPtr(this).hex<false, ZuFmt::Alt<>>() << " ZmLHash::iterBegin(" << iter.key_ << ") found slot=" << iter.next << " prev=" << iter.prev << "\n");
  }
  void iterate_(Iter_ &iter) {
    int next = iter.next;
    if (next < 0) {
      iter.slot = -1;
      return;
    }
    iter.slot = next;
    int size = 1<<bits();
    while (++next < size)
      if (!!m_table[next]) {
	iter.next = next;
	return;
      }
    iter.next = -1;
  }
  template <typename IKey>
  void iterate_(KeyIter_<IKey> &iter) {
    iter.prev = iter.slot;
    iter.slot = iter.next;
    int next = iter.next;
    if (next < 0) {
  end:
      iter.slot = iter.next = -1;
      // std::cout << (ZuCArray<80>{} << ZuBoxPtr(this).hex<false, ZuFmt::Alt<>>() << " ZmLHash::iterate_(" << iter.key_ << ") ended\n");
      return;
    }
    // std::cout << (ZuCArray<80>{} << ZuBoxPtr(this).hex<false, ZuFmt::Alt<>>() << " ZmLHash::iterate_(" << iter.key_ << ") next slot=" << next << "\n");
    while (!Cmp::equals(m_table[next].key(), iter.key_)) {
      if (m_table[next].tail()) goto end;
      iter.prev = next;
      next = m_table[next].next();
      // std::cout << (ZuCArray<80>{} << ZuBoxPtr(this).hex<false, ZuFmt::Alt<>>() << " ZmLHash::iterate_(" << iter.key_ << ") next slot=" << next << "\n");
    }
    // std::cout << (ZuCArray<80>{} << ZuBoxPtr(this).hex<false, ZuFmt::Alt<>>() << " ZmLHash::iterate_(" << iter.key_ << ") found slot=" << next << "\n");
    iter.slot = next;
    iter.next = m_table[next].tail() ? -1 : m_table[next].next();
  }
  template <typename I>
  const T *iterate(I &iter) {
    iterate_(iter);
    return ptr_(iter.slot);
  }
  template <typename I>
  decltype(auto) key(I &iter) {
    iterate_(iter);
    return key_(iter.slot);
  }
  template <typename I>
  decltype(auto) val(I &iter) {
    iterate_(iter);
    return val_(iter.slot);
  }
  void iterEnd(Iter_ &iter) {
    iter.unlock(m_lock);
    iter.slot = iter.next = -1;
  }
  void iterDel(Iter_ &iter) {
    int slot = iter.slot;
    if (slot < 0) return;
    bool advanceRegardless =
      !m_table[slot].tail() && int(m_table[slot].next()) < slot;
    del__(m_table[slot].head() ? (-slot - 2) : prev(slot));
    if (!advanceRegardless && !!m_table[slot]) iter.next = slot;
    iter.slot = -1;
  }
  template <typename IKey>
  void iterDel(KeyIter_<IKey> &iter) {
    int slot = iter.slot;
    if (slot < 0) return;
    del__(iter.prev < 0 ? (-slot - 2) : iter.prev);
    iter.slot = -1;
    if (!m_table[slot]) return;
    for (;;) {
      if (Cmp::equals(m_table[slot].key(), iter.key_)) {
	iter.next = slot;
	return;
      }
      if (m_table[slot].tail()) break;
      slot = m_table[slot].next();
    }
    iter.next = -1;
  }

private:
  void telemetry_(ZmHashTelemetry &data) const {
    data.addr = reinterpret_cast<uintptr_t>(this);
    data.loadFactor = loadFactor();
    unsigned count = m_count.load_();
    unsigned bits = this->bits();
    data.effLoadFactor = double(count) / (1<<bits);
    data.nodeSize = sizeof(Node);
    data.count = count;
    data.resized = resized();
    data.bits = bits;
    data.cBits = 0;
    data.linear = true;
    data.shadow = false;
  }
};

template <typename P0, typename P1, typename NTP = ZmLHash_Defaults>
using ZmLHashKV =
  ZmLHash<ZuTuple<P0, P1>,
    ZmLHashKeyVal<ZuTupleAxor<0>(), ZuTupleAxor<1>(), NTP>>;

#endif /* ZmLHash_HH */
