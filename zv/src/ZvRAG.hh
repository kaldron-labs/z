//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// red/amber/green enumeration

#ifndef ZvRAG_HH
#define ZvRAG_HH

#ifndef ZvLib_HH
#include <zlib/ZvLib.hh>
#endif

#include <zlib/ZtEnum.hh>

namespace ZvRAG {
  ZtEnum(ZvRAG, int8_t, Off, Red, Amber, Green);
}

#endif /* ZvRAG_HH */
