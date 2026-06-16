//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdio.h>
#include <time.h>

#include <iostream>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZiFile.hh>
#include <zlib/ZtCLI.hh>

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

uint64_t nowMS()
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t(ts.tv_sec) * 1000) + (uint64_t(ts.tv_nsec) / 1000000);
}

uint64_t &startMS()
{
  static uint64_t start = nowMS();
  return start;
}

namespace Pair {
  enum T { ZhttpCaddy, ZhttpZhttpd, CurlZhttpd };
}

namespace Proto {
  enum T { H1TCP, H1TLS, H3 };
}

namespace PairOpt {
  ZtEnum(PairOpt, int8_t, all, zhttpCaddy, zhttpZhttpd, curlZhttpd);
}

namespace ProtoOpt {
  ZtEnum(ProtoOpt, int8_t, all, h1tcp, h1tls, h3);
}

struct Case {
  Pair::T	pair;
  Proto::T	proto;
  unsigned	jobs;
  unsigned	requests;
};

using OptString = ZtString<ZtStringHeapID<"ZtCLI.Option">>;

struct Options {
  OptString	pair;
  OptString	proto;
  uint32_t	jobs = 0;
  uint32_t	requests = 0;
  OptString	caseName;
  bool		quiet = false;
  bool		help = false;
};

ZtStruct((Options, CLI),
  (((pair),     (CLI::Long<"pair">)),                            (String, "all")),
  (((proto),    (CLI::Long<"proto">)),                           (String, "all")),
  (((jobs),     (CLI::Opt<'j'>, CLI::Long<"jobs">)),             (UInt32, 0)),
  (((requests), (CLI::Opt<'n'>, CLI::Long<"requests">)),         (UInt32, 0)),
  (((caseName), (CLI::Long<"case">)),                            (String, "")),
  (((quiet),    (CLI::Flag<'q'>, CLI::Long<"quiet">)),           (Bool, false)),
  (((help),     (CLI::Flag<'h'>, CLI::Long<"help">)),            (Bool, false)));

Options options;

const char *pairName(Pair::T pair)
{
  switch (pair) {
    case Pair::ZhttpCaddy: return "zhttp>caddy";
    case Pair::ZhttpZhttpd: return "zhttp>zhttpd";
    case Pair::CurlZhttpd: return "curl>zhttpd";
  }
  return "unknown";
}

const char *pairCaseName(Pair::T pair)
{
  switch (pair) {
    case Pair::ZhttpCaddy: return "ZhttpCaddy";
    case Pair::ZhttpZhttpd: return "ZhttpZhttpd";
    case Pair::CurlZhttpd: return "CurlZhttpd";
  }
  return "Unknown";
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

const char *protoCaseName(Proto::T proto)
{
  switch (proto) {
    case Proto::H1TCP: return "H1TCP";
    case Proto::H1TLS: return "H1TLS";
    case Proto::H3: return "H3";
  }
  return "Unknown";
}

void caseName(ZtString<> &s, const Case &c)
{
  s << pairCaseName(c.pair) << '/' << protoCaseName(c.proto) <<
    "/j" << c.jobs << 'n' << c.requests;
}

void usage(int code = 1)
{
  std::cerr <<
    "Usage: ZhttpMatrixTest [OPTION]...\n\n"
    "Options:\n"
    "  --pair=PAIR       all, zhttpCaddy, zhttpZhttpd, curlZhttpd\n"
    "  --proto=PROTO     all, h1tcp, h1tls, h3\n"
    "  -j, --jobs=N      select workload concurrency\n"
    "  -n, --requests=N  select workload request count\n"
    "  --case=CASE       exact case, e.g. ZhttpZhttpd/H3/j10n1000\n"
    "  -q, --quiet       quiet output\n"
    "  -h, --help        show help\n" <<
    std::flush;
  ::exit(code);
}

bool validOptions()
{
  if (PairOpt::lookup(options.pair) < 0) return false;
  if (ProtoOpt::lookup(options.proto) < 0) return false;
  if (options.jobs && options.jobs != 1 && options.jobs != 10) return false;
  if (options.requests && options.requests != 1 && options.requests != 1000)
    return false;
  return true;
}

bool selected(const Case &c)
{
  if (options.caseName) {
    ZtString<> name;
    caseName(name, c);
    return name == options.caseName;
  }
  switch (PairOpt::lookup(options.pair)) {
    case PairOpt::all: break;
    case PairOpt::zhttpCaddy:
      if (c.pair != Pair::ZhttpCaddy) return false;
      break;
    case PairOpt::zhttpZhttpd:
      if (c.pair != Pair::ZhttpZhttpd) return false;
      break;
    case PairOpt::curlZhttpd:
      if (c.pair != Pair::CurlZhttpd) return false;
      break;
    default:
      return false;
  }
  switch (ProtoOpt::lookup(options.proto)) {
    case ProtoOpt::all: break;
    case ProtoOpt::h1tcp:
      if (c.proto != Proto::H1TCP) return false;
      break;
    case ProtoOpt::h1tls:
      if (c.proto != Proto::H1TLS) return false;
      break;
    case ProtoOpt::h3:
      if (c.proto != Proto::H3) return false;
      break;
    default:
      return false;
  }
  if (options.jobs && c.jobs != options.jobs) return false;
  if (options.requests && c.requests != options.requests) return false;
  return true;
}

template <typename L>
void eachCase(L l)
{
  Pair::T pairs[] = {
    Pair::ZhttpCaddy,
    Pair::ZhttpZhttpd,
    Pair::CurlZhttpd
  };
  Proto::T protos[] = {
    Proto::H1TCP,
    Proto::H1TLS,
    Proto::H3
  };
  struct Workload {
    unsigned jobs;
    unsigned requests;
  };
  Workload workloads[] = {
    {1, 1},
    {1, 1000},
    {10, 1000}
  };
  for (auto pair : pairs)
    for (auto proto : protos)
      for (auto workload : workloads)
	l(Case{pair, proto, workload.jobs, workload.requests});
}

bool anySelected()
{
  bool any = false;
  eachCase([&any](const Case &c) {
    if (selected(c)) any = true;
  });
  return any;
}

bool selectedNeedsCaddy()
{
  bool need = false;
  eachCase([&need](const Case &c) {
    if (selected(c) && c.pair == Pair::ZhttpCaddy) need = true;
  });
  return need;
}

bool selectedNeedsCurlH3()
{
  bool need = false;
  eachCase([&need](const Case &c) {
    if (selected(c) && c.proto == Proto::H3) need = true;
  });
  return need;
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
    "lsan_suppressions=\n"
    "for f in .lsan-suppressions ../../.lsan-suppressions; do\n"
    "  if [ -f \"$f\" ]; then\n"
    "    lsan_suppressions=$f\n"
    "    break\n"
    "  fi\n"
    "done\n"
    "if [ -n \"$lsan_suppressions\" ]; then\n"
    "  LSAN_OPTIONS=\"${LSAN_OPTIONS:+$LSAN_OPTIONS:}suppressions=$lsan_suppressions\"\n"
    "  export LSAN_OPTIONS\n"
    "fi\n"
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
      ": >" << tempPath << "/server.err\n"
      "for start_attempt in $(seq 1 20); do\n"
      "  caddy run --config " << caddyfile << " >" << tempPath <<
	"/server.out 2>" << tempPath << "/server.err &\n"
      "  pid=$!\n"
      "  sleep 0.1\n"
      "  if kill -0 \"$pid\" 2>/dev/null; then\n"
      "    break\n"
      "  fi\n"
      "  if grep -q 'address already in use' " << tempPath <<
	"/server.err; then\n"
      "    wait \"$pid\" 2>/dev/null || true\n"
      "    pid=\n"
      "    : >" << tempPath << "/server.err\n"
      "    sleep 0.2\n"
      "    continue\n"
      "  fi\n"
      "  break\n"
      "done\n";
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
      " >" << tempPath << "/server.out 2>" << tempPath << "/server.err &\n"
      "pid=$!\n";
  }
  script <<
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
  if (!temp.init("ZhttpMatrix")) {
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

uint64_t printCaseStart(const Case &c)
{
  uint64_t t = nowMS();
  std::cout << "# t=" << (t - startMS()) << "ms interop case: " <<
    pairName(c.pair) << ' ' << protoName(c.proto) <<
    " -j" << c.jobs << " -n" << c.requests << '\n';
  return t;
}

void printCaseEnd(const Case &c, bool ok, uint64_t start)
{
  uint64_t t = nowMS();
  std::cout << "# t=" << (t - startMS()) << "ms duration=" <<
    (t - start) << "ms " << (ok ? "ok" : "not ok") << ": " <<
    pairName(c.pair) << ' ' << protoName(c.proto) <<
    " -j" << c.jobs << " -n" << c.requests << '\n';
}

void runCase(Case c)
{
  ZuTestScopeRT(runCase);
  uint64_t start = printCaseStart(c);
  bool ok = runCase_(c);
  printCaseEnd(c, ok, start);
  ZuCheckRT(ok);
}

void testPrerequisites()
{
  ZuTestScopeRT(testPrerequisites);
  ZuCheckRT(anySelected());
  if (selectedNeedsCaddy())
    ZuCheckRT(haveCaddy());
  if (selectedNeedsCurlH3())
    ZuCheckRT(haveCurlH3());
}

bool prerequisitesOK()
{
  if (!anySelected()) return false;
  if (selectedNeedsCaddy() && !haveCaddy()) return false;
  if (selectedNeedsCurlH3() && !haveCurlH3()) return false;
  return true;
}

} // namespace

#define ZHTTP_INTEROP_CASE(pair_, proto_, j, n) \
  do { \
    Case c{Pair::pair_, Proto::proto_, j, n}; \
    if (selected(c)) \
      ZuTestCallRT_(#pair_ "/" #proto_ "/j" #j "n" #n, runCase, c); \
  } while (0)

#define ZHTTP_INTEROP_WORKLOADS(pair_, proto_) \
  ZHTTP_INTEROP_CASE(pair_, proto_, 1, 1); \
  ZHTTP_INTEROP_CASE(pair_, proto_, 1, 1000); \
  ZHTTP_INTEROP_CASE(pair_, proto_, 10, 1000)

#define ZHTTP_INTEROP_PROTOCOLS(pair_) \
  ZHTTP_INTEROP_WORKLOADS(pair_, H1TCP); \
  ZHTTP_INTEROP_WORKLOADS(pair_, H1TLS); \
  ZHTTP_INTEROP_WORKLOADS(pair_, H3)

#define ZHTTP_INTEROP_COUNT(pair_, proto_, j, n) \
  do { \
    Case c{Pair::pair_, Proto::proto_, j, n}; \
    if (selected(c)) ++nTests; \
  } while (0)

#define ZHTTP_INTEROP_COUNT_WORKLOADS(pair_, proto_) \
  ZHTTP_INTEROP_COUNT(pair_, proto_, 1, 1); \
  ZHTTP_INTEROP_COUNT(pair_, proto_, 1, 1000); \
  ZHTTP_INTEROP_COUNT(pair_, proto_, 10, 1000)

#define ZHTTP_INTEROP_COUNT_PROTOCOLS(pair_) \
  ZHTTP_INTEROP_COUNT_WORKLOADS(pair_, H1TCP); \
  ZHTTP_INTEROP_COUNT_WORKLOADS(pair_, H1TLS); \
  ZHTTP_INTEROP_COUNT_WORKLOADS(pair_, H3)

#define ZHTTP_INTEROP_RUN(pair_, proto_, j, n) \
  do { \
    Case c{Pair::pair_, Proto::proto_, j, n}; \
    if (selected(c)) { \
      ZtString<> name; \
      caseName(name, c); \
      uint64_t start = printCaseStart(c); \
      bool ok = prereqOK && runCase_(c); \
      printCaseEnd(c, ok, start); \
      pass &= ok; \
      std::cout << (ok ? "ok " : "not ok ") << ++testNo << " - " << \
	name << '\n'; \
    } \
  } while (0)

#define ZHTTP_INTEROP_RUN_WORKLOADS(pair_, proto_) \
  ZHTTP_INTEROP_RUN(pair_, proto_, 1, 1); \
  ZHTTP_INTEROP_RUN(pair_, proto_, 1, 1000); \
  ZHTTP_INTEROP_RUN(pair_, proto_, 10, 1000)

#define ZHTTP_INTEROP_RUN_PROTOCOLS(pair_) \
  ZHTTP_INTEROP_RUN_WORKLOADS(pair_, H1TCP); \
  ZHTTP_INTEROP_RUN_WORKLOADS(pair_, H1TLS); \
  ZHTTP_INTEROP_RUN_WORKLOADS(pair_, H3)

int main(int argc, char **argv)
{
  argc = ZtCLI::load(options, argc, const_cast<const char *const *>(argv));
  if (options.help) usage(0);
  if (argc != 1 || !validOptions()) usage();
  verbose = !options.quiet && !::getenv("HARNESS_ACTIVE");

  unsigned nTests = 1;
  ZHTTP_INTEROP_COUNT_PROTOCOLS(ZhttpCaddy);
  ZHTTP_INTEROP_COUNT_PROTOCOLS(ZhttpZhttpd);
  ZHTTP_INTEROP_COUNT_PROTOCOLS(CurlZhttpd);

  std::cout << "TAP version 14\n1.." << nTests << '\n';

  bool pass = true;
  unsigned testNo = 1;
  bool prereqOK = prerequisitesOK();
  pass &= prereqOK;
  std::cout << (prereqOK ? "ok " : "not ok ") << testNo <<
    " - testPrerequisites\n";

  ZHTTP_INTEROP_RUN_PROTOCOLS(ZhttpCaddy);
  ZHTTP_INTEROP_RUN_PROTOCOLS(ZhttpZhttpd);
  ZHTTP_INTEROP_RUN_PROTOCOLS(CurlZhttpd);

  return pass ? 0 : 1;
}
