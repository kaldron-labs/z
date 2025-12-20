//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// pre-declare std::basic_string without pulling in all of STL

#ifndef ZuStdString_HH
#define ZuStdString_HH

#ifndef ZuLib_HH
#include <zlib/ZuLib.hh>
#endif

#ifdef __GNUC__
#include <bits/stringfwd.h>
#else
namespace std {
  template <class, class, class> class basic_string;
}
#endif

#endif /* ZuStdString_HH */
