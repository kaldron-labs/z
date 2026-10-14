//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// HTTP server-sent event framing

#ifndef ZhttpSSE_HH
#define ZhttpSSE_HH

#include <zlib/ZhttpLib.hh>

#include <stdint.h>

#include <zlib/ZuBox.hh>
#include <zlib/ZuDerive.hh>
#include <zlib/ZuIntrin.hh>
#include <zlib/ZuSpan.hh>
#include <zlib/ZuTraits.hh>

#include <zlib/ZmRef.hh>

#include <zlib/ZtString.hh>

#include <zlib/ZiIOBuf.hh>

namespace Zhttp {

ZuDerive(SSELine, (ZtString<ZtStringHeapID<"Zhttp.SSE.Line">>));
ZuDerive(SSEID, (ZtString<ZtStringHeapID<"Zhttp.SSE.ID">>));
ZuDerive(SSEInputBuf, (ZiIOBufAlloc<
  ZiIOBuf_DefltSize, ZiIOBuf_DefltMaxSize, "Zhttp.SSE.Input">));

namespace Private {
ZhttpAPI int sseField(ZuCSpan field);
}

struct SSEEvent {
  // body owns its bytes; id is valid only during the event callback.
  ZmRef<ZiIOBuf> body;
  ZuCSpan id;
  int64_t retry = -1;
  ZuCSpan data() const { if (body) return body->cspan(); return {}; }
};

class SSEDecoder {
public:
  enum { Open, Closed };

  SSEDecoder() = default;
  SSEDecoder(unsigned maxLine, unsigned maxData) { reset(maxLine, maxData); }

  void reset(unsigned maxLine, unsigned maxData) {
    m_line.length_(0);
    reset_();
    m_maxLine = maxLine;
    m_maxData = maxData;
    m_state = Open;
  }

  int state() const { return m_state; }
  bool complete() const {
    return m_state == Open && !m_line.length() && !m_haveData;
  }
  void close() {
    m_state = Closed;
    m_line.length_(0);
    reset_();
  }

  template <typename L>
  bool feed(ZuCSpan input, L &&l) {
    if (m_state == Closed) return false;
    unsigned n = input.length();
    unsigned begin = 0;
    for (unsigned i = 0; i < n; ++i) {
      if (input[i] != '\n') continue;
      auto line = ZuCSpan{input}.offset(begin).trunc(i - begin);
      if (m_line.length()) {
	if (!append_(line)) return false;
	line_(m_line, l);
      } else {
	if (line.length() > m_maxLine) { close(); return false; }
	line_(line, l);
      }
      m_line.length_(0);
      if (m_state == Closed) return false;
      begin = i + 1;
    }
    if (begin == n) return true;
    return append_(ZuCSpan(input.data() + begin, n - begin));
  }

private:
  bool append_(ZuCSpan span) {
    unsigned lineLength = m_line.length();
    if (ZuUnlikely(lineLength > m_maxLine ||
	span.length() > m_maxLine - lineLength)) {
      close();
      return false;
    }
    m_line << span;
    return true;
  }

  void reset_() {
    m_data = nullptr;
    m_haveData = false;
    m_id.length_(0);
    m_retry = -1;
  }

  template <typename L>
  void line_(ZuCSpan line, L &l) {
    line.chomp([](char c) { return c == '\r'; });
    if (!line) {
      if (m_haveData) l(SSEEvent{ZuMv(m_data), m_id, m_retry});
      reset_();
      return;
    }
    if (line[0] == ':') return;
    unsigned lineLength = line.length();
    unsigned colon = 0;
    while (colon < lineLength && line[colon] != ':') ++colon;
    ZuCSpan field{line.data(), colon};
    ZuCSpan value;
    if (colon < lineLength) {
      value = ZuCSpan(line.data() + colon + 1, lineLength - colon - 1);
      if (value && value[0] == ' ') value.offset(1);
    }
    switch (Private::sseField(field)) {
      case 0: {
	unsigned dataLength = m_data ? m_data->length : 0;
	unsigned valueLength = value.length();
	unsigned separator = m_haveData;
	if (ZuUnlikely(separator > m_maxData ||
	    valueLength > m_maxData - separator ||
	    dataLength > m_maxData - separator - valueLength)) {
	  close();
	  return;
	}
	if (!m_data) m_data = new SSEInputBuf{};
	if ((separator && !m_data->append(ZuBSpan{"\n"})) ||
	    !m_data->append(value)) close();
	else m_haveData = true;
	break;
      }
      case 1: m_id = value; break;
      case 2: {
	int64_t retry = ZuBox<int64_t>{value};
	if (retry >= 0) m_retry = retry;
	break;
      }
    }
  }

  SSELine	m_line;
  ZmRef<ZiIOBuf>	m_data;
  bool		m_haveData = false;
  SSEID	m_id;
  unsigned	m_maxLine = 0;
  unsigned	m_maxData = 0;
  int64_t	m_retry = -1;
  int		m_state = Open;
};

template <typename S>
inline bool saveSSEHead(S &s, ZuCSpan id, int64_t retry)
{
  for (auto c : id)
    if (c == '\r' || c == '\n') return false;
  if (id) s << "id: " << id << '\n';
  if (retry >= 0) s << "retry: " << retry << '\n';
  return true;
}

template <typename S>
inline void saveSSEHead(S &s, uint64_t id, int64_t retry)
{
  s << "id: " << id << '\n';
  if (retry >= 0) s << "retry: " << retry << '\n';
}

template <typename S>
inline void saveSSEEnd(S &s) { s << '\n'; }

template <typename S>
inline bool saveSSE(S &s, ZuCSpan id, int64_t retry, ZuCSpan body)
{
  if (!saveSSEHead(s, id, retry)) return false;
  unsigned begin = 0;
  for (unsigned i = 0; i < body.length(); ++i) {
    if (body[i] != '\n') continue;
    s << "data: " << ZuCSpan{body}.offset(begin).trunc(i - begin) << '\n';
    begin = i + 1;
  }
  s << "data: " << ZuCSpan{body}.offset(begin) << '\n';
  saveSSEEnd(s);
  return true;
}

} // Zhttp

#endif /* ZhttpSSE_HH */
