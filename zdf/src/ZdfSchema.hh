//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Data Frame library - DB schema

#ifndef ZdfSchema_HH
#define ZdfSchema_HH

#ifndef ZdfLib_HH
#include <zlib/ZdfLib.hh>
#endif

#include <zlib/Zfb.hh>
#include <zlib/ZfbStruct.hh>

#include <zlib/Zdb.hh>

#include <zlib/ZdfTypes.hh>

#include <zlib/zdf_series_fixed_fbs.h>
#include <zlib/zdf_series_float_fbs.h>
#include <zlib/zdf_blk_fixed_fbs.h>
#include <zlib/zdf_blk_float_fbs.h>
#include <zlib/zdf_blk_data_fbs.h>

namespace Zdf::DB {

struct SeriesFixed {
  SeriesID	id;		// 1-based
  IDString	name;
  Fixed0	first;		// first value in series
  ZuDateTime	epoch;		// time when series was created
  BlkOffset	blkOffset;	// first block
  NDP		ndp;		// NDP of first value in series

  friend ZfStructPrint ZuPrintType(SeriesFixed *);
};

ZfbStruct(ZdfAPI, SeriesFixed,
  (((id),	(Ctor<0>, Keys<0>, Descend<0>)),	(UInt32)),
  (((name),	(Ctor<1>, Keys<1>)),			(String)),
  (((first),	(Ctor<2>, Mutable)),			(Int64)),
  (((ndp),	(Ctor<5>, Mutable)),			(UInt8)),
  (((epoch),	(Ctor<3>)),				(DateTime)),
  (((blkOffset),(Ctor<4>, Mutable)),			(UInt64)));

ZfbRoot(SeriesFixed);

struct SeriesFloat {
  SeriesID	id;		// 1-based
  IDString	name;
  Float0	first;		// first value in series
  ZuDateTime	epoch;		// intentionally denormalized
  BlkOffset	blkOffset;	// first block

  friend ZfStructPrint ZuPrintType(SeriesFloat *);
};

ZfbStruct(ZdfAPI, SeriesFloat,
  (((id),	(Ctor<0>, Keys<0>, Descend<0>)),	(UInt32)),
  (((name),	(Ctor<1>, Keys<1>)),			(String)),
  (((first),	(Ctor<2>, Mutable)),			(Float)),
  (((epoch),	(Ctor<3>)),				(DateTime)),
  (((blkOffset),(Ctor<4>, Mutable)),			(UInt64)));

ZfbRoot(SeriesFloat);

struct BlkFixed {
  BlkOffset	blkOffset;
  Offset	offset;
  Fixed0	last;
  SeriesID	seriesID;
  BlkCount	count;
  NDP		ndp;

  friend ZfStructPrint ZuPrintType(BlkFixed *);
};

ZfbStruct(ZdfAPI, BlkFixed,
  (((seriesID),	(Ctor<3>, Keys<0>, Group<0>)),		(UInt32)),
  (((blkOffset),(Ctor<0>, Keys<0>)),			(UInt64)),
  (((offset),	(Ctor<1>, Mutable)),			(UInt64)),
  (((last),	(Ctor<2>, Mutable)),			(Int64)),
  (((count),	(Ctor<4>, Mutable)),			(UInt16)),
  (((ndp),	(Ctor<5>, Mutable)),			(UInt8)));

ZfbRoot(BlkFixed);

struct BlkFloat {
  BlkOffset	blkOffset;
  Offset	offset;
  Float0	last;
  SeriesID	seriesID;
  BlkCount	count;

  friend ZfStructPrint ZuPrintType(BlkFloat *);
};

ZfbStruct(ZdfAPI, BlkFloat,
  (((seriesID),	(Ctor<3>, Keys<0>, Group<0>)),		(UInt32)),
  (((blkOffset),(Ctor<0>, Keys<0>)),			(UInt64)),
  (((offset),	(Ctor<1>, Mutable)),			(UInt64)),
  (((last),	(Ctor<2>, Mutable)),			(Float)),
  (((count),	(Ctor<4>, Mutable)),			(UInt16)));

ZfbRoot(BlkFloat);

using BlkDataBuf = ZuArray<uint8_t, BlkSize>;

struct BlkData;
struct BlkData_Evict { // Zdb evict hook
  static void evict(ZdbRow<BlkData> *);
};
struct BlkData {
  BlkOffset		blkOffset;
  SeriesID		seriesID;
  BlkDataBuf		buf;
  void			*series;	// AnySeries

  friend ZfStructPrint ZuPrintType(BlkData *);
  friend BlkData_Evict ZdbEvictHook(BlkData *);
};

ZfbStruct(ZdfAPI, BlkData,
  (((seriesID),	(Ctor<1>, Keys<0>, Group<0>, Descend<0>)),	(UInt32)),
  (((blkOffset),(Ctor<0>, Keys<0>, Descend<0>)),		(UInt64)),
  (((buf),	(Mutable)),					(Bytes)));

ZfbRoot(BlkData);

ZdbTableDerive(SeriesFixedTable, SeriesFixed);
ZdbTableDerive(SeriesFloatTable, SeriesFloat);
ZdbTableDerive(BlkFixedTable, BlkFixed);
ZdbTableDerive(BlkFloatTable, BlkFloat);
ZdbTableDerive(BlkDataTable, BlkData);

} // Zdf::DB

#endif /* ZdfSchema_HH */
