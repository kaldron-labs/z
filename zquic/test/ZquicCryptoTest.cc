//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZquicCrypto.hh>
#include <zlib/ZtlsPico.hh>

using namespace ZuTestUtil;

void testCryptoPosture()
{
  ZuTestScope(testCryptoPosture);

  Zquic::Crypto crypto;
  ZuCHECK(crypto.init(Zquic::CryptoConfig{false, true, "h3"}),
    "crypto init failed");
  ZuCHECK(crypto.alpn() == "h3", "ALPN not retained");
  ZuCHECK(!crypto.earlyDataEnabled(), "0-RTT must remain disabled");
  ZuCHECK(crypto.diag().zeroRTTRejected == 1, "0-RTT rejection not counted");

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
  ZuTestCall(testTLSOutputBuffer);
}
