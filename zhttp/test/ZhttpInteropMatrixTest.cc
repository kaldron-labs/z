//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdio.h>

#include <iostream>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZiFile.hh>

#include "ZhttpTestUtil.hh"

using namespace ZuTestUtil;

namespace {

using Zhttp::Test::TempDir;
using Zhttp::Test::cspan;
using Zhttp::Test::haveCurlH3;
using Zhttp::Test::loopbackPort;
using Zhttp::Test::printFile;
using Zhttp::Test::systemOK;
using Zhttp::Test::writeSelfSignedLocalhostCert;
using Zquic::Test::haveCaddy;

ZuCSpan Path = "/zhttp-interop";
ZuCSpan Body = "zhttp-interop-ok";

namespace Pair {
  enum T { ZhttpCaddy, ZhttpZhttpd, CurlZhttpd };
}

namespace Proto {
  enum T { H1TCP, H1TLS, H3 };
}

struct Case {
  Pair::T	pair;
  Proto::T	proto;
  unsigned	jobs;
  unsigned	requests;
};

const char *pairName(Pair::T pair)
{
  switch (pair) {
    case Pair::ZhttpCaddy: return "zhttp>caddy";
    case Pair::ZhttpZhttpd: return "zhttp>zhttpd";
    case Pair::CurlZhttpd: return "curl>zhttpd";
  }
  return "unknown";
}

const char *protoName(Proto::T proto)
{
  switch (proto) {
    case Proto::H1TCP: return "h1/tcp";
    case Proto::H1TLS: return "h1/tls";
    case Proto::H3: return "h3/quic";
  }
  return "unknown";
}

bool writeFile(ZuCSpan path, ZuCSpan data, unsigned mode = 0666)
{
  ZiFile f;
  if (f.open(path, ZiFile::Write, mode) != Zi::OK) return false;
  if (f.write(data.data(), data.length()) != Zi::OK) return false;
  f.close();
  return true;
}

bool writeRoot(TempDir &temp, ZtString<> &rootPath)
{
  rootPath = temp.pathOf("root");
  if (ZiFile::mkdir(rootPath) != Zi::OK) return false;
  ZtString<> filePath;
  filePath << rootPath << Path;
  ZtString<> body;
  body << Body << '\n';
  return writeFile(filePath, cspan(body));
}

bool writeCaddyfile(
  ZuCSpan path, Proto::T proto, unsigned port, ZuCSpan rootPath,
  ZuCSpan certPath, ZuCSpan keyPath)
{
  ZtString<> filePath;
  filePath << path;
  FILE *f = fopen(filePath.data(), "w");
  if (!f) return false;
  int n;
  if (proto == Proto::H1TCP)
    n = fprintf(f,
      "{\n"
      "  admin off\n"
      "  auto_https off\n"
      "}\n"
      "http://127.0.0.1:%u {\n"
      "  root * %.*s\n"
      "  file_server\n"
      "}\n",
      port, int(rootPath.length()), rootPath.data());
  else
    n = fprintf(f,
      "{\n"
      "  admin off\n"
      "  auto_https disable_redirects\n"
      "  servers :%u {\n"
      "    protocols h1 h2 h3\n"
      "  }\n"
      "}\n"
      "https://localhost:%u {\n"
      "  tls %.*s %.*s\n"
      "  root * %.*s\n"
      "  file_server\n"
      "}\n",
      port, port,
      int(certPath.length()), certPath.data(),
      int(keyPath.length()), keyPath.data(),
      int(rootPath.length()), rootPath.data());
  return n > 0 && !fclose(f);
}

void appendReadyCommand(
  ZtString<> &script, Proto::T proto, unsigned port, ZuCSpan certPath)
{
  switch (proto) {
    case Proto::H1TCP:
      script <<
	"  if curl --http1.1 --fail -sS --connect-timeout 1 --max-time 2 "
	"http://127.0.0.1:" << port << Path <<
	" >/dev/null 2>&1; then\n";
      break;
    case Proto::H1TLS:
      script <<
	"  if curl --http1.1 --cacert " << certPath <<
	" --fail -sS --connect-timeout 1 --max-time 2 "
	"--resolve localhost:" << port << ":127.0.0.1 "
	"https://localhost:" << port << Path <<
	" >/dev/null 2>&1; then\n";
      break;
    case Proto::H3:
      script <<
	"  if curl --http3-only --cacert " << certPath <<
	" --fail -sS --connect-timeout 1 --max-time 2 "
	"--resolve localhost:" << port << ":127.0.0.1 "
	"https://localhost:" << port << Path <<
	" >/dev/null 2>&1; then\n";
      break;
  }
}

void appendURL(ZtString<> &s, Proto::T proto, unsigned port)
{
  if (proto == Proto::H1TCP)
    s << "http://127.0.0.1:" << port << Path;
  else
    s << "https://localhost:" << port << Path;
}

void appendZhttpCommand(
  ZtString<> &script, const Case &c, unsigned port, ZuCSpan certPath,
  ZuCSpan tempPath)
{
  script <<
    "if ! timeout 20s \"$client\" -j " << c.jobs << " -n " << c.requests;
  switch (c.proto) {
    case Proto::H1TCP:
      break;
    case Proto::H1TLS:
      script << " --http3=disable -c " << certPath;
      break;
    case Proto::H3:
      script << " --http3=force -c " << certPath;
      break;
  }
  script << " -o " << tempPath << "/body ";
  appendURL(script, c.proto, port);
  script << " >" << tempPath << "/client.out 2>" << tempPath <<
    "/client.err; then\n"
    "  cat " << tempPath << "/client.err\n"
    "  exit 1\n"
    "fi\n"
    "if [ " << c.requests << " -eq 1 ]; then\n"
    "  grep -qx '" << Body << "' " << tempPath << "/body\n"
    "else\n"
    "  grep -qx '" << Body << "' " << tempPath << "/body.0\n"
    "  grep -qx '" << Body << "' " << tempPath << "/body." <<
      (c.requests - 1) << "\n"
    "fi\n";
}

void appendCurlCommand(
  ZtString<> &script, const Case &c, unsigned port, ZuCSpan certPath,
  ZuCSpan tempPath)
{
  script <<
    "cfg=" << tempPath << "/curl.cfg\n"
    ": >\"$cfg\"\n"
    "i=0\n"
    "while [ \"$i\" -lt " << c.requests << " ]; do\n"
    "  printf 'url = \"";
  appendURL(script, c.proto, port);
  script <<
    "\"\\noutput = \"/dev/null\"\\n' >>\"$cfg\"\n"
    "  i=$((i + 1))\n"
    "done\n"
    "curl_opts='--fail -sS --connect-timeout 2 --max-time 20'\n";
  switch (c.proto) {
    case Proto::H1TCP:
      script << "curl_proto='--http1.1'\n";
      break;
    case Proto::H1TLS:
      script << "curl_proto='--http1.1 --cacert " << certPath <<
	" --resolve localhost:" << port << ":127.0.0.1'\n";
      break;
    case Proto::H3:
      script << "curl_proto='--http3-only --cacert " << certPath <<
	" --resolve localhost:" << port << ":127.0.0.1'\n";
      break;
  }
  script <<
    "curl_parallel=\n"
    "if [ " << c.jobs << " -gt 1 ]; then\n"
    "  curl_parallel='--parallel --parallel-max " << c.jobs << "'\n"
    "fi\n"
    "if ! timeout 20s curl $curl_opts $curl_proto $curl_parallel "
      "--config \"$cfg\" >" << tempPath << "/curl.out 2>" << tempPath <<
      "/curl.err; then\n"
    "  cat " << tempPath << "/curl.err\n"
    "  exit 1\n"
    "fi\n";
}

bool writeScript(
  ZuCSpan path, const Case &c, unsigned port, ZuCSpan tempPath,
  ZuCSpan rootPath, ZuCSpan certPath, ZuCSpan keyPath, ZuCSpan caddyfile)
{
  bool caddy = c.pair == Pair::ZhttpCaddy;
  bool zhttp = c.pair != Pair::CurlZhttpd;
  ZtString<> script;
  script <<
    "#!/bin/sh\n"
    "set -eu\n"
    "pid=\n"
    "cleanup() {\n"
    "  if [ -n \"$pid\" ]; then\n"
    "    kill \"$pid\" 2>/dev/null || true\n"
    "    wait \"$pid\" 2>/dev/null || true\n"
    "  fi\n"
    "}\n"
    "trap cleanup EXIT INT TERM\n"
    "server=../example/zhttpd\n"
    "client=../example/zhttp\n"
    "[ -x \"$server\" ] || server=./zhttp/example/zhttpd\n"
    "[ -x \"$client\" ] || client=./zhttp/example/zhttp\n";
  if (caddy)
    script <<
      "export XDG_DATA_HOME=" << tempPath << "/caddy-data\n"
      "export XDG_CONFIG_HOME=" << tempPath << "/caddy-config\n"
      "mkdir -p \"$XDG_DATA_HOME\" \"$XDG_CONFIG_HOME\"\n"
      "caddy validate --config " << caddyfile << " >/dev/null 2>&1\n"
      "caddy run --config " << caddyfile << " >" << tempPath <<
	"/server.out 2>" << tempPath << "/server.err &\n";
  else {
    script << "\"$server\" " << rootPath;
    switch (c.proto) {
      case Proto::H1TCP:
	script << " --http";
	break;
      case Proto::H1TLS:
	script << " --https --cert " << certPath << " --key " << keyPath;
	break;
      case Proto::H3:
	script << " --http3 --cert " << certPath << " --key " << keyPath;
	break;
    }
    script <<
      " --addr 127.0.0.1 --port " << port <<
      " --timeout 0 --log " << tempPath << "/access.log"
      " >" << tempPath << "/server.out 2>" << tempPath << "/server.err &\n";
  }
  script <<
    "pid=$!\n"
    "ready=0\n"
    "for i in $(seq 1 200); do\n";
  appendReadyCommand(script, c.proto, port, certPath);
  script <<
    "    ready=1\n"
    "    break\n"
    "  fi\n"
    "  if ! kill -0 \"$pid\" 2>/dev/null; then\n"
    "    cat " << tempPath << "/server.err\n"
    "    exit 1\n"
    "  fi\n"
    "  sleep 0.1\n"
    "done\n"
    "if [ \"$ready\" -ne 1 ]; then\n"
    "  cat " << tempPath << "/server.err\n"
    "  exit 1\n"
    "fi\n";
  if (zhttp)
    appendZhttpCommand(script, c, port, certPath, tempPath);
  else
    appendCurlCommand(script, c, port, certPath, tempPath);
  return writeFile(path, cspan(script), 0777);
}

bool runCase_(const Case &c)
{
  TempDir temp;
  if (!temp.init("ZhttpInteropMatrix")) {
    std::cout << "# failed to create temporary directory\n";
    return false;
  }
  ZtString<> rootPath;
  if (!writeRoot(temp, rootPath)) {
    std::cout << "# failed to create static root\n";
    return false;
  }
  ZtString<> certPath, keyPath;
  if (!writeSelfSignedLocalhostCert(temp, certPath, keyPath)) {
    std::cout << "# failed to create TLS certificate\n";
    return false;
  }
  unsigned port = loopbackPort();
  if (!port) {
    std::cout << "# failed to allocate loopback port\n";
    return false;
  }
  auto caddyfile = temp.pathOf("Caddyfile");
  if (c.pair == Pair::ZhttpCaddy &&
      !writeCaddyfile(caddyfile, c.proto, port, cspan(rootPath),
	cspan(certPath), cspan(keyPath))) {
    std::cout << "# failed to write Caddyfile\n";
    return false;
  }
  auto script = temp.pathOf("matrix.sh");
  if (!writeScript(script, c, port, static_cast<const char *>(temp.path),
      cspan(rootPath), cspan(certPath), cspan(keyPath), cspan(caddyfile))) {
    std::cout << "# failed to write matrix script\n";
    return false;
  }

  ZtString<> cmd;
  cmd << "sh " << script;
  if (systemOK(::system(cmd.data()))) return true;

  std::cout << "# failed case: " << pairName(c.pair) << ' ' <<
    protoName(c.proto) << " -j" << c.jobs << " -n" << c.requests << '\n';
  printFile("server stderr", temp.pathOf("server.err"));
  printFile("client stderr", temp.pathOf("client.err"));
  printFile("curl stderr", temp.pathOf("curl.err"));
  printFile("script", script);
  return false;
}

void runCase(Case c)
{
  ZuTestScopeRT(runCase);
  std::cout << "# interop case: " << pairName(c.pair) << ' ' <<
    protoName(c.proto) << " -j" << c.jobs << " -n" << c.requests << '\n';
  ZuCheckRT(runCase_(c));
}

void testPrerequisites()
{
  ZuTestScope(testPrerequisites);
  ZuCHECK(haveCaddy(), "caddy is required for Zhttp interop matrix tests");
  ZuCHECK(haveCurlH3(),
    "curl with HTTP3/ngtcp2/nghttp3 is required for Zhttp interop matrix tests");
}

} // namespace

#define ZHTTP_INTEROP_CASE(pair, proto, j, n) \
  ZuTestCallRT_(#pair "/" #proto "/j" #j "n" #n, runCase, \
    Case{Pair::pair, Proto::proto, j, n})

#define ZHTTP_INTEROP_WORKLOADS(pair, proto) \
  ZHTTP_INTEROP_CASE(pair, proto, 1, 1); \
  ZHTTP_INTEROP_CASE(pair, proto, 1, 1000); \
  ZHTTP_INTEROP_CASE(pair, proto, 10, 1000)

#define ZHTTP_INTEROP_PROTOCOLS(pair) \
  ZHTTP_INTEROP_WORKLOADS(pair, H1TCP); \
  ZHTTP_INTEROP_WORKLOADS(pair, H1TLS); \
  ZHTTP_INTEROP_WORKLOADS(pair, H3)

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testPrerequisites);
  ZHTTP_INTEROP_PROTOCOLS(ZhttpCaddy);
  ZHTTP_INTEROP_PROTOCOLS(ZhttpZhttpd);
  ZHTTP_INTEROP_PROTOCOLS(CurlZhttpd);
}
