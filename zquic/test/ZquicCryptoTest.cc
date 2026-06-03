//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZquicCrypto.hh>

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

  crypto.installSecret(Zquic::CryptoLevel::Initial, "initial-secret");
  crypto.installSecret(Zquic::CryptoLevel::Handshake, "hs-secret");
  crypto.installSecret(Zquic::CryptoLevel::OneRTT, "app-secret");
  ZuCHECK(crypto.completeHandshake(), "1-RTT readiness not reached");
  ZuCHECK(!crypto.secretInstalled(Zquic::CryptoLevel::Initial) &&
      !crypto.secretInstalled(Zquic::CryptoLevel::Handshake),
    "lower-level secrets were not discarded");
  ZuCHECK(crypto.secretInstalled(Zquic::CryptoLevel::OneRTT),
    "1-RTT secret not recorded");
  ZuCHECK(crypto.diag().secretsInstalled == 3 &&
      crypto.diag().secretsDiscarded == 2,
    "secret lifecycle diagnostics mismatch");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testCryptoPosture);
}
