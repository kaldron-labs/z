//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/Zquic.hh>
#include <zlib/Zquic.hh>
#include <zlib/ZquicCrypto.hh>
#include <zlib/ZquicPath.hh>
#include <zlib/ZquicRecovery.hh>
#include <zlib/ZquicStream.hh>

using namespace ZuTestUtil;

namespace {

using StreamTxBufAlloc = Zquic::StreamTxBufAlloc<>;

struct App { };
struct TestLink;
struct TestStream :
  public Zquic::Stream<TestLink, TestStream, StreamTxBufAlloc>
{
  using Base = Zquic::Stream<TestLink, TestStream, StreamTxBufAlloc>;
  using Base::Base;
  int process(Zquic::RxStream &) { ++processed; return 0; }
  unsigned processed = 0;
};
struct TestLink :
  public Zquic::Link<App, TestLink,
    StreamTxBufAlloc, TestStream>
{
  using Base = Zquic::Link<App, TestLink,
    StreamTxBufAlloc, TestStream>;
  TestLink(App *app, bool isServer = false) : Base{app, isServer} { }
};

ZuCSpan bytes_(const uint8_t *data, unsigned len)
{
  return ZuCSpan{reinterpret_cast<const char *>(data), len};
}

static ZmRef<ZiIOBuf> streamPacket_(
  uint64_t id, uint64_t offset, ZuCSpan payload, bool fin,
  Zquic::Frame &frame, unsigned &used)
{
  ZmRef<ZiIOBuf> packet = new Zquic::PacketRxBufAlloc<>{nullptr};
  int n = Zquic::FrameCodec::writeStream(
    packet->data_(), packet->size, id, offset, payload, fin);
  if (n <= 0) return nullptr;
  packet->skip = 0;
  packet->length = unsigned(n);
  if (Zquic::FrameCodec::parse(
      ZuCSpan{reinterpret_cast<const char *>(packet->data_()),
	packet->length}, frame, used) ||
      used != packet->length)
    return nullptr;
  return packet;
}

} // namespace

void testHandshakeStreamsAndClose()
{
  ZuTestScope(testHandshakeStreamsAndClose);

  Zquic::CxnID dcid{"client01"};
  Zquic::Crypto clientCrypto;
  Zquic::Crypto serverCrypto;
  ZuCHECK(clientCrypto.init(Zquic::CryptoConfig{false, false, "h3"}) &&
      serverCrypto.init(Zquic::CryptoConfig{true, false, "h3"}),
    "loop crypto init failed");
  ZuCHECK(clientCrypto.deriveInitial(dcid) && serverCrypto.deriveInitial(dcid),
    "loop Initial key derivation failed");

  clientCrypto.installSecret(Zquic::CryptoLevel::OneRTT, "client-app");
  serverCrypto.installSecret(Zquic::CryptoLevel::OneRTT, "server-app");
  ZuCHECK(clientCrypto.completeHandshake() && serverCrypto.completeHandshake(),
    "loop 1-RTT readiness failed");

  App app;
  TestLink client{&app};
  TestLink server{&app, true};
  auto c0 = client.stream(Zquic::StreamType::Bidi);
  auto c1 = client.stream(Zquic::StreamType::Uni);
  auto s0 = server.stream(Zquic::StreamType::Bidi);
  ZuCHECK(c0->id() == 0 && c1->id() == 2 && s0->id() == 1,
    "loop stream IDs mismatch");
  {
    auto tx = c0->txStream_();
    tx << "request-body" << Zi::flush();
  }
  ZuCHECK(c0->txBytes() == 12, "loop stream Tx accounting mismatch");

  client.close(Zquic::TransportError::NoError);
  ZuCHECK(client.closed() && !client.closeError(), "loop graceful close failed");
}

void testSplitReorderedStreamFrames()
{
  ZuTestScope(testSplitReorderedStreamFrames);

  uint8_t p0[700], p1[700], p2[200];
  memset(p0, 'a', sizeof(p0));
  memset(p1, 'b', sizeof(p1));
  memset(p2, 'c', sizeof(p2));
  Zquic::Frame f;
  unsigned used = 0;
  App app;
  TestLink client{&app};
  auto stream = client.stream(Zquic::StreamType::Bidi);

  auto packet = streamPacket_(
    stream->id(), sizeof(p0), bytes_(p1, sizeof(p1)), false, f, used);
  ZuCHECK(packet &&
      f.offset == sizeof(p0) && f.length == sizeof(p1),
    "loop second STREAM frame parse failed");
  ZuCHECK(stream->processFrame(f, packet) == 0 &&
      stream->rxPending() == 1 &&
      !stream->rxBytes(),
    "loop out-of-order stream receive failed");

  packet = streamPacket_(
    stream->id(), 0, bytes_(p0, sizeof(p0)), false, f, used);
  ZuCHECK(packet &&
      f.offset == 0 && f.length == sizeof(p0),
    "loop first STREAM frame parse failed");
  ZuCHECK(stream->processFrame(f, packet) == 0 &&
      stream->rxBytes() == sizeof(p0) + sizeof(p1) &&
      !stream->rxPending(),
    "loop stream gap fill failed");

  packet = streamPacket_(
    stream->id(), sizeof(p0) + sizeof(p1), bytes_(p2, sizeof(p2)), true,
    f, used);
  ZuCHECK(packet &&
      f.offset == sizeof(p0) + sizeof(p1) && f.length == sizeof(p2) && f.fin,
    "loop final STREAM frame parse failed");
  ZuCHECK(stream->processFrame(f, packet) == 0 &&
      stream->rxComplete() &&
      stream->finalSize() == sizeof(p0) + sizeof(p1) + sizeof(p2),
    "loop stream final-size completion failed");
}

void testRecoveryFlowAndPMTUD()
{
  ZuTestScope(testRecoveryFlowAndPMTUD);

  Zquic::FlowCredit credit{1200};
  ZuCHECK(credit.consume(1200) && credit.blocked(),
    "loop flow block failed");
  ZuCHECK(!credit.consume(1), "loop blocked flow allowed data");
  credit.extend(2400);
  ZuCHECK(credit.consume(600) && !credit.blocked(),
    "loop flow unblock failed");

  Zquic::SentPacketTracker sent;
  ZuCHECK(sent.add({1, 100, 1200, Zquic::PacketSpace::AppData,
      true, true, false}) &&
      sent.add({2, 110, 1200, Zquic::PacketSpace::AppData,
      true, true, false}) &&
      sent.add({5, 120, 1200, Zquic::PacketSpace::AppData,
      true, true, false}),
    "loop sent-packet add failed");
  ZuCHECK(sent.ack(5), "loop ACK failed");
  ZuCHECK(sent.markPacketThresholdLoss(5) == 2 &&
      sent.lost() == 2 && sent.retransmittable() == 2,
    "loop packet-threshold loss failed");

  Zquic::Path path = Zquic::Path::client(
    ZiSockAddr{ZiIP("127.0.0.1"), 10000},
    ZiSockAddr{ZiIP("127.0.0.1"), 10001});
  ZuCHECK(!path.canSend(1200), "loop anti-amplification allowed send");
  path.received(1200);
  ZuCHECK(path.canSend(1200), "loop anti-amplification budget failed");
  path.configuredMaxUDP(Zquic::BufSize);
  path.startProbe(1400);
  path.probeAcked();
  ZuCHECK(path.activeMaxUDP() == 1400, "loop PMTUD growth failed");
  path.startProbe(Zquic::BufSize);
  path.probeLost();
  ZuCHECK(path.activeMaxUDP() == 1400 &&
      path.diag().probesLost == 1,
    "loop PMTUD probe loss handling failed");

  Zquic::Path retry = Zquic::Path::client(
    ZiSockAddr{ZiIP("127.0.0.1"), 10002},
    ZiSockAddr{ZiIP("127.0.0.1"), 10003});
  retry.validated();
  ZuCHECK(retry.startNextProbe(128) &&
      retry.probeExpired() &&
      retry.probeRetryPending() &&
      retry.nextProbeSize(64) == retry.retryProbeSize(),
    "loop PMTUD expiry retry setup failed");
  unsigned retrySize = retry.retryProbeSize();
  ZuCHECK(retry.startNextProbe(64) &&
      retry.probeAttempts() == 2 &&
      retry.probeSize() == retrySize &&
      !retry.probeRetryPending(),
    "loop PMTUD expiry retry start failed");

  Zquic::Path blackhole = Zquic::Path::client(
    ZiSockAddr{ZiIP("127.0.0.1"), 10004},
    ZiSockAddr{ZiIP("127.0.0.1"), 10005});
  blackhole.validated();
  blackhole.startProbe(1400);
  blackhole.probeAcked();
  blackhole.startProbe(1400);
  ZuCHECK(blackhole.probeExpired() &&
      blackhole.startNextProbe(64) &&
      blackhole.probeExpired() &&
      blackhole.startNextProbe(64) &&
      blackhole.probeAttempts() == Zquic::Path::MaxProbeAttempts,
    "loop PMTUD active-size expiry retry sequence failed");
  ZuCHECK(!blackhole.probeExpired() &&
      blackhole.activeMaxUDP() == Zquic::MinUDPPayload &&
      blackhole.diag().blackholes == 1,
    "loop PMTUD active-size blackhole fallback failed");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testHandshakeStreamsAndClose);
  ZuTestCall(testSplitReorderedStreamFrames);
  ZuTestCall(testRecoveryFlowAndPMTUD);
}
