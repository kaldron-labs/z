//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// command line interface - in-memory history

#include <zlib/ZrlHistory.hh>

namespace Zrl {

void History::save(unsigned i, ZuBSpan s)
{
  if (s) set(i, s);
}

bool History::load(unsigned i, HistFn fn) const
{
  if (auto s_ = ptr(i)) {
    fn(*s_);
    return true;
  }
  return false;
}

} // namespace Zrl
