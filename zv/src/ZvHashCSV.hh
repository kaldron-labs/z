//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// hash table configuration

#ifndef ZvHashCSV_HH
#define ZvHashCSV_HH

#ifndef ZvLib_HH
#include <zlib/ZvLib.hh>
#endif

#include <zlib/ZuArray.hh>

#include <zlib/ZmHash.hh>

#include <zlib/ZtStruct.hh>
#include <zlib/ZvCSV.hh>

namespace ZvHashCSV {

struct Data {
  ZmIDString	id;
  double	loadFactor;
  uint8_t	bits;
  uint8_t	cBits;
};

ZtStruct(Data,
    (((id),		(Ctor<0>, Keys<0>)),	(String)),
    (((bits),		(Ctor<2>)),		(UInt8)),
    (((loadFactor),	(Ctor<1>)),		(Float)),
    (((cBits),		(Ctor<3>)),		(UInt8)));

class CSV : public ZvCSV<Data> {
public:
  void read(ZuCSpan file) {
    ZvCSV::readFile(file,
	[this]() { return &m_data; },
	[](Data *data) {
	  ZmHashMgr::init(data->id, ZmHashParams{}.
	      bits(data->bits).
	      loadFactor(data->loadFactor).
	      cBits(data->cBits));
	});
  }

private:
  Data	m_data;
};

inline void init(ZuCSpan file) {
  if (file) CSV{}.read(file);
}

} // ZvHashCSV

#endif /* ZvHashCSV_HH */
