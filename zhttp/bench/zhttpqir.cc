//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include "ZhttpQIR.hh"

#include <stdio.h>

int main(int argc, char **argv)
{
  using namespace Zhttp::QIR;

  if (argc == 2 && ZuCSpan{argv[1]} == "--help") {
    usage();
    return OK;
  }
  if (argc != 2) {
    usage();
    return Usage;
  }
  Role role;
  if (!parseRole(argv[1], role)) {
    usage();
    return Usage;
  }
  return run(role);
}
