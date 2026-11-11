//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>
#include <zlib/ZiMultiplex.hh>
#include <zlib/ZgrpcFrame.hh>

namespace Zgrpc {

ZtEnumImplNS(FrameError);
ZtEnumImplNS(FrameResult);

// Reuse Zi's tuned packet size and heap, allowing assembly up to the codec's
// implementation ceiling. The per-call limit is checked before allocation.
template <Zi::IOBufType::T Type>
static ZmRef<ZiIOBuf> allocMessage(unsigned length)
{
  static ZiMultiplex mx;
  static auto allocator = mx.allocator<Type, ZiIOBuf,
    ZiIOBuf_DefltSize, ZiIOBuf_DefltAlign, ZuStringT<"Zgrpc.Message">>();
  return allocator->allocBuf(nullptr, length);
}

void FrameRx::reset(unsigned limit)
{
  m_limit = limit;
  m_prefixLen = m_length = m_received = 0;
  m_buf = nullptr;
  m_error = FrameError::OK;
}

FrameResult::T FrameRx::fail(FrameError::T error)
{
  m_error = error;
  m_buf = nullptr;
  return FrameResult::Failed;
}

FrameResult::T FrameRx::feed(ZuBSpan input, ZmRef<ZiIOBuf> backing,
  unsigned &used, Frame &out, unsigned budget, bool exclusive)
{
  used = 0;
  out = {};
  if (failed()) return FrameResult::Failed;
  unsigned avail = input.length();
  if (avail > budget) avail = budget;
  if (m_prefixLen < PrefixSize) {
    unsigned n = PrefixSize - m_prefixLen;
    if (n > avail) n = avail;
    if (n) memcpy(m_prefix + m_prefixLen, input.data(), n);
    m_prefixLen += n;
    used = n;
    if (m_prefixLen < PrefixSize) return FrameResult::More;
    switch (m_prefix[0]) {
      case 0: break;
      case 1: return fail(FrameError::Compression);
      default: return fail(FrameError::Flag);
    }
    m_length = (unsigned(m_prefix[1])<<24) | (unsigned(m_prefix[2])<<16) |
      (unsigned(m_prefix[3])<<8) | m_prefix[4];
    if (m_length > m_limit || m_length > INT_MAX) return fail(FrameError::Limit);
  }
  if (!m_length) {
    m_prefixLen = 0;
    return FrameResult::Message;
  }
  unsigned n = m_length - m_received;
  if (n > avail - used) n = avail - used;
  if (!m_received && n == m_length && backing) {
    out = {{input.data() + used, n}, ZuMv(backing)};
    used += n;
    m_prefixLen = m_length = 0;
    return FrameResult::Message;
  }
  if (n) {
    if (!m_buf && backing && exclusive && used + n == input.length()) {
      unsigned offset = unsigned(input.data() + used - backing->data());
      backing->skip += offset;
      backing->length = n;
      m_buf = ZuMv(backing);
      if (!m_buf->ensure(m_length)) return fail(FrameError::Limit);
    } else {
      if (!m_buf) {
	switch (m_bufType) {
	  case Zi::IOBufType::RxTCP: m_buf = allocMessage<Zi::IOBufType::RxTCP>(m_length); break;
	  case Zi::IOBufType::RxTLS: m_buf = allocMessage<Zi::IOBufType::RxTLS>(m_length); break;
	  default: m_buf = allocMessage<Zi::IOBufType::App>(m_length); break;
	}
      }
      memcpy(m_buf->data() + m_received, input.data() + used, n);
    }
    m_received += n;
    used += n;
  }
  if (m_received != m_length) return FrameResult::More;
  m_buf->length = m_length;
  out = {m_buf->cspan(), ZuMv(m_buf)};
  m_prefixLen = m_length = m_received = 0;
  return FrameResult::Message;
}

bool FrameRx::end()
{
  if (failed()) return false;
  if (m_prefixLen) { fail(FrameError::Truncated); return false; }
  return true;
}

void framePrefix(uint8_t *p, unsigned n)
{
  p[0] = 0;
  p[1] = n>>24; p[2] = n>>16; p[3] = n>>8; p[4] = n;
}

} // Zgrpc
