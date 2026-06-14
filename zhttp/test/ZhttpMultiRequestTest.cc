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

bool writeScript(ZuCSpan path, const char *tempPath_, unsigned port)
{
  ZuCSpan tempPath{tempPath_};
  ZtString<> rootPath;
  rootPath << tempPath << "/root";
  if (ZiFile::mkdir(rootPath) != Zi::OK) return false;
  ZtString<> staticPath;
  staticPath << rootPath << "/zhttp-multi";
  {
    ZiFile f;
    ZuCSpan body{"zhttp-multi-ok\n"};
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
    "server=../example/zhttpd\n"
    "client=../example/zhttp\n"
    "[ -x \"$server\" ] || server=./zhttp/example/zhttpd\n"
    "[ -x \"$client\" ] || client=./zhttp/example/zhttp\n"
    "\"$client\" -j 2 http://127.0.0.1:" << port <<
      "/zhttp-multi >/dev/null 2>" << tempPath << "/bad-j.err && exit 1\n"
    "\"$client\" -n 1 -j 1 http://127.0.0.1:" << port <<
      "/zhttp-multi >/dev/null 2>" << tempPath << "/bad-n1j.err && exit 1\n"
    "\"$client\" -n 2 -j 3 http://127.0.0.1:" << port <<
      "/zhttp-multi >/dev/null 2>" << tempPath << "/bad-gt.err && exit 1\n"
    "\"$client\" -n 0 http://127.0.0.1:" << port <<
      "/zhttp-multi >/dev/null 2>" << tempPath << "/bad-n0.err && exit 1\n"
    "\"$server\" " << rootPath <<
      " --http --addr 127.0.0.1 --port " << port <<
      " >" << tempPath << "/server.out 2>" << tempPath << "/server.err &\n"
    "pid=$!\n"
    "sleep 1\n"
    "ok=0\n"
    "for i in $(seq 1 80); do\n"
    "  if \"$client\" -n 3 -o " << tempPath <<
      "/body http://127.0.0.1:" << port <<
      "/zhttp-multi >" << tempPath << "/client.out 2>" <<
      tempPath << "/client.err; then\n"
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
    "grep -qx 'zhttp-multi-ok' " << tempPath << "/body.0\n"
    "grep -qx 'zhttp-multi-ok' " << tempPath << "/body.1\n"
    "grep -qx 'zhttp-multi-ok' " << tempPath << "/body.2\n"
    "\"$client\" -n 4 -j 2 -o " << tempPath <<
      "/bodyj http://127.0.0.1:" << port <<
      "/zhttp-multi >" << tempPath << "/client-j.out 2>" <<
      tempPath << "/client-j.err\n"
    "grep -qx 'zhttp-multi-ok' " << tempPath << "/bodyj.0\n"
    "grep -qx 'zhttp-multi-ok' " << tempPath << "/bodyj.1\n"
    "grep -qx 'zhttp-multi-ok' " << tempPath << "/bodyj.2\n"
    "grep -qx 'zhttp-multi-ok' " << tempPath << "/bodyj.3\n"
    "cd " << tempPath << "\n"
    "\"$OLDPWD/$client\" -n 2 http://127.0.0.1:" << port <<
      "/zhttp-multi >client-default.out 2>client-default.err\n"
    "grep -qx 'zhttp-multi-ok' index.html.0\n"
    "grep -qx 'zhttp-multi-ok' index.html.1\n";
  ZiFile f;
  if (f.open(path, ZiFile::Write, 0777) != Zi::OK) return false;
  if (f.write(script.data(), script.length()) != Zi::OK) return false;
  f.close();
  return true;
}

void testMultiRequestCLIAndOutput()
{
  ZuTestScope(testMultiRequestCLIAndOutput);

  TempDir temp;
  ZuCHECK(temp.init("ZhttpMultiRequest"),
    "Zhttp multi-request temporary directory failed");
  unsigned port = loopbackPort();
  if (!port) {
    ZuCHECK(true, "Zhttp multi-request integration skipped");
    return;
  }
  auto script = temp.pathOf("multi.sh");
  ZuCHECK(writeScript(script, static_cast<const char *>(temp.path), port),
    "Zhttp multi-request script generation failed");
  ZtString<> cmd;
  cmd << "sh " << script;
  if (!systemOK(::system(cmd.data()))) {
    printFile("server stderr", temp.pathOf("server.err"));
    printFile("client stderr", temp.pathOf("client.err"));
    printFile("client -j stderr", temp.pathOf("client-j.err"));
    printFile("client default stderr", temp.pathOf("client-default.err"));
    ZuCHECK(false, "Zhttp multi-request integration failed");
  } else {
    ZuCHECK(true, "Zhttp multi-request integration succeeded");
  }
}

} // namespace

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testMultiRequestCLIAndOutput);
}
