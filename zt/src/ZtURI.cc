//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZtURI.hh>

namespace ZtURI {

// returns the number of leading '/' characters
unsigned skip(ZuSpan<char> span) {
  unsigned i, n = span.length();
  for (i = 0; i < n && span[i] == '/'; i++);
  return i;
}

// find end of path component, decoding percent-encoding in-place
// - returns {output, input, terminator}
// - terminator is the terminating character (normally '?' or '/')
// - terminator is 0 if the end of the span was reached
// - output is the number of bytes resulting from unescaping (<= input),
// - input is the number of bytes consumed, including any terminator
// - output is -1 on error

ZuTuple<int, int, char> eoc(ZuSpan<char> span)
{
  unsigned n = span.length();

  if (ZuUnlikely(n < 1)) goto bad;

  // fast path - no decoding/mutation
  unsigned i;
  for (i = 0; i < n; i++) {
    char c = span[i];
    if (ZuUnlikely(c == '%')) goto slow;
    if (ZuUnlikely(c == '/' || c == '?' || escaped(c))) {
      span[i] = 0;
      return {i, i + 1, c};
    }
  }
  return {i, i, 0};

slow:
  // slow path - span[i] == '%' || span[i] == '+' is a precondition
  {
    unsigned o = i;
    while (i < n) {
      char c = span[i];
      if (ZuUnlikely(c == '%')) {
	if (ZuUnlikely(++i > n - 2)) goto bad;
	auto h = hex(span[i++]);
	auto l = hex(span[i++]);
	if (ZuUnlikely(h < 0 || l < 0)) goto bad;
	span[o++] = char((h<<4) | l);
	continue;
      }
      if (ZuUnlikely(c == '/' || c == '?' || escaped(c))) {
	++i;
	memset(&span[o], 0, i - o); // terminate and pad with zeros
	return {o, i, c};
      }
      ++i;
      span[o++] = c; // o < i is guaranteed
    }
    return {o, i, 0};
  }

bad:
  return {-1};
}

#if 0
// find beginning of query

int boq(ZuCSpan span)
{
  unsigned n = span.length();

  if (ZuUnlikely(!n)) return -1;

  char c;

  for (unsigned o = 0; o < n; o++) {
    c = span[o];
    if (ZuLikely(c == '?')) return o + 1;
    if (isspace__(c)) return -1;
  }
  return -1;
}
#endif

// find end of string, decoding percent-encoding in-place

ZuTuple<int, int, char> eos(ZuSpan<char> span)
{
  unsigned n = span.length();

  if (ZuUnlikely(n < 1)) goto bad;

  // fast path - no decoding/mutation
  unsigned i;
  for (i = 0; i < n; i++) {
    char c = span[i];
    if (ZuUnlikely(c == '%' || c == '+')) goto slow;
    if (ZuUnlikely(escaped(c))) {
      span[i] = 0;
      return {i, i + 1, c};
    }
  }
  return {i, i, 0};

slow:
  // slow path - span[i] == '%' || span[i] == '+' is a precondition
  {
    unsigned o = i;
    while (i < n) {
      char c = span[i];
      if (ZuUnlikely(c == '+')) {
	++i;
	span[o++] = ' ';
	continue;
      }
      if (ZuUnlikely(c == '%')) {
	if (ZuUnlikely(++i > n - 2)) goto bad;
	auto h = hex(span[i++]);
	auto l = hex(span[i++]);
	if (ZuUnlikely(h < 0 || l < 0)) goto bad;
	span[o++] = char((h<<4) | l);
	continue;
      }
      if (ZuUnlikely(escaped(c))) {
	++i;
	memset(&span[o], 0, i - o); // terminate and pad with zeros
	return {o, i, c};
      }
      ++i;
      span[o++] = c; // o < i is guaranteed
    }
    return {o, i, 0};
  }

bad:
  return {-1};
}

// scankey() consumes part of a potentially composite key
// - returns {next, span}
//   - int next is offset to the next part, -1 if scan failed
//   - ZuCSpan span is the scanned part of the key
// - if span is zero-length, the key was "[]"
// - if the span is a number, the current node is implied to be an array,
//   otherwise it's an object field
//
//   "a"		{1, "a"}
//   "a[]"		{1, "a"}
//     "[]"		  {2, ""}
//   "a[0]"		{1, "a"}
//     "[0]"		  {3, "0"}
//   "a.b"		{2, "a"}
//     "b"		  {1, "b"}
//   "a.0.b.0"		{2, "a"}
//     "0.b.0"		  {2, "0"}
//     "b.0"		  {2, "b"}
//     "0"		  {1, "0"}
//   "a[0][b][0]"	{1, "a"}
//     "[0][b][0]"	{3, "0"}
//     "[b][0]"		{3, "b"}
//     "[0]"		{3, "0"}

ZuTuple<int, ZuCSpan> scanKey(ZuCSpan key)
{
  unsigned e = key.length();
  if (!e) return {-1};
  auto c = key[0];

  if (c == '[') {
    unsigned i = 0;
    while (++i < e) {
      c = key[i];
      if (c == ']') return {i + 1, ZuCSpan(&key[1], i - 1)};
    }
    return {-1};
  }

  if (c == '.') return {-1};

  unsigned i = 0;
  while (++i < e) {
    c = key[i];
    if (c == '[') break;
    if (c == '.') return {i + 1, ZuCSpan(&key[0], i)};
  }
  return {i, ZuCSpan(&key[0], i)};
}

// scan() scans endpoint parameters and query strings from a URI

ZuTuple<int, ZuPtr<Node>> scan(ZuSpan<char> span)
{
  ZuPtr<Node> root = new Node{Node::Object{}};

  if (ZuUnlikely(!span)) return {0, ZuMv(root)};

  {
    // remember begin and end
    auto begin = span.begin();
    auto end = span.end();

    int o;

    // parse endpoint path
    {
      span.offset(skip(span)); // ignore any number of leading '/'
      ZuBox<uint8_t> index = 0;
      char c;
      while (span) {
	auto s = eoc(span);
	if (ZuUnlikely((o = s.p<0>()) < 0)) goto bad;
	if (o) {
	  ZuSpan<char> val(&span[0], unsigned(o));
	  root->field(ZuCArray<4>() << '_' << index)->string(val);
	}
	span.offset(s.p<1>());
	if ((c = s.p<2>()) != '/') break;
	++index;
      }
      if (c && c != '?') goto bad;
    }

    // initialize top-level root node - which is always an object
    while (span) {
      // scan the key
      auto s = eos(span);
      if (ZuUnlikely((o = s.p<0>()) < 0)) goto bad;
      if (ZuUnlikely(s.p<2>() != '=')) goto bad;
      ZuCSpan key(&span[0], unsigned(o));
      span.offset(s.p<1>());

      // scan the value
      s = eos(span);
      if (ZuUnlikely((o = s.p<0>()) < 0)) goto bad;
      auto c = s.p<2>();
      if (ZuUnlikely(c && c != '&' && c != ' ')) goto bad;
      ZuSpan<char> val(&span[0], unsigned(o));
      span.offset(s.p<1>());

      // scan within the key, descend to the leaf node, set the value
      Node *node = root;
      while (key) {
	auto sk = scanKey(key); // {offset, span}
	auto offset = sk.p<0>();
	if (offset < 0) goto bad;
	key.offset(offset);
	const auto &keyPart = sk.p<1>();
	if (!keyPart) break;
	auto c = keyPart[0];
	if (c >= '0' && c <= '9')
	  node = node->elem(ZuBox<unsigned>(keyPart));
	else
	  node = node->field(keyPart);
	if (!node) goto bad;
      }
      node->string(val);
    }
    // adjust end if there is trailing data in span
    if (span) end = span.begin();

    // return root node
    return {int(end - begin), ZuMv(root)};
  }

bad:
  return {-1, nullptr};
}

} // ZtURI
