//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZiFile.hh>
#include <zlib/Zquic.hh>

#include "ZhttpTestUtil.hh"
#include "zhttpqir.hh"

using namespace ZuTestUtil;
using Zhttp::Test::TempDir;

struct TestEnvKV {
  const char *name = nullptr;
  const char *value = nullptr;
};

static const TestEnvKV *testEnv_;

static const char *testGetenv_(const char *name)
{
  if (!testEnv_) return nullptr;
  for (unsigned i = 0; testEnv_[i].name; ++i)
    if (!::strcmp(testEnv_[i].name, name)) return testEnv_[i].value;
  return nullptr;
}

static void testGetpath_(const char *name, Zi::Path &out)
{
  out.length(0);
  if (auto path = testGetenv_(name)) out = path;
}

static bool eq_(ZuCSpan a, ZuCSpan b)
{
  return a == b;
}

static bool eq_(const ZtString<> &a, ZuCSpan b)
{
  return a.cspan() == b;
}

static bool eq_(const Zi::Path &a, ZuCSpan b)
{
  return a == Zi::Path{b};
}

static void testRoleParsing()
{
  ZuTestScope(testRoleParsing);
  using namespace Zhttp::QIR;

  Role role = Client;
  ZuCHECK(parseRole("client", role) && role == Client,
    "client role parses");
  ZuCHECK(parseRole("server", role) && role == Server,
    "server role parses");
  ZuCHECK(!parseRole("--help", role), "help is not a role");
  ZuCHECK(!parseRole("bogus", role), "unknown role rejected");
  ZuCHECK(roleName(Client) == "client", "client role name");
  ZuCHECK(roleName(Server) == "server", "server role name");
}

static void testCaseParsing()
{
  ZuTestScope(testCaseParsing);
  using namespace Zhttp::QIR;

  ZuCHECK(parseCase("handshake") == Handshake, "handshake parses");
  ZuCHECK(parseCase("transfer") == Transfer, "transfer parses");
  ZuCHECK(parseCase("http3") == HTTP3, "http3 parses");
  ZuCHECK(parseCase("rebind-port") == RebindPort, "rebind-port parses");
  ZuCHECK(parseCase("rebind-addr") == RebindAddr, "rebind-addr parses");
  ZuCHECK(parseCase("connectionmigration") == ConnectionMigration,
    "connectionmigration parses");
  ZuCHECK(parseCase("retry") == Unsupported, "retry unsupported");
  ZuCHECK(parseCase("resumption") == Unsupported, "resumption unsupported");
  ZuCHECK(parseCase("zerortt") == Unsupported, "zerortt unsupported");
  ZuCHECK(parseCase("versionnegotiation") == Unsupported,
    "versionnegotiation unsupported");
  ZuCHECK(parseCase("chacha20") == Unsupported, "chacha20 unsupported");
  ZuCHECK(parseCase("keyupdate") == Unsupported, "keyupdate unsupported");
  ZuCHECK(parseCase("v2") == Unsupported, "v2 unsupported");

  ZuCHECK(caseSupported(Handshake), "handshake supported");
  ZuCHECK(caseSupported(Transfer), "transfer supported");
  ZuCHECK(caseSupported(HTTP3), "http3 supported");
  ZuCHECK(caseSupported(RebindPort), "rebind-port supported");
  ZuCHECK(caseSupported(RebindAddr), "rebind-addr supported");
  ZuCHECK(!caseSupported(ConnectionMigration),
    "connectionmigration initially unsupported");
  ZuCHECK(caseHQ(Handshake) && caseHQ(Transfer) && caseHQ(RebindPort) &&
      caseHQ(RebindAddr) && !caseHQ(HTTP3),
    "hq testcase classification");
  ZuCHECK(caseH3(HTTP3) && !caseH3(Handshake), "h3 testcase classification");
}

static void testEnvLoading()
{
  ZuTestScope(testEnvLoading);
  using namespace Zhttp::QIR;

  {
    const TestEnvKV env[] = {
      {"TESTCASE", "http3"},
      {"REQUESTS", "https://server/file"},
      {nullptr, nullptr}
    };
    testEnv_ = env;
    Env out;
    ZuCHECK(loadEnv(Client, out, testGetenv_, testGetpath_) == OK,
      "default client env loads");
    ZuCHECK(out.testCase == HTTP3, "env testcase");
    ZuCHECK(eq_(out.requests, "https://server/file"), "env requests");
    ZuCHECK(eq_(out.www, "/www"), "default www");
    ZuCHECK(eq_(out.downloads, "/downloads"), "default downloads");
    ZuCHECK(eq_(out.ca, "/certs/ca.pem"), "default ca");
    ZuCHECK(eq_(out.cert, "/certs/cert.pem"), "default cert");
    ZuCHECK(eq_(out.key, "/certs/priv.key"), "default key");
    ZuCHECK(out.port == DefaultPort, "default port");
    ZuCHECK(!*out.heartBeat, "default testcase heartbeat disabled");
  }
  {
    const TestEnvKV env[] = {
      {"TESTCASE", "transfer"},
      {"REQUESTS", "https://server/a"},
      {"SSLKEYLOGFILE", "/tmp/keylog"},
      {"ZHTTP_QIR_WWW", "/tmp/www"},
      {"ZHTTP_QIR_DOWNLOADS", "/tmp/dl"},
      {"ZHTTP_QIR_CA", "/tmp/ca.pem"},
      {"ZHTTP_QIR_CERT", "/tmp/cert.pem"},
      {"ZHTTP_QIR_KEY", "/tmp/key.pem"},
      {"QLOGDIR", "/tmp/qlog"},
      {"ZHTTP_QIR_PORT", "9443"},
      {nullptr, nullptr}
    };
    testEnv_ = env;
    Env out;
    ZuCHECK(loadEnv(Client, out, testGetenv_, testGetpath_) == OK,
      "override client env loads");
    ZuCHECK(out.testCase == Transfer, "override testcase");
    ZuCHECK(eq_(out.keyLog, "/tmp/keylog"), "keylog override");
    ZuCHECK(eq_(out.www, "/tmp/www"), "www override");
    ZuCHECK(eq_(out.downloads, "/tmp/dl"), "downloads override");
    ZuCHECK(eq_(out.ca, "/tmp/ca.pem"), "ca override");
    ZuCHECK(eq_(out.cert, "/tmp/cert.pem"), "cert override");
    ZuCHECK(eq_(out.key, "/tmp/key.pem"), "key override");
    ZuCHECK(eq_(out.qlogPath, "/tmp/qlog/client.sqlog"), "qlog path");
    ZuCHECK(out.port == 9443, "port override");
    ZuCHECK(!*out.heartBeat, "transfer heartbeat disabled");
  }
  {
    const TestEnvKV env[] = {
      {"TESTCASE", "rebind-port"},
      {"REQUESTS", "https://server/a"},
      {nullptr, nullptr}
    };
    testEnv_ = env;
    Env out;
    ZuCHECK(loadEnv(Client, out, testGetenv_, testGetpath_) == OK,
      "rebind client env loads");
    ZuCHECK(out.heartBeat == qirHeartBeat(RebindPort),
      "rebind heartbeat enabled");
  }
  {
    const TestEnvKV env[] = {{nullptr, nullptr}};
    testEnv_ = env;
    Env out;
    ZuCHECK(loadEnv(Server, out, testGetenv_, testGetpath_) == Usage,
      "missing testcase is usage");
  }
  {
    const TestEnvKV env[] = {
      {"TESTCASE", "retry"},
      {nullptr, nullptr}
    };
    testEnv_ = env;
    Env out;
    ZuCHECK(loadEnv(Server, out, testGetenv_, testGetpath_) == UnsupportedStatus,
      "unsupported testcase exits 127 before server startup");
  }
  {
    const TestEnvKV env[] = {
      {"TESTCASE", "http3"},
      {"ZHTTP_QIR_PORT", "0"},
      {nullptr, nullptr}
    };
    testEnv_ = env;
    Env out;
    ZuCHECK(loadEnv(Server, out, testGetenv_, testGetpath_) == Usage,
      "invalid port rejected");
  }
  {
    const TestEnvKV env[] = {
      {"TESTCASE", "http3"},
      {nullptr, nullptr}
    };
    testEnv_ = env;
    Env out;
    ZuCHECK(loadEnv(Client, out, testGetenv_, testGetpath_) == Usage,
      "client requires requests");
    ZuCHECK(loadEnv(Server, out, testGetenv_, testGetpath_) == OK,
      "server permits empty requests");
  }
}

static void testRequests()
{
  ZuTestScope(testRequests);
  using namespace Zhttp::QIR;

  Requests requests;
  ZuCHECK(parseRequests("https://server/a", "/downloads", requests) == OK &&
      requests.length() == 1 &&
      eq_(requests[0].path, "/a") &&
      eq_(requests[0].output, "/downloads/a"),
    "single request parses");
  ZuCHECK(parseRequests(" https://server/a  https://server:443/b/c\n"
      "\thttps://server/d?x=1#f ", "/downloads", requests) == OK &&
      requests.length() == 3 &&
      eq_(requests[0].output, "/downloads/a") &&
      eq_(requests[1].output, "/downloads/b/c") &&
      eq_(requests[2].output, "/downloads/d"),
    "multiple requests parse in order");
  ZuCHECK(parseRequests("", "/downloads", requests) == Usage,
    "empty requests rejected");
  ZuCHECK(parseRequests("http://server/a", "/downloads", requests) == Usage,
    "bad scheme rejected");
}

static void testURLMapping()
{
  ZuTestScope(testURLMapping);
  using namespace Zhttp::QIR;

  Request request;
  ZuCHECK(mapURL("https://server:443/a/b/file.bin", "/downloads", request) ==
      PathOK &&
      eq_(request.path, "/a/b/file.bin") &&
      eq_(request.output, "/downloads/a/b/file.bin"),
    "nested path maps");
  ZuCHECK(mapURL("https://server/a.txt", "/downloads", request) == PathOK &&
      eq_(request.output, "/downloads/a.txt"),
    "implicit port maps");
  ZuCHECK(mapURL("https://server/a.txt?x=1", "/downloads", request) ==
      PathOK &&
      eq_(request.output, "/downloads/a.txt"),
    "query stripped");
  ZuCHECK(mapURL("https://server/a.txt#frag", "/downloads", request) ==
      PathOK &&
      eq_(request.output, "/downloads/a.txt"),
    "fragment stripped");
  ZuCHECK(mapURL("https://server/a%20b", "/downloads", request) == PathOK &&
      eq_(request.output, "/downloads/a%20b"),
    "ordinary percent-encoded path preserved");
  ZuCHECK(mapURL("http://server/a", "/downloads", request) == PathBadScheme,
    "non-https rejected");
  ZuCHECK(mapURL("https://server", "/downloads", request) == PathEmpty,
    "missing path rejected");
  ZuCHECK(mapURL("https://server/", "/downloads", request) == PathEmpty,
    "root path rejected");
  ZuCHECK(mapURL("https://server/a/../b", "/downloads", request) ==
      PathEscape,
    "dot-dot path rejected");
  ZuCHECK(mapURL("https://server/a/", "/downloads", request) == PathDir,
    "directory output rejected");
}

static void testParentDirs()
{
  ZuTestScope(testParentDirs);
  using namespace Zhttp::QIR;

  TempDir temp;
  ZuCHECK(temp.init("ZhttpQIR"), "QIR temp dir created");
  Request request;
  Zi::Path downloads;
  downloads << static_cast<const char *>(temp.path) << "/downloads";
  ZuCHECK(mapURL("https://server/a/b/file.bin", downloads, request) ==
      PathOK,
    "nested output maps for parent dirs");
  ZuCHECK(ensureParentDirs(request.output), "nested parent dirs created");
  ZtString<> parent;
  parent << downloads << "/a/b";
  ZuCHECK(ZiStat{parent}.isdir(), "nested parent dir exists");

  Request bad;
  ZuCHECK(mapURL("https://server/a/../bad", downloads, bad) == PathEscape,
    "escaped output refused before parent creation");
}

static void testHQRequest()
{
  ZuTestScope(testHQRequest);
  using namespace Zhttp::QIR;

  HqRequest request;
  ZuCHECK(parseHQRequest("GET /file", request) == PathOK &&
      eq_(request.path, "/file"),
    "hq request parses");
  ZuCHECK(parseHQRequest("GET /file\r\n", request) == PathOK &&
      eq_(request.path, "/file"),
    "hq crlf request parses");
  ZuCHECK(parseHQRequest("GET ", request) == PathEmpty,
    "hq missing path rejected");
  ZuCHECK(parseHQRequest("POST /file", request) == PathEscape,
    "hq non-get rejected");
  ZuCHECK(parseHQRequest("GET /../file", request) == PathEscape,
    "hq escaped path rejected");
}

static void testHQFileMapping()
{
  ZuTestScope(testHQFileMapping);
  using namespace Zhttp::QIR;

  Zi::Path file;
  ZuCHECK(mapHQPath("/a/b/file.bin", "/www", file) == PathOK &&
      eq_(file, "/www/a/b/file.bin"),
    "hq path maps under www");
  ZuCHECK(mapHQPath("/a/b/file.bin", "/www/", file) == PathOK &&
      eq_(file, "/www/a/b/file.bin"),
    "hq path maps under slash-terminated www");
  ZuCHECK(mapHQPath("", "/www", file) == PathEmpty,
    "hq empty path rejected");
  ZuCHECK(mapHQPath("/a/../file", "/www", file) == PathEscape,
    "hq escaped file path rejected");
  ZuCHECK(mapHQPath("/a/", "/www", file) == PathDir,
    "hq directory file path rejected");

  ZtString<> line;
  ZuCHECK(buildHQRequestLine("/a/b/file.bin", line) &&
      eq_(line, "GET /a/b/file.bin\r\n"),
    "hq request line builds");
  ZuCHECK(!buildHQRequestLine("/a/../file", line),
    "escaped hq request line rejected");
  ZuCHECK(!buildHQRequestLine("/a/", line),
    "directory hq request line rejected");
}

static void testTimeouts()
{
  ZuTestScope(testTimeouts);
  using namespace Zhttp::QIR;

  auto t = timeouts();
  ZuCHECK(t.connect == ZuTime{5}, "connect timeout");
  ZuCHECK(t.total == ZuTime{60}, "total timeout");
  ZuCHECK(t.h3Quiet == ZuTime{15}, "h3 quiet timeout");
  ZuCHECK(t.h3Stall == ZuTime{30}, "h3 stall timeout");
}

static void testMigrationConfig()
{
  ZuTestScope(testMigrationConfig);
  using namespace Zhttp::QIR;

  ZuCHECK(qirMigrationMode() == Zquic::MigrationMode::Active,
    "QIR migration mode is active");
  ZuCHECK(qirMigrationCIDReserve() == QIRMigrationCIDReserve,
    "QIR migration CID reserve");
  ZuCHECK(qirHeartBeat(RebindPort) == ZuTime{1},
    "QIR rebind-port heartbeat interval");
  ZuCHECK(qirHeartBeat(RebindAddr) == ZuTime{1},
    "QIR rebind-addr heartbeat interval");
  ZuCHECK(!*qirHeartBeat(Transfer), "QIR transfer heartbeat disabled");
  ZuCHECK(ZuCSpan{qirMigrationModeArg()} == "active",
    "QIR migration mode argv");
  ZuCHECK(ZuCSpan{qirMigrationCIDReserveArg()} == "4",
    "QIR migration CID reserve argv");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZiTestResidue::init("zhttpqirtest");
  ZuTestMain();
  ZuTestCall(testRoleParsing);
  ZuTestCall(testCaseParsing);
  ZuTestCall(testEnvLoading);
  ZuTestCall(testRequests);
  ZuTestCall(testURLMapping);
  ZuTestCall(testParentDirs);
  ZuTestCall(testHQRequest);
  ZuTestCall(testHQFileMapping);
  ZuTestCall(testTimeouts);
  ZuTestCall(testMigrationConfig);
}
