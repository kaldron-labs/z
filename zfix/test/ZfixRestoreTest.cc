//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmRef.hh>
#include <zlib/ZfixLinkTx.hh>
#include <zlib/ZfixRx.hh>

using namespace ZuTestUtil;
using namespace Zfix;

static SessionConfig config()
{
  SessionConfig c;
  c.header.version = Version::V4_4; c.header.local = "LOCAL"; c.header.peer = "PEER";
  return c;
}
struct Receiver : Rx<Receiver> {
  explicit Receiver(SessionConfig c = config(), uint64_t epoch = 1) :
    Rx<Receiver>{ZuMv(c), {}, epoch} { }

  void eventRx(SessionEvent, ZmRef<Msg>) { }
  void rejectRx(ZmRef<Msg>, unsigned, int) { }
  void resendRx(uint64_t, uint64_t, ZmRef<Msg>) { }
  void sessionRx(ZmRef<Msg>) { }
  void rxPost(uint64_t) { }
  void gapArm(uint64_t) { }
  void gapCancel() { }

  ZuTime nowRx() { return ZuTime{100}; }
};

struct Sender : Tx<Sender> {
  explicit Sender(SessionConfig c = config(), uint64_t epoch = 1) :
    Tx<Sender>{ZuMv(c), {}, epoch} { }

  void txPost(unsigned, uint64_t) { }
  unsigned loadTx(TxBody::Ref, TxContext, uint64_t) { return TxLoad::Accepted; }
  bool unloadTx(TxBody::Ref, TxContext, uint64_t) { return true; }
  bool frameTx(TxBody::Ref, TxContext, ZmRef<ZiIOBuf>, ZuDateTime) { return false; }
  TxReload historyTx(TxContext, uint64_t, uint64_t) { return {}; }
  void historyArchive(uint64_t, uint64_t, uint64_t) { }
  ZuDateTime utcTx() { return {}; }
  void errorTx(TxContext, Fmt::Error) { }
};

static void ownerRestore()
{
  ZuTestScope(ownerRestore);
  Receiver rx; Sender tx;
  ZuCheck(rx.restore_(1, RxProgress{14, 8, 9, 12}));
  ZuCheck(tx.restore_(1, TxProgress{9, 9, 3, 2, 7}));
  auto r = rx.progress_(); auto t = tx.progress_();
  ZuCheck(r.received == 14 && r.processed == 8 && r.gapBegin == 9 && r.gapEnd == 12);
  ZuCheck(t.loaded == 9 && t.sent == 9 && t.acknowledged == 3 &&
    t.replayBegin == 2 && t.replayEnd == 7);
  // Clear recovery directly on the chosen owner, preserving its cursors.
  r.gapBegin = r.gapEnd = t.replayBegin = t.replayEnd = 0;
  ZuCheck(rx.restore_(1, r) && tx.restore_(1, t));
  ZuCheck(!rx.progress_().gapBegin && !tx.progress_().replayBegin);
  r.processed = 15; t.acknowledged = 10;
  ZuCheck(!rx.restore_(1, r) && rx.progress_().processed == 8);
  ZuCheck(!tx.restore_(1, t) && tx.progress_().acknowledged == 3);
}

static void ranges()
{
  ZuTestScope(ranges);
  auto c = config(); c.sequenceLimit = 2048;
  Receiver rx{c}; Sender tx{c};
  ZuCheck(rx.restore_(1, RxProgress{2049, 7, 7, 10}));
  ZuCheck(!rx.restore_(1, RxProgress{2050, 7, 7, 10}));
  ZuCheck(!rx.restore_(1, RxProgress{12, 0}));
  ZuCheck(!rx.restore_(1, RxProgress{12, 7, 0, 10}));
  ZuCheck(!rx.restore_(1, RxProgress{12, 7, 8, 7}));
  ZuCheck(!rx.restore_(1, RxProgress{12, 7, 6, 10}));
  ZuCheck(!rx.restore_(1, RxProgress{12, 7, 7, 13}));
  ZuCheck(rx.progress_().received == 2049 && rx.progress_().processed == 7);
  ZuCheck(tx.restore_(1, TxProgress{2049, 8, 4, 5, 2049}));
  ZuCheck(!tx.restore_(1, TxProgress{2050, 8, 4}));
  ZuCheck(!tx.restore_(1, TxProgress{10, 8, 0}));
  ZuCheck(!tx.restore_(1, TxProgress{10, 8, 9}));
  ZuCheck(!tx.restore_(1, TxProgress{10, 11, 4}));
  ZuCheck(!tx.restore_(1, TxProgress{10, 8, 4, 0, 10}));
  ZuCheck(!tx.restore_(1, TxProgress{10, 8, 4, 8, 7}));
  ZuCheck(!tx.restore_(1, TxProgress{10, 8, 4, 5, 11}));
  ZuCheck(tx.progress_().loaded == 2049 && tx.progress_().acknowledged == 4);
}

static void lifecycle()
{
  ZuTestScope(lifecycle);
  Receiver rx; Sender tx;
  ZuCheck(!rx.restore_(0, RxProgress{}) && !tx.restore_(0, TxProgress{}));
  ZuCheck(!rx.restore_(2, RxProgress{}) && !tx.restore_(2, TxProgress{}));
  ZuCheck(rx.open_(ZuTime{100}).connection == 1 && tx.attach_(1));
  ZuCheck(!rx.restore_(1, RxProgress{}) && !tx.restore_(1, TxProgress{}));
  rx.lost_(1); rx.closed_(1); tx.detach_(1);
  ZuCheck(!rx.restore_(1, RxProgress{}) && !tx.restore_(1, TxProgress{}));
}

static void wireChange()
{
  ZuTestScope(wireChange);
  RxProgress r; TxProgress t;
  auto c = config(); c.header.version = Version::V4_2;
  {
    Receiver rx{c, 17}; Sender tx{c, 17};
    ZuCheck(rx.restore_(17, RxProgress{14, 8, 9, 12}));
    ZuCheck(tx.restore_(17, TxProgress{9, 9, 3, 2, 7}));
    r = rx.progress_(); t = tx.progress_();
  }
  c.mode = Transport::FIXT11; c.appVerID = AppVersion::V5_0_SP2;
  c.header.version = Version::VT1_1;
  c.header.local = "NEW-LOCAL"; c.header.peer = "NEW-PEER";
  c.header.localSub = "NEW-LS"; c.header.peerSub = "NEW-PS";
  c.header.localLocation = "NEW-LL"; c.header.peerLocation = "NEW-PL";
  ZuCheck(c.valid());
  Receiver rx{c, 17}; Sender tx{c, 17};
  ZuCheck(rx.restore_(17, r) && tx.restore_(17, t));
  r = rx.progress_(); t = tx.progress_();
  ZuCheck(r.received == 14 && r.processed == 8 && r.gapBegin == 9 && r.gapEnd == 12);
  ZuCheck(t.loaded == 9 && t.sent == 9 && t.acknowledged == 3 &&
    t.replayBegin == 2 && t.replayEnd == 7);
}

static void stagedUnsent()
{
  ZuTestScope(stagedUnsent);
  Sender tx;
  ZuCheck(tx.restore_(1, TxProgress{3, 1, 1}));
  // Restored sequence assignments do not recreate another process's allocations.
  ZuCheck(!tx.queuedBytes_() && !tx.retained_() && tx.next_() == 3 &&
    tx.published_() == 1 && tx.acknowledged_() == 1);
}

int main(int argc, char **argv)
{
  parse(argc, argv); ZuTestMain();
  ZuTestCall(ownerRestore);
  ZuTestCall(ranges);
  ZuTestCall(lifecycle);
  ZuTestCall(wireChange);
  ZuTestCall(stagedUnsent);
  return 0;
}
