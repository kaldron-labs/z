//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmRef.hh>
#include <zlib/ZfixLinkTx.hh>
#include <zlib/ZfixRx.hh>
#include <zlib/ZfixCheckpoint.hh>

using namespace ZuTestUtil;
using namespace Zfix;

static SessionConfig config()
{
  SessionConfig c;
  c.id = "PRIMARY";
  c.header.version = Version::V4_4; c.header.local = "LOCAL"; c.header.peer = "PEER";
  return c;
}
struct Receiver : Rx<Receiver> {
  explicit Receiver(SessionConfig c = config(), uint64_t epoch = 1) :
    Rx<Receiver>{ZuMv(c), {}, epoch} { }

  void eventRx_(SessionEvent, ZmRef<Msg>) { }
  void rejectRx_(ZmRef<Msg>, unsigned, int) { }
  void resendRx_(uint64_t, uint64_t, ZmRef<Msg>) { }
  void sessionRx_(ZmRef<Msg>) { }
  void rxPost_(uint64_t) { }
  void gapArm_(uint64_t) { }
  void gapCancel_() { }

  ZuTime nowRx_() { return ZuTime{100}; }
};

struct Sender : Tx<Sender> {
  explicit Sender(SessionConfig c = config(), uint64_t epoch = 1) :
    Tx<Sender>{ZuMv(c), {}, epoch} { }

  void txPost_(unsigned, uint64_t) { }
  unsigned loadTx_(TxBody::Ref, TxContext, uint64_t) { return TxLoad::Accepted; }
  bool unloadTx_(TxBody::Ref, TxContext, uint64_t) { return true; }
  bool frameTx_(TxBody::Ref, TxContext, ZmRef<ZiIOBuf>, ZuDateTime) { return false; }
  TxReload historyTx_(TxContext, uint64_t, uint64_t) { return {}; }
  void historyArchive_(uint64_t, uint64_t, uint64_t) { }
  ZuDateTime utcTx_() { return {}; }
  void errorTx_(TxContext, Fmt::Error) { }
};

static Checkpoint record()
{
  Checkpoint s;
  s.id = config().id;
  s.epoch = 1;
  return s;
}

static void merging()
{
  ZuTestScope(merging);
  auto s = record();
  RxProgress rx{12, 7, 7, 10}, got;
  s.fields = StateField::RxProcessed();
  s.rx = {0, 6, 0, 99}; // other values are deliberately invalid and omitted
  ZuCheck(restoreRx(s, rx, 100, got));
  ZuCheck(got.received == 12 && got.processed == 6 && got.gapBegin == 7 && got.gapEnd == 10);
  s.fields |= StateField::RxGap(); s.rx.processed = 8; s.rx.gapBegin = s.rx.gapEnd = 0;
  ZuCheck(restoreRx(s, rx, 100,
    got) && got.received == 12 && got.processed == 8 && !got.gapBegin);
  s.rx.processed = 0;
  ZuCheck(!restoreRx(s, rx, 100, got));
  s.rx.processed = 8; s.rx.gapEnd = 10;
  ZuCheck(!restoreRx(s, rx, 100, got));
  s.fields = StateField::RxReceived(); s.rx.received = 101;
  ZuCheck(restoreRx(s, rx, 100, got)); // exclusive limit + 1 is valid
  s.rx.received = 102;
  ZuCheck(!restoreRx(s, rx, 100, got));

  TxProgress tx{10, 8, 4, 5, 8}, output;
  s.fields = StateField::TxAckd(); s.tx = {0, 0, 7, 0, 99};
  ZuCheck(restoreTx(s, tx, 100, output));
  ZuCheck(output.loaded == 10 && output.sent == 8 && output.acknowledged == 7 &&
    output.replayBegin == 5 && output.replayEnd == 8);
  s.fields |= StateField::TxReplay(); s.tx.replayBegin = s.tx.replayEnd = 0;
  ZuCheck(restoreTx(s, tx, 100, output) && !output.replayBegin);
  s.tx.replayBegin = 5; s.tx.replayEnd = 10;
  ZuCheck(restoreTx(s, tx, 100, output) && output.replayEnd == output.loaded &&
    output.replayEnd > output.sent);
  s.tx.replayEnd = 11;
  ZuCheck(!restoreTx(s, tx, 100, output));
  s.tx.replayBegin = s.tx.replayEnd = 0;
  ZuCheck(restoreTx(s, tx, 100, output) && output.sent < output.loaded);
  s.fields |= StateField::TxSent(); s.tx.sent = 10;
  ZuCheck(restoreTx(s, tx, 100,
    output) && output.sent == output.loaded);
  s.tx.acknowledged = 11;
  ZuCheck(!restoreTx(s, tx, 100, output));
}

static void ownerRestore()
{
  ZuTestScope(ownerRestore);
  Receiver rx;
  Sender tx;
  auto state = record();
  state.fields = StateMask::Rx | StateMask::Tx;
  state.rx = {14, 8, 9, 12};
  state.tx = {9, 9, 3, 2, 7};
  ZuCheck(rx.restore_(state) && tx.restore_(state));
  Checkpoint snapshot;
  ZuCheck(rx.snapshot_(snapshot) && tx.snapshot_(snapshot));
  ZuCheck(snapshot.fields == StateMask::All && snapshot.epoch == 1
    && snapshot.id == "PRIMARY");
  ZuCheck(snapshot.rx.received == 14 && snapshot.rx.processed == 8 &&
    snapshot.rx.gapBegin == 9 && snapshot.rx.gapEnd == 12);
  ZuCheck(snapshot.tx.loaded == 9 && snapshot.tx.sent == 9 && snapshot.tx.acknowledged == 3 &&
    snapshot.tx.replayBegin == 2 && snapshot.tx.replayEnd == 7);
  state.fields = StateField::RxGap() | StateField::TxReplay();
  state.rx.gapBegin = state.rx.gapEnd = state.tx.replayBegin = state.tx.replayEnd = 0;
  state.rx.processed = state.tx.loaded = 0;
  ZuCheck(rx.restore_(state) && tx.restore_(state));
  Checkpoint next;
  ZuCheck(rx.snapshot_(next) && tx.snapshot_(next));
  ZuCheck(next.rx.processed == 8 && next.rx.received == 14 && !next.rx.gapBegin &&
    next.tx.loaded == 9 && next.tx.acknowledged == 3 && !next.tx.replayBegin);
  state.fields = StateField::TxAckd(); state.tx.acknowledged = 10;
  ZuCheck(!tx.restore_(state) && tx.progress_().acknowledged == 3);
  state.fields = StateField::RxProcessed(); state.rx.processed = 15;
  ZuCheck(!rx.restore_(state) && rx.progress_().processed == 8);
}

static void stamp()
{
  ZuTestScope(stamp);
  Receiver rx;
  Sender tx;
  auto state = record();
  state.epoch = 2;
  ZuCheck(!rx.restore_(state) && !tx.restore_(state));
  state.epoch = 1; state.id = "OTHER";
  ZuCheck(!rx.restore_(state) && !tx.restore_(state) && !rx.snapshot_(state));
  state = record(); state.id = "";
  ZuCheck(!rx.restore_(state) && !tx.restore_(state));
  state = record(); state.fields = 1U << 31;
  ZuCheck(!rx.restore_(state) && !tx.restore_(state));
  Checkpoint incomplete;
  incomplete.fields = StateField::TxSent();
  ZuCheck(!rx.snapshot_(incomplete) && !incomplete.epoch && !incomplete.id.length());
  incomplete = {}; incomplete.id = "OTHER";
  ZuCheck(!rx.snapshot_(incomplete));
  state = record();
  ZuCheck(rx.open_(ZuTime{100}).connection == 1 && tx.attach_(1));
  ZuCheck(!rx.restore_(state) && !tx.restore_(state));
  rx.lost_(1); rx.closed_(1); tx.detach_(1);
  ZuCheck(!rx.restore_(state) && !tx.restore_(state)); // cannot roll an active epoch back
}
static void wireChange()
{
  ZuTestScope(wireChange);
  auto c = config(); c.header.version = Version::V4_2;
  Checkpoint saved;
  {
    Receiver rx{c, 17}; Sender tx{c, 17};
    auto state = record(); state.epoch = 17; state.fields = StateMask::All;
    state.rx = {14, 8, 9, 12}; state.tx = {9, 9, 3, 2, 7};
    ZuCheck(rx.restore_(state) && tx.restore_(state));
    ZuCheck(rx.snapshot_(saved) && tx.snapshot_(saved));
  }
  c.mode = Transport::FIXT11; c.appVerID = AppVersion::V5_0_SP2;
  c.header.version = Version::VT1_1;
  c.header.local = "NEW-LOCAL"; c.header.peer = "NEW-PEER";
  c.header.localSub = "NEW-LS"; c.header.peerSub = "NEW-PS";
  c.header.localLocation = "NEW-LL"; c.header.peerLocation = "NEW-PL";
  ZuCheck(c.valid());
  Receiver rx{c, 17}; Sender tx{c, 17};
  ZuCheck(rx.restore_(saved) && tx.restore_(saved));
  Checkpoint resumed;
  ZuCheck(rx.snapshot_(resumed) && tx.snapshot_(resumed));
  ZuCheck(resumed.id == "PRIMARY" && resumed.id == saved.id && resumed.epoch == 17 &&
    resumed.fields == StateMask::All);
  ZuCheck(resumed.rx.received == 14 && resumed.rx.processed == 8 &&
    resumed.rx.gapBegin == 9 && resumed.rx.gapEnd == 12);
  ZuCheck(resumed.tx.loaded == 9 && resumed.tx.sent == 9 && resumed.tx.acknowledged == 3 &&
    resumed.tx.replayBegin == 2 && resumed.tx.replayEnd == 7);
  saved.id = "OTHER";
  ZuCheck(!rx.restore_(saved) && !tx.restore_(saved));
}

static void stagedUnsent()
{
  ZuTestScope(stagedUnsent);
  Sender tx;
  auto state = record();
  state.fields = StateMask::Tx;
  state.tx = {3, 1, 1, 0, 0};
  ZuCheck(tx.restore_(state));
  state.fields = StateField::TxReplay();
  state.tx = {};
  ZuCheck(tx.restore_(state));
  auto restored = tx.progress_();
  ZuCheck(restored.loaded == 3 && restored.sent == 1 && restored.acknowledged == 1 &&
    !restored.replayBegin);
  // Restored sequence assignments do not recreate another process's allocations.
  ZuCheck(!tx.queuedBytes_() && !tx.retained_() && tx.next_() == 3 &&
    tx.published_() == 1 && tx.acknowledged_() == 1);
}

int main(int argc, char **argv)
{
  parse(argc, argv); ZuTestMain();
  ZuTestCall(merging);
  ZuTestCall(ownerRestore);
  ZuTestCall(stamp);
  ZuTestCall(wireChange);
  ZuTestCall(stagedUnsent);
  return 0;
}
