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
    if (scan.p<0>() != int(json.length())) return 0;
    ++n;
    ++i;
  }
  return n;
}

static bool containsQLogVersion_(ZuCSpan data)
{
  return data.find<"qlog_version">() >= 0;
}

static bool containsQLogHeaderMetadata_(ZuCSpan data)
{
  return
    data.find<"qlog_format">() >= 0 &&
    data.find<"vantage_point">() >= 0 &&
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
  return data.find<"transport:packet_sent">() >= 0;
}

static bool containsTransportPacketReceived_(ZuCSpan data)
{
  return data.find<"transport:packet_received">() >= 0;
}

static bool containsTransportPacketBuffered_(ZuCSpan data)
{
  return data.find<"transport:packet_buffered">() >= 0;
}

static bool containsTransportPacketDropped_(ZuCSpan data)
{
  return data.find<"transport:packet_dropped">() >= 0;
}

static bool containsDatagramReceived_(ZuCSpan data)
{
  return data.find<"transport:datagrams_received">() >= 0;
}

static bool containsDatagramSent_(ZuCSpan data)
{
  return data.find<"transport:datagrams_sent">() >= 0;
}

static bool containsTypedPacketFields_(ZuCSpan data)
{
  return
    data.find<"packet_type">() >= 0 &&
    data.find<"packet_space">() >= 0 &&
    data.find<"packet_number">() >= 0 &&
    data.find<"bytes_in_flight">() >= 0 &&
    data.find<"frames">() >= 0 &&
    data.find<"frames_truncated">() >= 0 &&
    data.find<"ack_eliciting">() >= 0;
}

static bool containsRecoveryEvents_(ZuCSpan data)
{
  return
    data.find<"transport:packets_acked">() >= 0 &&
    data.find<"transport:packet_lost">() >= 0 &&
    data.find<"recovery:packet_lost">() >= 0 &&
    data.find<"recovery:marked_for_retransmit">() >= 0 &&
    data.find<"recovery:metrics_updated">() >= 0 &&
    data.find<"recovery:loss_timer_updated">() >= 0 &&
    data.find<"recovery:congestion_state_updated">() >= 0 &&
    data.find<"recovery:ecn_state_updated">() >= 0;
}

static bool containsRecoveryFields_(ZuCSpan data)
{
  return
    data.find<"largest_acked">() >= 0 &&
    data.find<"ack_delay_us">() >= 0 &&
    data.find<"acked_bytes">() >= 0 &&
    data.find<"lost_bytes">() >= 0 &&
    data.find<"deadline_us">() >= 0 &&
    data.find<"smoothed_rtt_us">() >= 0 &&
    data.find<"rtt_variance_us">() >= 0 &&
    data.find<"cwnd">() >= 0 &&
    data.find<"ssthresh">() >= 0 &&
    data.find<"previous_ect0">() >= 0 &&
    data.find<"previous_ect1">() >= 0 &&
    data.find<"previous_ce">() >= 0 &&
    data.find<"disabled">() >= 0;
}

static bool containsSecurityEvents_(ZuCSpan data)
{
  return
    data.find<"security:key_updated">() >= 0 &&
    data.find<"security:key_retired">() >= 0 &&
    data.find<"security:transport_parameters_set">() >= 0 &&
    data.find<"security:alpn_information">() >= 0 &&
    data.find<"security:tls_alert">() >= 0 &&
    data.find<"security:retry_sent">() >= 0 &&
    data.find<"security:retry_validated">() >= 0 &&
    data.find<"security:token_issued">() >= 0 &&
    data.find<"security:token_validated">() >= 0 &&
    data.find<"security:token_rejected">() >= 0 &&
    data.find<"security:version_negotiation">() >= 0 &&
    data.find<"security:stateless_reset">() >= 0 &&
    data.find<"security:packet_protection_failed">() >= 0;
}

static bool containsSecurityFields_(ZuCSpan data)
{
  return
    data.find<"key_type">() >= 0 &&
    data.find<"trigger">() >= 0 &&
    data.find<"alpn">() >= 0 &&
    data.find<"success">() >= 0;
}

static bool containsPathCIDEvents_(ZuCSpan data)
{
  return
    data.find<"path:path_updated">() >= 0 &&
    data.find<"path:path_validation_updated">() >= 0 &&
    data.find<"path:pmtud_updated">() >= 0 &&
    data.find<"connectivity:connection_id_updated">() >= 0;
}

static bool containsPathCIDFields_(ZuCSpan data)
{
  return
    data.find<"anti_amplification">() >= 0 &&
    data.find<"deadline_us">() >= 0 &&
    data.find<"mtu">() >= 0 &&
    data.find<"validated">() >= 0 &&
    data.find<"sequence">() >= 0 &&
    data.find<"reset_token">() >= 0 &&
    data.find<"associated">() >= 0;
}

static bool containsStreamEvents_(ZuCSpan data)
{
  return data.find<"transport:stream_state_updated">() >= 0 &&
    data.find<"transport:stream_data_moved">() >= 0 &&
    data.find<"transport:connection_data_blocked_updated">() >= 0 &&
    data.find<"transport:stream_data_blocked_updated">() >= 0;
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
  return data.find<"connectivity:connection_closed">() >= 0;
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
  ZuCHECK(ZquicLogger::init(params), "qlog init failed");
  uint8_t odcid[] = { 0xde, 0xad, 0xbe, 0xef };
  uint8_t groupID[] = { 0x01, 0x02, 0x03, 0x04 };
  uint8_t dcid[] = { 0x11, 0x12, 0x13, 0x14 };
  uint8_t scid[] = { 0x21, 0x22, 0x23, 0x24 };
  ZuCHECK(ZquicLogger::metadata(ZquicLogMetadata{
      .vantagePoint = "client",
      .originalDCID = ZuBSpan{odcid, sizeof(odcid)},
      .groupID = ZuBSpan{groupID, sizeof(groupID)},
      .dcid = ZuBSpan{dcid, sizeof(dcid)},
      .scid = ZuBSpan{scid, sizeof(scid)}
    }), "qlog metadata failed");
  ZquicLogger::start();
  ZuCHECK(ZquicLogger::enabled(), "qlog did not enable");
  ZquicLogPacketEvent packet;
  packet.packetType = PktType::Initial;
  packet.packetSpace = PktNumSpace::Initial;
  packet.packetSize = 1200;
  ZquicLogger::packetSent(ZuMv(packet));
  ZquicLogger::lifecycle(ZquicLogLifecycle::Closed);
  ZquicLogger::stop();
  ZquicLogDiag diag = ZquicLogger::diag();
  ZquicLogger::final();

  ZuCHECK(diag.recordsEnqueued >= 2, "qlog enqueue diagnostics mismatch");
  ZuCHECK(diag.recordsWritten >= 3, "qlog write diagnostics mismatch");
  ZuCHECK(diag.writerFailures == 0, "qlog writer failure");

  ZtString<> data = readFile_(path);
  ZuCHECK(data, "qlog output was not written");
  ZuCHECK(parseJSONSeq_(data) >= 3, "qlog JSON-SEQ parse failed");
  ZuCHECK(containsQLogVersion_(data), "qlog header missing version");
  ZuCHECK(containsQLogHeaderMetadata_(data), "qlog header metadata missing");
  ZuCHECK(containsQLogVantage_(data, "client"),
    "qlog header missing client vantage point");
  ZuCHECK(containsQLogConnectionMetadata_(data),
    "qlog header missing connection metadata");
  ZuCHECK(containsPacketSent_(data), "qlog event missing packet_sent");
  ZuCHECK(!containsZiLogPrefix_(data), "qlog contains ZiLog text prefix");

  ZtString<> aged = readFile_(agedPath);
  ZuCHECK(aged.find<"old-qlog">() >= 0, "qlog output was not aged");
  ZiFile::remove(path);
  ZiFile::remove(agedPath);
#else
  ZquicLogParams params;
  params.enabled(true).path("ZquicLogTest.sqlog");
  ZuCHECK(ZquicLogger::init(params), "compiled-out qlog init failed");
  ZquicLogger::start();
  ZuCHECK(!ZquicLogger::enabled(), "compiled-out qlog enabled");
  ZquicLogPacketEvent packet;
  ZquicLogger::packetSent(ZuMv(packet));
  ZquicLogDiag diag = ZquicLogger::diag();
  ZquicLogger::stop();
  ZquicLogger::final();
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
  ZuCHECK(ZquicLogger::init(params), "qlog drop init failed");
  ZquicLogger::start();
  for (unsigned i = 0; i < 10000; ++i) {
    ZquicLogPacketEvent packet;
    packet.packetType = PktType::Short;
    packet.packetSpace = PktNumSpace::AppData;
    packet.packetNumber = i;
    packet.packetSize = 1200;
    ZquicLogger::packetReceived(ZuMv(packet));
  }
  ZquicLogger::stop();
  ZquicLogDiag diag = ZquicLogger::diag();
  ZquicLogger::final();

  ZuCHECK(diag.recordsDropped || diag.ringBackPressure,
    "qlog tiny-ring event was not dropped");
  ZiFile::remove(path);
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
  ZuCHECK(ZquicLogger::init(params), "typed qlog init failed");
  ZquicLogger::start();

  ZquicLogDatagramEvent datagram;
  datagram.size = 1234;
  datagram.ecn = EcnMark::ECT0;
  ZquicLogger::datagramReceived(ZuMv(datagram));
  ZquicLogDatagramEvent sentDatagram;
  sentDatagram.size = 1200;
  sentDatagram.ecn = EcnMark::N;
  ZquicLogger::datagramSent(ZuMv(sentDatagram));

  ZquicLogPacketEvent packet;
  packet.packetType = PktType::Short;
  packet.packetSpace = PktNumSpace::AppData;
  packet.packetNumber = 42;
  packet.packetSize = 1200;
  packet.payloadSize = 1180;
  packet.ecn = EcnMark::ECT0;
  packet.bytesInFlight = 2400;
  packet.frameCount = 2;
  packet.ackEliciting = true;
  ZquicLogFrameEvent crypto;
  crypto.type = FrameType::Crypto;
  crypto.offset = 0;
  crypto.length = 64;
  packet.frames.push(crypto);
  ZquicLogFrameEvent stream;
  stream.type = FrameType::Stream;
  stream.streamID = 4;
  stream.offset = 128;
  stream.length = 32;
  stream.fin = true;
  packet.frames.push(stream);
  ZquicLogger::packetSent(ZuMv(packet));

  ZquicLogPacketEvent rx;
  rx.packetType = PktType::Short;
  rx.packetSpace = PktNumSpace::AppData;
  rx.packetNumber = 43;
  rx.packetSize = 1100;
  rx.payloadSize = 1080;
  rx.ecn = EcnMark::CE;
  rx.frameCount = 2;
  rx.ackEliciting = true;
  ZquicLogFrameEvent ack;
  ack.type = FrameType::Ack;
  ack.largestAcked = 40;
  ack.ackDelayUS = 25;
  ack.rangeCount = 2;
  ack.ect0 = 10;
  ack.ect1 = 2;
  ack.ce = 1;
  rx.frames.push(ack);
  ZquicLogFrameEvent rxStream;
  rxStream.type = FrameType::Stream;
  rxStream.streamID = 8;
  rxStream.offset = 256;
  rxStream.length = 48;
  rx.frames.push(rxStream);
  ZquicLogger::packetReceived(ZuMv(rx));

  ZquicLogPacketEvent rxControl;
  rxControl.packetType = PktType::Short;
  rxControl.packetSpace = PktNumSpace::AppData;
  rxControl.packetNumber = 44;
  rxControl.packetSize = 1000;
  rxControl.payloadSize = 980;
  rxControl.ecn = EcnMark::ECT1;
  rxControl.frameCount = ZquicLogFrameMax;
  rxControl.framesTruncated = true;

  ZquicLogFrameEvent reset;
  reset.type = FrameType::ResetStream;
  reset.streamID = 10;
  reset.errorCode = 99;
  reset.length = 4096;
  rxControl.frames.push(reset);

  ZquicLogFrameEvent stop;
  stop.type = FrameType::StopSending;
  stop.streamID = 10;
  stop.errorCode = 17;
  rxControl.frames.push(stop);

  ZquicLogFrameEvent maxStreamData;
  maxStreamData.type = FrameType::MaxStreamData;
  maxStreamData.streamID = 10;
  maxStreamData.value = 8192;
  rxControl.frames.push(maxStreamData);

  ZquicLogFrameEvent newCID;
  newCID.type = FrameType::NewConnectionID;
  newCID.offset = 3;
  newCID.value = 4;
  newCID.length = 8;
  rxControl.frames.push(newCID);

  ZquicLogFrameEvent retireCID;
  retireCID.type = FrameType::RetireConnectionID;
  retireCID.value = 2;
  rxControl.frames.push(retireCID);

  ZquicLogFrameEvent challenge;
  challenge.type = FrameType::PathChallenge;
  challenge.length = 8;
  rxControl.frames.push(challenge);

  ZquicLogFrameEvent close;
  close.type = FrameType::ConnectionClose;
  close.errorCode = 0x100;
  close.length = 12;
  rxControl.frames.push(close);

  ZuCHECK(rxControl.frames.length() == 7, "control frame setup incomplete");
  ZuCHECK(rxControl.frames[1].type == FrameType::StopSending,
    "control frame copy failed");
  ZquicLogPacketEvent rxMove = ZuMv(rxControl);
  ZuCHECK(rxMove.frames.length() == 7, "control frame move length failed");
  ZuCHECK(rxMove.frames[1].type == FrameType::StopSending,
    "control frame move failed");
  ZquicLogger::packetReceived(ZuMv(rxMove));

  ZquicLogPacketEvent buffered;
  buffered.packetType = PktType::Initial;
  buffered.packetSpace = PktNumSpace::Initial;
  buffered.packetSize = 1200;
  buffered.reason = ZquicLogPacketEvent::Reason::Coalescing;
  ZquicLogger::packetBuffered(ZuMv(buffered));

  ZquicLogPacketEvent drop;
  drop.packetType = PktType::Initial;
  drop.packetSpace = PktNumSpace::Initial;
  drop.packetSize = 50;
  drop.reason = ZquicLogPacketEvent::Reason::ParseLong;
  ZquicLogger::packetDropped(ZuMv(drop));

  ZquicLogger::stop();
  ZquicLogDiag diag = ZquicLogger::diag();
  ZquicLogger::final();

  ZuCHECK(diag.recordsEnqueued >= 7, "typed qlog enqueue mismatch");
  ZuCHECK(diag.recordsWritten >= 8, "typed qlog write mismatch");
  ZuCHECK(diag.writerFailures == 0, "typed qlog writer failure");

  ZtString<> data = readFile_(path);
  ZuCHECK(data, "typed qlog output was not written");
  ZuCHECK(parseJSONSeq_(data) >= 8, "typed qlog JSON-SEQ parse failed");
  ZuCHECK(containsDatagramReceived_(data), "datagram event missing");
  ZuCHECK(containsDatagramSent_(data), "datagram sent event missing");
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
  ZuCHECK(data.find<"new_connection_id">() >= 0,
    "NEW_CONNECTION_ID summary missing");
  ZuCHECK(data.find<"retire_connection_id">() >= 0,
    "RETIRE_CONNECTION_ID summary missing");
  ZuCHECK(data.find<"path_challenge">() >= 0,
    "PATH_CHALLENGE summary missing");
  ZuCHECK(data.find<"connection_close">() >= 0,
    "CONNECTION_CLOSE summary missing");
  ZuCHECK(data.find<"error_code">() >= 0, "frame error code missing");
  ZuCHECK(data.find<"value">() >= 0, "frame value missing");
  ZuCHECK(data.find<"crypto">() >= 0, "crypto frame summary missing");
  ZuCHECK(data.find<"stream_id">() >= 0, "stream frame summary missing");
  ZuCHECK(data.find<"ack_delay_us">() >= 0, "ACK delay missing");
  ZuCHECK(data.find<"ect0">() >= 0, "ACK_ECN ECT0 missing");
  ZuCHECK(data.find<"ce">() >= 0, "ACK_ECN CE missing");
  ZuCHECK(data.find<"coalescing">() >= 0, "buffer reason missing");
  ZuCHECK(data.find<"parse_long">() >= 0, "drop reason missing");
  ZiFile::remove(path);
#endif
}

void testQLogTypedRecoveryEvents()
{
  ZuTestScope(testQLogTypedRecoveryEvents);

#ifdef Zquic_DEBUG
  Zi::Path path = testPath_("ZquicLogRecoveryTest.sqlog");
  ZiFile::remove(path);

  ZquicLogParams params;
  params.enabled(true).path(path).thread("zquic-qlog-recovery").ringSize(1<<15);
  ZuCHECK(ZquicLogger::init(params), "recovery qlog init failed");
  ZquicLogger::start();

  ZquicLogAckEvent ack;
  ack.packetSpace = PktNumSpace::AppData;
  ack.largestAcked = 99;
  ack.ackDelayUS = 2500;
  ack.ackedBytes = 3600;
  ack.lostBytes = 0;
  ack.rangeCount = 2;
  ack.ackedFrames = 3;
  ZquicLogger::packetsAcked(ZuMv(ack));

  ZquicLogRecoveryEvent loss{
    .kind = ZquicLogRecoveryKind::Aggregate,
    .packetSpace = PktNumSpace::AppData,
    .reason = ZquicLogRecoveryReason::PacketThreshold,
    .packetNumber = 77,
    .bytes = 1200,
    .bytesInFlight = 2400,
    .frameCount = 1
  };
  ZquicLogger::packetLost(loss);
  ZquicLogger::recoveryPacketLost(loss);
  ZquicLogger::markedForRetransmit(loss);

  ZquicLogRecoveryEvent metrics{
    .kind = ZquicLogRecoveryKind::RTT,
    .packetSpace = PktNumSpace::AppData,
    .latestRTTUS = 18000,
    .smoothedRTTUS = 20000,
    .rttVarianceUS = 3000,
    .minRTTUS = 16000,
    .cwnd = 12000,
    .ssthresh = 64000,
    .bytesInFlight = 3600
  };
  ZquicLogger::metricsUpdated(ZuMv(metrics));

	  ZquicLogRecoveryEvent timer{
	    .kind = ZquicLogRecoveryKind::PTO,
	    .packetSpace = PktNumSpace::Handshake,
	    .reason = ZquicLogRecoveryReason::Armed,
	    .deadlineUS = 123456
	  };
	  ZquicLogger::lossTimerUpdated(ZuMv(timer));

	  ZquicLogRecoveryEvent ptoExpired{
	    .kind = ZquicLogRecoveryKind::PTO,
	    .packetSpace = PktNumSpace::AppData,
	    .reason = ZquicLogRecoveryReason::Expired,
	    .value = 2,
	    .bytesInFlight = 2400
	  };
	  ZquicLogger::lossTimerUpdated(ZuMv(ptoExpired));

	  ZquicLogRecoveryEvent ptoBackoff{
	    .kind = ZquicLogRecoveryKind::PTO,
	    .packetSpace = PktNumSpace::AppData,
	    .reason = ZquicLogRecoveryReason::Backoff,
	    .value = 3,
	    .bytes = 2,
	    .bytesInFlight = 2400
	  };
	  ZquicLogger::lossTimerUpdated(ZuMv(ptoBackoff));

	  ZquicLogRecoveryEvent ptoProbe{
	    .kind = ZquicLogRecoveryKind::PTO,
	    .packetSpace = PktNumSpace::AppData,
	    .reason = ZquicLogRecoveryReason::Probe,
	    .value = 3,
	    .bytes = 2
	  };
	  ZquicLogger::lossTimerUpdated(ZuMv(ptoProbe));

  ZquicLogRecoveryEvent congestion{
    .kind = ZquicLogRecoveryKind::NewReno,
    .packetSpace = PktNumSpace::AppData,
    .reason = ZquicLogRecoveryReason::Ack,
    .cwnd = 13200,
    .ssthresh = 64000,
    .bytesInFlight = 1200
  };
  ZquicLogger::congestionStateUpdated(ZuMv(congestion));

  ZquicLogECNEvent ecn{
    .packetSpace = PktNumSpace::AppData,
    .state = ZquicLogECNState::Validated,
    .reason = ZquicLogECNReason::AckECN,
    .ect0 = 7,
    .ect1 = 1,
    .ce = 2,
    .previousECT0 = 6,
    .previousECT1 = 1,
    .previousCE = 1,
    .largestAcked = 12
  };
  ZquicLogger::ecnStateUpdated(ZuMv(ecn));

  ZquicLogECNEvent fallback{
    .packetSpace = PktNumSpace::AppData,
    .state = ZquicLogECNState::Disabled,
    .reason = ZquicLogECNReason::CounterExceedsAck,
    .ect0 = 20,
    .ce = 1,
    .previousECT0 = 7,
    .previousECT1 = 1,
    .previousCE = 2,
    .largestAcked = 12,
    .disabled = true
  };
  ZquicLogger::ecnStateUpdated(ZuMv(fallback));

  ZquicLogger::stop();
  ZquicLogDiag diag = ZquicLogger::diag();
  ZquicLogger::final();

	  ZuCHECK(diag.recordsEnqueued >= 12, "recovery qlog enqueue mismatch");
	  ZuCHECK(diag.recordsWritten >= 13, "recovery qlog write mismatch");
  ZuCHECK(diag.writerFailures == 0, "recovery qlog writer failure");

  ZtString<> data = readFile_(path);
  ZuCHECK(data, "recovery qlog output was not written");
	  ZuCHECK(parseJSONSeq_(data) >= 13, "recovery qlog JSON-SEQ parse failed");
	  ZuCHECK(containsRecoveryEvents_(data), "recovery event coverage missing");
	  ZuCHECK(containsRecoveryFields_(data), "recovery fields missing");
	  ZuCHECK(data.find<"packet_threshold">() >= 0, "loss reason missing");
	  ZuCHECK(data.find<"expired">() >= 0, "PTO expiry reason missing");
	  ZuCHECK(data.find<"backoff">() >= 0, "PTO backoff reason missing");
	  ZuCHECK(data.find<"probe">() >= 0, "PTO probe reason missing");
	  ZuCHECK(data.find<"counter_exceeds_ack">() >= 0, "ECN reason missing");
  ZiFile::remove(path);
#endif
}

void testQLogTypedSecurityEvents()
{
  ZuTestScope(testQLogTypedSecurityEvents);

#ifdef Zquic_DEBUG
  Zi::Path path = testPath_("ZquicLogSecurityTest.sqlog");
  ZiFile::remove(path);

  ZquicLogParams params;
  params.enabled(true).path(path).thread("zquic-qlog-security").ringSize(1<<15);
  ZuCHECK(ZquicLogger::init(params), "security qlog init failed");
  ZquicLogger::start();

  ZquicLogSecurityEvent key{
    .kind = ZquicLogSecurityKind::KeyUpdated,
    .packetSpace = PktNumSpace::AppData,
    .keyType = ZquicLogSecurityKeyType::RX,
    .trigger = ZquicLogSecurityTrigger::Remote,
    .reason = ZquicLogSecurityReason::KeyPhase
  };
  ZquicLogger::keyUpdated(ZuMv(key));

  ZquicLogSecurityEvent retired{
    .kind = ZquicLogSecurityKind::KeyRetired,
    .packetSpace = PktNumSpace::Handshake,
    .keyType = ZquicLogSecurityKeyType::TX,
    .trigger = ZquicLogSecurityTrigger::HandshakeComplete,
    .reason = ZquicLogSecurityReason::PacketSpace
  };
  ZquicLogger::keyRetired(ZuMv(retired));

  ZquicLogSecurityEvent paramsSet{
    .kind = ZquicLogSecurityKind::TransportParameters,
    .trigger = ZquicLogSecurityTrigger::Peer,
    .value = 1350
  };
  ZquicLogger::transportParametersSet(ZuMv(paramsSet));

  ZquicLogSecurityEvent alpn{
    .kind = ZquicLogSecurityKind::ALPN,
    .trigger = ZquicLogSecurityTrigger::Selected,
    .alpn = "h3"
  };
  ZquicLogger::alpnInformation(ZuMv(alpn));

  ZquicLogSecurityEvent alert{
    .kind = ZquicLogSecurityKind::TLS,
    .reason = ZquicLogSecurityReason::Handshake,
    .success = false
  };
  ZquicLogger::tlsAlert(ZuMv(alert));

  ZquicLogSecurityEvent retry{
    .kind = ZquicLogSecurityKind::Retry,
    .trigger = ZquicLogSecurityTrigger::Sent,
    .reason = ZquicLogSecurityReason::AddressValidation,
    .value = 42
  };
  ZquicLogger::securityEvent("security:retry_sent", retry);

  ZquicLogger::securityEvent("security:retry_validated",
    ZquicLogSecurityEvent{
      .kind = ZquicLogSecurityKind::Retry,
      .trigger = ZquicLogSecurityTrigger::Received,
      .reason = ZquicLogSecurityReason::OK,
      .value = 42
    });

  ZquicLogSecurityEvent token{
    .kind = ZquicLogSecurityKind::Token,
    .trigger = ZquicLogSecurityTrigger::Validated,
    .reason = ZquicLogSecurityReason::OK,
    .value = 38
  };
  ZquicLogger::securityEvent("security:token_validated", token);

  ZquicLogger::securityEvent("security:token_issued",
    ZquicLogSecurityEvent{
      .kind = ZquicLogSecurityKind::Token,
      .trigger = ZquicLogSecurityTrigger::Sent,
      .reason = ZquicLogSecurityReason::NewToken,
      .value = 38
    });

  ZquicLogger::securityEvent("security:token_rejected",
    ZquicLogSecurityEvent{
      .kind = ZquicLogSecurityKind::Token,
      .trigger = ZquicLogSecurityTrigger::Validated,
      .reason = ZquicLogSecurityReason::Expired,
      .value = 38,
      .success = false
    });

  ZquicLogSecurityEvent vn{
    .kind = ZquicLogSecurityKind::VersionNegotiation,
    .trigger = ZquicLogSecurityTrigger::Sent,
    .reason = ZquicLogSecurityReason::UnsupportedVersion,
    .value = 0x1a2a3a4a
  };
  ZquicLogger::securityEvent("security:version_negotiation", ZuMv(vn));

  ZquicLogSecurityEvent reset{
    .kind = ZquicLogSecurityKind::StatelessReset,
    .trigger = ZquicLogSecurityTrigger::Received,
    .reason = ZquicLogSecurityReason::TokenMatch,
    .value = 43
  };
  ZquicLogger::securityEvent("security:stateless_reset", ZuMv(reset));

  ZquicLogSecurityEvent protection{
    .kind = ZquicLogSecurityKind::PacketProtection,
    .packetSpace = PktNumSpace::AppData,
    .trigger = ZquicLogSecurityTrigger::RX,
    .reason = ZquicLogSecurityReason::InvalidKeyPhase,
    .success = false
  };
  ZquicLogger::securityEvent(
    "security:packet_protection_failed", ZuMv(protection));

  ZquicLogger::stop();
  ZquicLogDiag diag = ZquicLogger::diag();
  ZquicLogger::final();

  ZuCHECK(diag.recordsEnqueued >= 13, "security qlog enqueue mismatch");
  ZuCHECK(diag.recordsWritten >= 14, "security qlog write mismatch");
  ZuCHECK(diag.writerFailures == 0, "security qlog writer failure");

  ZtString<> data = readFile_(path);
  ZuCHECK(data, "security qlog output was not written");
  ZuCHECK(parseJSONSeq_(data) >= 14, "security qlog JSON-SEQ parse failed");
  ZuCHECK(containsSecurityEvents_(data), "security event coverage missing");
  ZuCHECK(containsSecurityFields_(data), "security fields missing");
  ZuCHECK(data.find<"key_phase">() >= 0, "key update reason missing");
  ZuCHECK(data.find<"h3">() >= 0, "ALPN value missing");
  ZuCHECK(data.find<"address_validation">() >= 0,
    "Retry qlog reason missing");
  ZuCHECK(data.find<"unsupported_version">() >= 0,
    "Version Negotiation qlog reason missing");
  ZuCHECK(data.find<"token_match">() >= 0,
    "stateless reset qlog reason missing");
  ZuCHECK(data.find<"invalid_key_phase">() >= 0,
    "packet protection qlog reason missing");
  ZiFile::remove(path);
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
  ZuCHECK(ZquicLogger::init(params), "path qlog init failed");
  ZquicLogger::start();

  ZquicLogPathEvent pathEvent{
    .kind = ZquicLogPathKind::Path,
    .action = ZquicLogPathAction::Updated,
    .reason = ZquicLogPathReason::Promoted,
    .bytes = 1200,
    .antiAmplification = 2400,
    .deadlineUS = 1000000,
    .mtu = 1350,
    .validated = true
  };
  ZquicLogger::pathUpdated(ZuMv(pathEvent));

  ZquicLogPathEvent validation{
    .kind = ZquicLogPathKind::PathValidation,
    .action = ZquicLogPathAction::ChallengeSent,
    .reason = ZquicLogPathReason::PeerAddressChange,
    .deadlineUS = 2000000,
    .mtu = 1350
  };
  ZquicLogger::pathValidationUpdated(ZuMv(validation));

  ZquicLogPathEvent pmtud{
    .kind = ZquicLogPathKind::PMTUD,
    .action = ZquicLogPathAction::Sent,
    .reason = ZquicLogPathReason::Probe,
    .mtu = 1400,
    .validated = true
  };
  ZquicLogger::pmtudUpdated(ZuMv(pmtud));

  ZquicLogCIDEvent cid{
    .kind = ZquicLogCIDKind::ConnectionID,
    .action = ZquicLogCIDAction::RouteBound,
    .reason = ZquicLogCIDReason::PathPromoted,
    .sequence = 7,
    .length = 8,
    .local = false,
    .associated = true,
    .resetToken = true
  };
  ZquicLogger::cidUpdated(ZuMv(cid));

  ZquicLogger::stop();
  ZquicLogDiag diag = ZquicLogger::diag();
  ZquicLogger::final();

  ZuCHECK(diag.recordsEnqueued >= 4, "path qlog enqueue mismatch");
  ZuCHECK(diag.recordsWritten >= 5, "path qlog write mismatch");
  ZuCHECK(diag.writerFailures == 0, "path qlog writer failure");

  ZtString<> data = readFile_(path);
  ZuCHECK(data, "path qlog output was not written");
  ZuCHECK(parseJSONSeq_(data) >= 5, "path qlog JSON-SEQ parse failed");
  ZuCHECK(containsPathCIDEvents_(data), "path/CID event coverage missing");
  ZuCHECK(containsPathCIDFields_(data), "path/CID fields missing");
  ZuCHECK(data.find<"path_promoted">() >= 0, "CID reason missing");
  ZuCHECK(data.find<"probe">() >= 0, "PMTUD reason missing");
  ZiFile::remove(path);
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
  ZuCHECK(ZquicLogger::init(params), "stream qlog init failed");
  ZquicLogger::start();

  ZquicLogStreamEvent open{
    .streamType = ZquicLogStreamType::Bidirectional,
    .oldState = ZquicLogStreamState::Idle,
    .newState = ZquicLogStreamState::Open,
    .streamSide = ZquicLogStreamSide::Sending,
    .reason = ZquicLogStreamReason::LocalOpen,
    .streamID = 4
  };
  ZquicLogger::streamStateUpdated(ZuMv(open));

  ZquicLogStreamEvent closed{
    .streamType = ZquicLogStreamType::Bidirectional,
    .oldState = ZquicLogStreamState::Open,
    .newState = ZquicLogStreamState::Closed,
    .streamSide = ZquicLogStreamSide::Receiving,
    .reason = ZquicLogStreamReason::Reaped,
    .streamID = 4,
    .offset = 128,
    .length = 32,
    .fin = true
  };
  ZquicLogger::streamStateUpdated(ZuMv(closed));

  ZquicLogStreamDataEvent moved{
    .from = ZquicLogStreamDataLoc::Application,
    .to = ZquicLogStreamDataLoc::Transport,
    .streamID = 4,
    .offset = 64,
    .length = 32
  };
  ZquicLogger::streamDataMoved(ZuMv(moved));

  ZquicLogStreamDataEvent sent{
    .from = ZquicLogStreamDataLoc::Transport,
    .to = ZquicLogStreamDataLoc::Network,
    .additionalInfo = ZquicLogStreamDataInfo::FinSet,
    .streamID = 4,
    .offset = 64,
    .length = 32
  };
  ZquicLogger::streamDataMoved(ZuMv(sent));

  ZquicLogger::connectionDataBlockedUpdated(
    ZquicLogBlockedEvent{
      .oldState = ZquicLogBlockedState::Unblocked,
      .newState = ZquicLogBlockedState::Blocked,
      .reason = ZquicLogBlockedReason::ConnectionFlowControl
    });
  ZquicLogger::streamDataBlockedUpdated(
    ZquicLogBlockedEvent{
      .oldState = ZquicLogBlockedState::Blocked,
      .newState = ZquicLogBlockedState::Unblocked,
      .reason = ZquicLogBlockedReason::StreamFlowControl,
      .streamID = 4
    });

  ZquicLogger::stop();
  ZquicLogDiag diag = ZquicLogger::diag();
  ZquicLogger::final();

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
  ZiFile::remove(path);
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
  ZuCHECK(ZquicLogger::init(params), "close qlog init failed");
  ZquicLogger::start();

  ZquicLogCloseEvent local{
    .initiator = ZquicLogCloseInitiator::Local,
    .trigger = ZquicLogCloseTrigger::Application,
    .reason = ZquicLogCloseReason::LocalClose,
    .applicationError = ZquicLogCloseError::Unknown,
    .errorCode = 42,
    .application = true,
    .frame = true
  };
  ZquicLogger::connectionClosed(ZuMv(local));

  ZquicLogCloseEvent idle{
    .initiator = ZquicLogCloseInitiator::Local,
    .trigger = ZquicLogCloseTrigger::IdleTimeout,
    .reason = ZquicLogCloseReason::Idle,
    .connectionError = ZquicLogCloseError::NoError,
    .errorCode = 0
  };
  ZquicLogger::connectionClosed(ZuMv(idle));

  ZquicLogger::stop();
  ZquicLogDiag diag = ZquicLogger::diag();
  ZquicLogger::final();

  ZuCHECK(diag.recordsEnqueued >= 2, "close qlog enqueue mismatch");
  ZuCHECK(diag.recordsWritten >= 3, "close qlog write mismatch");
  ZuCHECK(diag.writerFailures == 0, "close qlog writer failure");

  ZtString<> data = readFile_(path);
  ZuCHECK(data, "close qlog output was not written");
  ZuCHECK(parseJSONSeq_(data) >= 3, "close qlog JSON-SEQ parse failed");
  ZuCHECK(containsCloseEvents_(data), "close event coverage missing");
  ZuCHECK(containsCloseFields_(data), "close fields missing");
  ZuCHECK(data.find<"local_close">() >= 0, "local close reason missing");
  ZuCHECK(data.find<"idle_timeout">() >= 0, "idle close trigger missing");
  ZiFile::remove(path);
#endif
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testQLogFileOutput);
  ZuTestCall(testQLogBackPressure);
  ZuTestCall(testQLogTypedTransportEvents);
  ZuTestCall(testQLogTypedRecoveryEvents);
  ZuTestCall(testQLogTypedSecurityEvents);
  ZuTestCall(testQLogTypedPathCIDEvents);
  ZuTestCall(testQLogTypedStreamEvents);
  ZuTestCall(testQLogTypedCloseEvents);
}
