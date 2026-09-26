//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Vault against a controlled Secret Service on a private session bus.

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuHex.hh>
#include <zlib/ZmAtomic.hh>
#include <zlib/ZmBlock.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZiFile.hh>
#include <zlib/ZdbusServer.hh>
#include <zlib/ZtlsRandom.hh>
#include <zlib/ZtlsVault.hh>

#include "../../zdbus/util/ZdbusTestTool.hh"
#include "../src/ZtlsVaultSS.hh"

using namespace ZuTestUtil;

namespace VaultDBusTest_ {

using namespace Ztls_::SS;

constexpr auto ServicePath = "/org/freedesktop/secrets"_Zu;
constexpr auto SessionPath = "/org/freedesktop/secrets/session/test"_Zu;
constexpr auto CollectionPath = "/org/freedesktop/secrets/collection/test"_Zu;
constexpr auto ItemPath = "/org/freedesktop/secrets/collection/test/1"_Zu;
constexpr auto PromptObjPath = "/org/freedesktop/secrets/prompt/test"_Zu;

struct PeerState {
  Text key;
  ZtBArray<ZtArrayHeapID<"Ztls.Vault.ITest.Value">> value;
  unsigned sessions = 0;
  unsigned collections = 0;
  unsigned items = 0;
  unsigned secrets = 0;
  unsigned closes = 0;
  unsigned prompts = 0;
  bool hasCollection = false;
  bool promptItem = false;
  bool dismiss = false;
  bool withhold = false;
};

static Zdbus_::HeadSpec replyHead(uint32_t serial,
    const Zdbus_::FrameInfo &info)
{
  Zdbus_::HeadSpec head;
  head.type = Zdbus_::MessageType::MethodReturn;
  head.serial = serial;
  head.replySerial = info.serial;
  head.destination = info.headers.sender;
  return head;
}

template <typename Body>
static bool parse(const ZmRef<ZiIOBuf> &frame,
    const Zdbus_::FrameInfo &info, Body &body)
{
  auto handler = ZfDBUS::handler<Body>({frame->cspan(info.bodyOffset),
    info.headers.signature, info.bodyOffset, info.order});
  if (!handler) return false;
  handler.load(body);
  return true;
}

static void method(ZdbusServer &server, PeerState &state,
    ZmRef<ZiIOBuf> frame, Zdbus_::FrameInfo info)
{
  server.send([frame = ZuMv(frame), info, &state, &server](uint32_t serial) {
    auto head = replyHead(serial, info);
    if (info.headers.member == "OpenSession" &&
        info.headers.path == ServicePath) {
      OpenSessionArg arg;
      if (!parse(frame, info, arg) || arg.algorithm != "plain")
        return Zdbus_::BuildResult{{}, {}, Zdbus_::BuildError::Body};
      ++state.sessions;
      return Zdbus_::message(head,
        OpenSessionRes{Variant{Text{""}}, Text{SessionPath}});
    }
    if (info.headers.member == "Close" &&
        info.headers.path == SessionPath) {
      ++state.closes;
      return Zdbus_::message(head, Empty{});
    }
    if (info.headers.member == "SearchItems" &&
        info.headers.path == ServicePath) {
      SearchArg arg;
      if (!parse(frame, info, arg))
        return Zdbus_::BuildResult{{}, {}, Zdbus_::BuildError::Body};
      SearchRes reply;
      if (state.key &&
          arg.attributes.attrs->findVal("key") == state.key) {
        if (state.key == "global/locked")
          reply.locked.push(Text{ItemPath});
        else
          reply.unlocked.push(Text{ItemPath});
      }
      return Zdbus_::message(head, reply);
    }
    if (info.headers.member == "ReadAlias" &&
        info.headers.path == ServicePath) {
      Text path;
      if (state.hasCollection) path = CollectionPath;
      else path = "/";
      return Zdbus_::message(head, ReadAliasRes{ZuMv(path)});
    }
    if (info.headers.member == "CreateCollection" &&
        info.headers.path == ServicePath) {
      CreateCollectionArg arg;
      if (!parse(frame, info, arg))
        return Zdbus_::BuildResult{{}, {}, Zdbus_::BuildError::Body};
      ++state.collections;
      state.hasCollection = true;
      return Zdbus_::message(head,
        CreateCollectionRes{Text{CollectionPath}, Text{"/"}});
    }
    if (info.headers.member == "Unlock" &&
        info.headers.path == ServicePath) {
      UnlockArg arg;
      if (!parse(frame, info, arg))
        return Zdbus_::BuildResult{{}, {}, Zdbus_::BuildError::Body};
      return Zdbus_::message(head,
        UnlockRes{ZuMv(arg.objects), Text{"/"}});
    }
    if (info.headers.member == "CreateItem" &&
        info.headers.path == CollectionPath) {
      CreateItemArg arg;
      if (!parse(frame, info, arg))
        return Zdbus_::BuildResult{{}, {}, Zdbus_::BuildError::Body};
      auto attrs = arg.properties.find(
        "org.freedesktop.Secret.Item.Attributes");
      if (!attrs || !attrs->val().is<AttrsView>())
        return Zdbus_::BuildResult{{}, {}, Zdbus_::BuildError::Body};
      Text requestedKey{
        attrs->val().p<AttrsView>().attrs->findVal("key")};
      if (requestedKey == "global/denied") {
        head.type = Zdbus_::MessageType::Error;
        head.errorName = "org.freedesktop.Secret.Error.IsLocked";
        return Zdbus_::message(head, Empty{});
      }
      state.key = ZuMv(requestedKey);
      state.value = decltype(state.value){arg.secret.value};
      ++state.items;
      state.promptItem = state.key == "global/prompted" ||
        state.key == "global/dismissed" ||
        state.key == "global/timedout";
      state.dismiss = state.key == "global/dismissed";
      state.withhold = state.key == "global/timedout";
      Text item = state.promptItem ? Text{"/"} : Text{ItemPath};
      Text prompt = state.promptItem ? Text{PromptObjPath} : Text{"/"};
      return Zdbus_::message(head,
        CreateItemRes{ZuMv(item), ZuMv(prompt)});
    }
    if (info.headers.member == "GetSecret" &&
        info.headers.path == ItemPath) {
      ++state.secrets;
      return Zdbus_::message(head, GetSecretRes{
        Secret{Text{SessionPath}, {}, state.value,
          Text{"application/octet-stream"}}});
    }
    if (info.headers.member == "Prompt" &&
        info.headers.path == PromptObjPath) {
      ++state.prompts;
      if (state.withhold) return Zdbus_::message(head, Empty{});
      bool dismiss = state.dismiss;
      server.send([dismiss](uint32_t signalSerial) {
        Zdbus_::HeadSpec signal;
        signal.type = Zdbus_::MessageType::Signal;
        signal.serial = signalSerial;
        signal.path = PromptObjPath;
        signal.interface = "org.freedesktop.Secret.Prompt";
        signal.member = "Completed";
        ZtBArray<ZtArrayHeapID<"Ztls.Vault.ITest.Prompt">> encoded;
        ZfDBUS::save(encoded, Ztls_::SS::PromptPath{Text{ItemPath}});
        PromptCompleted body{dismiss,
          Variant{ZfDBUS::Any{encoded, "o", 0, ZfDBUS::Order::Little}}};
        return Zdbus_::message(signal, body);
      });
      return Zdbus_::message(head, Empty{});
    }
    return Zdbus_::BuildResult{{}, {}, Zdbus_::BuildError::Body};
  });
}

static void native()
{
  ZuTestScopeRT(native);
  const char *fixture = ZiStat{"../../zdbus/util/zdbusbus"}.exists() ?
    "../../zdbus/util/zdbusbus" : "zdbus/util/zdbusbus";
  int input[2], output[2];
  ZuCheckRT(!::pipe2(input, O_CLOEXEC));
  if (::pipe2(output, O_CLOEXEC)) {
    ::close(input[0]);
    ::close(input[1]);
    ZuCheckRT(false);
  }
  pid_t child = ::fork();
  if (!child) {
    ::close(input[1]);
    ::close(output[0]);
    ::dup2(input[0], STDIN_FILENO);
    ::dup2(output[1], STDOUT_FILENO);
    ::close(input[0]);
    ::close(output[1]);
    ::execl(fixture, "zdbusbus", static_cast<char *>(nullptr));
    ::_exit(errno == ENOENT ? 127 : 126);
  }
  ::close(input[0]);
  ::close(output[1]);
  ZuCheckRT(child > 0);
  bool inputOpen = true;
  bool childReaped = false;
  ZuGuard reap{[child, &input, &inputOpen, &childReaped]() {
    if (inputOpen) ::close(input[1]);
    if (!childReaped) {
      int status = 0;
      ZdbusTestTool::waitChild(child, 0, status);
    }
  }};
  ZdbusAddress::Text addressText;
  bool got = ZdbusTestTool::line(output[0], addressText);
  ::close(output[0]);
  ZdbusAddress address;
  ZuCheckRT(got && ZdbusAddress::parse(address, addressText));

  const char *previous = ::getenv("DBUS_SESSION_BUS_ADDRESS");
  Text saved{previous ? previous : ""};
  bool hadBus = previous != nullptr;
  ZuCheckRT(!::setenv("DBUS_SESSION_BUS_ADDRESS", addressText, 1));
  ZuGuard restore{[&saved, hadBus]() {
    if (hadBus) ::setenv("DBUS_SESSION_BUS_ADDRESS", saved, 1);
    else ::unsetenv("DBUS_SESSION_BUS_ADDRESS");
  }};

  ZmScheduler sched{ZmSchedParams().id("VaultDBusTest").nThreads(2)};
  ZdbusServer server;
  PeerState state;
  ZmSemaphore ready;
  ZmAtomic<unsigned> failed = 0;
  sched.start();
  server.init(&sched, 1, 2, ZuMv(address), "org.freedesktop.secrets",
    {}, [&ready](ZuCSpan) { ready.post(); },
    [&server, &state](ZmRef<ZiIOBuf> frame, Zdbus_::FrameInfo info) {
      method(server, state, ZuMv(frame), info);
    }, {}, [&failed, &ready](Zdbus_::CxnFailure) {
      failed.store_(1);
      ready.post();
    });
  server.start();
  bool serverActive = true;
  ZuGuard stopServer{[&server, &sched, &serverActive]() {
    if (!serverActive) return;
    ZmBlock<>{}([&server](auto done) { server.stop(ZuMv(done)); });
    server.final();
    sched.stop();
  }};
  ZuCheckRT(!ready.timedwait(Zm::now(5)) && !failed.load_());

  Ztls::Random rng;
  ZuCheckRT(rng.init());
  uint8_t id[8];
  ZuCheckRT(rng.random(id));
  char hex[ZuHex::enclen(sizeof(id))];
  ZuHex::encode(hex, id);
  Zi::Path home = ZiFile::append(ZiFile::tmpDir(),
    Zi::Path{} << "ztls-vault-dbus-" << ZuCSpan{hex, sizeof(hex)});
  ZuGuard cleanup{[&home]() { ZiFile::removeTree(home); }};
  Zt::setenv("ZTLSVAULTDBUSTEST_HOME", home);

  Ztls::VaultConfig cf;
  cf.program = "ztls-vault-dbus-test";
  cf.envPrefix = "ZTLSVAULTDBUSTEST";
  cf.store = Ztls::VaultStore::KeyRing;
  cf.variant = Ztls::VaultVariant::Direct;
  Ztls::Vault vault;
  ZuCheckRT(!vault.init(cf).is<ZeException>());
  ZuCheckRT(!vault.open().is<ZeException>());
  ZuGuard stopVault{[&vault]() { vault.close(); }};
  Ztls::Scope scope{Ztls::Scopes::Global{}};
  bool called = false;
  ZuCheckRT(vault.load(scope, "token", [&called](ZuSpan<uint8_t>) {
    called = true;
  }).is<ZeException>() && !called);
  uint8_t value[] = {0, 0xff, 7};
  ZuCheckRT(!vault.save(scope, "token", value).is<ZeException>());
  ZuCheckRT(!vault.load(scope, "token", [&called, &value](ZuSpan<uint8_t> got) {
    called = got == ZuBSpan{value};
  }).is<ZeException>() && called);
  ZuCheckRT(!vault.save(scope, "empty", {}).is<ZeException>());
  called = false;
  ZuCheckRT(!vault.load(scope, "empty", [&called](ZuSpan<uint8_t> got) {
    called = !got.length();
  }).is<ZeException>() && called);
  ZuCheckRT(!vault.save(scope, "locked", value).is<ZeException>());
  called = false;
  ZuCheckRT(!vault.load(scope, "locked", [&called, &value](ZuSpan<uint8_t> got) {
    called = got == ZuBSpan{value};
  }).is<ZeException>() && called);
  ZuCheckRT(!vault.save(scope, "prompted", value)
    .is<ZeException>());
  ZuCheckRT(vault.save(scope, "dismissed", value)
    .is<ZeException>());
  ZuCheckRT(vault.save(scope, "timedout", value)
    .is<ZeException>());
  ZuCheckRT(!vault.save(scope, "recovered", value)
    .is<ZeException>());
  ZuCheckRT(!vault.save(scope, "token", value)
    .is<ZeException>());
  vault.close();
  vault.final();

  Ztls::VaultConfig fileCf = cf;
  fileCf.store = Ztls::VaultStore::File;
  Ztls::Vault file;
  ZuCheckRT(!file.init(fileCf).is<ZeException>());
  ZuCheckRT(!file.open().is<ZeException>());
  ZuGuard stopFile{[&file]() { file.close(); }};
  uint8_t other[] = {9, 8};
  ZuCheckRT(!file.save(scope, "token", other).is<ZeException>());
  ZuCheckRT(!file.save(scope, "fallback", other)
    .is<ZeException>());
  file.close();
  file.final();

  cf.store = Ztls::VaultStore::Auto;
  Ztls::Vault autoVault;
  ZuCheckRT(!autoVault.init(cf).is<ZeException>());
  ZuCheckRT(!autoVault.open().is<ZeException>());
  ZuGuard stopAuto{[&autoVault]() { autoVault.close(); }};
  bool nativeValue = false;
  ZuCheckRT(!autoVault.load(scope, "token", [&nativeValue, &value](ZuSpan<uint8_t> got) {
    nativeValue = got == ZuBSpan{value};
  }).is<ZeException>() && nativeValue);
  bool fallbackValue = false;
  ZuCheckRT(!autoVault.load(scope, "fallback",
    [&fallbackValue, &other](ZuSpan<uint8_t> got) {
      fallbackValue = got == ZuBSpan{other};
    }).is<ZeException>() && fallbackValue);
  Ztls::Vault contender;
  ZuCheckRT(!contender.init(fileCf).is<ZeException>());
  ZuCheckRT(contender.open().is<ZeException>());
  contender.final();
  ZuCheckRT(!autoVault.save(scope, "denied", other)
    .is<ZeException>());
  fallbackValue = false;
  ZuCheckRT(!autoVault.load(scope, "denied",
    [&fallbackValue, &other](ZuSpan<uint8_t> got) {
      fallbackValue = got == ZuBSpan{other};
    }).is<ZeException>() && fallbackValue);
  ZuCheckRT(!autoVault.save(scope, "timedout", other)
    .is<ZeException>());

  ::close(input[1]);
  inputOpen = false;
  int busStatus = 0;
  bool busExited = ZdbusTestTool::waitChild(child, 0, busStatus);
  childReaped = true;
  ZuCheckRT(busExited);
  fallbackValue = false;
  ZuCheckRT(!autoVault.load(scope, "fallback",
    [&fallbackValue, &other](ZuSpan<uint8_t> got) {
      fallbackValue = got == ZuBSpan{other};
    }).is<ZeException>() && fallbackValue);
  fallbackValue = false;
  ZuCheckRT(!autoVault.load(scope, "timedout",
    [&fallbackValue, &other](ZuSpan<uint8_t> got) {
      fallbackValue = got == ZuBSpan{other};
    }).is<ZeException>() && fallbackValue);
  ZuCheckRT(!autoVault.save(scope, "after-loss", other)
    .is<ZeException>());
  autoVault.close();
  autoVault.final();
  ZuCheckRT(state.sessions == 2 && state.collections == 1 &&
    state.items == 9 && state.secrets == 4 && state.prompts == 4 &&
    state.closes == 1);
}

} // namespace VaultDBusTest_

int main()
{
  ZuTestMain();
  ZuTestCall_("native", VaultDBusTest_::native);
}
