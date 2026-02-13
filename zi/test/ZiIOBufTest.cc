//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdlib.h>
#include <string.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmTrap.hh>

#include <zlib/ZtString.hh>

#include <zlib/ZiIOBuf.hh>

#include "ZiTestResidue.hh"

using namespace ZuTestUtil;

void testSliceAndEnsurePreserveSpan()
{
  ZuTestScope(testSliceAndEnsurePreserveSpan);

  ZmRef<ZiIOBuf> buf = new ZiIOBufAlloc<>{};
  *buf << "fbah";
  ZtString<> s;
  s << ZuCSpan(buf->cspan());
  ZuCheck(s == "fbah");
  ++buf->skip, --buf->length;
  s.clear();
  s << ZuCSpan(buf->cspan());
  ZuCheck(s == "bah");

  buf->clear();
  *buf << "0123456789";
  buf->skip = 2;
  buf->length = 5;
  ZtString<> before;
  before << ZuCSpan(buf->cspan());
  auto old_skip = buf->skip;
  auto old_length = buf->length;
  ZuCheck(buf->ensure(buf->size + 128));
  ZuCheck(buf->skip == old_skip);
  ZuCheck(buf->length == old_length);
  ZtString<> after;
  after << ZuCSpan(buf->cspan());
  ZuCheck(after == before);
}

void testAdvanceRewind()
{
  ZuTestScope(testAdvanceRewind);

  ZmRef<ZiIOBuf> buf = new ZiIOBufAlloc<>{};
  *buf << "abcdef";
  buf->advance(2);
  ZuCheck(buf->length == 4);
  ZuCheck(buf->skip == 2);
  ZtString<> s;
  s << ZuCSpan(buf->cspan());
  ZuCheck(s == "cdef");

  buf->rewind(1);
  ZuCheck(buf->length == 5);
  ZuCheck(buf->skip == 1);
  s.clear();
  s << ZuCSpan(buf->cspan());
  ZuCheck(s == "bcdef");

  buf->advance(1000);
  ZuCheck(buf->length == 0);
}

void testPrependAndGrow()
{
  ZuTestScope(testPrependAndGrow);

  ZmRef<ZiIOBuf> buf = new ZiIOBufAlloc<8, 4096>{};
  buf->clear();
  buf->skip = buf->size;
  uint8_t *tail = buf->prepend(4);
  if (!tail) {
    ZuCheck(false);
    return;
  }
  memcpy(tail, "tail", 4);

  auto oldSize = buf->size;

  // Force a prepend requiring growth.
  uint8_t *p = buf->prepend(32);
  if (!p) {
    ZuCheck(false);
    return;
  }
  ZuCheck(buf->length == 36);
  ZuCheck(buf->size >= oldSize);
}

void testSpanOffsetAndLargeEnsure()
{
  ZuTestScope(testSpanOffsetAndLargeEnsure);

  ZmRef<ZiIOBuf> buf = new ZiIOBufAlloc<16, 1 << 20>{};
  *buf << "0123456789";

  auto s = buf->cspan(1000);
  ZuCheck(s.length() == 0);

  unsigned large = 256 * 1024;
  ZuCheck(buf->ensure(large));
  ZuCheck(buf->size >= large);

  ZtString<> out;
  out << ZuCSpan(buf->cspan());
  ZuCheck(out == "0123456789");
}

int main(int argc, char **argv)
{
  ZiTestResidue::init("ZiIOBufTest");
  ZmTrap::sigintFn(&ZiTestResidue::cleanupNow);
  ZmTrap::trap();
  ::atexit(&ZiTestResidue::cleanupNow);

  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testSliceAndEnsurePreserveSpan);
  ZuTestCall(testAdvanceRewind);
  ZuTestCall(testPrependAndGrow);
  ZuTestCall(testSpanOffsetAndLargeEnsure);
  return 0;
}
