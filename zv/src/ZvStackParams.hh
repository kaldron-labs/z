//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// ZmStack configuration

#ifndef ZvStackParams_HH
#define ZvStackParams_HH

#ifndef ZvLib_HH
#include <zlib/ZvLib.hh>
#endif

#include <zlib/ZmStack.hh>

#include <zlib/ZvCf.hh>
#include <zlib/ZvCSV.hh>

struct ZvStackParams : public ZmStackParams {
  ZvStackParams(const ZmStackParams &p) : ZmStackParams{p} { }
  ZvStackParams &operator =(const ZmStackParams &p) {
    ZmStackParams::operator =(p);
    return *this;
  }
  ZvStackParams(ZmStackParams &&p) : ZmStackParams{ZuMv(p)} { }
  ZvStackParams &operator =(ZmStackParams &&p) {
    ZmStackParams::operator =(ZuMv(p));
    return *this;
  }

  ZvStackParams(const ZvCf *cf) : ZmStackParams() { init(cf); }
  ZvStackParams(const ZvCf *cf, ZmStackParams deflt) :
      ZmStackParams{ZuMv(deflt)} { init(cf); }

  void init(const ZvCf *cf) {
    ZmStackParams::operator =(ZmStackParams());

    if (!cf) return;

    initial(cf->getInt("initial", 2, 28, initial()));
    maxFrag(cf->getDbl("maxFrag", 1, 256, maxFrag()));
  }
};

#endif /* ZvStackParams_HH */
