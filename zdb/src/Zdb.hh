//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z database

// select() is an un-cached backing data store query that
// returns 0..N immutable ZuTuples for read-only purposes
// - cache consistency is assured by enqueuing the select on the
//   back-end write queue, ensuring results reflect any pending updates
//   outstanding at the time of the call; the results may become outdated
//   when eventually processed if further updates are performed while
//   the select itself is outstanding (this is an intentional design
//   limitation)

// insert() inserts new rows
// find() returns 0..1 mutable ZdbRow<T> values for read-modify-write
// update() updates existing rows
// del() deletes existing rows

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

#include <limits.h>

#include <zlib/ZuTraits.hh>
#include <zlib/ZuArray.hh>
#include <zlib/ZuTL.hh>
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
#include <zlib/ZmTime.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmEngine.hh>
#include <zlib/ZmPolyCache.hh>
#include <zlib/ZmPLock.hh>
#include <zlib/ZmLHash.hh>
#include <zlib/ZmScratch.hh>

#include <zlib/ZtString.hh>
#include <zlib/ZtEnum.hh>

#include <zlib/ZePlatform.hh>
#include <zlib/ZiLog.hh>
#include <zlib/ZiAssert.hh>

#include <zlib/ZiFile.hh>
#include <zlib/ZiMultiplex.hh>
#include <zlib/ZiRx.hh>
#include <zlib/ZiTx.hh>
#include <zlib/ZiFB.hh>
#include <zlib/ZiFBReflect.hh>

#include <zlib/ZfCf.hh>

#include <zlib/ZdbTypes.hh>
#include <zlib/ZtcDB.hh>
#include <zlib/ZdbBuf.hh>
#include <zlib/ZdbMsg.hh>
#include <zlib/ZdbStore.hh>
#include <zlib/ZdbSaga.hh>

#define ZdbTableDerive(Name, T_) \
  struct Name; \
  ZuPP_Strip(T_) *ZdbTableModel(Name *); \
  Name *ZdbTableType(ZuPP_Strip(T_) *); \
  ZuDerive(Name, (Zdb_::Table<ZuPP_Strip(T_), Name>))

#if defined(ZDEBUG) && !defined(Zdb_DEBUG)
#define Zdb_DEBUG 1
#endif

#if Zdb_DEBUG
#define ZdbDEBUG(db, e) \
  do { if ((db)->debug()) ZiLOG(Debug, "Zdb", (e)); } while (0)
#else
#define ZdbDEBUG(db, e) (void())
#endif

// Zdb-specific row traits and hooks
//
// ZdbHeapID(T *)	- heap ID for rows
// ZdbBufHeapID(T *)	- heap ID for buffers
// ZdbBufSize(T *)	- buffer size
// ZdbEvictHook(T *)	- hook into cache eviction

// type-specific cache ID, e.g.
// struct Foo {
//   ...
//   friend ZuStringT<"Foo"> ZdbHeapID(Foo *);
// };
ZuStringT<"Zdb.Row"> ZdbHeapID(...); // default
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

// Erased row writers must support any schema's forced alignment. Actual
// root padding still follows the fields emitted, not this storage bound.
using Builder = ZiFB::IOBuilder<ZiFB::Wire::MaxAlign>;

namespace HostState {
  using namespace Ztc::DBHostState;
}
namespace CacheMode {
  using namespace Ztc::DBCacheMode;
}

// --- pre-declarations

class DB;				// database
class Host;				// cluster host
class AnyTable;				// untyped table
template <typename Impl_> class Table_;	// typed table implementation
template <typename T_, typename Impl_> class Table; // typed table
template <typename T> class Row_;
struct Count;
struct Find;
struct Record_Print;

template <typename T> Table<T> *ZdbTableType(T *);
template <typename T>
using TableT = ZuDecay<decltype(*ZdbTableType(ZuDeclVal<T *>()))>;
template <typename T, typename Impl> T *ZdbTableModel(Table<T, Impl> *);
template <typename Impl>
using TableModelT = ZuDecay<decltype(*ZdbTableModel(ZuDeclVal<Impl *>()))>;

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

  void hbRcvd(Msg::HeartbeatView);
  void hbTimeout();
  void hbSend();

  void repRecordRcvd(ZmRef<IOBuf>);
  void repCommitRcvd(ZmRef<IOBuf>);

private:
  DB			*m_db;
  Host			*m_host;	// nullptr if not yet associated

  ZmScheduler::Timer	m_hbTimer;
};
ZmListDerive(CxnList, Cxn_,
  ZmListNode<Cxn_,
    ZmListHeapID<"Zdb.Cxn">>);
using Cxn = CxnList::Node;

// --- DB state - SN and key/value linear hash from {table ID, shard} -> UN

using DBStateT = ZuTuple<IDString, Shard>;
ZmLHashKVDerive(DBState_, DBStateT, UN, ZmLHashLocal<>);
struct DBState : public DBState_ {
  SN		sn = 0;

  DBState() = delete;

  DBState(unsigned size) : DBState_{ZmHashParams{size}} { }

  DBState(ZiFB::View<Msg::DBState> dbState) :
    DBState_{ZmHashParams{dbState.get<Msg::ZfField(DBState, tableStates)>().length()}},
    sn{dbState.get<Msg::ZfField(DBState, sn)>()}
  {
    for (auto state: dbState.get<Msg::ZfField(DBState, tableStates)>())
      add(state.get<Msg::ZfField(TableState, table)>(),
	state.get<Msg::ZfField(TableState, shard)>(),
	state.get<Msg::ZfField(TableState, un)>());
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
  const T *add(ZuCSpan id, Shard shard, UN un) {
    return DBState_::add(ZuFwdTuple(id, shard), un);
  }
  bool update(ZuCSpan id, Shard shard, UN un_) {
    auto state = find(id, shard);
    if (!state) {
      add(id, shard, un_);
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
	update(rstate->template p<0>().template p<0>(),
	  rstate->template p<0>().template p<1>(),
	  rstate->template p<1>());
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

// The native hash uses the canonical message schema without materializing
// an array of TableState objects. Loading merges into an existing peer state.
struct DBStateFmt : public ZiFB::As<Msg::DBState> {
  template <typename O, typename Facet = ZuFacet::FB>
  class Handler : public ZiFB::As<Msg::DBState>::template Handler<Msg::DBState, Facet> {
    using Base = ZiFB::As<Msg::DBState>::template Handler<Msg::DBState, Facet>;
  public:
    explicit Handler(ZiFB::View<O, Facet> view) :
      Base{{ZiFB::View<Msg::DBState, Facet>{view.data()}}} { }

    void load(DBState &o) const {
      auto dbState = this->view;
      o.sn = dbState.template get<Msg::ZfField(DBState, sn)>();
      for (auto state: dbState.template get<Msg::ZfField(DBState, tableStates)>())
	o.update(state.template get<Msg::ZfField(TableState, table)>(),
	  state.template get<Msg::ZfField(TableState, shard)>(),
	  state.template get<Msg::ZfField(TableState, un)>());
    }
    void update(DBState &o) const { load(o); }

    template <template <typename> class Filter = ZfFieldFilter::Save, typename B>
    static ZiFB::Offset save(B &fbb, const DBState &o) {
      ZiFB::Offset states = 0;
      if constexpr (Filter<Msg::ZfField(DBState, tableStates)>{}) {
	unsigned n = o.count_();
	auto offsets = ZtScratch(ZiFB::Offsets, n);
	{
	  auto i = o.citer();
	  while (auto state = i())
	    offsets.push(ZiFB::save(fbb, Msg::TableState{
	      state->template p<0>().template p<0>(), state->template p<1>(),
	      state->template p<0>().template p<1>()}));
	}
	states = fbb.offsets(offsets);
      }
      auto table = fbb.startTable();
      if constexpr (Filter<Msg::ZfField(DBState, sn)>{})
	Base::template field<Msg::ZfField(DBState, sn)>(fbb, table, o.sn);
      if constexpr (Filter<Msg::ZfField(DBState, tableStates)>{})
	Base::template fieldOffset<Msg::ZfField(DBState, tableStates)>(fbb, table, states);
      return fbb.endTable(table);
    }

  private:
    // The inherited factories construct borrowed Msg::DBState, not a hash.
    using Base::ctor;
    using Base::alloc;
    using Base::new_;
  };
};
DBStateFmt ZiFB_Fmt(DBState *);

// Native send data retains the canonical heartbeat field IDs and wire name.
struct Heartbeat {
  ZuCSpan host;
  uint32_t state, nShards;
  const DBState *dbState;
};
ZfStruct(ZdbAPI, (Heartbeat, FB),
  (host,,			String),
  (state,	(Deflt<0>),	UInt32),
  (nShards,	(Deflt<0>),	UInt32),
  (dbState,,			UDT));
ZiFB::As<Msg::Heartbeat> ZiFB_Fmt(Heartbeat *);

// --- generic row

// possible row state paths:
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

class ZdbAPI AnyRow : public ZmPolymorph {
  AnyRow() = delete;
  AnyRow(const AnyRow &) = delete;
  AnyRow &operator =(const AnyRow &) = delete;
  AnyRow(AnyRow &&) = delete;
  AnyRow &operator =(AnyRow &&) = delete;

  friend AnyTable;
  template <typename> friend class Table_;

public:
  AnyRow(AnyTable *table, Shard shard) :
    m_table{table}, m_shard{shard} { }

  AnyTable *table() const { return m_table; }
  Shard shard() const { return m_shard; }
  UN un() const { return m_un; }
  SN sn() const { return m_sn; }
  VN vn() const { return m_vn; }
  int state() const { return m_state; }	// RowState
  UN origUN() const { return m_origUN; }
  bool evicted() const { return m_pinCount < 0; }
  bool pinned() const { return m_pinCount > 0; }

  ZmRef<IOBuf> replicate(int type);

  virtual void *ptr_() { return nullptr; }
  const void *ptr_() const { return const_cast<AnyRow *>(this)->ptr_(); }

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
  friend ZuPrintFn ZuPrintType(AnyRow *);

private:
  void init(Shard shard, UN un, SN sn, VN vn) {
    m_shard = shard;
    m_un = un;
    m_sn = sn;
    m_vn = vn;
    m_state = RowState::Committed;
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
  int8_t	m_state = RowState::Undefined;
};

inline UN AnyRow_UNAxor(const ZmRef<AnyRow> &row) {
  return row->un();
}

// Temporarily there may be more than one UN referencing a cached row; rows
// participate in several caches, so this registry retains shared ownership.
ZmHashKVDerive(CacheUN, UN, ZmRef<AnyRow>,
  (ZmHashLock<ZmPLock,
	ZmHashHeapID<"Zdb.UpdCache">>));

// --- typed row

// Zdf data-frames are comprised of series fields that do not form part of
// a primary or secondary key for the row - Zdb skips Zdf fields
// and does not persist them
template <typename Field>
using FieldFilter =
  ZuBool<!ZuTypeIn<ZuFieldProp::Series, typename Field::Props>{}>;

template <typename O, typename Facet = ZuFacet::Core>
using Fields = ZuTypeGrep<FieldFilter, ZuActiveFields<O, Facet>>;

template <typename T_>
class Row_ : public AnyRow {
public:
  using T = T_;
  using Table = TableT<T>;

  Row_(Table *table_, Shard shard) : AnyRow{table_, shard} { }

  Row_() = delete;
  Row_(const Row_ &) = delete;
  Row_ &operator =(const Row_ &) = delete;
  Row_(Row_ &&) = delete;
  Row_ &operator =(Row_ &&) = delete;

  Table *table() const { return static_cast<Table *>(AnyRow::table()); }

  void *ptr_() { return &m_data[0]; }
  const void *ptr_() const { return &m_data[0]; }

  T *ptr() { return reinterpret_cast<T *>(&m_data[0]); }
  const T *ptr() const { return reinterpret_cast<const T *>(&m_data[0]); }

  ~Row_() { ptr()->~T(); }

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
    using O = Row_;
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
  friend Fields<Facet> ZuFields_(Row_ *, Facet *);

  friend Row_ ZuStructured_(Row_ *);

private:
  alignas(T) uint8_t	m_data[sizeof(T)];
};

// typed row cache
ZmPolyCacheDeriveT((T), Cache, (Row_<T>),
  (ZmPolyCacheHeapID_<Zdb_HeapID<T>>));

// typed row
template <typename T>
struct Row : public Cache<T>::Node {
  ZuDerive_(Row, Cache<T>::Node)
  using Row_<T>::data;	// disambiguate from Node::data

  void evict() {
    using Hook = Zdb_EvictHook<T>;
    if constexpr (!ZuIsSame<void, Hook>{}) Hook::evict(this);
    AnyRow::evict();
  }
};

// --- table configuration

struct TableCf {
  IDString		id;
  int			cacheMode = CacheMode::Normal;

  TableCf() = default;
  TableCf(ZuCSpan id_) : id{id_} { }
  TableCf(ZuCSpan id_, int cacheMode_) :
    id{id_}, cacheMode{cacheMode_} { }
  ZdbAPI TableCf(ZuCSpan id_, const ZfCf::AnyNode *cf);

  static const auto &IDAxor(const TableCf &cf) { return cf.id; }
};

ZfStruct(ZdbAPI, (TableCf, Cf),
  (cacheMode, (Mutable, Enum<CacheMode::Map>),		Int32));


// --- table configuration

ZmRBTreeDerive(TableCfs, TableCf,
  ZmRBTreeKey<TableCf::IDAxor,
    ZmRBTreeUnique<true,
      ZmRBTreeHeapID<"Zdb.TableCf">>>);

// --- generic table

class ZdbAPI AnyTable : public ZmPolymorph, public Ztc::DBTable {
friend DB;
friend Cxn_;
friend AnyRow;
friend Record_Print;	// uses rowPrintFB

protected:
  AnyTable(DB *db, TableCf *cf, bool internal, IOBufAllocFn);

public:
  ~AnyTable() noexcept;

private:
  template <typename L> void open(L &&l);	// l(OpenResult)
  bool opened(OpenResult);
  template <typename L> void close(L &&l);	// l()
protected:
  void count(unsigned keyID, ZmRef<IOBuf>, ZmRef<Count>);
  void retrieve(unsigned keyID, ZmRef<IOBuf>, ZmRef<Find>);

public:
  DB *db() const { return m_db; }
  ZiMultiplex *mx() const { return m_mx; }
  const TableCf &config() const { return *m_cf; }
  IOBufAllocFn bufAllocFn() const { return m_bufAllocFn; }

  static const auto &IDAxor(AnyTable *table) { return table->config().id; }

  const auto &id() const { return config().id; }
  Ztc::DBTable::TelKey telKey() const override;
  void telemetry(Ztc::DBTableTelemetry &) const override;

  template <typename ...Args>
  void run(Shard, Args &&...) const;
  template <typename ...Args>
  void invoke(Shard, Args &&...) const;
  bool invoked(Shard) const;

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
  unsigned nShards_() const;

public:
  // next UN that will be allocated
  UN nextUN(Shard shard) const { return m_nextUN[shard]; }

  // enable/disable writing to cache (temporarily)
  void writeCache(bool enabled) { m_writeCache = enabled; }

  // all transactions begin with a insert(), update() or del(),
  // and complete with row->commit() or row->abort()

protected:
  // --- implemented by Table<T>

  // construct a typed row from a trusted replication buffer
  virtual ZmRef<AnyRow> rowLoad(IOBuf *, Shard) = 0;

  // rowSave(fbb, ptr) - save row into flatbuffer, return offset
  virtual ZiFB::Offset rowSave(Builder &, const void *) const = 0;
  virtual ZiFB::Offset rowSaveUpd(Builder &, const void *) const = 0;
  virtual ZiFB::Offset rowSaveDel(Builder &, const void *) const = 0;
  virtual unsigned rowAlign() const = 0;
  // Validate nested row data at network ingress, before replication state changes.
  virtual bool rowVerify(Msg::RecordView) const = 0;
  // rowRecover(record) - process a verified recovered FB record
  virtual void rowRecover(Msg::RecordView) = 0;

  // rowFields() - run-time field array
  virtual ZfVFieldArray rowFields() const = 0;
  // rowKeyFields() - run-time key field arrays
  virtual ZfVKeyFieldArray rowKeyFields() const = 0;
  // rowSchema() - flatbuffer reflection schema
  virtual Schema rowSchema() const = 0;

  // rowPrint(stream, ptr) - print row
  virtual void rowPrint(ZuVStream &, const void *) const = 0;
  // rowPrintFB(stream, data) - print buffer
  virtual void rowPrintFB(ZuVStream &, ZuBSpan) const = 0;

  // buffer cache
  virtual void cacheBuf_(Shard shard, ZmRef<IOBuf>) = 0;
  virtual ZmRef<IOBuf> evictBuf_(Shard shard, IOBuf *) = 0;

  // cache statistics
  virtual void cacheStats(Shard shard, ZmCacheStats &stats) const = 0;

protected:
  bool writeCache() const { return m_writeCache; }

  static OpResult::T cmpUN_(UN next, UN un) {
    if (ZuUnlikely(un == nullUN())) return OpResult::Invalid;
    switch (ZuCmp<UN>::cmp(next, un)) {
      case -1: return OpResult::NotReady;
      case  0: return OpResult::Executed;
      default: return OpResult::Skipped;
    }
  }

  auto findUN(Shard shard, UN un) const {
    return m_cacheUN[shard]->findVal(un);
  }
  void cacheUN(Shard shard, UN un, AnyRow *row) {
    m_cacheUN[shard]->add(un, row);
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

  // recovery - shard thread, verified record
  void recover(Shard shard, Msg::RecordView record);

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
  const bool		m_internal;

  // Table threads SWMR
  ZtArray<ZmAtomic<UN>>	m_nextUN;		// UN allocator

  // open/closed state, record count
  ZmAtomic<unsigned>	m_open = 0;		// Table threads SWMR
  ZmAtomic<uint64_t>	m_count = 0;		// ''

  // backing data store table
  StoreTbl		*m_storeTbl = nullptr;	// Table threads

  // row cache indexed by UN (sharded)
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
  using FB = ZiFB::View<T>;
  FB fbo() const { return buf->fbo<T>(); }
  // trusted buffer (written locally), e.g. in buffer cache
  FB fbo_() const { return buf->fbo_<T>(); }

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
      return getView(o.fbo_());
    }
    static decltype(Orig::get(ZuDeclVal<const FB &>())) getView(const FB &view) {
      return Orig::get(view);
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
  friend ZfStructPrint ZuPrintType(RepBuf_ *);
};

} // Zdb_

// Extract all key fields from one borrowed row view. Field-by-field adaptation
// otherwise repeats envelope and nested-root navigation for each key member.
template <typename T, template <typename> class Filter, typename ...Fields>
struct ZuStructTuple_Render<Zdb_::RepBuf_<T>, Filter, ZuTypeList<Fields...>> {
  using O = Zdb_::RepBuf_<T>;

  template <template <typename> class Ref>
  static auto get_(const O &o) {
    auto view = o.fbo_();
    return ZuStructTupleT<O, Ref, ZuAsIs, Filter>{
      Fields::getView(view)...
    };
  }
  static auto get(const O &o) { return get_<ZuMkCRef>(o); }
  static auto get(O &&o) { return get_<ZuMkRRef>(o); }
};

namespace Zdb_ {

// replication buffer cache
ZmPolyHashDeriveT((T), BufCache, (RepBuf_<T>),
  (ZmPolyHashHeapID_<ZdbBuf_HeapID<T>>));

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
template <typename Heap = ZuVoid>
struct Count_ : public Heap, public ZmPolymorph, public Count__ {
  ZuDerive_(Count_, Count__)
};
ZuDerive(Count_Heap, (ZmHeap<"Zdb.Count", Count_<>>));
ZuDerive(Count, (Count_<Count_Heap>));

// backing data store select() context
template <typename Tuple> struct Select__ {
  using Result = ZuUnion<void, Tuple>;
  using Fn = ZmFn<void(Result, unsigned), ZmFnHeapID<"Zdb.Select.Fn">>;

  AnyTable	*table;
  Fn	fn;
};
template <typename Tuple, typename Heap = ZuVoid>
struct Select_ : public Heap, public ZmPolymorph, public Select__<Tuple> {
  ZuDerive_(Select_, Select__<Tuple>)
};
template <typename Tuple>
ZuDerive(Select_Heap, (ZmHeap<"Zdb.Select", Select_<Tuple>>));
template <typename Tuple>
ZuDerive(Select, (Select_<Tuple, Select_Heap<Tuple>>));

// backing data store find() context
struct Find : public ZmPolymorph {
  Find(AnyTable *table_, Shard shard_) : table{table_}, shard{shard_} { }

  virtual void found(ZmRef<AnyRow>) = 0;
  virtual void printKey(ZuVStream &) const = 0;

  AnyTable	*table;
  Shard		shard;
};
template <typename T, typename Key> struct Find__ {
  using Fn = ZmFn<void(ZmRef<Row<T>>), ZmFnHeapID<"Zdb.Find.Fn">>;

  Key		key;
  Fn		fn;
};
template <typename T, typename Key, typename Heap = ZuVoid>
struct Find_ : public Heap, public Find, public Find__<T, Key> {
  template <typename L>
  Find_(AnyTable *table, Shard shard, Key key, L &&l) :
    Find{table, shard}, Find__<T, Key>{ZuMv(key), ZuFwd<L>(l)} { }

  void found(ZmRef<AnyRow> row) override {
    this->fn(ZmRef<Row<T>>{ZuMv(row)});
  }
  void printKey(ZuVStream &s) const override { s << this->key; }
};
template <typename T, typename Key>
ZuDerive(Find_Heap, (ZmHeap<"Zdb.Find", Find_<T, Key>>));
template <typename T, typename Key>
ZuDerive(FindCtx, (Find_<T, Key, Find_Heap<T, Key>>));

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

  // Stores compare a group prefix followed by its ordered members. Key tuples
  // retain declaration order; field metadata preserves their accessors while
  // presenting the store's prefix order, including inverse secondary indexes.
  static ZfVFieldArray storeFields() {
    return ZfVFields_<ZuTypeConcat<GroupFields, MemberFields>>();
  }
};

// --- typed table

template <typename Impl_>
class Table_ : public AnyTable {
friend DB;
friend Cxn_;
friend AnySaga__;
template <typename> friend class Row_;

public:
  using Impl = Impl_;
  using T = TableModelT<Impl>;

  ZuInline auto impl() { return static_cast<Impl *>(this); }
  ZuInline auto impl() const { return static_cast<const Impl *>(this); }

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

  Table_(DB *db, TableCf *cf, bool internal) :
    AnyTable{db, cf, internal, Table_::allocBuf},
    m_schema(ZiFB::SchemaOut::Schema<T>::Size)
  {
    ZiFB::saveSchema<T>(m_schema);
    unsigned n = nShards_();
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
  // rowLoad(buf, shard)
  // - construct row from flatbuffer (trusted source)
  ZmRef<AnyRow> rowLoad(IOBuf *buf, Shard shard) override {
    auto record = record_(msg_(buf->hdr()));
    if (record.get<Msg::ZfField(Record, vn)>() < 0) return {}; // deleted
    auto data = record.get<Msg::ZfField(Record, data)>();
    ZiAssert(data, "Zdb",
      (id = this->id()), "missing record data in table " << id, return {});
    auto fbo = ZiFB::view<T>(data);
    ZiAssert(fbo, "Zdb",
      (id = this->id()), "bad record data in table " << id, return {});
    ZmRef<Row<T>> row = new Row<T>(impl(), shard);
    ZiFB::handler(fbo).new_(row->ptr());
    row->init(
      record.get<Msg::ZfField(Record, shard)>(), record.get<Msg::ZfField(Record, un)>(),
      record.get<Msg::ZfField(Record, sn)>(), record.get<Msg::ZfField(Record, vn)>());
    return row;
  }
  // rowSave(fbb, ptr) - save row into flatbuffer, return offset
  ZiFB::Offset rowSave(Builder &fbb, const void *ptr) const override {
    return ZiFB::save(fbb, *static_cast<const T *>(ptr));
  }
  unsigned rowAlign() const override {
    constexpr auto row = ZiFB::BufferAlign<T>::N;
    constexpr auto msg = ZiFB::BufferAlign<Msg::Root>::N;
    return row > msg ? row : msg;
  }
  ZiFB::Offset rowSaveUpd(Builder &fbb, const void *ptr) const override {
    return ZiFB::save<ZuFacet::FB, ZfFieldFilter::Upd>(
      fbb, *static_cast<const T *>(ptr));
  }
  ZiFB::Offset rowSaveDel(Builder &fbb, const void *ptr) const override {
    return ZiFB::save<ZuFacet::FB, ZfFieldFilter::Del>(
      fbb, *static_cast<const T *>(ptr));
  }
  bool rowVerify(Msg::RecordView record) const override {
    return bool(data<T>(record));
  }
  // rowRecover(record) - process a verified recovered record
  void rowRecover(Msg::RecordView record) override {
    auto fbo = data_<T>(record);
    auto shard = record.get<Msg::ZfField(Record, shard)>();
    // Every buffer-cache index retains the same node. Invalidating all
    // versions by immutable key 0 also invalidates their secondary entries,
    // without interpreting fields omitted by update/delete projection.
    {
      auto i = m_bufCache[shard].template iter<0>(ZuStructKey<0>(fbo));
      while (auto repBuf = i()) repBuf->stale = true;
    }
    // maintain cache consistency
    if (record.get<Msg::ZfField(Record, vn)>() >= 0) {
      // Primary key is immutable. No load or eviction here.
      if (ZmRef<Row<T>> row = m_cache[shard].find(ZuStructKey<0>(fbo))) {
	if constexpr (KeyIDs::N > 1)
	  m_cache[shard].template update<ZuTypeTail<1, KeyIDs>>(ZuMv(row),
	    [fbo](const ZmRef<Row<T>> &row) {
	      ZiFB::handler(fbo).update(row->data());
	    });
	else
	  ZiFB::handler(fbo).update(row->data());
      }
    } else {
      m_cache[shard].template del<0>(ZuStructKey<0>(fbo));
    }
  }

  // rowFields() - run-time field array
  ZfVFieldArray rowFields() const override { return ZfVFields<T>(); }
  // rowKeyFields() - run-time key field arrays
  struct KeyFieldStorage {
    ZuArray<ZfVFieldArray, KeyIDs::N> data;
    KeyFieldStorage() : data(KeyIDs::N, false) {
      init(ZuMkSeq<KeyIDs::N>{});
    }
    template <unsigned ...I>
    void init(ZuSeq<I...>) {
      ((data[I] = SplitKey<T, I>::storeFields()), ...);
    }
  };
  ZfVKeyFieldArray rowKeyFields() const override {
    static const KeyFieldStorage fields;
    return fields.data;
  }
  // rowSchema() - flatbuffer reflection schema
  Schema rowSchema() const override {
    return Schema::from<T>(m_schema);
  }

  // rowPrint(stream, ptr) - print row
  void rowPrint(ZuVStream &s, const void *ptr) const override {
    ZfStructPrint::print(s, *static_cast<const T *>(ptr));
  }
  // rowPrintFB(stream, data) - print flatbuffer
  void rowPrintFB(ZuVStream &s, ZuBSpan data) const override {
    if (!ZiFB::verify<T>(data)) return;
    s << ZiFB::view<T>(data);
  }

  // find buffer in buffer cache
  template <unsigned KeyID>
  ZmRef<IOBuf>
  findBuf(Shard shard, const Key<KeyID> &key) const {
    auto i = m_bufCache[shard].template iter<KeyID>(key);
    while (auto repBuf = i()) {
      if (repBuf->stale) continue;
      // Update records are projections, not complete rows for cold loads.
      auto vn = record_(msg_(repBuf->buf->hdr())).template get<Msg::ZfField(Record, vn)>();
      if (vn <= 0) return repBuf->buf;
    }
    return {};
  }

  // find, falling through row cache, buffer cache, backing data store
  template <
    unsigned KeyID, bool UpdateLRU, bool Evict, typename L>
  void find_(Shard shard, Key<KeyID>, L &&l);
  // find from backing data store (retried on failure)
  template <unsigned KeyID, typename L>
  void retrieve(Shard shard, Key<KeyID>, L &&);

  template <unsigned KeyID>
  struct FindLoad {
    Table_ *table;
    Shard shard;

    template <typename L>
    void operator ()(const Key<KeyID> &key, L &&l) {
      if (auto buf = table->template findBuf<KeyID>(shard, key)) {
	l(table->rowLoad(buf, shard));
	return;
      }
      table->template retrieve<KeyID>(shard, key, ZuFwd<L>(l));
    }
  };

  struct FindEvict {
    Table_ *table;

    bool operator ()(AnyRow *row) const {
      if (row->pinned()) return false;
      table->evictUN(row->shard(), row->un());
      row->evict();
      return true;
    }
  };

  // buffer cache
  void cacheBuf_(Shard shard, ZmRef<IOBuf> buf) override {
    bool projected = record_(msg_(buf->hdr())).get<Msg::ZfField(Record, vn)>() != 0;
    ZmRef<RepBuf<T>> repBuf = new RepBuf<T>{ZuMv(buf)};
    if (projected)
      m_bufCache[shard].template hash<0>()->addNode(ZuMv(repBuf));
    else
      m_bufCache[shard].add(ZuMv(repBuf));
  }
  ZmRef<IOBuf> evictBuf_(Shard shard, IOBuf *buf) override {
    auto node = static_cast<RepBuf<T> *>(static_cast<RepBuf_<T> *>(buf->rep));
    bool projected = record_(msg_(buf->hdr())).get<Msg::ZfField(Record, vn)>() != 0;
    ZmRef<RepBuf<T>> repBuf = projected ?
      m_bufCache[shard].template hash<0>()->delNode(node) :
      m_bufCache[shard].delNode(node);
    if (repBuf)
      return ZuMv(repBuf->buf);
    return nullptr;
  }

  // cache statistics
  void cacheStats(Shard shard, ZmCacheStats &stats) const override {
    m_cache[shard].stats(stats);
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

  // find - lambda(ZdbRowRef<T>)
  // - non-unique secondary keys return one unspecified match, as do findUpd/Del
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

    ZmRef<Row<T>> row = m_cache[shard].template del<KeyID>(key);
    if (row) {
      if (row->pinned()) row->unpin();
      evictUN(shard, row->un());
      row->evict();
    }
  }
  void evict(Row<T> *row) {
    auto shard = row->shard();

    ZmAssert(invoked(shard));

    m_cache[shard].delNode(row);
    if (row->pinned()) row->unpin();
    evictUN(shard, row->un());
    row->evict();
  }

public:
  // idempotent insert: commit locally; backend ignores primary-key collisions
  // - caller ensures supplied data is consistent with an existing durable row
  // - insert lambda(ZdbRow<T> *)
  template <typename L>
  void insert(ZmRef<Row<T>> row, L &&l) {
    auto shard = row->shard();

    ZmAssert(invoked(shard));

    if (!insert_(nextUN(shard), row.ptr(), l)) l(nullptr);
  }
  // create new row at an intended UN
  // - lambda(OpResult::T, ZdbRow<T> *, UN)
  template <typename L>
  void insert(UN un, ZmRef<Row<T>> row, L &&l) {
    auto shard = row->shard();

    ZiAssert(invoked(shard), "Zdb", (shard),
      "insert called outside shard " << ZuBoxed(shard), return);

    auto next = nextUN(shard);
    auto result = cmpUN_(next, un);
    switch (result) {
      case OpResult::Executed: {
	auto l_ = [&l, next](Row<T> *row_) {
	  l(OpResult::Executed, row_, next);
	};
	if (!insert_(un, row.ptr(), l_))
	  l(OpResult::Invalid, nullptr, next);
	return;
      }
      default:
	l(result, nullptr, next);
	return;
    }
  }

  // update lambda(ZdbRow<T> *)

  // update row
  template <typename KeyIDs_ = ZuSeq<>, typename L>
  void update(ZmRef<Row<T>> row, L &&l) {
    auto shard = row->shard();

    ZmAssert(invoked(shard));

    if (!update_<KeyIDs_>(nextUN(shard), row.ptr(), l)) l(nullptr);
  }
  // update row at an intended UN
  // - lambda(OpResult::T, ZdbRow<T> *, UN)
  template <typename KeyIDs_ = ZuSeq<>, typename L>
  void update(ZmRef<Row<T>> row, UN un, L &&l) {
    auto shard = row->shard();

    ZiAssert(invoked(shard), "Zdb", (shard),
      "update called outside shard " << ZuBoxed(shard), return);

    auto next = nextUN(shard);
    auto result = cmpUN_(next, un);
    switch (result) {
      case OpResult::Executed: {
	auto l_ = [&l, next](Row<T> *row_) {
	  l(OpResult::Executed, row_, next);
	};
	if (!update_<KeyIDs_>(un, row.ptr(), l_))
	  l(OpResult::Invalid, nullptr, next);
	return;
      }
      default:
	l(result, nullptr, next);
	return;
    }
  }

  // find and update record (with key, without row)
  template <
    unsigned KeyID, typename KeyIDs_ = ZuSeq<>, typename L>
  inline void findUpd(Shard shard, Key<KeyID> key, L &&l) {
    findUpd_<KeyID>(shard, ZuMv(key),
	[this, l = ZuFwd<L>(l)](ZmRef<Row<T>> row) mutable {
	if (ZuUnlikely(!row)) { l(row.ptr()); return; }
	update<KeyIDs_>(ZuMv(row), ZuMv(l));
      });
  }
  // find and update record at an intended UN (with key, without row)
  // - lambda(OpResult::T, ZdbRow<T> *, UN)
  template <
    unsigned KeyID, typename KeyIDs_ = ZuSeq<>, typename L>
  inline void findUpd(Shard shard, Key<KeyID> key, UN un, L &&l) {
    ZiAssert(invoked(shard), "Zdb", (shard),
      "findUpd called outside shard " << ZuBoxed(shard), return);

    auto next = nextUN(shard);
    auto result = cmpUN_(next, un);
    if (ZuUnlikely(result != OpResult::Executed)) {
      l(result, nullptr, next);
      return;
    }
    findUpd_<KeyID>(shard, ZuMv(key),
      [this, shard, un, l = ZuFwd<L>(l)](
	ZmRef<Row<T>> row) mutable {
	if (ZuLikely(row)) {
	  update<KeyIDs_>(ZuMv(row), un, ZuMv(l));
	  return;
	}
	auto next = nextUN(shard);
	auto result = cmpUN_(next, un);
	if (result == OpResult::Executed) result = OpResult::Missing;
	l(result, nullptr, next);
      });
  }

  // delete lambda(ZdbRow<T> *)

  // delete record
  template <typename L>
  void del(ZmRef<Row<T>> row, L &&l) {
    auto shard = row->shard();

    ZmAssert(invoked(shard));

    if (!del_(nextUN(shard), row.ptr(), l)) l(nullptr);
  }
  // delete record at an intended UN
  // - lambda(OpResult::T, ZdbRow<T> *, UN)
  template <typename L>
  void del(ZmRef<Row<T>> row, UN un, L &&l) {
    auto shard = row->shard();

    ZiAssert(invoked(shard), "Zdb", (shard),
      "del called outside shard " << ZuBoxed(shard), return);

    auto next = nextUN(shard);
    auto result = cmpUN_(next, un);
    switch (result) {
      case OpResult::Executed: {
	auto l_ = [&l, next](Row<T> *row_) {
	  l(OpResult::Executed, row_, next);
	};
	if (!del_(un, row.ptr(), l_))
	  l(OpResult::Invalid, nullptr, next);
	return;
      }
      default:
	l(result, nullptr, next);
	return;
    }
  }

  // find and delete record (with key, without row)
  template <unsigned KeyID, typename L>
  inline void findDel(Shard shard, Key<KeyID> key, L &&l)
  {
    findUpd_<KeyID>(shard, ZuMv(key),
	[this, l = ZuFwd<L>(l)](ZmRef<Row<T>> row) mutable {
	if (ZuUnlikely(!row)) { l(row.ptr()); return; }
	del(ZuMv(row), ZuMv(l));
      });
  }

  // find and delete record at an intended UN (with key, without row)
  // - lambda(OpResult::T, ZdbRow<T> *, UN)
  template <unsigned KeyID, typename L>
  inline void findDel(Shard shard, Key<KeyID> key, UN un, L &&l) {
    ZiAssert(invoked(shard), "Zdb", (shard),
      "findDel called outside shard " << ZuBoxed(shard), return);

    auto next = nextUN(shard);
    auto result = cmpUN_(next, un);
    if (ZuUnlikely(result != OpResult::Executed)) {
      l(result, nullptr, next);
      return;
    }
    findUpd_<KeyID>(shard, ZuMv(key),
      [this, shard, un, l = ZuFwd<L>(l)](
	ZmRef<Row<T>> row) mutable {
	if (ZuLikely(row)) {
	  del(ZuMv(row), un, ZuMv(l));
	  return;
	}
	auto next = nextUN(shard);
	auto result = cmpUN_(next, un);
	if (result == OpResult::Executed) result = OpResult::Missing;
	l(result, nullptr, next);
      });
  }

private:
  template <typename L>
  bool insert_(UN un, Row<T> *row, L &l) {
    if (!row->insert_(un)) return false;
    try {
      l(row);
    } catch (...) { row->abort(); throw; }
    row->abort();
    return true;
  }

  template <typename KeyIDs_, typename L>
  bool update_(UN un, Row<T> *row, L &l) {
    if (!update_(row, un)) return false;
    // keep stale buffers alive until commit or rollback is complete
    auto bufs = ZmScratch(ZmRef<RepBuf<T>>, KeyIDs::N);	// undo buffer
    auto abort = [&row, &bufs]() {
      if (row->abort())
	for (unsigned i = 0, n = bufs.length(); i < n; i++)
	  bufs[i]->stale = false;
      bufs.null();
    };
    auto shard = row->shard();
    ZuUnroll::all<KeyIDs>([this, shard, &row, &bufs](auto KeyID) {
      auto key = ZuStructKey<KeyID>(row->data());
      auto i = m_bufCache[shard].template iter<KeyID>(ZuMv(key));
      while (auto repBuf = i()) {
	if (!repBuf->stale) {
	  repBuf->stale = true;
	  new (bufs.push()) ZmRef<RepBuf<T>>{ZuMv(repBuf)};
	  // at most one buffer per key can be fresh
	  break;
	}
      }
    });
    try {
      m_cache[shard].template update<KeyIDs_>(row,
	[&l](typename Cache<T>::Node *node) {
	  l(static_cast<Row<T> *>(node));
	});
    } catch (...) { abort(); throw; }
    abort();
    return true;
  }

  template <typename L>
  bool del_(UN un, Row<T> *row, L &l) {
    if (!del_(row, un)) return false;
    // all row keys are being invalidated, need to:
    // - evict from cache
    // - mark pending buffers indexed by the old keys as stale
    // - revert above actions on abort
    // - note that a new buffer is written by commit(), which
    //   causes a future find() to return null
    // keep stale buffers alive until commit or rollback is complete
    auto bufs = ZmScratch(ZmRef<RepBuf<T>>, KeyIDs::N);	// undo buffer
    auto abort = [&row, &bufs]() {
      if (row->abort())
	for (unsigned i = 0, n = bufs.length(); i < n; i++)
	  bufs[i]->stale = false;
      bufs.null();
    };
    auto shard = row->shard();
    ZuUnroll::all<KeyIDs>([this, shard, &row, &bufs](auto KeyID) {
      auto key = ZuStructKey<KeyID>(row->data());
      auto i = m_bufCache[shard].template iter<KeyID>(ZuMv(key));
      while (auto repBuf = i()) {
	if (!repBuf->stale) {
	  repBuf->stale = true;
	  new (bufs.push()) ZmRef<RepBuf<T>>{ZuMv(repBuf)};
	  break;
	}
      }
    });
    try {
      l(row);
    } catch (...) { abort(); throw; }
    abort();
    return true;
  }

  // commit insert/update/delete - causes replication/write
  ZmRef<IOBuf> commit(AnyRow *row) {
    auto shard = row->shard();

    ZmAssert(invoked(shard));

    int origState = row->state();
    if (!row->commit_()) return {};
    switch (origState) {
      case RowState::Insert:
	if (writeCache()) {
	  m_cache[shard].add(row, [this](AnyRow *row) {
	    if (row->pinned()) return false;
	    evictUN(row->shard(), row->un());
	    row->evict();
	    return true;
	  });
	  cacheUN(shard, row->un(), row);
	}
	incCount();
	break;
      case RowState::Update:
	// evictUN() already called from update_()
	if (writeCache())
	  cacheUN(shard, row->un(), row);
	break;
      case RowState::Delete:
	// evictUN() already called from del_()
	if (m_cache[shard].delNode(static_cast<Row<T> *>(row)))
	  row->evict();
	decCount();
	break;
    }
    ZmRef<IOBuf> buf = row->replicate(Msg::Tag::Replication);
    write(shard, buf, true);
    return buf;
  }

  // abort insert/update/delete
  bool abort(AnyRow *row) {
    ZmAssert(invoked(row->shard()));

    return row->abort_();
  }

  // low-level update, calls AnyRow::update_()
  bool update_(Row<T> *row, UN un) {
    auto prevUN = row->un();
    if (!row->update_(un)) return false;
    evictUN(row->shard(), prevUN);
    return true;
  }

  // low-level delete, calls AnyRow::del_()
  bool del_(Row<T> *row, UN un) {
    auto prevUN = row->un();
    if (!row->del_(un)) return false;
    evictUN(row->shard(), prevUN);
    return true;
  }

private:
  SchemaBuf			m_schema;

  // row caches
  ZuDerive(CacheArray, (ZtArray<Cache<T>>));
  CacheArray			m_cache;

  // pending replications
  ZuDerive(BufCacheArray, (ZtArray<BufCache<T>>));
  BufCacheArray			m_bufCache;
};

template <typename T, typename Impl>
using TableImpl = ZuIf<ZuIsSame<Impl, void>{}, Table<T>, Impl>;

template <typename T_, typename Impl_>
class Table : public Table_<TableImpl<T_, Impl_>> {
  using Base = Table_<TableImpl<T_, Impl_>>;

public:
  using Base::Base;
  using Base::operator =;
};

template <typename T>
inline ZmRef<IOBuf> Row_<T>::commit() {
  return this->table()->commit(static_cast<AnyRow *>(this));
}
template <typename T>
inline bool Row_<T>::abort() {
  return this->table()->abort(static_cast<AnyRow *>(this));
}

ZdbTableDerive(SagaTable, SagaData);
ZdbTableDerive(SagaStepTable, SagaStep);
ZdbTableDerive(SagaTypeTable, SagaTypeStep);

// --- table container

// Tables are shared with typed table handles and cannot own this DB registry node.
ZmRBTreeDerive(Tables, ZmRef<AnyTable>,
  ZmRBTreeKey<AnyTable::IDAxor,
    ZmRBTreeUnique<true,
      ZmRBTreeHeapID<"Zdb.Table">>>);

// --- DB host configuration

struct HostCf {
  ZuID		id;
  int		priority = 0;	// -1 is used internally for a failed host
  ZiIP		ip;
  uint16_t	port = 0;
  bool		standalone = false;
  String	up;
  String	down;

  HostCf(ZuCSpan id_) : id{id_}, standalone{true} { }
  HostCf(
      ZuCSpan id_, int priority_, ZiIP ip_, uint16_t port_,
      bool standalone_, String up_, String down_) :
    id{id_}, priority{priority_}, ip{ip_}, port{port_},
    standalone{standalone_}, up{ZuMv(up_)}, down{ZuMv(down_)}
  {
    if (!standalone && (!ip || !port))
      throw ZeEXCEPT(Error, "Zdb", ([id = id](auto &s) {
	s << '"' << id << "\": non-standalone host requires ip and port";
      }));
  }
  ZdbAPI HostCf(ZuCSpan id_, const ZfCf::AnyNode *cf);

  static ZuCSpan IDAxor(const HostCf &cfg) { return cfg.id; }
};

ZfStruct(ZdbAPI, (HostCf, Cf),
  (priority,	(Mutable, (Range<0, INT_MAX>)),		Int32),
  (ip, (Mutable),					String),
  (port,	(Mutable, (Range<1, 65534>)),		UInt16),
  (standalone, (Mutable),				Bool),
  (up, (Mutable),					String),
  (down, (Mutable),					String));


ZmRBTreeDerive(HostCfs, HostCf,
  ZmRBTreeKey<HostCf::IDAxor,
    ZmRBTreeUnique<true,
      ZmRBTreeHeapID<"Zdb.HostCf">>>);

// --- DB host

class ZdbAPI Host : public Ztc::DBHost {
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

  Ztc::DBHost::TelKey telKey() const override;
  void telemetry(Ztc::DBHostTelemetry &) const override;

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
  static ZuTuple<int, const ZuID &> IndexAxor(const Host &h) {
    return {h.priority(), h.config().id};
  }

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
ZmRBTreeDerive(HostIndex, Host,
  ZmRBTreeNode<Host,
    ZmRBTreeShadow<
      ZmRBTreeKey<Host::IndexAxor,
	ZmRBTreeUnique<true>>>>);
ZmHashDerive(Hosts, HostIndex::Node,
  (ZmHashNode<HostIndex::Node,
    ZmHashKey<Host::IDAxor,
	ZmHashHeapID<"Zdb.Host">>>));

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

struct StoreLoadCf {
  String	module;
  bool		preload = false;
};

ZfStruct(ZdbAPI, (StoreLoadCf, Cf),
  (module, (Mutable, Required),		String),
  (preload, (Mutable),			Bool));

using DBThreads = Ztc::DBThreads;
using DBSIDs = Ztc::DBSIDs;

struct DBCf {
  ZuID			thread;
  mutable unsigned	sid = 0;
  unsigned		nShards = 1;
  DBThreads		threads;
  mutable DBSIDs	sids;
  const ZfCf::AnyNode	*storeCf = nullptr;
  TableCfs		tableCfs;
  HostCfs		hostCfs;
  ZuID			hostID;
  unsigned		nAccepts = 8;
  unsigned		heartbeatFreq = 1;
  unsigned		heartbeatTimeout = 4;
  unsigned		reconnectFreq = 1;
  unsigned		electionTimeout = 8;
  ZmHashParams		cxnHash;
#if Zdb_DEBUG
  bool			debug = 0;
#endif

  DBCf() = default;
  DBCf(
      ZuID thread_, unsigned nShards_, DBThreads threads_,
      ZuID hostID_, unsigned nAccepts_,
      unsigned heartbeatFreq_, unsigned heartbeatTimeout_,
      unsigned reconnectFreq_, unsigned electionTimeout_
#if Zdb_DEBUG
      , bool debug_
#endif
      ) :
    thread{ZuMv(thread_)}, nShards{nShards_}, threads(ZuMv(threads_)),
    hostID{ZuMv(hostID_)}, nAccepts{nAccepts_},
    heartbeatFreq{heartbeatFreq_}, heartbeatTimeout{heartbeatTimeout_},
    reconnectFreq{reconnectFreq_}, electionTimeout{electionTimeout_}
#if Zdb_DEBUG
    , debug{debug_}
#endif
  {
    validate();
  }
  void validate() const {
    if (!nShards || nShards > 64 || (nShards & (nShards - 1)))
      throw ZeEXCEPT(Error, "Zdb", ([nShards = nShards](auto &s) {
	s << "\"shards\" invalid value " << nShards
	  << " (must be a power of two in [1, 64])";
      }));
    if (threads) {
      unsigned nThreads = threads.length();
      if ((nThreads & (nThreads - 1)) || nThreads > nShards)
	throw ZeEXCEPT(Error, "Zdb", ([nThreads, nShards = nShards](auto &s) {
	  s << "\"threads\" invalid array size " << nThreads
	    << " (" << nShards << " shards)";
	}));
    }
  }
  ZdbAPI DBCf(const ZfCf::AnyNode *cf);
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

ZfStruct(ZdbAPI, (DBCf, Cf),
  (thread,		(Mutable, Required),				String),
  (nShards,		(Mutable, Cf::ID<"shards">, (Range<1U, 64U>)),	UInt32),
  (threads,		(Mutable),					StringVec),
  (hostID,		(Mutable),					String),
  (nAccepts,		(Mutable, (Range<1U, INT_MAX>)),		UInt32),
  (heartbeatFreq,	(Mutable, (Range<1U, 3600U>)),			UInt32),
  (heartbeatTimeout,	(Mutable, (Range<1U, 14400U>)),			UInt32),
  (reconnectFreq,	(Mutable, (Range<1U, 3600U>)),			UInt32),
  (electionTimeout,	(Mutable, (Range<1U, 3600U>)),			UInt32)
#if Zdb_DEBUG
  ,
  (debug, (Mutable),							Bool)
#endif
);


// --- DB

class ZdbAPI DB :
    public ZmPolymorph, public ZmEngine<DB>, public Ztc::DB {
public:
  using Engine = ZmEngine<DB>;

  using Engine::start;
  using Engine::stop;
  bool start() override { return Engine::start(); }
  bool stop() override { return Engine::stop(); }

friend Engine;
friend class SagaDB;
friend Cxn_;
friend Host;
friend AnyTable;
friend AnyRow;
friend AnySaga__;
friend SagaDriver;

private:
  using Lock = ZmLock;
  using Guard = ZmGuard<Lock>;
  using ReadGuard = ZmReadGuard<Lock>;

public:
#if Zdb_DEBUG
  bool debug() const { return m_cf.debug; }
#endif

  DB();
  ~DB();

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
  ZmRef<TableT<T>> initTable(ZuCSpan id) {
    using Impl = TableT<T>;
    return initTable_(id,
      [](DB *db, TableCf *tableCf) mutable {
	return static_cast<AnyTable *>(new Impl{db, tableCf, false});
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

  unsigned nShards() const { return m_cf.nShards; }
  unsigned shardSID(Shard shard) const {
    ZmAssert(shard < m_cf.nShards, return 0);
    return m_cf.sids[shard & (m_cf.sids.length() - 1)];
  }
  template <typename ...Args>
  void shardRun(Shard shard, Args &&...args) const {
    ZmAssert(shard < m_cf.nShards);
    m_mx->run(ZuFwd<Args>(args)...,
      m_cf.sids[shard & (m_cf.sids.length() - 1)]);
  }
  template <typename ...Args>
  void shardInvoke(Shard shard, Args &&...args) const {
    ZmAssert(shard < m_cf.nShards);
    m_mx->invoke(ZuFwd<Args>(args)...,
      m_cf.sids[shard & (m_cf.sids.length() - 1)]);
  }
  bool shardInvoked(Shard shard) const {
    ZmAssert(shard < m_cf.nShards);
    return m_mx->invoked(
      m_cf.sids[shard & (m_cf.sids.length() - 1)]);
  }

  const DBCf &config() const { return m_cf; }
  ZiMultiplex *mx() const { return m_mx; }
  auto sid() const { return config().sid; }

  int state() const {
    return ZuLikely(m_self) ?
      m_self->state() : HostState::Instantiated;
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

  Ztc::DB::TelKey telKey() const override { return {m_cf.hostID}; }
  void telemetry(Ztc::DBTelemetry &) const override;
  unsigned allDBHosts(Ztc::DB::AllDBHostsFn) const override;
  unsigned allDBTables(Ztc::DB::AllDBTablesFn) const override;

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
  void startFailed();
  void sagaActivate(Host *);
  void sagaReplay();
  void sagaActivateDone();
  void sagaActivateFail(ZeException);
  void electionDone();
  void stop_0();
  void stop_1();
  void stop_2();
  using DrainFn = ZmFn<void(), ZmFnHeapID<"Zdb.DrainFn">>;
  template <typename L>
  void drainShards(unsigned shard, L &&l) {
    ZmAssert(invoked());
    if (shard == nShards()) {
      l();
      return;
    }
    // Drain shard callbacks before final() releases their tables.
    shardRun(Shard(shard), [this, shard, l = ZuFwd<L>(l)]() mutable {
      run([this, shard, l = ZuMv(l)]() mutable {
	drainShards(shard + 1, ZuMv(l));
      });
    });
  }

  // leader election and activation/deactivation
  void holdElection();		// elect new leader
  void deactivate(bool failed);	// become follower
  void reactivate(Host *host);	// re-assert leader

  void up_(Host *oldMaster);	// run up command
  void down_(bool failed);	// run down command

  void sagaRun(ZmRef<AnySaga_>);
  uint64_t sagaEpoch() const { return m_sagaEpoch; }
  SagaState::T sagaState() const { return m_sagaState; }
  bool appActive() const { return m_appActive; }
  void sagaPending() { ++m_sagaPending; }
  TableT<SagaData> *sagaTable() const { return m_sagaTable; }
  TableT<SagaStep> *sagaStepTable() const { return m_sagaStepTable; }
  void sagaDelRec(SagaRec *rec) {
    if (rec->un != nullUN())
      m_sagaUNHash->delNode(static_cast<SagaUNHash::Node *>(rec));
    m_sagaStepHash->delNode(rec);
  }
  void sagaBlocked(AnySaga_ *saga) { m_sagaBlocked = saga; }
  void sagaEnd(AnySaga_ *);
  bool sagaPrepare(AnySaga__ *, AnyTable *, Shard, SagaOp::T,
    bool, UN &, bool &);
  void sagaResult(ZmRef<AnySaga_>, uint64_t, OpResult::T, Shard);
  void sagaStepRecovered(ZmRef<AnySaga_>, uint64_t, uint32_t, Shard);
  SagaRec *sagaRec(AnySaga_ *, unsigned);
  bool sagaReserved(AnyTable *, Shard, UN);
  void sagaFail(ZmRef<AnySaga_>, uint64_t, ZeException);
  void sagaDrain(DrainFn);
  void sagaRetire();
  void sagaDrained();
  void sagaClean();
  void sagaDeactivate();

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
  void hbRcvd(Host *host, Msg::HeartbeatView hb);
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

  // immutable after optional pre-start saga registration
  SagaRecoveryFn	m_sagaCatalogFn;
  SagaRecoveryFn	m_sagaScanFn;
  SagaRunFn		m_sagaRunFn;
  TableCf		m_sagaCf{"saga"};
  TableCf		m_sagaStepCf{"saga_step"};
  ZmRef<TableT<SagaData>> m_sagaTable;
  ZmRef<TableT<SagaStep>> m_sagaStepTable;

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

  ZmRef<SagaHashObj>	m_sagaHash;
  ZmRef<SagaStepHashObj> m_sagaStepHash;
  ZmRef<SagaUNHashObj>	m_sagaUNHash;
  ZmRef<SagaScan>	m_sagaScan;
  SagaList		m_sagaQueue;
  DrainFn		m_sagaDrainFn;
  unsigned		m_sagaPending = 0;
  unsigned		m_sagaLive = 0;
  SagaState::T		m_sagaState = SagaState::Inactive;
  uint64_t		m_sagaEpoch = 0;
  AnySaga_ *m_sagaBlocked = nullptr;

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

};

inline unsigned AnyTable::nShards_() const
{
  return m_db->nShards();
}

template <typename ...Args>
inline void AnyTable::run(Shard shard, Args &&...args) const
{
  m_db->shardRun(shard, ZuFwd<Args>(args)...);
}

template <typename ...Args>
inline void AnyTable::invoke(Shard shard, Args &&...args) const
{
  m_db->shardInvoke(shard, ZuFwd<Args>(args)...);
}

inline bool AnyTable::invoked(Shard shard) const
{
  return m_db->shardInvoked(shard);
}

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

template <typename Impl_>
template <unsigned KeyID, typename L>
inline void Table_<Impl_>::count(GroupKey<KeyID> key, L &&l)
{
  using Context = Count;

  auto context = ZmRef(new Context{ZuFwd<L>(l)});

  Builder fbb{allocBuf(), ZiFB::BufferAlign<GroupKey<KeyID>>::N};
  fbb.finish(ZiFB::save(fbb, key));
  auto keyBuf = fbb.buf();
  AnyTable::count(KeyID, ZuMv(keyBuf), ZuMv(context));
}

template <typename Impl_>
template <
  unsigned KeyID,
  typename SelectKey,
  typename Tuple_,
  bool SelectRow,
  bool SelectNext,
  typename L>
inline void Table_<Impl_>::select_(
  SelectKey selectKey, bool inclusive, unsigned limit, L &&l)
{
  using Context = Select<Tuple_>;

  auto context = ZmRef(new Context{this, ZuFwd<L>(l)});

  Builder fbb{allocBuf(), ZiFB::BufferAlign<SelectKey>::N};
  fbb.finish(ZiFB::save(fbb, selectKey));
  auto keyBuf = fbb.buf();

  auto tupleFn = TupleFn{ZuMv(context),
    [](Context *context, TupleResult result) {
      if (ZuUnlikely(result.is<Event>())) { // error
	ZiLogEvent(ZuMv(result).p<Event>());
	auto table = context->table;
	auto db = table->db();
	ZiLOG(Fatal, "Zdb", ([id = table->id()](auto &s) {
	  s << "select from " << id << " failed";
	}));
	db->run([db]() { db->fail(); });
	context->fn(typename Context::Result{}, 0);
	return;
      }
      if (ZuUnlikely(!result.is<TupleData>())) { // end of results
	context->fn(typename Context::Result{}, 0);
	return;
      }
      const auto &tupleData = result.p<TupleData>();
      typename Context::Result output;
      ZiFB::handler<Tuple_>(tupleData.buf->cspan()).new_(output.template new_<Tuple_>());
      context->fn(ZuMv(output), tupleData.count);
    }};

  storeTbl()->select(
    SelectRow, SelectNext, inclusive,
    KeyID, ZuMv(keyBuf), limit, ZuMv(tupleFn));
}

template <typename Impl_>
template <
  unsigned KeyID, bool UpdateLRU, bool Evict, typename L>
inline void Table_<Impl_>::find_(Shard shard, Key<KeyID> key, L &&l) {
  ZmAssert(invoked(shard));

  FindLoad<KeyID> load{this, shard};
  if constexpr (Evict) {
    m_cache[shard].template find<KeyID, UpdateLRU>(
      ZuMv(key), ZuFwd<L>(l), ZuMv(load),
	FindEvict{this});
  } else
    m_cache[shard].template find<KeyID, UpdateLRU, false>(
	ZuMv(key), ZuFwd<L>(l), ZuMv(load));
}

template <typename Impl_>
template <unsigned KeyID, typename L>
inline void Table_<Impl_>::retrieve(
  Shard shard, Key<KeyID> key, L &&l)
{
  using Key_ = Key<KeyID>;
  using Context = FindCtx<T, Key_>;

  auto context = ZmRef(new Context{
    this, shard, ZuMv(key), ZuFwd<L>(l)});

  Builder fbb{allocBuf(), ZiFB::BufferAlign<Key_>::N};
  fbb.finish(ZiFB::save(fbb, context->key));
  auto keyBuf = fbb.buf();
  AnyTable::retrieve(KeyID, ZuMv(keyBuf), ZuMv(context));
}

template <typename T_, typename Impl, typename Complete, typename L>
inline void AnySaga__::insert(
    Impl *table, ZmRef<Row<T_>> row, Complete &&complete, L &&l)
{
  using Complete_ = ZuDecay<Complete>;
  using Fn = ZuDecay<L>;
  using Call = SagaInsert<Complete_, Fn>;

  Call call{ZuFwd<Complete>(complete), Fn{ZuFwd<L>(l)}};
  auto shard = row ? row->shard() : Shard{0};
  ZiAssert(table && table->invoked(shard), "Zdb", (),
    "saga insert invoked off shard", ::abort());
  if (ZuUnlikely(!m_fwd)) {
    table->insert(ZuMv(row), ZuMv(call));
    return;
  }
  UN un = nullUN();
  bool saved = false;
  if (!prepare_(table, shard, SagaOp::Insert, bool(row), un, saved)) {
    if (row) new (row->ptr()) T_{};
    return;
  }
  insert_(table, shard, ZuMv(row), un, saved, ZuMv(call));
}

template <typename T_, typename Impl, typename Call>
inline void AnySaga__::insert_(
    Impl *table, Shard shard, ZmRef<Row<T_>> row,
    UN un, bool saved, Call call)
{
  if (!row) {
    if (saved) {
      auto result = table->cmpUN_(table->nextUN(shard), un);
      if (result == OpResult::Executed) result = OpResult::Missing;
      result_(result, shard);
    } else
      result_(OpResult::Missing, shard);
    return;
  }
  if (!saved) un = table->nextUN(shard);
  if (!saved) {
    intent_(shard, un);
    table->insert(row, ZuMv(call));
    return;
  }
  OpResult::T result = OpResult::Invalid;
  table->insert(un, row, [this, &call, &result, shard](
      OpResult::T r, Row<T_> *o, UN) mutable {
    result = r;
    if (r != OpResult::Executed) return;
    stepShard(m_step, shard);
    call(o);
  });
  if (result == OpResult::Executed) return;
  new (row->ptr()) T_{};
  replay_(result, shard);
}

template <typename Complete>
inline void AnySaga__::skip(Complete &&complete)
{
  if (ZuUnlikely(m_rec)) {
    result_(OpResult::Invalid, m_rec->shard);
    return;
  }
  ZuFwd<Complete>(complete)(true);
}

template <typename KeyIDs_, typename T_, typename Impl,
  typename Complete, typename L>
inline void AnySaga__::update(
    Impl *table, ZmRef<Row<T_>> row, Complete &&complete, L &&l)
{
  using Complete_ = ZuDecay<Complete>;
  using Fn = ZuDecay<L>;
  using Call = SagaUpdate<Complete_, Fn>;

  Call call{ZuFwd<Complete>(complete), Fn{ZuFwd<L>(l)}};
  auto shard = row ? row->shard() : Shard{0};
  ZiAssert(table && table->invoked(shard), "Zdb", (),
    "saga update invoked off shard", ::abort());
  if (ZuUnlikely(!m_fwd)) {
    table->template update<KeyIDs_>(ZuMv(row), ZuMv(call));
    return;
  }
  UN un = nullUN();
  bool saved = false;
  if (!prepare_(table, shard, SagaOp::Update, bool(row), un, saved))
    return;
  update_<KeyIDs_>(table, shard, ZuMv(row), un, saved, ZuMv(call));
}

template <typename KeyIDs_, typename T_, typename Impl, typename Call>
inline void AnySaga__::update_(
    Impl *table, Shard shard, ZmRef<Row<T_>> row,
    UN un, bool saved, Call call)
{
  if (!row) {
    if (saved) {
      auto result = table->cmpUN_(table->nextUN(shard), un);
      if (result == OpResult::Executed) result = OpResult::Missing;
      result_(result, shard);
    } else
      result_(OpResult::Missing, shard);
    return;
  }
  if (!saved) un = table->nextUN(shard);
  if (!saved) {
    intent_(shard, un);
    table->template update<KeyIDs_>(row, ZuMv(call));
    return;
  }
  OpResult::T result = OpResult::Invalid;
  table->template update<KeyIDs_>(row, un, [
    this, &call, &result, shard
  ](OpResult::T r, Row<T_> *o, UN) mutable {
    result = r;
    if (r != OpResult::Executed) return;
    stepShard(m_step, shard);
    call(o);
  });
  if (result == OpResult::Executed) return;
  replay_(result, shard);
}

template <unsigned KeyID, typename KeyIDs_, typename Impl,
  typename Complete, typename L>
inline void AnySaga__::findUpd(
    Impl *table, Shard shard,
    typename Impl::template Key<KeyID> key,
    Complete &&complete, L &&l)
{
  using Complete_ = ZuDecay<Complete>;
  using Fn = ZuDecay<L>;
  using Call = SagaUpdate<Complete_, Fn>;

  Call call{ZuFwd<Complete>(complete), Fn{ZuFwd<L>(l)}};
  ZiAssert(table && table->invoked(shard), "Zdb", (),
    "saga find-update invoked off shard", ::abort());
  if (ZuUnlikely(!m_fwd)) {
    table->template findUpd<KeyID, KeyIDs_>(shard, ZuMv(key),
      ZuMv(call));
    return;
  }
  findUpd_<KeyID, KeyIDs_>(
    table, shard, ZuMv(key), ZuMv(call));
}

template <unsigned KeyID, typename KeyIDs_, typename Impl, typename Call>
inline void AnySaga__::findUpd_(
    Impl *table, Shard shard,
    typename Impl::template Key<KeyID> key, Call call)
{
  UN un = nullUN();
  bool saved = false;
  if (!prepare_(table, shard, SagaOp::Update, false, un, saved))
    return;
  if (saved) {
    auto result = un == nullUN() ? OpResult::Skipped :
      table->cmpUN_(table->nextUN(shard), un);
    if (result != OpResult::Executed) {
      replay_(result, shard);
      return;
    }
  }
  table->template findUpd_<KeyID>(shard, ZuMv(key),
    SagaFindUpdate<KeyIDs_, Impl, Call>{
      this, table, shard, un, saved, ZuMv(call)});
}

template <typename T_, typename Impl, typename Complete, typename L>
inline void AnySaga__::del(
    Impl *table, ZmRef<Row<T_>> row, Complete &&complete, L &&l)
{
  using Complete_ = ZuDecay<Complete>;
  using Fn = ZuDecay<L>;
  using Call = SagaDelete<Complete_, Fn>;

  Call call{ZuFwd<Complete>(complete), Fn{ZuFwd<L>(l)}};
  auto shard = row ? row->shard() : Shard{0};
  ZiAssert(table && table->invoked(shard), "Zdb", (),
    "saga delete invoked off shard", ::abort());
  if (ZuUnlikely(!m_fwd)) {
    if (row)
      table->del(ZuMv(row), ZuMv(call));
    else
      call(nullptr);
    return;
  }
  UN un = nullUN();
  bool saved = false;
  if (!prepare_(table, shard, SagaOp::Delete, bool(row), un, saved))
    return;
  del_(table, shard, ZuMv(row), un, saved, ZuMv(call));
}

template <typename T_, typename Impl, typename Call>
inline void AnySaga__::del_(
    Impl *table, Shard shard, ZmRef<Row<T_>> row,
    UN un, bool saved, Call call)
{
  if (saved && un == nullUN()) {
    stepShard(m_step, shard);
    stepRecovered_(shard);
    return;
  }
  if (!row && saved) {
    auto result = table->cmpUN_(table->nextUN(shard), un);
    if (result == OpResult::Executed) result = OpResult::Missing;
    result_(result, shard);
    return;
  }
  if (!saved) un = row ? table->nextUN(shard) : nullUN();
  if (!saved) {
    intent_(shard, un);
    if (row)
      table->del(row, ZuMv(call));
    else
      call(nullptr);
    return;
  }
  OpResult::T result = OpResult::Invalid;
  table->del(row, un, [this, &call, &result, shard](
      OpResult::T r, Row<T_> *o, UN) mutable {
    result = r;
    if (r != OpResult::Executed) return;
    stepShard(m_step, shard);
    call(o);
  });
  if (result == OpResult::Executed) return;
  replay_(result, shard);
}

template <unsigned KeyID, typename Impl, typename Complete, typename L>
inline void AnySaga__::findDel(
    Impl *table, Shard shard,
    typename Impl::template Key<KeyID> key,
    Complete &&complete, L &&l)
{
  using Complete_ = ZuDecay<Complete>;
  using Fn = ZuDecay<L>;
  using Call = SagaDelete<Complete_, Fn>;

  Call call{ZuFwd<Complete>(complete), Fn{ZuFwd<L>(l)}};
  ZiAssert(table && table->invoked(shard), "Zdb", (),
    "saga find-delete invoked off shard", ::abort());
  if (ZuUnlikely(!m_fwd)) {
    table->template findDel<KeyID>(shard, ZuMv(key), ZuMv(call));
    return;
  }
  findDel_<KeyID>(table, shard, ZuMv(key), ZuMv(call));
}

template <unsigned KeyID, typename Impl, typename Call>
inline void AnySaga__::findDel_(
    Impl *table, Shard shard,
    typename Impl::template Key<KeyID> key, Call call)
{
  UN un = nullUN();
  bool saved = false;
  if (!prepare_(table, shard, SagaOp::Delete, false, un, saved))
    return;
  if (saved) {
    auto result = un == nullUN() ? OpResult::Skipped :
      table->cmpUN_(table->nextUN(shard), un);
    if (result != OpResult::Executed) {
      replay_(result, shard);
      return;
    }
  }
  table->template findUpd_<KeyID>(shard, ZuMv(key),
    SagaFindDelete<Impl, Call>{
      this, table, shard, un, saved, ZuMv(call)});
}

template <typename KeyIDs, typename Impl, typename Call>
inline void SagaFindUpdate<KeyIDs, Impl, Call>::operator ()(
    ZmRef<Row<typename Impl::T>> row)
{
  if (!saved) {
    UN un_ = nullUN();
    bool saved_ = false;
    if (!saga->prepare_(
	  table, shard, SagaOp::Update, bool(row), un_, saved_))
      return;
    un = un_;
    saved = saved_;
  }
  saga->template update_<KeyIDs>(
    table, shard, ZuMv(row), un, saved, ZuMv(call));
}

template <typename Impl, typename Call>
inline void SagaFindDelete<Impl, Call>::operator ()(
    ZmRef<Row<typename Impl::T>> row)
{
  if (!saved) {
    UN un_ = nullUN();
    bool saved_ = false;
    if (!saga->prepare_(
	  table, shard, SagaOp::Delete, bool(row), un_, saved_))
      return;
    un = un_;
    saved = saved_;
  }
  saga->del_(table, shard, ZuMv(row), un, saved, ZuMv(call));
}

enum { SagaScanSize = 256 }; // cold store query page, not a replay batch

class ZdbAPI SagaDB : public DB {
public:
  void sagas(SagaDefs);
  template <typename S, typename Submit, typename Complete,
    typename = ZuIfT<SagaCallbackValid<Submit>{} && SagaCallbackValid<Complete>{}>>
  bool saga(Shard shard, SagaID id, ZmRef<S> saga,
      Submit &&submit, Complete &&complete, ZuTime deadline = {}) {
    return saga_(shard, id, ZmRef<AnySaga_>{ZuMv(saga)},
      completeFn(ZuFwd<Submit>(submit)),
      completeFn(ZuFwd<Complete>(complete)), deadline);
  }
  void final();

private:
  template <typename Complete>
  static SagaCompleteFn completeFn(Complete &&complete) {
    if constexpr (ZuIsSame<ZuDecay<Complete>, SagaCompleteFn>{})
      return ZuFwd<Complete>(complete);
    else
      return SagaCompleteFn{[complete = ZuFwd<Complete>(complete)](
        bool ok) mutable { complete(ok); }};
  }

  int match(ZuCSpan type) const;
  bool saga_(Shard, SagaID, ZmRef<AnySaga_>, SagaCompleteFn, SagaCompleteFn, ZuTime);
  void sagaCatalog();
  void sagaCatalogOpen(ZmRef<SagaCatalog>, OpenResult);
  void sagaCatalogScan(ZmRef<SagaCatalog>, bool);
  void sagaCatalogWrite(ZmRef<SagaCatalog>);
  void sagaCatalogClose(ZmRef<SagaCatalog>);
  void sagaCatalogFinish(ZmRef<SagaCatalog>);
  void sagaScanData(ZmRef<SagaScan>, bool);
  void sagaLoadData(ZmRef<SagaScan>);
  void sagaScanSteps(ZmRef<SagaScan>, bool);
  void sagaLoadSteps(ZmRef<SagaScan>);
  void sagaOrphanDone(ZmRef<SagaScan>, bool);

  SagaDefs m_sagaDefs;
  ZmRef<SagaDefHash> m_sagaDefHash;
};

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
  Msg::RecordView record;
  const AnyTable *table = nullptr;
  template <typename S> void print(S &s) const {
    auto id = record.get<Msg::ZfField(Record, table)>();
    auto data = record.get<Msg::ZfField(Record, data)>();
    s << "{db=" << id
      << " shard=" << ZuBoxed(record.get<Msg::ZfField(Record, shard)>())
      << " un=" << record.get<Msg::ZfField(Record, un)>()
      << " sn=" << ZuBoxed(record.get<Msg::ZfField(Record, sn)>())
      << " vn=" << record.get<Msg::ZfField(Record, vn)>() << "}";
    if (data) {
      s << " data=";
      if (table) {
	ZuVStream s_(s);
	table->rowPrintFB(s_, data);
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
  Msg::HeartbeatView hb;
  template <typename S> void print(S &s) const {
    auto id = hb.get<Msg::ZfField(Heartbeat, host)>();
    auto dbState = hb.get<Msg::ZfField(Heartbeat, dbState)>();
    s << "{host=" << id
      << " state=" << HostState::name(hb.get<Msg::ZfField(Heartbeat, state)>())
      << " dbState={sn=" << ZuBoxed(dbState.get<Msg::ZfField(DBState, sn)>())
      << " dbs=[";
    unsigned n = 0;
    for (auto state: dbState.get<Msg::ZfField(DBState, tableStates)>()) {
      if (n++) s << ',';
      s << '{' << state.get<Msg::ZfField(TableState, table)>() << '.'
	<< ZuBoxed(state.get<Msg::ZfField(TableState, shard)>()) << ','
	<< ZuBoxed(state.get<Msg::ZfField(TableState, un)>()) << '}';
    }
    s << "]}}";
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
inline void AnyRow::print(S &s) const {
  s << "{table=" << m_table->id()
    << " state=" << RowState::name(m_state)
    << " shard=" << ZuBoxed(m_shard)
    << " un=" << m_un
    << " sn=" << m_sn
    << " vn=" << m_vn;
  if (m_origUN != nullUN()) s << " origUN=" << m_origUN;
  s << " data=";
  {
    ZuVStream s_{s};
    m_table->rowPrint(s_, ptr_());
  }
  s << '}';
}

} // Zdb_

// external API

using ZdbAnyRow = Zdb_::AnyRow;
template <typename T> using ZdbRow = Zdb_::Row<T>;
template <typename T> using ZdbRowRef = ZmRef<ZdbRow<T>>;
template <typename T> using ZdbRowBuilder = Zdb_::RowBuilder<T>;
namespace ZdbRowState = Zdb_::RowState;

using ZdbAnyTable = Zdb_::AnyTable;
template <typename T> using ZdbTable = Zdb_::TableT<T>;
using ZdbTableCf = Zdb_::TableCf;
template <typename T> using ZdbTblRef = ZmRef<ZdbTable<T>>;

using Zdb = Zdb_::DB;
using ZdbSagaDB = Zdb_::SagaDB;
using ZdbHandler = Zdb_::DBHandler;
using ZdbCf = Zdb_::DBCf;

using ZdbUpFn = Zdb_::UpFn;
using ZdbDownFn = Zdb_::DownFn;
using ZdbHandler = Zdb_::DBHandler;

using ZdbHost = Zdb_::Host;

using ZdbBuf = Zdb_::IOBuf;

#endif /* Zdb_HH */
