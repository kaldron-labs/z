//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>

#include <zlib/ZmSingleton.hh>
#include <zlib/ZuTestUtil.hh>
#include <zlib/Zquic.hh>

using namespace ZuTestUtil;

#define ZQUIC_CHECK_RT(x, ...) ZuCheckRT(x, log_(__VA_ARGS__))

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
    .pn = 1,
    .sentTime = ZuTime{0},
    .bytes = 100,
    .space = Zquic::PktNumSpace::AppData,
    .ackEliciting = true,
    .inFlight = true });
  diag.notePTO();
  diag.noteRetransmit(Zquic::SentFrameRef::control());
  diag.setRecovery(12000, 500);
  diag.setHandshakeState("one_rtt");
  diag.setStreamCounts(3, 2);

  Zquic::BufDiag buf;
  diag.add(buf);

  Zquic::PathDiag path;
  path.probesSent = 2;
  path.probesAckd = 1;
  path.probesLost = 1;
  path.blackholes = 1;
  diag.add(path);

  ZuCHECK(Zquic::Diag::packetSpaceName(Zquic::PktNumSpace::AppData) ==
      "AppData" &&
      Zquic::Diag::frameTypeName(Zquic::FrameType::Stream) == "Stream" &&
      Zquic::Diag::streamTypeName(Zquic::StreamType::Simplex) == "Simplex",
    "diagnostic stable names mismatch");

#ifdef Zquic_DEBUG
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
#else
  ZuCHECK(diag.packetsRx == 0 &&
      diag.packetsTx == 0 &&
      diag.bytesRx == 0 &&
      diag.bytesTx == 0 &&
      diag.packetsLost == 0 &&
      diag.ptoCount == 0 &&
      diag.retransmittedFrames == 0,
    "release diagnostics should be inert");

  ZuCHECK(!diag.summary() &&
      !diag.recoverySummary() &&
      !diag.streamSummary(),
    "release diagnostic summaries should be empty");
#endif
}

void testPktBuildMetadata()
{
  ZuTestScopeRT(testPktBuildMetadata);

  uint8_t byte = 0x5a;
  Zquic::PktBuild build;
  for (unsigned i = 0; i < Zquic::PlainVec::Max; ++i)
    ZQUIC_CHECK_RT(build.add(ZuBSpan{&byte, 1}),
      "packet plaintext vector append failed");
  ZQUIC_CHECK_RT(build.count() == Zquic::PlainVec::Max &&
      build.bytes() == Zquic::PlainVec::Max &&
      !build.add(ZuBSpan{&byte, 1}) &&
      !build.pad(1),
    "packet plaintext vector capacity was not enforced");

  build.reset();
  ZQUIC_CHECK_RT(build.scratchAvail() == Zquic::PktBuildScratchSize &&
      build.padTo(Zquic::MinUDPPayload) &&
      build.count() == 1 &&
      build.bytes() == Zquic::MinUDPPayload &&
      build.data()[0].base[0] == 0 &&
      build.data()[0].base[Zquic::MinUDPPayload - 1] == 0,
    "QUIC Initial minimum padding metadata mismatch");

  build.reset();
  ZQUIC_CHECK_RT(build.padForProtSample(1, 16) &&
      build.count() == 1 &&
      build.bytes() == 3,
    "packet protection sample padding metadata mismatch");
}

void testPacketAllocRoles()
{
  ZuTestScope(testPacketAllocRoles);

  Zi::IOBufAllocator *rx = ([] { static ZiMultiplex mx; return Zquic::pktRxAllocator(&mx); })();
  Zi::IOBufAllocator *tx = ([] { static ZiMultiplex mx; return Zquic::pktTxAllocator(&mx); })();
  auto packet = rx->allocBuf();
  auto wire = tx->allocBuf();
  ZuCHECK(packet->avail() >= Zquic::BufSize && wire->avail() >= Zquic::BufSize,
    "UDP recvmsg metadata must not reduce packet payload capacity");
  ZuCHECK(rx->type() == Zi::IOBufType::RxUDP &&
      tx->type() == Zi::IOBufType::TxUDP,
    "packet allocator roles mismatch");
  packet->data()[0] = 0x5a;
  packet->data()[1] = 0xa5;
  ZmRef<ZiIOBuf> observer = packet;
  auto backing = packet.ptr();
  {
    Zquic::StreamRxData first{packet, packet->data(), 1, nullptr, 0};
    Zquic::StreamRxData second{packet, packet->data() + 1, 1, nullptr, 1};
    packet = nullptr;
    ZuCHECK(first.packet.ptr() == backing && second.packet.ptr() == backing &&
	first.span()[0] == 0x5a && second.span()[0] == 0xa5 && backing->refCount() == 3,
      "STREAM slices must retain original packet backing");
    first.packet = nullptr;
    ZuCHECK(second.packet.ptr() == backing && second.span()[0] == 0xa5 &&
	backing->refCount() == 2,
      "one STREAM release must not recycle another STREAM's packet");
    ZuCHECK(second.ensure(second.size + 1) && !second.builtin() &&
	second.packet.ptr() == backing && second.span()[0] == 0xa5 &&
	backing->refCount() == 2,
      "grown STREAM descriptor must preserve bytes and its packet lifetime");
  }
  ZuCHECK(observer->refCount() == 1 && observer->data()[1] == 0xa5,
    "STREAM descriptor teardown must release exactly its retained packet reference");
  observer = nullptr;

  ZmRef<ZiIOBuf> stream = new Zquic::StreamTxBufAlloc<>{
    ZmSingleton<typename Zquic::StreamTxBufAlloc<>::Allocator>::instance(), nullptr, 0};
  ZmRef<ZiIOBuf> crypto = new Zquic::CryptoTxBufAlloc<>{
    ZmSingleton<typename Zquic::CryptoTxBufAlloc<>::Allocator>::instance(), nullptr, 0};
  ZmRef<ZiIOBuf> reassembly =
    new Zquic::CryptoRxBufAlloc<>{ZmSingleton<typename Zquic::CryptoRxBufAlloc<>::Allocator>::instance(), nullptr, 0};
  ZuCHECK(stream->allocator->type() == Zi::IOBufType::App &&
      crypto->allocator->type() == Zi::IOBufType::App &&
      reassembly->allocator->type() == Zi::IOBufType::App,
    "retained plaintext must remain ordinary allocation");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testPacketAllocRoles);
  ZuTestCall(testDiagAggregation);
  ZuTestCall(testPktBuildMetadata);
}
