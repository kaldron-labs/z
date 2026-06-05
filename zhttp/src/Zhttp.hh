//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
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

#include <zlib/ZtBuiltin.hh>
#include <zlib/ZtLocalArray.hh>

#include <zlib/Ztls.hh>

namespace Zhttp {

constexpr unsigned DefltMaxHdr = (64<<10);	// 64K default
constexpr unsigned DefltMaxBody = (1<<20);	// 1M default

using HdrData = ZtArray<uint8_t, ZtArrayHeapID<"Zhttp.HdrData">>;
using BodyData = ZtArray<uint8_t, ZtArrayHeapID<"Zhttp.BodyData">>;
using TrailerData = ZtArray<uint8_t, ZtArrayHeapID<"Zhttp.TrailerData">>;

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
// - ZuMatcher needs consistent casing for efficient key matching
inline void normalize(ZuSpan<uint8_t> key) {
  unsigned n = key.length();
  int c; // intentionally int

  for (unsigned o = 0; o < n; o++) {
    c = key[o];
    if (c >= 'A' && c <= 'Z') key[o] = c + 'a' - 'A';
  }
}

// Headers handles everything after the start line or a chunked trailer

// built-in keys that are always matched for every message
namespace Key {
  enum {
    TransferEncoding = 0,
    ContentLength,
    N
  };
}

template <typename Keys_, typename KVs_>
struct Headers {
  using Keys = typename Keys_::template Unshift<ZuStringTL<
    "transfer-encoding",
    "content-length"
  >>;
  using KVs = KVs_;

  static constexpr auto keyMatcher = ZuMatcher<Keys>();

  static int kvMatch(ZuBSpan kv) {
    if constexpr (KVs::N) {
      static constexpr auto matcher = ZuMatcher<KVs>();
      return matcher.match(kv);
    } else
      return -1;
  }

  unsigned		offset = 0;
  bool			complete = false;
  int			contentLength = -1;
  TransferEncoding::T	xferEncoding = -1;
  bool			chunked = false;

  // parse headers
  template <typename Msg, typename KeyFn, typename KVFn>
  int parse(ZuSpan<uint8_t> data, Msg &&msg, KeyFn &&keyFn, KVFn &&kvFn) {
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
      auto scratch = ZtLocalArray(HdrData, key.length() + 2 + value.length());
      scratch << key << uint8_t(':') << uint8_t(' ') << value;
      ZuSpan<uint8_t> key_{scratch.data(), key.length()};
      normalize(key_);
      ZuSpan kv{scratch.data(), scratch.length()}; // "key: value"
      int j = kvMatch(kv);
      if (j >= 0)
	kvFn(msg, j);
      else {
	j = keyMatcher.match(key_);
	if (j >= 0) {
	  switch (j) {
	    case Key::TransferEncoding: {
	      bool valid = true;
	      split(value, [this, &valid](unsigned i, ZuBSpan token) {
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
	      if (!valid) return -1;
	    } break;
	    case Key::ContentLength:
	      contentLength =
		ZuBox<unsigned>{reinterpret_cast<const char *>(&value[0]),
		  unsigned(value.length())};
	      break;
	    default:
	      keyFn(msg, j - Key::N, value);
	      break;
	  }
	}
      }
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
    offset = 0;
    complete = false;
    contentLength = -1;
    xferEncoding = -1;
    chunked = false;
  }
};

// parse() returns:
// +ve - offset to body
// 0   - incomplete
// -1  - invalid / corrupt

template <typename Keys, typename KVs>
struct Request_ : public Headers<Keys, KVs> {
  Method::T	method = -1;	// Method

  using Base = Headers<Keys, KVs>;
  using Base::offset;
  using Base::reset;

  // parse request
  template <typename Msg, typename Operation, typename Key, typename KV>
  int parse(
    ZuSpan<uint8_t> data,
    Msg &&msg, Operation &&operation, Key &&key, KV &&kv)
  {
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
      ZuCSpan path{&data[b], o - b};
      b = ++o;
      o = eol({&data[b], n - b});
      if (ZuUnlikely(o < 0)) return 0; // unterminated protocol
      // ZuCSpan protocol{&data[b], unsigned(o)};
      ZuFwd<Operation>(operation)(msg, method, path);
      offset = b + o + 2;
    }
    return Headers<Keys, KVs>::parse(data, msg, ZuFwd<Key>(key), ZuFwd<KV>(kv));
  }

  // reset for next message
  void reset() {
    method = -1;
    Base::reset();
  }
};

template <typename Keys, typename KVs>
struct Response_ : public Headers<Keys, KVs> {
  int		status = -1;	// e.g. 200

  using Base = Headers<Keys, KVs>;
  using Base::offset;
  using Base::reset;

  // parse response
  template <typename Msg, typename Status, typename Key, typename KV>
  int parse(ZuSpan<uint8_t> data, Msg &&msg, Status &&status_, Key &&key, KV &&kv) {
    if (!offset) {
      unsigned n = data.length();
      if (ZuUnlikely(n < 19)) return 0; // shortest possible response is 19
      int o = 0; // intentionally int
      for (o = 0; data[o] != ' '; )
	if (ZuUnlikely(++o > 8)) return 0; // unterminated protocol
      if (!o) return 0; // missing protocol
      // ZuCSpan protocol{&data[0], unsigned(o)};
      unsigned b = ++o;
      int c; // intentionally int
      while ((c = data[o]) != ' ') {
	if (c < '0' || c > '9') return -1; // not a number
	c -= '0';
	status = status < 0 ? c : (status * 10) + c;
	if (ZuUnlikely(++o > b + 3)) return 0; // unterminated status
      }
      if (ZuUnlikely(b == o)) return -1; // missing status
      b = ++o;
      o = eol({&data[b], n - b});
      if (ZuUnlikely(o < 0)) return 0; // unterminated reason
      // ZuCSpan reason{&data[b], unsigned(o)};
      ZuFwd<Status>(status_)(msg, status);
      offset = b + o + 2;
    }
    return Headers<Keys, KVs>::parse(data, msg, ZuFwd<Key>(key), ZuFwd<KV>(kv));
  }

  // reset for next message
  void reset() {
    status = -1;
    Base::reset();
  }
};

// header state
template <template <typename, typename> class Msg, typename Keys, typename KVs, unsigned Max>
struct Header : public Msg<Keys, KVs> {
  using Base = Msg<Keys, KVs>;
  using Base::reset;

  unsigned		max = Max;
  bool			valid = true;

  // reset for next message
  void reset() {
    max = Max;
    valid = true;
    Base::reset();
  }
};

// request header
template <
  typename Keys = ZuStringTL<>,
  typename KVs = ZuStringTL<>,
  unsigned Max = DefltMaxHdr>
using Request = Header<Request_, Keys, KVs, Max>;
// response header
template <
  typename Keys = ZuStringTL<>,
  typename KVs = ZuStringTL<>,
  unsigned Max = DefltMaxHdr>
using Response = Header<Response_, Keys, KVs, Max>;

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
  (ZtBuiltin<ZtArray<uint8_t, ZtArrayHeapID<"Zhttp.Trailer">>, 4>));
template <unsigned Max = DefltMaxBody>
struct Body {
  ZuSpan<uint8_t>	span;
  ZuSpan<uint8_t>	chunkTrlr;
  unsigned		max = Max;
  int			contentLength = -1;
  unsigned		received = 0;
  ZuArray<uint8_t, 12>	chunkBuf;
  ChunkHdr		chunkHdr;
  unsigned		chunkTotal = 0;
  unsigned		chunkRemaining = 0;
  unsigned		trlrLength = 0;
  unsigned		trlrMatch = 0;
  TransferEncoding::T	xferEncoding = -1;
  bool			chunked = false;
  bool			chunkNeedCRLF = false;
  bool			valid = true;
  bool			complete = false;

  // access and validate the transfer-encoding and content-length headers
  // - returns -1 if header is invalid
  template <typename Header>
  int init(const Header &header, unsigned max_ = Max) {
    max = max_;
    xferEncoding = header.xferEncoding;
    chunked = header.chunked;
    if (chunked) return 0;
    contentLength = header.contentLength < 0 ? 0 : header.contentLength;
    if (contentLength < 0 || contentLength > max) {
      valid = false;
      return -1;
    }
    if (!contentLength) {
      complete = true;
      return 0;
    }
    return 0;
  }

  static unsigned trlrMatch_(unsigned match, uint8_t c) {
    switch (match) {
      case 0: return c == '\r' ? 1 : 0;
      case 1: return c == '\n' ? 2 : c == '\r' ? 1 : 0;
      case 2: return c == '\r' ? 3 : 0;
      case 3: return c == '\n' ? 4 : c == '\r' ? 1 : 0;
      default: return 4;
    }
  }

  template <typename Parser, typename Rcvd>
  int process(Parser &parser, Rcvd &&rcvd) {
    if (ZuUnlikely(complete)) return valid ? 0 : -1;
    auto &stream = parser.stream;
    unsigned consumed = 0;
    if (!chunked) {
      if (ZuUnlikely(contentLength < 0)) {
	valid = false, complete = true;
	return -1;
      }
      while (received < unsigned(contentLength)) {
	auto data = stream.span();
	if (!data) return consumed;
	unsigned n = unsigned(contentLength) - received;
	if (n > data.length()) n = data.length();
	bool final = received + n >= unsigned(contentLength);
	auto body = ZtLocalArray(BodyData, n);
	body << ZuBSpan{data.data(), n};
	received += n;
	stream.advance(n);
	consumed += n;
	span = body.span();
	complete = final;
	bool ok = rcvd(parser);
	span = {};
	if (ZuUnlikely(!ok)) return -1;
	if (complete) break;
      }
      return consumed;
    }

    for (;;) {
      if (chunkNeedCRLF) {
	while (chunkBuf.length() < 2) {
	  auto data = stream.span();
	  if (!data) return consumed;
	  chunkBuf << data[0];
	  stream.advance(1);
	  ++consumed;
	}
	if (chunkBuf[0] != '\r' || chunkBuf[1] != '\n') {
	  valid = false, complete = true;
	  return -1;
	}
	chunkBuf = {};
	chunkNeedCRLF = false;
      }

      // reading chunk header?
      if (!chunkHdr.complete()) {
	auto data = stream.span();
	if (!data) return consumed;
	if (chunkBuf.length() >= chunkBuf.size()) {
	  valid = false, complete = true;
	  return -1;
	}
	chunkBuf << data[0];
	stream.advance(1);
	++consumed;
	if (chunkBuf.length() < 2) continue;
	if (chunkBuf[chunkBuf.length() - 2] != '\r' ||
	    chunkBuf[chunkBuf.length() - 1] != '\n') {
	  if (chunkBuf.length() < 10) continue;
	  valid = false, complete = true;
	  return -1;
	}
	chunkHdr.parse(chunkBuf);
	if (!chunkHdr.complete()) continue;
	// chunk header complete - update state
	if (!chunkHdr.valid()) {
	  valid = false, complete = true;
	  return -1;
	}
	chunkRemaining = chunkHdr.length;
	chunkTotal += chunkRemaining;
	chunkBuf = {};
      }

      // last chunk?
      if (chunkHdr.eob()) {
	auto data = stream.span();
	if (!data) return consumed;
	bool done = false;
	unsigned used = data.length();
	for (unsigned i = 0, n = data.length(); i < n; ++i) {
	  trlrMatch = trlrMatch_(trlrMatch, data[i]);
	  ++trlrLength;
	  bool empty = trlrLength == 2 && trlrMatch == 2;
	  done = empty || trlrMatch == 4;
	  if (done) {
	    used = i + 1;
	    break;
	  }
	}
	auto trailer = ZtLocalArray(TrailerData, used);
	trailer << ZuBSpan{data.data(), used};
	chunkTrlr = trailer.span();
	stream.advance(used);
	consumed += used;
	complete = done;
	if (used) {
	  bool ok = rcvd(parser);
	  chunkTrlr = {};
	  if (ZuUnlikely(!ok)) return -1;
	}
	if (done) return consumed;
	continue;
      }

      // append to body
      while (chunkRemaining) {
	auto data = stream.span();
	if (!data) return consumed;
	unsigned n = chunkRemaining;
	if (n > data.length()) n = data.length();
	auto body = ZtLocalArray(BodyData, n);
	body << ZuBSpan{data.data(), n};
	received += n;
	chunkRemaining -= n;
	stream.advance(n);
	consumed += n;
	span = body.span();
	bool ok = rcvd(parser);
	span = {};
	if (ZuUnlikely(!ok)) return -1;
	if (!chunkRemaining) {
	  chunkNeedCRLF = true;
	  chunkHdr = {};
	  chunkBuf = {};
	}
      }
    }
  }

  // reset for next message
  void reset() {
    span = {};
    chunkTrlr = {};
    max = Max;
    chunkBuf = {};
    chunkHdr = {};
    chunkTotal = 0;
    chunkRemaining = 0;
    trlrLength = 0;
    trlrMatch = 0;
    contentLength = -1;
    received = 0;
    xferEncoding = -1;
    chunked = false;
    chunkNeedCRLF = false;
    valid = true;
    complete = false;
  }
};

// HTTP message builder

template <bool HasBody> struct Builder_Body { };
template <> struct Builder_Body<true> {
  unsigned	contentLen = 0;

  void contentLength(unsigned n) { contentLen = n; }
};

template <
  typename Header_,
  typename Body_,
  typename Context,
  typename RxStream_>
struct Parser;

template <
  typename Header = Response<>,
  typename Body = Body<>,
  typename Context = ZuEmpty,		// additional context for callbacks
  typename RxStream,
  typename ...Args>
auto parser(RxStream stream, Args &&... args) {
  return Parser<Header, Body, Context, RxStream>(
    ZuMv(stream), ZuFwd<Args>(args)...);
}

template <
  typename Header_ = Response<>,
  typename Body_ = Body<>,
  typename Context = ZuEmpty,		// additional context for callbacks
  typename RxStream_ = ZuEmpty>
struct Parser {
  using Header = Header_;
  using Body = Body_;
  using RxStream = RxStream_;

  RxStream		stream;
  Context		context;
  Header		header;
  Body			body;

  Parser(RxStream stream_) : stream{ZuMv(stream_)} { }
  template <typename ...Args,
    decltype(Context(ZuDeclVal<Args &&>()...), int()) = 0>
  Parser(RxStream stream_, Args &&...args) :
    stream{ZuMv(stream_)}, context(ZuFwd<Args>(args)...) { }

  // bool rcvd()
  // - returns false to disconnect
  template <typename Head, typename Key, typename KV, typename Rcvd>
  int process(
    this auto &&self, Head &&head, Key &&key, KV &&kv, Rcvd &&rcvd)
  {
    if (ZuUnlikely(self.body.complete)) return -1; // should not happen

    unsigned consumed = 0;

    if (!self.header.complete) {
      auto data = self.stream.span();
      if (!data) return 0;
      auto hdr = ZtLocalArray(HdrData, self.header.max);
      int o = -1;
      bool hdrTooLarge = false;
      if constexpr (requires { self.stream.spans([](ZuSpan<uint8_t>) {
	    return true;
	  }); }) {
	self.stream.spans([&hdr, &o, &hdrTooLarge, max = self.header.max](
	      ZuSpan<uint8_t> data) {
	  unsigned n = data.length();
	  unsigned remaining = max - hdr.length();
	  if (n > remaining) n = remaining;
	  hdr << ZuBSpan{data.data(), n};
	  o = eoh(hdr.span());
	  if (o >= 0) return false;
	  if (ZuUnlikely(n < data.length())) {
	    hdrTooLarge = true;
	    return false;
	  }
	  return true;
	});
      } else {
	o = eoh(data);
	if (ZuUnlikely(o < 0)) {
	  if (data.length() > self.header.max) hdrTooLarge = true;
	} else if (ZuUnlikely(unsigned(o) > self.header.max))
	  hdrTooLarge = true;
	if (o >= 0) hdr << ZuBSpan{data.data(), unsigned(o)};
      }
      if (ZuUnlikely(hdrTooLarge)) {
	self.header.complete = true;
	self.header.valid = false;
	ZiLOG(Error, "Zhttp", "HTTP header too large");
	return -1;
      }
      if (o < 0) return 0;
      ZuSpan<uint8_t> headerSpan{hdr.data(), unsigned(o)};
      int n = self.header.parse(
	headerSpan, self, ZuFwd<Head>(head), ZuFwd<Key>(key),
	ZuFwd<KV>(kv));
      if (n < 0) {
	ZiLOG(Error, "Zhttp", "invalid HTTP response");
	return -1;
      }
      // defensive sanity check on parse() return, n > o wreaks havoc
      ZiAssert(n <= o, "Zhttp", (o, n), "o=" << o << " n=" << n, n = o);
      if (!self.header.complete) return 0;
      self.stream.advance(n);
      consumed += n;
      n = self.body.init(self.header);
      if (n < 0) {
	ZiLOG(Error, "Zhttp", "invalid HTTP transfer-encoding / content-length");
	return -1;
      }
      if (self.body.complete) {
	bool disconnect = !rcvd(self);
	if (ZuUnlikely(disconnect)) return -1;
	return consumed;
      }
    }
    if (!self.body.complete) {
      int o = self.body.process(self, ZuFwd<Rcvd>(rcvd));
      if (o < 0) { ZiLOG(Error, "Zhttp", "invalid HTTP body"); return -1; }
      consumed += o;
    }
    if (!self.body.complete) return consumed;
    if (!self.body.valid) {
      // invalid body should have been caught by body.process() returning -1
      ZiLOG(Error, "Zhttp", "Zhttp internal error");
      return -1;
    }

    return consumed;
  }

  // reset() to reuse the buffer for a subsequent message
  void reset() {
    header.reset();
    body.reset();
  }
};

template <
  typename Keys,
  typename KVs,
  bool HasBody,
  bool IsChunked,
  typename Context,
  typename TxStream>
struct Builder;

template <
  typename TxStream,
  typename Keys = ZuStringTL<>,		// custom header keys
  typename KVs = ZuStringTL<>,		// custom header fixed key/values
  bool HasBody = false,			// has a body
  bool IsChunked = false,		// body is chunked
  typename Context = ZuEmpty,		// additional context for callbacks
  typename ...Args>
auto builder(TxStream stream, Args &&... args) {
  return Builder<Keys, KVs, HasBody, IsChunked, Context, TxStream>(
    ZuMv(stream), ZuFwd<Args>(args)...);
}

template <
  typename Keys = ZuStringTL<>,		// custom header keys
  typename KVs = ZuStringTL<>,		// custom header fixed key/values
  bool HasBody = false,			// has a body
  bool IsChunked = false,		// body is chunked
  typename Context = ZuEmpty,		// additional context for callbacks
  typename TxStream = ZuEmpty>
struct Builder : public Builder_Body<HasBody> {
  TxStream		stream;
  Context		context;

  Builder(TxStream stream_) : stream{ZuMv(stream_)} { }
  template <typename ...Args,
    decltype(Context(ZuDeclVal<Args &&>()...), int()) = 0>
  Builder(TxStream stream_, Args &&...args) :
    stream{ZuMv(stream_)}, context(ZuFwd<Args>(args)...) { }

  template <typename L, typename = void>
  struct IsCallable : public ZuFalse { };
  template <typename L>
  struct IsCallable<L, decltype(ZuDeclVal<L &>()(ZuDeclVal<Builder &>()))> :
    public ZuTrue { };

  template <typename V>
  void emit(this auto &self, V &&v) {
    if constexpr (!IsCallable<V>{})
      self.stream << ZuFwd<V>(v);
    else
      ZuFwd<V>(v)(self);
  }

  template <typename KeyFn>
  void headers(this auto &self, KeyFn &&keyFn) {
    if constexpr (HasBody) {
      if constexpr (IsChunked)
	self.stream << "transfer-encoding: chunked\r\n";
      else
	self.stream << "content-length: " << self.contentLen << "\r\n";
    }
    // custom fixed header key/values
    ZuUnroll::all<KVs>([&self]<typename KV>() {
      self.stream << KV{}() << "\r\n";
    });
    // custom variable header keys
    ZuUnroll::all<Keys>([&self, &keyFn]<typename Key>() {
      using I = ZuTypeIndex<Key, Keys>;
      self.stream << Key{}() << ": " << keyFn(self, I{}()) << "\r\n";
    });
    self.stream << "\r\n";
  }

  // request with query
  template <typename Path, typename Query, typename Host, typename KeyFn>
  void request(
    this auto &self,
    unsigned method, Path &&path, Query &&query, Host &&host, KeyFn &&keyFn)
  {
    // method
    self.stream << Method::name(method) << ' ';
    // path
    self.emit(ZuFwd<Path>(path));
    // query (may be prefixed with trailing path components)
    self.emit(ZuFwd<Query>(query));
    // host
    self.stream << " HTTP/1.1\r\nhost: ";
    self.emit(ZuFwd<Host>(host));
    self.stream << "\r\n";
    self.headers(ZuFwd<KeyFn>(keyFn));
  }

  // request without query
  template <typename Path, typename Host, typename KeyFn>
  void request(
    this auto &self,
    unsigned method, Path &&path, Host &&host, KeyFn &&keyFn)
  {
    // method
    self.stream << Method::name(method) << ' ';
    // path
    self.emit(ZuFwd<Path>(path));
    // host
    self.stream << " HTTP/1.1\r\nhost: ";
    self.emit(ZuFwd<Host>(host));
    self.stream << "\r\n";
    self.headers(ZuFwd<KeyFn>(keyFn));
  }

  // response
  template <typename Reason, typename KeyFn>
  void response(
    this auto &self, unsigned status, Reason &&reason, KeyFn &&keyFn)
  {
    // status
    self.stream << "HTTP/1.1 " <<
      ZuBox<unsigned>{status}.fmt<ZuFmt::Right<3>>() << ' ';
    // reason
    self.emit(ZuFwd<Reason>(reason));
    self.stream << "\r\n";
    self.headers(ZuFwd<KeyFn>(keyFn));
  }

  template <typename V>
  Builder &operator <<(V &&v) {
    stream << ZuFwd<V>(v);
    return *this;
  }

  TxStream finish(this auto &&self) {
    return ZuMv(self.stream);
  }
};

} // Zhttp

#endif /* Zhttp_HH */
