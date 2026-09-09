//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// hash table configuration

#ifndef ZiHashCSV_HH
#define ZiHashCSV_HH

#ifndef ZiLib_HH
#include <zlib/ZiLib.hh>
#endif

#include <zlib/ZuID.hh>

#include <zlib/ZmHash.hh>

#include <zlib/ZfStruct.hh>

#include <zlib/ZiCSV.hh>

namespace ZiHashCSV {

struct Data {
  ZuID		id;
  double	loadFactor;
  uint8_t	bits;
  uint8_t	cBits;
};

ZfStruct(ZiAPI, Data,
    (((id),		(Ctor<0>, Keys<0>)),	(String)),
    (((bits),		(Ctor<2>)),		(UInt8)),
    (((loadFactor),	(Ctor<1>)),		(Float)),
    (((cBits),		(Ctor<3>)),		(UInt8)));

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

inline void init(ZuCSpan file) {
  if (file) CSV{}.read(file);
}

} // ZiHashCSV

#endif /* ZiHashCSV_HH */
