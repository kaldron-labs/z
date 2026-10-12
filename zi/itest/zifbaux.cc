//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef ZFLATC_HEADER
#define ZFLATC_HEADER <zflatcgen.hh>
#endif
#include ZFLATC_HEADER

unsigned flatcFields()
{
  using H = ZiFB::As<flatctest::Row>::Handler<flatctest::Row, ZuFacet::FB>;
  return H::SchemaFields::N;
}
