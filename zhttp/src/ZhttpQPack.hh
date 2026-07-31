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
#include <zlib/ZuPtr.hh>
#include <zlib/ZuString.hh>

#include <zlib/ZmAssert.hh>
#include <zlib/ZmHash.hh>
#include <zlib/ZmLHash.hh>

#include <zlib/ZtArray.hh>
#include <zlib/ZtEnum.hh>
#include <zlib/ZtScratch.hh>
#include <zlib/ZtString.hh>

#include <zlib/ZiAssert.hh>

#include <zlib/ZhttpCompression.hh>
#include <zlib/ZhttpStaticTable.hh>

namespace Zhttp {

namespace H3 {

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
  Flush, CapacityCommit, InsertCommit);

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
using QPackNameSet = ZmHashKV<
  HeaderName, bool,
  ZmHashLock<ZmNoLock,
    ZmHashHeapID<"Zhttp.H3.Params.Names">>>;

struct QPackLimits {
  uint32_t	rxCapacity = 0;
  uint32_t	txCapacity = 0;
  uint32_t	rxBlocked = 0;
  uint32_t	txSections = 0;
};

class Params {
public:
  Params() :
    index_{new QPackNameSet},
    neverIndex_{new QPackNameSet} { }

  Params &&maxHeaderListSize(unsigned v) {
    maxHeaderListSize_ = v;
    return ZuMv(*this);
  }
  Params &&qpackRxCapacity(unsigned v) {
    qpackRxCapacity_ = v;
    return ZuMv(*this);
  }
  Params &&qpackTxCapacity(unsigned v) {
    qpackTxCapacity_ = v;
    return ZuMv(*this);
  }
  Params &&qpackRxBlocked(unsigned v) {
    qpackRxBlocked_ = v;
    return ZuMv(*this);
  }
  Params &&qpackTxSections(unsigned v) {
    qpackTxSections_ = v;
    return ZuMv(*this);
  }
  Params &&qpackLimits(QPackLimits v) {
    qpackRxCapacity_ = v.rxCapacity;
    qpackTxCapacity_ = v.txCapacity;
    qpackRxBlocked_ = v.rxBlocked;
    qpackTxSections_ = v.txSections;
    return ZuMv(*this);
  }
  Params &&qpackIndex(ZuCSpan name) {
    name = headerName_(name);
    if (!index_->find(name)) {
      detach_(index_);
      index_->add(HeaderName{name}, true);
    }
    return ZuMv(*this);
  }
  Params &&qpackNeverIndex(ZuCSpan name) {
    name = headerName_(name);
    if (!neverIndex_->find(name)) {
      detach_(neverIndex_);
      neverIndex_->add(HeaderName{name}, true);
    }
    return ZuMv(*this);
  }

  bool indexAllowed(ZuCSpan name) const {
    return !neverIndex_->find(name) && index_->find(name);
  }
  bool neverIndex(ZuCSpan name) const {
    if (neverIndex_->find(name)) return true;
    static constexpr auto matcher =
      ZuMatcher<"authorization", "cookie", "set-cookie">();
    return matcher.exact(name) >= 0;
  }

  unsigned maxHeaderListSize() const { return maxHeaderListSize_; }
  unsigned qpackRxCapacity() const { return qpackRxCapacity_; }
  unsigned qpackTxCapacity() const { return qpackTxCapacity_; }
  unsigned qpackRxBlocked() const { return qpackRxBlocked_; }
  unsigned qpackTxSections() const { return qpackTxSections_; }
  QPackLimits qpackLimits() const {
    return {
      qpackRxCapacity_, qpackTxCapacity_,
      qpackRxBlocked_, qpackTxSections_
    };
  }

private:
  static void detach_(ZmRef<QPackNameSet> &set) {
    if (set->refCount() <= 1) return;
    ZmRef<QPackNameSet> copy{new QPackNameSet};
    auto i = set->iter();
    while (auto entry = i()) copy->add(entry->key(), true);
    set = ZuMv(copy);
  }

  static ZuCSpan headerName_(ZuCSpan name) {
    for (unsigned i = 0; i < name.length(); ++i)
      if (!name[i]) {
	name = ZuCSpan{name.data(), i};
	break;
      }
    return name;
  }

  ZmRef<QPackNameSet>	index_;
  ZmRef<QPackNameSet>	neverIndex_;
  unsigned		maxHeaderListSize_ = 1<<16;
  unsigned		qpackRxCapacity_ = 0;
  unsigned		qpackTxCapacity_ = 0;
  unsigned		qpackRxBlocked_ = 0;
  unsigned		qpackTxSections_ = 0;
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
  QPackRxArray	entries;	// oldest-to-newest
  uint32_t	head_ = 0;
  uint64_t	baseAbs_ = 0;
  uint64_t	insertCount_ = 0;
  uint32_t	capacityBytes_ = 0;
  uint32_t	maxCapacityBytes_ = 0;
  uint32_t	usedBytes_ = 0;

  // Connection-affine Rx state. Callers must serialize access from the owning
  // receive path; table storage is deliberately unsynchronized.
  bool init(uint32_t);
  void final();
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
  uint32_t slots() const { return entries.size(); }
};

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
  uint64_t	prevName = uint64_t(-1);
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
    ZmLHashLocal<>>>;

struct QPackTxNameEntry {
  uint64_t		abs = 0;
  Compression::NameView key;

  static Compression::NameView KeyAxor(
      const QPackTxNameEntry &entry) {
    return entry.key;
  }
};

using QPackTxNames = ZmLHash<QPackTxNameEntry,
  ZmLHashKey<QPackTxNameEntry::KeyAxor, ZmLHashLocal<>>>;

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

using QPackTxSections =
  ZmLHash<QPackTxSection,
    ZmLHashKey<QPackTxSection::StreamAxor,
      ZmLHashLocal<>>>;

struct QPackTxTable {
  ZuPtr<QPackTxHash>	exact;
  ZuPtr<QPackTxNames>	names;
  QPackTxOrder		order;
  ZuPtr<QPackTxSections> sections;
  uint32_t		orderHead_ = 0;
  uint64_t		insertCount_ = 0;
  uint64_t		knownReceivedCount_ = 0;
  uint32_t		capacityBytes_ = 0;
  uint32_t		localCapacityBytes_ = 0;
  uint32_t		peerCapacityBytes_ = 0;
  uint32_t		peerBlocked_ = 0;
  uint32_t		maxSections_ = 0;
  uint32_t		sectionCount_ = 0;
  uint32_t		usedBytes_ = 0;
  bool			capacitySent = false;

  // Connection-affine Tx state. Callers must serialize access from the owning
  // transmit path.
  bool init(uint32_t, uint32_t);
  void final();
  bool setCapacity(uint32_t);
  bool peerCapacity(uint32_t);
  void peerBlocked(uint32_t v) { peerBlocked_ = v; }
  const QPackTxEntry *find(ZuCSpan, ZuCSpan) const;
  const QPackTxEntry *findName(ZuCSpan) const;
  const QPackTxEntry *findName(ZuCSpan, uint64_t) const;
  const QPackTxEntry *findAbs(uint64_t) const;
  bool insert(Header, uint64_t * = nullptr);
  bool insert(QPackTxString, QPackTxString, uint64_t * = nullptr);
  template <typename Inserts>
  bool canCommit(uint32_t capacity, const Inserts &inserts) const {
    if (capacity > effectiveCapacity()) return false;
    uint32_t used = usedBytes_;
    uint32_t head = orderHead_;
    auto makeRoom = [&](uint32_t n) {
      while (head < order.length() && used + n > capacity) {
	if (order[head].refcnt) return false;
	used -= order[head++].size;
      }
      if (used + n > capacity) used = capacity - n;
      return true;
    };
    if (!makeRoom(0)) return false;
    for (unsigned i = 0; i < inserts.length(); ++i) {
      uint64_t n_ =
	uint64_t(inserts[i].name.length()) + inserts[i].value.length() + 32;
      if (n_ > capacity) return false;
      uint32_t n = uint32_t(n_);
      if (!makeRoom(n)) return false;
      used += n;
    }
    return true;
  }
  bool lookupAbs(uint64_t, Header &) const;
  bool evict();
  bool dropOldest();
  bool insertCountIncrement(uint64_t);
  bool sectionAck(uint64_t);
  bool streamCancellation(uint64_t);
  bool applyDecoder(QPackInsn::T, uint64_t);
  bool sectionAdmissible(uint64_t) const;
  bool trackSection(uint64_t, ZuSpan<uint64_t>);
  bool trackSection(uint64_t, QPackTxRefs);
  void compactOrder();
  void rebuildHashes();

  uint64_t insertCount() const { return insertCount_; }
  uint64_t knownReceivedCount() const { return knownReceivedCount_; }
  uint32_t capacity() const { return capacityBytes_; }
  uint32_t localCapacity() const { return localCapacityBytes_; }
  uint32_t peerCapacity() const { return peerCapacityBytes_; }
  uint32_t effectiveCapacity() const {
    return localCapacityBytes_ < peerCapacityBytes_ ?
      localCapacityBytes_ : peerCapacityBytes_;
  }
  uint32_t peerBlocked() const { return peerBlocked_; }
  uint32_t maxSections() const { return maxSections_; }
  uint32_t used() const { return usedBytes_; }
  uint32_t count() const { return order.length() - orderHead_; }
  uint32_t orderSlots() const { return order.size(); }
  uint32_t exactSlots() const { return exact ? exact->size() : 0; }
  uint32_t exactResized() const { return exact ? exact->resized() : 0; }
  uint32_t nameSlots() const { return names ? names->size() : 0; }
  uint32_t nameResized() const { return names ? names->resized() : 0; }
  uint32_t sectionCount() const { return sectionCount_; }
  uint32_t sectionSlots() const { return sections ? sections->size() : 0; }
  uint32_t sectionResized() const {
    return sections ? sections->resized() : 0;
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
  static int decodeEncoderInsn(ZuCSpan, QPackDecodedInsn &);
  static int decodeDecoderInsn(ZuCSpan, QPackDecodedInsn &);
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
    auto nameStorage =
      ZtScratch(HdrBytes, Compression::Huffman::declen(in.length()));
    auto valueStorage =
      ZtScratch(HdrBytes, Compression::Huffman::declen(in.length()));

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
	if (Compression::decodePref(
	    in, o, 6, index, &indexFirst) < 0)
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
	if (Compression::decodePref(in, o, 4, index) < 0 ||
	    !table || !table->lookupPostBase(base, index, indexed))
	  return -1;
	name = indexed.name;
	value = indexed.value;
	flags.dynamicRef = true;
	flags.postBase = true;
      } else if ((first & 0xc0) == 0x40) {
	uint64_t index = 0;
	uint8_t nameFirst = 0;
	if (Compression::decodePref(
	    in, o, 4, index, &nameFirst) < 0)
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
	if (Compression::decodePref(in, o, 3, index) < 0 ||
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
      params, insertCount, params.qpackRxCapacity());
  }
};

class QPackInsnParser {
public:
  template <typename Decode, typename Apply>
  bool parse(ZuBSpan span, Decode decode, Apply apply) {
    if (bytes.length() != offset) {
      for (unsigned i = 0; i < span.length(); ++i) bytes.push(span[i]);
      return drain_(decode, apply);
    }

    unsigned o = 0;
    while (o < span.length()) {
      QPackDecodedInsn insn;
      int n = decode(ZuCSpan{span}.offset(o), insn);
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
      int n = decode(ZuCSpan{bytes}.offset(offset), insn);
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
  StaticEntry<ZuStringT<Key>>
#define Zhttp_QPack_2(Key, Value) \
  StaticEntry<ZuStringT<Key>, ZuStringT<Value>>
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

using QPackStatic = StaticTable<QPackTbl>;

// evaluates QPACK static table index I given <Key, Value>
// - use <Key, void> for entries which are Key only
// - evaluates to -1 if <Key, Value> are not in table
template <typename Key, typename Value,
  bool = ZuTypeIn<StaticEntry<Key, Value>, QPackTbl>{}>
struct QPackIndex_ {
  using T = ZuInt<-1>;
};
template <typename Key, typename Value>
struct QPackIndex_<Key, Value, true> {
  using T = ZuTypeIndex<StaticEntry<Key, Value>, QPackTbl>;
};
template <typename Key, typename Value>
using QPackIndex = typename QPackIndex_<Key, Value>::T;

template <ZuString Key, ZuString Value>
using QPackKVIndex = QPackIndex<ZuStringT<Key>, ZuStringT<Value>>;
template <typename KV>
using QPackKey = StaticKey<KV>;
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
template <typename KV>
using QPackValue = StaticValue<KV>;

} // namespace H3

} // namespace Zhttp

#endif /* ZhttpQPack_HH */
