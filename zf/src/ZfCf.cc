//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdlib.h>
#include <string.h>

#include <zlib/ZfCf.hh>

#include <zlib/ZtLocalArray.hh>

namespace ZfCf {

using Args = ZtArray<AnyNode::String, ZtArrayHeapID<"ZfCf.Args">>;
using ArgSpans = ZtArray<ZuCSpan, ZtArrayHeapID<"ZfCf.ArgSpans">>;

unsigned Scan::position(ZuCSpan at, unsigned offset)
{
  if (m_state.span.data() && at.data())
    offset += unsigned(at.data() - m_state.span.data());
  if (offset > m_state.span.length()) offset = m_state.span.length();
  while (m_state.offset < offset) {
    if (m_state.span[m_state.offset++] == '\n') {
      ++m_state.line;
      m_state.lineOffset = m_state.offset;
    }
  }
  return offset;
}

void Scan::fail(ZuCSpan at, unsigned offset)
{
  if (m_state.error.failed) return;
  offset = position(at, offset);
  m_state.error.offset = offset;
  m_state.error.line = m_state.line;
  m_state.error.column = offset - m_state.lineOffset + 1;
  m_state.error.ch =
    offset < m_state.span.length() ? m_state.span[offset] : 0;
  m_state.error.failed = true;
}

static unsigned nonSpace(ZuCSpan span)
{
  unsigned i = 0;
  while (i < span.length() && isspace__(span[i])) ++i;
  return i;
}

bool Scan::appendDefine(ZuCSpan key, AnyNode::String &out)
{
  if (!m_defines) return false;
  auto node = m_defines->findPtr(key);
  if (!node) return false;
  const auto &value = node->val();
  out.append(value.data(), value.length());
  return true;
}

void Scan::setDefine(ZuCSpan key, ZuCSpan value)
{
  if (auto node = m_defines->findPtr(key)) {
    node->val() = value;
    return;
  }
  m_defines->add(DefKey{key}, DefVal{value});
}

namespace TokenState { enum { Bare, Single, Double }; }

template <bool Key>
static bool delimiter(char c)
{
  if (isspace__(c)) return true;
  if constexpr (Key)
    return c == ':';
  else
    return c == ',' || c == ')' || c == ']' || c == '}';
}

template <bool Key>
int Scan::eos(ZuCSpan span, AnyNode::String &out)
{
  auto n = span.length();
  if (!n) {
    fail(span);
    return -1;
  }

  unsigned state = TokenState::Bare;
  unsigned i = 0;
  bool token = false;

  // exact-copy fast path
  while (i < n) {
    char c = span[i];
    if (delimiter<Key>(c)) break;
    if (c == '\\' || c == '\'' || c == '"')
      goto slow;
    if constexpr (!Key)
      if (c == '$') goto slow;
    ++i;
  }
  if (i) {
    out.append(span.data(), i);
    return int(i);
  }
  if (delimiter<Key>(span[0])) return -1;

slow:
  if (i) out.append(span.data(), i);
  token = i;
  while (i < n) {
    char c = span[i];
    if (state == TokenState::Bare && delimiter<Key>(c)) break;

    if (c == '\\') {
      token = true;
      if (++i >= n) {
	fail(span, i);
	return -1;
      }
      c = span[i++];
      switch (c) {
	case 'b': out << '\b'; break;
	case 'f': out << '\f'; break;
	case 'n': out << '\n'; break;
	case 'r': out << '\r'; break;
	case 't': out << '\t'; break;
	case 'u': {
	  using Fmt = ZuFmt::Hex<1, ZuFmt::Right<4>>;
	  uint16_t utf[2];
	  unsigned nUTF = 1;
	  auto scanHex = [&span, &i, n](uint16_t &u) {
	    if (i + 4 > n) return false;
	    if (ZuBoxed(u).scan<Fmt>(span.data() + i) != 4) return false;
	    i += 4;
	    return true;
	  };
	  if (!scanHex(utf[0])) {
	    fail(span, i);
	    return -1;
	  }
	  if (utf[0] >= 0xd800 && utf[0] <= 0xdbff) {
	    if (i + 2 > n || span[i] != '\\' || span[i + 1] != 'u') {
	      fail(span, i);
	      return -1;
	    }
	    i += 2;
	    if (!scanHex(utf[1])) {
	      fail(span, i);
	      return -1;
	    }
	    nUTF = 2;
	  }
	  ZuSpan<const uint16_t> input{utf, nUTF};
	  auto l = ZuUTF<char, uint16_t>::len(input);
	  if (!l) {
	    fail(span, i);
	    return -1;
	  }
	  auto o = out.length();
	  out.length(o + l);
	  if (ZuUTF<char, uint16_t>::cvt({out.data() + o, l}, input) != l) {
	    fail(span, i);
	    return -1;
	  }
	  break;
	}
	default: out << c; break;
      }
      continue;
    }

    switch (state) {
      case TokenState::Bare:
	if (c == '\'') {
	  state = TokenState::Single;
	  token = true;
	  ++i;
	  continue;
	}
	if (c == '"') {
	  state = TokenState::Double;
	  token = true;
	  ++i;
	  continue;
	}
	break;
      case TokenState::Single:
	if (c == '\'') {
	  state = TokenState::Bare;
	  ++i;
	  continue;
	}
	break;
      case TokenState::Double:
	if (c == '"') {
	  state = TokenState::Bare;
	  ++i;
	  continue;
	}
	break;
    }

    if constexpr (!Key) {
      if (state != TokenState::Single &&
	  c == '$' && i + 1 < n && span[i + 1] == '{') {
	unsigned begin = i + 2;
	unsigned end = begin;
	if (begin >= n || !isalpha__(span[begin])) {
	  fail(span, begin);
	  return -1;
	}
	while (end < n && isword__(span[end])) ++end;
	if (end >= n || span[end] != '}') {
	  fail(span, end);
	  return -1;
	}
	ZuCSpan name{span.data() + begin, end - begin};
	if (!appendDefine(name, out)) {
	  auto env = ZtLocalString(ZtString<>, name.length() + 1);
	  env << name;
	  if (auto value = ::getenv(env)) out.append(value, strlen(value));
	}
	token = true;
	i = end + 1;
	continue;
      }
    }

    out << c;
    token = true;
    ++i;
  }

  if (state != TokenState::Bare || !token) {
    fail(span, i);
    return -1;
  }
  out.length(out.length());
  return int(i);
}

static int eoc(ZuCSpan span)
{
  if (!span || span[0] != '#') return -1;
  unsigned i = 1, n = span.length();
  while (i < n && span[i] != '\n') ++i;
  if (i < n) ++i;
  return int(i);
}

static unsigned skipTrivia(ZuCSpan span)
{
  unsigned total = 0;
  for (;;) {
    auto i = nonSpace(span);
    span.offset(i);
    total += i;
    auto n = eoc(span);
    if (n < 0) return total;
    span.offset(n);
    total += n;
  }
}

template <char Close>
static ZuTuple<int, char> eod(ZuCSpan span)
{
  unsigned n = span.length();
  for (unsigned o = 0; o < n; o++) {
    char c = span[o];
    if (c == ',') return {int(o + 1), ','};
    if (c == '#') return {int(o), '#'};
    if constexpr (Close)
      if (c == Close) return {int(o + 1), Close};
    if (!isspace__(c)) return {-1, -1};
  }
  if constexpr (Close)
    return {-1, -1};
  else
    return {int(n), 0};
}

ZuTuple<int, int> Scan::bov(ZuCSpan span)
{
  unsigned o = 0;
  while (o < span.length() && isspace__(span[o])) ++o;
  if (o >= span.length()) return {-1, -1};
  switch (span[o]) {
    case '[': return {int(o + 1), ValueTC::Array};
    case '{': return {int(o + 1), ValueTC::Object};
    case ']':
    case '}':
    case ',':
    case ':': return {-1, -1};
    default: break;
  }
  return {int(o), ValueTC::String};
}

ZuTuple<int, ZuPtr<AnyNode>>
Scan::eov_Array(ZuCSpan span, AnyNode *parent)
{
  unsigned total = 0;
  auto node = newNode<AnyNode::Array>(parent);
  auto &array = node->data;
  for (;;) {
    auto ev = eov(span, node.ptr());
    if (ev.p<0>() >= 0) {
      array.push(ZuMv(ev.p<1>()));
      span.offset(ev.p<0>());
      total += ev.p<0>();
    }
    auto bd = bod<']'>(span);
    if (bd.p<0>() < 0) {
      fail(span, nonSpace(span));
      return {-1, nullptr};
    }
    span.offset(bd.p<0>());
    total += bd.p<0>();
    if (bd.p<1>() == ']') return {int(total), ZuMv(node)};
  }
}

ZuTuple<int, ZuPtr<AnyNode>>
Scan::eov(ZuCSpan span, AnyNode *parent)
{
  auto bv = bov(span);
  int prefix = bv.p<0>();
  if (prefix < 0) return {-1, nullptr};
  span.offset(prefix);
  switch (bv.p<1>()) {
    case ValueTC::Array: {
      auto ev = eov_Array(span, parent);
      if (ev.p<0>() < 0) return {-1, nullptr};
      ev.p<0>() += prefix;
      return ev;
    }
    case ValueTC::Object: {
      auto node = newNode<AnyNode::Object>(parent);
      int o = eov_Object(span, node.ptr(), false);
      if (o < 0) return {-1, nullptr};
      return {prefix + o, ZuMv(node)};
    }
    case ValueTC::String: {
      auto node = newNode<AnyNode::String>(parent);
      int o = eos<false>(span, node->data);
      if (o < 0) return {-1, nullptr};
      return {prefix + o, ZuMv(node)};
    }
  }
  return {-1, nullptr};
}

int Scan::eod(ZuCSpan span, AnyNode *node)
{
  unsigned length = span.length();
  unsigned n = 0;
  while (n < length && span[n] != '\n') ++n;
  unsigned next = n + (n < length);
  if (n < 4 || span[0] != '%' || !isalpha__(span[1])) {
    fail(span, n > 1 ? 1 : n);
    return -1;
  }

  unsigned i = 2;
  while (i < n && isword__(span[i])) ++i;
  ZuCSpan directive{span.data() + 1, i - 1};
  if (i >= n || span[i++] != '(') {
    fail(span, i < n ? i : n);
    return -1;
  }

  auto args = ZtLocalArray(Args, 4);
  bool afterComma = false;
  for (;;) {
    while (i < n && isspace__(span[i])) ++i;
    if (i >= n) {
      fail(span, i);
      return -1;
    }
    if (span[i] == ')') {
      if (afterComma) {
	fail(span, i);
	return -1;
      }
      ++i;
      break;
    }

    auto arg = args.push(AnyNode::String{});
    int o = eos<false>({span.data() + i, n - i}, *arg);
    if (o < 0) return -1;
    i += o;
    afterComma = false;

    while (i < n && isspace__(span[i])) ++i;
    if (i >= n) {
      fail(span, i);
      return -1;
    }
    if (span[i] == ')') {
      ++i;
      break;
    }
    if (span[i++] != ',') {
      fail(span, i - 1);
      return -1;
    }
    afterComma = true;
  }

  while (i < n && isspace__(span[i])) ++i;
  if (i < n && span[i] == '#') i = n;
  if (i < n) {
    fail(span, i);
    return -1;
  }

  if (directive == "define") {
    if (args.length() != 2 || !m_defines) {
      fail(span);
      return -1;
    }
    setDefine(args[0], args[1]);
    return int(next);
  }
  if (!m_pctFn) {
    fail(span);
    return -1;
  }

  auto argSpans = ZtLocalArray(
    ArgSpans, args.length(), args.length());
  for (unsigned j = 0; j < args.length(); j++) argSpans[j] = args[j];
  ZuSpan<const ZuCSpan> argSpan{argSpans.data(), argSpans.length()};
  if (!m_pctFn(*this, directive, argSpan,
    [this, node](ZuCSpan span) {
      auto outerState = m_state;
      m_state = {span};
      int o = eov_Object(span, node, true);
      bool ok = o >= 0;
      auto tail = span;
      if (ok) {
	tail.offset(o);
	auto i = skipTrivia(tail);
	if (i < tail.length()) {
	  fail(tail, i);
	  ok = false;
	}
      }
      auto expansionError = m_state.error;
      m_state = outerState;
      if (expansionError.failed) m_state.error = expansionError;
      return ok;
    })) {
    fail(span);
    return -1;
  }
  return int(next);
}

ZuTuple<int, bool> Scan::bok(ZuCSpan span, AnyNode *node)
{
  unsigned total = 0;
  for (;;) {
    auto o = nonSpace(span);
    span.offset(o);
    total += o;
    auto offset = position(span);
    if (!span || span[0] == '}') return {int(total), false};

    if (span[0] == '#') {
      o = eoc(span);
      if (o < 0) {
	fail(span);
	return {-1, false};
      }
      span.offset(o);
      total += o;
      continue;
    }
    if (span[0] == ',') {
      span.offset(1);
      ++total;
      continue;
    }
    if (span[0] != '%') return {int(total), true};
    if (offset != m_state.lineOffset) {
      fail(span);
      return {-1, false};
    }

    int n = eod(span, node);
    if (n < 0) return {-1, false};
    span.offset(n);
    total += n;
  }
}

int Scan::eov_Object(ZuCSpan span, AnyNode *node, bool root)
{
  unsigned total = 0;
  bool close = !root;
  auto &object = node->data<AnyNode::Object>();

  if (root) {
    auto o = nonSpace(span);
    span.offset(o);
    total += o;
    if (!span) return int(total);
    if (span[0] == '{') {
      close = true;
      span.offset(1);
      ++total;
    }
  }

  for (;;) {
    auto bk = bok(span, node);
    int o = bk.p<0>();
    if (o < 0) return -1;
    span.offset(o);
    total += o;

    if (bk.p<1>()) {
      AnyNode::String key;
      int ek = eos<true>(span, key);
      if (ek < 0) {
	fail(span);
	return -1;
      }
      span.offset(ek);
      total += ek;
      o = boc(span);
      if (o < 0) {
	fail(span, nonSpace(span));
	return -1;
      }
      span.offset(o);
      total += o;

      auto ev = eov(span, node);
      if (ev.p<0>() < 0) {
	fail(span, nonSpace(span));
	return -1;
      }
      object.push(AnyNode::Field{ZuMv(key), ZuMv(ev.p<1>())});
      span.offset(ev.p<0>());
      total += ev.p<0>();
    }

    auto bd = close ? ZfCf::eod<'}'>(span) : ZfCf::eod<0>(span);
    if (bd.p<0>() < 0) {
      fail(span, nonSpace(span));
      return -1;
    }
    span.offset(bd.p<0>());
    total += bd.p<0>();
    if (bd.p<1>() == '#') continue;
    if (bd.p<1>() != ',') return int(total);
  }
}

ZuTuple<int, ZuPtr<const AnyNode>> Scan::scan()
{
  if (ZuUnlikely(!m_state.span.data())) {
    fail(m_state.span);
    throw ZfCf_EXCEPT(badSyntax(
      m_state.error.line, m_state.error.column,
      m_state.error.offset, m_state.error.ch));
  }
  auto node = newNode<AnyNode::Object>(nullptr);
  int o = eov_Object(m_state.span, node.ptr(), true);
  if (o < 0 || m_state.error.failed) {
    if (!m_state.error.failed) fail(m_state.span);
    throw ZfCf_EXCEPT(badSyntax(
      m_state.error.line, m_state.error.column,
      m_state.error.offset, m_state.error.ch));
  }
  auto tail = m_state.span;
  tail.offset(o);
  auto i = skipTrivia(tail);
  if (i < tail.length()) {
    fail(tail, i);
    throw ZfCf_EXCEPT(badSyntax(
      m_state.error.line, m_state.error.column,
      m_state.error.offset, m_state.error.ch));
  }
  return {int(m_state.span.length()), ZuMv(node)};
}

ZuTuple<int, ZuPtr<const AnyNode>> scan(
  ZuCSpan span, PctFn pctFn, ZmRef<Defines> defines)
{
  return Scan{span, ZuMv(pctFn), ZuMv(defines)}.scan();
}

} // ZfCf
