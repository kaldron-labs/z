//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// heap configuration

#ifndef ZvHeapCSV_HH
#define ZvHeapCSV_HH

#ifndef ZvLib_HH
#include <zlib/ZvLib.hh>
#endif

#include <zlib/ZuArray.hh>

#include <zlib/ZmHeap.hh>

#include <zlib/ZtStruct.hh>

#include <zlib/ZiCSV.hh>

namespace ZvHeapCSV {

struct Data {
  ZmIDString	id;
  uint64_t	cacheSize;
  uint16_t	partition;
  uint8_t	alignment;
  ZmBitmap	cpuset;
};

ZtStruct(Data,
    (((id),		(Ctor<0>, Keys<0>, Group<0>)),	(String)),
    (((partition),	(Ctor<2>, Keys<0>)),		(UInt16)),
    (((alignment),	(Ctor<3>)),			(UInt8)),
    (((cacheSize),	(Ctor<1>)),			(UInt64)),
    (((cpuset),		(Ctor<4>)),			(String)));

class CSV : public ZiCSV::Reader<Data> {
public:
  template <typename Path>
  void read(const Path &file) {
    this->readFile(file,
	[this]() { return &m_data; },
	[](Data *data) {
	  ZmHeapMgr::init(data->id, data->partition, ZmHeapConfig{
	      data->alignment,
	      data->cacheSize,
	      data->cpuset});
	});
  }

private:
  Data	m_data;
};

inline void init(ZuCSpan file) {
  if (file) CSV{}.read(file);
}

} // ZvHeapCSV

#endif /* ZvHeapCSV_HH */
