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
using namespace Zquic;
using namespace ZquicLog_;

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

static void removeTestLog_(const Zi::Path &path)
{
  if (!::getenv("ZQUIC_TEST_KEEP")) ZiFile::remove(path);
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
    if (scan.p<0>() != int(json.length())) return 0;
    ++n;
    ++i;
  }
  return n;
}

static bool containsQLogFileSchema_(ZuCSpan data)
{
  return
    data.find<"file_schema">() >= 0 &&
    data.find<"urn:ietf:params:qlog:file:sequential">() >= 0 &&
    data.find<"serialization_format">() >= 0 &&
    data.find<"application/qlog+json-seq">() >= 0;
}

static bool containsQLogHeaderMetadata_(ZuCSpan data)
{
  return
    data.find<"vantage_point">() >= 0 &&
    data.find<"event_schemas">() >= 0 &&
    data.find<"urn:ietf:params:qlog:events:quic-12">() >= 0 &&
    data.find<"urn:zlib:zquic:qlog:events:zquic">() >= 0 &&
    data.find<"time_format">() >= 0 &&
    data.find<"relative_to_epoch">() >= 0 &&
    data.find<"reference_time">() >= 0 &&
    data.find<"implementation">() >= 0 &&
    data.find<"version">() >= 0 &&
    data.find<"zquic">() >= 0;
}

static bool containsQLogVantage_(ZuCSpan data, ZuCSpan vantage)
{
  ZtString<> s;
  s << "\"type\":\"" << vantage << '"';
  return data.find(s) >= 0;
}

static bool containsQLogConnectionMetadata_(ZuCSpan data)
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

static bool containsPacketSent_(ZuCSpan data)
{
  return data.find<"packet_sent">() >= 0;
}

static bool containsTransportPacketSent_(ZuCSpan data)
{
  return data.find<"quic:packet_sent">() >= 0;
}

static bool containsTransportPacketReceived_(ZuCSpan data)
{
  return data.find<"quic:packet_received">() >= 0;
}

static bool containsTransportPacketBuffered_(ZuCSpan data)
{
  return data.find<"quic:packet_buffered">() >= 0;
}

static bool containsTransportPacketDropped_(ZuCSpan data)
{
  return data.find<"quic:packet_dropped">() >= 0;
}

static bool containsDatagramReceived_(ZuCSpan data)
{
  return data.find<"quic:udp_datagrams_received">() >= 0;
}

static bool containsDatagramSent_(ZuCSpan data)
{
  return data.find<"quic:udp_datagrams_sent">() >= 0;
}

static bool containsTypedPacketFields_(ZuCSpan data)
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

static bool containsTypedDatagramFields_(ZuCSpan data)
{
  return
    data.find<"\"count\":1">() >= 0 &&
    data.find<"\"raw\":[{">() >= 0 &&
    data.find<"\"payload_length\":1234">() >= 0 &&
    data.find<"\"payload_length\":1200">() >= 0 &&
    data.find<"\"ecn\":[\"ECT0\"">() >= 0;
}

static bool containsRecoveryEvents_(ZuCSpan data)
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

static bool containsRecoveryFields_(ZuCSpan data)
{
  return
    data.find<"packet_number_space">() >= 0 &&
    data.find<"packet_numbers">() >= 0 &&
    data.find<"\"packet_numbers\":[97,98,99]">() >= 0 &&
    data.find<"\"name\":\"quic:packet_lost\",\"data\":{\"header\":{">() >= 0 &&
    data.find<"\"trigger\":\"reordering_threshold\"">() >= 0 &&
    data.find<"\"name\":\"quic:marked_for_retransmit\"">() >= 0 &&
    data.find<"\"frame_type\":\"crypto\"">() >= 0 &&
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
      "\"unknown\",\"new\":\"capable\"">() >= 0 &&
    data.find<"\"name\":\"quic:ecn_state_updated\",\"data\":{\"old\":"
      "\"unknown\",\"new\":\"failed\"">() >= 0;
}

static bool containsSecEvents_(ZuCSpan data)
{
  return
    data.find<"quic:key_updated">() >= 0 &&
    data.find<"quic:key_discarded">() >= 0 &&
    data.find<"quic:parameters_set">() >= 0 &&
    data.find<"quic:alpn_information">() >= 0 &&
    data.find<"zquic:tls_alert">() >= 0 &&
    data.find<"zquic:retry_sent">() >= 0 &&
    data.find<"zquic:retry_validated">() >= 0 &&
    data.find<"zquic:token_issued">() >= 0 &&
    data.find<"zquic:token_validated">() >= 0 &&
    data.find<"zquic:token_rejected">() >= 0 &&
    data.find<"quic:version_information">() >= 0 &&
    data.find<"zquic:stateless_reset">() >= 0 &&
    data.find<"zquic:packet_protection_failed">() >= 0;
}

static bool containsSecFields_(ZuCSpan data)
{
	  return
	    data.find<"key_type">() >= 0 &&
	    data.find<"packet_number_space">() >= 0 &&
	    data.find<"trigger">() >= 0 &&
	    data.find<"alpn">() >= 0 &&
	    data.find<"success">() >= 0;
}

static bool containsPathCIDEvents_(ZuCSpan data)
{
  return
    data.find<"quic:tuple_assigned">() >= 0 &&
    data.find<"quic:path_validated">() >= 0 &&
    data.find<"quic:mtu_updated">() >= 0 &&
    data.find<"quic:connection_id_updated">() >= 0;
}

static bool containsPathCIDFields_(ZuCSpan data)
{
  return
    data.find<"tuple_id">() >= 0 &&
    data.find<"success">() >= 0 &&
    data.find<"vantage">() >= 0 &&
    data.find<"\"success\":false">() >= 0 &&
    data.find<"initiator">() >= 0 &&
    data.find<"new">() >= 0;
}

static bool containsStreamEvents_(ZuCSpan data)
{
  return data.find<"quic:stream_state_updated">() >= 0 &&
    data.find<"quic:stream_data_moved">() >= 0 &&
    data.find<"quic:connection_data_blocked_updated">() >= 0 &&
    data.find<"quic:stream_data_blocked_updated">() >= 0;
}

static bool containsStreamFields_(ZuCSpan data)
{
  return
    data.find<"stream_id">() >= 0 &&
    data.find<"stream_type">() >= 0 &&
    data.find<"old">() >= 0 &&
    data.find<"new">() >= 0 &&
    data.find<"stream_side">() >= 0 &&
    data.find<"from">() >= 0 &&
    data.find<"to">() >= 0 &&
    data.find<"raw">() >= 0 &&
    data.find<"length">() >= 0 &&
    data.find<"connection_flow_control">() >= 0 &&
    data.find<"stream_flow_control">() >= 0;
}

static bool containsCloseEvents_(ZuCSpan data)
{
  return data.find<"quic:connection_closed">() >= 0;
}

static bool containsCloseFields_(ZuCSpan data)
{
  return
    data.find<"initiator">() >= 0 &&
    data.find<"trigger">() >= 0 &&
    data.find<"connection_error">() >= 0 &&
    data.find<"application_error">() >= 0 &&
    data.find<"error_code">() >= 0;
}

static bool containsZiLogPrefix_(ZuCSpan data)
{
  return data.find<"[Zquic]">() >= 0;
}

static void closeQLog_(ZquicLogger::Trace &trace)
{
  ZmSemaphore done;
  ZquicLogger::close(trace, [&done]() { done.post(); });
  done.wait();
}

void testQLogFileOutput()
{
  ZuTestScope(testQLogFileOutput);

#ifdef Zquic_DEBUG
  Zi::Path path = testPath_("ZquicLogTest.sqlog");
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

  ZquicLogParams params;
  params.enabled(true).path(path).thread("zquic-qlog-test").ringSize(1<<15).
    age(2);
  ZquicLogger::Trace trace;
  ZuCHECK(ZquicLogger::init(trace, params, Zquic::Vantage::Unknown), "qlog init failed");
  auto id4 = [](uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    Zquic::CxnID id;
    id.length(4);
    id[0] = a;
    id[1] = b;
    id[2] = c;
    id[3] = d;
    return id;
  };
  Zquic::CxnID odcid = id4(0xde, 0xad, 0xbe, 0xef);
  Zquic::CxnID groupID = id4(0x01, 0x02, 0x03, 0x04);
  Zquic::CxnID dcid = id4(0x11, 0x12, 0x13, 0x14);
  Zquic::CxnID scid = id4(0x21, 0x22, 0x23, 0x24);
  ZquicLogger::start();
  ZuCHECK(ZquicLogger::enabled(), "qlog did not enable");
  PktEvent packet;
  packet.packetType = PktType::Initial;
  packet.packetSpace = PktNumSpace::Initial;
  packet.packetSize = 1200;
  packet.linkInfo = Zquic::LinkInfo{
    .vantage = Zquic::Vantage::Client,
    .origDCID = odcid,
    .groupID = groupID,
    .dcid = dcid,
    .scid = scid
  };
  ZquicLogger::pktSent(trace, ZuMv(packet));
  Zquic::CxnID odcid2 = id4(0xca, 0xfe, 0xba, 0xbe);
  Zquic::CxnID groupID2 = id4(0x05, 0x06, 0x07, 0x08);
  Zquic::CxnID dcid2 = id4(0x31, 0x32, 0x33, 0x34);
  Zquic::CxnID scid2 = id4(0x41, 0x42, 0x43, 0x44);
  ZquicLogger::cxnStarted(trace, CxnStartedEvent{
    .local = ZiSockAddr{ZiIP{"127.0.0.1"}, 4443},
    .remote = ZiSockAddr{ZiIP{"127.0.0.1"}, 5555},
    .linkInfo = Zquic::LinkInfo{
      .vantage = Zquic::Vantage::Server,
      .origDCID = odcid2,
      .groupID = groupID2,
      .dcid = dcid2,
      .scid = scid2
    }
  });
  closeQLog_(trace);
  ZquicLogger::stop();
  ZquicLogDiag diag = ZquicLogger::diag();
  ZquicLogger::final(trace);

  ZuCHECK(diag.recordsEnqueued >= 2, "qlog enqueue diagnostics mismatch");
  ZuCHECK(diag.recordsWritten >= 3, "qlog write diagnostics mismatch");
  ZuCHECK(diag.writerFailures == 0, "qlog writer failure");

  ZtString<> data = readFile_(path);
  ZuCHECK(data, "qlog output was not written");
  ZuCHECK(parseJSONSeq_(data) >= 3, "qlog JSON-SEQ parse failed");
  ZuCHECK(containsQLogFileSchema_(data), "qlog header missing file schema");
  ZuCHECK(containsQLogHeaderMetadata_(data), "qlog header linkInfo missing");
  ZuCHECK(containsQLogVantage_(data, "unknown"),
    "qlog header should not assume a process-wide vantage point");
  ZuCHECK(containsQLogConnectionMetadata_(data),
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
  ZuCHECK(containsPacketSent_(data), "qlog event missing packet_sent");
  ZuCHECK(!containsZiLogPrefix_(data), "qlog contains ZiLog text prefix");

  ZtString<> aged = readFile_(agedPath);
  ZuCHECK(aged.find<"old-qlog">() >= 0, "qlog output was not aged");
  removeTestLog_(path);
  removeTestLog_(agedPath);
#else
  ZquicLogParams params;
  params.enabled(true).path("ZquicLogTest.sqlog");
  ZquicLogger::Trace trace;
  ZuCHECK(ZquicLogger::init(trace, params, Zquic::Vantage::Unknown), "compiled-out qlog init failed");
  ZquicLogger::start();
  ZuCHECK(!ZquicLogger::enabled(), "compiled-out qlog enabled");
  PktEvent packet;
  ZquicLogger::pktSent(trace, ZuMv(packet));
  ZquicLogDiag diag = ZquicLogger::diag();
  closeQLog_(trace);
  ZquicLogger::stop();
  ZquicLogger::final(trace);
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
  ZquicLogger::Trace trace;
  ZuCHECK(ZquicLogger::init(trace, params, Zquic::Vantage::Unknown), "qlog drop init failed");
  ZquicLogger::start();
  for (unsigned i = 0; i < 10000; ++i) {
    PktEvent packet;
    packet.packetType = PktType::Short;
    packet.packetSpace = PktNumSpace::AppData;
    packet.packetNumber = i;
    packet.packetSize = 1200;
    ZquicLogger::pktRecv(trace, ZuMv(packet));
  }
  closeQLog_(trace);
  ZquicLogger::stop();
  ZquicLogDiag diag = ZquicLogger::diag();
  ZquicLogger::final(trace);

  ZuCHECK(diag.recordsDropped || diag.ringBackPressure,
    "qlog tiny-ring event was not dropped");
  removeTestLog_(path);
#endif
}

void testQLogAppTrace()
{
  ZuTestScope(testQLogAppTrace);

#ifdef Zquic_DEBUG
  Zi::Path path = testPath_("ZquicLogAppTraceTest.sqlog");
  ZiFile::remove(path);

  ZquicLogParams params;
  params.enabled(true).path(path).thread("zquic-qlog-app").ringSize(1<<15);
  ZquicLogger::Trace trace;
  ZuCHECK(ZquicLogger::init(trace, params, Zquic::Vantage::Client),
    "app trace qlog init failed");
  ZquicLogger::start();
  ZquicLogger::log(trace, [](auto &o, ZuTime time) {
    PktEvent packet;
    packet.packetType = PktType::Initial;
    packet.packetSpace = PktNumSpace::Initial;
    packet.packetSize = 1200;
    o.logPktSent(packet, time);
  });
  closeQLog_(trace);
  ZquicLogger::stop();
  ZquicLogDiag diag = ZquicLogger::diag();
  ZquicLogger::final(trace);

  ZuCHECK(diag.recordsEnqueued >= 1, "app trace qlog enqueue mismatch");
  ZuCHECK(diag.recordsWritten >= 2, "app trace qlog write mismatch");
  ZuCHECK(diag.writerFailures == 0, "app trace qlog writer failure");

  ZtString<> data = readFile_(path);
  ZuCHECK(data, "app trace qlog output was not written");
  ZuCHECK(parseJSONSeq_(data) >= 2, "app trace qlog JSON-SEQ parse failed");
  ZuCHECK(containsQLogVantage_(data, "client"),
    "app trace qlog missing client vantage");
  ZuCHECK(containsPacketSent_(data), "app trace qlog missing packet_sent");
  removeTestLog_(path);
#endif
}

void testQLogMultiTraceGroups()
{
  ZuTestScope(testQLogMultiTraceGroups);

#ifdef Zquic_DEBUG
  Zi::Path path1 = testPath_("ZquicLogMultiTrace1.sqlog");
  Zi::Path path2 = testPath_("ZquicLogMultiTrace2.sqlog");
  ZiFile::remove(path1);
  ZiFile::remove(path2);

  auto id4 = [](uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    Zquic::CxnID id;
    id.length(4);
    id[0] = a;
    id[1] = b;
    id[2] = c;
    id[3] = d;
    return id;
  };
  auto pkt = [](const Zquic::CxnID &groupID, uint64_t pn) {
    PktEvent packet;
    packet.packetType = PktType::Short;
    packet.packetSpace = PktNumSpace::AppData;
    packet.packetNumber = pn;
    packet.packetSize = 1200;
    packet.linkInfo = Zquic::LinkInfo{
      .vantage = Zquic::Vantage::Client,
      .groupID = groupID,
      .dcid = groupID,
      .scid = groupID
    };
    return packet;
  };

  ZquicLogParams params1;
  params1.enabled(true).path(path1).thread("zquic-qlog-multi").ringSize(1<<15);
  ZquicLogParams params2;
  params2.enabled(true).path(path2).thread("zquic-qlog-multi").ringSize(1<<15);
  ZquicLogger::Trace trace1;
  ZquicLogger::Trace trace2;
  ZuCHECK(ZquicLogger::init(trace1, params1, Zquic::Vantage::Client),
    "multi trace 1 qlog init failed");
  ZuCHECK(ZquicLogger::init(trace2, params2, Zquic::Vantage::Server),
    "multi trace 2 qlog init failed");
  ZquicLogger::start();
  ZquicLogger::start();

  Zquic::CxnID group1 = id4(0x10, 0x11, 0x12, 0x13);
  Zquic::CxnID group2 = id4(0x20, 0x21, 0x22, 0x23);
  Zquic::CxnID group3 = id4(0x30, 0x31, 0x32, 0x33);
  ZquicLogger::pktSent(trace1, pkt(group1, 1));
  ZquicLogger::pktSent(trace1, pkt(group2, 2));
  ZquicLogger::pktSent(trace2, pkt(group3, 3));

  closeQLog_(trace1);
  closeQLog_(trace2);
  ZquicLogger::stop();
  ZquicLogDiag diag = ZquicLogger::diag();
  ZquicLogger::final(trace1);
  ZquicLogger::final(trace2);

  ZuCHECK(diag.recordsEnqueued >= 3, "multi trace qlog enqueue mismatch");
  ZuCHECK(diag.recordsWritten >= 5, "multi trace qlog write mismatch");
  ZuCHECK(diag.writerFailures == 0, "multi trace qlog writer failure");

  ZtString<> data1 = readFile_(path1);
  ZtString<> data2 = readFile_(path2);
  ZuCHECK(data1, "multi trace 1 output was not written");
  ZuCHECK(data2, "multi trace 2 output was not written");
  ZuCHECK(parseJSONSeq_(data1) >= 3,
    "multi trace 1 qlog JSON-SEQ parse failed");
  ZuCHECK(parseJSONSeq_(data2) >= 2,
    "multi trace 2 qlog JSON-SEQ parse failed");
  ZuCHECK(containsQLogVantage_(data1, "client"),
    "multi trace 1 missing client vantage");
  ZuCHECK(containsQLogVantage_(data2, "server"),
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
  removeTestLog_(path1);
  removeTestLog_(path2);
#endif
}

void testQLogTypedTransportEvents()
{
  ZuTestScope(testQLogTypedTransportEvents);

#ifdef Zquic_DEBUG
  Zi::Path path = testPath_("ZquicLogTypedTest.sqlog");
  ZiFile::remove(path);

  ZquicLogParams params;
  params.enabled(true).path(path).thread("zquic-qlog-typed").ringSize(1<<15);
  ZquicLogger::Trace trace;
  ZuCHECK(ZquicLogger::init(trace, params, Zquic::Vantage::Unknown), "typed qlog init failed");
  ZquicLogger::start();

  DgramEvent datagram;
  datagram.size = 1234;
  datagram.ecn = EcnMark::ECT0;
  ZquicLogger::dgramRecv(trace, ZuMv(datagram));
  DgramEvent sentDatagram;
  sentDatagram.size = 1200;
  sentDatagram.ecn = EcnMark::N;
  ZquicLogger::dgramSent(trace, ZuMv(sentDatagram));

  PktEvent packet;
  packet.packetType = PktType::Short;
  packet.packetSpace = PktNumSpace::AppData;
  packet.packetNumber = 42;
  packet.packetSize = 1200;
  packet.payloadSize = 1180;
  packet.ecn = EcnMark::ECT0;
  packet.bytesInFlight = 2400;
  packet.frameCount = 7;
  packet.ackEliciting = true;
  FrameEvent crypto;
  crypto.type = FrameType::Crypto;
  crypto.offset = 0;
  crypto.length = 64;
  packet.frames.push(crypto);
  FrameEvent stream;
  stream.type = FrameType::Stream;
  stream.streamID = 4;
  stream.offset = 128;
  stream.length = 32;
  stream.fin = true;
  packet.frames.push(stream);
  FrameEvent maxStreams;
  maxStreams.type = FrameType::MaxStreams;
  maxStreams.value = 16;
  maxStreams.streamType = StreamType::Simplex;
  packet.frames.push(maxStreams);
  FrameEvent streamsBlocked;
  streamsBlocked.type = FrameType::StreamsBlocked;
  streamsBlocked.value = 8;
  streamsBlocked.streamType = StreamType::Duplex;
  packet.frames.push(streamsBlocked);
  packet.frames.push(FrameEvent{.type = FrameType::PathResponse});
  packet.frames.push(FrameEvent{
    .length = 16,
    .type = FrameType::NewToken
  });
  packet.frames.push(FrameEvent{.type = FrameType::HandshakeDone});
  ZquicLogger::pktSent(trace, ZuMv(packet));

  PktEvent rx;
  rx.packetType = PktType::Short;
  rx.packetSpace = PktNumSpace::AppData;
  rx.packetNumber = 43;
  rx.packetSize = 1100;
  rx.payloadSize = 1080;
  rx.ecn = EcnMark::CE;
  rx.frameCount = 2;
  rx.ackEliciting = true;
  FrameEvent ack;
  ack.type = FrameType::Ack;
  ack.largestAcked = 40;
  ack.ackDelayUS = 25;
  ack.rangeCount = 2;
  new (ack.ackRanges.push()) ZquicLog_::QAckRange{.first = 32, .largest = 35};
  new (ack.ackRanges.push()) ZquicLog_::QAckRange{.first = 38, .largest = 40};
  ack.ect0 = 10;
  ack.ect1 = 2;
  ack.ce = 1;
  rx.frames.push(ack);
  FrameEvent rxStream;
  rxStream.type = FrameType::Stream;
  rxStream.streamID = 8;
  rxStream.offset = 256;
  rxStream.length = 48;
  rx.frames.push(rxStream);
  ZquicLogger::pktRecv(trace, ZuMv(rx));

  PktEvent rxControl;
  rxControl.packetType = PktType::Short;
  rxControl.packetSpace = PktNumSpace::AppData;
  rxControl.packetNumber = 44;
  rxControl.packetSize = 1000;
  rxControl.payloadSize = 980;
  rxControl.ecn = EcnMark::ECT1;
  rxControl.frameCount = FrameMax;
  rxControl.framesTruncated = true;

  FrameEvent reset;
  reset.type = FrameType::ResetStream;
  reset.streamID = 10;
  reset.errorCode = 99;
  reset.length = 4096;
  rxControl.frames.push(reset);

  FrameEvent stop;
  stop.type = FrameType::StopSending;
  stop.streamID = 10;
  stop.errorCode = 17;
  rxControl.frames.push(stop);

  FrameEvent maxStreamData;
  maxStreamData.type = FrameType::MaxStreamData;
  maxStreamData.streamID = 10;
  maxStreamData.value = 8192;
  rxControl.frames.push(maxStreamData);

  FrameEvent newCID;
  newCID.type = FrameType::NewCxnID;
  newCID.offset = 3;
  newCID.value = 4;
  newCID.length = 8;
  newCID.cxnID = CxnID{"qlogcid1"};
  newCID.resetToken = ResetToken{"0123456789abcdef"};
  rxControl.frames.push(newCID);

  FrameEvent retireCID;
  retireCID.type = FrameType::RetireCxnID;
  retireCID.value = 2;
  rxControl.frames.push(retireCID);

  FrameEvent challenge;
  challenge.type = FrameType::PathChallenge;
  challenge.length = 8;
  rxControl.frames.push(challenge);

  FrameEvent close;
  close.type = FrameType::ConnectionClose;
  close.errorCode = 0x100;
  close.length = 12;
  rxControl.frames.push(close);

  ZuCHECK(rxControl.frames.length() == 7, "control frame setup incomplete");
  ZuCHECK(rxControl.frames[1].type == FrameType::StopSending,
    "control frame copy failed");
  PktEvent rxMove = ZuMv(rxControl);
  ZuCHECK(rxMove.frames.length() == 7, "control frame move length failed");
  ZuCHECK(rxMove.frames[1].type == FrameType::StopSending,
    "control frame move failed");
  ZquicLogger::pktRecv(trace, ZuMv(rxMove));

  PktEvent buffered;
  buffered.packetType = PktType::Initial;
  buffered.packetSpace = PktNumSpace::Initial;
  buffered.packetSize = 1200;
  buffered.reason = PktEvent::Reason::Coalescing;
  ZquicLogger::pktBuf(trace, ZuMv(buffered));

  PktEvent drop;
  drop.packetType = PktType::Initial;
  drop.packetSpace = PktNumSpace::Initial;
  drop.packetSize = 50;
  drop.reason = PktEvent::Reason::ParseLong;
  ZquicLogger::pktDrop(trace, ZuMv(drop));

  closeQLog_(trace);
  ZquicLogger::stop();
  ZquicLogDiag diag = ZquicLogger::diag();
  ZquicLogger::final(trace);

  ZuCHECK(diag.recordsEnqueued >= 7, "typed qlog enqueue mismatch");
  ZuCHECK(diag.recordsWritten >= 8, "typed qlog write mismatch");
  ZuCHECK(diag.writerFailures == 0, "typed qlog writer failure");

  ZtString<> data = readFile_(path);
  ZuCHECK(data, "typed qlog output was not written");
  ZuCHECK(parseJSONSeq_(data) >= 8, "typed qlog JSON-SEQ parse failed");
  ZuCHECK(containsDatagramReceived_(data), "datagram event missing");
  ZuCHECK(containsDatagramSent_(data), "datagram sent event missing");
  ZuCHECK(containsTypedDatagramFields_(data), "typed datagram fields missing");
  ZuCHECK(containsTransportPacketSent_(data), "packet_sent event missing");
  ZuCHECK(containsTransportPacketReceived_(data), "packet_received event missing");
  ZuCHECK(containsTransportPacketBuffered_(data),
    "packet_buffered event missing");
  ZuCHECK(containsTransportPacketDropped_(data), "packet_dropped event missing");
  ZuCHECK(containsTypedPacketFields_(data), "typed packet fields missing");
  ZuCHECK(data.find<"reset_stream">() >= 0, "RESET_STREAM summary missing");
  ZuCHECK(data.find<"stop_sending">() >= 0, "STOP_SENDING summary missing");
  ZuCHECK(data.find<"max_stream_data">() >= 0,
    "MAX_STREAM_DATA summary missing");
  ZuCHECK(data.find<"max_streams">() >= 0, "MAX_STREAMS summary missing");
  ZuCHECK(data.find<"streams_blocked">() >= 0,
    "STREAMS_BLOCKED summary missing");
  ZuCHECK(data.find<"\"stream_type\":\"unidirectional\"">() >= 0,
    "MAX_STREAMS stream type missing");
  ZuCHECK(data.find<"new_connection_id">() >= 0,
    "NEW_CONNECTION_ID summary missing");
  ZuCHECK(data.find<"\"connection_id\":\"716C6F6763696431\"">() >= 0,
    "NEW_CONNECTION_ID connection ID missing");
  ZuCHECK(data.find<
    "\"stateless_reset_token\":\"30313233343536373839616263646566\"">() >= 0,
    "NEW_CONNECTION_ID reset token missing");
  ZuCHECK(data.find<"\"connection_id\":\"\"">() < 0,
    "empty frame connection ID leaked");
  ZuCHECK(data.find<"\"stateless_reset_token\":\"\"">() < 0,
    "empty frame reset token leaked");
  ZuCHECK(data.find<"retire_connection_id">() >= 0,
    "RETIRE_CONNECTION_ID summary missing");
  ZuCHECK(data.find<"path_challenge">() >= 0,
    "PATH_CHALLENGE summary missing");
  ZuCHECK(data.find<"path_response">() >= 0,
    "PATH_RESPONSE summary missing");
  ZuCHECK(data.find<"new_token">() >= 0,
    "NEW_TOKEN summary missing");
  ZuCHECK(data.find<"handshake_done">() >= 0,
    "HANDSHAKE_DONE summary missing");
  ZuCHECK(data.find<"connection_close">() >= 0,
    "CONNECTION_CLOSE summary missing");
  ZuCHECK(data.find<"error_code">() >= 0, "frame error code missing");
  ZuCHECK(data.find<"maximum">() >= 0, "frame maximum missing");
  ZuCHECK(data.find<"limit">() >= 0, "frame limit missing");
  ZuCHECK(data.find<"crypto">() >= 0, "crypto frame summary missing");
  ZuCHECK(data.find<"stream_id">() >= 0, "stream frame summary missing");
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
  removeTestLog_(path);
#endif
}

void testQLogTypedRecoveryEvents()
{
  ZuTestScope(testQLogTypedRecoveryEvents);

#ifdef Zquic_DEBUG
  Zi::Path path = testPath_("ZquicLogRecoveryTest.sqlog");
  ZiFile::remove(path);

  ZquicLogParams params;
  params.enabled(true).path(path).thread("zquic-qlog-recovery").ringSize(1<<18);
  ZquicLogger::Trace trace;
  ZuCHECK(ZquicLogger::init(trace, params, Zquic::Vantage::Unknown), "recovery qlog init failed");
  ZquicLogger::start();

  AckEvent ack;
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
  ZquicLogger::pktsAcked(trace, ZuMv(ack));

  RecEvent loss{
    .packetNumber = 77,
    .bytes = 1200,
    .bytesInFlight = 2400,
    .kind = RecKind::Aggregate,
    .packetSpace = PktNumSpace::AppData,
    .reason = RecReason::PacketThreshold,
    .frameCount = 1
  };
  FrameEvent lostCrypto{
    .offset = 8,
    .length = 12,
    .type = FrameType::Crypto
  };
  loss.frames.push(lostCrypto);
  ZquicLogger::pktLost(trace, loss);
  ZquicLogger::recPktLost(trace, loss);
  ZquicLogger::markRetrans(trace, loss);

  RecEvent metrics{
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
  ZquicLogger::metricsUpd(trace, ZuMv(metrics));

	  RecEvent timer{
	    .deadlineUS = 123456,
	    .kind = RecKind::PTO,
	    .packetSpace = PktNumSpace::Handshake,
	    .reason = RecReason::Armed
	  };
	  ZquicLogger::lossTimerUpd(trace, ZuMv(timer));

	  RecEvent lossTimer{
	    .deadlineUS = 123456,
	    .kind = RecKind::Loss,
	    .packetSpace = PktNumSpace::AppData,
	    .reason = RecReason::Armed
	  };
	  ZquicLogger::lossTimerUpd(trace, ZuMv(lossTimer));

	  RecEvent ptoExpired{
	    .value = 2,
	    .bytesInFlight = 2400,
	    .kind = RecKind::PTO,
	    .packetSpace = PktNumSpace::AppData,
	    .reason = RecReason::Expired
	  };
	  ZquicLogger::lossTimerUpd(trace, ZuMv(ptoExpired));

	  RecEvent ptoBackoff{
	    .value = 3,
	    .bytes = 2,
	    .bytesInFlight = 2400,
	    .kind = RecKind::PTO,
	    .packetSpace = PktNumSpace::AppData,
	    .reason = RecReason::Backoff
	  };
	  ZquicLogger::lossTimerUpd(trace, ZuMv(ptoBackoff));

	  RecEvent ptoProbe{
	    .value = 3,
	    .bytes = 2,
	    .kind = RecKind::PTO,
	    .packetSpace = PktNumSpace::AppData,
	    .reason = RecReason::Probe
	  };
	  ZquicLogger::lossTimerUpd(trace, ZuMv(ptoProbe));

  RecEvent congestion{
    .cwnd = 13200,
    .ssthresh = 64000,
    .bytesInFlight = 1200,
    .kind = RecKind::NewReno,
    .packetSpace = PktNumSpace::AppData,
    .reason = RecReason::Ack
  };
  ZquicLogger::congStateUpd(trace, ZuMv(congestion));

  ECNEvent ecn{
    .ect0 = 7,
    .ect1 = 1,
    .ce = 2,
    .previousECT0 = 6,
    .previousECT1 = 1,
    .previousCE = 1,
    .largestAcked = 12,
    .packetSpace = PktNumSpace::AppData,
    .state = ECNState::Capable,
    .reason = ECNReason::AckECN
  };
  ZquicLogger::ecnStateUpd(trace, ZuMv(ecn));

  ECNEvent fallback{
    .ect0 = 20,
    .ce = 1,
    .previousECT0 = 7,
    .previousECT1 = 1,
    .previousCE = 2,
    .largestAcked = 12,
    .packetSpace = PktNumSpace::AppData,
    .state = ECNState::Failed,
    .reason = ECNReason::CounterExceedsAck,
    .disabled = true
  };
  ZquicLogger::ecnStateUpd(trace, ZuMv(fallback));

  closeQLog_(trace);
  ZquicLogger::stop();
  ZquicLogDiag diag = ZquicLogger::diag();
  ZquicLogger::final(trace);

	  ZuCHECK(diag.recordsEnqueued >= 12, "recovery qlog enqueue mismatch");
	  ZuCHECK(diag.recordsWritten >= 13, "recovery qlog write mismatch");
  ZuCHECK(diag.writerFailures == 0, "recovery qlog writer failure");

  ZtString<> data = readFile_(path);
  ZuCHECK(data, "recovery qlog output was not written");
	  ZuCHECK(parseJSONSeq_(data) >= 13, "recovery qlog JSON-SEQ parse failed");
	  ZuCHECK(containsRecoveryEvents_(data), "recovery event coverage missing");
	  ZuCHECK(containsRecoveryFields_(data), "recovery fields missing");
	  ZuCHECK(data.find<"smoothed_rtt_us">() < 0,
	    "old recovery metric smoothed_rtt_us emitted");
	  ZuCHECK(data.find<"rtt_variance_us">() < 0,
	    "old recovery metric rtt_variance_us emitted");
	  ZuCHECK(data.find<"cwnd">() < 0, "old recovery metric cwnd emitted");
	  ZuCHECK(data.find<"\"packet_type\":\"1RTT\"">() >= 0,
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
  removeTestLog_(path);
#endif
}

void testQLogTypedSecEvents()
{
  ZuTestScope(testQLogTypedSecEvents);

#ifdef Zquic_DEBUG
  Zi::Path path = testPath_("ZquicLogSecurityTest.sqlog");
  ZiFile::remove(path);

  ZquicLogParams params;
  params.enabled(true).path(path).thread("zquic-qlog-security").ringSize(1<<15);
  ZquicLogger::Trace trace;
  ZuCHECK(ZquicLogger::init(trace, params, Zquic::Vantage::Unknown), "security qlog init failed");
  Zquic::CxnID odcid{"QLOG"};
  Zquic::LinkInfo linkInfo{
    .vantage = Zquic::Vantage::Client,
    .origDCID = odcid
  };
  ZquicLogger::start();

  SecEvent key{
    .linkInfo = linkInfo,
    .value = 1,
    .kind = SecKind::KeyUpdated,
    .packetSpace = PktNumSpace::AppData,
    .keyType = SecKeyType::RX,
    .trigger = SecTrigger::Remote,
    .reason = SecReason::KeyPhase
  };
  ZquicLogger::keyUpdated(trace, ZuMv(key));

  SecEvent retired{
    .linkInfo = linkInfo,
    .kind = SecKind::KeyRetired,
    .packetSpace = PktNumSpace::Handshake,
    .keyType = SecKeyType::TX,
    .trigger = SecTrigger::HSComplete,
    .reason = SecReason::PacketSpace
  };
  ZquicLogger::keyRetired(trace, ZuMv(retired));

  ParamsEvent paramsSet{
    .maxIdleTimeout = 42,
    .maxUDPPayloadSize = 1350,
    .ackDelayExponent = 3,
    .maxAckDelay = 25,
    .activeCxnIDLimit = 4,
    .initialMaxData = 65536,
    .initialMaxStreamDataBidiLocal = 4096,
    .initialMaxStreamDataBidiRemote = 8192,
    .initialMaxStreamDataUni = 2048,
    .initialMaxStreamsBidi = 8,
    .initialMaxStreamsUni = 4,
    .initiator = Initiator::Remote,
    .disableActiveMigration = true
  };
  ZquicLogger::paramsSet(trace, ZuMv(paramsSet));

  SecEvent alpn{
    .alpn = "h3",
    .kind = SecKind::ALPN,
    .trigger = SecTrigger::Selected
  };
  ZquicLogger::alpnInfo(trace, ZuMv(alpn));

  SecEvent alert{
    .kind = SecKind::TLS,
    .reason = SecReason::Handshake,
    .success = false
  };
  ZquicLogger::tlsAlert(trace, ZuMv(alert));

  SecEvent retry{
    .value = 42,
    .kind = SecKind::Retry,
    .trigger = SecTrigger::Sent,
    .reason = SecReason::AddrValid
  };
  ZquicLogger::secEvent(trace, EventName::RetrySent, retry);

  ZquicLogger::secEvent(trace, EventName::RetryValid,
    SecEvent{
      .value = 42,
      .kind = SecKind::Retry,
      .trigger = SecTrigger::Received,
      .reason = SecReason::OK
    });

  SecEvent token{
    .value = 38,
    .kind = SecKind::Token,
    .trigger = SecTrigger::Validated,
    .reason = SecReason::OK
  };
  ZquicLogger::secEvent(trace, EventName::TokenValid, token);

  ZquicLogger::secEvent(trace, EventName::TokenIssued,
    SecEvent{
      .value = 38,
      .kind = SecKind::Token,
      .trigger = SecTrigger::Sent,
      .reason = SecReason::NewToken
    });

  ZquicLogger::secEvent(trace, EventName::TokenReject,
    SecEvent{
      .value = 38,
      .kind = SecKind::Token,
      .trigger = SecTrigger::Validated,
      .reason = SecReason::Expired,
      .success = false
    });

  VersionEvent vn;
  new (vn.serverVersions.push()) uint32_t(Version1);
  new (vn.clientVersions.push()) uint32_t(0x1a2a3a4a);
  ZquicLogger::versionInfo(trace, ZuMv(vn));

  SecEvent reset{
    .value = 43,
    .kind = SecKind::StatelessRst,
    .trigger = SecTrigger::Received,
    .reason = SecReason::TokenMatch
  };
  ZquicLogger::secEvent(trace, EventName::StatelessRst, ZuMv(reset));

  SecEvent protection{
    .kind = SecKind::PktProtect,
    .packetSpace = PktNumSpace::AppData,
    .trigger = SecTrigger::RX,
    .reason = SecReason::BadKeyPhase,
    .success = false
  };
  ZquicLogger::secEvent(trace,
    EventName::PktProtectFail, ZuMv(protection));

  ZquicLogger::secEvent(trace, EventName::ZeroRTTReject,
    SecEvent{
      .kind = SecKind::TLS,
      .trigger = SecTrigger::Received,
      .reason = SecReason::ZeroRTT,
      .success = false
    });

  closeQLog_(trace);
  ZquicLogger::stop();
  ZquicLogDiag diag = ZquicLogger::diag();
  ZquicLogger::final(trace);

  ZuCHECK(diag.recordsEnqueued >= 14, "security qlog enqueue mismatch");
  ZuCHECK(diag.recordsWritten >= 15, "security qlog write mismatch");
  ZuCHECK(diag.writerFailures == 0, "security qlog writer failure");

  ZtString<> data = readFile_(path);
  ZuCHECK(data, "security qlog output was not written");
  ZuCHECK(parseJSONSeq_(data) >= 15, "security qlog JSON-SEQ parse failed");
  ZuCHECK(containsSecEvents_(data), "security event coverage missing");
  ZuCHECK(containsSecFields_(data), "security fields missing");
  ZuCHECK(data.find<"\"name\":\"quic:key_updated\",\"data\":{\"key_type\":"
    "\"server_1rtt_secret\",\"key_phase\":1,\"trigger\":\"remote_update\"}">()
      >= 0, "key update fields missing");
  ZuCHECK(data.find<"\"name\":\"quic:key_discarded\",\"data\":{\"key_type\":"
    "\"client_handshake_secret\",\"trigger\":\"tls\"}">()
      >= 0, "key discarded fields missing");
  ZuCHECK(data.find<"\"name\":\"quic:key_updated\","
    "\"data\":{\"kind\"">() < 0,
    "generic security fields leaked into key_updated");
  ZuCHECK(data.find<"\"name\":\"quic:key_discarded\","
    "\"data\":{\"kind\"">() < 0,
    "generic security fields leaked into key_discarded");
  ZuCHECK(data.find<"\"name\":\"quic:key_discarded\"">() >= 0 &&
    data.find<"\"name\":\"quic:key_discarded\",\"data\":{\"key_type\":"
    "\"client_handshake_secret\",\"key_phase\"">() < 0,
    "handshake key discard emitted key_phase");
  ZuCHECK(data.find<"\"name\":\"quic:parameters_set\",\"data\":{"
    "\"initiator\":\"remote\"">() >= 0,
    "parameters_set initiator missing");
  ZuCHECK(data.find<"max_udp_payload_size">() >= 0,
    "parameters_set max UDP payload missing");
  ZuCHECK(data.find<"initial_max_data">() >= 0,
    "parameters_set max data missing");
  ZuCHECK(data.find<"initial_max_streams_bidi">() >= 0,
    "parameters_set stream limit missing");
  ZuCHECK(data.find<"disable_active_migration">() >= 0,
    "parameters_set migration flag missing");
  ZuCHECK(data.find<"\"name\":\"quic:parameters_set\","
    "\"data\":{\"kind\"">() < 0,
    "generic security fields leaked into parameters_set");
  ZuCHECK(data.find<"\"name\":\"quic:alpn_information\",\"data\":{"
    "\"chosen_alpn\":{\"string_value\":\"h3\"}}">() >= 0,
    "ALPN chosen_alpn missing");
  ZuCHECK(data.find<"\"name\":\"quic:alpn_information\","
    "\"data\":{\"kind\"">() < 0,
    "generic security fields leaked into alpn_information");
  ZuCHECK(data.find<"address_validation">() >= 0,
    "Retry qlog reason missing");
  ZuCHECK(data.find<"\"name\":\"quic:version_information\",\"data\":{"
    "\"server_versions\":[\"00000001\"],"
    "\"client_versions\":[\"1a2a3a4a\"]}">() >= 0,
    "version_information fields missing");
  ZuCHECK(data.find<"\"name\":\"quic:version_information\","
    "\"data\":{\"kind\"">() < 0,
    "generic security fields leaked into version_information");
  ZuCHECK(data.find<"unsupported_version">() < 0,
    "old version_information reason leaked");
	  ZuCHECK(data.find<"token_match">() >= 0,
	    "stateless reset qlog reason missing");
	  ZuCHECK(data.find<"invalid_key_phase">() >= 0,
	    "packet protection qlog reason missing");
	  ZuCHECK(data.find<"\"name\":\"zquic:zero_rtt_rejected\","
	    "\"data\":{\"kind\":\"tls\",\"packet_number_space\":\"initial\","
	    "\"key_type\":\"\",\"trigger\":\"received\",\"alpn\":\"\","
	    "\"reason\":\"0rtt\",\"value\":0,\"success\":false}">() >= 0,
	    "0-RTT rejection qlog event missing");
	  ZuCHECK(data.find<"\"packet_space\"">() < 0,
	    "old security packet_space field leaked");
	  removeTestLog_(path);
#endif
}

void testQLogTypedPathCIDEvents()
{
  ZuTestScope(testQLogTypedPathCIDEvents);

#ifdef Zquic_DEBUG
  Zi::Path path = testPath_("ZquicLogPathCIDTest.sqlog");
  ZiFile::remove(path);

  ZquicLogParams params;
  params.enabled(true).path(path).thread("zquic-qlog-path").ringSize(1<<15);
  ZquicLogger::Trace trace;
  ZuCHECK(ZquicLogger::init(trace, params, Zquic::Vantage::Unknown), "path qlog init failed");
  ZquicLogger::start();

  PathEvent pathEvent{
    .tupleID = 7,
    .bytes = 1200,
    .antiAmplification = 2400,
    .deadlineUS = 1000000,
    .mtu = 1350,
    .kind = PathKind::Path,
    .action = PathAction::Updated,
    .reason = PathReason::Promoted,
    .validated = true
  };
  ZquicLogger::pathUpdated(trace, ZuMv(pathEvent));

  PathEvent validation{
    .deadlineUS = 2000000,
    .mtu = 1350,
    .kind = PathKind::PathValid,
    .action = PathAction::ChallengeTx,
    .reason = PathReason::PeerAddrChange
  };
  ZquicLogger::pathValidUpd(trace, ZuMv(validation));

  PathEvent pmtud{
    .mtu = 1400,
    .kind = PathKind::PMTUD,
    .action = PathAction::Sent,
    .reason = PathReason::Probe,
    .validated = true
  };
  ZquicLogger::pmtudUpdated(trace, ZuMv(pmtud));

  CIDEvent cid{
    .cxnID = CxnID{"cidpath1"},
    .sequence = 7,
    .kind = CIDKind::CxnID,
    .action = CIDAction::RouteBound,
    .reason = CIDReason::PathPromoted,
    .length = 8,
    .local = false,
    .associated = true,
    .resetToken = true
  };
  ZquicLogger::cidUpdated(trace, ZuMv(cid));

  closeQLog_(trace);
  ZquicLogger::stop();
  ZquicLogDiag diag = ZquicLogger::diag();
  ZquicLogger::final(trace);

  ZuCHECK(diag.recordsEnqueued >= 4, "path qlog enqueue mismatch");
  ZuCHECK(diag.recordsWritten >= 5, "path qlog write mismatch");
  ZuCHECK(diag.writerFailures == 0, "path qlog writer failure");

  ZtString<> data = readFile_(path);
  ZuCHECK(data, "path qlog output was not written");
  ZuCHECK(parseJSONSeq_(data) >= 5, "path qlog JSON-SEQ parse failed");
  ZuCHECK(containsPathCIDEvents_(data), "path/CID event coverage missing");
  ZuCHECK(containsPathCIDFields_(data), "path/CID fields missing");
	  ZuCHECK(data.find<"\"name\":\"quic:tuple_assigned\",\"data\":{"
	    "\"tuple_id\":\"7\"}">() >= 0, "tuple_assigned fields missing");
	  ZuCHECK(data.find<"\"name\":\"quic:path_validated\",\"data\":{"
	    "\"success\":false,\"vantage\":\"unknown\"}">() >= 0,
	    "path_validated fields missing");
	  ZuCHECK(data.find<"\"name\":\"quic:tuple_assigned\","
	    "\"data\":{\"kind\"">() < 0,
	    "generic path fields leaked into tuple_assigned");
  ZuCHECK(data.find<"deadline_us">() < 0,
    "old path deadline leaked into standard tuple_assigned");
  ZuCHECK(data.find<"\"name\":\"quic:connection_id_updated\",\"data\":{"
    "\"initiator\":\"remote\",\"new\":\"6369647061746831\"}">() >= 0,
    "connection_id_updated fields missing");
  ZuCHECK(data.find<"\"name\":\"quic:connection_id_updated\","
    "\"data\":{\"kind\"">() < 0,
    "generic CID fields leaked into connection_id_updated");
  ZuCHECK(data.find<"path_promoted">() < 0,
    "old CID reason leaked into standard event");
  ZuCHECK(data.find<"reset_token">() < 0,
    "old CID reset token flag leaked into standard event");
  ZuCHECK(data.find<"\"name\":\"quic:mtu_updated\",\"data\":{\"new\":1400,"
    "\"done\":true}">() >= 0, "MTU update fields missing");
  ZuCHECK(data.find<"\"name\":\"quic:mtu_updated\","
    "\"data\":{\"kind\"">() < 0,
    "generic path fields leaked into mtu_updated");
  removeTestLog_(path);
#endif
}

void testQLogTypedStreamEvents()
{
  ZuTestScope(testQLogTypedStreamEvents);

#ifdef Zquic_DEBUG
  Zi::Path path = testPath_("ZquicLogStreamTest.sqlog");
  ZiFile::remove(path);

  ZquicLogParams params;
  params.enabled(true).path(path).thread("zquic-qlog-stream").ringSize(1<<15);
  ZquicLogger::Trace trace;
  ZuCHECK(ZquicLogger::init(trace, params, Zquic::Vantage::Unknown), "stream qlog init failed");
  ZquicLogger::start();

  StreamEvent open{
    .streamID = 4,
    .streamType = StreamType::Duplex,
    .oldState = StreamState::Idle,
    .newState = StreamState::Open,
    .streamSide = StreamSide::Sending,
    .reason = StreamReason::LocalOpen
  };
  ZquicLogger::streamStateUpd(trace, ZuMv(open));

  StreamEvent closed{
    .streamID = 4,
    .offset = 128,
    .length = 32,
    .streamType = StreamType::Duplex,
    .oldState = StreamState::Open,
    .newState = StreamState::Closed,
    .streamSide = StreamSide::Receiving,
    .reason = StreamReason::Reaped,
    .fin = true
  };
  ZquicLogger::streamStateUpd(trace, ZuMv(closed));

  StreamDataEvent moved{
    .streamID = 4,
    .offset = 64,
    .length = 32,
    .from = StreamDataLoc::Application,
    .to = StreamDataLoc::Transport
  };
  ZquicLogger::streamDataMoved(trace, ZuMv(moved));

  StreamDataEvent sent{
    .streamID = 4,
    .offset = 64,
    .length = 32,
    .from = StreamDataLoc::Transport,
    .to = StreamDataLoc::Network,
    .additionalInfo = StreamDataInfo::FinSet
  };
  ZquicLogger::streamDataMoved(trace, ZuMv(sent));

  ZquicLogger::cxnDataBlockedUpd(trace,
    BlockedEvent{
      .oldState = BlockedState::Unblocked,
      .newState = BlockedState::Blocked,
      .reason = BlockedReason::CxnFlowCtrl
    });
  ZquicLogger::streamDataBlockedUpd(trace,
    BlockedEvent{
      .streamID = 4,
      .oldState = BlockedState::Blocked,
      .newState = BlockedState::Unblocked,
      .reason = BlockedReason::StreamFlowCtrl
    });

  closeQLog_(trace);
  ZquicLogger::stop();
  ZquicLogDiag diag = ZquicLogger::diag();
  ZquicLogger::final(trace);

  ZuCHECK(diag.recordsEnqueued >= 6, "stream qlog enqueue mismatch");
  ZuCHECK(diag.recordsWritten >= 7, "stream qlog write mismatch");
  ZuCHECK(diag.writerFailures == 0, "stream qlog writer failure");

  ZtString<> data = readFile_(path);
  ZuCHECK(data, "stream qlog output was not written");
  ZuCHECK(parseJSONSeq_(data) >= 7, "stream qlog JSON-SEQ parse failed");
  ZuCHECK(containsStreamEvents_(data), "stream event coverage missing");
  ZuCHECK(containsStreamFields_(data), "stream fields missing");
  ZuCHECK(data.find<"local_open">() >= 0, "stream open reason missing");
  ZuCHECK(data.find<"reaped">() >= 0, "stream close reason missing");
  ZuCHECK(data.find<"application">() >= 0, "stream data source missing");
  ZuCHECK(data.find<"network">() >= 0, "stream data destination missing");
  ZuCHECK(data.find<"fin_set">() >= 0, "stream data FIN info missing");
  removeTestLog_(path);
#endif
}

void testQLogTypedCloseEvents()
{
  ZuTestScope(testQLogTypedCloseEvents);

#ifdef Zquic_DEBUG
  Zi::Path path = testPath_("ZquicLogCloseTest.sqlog");
  ZiFile::remove(path);

  ZquicLogParams params;
  params.enabled(true).path(path).thread("zquic-qlog-close").ringSize(1<<15);
  ZquicLogger::Trace trace;
  ZuCHECK(ZquicLogger::init(trace, params, Zquic::Vantage::Unknown), "close qlog init failed");
  ZquicLogger::start();

  CloseEvent local{
    .errorCode = 42,
    .initiator = CloseInitiator::Local,
    .trigger = CloseTrigger::Application,
    .reason = CloseReason::LocalClose,
    .applicationError = CloseError::Unknown
  };
  ZquicLogger::cxnClosed(trace, ZuMv(local));

  CloseEvent idle{
    .errorCode = 0,
    .initiator = CloseInitiator::Local,
    .trigger = CloseTrigger::IdleTimeout,
    .reason = CloseReason::Idle,
    .connectionError = CloseError::NoError
  };
  ZquicLogger::cxnClosed(trace, ZuMv(idle));

  CloseEvent frameEncoding{
    .errorCode = TransportError::FrameEncoding,
    .initiator = CloseInitiator::Remote,
    .trigger = CloseTrigger::Error,
    .reason = CloseReason::PeerCloseFrame,
    .connectionError = CloseError::Unknown
  };
  ZquicLogger::cxnClosed(trace, ZuMv(frameEncoding));

  closeQLog_(trace);
  ZquicLogger::stop();
  ZquicLogDiag diag = ZquicLogger::diag();
  ZquicLogger::final(trace);

  ZuCHECK(diag.recordsEnqueued >= 3, "close qlog enqueue mismatch");
  ZuCHECK(diag.recordsWritten >= 4, "close qlog write mismatch");
  ZuCHECK(diag.writerFailures == 0, "close qlog writer failure");

  ZtString<> data = readFile_(path);
  ZuCHECK(data, "close qlog output was not written");
  ZuCHECK(parseJSONSeq_(data) >= 4, "close qlog JSON-SEQ parse failed");
  ZuCHECK(containsCloseEvents_(data), "close event coverage missing");
  ZuCHECK(containsCloseFields_(data), "close fields missing");
  ZuCHECK(data.find<"local_close">() >= 0, "local close reason missing");
  ZuCHECK(data.find<"idle_timeout">() >= 0, "idle close trigger missing");
  ZuCHECK(data.find<"frame_encoding_error">() >= 0,
    "known transport close error missing");
  ZuCHECK(data.find<"\"connection_error\":\"unknown\",\"error_code\":8">() <
      0, "known transport close error emitted as unknown");
  ZuCHECK(data.find<"\"error_code\":0">() < 0,
    "close event emitted default error_code");
  ZuCHECK(data.find<"\"reason\":\"\"">() < 0,
    "close event emitted empty reason");
  ZuCHECK(data.find<"\"application\":">() < 0,
    "close event leaked local application flag");
  ZuCHECK(data.find<"\"frame\":">() < 0,
    "close event leaked local frame flag");
  removeTestLog_(path);
#endif
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testQLogFileOutput);
  ZuTestCall(testQLogBackPressure);
  ZuTestCall(testQLogAppTrace);
  ZuTestCall(testQLogMultiTraceGroups);
  ZuTestCall(testQLogTypedTransportEvents);
  ZuTestCall(testQLogTypedRecoveryEvents);
  ZuTestCall(testQLogTypedSecEvents);
  ZuTestCall(testQLogTypedPathCIDEvents);
  ZuTestCall(testQLogTypedStreamEvents);
  ZuTestCall(testQLogTypedCloseEvents);
}
