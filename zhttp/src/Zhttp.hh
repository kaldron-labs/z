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
#include <zlib/ZuStream.hh>

#include <zlib/ZtBuiltin.hh>

#include <zlib/Ztls.hh>

// FIXME

// while on-the-wire data will remain in `ZiIOBuf`, temporary uncompressed data should be
// predominantly on-stack; see `ZuBase64Test.cc` `enc()` for an example of encoding to an on-stack
// buffer; since `Zhttp` is above `Zt`, we'll use `ZtLocalArray` for on-stack arrays staging
// uncompressed data (either decoded from network buffers, or being encoded to network buffers);
// the goal is to reduce heap memory allocation to a minimum, and potentially eliminate
// `HeaderBytes` entirely; apps should interface with `Zhttp` via inversion-of-control callback
// mechanisms, where `Zhttp` decodes/uncompresses to on-stack temporary storage then calls the app
// with the data

// - KVs can be output as-is (for HTTP 1.1)
// - down the road with QPACK:
//   - QPackKVs is a ZuStringTL<...>, QPackKV2ID is a ZuTypeList<ZuUnsigned<X>, ...>,
//     where X is the QPACK static table index (ID) for the corresponding QPackKV string
//     QPackID2KV is a ZuTypeList<ZuUnsigned<X>, void, ...>, which maps QPACK IDs back
//     to kv
//   - QPackKeys is a ZuStringTL<...>, QPackKey2ID is a ZuTypeList<ZuUnsigned<X>, ...>,
//     where X is the QPACK static table index (ID) for the corresponding QPackKey string
//     QPackID2Key is a ZuTypeList<ZuUnsigned<X>, void, ...>, which maps QPACK IDs back
//     to key
// - this allows compile-time determination of which KVs and Keys should be QPack-encoded
//   on transmit (ZuTypeIndex<QPackKVs, ZuStringT<"...">>[} will be undefined)
// - on receive,
//   ZuSwitch::dispatch<...>(id, [...](auto ID) {
//     using KV = ZuType<ID, QPackID2KV>;
//     if constexpr (!ZuIsSame<KV, void>{})
//       
//   ZuSwitch -> ZuType<I, QPackID2Key> -> keys_[J] = span
//   - BUT with Huffman coding (QPACK uses HPACK), storing the spans in the Reader doesn't
//     work, what's really needed is a mutable context with callbacks so huffman decoding
//     can be on-stack (e.g. "..." -> "1234" -> context.i = 1234;)
//   - this mirrors Builder

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
      normalize(key);
      ZuSpan kv(&key[0], &value[n] - &key[0]); // "key: value"
      int j = kvMatch(kv);
      if (j >= 0)
	kvFn(msg, j);
      else {
	j = keyMatcher.match(key);
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

// header loader
template <template <typename, typename> class Msg, typename Keys, typename KVs, unsigned Max>
struct Header : public Msg<Keys, KVs> {
  using Base = Msg<Keys, KVs>;
  using Base::complete;
  using Base::parse;
  using Base::reset;

  ZuSpan<uint8_t>	span;
  unsigned		max = Max;
  bool			valid = true;

  template <typename Parser, typename Head, typename Key, typename KV>
  int process(
    ZiIOBuf *buf, ZuSpan<uint8_t> rcvd,
    Parser &&parser, Head &&head, Key &&key, KV &&kv)
  {
    if (ZuUnlikely(complete)) return valid ? 0 : -1;
    if (buf->length + rcvd.length() > max) {
      complete = true, valid = false;
      return -1;
    }
    buf->append(&rcvd[0], rcvd.length());
    span = buf->span();

    int o = span.length();
    int n = parse(
      span, parser, ZuFwd<Head>(head), ZuFwd<Key>(key), ZuFwd<KV>(kv));
    if (n < 0) {
      complete = true, valid = false;
      return -1;
    }
    // defensive sanity check on parse() return, n > o wreaks havoc
    ZiAssert(n <= o, "Zhttp", (o, n), "o=" << o << " n=" << n, n = o);
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

  // access and validate the transfer-encoding and content-length headers
  // - attempts to consume any body data lingering in the buffer
  // - returns number of additional bytes consumed
  // - returns -1 if header is invalid
  template <typename Header>
  int init(ZiIOBuf *buf, const Header &header, unsigned max_ = Max) {
    max = max_;
    offset = header.span.length();
    xferEncoding = header.xferEncoding;
    chunked = header.chunked;
    if (chunked) {
      buf->length = offset;
      return 0;
    }
    contentLength = header.contentLength < 0 ? 0 : header.contentLength;
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
    ZiAssert(n >= o, "Zhttp", (o, n), "o=" << o << " n=" << n, o = n);
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
    contentLength = -1;
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
  typename Body_ = Body<>,
  typename Context = ZuEmpty>		// additional context for callbacks
struct Parser {
  using Header = Header_;
  using Body = Body_;

  ZmRef<ZiIOBuf>	buf;
  Context		context;
  Header		header;
  Body			body;

  Parser(ZmRef<ZiIOBuf> buf_) : buf{ZuMv(buf_)} { }
  template <typename ...Args,
    decltype(Context(ZuDeclVal<Args &&>()...), int()) = 0>
  Parser(ZmRef<ZiIOBuf> buf_, Args &&...args) :
    buf{ZuMv(buf_)}, context(ZuFwd<Args>(args)...) { }

  // bool rcvd()
  // - returns false to disconnect
  template <typename Head, typename Key, typename KV, typename Rcvd>
  int process(
    this auto &&self, ZuSpan<uint8_t> data,
    Head &&head, Key &&key, KV &&kv, Rcvd &&rcvd)
  {
    if (ZuUnlikely(self.body.complete)) return -1; // should not happen

    unsigned consumed = 0;
    int o;

    if (!self.header.complete) {
      o = self.header.process(
	self.buf, data, self, ZuFwd<Head>(head), ZuFwd<Key>(key),
	ZuFwd<KV>(kv));
      if (o < 0) { ZiLOG(Error, "Zhttp", "invalid HTTP response"); return -1; }
      if (!self.header.complete) return o;
      if (o) {
	consumed += o;
	data.offset(o);
      }
      auto n = self.body.init(self.buf, self.header);
      if (n < 0) {
	ZiLOG(Error, "Zhttp", "invalid HTTP transfer-encoding / content-length");
	return -1;
      }
      if (n) {
	consumed += n;
	data.offset(n);
      }
      if (!data) return consumed;
    }
    if (!self.body.complete) {
      o = self.body.process(self.buf, data);
      if (o < 0) { ZiLOG(Error, "Zhttp", "invalid HTTP body"); return -1; }
      if (o) {
	consumed += o;
	data.offset(o);
      }
    }
    if (!self.body.complete) return consumed;
    if (!self.body.valid) {
      // invalid body should have been caught by body.process() returning -1
      ZiLOG(Error, "Zhttp", "Zhttp internal error");
      return -1;
    }

    bool disconnect = !rcvd(self);

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
  typename Keys = ZuStringTL<>,		// custom header keys
  typename KVs = ZuStringTL<>,		// custom header fixed key/values
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
  template <typename Path, typename Query, typename Host, typename KeyFn>
  void request(
    this auto &self,
    unsigned method, Path &&path, Query &&query, Host &&host, KeyFn &&keyFn)
  {
    auto &buf = *(self.buf);
    // method
    buf << Method::name(method) << ' ';
    // path
    if constexpr (!IsCallable<Path>{})
      buf << ZuFwd<Path>(path);
    else
      ZuFwd<Path>(path)(self);
    // query (may be prefixed with trailing path components)
    if constexpr (!IsCallable<Query>{})
      buf << ZuFwd<Query>(query);
    else
      ZuFwd<Query>(query)(self);
    // host
    buf << " HTTP/1.1\r\nhost: ";
    if constexpr (!IsCallable<Host>{})
      buf << ZuFwd<Host>(host);
    else
      ZuFwd<Host>(host)(self);
    buf << "\r\n";
    // canonical headers
    if constexpr (HasBody) {
      buf << "content-length:           \r\n"; // placeholder empty value
      self.contentLen = buf.length - 12;
    }
    // custom fixed header key/values
    ZuUnroll::all<KVs>([&buf]<typename KV>() {
      buf << KV{}() << "\r\n";
    });
    // custom variable header keys
    ZuUnroll::all<Keys>([&self, &buf, &keyFn]<typename Key>() {
      using I = ZuTypeIndex<Key, Keys>;
      buf << Key{}() << ": " << keyFn(self, I{}()) << "\r\n";
    });
    buf << "\r\n";
    if constexpr (HasBody) self.body = buf.length;
  }

  // request without query
  template <typename Path, typename Host, typename KeyFn>
  void request(
    this auto &self,
    unsigned method, Path &&path, Host &&host, KeyFn &&keyFn)
  {
    auto &buf = *(self.buf);
    // method
    buf << Method::name(method) << ' ';
    // path
    if constexpr (!IsCallable<Path>{})
      buf << ZuFwd<Path>(path);
    else
      ZuFwd<Path>(path)(self);
    // host
    buf << " HTTP/1.1\r\nhost: ";
    if constexpr (!IsCallable<Host>{})
      buf << ZuFwd<Host>(host);
    else
      ZuFwd<Host>(host)(self);
    buf << "\r\n";
    // canonical headers
    if constexpr (HasBody) {
      buf << "content-length:           \r\n";
      self.contentLen = buf.length - 12;
    }
    // custom fixed header key/values
    ZuUnroll::all<KVs>([&buf]<typename KV>() {
      buf << KV{}() << "\r\n";
    });
    // custom variable header keys
    ZuUnroll::all<Keys>([&self, &buf, &keyFn]<typename Key>() {
      using I = ZuTypeIndex<Key, Keys>;
      buf << Key{}() << ": " << keyFn(self, I{}()) << "\r\n";
    });
    buf << "\r\n";
    if constexpr (HasBody) self.body = buf.length;
  }

  // response
  template <typename Reason, typename KeyFn>
  void response(
    this auto &self, unsigned status, Reason &&reason, KeyFn &&keyFn)
  {
    auto &buf = *(self.buf);
    // status
    buf << "HTTP/1.1 " << ZuBox<unsigned>{status}.fmt<ZuFmt::Right<3>>() << ' ';
    // reason
    if constexpr (!IsCallable<Reason>{})
      buf << ZuFwd<Reason>(reason);
    else
      ZuFwd<Reason>(reason)(self);
    buf << "\r\n";
    // canonical headers
    if constexpr (HasBody) {
      buf << "content-length:           \r\n";
      self.contentLen = buf.length - 12;
    }
    // custom fixed header key/values
    ZuUnroll::all<KVs>([&buf]<typename KV>() {
      buf << KV{}() << "\r\n";
    });
    // custom variable header keys
    ZuUnroll::all<Keys>([&self, &buf, &keyFn]<typename Key>() {
      using I = ZuTypeIndex<Key, Keys>;
      buf << Key{}() << ": " << keyFn(self, I{}()) << "\r\n";
    });
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
