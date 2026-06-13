//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef ZhttpCaddyInterop_HH
#define ZhttpCaddyInterop_HH

#include "ZhttpTestUtil.hh"

namespace Zhttp::Test {

using Zquic::Test::CaddyProcess;
using Zquic::Test::haveCaddy;
using Zquic::Test::waitCaddyReady;
using Zquic::Test::writeCaddyfile;

inline bool writeCaddyConfig(
  ZuCSpan path, unsigned port, ZuCSpan protocols, ZuCSpan body)
{
  ZtString<> filePath;
  filePath << path;
  FILE *f = fopen(filePath.data(), "w");
  if (!f) return false;
  int n = fprintf(f,
    "{\n"
    "  \"admin\": { \"disabled\": true },\n"
    "  \"apps\": {\n"
    "    \"http\": {\n"
    "      \"servers\": {\n"
    "        \"interop\": {\n"
    "          \"listen\": [\"127.0.0.1:%u\"],\n"
    "          \"protocols\": [%.*s],\n"
    "          \"automatic_https\": { \"disable_redirects\": true },\n"
    "          \"routes\": [\n"
    "            {\n"
    "              \"match\": [ { \"host\": [\"localhost\"] } ],\n"
    "              \"handle\": [\n"
    "                {\n"
    "                  \"handler\": \"subroute\",\n"
    "                  \"routes\": [\n"
    "                    {\n"
    "                      \"match\": [ { \"path\": [\"/zhttp-interop\"] } ],\n"
    "                      \"handle\": [\n"
    "                        {\n"
    "                          \"handler\": \"static_response\",\n"
    "                          \"status_code\": 200,\n"
    "                          \"headers\": { \"Content-Type\": [\"text/plain\"] },\n"
    "                          \"body\": \"%.*s\"\n"
    "                        }\n"
    "                      ]\n"
    "                    }\n"
    "                  ]\n"
    "                }\n"
    "              ],\n"
    "              \"terminal\": true\n"
    "            }\n"
    "          ]\n"
    "        }\n"
    "      }\n"
    "    },\n"
    "    \"tls\": {\n"
    "      \"automation\": {\n"
    "        \"policies\": [\n"
    "          {\n"
    "            \"subjects\": [\"localhost\"],\n"
    "            \"issuers\": [ { \"module\": \"internal\" } ]\n"
    "          }\n"
    "        ]\n"
    "      }\n"
    "    }\n"
    "  }\n"
    "}\n",
    port, int(protocols.length()), protocols.data(),
    int(body.length()), body.data());
  return n > 0 && !fclose(f);
}

inline bool writeCurlScript(
  ZuCSpan path, ZuCSpan tempPath, ZuCSpan configPath, unsigned port,
  ZuCSpan curlProtocol, ZuCSpan expectedVersion, ZuCSpan expectedBody,
  bool expectH3Failure = false)
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
    "XDG_DATA_HOME=%.*s/data XDG_CONFIG_HOME=%.*s/config "
      "caddy validate --config %.*s >%.*s/validate.log 2>&1\n"
    "XDG_DATA_HOME=%.*s/data XDG_CONFIG_HOME=%.*s/config "
      "caddy run --config %.*s >%.*s/caddy.log 2>&1 &\n"
    "pid=$!\n"
    "ok=0\n"
    "for i in $(seq 1 80); do\n"
    "  if curl %.*s -k --fail -sS --connect-timeout 2 --max-time 5 "
      "--resolve localhost:%u:127.0.0.1 "
      "-o %.*s/body -w '%%{http_version}' "
      "https://localhost:%u/zhttp-interop >%.*s/version 2>%.*s/curl.err; then\n"
    "    ok=1\n"
    "    break\n"
    "  fi\n"
    "  if ! kill -0 \"$pid\" 2>/dev/null; then\n"
    "    cat %.*s/caddy.log\n"
    "    exit 1\n"
    "  fi\n"
    "  sleep 0.1\n"
    "done\n"
    "if [ %u -ne 0 ]; then\n"
    "  if curl --http3-only -k --fail -sS --connect-timeout 1 --max-time 3 "
      "--resolve localhost:%u:127.0.0.1 "
      "https://localhost:%u/zhttp-interop >/dev/null 2>%.*s/h3.err; then\n"
    "    echo 'HTTP/3 was accepted by an HTTP/1.1 fallback endpoint'\n"
    "    exit 1\n"
    "  fi\n"
    "fi\n"
    "if [ \"$ok\" -ne 1 ]; then\n"
    "  cat %.*s/caddy.log\n"
    "  cat %.*s/curl.err\n"
    "  exit 1\n"
    "fi\n"
    "grep -qx '%.*s' %.*s/body\n"
    "grep -qx '%.*s' %.*s/version\n",
    int(tempPath.length()), tempPath.data(),
    int(tempPath.length()), tempPath.data(),
    int(configPath.length()), configPath.data(),
    int(tempPath.length()), tempPath.data(),
    int(tempPath.length()), tempPath.data(),
    int(tempPath.length()), tempPath.data(),
    int(configPath.length()), configPath.data(),
    int(tempPath.length()), tempPath.data(),
    int(curlProtocol.length()), curlProtocol.data(),
    port,
    int(tempPath.length()), tempPath.data(),
    port,
    int(tempPath.length()), tempPath.data(),
    int(tempPath.length()), tempPath.data(),
    int(tempPath.length()), tempPath.data(),
    expectH3Failure ? 1U : 0U,
    port, port,
    int(tempPath.length()), tempPath.data(),
    int(tempPath.length()), tempPath.data(),
    int(tempPath.length()), tempPath.data(),
    int(expectedBody.length()), expectedBody.data(),
    int(tempPath.length()), tempPath.data(),
    int(expectedVersion.length()), expectedVersion.data(),
    int(tempPath.length()), tempPath.data());
  return n > 0 && !fclose(f);
}

inline bool runCaddyCurl(
  ZuCSpan protocols, ZuCSpan curlProtocol, ZuCSpan expectedVersion,
  ZuCSpan expectedBody, bool expectH3Failure = false)
{
  TempDir temp;
  if (!temp.init("ZhttpCaddyInterop")) return false;
  unsigned port = loopbackPort();
  if (!port) return false;

  auto config = temp.pathOf("caddy.json");
  auto script = temp.pathOf("curl.sh");
  if (!writeCaddyConfig(
	config, port, protocols, expectedBody) ||
      !writeCurlScript(script, temp.path, config, port, curlProtocol,
	expectedVersion, expectedBody, expectH3Failure))
    return false;

  ZtString<> cmd;
  cmd << "sh " << script;
  return systemOK(::system(cmd.data()));
}

} // namespace Zhttp::Test

#endif /* ZhttpCaddyInterop_HH */
