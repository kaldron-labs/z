//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>

#include <limits.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmBlock.hh>
#include <zlib/ZtString.hh>
#include <zlib/Zquic.hh>

using namespace ZuTestUtil;

namespace {

constexpr unsigned TLSBufSize = (64<<10); // 64K
constexpr unsigned MaxTLSMessages = 32;

#ifdef Zquic_DEBUG
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

static void closeQLog_(ZquicLogger::Trace &trace)
{
  ZmBlock<>{}([&trace](auto wake) {
    ZquicLogger::close(trace, ZuMv(wake));
  });
}
#endif

ZuBSpan bytes_(const uint8_t *data, unsigned len)
{
  return ZuBSpan{data, len};
}

struct TempDir {
  char		path[PATH_MAX]{};
  ZtString<>	certPath;
  ZtString<>	keyPath;

  ~TempDir() { cleanup(); }

  bool init()
  {
    strcpy(path, "/tmp/ZquicHandshakeTest.XXXXXX");
    if (!mkdtemp(path)) return false;
    certPath << static_cast<const char *>(path) << "/cert.pem";
    keyPath << static_cast<const char *>(path) << "/key.pem";

    ZtString<> cmd;
    cmd <<
      "openssl req -x509 -newkey rsa:2048 -nodes -days 1 "
      "-subj /CN=localhost "
      "-addext basicConstraints=critical,CA:TRUE "
      "-addext keyUsage=critical,digitalSignature,keyEncipherment,keyCertSign "
      "-addext subjectAltName=DNS:localhost,IP:127.0.0.1 "
      "-keyout " << keyPath << ' ' <<
      "-out " << certPath << " >/dev/null 2>&1";
    return systemOK(system(cmd.data()));
  }

  ZtString<> pathOf(const char *name) const
  {
    ZtString<> s;
    s << static_cast<const char *>(path) << '/' << name;
    return s;
  }

  static bool systemOK(int status)
  {
    return status != -1 && WIFEXITED(status) && !WEXITSTATUS(status);
  }

  void cleanup()
  {
    if (!path[0]) return;
    const char *names[] = { "cert.pem", "key.pem", nullptr };
    for (auto name = names; *name; ++name) {
      auto p = pathOf(*name);
      unlink(p.data());
    }
    rmdir(path);
    path[0] = 0;
  }
};

struct TLSMessage {
  size_t	epoch = 0;
  unsigned	length = 0;
  uint8_t	data[TLSBufSize]{};
};

struct TLSPeer {
  Zquic::Crypto		*crypto = nullptr;
  Zquic::CryptoStream	tx[4];
  Zquic::CryptoStream	rx[4];
};

struct TLSQueue {
  TLSMessage	messages[MaxTLSMessages];
  unsigned	count = 0;

  bool push(size_t epoch, const uint8_t *data, unsigned len)
  {
    if (count >= MaxTLSMessages || len > TLSBufSize) return false;
    messages[count].epoch = epoch;
    messages[count].length = len;
    if (len) memcpy(messages[count].data, data, len);
    ++count;
    return true;
  }

  bool pop(TLSMessage &msg)
  {
    if (!count) return false;
    msg = messages[0];
    if (--count)
      memmove(messages, messages + 1, count * sizeof(TLSMessage));
    return true;
  }
};

bool enqueueFlight_(TLSPeer &peer, TLSQueue &queue, const uint8_t *data,
    size_t offsets[5])
{
  for (size_t epoch = 0; epoch < 4; ++epoch) {
    if (offsets[epoch + 1] <= offsets[epoch]) continue;
    unsigned off = unsigned(offsets[epoch]);
    unsigned len = unsigned(offsets[epoch + 1] - offsets[epoch]);
    uint8_t frame[TLSBufSize];
    int n = peer.tx[epoch].writeFrame(
      frame, sizeof(frame), bytes_(data + off, len));
    if (n < 0 || !queue.push(epoch, frame, unsigned(n))) return false;
  }
  return true;
}

bool handleTLS_(
  TLSPeer &dst, TLSQueue &out, const TLSMessage *msg)
{
  ZmRef<ZiIOBuf> buf =
    new Zquic::CryptoTxBufAlloc<TLSBufSize, TLSBufSize>{nullptr};
  size_t offsets[5] = {};
  ZuBSpan in;
  size_t inEpoch = msg ? msg->epoch : 0;
  if (msg) {
    Zquic::Frame frame;
    unsigned used = 0;
    if (Zquic::FrameCodec::parse(bytes_(msg->data, msg->length), frame, used) ||
	used != msg->length)
      return false;
    if (dst.rx[inEpoch].receiveFrame(frame, in))
      return false;
    if (!in) return true;
  }
  int n = dst.crypto->handleTLSMessage(buf.ptr(), offsets, inEpoch, in);
  return n >= 0 && enqueueFlight_(dst, out, buf->data(), offsets);
}

bool drainTLS_(TLSPeer &dst, TLSQueue &in, TLSQueue &out)
{
  TLSMessage msg;
  while (in.pop(msg)) {
    if (dst.crypto->tlsReadEpoch() != msg.epoch) return false;
    if (!handleTLS_(dst, out, &msg)) return false;
  }
  return true;
}

bool exchangeTLS_(Zquic::Crypto &client, Zquic::Crypto &server)
{
  TLSPeer c{&client};
  TLSPeer s{&server};
  TLSQueue c2s, s2c;
  if (!handleTLS_(c, c2s, nullptr)) return false;
  for (unsigned round = 0; round < 16; ++round) {
    if (!drainTLS_(s, c2s, s2c)) return false;
    if (!drainTLS_(c, s2c, c2s)) return false;
    if (client.oneRTTReady() && server.oneRTTReady())
      return true;
    if (!c2s.count && !s2c.count) break;
  }
  return client.oneRTTReady() && server.oneRTTReady();
}

} // namespace

void testDeterministicHSProfile()
{
  ZuTestScope(testDeterministicHSProfile);

  Zquic::CxnID dcid{"client01"};
  Zquic::Crypto client;
  Zquic::Crypto server;
  ZuCHECK(client.init(Zquic::CryptoConfig{false, false, "h3"}),
    "client crypto init failed");
  ZuCHECK(server.init(Zquic::CryptoConfig{true, false, "h3"}),
    "server crypto init failed");
  ZuCHECK(client.alpn() == "h3" && server.alpn() == "h3",
    "handshake ALPN mismatch");
  ZuCHECK(!client.earlyDataEnabled() && !server.earlyDataEnabled(),
    "0-RTT was enabled");
  ZuCHECK(!server.diag().zeroRTTRejected,
    "server 0-RTT rejection changed unexpectedly");

  ZuCHECK(client.deriveInitial(dcid) && server.deriveInitial(dcid),
    "Initial key derivation failed");
  ZuCHECK(!memcmp(
      client.initialKeys().client.key.data(),
      server.initialKeys().client.key.data(),
      Zquic::InitialSecret::KeyLen),
    "client Initial keys differ across peers");
  ZuCHECK(!memcmp(
      client.initialKeys().server.key.data(),
      server.initialKeys().server.key.data(),
      Zquic::InitialSecret::KeyLen),
    "server Initial keys differ across peers");

  client.installSecret(Zquic::PktNumSpace::Initial, "client-initial");
  server.installSecret(Zquic::PktNumSpace::Initial, "server-initial");
  client.installSecret(Zquic::PktNumSpace::Handshake, "client-hs");
  server.installSecret(Zquic::PktNumSpace::Handshake, "server-hs");
  client.installSecret(Zquic::PktNumSpace::AppData, "client-app");
  server.installSecret(Zquic::PktNumSpace::AppData, "server-app");
  ZuCHECK(client.completeHandshake() && server.completeHandshake(),
    "1-RTT readiness not reached");
}

void testCryptoStreamFrames()
{
  ZuTestScope(testCryptoStreamFrames);

  uint8_t frame[64];
  Zquic::CryptoDiag diag;
  Zquic::CryptoStream tx;
  int n = tx.writeFrame(frame, sizeof(frame), "abc", &diag);
  ZuCHECK(n > 0 && tx.txOffset() == 3 &&
      diag.cryptoBytesTx == 3,
    "CRYPTO Tx frame accounting failed");

  Zquic::Frame parsed;
  unsigned used = 0;
  ZuCHECK(!Zquic::FrameCodec::parse(
      bytes_(frame, unsigned(n)), parsed, used) &&
      used == unsigned(n) &&
      parsed.type == Zquic::FrameType::Crypto &&
      parsed.offset == 0 && parsed.payload == "abc",
    "CRYPTO Tx frame parse failed");
  ZuBSpan txPayload;
  ZuCHECK(tx.txPayload(0, 3, txPayload) && txPayload == "abc",
    "CRYPTO Tx payload retention failed");
  ZuCHECK(!tx.txPayload(2, 3, txPayload),
    "CRYPTO Tx payload retention accepted overrun");

  Zquic::CryptoStream directRx;
  Zquic::CryptoDiag directDiag;
  static const char directPayload[] = "direct";
  ZuBSpan directIn{directPayload, unsigned(sizeof(directPayload) - 1)};
  ZuBSpan directOut;
  ZuCHECK(!directRx.receive(0, directIn, directOut, &directDiag) &&
      directOut.data() == directIn.data() &&
      directOut.length() == directIn.length() &&
      directRx.rxOffset() == directIn.length() &&
      !directRx.rangeCount() &&
      directDiag.cryptoBytesRx == directIn.length(),
    "in-order CRYPTO fast path copied payload");

  uint8_t first[64];
  uint8_t second[64];
  int f1 = Zquic::FrameCodec::writeCrypto(first, sizeof(first), 0, "hello");
  int f2 = Zquic::FrameCodec::writeCrypto(second, sizeof(second), 5, "world");
  ZuCHECK(f1 > 0 && f2 > 0, "CRYPTO frame setup failed");

  Zquic::CryptoStream rx;
  ZuBSpan contiguous;
  ZuCHECK(!Zquic::FrameCodec::parse(bytes_(second, unsigned(f2)),
      parsed, used) &&
      !rx.receiveFrame(parsed, contiguous, &diag) &&
      !contiguous && rx.rxOffset() == 0 && rx.rangeCount() == 1,
    "out-of-order CRYPTO frame was delivered");
  ZuCHECK(!Zquic::FrameCodec::parse(bytes_(first, unsigned(f1)),
      parsed, used) &&
      !rx.receiveFrame(parsed, contiguous, &diag) &&
      contiguous == "helloworld" && rx.rxOffset() == 10 &&
      !rx.rangeCount(),
    "CRYPTO reassembly failed");
  ZuCHECK(!rx.receive(0, "hello", contiguous, &diag) &&
      !contiguous && rx.rxOffset() == 10,
    "duplicate CRYPTO data was delivered");
  ZuCHECK(diag.cryptoBytesRx == 10,
    "CRYPTO Rx diagnostics failed");
}

void testMessageLevelTLSHandshake()
{
  ZuTestScope(testMessageLevelTLSHandshake);

  TempDir temp;
  ZuCHECK(temp.init(), "temporary TLS certificate generation failed");

  Zquic::TransportParams clientParams;
  clientParams.maxUDPPayloadSize = 1400;
  clientParams.initialMaxData = 8192;
  clientParams.initialMaxStreamsBidi = 8;
  clientParams.initialSCID = "clienttp";

  Zquic::TransportParams serverParams;
  serverParams.maxUDPPayloadSize = 1350;
  serverParams.initialMaxData = 16384;
  serverParams.initialMaxStreamsUni = 4;
  serverParams.initialSCID = "servertp";

  Zquic::Crypto client;
  Zquic::Crypto server;
  ZuCHECK(client.initTLS(Zquic::CryptoConfig{
      false, false, "h3", temp.certPath.cspan(), {}, {}, {}, "localhost",
      &clientParams}),
    "client TLS init failed");
  ZuCHECK(server.initTLS(Zquic::CryptoConfig{
      true, false, "h3", {}, temp.certPath.cspan(), temp.keyPath.cspan(), {},
      {}, &serverParams}),
    "server TLS init failed");
  ZuCHECK(!client.earlyDataEnabled() && !server.earlyDataEnabled(),
    "0-RTT enabled in TLS message path");

  ZuCHECK(exchangeTLS_(client, server),
    "message-level TLS handshake did not complete");
  ZuCHECK(client.oneRTTReady() && server.oneRTTReady(),
    "1-RTT readiness not set after TLS handshake");
  ZuCHECK(client.secretInstalled(Zquic::PktNumSpace::Handshake) &&
      client.secretInstalled(Zquic::PktNumSpace::AppData) &&
      server.secretInstalled(Zquic::PktNumSpace::Handshake) &&
      server.secretInstalled(Zquic::PktNumSpace::AppData),
    "TLS traffic secrets not recorded");
  ZuCHECK(client.txSecretInstalled(Zquic::PktNumSpace::AppData) &&
      client.rxSecretInstalled(Zquic::PktNumSpace::AppData) &&
      server.txSecretInstalled(Zquic::PktNumSpace::AppData) &&
      server.rxSecretInstalled(Zquic::PktNumSpace::AppData),
    "TLS traffic keys not derived");
  ZuCHECK(client.txSecret(Zquic::PktNumSpace::AppData).tagLen == 16 &&
      server.rxSecret(Zquic::PktNumSpace::AppData).tagLen == 16,
    "TLS traffic key linkInfo mismatch");

  uint8_t payload[32] = {};
  ZuCHECK(Zquic::FrameCodec::writePing(payload, sizeof(payload)) == 1,
    "1-RTT traffic probe frame encode failed");
  Zquic::CxnID dcid{"server01"};
  uint8_t header[128];
  int h = Zquic::Pkt::writeShort(header, sizeof(header), dcid, 3, 2);
  ZuCHECK(h > 0, "1-RTT traffic probe short header encode failed");
  unsigned pnOffset = unsigned(h) - 2;
  uint8_t packet[256];
  ptls_iovec_t plain = ptls_iovec_init(payload, sizeof(payload));
  int n = Zquic::PktProt::protectShortV(
    packet, sizeof(packet), client.txProtState(Zquic::PktNumSpace::AppData),
    3, bytes_(header, unsigned(h)), &plain, 1, pnOffset, 2);
  ZuCHECK(n == int(unsigned(h) + sizeof(payload) + 16),
    "1-RTT traffic probe protection failed");
  uint64_t pn = 0;
  unsigned payloadOffset = 0;
  int plainLen = Zquic::PktProt::unprotectShort(
    packet, unsigned(n), server.rxProtState(Zquic::PktNumSpace::AppData),
    0, pnOffset, pn, payloadOffset);
  ZuCHECK(plainLen == int(sizeof(payload)) &&
      pn == 3 && payloadOffset == unsigned(h) &&
      !memcmp(packet + payloadOffset, payload, sizeof(payload)),
    "1-RTT traffic probe decrypt failed");
  ZuCHECK(client.negotiatedProtocol() == "h3" &&
      server.negotiatedProtocol() == "h3",
    "ALPN negotiation failed");
  ZuCHECK(client.peerParamsSet() &&
      server.peerParamsSet(),
    "TLS transport parameters were not collected");
  ZuCHECK(client.peerParams().maxUDPPayloadSize == 1350 &&
      client.peerParams().initialMaxData == 16384 &&
      client.peerParams().initialSCID == serverParams.initialSCID,
    "client did not collect server transport parameters");
  ZuCHECK(server.peerParams().maxUDPPayloadSize == 1400 &&
      server.peerParams().initialMaxData == 8192 &&
      server.peerParams().initialSCID == clientParams.initialSCID,
    "server did not collect client transport parameters");
  ZuCHECK(client.diag().transportParamsEncoded &&
      client.diag().transportParamsDecoded &&
      server.diag().transportParamsEncoded &&
      server.diag().transportParamsDecoded,
    "TLS transport parameter diagnostics were not updated");
  ZuCHECK(!client.diag().zeroRTTRejected &&
      !server.diag().zeroRTTRejected,
    "0-RTT posture counters changed unexpectedly");
  ZuCHECK(client.diag().tlsMessagesHandled &&
      server.diag().tlsMessagesHandled &&
      client.diag().tlsMessagesEmitted &&
      server.diag().tlsMessagesEmitted,
    "TLS message diagnostics were not updated");
}

void testZeroRTTPktDrop()
{
  ZuTestScope(testZeroRTTPktDrop);

  Zquic::CxnID dcid{"client01"};
  Zquic::CxnID scid{"server01"};
  uint8_t packet[128];
  int n = Zquic::Pkt::writeLong(
    packet, sizeof(packet), Zquic::PktType::ZeroRTT, dcid, scid, 8, 2);
  ZuCHECK(n > 0, "0-RTT packet header write failed");
  packet[n++] = 0;
  packet[n++] = 1;

  Zquic::LongHdr h;
  ZuCHECK(Zquic::Pkt::parseLong(
      ZuBSpan{packet, unsigned(n)}, h) > 0,
    "0-RTT packet parse failed");
  ZuCHECK(h.type == Zquic::PktType::ZeroRTT, "0-RTT type not recognized");

  Zquic::Crypto server;
#ifdef Zquic_DEBUG
  Zi::Path qlogPath;
  qlogPath << "ZquicHandshakeZeroRTT.sqlog";
  ZiFile::remove(qlogPath);
	ZquicLogParams params;
	params.enabled(true).path(qlogPath);
	ZquicLogger::Trace trace;
	ZuCHECK(ZquicLogger::init(trace, params, Zquic::Vantage::Server),
	  "0-RTT qlog init failed");
	ZquicLogger::start();
#endif
	ZuCHECK(server.init(Zquic::CryptoConfig{
	    .isServer = true,
	    .enable0RTT = true,
	    .alpn = "h3",
#ifdef Zquic_DEBUG
	    .qlogTrace = &trace
#endif
	  }),
	  "server crypto init failed");
  ZuCHECK(server.rejectZeroRTT(), "0-RTT reject failed");
  ZuCHECK(!server.oneRTTReady() &&
      !server.earlyDataEnabled() &&
      server.earlyDataRejected() &&
      server.diag().zeroRTTRejected == 1,
    "0-RTT reject affected handshake readiness");
#ifdef Zquic_DEBUG
	closeQLog_(trace);
	ZquicLogger::stop();
	ZquicLogger::final(trace);
	ZtString<> qlog = readFile_(qlogPath);
  ZuCHECK(qlog.find<"zquic:zero_rtt_rejected">() >= 0 &&
      qlog.find<"\"reason\":\"0rtt\"">() >= 0 &&
      qlog.find<"\"success\":false">() >= 0,
    "0-RTT rejection qlog output missing");
  if (!::getenv("ZQUIC_TEST_KEEP")) ZiFile::remove(qlogPath);
#endif
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testDeterministicHSProfile);
  ZuTestCall(testCryptoStreamFrames);
  ZuTestCall(testMessageLevelTLSHandshake);
  ZuTestCall(testZeroRTTPktDrop);
}
