//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdlib.h>

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZiIOContext.hh>
#include <zlib/ZiIOBuf.hh>

using namespace ZuTestUtil;

void testInitAndCallback()
{
  ZuTestScope(testInitAndCallback);

  ZiIOContext io;
  ZmRef<ZiIOBuf> buf = new Zi::IOBufAlloc<ZiIOBuf, 8, ZiIOBuf_DefltMaxSize, ZiIOBuf_HeapID>{nullptr, 0};
  bool called = false;

  io.tos = uint8_t(3);
  io.init(ZiIOFn{[&called](ZiIOContext &ctx) {
      called = true;
      ctx.length = 3;
      ctx.complete();
      return true;
    }}, buf, 0);

  ZuCheck(io.initialized());
  ZuCheck(io.buf.ptr() == buf.ptr());
  ZuCheck(io.buf->size == buf->size);
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

  ZiIOContext io;
  ZmRef<ZiIOBuf> buf = new Zi::IOBufAlloc<ZiIOBuf, 4, ZiIOBuf_DefltMaxSize, ZiIOBuf_HeapID>{nullptr, 0};

  io.tos = uint8_t(2);
  io.init(ZiIOFn{[](ZiIOContext &) { return true; }}, buf, 1);
  ZuCheck(io.initialized());
  ZuCheck(io.tos.template is<void>());

  io.complete();
  ZuCheck(io.completed());
  ZuCheck(!io.disconnected());

  io.tos = uint8_t(1);
  io.init(ZiIOFn{[](ZiIOContext &) { return true; }}, buf, 0);
  ZuCheck(io.tos.template is<void>());
  io.disconnect();
  ZuCheck(io.disconnected());
  ZuCheck(io.completed());
}

// The owner counter outlives the context and any independent kernel/app hold.
struct ContextBuf : public ZiIOBuf {
  using ZiIOBuf::ZiIOBuf;
  ~ContextBuf() { ++*static_cast<unsigned *>(owner); }
};
using ContextAlloc = Zi::IOBufAlloc<ContextBuf, 32, 128,
  ZuStringT<"ZiIOContextTest.Buf">>;

void testMoveOwnsBufferAndMetadata()
{
  ZuTestScope(testMoveOwnsBufferAndMetadata);

  unsigned destroyed = 0, calls = 0;
  ZiIOContext source;
  ZmRef<ZiIOBuf> buf = new ContextAlloc{&destroyed, 0};
  *buf << "payload";
  auto original = buf.ptr();
  source.init(ZiIOFn{[&calls](ZiIOContext &io) {
    ++calls;
    return io.buf && io.offset == 2 && io.length == 3;
  }}, ZuMv(buf), 2, ZiSockAddr{ZiIP{"::1"}, 4242}, uint8_t(0x2e));
  source.length = 3;
  source.flags = MSG_PEEK;

  ZiIOContext moved{ZuMv(source)};
  ZuCheck(!source.buf && source.completed());
  ZuCheck(moved.buf.ptr() == original);
  ZuCheck(moved.buf->refCount() == 1);
  ZuCheck(moved.offset == 2 && moved.length == 3);
  ZuCheck(moved.flags == MSG_PEEK);
  ZuCheck(moved.addr.ip() == ZiIP{"::1"});
  ZuCheck(moved.addr.port() == 4242);
  ZuCheck(moved.tos.template is<uint8_t>());
  ZuCheck(moved.tos.template p<uint8_t>() == 0x2e);
  ZuCheck(moved());
  ZuCheck(calls == 1 && !destroyed);
  source.complete();
  ZuCheck(!destroyed);
  moved.complete();
  ZuCheck(destroyed == 1);
}

void testMoveAssignmentAndLastHold()
{
  ZuTestScope(testMoveAssignmentAndLastHold);

  unsigned oldDestroyed = 0, destroyed = 0, calls = 0;
  ZiIOContext source, destination;
  source.init(ZiIOFn{[&calls](ZiIOContext &) { ++calls; return true; }},
    new ContextAlloc{&destroyed, 0}, 7,
    ZiSockAddr{ZiIP{"127.0.0.1"}, 1234}, uint8_t(0));
  source.length = -1;
  source.flags = MSG_PEEK;
  destination.init(ZiIOFn{[](ZiIOContext &) { return false; }},
    new ContextAlloc{&oldDestroyed, 0});
  auto hold = source.buf;
  auto original = hold.ptr();

  destination = ZuMv(source);
  ZuCheck(oldDestroyed == 1 && !destroyed);
  ZuCheck(source.completed() && !source.buf);
  ZuCheck(destination.buf.ptr() == original);
  ZuCheck(destination.buf->refCount() == 2);
  ZuCheck(destination.offset == 7 && destination.length == -1);
  ZuCheck(destination.flags == MSG_PEEK);
  ZuCheck(destination.addr.ip() == ZiIP{"127.0.0.1"});
  ZuCheck(destination.addr.port() == 1234);
  ZuCheck(destination.tos.template is<uint8_t>());
  ZuCheck(destination.tos.template p<uint8_t>() == 0);
  ZuCheck(destination());
  ZuCheck(calls == 1);
  destination.disconnect();
  ZuCheck(destination.disconnected());
  ZuCheck(!destroyed && hold->refCount() == 1);
  hold = nullptr;
  ZuCheck(destroyed == 1);
}

void testEmptyDatagramRetainsItsOwner()
{
  ZuTestScope(testEmptyDatagramRetainsItsOwner);

  unsigned destroyed = 0;
  ZiIOContext source;
  source.init(ZiIOFn{[](ZiIOContext &io) {
    return io.buf && !io.buf->length && !io.length &&
      io.addr.port() == 4242 && io.tos.template is<uint8_t>();
  }}, new ContextAlloc{&destroyed, 0}, 0,
    ZiSockAddr{ZiIP{"127.0.0.1"}, 4242}, uint8_t(0));
  ZiIOContext moved{ZuMv(source)};
  ZuCheck(moved());
  ZuCheck(!destroyed && !moved.completed());
  moved.complete();
  ZuCheck(destroyed == 1);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testInitAndCallback);
  ZuTestCall(testCompleteAndDisconnect);
  ZuTestCall(testMoveOwnsBufferAndMetadata);
  ZuTestCall(testMoveAssignmentAndLastHold);
  ZuTestCall(testEmptyDatagramRetainsItsOwner);
  return 0;
}
