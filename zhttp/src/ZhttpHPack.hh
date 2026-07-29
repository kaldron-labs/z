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

#include <zlib/ZmHash.hh>
#include <zlib/ZmRef.hh>

#include <zlib/ZtArray.hh>
#include <zlib/ZtEnum.hh>
#include <zlib/ZtString.hh>

#include <zlib/ZhttpCompression.hh>
#include <zlib/ZhttpStaticTable.hh>

namespace Zhttp {

namespace H2 {

struct Field {
  ZuCSpan	name;
  ZuCSpan	value;
};

#define Zhttp_HPack(Key, Value) \
  ZuTypeList<ZuStringT<Key>, ZuStringT<Value>>
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

using HPackStatic = StaticTable<HPackTbl>;

ZtEnumStruct(HPackFailure, uint8_t,
  None, Truncated, Integer, String, Index, Capacity, HeaderList);

using HPackString =
  ZtString<ZtStringHeapID<"Zhttp.H2.HPack.String">>;

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
  bool insert(ZuCSpan, ZuCSpan);
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

class HPack {
public:
  static bool staticField(uint64_t, Field &);
  static int staticIndex(ZuCSpan, ZuCSpan);
  static int staticNameIndex(ZuCSpan);
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
    unsigned offset = 0;
    while (offset < input.length()) {
      unsigned before = offset;
      Field decoded;
      bool emitted = false;
      int state = decode_(input, offset, decoded, emitted);
      if (state < 0) return -1;
      if (!state) {
	for (unsigned i = before; i < input.length(); ++i)
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
    for (unsigned i = 0; i < input.length(); ++i)
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
  int decode_(ZuCSpan, unsigned &, Field &, bool &);
  bool indexed_(uint64_t, Field &);
  int literal_(ZuCSpan, unsigned &, unsigned, bool, Field &);
  int string_(ZuCSpan, unsigned &, unsigned, uint8_t, HPackString &);
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

  bool init(uint32_t capacity);
  void reset();
  void final();
  void neverIndex(ZuCSpan);
  bool neverIndexed(ZuCSpan) const;
  template <typename Bytes>
  int field(Bytes &out, Field field) const {
    int index = HPack::staticIndex(field.name, field.value);
    if (index > 0)
      return Compression::putPref(out, 0x80, 7, unsigned(index)) < 0 ?
	-1 : int(out.length());
    int nameIndex = HPack::staticNameIndex(field.name);
    uint8_t prefix = neverIndexed(field.name) ? 0x10 : 0x00;
    if (nameIndex > 0) {
      if (Compression::putPref(out, prefix, 4, unsigned(nameIndex)) < 0)
	return -1;
    } else {
      if (Compression::putPref(out, prefix, 4, 0) < 0 ||
	  Compression::putString(out, 0, 7, field.name) < 0)
	return -1;
    }
    return Compression::putString(out, 0, 7, field.value) < 0 ?
      -1 : int(out.length());
  }
  template <typename Bytes>
  int field(
    Bytes &out, ZuCSpan name,
    ZuCSpan value1, char separator, ZuCSpan value2) const {
    int nameIndex = HPack::staticNameIndex(name);
    uint8_t prefix = neverIndexed(name) ? 0x10 : 0x00;
    if (nameIndex > 0) {
      if (Compression::putPref(out, prefix, 4, unsigned(nameIndex)) < 0)
	return -1;
    } else {
      if (Compression::putPref(out, prefix, 4, 0) < 0 ||
	  Compression::putString(out, 0, 7, name) < 0)
	return -1;
    }
    return Compression::putString(
      out, 0, 7, value1, separator, value2) < 0 ?
	-1 : int(out.length());
  }
  template <typename Bytes>
  int capacity(Bytes &out, uint32_t value) {
    if (value > m_maxCapacity || !m_table.capacity(value)) return -1;
    return Compression::putPref(out, 0x20, 5, value) < 0 ?
      -1 : int(out.length());
  }

  const HPackTable &table() const { return m_table; }

private:
  void detachNeverIndex_();

  HPackTable	m_table;
  ZmRef<HPackNameSet>	m_neverIndex;
  uint32_t	m_maxCapacity = 0;
};

} // namespace H2

} // namespace Zhttp

#endif /* ZhttpHPack_HH */
