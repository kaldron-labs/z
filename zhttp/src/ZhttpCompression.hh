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

#include <zlib/ZuBitStream.hh>
#include <zlib/ZuBox.hh>
#include <zlib/ZuPrint.hh>
#include <zlib/ZuTraits.hh>

namespace Zhttp {

namespace Compression {

template <typename U,
  bool = ZuTraits<U>::IsArray || ZuTraits<U>::IsString>
struct IsPrintString : public ZuFalse { };
template <typename U>
struct IsPrintString<U, true> : public ZuBool<
  sizeof(typename ZuTraits<U>::Elem) == 1> { };

struct PrintCount {
  void push(uint8_t) { ++n; }
  void skip(uint64_t n_) { n += n_; }
  uint64_t length() const { return n; }

  uint64_t n = 0;
};

template <typename Bytes, typename = void>
struct HasSkip : public ZuFalse { };
template <typename Bytes>
struct HasSkip<Bytes, decltype(
  ZuDeclVal<Bytes &>().skip(ZuDeclVal<uint64_t>()), void())> :
  public ZuTrue { };

template <typename Bytes>
class PrintBytes {
public:
  PrintBytes(Bytes &out) : m_out{out} { }

  PrintBytes &operator <<(char v) {
    m_out.push(uint8_t(v));
    return *this;
  }

  template <typename U>
  ZuIfT<IsPrintString<U>{}, PrintBytes &> operator <<(const U &v) {
    auto data = ZuTraits<U>::data(v);
    auto n = ZuTraits<U>::length(v);
    if constexpr (HasSkip<Bytes>{})
      m_out.skip(n);
    else
      for (decltype(n) i = 0; i < n; ++i)
	m_out.push(uint8_t(data[i]));
    return *this;
  }
  template <typename U>
  ZuIfT<!IsPrintString<U>{} && ZuPrint<U>::Delegate, PrintBytes &>
  operator <<(const U &v) {
    ZuPrint<U>::print(*this, v);
    return *this;
  }
  template <typename U>
  ZuIfT<!IsPrintString<U>{} && ZuPrint<U>::Buffer, PrintBytes &>
  operator <<(const U &v) {
    unsigned n = ZuPrint<U>::length(v);
    auto data = static_cast<char *>(ZuAlloca(n, 1));
    if (ZuUnlikely(!data && n)) {
      m_ok = false;
      return *this;
    }
    n = ZuPrint<U>::print(data, n, v);
    for (unsigned i = 0; i < n; ++i) m_out.push(uint8_t(data[i]));
    return *this;
  }
  template <typename U>
  ZuIfT<
    !ZuPrint<U>::OK && ZuTraits<U>::IsReal &&
      ZuTraits<U>::IsPrimitive && !ZuTraits<U>::IsArray,
    PrintBytes &>
  operator <<(U v) {
    return *this << ZuBoxed(v);
  }

  bool ok() const { return m_ok; }

private:
  Bytes	&m_out;
  bool	m_ok = true;
};

template <typename P>
uint64_t printLength(const P &v) {
  PrintCount count;
  PrintBytes<PrintCount>{count} << v;
  return count.length();
}

struct NameView {
  NameView() = default;
  NameView(ZuCSpan name_) : name{name_} { }

  bool equals(const NameView &v) const { return name == v.name; }
  int cmp(const NameView &v) const { return name.cmp(v.name); }
  uint32_t hash() const { return name.hash(); }

  ZuCSpan	name;
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
  uint64_t encode(ZuSpan<uint8_t>, ZuBSpan);

  ZuInline constexpr uint64_t declen(uint64_t slen) {
    return ((slen / 5U)<<3) + (((slen % 5U)<<3)/5U);
  }
  int64_t decode(ZuSpan<uint8_t>, ZuBSpan);

} // namespace Huffman

// incremental HPACK/QPACK prefix integer parser
struct PrefInt {
  template <unsigned Bits>
  int start(uint8_t first, uint64_t &value) {
    static_assert(Bits && Bits <= 8);
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
  ZuCSpan in, unsigned &offset, uint64_t &value,
  uint8_t *firstByte = nullptr)
{
  if (offset >= in.length()) return -2;
  uint8_t first = uint8_t(in[offset++]);
  if (firstByte) *firstByte = first;
  PrefInt decoder;
  int state = decoder.template start<Bits>(first, value);
  if (state < 0) return -1;
  if (state > 0) return 0;
  state = decoder.process(ZuBSpan{in}, offset, value);
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
int putString(
  Bytes &out, uint8_t prefix, unsigned bits, ZuCSpan value) {
  if (putPref(out, prefix, bits, value.length()) < 0) return -1;
  for (unsigned i = 0; i < value.length(); ++i)
    out.push(uint8_t(value[i]));
  return 0;
}

template <typename Bytes, typename P>
int putPrint(
  Bytes &out, uint8_t prefix, unsigned bits, const P &value) {
  uint64_t n = printLength(value);
  if (putPref(out, prefix, bits, n) < 0) return -1;
  if constexpr (HasSkip<Bytes>{}) {
    out.skip(n);
    return 0;
  } else {
    PrintBytes<Bytes> bytes{out};
    bytes << value;
    return bytes.ok() ? 0 : -1;
  }
}

template <unsigned PrefixBits, uint8_t HuffmanMask>
inline int decodeString(
  ZuSpan<uint8_t> storage, ZuCSpan in, unsigned &offset, ZuCSpan &out) {
  uint64_t length = 0;
  uint8_t first = 0;
  int n = decodePref<PrefixBits>(in, offset, length, &first);
  if (n < 0) return n;
  unsigned size = in.length();
  if (length > size - offset) return -2;
  ZuCSpan raw{&in[offset], unsigned(length)};
  offset += unsigned(length);
  if (!(first & HuffmanMask)) {
    out = raw;
    return int(raw.length());
  }
  unsigned decodedMax = Huffman::declen(raw.length());
  if (decodedMax > storage.length()) return -2;
  storage.trunc(decodedMax);
  int64_t decoded = Huffman::decode(storage, raw);
  if (decoded < 0) return -1;
  out = ZuCSpan{storage.data(), unsigned(decoded)};
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

  int finish(Bytes &storage, ZuCSpan &out) {
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
