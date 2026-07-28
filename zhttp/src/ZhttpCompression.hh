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
#include <zlib/ZuTraits.hh>

namespace Zhttp {

namespace Compression {

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

struct PrefInt {
  int start(uint8_t first, unsigned bits, uint64_t &value) {
    if (!bits || bits > 8) return -1;
    uint8_t mask = uint8_t((1U << bits) - 1U);
    m_value = first & mask;
    m_shift = 0;
    m_more = m_value == mask;
    value = m_value;
    return m_more ? 0 : 1;
  }

  int process(ZuBSpan in, unsigned &offset, uint64_t &value) {
    while (m_more && offset < in.length()) {
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

inline int decodePref(
  ZuCSpan in, unsigned &offset, unsigned bits, uint64_t &value,
  uint8_t *firstByte = nullptr)
{
  if (offset >= in.length()) return -2;
  uint8_t first = uint8_t(in[offset++]);
  if (firstByte) *firstByte = first;
  PrefInt decoder;
  int state = decoder.start(first, bits, value);
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

template <typename Bytes>
int putString(
  Bytes &out, uint8_t prefix, unsigned bits,
  ZuCSpan value1, char separator, ZuCSpan value2) {
  if (putPref(
      out, prefix, bits, value1.length() + 1 + value2.length()) < 0)
    return -1;
  for (unsigned i = 0; i < value1.length(); ++i)
    out.push(uint8_t(value1[i]));
  out.push(uint8_t(separator));
  for (unsigned i = 0; i < value2.length(); ++i)
    out.push(uint8_t(value2[i]));
  return 0;
}

template <typename Bytes>
int decodeString(
  Bytes &storage, ZuCSpan in, unsigned &offset, unsigned prefixBits,
  uint8_t huffmanMask, ZuCSpan &out) {
  uint64_t length = 0;
  uint8_t first = 0;
  int n = decodePref(in, offset, prefixBits, length, &first);
  if (n < 0) return n;
  if (length > in.length() - offset) return -2;
  ZuCSpan raw{in.data() + offset, unsigned(length)};
  offset += unsigned(length);
  if (!(first & huffmanMask)) {
    out = raw;
    return int(raw.length());
  }
  storage.length(Huffman::declen(raw.length()));
  int64_t decoded = Huffman::decode(storage.span(), raw);
  if (decoded < 0) return -1;
  storage.length(uint64_t(decoded));
  out = storage;
  return int(out.length());
}

template <typename Bytes>
class StringDecoder {
public:
  int start(
    uint8_t first, unsigned prefixBits, uint8_t huffmanMask,
    uint64_t maxLength) {
    m_bytes.length(0);
    m_length = 0;
    m_maxLength = maxLength;
    m_huffman = first & huffmanMask;
    m_lengthReady = false;
    m_complete = false;
    int state = m_prefix.start(first, prefixBits, m_length);
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
