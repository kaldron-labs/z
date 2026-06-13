//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZtArray.hh>
#include <zlib/ZhttpHPack.hh>

using namespace ZuTestUtil;

using HdrBytes = ZtArray<uint8_t, ZtArrayHeapID<"Zhttp.H3.HPackTest.Bytes">>;

static ZuCSpan bytes_(const uint8_t *data, unsigned length)
{
  return ZuCSpan{reinterpret_cast<const char *>(data), length};
}

static ZuCSpan bytes_(const HdrBytes &data)
{
  return bytes_(data.data(), data.length());
}

static ZuBSpan bspan_(ZuCSpan data)
{
  return ZuBSpan{
    reinterpret_cast<const uint8_t *>(data.data()), data.length()};
}

static bool encodeEq_(ZuCSpan in, const uint8_t *expected, unsigned n)
{
  HdrBytes encoded;
  encoded.length(Zhttp::H3::HPack::enclen(in.length()));
  uint64_t used = Zhttp::H3::HPack::encode(
    ZuSpan<uint8_t>{encoded.data(), encoded.length()}, bspan_(in));
  encoded.length(used);
  return used == n &&
    bytes_(encoded) == bytes_(expected, n);
}

static bool decodeEq_(const uint8_t *in, unsigned n, ZuCSpan expected)
{
  HdrBytes decoded;
  decoded.length(Zhttp::H3::HPack::declen(n));
  int64_t used = Zhttp::H3::HPack::decode(
    decoded.span(), ZuBSpan{in, n});
  decoded.length(used < 0 ? 0 : uint64_t(used));
  return used == int64_t(expected.length()) &&
    bytes_(decoded) == expected;
}

static bool roundTrip_(ZuCSpan in)
{
  HdrBytes encoded;
  HdrBytes decoded;
  encoded.length(Zhttp::H3::HPack::enclen(in.length()));
  uint64_t encodedLen = Zhttp::H3::HPack::encode(
    ZuSpan<uint8_t>{encoded.data(), encoded.length()}, bspan_(in));
  encoded.length(encodedLen);
  decoded.length(Zhttp::H3::HPack::declen(encoded.length()));
  int64_t decodedLen = Zhttp::H3::HPack::decode(
    decoded.span(),
    ZuBSpan{encoded.data(), encoded.length()});
  decoded.length(decodedLen < 0 ? 0 : uint64_t(decodedLen));
  return decodedLen == int64_t(in.length()) &&
    bytes_(decoded) == in;
}

void testHPackHuffmanKnownVectors()
{
  ZuTestScope(testHPackHuffmanKnownVectors);

  uint8_t www[] = {
    0xf1, 0xe3, 0xc2, 0xe5, 0xf2, 0x3a, 0x6b, 0xa0,
    0xab, 0x90, 0xf4, 0xff
  };
  ZuCHECK(decodeEq_(www, sizeof(www), "www.example.com"),
    "HPACK Huffman RFC example decode failed");
  ZuCHECK(encodeEq_("www.example.com", www, sizeof(www)),
    "HPACK Huffman RFC example encode failed");

  uint8_t highOctets[] = {
    0xff, 0xfe, 0x6f, 0xff, 0xff, 0xbb
  };
  uint8_t highRaw[] = { 0x80, 0xff };
  ZuCHECK(decodeEq_(highOctets, sizeof(highOctets),
      bytes_(highRaw, sizeof(highRaw))),
    "HPACK Huffman high-octet decode failed");
  ZuCHECK(encodeEq_(bytes_(highRaw, sizeof(highRaw)),
      highOctets, sizeof(highOctets)),
    "HPACK Huffman high-octet encode failed");

  uint8_t zero[] = { 0x07 };
  ZuCHECK(decodeEq_(zero, sizeof(zero), "0"),
    "HPACK Huffman single 5-bit symbol decode failed");
  ZuCHECK(encodeEq_("0", zero, sizeof(zero)),
    "HPACK Huffman single 5-bit symbol encode failed");

  uint8_t space[] = { 0x53 };
  ZuCHECK(decodeEq_(space, sizeof(space), " "),
    "HPACK Huffman single 6-bit symbol decode failed");
  ZuCHECK(encodeEq_(" ", space, sizeof(space)),
    "HPACK Huffman single 6-bit symbol encode failed");

  uint8_t amp[] = { 0xf8 };
  ZuCHECK(decodeEq_(amp, sizeof(amp), "&"),
    "HPACK Huffman single 8-bit symbol decode failed");
  ZuCHECK(encodeEq_("&", amp, sizeof(amp)),
    "HPACK Huffman single 8-bit symbol encode failed");

  uint8_t packedZeros[] = { 0x00, 0x3f };
  ZuCHECK(decodeEq_(packedZeros, sizeof(packedZeros), "00"),
    "HPACK Huffman packed symbols decode failed");
  ZuCHECK(encodeEq_("00", packedZeros, sizeof(packedZeros)),
    "HPACK Huffman packed symbols encode failed");
}

void testHPackHuffmanRoundTrips()
{
  ZuTestScope(testHPackHuffmanRoundTrips);

  ZuCHECK(roundTrip_(""), "HPACK Huffman empty string round trip failed");
  ZuCHECK(roundTrip_(
      ":method: GET\r\naccept-encoding: gzip, deflate, br\r\n"),
    "HPACK Huffman HTTP-ish ASCII round trip failed");

  uint8_t all[256];
  for (unsigned i = 0; i < 256; ++i) all[i] = uint8_t(i);
  ZuCHECK(roundTrip_(bytes_(all, sizeof(all))),
    "HPACK Huffman all-octet sequence round trip failed");

  bool allSingles = true;
  for (unsigned i = 0; i < 256; ++i) {
    uint8_t one = uint8_t(i);
    if (!roundTrip_(bytes_(&one, 1))) allSingles = false;
  }
  ZuCHECK(allSingles, "HPACK Huffman single-octet round trips failed");

  uint8_t buf[257];
  bool allVariable = true;
  for (unsigned n = 1; n <= sizeof(buf); ++n) {
    for (unsigned i = 0; i < n; ++i)
      buf[i] = uint8_t((i*37U + n*11U + (i>>1)) & 0xffU);
    if (!roundTrip_(bytes_(buf, n))) allVariable = false;
  }
  ZuCHECK(allVariable, "HPACK Huffman variable-length round trips failed");
}

void testHPackHuffmanLengths()
{
  ZuTestScope(testHPackHuffmanLengths);

  ZuCHECK(Zhttp::H3::HPack::enclen(0) == 0,
    "HPACK Huffman zero encode length mismatch");
  ZuCHECK(Zhttp::H3::HPack::enclen(1) == 4,
    "HPACK Huffman one-byte encode length mismatch");
  ZuCHECK(Zhttp::H3::HPack::enclen(2) == 8,
    "HPACK Huffman two-byte encode length mismatch");
  ZuCHECK(Zhttp::H3::HPack::enclen(3) == 12,
    "HPACK Huffman three-byte encode length mismatch");
  ZuCHECK(Zhttp::H3::HPack::enclen(4) == 15,
    "HPACK Huffman four-byte encode length mismatch");
  ZuCHECK(Zhttp::H3::HPack::enclen(5) == 19,
    "HPACK Huffman five-byte encode length mismatch");

  ZuCHECK(Zhttp::H3::HPack::declen(0) == 0,
    "HPACK Huffman zero decode length mismatch");
  ZuCHECK(Zhttp::H3::HPack::declen(1) == 1,
    "HPACK Huffman one-byte decode length mismatch");
  ZuCHECK(Zhttp::H3::HPack::declen(2) == 3,
    "HPACK Huffman two-byte decode length mismatch");
  ZuCHECK(Zhttp::H3::HPack::declen(3) == 4,
    "HPACK Huffman three-byte decode length mismatch");
  ZuCHECK(Zhttp::H3::HPack::declen(4) == 6,
    "HPACK Huffman four-byte decode length mismatch");
  ZuCHECK(Zhttp::H3::HPack::declen(5) == 8,
    "HPACK Huffman five-byte decode length mismatch");
  ZuCHECK(Zhttp::H3::HPack::declen(12) == 19,
    "HPACK Huffman RFC vector decode length mismatch");
}

void testHPackHuffmanMalformed()
{
  ZuTestScope(testHPackHuffmanMalformed);

  HdrBytes decoded;

  uint8_t eosBadPad[] = { 0xff, 0xff, 0xff, 0xfc };
  decoded.length(Zhttp::H3::HPack::declen(sizeof(eosBadPad)));
  ZuCHECK(Zhttp::H3::HPack::decode(
      decoded.span(),
      ZuBSpan{eosBadPad, sizeof(eosBadPad)}) < 0,
    "HPACK Huffman accepted EOS symbol");

  uint8_t eosGoodPad[] = { 0xff, 0xff, 0xff, 0xff };
  decoded.length(Zhttp::H3::HPack::declen(sizeof(eosGoodPad)));
  ZuCHECK(Zhttp::H3::HPack::decode(
      decoded.span(),
      ZuBSpan{eosGoodPad, sizeof(eosGoodPad)}) < 0,
    "HPACK Huffman accepted EOS symbol with valid-looking padding");

  uint8_t overlongPadding[] = { 0xff };
  decoded.length(Zhttp::H3::HPack::declen(sizeof(overlongPadding)));
  ZuCHECK(Zhttp::H3::HPack::decode(
      decoded.span(),
      ZuBSpan{overlongPadding, sizeof(overlongPadding)}) < 0,
    "HPACK Huffman accepted overlong padding");

  uint8_t badPadding[] = { 0x00 };
  decoded.length(Zhttp::H3::HPack::declen(sizeof(badPadding)));
  ZuCHECK(Zhttp::H3::HPack::decode(
      decoded.span(),
      ZuBSpan{badPadding, sizeof(badPadding)}) < 0,
    "HPACK Huffman accepted zero padding");

  uint8_t truncatedLong[] = { 0xff, 0xfe };
  decoded.length(Zhttp::H3::HPack::declen(sizeof(truncatedLong)));
  ZuCHECK(Zhttp::H3::HPack::decode(
      decoded.span(),
      ZuBSpan{truncatedLong, sizeof(truncatedLong)}) < 0,
    "HPACK Huffman accepted truncated long symbol");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testHPackHuffmanKnownVectors);
  ZuTestCall(testHPackHuffmanRoundTrips);
  ZuTestCall(testHPackHuffmanLengths);
  ZuTestCall(testHPackHuffmanMalformed);
}
