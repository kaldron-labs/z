//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>

#include <zlib/ZuHex.hh>
#include <zlib/ZuTestUtil.hh>
#include <zlib/ZtlsAEAD.hh>

using namespace ZuTestUtil;

static bool decode(ZuSpan<uint8_t> out, ZuCSpan hex)
{
  return hex.length() == out.length() << 1 && ZuHex::decode<false>(out, hex) == out.length();
}

static bool filled(ZuBSpan bytes, uint8_t value)
{
  for (auto c : bytes) if (c != value) return false;
  return true;
}

static void vector()
{
  ZuTestScope(vector);
  // RFC 8439 section 2.8.2.
  constexpr auto input =
    "Ladies and Gentlemen of the class of '99: If I could offer you only one tip "
    "for the future, sunscreen would be it."_z;
  uint8_t key[32], nonce[12], aad[12], expected[input.length() + 16];
  for (unsigned i = 0; i < sizeof(key); ++i) key[i] = 0x80 + i;
  ZuCheck(decode(nonce, "070000004041424344454647"));
  ZuCheck(decode(aad, "50515253c0c1c2c3c4c5c6c7"));
  ZuCheck(decode(expected,
    "d31a8d34648e60db7b86afbc53ef7ec2a4aded51296e08fea9e2b5a736ee62d6"
    "3dbea45e8ca9671282fafb69da92728b1a71de0a9e060b2905d6a5b67ecd3b36"
    "92ddbd7f2d778b8c9803aee328091b58fab324e4fad675945585808b4831d7bc3"
    "ff4def08e4b7a9de576d26586cec64b6116"
    "1ae10b594f09e26a7e902ecbd0600691"));
  uint8_t output[sizeof(expected) + 8];
  memset(output, 0xa5, sizeof(output));
  Ztls::ChaCha20Poly1305 enc, dec;
  ZuCheck(enc.init(true, key));
  ZuCheck(dec.init(false, key));
  ZuCheck(enc.seal(nonce, aad, input, output));
  ZuCheck(!memcmp(output, expected, sizeof(expected)));
  ZuCheck(filled({output + sizeof(expected), 8}, 0xa5));
  // Exact in-place decrypt and unused output tail.
  ZuCheck(dec.open(nonce, aad, {output, sizeof(expected)}, output));
  ZuCheck(!memcmp(output, input.data(), input.length()));
  ZuCheck(filled({output + sizeof(expected), 8}, 0xa5));
  // Repeated messages retain key/direction while resetting the nonce state.
  memcpy(output, input.data(), input.length());
  ZuCheck(enc.seal(nonce, aad, {output, input.length()}, output));
  ZuCheck(!memcmp(output, expected, sizeof(expected)));
  ZuCheck(dec.open(nonce, aad, expected, {output, input.length()}));
  ZuCheck(!memcmp(output, input.data(), input.length()));
  // Wrong AAD fails, clears all provisional plaintext, and invalidates dec.
  aad[0] ^= 1;
  memset(output, 0xa5, sizeof(output));
  ZuCheck(!dec.open(nonce, aad, expected, output));
  ZuCheck(filled({output, input.length()}, 0));
  ZuCheck(filled({output + input.length(), sizeof(output) - input.length()}, 0xa5));
  aad[0] ^= 1;
  ZuCheck(!dec.open(nonce, aad, expected, output));
  ZuCheck(dec.init(false, key));
  memcpy(output, expected, sizeof(expected));
  output[sizeof(expected) - 1] ^= 1;
  ZuCheck(!dec.open(nonce, aad, {output, sizeof(expected)}, output));
  ZuCheck(filled({output, input.length()}, 0));
  ZuCheck(dec.init(false, key));
  ZuCheck(dec.open(nonce, aad, expected, output));
}

static void empty()
{
  ZuTestScope(empty);
  uint8_t key[Ztls::ChaCha20Poly1305::KeySize]{};
  uint8_t nonce[Ztls::ChaCha20Poly1305::NonceSize]{};
  uint8_t tag[Ztls::ChaCha20Poly1305::TagSize];
  Ztls::ChaCha20Poly1305 enc, dec;
  ZuCheck(enc.init(true, key));
  ZuCheck(dec.init(false, key));
  ZuCheck(enc.seal(nonce, {}, {}, tag));
  uint8_t expected[sizeof(tag)];
  // Independent ChaCha20-Poly1305 zero-key/zero-nonce empty-message tag.
  ZuCheck(decode(expected, "4eb972c9a8fb3a1b382bb4d36f5ffad1"));
  ZuCheck(!memcmp(tag, expected, sizeof(tag)));
  ZuCheck(dec.open(nonce, {}, tag, {}));
  nonce[sizeof(nonce) - 1] = 1;
  ZuCheck(enc.seal(nonce, {}, {}, tag));
  ZuCheck(memcmp(tag, expected, sizeof(tag)) != 0);
  ZuCheck(dec.open(nonce, {}, tag, {}));
  tag[0] ^= 1;
  ZuCheck(!dec.open(nonce, {}, tag, {}));
}

static void errors()
{
  ZuTestScope(errors);
  uint8_t key[Ztls::ChaCha20Poly1305::KeySize]{};
  uint8_t nonce[Ztls::ChaCha20Poly1305::NonceSize]{};
  uint8_t output[Ztls::ChaCha20Poly1305::TagSize];
  Ztls::ChaCha20Poly1305 aead;
  ZuCheck(!aead.init(true, {key, sizeof(key) - 1}));
  memset(output, 0xa5, sizeof(output));
  ZuCheck(!aead.seal(nonce, {}, {}, output));
  ZuCheck(filled(output, 0));
  ZuCheck(aead.init(true, key));
  memset(output, 0xa5, sizeof(output));
  ZuCheck(!aead.seal(nonce, {}, {}, {output, sizeof(output) - 1}));
  ZuCheck(filled({output, sizeof(output) - 1}, 0));
  ZuCheck(output[sizeof(output) - 1] == 0xa5);
  ZuCheck(aead.init(true, key));
  ZuCheck(!aead.seal({nonce, sizeof(nonce) - 1}, {}, {}, output));
  ZuCheck(aead.init(false, key));
  ZuCheck(!aead.seal(nonce, {}, {}, output));
  ZuCheck(aead.init(false, key));
  memset(output, 0xa5, sizeof(output));
  ZuCheck(!aead.open(nonce, {}, {output, sizeof(output) - 1}, output));
  ZuCheck(filled(output, 0));
  ZuCheck(aead.init(true, key));
  ZuCheck(aead.seal(nonce, {}, {}, output));
}

int main()
{
  ZuTestMain();
  ZuTestCall_("vector", vector);
  ZuTestCall_("empty", empty);
  ZuTestCall_("errors", errors);
}
