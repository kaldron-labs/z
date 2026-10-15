//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/Zfb.hh>
#include "zfbmap_user.hh"

bool mapConsume(const ZuBSpan &bytes)
{
  if (!Zfb::verify<map_import::Holder>(bytes)) return false;
  auto output = Zfb::load<map_import::Holder>(bytes);
  auto node = output.values.find("base");
  return node && node->val() && node->val()->value == 17 &&
    output.next && output.next->values.find("nested");
}
