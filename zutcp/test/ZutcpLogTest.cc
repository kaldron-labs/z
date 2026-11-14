//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

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

static bool hasQLogFileSchema(ZuCSpan data)
{
  return
    data.find<"file_schema">() >= 0 &&
    data.find<"urn:ietf:params:qlog:file:sequential">() >= 0 &&
    data.find<"serialization_format">() >= 0 &&
    data.find<"application/qlog+json-seq">() >= 0;
}

static bool hasQLogHeaderMetadata(ZuCSpan data)
{
  return
    data.find<"vantage_point">() >= 0 &&
    data.find<"event_schemas">() >= 0 &&
    data.find<"urn:ietf:params:qlog:events:quic-12">() >= 0 &&
    data.find<"urn:zlib:zutcp:qlog:events:zutcp">() >= 0 &&
    data.find<"time_format">() >= 0 &&
    data.find<"relative_to_epoch">() >= 0 &&
    data.find<"reference_time">() >= 0 &&
    data.find<"implementation">() >= 0 &&
    data.find<"version">() >= 0 &&
    data.find<"zutcp">() >= 0;
}

static bool hasQLogVantage(ZuCSpan data, ZuCSpan vantage)
{
  ZtString<> s;
  s << "\"type\":\"" << vantage << '"';
  return data.find(s) >= 0;
}

static bool hasQLogConnectionMetadata(ZuCSpan data)
{
  return
    data.find<"common_fields">() >= 0 &&
    data.find<"ODCID">() >= 0 &&
    data.find<"group_id">() >= 0 &&
    data.find<"DCID">() >= 0 &&
    data.find<"SCID">() >= 0 &&
    data.find<"DEADBEEF">() >= 0 &&
    data.find<"01020304">() >= 0 &&
    data.find<"11121314">() >= 0 &&
    data.find<"21222324">() >= 0;
}

static bool hasPacketSent(ZuCSpan data)
{
  return data.find<"packet_sent">() >= 0;
}

static bool hasTransportPacketSent(ZuCSpan data)
{
  return data.find<"quic:packet_sent">() >= 0;
}

static bool hasTransportPacketReceived(ZuCSpan data)
{
  return data.find<"quic:packet_received">() >= 0;
}

static bool hasTransportPacketBuffered(ZuCSpan data)
{
  return data.find<"quic:packet_buffered">() >= 0;
}

static bool hasTransportPacketDropped(ZuCSpan data)
{
  return data.find<"quic:packet_dropped">() >= 0;
}

static bool hasDatagramReceived(ZuCSpan data)
{
  return data.find<"quic:udp_datagrams_received">() >= 0;
}

static bool hasDatagramSent(ZuCSpan data)
{
  return data.find<"quic:udp_datagrams_sent">() >= 0;
}

static bool hasTypedPacketFields(ZuCSpan data)
{
  return
    data.find<"header">() >= 0 &&
    data.find<"packet_type">() >= 0 &&
    data.find<"packet_number_space">() >= 0 &&
	    data.find<"packet_number">() >= 0 &&
	    data.find<"raw">() >= 0 &&
	    data.find<"payload_length">() >= 0 &&
	    data.find<"trigger">() >= 0 &&
	    data.find<"bytes_in_flight">() >= 0 &&
	    data.find<"frames">() >= 0 &&
	    data.find<"frames_truncated">() >= 0 &&
    data.find<"ack_eliciting">() >= 0;
}

static bool hasTypedDatagramFields(ZuCSpan data)
{
  return
    data.find<"\"count\":1">() >= 0 &&
    data.find<"\"raw\":[{">() >= 0 &&
    data.find<"\"payload_length\":1234">() >= 0 &&
    data.find<"\"payload_length\":1200">() >= 0 &&
    data.find<"\"ecn\":[\"ECT0\"">() >= 0;
}

static bool hasRecoveryEvents(ZuCSpan data)
{
  return
    data.find<"quic:packets_acked">() >= 0 &&
    data.find<"quic:packet_lost">() >= 0 &&
    data.find<"quic:packet_lost">() >= 0 &&
    data.find<"quic:marked_for_retransmit">() >= 0 &&
    data.find<"quic:recovery_metrics_updated">() >= 0 &&
    data.find<"quic:timer_updated">() >= 0 &&
    data.find<"quic:congestion_state_updated">() >= 0 &&
    data.find<"quic:ecn_state_updated">() >= 0;
}

static bool hasRecoveryFields(ZuCSpan data)
{
  return
    data.find<"packet_number_space">() >= 0 &&
    data.find<"packet_numbers">() >= 0 &&
    data.find<"\"packet_numbers\":[97,98,99]">() >= 0 &&
    data.find<"\"name\":\"quic:packet_lost\",\"data\":{\"header\":{">() >= 0 &&
    data.find<"\"trigger\":\"reordering_threshold\"">() >= 0 &&
    data.find<"\"name\":\"quic:marked_for_retransmit\"">() >= 0 &&
    data.find<"\"frame_type\":\"data\"">() >= 0 &&
    data.find<"latest_rtt">() >= 0 &&
    data.find<"smoothed_rtt">() >= 0 &&
    data.find<"rtt_variance">() >= 0 &&
    data.find<"min_rtt">() >= 0 &&
    data.find<"congestion_window">() >= 0 &&
    data.find<"timer_type">() >= 0 &&
    data.find<"event_type">() >= 0 &&
    data.find<"delta">() >= 0 &&
    data.find<"ssthresh">() >= 0 &&
    data.find<"\"name\":\"quic:congestion_state_updated\",\"data\":{\"new\":"
      "\"slow_start\",\"trigger\":\"ack\"">() >= 0 &&
    data.find<"\"name\":\"quic:ecn_state_updated\",\"data\":{\"old\":"
      "\"unknown\",\"new\":\"testing\"">() >= 0 &&
    data.find<"\"name\":\"quic:ecn_state_updated\",\"data\":{\"old\":"
      "\"testing\",\"new\":\"capable\"">() >= 0 &&
    data.find<"\"name\":\"quic:ecn_state_updated\",\"data\":{\"old\":"
      "\"capable\",\"new\":\"failed\"">() >= 0;
}





static bool hasCloseEvents(ZuCSpan data)
{
  return data.find<"quic:connection_closed">() >= 0;
}

static bool hasCloseFields(ZuCSpan data)
{
  return
    data.find<"initiator">() >= 0 &&
    data.find<"trigger">() >= 0 &&
    data.find<"connection_error">() >= 0 &&
    data.find<"application_error">() >= 0 &&
    data.find<"error_code">() >= 0;
}

static bool hasZiLogPrefix(ZuCSpan data)
{
  return data.find<"[Zutcp]">() >= 0;
}
#endif

static void closeQLog(ZutcpLogger::Trace &trace)
{
  ZmSemaphore done;
  ZutcpLogger::close(trace, [&done]() { done.post(); });
  done.wait();
}

void testQLogFileOutput()
{
  ZuTestScope(testQLogFileOutput);

#ifdef Zutcp_DEBUG
  Zi::Path path = testPath("ZutcpLogTest.sqlog");
  Zi::Path agedPath;
  agedPath << path << ".1";
  ZiFile::remove(path);
  ZiFile::remove(agedPath);

  {
    ZiFile old{path, ZiFile::Write | ZiFile::GC};
    ZuCHECK(!!old, "qlog aging seed open failed");
    ZuCHECK(old.write("old-qlog\n", 9) == Zi::OK,
      "qlog aging seed write failed");
  }

  ZutcpLogParams params;
  params.enabled(true).path(path).ringSize(1<<15).
    age(2);
  ZutcpLogger::Trace trace;
  ZuCHECK(ZutcpLogger::init(trace, params, Zutcp::Vantage::Unknown), "qlog init failed");
  auto id4 = [](uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    Zutcp::CxnID id;
    id.length(4);
    id[0] = a;
    id[1] = b;
    id[2] = c;
    id[3] = d;
    return id;
  };
  Zutcp::CxnID odcid = id4(0xde, 0xad, 0xbe, 0xef);
  Zutcp::CxnID groupID = id4(0x01, 0x02, 0x03, 0x04);
  Zutcp::CxnID dcid = id4(0x11, 0x12, 0x13, 0x14);
  Zutcp::CxnID scid = id4(0x21, 0x22, 0x23, 0x24);
  ZutcpLogger::start();
  ZuCHECK(ZutcpLogger::enabled(), "qlog did not enable");
  PktEvt packet;
  packet.packetType = PktType::Initial;
  packet.packetSpace = PktNumSpace::Initial;
  packet.packetSize = 1200;
  packet.linkInfo = Zutcp::LinkInfo{
    .vantage = Zutcp::Vantage::Client,
    .origDCID = odcid,
    .groupID = groupID,
    .dcid = dcid,
    .scid = scid
  };
  ZutcpLogger::pktSent(trace, ZuMv(packet));
  Zutcp::CxnID odcid2 = id4(0xca, 0xfe, 0xba, 0xbe);
  Zutcp::CxnID groupID2 = id4(0x05, 0x06, 0x07, 0x08);
  Zutcp::CxnID dcid2 = id4(0x31, 0x32, 0x33, 0x34);
  Zutcp::CxnID scid2 = id4(0x41, 0x42, 0x43, 0x44);
  ZutcpLogger::cxnStarted(trace, CxnStartedEvt{
    .local = ZiSockAddr{ZiIP{"127.0.0.1"}, 4443},
    .remote = ZiSockAddr{ZiIP{"127.0.0.1"}, 5555},
    .linkInfo = Zutcp::LinkInfo{
      .vantage = Zutcp::Vantage::Server,
      .origDCID = odcid2,
      .groupID = groupID2,
      .dcid = dcid2,
      .scid = scid2
    }
  });
  closeQLog(trace);
  ZutcpLogger::stop();
  ZutcpLogDiag diag = ZutcpLogger::diag();
  ZutcpLogger::final(trace);

  ZuCHECK(diag.recordsEnqueued >= 2, "qlog enqueue diagnostics mismatch");
  ZuCHECK(diag.recordsWritten >= 3, "qlog write diagnostics mismatch");
  ZuCHECK(diag.writerFailures == 0, "qlog writer failure");

  ZtString<> data = readFile_(path);
  ZuCHECK(data, "qlog output was not written");
  ZuCHECK(parseJSONSeq(data) >= 3, "qlog JSON-SEQ parse failed");
  ZuCHECK(hasQLogFileSchema(data), "qlog header missing file schema");
  ZuCHECK(hasQLogHeaderMetadata(data), "qlog header linkInfo missing");
  ZuCHECK(hasQLogVantage(data, "unknown"),
    "qlog header should not assume a process-wide vantage point");
  ZuCHECK(hasQLogConnectionMetadata(data),
    "qlog packet event missing connection linkInfo");
  ZuCHECK(data.find<"CAFEBABE">() >= 0 &&
      data.find<"05060708">() >= 0 &&
      data.find<"31323334">() >= 0 &&
      data.find<"41424344">() >= 0,
    "qlog event linkInfo did not carry replacement connection linkInfo");
  ZuCHECK(data.find<"\"name\":\"quic:connection_started\",\"data\":{"
    "\"local\":{\"ip_v4\":\"127.0.0.1\",\"port_v4\":4443},"
    "\"remote\":{\"ip_v4\":\"127.0.0.1\",\"port_v4\":5555}}">() >= 0,
    "connection_started endpoint data missing");
  ZuCHECK(hasPacketSent(data), "qlog event missing packet_sent");
  ZuCHECK(!hasZiLogPrefix(data), "qlog contains ZiLog text prefix");

  ZtString<> aged = readFile_(agedPath);
  ZuCHECK(aged.find<"old-qlog">() >= 0, "qlog output was not aged");
#else
  ZutcpLogParams params;
  params.enabled(true).path("ZutcpLogTest.sqlog");
  ZutcpLogger::Trace trace;
  ZuCHECK(ZutcpLogger::init(trace, params, Zutcp::Vantage::Unknown), "compiled-out qlog init failed");
  ZutcpLogger::start();
  ZuCHECK(!ZutcpLogger::enabled(), "compiled-out qlog enabled");
  PktEvt packet;
  ZutcpLogger::pktSent(trace, ZuMv(packet));
  ZutcpLogDiag diag = ZutcpLogger::diag();
  closeQLog(trace);
  ZutcpLogger::stop();
  ZutcpLogger::final(trace);
  ZuCHECK(!diag.recordsEnqueued && !diag.recordsWritten,
    "compiled-out qlog produced diagnostics");
#endif
}

void testQLogBackPressure()
{
  ZuTestScope(testQLogBackPressure);

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
    packet.packetType = PktType::Short;
    packet.packetSpace = PktNumSpace::AppData;
    packet.packetNumber = i;
    packet.packetSize = 1200;
    ZutcpLogger::pktRecv(trace, ZuMv(packet));
  }
  closeQLog(trace);
  ZutcpLogger::stop();
  ZutcpLogDiag diag = ZutcpLogger::diag();
  ZutcpLogger::final(trace);

  ZuCHECK(diag.recordsDropped || diag.ringBackPressure,
    "qlog tiny-ring event was not dropped");
#endif
}

void testQLogAppTrace()
{
  ZuTestScope(testQLogAppTrace);

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
    packet.packetType = PktType::Initial;
    packet.packetSpace = PktNumSpace::Initial;
    packet.packetSize = 1200;
    o.logPktSent(packet, time);
  });
  closeQLog(trace);
  ZutcpLogger::stop();
  ZutcpLogDiag diag = ZutcpLogger::diag();
  ZutcpLogger::final(trace);

  ZuCHECK(diag.recordsEnqueued >= 1, "app trace qlog enqueue mismatch");
  ZuCHECK(diag.recordsWritten >= 2, "app trace qlog write mismatch");
  ZuCHECK(diag.writerFailures == 0, "app trace qlog writer failure");

  ZtString<> data = readFile_(path);
  ZuCHECK(data, "app trace qlog output was not written");
  ZuCHECK(parseJSONSeq(data) >= 2, "app trace qlog JSON-SEQ parse failed");
  ZuCHECK(hasQLogVantage(data, "client"),
    "app trace qlog missing client vantage");
  ZuCHECK(hasPacketSent(data), "app trace qlog missing packet_sent");
#endif
}

void testQLogMultiTraceGroups()
{
  ZuTestScope(testQLogMultiTraceGroups);

#ifdef Zutcp_DEBUG
  Zi::Path path1 = testPath("ZutcpLogMultiTrace1.sqlog");
  Zi::Path path2 = testPath("ZutcpLogMultiTrace2.sqlog");
  ZiFile::remove(path1);
  ZiFile::remove(path2);

  auto id4 = [](uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    Zutcp::CxnID id;
    id.length(4);
    id[0] = a;
    id[1] = b;
    id[2] = c;
    id[3] = d;
    return id;
  };
  auto pkt = [](const Zutcp::CxnID &groupID, uint64_t pn) {
    PktEvt packet;
    packet.packetType = PktType::Short;
    packet.packetSpace = PktNumSpace::AppData;
    packet.packetNumber = pn;
    packet.packetSize = 1200;
    packet.linkInfo = Zutcp::LinkInfo{
      .vantage = Zutcp::Vantage::Client,
      .groupID = groupID,
      .dcid = groupID,
      .scid = groupID
    };
    return packet;
  };

  ZutcpLogParams params1;
  params1.enabled(true).path(path1).ringSize(1<<15);
  ZutcpLogParams params2;
  params2.enabled(true).path(path2).ringSize(1<<15);
  ZutcpLogger::Trace trace1;
  ZutcpLogger::Trace trace2;
  ZuCHECK(ZutcpLogger::init(trace1, params1, Zutcp::Vantage::Client),
    "multi trace 1 qlog init failed");
  ZuCHECK(ZutcpLogger::init(trace2, params2, Zutcp::Vantage::Server),
    "multi trace 2 qlog init failed");
  ZutcpLogger::start();
  ZutcpLogger::start();

  Zutcp::CxnID group1 = id4(0x10, 0x11, 0x12, 0x13);
  Zutcp::CxnID group2 = id4(0x20, 0x21, 0x22, 0x23);
  Zutcp::CxnID group3 = id4(0x30, 0x31, 0x32, 0x33);
  ZutcpLogger::pktSent(trace1, pkt(group1, 1));
  ZutcpLogger::pktSent(trace1, pkt(group2, 2));
  ZutcpLogger::pktSent(trace2, pkt(group3, 3));

  closeQLog(trace1);
  closeQLog(trace2);
  ZutcpLogger::stop();
  ZutcpLogDiag diag = ZutcpLogger::diag();
  ZutcpLogger::final(trace1);
  ZutcpLogger::final(trace2);

  ZuCHECK(diag.recordsEnqueued >= 3, "multi trace qlog enqueue mismatch");
  ZuCHECK(diag.recordsWritten >= 5, "multi trace qlog write mismatch");
  ZuCHECK(diag.writerFailures == 0, "multi trace qlog writer failure");

  ZtString<> data1 = readFile_(path1);
  ZtString<> data2 = readFile_(path2);
  ZuCHECK(data1, "multi trace 1 output was not written");
  ZuCHECK(data2, "multi trace 2 output was not written");
  ZuCHECK(parseJSONSeq(data1) >= 3,
    "multi trace 1 qlog JSON-SEQ parse failed");
  ZuCHECK(parseJSONSeq(data2) >= 2,
    "multi trace 2 qlog JSON-SEQ parse failed");
  ZuCHECK(hasQLogVantage(data1, "client"),
    "multi trace 1 missing client vantage");
  ZuCHECK(hasQLogVantage(data2, "server"),
    "multi trace 2 missing server vantage");
  ZuCHECK(data1.find<"10111213">() >= 0 &&
      data1.find<"20212223">() >= 0,
    "multi trace 1 missing per-link group IDs");
  ZuCHECK(data1.find<"30313233">() < 0,
    "multi trace 1 leaked trace 2 group ID");
  ZuCHECK(data2.find<"30313233">() >= 0,
    "multi trace 2 missing group ID");
  ZuCHECK(data2.find<"10111213">() < 0 &&
      data2.find<"20212223">() < 0,
    "multi trace 2 leaked trace 1 group IDs");
#endif
}

void testQLogTypedTransportEvents()
{
  ZuTestScope(testQLogTypedTransportEvents);

#ifdef Zutcp_DEBUG
  Zi::Path path = testPath("ZutcpLogTypedTest.sqlog");
  ZiFile::remove(path);

  ZutcpLogParams params;
  params.enabled(true).path(path).ringSize(1<<15);
  ZutcpLogger::Trace trace;
  ZuCHECK(ZutcpLogger::init(trace, params, Zutcp::Vantage::Unknown), "typed qlog init failed");
  ZutcpLogger::start();

  DgramEvt datagram;
  datagram.size = 1234;
  datagram.ecn = EcnMark::ECT0;
  ZutcpLogger::dgramRecv(trace, ZuMv(datagram));
  DgramEvt sentDatagram;
  sentDatagram.size = 1200;
  sentDatagram.ecn = EcnMark::N;
  ZutcpLogger::dgramSent(trace, ZuMv(sentDatagram));

  PktEvt packet;
  packet.packetType = PktType::Short;
  packet.packetSpace = PktNumSpace::AppData;
  packet.packetNumber = 42;
  packet.packetSize = 1200;
  packet.payloadSize = 1180;
  packet.ecn = EcnMark::ECT0;
  packet.bytesInFlight = 2400;
  packet.frameCount = 5;
  packet.ackEliciting = true;
  FrameEvt prefixData;
  prefixData.type = FrameType::Data;
  prefixData.offset = 0;
  prefixData.length = 64;
  packet.frames.push(prefixData);
  FrameEvt stream;
  stream.type = FrameType::Data;
  stream.offset = 128;
  stream.length = 32;
  stream.fin = true;
  packet.frames.push(stream);
  FrameEvt maxData;
  maxData.type = FrameType::MaxData;
  maxData.value = 16;
  packet.frames.push(maxData);
  FrameEvt blocked;
  blocked.type = FrameType::DataBlocked;
  blocked.value = 8;
  packet.frames.push(blocked);
  packet.frames.push(FrameEvt{.type = FrameType::Ping});
  ZutcpLogger::pktSent(trace, ZuMv(packet));

  PktEvt rx;
  rx.packetType = PktType::Short;
  rx.packetSpace = PktNumSpace::AppData;
  rx.packetNumber = 43;
  rx.packetSize = 1100;
  rx.payloadSize = 1080;
  rx.ecn = EcnMark::CE;
  rx.frameCount = 2;
  rx.ackEliciting = true;
  FrameEvt ack;
  ack.type = FrameType::Ack;
  ack.largestAcked = 40;
  ack.ackDelayUS = 25;
  ack.rangeCount = 2;
  new (ack.ackRanges.push()) ZutcpLog_::QAckRange{.first = 32, .largest = 35};
  new (ack.ackRanges.push()) ZutcpLog_::QAckRange{.first = 38, .largest = 40};
  ack.ect0 = 10;
  ack.ect1 = 2;
  ack.ce = 1;
  rx.frames.push(ack);
  FrameEvt reader;
  reader.type = FrameType::Data;
  reader.offset = 256;
  reader.length = 48;
  rx.frames.push(reader);
  ZutcpLogger::pktRecv(trace, ZuMv(rx));

  PktEvt rxControl;
  rxControl.packetType = PktType::Short;
  rxControl.packetSpace = PktNumSpace::AppData;
  rxControl.packetNumber = 44;
  rxControl.packetSize = 1000;
  rxControl.payloadSize = 980;
  rxControl.ecn = EcnMark::ECT1;
  rxControl.frameCount = FrameMax;
  rxControl.framesTruncated = true;

  FrameEvt reset;
  reset.type = FrameType::Reset;
  reset.errorCode = 99;
  reset.length = 4096;
  rxControl.frames.push(reset);

  FrameEvt stop;
  stop.type = FrameType::StopSending;
  stop.errorCode = 17;
  rxControl.frames.push(stop);

  FrameEvt maxCredit;
  maxCredit.type = FrameType::MaxData;
  maxCredit.value = 8192;
  rxControl.frames.push(maxCredit);

  FrameEvt close;
  close.type = FrameType::ConnectionClose;
  close.errorCode = 0x100;
  close.length = 12;
  rxControl.frames.push(close);

  ZuCHECK(rxControl.frames.length() == 4, "control frame setup incomplete");
  ZuCHECK(rxControl.frames[1].type == FrameType::StopSending,
    "control frame copy failed");
  PktEvt rxMove = ZuMv(rxControl);
  ZuCHECK(rxMove.frames.length() == 4, "control frame move length failed");
  ZuCHECK(rxMove.frames[1].type == FrameType::StopSending,
    "control frame move failed");
  ZutcpLogger::pktRecv(trace, ZuMv(rxMove));

  PktEvt buffered;
  buffered.packetType = PktType::Initial;
  buffered.packetSpace = PktNumSpace::Initial;
  buffered.packetSize = 1200;
  ZutcpLogger::pktBuf(trace, ZuMv(buffered));

  PktEvt drop;
  drop.packetType = PktType::Initial;
  drop.packetSpace = PktNumSpace::Initial;
  drop.packetSize = 50;
  drop.reason = Reason::ParseLong;
  ZutcpLogger::pktDrop(trace, ZuMv(drop));

  closeQLog(trace);
  ZutcpLogger::stop();
  ZutcpLogDiag diag = ZutcpLogger::diag();
  ZutcpLogger::final(trace);

  ZuCHECK(diag.recordsEnqueued >= 7, "typed qlog enqueue mismatch");
  ZuCHECK(diag.recordsWritten >= 8, "typed qlog write mismatch");
  ZuCHECK(diag.writerFailures == 0, "typed qlog writer failure");

  ZtString<> data = readFile_(path);
  ZuCHECK(data, "typed qlog output was not written");
  ZuCHECK(parseJSONSeq(data) >= 8, "typed qlog JSON-SEQ parse failed");
  ZuCHECK(hasDatagramReceived(data), "datagram event missing");
  ZuCHECK(hasDatagramSent(data), "datagram sent event missing");
  ZuCHECK(hasTypedDatagramFields(data), "typed datagram fields missing");
  ZuCHECK(hasTransportPacketSent(data), "packet_sent event missing");
  ZuCHECK(hasTransportPacketReceived(data), "packet_received event missing");
  ZuCHECK(hasTransportPacketBuffered(data),
    "packet_buffered event missing");
  ZuCHECK(hasTransportPacketDropped(data), "packet_dropped event missing");
  ZuCHECK(hasTypedPacketFields(data), "typed packet fields missing");
  ZuCHECK(data.find<"reset">() >= 0, "RESET_STREAM summary missing");
  ZuCHECK(data.find<"stop_sending">() >= 0, "STOP_SENDING summary missing");
  ZuCHECK(data.find<"max_data">() >= 0 && data.find<"data_blocked">() >= 0,
    "connection flow-control summaries missing");
  ZuCHECK(data.find<"stream_type">() < 0 && data.find<"stream_id">() < 0 &&
    data.find<"max_stream_data">() < 0 && data.find<"max_streams">() < 0 &&
    data.find<"streams_blocked">() < 0,
    "removed per-stream control fields leaked into packet logging");
  ZuCHECK(data.find<"new_connection_id">() < 0 &&
      data.find<"retire_connection_id">() < 0 &&
      data.find<"path_challenge">() < 0 && data.find<"path_response">() < 0 &&
      data.find<"stateless_reset_token">() < 0,
    "removed CID/path controls leaked into packet logging");
  ZuCHECK(data.find<"new_token">() < 0,
    "deleted NEW_TOKEN summary leaked");
  ZuCHECK(data.find<"handshake_done">() < 0,
    "deleted HANDSHAKE_DONE summary leaked");
  ZuCHECK(data.find<"connection_close">() >= 0,
    "CONNECTION_CLOSE summary missing");
  ZuCHECK(data.find<"error_code">() >= 0, "frame error code missing");
  ZuCHECK(data.find<"maximum">() >= 0, "frame maximum missing");
  ZuCHECK(data.find<"limit">() >= 0, "frame limit missing");
  ZuCHECK(data.find<"data">() >= 0, "data frame summary missing");
  ZuCHECK(data.find<"\"frame_type\":\"data\"">() >= 0, "connection data summary missing");
  ZuCHECK(data.find<"ack_delay">() >= 0, "ACK delay missing");
  ZuCHECK(data.find<"ack_delay_us">() < 0, "old ACK delay field leaked");
  ZuCHECK(data.find<"acked_ranges">() >= 0, "ACK ranges missing");
  ZuCHECK(data.find<"\"acked_ranges\":[]">() < 0,
    "empty ACK ranges leaked");
	  ZuCHECK(data.find<"[[32,35],[38,40]]">() >= 0,
	    "ACK range encoding missing");
	  ZuCHECK(data.find<"packet_space">() < 0,
	    "old packet_space field leaked");
	  ZuCHECK(data.find<"ect0">() >= 0, "ACK_ECN ECT0 missing");
	  ZuCHECK(data.find<"ce">() >= 0, "ACK_ECN CE missing");
	  ZuCHECK(data.find<"\"trigger\":\"invalid\"">() >= 0,
	    "drop trigger missing");
	  ZuCHECK(data.find<"\"reason\"">() < 0,
	    "old packet reason field leaked");
	  ZuCHECK(data.find<"coalescing">() < 0,
	    "private coalescing reason leaked into packet event");
	  ZuCHECK(data.find<"parse_long">() < 0,
	    "private parse_long reason leaked into packet event");
#endif
}

void testQLogTypedRecoveryEvents()
{
  ZuTestScope(testQLogTypedRecoveryEvents);

#ifdef Zutcp_DEBUG
  Zi::Path path = testPath("ZutcpLogRecoveryTest.sqlog");
  ZiFile::remove(path);

  ZutcpLogParams params;
  params.enabled(true).path(path).ringSize(1<<18);
  ZutcpLogger::Trace trace;
  ZuCHECK(ZutcpLogger::init(trace, params, Zutcp::Vantage::Unknown), "recovery qlog init failed");
  ZutcpLogger::start();

  AckEvt ack;
  ack.packetSpace = PktNumSpace::AppData;
  ack.largestAcked = 99;
  ack.ackDelayUS = 2500;
  ack.ackedBytes = 3600;
  ack.lostBytes = 0;
  ack.rangeCount = 2;
  ack.ackedFrames = 3;
  new (ack.packetNumbers.push()) uint64_t(97);
  new (ack.packetNumbers.push()) uint64_t(98);
  new (ack.packetNumbers.push()) uint64_t(99);
  ZutcpLogger::pktsAcked(trace, ZuMv(ack));

  RecLogEvt loss{
    .packetNumber = 77,
    .bytes = 1200,
    .bytesInFlight = 2400,
    .kind = RecKind::Aggregate,
    .packetSpace = PktNumSpace::AppData,
    .reason = RecReason::PacketThreshold,
    .frameCount = 1
  };
  FrameEvt lostData{
    .offset = 8,
    .length = 12,
    .type = FrameType::Data
  };
  loss.frames.push(lostData);
  ZutcpLogger::pktLost(trace, loss);
  ZutcpLogger::recPktLost(trace, loss);
  ZutcpLogger::markRetrans(trace, loss);

  RecLogEvt metrics{
    .latestRTTUS = 18000,
    .smoothedRTTUS = 20000,
    .rttVarianceUS = 3000,
    .minRTTUS = 16000,
    .cwnd = 12000,
    .ssthresh = 64000,
    .bytesInFlight = 3600,
    .kind = RecKind::RTT,
    .packetSpace = PktNumSpace::AppData
  };
  ZutcpLogger::metricsUpd(trace, ZuMv(metrics));

	  RecLogEvt timer{
	    .deadlineUS = 123456,
	    .kind = RecKind::PTO,
	    .packetSpace = PktNumSpace::Initial,
	    .reason = RecReason::Armed
	  };
	  ZutcpLogger::lossTimerUpd(trace, ZuMv(timer));

	  RecLogEvt lossTimer{
	    .deadlineUS = 123456,
	    .kind = RecKind::Loss,
	    .packetSpace = PktNumSpace::AppData,
	    .reason = RecReason::Armed
	  };
	  ZutcpLogger::lossTimerUpd(trace, ZuMv(lossTimer));

	  RecLogEvt ptoExpired{
	    .value = 2,
	    .bytesInFlight = 2400,
	    .kind = RecKind::PTO,
	    .packetSpace = PktNumSpace::AppData,
	    .reason = RecReason::Expired
	  };
	  ZutcpLogger::lossTimerUpd(trace, ZuMv(ptoExpired));

	  RecLogEvt ptoBackoff{
	    .value = 3,
	    .bytes = 2,
	    .bytesInFlight = 2400,
	    .kind = RecKind::PTO,
	    .packetSpace = PktNumSpace::AppData,
	    .reason = RecReason::Backoff
	  };
	  ZutcpLogger::lossTimerUpd(trace, ZuMv(ptoBackoff));

	  RecLogEvt ptoProbe{
	    .value = 3,
	    .bytes = 2,
	    .kind = RecKind::PTO,
	    .packetSpace = PktNumSpace::AppData,
	    .reason = RecReason::Probe
	  };
	  ZutcpLogger::lossTimerUpd(trace, ZuMv(ptoProbe));

  RecLogEvt congestion{
    .cwnd = 13200,
    .ssthresh = 64000,
    .bytesInFlight = 1200,
    .kind = RecKind::NewReno,
    .packetSpace = PktNumSpace::AppData,
    .reason = RecReason::Ack
  };
  ZutcpLogger::congStateUpd(trace, ZuMv(congestion));

  ECNEvt ecnTesting{
    .packetSpace = PktNumSpace::AppData,
    .oldState = ECNState::Unknown,
    .state = ECNState::Testing,
    .reason = ECNReason::Probe
  };
  ZutcpLogger::ecnStateUpd(trace, ZuMv(ecnTesting));

  ECNEvt ecn{
    .ect0 = 7,
    .ect1 = 1,
    .ce = 2,
    .previousECT0 = 6,
    .previousECT1 = 1,
    .previousCE = 1,
    .largestAcked = 12,
    .packetSpace = PktNumSpace::AppData,
    .oldState = ECNState::Testing,
    .state = ECNState::Capable,
    .reason = ECNReason::AckECN
  };
  ZutcpLogger::ecnStateUpd(trace, ZuMv(ecn));

  ECNEvt fallback{
    .ect0 = 20,
    .ce = 1,
    .previousECT0 = 7,
    .previousECT1 = 1,
    .previousCE = 2,
    .largestAcked = 12,
    .packetSpace = PktNumSpace::AppData,
    .oldState = ECNState::Capable,
    .state = ECNState::Failed,
    .reason = ECNReason::CounterExceedsAck,
    .disabled = true
  };
  ZutcpLogger::ecnStateUpd(trace, ZuMv(fallback));

  closeQLog(trace);
  ZutcpLogger::stop();
  ZutcpLogDiag diag = ZutcpLogger::diag();
  ZutcpLogger::final(trace);

	  ZuCHECK(diag.recordsEnqueued >= 12, "recovery qlog enqueue mismatch");
	  ZuCHECK(diag.recordsWritten >= 13, "recovery qlog write mismatch");
  ZuCHECK(diag.writerFailures == 0, "recovery qlog writer failure");

  ZtString<> data = readFile_(path);
  ZuCHECK(data, "recovery qlog output was not written");
	  ZuCHECK(parseJSONSeq(data) >= 13, "recovery qlog JSON-SEQ parse failed");
	  ZuCHECK(hasRecoveryEvents(data), "recovery event coverage missing");
	  ZuCHECK(hasRecoveryFields(data), "recovery fields missing");
	  ZuCHECK(data.find<"smoothed_rtt_us">() < 0,
	    "old recovery metric smoothed_rtt_us emitted");
	  ZuCHECK(data.find<"rtt_variance_us">() < 0,
	    "old recovery metric rtt_variance_us emitted");
	  ZuCHECK(data.find<"cwnd">() < 0, "old recovery metric cwnd emitted");
	  ZuCHECK(data.find<"\"packet_type\":\"short\"">() >= 0,
	    "packet_lost packet type missing");
	  ZuCHECK(data.find<"\"packet_number\":77">() >= 0,
	    "packet_lost packet number missing");
	  ZuCHECK(data.find<"\"trigger\":\"reordering_threshold\"">() >= 0,
	    "packet_lost trigger missing");
	  ZuCHECK(data.find<"\"timer_type\":\"pto\"">() >= 0,
	    "PTO timer type missing");
	  ZuCHECK(data.find<"\"timer_type\":\"loss_timeout\"">() >= 0,
	    "loss timer type missing");
	  ZuCHECK(data.find<"\"event_type\":\"set\"">() >= 0,
	    "timer set event missing");
	  ZuCHECK(data.find<"\"event_type\":\"expired\"">() >= 0,
	    "timer expired event missing");
	  ZuCHECK(data.find<"\"new\":\"capable\"">() >= 0,
	    "ECN capable state missing");
	  ZuCHECK(data.find<"\"new\":\"failed\"">() >= 0,
	    "ECN failed state missing");
	  ZuCHECK(data.find<"counter_exceeds_ack">() < 0,
	    "ECN private reason leaked into standard event");
	  ZuCHECK(data.find<"\"name\":\"quic:congestion_state_updated\","
	    "\"data\":{\"kind\"">() < 0,
	    "generic recovery fields leaked into congestion event");
	  ZuCHECK(data.find<"\"name\":\"quic:marked_for_retransmit\","
	    "\"data\":{\"kind\"">() < 0,
	    "generic recovery fields leaked into retransmit event");
#endif
}

void testQLogTypedSetupEvents()
{
  ZuTestScope(testQLogTypedSetupEvents);
#ifdef Zutcp_DEBUG
  Zi::Path path = testPath("ZutcpLogSetupTest.sqlog");
  ZutcpLogParams params;
  params.enabled(true).path(path).ringSize(1<<15);
  ZutcpLogger::Trace trace;
  ZuCHECK(ZutcpLogger::init(trace, params, Zutcp::Vantage::Client),
    "setup qlog init failed");
  ZutcpLogger::start();
  ParamsEvt parameters{
    .maxIdleTimeout = 42,
    .maxPayloadSize = 1350,
    .ackDelayExponent = 3,
    .maxAckDelay = 25,
    .initialMaxData = 65536,
    .initiator = Initiator::Remote
  };
  ZutcpLogger::paramsSet(trace, ZuMv(parameters));
  VersionEvt version;
  version.serverVersions.push(Version1);
  version.clientVersions.push(0x1abcdef0);
  ZutcpLogger::versionInfo(trace, ZuMv(version));
  closeQLog(trace);
  ZutcpLogger::stop();
  auto diag = ZutcpLogger::diag();
  ZutcpLogger::final(trace);
  auto data = readFile_(path);
  ZuCHECK(diag.recordsEnqueued >= 2 && diag.recordsWritten == 3 &&
      !diag.writerFailures && parseJSONSeq(data) == 3,
    "setup qlog must write two complete event records");
  ZuCHECK(data.find<"quic:parameters_set">() >= 0 &&
      data.find<"\"initial_max_data\":65536">() >= 0 &&
      data.find<"\"initiator\":\"remote\"">() >= 0 &&
      data.find<"initial_max_stream">() < 0,
    "setup parameters must retain connection credit without stream settings");
  ZuCHECK(data.find<"quic:version_information">() >= 0 &&
      data.find<"\"server_versions\":[\"00000001\"]">() >= 0 &&
      data.find<"\"client_versions\":[\"1abcdef0\"]">() >= 0,
    "version information must preserve the independent wire version lists");
#endif
  }

void testQLogTypedPathCIDEvents()
{
  ZuTestScope(testQLogTypedPathCIDEvents);
#ifdef Zutcp_DEBUG
  Zi::Path path = testPath("ZutcpLogPathCIDTest.sqlog");
  ZiFile::remove(path);
  ZutcpLogParams params;
  params.enabled(true).path(path).ringSize(1<<15);
  ZutcpLogger::Trace trace;
  ZuCHECK(ZutcpLogger::init(trace, params, Zutcp::Vantage::Unknown), "path qlog init failed");
  ZutcpLogger::start();
  ZutcpLogger::pathUpdated(trace, PathEvt{.tupleID = 7});
  ZutcpLogger::pmtudUpdated(trace, PathEvt{
    .mtu = 1400, .kind = PathKind::PMTUD, .action = PathAction::Sent});
  CIDEvt cid{
    .cxnID = CxnID{"cidpath1"}, .action = CIDAction::RouteBound,
    .reason = CIDReason::RouteInstall, .length = 8, .local = true, .associated = true};
  ZutcpLogger::cidUpdated(trace, cid);
  cid.action = CIDAction::Tombstone;
  cid.reason = CIDReason::RouteTombstone;
  cid.associated = false;
  ZutcpLogger::cidUpdated(trace, cid);
  closeQLog(trace);
  ZutcpLogger::stop();
  auto diag = ZutcpLogger::diag();
  ZutcpLogger::final(trace);
  auto data = readFile_(path);
  ZuCHECK(!diag.writerFailures && parseJSONSeq(data) >= 5,
    "bootstrap tuple/CID and MTU records were not written");
  ZuCHECK(data.find<"quic:tuple_assigned">() >= 0 &&
      data.find<"quic:mtu_updated">() >= 0 &&
      data.find<"quic:connection_id_updated">() >= 0 &&
      data.find<"\"new\":1400">() >= 0 &&
      data.find<"\"old\":\"6369647061746831\"">() >= 0 &&
      data.find<"\"new\":\"6369647061746831\"">() >= 0,
    "bootstrap CID install/tombstone or MTU fields missing");
  ZuCHECK(data.find<"attempt_id">() < 0 && data.find<"migration">() < 0 &&
      data.find<"path_validated">() < 0 && data.find<"reset_token">() < 0,
    "deleted migration metadata leaked into logging");
#endif
}

void testQLogBlockedEvents()
{
  ZuTestScope(testQLogBlockedEvents);
#ifdef Zutcp_DEBUG
  Zi::Path path = testPath("ZutcpLogBlockedTest.sqlog");
  ZiFile::remove(path);
  ZutcpLogParams params;
  params.enabled(true).path(path).ringSize(1<<15);
  ZutcpLogger::Trace trace;
  ZuCHECK(ZutcpLogger::init(trace, params, Zutcp::Vantage::Unknown),
    "blocked qlog init failed");
  ZutcpLogger::start();
  ZutcpLogger::cxnDataBlockedUpd(trace, BlockedEvt{
    .oldState = BlockedState::Unblocked,
    .newState = BlockedState::Blocked,
    .reason = BlockedReason::CxnFlowCtrl});
  ZutcpLogger::cxnDataBlockedUpd(trace, BlockedEvt{
    .oldState = BlockedState::Blocked,
    .newState = BlockedState::Unblocked,
    .reason = BlockedReason::CxnFlowCtrl});
  closeQLog(trace);
  ZutcpLogger::stop();
  auto diag = ZutcpLogger::diag();
  ZutcpLogger::final(trace);
  auto data = readFile_(path);
  ZuCHECK(diag.recordsEnqueued >= 2 && diag.recordsWritten >= 3 &&
    !diag.writerFailures && parseJSONSeq(data) >= 3,
    "connection blocking transitions must drain as valid JSON-SEQ");
  ZuCHECK(data.find<"connection_data_blocked_updated">() >= 0 &&
    data.find<"connection_flow_control">() >= 0 &&
    data.find<"\"new\":\"blocked\"">() >= 0 &&
    data.find<"\"new\":\"unblocked\"">() >= 0,
    "connection blocked/unblocked transitions missing");
  ZuCHECK(data.find<"stream_id">() < 0 && data.find<"stream_type">() < 0 &&
    data.find<"stream_state_updated">() < 0 && data.find<"stream_data_moved">() < 0,
    "removed stream events or identity must not appear in connection logging");
#endif
}



void testQLogTypedCloseEvents()
{
  ZuTestScope(testQLogTypedCloseEvents);

#ifdef Zutcp_DEBUG
  Zi::Path path = testPath("ZutcpLogCloseTest.sqlog");
  ZiFile::remove(path);

  ZutcpLogParams params;
  params.enabled(true).path(path).ringSize(1<<15);
  ZutcpLogger::Trace trace;
  ZuCHECK(ZutcpLogger::init(trace, params, Zutcp::Vantage::Unknown), "close qlog init failed");
  ZutcpLogger::start();

  CloseEvt local{
    .errorCode = 42,
    .initiator = CloseInitiator::Local,
    .trigger = CloseTrigger::Application,
    .reason = CloseReason::LocalClose,
    .applicationError = CloseError::Unknown
  };
  ZutcpLogger::cxnClosed(trace, ZuMv(local));

  CloseEvt idle{
    .errorCode = 0,
    .initiator = CloseInitiator::Local,
    .trigger = CloseTrigger::IdleTimeout,
    .reason = CloseReason::Idle,
    .connectionError = CloseError::NoError
  };
  ZutcpLogger::cxnClosed(trace, ZuMv(idle));

  CloseEvt frameEncoding{
    .errorCode = TransportError::FrameEncoding,
    .initiator = CloseInitiator::Remote,
    .trigger = CloseTrigger::Error,
    .reason = CloseReason::PeerCloseFrame,
    .connectionError = CloseError::Unknown
  };
  ZutcpLogger::cxnClosed(trace, ZuMv(frameEncoding));

  closeQLog(trace);
  ZutcpLogger::stop();
  ZutcpLogDiag diag = ZutcpLogger::diag();
  ZutcpLogger::final(trace);

  ZuCHECK(diag.recordsEnqueued >= 3, "close qlog enqueue mismatch");
  ZuCHECK(diag.recordsWritten >= 4, "close qlog write mismatch");
  ZuCHECK(diag.writerFailures == 0, "close qlog writer failure");

  ZtString<> data = readFile_(path);
  ZuCHECK(data, "close qlog output was not written");
  ZuCHECK(parseJSONSeq(data) >= 4, "close qlog JSON-SEQ parse failed");
  ZuCHECK(hasCloseEvents(data), "close event coverage missing");
  ZuCHECK(hasCloseFields(data), "close fields missing");
  ZuCHECK(data.find<"local_close">() >= 0, "local close reason missing");
  ZuCHECK(data.find<"idle_timeout">() >= 0, "idle close trigger missing");
  ZuCHECK(data.find<"frame_encoding_error">() >= 0,
    "known transport close error missing");
  ZuCHECK(data.find<"\"connection_error\":\"unknown\"">() < 0, "known transport close error emitted as unknown");
  ZuCHECK(data.find<"\"error_code\":0">() < 0,
    "close event emitted default error_code");
  ZuCHECK(data.find<"\"reason\":\"\"">() < 0,
    "close event emitted empty reason");
  ZuCHECK(data.find<"\"application\":">() < 0,
    "close event leaked local application flag");
  ZuCHECK(data.find<"\"frame\":">() < 0,
    "close event leaked local frame flag");
#endif
}

void testQLogRestartDrain()
{
  ZuTestScope(testQLogRestartDrain);
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
  closeQLog(first);
  ZuCheck(ZutcpLogger::stop());
  ZuCheck(ZutcpLogger::stop());
  ZuCheck(delivered == 1);
  ZutcpLogger::final(first);
  ZutcpLogger::final(next);
  ZutcpLogger::final(sibling);
  ZuCheck(!ZutcpLogger::enabled());
#endif
}

void testQLogStopDrain()
{
  ZuTestScope(testQLogStopDrain);
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
  g_residue = ZiTestResidue::dir("qlog");
#endif
  ZuTestMain();
  ZuTestCall(testQLogRestartDrain);
  ZuTestCall(testQLogStopDrain);
  ZuTestCall(testQLogFileOutput);
  ZuTestCall(testQLogBackPressure);
  ZuTestCall(testQLogAppTrace);
  ZuTestCall(testQLogMultiTraceGroups);
  ZuTestCall(testQLogTypedTransportEvents);
  ZuTestCall(testQLogTypedRecoveryEvents);
  ZuTestCall(testQLogTypedSetupEvents);
  ZuTestCall(testQLogTypedPathCIDEvents);
  ZuTestCall(testQLogBlockedEvents);
  ZuTestCall(testQLogTypedCloseEvents);
}
