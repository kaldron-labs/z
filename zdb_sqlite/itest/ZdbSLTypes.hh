//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef ZdbSLTypes_HH
#define ZdbSLTypes_HH

#include <zlib/Zdb.hh>

#include "zdbsltypes_fbs.h"

namespace zdbsltest {

using TestBytes = ZtArray<uint8_t>;
using TestStrings = ZtArray<ZtString<>>;
using TestBytesVec = ZtArray<TestBytes>;

struct AllTypes {
  uint64_t		id = 0;
  ZtString<>		stringValue;
  TestBytes		bytesValue;
  bool			boolValue = false;
  int8_t		int8Value = 0;
  uint8_t		uint8Value = 0;
  int16_t		int16Value = 0;
  uint16_t		uint16Value = 0;
  int32_t		int32Value = 0;
  uint32_t		uint32Value = 0;
  int64_t		int64Value = 0;
  uint64_t		uint64Value = 0;
  int128_t		int128Value = 0;
  uint128_t		uint128Value = 0;
  double		floatValue = 0;
  ZuFixed		fixedValue;
  ZuDecimal		decimalValue;
  ZuTime		timeValue;
  ZuDateTime		dateTimeValue;
  ZtBitmap		bitmapValue;
  ZiIP			ipValue;
  TestStrings		stringVec;
  TestBytesVec		bytesVec;
  ZtArray<int8_t>	int8Vec;
  ZtArray<uint8_t>	uint8Vec;
  ZtArray<int16_t>	int16Vec;
  ZtArray<uint16_t>	uint16Vec;
  ZtArray<int32_t>	int32Vec;
  ZtArray<uint32_t>	uint32Vec;
  ZtArray<int64_t>	int64Vec;
  ZtArray<uint64_t>	uint64Vec;
  ZtArray<int128_t>	int128Vec;
  ZtArray<uint128_t>	uint128Vec;
  ZtArray<double>	floatVec;
  ZtArray<ZuFixed>	fixedVec;
  ZtArray<ZuDecimal>	decimalVec;
  ZtArray<ZuTime>	timeVec;
  ZtArray<ZuDateTime>	dateTimeVec;

  friend ZfStructPrint ZuPrintType(AllTypes *);
  friend ZuStringT<"zdbsltest.allTypes"> ZdbHeapID(AllTypes *);
  friend ZuUnsigned<2048> ZdbBufSize(AllTypes *);
  friend ZuStringT<"zdbsltest.allTypes.buf"> ZdbBufHeapID(AllTypes *);
};

ZfbStruct(, AllTypes,
  (id,		(Keys<0>),					UInt64),
  (stringValue,	(Mutable),					String),
  (bytesValue,	(Mutable),					Bytes),
  (boolValue,	(Mutable),					Bool),
  (int8Value,	(Mutable),					Int8),
  (uint8Value,	(Mutable),					UInt8),
  (int16Value,	(Mutable),					Int16),
  (uint16Value,	(Mutable),					UInt16),
  (int32Value,	(Mutable),					Int32),
  (uint32Value,	(Mutable),					UInt32),
  (int64Value,	(Mutable),					Int64),
  (uint64Value,	(Mutable),					UInt64),
  (int128Value,	(Mutable),					Int128),
  (uint128Value,	(Mutable),				UInt128),
  (floatValue,	(Mutable),					Float),
  (fixedValue,	(Mutable),					Fixed),
  (decimalValue,	(Mutable),				Decimal),
  (timeValue,	(Keys<1>, Mutable),				Time),
  (dateTimeValue,	(Keys<1>, Mutable, Descend<1>),		DateTime),
  (bitmapValue,	(Mutable),					UDT),
  (ipValue,		(Mutable),				UDT),
  (stringVec,	(Mutable),					StringVec),
  (bytesVec,		(Mutable),				BytesVec),
  (int8Vec,		(Mutable),				Int8Vec),
  (uint8Vec,		(Mutable),				UInt8Vec),
  (int16Vec,	(Mutable),					Int16Vec),
  (uint16Vec,	(Mutable),					UInt16Vec),
  (int32Vec,	(Mutable),					Int32Vec),
  (uint32Vec,	(Mutable),					UInt32Vec),
  (int64Vec,	(Mutable),					Int64Vec),
  (uint64Vec,	(Mutable),					UInt64Vec),
  (int128Vec,	(Mutable),					Int128Vec),
  (uint128Vec,	(Mutable),					UInt128Vec),
  (floatVec,		(Mutable),				FloatVec),
  (fixedVec,		(Mutable),				FixedVec),
  (decimalVec,	(Mutable),					DecimalVec),
  (timeVec,		(Mutable),				TimeVec),
  (dateTimeVec,	(Mutable),					DateTimeVec));

ZfbRoot(AllTypes);
ZdbTableDerive(AllTypesTable, AllTypes);

} // zdbsltest

#endif /* ZdbSLTypes_HH */
