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
#include <zlib/ZmLHash.hh>

#include <zlib/ZtArray.hh>
#include <zlib/ZtEnum.hh>
#include <zlib/ZtString.hh>

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wchanges-meaning"
#endif
#include <zlib/ZdbMemStore.hh>
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
#include <zlib/ZdbSLCodec.hh>

namespace ZdbSL {

using namespace Zdb_;

ZuDerive(SQLString, ZtString<ZtStringHeapID<"ZdbSL.SQLString">>);
ZuDerive(FieldID, ZtString<ZtStringHeapID<"ZdbSL.FieldID">>);

namespace Synchronous {
  ZtEnum(ZdbSLAPI, Synchronous, int8_t, NORMAL, FULL, OFF);
}

namespace OpenState {
  enum { Closed, Opened, Reopen };
}

struct XField {
  FieldID		id;
  const ZfVField	*vfield = nullptr;
  ZdbMem::XField	field{};
};
ZuDerive(XFields, (ZtArray<XField, ZtArrayHeapID<"ZdbSL.XField">>));
ZuDerive(XKeyFields,
  (ZtArray<XFields, ZtArrayHeapID<"ZdbSL.XKeyField">>));
ZuDerive(UpdFields,
  (ZtArray<const ZfVField *, ZtArrayHeapID<"ZdbSL.UpdFields">>));
ZuDerive(KeyGroup,
  (ZtArray<unsigned, ZtArrayHeapID<"ZdbSL.KeyGroup">>));
ZuDerive(MaxUN, (ZtArray<UN, ZtArrayHeapID<"ZdbSL.MaxUN">>));
ZmLHashKVDerive(FieldMap, FieldID, unsigned, ZmLHashLocal<>);

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
friend Store;

public:
  StoreTbl(
    Store *, bool, IDString, unsigned,
    ZfVFieldArray, ZfVKeyFieldArray,
    const reflection::Schema *, IOBufAllocFn);
  virtual ~StoreTbl();

  Store *store() const { return m_store; }
  const IDString &id() const { return m_id; }
  bool internal() const { return m_internal; }

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

  ZmRef<IOBuf> tuple_(sqlite3_stmt *, const XFields &);
  template <bool Recovery> ZmRef<IOBuf> row_(sqlite3_stmt *);

  Store			*m_store = nullptr;
  IDString		m_id;
  SQLString		m_relation;
  ZfVFieldArray		m_fields;
  UpdFields		m_updFields;
  ZfVKeyFieldArray	m_keyFields;
  XFields		m_xFields;
  XFields		m_xUpdFields;
  XKeyFields		m_xKeyFields;
  KeyGroup		m_keyGroup;
  FieldMap		m_fieldMap;
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
  ZtString<>	thread;
  ZtString<>	connection;
  Synchronous::T synchronous = Synchronous::NORMAL;
};

ZfStruct(ZdbSLAPI, (StoreCf, Cf),
  (((thread), (Required)),			(String)),
  (((connection), (Required)),			(String)),
	(((synchronous), (Enum<Synchronous::Map>)),	(Int8,
	  Synchronous::NORMAL)));

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

  ZtString<>		m_connection;
  ZiMultiplex		*m_mx = nullptr;
  unsigned		m_sid = 0;
  unsigned		m_nShards = 0;
  FailFn		m_failFn;
  ZmRef<StoreTbls>	m_storeTbls;
  sqlite3		*m_conn = nullptr;
  bool			m_stopping = false;
  Synchronous::T	m_synchronous = Synchronous::NORMAL;
};

} // ZdbSL

extern "C" {
  ZdbSLExtern Zdb_::Store *ZdbStore();
}

#endif /* ZdbSL_HH */
