//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZhttpQPack.hh>

namespace Zhttp { namespace H3 {

static int putPref_(HdrBytes &out, uint8_t prefix, unsigned prefixBits,
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

static int putString_(HdrBytes &out, uint8_t prefix, unsigned prefixBits,
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

static_assert(QPackTbl::N == 99);

template <unsigned I>
static Header qpackStaticField_()
{
  using KV = QPackKV<I>;
  using Key = ZuType<0, KV>;
  using Value = QPackValue<KV>;
  if constexpr (ZuIsSame<Value, void>{})
    return Header{Key{}(), ""};
  else
    return Header{Key{}(), Value{}()};
}

QPackRxEntry *QPackRxTable::pushNewest()
{
  if (head_ && head_ >= 64 && head_ >= (entries.length()>>1))
    compact();
  return new (entries.push()) QPackRxEntry();
}

const QPackRxEntry *QPackRxTable::oldest() const
{
  return head_ < entries.length() ? &entries[head_] : nullptr;
}

void QPackRxTable::dropOldest()
{
  if (head_ >= entries.length()) return;
  usedBytes_ -= entries[head_].size;
  ++head_;
  ++baseAbs_;
  if (head_ == entries.length()) {
    entries.length(0);
    head_ = 0;
  } else if (head_ >= 64 && head_ >= (entries.length()>>1))
    compact();
}

void QPackRxTable::compact()
{
  if (!head_) return;
  entries.splice(0, head_);
  head_ = 0;
}

bool QPackRxTable::setCapacity(uint32_t capacity)
{
  if (capacity > maxCapacityBytes_) return false;
  capacityBytes_ = capacity;
  while (oldest() && usedBytes_ > capacityBytes_) dropOldest();
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
  while (oldest() && usedBytes_ + n > capacityBytes_) dropOldest();
  auto e = pushNewest();
  e->abs = insertCount_++;
  e->size = n;
  e->name = name;
  e->value = value;
  usedBytes_ += n;
  return true;
}

bool QPackRxTable::duplicate(uint64_t relativeIndex)
{
  if (!insertCount_ || relativeIndex >= insertCount_) return false;
  uint64_t abs = insertCount_ - relativeIndex - 1;
  uint64_t count = entries.length() - head_;
  if (abs < baseAbs_ || abs >= baseAbs_ + count) return false;
  uint32_t src = head_ + uint32_t(abs - baseAbs_);
  uint32_t n = entries[src].size;
  if (n > capacityBytes_) return false;

  bool evictsSrc = false;
  uint32_t used = usedBytes_;
  for (uint32_t head = head_; head < entries.length() &&
      used + n > capacityBytes_; ++head) {
    if (head == src) {
      evictsSrc = true;
      break;
    }
    used -= entries[head].size;
  }

  if (evictsSrc) {
    QPackRxString name{entries[src].name};
    QPackRxString value{entries[src].value};
    while (oldest() && usedBytes_ + n > capacityBytes_) dropOldest();
    auto e = pushNewest();
    e->abs = insertCount_++;
    e->size = n;
    e->name = ZuMv(name);
    e->value = ZuMv(value);
    usedBytes_ += n;
    return true;
  }

  while (oldest() && usedBytes_ + n > capacityBytes_) dropOldest();
  if (head_ && head_ >= 64 && head_ >= (entries.length()>>1)) compact();
  entries.ensure(entries.length() + 1);
  src = head_ + uint32_t(abs - baseAbs_);
  Header h{entries[src].name, entries[src].value};
  auto e = new (entries.push()) QPackRxEntry();
  e->abs = insertCount_++;
  e->size = n;
  e->name = h.name;
  e->value = h.value;
  usedBytes_ += n;
  return true;
}

bool QPackRxTable::lookupAbs(uint64_t abs, Header &h) const
{
  uint64_t count = entries.length() - head_;
  if (abs < baseAbs_ || abs >= baseAbs_ + count) return false;
  const auto &e = entries[head_ + unsigned(abs - baseAbs_)];
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
  uint64_t n = capacity>>5;
  if (n > order.size()) {
    order.ensure(n);
    rebuildHash();
  }
  return true;
}

bool QPackTxTable::setCapacity(uint32_t capacity)
{
  if (capacity > maxCapacityBytes_) return false;
  uint32_t old = capacityBytes_;
  capacityBytes_ = capacity;
  if (evict()) return true;
  capacityBytes_ = old;
  return false;
}

const QPackTxEntry *QPackTxTable::find(ZuCSpan name, ZuCSpan value) const
{
  auto h = hash->find(QPackFieldKey{name, value});
  if (!h || h->index < orderHead_ || h->index >= order.length())
    return nullptr;
  const auto &e = order[h->index];
  return e.name == name && e.value == value ? &e : nullptr;
}

const QPackTxEntry *QPackTxTable::findAbs(uint64_t abs) const
{
  if (orderHead_ >= order.length()) return nullptr;
  uint64_t base = order[orderHead_].abs;
  uint64_t count = order.length() - orderHead_;
  if (abs < base || abs >= base + count) return nullptr;
  const auto &o = order[orderHead_ + unsigned(abs - base)];
  if (o.abs != abs) return nullptr;
  return &o;
}

bool QPackTxTable::lookupAbs(uint64_t abs, Header &h) const
{
  if (orderHead_ >= order.length()) return false;
  uint64_t base = order[orderHead_].abs;
  uint64_t count = order.length() - orderHead_;
  if (abs < base || abs >= base + count) return false;
  const auto &o = order[orderHead_ + unsigned(abs - base)];
  if (o.abs != abs) return false;
  h = Header{o.name, o.value};
  return true;
}

bool QPackTxTable::insert(Header h, uint64_t *abs)
{
  return insert(QPackTxString{h.name}, QPackTxString{h.value}, abs);
}

bool QPackTxTable::insert(
  QPackTxString name, QPackTxString value, uint64_t *abs)
{
  if (auto e = find(name, value)) {
    if (abs) *abs = e->abs;
    return true;
  }
  uint32_t n = qpackEntrySize_(name, value);
  if (n > capacityBytes_) return false;
  while (orderHead_ < order.length() && usedBytes_ + n > capacityBytes_)
    if (!dropOldest()) return false;
  if (usedBytes_ + n > capacityBytes_) return false;
  uint64_t nextAbs = insertCount_;
  if (order.size() < order.length() + 1) {
    order.ensure(order.length() + 1);
    rebuildHash();
  }
  auto entry = new (order.push()) QPackTxOrderEntry();
  entry->abs = nextAbs;
  entry->size = n;
  entry->name = ZuMv(name);
  entry->value = ZuMv(value);
  QPackTxHashEntry hashEntry;
  hashEntry.index = uint32_t(order.length() - 1);
  hashEntry.key = {entry->name, entry->value};
  if (!hash->add(hashEntry)) {
    order.length(order.length() - 1);
    return false;
  }
  usedBytes_ += n;
  insertCount_ = nextAbs + 1;
  if (abs) *abs = nextAbs;
  return true;
}

bool QPackTxTable::evict()
{
  while (orderHead_ < order.length() && usedBytes_ > capacityBytes_)
    if (!dropOldest()) return false;
  return usedBytes_ <= capacityBytes_;
}

bool QPackTxTable::dropOldest()
{
  if (orderHead_ >= order.length()) return false;
  const auto &old = order[orderHead_];
  if (old.refcnt) return false;
  usedBytes_ -= old.size;
  hash->del(QPackFieldKey{old.name, old.value});
  ++orderHead_;
  if (orderHead_ == order.length()) {
    order.length(0);
    orderHead_ = 0;
  } else if (orderHead_ >= 64 && orderHead_ >= (order.length()>>1))
    compactOrder();
  return true;
}

void QPackTxTable::compactOrder()
{
  if (!orderHead_) return;
  order.splice(0, orderHead_);
  orderHead_ = 0;
  rebuildHash();
}

void QPackTxTable::rebuildHash()
{
  hash->clean();
  for (unsigned i = orderHead_; i < order.length(); ++i) {
    QPackTxHashEntry h;
    h.index = i;
    h.key = {order[i].name, order[i].value};
    hash->add(h);
  }
}

bool QPackTxTable::insertCountIncrement(uint64_t n)
{
  if (!n || n > insertCount_ || knownReceivedCount_ > insertCount_ - n)
    return false;
  knownReceivedCount_ += n;
  return knownReceivedCount_ <= insertCount_;
}

bool QPackTxTable::trackSection(uint64_t streamID, ZuSpan<uint64_t> refs)
{
  QPackTxRefs refs_;
  refs_.length(refs.length());
  for (unsigned i = 0; i < refs.length(); ++i) refs_[i] = refs[i];
  return trackSection(streamID, ZuMv(refs_));
}

bool QPackTxTable::trackSection(uint64_t streamID, QPackTxRefs refs)
{
  if (!refs.length()) return true;
  if (sections->find(streamID)) return false;
  QPackTxSection section_;
  section_.streamID = streamID;
  section_.refs = ZuMv(refs);
  auto section = const_cast<QPackTxSection *>(sections->add(ZuMv(section_)));
  if (!section) return false;
  for (unsigned i = 0; i < section->refs.length(); ++i) {
    if (auto e = findAbs(section->refs[i]))
      const_cast<QPackTxEntry *>(e)->refcnt++;
  }
  return true;
}

bool QPackTxTable::sectionAck(uint64_t streamID)
{
  auto section = sections->find(streamID);
  if (!section) return false;
  for (unsigned i = 0; i < section->refs.length(); ++i) {
    if (auto e = findAbs(section->refs[i]))
      if (e->refcnt) const_cast<QPackTxEntry *>(e)->refcnt--;
  }
  sections->del(streamID);
  return true;
}

bool QPackTxTable::streamCancellation(uint64_t streamID)
{
  return sectionAck(streamID);
}

int QPack::decodeHuffman(HdrBytes &out, ZuCSpan in)
{
  out.length(HPack::declen(in.length()));
  int64_t n = HPack::decode(out, in);
  if (n < 0) return -1;
  out.length(uint64_t(n));
  return int(n);
}

int QPack::decodeString(
  HdrBytes &storage, ZuCSpan in, unsigned &o, unsigned prefixBits,
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
  out = storage;
  return int(out.length());
}

bool QPack::staticNameIndex(ZuCSpan name, uint64_t &index)
{
  bool ok = false;
  unsigned i = 0;
  ZuUnroll::all<QPackTbl>([&ok, &name, &index, i]<typename KV>() mutable {
    if (!ok) {
      using Key = ZuType<0, KV>;
      if (Key{}() == name) {
	index = i;
	ok = true;
      }
    }
    ++i;
  });
  return ok;
}

int QPack::staticIndex(ZuCSpan name, ZuCSpan value)
{
  int index = -1;
  unsigned i = 0;
  ZuUnroll::all<QPackTbl>(
      [&index, &name, &value, i]<typename KV>() mutable {
    if (index < 0) {
      using Key = ZuType<0, KV>;
      using Value = QPackValue<KV>;
      if (Key{}() == name) {
	if constexpr (ZuIsSame<Value, void>{}) {
	  if (!value.length()) index = int(i);
	} else if (Value{}() == value)
	  index = int(i);
      }
    }
    ++i;
  });
  return index;
}

bool QPack::staticField(uint64_t index, Header &field)
{
  if (index >= QPackTbl::N) return false;
  bool ok = false;
  ZuSwitch::dispatch<QPackTbl::N>(unsigned(index),
    [&field, &ok](auto i) {
      field = qpackStaticField_<i>();
      ok = true;
    });
  return ok;
}

bool QPack::staticName(uint64_t index, HeaderName &name)
{
  if (index >= QPackTbl::N) return false;
  bool ok = false;
  ZuSwitch::dispatch<QPackTbl::N>(unsigned(index),
    [&name, &ok](auto i) {
      using KV = QPackKV<i>;
      using Key = ZuType<0, KV>;
      name = Key{}();
      ok = true;
    });
  return ok;
}

int QPack::encodeFieldSectionPrefix(
  HdrBytes &out, const FieldSectionPrefix &prefix, uint64_t maxCapacity)
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

int QPack::encodeFieldLine(HdrBytes &out, Header h, const Params &params)
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

int QPack::encodeLiteral(HdrBytes &out, ZuSpan<Header> headers,
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

int QPack::encodeSetCapacity(HdrBytes &out, uint64_t capacity)
{
  out.length(0);
  return putPref_(out, 0x20, 5, capacity) < 0 ? -1 : int(out.length());
}

int QPack::encodeInsertWithNameRef(
  HdrBytes &out, uint64_t index, bool dynamic, ZuCSpan value)
{
  out.length(0);
  uint8_t prefix = uint8_t(0x80 | (dynamic ? 0x00 : 0x40));
  return putPref_(out, prefix, 6, index) < 0 ||
    putString_(out, 0x00, 7, value) < 0 ? -1 : int(out.length());
}

int QPack::encodeInsertLiteral(HdrBytes &out, Header h)
{
  out.length(0);
  return putString_(out, 0x40, 5, h.name) < 0 ||
    putString_(out, 0x00, 7, h.value) < 0 ? -1 : int(out.length());
}

int QPack::encodeSectionAck(HdrBytes &out, uint64_t streamID)
{
  out.length(0);
  return putPref_(out, 0x80, 7, streamID) < 0 ? -1 : int(out.length());
}

int QPack::decodeEncoderInsn(ZuCSpan in, QPackDecodedInsn &i)
{
  unsigned o = 0;
  uint8_t first = 0;
  if (!in.length()) return -2;
  i = {};

  if (uint8_t(in[0]) & 0x80) {
    i.type = QPackInsn::InsertWithNameRef;
    int n = qpackDecodePrefInt_(in, o, 6, i.value, &first);
    if (n < 0) return n;
    i.nameRefDynamic = !(first & 0x40);
    if ((n = decodeString(
	  i.valueStorage, in, o, 7, 0x80, i.header.value)) < 0)
      return n;
    return int(o);
  }
  if ((uint8_t(in[0]) & 0xc0) == 0x40) {
    i.type = QPackInsn::InsertWithoutNameRef;
    int n = decodeString(i.nameStorage, in, o, 5, 0x20, i.header.name);
    if (n < 0) return n;
    n = decodeString(i.valueStorage, in, o, 7, 0x80, i.header.value);
    if (n < 0) return n;
    return int(o);
  }
  if ((uint8_t(in[0]) & 0xe0) == 0x20) {
    i.type = QPackInsn::SetCapacity;
    int n = qpackDecodePrefInt_(in, o, 5, i.value);
    return n < 0 ? n : int(o);
  }
  if ((uint8_t(in[0]) & 0xe0) == 0x00) {
    i.type = QPackInsn::Duplicate;
    int n = qpackDecodePrefInt_(in, o, 5, i.value);
    return n < 0 ? n : int(o);
  }
  return -1;
}

int QPack::decodeDecoderInsn(ZuCSpan in, QPackDecodedInsn &i)
{
  unsigned o = 0;
  if (!in.length()) return -2;
  i = {};
  if (uint8_t(in[0]) & 0x80) {
    i.type = QPackInsn::SectionAck;
    int n = qpackDecodePrefInt_(in, o, 7, i.value);
    return n < 0 ? n : int(o);
  }
  if (uint8_t(in[0]) & 0x40) {
    i.type = QPackInsn::StreamCancellation;
    int n = qpackDecodePrefInt_(in, o, 6, i.value);
    return n < 0 ? n : int(o);
  }
  i.type = QPackInsn::InsertCountIncrement;
  int n = qpackDecodePrefInt_(in, o, 6, i.value);
  if (n < 0) return n;
  if (!i.value) return -1;
  return int(o);
}

}} // namespace Zhttp::H3
