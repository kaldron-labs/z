//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdlib.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZtString.hh>

using namespace ZuTestUtil;

static void boundary()
{
  ZuTestScope(boundary);
  ZtString<> cmd;
  cmd << "sh \"" << ZWS_TEST_SRCDIR << "/ZwsSourceBoundaryFixture.sh\" \"" <<
    ZWS_LIB_SRCDIR << '"';
  ZuCHECK(!::system(cmd.data()), "WebSocket stream source boundary violation");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(boundary);
  return 0;
}
