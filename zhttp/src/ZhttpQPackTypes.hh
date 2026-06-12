//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z HTTP/3 QPACK shared types

#ifndef ZhttpQPackTypes_HH
#define ZhttpQPackTypes_HH

#ifndef ZhttpLib_HH
#include <zlib/ZhttpLib.hh>
#endif

#include <zlib/ZuHash.hh>
#include <zlib/ZuString.hh>
#include <zlib/ZtEnum.hh>
#include <zlib/ZtString.hh>
#include <zlib/ZtArray.hh>
#include <zlib/ZmLHash.hh>

namespace Zhttp { namespace H3 {

using HeaderBytes = ZtArray<uint8_t, ZtArrayHeapID<"Zhttp.H3.HeaderBytes">>;

struct QPackInstruction {
  ZtEnum(QPackInstruction, int8_t,
    InsertWithNameRef, InsertWithoutNameRef, Duplicate, SetCapacity,
    SectionAck, StreamCancellation, InsertCountIncrement);
};

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

struct QPackBuildFailure {
  ZtEnum(QPackBuildFailure, uint8_t,
    None, Plan, PrefixEncode, CapacityPolicy, EncoderCapacityWrite,
    EncoderInsertWrite, HeadersFrameHeaderWrite, HeadersPayloadEmit,
    Flush, CapacityCommit, InsertCommit, SectionTracking);
};

struct QPackDecodedInstruction {
  QPackInstruction::T	type = QPackInstruction::SetCapacity;
  uint64_t		value = 0;
  Header		header;
  HeaderBytes		nameStorage;
  HeaderBytes		valueStorage;
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

  static QPackFieldKey FieldAxor(const QPackTxEntry &entry) {
    return {entry.name, entry.value};
  }
};

using QPackTxHash = ZmLHash<QPackTxEntry,
  ZmLHashKey<QPackTxEntry::FieldAxor,
    ZmLHashID<QPackTxHashID>>>;

struct QPackTxOrderEntry {
  uint64_t	abs = 0;
  const QPackTxEntry *entry = nullptr;
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

struct QPackEncoderTx {
  virtual ~QPackEncoderTx() = default;
  virtual bool write(ZuBSpan) = 0;
};

struct QPackTxTable {
  // Connection-affine Tx state. Callers must serialize access from the owning
  // transmit path.
  QPackTxTable() : hash{ZmHashParams{128}}, sections{ZmHashParams{64}} { }

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

  uint64_t insertCount() const { return insertCount_; }
  uint64_t knownReceivedCount() const { return knownReceivedCount_; }
  uint32_t capacity() const { return capacityBytes_; }
  uint32_t maxCapacity() const { return maxCapacityBytes_; }
  uint32_t used() const { return usedBytes_; }

  QPackTxHash	hash;
  QPackTxOrder	order;
  QPackTxSections sections;
  uint32_t	orderHead_ = 0;
  uint64_t	insertCount_ = 0;
  uint64_t	knownReceivedCount_ = 0;
  uint32_t	capacityBytes_ = 0;
  uint32_t	maxCapacityBytes_ = 0;
  uint32_t	usedBytes_ = 0;
  bool		capacitySent = false;
};

}} // namespace Zhttp::H3

#endif /* ZhttpQPackTypes_HH */
