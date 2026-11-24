//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdlib.h>

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZiIOContext.hh>
#include <zlib/ZiMultiplex.hh>

using namespace ZuTestUtil;

void testInitAndCallback()
{
  ZuTestScope(testInitAndCallback);

  ZiTxContext io;
  ZiMultiplex allocatorMx;
  auto allocator = allocatorMx.allocator<Zi::IOBufType::App, ZiIOBuf, 8>();
  ZmRef<ZiIOBuf> buf = allocator->allocBuf();
  bool called = false;

  io.tos = uint8_t(3);
  io.init(ZiTxFn{[&called](ZiTxContext &ctx) {
      called = true;
      ctx.length = 3;
      ctx.complete();
      return true;
    }}, buf);

  ZuCheck(io.initialized());
  ZuCheck(io.buf == buf);
  ZuCheck(io.buf->size >= 8);
  ZuCheck(io.offset == 0);
  ZuCheck(!io.completed());
  ZuCheck(io.tos.template is<void>());

  ZuCheck(io());
  ZuCheck(called);
  ZuCheck(io.completed());
}

void testCompleteAndDisconnect()
{
  ZuTestScope(testCompleteAndDisconnect);

  ZiRxContext io;
  ZiMultiplex allocatorMx;
  auto allocator = allocatorMx.allocator<Zi::IOBufType::App, ZiIOBuf, 4>();
  ZmRef<ZiIOBuf> buf = allocator->allocBuf();

  io.tos = uint8_t(2);
  io.init(ZiRxFn{[](ZiRxContext &) { return true; }}, buf);
  ZuCheck(io.initialized());
  ZuCheck(io.tos.template is<void>());

  io.complete();
  ZuCheck(io.completed());
  ZuCheck(!io.disconnected());

  io.tos = uint8_t(1);
  io.init(ZiRxFn{[](ZiRxContext &) { return true; }}, buf);
  ZuCheck(io.tos.template is<void>());
  io.disconnect();
  ZuCheck(io.disconnected());
  ZuCheck(io.completed());
}

static void testTxTerminal()
{
  ZuTestScope(testTxTerminal);
  ZiMultiplex mx;
  auto allocator = mx.allocator<Zi::IOBufType::App, ZiIOBuf, 8>();
  auto first = allocator->allocBuf();
  auto later = allocator->allocBuf();
  *first << "data";
  ZiTxContext io;
  io.init(ZiTxFn{[](ZiTxContext &) { return true; }}, first);
  io.offset = 1;
  io.disconnect();
  io.disconnect();
  io.init(ZiTxFn{[](ZiTxContext &) { return false; }}, later);
  io.complete();
  ZuCheck(io.disconnected() && !io.closed() && !io.completed());
  ZuCheck(io.buf == first && io.offset == 1 && io());
  io.close();
  io.close();
  io.disconnect();
  ZuCheck(io.closed() && !io.disconnected() && io.completed() && !io.buf);
  ZiTxContext idle;
  idle.disconnect();
  ZuCheck(idle.disconnected() && idle.completed() && !idle.buf);
  idle.close();
  ZuCheck(idle.closed() && idle.completed());
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testInitAndCallback);
  ZuTestCall(testCompleteAndDisconnect);
  ZuTestCall(testTxTerminal);
  return 0;
}
