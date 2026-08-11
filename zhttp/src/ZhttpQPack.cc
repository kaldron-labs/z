//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuMatcher.hh>
#include <zlib/ZuSwitch.hh>

#include <zlib/ZhttpQPack.hh>

namespace Zhttp { namespace H3 {

ZtEnumImplStruct(QPackInsn);
ZtEnumImplStruct(QPackBuildFailure);

using QPackStatic_ = StaticTable<QPackTbl>;

static int qpackStaticName_(ZuBSpan value)
{
  static constexpr auto matcher = ZuMatcher<QPackStatic_::Names>();
  return matcher.exact(value);
}

static int qpackStaticNameIndex_(ZuBSpan value)
{
  int i = qpackStaticName_(value);
  if (i < 0) return -1;
  int index = -1;
  ZuSwitch::dispatch<QPackStatic_::Names::N>(
    unsigned(i), [&index](auto i) {
      using Key = ZuType<i, QPackStatic_::Names>;
      index = ZuTypeIndex<Key, QPackStatic_::Keys>{};
    });
  return index;
}

static int qpackStaticIndex_(ZuBSpan name, ZuBSpan value)
{
  int i = qpackStaticName_(name);
  if (i < 0) return -1;
  int index = -1;
  ZuSwitch::dispatch<QPackStatic_::Names::N>(
    unsigned(i), [&index, &value](auto nameIndex) {
      using Key = ZuType<nameIndex, QPackStatic_::Names>;
      using KeyEntries = QPackStatic_::Entries<Key>;
      using KeyValues = QPackStatic_::Values<Key>;
      static constexpr auto matcher = ZuMatcher<KeyValues>();
      int j = matcher.exact(value);
      if (j < 0) return;
      ZuSwitch::dispatch<KeyEntries::N>(
	unsigned(j), [&index, nameIndex](auto valueIndex) {
	(void)nameIndex; // gcc bug requires the capture; clang warns if unused
	using KV = ZuType<valueIndex, KeyEntries>;
	index = ZuTypeIndex<KV, QPackTbl>{};
      });
    });
  return index;
}

bool Params::neverIndex(ZuBSpan name) const
{
  if (neverIndex_->find(name)) return true;
  static constexpr auto matcher =
    ZuMatcher<"authorization", "cookie", "set-cookie">();
  return matcher.exact(name) >= 0;
}

static uint32_t qpackEntrySize_(ZuBSpan name, ZuBSpan value)
{
  uint64_t n = uint64_t(name.length()) + value.length() + 32;
  return n > uint32_t(-1) ? uint32_t(-1) : uint32_t(n);
}

ZuAssert(QPackTbl::N == 99);

template <unsigned I>
static Header qpackStaticField_()
{
  using KV = QPackKV<I>;
  using Key = StaticKey<KV>;
  using Value = QPackValue<KV>;
  if constexpr (ZuIsSame<Value, void>{})
    return Header{Key{}(), ""};
  else
    return Header{Key{}(), Value{}()};
}

QPackRxEntry *QPackRxTable::pushNewest()
{
  if (head_ && (entries.length() == entries.size() ||
      (head_ >= 64 && head_ >= (entries.length()>>1))))
    compact();
  if (entries.length() >= entries.size()) return nullptr;
  return new (entries.push()) QPackRxEntry();
}

bool QPackRxTable::init(uint32_t capacity)
{
  final();
  maxCapacityBytes_ = capacity;
  uint32_t n = capacity>>5;
  if (n) entries.ensure(n);
  return entries.size() >= n;
}

void QPackRxTable::final()
{
  entries.init();
  head_ = 0;
  baseAbs_ = 0;
  insertCount_ = 0;
  capacityBytes_ = 0;
  maxCapacityBytes_ = 0;
  usedBytes_ = 0;
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

bool QPackRxTable::insert(ZuBSpan name, ZuBSpan value)
{
  uint32_t n = qpackEntrySize_(name, value);
  if (n > capacityBytes_) return false;
  while (oldest() && usedBytes_ + n > capacityBytes_) dropOldest();
  auto e = pushNewest();
  if (!e) return false;
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
    if (!e) return false;
    e->abs = insertCount_++;
    e->size = n;
    e->name = ZuMv(name);
    e->value = ZuMv(value);
    usedBytes_ += n;
    return true;
  }

  while (oldest() && usedBytes_ + n > capacityBytes_) dropOldest();
  if (head_ && (entries.length() == entries.size() ||
      (head_ >= 64 && head_ >= (entries.length()>>1))))
    compact();
  if (entries.length() >= entries.size()) return false;
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

bool QPackTxTable::init(uint32_t capacity, uint32_t maxSections)
{
  final();
  localCapacityBytes_ = capacity;
  maxSections_ = maxSections;
  uint32_t n = capacity>>5;
  if (n) {
    order.ensure(n);
    exact = new QPackTxHash{ZmHashParams{n}};
    names = new QPackTxNames{ZmHashParams{n}};
  }
  if (maxSections)
    sections = new QPackTxSections{ZmHashParams{maxSections}};
  return order.size() >= n &&
    (!exact || exact->size() >= n) &&
    (!names || names->size() >= n) &&
    (!sections || sections->size() >= maxSections);
}

void QPackTxTable::final()
{
  sections = nullptr;
  names = nullptr;
  exact = nullptr;
  order.init();
  orderHead_ = 0;
  insertCount_ = 0;
  knownReceivedCount_ = 0;
  capacityBytes_ = 0;
  localCapacityBytes_ = 0;
  peerCapacityBytes_ = 0;
  peerBlocked_ = 0;
  maxSections_ = 0;
  sectionCount_ = 0;
  usedBytes_ = 0;
  capacitySent = false;
  frozen_ = false;
}

bool QPackTxTable::peerCapacity(uint32_t capacity)
{
  if (frozen_) return false;
  peerCapacityBytes_ = capacity;
  uint32_t effective = effectiveCapacity();
  if (capacityBytes_ <= effective) return true;
  return setCapacity(effective);
}

bool QPackTxTable::setCapacity(uint32_t capacity)
{
  if (frozen_) return false;
  if (capacity > effectiveCapacity()) return false;
  uint32_t old = capacityBytes_;
  capacityBytes_ = capacity;
  if (evict()) return true;
  capacityBytes_ = old;
  return false;
}

const QPackTxEntry *QPackTxTable::find(ZuBSpan name, ZuBSpan value) const
{
  if (!exact) return nullptr;
  auto h = exact->find(QPackFieldKey{name, value});
  if (!h || h->index < orderHead_ || h->index >= order.length())
    return nullptr;
  const auto &e = order[h->index];
  return e.name == name && e.value == value ? &e : nullptr;
}

const QPackTxEntry *QPackTxTable::findName(ZuBSpan name) const
{
  if (!names) return nullptr;
  auto indexed = names->find(Compression::NameView{name});
  if (!indexed) return nullptr;
  auto entry = findAbs(indexed->abs);
  return entry && entry->name == name ? entry : nullptr;
}

const QPackTxEntry *QPackTxTable::findName(
  ZuBSpan name, uint64_t base) const
{
  auto entry = findName(name);
  while (entry) {
    if (entry->abs < base &&
	entry->abs + 1 <= knownReceivedCount_)
      return entry;
    if (entry->prevName == uint64_t(-1)) break;
    entry = findAbs(entry->prevName);
  }
  return nullptr;
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
  if (frozen_) return false;
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
  if (orderHead_ && order.length() == order.size()) compactOrder();
  if (!exact || !names || order.length() >= order.size()) return false;
  uint64_t prevName = uint64_t(-1);
  if (auto prior = findName(name)) prevName = prior->abs;
  auto entry = new (order.push()) QPackTxOrderEntry();
  entry->abs = nextAbs;
  entry->prevName = prevName;
  entry->size = n;
  entry->owned(ZuMv(name), ZuMv(value));
  QPackTxHashEntry hashEntry;
  hashEntry.index = uint32_t(order.length() - 1);
  hashEntry.key = {entry->name, entry->value};
  if (!exact->add(hashEntry)) {
    order.length(order.length() - 1);
    return false;
  }
  auto nameEntry = const_cast<QPackTxNameEntry *>(
      names->find(Compression::NameView{entry->name}));
  if (nameEntry) {
    nameEntry->abs = nextAbs;
    nameEntry->key = Compression::NameView{entry->name};
  } else if (!names->add(QPackTxNameEntry{
      nextAbs, Compression::NameView{entry->name}})) {
    exact->del(hashEntry.key);
    order.length(order.length() - 1);
    return false;
  }
  ZmAssert(!exact->resized());
  ZmAssert(!names->resized());
  ZmAssert(order.length() - orderHead_ <= order.size());
  usedBytes_ += n;
  insertCount_ = nextAbs + 1;
  if (abs) *abs = nextAbs;
  return true;
}

bool QPackTxTable::insertView(Header field, uint64_t *abs)
{
  if (frozen_) return false;
  if (auto e = find(field.name, field.value)) {
    if (abs) *abs = e->abs;
    return true;
  }
  uint32_t n = qpackEntrySize_(field.name, field.value);
  if (n > capacityBytes_) return false;
  while (orderHead_ < order.length() && usedBytes_ + n > capacityBytes_)
    if (!dropOldest()) return false;
  if (usedBytes_ + n > capacityBytes_) return false;
  uint64_t nextAbs = insertCount_;
  if (orderHead_ && order.length() == order.size()) compactOrder();
  if (!exact || !names || order.length() >= order.size()) return false;
  uint64_t prevName = uint64_t(-1);
  if (auto prior = findName(field.name)) prevName = prior->abs;
  auto entry = new (order.push()) QPackTxOrderEntry();
  entry->abs = nextAbs;
  entry->prevName = prevName;
  entry->size = n;
  entry->view(field);
  QPackTxHashEntry hashEntry;
  hashEntry.index = uint32_t(order.length() - 1);
  hashEntry.key = {entry->name, entry->value};
  if (!exact->add(hashEntry)) {
    order.length(order.length() - 1);
    return false;
  }
  auto nameEntry = const_cast<QPackTxNameEntry *>(
      names->find(Compression::NameView{entry->name}));
  if (nameEntry) {
    nameEntry->abs = nextAbs;
    nameEntry->key = Compression::NameView{entry->name};
  } else if (!names->add(QPackTxNameEntry{
      nextAbs, Compression::NameView{entry->name}})) {
    exact->del(hashEntry.key);
    order.length(order.length() - 1);
    return false;
  }
  ZmAssert(!exact->resized());
  ZmAssert(!names->resized());
  ZmAssert(order.length() - orderHead_ <= order.size());
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
  usedBytes_ -= old.size;
  exact->del(QPackFieldKey{old.name, old.value});
  auto name = names->find(Compression::NameView{old.name});
  if (name && name->abs == old.abs)
    names->del(Compression::NameView{old.name});
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
  rebuildHashes();
}

void QPackTxTable::rebuildHashes()
{
  if (!exact || !names) return;
  exact->clean();
  names->clean();
  for (unsigned i = orderHead_; i < order.length(); ++i) {
    QPackTxHashEntry h;
    h.index = i;
    h.key = {order[i].name, order[i].value};
    if (!exact->add(h)) {
      ZmAssert(false);
      return;
    }
    auto name = const_cast<QPackTxNameEntry *>(
	names->find(Compression::NameView{order[i].name}));
    if (name) {
      name->abs = order[i].abs;
      name->key = Compression::NameView{order[i].name};
    } else if (!names->add(QPackTxNameEntry{
	order[i].abs, Compression::NameView{order[i].name}})) {
      ZmAssert(false);
      return;
    }
  }
  ZmAssert(!exact->resized());
  ZmAssert(!names->resized());
}

bool QPackTxTable::insertCountIncrement(uint64_t n)
{
  if (!n || n > insertCount_ || knownReceivedCount_ > insertCount_ - n)
    return false;
  knownReceivedCount_ += n;
  return knownReceivedCount_ <= insertCount_;
}

bool QPackTxTable::registerSection(uint64_t streamID)
{
  if (!sectionAdmissible(streamID)) return false;
  auto section = sections->add(QPackTxSection{streamID});
  if (!section) return false;
  ZmAssert(!sections->resized());
  ZmAssert(sectionCount_ < maxSections_);
  ++sectionCount_;
  return true;
}

bool QPackTxTable::sectionAck(uint64_t streamID)
{
  if (!sections) return false;
  auto section = sections->find(streamID);
  if (!section) return false;
  sections->del(streamID);
  --sectionCount_;
  return true;
}

bool QPackTxTable::streamCancellation(uint64_t streamID)
{
  return sectionAck(streamID);
}

bool QPackTxTable::sectionAdmissible(uint64_t streamID) const
{
  return sections && !sections->find(streamID) &&
    sectionCount_ < maxSections_;
}

bool QPackTxTable::applyDecoder(QPackInsn::T type, uint64_t value)
{
  switch (type) {
    case QPackInsn::SectionAck: return sectionAck(value);
    case QPackInsn::StreamCancellation: return streamCancellation(value);
    case QPackInsn::InsertCountIncrement:
      return insertCountIncrement(value);
    default: return false;
  }
}

bool QPack::staticNameIndex(ZuBSpan name, uint64_t &index)
{
  int i = qpackStaticNameIndex_(name);
  if (i < 0) return false;
  index = unsigned(i);
  return true;
}

int QPack::staticIndex(ZuBSpan name, ZuBSpan value)
{
  return qpackStaticIndex_(name, value);
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
      using Key = StaticKey<KV>;
      name = Key{}();
      ok = true;
    });
  return ok;
}

int QPack::decodeFieldSectionPrefix(
  ZuBSpan in, EncodedFieldSectionPrefix &prefix)
{
  prefix = {};
  unsigned o = 0;
  uint8_t baseFirst = 0;
  if (Compression::decodePref<8>(
	  in, o, prefix.encodedInsertCount) < 0 ||
	  Compression::decodePref<7>(
	in, o, prefix.deltaBase, &baseFirst) < 0)
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
  ZuBSpan in, FieldSectionPrefix &prefix, uint64_t insertCount,
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

int QPack::decodeEncoderInsn(ZuBSpan in, QPackDecodedInsn &i)
{
  unsigned o = 0;
  uint8_t first = 0;
  if (!in.length()) return -2;
  i = {};

  if (uint8_t(in[0]) & 0x80) {
    i.type = QPackInsn::InsertWithNameRef;
    int n = Compression::decodePref<6>(in, o, i.value, &first);
    if (n < 0) return n;
    i.nameRefDynamic = !(first & 0x40);
    if (o < in.length() && (uint8_t(in[o]) & 0x80))
      i.valueStorage.length(Compression::Huffman::declen(in.length() - o));
    if ((n = decodeString<7, 0x80>(
	  i.valueStorage.span(), in, o, i.header.value)) < 0)
      return n;
    return int(o);
  }
  if ((uint8_t(in[0]) & 0xc0) == 0x40) {
    i.type = QPackInsn::InsertWithoutNameRef;
    if (uint8_t(in[o]) & 0x20)
      i.nameStorage.length(Compression::Huffman::declen(in.length() - o));
    int n = decodeString<5, 0x20>(
	  i.nameStorage.span(), in, o, i.header.name);
    if (n < 0) return n;
    if (o < in.length() && (uint8_t(in[o]) & 0x80))
      i.valueStorage.length(Compression::Huffman::declen(in.length() - o));
    n = decodeString<7, 0x80>(
	  i.valueStorage.span(), in, o, i.header.value);
    if (n < 0) return n;
    return int(o);
  }
  if ((uint8_t(in[0]) & 0xe0) == 0x20) {
    i.type = QPackInsn::SetCapacity;
    int n = Compression::decodePref<5>(in, o, i.value);
    return n < 0 ? n : int(o);
  }
  if ((uint8_t(in[0]) & 0xe0) == 0x00) {
    i.type = QPackInsn::Duplicate;
    int n = Compression::decodePref<5>(in, o, i.value);
    return n < 0 ? n : int(o);
  }
  return -1;
}

int QPack::decodeDecoderInsn(ZuBSpan in, QPackDecodedInsn &i)
{
  unsigned o = 0;
  if (!in.length()) return -2;
  i = {};
  if (uint8_t(in[0]) & 0x80) {
    i.type = QPackInsn::SectionAck;
    int n = Compression::decodePref<7>(in, o, i.value);
    return n < 0 ? n : int(o);
  }
  if (uint8_t(in[0]) & 0x40) {
    i.type = QPackInsn::StreamCancellation;
    int n = Compression::decodePref<6>(in, o, i.value);
    return n < 0 ? n : int(o);
  }
  i.type = QPackInsn::InsertCountIncrement;
  int n = Compression::decodePref<6>(in, o, i.value);
  if (n < 0) return n;
  if (!i.value) return -1;
  return int(o);
}

}} // namespace Zhttp::H3
