//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmSingleton.hh>
#include <zlib/ZtArray.hh>
#include <zlib/ZfixTxMsg.hh>
#include <zlib/ZfixRxMsg.hh>

using namespace ZuTestUtil;
using namespace Zfix;
using namespace Zfix::Fmt;

template <typename Facet = ZuFacet::FIX, typename O>
static bool golden(const O &o, ZuCSpan wire)
{
  ZtArray<char, ZtArrayHeapID<"Zfix.SessionTest">> bytes;
  bytes.length(wire.length(), false);
  Sink out{{bytes.data(), bytes.length()}};
  return save<Facet, ArchiveField>(out, o) && out.flush() && out.data() == wire;
}

template <typename O> static Error bodyLoad(O &o, ZuCSpan input) {
  Error error;
  read<O, ZuFacet::FIX, ArchiveField>(o, input, error);
  return error;
}

static void logon()
{
  ZuTestScope(logon);
  Session::Logon value;
  ZuCheck((golden<ZuFacet::FIXLogon>(value, "98=0\1" "108=30\1" "141=N\1" "383=1048576\1"_z)));
  value.nextExpected = 17;
  value.defaultAppVerID = "9";
  ZuCheck((golden<ZuFacet::FIXLogonSync>(value,
    "98=0\1" "108=30\1" "141=N\1" "383=1048576\1" "789=17\1"_z)));
  ZuCheck((golden<ZuFacet::FIXLogonApp>(value,
    "98=0\1" "108=30\1" "141=N\1" "383=1048576\1" "1137=9\1"_z)));
  ZuCheck((golden<ZuFacet::FIXLogonSyncApp>(value,
    "98=0\1" "108=30\1" "141=N\1" "383=1048576\1" "789=17\1" "1137=9\1"_z)));
  ZuCheck(!bodyLoad(value, "98=0\1" "108=0\1"_z));
  ZuCheck(!value.heartBtInt && !value.maxMessageSize && !value.nextExpected);
  ZuCheck(bodyLoad(value, "98=0\1"_z).code == ErrorCode::Required);
  ZuCheck(!bodyLoad(value, "98=0\1" "108=30\1" "789=0\1"_z));
  ZuCheck(!bodyLoad(value, "98=0\1" "108=30\1" "383=0\1"_z));
}

template <typename Facet> static void authFacet()
{
  ZuTestScope(authFacet);
  constexpr auto Payload = "A\1\0=B"_z;
  Session::Logon value;
  value.rawAuth = ZuBSpan{Payload};
  value.nextExpected = 17; value.defaultAppVerID = "9";
  ZmRef<ZiIOBuf> buffer = ZmSingleton<Zi::AppBufAllocator<
    ZiIOBuf, ZiIOBuf_DefltSize>>::instance()->allocBuf();
  Sink out{{buffer->data(), buffer->size}};
  ZuCheck((save<Facet, ArchiveField>(out, value) && out.flush()));
  Session::Logon received;
  Error error;
  ZuCheck((read<Session::Logon, Facet, ArchiveField>(received, out.data(), error)));
  FieldPos pos;
  ZuCheck((!inspect<Session::Logon, Facet>(out.data(), 96, pos)));
  ZuCheck(received.rawAuth == value.rawAuth && received.rawAuth.data() ==
    reinterpret_cast<const uint8_t *>(out.data().data() + pos.offset));
}
static void rawAuth()
{
  ZuTestScope(rawAuth);
  ZuTestCall((authFacet<ZuFacet::FIXLogon>));
  ZuTestCall((authFacet<ZuFacet::FIXLogonSync>));
  ZuTestCall((authFacet<ZuFacet::FIXLogonApp>));
  ZuTestCall((authFacet<ZuFacet::FIXLogonSyncApp>));
}

static void liveness()
{
  ZuTestScope(liveness);
  Session::HeartBt heartbeat;
  ZuCheck((golden<ZuFacet::FIXEmpty>(heartbeat, {})));
  constexpr auto Wire = "112=opaque/001 abc\1"_z;
  Session::TestReq request;
  ZuCheck(!bodyLoad(request, Wire));
  heartbeat.testReqID = request.testReqID;
  ZuCheck((golden(heartbeat, Wire)));
  ZuCheck((golden(request, Wire)));
  ZuCheck(request.testReqID == "opaque/001 abc");
  ZuCheck(bodyLoad(request, {}).code == ErrorCode::Required);
  constexpr auto Empty = "112=\1"_z;
  ZuCheck(!bodyLoad(request, Empty) && request.testReqID.data() && !request.testReqID.length());
  ZuCheck(bodyLoad(request, "112=a\1" "112=b\1"_z).code == ErrorCode::Duplicate);
  ZuCheck(!bodyLoad(request, "112=a\0b\1"_z));
  constexpr auto Complete =
    "49=S\1" "56=T\1" "34=7\1" "112=opaque/001 abc\1"_z;
  auto h = handler<Session::TestReq>(Complete);
  auto p = h.alloc();
  ZuCheck(h.valid() && p && p->testReqID == "opaque/001 abc");
  delete p;
}

static void recovery()
{
  ZuTestScope(recovery);
  Session::ResendReq resend;
  resend.beginSeqNo = 7;
  ZuCheck((golden(resend, "7=7\1" "16=0\1"_z)));
  ZuCheck(!bodyLoad(resend, "7=7\1" "16=9\1"_z));
  ZuCheck(resend.beginSeqNo == 7 && resend.endSeqNo == 9);
  ZuCheck(!bodyLoad(resend, "7=0\1" "16=9\1"_z));
  ZuCheck(bodyLoad(resend, "7=1\1"_z).code == ErrorCode::Required);
  Session::SeqReset reset;
  reset.newSeqNo = 9;
  reset.gapFill = true;
  ZuCheck((golden(reset, "36=9\1" "123=Y\1"_z)));
  ZuCheck(!bodyLoad(reset, "36=9\1"_z) && !reset.gapFill);
  ZuCheck(!bodyLoad(reset, "36=0\1"_z));
  ZuCheck(bodyLoad(reset, "123=Y\1"_z).code == ErrorCode::Required);
}

static void diagnostics()
{
  ZuTestScope(diagnostics);
  Session::Reject reject;
  reject.refSeqNo = 7;
  reject.refTagID = 11;
  reject.reason = 5;
  reject.text = "bad value";
  ZuCheck((golden(reject, "45=7\1" "371=11\1" "373=5\1" "58=bad value\1"_z)));
  ZuCheck((golden<ZuFacet::FIXRejectBase>(reject, "45=7\1" "373=5\1"_z)));
  ZuCheck((golden<ZuFacet::FIXRejectTag>(reject, "45=7\1" "371=11\1" "373=5\1"_z)));
  ZuCheck((golden<ZuFacet::FIXRejectText>(reject, "45=7\1" "373=5\1" "58=bad value\1"_z)));
  ZuCheck(!bodyLoad(reject, "45=7\1"_z));
  ZuCheck(!reject.refTagID && reject.reason == -1 && !reject.text.data());
  auto bad = [&reject](ZuCSpan wire, int32_t tag) {
    auto error = bodyLoad(reject, wire);
    return !error;
  };
  ZuCheck(bad("45=0\1"_z, 45));
  ZuCheck(!bodyLoad(reject, "45=7\1" "371=-11\1"_z) && reject.refTagID == -11);
  ZuCheck((golden<ZuFacet::FIXRejectRefTag>(reject, "45=7\1" "371=-11\1"_z)));
  ZuCheck(bad("45=7\1" "371=-2147483649\1"_z, 371));
  ZuCheck(bad("45=7\1" "373=-1\1"_z, 373));
  ZuCheck(bad("45=7\1" "373=100\1"_z, 373));
  Session::Logout logout;
  ZuCheck((golden<ZuFacet::FIXEmpty>(logout, {})));
  logout.text = "bye";
  ZuCheck((golden(logout, "58=bye\1"_z)));
}

static void dispatch()
{
  ZuTestScope(dispatch);
  ZuCheck(Session::MTs::Messages::N == 7 && Session::Msg::N == 8);
  ZuCheck(Session::type("A") == Session::Msg::Index<Session::Logon>{});
  ZuCheck(Session::type("0") == Session::Msg::Index<Session::HeartBt>{});
  ZuCheck(Session::type("5") == Session::Msg::Index<Session::Logout>{});
  ZuCheck(!Session::type("D") && !Session::type("AA") && !Session::type({}));
  constexpr auto Wire = "8=FIX.4.4\1" "9=60\1" "35=1\1" "49=S\1" "56=T\1" "34=7\1"
    "52=20261026-12:34:56\1" "112=opaque/001 abc\1" "10=029\1"_z;
  auto framed = frame(Wire);
  Session::Msg message;
  RxMsg input; input.frame = framed.view;
  ZuCheck(!readSession(input));

  ZuCheck(framed.state == FrameState::Complete);
  auto error = Session::read(framed.view, message);
  ZuCheck(!error && message.is<Session::TestReq>());
  ZuCheck(message.p<Session::TestReq>().testReqID == "opaque/001 abc");
  ZuCheck(input.seqNo == 7 && input.sender == "S");
  auto invalid = framed.view;
  invalid.msgType = "D";
  error = Session::read(invalid, message);
  ZuCheck(error.code == ErrorCode::Unresolved && message.is<void>());
  ZtArray<char, ZtArrayHeapID<"Zfix.SessionTest">> malformed;
  malformed.length(Wire.length() + sizeof("112=a\1"), false);
  FrameWriter bad{{malformed.data(), malformed.length()}, "FIX.4.4"};
  bad.body().field(35); bad.body() << '1'; bad.body().end();
  bad.body().field(49); bad.body() << 'S'; bad.body().end();
  bad.body().field(56); bad.body() << 'T'; bad.body().end();
  bad.body().field(34); bad.body() << '7'; bad.body().end();
  bad.body().field(112); bad.body() << 'a'; bad.body().end();
  bad.body().field(112); bad.body() << 'b'; bad.body().end();
  auto duplicate = bad.finish();
  ZuCheck(bool(duplicate));
  error = Session::read(frame(duplicate.data).view, message);
  ZuCheck(error.code == ErrorCode::Duplicate && error.tag == 112 && message.is<void>());
  ZuCheck(input.seqNo == 7 && input.sender == "S");

}

static void writing()
{
  ZuTestScope(writing);
  SessionConfig session;
  session.identity.version = Version::V4_4;
  session.identity.local = "S"; session.identity.peer = "T";
  auto config = txConfig(session);
  TxContext context{.seqNo = 7,
    .sendingTime = ZuDateTime{ZuDateTimeScan::FIX{}, "20261026-12:34:56"}};
  constexpr auto MaxBody = "98=0\1" "108=30\1" "141=N\1" "383=1048576\1"
    "789=17\1" "1137=9\1"_z;
  unsigned capacity = MaxBody.length() + sizeof(
    "8=FIX.4.4\1" "9=999\1" "35=A\1" "49=S\1" "56=T\1" "34=7\1"
    "52=20261026-12:34:56.123456789\1" "10=000\1") - 1;
  ZtArray<char, ZtArrayHeapID<"Zfix.SessionTest">> bytes;
  bytes.length(capacity, false);
  Session::Msg message{Session::HeartBt{}};
  auto empty = Session::write({bytes.data(), bytes.length()}, config, context, message);
  ZuCheck(bool(empty));
  auto framed = frame(empty.data);
  ZuCheck(framed.state == FrameState::Complete && framed.view.msgType == "0");
  ZuCheck(framed.view.content.find("112="_z) < 0);
  message.p<Session::HeartBt>().testReqID = "";
  auto explicitEmpty = Session::write({bytes.data(), bytes.length()}, config, context, message);
  ZuCheck(explicitEmpty && frame(explicitEmpty.data).view.content.find("112=\1"_z) >= 0);
  message.p<Session::Logon>(Session::Logon{});
  auto syncMissing = Session::write(
    {bytes.data(), bytes.length()}, config, context, message, Transport::FIX44A);
  ZuCheck(!syncMissing && syncMissing.error.tag == 789);
  message.p<Session::Logon>().nextExpected = 17;
  auto sync = Session::write({bytes.data(), bytes.length()}, config, context, message, Transport::FIX44A);
  ZuCheck(sync && frame(sync.data).view.content.find("789=17\1"_z) >= 0);
  auto appMissing = Session::write(
    {bytes.data(), bytes.length()}, config, context, message, Transport::FIXT11);
  ZuCheck(!appMissing && appMissing.error.tag == 1137);
  message.p<Session::Logon>().defaultAppVerID = "9";
  auto app = Session::write({bytes.data(), bytes.length()}, config, context, message, Transport::FIXT11);
  ZuCheck(app && frame(app.data).view.content.find("1137=9\1"_z) >= 0);
  Session::Reject reject;
  reject.refSeqNo = 7;
  message.p<Session::Reject>(reject);
  auto reference = Session::write({bytes.data(), bytes.length()}, config, context, message);
  ZuCheck(bool(reference));
  auto body = frame(reference.data).view.content;
  ZuCheck(body.find("45=7\1"_z) >= 0 && body.find("371="_z) < 0 &&
    body.find("373="_z) < 0 && body.find("58="_z) < 0);
  message = Session::Msg{};
  auto unresolved = Session::write({bytes.data(), bytes.length()}, config, context, message);
  ZuCheck(!unresolved && !unresolved.data);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(writing);
  ZuTestCall(logon);
  ZuTestCall(rawAuth);
  ZuTestCall(liveness);
  ZuTestCall(recovery);
  ZuTestCall(diagnostics);
  ZuTestCall(dispatch);
  return 0;
}
