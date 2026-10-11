//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef zdbsltypes_HH
#define zdbsltypes_HH

#include <zlib/Zdb.hh>


namespace zdbsltest {

using TestBytes = ZtArray<uint8_t>;
using TestStrings = ZtArray<ZtString<>>;
using TestBytesVec = ZtArray<TestBytes>;

struct MappedTime {
  int64_t sec;
  constexpr MappedTime(int64_t value) : sec{value} { }
  MappedTime(ZuTime value) : sec{value.sec()} { }
  operator ZuTime() const { return {sec, 0}; }
};
ZfStruct(, MappedTime, (sec, (Ctor<0>), Int64));

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

  float			realValue = 0;
  ZtArray<float>	realVec;
  ZtArray<ZtString<>>	names;

  MappedTime		mappedTime{23};

  const auto &cstrings() const & { return names; }
  auto &cstrings() & { return names; }
  auto &&cstrings() && { return ZuMv(names); }
  void cstrings(ZfStruct_::CStringVec v) {
    names.length(0);
    names.size(v.length());
    v.all([this](const char *s) { new (names.push()) ZtString<>{s}; });
  }

  friend ZfStructPrint ZuPrintType(AllTypes *);
  friend ZuStringT<"zdbsltest.allTypes"> ZdbHeapID(AllTypes *);
  friend ZuUnsigned<2048> ZdbBufSize(AllTypes *);
  friend ZuStringT<"zdbsltest.allTypes.buf"> ZdbBufHeapID(AllTypes *);
};

ZfStruct(, AllTypes,
  (id,			(Ctor<0>, Keys<0>),				UInt64),
  (stringValue,		(Ctor<1>, Mutable),				String),
  (bytesValue,		(Ctor<2>, Mutable),				Bytes),
  (boolValue,		(Ctor<3>, Mutable),				Bool),
  (int8Value,		(Ctor<4>, Mutable),				Int8),
  (uint8Value,		(Ctor<5>, Mutable),				UInt8),
  (int16Value,		(Ctor<6>, Mutable),				Int16),
  (uint16Value,		(Ctor<7>, Mutable),				UInt16),
  (int32Value,		(Ctor<8>, Mutable),				Int32),
  (uint32Value,		(Ctor<9>, Mutable),				UInt32),
  (int64Value,		(Ctor<10>, Mutable),				Int64),
  (uint64Value,		(Ctor<11>, Mutable),				UInt64),
  (int128Value,		(Ctor<12>, Mutable),				Int128),
  (uint128Value,	(Ctor<13>, Mutable),				UInt128),
  (floatValue,		(Ctor<14>, Mutable),				Float),
  (fixedValue,		(Ctor<15>, Mutable),				Fixed),
  (decimalValue,	(Ctor<16>, Mutable),				Decimal),
  (timeValue,		(Ctor<17>, Keys<1>, Mutable),			Time),
  (dateTimeValue,	(Ctor<18>, Keys<1>, Mutable, Descend<1>),	DateTime),
  (bitmapValue,		(Ctor<19>, Mutable),				UDT),
  (ipValue,		(Ctor<20>, Mutable),				UDT),
  (stringVec,		(Ctor<21>, Mutable),				StringVec),
  (bytesVec,		(Ctor<22>, Mutable),				BytesVec),
  (int8Vec,		(Ctor<23>, Mutable),				Int8Vec),
  (uint8Vec,		(Ctor<24>, Mutable),				UInt8Vec),
  (int16Vec,		(Ctor<25>, Mutable),				Int16Vec),
  (uint16Vec,		(Ctor<26>, Mutable),				UInt16Vec),
  (int32Vec,		(Ctor<27>, Mutable),				Int32Vec),
  (uint32Vec,		(Ctor<28>, Mutable),				UInt32Vec),
  (int64Vec,		(Ctor<29>, Mutable),				Int64Vec),
  (uint64Vec,		(Ctor<30>, Mutable),				UInt64Vec),
  (int128Vec,		(Ctor<31>, Mutable),				Int128Vec),
  (uint128Vec,		(Ctor<32>, Mutable),				UInt128Vec),
  (floatVec,		(Ctor<33>, Mutable),				FloatVec),
  (fixedVec,		(Ctor<34>, Mutable),				FixedVec),
  (decimalVec,		(Ctor<35>, Mutable),				DecimalVec),
  (timeVec,		(Ctor<36>, Mutable),				TimeVec),
  (dateTimeVec,		(Ctor<37>, Mutable),				DateTimeVec),
  (realValue,		(Ctor<38>, Mutable),				Float),
  (realVec,		(Ctor<39>, Mutable),				FloatVec),
  ((names, AliasFn, cstrings, cstrings), (Ctor<40>, Mutable),		CStringVec),
  (mappedTime,		(Ctor<41>, Mutable, Deflt<MappedTime{23}>),	Time));
ZiFB::Table<"zdbsltest.fbs.AllTypes"> ZiFBType(AllTypes *);

ZdbTableDerive(AllTypesTable, AllTypes);

} // zdbsltest

#endif /* zdbsltypes_HH */
