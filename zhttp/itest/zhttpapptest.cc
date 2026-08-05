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
using Zhttp::Test::loopbackPort;
using Zhttp::Test::printFile;
using Zhttp::Test::systemOK;
using Zhttp::Test::writeLocalhostCert;

bool writeHTTPAppScript(
  ZuCSpan path, const char *tempPath_, unsigned port, ZuCSpan transport,
  bool ipv6, ZuCSpan certPath = {}, ZuCSpan keyPath = {})
{
  ZuCSpan tempPath{tempPath_};
  ZuCSpan addr = ipv6 ? ZuCSpan{"::1"} : ZuCSpan{"127.0.0.1"};
  ZuCSpan urlHost = ipv6 ? ZuCSpan{"[::1]"} : ZuCSpan{"127.0.0.1"};
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
  auto writeFile = [&rootPath](ZuCSpan name) {
    ZtString<> filePath;
    filePath << rootPath << '/' << name;
    ZiFile f;
    if (f.open(filePath, ZiFile::Write, 0666) != Zi::OK) return false;
    if (f.write(name.data(), name.length()) != Zi::OK) return false;
    f.close();
    return true;
  };
  if (!writeFile("pipeline-a") || !writeFile("pipeline-b")) return false;
  ZtString<> largePath;
  largePath << rootPath << "/large";
  {
    ZiFile f;
    ZuCSpan block{"0123456789abcdef\n"};
    if (f.open(largePath, ZiFile::Write, 0666) != Zi::OK) return false;
    for (unsigned i = 0; i < 4097; ++i)
      if (f.write(block.data(), block.length()) != Zi::OK) return false;
    f.close();
  }

  ZtString<> script;
  script <<
    "#!/bin/sh\n"
    "set -eu\n"
    "# Parent/unit tests own leak checks; keep instrumented app children fast.\n"
    "ASAN_OPTIONS=\"${ASAN_OPTIONS:+$ASAN_OPTIONS:}detect_leaks=0\"\n"
    "export ASAN_OPTIONS\n"
    "pid=\n"
    "cleanup() {\n"
    "  if [ -n \"$pid\" ]; then\n"
    "    kill \"$pid\" 2>/dev/null || true\n"
    "    wait \"$pid\" 2>/dev/null || true\n"
    "  fi\n"
    "}\n"
    "trap cleanup EXIT INT TERM\n"
    "server=../util/zhttpd\n"
    "client=../util/zhttp\n";
  if (transport == "http")
    script << "\"$server\" " << rootPath <<
      " --http --addr " << addr << " --port " << port;
  else if (transport == "https")
    script << "\"$server\" " << rootPath <<
      " --https --addr " << addr << " --port " << port <<
      " --cert " << certPath << " --key " << keyPath;
  else if (transport == "prefer")
    script << "\"$server\" " << rootPath <<
      " --https --http3 --addr " << addr << " --port " << port <<
      " --cert " << certPath << " --key " << keyPath;
  else
    script << "\"$server\" " << rootPath <<
      " --http3 --addr " << addr << " --port " << port <<
      " --cert " << certPath << " --key " << keyPath;
  script <<
      " >" << tempPath << "/server.out 2>" << tempPath << "/server.err &\n"
    "pid=$!\n"
    "sleep 1\n";
  if (transport == "http" && !ipv6)
    script <<
      "python3 -c 'import socket,sys; s=socket.create_connection((\"127.0.0.1\"," <<
      port << ")); s.sendall(b\"WHAT /bad HTTP/1.1\\r\\nhost: localhost\\r\\n\\r\\n\"); "
      "d=s.recv(128); s.close(); sys.exit(0 if d.startswith(b\"HTTP/1.1 501\") else 1)'\n"
      "kill -0 \"$pid\"\n"
      "test \"$(grep -c 'zhttpd.access' " << tempPath << "/server.err)\" -eq 1\n"
      "python3 -c 'import socket,sys; s=socket.create_connection((\"127.0.0.1\"," <<
      port << ")); s.sendall(b\"GET /pipeline-a HTTP/1.1\\r\\nhost: localhost\\r\\n\\r\\nGET /pipeline-b HTTP/1.1\\r\\nhost: localhost\\r\\nconnection: close\\r\\n\\r\\n\"); "
      "d=b\"\".join(iter(lambda:s.recv(4096),b\"\")); s.close(); "
      "a=d.find(b\"pipeline-a\"); b=d.find(b\"pipeline-b\"); "
      "sys.exit(0 if a>=0 and b>a else 1)'\n";
  script <<
    "ok=0\n"
    "for i in $(seq 1 80); do\n"
    "  if \"$client\" ";
  if (transport == "http")
    script << "-o " << tempPath << "/body " <<
      "http://" << urlHost << ':' << port << "/zhttp-interop ";
  else if (transport == "https" || transport == "prefer")
    script << "-j 5 -n 10 " <<
      (transport == "https" ? "-3 disable " : "") <<
      "-c " << certPath << " -o " << tempPath << "/body " <<
      "https://" << urlHost << ':' << port << "/zhttp-interop ";
  else
    script << "-3 force -j 5 -n 10 -c " << certPath << " -o " <<
      tempPath << "/body https://" << urlHost << ':' << port <<
      "/zhttp-interop ";
  script <<
      ">" << tempPath << "/client.out 2>" << tempPath << "/client.err; then\n"
    "    ok=1\n"
    "    break\n"
    "  else\n"
    "    rc=$?\n"
    "  fi\n"
    "  if [ \"$rc\" -ge 128 ]; then\n"
    "    cat " << tempPath << "/client.err\n"
    "    exit \"$rc\"\n"
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
    "grep -qx 'zhttp-ok' " << tempPath << "/body";
  if (transport != "http") script << ".9";
  script << '\n' <<
    "\"$client\" ";
  if (transport == "http")
    script << "-o " << tempPath << "/large-body http://" << urlHost << ':' <<
      port << "/large";
  else if (transport == "https" || transport == "prefer")
    script << "-j 5 -n 10 " <<
      (transport == "https" ? "-3 disable " : "") <<
      "-c " << certPath << " -o " << tempPath << "/large-body https://" <<
      urlHost << ':' << port << "/large";
  else
    script << "-3 force -j 5 -n 10 -c " << certPath << " -o " << tempPath <<
      "/large-body https://" << urlHost << ':' << port << "/large";
  script << " >" << tempPath << "/large-client.out 2>" << tempPath <<
    "/large-client.err\n"
    "cmp " << largePath << ' ' << tempPath << "/large-body";
  if (transport != "http") script << ".9";
  script << '\n' <<
    "for i in $(seq 1 100); do\n"
    "  grep -q '\"/large\" 200' " << tempPath <<
      "/server.err && break\n"
    "  sleep 0.01\n"
    "done\n"
    "grep -q '\"/large\" 200' " << tempPath << "/server.err\n";
  ZiFile f;
  if (f.open(path, ZiFile::Write, 0777) != Zi::OK) return false;
  if (f.write(script.data(), script.length()) != Zi::OK) return false;
  f.close();
  return true;
}

void testAppTransport(ZuCSpan transport, bool ipv6 = false)
{
  ZuTestScope(testAppTransport);

  TempDir temp;
  ZuCHECK(temp.init("ZhttpApp"),
    "Zhttp app temporary directory failed");
  ZtString<> certPath, keyPath;
  if (transport != "http")
    ZuCHECK(writeLocalhostCert(temp, certPath, keyPath),
      "Zhttp app certificate generation failed");
  unsigned port = loopbackPort();
  ZuCHECK(port, "Zhttp app port allocation failed");
  if (!port) return;
  auto script = temp.pathOf("client-server.sh");
  ZuCHECK(writeHTTPAppScript(script, static_cast<const char *>(temp.path), port,
      transport, ipv6, certPath, keyPath),
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
void testPreferApp() { testAppTransport("prefer"); }
void testHTTPAppIPv6() { testAppTransport("http", true); }
void testHTTPSAppIPv6() { testAppTransport("https", true); }
void testH3AppIPv6() { testAppTransport("h3", true); }

} // namespace

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testHTTPApp);
  ZuTestCall(testHTTPSApp);
  ZuTestCall(testH3App);
  ZuTestCall(testPreferApp);
  ZuTestCall(testHTTPAppIPv6);
  ZuTestCall(testHTTPSAppIPv6);
  ZuTestCall(testH3AppIPv6);
}
