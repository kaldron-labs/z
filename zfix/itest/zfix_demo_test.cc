//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdlib.h>
#include <zlib/ZuTestUtil.hh>

using namespace ZuTestUtil;

static void tcp()
{
  ZuTestScope(tcp);
  ZuCheck(::system("sh \"${ZFIX_DEMO_DIR:-.}/zfix_demo_check.sh\" "
    "\"${ZFIX_DEMO_PROGRAM:-./zfix_demo}\" 1>&2") == 0);
}
static void tls()
{
  ZuTestScope(tls);
  ZuCheck(::system("sh \"${ZFIX_DEMO_DIR:-.}/zfix_demo_tls_check.sh\" "
    "\"${ZFIX_DEMO_PROGRAM:-./zfix_demo}\" 1>&2") == 0);
}
int main(int argc, char **argv)
{
  parse(argc, argv); ZuTestMain();
  ZuTestCall(tcp);
  ZuTestCall(tls);
}
