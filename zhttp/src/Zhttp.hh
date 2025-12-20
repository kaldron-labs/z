//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// Z http library
// - HTTP 1.1
//   - optionally chunked body
//   - optional chunked trailers (rarely used feature)
// - caller is responsible for body decompression (if required)

#ifndef Zhttp_HH
#define Zhttp_HH

#include <zlib/ZhttpLib.hh>

#include <zlib/ZuMatcher.hh>
#include <zlib/ZuUnroll.hh>
#include <zlib/ZuStream.hh>

#include <zlib/ZtBuiltin.hh>

#include <zlib/Ztls.hh>

namespace Zhttp {

constexpr unsigned DefltMaxHdr = (64<<10);	// 64K default
constexpr unsigned DefltMaxBody = (1<<20);	// 1M default

namespace Method {
  ZtEnum(Method, int8_t,
    GET, POST, PUT, DELETE, PATCH, HEAD, OPTIONS, CONNECT, TRACE);
}

namespace TransferEncoding {
  ZtEnum(TransferEncoding, int8_t, compress, deflate, gzip);
}

// hard-coded linear white space (ASCII/UTF8)
ZuInline constexpr bool islws(uint8_t c) {
  return c == '\t' || c == ' ';
}

// hard-coded Boyer-Moore to find end of header "\r\n\r\n"
ZuInline int eoh(ZuBSpan data) {
  unsigned n = data.length();

  if (ZuUnlikely(n < 4)) return -1;
  n -= 4;

  int j;
  uint8_t c;

  for (unsigned o = 0; o <= n; ) {
    j = 3;
    while (j >= 0 && ((j & 1) ? '\n' : '\r') == (c = data[o + j])) j--;
    if (j < 0) return o + 4;
    j -= (c == '\r' ? 2 : c == '\n' ? 3 : -1);
    o += j < 1 ? 1 : j;
  }
  return -1;
}

// hard-coded Boyer-Moore to find end of line "\r\n[^\t ]" or "\r\n"
template <bool CanFold = true> // set to false to just match "\r\n"
ZuInline int eol(ZuBSpan data) {
  unsigned n = data.length();

  if (ZuUnlikely(n < 2)) return -1;
  n -= 2;

  uint8_t c;

  for (unsigned o = 0; o <= n; ) {
    if (ZuLikely(o < n)) {
      c = data[o + 2];
      if constexpr (CanFold)
	if (c == '\t' || c == ' ') { o += 3; continue; }
    }
    if (data[o + 1] != '\n') { ++o; continue; }
    if (data[o] == '\r') return o;
    o += 2;
  }
  return -1;
}

// find end of key ':'
// - uses memchr to leverage any available performance advantage
ZuInline int eok(ZuBSpan data) {
  auto p = static_cast<const uint8_t *>(memchr(&data[0], ':', data.length()));
  if (!p) return -1;
  return p - &data[0];
}

// skip leading linear white space to find beginning of header value
ZuInline int bov(ZuBSpan data) {
  for (unsigned o = 0, n = data.length(); o < n; ++o)
    if (!islws(data[o])) return o;
  return -1;
}

// remove trailing linear white space to find end of header value
ZuInline int eov(ZuBSpan data) {
  for (int o = data.length(); --o >= 0; )
    if (!islws(data[o])) return o + 1;
  return -1;
}

// split and iterate over HTTP value delimited by \s+,\s+
// - strips leading/trailing white space
// - single-pass, no back-tracking
// - optional alternate delimiter character (';' is also frequently used)
template <uint8_t Delim = ',', typename L>
inline void split(ZuBSpan data, L &&l) {
  unsigned count = 0;
  int begin, end;
  unsigned o = 0, n = data.length();

  for (;;) {
    // skip leading linear white space
    for (; o < n; ++o) if (!islws(data[o])) break;
    begin = o; end = -1;
    // find delimiter or end of string, remembering last non-white-space
    for (; o < n; ++o) {
      auto c = data[o];
      if (c == Delim) break;
      if (end < 0 ) {
	if (islws(c)) end = o;
      } else {
	if (!islws(c)) end = -1;
      }
    }
    if (end < 0) end = o;
    if (ZuLikely(end > begin || count || o < n))
      l(count++, ZuBSpan(&data[begin], unsigned(end - begin)));
    if (o >= n) break;
    // skip trailing linear white space
    while (++o < n) if (!islws(data[o])) break;
  }
}

// normalize key case to be consistent (mutates key in place)
inline void normalize(ZuSpan<uint8_t> key) {
  unsigned n = key.length();
  bool upper = true;
  int c; // intentionally int

  for (unsigned o = 0; o < n; o++) {
    c = key[o];
    if (c == '-') { upper = true; continue; }
    if (upper) {
      if (c >= 'a' && c <= 'z') key[o] = c + 'A' - 'a';
      upper = false;
    } else {
      if (c >= 'A' && c <= 'Z') key[o] = c + 'a' - 'A';
    }
  }
}

// Headers handles everything after the start line or a chunked trailer

// built-in keys that are always matched for every message
namespace Key {
  enum {
    TransferEncoding = -2,
    ContentLength = -1,
    N = 2
  };
}

template <typename Keys_>
struct Headers {
  using Keys = typename Keys_::template Unshift<ZuStringTL<
    "Transfer-Encoding",
    "Content-Length"
  >>;

  static constexpr auto matcher = ZuMatcher<Keys>();

  ZuCSpan	keys_[Keys::N];
  unsigned	offset = 0;
  bool		complete = false;

  ZuCSpan key(int i) const {
    i += Key::N;
    return (ZuUnlikely(i < 0 || i >= Keys::N)) ? ZuCSpan() : keys_[i];
  }

  // following a previous parse() the buffer's memory address
  // may have moved due to growth reallocation; if necessary
  // rebase all previously parsed headers
  void rebase(ptrdiff_t o) {
    if (!o || !offset) return;	// not moved or nothing parsed yet
    ZuUnroll::all<Keys::N>([this, o](auto I) {
      if (keys_[I]) keys_[I].rebase(o);
    });
  }

  // parse headers
  int parse(ZuSpan<uint8_t> data) {
    if (complete) return offset;
    unsigned o = offset;
    data.offset(o);
    for (;;) {
      if (data.length() < 2) return 0;
      if (data[0] == '\r' && data[1] == '\n') break;
      int n = eok(data);
      if (ZuUnlikely(n < 0)) return eol(data) < 0 ? 0 : -1; // unterminated key
      ZuSpan key(&data[0], unsigned(n));
      o += n + 1;
      data.offset(n + 1); // skip key and delimiter
      n = bov(data);
      if (ZuUnlikely(n < 0)) return 0; // unterminated value
      o += n;
      data.offset(n); // skip white space
      n = eol(data);
      if (ZuUnlikely(n < 0)) return 0; // unterminated value
      ZuSpan value(&data[0], unsigned(n));
      o += n + 2;
      data.offset(n + 2); // skip value and EOL
      n = eov(value);
      if (ZuUnlikely(n < 0)) return -1; // should never happen
      value.trunc(n);
      normalize(key);
      int j = matcher.match(key);
      if (j >= 0) keys_[j] = value;
      offset = o;
    }
    o += 2;
    data.offset(2);
    offset = o;
    complete = true;

    return o;
  }

  // reset for next message
  void reset() {
    ZuUnroll::all<Keys::N>([this](auto I) { keys_[I] = {}; });
    offset = 0;
    complete = false;
  }
};

// parse() returns:
// +ve - offset to body
// 0   - incomplete
// -1  - invalid / corrupt

template <typename Keys>
struct Request_ : public Headers<Keys> {
  Method::T	method = -1;	// Method
  ZuCSpan	path;		// path
  ZuCSpan	protocol;	// e.g. HTTP/1.1

  using Base = Headers<Keys>;
  using Base::offset;
  using Base::reset;

  // rebase spans
  void rebase(ptrdiff_t o) {
    if (!o || !offset) return;
    path.rebase(o);
    protocol.rebase(o);
    Headers<Keys>::rebase(o);
  }

  // parse request
  int parse(ZuSpan<uint8_t> data) {
    if (!offset) {
      unsigned n = data.length();
      if (ZuUnlikely(n < 27)) return 0; // shortest request length is 27
      int o = 0; // intentionally int
      for (o = 0; data[o] != ' '; )
	if (ZuUnlikely(++o > 7)) return 0; // unterminated method
      if (!o) return 0; // missing method
      method = Method::lookup({&data[0], unsigned(o)});
      unsigned b = ++o;
      while (data[o] != ' ')
	if (ZuUnlikely(++o >= n)) return 0; // unterminated path
      if (ZuUnlikely(b == o)) return -1; // missing path
      path = {&data[b], o - b};
      b = ++o;
      o = eol({&data[b], n - b});
      if (ZuUnlikely(o < 0)) return 0; // unterminated protocol
      protocol = {&data[b], unsigned(o)};
      offset = b + o + 2;
    }
    return Headers<Keys>::parse(data);
  }

  // reset for next message
  void reset() {
    method = -1;
    path = {};
    protocol = {};
    Base::reset();
  }
};

template <typename Keys>
struct Response_ : public Headers<Keys> {
  ZuCSpan	protocol;	// e.g. HTTP/1.1
  int		code = -1;	// e.g. 200
  ZuCSpan	reason;		// e.g. OK

  using Base = Headers<Keys>;
  using Base::offset;
  using Base::reset;

  // rebase spans
  void rebase(ptrdiff_t o) {
    if (!o || !offset) return;
    protocol.rebase(o);
    reason.rebase(o);
    Headers<Keys>::rebase(o);
  }

  // parse response line
  int parse(ZuSpan<uint8_t> data) { // returns offset to body, -1 if incomplete
    if (!offset) {
      unsigned n = data.length();
      if (ZuUnlikely(n < 19)) return 0; // shortest possible response is 19
      int o = 0; // intentionally int
      for (o = 0; data[o] != ' '; )
	if (ZuUnlikely(++o > 8)) return 0; // unterminated protocol
      if (!o) return 0; // missing protocol
      protocol = {&data[0], unsigned(o)};
      unsigned b = ++o;
      int c; // intentionally int
      while ((c = data[o]) != ' ') {
	if (c < '0' || c > '9') return -1; // not a number
	c -= '0';
	code = code < 0 ? c : (code * 10) + c;
	if (ZuUnlikely(++o > b + 3)) return 0; // unterminated code
      }
      if (ZuUnlikely(b == o)) return -1; // missing code
      b = ++o;
      o = eol({&data[b], n - b});
      if (ZuUnlikely(o < 0)) return 0; // unterminated reason
      reason = {&data[b], unsigned(o)};
      offset = b + o + 2;
    }
    return Headers<Keys>::parse(data);
  }

  // reset for next message
  void reset() {
    protocol = {};
    code = -1;
    reason = {};
    Base::reset();
  }
};

// header loader
template <template <typename> class Msg, typename Keys, unsigned Max>
struct Header : public Msg<Keys> {
  using Base = Msg<Keys>;
  using Base::complete;
  using Base::rebase;
  using Base::parse;
  using Base::reset;

  ZuSpan<uint8_t>	span;
  unsigned		max = Max;
  bool			valid = true;

  int process(ZiIOBuf *buf, ZuSpan<uint8_t> rcvd) {
    if (ZuUnlikely(complete)) return valid ? 0 : -1;
    if (buf->length + rcvd.length() > max) {
      complete = true, valid = false;
      return -1;
    }
    auto span_ = &span[0];
    buf->append(&rcvd[0], rcvd.length());
    span = buf->span();
    if (span_) rebase(&span[0] - span_);

    int o = span.length();
    int n = parse(span);
    if (n < 0) {
      complete = true, valid = false;
      return -1;
    }
    // defensive sanity check on parse() return, n > o wreaks havoc
    ZeAssert(n <= o, (o, n), "o=" << o << " n=" << n, n = o);
    unsigned consumed = rcvd.length();
    if (n) {
      consumed -= (o - n);
      span.trunc(n);
    }
    return consumed;
  }

  // reset for next message
  void reset() {
    span = {};
    max = Max;
    valid = true;
    Base::reset();
  }
};

// request header
template <
  typename Keys = ZuStringTL<>,
  unsigned Max = DefltMaxHdr>
using Request = Header<Request_, Keys, Max>;
// response header
template <
  typename Keys = ZuStringTL<>,
  unsigned Max = DefltMaxHdr>
using Response = Header<Response_, Keys, Max>;

ZuInline constexpr uint8_t hex(uint8_t c) {
  c |= 0x20;
  return 
    (ZuLikely(c >= '0' && c <= '9')) ?  c - '0' :
    (ZuLikely(c >= 'a' && c <= 'f')) ? (c - 'a') + 10 : 0xff;
}

struct ChunkHdr {
  int		offset = 0;
  int		length = 0;

  // parse chunk header
  // - returns offset if complete
  // - returns 0 if unterminated/incomplete
  // - returns -1 if invalid/corrupt
  inline int parse(ZuBSpan data) {
    unsigned o = 0, n = data.length();
    if (n > 10) n = 10;
    int len = 0;
    while (o < n) {
      auto c = data[o];
      if (c == '\r') {
	if (++o < n && data[o] == '\n') {
	  if (len < 0 || o < 2) return offset = -1; // invalid
	  length = len;
	  return offset = o + 1;
	}
	if (o >= n && n < 10) return 0; // unterminated / incomplete
	return offset = -1;
      }
      uint8_t i = hex(c);
      if (i == 0xff) return offset = -1;
      len = (len<<4) | i;
      o++;
    }
    return offset = -1;
  }

  bool complete() { return offset != 0; }
  bool valid() { return offset >= 0; }
  bool eob() { return !length; } // end of body
};

// body loader
ZuDerive(TrailerBuf,
  (ZtBuiltin<ZtArray<char, ZtArrayHeapID<"Zhttp.Trailer">>, 4>));
template <unsigned Max = DefltMaxBody>
struct Body {
  ZuSpan<uint8_t>	span;
  unsigned		max = Max;
  int			offset = -1;
  int			contentLength = -1;
  ZuArray<uint8_t, 12>	chunkBuf;
  ChunkHdr		chunkHdr;
  TrailerBuf		chunkTrlr;
  unsigned		chunkTotal = 0;
  TransferEncoding::T	xferEncoding = -1;
  bool			chunked = false;
  bool			valid = true;
  bool			complete = false;

  // access and validate the Transfer-Encoding and Content-Length headers
  // - attempts to consume any body data lingering in the buffer
  // - returns number of additional bytes consumed
  // - returns -1 if header is invalid
  template <typename Header>
  int init(ZiIOBuf *buf, const Header &header, unsigned max_ = Max) {
    max = max_;
    offset = header.span.length();
    if (auto s = header.key(Key::TransferEncoding))
      split(s, [this](unsigned i, ZuBSpan token) {
	// chunked must come last, anything else must be first
	if (chunked)
	  valid = false;
	else if (token == "chunked")
	  chunked = true;
	else if (i)
	  valid = false;
	else
	  xferEncoding = TransferEncoding::lookup(token);
      });
    if (!valid) {
      buf->length = offset;
      return -1;
    }
    if (chunked) {
      buf->length = offset;
      return 0;
    }
    // from here body is valid and not chunked
    if (auto s = header.key(Key::ContentLength))
      contentLength = ZuBox<unsigned>{s};
    else
      contentLength = 0;
    if (contentLength < 0 || contentLength > max) {
      buf->length = offset;
      valid = false;
      return -1;
    }
    if (!contentLength) {
      buf->length = offset;
      complete = true;
      return 0;
    }
    unsigned o = offset, n = buf->length;
    // defensive sanity check on buffer length, n < o wreaks havoc
    ZeAssert(n >= o, (o, n), "o=" << o << " n=" << n, o = n);
    n -= o;
    if (n > contentLength) n = contentLength;
    buf->length = offset + n;
    span = buf->span(offset);
    if (n == contentLength)
      complete = true;
    else
      buf->ensure(offset + contentLength);
    return n;
  }

  // once complete, chunkTrlr can be parsed by Header
  int process(ZiIOBuf *buf, ZuSpan<uint8_t> rcvd) {
    if (ZuUnlikely(complete)) return valid ? 0 : -1;
    if (!chunked) {
      if (ZuUnlikely(contentLength < 0)) {
	valid = false, complete = true;
	return -1;
      }
      unsigned remaining = contentLength - span.length();
      unsigned n = rcvd.length();
      if (n > remaining) n = remaining;
      buf->append(&rcvd[0], n);
      span = buf->span(offset);
      if (n == remaining) complete = true;
      return n;
    }
    unsigned consumed = 0;
    while (rcvd) {
      // reading chunk header?
      if (!chunkHdr.complete()) {
	unsigned remaining = chunkBuf.size() - chunkBuf.length();
	unsigned n = rcvd.length();
	if (n > remaining) n = remaining;
	chunkBuf << ZuCSpan(&rcvd[0], n);
	if (chunkTotal) { // not the first chunk, chunkBuf starts with "\r\n"
	  if (chunkBuf.length() < 5) return n; // incomplete
	  if (chunkBuf[0] != '\r' || chunkBuf[1] != '\n') {
	    valid = false, complete = true;
	    return -1;
	  }
	  chunkBuf.shift(2);
	}
	chunkHdr.parse(chunkBuf);
	if (!chunkHdr.complete()) return n;
	// chunk header complete - update state
	if (!chunkHdr.valid()) {
	  valid = false, complete = true;
	  return -1;
	}
	chunkTotal += chunkHdr.length;
	n -= (chunkBuf.length() - chunkHdr.offset);
	chunkBuf.length(chunkHdr.offset);
	consumed += n;
	rcvd.offset(n);
	if (!rcvd) break;
      }
      // last chunk?
      if (chunkHdr.eob()) {
	// optimize for fast path - no trailer payload
	if (ZuLikely(!chunkTrlr &&
	    rcvd.length() >= 2 &&
	    rcvd[0] == '\r' && rcvd[1] == '\n')) {
	  chunkTrlr << "\r\n\r\n";
	  consumed += 2;
	  complete = true;
	  break;
	}
	if (ZuLikely(chunkTrlr.length() == 1 &&
	    chunkTrlr[0] == '\r' && rcvd[0] == '\n')) {
	  chunkTrlr << "\n\r\n";
	  ++consumed;
	  complete = true;
	  break;
	}
	// slow path - append received data to chunk trailer
	unsigned o = chunkTrlr.length();
	chunkTrlr << rcvd;
	int n = eoh(chunkTrlr);
	if (ZuUnlikely(n < 0)) { // unterminated trailer
	  consumed += rcvd.length();
	  break;
	}
	chunkTrlr.length(n);
	// truncate threshold is wastage > min(1K, o)
	unsigned tt = n + (1<<10);
	if (tt > (n<<1)) tt = n<<1;
	if (chunkTrlr.size() > tt) chunkTrlr.truncate();
	// complete
	n -= o;
	consumed += n;
	rcvd.offset(n);
	complete = true;
	break;
      }
      // append to body
      if (span.length() < chunkTotal) {
	unsigned n = chunkTotal - span.length();
	if (n > rcvd.length()) n = rcvd.length();
	buf->append(&rcvd[0], n);
	span = buf->span(offset);
	consumed += n;
	rcvd.offset(n);
	if (span.length() >= chunkTotal) { // onto the next chunk
	  chunkHdr = {};
	  chunkBuf = {};
	}
      }
    }
    return consumed;
  }

  // reset for next message
  void reset() {
    span = {};
    max = Max;
    chunkBuf = {};
    chunkHdr = {};
    chunkTrlr.clear();
    chunkTotal = 0;
    xferEncoding = -1;
    chunked = false;
    valid = true;
    complete = false;
  }
};

// HTTP message builder

template <bool HasBody> struct Builder_Body { };
template <> struct Builder_Body<true> {
  int		contentLen = -1,	// offset of content-length
		body = -1;		// offset of body
};

template <
  typename Header_ = Response<>,
  typename Body_ = Body<>>
struct RxMsg {
  using Header = Header_;
  using Body = Body_;

  ZmRef<ZiIOBuf>	buf;
  Header		header;
  Body			body;

  RxMsg(ZmRef<ZiIOBuf> buf_) : buf{ZuMv(buf_)} { }

  // bool rcvd()
  // - returns false to disconnect
  template <typename Rcvd>
  int process(ZuSpan<uint8_t> data, Rcvd rcvd) {
    if (ZuUnlikely(body.complete)) return -1; // should not happen

    unsigned consumed = 0;
    int o;

    if (!header.complete) {
      o = header.process(buf, data);
      if (o < 0) { ZeLOG(Error, "invalid HTTP response"); return -1; }
      if (!header.complete) return o;
      if (o) {
	consumed += o;
	data.offset(o);
      }
      auto n = body.init(buf, header);
      if (n < 0) {
	ZeLOG(Error, "invalid HTTP Transfer-Encoding / Content-Length");
	return -1;
      }
      if (n) {
	consumed += n;
	data.offset(n);
      }
      if (!data) return consumed;
    }
    if (!body.complete) {
      o = body.process(buf, data);
      if (o < 0) { ZeLOG(Error, "invalid HTTP body"); return -1; }
      if (o) {
	consumed += o;
	data.offset(o);
      }
    }
    if (!body.complete) return consumed;
    if (!body.valid) {
      // invalid body should have been caught by body.process() returning -1
      ZeLOG(Error, "Zhttp internal error");
      return -1;
    }

    bool disconnect = !rcvd();

    if (ZuUnlikely(disconnect)) return -1;

    return consumed;
  }

  // reset() to reuse the buffer for a subsequent message
  void reset() {
    header.reset();
    body.reset();
  }
};

template <
  bool HasBody = false,			// has a body
  typename Context = ZuEmpty>		// additional context for callbacks
struct Builder : public Builder_Body<HasBody> {
  ZmRef<ZiIOBuf>	buf;
  Context		context;

  Builder(ZmRef<ZiIOBuf> buf_) : buf{ZuMv(buf_)} { }
  template <typename ...Args,
    decltype(Context(ZuDeclVal<Args &&>()...), int()) = 0>
  Builder(ZmRef<ZiIOBuf> buf_, Args &&...args) :
    buf{ZuMv(buf_)}, context(ZuFwd<Args>(args)...) { }

  template <typename L, typename = void>
  struct IsCallable : public ZuFalse { };
  template <typename L>
  struct IsCallable<L, decltype(ZuDeclVal<L &>()(ZuDeclVal<Builder &>()))> :
    public ZuTrue { };

  // request with query
  template <typename Path, typename Query, typename Host, typename Headers>
  void request(
    this auto &self,
    unsigned method, Path &&path, Query &&query, Host &&host,
    Headers &&headers)
  {
    auto &buf = *(self.buf);
    // method
    buf << Method::name(method) << ' ';
    // path
    if constexpr (!IsCallable<Path>{})
      buf << ZuFwd<Path>(path);
    else
      ZuFwd<Path>(path)(ZuFwdLike<decltype(self)>(self));
    // query (may be prefixed with trailing path components)
    if constexpr (!IsCallable<Query>{})
      buf << ZuFwd<Query>(query);
    else
      ZuFwd<Query>(query)(ZuFwdLike<decltype(self)>(self));
    // host
    buf << " HTTP/1.1\r\nHost: ";
    if constexpr (!IsCallable<Host>{})
      buf << ZuFwd<Host>(host);
    else
      ZuFwd<Host>(host)(ZuFwdLike<decltype(self)>(self));
    buf << "\r\n";
    // canonical headers
    if constexpr (HasBody) {
      buf << "Content-Length:           \r\n"; // placeholder empty value
      self.contentLen = buf.length - 12;
    }
    // custom headers
    if constexpr (!IsCallable<Headers>{})
      buf << ZuFwd<Headers>(headers);
    else
      ZuFwd<Headers>(headers)(ZuFwdLike<decltype(self)>(self));
    buf << "\r\n";
    if constexpr (HasBody) self.body = buf.length;
  }

  // request without query
  template <typename Path, typename Host, typename Headers>
  void request(
    this auto &self,
    unsigned method, Path &&path, Host &&host, Headers &&headers)
  {
    auto &buf = *(self.buf);
    // method
    buf << Method::name(method) << ' ';
    // path
    if constexpr (!IsCallable<Path>{})
      buf << ZuFwd<Path>(path);
    else
      ZuFwd<Path>(path)(ZuFwdLike<decltype(self)>(self));
    // host
    buf << " HTTP/1.1\r\nHost: ";
    if constexpr (!IsCallable<Host>{})
      buf << ZuFwd<Host>(host);
    else
      ZuFwd<Host>(host)(ZuFwdLike<decltype(self)>(self));
    buf << "\r\n";
    // canonical headers
    if constexpr (HasBody) {
      buf << "Content-Length:           \r\n";
      self.contentLen = buf.length - 12;
    }
    // custom headers
    if constexpr (!IsCallable<Headers>{})
      buf << ZuFwd<Headers>(headers);
    else
      ZuFwd<Headers>(headers)(ZuFwdLike<decltype(self)>(self));
    buf << "\r\n";
    if constexpr (HasBody) self.body = buf.length;
  }

  // response
  template <typename Reason, typename Headers>
  void response(
    this auto &self, unsigned code, Reason &&reason, Headers &&headers)
  {
    auto &buf = *(self.buf);
    // code
    buf << "HTTP/1.1 " << ZuBox<unsigned>{code}.fmt<ZuFmt::Right<3>>() << ' ';
    // reason
    if constexpr (!IsCallable<Reason>{})
      buf << ZuFwd<Reason>(reason);
    else
      ZuFwd<Reason>(reason)(ZuFwdLike<decltype(self)>(self));
    buf << "\r\n";
    // canonical headers
    if constexpr (HasBody) {
      buf << "Content-Length:           \r\n";
      self.contentLen = buf.length - 12;
    }
    // custom headers
    self.contentLen = buf.length - 12;
    if constexpr (!IsCallable<Headers>{})
      buf << ZuFwd<Headers>(headers);
    else
      ZuFwd<Headers>(headers)(ZuFwdLike<decltype(self)>(self));
    buf << "\r\n";
    if constexpr (HasBody) self.body = buf.length;
  }

  ZmRef<ZiIOBuf> finish(this auto &self) {
    auto &buf = *(self.buf);
    // finish / re-write
    if constexpr (HasBody) {
      ZuStream s{buf.data() + self.contentLen, 10};
      s << ZuBox<uint32_t>{buf.length - self.body};
    }
    return ZuMv(self.buf);
  }
};

} // Zhttp

#endif /* Zhttp_HH */
