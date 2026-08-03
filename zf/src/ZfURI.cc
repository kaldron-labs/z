//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZfURI.hh>

namespace ZfURI {

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
  auto r = ZuPercent::Codec<PercentPath>::decode(span);
  return {r.out, r.in, r.term};
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
  auto r = ZuPercent::Codec<PercentQuery>::decode(span);
  return {r.out, r.in, r.term};
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

ZuTuple<int, ZuPtr<AnyNode>> scan(ZuSpan<char> span)
{
  ZuPtr<AnyNode> root = newNode<AnyNode::Object>();

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
	  auto node = path(root, index);
	  if (!node || !string(*node, val)) goto bad;
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
      ZuPtr<AnyNode> *slot = &root;
      while (key) {
	auto sk = scanKey(key); // {offset, span}
	auto offset = sk.p<0>();
	if (offset < 0) goto bad;
	key.offset(offset);
	const auto &keyPart = sk.p<1>();
	if (!keyPart) break;
	auto c = keyPart[0];
	if (c >= '0' && c <= '9')
	  slot = elem(*slot, ZuBox<unsigned>(keyPart));
	else
	  slot = field(*slot, keyPart);
	if (!slot) goto bad;
      }
      if (!string(*slot, val)) goto bad;
    }
    // adjust end if there is trailing data in span
    if (span) end = span.begin();

    // return root node
    return {int(end - begin), ZuMv(root)};
  }

bad:
  return {-1, nullptr};
}

} // ZfURI
