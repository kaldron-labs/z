//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZfJSON.hh>

#include <zlib/ZiFile.hh>

#include <zlib/Zutcp.hh>

#include "ZiTestResidue.hh"

using namespace ZuTestUtil;
using namespace Zutcp;
using namespace ZutcpLog_;

#ifdef Zutcp_DEBUG
static Zi::Path g_residue;

static Zi::Path testPath(ZuCSpan name)
{
  return ZiFile::append(g_residue, name);
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

static unsigned parseJSONSeq(ZuCSpan data)
{
  unsigned n = 0;
  unsigned i = 0, l = data.length();
  while (i < l) {
    ZuCSpan rest{data.data() + i, l - i};
    if (!rest.match<"\x1e">()) return 0;
    ++i;
    unsigned start = i;
    while (i < l && data[i] != '\n') ++i;
    if (i >= l) return 0;
    ZtString<> json;
    json << ZuCSpan{data.data() + start, i - start};
    auto scan = ZfJSON::scan(json);
    if (scan.p<0>() != int(json.length())) return 0;
    ++n;
    ++i;
  }
  return n;
}

#endif

static void closeLog(ZutcpLogger::Trace &trace)
{
  ZmSemaphore done;
  ZutcpLogger::close(trace, [&done]() { done.post(); });
  done.wait();
}

void testTCPEventValues()
{
  ZuTestScope(testTCPEventValues);
#ifdef Zutcp_DEBUG
  auto path = testPath("tcp-events.json-seq");
  ZiFile::remove(path);
  Zi::Path aged;
  aged << path << ".1";
  ZiFile::remove(aged);
  {
    ZiFile old{path, ZiFile::Write | ZiFile::GC};
    ZuCHECK(old && old.write("previous\n", 9) == Zi::OK, "log aging seed");
  }
  ZutcpLogParams params;
  params.enabled(true).path(path).age(2).ringSize(1<<15);
  ZutcpLogger::Trace trace;
  ZuCHECK(ZutcpLogger::init(trace, params, Vantage::Client) && ZutcpLogger::start(),
    "TCP trace initializes and starts");
  LinkInfo info;
  info.vantage = Vantage::Client;
  info.local = {ZiIP("::1"), 49152};
  info.remote = {ZiIP("::2"), 443};
  CxnStartedEvt started;
  started.local = info.local;
  started.remote = info.remote;
  started.linkInfo = info;
  ZutcpLogger::cxnStarted(trace, started);
  PktEvt packet;
  packet.linkInfo = info;
  packet.header.seq = 0xffffffffU;
  packet.header.ack = 42;
  packet.header.flags = 0x18;
  packet.header.window = 65535;
  packet.header.options.flags = TCPOptions::TS;
  packet.header.options.tsval = 0;
  packet.header.options.tsecr = 19;
  packet.header.options.sackCount = 2;
  packet.header.options.sack[0] = {9,12};
  packet.header.options.sack[1] = {20,30};
  packet.packetSize = 128;
  packet.payloadSize = 19;
  packet.ecn = EcnMark::ECT0;
  ZutcpLogger::pktSent(trace, packet);
  ZutcpLogger::pktRecv(trace, packet);
  packet.reason = Reason::PAWS;
  ZutcpLogger::pktDrop(trace, packet);
  AckEvt ack;
  ack.linkInfo = info;
  ack.byteNext = (uint64_t(1)<<32) + 19;
  ack.ackedBytes = 19;
  ack.lostBytes = 3;
  ZutcpLogger::pktsAcked(trace, ack);
  RecLogEvt recovery;
  recovery.linkInfo = info;
  recovery.transmissionID = 123;
  recovery.bytes = 19;
  recovery.deadlineUS = 1000;
  recovery.latestRTTUS = 500;
  recovery.smoothedRTTUS = 550;
  recovery.cwnd = 2400;
  recovery.ssthresh = 1200;
  recovery.bytesInFlight = 19;
  recovery.kind = RecKind::RTO;
  recovery.reason = RecReason::Expired;
  ZutcpLogger::pktLost(trace, recovery);
  ZutcpLogger::markRetrans(trace, recovery);
  ZutcpLogger::metricsUpd(trace, recovery);
  ZutcpLogger::lossTimerUpd(trace, recovery);
  ZutcpLogger::congStateUpd(trace, recovery);
  PathEvt route;
  route.linkInfo = info;
  route.mtu = 1500;
  route.bytes = 19;
  route.validated = true;
  ZutcpLogger::pathUpdated(trace, route);
  ZutcpLogger::pmtudUpdated(trace, route);
  CxnStateEvt state;
  state.linkInfo = info;
  state.newState = LinkState::Established;
  ZutcpLogger::cxnStateUpd(trace, state);
  CloseEvt close;
  close.linkInfo = info;
  close.errorCode = 0x123456789abcdef0ULL;
  close.reason = CloseReason::LocalClose;
  ZutcpLogger::cxnClosed(trace, close);
  closeLog(trace);
  ZuCHECK(ZutcpLogger::stop(), "writer stops after the trace close completion");
  auto diag = ZutcpLogger::diag();
  ZutcpLogger::final(trace);
  auto data = readFile_(path);
  ZuCHECK(parseJSONSeq(data) == 15 && diag.recordsWritten == 15 && !diag.writerFailures,
    "header and every actual TCP event are complete JSON-sequence records");
  if (!data) return;
  ZuCHECK(strstr(data.data(), "\"format\":\"zutcp-tcp-json-seq\"") &&
      strstr(data.data(), "\"local_ip\":\"::1\"") &&
      strstr(data.data(), "\"local_port\":49152") &&
      strstr(data.data(), "\"remote_port\":443"), "TCP trace records the actual tuple");
  ZuCHECK(strstr(data.data(), "\"seq\":4294967295") &&
      strstr(data.data(), "\"ack\":42") && strstr(data.data(), "\"flags\":24") &&
      strstr(data.data(), "\"window\":65535") &&
      strstr(data.data(), "\"sack\":[[9,12],[20,30]]") &&
      strstr(data.data(), "\"options_bytes\":28") &&
      strstr(data.data(), "\"byte_next\":4294967315"),
    "wire sequence/options and wide cumulative byte frontiers retain their own coordinates");
  ZuCHECK(strstr(data.data(), "\"local_transmission_id\":123") &&
      strstr(data.data(), "\"local_error_code\":1311768467463790320") &&
      !strstr(data.data(), "quic:") && !strstr(data.data(), "packet_number") &&
      !strstr(data.data(), "dcid") && !strstr(data.data(), "frames"),
    "internal transmission identity and local errors are distinct from actual wire evidence");
  ZuCHECK(readFile_(aged) == "previous\n", "configured output aging preserves the prior file");
#endif
}

void testTraceIsolation()
{
  ZuTestScope(testTraceIsolation);
#ifdef Zutcp_DEBUG
  ZutcpLogger::Trace first, second;
  auto one = testPath("first.json-seq"), two = testPath("second.json-seq");
  ZiFile::remove(one);
  ZiFile::remove(two);
  ZutcpLogParams params;
  params.enabled(true).path(one).ringSize(1<<15);
  ZuCHECK(ZutcpLogger::init(first, params, Vantage::Client), "first trace configures");
  params.path(two);
  ZuCHECK(ZutcpLogger::init(second, params, Vantage::Server) && ZutcpLogger::start(),
    "second trace shares the existing writer");
  PktEvt packet;
  packet.header.seq = 100;
  ZutcpLogger::pktSent(first, packet);
  packet.header.seq = 200;
  ZutcpLogger::pktRecv(second, packet);
  closeLog(first);
  packet.header.seq = 201;
  ZutcpLogger::pktRecv(second, packet);
  closeLog(second);
  ZutcpLogger::stop();
  auto a = readFile_(one), b = readFile_(two);
  ZuCHECK(a && b, "both trace outputs exist");
  if (!a || !b) return;
  ZuCHECK(parseJSONSeq(a) == 2 && parseJSONSeq(b) == 3 &&
      strstr(a.data(), "\"seq\":100") && !strstr(a.data(), "\"seq\":200") &&
      strstr(b.data(), "\"seq\":200") && strstr(b.data(), "\"seq\":201"),
    "closing one trace preserves the sibling writer and keeps endpoint observations isolated");
  ZutcpLogger::final(first);
  ZutcpLogger::final(second);
#endif
}

void testLogBackPressure()
{
  ZuTestScope(testLogBackPressure);

#ifdef Zutcp_DEBUG
  Zi::Path path = testPath("ZutcpLogDropTest.sqlog");
  ZiFile::remove(path);

  ZutcpLogParams params;
  params.enabled(true).path(path).ringSize(256);
  ZutcpLogger::Trace trace;
  ZuCHECK(ZutcpLogger::init(trace, params, Zutcp::Vantage::Unknown), "qlog drop init failed");
  ZutcpLogger::start();
  for (unsigned i = 0; i < 10000; ++i) {
    PktEvt packet;
    packet.header.seq = i;
    packet.header.flags = 0x10;
    packet.packetSize = 1200;
    ZutcpLogger::pktRecv(trace, ZuMv(packet));
  }
  closeLog(trace);
  ZutcpLogger::stop();
  ZutcpLogDiag diag = ZutcpLogger::diag();
  ZutcpLogger::final(trace);

  ZuCHECK(diag.recordsDropped || diag.ringBackPressure,
    "qlog tiny-ring event was not dropped");
#endif
}

void testLogAppTrace()
{
  ZuTestScope(testLogAppTrace);

#ifdef Zutcp_DEBUG
  Zi::Path path = testPath("ZutcpLogAppTraceTest.sqlog");
  ZiFile::remove(path);

  ZutcpLogParams params;
  params.enabled(true).path(path).ringSize(1<<15);
  ZutcpLogger::Trace trace;
  ZuCHECK(ZutcpLogger::init(trace, params, Zutcp::Vantage::Client),
    "app trace qlog init failed");
  ZutcpLogger::start();
  ZutcpLogger::log(trace, [](auto &o, ZuTime time) {
    PktEvt packet;
    packet.header.seq = 99;
    packet.header.flags = 0x02;
    packet.linkInfo.vantage = Vantage::Client;
    packet.packetSize = 1200;
    o.logPktSent(packet, time);
  });
  closeLog(trace);
  ZutcpLogger::stop();
  ZutcpLogDiag diag = ZutcpLogger::diag();
  ZutcpLogger::final(trace);

  ZuCHECK(diag.recordsEnqueued >= 1, "app trace qlog enqueue mismatch");
  ZuCHECK(diag.recordsWritten >= 2, "app trace qlog write mismatch");
  ZuCHECK(diag.writerFailures == 0, "app trace qlog writer failure");

  ZtString<> data = readFile_(path);
  ZuCHECK(data, "app trace qlog output was not written");
  ZuCHECK(parseJSONSeq(data) >= 2, "app trace qlog JSON-SEQ parse failed");
  if (!data) return;
  ZuCHECK(strstr(data.data(), "\"vantage\":1"),
    "app trace qlog missing client vantage");
  ZuCHECK(strstr(data.data(), "\"event\":\"segment_sent\""),
    "application trace contains actual segment output");
#endif
}

void testLogRestartDrain()
{
  ZuTestScope(testLogRestartDrain);
#ifdef Zutcp_DEBUG
  Zi::Path path = testPath("ZutcpLogRestart.sqlog");
  ZutcpLogParams params;
  params.enabled(true).path(path).ringSize(1<<15);
  ZutcpLogger::Trace first, next;
  ZuCheck(ZutcpLogger::init(first, params, Zutcp::Vantage::Client));
  ZutcpLogger::start();
  ZmSemaphore held, release, stopped, restarted;
  ZutcpLogger::close(first, [&held, &release]() { held.post(); release.wait(); });
  ZuCheck(held.timedwait(Zm::now(10)) == 0);
  ZmAtomic<unsigned> completions = 0;
  ZutcpLogger::stopIdle([&stopped, &completions](bool ok) {
    if (ok) ++completions;
    stopped.post();
  });
  ZuCheck(!ZutcpLogger::enabled());
  ZuCheck(ZutcpLogger::init(next, params, Zutcp::Vantage::Server));
  ZutcpLogger::start([&restarted, &completions](bool ok) {
    if (ok) ++completions;
    restarted.post();
  });
  // Both requests returned while the old writer is still held.
  ZuCheck(completions == 0);
  ZuCheck(!ZutcpLogger::enabled());
  release.post();
  ZuCheck(stopped.timedwait(Zm::now(10)) == 0);
  ZuCheck(restarted.timedwait(Zm::now(10)) == 0);
  ZuCheck(completions == 2);
  ZuCheck(ZutcpLogger::enabled());
  ZutcpLogger::Trace sibling;
  ZuCheck(ZutcpLogger::init(sibling, params, Zutcp::Vantage::Client));
  ZmSemaphore nextClosed, siblingClosed;
  // Match Hub shutdown: trace close immediately requests idle stop from the
  // writer callback, potentially before the producer has returned from push2.
  ZutcpLogger::close(next, [&nextClosed, &completions]() {
    ZutcpLogger::stopIdle([&nextClosed, &completions](bool ok) {
      if (ok) ++completions;
      nextClosed.post();
    });
  });
  ZuCheck(nextClosed.timedwait(Zm::now(10)) == 0);
  ZuCheck(completions == 3);
  ZuCheck(ZutcpLogger::enabled());
  ZutcpLogger::close(sibling, [&siblingClosed, &completions]() {
    ZutcpLogger::stopIdle([&siblingClosed, &completions](bool ok) {
      if (ok) ++completions;
      siblingClosed.post();
    });
  });
  ZuCheck(siblingClosed.timedwait(Zm::now(10)) == 0);
  ZuCheck(completions == 4);
  ZuCheck(!ZutcpLogger::enabled());
  // A stopped final-close callback must leave the singleton reusable.
  ZuCheck(ZutcpLogger::init(first, params, Zutcp::Vantage::Client));
  ZuCheck(ZutcpLogger::start());
  ZmAtomic<unsigned> delivered = 0;
  ZutcpLogger::log(first, [&delivered](auto &, ZuTime) { ++delivered; });
  closeLog(first);
  ZuCheck(ZutcpLogger::stop());
  ZuCheck(ZutcpLogger::stop());
  ZuCheck(delivered == 1);
  ZutcpLogger::final(first);
  ZutcpLogger::final(next);
  ZutcpLogger::final(sibling);
  ZuCheck(!ZutcpLogger::enabled());
#endif
}

void testLogStopDrain()
{
  ZuTestScope(testLogStopDrain);
#ifdef Zutcp_DEBUG
  ZutcpLogParams params;
  params.enabled(true).path(testPath("ZutcpLogDrain.sqlog")).ringSize(4096);
  ZutcpLogger::Trace trace;
  ZuCheck(ZutcpLogger::init(trace, params, Zutcp::Vantage::Client));
  ZuCheck(ZutcpLogger::start());
  ZuCheck(ZutcpLogger::start());
  ZmSemaphore held, release, stopped;
  ZmAtomic<unsigned> written = 0;
  ZmAtomic<unsigned> done = 0;
  ZutcpLogger::log(trace, [&held, &release](auto &, ZuTime) {
    held.post();
    release.wait();
  });
  ZuCheck(held.timedwait(Zm::now(10)) == 0);
  for (unsigned i = 0; i < 10000; ++i)
    ZutcpLogger::log(trace, [&written](auto &, ZuTime) { ++written; });
  ZuCheck(ZutcpLogger::diag().ringBackPressure > 0);
  ZutcpLogger::stop([&done, &stopped](bool ok) {
    done = ok;
    stopped.post();
  });
  ZuCheck(!done);
  release.post();
  ZuCheck(stopped.timedwait(Zm::now(10)) == 0);
  ZuCheck(done);
  ZuCheck(written == 10000);
  ZutcpLogger::final(trace);
#endif
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZiTestResidue::init("ZutcpLogTest");
#ifdef Zutcp_DEBUG
  g_residue = ZiTestResidue::dir("tcp-log");
#endif
  ZuTestMain();
  ZuTestCall(testLogRestartDrain);
  ZuTestCall(testLogStopDrain);
  ZuTestCall(testTCPEventValues);
  ZuTestCall(testTraceIsolation);
  ZuTestCall(testLogBackPressure);
  ZuTestCall(testLogAppTrace);
}
