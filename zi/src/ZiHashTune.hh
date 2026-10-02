//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// hash table configuration and tuning utilities

#ifndef ZiHashTune_HH
#define ZiHashTune_HH

#ifndef ZiLib_HH
#include <zlib/ZiLib.hh>
#endif

#include <zlib/ZuID.hh>

#include <zlib/ZmHashMgr.hh>
#include <zlib/ZtcHash.hh>

#include <zlib/ZtPlatform.hh>

#include <zlib/ZfStruct.hh>

#include <zlib/ZiCSV.hh>
#include <zlib/ZiFile.hh>
#include <zlib/ZiFileTxStream.hh>

namespace ZiHashTune {

struct Data {
  ZuID		id;
  double	loadFactor;
  uint8_t	bits;
  uint8_t	cBits;
};

ZfStruct(ZiAPI, Data,
    (((id),		(Ctor<0>, Keys<0>)),	String),
    (((bits),		(Ctor<2>)),		UInt8),
    (((loadFactor),	(Ctor<1>)),		Float),
    (((cBits),		(Ctor<3>)),		UInt8));

class CSV : public ZiCSV::Reader<Data> {
public:
  template <typename Path>
  void read(const Path &file) {
    this->readFile(file, [](const auto &scan) {
      Data data = scan.ctor();
      ZmHashMgr::init(data.id, ZmHashParams{}.
	  bits(data.bits).
	  loadFactor(data.loadFactor).
	  cBits(data.cBits));
    });
  }
};

template <typename Path>
inline void init(const Path &file) {
  if (file) CSV{}.read(file);
}

inline void load() {
  init(Zt::getpath("Z_HASHTUNE"));
}

inline void save(double headroom = 0.05) {
  ZiFile file{Zt::getpath("Z_HASHTUNE"), ZiFile::Write | ZiFile::GC};
  if (!file) throw file.error();
  ZiFileTxStream<> stream{file};
  stream << Ztc::hashTuneCSV(headroom);
  if (!stream.flush()) {
    if (file.error()) throw file.error();
    throw ZeEXCEPT(Error, "ZiHashTune", "output failed");
  }
}

} // ZiHashTune

#endif /* ZiHashTune_HH */
