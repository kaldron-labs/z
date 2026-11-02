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

// Administrative render facets reuse the ordinary declarations on their actual
// message type. This is the normal ZfStructRender machinery plus that same
// explicitly selected field typelist, never another runtime representation.
#define ZfixSessionRender(API, O, Facet, ...) \
  __VA_OPT__(ZuPP_Eval(ZuPP_MapArg(ZfField_RenderDecl, (O, Facet), __VA_ARGS__))) \
  using ZfFields_##O##_##Facet = ZuTypeConcat<Zfix::SessionFields<O>, ZuTypeList< \
    __VA_OPT__(ZuPP_Eval(ZuPP_MapArgComma(ZfField_RenderType, (O, Facet), __VA_ARGS__)))>>; \
  ZfFields_##O##_##Facet ZuFields_(O *, ZuFacet::Facet *); \
  API ZfVFieldMatchFn ZfFieldMatcher_(O *, ZuFacet::Facet *)

namespace Zfix::Session {

struct Logon : ZmNHeap<Logon> {
  ZuBSpan		signature;
  int32_t		encryptMethod = 0;
  uint32_t		heartBtInt = 30;
  bool			resetSeqNum = false;
  uint32_t		maxMessageSize = Fmt::Limits::FrameBytes;
  uint64_t		nextExpected = 0;
  ZuCSpan		defaultAppVerID;
  ZuBSpan		rawAuth;
  static constexpr auto	MsgType = "A"_z;
friend Fmt::AsObject Zfix_Fmt(Logon *);
friend inline ZuStringT<"Zfix.Logon"> ZmNHeapID(Logon *);
};

ZfStruct(ZfixAPI, (Logon),
  (encryptMethod, (FIX::ID<98>, Required),		Int32),
  (heartBtInt, (FIX::ID<108>, Required),		UInt32),
  (resetSeqNum, (FIX::ID<141>, Deflt<false>),	Bool),
  (maxMessageSize, (FIX::ID<383>, Deflt<0>), UInt32),
  (nextExpected, (FIX::ID<789>, Deflt<0>), UInt64),
  (defaultAppVerID, (FIX::ID<1137>),		String),
  (rawAuth, (FIX::ID<95>, FIX::Binary<96>, FIX::Opt), Bytes),
  (signature, ((FIX::Sig<93, 89>)), Bytes));
using LogonFIXFields = ZuTypeConcat<SessionFields<Logon>, ZfFields_Logon>;
LogonFIXFields ZuFields_(Logon *, ZuFacet::FIX *);
ZfixAPI ZfVFieldMatchFn ZfFieldMatcher_(Logon *, ZuFacet::FIX *);

ZfixSessionRender(ZfixAPI, Logon, FIXLogon,
  encryptMethod, heartBtInt, resetSeqNum, maxMessageSize, rawAuth, signature);
ZfixSessionRender(ZfixAPI, Logon, FIXLogonSync,
  encryptMethod, heartBtInt, resetSeqNum, maxMessageSize, nextExpected, rawAuth, signature);
ZfixSessionRender(ZfixAPI, Logon, FIXLogonApp,
  encryptMethod, heartBtInt, resetSeqNum, maxMessageSize, defaultAppVerID, rawAuth, signature);
ZfixSessionRender(ZfixAPI, Logon, FIXLogonSyncApp,
  encryptMethod, heartBtInt, resetSeqNum, maxMessageSize, nextExpected,
  defaultAppVerID, rawAuth, signature);

struct HeartBt : ZmNHeap<HeartBt> {
  ZuBSpan		signature;
  ZuCSpan		testReqID;
  static constexpr auto	MsgType = "0"_z;
friend Fmt::AsObject Zfix_Fmt(HeartBt *);
friend inline ZuStringT<"Zfix.HeartBt"> ZmNHeapID(HeartBt *);
};

ZfStruct(ZfixAPI, (HeartBt),
  (testReqID, (FIX::ID<112>),		String),
  (signature, ((FIX::Sig<93, 89>)), Bytes));
using HeartBtFIXFields = ZuTypeConcat<SessionFields<HeartBt>, ZfFields_HeartBt>;
HeartBtFIXFields ZuFields_(HeartBt *, ZuFacet::FIX *);
ZfixAPI ZfVFieldMatchFn ZfFieldMatcher_(HeartBt *, ZuFacet::FIX *);

ZfixSessionRender(ZfixAPI, HeartBt, FIXEmpty, signature);

struct TestReq : ZmNHeap<TestReq> {
  ZuBSpan		signature;
  ZuCSpan		testReqID;
  static constexpr auto	MsgType = "1"_z;
friend Fmt::AsObject Zfix_Fmt(TestReq *);
friend inline ZuStringT<"Zfix.TestReq"> ZmNHeapID(TestReq *);
};

ZfStruct(ZfixAPI, (TestReq),
  (testReqID, (FIX::ID<112>, Required),	String),
  (signature, ((FIX::Sig<93, 89>)), Bytes));
using TestReqFIXFields = ZuTypeConcat<SessionFields<TestReq>, ZfFields_TestReq>;
TestReqFIXFields ZuFields_(TestReq *, ZuFacet::FIX *);
ZfixAPI ZfVFieldMatchFn ZfFieldMatcher_(TestReq *, ZuFacet::FIX *);


struct ResendReq : ZmNHeap<ResendReq> {
  ZuBSpan		signature;
  uint64_t		beginSeqNo = 1;
  uint64_t		endSeqNo = 0;
  static constexpr auto	MsgType = "2"_z;
friend Fmt::AsObject Zfix_Fmt(ResendReq *);
friend inline ZuStringT<"Zfix.ResendReq"> ZmNHeapID(ResendReq *);
};

ZfStruct(ZfixAPI, (ResendReq),
  (beginSeqNo, (FIX::ID<7>, Required), UInt64),
  (endSeqNo, (FIX::ID<16>, Required),	UInt64),
  (signature, ((FIX::Sig<93, 89>)), Bytes));
using ResendReqFIXFields = ZuTypeConcat<SessionFields<ResendReq>, ZfFields_ResendReq>;
ResendReqFIXFields ZuFields_(ResendReq *, ZuFacet::FIX *);
ZfixAPI ZfVFieldMatchFn ZfFieldMatcher_(ResendReq *, ZuFacet::FIX *);


struct Reject : ZmNHeap<Reject> {
  ZuBSpan		signature;
  uint64_t		refSeqNo = 0;
  int32_t		refTagID = 0;
  int32_t		reason = -1;
  ZuCSpan		text;
  static constexpr auto	MsgType = "3"_z;
friend Fmt::AsObject Zfix_Fmt(Reject *);
friend inline ZuStringT<"Zfix.Reject"> ZmNHeapID(Reject *);
};

ZfStruct(ZfixAPI, (Reject),
  (refSeqNo, (FIX::ID<45>, Required), UInt64),
  (refTagID, (FIX::ID<371>, Deflt<0>), Int32),
  (reason, (FIX::ID<373>, Deflt<-1>), Int32),
  (text, (FIX::ID<58>),			String),
  (signature, ((FIX::Sig<93, 89>)), Bytes));
using RejectFIXFields = ZuTypeConcat<SessionFields<Reject>, ZfFields_Reject>;
RejectFIXFields ZuFields_(Reject *, ZuFacet::FIX *);
ZfixAPI ZfVFieldMatchFn ZfFieldMatcher_(Reject *, ZuFacet::FIX *);

ZfixSessionRender(ZfixAPI, Reject, FIXRejectBase, refSeqNo, reason, signature);
ZfixSessionRender(ZfixAPI, Reject, FIXRejectTag, refSeqNo, refTagID, reason, signature);
ZfixSessionRender(ZfixAPI, Reject, FIXRejectText, refSeqNo, reason, text, signature);
ZfixSessionRender(ZfixAPI, Reject, FIXRejectRef, refSeqNo, signature);
ZfixSessionRender(ZfixAPI, Reject, FIXRejectRefTag, refSeqNo, refTagID, signature);
ZfixSessionRender(ZfixAPI, Reject, FIXRejectRefText, refSeqNo, text, signature);
ZfixSessionRender(ZfixAPI, Reject, FIXRejectRefAll, refSeqNo, refTagID, text, signature);

struct SeqReset : ZmNHeap<SeqReset> {
  ZuBSpan		signature;
  uint64_t		newSeqNo = 1;
  bool			gapFill = false;
  static constexpr auto	MsgType = "4"_z;
friend Fmt::AsObject Zfix_Fmt(SeqReset *);
friend inline ZuStringT<"Zfix.SeqReset"> ZmNHeapID(SeqReset *);
};

ZfStruct(ZfixAPI, (SeqReset),
  (newSeqNo, (FIX::ID<36>, Required), UInt64),
  (gapFill, (FIX::ID<123>, Deflt<false>),	Bool),
  (signature, ((FIX::Sig<93, 89>)), Bytes));
using SeqResetFIXFields = ZuTypeConcat<SessionFields<SeqReset>, ZfFields_SeqReset>;
SeqResetFIXFields ZuFields_(SeqReset *, ZuFacet::FIX *);
ZfixAPI ZfVFieldMatchFn ZfFieldMatcher_(SeqReset *, ZuFacet::FIX *);


struct Logout : ZmNHeap<Logout> {
  ZuBSpan		signature;
  ZuCSpan		text;
  static constexpr auto	MsgType = "5"_z;
friend Fmt::AsObject Zfix_Fmt(Logout *);
friend inline ZuStringT<"Zfix.Logout"> ZmNHeapID(Logout *);
};

ZfStruct(ZfixAPI, (Logout),
  (text, (FIX::ID<58>),			String),
  (signature, ((FIX::Sig<93, 89>)), Bytes));
using LogoutFIXFields = ZuTypeConcat<SessionFields<Logout>, ZfFields_Logout>;
LogoutFIXFields ZuFields_(Logout *, ZuFacet::FIX *);
ZfixAPI ZfVFieldMatchFn ZfFieldMatcher_(Logout *, ZuFacet::FIX *);

ZfixSessionRender(ZfixAPI, Logout, FIXEmpty, signature);

#undef ZfixSessionRender

using Msg = ZuUnion<void, Logon, HeartBt, TestReq, ResendReq,
  Reject, SeqReset, Logout>;
using MTs = MTList<MT<Logon, "A">, MT<HeartBt, "0">,
  MT<TestReq, "1">, MT<ResendReq, "2">, MT<Reject, "3">,
  MT<SeqReset, "4">, MT<Logout, "5">>;

// Zero means business/unknown; only the seven reserved control MsgTypes match.
ZfixExtern unsigned type(ZuCSpan);
// All returned spans borrow frame backing. Failure never installs a successful
// control alternative; the caller retains backing for the enclosing context.
ZfixExtern Fmt::Error read(const Fmt::FrameView &, Msg &, Fmt::Limits = {});

// Standard optional emission facets are selected once in the session library.
ZfixExtern ZuCSpan msgType(const Msg &);
template <typename Fn>
Fmt::Error visit(const Msg &control, unsigned mode, Fn &&fn)
{
  Fmt::Error result{Fmt::ErrorCode::Unresolved, 35};
  ZuSwitch::dispatch<Msg::N>(control.type(),
    [&fn, &control, mode, &result]<typename I>(I) {
      using O = Msg::Type<I{}>;
      if constexpr (!ZuIsSame<O, void>{}) {
	const auto &message = control.template p<I{}>();
	auto send = [&fn, &message]<typename Facet>() {
	  return fn.template operator()<O, Facet>(message);
	};
	if constexpr (ZuIsSame<O, Logon>{}) {
	  if (mode == Transport::FIX44A && !message.nextExpected) {
	    result = {Fmt::ErrorCode::Required, 789}; return;
	  }
	  if (mode == Transport::FIXT11 && !message.defaultAppVerID.length()) {
	    result = {Fmt::ErrorCode::Required, 1137}; return;
	  }
	  if (message.nextExpected) {
	    if (message.defaultAppVerID.data())
	      result = send.template operator()<ZuFacet::FIXLogonSyncApp>();
	    else result = send.template operator()<ZuFacet::FIXLogonSync>();
	  } else {
	    if (message.defaultAppVerID.data())
	      result = send.template operator()<ZuFacet::FIXLogonApp>();
	    else result = send.template operator()<ZuFacet::FIXLogon>();
	  }
	} else if constexpr (ZuIsSame<O, HeartBt>{}) {
	  if (message.testReqID.data()) result = send.template operator()<ZuFacet::FIX>();
	  else result = send.template operator()<ZuFacet::FIXEmpty>();
	} else if constexpr (ZuIsSame<O, Logout>{}) {
	  if (message.text.data()) result = send.template operator()<ZuFacet::FIX>();
	  else result = send.template operator()<ZuFacet::FIXEmpty>();
	} else if constexpr (ZuIsSame<O, Reject>{}) {
	  if (!message.refSeqNo) { result = {Fmt::ErrorCode::Value, 45}; return; }
	  if (message.reason < -1 || message.reason > 99) {
	    result = {Fmt::ErrorCode::Value, 373}; return;
	  }
	  using Facets = ZuTypeList<ZuFacet::FIXRejectRef, ZuFacet::FIXRejectRefTag,
	    ZuFacet::FIXRejectRefText, ZuFacet::FIXRejectRefAll,
	    ZuFacet::FIXRejectBase, ZuFacet::FIXRejectTag, ZuFacet::FIXRejectText, ZuFacet::FIX>;
	  unsigned fields = unsigned(message.refTagID != 0) |
	    (unsigned(message.text.data() != nullptr) << 1) |
	    (unsigned(message.reason >= 0) << 2);
	  ZuSwitch::dispatch<Facets::N>(fields, [&send, &result]<typename J>(J) {
	    result = send.template operator()<ZuType<J{}, Facets>>();
	  });
	} else result = send.template operator()<ZuFacet::FIX>();
      }
    });
  return result;
}

template <typename Out, template <typename> class Filter = ArchiveField>
Fmt::Error save(Out &out, const Msg &control, unsigned mode = Transport::FIX4) {
  auto error = visit(control, mode, [&out]<typename O, typename Facet>(const O &message) {
    Fmt::save<Facet, Filter>(out, message); return out.error();
  });
  if (error) out.fail(error);
  return error;
}
} // Zfix::Session

#endif /* ZfixSession_HH */
