//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/Zquic.hh>

namespace Zquic {

ZuCSpan Diag::packetSpaceName(PktNumSpace::T space)
{
  return PktNumSpace{}.name(space);
}

ZuCSpan Diag::frameTypeName(FrameType::T type)
{
  return FrameType{}.name(type);
}

ZuCSpan Diag::streamTypeName(Zi::StreamType::T type)
{
  return Zi::StreamType{}.name(type);
}

void Diag::summary(DiagText &out) const
{
  out.length(0);
  out << "packetsRx=" << packetsRx <<
    " packetsTx=" << packetsTx <<
    " bytesRx=" << bytesRx <<
    " bytesTx=" << bytesTx <<
    " headerBytesRx=" << headerBytesRx <<
    " headerBytesTx=" << headerBytesTx <<
    " bodyBytesRx=" << bodyBytesRx <<
    " bodyBytesTx=" << bodyBytesTx <<
    " streamBytesRx=" << streamBytesRx <<
    " streamBytesTx=" << streamBytesTx <<
    " packetsLost=" << packetsLost <<
    " ptoCount=" << ptoCount <<
    " retransmittedFrames=" << retransmittedFrames <<
    " cwnd=" << cwnd <<
    " bytesInFlight=" << bytesInFlight <<
#ifdef Zquic_DEBUG
    " handshakeState=" << handshakeState <<
#endif
    " openStreams=" << openStreams <<
    " closedStreams=" << closedStreams <<
    " pmtudProbes=" << pmtudProbes <<
    " pmtudSuccess=" << pmtudSuccess <<
    " pmtudFailure=" << pmtudFailure;
}

void Diag::recoverySummary(DiagText &out) const
{
  out.length(0);
  out << "cwnd=" << cwnd <<
    " bytesInFlight=" << bytesInFlight <<
    " packetsLost=" << packetsLost <<
    " ptoCount=" << ptoCount <<
    " retransmittedFrames=" << retransmittedFrames;
}

void Diag::streamSummary(DiagText &out) const
{
  out.length(0);
  out << "openStreams=" << openStreams <<
    " closedStreams=" << closedStreams <<
    " streamBytesRx=" << streamBytesRx <<
    " streamBytesTx=" << streamBytesTx <<
    " headerBytesRx=" << headerBytesRx <<
    " headerBytesTx=" << headerBytesTx <<
    " bodyBytesRx=" << bodyBytesRx <<
    " bodyBytesTx=" << bodyBytesTx;
}

} // namespace Zquic
