//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdio.h>
#include <string.h>
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
constexpr unsigned DefaultCaseTimeout = 15;
constexpr unsigned DefaultStallTimeout = 15;
constexpr unsigned DefaultQuietTimeout = 5;
constexpr unsigned ReadyAttempts = 15;

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

struct Case {
  Pair::T	pair;
  Proto::T	proto;
  unsigned	jobs;
  unsigned	requests;
  unsigned	timeout;
  unsigned	stallTimeout;
  unsigned	quietTimeout;
};

using OptString = ZtString<ZtStringHeapID<"ZtCLI.Option">>;

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
  uint32_t	quicDiag = 0;
  uint32_t	memDiag = 0;
  bool		quiet = false;
  bool		help = false;
};

ZtStruct((Options, CLI),
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
  (((quicDiag), (CLI::Long<"quic-diag">)),                        (UInt32, 0)),
  (((memDiag),  (CLI::Long<"mem-diag">)),                         (UInt32, 0)),
  (((quiet),    (CLI::Flag<'q'>, CLI::Long<"quiet">)),            (Bool, false)),
  (((help),     (CLI::Flag<'h'>, CLI::Long<"help">)),             (Bool, false)));

Options options;

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

void caseName(ZtString<> &s, const Case &c)
{
  s << pairCaseName(c.pair) << '/' << protoCaseName(c.proto) <<
    "/j" << c.jobs << 'n' << c.requests;
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

int findChar(ZuCSpan s, char c, unsigned off = 0)
{
  for (unsigned i = off, n = s.length(); i < n; ++i)
    if (s[i] == c) return int(i);
  return -1;
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
  unsigned jobs, requests;
  if (!parseUInt(ZuCSpan{s.data() + workload + 1,
	  unsigned(nOff) - workload - 1}, jobs) ||
      !parseUInt(ZuCSpan{s.data() + nOff + 1,
	  s.length() - unsigned(nOff) - 1}, requests))
    return false;
  Pair::T pair;
  Proto::T proto;
  if (!parsePair(ZuCSpan{s.data(), unsigned(pairEnd)}, pair) ||
      !parseProto(ZuCSpan{s.data() + pairEnd + 1,
	unsigned(protoEnd - pairEnd - 1)}, proto))
    return false;
  if (!jobs || !requests) return false;
  c = {pair, proto, jobs, requests,
    options.timeout, options.stallTimeout, options.quietTimeout};
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
  script << "if ! ";
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
      if (options.quicRxDrop)
	script << " --quic-rx-drop=" << options.quicRxDrop;
      if (options.quicTxDrop)
	script << " --quic-tx-drop=" << options.quicTxDrop;
      if (options.quicDiag)
	script << " --quic-diag=" << options.quicDiag;
      break;
  }
	  script << " -o " << tempPath << "/body ";
	  appendURL(script, c.proto, port);
	  script << " >" << tempPath << "/client.out 2>" << tempPath <<
	    "/client.err; then\n"
	    "  stop_pcap\n"
	    "  analyze_pcap\n"
	    "  cat " << tempPath << "/client.err\n"
	    "  exit 1\n"
	    "fi\n";
	  if ((options.debug || options.pcap) && c.proto == Proto::H3)
	    script <<
	      "stop_pcap\n"
	      "analyze_pcap\n";
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
  appendURL(script, c.proto, port);
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
	    "  analyze_pcap\n"
	    "  cat " << tempPath << "/curl.err\n"
	    "  exit 1\n"
	    "fi\n";
	  if ((options.debug || options.pcap) && c.proto == Proto::H3)
	    script <<
	      "stop_pcap\n"
	      "analyze_pcap\n";
}

bool writeScript(
  ZuCSpan path, const Case &c, unsigned port, ZuCSpan tempPath,
  ZuCSpan rootPath, ZuCSpan certPath, ZuCSpan keyPath, ZuCSpan caddyfile)
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
	    "analyze_pcap() {\n"
	    "  [ -s \"$pcap_file\" ] || return 0\n"
	    "  if command -v tshark >/dev/null 2>&1; then\n"
	    "    tshark -o \"tls.keylog_file:$key_log_file\" "
	      "-r \"$pcap_file\" -Y 'udp.port == " << port << "' "
	      "-T fields -e frame.time_epoch -e udp.srcport -e udp.dstport "
	      "-e udp.length >\"$pcap_tsv\" 2>" << tempPath <<
	      "/pcap.err || true\n"
	    "    awk 'NR==1 { first=$1; last=$1; count=1; next } "
	      "NF { gap=$1-last; if (gap > max) max=gap; last=$1; ++count } "
	      "END { if (count) { printf(\"pcap packets=%u first=%.6f "
	      "last=%.6f max_gap=%.3f\\n\", count, first, last, max); "
	      "if (max >= 3.0) printf(\"pcap stall gap=%.3f\\n\", max); } }' "
	      "\"$pcap_tsv\" >\"$pcap_summary\"\n"
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
    "server=gzhttpd\n"
    "client=gzhttp\n"
    "[ -x \"$server\" ] || server=./zhttp/test/zhttpd\n"
    "[ -x \"$client\" ] || client=./zhttp/test/zhttp\n";
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
	    if (options.quicRxDrop)
	      script << " --quic-rx-drop=" << options.quicRxDrop;
	    if (options.quicTxDrop)
	      script << " --quic-tx-drop=" << options.quicTxDrop;
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
	  return writeFile(path, cspan(script), 0777);
	}

	void preserveTemp(TempDir &temp)
	{
	  if ((!preserveLogs() && !options.pcap) || !temp.path[0]) return;
	  std::cout << "# preserved logs: " << static_cast<const char *>(temp.path) <<
	    '\n';
	  temp.path[0] = 0;
	}

bool runCase_(const Case &c)
{
  TempDir temp;
  if (!temp.init("zhttpmatrix")) {
    std::cout << "# failed to create temporary directory\n";
    return false;
  }
  ZtString<> rootPath;
  if (!writeRoot(temp, rootPath)) {
    std::cout << "# failed to create static root\n";
    preserveTemp(temp);
    return false;
  }
  ZtString<> certPath, keyPath;
  if (!writeSelfSignedLocalhostCert(temp, certPath, keyPath)) {
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
      !writeCaddyfile(caddyfile, c.proto, port, cspan(rootPath),
	cspan(certPath), cspan(keyPath))) {
    std::cout << "# failed to write Caddyfile\n";
    preserveTemp(temp);
    return false;
  }
  auto script = temp.pathOf("matrix.sh");
  if (!writeScript(script, c, port, static_cast<const char *>(temp.path),
      cspan(rootPath), cspan(certPath), cspan(keyPath), cspan(caddyfile))) {
    std::cout << "# failed to write matrix script\n";
    preserveTemp(temp);
    return false;
  }

  ZtString<> cmd;
  cmd << "sh " << script;
  if (systemOK(::system(cmd.data()))) {
    preserveTemp(temp);
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

int main(int argc, char **argv)
{
  argc = ZtCLI::load(options, argc, const_cast<const char *const *>(argv));
  if (options.help) usage(0);
  if (argc != 1) usage();
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
    uint64_t start = printCaseStart(c);
    bool ok = prereqOK && runCase_(c);
    printCaseEnd(c, ok, start);
    pass &= ok;
    std::cout << (ok ? "ok " : "not ok ") << ++testNo << " - " <<
      name << '\n';
  });

  return pass ? 0 : 1;
}
