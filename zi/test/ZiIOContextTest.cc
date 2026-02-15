//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdlib.h>

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZiIOContext.hh>

using namespace ZuTestUtil;

void testInitAndCallback()
{
  ZuTestScope(testInitAndCallback);

  ZiIOContext io;
  char buf[8] = {0};
  bool called = false;

  io.init(ZiIOFn{[&called](ZiIOContext &ctx) {
      called = true;
      ctx.length = 3;
      ctx.complete();
      return true;
    }}, buf, sizeof(buf), 0);

  ZuCheck(io.initialized());
  ZuCheck(io.ptr == reinterpret_cast<uint8_t *>(buf));
  ZuCheck(io.size == sizeof(buf));
  ZuCheck(io.offset == 0);
  ZuCheck(!io.completed());

  ZuCheck(io());
  ZuCheck(called);
  ZuCheck(io.completed());
}

void testCompleteAndDisconnect()
{
  ZuTestScope(testCompleteAndDisconnect);

  ZiIOContext io;
  char buf[4] = {0};

  io.init(ZiIOFn{[](ZiIOContext &) { return true; }}, buf, sizeof(buf), 1);
  ZuCheck(io.initialized());

  io.complete();
  ZuCheck(io.completed());
  ZuCheck(!io.disconnected());

  io.init(ZiIOFn{[](ZiIOContext &) { return true; }}, buf, sizeof(buf), 0);
  io.disconnect();
  ZuCheck(io.disconnected());
  ZuCheck(io.completed());
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testInitAndCallback);
  ZuTestCall(testCompleteAndDisconnect);
  return 0;
}
