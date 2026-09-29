//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdlib.h>
#include <string.h>

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZtString.hh>

#include <zlib/ZiIOBuf.hh>

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

struct HugePrint;
struct HugePrintFn : ZuPrintBuffer {
  static unsigned length(const HugePrint &) { return UINT_MAX; }
  static unsigned print(char *, unsigned, const HugePrint &) { ZmAssert(false); return 0; }
};
struct HugePrint { friend HugePrintFn ZuPrintType(HugePrint *); };
struct FailingDelegate {
  unsigned *calls;
  template <typename S> void print(S &s) const {
    ++*calls;
    s << HugePrint{} << "ignored";
  }
  friend ZuPrintFn ZuPrintType(FailingDelegate *);
};

void testOutputFailure()
{
  ZuTestScope(testOutputFailure);

  ZmRef<ZiIOBuf> buf = new ZiIOBufAlloc<>{};
  *buf << "prefix";
  ZuCheck(!buf->append(reinterpret_cast<const uint8_t *>("x"), UINT_MAX));
  ZuCheck(buf->failed());
  *buf << "ignored" << '!' << 42;
  ZuCheck(buf->cspan() == "prefix");
  ZuCheck(!buf->ensure(buf->length));

  buf->clear();
  ZuCheck(!buf->failed());
  *buf << 'a' << 42;
  ZuCheck(buf->cspan() == "a42");
  unsigned calls = 0;
  *buf << FailingDelegate{&calls};
  ZuCheck(buf->failed() && calls == 1);
  *buf << FailingDelegate{&calls};
  ZuCheck(calls == 1 && buf->cspan() == "a42");

  buf->clear();
  buf->skip = 1;
  *buf << HugePrint{};
  ZuCheck(buf->failed() && !buf->length);
  buf->clear();
  *buf << "prefix";
  auto deniedGrowth = [](unsigned size, unsigned) { return size; };
  ZuCheck(!buf->ensure<deniedGrowth>(buf->size + 1));
  ZuCheck(buf->failed() && buf->cspan() == "prefix");
  *buf << "ignored";
  ZuCheck(buf->cspan() == "prefix");

  buf->clear();
  *buf << "ok";
  ZuCheck(!buf->failed() && buf->cspan() == "ok");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testSliceAndEnsurePreserveSpan);
  ZuTestCall(testAdvanceRewind);
  ZuTestCall(testPrependAndGrow);
  ZuTestCall(testSpanOffsetAndLargeEnsure);
  ZuTestCall(testOutputFailure);
  return 0;
}
