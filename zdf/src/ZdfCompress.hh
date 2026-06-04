//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// series compression for int64_t
// - byte-aligned
// - signed data
// - Huffman-coded length prefix
// - single-byte RLE
// - efficient random-access (seeking) and interpolation searching
// - little-endian (to align with common architectures)
// - composable Encoders and Decoders providing:
//   - absolute, delta (first derivative), delta-of-delta (second derivative)

// series compression for double (64bit IEEE floating point)
// - Chimp algorithm (https://vldb.org/pvldb/vol15/p3058-liakos.pdf)
// - improved from Gorilla (https://www.vldb.org/pvldb/vol8/p1816-teller.pdf)
// - Gorilla originated at Facebook and is used in TimescaleDB, InfluxDB, ...

#ifndef ZdfCompress_HH
#define ZdfCompress_HH

#ifndef ZdfLib_HH
#include <zlib/ZdfLib.hh>
#endif

#include <zlib/ZuInt.hh>
#include <zlib/ZuByteSwap.hh>
#include <zlib/ZuIntrin.hh>
#include <zlib/ZuBitStream.hh>

#include <zlib/ZmAssert.hh>

namespace Zdf {

class Decoder {
public:
  using Value = int64_t;

  Decoder() = default;
  Decoder(const Decoder &) = default;
  Decoder &operator =(const Decoder &) = default;
  Decoder(Decoder &&) = default;
  Decoder &operator =(Decoder &&) = default;

  Decoder(const uint8_t *start, const uint8_t *end) :
    m_pos{start}, m_end{end} { }

  bool operator !() const { return !m_pos; }
  ZuOpBool

  const uint8_t *pos() const { return m_pos; }
  const uint8_t *end() const { return m_end; }
  unsigned offset() const { return m_offset; }

  // seek forward
  bool seek(unsigned offset) {
    while (offset) {
      if (m_rle) {
	if (m_rle >= offset) {
	  m_offset += offset;
	  m_rle -= offset;
	  return true;
	}
	m_offset += m_rle;
	offset -= m_rle;
	m_rle = 0;
      } else {
	if (!read_(nullptr)) return false;
	++m_offset;
	--offset;
      }
    }
    return true;
  }

  // seek forward, informing upper layer of skipped values
  // l(int64_t value, unsigned offset)
  template <typename L>
  bool seek(unsigned offset, L &&l) {
    while (offset) {
      int64_t value;
      if (m_rle)
	value = m_prev;
      else if (!read_(&value))
	return false;
      ++m_rle;
      if (m_rle >= offset) {
	l(value, offset);
	m_offset += offset;
	m_rle -= offset;
	return true;
      }
      ZuFwd<L>(l)(value, m_rle);
      m_offset += m_rle;
      offset -= m_rle;
      m_rle = 0;
    }
    return true;
  }

  // search forward for a value
  // l(int64_t value, unsigned runlength) -> unsigned skip
  // search ends when skip < runlength
  template <typename L>
  bool search(L &&l) {
    const uint8_t *prevPos;
    unsigned skip;
    if (m_rle) {
      skip = l(m_prev, m_rle);
      m_offset += skip;
      if (m_rle -= skip) return true;
    }
    int64_t value;
    for (;;) {
      prevPos = m_pos;
      if (!read_(&value)) return false;
      skip = ZuFwd<L>(l)(value, m_rle + 1);
      if (!skip) {
	m_pos = prevPos;
	return true;
      }
      ++m_offset;
      --skip;
      if (m_rle) {
	if (!skip) return true;
	m_offset += skip;
	if (m_rle -= skip) return true;
      }
    }
  }

  bool read(int64_t &value) {
    if (m_rle) {
      ++m_offset;
      --m_rle;
      value = m_prev;
      return true;
    }
    if (read_(&value)) {
      ++m_offset;
      return true;
    }
    return false;
  }

  // same as read(), but discards value
  bool skip() {
    if (m_rle) {
      ++m_offset;
      --m_rle;
      return true;
    }
    if (read_(nullptr)) {
      ++m_offset;
      return true;
    }
    return false;
  }

  void extend(const uint8_t *end) { m_end = end; }

private:
  bool read_(int64_t *out) {
  again:
    if (ZuUnlikely(m_pos >= m_end)) return false;
    unsigned byte = *m_pos;
    if (byte & 0x80) {
      ++m_pos;
      if (byte == 0x80) {				// reset
	m_prev = 0;
	goto again;
      } else {
	m_rle = (byte & 0x7f) - 1;
	if (out) *out = m_prev;
	return true;
      }
    }
    int64_t value;
    if (!(byte & 0x20)) {				// 5 bits
      ++m_pos;
      value = byte & 0x1f;
    } else if ((byte & 0x30) == 0x20) {			// 12 bits
      if (m_pos + 2 > m_end) return false;
      ++m_pos;
      value = byte & 0xf;
      value |= int64_t(*m_pos++)<<4;
    } else if ((byte & 0x38) == 0x30) {			// 19 bits
      if (m_pos + 3 > m_end) return false;
      ++m_pos;
      value = byte & 0x7;
      value |= int64_t(*m_pos++)<<3;
      value |= int64_t(*m_pos++)<<11;
    } else if ((byte & 0x3c) == 0x38) {			// 26 bits
      if (m_pos + 4 > m_end) return false;
      ++m_pos;
      value = byte & 0x3;
      value |= int64_t(*m_pos++)<<2;
      value |= int64_t(*m_pos++)<<10;
      value |= int64_t(*m_pos++)<<18;
    } else if ((byte & 0x3e) == 0x3c) {			// 33 bits
      if (m_pos + 5 > m_end) return false;
      ++m_pos;
      value = byte & 0x1;
      value |= int64_t(*m_pos++)<<1;
      value |= int64_t(*m_pos++)<<9;
      value |= int64_t(*m_pos++)<<17;
      value |= int64_t(*m_pos++)<<25;
    } else if ((byte & 0x3f) == 0x3e) {			// 40 bits
      if (m_pos + 6 > m_end) return false;
      ++m_pos;
      value = *m_pos++;
      value |= int64_t(*m_pos++)<<8;
      value |= int64_t(*m_pos++)<<16;
      value |= int64_t(*m_pos++)<<24;
      value |= int64_t(*m_pos++)<<32;
    } else {						// 64 bits
      if (m_pos + 9 > m_end) return false;
      ++m_pos;
#ifdef __x86_64__
      // potentially misaligned
      value = *reinterpret_cast<const ZuLittleEndian<uint64_t> *>(m_pos);
#else
      {
	ZuLittleEndian<uint64_t> value_;
	memcpy(&value_, m_pos, 8);
	value = value_;
      }
#endif
      m_pos += 8;
    }
    if (byte & 0x40) value = ~value;
    if (out) *out = m_prev = value;
    return true;
  }

private:
  const uint8_t	*m_pos = nullptr;
  const uint8_t	*m_end = nullptr;
  int64_t	m_prev = 0;
  unsigned	m_rle = 0;
  unsigned	m_offset = 0;
};

template <typename> class Encoder;

template <> class Encoder<Decoder> {
  Encoder(const Encoder &) = delete;
  Encoder &operator =(const Encoder &) = delete;

public:
  Encoder(uint8_t *start, uint8_t *end) : m_pos{start}, m_end{end} { }

  Encoder() { }
  Encoder(Encoder &&w) :
    m_pos{w.m_pos}, m_end{w.m_end},
    m_rle{w.m_rle}, m_prev{w.m_prev}, m_offset{w.m_offset}
  {
    w.m_pos = nullptr;
    w.m_end = nullptr;
    w.m_rle = nullptr;
    w.m_prev = 0;
    w.m_offset = 0;
  }
  Encoder &operator =(Encoder &&w) {
    if (ZuLikely(this != &w)) {
      this->~Encoder(); // nop
      new (this) Encoder{ZuMv(w)};
    }
    return *this;
  }
  ~Encoder() = default;

  // an Encoder can be constructed from a completed Decoder in order
  // to append to an existing buffer; a reset sentinel code (0x80) is
  // written so that decoders reset their "previous value" to zero,
  // ensuring that any initial RLE of zero is processed correctly
  Encoder(const Decoder &decoder, uint8_t *end) :
    m_pos{const_cast<uint8_t *>(decoder.pos())}, m_end{end},
    m_offset{decoder.offset()}
  {
    ZmAssert(m_pos < m_end);
    *m_pos++ = 0x80; // reset
  }

  uint8_t *pos() const { return m_pos; }
  uint8_t *end() const { return m_end; }
  unsigned offset() const { return m_offset; }

  bool operator !() const { return !m_pos; }
  ZuOpBool

  bool write(int64_t value_) {
    if (ZuLikely(value_ == m_prev)) {
      if (m_rle) {
	if (++*m_rle == 0xff) m_rle = nullptr; // prevent run-length overrun
	++m_offset;
	return true;
      }
      if (m_pos >= m_end) return false;
      *(m_rle = m_pos++) = 0x81;
      ++m_offset;
      return true;
    } else
      m_rle = nullptr;
    unsigned negative = value_ < 0;
    int64_t value = value_;
    if (negative) value = ~value;
    unsigned n = !value ? 0 : 64 - ZuIntrin::clz(value);
    n = (n + 1) / 7;
    if (n >= 6) n = 8;
    if (m_pos + n >= m_end) return false;
    if (n == 8) n = 6;
    negative <<= 6;
    switch (n) {
      case 0:							// 5 bits
	*m_pos++ = negative | value;
	break;
      case 1:							// 12 bits
	*m_pos++ = negative | 0x20 | (value & 0xf);
	value >>= 4; *m_pos++ = value;
	break;
      case 2:							// 19 bits
	*m_pos++ = negative | 0x30 | (value & 0x7);
	value >>= 3; *m_pos++ = value & 0xff;
	value >>= 8; *m_pos++ = value;
	break;
      case 3:							// 26 bits
	*m_pos++ = negative | 0x38 | (value & 0x3);
	value >>= 2; *m_pos++ = value & 0xff;
	value >>= 8; *m_pos++ = value & 0xff;
	value >>= 8; *m_pos++ = value;
	break;
      case 4:							// 33 bits
	*m_pos++ = negative | 0x3c | (value & 0x1);
	value >>= 1; *m_pos++ = value & 0xff;
	value >>= 8; *m_pos++ = value & 0xff;
	value >>= 8; *m_pos++ = value & 0xff;
	value >>= 8; *m_pos++ = value;
	break;
      case 5:							// 40 bits
	*m_pos++ = negative | 0x3e;
	*m_pos++ = value & 0xff;
	value >>= 8; *m_pos++ = value & 0xff;
	value >>= 8; *m_pos++ = value & 0xff;
	value >>= 8; *m_pos++ = value & 0xff;
	value >>= 8; *m_pos++ = value;
	break;
      case 6:							// 64 bits
	*m_pos++ = negative | 0x3f;
#ifdef __x86_64__
	// potentially misaligned (intentional)
	*reinterpret_cast<ZuLittleEndian<uint64_t> *>(m_pos) = value;
#else
	{
	  ZuLittleEndian<uint64_t> value_ = value;
	  memcpy(m_pos, static_cast<void *>(&value_), 8);
	}
#endif
	m_pos += 8;
	break;
    }
    m_prev = value_;
    ++m_offset;
    return true;
  }

  int64_t last() const { return m_prev; }

  void finish() { }

private:
  uint8_t	*m_pos =nullptr;
  uint8_t	*m_end =nullptr;
  uint8_t	*m_rle = nullptr;
  int64_t	m_prev = 0;
  unsigned	m_offset = 0;
};

template <typename Base = Decoder>
class DeltaDecoder : public Base {
  ZuAssert((ZuIsSame<typename Base::Value, int64_t>{}));

public:
  DeltaDecoder() : Base{} { }
  DeltaDecoder(const DeltaDecoder &) = default;
  DeltaDecoder &operator =(const DeltaDecoder &) = default;
  DeltaDecoder(DeltaDecoder &&) = default;
  DeltaDecoder &operator =(DeltaDecoder &&) = default;

  DeltaDecoder(const uint8_t *start, const uint8_t *end) :
    Base{start, end} { }

  // seek forward
  bool seek(unsigned offset) {
    return Base::seek(offset,
      [this](int64_t skip, unsigned rle) {
	m_base += skip * rle;
      });
  }

  // seek forward
  template <typename L>
  bool seek(unsigned offset, L &&l) {
    return Base::seek(offset,
      [this, &l](int64_t skip, unsigned rle) {
	for (unsigned i = 0; i < rle; i++)
	  ZuFwd<L>(l)(m_base += skip, 1);
      });
  }

  // search forward
  template <typename L>
  bool search(L &&l) {
    return Base::search(
      [this, &l](int64_t skip, unsigned rle) {
	int64_t value;
	for (unsigned i = 0; i < rle; i++) {
	  value = m_base + skip;
	  if (!ZuFwd<L>(l)(value, 1)) return i;
	  m_base = value;
	}
	return rle;
      });
  }

  bool read(int64_t &value_) {
    int64_t value;
    if (ZuUnlikely(!Base::read(value))) return false;
    value_ = (m_base += value);
    return true;
  }

  int64_t base() const { return m_base; }

private:
  int64_t		m_base = 0;
};

template <typename Base_>
class Encoder<DeltaDecoder<Base_>> : public Encoder<Base_> {
  Encoder(const Encoder &) = delete;
  Encoder &operator =(const Encoder &) = delete;

public:
  using Base = Encoder<Base_>;
  using Decoder = DeltaDecoder<Base_>;

  Encoder(uint8_t *start, uint8_t *end) : Base{start, end} { }

  Encoder() { }
  Encoder(Encoder &&w) : Base{static_cast<Base &&>(w)}, m_base{w.m_base} {
    w.m_base = 0;
  }
  Encoder &operator =(Encoder &&w) {
    if (ZuLikely(this != &w)) {
      this->~Encoder(); // nop
      new (this) Encoder{ZuMv(w)};
    }
    return *this;
  }

  Encoder(const Decoder &decoder, uint8_t *end) :
    Base{decoder, end}, m_base{decoder.base()} { }

  bool write(int64_t value) {
    int64_t delta = value - m_base;
    if (ZuUnlikely(!Base::write(delta))) return false;
    m_base = value;
    return true;
  }

  int64_t last() const { return m_base + Base::last(); }

private:
  int64_t	m_base = 0;
};

using BitReader = ZuBitStream::LE::Reader;

class FloatDecoder : public BitReader {
public:
  using Value = double;

  ZuAssert(sizeof(Value) == 8); // sanity check

  FloatDecoder() = default;
  FloatDecoder(const FloatDecoder &) = default;
  FloatDecoder &operator =(const FloatDecoder &) = default;
  FloatDecoder(FloatDecoder &&) = default;
  FloatDecoder &operator =(FloatDecoder &&) = default;

  FloatDecoder(const uint8_t *start, const uint8_t *end) :
    BitReader{start, end} { }

  unsigned offset() const { return m_offset; }

  // seek forward
  bool seek(unsigned offset) {
    while (offset) {
      auto context = save();
      if (!read_(nullptr)) {
	load(context);
	return false;
      }
      ++m_offset;
      --offset;
    }
    return true;
  }

  // seek forward, informing upper layer of skipped values
  // l(int64_t value, unsigned offset)
  template <typename L>
  bool seek(unsigned offset, L &&l) {
    while (offset) {
      double value;
      auto context = save();
      if (!read_(&value)) {
	load(context);
	return false;
      }
      ZuFwd<L>(l)(value, 1);
      ++m_offset;
      --offset;
    }
    return true;
  }

  // search forward for a value
  // - l(double value, unsigned offset) -> unsigned skipped
  // - search ends when skipped < offset
  template <typename L>
  bool search(L &&l) {
    double value;
    for (;;) {
      auto context = save();
      auto xcontext = ZuTuple<uint64_t, unsigned>{m_prev, m_prevLZ};
      if (!read_(&value)) {
	load(context);
	return false;
      }
      if (!ZuFwd<L>(l)(value, 1)) {
	load(context);
	m_prev = xcontext.p<0>();
	m_prevLZ = xcontext.p<1>();
	return true;
      }
      ++m_offset;
    }
  }

  bool read(double &value) {
    auto context = save();
    if (!read_(&value)) {
      load(context);
      return false;
    }
    ++m_offset;
    return true;
  }

  // same as read(), but discards value
  bool skip() {
    auto context = save();
    if (!read_(nullptr)) {
      load(context);
      return false;
    }
    ++m_offset;
    return true;
  }

private:
  // Note: read_() does not mutate m_prev or m_prevLZ on failure
  // - care is taken to prevent buffer overrun
  // - attempts to read beyond the end of the buffer will fail
  // - rewinding a failed read_() only requires the caller to restore
  //   the underlying BitReader state
  // - rewinding a *successful* read_() requires the caller to restore
  //   both the BitReader state, m_prev and m_prevLZ
  bool read_(double *out) {
    static uint8_t lzmap[] = { 0, 8, 12, 16, 18, 20, 22, 24 };

  again:
    if (ZuUnlikely(!avail<2>())) return false;
    uint64_t value;
    switch (in<2>()) {
      case 0:
	value = 0;
	break;
      case 1: {
	if (ZuUnlikely(!avail<9>())) return false;
	unsigned lz = lzmap[in<3>()];
	unsigned sb = in<6>();
	if (!sb) { m_prev = 0; m_prevLZ = 0; goto again; } // reset
	if (ZuUnlikely(!avail(sb))) return false;
	value = in(sb)<<(64 - sb - lz);
	m_prevLZ = lz;
      } break;
      case 2: {
	unsigned sb = 64 - m_prevLZ;
	if (ZuUnlikely(!avail(sb))) return false;
	value = in(sb);
      } break;
      case 3: {
	if (ZuUnlikely(!avail<3>())) return false;
	unsigned lz = lzmap[in<3>()];
	unsigned sb = 64 - lz;
	if (ZuUnlikely(!avail(sb))) return false;
	value = in(sb);
	m_prevLZ = lz;
      } break;
    }
    value ^= m_prev;
    m_prev = value;
    if (out) *out = ZuPun<uint64_t, double>(value).out;
    return true;
  }

private:
  uint64_t	m_prev = 0;
  unsigned	m_prevLZ = 0;	// previous LZ
  unsigned	m_offset = 0;
};

using BitWriter = ZuBitStream::LE::Writer;

template <> class Encoder<FloatDecoder> : public BitWriter {
  Encoder(const Encoder &) = delete;
  Encoder &operator =(const Encoder &) = delete;

public:
  using Decoder = FloatDecoder;

  Encoder(uint8_t *start, uint8_t *end) : BitWriter{start, end} { }

  Encoder() { }
  Encoder(Encoder &&w) :
    BitWriter{ZuMv(w)},
    m_prev{w.m_prev}, m_prevLZ{w.m_prevLZ}, m_offset{w.m_offset}
  {
    w.m_prev = 0;
    w.m_prevLZ = 0;
    w.m_offset = 0;
  }
  Encoder &operator =(Encoder &&w) {
    if (ZuLikely(this != &w)) {
      this->~Encoder(); // nop
      new (this) Encoder{ZuMv(w)};
    }
    return *this;
  }

  Encoder(const Decoder &decoder, uint8_t *end) :
    BitWriter{decoder, end},
    m_offset{decoder.offset()}
  {
    ZmAssert(pos() + 2 < end);
    out(1, 11); // reset
  }

  unsigned offset() const { return m_offset; }

public:
  bool write(double in) {
    static uint8_t lzround[] = {
      0, 0, 0, 0, 0, 0, 0, 0,
      8, 8, 8, 8, 12, 12, 12, 12,
      16, 16, 18, 18, 20, 20, 22, 22,
      24, 24, 24, 24, 24, 24, 24, 24,
      24, 24, 24, 24, 24, 24, 24, 24,
      24, 24, 24, 24, 24, 24, 24, 24,
      24, 24, 24, 24, 24, 24, 24, 24,
      24, 24, 24, 24, 24, 24, 24, 24
    };
    static uint8_t lzmap[] = {
      0, 0, 0, 0, 0, 0, 0, 0,
      1, 1, 1, 1, 2, 2, 2, 2,
      3, 3, 4, 4, 5, 5, 6, 6,
      7
    };

    auto value = ZuPun<double, uint64_t>(in).out;
    value ^= m_prev;
    if (ZuUnlikely(!value)) {
      if (ZuUnlikely(!this->avail<2>())) return false;
      out<2>(0);
      ++m_offset;
      return true;
    }
    unsigned lz = lzround[ZuIntrin::clz(value)];
    unsigned tz = ZuIntrin::ctz(value);
    if (tz > 6) {
      unsigned sb = 64 - lz - tz;
      if (ZuUnlikely(!this->avail(sb + 11))) return false;
      out((uint64_t(sb)<<5) | (lzmap[lz]<<2) | 1, 11);
      out(value>>tz, sb);
      m_prevLZ = lz;
    } else if (lz == m_prevLZ) {
      unsigned sb = 64 - lz;
      if (ZuUnlikely(!this->avail(sb + 2))) return false;
      out<2>(2);
      out(value, sb);
    } else {
      unsigned sb = 64 - lz;
      if (ZuUnlikely(!this->avail(sb + 5))) return false;
      out<5>((lzmap[lz]<<2) | 3);
      out(value, sb);
      m_prevLZ = lz;
    }
    m_prev ^= value;
    ++m_offset;
    return true;
  }

  double last() const {
    return ZuPun<uint64_t, double>(m_prev).out;
  }

  void finish() {
    if (avail<2>()) out<2>(1); // ensure decoder terminates
    BitWriter::finish();
  }

private:
  uint64_t	m_prev = 0;
  unsigned	m_prevLZ = 0;	// previous LZ
  unsigned	m_offset = 0;
};

} // namespace Zdf

#endif /* ZdfCompress_HH */
