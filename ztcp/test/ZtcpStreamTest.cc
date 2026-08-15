//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZtArray.hh>

#include <zlib/Ztcp.hh>

using namespace ZuTestUtil;

#define ZTCP_CHECK_RT(x, ...) ZuCheckRT(x, log_(__VA_ARGS__))

namespace {

using Buf = Ztcp::RxBufAlloc<16>;

ZmRef<ZiIOBuf> buf(ZuCSpan s)
{
  ZmRef<ZiIOBuf> b = new Buf{};
  *b << s;
  return b;
}

ZtArray<uint8_t> payload(unsigned n)
{
  ZtArray<uint8_t> p;
  p.length(n);
  for (unsigned i = 0; i < n; ++i) p[i] = uint8_t('a' + (i % 26));
  return p;
}

void pushPayload(Ztcp::RxStream &rx, const ZtArray<uint8_t> &p)
{
  unsigned off = 0, l = p.length();
  while (off < l) {
    unsigned n = l - off;
    if (n > 13) n = 13;
    ZmRef<ZiIOBuf> b = new Buf{};
    b->append(p.data() + off, n);
    rx.push(ZuMv(b));
    off += n;
  }
}

bool consumeExact(Ztcp::RxStream &rx, ZuBSpan expected)
{
  unsigned remaining = expected.length();
  bool ok = false;
  rx.consume(
    [&remaining](ZuSpan<uint8_t> span) -> int64_t {
      if (remaining > span.length()) {
	remaining -= span.length();
	return 0;
      }
      return remaining;
    },
    [&ok, expected](ZuSpan<uint8_t> span) {
      ok = span.length() == expected.length() &&
	!memcmp(span.data(), expected.data(), expected.length());
    });
  return ok;
}

void testBoundary()
{
  ZuTestScopeRT(testBoundary);

  auto p = payload(80);
  Ztcp::RxStream rx;
  pushPayload(rx, p);

  bool ok = false;
  int64_t n = rx.consume(
    [total = 0U](ZuSpan<uint8_t> span) mutable -> int64_t {
      total += span.length();
      if (total < 80) return 0;
      return span.length() - (total - 80);
    },
    [&ok, &p](ZuSpan<uint8_t> span) {
      ok = span.length() == p.length() &&
	!memcmp(span.data(), p.data(), p.length());
    });
  ZTCP_CHECK_RT(n == 80, "large multi-buffer consume failed");
  ZTCP_CHECK_RT(ok, "large multi-buffer payload mismatch");
  ZTCP_CHECK_RT(!rx, "stream should be empty after full consume");
}

void testPartial()
{
  ZuTestScopeRT(testPartial);

  Ztcp::RxStream rx;
  rx.push(buf("he"));
  rx.push(buf("lloworld"));

  ZTCP_CHECK_RT(consumeExact(rx, "hello"), "partial first consume failed");
  ZTCP_CHECK_RT(!!rx, "partial consume should leave unread data");
  ZTCP_CHECK_RT(consumeExact(rx, "world"), "partial second consume failed");
  ZTCP_CHECK_RT(!rx, "stream should be empty after second consume");
}

void testPauseAndEmpty()
{
  ZuTestScopeRT(testPauseAndEmpty);

  Ztcp::RxStream rx;
  rx.push(buf(""));
  ZTCP_CHECK_RT(!rx, "empty buffers must not be queued");

  rx.push(buf("abc"));
  int64_t n = rx.consume(
    [](ZuSpan<uint8_t>) -> int64_t { return 0; },
    [](ZuSpan<uint8_t>) { });
  ZTCP_CHECK_RT(!n, "paused consume should report no bytes");
  ZTCP_CHECK_RT(!!rx, "paused consume should leave queued data intact");
}

} // namespace

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testBoundary);
  ZuTestCall(testPartial);
  ZuTestCall(testPauseAndEmpty);
  return 0;
}
