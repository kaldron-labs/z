//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Built-in FIX control messages; application dictionaries remain separate

#ifndef ZfixSession_HH
#define ZfixSession_HH

#ifndef ZfixLib_HH
#include <zlib/ZfixLib.hh>
#endif

#include <zlib/ZuUnion.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/Zfix.hh>
#include <zlib/ZfixFrame.hh>

ZuStructFacet(FIXLogon);
ZuStructFacet(FIXLogonSync);
ZuStructFacet(FIXLogonApp);
ZuStructFacet(FIXLogonSyncApp);
ZuStructFacet(FIXEmpty);
ZuStructFacet(FIXRejectBase);
ZuStructFacet(FIXRejectTag);
ZuStructFacet(FIXRejectText);
ZuStructFacet(FIXRejectRef);
ZuStructFacet(FIXRejectRefTag);
ZuStructFacet(FIXRejectRefText);
ZuStructFacet(FIXRejectRefAll);

namespace Zfix::Session {

struct Logon : ZmNHeap<Logon> {
  int32_t		encryptMethod = 0;
  uint32_t		heartBtInt = 30;
  bool			resetSeqNum = false;
  uint32_t		maxMessageSize = Fmt::Limits::FrameBytes;
  uint64_t		nextExpected = 0;
  ZuCSpan		defaultAppVerID;
  static constexpr auto	msgType = "A"_z;
friend Fmt::AsObject Zfix_Fmt(Logon *);
friend inline ZuStringT<"Zfix.Logon"> ZmNHeapID(Logon *);
};

ZfStruct(ZfixAPI, (Logon, FIX),
  (encryptMethod, (FIX::ID<98>, Required),		Int32),
  (heartBtInt, (FIX::ID<108>, Required),		UInt32),
  (resetSeqNum, (FIX::ID<141>, Deflt<false>),	Bool),
  (maxMessageSize, (FIX::ID<383>, Deflt<0>, (Range<1U, UINT32_MAX>)), UInt32),
  (nextExpected, (FIX::ID<789>, Deflt<0>, (Range<1, UINT64_MAX>)), UInt64),
  (defaultAppVerID, (FIX::ID<1137>),		String));
ZfStructRender(ZfixAPI, Logon, FIXLogon,
  encryptMethod, heartBtInt, resetSeqNum, maxMessageSize);
ZfStructRender(ZfixAPI, Logon, FIXLogonSync,
  encryptMethod, heartBtInt, resetSeqNum, maxMessageSize, nextExpected);
ZfStructRender(ZfixAPI, Logon, FIXLogonApp,
  encryptMethod, heartBtInt, resetSeqNum, maxMessageSize, defaultAppVerID);
ZfStructRender(ZfixAPI, Logon, FIXLogonSyncApp,
  encryptMethod, heartBtInt, resetSeqNum, maxMessageSize, nextExpected, defaultAppVerID);

struct HeartBt : ZmNHeap<HeartBt> {
  ZuCSpan		testReqID;
  static constexpr auto	msgType = "0"_z;
friend Fmt::AsObject Zfix_Fmt(HeartBt *);
friend inline ZuStringT<"Zfix.HeartBt"> ZmNHeapID(HeartBt *);
};

ZfStruct(ZfixAPI, (HeartBt, FIX),
  (testReqID, (FIX::ID<112>),		String));
ZfStructRender(ZfixAPI, HeartBt, FIXEmpty);

struct TestReq : ZmNHeap<TestReq> {
  ZuCSpan		testReqID;
  static constexpr auto	msgType = "1"_z;
friend Fmt::AsObject Zfix_Fmt(TestReq *);
friend inline ZuStringT<"Zfix.TestReq"> ZmNHeapID(TestReq *);
};

ZfStruct(ZfixAPI, (TestReq, FIX),
  (testReqID, (FIX::ID<112>, Required),	String));

struct ResendReq : ZmNHeap<ResendReq> {
  uint64_t		beginSeqNo = 1;
  uint64_t		endSeqNo = 0;
  static constexpr auto	msgType = "2"_z;
friend Fmt::AsObject Zfix_Fmt(ResendReq *);
friend inline ZuStringT<"Zfix.ResendReq"> ZmNHeapID(ResendReq *);
};

ZfStruct(ZfixAPI, (ResendReq, FIX),
  (beginSeqNo, (FIX::ID<7>, Required, (Range<1, UINT64_MAX>)), UInt64),
  (endSeqNo, (FIX::ID<16>, Required),	UInt64));

struct Reject : ZmNHeap<Reject> {
  uint64_t		refSeqNo = 0;
  uint32_t		refTagID = 0;
  int32_t		reason = -1;
  ZuCSpan		text;
  static constexpr auto	msgType = "3"_z;
friend Fmt::AsObject Zfix_Fmt(Reject *);
friend inline ZuStringT<"Zfix.Reject"> ZmNHeapID(Reject *);
};

ZfStruct(ZfixAPI, (Reject, FIX),
  (refSeqNo, (FIX::ID<45>, Required, (Range<1, UINT64_MAX>)), UInt64),
  (refTagID, (FIX::ID<371>, Deflt<0>, (Range<1U, UINT32_MAX>)), UInt32),
  (reason, (FIX::ID<373>, Deflt<-1>, (Range<0, 99>)), Int32),
  (text, (FIX::ID<58>),			String));
ZfStructRender(ZfixAPI, Reject, FIXRejectBase, refSeqNo, reason);
ZfStructRender(ZfixAPI, Reject, FIXRejectTag, refSeqNo, refTagID, reason);
ZfStructRender(ZfixAPI, Reject, FIXRejectText, refSeqNo, reason, text);
ZfStructRender(ZfixAPI, Reject, FIXRejectRef, refSeqNo);
ZfStructRender(ZfixAPI, Reject, FIXRejectRefTag, refSeqNo, refTagID);
ZfStructRender(ZfixAPI, Reject, FIXRejectRefText, refSeqNo, text);
ZfStructRender(ZfixAPI, Reject, FIXRejectRefAll, refSeqNo, refTagID, text);

struct SeqReset : ZmNHeap<SeqReset> {
  uint64_t		newSeqNo = 1;
  bool			gapFill = false;
  static constexpr auto	msgType = "4"_z;
friend Fmt::AsObject Zfix_Fmt(SeqReset *);
friend inline ZuStringT<"Zfix.SeqReset"> ZmNHeapID(SeqReset *);
};

ZfStruct(ZfixAPI, (SeqReset, FIX),
  (newSeqNo, (FIX::ID<36>, Required, (Range<1, UINT64_MAX>)), UInt64),
  (gapFill, (FIX::ID<123>, Deflt<false>),	Bool));

struct Logout : ZmNHeap<Logout> {
  ZuCSpan		text;
  static constexpr auto	msgType = "5"_z;
friend Fmt::AsObject Zfix_Fmt(Logout *);
friend inline ZuStringT<"Zfix.Logout"> ZmNHeapID(Logout *);
};

ZfStruct(ZfixAPI, (Logout, FIX),
  (text, (FIX::ID<58>),			String));
ZfStructRender(ZfixAPI, Logout, FIXEmpty);

using Msg = ZuUnion<void, Logon, HeartBt, TestReq, ResendReq,
  Reject, SeqReset, Logout>;
using MTs = MTList<MT<Logon, "A">, MT<HeartBt, "0">,
  MT<TestReq, "1">, MT<ResendReq, "2">, MT<Reject, "3">,
  MT<SeqReset, "4">, MT<Logout, "5">>;

// Zero means business/unknown; only the seven reserved control MsgTypes match.
ZfixExtern unsigned type(ZuCSpan);
// All returned spans borrow frame backing. Failure never installs a successful
// control alternative; the caller retains backing for the enclosing context.
ZfixExtern Fmt::Error read(const Fmt::FrameView &, Msg &,
  Fmt::Header &, Fmt::Trailer &, Fmt::Limits = {});

// Standard optional emission facets are selected once in the session library.
ZfixExtern ZuCSpan msgType(const Msg &);
ZfixExtern Fmt::Error save(Fmt::Sink &, const Msg &, unsigned mode = Transport::FIX4);
template <typename HeaderFacet = ZuFacet::FIX>
Fmt::FrameOutput write(ZuSpan<char> storage, Fmt::Header header,
    const Msg &control, unsigned mode = Transport::FIX4,
    Fmt::Limits limits = {}, const Fmt::Trailer &trailer = {}) {
  header.msgType = msgType(control);
  Fmt::FrameWriter writer{storage, header.beginString, limits};
  if (writer.body().failed()) return writer.finish();
  if (auto error = Fmt::writeHeader<HeaderFacet>(writer.body(), header)) return {{}, error};
  if (auto error = save(writer.body(), control, mode)) return {{}, error};
  if (auto error = Fmt::writeTrailer(writer.body(), trailer)) return {{}, error};
  return writer.finish();
}
} // Zfix::Session

#endif /* ZfixSession_HH */
