//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef ZquicInteropTest_HH
#define ZquicInteropTest_HH

#include <errno.h>
#include <limits.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <iostream>

#ifndef _WIN32
#include <arpa/inet.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#else
#include <windows.h>
#endif

#include <zlib/ZuBox.hh>
#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmThread.hh>
#include <zlib/ZtString.hh>

#include <zlib/ZiFile.hh>

#include "ZiTestResidue.hh"

namespace Zquic::Test {

inline bool systemOK(int status)
{
#ifndef _WIN32
  return status != -1 && WIFEXITED(status) && !WEXITSTATUS(status);
#else
  return status == 0;
#endif
}

inline void sleepMS(unsigned ms)
{
#ifndef _WIN32
  usleep(ms * 1000);
#else
  Sleep(ms);
#endif
}

inline void printFile(const char *label, const ZtString<> &path)
{
  ZiFile file{path, ZiFile::ReadOnly | ZiFile::GC};
  if (!file) return;
  auto length = file.size();
  if (length < 0 || uint64_t(length) > INT_MAX) return;
  ZtString<> data;
  data.length(unsigned(length));
  if (length && file.read(data.data(), unsigned(length)) != int(length)) return;
  ZuTestUtil::log(label, ':');
  if (!data) {
    ZuTestUtil::log("<empty>");
    return;
  }
  unsigned offset = 0;
  while (offset < data.length()) {
    ZuCSpan tail{data.data() + offset, data.length() - offset};
    int eol = tail.find("\n");
    unsigned n = eol < 0 ? tail.length() : unsigned(eol);
    ZuTestUtil::log(ZuCSpan{tail.data(), n});
    offset += n + (eol >= 0);
  }
}

inline bool writeLocalhostCert_(
  ZuCSpan certPath, ZuCSpan keyPath)
{
  ZtString<> cmd;
  cmd <<
    "openssl req -x509 -newkey rsa:2048 -nodes -days 1 "
    "-subj /CN=localhost "
    "-addext basicConstraints=critical,CA:TRUE "
    "-addext keyUsage=critical,digitalSignature,keyEncipherment,keyCertSign "
    "-addext subjectAltName=DNS:localhost,IP:127.0.0.1,IP:::1 "
    "-keyout " << keyPath << ' ' <<
    "-out " << certPath << " >/dev/null 2>&1";
  return systemOK(::system(cmd.data()));
}

struct TempDir {
  Zi::Path	path;
  ZtString<>	certPath;
  ZtString<>	keyPath;

  bool init(const char *prefix)
  {
    static unsigned counter;
    Zi::Name name;
    name << prefix << '-' << ZuBox<unsigned>{++counter};
    path = ZiTestResidue::dir(name);
    certPath = pathOf("cert.pem");
    keyPath = pathOf("key.pem");
    return true;
  }

  bool init()
  {
    return init("ZquicInteropTest") &&
      writeLocalhostCert_(certPath.cspan(), keyPath.cspan());
  }

  ZtString<> pathOf(const char *name) const
  {
    ZtString<> s;
    s << path << '/' << name;
    return s;
  }

};

inline bool writeLocalhostCert(
  TempDir &temp, ZtString<> &certPath, ZtString<> &keyPath)
{
  certPath = temp.pathOf("cert.pem");
  keyPath = temp.pathOf("key.pem");
  temp.certPath = certPath;
  temp.keyPath = keyPath;
  return writeLocalhostCert_(certPath.cspan(), keyPath.cspan());
}

inline bool haveCurlH3()
{
  return
    systemOK(::system("command -v curl >/dev/null")) &&
    systemOK(::system("curl --version | grep -q HTTP3")) &&
    systemOK(::system("curl --version | grep -q ngtcp2")) &&
    systemOK(::system("curl --version | grep -q nghttp3"));
}

inline bool haveCaddy()
{
  return systemOK(::system("command -v caddy >/dev/null"));
}

inline bool runCurlH3(
  const TempDir &temp, unsigned port, ZuCSpan path, ZuCSpan expectedBody)
{
  auto body = temp.pathOf("curl.body");
  auto version = temp.pathOf("curl.version");
  auto err = temp.pathOf("curl.err");

  ZtString<> cmd;
  cmd <<
    "curl --http3-only -k --fail -sS --connect-timeout 2 --max-time 5 "
    "--resolve localhost:" << port << ":127.0.0.1 "
    "-o " << body << " -w '%{http_version}' "
    "https://localhost:" << port << path <<
    " >" << version << " 2>" << err;
  if (!systemOK(::system(cmd.data()))) {
    printFile("curl stderr", err);
    printFile("curl version", version);
    return false;
  }

  cmd.length(0);
  cmd << "grep -qx '" << expectedBody << "' " << body <<
    " && grep -qx '3' " << version;
  if (systemOK(::system(cmd.data()))) return true;
  printFile("curl body", body);
  printFile("curl version", version);
  printFile("curl stderr", err);
  return false;
}

#ifndef _WIN32
inline unsigned loopbackPort(unsigned port)
{
  int tcp = ::socket(AF_INET, SOCK_STREAM, 0);
  if (tcp < 0) return 0;
  int one = 1;
  ::setsockopt(tcp, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = htons(port);
  if (::bind(tcp, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
    ::close(tcp);
    return 0;
  }
  int udp = ::socket(AF_INET, SOCK_DGRAM, 0);
  if (udp < 0 ||
      ::bind(udp, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
    if (udp >= 0) ::close(udp);
    ::close(tcp);
    return 0;
  }
  ::close(udp);
  ::close(tcp);
  return port;
}
#else
inline unsigned loopbackPort(unsigned port) { return port; }
#endif

inline bool writeCaddyfile(
  ZuCSpan path, unsigned port, ZuCSpan certPath, ZuCSpan keyPath,
  ZuCSpan route, ZuCSpan body)
{
  ZtString<> data;
  data <<
    "{\n"
    "  admin off\n"
    "  auto_https disable_redirects\n"
    "  servers :" << port << " {\n"
    "    protocols h1 h2 h3\n"
    "  }\n"
    "}\n"
    "https://localhost:" << port << " {\n"
    "  tls " << certPath << ' ' << keyPath << '\n' <<
    "  respond " << route << " \"" << body << "\" 200\n"
    "}\n";
  ZiFile file{Zi::Path{path}, ZiFile::Write | ZiFile::GC};
  return file && file.write(data.data(), data.length()) == Zi::OK;
}

#ifndef _WIN32
// Caddy's run --pingback protocol echoes stdin after loading its configuration.
// This is a startup event, independent of request timing or HTTP retry policy.
struct CaddyStartup {
  enum { NonceSize = 32 }; // Caddy's own start command uses a 32-byte nonce
  ZtString<> address;
  uint8_t nonce[NonceSize];
  int listener = -1;
  int input = -1;
  int peer = -1;

  ~CaddyStartup() {
    if (peer >= 0) ::close(peer);
    if (input >= 0) ::close(input);
    if (listener >= 0) ::close(listener);
  }
  bool init() {
    ZiFile random{Zi::Path{"/dev/urandom"}, ZiFile::ReadOnly | ZiFile::GC};
    if (!random || random.read(nonce, NonceSize) != NonceSize) return false;
    listener = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0 || ::fcntl(listener, F_SETFL, O_NONBLOCK)) return false;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::bind(listener, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) ||
	::listen(listener, 1)) return false;
    socklen_t n = sizeof(addr);
    if (::getsockname(listener, reinterpret_cast<sockaddr *>(&addr), &n)) return false;
    address << "127.0.0.1:" << ntohs(addr.sin_port);
    int pipeFD[2]; // POSIX pipe's read and write endpoints
    if (::pipe(pipeFD)) return false;
    input = pipeFD[0];
    // The nonce fits in PIPE_BUF; preload it before any child can exit/close.
    int written;
    do { written = ::write(pipeFD[1], nonce, NonceSize); }
    while (written < 0 && errno == EINTR);
    ::close(pipeFD[1]);
    return written == NonceSize;
  }
  static bool readable(int fd, ZuTime deadline) {
    pollfd event{fd, POLLIN, 0};
    for (;;) {
      int remaining = (deadline - Zm::now()).millisecs();
      if (remaining <= 0) return false;
      int n = ::poll(&event, 1, remaining);
      if (n < 0 && errno == EINTR) continue;
      return n > 0 && (event.revents & POLLIN);
    }
  }
  bool wait() {
    ZuTime deadline = Zm::now(10);
    if (!readable(listener, deadline)) return false;
    peer = ::accept(listener, nullptr, nullptr);
    if (peer < 0) return false;
    uint8_t echoed[NonceSize];
    unsigned offset = 0;
    while (offset < NonceSize) {
      if (!readable(peer, deadline)) return false;
      int n = ::recv(peer, echoed + offset, NonceSize - offset, MSG_DONTWAIT);
      if (n < 0 && errno == EINTR) continue;
      if (n <= 0) return false;
      offset += unsigned(n);
    }
    return !memcmp(echoed, nonce, NonceSize);
  }
};

struct CaddyProcess {
  pid_t		pid = -1;
  ZtString<>	dataHome;
  ZtString<>	configHome;
  ZtString<>	logPath;

  ~CaddyProcess() { stop(); }

  bool start(const TempDir &temp, ZuCSpan caddyfile)
  {
    stop();
    dataHome = temp.pathOf("caddy-data");
    configHome = temp.pathOf("caddy-config");
    logPath = temp.pathOf("caddy.log");
    mkdir(dataHome.data(), 0700);
    mkdir(configHome.data(), 0700);

    ZtString<> cmd;
    cmd <<
      "XDG_DATA_HOME=" << dataHome <<
      " XDG_CONFIG_HOME=" << configHome <<
      " caddy validate --config " << caddyfile << " >/dev/null 2>&1";
    if (!systemOK(::system(cmd.data()))) return false;

    CaddyStartup startup;
    if (!startup.init()) return false;
    pid = fork();
    if (pid < 0) {
      pid = -1;
      return false;
    }
    if (!pid) {
      ::close(startup.listener);
      if (startup.input != STDIN_FILENO) {
	if (::dup2(startup.input, STDIN_FILENO) < 0) _exit(127);
	::close(startup.input);
      }
      setenv("XDG_DATA_HOME", dataHome.data(), 1);
      setenv("XDG_CONFIG_HOME", configHome.data(), 1);
      freopen(logPath.data(), "w", stdout);
      freopen(logPath.data(), "a", stderr);
      execlp("caddy", "caddy", "run", "--config", caddyfile.data(),
	"--pingback", startup.address.data(), (char *)nullptr);
      _exit(127);
    }
    ::close(startup.input);
    startup.input = -1;
    if (startup.wait()) return true;
    stop();
    return false;
  }

  void stop()
  {
    if (pid <= 0) return;
    ZmSemaphore exited;
    ZmThread waiter;
    // Leave the child waitable until this thread sends any final signal, so
    // a timeout/exit race cannot signal a PID already reused by another process.
    bool waiting = !waiter.run([child = pid, &exited] {
      siginfo_t status{};
      while (::waitid(P_PID, child, &status, WEXITED | WNOWAIT) < 0 &&
	  errno == EINTR) { }
      exited.post();
    }, ZmThreadParams{}.name("CaddyExit"));
    kill(pid, SIGTERM);
    if (!waiting || exited.timedwait(Zm::now(1))) kill(pid, SIGKILL);
    if (waiting) waiter.join();
    while (waitpid(pid, nullptr, 0) < 0 && errno == EINTR) { }
    pid = -1;
  }
};

inline bool waitCaddyReady(unsigned port, ZuCSpan route)
{
  // CaddyProcess::start has already received startup confirmation. Keep the
  // public helper as one route check; elapsed time never establishes readiness.
  ZtString<> cmd;
  cmd <<
    "curl --http1.1 -k --fail -sS --connect-timeout 1 --max-time 2 "
    "--resolve localhost:" << port << ":127.0.0.1 "
    "https://localhost:" << port << route << " >/dev/null 2>&1";
  return systemOK(::system(cmd.data()));
}
#else
struct CaddyProcess {
  ZtString<> logPath;
  bool start(const TempDir &, ZuCSpan) { return false; }
};
inline bool waitCaddyReady(unsigned, ZuCSpan) { return false; }
#endif

} // namespace Zquic::Test

#endif /* ZquicInteropTest_HH */
