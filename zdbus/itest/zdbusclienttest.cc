//  -*- mode:c++; indent-tabs-mode:t; tab-width=8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <errno.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmAtomic.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZdbusClient.hh>
#include <zlib/ZdbusServer.hh>

#include "../util/ZdbusTestTool.hh"

using namespace ZuTestUtil;

enum { TimeoutSeconds = 5 };

struct Empty { };
ZuTypeList<> ZuFields_(Empty *, ZuFacet::DBUS *);

struct IDReply {
  ZtString<ZtStringHeapID<"ZdbusTest.ID">> value;
};
ZfStruct(, IDReply, (((value), (Mutable)), (String)));
ZfStructRender(, IDReply, DBUS, value);

struct ErrorBody {
  ZtString<ZtStringHeapID<"ZdbusTest.Error">> message;
  uint32_t code;
};
ZfStruct(, ErrorBody,
  (((message), (Mutable)), (String)),
  (((code), (Mutable)), (UInt32)));
ZfStructRender(, ErrorBody, DBUS, code, message);

using GetIdHeaders = ZuTypeList<
  Zdbus_::HeaderEntry<Zdbus_::Header::Path,
    ZuStringT<"/org/freedesktop/DBus">>,
  Zdbus_::HeaderEntry<Zdbus_::Header::Interface,
    ZuStringT<"org.freedesktop.DBus">>,
  Zdbus_::HeaderEntry<Zdbus_::Header::Member,
    ZuStringT<"GetId">>,
  Zdbus_::HeaderEntry<Zdbus_::Header::Destination,
    ZuStringT<"org.freedesktop.DBus">>>;
struct GetIdRes : ZdbusResParser<GetIdRes, IDReply> { };
struct GetIdReq : ZdbusReqBuilder<GetIdReq, Empty, GetIdHeaders> {
  using Responses = ZuTypeList<GetIdRes>;
};

using FailHeaders = ZuTypeList<
  Zdbus_::HeaderEntry<Zdbus_::Header::Path,
    ZuStringT<"/org/example/ZdbusTest">>,
  Zdbus_::HeaderEntry<Zdbus_::Header::Interface,
    ZuStringT<"org.example.ZdbusTest">>,
  Zdbus_::HeaderEntry<Zdbus_::Header::Member,
    ZuStringT<"Fail">>,
  Zdbus_::HeaderEntry<Zdbus_::Header::Destination,
    ZuStringT<"org.example.ZdbusTest">>>;
using RejectedHeaders = ZuTypeList<
  Zdbus_::HeaderEntry<Zdbus_::Header::ErrorName,
    ZuStringT<"org.example.ZdbusTest.Error.Rejected">>>;
struct RejectedErr : ZdbusErrParser<RejectedErr, ErrorBody,
  RejectedHeaders> { };
struct FailReq : ZdbusReqBuilder<FailReq, IDReply, FailHeaders> {
  using Errors = ZuTypeList<RejectedErr>;
};

using EchoHeaders = ZuTypeList<
  Zdbus_::HeaderEntry<Zdbus_::Header::Path,
    ZuStringT<"/org/example/ZdbusTest">>,
  Zdbus_::HeaderEntry<Zdbus_::Header::Interface,
    ZuStringT<"org.example.ZdbusTest">>,
  Zdbus_::HeaderEntry<Zdbus_::Header::Member,
    ZuStringT<"Echo">>>;
struct EchoReq : ZdbusReqParser<EchoReq, IDReply, EchoHeaders> { };

using ChangedHeaders = ZuTypeList<
  Zdbus_::HeaderEntry<Zdbus_::Header::Path,
    ZuStringT<"/org/example/ZdbusTest">>,
  Zdbus_::HeaderEntry<Zdbus_::Header::Interface,
    ZuStringT<"org.example.ZdbusTest">>,
  Zdbus_::HeaderEntry<Zdbus_::Header::Member,
    ZuStringT<"Changed">>>;
struct ChangedSig : ZdbusSigParser<ChangedSig, IDReply,
  ChangedHeaders> { };

static bool waitFor(ZmSemaphore &sem)
{
  return sem.timedwait(Zm::now(TimeoutSeconds)) == 0;
}

static void client()
{
  ZuTestScope(client);
  const char *fixture = ZiStat{"../util/zdbusbus"}.exists() ?
    "../util/zdbusbus" : "zdbus/util/zdbusbus";
  // pipe2's two-int arrays are part of the operating-system ABI.
  int input[2];
  int output[2];
  bool inputOK = !::pipe2(input, O_CLOEXEC);
  ZuCHECK(inputOK, "input pipe2 failed");
  if (!inputOK) return;
  bool outputOK = !::pipe2(output, O_CLOEXEC);
  ZuCHECK(outputOK, "output pipe2 failed");
  if (!outputOK) {
    ::close(input[0]);
    ::close(input[1]);
    return;
  }
  pid_t child = ::fork();
  if (child < 0) {
    ::close(input[0]);
    ::close(input[1]);
    ::close(output[0]);
    ::close(output[1]);
  }
  if (!child) {
    ::close(input[1]);
    ::close(output[0]);
    ::dup2(input[0], STDIN_FILENO);
    ::dup2(output[1], STDOUT_FILENO);
    ::close(input[0]);
    ::close(output[1]);
    ::execl(fixture, "zdbusbus",
      static_cast<char *>(nullptr));
    _exit(errno == ENOENT ? 127 : 126);
  }
  ZuCHECK(child > 0, "fork failed");
  if (child < 0) return;
  ::close(input[0]);
  ::close(output[1]);

  ZdbusAddress::Text line;
  bool gotAddress = ZdbusTestTool::line(output[0], line);
  ::close(output[0]);
  ZdbusAddress address;
  ZuCHECK(gotAddress && ZdbusAddress::parse(address, line),
    "private bus did not provide an address");
  if (address) {
    ZdbusAddress serverAddress = address;
    ZmScheduler sched{ZmSchedParams().id("ZdbusClientTest").nThreads(4)};
    ZdbusClient cli;
    ZdbusServer srv;
    ZmSemaphore ready;
    ZmSemaphore serverReady;
    ZmSemaphore returned;
    ZmSemaphore errored;
    ZmSemaphore echoed;
    ZmSemaphore held;
    ZmSemaphore queued;
    ZmSemaphore timedOut;
    ZmSemaphore recovered;
    ZmSemaphore cancelled;
    ZmSemaphore cancelAck;
    ZmSemaphore noReplySent;
    ZmSemaphore noReplySeen;
    ZmSemaphore rejected;
    ZmSemaphore matched;
    ZmSemaphore unmatched;
    ZmSemaphore signalled;
    ZmSemaphore signalBarrier;
    ZmSemaphore lateSent;
    ZmSemaphore subAdded;
    ZmSemaphore subSignalled;
    ZmSemaphore subRemoved;
    ZmSemaphore stopped;
    ZmSemaphore secondStopped;
    ZmSemaphore serverStopped;
    ZmAtomic<unsigned> failed = 0;
    ZmAtomic<unsigned> good = 0;
    ZmAtomic<unsigned> errorOK = 0;
    ZmAtomic<unsigned> echoOK = 0;
    ZmAtomic<unsigned> routed = 0;
    ZmAtomic<unsigned> rejectOK = 0;
    ZmAtomic<unsigned> signalOK = 0;
    ZmAtomic<unsigned> queueOK = 0;
    ZmAtomic<unsigned> timeoutOK = 0;
    ZmAtomic<unsigned> recoveryOK = 0;
    ZmAtomic<unsigned> cancelOK = 0;
    ZmAtomic<unsigned> cancelCalls = 0;
    ZmAtomic<uint32_t> cancelSerial = 0;
    ZmRef<ZiIOBuf> heldFrame;
    Zdbus_::FrameInfo heldInfo;
    ZmAtomic<unsigned> subOK = 0;
    ZmAtomic<uint64_t> firstSub = 0;
    ZmAtomic<uint64_t> secondSub = 0;
    ZmAtomic<uint32_t> firstSerial = 0;
    ZmAtomic<uint32_t> secondSerial = 0;
    ZmAtomic<uint32_t> noReplySerial = 0;
    ZmAtomic<uint32_t> echoReqSerial = 0;
    ZmAtomic<uint32_t> echoResSerial = 0;
    ZmAtomic<uint32_t> failReqSerial = 0;
    ZmAtomic<uint32_t> failResSerial = 0;
    ZmAtomic<uint32_t> signalOutSerial = 0;
    ZmAtomic<uint32_t> signalSerial = 0;

    sched.start();
    srv.init(&sched, 3, 4, ZuMv(serverAddress),
      "org.example.ZdbusTest", {},
      [&serverReady](ZuCSpan name) {
        if (name == "org.example.ZdbusTest") serverReady.post();
      },
      [&srv, &held, &heldFrame, &heldInfo, &failResSerial](ZmRef<ZiIOBuf> frame,
          Zdbus_::FrameInfo info) {
        if (info.headers.path != "/org/example/ZdbusTest" ||
            info.headers.interface != "org.example.ZdbusTest") return;
        if (info.headers.member == "Hold" &&
            !info.headers.signature.length()) {
          heldFrame = ZuMv(frame);
          heldInfo = info;
          held.post();
          return;
        }
        if (info.headers.signature != "s") return;
        if (info.headers.member == "Fail") {
          srv.send([frame = ZuMv(frame), info,
              &failResSerial](uint32_t serial) {
            failResSerial.store_(serial);
            Zdbus_::HeadSpec head;
            head.type = Zdbus_::MessageType::Error;
            head.serial = serial;
            head.replySerial = info.serial;
            head.destination = info.headers.sender;
            head.errorName = "org.example.ZdbusTest.Error.Rejected";
            ErrorBody body{{"rejected"}, 23};
            return Zdbus_::message(head, body);
          });
          return;
        }
      }, {},
      [&failed, &serverReady](Zdbus_::CxnFailure) {
        ++failed;
        serverReady.post();
      });
    ZuCHECK(srv.route<EchoReq>([&srv, &routed, &noReplySeen,
        &echoResSerial](auto result) {
      using Value = ZuDecay<decltype(result)>;
      if constexpr (ZuIsSame<Value, ZdbusParsed<EchoReq>>{}) {
        if (result.object.value != "hello") return;
        ++routed;
        if (result.info.flags & Zdbus_::MessageFlag::NoReplyExpected) {
          noReplySeen.post();
          return;
        }
        srv.send([frame = ZuMv(result.frame), info = result.info,
            &echoResSerial](
            uint32_t serial) {
          echoResSerial.store_(serial);
          auto body = frame->cspan(info.bodyOffset);
          auto handler = ZfDBUS::handler<IDReply>({body, "s",
            info.bodyOffset, info.order});
          if (!handler) return Zdbus_::BuildResult{{}, {},
            Zdbus_::BuildError::Body};
          Zdbus_::HeadSpec head;
          head.type = Zdbus_::MessageType::MethodReturn;
          head.serial = serial;
          head.replySerial = info.serial;
          head.destination = info.headers.sender;
          return Zdbus_::message(head, handler.ctor());
        });
      }
      }), "exact Echo route registration failed");
    ZuCheck(!srv.route("/org/example/ZdbusTest",
      "org.example.ZdbusTest", "Echo",
      [](ZmRef<ZiIOBuf>, Zdbus_::FrameInfo) { }));
    srv.start();
    ZuCHECK(waitFor(serverReady) && !failed.load_(),
      "service name acquisition failed");

    Zdbus_::ClientParams cliParams;
    cliParams.pendingLimit = 1;
    cliParams.signalBatch = 1;
    cliParams.cxn.msgs = 1;
    cli.init(&sched, 1, 2, ZuMv(address), cliParams,
      [&ready](ZuCSpan name) {
        if (name.length()) ready.post();
      }, [&sched, &signalOK, &signalled, &signalBarrier,
          &signalSerial](
          ZmRef<ZiIOBuf> frame,
          Zdbus_::FrameInfo info) {
        if (info.headers.path == "/org/example/ZdbusTest" &&
            info.headers.interface == "org.example.ZdbusTest" &&
            info.headers.member == "Changed") {
          signalOK.store_(1);
          signalSerial.store_(info.serial);
          if (info.headers.signature == "s") {
            auto body = frame->cspan(info.bodyOffset);
            auto handler = ZfDBUS::handler<IDReply>({body, "s",
              info.bodyOffset, info.order});
            if (handler) {
              signalOK.store_(2);
              if (handler.ctor().value == "changed") signalOK.store_(3);
            }
          }
          signalled.post();
          sched.run([&signalBarrier] { signalBarrier.post(); }, 2);
        }
      }, {},
      [&failed, &ready](Zdbus_::CxnFailure) {
        ++failed;
        ready.post();
      });
    cli.start();
    bool connected = waitFor(ready) && !failed.load_();
    ZuCHECK(connected, "Hello did not complete");
    if (connected) {
      cli.call<GetIdReq>([&firstSerial](uint32_t serial) {
        firstSerial.store_(serial);
        Empty body;
        GetIdReq req;
        req.init(&body);
        return req.build(serial);
      }, [&good, &returned](auto result) {
        using Value = ZuDecay<decltype(result)>;
        if constexpr (ZuIsSame<Value, ZdbusParsed<GetIdRes>>{})
          if (result.object.value.length() && result.frame) ++good;
        returned.post();
      }, Zm::now(TimeoutSeconds));
      ZuCHECK(waitFor(returned), "GetId did not return");
      ZuCHECK(good.load_() == 1, "GetId response was invalid");

      cli.call([&secondSerial](uint32_t serial) {
        secondSerial.store_(serial);
        Zdbus_::HeadSpec head;
        head.type = Zdbus_::MessageType::MethodCall;
        head.serial = serial;
        head.path = "/org/freedesktop/DBus";
        head.interface = "org.freedesktop.DBus";
        head.member = "DefinitelyAbsentMethod";
        head.destination = "org.freedesktop.DBus";
        return Zdbus_::message(head, Empty{});
      }, [&errorOK, &errored, &secondSerial](Zdbus_::CallResult result) {
        if (result && result.frame &&
            result.info.type == Zdbus_::MessageType::Error &&
            result.info.headers.replySerial == secondSerial.load_() &&
            result.info.headers.errorName ==
              "org.freedesktop.DBus.Error.UnknownMethod") ++errorOK;
        errored.post();
      }, Zm::now(TimeoutSeconds));
      ZuCHECK(waitFor(errored), "unknown-method error did not return");
      ZuCHECK(firstSerial.load_() && secondSerial.load_() &&
        firstSerial.load_() != secondSerial.load_(),
        "outbound serials were not distinct");
      ZuCHECK(errorOK.load_() == 1, "named error was not correlated");

      cli.call([&echoReqSerial](uint32_t serial) {
        echoReqSerial.store_(serial);
        Zdbus_::HeadSpec head;
        head.type = Zdbus_::MessageType::MethodCall;
        head.serial = serial;
        head.path = "/org/example/ZdbusTest";
        head.interface = "org.example.ZdbusTest";
        head.member = "Echo";
        head.destination = "org.example.ZdbusTest";
        IDReply request{{"hello"}};
        return Zdbus_::message(head, request);
      }, [&echoOK, &echoed, &echoReqSerial,
          &echoResSerial](Zdbus_::CallResult result) {
        if (result && result.frame &&
            result.info.type == Zdbus_::MessageType::MethodReturn &&
            result.info.headers.signature == "s" &&
            result.info.serial == echoResSerial.load_() &&
            result.info.headers.replySerial == echoReqSerial.load_()) {
          auto body = result.frame->cspan(result.info.bodyOffset);
          auto handler = ZfDBUS::handler<IDReply>({body, "s",
            result.info.bodyOffset, result.info.order});
          if (handler && handler.ctor().value == "hello") ++echoOK;
        }
        echoed.post();
      }, Zm::now(TimeoutSeconds));
      ZuCHECK(waitFor(echoed), "service Echo did not return");
      ZuCHECK(echoOK.load_() == 1, "service Echo response was invalid");
      ZuCHECK(routed.load_() == 1, "exact Echo route was not selected");

      cli.send([&noReplySerial](uint32_t serial) {
        noReplySerial.store_(serial);
        Zdbus_::HeadSpec head;
        head.type = Zdbus_::MessageType::MethodCall;
        head.flags = Zdbus_::MessageFlag::NoReplyExpected;
        head.serial = serial;
        head.path = "/org/example/ZdbusTest";
        head.interface = "org.example.ZdbusTest";
        head.member = "Echo";
        head.destination = "org.example.ZdbusTest";
        return Zdbus_::message(head, IDReply{{"hello"}});
      }, [&noReplySent](bool ok) {
        if (ok) noReplySent.post();
      });
      ZuCHECK(waitFor(noReplySent) && waitFor(noReplySeen) &&
        routed.load_() == 2 && noReplySerial.load_() &&
        noReplySerial.load_() != firstSerial.load_() &&
        noReplySerial.load_() != secondSerial.load_(),
        "no-reply call did not use an independent serial");

      cli.call([](uint32_t serial) {
        Zdbus_::HeadSpec head;
        head.type = Zdbus_::MessageType::MethodCall;
        head.serial = serial;
        head.path = "/org/example/ZdbusTest";
        head.interface = "org.example.ZdbusTest";
        head.member = "Hold";
        head.destination = "org.example.ZdbusTest";
        return Zdbus_::message(head, Empty{});
      }, [&timeoutOK, &timedOut](Zdbus_::CallResult result) {
        if (result.error == Zdbus_::ClientError::Timeout) ++timeoutOK;
        timedOut.post();
      }, Zm::now(1));
      cli.call([](uint32_t serial) {
        Empty body;
        GetIdReq req;
        req.init(&body);
        return req.build(serial);
      }, [&queueOK, &queued](Zdbus_::CallResult result) {
        if (result.error == Zdbus_::ClientError::Queue) ++queueOK;
        queued.post();
      });
      ZuCHECK(waitFor(queued) && queueOK.load_() == 1,
        "pending-call limit did not report queue pressure");
      ZuCHECK(waitFor(held), "non-replying method was not received");
      ZuCHECK(waitFor(timedOut) && timeoutOK.load_() == 1,
        "non-replying method did not time out exactly once");
      auto recover = [&cli, &recoveryOK, &recovered] {
        cli.call<GetIdReq>([](uint32_t serial) {
          Empty body;
          GetIdReq req;
          req.init(&body);
          return req.build(serial);
        }, [&recoveryOK, &recovered](auto result) {
          using Value = ZuDecay<decltype(result)>;
          if constexpr (ZuIsSame<Value, ZdbusParsed<GetIdRes>>{})
            if (result.object.value.length()) ++recoveryOK;
          recovered.post();
        }, Zm::now(TimeoutSeconds));
      };
      recover();
      ZuCHECK(waitFor(recovered) && recoveryOK.load_() == 1,
        "pending-call slot was not released after timeout");

      cli.call([&cancelSerial](uint32_t serial) {
        cancelSerial.store_(serial);
        Zdbus_::HeadSpec head;
        head.type = Zdbus_::MessageType::MethodCall;
        head.serial = serial;
        head.path = "/org/example/ZdbusTest";
        head.interface = "org.example.ZdbusTest";
        head.member = "Hold";
        head.destination = "org.example.ZdbusTest";
        return Zdbus_::message(head, Empty{});
      }, [&cancelOK, &cancelCalls, &cancelled](
          Zdbus_::CallResult result) {
        ++cancelCalls;
        if (result.error == Zdbus_::ClientError::Cancelled) ++cancelOK;
        cancelled.post();
      }, Zm::now(TimeoutSeconds));
      ZuCHECK(waitFor(held) && cancelSerial.load_(),
        "cancellable method was not received");
      cli.cancel(cancelSerial.load_(), [&cancelAck](bool ok) {
        if (ok) cancelAck.post();
      });
      ZuCHECK(waitFor(cancelled) && waitFor(cancelAck) &&
        cancelOK.load_() == 1,
        "pending call was not cancelled exactly once");
      recover();
      ZuCHECK(waitFor(recovered) && recoveryOK.load_() == 2,
        "pending-call slot was not released after cancellation");

      cli.call<FailReq>([&failReqSerial](uint32_t serial) {
        failReqSerial.store_(serial);
        IDReply request{{"ignored"}};
        FailReq req;
        req.init(&request);
        return req.build(serial);
      }, [&rejectOK, &rejected, &failReqSerial,
          &failResSerial](auto result) {
        using Value = ZuDecay<decltype(result)>;
        if constexpr (ZuIsSame<Value, ZdbusParsed<RejectedErr>>{})
          if (result.object.code == 23 &&
              result.object.message == "rejected" && result.frame &&
              result.info.serial == failResSerial.load_() &&
              result.info.headers.replySerial == failReqSerial.load_())
            ++rejectOK;
        rejected.post();
      }, Zm::now(TimeoutSeconds));
      ZuCHECK(waitFor(rejected), "service error did not return");
      ZuCHECK(rejectOK.load_() == 1,
        "structured named error was invalid");

      auto subFn = [&subOK, &subSignalled](ZmRef<ZiIOBuf> frame,
          Zdbus_::FrameInfo info) {
        if (info.headers.signature == "s") {
          auto body = frame->cspan(info.bodyOffset);
          auto handler = ZfDBUS::handler<IDReply>({body, "s",
            info.bodyOffset, info.order});
          if (handler && handler.ctor().value == "changed") ++subOK;
        }
        subSignalled.post();
      };
      cli.subscribe<ChangedSig>([&subOK, &subSignalled](auto result) {
        using Value = ZuDecay<decltype(result)>;
        if constexpr (ZuIsSame<Value, ZdbusParsed<ChangedSig>>{})
          if (result.object.value == "changed") ++subOK;
        subSignalled.post();
      }, [&firstSub, &subAdded](uint64_t id) {
          firstSub.store_(id);
          subAdded.post();
        });
      cli.subscribe("/org/example/ZdbusTest", "org.example.ZdbusTest",
        "Changed", subFn, [&secondSub, &subAdded](uint64_t id) {
          secondSub.store_(id);
          subAdded.post();
        });
      ZuCHECK(waitFor(subAdded) && waitFor(subAdded) &&
        firstSub.load_() && secondSub.load_() &&
        firstSub.load_() != secondSub.load_(),
        "signal subscriptions were not registered");

      constexpr auto matchRule =
        "type='signal',interface='org.example.ZdbusTest'"_Zu;
      cli.addMatch(matchRule, [&matched](Zdbus_::CallResult result) {
        if (result && result.info.type ==
            Zdbus_::MessageType::MethodReturn &&
            !result.info.headers.signature.length()) matched.post();
      }, Zm::now(TimeoutSeconds));
      ZuCHECK(waitFor(matched), "AddMatch did not return");
      Zdbus_::SubText heldSender{heldInfo.headers.sender};
      srv.send([heldSender = ZuMv(heldSender),
          replySerial = heldInfo.serial](uint32_t serial) {
        Zdbus_::HeadSpec head;
        head.type = Zdbus_::MessageType::MethodReturn;
        head.serial = serial;
        head.replySerial = replySerial;
        head.destination = heldSender;
        return Zdbus_::message(head, IDReply{{"late"}});
      }, [&lateSent](bool ok) {
        if (ok) lateSent.post();
      });
      ZuCHECK(waitFor(lateSent), "late return was not sent");
      srv.send([&signalOutSerial](uint32_t serial) {
        signalOutSerial.store_(serial);
        Zdbus_::HeadSpec head;
        head.type = Zdbus_::MessageType::Signal;
        head.serial = serial;
        head.path = "/org/example/ZdbusTest";
        head.interface = "org.example.ZdbusTest";
        head.member = "Changed";
        IDReply body{{"changed"}};
        return Zdbus_::message(head, body);
      });
      ZuCHECK(waitFor(signalled), "service signal did not arrive");
      ZuCHECK(waitFor(signalBarrier) && cancelCalls.load_() == 1,
        "late return completed the cancelled call again");
      ZuCHECK(signalOK.load_() >= 1, "service signal headers invalid");
      ZuCHECK(signalOK.load_() >= 2, "service signal body invalid");
      ZuCHECK(signalOK.load_() == 3, "service signal value invalid");
      ZuCHECK(echoResSerial.load_() && failResSerial.load_() &&
        signalSerial.load_() &&
        signalSerial.load_() == signalOutSerial.load_() &&
        echoResSerial.load_() != failResSerial.load_() &&
        echoResSerial.load_() != signalSerial.load_() &&
        failResSerial.load_() != signalSerial.load_(),
        "service return/error/signal did not use fresh serials");
      ZuCHECK(waitFor(subSignalled) && waitFor(subSignalled) &&
        subOK.load_() == 2,
        "keyed signal fan-out did not complete across batches");
      cli.unsubscribe("/org/example/ZdbusTest",
        "org.example.ZdbusTest", "Changed", firstSub.load_(),
        [&subRemoved](bool ok) { if (ok) subRemoved.post(); });
      ZuCHECK(waitFor(subRemoved), "signal unsubscribe failed");
      srv.send([](uint32_t serial) {
        Zdbus_::HeadSpec head;
        head.type = Zdbus_::MessageType::Signal;
        head.serial = serial;
        head.path = "/org/example/ZdbusTest";
        head.interface = "org.example.ZdbusTest";
        head.member = "Changed";
        return Zdbus_::message(head, IDReply{{"changed"}});
      });
      ZuCHECK(waitFor(subSignalled) && subOK.load_() == 3,
        "unsubscribed signal handler was still dispatched");
      cli.removeMatch(matchRule,
        [&unmatched](Zdbus_::CallResult result) {
          if (result && result.info.type ==
              Zdbus_::MessageType::MethodReturn &&
              !result.info.headers.signature.length()) unmatched.post();
        }, Zm::now(TimeoutSeconds));
      ZuCHECK(waitFor(unmatched), "RemoveMatch did not return");
      for (unsigned i = 0; i < 2; ++i)
        cli.subscribe("/org/example/ZdbusTest",
          "org.example.ZdbusTest", "Changed",
          [](ZmRef<ZiIOBuf>, Zdbus_::FrameInfo) { },
          [&subAdded](uint64_t id) {
            if (id) subAdded.post();
          });
      ZuCHECK(waitFor(subAdded) && waitFor(subAdded),
        "stop-cleanup subscriptions were not registered");
    }
    sched.run([&cli, &stopped, &secondStopped] {
      cli.stop([&cli, &stopped] {
        cli.final();
        stopped.post();
      });
      cli.stop([&secondStopped] { secondStopped.post(); });
    }, 2);
    ZuCHECK(waitFor(stopped) && waitFor(secondStopped),
      "client stop callbacks timed out");
    srv.stop([&serverStopped] { serverStopped.post(); });
    ZuCHECK(waitFor(serverStopped), "service stop timed out");
    srv.final();
    sched.stop();
    ZuCHECK(!failed.load_(), "client connection failed");
  }
  ::close(input[1]);
  int status;
  ZuCHECK(ZdbusTestTool::waitChild(child, 0, status) &&
    WIFEXITED(status) && !WEXITSTATUS(status),
    "private bus fixture did not stop cleanly");
}

int main(int argc, char **argv)
{
  if (!ZdbusTestTool::available("dbus-daemon")) {
    constexpr char skip[] = "1..0 # SKIP dbus-daemon unavailable\n";
    ::write(STDOUT_FILENO, skip, sizeof(skip) - 1);
    return 0;
  }
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(client);
  return 0;
}
