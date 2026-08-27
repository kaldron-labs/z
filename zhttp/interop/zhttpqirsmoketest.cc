//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdlib.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZiFile.hh>
#include <zlib/ZtString.hh>

#include "ZhttpTestUtil.hh"
#include "ZhttpInteropPorts.hh"

using namespace ZuTestUtil;
using Zhttp::Test::TempDir;
using Zhttp::Test::loopbackPort;
using Zhttp::Test::systemOK;
using Zhttp::Test::writeLocalhostCert;

static int exitCode_(int status)
{
  if (!WIFEXITED(status)) return -1;
  return WEXITSTATUS(status);
}

static bool runStatus_(ZuCSpan cmd, int expected)
{
  ZtString<> script;
  script << cmd << " >/dev/null 2>&1";
  int code = exitCode_(::system(script.data()));
  return code == expected;
}

static void setenv_(const char *name, const ZtString<> &value)
{
  ::setenv(name, value.data(), 1);
}

static void setenv_(const char *name, unsigned value)
{
  ZtString<> s;
  s << value;
  ::setenv(name, s.data(), 1);
}

static pid_t startServer_(
  ZuCSpan testCase, const ZtString<> &www, const ZtString<> &cert,
  const ZtString<> &key, unsigned port)
{
  int ready[2];
  if (::pipe(ready) != 0) return -1;

  pid_t pid = ::fork();
  if (pid) {
    ::close(ready[1]);
    char b = 0;
    if (pid > 0) (void)::read(ready[0], &b, 1);
    ::close(ready[0]);
    return pid;
  }

  ::close(ready[0]);
  int fd = ::open("/dev/null", O_RDWR);
  if (fd >= 0) {
    ::dup2(fd, 1);
    ::dup2(fd, 2);
    if (fd > 2) ::close(fd);
  }
  ::setenv("TESTCASE", ZtString<>{testCase}.data(), 1);
  setenv_("ZHTTP_QIR_WWW", www);
  setenv_("ZHTTP_QIR_CERT", cert);
  setenv_("ZHTTP_QIR_KEY", key);
  setenv_("ZHTTP_QIR_PORT", port);
  char b = 1;
  (void)::write(ready[1], &b, 1);
  ::close(ready[1]);
  ::execl("./zhttpqir", "zhttpqir", "server", static_cast<char *>(nullptr));
  ::_exit(127);
}

static bool stopServer_(pid_t pid)
{
  if (pid <= 0) return false;
  ::kill(pid, SIGTERM);
  int status = 0;
  if (::waitpid(pid, &status, 0) != pid) return false;
  if (WIFEXITED(status)) return WEXITSTATUS(status) == 0;
  return WIFSIGNALED(status) && WTERMSIG(status) == SIGTERM;
}

static bool writeFile_(ZuCSpan path, ZuCSpan body)
{
  ZiFile file;
  if (file.open(path,
      ZiFile::Write | ZiFile::Create | ZiFile::Truncate | ZiFile::GC,
      0666) != Zi::OK)
    return false;
  return file.write(body.data(), body.length()) == Zi::OK;
}

static bool cmpFile_(ZuCSpan a, ZuCSpan b)
{
  ZtString<> cmd;
  cmd << "cmp " << a << ' ' << b;
  return systemOK(::system(cmd.data()));
}

static void testExecutableContract()
{
  ZuTestScope(testExecutableContract);

  ZuCHECK(runStatus_("./zhttpqir --help", 0), "help exits 0");
  ZuCHECK(runStatus_("./zhttpqir", 2), "missing role exits 2");
  ZuCHECK(runStatus_("./zhttpqir bogus", 2), "bad role exits 2");
  ZuCHECK(runStatus_("TESTCASE=retry ./zhttpqir client", 127),
    "unsupported testcase exits 127");
  ZuCHECK(runStatus_("TESTCASE=http3 ./zhttpqir client", 2),
    "supported client without requests exits 2");
}

static void testClientFilesystemPrep()
{
  ZuTestScope(testClientFilesystemPrep);

  TempDir temp;
  ZuCHECK(temp.init("ZhttpQIRSmoke"), "QIR smoke temp dir created");
  ZtString<> downloads;
  downloads << static_cast<const char *>(temp.path) << "/downloads";
  ZtString<> cmd;
  cmd << "TESTCASE=http3 REQUESTS='https://server/a/b/file.bin "
    "https://other/file.bin' "
    "ZHTTP_QIR_DOWNLOADS=" << downloads << " ./zhttpqir client";
  ZuCHECK(runStatus_(cmd, 2),
    "valid client request creates dirs before authority validation");
  ZtString<> parent;
  parent << downloads << "/a/b";
  ZuCHECK(ZiStat{parent}.isdir(), "client creates output parent dirs");

  cmd.length(0);
  cmd << "TESTCASE=http3 REQUESTS=https://server/a/../bad "
    "ZHTTP_QIR_DOWNLOADS=" << downloads << " ./zhttpqir client";
  ZuCHECK(runStatus_(cmd, 2), "client rejects escaped output path");
}

static bool runHQSingleFile_(ZuCSpan testCase)
{
  TempDir temp;
  if (!temp.init("ZhttpQIRHQ")) return false;

  ZtString<> www;
  www << static_cast<const char *>(temp.path) << "/www";
  ZtString<> downloads;
  downloads << static_cast<const char *>(temp.path) << "/downloads";
  ZtString<> certPath;
  ZtString<> keyPath;
  if (ZiFile::mkdir(www) != Zi::OK ||
      ZiFile::mkdir(downloads) != Zi::OK ||
      !writeLocalhostCert(temp, certPath, keyPath))
    return false;

  ZtString<> src;
  src << www << "/file.bin";
  if (!writeFile_(src, "hq-body")) return false;

  unsigned port = loopbackPort(ZhttpInteropPort::QIRHQSingle);
  pid_t server = startServer_(testCase, www, certPath, keyPath, port);
  if (server <= 0) return false;

  ZtString<> cmd;
  cmd << "TESTCASE=" << testCase <<
    " REQUESTS=https://127.0.0.1:" << port <<
    "/file.bin ZHTTP_QIR_DOWNLOADS=" << downloads <<
    " ZHTTP_QIR_CA=" << certPath <<
    " ZHTTP_QIR_PORT=" << port << " ./zhttpqir client";

  bool ok = runStatus_(cmd, 0);
  bool stopped = stopServer_(server);

  ZtString<> dst;
  dst << downloads << "/file.bin";
  return ok && stopped && cmpFile_(src, dst);
}

static void testHQSingleFile()
{
  ZuTestScope(testHQSingleFile);

  ZuCHECK(runHQSingleFile_("transfer"), "transfer hq single-file");
}

static void testHQHandshakeFile()
{
  ZuTestScope(testHQHandshakeFile);

  ZuCHECK(runHQSingleFile_("handshake"), "handshake hq single-file");
}

static void testHQRebindPortFile()
{
  ZuTestScope(testHQRebindPortFile);

  ZuCHECK(runHQSingleFile_("rebind-port"), "rebind-port hq single-file");
}

static void testHQRebindAddrFile()
{
  ZuTestScope(testHQRebindAddrFile);

  ZuCHECK(runHQSingleFile_("rebind-addr"), "rebind-addr hq single-file");
}

static void testH3SingleFile()
{
  ZuTestScope(testH3SingleFile);

  TempDir temp;
  ZuCHECK(temp.init("ZhttpQIRH3"), "QIR H3 temp dir created");

  ZtString<> www;
  www << static_cast<const char *>(temp.path) << "/www";
  ZtString<> downloads;
  downloads << static_cast<const char *>(temp.path) << "/downloads";
  ZtString<> certPath;
  ZtString<> keyPath;
  ZuCHECK(ZiFile::mkdir(www) == Zi::OK, "www created");
  ZuCHECK(ZiFile::mkdir(downloads) == Zi::OK, "downloads created");
  ZuCHECK(writeLocalhostCert(temp, certPath, keyPath),
    "localhost cert created");

  ZtString<> src;
  src << www << "/file.bin";
  ZuCHECK(writeFile_(src, "h3-body"), "source file written");

  unsigned port = loopbackPort(ZhttpInteropPort::QIRH3Single);
  pid_t server = startServer_("http3", www, certPath, keyPath, port);
  ZuCHECK(server > 0, "H3 server forked");

  ZtString<> cmd;
  cmd << "TESTCASE=http3 REQUESTS=https://127.0.0.1:" << port <<
    "/file.bin ZHTTP_QIR_DOWNLOADS=" << downloads <<
    " ZHTTP_QIR_CA=" << certPath <<
    " ZHTTP_QIR_PORT=" << port << " ./zhttpqir client";

  bool ok = runStatus_(cmd, 0);
  bool stopped = stopServer_(server);
  ZuCHECK(ok, "H3 client downloaded file");
  ZuCHECK(stopped, "H3 server stopped");

  ZtString<> dst;
  dst << downloads << "/file.bin";
  ZuCHECK(cmpFile_(src, dst), "H3 downloaded file matches");
}

static void testHQMultiFile()
{
  ZuTestScope(testHQMultiFile);

  TempDir temp;
  ZuCHECK(temp.init("ZhttpQIRHQMulti"), "QIR HQ multi temp dir created");

  ZtString<> www;
  www << static_cast<const char *>(temp.path) << "/www";
  ZtString<> downloads;
  downloads << static_cast<const char *>(temp.path) << "/downloads";
  ZtString<> certPath;
  ZtString<> keyPath;
  ZuCHECK(ZiFile::mkdir(www) == Zi::OK, "www created");
  ZuCHECK(ZiFile::mkdir(downloads) == Zi::OK, "downloads created");
  ZuCHECK(writeLocalhostCert(temp, certPath, keyPath),
    "localhost cert created");

  ZtString<> nested;
  nested << www << "/nested";
  ZuCHECK(ZiFile::mkdir(nested) == Zi::OK, "nested www dir created");

  ZtString<> src0, src1, src2;
  src0 << www << "/a.bin";
  src1 << nested << "/b.bin";
  src2 << www << "/c.bin";
  ZuCHECK(writeFile_(src0, "alpha"), "source a written");
  ZuCHECK(writeFile_(src1, "bravo"), "source b written");
  ZuCHECK(writeFile_(src2, "charlie"), "source c written");

  unsigned port = loopbackPort(ZhttpInteropPort::QIRHQMulti);
  pid_t server = startServer_("transfer", www, certPath, keyPath, port);
  ZuCHECK(server > 0, "HQ server forked");

  ZtString<> cmd;
  cmd << "TESTCASE=transfer REQUESTS='https://127.0.0.1:" << port <<
    "/a.bin https://127.0.0.1:" << port <<
    "/nested/b.bin https://127.0.0.1:" << port <<
    "/c.bin' ZHTTP_QIR_DOWNLOADS=" << downloads <<
    " ZHTTP_QIR_CA=" << certPath <<
    " ZHTTP_QIR_PORT=" << port << " ./zhttpqir client";

  bool ok = runStatus_(cmd, 0);
  bool stopped = stopServer_(server);
  ZuCHECK(ok, "HQ client downloaded multiple files");
  ZuCHECK(stopped, "HQ server stopped");

  ZtString<> dst0, dst1, dst2;
  dst0 << downloads << "/a.bin";
  dst1 << downloads << "/nested/b.bin";
  dst2 << downloads << "/c.bin";
  ZuCHECK(cmpFile_(src0, dst0), "HQ a matches");
  ZuCHECK(cmpFile_(src1, dst1), "HQ b matches");
  ZuCHECK(cmpFile_(src2, dst2), "HQ c matches");
}

static void testH3MultiFile()
{
  ZuTestScope(testH3MultiFile);

  TempDir temp;
  ZuCHECK(temp.init("ZhttpQIRH3Multi"), "QIR H3 multi temp dir created");

  ZtString<> www;
  www << static_cast<const char *>(temp.path) << "/www";
  ZtString<> downloads;
  downloads << static_cast<const char *>(temp.path) << "/downloads";
  ZtString<> certPath;
  ZtString<> keyPath;
  ZuCHECK(ZiFile::mkdir(www) == Zi::OK, "www created");
  ZuCHECK(ZiFile::mkdir(downloads) == Zi::OK, "downloads created");
  ZuCHECK(writeLocalhostCert(temp, certPath, keyPath),
    "localhost cert created");

  ZtString<> nested;
  nested << www << "/nested";
  ZuCHECK(ZiFile::mkdir(nested) == Zi::OK, "nested www dir created");

  ZtString<> src0, src1, src2;
  src0 << www << "/a.bin";
  src1 << nested << "/b.bin";
  src2 << www << "/c.bin";
  ZuCHECK(writeFile_(src0, "alpha-h3"), "source a written");
  ZuCHECK(writeFile_(src1, "bravo-h3"), "source b written");
  ZuCHECK(writeFile_(src2, "charlie-h3"), "source c written");

  unsigned port = loopbackPort(ZhttpInteropPort::QIRH3Multi);
  pid_t server = startServer_("http3", www, certPath, keyPath, port);
  ZuCHECK(server > 0, "H3 server forked");

  ZtString<> cmd;
  cmd << "TESTCASE=http3 REQUESTS='https://127.0.0.1:" << port <<
    "/a.bin https://127.0.0.1:" << port <<
    "/nested/b.bin https://127.0.0.1:" << port <<
    "/c.bin' ZHTTP_QIR_DOWNLOADS=" << downloads <<
    " ZHTTP_QIR_CA=" << certPath <<
    " ZHTTP_QIR_PORT=" << port << " ./zhttpqir client";

  bool ok = runStatus_(cmd, 0);
  bool stopped = stopServer_(server);
  ZuCHECK(ok, "H3 client downloaded multiple files");
  ZuCHECK(stopped, "H3 server stopped");

  ZtString<> dst0, dst1, dst2;
  dst0 << downloads << "/a.bin";
  dst1 << downloads << "/nested/b.bin";
  dst2 << downloads << "/c.bin";
  ZuCHECK(cmpFile_(src0, dst0), "H3 a matches");
  ZuCHECK(cmpFile_(src1, dst1), "H3 b matches");
  ZuCHECK(cmpFile_(src2, dst2), "H3 c matches");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testExecutableContract);
  ZuTestCall(testClientFilesystemPrep);
  ZuTestCall(testH3SingleFile);
  ZuTestCall(testH3MultiFile);
  ZuTestCall(testHQSingleFile);
  ZuTestCall(testHQMultiFile);
  ZuTestCall(testHQHandshakeFile);
  ZuTestCall(testHQRebindPortFile);
  ZuTestCall(testHQRebindAddrFile);
}
