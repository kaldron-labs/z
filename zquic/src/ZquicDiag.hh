//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z QUIC diagnostics

#ifndef Zquic_HH
#error "include zlib/Zquic.hh before this header"
#endif

#include <zlib/ZtString.hh>


namespace Zquic {

using DiagText = ZtString<
  ZtStringHeapID<"Zquic.DiagText",
    ZtStringHeapMax<4096>>>;

#ifdef Zquic_DEBUG
using RuntimeDiagCounter = uint64_t;
#else
struct RuntimeDiagCounter {
  RuntimeDiagCounter() = default;
  RuntimeDiagCounter(uint64_t) { }

  RuntimeDiagCounter &operator =(uint64_t) { return *this; }
  RuntimeDiagCounter &operator =(uint32_t) { return *this; }
  RuntimeDiagCounter &operator =(bool) { return *this; }
  RuntimeDiagCounter &operator ++() { return *this; }
  RuntimeDiagCounter operator ++(int) { return {}; }
  RuntimeDiagCounter &operator +=(uint64_t) { return *this; }
  RuntimeDiagCounter &operator +=(uint32_t) { return *this; }
  operator uint64_t() const { return 0; }
};
#endif

struct Diag {
  static ZuCSpan packetSpaceName(PktNumSpace::T);
  static ZuCSpan frameTypeName(FrameType::T);
  static ZuCSpan streamTypeName(Zi::StreamType::T);

  void notePktRx(unsigned bytes) {
    ++packetsRx;
    bytesRx += bytes;
  }
  void notePktTx(unsigned bytes) {
    ++packetsTx;
    bytesTx += bytes;
  }
  void noteHdrRx(unsigned bytes) { headerBytesRx += bytes; }
  void noteHdrTx(unsigned bytes) { headerBytesTx += bytes; }
  void noteBodyRx(unsigned bytes) { bodyBytesRx += bytes; }
  void noteBodyTx(unsigned bytes) { bodyBytesTx += bytes; }
  void noteStreamRx(unsigned bytes) { streamBytesRx += bytes; }
  void noteStreamTx(unsigned bytes) { streamBytesTx += bytes; }
  void noteLoss(const SentPkt &) { ++packetsLost; }
  void notePTO() { ++ptoCount; }
  void noteRetransmit(const SentFrameRef &) { ++retransmittedFrames; }
  void setRecovery(uint64_t cwnd_, uint64_t bytesInFlight_) {
    cwnd = cwnd_;
    bytesInFlight = bytesInFlight_;
  }
  void setHandshakeState(ZuCSpan state) {
#ifdef Zquic_DEBUG
    handshakeState = state;
#else
    (void)state;
#endif
  }
  void setStreamCounts(uint64_t open, uint64_t closed) {
    openStreams = open;
    closedStreams = closed;
  }

  void add(const BufDiag &) { }

  void add(const PathDiag &diag) {
    pmtudProbes += diag.probesSent;
    pmtudSuccess += diag.probesAckd;
    pmtudFailure += diag.probesLost + diag.blackholes;
  }

  void summary(DiagText &) const;
  DiagText summary() const {
    DiagText out;
    summary(out);
    return out;
  }
  void recoverySummary(DiagText &) const;
  DiagText recoverySummary() const {
    DiagText out;
    recoverySummary(out);
    return out;
  }
  void streamSummary(DiagText &) const;
  DiagText streamSummary() const {
    DiagText out;
    streamSummary(out);
    return out;
  }

#ifdef Zquic_DEBUG
  DiagText	handshakeState;
#endif
  [[no_unique_address]] RuntimeDiagCounter packetsRx = 0;
  [[no_unique_address]] RuntimeDiagCounter packetsTx = 0;
  [[no_unique_address]] RuntimeDiagCounter bytesRx = 0;
  [[no_unique_address]] RuntimeDiagCounter bytesTx = 0;
  [[no_unique_address]] RuntimeDiagCounter headerBytesRx = 0;
  [[no_unique_address]] RuntimeDiagCounter headerBytesTx = 0;
  [[no_unique_address]] RuntimeDiagCounter bodyBytesRx = 0;
  [[no_unique_address]] RuntimeDiagCounter bodyBytesTx = 0;
  [[no_unique_address]] RuntimeDiagCounter streamBytesRx = 0;
  [[no_unique_address]] RuntimeDiagCounter streamBytesTx = 0;
  [[no_unique_address]] RuntimeDiagCounter packetsLost = 0;
  [[no_unique_address]] RuntimeDiagCounter ptoCount = 0;
  [[no_unique_address]] RuntimeDiagCounter retransmittedFrames = 0;
  [[no_unique_address]] RuntimeDiagCounter cwnd = 0;
  [[no_unique_address]] RuntimeDiagCounter bytesInFlight = 0;
  [[no_unique_address]] RuntimeDiagCounter openStreams = 0;
  [[no_unique_address]] RuntimeDiagCounter closedStreams = 0;
  [[no_unique_address]] RuntimeDiagCounter pmtudProbes = 0;
  [[no_unique_address]] RuntimeDiagCounter pmtudSuccess = 0;
  [[no_unique_address]] RuntimeDiagCounter pmtudFailure = 0;
};

struct RuntimeRxDiag {
  AckECN	ecnRx[PktNumSpace::N];

  [[no_unique_address]] RuntimeDiagCounter endpointReady = 0;
  [[no_unique_address]] RuntimeDiagCounter datagramsRx = 0;
  [[no_unique_address]] RuntimeDiagCounter bytesRx = 0;
  [[no_unique_address]] RuntimeDiagCounter packetsRx = 0;
  [[no_unique_address]] RuntimeDiagCounter framesRx = 0;
  [[no_unique_address]] RuntimeDiagCounter duplicatePacketsRx = 0;
  [[no_unique_address]] RuntimeDiagCounter ackCommitsRx = 0;
  [[no_unique_address]] RuntimeDiagCounter ackElicitingRx = 0;
  [[no_unique_address]] RuntimeDiagCounter ackImmediateRx = 0;
  [[no_unique_address]] RuntimeDiagCounter ackSnapshotPostsRx = 0;
  [[no_unique_address]] RuntimeDiagCounter streamNoDataRx = 0;
  [[no_unique_address]] RuntimeDiagCounter cryptoBytesRx = 0;
  [[no_unique_address]] RuntimeDiagCounter streamBytesRx = 0;
  [[no_unique_address]] RuntimeDiagCounter invalidStreamFrames = 0;
  [[no_unique_address]] RuntimeDiagCounter closedStreamFrames = 0;
  [[no_unique_address]] RuntimeDiagCounter suspiciousStreamCloses = 0;
  [[no_unique_address]] RuntimeDiagCounter streamMaxClosedRx = 0;
  [[no_unique_address]] RuntimeDiagCounter streamMaxInvalidRx = 0;
  [[no_unique_address]] RuntimeDiagCounter streamCtlClosedRx = 0;
  [[no_unique_address]] RuntimeDiagCounter streamCtlInvalidRx = 0;
  [[no_unique_address]] RuntimeDiagCounter streamDataInvalidRx = 0;
  [[no_unique_address]] RuntimeDiagCounter streamDataStateRx = 0;
  [[no_unique_address]] RuntimeDiagCounter streamDataFinalRx = 0;
  [[no_unique_address]] RuntimeDiagCounter streamRxDeqStateRx = 0;
  [[no_unique_address]] RuntimeDiagCounter streamRxDeqFinalRx = 0;
  [[no_unique_address]] RuntimeDiagCounter streamBlockedClosedRx = 0;
  [[no_unique_address]] RuntimeDiagCounter streamBlockedInvalidRx = 0;
  [[no_unique_address]] RuntimeDiagCounter streamBlockedFinalRx = 0;
  [[no_unique_address]] RuntimeDiagCounter unhandledAppEvents = 0;
  [[no_unique_address]] RuntimeDiagCounter peerKeyUpdates = 0;
  [[no_unique_address]] RuntimeDiagCounter invalidKeyPhases = 0;
  [[no_unique_address]] RuntimeDiagCounter oldKeysAccepted = 0;
  [[no_unique_address]] RuntimeDiagCounter keyDiscards = 0;
  [[no_unique_address]] RuntimeDiagCounter newTokenRx = 0;
  [[no_unique_address]] RuntimeDiagCounter failures = 0;
  [[no_unique_address]] RuntimeDiagCounter handshakeComplete = 0;
};

struct MigrationDiag {
  [[no_unique_address]] RuntimeDiagCounter requested = 0;
  [[no_unique_address]] RuntimeDiagCounter started = 0;
  [[no_unique_address]] RuntimeDiagCounter promoted = 0;
  [[no_unique_address]] RuntimeDiagCounter abandoned = 0;
  [[no_unique_address]] RuntimeDiagCounter failed = 0;
  [[no_unique_address]] RuntimeDiagCounter policyReject = 0;
  [[no_unique_address]] RuntimeDiagCounter noPeerCID = 0;
  [[no_unique_address]] RuntimeDiagCounter endpointFailure = 0;
  [[no_unique_address]] RuntimeDiagCounter timeouts = 0;
  [[no_unique_address]] RuntimeDiagCounter peerObserved = 0;
  [[no_unique_address]] RuntimeDiagCounter natRebind = 0;
  [[no_unique_address]] RuntimeDiagCounter localRebindOK = 0;
  [[no_unique_address]] RuntimeDiagCounter localRebindFail = 0;
};

struct RuntimeTxDiag {
  AckECN	peerAckECN[PktNumSpace::N];
  bool		ptoTimerActive = false;
  bool		lossTimerActive = false;

  [[no_unique_address]] RuntimeDiagCounter packetsTx = 0;
  [[no_unique_address]] RuntimeDiagCounter bytesTx = 0;
  [[no_unique_address]] RuntimeDiagCounter cryptoBytesTx = 0;
  [[no_unique_address]] RuntimeDiagCounter streamBytesTx = 0;
  [[no_unique_address]] RuntimeDiagCounter ackOnlyPacketsTx = 0;
  [[no_unique_address]] RuntimeDiagCounter streamOnlyPacketsTx = 0;
  [[no_unique_address]] RuntimeDiagCounter ackStreamPacketsTx = 0;
  [[no_unique_address]] RuntimeDiagCounter ackSnapshotInstallsTx = 0;
  [[no_unique_address]] RuntimeDiagCounter ackDueInstallsTx = 0;
  [[no_unique_address]] RuntimeDiagCounter ackAppendTx = 0;
  [[no_unique_address]] RuntimeDiagCounter ackAppendEmptyTx = 0;
  [[no_unique_address]] RuntimeDiagCounter ackAppendNotDueTx = 0;
  [[no_unique_address]] RuntimeDiagCounter ackSentTx = 0;
  [[no_unique_address]] RuntimeDiagCounter controlOnlyPacketsTx = 0;
  [[no_unique_address]] RuntimeDiagCounter ackControlPacketsTx = 0;
  [[no_unique_address]] RuntimeDiagCounter streamControlPacketsTx = 0;
  [[no_unique_address]] RuntimeDiagCounter ackStreamControlPacketsTx = 0;
  [[no_unique_address]] RuntimeDiagCounter cryptoPacketsTx = 0;
  [[no_unique_address]] RuntimeDiagCounter otherPacketsTx = 0;
  [[no_unique_address]] RuntimeDiagCounter streamFramesTx = 0;
  [[no_unique_address]] RuntimeDiagCounter controlFramesTx = 0;
  [[no_unique_address]] RuntimeDiagCounter cryptoFramesTx = 0;
  [[no_unique_address]] RuntimeDiagCounter maxDataTx = 0;
  [[no_unique_address]] RuntimeDiagCounter maxStreamDataTx = 0;
  [[no_unique_address]] RuntimeDiagCounter maxStreamsTx = 0;
  [[no_unique_address]] RuntimeDiagCounter dataBlockedTx = 0;
  [[no_unique_address]] RuntimeDiagCounter streamDataBlockedTx = 0;
  [[no_unique_address]] RuntimeDiagCounter streamsBlockedTx = 0;
  [[no_unique_address]] RuntimeDiagCounter resetStreamTx = 0;
  [[no_unique_address]] RuntimeDiagCounter stopSendingTx = 0;
  [[no_unique_address]] RuntimeDiagCounter pathChallengeTx = 0;
  [[no_unique_address]] RuntimeDiagCounter pathResponseTx = 0;
  [[no_unique_address]] RuntimeDiagCounter handshakeDoneTx = 0;
  [[no_unique_address]] RuntimeDiagCounter newCxnIDTx = 0;
  [[no_unique_address]] RuntimeDiagCounter newTokenTx = 0;
  [[no_unique_address]] RuntimeDiagCounter pathRxObserved = 0;
  [[no_unique_address]] RuntimeDiagCounter pathRxSame = 0;
  [[no_unique_address]] RuntimeDiagCounter pathRxNull = 0;
  [[no_unique_address]] RuntimeDiagCounter pathValidationActive = 0;
  [[no_unique_address]] RuntimeDiagCounter pathValidationDisabled = 0;
  [[no_unique_address]] RuntimeDiagCounter pathValidationStarted = 0;
  [[no_unique_address]] RuntimeDiagCounter pathValidationPromoted = 0;
  [[no_unique_address]] RuntimeDiagCounter pathResponseUnknown = 0;
  [[no_unique_address]] MigrationDiag migration;
  [[no_unique_address]] RuntimeDiagCounter ptoSched = 0;
  [[no_unique_address]] RuntimeDiagCounter ptoNoLevel = 0;
  [[no_unique_address]] RuntimeDiagCounter ptoArmed = 0;
  [[no_unique_address]] RuntimeDiagCounter ptoExpired = 0;
  [[no_unique_address]] RuntimeDiagCounter ptoFlush = 0;
  [[no_unique_address]] RuntimeDiagCounter ptoRetx = 0;
  [[no_unique_address]] RuntimeDiagCounter ptoProbe = 0;
  [[no_unique_address]] RuntimeDiagCounter ptoCount = 0;
  [[no_unique_address]] RuntimeDiagCounter ptoBackoff = 0;
  [[no_unique_address]] RuntimeDiagCounter ptoTimeoutUS = 0;
  [[no_unique_address]] RuntimeDiagCounter retransmittedFrames = 0;
  [[no_unique_address]] RuntimeDiagCounter lossArmed = 0;
  [[no_unique_address]] RuntimeDiagCounter lossCanceled = 0;
  [[no_unique_address]] RuntimeDiagCounter lossExpired = 0;
  [[no_unique_address]] RuntimeDiagCounter
    pktBytesInFlight[PktNumSpace::N] = {};
  [[no_unique_address]] RuntimeDiagCounter sentPackets[PktNumSpace::N] = {};
  [[no_unique_address]] RuntimeDiagCounter
    retransmitPending[PktNumSpace::N] = {};
  [[no_unique_address]] RuntimeDiagCounter
    retransmittable[PktNumSpace::N] = {};
  [[no_unique_address]] RuntimeDiagCounter congestionWindow = 0;
  [[no_unique_address]] RuntimeDiagCounter congestionSSThresh = 0;
  [[no_unique_address]] RuntimeDiagCounter congestionBytesInFlight = 0;
  [[no_unique_address]] RuntimeDiagCounter persistentCongestion = 0;
  [[no_unique_address]] RuntimeDiagCounter ecnValidationFailures = 0;
  [[no_unique_address]] RuntimeDiagCounter unhandledAppEvents = 0;
  [[no_unique_address]] RuntimeDiagCounter failures = 0;
};

struct RuntimeDiag {
  RuntimeDiag() = default;
  RuntimeDiag(const RuntimeRxDiag &rx_, const RuntimeTxDiag &tx_) :
    rx{rx_}, tx{tx_} { }

  uint64_t failures() const { return rx.failures + tx.failures; }
  uint64_t unhandledAppEvents() const {
    return rx.unhandledAppEvents + tx.unhandledAppEvents;
  }

  RuntimeRxDiag	rx;
  RuntimeTxDiag	tx;
};

} // namespace Zquic
