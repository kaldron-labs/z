//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdlib.h>
#include <string.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmScratch.hh>
#include <zlib/ZmTrap.hh>

#include <zlib/ZiLog.hh>
#include <zlib/ZiCSV.hh>
#include <zlib/ZiFile.hh>

#include "ZiTestResidue.hh"

using namespace ZuTestUtil;

struct LogCsvRow {
  ZuCArray<64>	component;
  ZuCArray<512>	msg;
};
ZfStruct(LogCsvRow,
  (((component), (Ctor<6>)), (String)),
  (((msg),       (Ctor<7>)), (String)));

namespace {

Zi::Path g_root;
Zi::Path g_log;
Zi::Path g_log1;
Zi::Path g_log2;
Zi::Path g_csv;
Zi::Path g_csv1;

bool contains(const ZtString<> &s, const char *needle)
{
  ZuCSpan span{s};
  return ::memmem(span.data(), span.length(), needle, ::strlen(needle));
}

void initPaths()
{
  g_root = ZiFile::append(ZiTestResidue::tempRoot(), "log");
  g_log = ZiFile::append(g_root, "ZiLogTest.log");
  g_csv = ZiFile::append(g_root, "ZiLogTest.csv");

  g_log1 = {};
  g_log1 << g_log << ".1";
  g_log2 = {};
  g_log2 << g_log << ".2";

  g_csv1 = {};
  g_csv1 << g_csv << ".1";

  ZiFile::mkdir(g_root);
  ZiTestResidue::addDir(g_root);

  ZiTestResidue::addFile(g_log);
  ZiTestResidue::addFile(g_log1);
  ZiTestResidue::addFile(g_log2);
  ZiTestResidue::addFile(g_csv);
  ZiTestResidue::addFile(g_csv1);
}

void resetArtifacts()
{
  ZiFile::remove(g_log);
  ZiFile::remove(g_log1);
  ZiFile::remove(g_log2);
  ZiFile::remove(g_csv);
  ZiFile::remove(g_csv1);
}

void configure(ZmRef<ZiSink> sink)
{
  ZiLog::stop();
  ZiLog::init("ZiLogTest");
  ZiLog::level(0);
  ZiLog::sink(ZuMv(sink));
}

ZtString<> readFile(const Zi::Path &path)
{
  ZiFile f;
  if (f.open(path, ZiFile::ReadOnly | ZiFile::GC) != Zi::OK) {
    log_("open(", path, ") failed: ", f.error());
    Zm::exit(1);
  }

  unsigned n = f.size();
  auto buf = ZmScratch(char, n + 1);
  int r = f.read(buf.data(), n);
  if (r < 0) {
    log_("read(", path, ") failed: ", f.error());
    Zm::exit(1);
  }

  buf.length(r);
  ZtString<> out;
  out << buf;
  return out;
}

void testFileSinkWritesAndAges()
{
  ZuTestScope(testFileSinkWritesAndAges);

  resetArtifacts();

  configure(ZiLog::fileSink(ZiSinkOptions{}.path(g_log).age(2)));
  ZiLog::start();
  ZiLOG(Info, "ZiLogFile", "file sink line one");
  ZiLOG(Error, "ZiLogFile", "file sink line two");
  ZiLog::stop();

  auto text = readFile(g_log);
  ZuCheck(contains(text, "ZiLogFile"));
  ZuCheck(contains(text, "file sink line one"));
  ZuCheck(contains(text, "file sink line two"));

  ZiLog::age();
  ZuCheck(ZiStat{g_log1}.exists());
}

void testCsvSinkWritesAndParses()
{
  ZuTestScope(testCsvSinkWritesAndParses);

  resetArtifacts();

  configure(ZiLog::csvSink(ZiSinkOptions{}.path(g_csv).age(2)));
  ZiLog::start();
  ZiLOG(Info, "ZiLogCsv", "csv,message");
  ZiLOG(Warning, "ZiLogCsv", "second message");
  ZiLog::stop();

  auto text = readFile(g_csv);
  ZuCheck(contains(text, "component"));
  ZuCheck(contains(text, "msg"));
  ZuCheck(contains(text, "csv,message"));

  auto reader = ZiCSV::reader<LogCsvRow>();
  unsigned rows = 0;
  bool sawQuoted = false;
  auto r = reader.readFile(g_csv, [&rows, &sawQuoted](const auto &scan) {
    auto row = scan.ctor();
    ++rows;
    if (row.component == "ZiLogCsv" && row.msg == "csv,message")
      sawQuoted = true;
  });
  ZuCheck(!r.template is<ZeException>());
  ZuCheck(rows == 2);
  ZuCheck(sawQuoted);

  ZiLog::age();
  ZuCheck(ZiStat{g_csv1}.exists());
}

void testLambdaSinkReceivesEvents()
{
  ZuTestScope(testLambdaSinkReceivesEvents);

  resetArtifacts();

  unsigned calls = 0;
  int8_t severity = -1;
  ZeString component;
  ZtString<> msg;

  configure(ZiLog::lambdaSink([&calls, &severity, &component, &msg](
      ZeLogBuf &buf, const ZeEventInfo &info) {
    ++calls;
    severity = info.severity;
    component = info.component;
    msg = {};
    msg << ZuCSpan(buf.data(), buf.length());
  }));

  ZiLog::start();
  ZiLOG(Warning, "ZiLogLambda", "lambda sink message");
  ZiLog::stop();

  ZuCheck(calls == 1);
  ZuCheck(severity == Ze::Warning);
  ZuCheck(component == "ZiLogLambda");
  ZuCheck(contains(msg, "lambda sink message"));
}

} // namespace

int main(int argc, char **argv)
{
  ZiTestResidue::init("ZiLogTest");
  ZmTrap::sigintFn(&ZiTestResidue::cleanupNow);
  ZmTrap::trap();
  ::atexit(&ZiTestResidue::cleanupNow);

  initPaths();
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testFileSinkWritesAndAges);
  ZuTestCall(testCsvSinkWritesAndParses);
  ZuTestCall(testLambdaSinkReceivesEvents);
  ZiLog::stop();
  return 0;
}
