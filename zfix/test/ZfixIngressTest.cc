//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZfixIngress.hh>

using namespace ZuTestUtil;
using namespace Zfix;
static void budget() {
  ZuTestScope(budget);
  PostedIngress b{{1024}};
  ZuCheck(!b.acquire_(0));
  ZuCheck(b.acquire_(512));
  ZuCheck(b.acquire_(512));
  ZuCheck(!b.acquire_(1));
  ZuCheck(!b.limits_({1023}));
  ZuCheck(b.limits_().bytes == 1024);
  auto s = b.state_();
  ZuCheck(s.bytes == 1024 && s.count == 2 && s.peak == 1024 && s.refused == 2);
  b.release_(512);
  ZuCheck(b.limits_({512}));
  ZuCheck(!b.acquire_(1));
  b.release_(512);
  ZuCheck(b.limits_({0}));
  ZuCheck(!b.acquire_(1));
  ZuCheck(b.limits_({2048}));
  ZuCheck(b.acquire_(2048)); b.release_(2048);
  s = b.state_();
  ZuCheck(!s.bytes && !s.count && s.peak == 2048 && s.refused == 4);
}
static void overflow() {
  ZuTestScope(overflow);
  PostedIngress b{{UINT64_MAX}};
  ZuCheck(b.acquire_(UINT64_MAX - 1));
  ZuCheck(!b.acquire_(2));
  ZuCheck(b.acquire_(1));
  ZuCheck(!b.acquire_(1));
  b.release_(UINT64_MAX - 1); b.release_(1);
  auto s = b.state_();
  ZuCheck(!s.bytes && !s.count && s.peak == UINT64_MAX && s.refused == 2);
}
int main(int argc, char **argv) {
  parse(argc, argv); ZuTestMain();
  ZuTestCall(budget);
  ZuTestCall(overflow);
  return 0;
}
