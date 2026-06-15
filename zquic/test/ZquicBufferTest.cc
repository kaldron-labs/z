//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZquicBuf.hh>
#include <zlib/ZquicDiag.hh>

using namespace ZuTestUtil;

void testDiagAggregation()
{
  ZuTestScope(testDiagAggregation);

  Zquic::Diag diag;
  diag.notePktRx(100);
  diag.notePktTx(200);
  diag.noteHdrRx(11);
  diag.noteHdrTx(12);
  diag.noteBodyRx(13);
  diag.noteBodyTx(14);
  diag.noteStreamRx(30);
  diag.noteStreamTx(40);
  diag.noteLoss(Zquic::SentPkt{
    1, 0, 100, Zquic::PktSpace::AppData, true, true, false });
  diag.notePTO();
  diag.noteRetransmit(Zquic::SentFrameRef::control());
  diag.setRecovery(12000, 500);
  diag.setHandshakeState("one_rtt");
  diag.setStreamCounts(3, 2);

  Zquic::BufDiag buf;
  diag.add(buf);

  Zquic::PathDiag path;
  path.probesSent = 2;
  path.probesAcked = 1;
  path.probesLost = 1;
  path.blackholes = 1;
  diag.add(path);

  ZuCHECK(diag.packetsRx == 1 &&
      diag.packetsTx == 1 &&
      diag.bytesRx == 100 &&
      diag.bytesTx == 200 &&
      diag.headerBytesRx == 11 &&
      diag.headerBytesTx == 12 &&
      diag.bodyBytesRx == 13 &&
      diag.bodyBytesTx == 14 &&
      diag.streamBytesRx == 30 &&
      diag.streamBytesTx == 40 &&
      diag.packetsLost == 1 &&
      diag.ptoCount == 1 &&
      diag.retransmittedFrames == 1 &&
      diag.cwnd == 12000 &&
      diag.bytesInFlight == 500 &&
      diag.handshakeState == "one_rtt" &&
      diag.openStreams == 3 &&
      diag.closedStreams == 2 &&
      diag.pmtudProbes == 2 &&
      diag.pmtudSuccess == 1 &&
      diag.pmtudFailure == 2,
    "diagnostic aggregation mismatch");

  ZuCHECK(Zquic::Diag::packetSpaceName(Zquic::PktSpace::AppData) ==
      "AppData" &&
      Zquic::Diag::frameTypeName(Zquic::FrameType::Stream) == "Stream" &&
      Zquic::Diag::streamTypeName(Zi::StreamType::Simplex) == "Simplex",
    "diagnostic stable names mismatch");

  Zquic::DiagText summary = diag.summary();
  ZuCHECK(strstr(summary.data(), "packetsRx=1") &&
      strstr(summary.data(), "headerBytesRx=11") &&
      strstr(summary.data(), "bodyBytesTx=14") &&
      strstr(summary.data(), "cwnd=12000") &&
      strstr(summary.data(), "handshakeState=one_rtt") &&
      strstr(summary.data(), "pmtudFailure=2"),
    "diagnostic summary text mismatch");

  Zquic::DiagText recovery = diag.recoverySummary();
  ZuCHECK(strstr(recovery.data(), "bytesInFlight=500") &&
      strstr(recovery.data(), "retransmittedFrames=1"),
    "diagnostic recovery summary mismatch");

  Zquic::DiagText streams = diag.streamSummary();
  ZuCHECK(strstr(streams.data(), "openStreams=3") &&
      strstr(streams.data(), "headerBytesTx=12") &&
      strstr(streams.data(), "bodyBytesRx=13"),
    "diagnostic stream summary mismatch");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testDiagAggregation);
}
