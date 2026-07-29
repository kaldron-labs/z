//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZhttpCompression.hh>

#include <zlib/ZuArray.hh>

namespace Zhttp { namespace Compression {

struct HuffmanSymbol_ {
  uint32_t	code;
  uint8_t	bits;
  uint16_t	symbol;
};

// RFC 7541 Appendix B defines 256 octets plus EOS.
static constexpr ZuArray<HuffmanSymbol_, 257> huffman_ = {
  {0x1ff8U, 13, 0}, {0x7fffd8U, 23, 1}, {0xfffffe2U, 28, 2},
  {0xfffffe3U, 28, 3}, {0xfffffe4U, 28, 4}, {0xfffffe5U, 28, 5},
  {0xfffffe6U, 28, 6}, {0xfffffe7U, 28, 7}, {0xfffffe8U, 28, 8},
  {0xffffeaU, 24, 9}, {0x3ffffffcU, 30, 10}, {0xfffffe9U, 28, 11},
  {0xfffffeaU, 28, 12}, {0x3ffffffdU, 30, 13}, {0xfffffebU, 28, 14},
  {0xfffffecU, 28, 15}, {0xfffffedU, 28, 16}, {0xfffffeeU, 28, 17},
  {0xfffffefU, 28, 18}, {0xffffff0U, 28, 19}, {0xffffff1U, 28, 20},
  {0xffffff2U, 28, 21}, {0x3ffffffeU, 30, 22}, {0xffffff3U, 28, 23},
  {0xffffff4U, 28, 24}, {0xffffff5U, 28, 25}, {0xffffff6U, 28, 26},
  {0xffffff7U, 28, 27}, {0xffffff8U, 28, 28}, {0xffffff9U, 28, 29},
  {0xffffffaU, 28, 30}, {0xffffffbU, 28, 31}, {0x14U, 6, 32},
  {0x3f8U, 10, 33}, {0x3f9U, 10, 34}, {0xffaU, 12, 35}, {0x1ff9U, 13, 36},
  {0x15U, 6, 37}, {0xf8U, 8, 38}, {0x7faU, 11, 39}, {0x3faU, 10, 40},
  {0x3fbU, 10, 41}, {0xf9U, 8, 42}, {0x7fbU, 11, 43}, {0xfaU, 8, 44},
  {0x16U, 6, 45}, {0x17U, 6, 46}, {0x18U, 6, 47}, {0x0U, 5, 48},
  {0x1U, 5, 49}, {0x2U, 5, 50}, {0x19U, 6, 51}, {0x1aU, 6, 52},
  {0x1bU, 6, 53}, {0x1cU, 6, 54}, {0x1dU, 6, 55}, {0x1eU, 6, 56},
  {0x1fU, 6, 57}, {0x5cU, 7, 58}, {0xfbU, 8, 59}, {0x7ffcU, 15, 60},
  {0x20U, 6, 61}, {0xffbU, 12, 62}, {0x3fcU, 10, 63}, {0x1ffaU, 13, 64},
  {0x21U, 6, 65}, {0x5dU, 7, 66}, {0x5eU, 7, 67}, {0x5fU, 7, 68},
  {0x60U, 7, 69}, {0x61U, 7, 70}, {0x62U, 7, 71}, {0x63U, 7, 72},
  {0x64U, 7, 73}, {0x65U, 7, 74}, {0x66U, 7, 75}, {0x67U, 7, 76},
  {0x68U, 7, 77}, {0x69U, 7, 78}, {0x6aU, 7, 79}, {0x6bU, 7, 80},
  {0x6cU, 7, 81}, {0x6dU, 7, 82}, {0x6eU, 7, 83}, {0x6fU, 7, 84},
  {0x70U, 7, 85}, {0x71U, 7, 86}, {0x72U, 7, 87}, {0xfcU, 8, 88},
  {0x73U, 7, 89}, {0xfdU, 8, 90}, {0x1ffbU, 13, 91}, {0x7fff0U, 19, 92},
  {0x1ffcU, 13, 93}, {0x3ffcU, 14, 94}, {0x22U, 6, 95}, {0x7ffdU, 15, 96},
  {0x3U, 5, 97}, {0x23U, 6, 98}, {0x4U, 5, 99}, {0x24U, 6, 100},
  {0x5U, 5, 101}, {0x25U, 6, 102}, {0x26U, 6, 103}, {0x27U, 6, 104},
  {0x6U, 5, 105}, {0x74U, 7, 106}, {0x75U, 7, 107}, {0x28U, 6, 108},
  {0x29U, 6, 109}, {0x2aU, 6, 110}, {0x7U, 5, 111}, {0x2bU, 6, 112},
  {0x76U, 7, 113}, {0x2cU, 6, 114}, {0x8U, 5, 115}, {0x9U, 5, 116},
  {0x2dU, 6, 117}, {0x77U, 7, 118}, {0x78U, 7, 119}, {0x79U, 7, 120},
  {0x7aU, 7, 121}, {0x7bU, 7, 122}, {0x7ffeU, 15, 123}, {0x7fcU, 11, 124},
  {0x3ffdU, 14, 125}, {0x1ffdU, 13, 126}, {0xffffffcU, 28, 127},
  {0xfffe6U, 20, 128}, {0x3fffd2U, 22, 129}, {0xfffe7U, 20, 130},
  {0xfffe8U, 20, 131}, {0x3fffd3U, 22, 132}, {0x3fffd4U, 22, 133},
  {0x3fffd5U, 22, 134}, {0x7fffd9U, 23, 135}, {0x3fffd6U, 22, 136},
  {0x7fffdaU, 23, 137}, {0x7fffdbU, 23, 138}, {0x7fffdcU, 23, 139},
  {0x7fffddU, 23, 140}, {0x7fffdeU, 23, 141}, {0xffffebU, 24, 142},
  {0x7fffdfU, 23, 143}, {0xffffecU, 24, 144}, {0xffffedU, 24, 145},
  {0x3fffd7U, 22, 146}, {0x7fffe0U, 23, 147}, {0xffffeeU, 24, 148},
  {0x7fffe1U, 23, 149}, {0x7fffe2U, 23, 150}, {0x7fffe3U, 23, 151},
  {0x7fffe4U, 23, 152}, {0x1fffdcU, 21, 153}, {0x3fffd8U, 22, 154},
  {0x7fffe5U, 23, 155}, {0x3fffd9U, 22, 156}, {0x7fffe6U, 23, 157},
  {0x7fffe7U, 23, 158}, {0xffffefU, 24, 159}, {0x3fffdaU, 22, 160},
  {0x1fffddU, 21, 161}, {0xfffe9U, 20, 162}, {0x3fffdbU, 22, 163},
  {0x3fffdcU, 22, 164}, {0x7fffe8U, 23, 165}, {0x7fffe9U, 23, 166},
  {0x1fffdeU, 21, 167}, {0x7fffeaU, 23, 168}, {0x3fffddU, 22, 169},
  {0x3fffdeU, 22, 170}, {0xfffff0U, 24, 171}, {0x1fffdfU, 21, 172},
  {0x3fffdfU, 22, 173}, {0x7fffebU, 23, 174}, {0x7fffecU, 23, 175},
  {0x1fffe0U, 21, 176}, {0x1fffe1U, 21, 177}, {0x3fffe0U, 22, 178},
  {0x1fffe2U, 21, 179}, {0x7fffedU, 23, 180}, {0x3fffe1U, 22, 181},
  {0x7fffeeU, 23, 182}, {0x7fffefU, 23, 183}, {0xfffeaU, 20, 184},
  {0x3fffe2U, 22, 185}, {0x3fffe3U, 22, 186}, {0x3fffe4U, 22, 187},
  {0x7ffff0U, 23, 188}, {0x3fffe5U, 22, 189}, {0x3fffe6U, 22, 190},
  {0x7ffff1U, 23, 191}, {0x3ffffe0U, 26, 192}, {0x3ffffe1U, 26, 193},
  {0xfffebU, 20, 194}, {0x7fff1U, 19, 195}, {0x3fffe7U, 22, 196},
  {0x7ffff2U, 23, 197}, {0x3fffe8U, 22, 198}, {0x1ffffecU, 25, 199},
  {0x3ffffe2U, 26, 200}, {0x3ffffe3U, 26, 201}, {0x3ffffe4U, 26, 202},
  {0x7ffffdeU, 27, 203}, {0x7ffffdfU, 27, 204}, {0x3ffffe5U, 26, 205},
  {0xfffff1U, 24, 206}, {0x1ffffedU, 25, 207}, {0x7fff2U, 19, 208},
  {0x1fffe3U, 21, 209}, {0x3ffffe6U, 26, 210}, {0x7ffffe0U, 27, 211},
  {0x7ffffe1U, 27, 212}, {0x3ffffe7U, 26, 213}, {0x7ffffe2U, 27, 214},
  {0xfffff2U, 24, 215}, {0x1fffe4U, 21, 216}, {0x1fffe5U, 21, 217},
  {0x3ffffe8U, 26, 218}, {0x3ffffe9U, 26, 219}, {0xffffffdU, 28, 220},
  {0x7ffffe3U, 27, 221}, {0x7ffffe4U, 27, 222}, {0x7ffffe5U, 27, 223},
  {0xfffecU, 20, 224}, {0xfffff3U, 24, 225}, {0xfffedU, 20, 226},
  {0x1fffe6U, 21, 227}, {0x3fffe9U, 22, 228}, {0x1fffe7U, 21, 229},
  {0x1fffe8U, 21, 230}, {0x7ffff3U, 23, 231}, {0x3fffeaU, 22, 232},
  {0x3fffebU, 22, 233}, {0x1ffffeeU, 25, 234}, {0x1ffffefU, 25, 235},
  {0xfffff4U, 24, 236}, {0xfffff5U, 24, 237}, {0x3ffffeaU, 26, 238},
  {0x7ffff4U, 23, 239}, {0x3ffffebU, 26, 240}, {0x7ffffe6U, 27, 241},
  {0x3ffffecU, 26, 242}, {0x3ffffedU, 26, 243}, {0x7ffffe7U, 27, 244},
  {0x7ffffe8U, 27, 245}, {0x7ffffe9U, 27, 246}, {0x7ffffeaU, 27, 247},
  {0x7ffffebU, 27, 248}, {0xffffffeU, 28, 249}, {0x7ffffecU, 27, 250},
  {0x7ffffedU, 27, 251}, {0x7ffffeeU, 27, 252}, {0x7ffffefU, 27, 253},
  {0x7fffff0U, 27, 254}, {0x3ffffeeU, 26, 255}, {0x3fffffffU, 30, 256}
};

static constexpr uint16_t huffmanPack_(unsigned bits, unsigned symbol)
{
  return uint16_t((bits << 9) | symbol);
}

static ZuInline unsigned huffmanEntryBits_(uint16_t v)
{
  return v >> 9;
}

static ZuInline unsigned huffmanEntrySymbol_(uint16_t v)
{
  return v & 0x1ffU;
}

// Compile-time instantiated.  The 16-bit primary fanout keeps the common
// decode path to one dependent lookup; the two suffix tables cover only the
// RFC codes beginning 0xfffe and 0xffff.
struct HuffmanDecode_ {
  static constexpr unsigned PrimaryBits = 16;
  static constexpr unsigned FFFEBits = 5;
  static constexpr unsigned FFFFBits = 14;
  static constexpr unsigned PrimaryHalf = 1U << (PrimaryBits - 1);

  ZuArray<uint16_t, PrimaryHalf> primaryLo;
  ZuArray<uint16_t, PrimaryHalf> primaryHi;
  ZuArray<uint16_t, 1U << FFFEBits> fffe;
  ZuArray<uint16_t, 1U << FFFFBits> ffff;

  constexpr uint16_t &primary(unsigned i) {
    return i < PrimaryHalf ? primaryLo[i] : primaryHi[i - PrimaryHalf];
  }
  constexpr uint16_t primary(unsigned i) const {
    return i < PrimaryHalf ? primaryLo[i] : primaryHi[i - PrimaryHalf];
  }

  template <unsigned TableBits, unsigned N>
  static constexpr void fill_(
    ZuArray<uint16_t, N> &table,
    uint32_t code, unsigned codeBits, unsigned bits, unsigned symbol)
  {
    static_assert(N == (1U << TableBits));
    uint32_t first = code << (TableBits - codeBits);
    uint32_t n = 1U << (TableBits - codeBits);
    uint16_t v = huffmanPack_(bits, symbol);
    for (uint32_t i = 0; i < n; ++i) table[first + i] = v;
  }

  constexpr void fillPrimary_(
    uint32_t code, unsigned codeBits, unsigned bits, unsigned symbol)
  {
    uint32_t first = code << (PrimaryBits - codeBits);
    uint32_t n = 1U << (PrimaryBits - codeBits);
    uint16_t v = huffmanPack_(bits, symbol);
    for (uint32_t i = 0; i < n; ++i) primary(first + i) = v;
  }

  constexpr HuffmanDecode_() :
    primaryLo(PrimaryHalf),
    primaryHi(PrimaryHalf),
    fffe(1U << FFFEBits),
    ffff(1U << FFFFBits)
  {
    for (unsigned i = 0; i < huffman_.size(); ++i) {
      const auto &sym = huffman_[i];
      if (sym.bits <= PrimaryBits) {
	if (sym.symbol < 256)
	  fillPrimary_(sym.code, sym.bits, sym.bits, sym.symbol);
	continue;
      }

      uint32_t prefix = sym.code >> (sym.bits - PrimaryBits);
      unsigned suffixBits = sym.bits - PrimaryBits;
      uint32_t suffix = sym.code & ((1U << suffixBits) - 1U);
      if (prefix == 0xfffeU)
	fill_<FFFEBits>(fffe, suffix, suffixBits, sym.bits, sym.symbol);
      else if (prefix == 0xffffU)
	fill_<FFFFBits>(ffff, suffix, suffixBits, sym.bits, sym.symbol);
    }
  }
};

static constexpr HuffmanDecode_ huffmanDecode_;

static ZuInline uint16_t huffmanLongLookup_(
  const uint16_t *table, unsigned tableBits, uint64_t bits, unsigned nBits)
{
  if (nBits <= HuffmanDecode_::PrimaryBits) return 0;
  unsigned suffixBits = nBits - HuffmanDecode_::PrimaryBits;
  if (suffixBits > tableBits) suffixBits = tableBits;
  uint32_t suffix =
    uint32_t(bits >> (64 - HuffmanDecode_::PrimaryBits - suffixBits)) &
    ((1U << suffixBits) - 1U);
  uint16_t v = table[suffix << (tableBits - suffixBits)];
  return v && huffmanEntryBits_(v) <= nBits ? v : 0;
}

static ZuInline uint16_t huffmanLookup_(uint64_t bits, unsigned nBits)
{
  if (!nBits) return 0;

  uint16_t prefix = uint16_t(bits >> 48);
  uint16_t v = huffmanDecode_.primary(prefix);
  if (ZuLikely(v))
    return huffmanEntryBits_(v) <= nBits ? v : 0;
  if (ZuUnlikely(prefix == 0xfffeU))
    return huffmanLongLookup_(
      huffmanDecode_.fffe.data(), HuffmanDecode_::FFFEBits, bits, nBits);
  if (ZuUnlikely(prefix == 0xffffU))
    return huffmanLongLookup_(
      huffmanDecode_.ffff.data(), HuffmanDecode_::FFFFBits, bits, nBits);
  return 0;
}

static ZuInline void huffmanRefill_(
  uint64_t &bits, unsigned &nBits, Huffman::BitReader &in, unsigned want)
{
  if (nBits >= want) return;
  unsigned rBits = (want - nBits + 7U) & ~7U;
  unsigned avail = unsigned(in.end() - in.pos())<<3;
  if (rBits > avail) rBits = avail;
  if (!rBits) return;
  bits |= in.in(rBits) << (64 - nBits - rBits);
  nBits += rBits;
}

static ZuInline bool huffmanEOSPadding_(uint64_t bits, unsigned nBits)
{
  if (!nBits) return true;
  if (nBits > 7) return false;
  return (bits >> (64 - nBits)) == ((1U << nBits) - 1U);
}

uint64_t Huffman::encode(ZuSpan<uint8_t> out, ZuBSpan in)
{
  if (ZuUnlikely(!in.length())) return 0;
  BitWriter writer{out.data(), out.data() + out.length()};
  for (unsigned i = 0, n = in.length(); i < n; ++i) {
    auto &sym = huffman_[uint8_t(in[i])];
    writer.out(sym.code, sym.bits);
  }
  unsigned pad = (8 - writer.outBits()) & 7;
  if (pad) writer.out((1U << pad) - 1U, pad);
  writer.finish();
  return writer.pos() - out.data();
}

int64_t Huffman::decode(ZuSpan<uint8_t> out, ZuBSpan bytes)
{
  auto p = bytes.data();
  Huffman::BitReader in{p, p + bytes.length()};
  auto o = out.data();
  auto end = o + out.length();
  uint64_t bits = 0;
  unsigned nBits = 0;

  for (;;) {
    huffmanRefill_(bits, nBits, in, HuffmanDecode_::PrimaryBits);
    uint16_t v = huffmanLookup_(bits, nBits);
    if (ZuUnlikely(!v)) {
      huffmanRefill_(bits, nBits, in, 30);
      v = huffmanLookup_(bits, nBits);
      if (ZuUnlikely(!v)) {
	if (in.pos() == in.end() && huffmanEOSPadding_(bits, nBits))
	  return o - out.data();
	return -1;
      }
    }

    unsigned symbol = huffmanEntrySymbol_(v);
    if (ZuUnlikely(symbol == 256)) return -1;
    if (ZuUnlikely(o >= end)) return -1;
    *o++ = uint8_t(symbol);
    unsigned used = huffmanEntryBits_(v);
    bits <<= used;
    nBits -= used;
  }
}

}} // namespace Zhttp::Compression
