//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Pooled FIX frame extraction from TCP/TLS receive streams

#ifndef ZfixRxFramer_HH
#define ZfixRxFramer_HH

#ifndef ZfixLib_HH
#include <zlib/ZfixLib.hh>
#endif

#include <string.h>

#include <zlib/ZmRef.hh>
#include <zlib/ZiIOBuf.hh>
#include <zlib/ZiRxStream.hh>
#include <zlib/ZfixFrame.hh>

namespace Zfix {

// Rx-owned framing retains original stream ranges. Header fragments consolidate
// in the original exclusive node; shared storage consolidates through the same
// final-frame allocator used by RxStream. No separate prefix/assembly pool.
class RxFramer {
public:
  explicit RxFramer(Fmt::Limits limits = {}) : m_framer{limits} { }

  const Fmt::Limits &limits() const { return m_framer.limits(); }
  const Fmt::Error &error_() const { return m_error; }

  void reset_() { m_expected = 0; m_gather = false; m_error = {}; m_framer.reset(); }

  template <typename Stream, typename Alloc>
  int next_(Stream &stream, Alloc &&alloc, Zi::RxRange &out,
      Fmt::FrameView *view = nullptr) {
    out = {};
    if (view) *view = {};
    if (m_error) return -1;
    if (!stream) return 0;
    Fmt::FrameView parsed;
    bool validated = false;
    while (!m_expected) {
      auto span = stream.span();
      auto r = m_framer.scan(ZuCSpan{span});
      if (r.error) return fail(r.error);
      m_expected = r.expected;
      if (r.state == Fmt::FrameState::Complete) {
	parsed = r.view; validated = true;
      }
      if (m_expected) {
	m_gather = stream.length() < m_expected;
	break;
      }
      if (stream.length() == span.length()) return 0;
      // Extend to the next envelope delimiter in one consolidation, bounded
      // by the field limit. Never gather body/trailing frames to inspect 8/9.
      uint64_t available = stream.length() - span.length();
      uint64_t bound = uint64_t(m_framer.limits().fieldBytes) + 3;
      if (available > bound) available = bound;
      unsigned extra = 0;
      bool delimiter = false;
      stream.each(span.length(), available, [&extra, &delimiter](auto part) -> int64_t {
	if (delimiter) return 0;
	auto end = static_cast<const uint8_t *>(memchr(part.data(), '\1', part.length()));
	unsigned n = end ? unsigned(end - part.data()) + 1 : part.length();
	extra += n; delimiter = end;
	return n;
      });
      if (!stream.gather(unsigned(span.length()) + extra, alloc))
	return fail({Fmt::ErrorCode::Output});
    }
    if (m_gather && !stream.gather(m_expected, alloc))
      return fail({Fmt::ErrorCode::Output});
    if (stream.length() < m_expected) return 0;
    unsigned remaining = m_expected;
    auto n = stream.extract([&remaining](auto span) -> int64_t {
      if (span.length() >= remaining) return remaining;
      remaining -= span.length(); return 0;
    }, alloc, out);
    if (n < 0) return fail({Fmt::ErrorCode::Output});
    if (!n) return 0;
    if (!validated) {
      auto r = Fmt::frame(ZuCSpan{out.cspan()}, m_framer.limits());
      if (r.error) { out = {}; return fail(r.error); }
      parsed = r.view;
    }
    if (view) *view = parsed;
    m_expected = 0; m_gather = false; m_framer.reset();
    return 1;
  }

private:
  int fail(Fmt::Error error) { m_error = error; return -1; }

  // Rx thread exclusive; immutable limits belong to the embedded framer.
  Fmt::Framer		m_framer;
  bool		m_gather = false;
  unsigned		m_expected = 0;
  Fmt::Error		m_error;
};

} // Zfix

#endif /* ZfixRxFramer_HH */
