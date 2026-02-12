//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// simple fixed-size bitfield of 1|2|4|8|16|32|64-bit values
// - intentionally not constexpr

#ifndef ZuBitfield_HH
#define ZuBitfield_HH

#ifndef ZuLib_HH
#include <zlib/ZuLib.hh>
#endif

#include <zlib/ZuInt.hh>
#include <zlib/ZuTraits.hh>
#include <zlib/ZuSpan.hh>
#include <zlib/ZuBox.hh>

template <unsigned Width> struct ZuBitfield_;
template <> struct ZuBitfield_<1> {
  static constexpr unsigned word(unsigned i) { return i>>6; }
  static constexpr unsigned shift(unsigned i) { return i & 0x3f; }
  static constexpr uint64_t mask() { return 1; }
};
template <> struct ZuBitfield_<2> {
  static constexpr unsigned word(unsigned i) { return i>>5; }
  static constexpr unsigned shift(unsigned i) { return (i & 0x1f)<<1; }
  static constexpr uint64_t mask() { return 3; }
};
template <> struct ZuBitfield_<4> {
  static constexpr unsigned word(unsigned i) { return i>>4; }
  static constexpr unsigned shift(unsigned i) { return (i & 0xf)<<2; }
  static constexpr uint64_t mask() { return 0xf; }
};
template <> struct ZuBitfield_<8> {
  static constexpr unsigned word(unsigned i) { return i>>3; }
  static constexpr unsigned shift(unsigned i) { return (i & 7)<<3; }
  static constexpr uint64_t mask() { return 0xff; }
};
template <> struct ZuBitfield_<16> {
  static constexpr unsigned word(unsigned i) { return i>>2; }
  static constexpr unsigned shift(unsigned i) { return (i & 3)<<4; }
  static constexpr uint64_t mask() { return 0xffff; }
};
template <> struct ZuBitfield_<32> {
  static constexpr unsigned word(unsigned i) { return i>>1; }
  static constexpr unsigned shift(unsigned i) { return (i & 1)<<5; }
  static constexpr uint64_t mask() { return 0xffffffffULL; }
};
template <> struct ZuBitfield_<64> {
  static constexpr unsigned word(unsigned i) { return i; }
  static constexpr unsigned shift(unsigned i) { return 0; }
  static constexpr uint64_t mask() { return ~static_cast<uint64_t>(0); }
};

template <unsigned N, unsigned Width>
class ZuBitfield : public ZuBitfield_<Width> {
  using Base = ZuBitfield_<Width>;
  using Base::word;
  using Base::shift;
  using Base::mask;

  static_assert(N > 0);

  static uint64_t get_(unsigned i, uint64_t w) {
    return (w>>shift(i)) & mask();
  }
  static uint64_t set_(unsigned i, uint64_t w, uint64_t v) {
    auto s = shift(i);
    return (w & ~(mask()<<s)) | (v<<s);
  }

public:
  enum { Words = word(N - 1) + 1 };
  enum { Bytes = Words * unsigned(sizeof(uint64_t)) };

  ZuBitfield() noexcept { zero(); }
  ZuBitfield(const ZuBitfield &b) noexcept { memcpy(data, b.data, Bytes); }
  ZuBitfield &operator =(const ZuBitfield &b) noexcept {
    if (ZuLikely(this != &b)) memcpy(data, b.data, Bytes);
    return *this;
  }
  ZuBitfield(ZuBitfield &&b) = default;
  ZuBitfield &operator =(ZuBitfield &&b) = default;

  ZuBitfield(ZuCSpan) noexcept { zero(); }

  ZuBitfield &zero() { memset(data, 0, Bytes); return *this; }
  ZuBitfield &fill() { memset(data, 0xff, Bytes); return *this; }

  uint64_t get(unsigned i) const { return get_(i, data[word(i)]); }
  ZuBitfield &set(unsigned i, uint64_t v) {
    auto j = word(i);
    data[j] = set_(i, data[j], v);
    return *this;
  }

  struct Field {
    ZuBitfield	&bitmap;
    unsigned	i;

    uint64_t get() const { return bitmap.get(i); }
    operator uint64_t() const { return bitmap.get(i); }

    void set(uint64_t v) { bitmap.set(i, v); }
    Field &operator =(uint64_t v) { bitmap.set(i, v); return *this; }

    bool operator !() const { return !bitmap.get(i); }
  };
  const Field operator [](unsigned i) const {
    return {*const_cast<ZuBitfield *>(this), i};
  }
  Field operator [](unsigned i) { return {*this, i}; }

  bool operator !() const {
    for (unsigned i = 0; i < Words; i++)
      if (data[i]) return false;
    return true;
  }

  uint64_t	data[Words];
};

#endif /* ZuBitfield_HH */
