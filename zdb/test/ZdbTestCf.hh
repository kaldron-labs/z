//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Raw configuration sections retained by database fixtures.

#ifndef ZdbTestCf_HH
#define ZdbTestCf_HH

#include <zlib/ZfCf.hh>

namespace zdbtest {

struct ConfigFields {
  ZfCf::Union<> mx;
  ZfCf::Union<> zdb;
  ZfCf::Union<> store;
};
ZfStruct(, (ConfigFields, Cf),
  (mx,		(Ctor<0>),	UDT),
  (zdb,		(Ctor<1>),	UDT),
  (store,	(Ctor<2>),	UDT));

} // zdbtest

#endif /* ZdbTestCf_HH */
