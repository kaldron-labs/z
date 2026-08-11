//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z HTTP/2 HPACK codec

#ifndef ZhttpHPack_HH
#define ZhttpHPack_HH

#ifndef ZhttpLib_HH
#include <zlib/ZhttpLib.hh>
#endif

#include <zlib/ZuPtr.hh>

#include <zlib/ZmHash.hh>
#include <zlib/ZmLHash.hh>
#include <zlib/ZmRef.hh>

#include <zlib/ZtArray.hh>
#include <zlib/ZtEnum.hh>

#include <zlib/ZhttpCompression.hh>

namespace Zhttp {

namespace H2 {

struct Field {
  ZuBSpan	name;
  ZuBSpan	value;

  bool equals(const Field &field) const {
    return name == field.name && value == field.value;
  }
  int cmp(const Field &field) const {
    if (int i = name.cmp(field.name)) return i;
    return value.cmp(field.value);
  }
  friend bool operator ==(const Field &l, const Field &r) {
    return l.equals(r);
  }
  friend int operator <=>(const Field &l, const Field &r) {
    return l.cmp(r);
  }
  uint32_t hash() const { return name.hash() ^ value.hash(); }
};

#define Zhttp_HPack_(Key, Value) \
  StaticEntry<ZuStringT<Key>, ZuStringT<Value>>
#define Zhttp_HPack(KV) \
  ZuPP_Defer(Zhttp_HPack_)(ZuPP_Strip(KV))
#define ZhttpHPackTbl(...) \
  ZuTypeList<ZuPP_Eval_(ZuPP_MapComma(Zhttp_HPack, __VA_ARGS__))>

using HPackTbl = ZhttpHPackTbl(
  (":authority", ""), (":method", "GET"), (":method", "POST"),
  (":path", "/"), (":path", "/index.html"), (":scheme", "http"),
  (":scheme", "https"), (":status", "200"), (":status", "204"),
  (":status", "206"), (":status", "304"), (":status", "400"),
  (":status", "404"), (":status", "500"), ("accept-charset", ""),
  ("accept-encoding", "gzip, deflate"), ("accept-language", ""),
  ("accept-ranges", ""), ("accept", ""),
  ("access-control-allow-origin", ""), ("age", ""), ("allow", ""),
  ("authorization", ""), ("cache-control", ""),
  ("content-disposition", ""), ("content-encoding", ""),
  ("content-language", ""), ("content-length", ""),
  ("content-location", ""), ("content-range", ""),
  ("content-type", ""), ("cookie", ""), ("date", ""), ("etag", ""),
  ("expect", ""), ("expires", ""), ("from", ""), ("host", ""),
  ("if-match", ""), ("if-modified-since", ""), ("if-none-match", ""),
  ("if-range", ""), ("if-unmodified-since", ""), ("last-modified", ""),
  ("link", ""), ("location", ""), ("max-forwards", ""),
  ("proxy-authenticate", ""), ("proxy-authorization", ""),
  ("range", ""), ("referer", ""), ("refresh", ""), ("retry-after", ""),
  ("server", ""), ("set-cookie", ""), ("strict-transport-security", ""),
  ("transfer-encoding", ""), ("user-agent", ""), ("vary", ""),
  ("via", ""), ("www-authenticate", ""));

ZtEnumStruct(ZhttpAPI, HPackFailure, uint8_t,
  None, Truncated, Integer, String, Index, Capacity, HeaderList);

using HPackString =
  ZtBArray<ZtArrayHeapID<"Zhttp.H2.HPack.String">>;

struct HPackEntry {
  HPackString	name;
  HPackString	value;
  uint32_t	size = 0;
};

using HPackEntries =
  ZtArray<HPackEntry, ZtArrayHeapID<"Zhttp.H2.HPack.Entries">>;
using HPackBytes =
  ZtArray<uint8_t, ZtArrayHeapID<"Zhttp.H2.HPack.Bytes">>;
using HPackNameSet = ZmHashKV<
  HPackString, bool,
  ZmHashLock<ZmNoLock,
    ZmHashHeapID<"Zhttp.H2.HPack.NeverIndex">>>;

class HPackTable {
public:
  bool capacity(uint32_t);
  bool insert(ZuBSpan, ZuBSpan);
  bool lookup(uint64_t, Field &) const;
  void reset();

  uint32_t capacity() const { return m_capacity; }
  uint32_t used() const { return m_used; }
  unsigned count() const { return m_entries.length() - m_head; }

private:
  void evict_();
  void compact_();

  HPackEntries	m_entries;
  unsigned	m_head = 0;
  uint32_t	m_capacity = 0;
  uint32_t	m_used = 0;
};

struct HPackTxEntry {
  uint64_t	abs = 0;
  uint32_t	size = 0;
  HPackString	name;
  HPackString	value;
};

struct HPackTxExactEntry {
  uint64_t	abs = 0;
  Field		key;

  static Field KeyAxor(const HPackTxExactEntry &entry) {
    return entry.key;
  }
};

struct HPackTxNameEntry {
  uint64_t		abs = 0;
  Compression::NameView key;

  static Compression::NameView KeyAxor(
      const HPackTxNameEntry &entry) {
    return entry.key;
  }
};

using HPackTxEntries =
  ZtArray<HPackTxEntry, ZtArrayHeapID<"Zhttp.H2.HPack.TxEntries">>;
using HPackTxExact = ZmLHash<HPackTxExactEntry,
  ZmLHashKey<HPackTxExactEntry::KeyAxor, ZmLHashLocal<>>>;
using HPackTxNames = ZmLHash<HPackTxNameEntry,
  ZmLHashKey<HPackTxNameEntry::KeyAxor, ZmLHashLocal<>>>;

class HPackTxTable {
public:
  bool init(uint32_t);
  void final();
  bool capacity(uint32_t);
  const HPackTxEntry *find(Field) const;
  const HPackTxEntry *findName(ZuBSpan) const;
  bool insert(Field);

  uint32_t capacity() const { return m_capacity; }
  uint32_t localCapacity() const { return m_localCapacity; }
  uint32_t used() const { return m_used; }
  uint64_t insertCount() const { return m_insertCount; }
  unsigned count() const { return m_entries.length() - m_head; }
  unsigned orderSlots() const { return m_entries.size(); }
  unsigned exactSlots() const { return m_exact ? m_exact->size() : 0; }
  unsigned nameSlots() const { return m_names ? m_names->size() : 0; }
  unsigned exactResized() const {
    return m_exact ? m_exact->resized() : 0;
  }
  unsigned nameResized() const {
    return m_names ? m_names->resized() : 0;
  }

private:
  const HPackTxEntry *findAbs_(uint64_t) const;
  bool dropOldest_();
  void compact_();
  void rebuild_();

  HPackTxEntries	m_entries;
  ZuPtr<HPackTxExact> m_exact;
  ZuPtr<HPackTxNames> m_names;
  unsigned	m_head = 0;
  uint64_t	m_insertCount = 0;
  uint32_t	m_capacity = 0;
  uint32_t	m_localCapacity = 0;
  uint32_t	m_used = 0;
};

class HPack {
public:
  static bool staticField(uint64_t, Field &);
  static int staticIndex(ZuBSpan, ZuBSpan);
  static int staticNameIndex(ZuBSpan);
};

ZtEnumStruct(ZhttpAPI, HPackRep, uint8_t,
  Indexed, Incremental, NonIndexed, NeverIndexed);

struct HPackPlan {
  Field		field;
  uint64_t	index = 0;
  HPackRep::T	rep = HPackRep::NonIndexed;
};

struct HPackUpdates {
  uint64_t	generation = 0;
  uint32_t	first = 0;
  uint32_t	second = 0;
  uint8_t	count = 0;
};

class HPackDecoder {
public:
  bool init(uint32_t capacity, uint64_t maxHeaderListSize);
  void reset();
  void final();

  template <typename FieldFn>
  int process(ZuBSpan input, FieldFn &&field) {
    if (m_pending)
      return resume(input, ZuFwd<FieldFn>(field));
    unsigned offset = 0, n = input.length();
    while (offset < n) {
      unsigned before = offset;
      Field decoded;
      bool emitted = false;
      int state = decode_(input, offset, decoded, emitted);
      if (state < 0) return -1;
      if (!state) {
	for (unsigned i = before; i < n; ++i)
	  m_pending.push(input[i]);
	return 0;
      }
      if (emitted) field(decoded);
    }
    return 1;
  }

  template <typename FieldFn>
  int resume(ZuBSpan input, FieldFn &&field) {
    if (!m_pending)
      return process(input, ZuFwd<FieldFn>(field));
    for (unsigned i = 0, n = input.length(); i < n; ++i)
      m_pending.push(input[i]);
    while (m_pending) {
      unsigned offset = 0;
      Field decoded;
      bool emitted = false;
      int state = decode_(m_pending, offset, decoded, emitted);
      if (state < 0) return -1;
      if (!state) return 0;
      if (emitted) field(decoded);
      m_pending.shift(offset);
    }
    return 1;
  }

  bool finish();
  HPackFailure::T failure() const { return m_failure; }
  const HPackTable &table() const { return m_table; }

private:
  int decode_(ZuBSpan, unsigned &, Field &, bool &);
  bool indexed_(uint64_t, Field &);
  template <unsigned Bits, bool Indexing>
  int literal_(ZuBSpan, unsigned &, Field &);
  template <unsigned Bits, uint8_t Huffman>
  int string_(ZuBSpan, unsigned &, HPackString &);
  bool account_(Field);
  int fail_(HPackFailure::T);

  HPackTable	m_table;
  HPackBytes	m_pending;
  HPackString	m_name;
  HPackString	m_value;
  uint64_t	m_maxHeaderListSize = 0;
  uint64_t	m_headerListSize = 0;
  uint32_t	m_maxCapacity = 0;
  HPackFailure::T m_failure = HPackFailure::None;
  bool		m_capacityAllowed = true;
};

class HPackEncoder {
public:
  HPackEncoder() : m_neverIndex{new HPackNameSet} { }

  bool init(uint32_t);
  void reset();
  void final();
  bool peerCapacity(uint32_t);
  void neverIndex(ZuBSpan);
  bool neverIndexed(ZuBSpan) const;
  HPackPlan plan(Field) const;
  HPackUpdates updates() const;
  void commit(const HPackPlan &);
  void commit(const HPackUpdates &);

  template <typename Bytes>
  int emit(Bytes &out, const HPackPlan &plan) const {
    switch (plan.rep) {
      case HPackRep::Indexed:
	return Compression::putPref(out, 0x80, 7, plan.index) < 0 ?
	  -1 : int(out.length());
      case HPackRep::Incremental:
      case HPackRep::NonIndexed:
      case HPackRep::NeverIndexed:
	break;
    }
    uint8_t prefix =
      plan.rep == HPackRep::Incremental ? 0x40 :
      plan.rep == HPackRep::NeverIndexed ? 0x10 : 0x00;
    unsigned bits = plan.rep == HPackRep::Incremental ? 6 : 4;
    if (Compression::putPref(out, prefix, bits, plan.index) < 0)
      return -1;
    if (!plan.index &&
	Compression::putString(out, 0, 7, plan.field.name) < 0)
      return -1;
    int n = Compression::putString(out, 0, 7, plan.field.value);
    return n < 0 ? -1 : int(out.length());
  }

  template <typename Bytes>
  int emit(Bytes &out, const HPackUpdates &updates) const {
    for (unsigned i = 0; i < updates.count; ++i)
      if (Compression::putPref(
	  out, 0x20, 5, i ? updates.second : updates.first) < 0)
	return -1;
    return int(out.length());
  }

  template <typename Bytes>
  int field(Bytes &out, Field field) {
    auto plan_ = plan({field.name, field.value});
    if (emit(out, plan_) < 0) return -1;
    commit(plan_);
    return int(out.length());
  }

  const HPackTxTable &table() const { return m_table; }

private:
  void detachNeverIndex_();
  void pending_(uint32_t);

  HPackTxTable	m_table;
  ZmRef<HPackNameSet>	m_neverIndex;
  uint64_t	m_generation = 0;
  uint32_t	m_localCapacity = 0;
  uint32_t	m_peerCapacity = 4096;
  uint32_t	m_signalledCapacity = 4096;
  uint32_t	m_pendingMin = 0;
  uint32_t	m_pendingFinal = 0;
  bool		m_pending = false;
};

} // namespace H2

} // namespace Zhttp

#endif /* ZhttpHPack_HH */
