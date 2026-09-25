//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef ZhttpQIR_HH
#define ZhttpQIR_HH

#ifndef ZhttpLib_HH
#include <zlib/ZhttpLib.hh>
#endif

#include <zlib/ZuTime.hh>
#include <zlib/ZtArray.hh>
#include <zlib/ZtString.hh>
#include <zlib/ZiPlatform.hh>
#include <zlib/Zquic.hh>

namespace Zhttp::QIR {

enum Role {
  Client,
  Server
};

enum Case {
  Handshake,
  Transfer,
  HTTP3,
  RebindPort,
  RebindAddr,
  ConnectionMigration,
  Unsupported
};

enum Status {
  OK = 0,
  Error = 1,
  Usage = 2,
  UnsupportedStatus = 127
};

enum {
  DefaultPort = 443,
  QIRMigrationCIDReserve = 4
};

enum PathStatus {
  PathOK,
  PathBadScheme,
  PathEmpty,
  PathEscape,
  PathDir
};

struct Timeouts {
  ZuTime connect;
  ZuTime total;
  ZuTime h3Quiet;
  ZuTime h3Stall;
};

struct Env {
  Case		testCase = Unsupported;
  ZuTime	heartBeat;
  ZtString<>	requests;
  Zi::Path	keyLog;
  Zi::Path	www;
  Zi::Path	downloads;
  Zi::Path	ca;
  Zi::Path	cert;
  Zi::Path	key;
  Zi::Path	qlogPath;
  unsigned	port = DefaultPort;
};

struct Request {
  ZtString<>	url;
  ZtString<>	host;
  ZtString<>	path;
  Zi::Path	output;
  unsigned	port = DefaultPort;
};

struct HqRequest {
  ZtString<>	path;
};

using Requests = ZtArray<Request>;

constexpr Timeouts timeouts()
{
  return {ZuTime{5}, ZuTime{60}, ZuTime{15}, ZuTime{30}};
}

Zquic::MigrationMode::T qirMigrationMode();
unsigned qirMigrationCIDReserve();
ZuTime qirHeartBeat(Case testCase);
const char *qirMigrationModeArg();
const char *qirMigrationCIDReserveArg();

ZuCSpan roleName(Role role);
bool parseRole(ZuCSpan s, Role &role);

ZuCSpan caseName(Case testCase);
Case parseCase(ZuCSpan s);
bool caseSupported(Case testCase);
bool caseH3(Case testCase);
bool caseHQ(Case testCase);

using GetPathFn = void (*)(const char *, Zi::Path &);
int loadEnv(Role role, Env &env, const char *(*getenvFn)(const char *),
    GetPathFn getpathFn);
int parseRequests(ZuCSpan requests, const Zi::Path &downloads, Requests &out);
PathStatus mapURL(ZuCSpan url, const Zi::Path &downloads, Request &request);
PathStatus parseHQRequest(ZuCSpan line, HqRequest &request);
PathStatus mapHQPath(ZuCSpan path, const Zi::Path &www, Zi::Path &file);
bool buildHQRequestLine(ZuCSpan path, ZtString<> &line);
bool ensureParentDirs(const Zi::Path &path);

int run(Role role);
void usage();

} // namespace Zhttp::QIR

#endif /* ZhttpQIR_HH */
