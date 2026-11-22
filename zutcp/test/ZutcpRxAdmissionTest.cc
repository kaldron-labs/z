//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmHeap.hh>
#include <zlib/ZmRef.hh>

#include <zlib/Zutcp.hh>

#include "ZutcpTestAlloc.hh"
#include "ZutcpTestInstance.hh"

using namespace ZuTestUtil;
using namespace Zutcp;

struct AdmissionApp : public ZmNHeap<AdmissionApp>, public ZutcpTest::Context<> {
friend inline ZuStringT<"Zutcp.RxAdmissionTest.AdmissionApp"> ZmNHeapID(AdmissionApp *);
  using ZmNHeap<AdmissionApp>::operator new;
  using ZmNHeap<AdmissionApp>::operator delete;
  unsigned demands = 0;

  AdmissionApp() { ZmAssert_(init(ZutcpTest::Params{&multiplex}.maxData(16))); }
  bool rxInvoked() const { return true; }
  bool txInvoked() const { return true; }
  template <typename L> void rxRun(L l) { l(); }
  template <typename L> void txRun(L l) { l(); }
  template <typename L> void txInvoke(L l) { l(); }
  template <typename O, typename L> void txInvoke(O *, L l) { l(); }
  void retainRx(ZiIOBuf *packet, Zi::IOQueue &retained) {
    ++demands;
    ZutcpTest::Context<>::retainRx(packet, retained);
  }
};

struct AdmissionCxn : ZmNHeap<AdmissionCxn>,
  Cxn_<AdmissionApp, AdmissionCxn, TxBufAlloc<>> {
friend inline ZuStringT<"Zutcp.Test.RxAdmission"> ZmNHeapID(AdmissionCxn *);
  using Base = Zutcp::Cxn_<AdmissionApp, AdmissionCxn, Zutcp::TxBufAlloc<>>;
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

static void testDuplicateRanges()
{
  ZuTestScope(testDuplicateRanges);
  ZmRef<AdmissionApp> appRef = new AdmissionApp;
  auto &app = *appRef;
  ZmRef<AdmissionCxn> cxn = new AdmissionCxn{&app};
  auto source = bytes(app, "tail");
  ZuCHECK(cxn->processFrame(dataFrame(source, 16), source) < 0 &&
      !cxn->rxPending() && !cxn->rxCreditUsed() &&
      !cxn->rxAdmitted() && cxn->rxError() == DataError::None,
    "out-of-window input commits no bytes, final size or protocol error");
  ZuCHECK(cxn->processFrame(dataFrame(source, 4), source) == 0 &&
      cxn->rxPending() == 1 && cxn->rxAdmitted() == 4,
    "an eligible range remains admissible after rejecting out-of-window input");
  unsigned demands = app.demands;
  unsigned refs = source->refCount();
  ZuCHECK(cxn->processFrame(dataFrame(source, 4), source) == 0 &&
      app.demands == demands && source->refCount() == refs,
    "duplicate bytes demand no additional frame or retention reference");
  cxn->closeRx();
  ZuCHECK(source->refCount() == 1, "teardown releases reassembly and retention references");
}

static void testGapService()
{
  ZuTestScope(testGapService);
  ZmRef<AdmissionApp> appRef = new AdmissionApp;
  auto &app = *appRef;
  ZmRef<AdmissionCxn> cxn = new AdmissionCxn{&app};
  auto tail = bytes(app, "tail");
  auto head = bytes(app, "head");
  ZuCHECK(cxn->processFrame(dataFrame(tail, 4), tail) == 0 && cxn->rxPending() == 1,
    "service fixture must retain an out-of-order range");
  unsigned refs = head->refCount();
  ZuCHECK(cxn->processFrame(dataFrame(head, 0), head) == 0 && !cxn->rxPending() &&
      cxn->rxBytes() == 8 && head->refCount() == refs + 2,
    "gap service keeps reader and retention references to the original ingress backing");
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
  ZuCHECK(consumed == 8 && exact, "packet-backed gap service preserves exact byte order");
  cxn->closeRx();
}

static void testServiceWindow()
{
  ZuTestScope(testServiceWindow);
  ZmRef<AdmissionApp> appRef = new AdmissionApp;
  auto &app = *appRef;
  ZmRef<AdmissionCxn> cxn = new AdmissionCxn{&app};
  auto part = bytes(app, "data");
  bool accepted = true;
  for (unsigned i = 0; i < 4; ++i)
    accepted &= cxn->processFrame(dataFrame(part, i<<2), part) == 0;
  ZuCHECK(accepted && cxn->rxBytes() == 16 && part->refCount() == 6 &&
      cxn->rxCreditUsed() == 16,
    "held input shares its original backing across four slices and one retention reference");
  ZuCHECK(cxn->processFrame(dataFrame(part, 16), part) < 0 && cxn->rxBytes() == 16,
    "service must not bypass existing receive byte credit");
  cxn->closeRx();
  ZuCHECK(part->refCount() == 1, "teardown releases every held slice and retention reference");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testDuplicateRanges);
  ZuTestCall(testGapService);
  ZuTestCall(testServiceWindow);
}
