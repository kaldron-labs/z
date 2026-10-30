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

#include <zlib/ZmRef.hh>
#include <zlib/ZiIOBuf.hh>
#include <zlib/ZiRxStream.hh>
#include <zlib/ZfixFrame.hh>

namespace Zfix {

// Rx-owned, reset for each physical connection. next_ returns 1 for one
// detached frame, 0 for incomplete input and -1 for a sticky framing error.
// Alloc supplies a queue-compatible pooled buffer. The caller retains a
// completed frame across admission pauses and must not call next_ until that
// frame is consumed. Reset only after abandoning the old stream on disconnect.
// Complete input uses RxStream::extract's detach/smaller-side-copy policy;
// fragmented input is gathered once directly into its final pooled buffer.
// An optional view borrows that output buffer and reuses the validated offsets.
class RxFramer {
public:
  explicit RxFramer(Fmt::Limits limits = {}) : m_framer{limits} { }

  const Fmt::Limits &limits() const { return m_framer.limits(); }
  const Fmt::Error &error_() const { return m_error; }

  void reset_() {
    m_prefix = nullptr; m_expected = 0; m_error = {}; m_framer.reset();
  }

  template <typename Stream, typename Alloc>
  int next_(Stream &stream, Alloc &&alloc, ZmRef<ZiIOBuf> &out,
      Fmt::FrameView *view = nullptr) {
    out = nullptr;
    if (view) *view = {};
    Fmt::FrameView parsed;
    if (m_error) return -1;
    if (!stream) return 0;
    bool validated = false;
    if (!m_prefix && !m_expected) {
      auto span = stream.span();
      auto r = Fmt::frame(ZuCSpan{span}, m_framer.limits());
      if (r.error) return fail(r.error);
      m_expected = r.expected;
      validated = r.state == Fmt::FrameState::Complete;
      if (validated) parsed = r.view;
    }
    if (!m_expected) {
      if (!m_prefix) {
	m_prefix = alloc();
	if (!m_prefix) return fail({Fmt::ErrorCode::Output});
      }
      // Only the leading 8/9 fields need gathering to learn the full length.
      // Scan each new header byte once; never walk a growing body repeatedly.
      Zi::RxFramePos pos;

      unsigned frameBytes = m_framer.limits().frameBytes;
      stream.scan([this, frameBytes](auto span) -> int64_t {
	unsigned n = span.length();
	for (unsigned i = 0; i < n; ++i) {
	  if (m_prefix->length >= frameBytes) {
	    m_error = {Fmt::ErrorCode::Size}; return -1;
	  }
	  if (!m_prefix->append(span.data() + i, 1)) {
	    m_error = {Fmt::ErrorCode::Output}; return -1;
	  }
	  auto r = m_framer.scan(ZuCSpan{m_prefix->cspan()});
	  if (r.error) { m_error = r.error; return -1; }
	  if (r.expected) { m_expected = r.expected; return i + 1; }
	}
	return 0;
      }, pos, m_prefix->length);
      if (m_error) return -1;
      if (!m_expected) return 0;
      m_prefix = nullptr;
    }
    if (stream.length() < m_expected) return 0;
    unsigned remaining = m_expected;
    auto n = stream.extract([&remaining](auto span) -> int64_t {
      if (span.length() >= remaining) return remaining;
      remaining -= span.length(); return 0;
    }, ZuFwd<Alloc>(alloc), out);
    if (n < 0) return fail({Fmt::ErrorCode::Output});
    if (!n) return 0;
    if (!validated) {
      auto r = Fmt::frame(ZuCSpan{out->cspan()}, m_framer.limits());
      if (r.error) { out = nullptr; return fail(r.error); }
      parsed = r.view;
    }
    if (view) {
      // extract may have copied a coalesced prefix into a different buffer.
      // Rebase validated offsets without repeating the checksum traversal.
      auto bytes = ZuCSpan{out->cspan()};
      auto rebase = [old = parsed.wire.data(), data = bytes.data()](ZuCSpan span) {
	return ZuCSpan{data + (span.data() - old), span.length()};
      };
      parsed.beginString = rebase(parsed.beginString);
      parsed.msgType = rebase(parsed.msgType); parsed.content = rebase(parsed.content);
      parsed.wire = bytes; *view = parsed;
    }
    m_expected = 0; m_framer.reset();
    return 1;
  }

private:
  int fail(Fmt::Error error) { m_error = error; return -1; }

  // Rx thread exclusive; immutable limits belong to the embedded framer.
  Fmt::Framer		m_framer;
  ZmRef<ZiIOBuf>	m_prefix;
  unsigned		m_expected = 0;
  Fmt::Error		m_error;
};

} // Zfix

#endif /* ZfixRxFramer_HH */
