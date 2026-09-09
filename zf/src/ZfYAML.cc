//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <limits.h>

#include <zlib/ZuBox.hh>
#include <zlib/ZuMatcher.hh>

#include <zlib/ZmLHash.hh>

#include <zlib/ZtArray.hh>
#include <zlib/ZtString.hh>

#include <zlib/ZfYAML.hh>

namespace ZfYAMLError {

ZuCSpan reason(uint8_t code)
{
  switch (code) {
    case ZfYAML::SyntaxErrorCode::Syntax: return {};
    case ZfYAML::SyntaxErrorCode::Indent: return "invalid indentation";
    case ZfYAML::SyntaxErrorCode::DuplicateKey: return "duplicate key";
    case ZfYAML::SyntaxErrorCode::Directive: return "directive not supported";
    case ZfYAML::SyntaxErrorCode::Tag: return "tag not supported";
    case ZfYAML::SyntaxErrorCode::ComplexKey: return "complex key not supported";
    case ZfYAML::SyntaxErrorCode::MergeKey: return "merge key not supported";
    case ZfYAML::SyntaxErrorCode::Anchor: return "invalid anchor";
    case ZfYAML::SyntaxErrorCode::AliasMissing: return "unknown alias";
    case ZfYAML::SyntaxErrorCode::AliasActive: return "cyclic alias";
    case ZfYAML::SyntaxErrorCode::AliasMax: return "alias limit exceeded";
    case ZfYAML::SyntaxErrorCode::AliasDepthMax:
      return "alias depth limit exceeded";
    case ZfYAML::SyntaxErrorCode::NodeMax: return "node limit exceeded";
    case ZfYAML::SyntaxErrorCode::Unicode: return "invalid Unicode";
    case ZfYAML::SyntaxErrorCode::Escape: return "invalid escape";
    case ZfYAML::SyntaxErrorCode::SecondDocument: return "second document";
    case ZfYAML::SyntaxErrorCode::TrailingInput: return "trailing input";
  }
  return {};
}

} // ZfYAMLError

namespace ZfYAML {

namespace Context { enum { Block, FlowSeq, FlowMap }; }
namespace Style { enum { Plain, Single, Double, Literal, Folded }; }
namespace Chomp { enum { Clip, Strip, Keep }; }
namespace ValueKind { enum { String, Array, Object }; }

struct TrueIDs {
  using Keys = ZuStringTL<
    "y", "Y", "yes", "Yes", "YES", "true", "True", "TRUE",
    "on", "On", "ON">;
};
struct FalseIDs {
  using Keys = ZuStringTL<
    "n", "N", "no", "No", "NO", "false", "False", "FALSE",
    "off", "Off", "OFF">;
};
static constexpr auto true_ = ZuMatcher<TrueIDs>();
static constexpr auto false_ = ZuMatcher<FalseIDs>();

bool YAMLPolicy::implicit(ZuCSpan s)
{
  return true_.exact(s) >= 0 || false_.exact(s) >= 0 ||
    s == "~" || s == "null" || s == "Null" || s == "NULL";
}

static bool intDigits(
    ZuCSpan s, unsigned i, unsigned base, bool underscore = true)
{
  bool any = false;
  for (unsigned n = s.length(); i < n; ++i) {
    if (underscore && s[i] == '_') {
      any = true;
      continue;
    }
    unsigned v = s[i] >= 'a' ? s[i] - 'a' + 10 :
      s[i] >= 'A' ? s[i] - 'A' + 10 : s[i] - '0';
    if (v >= base) return false;
    any = true;
  }
  return any;
}

int YAMLPolicy::intBase(ZuCSpan s)
{
  unsigned i = 0, n = s.length();
  if (!n) return 0;
  if (s[i] == '+' || s[i] == '-') if (++i >= n) return 0;
  if (i + 2 < n && s[i] == '0') {
    switch (s[i + 1]) {
      case 'b': return intDigits(s, i + 2, 2) ? 2 : 0;
      case 'o': return intDigits(s, i + 2, 8, false) ? 8 : 0;
      case 'x': return intDigits(s, i + 2, 16) ? 16 : 0;
    }
  }
  if (i + 1 < n && s[i] == '0')
    return intDigits(s, i + 1, 8) ? 8 : 0;
  if (s[i] >= '1' && s[i] <= '9') {
    unsigned j = i + 1;
    while (j < n && ((s[j] >= '0' && s[j] <= '9') || s[j] == '_')) ++j;
    if (j < n && s[j] == ':') {
      do {
	if (++j >= n || s[j] < '0' || s[j] > '9') return 0;
	unsigned v = s[j++] - '0';
	if (j < n && s[j] >= '0' && s[j] <= '9') v = v * 10 + s[j++] - '0';
	if (v > 59) return 0;
      } while (j < n && s[j] == ':');
      return j == n ? 60 : 0;
    }
    return j == n ? 10 : 0;
  }
  return s[i] == '0' && i + 1 == n ? 10 : 0;
}

static double intValue(ZuCSpan s, unsigned base)
{
  unsigned i = 0, n = s.length();
  bool negative = false;
  if (s[i] == '+' || s[i] == '-') negative = s[i++] == '-';
  double value = 0;
  if (base == 60) {
    double group = 0;
    for (; i < n; ++i) {
      if (s[i] == '_') continue;
      if (s[i] == ':') {
	value = value * 60 + group;
	group = 0;
	continue;
      }
      group = group * 10 + s[i] - '0';
    }
    value = value * 60 + group;
  } else {
    if (base == 2 || base == 16 ||
	(base == 8 && i + 1 < n && s[i + 1] == 'o')) i += 2;
    for (; i < n; ++i) {
      if (s[i] == '_') continue;
      unsigned digit = s[i] >= 'a' ? s[i] - 'a' + 10 :
	s[i] >= 'A' ? s[i] - 'A' + 10 : s[i] - '0';
      value = value * base + digit;
    }
  }
  return negative ? -value : value;
}

static double numberValue(ZuCSpan s)
{
  if (unsigned base = YAMLPolicy::intBase(s)) return intValue(s, base);
  return ZuBox<double>{s}.val();
}

using AName = ZtString<ZtStringHeapID<"ZfYAML.AnchorName">>;

struct Anchor {
  AnyNode	*node = nullptr;
  unsigned	depth = 0;
};

using Anchors = ZtArray<Anchor, ZtArrayHeapID<"ZfYAML.Anchors">>;

static const char *anchorMapID() { return "ZfYAML.AnchorMap"; }
using AnchorMap = ZmLHashKV<
  AName, unsigned, ZmLHashID<anchorMapID, ZmLHashLocal<>>>;

static int utf8Err(ZuCSpan s, bool controls)
{
  unsigned i = 0, n = s.length();
  while (i < n) {
    uint8_t c = s[i++];
    if (!controls && c < 0x20 && c != '\t' && c != '\n' && c != '\r')
      return i - 1;
    if (c < 0x80) continue;
    unsigned m;
    uint32_t u;
    if ((c & 0xe0) == 0xc0) {
      m = 1;
      u = c & 0x1f;
      if (u < 2) return i - 1;
    } else if ((c & 0xf0) == 0xe0) {
      m = 2;
      u = c & 0x0f;
    } else if ((c & 0xf8) == 0xf0) {
      m = 3;
      u = c & 0x07;
    } else {
      return i - 1;
    }
    if (i + m > n) return i - 1;
    unsigned bytes = m;
    while (m--) {
      uint8_t d = s[i++];
      if ((d & 0xc0) != 0x80) return i - 1;
      u = (u << 6) | (d & 0x3f);
    }
    if ((bytes == 1 && u < 0x80) || (bytes == 2 && u < 0x800) ||
	(bytes == 3 && u < 0x10000) || u > 0x10ffff ||
	(u >= 0xd800 && u <= 0xdfff)) return i - 1;
  }
  return -1;
}

static ZuPtr<AnyNode> clone_(const AnyNode *src, AnyNode *parent)
{
  if (src->has<AnyNode::String>()) {
    auto out = newNode<AnyNode::String>(parent, src->data<AnyNode::String>());
    out->scalarType = src->scalarType;
    return out;
  }
  if (src->has<AnyNode::Array>()) {
    auto out = newNode<AnyNode::Array>(parent);
    for (auto &v: src->data<AnyNode::Array>())
      out->data.push(clone_(v, out));
    return out;
  }
  auto out = newNode<AnyNode::Object>(parent);
  for (auto &f: src->data<AnyNode::Object>())
    out->data.push(AnyNode::Field{f.p<0>(), clone_(f.p<1>(), out)});
  return out;
}

struct Scan::Impl {
  Scan		&scan;
  ZuCSpan	in;
  Limits	limits;
  unsigned	nodes = 0;
  unsigned	aliases = 0;
  Anchors	anchors;
  AnchorMap	anchorMap;

  struct Line {
    unsigned	begin = 0;
    unsigned	content = 0;
    unsigned	end = 0;
    unsigned	next = 0;
    unsigned	indent = 0;
    bool		blank = false;
  };

  struct Value {
    ZuPtr<AnyNode>	node;
    unsigned		next = 0;
    unsigned		depth = 0;
  };

  Impl(Scan &scan) :
    scan{scan}, in{scan.m_span}, limits{scan.m_limits} { }

  bool bad(unsigned pos, uint8_t code = SyntaxErrorCode::Syntax)
  {
    if (scan.m_error.failed) return false;
    if (pos > in.length()) pos = in.length();
    auto &e = scan.m_error;
    e.offset = pos;
    e.code = code;
    e.ch = pos < in.length() ? in[pos] : 0;
    e.line = 1;
    unsigned bol = 0;
    for (unsigned i = 0; i < pos; ++i) {
      if (in[i] == '\r') {
	if (i + 1 < pos && in[i + 1] == '\n') ++i;
	++e.line;
	bol = i + 1;
      } else if (in[i] == '\n') {
	++e.line;
	bol = i + 1;
      }
    }
    e.column = pos - bol + 1;
    e.failed = true;
    return false;
  }

  static bool sep(char c) {
    return c == 0 || c == ' ' || c == '\t' || c == '\r' || c == '\n' ||
      c == ',' || c == ']' || c == '}' || c == '#';
  }

  char at(unsigned i) const { return i < in.length() ? in[i] : 0; }

  Line bol(unsigned pos)
  {
    Line l;
    l.begin = pos;
    unsigned n = in.length();
    while (pos < n && in[pos] == ' ') ++pos;
    if (pos < n && in[pos] == '\t') {
      bad(pos, SyntaxErrorCode::Indent);
      return l;
    }
    l.content = pos;
    l.indent = pos - l.begin;
    while (pos < n && in[pos] != '\r' && in[pos] != '\n') ++pos;
    l.end = pos;
    if (pos < n && in[pos] == '\r') {
      ++pos;
      if (pos < n && in[pos] == '\n') ++pos;
    } else if (pos < n) {
      ++pos;
    }
    l.next = pos;
    unsigned i = l.content;
    while (i < l.end && in[i] == ' ') ++i;
    l.blank = i == l.end || in[i] == '#';
    return l;
  }

  bool bot(unsigned &pos, Line &line)
  {
    unsigned n = in.length();
    while (pos < n) {
      line = bol(pos);
      if (scan.m_error.failed) return false;
      if (!line.blank) {
	pos = line.content;
	return true;
      }
      pos = line.next;
    }
    return false;
  }

  void trivia(unsigned &pos, bool flow)
  {
    unsigned n = in.length();
    for (;;) {
      while (pos < n &&
	  (in[pos] == ' ' || in[pos] == '\t' ||
	   (flow && (in[pos] == '\r' || in[pos] == '\n')))) {
	if (in[pos] == '\t' && !flow) {
	  bad(pos, SyntaxErrorCode::Indent);
	  return;
	}
	++pos;
      }
      if (flow && pos < n && in[pos] == '#') {
	while (pos < n && in[pos] != '\r' && in[pos] != '\n') ++pos;
	continue;
      }
      return;
    }
  }

  template <typename Data, typename ...Args>
  ZuPtr<AnyNode> node(unsigned pos, AnyNode *parent, Args &&...args)
  {
    if (nodes >= limits.nodes) {
      bad(pos, SyntaxErrorCode::NodeMax);
      return {};
    }
    ++nodes;
    return newNode<Data>(parent, ZuFwd<Args>(args)...);
  }

  static bool nameChar(char c)
  {
    return c && c != ' ' && c != '\t' && c != '\r' && c != '\n' &&
      c != ',' && c != '[' && c != ']' && c != '{' && c != '}' &&
      c != '#';
  }

  bool boa(unsigned &pos, char indicator, AName &name)
  {
    unsigned begin = ++pos;
    while (nameChar(at(pos))) ++pos;
    if (pos == begin || !sep(at(pos)))
      return bad(begin, SyntaxErrorCode::Anchor);
    name = ZuCSpan{in.data() + begin, pos - begin};
    return true;
  }

  unsigned anchor(AName name)
  {
    unsigned i = anchors.length();
    anchors.push(Anchor{});
    if (anchorMap.find(name)) anchorMap.del(name);
    anchorMap.add(ZuMv(name), i);
    return i;
  }

  Anchor *findAnchor(ZuCSpan name)
  {
    if (auto n = anchorMap.find(name)) return &anchors[n->p<1>()];
    return nullptr;
  }

  unsigned count(const AnyNode *n) const
  {
    if (!n) return 0;
    unsigned out = 1;
    if (n->has<AnyNode::Array>()) {
      for (auto &v: n->data<AnyNode::Array>()) out += count(v);
    } else if (n->has<AnyNode::Object>()) {
      for (auto &f: n->data<AnyNode::Object>()) out += count(f.p<1>());
    }
    return out;
  }

  static bool number(ZuCSpan s)
  {
    unsigned i = 0, n = s.length();
    if (!n) return false;
    if (YAMLPolicy::intBase(s)) return true;
    if (s[i] == '-') if (++i >= n) return false;
    if (s[i] == '0') {
      ++i;
    } else {
      if (s[i] < '1' || s[i] > '9') return false;
      while (++i < n && s[i] >= '0' && s[i] <= '9');
    }
    if (i < n && s[i] == '.') {
      if (++i >= n || s[i] < '0' || s[i] > '9') return false;
      while (++i < n && s[i] >= '0' && s[i] <= '9');
    }
    if (i < n && (s[i] == 'e' || s[i] == 'E')) {
      if (++i < n && (s[i] == '+' || s[i] == '-')) ++i;
      if (i >= n || s[i] < '0' || s[i] > '9') return false;
      while (++i < n && s[i] >= '0' && s[i] <= '9');
    }
    return i == n;
  }

  static int scalarType(ZuCSpan s)
  {
    if (s == "null") return ScalarTC::Null;
    if (number(s)) return ScalarTC::Number;
    if (true_.exact(s) >= 0) return ScalarTC::True;
    if (false_.exact(s) >= 0) return ScalarTC::False;
    return ScalarTC::String;
  }

  bool utf8(ZuCSpan s, unsigned base, bool controls = false)
  {
    int offset = utf8Err(s, controls);
    if (offset >= 0) return bad(base + unsigned(offset), SyntaxErrorCode::Unicode);
    return true;
  }

  static void putUTF8(AnyNode::String &out, uint32_t u)
  {
    if (u < 0x80) {
      out << char(u);
    } else if (u < 0x800) {
      out << char(0xc0 | (u >> 6)) << char(0x80 | (u & 0x3f));
    } else if (u < 0x10000) {
      out << char(0xe0 | (u >> 12)) << char(0x80 | ((u >> 6) & 0x3f)) <<
	char(0x80 | (u & 0x3f));
    } else {
      out << char(0xf0 | (u >> 18)) << char(0x80 | ((u >> 12) & 0x3f)) <<
	char(0x80 | ((u >> 6) & 0x3f)) << char(0x80 | (u & 0x3f));
    }
  }

  bool hex(unsigned &pos, unsigned n, uint32_t &u)
  {
    u = 0;
    for (unsigned i = 0; i < n; ++i) {
      char c = at(pos++);
      unsigned v;
      if (c >= '0' && c <= '9') v = c - '0';
      else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
      else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
      else return bad(pos - 1, SyntaxErrorCode::Escape);
      u = (u << 4) | v;
    }
    return true;
  }

  bool quoted(unsigned &pos, AnyNode::String &out, char quote)
  {
    unsigned begin = pos++;
    unsigned n = in.length();
    while (pos < n) {
      char c = in[pos++];
      if (c == quote) {
	if (quote == '\'' && at(pos) == '\'') {
	  ++pos;
	  out << '\'';
	  continue;
	}
	return utf8(out, begin, true);
      }
      if (c == '\r' || c == '\n') {
	if (c == '\r' && at(pos) == '\n') ++pos;
	unsigned p = pos;
	Line l;
	unsigned blanks = 0;
	while (p < n) {
	  l = bol(p);
	  if (!l.blank) break;
	  ++blanks;
	  p = l.next;
	}
	if (scan.m_error.failed || p >= in.length())
	  return bad(pos, SyntaxErrorCode::Syntax);
	out << (blanks ? '\n' : ' ');
	while (blanks > 1) { --blanks; out << '\n'; }
	pos = l.content;
	continue;
      }
      if (quote != '"' || c != '\\') {
	if (uint8_t(c) < 0x20 && c != '\t')
	  return bad(pos - 1, SyntaxErrorCode::Unicode);
	out << c;
	continue;
      }
      if (pos >= n) return bad(pos, SyntaxErrorCode::Escape);
      c = in[pos++];
      switch (c) {
	case '0': out << '\0'; break;
	case 'a': out << '\a'; break;
	case 'b': out << '\b'; break;
	case 't': out << '\t'; break;
	case 'n': out << '\n'; break;
	case 'v': out << '\v'; break;
	case 'f': out << '\f'; break;
	case 'r': out << '\r'; break;
	case 'e': out << char(0x1b); break;
	case ' ': out << ' '; break;
	case '"': out << '"'; break;
	case '/': out << '/'; break;
	case '\\': out << '\\'; break;
	case 'N': putUTF8(out, 0x85); break;
	case '_': putUTF8(out, 0xa0); break;
	case 'L': putUTF8(out, 0x2028); break;
	case 'P': putUTF8(out, 0x2029); break;
	case 'x':
	case 'u':
	case 'U': {
	  uint32_t u;
	  unsigned digits = c == 'x' ? 2 : c == 'u' ? 4 : 8;
	  if (!hex(pos, digits, u)) return false;
	  if (c == 'u' && u >= 0xd800 && u <= 0xdbff) {
	    if (at(pos) != '\\' || at(pos + 1) != 'u')
	      return bad(pos, SyntaxErrorCode::Unicode);
	    pos += 2;
	    uint32_t lo;
	    if (!hex(pos, 4, lo)) return false;
	    if (lo < 0xdc00 || lo > 0xdfff)
	      return bad(pos - 4, SyntaxErrorCode::Unicode);
	    u = 0x10000 + ((u - 0xd800) << 10) + (lo - 0xdc00);
	  } else if ((u >= 0xd800 && u <= 0xdfff) || u > 0x10ffff) {
	    return bad(pos - digits, SyntaxErrorCode::Unicode);
	  }
	  putUTF8(out, u);
	} break;
	case '\r':
	  if (at(pos) == '\n') ++pos;
	  while (at(pos) == ' ') ++pos;
	  break;
	case '\n':
	  while (at(pos) == ' ') ++pos;
	  break;
	default: return bad(pos - 1, SyntaxErrorCode::Escape);
      }
    }
    return bad(pos, SyntaxErrorCode::Syntax);
  }

  bool plain(
      unsigned &pos, AnyNode::String &out, int context, unsigned indent,
      bool key = false)
  {
    unsigned begin = pos;
    unsigned end = pos;
    bool separated = true;
    unsigned n = in.length();
    while (pos < n) {
      char c = in[pos];
      if (c == '\r' || c == '\n') break;
      if (c == '#' && separated) break;
      if (c == ':' && sep(at(pos + 1))) break;
      if (context != Context::Block &&
	  (c == ',' || c == ']' || c == '}' || c == '[' || c == '{')) break;
      separated = c == ' ' || c == '\t';
      if (!separated) end = pos + 1;
      ++pos;
    }
    if (end == begin) return bad(begin);
    ZuCSpan first{in.data() + begin, end - begin};
    if (!utf8(first, begin)) return false;
    out.append(first.data(), first.length());
    if (key || context != Context::Block) {
      pos = end;
      return true;
    }
    unsigned last = end;
    unsigned next = pos;
    unsigned scalarIndent = 0;
    for (;;) {
      if (at(next) == '\r') { ++next; if (at(next) == '\n') ++next; }
      else if (at(next) == '\n') ++next;
      else { pos = last; return true; }
      Line l;
      unsigned blanks = 0;
      unsigned p = next;
      while (p < n) {
	l = bol(p);
	if (!l.blank) break;
	++blanks;
	p = l.next;
      }
      if (scan.m_error.failed || p >= in.length() || l.indent <= indent) {
	pos = last;
	return !scan.m_error.failed;
      }
      if (!scalarIndent) scalarIndent = l.indent;
      if (l.indent < scalarIndent) {
	pos = last;
	return true;
      }
      out << (blanks ? '\n' : ' ');
      while (blanks > 1) { --blanks; out << '\n'; }
      unsigned part = l.begin + scalarIndent;
	if (!plain(part, out, Context::Block, indent, true)) return false;
      unsigned tail = part;
      while (tail < l.end && (at(tail) == ' ' || at(tail) == '\t')) ++tail;
      if (tail < l.end && at(tail) != '#') return bad(tail);
	last = part;
      next = l.end;
    }
  }

  bool eos(
      unsigned &pos, AnyNode::String &out, int &type,
      int context, unsigned indent, bool key = false)
  {
    switch (bos(pos)) {
      case Style::Single:
      case Style::Double:
	type = ScalarTC::String;
	return quoted(pos, out, at(pos));
      case Style::Literal:
      case Style::Folded:
	if (key || context != Context::Block) return bad(pos);
	return blockScalar(pos, out, at(pos) == '>', indent);
    }
    if (!plain(pos, out, context, indent, key)) return false;
    type = key ? ScalarTC::String : scalarType(out);
    return true;
  }

  int bos(unsigned pos) const
  {
    switch (at(pos)) {
      case '\'': return Style::Single;
      case '"': return Style::Double;
      case '|': return Style::Literal;
      case '>': return Style::Folded;
      default: return Style::Plain;
    }
  }

  bool blockScalar(
      unsigned &pos, AnyNode::String &out, bool folded, unsigned parentIndent)
  {
    ++pos;
    int chomp = Chomp::Clip;
    unsigned explicitIndent = 0;
    for (unsigned k = 0; k < 2; ++k) {
      char c = at(pos);
      if (c == '+' || c == '-') {
	if (chomp != Chomp::Clip) return bad(pos);
	chomp = c == '+' ? Chomp::Keep : Chomp::Strip;
	++pos;
      } else if (c >= '1' && c <= '9') {
	if (explicitIndent) return bad(pos);
	explicitIndent = c - '0';
	++pos;
      }
    }
    while (at(pos) == ' ') ++pos;
    if (at(pos) == '#') while (at(pos) && at(pos) != '\r' && at(pos) != '\n') ++pos;
    if (at(pos) != '\r' && at(pos) != '\n' && at(pos)) return bad(pos);
    if (at(pos) == '\r') { ++pos; if (at(pos) == '\n') ++pos; }
    else if (at(pos) == '\n') ++pos;

    unsigned contentIndent = explicitIndent ? parentIndent + explicitIndent : 0;
    unsigned breaks = 0;
    bool have = false;
    bool prevMore = false;
    unsigned n = in.length();
    while (pos < n) {
      Line l = bol(pos);
      if (scan.m_error.failed) return false;
      if (l.blank) {
	++breaks;
	pos = l.next;
	continue;
      }
      if (!contentIndent) contentIndent = l.indent;
      if (l.indent < contentIndent || contentIndent <= parentIndent) break;
      bool more = l.indent > contentIndent;
      if (have) {
	if (!folded) {
	  out << '\n';
	  while (breaks) { --breaks; out << '\n'; }
	} else if (more || prevMore) {
	  out << '\n';
	  while (breaks) { --breaks; out << '\n'; }
	} else if (breaks) {
	  while (breaks) { --breaks; out << '\n'; }
	} else {
	  out << ' ';
	}
      } else {
	while (breaks) { --breaks; out << '\n'; }
      }
      unsigned begin = l.begin + contentIndent;
      ZuCSpan part{in.data() + begin, l.end - begin};
      if (!utf8(part, begin)) return false;
      out.append(part.data(), part.length());
      have = true;
      prevMore = more;
      pos = l.next;
    }
    if (chomp == Chomp::Keep) {
      out << '\n';
      while (breaks) { --breaks; out << '\n'; }
    } else if (chomp == Chomp::Clip && (have || breaks)) {
      out << '\n';
    }
    return true;
  }

  int colon(unsigned pos, unsigned end) const
  {
    char quote = 0;
    unsigned flow = 0;
    for (; pos < end; ++pos) {
      char c = in[pos];
      if (quote) {
	if (quote == '"' && c == '\\') { ++pos; continue; }
	if (c == quote) {
	  if (quote == '\'' && at(pos + 1) == '\'') { ++pos; continue; }
	  quote = 0;
	}
	continue;
      }
      if (c == '\'' || c == '"') { quote = c; continue; }
      if (c == '[' || c == '{') { ++flow; continue; }
      if (c == ']' || c == '}') { if (flow) --flow; continue; }
      if (!flow && c == ':' && sep(at(pos + 1))) return pos;
      if (!flow && c == '#' && (pos == 0 || at(pos - 1) == ' ')) break;
    }
    return -1;
  }

  bool duplicate(const AnyNode::Object &object, ZuCSpan key) const
  {
    for (auto &f: object) if (f.p<0>() == key) return true;
    return false;
  }

  Value alias(unsigned pos, AnyNode *parent, unsigned namePos)
  {
    AName name;
    unsigned next = namePos;
    if (!boa(next, '*', name)) return {};
    auto a = findAnchor(name);
    if (!a) { bad(namePos, SyntaxErrorCode::AliasMissing); return {}; }
    if (!a->node) { bad(namePos, SyntaxErrorCode::AliasActive); return {}; }
    if (aliases >= limits.aliases) { bad(namePos, SyntaxErrorCode::AliasMax); return {}; }
    unsigned depth = a->depth + 1;
    if (depth > limits.aliasDepth) {
      bad(namePos, SyntaxErrorCode::AliasDepthMax);
      return {};
    }
    unsigned n = count(a->node);
    if (nodes + n > limits.nodes) { bad(namePos, SyntaxErrorCode::NodeMax); return {}; }
    ++aliases;
    nodes += n;
    return {clone_(a->node, parent), next, depth};
  }

  Value eov(
      unsigned pos, AnyNode *parent, int context, unsigned indent)
  {
    trivia(pos, context != Context::Block);
    if (scan.m_error.failed) return {};
    unsigned anchorIndex = UINT_MAX;
    if (at(pos) == '&') {
      AName name;
      if (!boa(pos, '&', name)) return {};
      anchorIndex = anchor(ZuMv(name));
      trivia(pos, context != Context::Block);
      if (context == Context::Block &&
	  (at(pos) == '\r' || at(pos) == '\n' || at(pos) == '#')) {
	while (at(pos) && at(pos) != '\r' && at(pos) != '\n') ++pos;
	if (at(pos) == '\r') { ++pos; if (at(pos) == '\n') ++pos; }
	else if (at(pos) == '\n') ++pos;
	Line l;
	unsigned p = pos;
	if (!bot(p, l) || l.indent <= indent) {
	  bad(pos, SyntaxErrorCode::Anchor);
	  return {};
	}
	pos = p;
	indent = l.indent;
      }
      if (at(pos) == '&') {
	bad(pos, SyntaxErrorCode::Anchor);
	return {};
      }
    }

    Value out;
    switch (at(pos)) {
      case '*': out = alias(pos, parent, pos); break;
      case '!': bad(pos, SyntaxErrorCode::Tag); break;
      case '%': bad(pos, SyntaxErrorCode::Directive); break;
      case '?':
	if (sep(at(pos + 1))) bad(pos, SyntaxErrorCode::ComplexKey);
	else out = eov_String(pos, parent, context, indent);
	break;
      default: {
	switch (bov(pos, context, indent)) {
	  case ValueKind::Array:
	    out = eov_Array(pos, parent, context, indent);
	    break;
	  case ValueKind::Object:
	    out = eov_Object(pos, parent, context, indent);
	    break;
	  default:
	    out = eov_String(pos, parent, context, indent);
	    break;
	}
      } break;
    }
    if (!out.node) return {};
    if (anchorIndex != UINT_MAX) {
      auto &a = anchors[anchorIndex];
      a.node = out.node;
      a.depth = out.depth;
    }
    return out;
  }

  int bov(unsigned pos, int context, unsigned indent)
  {
    if (at(pos) == '[') return ValueKind::Array;
    if (at(pos) == '{') return ValueKind::Object;
    if (context != Context::Block) return ValueKind::String;
    Line l = bol(pos - indent);
    if (l.indent == indent && at(pos) == '-' && sep(at(pos + 1)))
      return ValueKind::Array;
    if (colon(pos, l.end) >= 0) return ValueKind::Object;
    return ValueKind::String;
  }

  Value eov_String(
      unsigned pos, AnyNode *parent, int context, unsigned indent)
  {
    auto out = node<AnyNode::String>(pos, parent);
    if (!out) return {};
    int type = ScalarTC::String;
    unsigned next = pos;
    if (!eos(next, out->data<AnyNode::String>(), type, context, indent))
      return {};
    out->scalarType = type;
    return {ZuMv(out), next, 0};
  }

  Value null(unsigned pos, AnyNode *parent)
  {
    auto out = node<AnyNode::String>(pos, parent);
    if (!out) return {};
    out->scalarType = ScalarTC::Null;
    return {ZuMv(out), pos, 0};
  }

  char bod(unsigned &pos, char close)
  {
    trivia(pos, true);
    char c = at(pos);
    return c == ',' || c == close ? c : 0;
  }

  Value eov_Array(
      unsigned pos, AnyNode *parent, int context, unsigned indent)
  {
    if (context == Context::Block && at(pos) != '[')
      return eov_ArrayBlock(pos, parent, indent);
    return eov_ArrayFlow(pos, parent);
  }

  Value eov_ArrayFlow(unsigned pos, AnyNode *parent)
  {
    auto out = node<AnyNode::Array>(pos, parent);
    if (!out) return {};
    unsigned depth = 0;
    ++pos;
    trivia(pos, true);
    if (at(pos) == ']') return {ZuMv(out), pos + 1, depth};
    for (;;) {
      if (at(pos) == ',') { bad(pos); return {}; }
      auto v = eov(pos, out, Context::FlowSeq, 0);
      if (!v.node) return {};
      if (v.depth > depth) depth = v.depth;
      out->data<AnyNode::Array>().push(ZuMv(v.node));
      pos = v.next;
      char d = bod(pos, ']');
      if (d == ']') return {ZuMv(out), pos + 1, depth};
      if (d != ',') { bad(pos); return {}; }
      ++pos;
      trivia(pos, true);
      if (at(pos) == ']') return {ZuMv(out), pos + 1, depth};
    }
  }

  bool key(unsigned &pos, AnyNode::String &out, int context, unsigned indent)
  {
    if (at(pos) == '&' || at(pos) == '*')
      return bad(pos, SyntaxErrorCode::ComplexKey);
    int type;
    return eos(pos, out, type, context, indent, true);
  }

  Value eov_Object(
      unsigned pos, AnyNode *parent, int context, unsigned indent)
  {
    if (context == Context::Block && at(pos) != '{')
      return eov_ObjectBlock(pos, parent, indent);
    return eov_ObjectFlow(pos, parent);
  }

  Value eov_ObjectFlow(unsigned pos, AnyNode *parent)
  {
    auto out = node<AnyNode::Object>(pos, parent);
    if (!out) return {};
    unsigned depth = 0;
    ++pos;
    trivia(pos, true);
    if (at(pos) == '}') return {ZuMv(out), pos + 1, depth};
    for (;;) {
      AnyNode::String k;
      bool quotedKey = at(pos) == '\'' || at(pos) == '"';
      if (!key(pos, k, Context::FlowMap, 0)) return {};
      if (k == "<<" && !quotedKey) {
	bad(pos - k.length(), SyntaxErrorCode::MergeKey);
	return {};
      }
      trivia(pos, true);
      if (at(pos) != ':') { bad(pos); return {}; }
      ++pos;
      trivia(pos, true);
      Value v;
      if (at(pos) == ',' || at(pos) == '}') v = null(pos, out);
      else v = eov(pos, out, Context::FlowMap, 0);
      if (!v.node) return {};
      if (duplicate(out->data<AnyNode::Object>(), k)) {
	bad(pos, SyntaxErrorCode::DuplicateKey);
	return {};
      }
      if (v.depth > depth) depth = v.depth;
      out->data<AnyNode::Object>().push(
	AnyNode::Field{ZuMv(k), ZuMv(v.node)});
      pos = v.next;
      char d = bod(pos, '}');
      if (d == '}') return {ZuMv(out), pos + 1, depth};
      if (d != ',') { bad(pos); return {}; }
      ++pos;
      trivia(pos, true);
      if (at(pos) == '}') return {ZuMv(out), pos + 1, depth};
    }
  }

  Value eov_ArrayBlock(unsigned pos, AnyNode *parent, unsigned indent)
  {
    auto out = node<AnyNode::Array>(pos, parent);
    if (!out) return {};
    unsigned depth = 0;
    unsigned next = pos;
    for (;;) {
      Line l;
      if (next == pos) {
	unsigned b = pos;
	while (b && at(b - 1) != '\r' && at(b - 1) != '\n') --b;
	l = bol(b);
	l.content = pos;
	l.indent = indent;
      } else {
	unsigned q = next;
	if (!bot(q, l)) { next = in.length(); break; }
	if (l.indent != indent) { next = l.begin; break; }
	next = q;
      }
      if (scan.m_error.failed) return {};
      if (l.blank || l.indent != indent || at(l.content) != '-' ||
	  !sep(at(l.content + 1))) break;
      unsigned p = l.content + 1;
      while (p < l.end && at(p) == ' ') ++p;
      Value v;
      if (p >= l.end || at(p) == '#') {
	unsigned q = l.next;
	Line child;
	if (bot(q, child) && child.indent > indent)
	  v = eov(q, out, Context::Block, child.indent);
	else
	  v = null(l.end, out);
      } else if (colon(p, l.end) >= 0) {
	v = eov_ObjectBlock(p, out, indent + 2);
      } else {
	v = eov(p, out, Context::Block, indent);
      }
      if (!v.node) return {};
      if (v.depth > depth) depth = v.depth;
      out->data<AnyNode::Array>().push(ZuMv(v.node));
      next = v.next;
      if (next <= l.end) next = l.next;
    }
    return {ZuMv(out), next, depth};
  }

  Value eov_ObjectBlock(unsigned pos, AnyNode *parent, unsigned indent)
  {
    auto out = node<AnyNode::Object>(pos, parent);
    if (!out) return {};
    unsigned depth = 0;
    unsigned next = pos;
    for (;;) {
      Line l;
      if (next == pos) {
	unsigned b = pos;
	while (b && at(b - 1) != '\r' && at(b - 1) != '\n') --b;
	l = bol(b);
	l.content = pos;
	l.indent = indent;
      } else {
	unsigned q = next;
	if (!bot(q, l)) { next = in.length(); break; }
	if (l.indent != indent) { next = l.begin; break; }
	next = q;
      }
      if (at(l.content) == '?' && sep(at(l.content + 1))) {
	bad(l.content, SyntaxErrorCode::ComplexKey);
	return {};
      }
      int c = colon(l.content, l.end);
      if (c < 0) { if (next == pos) bad(l.content); break; }
      unsigned p = l.content;
      AnyNode::String k;
      if (!key(p, k, Context::Block, indent)) return {};
      while (p < unsigned(c) && at(p) == ' ') ++p;
      if (p != unsigned(c)) { bad(p); return {}; }
      bool quotedKey = at(l.content) == '\'' || at(l.content) == '"';
      if (!quotedKey && k == "<<") {
	bad(l.content, SyntaxErrorCode::MergeKey);
	return {};
      }
      if (duplicate(out->data<AnyNode::Object>(), k)) {
	bad(l.content, SyntaxErrorCode::DuplicateKey);
	return {};
      }
      p = c + 1;
      while (p < l.end && at(p) == ' ') ++p;
      Value v;
      if (p < l.end && at(p) != '#') {
	v = eov(p, out, Context::Block, indent);
      } else {
	unsigned q = l.next;
	Line child;
	if (bot(q, child) &&
	    (child.indent > indent ||
	     (child.indent == indent && at(q) == '-' && sep(at(q + 1)))))
	  v = eov(q, out, Context::Block, child.indent);
	else
	  v = null(l.end, out);
      }
      if (!v.node) return {};
      if (v.depth > depth) depth = v.depth;
      out->data<AnyNode::Object>().push(
	AnyNode::Field{ZuMv(k), ZuMv(v.node)});
      next = v.next;
      if (next <= l.end) next = l.next;
    }
    return {ZuMv(out), next, depth};
  }

  bool eod(unsigned pos, ZuCSpan value) const
  {
    if (pos + value.length() > in.length() ||
	ZuCSpan{in.data() + pos, value.length()} != value)
      return false;
    unsigned i = pos + value.length();
    while (at(i) == ' ') ++i;
    return !at(i) || at(i) == '#' || at(i) == '\r' || at(i) == '\n';
  }

  ZuTuple<int, ZuPtr<AnyNode>> run()
  {
    if (!in.data()) {
      bad(0);
      return {};
    }
    unsigned pos = 0;
    unsigned n = in.length();
    if (n >= 3 && uint8_t(in[0]) == 0xef &&
	uint8_t(in[1]) == 0xbb && uint8_t(in[2]) == 0xbf) pos = 3;
    if (!utf8({in.data() + pos, n - pos}, pos)) return {};
    for (unsigned i = pos; i + 2 < n; ++i)
      if (uint8_t(in[i]) == 0xef && uint8_t(in[i + 1]) == 0xbb &&
	  uint8_t(in[i + 2]) == 0xbf) {
	bad(i, SyntaxErrorCode::Unicode);
	return {};
      }
    Line l;
    if (!bot(pos, l)) {
      if (scan.m_error.failed) return {};
      auto root = node<AnyNode::Object>(pos, nullptr);
      return {int(n), ZuMv(root)};
    }
    if (at(pos) == '%') { bad(pos, SyntaxErrorCode::Directive); return {}; }
    bool start = eod(pos, "---");
    if (start) {
      pos = l.next;
      if (!bot(pos, l)) {
	auto root = null(n, nullptr);
	return {int(n), ZuMv(root.node)};
      }
      if (l.indent == 0 && eod(pos, "...")) {
	auto root = null(pos, nullptr);
	pos = l.next;
	if (bot(pos, l)) {
	  if (l.indent == 0 && eod(pos, "---"))
	    bad(pos, SyntaxErrorCode::SecondDocument);
	  else
	    bad(pos, SyntaxErrorCode::TrailingInput);
	  return {};
	}
	return {int(n), ZuMv(root.node)};
      }
    }
    auto root = eov(pos, nullptr, Context::Block, l.indent);
    if (!root.node) return {};
    root.node->parent = nullptr;
    pos = root.next;
    Line tail;
    if (bot(pos, tail)) {
      if (tail.indent == 0 && eod(pos, "...")) {
	pos = tail.next;
	if (bot(pos, tail)) {
	  if (tail.indent == 0 && eod(pos, "---"))
	    bad(pos, SyntaxErrorCode::SecondDocument);
	  else
	    bad(pos, SyntaxErrorCode::TrailingInput);
	  return {};
	}
      } else if (tail.indent == 0 && eod(pos, "---")) {
	bad(pos, SyntaxErrorCode::SecondDocument);
	return {};
      } else {
	bad(pos, SyntaxErrorCode::TrailingInput);
	return {};
      }
    }
    return {int(n), ZuMv(root.node)};
  }

};

ZuTuple<int, ZuPtr<AnyNode>> Scan::scan()
{
  m_error = {};
  Impl impl{*this};
  auto result = impl.run();
  if (m_error.failed) {
    const auto &e = m_error;
    throw ZfYAML_EXCEPT(
      ZfYAMLError::badSyntax(e.line, e.column, e.offset, e.ch, e.code));
  }
  return result;
}

ZuTuple<int, ZuPtr<AnyNode>> scan(ZuCSpan span, Limits limits)
{
  return Scan{span, limits}.scan();
}

static int fld(const AnyNode::Object &o, ZuCSpan key)
{
  for (unsigned i = 0, n = o.length(); i < n; ++i)
    if (o[i].p<0>() == key) return i;
  return -1;
}

static AnyNode::Field erase_(AnyNode::Object &o, unsigned i)
{
  AnyNode::Field out{ZuMv(o[i])};
  o.splice(i, 1);
  return out;
}

static bool ancestor(const AnyNode *node, const AnyNode *target)
{
  for (; node; node = node->parent) if (node == target) return true;
  return false;
}

using ResolveActive = ZtArray<
  const AnyNode *, ZtArrayHeapID<"ZfYAML.ResolveActive">>;

static bool active(const ResolveActive &active, const AnyNode *target)
{
  for (auto v: active) if (v == target) return true;
  return false;
}

static bool pctDecode(ZuCSpan in, AnyNode::String &out)
{
  for (unsigned i = 0, n = in.length(); i < n; ++i) {
    char c = in[i];
    if (c != '%') {
      out << c;
      continue;
    }
    if (i + 2 >= n) return false;
    unsigned v = 0;
    for (unsigned j = 0; j < 2; ++j) {
      c = in[++i];
      unsigned d;
      if (c >= '0' && c <= '9') d = c - '0';
      else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
      else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
      else return false;
      v = (v << 4) | d;
    }
    out << char(v);
  }
  // Match decodeURIComponent's UTF-8 validation after percent decoding.
  return utf8Err(out, true) < 0;
}

static bool index_(ZuCSpan s, unsigned &v)
{
  unsigned n = s.length();
  if (!n || (n > 1 && s[0] == '0')) return false;
  v = 0;
  for (unsigned i = 0; i < n; ++i) {
    if (s[i] < '0' || s[i] > '9') return false;
    unsigned d = s[i] - '0';
    if (v > (UINT_MAX - d) / 10) return false;
    v = v * 10 + d;
  }
  return true;
}

static const AnyNode *pointer(
    const AnyNode *root, ZuCSpan ref, const AnyNode *site)
{
  if (ref.length() < 2 || ref[0] != '#' || ref[1] != '/') return nullptr;
  const AnyNode *node = root;
  unsigned pos = 2;
  unsigned n = ref.length();
  for (;;) {
    unsigned end = pos;
    while (end < n && ref[end] != '/') ++end;
    AnyNode::String key;
    if (!pctDecode({ref.data() + pos, end - pos}, key)) {
      ZeString path;
      site->path(path);
      throw ZfYAML_EXCEPT(ZfYAMLError::badPointer(path, ref));
    }
    AnyNode::String decoded;
    for (unsigned i = 0, n = key.length(); i < n; ++i) {
      if (key[i] == '~' && i + 1 < n) {
	if (key[i + 1] == '1') { decoded << '/'; ++i; continue; }
	if (key[i + 1] == '0') { decoded << '~'; ++i; continue; }
      }
      decoded << key[i];
    }
    if (node->has<AnyNode::Object>()) {
      int i = fld(node->data<AnyNode::Object>(), decoded);
      if (i < 0) return nullptr;
      node = node->data<AnyNode::Object>()[i].p<1>();
    } else if (node->has<AnyNode::Array>()) {
      unsigned i;
      if (!index_(decoded, i) || i >= node->data<AnyNode::Array>().length())
	return nullptr;
      node = node->data<AnyNode::Array>()[i];
    } else {
      return nullptr;
    }
    if (end == n) return node;
    pos = end + 1;
  }
}

ZuPtr<AnyNode> resolve(ZuPtr<AnyNode> &&root)
{
  if (!root) return {};
  struct Work {
    AnyNode		*node = nullptr;
    const AnyNode	*target = nullptr;
  };
  ZtArray<Work, ZtArrayHeapID<"ZfYAML.ResolveStack">> stack;
  ResolveActive activeSet;
  stack.push(Work{root});
  while (stack) {
    Work w{ZuMv(stack[stack.length() - 1])};
    stack.pop();
    if (w.target) {
      if (activeSet && activeSet[activeSet.length() - 1] == w.target)
	activeSet.pop();
      continue;
    }
    AnyNode *node = w.node;
    const AnyNode *expanded = nullptr;
    if (node->has<AnyNode::Object>()) {
      auto &o = node->data<AnyNode::Object>();
      int ri = fld(o, "$ref");
      if (ri >= 0) {
	auto refNode = o[ri].template p<1>().ptr();
	if (refNode->has<AnyNode::String>() &&
	    refNode->scalarType == ScalarTC::String) {
	  ZuCSpan ref = refNode->data<AnyNode::String>();
	  auto target = pointer(root, ref, node);
	  if (target && target->has<AnyNode::Object>() &&
	      !ancestor(node, target) && !active(activeSet, target)) {
	    auto base = clone_(target, node);
	    auto &dst = base->data<AnyNode::Object>();
	    AnyNode::Object rest;
	    for (unsigned i = 0, n = o.length(); i < n; ++i)
	      if (int(i) != ri) rest.push(ZuMv(o[i]));
	    for (auto &f: rest) {
	      int di = fld(dst, f.p<0>());
	      if (di >= 0) {
		dst[di].template p<1>() = ZuMv(f.p<1>());
		dst[di].template p<1>()->parent = node;
	      } else {
		f.p<1>()->parent = node;
		dst.push(ZuMv(f));
	      }
	    }
	    o = ZuMv(dst);
	    for (auto &f: o) f.p<1>()->parent = node;
	    expanded = target;
	  }
	}
      }
    }
    if (expanded) {
      activeSet.push(expanded);
      stack.push(Work{nullptr, expanded});
    }
    if (node->has<AnyNode::Array>()) {
      auto &a = node->data<AnyNode::Array>();
      for (unsigned i = a.length(); i; --i) stack.push(Work{a[i - 1]});
    } else if (node->has<AnyNode::Object>()) {
      for (auto &f: node->data<AnyNode::Object>())
	stack.push(Work{f.template p<1>()});
    }
  }
  return ZuMv(root);
}

static bool same(const AnyNode *a, const AnyNode *b)
{
  if (!a->has<AnyNode::String>() || !b->has<AnyNode::String>()) return false;
  if (a->scalarType == ScalarTC::Number && b->scalarType == ScalarTC::Number)
    return numberValue(a->data<AnyNode::String>()) ==
      numberValue(b->data<AnyNode::String>());
  if ((a->scalarType == ScalarTC::True || a->scalarType == ScalarTC::False) &&
      (b->scalarType == ScalarTC::True || b->scalarType == ScalarTC::False))
    return a->scalarType == b->scalarType;
  if (a->scalarType != b->scalarType) return false;
  if (a->scalarType == ScalarTC::Null) return true;
  return a->data<AnyNode::String>() == b->data<AnyNode::String>();
}

static ZeString childPath(const AnyNode *node, ZuCSpan key = {})
{
  ZeString path;
  node->path(path);
  if (key) {
    if (path) path << '.';
    path << key;
  }
  return path;
}

[[noreturn]] static void mergeError(const AnyNode *node)
{
  throw ZfYAML_EXCEPT(ZfYAMLError::badMerge(childPath(node)));
}

static void merge_(
    ZuPtr<AnyNode> &dst, ZuPtr<AnyNode> &&src, AnyNode *parent)
{
  if (dst->has<AnyNode::String>()) {
    if (dst->scalarType == ScalarTC::Null) mergeError(dst);
    dst = ZuMv(src);
    dst->parent = parent;
    return;
  }
  if (dst->has<AnyNode::Object>()) {
    if (!src->has<AnyNode::Object>()) mergeError(dst);
    auto &d = dst->data<AnyNode::Object>();
    auto &s = src->data<AnyNode::Object>();
    for (auto &f: s) {
      int i = fld(d, f.p<0>());
      if (i < 0) {
	f.p<1>()->parent = dst;
	d.push(ZuMv(f));
      } else {
	merge_(d[i].p<1>(), ZuMv(f.p<1>()), dst);
      }
    }
    return;
  }
  if (!src->has<AnyNode::Array>()) mergeError(dst);
  auto &d = dst->data<AnyNode::Array>();
  auto &s = src->data<AnyNode::Array>();
  // Schema arrays are normally short; linear membership preserves order and
  // avoids allocating a second index for each allOf merge.
  for (auto &v: s) {
    bool found = false;
    for (auto &dv: d) if (same(dv, v)) { found = true; break; }
    if (!found) {
      v->parent = dst;
      d.push(ZuMv(v));
    }
  }
}

struct FSlot {
  ZuPtr<AnyNode>	*direct = nullptr;
  unsigned	frame = UINT_MAX;
};

using FSnapshot = ZtArray<
  AnyNode::Field, ZtArrayHeapID<"ZfYAML.FlattenSnapshot">>;

struct FFrame {
  FSnapshot	snapshot;
  ZuPtr<AnyNode>	all;
  ZuPtr<AnyNode>	incoming;
  FSlot		owner;
  AnyNode	*node = nullptr;
  unsigned	index = 0;
  unsigned	allIndex = 0;
  bool		oneOf = false;
  bool		anyOf = false;
};

namespace FOp { enum { Enter, Array, Object, All, Merge }; }

struct FTask {
  FSlot		slot;
  AnyNode	*parent = nullptr;
  unsigned	frame = UINT_MAX;
  unsigned	index = 0;
  int		op = FOp::Enter;
  bool		oneOf = false;
  bool		anyOf = false;
};

using FFrames = ZtArray<FFrame, ZtArrayHeapID<"ZfYAML.FlattenFrames">>;
using FTasks = ZtArray<FTask, ZtArrayHeapID<"ZfYAML.FlattenStack">>;

static ZuPtr<AnyNode> &fslot(FFrames &frames, FSlot slot)
{
  if (slot.direct) return *slot.direct;
  return frames[slot.frame].incoming;
}

static void flatten_(
    ZuPtr<AnyNode> &root, bool oneOf, bool anyOf)
{
  FFrames frames;
  FTasks tasks;
  tasks.push(FTask{FSlot{&root}, nullptr,
    UINT_MAX, 0, FOp::Enter, oneOf, anyOf});
  while (tasks) {
    FTask task{ZuMv(tasks[tasks.length() - 1])};
    tasks.pop();
    switch (task.op) {
      case FOp::Enter: {
	auto &node = fslot(frames, task.slot);
	if (node->has<AnyNode::String>()) {
	  if (node->scalarType == ScalarTC::Null)
	    node = newNode<AnyNode::Object>(task.parent);
	  else
	    node->parent = task.parent;
	  break;
	}
	node->parent = task.parent;
	if (node->has<AnyNode::Array>()) {
	  tasks.push(FTask{task.slot, task.parent, UINT_MAX, 0, FOp::Array});
	  break;
	}
	FFrame frame{
	  {}, {}, {}, task.slot, node, 0, 0, task.oneOf, task.anyOf};
	// Object.entries() snapshots both field order and values before mutation.
	// Unique tree ownership therefore requires cloning the iteration snapshot.
	for (auto &f: node->data<AnyNode::Object>())
	  frame.snapshot.push(
	    AnyNode::Field{f.p<0>(), clone_(f.p<1>(), nullptr)});
	unsigned fi = frames.length();
	frames.push(ZuMv(frame));
	tasks.push(FTask{{}, nullptr, fi, 0, FOp::Object});
      } break;

      case FOp::Array: {
	auto &node = fslot(frames, task.slot);
	auto &a = node->data<AnyNode::Array>();
	if (task.index >= a.length()) break;
	unsigned i = task.index++;
	tasks.push(task);
	tasks.push(FTask{FSlot{&a[i]}, node});
      } break;

      case FOp::Object: {
	auto &frame = frames[task.frame];
	if (frame.index >= frame.snapshot.length()) {
	  frame.snapshot.null();
	  frame.all = {};
	  break;
	}
	auto &o = frame.node->data<AnyNode::Object>();
	auto &entry = frame.snapshot[frame.index++];
	AnyNode::String key{entry.p<0>()};
	int i = fld(o, key);
	if (key == "allOf" && entry.p<1>()->has<AnyNode::Array>()) {
	  if (i >= 0) erase_(o, i);
	  frame.all = ZuMv(entry.p<1>());
	  frame.allIndex = 0;
	  tasks.push(task);
	  tasks.push(FTask{{}, nullptr, task.frame, 0, FOp::All});
	  break;
	}
	if ((key == "oneOf" || key == "anyOf") &&
	    entry.p<1>()->has<AnyNode::Array>()) {
	  bool enabled = key == "oneOf" ? frame.oneOf : frame.anyOf;
	  auto &a = entry.p<1>()->data<AnyNode::Array>();
	  if (enabled) {
	    if (i >= 0) erase_(o, i);
	    if (a) {
	      auto p = childPath(frame.node, key);
	      throw ZfYAML_EXCEPT(ZfYAMLError::badAlternative(p, key));
	    }
	    tasks.push(task);
	    break;
	  }
	  if (i < 0) {
	    o.push(AnyNode::Field{key, ZuMv(entry.p<1>())});
	    i = o.length() - 1;
	  } else {
	    o[i].p<1>() = ZuMv(entry.p<1>());
	  }
	  tasks.push(task);
	  tasks.push(FTask{FSlot{&o[i].p<1>()}, frame.node,
	    UINT_MAX, 0, FOp::Enter, key == "oneOf", key == "anyOf"});
	  break;
	}
	if (i < 0) {
	  o.push(AnyNode::Field{key, ZuMv(entry.p<1>())});
	  i = o.length() - 1;
	} else {
	  o[i].p<1>() = ZuMv(entry.p<1>());
	}
	tasks.push(task);
	tasks.push(FTask{FSlot{&o[i].p<1>()}, frame.node});
      } break;

      case FOp::All: {
	auto &frame = frames[task.frame];
	auto &a = frame.all->data<AnyNode::Array>();
	if (frame.allIndex >= a.length()) {
	  frame.all = {};
	  break;
	}
	frame.incoming = ZuMv(a[frame.allIndex++]);
	tasks.push(task);
	tasks.push(FTask{{}, nullptr, task.frame, 0, FOp::Merge});
	tasks.push(FTask{FSlot{nullptr, task.frame}});
      } break;

      case FOp::Merge: {
	auto &frame = frames[task.frame];
	auto &owner = fslot(frames, frame.owner);
	merge_(owner, ZuMv(frame.incoming), frame.node->parent);
      } break;
    }
  }
}

ZuPtr<AnyNode> flatten(ZuPtr<AnyNode> &&root, bool oneOf, bool anyOf)
{
  if (!root) return {};
  flatten_(root, oneOf, anyOf);
  return ZuMv(root);
}

} // ZfYAML
