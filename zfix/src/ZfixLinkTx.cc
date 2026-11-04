//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZtScratch.hh>

#include <zlib/ZfixLinkTx.hh>

namespace Zfix {

ZtEnumImplNS(TxNoticeKind);

TxBody::Ref controlBody(unsigned action, const SessionEvent &event,
    const LinkState &session, uint64_t expected, ZuCSpan echo,
    Fmt::Error &error, unsigned rxFrameBytes, Fmt::Limits txLimits)
{
  Session::Msg control;
  const auto &config = session.config_();
  switch (action) {
    case SessionAction::SendLogon: {
      Session::Logon logon;
      logon.heartBtInt = session.heartBtInt_();
      logon.resetSeqNum = session.resetArmed_();
      logon.maxMessageSize = rxFrameBytes;
      if (config.mode == Transport::FIX44A) logon.nextExpected = expected;
      if (config.mode == Transport::FIXT11)
	logon.defaultAppVerID = AppVersion::Wire::v2s(config.appVerID);
      control.p<Session::Logon>(logon);
      break;
    }
    case SessionAction::SendProbe: {
      unsigned size = sizeof("Zfix/") + 3 + ZuBox<uint128_t>{event.instance}.length() +
	ZuBox<uint64_t>{event.epoch}.length() + ZuBox<uint64_t>{event.connection}.length() +
	ZuBox<uint64_t>{event.probe}.length();
      auto probe = ZtScratch(SessionString, size);
      LinkState::printProbe(probe, event);
      control.p<Session::TestReq>(Session::TestReq{.testReqID = probe});
      return TxBody::captureControl(control, error, config.mode, txLimits, event.probe);
    }
    case SessionAction::SendHeartbeat: control.p<Session::HeartBt>(Session::HeartBt{}); break;
    case SessionAction::Echo: {
      if (!echo.data()) { error = {Fmt::ErrorCode::Required, 112}; return {}; }
      control.p<Session::HeartBt>(Session::HeartBt{.testReqID = echo}); break;
    }
    case SessionAction::RequestGap:
      control.p<Session::ResendReq>(Session::ResendReq{
	.beginSeqNo = event.gapBegin, .endSeqNo = event.gapEnd});
      break;
    case SessionAction::SendLogout: control.p<Session::Logout>(Session::Logout{}); break;
    default: error = {Fmt::ErrorCode::Unresolved, 35}; return {};
  }
  return TxBody::captureControl(control, error, config.mode, txLimits);
}
} // Zfix

