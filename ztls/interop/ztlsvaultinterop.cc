//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Isolated native-store check against an independent Secret Service.

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

#include <iostream>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuHex.hh>
#include <zlib/ZiFile.hh>
#include <zlib/ZtlsRandom.hh>
#include <zlib/ZtlsVault.hh>
#include <zlib/ZdbusAddress.hh>

#include "../../zdbus/util/ZdbusTestTool.hh"

using namespace ZuTestUtil;

namespace VaultInterop_ {

static void native()
{
  ZuTestScopeRT(native);
  Ztls::Random rng;
  ZuCheckRT(rng.init());
  uint8_t id[8];
  ZuCheckRT(rng.random(id));
  char hex[ZuHex::enclen(sizeof(id))];
  ZuHex::encode(hex, id);
  Zi::Path home = ZiFile::append(ZiFile::tmpDir(),
    Zi::Path{} << "ztls-vault-interop-" << ZuCSpan{hex, sizeof(hex)});
  ZuGuard cleanup{[&home]() { ZiFile::removeTree(home); }};
  Zi::Path runtime = ZiFile::append(home, "runtime");
  Zi::Path data = ZiFile::append(home, "data");
  Zi::Path config = ZiFile::append(home, "config");
  Zi::Path cache = ZiFile::append(home, "cache");
  Zi::Path control = ZiFile::append(home, "control");
  ZuCheckRT(ZiFile::mkdir(home, 0700) == Zi::OK);
  ZuCheckRT(ZiFile::mkdir(runtime, 0700) == Zi::OK);
  ZuCheckRT(ZiFile::mkdir(data, 0700) == Zi::OK);
  ZuCheckRT(ZiFile::mkdir(config, 0700) == Zi::OK);
  ZuCheckRT(ZiFile::mkdir(cache, 0700) == Zi::OK);
  ZuCheckRT(ZiFile::mkdir(control, 0700) == Zi::OK);
  ZuCheckRT(!::setenv("HOME", home, 1));
  ZuCheckRT(!::setenv("XDG_RUNTIME_DIR", runtime, 1));
  ZuCheckRT(!::setenv("XDG_DATA_HOME", data, 1));
  ZuCheckRT(!::setenv("XDG_CONFIG_HOME", config, 1));
  ZuCheckRT(!::setenv("XDG_CACHE_HOME", cache, 1));
  ::unsetenv("DISPLAY");
  ::unsetenv("WAYLAND_DISPLAY");
  ::unsetenv("GNOME_KEYRING_CONTROL");

  const char *fixture = ZiStat{"../../zdbus/util/zdbusbus"}.exists() ?
    "../../zdbus/util/zdbusbus" : "zdbus/util/zdbusbus";
  int input[2], output[2];
  ZuCheckRT(!::pipe2(input, O_CLOEXEC));
  if (::pipe2(output, O_CLOEXEC)) {
    ::close(input[0]);
    ::close(input[1]);
    ZuCheckRT(false);
  }
  pid_t bus = ::fork();
  if (!bus) {
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
  ZuCheckRT(bus > 0);
  ZuGuard stopBus{[bus, &input]() {
    ::close(input[1]);
    int status = 0;
    ZdbusTestTool::waitChild(bus, 0, status);
  }};
  ZdbusAddress::Text address;
  bool got = ZdbusTestTool::line(output[0], address);
  ::close(output[0]);
  ZdbusAddress parsed;
  ZuCheckRT(got && ZdbusAddress::parse(parsed, address));
  ZuCheckRT(!::setenv("DBUS_SESSION_BUS_ADDRESS", address, 1));

  int unlock[2], daemonOut[2];
  ZuCheckRT(!::pipe2(unlock, O_CLOEXEC));
  if (::pipe2(daemonOut, O_CLOEXEC)) {
    ::close(unlock[0]);
    ::close(unlock[1]);
    ZuCheckRT(false);
  }
  pid_t daemon = ::fork();
  if (!daemon) {
    ::close(unlock[1]);
    ::close(daemonOut[0]);
    ::dup2(unlock[0], STDIN_FILENO);
    ::dup2(daemonOut[1], STDOUT_FILENO);
    ::close(unlock[0]);
    ::close(daemonOut[1]);
    ZiFile nullFile{"/dev/null", ZiFile::WriteOnly | ZiFile::GC};
    if (!nullFile) ::_exit(1);
    ::dup2(nullFile.handle(), STDERR_FILENO);
    ::execlp("gnome-keyring-daemon", "gnome-keyring-daemon",
      "--foreground", "--unlock", "--components=secrets", "--control-directory",
      static_cast<const char *>(control), static_cast<char *>(nullptr));
    ::_exit(errno == ENOENT ? 127 : 126);
  }
  ::close(unlock[0]);
  ::close(daemonOut[1]);
  ZuGuard closeDaemonOut{[&daemonOut]() { ::close(daemonOut[0]); }};
  ::write(unlock[1], "\n", 1);
  ::close(unlock[1]);
  ZuCheckRT(daemon > 0);
  ZuGuard stopDaemon{[daemon]() {
    int status = 0;
    ZdbusTestTool::waitChild(daemon, SIGTERM, status);
  }};
  ZdbusAddress::Text daemonReady;
  ZuCheckRT(ZdbusTestTool::line(daemonOut[0], daemonReady));

  Ztls::VaultConfig cf;
  cf.service = "ztls-vault-interop-test";
  cf.account = "isolated";
  cf.store = Ztls::VaultStore::KeyRing;
  cf.variant = Ztls::VaultVariant::Direct;
  Ztls::Vault vault;
  ZuCheckRT(!vault.init(cf).is<ZeException>());
  bool started = false;
  ZuGuard stopVault{[&vault, &started]() {
    if (started) vault.stop();
    vault.final();
  }};
  ZuCheckRT(!vault.start().is<ZeException>());
  started = true;
  Ztls::Scope scope{Ztls::Scopes::Global{}};
  bool called = false;
  ZuCheckRT(vault.load(scope, "missing", [&called](ZuBSpan) {
    called = true;
  }).is<ZeException>() && !called);
  uint8_t first[] = {0, 0xff, 7};
  uint8_t second[] = {8, 0, 9, 0xff};
  ZuCheckRT(!vault.save(scope, "token", first).is<ZeException>());
  ZuCheckRT(!vault.save(scope, "token", second).is<ZeException>());
  ZuCheckRT(!vault.load(scope, "token", [&called, &second](ZuBSpan value) {
    called = value == ZuBSpan{second};
  }).is<ZeException>() && called);
  vault.stop();
  started = false;

  cf.variant = Ztls::VaultVariant::Secrets;
  Ztls::Vault secrets;
  ZuCheckRT(!secrets.init(cf).is<ZeException>());
  ZuCheckRT(!secrets.start().is<ZeException>());
  ZuCheckRT(!secrets.save(scope, "private", second).is<ZeException>());
  secrets.stop();
  secrets.final();
  Zi::Path age = ZiFile::append(
    ZiFile::append(ZiFile::append(home, ".ztls-vault-interop-test"),
      "vault"), "secrets.age");
  ZuCheckRT(ZiStat{age}.exists());

  cf.store = Ztls::VaultStore::Auto;
  Ztls::Vault recovered;
  ZuCheckRT(!recovered.init(cf).is<ZeException>());
  ZuCheckRT(!recovered.start().is<ZeException>());
  called = false;
  ZuCheckRT(!recovered.load(scope, "private",
    [&called, &second](ZuBSpan value) {
      called = value == ZuBSpan{second};
    }).is<ZeException>() && called);
  recovered.stop();
  recovered.final();
}

} // namespace VaultInterop_

int main()
{
  if (!ZdbusTestTool::available("dbus-daemon") ||
      !ZdbusTestTool::available("gnome-keyring-daemon")) {
    std::cout << "1..0 # SKIP dbus-daemon or gnome-keyring-daemon unavailable\n";
    return 0;
  }
  ZuTestMain();
  ZuTestCall_("native", VaultInterop_::native);
}
