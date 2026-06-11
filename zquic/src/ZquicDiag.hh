//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z QUIC diagnostics

#ifndef ZquicDiag_HH
#define ZquicDiag_HH

#ifndef ZquicLib_HH
#include <zlib/ZquicLib.hh>
#endif

#include <zlib/ZtString.hh>

#include <zlib/ZquicPath.hh>
#include <zlib/ZquicRecovery.hh>

namespace Zquic {

using DiagText = ZtString<
  ZtStringHeapID<"Zquic.DiagText",
    ZtStringHeapMax<4096>>>;

struct Diag {
  static ZuCSpan packetSpaceName(PacketSpace::T);
  static ZuCSpan frameTypeName(FrameType::T);
  static ZuCSpan streamTypeName(Zi::StreamType::T);

  void notePacketRx(unsigned bytes) {
    ++packetsRx;
    bytesRx += bytes;
  }
  void notePacketTx(unsigned bytes) {
    ++packetsTx;
    bytesTx += bytes;
  }
  void noteHeaderRx(unsigned bytes) { headerBytesRx += bytes; }
  void noteHeaderTx(unsigned bytes) { headerBytesTx += bytes; }
  void noteBodyRx(unsigned bytes) { bodyBytesRx += bytes; }
  void noteBodyTx(unsigned bytes) { bodyBytesTx += bytes; }
  void noteStreamRx(unsigned bytes) { streamBytesRx += bytes; }
  void noteStreamTx(unsigned bytes) { streamBytesTx += bytes; }
  void noteLoss(const SentPacket &) { ++packetsLost; }
  void notePTO() { ++ptoCount; }
  void noteRetransmit(const SentFrameRef &) { ++retransmittedFrames; }
  void setRecovery(uint64_t cwnd_, uint64_t bytesInFlight_) {
    cwnd = cwnd_;
    bytesInFlight = bytesInFlight_;
  }
  void setHandshakeState(ZuCSpan state) {
    handshakeState.length(0);
    handshakeState << state;
  }
  void setStreamCounts(uint64_t open, uint64_t closed) {
    openStreams = open;
    closedStreams = closed;
  }

  void add(const BufDiag &) { }

  void add(const PathDiag &diag) {
    pmtudProbes += diag.probesSent;
    pmtudSuccess += diag.probesAcked;
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

  uint64_t	packetsRx = 0;
  uint64_t	packetsTx = 0;
  uint64_t	bytesRx = 0;
  uint64_t	bytesTx = 0;
  uint64_t	headerBytesRx = 0;
  uint64_t	headerBytesTx = 0;
  uint64_t	bodyBytesRx = 0;
  uint64_t	bodyBytesTx = 0;
  uint64_t	streamBytesRx = 0;
  uint64_t	streamBytesTx = 0;
  uint64_t	packetsLost = 0;
  uint64_t	ptoCount = 0;
  uint64_t	retransmittedFrames = 0;
  uint64_t	cwnd = 0;
  uint64_t	bytesInFlight = 0;
  DiagText	handshakeState;
  uint64_t	openStreams = 0;
  uint64_t	closedStreams = 0;
  uint64_t	pmtudProbes = 0;
  uint64_t	pmtudSuccess = 0;
  uint64_t	pmtudFailure = 0;
};

} // namespace Zquic

#endif /* ZquicDiag_HH */
