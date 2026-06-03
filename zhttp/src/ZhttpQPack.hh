//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z HTTP/3 QPACK support

#ifndef ZhttpQPack_HH
#define ZhttpQPack_HH

#ifndef Zhttp_HH
#include <zlib/Zhttp.hh>
#endif

#include <zlib/ZquicPacket.hh>
#include <zlib/ZiAssert.hh>

namespace Zhttp { namespace H3 {

struct H3Error {
  ZtEnum(H3Error, uint16_t,
    NoError, GeneralProtocol, Internal, StreamCreation, ClosedCriticalStream,
    FrameUnexpected, FrameError, ExcessiveLoad, IDError, SettingsError,
    MissingSettings, RequestRejected, RequestCancelled, RequestIncomplete,
    MessageError, ConnectError, VersionFallback, QPackDecompressionFailed,
    QPackEncoderStreamError, QPackDecoderStreamError);
};

struct QPackInstruction {
  ZtEnum(QPackInstruction, int8_t,
    InsertWithNameRef, InsertWithoutNameRef, Duplicate, SetCapacity,
    SectionAck, StreamCancellation, InsertCountIncrement);
};

struct Header {
  ZuCSpan	name;
  ZuCSpan	value;
};

struct QPackDecodedInstruction {
  QPackInstruction::T	type = QPackInstruction::SetCapacity;
  uint64_t		value = 0;
  Header		header;
  bool			nameRefDynamic = false;
};

struct FieldSectionPrefix {
  uint64_t	requiredInsertCount = 0;
  uint64_t	base = 0;
  bool		baseNegative = false;
};

using HeaderBytes = ZtArray<uint8_t, ZtArrayHeapID<"Zhttp.H3.HeaderBytes">>;
using Headers = ZtArray<Header, ZtArrayHeapID<"Zhttp.H3.Headers">>;
using HeaderName = ZtString<ZtStringHeapID<"Zhttp.H3.HeaderName">>;

inline int qpackDecodePrefInt_(
  ZuCSpan in, unsigned &o, unsigned prefixBits, uint64_t &v,
  uint8_t *firstByte = nullptr)
{
  if (o >= in.length() || !prefixBits || prefixBits > 8) return -1;
  uint8_t mask = uint8_t((1U << prefixBits) - 1U);
  uint8_t first = uint8_t(in[o++]);
  if (firstByte) *firstByte = first;
  v = first & mask;
  if (v < mask) return 0;

  unsigned shift = 0;
  for (;;) {
    if (o >= in.length() || shift >= 56) return -1;
    uint8_t b = uint8_t(in[o++]);
    v += uint64_t(b & 0x7f) << shift;
    if (!(b & 0x80)) return 0;
    shift += 7;
  }
}

struct CodecBytes {
  static int putVar(HeaderBytes &out, uint64_t v) {
    uint8_t buf[8];
    int n = Zquic::VarInt::encode(buf, sizeof(buf), v);
    if (n < 0) return -1;
    for (int i = 0; i < n; ++i) out.push(buf[i]);
    return 0;
  }

  static void putSpan(HeaderBytes &out, ZuCSpan s) {
    for (unsigned i = 0; i < s.length(); ++i) out.push(uint8_t(s[i]));
  }
};

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
    ZiAssert(nIndex_ < MaxNames, "Zhttp", (name),
      "too many QPACK indexed header names", return ZuMv(*this));
    index_[nIndex_++] = name;
    return ZuMv(*this);
  }
  Params &&qpackNeverIndex(ZuCSpan name) {
    ZiAssert(nNeverIndex_ < MaxNames, "Zhttp", (name),
      "too many QPACK never-index header names", return ZuMv(*this));
    neverIndex_[nNeverIndex_++] = name;
    return ZuMv(*this);
  }

  bool indexAllowed(ZuCSpan name) const {
    for (unsigned i = 0; i < nNeverIndex_; ++i)
      if (neverIndex_[i] == name) return false;
    for (unsigned i = 0; i < nIndex_; ++i)
      if (index_[i] == name) return true;
    return false;
  }
  bool neverIndex(ZuCSpan name) const {
    for (unsigned i = 0; i < nNeverIndex_; ++i)
      if (neverIndex_[i] == name) return true;
    return name == "authorization" || name == "cookie" ||
      name == "set-cookie";
  }

  unsigned maxHeaderListSize() const { return maxHeaderListSize_; }
  unsigned qpackTableCapacity() const { return qpackTableCapacity_; }
  unsigned qpackBlockedStreams() const { return qpackBlockedStreams_; }

private:
  static constexpr unsigned MaxNames = 32;

  unsigned	maxHeaderListSize_ = DefltMaxHdr;
  unsigned	qpackTableCapacity_ = 0;
  unsigned	qpackBlockedStreams_ = 0;
  HeaderName	index_[MaxNames];
  HeaderName	neverIndex_[MaxNames];
  unsigned	nIndex_ = 0;
  unsigned	nNeverIndex_ = 0;
};

struct QPack {
  static int staticIndex(ZuCSpan name, ZuCSpan value);
  static bool staticField(uint64_t, Header &);
  static bool staticName(uint64_t, HeaderName &);
  static bool staticNameIndex(ZuCSpan, uint64_t &);
  static int encodeFieldSectionPrefix(HeaderBytes &, const FieldSectionPrefix &);
  static int decodeFieldSectionPrefix(ZuCSpan, FieldSectionPrefix &);
  static bool fieldSectionBase(
    const FieldSectionPrefix &, uint64_t, uint64_t &);
  static bool validateFieldSectionPrefix(
    const FieldSectionPrefix &, uint64_t);
  static int encodeFieldLine(HeaderBytes &, Header, const Params &);
  static int encodeDynamicIndexed(HeaderBytes &, uint64_t);
  static int encodeDynamicNameRef(
    HeaderBytes &, uint64_t, ZuCSpan, bool = false);
  static int encodeLiteral(
    HeaderBytes &, ZuSpan<Header>, const Params &,
    const FieldSectionPrefix & = {});
  static int encodeSetCapacity(HeaderBytes &, uint64_t);
  static int encodeInsertWithNameRef(
    HeaderBytes &, uint64_t, bool, ZuCSpan);
  static int encodeInsertLiteral(HeaderBytes &, Header);
  static int encodeDuplicate(HeaderBytes &, uint64_t);
  static int encodeSectionAck(HeaderBytes &, uint64_t);
  static int encodeStreamCancellation(HeaderBytes &, uint64_t);
  static int encodeInsertCountIncrement(HeaderBytes &, uint64_t);
  static int decodeInstructionOne(ZuCSpan, QPackDecodedInstruction &);
  static int decodeInstruction(ZuCSpan, QPackDecodedInstruction &);
  static int decodeHuffman(HeaderBytes &, ZuCSpan);
  static int decodeString(
    HeaderBytes &, ZuCSpan, unsigned &, unsigned, uint8_t, ZuCSpan &);

  template <typename L>
  static int decodeLiteral(
    ZuCSpan in, L l, const Params &params = {}, uint64_t insertCount = 0) {
    FieldSectionPrefix prefix;
    int prefixLen = decodeFieldSectionPrefix(in, prefix);
    if (prefixLen < 0 || !validateFieldSectionPrefix(prefix, insertCount))
      return -1;
    unsigned o = unsigned(prefixLen);
    unsigned headerBytes = 0;
    while (o < in.length()) {
      uint8_t first = uint8_t(in[o]);
      ZuCSpan name;
      ZuCSpan value;
      HeaderBytes nameStorage;
      HeaderBytes valueStorage;

      if (first & 0x80) {
	uint64_t index = 0;
	uint8_t indexFirst = 0;
	if (qpackDecodePrefInt_(in, o, 6, index, &indexFirst) < 0 ||
	    !(indexFirst & 0x40))
	  return -1;
	Header h;
	if (!staticField(index, h)) return -1;
	name = h.name;
	value = h.value;
      } else if ((first & 0xc0) == 0x40) {
	uint64_t index = 0;
	uint8_t nameFirst = 0;
	if (qpackDecodePrefInt_(in, o, 4, index, &nameFirst) < 0 ||
	    !(nameFirst & 0x10))
	  return -1;
	Header h;
	if (!staticField(index, h)) return -1;
	name = h.name;
	if (decodeString(valueStorage, in, o, 7, 0x80, value) < 0)
	  return -1;
      } else if ((first & 0xe0) == 0x20) {
	if (decodeString(nameStorage, in, o, 3, 0x08, name) < 0)
	  return -1;
	if (decodeString(valueStorage, in, o, 7, 0x80, value) < 0)
	  return -1;
      } else
	return -1;

      headerBytes += name.length() + value.length();
      if (headerBytes > params.maxHeaderListSize()) return -1;
      l(Header{name, value});
    }
    return int(o);
  }
};

class DynamicTable {
public:
  static constexpr unsigned MaxEntries = 64;

  explicit DynamicTable(unsigned capacity = 0) : m_capacity{capacity} { }

  unsigned capacity() const { return m_capacity; }
  unsigned used() const { return m_used; }
  unsigned count() const { return m_count; }
  uint64_t insertCount() const { return m_insertCount; }

  void capacity(unsigned v) {
    m_capacity = v;
    evict_();
  }

  bool insert(Header h) {
    unsigned n = entrySize_(h.name, h.value);
    if (n > m_capacity || m_count >= MaxEntries) return false;
    for (unsigned i = m_count; i; --i) m_entries[i] = m_entries[i - 1];
    m_entries[0].name = h.name;
    m_entries[0].value = h.value;
    m_entries[0].size = n;
    ++m_count;
    ++m_insertCount;
    m_used += n;
    evict_();
    return m_entries[0].name == h.name && m_entries[0].value == h.value;
  }

  bool duplicate(unsigned index) {
    if (!index || index > m_count) return false;
    Entry e = m_entries[index - 1];
    return insert(Header{e.name, e.value});
  }

  int find(ZuCSpan name, ZuCSpan value) const {
    for (unsigned i = 0; i < m_count; ++i)
      if (m_entries[i].name == name && m_entries[i].value == value)
	return int(i + 1);
    return -1;
  }

  bool entry(unsigned index, Header &h) const {
    if (!index || index > m_count) return false;
    h = Header{m_entries[index - 1].name, m_entries[index - 1].value};
    return true;
  }
  bool entryAbsolute(uint64_t absolute, Header &h) const {
    if (absolute >= m_insertCount) return false;
    uint64_t distance = m_insertCount - absolute - 1;
    if (distance >= m_count) return false;
    h = Header{
      m_entries[unsigned(distance)].name,
      m_entries[unsigned(distance)].value};
    return true;
  }
  bool entryRelative(uint64_t base, uint64_t index, Header &h) const {
    if (index >= base) return false;
    return entryAbsolute(base - index - 1, h);
  }
  bool entryPostBase(uint64_t base, uint64_t index, Header &h) const {
    if (base > uint64_t(-1) - index) return false;
    return entryAbsolute(base + index, h);
  }

private:
  struct Entry {
    HeaderName	name;
    HeaderName	value;
    unsigned	size = 0;
  };

  static unsigned entrySize_(ZuCSpan name, ZuCSpan value) {
    return name.length() + value.length() + 32;
  }

  void evict_() {
    while (m_count && m_used > m_capacity) {
      m_used -= m_entries[m_count - 1].size;
      --m_count;
    }
  }

  Entry		m_entries[MaxEntries];
  unsigned	m_capacity = 0;
  unsigned	m_used = 0;
  unsigned	m_count = 0;
  uint64_t	m_insertCount = 0;
};

template <typename L>
int decodeLiteralDynamic(
  ZuCSpan in, const DynamicTable &table, L l,
  const Params &params = {}, uint64_t insertCount = 0)
{
  FieldSectionPrefix prefix;
  int prefixLen = QPack::decodeFieldSectionPrefix(in, prefix);
  uint64_t base = 0;
  if (prefixLen < 0 || !QPack::fieldSectionBase(prefix, insertCount, base))
    return -1;

  unsigned o = unsigned(prefixLen);
  unsigned headerBytes = 0;
  while (o < in.length()) {
    uint8_t first = uint8_t(in[o]);
    ZuCSpan name;
    ZuCSpan value;
    Header indexed;
    HeaderBytes nameStorage;
    HeaderBytes valueStorage;

    auto readValue = [&]() -> bool {
      return QPack::decodeString(valueStorage, in, o, 7, 0x80, value) >= 0;
    };

    if (first & 0x80) {
      uint64_t index = 0;
      uint8_t indexFirst = 0;
      if (qpackDecodePrefInt_(in, o, 6, index, &indexFirst) < 0)
	return -1;
      if (indexFirst & 0x40) {
	if (!QPack::staticField(index, indexed)) return -1;
      } else if (!table.entryRelative(base, index, indexed))
	return -1;
      name = indexed.name;
      value = indexed.value;
    } else if ((first & 0xf0) == 0x10) {
      uint64_t index = 0;
      if (qpackDecodePrefInt_(in, o, 4, index) < 0 ||
	  !table.entryPostBase(base, index, indexed))
	return -1;
      name = indexed.name;
      value = indexed.value;
    } else if ((first & 0xc0) == 0x40) {
      uint64_t index = 0;
      uint8_t nameFirst = 0;
      if (qpackDecodePrefInt_(in, o, 4, index, &nameFirst) < 0)
	return -1;
      if (nameFirst & 0x10) {
	if (!QPack::staticField(index, indexed)) return -1;
      } else if (!table.entryRelative(base, index, indexed))
	return -1;
      name = indexed.name;
      if (!readValue()) return -1;
    } else if ((first & 0xf0) == 0x00) {
      uint64_t index = 0;
      if (qpackDecodePrefInt_(in, o, 3, index) < 0 ||
	  !table.entryPostBase(base, index, indexed))
	return -1;
      name = indexed.name;
      if (!readValue()) return -1;
    } else if ((first & 0xe0) == 0x20) {
      if (QPack::decodeString(nameStorage, in, o, 3, 0x08, name) < 0)
	return -1;
      if (!readValue()) return -1;
    } else
      return -1;

    headerBytes += name.length() + value.length();
    if (headerBytes > params.maxHeaderListSize()) return -1;
    l(Header{name, value});
  }
  return int(o);
}

class DynamicState {
public:
  static constexpr unsigned MaxBlocked = 64;

  explicit DynamicState(unsigned capacity = 0, unsigned maxBlocked = 0) :
    m_table{capacity}, m_maxCapacity{capacity}, m_maxBlocked{maxBlocked} { }

  DynamicTable &table() { return m_table; }
  const DynamicTable &table() const { return m_table; }
  unsigned maxCapacity() const { return m_maxCapacity; }
  uint64_t insertCount() const { return m_table.insertCount(); }
  uint64_t knownReceivedCount() const { return m_knownReceivedCount; }
  unsigned blockedCount() const { return m_blockedCount; }
  unsigned acknowledgements() const { return m_acknowledgements; }
  unsigned cancellations() const { return m_cancellations; }

  bool capacity(unsigned v) {
    if (v > m_maxCapacity) return false;
    m_table.capacity(v);
    return true;
  }
  bool insert(Header h) { return m_table.insert(h); }
  bool duplicate(unsigned index) { return m_table.duplicate(index); }

  bool applyInstruction(const QPackDecodedInstruction &i) {
    if (i.type == QPackInstruction::SetCapacity) {
      if (i.value > unsigned(-1)) return false;
      return capacity(unsigned(i.value));
    }
    if (i.type == QPackInstruction::InsertWithNameRef)
      return insertWithNameRef_(i);
    if (i.type == QPackInstruction::InsertWithoutNameRef)
      return insert(i.header);
    if (i.type == QPackInstruction::Duplicate)
      return duplicate(unsigned(i.value));
    if (i.type == QPackInstruction::SectionAck)
      return sectionAck(i.value);
    if (i.type == QPackInstruction::StreamCancellation)
      return streamCancellation(i.value);
    if (i.type == QPackInstruction::InsertCountIncrement)
      return insertCountIncrement(i.value);
    return false;
  }

  bool block(uint64_t streamID, uint64_t requiredInsertCount) {
    if (requiredInsertCount <= insertCount()) return true;
    for (unsigned i = 0; i < m_blockedCount; ++i)
      if (m_blocked[i].streamID == streamID) {
	m_blocked[i].requiredInsertCount = requiredInsertCount;
	return true;
      }
    if (m_blockedCount >= m_maxBlocked || m_blockedCount >= MaxBlocked)
      return false;
    m_blocked[m_blockedCount++] = Blocked{streamID, requiredInsertCount};
    return true;
  }

  unsigned unblockReady() {
    unsigned n = 0;
    for (unsigned i = 0; i < m_blockedCount; ) {
      if (m_blocked[i].requiredInsertCount > insertCount()) {
	++i;
	continue;
      }
      removeBlocked_(i);
      ++n;
    }
    return n;
  }

  bool sectionAck(uint64_t streamID) {
    removeBlockedStream_(streamID);
    ++m_acknowledgements;
    return true;
  }

  bool streamCancellation(uint64_t streamID) {
    if (removeBlockedStream_(streamID)) ++m_cancellations;
    return true;
  }

  bool insertCountIncrement(uint64_t n) {
    if (n > insertCount() || m_knownReceivedCount + n > insertCount())
      return false;
    m_knownReceivedCount += n;
    return true;
  }

private:
  struct Blocked {
    uint64_t	streamID = 0;
    uint64_t	requiredInsertCount = 0;
  };

  void removeBlocked_(unsigned i) {
    for (unsigned j = i + 1; j < m_blockedCount; ++j)
      m_blocked[j - 1] = m_blocked[j];
    --m_blockedCount;
  }

  bool removeBlockedStream_(uint64_t streamID) {
    for (unsigned i = 0; i < m_blockedCount; ++i) {
      if (m_blocked[i].streamID != streamID) continue;
      removeBlocked_(i);
      return true;
    }
    return false;
  }

  bool insertWithNameRef_(const QPackDecodedInstruction &i) {
    HeaderName name;
    if (i.nameRefDynamic) {
      Header h;
      if (!m_table.entry(unsigned(i.value), h)) return false;
      name = h.name;
    } else if (!QPack::staticName(i.value, name))
      return false;
    return insert(Header{name, i.header.value});
  }

  DynamicTable	m_table;
  unsigned	m_maxCapacity = 0;
  unsigned	m_maxBlocked = 0;
  Blocked	m_blocked[MaxBlocked];
  unsigned	m_blockedCount = 0;
  uint64_t	m_knownReceivedCount = 0;
  unsigned	m_acknowledgements = 0;
  unsigned	m_cancellations = 0;
};

}} // namespace Zhttp::H3

#endif /* ZhttpQPack_HH */
