//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/Zutcp.hh>

namespace Zutcp {

ZuCSpan Diag::packetSpaceName(PktNumSpace::T space)
{
  return PktNumSpace{}.name(space);
}

ZuCSpan Diag::frameTypeName(FrameType::T type)
{
  return FrameType{}.name(type);
}

void Diag::summary(DiagText &out) const
{
  out.length(0);
#ifdef Zutcp_DEBUG
  out << "packetsRx=" << packetsRx <<
    " packetsTx=" << packetsTx <<
    " bytesRx=" << bytesRx <<
    " bytesTx=" << bytesTx <<
    " headerBytesRx=" << headerBytesRx <<
    " headerBytesTx=" << headerBytesTx <<
    " bodyBytesRx=" << bodyBytesRx <<
    " bodyBytesTx=" << bodyBytesTx <<
    " dataBytesRx=" << dataBytesRx <<
    " dataBytesTx=" << dataBytesTx <<
    " packetsLost=" << packetsLost <<
    " ptoCount=" << ptoCount <<
    " retransmittedFrames=" << retransmittedFrames <<
    " cwnd=" << cwnd <<
    " bytesInFlight=" << bytesInFlight <<
#ifdef Zutcp_DEBUG
    " handshakeState=" << handshakeState <<
#endif
    " pmtudProbes=" << pmtudProbes <<
    " pmtudSuccess=" << pmtudSuccess <<
    " pmtudFailure=" << pmtudFailure;
#endif
}

void Diag::recoverySummary(DiagText &out) const
{
  out.length(0);
#ifdef Zutcp_DEBUG
  out << "cwnd=" << cwnd <<
    " bytesInFlight=" << bytesInFlight <<
    " packetsLost=" << packetsLost <<
    " ptoCount=" << ptoCount <<
    " retransmittedFrames=" << retransmittedFrames;
#endif
}

void Diag::dataSummary(DiagText &out) const
{
  out.length(0);
#ifdef Zutcp_DEBUG
  out << "dataBytesRx=" << dataBytesRx <<
    " dataBytesTx=" << dataBytesTx <<
    " headerBytesRx=" << headerBytesRx <<
    " headerBytesTx=" << headerBytesTx <<
    " bodyBytesRx=" << bodyBytesRx <<
    " bodyBytesTx=" << bodyBytesTx;
#endif
}

} // namespace Zutcp
