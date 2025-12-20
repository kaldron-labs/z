//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// ZtStack configuration

#ifndef ZvStackParams_HH
#define ZvStackParams_HH

#ifndef ZvLib_HH
#include <zlib/ZvLib.hh>
#endif

#include <zlib/ZtStack.hh>

#include <zlib/ZvCf.hh>
#include <zlib/ZvCSV.hh>

struct ZvStackParams : public ZtStackParams {
  ZvStackParams(const ZtStackParams &p) : ZtStackParams{p} { }
  ZvStackParams &operator =(const ZtStackParams &p) {
    ZtStackParams::operator =(p);
    return *this;
  }
  ZvStackParams(ZtStackParams &&p) : ZtStackParams{ZuMv(p)} { }
  ZvStackParams &operator =(ZtStackParams &&p) {
    ZtStackParams::operator =(ZuMv(p));
    return *this;
  }

  ZvStackParams(const ZvCf *cf) : ZtStackParams() { init(cf); }
  ZvStackParams(const ZvCf *cf, ZtStackParams deflt) :
      ZtStackParams{ZuMv(deflt)} { init(cf); }

  void init(const ZvCf *cf) {
    ZtStackParams::operator =(ZtStackParams());

    if (!cf) return;

    initial(cf->getInt("initial", 2, 28, initial());
    increment(cf->getInt("increment", 0, 12, increment());
    maxFrag(cf->getDbl("maxFrag", 1, 256, maxFrag());
  }
};

#endif /* ZvStackParams_HH */
