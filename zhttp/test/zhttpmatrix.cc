//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <iostream>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZiFile.hh>
#include <zlib/ZfCLI.hh>
#include <zlib/Zquic.hh>

#include "ZhttpTestUtil.hh"

using namespace ZuTestUtil;

namespace {

using Zhttp::Test::TempDir;
using Zhttp::Test::haveCurlH3;
using Zhttp::Test::loopbackPort;
using Zhttp::Test::printFile;
using Zhttp::Test::systemOK;
using Zhttp::Test::writeLocalhostCert;
using Zquic::Test::haveCaddy;

ZuCSpan Path = "/zhttp-interop";
ZuCSpan Body = "zhttp-interop-ok";
constexpr unsigned DefaultCaseTimeout = 15;
constexpr unsigned DefaultStallTimeout = 15;
constexpr unsigned DefaultQuietTimeout = 5;
constexpr unsigned ReadyAttempts = 15;
// Large enough to exercise post-header/body migration without dominating
// interop runtime.
constexpr uint64_t MigrationAfterBytes = 1024;
constexpr unsigned LargeBodyLines = 16384;

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
  enum T { ZhttpCaddy, ZhttpZhttpd, CurlCaddy, CurlZhttpd };
}

namespace Proto {
  enum T { H1TCP, H1TLS, H3 };
}

namespace Scenario {
  enum T {
    Default,
    MigrateHeaders,
    MigrateBytes,
    MigrateDrop,
    MigrateCaddy,
    MigrateCurl
  };
}

struct Case {
  Pair::T	pair;
  Proto::T	proto;
  unsigned	jobs;
  unsigned	requests;
  unsigned	timeout;
  unsigned	stallTimeout;
  unsigned	quietTimeout;
  Scenario::T	scenario = Scenario::Default;
};

using OptString = ZtString<ZtStringHeapID<"ZfCLI.Option">>;

struct Options {
  OptString	caseName;
  uint32_t	timeout = DefaultCaseTimeout;
  uint32_t	stallTimeout = DefaultStallTimeout;
  uint32_t	quietTimeout = DefaultQuietTimeout;
  bool		debug = false;
  bool		frag = false;
  bool		yield = false;
  bool		pcap = false;
  bool		discardResponse = false;
  OptString	quicRxDrop;
  OptString	quicTxDrop;
  OptString	quicMigration{"passive"};
  uint32_t	quicMigrationCIDReserve = 1;
  bool		quicMigrationCloseOnFailure = false;
  OptString	quicMigrationLocal;
  bool		quicMigrateLocal = false;
  bool		quicMigrateAfterHeaders = false;
  uint64_t	quicMigrateAfterBytes = 0;
  uint32_t	quicDiag = 0;
  uint32_t	memDiag = 0;
  bool		quiet = false;
  bool		help = false;
};

ZfStruct((Options, CLI),
  (((caseName), (CLI::Long<"case">)),                            (String, "")),
  (((timeout),  (CLI::Long<"timeout">)),                         (UInt32, DefaultCaseTimeout)),
  (((stallTimeout),
    (CLI::Long<"stall-timeout">)),                                (UInt32, DefaultStallTimeout)),
  (((quietTimeout),
    (CLI::Long<"quiet-timeout">)),                                (UInt32, DefaultQuietTimeout)),
  (((debug),    (CLI::Long<"debug">)),                            (Bool, false)),
  (((frag),     (CLI::Long<"frag">)),                             (Bool, false)),
  (((yield),    (CLI::Long<"yield">)),                            (Bool, false)),
  (((pcap),     (CLI::Long<"pcap">)),                             (Bool, false)),
  (((discardResponse),
    (CLI::Long<"discard-response">)),                             (Bool, false)),
  (((quicRxDrop), (CLI::Long<"quic-rx-drop">)),                   (String, "")),
  (((quicTxDrop), (CLI::Long<"quic-tx-drop">)),                   (String, "")),
  (((quicMigration), (CLI::Long<"quic-migration">)),              (String, "passive")),
  (((quicMigrationCIDReserve),
    (CLI::Long<"quic-migration-cid-reserve">)),                   (UInt32, 1)),
  (((quicMigrationCloseOnFailure),
    (CLI::Long<"quic-migration-close-on-failure">)),              (Bool, false)),
  (((quicMigrationLocal),
    (CLI::Long<"quic-migration-local">)),                         (String, "")),
  (((quicMigrateLocal),
    (CLI::Long<"quic-migrate-local">)),                           (Bool, false)),
  (((quicMigrateAfterHeaders),
    (CLI::Long<"quic-migrate-after-headers">)),                   (Bool, false)),
  (((quicMigrateAfterBytes),
    (CLI::Long<"quic-migrate-after-bytes">)),                     (UInt64, 0)),
  (((quicDiag), (CLI::Long<"quic-diag">)),                        (UInt32, 0)),
  (((memDiag),  (CLI::Long<"mem-diag">)),                         (UInt32, 0)),
  (((quiet),    (CLI::Flag<'q'>, CLI::Long<"quiet">)),            (Bool, false)),
  (((help),     (CLI::Flag<'h'>, CLI::Long<"help">)),             (Bool, false)));

Options options;
ZtString<> matrixDir;

const char *pairName(Pair::T pair)
{
  switch (pair) {
    case Pair::ZhttpCaddy: return "zhttp>caddy";
    case Pair::ZhttpZhttpd: return "zhttp>zhttpd";
    case Pair::CurlCaddy: return "curl>caddy";
    case Pair::CurlZhttpd: return "curl>zhttpd";
  }
  return "unknown";
}

const char *pairCaseName(Pair::T pair)
{
  switch (pair) {
    case Pair::ZhttpCaddy: return "zhttp-caddy";
    case Pair::ZhttpZhttpd: return "zhttp-zhttpd";
    case Pair::CurlCaddy: return "curl-caddy";
    case Pair::CurlZhttpd: return "curl-zhttpd";
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

const char *protoCaseName(Proto::T proto)
{
  switch (proto) {
    case Proto::H1TCP: return "h1-tcp";
    case Proto::H1TLS: return "h1-tls";
    case Proto::H3: return "h3";
  }
  return "unknown";
}

const char *scenarioCaseName(Scenario::T scenario)
{
  switch (scenario) {
    case Scenario::Default: return "";
    case Scenario::MigrateHeaders: return "/mig-headers";
    case Scenario::MigrateBytes: return "/mig-bytes";
    case Scenario::MigrateDrop: return "/mig-drop";
    case Scenario::MigrateCaddy: return "/mig-caddy";
    case Scenario::MigrateCurl: return "/mig-curl";
  }
  return "/unknown";
}

void caseName(ZtString<> &s, const Case &c)
{
  s << pairCaseName(c.pair) << '/' << protoCaseName(c.proto) <<
    "/j" << c.jobs << 'n' << c.requests << scenarioCaseName(c.scenario);
}

void usage(int code = 1)
{
  std::cerr <<
    "Usage: zhttpmatrix [OPTION]...\n\n"
    "Options:\n"
    "  --case=CASE       exact case, e.g. zhttp-caddy/h3/j10n100000\n"
    "                    default matrix excludes curl-caddy; explicit cases may use it\n"
    "  --timeout=N       client timeout in seconds, default 15, 0 disables\n"
    "  --stall-timeout=N no-progress stall timeout in seconds, default 15,\n"
    "                    0 disables; zhttp only\n"
    "  --quiet-timeout=N quiet transport timeout in seconds, default 5,\n"
    "                    0 disables; zhttp only\n"
    "  --debug           debug zhttp/zhttpd and preserve case directories\n"
    "  --frag            fragment zhttp/zhttpd ZiMultiplex\n"
    "  --yield           yield in zhttp/zhttpd ZiMultiplex\n"
    "  --pcap            capture H3 UDP traffic and preserve case directories\n"
    "  --discard-response discard zhttp response bodies\n"
    "  --quic-rx-drop=N% pass QUIC receive packet drop rate to zhttp/zhttpd\n"
    "  --quic-tx-drop=N% pass QUIC transmit packet drop rate to zhttp/zhttpd\n"
    "  --quic-migration=MODE\n"
    "                    pass QUIC migration policy to zhttp/zhttpd H3 rows;\n"
    "                    disabled, passive, active; default passive\n"
    "  --quic-migration-cid-reserve=N\n"
    "                    pass peer CID reserve to zhttp/zhttpd H3 rows\n"
    "  --quic-migration-close-on-failure\n"
    "                    pass active migration close-on-failure to H3 rows\n"
    "  --quic-migration-local=ADDR[:PORT]\n"
    "                    pass explicit client local migration address\n"
    "  --quic-migrate-local\n"
    "                    request one zhttp H3 client local UDP port migration;\n"
    "                    requires --quic-migration=active\n"
    "  --quic-migrate-after-headers\n"
    "                    request zhttp H3 client migration after headers\n"
    "  --quic-migrate-after-bytes=N\n"
    "                    request zhttp H3 client migration after N body bytes\n"
#ifdef Zquic_DEBUG
    "  --quic-diag=N     pass QUIC diagnostic print interval in seconds\n"
#endif
    "  --mem-diag=N      pass memory diagnostic print interval in seconds\n"
    "  -q, --quiet       quiet output\n"
    "  -h, --help        show help\n" <<
    std::flush;
  ::exit(code);
}

bool preserveLogs()
{
  return options.debug || options.memDiag
    || options.quicDiag
    ;
}

bool selected(const Case &c)
{
  if (options.caseName) return true;
  if (c.pair == Pair::CurlCaddy) return false;
  return true;
}

bool scenarioMigrates(const Case &c)
{
  return c.scenario == Scenario::MigrateHeaders ||
    c.scenario == Scenario::MigrateBytes ||
    c.scenario == Scenario::MigrateDrop ||
    c.scenario == Scenario::MigrateCaddy;
}

ZuCSpan caseMigrationMode(const Case &c)
{
  if (c.scenario == Scenario::MigrateCurl) return "passive";
  return scenarioMigrates(c) ? ZuCSpan{"active"} : ZuCSpan{options.quicMigration};
}

unsigned caseMigrationCIDReserve(const Case &c)
{
  unsigned reserve = options.quicMigrationCIDReserve;
  if (scenarioMigrates(c) && reserve < 2) reserve = 2;
  return reserve;
}

bool caseMigrateAfterHeaders(const Case &c)
{
  return options.quicMigrateAfterHeaders ||
    c.scenario == Scenario::MigrateHeaders ||
    c.scenario == Scenario::MigrateDrop ||
    c.scenario == Scenario::MigrateCaddy;
}

uint64_t caseMigrateAfterBytes(const Case &c)
{
  if (options.quicMigrateAfterBytes) return options.quicMigrateAfterBytes;
  return c.scenario == Scenario::MigrateBytes ? MigrationAfterBytes : 0;
}

bool caseLargeBody(const Case &c)
{
  return c.scenario == Scenario::MigrateBytes;
}

bool caseQuicRxDrop(const Case &c)
{
  return options.quicRxDrop || c.scenario == Scenario::MigrateDrop;
}

bool caseQuicTxDrop(const Case &c)
{
  return options.quicTxDrop || c.scenario == Scenario::MigrateDrop;
}

ZuCSpan caseQuicRxDropValue(const Case &c)
{
  return options.quicRxDrop ? ZuCSpan{options.quicRxDrop} : ZuCSpan{"1%"};
}

ZuCSpan caseQuicTxDropValue(const Case &c)
{
  return options.quicTxDrop ? ZuCSpan{options.quicTxDrop} : ZuCSpan{"1%"};
}

ZuCSpan caseMigrationLocal(const Case &c)
{
  if (options.quicMigrationLocal) return options.quicMigrationLocal;
  return scenarioMigrates(c) ? ZuCSpan{"127.0.0.1:0"} : ZuCSpan{};
}

bool parsePair(ZuCSpan name, Pair::T &pair)
{
  if (name == "zhttp-caddy") {
    pair = Pair::ZhttpCaddy;
    return true;
  }
  if (name == "zhttp-zhttpd") {
    pair = Pair::ZhttpZhttpd;
    return true;
  }
  if (name == "curl-caddy") {
    pair = Pair::CurlCaddy;
    return true;
  }
  if (name == "curl-zhttpd") {
    pair = Pair::CurlZhttpd;
    return true;
  }
  return false;
}

bool parseProto(ZuCSpan name, Proto::T &proto)
{
  if (name == "h1-tcp") {
    proto = Proto::H1TCP;
    return true;
  }
  if (name == "h1-tls") {
    proto = Proto::H1TLS;
    return true;
  }
  if (name == "h3") {
    proto = Proto::H3;
    return true;
  }
  return false;
}

bool parseScenario(ZuCSpan name, Scenario::T &scenario)
{
  if (!name) {
    scenario = Scenario::Default;
    return true;
  }
  if (name == "mig-headers") {
    scenario = Scenario::MigrateHeaders;
    return true;
  }
  if (name == "mig-bytes") {
    scenario = Scenario::MigrateBytes;
    return true;
  }
  if (name == "mig-drop") {
    scenario = Scenario::MigrateDrop;
    return true;
  }
  if (name == "mig-caddy") {
    scenario = Scenario::MigrateCaddy;
    return true;
  }
  if (name == "mig-curl") {
    scenario = Scenario::MigrateCurl;
    return true;
  }
  return false;
}

int findChar(ZuCSpan s, char c, unsigned off = 0)
{
  for (unsigned i = off, n = s.length(); i < n; ++i)
    if (s[i] == c) return int(i);
  return -1;
}

bool hasDirSep(ZuCSpan s)
{
  for (unsigned i = 0, n = s.length(); i < n; ++i)
    if (s[i] == '/' || s[i] == '\\') return true;
  return false;
}

ZtString<> pathAbs(ZuCSpan path)
{
#ifndef _WIN32
  ZiFile::Path p;
  p << path;
  if (ZiFile::absolute(p)) return p;
  return ZiFile::append(ZiFile::cwd(), p);
#else
  ZtString<> p;
  p << path;
  return p;
#endif
}

bool pathExists(ZuCSpan path)
{
#ifndef _WIN32
  ZiFile::Path p;
  p << path;
  return ZiStat{ZuMv(p)}.exists();
#else
  return bool(path);
#endif
}

ZtString<> pathDirname(ZuCSpan path)
{
#ifndef _WIN32
  ZiFile::Path p;
  p << path;
  return ZiFile::dirname(p);
#else
  int off = -1;
  for (unsigned i = 0, n = path.length(); i < n; ++i)
    if (path[i] == '/' || path[i] == '\\') off = int(i);
  if (off < 0) return ".";
  if (!off) return "/";
  ZtString<> dir;
  dir << ZuCSpan{path.data(), unsigned(off)};
  return dir;
#endif
}

ZtString<> executableBinDir(ZuCSpan dir)
{
#ifndef _WIN32
  ZiFile::Path p;
  p << dir;
  if (ZiFile::leafname(p) == ".libs") return ZiFile::dirname(p);
#endif
  ZtString<> out;
  out << dir;
  return out;
}

ZtString<> executableDir(const char *argv0)
{
  if (!argv0 || !argv0[0]) return ".";

  ZuCSpan arg0{argv0};
  if (hasDirSep(arg0))
    return executableBinDir(pathDirname(pathAbs(arg0).cspan()).cspan());

  if (const char *path_ = ::getenv("PATH")) {
    ZuCSpan path{path_};
    unsigned begin = 0;
    for (unsigned i = 0, n = path.length(); i <= n; ++i) {
      if (i < n && path[i] != ':') continue;
      ZtString<> dir;
      if (i > begin)
	dir << ZuCSpan{path.data() + begin, i - begin};
      else
	dir << '.';
      ZtString<> candidate;
      candidate << dir << '/' << arg0;
      ZtString<> abs = pathAbs(candidate.cspan());
      if (pathExists(abs.cspan()))
	return executableBinDir(pathDirname(abs.cspan()).cspan());
      begin = i + 1;
    }
  }

  return ".";
}

void appendShellQuote(ZtString<> &s, ZuCSpan v)
{
  s << '\'';
  for (unsigned i = 0, n = v.length(); i < n; ++i) {
    if (v[i] == '\'')
      s << "'\\''";
    else
      s << v[i];
  }
  s << '\'';
}

bool parseUInt(ZuCSpan s, unsigned &v)
{
  if (!s) return false;
  unsigned v_ = 0;
  for (unsigned i = 0, n = s.length(); i < n; ++i) {
    unsigned c = s[i] - '0';
    if (c > 9) return false;
    v_ = (v_ * 10) + c;
  }
  v = v_;
  return true;
}

bool parseCase(Case &c)
{
  ZuCSpan s{options.caseName.data(), options.caseName.length()};
  int pairEnd = findChar(s, '/');
  if (pairEnd <= 0) return false;
  int protoEnd = findChar(s, '/', pairEnd + 1);
  if (protoEnd <= pairEnd + 1) return false;
  unsigned workload = unsigned(protoEnd + 1);
  if (workload + 1 >= s.length() || s[workload] != 'j') return false;
  int nOff = findChar(s, 'n', workload + 1);
  if (nOff <= int(workload + 1) || nOff + 1 >= int(s.length()))
    return false;
  int scenarioOff = findChar(s, '/', nOff + 1);
  unsigned requestEnd = scenarioOff < 0 ? s.length() : unsigned(scenarioOff);
  unsigned jobs, requests;
  if (!parseUInt(ZuCSpan{s.data() + workload + 1,
	  unsigned(nOff) - workload - 1}, jobs) ||
      !parseUInt(ZuCSpan{s.data() + nOff + 1,
	  requestEnd - unsigned(nOff) - 1}, requests))
    return false;
  Pair::T pair;
  Proto::T proto;
  Scenario::T scenario;
  if (!parsePair(ZuCSpan{s.data(), unsigned(pairEnd)}, pair) ||
      !parseProto(ZuCSpan{s.data() + pairEnd + 1,
	unsigned(protoEnd - pairEnd - 1)}, proto) ||
      !parseScenario(
	scenarioOff < 0 ? ZuCSpan{} :
	  ZuCSpan{s.data() + unsigned(scenarioOff) + 1,
	    s.length() - unsigned(scenarioOff) - 1},
	scenario))
    return false;
  if (!jobs || !requests) return false;
  c = {pair, proto, jobs, requests,
    options.timeout, options.stallTimeout, options.quietTimeout, scenario};
  return true;
}

template <typename L>
void eachCase(L l)
{
  if (options.caseName) {
    Case c;
    if (parseCase(c)) l(c);
    return;
  }
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
  unsigned jobs[] = {1, 5};
  unsigned requests[] = {1, 10, 1000};
  for (auto pair : pairs)
    for (auto proto : protos)
      for (auto job : jobs)
	for (auto request : requests)
	  l(Case{pair, proto, job, request,
	      options.timeout, options.stallTimeout, options.quietTimeout});
  l(Case{
    Pair::ZhttpZhttpd, Proto::H3, 1, 1,
    options.timeout, options.stallTimeout, options.quietTimeout,
    Scenario::MigrateHeaders});
  l(Case{
    Pair::ZhttpZhttpd, Proto::H3, 1, 1,
    options.timeout, options.stallTimeout, options.quietTimeout,
    Scenario::MigrateBytes});
  l(Case{
    Pair::ZhttpZhttpd, Proto::H3, 1, 1,
    options.timeout, options.stallTimeout, options.quietTimeout,
    Scenario::MigrateDrop});
  l(Case{
    Pair::ZhttpCaddy, Proto::H3, 1, 1,
    options.timeout, options.stallTimeout, options.quietTimeout,
    Scenario::MigrateCaddy});
  l(Case{
    Pair::CurlZhttpd, Proto::H3, 1, 1,
    options.timeout, options.stallTimeout, options.quietTimeout,
    Scenario::MigrateCurl});
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
    if (selected(c) &&
	(c.pair == Pair::ZhttpCaddy || c.pair == Pair::CurlCaddy))
      need = true;
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

bool validMigrationOptions()
{
  if (!Zquic::validMigrationMode(options.quicMigration)) return false;
  bool active = options.quicMigration == "active";
  bool migrates =
    options.quicMigrateLocal || options.quicMigrationLocal ||
    options.quicMigrateAfterHeaders || options.quicMigrateAfterBytes;
  if (migrates && !active)
    return false;
  return true;
}

bool haveCurlH3Migration()
{
  static int available = -1;
  if (available < 0)
    available = systemOK(::system(
      "curl --help all 2>/dev/null | "
      "grep -Eq -- '--quic-migrate|--quic-rebind|--http3-migration'")) ? 1 : 0;
  return available;
}

bool skipCase(const Case &c, ZtString<> &reason)
{
  if (c.scenario == Scenario::MigrateCurl && !haveCurlH3Migration()) {
    reason = "curl lacks HTTP/3 migration/rebind CLI";
    return true;
  }
  return false;
}

bool writeFile(ZuCSpan path, ZuCSpan data, unsigned mode = 0666)
{
  ZiFile f;
  if (f.open(path, ZiFile::Write, mode) != Zi::OK) return false;
  if (f.write(data.data(), data.length()) != Zi::OK) return false;
  f.close();
  return true;
}

bool pcapEnabled(const Case &c)
{
  return (options.debug || options.pcap) && c.proto == Proto::H3;
}

void analyzePcap(unsigned port, TempDir &temp)
{
  auto scriptPath = temp.pathOf("pcap-analyze.sh");
  auto pcapPath = temp.pathOf("traffic.pcapng");
  auto tsvPath = temp.pathOf("traffic.tsv");
  auto summaryPath = temp.pathOf("pcap.summary");
  auto errPath = temp.pathOf("pcap.err");
  auto keyLogPath = temp.pathOf("keylog.txt");
  ZtString<> script;
  script <<
    "#!/bin/sh\n"
    "set -eu\n"
    "pcap_file=";
  appendShellQuote(script, pcapPath.cspan());
  script << "\n"
    "pcap_tsv=";
  appendShellQuote(script, tsvPath.cspan());
  script << "\n"
    "pcap_summary=";
  appendShellQuote(script, summaryPath.cspan());
  script << "\n"
    "pcap_err=";
  appendShellQuote(script, errPath.cspan());
  script << "\n"
    "key_log_file=";
  appendShellQuote(script, keyLogPath.cspan());
  script << "\n"
    "[ -s \"$pcap_file\" ] || exit 0\n"
    "if command -v tshark >/dev/null 2>&1; then\n"
    "  tshark -o \"tls.keylog_file:$key_log_file\" "
      "-r \"$pcap_file\" -Y 'udp.port == " << port << "' "
      "-T fields -e frame.time_epoch -e udp.srcport -e udp.dstport "
      "-e udp.length >\"$pcap_tsv\" 2>\"$pcap_err\" || true\n"
    "  awk 'NR==1 { first=$1; last=$1; count=1; next } "
      "NF { gap=$1-last; if (gap > max) max=gap; last=$1; ++count } "
      "END { if (count) { printf(\"pcap packets=%u first=%.6f "
      "last=%.6f max_gap=%.3f\\n\", count, first, last, max); "
      "if (max >= 3.0) printf(\"pcap stall gap=%.3f\\n\", max); } }' "
      "\"$pcap_tsv\" >\"$pcap_summary\"\n"
    "fi\n";
  if (!writeFile(scriptPath.cspan(), script.cspan(), 0777)) return;
  ZtString<> cmd;
  cmd << "sh ";
  appendShellQuote(cmd, scriptPath.cspan());
  (void)::system(cmd.data());
}

bool writeRoot(TempDir &temp, const Case &c, ZtString<> &rootPath)
{
  rootPath = temp.pathOf("root");
  if (ZiFile::mkdir(rootPath) != Zi::OK) return false;
  ZtString<> filePath;
  filePath << rootPath << Path;
  ZtString<> body;
  if (caseLargeBody(c)) {
    for (unsigned i = 0; i < LargeBodyLines; ++i)
      body << Body << '\n';
  } else
    body << Body << '\n';
  return writeFile(filePath, body.cspan());
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
		"  if curl --http1.1 --fail -sS --connect-timeout 1 --max-time 1 "
	"http://127.0.0.1:" << port << Path <<
	" >/dev/null 2>&1; then\n";
      break;
    case Proto::H1TLS:
      script <<
	"  if curl --http1.1 --cacert " << certPath <<
		" --fail -sS --connect-timeout 1 --max-time 1 "
	"--resolve localhost:" << port << ":127.0.0.1 "
	"https://localhost:" << port << Path <<
	" >/dev/null 2>&1; then\n";
      break;
    case Proto::H3:
      script <<
	"  if curl --http3-only --cacert " << certPath <<
		" --fail -sS --connect-timeout 1 --max-time 1 "
	"--resolve localhost:" << port << ":127.0.0.1 "
	"https://localhost:" << port << Path <<
	" >/dev/null 2>&1; then\n";
      break;
  }
}

void appendURL(ZtString<> &s, const Case &c, unsigned port)
{
  if (c.proto == Proto::H1TCP)
    s << "http://127.0.0.1:" << port << Path;
  else if (c.pair == Pair::ZhttpZhttpd)
    s << "https://127.0.0.1:" << port << Path;
  else
    s << "https://localhost:" << port << Path;
}

void appendZhttpCommand(
  ZtString<> &script, const Case &c, unsigned port, ZuCSpan certPath,
  ZuCSpan tempPath)
{
  script << "if ! ";
  if (c.timeout) script << "timeout " << c.timeout << "s ";
  script << "\"$client\" -j " << c.jobs << " -n " << c.requests;
#ifdef ZiMultiplex_DEBUG
  if (options.debug) script << " --debug";
  if (options.frag && c.proto != Proto::H3) script << " --frag";
  if (options.yield) script << " --yield";
#endif
  if (options.discardResponse) script << " --discard-response";
  if (options.memDiag) script << " --mem-diag=" << options.memDiag;
  script << " --timeout=" << c.timeout <<
    " --stall-timeout=" << c.stallTimeout <<
    " --quiet-timeout=" << c.quietTimeout;
  switch (c.proto) {
    case Proto::H1TCP:
      break;
    case Proto::H1TLS:
      script << " --http3=disable -c " << certPath;
      break;
    case Proto::H3:
      script << " --http3=force -c " << certPath;
      if (options.debug || options.pcap)
	script << " --key-log=$key_log_file";
      if (caseQuicRxDrop(c))
	script << " --quic-rx-drop=" << caseQuicRxDropValue(c);
      if (caseQuicTxDrop(c))
	script << " --quic-tx-drop=" << caseQuicTxDropValue(c);
      if (caseMigrationMode(c))
	script << " --quic-migration=" << caseMigrationMode(c);
      script << " --quic-migration-cid-reserve=" <<
	caseMigrationCIDReserve(c);
      if (options.quicMigrationCloseOnFailure)
	script << " --quic-migration-close-on-failure";
      if (ZuCSpan local = caseMigrationLocal(c))
	script << " --quic-migration-local=" << local;
      if (options.quicMigrateLocal)
	script << " --quic-migrate-local";
      if (caseMigrateAfterHeaders(c))
	script << " --quic-migrate-after-headers";
      if (uint64_t bytes = caseMigrateAfterBytes(c))
	script << " --quic-migrate-after-bytes=" << bytes;
      if (options.quicDiag)
	script << " --quic-diag=" << options.quicDiag;
      break;
  }
  script << " -o " << tempPath << "/body ";
  appendURL(script, c, port);
  script << " >" << tempPath << "/client.out 2>" << tempPath <<
    "/client.err; then\n"
    "  stop_pcap\n"
    "  cat " << tempPath << "/client.err\n"
    "  exit 1\n"
    "fi\n";
  if ((options.debug || options.pcap) && c.proto == Proto::H3)
    script << "stop_pcap\n";
  if (!options.discardResponse)
    script <<
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
  appendURL(script, c, port);
  script <<
    "\"\\noutput = \"/dev/null\"\\n' >>\"$cfg\"\n"
    "  i=$((i + 1))\n"
    "done\n"
    "curl_opts='--fail -sS --connect-timeout 2";
  if (c.timeout) script << " --max-time " << c.timeout;
  script << "'\n";
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
    "if ! ";
  if (c.timeout) script << "timeout " << c.timeout << "s ";
  script <<
    "curl $curl_opts $curl_proto $curl_parallel "
    "--config \"$cfg\" >" << tempPath << "/curl.out 2>" << tempPath <<
    "/curl.err; then\n"
    "  stop_pcap\n"
    "  cat " << tempPath << "/curl.err\n"
    "  exit 1\n"
    "fi\n";
  if ((options.debug || options.pcap) && c.proto == Proto::H3)
    script << "stop_pcap\n";
}

bool writeScript(
  ZuCSpan path, const Case &c, unsigned port, ZuCSpan tempPath,
  ZuCSpan rootPath, ZuCSpan certPath, ZuCSpan keyPath, ZuCSpan caddyfile,
  ZuCSpan exeDir)
{
  bool caddy = c.pair == Pair::ZhttpCaddy || c.pair == Pair::CurlCaddy;
  bool zhttp = c.pair == Pair::ZhttpCaddy || c.pair == Pair::ZhttpZhttpd;
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
    "pcap_pid=\n"
    "pcap_file=" << tempPath << "/traffic.pcapng\n"
    "pcap_tsv=" << tempPath << "/traffic.tsv\n"
    "pcap_summary=" << tempPath << "/pcap.summary\n"
    "key_log_file=" << tempPath << "/keylog.txt\n"
    "stop_pcap() {\n"
    "  if [ -n \"$pcap_pid\" ]; then\n"
    "    kill -INT \"$pcap_pid\" 2>/dev/null || true\n"
    "    wait \"$pcap_pid\" 2>/dev/null || true\n"
    "    pcap_pid=\n"
    "  fi\n"
    "}\n"
    "cleanup() {\n"
    "  stop_pcap\n"
    "  if [ -n \"$pid\" ]; then\n"
    "    kill \"$pid\" 2>/dev/null || true\n"
    "    for i in $(seq 1 20); do\n"
    "      if ! kill -0 \"$pid\" 2>/dev/null; then\n"
    "        wait \"$pid\" 2>/dev/null || true\n"
    "        pid=\n"
    "        break\n"
    "      fi\n"
    "      sleep 0.05\n"
    "    done\n"
    "    if [ -n \"$pid\" ]; then\n"
    "      kill -KILL \"$pid\" 2>/dev/null || true\n"
    "      wait \"$pid\" 2>/dev/null || true\n"
    "    fi\n"
    "  fi\n"
    "}\n"
    "trap cleanup EXIT INT TERM\n"
    "bin_dir=";
  appendShellQuote(script, exeDir);
  script <<
    "\n"
    "server=gzhttpd\n"
    "client=gzhttp\n"
    "[ -x \"$server\" ] || server=\"$bin_dir/zhttpd\"\n"
    "[ -x \"$client\" ] || client=\"$bin_dir/zhttp\"\n";
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
#ifdef ZiMultiplex_DEBUG
    if (options.debug) script << " --debug";
    if (options.frag && c.proto != Proto::H3) script << " --frag";
    if (options.yield) script << " --yield";
#endif
    if (options.memDiag) script << " --mem-diag=" << options.memDiag;
    switch (c.proto) {
      case Proto::H1TCP:
	script << " --http";
	break;
      case Proto::H1TLS:
	script << " --https --cert " << certPath << " --key " << keyPath;
	break;
      case Proto::H3:
	script << " --http3 --cert " << certPath << " --key " << keyPath;
	if (options.debug || options.pcap)
	  script << " --key-log=$key_log_file";
	if (caseQuicRxDrop(c))
	  script << " --quic-rx-drop=" << caseQuicRxDropValue(c);
	if (caseQuicTxDrop(c))
	  script << " --quic-tx-drop=" << caseQuicTxDropValue(c);
	if (caseMigrationMode(c))
	  script << " --quic-migration=" << caseMigrationMode(c);
	script << " --quic-migration-cid-reserve=" <<
	  caseMigrationCIDReserve(c);
	if (options.quicMigrationCloseOnFailure)
	  script << " --quic-migration-close-on-failure";
	if (options.quicDiag)
	  script << " --quic-diag=" << options.quicDiag;
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
    "for i in $(seq 1 " << ReadyAttempts << "); do\n";
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
  if ((options.debug || options.pcap) && c.proto == Proto::H3)
    script <<
      "if command -v dumpcap >/dev/null 2>&1 && "
	"command -v tshark >/dev/null 2>&1; then\n"
      "  dumpcap -q -i lo -f 'udp port " << port <<
	"' -w \"$pcap_file\" >" << tempPath <<
	"/pcap.out 2>" << tempPath << "/pcap.err &\n"
      "  pcap_pid=$!\n"
      "  sleep 0.2\n"
      "else\n"
      "  echo 'pcap unavailable: dumpcap/tshark not executable' "
	">\"$pcap_summary\"\n"
      "fi\n";
  if (zhttp)
    appendZhttpCommand(script, c, port, certPath, tempPath);
  else
    appendCurlCommand(script, c, port, certPath, tempPath);
  return writeFile(path, script.cspan(), 0777);
}

void preserveTemp(TempDir &temp, bool force = false)
{
  if ((!force && !preserveLogs() && !options.pcap) || !temp.path[0])
    return;
  std::cout << "# preserved logs: " << static_cast<const char *>(temp.path) <<
    '\n';
  temp.path[0] = 0;
}

bool runCase_(const Case &c, uint64_t &duration)
{
  duration = 0;
  TempDir temp;
  if (!temp.init("zhttpmatrix")) {
    std::cout << "# failed to create temporary directory\n";
    return false;
  }
  ZtString<> rootPath;
  if (!writeRoot(temp, c, rootPath)) {
    std::cout << "# failed to create static root\n";
    preserveTemp(temp);
    return false;
  }
  ZtString<> certPath, keyPath;
  if (!writeLocalhostCert(temp, certPath, keyPath)) {
    std::cout << "# failed to create TLS certificate\n";
    preserveTemp(temp);
    return false;
  }
  unsigned port = loopbackPort();
  if (!port) {
    std::cout << "# failed to allocate loopback port\n";
    preserveTemp(temp);
    return false;
  }
  auto caddyfile = temp.pathOf("Caddyfile");
  if ((c.pair == Pair::ZhttpCaddy || c.pair == Pair::CurlCaddy) &&
      !writeCaddyfile(caddyfile, c.proto, port, rootPath.cspan(),
	certPath.cspan(), keyPath.cspan())) {
    std::cout << "# failed to write Caddyfile\n";
    preserveTemp(temp);
    return false;
  }
  auto script = temp.pathOf("matrix.sh");
  if (!writeScript(script, c, port, static_cast<const char *>(temp.path),
      rootPath.cspan(), certPath.cspan(), keyPath.cspan(), caddyfile.cspan(),
      matrixDir.cspan())) {
    std::cout << "# failed to write matrix script\n";
    preserveTemp(temp);
    return false;
  }

  ZtString<> cmd;
  cmd << "sh " << script;
  uint64_t runStart = nowMS();
  bool ok = systemOK(::system(cmd.data()));
  duration = nowMS() - runStart;
  if (pcapEnabled(c)) analyzePcap(port, temp);
  if (ok) {
    preserveTemp(temp, c.timeout && duration > uint64_t(c.timeout) * 1000);
    return true;
  }

  std::cout << "# failed case: " << pairName(c.pair) << ' ' <<
    protoName(c.proto) << " -j" << c.jobs << " -n" << c.requests << '\n';
  printFile("server stderr", temp.pathOf("server.err"));
  printFile("client stderr", temp.pathOf("client.err"));
  printFile("curl stderr", temp.pathOf("curl.err"));
  printFile("pcap summary", temp.pathOf("pcap.summary"));
  printFile("pcap stderr", temp.pathOf("pcap.err"));
  printFile("script", script);
  preserveTemp(temp);
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

void printCaseEnd(const Case &c, bool ok, uint64_t start, uint64_t duration)
{
  uint64_t t = nowMS();
  std::cout << "# t=" << (t - startMS()) << "ms duration=" <<
    (duration ? duration : t - start) << "ms " << (ok ? "ok" : "not ok") << ": " <<
    pairName(c.pair) << ' ' << protoName(c.proto) <<
    " -j" << c.jobs << " -n" << c.requests << '\n';
}

void runCase(Case c)
{
  ZuTestScopeRT(runCase);
  uint64_t start = printCaseStart(c);
  uint64_t duration;
  bool ok = runCase_(c, duration);
  printCaseEnd(c, ok, start, duration);
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

int main(int argc, char **argv)
{
  matrixDir = executableDir(argv[0]);
  argc = ZfCLI::load(options, argc, const_cast<const char *const *>(argv));
  if (options.help) usage(0);
  if (argc != 1) usage();
  if (!validMigrationOptions()) usage();
  verbose = !options.quiet && !::getenv("HARNESS_ACTIVE");

  unsigned nTests = 1;
  eachCase([&nTests](const Case &c) {
    if (selected(c)) ++nTests;
  });

  std::cout << "TAP version 14\n1.." << nTests << '\n';

  bool pass = true;
  unsigned testNo = 1;
  bool prereqOK = prerequisitesOK();
  pass &= prereqOK;
  std::cout << (prereqOK ? "ok " : "not ok ") << testNo <<
    " - testPrerequisites\n";

  eachCase([&pass, prereqOK, &testNo](const Case &c) {
    if (!selected(c)) return;
    ZtString<> name;
    caseName(name, c);
    ZtString<> skip;
    if (skipCase(c, skip)) {
      std::cout << "ok " << ++testNo << " - " << name << " # SKIP " <<
	skip << '\n';
      return;
    }
    uint64_t start = printCaseStart(c);
    uint64_t duration = 0;
    bool ok = prereqOK && runCase_(c, duration);
    printCaseEnd(c, ok, start, duration);
    pass &= ok;
    std::cout << (ok ? "ok " : "not ok ") << ++testNo << " - " <<
      name << '\n';
  });

  return pass ? 0 : 1;
}
