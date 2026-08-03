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
  cmd << "sh \"" << ZHTTP_TEST_SRCDIR << "/ZhttpBoundaryFixture.sh\" \"" <<
    ZHTTP_UTIL_SRCDIR << "\" \"" << ZHTTP_LIB_SRCDIR << '"';
  ZuCHECK(!::system(cmd.data()), "zhttp utility boundary violation");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(boundary);
  return 0;
}
