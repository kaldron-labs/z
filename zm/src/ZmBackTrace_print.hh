//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// backtrace printing

#ifndef ZmBackTrace_print_HH
#define ZmBackTrace_print_HH

#ifndef ZmLib_HH
#include <zlib/ZmLib.hh>
#endif

#ifndef ZmBackTrace__HH
#include <zlib/ZmBackTrace_.hh>
#endif

#include <zlib/ZuTraits.hh>
#include <zlib/ZuPrint.hh>
#include <zlib/ZuArray.hh>

#include <zlib/ZuVStream.hh>

ZmExtern void ZmBackTrace_print(ZuVStream &s, const ZmBackTrace &bt);

// generic printing
struct ZmBackTrace_Print : public ZuPrintDelegate {
  template <typename S>
  static void print(S &s_, const ZmBackTrace &bt) {
    ZuVStream s{s_};
    ZmBackTrace_print(s, bt);
  }
  static void print(ZuVStream &s, const ZmBackTrace &bt) {
    ZmBackTrace_print(s, bt);
  }
};

#endif /* ZmBackTrace_print_HH */
