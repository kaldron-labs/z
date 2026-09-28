//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// heap configuration and tuning utilities

#ifndef ZiHeapTune_HH
#define ZiHeapTune_HH

#ifndef ZiLib_HH
#include <zlib/ZiLib.hh>
#endif

#include <zlib/ZuID.hh>

#include <zlib/ZmBitmap.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZtcHeap.hh>

#include <zlib/ZtPlatform.hh>

#include <zlib/ZfStruct.hh>

#include <zlib/ZiCSV.hh>
#include <zlib/ZiFile.hh>
#include <zlib/ZiFileTxStream.hh>

namespace ZiHeapTune {

struct Data {
  ZuID		id;
  uint64_t	cacheSize;
  uint16_t	partition;
  uint8_t	vshift;
  ZmBitmap	cpuset;
};

ZfStruct(ZiAPI, Data,
    (((id),		(Ctor<0>, Keys<0>, Group<0>)),	(String)),
    (((partition),	(Ctor<2>, Keys<0>)),		(UInt16)),
    (((vshift),		(Ctor<3>, Keys<0>)),		(UInt8)),
    (((cacheSize),	(Ctor<1>)),			(UInt64)),
    (((cpuset),		(Ctor<4>)),			(String)));

class CSV : public ZiCSV::Reader<Data> {
public:
  template <typename Path>
  void read(const Path &file) {
    this->readFile(file, [](const auto &scan) {
      Data data = scan.ctor();
      ZmHeapMgr::init(data.id, data.partition, data.vshift,
	ZmHeapConfig{data.cacheSize, data.cpuset});
    });
  }
};

template <typename Path>
inline void init(const Path &file) {
  if (file) CSV{}.read(file);
}

inline void load() {
  init(Zt::getpath("Z_HEAPTUNE"));
}

inline void save(double headroom = 0.05) {
  ZiFile file{Zt::getpath("Z_HEAPTUNE"), ZiFile::Write | ZiFile::GC};
  if (!file) throw file.error();
  ZiFileTxStream<> stream{file};
  stream << Ztc::heapTuneCSV(headroom);
  stream.flush();
  if (file.error()) throw file.error();
}

} // ZiHeapTune

#endif /* ZiHeapTune_HH */
