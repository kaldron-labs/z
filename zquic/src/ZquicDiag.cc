//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZquicDiag.hh>

namespace Zquic {

ZuCSpan Diag::packetSpaceName(PacketSpace::T space)
{
  if (space == PacketSpace::Initial) return "initial";
  if (space == PacketSpace::Handshake) return "handshake";
  if (space == PacketSpace::AppData) return "app_data";
  return "unknown";
}

ZuCSpan Diag::frameTypeName(FrameType::T type)
{
  if (type == FrameType::Padding) return "padding";
  if (type == FrameType::Ping) return "ping";
  if (type == FrameType::Ack) return "ack";
  if (type == FrameType::Crypto) return "crypto";
  if (type == FrameType::Stream) return "stream";
  if (type == FrameType::MaxData) return "max_data";
  if (type == FrameType::MaxStreamData) return "max_stream_data";
  if (type == FrameType::MaxStreams) return "max_streams";
  if (type == FrameType::DataBlocked) return "data_blocked";
  if (type == FrameType::StreamDataBlocked) return "stream_data_blocked";
  if (type == FrameType::StreamsBlocked) return "streams_blocked";
  if (type == FrameType::ResetStream) return "reset_stream";
  if (type == FrameType::StopSending) return "stop_sending";
  if (type == FrameType::NewConnectionID) return "new_connection_id";
  if (type == FrameType::RetireConnectionID) return "retire_connection_id";
  if (type == FrameType::PathChallenge) return "path_challenge";
  if (type == FrameType::PathResponse) return "path_response";
  if (type == FrameType::ConnectionClose) return "connection_close";
  return "unknown";
}

ZuCSpan Diag::streamTypeName(StreamType::T type)
{
  if (type == StreamType::Bidi) return "bidi";
  if (type == StreamType::Uni) return "uni";
  return "unknown";
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
    " handshakeState=" << handshakeState <<
    " openStreams=" << openStreams <<
    " closedStreams=" << closedStreams <<
    " pmtudProbes=" << pmtudProbes <<
    " pmtudSuccess=" << pmtudSuccess <<
    " pmtudFailure=" << pmtudFailure <<
    " rxPacketToStreamCopies=" << rxPacketToStreamCopies <<
    " bufferContractViolations=" << bufferContractViolations;
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
