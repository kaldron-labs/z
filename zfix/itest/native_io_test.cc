//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdlib.h>
#include <zlib/ZuTestUtil.hh>

using namespace ZuTestUtil;

static void processes()
{
  ZuTestScope(processes);
  ZuCheck(::system("python3 \"${ZFIX_IO_DIR:-.}/native_io_test.py\" "
    "--client \"${ZFIX_IO_CLIENT:-../util/zfix}\" "
    "--server \"${ZFIX_IO_SERVER:-../util/zfixd}\" 1>&2") == 0);
}
int main(int argc, char **argv)
{
  parse(argc, argv); ZuTestMain();
  ZuTestCall(processes);
}
