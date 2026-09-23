//  -*- mode:c++; indent-tabs-mode:t; tab-width=8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Direct D-Bus message construction into a pooled I/O buffer

#ifndef ZdbusMessage_HH
#define ZdbusMessage_HH

#ifndef ZdbusLib_HH
#include <zlib/ZdbusLib.hh>
#endif

#include <string.h>

#include <zlib/ZuIntrin.hh>
#include <zlib/ZmAssert.hh>
#include <zlib/ZiIOBuf.hh>
#include <zlib/ZfDBUS.hh>
#include <zlib/ZdbusEnvelope.hh>

namespace Zdbus_ {

struct HeadSpec {
  ZuCSpan	path;
  ZuCSpan	interface;
  ZuCSpan	member;
  ZuCSpan	errorName;
  ZuCSpan	destination;
  uint32_t	replySerial = 0;
  uint32_t	serial = 0;
  uint8_t	type = 0;
  uint8_t	flags = 0;
  uint8_t	order = ZfDBUS::Order::Little;
};

struct HeadPlan {
  unsigned	fieldsLength = 0;
  unsigned	bodyOffset = 0;
};

namespace BuildError {
  enum { OK, Header, Body, Resource };
}

struct BuildResult {
  ZmRef<ZiIOBuf>	buf;
  ZfDBUS::Result	codec;
  int			error = BuildError::OK;

  explicit operator bool() const { return error == BuildError::OK; }
};

// The final buffer has already been reserved. Neither operator allocates.
struct Sink {
  ZiIOBuf	*buf;

  void patch32(unsigned offset, uint32_t value, uint8_t order) {
    bool swap = order == ZfDBUS::Order::Big;
    if constexpr (Zu_BIGENDIAN) swap = !swap;
    uint32_t encoded = swap ? ZuIntrin::bswap(value) : value;
    memcpy(buf->data() + offset, &encoded, sizeof(encoded));
  }

  Sink &operator <<(char c) {
    *buf->end() = uint8_t(c);
    ++buf->length;
    return *this;
  }
  Sink &operator <<(ZuBSpan bytes) {
    unsigned n = bytes.length();
    if (n) memcpy(buf->end(), bytes.begin(), n);
    buf->length += n;
    return *this;
  }
};

ZdbusExtern bool planHead(HeadPlan &, const HeadSpec &, ZuCSpan signature);
ZdbusExtern void writeHead(Sink &, const HeadPlan &, const HeadSpec &,
  ZuCSpan signature, unsigned bodyLength);

template <typename Body, typename Facet = ZuFacet::DBUS>
BuildResult message(const HeadSpec &head, const Body &body,
  unsigned limit = Wire::MaxMessageSize)
{
  BuildResult result;
  static constexpr auto sig = ZfDBUS::signatureConst<Body, Facet>();
  ZuCSpan signature{sig.bytes, sig.length};
  HeadPlan plan;
  if (!planHead(plan, head, signature)) {
    result.error = BuildError::Header;
    return result;
  }
  result.codec = ZfDBUS::measure<Facet>(body,
    {.offset = plan.bodyOffset, .order = head.order});
  if (!result.codec) {
    result.error = BuildError::Body;
    return result;
  }
  unsigned total = result.codec.offset;
  if (total > limit || total > Wire::MaxMessageSize) {
    result.error = BuildError::Resource;
    return result;
  }
  using Buf = ZiIOBufAlloc<ZiIOBuf_DefltSize,
    Wire::MaxMessageSize, "Zdbus.Message">;
  result.buf = new Buf{};
  if (!result.buf->ensure(total)) {
    result.buf = {};
    result.error = BuildError::Resource;
    return result;
  }
  Sink sink{result.buf.ptr()};
  writeHead(sink, plan, head, signature, total - plan.bodyOffset);
  ZmAssert_(result.buf->length == plan.bodyOffset);
  ZfDBUS::saveMeasured<Facet>(sink, body,
    {.offset = plan.bodyOffset, .order = head.order}, result.codec);
  ZmAssert_(result.buf->length == total);
  return result;
}

} // Zdbus_

#endif /* ZdbusMessage_HH */
