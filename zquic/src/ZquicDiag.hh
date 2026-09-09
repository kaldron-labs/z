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
using DiagCounter = uint64_t;
#else
struct DiagCounter {
  DiagCounter() = default;
  DiagCounter(uint64_t) { }

  DiagCounter &operator =(uint64_t) { return *this; }
  DiagCounter &operator =(uint32_t) { return *this; }
  DiagCounter &operator =(bool) { return *this; }
  DiagCounter &operator ++() { return *this; }
  DiagCounter operator ++(int) { return {}; }
  DiagCounter &operator +=(uint64_t) { return *this; }
  DiagCounter &operator +=(uint32_t) { return *this; }
  operator uint64_t() const { return 0; }
};
#endif

struct ZquicAPI Diag {
  static ZuCSpan packetSpaceName(PktNumSpace::T);
  static ZuCSpan frameTypeName(FrameType::T);
  static ZuCSpan streamTypeName(Zquic::StreamType::T);

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
  [[no_unique_address]] DiagCounter packetsRx = 0;
  [[no_unique_address]] DiagCounter packetsTx = 0;
  [[no_unique_address]] DiagCounter bytesRx = 0;
  [[no_unique_address]] DiagCounter bytesTx = 0;
  [[no_unique_address]] DiagCounter headerBytesRx = 0;
  [[no_unique_address]] DiagCounter headerBytesTx = 0;
  [[no_unique_address]] DiagCounter bodyBytesRx = 0;
  [[no_unique_address]] DiagCounter bodyBytesTx = 0;
  [[no_unique_address]] DiagCounter streamBytesRx = 0;
  [[no_unique_address]] DiagCounter streamBytesTx = 0;
  [[no_unique_address]] DiagCounter packetsLost = 0;
  [[no_unique_address]] DiagCounter ptoCount = 0;
  [[no_unique_address]] DiagCounter retransmittedFrames = 0;
  [[no_unique_address]] DiagCounter cwnd = 0;
  [[no_unique_address]] DiagCounter bytesInFlight = 0;
  [[no_unique_address]] DiagCounter openStreams = 0;
  [[no_unique_address]] DiagCounter closedStreams = 0;
  [[no_unique_address]] DiagCounter pmtudProbes = 0;
  [[no_unique_address]] DiagCounter pmtudSuccess = 0;
  [[no_unique_address]] DiagCounter pmtudFailure = 0;
};

struct LinkRxDiag {
  // Public diagnostic aggregate layout is retained for dependent API users.
  AckECN	ecnRx[PktNumSpace::N];

  [[no_unique_address]] DiagCounter endpointReady = 0;
  ZmAtomic<uint64_t>	datagramsRx = 0;
  ZmAtomic<uint64_t>	bytesRx = 0;
  [[no_unique_address]] DiagCounter packetsRx = 0;
  [[no_unique_address]] DiagCounter framesRx = 0;
  [[no_unique_address]] DiagCounter duplicatePacketsRx = 0;
  [[no_unique_address]] DiagCounter ackCommitsRx = 0;
  [[no_unique_address]] DiagCounter ackElicitingRx = 0;
  [[no_unique_address]] DiagCounter ackImmediateRx = 0;
  [[no_unique_address]] DiagCounter ackSnapshotPostsRx = 0;
  [[no_unique_address]] DiagCounter streamNoDataRx = 0;
  [[no_unique_address]] DiagCounter cryptoBytesRx = 0;
  [[no_unique_address]] DiagCounter streamBytesRx = 0;
  [[no_unique_address]] DiagCounter invalidStreamFrames = 0;
  [[no_unique_address]] DiagCounter closedStreamFrames = 0;
  [[no_unique_address]] DiagCounter suspectStreamCloses = 0;
  [[no_unique_address]] DiagCounter streamMaxClosedRx = 0;
  [[no_unique_address]] DiagCounter streamMaxInvalidRx = 0;
  [[no_unique_address]] DiagCounter streamCtlClosedRx = 0;
  [[no_unique_address]] DiagCounter streamCtlInvalidRx = 0;
  [[no_unique_address]] DiagCounter streamDataInvalidRx = 0;
  [[no_unique_address]] DiagCounter streamDataStateRx = 0;
  [[no_unique_address]] DiagCounter streamDataFinalRx = 0;
  [[no_unique_address]] DiagCounter streamRxDeqStateRx = 0;
  [[no_unique_address]] DiagCounter streamRxDeqFinalRx = 0;
  [[no_unique_address]] DiagCounter streamBlockedClosedRx = 0;
  [[no_unique_address]] DiagCounter streamBlockedInvalidRx = 0;
  [[no_unique_address]] DiagCounter streamBlockedFinalRx = 0;
  [[no_unique_address]] DiagCounter unhandledAppEvents = 0;
  [[no_unique_address]] DiagCounter peerKeyUpdates = 0;
  [[no_unique_address]] DiagCounter invalidKeyPhases = 0;
  [[no_unique_address]] DiagCounter oldKeysAccepted = 0;
  [[no_unique_address]] DiagCounter keyDiscards = 0;
  [[no_unique_address]] DiagCounter newTokenRx = 0;
  [[no_unique_address]] DiagCounter failures = 0;
  [[no_unique_address]] DiagCounter handshakeComplete = 0;
};

struct MigrationDiag {
  [[no_unique_address]] DiagCounter requested = 0;
  [[no_unique_address]] DiagCounter started = 0;
  [[no_unique_address]] DiagCounter promoted = 0;
  [[no_unique_address]] DiagCounter abandoned = 0;
  [[no_unique_address]] DiagCounter failed = 0;
  [[no_unique_address]] DiagCounter policyReject = 0;
  [[no_unique_address]] DiagCounter noPeerCID = 0;
  [[no_unique_address]] DiagCounter endpointFailure = 0;
  [[no_unique_address]] DiagCounter timeouts = 0;
  [[no_unique_address]] DiagCounter peerObserved = 0;
  [[no_unique_address]] DiagCounter natRebind = 0;
  [[no_unique_address]] DiagCounter localRebindOK = 0;
  [[no_unique_address]] DiagCounter localRebindFail = 0;
};

struct LinkTxDiag {
  // Public diagnostic aggregate layout is retained for dependent API users.
  AckECN	peerAckECN[PktNumSpace::N];
  bool		ptoTimerActive = false;
  bool		lossTimerActive = false;

  ZmAtomic<uint64_t>	packetsTx = 0;
  ZmAtomic<uint64_t>	bytesTx = 0;
  [[no_unique_address]] DiagCounter cryptoBytesTx = 0;
  [[no_unique_address]] DiagCounter streamBytesTx = 0;
  [[no_unique_address]] DiagCounter ackOnlyPacketsTx = 0;
  [[no_unique_address]] DiagCounter streamOnlyPacketsTx = 0;
  [[no_unique_address]] DiagCounter ackStreamPacketsTx = 0;
  [[no_unique_address]] DiagCounter ackSnapshotInstallsTx = 0;
  [[no_unique_address]] DiagCounter ackDueInstallsTx = 0;
  [[no_unique_address]] DiagCounter ackAppendTx = 0;
  [[no_unique_address]] DiagCounter ackAppendEmptyTx = 0;
  [[no_unique_address]] DiagCounter ackAppendNotDueTx = 0;
  [[no_unique_address]] DiagCounter ackSentTx = 0;
  [[no_unique_address]] DiagCounter ctlOnlyPktsTx = 0;
  [[no_unique_address]] DiagCounter ackCtlPktsTx = 0;
  [[no_unique_address]] DiagCounter streamCtlPktsTx = 0;
  [[no_unique_address]] DiagCounter ackStreamCtlPktsTx = 0;
  [[no_unique_address]] DiagCounter cryptoPacketsTx = 0;
  [[no_unique_address]] DiagCounter otherPacketsTx = 0;
  [[no_unique_address]] DiagCounter streamFramesTx = 0;
  [[no_unique_address]] DiagCounter controlFramesTx = 0;
  [[no_unique_address]] DiagCounter cryptoFramesTx = 0;
  [[no_unique_address]] DiagCounter maxDataTx = 0;
  [[no_unique_address]] DiagCounter maxStreamDataTx = 0;
  [[no_unique_address]] DiagCounter maxStreamsTx = 0;
  [[no_unique_address]] DiagCounter dataBlockedTx = 0;
  [[no_unique_address]] DiagCounter streamDataBlockedTx = 0;
  [[no_unique_address]] DiagCounter streamsBlockedTx = 0;
  [[no_unique_address]] DiagCounter resetStreamTx = 0;
  [[no_unique_address]] DiagCounter stopSendingTx = 0;
  [[no_unique_address]] DiagCounter pathChallengeTx = 0;
  [[no_unique_address]] DiagCounter pathResponseTx = 0;
  [[no_unique_address]] DiagCounter handshakeDoneTx = 0;
  [[no_unique_address]] DiagCounter newCxnIDTx = 0;
  [[no_unique_address]] DiagCounter newTokenTx = 0;
  [[no_unique_address]] DiagCounter pathRxObserved = 0;
  [[no_unique_address]] DiagCounter pathRxSame = 0;
  [[no_unique_address]] DiagCounter pathRxNull = 0;
  [[no_unique_address]] DiagCounter pathValidationActive = 0;
  [[no_unique_address]] DiagCounter pathValidationDisabled = 0;
  [[no_unique_address]] DiagCounter pathValidating = 0;
  [[no_unique_address]] DiagCounter pathValidationPromoted = 0;
  [[no_unique_address]] DiagCounter pathResponseUnknown = 0;
  [[no_unique_address]] MigrationDiag migration;
  [[no_unique_address]] DiagCounter ptoSched = 0;
  [[no_unique_address]] DiagCounter ptoNoLevel = 0;
  [[no_unique_address]] DiagCounter ptoArmed = 0;
  [[no_unique_address]] DiagCounter ptoExpired = 0;
  [[no_unique_address]] DiagCounter ptoFlush = 0;
  [[no_unique_address]] DiagCounter ptoRetx = 0;
  [[no_unique_address]] DiagCounter ptoProbe = 0;
  [[no_unique_address]] DiagCounter ptoCount = 0;
  [[no_unique_address]] DiagCounter ptoBackoff = 0;
  [[no_unique_address]] DiagCounter ptoTimeoutUS = 0;
  [[no_unique_address]] DiagCounter retransmittedFrames = 0;
  [[no_unique_address]] DiagCounter lossArmed = 0;
  [[no_unique_address]] DiagCounter lossCanceled = 0;
  [[no_unique_address]] DiagCounter lossExpired = 0;
  // Public diagnostic aggregate layout is retained for dependent API users.
  [[no_unique_address]] DiagCounter
    pktBytesInFlight[PktNumSpace::N] = {};
  [[no_unique_address]] DiagCounter sentPackets[PktNumSpace::N] = {};
  [[no_unique_address]] DiagCounter
    retransmitPending[PktNumSpace::N] = {};
  [[no_unique_address]] DiagCounter
    retransmittable[PktNumSpace::N] = {};
  [[no_unique_address]] DiagCounter congestionWindow = 0;
  [[no_unique_address]] DiagCounter congestionSSThresh = 0;
  [[no_unique_address]] DiagCounter bytesInFlight = 0;
  [[no_unique_address]] DiagCounter persistentCongestion = 0;
  [[no_unique_address]] DiagCounter ecnValidationFailures = 0;
  [[no_unique_address]] DiagCounter unhandledAppEvents = 0;
  [[no_unique_address]] DiagCounter failures = 0;
};

struct LinkDiag {
  LinkDiag() = default;
  LinkDiag(const LinkRxDiag &rx_, const LinkTxDiag &tx_) :
    rx{rx_}, tx{tx_} { }

  uint64_t failures() const { return rx.failures + tx.failures; }
  uint64_t unhandledAppEvents() const {
    return rx.unhandledAppEvents + tx.unhandledAppEvents;
  }

  LinkRxDiag	rx;
  LinkTxDiag	tx;
};

} // namespace Zquic
