//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuMatcher.hh>
#include <zlib/ZuSwitch.hh>

#include <zlib/ZmAssert.hh>

#include <zlib/ZhttpHPack.hh>

namespace Zhttp { namespace H2 {

ZtEnumImplStruct(HPackFailure);
ZtEnumImplStruct(HPackRep);

using HPackStatic_ = StaticTable<HPackTbl>;

static int hpackStaticName_(ZuBSpan value)
{
  static constexpr auto matcher = ZuMatcher<HPackStatic_::Names>();
  return matcher.exact(value);
}

static int hpackStaticNameIndex_(ZuBSpan value)
{
  int i = hpackStaticName_(value);
  if (i < 0) return -1;
  int index = -1;
  ZuSwitch::dispatch<HPackStatic_::Names::N>(
    unsigned(i), [&index](auto i) {
      using Key = ZuType<i, HPackStatic_::Names>;
      index = ZuTypeIndex<Key, HPackStatic_::Keys>{};
    });
  return index;
}

static int hpackStaticIndex_(ZuBSpan name, ZuBSpan value)
{
  int i = hpackStaticName_(name);
  if (i < 0) return -1;
  int index = -1;
  ZuSwitch::dispatch<HPackStatic_::Names::N>(
    unsigned(i), [&index, &value](auto nameIndex) {
      using Key = ZuType<nameIndex, HPackStatic_::Names>;
      using KeyEntries = HPackStatic_::Entries<Key>;
      using KeyValues = HPackStatic_::Values<Key>;
      static constexpr auto matcher = ZuMatcher<KeyValues>();
      int j = matcher.exact(value);
      if (j < 0) return;
      ZuSwitch::dispatch<KeyEntries::N>(
	unsigned(j), [&index, nameIndex](auto valueIndex) {
	(void)nameIndex; // gcc bug requires the capture; clang warns if unused
	using KV = ZuType<valueIndex, KeyEntries>;
	index = ZuTypeIndex<KV, HPackTbl>{};
      });
    });
  return index;
}

ZuAssert(HPackTbl::N == 61);

static uint32_t entrySize_(ZuBSpan name, ZuBSpan value)
{
  uint64_t size = uint64_t(name.length()) + value.length() + 32;
  return size > uint32_t(-1) ? uint32_t(-1) : uint32_t(size);
}

bool HPackTable::capacity(uint32_t value)
{
  m_capacity = value;
  evict_();
  return m_used <= m_capacity;
}

bool HPackTable::insert(ZuBSpan name, ZuBSpan value)
{
  uint32_t size = entrySize_(name, value);
  if (size > m_capacity) {
    reset();
    return true;
  }
  while (m_used > m_capacity - size) {
    if (m_head >= m_entries.length()) return false;
    m_used -= m_entries[m_head++].size;
  }
  compact_();
  auto entry = m_entries.push();
  new (entry) HPackEntry{
    .name = HPackString{name},
    .value = HPackString{value},
    .size = size
  };
  m_used += size;
  return true;
}

bool HPackTable::lookup(uint64_t index, Field &field) const
{
  if (!index) return false;
  if (index <= 61) return HPack::staticField(index, field);
  uint64_t relative = index - 62;
  unsigned count_ = count();
  if (relative >= count_) return false;
  auto &entry = m_entries[m_head + count_ - 1 - unsigned(relative)];
  field = {entry.name, entry.value};
  return true;
}

void HPackTable::reset()
{
  m_entries.length(0);
  m_head = 0;
  m_used = 0;
}

void HPackTable::evict_()
{
  while (m_used > m_capacity && m_head < m_entries.length())
    m_used -= m_entries[m_head++].size;
  compact_();
}

void HPackTable::compact_()
{
  if (!m_head) return;
  m_entries.shift(m_head);
  m_head = 0;
}

bool HPackTxTable::init(uint32_t capacity)
{
  final();
  m_localCapacity = capacity;
  uint32_t n = capacity>>5;
  if (n) {
    m_entries.ensure(n);
    m_exact = new HPackTxExact{ZmHashParams{n}};
    m_names = new HPackTxNames{ZmHashParams{n}};
  }
  m_capacity = capacity < 4096 ? capacity : 4096;
  return m_entries.size() >= n &&
    (!m_exact || m_exact->size() >= n) &&
    (!m_names || m_names->size() >= n);
}

void HPackTxTable::final()
{
  m_names = nullptr;
  m_exact = nullptr;
  m_entries.init();
  m_head = 0;
  m_insertCount = 0;
  m_capacity = 0;
  m_localCapacity = 0;
  m_used = 0;
}

bool HPackTxTable::capacity(uint32_t capacity)
{
  if (capacity > m_localCapacity) return false;
  m_capacity = capacity;
  while (m_head < m_entries.length() && m_used > m_capacity)
    if (!dropOldest_()) return false;
  return m_used <= m_capacity;
}

const HPackTxEntry *HPackTxTable::findAbs_(uint64_t abs) const
{
  if (m_head >= m_entries.length()) return nullptr;
  uint64_t base = m_entries[m_head].abs;
  uint64_t count_ = m_entries.length() - m_head;
  if (abs < base || abs >= base + count_) return nullptr;
  const auto &entry = m_entries[m_head + unsigned(abs - base)];
  return entry.abs == abs ? &entry : nullptr;
}

const HPackTxEntry *HPackTxTable::find(Field field) const
{
  if (!m_exact) return nullptr;
  auto indexed = m_exact->find(field);
  if (!indexed) return nullptr;
  auto entry = findAbs_(indexed->abs);
  if (!entry) return nullptr;
  return Field{entry->name, entry->value} == field ? entry : nullptr;
}

const HPackTxEntry *HPackTxTable::findName(ZuBSpan name) const
{
  if (!m_names) return nullptr;
  auto indexed = m_names->find(Compression::NameView{name});
  if (!indexed) return nullptr;
  auto entry = findAbs_(indexed->abs);
  return entry && entry->name == name ? entry : nullptr;
}

bool HPackTxTable::insert(Field field)
{
  uint64_t n_ =
    uint64_t(field.name.length()) + field.value.length() + 32;
  if (n_ > m_capacity) return false;
  uint32_t n = uint32_t(n_);
  while (m_head < m_entries.length() && m_used + n > m_capacity)
    if (!dropOldest_()) return false;
  if (m_head && m_entries.length() == m_entries.size()) compact_();
  if (!m_exact || !m_names || m_entries.length() >= m_entries.size())
    return false;

  uint64_t abs = m_insertCount;
  auto entry = new (m_entries.push()) HPackTxEntry();
  entry->abs = abs;
  entry->size = n;
  entry->name = field.name;
  entry->value = field.value;

  Field key{entry->name, entry->value};
  if (!m_exact->add(HPackTxExactEntry{abs, key})) {
    m_entries.length(m_entries.length() - 1);
    return false;
  }
  auto name = const_cast<HPackTxNameEntry *>(
      m_names->find(Compression::NameView{entry->name}));
  if (name) {
    name->abs = abs;
    name->key = Compression::NameView{entry->name};
  } else if (!m_names->add(
      HPackTxNameEntry{abs, Compression::NameView{entry->name}})) {
    m_exact->del(key);
    m_entries.length(m_entries.length() - 1);
    return false;
  }

  ZmAssert(!m_exact->resized());
  ZmAssert(!m_names->resized());
  m_used += n;
  ++m_insertCount;
  return true;
}

bool HPackTxTable::dropOldest_()
{
  if (m_head >= m_entries.length()) return false;
  const auto &entry = m_entries[m_head];
  m_used -= entry.size;
  m_exact->del(Field{entry.name, entry.value});
  auto name = m_names->find(Compression::NameView{entry.name});
  if (name && name->abs == entry.abs)
    m_names->del(Compression::NameView{entry.name});
  ++m_head;
  if (m_head == m_entries.length()) {
    m_entries.length(0);
    m_head = 0;
  } else if (m_head >= 64 && m_head >= (m_entries.length()>>1))
    compact_();
  return true;
}

void HPackTxTable::compact_()
{
  if (!m_head) return;
  m_entries.splice(0, m_head);
  m_head = 0;
  rebuild_();
}

void HPackTxTable::rebuild_()
{
  if (!m_exact || !m_names) return;
  m_exact->clean();
  m_names->clean();
  for (unsigned i = m_head; i < m_entries.length(); ++i) {
    auto &entry = m_entries[i];
    if (!m_exact->add(HPackTxExactEntry{
	  entry.abs, {entry.name, entry.value}})) {
      ZmAssert(false);
      return;
    }
    auto name = const_cast<HPackTxNameEntry *>(
	m_names->find(Compression::NameView{entry.name}));
    if (name) {
      name->abs = entry.abs;
      name->key = Compression::NameView{entry.name};
    } else if (!m_names->add(HPackTxNameEntry{
	  entry.abs, Compression::NameView{entry.name}})) {
      ZmAssert(false);
      return;
    }
  }
  ZmAssert(!m_exact->resized());
  ZmAssert(!m_names->resized());
}

bool HPack::staticField(uint64_t index, Field &field)
{
  if (!index || index > HPackTbl::N) return false;
  ZuSwitch::dispatch<HPackTbl::N>(unsigned(index - 1), [&field](auto i) {
    using KV = ZuType<i, HPackTbl>;
    using Key = StaticKey<KV>;
    using Value = StaticValue<KV>;
    field = {Key{}(), Value{}()};
  });
  return true;
}

int HPack::staticIndex(ZuBSpan name, ZuBSpan value)
{
  int index = hpackStaticIndex_(name, value);
  return index < 0 ? -1 : index + 1;
}

int HPack::staticNameIndex(ZuBSpan name)
{
  int index = hpackStaticNameIndex_(name);
  return index < 0 ? -1 : index + 1;
}

bool HPackDecoder::init(uint32_t capacity, uint64_t maxHeaderListSize)
{
  final();
  m_maxCapacity = capacity;
  m_maxHeaderListSize = maxHeaderListSize;
  return m_table.capacity(capacity);
}

void HPackDecoder::reset()
{
  m_pending.length(0);
  m_name.length(0);
  m_headerListSize = 0;
  m_failure = HPackFailure::None;
  m_capacityAllowed = true;
}

void HPackDecoder::final()
{
  reset();
  m_table.reset();
  m_table.capacity(0);
  m_maxCapacity = 0;
  m_maxHeaderListSize = 0;
}

int HPackDecoder::fail_(HPackFailure::T failure)
{
  if (m_failure == HPackFailure::None) m_failure = failure;
  return -1;
}

bool HPackDecoder::indexed_(uint64_t index, Decoded &decoded)
{
  Field field;
  if (!m_table.lookup(index, field)) {
    fail_(HPackFailure::Index);
    return false;
  }
  decoded.name = field.name;
  decoded.indexedValue = field.value;
  decoded.indexed = true;
  return true;
}

template <unsigned Bits, uint8_t Huffman>
int HPackDecoder::string_(
  ZuBSpan input, unsigned &offset, HPackString &out)
{
  uint64_t length = 0;
  uint8_t first = 0;
  int n = Compression::decodePref<Bits>(
    input, offset, length, &first);
  if (n < 0) return n;
  unsigned size = input.length();
  if (length > size - offset) return -2;
  ZuBSpan raw{&input[offset], unsigned(length)};
  offset += unsigned(length);
  if (!(first & Huffman)) {
    out = raw;
    return int(length);
  }
  out.length(Compression::Huffman::declen(length));
  int64_t decoded = Compression::Huffman::decode(
    out.span(),
    raw);
  if (decoded < 0) return -1;
  out.length(uint64_t(decoded));
  return int(decoded);
}

template <unsigned Bits, uint8_t Huffman>
int HPackDecoder::value_(
  ZuSpan<uint8_t> input, unsigned &offset,
  ZuSpan<uint8_t> &out, bool &huffman)
{
  uint64_t length = 0;
  uint8_t first = 0;
  int n = Compression::decodePref<Bits>(
    input, offset, length, &first);
  if (n < 0) return n;
  unsigned size = input.length();
  if (length > size - offset) return -2;
  out = {&input[offset], unsigned(length)};
  offset += unsigned(length);
  huffman = first & Huffman;
  return int(length);
}

template <unsigned Bits, bool Indexing>
int HPackDecoder::literal_(
  ZuSpan<uint8_t> input, unsigned &offset, Decoded &decoded)
{
  uint64_t index = 0;
  int state = Compression::decodePref<Bits>(input, offset, index);
  if (state == -2) return 0;
  if (state < 0) return fail_(HPackFailure::Integer);
  if (index) {
    Field indexed;
    if (!m_table.lookup(index, indexed)) {
      return fail_(HPackFailure::Index);
    }
    decoded.name = indexed.name;
  } else {
    state = string_<7, 0x80>(input, offset, m_name);
    if (state == -2) return 0;
    if (state < 0) return fail_(HPackFailure::String);
    decoded.name = m_name;
  }
  state = value_<7, 0x80>(
    input, offset, decoded.value, decoded.huffman);
  if (state == -2) return 0;
  if (state < 0) return fail_(HPackFailure::String);
  decoded.indexing = Indexing;
  return 1;
}

bool HPackDecoder::account_(Field field)
{
  uint64_t size =
    uint64_t(field.name.length()) + field.value.length() + 32;
  if (m_headerListSize > m_maxHeaderListSize ||
      size > m_maxHeaderListSize - m_headerListSize) {
    fail_(HPackFailure::HeaderList);
    return false;
  }
  m_headerListSize += size;
  return true;
}

int HPackDecoder::decode_(
  ZuSpan<uint8_t> input,
  unsigned &offset, Decoded &decoded, bool &emitted)
{
  if (offset >= input.length()) return 0;
  unsigned start = offset;
  uint8_t first = uint8_t(input[offset]);
  uint64_t value = 0;
  emitted = false;
  if (first & 0x80) {
    int n = Compression::decodePref<7>(input, offset, value);
    if (n == -2) { offset = start; return 0; }
    if (n < 0) return fail_(HPackFailure::Integer);
    m_capacityAllowed = false;
    if (!indexed_(value, decoded)) return -1;
    emitted = true;
    return 1;
  }
  if ((first & 0xe0) == 0x20) {
    if (!m_capacityAllowed) return fail_(HPackFailure::Capacity);
    int n = Compression::decodePref<5>(input, offset, value);
    if (n == -2) { offset = start; return 0; }
    if (n < 0) return fail_(HPackFailure::Integer);
    if (value > m_maxCapacity || !m_table.capacity(uint32_t(value)))
      return fail_(HPackFailure::Capacity);
    return 1;
  }
  m_capacityAllowed = false;
  int state = (first & 0xc0) == 0x40 ?
    literal_<6, true>(input, offset, decoded) :
    literal_<4, false>(input, offset, decoded);
  if (state <= 0) {
    if (state < 0) return -1;
    offset = start;
    return 0;
  }
  emitted = true;
  return 1;
}

bool HPackDecoder::finish()
{
  if (m_pending) {
    fail_(HPackFailure::Truncated);
    return false;
  }
  return m_failure == HPackFailure::None;
}

bool HPackEncoder::init(uint32_t capacity)
{
  final();
  m_localCapacity = capacity;
  m_peerCapacity = 4096;
  m_signalledCapacity = 4096;
  if (!m_table.init(capacity)) return false;
  m_seedState = HPackSeedState::Cold;
  uint32_t effective = capacity < 4096 ? capacity : 4096;
  if (effective != m_signalledCapacity) pending_(effective);
  return true;
}

void HPackEncoder::reset()
{
}

void HPackEncoder::final()
{
  m_table.final();
  m_updateBytes.length(0);
  detachNeverIndex_();
  m_neverIndex->clean();
  m_generation = 0;
  m_localCapacity = 0;
  m_peerCapacity = 4096;
  m_signalledCapacity = 4096;
  m_pendingMin = 0;
  m_pendingFinal = 0;
  m_plans.length(0);
  m_plan = nullptr;
  m_seedCapacity = 0;
  m_seedState = HPackSeedState::Cold;
  m_pending = false;
}

bool HPackEncoder::peerCapacity(uint32_t capacity)
{
  m_peerCapacity = capacity;
  uint32_t effective =
    m_localCapacity < capacity ? m_localCapacity : capacity;
  if (!m_table.capacity(effective)) return false;
  if (m_seedState == HPackSeedState::Frozen && effective < m_seedCapacity)
    m_seedCapacity = effective;
  if (effective != m_signalledCapacity || m_pending) pending_(effective);
  return true;
}

HPackPlan HPackEncoder::plan(Field field) const
{
  HPackPlan plan_{.field = field};
  bool never = neverIndexed(field.name);
  if (!never) {
    int index = HPack::staticIndex(field.name, field.value);
    if (index > 0) {
      plan_.index = unsigned(index);
      plan_.rep = HPackRep::Indexed;
      return plan_;
    }
  }
  if (!never)
    if (auto entry = m_table.find(field)) {
      plan_.index =
	62 + m_table.insertCount() - entry->abs - 1;
      plan_.rep = HPackRep::Indexed;
      return plan_;
    }

  int nameIndex = HPack::staticNameIndex(field.name);
  if (nameIndex > 0)
    plan_.index = unsigned(nameIndex);
  else if (auto entry = m_table.findName(field.name))
    plan_.index = 62 + m_table.insertCount() - entry->abs - 1;

  if (never) {
    plan_.rep = HPackRep::NeverIndexed;
    return plan_;
  }
  uint64_t size =
    uint64_t(field.name.length()) + field.value.length() + 32;
  plan_.rep = m_table.capacity() && size <= m_table.capacity() ?
    HPackRep::Incremental : HPackRep::NonIndexed;
  return plan_;
}

HPackUpdates HPackEncoder::updates() const
{
  HPackUpdates updates{.generation = m_generation};
  if (!m_pending) return updates;
  updates.first = m_pendingMin;
  updates.count = 1;
  if (m_pendingFinal != m_pendingMin) {
    updates.second = m_pendingFinal;
    updates.count = 2;
  }
  return updates;
}

void HPackEncoder::commit(const HPackPlan &plan)
{
  if (plan.rep != HPackRep::Incremental) return;
  bool ok = m_table.insert(plan.field);
  ZmAssert(ok);
  (void)ok;
}

void HPackEncoder::commit(const HPackUpdates &updates)
{
  if (!updates.count) return;
  m_signalledCapacity =
    updates.count == 2 ? updates.second : updates.first;
  if (updates.generation != m_generation) return;
  m_pending = false;
  m_pendingMin = 0;
  m_pendingFinal = 0;
}

void HPackEncoder::pending_(uint32_t capacity)
{
  ++m_generation;
  if (!m_pending) {
    m_pendingMin = m_pendingFinal = capacity;
    m_pending = true;
    prepareUpdates_();
    return;
  }
  if (capacity < m_pendingMin) m_pendingMin = capacity;
  m_pendingFinal = capacity;
  prepareUpdates_();
}

void HPackEncoder::prepareUpdates_()
{
  m_updateBytes.length(0);
  if (!m_pending) return;
  (void)Compression::putPref(m_updateBytes, 0x20, 5, m_pendingMin);
  if (m_pendingFinal != m_pendingMin)
    (void)Compression::putPref(m_updateBytes, 0x20, 5, m_pendingFinal);
}

bool HPackEncoder::beginBlock(unsigned plan, bool &bootstrap)
{
  bootstrap = false;
  switch (m_seedState) {
    case HPackSeedState::Warm:
      m_plan = plan < m_plans.length() ? &m_plans[plan] : nullptr;
      m_seedState = HPackSeedState::Reserved;
      bootstrap = true;
      return true;
    case HPackSeedState::Frozen:
    case HPackSeedState::Disabled:
      return true;
    case HPackSeedState::Cold:
    case HPackSeedState::Reserved:
      return false;
  }
  return false;
}

void HPackEncoder::commitBlock()
{
  if (m_seedState == HPackSeedState::Reserved) {
    if (m_plan) m_table = ZuMv(m_plan->table);
    m_seedState = HPackSeedState::Frozen;
  }
}

void HPackEncoder::rollbackBlock()
{
  if (m_seedState == HPackSeedState::Reserved)
    m_seedState = HPackSeedState::Warm;
}

void HPackEncoder::commitUpdates()
{
  auto updates_ = updates();
  commit(updates_);
  m_updateBytes.length(0);
}

void HPackEncoder::neverIndex(ZuBSpan name)
{
  if (!m_neverIndex->find(name)) {
    detachNeverIndex_();
    m_neverIndex->add(HPackString{name}, true);
  }
}

bool HPackEncoder::neverIndexed(ZuBSpan name) const
{
  static constexpr auto matcher =
    ZuMatcher<"authorization", "cookie", "set-cookie">();
  if (matcher.exact(name) >= 0) return true;
  return m_neverIndex->find(name);
}

uint64_t HPackEncoder::nameIndex(ZuBSpan name) const
{
  int index = HPack::staticNameIndex(name);
  if (index > 0) return unsigned(index);
  const auto &lookup = m_table;
  if (auto entry = lookup.findName(name))
    return 62 + lookup.insertCount() - entry->abs - 1;
  return 0;
}

void HPackEncoder::detachNeverIndex_()
{
  if (m_neverIndex->refCount() <= 1) return;
  ZmRef<HPackNameSet> copy{new HPackNameSet};
  auto i = m_neverIndex->iter();
  while (auto entry = i()) copy->add(entry->key(), true);
  m_neverIndex = ZuMv(copy);
}

}} // namespace Zhttp::H2
