//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z HTTP/3 QPACK support

#ifndef ZhttpQPack_HH
#define ZhttpQPack_HH

#ifndef ZhttpLib_HH
#include <zlib/ZhttpLib.hh>
#endif

#include <zlib/ZuHash.hh>
#include <zlib/ZuString.hh>

#include <zlib/ZmLHash.hh>

#include <zlib/ZtEnum.hh>
#include <zlib/ZtString.hh>
#include <zlib/ZtArray.hh>

#include <zlib/ZiAssert.hh>
#include <zlib/ZtLocalArray.hh>

#include <zlib/ZhttpHPack.hh>

namespace Zhttp { namespace H3 {

using HdrBytes = ZtArray<uint8_t, ZtArrayHeapID<"Zhttp.H3.HdrBytes">>;

ZtEnumStruct(QPackInsn, int8_t,
  InsertWithNameRef, InsertWithoutNameRef, Duplicate, SetCapacity,
  SectionAck, StreamCancellation, InsertCountIncrement);

struct Header {
  ZuCSpan	name;
  ZuCSpan	value;
};

struct QPackFieldFlags {
  bool	staticRef = false;
  bool	dynamicRef = false;
  bool	postBase = false;
  bool	neverIndex = false;
};

ZtEnumStruct(QPackBuildFailure, uint8_t,
  None, Plan, PrefixEncode, CapacityPolicy, EncoderCapacityWrite,
  EncoderInsertWrite, HeadersFrameHeaderWrite, HeadersPayloadEmit,
  Flush, CapacityCommit, InsertCommit, SectionTracking);

struct QPackDecodedInsn {
  QPackInsn::T	type = QPackInsn::SetCapacity;
  uint64_t		value = 0;
  Header		header;
  HdrBytes		nameStorage;
  HdrBytes		valueStorage;
  bool			nameRefDynamic = false;
};

struct FieldSectionPrefix {
  uint64_t	requiredInsertCount = 0;
  uint64_t	base = 0;
};

struct EncodedFieldSectionPrefix {
  uint64_t	encodedInsertCount = 0;
  uint64_t	deltaBase = 0;
  bool		baseNegative = false;
};

using Headers = ZtArray<Header, ZtArrayHeapID<"Zhttp.H3.Headers">>;
using HeaderName = ZtString<ZtStringHeapID<"Zhttp.H3.HeaderName">>;
using QPackNameList =
  ZtArray<HeaderName, ZtArrayHeapID<"Zhttp.H3.Params.Names">>;

struct Params {
  Params &&maxHeaderListSize(unsigned v) {
    maxHeaderListSize_ = v;
    return ZuMv(*this);
  }
  Params &&qpackTableCapacity(unsigned v) {
    qpackTableCapacity_ = v;
    return ZuMv(*this);
  }
  Params &&qpackBlockedStreams(unsigned v) {
    qpackBlockedStreams_ = v;
    return ZuMv(*this);
  }
  Params &&qpackIndex(ZuCSpan name) {
    new (index_.push()) HeaderName{headerName_(name)};
    return ZuMv(*this);
  }
  Params &&qpackNeverIndex(ZuCSpan name) {
    new (neverIndex_.push()) HeaderName{headerName_(name)};
    return ZuMv(*this);
  }

  bool indexAllowed(ZuCSpan name) const {
    for (unsigned i = 0; i < neverIndex_.length(); ++i)
      if (neverIndex_[i] == name) return false;
    for (unsigned i = 0; i < index_.length(); ++i)
      if (index_[i] == name) return true;
    return false;
  }
  bool neverIndex(ZuCSpan name) const {
    for (unsigned i = 0; i < neverIndex_.length(); ++i)
      if (neverIndex_[i] == name) return true;
    return name == "authorization" || name == "cookie" ||
      name == "set-cookie";
  }

  unsigned maxHeaderListSize() const { return maxHeaderListSize_; }
  unsigned qpackTableCapacity() const { return qpackTableCapacity_; }
  unsigned qpackBlockedStreams() const { return qpackBlockedStreams_; }

private:
  static ZuCSpan headerName_(ZuCSpan name) {
    for (unsigned i = 0; i < name.length(); ++i)
      if (!name[i]) {
	name = ZuCSpan{name.data(), i};
	break;
      }
    return name;
  }

  unsigned	maxHeaderListSize_ = 1<<16;
  unsigned	qpackTableCapacity_ = 0;
  unsigned	qpackBlockedStreams_ = 0;
  QPackNameList	index_;
  QPackNameList	neverIndex_;
};

using QPackRxString =
  ZtString<ZtStringHeapID<"Zhttp.H3.QPackRx.String">>;

struct QPackRxEntry {
  uint64_t	abs = 0;
  uint32_t	size = 0;
  QPackRxString	name;
  QPackRxString	value;
};

using QPackRxArray =
  ZtArray<QPackRxEntry, ZtArrayHeapID<"Zhttp.H3.QPackRx.Array">>;

struct QPackRxTable {
  // Connection-affine Rx state. Callers must serialize access from the owning
  // receive path; table storage is deliberately unsynchronized.
  bool setCapacity(uint32_t);
  bool insert(Header);
  bool insert(ZuCSpan, ZuCSpan);
  bool duplicate(uint64_t);
  bool lookupAbs(uint64_t, Header &) const;
  bool lookupRelative(uint64_t, uint64_t, Header &) const;
  bool lookupPostBase(uint64_t, uint64_t, Header &) const;
  QPackRxEntry *pushNewest();
  void dropOldest();
  const QPackRxEntry *oldest() const;
  void compact();
  uint64_t insertCount() const { return insertCount_; }
  uint64_t baseAbs() const { return baseAbs_; }
  uint32_t capacity() const { return capacityBytes_; }
  uint32_t maxCapacity() const { return maxCapacityBytes_; }
  uint32_t used() const { return usedBytes_; }
  uint32_t count() const { return entries.length() - head_; }
  uint32_t maxEntries() const { return maxCapacityBytes_>>5; }

  QPackRxArray	entries;	// oldest-to-newest
  uint32_t	head_ = 0;
  uint64_t	baseAbs_ = 0;
  uint64_t	insertCount_ = 0;
  uint32_t	capacityBytes_ = 0;
  uint32_t	maxCapacityBytes_ = 0;
  uint32_t	usedBytes_ = 0;
};

static inline const char *QPackTxHashID() { return "Zhttp.H3.QPackTx"; }

using QPackTxString =
  ZtString<ZtStringHeapID<"Zhttp.H3.QPackTx.String">>;

struct QPackFieldKey {
  ZuCSpan	name;
  ZuCSpan	value;

  bool equals(const QPackFieldKey &k) const {
    return name == k.name && value == k.value;
  }
  int cmp(const QPackFieldKey &k) const {
    if (int i = name.cmp(k.name)) return i;
    return value.cmp(k.value);
  }
  friend bool operator ==(const QPackFieldKey &l, const QPackFieldKey &r) {
    return l.equals(r);
  }
  friend int operator <=>(const QPackFieldKey &l, const QPackFieldKey &r) {
    return l.cmp(r);
  }
  uint32_t hash() const {
    uint32_t h = ZuHash<ZuCSpan>::hash(name);
    return h ^ (ZuHash<ZuCSpan>::hash(value) + 0x9e3779b9U +
      (h<<6) + (h>>2));
  }
};

struct QPackTxEntry {
  uint64_t	abs = 0;
  uint32_t	size = 0;
  uint64_t	refcnt = 0;
  QPackTxString	name;
  QPackTxString	value;
};

struct QPackTxHashEntry {
  uint32_t	index = 0;
  QPackFieldKey	key;

  static QPackFieldKey FieldAxor(const QPackTxHashEntry &entry) {
    return entry.key;
  }
};

using QPackTxHash = ZmLHash<QPackTxHashEntry,
  ZmLHashKey<QPackTxHashEntry::FieldAxor,
    ZmLHashID<QPackTxHashID>>>;

struct QPackTxOrderEntry : public QPackTxEntry {
  static QPackFieldKey FieldAxor(const QPackTxOrderEntry &entry) {
    return {entry.name, entry.value};
  }
};

using QPackTxOrder =
  ZtArray<QPackTxOrderEntry, ZtArrayHeapID<"Zhttp.H3.QPackTx.Order">>;

using QPackTxRefs =
  ZtArray<uint64_t, ZtArrayHeapID<"Zhttp.H3.QPackTx.Refs">>;

struct QPackTxSection {
  uint64_t	streamID = 0;
  QPackTxRefs	refs;

  static uint64_t StreamAxor(const QPackTxSection &section) {
    return section.streamID;
  }
};

static inline const char *QPackTxSectionsID() {
  return "Zhttp.H3.QPackTx.Sections";
}

using QPackTxSections =
  ZmLHash<QPackTxSection,
    ZmLHashKey<QPackTxSection::StreamAxor,
      ZmLHashID<QPackTxSectionsID>>>;

struct QPackTxTable {
  // Connection-affine Tx state. Callers must serialize access from the owning
  // transmit path.
  QPackTxTable() :
    hash{new QPackTxHash{ZmHashParams{128}}},
    sections{new QPackTxSections{ZmHashParams{64}}} { }

  bool setCapacity(uint32_t);
  bool setMaxCapacity(uint32_t);
  const QPackTxEntry *find(ZuCSpan, ZuCSpan) const;
  const QPackTxEntry *findAbs(uint64_t) const;
  bool insert(Header, uint64_t * = nullptr);
  bool insert(QPackTxString, QPackTxString, uint64_t * = nullptr);
  bool lookupAbs(uint64_t, Header &) const;
  bool evict();
  bool dropOldest();
  bool insertCountIncrement(uint64_t);
  bool sectionAck(uint64_t);
  bool streamCancellation(uint64_t);
  bool trackSection(uint64_t, ZuSpan<uint64_t>);
  bool trackSection(uint64_t, QPackTxRefs);
  void compactOrder();
  void rebuildHash();

  uint64_t insertCount() const { return insertCount_; }
  uint64_t knownReceivedCount() const { return knownReceivedCount_; }
  uint32_t capacity() const { return capacityBytes_; }
  uint32_t maxCapacity() const { return maxCapacityBytes_; }
  uint32_t used() const { return usedBytes_; }

  ZmRef<QPackTxHash> hash;
  QPackTxOrder	order;
  ZmRef<QPackTxSections> sections;
  uint32_t	orderHead_ = 0;
  uint64_t	insertCount_ = 0;
  uint64_t	knownReceivedCount_ = 0;
  uint32_t	capacityBytes_ = 0;
  uint32_t	maxCapacityBytes_ = 0;
  uint32_t	usedBytes_ = 0;
  bool		capacitySent = false;
};

inline int qpackDecodePrefInt_(
  ZuCSpan in, unsigned &o, unsigned prefixBits, uint64_t &v,
  uint8_t *firstByte = nullptr)
{
  if (!prefixBits || prefixBits > 8) return -1;
  if (o >= in.length()) return -2;
  uint8_t mask = uint8_t((1U << prefixBits) - 1U);
  uint8_t first = uint8_t(in[o++]);
  if (firstByte) *firstByte = first;
  v = first & mask;
  if (v < mask) return 0;

  unsigned shift = 0;
  for (;;) {
    if (o >= in.length()) return -2;
    if (shift >= 56) return -1;
    uint8_t b = uint8_t(in[o++]);
    v += uint64_t(b & 0x7f) << shift;
    if (!(b & 0x80)) return 0;
    shift += 7;
  }
}

struct CodecBytes {
  static void putSpan(HdrBytes &out, ZuCSpan s) {
    for (unsigned i = 0; i < s.length(); ++i) out.push(uint8_t(s[i]));
  }
};

struct QPack {
  static int staticIndex(ZuCSpan name, ZuCSpan value);
  static bool staticField(uint64_t, Header &);
  static bool staticName(uint64_t, HeaderName &);
  static bool staticNameIndex(ZuCSpan, uint64_t &);
  static int encodeFieldSectionPrefix(
    HdrBytes &, const FieldSectionPrefix &, uint64_t = 0);
  static int decodeFieldSectionPrefix(
    ZuCSpan, FieldSectionPrefix &, uint64_t = 0, uint64_t = 0);
  static int decodeFieldSectionPrefix(
    ZuCSpan, EncodedFieldSectionPrefix &);
  static bool fieldSectionBase(
    const EncodedFieldSectionPrefix &, uint64_t, uint64_t &);
  static bool validateFieldSectionPrefix(
    const FieldSectionPrefix &, uint64_t);
  static int encodeFieldLine(HdrBytes &, Header, const Params &);
  static int encodeLiteral(
    HdrBytes &, ZuSpan<Header>, const Params &,
    const FieldSectionPrefix & = {});
  static int encodeSetCapacity(HdrBytes &, uint64_t);
  static int encodeInsertWithNameRef(
    HdrBytes &, uint64_t, bool, ZuCSpan);
  static int encodeInsertLiteral(HdrBytes &, Header);
  static int encodeSectionAck(HdrBytes &, uint64_t);
  static int decodeEncoderInsnOne(ZuCSpan, QPackDecodedInsn &);
  static int decodeDecoderInsnOne(ZuCSpan, QPackDecodedInsn &);
  static int decodeHuffman(HdrBytes &, ZuCSpan);
  static int decodeString(
    HdrBytes &, ZuCSpan, unsigned &, unsigned, uint8_t, ZuCSpan &);

  template <typename L>
  static int decodeFieldSection(
    ZuCSpan in, const QPackRxTable *table, L l,
    const Params &params = {}, uint64_t insertCount = 0,
    uint64_t maxCapacity = 0, FieldSectionPrefix *decodedPrefix = nullptr) {
    if (table) {
      insertCount = table->insertCount();
      maxCapacity = table->maxCapacity();
    }
    FieldSectionPrefix prefix;
    int prefixLen = decodeFieldSectionPrefix(
      in, prefix, insertCount, maxCapacity);
    if (prefixLen < 0 || !validateFieldSectionPrefix(prefix, insertCount))
      return -1;
    if (decodedPrefix) *decodedPrefix = prefix;

    unsigned o = unsigned(prefixLen);
    uint64_t base = prefix.base;
    uint64_t headerBytes = 0;
    // Non-Huffman strings are returned as spans into the input section.
    // Huffman strings use these per-section scratch buffers, reused for each
    // field rather than allocated inside the representation loop.
    auto nameStorage = ZtLocalArray(HdrBytes, HPack::declen(in.length()));
    auto valueStorage = ZtLocalArray(HdrBytes, HPack::declen(in.length()));

    auto countHeader = [&headerBytes, &params](ZuCSpan name, ZuCSpan value) {
      if (headerBytes > params.maxHeaderListSize() - name.length())
	return false;
      headerBytes += name.length();
      if (headerBytes > params.maxHeaderListSize() - value.length())
	return false;
      headerBytes += value.length();
      return true;
    };
    auto readValue = [&valueStorage, &in, &o](ZuCSpan &value) {
      valueStorage.length(0);
      return decodeString(valueStorage, in, o, 7, 0x80, value) >= 0;
    };

    while (o < in.length()) {
      uint8_t first = uint8_t(in[o]);
      ZuCSpan name;
      ZuCSpan value;
      Header indexed;
      HeaderName indexedNameStorage;
      QPackFieldFlags flags;

      if (first & 0x80) {
	uint64_t index = 0;
	uint8_t indexFirst = 0;
	if (qpackDecodePrefInt_(in, o, 6, index, &indexFirst) < 0)
	  return -1;
	if (indexFirst & 0x40) {
	  if (!staticField(index, indexed)) return -1;
	  flags.staticRef = true;
	} else {
	  if (!table || !table->lookupRelative(base, index, indexed))
	    return -1;
	  flags.dynamicRef = true;
	}
	name = indexed.name;
	value = indexed.value;
      } else if ((first & 0xf0) == 0x10) {
	uint64_t index = 0;
	if (qpackDecodePrefInt_(in, o, 4, index) < 0 ||
	    !table || !table->lookupPostBase(base, index, indexed))
	  return -1;
	name = indexed.name;
	value = indexed.value;
	flags.dynamicRef = true;
	flags.postBase = true;
      } else if ((first & 0xc0) == 0x40) {
	uint64_t index = 0;
	uint8_t nameFirst = 0;
	if (qpackDecodePrefInt_(in, o, 4, index, &nameFirst) < 0)
	  return -1;
	flags.neverIndex = nameFirst & 0x20;
	if (nameFirst & 0x10) {
	  if (!staticName(index, indexedNameStorage)) return -1;
	  name = indexedNameStorage;
	  flags.staticRef = true;
	} else {
	  if (!table || !table->lookupRelative(base, index, indexed))
	    return -1;
	  name = indexed.name;
	  flags.dynamicRef = true;
	}
	if (!readValue(value)) return -1;
      } else if ((first & 0xf0) == 0x00) {
	uint64_t index = 0;
	if (qpackDecodePrefInt_(in, o, 3, index) < 0 ||
	    !table || !table->lookupPostBase(base, index, indexed))
	  return -1;
	flags.neverIndex = first & 0x08;
	flags.dynamicRef = true;
	flags.postBase = true;
	name = indexed.name;
	if (!readValue(value)) return -1;
      } else if ((first & 0xe0) == 0x20) {
	nameStorage.length(0);
	valueStorage.length(0);
	if (decodeString(nameStorage, in, o, 3, 0x08, name) < 0 ||
	    decodeString(valueStorage, in, o, 7, 0x80, value) < 0)
	  return -1;
	flags.neverIndex = first & 0x10;
      } else
	return -1;

      if (!countHeader(name, value)) return -1;
      l(Header{name, value}, flags);
    }
    return int(o);
  }

  template <typename L>
  static int decodeLiteral(
    ZuCSpan in, L l, const Params &params = {}, uint64_t insertCount = 0) {
    return decodeFieldSection(
      in, nullptr,
      [&l](Header h, QPackFieldFlags) { l(h); },
      params, insertCount, params.qpackTableCapacity());
  }
};

struct QPackInsnParser {
  template <typename Decode, typename Apply>
  bool parse(ZuBSpan span, Decode decode, Apply apply) {
    if (bytes.length() != offset) {
      for (unsigned i = 0; i < span.length(); ++i) bytes.push(span[i]);
      return drain_(decode, apply);
    }

    unsigned o = 0;
    while (o < span.length()) {
      QPackDecodedInsn insn;
      int n = decode(ZuCSpan{
	reinterpret_cast<const char *>(span.data() + o), span.length() - o},
	insn);
      if (n == -2) {
	for (unsigned i = o; i < span.length(); ++i) bytes.push(span[i]);
	offset = 0;
	return bytes.length() <= MaxBuffered;
      }
      if (n < 0 || !apply(insn)) return false;
      o += unsigned(n);
    }
    return true;
  }

  void reset() {
    bytes.length(0);
    offset = 0;
  }

private:
  template <typename Decode, typename Apply>
  bool drain_(Decode decode, Apply apply) {
    for (;;) {
      QPackDecodedInsn insn;
      int n = decode(ZuCSpan{
	reinterpret_cast<const char *>(bytes.data() + offset),
	bytes.length() - offset}, insn);
      if (n == -2) {
	if (offset > 4096 && offset > (bytes.length()>>1)) {
	  bytes.splice(0, offset);
	  offset = 0;
	}
	return bytes.length() - offset <= MaxBuffered;
      }
      if (n < 0 || !apply(insn)) return false;
      offset += unsigned(n);
      if (offset == bytes.length()) {
	reset();
	return true;
      }
    }
  }

  HdrBytes	bytes;
  unsigned	offset = 0;
  static constexpr unsigned MaxBuffered = 1<<16;
};

template <typename L>
int decodeLiteralDynamic(
  ZuCSpan in, const QPackRxTable &table, L l,
  const Params &params = {})
{
  return QPack::decodeFieldSection(
    in, &table,
    [&l](Header h, QPackFieldFlags) { l(h); },
    params);
}

// QPack static table compile-time lookup definition
#define Zhttp_QPack_1(Key) \
  ZuTypeList<ZuStringT<Key>, void>
#define Zhttp_QPack_2(Key, Value) \
  ZuTypeList<ZuStringT<Key>, ZuStringT<Value>>
#define Zhttp_QPack_N(_0, _1, Fn, ...) Fn
#define Zhttp_QPack_(...) \
  Zhttp_QPack_N(__VA_ARGS__, \
    Zhttp_QPack_2, \
    Zhttp_QPack_1)(__VA_ARGS__)
#define Zhttp_QPack(KV) \
  ZuPP_Defer(Zhttp_QPack_)(ZuPP_Strip(KV))
#define ZhttpQPackTbl(...) \
  ZuTypeList<ZuPP_Eval_(ZuPP_MapComma(Zhttp_QPack,  __VA_ARGS__))>

// QPACK static table (compile-time)
using QPackTbl = ZhttpQPackTbl(
  (":authority"),
  (":path", "/"),
  ("age", "0"),
  ("content-disposition"),
  ("content-length", "0"),
  ("cookie"),
  ("date"),
  ("etag"),
  ("if-modified-since"),
  ("if-none-match"),
  ("last-modified"),
  ("link"),
  ("location"),
  ("referer"),
  ("set-cookie"),
  (":method", "CONNECT"),
  (":method", "DELETE"),
  (":method", "GET"),
  (":method", "HEAD"),
  (":method", "OPTIONS"),
  (":method", "POST"),
  (":method", "PUT"),
  (":scheme", "http"),
  (":scheme", "https"),
  (":status", "103"),
  (":status", "200"),
  (":status", "304"),
  (":status", "404"),
  (":status", "503"),
  ("accept", "*/*"),
  ("accept", "application/dns-message"),
  ("accept-encoding", "gzip, deflate, br"),
  ("accept-ranges", "bytes"),
  ("access-control-allow-headers", "cache-control"),
  ("access-control-allow-headers", "content-type"),
  ("access-control-allow-origin", "*"),
  ("cache-control", "max-age=0"),
  ("cache-control", "max-age=2592000"),
  ("cache-control", "max-age=604800"),
  ("cache-control", "no-cache"),
  ("cache-control", "no-store"),
  ("cache-control", "public, max-age=31536000"),
  ("content-encoding", "br"),
  ("content-encoding", "gzip"),
  ("content-type", "application/dns-message"),
  ("content-type", "application/javascript"),
  ("content-type", "application/json"),
  ("content-type", "application/x-www-form-urlencoded"),
  ("content-type", "image/gif"),
  ("content-type", "image/jpeg"),
  ("content-type", "image/png"),
  ("content-type", "text/css"),
  ("content-type", "text/html; charset=utf-8"),
  ("content-type", "text/plain"),
  ("content-type", "text/plain;charset=utf-8"),
  ("range", "bytes=0-"),
  ("strict-transport-security", "max-age=31536000"),
  ("strict-transport-security", "max-age=31536000; includesubdomains"),
  ("strict-transport-security", "max-age=31536000; includesubdomains; preload"),
  ("vary", "accept-encoding"),
  ("vary", "origin"),
  ("x-content-type-options", "nosniff"),
  ("x-xss-protection", "1; mode=block"),
  (":status", "100"),
  (":status", "204"),
  (":status", "206"),
  (":status", "302"),
  (":status", "400"),
  (":status", "403"),
  (":status", "421"),
  (":status", "425"),
  (":status", "500"),
  ("accept-language"),
  ("access-control-allow-credentials", "FALSE"),
  ("access-control-allow-credentials", "TRUE"),
  ("access-control-allow-headers", "*"),
  ("access-control-allow-methods", "get"),
  ("access-control-allow-methods", "get, post, options"),
  ("access-control-allow-methods", "options"),
  ("access-control-expose-headers", "content-length"),
  ("access-control-request-headers", "content-type"),
  ("access-control-request-method", "get"),
  ("access-control-request-method", "post"),
  ("alt-svc", "clear"),
  ("authorization"),
  ("content-security-policy", "script-src 'none'; object-src 'none'; base-uri 'none'"),
  ("early-data", "1"),
  ("expect-ct"),
  ("forwarded"),
  ("if-range"),
  ("origin"),
  ("purpose", "prefetch"),
  ("server"),
  ("timing-allow-origin", "*"),
  ("upgrade-insecure-requests", "1"),
  ("user-agent"),
  ("x-forwarded-for"),
  ("x-frame-options", "deny"),
  ("x-frame-options", "sameorigin"));

// evaluates QPACK static table index I given <Key, Value>
// - use <Key, void> for entries which are Key only
// - evaluates to -1 if <Key, Value> are not in table
template <typename Key, typename Value,
  bool = ZuTypeIn<ZuTypeList<Key, Value>, QPackTbl>{}>
struct QPackIndex_ {
  using T = ZuInt<-1>;
};
template <typename Key, typename Value>
struct QPackIndex_<Key, Value, true> {
  using T = ZuTypeIndex<ZuTypeList<Key, Value>, QPackTbl>;
};
template <typename Key, typename Value>
using QPackIndex = typename QPackIndex_<Key, Value>::T;

template <ZuString Key, ZuString Value>
using QPackKVIndex = QPackIndex<ZuStringT<Key>, ZuStringT<Value>>;
template <typename KV>
using QPackKey = ZuType<0, KV>;
using QPackKeys = ZuTypeMap<QPackKey, QPackTbl>;
template <typename Key, bool = ZuTypeIn<Key, QPackKeys>{}>
struct QPackKeyIndex_ {
  using T = ZuInt<-1>;
};
template <typename Key>
struct QPackKeyIndex_<Key, true> {
  using T = ZuTypeIndex<Key, QPackKeys>;
};
template <ZuString Key>
using QPackKeyIndex = typename QPackKeyIndex_<ZuStringT<Key>>::T;

// evaluates QPACK static table key, value given an index I
// - undefined if I is out of range
template <unsigned I>
using QPackKV = ZuType<I, QPackTbl>;
template <typename KV, bool = (KV::N > 1)>
struct QPackValue_ { using T = void; };
template <typename KV>
struct QPackValue_<KV, true> { using T = ZuType<1, KV>; };
template <typename KV>
using QPackValue = typename QPackValue_<KV>::T;

}} // namespace Zhttp::H3

#endif /* ZhttpQPack_HH */
