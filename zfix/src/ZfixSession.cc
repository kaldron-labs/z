//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZfixSession.hh>

namespace Zfix::Session {

ZfStructImpl(Logon);
ZfStructImpl(Logon, FIX);
ZfStructImpl(Logon, FIXLogon);
ZfStructImpl(Logon, FIXLogonSync);
ZfStructImpl(Logon, FIXLogonApp);
ZfStructImpl(Logon, FIXLogonSyncApp);
ZfStructImpl(HeartBt);
ZfStructImpl(HeartBt, FIX);
ZfStructImpl(HeartBt, FIXEmpty);
ZfStructImpl(TestReq);
ZfStructImpl(TestReq, FIX);
ZfStructImpl(ResendReq);
ZfStructImpl(ResendReq, FIX);
ZfStructImpl(Reject);
ZfStructImpl(Reject, FIX);
ZfStructImpl(Reject, FIXRejectBase);
ZfStructImpl(Reject, FIXRejectTag);
ZfStructImpl(Reject, FIXRejectText);
ZfStructImpl(Reject, FIXRejectRef);
ZfStructImpl(Reject, FIXRejectRefTag);
ZfStructImpl(Reject, FIXRejectRefText);
ZfStructImpl(Reject, FIXRejectRefAll);
ZfStructImpl(SeqReset);
ZfStructImpl(SeqReset, FIX);
ZfStructImpl(Logout);
ZfStructImpl(Logout, FIX);
ZfStructImpl(Logout, FIXEmpty);

unsigned type(ZuCSpan type)
{
  if (type.length() != 1) return 0;
  switch (type[0]) {
    case 'A': return Msg::Index<Logon>{};
    case '0': return Msg::Index<HeartBt>{};
    case '1': return Msg::Index<TestReq>{};
    case '2': return Msg::Index<ResendReq>{};
    case '3': return Msg::Index<Reject>{};
    case '4': return Msg::Index<SeqReset>{};
    case '5': return Msg::Index<Logout>{};
    default: return 0;
  }
}

Fmt::Error read(const Fmt::FrameView &frame, Msg &control, Fmt::Limits limits)
{
  control = Msg{};
  Fmt::Error error;
  ZuSwitch::dispatch<Msg::N>(type(frame.msgType),
    [&frame, &control, &error, limits]<typename I>(I) {
      using O = Msg::Type<I{}>;
      if constexpr (ZuIsSame<O, void>{}) error = {Fmt::ErrorCode::Unresolved, 35};
      else {
	Fmt::MessageHandler<O> h{frame, limits};
	if (!h.valid()) { error = h.error(); return; }
	auto message = h.ctor();
	if (!h.valid()) { error = h.error(); return; }
	control.template p<O>(ZuMv(message));
      }
    });
  return error;
}

ZuCSpan msgType(const Msg &control)
{
  ZuCSpan type;
  ZuSwitch::dispatch<Msg::N>(control.type(), [&type]<typename I>(I) {
    using O = Msg::Type<I{}>;
    if constexpr (!ZuIsSame<O, void>{}) type = O::MsgType;
  });
  return type;
}

} // Zfix::Session

