//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Data Series Block

#ifndef ZdfBlk_HH
#define ZdfBlk_HH

#ifndef ZdfLib_HH
#include <zlib/ZdfLib.hh>
#endif

#include <zlib/ZdfTypes.hh>
#include <zlib/ZdfCompress.hh>
#include <zlib/ZdfSchema.hh>

namespace Zdf {

template <typename> class Series;

using BlkData = ZdbObject<DB::BlkData>;

// all of US equities trades since 2003 is ~350B rows
// 47bits handles 140T rows for a single series, more than enough

union Last {
  int64_t	fixed;
  double	float_;
};

struct Blk {
  uint64_t		ocn = 0;		// offset/count/ndp
  Last			last{.fixed = 0};	// last value in block
  ZmRef<BlkData>	blkData;		// cached data

  static constexpr uint64_t OffsetMask = (uint64_t(1)<<47) - 1;
  enum { CountShift = 47 };
  static constexpr uint64_t CountMask = 0xfff;
  enum { NDPShift = 59 };
  static constexpr uint64_t NDPMask = 0x1f;

  ZuAssert(CountMask + 1 >= MaxBlkCount);

  void init(uint64_t offset_, uint64_t count_, uint64_t ndp_, int64_t last_) {
    ZmAssert(offset_ <= OffsetMask);
    ZmAssert(count_ > 0 && count_ <= MaxBlkCount);
    ZmAssert(ndp_ <= NDPMask);
    --count_;
    ocn = offset_ | (count_<<CountShift) | (ndp_<<NDPShift);
    last.fixed = last_;
  }
  void init(uint64_t offset_, uint64_t count_, double last_) {
    ZmAssert(offset_ <= OffsetMask);
    ZmAssert(count_ > 0 && count_ <= MaxBlkCount);
    --count_;
    ocn = offset_ | (count_<<CountShift);
    last.float_ = last_;
  }

  ZuInline uint64_t offset() const { return ocn & OffsetMask; }
  ZuInline BlkCount count() const {
    return ((ocn>>CountShift) & CountMask) + 1;
  }
  ZuInline NDP ndp() const { return (ocn>>NDPShift) & NDPMask; }

  ZuInline void offset(uint64_t offset_) {
    ZmAssert(offset_ <= OffsetMask);
    ocn = (ocn & ~OffsetMask) | offset_;
  }
  ZuInline void count(uint64_t count_) {
    ZmAssert(count_ > 0 && count_ <= MaxBlkCount);
    --count_;
    ocn = (ocn & ~(CountMask<<CountShift)) | (count_<<CountShift);
  }
  ZuInline void ndp(uint64_t ndp_) {
    ZmAssert(ndp_ <= NDPMask);
    ocn = (ocn & ~(NDPMask<<NDPShift)) | (ndp_<<NDPShift);
  }
  ZuInline void count_ndp(uint64_t count_, uint64_t ndp_) {
    ZmAssert(count_ > 0 && count_ <= MaxBlkCount);
    ZmAssert(ndp_ <= NDPMask);
    --count_;
    ocn = (ocn & OffsetMask) | (count_<<CountShift) | (ndp_<<NDPShift);
  }

  template <typename Decoder>
  Decoder decoder() const {
    ZiAssert(blkData, "Zdf", (), "blkData not loaded", return {});
    const auto &buf = blkData->data().buf;
    auto start = buf.data();
    return Decoder{start, start + buf.length()};
  }

  template <typename Decoder>
  Encoder<Decoder> encoder(Series<Decoder> *series) {
    ZiAssert(blkData, "Zdf", (), "blkData not instantiated", return {});
    auto &buf = blkData->data().buf;
    auto start = buf.data();
    return {start, start + BlkSize};
  }

  template <typename Encoder>
  void sync(const Encoder &encoder, int64_t last_, NDP ndp) { // fixed
    count_ndp(encoder.offset(), ndp);
    last.fixed = last_;
    ZiAssert(blkData, "Zdf", (), "blkData not loaded", return);
    auto &buf = blkData->data().buf;
    buf.length(encoder.pos() - buf.data());
  }
  template <typename Encoder>
  void sync(const Encoder &encoder, double last_) { // floating
    count(encoder.offset());
    last.float_ = last_;
    ZiAssert(blkData, "Zdf", (), "blkData not loaded", return);
    auto &buf = blkData->data().buf;
    buf.length(encoder.pos() - buf.data());
  }

  unsigned space() const {
    ZiAssert(blkData, "Zdf", (), "blkData not loaded", return 0);
    const auto &buf = blkData->data().buf;
    return BlkSize - buf.length();
  }
};

} // namespace Zdf

#endif /* ZdfBlk_HH */
