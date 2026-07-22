//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdlib.h>
#include <string.h>

#include <zlib/ZvNewCf.hh>

namespace ZvNewCf {

static bool appendDefine(
    Defines *defines, ZuCSpan key, AnyNode::String &out) {
  if (!defines) return false;
  auto node = defines->findPtr(key);
  if (!node) return false;
  const auto &value = node->val();
  out.append(value.data(), value.length());
  return true;
}

static void setDefine(Defines *defines, ZuCSpan key, ZuCSpan value) {
  if (auto node = defines->findPtr(key)) {
    node->val() = value;
    return;
  }
  defines->add(DefKey{key}, DefVal{value});
}

namespace TokenState { enum { Bare, Single, Double }; }

template <bool Key>
static bool delimiter(char c) {
  if (isspace__(c)) return true;
  if constexpr (Key)
    return c == ':';
  else
    return c == ',' || c == ')' || c == ']' || c == '}';
}

template <bool Key>
static int eos_(
  ZuCSpan span, AnyNode::String &out, Defines *defines)
{
  auto n = span.length();
  if (!n) return -1;

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
      if (++i >= n) return -1;
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
	  if (!scanHex(utf[0])) return -1;
	  if (utf[0] >= 0xd800 && utf[0] <= 0xdbff) {
	    if (i + 2 > n || span[i] != '\\' || span[i + 1] != 'u')
	      return -1;
	    i += 2;
	    if (!scanHex(utf[1])) return -1;
	    nUTF = 2;
	  }
	  ZuSpan<const uint16_t> input{utf, nUTF};
	  auto l = ZuUTF<char, uint16_t>::len(input);
	  if (!l) return -1;
	  auto o = out.length();
	  out.length(o + l);
	  if (ZuUTF<char, uint16_t>::cvt({out.data() + o, l}, input) != l)
	    return -1;
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
	if (begin >= n || !isalpha__(span[begin])) return -1;
	while (end < n && isword__(span[end])) ++end;
	if (end >= n || span[end] != '}') return -1;
	ZuCSpan name{span.data() + begin, end - begin};
	if (!appendDefine(defines, name, out)) {
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

  if (state != TokenState::Bare || !token) return -1;
  out.length(out.length());
  return int(i);
}

template <bool Key>
int eos(ZuCSpan span, AnyNode::String &out) {
  return eos_<Key>(span, out, nullptr);
}

template int eos<false>(ZuCSpan, AnyNode::String &);
template int eos<true>(ZuCSpan, AnyNode::String &);

static ZuTuple<int, bool> bok(ZuCSpan span) {
  for (unsigned o = 0, n = span.length(); o < n; o++) {
    char c = span[o];
    if (!isspace__(c))
      return c == ',' || c == '}' ? ZuTuple<int, bool>{-1, false} :
	ZuTuple<int, bool>{int(o), c == '%'};
  }
  return {-1, false};
}

static ZuTuple<int, char> eor(ZuCSpan span) {
  unsigned n = span.length();
  for (unsigned o = 0; o < n; o++) {
    char c = span[o];
    if (c == ',') return {int(o + 1), ','};
    if (!isspace__(c)) return {-1, -1};
  }
  return {int(n), 0};
}

ZuTuple<int, int> bov(ZuCSpan span) {
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

static int eov_Object_(
  ZuCSpan, AnyNode::Object &, bool, const PctFn &, Defines *);

static ZuTuple<int, ZuPtr<AnyNode>> eov(
    ZuCSpan span, const PctFn &pctFn, Defines *defines);

static void setParents(AnyNode *node, AnyNode *parent = nullptr) {
  node->parent = parent;
  if (node->has<AnyNode::Array>()) {
    for (auto &child: node->data<AnyNode::Array>())
      setParents(child.ptr(), node);
  } else if (node->has<AnyNode::Object>()) {
    for (auto &field: node->data<AnyNode::Object>())
      setParents(field.p<1>().ptr(), node);
  }
}

static ZuTuple<int, ZuPtr<AnyNode>> eov_Array_(
    ZuCSpan span, const PctFn &pctFn, Defines *defines) {
  unsigned total = 0;
  auto node = newNode<AnyNode::Array>();
  auto &array = node->data;
  for (;;) {
    auto ev = eov(span, pctFn, defines);
    if (ev.p<0>() >= 0) {
      array.push(ZuMv(ev.p<1>()));
      span.offset(ev.p<0>());
      total += ev.p<0>();
    }
    auto bd = bod<']'>(span);
    if (bd.p<0>() < 0) return {-1, nullptr};
    span.offset(bd.p<0>());
    total += bd.p<0>();
    if (bd.p<1>() == ']') return {int(total), ZuMv(node)};
  }
}

static ZuTuple<int, ZuPtr<AnyNode>> eov(
    ZuCSpan span, const PctFn &pctFn, Defines *defines) {
  auto bv = bov(span);
  int prefix = bv.p<0>();
  if (prefix < 0) return {-1, nullptr};
  span.offset(prefix);
  switch (bv.p<1>()) {
    case ValueTC::Array: {
      auto ev = eov_Array_(span, pctFn, defines);
      if (ev.p<0>() < 0) return {-1, nullptr};
      ev.p<0>() += prefix;
      return ev;
    }
    case ValueTC::Object: {
      auto node = newNode<AnyNode::Object>();
      int o = eov_Object_(span, node->data, false, pctFn, defines);
      if (o < 0) return {-1, nullptr};
      return {prefix + o, ZuMv(node)};
    }
    case ValueTC::String: {
      auto node = newNode<AnyNode::String>();
      int o = eos_<false>(span, node->data, defines);
      if (o < 0) return {-1, nullptr};
      return {prefix + o, ZuMv(node)};
    }
  }
  return {-1, nullptr};
}

static int eop(
    ZuCSpan span, AnyNode::Object &object, const PctFn &pctFn,
    Defines *defines) {
  unsigned n = span.length();
  if (n < 4 || span[0] != '%' || !isalpha__(span[1])) return -1;

  unsigned i = 2;
  while (i < n && isword__(span[i])) ++i;
  ZuCSpan directive{span.data() + 1, i - 1};
  if (i >= n || span[i++] != '(') return -1;

  AnyNode::String argData;
  ZtArray<unsigned> argLengths;
  bool afterComma = false;
  for (;;) {
    while (i < n && isspace__(span[i])) ++i;
    if (i >= n) return -1;
    if (span[i] == ')') {
      if (afterComma) return -1;
      ++i;
      break;
    }

    unsigned begin = argData.length();
    int o = eos_<false>(
      {span.data() + i, n - i}, argData, defines);
    if (o < 0) return -1;
    i += o;
    argLengths.push(argData.length() - begin);
    afterComma = false;

    while (i < n && isspace__(span[i])) ++i;
    if (i >= n) return -1;
    if (span[i] == ')') {
      ++i;
      break;
    }
    if (span[i++] != ',') return -1;
    afterComma = true;
  }

  ZtArray<ZuCSpan> args;
  args.length(argLengths.length());
  unsigned offset = 0;
  for (unsigned j = 0; j < args.length(); j++) {
    unsigned length = argLengths[j];
    args[j] = {argData.data() + offset, length};
    offset += length;
  }

  if (directive == "define") {
    if (args.length() != 2 || !defines) return -1;
    setDefine(defines, args[0], args[1]);
    return int(i);
  }
  if (!pctFn) return -1;

  ZuSpan<const ZuCSpan> argSpan{args.data(), args.length()};
  if (!pctFn(directive, argSpan,
    [&object, &pctFn, defines](ZuCSpan span) {
      int o = eov_Object_(span, object, true, pctFn, defines);
      if (o < 0) return false;
      span.offset(o);
      span.trim();
      return !span;
    })) return -1;
  return int(i);
}

static int eov_Object_(
    ZuCSpan span, AnyNode::Object &object, bool root, const PctFn &pctFn,
    Defines *defines) {
  unsigned total = 0;
  bool close = !root;

  if (root) {
    auto n = span.length();
    span.trim();
    auto o = n - span.length();
    total += o;
    if (!span) return int(total);
    if (span && span[0] == '{') {
      close = true;
      span.offset(1);
      ++total;
    }
  }

  for (;;) {
    auto bk = bok(span);
    int o = bk.p<0>();
    if (o >= 0) {
      span.offset(o);
      total += o;

      if (bk.p<1>()) {
	o = eop(span, object, pctFn, defines);
	if (o < 0) return -1;
	span.offset(o);
	total += o;
      } else {
	AnyNode::String key;
	int ek = eos<true>(span, key);
	if (ek < 0) return -1;
	span.offset(ek);
	total += ek;
	o = boc(span);
	if (o < 0) return -1;
	span.offset(o);
	total += o;

	auto ev = eov(span, pctFn, defines);
	if (ev.p<0>() < 0) return -1;
	object.push(AnyNode::Field{ZuMv(key), ZuMv(ev.p<1>())});
	span.offset(ev.p<0>());
	total += ev.p<0>();
      }
    }

    auto bd = close ? bod<'}'>(span) : eor(span);
    if (bd.p<0>() < 0) return -1;
    span.offset(bd.p<0>());
    total += bd.p<0>();
    if (bd.p<1>() != ',') return int(total);
  }
}

ZuTuple<int, ZuPtr<AnyNode>> eov_Array(ZuCSpan span) {
  auto v = eov_Array_(span, PctFn{}, nullptr);
  if (v.p<1>()) setParents(v.p<1>());
  return v;
}

ZuTuple<int, ZuPtr<AnyNode>> eov_Object(ZuCSpan span, bool root) {
  auto node = newNode<AnyNode::Object>();
  int o = eov_Object_(span, node->data, root, PctFn{}, nullptr);
  if (o < 0) return {-1, nullptr};
  setParents(node);
  return {o, ZuMv(node)};
}

ZuTuple<int, ZuPtr<const AnyNode>> scan(
  ZuCSpan span, PctFn pctFn, ZmRef<Defines> defines) {
  if (ZuUnlikely(!span.data())) return {-1, nullptr};
  auto node = newNode<AnyNode::Object>();
  int o = eov_Object_(span, node->data, true, pctFn, defines);
  if (o < 0) return {-1, nullptr};
  auto tail = span;
  tail.offset(o);
  tail.trim();
  if (tail) return {-1, nullptr};
  setParents(node);
  return {int(span.length()), ZuMv(node)};
}

} // ZvNewCf
