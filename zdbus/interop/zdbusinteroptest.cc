//  -*- mode:c++; indent-tabs-mode:t; tab-width=8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Native client against the independently built libdbus-1 reference service

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuUnion.hh>
#include <zlib/ZmAtomic.hh>
#include <zlib/ZmLHash.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZdbusClient.hh>
#include <zlib/ZdbusServer.hh>

#include "ZfMapTest.hh"
#include "../util/ZdbusTestTool.hh"

using namespace ZuTestUtil;

namespace Interop {

enum { TimeoutSeconds = 5 };

struct Process {
  pid_t pid = -1;
  int input = -1;
  int output = -1;

  ~Process() { finish(false); }

  bool start(const char *path, const char *arg1 = nullptr,
    const char *arg2 = nullptr) {
    // pipe2's two-int arrays are required by the OS ABI.
    int in[2];
    int out[2];
    if (::pipe2(in, O_CLOEXEC)) return false;
    if (::pipe2(out, O_CLOEXEC)) {
      ::close(in[0]); ::close(in[1]);
      return false;
    }
    pid_t child = ::fork();
    if (child < 0) {
      ::close(in[0]); ::close(in[1]);
      ::close(out[0]); ::close(out[1]);
      return false;
    }
    if (!child) {
      ::close(in[1]);
      ::close(out[0]);
      ::dup2(in[0], STDIN_FILENO);
      ::dup2(out[1], STDOUT_FILENO);
      ::close(in[0]);
      ::close(out[1]);
      if (arg2) {
        ::alarm(15);
        ::execl(path, path, arg1, arg2, static_cast<char *>(nullptr));
      } else if (arg1)
        ::execl(path, path, arg1, static_cast<char *>(nullptr));
      else ::execl(path, path, static_cast<char *>(nullptr));
      _exit(errno == ENOENT ? 127 : 126);
    }
    ::close(in[0]);
    ::close(out[1]);
    pid = child;
    input = in[1];
    output = out[0];
    return true;
  }

  template <typename Text>
  bool line(Text &text) {
    return ZdbusTestTool::line(output, text);
  }

  bool finish(bool graceful) {
    if (input >= 0) { ::close(input); input = -1; }
    if (output >= 0) { ::close(output); output = -1; }
    if (pid <= 0) return true;
    int status;
    bool reaped = ZdbusTestTool::waitChild(pid,
      graceful ? 0 : SIGTERM, status);
    if (reaped) pid = -1;
    return reaped && (!graceful ||
      (WIFEXITED(status) && !WEXITSTATUS(status)));
  }
};

static bool waitFor(ZmSemaphore &sem)
{
  return sem.timedwait(Zm::now(TimeoutSeconds)) == 0;
}

ZuDerive(Text, ZtString<ZtStringHeapID<"ZdbusInterop.Text">>);

struct Empty { };
ZuTypeList<> ZuFields_(Empty *, ZuFacet::DBUS *);
struct TextBody { Text text; };
ZfStruct(, TextBody, (((text), (Mutable)), (String)));
ZfStructRender(, TextBody, DBUS, text);
struct ErrorBody { uint32_t code; Text text; };
ZfStruct(, ErrorBody,
  (((code), (Mutable)), (UInt32)),
  (((text), (Mutable)), (String)));
ZfStructRender(, ErrorBody, DBUS, code, text);
struct UIntBody { uint32_t value; };
ZfStruct(, UIntBody, (((value), (Mutable)), (UInt32)));
ZfStructRender(, UIntBody, DBUS, value);
struct Inner { uint16_t small; uint32_t count; };
using Values = ZtArray<uint32_t,
  ZtArrayHeapID<"ZdbusInterop.Values">>;
struct Variant : ZuUnion<uint32_t, Text, ZfDBUS::Any> {
  ZuDerive_(Variant, (ZuUnion<uint32_t, Text, ZfDBUS::Any>));
  friend ZuDefaultRDecayer ZuRDecayer(Variant *);
  friend ZfDBUS::AsVariant ZfDBUS_Fmt(Variant *);
};
using Dict = ZfMapTest<"ZdbusInterop.Dict",
  ZmLHashKV<Text, Variant>>;
inline ZfDBUS::AsMap<> ZfDBUS_Fmt(Dict *);
struct ComplexBody {
  Inner inner;
  Values values;
  Dict dict;
  Variant choice;
};
ZfStruct(, Inner,
  (((small), (Mutable)), (UInt16)),
  (((count), (Mutable)), (UInt32)));
ZfStructRender(, Inner, DBUS, small, count);
ZfStruct(, ComplexBody,
  (((inner), (Mutable)), (UDT)),
  (((values), (Mutable)), (UInt32Vec)),
  (((dict), (Mutable)), (UDT)),
  (((choice), (Mutable)), (UDT)));
ZfStructRender(, ComplexBody, DBUS, inner, values, dict, choice);

static void initComplex(ComplexBody &body)
{
  body.inner = {9, 42};
  body.values.push(1);
  body.values.push(2);
  body.dict.add("answer", Variant{uint32_t(42)});
  body.choice = Variant{Text{"tag"}};
}

static bool complexOK(const ComplexBody &body)
{
  if (body.inner.small != 9 || body.inner.count != 42 ||
      body.values.length() != 2 || body.values[0] != 1 ||
      body.values[1] != 2 || body.dict.count_() != 1 ||
      !body.choice.is<Text>() || body.choice.p<Text>() != "tag")
    return false;
  auto entry = body.dict.find("answer");
  return entry && entry->template p<1>().template is<uint32_t>() &&
    entry->template p<1>().template p<uint32_t>() == 42;
}

using PeerHeaders = ZuTypeList<
  Zdbus_::HeaderEntry<Zdbus_::Header::Path,
    ZuStringT<"/org/example/ZdbusPeer">>,
  Zdbus_::HeaderEntry<Zdbus_::Header::Interface,
    ZuStringT<"org.example.ZdbusPeer">>,
  Zdbus_::HeaderEntry<Zdbus_::Header::Destination,
    ZuStringT<"org.example.ZdbusPeer">>>;
template <typename Member>
using PeerMethod = typename PeerHeaders::template Push<
  Zdbus_::HeaderEntry<Zdbus_::Header::Member, Member>>;
struct TextRes : ZdbusResParser<TextRes, TextBody> { };
using RejectedHeaders = ZuTypeList<
  Zdbus_::HeaderEntry<Zdbus_::Header::ErrorName,
    ZuStringT<"org.example.ZdbusPeer.Rejected">>>;
struct RejectedErr : ZdbusErrParser<RejectedErr, ErrorBody,
  RejectedHeaders> { };
struct OddReq : ZdbusReqBuilder<OddReq, Empty,
  PeerMethod<ZuStringT<"OddReply">>> {
  using Responses = ZuTypeList<TextRes>;
};
struct OtherReq : ZdbusReqBuilder<OtherReq, Empty,
  PeerMethod<ZuStringT<"OtherError">>> {
  using Errors = ZuTypeList<RejectedErr>;
};

static Zdbus_::HeadSpec peerHead(uint32_t serial, ZuCSpan member)
{
  Zdbus_::HeadSpec head;
  head.type = Zdbus_::MessageType::MethodCall;
  head.serial = serial;
  head.path = "/org/example/ZdbusPeer";
  head.interface = "org.example.ZdbusPeer";
  head.member = member;
  head.destination = "org.example.ZdbusPeer";
  return head;
}

static Zdbus_::HeadSpec replyHead(uint32_t serial,
  const Zdbus_::FrameInfo &info, unsigned kind)
{
  Zdbus_::HeadSpec head;
  head.type = kind;
  head.serial = serial;
  head.replySerial = info.serial;
  head.destination = info.headers.sender;
  return head;
}

static Zdbus_::HeadSpec signalHead(uint32_t serial)
{
  Zdbus_::HeadSpec head;
  head.type = Zdbus_::MessageType::Signal;
  head.serial = serial;
  head.path = "/org/example/ZdbusPeer";
  head.interface = "org.example.ZdbusPeer";
  head.member = "Changed";
  return head;
}

static void clientToPeer()
{
  ZuTestScope(clientToPeer);
  const char *fixture = ZiStat{"../util/zdbusbus"}.exists() ?
    "../util/zdbusbus" : "zdbus/util/zdbusbus";
  const char *peerPath = ZiStat{"../util/zdbuspeer"}.exists() ?
    "../util/zdbuspeer" : "zdbus/util/zdbuspeer";
  Process bus;
  ZuCHECK(bus.start(fixture), "private-bus fixture failed to start");
  if (bus.pid <= 0) return;
  ZdbusAddress::Text addressText;
  ZdbusAddress address;
  ZuCHECK(bus.line(addressText) &&
    ZdbusAddress::parse(address, addressText),
    "private bus did not report an address");
  if (!address) return;
  Process peer;
  ZuCHECK(peer.start(peerPath, addressText.data(), "service"),
    "libdbus service failed to start");
  if (peer.pid <= 0) return;
  Text readyLine;
  ZuCHECK(peer.line(readyLine) && readyLine == "READY",
    "libdbus service did not become ready");
  if (readyLine != "READY") return;

  ZmScheduler sched{ZmSchedParams().id("ZdbusInteropTest").nThreads(2)};
  ZdbusClient cli;
  ZmSemaphore ready;
  ZmSemaphore echoDone;
  ZmSemaphore complexDone;
  ZmSemaphore failDone;
  ZmSemaphore oddDone;
  ZmSemaphore otherDone;
  ZmSemaphore matchDone;
  ZmSemaphore unmatchDone;
  ZmSemaphore emitDone;
  ZmSemaphore signalDone;
  ZmSemaphore quitDone;
  ZmSemaphore stopped;
  ZmAtomic<unsigned> failures = 0;
  ZmAtomic<unsigned> echoOK = 0;
  ZmAtomic<unsigned> complexPassed = 0;
  ZmAtomic<unsigned> failOK = 0;
  ZmAtomic<unsigned> oddOK = 0;
  ZmAtomic<unsigned> otherOK = 0;
  ZmAtomic<unsigned> signalOK = 0;
  ZmAtomic<unsigned> matchOK = 0;
  ZmAtomic<unsigned> unmatchOK = 0;
  ZmAtomic<unsigned> emitOK = 0;
  ZmAtomic<unsigned> quitOK = 0;
  ZmAtomic<uint32_t> echoSerial = 0;
  ZmAtomic<uint32_t> complexSerial = 0;
  ZmAtomic<uint32_t> failSerial = 0;
  ZmAtomic<uint32_t> otherSerial = 0;

  sched.start();
  cli.init(&sched, 1, 2, ZuMv(address), {},
    [&ready](ZuCSpan name) { if (name.length()) ready.post(); },
    [&signalOK, &signalDone](ZmRef<ZiIOBuf> frame,
        Zdbus_::FrameInfo info) {
      if (info.headers.path != "/org/example/ZdbusPeer" ||
          info.headers.interface != "org.example.ZdbusPeer" ||
          info.headers.member != "Changed") return;
      if (info.headers.signature == "s") {
        auto handler = ZfDBUS::handler<TextBody>({
          frame->cspan(info.bodyOffset), "s", info.bodyOffset, info.order});
        if (handler && handler.ctor().text == "changed") ++signalOK;
      }
      signalDone.post();
    }, {}, [&failures, &ready](Zdbus_::CxnFailure) {
      ++failures;
      ready.post();
    });
  cli.start();
  bool connected = waitFor(ready) && !failures.load_();
  ZuCHECK(connected, "native client Hello failed");
  if (connected) {
    cli.call([&echoSerial](uint32_t serial) {
      echoSerial.store_(serial);
      return Zdbus_::message(peerHead(serial, "Echo"),
        TextBody{{"hello"}});
    }, [&echoOK, &echoDone, &echoSerial](Zdbus_::CallResult result) {
      if (result && result.frame &&
          result.info.type == Zdbus_::MessageType::MethodReturn &&
          result.info.headers.replySerial == echoSerial.load_() &&
          result.info.headers.signature == "s") {
        auto handler = ZfDBUS::handler<TextBody>({
          result.frame->cspan(result.info.bodyOffset), "s",
          result.info.bodyOffset, result.info.order});
        if (handler && handler.ctor().text == "hello") ++echoOK;
      }
      echoDone.post();
    }, Zm::now(TimeoutSeconds));
    ZuCHECK(waitFor(echoDone) && echoOK.load_() == 1,
      "libdbus Echo return did not round-trip");

    cli.call([&complexSerial](uint32_t serial) {
      complexSerial.store_(serial);
      ComplexBody body;
      initComplex(body);
      return Zdbus_::message(peerHead(serial, "Complex"), body);
    }, [&complexPassed, &complexDone, &complexSerial](
        Zdbus_::CallResult result) {
      if (result && result.frame &&
          result.info.type == Zdbus_::MessageType::MethodReturn &&
          result.info.headers.replySerial == complexSerial.load_() &&
          result.info.headers.signature == "(qu)aua{sv}v") {
        auto handler = ZfDBUS::handler<ComplexBody>({
          result.frame->cspan(result.info.bodyOffset),
          result.info.headers.signature, result.info.bodyOffset,
          result.info.order});
        if (handler) {
          ComplexBody body;
          handler.load(body);
          if (complexOK(body)) ++complexPassed;
        }
      }
      complexDone.post();
    }, Zm::now(TimeoutSeconds));
    ZuCHECK(waitFor(complexDone) && complexPassed.load_() == 1,
      "libdbus complex return did not round-trip");

    cli.call([&failSerial](uint32_t serial) {
      failSerial.store_(serial);
      return Zdbus_::message(peerHead(serial, "Fail"),
        TextBody{{"ignored"}});
    }, [&failOK, &failDone, &failSerial](Zdbus_::CallResult result) {
      if (result && result.frame &&
          result.info.type == Zdbus_::MessageType::Error &&
          result.info.headers.replySerial == failSerial.load_() &&
          result.info.headers.errorName ==
            "org.example.ZdbusPeer.Rejected" &&
          result.info.headers.signature == "us") {
        auto handler = ZfDBUS::handler<ErrorBody>({
          result.frame->cspan(result.info.bodyOffset), "us",
          result.info.bodyOffset, result.info.order});
        if (handler) {
          auto body = handler.ctor();
          if (body.code == 23 && body.text == "rejected") ++failOK;
        }
      }
      failDone.post();
    }, Zm::now(TimeoutSeconds));
    ZuCHECK(waitFor(failDone) && failOK.load_() == 1,
      "libdbus structured named error did not round-trip");
    ZuCheck(echoSerial.load_() && failSerial.load_() &&
      echoSerial.load_() != failSerial.load_());

    cli.call<OddReq>([](uint32_t serial) {
      Empty body;
      OddReq req;
      req.init(&body);
      return req.build(serial);
    }, [&oddOK, &oddDone](auto result) {
      using Value = ZuDecay<decltype(result)>;
      if constexpr (ZuIsSame<Value, ZdbusTypedFault>{})
        if (result.error == Zdbus_::TypedError::Signature) ++oddOK;
      oddDone.post();
    }, Zm::now(TimeoutSeconds));
    ZuCHECK(waitFor(oddDone) && oddOK.load_() == 1,
      "mismatched libdbus reply signature was not rejected");

    cli.call<OtherReq>([&otherSerial](uint32_t serial) {
      otherSerial.store_(serial);
      Empty body;
      OtherReq req;
      req.init(&body);
      return req.build(serial);
    }, [&otherOK, &otherDone, &otherSerial](auto result) {
      using Value = ZuDecay<decltype(result)>;
      if constexpr (ZuIsSame<Value, ZdbusRemoteError>{}) {
        auto handler = ZfDBUS::handler<UIntBody>({result.body(),
          result.signature(), result.info.bodyOffset, result.info.order});
        if (result.name() == "org.example.ZdbusPeer.Other" &&
            result.info.headers.replySerial == otherSerial.load_() &&
            handler && handler.ctor().value == 99) ++otherOK;
      }
      otherDone.post();
    }, Zm::now(TimeoutSeconds));
    ZuCHECK(waitFor(otherDone) && otherOK.load_() == 1,
      "unknown named libdbus error was not retained");

    constexpr auto matchRule =
      "type='signal',interface='org.example.ZdbusPeer'"_Zu;
    cli.addMatch(matchRule,
      [&matchOK, &matchDone](Zdbus_::CallResult result) {
        if (result && result.info.type ==
            Zdbus_::MessageType::MethodReturn) ++matchOK;
        matchDone.post();
      }, Zm::now(TimeoutSeconds));
    ZuCHECK(waitFor(matchDone) && matchOK.load_() == 1,
      "AddMatch did not return");

    cli.call([](uint32_t serial) {
      return Zdbus_::message(peerHead(serial, "Emit"), Empty{});
    }, [&emitOK, &emitDone](Zdbus_::CallResult result) {
      if (result && result.info.type ==
          Zdbus_::MessageType::MethodReturn) ++emitOK;
      emitDone.post();
    }, Zm::now(TimeoutSeconds));
    ZuCHECK(waitFor(emitDone) && emitOK.load_() == 1,
      "libdbus Emit did not return");
    ZuCHECK(waitFor(signalDone) && signalOK.load_() == 1,
      "libdbus signal did not round-trip");

    cli.removeMatch(matchRule,
      [&unmatchOK, &unmatchDone](Zdbus_::CallResult result) {
        if (result && result.info.type ==
            Zdbus_::MessageType::MethodReturn) ++unmatchOK;
        unmatchDone.post();
      }, Zm::now(TimeoutSeconds));
    ZuCHECK(waitFor(unmatchDone) && unmatchOK.load_() == 1,
      "RemoveMatch did not return");

    cli.call([](uint32_t serial) {
      return Zdbus_::message(peerHead(serial, "Quit"), Empty{});
    }, [&quitOK, &quitDone](Zdbus_::CallResult result) {
      if (result && result.info.type ==
          Zdbus_::MessageType::MethodReturn) ++quitOK;
      quitDone.post();
    }, Zm::now(TimeoutSeconds));
    ZuCHECK(waitFor(quitDone) && quitOK.load_() == 1,
      "libdbus Quit did not return");
  }
  cli.stop([&stopped] { stopped.post(); });
  ZuCHECK(waitFor(stopped), "native client stop timed out");
  cli.final();
  sched.stop();
  ZuCheck(!failures.load_());
  ZuCHECK(peer.finish(true), "libdbus service exited unsuccessfully");
  ZuCHECK(bus.finish(true), "private bus fixture exited unsuccessfully");
}

static void peerToServer()
{
  ZuTestScope(peerToServer);
  const char *fixture = ZiStat{"../util/zdbusbus"}.exists() ?
    "../util/zdbusbus" : "zdbus/util/zdbusbus";
  const char *peerPath = ZiStat{"../util/zdbuspeer"}.exists() ?
    "../util/zdbuspeer" : "zdbus/util/zdbuspeer";
  Process bus;
  ZuCHECK(bus.start(fixture), "private-bus fixture failed to start");
  if (bus.pid <= 0) return;
  ZdbusAddress::Text addressText;
  ZdbusAddress address;
  ZuCHECK(bus.line(addressText) &&
    ZdbusAddress::parse(address, addressText),
    "private bus did not report an address");
  if (!address) return;

  ZmScheduler sched{ZmSchedParams().id("ZdbusInteropSrv").nThreads(2)};
  ZdbusServer srv;
  ZmSemaphore ready;
  ZmSemaphore stopped;
  ZmSemaphore signalSent;
  ZmAtomic<unsigned> failures = 0;
  ZmAtomic<unsigned> echoCalls = 0;
  ZmAtomic<unsigned> complexCalls = 0;
  ZmAtomic<unsigned> failCalls = 0;
  ZmAtomic<unsigned> emitCalls = 0;
  sched.start();
  srv.init(&sched, 1, 2, ZuMv(address),
    "org.example.ZdbusService", {},
    [&ready](ZuCSpan name) {
      if (name == "org.example.ZdbusService") ready.post();
    }, {}, {}, [&failures, &ready](Zdbus_::CxnFailure) {
      ++failures;
      ready.post();
    });
  ZuCHECK(srv.route("/org/example/ZdbusPeer",
    "org.example.ZdbusPeer", "Echo",
    [&srv, &echoCalls](ZmRef<ZiIOBuf> frame,
        Zdbus_::FrameInfo info) {
      ++echoCalls;
      srv.send([frame = ZuMv(frame), info](uint32_t serial) {
        auto handler = ZfDBUS::handler<TextBody>({
          frame->cspan(info.bodyOffset), info.headers.signature,
          info.bodyOffset, info.order});
        if (!handler) return Zdbus_::BuildResult{{}, {},
          Zdbus_::BuildError::Body};
        return Zdbus_::message(replyHead(serial, info,
          Zdbus_::MessageType::MethodReturn), handler.ctor());
      });
    }), "Echo route registration failed");
  ZuCHECK(srv.route("/org/example/ZdbusPeer",
    "org.example.ZdbusPeer", "Complex",
    [&srv, &complexCalls](ZmRef<ZiIOBuf> frame,
        Zdbus_::FrameInfo info) {
      ++complexCalls;
      srv.send([frame = ZuMv(frame), info](uint32_t serial) {
        auto handler = ZfDBUS::handler<ComplexBody>({
          frame->cspan(info.bodyOffset), info.headers.signature,
          info.bodyOffset, info.order});
        if (!handler) return Zdbus_::BuildResult{{}, {},
          Zdbus_::BuildError::Body};
        ComplexBody request;
        handler.load(request);
        if (!complexOK(request))
          return Zdbus_::BuildResult{{}, {},
            Zdbus_::BuildError::Body};
        ComplexBody reply;
        initComplex(reply);
        return Zdbus_::message(replyHead(serial, info,
          Zdbus_::MessageType::MethodReturn), reply);
      });
    }), "Complex route registration failed");
  ZuCHECK(srv.route("/org/example/ZdbusPeer",
    "org.example.ZdbusPeer", "Fail",
    [&srv, &failCalls](ZmRef<ZiIOBuf> frame,
        Zdbus_::FrameInfo info) {
      ++failCalls;
      srv.send([frame = ZuMv(frame), info](uint32_t serial) {
        auto head = replyHead(serial, info, Zdbus_::MessageType::Error);
        head.errorName = "org.example.ZdbusPeer.Rejected";
        return Zdbus_::message(head, ErrorBody{23, {"rejected"}});
      });
    }), "Fail route registration failed");
  ZuCHECK(srv.route("/org/example/ZdbusPeer",
    "org.example.ZdbusPeer", "Emit",
    [&srv, &emitCalls](ZmRef<ZiIOBuf> frame,
        Zdbus_::FrameInfo info) {
      ++emitCalls;
      srv.send([frame, info](uint32_t serial) {
        return Zdbus_::message(replyHead(serial, info,
          Zdbus_::MessageType::MethodReturn), Empty{});
      });
      srv.send([](uint32_t serial) {
        return Zdbus_::message(signalHead(serial),
          TextBody{{"changed"}});
      });
    }), "Emit route registration failed");
  srv.start();
  bool running = waitFor(ready) && !failures.load_();
  ZuCHECK(running, "native server did not acquire its name");
  if (running) {
    Process caller;
    ZuCHECK(caller.start(peerPath, addressText.data(), "caller"),
      "libdbus caller failed to start");
    if (caller.pid > 0) {
      Text readyLine;
      ZuCHECK(caller.line(readyLine) && readyLine == "READY",
        "libdbus caller did not become ready");
      ZuCHECK(caller.finish(true), "libdbus caller rejected native service");
    }

    Process subscriber;
    ZuCHECK(subscriber.start(peerPath, addressText.data(), "subscriber"),
      "libdbus subscriber failed to start");
    if (subscriber.pid > 0) {
      Text readyLine;
      ZuCHECK(subscriber.line(readyLine) && readyLine == "READY",
        "libdbus subscriber did not install its match rule");
      if (readyLine == "READY") {
        srv.send([](uint32_t serial) {
          return Zdbus_::message(signalHead(serial),
            TextBody{{"changed"}});
        }, [&signalSent](bool ok) {
          if (ok) signalSent.post();
        });
        ZuCHECK(waitFor(signalSent), "native signal was not sent");
        ZuCHECK(subscriber.finish(true),
          "libdbus subscriber rejected native signal");
      }
    }
  }
  srv.stop([&stopped] { stopped.post(); });
  ZuCHECK(waitFor(stopped), "native server stop timed out");
  srv.final();
  sched.stop();
  ZuCheck(!failures.load_());
  ZuCheck(echoCalls.load_() == 1);
  ZuCheck(complexCalls.load_() == 1);
  ZuCheck(failCalls.load_() == 1);
  ZuCheck(emitCalls.load_() == 1);
  ZuCHECK(bus.finish(true), "private bus fixture exited unsuccessfully");
}

} // Interop

int main(int argc, char **argv)
{
  if (!ZdbusTestTool::available("dbus-daemon")) {
    constexpr char skip[] = "1..0 # SKIP dbus-daemon unavailable\n";
    ::write(STDOUT_FILENO, skip, sizeof(skip) - 1);
    return 0;
  }
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(Interop::clientToPeer);
  ZuTestCall(Interop::peerToServer);
  return 0;
}
