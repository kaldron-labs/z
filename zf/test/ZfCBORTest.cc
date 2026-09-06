//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZfCBOR.hh>

using namespace ZuTestUtil;

static constexpr ZfCBOR::Limits limits{256, 4, 16, 64};

struct Seen {
  unsigned items = 0;
  ZuBSpan root;
};

static bool visit(void *ptr, const ZfCBOR::Item &item)
{
  auto &seen = *static_cast<Seen *>(ptr);
  ++seen.items;
  if (!item.depth) seen.root = item.encoded;
  return true;
}

static void cose()
{
  ZuTestScope(cose);
  // ES256 COSE_Key: kty, alg, crv, x, y.
  enum { CoordinateSize = 32, Size = 1 + 6 + 2 * (3 + CoordinateSize) };
  uint8_t key[Size] = {0xa5, 0x01, 0x02, 0x03, 0x26, 0x20, 0x01,
    0x21, 0x58, CoordinateSize};
  unsigned o = 10;
  for (unsigned i = 0; i < CoordinateSize; ++i) key[o++] = uint8_t(i);
  key[o++] = 0x22;
  key[o++] = 0x58;
  key[o++] = CoordinateSize;
  for (unsigned i = 0; i < CoordinateSize; ++i) key[o++] = uint8_t(i + 1);

  Seen seen;
  auto result = ZfCBOR::scan(key, limits, &seen, visit);
  ZuCheck(bool(result));
  ZuCheck(result.offset == Size);
  ZuCheck(seen.items == 11);
  ZuBSpan encoded{key, Size};
  ZuCheck(seen.root == encoded);
}

static void bounds()
{
  ZuTestScope(bounds);
  uint8_t truncated[] = {0x58, 0x02, 0x01};
  ZuCheck(ZfCBOR::scan(truncated, limits, nullptr, nullptr).error ==
    ZfCBOR::Error::Syntax);
  uint8_t nested[] = {0x81, 0x80};
  ZuCheck(ZfCBOR::scan(nested, {2, 1, 2, 1}, nullptr, nullptr).error ==
    ZfCBOR::Error::Depth);
  uint8_t data[] = {0x42, 0x01, 0x02};
  ZuCheck(ZfCBOR::scan(data, {3, 1, 1, 1}, nullptr, nullptr).error ==
    ZfCBOR::Error::Data);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(cose);
  ZuTestCall(bounds);
  return 0;
}
