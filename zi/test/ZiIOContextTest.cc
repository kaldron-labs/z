//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdlib.h>

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZiIOContext.hh>
#include <zlib/ZiIOBuf.hh>
#include <zlib/ZiMultiplex.hh>

using namespace ZuTestUtil;

// Physical connection identity is independent of a backing allocation owner.
class ContextConnection : public ZiConnection {
public:
  ContextConnection() : ZiConnection{nullptr, {}} { }
  void connected(ZiRxContext &) override { }
  void disconnected(bool) override { }
};

void testInitAndCallback()
{
  ZuTestScope(testInitAndCallback);

  ZiTxContext io;
  ZmRef<ZiIOBuf> buf = new Zi::IOBufAlloc<ZiIOBuf, 8, ZiIOBuf_DefltMaxSize, ZiIOBuf_HeapID>{nullptr, 0};
  bool called = false;

  io.tos = uint8_t(3);
  io.init(ZiTxFn{[&called](ZiTxContext &ctx) {
      called = true;
      ctx.length = 3;
      ctx.complete();
      return true;
    }}, buf);

  ZuCheck(io.initialized());
  ZuCheck(io.buf.ptr() == buf.ptr());
  ZuCheck(io.buf->size == buf->size);
  ZuCheck(io.buf->skip == 0 && !io.length);
  ZuCheck(!io.completed());
  ZuCheck(io.tos.template is<void>());

  ZuCheck(io());
  ZuCheck(called);
  ZuCheck(io.completed());
}

void testCompleteAndDisconnect()
{
  ZuTestScope(testCompleteAndDisconnect);

  ZiTxContext io;
  ZmRef<ZiIOBuf> buf = new Zi::IOBufAlloc<ZiIOBuf, 4, ZiIOBuf_DefltMaxSize, ZiIOBuf_HeapID>{nullptr, 0};

  io.tos = uint8_t(2);
  buf->length = 1;
  buf->advance(1);
  io.init(ZiTxFn{[](ZiTxContext &) { return true; }}, buf);
  ZuCheck(io.initialized());
  ZuCheck(io.tos.template is<void>());

  io.complete();
  ZuCheck(io.completed());
  ZuCheck(!io.disconnected());

  io.tos = uint8_t(1);
  io.init(ZiTxFn{[](ZiTxContext &) { return true; }}, buf);
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

  ContextConnection cxn;
  unsigned destroyed = 0, calls = 0;
  ZiTxContext source;
  ZmRef<ZiIOBuf> buf = new ContextAlloc{&destroyed, 0};
  *buf << "payload";
  auto original = buf.ptr();
  buf->advance(2);
  source.cxn = &cxn;
  source.init(ZiTxFn{[&calls](ZiTxContext &io) {
    ++calls;
    return io.buf && io.buf->skip == 2 && io.length == 3;
  }}, ZuMv(buf), ZiSockAddr{ZiIP{"::1"}, 4242}, uint8_t(0x2e));
  source.length = 3;
  source.txRange(source.buf, 2, 3);
  source.flags |= ZiTxContext::TxDataDone | ZiTxContext::Disconnect;

  ZiTxContext moved{ZuMv(source)};
  ZuCheck(!source.buf && source.completed());
  ZuCheck(moved.buf.ptr() == original && moved.cxn == &cxn);
  ZuCheck(moved.buf->refCount() == 1);
  ZuCheck(moved.buf->skip == 2 && moved.length == 3);
  ZuCheck(moved.flags == (ZiTxContext::TxDataDone |
    ZiTxContext::Disconnect | ZiTxContext::Range));
  ZuCheck(moved.txOffset == 2 && moved.txRemaining == 3);
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

  ContextConnection cxn;
  unsigned oldDestroyed = 0, destroyed = 0, calls = 0;
  ZiTxContext source, destination;
  ZmRef<ZiIOBuf> buf = new ContextAlloc{&destroyed, 0};
  *buf << "payload";
  buf->advance(7);
  source.cxn = &cxn;
  source.init(ZiTxFn{[&calls](ZiTxContext &) { ++calls; return true; }},
    ZuMv(buf),
    ZiSockAddr{ZiIP{"127.0.0.1"}, 1234}, uint8_t(0));
  source.length = -1;
  source.txRange(source.buf, 2, 3);
  source.flags |= ZiTxContext::TxDataDone | ZiTxContext::Disconnect;
  destination.init(ZiTxFn{[](ZiTxContext &) { return false; }},
    new ContextAlloc{&oldDestroyed, 0});
  auto hold = source.buf;
  auto original = hold.ptr();

  destination = ZuMv(source);
  ZuCheck(oldDestroyed == 1 && !destroyed);
  ZuCheck(source.completed() && !source.buf);
  ZuCheck(destination.buf.ptr() == original && destination.cxn == &cxn);
  ZuCheck(destination.buf->refCount() == 2);
  ZuCheck(destination.buf->skip == 7 && destination.length == -1);
  ZuCheck(destination.flags == (ZiTxContext::TxDataDone |
    ZiTxContext::Disconnect | ZiTxContext::Range));
  ZuCheck(destination.txOffset == 2 && destination.txRemaining == 3);
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
  ZiRxContext source;
  source.init(ZiRxFn{[](ZiRxContext &io) {
    return io.buf && !io.buf->length && io.flags == MSG_PEEK &&
      io.addr.port() == 4242 && io.tos.template is<uint8_t>();
  }}, new ContextAlloc{&destroyed, 0});
  source.addr = ZiSockAddr{ZiIP{"127.0.0.1"}, 4242};
  source.tos = uint8_t(0);
  source.flags = MSG_PEEK;
  ZiRxContext moved{ZuMv(source)};
  ZuCheck(source.completed() && !source.buf);
  ZuCheck(moved());
  ZuCheck(!destroyed && !moved.completed());
  moved.complete();
  ZuCheck(destroyed == 1 && moved.flags == MSG_PEEK);
}

void testIndependentTransmitRanges()
{
  ZuTestScope(testIndependentTransmitRanges);
  ZmRef<ZiIOBuf> backing = new Zi::IOBufAlloc<ZiIOBuf, 64,
    ZiIOBuf_DefltMaxSize, ZiIOBuf_HeapID>{nullptr, 0};
  *backing << "prefix-message-tail";
  ZiTxContext first, second;
  first.txRange(backing, 7, 7);
  second.txRange(backing, 7, 7);
  first.txAdvance(3);
  ZuCheck(first.txOffset == 10 && first.txRemaining == 4);
  ZuCheck(second.txOffset == 7 && second.txRemaining == 7);
  ZuCheck(backing->skip == 0 && backing->cspan() == "prefix-message-tail");
  auto selected = second.txData();
  second.txAdvance(7);
  ZuCheck(first.txData() == selected + 3 && !second.txRemaining);
  ZuCheck(backing->cspan() == "prefix-message-tail");
}

void testReceiveMoveAndNativeFlags()
{
  ZuTestScope(testReceiveMoveAndNativeFlags);
  ContextConnection cxn;
  unsigned oldDestroyed = 0, destroyed = 0, calls = 0;
  ZiRxContext source, destination;
  ZmRef<ZiIOBuf> buf = new ContextAlloc{&destroyed, 0};
  *buf << "payload";
  source.cxn = &cxn;
  source.init(ZiRxFn{[&calls](ZiRxContext &io) {
    ++calls;
    return io.buf && io.buf->length == 7 && io.flags == MSG_PEEK &&
      io.disconnected();
  }}, ZuMv(buf));
  source.addr = ZiSockAddr{ZiIP{"127.0.0.1"}, 4242};
  source.tos = uint8_t(0x2e);
  source.flags = MSG_PEEK;
  source.state |= ZiRxContext::Disconnect;
  destination.init(ZiRxFn{[](ZiRxContext &) { return false; }},
    new ContextAlloc{&oldDestroyed, 0});
  auto hold = source.buf;
  auto original = hold.ptr();
  destination = ZuMv(source);
  ZuCheck(oldDestroyed == 1 && !destroyed);
  ZuCheck(source.completed() && !source.buf);
  ZuCheck(destination.cxn == &cxn && destination.buf.ptr() == original);
  ZuCheck(destination.buf->refCount() == 2 && destination.flags == MSG_PEEK);
  ZuCheck(destination.addr.port() == 4242 && destination.tos.p<uint8_t>() == 0x2e);
  ZuCheck(destination() && calls == 1 && destination.disconnected());
  destination.complete();
  ZuCheck(destination.completed() && destination.flags == MSG_PEEK &&
    destination.disconnected() && destination.cxn == &cxn);
  ZuCheck(!destroyed && hold->refCount() == 1);
  hold = nullptr;
  ZuCheck(destroyed == 1);
  destination.init(ZiRxFn{[](ZiRxContext &io) { return !io.buf; }});
  ZuCheck(!destination.flags && !destination.state &&
    destination.tos.is<void>() && destination.cxn == &cxn &&
    destination.addr.port() == 4242);
  ZuCheck(destination() && !destination.completed());
}

void testReceiveAvailableInput()
{
  ZuTestScope(testReceiveAvailableInput);
  unsigned destroyed = 0, calls = 0, available = 0;
  ZiRxContext io;
  ZmRef<ZiIOBuf> buf = new ContextAlloc{&destroyed, 0};
  *buf << "ab";
  io.init(ZiRxFn{[&calls, &available](ZiRxContext &ctx) {
    ++calls;
    available = ctx.buf->length;
    return calls > 1;
  }}, ZuMv(buf));
  io.flags = MSG_PEEK;
  ZuCheck(!io() && calls == 1 && available == 2);
  *io.buf << "cde";
  ZuCheck(io() && calls == 2 && available == 5);
  ZuCheck(io.flags == MSG_PEEK && !io.completed());
  io.disconnect();
  ZuCheck(destroyed == 1 && io.completed() && io.disconnected() &&
    io.flags == MSG_PEEK);
}

void testTransmitPackedTransitions()
{
  ZuTestScope(testTransmitPackedTransitions);
  ZiTxContext io;
  ZmRef<ZiIOBuf> backing = new Zi::IOBufAlloc<ZiIOBuf, 16,
    ZiIOBuf_DefltMaxSize, ZiIOBuf_HeapID>{nullptr, 0};
  *backing << "payload";
  io.init(ZiTxFn{[](ZiTxContext &ctx) {
    return ctx.txRemaining == UINT_MAX && !(ctx.flags & ZiTxContext::Range) &&
      (ctx.flags & ZiTxContext::TxDataDone) && ctx.disconnected();
  }}, backing);
  io.flags = ZiTxContext::TxDataDone | ZiTxContext::Disconnect;
  io.txRange(backing, 1, 3);
  io.txSelect();
  io.txAdvance(1);
  ZuCheck(io.flags == (ZiTxContext::TxDataDone | ZiTxContext::Disconnect |
    ZiTxContext::Range));
  ZuCheck(io.txOffset == 2 && io.txRemaining == 2 &&
    backing->skip == 0 && backing->length == 7);
  ZuCheck(io());
  io.length = 2;
  io.complete();
  ZuCheck(io.flags == (ZiTxContext::TxDataDone | ZiTxContext::Disconnect) &&
    io.length == 2 && io.txRemaining == UINT_MAX);
  io.txRange(backing, 0, 7);
  io.flags &= ~ZiTxContext::TxDataDone;
  ZuCheck(io.flags == (ZiTxContext::Disconnect | ZiTxContext::Range) &&
    io.txRemaining == 7 && io.buf == backing);
  io.init(ZiTxFn{[](ZiTxContext &) { return true; }});
  ZuCheck(!io.flags && !io.length && !io.buf &&
    io.txRemaining == UINT_MAX && io.tos.is<void>());
}

void testTransmitResultWithoutBacking()
{
  ZuTestScope(testTransmitResultWithoutBacking);
  ContextConnection cxn;
  ZiTxContext io;
  unsigned calls = 0;
  io.cxn = &cxn;
  io.init(ZiTxFn{[&cxn, &calls](ZiTxContext &ctx) {
    ++calls;
    return !ctx.buf && ctx.cxn == &cxn && ctx.length == -1 &&
      (ctx.flags & ZiTxContext::TxDataDone) && ctx.disconnected();
  }});
  io.length = -1;
  io.flags = ZiTxContext::TxDataDone | ZiTxContext::Disconnect;
  ZuCheck(io() && calls == 1);
  io.complete();
  ZuCheck(io.completed() && !io.buf && io.cxn == &cxn && io.length == -1 &&
    io.flags == (ZiTxContext::TxDataDone | ZiTxContext::Disconnect));
}

void testTransportResultAndConstructionStatus()
{
  ZuTestScope(testTransportResultAndConstructionStatus);
  unsigned destroyed = 0;
  ZmRef<ZiIOBuf> backing = new ContextAlloc{&destroyed, 0};
  *backing << "payload";
  ZuCheck(!backing->failed());
  auto deniedGrowth = [](unsigned size, unsigned) { return size; };
  ZuCheck(!backing->ensure<deniedGrowth>(backing->size + 1) && backing->failed());
  ZiTxContext first, second;
  first.txRange(backing, 0, 7);
  second.txRange(backing, 0, 7);
  first.length = -1;
  second.length = 3;
  first.complete();
  ZuCheck(first.length == -1 && second.length == 3 && backing->failed());
  ZuCheck(second.txRemaining == 7 && backing->skip == 0 &&
    backing->cspan() == "payload");
  backing = nullptr;
  ZuCheck(!destroyed);
  second.complete();
  ZuCheck(destroyed == 1 && second.length == 3);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testIndependentTransmitRanges);
  ZuTestCall(testInitAndCallback);
  ZuTestCall(testCompleteAndDisconnect);
  ZuTestCall(testMoveOwnsBufferAndMetadata);
  ZuTestCall(testMoveAssignmentAndLastHold);
  ZuTestCall(testEmptyDatagramRetainsItsOwner);
  ZuTestCall(testReceiveMoveAndNativeFlags);
  ZuTestCall(testReceiveAvailableInput);
  ZuTestCall(testTransmitPackedTransitions);
  ZuTestCall(testTransmitResultWithoutBacking);
  ZuTestCall(testTransportResultAndConstructionStatus);
  return 0;
}
