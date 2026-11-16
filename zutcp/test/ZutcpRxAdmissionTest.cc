//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/Zutcp.hh>
#include "ZutcpTestAlloc.hh"

using namespace ZuTestUtil;
using namespace Zutcp;

struct AdmissionApp : Hub<AdmissionApp> {
  ZiMultiplex multiplex;
  bool allow = true;
  unsigned demands = 0;

  AdmissionApp() { ZmAssert_(init(HubParams{&multiplex}.maxData(16))); }
  ~AdmissionApp() { final(); }
  bool rxInvoked() const { return true; }
  bool txInvoked() const { return true; }
  template <typename L> void rxRun(L l) { l(); }
  template <typename L> void txRun(L l) { l(); }
  template <typename L> void txInvoke(L l) { l(); }
  template <typename O, typename L> void txInvoke(O *, L l) { l(); }
  // The native pool/account bounds are separately exercised by RxAccounting
  // and the real Stack fixture. This seam drives connection resource refusal.
  bool retainRx(ZiIOBuf *, ZmRef<RxAccount> &, uint64_t) {
    ++demands;
    return allow;
  }
};

struct AdmissionCxn : ZmNHeap<AdmissionCxn>,
  Connection<AdmissionApp, AdmissionCxn, TxBufAlloc<>> {
friend inline ZuStringT<"Zutcp.Test.RxAdmission"> ZmNHeapID(AdmissionCxn *);
  using Base = Zutcp::Connection<AdmissionApp, AdmissionCxn, Zutcp::TxBufAlloc<>>;
  using ZmNHeap<AdmissionCxn>::operator new;
  using ZmNHeap<AdmissionCxn>::operator delete;
  unsigned controls = 0;

  AdmissionCxn(AdmissionApp *app) : Base{app} { configLocalParams(app); }
  int process(Reader &) { return 0; } // deliberately hold delivered bytes
  bool flushTx(ZiSockAddr = {}) { return false; }
  void queueTxFlush(ZiSockAddr = {}) { }
  void queueRetransmit() { }
  void pto() { }
  bool retransmit(bool = false, unsigned = 2) { return false; }
  void finish() { Base::resetLink(); }
  int receive(const ByteData &data, ZmRef<ZiIOBuf> packet, bool *admitted = nullptr) {
    bool refused = false;
    int result = Base::processFrame(data, ZuMv(packet), nullptr, &refused);
    if (admitted) *admitted = !refused;
    return refused ? 0 : result;
  }

};

static ZmRef<ZiIOBuf> bytes(AdmissionApp &app, ZuBSpan data)
{
  auto buf = ZutcpTest::pktTxAllocator(app.mx())->allocBuf();
  memcpy(buf->data(), data.data(), data.length());
  buf->length = data.length();
  return buf;
}

static ByteData dataFrame(ZiIOBuf *buf, uint64_t offset)
{
  ByteData frame;
  frame.offset = offset;
  frame.length = buf->length;
  frame.payload = buf->cspan();
  return frame;
}

static void testRefusedData()
{
  ZuTestScope(testRefusedData);
  AdmissionApp app;
  ZmRef<AdmissionCxn> cxn = new AdmissionCxn{&app};
  auto source = bytes(app, "tail");
  app.allow = false;
  bool admitted = true;
  ZuCHECK(cxn->receive(dataFrame(source, 4), source, &admitted) == 0 &&
      !admitted && !cxn->rxPending() && !cxn->rxCreditUsed() &&
      !cxn->rxAdmitted() && cxn->rxError() == DataError::None,
    "resource refusal must not commit bytes, final size or a protocol error");
  app.allow = true;
  ZuCHECK(cxn->receive(dataFrame(source, 4), source, &admitted) == 0 &&
      admitted && cxn->rxPending() == 1 && cxn->rxAdmitted() == 4,
    "the same unacknowledged range must be admissible after capacity returns");
  unsigned demands = app.demands;
  ZuCHECK(cxn->receive(dataFrame(source, 4), source) == 0 && app.demands == demands,
    "duplicate bytes must demand no additional physical frame");
  cxn->closeRx();
}

static void testGapService()
{
  ZuTestScope(testGapService);
  AdmissionApp app;
  ZmRef<AdmissionCxn> cxn = new AdmissionCxn{&app};
  auto tail = bytes(app, "tail");
  auto head = bytes(app, "head");
  ZuCHECK(cxn->processFrame(dataFrame(tail, 4), tail) == 0 && cxn->rxPending() == 1,
    "service fixture must retain an out-of-order range");
  app.allow = false;
  unsigned refs = head->refCount();
  ZuCHECK(cxn->processFrame(dataFrame(head, 0), head) == 0 && !cxn->rxPending() &&
      cxn->rxBytes() == 8 && head->refCount() == refs,
    "gap service must drain contiguous input without retaining its ingress backing");
  bool exact = false;
  unsigned remaining = 8;
  auto consumed = cxn->reader().consume(
    [&remaining](auto span) -> int64_t {
      if (remaining > span.length()) { remaining -= span.length(); return 0; }
      return remaining;
    },
    [&exact](auto span) {
      exact = span.length() == 8 && !memcmp(span.data(), "headtail", 8);
    });
  ZuCHECK(consumed == 8 && exact, "service copy must preserve exact byte order");
  cxn->closeRx();
}

static void testServiceWindow()
{
  ZuTestScope(testServiceWindow);
  AdmissionApp app;
  app.allow = false;
  ZmRef<AdmissionCxn> cxn = new AdmissionCxn{&app};
  auto part = bytes(app, "data");
  bool accepted = true;
  for (unsigned i = 0; i < 4; ++i)
    accepted &= cxn->processFrame(dataFrame(part, i<<2), part) == 0;
  ZuCHECK(accepted && cxn->rxBytes() == 16 && part->refCount() == 1 &&
      cxn->rxCreditUsed() == 16,
    "application backpressure must retain only bounded heap bytes during contiguous service");
  ZuCHECK(cxn->processFrame(dataFrame(part, 16), part) < 0 && cxn->rxBytes() == 16,
    "service must not bypass existing receive byte credit");
  cxn->closeRx();
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testRefusedData);
  ZuTestCall(testGapService);
  ZuTestCall(testServiceWindow);
}
