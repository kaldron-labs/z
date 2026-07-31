//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// internal alert persistence recovery algorithm

#ifndef ZtcAlert_HH
#define ZtcAlert_HH

#ifndef ZvLib_HH
#include <zlib/ZvLib.hh>
#endif

#include <zlib/ZiPlatform.hh>
#include <zlib/ZuDateTime.hh>

namespace Ztc {
namespace Alert_ {

namespace LoadResult {
  enum T { OK, Missing, Incomplete, Corrupt, IOError };
}

inline bool validDate(uint32_t date)
{
  if (!date) return false;
  ZuDateTime value{
    ZuDateTime::YYYYMMDD{date}, ZuDateTime::HHMMSS{0}};
  return value.yyyymmdd() == int(date);
}

inline uint32_t addDays(uint32_t date, int days)
{
  ZuDateTime value{
    ZuDateTime::YYYYMMDD{date}, ZuDateTime::HHMMSS{0}};
  value.julian() += days;
  return uint32_t(value.yyyymmdd());
}

template <typename IO>
bool recover(IO &io, uint64_t &count, Zi::Offset &dataEnd)
{
  Zi::Offset dataSize = io.dataSize();
  Zi::Offset indexSize = io.indexSize();
  if (dataSize < 0 || indexSize < 0) return false;
  uint64_t entries = uint64_t(indexSize) / sizeof(uint64_t);
  dataEnd = 0;
  count = 0;
  for (uint64_t seqNo = 0; seqNo < entries; ++seqNo) {
    uint64_t offset = 0;
    if (!io.index(seqNo, offset) || offset != uint64_t(dataEnd))
      return false;
    unsigned length = 0;
    LoadResult::T result = io.frame(seqNo, offset, length);
    if (result == LoadResult::Incomplete && seqNo + 1 == entries) {
      entries = seqNo;
      break;
    }
    if (result != LoadResult::OK) return false;
    if (uint64_t(dataEnd) > UINT64_MAX - length) return false;
    dataEnd += length;
    ++count;
  }
  Zi::Offset indexEnd = Zi::Offset(entries * sizeof(uint64_t));
  if (dataSize != dataEnd && !io.truncateData(dataEnd)) return false;
  if (indexSize != indexEnd && !io.truncateIndex(indexEnd)) return false;
  count = entries;
  return true;
}

} // Alert_
} // Ztc

#endif /* ZtcAlert_HH */
