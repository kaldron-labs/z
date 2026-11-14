//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef ZutcpConnectionFixture_HH
#define ZutcpConnectionFixture_HH

#include <zlib/Zutcp.hh>
#include "ZutcpTestAlloc.hh"

class TestApp : public Zutcp::Hub<TestApp> {
public:
  Zi::IOBufAllocator *dataTxAllocator() const { return Zutcp::inlineAllocator(); }
  TestApp() {
    ZiAssert(init(Zutcp::HubParams{&m_mx}.maxData(16)), "Zutcp", (),
      "connection test app init failed", abort());
  }
  ~TestApp() { final(); }
  bool rxInvoked() const { return true; }
  bool txInvoked() const { return true; }
  template <typename L> void rxRun(L l) { l(); }
  template <typename L> void txRun(L l) { l(); }
  template <typename L> void txInvoke(L l) { l(); }
  template <typename O, typename L> void txInvoke(O *, L l) { l(); }

private:
  ZiMultiplex m_mx;
};

class TestConnection : public ZmNHeap<TestConnection>,
  public Zutcp::Connection<TestApp, TestConnection, Zutcp::TxBufAlloc<>> {
friend ZuStringT<"Zutcp.ConnectionTest"> ZmNHeapID(TestConnection *);
  using Base = Zutcp::Connection<TestApp, TestConnection, Zutcp::TxBufAlloc<>>;
public:
  using ZmNHeap<TestConnection>::operator new;
  using ZmNHeap<TestConnection>::operator delete;
  TestConnection(TestApp *app) : Base{app} { configLocalParams(app); }
  int process(Zutcp::Reader &) { return 0; }
  bool flushTx() { return false; }
  void pto() { }
  void queueRetransmit() { }
  bool retransmit(bool = false, unsigned = 2) { return false; }
  bool retry(const Zutcp::TxRange &range, bool fin) {
    return Base::requeueDataRef(Zutcp::SentFrameRef::data(range, fin));
  }
  void resetRuntime() { Base::resetLink(); configLocalParams(app()); }
};

static ZmRef<ZiIOBuf> packet(TestApp &app, ZuBSpan bytes)
{
  auto buf = ZutcpTest::pktRxAllocator(app.mx())->allocBuf();
  memcpy(buf->data_(), bytes.data(), bytes.length());
  buf->length = bytes.length();
  return buf;
}

static Zutcp::ByteData dataFrame(ZiIOBuf *buf, uint64_t offset, bool fin = false)
{
  Zutcp::ByteData frame;
  frame.offset = offset;
  frame.length = buf->length;
  frame.payload = buf->cspan();
  frame.fin = fin;
  return frame;
}

static bool consume(Zutcp::Reader &rx, ZuBSpan expected)
{
  unsigned remaining = expected.length();
  bool matched = false;
  auto n = rx.consume([&remaining](ZuSpan<uint8_t> span) -> int64_t {
    if (remaining > span.length()) { remaining -= span.length(); return 0; }
    return remaining;
  }, [&matched, expected](ZuSpan<uint8_t> span) {
    matched = span.length() == expected.length() &&
      !memcmp(span.data(), expected.data(), expected.length());
  });
  return n == expected.length() && matched;
}

#endif
