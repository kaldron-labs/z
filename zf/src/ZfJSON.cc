//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZfJSON.hh>

namespace ZfJSON {

int bos(ZuCSpan span) {
  unsigned n = span.length();

  if (ZuUnlikely(!n)) return -1;

  char c;

  for (unsigned o = 0; o < n; o++) {
    c = span[o];
    if (ZuLikely(c == '"')) return o + 1;
    if (!isspace__(c)) return -1;
  }
  return -1;
}

ZuTuple<int, int> eos(ZuSpan<char> span) {
  unsigned n = span.length();

  if (ZuUnlikely(n < 1)) goto bad;

  // fast path
  unsigned i;
  for (i = 0; i < n; i++) {
    char c = span[i];
    if (ZuUnlikely(c == '"')) {
      span[i] = 0;
      return {i, i + 1};
    }
    if (ZuUnlikely(c == '\\')) goto slow;
  }
  goto bad;

slow:
  // slow path - span[i] == '\\' is a precondition
  {
    unsigned o = i;
    while (i < n) {
      char c = span[i];
      if (ZuUnlikely(c == '"')) {
	++i;
	memset(&span[o], 0, i - o); // terminate and pad with zeros
	return {o, i};
      }
      if (ZuUnlikely(c == '\\')) {
	if (ZuUnlikely(++i >= n)) break;
	c = span[i];
	switch (c) {
	  case 'b': c = '\b'; break;
	  case 'f': c = '\f'; break;
	  case 'n': c = '\n'; break;
	  case 'r': c = '\r'; break;
	  case 't': c = '\t'; break;
	  case 'u': {
	    using Fmt = ZuFmt::Hex<1, ZuFmt::Right<4>>;
	    if (ZuUnlikely(i + 5 >= n)) goto bad;
	    uint16_t u16[2];
	    i += ZuBoxed(u16[0]).scan<Fmt>(&span[++i]);
	    if (ZuUTF16::in(u16[0]) == 2) {
	      if (ZuUnlikely(i + 6 >= n)) goto bad;
	      if (span[i] != '\\') goto bad;
	      if (span[++i] != 'u') goto bad;
	      i += ZuBoxed(u16[1]).scan<Fmt>(&span[++i]);
	    }
	    uint32_t u32;
	    if (!ZuUTF16::in(&u16[0], 2, u32)) goto bad;
	    o += ZuUTF8::out(reinterpret_cast<uint8_t *>(&span[o]), n - o, u32);
	    continue;
	  } break;
	  default: break;
	}
      }
      ++i;
      span[o++] = c; // o < i is guaranteed
    }
  }

bad:
  return {-1, -1};
}

namespace Key {
  // UTF8 / ASCII, which is all we care about
  static constexpr const uint8_t lookup_[] = {
    0x10, 0x00, 0xff, 0x03, 0xfe, 0xff, 0xff, 0x87, 0xfe, 0xff, 0xff, 0x07
  };

  ZuInline constexpr bool is(uint8_t c) {
    c -= 32;
    return c > 94 ? false : bool(lookup_[c>>3] & (1<<(c & 7)));
  }
}

int bok(ZuCSpan span) {
  unsigned n = span.length();

  if (ZuUnlikely(!n)) return -1;

  char c;

  for (unsigned o = 0; o < n; o++) {
    c = span[o];
    if (Key::is(c)) return o;
    if (!isspace__(c)) return -1;
  }
  return -1;
}

unsigned eok(ZuCSpan span) { // no error return possible
  unsigned n = span.length();

  char c;
  unsigned o;

  for (o = 0; o < n; o++) {
    c = span[o];
    if (!Key::is(c)) break;
  }
  return o;
}

// beginning of top-level
ZuTuple<int, int> botl(ZuCSpan span) {
  unsigned n = span.length();

  if (ZuUnlikely(n < 2)) goto bad; // minimum is {} or []

  {
    char c;

    for (unsigned o = 0; o < n; o++) {
      c = span[o];
      if (ZuLikely(c == '{')) return {o + 1, ValueTC::Object};
      if (ZuLikely(c == '[')) return {o + 1, ValueTC::Array};
      if (!isspace__(c)) break;
    }
  }

bad:
  return {-1, -1};
}

int boc(ZuCSpan span) {
  unsigned n = span.length();
  for (unsigned o = 0; o < n; o++) {
    char c = span[o];
    if (ZuLikely(c == ':')) return o + 1;
    if (!isspace__(c)) break;
  }
  return -1;
}

namespace ValueTC {
  enum { W = -1, WhiteSpace = W };
  enum { B = -2, Bad = B };

  // UTF8 / ASCII, which is all we care about here
  static constexpr const int8_t lookup_[] = {
    B, B, B, B, B, B, B, B, B, W, W, W, W, W, B, B,
    B, B, B, B, B, B, B, B, B, B, B, B, B, B, B, B,
    W, B, S, B, B, B, B, B, B, B, B, B, B, N, B, B,
    N, N, N, N, N, N, N, N, N, N, B, B, B, B, B, B,
    B, B, B, B, B, B, B, B, B, N, B, B, B, B, N, B,
    B, B, B, B, B, B, B, B, B, B, B, A, B, B, B, B,
    B, B, B, B, B, B, F, B, B, N, B, B, B, B, N, B,
    B, B, B, B, T, B, B, B, B, B, B, O, B, B
  };

  ZuInline constexpr int lookup(uint8_t c) {
    return c > 126 ? B : lookup_[c];
  }
}

ZuTuple<int, int> bov(ZuCSpan span) {
  unsigned n = span.length();

  if (ZuUnlikely(!n)) goto bad;

  unsigned o;

  for (o = 0; o < n; o++) {
    char c = span[o];
    int tc = ValueTC::lookup(c);
    if (tc == ValueTC::WhiteSpace) continue;
    if (tc == ValueTC::Bad) goto bad;
    if (c == 'n') { // null / nan ambiguity
      if (o + 1 >= n) goto bad;
      c = span[o + 1];
      if (c == 'u') return {o, ValueTC::Null};
    }
    switch (tc) {
      case ValueTC::Array:
      case ValueTC::Object:
      case ValueTC::String:
	++o;
	break;
    }
    return {o, tc};
  }
bad:
  return {-1, -1};
}

int eov_Null(ZuCSpan span) {
  unsigned n = span.length();

  if (ZuUnlikely(n < 4)) return -1;
  if (ZuUnlikely(
      span[1] != 'u' ||
      span[2] != 'l' ||
      span[3] != 'l')) return -1;
  return 4;
}

// this implementation intentionally permits
// - trailing commas as in [a,b,]
ZuTuple<int, ZuPtr<AnyNode>> eov_Array(ZuSpan<char> span) {
  if (ZuUnlikely(!span)) return {-1, nullptr};
  auto begin = span.begin();
  auto end = span.end();
  auto node = newNode<AnyNode::Array>();
  auto &array = node->data;
  int o;
  for (;;) {
    // value
    auto bv = bov(span);
    if (ZuUnlikely((o = bv.p<0>()) < 0)) goto next;
    span.offset(o);
    switch (bv.p<1>()) {
      case ValueTC::Null:
	if ((o = eov_Null(span)) < 0) goto bad;
	array.push(newNode<AnyNode::Null>());
	span.offset(o);
	break;
      case ValueTC::Array: {
	auto ev = eov_Array(span);
	if ((o = ev.p<0>()) < 0) goto bad;
	array.push(ZuMv(ev.p<1>()));
	span.offset(o);
      } break;
      case ValueTC::Object: {
	auto ev = eov_Object(span);
	if ((o = ev.p<0>()) < 0) goto bad;
	array.push(ZuMv(ev.p<1>()));
	span.offset(o);
      } break;
      case ValueTC::String: {
	auto ev = eos({&span[0], span.length()});
	if ((o = ev.p<0>()) < 0) goto bad;
	array.push(newNode<AnyNode::String>(
	    ZuSpan<char>(&span[0], unsigned(o))));
	span.offset(ev.p<1>());
      } break;
      case ValueTC::Number:
	if ((o = eov_Number(span)) < 0) goto bad;
	array.push(newNode<AnyNode::Number>(
	    ZuSpan<char>(&span[0], unsigned(o))));
	span.offset(o);
        break;
      case ValueTC::True:
	if ((o = eov_True(span)) < 0) goto bad;
	array.push(newNode<AnyNode::True>());
	span.offset(o);
	break;
      case ValueTC::False:
	if ((o = eov_False(span)) < 0) goto bad;
	array.push(newNode<AnyNode::False>());
	span.offset(o);
	break;
    }
next:
    // comma or closing
    auto bd = bod<']'>(span);
    if ((o = bd.p<0>()) < 0) goto bad;
    span.offset(o);
    if (bd.p<1>() == ']') break;
  }
  if (span) end = span.begin();
  return {int(end - begin), ZuMv(node)};
bad:
  return {-1, nullptr};
}

// this implementation intentionally permits
// - trailing commas: {"a":"b",}
// - skipped fields: {"a":"b",,"c":"d"}
// - unquoted keys: {a:"b"}
ZuTuple<int, ZuPtr<AnyNode>> eov_Object(ZuSpan<char> span) {
  if (ZuUnlikely(!span)) return {-1, nullptr};
  auto begin = span.begin();
  auto end = span.end();
  auto node = newNode<AnyNode::Object>();
  auto &object = node->data;
  ZuCSpan key;
  ZuPtr<AnyNode> value;
  int o;
  for (;;) {
    // key
    if ((o = bos(span)) < 0) { // permit unquoted keys
      if ((o = bok(span)) < 0) goto next; // permit {} {k:v,} etc.
      span.offset(o);
      o = eok(span);
      key = ZuCSpan(&span[0], unsigned(o));
      span.offset(o);
    } else {
      span.offset(o);
      {
	auto ek = eos({&span[0], span.length()});
	if ((o = ek.p<0>()) < 0) goto bad;
	key = ZuCSpan(&span[0], unsigned(o));
	span.offset(ek.p<1>());
      }
    }
    // colon
    if ((o = boc(span)) < 0) goto bad;
    span.offset(o);
    {
      // value
      auto bv = bov(span);
      if ((o = bv.p<0>()) < 0) goto bad;
      span.offset(o);
      switch (bv.p<1>()) {
	case ValueTC::Null:
	  if ((o = eov_Null(span)) < 0) goto bad;
	  value = newNode<AnyNode::Null>();
	  span.offset(o);
	  break;
	case ValueTC::Array: {
	  auto ev = eov_Array(span);
	  if ((o = ev.p<0>()) < 0) goto bad;
	  value = ZuMv(ev.p<1>());
	  span.offset(o);
	} break;
	case ValueTC::Object: {
	  auto ev = eov_Object(span);
	  if ((o = ev.p<0>()) < 0) goto bad;
	  value = ZuMv(ev.p<1>());
	  span.offset(o);
	} break;
	case ValueTC::String: {
	  auto ev = eos({&span[0], span.length()});
	  if ((o = ev.p<0>()) < 0) goto bad;
	  value = newNode<AnyNode::String>(
	    ZuSpan<char>(&span[0], unsigned(o)));
	  span.offset(ev.p<1>());
	} break;
	case ValueTC::Number:
	  if ((o = eov_Number(span)) < 0) goto bad;
	  value = newNode<AnyNode::Number>(
	    ZuSpan<char>(&span[0], unsigned(o)));
	  span.offset(o);
	  break;
	case ValueTC::True:
	  if ((o = eov_True(span)) < 0) goto bad;
	  value = newNode<AnyNode::True>();
	  span.offset(o);
	  break;
	case ValueTC::False:
	  if ((o = eov_False(span)) < 0) goto bad;
	  value = newNode<AnyNode::False>();
	  span.offset(o);
	  break;
      }
      object.push(AnyNode::Field{key, ZuMv(value)});
    }
next:
    // comma or closing
    auto bd = bod<'}'>(span);
    if ((o = bd.p<0>()) < 0) goto bad;
    span.offset(o);
    if (bd.p<1>() == '}') break;
  }
  if (span) end = span.begin();
  return {int(end - begin), ZuMv(node)};
bad:
  return {-1, nullptr};
}

int eov_Number(ZuCSpan span) {
  unsigned n = span.length();

  unsigned o = 0;
  char c;

  // scan value
  c = span[o];
  if (c == '-') {
    if (++o >= n) return -1;
    c = span[o];
  }
  if (c == '.') goto ldot; // permits leading .
  if (c < '0' || c > '9') return -1;
  while (++o < n) {
    c = span[o];
    if (c < '0' || c > '9') goto dot;
  }
  goto end;
dot:
  if (o < n && c == '.') {
ldot:
    while (++o < n) {
      c = span[o];
      if (c < '0' || c > '9') goto exp; // permits trailing .
    }
    goto end;
  }
exp:
  if (c == 'e' || c == 'E') {
    if (++o >= n) return -1;
    c = span[o];
    if (c == '+' || c == '-') {
      if (++o >= n) return -1;
      c = span[o];
    }
    if (c < '0' || c > '9') return -1;
    while (++o < n) {
      c = span[o];
      if (c < '0' || c > '9') break;
    }
  }
end:
  return o ? o : -1;
}

// ZuDecimal is used for all JSON numbers due to JavaScript's bizarre
// lack of primitive integer types
ZuTuple<int, ZuDecimal> eov_Decimal(ZuCSpan span) {
  unsigned n = span.length();

  ZuDecimal d;

  if (!n) goto bad;

  // handle nan, NaN
  {
    char c = span[0];
    if (c == 'n' || c == 'N') {
      if (n < 3) goto bad;
      c = span[1];
      if (c != 'a' && c != 'A') goto bad;
      c = span[2];
      if (c != 'n' && c != 'N') goto bad;
      return {3, ZuDecimal{}};
    }
  }

  {
    // scan value
    int o = d.scan(span);
    if (o < 0) goto bad;
    if (unsigned(o) >= n) return {o, d};

    // check for optional exponent
    char c = span[o];
    if (ZuLikely(c != 'e' && c != 'E')) return {o, d};

    // scan exponent
    if (++o + 1 > n) goto bad;
    if (span[o] == '+') { if (++o + 1 > n) goto bad; }
    span.offset(o);
    ZuBox<int> e;
    {
      int o_ = e.scan(span);
      if (o_ < 0) goto bad;
      o += o_;
    }
    if (ZuLikely(e)) {
      if (ZuUnlikely(e < -18))
	d.value = 0;
      else if (ZuUnlikely(e > 18))
	d.value = ZuDecimal::null();		// no +/- inf
      else if (e < 0)
	d.value /= ZuDecimalFn::pow10_64(-e);
      else
	d.value *= ZuDecimalFn::pow10_64(e);
    }
    return {o, d};
  }

bad:
  return {-1, ZuDecimal{}};
}

ZuTuple<int, double> eov_Float(ZuCSpan span) {
  unsigned n = span.length();

  using Double = ZuBox<double>;

  Double d;

  // handle nan, NaN and +/- inf, Inf, infinity, Infinity
  {
    char c = span[0];
    if (c == 'n' || c == 'N') {
      if (n < 3) goto bad;
      c = span[1];
      if (c != 'a' && c != 'A') goto bad;
      c = span[2];
      if (c != 'n' && c != 'N') goto bad;
      return {3, Double{}};
    }
    bool negative, inf;
    if (c == '-') {
      negative = true;
      c = span[1];
      if (inf = c == 'i' || c == 'I') {
	--n;
	span.offset(1);
      }
    } else {
      negative = false;
      inf = c == 'i' || c == 'I';
    }
    if (inf) {
      unsigned o = 3;
      if ((c = span[1]) != 'n' && c != 'N') goto bad;
      if ((c = span[2]) != 'f' && c != 'F') goto bad;
      if (n >= 8 && ((c = span[3]) == 'i' || c == 'I')) {
	o = 8;
	if ((c = span[4]) != 'n' && c != 'N') goto bad;
	if ((c = span[5]) != 'i' && c != 'I') goto bad;
	if ((c = span[6]) != 't' && c != 'T') goto bad;
	if ((c = span[7]) != 'y' && c != 'Y') goto bad;
      }
      d = ZuCmp<double>::inf();
      return {o + negative, negative ? -d : d};
    }
  }

  {
    // scan value
    int o = d.scan(span);
    if (o < 0) goto bad;
    if (unsigned(o) >= n) return {o, d};

    // check for optional exponent
    char c = span[o];
    if (ZuLikely(c != 'e' && c != 'E')) return {o, d};

    // scan exponent
    if (++o + 1 > n) goto bad;
    if (span[o] == '+') { if (++o + 1 > n) goto bad; }
    span.offset(o);
    ZuBox<int> e;
    {
      int o_ = e.scan(span);
      if (o_ < 0) goto bad;
      o += o_;
    }
    if (ZuLikely(e)) {
      if (e < 0) d /= ZuDecimalFn::pow10_64(-e);
      else d *= ZuDecimalFn::pow10_64(e);
    }
    return {o, d};
  }

bad:
  return {-1, Double{}};
}

int eov_True(ZuCSpan span) {
  unsigned n = span.length();

  if (ZuUnlikely(n < 4)) return -1;
  if (ZuUnlikely(
      span[1] != 'r' ||
      span[2] != 'u' ||
      span[3] != 'e')) return -1;
  return 4;
}

int eov_False(ZuCSpan span) {
  unsigned n = span.length();

  if (ZuUnlikely(n < 5)) return -1;
  if (ZuUnlikely(
      span[1] != 'a' ||
      span[2] != 'l' ||
      span[3] != 's' ||
      span[4] != 'e')) return -1;
  return 5;
}

ZuTuple<int, ZuPtr<AnyNode>> scan(ZuPtr<AnyNode> root, ZuSpan<char> span)
{
  if (ZuUnlikely(!span)) return {0, ZuMv(root)};

  auto begin = span.begin();
  auto end = span.end();
  int o;
  auto tl = botl(span);
  if ((o = tl.p<0>()) < 0) goto bad;
  span.offset(o);
  {
    ZuPtr<AnyNode> node;
    switch (tl.p<1>()) {
      case ValueTC::Array: {
	auto ev = eov_Array(span);
	if ((o = ev.p<0>()) < 0) goto bad;
	span.offset(o);
	node = ZuMv(ev.p<1>());
      } break;
      case ValueTC::Object: {
	auto ev = eov_Object(span);
	if ((o = ev.p<0>()) < 0) goto bad;
	span.offset(o);
	node = ZuMv(ev.p<1>());
      } break;
    }
    if (span) end = span.begin();
    root->data<AnyNode::Array>().push(ZuMv(node));
    return {int(end - begin), ZuMv(root)};
  }
bad:
  return {-1, nullptr};
}

ZuTuple<int, ZuPtr<AnyNode>> scan(ZuSpan<char> span)
{
  ZuPtr<AnyNode> root = newNode<AnyNode::Array>();

  return scan(ZuMv(root), span);
}

class StrictScanner {
public:
  StrictScanner(ZuSpan<char> span, const ScanLimits &limits) :
    m_begin{span.begin()}, m_p{span.begin()}, m_end{span.end()},
    m_limits{limits} { }

  ScanResult scan()
  {
    if (unsigned(m_end - m_begin) > m_limits.size)
      return result(ScanError::Size);
    skip();
    auto node = value(0);
    if (!node) return result(m_error);
    skip();
    if (m_p != m_end) return result(ScanError::Syntax);
    auto root = newNode<AnyNode::Array>();
    root->data.push(ZuMv(node));
    ScanResult result;
    result.offset = int(m_p - m_begin);
    result.error = ScanError::OK;
    result.root = ZuMv(root);
    return result;
  }

private:
  ScanResult result(int error)
  {
    ScanResult result;
    result.offset = int(m_p - m_begin);
    result.error = error;
    return result;
  }

  void skip()
  {
    while (m_p < m_end && isspace__(*m_p)) ++m_p;
  }

  bool node()
  {
    if (m_nodes >= m_limits.nodes) {
      m_error = ScanError::Nodes;
      return false;
    }
    ++m_nodes;
    return true;
  }

  static int hex(char c)
  {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  }

  bool u16(uint16_t &u)
  {
    if (m_end - m_p < 4) return false;
    u = 0;
    for (unsigned i = 0; i < 4; ++i) {
      int v = hex(*m_p++);
      if (v < 0) return false;
      u = uint16_t((u << 4) | v);
    }
    return true;
  }

  bool put(char *&out, char c, char *start)
  {
    if (unsigned(out - start) >= m_limits.string) {
      m_error = ScanError::String;
      return false;
    }
    *out++ = c;
    return true;
  }

  bool putUTF8(char *&out, uint32_t u, char *start)
  {
    unsigned n = ZuUTF8::out(u);
    if (unsigned(out - start) + n > m_limits.string) {
      m_error = ScanError::String;
      return false;
    }
    out += ZuUTF8::out(reinterpret_cast<uint8_t *>(out), n, u);
    return true;
  }

  bool string(ZuSpan<char> &span)
  {
    if (m_p >= m_end || *m_p++ != '"') return false;
    char *start = m_p;
    char *out = m_p;
    while (m_p < m_end) {
      uint8_t c = uint8_t(*m_p++);
      if (c == '"') {
	memset(out, 0, m_p - out);
	span = {start, unsigned(out - start)};
	return true;
      }
      if (c < 0x20) return false;
      if (c == '\\') {
	if (m_p >= m_end) return false;
	switch (c = uint8_t(*m_p++)) {
	  case '"': case '\\': case '/': break;
	  case 'b': c = '\b'; break;
	  case 'f': c = '\f'; break;
	  case 'n': c = '\n'; break;
	  case 'r': c = '\r'; break;
	  case 't': c = '\t'; break;
	  case 'u': {
	    uint16_t u16[2];
	    if (!this->u16(u16[0])) return false;
	    unsigned n = ZuUTF16::in(u16[0]);
	    if (!n) return false;
	    if (n == 2) {
	      if (m_end - m_p < 6 || *m_p++ != '\\' || *m_p++ != 'u' ||
		  !this->u16(u16[1])) return false;
	    }
	    uint32_t u;
	    if (!ZuUTF16::in(u16, n, u) || !putUTF8(out, u, start))
	      return false;
	    continue;
	  }
	  default: return false;
	}
	if (!put(out, char(c), start)) return false;
	continue;
      }
      if (c < 0x80) {
	if (!put(out, char(c), start)) return false;
	continue;
      }
      unsigned n = ZuUTF8::in(c);
      if (!n || m_end - (m_p - 1) < int(n)) return false;
      const uint8_t *in = reinterpret_cast<const uint8_t *>(m_p - 1);
      uint32_t u;
      if (ZuUTF8::in(in, n, u) != n ||
	  ZuUTF8::out(u) != n || u > 0x10ffff ||
	  (u >= 0xd800 && u <= 0xdfff)) return false;
      for (unsigned i = 1; i < n; ++i)
	if ((in[i] & 0xc0) != 0x80) return false;
      if (unsigned(out - start) + n > m_limits.string) {
	m_error = ScanError::String;
	return false;
      }
      for (unsigned i = 0; i < n; ++i) *out++ = char(in[i]);
      m_p += n - 1;
    }
    return false;
  }

  ZuPtr<AnyNode> value(unsigned depth)
  {
    skip();
    if (m_p >= m_end || !node()) return {};
    switch (*m_p) {
      case '{': return object(depth + 1);
      case '[': return array(depth + 1);
      case '"': {
	ZuSpan<char> span;
	if (!string(span)) return syntax();
	return newNode<AnyNode::String>(span);
      }
      case 't':
	if (!literal("true", 4)) return syntax();
	return newNode<AnyNode::True>();
      case 'f':
	if (!literal("false", 5)) return syntax();
	return newNode<AnyNode::False>();
      case 'n':
	if (!literal("null", 4)) return syntax();
	return newNode<AnyNode::Null>();
      default: {
	char *begin = m_p;
	if (!number()) return syntax();
	return newNode<AnyNode::Number>(
	    ZuSpan<char>{begin, unsigned(m_p - begin)});
      }
    }
  }

  ZuPtr<AnyNode> syntax()
  {
    if (m_error == ScanError::OK) m_error = ScanError::Syntax;
    return {};
  }

  bool literal(const char *text, unsigned n)
  {
    if (m_end - m_p < int(n) || memcmp(m_p, text, n)) return false;
    m_p += n;
    return true;
  }

  bool number()
  {
    char *p = m_p;
    if (p < m_end && *p == '-') ++p;
    if (p >= m_end) return false;
    if (*p == '0') {
      ++p;
      if (p < m_end && *p >= '0' && *p <= '9') return false;
    } else {
      if (*p < '1' || *p > '9') return false;
      do { ++p; } while (p < m_end && *p >= '0' && *p <= '9');
    }
    if (p < m_end && *p == '.') {
      if (++p >= m_end || *p < '0' || *p > '9') return false;
      do { ++p; } while (p < m_end && *p >= '0' && *p <= '9');
    }
    if (p < m_end && (*p == 'e' || *p == 'E')) {
      ++p;
      if (p < m_end && (*p == '+' || *p == '-')) ++p;
      if (p >= m_end || *p < '0' || *p > '9') return false;
      do { ++p; } while (p < m_end && *p >= '0' && *p <= '9');
    }
    m_p = p;
    return true;
  }

  ZuPtr<AnyNode> array(unsigned depth)
  {
    if (depth > m_limits.depth) {
      m_error = ScanError::Depth;
      return {};
    }
    ++m_p;
    auto node = newNode<AnyNode::Array>();
    skip();
    if (m_p < m_end && *m_p == ']') { ++m_p; return node; }
    for (;;) {
      auto child = value(depth);
      if (!child) return {};
      node->data.push(ZuMv(child));
      skip();
      if (m_p >= m_end) return syntax();
      if (*m_p == ']') { ++m_p; return node; }
      if (*m_p++ != ',') return syntax();
      skip();
      if (m_p >= m_end || *m_p == ']') return syntax();
    }
  }

  ZuPtr<AnyNode> object(unsigned depth)
  {
    if (depth > m_limits.depth) {
      m_error = ScanError::Depth;
      return {};
    }
    ++m_p;
    auto node = newNode<AnyNode::Object>();
    skip();
    if (m_p < m_end && *m_p == '}') { ++m_p; return node; }
    for (;;) {
      ZuSpan<char> key;
      if (!string(key)) return syntax();
      for (auto &field: node->data)
	if (field.p<0>() == key) {
	  m_error = ScanError::Duplicate;
	  return {};
	}
      skip();
      if (m_p >= m_end || *m_p++ != ':') return syntax();
      auto child = value(depth);
      if (!child) return {};
      node->data.push(AnyNode::Field{key, ZuMv(child)});
      skip();
      if (m_p >= m_end) return syntax();
      if (*m_p == '}') { ++m_p; return node; }
      if (*m_p++ != ',') return syntax();
      skip();
      if (m_p >= m_end || *m_p == '}') return syntax();
    }
  }

  char			*m_begin;
  char			*m_p;
  char			*m_end;
  const ScanLimits	&m_limits;
  unsigned		m_nodes = 0;
  int			m_error = ScanError::OK;
};

ScanResult scanStrict(ZuSpan<char> span, const ScanLimits &limits)
{
  return StrictScanner{span, limits}.scan();
}

} // ZfJSON
