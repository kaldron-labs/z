//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZquicCrypto.hh>
#include <zlib/ZtlsPico.hh>

#include <zpicotls/openssl.h>

using namespace ZuTestUtil;

static ZuBSpan bytes_(const uint8_t *data, unsigned len)
{
  return ZuBSpan{data, len};
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
    new Zquic::CryptoTxBufAlloc<
      Zquic::Crypto::TLSOutputMax, Zquic::Crypto::TLSOutputMax>{nullptr};
  size_t offsets[5] = {};
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
  ZuCHECK(Zquic::PktProt::deriveTrafficSecret(
      secret, &ptls_openssl_aes128gcmsha256,
      bytes_(secretBytes, sizeof(secretBytes))),
    "0-RTT traffic secret derivation failed");

  Zquic::Crypto crypto;
  ZuCHECK(crypto.init(Zquic::CryptoConfig{false, false, "h3"}),
    "crypto init failed");
  ZuCHECK(crypto.updateTxKeyTrafficSecret(Zquic::PktKeyLevel::ZeroRTT, secret) &&
      crypto.updateRxKeyTrafficSecret(Zquic::PktKeyLevel::ZeroRTT, secret),
    "0-RTT traffic secret install failed");
  ZuCHECK(crypto.earlyDataEnabled(),
    "0-RTT key install did not enable early data state");
  ZuCHECK(crypto.earlyDataOffered(),
    "0-RTT key install did not enter offered state");
  ZuCHECK(crypto.txKeyTrafficSecretInstalled(Zquic::PktKeyLevel::ZeroRTT) &&
      crypto.rxKeyTrafficSecretInstalled(Zquic::PktKeyLevel::ZeroRTT),
    "0-RTT key-level traffic secrets not recorded");
  ZuCHECK(!crypto.txTrafficSecretInstalled(Zquic::PktNumSpace::AppData) &&
      !crypto.rxTrafficSecretInstalled(Zquic::PktNumSpace::AppData) &&
      !crypto.secretInstalled(Zquic::PktNumSpace::AppData),
    "0-RTT keys contaminated 1-RTT AppData state");

  ZuCHECK(crypto.rejectZeroRTT(), "0-RTT reject failed");
  ZuCHECK(!crypto.earlyDataEnabled() &&
      !crypto.txKeyTrafficSecretInstalled(Zquic::PktKeyLevel::ZeroRTT) &&
      !crypto.rxKeyTrafficSecretInstalled(Zquic::PktKeyLevel::ZeroRTT),
    "0-RTT reject did not clear early traffic state");
  ZuCHECK(crypto.diag().zeroRTTRejected == 1 &&
      crypto.diag().secretsDiscarded == 1,
    "0-RTT reject diagnostics mismatch");
  ZuCHECK(crypto.rejectZeroRTT() &&
      crypto.diag().zeroRTTRejected == 1 &&
      crypto.diag().secretsDiscarded == 1,
    "0-RTT reject was not idempotent");
}

void testTLSOutputBuffer()
{
  ZuTestScope(testTLSOutputBuffer);

  Zquic::Crypto crypto;
  ZuCHECK(crypto.initTLS(Zquic::CryptoConfig{
      false, false, "h3", {}, {}, {}, "localhost"}),
    "client TLS init failed");

  ZmRef<ZiIOBuf> out =
    new Zquic::CryptoTxBufAlloc<
      Zquic::Crypto::TLSOutputMax, Zquic::Crypto::TLSOutputMax>{nullptr};
  size_t offsets[5] = {};
  Ztls::Pico::reset_stats();
  int n = crypto.handleTLSMessage(out.ptr(), offsets, 0, {});
  auto stats = Ztls::Pico::stats();
  ZuCHECK(n > 0 && out->length == unsigned(n) && !out->skip,
    "TLS output buffer was not published");
  ZuCHECK(offsets[0] == 0 && offsets[1] == unsigned(n),
    "TLS output epoch offsets mismatch");
  ZuCHECK(!stats.internal_alloc && !stats.internal_free,
    "TLS output used picotls internal buffer allocation");
  ZuCHECK(crypto.diag().tlsMessagesEmitted == 1,
    "TLS output diagnostic not updated");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testCryptoPosture);
  ZuTestCall(testEarlyDataTLSProperties);
  ZuTestCall(testEarlyTrafficSecretState);
  ZuTestCall(testTLSOutputBuffer);
}
