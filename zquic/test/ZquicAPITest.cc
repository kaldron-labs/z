//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmBlock.hh>
#include <zlib/ZmList.hh>
#include <zlib/ZfJSON.hh>
#include <zlib/ZtString.hh>
#include <zlib/ZiFile.hh>
#include <zlib/Zquic.hh>

#include <zlib/ZtlsEngineOpenSSL.hh>

#include "ZiTestResidue.hh"

using namespace ZuTestUtil;

#define ZQUIC_CHECK_RT(x, ...) ZuCheckRT(x, log_(__VA_ARGS__))


#ifdef Zquic_DEBUG
static Zi::Path g_residue;

static Zi::Path testPath(ZuCSpan name)
{
  return ZiFile::append(g_residue, name);
}

static ZtString<> readFile(const Zi::Path &path)
{
  ZtString<> data;
  ZiFile file{path, ZiFile::ReadOnly | ZiFile::GC};
  if (!file) return data;
  auto size = file.size();
  if (size <= 0 || size > (1<<20)) return data;
  data.length(unsigned(size));
  int n = file.read(data.data(), data.length());
  if (n <= 0)
    data.length(0);
  else
    data.length(unsigned(n));
  return data;
}

static unsigned parseJSONSeq(ZuCSpan data)
{
  unsigned n = 0;
  unsigned i = 0, l = data.length();
  while (i < l) {
    ZuCSpan rest{data.data() + i, l - i};
    if (!rest.match<"\x1e">()) return 0;
    ++i;
    unsigned start = i;
    while (i < l && data[i] != '\n') ++i;
    if (i >= l) return 0;
    ZtString<> json;
    json << ZuCSpan{data.data() + start, i - start};
    auto scan = ZfJSON::scan(json);
    if (scan.p<0>() != int(json.length())) return 0;
    ++n;
    ++i;
  }
  return n;
}

static void closeQLog(ZquicLogger::Trace &trace)
{
  ZmBlock<>{}([&trace](auto wake) {
    ZquicLogger::close(trace, ZuMv(wake));
  });
}
#endif

struct TestLink;
struct TestStream :
    public Zquic::Stream<TestLink, TestStream> {
  using Base = Zquic::Stream<TestLink, TestStream>;
  using Base::Base;

  unsigned txCompleteCount = 0;
  ZmFn<> txCompleteFn;

  int process(Zquic::RxStream &) { return 0; }
  void txComplete(bool) {
    ++txCompleteCount;
    if (txCompleteFn) txCompleteFn();
  }
};

struct HubApp : public Zquic::Hub<HubApp> {
  using Shard = int8_t;
  enum { Rx, Tx };
  using PendingQueue =
    ZmList<ZmFn<>, ZmListHeapID<"Zquic.APITest.Pending">>;

  PendingQueue rxPendingQ;
  PendingQueue txPendingQ;
  Zquic::MigrationMode::T migration = Zquic::MigrationMode::Passive;
  Shard shard = Rx;
  bool deferMode = false;

  void activeMigration(bool v) {
    migration = v ?
      Zquic::MigrationMode::Active : Zquic::MigrationMode::Passive;
  }
  void migrationMode(Zquic::MigrationMode::T v) {
    migration = v;
  }
  Zquic::MigrationMode::T migrationMode() const {
    return migration;
  }
  bool rxInvoked() const { return !deferMode || shard == Rx; }
  bool txInvoked() const { return !deferMode || shard == Tx; }
  template <typename L> void rxRun(L l) {
    if (!deferMode) { l(); return; }
    rxPendingQ.push(ZmFn<>::Lambda::fn(
      [l = ZuMv(l)]() mutable { l(); }));
  }
  template <typename L> void rxInvoke(L l) { rxRun(ZuMv(l)); }
  template <typename L> void txRun(L l) {
    if (!deferMode) { l(); return; }
    txPendingQ.push(ZmFn<>::Lambda::fn(
      [l = ZuMv(l)]() mutable { l(); }));
  }
  template <typename L> void txInvoke(L l) { txRun(ZuMv(l)); }
  template <typename O, typename L> void txInvoke(O *, L l) {
    txRun(ZuMv(l));
  }

  void defer() {
    deferMode = true;
    shard = Rx;
  }
  void enterRx() { shard = Rx; }
  void enterTx() { shard = Tx; }
  bool rxPending() const { return rxPendingQ.count_(); }
  bool txPending() const { return txPendingQ.count_(); }
  void runRx() {
    assert(rxPendingQ.count_());
    shard = Rx;
    auto fn = rxPendingQ.shiftVal();
    fn();
  }
  void runTx() {
    assert(txPendingQ.count_());
    shard = Tx;
    auto fn = txPendingQ.shiftVal();
    fn();
  }
};
struct ClientApp : public Zquic::Client<ClientApp> { };
struct ServerAppLink;
struct ServerApp : public Zquic::Server<ServerApp, ServerAppLink> { };
struct ServerAppStream :
    public Zquic::SrvStream<
      ServerAppLink, ServerAppStream> {
  using Base = Zquic::SrvStream<
    ServerAppLink, ServerAppStream>;
  using Base::Base;

  int process(Zquic::RxStream &) { return 0; }
};
struct ServerAppLink :
  public ZmNHeap<ServerAppLink>,
    public Zquic::SrvLink<
      ServerApp, ServerAppLink, ServerAppStream> {
  friend inline ZuStringT<"Zquic.API.SrvLink"> ZmNHeapID(ServerAppLink *);
  using ZmNHeap<ServerAppLink>::operator new;
  using ZmNHeap<ServerAppLink>::operator delete;

  using Base = Zquic::SrvLink<
    ServerApp, ServerAppLink, ServerAppStream>;

  ServerAppLink(ServerApp *app) : Base{app} { }

  void connected(Zquic::Connected) { }
  void streamed(ZmRef<ServerAppStream>) { }
};

struct ClientShapeApp : public Zquic::Client<ClientShapeApp> { };
struct ClientShapeLink;
struct ClientShapeStream :
    public Zquic::CliStream<
      ClientShapeLink, ClientShapeStream> {
  using Base = Zquic::CliStream<
    ClientShapeLink, ClientShapeStream>;
  using Base::Base;

  int process(Zquic::RxStream &) { return 0; }
};

struct ClientShapeLink :
    public ZmNHeap<ClientShapeLink>,
    public Zquic::CliLink<
      ClientShapeApp, ClientShapeLink, ClientShapeStream> {
friend inline ZuStringT<"Zquic.APITest.CliLink"> ZmNHeapID(ClientShapeLink *);
  using ZmNHeap<ClientShapeLink>::operator new;
  using ZmNHeap<ClientShapeLink>::operator delete;

  using Base = Zquic::CliLink<
    ClientShapeApp, ClientShapeLink, ClientShapeStream>;

  ZmAtomic<unsigned> failures = 0;
  ZmAtomic<unsigned> disconnects = 0;

  ClientShapeLink(ClientShapeApp *app) : Base{app} { }

  void connected(Zquic::Connected) { }
  void streamed(ZmRef<ClientShapeStream>) { }
  void connectFailed(bool) { ++failures; }
  void disconnected(bool) { ++disconnects; }
  Zquic::LinkDiag diag() const { return Base::diag_(); }
};

struct ServerShapeLink;
struct ServerShapeApp :
    public Zquic::Server<ServerShapeApp, ServerShapeLink> { };
struct ServerShapeStream :
    public Zquic::SrvStream<
      ServerShapeLink, ServerShapeStream> {
  using Base = Zquic::SrvStream<
    ServerShapeLink, ServerShapeStream>;
  using Base::Base;

  int process(Zquic::RxStream &) { return 0; }
};

struct ServerShapeLink :
    public ZmNHeap<ServerShapeLink>,
    public Zquic::SrvLink<
      ServerShapeApp, ServerShapeLink, ServerShapeStream> {
friend inline ZuStringT<"Zquic.APITest.SrvLink"> ZmNHeapID(ServerShapeLink *);
  using ZmNHeap<ServerShapeLink>::operator new;
  using ZmNHeap<ServerShapeLink>::operator delete;

  using Base = Zquic::SrvLink<
    ServerShapeApp, ServerShapeLink, ServerShapeStream>;

  ZmFn<void()> startedFn;
  ZmFn<void()> promotedFn;

  ServerShapeLink(ServerShapeApp *app) : Base{app} { }

  void connected(Zquic::Connected) { }
  void streamed(ZmRef<ServerShapeStream>) { }
  void migrationStarted(const Zquic::MigrationResult &) {
    if (startedFn) startedFn();
  }
  void migrationPromoted(const Zquic::MigrationResult &) {
    if (promotedFn) promotedFn();
  }
  void migrationFailed(const Zquic::MigrationResult &) {
    if (promotedFn) promotedFn();
  }
  void onStarted(ZmFn<void()> fn) { startedFn = ZuMv(fn); }
  void onPromoted(ZmFn<void()> fn) { promotedFn = ZuMv(fn); }
  void initSrvPath(ZiSockAddr local, ZiSockAddr remote) {
    Base::initServerPathTx(ZuMv(local), ZuMv(remote));
  }
#ifdef Zquic_DEBUG
  bool pathBindingValid(const ZiSockAddr &local, unsigned, uint64_t) const {
    // The shape fixture uses a synthetic local binding without a listener.
    return local == Base::activePathLocal_();
  }
  void observePath(ZiSockAddr local, ZiSockAddr remote) {
    Base::observePathRxTx(ZuMv(local), ZuMv(remote));
  }
  bool promoteObservedPath() {
    ZuBSpan challenge = Base::validatingChallenge_();
    if (challenge.length() != Zquic::PathChallenge::Length) return false;
    uint8_t response[Zquic::PathChallenge::Length];
    memcpy(response, challenge.data(), sizeof(response));
    return Base::onPathResponse(ZuBSpan{response, sizeof(response)});
  }
#endif
};

struct TestLink :
    public ZmNHeap<TestLink>,
    public Zquic::Link<
      HubApp, TestLink,
      TestStream> {
friend inline ZuStringT<"Zquic.APITest.Link"> ZmNHeapID(TestLink *);
  using ZmNHeap<TestLink>::operator new;
  using ZmNHeap<TestLink>::operator delete;

  using Base = Zquic::Link<
    HubApp, TestLink,
    TestStream>;
  ZmRef<TestStream> lastOpened;
  ZmRef<TestStream> lastDataStream;
  ZmRef<TestStream> lastResetStream;
  ZmRef<TestStream> lastStopStream;
  ZiSockAddr lastPathLocal;
  ZiSockAddr lastPathRemote;
  ZiSockAddr lastMigrationLocal;
  ZiSockAddr lastMigrationRemote;
  Zquic::PathInfo lastPath;
  Zquic::MigrationResult lastMigration;
  ZtString<> migrationCallbackOrder;
  uint64_t retiredSeq = 0;
  uint64_t lastDataOffset = 0;
  uint64_t lastResetAppError = 0;
  uint64_t lastResetFinalSize = 0;
  uint64_t lastResetSentID = 0;
  uint64_t lastResetSentAppError = 0;
  uint64_t lastResetSentFinalSize = 0;
  uint64_t lastStopAppError = 0;
  uint64_t lastStopSentID = 0;
  uint64_t lastStopSentAppError = 0;
  uint64_t lastFlowStreamID = 0;
  uint64_t lastFlowMaximum = 0;
  uint64_t lastCloseError = 0;
  Zquic::CxnID retiredCID;
  Zquic::FrameType::T lastFlowType = Zquic::FrameType::Unknown;
  Zquic::FrameType::T lastCloseType = Zquic::FrameType::Unknown;
  Zquic::StreamType::T lastFlowStreamType = Zquic::StreamType::Duplex;
  unsigned lastDataLength = 0;
  unsigned lastPathMaxUDP = 0;
  unsigned retiredCount = 0;
  unsigned streamOpenCount = 0;
  unsigned localStreamOpens = 0;
  unsigned peerStreamOpens = 0;
  unsigned streamDataCount = 0;
  unsigned resetReceivedCount = 0;
  unsigned resetSentCount = 0;
  unsigned stopReceivedCount = 0;
  unsigned stopSentCount = 0;
  unsigned flowBlockedCount = 0;
  unsigned transportCloseCount = 0;
  unsigned statelessResetCount = 0;
  unsigned migrationStartedCount = 0;
  unsigned migrationPromotedCount = 0;
  unsigned pathUpdateCount = 0;
  unsigned migrationFailureCount = 0;
  unsigned txFlushQueued = 0;
  unsigned txRecoveryCalls = 0;
  bool lastDataFin = false;
  bool lastPathValidated = false;

  TestLink(HubApp *app, bool isServer = false) : Base{app, isServer} {
    Base::configLocalParams(app);
    Base::TxCryptoSnapshot snapshot;
    Base::snapshotTxCrypto(snapshot);
    Base::installTxCrypto(snapshot);
  }

  enum { RecBatch = Base::RecoveryScanBatch };
  using Base::congestionAllowance_;
  using Base::noteAck;
  using Base::appendPendingAck;
  using Base::ackSentTx;
  using Base::applyAckOfAckTx;
  using Base::recordPathRx;
  using Base::observePathRx_;
  using Base::refreshPathRxTx;
  using Base::notePeerPkt;
  using Base::processAckFrame;
  using Base::drainPeerAcksTx;
  using Base::awaitPeerAckTx;
  using Base::publishStreamTx;
  using Base::postFlowUpdate;
  using Base::resetLink;
  using Base::AckSnapshot;
  using Base::updateAckTx;
  using Base::ackFrameValidTx;
  using Base::pendingAck;
  using Base::txPackets;
  using Base::deferTxFlush;
  using Base::timersActive;
  using Base::resetPath;

  template <typename Send>
  bool shortCommit(Send send, bool allocate = true, bool resetOnAlloc = false) {
    Zquic::PktBuild build;
    if (!Base::buildPingProbe(build)) return false;
    Base::TxPktRefs refs;
    return Base::sendProtShortPkt(
      Zquic::CxnID{"commit"}, 1, build, ZiSockAddr{}, {}, &refs, true,
      [this, allocate, resetOnAlloc]() {
	if (resetOnAlloc) (void)resetCommit();
	return allocate ? app()->txPktAllocator()->allocBuf(nullptr, 0) : ZmRef<ZiIOBuf>{};
      },
      [this, &send](auto packet, ZiSockAddr addr, Zquic::EcnMark::T ecn) {
	return this->Base::template submitPathPkt<Base::PathTx::Direct>(ZuMv(packet), ZuMv(addr),
	  [&send](auto buf, ZiSockAddr, Zquic::EcnMark::T) { return send(ZuMv(buf)); }, ecn);
      });
  }
  bool resetCommit() {
    Base::resetTx();
    Base::setTxPN(Zquic::PktNumSpace::AppData, 1);
    return Base::recordTxPkt(Zquic::PktNumSpace::AppData, 0, 99,
      Zquic::SentFrameRef{}, true);
  }

  bool ackEstablished() {
    uint8_t bytes[32] = {};
    Zquic::TrafficSecret secret;
    return Zquic::PktProt::deriveSecret(secret,
      &Ztls_::opensslAes128gcmsha256, ZuBSpan{bytes, sizeof(bytes)}) &&
      Base::installAppDataKeys_(secret, secret, Zquic::CxnID{"ack-test"});
  }
  bool appPackets(unsigned count) {
    constexpr auto space = Zquic::PktNumSpace::AppData;
    Base::setTxPN(space, count + 1);
    for (unsigned pn = 1; pn <= count; ++pn)
      if (!Base::recordTxPkt(space, pn, 100,
	  Zquic::SentFrameRef{}, true)) return false;
    return true;
  }
  unsigned appPacketCount() const {
    return Base::txPackets_(Zquic::PktNumSpace::AppData).count();
  }

  bool recoveryPackets(unsigned count, bool ecn = false) {
    if (ecn) Base::setEcnDisabled_(false);
    Base::setTxPN(Zquic::PktNumSpace::Handshake, count + 1);
    for (unsigned pn = 1; pn <= count; ++pn)
      if (!Base::recordTxPkt(Zquic::PktNumSpace::Handshake, pn, 1200,
	  Zquic::SentFrameRef{}, true, false, 0, Zquic::PktNumSpace::N, 0,
	  Zquic::PktType::Handshake,
	  ecn ? Zquic::EcnMark::ECT0 : Zquic::EcnMark::NotECT))
	return false;
    return true;
  }
  void recoveryAck(unsigned largest, unsigned first, bool ecn = false) {
    Base::AckSnapshot ack;
    ack.level = Zquic::PktNumSpace::Handshake;
    ack.ranges.push(Zquic::AckRange{largest, first});
    if (ecn) {
      ack.ecn.ect0 = largest - first;
      ack.ecn.ce = 1;
    }
    Base::processAckFrameTx(ack);
  }
  auto recoveryDiag() {
    // Inspect live owner state, including builds which erase diagnostics.
    struct State {
      uint64_t congestionWindow;
      uint64_t congestionSSThresh;
      uint64_t bytesInFlight;
    };
    return State{
      Base::congestionWindow(), Base::congestionSSThresh(), Base::txBytesInFlight()};
  }
  auto recoveryPathDiag() const { return Base::pathDiag_(); }
  void recoveryStop() { Base::cancelTimers_(); }
  void recoveryReset() { Base::resetTx(); }
  void recoveryDiscard() {
    Base::discardPktNumSpace_1(Zquic::PktNumSpace::Handshake);
  }

  bool longProbe(
    const Zquic::TrafficSecret &secret, unsigned pnLength, bool early,
    ZmRef<ZiIOBuf> &packet) {
    if (!Base::txInstallKeySecret(early ?
	Zquic::PktKeyLevel::ZeroRTT : Zquic::PktKeyLevel::Handshake, secret))
      return false;
    Zquic::PktBuild payload;
    int n = Zquic::FrameCodec::writePing(
      payload.scratch(), payload.scratchAvail());
    if (n <= 0 || !payload.commitScratch(unsigned(n))) return false;
    auto alloc = []() {
      return ZmRef<ZiIOBuf>{([](void *owner) {
      static Zi::IOBufAllocator *allocator =
	([] { static ZiMultiplex mx; return Zquic::pktTxAllocator(&mx); })();
      return allocator->allocBuf(owner, 0);
    })(nullptr)};
    };
    auto send = [&packet](auto buf, ZiSockAddr) {
      packet = ZuMv(buf);
      return true;
    };
    if (early)
      return Base::template sendProtLongPkt<Zquic::PktType::ZeroRTT>(
	Base::CIDSel::Peer, Base::CIDSel::Local, pnLength,
	payload, {}, {}, nullptr, false, alloc, send);
    return Base::template sendProtLongPkt<Zquic::PktType::Handshake>(
      Base::CIDSel::Peer, Base::CIDSel::Local, pnLength,
      payload, {}, {}, nullptr, false, alloc, send);
  }

  const Zquic::TransportParams &localParams() const {
    return Base::localTransportParams();
  }

  void streamed(ZmRef<TestStream>) { }
  void streamOpen(ZmRef<TestStream> stream, bool local) {
    lastOpened = ZuMv(stream);
    local ? ++localStreamOpens : ++peerStreamOpens;
    ++streamOpenCount;
  }
  void streamData(
    ZmRef<TestStream> stream, uint64_t offset, ZuBSpan payload, bool fin) {
    lastDataStream = ZuMv(stream);
    lastDataOffset = offset;
    lastDataLength = payload.length();
    lastDataFin = fin;
    ++streamDataCount;
  }
  void streamResetReceived(
    ZmRef<TestStream> stream, uint64_t appError, uint64_t finalSize) {
    lastResetStream = ZuMv(stream);
    lastResetAppError = appError;
    lastResetFinalSize = finalSize;
    ++resetReceivedCount;
  }
  void streamResetSent(
    uint64_t streamID, uint64_t appError, uint64_t finalSize) {
    lastResetSentID = streamID;
    lastResetSentAppError = appError;
    lastResetSentFinalSize = finalSize;
    ++resetSentCount;
  }
  void streamStopSendingReceived(
    ZmRef<TestStream> stream, uint64_t appError) {
    lastStopStream = ZuMv(stream);
    lastStopAppError = appError;
    ++stopReceivedCount;
  }
  void streamStopSendingSent(uint64_t streamID, uint64_t appError) {
    lastStopSentID = streamID;
    lastStopSentAppError = appError;
    ++stopSentCount;
  }
  void flowBlocked(
    Zquic::FrameType::T type, uint64_t streamID,
    Zquic::StreamType::T streamType, uint64_t maximum) {
    lastFlowType = type;
    lastFlowStreamID = streamID;
    lastFlowStreamType = streamType;
    lastFlowMaximum = maximum;
    ++flowBlockedCount;
  }
  void transportClose(Zquic::FrameType::T type, uint64_t errorCode) {
    lastCloseType = type;
    lastCloseError = errorCode;
    ++transportCloseCount;
  }
  void ptoTimer() { }
  void queueRetransmit() { }
  bool retransmit_(bool = false, unsigned = 2,
    Zquic::PktNumSpace::T = Zquic::PktNumSpace::N) {
    ++txRecoveryCalls; return false;
  }
  void postOrdinaryTx() { Base::queueTxFlush(); }
  void postRecoveryTx() { Base::queueRetransmit(); }
  void resetRuntimeForWork() { Base::resetLink([] { }); }
  void statelessReset() { ++statelessResetCount; }
  void migrationStarted(const Zquic::MigrationResult &result) {
    lastMigration = result;
    migrationCallbackOrder << "S";
    ++migrationStartedCount;
  }
  void pathUpdate(const Zquic::PathInfo &info) {
    lastPath = info;
    lastPathLocal = info.local;
    lastPathRemote = info.remote;
    lastPathValidated = info.validated;
    lastPathMaxUDP = info.activeMaxUDP;
    migrationCallbackOrder << "U";
    ++pathUpdateCount;
  }
  void migrationPromoted(const Zquic::MigrationResult &result) {
    lastMigration = result;
    migrationCallbackOrder << "P";
    ++migrationPromotedCount;
  }
  void migrationFailed(const Zquic::MigrationResult &result) {
    lastMigration = result;
    lastMigrationLocal = result.candidate.local;
    lastMigrationRemote = result.candidate.remote;
    migrationCallbackOrder << "F";
    ++migrationFailureCount;
  }
	  void setPeerResetToken(const Zquic::ResetToken &token) {
	    Base::setPeerResetToken_(token);
	  }
  void observePacket(const ZmRef<Zquic::RxPkt> &pkt,
      unsigned offset, unsigned length, unsigned bytes, uint64_t pn,
      bool nonProbing, bool controls) {
    Base::observePathPacket(pkt, offset, length, bytes, pn, true, nonProbing, controls);
  }
  bool authenticatedPing(
      const Zquic::TrafficSecret &secret, const Zquic::CxnID &cid, uint64_t pn,
      ZiSockAddr local, ZiSockAddr remote) {
    Zquic::PktProtState tx;
    if (!tx.init(secret, Zquic::PktNumSpace::AppData, true)) return false;
    ZmRef<Zquic::RxPkt> pkt{app()->rxPktAllocator()->allocBuf(nullptr, 0)};
    if (!pkt) return false;
    enum { PNLength = 2 };
    int h = Zquic::Pkt::writeShort(pkt->data_(), pkt->size, cid, pn, PNLength);
    if (h < 0) return false;
    uint8_t payload[32]{};
    if (Zquic::FrameCodec::writePing(payload, sizeof(payload)) != 1) return false;
    int n = Zquic::PktProt::protectShort(
      pkt->data_(), pkt->size, tx, pn,
      Zquic::byteSpan(pkt->data_(), unsigned(h)),
      Zquic::byteSpan(payload, sizeof(payload)), unsigned(h) - PNLength, PNLength);
    if (n < 0) return false;
    pkt->skip = 0;
    pkt->length = unsigned(n);
    pkt->local = ZuMv(local);
    pkt->addr = ZuMv(remote);
    return Base::rxProtShortPkt(pkt, 0, unsigned(n),
      [this](auto level, auto packetPN, ZuBSpan frames, ZiSockAddr addr,
          const auto &packet, auto &ack, auto *qlog) {
        return Base::consumeProtFrames(
          level, packetPN, frames, ZuMv(addr), packet, ack, qlog,
          [](size_t, ZuBSpan, ZiSockAddr) { return true; },
          [](Zquic::PktNumSpace::T, const Zquic::Frame &, ZiSockAddr) {
            return true;
          });
      });
  }
  bool cidRetxValid(const Zquic::ControlFrame &frame) {
    Zquic::PktBuild build;
    return Base::buildRetxCtl(build, Zquic::controlRef(frame));
  }
  bool cidPacket(const Zquic::ControlFrame &frame) {
    return Base::recordTxPkt(Zquic::PktNumSpace::AppData, 0, 100,
      Zquic::controlRef(frame), true);
  }
  bool cidRetxQueued() const {
    return Base::txPackets_(Zquic::PktNumSpace::AppData).retransmitPending();
  }
  bool takeRetx() {
    Zquic::PktNumSpace::T space;
    Zquic::SentFrameRef ref;
    return Base::nextRetransmit_(space, ref);
  }
	  bool addLocalCID(
	    const Zquic::CxnID &id, uint64_t sequence,
	    const Zquic::ResetToken &token = {}) {
	    return Base::addLocalCID_(id, sequence, token);
	  }
	  bool addPeerCID(
	    const Zquic::CxnID &id, uint64_t sequence,
	    const Zquic::ResetToken &token = {}) {
	    return Base::addPeerCID_(id, sequence, token);
	  }
	  void setRuntimeCIDs(const Zquic::CxnID &local, const Zquic::CxnID &peer) {
	    Base::setRuntimeCIDs_(Zquic::CxnID{"initcid0"}, local, peer);
	  }
	  bool chooseMigrationPeerCID(
	    bool requireNewPeerCID, Zquic::CxnID &id, uint64_t &sequence,
	    Zquic::MigrationReason::T &reason) {
	    bool ok = Base::selectPeerCIDRx(
	      requireNewPeerCID, id, sequence);
	    reason = ok ? Zquic::MigrationReason::T(Zquic::MigrationReason::None) :
	      Zquic::MigrationReason::T(Zquic::MigrationReason::NoPeerCID);
	    return ok;
	  }
	  void setPeerTransportParams(const Zquic::TransportParams &params) {
	    Base::crypto_().peerParams(params);
	    Base::TxCryptoSnapshot snapshot;
	    Base::snapshotTxCrypto(snapshot);
	    app()->txRun([this, snapshot = ZuMv(snapshot)] {
	      Base::installTxCrypto(snapshot);
	    });
	  }
	  bool prepareActiveMigration(
	    const Zquic::MigrationParams &params,
	    Zquic::MigrationReason::T &reason, bool preferred = false) {
	    return Base::prepareMig(params, reason, preferred);
	  }
	  bool activateActiveMigration() {
	    return Base::activateMig(false);
	  }
	  void activeRebindStart() {
	    Base::migRebindStart();
	  }
	  void activeRebindFail(Zquic::MigrationReason::T reason) {
	    Base::migRebindFail(reason);
	    Base::failMig(reason);
	  }
	  bool buildActiveMigChallenge(Zquic::Frame &frame) {
	    Zquic::PktBuild build;
	    Base::TxPktRefs refs;
	    Zquic::ControlFrame control;
	    if (!Base::buildMigChal(build, refs, control))
	      return false;
	    if (refs.count() != 1) return false;
	    unsigned used = 0;
	    return !Zquic::FrameCodec::parse(
	      build.data()[0],
	      frame, used) && used == build.data()[0].length();
	  }
	  bool installAppDataKeys(
	    const Zquic::TrafficSecret &rx, const Zquic::TrafficSecret &tx,
	    const Zquic::CxnID &localCID) {
	    return Base::installAppDataKeys_(rx, tx, localCID);
	  }
	  bool receiveNewCxnID(const Zquic::Frame &frame) {
	    return Base::receiveNewCxnID_(frame);
	  }
  bool receiveRetireCxnID(const Zquic::Frame &frame) {
    return Base::receiveRetireCxnID_(frame);
  }
  bool peerCID(
    uint64_t sequence, Zquic::CxnID &id,
    Zquic::ResetToken &token) const {
    auto cid = Base::peerCID_(sequence);
    if (!cid) return false;
    id = cid->id;
    token = cid->resetToken;
    return true;
  }
  bool peerCIDActive(uint64_t sequence) const {
    auto cid = Base::peerCID_(sequence);
    return cid && cid->state == Zquic::CxnState::Active;
  }
  Zquic::CxnID activeTxPeerCID() const {
    return Base::headerCID(Base::CIDSel::Peer);
  }
  bool localCIDRetired(uint64_t sequence) const {
    auto cid = Base::findLocalCID(sequence);
    return cid && cid->state == Zquic::CxnState::Retired;
  }
  void retiredLocalCID(uint64_t sequence, const Zquic::CxnID &id) {
    retiredSeq = sequence;
    retiredCID = id;
    ++retiredCount;
  }
	  bool checkStatelessReset(ZuBSpan datagram) {
	    return Base::checkStatelessReset_(datagram);
	  }
	  bool draining() const { return Base::drainingRx(); }
	  bool startHandshakeForTest() { return Base::startHandshake(); }
	  bool handshakingRx() const {
	    return Base::handshakeStartedRx() && !Base::establishedRx();
	  }
	  bool handshakingTx() const {
	    return Base::handshakeStartedTx() && !Base::establishedTx();
	  }
	  bool closingRx() const { return Base::closingRx_(); }
	  bool closingTx() const { return Base::closingTx_(); }
	  bool linkClosedRx() const { return Base::linkClosedRx_(); }
	  bool linkClosedTx() const { return Base::linkClosedTx_(); }
	  uint64_t closeErrorRx() const { return Base::closeErrorRx_(); }
	  uint64_t closeErrorTx() const { return Base::closeErrorTx_(); }
	  void closeTxForTest(uint64_t errorCode, bool application = false) {
	    Base::closeStateTx(errorCode, application);
	  }
  void scheduleStream(const ZmRef<TestStream> &stream) {
    Base::streamWritable(stream);
  }
  ZmRef<TestStream> nextScheduledStream() {
    return Base::nextWritableStream();
  }
  void clearScheduledStreams() { Base::clearWritableStreams(); }
  void postPeerLimits(uint64_t duplex, uint64_t simplex) {
    Base::setPeerStreamLimitsRx(duplex, simplex);
  }
  bool receiveMaxStreams(
    Zquic::StreamType::T type, uint64_t maximum) {
    Zquic::Frame frame;
    frame.type = Zquic::FrameType::MaxStreams;
    frame.streamType = type;
    frame.value = maximum;
    return Base::rxApplyMaxStreams(frame);
  }
  bool queueMaxStreams(
    Zquic::StreamType::T type, uint64_t maximum) {
    return Base::txQueueControl(Zquic::ControlFrame::flowUpdate(
      Zquic::FlowUpdate{
	Zquic::FrameType::MaxStreams, 0, maximum, type}));
  }
  bool flushControlSends() {
    return Base::flushCtlStreams(
      ZiSockAddr{},
      [](Zquic::PktBuild &) { return true; },
      [](Zquic::PktBuild &, ZiSockAddr, const Base::TxPktRefs &) {
	return true;
      });
  }
  unsigned scheduledStreams() const {
    return Base::scheduledStreamCount();
  }
  ZmRef<TestStream> findTxStream(int64_t id) const {
    return Base::findTxStream_(id);
  }
  void dataBlockedForTest(uint64_t maximum) {
    Base::dataBlocked(maximum);
  }
	  void closeTransport(Zquic::FrameType::T type, uint64_t errorCode) {
	    Base::closeLink(errorCode);
	    Base::transportClose_(type, errorCode);
	  }
	  void closeForTest(uint64_t errorCode = 0) {
	    Base::closeState(errorCode);
	  }
  void initServerPath(ZiSockAddr local, ZiSockAddr remote) {
    Base::initServerPathTx(ZuMv(local), ZuMv(remote));
  }
  void initClientPath(ZiSockAddr local, ZiSockAddr remote) {
    Base::initClientPathTx(ZuMv(local), ZuMv(remote));
  }
  void observePath(ZiSockAddr local, ZiSockAddr remote) {
    Base::startPathValidation(ZuMv(local), ZuMv(remote));
  }
  bool epochPacket(unsigned pn, Zquic::EcnMark::T ecn = Zquic::EcnMark::NotECT,
      TestStream *stream = nullptr) {
    constexpr auto space = Zquic::PktNumSpace::AppData;
    Base::setTxPN(space, pn + 1);
    Base::TxPktRefs refs;
    if (stream) {
      Zquic::TxRange range;
      if (!refs.add(Zquic::SentFrameRef::stream(stream->id(), range, true), stream))
	return false;
    }
    return Base::recordTxPkt(space, pn, 100, refs, true, false, 0,
      Zquic::PktNumSpace::N, 0, Zquic::PktType::Short, ecn);
  }
  void epochAck(unsigned pn, Zquic::AckECN ecn = {}) {
    Base::AckSnapshot ack;
    ack.level = Zquic::PktNumSpace::AppData;
    ack.ranges.push(Zquic::AckRange{pn, pn});
    ack.ecn = ecn;
    ack.receivedUS = uint64_t(Zm::now().microsecs()) + 1000;
    Base::processAckFrameTx(ack);
  }
  uint64_t controllerFlight() const {
    return Base::congestionWindow() - Base::congestionAllowance_();
  }
  auto pmtudTimeoutFn() { return Base::cxnTimerFn(Zquic::CxnTimer::PMTUD); }
  bool beginPMTUD() {
    return Base::sendPMTUDProbe(Base::activePathRemote_(),
      [](Zquic::PktBuild &, ZiSockAddr, unsigned) { return true; });
  }
  bool activeECN() const { return !Base::ecnDisabled_(); }
  void advertisePreferred(const ZiSockAddr &local, const ZiSockAddr &local6 = {}) {
    Zquic::PreferredAddress preferred;
    preferred.ipv4 = local;
    preferred.ipv6 = local6;
    Zquic::CxnIDGen::random(preferred.cid);
    preferred.resetToken.generate();
    Base::setPreferredAddress_(preferred);
    Base::TxCryptoSnapshot snapshot;
    Base::snapshotTxCrypto(snapshot);
    Base::installTxCrypto(snapshot);
  }
  void observePreferred(ZiSockAddr local, ZiSockAddr remote,
      bool fresh, bool nonProbing, uint64_t pn) {
    Base::observePathRxTx(ZuMv(local), ZuMv(remote), 1200, false, false,
      fresh, nonProbing, pn);
  }
  ZiSockAddr activeLocal() const { return Base::activePathLocal_(); }
  uint64_t migrationAttemptForTest() const { return Base::migrationResult().attemptID; }
  void observePathRx(ZiSockAddr local, ZiSockAddr remote) {
    Base::observePathRxTx(ZuMv(local), ZuMv(remote), false);
  }
  bool migrationActive() const { return Base::migrationActive_(); }
  Zquic::PathInfo candidatePathInfo() const {
    return Base::candidatePathInfo_();
  }
  ZuBSpan validatingChallenge() const {
    return Base::validatingChallenge_();
  }
  bool pathResponse(ZuBSpan data) { return Base::onPathResponse(data); }
  void pathResponseRx(ZuBSpan data) { Base::rxPathResponse(data); }
  void pathTimeout() { Base::pathExpired(); }
  void flushTx_() { ++txFlushQueued; }
  void flushTx_(ZiSockAddr) { ++txFlushQueued; }
  void queueTxFlush() { ++txFlushQueued; }
  void queueTxFlush(ZiSockAddr) { ++txFlushQueued; }
  Zquic::LinkDiag diag() const { return Base::diag_(); }
};

struct HubFixture {
  ZiMultiplex	mx;
  HubApp	app;

  HubFixture(bool cc = true, bool ecn = false, bool start = true,
      unsigned maxUDP = Zquic::MinUDPPayload) : mx{mxParams()} {
    ZiAssert(app.init(Zquic::HubParams(&mx, "3", "4")
	.maxUDP(maxUDP).congestionControl(cc).ecn(ecn)),
      "Zquic", (), "API test hub init failed", return);
    ZiAssert(!start || mx.start(), "Zquic", (),
      "API test multiplexer start failed", return);
  }
  ~HubFixture() {
    app.final();
    mx.stop();
  }

  static ZiMxParams mxParams() {
    return ZiMxParams()
      .scheduler([](auto &s) {
	s.nThreads(4);
      })
      .rxThread(1).txThread(2);
  }
};

static bool parseFrame(const uint8_t *b, int n, Zquic::Frame &frame)
{
  unsigned used = 0;
  return n > 0 && !Zquic::FrameCodec::parse(
    ZuBSpan{b, unsigned(n)}, frame, used) &&
    used == unsigned(n);
}

static ZmRef<ZiIOBuf> streamPkt(
  uint64_t id, uint64_t offset, ZuBSpan payload, bool fin,
  Zquic::Frame &frame, unsigned &used)
{
  ZmRef<ZiIOBuf> packet = ([](void *owner) {
      static Zi::IOBufAllocator *allocator =
	([] { static ZiMultiplex mx; return Zquic::pktRxAllocator(&mx); })();
      return allocator->allocBuf(owner, 0);
    })(nullptr);
  if (ZuUnlikely(!packet)) return nullptr;
  int n = Zquic::FrameCodec::writeStream(
    packet->data_(), packet->size, id, offset, payload, fin);
  if (n <= 0) return nullptr;
  packet->skip = 0;
  packet->length = unsigned(n);
  if (Zquic::FrameCodec::parse(
      ZuBSpan{packet->data(), packet->length}, frame, used) ||
      used != packet->length)
    return nullptr;
  return packet;
}



void testLongProbePadding()
{
  ZuTestScopeRT(testLongProbePadding);

  HubFixture fixture;
  Zquic::TrafficSecret secret;
  ZuBArray<32> material(32, false);
  memset(material.data(), 0x35, material.length());
  ZQUIC_CHECK_RT(Zquic::PktProt::deriveSecret(
      secret, &Ztls_::opensslAes128gcmsha256, material),
    "long probe secret derivation failed");
  ZmRef<TestLink> link = new TestLink{&fixture.app};
  link->setRuntimeCIDs(
    Zquic::CxnID{"local000"}, Zquic::CxnID{"peer0000"});
  for (unsigned early = 0; early < 2; ++early) {
    for (unsigned pnLength = 1; pnLength <= 4; ++pnLength) {
      ZmRef<ZiIOBuf> packet;
      bool sent = link->longProbe(secret, pnLength, bool(early), packet);
      ZQUIC_CHECK_RT(sent && packet, "tiny long probe protection failed");
      if (!sent || !packet) continue;
      Zquic::LongHdr header;
      ZQUIC_CHECK_RT(Zquic::Pkt::parseLong(packet->cspan(), header) >= 0 &&
	packet->length >= header.pnOffset + 4 + 16 &&
	uint64_t(header.pnOffset) + header.length == packet->length,
        "long padding/sample or encoded packet length mismatch");
      uint64_t pn = 0;
      unsigned offset = 0;
      int n = Zquic::PktProt::unprotectLong(
        packet->data_(), packet->length, secret, 0,
        header.pnOffset, pn, offset);
      unsigned expected = pnLength < 4 ? 4 - pnLength : 1;
      ZQUIC_CHECK_RT(n == int(expected) && pn == pnLength - 1 &&
	packet->data_()[offset] == Zquic::FrameType::Ping,
        "long probe plaintext or packet number mismatch");
      for (unsigned i = 1; i < expected; ++i)
        ZQUIC_CHECK_RT(packet->data_()[offset + i] == 0,
	"long probe padding is not PADDING");
    }
  }
  ZQUIC_CHECK_RT(!link->diag().failures(), "long probes counted as failures");
}

void testParams()
{
  ZuTestScope(testParams);

  ZuCSpan alpn[] = { "zquic-test" };

  auto clientParams = Zquic::ClientParams(nullptr, "1", "2")
    .caPath("ca.pem")
    .certPath("client.pem")
    .keyPath("client.key")
    .asyncThread("4")
    .maxData(1<<20)
    .maxStreamData(1<<16)
    .maxStreamsDuplex(16)
    .maxStreamsSimplex(4)
    .heartBeat(ZuTime{7})
    .maxUDP(1200)
    .migrationMode(Zquic::MigrationMode::Disabled)
    .activeMigration(true)
    .migCIDRes(Zquic::LocalCIDLimit + 8)
    .migCloseOnFail(true)
    .alpn(alpn)
    .errorFn(Zquic::defaultErrorFn());
  ZuCHECK(clientParams.migrationMode() == Zquic::MigrationMode::Active,
    "activeMigration(true) did not enable active migration mode");
  ZuCHECK(clientParams.activeMigration(),
    "active migration getter did not report enabled mode");
  ZuCHECK(clientParams.migCIDRes() ==
      Zquic::LocalCIDLimit - 1,
    "migration CID reserve was not clamped");
  ZuCHECK(clientParams.migCloseOnFail(),
    "migration close-on-failure getter did not report enabled policy");
  ZuCHECK(clientParams.heartBeat() == ZuTime{7},
    "heartBeat getter did not report configured interval");
  auto zeroParams = Zquic::ClientParams(nullptr, "1", "2")
    .heartBeat(ZuTime{0});
  ZuCHECK(!*zeroParams.heartBeat(),
    "explicit zero heartBeat interval was not normalized to null");
  bool badHeartBeat = false;
  try {
    (void)Zquic::ClientParams(nullptr, "1", "2").heartBeat(ZuTime{-1});
  } catch (const ZeException &) {
    badHeartBeat = true;
  }
  ZuCHECK(badHeartBeat, "negative heartBeat interval was accepted");

  auto serverParams = Zquic::ServerParams(nullptr, "1", "2")
    .caPath("ca.pem")
    .certPath("server.pem")
    .keyPath("server.key")
    .alpn(ZuSpan<ZuCSpan>{"h3"})
    .errorFn(Zquic::defaultErrorFn());
  ZuCHECK(serverParams.migrationMode() == Zquic::MigrationMode::Passive,
    "default migration mode was not passive");
  ZuCHECK(!serverParams.activeMigration(),
    "default active migration getter unexpectedly reported active");
  ZuCHECK(!serverParams.migCloseOnFail(),
    "default migration close-on-failure policy was not false");
  ZuCHECK(!*serverParams.heartBeat(),
    "default heartBeat interval was not disabled");

  HubFixture fixture;
  {
    ZmRef<TestLink> link = new TestLink{&fixture.app, false};
    ZuCHECK(link->localParams().disableActiveMigration,
      "default local transport params enabled active migration");
  }
  {
    fixture.app.activeMigration(true);
    ZmRef<TestLink> link = new TestLink{&fixture.app, false};
    ZuCHECK(!link->localParams().disableActiveMigration,
      "active migration mode did not clear disable_active_migration");
  }
}

void testStreamShape()
{
  ZuTestScope(testStreamShape);

  HubFixture fixture;
  ZmRef<TestLink> client = new TestLink{&fixture.app, false};
  ZmRef<TestLink> server = new TestLink{&fixture.app, true};

  auto c0 = client->stream();
  auto c1 = client->stream(Zquic::StreamType::Simplex);
  auto s0 = server->stream();

  ZuCHECK(c0 && c0->id() == 0, "client bidi stream ID mismatch");
  ZuCHECK(c1 && c1->id() == 2, "client uni stream ID mismatch");
  ZuCHECK(s0 && s0->id() == 1, "server bidi stream ID mismatch");
  ZuCHECK(c0->link() == client.ptr() && c1->link() == client.ptr(),
    "client stream owner link mismatch");
  ZuCHECK(s0->link() == server.ptr(), "server stream owner link mismatch");
  ZuCHECK(client->streamCount() == 1 && !client->findStream(c1->id()) &&
      client->findTxStream(c1->id()) == c1,
    "send-only client stream must reside in the Tx registry");
  ZuCHECK(client->findStream(0) == c0, "stream hash lookup failed");

  {
    auto tx = c0->txStream_();
    tx << "abc" << Zi::flush();
  }
  ZuCHECK(c0->txBytes() == 3, "stream Tx byte accounting mismatch");
  c0->fin();
  c0->reset(7);
  c0->stop(9);
  ZuCHECK(c0->finSent(), "FIN state was not recorded");

  client->closeForTest(42);
  ZuCHECK(client->closed() && client->closeError() == 42,
    "link close state mismatch");
}

void testWritableMembership()
{
  ZuTestScope(testWritableMembership);

  HubFixture fixture;
  ZmRef<TestLink> link = new TestLink{&fixture.app};
  auto first = link->stream();
  auto second = link->stream();
  ZuCHECK(first && second, "writable stream setup failed");
  if (!first || !second) return;
  int refs = first->refCount();
  link->scheduleStream(first);
  link->scheduleStream(first);
  link->scheduleStream(second);
  ZuCHECK(link->scheduledStreams() == 2 && first->refCount() == refs + 1,
    "duplicate writable notification added membership or ownership");
  auto dequeued = link->nextScheduledStream();
  ZuCHECK(dequeued == first && !first->txQueued(),
    "writable dequeue lost FIFO order or queued flag");
  link->scheduleStream(first);
  ZuCHECK(link->nextScheduledStream() == second &&
      link->nextScheduledStream() == first && !link->scheduledStreams(),
    "writable requeue did not preserve FIFO order");
  dequeued = nullptr;
  ZuCHECK(first->refCount() == refs, "writable dequeue retained ownership");
  link->scheduleStream(first);
  link->scheduleStream(second);
  link->clearScheduledStreams();
  ZuCHECK(!first->txQueued() && !second->txQueued() &&
      !link->scheduledStreams() && first->refCount() == refs,
    "writable cancellation retained membership, flags, or ownership");
}

void testAlignedSurfaceShape()
{
  ZuTestScope(testAlignedSurfaceShape);

  ZiMultiplex mx{HubFixture::mxParams()};
  ClientShapeApp clientApp;
  bool clientOK = clientApp.init(
    Zquic::ClientParams(&mx, "3", "4").alpn(ZuSpan<ZuCSpan>{"h3"}));
  ZuCHECK(clientOK, "aligned client app init failed");
  if (!clientOK) {
    mx.stop();
    return;
  }
  ServerShapeApp serverApp;
  bool serverOK = serverApp.init(
    Zquic::ServerParams(&mx, "3", "4")
      .certPath("server.pem")
      .keyPath("server.key")
      .alpn(ZuSpan<ZuCSpan>{"h3"}));
  ZuCHECK(serverOK, "aligned server app init failed");
  if (!serverOK) {
    clientApp.final();
    mx.stop();
    return;
  }
  bool mxOK = mx.start();
  ZuCHECK(mxOK, "aligned shape multiplexer start failed");
  if (!mxOK) return;

  ZmRef<ClientShapeLink> client = new ClientShapeLink{&clientApp};
  ZmRef<ClientShapeStream> c0;
  ZmBlock<>{}([&clientApp, &client, &c0](auto wake) {
    clientApp.rxRun([client, &c0, wake = ZuMv(wake)]() mutable {
      c0 = client->stream();
      wake();
    });
  });
  ZuCHECK(c0 && c0->id() == 0 && c0->link() == client.ptr() &&
      !client->isServer(),
    "client aligned link/stream shape mismatch");
#ifdef Zquic_DEBUG
  ZuCHECK(client->diag().unhandledAppEvents(),
    "default client stream-open hook was not visible in diagnostics");
#endif

  ZmRef<ServerShapeLink> server = new ServerShapeLink{&serverApp};
  ZmRef<ServerShapeStream> s0;
  ZmBlock<>{}([&serverApp, &server, &s0](auto wake) {
    serverApp.rxRun([server, &s0, wake = ZuMv(wake)]() mutable {
      s0 = server->stream();
      wake();
    });
  });
  ZuCHECK(s0 && s0->id() == 1 && s0->link() == server.ptr() &&
      server->isServer(),
    "server aligned link/stream shape mismatch");

  in_addr localIP;
  localIP.s_addr = htonl(0x0a000001);
  in_addr remoteIP;
  remoteIP.s_addr = htonl(0x0a000002);
  ZiSockAddr localAddr{ZiIP{localIP}, 4433};
  ZiSockAddr remoteAddr{ZiIP{remoteIP}, 50000};
  ZmBlock<>{}([&serverApp, &server, &localAddr, &remoteAddr](auto wake) {
    serverApp.txRun([
      server, localAddr, remoteAddr, wake = ZuMv(wake)
    ]() mutable {
      server->initSrvPath(localAddr, remoteAddr);
      wake();
    });
  });
  bool pathInfoSeen = false;
  Zquic::PathInfo activePath;
  Zquic::PathInfo candidatePath;
  ZmBlock<>{}([&server, &activePath, &candidatePath, &pathInfoSeen](auto wake) {
    server->pathInfo([
      &activePath, &candidatePath, &pathInfoSeen, wake = ZuMv(wake)
    ](const Zquic::PathInfo &active, const Zquic::PathInfo &candidate)
	mutable {
      activePath = active;
      candidatePath = candidate;
      pathInfoSeen = true;
      wake();
    });
  });
	  ZuCHECK(pathInfoSeen &&
	      activePath.role == Zquic::PathRole::Active &&
	      activePath.local == localAddr &&
	      activePath.remote == remoteAddr &&
	      candidatePath.role == Zquic::PathRole::Active &&
	      !candidatePath.remote,
	    "posted server pathInfo snapshot mismatch");
  bool migrationStateSeen = false;
  Zquic::MigrationResult migrationState;
  ZmBlock<>{}([&server, &migrationState, &migrationStateSeen](auto wake) {
    server->migrationState([
      &migrationState, &migrationStateSeen, wake = ZuMv(wake)
    ](const Zquic::MigrationResult &result) mutable {
      migrationState = result;
      migrationStateSeen = true;
      wake();
    });
  });
  ZuCHECK(migrationStateSeen &&
      migrationState.state == Zquic::MigrationState::Idle &&
      migrationState.active.role == Zquic::PathRole::Active &&
      migrationState.active.remote == remoteAddr,
    "posted server migrationState snapshot mismatch");

#ifdef Zquic_DEBUG
  ZiSockAddr promotedRemote{ZiIP{remoteIP}, 50001};
  bool promoted = false;
  ZiSockAddr peerAfterPromotion;
  ZmBlock<>{}([&serverApp, &server, &localAddr, &promotedRemote](auto wake) {
    server->onStarted(ZuMv(wake));
    serverApp.txRun([server, localAddr, promotedRemote]() mutable {
      server->observePath(localAddr, promotedRemote);
    });
  });
  ZmBlock<>{}([&serverApp, &server, &promoted](auto wake) {
    server->onPromoted(ZuMv(wake));
    serverApp.txRun([server, &promoted]() mutable {
      promoted = server->promoteObservedPath();
    });
  });
  ZmBlock<>{}([&serverApp, &server, &peerAfterPromotion](auto wake) {
    serverApp.txRun([server, &peerAfterPromotion, wake = ZuMv(wake)]() mutable {
      peerAfterPromotion = server->peer();
      wake();
    });
  });
  ZuCHECK(promoted &&
      peerAfterPromotion == promotedRemote,
    "server peer address was not updated after path promotion");
#endif

  // Drain path-promotion work while its app and links remain valid.
  mx.stop();
  s0 = nullptr;
  server = nullptr;
  c0 = nullptr;
  client = nullptr;
  serverApp.final();
  clientApp.final();
}

void testPendingTxReasons()
{
  ZuTestScopeRT(testPendingTxReasons);

  HubFixture fixture;
  ZmRef<TestLink> link = new TestLink{&fixture.app};
  auto &app = fixture.app;
  app.defer();
  app.enterTx();
  unsigned flushes = link->txFlushQueued;
  unsigned recovery = link->txRecoveryCalls;
  for (unsigned i = 0; i < 5; ++i) {
    link->postOrdinaryTx();
    link->postRecoveryTx();
  }
  ZQUIC_CHECK_RT(app.txPendingQ.count_() == 2,
    "duplicate Tx reasons posted more than one continuation each");
  app.runTx();
  app.runTx();
  ZQUIC_CHECK_RT(!app.txPending() && link->txFlushQueued == flushes + 1 &&
      link->txRecoveryCalls == recovery + 1,
    "ordinary/recovery continuations did not run independently exactly once");
  link->postOrdinaryTx();
  link->postRecoveryTx();
  app.deferMode = false;
  link->resetRuntimeForWork();
  app.deferMode = true;
  app.enterTx();
  flushes = link->txFlushQueued;
  recovery = link->txRecoveryCalls;
  link->postOrdinaryTx();
  link->postRecoveryTx();
  ZQUIC_CHECK_RT(app.txPendingQ.count_() == 4,
    "runtime reset retained stale pending reason bits");
  app.runTx();
  app.runTx();
  link->postOrdinaryTx();
  link->postRecoveryTx();
  ZQUIC_CHECK_RT(app.txPendingQ.count_() == 2 && link->txFlushQueued == flushes &&
      link->txRecoveryCalls == recovery,
    "stale continuation ran work or cleared a new runtime pending bit");
  app.runTx();
  app.runTx();
  ZQUIC_CHECK_RT(!app.txPending() && link->txFlushQueued == flushes + 1 &&
      link->txRecoveryCalls == recovery + 1,
    "new runtime continuations were suppressed by stale work");
}

void testLinkStateOwnership()
{
  ZuTestScope(testLinkStateOwnership);

  {
    HubFixture fixture;
    ZmRef<TestLink> link = new TestLink{&fixture.app};
    fixture.app.defer();
    fixture.app.enterRx();

    ZuCHECK(link->startHandshakeForTest() && link->handshakingRx() &&
	fixture.app.txPending(),
      "Rx handshake transition was not published to Tx");
    fixture.app.enterTx();
    ZuCHECK(!link->handshakingTx(),
      "Tx observed handshake state before applying its snapshot");
    fixture.app.runTx();
    ZuCHECK(link->handshakingTx() && !fixture.app.txPending(),
      "Tx did not apply the Rx handshake snapshot");
  }

  {
    HubFixture fixture;
    ZmRef<TestLink> link = new TestLink{&fixture.app};
    fixture.app.defer();
    fixture.app.enterRx();

    link->closeForTest(17);
    ZuCHECK(link->closingRx() && link->linkClosedRx() &&
	link->closeErrorRx() == 17 && link->closed() &&
	link->closeError() == 17 && fixture.app.txPending(),
      "Rx close transition did not update canonical state");
    fixture.app.enterTx();
    ZuCHECK(!link->closingTx() && !link->linkClosedTx(),
      "Tx observed Rx close state before applying its snapshot");
    fixture.app.runTx();
    ZuCHECK(link->closingTx() && link->linkClosedTx() &&
	link->closeErrorTx() == 17,
      "Tx did not apply the Rx close snapshot");
    while (fixture.app.txPending()) fixture.app.runTx();
  }

  {
    HubFixture fixture;
    ZmRef<TestLink> link = new TestLink{&fixture.app};
    fixture.app.defer();
    fixture.app.enterRx();

    ZuCHECK(link->startHandshakeForTest() && fixture.app.txPending(),
      "stale-snapshot close setup failed");
    fixture.app.enterTx();
    link->closeTxForTest(23, true);
    ZuCHECK(link->closingTx() && link->linkClosedTx() &&
	link->closeErrorTx() == 23 && fixture.app.rxPending(),
      "Tx close transition did not update Tx-owned state");
    fixture.app.runTx();
    ZuCHECK(link->closingTx() && link->linkClosedTx() &&
	link->closeErrorTx() == 23,
      "stale Rx snapshot regressed the Tx close state");
    fixture.app.runRx();
    ZuCHECK(link->closingRx() && link->linkClosedRx() &&
	link->closeErrorRx() == 23 && link->closed() &&
	link->closeError() == 23 && !fixture.app.rxPending() &&
	!fixture.app.txPending(),
      "Rx did not apply the Tx close snapshot exactly once");
  }

  {
    HubFixture fixture;
    ZmRef<TestLink> link = new TestLink{&fixture.app};
    fixture.app.defer();
    fixture.app.enterRx();

    link->closeForTest(31);
    fixture.app.enterTx();
    link->closeTxForTest(37);
    fixture.app.runTx();
    ZuCHECK(link->closingTx() && link->closeErrorTx() == 31,
      "canonical Rx close did not supersede a racing Tx close");
    while (fixture.app.txPending()) fixture.app.runTx();
    fixture.app.runRx();
    ZuCHECK(link->closingRx() && link->closeErrorRx() == 31 &&
	link->closeError() == 31 && !fixture.app.rxPending(),
      "racing Tx close overwrote the canonical Rx close");
  }
}

void testStreamLimitOwnership()
{
  ZuTestScope(testStreamLimitOwnership);

  HubFixture fixture;
  ZmRef<TestLink> link = new TestLink{&fixture.app};
  link->initClientPath(ZiSockAddr{}, ZiSockAddr{});
  fixture.app.defer();

  link->postPeerLimits(1, 2);
  ZuCHECK(!fixture.app.txPending() &&
      link->peerStreamLimit(Zquic::StreamType::Duplex) == 1 &&
      link->peerStreamLimit(Zquic::StreamType::Simplex) == 2,
    "Rx did not install the initial stream allowances locally");

  ZuCHECK(link->stream(), "initial peer stream limit did not open a stream");
  ZuCHECK(!link->stream() &&
      link->queuedLocalStreams(Zquic::StreamType::Duplex) == 1,
    "saturated peer stream limit did not queue a local stream");
  while (fixture.app.txPending()) fixture.app.runTx();

  fixture.app.enterRx();
  ZuCHECK(link->receiveMaxStreams(Zquic::StreamType::Duplex, 2) &&
      fixture.app.txPending(),
    "MAX_STREAMS did not publish the admitted stream to Tx");
  ZuCHECK(link->peerStreamLimit(Zquic::StreamType::Duplex) == 2 &&
      link->localStreamsOpened(Zquic::StreamType::Duplex) == 2 &&
      !link->queuedLocalStreams(Zquic::StreamType::Duplex),
    "MAX_STREAMS did not open the queued local stream on Rx");
  while (fixture.app.txPending()) fixture.app.runTx();
  fixture.app.enterRx();
  while (fixture.app.rxPending()) fixture.app.runRx();

  link->setLocalStreamLimit(Zquic::StreamType::Duplex, 4);
  fixture.app.enterTx();
  ZuCHECK(link->queueMaxStreams(Zquic::StreamType::Duplex, 3) &&
      link->flushControlSends(),
    "Tx rejected its current MAX_STREAMS control using Rx-owned state");
}

void testStatelessResetDetection()
{
  ZuTestScope(testStatelessResetDetection);

#ifdef Zquic_DEBUG
	Zi::Path qlogPath = testPath("ZquicAPIStatelessReset.sqlog");
	ZiFile::remove(qlogPath);
	ZquicLogParams params;
	params.enabled(true).path(qlogPath);
#endif

	HubFixture fixture;
#ifdef Zquic_DEBUG
	ZuCHECK(ZquicLogger::init(
	    fixture.app.qlogTrace(), params, Zquic::Vantage::Unknown),
	  "stateless reset qlog init failed");
	ZquicLogger::start();
#endif
	ZmRef<TestLink> link = new TestLink{&fixture.app};
  Zquic::ResetToken token{"0123456789abcdef"};
  uint8_t packet[64] = {};
  memset(packet, 0xa5, sizeof(packet));
  packet[0] = 0x65;
  memcpy(
    packet + sizeof(packet) - Zquic::ResetToken::Length,
    token.data(), Zquic::ResetToken::Length);
  ZuBSpan datagram{packet, sizeof(packet)};

  Zquic::ResetToken decoded;
  ZuCHECK(!Zquic::StatelessReset::decode(decoded, datagram) &&
      decoded == token,
    "stateless reset token decode mismatch");
  ZuCHECK(Zquic::StatelessReset::verify(datagram, token),
    "stateless reset token verify failed");
  ZuCHECK(!link->checkStatelessReset(datagram) && !link->draining(),
    "link accepted stateless reset before peer token was known");
  link->setPeerResetToken(token);
  auto stream = link->stream();
  link->scheduleStream(stream);
  ZuCHECK(link->scheduledStreams() == 1,
    "stateless reset queued-stream setup failed");
  fixture.app.defer();
  ZuCHECK(link->checkStatelessReset(datagram) && link->draining() &&
      fixture.app.txPending() && !fixture.app.rxPending() &&
      link->scheduledStreams() == 1 && !link->statelessResetCount,
    "stateless reset did not defer Tx cleanup");
  ZuCHECK(link->checkStatelessReset(datagram) && fixture.app.txPending() &&
      !fixture.app.rxPending() && !link->statelessResetCount,
    "duplicate stateless reset queued duplicate completion");
  fixture.app.runTx();
  ZuCHECK(!fixture.app.txPending() && fixture.app.rxPending() &&
      !link->scheduledStreams() && !link->statelessResetCount,
    "stateless reset Tx cleanup/completion ordering mismatch");
  fixture.app.runRx();
  ZuCHECK(!fixture.app.rxPending() && link->statelessResetCount == 1,
    "stateless reset Rx completion did not fire exactly once");

#ifdef Zquic_DEBUG
	closeQLog(fixture.app.qlogTrace());
	ZquicLogger::stop();
	ZquicLogDiag diag = ZquicLogger::diag();
	ZquicLogger::final(fixture.app.qlogTrace());
  ZuCHECK(diag.recordsEnqueued >= 1,
    "stateless reset qlog enqueue mismatch");
  ZuCHECK(diag.recordsWritten >= 1,
    "stateless reset qlog write mismatch");
  ZuCHECK(diag.writerFailures == 0,
    "stateless reset qlog writer failure");
  ZtString<> qlog = readFile(qlogPath);
  ZuCHECK(qlog, "stateless reset qlog output missing");
  ZuCHECK(qlog.find<"zquic:stateless_reset">() >= 0,
    "stateless reset qlog event missing");
  ZuCHECK(qlog.find<"\"trigger\":\"received\"">() >= 0,
    "stateless reset qlog trigger missing");
  ZuCHECK(qlog.find<"\"reason\":\"token_match\"">() >= 0,
    "stateless reset qlog reason missing");
#endif
}


void testAckPublications()
{
  ZuTestScopeRT(testAckPublications);
  HubFixture fixture{true, false, false};
  auto &app = fixture.app;
  ZmRef<TestLink> link = new TestLink{&app};
  ZQUIC_CHECK_RT(link->ackEstablished(), "ACK fixture keys did not install");
  app.defer();
  constexpr auto space = Zquic::PktNumSpace::AppData;
  link->noteAck(space, 0, true, {}, false, Zquic::EcnMark::ECT0);
  link->noteAck(space, 2, true, {}, false, Zquic::EcnMark::CE);
  Zquic::PktBuild before;
  app.enterTx();
  ZuCheck(link->appendPendingAck(space, before) && !before.ack(space));
  app.runTx();
  app.runTx();
  Zquic::PktBuild sparse;
  ZuCheck(link->appendPendingAck(space, sparse) && sparse.ackPrefix() == 1 &&
      sparse.ackLargest() == 2);
  Zquic::Frame frame;
  unsigned used = 0;
  ZuCheck(!Zquic::FrameCodec::parse(sparse.data()[0], frame, used) &&
      frame.ackRanges.length() == 2 && frame.ackECN.ect0 == 1 && frame.ackECN.ce == 1);
  app.enterRx();
  link->noteAck(space, 1, true, {});
  app.runTx();
  link->ackSentTx(space, sparse.ackGen());
  Zquic::PktTxUpdate confirmed;
  confirmed.ackdAck[space] = true;
  confirmed.ackLargest[space] = sparse.ackPrefix() - 1;
  link->applyAckOfAckTx(confirmed);
  Zquic::PktBuild filled;
  ZuCheck(link->appendPendingAck(space, filled) && filled.ack(space) &&
      filled.ackGen() == 3 && !Zquic::FrameCodec::parse(filled.data()[0], frame, used) &&
      frame.ackRanges.length() == 1 && frame.ackRanges[0].first == 1 &&
      frame.ackRanges[0].largest == 2);
  link->ackSentTx(space, filled.ackGen());
  app.enterRx();
  // Fill all three spaces before Tx consumes any updates.
  for (unsigned i = 0; i < 52; ++i)
    for (unsigned j = 0; j < Zquic::PktNumSpace::N; ++j)
      link->noteAck(Zquic::PktNumSpace::T(j), 10 + 2*i, true, {});
  while (app.txPending()) app.runTx();
  for (unsigned j = 0; j < Zquic::PktNumSpace::N; ++j) {
    auto level = Zquic::PktNumSpace::T(j);
    unsigned expected = level == space ? 48 : 8;
    Zquic::PktBuild packet;
    ZuCheck(link->appendPendingAck(level, packet) && packet.ack(level) &&
	!Zquic::FrameCodec::parse(packet.data()[0], frame, used) &&
	frame.ackRanges.length() == expected);
    for (unsigned i = 0; i < frame.ackRanges.length(); ++i) {
      auto pn = 10 + 2*(52 - expected + i);
      ZuCheck(frame.ackRanges[i].first == pn && frame.ackRanges[i].largest == pn);
    }
  }
  link->recoveryStop();
}

void testPeerAckPublications()
{
  ZuTestScopeRT(testPeerAckPublications);
  HubFixture fixture{false, false, false};
  auto &app = fixture.app;
  ZmRef<TestLink> link = new TestLink{&app};
  ZQUIC_CHECK_RT(link->ackEstablished(), "peer ACK fixture keys did not install");
  ZQUIC_CHECK_RT(link->appPackets(70), "peer ACK fixture packets not retained");
  app.defer();
  constexpr auto space = Zquic::PktNumSpace::AppData;
  Zquic::Frame ack;
  ack.type = Zquic::FrameType::Ack;
  ack.ackRanges.push(Zquic::AckRange{1, 1});
  link->processAckFrame(space, ack);
  ZQUIC_CHECK_RT(!app.txPending(), "ordinary peer ACK posted to Tx");
  app.enterTx();
  link->drainPeerAcksTx();
  ZQUIC_CHECK_RT(link->appPacketCount() == 69,
    "Tx did not retire the atomically published peer ACK");
  app.enterRx();
  for (unsigned pn = 2; pn <= 65; ++pn) {
    ack.ackRanges[0].largest = pn;
    link->processAckFrame(space, ack);
  }
  ZQUIC_CHECK_RT(!app.txPending(), "ordinary peer ACK ring posted before pressure");
  ack.ackRanges[0].largest = 66;
  link->processAckFrame(space, ack);
  ZQUIC_CHECK_RT(app.txPending(), "full peer ACK ring did not use exceptional handoff");
  while (app.txPending()) app.runTx();
  ZQUIC_CHECK_RT(link->appPacketCount() == 4,
    "peer ACK pressure lost ACK coverage or reordered fallback");
  app.enterRx();
  ack.ackRanges[0] = Zquic::AckRange{67, 67};
  link->processAckFrame(space, ack);
  ZQUIC_CHECK_RT(!app.txPending(), "advancing trimmed peer ACK did not resume publication");
  app.enterTx();
  ZQUIC_CHECK_RT(link->awaitPeerAckTx() && link->appPacketCount() == 3,
    "blocked sender did not recheck already published ACK");
  ZQUIC_CHECK_RT(!link->awaitPeerAckTx(), "empty peer ACK ring reported progress");
  app.enterRx();
  ack.ackRanges[0] = Zquic::AckRange{68, 68};
  link->processAckFrame(space, ack);
  ZQUIC_CHECK_RT(app.txPending(), "peer ACK did not wake a blocked sender");
  while (app.txPending()) app.runTx();
  ZQUIC_CHECK_RT(link->appPacketCount() == 2,
    "blocked-sender service lost the peer ACK");
  app.enterRx();
  ack.ackRanges[0] = Zquic::AckRange{70, 70};
  ack.ackRanges.push(Zquic::AckRange{69, 69});
  link->processAckFrame(space, ack);
  ZQUIC_CHECK_RT(app.txPending(), "sparse peer ACK skipped exceptional handoff");
  while (app.txPending()) app.runTx();
  ZQUIC_CHECK_RT(!link->appPacketCount(), "sparse peer ACK lost retained packet coverage");
  app.enterRx();
  app.enterTx();
  link->recoveryStop();
}

void testTargetedCreditUpdates()
{
  ZuTestScopeRT(testTargetedCreditUpdates);
  HubFixture fixture{false, false, false};
  auto &app = fixture.app;
  ZmRef<TestLink> link = new TestLink{&app};
  // Keep enough streams idle for reset to span two Tx turns.
  enum { Streams = 70 };
  ZuArray<ZmRef<TestStream>, Streams> streams;
  for (unsigned i = 0; i < Streams; ++i) {
    ZmRef<TestStream> stream = new TestLink::Streams::Node{link.ptr(), int64_t(i << 2)};
    stream->rxCredit(32);
    link->publishStreamTx(stream);
    streams.push(ZuMv(stream));
  }
  app.defer();
  auto grant = [&link](const ZmRef<TestStream> &stream, uint64_t maximum) {
    stream->extendRxCredit(maximum);
    link->postFlowUpdate(Zquic::FlowUpdate{
      Zquic::FrameType::MaxStreamData, uint64_t(stream->id()), maximum,
      Zquic::StreamType::Duplex});
  };
  grant(streams[69], 64);
  ZQUIC_CHECK_RT(app.txPendingQ.count_() == 1, "stream credit increase did not post once");
  app.runTx();
  Zquic::ControlFrame latest;
  bool valid = streams[69]->nextQueuedControl(latest) &&
    latest.type == Zquic::FrameType::MaxStreamData &&
    latest.streamID == uint64_t(streams[69]->id()) && latest.value == 64;
  for (unsigned i = 0; i < 69; ++i) valid &= !streams[i]->controlQueued();
  ZQUIC_CHECK_RT(valid && !app.txPending(),
    "a targeted credit increase did not reach its stream in one turn");
  app.enterRx();
  grant(streams[0], 96);
  grant(streams[0], 128);
  while (app.txPending()) app.runTx();
  ZQUIC_CHECK_RT(streams[0]->nextQueuedControl(latest) && latest.value == 128,
    "successive credit updates did not retain the largest limit");
  streams[0]->controlSent(latest);
  // Hold the Tx reset while Rx publishes a new frontier. These fixture streams
  // are registered only on Tx, isolating credit service from Rx stream close.
  app.enterRx();
  bool resetDone = false;
  link->resetLink([&resetDone] { resetDone = true; });
  grant(streams[69], 160);
  app.runTx();
  ZQUIC_CHECK_RT(!resetDone && app.txPending(),
    "runtime reset did not yield while retiring stream controls");
  // A new control on a stream beyond the first reset batch must survive its
  // later visit, independently of the published credit frontier above.
  streams[64]->queueStopSending(23);
  while (app.txPending()) app.runTx();
  ZQUIC_CHECK_RT(streams[69]->nextQueuedControl(latest) && latest.value == 160,
    "queued Tx reset swallowed credit published after the Rx reset boundary");
  while (app.rxPending()) app.runRx();
  app.enterTx();
  bool stopQueued = false;
  while (streams[64]->nextQueuedControl(latest)) {
    stopQueued |= latest.type == Zquic::FrameType::StopSending && latest.errorCode == 23;
    streams[64]->controlSent(latest);
  }
  ZQUIC_CHECK_RT(resetDone && stopQueued,
    "runtime reset completion lost a new-runtime stream control");
  link->recoveryStop();
}

void testShortSendCommit()
{
  ZuTestScopeRT(testShortSendCommit);
  constexpr auto space = Zquic::PktNumSpace::AppData;
  for (bool cc : {false, true})
  for (bool reset : {false, true}) for (bool accept : {false, true}) {
    HubFixture fixture{cc, false, false};
    auto &app = fixture.app;
    ZmRef<TestLink> link = new TestLink{&app};
    ZQUIC_CHECK_RT(link->ackEstablished(), "short send keys failed");
    link->resetPath();
    link->updateAckTx(space, {7, 7}, true, true, Zquic::EcnMark::NotECT, true, 0, 0);
    app.defer();
    app.enterTx();
    bool submitted = false;
    ZQUIC_CHECK_RT(!link->shortCommit([&submitted](ZmRef<ZiIOBuf>) {
      submitted = true;
      return true;
    }, false) && !submitted && !link->appPacketCount() && !link->timersActive(),
      "packet allocation failure installed recovery or reached submission");
    bool installed = false, nested = false, replaced = !reset;
    unsigned bytes = 0;
    bool sent = link->shortCommit([
      &bytes, &installed, &nested, &replaced, &link, &app, reset, accept
    ](ZmRef<ZiIOBuf> packet) {
      bytes = packet->length;
      TestLink::AckSnapshot peer;
      peer.level = space;
      peer.ranges.push(Zquic::AckRange{0, 0});
      installed = link->appPacketCount() == 1 && link->ackFrameValidTx(peer) &&
	!link->txPackets() && !link->timersActive() &&
	link->recoveryPathDiag().bytesTx == bytes;
      peer.ranges[0] = Zquic::AckRange{1, 1};
      installed &= !link->ackFrameValidTx(peer);
      nested = link->deferTxFlush() && link->deferTxFlush() && !app.txPending();
      if (reset) replaced = link->resetCommit();
      else {
	link->updateAckTx(space, {8, 7}, true, true, Zquic::EcnMark::NotECT, true, 0, 0);
      }
      return accept;
    });
    ZQUIC_CHECK_RT(installed,
      "wire visibility preceded recovery/PN/path state or followed timer/diagnostic work");
    ZQUIC_CHECK_RT(sent == accept && nested && replaced,
      "short send changed acceptance or did not defer nested flushes");
    if (reset) {
      ZQUIC_CHECK_RT(link->appPacketCount() == 1 &&
	  link->recoveryDiag().bytesInFlight == 99 && !link->txPackets() &&
	  !link->recoveryPathDiag().bytesTx && !app.txPending(),
	"old short send modified the reset runtime or retained a stale nested flush");
    } else {
      ZQUIC_CHECK_RT(link->appPacketCount() == unsigned(accept) &&
	  link->txPackets() == unsigned(accept) &&
	  link->recoveryPathDiag().bytesTx == (accept ? bytes : 0) &&
	  link->pendingAck(space) && app.txPendingQ.count_() == 1,
	"short send refusal/commit lost ACK work, path accounting or flush coalescing");
      Zquic::PktBuild pending;
      ZuCheck(link->appendPendingAck(space, pending) && pending.ackGen() == 2);
      auto before = link->txFlushQueued;
      app.runTx();
      ZQUIC_CHECK_RT(link->txFlushQueued == before + 1 && !app.txPending(),
	"nested short send did not resume exactly once after finish");
      bool unique = false;
      (void)link->shortCommit([&link, &unique](ZmRef<ZiIOBuf>) {
	TestLink::AckSnapshot peer;
	peer.level = space;
	peer.ranges.push(Zquic::AckRange{1, 1});
	unique = link->ackFrameValidTx(peer);
	return false;
      });
      ZQUIC_CHECK_RT(unique && link->appPacketCount() == unsigned(accept),
	"refused packet number was reused or refusal discarded an earlier record");
    }
    link->recoveryStop();
  }
  {
    HubFixture fixture{false, false, false};
    auto &app = fixture.app;
    ZmRef<TestLink> link = new TestLink{&app};
    ZQUIC_CHECK_RT(link->ackEstablished(), "immediate peer response keys failed");
    link->resetPath();
    app.defer();
    app.enterTx();
    bool retained = false;
    ZQUIC_CHECK_RT(link->shortCommit([&app, &link, &retained](ZmRef<ZiIOBuf>) {
      app.enterRx();
      Zquic::Frame ack;
      ack.type = Zquic::FrameType::Ack;
      ack.ackRanges.push(Zquic::AckRange{0, 0});
      link->processAckFrame(space, ack);
      app.enterTx();
      retained = link->appPacketCount() == 1 && !app.txPending();
      return true;
    }) && retained && link->txPackets() == 1,
      "immediate peer ACK ran recovery inside submission or posted ordinary ACK state");
    link->drainPeerAcksTx();
    ZQUIC_CHECK_RT(!link->appPacketCount(),
      "peer response published during submission did not retire the prepared record");
    while (app.rxPending()) app.runRx();
    while (app.txPending()) app.runTx();
    app.enterTx();
    bool submitted = false;
    ZQUIC_CHECK_RT(!link->shortCommit([&submitted](ZmRef<ZiIOBuf>) {
      submitted = true;
      return true;
    }, true, true) && !submitted && link->appPacketCount() == 1 &&
      link->recoveryDiag().bytesInFlight == 99,
      "allocation callback reset allowed an old-runtime packet to reach submission");
    link->recoveryStop();
  }
}

void disabledCongestionRecovery()
{
  ZuTestScope(disabledCongestionRecovery);
  ZuCheck(Zquic::HubParams{}.congestionControl());
  for (bool ecn : {false, true}) {
    HubFixture fixture{false, ecn};
    ZuCheck(!fixture.app.congestionControl());
    ZuCheck(fixture.app.ecn() == ecn);
    ZmRef<TestLink> link = new TestLink{&fixture.app};
    // Exercise constructor-only state as well as reset and space discard.
    auto diag = link->recoveryDiag();
    ZuCheck(!diag.congestionWindow && !diag.congestionSSThresh);
    ZuCheck(link->congestionAllowance_() == unsigned(-1));
    ZuCheck(link->recoveryPackets(32));
    diag = link->recoveryDiag();
    ZuCheck(diag.bytesInFlight == 32 * 1200);
    ZuCheck(!diag.congestionWindow && !diag.congestionSSThresh);
    link->recoveryAck(32, 32, true);
    diag = link->recoveryDiag();
    ZuCheck(diag.bytesInFlight == 2400);
    ZuCheck(!diag.congestionWindow && !diag.congestionSSThresh);
    ZuCheck(!link->recoveryPathDiag().ecnCEEvents);
    ZuCheck(!link->recoveryPathDiag().ecnValidationFailures);
    link->recoveryDiscard();
    ZuCheck(!link->recoveryDiag().bytesInFlight);
    link->recoveryReset();
    ZuCheck(link->recoveryPackets(1));
    link->recoveryAck(1, 1);
    ZuCheck(!link->recoveryDiag().bytesInFlight);
    link->recoveryStop();
    ZmBlock<>{}([link](auto wake) {
      link->disconnect_(false, [wake = ZuMv(wake)]() mutable { wake(); });
    });
  }
}

void testRecoveryRuntimeOrdering()
{
  ZuTestScope(testRecoveryRuntimeOrdering);
  for (unsigned mode = 0; mode < 3; ++mode) {
    bool ecn = mode != 0;
    unsigned count = mode == 2 ? 2 : 4;
    HubFixture fixture;
    ZmRef<TestLink> link = new TestLink{&fixture.app};
    link->recoveryReset();
    uint64_t initial = link->recoveryDiag().congestionWindow;
    ZuCHECK(link->recoveryPackets(count, ecn), "runtime recovery packet setup");
    link->recoveryAck(count, count, ecn);
    auto diag = link->recoveryDiag();
    ZuCHECK(diag.congestionWindow == initial/2 &&
      diag.bytesInFlight == (mode == 2 ? 1200 : 2400),
      "runtime same ACK must establish loss/ECN before growth");
    ZuCHECK(link->recoveryPathDiag().ecnCEEvents == unsigned(ecn) &&
      !link->recoveryPathDiag().ecnValidationFailures,
      "ECN variant must exercise the validated CE response");
    link->recoveryStop();
  }
  for (bool reset : {false, true}) {
    HubFixture fixture;
    ZmRef<TestLink> link = new TestLink{&fixture.app};
    link->recoveryReset();
    uint64_t initial = link->recoveryDiag().congestionWindow;
    ZuCHECK(link->recoveryPackets(TestLink::RecBatch + 1),
      "runtime continuation packet setup");
    fixture.app.defer();
    fixture.app.enterTx();
    link->recoveryAck(TestLink::RecBatch + 1, 1);
    ZuCHECK(fixture.app.txPending() &&
      link->recoveryDiag().congestionWindow == initial &&
      link->recoveryDiag().bytesInFlight == 1200,
      "runtime first continuation retires without committing growth");
    if (reset) link->recoveryReset();
    else link->recoveryDiscard();
    while (fixture.app.txPending()) fixture.app.runTx();
    ZuCHECK(link->recoveryDiag().congestionWindow == initial &&
      !link->recoveryDiag().bytesInFlight,
      "runtime reset/discard must reject queued growth and retire once");
    while (fixture.app.rxPending()) fixture.app.runRx();
    fixture.app.enterTx();
    link->recoveryStop();
  }
}

void testApplicationCallbacks()
{
  ZuTestScope(testApplicationCallbacks);

  HubFixture fixture;
  ZmRef<TestLink> server = new TestLink{&fixture.app, true};
  ZmRef<TestLink> client = new TestLink{&fixture.app};
  uint8_t b[128];
  Zquic::Frame frame;
  unsigned used = 0;

  auto local = client->stream();
  ZuCHECK(local && client->streamOpenCount == 1 &&
      client->localStreamOpens == 1 && client->lastOpened == local,
    "local stream-open callback mismatch");

  auto packet = streamPkt(0, 0, "abc", false, frame, used);
  ZuCHECK(packet && server->receiveFrame(frame, packet) == 0,
    "peer STREAM callback setup failed");
  auto peer = server->findStream(0);
  ZuCHECK(peer && server->streamOpenCount == 1 &&
      server->peerStreamOpens == 1 &&
      server->streamDataCount == 1 &&
      server->lastDataStream == peer &&
      !server->lastDataOffset &&
      server->lastDataLength == 3 &&
      !server->lastDataFin,
    "peer stream/data callbacks mismatch");

  int n = Zquic::FrameCodec::writeResetStream(b, sizeof(b), 0, 7, 3);
  ZuCHECK(parseFrame(b, n, frame) &&
      server->receiveFrame(frame) == 0 &&
      server->resetReceivedCount == 1 &&
      server->lastResetStream == peer &&
      server->lastResetAppError == 7 &&
      server->lastResetFinalSize == 3,
    "RESET_STREAM receive callback mismatch");

  n = Zquic::FrameCodec::writeStopSending(b, sizeof(b), local->id(), 9);
  ZuCHECK(parseFrame(b, n, frame) &&
      client->receiveFrame(frame) == 0 &&
      client->stopReceivedCount == 1 &&
      client->lastStopStream == local &&
      client->lastStopAppError == 9,
    "STOP_SENDING receive callback mismatch");

  auto reset = client->stream();
  reset->reset(11);
  ZuCHECK(client->resetSentCount == 1 &&
      client->lastResetSentID == uint64_t(reset->id()) &&
      client->lastResetSentAppError == 11,
    "local RESET_STREAM callback mismatch");

  auto stop = client->stream();
  stop->stop(12);
  ZuCHECK(client->stopSentCount == 1 &&
      client->lastStopSentID == uint64_t(stop->id()) &&
      client->lastStopSentAppError == 12,
    "local STOP_SENDING callback mismatch");

  client->dataBlockedForTest(4096);
  ZuCHECK(client->flowBlockedCount == 1 &&
      client->lastFlowType == Zquic::FrameType::DataBlocked &&
      client->lastFlowMaximum == 4096,
    "flow blocked callback mismatch");

  client->closeTransport(Zquic::FrameType::ConnectionClose, 42);
  ZuCHECK(client->transportCloseCount == 1 &&
      client->lastCloseType == Zquic::FrameType::ConnectionClose &&
      client->lastCloseError == 42,
    "transport close callback mismatch");

  ZmRef<TestLink> path = new TestLink{&fixture.app, true};
  in_addr localIP;
  localIP.s_addr = htonl(0x0a000001);
  in_addr oldRemoteIP;
  oldRemoteIP.s_addr = htonl(0x0a000002);
  ZiSockAddr localAddr{ZiIP{localIP}, 4433};
  ZiSockAddr oldRemote{ZiIP{oldRemoteIP}, 50000};
#ifdef Zquic_DEBUG
  ZiSockAddr newRemote{ZiIP{oldRemoteIP}, 50001};
  ZiSockAddr failRemote{ZiIP{oldRemoteIP}, 50002};
  ZiSockAddr disabledRemote{ZiIP{oldRemoteIP}, 50003};
#endif
  path->initServerPath(localAddr, oldRemote);
#ifdef Zquic_DEBUG
  fixture.app.migrationMode(Zquic::MigrationMode::Disabled);
  path->observePathRx(localAddr, disabledRemote);
  Zquic::LinkDiag disabledDiag = path->diag();
  ZuCHECK(!path->migrationActive() &&
      disabledDiag.tx.pathRxObserved == 1 &&
      disabledDiag.tx.pathValidationDisabled == 1 &&
      disabledDiag.tx.migration.policyReject == 1 &&
      disabledDiag.tx.pathValidating == 0,
    "disabled migration mode did not ignore peer tuple change");
  fixture.app.migrationMode(Zquic::MigrationMode::Passive);
  path->observePathRx(localAddr, newRemote);
  Zquic::PathInfo candidate = path->candidatePathInfo();
  ZuCHECK(path->migrationActive() &&
      candidate.role == Zquic::PathRole::Candidate &&
      candidate.migrationState == Zquic::MigrationState::Validating &&
      candidate.reason == Zquic::MigrationReason::Passive &&
      !candidate.validated,
    "candidate path migration snapshot mismatch");
  ZuBSpan challenge = path->validatingChallenge();
  uint8_t response[Zquic::PathChallenge::Length]{};
  memcpy(response, challenge.data(), challenge.length());
  ZuCHECK(path->pathResponse(ZuBSpan{response, sizeof(response)}),
    "path response did not promote candidate path");
  ZuCHECK(path->migrationStartedCount == 1,
    "migration-start callback count mismatch");
  ZuCHECK(path->pathUpdateCount == 1,
    "path-update callback count mismatch");
  ZuCHECK(path->migrationPromotedCount == 1,
    "migration-promoted callback count mismatch");
  ZuCHECK(path->migrationCallbackOrder == "SUP",
    "migration callback order mismatch: ", path->migrationCallbackOrder);
  ZuCHECK(path->lastPathValidated,
    "path-update callback did not report a validated path");
  ZuCHECK(path->lastPathMaxUDP >= Zquic::MinUDPPayload,
    "path-update callback MTU mismatch");
  Zquic::MigrationDiag passiveDiag = path->diag().tx.migration;
  ZuCHECK(passiveDiag.requested == 1 &&
      passiveDiag.started == 1 &&
      passiveDiag.promoted == 1 &&
      passiveDiag.peerObserved == 1 &&
      passiveDiag.natRebind == 1,
    "passive migration diagnostic counters mismatch");

  ZmRef<TestLink> timeoutPath = new TestLink{&fixture.app, true};
  timeoutPath->initServerPath(localAddr, oldRemote);
  timeoutPath->observePath(localAddr, failRemote);
  timeoutPath->pathTimeout();
  ZuCHECK(timeoutPath->migrationStartedCount == 1 &&
      timeoutPath->migrationFailureCount == 1 &&
      timeoutPath->migrationCallbackOrder == "SF" &&
      timeoutPath->lastMigration.state == Zquic::MigrationState::Failed &&
      timeoutPath->lastMigration.reason == Zquic::MigrationReason::Timeout,
    "migration failure callback mismatch");
  Zquic::MigrationDiag timeoutDiag = timeoutPath->diag().tx.migration;
  ZuCHECK(timeoutDiag.requested == 1 &&
      timeoutDiag.started == 1 &&
      timeoutDiag.abandoned == 1 &&
      timeoutDiag.failed == 1 &&
      timeoutDiag.timeouts == 1 &&
      timeoutDiag.peerObserved == 0,
    "timeout migration diagnostic counters mismatch");
#endif
}

void testStopSendingOwnership()
{
  ZuTestScope(testStopSendingOwnership);

  HubFixture fixture;
  ZmRef<TestLink> link = new TestLink{&fixture.app};
  auto stream = link->stream();
  uint8_t b[128];
  Zquic::Frame frame;
  int n = Zquic::FrameCodec::writeStopSending(
    b, sizeof(b), stream->id(), 9);
  ZuCHECK(parseFrame(b, n, frame),
    "STOP_SENDING ownership setup failed");

  fixture.app.defer();
  ZuCHECK(link->receiveFrame(frame) == 0 && !stream->stopReceived() &&
      fixture.app.txPending() && !stream->txCompleteCount &&
      stream->rxError() == Zquic::StreamError::None &&
      link->stopReceivedCount == 0,
    "STOP_SENDING did not defer completion to Tx");
  ZuCHECK(link->receiveFrame(frame) == 0 && fixture.app.txPending() &&
      !stream->txCompleteCount && link->stopReceivedCount == 0,
    "duplicate STOP_SENDING queued duplicate completion");

  fixture.app.enterTx();
  stream->reset(11);
  ZuCHECK(stream->txCompleteCount == 1 &&
      stream->txError() == Zquic::StreamError::Reset &&
      stream->txAppError() == 11,
    "racing reset did not complete stream on Tx");
  fixture.app.runTx();
  ZuCHECK(stream->txCompleteCount == 1 &&
      stream->txError() == Zquic::StreamError::Reset &&
      stream->txAppError() == 11,
    "STOP_SENDING/reset race completed Tx more than once");
  for (unsigned i = 0; fixture.app.txPending() && i < 4; ++i)
    fixture.app.runTx();
  ZuCHECK(!fixture.app.txPending() && stream->txCompleteCount == 1,
    "STOP_SENDING/reset race did not drain queued Tx work exactly once");

  HubFixture resetFixture;
  ZmRef<TestLink> resetLink = new TestLink{&resetFixture.app};
  auto resetStream = resetLink->stream();
  resetFixture.app.defer();
  resetFixture.app.enterTx();
  resetStream->stop(13);
  ZuCHECK(resetFixture.app.rxPending() &&
      resetStream->txError() == Zquic::StreamError::None,
    "local STOP_SENDING did not defer its Rx cause");

  n = Zquic::FrameCodec::writeResetStream(
    b, sizeof(b), resetStream->id(), 17, 0);
  ZuCHECK(parseFrame(b, n, frame),
    "RESET_STREAM ownership setup failed");
  resetFixture.app.enterRx();
  ZuCHECK(resetLink->receiveFrame(frame) == 0 &&
      resetStream->rxError() == Zquic::StreamError::Reset &&
      resetStream->rxAppError() == 17,
    "racing peer reset did not establish the Rx cause");
  resetFixture.app.runRx();
  ZuCHECK(resetStream->rxError() == Zquic::StreamError::Reset &&
      resetStream->rxAppError() == 17,
    "deferred STOP_SENDING overwrote the Rx reset cause");
  for (unsigned i = 0;
      (resetFixture.app.rxPending() || resetFixture.app.txPending()) && i < 8;
      ++i) {
    if (resetFixture.app.rxPending()) resetFixture.app.runRx();
    if (resetFixture.app.txPending()) resetFixture.app.runTx();
  }
  ZuCHECK(!resetFixture.app.rxPending() && !resetFixture.app.txPending(),
    "STOP_SENDING/RESET_STREAM race did not drain deferred work");
}

void testStreamRegistryOwnership()
{
  ZuTestScope(testStreamRegistryOwnership);

  HubFixture fixture;
  ZmRef<TestLink> link = new TestLink{&fixture.app, true};
  fixture.app.defer();
  fixture.app.enterRx();

  Zquic::Frame frame;
  unsigned used = 0;
  auto packet = streamPkt(2, 0, "", true, frame, used);
  ZuCHECK(packet && link->receiveFrame(frame, packet) == 0,
    "peer stream ownership setup failed");
  ZmRef<TestStream> stream = link->lastDataStream;
  ZuCHECK(stream && stream->retiredRx() && !link->findStream(2) && fixture.app.txPending(),
    "terminal peer stream was not retired on Rx");
  fixture.app.runTx();
  ZuCHECK(link->findTxStream(2) == stream && !fixture.app.rxPending(),
    "Tx publication posted back to Rx");

  fixture.app.enterTx();
  for (unsigned i = 0; fixture.app.txPending() && i < 8; ++i)
    fixture.app.runTx();
  ZuCHECK(!fixture.app.txPending() && !link->findTxStream(2),
    "Tx retirement did not remove the Tx-owned stream index");

  fixture.app.enterRx();
  packet = streamPkt(0, 0, "", true, frame, used);
  ZuCHECK(packet && link->receiveFrame(frame, packet) == 0 && !link->findStream(0),
    "request FIN did not detach the receive direction");
  auto request = link->lastDataStream;
  frame = {};
  frame.type = Zquic::FrameType::MaxStreamData;
  frame.streamID = 0;
  frame.value = 16384;
  ZuCHECK(link->applyMaxStreamData(frame), "detached request lost response credit");
  while (fixture.app.txPending()) fixture.app.runTx();
  ZuCHECK(request && link->findTxStream(0) == request &&
      request->txCreditLimit() >= frame.value && !fixture.app.rxPending(),
    "response credit did not reach Tx after request retirement");
  fixture.app.enterRx();
  frame.type = Zquic::FrameType::StopSending;
  frame.errorCode = 9;
  ZuCHECK(link->receiveFrame(frame) == 0 && link->receiveFrame(frame) == 0,
    "detached request lost STOP_SENDING");
  while (fixture.app.txPending()) fixture.app.runTx();
  while (fixture.app.rxPending()) fixture.app.runRx();
  ZuCHECK(request->stopReceived() && link->stopReceivedCount == 1,
    "response sender did not deliver exactly one stop callback");

}

void testCxnIDFrameLifecycle()
{
  ZuTestScope(testCxnIDFrameLifecycle);

  HubFixture fixture;
  ZmRef<TestLink> link = new TestLink{&fixture.app};
  Zquic::ResetToken peerToken{"0123456789abcdef"};
  Zquic::Frame f;
  f.type = Zquic::FrameType::NewCxnID;
  f.value = 5;
  f.offset = 0;
  f.length = 9;
  f.payload = "peerCID09";
  f.resetToken = peerToken;
  ZuCHECK(link->receiveNewCxnID(f),
    "NEW_CONNECTION_ID frame was rejected");

  Zquic::CxnID peerCID;
  Zquic::ResetToken foundToken;
  ZuCHECK(link->peerCID(5, peerCID, foundToken) &&
      peerCID == Zquic::CxnID{"peerCID09"} && foundToken == peerToken,
    "NEW_CONNECTION_ID did not store peer CID and reset token");

  Zquic::ResetToken peerToken2{"1234567890abcdef"};
  f.value = 6;
  f.offset = 6;
  f.length = 9;
  f.payload = "peerCID10";
  f.resetToken = peerToken2;
  ZuCHECK(link->receiveNewCxnID(f) &&
      !link->peerCID(5, peerCID, foundToken) &&
      link->peerCID(6, peerCID, foundToken) &&
      peerCID == Zquic::CxnID{"peerCID10"} &&
      foundToken == peerToken2,
    "retired peer CID slot was not reused for NEW_CONNECTION_ID");

  ZuCHECK(link->receiveNewCxnID(f) &&
      link->peerCID(6, peerCID, foundToken) &&
      peerCID == Zquic::CxnID{"peerCID10"} &&
      foundToken == peerToken2,
    "duplicate NEW_CONNECTION_ID tuple was not deterministic");

  Zquic::Frame duplicate = f;
  duplicate.payload = "peerCID11";
  ZuCHECK(!link->receiveNewCxnID(duplicate),
    "duplicate NEW_CONNECTION_ID sequence with different CID was accepted");

  duplicate = f;
  duplicate.resetToken = peerToken;
  ZuCHECK(!link->receiveNewCxnID(duplicate),
    "duplicate NEW_CONNECTION_ID sequence with different token was accepted");

  duplicate = f;
  duplicate.value = 7;
  duplicate.payload = "peerCID11";
  duplicate.resetToken = peerToken2;
  ZuCHECK(!link->receiveNewCxnID(duplicate),
    "duplicate reset token for different peer CID was accepted");

  Zquic::CxnID localCID{"local003"};
  Zquic::ResetToken localToken{"fedcba9876543210"};
  ZuCHECK(link->addLocalCID(localCID, 3, localToken),
    "local CID setup failed");
  Zquic::Frame retire;
  retire.type = Zquic::FrameType::RetireCxnID;
  retire.value = 3;
  ZuCHECK(link->receiveRetireCxnID(retire),
    "RETIRE_CONNECTION_ID frame was rejected");
	  ZuCHECK(link->localCIDRetired(3) && link->retiredCount == 1 &&
	      link->retiredSeq == 3 && link->retiredCID == localCID,
	    "RETIRE_CONNECTION_ID did not retire local CID and notify hook");

	  Zquic::CxnID selectedCID;
	  uint64_t selectedSeq = 0;
	  Zquic::MigrationReason::T reason = Zquic::MigrationReason::None;
	  ZmRef<TestLink> cidLink = new TestLink{&fixture.app};
	  cidLink->setRuntimeCIDs(
	    Zquic::CxnID{"local000"}, Zquic::CxnID{"peer0000"});
	  ZuCHECK(cidLink->addPeerCID(
	      Zquic::CxnID{"peer0001"}, 1,
	      Zquic::ResetToken{"abcdef0123456789"}) &&
	      cidLink->chooseMigrationPeerCID(
		true, selectedCID, selectedSeq, reason) &&
	      selectedCID == Zquic::CxnID{"peer0001"} && selectedSeq == 1 &&
	      reason == Zquic::MigrationReason::None,
	    "active migration did not select spare peer CID");

	  ZmRef<TestLink> noSpare = new TestLink{&fixture.app};
	  noSpare->setRuntimeCIDs(
	    Zquic::CxnID{"local010"}, Zquic::CxnID{"peer0100"});
	  selectedCID = {};
	  selectedSeq = 0;
	  reason = Zquic::MigrationReason::None;
	  ZuCHECK(noSpare->chooseMigrationPeerCID(
	      false, selectedCID, selectedSeq, reason) &&
	      selectedCID == Zquic::CxnID{"peer0100"} && selectedSeq == 0,
	    "passive migration did not reuse active peer CID");
	  ZuCHECK(!noSpare->chooseMigrationPeerCID(
	      true, selectedCID, selectedSeq, reason) &&
	      reason == Zquic::MigrationReason::NoPeerCID,
	    "active migration without spare peer CID was accepted");

	  ZmRef<TestLink> zeroCID = new TestLink{&fixture.app};
	  selectedCID = {};
	  selectedSeq = 0;
	  reason = Zquic::MigrationReason::None;
	  ZuCHECK(zeroCID->chooseMigrationPeerCID(
	      true, selectedCID, selectedSeq, reason) &&
	      !selectedCID && selectedSeq == 0 &&
	      reason == Zquic::MigrationReason::None,
	    "zero-length peer CID migration was rejected");
	}

void testActiveMigrationAPI()
{
  ZuTestScope(testActiveMigrationAPI);

  HubFixture fixture;
  ZiSockAddr localAddr{ZiIP{"127.0.0.1"}, 4433};
  ZiSockAddr oldRemote{ZiIP{"127.0.0.1"}, 50000};
  ZiSockAddr newRemote{ZiIP{"127.0.0.1"}, 50001};
  ZiSockAddr otherRemote{ZiIP{"127.0.0.1"}, 50002};
  Zquic::TrafficSecret rx;
  Zquic::TrafficSecret tx;

  ZmRef<TestLink> link = new TestLink{&fixture.app};
  link->initClientPath(localAddr, oldRemote);
  link->setRuntimeCIDs(
    Zquic::CxnID{"localA00"}, Zquic::CxnID{"peerA000"});
  link->installAppDataKeys(rx, tx, Zquic::CxnID{"localA00"});
  Zquic::TransportParams peerParams;
  peerParams.disableActiveMigration = false;
  link->setPeerTransportParams(peerParams);
  ZuCHECK(link->addPeerCID(
      Zquic::CxnID{"peerA001"}, 1,
      Zquic::ResetToken{"0123456789abcdef"}),
    "active migration spare CID setup failed");

  Zquic::MigrationParams params;
  params.local = localAddr;
  params.remote = newRemote;
  Zquic::MigrationReason::T reason = Zquic::MigrationReason::None;
  ZuCHECK(link->prepareActiveMigration(params, reason) &&
      reason == Zquic::MigrationReason::None,
    "active migration preparation failed");
  Zquic::PathInfo candidate = link->candidatePathInfo();
  ZuCHECK(link->migrationActive() &&
      candidate.remote == newRemote &&
      candidate.peerCIDSequence == 1 &&
      candidate.migrationState == Zquic::MigrationState::Requested,
    "active migration candidate snapshot mismatch");

  Zquic::MigrationParams second = params;
  second.remote = otherRemote;
  reason = Zquic::MigrationReason::None;
  ZuCHECK(!link->prepareActiveMigration(second, reason) &&
      reason == Zquic::MigrationReason::Validation &&
      link->candidatePathInfo().remote == newRemote,
    "concurrent active migration request replaced candidate");

  ZuCHECK(link->activateActiveMigration() &&
      link->migrationStartedCount == 1 &&
      link->candidatePathInfo().migrationState ==
	Zquic::MigrationState::Validating,
    "active migration activation callback/state mismatch");
  Zquic::Frame challenge;
  ZuCHECK(link->buildActiveMigChallenge(challenge) &&
      challenge.type == Zquic::FrameType::PathChallenge &&
      challenge.payload.length() == Zquic::PathChallenge::Length,
    "active migration PATH_CHALLENGE was not built");
  ZuCHECK(link->migrationActive(),
    "active migration candidate cleared after PATH_CHALLENGE build");
  uint8_t response[Zquic::PathChallenge::Length];
  ZuBSpan challengeData = link->validatingChallenge();
  ZuCHECK(challengeData.length() == sizeof(response),
    "active migration challenge snapshot was empty");
  if (challengeData.length() != sizeof(response)) return;
  for (unsigned i = 0; i < sizeof(response); ++i)
    response[i] = challengeData[i];
  ZuCHECK(link->pathResponse(ZuBSpan{response, sizeof(response)}),
    "active migration matching PATH_RESPONSE was rejected");
  ZuCHECK(!link->migrationActive(),
    "active migration candidate was not cleared after promotion");
  ZuCHECK(link->migrationPromotedCount == 1,
    "active migration promotion callback was not emitted");
  ZuCHECK(link->pathUpdateCount == 1,
    "active migration path update callback was not emitted");
  ZuCHECK(link->migrationCallbackOrder == "SUP",
    "active migration promotion callback order mismatch");
#ifdef Zquic_DEBUG
  Zquic::MigrationDiag activeDiag = link->diag().tx.migration;
  ZuCHECK(activeDiag.requested == 2 &&
      activeDiag.started == 1 &&
      activeDiag.promoted == 1 &&
      activeDiag.policyReject == 0 &&
      activeDiag.noPeerCID == 0,
    "active migration diagnostic counters mismatch");
#endif

  ZmRef<TestLink> disabled = new TestLink{&fixture.app};
  disabled->initClientPath(localAddr, oldRemote);
  disabled->setRuntimeCIDs(
    Zquic::CxnID{"localB00"}, Zquic::CxnID{"peerB000"});
  disabled->installAppDataKeys(rx, tx, Zquic::CxnID{"localB00"});
  peerParams.disableActiveMigration = true;
  disabled->setPeerTransportParams(peerParams);
  ZuCHECK(disabled->addPeerCID(
      Zquic::CxnID{"peerB001"}, 1,
      Zquic::ResetToken{"1123456789abcdef"}),
    "disabled active migration spare CID setup failed");
  reason = Zquic::MigrationReason::None;
  ZuCHECK(!disabled->prepareActiveMigration(params, reason) &&
      reason == Zquic::MigrationReason::PeerDisabled &&
      !disabled->migrationActive(),
    "active migration ignored peer disable_active_migration");
#ifdef Zquic_DEBUG
  Zquic::MigrationDiag disabledMigrationDiag =
    disabled->diag().tx.migration;
  ZuCHECK(disabledMigrationDiag.requested == 1 &&
      disabledMigrationDiag.policyReject == 1 &&
      disabledMigrationDiag.started == 0,
    "peer-disabled migration diagnostic counters mismatch");
#endif

  ZmRef<TestLink> noSpare = new TestLink{&fixture.app};
  noSpare->initClientPath(localAddr, oldRemote);
  noSpare->setRuntimeCIDs(
    Zquic::CxnID{"localC00"}, Zquic::CxnID{"peerC000"});
  noSpare->installAppDataKeys(rx, tx, Zquic::CxnID{"localC00"});
  peerParams.disableActiveMigration = false;
  noSpare->setPeerTransportParams(peerParams);
  reason = Zquic::MigrationReason::None;
  ZuCHECK(!noSpare->prepareActiveMigration(params, reason) &&
      reason == Zquic::MigrationReason::NoPeerCID &&
      !noSpare->migrationActive() &&
      noSpare->migrationStartedCount == 0,
    "active migration without spare CID advanced to validation");
#ifdef Zquic_DEBUG
  Zquic::MigrationDiag noSpareDiag = noSpare->diag().tx.migration;
  ZuCHECK(noSpareDiag.requested == 1 &&
      noSpareDiag.started == 1 &&
      noSpareDiag.noPeerCID == 1 &&
      noSpareDiag.failed == 1 &&
      noSpareDiag.promoted == 0,
    "no-CID migration diagnostic counters mismatch");
#endif
}

void testPreferredMigration()
{
  ZuTestScope(testPreferredMigration);

  HubFixture fixture;
  ZiSockAddr local{ZiIP{"127.0.0.1"}, 4433};
  ZiSockAddr original{ZiIP{"127.0.0.1"}, 50000};
  ZiSockAddr preferred{ZiIP{"127.0.0.1"}, 50001};
  Zquic::TrafficSecret rx, tx;
  ZmRef<TestLink> link = new TestLink{&fixture.app};
  link->initClientPath(local, original);
  link->setRuntimeCIDs(Zquic::CxnID{"localP00"}, Zquic::CxnID{"peerP000"});
  link->installAppDataKeys(rx, tx, Zquic::CxnID{"localP00"});
  Zquic::TransportParams peerParams;
  peerParams.disableActiveMigration = true;
  link->setPeerTransportParams(peerParams);
  ZuCHECK(link->addPeerCID(Zquic::CxnID{"peerP002"}, 2,
      Zquic::ResetToken{"2123456789abcdef"}) &&
      link->addPeerCID(Zquic::CxnID{"peerP001"}, 1,
      Zquic::ResetToken{"1123456789abcdef"}),
    "preferred migration CID setup failed");
  Zquic::MigrationParams params;
  params.local = local;
  params.remote = preferred;
  Zquic::MigrationReason::T reason;
  ZuCHECK(link->prepareActiveMigration(params, reason, true) &&
      link->candidatePathInfo().peerCIDSequence == 1 &&
      link->activateActiveMigration(),
    "preferred migration did not select sequence 1 with active migration disabled");
  Zquic::PathData response{link->validatingChallenge()};
  response[0] ^= 1;
  link->pathResponseRx(response);
  ZuCHECK(link->migrationActive() && !link->migrationPromotedCount,
    "mismatched response promoted the preferred path");
  link->pathTimeout();
  ZuCHECK(!link->migrationActive() && link->migrationFailureCount == 1 &&
      link->lastMigration.active.remote == original,
    "preferred validation failure did not retain the original path");
  ZuCHECK(link->prepareActiveMigration(params, reason, true) &&
      link->candidatePathInfo().peerCIDSequence == 1 &&
      link->activateActiveMigration(),
    "preferred CID reservation leaked after validation timeout");
  response = Zquic::PathData{link->validatingChallenge()};
  link->pathResponseRx(response);
  ZuCHECK(!link->migrationActive() && link->migrationPromotedCount == 1 &&
      link->lastPathRemote == preferred,
    "preferred path was not promoted after a matching response");
}

void testCxnIDMigrationOverlap()
{
  ZuTestScope(testCxnIDMigrationOverlap);

  HubFixture fixture;
  fixture.app.activeMigration(true);
  ZiSockAddr localAddr{ZiIP{"127.0.0.1"}, 4433};
  ZiSockAddr oldRemote{ZiIP{"127.0.0.1"}, 50000};
  ZiSockAddr newRemote{ZiIP{"127.0.0.1"}, 50001};
  ZmRef<TestLink> link = new TestLink{&fixture.app};
  link->initClientPath(localAddr, oldRemote);
  link->setRuntimeCIDs(
    Zquic::CxnID{"localA00"}, Zquic::CxnID{"peerA000"});
  Zquic::TrafficSecret rx;
  Zquic::TrafficSecret tx;
  link->installAppDataKeys(rx, tx, Zquic::CxnID{"localA00"});
  Zquic::TransportParams peerParams;
  link->setPeerTransportParams(peerParams);
  ZuCHECK(link->addPeerCID(
      Zquic::CxnID{"peerA001"}, 1,
      Zquic::ResetToken{"0123456789abcdef"}),
    "migration-overlap spare CID setup failed");

  fixture.app.defer();
  fixture.app.enterTx();
  Zquic::MigrationParams params;
  params.local = localAddr;
  params.remote = newRemote;
  Zquic::MigrationReason::T reason = Zquic::MigrationReason::None;
  ZuCHECK(link->prepareActiveMigration(params, reason) &&
      fixture.app.rxPending() && !fixture.app.txPending(),
    "migration-overlap selection was not posted to Rx");
  fixture.app.runRx();
  ZuCHECK(!fixture.app.rxPending() && fixture.app.txPending(),
    "migration-overlap selection was not returned to Tx");
  fixture.app.runTx();
  ZuCHECK(!fixture.app.txPending() &&
      link->candidatePathInfo().peerCIDSequence == 1,
    "migration-overlap selected the wrong peer CID");
  ZuCHECK(link->activateActiveMigration(),
    "migration-overlap candidate activation failed");
  Zquic::Frame challenge;
  ZuCHECK(link->buildActiveMigChallenge(challenge),
    "migration-overlap challenge setup failed");

  fixture.app.enterRx();
  Zquic::Frame replacement;
  replacement.type = Zquic::FrameType::NewCxnID;
  replacement.value = 2;
  replacement.offset = 2;
  replacement.length = 8;
  replacement.payload = "peerA002";
  replacement.resetToken = Zquic::ResetToken{"1234567890abcdef"};
  ZuCHECK(link->receiveNewCxnID(replacement) &&
      !link->peerCIDActive(1) && link->peerCIDActive(2),
    "migration-overlap did not retire and replace the selected CID");

  uint8_t response[Zquic::PathChallenge::Length];
  ZuBSpan challengeData = link->validatingChallenge();
  ZuCHECK(challengeData.length() == sizeof(response),
    "migration-overlap challenge snapshot was empty");
  if (challengeData.length() != sizeof(response)) return;
  memcpy(response, challengeData.data(), sizeof(response));
  fixture.app.enterTx();
  ZuCHECK(link->pathResponse(ZuBSpan{response, sizeof(response)}) &&
      fixture.app.rxPending() && !link->migrationPromotedCount,
    "migration-overlap promotion did not defer CID binding to Rx");
  fixture.app.runRx();
  ZuCHECK(fixture.app.txPending() && !link->migrationPromotedCount,
    "migration-overlap CID rejection was not returned to Tx");
  fixture.app.runTx();
  ZuCHECK(!link->migrationActive() && link->migrationFailureCount == 1 &&
      !link->migrationPromotedCount && !link->pathUpdateCount &&
      link->activeTxPeerCID() == Zquic::CxnID{"peerA000"},
    "retired migration CID was promoted or reverted the active CID");
  ZuCHECK(fixture.app.rxPending(),
    "retired migration CID reservation was not released on Rx");
  fixture.app.runRx();

  Zquic::CxnID selected;
  uint64_t sequence = 0;
  reason = Zquic::MigrationReason::None;
  ZuCHECK(link->chooseMigrationPeerCID(
      true, selected, sequence, reason) &&
      selected == Zquic::CxnID{"peerA002"} && sequence == 2,
    "replacement peer CID was not selectable after overlap failure");
}

void testMigrationQLogEvents()
{
  ZuTestScope(testMigrationQLogEvents);

#ifdef Zquic_DEBUG
  Zi::Path qlogPath = testPath("ZquicAPIMigrationQLog.sqlog");
  ZiFile::remove(qlogPath);
  ZquicLogParams qlogParams;
  qlogParams.enabled(true).path(qlogPath).ringSize(1<<15);

  HubFixture fixture;
  ZuCHECK(ZquicLogger::init(
      fixture.app.qlogTrace(), qlogParams, Zquic::Vantage::Unknown),
    "migration runtime qlog init failed");
  ZquicLogger::start();

  ZiSockAddr localAddr{ZiIP{"127.0.0.1"}, 4433};
  ZiSockAddr localAddr2{ZiIP{"127.0.0.1"}, 4434};
  ZiSockAddr oldRemote{ZiIP{"127.0.0.1"}, 50000};
  ZiSockAddr activeRemote{ZiIP{"127.0.0.1"}, 50001};
  ZiSockAddr passiveRemote{ZiIP{"127.0.0.1"}, 50002};
  ZiSockAddr timeoutRemote{ZiIP{"127.0.0.1"}, 50003};
  ZiSockAddr mismatchRemote{ZiIP{"127.0.0.1"}, 50004};
  Zquic::TrafficSecret rx;
  Zquic::TrafficSecret tx;
  Zquic::TransportParams peerParams;
  peerParams.disableActiveMigration = false;

  auto initActive = [
    &fixture, &localAddr, &oldRemote, &rx, &tx, &peerParams
  ](ZuCSpan localCID, ZuCSpan peerCID, ZuCSpan spareCID, ZuCSpan token) {
    ZmRef<TestLink> link = new TestLink{&fixture.app};
    link->initClientPath(localAddr, oldRemote);
    link->setRuntimeCIDs(Zquic::CxnID{localCID}, Zquic::CxnID{peerCID});
    link->installAppDataKeys(rx, tx, Zquic::CxnID{localCID});
    link->setPeerTransportParams(peerParams);
    if (spareCID)
      link->addPeerCID(Zquic::CxnID{spareCID}, 1, Zquic::ResetToken{token});
    return link;
  };

  ZmRef<TestLink> active =
    initActive("localA00", "peerA000", "peerA001", "0123456789abcdef");
  Zquic::MigrationParams params;
  params.local = localAddr;
  params.remote = activeRemote;
  Zquic::MigrationReason::T reason = Zquic::MigrationReason::None;
  ZuCHECK(active->prepareActiveMigration(params, reason) &&
      active->activateActiveMigration(),
    "qlog active migration setup failed");
  Zquic::Frame frame;
  ZuCHECK(active->buildActiveMigChallenge(frame),
    "qlog active migration challenge build failed");
  uint8_t response[Zquic::PathChallenge::Length]{};
  ZuBSpan challenge = active->validatingChallenge();
  memcpy(response, challenge.data(), sizeof(response));
  ZuCHECK(active->pathResponse(ZuBSpan{response, sizeof(response)}),
    "qlog active migration response failed");

  fixture.app.migrationMode(Zquic::MigrationMode::Passive);
  ZmRef<TestLink> passive = new TestLink{&fixture.app, true};
  passive->initServerPath(localAddr, oldRemote);
  passive->observePathRx(localAddr, passiveRemote);
  challenge = passive->validatingChallenge();
  memcpy(response, challenge.data(), sizeof(response));
  ZuCHECK(passive->pathResponse(ZuBSpan{response, sizeof(response)}),
    "qlog passive migration response failed");

  ZmRef<TestLink> timeout = new TestLink{&fixture.app, true};
  timeout->initServerPath(localAddr, oldRemote);
  timeout->observePath(localAddr, timeoutRemote);
  timeout->pathTimeout();

  ZmRef<TestLink> mismatch = new TestLink{&fixture.app, true};
  mismatch->initServerPath(localAddr, oldRemote);
  mismatch->observePath(localAddr, mismatchRemote);
  challenge = mismatch->validatingChallenge();
  memcpy(response, challenge.data(), sizeof(response));
  response[0] ^= 1;
  ZuCHECK(!mismatch->pathResponse(ZuBSpan{response, sizeof(response)}),
    "qlog mismatch PATH_RESPONSE was accepted");
  mismatch->pathTimeout();

  ZmRef<TestLink> disabled =
    initActive("localB00", "peerB000", "peerB001", "1123456789abcdef");
  peerParams.disableActiveMigration = true;
  disabled->setPeerTransportParams(peerParams);
  reason = Zquic::MigrationReason::None;
  ZuCHECK(!disabled->prepareActiveMigration(params, reason) &&
      reason == Zquic::MigrationReason::PeerDisabled,
    "qlog peer-disabled migration was accepted");

  peerParams.disableActiveMigration = false;
  ZmRef<TestLink> noCID = initActive("localC00", "peerC000", {}, {});
  reason = Zquic::MigrationReason::None;
  ZuCHECK(!noCID->prepareActiveMigration(params, reason) &&
      reason == Zquic::MigrationReason::NoPeerCID,
    "qlog no-CID migration was accepted");

  ZmRef<TestLink> rebind =
    initActive("localD00", "peerD000", "peerD001", "2123456789abcdef");
  Zquic::MigrationParams rebindParams;
  rebindParams.local = localAddr2;
  rebindParams.remote = oldRemote;
  rebindParams.rebindLocal = true;
  rebindParams.closeOnFailure = true;
  reason = Zquic::MigrationReason::None;
  ZuCHECK(rebind->prepareActiveMigration(rebindParams, reason),
    "qlog rebind migration setup failed");
  rebind->activeRebindStart();
  rebind->activeRebindFail(Zquic::MigrationReason::Endpoint);
  // This fixture does not run the normal link disconnect sequence. Its
  // close-on-failure timer must be cancelled before the link is released.
  rebind->recoveryStop();
  ZuCHECK(!rebind->timersActive(), "migration log fixture retained a timer");

  closeQLog(fixture.app.qlogTrace());
  ZquicLogger::stop();
  ZquicLogDiag diag = ZquicLogger::diag();
  ZquicLogger::final(fixture.app.qlogTrace());

  ZuCHECK(diag.recordsEnqueued >= 24,
    "migration runtime qlog enqueue mismatch");
  ZuCHECK(diag.recordsWritten >= 25,
    "migration runtime qlog write mismatch");
  ZuCHECK(diag.writerFailures == 0,
    "migration runtime qlog writer failure");

  ZtString<> qlog = readFile(qlogPath);
  ZuCHECK(qlog, "migration runtime qlog output missing");
  ZuCHECK(parseJSONSeq(qlog) >= 25,
    "migration runtime qlog JSON-SEQ parse failed");
  ZuCHECK(qlog.find<"zquic:migration_updated">() >= 0,
    "runtime migration qlog event missing");
  ZuCHECK(qlog.find<"\"action\":\"requested\"">() >= 0 &&
      qlog.find<"\"action\":\"started\"">() >= 0 &&
      qlog.find<"\"action\":\"cid_selected\"">() >= 0 &&
      qlog.find<"\"action\":\"challenge_queued\"">() >= 0 &&
      qlog.find<"\"action\":\"response_matched\"">() >= 0 &&
      qlog.find<"\"action\":\"response_mismatch\"">() >= 0 &&
      qlog.find<"\"action\":\"promoted\"">() >= 0 &&
      qlog.find<"\"action\":\"abandoned\"">() >= 0 &&
      qlog.find<"\"action\":\"failed\"">() >= 0 &&
      qlog.find<"\"action\":\"rejected\"">() >= 0 &&
      qlog.find<"\"action\":\"cid_unavailable\"">() >= 0 &&
      qlog.find<"\"action\":\"rebind_start\"">() >= 0 &&
      qlog.find<"\"action\":\"rebind_fail\"">() >= 0 &&
      qlog.find<"\"action\":\"closed\"">() >= 0,
    "runtime migration qlog action coverage missing");
  ZuCHECK(qlog.find<"\"reason\":\"peer_disabled\"">() >= 0 &&
      qlog.find<"\"reason\":\"no_peer_cid\"">() >= 0 &&
      qlog.find<"\"reason\":\"validation\"">() >= 0 &&
      qlog.find<"\"reason\":\"timeout\"">() >= 0 &&
      qlog.find<"\"reason\":\"endpoint\"">() >= 0,
    "runtime migration qlog reason coverage missing");
  ZuCHECK(qlog.find<"\"local_rebind\":true">() >= 0 &&
      qlog.find<"\"close_on_failure\":true">() >= 0 &&
      qlog.find<"\"candidate_remote\"">() >= 0 &&
      qlog.find<"\"peer_cid_sequence\":1">() >= 0,
    "runtime migration qlog correlation fields missing");

  auto ordered = [&qlog](ZuCSpan a0, ZuCSpan a1, ZuCSpan a2 = {},
      ZuCSpan a3 = {}, ZuCSpan a4 = {}, ZuCSpan a5 = {}) {
    unsigned off = 0;
    auto next = [&qlog, &off](ZuCSpan action) {
      if (!action) return true;
      int pos = ZuCSpan{qlog.data() + off, qlog.length() - off}.find(action);
      if (pos < 0) return false;
      off += unsigned(pos) + action.length();
      return true;
    };
    return next(a0) && next(a1) && next(a2) && next(a3) &&
      next(a4) && next(a5);
  };
  ZuCHECK(ordered(
      "\"action\":\"requested\"", "\"action\":\"started\"",
      "\"action\":\"cid_selected\"", "\"action\":\"challenge_queued\"",
      "\"action\":\"response_matched\"", "\"action\":\"promoted\""),
    "active success qlog stream order mismatch");
  ZuCHECK(ordered(
      "\"action\":\"response_mismatch\"", "\"action\":\"abandoned\"",
      "\"action\":\"failed\""),
    "validation failure qlog stream order mismatch");
  ZuCHECK(ordered(
      "\"action\":\"cid_unavailable\"", "\"action\":\"failed\""),
    "no-CID qlog stream order mismatch");
  ZuCHECK(ordered(
      "\"action\":\"rebind_start\"", "\"action\":\"rebind_fail\"",
      "\"action\":\"failed\"", "\"action\":\"closed\""),
    "rebind failure qlog stream order mismatch");
#endif
}

void testInitValidate()
{
  ZuTestScope(testInitValidate);

  ZiLog::init("ZquicAPITest");
  ZiLog::level(0);
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();

  {
    unsigned errors = 0;
    HubApp hub;
    ZuCHECK(!hub.init(
	Zquic::HubParams(nullptr, "1", "2")
	  .errorFn(Zquic::ErrorFn{[&errors](ZeException) { ++errors; }})),
      "null multiplexer init unexpectedly succeeded");
    ZuCHECK(errors == 1, "null multiplexer error callback mismatch");
  }
  {
    unsigned errors = 0;
    ClientApp app;
    ZuCHECK(!app.init(
	Zquic::ClientParams(nullptr, "1", "2")
	  .certPath("client.pem")
	  .errorFn(Zquic::ErrorFn{[&errors](ZeException) { ++errors; }})),
      "client cert/key mismatch unexpectedly succeeded");
    ZuCHECK(errors == 1, "client cert/key error callback mismatch");
  }

  ZiMultiplex mx(
      ZiMxParams()
	.scheduler([](auto &s) {
	  s.nThreads(5)
	    .thread(1, [](auto &t) { t.isolated(1); })
	    .thread(2, [](auto &t) { t.isolated(1); })
	    .thread(3, [](auto &t) { t.isolated(1); })
	    .thread(4, [](auto &t) { t.isolated(1); }); })
	.rxThread(1).txThread(2));

  {
    unsigned errors = 0;
    HubApp app;
    ZuCHECK(!app.init(
	Zquic::HubParams(&mx, "9", "2")
	  .errorFn(Zquic::ErrorFn{[&errors](ZeException) { ++errors; }})),
      "invalid Rx thread unexpectedly succeeded");
    ZuCHECK(errors == 1, "invalid Rx thread error callback mismatch");
  }
  {
    unsigned errors = 0;
    HubApp app;
    ZuCHECK(!app.init(
	Zquic::HubParams(&mx, "1", "9")
	  .errorFn(Zquic::ErrorFn{[&errors](ZeException) { ++errors; }})),
      "invalid Tx thread unexpectedly succeeded");
    ZuCHECK(errors == 1, "invalid Tx thread error callback mismatch");
  }
  {
    unsigned errors = 0;
    HubApp app;
    ZuCHECK(!app.init(
	Zquic::HubParams(&mx, "3", "3")
	  .errorFn(Zquic::ErrorFn{[&errors](ZeException) { ++errors; }})),
      "same Rx/Tx thread unexpectedly succeeded");
    ZuCHECK(errors == 1, "same Rx/Tx thread error callback mismatch");
  }
  {
    unsigned errors = 0;
    HubApp app;
    ZuCHECK(!app.init(
	Zquic::HubParams(&mx, "3", "4")
	  .asyncThread("1")
	  .errorFn(Zquic::ErrorFn{[&errors](ZeException) { ++errors; }})),
      "I/O async thread unexpectedly succeeded");
    ZuCHECK(errors == 1, "I/O async thread error callback mismatch");
  }
  {
    unsigned errors = 0;
    ServerApp app;
    ZuCHECK(!app.init(
	Zquic::ServerParams(&mx, "3", "4")
	  .certPath("server.pem")
	  .errorFn(Zquic::ErrorFn{[&errors](ZeException) { ++errors; }})),
      "server cert/key mismatch unexpectedly succeeded");
    ZuCHECK(errors == 1, "server cert/key error callback mismatch");
  }
  {
    ClientApp app;
    bool ok = app.init(
      Zquic::ClientParams(&mx, "3", "4").alpn(ZuSpan<ZuCSpan>{"h3"}));
    ZuCHECK(ok, "valid client init failed");
    auto protocols = app.alpn_list();
    ZuCHECK(app.alpn_count() == 1 && protocols.length() == 1 && protocols[0] == "h3",
      "client ALPN list mismatch");
    if (ok) app.final();
  }
  {
    ServerApp app;
    bool ok = app.init(
      Zquic::ServerParams(&mx, {}, {})
	.certPath("server.pem")
	.keyPath("server.key")
	.alpn(ZuSpan<ZuCSpan>{"zquic-test"}));
    ZuCHECK(ok, "valid server default-thread init failed");
    if (ok) app.final();
  }

  ZiLog::stop();
}

static bool hashConfig(
  ZuCSpan id, unsigned tables, unsigned bits, double loadFactor)
{
  bool ok = false;
  Ztc::HashMgr::capture(
    [id](Ztc::Hash *hash) { return hash->telKey().p<0>() == id; },
    [tables, bits, loadFactor, &ok](const auto &rows) {
      ok = rows.length() == tables;
      for (const auto &row : rows)
	ok &= row.bits == bits && row.loadFactor == loadFactor &&
	  !row.cBits && !row.resized; // owner-local tables retain ZmNoLock
    });
  return ok;
}

void testHashConfig()
{
  ZuTestScopeRT(testHashConfig);
  struct Config {
    const char *id;
    unsigned tables, bits, tunedBits, tunedCBits;
  };
  const Config configs[] = {
    {"Zquic.Server.Links", 1, 5, 7, 2},
    {"Zquic.Endpoint.CxnRouter", 1, 5, 6, 1},
    {"Zquic.Stream.ObjectHash", 2, 8, 6, 2},
    {"Zquic.Stream.TxHash", 2, 8, 5, 1},
    {"Zquic.Pkt.OutstandingFlow", 1, 3, 5, 1},
    {"Zquic.Pkt.OutstandingExact", 1, 4, 6, 2},
    {"Zquic.Pkt.AckdFrame", 1, 8, 7, 1},
    {"Zquic.StreamScheduler", 1, 4, 6, 0}
  };
  for (bool tuned : {false, true}) {
    // Construct the recovery owners before configuration: their hashes are lazy.
    Zquic::OutstandingFrames outstanding;
    Zquic::RetransmitQueue pending;
    ZQUIC_CHECK_RT(hashConfig("Zquic.Pkt.OutstandingFlow", 0, 0, 0) &&
	hashConfig("Zquic.Pkt.OutstandingExact", 0, 0, 0) &&
	hashConfig("Zquic.Pkt.AckdFrame", 0, 0, 0),
      "recovery indexes allocated before use");
    if (tuned)
      for (const auto &config : configs)
	ZmHashMgr::init(config.id, ZmHashParams{}.bits(config.tunedBits)
	  .loadFactor(2).cBits(config.tunedCBits));

    ZiMultiplex mx{HubFixture::mxParams()};
    ClientShapeApp clientApp;
    ServerShapeApp serverApp;
    bool clientOK = clientApp.init(
      Zquic::ClientParams(&mx, "3", "4").alpn(ZuSpan<ZuCSpan>{"h3"}));
    bool serverOK = serverApp.init(Zquic::ServerParams(&mx, "3", "4")
      .certPath("server.pem").keyPath("server.key")
      .alpn(ZuSpan<ZuCSpan>{"h3"}));
    ZQUIC_CHECK_RT(clientOK && serverOK, "hash fixture initialization failed");
    {
      ZmRef<ClientShapeLink> client = new ClientShapeLink{&clientApp};
      ZmRef<ServerShapeLink> server = new ServerShapeLink{&serverApp};
      Zquic::StreamScheduler scheduler{ZmHashParams{}.bits(4)};
      bool populated = true;
      for (unsigned i = 0; i < 4; ++i) {
	Zquic::SentPkt packet;
	Zquic::TxRange range;
	range.length = 1;
	populated &= packet.addFrame(Zquic::SentFrameRef::stream(i * 4, range, true));
	outstanding.add(packet);
	populated &= pending.push(Zquic::SentFrameRef::crypto(i, 1));
	populated &= scheduler.add(i);
      }
      ZQUIC_CHECK_RT(populated, "hash fixture population failed");
      for (const auto &config : configs)
	ZQUIC_CHECK_RT(hashConfig(config.id, config.tables,
	    tuned ? config.tunedBits : config.bits, tuned ? 2 : 1),
	  "hash readback mismatch: ", config.id, " tuned=", tuned);
      outstanding.clear();
      pending.clear();
      scheduler.clear();
      ZQUIC_CHECK_RT(hashConfig("Zquic.Pkt.AckdFrame", 0, 0, 0),
	"retransmit index retained after clear");
      ZQUIC_CHECK_RT(pending.push(Zquic::SentFrameRef::crypto(9, 1)) &&
	  hashConfig("Zquic.Pkt.AckdFrame", 1, tuned ? 7 : 8, tuned ? 2 : 1),
	"recreated retransmit index lost named configuration");
    }
    if (clientOK) clientApp.final();
    if (serverOK) serverApp.final();
  }
}

static void testServerAddrs()
{
  ZuTestScope(testServerAddrs);
  using Addrs = Zquic::ServerAddrs;
  auto a4 = [](const char *ip, unsigned port = 443) {
    return ZiSockAddr{ZiIP{ip}, uint16_t(port)};
  };
  Addrs empty;
  ZuCheck(!empty && empty.normalize());
  auto valid4 = Addrs{a4("127.0.0.1"), a4("127.0.0.1", 8443), {}, {}};
  ZuCheck(valid4.normalize() && valid4 && valid4.preferred4.port() == 8443);
  auto invalid = [&valid4](auto change) {
    auto a = valid4;
    change(a);
    return !a.normalize();
  };
  ZuCheck(invalid([](auto &a) { a.original4.null(); }));
  ZuCheck(invalid([](auto &a) { a.preferred4.null(); }));
  ZuCheck(invalid([](auto &a) { a.preferred4 = a.original4; }));
  ZuCheck(invalid([&a4](auto &a) { a.original4 = a4("0.0.0.0"); }));
  ZuCheck(invalid([&a4](auto &a) { a.preferred4 = a4("224.0.0.1"); }));
  ZuCheck(invalid([&a4](auto &a) { a.preferred4 = a4("255.255.255.255"); }));
  ZuCheck(invalid([&a4](auto &a) { a.preferred4 = a4("127.0.0.2", 0); }));
  ZuCheck(invalid([&a4](auto &a) { a.preferred4 = a4("::1"); }));
  ZuCheck(invalid([&a4](auto &a) { a.preferred6 = a4("::1", 8443); }));
  Addrs both{a4("127.0.0.1"), a4("127.0.0.1"),
    a4("::1"), a4("::1", 8443)};
  ZuCheck(both.normalize() && !both.preferred4 && both.preferred6);
  Addrs v6{{}, {}, a4("::1"), a4("::1", 8443)};
  ZuCheck(v6.normalize());
  auto invalid6 = [&v6, &a4](const char *ip) {
    auto a = v6;
    a.preferred6 = a4(ip, 8443);
    return !a.normalize();
  };
  ZuCheck(invalid6("::"));
  ZuCheck(invalid6("ff02::1"));
  ZuCheck(invalid6("fe80::1"));
  ZuCheck(invalid6("fec0::1"));
  ZuCheck(invalid6("::ffff:127.0.0.1"));
  auto scoped = v6;
  scoped.preferred6.sin6().sin6_scope_id = 1;
  ZuCheck(!scoped.normalize());
  auto different = valid4;
  different.preferred4 = a4("127.0.0.2");
  ZuCheck(different.normalize() && different.preferred4.port() == 443);
  ZuCheck(Addrs::valid(a4("fd00::1"), true));
}

void testPreferredPathProof()
{
  ZuTestScope(testPreferredPathProof);
  HubFixture fixture;
  fixture.app.migrationMode(Zquic::MigrationMode::Disabled);
  ZiSockAddr original{ZiIP{"127.0.0.1"}, 443};
  ZiSockAddr preferred{ZiIP{"127.0.0.1"}, 8443};
  ZiSockAddr competing{ZiIP{"127.0.0.2"}, 8443};
  ZiSockAddr preferred6{ZiIP{"fd00::1"}, 8443};
  ZiSockAddr remote6{ZiIP{"fd00::3"}, 50000};
  ZiSockAddr remote{ZiIP{"127.0.0.3"}, 50000};
  for (unsigned proofFirst = 0; proofFirst != 2; ++proofFirst) {
    ZmRef<TestLink> link = new TestLink{&fixture.app, true};
    link->initServerPath(original, remote);
    link->advertisePreferred(preferred, preferred6);
    link->observePreferred(preferred, remote, true, false, 0);
    ZuCheck(link->migrationActive() && link->activeLocal() == original);
    Zquic::PathData challenge{link->validatingChallenge()};
    uint64_t attempt = link->migrationAttemptForTest();
    auto candidate = link->candidatePathInfo();
    link->observePreferred(competing, remote, true, true, 1);
    ZuCheck(link->candidatePathInfo().local == preferred &&
      link->validatingChallenge() == challenge);
    // Both families were advertised for this same connection. The second
    // candidate cannot replace the first, add credit, or qualify its input.
    link->observePreferred(preferred6, remote6, true, true, 2);
    ZuCheck(link->candidatePathInfo().local == preferred &&
      link->candidatePathInfo().remote == remote &&
      link->candidatePathInfo().bytesRx == candidate.bytesRx &&
      link->migrationAttemptForTest() == attempt &&
      link->validatingChallenge() == challenge);
    if (proofFirst) {
      ZuCheck(link->pathResponse(challenge));
      ZuCheck(!link->migrationPromotedCount && link->activeLocal() == original);
      link->observePreferred(preferred, remote, false, true, 0);
      ZuCheck(!link->migrationPromotedCount);
      // ACK-only is non-probing and qualifies even though it is not
      // acknowledgment-eliciting. Packet zero qualifies on an empty tracker.
      link->observePreferred(preferred, remote, true, true, 0);
    } else {
      link->observePreferred(preferred, remote, true, true, 0);
      ZuCheck(!link->migrationPromotedCount && link->activeLocal() == original);
      ZuCheck(link->pathResponse(challenge));
    }
    ZuCheck(link->migrationPromotedCount == 1 &&
      link->activeLocal() == preferred && !link->migrationActive());
    link->observePreferred(original, remote, true, true, 2);
    ZuCheck(link->activeLocal() == preferred && !link->migrationActive());
    link->recoveryStop();
  }
  ZmRef<TestLink> timeout = new TestLink{&fixture.app, true};
  timeout->initServerPath(original, remote);
  timeout->advertisePreferred(preferred);
  timeout->observePreferred(preferred, remote, true, false, 0);
  Zquic::PathData stale{timeout->validatingChallenge()};
  timeout->pathTimeout();
  ZuCheck(!timeout->pathResponse(stale) && timeout->activeLocal() == original);
  timeout->recoveryStop();
}

void testPreferredAuthenticatedFreshness()
{
  ZuTestScope(testPreferredAuthenticatedFreshness);
  HubFixture fixture;
  fixture.app.migrationMode(Zquic::MigrationMode::Disabled);
  ZiSockAddr original{ZiIP{"127.0.0.1"}, 443};
  ZiSockAddr preferred{ZiIP{"127.0.0.1"}, 8443};
  ZiSockAddr remote{ZiIP{"127.0.0.3"}, 50000};
  Zquic::CxnID localCID{"fresh000"}, peerCID{"peer0000"};
  uint8_t bytes[32]{};
  Zquic::TrafficSecret secret;
  ZuCheck(Zquic::PktProt::deriveSecret(secret,
    &Ztls_::opensslAes128gcmsha256, ZuBSpan{bytes, sizeof(bytes)}));
  for (bool reordered : {false, true}) {
    ZmRef<TestLink> link = new TestLink{&fixture.app, true};
    link->setRuntimeCIDs(localCID, peerCID);
    ZuCheck(link->installAppDataKeys(secret, secret, localCID));
    link->initServerPath(original, remote);
    link->advertisePreferred(preferred);
    if (reordered)
      ZuCheck(link->authenticatedPing(secret, localCID, 2, original, remote));
    ZuCheck(link->authenticatedPing(
      secret, localCID, reordered ? 1 : 0, preferred, remote));
    ZuCheck(link->migrationActive() && link->activeLocal() == original);
    Zquic::PathData challenge{link->validatingChallenge()};
    ZuCheck(link->pathResponse(challenge));
    ZuCheck((link->migrationPromotedCount == 1) == !reordered);
    if (reordered) {
      ZuCheck(link->authenticatedPing(secret, localCID, 1, preferred, remote));
      ZuCheck(!link->migrationPromotedCount);
      ZuCheck(link->authenticatedPing(secret, localCID, 3, preferred, remote));
      ZuCheck(link->migrationPromotedCount == 1);
    }
    link->recoveryStop();
  }
}

void testPreferredRecoveryEpoch()
{
  ZuTestScope(testPreferredRecoveryEpoch);
  ZiSockAddr original{ZiIP{"127.0.0.1"}, 443};
  ZiSockAddr preferred{ZiIP{"127.0.0.1"}, 8443};
  ZiSockAddr remote{ZiIP{"127.0.0.3"}, 50000};
  for (bool differentIP : {false, true}) {
    HubFixture fixture{true, true, false, Zquic::BufSize};
    ZmRef<TestLink> link = new TestLink{&fixture.app, true};
    ZiSockAddr destination = differentIP ?
      ZiSockAddr{ZiIP{"127.0.0.2"}, 8443} : preferred;
    ZuCheck(link->ackEstablished());
    link->initServerPath(original, remote);
    link->advertisePreferred(destination);
    link->observePreferred(original, remote, true, true, 0);
    ZuCheck(link->epochPacket(1, Zquic::EcnMark::ECT0));
    auto stalePMTUD = link->pmtudTimeoutFn();
    link->observePreferred(destination, remote, true, true, 1);
    Zquic::PathData challenge{link->validatingChallenge()};
    ZuCheck(link->pathResponse(challenge));
    ZuCheck(link->recoveryDiag().congestionWindow == 12000 &&
      !link->controllerFlight() && !link->activeECN());
    uint64_t initialPTO = link->diag().tx.ptoTimeoutUS;
    ZuCheck(link->epochPacket(2));
    link->epochAck(1, Zquic::AckECN{1, 0, 0});
    ZuCheck(link->controllerFlight() == 100 &&
      link->recoveryDiag().congestionWindow == 12000 &&
      link->diag().tx.ptoTimeoutUS == initialPTO && link->activeECN());
    // A late old-path CE snapshot must never reduce or invalidate the
    // promoted controller, including after ECN testing restarted.
    auto before = link->diag().tx;
    link->epochAck(1, Zquic::AckECN{1, 0, 1});
    ZuCheck(link->controllerFlight() == 100 &&
      link->recoveryDiag().congestionWindow == 12000 &&
      link->diag().tx.ecnValidationFailures == before.ecnValidationFailures);
    ZuCheck(link->beginPMTUD());
    auto probe = link->recoveryPathDiag();
    stalePMTUD();
    ZuCheck(link->recoveryPathDiag().probesExpired == probe.probesExpired);
    auto currentPMTUD = link->pmtudTimeoutFn();
    currentPMTUD();
    ZuCheck(link->recoveryPathDiag().probesExpired == probe.probesExpired + 1);
    link->epochAck(2, Zquic::AckECN{1, 0, 1});
    ZuCheck(!link->controllerFlight() &&
      link->recoveryDiag().congestionWindow == 12100);
    link->recoveryStop();
  }

  // A same-runtime promotion inside local send admission cannot release
  // the new path's flight when that older protected packet is refused.
  {
    HubFixture fixture{true, false, false};
    ZmRef<TestLink> link = new TestLink{&fixture.app, true};
    ZuCheck(link->ackEstablished());
    link->initServerPath(original, remote);
    link->advertisePreferred(preferred);
    link->observePreferred(original, remote, true, true, 0);
    link->observePreferred(preferred, remote, true, true, 1);
    Zquic::PathData challenge{link->validatingChallenge()};
    bool promoted = false;
    ZuCheck(!link->shortCommit([&link, &promoted, challenge](ZmRef<ZiIOBuf>) {
      promoted = link->pathResponse(challenge) && link->epochPacket(1);
      return false;
    }));
    ZuCheck(promoted && link->controllerFlight() == 100 &&
      link->recoveryDiag().congestionWindow == 12000);
    link->recoveryStop();
  }

  // STREAM FIN retirement invokes application completion synchronously.
  // A promotion in that callback must fence the already extracted summary.
  {
    HubFixture fixture{true, false, false};
    ZmRef<TestLink> link = new TestLink{&fixture.app, true};
    ZuCheck(link->ackEstablished());
    link->initServerPath(original, remote);
    link->advertisePreferred(preferred);
    auto stream = link->stream();
    stream->fin();
    uint64_t offset = 0;
    ZuCheck(stream->dequeueFin(offset) && link->epochPacket(1,
      Zquic::EcnMark::NotECT, stream.ptr()));
    link->observePreferred(preferred, remote, true, true, 1);
    Zquic::PathData challenge{link->validatingChallenge()};
    bool promoted = false;
    stream->txCompleteFn = [&link, &promoted, challenge] {
      promoted = link->pathResponse(challenge) && link->epochPacket(2);
    };
    link->epochAck(1);
    ZuCheck(promoted && stream->txCompleteCount == 1 &&
      link->controllerFlight() == 100 &&
      link->recoveryDiag().congestionWindow == 12000);
    stream->txCompleteFn = {};
    link->recoveryStop();
  }
}

void testLocalCIDTxValidity()
{
  ZuTestScope(testLocalCIDTxValidity);
  HubFixture fixture{true, false, false};
  auto &app = fixture.app;
  ZmRef<TestLink> link = new TestLink{&app, true};
  ZuCheck(link->ackEstablished());
  app.defer();
  Zquic::CxnID id{"mirror01"};
  Zquic::ResetToken token{"0123456789abcdef"};
  auto frame = Zquic::ControlFrame::newCxnID(1, 0, id, token);
  ZuCheck(link->addLocalCID(id, 1, token) && app.txPending());
  app.enterTx();
  ZuCheck(!link->cidRetxValid(frame)); // Rx insertion is not a Tx publication.
  app.runTx();
  ZuCheck(link->cidRetxValid(frame) && !app.txPending());
  ZuCheck(link->cidPacket(frame) && link->epochPacket(4));
  link->epochAck(4);
  ZuCheck(link->cidRetxQueued());
  app.enterRx();
  Zquic::Frame retired;
  retired.type = Zquic::FrameType::RetireCxnID;
  retired.value = 1;
  ZuCheck(link->receiveRetireCxnID(retired) && app.txPending());
  app.enterTx();
  ZuCheck(link->cidRetxValid(frame)); // Only the Tx snapshot is read here.
  while (app.txPending()) app.runTx();
  ZuCheck(!link->cidRetxValid(frame) && !link->takeRetx() && !link->cidRetxQueued());

  for (unsigned sequence = 2; sequence <= 65; ++sequence) {
    uint8_t bytes[8]{'m', 'i', 'r', 'r', 'o', 'r', 0, uint8_t(sequence)};
    Zquic::CxnID replacement{ZuBSpan{bytes, sizeof(bytes)}};
    auto next = Zquic::ControlFrame::newCxnID(sequence, 0, replacement, token);
    app.enterRx();
    ZuCheck(link->addLocalCID(replacement, sequence, token));
    while (app.txPending()) app.runTx();
    ZuCheck(link->cidRetxValid(next) && !link->cidRetxValid(frame));
    app.enterRx();
    retired.value = sequence;
    ZuCheck(link->receiveRetireCxnID(retired));
    while (app.txPending()) app.runTx();
    ZuCheck(!link->cidRetxValid(next));
  }

  app.enterRx();
  retired.value = 1;
  ZuCheck(link->receiveRetireCxnID(retired) && !app.txPending());
  retired.value = 66;
  ZuCheck(!link->receiveRetireCxnID(retired));
  ZuCheck(link->addLocalCID(id, 66, token));
  auto stale = Zquic::ControlFrame::newCxnID(66, 0, id, token);
  app.enterTx();
  link->recoveryReset();
  while (app.txPending()) app.runTx();
  ZuCheck(!link->cidRetxValid(stale));
  while (app.rxPending()) app.runRx();
  app.enterTx();
  link->recoveryStop();
  log("# Local CID Tx validity sizeof=", sizeof(ZuArray<Zquic_::LinkCID, Zquic::LocalCIDLimit>),
    " LinkCID sizeof=", sizeof(Zquic_::LinkCID),
    " LinkTxStorage sizeof=", sizeof(Zquic_::LinkTxStorage));
}

void testPacketPathHandoff()
{
  ZuTestScope(testPacketPathHandoff);
  HubFixture fixture{true, false, false};
  auto &app = fixture.app;
  ZmRef<TestLink> link = new TestLink{&app, true};
  ZuCheck(link->ackEstablished());
  ZiSockAddr original{ZiIP{"127.0.0.1"}, 443};
  ZiSockAddr preferred{ZiIP{"127.0.0.1"}, 8443};
  ZiSockAddr remote{ZiIP{"127.0.0.3"}, 50000};
  link->initServerPath(original, remote);
  link->advertisePreferred(preferred);
  app.defer();
  ZmRef<Zquic::RxPkt> packet{app.rxPktAllocator()->allocBuf(nullptr, 0)};
  ZuCheck(packet);
  packet->local = original;
  packet->addr = remote;
  unsigned references = packet->refCount();
  link->observePacket(packet, 0, 0, 123, 0, true, false);
  ZuCheck(!app.txPending() && packet->refCount() == references);
  app.enterTx();
  link->refreshPathRxTx();
  ZuCheck(link->recoveryPathDiag().bytesRx == 123);

  // Keeping the peer tuple while changing only the local server binding
  // must bypass the active fast path and retain one original packet.
  app.enterRx();
  packet->local = preferred;
  link->observePacket(packet, 0, 0, 1200, 1, true, false);
  ZuCheck(app.txPendingQ.count_() == 1 && packet->refCount() == references + 1);
  while (app.rxPending() || app.txPending()) {
    if (app.rxPending()) app.runRx();
    if (app.txPending()) app.runTx();
  }
  app.enterTx();
  ZuCheck(link->migrationActive() && link->candidatePathInfo().local == preferred);
  Zquic::PathData challenge{link->validatingChallenge()};

  // A preceding coalesced packet/header is not part of the immutable frame
  // span. Invalid prefix bytes make a missing absolute offset observable.
  enum { Prefix = 32 };
  memset(packet->data_(), 0xff, Prefix);
  int n = Zquic::FrameCodec::writePathResponse(
    packet->data_() + Prefix, packet->size - Prefix, challenge);
  ZuCheck(n > 0);
  packet->length = Prefix + unsigned(n);
  auto retained = packet.ptr();
  app.enterRx();
  link->observePacket(packet, Prefix, unsigned(n), packet->length, 2, false, true);
  ZuCheck(app.txPendingQ.count_() == 1 && packet->refCount() == references + 1);
  packet = nullptr;
  ZuCheck(retained->refCount() == 1);
  while (app.rxPending() || app.txPending()) {
    if (app.rxPending()) app.runRx();
    if (app.txPending()) app.runTx();
  }
  app.enterTx();
  ZuCheck(link->migrationPromotedCount == 1 && link->activeLocal() == preferred);
  link->recoveryStop();
}

int main(int argc, char **argv)
{
  ZuTestUtil::parse(argc, argv);

  ZiTestResidue::init("ZquicAPITest");
#ifdef Zquic_DEBUG
  g_residue = ZiTestResidue::dir("qlog");
#endif

  ZuTestMain();
  ZuTestCall(testPreferredPathProof);
  ZuTestCall(testPreferredAuthenticatedFreshness);
  ZuTestCall(testPreferredRecoveryEpoch);
  ZuTestCall(testLocalCIDTxValidity);
  ZuTestCall(testPacketPathHandoff);
  ZuTestCall(testParams);
  ZuTestCall(testServerAddrs);
  ZuTestCall(testLongProbePadding);
  ZuTestCall(testStreamShape);
  ZuTestCall(testWritableMembership);
  ZuTestCall(testAlignedSurfaceShape);
  ZuTestCall(testPendingTxReasons);
  ZuTestCall(testLinkStateOwnership);
  ZuTestCall(testStreamLimitOwnership);
  ZuTestCall(testStatelessResetDetection);
  ZuTestCall(testRecoveryRuntimeOrdering);
  ZuTestCall(disabledCongestionRecovery);
  ZuTestCall(testAckPublications);
  ZuTestCall(testPeerAckPublications);
  ZuTestCall(testTargetedCreditUpdates);
  ZuTestCall(testShortSendCommit);
  ZuTestCall(testApplicationCallbacks);
  ZuTestCall(testStopSendingOwnership);
  ZuTestCall(testStreamRegistryOwnership);
  ZuTestCall(testCxnIDFrameLifecycle);
  ZuTestCall(testActiveMigrationAPI);
  ZuTestCall(testPreferredMigration);
  ZuTestCall(testCxnIDMigrationOverlap);
  ZuTestCall(testMigrationQLogEvents);
  ZuTestCall(testInitValidate);
  ZuTestCall(testHashConfig);
}
