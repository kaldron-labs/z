//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZhttpQPack.hh>

namespace Zhttp { namespace H3 {

static int putPref_(HeaderBytes &out, uint8_t prefix, unsigned prefixBits,
  uint64_t v)
{
  if (!prefixBits || prefixBits > 8) return -1;
  uint8_t mask = uint8_t((1U << prefixBits) - 1U);
  if (v < mask) {
    out.push(prefix | uint8_t(v));
    return 0;
  }

  out.push(prefix | mask);
  v -= mask;
  while (v >= 128) {
    out.push(uint8_t((v & 0x7f) | 0x80));
    v >>= 7;
  }
  out.push(uint8_t(v));
  return 0;
}

static int putString_(HeaderBytes &out, uint8_t prefix, unsigned prefixBits,
  ZuCSpan s)
{
  if (putPref_(out, prefix, prefixBits, s.length()) < 0) return -1;
  CodecBytes::putSpan(out, s);
  return 0;
}

static uint32_t qpackEntrySize_(ZuCSpan name, ZuCSpan value)
{
  uint64_t n = uint64_t(name.length()) + value.length() + 32;
  return n > uint32_t(-1) ? uint32_t(-1) : uint32_t(n);
}

bool QPackRxTable::setCapacity(uint32_t capacity)
{
  if (capacity > maxCapacityBytes_) return false;
  capacityBytes_ = capacity;
  while (entries.length() && usedBytes_ > capacityBytes_) {
    usedBytes_ -= entries[0].size;
    entries.splice(0, 1);
    ++baseAbs_;
  }
  return true;
}

bool QPackRxTable::insert(Header h)
{
  return insert(h.name, h.value);
}

bool QPackRxTable::insert(ZuCSpan name, ZuCSpan value)
{
  uint32_t n = qpackEntrySize_(name, value);
  if (n > capacityBytes_) return false;
  while (entries.length() && usedBytes_ + n > capacityBytes_) {
    usedBytes_ -= entries[0].size;
    entries.splice(0, 1);
    ++baseAbs_;
  }
  auto e = entries.push();
  e->abs = insertCount_++;
  e->size = n;
  e->name = name;
  e->value = value;
  usedBytes_ += n;
  return true;
}

bool QPackRxTable::duplicate(uint64_t relativeIndex)
{
  Header h;
  if (!lookupRelative(insertCount_, relativeIndex, h)) return false;
  QPackRxString name = h.name;
  QPackRxString value = h.value;
  return insert(name, value);
}

bool QPackRxTable::lookupAbs(uint64_t abs, Header &h) const
{
  if (abs < baseAbs_ || abs >= baseAbs_ + entries.length()) return false;
  const auto &e = entries[unsigned(abs - baseAbs_)];
  h = Header{e.name, e.value};
  return true;
}

bool QPackRxTable::lookupRelative(
  uint64_t base, uint64_t index, Header &h) const
{
  if (!base || index >= base) return false;
  return lookupAbs(base - index - 1, h);
}

bool QPackRxTable::lookupPostBase(
  uint64_t base, uint64_t index, Header &h) const
{
  if (base > uint64_t(-1) - index) return false;
  return lookupAbs(base + index, h);
}

bool QPackTxTable::setMaxCapacity(uint32_t capacity)
{
  maxCapacityBytes_ = capacity;
  if (capacityBytes_ > maxCapacityBytes_) return setCapacity(maxCapacityBytes_);
  return true;
}

bool QPackTxTable::setCapacity(uint32_t capacity)
{
  if (capacity > maxCapacityBytes_) return false;
  capacityBytes_ = capacity;
  return evict();
}

const QPackTxEntry *QPackTxTable::find(ZuCSpan name, ZuCSpan value) const
{
  return hash.find(QPackFieldKey{name, value});
}

bool QPackTxTable::lookupAbs(uint64_t abs, Header &h) const
{
  QPackTxHash::CIter i{hash};
  while (auto e = i())
    if (e->abs == abs) {
      h = Header{e->name, e->value};
      return true;
    }
  return false;
}

bool QPackTxTable::insert(Header h, uint64_t *abs)
{
  if (auto e = find(h.name, h.value)) {
    if (abs) *abs = e->abs;
    return true;
  }
  uint32_t n = qpackEntrySize_(h.name, h.value);
  if (n > capacityBytes_) return false;
  while (order.length() && usedBytes_ + n > capacityBytes_) {
    if (!evict()) return false;
    if (!order.length() && usedBytes_ + n > capacityBytes_) return false;
  }
  QPackTxEntry e;
  e.abs = insertCount_++;
  e.size = n;
  e.name = h.name;
  e.value = h.value;
  if (!hash.add(e)) return false;
  order.push(e.abs);
  usedBytes_ += n;
  if (abs) *abs = e.abs;
  return true;
}

bool QPackTxTable::evict()
{
  while (order.length() && usedBytes_ > capacityBytes_) {
    uint64_t abs = order[0];
    bool removed = false;
    QPackTxHash::Iter i{hash};
    while (auto e = i()) {
      if (e->abs != abs) continue;
      if (e->refcnt) return false;
      usedBytes_ -= e->size;
      i.del();
      removed = true;
      break;
    }
    order.splice(0, 1);
    if (!removed) continue;
  }
  return usedBytes_ <= capacityBytes_;
}

bool QPackTxTable::insertCountIncrement(uint64_t n)
{
  if (!n || n > insertCount_ || knownReceivedCount_ > insertCount_ - n)
    return false;
  knownReceivedCount_ += n;
  return knownReceivedCount_ <= insertCount_;
}

void QPackTxTable::trackSection(uint64_t streamID, ZuSpan<uint64_t> refs)
{
  if (!refs.length()) return;
  auto section = sections.push();
  section->streamID = streamID;
  for (unsigned i = 0; i < refs.length(); ++i) {
    section->refs.push(refs[i]);
    QPackTxHash::Iter hi{hash};
    while (auto e = hi())
      if (e->abs == refs[i]) {
	const_cast<QPackTxEntry *>(e)->refcnt++;
	break;
      }
  }
}

bool QPackTxTable::sectionAck(uint64_t streamID)
{
  for (unsigned i = 0; i < sections.length(); ++i) {
    if (sections[i].streamID != streamID) continue;
    for (unsigned j = 0; j < sections[i].refs.length(); ++j) {
      uint64_t abs = sections[i].refs[j];
      QPackTxHash::Iter hi{hash};
      while (auto e = hi())
	if (e->abs == abs) {
	  if (e->refcnt) const_cast<QPackTxEntry *>(e)->refcnt--;
	  break;
	}
    }
    sections.splice(i, 1);
    return true;
  }
  return false;
}

bool QPackTxTable::streamCancellation(uint64_t streamID)
{
  return sectionAck(streamID);
}

int QPack::decodeHuffman(HeaderBytes &out, ZuCSpan in)
{
  out.length(HPack::declen(in.length()));
  int64_t n = HPack::decode(
    ZuSpan<uint8_t>{out.data(), out.length()},
    ZuBSpan{reinterpret_cast<const uint8_t *>(in.data()), in.length()});
  if (n < 0) return -1;
  out.length(uint64_t(n));
  return int(n);
}

int QPack::decodeString(
  HeaderBytes &storage, ZuCSpan in, unsigned &o, unsigned prefixBits,
  uint8_t huffmanMask, ZuCSpan &out)
{
  uint64_t len = 0;
  uint8_t first = 0;
  int n = qpackDecodePrefInt_(in, o, prefixBits, len, &first);
  if (n < 0) return n;
  if (in.length() < o + len) return -2;
  ZuCSpan raw{in.data() + o, unsigned(len)};
  o += unsigned(len);
  if (!(first & huffmanMask)) {
    out = raw;
    return int(raw.length());
  }
  if (decodeHuffman(storage, raw) < 0) return -1;
  out = ZuCSpan{
    reinterpret_cast<const char *>(storage.data()), storage.length()};
  return int(out.length());
}

bool QPack::staticNameIndex(ZuCSpan name, uint64_t &index)
{
  if (name == ":authority") { index = 0; return true; }
  if (name == ":path") { index = 1; return true; }
  if (name == "accept") { index = 29; return true; }
  if (name == "accept-encoding") { index = 31; return true; }
  if (name == "authorization") { index = 84; return true; }
  if (name == "content-length") { index = 4; return true; }
  if (name == "content-type") { index = 46; return true; }
  if (name == "cookie") { index = 5; return true; }
  if (name == "date") { index = 6; return true; }
  if (name == "server") { index = 92; return true; }
  if (name == "user-agent") { index = 95; return true; }
  return false;
}

int QPack::staticIndex(ZuCSpan name, ZuCSpan value)
{
  if (name == ":method" && value == "GET") return 17;
  if (name == ":method" && value == "POST") return 20;
  if (name == ":authority" && !value.length()) return 0;
  if (name == ":scheme" && value == "http") return 22;
  if (name == ":scheme" && value == "https") return 23;
  if (name == ":path" && value == "/") return 1;
  if (name == ":status" && value == "100") return 63;
  if (name == ":status" && value == "103") return 24;
  if (name == ":status" && value == "200") return 25;
  if (name == ":status" && value == "204") return 64;
  if (name == ":status" && value == "304") return 26;
  if (name == ":status" && value == "404") return 27;
  if (name == ":status" && value == "503") return 28;
  if (name == "accept" && value == "*/*") return 29;
  if (name == "accept-encoding" && value == "gzip, deflate, br") return 31;
  if (name == "content-length" && value == "0") return 4;
  if (name == "content-type" && value == "application/json") return 46;
  if (name == "content-type" && value == "text/plain") return 53;
  return -1;
}

bool QPack::staticField(uint64_t index, Header &field)
{
  if (index == 0) { field = Header{":authority", ""}; return true; }
  if (index == 1) { field = Header{":path", "/"}; return true; }
  if (index == 2) { field = Header{"age", "0"}; return true; }
  if (index == 4) { field = Header{"content-length", "0"}; return true; }
  if (index == 5) { field = Header{"cookie", ""}; return true; }
  if (index == 6) { field = Header{"date", ""}; return true; }
  if (index == 17 || index == 20) {
    field = Header{":method", index == 17 ? "GET" : "POST"};
    return true;
  }
  if (index == 22) { field = Header{":scheme", "http"}; return true; }
  if (index == 23) {
    field = Header{":scheme", "https"};
    return true;
  }
  if (index == 24) { field = Header{":status", "103"}; return true; }
  if (index == 25) { field = Header{":status", "200"}; return true; }
  if (index == 26) { field = Header{":status", "304"}; return true; }
  if (index == 27) { field = Header{":status", "404"}; return true; }
  if (index == 28) { field = Header{":status", "503"}; return true; }
  if (index == 29) { field = Header{"accept", "*/*"}; return true; }
  if (index == 30) {
    field = Header{"accept", "application/dns-message"};
    return true;
  }
  if (index == 31) {
    field = Header{"accept-encoding", "gzip, deflate, br"};
    return true;
  }
  if (index >= 44 && index <= 54) {
    ZuCSpan value = "";
    if (index == 44) value = "application/dns-message";
    else if (index == 45) value = "application/javascript";
    else if (index == 46) value = "application/json";
    else if (index == 47) value = "application/x-www-form-urlencoded";
    else if (index == 48) value = "image/gif";
    else if (index == 49) value = "image/jpeg";
    else if (index == 50) value = "image/png";
    else if (index == 51) value = "text/css";
    else if (index == 52) value = "text/html; charset=utf-8";
    else if (index == 53) value = "text/plain";
    else if (index == 54) value = "text/plain;charset=utf-8";
    field = Header{"content-type", value};
    return true;
  }
  if (index == 63) { field = Header{":status", "100"}; return true; }
  if (index == 64) { field = Header{":status", "204"}; return true; }
  if (index == 65) { field = Header{":status", "206"}; return true; }
  if (index == 66) { field = Header{":status", "302"}; return true; }
  if (index == 67) { field = Header{":status", "400"}; return true; }
  if (index == 68) { field = Header{":status", "403"}; return true; }
  if (index == 69) { field = Header{":status", "421"}; return true; }
  if (index == 70) { field = Header{":status", "425"}; return true; }
  if (index == 71) { field = Header{":status", "500"}; return true; }
  if (index == 84) { field = Header{"authorization", ""}; return true; }
  if (index == 92) { field = Header{"server", ""}; return true; }
  if (index == 95) { field = Header{"user-agent", ""}; return true; }
  return false;
}

bool QPack::staticName(uint64_t index, HeaderName &name)
{
  Header field;
  if (!staticField(index, field)) return false;
  name = field.name;
  return true;
}

int QPack::encodeFieldSectionPrefix(
  HeaderBytes &out, const FieldSectionPrefix &prefix, uint64_t maxCapacity)
{
  uint64_t encodedInsertCount = 0;
  uint64_t deltaBase = 0;
  bool negative = false;
  if (prefix.requiredInsertCount) {
    uint64_t maxEntries = maxCapacity>>5;
    if (!maxEntries) return -1;
    uint64_t fullRange = maxEntries<<1;
    encodedInsertCount = (prefix.requiredInsertCount % fullRange) + 1;
    if (prefix.base >= prefix.requiredInsertCount)
      deltaBase = prefix.base - prefix.requiredInsertCount;
    else {
      negative = true;
      deltaBase = prefix.requiredInsertCount - prefix.base - 1;
    }
  } else if (prefix.base)
    return -1;
  if (putPref_(out, 0, 8, encodedInsertCount) < 0 ||
      putPref_(out, negative ? 0x80 : 0x00, 7, deltaBase) < 0)
    return -1;
  return out.length();
}

int QPack::decodeFieldSectionPrefix(
  ZuCSpan in, EncodedFieldSectionPrefix &prefix)
{
  prefix = {};
  unsigned o = 0;
  uint8_t baseFirst = 0;
  if (qpackDecodePrefInt_(in, o, 8, prefix.encodedInsertCount) < 0 ||
      qpackDecodePrefInt_(in, o, 7, prefix.deltaBase, &baseFirst) < 0)
    return -1;
  prefix.baseNegative = baseFirst & 0x80;
  return int(o);
}

bool QPack::fieldSectionBase(
  const EncodedFieldSectionPrefix &prefix, uint64_t req, uint64_t &base)
{
  base = 0;
  if (!req) {
    if (prefix.deltaBase || prefix.baseNegative) return false;
    return true;
  }
  if (prefix.baseNegative) {
    if (req <= prefix.deltaBase) return false;
    base = req - prefix.deltaBase - 1;
  } else {
    if (prefix.deltaBase > uint64_t(-1) - req) return false;
    base = req + prefix.deltaBase;
  }
  return true;
}

int QPack::decodeFieldSectionPrefix(
  ZuCSpan in, FieldSectionPrefix &prefix, uint64_t insertCount,
  uint64_t maxCapacity)
{
  EncodedFieldSectionPrefix encoded;
  int n = decodeFieldSectionPrefix(in, encoded);
  if (n < 0) return -1;
  prefix = {};
  if (encoded.encodedInsertCount) {
    uint64_t maxEntries = maxCapacity>>5;
    if (!maxEntries) return -1;
    uint64_t fullRange = maxEntries<<1;
    if (encoded.encodedInsertCount > fullRange) return -1;
    uint64_t maxValue = insertCount + maxEntries;
    uint64_t maxWrapped = (maxValue / fullRange) * fullRange;
    uint64_t req = maxWrapped + encoded.encodedInsertCount - 1;
    if (req > maxValue) req -= fullRange;
    if (!req) return -1;
    prefix.requiredInsertCount = req;
  } else if (encoded.deltaBase || encoded.baseNegative)
    return -1;
  if (!fieldSectionBase(encoded, prefix.requiredInsertCount, prefix.base))
    return -1;
  return n;
}

bool QPack::validateFieldSectionPrefix(
  const FieldSectionPrefix &prefix, uint64_t insertCount)
{
  return prefix.requiredInsertCount <= insertCount && prefix.base <= insertCount;
}

int QPack::encodeFieldLine(HeaderBytes &out, Header h, const Params &params)
{
  int staticIndex = QPack::staticIndex(h.name, h.value);
  if (staticIndex >= 0)
    return putPref_(out, 0xc0, 6, unsigned(staticIndex)) < 0 ?
      -1 : int(out.length());

  uint64_t nameIndex = 0;
  if (staticNameIndex(h.name, nameIndex)) {
    uint8_t prefix = uint8_t(0x50 | (params.neverIndex(h.name) ? 0x20 : 0));
    return putPref_(out, prefix, 4, nameIndex) < 0 ||
      putString_(out, 0x00, 7, h.value) < 0 ? -1 : int(out.length());
  }

  return putString_(
    out, uint8_t(0x20 | (params.neverIndex(h.name) ? 0x10 : 0)),
    3, h.name) < 0 ||
    putString_(out, 0x00, 7, h.value) < 0 ? -1 : int(out.length());
}

int QPack::encodeDynamicIndexed(HeaderBytes &out, uint64_t relativeIndex)
{
  return putPref_(out, 0x80, 6, relativeIndex) < 0 ?
    -1 : int(out.length());
}

int QPack::encodeDynamicNameRef(
  HeaderBytes &out, uint64_t relativeIndex, ZuCSpan value, bool neverIndex)
{
  uint8_t prefix = uint8_t(0x40 | (neverIndex ? 0x20 : 0));
  return putPref_(out, prefix, 4, relativeIndex) < 0 ||
    putString_(out, 0x00, 7, value) < 0 ? -1 : int(out.length());
}

int QPack::encodeLiteral(HeaderBytes &out, ZuSpan<Header> headers,
    const Params &params, const FieldSectionPrefix &prefix)
{
  out.length(0);
  if (encodeFieldSectionPrefix(out, prefix) < 0) return -1;
  unsigned headerBytes = 0;
  for (auto &h : headers) {
    if (params.neverIndex(h.name) && params.qpackTableCapacity() &&
	params.indexAllowed(h.name))
      return -1;
    headerBytes += h.name.length() + h.value.length();
    if (headerBytes > params.maxHeaderListSize()) return -1;
    if (encodeFieldLine(out, h, params) < 0) return -1;
  }
  return out.length();
}

int QPack::encodeSetCapacity(HeaderBytes &out, uint64_t capacity)
{
  out.length(0);
  return putPref_(out, 0x20, 5, capacity) < 0 ? -1 : int(out.length());
}

int QPack::encodeInsertWithNameRef(
  HeaderBytes &out, uint64_t index, bool dynamic, ZuCSpan value)
{
  out.length(0);
  uint8_t prefix = uint8_t(0x80 | (dynamic ? 0x00 : 0x40));
  return putPref_(out, prefix, 6, index) < 0 ||
    putString_(out, 0x00, 7, value) < 0 ? -1 : int(out.length());
}

int QPack::encodeInsertLiteral(HeaderBytes &out, Header h)
{
  out.length(0);
  return putString_(out, 0x40, 5, h.name) < 0 ||
    putString_(out, 0x00, 7, h.value) < 0 ? -1 : int(out.length());
}

int QPack::encodeDuplicate(HeaderBytes &out, uint64_t index)
{
  out.length(0);
  return putPref_(out, 0x00, 5, index) < 0 ? -1 : int(out.length());
}

int QPack::encodeSectionAck(HeaderBytes &out, uint64_t streamID)
{
  out.length(0);
  return putPref_(out, 0x80, 7, streamID) < 0 ? -1 : int(out.length());
}

int QPack::encodeStreamCancellation(HeaderBytes &out, uint64_t streamID)
{
  out.length(0);
  return putPref_(out, 0x40, 6, streamID) < 0 ? -1 : int(out.length());
}

int QPack::encodeInsertCountIncrement(HeaderBytes &out, uint64_t n)
{
  out.length(0);
  if (!n) return -1;
  return putPref_(out, 0x00, 6, n) < 0 ? -1 : int(out.length());
}

int QPack::decodeEncoderInstructionOne(ZuCSpan in, QPackDecodedInstruction &i)
{
  unsigned o = 0;
  uint8_t first = 0;
  if (!in.length()) return -2;
  i = {};

  if (uint8_t(in[0]) & 0x80) {
    i.type = QPackInstruction::InsertWithNameRef;
    int n = qpackDecodePrefInt_(in, o, 6, i.value, &first);
    if (n < 0) return n;
    i.nameRefDynamic = !(first & 0x40);
    HeaderBytes valueStorage;
    if ((n = decodeString(valueStorage, in, o, 7, 0x80, i.header.value)) < 0)
      return n;
    return int(o);
  }
  if ((uint8_t(in[0]) & 0xe0) == 0x40) {
    i.type = QPackInstruction::InsertWithoutNameRef;
    HeaderBytes nameStorage, valueStorage;
    int n = decodeString(nameStorage, in, o, 5, 0x20, i.header.name);
    if (n < 0) return n;
    n = decodeString(valueStorage, in, o, 7, 0x80, i.header.value);
    if (n < 0) return n;
    return int(o);
  }
  if ((uint8_t(in[0]) & 0xe0) == 0x20) {
    i.type = QPackInstruction::SetCapacity;
    int n = qpackDecodePrefInt_(in, o, 5, i.value);
    return n < 0 ? n : int(o);
  }
  if ((uint8_t(in[0]) & 0xe0) == 0x00) {
    i.type = QPackInstruction::Duplicate;
    int n = qpackDecodePrefInt_(in, o, 5, i.value);
    return n < 0 ? n : int(o);
  }
  return -1;
}

int QPack::decodeDecoderInstructionOne(ZuCSpan in, QPackDecodedInstruction &i)
{
  unsigned o = 0;
  if (!in.length()) return -2;
  i = {};
  if (uint8_t(in[0]) & 0x80) {
    i.type = QPackInstruction::SectionAck;
    int n = qpackDecodePrefInt_(in, o, 7, i.value);
    return n < 0 ? n : int(o);
  }
  if (uint8_t(in[0]) & 0x40) {
    i.type = QPackInstruction::StreamCancellation;
    int n = qpackDecodePrefInt_(in, o, 6, i.value);
    return n < 0 ? n : int(o);
  }
  i.type = QPackInstruction::InsertCountIncrement;
  int n = qpackDecodePrefInt_(in, o, 6, i.value);
  if (n < 0) return n;
  if (!i.value) return -1;
  return int(o);
}

int QPack::decodeInstructionOne(ZuCSpan in, QPackDecodedInstruction &i)
{
  int n = decodeEncoderInstructionOne(in, i);
  if (n > 0) return n;
  return decodeDecoderInstructionOne(in, i);
}

int QPack::decodeInstruction(ZuCSpan in, QPackDecodedInstruction &i)
{
  int n = decodeInstructionOne(in, i);
  return n < 0 || n != int(in.length()) ? -1 : n;
}

}} // namespace Zhttp::H3
