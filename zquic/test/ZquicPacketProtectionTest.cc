//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZquicCrypto.hh>

#include <zpicotls/openssl.h>

using namespace ZuTestUtil;

namespace {

int hexNibble_(char c)
{
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

bool hexDecode_(uint8_t *out, unsigned len, const char *hex)
{
  if ((strlen(hex) >> 1) != len || (strlen(hex) & 1)) return false;
  for (unsigned i = 0; i < len; ++i) {
    int hi = hexNibble_(hex[i<<1]);
    int lo = hexNibble_(hex[(i<<1) + 1]);
    if (hi < 0 || lo < 0) return false;
    out[i] = uint8_t((hi<<4) | lo);
  }
  return true;
}

template <unsigned N>
bool hexEquals_(const uint8_t (&actual)[N], const char *expected)
{
  uint8_t b[N];
  return hexDecode_(b, N, expected) && !memcmp(actual, b, N);
}

ZuBSpan bytes_(const uint8_t *data, unsigned len)
{
  return ZuBSpan{data, len};
}

ZuBSpan text_(const char *s)
{
  return ZuBSpan{s};
}

} // namespace

void testRFC9001InitialKeys()
{
  ZuTestScope(testRFC9001InitialKeys);

  uint8_t dcidRaw[8];
  ZuCHECK(hexDecode_(dcidRaw, sizeof(dcidRaw), "8394c8f03e515708"),
    "test DCID decode failed");
  Zquic::CxnID dcid{bytes_(dcidRaw, sizeof(dcidRaw))};
  Zquic::InitialKeyMaterial keys;
  ZuCHECK(Zquic::InitialCrypto::derive(keys, dcid),
    "Initial key derivation failed");

  ZuCHECK(hexEquals_(keys.initial,
      "7db5df06e7a69e432496adedb0085192"
      "3595221596ae2ae9fb8115c1e9ed0a44"),
    "RFC 9001 initial secret mismatch");
  ZuCHECK(hexEquals_(keys.client.secret,
      "c00cf151ca5be075ed0ebfb5c80323c4"
      "2d6b7db67881289af4008f1f6c357aea"),
    "RFC 9001 client initial secret mismatch");
  ZuCHECK(hexEquals_(keys.client.key, "1f369613dd76d5467730efcbe3b1a22d"),
    "RFC 9001 client key mismatch");
  ZuCHECK(hexEquals_(keys.client.iv, "fa044b2f42a3fd3b46fb255c"),
    "RFC 9001 client IV mismatch");
  ZuCHECK(hexEquals_(keys.client.hp, "9f50449e04a0e810283a1e9933adedd2"),
    "RFC 9001 client HP key mismatch");

  ZuCHECK(hexEquals_(keys.server.secret,
      "3c199828fd139efd216c155ad844cc81"
      "fb82fa8d7446fa7d78be803acdda951b"),
    "RFC 9001 server initial secret mismatch");
  ZuCHECK(hexEquals_(keys.server.key, "cf3a5331653c364c88f0f379b6067e37"),
    "RFC 9001 server key mismatch");
  ZuCHECK(hexEquals_(keys.server.iv, "0ac1493ca1905853b0bba03e"),
    "RFC 9001 server IV mismatch");
  ZuCHECK(hexEquals_(keys.server.hp, "c206b8d9b9f0f37644430b490eeaa314"),
    "RFC 9001 server HP key mismatch");

  Zquic::Crypto crypto;
  ZuCHECK(crypto.deriveInitial(dcid), "Crypto Initial derivation failed");
  ZuCHECK(crypto.diag().initialKeysDerived == 1,
    "Initial derivation not counted");
  ZuCHECK(!memcmp(
      crypto.initialKeys().client.key, keys.client.key,
      Zquic::InitialSecret::KeyLen),
    "Crypto Initial derivation stored wrong keys");
}

void testInitialProt()
{
  ZuTestScope(testInitialProt);

  uint8_t dcidRaw[8];
  ZuCHECK(hexDecode_(dcidRaw, sizeof(dcidRaw), "8394c8f03e515708"),
    "test DCID decode failed");
  Zquic::CxnID dcid{bytes_(dcidRaw, sizeof(dcidRaw))};
  Zquic::InitialKeyMaterial keys;
  ZuCHECK(Zquic::InitialCrypto::derive(keys, dcid),
    "Initial key derivation failed");

  const char *aad = "unprotected QUIC header";
  const char *plain = "CRYPTO frame payload";
  uint8_t ciphertext[128];
  int n = Zquic::InitialCrypto::encrypt(
    ciphertext, sizeof(ciphertext), keys.client, 2,
    text_(aad), text_(plain));
  ZuCHECK(n == int(strlen(plain) + Zquic::InitialSecret::TagLen),
    "Initial encrypt length mismatch");
  ZuCHECK(memcmp(ciphertext, plain, strlen(plain)),
    "Initial encrypt left plaintext visible");

  uint8_t out[128];
  int m = Zquic::InitialCrypto::decrypt(
    out, sizeof(out), keys.client, 2, text_(aad), bytes_(ciphertext, n));
  ZuCHECK(m == int(strlen(plain)) && !memcmp(out, plain, strlen(plain)),
    "Initial decrypt mismatch");

  uint8_t inPlace[128];
  memcpy(inPlace, ciphertext, n);
  m = Zquic::InitialCrypto::decrypt(
    inPlace, sizeof(inPlace), keys.client, 2,
    text_(aad), bytes_(inPlace, n));
  ZuCHECK(m == int(strlen(plain)) && !memcmp(inPlace, plain, strlen(plain)),
    "Initial in-place decrypt mismatch");

  uint8_t tampered[128];
  memcpy(tampered, ciphertext, n);
  tampered[n - 1] ^= 0x01;
  ZuCHECK(Zquic::InitialCrypto::decrypt(
      out, sizeof(out), keys.client, 2, text_(aad), bytes_(tampered, n)) < 0,
    "Initial decrypt accepted bad tag");
  ZuCHECK(Zquic::InitialCrypto::decrypt(
      out, sizeof(out), keys.client, 3, text_(aad), bytes_(ciphertext, n)) < 0,
    "Initial decrypt accepted wrong packet number");
}

void testRFC9001ClientInitialProt()
{
  ZuTestScope(testRFC9001ClientInitialProt);

  uint8_t dcidRaw[8];
  ZuCHECK(hexDecode_(dcidRaw, sizeof(dcidRaw), "8394c8f03e515708"),
    "test DCID decode failed");
  Zquic::CxnID dcid{bytes_(dcidRaw, sizeof(dcidRaw))};
  Zquic::InitialKeyMaterial keys;
  ZuCHECK(Zquic::InitialCrypto::derive(keys, dcid),
    "Initial key derivation failed");

  uint8_t header[22];
  ZuCHECK(hexDecode_(header, sizeof(header),
      "c300000001088394c8f03e5157080000449e00000002"),
    "RFC 9001 unprotected Initial header decode failed");

  static const char CryptoFrameHex[] =
    "060040f1010000ed0303ebf8fa56f12939b9584a3896472ec40bb863cfd3e868"
    "04fe3a47f06a2b69484c00000413011302010000c000000010000e00000b6578"
    "616d706c652e636f6dff01000100000a00080006001d00170018001000070005"
    "04616c706e000500050100000000003300260024001d00209370b2c9caa47fba"
    "baf4559fedba753de171fa71f50f1ce15d43e994ec74d748002b000302030400"
    "0d0010000e0403050306030203080408050806002d00020101001c0002400100"
    "3900320408ffffffffffffffff05048000ffff07048000ffff08011001048000"
    "75300901100f088394c8f03e51570806048000ffff";
  uint8_t payload[1162] = {};
  ZuCHECK(hexDecode_(payload, (sizeof(CryptoFrameHex) - 1) >> 1, CryptoFrameHex),
    "RFC 9001 Initial payload decode failed");

  uint8_t ciphertext[sizeof(payload) + Zquic::InitialSecret::TagLen];
  int n = Zquic::InitialCrypto::encrypt(
    ciphertext, sizeof(ciphertext), keys.client, 2,
    bytes_(header, sizeof(header)), bytes_(payload, sizeof(payload)));
  ZuCHECK(n == int(sizeof(ciphertext)),
    "RFC 9001 Initial protected payload length mismatch");
  uint8_t sample[16];
  ZuCHECK(hexDecode_(sample, sizeof(sample), "d1b1c98dd7689fb8ec11d242b123dc9b"),
    "RFC 9001 protected payload sample decode failed");
  ZuCHECK(!memcmp(ciphertext, sample, sizeof(sample)),
    "RFC 9001 Initial protected payload sample mismatch");

  uint8_t mask[Zquic::InitialSecret::HPMaskLen];
  ZuCHECK(Zquic::InitialCrypto::headerMask(
      mask, sizeof(mask), keys.client, bytes_(ciphertext, 16)),
    "RFC 9001 Initial HP mask failed");
  header[0] ^= mask[0] & 0x0f;
  for (unsigned i = 0; i < 4; ++i) header[18 + i] ^= mask[1 + i];
  uint8_t protectedHdr[22];
  ZuCHECK(hexDecode_(protectedHdr, sizeof(protectedHdr),
      "c000000001088394c8f03e5157080000449e7b9aec34"),
    "RFC 9001 protected Initial header decode failed");
  ZuCHECK(!memcmp(header, protectedHdr, sizeof(header)),
    "RFC 9001 protected Initial header mismatch");

  uint8_t packet[sizeof(header) + sizeof(ciphertext)];
  n = Zquic::InitialPktProt::protectLong(
    packet, sizeof(packet), keys.client, 2,
    bytes_(protectedHdr, 0), {}, 0, 0);
  ZuCHECK(n < 0, "invalid Initial packet protection input accepted");

  uint8_t unprotectedHdr[22];
  ZuCHECK(hexDecode_(unprotectedHdr, sizeof(unprotectedHdr),
      "c300000001088394c8f03e5157080000449e00000002"),
    "RFC 9001 unprotected Initial header re-decode failed");
  n = Zquic::InitialPktProt::protectLong(
    packet, sizeof(packet), keys.client, 2,
    bytes_(unprotectedHdr, sizeof(unprotectedHdr)),
    bytes_(payload, sizeof(payload)), 18, 4);
  ZuCHECK(n == int(sizeof(packet)),
    "full Initial packet protection failed");
  ZuCHECK(!memcmp(packet, protectedHdr, sizeof(protectedHdr)) &&
      !memcmp(packet + sizeof(protectedHdr), sample, sizeof(sample)),
    "full Initial packet protection RFC vector mismatch");
  uint8_t packetV[sizeof(packet)];
  ptls_iovec_t initialVec[3] = {
    ptls_iovec_init(payload, 32),
    ptls_iovec_init(payload + 32, 0),
    ptls_iovec_init(payload + 32, sizeof(payload) - 32)
  };
  int nv = Zquic::InitialPktProt::protectLongV(
    packetV, sizeof(packetV), keys.client, 2,
    bytes_(unprotectedHdr, sizeof(unprotectedHdr)),
    initialVec, 3, 18, 4);
  ZuCHECK(nv == n && !memcmp(packetV, packet, sizeof(packet)),
    "Initial vector packet protection parity failed");

  uint64_t pn = 0;
  unsigned payloadOffset = 0;
  int plainLen = Zquic::InitialPktProt::unprotectLong(
    packet, sizeof(packet), keys.client, 0, 18, pn, payloadOffset);
  ZuCHECK(plainLen == int(sizeof(payload)) && pn == 2 &&
      payloadOffset == sizeof(unprotectedHdr),
    "full Initial packet unprotect metadata mismatch");
  ZuCHECK(!memcmp(packet, unprotectedHdr, sizeof(unprotectedHdr)) &&
      !memcmp(packet + payloadOffset, payload, sizeof(payload)),
    "full Initial packet in-place decrypt mismatch");
}

void testHdrProt()
{
  ZuTestScope(testHdrProt);

  uint8_t dcidRaw[8];
  ZuCHECK(hexDecode_(dcidRaw, sizeof(dcidRaw), "8394c8f03e515708"),
    "test DCID decode failed");
  Zquic::CxnID dcid{bytes_(dcidRaw, sizeof(dcidRaw))};
  Zquic::InitialKeyMaterial keys;
  ZuCHECK(Zquic::InitialCrypto::derive(keys, dcid),
    "Initial key derivation failed");

  uint8_t sample[16];
  ZuCHECK(hexDecode_(sample, sizeof(sample), "d1b1c98dd7689fb8ec11d242b123dc9b"),
    "RFC 9001 HP sample decode failed");
  uint8_t mask[Zquic::InitialSecret::HPMaskLen];
  ZuCHECK(Zquic::InitialCrypto::headerMask(
      mask, sizeof(mask), keys.client, bytes_(sample, sizeof(sample))),
    "Initial header mask failed");
  uint8_t expected[Zquic::InitialSecret::HPMaskLen];
  ZuCHECK(hexDecode_(expected, sizeof(expected), "437b9aec36"),
    "RFC 9001 HP mask decode failed");
  ZuCHECK(!memcmp(mask, expected, sizeof(mask)),
    "RFC 9001 client Initial HP mask mismatch");
  ZuCHECK(!Zquic::InitialCrypto::headerMask(
      mask, sizeof(mask), keys.client, bytes_(sample, sizeof(sample) - 1)),
    "short HP sample accepted");
}

void testTrafficSecretProt()
{
  ZuTestScope(testTrafficSecretProt);

  uint8_t secretBytes[32];
  for (unsigned i = 0; i < sizeof(secretBytes); ++i) secretBytes[i] = i;
  Zquic::TrafficSecret secret;
  ZuCHECK(Zquic::PktProt::deriveTrafficSecret(
      secret, &ptls_openssl_aes128gcmsha256,
      bytes_(secretBytes, sizeof(secretBytes))),
    "traffic secret derivation failed");
  ZuCHECK(secret.valid() && secret.keyLen == 16 && secret.ivLen == 12 &&
      secret.hpLen == 16 && secret.tagLen == 16,
    "traffic secret metadata mismatch");

  Zquic::PktProtState txLong;
  Zquic::PktProtState txLongSplit;
  Zquic::PktProtState rxLong;
  Zquic::PktProtState txShort;
  Zquic::PktProtState txShortSplit;
  Zquic::PktProtState rxShort;
  ZuCHECK(txLong.init(secret, Zquic::CryptoLevel::Handshake, true) &&
      txLongSplit.init(secret, Zquic::CryptoLevel::Handshake, true) &&
      rxLong.init(secret, Zquic::CryptoLevel::Handshake, false) &&
      txShort.init(secret, Zquic::CryptoLevel::OneRTT, true) &&
      txShortSplit.init(secret, Zquic::CryptoLevel::OneRTT, true) &&
      rxShort.init(secret, Zquic::CryptoLevel::OneRTT, false),
    "traffic packet protection state init failed");
  auto txLongAead = txLong.aead.get();
  auto txLongHP = txLong.hp.get();
  auto txLongHPSupp = txLong.hpSupp.get();
  auto rxLongAead = rxLong.aead.get();
  auto rxLongHP = rxLong.hp.get();
  auto txShortAead = txShort.aead.get();
  auto txShortHP = txShort.hp.get();
  auto txShortHPSupp = txShort.hpSupp.get();
  auto rxShortAead = rxShort.aead.get();
  auto rxShortHP = rxShort.hp.get();

  Zquic::CxnID dcid{"server01"};
  Zquic::CxnID scid{"client01"};
  uint8_t payload[32] = {};
  ZuCHECK(Zquic::FrameCodec::writePing(payload, sizeof(payload)) == 1,
    "traffic test PING encode failed");

  uint8_t header[128];
  static constexpr unsigned LongPNLength = 2;
  int h = Zquic::Pkt::writeHandshake(
    header, sizeof(header), dcid, scid, sizeof(payload), LongPNLength);
  ZuCHECK(h > 0, "traffic Handshake header encode failed");
  unsigned pnOffset = unsigned(h);
  ZuCHECK(Zquic::PktNumber::encode(
      header + h, sizeof(header) - unsigned(h), 7, LongPNLength) ==
      int(LongPNLength),
    "traffic Handshake packet number encode failed");
  h += LongPNLength;

  uint8_t packet[256];
  int n = Zquic::PktProt::protectLong(
    packet, sizeof(packet), txLong, 7, bytes_(header, unsigned(h)),
    bytes_(payload, sizeof(payload)), pnOffset, LongPNLength);
  ZuCHECK(n == int(unsigned(h) + sizeof(payload) + secret.tagLen),
    "traffic Handshake protection failed");
  ZuCHECK(memcmp(packet, header, unsigned(h)) &&
      memcmp(packet + unsigned(h), payload, sizeof(payload)),
    "traffic Handshake protection left packet visible");
  uint8_t packetV[256];
  ptls_iovec_t longVec[2] = {
    ptls_iovec_init(payload, 1),
    ptls_iovec_init(payload + 1, sizeof(payload) - 1)
  };
  int nv = Zquic::PktProt::protectLongV(
    packetV, sizeof(packetV), txLongSplit, 7, bytes_(header, unsigned(h)),
    longVec, 2, pnOffset, LongPNLength);
  ZuCHECK(nv == n && !memcmp(packetV, packet, unsigned(n)),
    "traffic Handshake vector protection parity failed");

  uint64_t pn = 0;
  unsigned payloadOffset = 0;
  int plainLen = Zquic::PktProt::unprotectLong(
    packet, unsigned(n), rxLong, 0, pnOffset, pn, payloadOffset);
  ZuCHECK(plainLen == int(sizeof(payload)) &&
      pn == 7 && payloadOffset == unsigned(h),
    "traffic Handshake unprotect metadata mismatch");
  ZuCHECK(!memcmp(packet, header, unsigned(h)) &&
      !memcmp(packet + payloadOffset, payload, sizeof(payload)),
    "traffic Handshake unprotect plaintext mismatch");

  h = Zquic::Pkt::writeShort(header, sizeof(header), dcid, 11, 2);
  ZuCHECK(h > 0, "traffic short header encode failed");
  pnOffset = unsigned(h) - 2;
  n = Zquic::PktProt::protectShort(
    packet, sizeof(packet), txShort, 11, bytes_(header, unsigned(h)),
    bytes_(payload, sizeof(payload)), pnOffset, 2);
  ZuCHECK(n == int(unsigned(h) + sizeof(payload) + secret.tagLen),
    "traffic short packet protection failed");
  ZuCHECK(memcmp(packet, header, unsigned(h)),
    "traffic short header was not protected");
  ptls_iovec_t shortVec[3] = {
    ptls_iovec_init(payload, 3),
    ptls_iovec_init(payload + 3, 0),
    ptls_iovec_init(payload + 3, sizeof(payload) - 3)
  };
	  nv = Zquic::PktProt::protectShortV(
	    packetV, sizeof(packetV), txShortSplit, 11, bytes_(header, unsigned(h)),
	    shortVec, 3, pnOffset, 2);
	  ZuCHECK(nv == n && !memcmp(packetV, packet, unsigned(n)),
	    "traffic short vector protection parity failed");

	  Zquic::TrafficSecret wrongSecret = secret;
	  wrongSecret.key[0] ^= 0x55;
	  Zquic::PktProtState wrongRx;
	  Zquic::PktProtState retryRx;
	  uint8_t retry[256];
	  memcpy(retry, packetV, unsigned(nv));
	  ZuCHECK(wrongRx.init(wrongSecret, Zquic::CryptoLevel::OneRTT, false) &&
	      retryRx.init(secret, Zquic::CryptoLevel::OneRTT, false),
	    "traffic short retry state init failed");
	  plainLen = Zquic::PktProt::unprotectShort(
	    retry, unsigned(nv), wrongRx, 0, pnOffset, pn, payloadOffset);
	  ZuCHECK(plainLen < 0, "traffic short wrong-key unprotect succeeded");
	  plainLen = Zquic::PktProt::unprotectShort(
	    retry, unsigned(nv), retryRx, 0, pnOffset, pn, payloadOffset);
	  ZuCHECK(plainLen == int(sizeof(payload)) &&
	      pn == 11 && payloadOffset == unsigned(h) &&
	      !memcmp(retry, header, unsigned(h)) &&
	      !memcmp(retry + payloadOffset, payload, sizeof(payload)),
	    "traffic short retry after failed unprotect did not recover");

	  plainLen = Zquic::PktProt::unprotectShort(
	    packet, unsigned(n), rxShort, 0, pnOffset, pn, payloadOffset);
  ZuCHECK(plainLen == int(sizeof(payload)) &&
      pn == 11 && payloadOffset == unsigned(h),
    "traffic short unprotect metadata mismatch");
  ZuCHECK(!memcmp(packet, header, unsigned(h)) &&
      !memcmp(packet + payloadOffset, payload, sizeof(payload)),
    "traffic short unprotect plaintext mismatch");
  ZuCHECK(txLong.aead.get() == txLongAead && txLong.hp.get() == txLongHP &&
      txLong.hpSupp.get() == txLongHPSupp &&
      rxLong.aead.get() == rxLongAead && rxLong.hp.get() == rxLongHP &&
      txShort.aead.get() == txShortAead && txShort.hp.get() == txShortHP &&
      txShort.hpSupp.get() == txShortHPSupp &&
      rxShort.aead.get() == rxShortAead && rxShort.hp.get() == rxShortHP,
    "traffic packet protection contexts were not reused");

  Zquic::PktProtState txZero;
  ZuCHECK(txZero.init(secret, Zquic::CryptoLevel::Handshake, true),
    "traffic zero-vector state init failed");
  static constexpr unsigned ZeroPNLength = 4;
  int zh = Zquic::Pkt::writeHandshake(
    header, sizeof(header), dcid, scid, secret.tagLen, ZeroPNLength);
  ZuCHECK(zh > 0, "traffic zero-vector header encode failed");
  unsigned zpnOffset = unsigned(zh);
  ZuCHECK(Zquic::PktNumber::encode(
      header + zh, sizeof(header) - unsigned(zh), 23, ZeroPNLength) ==
      int(ZeroPNLength),
    "traffic zero-vector packet number encode failed");
  zh += ZeroPNLength;
  int zn = Zquic::PktProt::protectLongV(
    packetV, sizeof(packetV), txZero, 23, bytes_(header, unsigned(zh)),
    nullptr, 0, zpnOffset, ZeroPNLength);
  ZuCHECK(zn == int(unsigned(zh) + secret.tagLen),
    "traffic zero-vector protection failed");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testRFC9001InitialKeys);
  ZuTestCall(testInitialProt);
  ZuTestCall(testRFC9001ClientInitialProt);
  ZuTestCall(testHdrProt);
  ZuTestCall(testTrafficSecretProt);
}
