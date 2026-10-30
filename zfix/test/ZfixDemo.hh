//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef ZfixDemo_HH
#define ZfixDemo_HH

#include <zlib/ZuTuple.hh>
#include <zlib/ZmPlatform.hh>
#include <zlib/ZmHash.hh>
#include <zlib/ZtArray.hh>
#include <zlib/ZfJSON.hh>
#include <zlib/Zfix.hh>
#include <zlib/ZfixSession.hh>
#include <zlib/ZfixTx.hh>

namespace ZfixDemo {

// Explicit example dictionary, not a canonical venue order protocol.
template <typename T> struct Rows : ZtArray<T, ZtArrayHeapID<"Zfix.Demo.Rows">> {
  ZuDerive_(Rows, (ZtArray<T, ZtArrayHeapID<"Zfix.Demo.Rows">>));
friend Zfix::Fmt::AsArray<> Zfix_Fmt(Rows *);
};

struct Party {
  ZuCSpan	id;
  unsigned	role = 0;
};

struct Request {
  uint64_t	key = 0;
  unsigned	qty = 0;
  ZuCSpan	symbol;
  Rows<Party>	parties;
};

struct Report {
  uint64_t	key = 0, exec = 0;
  ZuCSpan	kind;
  unsigned	qty = 0;
};

struct Event {
  ZuCSpan	symbol;
  int64_t	price = 0;
};

struct Login {
  Zfix::Session::Logon	common;
  ZuCSpan		user, password;
};

ZfStruct(, (Party, FIX), (id, (FIX::ID<448>, Required), String), (role, (FIX::ID<452>), UInt32));
ZfStruct(, (Request, FIX), (key, (FIX::ID<11>, Required), UInt64),
  (qty, (FIX::ID<38>, Required), UInt32), (symbol, (FIX::ID<55>, Required), String),
  (parties, (FIX::ID<453>), UDT));
ZfStruct(, (Report, FIX), (key, (FIX::ID<11>, Required), UInt64),
  (exec, (FIX::ID<17>, Required), UInt64), (kind, (FIX::ID<150>, Required), String),
  (qty, (FIX::ID<14>), UInt32));
ZfStruct(, (Event, FIX), (symbol, (FIX::ID<55>, Required), String), (price, (FIX::ID<44>),
  Int64));
ZfStruct(, (Login, FIX), (common, , UDT), (user, (FIX::ID<553>, Required), String),
  (password, (FIX::ID<554>, Required), String));
using Ack = Zfix::MT<Report, "U2", ZuFacet::FIX, Zfix::Discriminator<150, "A">>;
using Fill = Zfix::MT<Report, "U2", ZuFacet::FIX, Zfix::Discriminator<150, "F">>;
using Req = Zfix::MT<Request, "U1", ZuFacet::FIX, Zfix::NoDiscriminator, ZuTypeList<Ack,
  Fill>>;
using Tick = Zfix::MT<Event, "U3">;
using Dictionary = Zfix::MTList<Req, Ack, Fill, Tick>;
using LoginMsg = Zfix::MT<Login, "A">;

struct Call {
  uint64_t	ack = 0, fill = 0;
  bool		rejected = false;

  bool complete() const { return rejected || (ack && fill); }
};

struct CallStats {
  unsigned	registered = 0, complete = 0, duplicate = 0, unknown = 0,
    invalid = 0, rejected = 0;
  uint64_t	firstComplete = 0;
};

// All access belongs to the application's Rx owner. Completed keys stay until
// explicit release so late/duplicate reports do not create another call.
class Calls {
public:
  explicit Calls(unsigned limit) : m_limit{limit} { }

  bool add(uint64_t key);
  bool report(const Report &);
  bool reject(uint64_t key);

  bool release(uint64_t key) { return bool(m_calls.del(key)); }

  void clear() { m_calls.clean(); }

  unsigned count() const { return m_calls.count_(); }
  const CallStats &stats() const { return m_stats; }
private:
  using Map = ZmHashKV<uint64_t, Call, ZmHashLock<ZmNoLock, ZmHashHeapID<"Zfix.Demo.Calls">>>;
  void completed(uint64_t);
  const unsigned	m_limit;
  Map			m_calls;
  CallStats		m_stats;
};

struct Result {
  unsigned	qty = 0;
  uint64_t	ack = 0, fill = 0;
};

namespace Decision {

  enum { New, Cached, Refused, UnknownReplay, Conflict };
}

struct ServerStats {
  unsigned	fresh = 0, cached = 0, restored = 0, unknownReplay = 0;
};

class Results {
public:
  explicit Results(unsigned limit) : m_limit{limit} { }

  unsigned admit(uint64_t key, unsigned qty, bool replay);
  bool restore(uint64_t key, Result);
  const Result *find(uint64_t key) const;

  void clear() { m_results.clean(); }

  unsigned count() const { return m_results.count_(); }
  const ServerStats &stats() const { return m_stats; }
private:
  using Map = ZmHashKV<uint64_t, Result, ZmHashLock<ZmNoLock, ZmHashHeapID<"Zfix.Demo.Results">>>;
  const unsigned	m_limit;
  Map			m_results;
  uint64_t		m_nextExec = 0;
  ServerStats		m_stats;
};

// Tx-owned association and immutable replay history. Availability/retention are
// application decisions; an absent lookup delegates protocol gap fill to Zfix.
class Store {
public:
  using Key = ZuTuple<uint64_t, uint64_t>;

  explicit Store(unsigned count, uint64_t bytes = uint64_t(Zfix::Fmt::Limits::FrameBytes)<<2) :
    m_count{count}, m_limit{bytes} { }

  bool associate(Zfix::TxBody::Ref, Zfix::TxContext);
  bool published(Zfix::TxContext, ZuDateTime);
  bool erase(uint64_t epoch, uint64_t seq);
  uint64_t key(uint64_t epoch, uint64_t seq) const;
  Zfix::TxReload fetch(uint64_t epoch, uint64_t seq) const;
  bool restore(Zfix::TxHistory);

  unsigned count() const { return m_records.count_(); }
  uint64_t bytes() const { return m_bytes; }

  void clear() { m_records.clean(); m_bytes = 0; }
private:
  using Map = ZmHashKV<Key, Zfix::TxHistory,
    ZmHashLock<ZmNoLock, ZmHashHeapID<"Zfix.Demo.Store">>>;
  const unsigned	m_count;
  const uint64_t	m_limit;
  Map			m_records;
  uint64_t		m_bytes = 0;
};

struct Summary {
  // Both application links use one Rx shard and one Tx shard in this fixture.
  uint64_t	firstComplete = 0;
  unsigned	registered = 0, complete = 0, duplicate = 0, unknown = 0, rejected = 0;
  unsigned	fresh = 0, cached = 0, events = 0, groups = 0, authAccepted = 0, authRefused = 0;
  alignas(Zm::CacheLineSize)
  unsigned	localSent = 0;
  unsigned	archived = 0, retrieved = 0, seqRejects = 0;
  // Only main fills output metadata after all workers stop.
  ZuCSpan	scenario;
  unsigned	errors = 0;
  bool		tls = false;
};

ZfStruct(, (Summary, JSON), (scenario, , String), (tls, , Bool), (errors, , UInt32),
  (registered, , UInt32), (complete, , UInt32), (duplicate, , UInt32), (unknown, , UInt32),
  (rejected, , UInt32), (fresh, , UInt32), (cached, , UInt32), (events, , UInt32),
  (groups, , UInt32), (authAccepted, , UInt32), (authRefused, , UInt32),
  (localSent, , UInt32), (archived, , UInt32), (retrieved, , UInt32),
  (seqRejects, , UInt32), (firstComplete, , UInt64));
} // ZfixDemo

#endif /* ZfixDemo_HH */
