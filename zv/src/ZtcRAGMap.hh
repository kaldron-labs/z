//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// reflected names for the common telemetry RAG vocabulary

#ifndef ZtcRAGMap_HH
#define ZtcRAGMap_HH

#ifndef ZvLib_HH
#include <zlib/ZvLib.hh>
#endif

#include <zlib/ZtEnum.hh>

#include <zlib/ZtcTypes.hh>

namespace Ztc {
namespace RAG {

enum { N = Green + 1 };
ZtEnumNames(RAG, Off, Red, Amber, Green);
struct Map : public Map_ { };

} // RAG
} // Ztc

#endif /* ZtcRAGMap_HH */
