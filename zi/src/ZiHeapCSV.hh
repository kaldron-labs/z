//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// heap configuration

#ifndef ZiHeapCSV_HH
#define ZiHeapCSV_HH

#ifndef ZiLib_HH
#include <zlib/ZiLib.hh>
#endif

#include <zlib/ZuID.hh>

#include <zlib/ZmHeap.hh>

#include <zlib/ZfStruct.hh>

#include <zlib/ZiCSV.hh>

namespace ZiHeapCSV {

struct Data {
  ZuID		id;
  uint64_t	cacheSize;
  uint16_t	partition;
  ZmBitmap	cpuset;
};

ZfStruct(ZiAPI, Data,
    (((id),		(Ctor<0>, Keys<0>, Group<0>)),	(String)),
    (((partition),	(Ctor<2>, Keys<0>)),		(UInt16)),
    (((cacheSize),	(Ctor<1>)),			(UInt64)),
    (((cpuset),		(Ctor<3>)),			(String)));

class CSV : public ZiCSV::Reader<Data> {
public:
  template <typename Path>
  void read(const Path &file) {
    this->readFile(file, [](const auto &scan) {
      Data data = scan.ctor();
      ZmHeapMgr::init(data.id, data.partition, ZmHeapConfig{
	  data.cacheSize,
	  data.cpuset});
    });
  }
};

template <typename Path>
inline void init(const Path &file) {
  if (file) CSV{}.read(file);
}

} // ZiHeapCSV

#endif /* ZiHeapCSV_HH */
