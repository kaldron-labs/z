//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZiFile.hh>

#include "ZhttpTestUtil.hh"

using namespace ZuTestUtil;

namespace {

using Zhttp::Test::TempDir;
using Zhttp::Test::cspan;
using Zhttp::Test::loopbackPort;
using Zhttp::Test::printFile;
using Zhttp::Test::systemOK;
using Zhttp::Test::writeSelfSignedLocalhostCert;

bool writeHTTPAppScript(
  ZuCSpan path, const char *tempPath_, unsigned port, ZuCSpan transport,
  ZuCSpan certPath = {}, ZuCSpan keyPath = {})
{
  ZuCSpan tempPath{tempPath_};
  ZtString<> rootPath;
  rootPath << tempPath << "/root";
  if (ZiFile::mkdir(rootPath) != Zi::OK) return false;
  ZtString<> staticPath;
  staticPath << rootPath << "/zhttp-interop";
  {
    ZiFile f;
    ZuCSpan body{"zhttp-ok\n"};
    if (f.open(staticPath, ZiFile::Write, 0666) != Zi::OK) return false;
    if (f.write(body.data(), body.length()) != Zi::OK) return false;
    f.close();
  }

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
    "server=../example/zhttpserver\n"
    "client=../example/zhttp\n"
    "[ -x \"$server\" ] || server=./zhttp/example/zhttpserver\n"
    "[ -x \"$client\" ] || client=./zhttp/example/zhttp\n";
  if (transport == "http")
    script << "\"$server\" " << rootPath <<
      " --http --addr 127.0.0.1 --port " << port;
  else if (transport == "https")
    script << "\"$server\" " << rootPath <<
      " --https --addr 127.0.0.1 --port " << port <<
      " --cert " << certPath << " --key " << keyPath;
  else
    script << "\"$server\" " << rootPath <<
      " --http3 --addr 127.0.0.1 --port " << port <<
      " --cert " << certPath << " --key " << keyPath;
  script <<
      " >" << tempPath << "/server.out 2>" << tempPath << "/server.err &\n"
    "pid=$!\n"
    "sleep 1\n"
    "ok=0\n"
    "for i in $(seq 1 80); do\n"
    "  if \"$client\" ";
  if (transport == "http")
    script << "-o " << tempPath << "/body " <<
      "http://127.0.0.1:" << port << "/zhttp-interop ";
  else if (transport == "https")
    script << "-c " << certPath << " -o " << tempPath << "/body " <<
      "https://localhost:" << port << "/zhttp-interop ";
  else
    script << "--http3-only -c " << certPath << " -o " <<
      tempPath << "/body https://localhost:" << port << "/zhttp-interop ";
  script <<
      ">" << tempPath << "/client.out 2>" << tempPath << "/client.err; then\n"
    "    ok=1\n"
    "    break\n"
    "  fi\n"
    "  if ! kill -0 \"$pid\" 2>/dev/null; then\n"
    "    cat " << tempPath << "/server.err\n"
    "    exit 1\n"
    "  fi\n"
    "  sleep 0.1\n"
    "done\n"
    "if [ \"$ok\" -ne 1 ]; then\n"
    "  cat " << tempPath << "/server.err\n"
    "  cat " << tempPath << "/client.err\n"
    "  exit 1\n"
    "fi\n"
    "grep -qx 'zhttp-ok' " << tempPath << "/body\n";
  ZiFile f;
  if (f.open(path, ZiFile::Write, 0777) != Zi::OK) return false;
  if (f.write(script.data(), script.length()) != Zi::OK) return false;
  f.close();
  return true;
}

void testAppTransport(ZuCSpan transport)
{
  ZuTestScope(testAppTransport);

  TempDir temp;
  ZuCHECK(temp.init("ZhttpApp"),
    "Zhttp app temporary directory failed");
  ZtString<> certPath, keyPath;
  if (transport != "http")
    ZuCHECK(writeSelfSignedLocalhostCert(temp, certPath, keyPath),
      "Zhttp app certificate generation failed");
  unsigned port = loopbackPort();
  ZuCHECK(port, "Zhttp app port allocation failed");
  if (!port) return;
  auto script = temp.pathOf("client-server.sh");
  ZuCHECK(writeHTTPAppScript(script, static_cast<const char *>(temp.path), port,
      transport, certPath, keyPath),
    "Zhttp app script generation failed");
  ZtString<> cmd;
  cmd << "sh " << script;
  if (!systemOK(::system(cmd.data()))) {
    printFile("server stderr", temp.pathOf("server.err"));
    printFile("client stderr", temp.pathOf("client.err"));
    ZuCHECK(false, "Zhttp app HTTP integration failed");
  } else {
    ZuCHECK(true, "Zhttp app integration succeeded");
  }
}

void testHTTPApp() { testAppTransport("http"); }
void testHTTPSApp() { testAppTransport("https"); }
void testH3App() { testAppTransport("h3"); }

} // namespace

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testHTTPApp);
  ZuTestCall(testHTTPSApp);
  ZuTestCall(testH3App);
}
