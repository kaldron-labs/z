//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// Zdf vocabulary types

#ifndef ZdfTypes_HH
#define ZdfTypes_HH

#ifndef ZdfLib_HH
#include <zlib/ZdfLib.hh>
#endif

#include <zlib/ZePlatform.hh>

#include <zlib/ZdbTypes.hh>

namespace Zdf {

constexpr unsigned IDSize = Zdb_::IDSize;
constexpr unsigned IDSize_ = Zdb_::IDSize_;
using IDString = Zdb_::IDString;

enum { BlkSize = 0x1000 };		// 4K bytes
enum { MaxBlkCount = 0x1000 };		// 4K values (i.e. 1 per byte)

using Shard = uint8_t;			// shard

using DFID = uint32_t;			// data frame ID

using SeriesID = uint32_t;		// series ID

using BlkOffset = uint64_t;		// block offset
using Offset = uint64_t;		// value offset
constexpr Offset MaxOffset = ~Offset(0);

using Fixed0 = ZuBox0(int64_t);
using Float0 = ZuBox0(double);
using NDP = ZuFixedNDP;			// number of decimal places

using BlkCount = uint16_t;		// count of values within a block

using Event = ZeException;			// monomorphic ZeEvent

inline ZuTime DefltEpoch() { return {"2020/01/01"}; }	// default epoch

} // namespace Zdf

#endif /* ZdfTypes_HH */
