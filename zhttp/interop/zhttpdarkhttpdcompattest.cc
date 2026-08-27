//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZiFile.hh>

#ifndef _WIN32
#include <sys/wait.h>
#endif

#include "ZhttpTestUtil.hh"
#include "ZhttpInteropPorts.hh"

using namespace ZuTestUtil;

namespace {

using Zhttp::Test::TempDir;
using Zhttp::Test::loopbackPort;
using Zhttp::Test::printFile;
using Zhttp::Test::systemOK;

bool writeFile(ZuCSpan path, ZuCSpan body)
{
  ZiFile f;
  if (f.open(path, ZiFile::Write, 0666) != Zi::OK) return false;
  return f.write(body.data(), body.length()) == Zi::OK;
}

bool writeCompatScript(
  ZuCSpan path, const char *tempPath_, unsigned zport, unsigned dport)
{
  ZuCSpan tempPath{tempPath_};
  ZtString<> rootPath;
  rootPath << tempPath << "/root";
  if (ZiFile::mkdir(rootPath) != Zi::OK) return false;
  ZtString<> indexPath;
  indexPath << rootPath << "/index.html";
  ZtString<> filePath;
  filePath << rootPath << "/hello.txt";
  if (!writeFile(indexPath, "index\n") || !writeFile(filePath, "hello\n"))
    return false;

  ZtString<> script;
  script <<
    "#!/bin/sh\n"
    "set -eu\n"
    "zp=\n"
    "dp=\n"
    "cleanup() {\n"
    "  [ -n \"$zp\" ] && kill \"$zp\" 2>/dev/null || true\n"
    "  [ -n \"$dp\" ] && kill \"$dp\" 2>/dev/null || true\n"
    "  [ -n \"$zp\" ] && wait \"$zp\" 2>/dev/null || true\n"
    "  [ -n \"$dp\" ] && wait \"$dp\" 2>/dev/null || true\n"
    "}\n"
    "trap cleanup EXIT INT TERM\n"
    "command -v darkhttpd >/dev/null || exit 77\n"
    "command -v curl >/dev/null || exit 77\n"
    "server=../util/zhttpd\n"
    "\"$server\" " << rootPath <<
      " --http --addr 127.0.0.1 --port " << zport <<
      " >" << tempPath << "/zhttp.out 2>" << tempPath << "/zhttp.err &\n"
    "zp=$!\n"
    "darkhttpd " << rootPath <<
      " --addr 127.0.0.1 --port " << dport <<
      " >" << tempPath << "/dark.out 2>" << tempPath << "/dark.err &\n"
    "dp=$!\n"
    "for p in " << zport << " " << dport << "; do\n"
    "  ok=0\n"
    "  for i in $(seq 1 80); do\n"
    "    if curl -fsS --connect-timeout 1 --max-time 2 "
      "http://127.0.0.1:$p/hello.txt >/dev/null 2>&1; then\n"
    "      ok=1\n"
    "      break\n"
    "    fi\n"
    "    sleep 0.1\n"
    "  done\n"
    "  [ \"$ok\" -eq 1 ] || exit 1\n"
    "done\n"
    "for path in /hello.txt /; do\n"
    "  curl -fsS -w '%{http_code}\\n' -o " << tempPath <<
      "/z.body http://127.0.0.1:" << zport << "$path >" <<
      tempPath << "/z.status\n"
    "  curl -fsS -w '%{http_code}\\n' -o " << tempPath <<
      "/d.body http://127.0.0.1:" << dport << "$path >" <<
      tempPath << "/d.status\n"
    "  cmp " << tempPath << "/z.status " << tempPath << "/d.status\n"
    "  cmp " << tempPath << "/z.body " << tempPath << "/d.body\n"
    "done\n";
  ZiFile f;
  if (f.open(path, ZiFile::Write, 0777) != Zi::OK) return false;
  if (f.write(script.data(), script.length()) != Zi::OK) return false;
  f.close();
  return true;
}

void testDarkhttpdCompat()
{
  ZuTestScope(testDarkhttpdCompat);

  TempDir temp;
  ZuCHECK(temp.init("ZhttpDarkhttpd"),
    "Zhttp darkhttpd temporary directory failed");
  unsigned zport = loopbackPort(ZhttpInteropPort::DarkZ);
  unsigned dport = loopbackPort(ZhttpInteropPort::DarkD);
  ZuCHECK(zport && dport && zport != dport,
    "Zhttp darkhttpd port allocation failed");
  if (!zport || !dport || zport == dport) return;
  auto script = temp.pathOf("compat.sh");
  ZuCHECK(writeCompatScript(
      script, static_cast<const char *>(temp.path), zport, dport),
    "Zhttp darkhttpd script generation failed");
  ZtString<> cmd;
  cmd << "sh " << script;
  int rc = ::system(cmd.data());
#ifndef _WIN32
  if (WIFEXITED(rc) && WEXITSTATUS(rc) == 77) {
    ZuCHECK(true, "darkhttpd compatibility skipped");
    return;
  }
#endif
  if (!systemOK(rc)) {
    printFile("zhttp stderr", temp.pathOf("zhttp.err"));
    printFile("darkhttpd stderr", temp.pathOf("dark.err"));
    printFile("zhttp body", temp.pathOf("z.body"));
    printFile("darkhttpd body", temp.pathOf("d.body"));
    ZuCHECK(false, "darkhttpd compatibility failed");
  } else {
    ZuCHECK(true, "darkhttpd compatibility matched");
  }
}

} // namespace

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testDarkhttpdCompat);
}
