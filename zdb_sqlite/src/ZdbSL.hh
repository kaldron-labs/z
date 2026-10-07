//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Zdb SQLite persistent data store

#ifndef ZdbSL_HH
#define ZdbSL_HH

#ifndef ZdbSLLib_HH
#include <zlib/ZdbSLLib.hh>
#endif

#include <sqlite3.h>

#include <zlib/ZmHash.hh>

#include <zlib/ZtArray.hh>
#include <zlib/ZtEnum.hh>
#include <zlib/ZtString.hh>

#include <zlib/ZfbStruct.hh>

#include <zlib/ZdbStore.hh>

namespace ZdbSL {

using namespace Zdb_;

ZuDerive(SQLString, ZtString<ZtStringHeapID<"ZdbSL.SQLString">>);

namespace Synchronous {
  ZtEnum(ZdbSLAPI, Synchronous, int8_t, NORMAL, FULL, OFF);
}

namespace OpenState {
  enum { Closed, Opened, Reopen };
}

struct FieldInfo {
  const reflection::Field	*field = nullptr;
  uint8_t			type = 0;
  uint8_t			storage = 0;
  uint8_t			fixedSize = 0;
};

struct XField {
  IDString		id;
  const ZfVField	*vfield = nullptr;
  FieldInfo		field;
};
ZuDerive(XFields, (ZtArray<XField, ZtArrayHeapID<"ZdbSL.XField">>));
struct XFieldRef {
  const XField	*field = nullptr;
  bool		descend = false;
};
ZuDerive(XFieldRefs,
  (ZtArray<XFieldRef, ZtArrayHeapID<"ZdbSL.XFieldRef">>));
ZuDerive(XKeyFields,
  (ZtArray<XFieldRefs, ZtArrayHeapID<"ZdbSL.XKeyField">>));
ZuDerive(KeyGroup,
  (ZtArray<unsigned, ZtArrayHeapID<"ZdbSL.KeyGroup">>));
ZuDerive(KeyDirs,
  (ZtArray<uint8_t, ZtArrayHeapID<"ZdbSL.KeyDirs">>));
ZuDerive(MaxUN, (ZtArray<UN, ZtArrayHeapID<"ZdbSL.MaxUN">>));

struct KeyStmts {
  sqlite3_stmt	*count = nullptr;
  sqlite3_stmt	*selectKIX = nullptr;
  sqlite3_stmt	*selectKNX = nullptr;
  sqlite3_stmt	*selectKNI = nullptr;
  sqlite3_stmt	*selectRIX = nullptr;
  sqlite3_stmt	*selectRNX = nullptr;
  sqlite3_stmt	*selectRNI = nullptr;
  sqlite3_stmt	*find = nullptr;
};

ZuDerive(KeyStmtArray,
  (ZtArray<KeyStmts, ZtArrayHeapID<"ZdbSL.KeyStmt">>));

struct Stmts {
  KeyStmtArray	keys;
  sqlite3_stmt	*recover = nullptr;
  sqlite3_stmt	*insert = nullptr;
  sqlite3_stmt	*update = nullptr;
  sqlite3_stmt	*del = nullptr;
  sqlite3_stmt	*mrd = nullptr;
};

class Store;

class ZdbSLAPI StoreTbl : public Zdb_::StoreTbl {
public:
  StoreTbl(
    Store *, bool, IDString, unsigned,
    ZfVFieldArray, ZfVKeyFieldArray,
    const reflection::Schema *, IOBufAllocFn);
  virtual ~StoreTbl();

  Store *store() const { return m_store; }
  const IDString &id() const { return m_id; }
  bool internal() const { return m_internal; }
  void stopped() {
    finalize_();
    m_openState = OpenState::Closed;
  }
  bool openable() const {
    return m_openState == OpenState::Closed ||
      m_openState == OpenState::Reopen;
  }

  void open(OpenFn fn) { open_(ZuMv(fn)); }
  void close(CloseFn);
  void warmup();
  void count(KeyID, ZmRef<IOBuf>, CountFn);
  void select(bool, bool, bool, KeyID, ZmRef<IOBuf>, unsigned, TupleFn);
  void find(KeyID, ZmRef<IOBuf>, RowFn);
  void recover(Shard, UN, RowFn);
  void write(ZmRef<IOBuf>, CommitFn);

private:
  void open_(OpenFn);
  void close_(CloseFn);
  void count_(KeyID, ZmRef<IOBuf>, CountFn);
  void select_(bool, bool, bool, KeyID, ZmRef<IOBuf>, unsigned, TupleFn);
  void find_(KeyID, ZmRef<IOBuf>, RowFn);
  void recover_(Shard, UN, RowFn);
  void write_(ZmRef<IOBuf>, CommitFn);

  void schema_();
  void statements_();
  void state_();
  void finalize_();

  template <typename Fields>
  ZmRef<IOBuf> tuple_(sqlite3_stmt *, const Fields &);
  template <bool Recovery> ZmRef<IOBuf> row_(sqlite3_stmt *);

  Store			*m_store = nullptr;
  IDString		m_id;
  SQLString		m_relation;
  XFields		m_xFields;
  XFieldRefs		m_xUpdFields;
  XKeyFields		m_xKeyFields;
  KeyGroup		m_keyGroup;
  KeyDirs		m_keyDirs;
  IOBufAllocFn		m_bufAllocFn;
  bool			m_internal = false;
  int8_t		m_openState = OpenState::Closed;
  uint64_t		m_count = 0;
  MaxUN			m_maxUN;
  SN			m_maxSN = ZdbNullSN();
  Stmts			m_stmts;
};

inline auto StoreTbl_IDAxor(const StoreTbl &tbl) {
  return ZuTuple<bool, ZuCSpan>{tbl.internal(), tbl.id()};
}
ZmHashDerive(StoreTbls, StoreTbl,
  (ZmHashNode<StoreTbl,
    ZmHashKey<StoreTbl_IDAxor,
      ZmHashLock<ZmNoLock,
	ZmHashHeapID<"ZdbSL.StoreTbl">>>>));

struct StoreCf {
	Zdb_::String	thread;
	Zdb_::String	connect;
  Synchronous::T synchronous = Synchronous::NORMAL;
};

ZfStruct(ZdbSLAPI, (StoreCf, Cf),
  (thread, (Mutable, Required),					String),
  (connect, (Mutable, Required),				String),
	(synchronous, (Mutable, Enum<Synchronous::Map>),	Int8));

class ZdbSLAPI Store : public Zdb_::Store {
public:
  InitResult init(const ZfCf::AnyNode *, ZiMultiplex *, unsigned, FailFn);
  void final();
  void start(StartFn);
  void stop(StopFn);
  void open(
    bool, IDString, ZfVFieldArray, ZfVKeyFieldArray,
    const reflection::Schema *, IOBufAllocFn, OpenFn);

  bool stopping() const { return m_stopping; }
  sqlite3 *conn() const { return m_conn; }
  unsigned nShards() const { return m_nShards; }
  unsigned lengthLimit() const { return m_lengthLimit; }
  sqlite3_stmt *beginStmt() const { return m_beginStmt; }
  sqlite3_stmt *commitStmt() const { return m_commitStmt; }

  template <typename ...Args> void run(Args &&...args) {
    m_mx->run(ZuFwd<Args>(args)..., m_sid);
  }
  template <typename ...Args> void invoke(Args &&...args) {
    m_mx->invoke(ZuFwd<Args>(args)..., m_sid);
  }

private:
  void start_(StartFn);
  void stop_(StopFn);
  void stop_1(StopFn);
  void startup_();

  Zdb_::String		m_connection;
  ZiMultiplex		*m_mx = nullptr;
  unsigned		m_sid = 0;
  unsigned		m_nShards = 0;
  FailFn		m_failFn;
  ZmRef<StoreTbls>	m_storeTbls;
  sqlite3		*m_conn = nullptr;
  sqlite3_stmt		*m_beginStmt = nullptr;
  sqlite3_stmt		*m_commitStmt = nullptr;
  unsigned		m_lengthLimit = 0;
  bool			m_stopping = false;
  Synchronous::T	m_synchronous = Synchronous::NORMAL;
};

} // ZdbSL

extern "C" {
  ZdbSLExtern Zdb_::Store *ZdbStore();
}

#endif /* ZdbSL_HH */
