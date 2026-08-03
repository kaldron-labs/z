//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <cstdlib>
#include <cstring>

#include <zlib/ZuTestUtil.hh>
#include <zlib/zu_lib.h>
#include <zlib/zu_bitmap.h>

using namespace ZuTestUtil;

static void *bitmapAlloc(unsigned n)
{
  return std::malloc(n);
}

static void bitmapFree(void *p)
{
  std::free(p);
}

void testBitmapCAPI()
{
  ZuTestScope(testBitmapCAPI);

  const zu_bitmap_allocator allocator{&bitmapAlloc, &bitmapFree};

  zu_bitmap *b = zu_bitmap_new(&allocator, 8);
  ZuCheck(b);

  b = zu_bitmap_set(&allocator, b, 2);
  b = zu_bitmap_set_range(&allocator, b, 10, 13); // 10,11,12
  b = zu_bitmap_set_range(&allocator, b, 80, 83); // forces resize

  ZuCheck(zu_bitmap_get(b, 2));
  ZuCheck(zu_bitmap_get(b, 10));
  ZuCheck(zu_bitmap_get(b, 12));
  ZuCheck(zu_bitmap_get(b, 80));
  ZuCheck(zu_bitmap_length(b) >= 83);

  ZuCheck(zu_bitmap_first(b) == 2);
  ZuCheck(zu_bitmap_last(b) == 82);
  ZuCheck(zu_bitmap_next(b, 2) == 10);
  ZuCheck(zu_bitmap_prev(b, 82) == 81);

  unsigned outLen = zu_bitmap_out_len(b);
  auto *text = static_cast<char *>(std::malloc(outLen));
  ZuCheck(text);
  ZuCheck(zu_bitmap_out(text, outLen, b));

  zu_bitmap *parsed = nullptr;
  unsigned scanned = zu_bitmap_in(&allocator, &parsed, text);
  ZuCheck(parsed);
  ZuCheck(scanned == std::strlen(text));
  ZuCheck(zu_bitmap_cmp(b, parsed) == 0);
  ZuCheck(zu_bitmap_hash(b) == zu_bitmap_hash(parsed));

  zu_bitmap *d = zu_bitmap_new(&allocator, 96);
  ZuCheck(d);
  d = zu_bitmap_set_range(&allocator, d, 81, 84); // 81,82,83

  zu_bitmap *orv = nullptr;
  zu_bitmap *andv = nullptr;
  zu_bitmap *xorv = nullptr;
  ZuCheck(zu_bitmap_in(&allocator, &orv, text) > 0);
  ZuCheck(zu_bitmap_in(&allocator, &andv, text) > 0);
  ZuCheck(zu_bitmap_in(&allocator, &xorv, text) > 0);
  ZuCheck(orv && andv && xorv);

  orv = zu_bitmap_or(&allocator, orv, d);
  andv = zu_bitmap_and(&allocator, andv, d);
  xorv = zu_bitmap_xor(&allocator, xorv, d);

  ZuCheck(zu_bitmap_get(orv, 83));
  ZuCheck(zu_bitmap_get(andv, 81));
  ZuCheck(zu_bitmap_get(andv, 82));
  ZuCheck(!zu_bitmap_get(andv, 83));
  ZuCheck(zu_bitmap_get(xorv, 83));

  zu_bitmap_delete(&allocator, xorv);
  zu_bitmap_delete(&allocator, andv);
  zu_bitmap_delete(&allocator, orv);
  zu_bitmap_delete(&allocator, d);
  zu_bitmap_delete(&allocator, parsed);
  zu_bitmap_delete(&allocator, b);
  std::free(text);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testBitmapCAPI);
  return 0;
}
