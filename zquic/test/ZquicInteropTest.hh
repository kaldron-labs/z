//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef ZquicInteropTest_HH
#define ZquicInteropTest_HH

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#include <iostream>

#ifndef _WIN32
#include <arpa/inet.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#else
#include <windows.h>
#endif

#include <zlib/ZuBox.hh>

#include <zlib/ZtString.hh>

#include <zlib/ZiFile.hh>

#ifdef Z_TEST_RESIDUE
#include "ZiTestResidue.hh"
#endif

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

template <typename L>
bool waitUntil(L l, unsigned iterations = 2000)
{
  for (unsigned i = 0; i < iterations; ++i) {
    if (l()) return true;
    sleepMS(1);
  }
  return false;
}

inline void printFile(const char *label, const ZtString<> &path)
{
  FILE *f = fopen(path.data(), "r");
  if (!f) return;
  std::cout << "# " << label << ':';
  char buf[256];
  bool any = false;
  while (fgets(buf, sizeof(buf), f)) {
    if (!any) std::cout << '\n';
    any = true;
    std::cout << "# " << buf;
  }
  if (!any) std::cout << " <empty>\n";
  fclose(f);
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
#ifdef Z_TEST_RESIDUE
  Zi::Path	path;
#else
  char		path[PATH_MAX]{};
#endif
  ZtString<>	certPath;
  ZtString<>	keyPath;

#ifndef Z_TEST_RESIDUE
  ~TempDir() { cleanup(); }
#endif

  bool init(const char *prefix)
  {
#ifdef Z_TEST_RESIDUE
    static unsigned counter;
    Zi::Name name;
    name << prefix << '-' << ZuBox<unsigned>{++counter};
    path = ZiTestResidue::dir(name);
#else
    snprintf(path, sizeof(path), "/tmp/%s.XXXXXX", prefix);
    if (!mkdtemp(path)) return false;
#endif
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
#ifdef Z_TEST_RESIDUE
    s << path << '/' << name;
#else
    s << static_cast<const char *>(path) << '/' << name;
#endif
    return s;
  }

#ifndef Z_TEST_RESIDUE
  void cleanup()
  {
    if (!path[0]) return;
    ZtString<> cmd;
    cmd << "rm -rf " << static_cast<const char *>(path);
    ::system(cmd.data());
    path[0] = 0;
  }
#endif
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
  ZtString<> filePath;
  filePath << path;
  FILE *f = fopen(filePath.data(), "w");
  if (!f) return false;
  int n = fprintf(f,
    "{\n"
    "  admin off\n"
    "  auto_https disable_redirects\n"
    "  servers :%u {\n"
    "    protocols h1 h2 h3\n"
    "  }\n"
    "}\n"
    "https://localhost:%u {\n"
    "  tls %.*s %.*s\n"
    "  respond %.*s \"%.*s\" 200\n"
    "}\n",
    port, port,
    int(certPath.length()), certPath.data(),
    int(keyPath.length()), keyPath.data(),
    int(route.length()), route.data(),
    int(body.length()), body.data());
  return n > 0 && !fclose(f);
}

#ifndef _WIN32
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

    pid = fork();
    if (pid < 0) {
      pid = -1;
      return false;
    }
    if (!pid) {
      setenv("XDG_DATA_HOME", dataHome.data(), 1);
      setenv("XDG_CONFIG_HOME", configHome.data(), 1);
      freopen(logPath.data(), "w", stdout);
      freopen(logPath.data(), "a", stderr);
      execlp("caddy", "caddy", "run", "--config", caddyfile.data(),
	(char *)nullptr);
      _exit(127);
    }
    return true;
  }

  void stop()
  {
    if (pid <= 0) return;
    kill(pid, SIGTERM);
    for (unsigned i = 0; i < 20; ++i) {
      int status = 0;
      pid_t r = waitpid(pid, &status, WNOHANG);
      if (r == pid) {
	pid = -1;
	return;
      }
      sleepMS(50);
    }
    kill(pid, SIGKILL);
    waitpid(pid, nullptr, 0);
    pid = -1;
  }
};

inline bool waitCaddyReady(unsigned port, ZuCSpan route)
{
  for (unsigned i = 0; i < 80; ++i) {
    ZtString<> cmd;
    cmd <<
      "curl --http1.1 -k --fail -sS --connect-timeout 1 --max-time 2 "
      "--resolve localhost:" << port << ":127.0.0.1 "
      "https://localhost:" << port << route << " >/dev/null 2>&1";
    if (systemOK(::system(cmd.data()))) return true;
    sleepMS(100);
  }
  return false;
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
