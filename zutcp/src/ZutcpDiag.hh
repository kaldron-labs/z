//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z QUIC diagnostics

#ifndef Zutcp_HH
#error "include zlib/Zutcp.hh before this header"
#endif

#include <zlib/ZtString.hh>

namespace Zutcp {

using DiagText = ZtString<
  ZtStringHeapID<"Zutcp.DiagText",
    ZtStringHeapMax<4096>>>;

#ifdef Zutcp_DEBUG
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

struct ZutcpAPI Diag {
#ifdef Zutcp_DEBUG
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
  [[no_unique_address]] DiagCounter dataBytesRx = 0;
  [[no_unique_address]] DiagCounter dataBytesTx = 0;
  [[no_unique_address]] DiagCounter packetsLost = 0;
  [[no_unique_address]] DiagCounter ptoCount = 0;
  [[no_unique_address]] DiagCounter retransmittedFrames = 0;
  [[no_unique_address]] DiagCounter cwnd = 0;
  [[no_unique_address]] DiagCounter bytesInFlight = 0;
  [[no_unique_address]] DiagCounter pmtudProbes = 0;
  [[no_unique_address]] DiagCounter pmtudSuccess = 0;
  [[no_unique_address]] DiagCounter pmtudFailure = 0;

  static ZuCSpan packetSpaceName(PktNumSpace::T);
  static ZuCSpan frameTypeName(FrameType::T);

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
  void noteDataRx(unsigned bytes) { dataBytesRx += bytes; }
  void noteDataTx(unsigned bytes) { dataBytesTx += bytes; }
  void noteLoss(const SentPkt &) { ++packetsLost; }
  void notePTO() { ++ptoCount; }
  void noteRetransmit(const SentFrameRef &) { ++retransmittedFrames; }
  void setRecovery(uint64_t cwnd_, uint64_t bytesInFlight_) {
    cwnd = cwnd_;
    bytesInFlight = bytesInFlight_;
  }
  void setHandshakeState(ZuCSpan state) {
#ifdef Zutcp_DEBUG
    handshakeState = state;
#else
    (void)state;
#endif
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
  void dataSummary(DiagText &) const;
  DiagText dataSummary() const {
    DiagText out;
    dataSummary(out);
    return out;
  }
};

struct LinkRxDiag {
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
  [[no_unique_address]] DiagCounter dataZeroResultRx = 0;
  [[no_unique_address]] DiagCounter dataBytesRx = 0;
  [[no_unique_address]] DiagCounter invalidDataFrames = 0;
  [[no_unique_address]] DiagCounter closedDataFrames = 0;
  [[no_unique_address]] DiagCounter suspectDataCloses = 0;
  [[no_unique_address]] DiagCounter unhandledAppEvents = 0;
  [[no_unique_address]] DiagCounter failures = 0;
  [[no_unique_address]] DiagCounter handshakeComplete = 0;
};

struct LinkTxDiag {
  AckECN	peerAckECN[PktNumSpace::N];
  bool		ptoTimerActive = false;
  bool		lossTimerActive = false;

  ZmAtomic<uint64_t>	packetsTx = 0;
  ZmAtomic<uint64_t>	bytesTx = 0;
  [[no_unique_address]] DiagCounter dataBytesTx = 0;
  [[no_unique_address]] DiagCounter ackOnlyPacketsTx = 0;
  [[no_unique_address]] DiagCounter dataOnlyPacketsTx = 0;
  [[no_unique_address]] DiagCounter ackDataPacketsTx = 0;
  [[no_unique_address]] DiagCounter ackSnapshotInstallsTx = 0;
  [[no_unique_address]] DiagCounter ackDueInstallsTx = 0;
  [[no_unique_address]] DiagCounter ackAppendTx = 0;
  [[no_unique_address]] DiagCounter ackAppendEmptyTx = 0;
  [[no_unique_address]] DiagCounter ackAppendNotDueTx = 0;
  [[no_unique_address]] DiagCounter ackSentTx = 0;
  [[no_unique_address]] DiagCounter ctlOnlyPktsTx = 0;
  [[no_unique_address]] DiagCounter ackCtlPktsTx = 0;
  [[no_unique_address]] DiagCounter dataCtlPktsTx = 0;
  [[no_unique_address]] DiagCounter ackDataCtlPktsTx = 0;
  [[no_unique_address]] DiagCounter otherPacketsTx = 0;
  [[no_unique_address]] DiagCounter dataFramesTx = 0;
  [[no_unique_address]] DiagCounter controlFramesTx = 0;
  [[no_unique_address]] DiagCounter maxDataTx = 0;
  [[no_unique_address]] DiagCounter dataBlockedTx = 0;
  [[no_unique_address]] DiagCounter resetDataTx = 0;
  [[no_unique_address]] DiagCounter stopSendingTx = 0;
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
  LinkRxDiag	rx;
  LinkTxDiag	tx;

  LinkDiag() = default;
  LinkDiag(const LinkRxDiag &rx_, const LinkTxDiag &tx_) :
    rx{rx_}, tx{tx_} { }

  uint64_t failures() const { return rx.failures + tx.failures; }
  uint64_t unhandledAppEvents() const {
    return rx.unhandledAppEvents + tx.unhandledAppEvents;
  }
};

} // namespace Zutcp
