//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/Zquic.hh>
#include <zlib/ZtlsEngineOpenSSL.hh>

using namespace ZuTestUtil;

template <typename Policy>
static Zi::IOBufAllocator *allocator()
{
  static ZiMultiplex mx;
  static auto allocator = Zquic::plaintextAllocator<Policy>(&mx);
  return allocator;
}

void testCryptoPosture()
{
  ZuTestScope(testCryptoPosture);

  Zquic::Crypto crypto;
  ZuCHECK(crypto.init(Zquic::CryptoConfig{false, true, "h3"}),
    "crypto init failed");
  ZuCHECK(crypto.alpn() == "h3", "ALPN not retained");
  ZuCHECK(crypto.earlyDataEnabled() &&
      crypto.earlyDataState() == Zquic::EarlyDataState::Enabled,
    "0-RTT enablement not retained");
  ZuCHECK(!crypto.diag().zeroRTTRejected,
    "0-RTT enablement was counted as rejection");

  crypto.installSecret(Zquic::PktNumSpace::Initial, "initial-secret");
  crypto.installSecret(Zquic::PktNumSpace::Handshake, "hs-secret");
  crypto.installSecret(Zquic::PktNumSpace::AppData, "app-secret");
  ZuCHECK(crypto.completeHandshake(), "1-RTT readiness not reached");
  ZuCHECK(!crypto.secretInstalled(Zquic::PktNumSpace::Initial) &&
      !crypto.secretInstalled(Zquic::PktNumSpace::Handshake),
    "lower-level secrets were not discarded");
  ZuCHECK(crypto.secretInstalled(Zquic::PktNumSpace::AppData),
    "1-RTT secret not recorded");
  ZuCHECK(crypto.diag().secretsInstalled == 3 &&
      crypto.diag().secretsDiscarded == 2,
    "secret lifecycle diagnostics mismatch");
}

void testEarlyDataTLSProperties()
{
  ZuTestScope(testEarlyDataTLSProperties);

  Zquic::Crypto crypto;
  ZuCHECK(crypto.initTLS(Zquic::CryptoConfig{
      .isServer = false,
      .enable0RTT = true,
      .alpn = "h3",
      .serverName = "localhost"
    }),
    "client TLS init with 0-RTT failed");
  ZuCHECK(crypto.earlyDataEnabled() &&
      crypto.earlyDataState() == Zquic::EarlyDataState::Enabled,
    "0-RTT TLS init did not retain enabled state");

  ZmRef<ZiIOBuf> out =
    allocator<Zquic::CryptoTxBufAlloc<
      Zquic::Crypto::TLSOutputMax, Zquic::Crypto::TLSOutputMax>>()->allocBuf();
  Zquic::CryptoOffsets offsets;
  int n = crypto.handleTLSMessage(out.ptr(), offsets, 0, {});
  ZuCHECK(n > 0, "client TLS initial output failed");
  ZuCHECK(!crypto.earlyDataEnabled() &&
      crypto.earlyDataState() == Zquic::EarlyDataState::Rejected &&
      !crypto.maxEarlyData() &&
      crypto.diag().zeroRTTRejected == 1,
    "missing 0-RTT ticket did not settle as rejected");
}

void testEarlyTrafficSecretState()
{
  ZuTestScope(testEarlyTrafficSecretState);

  uint8_t secretBytes[32];
  for (unsigned i = 0; i < sizeof(secretBytes); ++i) secretBytes[i] = uint8_t(i);
  Zquic::TrafficSecret secret;
  ZuCHECK(Zquic::PktProt::deriveSecret(
      secret, &Ztls_::opensslAes128gcmsha256,
      Zquic::byteSpan(secretBytes, sizeof(secretBytes))),
    "0-RTT traffic secret derivation failed");

  Zquic::Crypto crypto;
  ZuCHECK(crypto.init(Zquic::CryptoConfig{false, false, "h3"}),
    "crypto init failed");
  ZuCHECK(crypto.txKeySecret(Zquic::PktKeyLevel::ZeroRTT, secret) &&
      crypto.rxKeySecret(Zquic::PktKeyLevel::ZeroRTT, secret),
    "0-RTT traffic secret install failed");
  ZuCHECK(crypto.earlyDataEnabled(),
    "0-RTT key install did not enable early data state");
  ZuCHECK(crypto.earlyDataOffered(),
    "0-RTT key install did not enter offered state");
  ZuCHECK(crypto.txKeySecretInstalled(Zquic::PktKeyLevel::ZeroRTT) &&
      crypto.rxKeySecretInstalled(Zquic::PktKeyLevel::ZeroRTT),
    "0-RTT key-level traffic secrets not recorded");
  ZuCHECK(!crypto.txSecretInstalled(Zquic::PktNumSpace::AppData) &&
      !crypto.rxSecretInstalled(Zquic::PktNumSpace::AppData) &&
      !crypto.secretInstalled(Zquic::PktNumSpace::AppData),
    "0-RTT keys contaminated 1-RTT AppData state");

  ZuCHECK(crypto.rejectZeroRTT(), "0-RTT reject failed");
  ZuCHECK(!crypto.earlyDataEnabled() &&
      !crypto.txKeySecretInstalled(Zquic::PktKeyLevel::ZeroRTT) &&
      !crypto.rxKeySecretInstalled(Zquic::PktKeyLevel::ZeroRTT),
    "0-RTT reject did not clear early traffic state");
  ZuCHECK(crypto.diag().zeroRTTRejected == 1 &&
      crypto.diag().secretsDiscarded == 1,
    "0-RTT reject diagnostics mismatch");
  ZuCHECK(crypto.rejectZeroRTT() &&
      crypto.diag().zeroRTTRejected == 1 &&
      crypto.diag().secretsDiscarded == 1,
    "0-RTT reject was not idempotent");
}

struct CountHP : Ztls_::CipherAlgo {
  const Ztls_::CipherAlgo *impl;
  mutable unsigned created = 0;

  explicit CountHP(const Ztls_::CipherAlgo *impl_) :
    Ztls_::CipherAlgo{*impl_}, impl{impl_} { }
  Ztls_::CipherContext *create(int encrypt, const void *key) const override {
    ++created;
    return impl->create(encrypt, key);
  }
};

struct CountAEAD : Ztls_::AeadAlgo {
  const Ztls_::AeadAlgo *impl;
  mutable unsigned created = 0;

  CountAEAD(const Ztls_::AeadAlgo *impl_, const Ztls_::CipherAlgo *hp) :
    Ztls_::AeadAlgo{*impl_}, impl{impl_} {
    ecbCipher = hp;
    ctrCipher = nullptr; // Packet protection never uses a supplementary cipher.
  }
  Ztls_::AeadContext *create(bool encrypt, const void *key, const void *iv) const override {
    ++created;
    return impl->create(encrypt, key, iv);
  }
};

struct CryptoPubApp : Zquic::Hub<CryptoPubApp> {
  bool rxInvoked() const { return true; }
  bool txInvoked() const { return true; }
};
struct CryptoPubLink;
struct CryptoPubStream :
  Zquic::Stream<CryptoPubLink, CryptoPubStream> {
  using Base = Zquic::Stream<CryptoPubLink, CryptoPubStream>;
  using Base::Base;
  int process(Zquic::RxStream &) { return 0; }
};
struct CryptoPubLink :
  Zquic::Link<CryptoPubApp, CryptoPubLink, CryptoPubStream> {
  using Base =
    Zquic::Link<CryptoPubApp, CryptoPubLink, CryptoPubStream>;
  using Base::Base;
  using Base::crypto_;
  using Base::snapshotTxCrypto;
  using Base::installTxCrypto;
  using Base::txInstallSecret;
  using Base::txKeySecretInstalled_;
  using Base::rejectZeroRTTTx;
  using Base::configLocalParams;
  using Snapshot = Base::TxCryptoSnapshot;
  void flushTx_() { }
  void queueTxFlush() { }
  void queueRetransmit() { }
  void ptoTimer() { }
  bool retransmit_(bool = false, unsigned = 2,
    Zquic::PktNumSpace::T = Zquic::PktNumSpace::N) { return false; }
};

void testTxCryptoGenerations()
{
  ZuTestScope(testTxCryptoGenerations);

  const auto &base = Ztls_::opensslAes128gcmsha256;
  CountHP hp{base.aead->ecbCipher};
  CountAEAD aead{base.aead, &hp};
  Ztls_::CipherSuite suite{base};
  suite.aead = &aead;
  uint8_t bytes[32];
  memset(bytes, 0x42, sizeof(bytes));
  Zquic::TrafficSecret secret, next;
  ZuCHECK(Zquic::PktProt::deriveSecret(secret, &suite, ZuBSpan{bytes, sizeof(bytes)}) &&
      Zquic::PktProt::deriveNextSecret(next, secret), "generation fixture keys failed");
  ZiMultiplex mx{ZiMxParams{}.scheduler([](auto &s) { s.nThreads(2); })
    .rxThread(1).txThread(2)};
  CryptoPubApp app;
  ZuCHECK(app.init(Zquic::HubParams(&mx, "1", "2")), "generation fixture app failed");
  {
    CryptoPubLink link{&app};
    link.configLocalParams(&app);
    auto &crypto = link.crypto_();
    ZuCHECK(crypto.init(Zquic::CryptoConfig{false, true, "h3"}) &&
        crypto.txSecret(Zquic::PktNumSpace::Handshake, secret),
      "generation fixture secret publication failed");
    CryptoPubLink::Snapshot first, second, unchanged;
    link.snapshotTxCrypto(first);
    ZuCHECK(first.localGen == 1 &&
        link.installTxCrypto(first) && aead.created == 1 && hp.created == 1 &&
        link.installTxCrypto(first) && aead.created == 1 && hp.created == 1,
      "a published Tx generation was installed more than once");
    link.snapshotTxCrypto(unchanged);
    ZuCHECK(!unchanged.gen[Zquic::PktKeyLevel::Handshake] &&
        !unchanged.peerGen && !unchanged.localGen &&
        link.installTxCrypto(unchanged) && aead.created == 1,
      "unchanged TLS progress republished or rebuilt encryption state");
    ZuCHECK(crypto.txSecret(Zquic::PktNumSpace::Handshake, next),
      "replacement secret publication failed");
    link.snapshotTxCrypto(second);
    ZuCHECK(second.gen[Zquic::PktKeyLevel::Handshake] == 2 &&
        link.installTxCrypto(second) && aead.created == 2 && hp.created == 2 &&
        link.installTxCrypto(first) && aead.created == 2,
      "late key publication replaced a newer Tx generation");
    Zquic::TransportParams params;
    params.initialMaxData = 8192;
    crypto.peerParams(params);
    link.snapshotTxCrypto(unchanged);
    ZuCHECK(unchanged.peerGen == 1 && unchanged.peerParams.initialMaxData == 8192 &&
        !unchanged.gen[Zquic::PktKeyLevel::Handshake] &&
        link.installTxCrypto(unchanged) && aead.created == 2,
      "parameter-only publication copied or rebuilt keys");
    ZuCHECK(crypto.txKeySecret(Zquic::PktKeyLevel::ZeroRTT, secret),
      "early key publication failed");
    link.snapshotTxCrypto(first);
    ZuCHECK(link.installTxCrypto(first) &&
        link.txKeySecretInstalled_(Zquic::PktKeyLevel::ZeroRTT) && aead.created == 3,
      "0-RTT Tx generation was not installed");
    link.rejectZeroRTTTx();
    ZuCHECK(link.installTxCrypto(first) &&
        !link.txKeySecretInstalled_(Zquic::PktKeyLevel::ZeroRTT) && aead.created == 3,
      "late publication resurrected rejected 0-RTT keys");
    ZuCHECK(crypto.txSecret(Zquic::PktNumSpace::AppData, secret),
      "1-RTT key publication failed");
    link.snapshotTxCrypto(first);
    ZuCHECK(link.installTxCrypto(first) && aead.created == 4 &&
        link.txInstallSecret(Zquic::PktNumSpace::AppData, next) && aead.created == 5 &&
        link.installTxCrypto(first) && aead.created == 5,
      "replayed TLS publication rolled back a QUIC key update");
    ++first.runtime;
    ZuCHECK(!link.installTxCrypto(first) && aead.created == 5,
      "a prior runtime's key snapshot was accepted");
  }
  app.final();
}

void testTxSecretPublication()
{
  ZuTestScope(testTxSecretPublication);

  const auto &base = Ztls_::opensslAes128gcmsha256;
  CountHP hp{base.aead->ecbCipher};
  CountAEAD aead{base.aead, &hp};
  Ztls_::CipherSuite suite{base};
  suite.aead = &aead;
  uint8_t bytes[32];
  for (unsigned i = 0; i < sizeof(bytes); ++i) bytes[i] = uint8_t(i);
  Zquic::TrafficSecret secret, next;
  ZuCHECK(Zquic::PktProt::deriveSecret(secret, &suite, ZuBSpan{bytes, sizeof(bytes)}) &&
      Zquic::PktProt::deriveNextSecret(next, secret),
    "traffic secrets must not require supplementary cipher support");
  Zquic::Crypto crypto;
  ZuCHECK(crypto.init(Zquic::CryptoConfig{false, true, "h3"}),
    "secret publication fixture init failed");
  ZuCHECK(!crypto.txKeyGen(Zquic::PktKeyLevel::Handshake) &&
      !crypto.txKeyGen(Zquic::PktKeyLevel::ZeroRTT) && !crypto.peerParamsGen(),
    "new crypto state retained publication generations");
  ZuCHECK(crypto.txSecret(Zquic::PktNumSpace::Handshake, secret) &&
      crypto.txKeyGen(Zquic::PktKeyLevel::Handshake) == 1 &&
      !aead.created && !hp.created,
    "publishing a TLS Tx secret created an Rx-owned encryption context");
  Zquic::PktProtState tx;
  ZuCHECK(tx.init(crypto.txSecret(Zquic::PktNumSpace::Handshake),
      Zquic::PktNumSpace::Handshake, true) && aead.created == 1 && hp.created == 1,
    "explicit Tx owner must create only AEAD and HP contexts");
  ZuCHECK(crypto.txSecret(Zquic::PktNumSpace::Handshake, next) &&
      crypto.txKeyGen(Zquic::PktKeyLevel::Handshake) == 2 &&
      aead.created == 1 && hp.created == 1 && tx.secret.secret.bspan() == secret.secret.bspan(),
    "new TLS publication rebuilt or changed the Tx owner's protector");
  ZuCHECK(crypto.rxSecret(Zquic::PktNumSpace::Handshake, secret) &&
      crypto.rxProtState(Zquic::PktNumSpace::Handshake).valid() &&
      aead.created == 2 && hp.created == 2,
    "Rx key publication must retain its decryption context");
  ZuCHECK(crypto.discardSecret(Zquic::PktNumSpace::Handshake) &&
      crypto.txKeyGen(Zquic::PktKeyLevel::Handshake) == 3 &&
      !crypto.txSecretInstalled(Zquic::PktNumSpace::Handshake),
    "retired Tx key generation was not published");
  crypto.discardSecret(Zquic::PktNumSpace::Handshake);
  ZuCHECK(crypto.txKeyGen(Zquic::PktKeyLevel::Handshake) == 3 &&
      crypto.txKeySecret(Zquic::PktKeyLevel::ZeroRTT, secret) &&
      crypto.txKeyGen(Zquic::PktKeyLevel::ZeroRTT) == 1 &&
      !crypto.txKeyGen(Zquic::PktKeyLevel::OneRTT),
    "key generations crossed encryption levels or repeated retirement");
  ZuCHECK(crypto.rejectZeroRTT() &&
      crypto.txKeyGen(Zquic::PktKeyLevel::ZeroRTT) == 2 &&
      crypto.rejectZeroRTT() && crypto.txKeyGen(Zquic::PktKeyLevel::ZeroRTT) == 2,
    "0-RTT rejection must publish removal exactly once");
  Zquic::TransportParams params;
  crypto.peerParams(params);
  ZuCHECK(crypto.peerParamsGen() == 1 && crypto.peerParamsSet(),
    "peer parameter publication was not versioned");
  ZuCHECK(crypto.init(Zquic::CryptoConfig{false, false, "h3"}) &&
      !crypto.txKeyGen(Zquic::PktKeyLevel::Handshake) &&
      !crypto.txKeyGen(Zquic::PktKeyLevel::ZeroRTT) && !crypto.peerParamsGen(),
    "new runtime retained earlier publication generations");
}

void testTLSOutputBuffer()
{
  ZuTestScope(testTLSOutputBuffer);

  Zquic::Crypto crypto;
  ZuCHECK(crypto.initTLS(Zquic::CryptoConfig{
      false, false, "h3", {}, {}, {}, "localhost"}),
    "client TLS init failed");

  ZmRef<ZiIOBuf> out =
    allocator<Zquic::CryptoTxBufAlloc<
      Zquic::Crypto::TLSOutputMax, Zquic::Crypto::TLSOutputMax>>()->allocBuf();
  Zquic::CryptoOffsets offsets;
  auto backing = out->data();
  auto capacity = out->size;
  int n = crypto.handleTLSMessage(out.ptr(), offsets, 0, {});
  ZuCHECK(n > 0 && out->length == unsigned(n) && !out->skip,
    "TLS output buffer was not published");
  ZuCHECK(offsets[0] == 0 && offsets[1] == unsigned(n),
    "TLS output epoch offsets mismatch");
  ZuCHECK(out->data() == backing && out->size == capacity,
    "TLS output replaced caller backing");
  ZuCHECK(crypto.diag().tlsMessagesEmitted == 1,
    "TLS output diagnostic not updated");
}

void testCryptoFlightOffsets()
{
  ZuTestScope(testCryptoFlightOffsets);

  Zquic::CryptoStream txCrypto[Zquic::PktNumSpace::N];
  for (auto &stream : txCrypto)
    stream.allocators(nullptr, allocator<Zquic::CryptoTxBufAlloc<>>());
  Zquic::LinkTxDiag diag;
  unsigned sends = 0;
  auto send = [&sends](
      Zquic::PktNumSpace::T, ZuBSpan, ZuBSpan,
      const Zquic::SentFrameRef &, ZiSockAddr) {
    ++sends;
    return true;
  };

  auto alloc = allocator<Zquic::CryptoTxBufAlloc<>>();
  auto backing = [alloc](ZuBSpan bytes) {
    auto buf = alloc->allocBuf();
    if (bytes.length() && !buf->append(bytes)) return ZmRef<ZiIOBuf>{};
    return buf;
  };

  Zquic::CryptoOffsets empty{0, 0, 0, 0, 0};
  ZuCHECK(Zquic::sendCryptoFlights_(
      txCrypto, diag, backing({}), empty, 16, {}, send) &&
      !sends && !diag.failures,
    "empty crypto flights failed");

  Zquic::CryptoOffsets one{0, 3, 3, 3, 3};
  ZuCHECK(Zquic::sendCryptoFlights_(
      txCrypto, diag, backing("abc"), one, 16, {}, send) &&
      sends == 1 && diag.cryptoBytesTx == Zquic::DiagCounter{3},
    "single-epoch crypto flights failed");

  Zquic::CryptoStream multiCrypto[Zquic::PktNumSpace::N];
  for (auto &stream : multiCrypto)
    stream.allocators(nullptr, alloc);
  Zquic::LinkTxDiag multiDiag;
  sends = 0;
  Zquic::CryptoOffsets multi{0, 2, 2, 4, 6};
  ZuCHECK(Zquic::sendCryptoFlights_(
      multiCrypto, multiDiag, backing("abcdef"), multi, 16, {}, send) &&
      sends == 3 && multiDiag.cryptoBytesTx == Zquic::DiagCounter{6},
    "multi-epoch crypto flights failed");

  const Zquic::CryptoOffsets invalid[] = {
    {0, 3, 3, 2, 3}, // descending
    {0, 0, 0, 0, 4}, // beyond backing
    {1, 1, 1, 3, 3}, // leading bytes omitted
    {0, 1, 1, 2, 2}, // trailing bytes omitted
    {0, 1, 2, 3, 3}, // 0-RTT CRYPTO output
    {0, 0, 0, 3},    // missing boundary
  };
  auto sent = sends;
  bool rejected = true;
  for (const auto &offsets : invalid)
    rejected &= !Zquic::sendCryptoFlights_(
      multiCrypto, multiDiag, backing("abc"), offsets, 16, {}, send);
  ZuCHECK(rejected && sends == sent && multiDiag.cryptoBytesTx == Zquic::DiagCounter{6},
    "invalid crypto offsets published data");
}

void testOwnedCryptoFlights()
{
  ZuTestScope(testOwnedCryptoFlights);

  Zquic::Crypto crypto;
  ZuCHECK(crypto.initTLS(Zquic::CryptoConfig{
      false, false, "h3", {}, {}, {}, "localhost"}),
    "owned TLS output initialization failed");
  ZmRef<ZiIOBuf> out = allocator<Zquic::CryptoTxBufAlloc<
    Zquic::Crypto::TLSOutputMax, Zquic::Crypto::TLSOutputMax>>()->allocBuf();
  Zquic::CryptoOffsets offsets;
  int n = crypto.handleTLSMessage(out.ptr(), offsets, 0, {});
  ZuCHECK(n > 0, "owned TLS output generation failed");
  if (n <= 0) return;
  Zquic::CryptoStream streams[Zquic::PktNumSpace::N];
  for (auto &stream : streams)
    stream.allocators(nullptr, allocator<Zquic::CryptoTxBufAlloc<>>());
  Zquic::LinkTxDiag diag;
  const uint8_t *original = out->data();
  unsigned sent = 0;
  bool identity = true;
  ZuCHECK(Zquic::sendCryptoFlights_(streams, diag, out, offsets, 17, {},
      [original, &sent, &identity](
	  Zquic::PktNumSpace::T space, ZuBSpan, ZuBSpan payload,
	  const Zquic::SentFrameRef &ref, ZiSockAddr) {
	identity = identity && space == Zquic::PktNumSpace::Initial &&
	  payload.data() == original + ref.offset;
	sent += payload.length();
	return true;
      }) && identity && sent == unsigned(n),
    "TLS first send lost original output backing or chunk positions");
  out = nullptr;
  ZuBSpan retained;
  ZuCHECK(streams[Zquic::PktNumSpace::Initial].txPayload(0, unsigned(n), retained) &&
      retained.data() == original && retained.length() == unsigned(n),
    "TLS chunks did not coalesce and retain original output after handoff");
  streams[Zquic::PktNumSpace::Initial].reset();
  ZuCHECK(!streams[Zquic::PktNumSpace::Initial].txPayload(0, unsigned(n), retained),
    "epoch reset retained retired TLS output");
}

void testCryptoOwnedBuffers()
{
  ZuTestScope(testCryptoOwnedBuffers);

  ZmRef<ZiIOBuf> first = allocator<Zquic::CryptoTxBufAlloc<>>()->allocBuf();
  ZmRef<ZiIOBuf> second = allocator<Zquic::CryptoTxBufAlloc<>>()->allocBuf();
  first->append(ZuBSpan{"prefixabcdef"});
  first->advance(6);
  second->append(ZuBSpan{"padghij"});
  second->advance(3);
  const uint8_t *a = first->data(), *b = second->data();
  Zquic::CryptoStream stream{nullptr, allocator<Zquic::CryptoTxBufAlloc<>>()};
  ZuCHECK(stream.sent(0, ZuMv(first)) &&
      stream.sent(6, ZuMv(second)),
    "owned CRYPTO buffer admission failed");
  first = nullptr; second = nullptr;
  ZuBSpan payload;
  ZuCHECK(stream.txPayload(0, 10, payload) && payload.data() == a &&
      payload == "abcdef",
    "coalesced CRYPTO range changed backing or crossed a backing boundary");
  ZuCHECK(stream.txPayload(4, 6, payload) && payload.data() == a + 4 &&
      payload == "ef" && stream.txPayload(6, 4, payload) &&
      payload.data() == b && payload == "ghij",
    "cross-buffer CRYPTO retry did not clip and continue original ranges");
  ZuCHECK(stream.sent(4, ZuBSpan{"efgh"}) &&
      !stream.sent(4, ZuBSpan{"efxh"}),
    "duplicate CRYPTO comparison did not traverse retained backing boundaries");
  ZuCHECK(!stream.sent(11, ZuBSpan{}) && stream.sent(10, ZuBSpan{}) &&
      !stream.txPayload(9, 2, payload),
    "CRYPTO gap, empty tail or invalid retained range handling changed");

  ZmRef<ZiIOBuf> shifted = allocator<Zquic::CryptoTxBufAlloc<>>()->allocBuf();
  shifted->append(ZuBSpan{"skipklmnop"});
  shifted->advance(4);
  const uint8_t *c = shifted->data();
  Zquic::CryptoOffsets offsets(Zquic::TLSEpochCount + 1, false);
  offsets[0] = 0;
  for (unsigned i = 1; i <= Zquic::TLSEpochCount; ++i) offsets[i] = 6;
  Zquic::CryptoStream flights[Zquic::PktNumSpace::N];
  for (auto &stream : flights)
    stream.allocators(nullptr, allocator<Zquic::CryptoTxBufAlloc<>>());
  Zquic::LinkTxDiag diag;
  bool identity = true;
  ZuCHECK(Zquic::sendCryptoFlights_(flights, diag, shifted, offsets, 2, {},
      [c, &identity](Zquic::PktNumSpace::T, ZuBSpan, ZuBSpan bytes,
	  const Zquic::SentFrameRef &ref, ZiSockAddr) {
	identity = identity && bytes.data() == c + ref.offset;
	return true;
      }) && identity,
    "offset CRYPTO first send lost backing identity");
  shifted = nullptr;
  ZuCHECK(flights[Zquic::PktNumSpace::Initial].txPayload(0, 6, payload) &&
      payload.data() == c && payload == "klmnop",
    "offset CRYPTO retransmission retained a storage-relative offset");
  stream.reset();
  ZuCHECK(!stream.txPayload(0, 1, payload),
    "CRYPTO reset retained earlier output buffers");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testCryptoPosture);
  ZuTestCall(testEarlyDataTLSProperties);
  ZuTestCall(testEarlyTrafficSecretState);
  ZuTestCall(testTxSecretPublication);
  ZuTestCall(testTxCryptoGenerations);
  ZuTestCall(testTLSOutputBuffer);
  ZuTestCall(testCryptoFlightOffsets);
  ZuTestCall(testOwnedCryptoFlights);
  ZuTestCall(testCryptoOwnedBuffers);
}
