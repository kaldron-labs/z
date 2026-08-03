//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <errno.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZePlatform.hh>

using namespace ZuTestUtil;

static void errors()
{
  ZuTestScope(errors);
  ZeError ok;
  ZuCHECK(!ok, "default error is OK");
  ZeError invalid{EINVAL};
  ZuCHECK(invalid && invalid.errNo() == EINVAL, "native error retained");
  ZuCHECK(invalid.message() && *invalid.message(), "native error formatted");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(errors);
  return 0;
}
