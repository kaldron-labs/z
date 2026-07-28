//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZhttpHPack.hh>

namespace Zhttp { namespace H2 {

static constexpr const char *hpackStatic_[][2] = {
  {":authority", ""}, {":method", "GET"}, {":method", "POST"},
  {":path", "/"}, {":path", "/index.html"}, {":scheme", "http"},
  {":scheme", "https"}, {":status", "200"}, {":status", "204"},
  {":status", "206"}, {":status", "304"}, {":status", "400"},
  {":status", "404"}, {":status", "500"}, {"accept-charset", ""},
  {"accept-encoding", "gzip, deflate"}, {"accept-language", ""},
  {"accept-ranges", ""}, {"accept", ""},
  {"access-control-allow-origin", ""}, {"age", ""}, {"allow", ""},
  {"authorization", ""}, {"cache-control", ""},
  {"content-disposition", ""}, {"content-encoding", ""},
  {"content-language", ""}, {"content-length", ""},
  {"content-location", ""}, {"content-range", ""},
  {"content-type", ""}, {"cookie", ""}, {"date", ""}, {"etag", ""},
  {"expect", ""}, {"expires", ""}, {"from", ""}, {"host", ""},
  {"if-match", ""}, {"if-modified-since", ""}, {"if-none-match", ""},
  {"if-range", ""}, {"if-unmodified-since", ""}, {"last-modified", ""},
  {"link", ""}, {"location", ""}, {"max-forwards", ""},
  {"proxy-authenticate", ""}, {"proxy-authorization", ""},
  {"range", ""}, {"referer", ""}, {"refresh", ""}, {"retry-after", ""},
  {"server", ""}, {"set-cookie", ""}, {"strict-transport-security", ""},
  {"transfer-encoding", ""}, {"user-agent", ""}, {"vary", ""},
  {"via", ""}, {"www-authenticate", ""}
};

static_assert(sizeof(hpackStatic_) / sizeof(hpackStatic_[0]) == 61);

static uint32_t entrySize_(ZuCSpan name, ZuCSpan value)
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

bool HPackTable::insert(ZuCSpan name, ZuCSpan value)
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

bool HPack::staticField(uint64_t index, Field &field)
{
  if (!index || index > 61) return false;
  auto &entry = hpackStatic_[index - 1];
  field = {entry[0], entry[1]};
  return true;
}

int HPack::staticIndex(ZuCSpan name, ZuCSpan value)
{
  for (unsigned i = 0; i < 61; ++i)
    if (name == hpackStatic_[i][0] && value == hpackStatic_[i][1])
      return int(i + 1);
  return -1;
}

int HPack::staticNameIndex(ZuCSpan name)
{
  for (unsigned i = 0; i < 61; ++i)
    if (name == hpackStatic_[i][0]) return int(i + 1);
  return -1;
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
  m_value.length(0);
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

bool HPackDecoder::indexed_(uint64_t index, Field &field)
{
  if (!m_table.lookup(index, field)) {
    fail_(HPackFailure::Index);
    return false;
  }
  return account_(field);
}

int HPackDecoder::string_(
  ZuCSpan input, unsigned &offset, unsigned bits, uint8_t huffman,
  HPackString &out)
{
  uint64_t length = 0;
  uint8_t first = 0;
  int n = Compression::decodePref(
    input, offset, bits, length, &first);
  if (n < 0) return n;
  if (length > input.length() - offset) return -2;
  ZuCSpan raw{input.data() + offset, unsigned(length)};
  offset += unsigned(length);
  if (!(first & huffman)) {
    out = raw;
    return int(length);
  }
  out.length(Compression::Huffman::declen(length));
  int64_t decoded = Compression::Huffman::decode(
    ZuSpan<uint8_t>{out.span()},
    raw);
  if (decoded < 0) return -1;
  out.length(uint64_t(decoded));
  return int(decoded);
}

int HPackDecoder::literal_(
  ZuCSpan input, unsigned &offset, unsigned bits,
  bool indexing, Field &field)
{
  uint64_t index = 0;
  int state = Compression::decodePref(input, offset, bits, index);
  if (state == -2) return 0;
  if (state < 0) return fail_(HPackFailure::Integer);
  if (index) {
    Field indexed;
    if (!m_table.lookup(index, indexed)) {
      return fail_(HPackFailure::Index);
    }
    m_name = indexed.name;
  } else {
    state = string_(input, offset, 7, 0x80, m_name);
    if (state == -2) return 0;
    if (state < 0) return fail_(HPackFailure::String);
  }
  state = string_(input, offset, 7, 0x80, m_value);
  if (state == -2) return 0;
  if (state < 0) return fail_(HPackFailure::String);
  field = {m_name, m_value};
  if (!account_(field)) return -1;
  if (indexing && !m_table.insert(field.name, field.value)) {
    return fail_(HPackFailure::Capacity);
  }
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
  ZuCSpan input, unsigned &offset, Field &field, bool &emitted)
{
  if (offset >= input.length()) return 0;
  unsigned start = offset;
  uint8_t first = uint8_t(input[offset]);
  uint64_t value = 0;
  emitted = false;
  if (first & 0x80) {
    int n = Compression::decodePref(input, offset, 7, value);
    if (n == -2) { offset = start; return 0; }
    if (n < 0) return fail_(HPackFailure::Integer);
    m_capacityAllowed = false;
    if (!indexed_(value, field)) return -1;
    emitted = true;
    return 1;
  }
  if ((first & 0xe0) == 0x20) {
    if (!m_capacityAllowed) return fail_(HPackFailure::Capacity);
    int n = Compression::decodePref(input, offset, 5, value);
    if (n == -2) { offset = start; return 0; }
    if (n < 0) return fail_(HPackFailure::Integer);
    if (value > m_maxCapacity || !m_table.capacity(uint32_t(value)))
      return fail_(HPackFailure::Capacity);
    return 1;
  }
  m_capacityAllowed = false;
  bool indexing = (first & 0xc0) == 0x40;
  unsigned bits = indexing ? 6 : 4;
  int state = literal_(input, offset, bits, indexing, field);
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
  m_maxCapacity = capacity;
  if (!m_table.capacity(capacity)) return false;
  neverIndex("authorization");
  neverIndex("cookie");
  neverIndex("set-cookie");
  return true;
}

void HPackEncoder::reset()
{
}

void HPackEncoder::final()
{
  m_table.reset();
  m_table.capacity(0);
  m_neverIndex.length(0);
  m_maxCapacity = 0;
}

void HPackEncoder::neverIndex(ZuCSpan name)
{
  if (!neverIndexed(name)) new (m_neverIndex.push()) HPackString{name};
}

bool HPackEncoder::neverIndexed(ZuCSpan name) const
{
  for (unsigned i = 0; i < m_neverIndex.length(); ++i)
    if (m_neverIndex[i] == name) return true;
  return false;
}

}} // namespace Zhttp::H2
