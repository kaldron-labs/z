//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z database

// Zdb is a clustered/replicated in-process/in-memory DB/ORM that includes
// RAFT-like leader election and failover. Zdb dynamically organizes
// cluster hosts into a replication chain from the leader to the
// lowest-priority follower. Replication is async. ZmEngine is used for
// start/stop state management. Zdb applications are stateful back-end
// services that defer to Zdb for activation/deactivation.
// Restart/recovery is from backing data store, then from the cluster
// leader (if the local host itself is not elected leader).

// Principal features:
// - Statically configured tables (intentional design limitation)
// - Plug-in backing data store (mocked for unit-testing)
//   - Currently Postgres, in-memory
// - In-memory write-through object cache
//   - Deferred async writes
//   - In-memory write queue of I/O buffers
// - Async replication independent of backing store
//   (can be disabled for replicated backing stores)
// - Primary and multiple-secondary unique in-memory and on-disk indices
// - Find, insert, update, delete operations (Find and CRUD)
// - Batched select and count queries (index-based, optionally grouped)
// - Front-end shares threads with the application
// - Optional data sharding for multi-threaded concurrency

// select() is an un-cached backing data store query that
// returns 0..N immutable ZuTuples for read-only purposes
// - cache consistency is assured by enqueuing the select on the
//   back-end write queue, ensuring results reflect any pending updates
//   outstanding at the time of the call; the results may become outdated
//   when eventually processed if further updates are performed while
//   the select itself is outstanding (this is an intentional design
//   limitation)

// insert() inserts new objects (rows)
// find() returns 0..1 mutable ZdbObjects for read-modify-write
// update() updates existing objects
// del() deletes existing objects

//  host state		engine state
//  ==========		============
//  Instantiated	Stopped
//  Initialized		Stopped
//  Electing		!Stopped
//  Active		!Stopped
//  Inactive		!Stopped
//  Stopping		Stopping | StartPending

#ifndef Zdb_HH
#define Zdb_HH

#ifndef ZdbLib_HH
#include <zlib/ZdbLib.hh>
#endif

#include <zlib/ZuTraits.hh>
#include <zlib/ZuCmp.hh>
#include <zlib/ZuHash.hh>
#include <zlib/ZuID.hh>
#include <zlib/ZuPrint.hh>
#include <zlib/ZuInt.hh>
#include <zlib/ZuDerive.hh>

#include <zlib/ZmAssert.hh>
#include <zlib/ZmRef.hh>
#include <zlib/ZmGuard.hh>
#include <zlib/ZmSpecific.hh>
#include <zlib/ZmFn.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmEngine.hh>
#include <zlib/ZmPolyCache.hh>
#include <zlib/ZmPLock.hh>
#include <zlib/ZmLHash.hh>

#include <zlib/ZtString.hh>
#include <zlib/ZtEnum.hh>

#include <zlib/ZePlatform.hh>
#include <zlib/ZiLog.hh>
#include <zlib/ZiAssert.hh>

#include <zlib/ZiFile.hh>
#include <zlib/ZiMultiplex.hh>
#include <zlib/ZiRx.hh>
#include <zlib/ZiTx.hh>

#include <zlib/Zfb.hh>
#include <zlib/ZfbStruct.hh>

#include <zlib/ZvCf.hh>

#include <zlib/ZdbTypes.hh>
#include <zlib/ZdbTelemetry.hh>
#include <zlib/ZdbBuf.hh>
#include <zlib/ZdbMsg.hh>
#include <zlib/ZdbStore.hh>

#if defined(ZDEBUG) && !defined(Zdb_DEBUG)
#define Zdb_DEBUG 1
#endif

#if Zdb_DEBUG
#define ZdbDEBUG(db, e) \
  do { if ((db)->debug()) ZiLOG(Debug, "Zdb", (e)); } while (0)
#else
#define ZdbDEBUG(db, e) (void())
#endif

// Zdb-specific object traits and hooks
//
// ZdbHeapID(T *)	- heap ID for objects
// ZdbBufHeapID(T *)	- heap ID for buffers
// ZdbBufSize(T *)	- buffer size
// ZdbEvictHook(T *)	- hook into cache eviction

// type-specific cache ID, e.g.
// struct Foo {
//   ...
//   friend ZuStringT<"Foo"> ZdbHeapID(Foo *);
// };
ZuStringT<"Zdb.Object"> ZdbHeapID(...); // default
template <typename U>
struct Zdb_HeapID_ { using T = decltype(ZdbHeapID(ZuDeclVal<U *>())); };
template <typename U>
using Zdb_HeapID = typename Zdb_HeapID_<ZuDecay<U>>::T;

void ZdbEvictHook(...); // default
template <typename U>
struct Zdb_EvictHook_ {
  using T = decltype(ZdbEvictHook(ZuDeclVal<U *>()));
};
template <typename U>
using Zdb_EvictHook = typename Zdb_EvictHook_<ZuDecay<U>>::T;

namespace Zdb_ {

// --- pre-declarations

class DB;				// database
class Host;				// cluster host
class AnyTable;				// untyped table
template <typename T> class Table;	// typed table
struct Record_Print;

// --- replication connection

class Cxn_ :
  public ZiConnection,
  public ZiRx<Cxn_, RxBufAlloc>,
  public ZiTx<Cxn_> {
friend DB;
friend Host;
friend AnyTable;

  using BufAlloc = Zdb_::RxBufAlloc;

  using Rx = ZiRx<Cxn_, BufAlloc>;
  using Tx = ZiTx<Cxn_>;

  using Rx::recv; // de-conflict with ZiConnection
  using Tx::send; // ''

protected:
  Cxn_(DB *db, Host *host, const ZiCxnInfo &ci);

private:
  DB *db() const { return m_db; }
  void host(Host *host) { m_host = host; }
  Host *host() const { return m_host; }

  void connected(ZiIOContext &);
  void disconnected(bool);

  void msgRead(ZiIOContext &);
  int msgRead2(ZmRef<IOBuf>);
  void msgRead3(ZmRef<IOBuf>);

  void hbRcvd(const fbs::Heartbeat *);
  void hbTimeout();
  void hbSend();

  void repRecordRcvd(ZmRef<IOBuf>);
  void repCommitRcvd(ZmRef<IOBuf>);

private:
  DB			*m_db;
  Host			*m_host;	// nullptr if not yet associated

  ZmScheduler::Timer	m_hbTimer;
};
ZuDerive(CxnList,
  (ZmList<Cxn_,
    ZmListNode<Cxn_,
      ZmListHeapID<"Zdb.Cxn">>>));
using Cxn = CxnList::Node;

// --- DB state - SN and key/value linear hash from {table ID, shard} -> UN

ZuDerive(DBState_, (ZmLHashKV<ZuTuple<IDString, Shard>, UN, ZmLHashLocal<>>));
struct DBState : public DBState_ {
  SN		sn = 0;

  DBState() = delete;

  DBState(unsigned size) : DBState_{ZmHashParams{size}} { }

  DBState(const fbs::DBState *dbState) :
    DBState_{ZmHashParams{dbState->tableStates()->size()}},
    sn{ZfbTransform::UInt128::load(dbState->sn())}
  {
    Zfb::Load::all(dbState->tableStates(),
	[this](unsigned, const fbs::TableState *tableState) {
	  add(Zfb::Load::str(tableState->table()), tableState->un());
	});
  }
  void load(const fbs::DBState *dbState) {
    sn = ZfbTransform::UInt128::load(dbState->sn());
    Zfb::Load::all(dbState->tableStates(),
	[this](unsigned, const fbs::TableState *tableState) {
	  update(Zfb::Load::str(tableState->table()), tableState->un());
	});
  }
  Zfb::Offset<fbs::DBState> save(Zfb::Builder &fbb) const {
    auto sn_ = ZfbTransform::UInt128::save(sn);
    auto i = citer();
    return fbs::CreateDBState(
      fbb, &sn_, Zfb::Save::vectorIter<fbs::TableState>(
	fbb, i.count(),
	[&i](Zfb::Builder &fbb, unsigned) {
	  if (auto state = i())
	    return fbs::CreateTableState(
	      fbb,
	      Zfb::Save::str(fbb, state->template p<0>().template p<0>()),
	      state->template p<1>(),
	      state->template p<0>().template p<1>());
	  else
	    return Zfb::Offset<fbs::TableState>{};
	}));
  }

  void reset() {
    sn = 0;
    clean();
  }

  bool updateSN(SN sn_) {
    if (sn < sn_) {
      sn = sn_;
      return true;
    }
    return false;
  }
  bool update(ZuTuple<ZuCSpan, Shard> key, UN un_) {
    auto state = find(key);
    if (!state) {
      add(key, un_);
      return true;
    }
    auto &un = const_cast<T *>(state)->template p<1>();
    if (un < un_) {
      un = un_;
      return true;
    }
    return false;
  }
  DBState &operator |=(const DBState &r) {
    if (ZuLikely(this != &r)) {
      updateSN(r.sn);
      auto i = r.citer();
      while (auto rstate = i())
	update(rstate->template p<0>(), rstate->template p<1>());
    }
    return *this;
  }
  DBState &operator =(const DBState &r) {
    if (ZuLikely(this != &r)) {
      clean();
      this->operator |=(r);
    }
    return *this;
  }

  int cmp(const DBState &r) const {
    return (sn > r.sn) - (sn < r.sn);
  }

  template <typename S> void print(S &) const;
  friend ZuPrintFn ZuPrintType(DBState *);
};

// --- generic object

// possible object state paths:
//
// Undefined > Insert			insert
// Insert > Committed			insert committed
// Insert > Undefined			insert aborted
// Committed > Update > Committed	update committed or aborted
// Committed > Delete > Deleted		delete committed
// Committed > Delete > Committed	delete aborted
//
// path forks:
//
// Insert > Committed | Undefined
// Delete > Deleted | Committed
//
// possible event sequences:
//
// insert, commit
// insert, abort
// update, commit
// update, abort
// del, commit
// del, abort
//
// events and state transitions:
//
// insert	Undefined > Insert
// commit	Insert > Committed
// abort	Insert > Undefined
// update	Committed > Update
// commit	Update > Committed
// abort	Update > Committed
// del		Committed > Delete
// commit	Delete > Deleted
// abort	Delete > Committed

class ZdbAPI AnyObject : public ZmPolymorph {
  AnyObject() = delete;
  AnyObject(const AnyObject &) = delete;
  AnyObject &operator =(const AnyObject &) = delete;
  AnyObject(AnyObject &&) = delete;
  AnyObject &operator =(AnyObject &&) = delete;

  friend AnyTable;
  template <typename> friend class Table;

public:
  AnyObject(AnyTable *table, Shard shard) :
    m_table{table}, m_shard{shard} { }

  AnyTable *table() const { return m_table; }
  Shard shard() const { return m_shard; }
  UN un() const { return m_un; }
  SN sn() const { return m_sn; }
  VN vn() const { return m_vn; }
  int state() const { return m_state; }	// ObjState
  UN origUN() const { return m_origUN; }
  bool evicted() const { return m_pinCount < 0; }
  bool pinned() const { return m_pinCount > 0; }

  ZmRef<IOBuf> replicate(int type);

  virtual void *ptr_() { return nullptr; }
  const void *ptr_() const { return const_cast<AnyObject *>(this)->ptr_(); }

  virtual void evict() {
    ZiAssert(m_pinCount <= 0, "Zdb", (), "invalid evict()", return);
    m_pinCount = -1;
  }
  void pin() {
    ZiAssert(m_pinCount >= 0, "Zdb", (), "invalid pin()", return);
    ++m_pinCount;
  }
  void unpin() {
    ZiAssert(m_pinCount > 0, "Zdb", (), "invalid unpin()", return);
    --m_pinCount;
  }

  template <typename S> void print(S &s) const;
  friend ZuPrintFn ZuPrintType(AnyObject *);

private:
  void init(Shard shard, UN un, SN sn, VN vn) {
    m_shard = shard;
    m_un = un;
    m_sn = sn;
    m_vn = vn;
    m_state = ObjState::Committed;
  }

  bool insert_(UN un);
  bool update_(UN un);
  bool del_(UN un);
  bool commit_();
  bool abort_();

  AnyTable	*m_table;
  UN		m_un = nullUN();
  SN		m_sn = nullSN();
  VN		m_vn = 0;
  UN		m_origUN = nullUN();
  int64_t	m_pinCount = 0;
  Shard		m_shard = 0;
  int8_t	m_state = ObjState::Undefined;
};

inline UN AnyObject_UNAxor(const ZmRef<AnyObject> &object) {
  return object->un();
}

// temporarily there may be more than one UN referencing a cached object
ZuDerive(CacheUN,
  (ZmHashKV<UN, ZmRef<AnyObject>,
    ZmHashLock<ZmPLock,
      ZmHashHeapID<"Zdb.UpdCache">>>));

// --- typed object

// Zdf data-frames are comprised of series fields that do not form part of
// a primary or secondary key for the object - Zdb skips Zdf fields
// and does not persist them
template <typename Field>
using FieldFilter =
  ZuBool<!ZuTypeIn<ZuFieldProp::Series, typename Field::Props>{}>;

template <typename O, typename Facet = ZuFacet::Core>
using Fields = ZuTypeGrep<FieldFilter, ZuFields<O, Facet>>;

template <typename T> class Table;

template <typename T_>
class Object_ : public AnyObject {
public:
  using T = T_;

  Object_(Table<T> *table_, Shard shard) : AnyObject{table_, shard} { }

  Object_() = delete;
  Object_(const Object_ &) = delete;
  Object_ &operator =(const Object_ &) = delete;
  Object_(Object_ &&) = delete;
  Object_ &operator =(Object_ &&) = delete;

  Table<T> *table() const {
    return static_cast<Table<T> *>(AnyObject::table());
  }

  void *ptr_() { return &m_data[0]; }
  const void *ptr_() const { return &m_data[0]; }

  T *ptr() { return reinterpret_cast<T *>(&m_data[0]); }
  const T *ptr() const { return reinterpret_cast<const T *>(&m_data[0]); }

  ~Object_() { ptr()->~T(); }

  const T &data() const & { return *ptr(); }
  T &data() & { return *ptr(); }
  T &&data() && { return ZuMv(*ptr()); }

  ZmRef<IOBuf> commit();
  bool abort();

  // transform original fields, overriding get/set
  template <typename Base>
  struct Adapter : public Base {
    using Orig = Base;
    template <template <typename> class Override>
    using Adapt = Adapter<Override<Orig>>;
    using O = Object_;
    // using decltype(auto) here creates a circular dependency
    static decltype(Orig::get(ZuDeclVal<const T &>())) get(const O &o) {
      return Orig::get(o.data());
    }
    static decltype(Orig::get(ZuDeclVal<T &>())) get(O &o) {
      return Orig::get(o.data());
    }
    static decltype(Orig::get(ZuDeclVal<T &&>())) get(O &&o) {
      return Orig::get(ZuMv(o).data());
    }
    template <typename U> static void set(O &o, U &&v) {
      return Orig::set(o.data(), ZuFwd<U>(v));
    }
    // remove any Ctor property
  private:
    template <typename>
    struct CtorFilter : public ZuTrue { };
    template <unsigned J>
    struct CtorFilter<ZuFieldProp::Ctor<J>> : public ZuFalse { };
  public:
    using Props = ZuTypeGrep<CtorFilter, typename Orig::Props>;
  };
  template <typename Field>
  using Map = typename Field::template Adapt<Adapter>;
  // bind Fields
  template <typename Facet>
  using Fields = ZuTypeMap<Map, Zdb_::Fields<T, Facet>>;
  template <typename Facet>
  friend Fields<Facet> ZuFields_(Object_ *, Facet *);

  friend Object_ ZuStructured_(Object_ *);

private:
  alignas(T) uint8_t	m_data[sizeof(T)];
};

// typed object cache
template <typename T>
ZuDerive(Cache,
  (ZmPolyCache<Object_<T>,
    ZmPolyCacheHeapID_<Zdb_HeapID<T>>>));

// typed object
template <typename T>
struct Object : public Cache<T>::Node {
  ZuDerive_(Object, Cache<T>::Node)
  using Object_<T>::data;	// disambiguate from Node::data

  void evict() {
    using Hook = Zdb_EvictHook<T>;
    if constexpr (!ZuIsSame<void, Hook>{}) Hook::evict(this);
    AnyObject::evict();
  }
};

// --- table configuration

struct TableCf {
  ZuDerive(SIDArray,
    (ZtArray<unsigned, ZtArrayHeapID<"Zdb.TableCf.SIDArray">>));

  // nShards and threads.length() must both be a power of 2
  // threads.length() must be <= nShards
  // nShards must be <= 64
  // nShards is immutable for the table, i.e. is an upper concurrency limit

  IDString		id;
  unsigned		nShards = 1;	// #shards
  ZvCfStringVec		threads;	// threads
  mutable SIDArray	sids = 0;	// thread slot IDs
  int			cacheMode = CacheMode::Normal;

  TableCf() = default;
  TableCf(ZuCSpan id_) : id{id_} { }
  TableCf(ZuCSpan id_, const ZvCf *cf) : id{id_} {
    nShards = cf->getScalar<unsigned>("shards", 1, 64, 1);
    const auto &threads_ = cf->getStringVec("threads");
    if (threads_) {
      unsigned nThreads = threads_.length();
      // ensure nThreads is a power of 2 and <= nShards
      if ((nThreads & (nThreads - 1)) || nThreads > nShards)
	throw ZeEXCEPT(Error, "Zdb", ([
	  key = ZeString{fullKey(cf, "threads")}, nThreads, nShards = nShards
	](auto &s) {
	  s << '"' << key << "\" invalid array size " << nThreads
	    << " (" << nShards << " shards)";
	}));
      threads = threads_;
    }
    cacheMode = cf->getEnum<CacheMode::Map, int>(
      "cacheMode", CacheMode::Normal);
  }

  static const auto &IDAxor(const TableCf &cf) { return cf.id; }
};

// --- table configuration

ZuDerive(TableCfs,
  (ZmRBTree<TableCf,
    ZmRBTreeKey<TableCf::IDAxor,
      ZmRBTreeUnique<true,
	ZmRBTreeHeapID<"Zdb.TableCf">>>>));

// --- generic table

struct TableTelemetry;

class ZdbAPI AnyTable : public ZmPolymorph {
friend DB;
friend Cxn_;
friend AnyObject;
friend Record_Print;	// uses objPrintFB

protected:
  AnyTable(DB *db, TableCf *cf, IOBufAllocFn);

public:
  ~AnyTable() noexcept;

private:
  template <typename L> void open(L &&l);	// l(OpenResult)
  bool opened(OpenResult);
  template <typename L> void close(L &&l);	// l()
protected:
  void warmup() { m_storeTbl->warmup(); }

public:
  DB *db() const { return m_db; }
  ZiMultiplex *mx() const { return m_mx; }
  const TableCf &config() const { return *m_cf; }
  IOBufAllocFn bufAllocFn() const { return m_bufAllocFn; }

  static const auto &IDAxor(AnyTable *table) { return table->config().id; }

  const auto &id() const { return config().id; }
  auto sid(Shard shard) const {
    const auto &config = this->config();
    return config.sids[shard & (config.sids.length() - 1)];
  }

  // DB thread (may be shared)
  template <typename ...Args>
  void run(Shard shard, Args &&...args) const {
    m_mx->run(ZuFwd<Args>(args)..., sid(shard));
  }
  template <typename ...Args>
  void invoke(Shard shard, Args &&...args) const {
    m_mx->invoke(ZuFwd<Args>(args)..., sid(shard));
  }
  bool invoked(Shard shard) const {
    return m_mx->invoked(sid(shard));
  }

  // record count - SWMR
  uint64_t count() const { return m_count.load_(); }

  // allocate I/O buffer
  ZmRef<IOBuf> allocBuf() { return m_bufAllocFn(); }

private:
  IOBuf *findBufUN(Shard shard, UN un) {
    return static_cast<IOBuf *>(m_bufCacheUN[shard]->find(un));
  }
protected:
  void cacheBufUN(Shard shard, IOBuf *buf) {
    m_bufCacheUN[shard]->addNode(buf);
  }
  auto evictBufUN(Shard shard, UN un) {
    return m_bufCacheUN[shard]->del(un);
  }

public:
  // next UN that will be allocated
  UN nextUN(Shard shard) const { return m_nextUN[shard]; }

  // enable/disable writing to cache (temporarily)
  void writeCache(bool enabled) { m_writeCache = enabled; }

  // all transactions begin with a insert(), update() or del(),
  // and complete with object->commit() or object->abort()

protected:
  // --- implemented by Table<T>

  // objSave(fbb, ptr) - save object into flatbuffer, return offset
  virtual Zfb::Offset<void> objSave(Zfb::Builder &, const void *) const = 0;
  virtual Zfb::Offset<void> objSaveUpd(Zfb::Builder &, const void *) const = 0;
  virtual Zfb::Offset<void> objSaveDel(Zfb::Builder &, const void *) const = 0;
  // objRecover(record) - process recovered FB record (untrusted source)
  virtual void objRecover(const fbs::Record *) = 0;

  // objFields() - run-time field array
  virtual ZtVFieldArray objFields() const = 0;
  // objKeyFields() - run-time key field arrays
  virtual ZtVKeyFieldArray objKeyFields() const = 0;
  // objSchema() - flatbuffer reflection schema
  virtual const reflection::Schema *objSchema() const = 0;

  // objPrint(stream, ptr) - print object
  virtual void objPrint(ZuVStream &, const void *) const = 0;
  // objPrintFB(stream, data) - print buffer
  virtual void objPrintFB(ZuVStream &, ZuBSpan) const = 0;

  // buffer cache
  virtual void cacheBuf_(Shard shard, ZmRef<IOBuf>) = 0;
  virtual ZmRef<IOBuf> evictBuf_(Shard shard, IOBuf *) = 0;

  // cache statistics
  virtual void cacheStats(Shard shard, ZmCacheStats &stats) const = 0;

public:
  friend TableTelemetry;

  Zfb::Offset<void> telemetry(Zfb::Builder &fbb, bool update) const;

protected:
  bool writeCache() const { return m_writeCache; }

  auto findUN(Shard shard, UN un) const {
    return m_cacheUN[shard]->findVal(un);
  }
  void cacheUN(Shard shard, UN un, AnyObject *object) {
    m_cacheUN[shard]->add(un, object);
  }
  void evictUN(Shard shard, UN un) {
    m_cacheUN[shard]->del(un);
  }

  StoreTbl *storeTbl() const { return m_storeTbl; }

protected:
  // cache replication buffer
  void cacheBuf(Shard shard, ZmRef<IOBuf>);
  // evict replication buffer
  void evictBuf(Shard shard, UN un);

  // outbound replication / write to backing data store
  void write(Shard shard, ZmRef<IOBuf> buf, bool active);

  // maintain record count
  void incCount() { ++m_count; }
  void decCount() { --m_count; }

private:
  // low-level write to backing data store
  void store(Shard shard, ZmRef<IOBuf>);
  void store_(Shard shard, ZmRef<IOBuf>);
  void committed(ZmRef<IOBuf>, CommitResult);

  // outbound recovery / replication
  void recSend(ZmRef<Cxn> cxn, Shard shard, UN un, UN endUN);
  void recSend_(
    ZmRef<Cxn> cxn, Shard shard, UN un, UN endUN, ZmRef<IOBuf> buf);
  void recNext(ZmRef<Cxn> cxn, Shard shard, UN un, UN endUN);
  ZmRef<IOBuf> mkBuf(Shard shard, UN un);
  void commitSend(Shard shard, UN un);

  // inbound replication
  void repRecordRcvd(Shard shard, ZmRef<IOBuf> buf);
  void repCommitRcvd(Shard shard, UN un);

  // recovery - DB thread
  void recover(Shard shard, const fbs::Record *record);

  // UN
  bool allocUN(Shard shard, UN un) {
    if (ZuUnlikely(un != m_nextUN[shard])) return false;
    ++m_nextUN[shard];
    return true;
  }
  void recoveredUN(Shard shard, UN un) {
    if (ZuUnlikely(un == nullUN())) return;
    if (m_nextUN[shard] <= un) m_nextUN[shard] = un + 1;
  }

  // immutable
  DB			*m_db;
  const TableCf		*m_cf;
  ZiMultiplex		*m_mx;

  // Table threads SWMR
  ZtArray<ZmAtomic<UN>>	m_nextUN;		// UN allocator

  // open/closed state, record count
  ZmAtomic<unsigned>	m_open = 0;		// Table threads SWMR
  ZmAtomic<uint64_t>	m_count = 0;		// ''

  // backing data store table
  StoreTbl		*m_storeTbl = nullptr;	// Table threads

  // object cache indexed by UN (sharded)
  ZuDerive(CacheUNArray, (ZtArray<ZmRef<CacheUN>>));
  bool			m_writeCache = true;
  CacheUNArray		m_cacheUN;

  // buffer cache indexed by UN (sharded)
  ZuDerive(BufCacheUNArray, (ZtArray<ZmRef<BufCacheUN>>));
  BufCacheUNArray	m_bufCacheUN;

  // I/O buffer allocation
  IOBufAllocFn		m_bufAllocFn;
};

// replication buffer
// - replication buffers contain a reference to the underlying I/O buffer
// - type information permits type-specific key indexing and caching
template <typename T_>
struct RepBuf_ : public ZmPolymorph {
  ZmRef<IOBuf>	buf;
  bool		stale = false;	// true if outdated by subsequent txn

  RepBuf_(ZmRef<IOBuf> buf_) : buf{ZuMv(buf_)} { buf->rep = this; }

  using T = T_;
  using FB = ZfbType<T>;
  const FB *fbo() const { return buf->fbo<T>(); }
  // trusted buffer (written locally), e.g. in buffer cache
  const FB *fbo_() const { return buf->fbo_<T>(); }

  // transform original fields, overriding get/set
  template <typename Base>
  struct Adapter : public Base {
    using Orig = Base;
    template <template <typename> class Override>
    using Adapt = Adapter<Override<Orig>>;
    using O = RepBuf_;
    enum { ReadOnly = true };
    // using decltype(auto) here creates a circular dependency
    static decltype(Orig::get(ZuDeclVal<const FB &>())) get(const O &o) {
      return Orig::get(*(o.fbo_()));
    }
    template <typename U> static void set(O &, U &&);
  };
  template <typename Field>
  using Map = typename Field::template Adapt<Adapter>;
  // bind Fields
  template <typename Facet>
  using Fields = ZuTypeMap<Map, Zdb_::Fields<FB, Facet>>;
  template <typename Facet>
  friend Fields<Facet> ZuFields_(RepBuf_ *, Facet *);

  friend RepBuf_ ZuStructured_(RepBuf_ *);

  // override printing
  friend ZtStructPrint ZuPrintType(RepBuf_ *);
};

// replication buffer cache
template <typename T>
ZuDerive(BufCache,
  (ZmPolyHash<RepBuf_<T>, ZmPolyHashHeapID_<ZdbBuf_HeapID<T>>>));

// replication buffer
template <typename T>
struct RepBuf : public BufCache<T>::Node {
  ZuDerive_(RepBuf, BufCache<T>::Node)
};

// backing data store count() context
struct Count__ {
  using Result = ZuUnion<void, uint64_t>;
  using Fn = ZmFn<void(Result), ZmFnHeapID<"Zdb.Count.Fn">>;

  Fn	fn;
};
template <typename Heap>
struct Count_ : public Heap, public ZmPolymorph, public Count__ {
  ZuDerive_(Count_, Count__)
};
using Count_Heap = ZmHeap<"Zdb.Count", Count_<ZuVoid>>;
ZuDerive(Count, (Count_<Count_Heap>));

// backing data store select() context
template <typename Tuple> struct Select__ {
  using Result = ZuUnion<void, Tuple>;
  using Fn = ZmFn<void(Result, unsigned), ZmFnHeapID<"Zdb.Select.Fn">>;

  Fn	fn;
};
template <typename Tuple, typename Heap>
struct Select_ : public Heap, public ZmPolymorph, public Select__<Tuple> {
  ZuDerive_(Select_, Select__<Tuple>)
};
template <typename Tuple>
using Select_Heap = ZmHeap<"Zdb.Select", Select_<Tuple, ZuVoid>>;
template <typename Tuple>
ZuDerive(Select, (Select_<Tuple, Select_Heap<Tuple>>));

// backing data store find() context
template <typename T, typename Key> struct Find__ {
  using Fn = ZmFn<void(ZmRef<Object<T>>), ZmFnHeapID<"Zdb.Find.Fn">>;

  Table<T>	*table;
  unsigned	shard;
  Key		key;
  Fn		fn;
};
template <typename T, typename Key, typename Heap>
struct Find_ : public Heap, public ZmPolymorph, public Find__<T, Key> {
  ZuDerive_(Find_, (Find__<T, Key>))
};
template <typename T, typename Key>
using Find_Heap = ZmHeap<"Zdb.Find", Find_<T, Key, ZuVoid>>;
template <typename T, typename Key>
ZuDerive(Find, (Find_<T, Key, Find_Heap<T, Key>>));

// split group keys into group part and grouped part
template <typename O, unsigned KeyID>
struct SplitKey {
  using Key = ZuStructKeyT<O, KeyID>;
  using KeyFields = ZuFields<Key>;
  // - filter fields that are part of a group
  template <typename Field>
  using IsGroup = ZuFieldProp::IsGroup<typename Field::Props, KeyID>;
  // - filter fields that are not part of a group
  template <typename Field>
  using NotGroup = ZuBool<!IsGroup<Field>{}>;
  // - extract group fields from fields comprising a key
  using GroupFields = ZuTypeGrep<IsGroup, KeyFields>;
  // - tuple type for group fields
  using GroupKey = ZuStructTupleT<Key, ZuMkCRef, ZuDecay, IsGroup>;
  // - extract member fields from fields comprising a key
  using MemberFields = ZuTypeGrep<NotGroup, KeyFields>;
  // - tuple type for group fields
  using MemberKey = ZuStructTupleT<Key, ZuMkCRef, ZuDecay, NotGroup>;
  // - filter keys that have 1 or more group fields
  using IsGroupKey = ZuBool<GroupFields::N>;
};

// --- typed table

template <typename T>
class Table : public AnyTable {
friend DB;
friend Cxn_;
friend Object_<T>;

public:
  enum { BufSize = ZdbBuf_Size<T>{} };

  using Fields = Zdb_::Fields<T>;
  using Keys = ZuStructKeys<T>;
  using KeyIDs = ZuStructKeyIDs<T>;
  template <int KeyID> using Key = ZuStructKeyT<T, KeyID>;
  // Tuple is same as ZuStructKeyT<T, ZuStructKeyID::All>
  using Tuple = ZuStructTuple<T>;

  ZuAssert(Fields::N < maxFields());
  ZuAssert(KeyIDs::N < maxKeys());

private:
  // - grouping key for a KeyID
  template <unsigned KeyID>
  using GroupKey = typename SplitKey<T, KeyID>::GroupKey;
  // - grouped key for a KeyID
  template <unsigned KeyID>
  using MemberKey = typename SplitKey<T, KeyID>::MemberKey;

public:
  static ZmRef<IOBuf> allocBuf() { return new IOBufAlloc<T>{}; }

  Table(DB *db, TableCf *cf) : AnyTable{db, cf, Table::allocBuf} {
    unsigned n = cf->nShards;
    ZuID cacheID = "Zdb.Cache."; cacheID << cf->id;
    ZuID bufCacheID = "Zdb.BufCache."; bufCacheID << cf->id;
    m_cache.size(n);
    m_bufCache.size(n);
    for (unsigned i = 0; i < n; i++) {
      new (m_cache.push()) Cache<T>{cacheID};
      new (m_bufCache.push()) BufCache<T>{bufCacheID};
    }
  }

  // buffer allocator
private:
  // objLoad(buf, shard)
  // - construct object from flatbuffer (trusted source)
  ZmRef<Object<T>> objLoad(IOBuf *buf, unsigned shard) {
    auto record = record_(msg_(buf->hdr()));
    if (record->vn() < 0) return {}; // deleted
    auto data = Zfb::Load::bytes(record->data());
    ZiAssert(data, "Zdb",
      (id = this->id()), "missing record data in table " << id, return {});
    auto fbo = ZfbStruct::root<T>(&data[0]);
    ZiAssert(fbo, "Zdb",
      (id = this->id()), "bad record data in table " << id, return {});
    ZmRef<Object<T>> object = new Object<T>(this, shard);
    ZfbStruct::new_<T>(object->ptr(), fbo);
    object->init(
      record->shard(), record->un(),
      ZfbTransform::UInt128::load(record->sn()), record->vn());
    return object;
  }
  // objSave(fbb, ptr) - save object into flatbuffer, return offset
  Zfb::Offset<void> objSave(Zfb::Builder &fbb, const void *ptr) const {
    return ZfbStruct::save(fbb, *static_cast<const T *>(ptr)).Union();
  }
  Zfb::Offset<void> objSaveUpd(Zfb::Builder &fbb, const void *ptr) const {
    return ZfbStruct::saveUpd(fbb, *static_cast<const T *>(ptr)).Union();
  }
  Zfb::Offset<void> objSaveDel(Zfb::Builder &fbb, const void *ptr) const {
    return ZfbStruct::saveDel(fbb, *static_cast<const T *>(ptr)).Union();
  }
  // objRecover(record) - process recovered record (untrusted source)
  void objRecover(const fbs::Record *record) {
    auto fbo = ZfbStruct::verify<T>(Zfb::Load::bytes(record->data()));
    if (!fbo) return;
    auto shard = record->shard();
    // mark outdated buffers as stale
    ZuUnroll::all<KeyIDs>([this, shard, fbo](auto KeyID) {
      auto key = ZuStructKey<KeyID>(*fbo);
      auto i = m_bufCache[shard].template iter<KeyID>(ZuMv(key));
      while (auto repBuf = i()) {
	if (!repBuf->stale) {
	  repBuf->stale = true;
	  break;
	}
      }
    });
    // maintain cache consistency
    if (record->vn() >= 0) {
      // primary key is immutable
      if constexpr (KeyIDs::N > 1)
	// no load or eviction here, this is just a key lookup in the cache
	if (ZmRef<Object<T>> object =
	    m_cache[shard].find(ZuStructKey<0>(*fbo)))
	  m_cache[shard].template update<ZuTypeTail<1, KeyIDs>>(ZuMv(object),
	    [fbo](const ZmRef<Object<T>> &object) {
	      ZfbStruct::update(object->data(), fbo);
	    });
    } else {
      m_cache[shard].template del<0>(ZuStructKey<0>(*fbo));
    }
  }

  // objFields() - run-time field array
  ZtVFieldArray objFields() const { return ZtVFields<T>(); }
  // objKeyFields() - run-time key field arrays
  ZtVKeyFieldArray objKeyFields() const { return ZtVKeyFields<T>(); }
  // objSchema() - flatbuffer reflection schema
  const reflection::Schema *objSchema() const {
    return reflection::GetSchema(ZfbSchema<T>::data());
  }

  // objPrint(stream, ptr) - print object
  void objPrint(ZuVStream &s, const void *ptr) const {
    ZtStructPrint::print(s, *static_cast<const T *>(ptr));
  }
  // objPrintFB(stream, data) - print flatbuffer
  void objPrintFB(ZuVStream &s, ZuBSpan data) const {
    auto fbo = ZfbStruct::verify<T>(data);
    if (!fbo) return;
    s << *fbo;
  }

  // find buffer in buffer cache
  template <unsigned KeyID>
  ZuTuple<ZmRef<IOBuf>, bool>
  findBuf(Shard shard, const Key<KeyID> &key) const {
    auto i = m_bufCache[shard].template iter<KeyID>(key);
    bool found = false;
    while (auto repBuf = i()) {
      if (!repBuf->stale) return {repBuf->buf, true};
      found = true;
    }
    return {ZmRef<IOBuf>{}, found};
  }

  // find, falling through object cache, buffer cache, backing data store
  template <
    unsigned KeyID, bool UpdateLRU, bool Evict, typename L>
  void find_(Shard shard, Key<KeyID>, L &&l);
  // find from backing data store (retried on failure)
  template <unsigned KeyID, typename L>
  void retrieve(Shard shard, Key<KeyID>, L &&);
  template <unsigned KeyID>
  void retrieve_(ZmRef<Find<T, Key<KeyID>>> context);

  // buffer cache
  void cacheBuf_(Shard shard, ZmRef<IOBuf> buf) {
    m_bufCache[shard].add(new RepBuf<T>{ZuMv(buf)});
  }
  ZmRef<IOBuf> evictBuf_(Shard shard, IOBuf *buf) {
    if (auto repBuf = m_bufCache[shard].delNode(
	static_cast<RepBuf<T> *>(static_cast<RepBuf_<T> *>(buf->rep))))
      return ZuMv(repBuf->buf);
    return nullptr;
  }

  // cache statistics
  void cacheStats(Shard shard, ZmCacheStats &stats) const {
    m_cache[shard].stats(stats);
  }

  // mitigate cold start
  void warmup() {
    AnyTable::warmup();
    unsigned n = config().nShards;
    for (unsigned i = 0; i < n; i++)
      run(i, [this, i]() mutable { warmup_(i); });
  }
private:
  void warmup_(Shard shard) {
    // warmup heaps
    ZmRef<Object<T>> object = new Object<T>(this, shard);
    object->init(shard, 0, 0, 0);
    new (object->ptr()) T{};
    // warmup caches
    m_cache[shard].add(object);
    m_cache[shard].delNode(object);
    // warmup UN cache
    cacheUN(shard, 0, object);
    evictUN(shard, 0);
    // warmup buffer cache
    ZmRef<IOBuf> buf = object->replicate(int(fbs::Body::Replication));
    cacheBuf(shard, buf);
    evictBuf(shard, 0);
  }

  template <
    unsigned KeyID,
    typename SelectKey,
    typename Tuple_,
    bool SelectRow,
    bool SelectNext,
    typename L>
  void select_(SelectKey selectKey, bool inclusive, unsigned limit, L &&l);

public:
  // table count is implemented by AnyTable
  uint64_t count() const { return AnyTable::count(); }
  // count query lambda(ZuUnion<void, uint64_t>)
  template <unsigned KeyID, typename L>	// initial
  void count(GroupKey<KeyID> groupKey, L &&l);

  // select query
  // - lambda(ZuUnion<void, ZuTuple<...>>, unsigned count)
  // - count is #results so far, including this one
  template <unsigned KeyID, typename L>	// initial
  void selectKeys(GroupKey<KeyID> groupKey, unsigned limit, L &&l) {
    select_<KeyID, GroupKey<KeyID>, Key<KeyID>, 0, 0>(
      ZuMv(groupKey), false, limit, ZuFwd<L>(l));
  }
  template <unsigned KeyID, typename L>	// continuation from key
  void nextKeys(Key<KeyID> key, bool inclusive, unsigned limit, L &&l) {
    select_<KeyID, Key<KeyID>, Key<KeyID>, 0, 1>(
      ZuMv(key), inclusive, limit, ZuFwd<L>(l));
  }
  template <unsigned KeyID, typename L>	// initial
  void selectRows(GroupKey<KeyID> groupKey, unsigned limit, L &&l) {
    select_<KeyID, GroupKey<KeyID>, Tuple, 1, 0>(
      ZuMv(groupKey), false, limit, ZuFwd<L>(l));
  }
  template <unsigned KeyID, typename L>	// continuation from key
  void nextRows(Key<KeyID> key, bool inclusive, unsigned limit, L &&l) {
    select_<KeyID, Key<KeyID>, Tuple, 1, 1>(
      ZuMv(key), inclusive, limit, ZuFwd<L>(l));
  }

  // find - lambda(ZdbObjRef<T>)
  template <unsigned KeyID, typename L>
  ZuInline void find(Shard shard, Key<KeyID> key, L &&l) {
    config().cacheMode == CacheMode::All ?
      find_<KeyID, true, false>(shard, ZuMv(key), ZuFwd<L>(l)) :
      find_<KeyID, true, true >(shard, ZuMv(key), ZuFwd<L>(l));
  }

private: // RMU version used by findUpd() and findDel()
  template <unsigned KeyID, typename L>
  void findUpd_(Shard shard, Key<KeyID> key, L &&l) {
    config().cacheMode == CacheMode::All ?
      find_<KeyID, false, false>(shard, ZuMv(key), ZuFwd<L>(l)) :
      find_<KeyID, false, true >(shard, ZuMv(key), ZuFwd<L>(l));
  }

public:
  // evict from cache, even if pinned
  template <unsigned KeyID>
  void evict(Shard shard, const Key<KeyID> &key) {
    ZmAssert(invoked(shard));

    ZmRef<Object<T>> object = m_cache[shard].template del<KeyID>(key);
    if (object) {
      if (object->pinned()) object->unpin();
      evictUN(shard, object->un());
      object->evict();
    }
  }
  void evict(Object<T> *object) {
    auto shard = object->shard();

    ZmAssert(invoked(shard));

    m_cache[shard].delNode(object);
    if (object->pinned()) object->unpin();
    evictUN(shard, object->un());
    object->evict();
  }

public:
  // create new object
  // - insert lambda(ZdbObject<T> *)
  template <typename L>
  void insert(ZmRef<Object<T>> object, L &&l) {
    auto shard = object->shard();

    ZmAssert(invoked(shard));

    object->insert_(nextUN(shard));
    try {
      l(object);
    } catch (...) { object->abort(); throw; }
    object->abort();
  }
  // create new object (idempotent with UN as key)
  template <typename L>
  void insert(UN un, ZmRef<Object<T>> object, L &&l) {
    auto shard = object->shard();

    ZmAssert(invoked(shard));

    if (un != nullUN() && ZuUnlikely(nextUN(shard) > un)) {
      l(nullptr);
      return;
    }
    insert(ZuMv(object), ZuFwd<L>(l));
  }

  // update lambda(ZdbObject<T> *)

  // update object
  template <typename KeyIDs_ = ZuSeq<>, typename L>
  void update(ZmRef<Object<T>> object, L &&l) {
    auto shard = object->shard();

    ZmAssert(invoked(shard));

    if (!update_(object.ptr(), nextUN(shard))) {
      l(nullptr);
      return;
    }
    auto bufs = ZmAlloc(ZmRef<RepBuf<T>>, KeyIDs::N);	// undo buffer
    auto nBufs = 0U;
    auto abort = [&object, &bufs, &nBufs]() {
      if (!object->abort()) return;
      for (unsigned i = 0; i < nBufs; i++) {
	bufs[i]->stale = false;
	bufs[i].~ZmRef<RepBuf<T>>();
      }
    };
    ZuUnroll::all<KeyIDs>([this, shard, &object, &bufs, &nBufs](auto KeyID) {
      auto key = ZuStructKey<KeyID>(object->data());
      auto i = m_bufCache[shard].template iter<KeyID>(ZuMv(key));
      while (auto repBuf = i()) {
	if (!repBuf->stale) {
	  repBuf->stale = true;
	  new (&bufs[nBufs++]) ZmRef<RepBuf<T>>{ZuMv(repBuf)};
	  // at most one buffer per key can be fresh
	  break;
	}
      }
    });
    try {
      m_cache[shard].template update<KeyIDs_>(object, [
	l = ZuFwd<L>(l)
      ](typename Cache<T>::Node *node) mutable {
	l(static_cast<Object<T> *>(node));
      });
    } catch (...) { abort(); throw; }
    abort();
  }
  // update object (idempotent) - calls l(null) to skip
  template <typename KeyIDs_ = ZuSeq<>, typename L>
  void update(ZmRef<Object<T>> object, UN un, L &&l) {
    auto shard = object->shard();

    ZmAssert(invoked(shard));

    if (un != nullUN() && ZuUnlikely(nextUN(shard) > un)) {
      l(nullptr);
      return;
    }
    update<KeyIDs_>(ZuMv(object), ZuFwd<L>(l));
  }

  // find and update record (with key, without object)
  template <
    unsigned KeyID, typename KeyIDs_ = ZuSeq<>, typename L>
  ZuInline void findUpd(Shard shard, Key<KeyID> key, L &&l) {
    findUpd_<KeyID>(shard, ZuMv(key),
      [this, l = ZuFwd<L>(l)](ZmRef<Object<T>> object) mutable {
	if (ZuUnlikely(!object)) { l(object); return; }
	update<KeyIDs_>(ZuMv(object), ZuMv(l));
      });
  }
  // find and update record (idempotent) (with key, without object)
  template <
    unsigned KeyID, typename KeyIDs_ = ZuSeq<>, typename L>
  ZuInline void findUpd(Shard shard, Key<KeyID> key, UN un, L &&l) {
    findUpd_<KeyID>(shard, ZuMv(key),
      [this, un, l = ZuFwd<L>(l)](ZmRef<Object<T>> object) mutable {
	if (ZuUnlikely(!object)) { l(object); return; }
	update<KeyIDs_>(ZuMv(object), un, ZuMv(l));
      });
  }

  // delete lambda(ZdbObject<T> *)

  // delete record
  template <typename L>
  void del(ZmRef<Object<T>> object, L &&l) {
    auto shard = object->shard();

    ZmAssert(invoked(shard));

    if (!del_(object.ptr(), nextUN(shard))) {
      l(nullptr);
      return;
    }
    // all object keys are being invalidated, need to:
    // - evict from cache
    // - mark pending buffers indexed by the old keys as stale
    // - revert above actions on abort
    // - note that a new buffer is written by commit(), which
    //   causes a future find() to return null
    auto bufs = ZmAlloc(ZmRef<RepBuf<T>>, KeyIDs::N);	// "undo" buffer
    auto nBufs = 0U;
    auto abort = [&object, &bufs, &nBufs]() {
      if (!object->abort()) return;
      for (unsigned i = 0; i < nBufs; i++) {
	bufs[i]->stale = false;
	bufs[i].~ZmRef<RepBuf<T>>();
      }
    };
    ZuUnroll::all<KeyIDs>([this, shard, &object, &bufs, &nBufs](auto KeyID) {
      auto key = ZuStructKey<KeyID>(object->data());
      auto i = m_bufCache[shard].template iter<KeyID>(ZuMv(key));
      while (auto repBuf = i()) {
	if (!repBuf->stale) {
	  repBuf->stale = true;
	  new (&bufs[nBufs++]) ZmRef<RepBuf<T>>{ZuMv(repBuf)};
	  break;
	}
      }
    });
    try {
      l(object);
    } catch (...) { abort(); throw; }
    abort();
  }
  // delete record (idempotent) - returns true if del can proceed
  template <typename L>
  void del(ZmRef<AnyObject> object, UN un, L &&l) {
    auto shard = object->shard();

    ZmAssert(invoked(shard));

    if (un != nullUN() && ZuUnlikely(nextUN(shard) > un)) {
      l(nullptr);
      return;
    }
    del(ZuMv(object), ZuFwd<L>(l));
  }

  // find and delete record (with key, without object)
  template <unsigned KeyID, typename L>
  ZuInline void findDel(Shard shard, const Key<KeyID> &key, L &&l)
  {
    findUpd_<KeyID>(shard, key,
      [this, l = ZuFwd<L>(l)](ZmRef<Object<T>> object) mutable {
	if (ZuUnlikely(!object)) { l(object); return; }
	del(ZuMv(object), ZuMv(l));
      });
  }

  // find and delete record (idempotent) (with key, without object)
  template <unsigned KeyID, typename L>
  ZuInline void findDel(Shard shard, const Key<KeyID> &key, UN un, L &&l) {
    findUpd_<KeyID>(shard, key,
      [this, un, l = ZuFwd<L>(l)](ZmRef<Object<T>> object) mutable {
	if (ZuUnlikely(!object)) { l(object); return; }
	del(ZuMv(object), un, ZuMv(l));
      });
  }

private:
  // commit insert/update/delete - causes replication/write
  ZmRef<IOBuf> commit(AnyObject *object) {
    auto shard = object->shard();

    ZmAssert(invoked(shard));

    int origState = object->state();
    if (!object->commit_()) return {};
    switch (origState) {
      case ObjState::Insert:
	if (writeCache()) {
	  m_cache[shard].add(object, [this](AnyObject *object) {
	    if (object->pinned()) return false;
	    evictUN(object->shard(), object->un());
	    object->evict();
	    return true;
	  });
	  cacheUN(shard, object->un(), object);
	}
	incCount();
	break;
      case ObjState::Update:
	// evictUN() already called from update_()
	if (writeCache())
	  cacheUN(shard, object->un(), object);
	break;
      case ObjState::Delete:
	// evictUN() already called from del_()
	if (m_cache[shard].delNode(static_cast<Object<T> *>(object)))
	  object->evict();
	decCount();
	break;
    }
    ZmRef<IOBuf> buf = object->replicate(int(fbs::Body::Replication));
    write(shard, buf, true);
    return buf;
  }

  // abort insert/update/delete
  bool abort(AnyObject *object) {
    ZmAssert(invoked(object->shard()));

    return object->abort_();
  }

  // low-level update, calls AnyObject::update_()
  bool update_(Object<T> *object, UN un) {
    evictUN(object->shard(), object->un());
    return object->update_(un);
  }

  // low-level delete, calls AnyObject::del_()
  bool del_(Object<T> *object, UN un) {
    evictUN(object->shard(), object->un());
    return object->del_(un);
  }

private:
  // object caches
  ZuDerive(CacheArray, (ZtArray<Cache<T>>));
  CacheArray			m_cache;

  // pending replications
  ZuDerive(BufCacheArray, (ZtArray<BufCache<T>>));
  BufCacheArray			m_bufCache;
};

template <typename T>
inline ZmRef<IOBuf> Object_<T>::commit() {
  return this->table()->commit(static_cast<AnyObject *>(this));
}
template <typename T>
inline bool Object_<T>::abort() {
  return this->table()->abort(static_cast<AnyObject *>(this));
}

// --- table container

ZuDerive(Tables,
  (ZmRBTree<ZmRef<AnyTable>,
    ZmRBTreeKey<AnyTable::IDAxor,
      ZmRBTreeUnique<true,
	ZmRBTreeHeapID<"Zdb.Table">>>>));

// --- DB host configuration

struct HostCf {
  ZuID		id;
  int		priority = 0;	// -1 is used internally for a failed host
  ZiIP		ip;
  uint16_t	port = 0;
  bool		standalone = false;
  ZvCfString	up;
  ZvCfString	down;

  HostCf(ZuCSpan id_) : id{id_}, standalone{true} { }
  HostCf(ZuCSpan id_, const ZvCf *cf) : id{id_} {
    if (!(standalone = cf->getBool("standalone", false))) {
      priority = cf->getInt<true>("priority", 0, 1<<30);
      ip = cf->get<true>("ip");
      port = cf->getInt<true>("port", 1, (1<<16) - 1);
    }
    up = cf->get("up");
    down = cf->get("down");
  }

  static ZuCSpan IDAxor(const HostCf &cfg) { return cfg.id; }
};

ZuDerive(HostCfs,
  (ZmRBTree<HostCf,
    ZmRBTreeKey<HostCf::IDAxor,
      ZmRBTreeUnique<true,
	ZmRBTreeHeapID<"Zdb.HostCf">>>>));

// --- DB host

struct HostTelemetry;

class ZdbAPI Host {
friend Cxn_;
friend DB;

protected:
  Host(DB *db, const HostCf *cf, unsigned dbCount);

public:
  const HostCf &config() const { return *m_cf; }

  ZuCSpan id() const { return m_cf->id; }
  int priority() const { return m_cf->priority; }
  bool standalone() const { return m_cf->standalone; }
  ZiIP ip() const { return m_cf->ip; }
  uint16_t port() const { return m_cf->port; }

  bool voted() const { return m_voted; }
  int state() const { return m_state; }

  bool replicating() const { return m_cxn; }
  static bool replicating(const Host *host) {
    return host ? host->replicating() : false;
  }

  static const char *stateName(int);

  template <typename S> void print(S &s) const {
    s << "{id=" << id() << ", priority=" << priority()
      << ", voted=" << voted() << ", state=" << state()
      << ", dbState=" << dbState() << '}';
  }
  friend ZuPrintFn ZuPrintType(Host *);

  static ZuCSpan IDAxor(const Host &h) { return h.id(); }
  static ZuTuple<int, ZuID> IndexAxor(const Host &h) {
    return ZuFwdTuple(h.priority(), h.id());
  }

  friend HostTelemetry;

  Zfb::Offset<void> telemetry(Zfb::Builder &fbb, bool update) const;

private:
  ZmRef<Cxn> cxn() const { return m_cxn; }

  void state(int s) { m_state = s; }

  const DBState &dbState() const { return m_dbState; }
  DBState &dbState() { return m_dbState; }

  bool active() const { return m_state == HostState::Active; }

  int cmp(const Host *host) const {
    if (ZuUnlikely(host == this)) return 0;
    int i;
    if (i = m_dbState.cmp(host->m_dbState)) return i;
    if (i = ZuCompare(active(), host->active())) return i;
    return ZuCompare(priority(), host->priority());
  }

  void voted(bool v) { m_voted = v; }

  void connect();
  void connectFailed(bool transient);
  void reconnect();
  void reconnect2();
  void cancelConnect();
  ZiConnection *connected(const ZiCxnInfo &ci);
  void associate(Cxn *cxn);
  void disconnected();

  void reactivate();

  DB			*m_db;
  const HostCf		*m_cf;
  ZiMultiplex		*m_mx;

  ZmScheduler::Timer	m_connectTimer;

  // guarded by DB

  ZmRef<Cxn>		m_cxn;
  int			m_state = HostState::Instantiated;
  DBState		m_dbState;
  bool			m_voted = false;
};

// host container
ZuDerive(HostIndex,
  (ZmRBTree<Host,
    ZmRBTreeNode<Host,
      ZmRBTreeShadow<
	ZmRBTreeKey<Host::IndexAxor,
	  ZmRBTreeUnique<true>>>>>));
ZuDerive(Hosts,
  (ZmHash<HostIndex::Node,
    ZmHashNode<HostIndex::Node,
      ZmHashKey<Host::IDAxor,
	ZmHashHeapID<"Zdb.Host">>>>));

// --- DB handler functions

// UpFn() - activate
typedef void (*UpFn)(DB *, Host *); // db, oldMaster
// DownFn() - de-activate
typedef void (*DownFn)(DB *, bool); // db, failed

struct DBHandler {
  UpFn		upFn = [](DB *, Host *) { };
  DownFn	downFn = [](DB *, bool failed) { };
};

// --- DB configuration

struct DBCf {
  ZuID			thread;
  mutable unsigned	sid = 0;
  ZmRef<ZvCf>		storeCf;
  TableCfs		tableCfs;
  HostCfs		hostCfs;
  ZuID			hostID;
  unsigned		nAccepts = 0;
  unsigned		heartbeatFreq = 0;
  unsigned		heartbeatTimeout = 0;
  unsigned		reconnectFreq = 0;
  unsigned		electionTimeout = 0;
  ZmHashParams		cxnHash;
#if Zdb_DEBUG
  bool			debug = 0;
#endif

  DBCf() = default;
  DBCf(const ZvCf *cf) {
    thread = cf->get<true>("thread");
    storeCf = cf->getCf("store");
    cf->getCf<true>("tables")->all([this](ZvCfNode *node) {
      if (auto tableCf = node->getCf())
	tableCfs.addNode(new TableCfs::Node{node->key, ZuMv(tableCf)});
    });
    cf->getCf<true>("hosts")->all([this](ZvCfNode *node) {
      if (auto hostCf = node->getCf())
	hostCfs.addNode(new HostCfs::Node{node->key, ZuMv(hostCf)});
    });
    hostID = cf->get("hostID"); // may be supplied separately
    nAccepts = cf->getInt("nAccepts", 1, 1<<10, 8);
    heartbeatFreq = cf->getInt("heartbeatFreq", 1, 3600, 1);
    heartbeatTimeout = cf->getInt("heartbeatTimeout", 1, 14400, 4);
    reconnectFreq = cf->getInt("reconnectFreq", 1, 3600, 1);
    electionTimeout = cf->getInt("electionTimeout", 1, 3600, 8);
#if Zdb_DEBUG
    debug = cf->getBool("debug");
#endif
  }
  DBCf(DBCf &&) = default;
  DBCf &operator =(DBCf &&) = default;

  const TableCf *tableCf(ZuCSpan id) const {
    if (auto node = tableCfs.findPtr(id)) return &node->val();
    return nullptr;
  }
  TableCf *tableCf(ZuCSpan id) {
    auto node = tableCfs.findPtr(id);
    if (!node) tableCfs.addNode(node = new TableCfs::Node{id});
    return &node->val();
  }

  const HostCf *hostCf(ZuCSpan id) const {
    if (auto node = hostCfs.findPtr(id)) return &node->val();
    return nullptr;
  }
  HostCf *hostCf(ZuCSpan id) {
    auto node = hostCfs.findPtr(id);
    if (!node) hostCfs.addNode(node = new HostCfs::Node{id});
    return &node->val();
  }
};

// --- DB

struct DBTelemetry;

class ZdbAPI DB : public ZmPolymorph, public ZmEngine<DB> {
public:
  using Engine = ZmEngine<DB>;

  using Engine::start;
  using Engine::stop;

friend Engine;
friend Cxn_;
friend Host;
friend AnyTable;
friend AnyObject;

private:
  using Lock = ZmLock;
  using Guard = ZmGuard<Lock>;
  using ReadGuard = ZmReadGuard<Lock>;

public:
#if Zdb_DEBUG
  bool debug() const { return m_cf.debug; }
#endif

  DB() { }
  ~DB() { }

  DB(const DB &) = delete;
  DB &operator =(const DB &) = delete;

  // init() and final() throw ZeException on error
  void init(
    DBCf config,
    ZiMultiplex *mx,
    DBHandler handler,
    ZmRef<Store> store = {});
  void final();

  template <typename T>
  ZmRef<Table<T>> initTable(ZuCSpan id) {
    return initTable_(id,
      [](DB *db, TableCf *tableCf) mutable {
	return static_cast<AnyTable *>(new Table<T>{db, tableCf});
      });
  }

private:
  using InitTableFn = ZmFn<AnyTable *(DB *, TableCf *),
    ZmFnHeapID<"Zdb.InitTableFn">>;
  ZmRef<AnyTable> initTable_(
    ZuCSpan, InitTableFn fn);

public:
  template <typename ...Args>
  void run(Args &&...args) const {
    m_mx->run(ZuFwd<Args>(args)..., m_cf.sid);
  }
  template <typename ...Args>
  void invoke(Args &&...args) const {
    m_mx->invoke(ZuFwd<Args>(args)..., m_cf.sid);
  }
  bool invoked() const { return m_mx->invoked(m_cf.sid); }

  const DBCf &config() const { return m_cf; }
  ZiMultiplex *mx() const { return m_mx; }
  auto sid() const { return config().sid; }

  int state() const {
    return ZuLikely(m_self) ? m_self->state() : HostState::Instantiated;
  }
private:
  void state(int n) {
    if (ZuUnlikely(!m_self)) {
      ZiLOG(Fatal, "Zdb", ([n](auto &s) {
	s << "Zdb::state(" << HostState::name(n) <<
	  ") called out of order";
      }));
      return;
    }
    m_self->state(n);
  }
public:
  bool active() const { return state() == HostState::Active; }

  Host *self() const { return m_self; }
  template <typename L> void allHosts(L &&l) const {
    auto i = m_hosts->citer();
    while (auto node = i()) l(node);
  }

  // backing data store
  Store *store() const { return m_store; }

  // trigger storage failure - intentionally deactivate
  void fail();

  // find table
  ZmRef<AnyTable> table(ZuCSpan id) {
    ZmAssert(invoked());

    return m_tables.findVal(id);
  }

  using AllTableFn = ZmFn<void(bool), ZmFnHeapID<"Zdb.AllTableFn">>;
  using AllFn = ZmFn<void(AnyTable *, AllTableFn),
    ZmFnHeapID<"Zdb.AllFn">>;
  using AllDoneFn = ZmFn<void(DB *, bool), ZmFnHeapID<"Zdb.AllDoneFn">>;

  void all(AllFn fn, AllDoneFn doneFn = AllDoneFn{});

  friend DBTelemetry;

  Zfb::Offset<void> telemetry(Zfb::Builder &fbb, bool update) const;

private:
  void storeFailed(ZeException e) {
    ZiLogEvent(ZuMv(e));
    run([this]() { fail(); });
  }

  void allDone(bool ok);

  template <typename L> void all_(L &&l) const {
    auto i = m_tables.citer();
    while (auto table = i.val()) ZuFwd<L>(l)(table);
  }

  // debug printing
  template <typename S> void print(S &);
  friend ZuPrintFn ZuPrintType(DB *);

  // ZmEngine implementation
  void start_();
  void stop_();
  template <typename L>
  bool spawn(L &&l) {
    if (!m_mx || !m_mx->running()) return false;
    m_mx->run(ZuFwd<L>(l), m_cf.sid);
    return true;
  }
  void wake();

  void start_1();
  void start_2();
  void stop_0();
  void stop_1();
  void stop_2();

  // leader election and activation/deactivation
  void holdElection();		// elect new leader
  void deactivate(bool failed);	// become follower
  void reactivate(Host *host);	// re-assert leader

  void up_(Host *oldMaster);	// run up command
  void down_(bool failed);	// run down command

  // host connection management
  void listen();
  void listening(const ZiListenInfo &);
  void listenFailed(bool transient);
  void stopListening();

  bool disconnectAll();

  ZiConnection *accepted(const ZiCxnInfo &ci);
  void connected(ZmRef<Cxn> cxn);
  void disconnected(ZmRef<Cxn> cxn);
  void associate(Cxn *cxn, ZuCSpan hostID);
  void associate(Cxn *cxn, Host *host);

  // heartbeats and voting
  void hbRcvd(Host *host, const fbs::Heartbeat *hb);
  void vote(Host *host);

  void hbStart();
  void hbSend();		// send heartbeat and reschedule self
  void hbSend_();		// send heartbeat (once, broadcast)
  void hbSend_(Cxn *cxn);	// send heartbeat (once, directed)

  void dbStateRefresh();	// refresh m_self->dbState()

  Host *setMaster();		// returns old leader
  void setNext(Host *host);
  void setNext();

  // outbound replication
  void repStart();
  void repStop();
  void recEnd();

  bool replicate(ZmRef<IOBuf> buf);

  // inbound replication
  void replicated(Host *host, ZuCSpan tblID, Shard shard, UN un, SN sn);

  bool isStandalone() const { return m_standalone; }

  // SN
  SN allocSN() { return m_nextSN++; }
  void recoveredSN(SN sn) {
    if (ZuUnlikely(sn == nullSN())) return;
    m_nextSN.maximum(sn + 1);
  }

  // data store
  bool repStore() const { return m_repStore; }

  DBCf			m_cf;
  ZiMultiplex		*m_mx = nullptr;
  ZmRef<Store>		m_store;
  bool			m_repStore = false;	// replicated data store

  // mutable while stopped
  DBHandler		m_handler;
  ZmRef<Hosts>		m_hosts;
  HostIndex		m_hostIndex;

  // SN allocator - atomic
  ZmAtomic<SN>		m_nextSN = 0;

  // DB thread
  Tables		m_tables;
  CxnList		m_cxns;

  AllFn			m_allFn;		// all() iteration context
  AllDoneFn		m_allDoneFn;		// ''
  unsigned		m_allCount = 0;		// remaining count
  unsigned		m_allNotOK = 0;		// remaining not OK

  bool			m_appActive =false;
  Host			*m_self = nullptr;
  Host			*m_leader = nullptr;	// == m_self if Active
  Host			*m_prev = nullptr;	// previous-ranked host
  Host			*m_next = nullptr;	// next-ranked host
  unsigned		m_recovering = 0;	// recovering next-ranked host
  DBState		m_recover{4};		// recovery state
  DBState		m_recoverEnd{4};	// recovery end
  int			m_nPeers = 0;	// # up to date peers
					// # votes received (Electing)
					// # pending disconnects (Stopping)
  ZuTime		m_hbSendTime;

  bool			m_standalone = false;

  ZmScheduler::Timer	m_listenTimer;
  ZmScheduler::Timer	m_hbSendTimer;
  ZmScheduler::Timer	m_electTimer;

  // telemetry
  ZuID			m_selfID, m_leaderID, m_prevID, m_nextID;
};

template <typename S>
inline void DB::print(S &s)
{
  s <<
    "self=" << ZuPrintPtr{m_self} << '\n' <<
    " prev=" << ZuPrintPtr{m_prev} << '\n' <<
    " next=" << ZuPrintPtr{m_next} << '\n' <<
    " recovering=" << m_recovering <<
    " replicating=" << Host::replicating(m_next);

  auto i = m_hostIndex.citer();

  while (Host *host = i()) {
    ZdbDEBUG(this, ZeString{} <<
	" host=" << ZuPrintPtr{host} << '\n' <<
	" leader=" << ZuPrintPtr{m_leader});

    if (host->voted()) {
      if (host != m_self) ++m_nPeers;
      if (!m_leader) { m_leader = host; continue; }
      if (host->cmp(m_leader) > 0) m_leader = host;
    }
  }
}

template <typename T>
template <unsigned KeyID, typename L>
inline void Table<T>::count(GroupKey<KeyID> key, L &&l)
{
  using Context = Count;

  auto context = ZmMkRef(new Context{ZuFwd<L>(l)});

  // using Key = GroupKey<KeyID>;

  Zfb::IOBuilder fbb{allocBuf()};
  fbb.Finish(ZfbStruct::save(fbb, key).Union());
  // fbb.Finish(ZfbStruct::SaveFieldsFn<Key, ZuFacet::Core, ZuFields<Key>, ZtFieldFilter::Save>::save(fbb, key).Union());
  auto keyBuf = fbb.buf();

  auto countFn = CountFn{ZuMv(context),
    [](Context *context, CountResult result) {
      if (ZuUnlikely(result.is<Event>())) { // error
	ZiLogEvent(ZuMv(result).p<Event>());
	context->fn(typename Context::Result{});
	return;
      }
      context->fn(typename Context::Result{result.p<CountData>().count});
    }};

  storeTbl()->count(KeyID, ZuMv(keyBuf), ZuMv(countFn));
}

template <typename T>
template <
  unsigned KeyID,
  typename SelectKey,
  typename Tuple_,
  bool SelectRow,
  bool SelectNext,
  typename L>
inline void Table<T>::select_(
  SelectKey selectKey, bool inclusive, unsigned limit, L &&l)
{
  using Context = Select<Tuple_>;

  auto context = ZmMkRef(new Context{ZuFwd<L>(l)});

  Zfb::IOBuilder fbb{allocBuf()};
  fbb.Finish(ZfbStruct::save(fbb, selectKey).Union());
  // fbb.Finish(ZfbStruct::SaveFieldsFn<SelectKey, ZuFields<SelectKey>, ZtFieldFilter::Load>::save(fbb, selectKey).Union());
  auto keyBuf = fbb.buf();

  auto tupleFn = TupleFn{ZuMv(context),
    [](Context *context, TupleResult result) {
      if (ZuUnlikely(result.is<Event>())) { // error
	ZiLogEvent(ZuMv(result).p<Event>());
	context->fn(typename Context::Result{}, 0);
	return;
      }
      if (ZuUnlikely(!result.is<TupleData>())) { // end of results
	context->fn(typename Context::Result{}, 0);
	return;
      }
      auto tupleData = result.p<TupleData>();
      auto fbo = ZfbStruct::root<T>(tupleData.buf->data());
      auto tuple = ZfbStruct::ctor<Tuple_>(fbo);
      context->fn(typename Context::Result{ZuMv(tuple)}, tupleData.count);
    }};

  storeTbl()->select(
    SelectRow, SelectNext, inclusive,
    KeyID, ZuMv(keyBuf), limit, ZuMv(tupleFn));
}

template <typename T>
template <
  unsigned KeyID, bool UpdateLRU, bool Evict, typename L>
inline void Table<T>::find_(Shard shard, Key<KeyID> key, L &&l) {
  ZmAssert(invoked(shard));

  auto load = [
    this, shard
  ]<typename L_>(const Key<KeyID> &key, L_ &&l) mutable {
    auto [buf, found] = findBuf<KeyID>(shard, key);
    if (buf) {
      l(objLoad(buf, shard));
      return;
    }
    if (found) {
      l(nullptr);
      return;
    }
    retrieve<KeyID>(shard, key, ZuFwd<L_>(l));
  };
  if constexpr (Evict) {
    m_cache[shard].template find<KeyID, UpdateLRU>(
      ZuMv(key), ZuFwd<L>(l), ZuMv(load),
      [this](AnyObject *object) {
	if (object->pinned()) return false;
	evictUN(object->shard(), object->un());
	object->evict();
	return true;
      });
  } else
    m_cache[shard].template find<KeyID, UpdateLRU, false>(
	ZuMv(key), ZuFwd<L>(l), ZuMv(load));
}

template <typename T>
template <unsigned KeyID, typename L>
inline void Table<T>::retrieve(
  Shard shard, Key<KeyID> key, L &&l)
{
  using Key_ = Key<KeyID>;
  using Context = Find<T, Key_>;

  auto context = ZmMkRef(new Context{
    this, shard, ZuMv(key), ZuFwd<L>(l)});

  retrieve_<KeyID>(ZuMv(context));
}
template <typename T>
template <unsigned KeyID>
inline void Table<T>::retrieve_(
  ZmRef<Find<T, ZuStructKeyT<T, KeyID>>> context)
{
  using Key_ = Key<KeyID>;
  using Context = Find<T, Key_>;

  Zfb::IOBuilder fbb{allocBuf()};
  fbb.Finish(ZfbStruct::save(fbb, context->key));
  auto keyBuf = fbb.buf();

  storeTbl()->find(KeyID, ZuMv(keyBuf), RowFn{ZuMv(context),
    [](Context *context, RowResult result) {
      auto table = context->table;
      if (ZuUnlikely(result.is<Event>())) {
	ZiLogEvent(ZuMv(result).p<Event>());
	auto db = context->table->db();
	ZiLOG(Fatal, "Zdb", ([context = ZuMv(context)](auto &s) {
	  s << "find of " << context->table->id()
	    << '/' << context->key << " failed";
	}));
	db->run([db]() { db->fail(); }); // trigger failover
	return;
      }
      auto shard = context->shard;
      if (ZuLikely(result.is<RowData>())) {
	auto buf = ZuMv(ZuMv(result).p<RowData>().buf);
	table->run(shard, [
	  table,
	  context = ZmMkRef(context),
	  buf = ZuMv(buf)
	]() mutable {
	  auto shard = context->shard;
	  ZmRef<Object<T>> object =
	    table->objLoad(ZuMv(buf), shard);
	  if (object->shard() != shard) {
	    auto fn = ZuMv(context->fn);
	    // sharding inconsistency is fatal, the app is broken
	    ZiLOG(Fatal, "Zdb", ([
	      context = ZuMv(context), object = ZuMv(object)
	    ](auto &s) {
	      s << "find of " << context->table->id()
		<< '/' << context->key << " failed: object " << *object
		<< " shard != find context shard " << context->shard;
	    }));
	    fn(nullptr);
	  } else
	    context->fn(ZuMv(object));
	});
      } else
	table->run(shard, [fn = ZuMv(context->fn)]() mutable {
	  fn(nullptr);
	});
    }});
}

// --- printing

template <typename S>
inline void DBState::print(S &s) const {
  s << "{sn=" << ZuBoxed(sn) << " dbs=[";
  unsigned n = count_();
  if (ZuLikely(n)) {
    unsigned j = 0;
    auto i = citer();
    while (auto state = i()) {
      if (j++) s << ',';
      s << '{'
	<< state->template p<0>().template p<0>() << '.'
	<< ZuBoxed(state->template p<0>().template p<1>()) << ','
	<< ZuBoxed(state->template p<1>()) << '}';
    }
  }
  s << "]}";
}

struct Record_Print {
  const fbs::Record *record = nullptr;
  const AnyTable *table = nullptr;
  template <typename S> void print(S &s) const {
    auto id = Zfb::Load::str(record->table());
    auto data = Zfb::Load::bytes(record->data());
    s << "{db=" << id
      << " shard=" << ZuBoxed(record->shard())
      << " un=" << record->un()
      << " sn=" << ZuBoxed(ZfbTransform::UInt128::load(record->sn()))
      << " vn=" << record->vn() << "}";
    if (data) {
      s << " data=";
      if (table) {
	ZuVStream s_(s);
	table->objPrintFB(s_, data);
      } else {
	s << "{...}";
      }
    } else {
      s << " data=(null)}";
    }
  }
  friend ZuPrintFn ZuPrintType(Record_Print *);
};

struct HB_Print {
  const fbs::Heartbeat *hb = nullptr;
  template <typename S> void print(S &s) const {
    auto id = Zfb::Load::str(hb->host());
    s << "{host=" << id
      << " state=" << HostState::name(hb->state())
      << " dbState=" << DBState{hb->dbState()} << "}";
  }
  friend ZuPrintFn ZuPrintType(HB_Print *);
};

template <typename S>
inline void IOBuf_::Print::print(S &s) const {
  auto msg = Zdb_::msg(buf->ptr<Hdr>());
  if (!msg) { s << "corrupt{}"; return; }
  if (auto record = Zdb_::record(msg)) {
    s << "record=" << Record_Print{record, table};
    return;
  }
  if (auto hb = Zdb_::hb(msg)) {
    s << "heartbeat=" << HB_Print{hb};
    return;
  }
  s << "unknown{}";
}

template <typename S>
inline void AnyObject::print(S &s) const {
  s << "{table=" << m_table->id()
    << " state=" << ObjState::name(m_state)
    << " shard=" << ZuBoxed(m_shard)
    << " un=" << m_un
    << " sn=" << m_sn
    << " vn=" << m_vn;
  if (m_origUN != nullUN()) s << " origUN=" << m_origUN;
  s << " data=";
  {
    ZuVStream s_{s};
    m_table->objPrint(s_, ptr_());
  }
  s << '}';
}

} // Zdb_

// external API

using ZdbAnyObject = Zdb_::AnyObject;
template <typename T> using ZdbObject = Zdb_::Object<T>;
template <typename T> using ZdbObjRef = ZmRef<ZdbObject<T>>;
namespace ZdbObjState = Zdb_::ObjState;

using ZdbAnyTable = Zdb_::AnyTable;
template <typename T> using ZdbTable = Zdb_::Table<T>;
using ZdbTableCf = Zdb_::TableCf;
template <typename T> using ZdbTblRef = ZmRef<ZdbTable<T>>;

using Zdb = Zdb_::DB;
using ZdbHandler = Zdb_::DBHandler;
using ZdbCf = Zdb_::DBCf;

using ZdbUpFn = Zdb_::UpFn;
using ZdbDownFn = Zdb_::DownFn;
using ZdbHandler = Zdb_::DBHandler;

using ZdbHost = Zdb_::Host;

using ZdbBuf = Zdb_::IOBuf;

#endif /* Zdb_HH */
