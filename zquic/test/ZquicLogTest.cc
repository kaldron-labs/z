//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZtJSON.hh>

#include <zlib/ZiFile.hh>

#include <zlib/Zquic.hh>
#include <zlib/ZquicLog.hh>

using namespace ZuTestUtil;

static Zi::Path testPath_(ZuCSpan name)
{
  Zi::Path path;
  path << name;
  return path;
}

static ZtString<> readFile_(const Zi::Path &path)
{
  ZtString<> data;
  ZiFile file{path, ZiFile::ReadOnly | ZiFile::GC};
  if (!file) return data;
  auto size = file.size();
  if (size <= 0 || size > (1<<20)) return data;
  data.length(unsigned(size));
  int n = file.read(data.data(), data.length());
  if (n <= 0)
    data.length(0);
  else
    data.length(unsigned(n));
  return data;
}

static unsigned parseJSONSeq_(ZuCSpan data)
{
  unsigned n = 0;
  unsigned i = 0;
  while (i < data.length()) {
    ZuCSpan rest{data.data() + i, data.length() - i};
    if (!rest.match<"\x1e">()) return 0;
    ++i;
    unsigned start = i;
    while (i < data.length() && data[i] != '\n') ++i;
    if (i >= data.length()) return 0;
    ZtString<> json;
    json << ZuCSpan{data.data() + start, i - start};
    auto scan = ZtJSON::scan(json);
    if (scan.p<0>() < 0) return 0;
    ++n;
    ++i;
  }
  return n;
}

static bool containsQLogVersion_(ZuCSpan data)
{
  return data.find<"qlog_version">() >= 0;
}

static bool containsPacketSent_(ZuCSpan data)
{
  return data.find<"packet_sent">() >= 0;
}

static bool containsZiLogPrefix_(ZuCSpan data)
{
  return data.find<"[Zquic]">() >= 0;
}

void testQLogFileOutput()
{
  ZuTestScope(testQLogFileOutput);

#ifdef Zquic_DEBUG
  Zi::Path path = testPath_("ZquicLogTest.sqlog");
  ZiFile::remove(path);

  ZquicLogParams params;
  params.enabled(true).path(path).thread("zquic-qlog-test").ringSize(1<<15);
  ZuCHECK(ZquicLog::init(params), "qlog init failed");
  ZquicLog::start();
  ZuCHECK(ZquicLog::enabled(), "qlog did not enable");
  ZquicLog::event(
    "packet_sent", "transport", "abcdef", 1200, "initial");
  ZquicLog::event(
    "connection_closed", "transport", "abcdef", 0, "local");
  ZquicLog::stop();
  ZquicLogDiag diag = ZquicLog::diag();
  ZquicLog::final();

  ZuCHECK(diag.recordsEnqueued >= 2, "qlog enqueue diagnostics mismatch");
  ZuCHECK(diag.recordsWritten >= 3, "qlog write diagnostics mismatch");
  ZuCHECK(diag.writerFailures == 0, "qlog writer failure");

  ZtString<> data = readFile_(path);
  ZuCHECK(data, "qlog output was not written");
  ZuCHECK(parseJSONSeq_(data) >= 3, "qlog JSON-SEQ parse failed");
  ZuCHECK(containsQLogVersion_(data), "qlog header missing version");
  ZuCHECK(containsPacketSent_(data), "qlog event missing packet_sent");
  ZuCHECK(!containsZiLogPrefix_(data), "qlog contains ZiLog text prefix");
  ZiFile::remove(path);
#else
  ZquicLogParams params;
  params.enabled(true).path("ZquicLogTest.sqlog");
  ZuCHECK(ZquicLog::init(params), "compiled-out qlog init failed");
  ZquicLog::start();
  ZuCHECK(!ZquicLog::enabled(), "compiled-out qlog enabled");
  ZquicLog::event("packet_sent", "transport");
  ZquicLogDiag diag = ZquicLog::diag();
  ZquicLog::stop();
  ZquicLog::final();
  ZuCHECK(!diag.recordsEnqueued && !diag.recordsWritten,
    "compiled-out qlog produced diagnostics");
#endif
}

void testQLogBackPressure()
{
  ZuTestScope(testQLogBackPressure);

#ifdef Zquic_DEBUG
  Zi::Path path = testPath_("ZquicLogDropTest.sqlog");
  ZiFile::remove(path);

  ZquicLogParams params;
  params.enabled(true).path(path).thread("zquic-qlog-drop").ringSize(256);
  ZuCHECK(ZquicLog::init(params), "qlog drop init failed");
  ZquicLog::start();
  for (unsigned i = 0; i < 10000; ++i)
    ZquicLog::event(
      "packet_received", "transport", "abcdef", i,
      "large record intended to pressure a tiny qlog ring");
  ZquicLog::stop();
  ZquicLogDiag diag = ZquicLog::diag();
  ZquicLog::final();

  ZuCHECK(diag.recordsDropped || diag.ringBackPressure,
    "qlog tiny-ring event was not dropped");
  ZiFile::remove(path);
#endif
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testQLogFileOutput);
  ZuTestCall(testQLogBackPressure);
}
