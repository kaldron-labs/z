//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// consistent scanning of bool values

#ifndef ZtScanBool_HH
#define ZtScanBool_HH

#ifndef ZtLib_HH
#include <zlib/ZtLib.hh>
#endif

#include <zlib/ZuICmp.hh>
#include <zlib/ZuSpan.hh>

struct ZtBadBool { };

template <bool Validate = false>
inline bool ZtScanBool(ZuCSpan s)
{
  using Cmp = ZuICmp<ZuCSpan>;
  if (s == "1" ||
      Cmp::equals(s, "y") ||
      Cmp::equals(s, "yes") ||
      Cmp::equals(s, "true"))
    return true;
  if constexpr (Validate) {
    if (s == "0" ||
	Cmp::equals(s, "n") ||
	Cmp::equals(s, "no") ||
	Cmp::equals(s, "false"))
      return false;
    throw ZtBadBool{};
  } else
    return false;
}

#endif /* ZtScanBool_HH */
