//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>

#include "ZhttpTestUtil.hh"

using namespace ZuTestUtil;

namespace {

using Zhttp::Test::TempDir;
using Zhttp::Test::cspan;
using Zhttp::Test::loopbackPort;
using Zhttp::Test::printFile;
using Zhttp::Test::systemOK;

bool writeHTTPClientServerScript(
  ZuCSpan path, ZuCSpan tempPath, unsigned port)
{
  ZtString<> filePath;
  filePath << path;
  FILE *f = fopen(filePath.data(), "w");
  if (!f) return false;
  int n = fprintf(f,
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
    "client=../example/zhttpclient\n"
    "[ -x \"$server\" ] || server=./zhttp/example/zhttpserver\n"
    "[ -x \"$client\" ] || client=./zhttp/example/zhttpclient\n"
    "\"$server\" --http --addr 127.0.0.1 --port %u "
      "--body=zhttp-ok >%.*s/server.out 2>%.*s/server.err &\n"
    "pid=$!\n"
    "ok=0\n"
    "for i in $(seq 1 80); do\n"
    "  if \"$client\" -o %.*s/body "
      "http://127.0.0.1:%u/zhttp-interop "
      ">%.*s/client.out 2>%.*s/client.err; then\n"
    "    ok=1\n"
    "    break\n"
    "  fi\n"
    "  if ! kill -0 \"$pid\" 2>/dev/null; then\n"
    "    cat %.*s/server.err\n"
    "    exit 1\n"
    "  fi\n"
    "  sleep 0.1\n"
    "done\n"
    "if [ \"$ok\" -ne 1 ]; then\n"
    "  cat %.*s/server.err\n"
    "  cat %.*s/client.err\n"
    "  exit 1\n"
    "fi\n"
    "grep -qx 'zhttp-ok' %.*s/body\n",
    port,
    int(tempPath.length()), tempPath.data(),
    int(tempPath.length()), tempPath.data(),
    int(tempPath.length()), tempPath.data(),
    port,
    int(tempPath.length()), tempPath.data(),
    int(tempPath.length()), tempPath.data(),
    int(tempPath.length()), tempPath.data(),
    int(tempPath.length()), tempPath.data(),
    int(tempPath.length()), tempPath.data(),
    int(tempPath.length()), tempPath.data());
  return n > 0 && !fclose(f);
}

void testHTTPClientServer()
{
  ZuTestScope(testHTTPClientServer);

  TempDir temp;
  ZuCHECK(temp.init("ZhttpClientServer"),
    "Zhttp client/server temporary directory failed");
  unsigned port = loopbackPort();
  ZuCHECK(port, "Zhttp client/server port allocation failed");
  if (!port) return;
  auto script = temp.pathOf("client-server.sh");
  ZuCHECK(writeHTTPClientServerScript(script, temp.path, port),
    "Zhttp client/server script generation failed");
  ZtString<> cmd;
  cmd << "sh " << script;
  if (!systemOK(::system(cmd.data()))) {
    printFile("server stderr", temp.pathOf("server.err"));
    printFile("client stderr", temp.pathOf("client.err"));
    ZuCHECK(false, "Zhttp client/server HTTP integration failed");
  }
}

} // namespace

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testHTTPClientServer);
}
