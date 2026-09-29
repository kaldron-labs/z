//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z HTTP shared header-compression primitives

#ifndef ZhttpCompression_HH
#define ZhttpCompression_HH

#ifndef ZhttpLib_HH
#include <zlib/ZhttpLib.hh>
#endif

#include <limits.h>
#include <string.h>

#include <zlib/ZuBitStream.hh>
#include <zlib/ZuBox.hh>
#include <zlib/ZuPrint.hh>
#include <zlib/ZuString.hh>
#include <zlib/ZuTL.hh>
#include <zlib/ZuTraits.hh>

namespace Zhttp {

template <typename Key_, typename Value_ = void>
struct StaticEntry {
  using Key = Key_;
  using Value = Value_;
};

template <typename KV> using StaticKey = typename KV::Key;
template <typename KV> using StaticValue = typename KV::Value;

template <typename KV, bool = ZuIsSame<StaticValue<KV>, void>{}>
struct StaticMatchValue_ { using T = StaticValue<KV>; };
template <typename KV>
struct StaticMatchValue_<KV, true> { using T = ZuStringT<"">; };
template <typename KV>
using StaticMatchValue = typename StaticMatchValue_<KV>::T;

template <typename Tbl>
struct StaticTable {
  using Keys = ZuTypeMap<StaticKey, Tbl>;
  using Names = ZuTypeUnique<Keys>;

  template <typename Key>
  struct Entries_ {
    template <typename KV>
    using Is = ZuIsSame<Key, StaticKey<KV>>;
    using T = ZuTypeGrep<Is, Tbl>;
  };
  template <typename Key>
  using Entries = typename Entries_<Key>::T;
  template <typename Key>
  using Values = ZuTypeMap<StaticMatchValue, Entries<Key>>;
};

namespace Compression {

template <typename U,
  bool = ZuTraits<U>::IsArray || ZuTraits<U>::IsString>
struct IsPrintString : public ZuFalse { };
template <typename U>
struct IsPrintString<U, true> : public ZuBool<
  sizeof(typename ZuTraits<U>::Elem) == 1> { };

template <typename Bytes, typename = void>
struct HasBuffer : public ZuFalse { };
template <typename Bytes>
struct HasBuffer<Bytes, decltype(
  ZuDeclVal<Bytes &>().length(),
  ZuDeclVal<Bytes &>().length(ZuDeclVal<uint64_t>()),
  ZuDeclVal<Bytes &>().ensure(ZuDeclVal<uint64_t>()), void())> :
  public ZuTrue { };

template <typename Bytes>
class PrintBytes {
public:
  PrintBytes(Bytes &out) : m_out{out} { }

  PrintBytes &operator <<(char v) {
    if (m_ok) m_out.push(uint8_t(v));
    return *this;
  }

  template <typename U,
    typename = ZuIfT<
      (IsPrintString<U>{}) ||
      (!IsPrintString<U>{} && ZuPrint<U>::Delegate) ||
      (!IsPrintString<U>{} && ZuPrint<U>::Buffer)>>
  PrintBytes & operator <<(const U &v) {
    if (!m_ok) return *this;
    if constexpr (IsPrintString<U>{}) {
      auto data = ZuTraits<U>::data(v);
      auto n = ZuTraits<U>::length(v);
      if constexpr (HasBuffer<Bytes>{}) {
	uint64_t offset = m_out.length();
	auto ptr = m_out.ensure(offset + n);
	if (!ptr && n) { m_ok = false; return *this; }
	if (n) memcpy(ptr + offset, data, n);
	m_out.length(offset + n);
      }
      else
	for (decltype(n) i = 0; i < n; ++i)
	  m_out.push(uint8_t(data[i]));
      return *this;
    } else if constexpr (!IsPrintString<U>{} && ZuPrint<U>::Delegate) {
      ZuPrint<U>::print(*this, v);
      return *this;
    } else {
      unsigned n = ZuPrint<U>::length(v);
      if constexpr (HasBuffer<Bytes>{}) {
	uint64_t offset = m_out.length();
	auto ptr = m_out.ensure(offset + n);
	if (!ptr && n) { m_ok = false; return *this; }
	if (n) n = ZuPrint<U>::print(reinterpret_cast<char *>(ptr + offset), n, v);
	m_out.length(offset + n);
      } else {
	auto data = static_cast<char *>(ZuAlloca(n, 1));
	if (ZuUnlikely(!data && n)) {
	  m_ok = false;
	  return *this;
	}
	n = ZuPrint<U>::print(data, n, v);
	for (unsigned i = 0; i < n; ++i) m_out.push(uint8_t(data[i]));
      }
      return *this;
    }
  }
  template <typename U,
    typename = ZuIfT<!ZuPrint<U>::OK && ZuTraits<U>::IsReal &&
      ZuTraits<U>::IsPrimitive && !ZuTraits<U>::IsArray>>
  PrintBytes &
  operator <<(U v) {
    return *this << ZuBoxed(v);
  }

  bool ok() const { return m_ok; }

private:
  Bytes	&m_out;
  bool	m_ok = true;
};

template <typename Bytes>
int putPref(Bytes &, uint8_t, unsigned, uint64_t);

template <typename Bytes>
class FieldSectionBuffer {
public:
  enum { PrefixReserve = 16 };

  FieldSectionBuffer(Bytes &bytes) : m_bytes{bytes} { }

  Bytes &bytes() { return m_bytes; }
  const Bytes &bytes() const { return m_bytes; }

  template <typename P>
  int putPrint(
      uint8_t prefix, unsigned bits, const P &value,
      uint64_t &offset, unsigned &length) {
    if constexpr (ZuPrint<P>::Buffer) {
      uint64_t start_ = m_bytes.length();
      if (start_ > UINT32_MAX - PrefixReserve) return -1;
      unsigned size = ZuPrint<P>::length(value);
      m_bytes.length(start_ + PrefixReserve);
      offset = m_bytes.length();
      auto ptr = m_bytes.ensure(offset + size);
      if (!ptr && size) return -1;
      length = size ? ZuPrint<P>::print(
	reinterpret_cast<char *>(ptr + offset), size, value) : 0;
      if (length > size) return -1;
      m_bytes.length(offset + length);

      Prefix encoded;
      if (Compression::putPref(encoded, prefix, bits, length) < 0)
	return -1;
      unsigned gap = PrefixReserve - encoded.length;
      memcpy(m_bytes.data() + start_ + gap,
	encoded.data, encoded.length);
      record_(uint32_t(start_), uint8_t(gap));
      return 0;
    } else {
      uint64_t start_ = m_bytes.length();
      if (start_ > UINT32_MAX - PrefixReserve) return -1;
      m_bytes.length(start_ + PrefixReserve);
      offset = m_bytes.length();
      PrintBytes<Bytes> out{m_bytes};
      out << value;
      uint64_t length_ = m_bytes.length() - offset;
      if (!out.ok() || length_ > UINT_MAX) return -1;
      length = unsigned(length_);

      Prefix encoded;
      if (Compression::putPref(encoded, prefix, bits, length) < 0)
	return -1;
      unsigned gap = PrefixReserve - encoded.length;
      memcpy(m_bytes.data() + start_ + gap,
	encoded.data, encoded.length);
      record_(uint32_t(start_), uint8_t(gap));
      return 0;
    }
  }

  uint64_t length(uint64_t start = 0) const {
    return m_bytes.length() - start - m_gapLength;
  }

  template <typename L>
  void each(uint64_t start, L &&l) const {
    uint64_t offset = start;
    uint32_t link = m_head;
    while (link) {
      uint32_t gap = link - 1;
      uint32_t next;
      memcpy(&next, m_bytes.data() + gap, sizeof(next));
      unsigned length = m_bytes[gap + sizeof(next)];
      if (gap > offset)
	l(ZuBSpan{m_bytes.data() + offset, unsigned(gap - offset)});
      offset = uint64_t(gap) + length;
      link = next;
    }
    if (offset < m_bytes.length())
      l(ZuBSpan{
	m_bytes.data() + offset, unsigned(m_bytes.length() - offset)});
  }

private:
  struct Prefix {
    void push(uint8_t c) { data[length++] = c; }

    uint8_t	data[11];
    unsigned	length = 0;
  };

  void record_(uint32_t offset, uint8_t length) {
    uint32_t link = offset + 1;
    uint32_t end = 0;
    memcpy(m_bytes.data() + offset, &end, sizeof(end));
    m_bytes[offset + sizeof(end)] = length;
    if (m_tail)
      memcpy(m_bytes.data() + m_tail - 1, &link, sizeof(link));
    else
      m_head = link;
    m_tail = link;
    m_gapLength += length;
  }

  Bytes		&m_bytes;
  uint64_t	m_gapLength = 0;
  uint32_t	m_head = 0;
  uint32_t	m_tail = 0;
};

struct NameView {
  NameView() = default;
  NameView(ZuBSpan name_) : name{name_} { }

  bool equals(const NameView &v) const { return name == v.name; }
  int cmp(const NameView &v) const { return name.cmp(v.name); }
  uint32_t hash() const { return name.hash(); }

  ZuBSpan	name;
};

inline bool operator ==(const NameView &l, const NameView &r) {
  return l.equals(r);
}
inline int operator <=>(const NameView &l, const NameView &r) {
  return l.cmp(r);
}

namespace Huffman {

  using BitWriter = ZuBitStream::BE::Writer;
  using BitReader = ZuBitStream::BE::Reader;

  ZuInline constexpr uint64_t enclen(uint64_t slen) {
    return (slen>>2)*15U + (((slen & 3U)*30U + 7U)>>3);
  }
  ZhttpAPI uint64_t encode(ZuSpan<uint8_t>, ZuBSpan);

  ZuInline constexpr uint64_t declen(uint64_t slen) {
    return ((slen / 5U)<<3) + (((slen % 5U)<<3)/5U);
  }
  ZhttpAPI int64_t decode(ZuSpan<uint8_t>, ZuBSpan);

} // namespace Huffman

// incremental HPACK/QPACK prefix integer parser
struct PrefInt {
  template <unsigned Bits>
  int start(uint8_t first, uint64_t &value) {
    ZuAssert(Bits && Bits <= 8);
    enum { Mask = (1U << Bits) - 1U };
    m_value = first & Mask;
    m_shift = 0;
    m_more = m_value == Mask;
    value = m_value;
    return m_more ? 0 : 1;
  }

  int process(ZuBSpan in, unsigned &offset, uint64_t &value) {
    unsigned n = in.length();
    while (m_more && offset < n) {
      uint8_t byte = in[offset++];
      uint64_t part = byte & 0x7fU;
      if (m_shift >= 64 ||
	  part > (uint64_t(-1) - m_value) >> m_shift)
	return -1;
      m_value += part << m_shift;
      if (!(byte & 0x80)) {
	m_more = false;
	value = m_value;
	return 1;
      }
      if (m_shift > 56) return -1;
      m_shift += 7;
    }
    value = m_value;
    return m_more ? 0 : 1;
  }

private:
  uint64_t	m_value = 0;
  unsigned	m_shift = 0;
  bool		m_more = false;
};

template <unsigned Bits>
inline int decodePref(
  ZuBSpan in, unsigned &offset, uint64_t &value,
  uint8_t *firstByte = nullptr)
{
  if (offset >= in.length()) return -2;
  uint8_t first = uint8_t(in[offset++]);
  if (firstByte) *firstByte = first;
  PrefInt decoder;
  int state = decoder.template start<Bits>(first, value);
  if (state < 0) return -1;
  if (state > 0) return 0;
  state = decoder.process(in, offset, value);
  return state > 0 ? 0 : state < 0 ? -1 : -2;
}

template <typename Bytes>
int putPref(Bytes &out, uint8_t prefix, unsigned bits, uint64_t value) {
  if (!bits || bits > 8) return -1;
  uint8_t mask = uint8_t((1U << bits) - 1U);
  if (value < mask) {
    out.push(prefix | uint8_t(value));
    return 0;
  }
  out.push(prefix | mask);
  value -= mask;
  while (value >= 128) {
    out.push(uint8_t((value & 0x7f) | 0x80));
    value >>= 7;
  }
  out.push(uint8_t(value));
  return 0;
}

template <typename Bytes>
void putBytes(Bytes &out, ZuBSpan value) {
  if constexpr (HasBuffer<Bytes>{}) {
    uint64_t offset = out.length();
    auto ptr = out.ensure(offset + value.length()) + offset;
    if (value) memcpy(ptr, value.data(), value.length());
    out.length(offset + value.length());
  } else {
    for (unsigned i = 0, n = value.length(); i < n; ++i)
      out.push(uint8_t(value[i]));
  }
}

template <typename Bytes>
int putString(
  Bytes &out, uint8_t prefix, unsigned bits, ZuBSpan value) {
  if (putPref(out, prefix, bits, value.length()) < 0) return -1;
  putBytes(out, value);
  return 0;
}

template <unsigned PrefixBits, uint8_t HuffmanMask>
inline int decodeString(
  ZuSpan<uint8_t> storage, ZuBSpan in, unsigned &offset, ZuBSpan &out) {
  uint64_t length = 0;
  uint8_t first = 0;
  int n = decodePref<PrefixBits>(in, offset, length, &first);
  if (n < 0) return n;
  unsigned size = in.length();
  if (length > size - offset) return -2;
  ZuBSpan raw{&in[offset], unsigned(length)};
  offset += unsigned(length);
  if (!(first &HuffmanMask)) {
    out = raw;
    return int(raw.length());
  }
  unsigned decodedMax = Huffman::declen(raw.length());
  if (decodedMax > storage.length()) return -2;
  storage.trunc(decodedMax);
  int64_t decoded = Huffman::decode(storage, raw);
  if (decoded < 0) return -1;
  out = {storage.data(), unsigned(decoded)};
  return int(out.length());
}

template <typename Bytes>
class StringDecoder {
public:
  template <unsigned PrefixBits, uint8_t HuffmanMask>
  int start(uint8_t first, uint64_t maxLength) {
    m_bytes.length(0);
    m_length = 0;
    m_maxLength = maxLength;
    m_huffman = first & HuffmanMask;
    m_lengthReady = false;
    m_complete = false;
    int state = m_prefix.template start<PrefixBits>(first, m_length);
    if (state < 0) return -1;
    if (state > 0) return length_();
    return 0;
  }

  int process(ZuBSpan in, unsigned &offset) {
    if (m_complete) return 1;
    if (!m_lengthReady) {
      int state = m_prefix.process(in, offset, m_length);
      if (state < 0) return -1;
      if (!state) return 0;
      if (length_() < 0) return -1;
    }
    uint64_t remaining = m_length - m_bytes.length();
    unsigned available = in.length() - offset;
    if (remaining < available) available = unsigned(remaining);
    for (unsigned i = 0; i < available; ++i)
      m_bytes.push(in[offset + i]);
    offset += available;
    if (m_bytes.length() == m_length) {
      m_complete = true;
      return 1;
    }
    return 0;
  }

  int finish(Bytes &storage, ZuBSpan &out) {
    if (!m_complete) return -1;
    if (!m_huffman) {
      out = m_bytes;
      return int(out.length());
    }
    storage.length(Huffman::declen(m_bytes.length()));
    int64_t decoded = Huffman::decode(storage.span(), m_bytes);
    if (decoded < 0 || uint64_t(decoded) > m_maxLength) return -1;
    storage.length(uint64_t(decoded));
    out = storage;
    return int(out.length());
  }

private:
  int length_() {
    uint64_t decodedMax =
      m_huffman ? Huffman::declen(m_length) : m_length;
    if (decodedMax > m_maxLength) return -1;
    m_lengthReady = true;
    if (!m_length) m_complete = true;
    return m_complete ? 1 : 0;
  }

  Bytes		m_bytes;
  PrefInt	m_prefix;
  uint64_t	m_length = 0;
  uint64_t	m_maxLength = 0;
  bool		m_huffman = false;
  bool		m_lengthReady = false;
  bool		m_complete = false;
};

} // namespace Compression

} // namespace Zhttp

#endif /* ZhttpCompression_HH */
