//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZePlatform.hh>
#include <zlib/ZtlsMD.hh>
#include <zlib/ZtlsHMAC.hh>

using namespace ZuTestUtil;

static bool matches(ZuBSpan input, ZuCSpan hex)
{
  constexpr auto Digits = "0123456789abcdef"_z;
  if (hex.length() != (input.length()<<1)) return false;
  for (unsigned i = 0, n = input.length(); i < n; ++i) {
    auto byte = input[i];
    if (hex[i<<1] != Digits[byte>>4] || hex[(i<<1) + 1] != Digits[byte & 15])
      return false;
  }
  return true;
}

template <typename L> static bool throws(L l)
{
  try { l(); }
  catch (const ZeException &) { return true; }
  return false;
}

template <Ztls::MDType Type>
static bool digest(ZuCSpan abc, ZuCSpan abcd, ZuCSpan empty)
{
  enum { Size = Ztls::MD<Type>::Size };
  uint8_t output[Size + 1];
  memset(output, 0x5a, sizeof(output));
  Ztls::MD<Type> md;
  md.update(ZuBSpan{"a"});
  md.update(ZuBSpan{"bc"});
  md.finish(output);
  if (!matches({output, Size}, abc) || output[Size] != 0x5a) return false;
  md.finish(output);
  if (!matches({output, Size}, abc)) return false;
  md.update(ZuBSpan{"d"});
  md.finish(output);
  if (!matches({output, Size}, abcd)) return false;
  md.reset();
  md.finish(output);
  return matches({output, Size}, empty) && output[Size] == 0x5a;
}

static void digests()
{
  ZuTestScope(digests);
  ZuCheck(digest<Ztls::SHA1>(
      "a9993e364706816aba3e25717850c26c9cd0d89d",
      "81fe8bfe87576c3ecb22426f8e57847382917acf",
      "da39a3ee5e6b4b0d3255bfef95601890afd80709"));
  ZuCheck(digest<Ztls::SHA256>(
      "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
      "88d4266fd4e6338d13b845fcf289579d209c897823b9217da3e161936f031589",
      "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
  ZuCheck(digest<Ztls::SHA384>(
      "cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed"
      "8086072ba1e7cc2358baeca134c825a7",
      "1165b3406ff0b52a3d24721f785462ca2276c9f454a116c2b2ba20171a7905ea"
      "5a026682eb659c4d5f115c363aa3c79b",
      "38b060a751ac96384cd9327eb1b1e36a21fdb71114be07434c0cc7bf63f6e1da"
      "274edebfe76f65fbd51ad2f14898b95b"));
  ZuCheck(digest<Ztls::SHA512>(
      "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a"
      "2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f",
      "d8022f2060ad6efd297ab73dcc5355c9b214054b0d1776a136a669d26a7d3b14"
      "f73aa0d0ebff19ee333368f0164b6419a96da49e3e481753e7e96b716bdccb6f",
      "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce"
      "47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e"));
}

template <Ztls::MDType Type>
static bool keyed(ZuBSpan key, ZuBSpan data, ZuCSpan expected)
{
  enum { Size = Ztls::HMAC<Type>::Size };
  uint8_t output[Size + 1];
  memset(output, 0x5a, sizeof(output));
  Ztls::HMAC<Type> mac;
  mac.start(key);
  mac.update(data);
  mac.finish(output);
  if (!matches({output, Size}, expected) || output[Size] != 0x5a) return false;
  mac.update({data.data(), data.length()/2});
  mac.update({data.length() ? data.data() + data.length()/2 : data.data(),
    data.length() - data.length()/2});
  mac.finish(output);
  if (!matches({output, Size}, expected)) return false;
  mac.update(ZuBSpan{"discard"});
  mac.reset();
  mac.update(data);
  mac.finish(output);
  if (!matches({output, Size}, expected)) return false;
  mac.start(ZuBSpan{"discarded key"});
  mac.start(key);
  mac.update(data);
  mac.finish(output);
  return matches({output, Size}, expected) && output[Size] == 0x5a;
}

static void macs()
{
  ZuTestScope(macs);
  const ZuBSpan text{"The quick brown fox jumps over the lazy dog"};
  const ZuBSpan key{"key"};
  ZuCheck(keyed<Ztls::SHA1>(key, text,
    "de7c9b85b8b78aa6bc8a7a36f70a90701c9db4d9"));
  ZuCheck(keyed<Ztls::SHA256>(key, text,
    "f7bc83f430538424b13298e6aa6fb143ef4d59a14946175997479dbc2d1a3cd8"));
  ZuCheck(keyed<Ztls::SHA384>(key, text,
    "d7f4727e2c0b39ae0f1e40cc96f60242d5b7801841cea6fc592c5d3e1ae507005"
    "82a96cf35e1e554995fe4e03381c237"));
  ZuCheck(keyed<Ztls::SHA512>(key, text,
    "b42af09057bac1e2d41708e48a902e09b5ff7f12ab428a4fe86653c73dd248fb82"
    "f948a549f7b791a5b41915ee4d1ec3935357e4e2317250d0372afa2ebeeb3a"));

  // RFC 4231 case 6 and independent boundary fixtures from Python hmac/hashlib.
  enum { LongKeySize = 131 };
  uint8_t longKey[LongKeySize];
  memset(longKey, 0xaa, sizeof(longKey));
  const ZuBSpan message{"Test Using Larger Than Block-Size Key - Hash Key First"};
  ZuCheck(keyed<Ztls::SHA256>({}, message,
    "79e2a6536cb16b4a1e9c2af81759764afd026173526f79f7816e9a3bc5fbaba8"));
  ZuCheck(keyed<Ztls::SHA256>({longKey, 64}, message,
    "84332a7580ed3cf75de83c644c8d2c1c262ad90e0190e5c5ae4b82b2102e8e75"));
  ZuCheck(keyed<Ztls::SHA256>({longKey, 65}, message,
    "c62955a96944ff68deabbc0eab6192065c1c55bb8ddee16151ed5337f911eab9"));
  ZuCheck(keyed<Ztls::SHA256>(longKey, message,
    "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54"));
  ZuCheck(keyed<Ztls::SHA256>({}, {},
    "b613679a0814d9ec772f95d778c35fc5ff1697c493715653c6c712144292c5ad"));
}

static void counters()
{
  ZuTestScope(counters);
  // RFC 4226 fixed counter messages. These test the HMAC primitive, not TOTP truncation.
  const ZuBSpan key{"12345678901234567890"};
  uint8_t counter[8]{};
  ZuCheck(keyed<Ztls::SHA1>(key, counter,
    "cc93cf18508d94934c64b65d8ba7667fb7cde4b0"));
  counter[7] = 1;
  ZuCheck(keyed<Ztls::SHA1>(key, counter,
    "75a48a19d4cbe100644e8ac1397eea747a2d33ab"));
}

static bool zero(ZuBSpan input)
{
  for (auto byte : input) if (byte) return false;
  return true;
}

static void recovery()
{
  ZuTestScope(recovery);
  enum { Size = Ztls::MD<>::Size };
  uint8_t output[Size + 1];
  memset(output, 0x5a, sizeof(output));
  Ztls::MD<> md;
  md.update(ZuBSpan{"abc"});
  ZuCheck(throws([&md, &output]() { md.finish({output, Size - 1}); }));
  ZuCheck(zero({output, Size - 1}) && output[Size - 1] == 0x5a);
  memset(output, 0x5a, sizeof(output));
  ZuCheck(throws([&md, &output]() { md.finish(output); }));
  ZuCheck(zero({output, Size}) && output[Size] == 0x5a);
  ZuCheck(throws([&md]() { md.update(ZuBSpan{"invalid"}); }));
  md.reset();
  md.update(ZuBSpan{"abc"});
  md.finish(output);
  ZuCheck(matches({output, Size},
    "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));

  Ztls::HMAC<> mac;
  ZuCheck(throws([&mac]() { mac.update(ZuBSpan{"unkeyed"}); }));
  mac.start(ZuBSpan{"key"});
  memset(output, 0x5a, sizeof(output));
  ZuCheck(throws([&mac, &output]() { mac.finish({output, Size - 1}); }));
  ZuCheck(zero({output, Size - 1}) && output[Size - 1] == 0x5a);
  ZuCheck(throws([&mac]() { mac.reset(); }));
  mac.start({});
  mac.finish(output);
  ZuCheck(matches({output, Size},
    "b613679a0814d9ec772f95d778c35fc5ff1697c493715653c6c712144292c5ad"));
}

static void extended()
{
  ZuTestScope(extended);
  uint8_t output[32];
  ZuBSpan parts[] = {ZuBSpan{"a"}, ZuBSpan{"bc"}};
  ZuCheck(Ztls::sha3_256(parts, output));
  ZuCheck(matches(output,
    "3a985da74fe225b2045c172d6bd390bd855f086e3e9d525b46bfe24511431532"));
  ZuCheck(Ztls::shake256(ZuBSpan{"abc"}, output));
  ZuCheck(matches(output,
    "483366601360a8771c6863080cc4114d8db44530f8f1e1ee4f94ea37e78b5739"));
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(digests);
  ZuTestCall(macs);
  ZuTestCall(counters);
  ZuTestCall(recovery);
  ZuTestCall(extended);
  return 0;
}
