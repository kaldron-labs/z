//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZquicConn.hh>

using namespace ZuTestUtil;

void testCloseDrain()
{
  ZuTestScope(testCloseDrain);

  Zquic::ConnState state;
  ZuCHECK(state.state() == Zquic::LinkState::Starting,
    "initial connection state mismatch");
  ZuCHECK(state.startHandshake() && state.establish(),
    "handshake state transition failed");
  ZuCHECK(state.close(Zquic::TransportError::NoError),
    "close transition failed");
  ZuCHECK(state.state() == Zquic::LinkState::Closing &&
    state.closeState() == Zquic::CloseState::Closing,
    "closing state mismatch");
  ZuCHECK(state.drain(), "drain transition failed");
  ZuCHECK(state.onPTO() && state.onPTO() && state.onPTO(),
    "drain PTO transition failed");
  ZuCHECK(state.state() == Zquic::LinkState::Closed &&
    state.closeState() == Zquic::CloseState::Closed,
    "connection did not close after three PTOs");
}

void testAbort()
{
  ZuTestScope(testAbort);

  Zquic::ConnState state;
  state.abort(Zquic::TransportError::InternalError);
  ZuCHECK(state.state() == Zquic::LinkState::Closed &&
    state.closeError() == Zquic::TransportError::InternalError,
    "abort state mismatch");
}

void testPeerCloseDrain()
{
  ZuTestScope(testPeerCloseDrain);

  Zquic::ConnState state;
  ZuCHECK(state.startHandshake() && state.establish(),
    "peer-close setup failed");
  ZuCHECK(state.peerClose(Zquic::TransportError::ProtocolViolation),
    "peer close transition failed");
  ZuCHECK(state.state() == Zquic::LinkState::Draining &&
      state.closeState() == Zquic::CloseState::Draining &&
      state.closeError() == Zquic::TransportError::ProtocolViolation,
    "peer close drain state mismatch");
  ZuCHECK(state.onPTO() && state.onPTO() && state.onPTO() && state.closed(),
    "peer close did not drain for three PTOs");
  ZuCHECK(!state.peerClose(), "closed connection accepted peer close");
}

void testIdleTimeoutAndDrop()
{
  ZuTestScope(testIdleTimeoutAndDrop);

  Zquic::ConnState idle;
  ZuCHECK(idle.startHandshake() && idle.establish(),
    "idle-timeout setup failed");
  ZuCHECK(idle.idleTimeout(), "idle timeout transition failed");
  ZuCHECK(idle.closed() &&
      idle.closeState() == Zquic::CloseState::Closed &&
      !idle.onPTO() && !idle.close(),
    "idle timeout did not close immediately");

  Zquic::ConnState dropped;
  ZuCHECK(dropped.drop(Zquic::TransportError::ConnectionRefused),
    "drop transition failed");
  ZuCHECK(dropped.closed() &&
      dropped.closeError() == Zquic::TransportError::ConnectionRefused &&
      !dropped.startHandshake(),
    "drop state mismatch");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testCloseDrain);
  ZuTestCall(testAbort);
  ZuTestCall(testPeerCloseDrain);
  ZuTestCall(testIdleTimeoutAndDrop);
}
