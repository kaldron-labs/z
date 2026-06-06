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

// HTTP message parser

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

  // parse one complete header line, excluding the terminating CRLF
  template <typename Msg, typename KeyFn, typename KVFn>
  int parseLine(ZuSpan<uint8_t> line, Msg &&msg, KeyFn &&keyFn, KVFn &&kvFn) {
    if (!line) {
      complete = true;
      return 1;
    }
    int n = eok(line);
    if (ZuUnlikely(n < 0)) return -1;
    ZuSpan key(&line[0], unsigned(n));
    line.offset(n + 1); // skip key and delimiter
    n = bov(line);
    if (ZuUnlikely(n < 0)) return -1;
    line.offset(n); // skip white space
    ZuSpan value{line.data(), line.length()};
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
    return 1;
  }

  // parse headers
  template <typename Msg, typename KeyFn, typename KVFn>
  int parse(ZuSpan<uint8_t> data, Msg &&msg, KeyFn &&keyFn, KVFn &&kvFn) {
    if (complete) return offset;
    unsigned o = offset;
    data.offset(o);
    for (;;) {
      if (data.length() < 2) return 0;
      int n = eol(data);
      if (ZuUnlikely(n < 0)) return 0;
      int r = parseLine({&data[0], unsigned(n)}, msg, keyFn, kvFn);
      if (ZuUnlikely(r < 0)) return -1;
      o += n + 2;
      data.offset(n + 2);
      offset = o;
      if (complete) return o;
    }
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

  // parse one complete request line, excluding the terminating CRLF
  template <typename Msg, typename Operation>
  int parseStartLine(ZuSpan<uint8_t> data, Msg &&msg, Operation &&operation) {
    unsigned n = data.length();
    int o = 0; // intentionally int
    for (o = 0; o < int(n) && data[o] != ' '; )
      if (ZuUnlikely(++o > 7)) return -1; // unterminated method
    if (ZuUnlikely(!o || o >= int(n))) return -1; // missing method
    method = Method::lookup({&data[0], unsigned(o)});
    unsigned b = ++o;
    while (o < int(n) && data[o] != ' ') ++o;
    if (ZuUnlikely(b == unsigned(o) || o >= int(n))) return -1;
    ZuCSpan path{&data[b], unsigned(o) - b};
    b = ++o;
    if (ZuUnlikely(b >= n)) return -1; // missing protocol
    ZuFwd<Operation>(operation)(msg, method, path);
    return 1;
  }

  // parse request
  template <typename Msg, typename Operation, typename Key, typename KV>
  int parse(
    ZuSpan<uint8_t> data,
    Msg &&msg, Operation &&operation, Key &&key, KV &&kv)
  {
    if (!offset) {
      int o = eol<false>(data);
      if (ZuUnlikely(o < 0)) return 0;
      int r = parseStartLine({&data[0], unsigned(o)}, msg,
	ZuFwd<Operation>(operation));
      if (ZuUnlikely(r < 0)) return -1;
      offset = o + 2;
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

  // parse one complete response line, excluding the terminating CRLF
  template <typename Msg, typename Status>
  int parseStartLine(ZuSpan<uint8_t> data, Msg &&msg, Status &&status_) {
    unsigned n = data.length();
    int o = 0; // intentionally int
    for (o = 0; o < int(n) && data[o] != ' '; )
      if (ZuUnlikely(++o > 8)) return -1; // unterminated protocol
    if (ZuUnlikely(!o || o >= int(n))) return -1; // missing protocol
    unsigned b = ++o;
    int c; // intentionally int
    while (o < int(n) && (c = data[o]) != ' ') {
      if (c < '0' || c > '9') return -1; // not a number
      c -= '0';
      status = status < 0 ? c : (status * 10) + c;
      if (ZuUnlikely(++o > int(b + 3))) return -1;
    }
    if (ZuUnlikely(b == unsigned(o) || o >= int(n))) return -1;
    ZuFwd<Status>(status_)(msg, status);
    return 1;
  }

  // parse response
  template <typename Msg, typename Status, typename Key, typename KV>
  int parse(ZuSpan<uint8_t> data, Msg &&msg, Status &&status_, Key &&key, KV &&kv) {
    if (!offset) {
      int o = eol<false>(data);
      if (ZuUnlikely(o < 0)) return 0;
      int r = parseStartLine({&data[0], unsigned(o)}, msg,
	ZuFwd<Status>(status_));
      if (ZuUnlikely(r < 0)) return -1;
      offset = o + 2;
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

  template <typename Parser, typename Stream, typename Rcvd>
  int process(Parser &parser, Stream &stream, Rcvd &&rcvd) {
    if (ZuUnlikely(complete)) return valid ? 0 : -1;
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

template <
  typename Header_ = Response<>,
  typename Body_ = Body<>,
  typename Context = ZuEmpty>		// additional context for callbacks
struct Parser {
  using Header = Header_;
  using Body = Body_;

  Context		context;
  Header		header;
  Body			body;

  Parser() = default;
  template <typename ...Args,
    decltype(Context(ZuDeclVal<Args &&>()...), int()) = 0>
  Parser(Args &&...args) : context(ZuFwd<Args>(args)...) { }

  // bool rcvd()
  // - returns false to disconnect
  template <typename Stream, typename Head, typename Key, typename KV, typename Rcvd>
  int process(
    this auto &&self, Stream &stream, Head &&head, Key &&key, KV &&kv, Rcvd &&rcvd)
  {
    if (ZuUnlikely(self.body.complete)) return -1; // should not happen

    unsigned consumed = 0;

    if (!self.header.complete) {
      auto hdrTooLarge = [&self]() {
	self.header.complete = true;
	self.header.valid = false;
	ZiLOG(Error, "Zhttp", "HTTP header too large");
	return -1;
      };
      auto parseLine = [&]<bool CanFold, typename Fn>(Fn &&fn) -> int {
	auto data = stream.span();
	if (!data) return 0;
	if (ZuUnlikely(self.header.offset >= self.header.max))
	  return hdrTooLarge();
	unsigned remaining = self.header.max - self.header.offset;
	auto consumeLine = [&](ZuSpan<uint8_t> line, unsigned n) -> int {
	  if (ZuUnlikely(n > remaining)) return hdrTooLarge();
	  int r = ZuFwd<Fn>(fn)(line);
	  if (ZuUnlikely(r < 0)) return -1;
	  stream.advance(n);
	  consumed += n;
	  self.header.offset += n;
	  return 1;
	};
	int o = eol<CanFold>(data);
	if (o >= 0) {
	  unsigned n = unsigned(o) + 2;
	  bool useScratch = false;
	  if constexpr (CanFold) {
	    if (o > 0 && n == data.length()) {
	      bool head = true;
	      stream.spans([&](ZuSpan<uint8_t> span) {
		if (head) { head = false; return true; }
		useScratch = span && islws(span[0]);
		return false;
	      });
	    }
	  }
	  if (!useScratch) return consumeLine({data.data(), unsigned(o)}, n);
	}
	auto line = ZtLocalArray(HdrData, remaining);
	o = -1;
	bool tooLarge = false;
	stream.spans([&](ZuSpan<uint8_t> data) {
	  unsigned n = data.length();
	  unsigned available = remaining - line.length();
	  if (n > available) n = available;
	  line << ZuBSpan{data.data(), n};
	  o = eol<CanFold>(line.span());
	  if (o >= 0) {
	    unsigned m = unsigned(o) + 2;
	    if constexpr (!CanFold)
	      return false;
	    else if (!o || m < line.length())
	      return false;
	  }
	  if (ZuUnlikely(n < data.length())) {
	    tooLarge = true;
	    return false;
	  }
	  return true;
	});
	if (ZuUnlikely(tooLarge || (o < 0 && line.length() >= remaining)))
	  return hdrTooLarge();
	if (o < 0) return 0;
	return consumeLine({line.data(), unsigned(o)}, unsigned(o) + 2);
      };
      while (!self.header.complete) {
	int n;
	if (!self.header.offset)
	  n = parseLine.template operator()<false>([&](ZuSpan<uint8_t> line) {
	    return self.header.parseStartLine(line, self, head);
	  });
	else
	  n = parseLine.template operator()<true>([&](ZuSpan<uint8_t> line) {
	    return self.header.parseLine(line, self, key, kv);
	  });
	if (n < 0) {
	  ZiLOG(Error, "Zhttp", "invalid HTTP response");
	  return -1;
	}
	if (!n) return consumed;
      }
      int n = self.body.init(self.header);
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
      int o = self.body.process(self, stream, ZuFwd<Rcvd>(rcvd));
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
  typename Header = Response<>,
  typename Body = Body<>,
  typename Context = ZuEmpty,		// additional context for callbacks
  typename ...Args>
auto parser(Args &&...args) {
  return Parser<Header, Body, Context>{ZuFwd<Args>(args)...};
}

// HTTP message builder

template <bool HasBody> struct Builder_Body {
  void reset() { }
};
template <> struct Builder_Body<true> {
  unsigned	contentLen = 0;

  void contentLength(unsigned n) { contentLen = n; }
  void reset() { contentLen = 0; }
};

template <
  typename Keys = ZuStringTL<>,		// custom header keys
  typename KVs = ZuStringTL<>,		// custom header fixed key/values
  bool HasBody = false,			// has a body
  bool IsChunked = false,		// body is chunked
  typename Context = ZuEmpty>		// additional context for callbacks
struct Builder : public Builder_Body<HasBody> {
  Context		context;
  bool			chunkStarted = false;

  Builder() = default;
  template <typename ...Args,
    decltype(Context(ZuDeclVal<Args &&>()...), int()) = 0>
  Builder(Args &&...args) : context(ZuFwd<Args>(args)...) { }

  template <typename Stream, typename L, typename = void>
  struct IsEmitter : public ZuFalse { };
  template <typename Stream, typename L>
  struct IsEmitter<Stream, L,
    decltype(ZuDeclVal<L &>()(ZuDeclVal<Builder &>(), ZuDeclVal<Stream &>()))> :
    public ZuTrue { };

private:
  template <typename Stream, typename V>
  void emit(this auto &self, Stream &stream, V &&v) {
    if constexpr (!IsEmitter<Stream, V>{})
      stream << ZuFwd<V>(v);
    else
      ZuFwd<V>(v)(self, stream);
  }

  template <typename Stream, typename KeyFn>
  void headers(this auto &self, Stream &stream, KeyFn &&keyFn) {
    if constexpr (HasBody) {
      if constexpr (IsChunked)
	stream << "transfer-encoding: chunked\r\n";
      else
	stream << "content-length: " << self.contentLen << "\r\n";
    }
    // custom fixed header key/values
    ZuUnroll::all<KVs>([&stream]<typename KV>() {
      stream << KV{}() << "\r\n";
    });
    // custom variable header keys
    ZuUnroll::all<Keys>([&self, &stream, &keyFn]<typename Key>() {
      using I = ZuTypeIndex<Key, Keys>;
      stream << Key{}() << ": " << keyFn(self, I{}()) << "\r\n";
    });
    stream << "\r\n";
  }

public:
  // request with query
  template <typename Stream, typename Path, typename Query, typename Host, typename KeyFn>
  void request(
    this auto &self, Stream &stream,
    unsigned method, Path &&path, Query &&query, Host &&host, KeyFn &&keyFn)
  {
    // method
    stream << Method::name(method) << ' ';
    // path
    self.emit(stream, ZuFwd<Path>(path));
    // query (may be prefixed with trailing path components)
    self.emit(stream, ZuFwd<Query>(query));
    // host
    stream << " HTTP/1.1\r\nhost: ";
    self.emit(stream, ZuFwd<Host>(host));
    stream << "\r\n";
    self.headers(stream, ZuFwd<KeyFn>(keyFn));
  }

  // request without query
  template <typename Stream, typename Path, typename Host, typename KeyFn>
  void request(
    this auto &self, Stream &stream,
    unsigned method, Path &&path, Host &&host, KeyFn &&keyFn)
  {
    // method
    stream << Method::name(method) << ' ';
    // path
    self.emit(stream, ZuFwd<Path>(path));
    // host
    stream << " HTTP/1.1\r\nhost: ";
    self.emit(stream, ZuFwd<Host>(host));
    stream << "\r\n";
    self.headers(stream, ZuFwd<KeyFn>(keyFn));
  }

  // response
  template <typename Stream, typename Reason, typename KeyFn>
  void response(
    this auto &self, Stream &stream,
    unsigned status, Reason &&reason, KeyFn &&keyFn)
  {
    // status
    stream << "HTTP/1.1 " <<
      ZuBox<unsigned>{status}.fmt<ZuFmt::Right<3>>() << ' ';
    // reason
    self.emit(stream, ZuFwd<Reason>(reason));
    stream << "\r\n";
    self.headers(stream, ZuFwd<KeyFn>(keyFn));
  }

  // chunk
  template <typename Stream>
  void chunk(Stream &stream, unsigned n) {
    if constexpr (IsChunked) {
      if (chunkStarted) stream << "\r\n";
      chunkStarted = true;
      stream << ZuBoxed(n).hex<false>() << "\r\n";
    }
  }

  // finish
  template <typename Stream>
  void finish(Stream &stream) {
    if constexpr (IsChunked) {
      if (chunkStarted) stream << "\r\n";
      stream << "0\r\n\r\n";
      chunkStarted = false;
    }
  }

  void reset() {
    Builder_Body<HasBody>::reset();
    chunkStarted = false;
  }
};

template <
  typename Keys = ZuStringTL<>,		// custom header keys
  typename KVs = ZuStringTL<>,		// custom header fixed key/values
  bool HasBody = false,			// has a body
  bool IsChunked = false,		// body is chunked
  typename Context = ZuEmpty,		// additional context for callbacks
  typename ...Args>
auto builder(Args &&...args) {
  return Builder<Keys, KVs, HasBody, IsChunked, Context>{ZuFwd<Args>(args)...};
}

} // Zhttp

#endif /* Zhttp_HH */
