//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Incremental gRPC envelopes and pooled protobuf output

#ifndef ZgrpcFrame_HH
#define ZgrpcFrame_HH

#ifndef ZgrpcLib_HH
#include <zlib/ZgrpcLib.hh>
#endif

#include <limits.h>
#include <zlib/Zgrpc.hh>
#include <zlib/ZiIOBuf.hh>
#include <zlib/Zpb.hh>

namespace Zgrpc {

// One flag byte followed by the protocol-mandated network-order uint32 length.
enum { PrefixSize = 5 };
ZtEnumNS(ZgrpcAPI, FrameError, int8_t, OK, Flag, Compression, Limit, Truncated, Encode);
ZtEnumNS(ZgrpcAPI, FrameResult, int8_t, More, Message, Failed);

struct Frame {
  ZuBSpan bytes;
  ZmRef<ZiIOBuf> backing;
};

// Verification precedes every typed construction. Borrowed declarations cover
// the entire native object, including views nested inside owning containers.
template <typename T, typename Facet = ZuFacet::PB, bool Borrowed = false>
Decoded<T> decodeFrame(const Frame &frame, Zpb::VerifyLimits limits = {})
{
  ZuAssert(!Zpb::Config<Facet>::SizePrefixed);
  if (!Zpb::verify<T, Facet>(frame.bytes, limits)) return {};
  auto handler = Zpb::handler<T, Facet>(frame.bytes);
  auto backing = Borrowed ? frame.backing : ZmRef<ZiIOBuf>{};
  if constexpr (ZuIsBase<T, ZmObject>{})
    return Message<T>{ZmRef<T>{handler.alloc()}, ZuMv(backing)};
  else
    return Message<T>{handler.ctor(), ZuMv(backing)};
}

class ZgrpcAPI FrameRx {
public:
  explicit FrameRx(unsigned limit = Default::MaxMessageBytes) : m_limit{limit} { }
  void reset(unsigned limit);

  // Consume at most one message and budget bytes; caller resumes the remaining
  // span in a bounded scheduler turn. A supplied backing must keep this slice
  // immutable until all returned Frame references are released. Without backing,
  // even a contiguous payload is copied into a consumer-owned pooled buffer.
  // exclusive permits consuming backing metadata for final fragmented assembly;
  // callers grant it only when no other reader retains any part of that buffer.
  FrameResult::T feed(ZuBSpan, ZmRef<ZiIOBuf>, unsigned &used, Frame &,
    unsigned budget = UINT_MAX, bool exclusive = false);
  bool end();
  bool failed() const { return m_error != FrameError::OK; }
  FrameError::T error() const { return m_error; }

private:
  FrameResult::T fail(FrameError::T error);

  unsigned		m_limit;
  uint8_t		m_prefix[PrefixSize];
  unsigned		m_prefixLen = 0;
  unsigned		m_length = 0;
  unsigned		m_received = 0;
  ZmRef<ZiIOBuf>		m_buf;
  FrameError::T		m_error = FrameError::OK;
};

ZgrpcAPI void framePrefix(uint8_t *, unsigned);

// Encode once into the writer's pooled buffer. The result retains detached
// skip/length and transport framing space; the caller adopts it into Writer.
// A zero-byte protobuf payload is valid. Failure is reported independently.
struct NoSign { void operator ()(ZuBSpan) const { } };

template <typename Facet = ZuFacet::PB, typename T, typename Sign = NoSign>
ZmRef<ZiIOBuf> encodeFrame(ZmRef<ZiIOBuf> buf, unsigned tailRoom,
  unsigned limit, const T &object, FrameError::T &error, Sign &&sign = {})
{
  ZuAssert(!Zpb::Config<Facet>::SizePrefixed);
  error = FrameError::OK;
  uint64_t reserve = uint64_t(buf->skip) + tailRoom + PrefixSize;
  if (reserve > INT_MAX) { error = FrameError::Limit; return nullptr; }
  uint64_t maxSize = reserve + limit;
  if (maxSize > INT_MAX) maxSize = INT_MAX;
  // Exclude the envelope from the backwards protobuf arena before encoding.
  // Detach leaves these five bytes available immediately ahead of the body.
  if (!buf->ensure(PrefixSize + tailRoom)) { error = FrameError::Limit; return nullptr; }
  buf->skip += PrefixSize;
  Zpb::IOBuilder builder{ZuMv(buf), unsigned(maxSize), tailRoom};
  auto bytes = Zpb::save<Facet>(builder, object);
  if (builder.failed()) {
    error = builder.error() == Zpb::Error::Limit ? FrameError::Limit : FrameError::Encode;
    return nullptr;
  }
  auto length = bytes.length();
  if (length > limit) { error = FrameError::Limit; return nullptr; }
  ZuFwd<Sign>(sign)(bytes);
  auto output = builder.buf();
  output->skip -= PrefixSize;
  output->length += PrefixSize;
  framePrefix(output->data(), length);
  return output;
}

} // Zgrpc

#endif /* ZgrpcFrame_HH */
