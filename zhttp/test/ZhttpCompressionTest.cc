//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZtArray.hh>
#include <zlib/ZhttpCompression.hh>

using namespace ZuTestUtil;

using HdrBytes =
  ZtArray<uint8_t, ZtArrayHeapID<"Zhttp.CompressionTest.Bytes">>;

struct StreamValue {
  template <typename S>
  void print(S &s) const {
    ++*calls;
    s << "foo" << "bar";
  }

  friend ZuPrintFn ZuPrintType(StreamValue *);

  unsigned *calls = nullptr;
};

static bool encodeEq_(ZuCSpan in, const uint8_t *expected, unsigned n)
{
  HdrBytes encoded;
  encoded.length(Zhttp::Compression::Huffman::enclen(in.length()));
  uint64_t used = Zhttp::Compression::Huffman::encode(encoded.span(), in);
  encoded.length(used);
  return used == n &&
    encoded == ZuBSpan{expected, n};
}

static bool decodeEq_(const uint8_t *in, unsigned n, ZuCSpan expected)
{
  HdrBytes decoded;
  decoded.length(Zhttp::Compression::Huffman::declen(n));
  int64_t used = Zhttp::Compression::Huffman::decode(
    decoded.span(), ZuBSpan{in, n});
  decoded.length(used < 0 ? 0 : uint64_t(used));
  return used == int64_t(expected.length()) &&
    decoded == expected;
}

static bool roundTrip_(ZuCSpan in)
{
  HdrBytes encoded;
  HdrBytes decoded;
  encoded.length(Zhttp::Compression::Huffman::enclen(in.length()));
  uint64_t encodedLen =
    Zhttp::Compression::Huffman::encode(encoded.span(), in);
  encoded.length(encodedLen);
  decoded.length(
    Zhttp::Compression::Huffman::declen(encoded.length()));
  int64_t decodedLen =
    Zhttp::Compression::Huffman::decode(decoded.span(), encoded);
  decoded.length(decodedLen < 0 ? 0 : uint64_t(decodedLen));
  return decodedLen == int64_t(in.length()) &&
    decoded == in;
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
      ZuBSpan{highRaw}),
    "HPACK Huffman high-octet decode failed");
  ZuCHECK(encodeEq_(ZuBSpan{highRaw},
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
  ZuCHECK(roundTrip_(ZuBSpan{all}),
    "HPACK Huffman all-octet sequence round trip failed");

  bool allSingles = true;
  for (unsigned i = 0; i < 256; ++i) {
    uint8_t one = uint8_t(i);
    if (!roundTrip_(ZuBSpan{&one, 1})) allSingles = false;
  }
  ZuCHECK(allSingles, "HPACK Huffman single-octet round trips failed");

  uint8_t buf[257];
  bool allVariable = true;
  for (unsigned n = 1; n <= sizeof(buf); ++n) {
    for (unsigned i = 0; i < n; ++i)
      buf[i] = uint8_t((i*37U + n*11U + (i>>1)) & 0xffU);
    if (!roundTrip_(ZuBSpan{buf, n})) allVariable = false;
  }
  ZuCHECK(allVariable, "HPACK Huffman variable-length round trips failed");
}

void testHPackHuffmanLengths()
{
  ZuTestScope(testHPackHuffmanLengths);

  ZuCHECK(Zhttp::Compression::Huffman::enclen(0) == 0,
    "HPACK Huffman zero encode length mismatch");
  ZuCHECK(Zhttp::Compression::Huffman::enclen(1) == 4,
    "HPACK Huffman one-byte encode length mismatch");
  ZuCHECK(Zhttp::Compression::Huffman::enclen(2) == 8,
    "HPACK Huffman two-byte encode length mismatch");
  ZuCHECK(Zhttp::Compression::Huffman::enclen(3) == 12,
    "HPACK Huffman three-byte encode length mismatch");
  ZuCHECK(Zhttp::Compression::Huffman::enclen(4) == 15,
    "HPACK Huffman four-byte encode length mismatch");
  ZuCHECK(Zhttp::Compression::Huffman::enclen(5) == 19,
    "HPACK Huffman five-byte encode length mismatch");

  ZuCHECK(Zhttp::Compression::Huffman::declen(0) == 0,
    "HPACK Huffman zero decode length mismatch");
  ZuCHECK(Zhttp::Compression::Huffman::declen(1) == 1,
    "HPACK Huffman one-byte decode length mismatch");
  ZuCHECK(Zhttp::Compression::Huffman::declen(2) == 3,
    "HPACK Huffman two-byte decode length mismatch");
  ZuCHECK(Zhttp::Compression::Huffman::declen(3) == 4,
    "HPACK Huffman three-byte decode length mismatch");
  ZuCHECK(Zhttp::Compression::Huffman::declen(4) == 6,
    "HPACK Huffman four-byte decode length mismatch");
  ZuCHECK(Zhttp::Compression::Huffman::declen(5) == 8,
    "HPACK Huffman five-byte decode length mismatch");
  ZuCHECK(Zhttp::Compression::Huffman::declen(12) == 19,
    "HPACK Huffman RFC vector decode length mismatch");
}

void testHPackHuffmanMalformed()
{
  ZuTestScope(testHPackHuffmanMalformed);

  HdrBytes decoded;

  uint8_t eosBadPad[] = { 0xff, 0xff, 0xff, 0xfc };
  decoded.length(
    Zhttp::Compression::Huffman::declen(sizeof(eosBadPad)));
  ZuCHECK(Zhttp::Compression::Huffman::decode(
      decoded.span(),
      ZuBSpan{eosBadPad, sizeof(eosBadPad)}) < 0,
    "HPACK Huffman accepted EOS symbol");

  uint8_t eosGoodPad[] = { 0xff, 0xff, 0xff, 0xff };
  decoded.length(
    Zhttp::Compression::Huffman::declen(sizeof(eosGoodPad)));
  ZuCHECK(Zhttp::Compression::Huffman::decode(
      decoded.span(),
      ZuBSpan{eosGoodPad, sizeof(eosGoodPad)}) < 0,
    "HPACK Huffman accepted EOS symbol with valid-looking padding");

  uint8_t overlongPadding[] = { 0xff };
  decoded.length(
    Zhttp::Compression::Huffman::declen(sizeof(overlongPadding)));
  ZuCHECK(Zhttp::Compression::Huffman::decode(
      decoded.span(),
      ZuBSpan{overlongPadding, sizeof(overlongPadding)}) < 0,
    "HPACK Huffman accepted overlong padding");

  uint8_t badPadding[] = { 0x00 };
  decoded.length(
    Zhttp::Compression::Huffman::declen(sizeof(badPadding)));
  ZuCHECK(Zhttp::Compression::Huffman::decode(
      decoded.span(),
      ZuBSpan{badPadding, sizeof(badPadding)}) < 0,
    "HPACK Huffman accepted zero padding");

  uint8_t truncatedLong[] = { 0xff, 0xfe };
  decoded.length(
    Zhttp::Compression::Huffman::declen(sizeof(truncatedLong)));
  ZuCHECK(Zhttp::Compression::Huffman::decode(
      decoded.span(),
      ZuBSpan{truncatedLong, sizeof(truncatedLong)}) < 0,
    "HPACK Huffman accepted truncated long symbol");
}

void testPrefixIntegers()
{
  ZuTestScope(testPrefixIntegers);

  HdrBytes encoded;
  uint8_t ten[] = {0x0a};
  uint8_t thirteenThirtySeven[] = {0x1f, 0x9a, 0x0a};
  ZuCHECK(Zhttp::Compression::putPref(encoded, 0, 5, 10) == 0 &&
      encoded == ZuBSpan{ten},
    "encode one-byte prefix integer");
  encoded.length(0);
  ZuCHECK(Zhttp::Compression::putPref(encoded, 0, 5, 1337) == 0 &&
      encoded == ZuBSpan{thirteenThirtySeven},
    "encode RFC multi-byte prefix integer");

  bool splits = true;
  for (unsigned split = 1; split <= encoded.length(); ++split) {
    Zhttp::Compression::PrefInt decoder;
    uint64_t value = 0;
    int state = decoder.start<5>(encoded[0], value);
    unsigned offset = 1;
    if (split > 1)
      state = decoder.process(
	ZuBSpan{encoded.data(), split}, offset, value);
    if (state < 0) {
      splits = false;
      continue;
    }
    offset = split;
    state = decoder.process(encoded, offset, value);
    if (state != 1 || value != 1337 || offset != encoded.length())
      splits = false;
  }
  ZuCHECK(splits, "decode prefix integer at every byte split");

  unsigned offset = 0;
  uint64_t value = 0;
  uint8_t truncated[] = {0x1f, 0x9a};
  ZuCHECK(Zhttp::Compression::decodePref<5>(
      truncated, offset, value) == -2,
    "truncated prefix integer");
  uint8_t overflow[] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0x02
  };
  offset = 0;
  ZuCHECK(Zhttp::Compression::decodePref<8>(
      overflow, offset, value) == -1,
    "overflowing prefix integer");
}

static bool fragmentedString_(ZuBSpan encoded, ZuBSpan expected)
{
  for (unsigned split = 1; split <= encoded.length(); ++split) {
    Zhttp::Compression::StringDecoder<HdrBytes> decoder;
    HdrBytes storage;
    ZuBSpan value;
    int state = decoder.start<7, 0x80>(encoded[0], 1024);
    unsigned offset = 1;
    if (split > 1)
      state = decoder.process(
	ZuBSpan{encoded.data(), split}, offset);
    if (state < 0) return false;
    offset = split;
    state = decoder.process(encoded, offset);
    if (state != 1 || decoder.finish(storage, value) < 0 ||
	value != expected || offset != encoded.length())
      return false;
  }
  return true;
}

void testStrings()
{
  ZuTestScope(testStrings);

  HdrBytes raw;
  Zhttp::Compression::putString(raw, 0, 7, "www.example.com");
  ZuCHECK(fragmentedString_(raw, "www.example.com"),
    "decode raw string at every byte split");

  HdrBytes huffman;
  HdrBytes encoded;
  huffman.length(
    Zhttp::Compression::Huffman::enclen(sizeof("www.example.com") - 1));
  huffman.length(Zhttp::Compression::Huffman::encode(
    huffman.span(), ZuBSpan{"www.example.com"}));
  Zhttp::Compression::putPref(encoded, 0x80, 7, huffman.length());
  for (unsigned i = 0; i < huffman.length(); ++i)
    encoded.push(huffman[i]);
  ZuCHECK(fragmentedString_(encoded, "www.example.com"),
    "decode Huffman string at every byte split");
}

void testFieldSectionBuffer()
{
  ZuTestScope(testFieldSectionBuffer);

  HdrBytes bytes;
  Zhttp::Compression::FieldSectionBuffer section{bytes};
  bytes.push(0xaa);
  uint64_t offset = 0;
  unsigned length = 0;
  unsigned calls = 0;
  ZuCHECK(section.putPrint(
      0, 7, StreamValue{&calls}, offset, length) == 0,
    "stream printable emission failed");
  bytes.push(0xbb);

  HdrBytes wire;
  section.each(0, [&wire](ZuBSpan span) {
    Zhttp::Compression::putBytes(wire, span);
  });
  const uint8_t expected[] = {
    0xaa, 0x06, 'f', 'o', 'o', 'b', 'a', 'r', 0xbb
  };
  ZuBSpan value{bytes.data() + offset, length};
  ZuCHECK(calls == 1 && length == 6 &&
      value == "foobar" &&
      section.length() == sizeof(expected) &&
      wire == ZuBSpan{expected},
    "stream printable was copied, replayed, or mis-finalized");

  bytes.length(0);
  Zhttp::Compression::FieldSectionBuffer boundedSection{bytes};
  auto bounded = ZuBoxed(uint64_t(3));
  ZuCHECK(boundedSection.putPrint(
      0, 7, bounded, offset, length) == 0,
    "bounded printable emission failed");
  wire.length(0);
  boundedSection.each(0, [&wire](ZuBSpan span) {
    Zhttp::Compression::putBytes(wire, span);
  });
  const uint8_t boundedExpected[] = {0x01, '3'};
  value = {bytes.data() + offset, length};
  ZuCHECK(length == 1 && value == "3" &&
      boundedSection.length() == sizeof(boundedExpected) &&
      wire == ZuBSpan{boundedExpected},
    "bounded printable used its capacity instead of rendered length");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testHPackHuffmanKnownVectors);
  ZuTestCall(testHPackHuffmanRoundTrips);
  ZuTestCall(testHPackHuffmanLengths);
  ZuTestCall(testHPackHuffmanMalformed);
  ZuTestCall(testPrefixIntegers);
  ZuTestCall(testStrings);
  ZuTestCall(testFieldSectionBuffer);
}
